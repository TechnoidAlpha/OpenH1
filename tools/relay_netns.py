#!/usr/bin/env python3
"""Internet play's relays end to end (port/linux/src/p2p_relay.c), on one Linux computer with no root: network
namespaces in a user namespace make a small internet (a bridge, 203.0.113.0/24, with a delay on each of its ports),
an MQTT broker and two STUN servers on it (this file's own), a relay of halo --relay, and a host and a joiner each
behind a symmetric NAT (iptables MASQUERADE --random: a port of its own for each destination, and only answers let
in), whose direct connection fails.

    python3 tools/relay_netns.py --game build/linux_x86_64/halo --maps ~/halo-x86-maps/maps [--seconds 30]

runs itself again inside `unshare -Urnm`, has the joiner join the host's debug.network_test game by its invite
through the relay (both name it in relays.txt), both playing scripted bots (debug.test_input), and prints how the
joiner reached the host, how long the join took, its pings (the latency meter's), the relay's lines, and
net_compare.py's judgement of the two copies' logs. Exit status 0 when the joiner joined through the relay and
net_compare.py passed.

    python3 tools/relay_netns.py --front [--joiner-game <OpenCE's halo>] ...

instead has the host register with the relay as its front (p2p_front.c: network.front, server.front_ports, its
publisher key in the front's front_operators.txt), names no relays anywhere, and has the joiner (another build's,
OpenCE's for one, given its invite as halo://join/...) reach the host at the front's public port; the host plays
Legacy (OpenCE's netcode) then. Exit status 0 when the joiner joined at the front's port and net_compare.py passed.
"""

import argparse
import asyncio
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BRIDGE_NET = "203.0.113"
BROKER = f"{BRIDGE_NET}.10"
STUN_SECOND = f"{BRIDGE_NET}.11"
RELAY = f"{BRIDGE_NET}.20"
DELAY_MS = 10

# ---------- an MQTT 3.1.1 broker: what the game's signalling uses (no will, QoS 0 deliveries)


def _length(value):
    out = bytearray()
    while True:
        byte = value % 128
        value //= 128
        out.append(byte | (128 if value else 0))
        if not value:
            return bytes(out)


def _topic_matches(pattern, topic):
    parts, names = pattern.split("/"), topic.split("/")
    for index, part in enumerate(parts):
        if part == "#":
            return True
        if index >= len(names) or (part != "+" and part != names[index]):
            return False
    return len(parts) == len(names)


class Broker:
    def __init__(self):
        self.clients = {}
        self.retained = {}

    async def client(self, reader, writer):
        subscriptions = set()
        try:
            while True:
                header = await reader.readexactly(1)
                multiplier, remaining = 1, 0
                while True:
                    byte = (await reader.readexactly(1))[0]
                    remaining += (byte & 127) * multiplier
                    multiplier *= 128
                    if not byte & 128:
                        break
                body = await reader.readexactly(remaining) if remaining else b""
                kind = header[0] >> 4
                if kind == 1:
                    name_length = struct.unpack(">H", body[:2])[0]
                    level = body[2 + name_length]
                    if level != 4:
                        writer.write(bytes([0x20, 2, 0, 1]))
                        await writer.drain()
                        return
                    writer.write(bytes([0x20, 2, 0, 0]))
                    self.clients[writer] = subscriptions
                elif kind == 8:
                    identifier = body[:2]
                    offset, granted = 2, bytearray()
                    while offset < len(body):
                        length = struct.unpack(">H", body[offset:offset + 2])[0]
                        topic = body[offset + 2:offset + 2 + length].decode("utf-8", "replace")
                        offset += 2 + length + 1
                        subscriptions.add(topic)
                        granted.append(0)
                    writer.write(bytes([0x90]) + _length(2 + len(granted)) + identifier + bytes(granted))
                    for topic, payload in list(self.retained.items()):
                        if any(_topic_matches(pattern, topic) for pattern in subscriptions):
                            writer.write(self._publish(topic, payload, True))
                elif kind == 10:
                    offset = 2
                    while offset < len(body):
                        length = struct.unpack(">H", body[offset:offset + 2])[0]
                        subscriptions.discard(body[offset + 2:offset + 2 + length].decode("utf-8", "replace"))
                        offset += 2 + length
                    writer.write(bytes([0xB0, 2]) + body[:2])
                elif kind == 3:
                    qos, retain = (header[0] >> 1) & 3, header[0] & 1
                    length = struct.unpack(">H", body[:2])[0]
                    topic = body[2:2 + length].decode("utf-8", "replace")
                    offset = 2 + length
                    if qos:
                        writer.write(bytes([0x40, 2]) + body[offset:offset + 2])
                        offset += 2
                    payload = body[offset:]
                    if retain:
                        if payload:
                            self.retained[topic] = payload
                        else:
                            self.retained.pop(topic, None)
                    message = self._publish(topic, payload, False)
                    for other, patterns in list(self.clients.items()):
                        if any(_topic_matches(pattern, topic) for pattern in patterns):
                            other.write(message)
                elif kind == 12:
                    writer.write(bytes([0xD0, 0]))
                elif kind == 14:
                    return
                await writer.drain()
        except (asyncio.IncompleteReadError, ConnectionError, IndexError, struct.error):
            pass
        finally:
            self.clients.pop(writer, None)
            writer.close()

    @staticmethod
    def _publish(topic, payload, retain):
        name = topic.encode()
        body = struct.pack(">H", len(name)) + name + payload
        return bytes([0x30 | (1 if retain else 0)]) + _length(len(body)) + body


def serve_broker(address):
    async def main():
        broker = Broker()
        server = await asyncio.start_server(broker.client, address, 1883)
        async with server:
            await server.serve_forever()
    asyncio.run(main())


def serve_stun(address):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((address, 3478))
    while True:
        data, (host, port) = sock.recvfrom(2048)
        if len(data) < 20 or data[:2] != b"\x00\x01":
            continue
        cookie, transaction = data[4:8], data[8:20]
        xport = port ^ 0x2112
        xaddress = bytes(a ^ b for a, b in zip(socket.inet_aton(host), cookie))
        attribute = struct.pack(">HHBBH", 0x0020, 8, 0, 1, xport) + xaddress
        sock.sendto(struct.pack(">HH", 0x0101, len(attribute)) + cookie + transaction + attribute, (host, port))


# ---------- the namespaces


def sh(*command, check=True):
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode:
        raise RuntimeError(f"{' '.join(command)}: {result.stderr.strip()}")
    return result


class Internet:
    """the bridge, and machines on it: open ones, and ones behind a symmetric NAT"""

    def __init__(self):
        os.makedirs("/run/netns", exist_ok=True)
        sh("ip", "netns", "add", "inet")
        sh("ip", "-n", "inet", "link", "add", "br0", "type", "bridge")
        sh("ip", "-n", "inet", "link", "set", "br0", "up")
        self.count = 0
        self.port("svc", [BROKER, STUN_SECOND])

    def port(self, namespace, addresses, delay=True):
        """namespace on the bridge, with these addresses (the first its own)"""
        self.count += 1
        sh("ip", "netns", "add", namespace)
        sh("ip", "-n", namespace, "link", "set", "lo", "up")
        outside, inside = f"p{self.count}", f"wan{self.count}"
        sh("ip", "link", "add", outside, "type", "veth", "peer", "name", inside)
        sh("ip", "link", "set", outside, "netns", "inet")
        sh("ip", "link", "set", inside, "netns", namespace)
        sh("ip", "-n", "inet", "link", "set", outside, "master", "br0", "up")
        for address in addresses:
            sh("ip", "-n", namespace, "addr", "add", f"{address}/24", "dev", inside)
        sh("ip", "-n", namespace, "link", "set", inside, "up")
        if delay:
            # (each way's delay, on the bridge's side toward the machine)
            sh("ip", "netns", "exec", "inet", "tc", "qdisc", "add", "dev", outside, "root", "netem", "delay",
               f"{DELAY_MS}ms")
        return inside

    def behind_nat(self, name, public, lan):
        """name behind a symmetric NAT router (name-nat) at public"""
        router = f"{name}-nat"
        wan = self.port(router, [public])
        sh("ip", "netns", "add", name)
        sh("ip", "-n", name, "link", "set", "lo", "up")
        sh("ip", "link", "add", f"{name}-lan", "type", "veth", "peer", "name", "eth0")
        sh("ip", "link", "set", f"{name}-lan", "netns", router)
        sh("ip", "link", "set", "eth0", "netns", name)
        sh("ip", "-n", router, "addr", "add", f"{lan}.1/24", "dev", f"{name}-lan")
        sh("ip", "-n", router, "link", "set", f"{name}-lan", "up")
        sh("ip", "-n", name, "addr", "add", f"{lan}.2/24", "dev", "eth0")
        sh("ip", "-n", name, "link", "set", "eth0", "up")
        sh("ip", "-n", name, "route", "add", "default", "via", f"{lan}.1")
        sh("ip", "netns", "exec", router, "sysctl", "-qw", "net.ipv4.ip_forward=1")
        sh("ip", "netns", "exec", router, "iptables", "-t", "nat", "-A", "POSTROUTING", "-o", wan, "-j",
           "MASQUERADE", "--random")
        # (nothing in but answers: conntrack's filter is each destination's own, a symmetric NAT's)
        sh("ip", "netns", "exec", router, "iptables", "-A", "FORWARD", "-i", wan, "-m", "conntrack", "!",
           "--ctstate", "ESTABLISHED,RELATED", "-j", "DROP")
        return f"{lan}.2"

    def close(self):
        result = sh("ip", "netns", "list", check=False)
        for line in result.stdout.splitlines():
            sh("ip", "netns", "delete", line.split()[0], check=False)


# ---------- the games


class Machine:
    def __init__(self, name, namespace, folder, game, maps, relays=()):
        self.name, self.namespace, self.game = name, namespace, game
        self.folder = folder / name
        (self.folder / "data").mkdir(parents=True)
        (self.folder / "data" / "maps").symlink_to(maps)
        (self.folder / "brokers.txt").write_text(f"{BROKER}:1883\n")
        (self.folder / "relays.txt").write_text("".join(f"{relay}\n" for relay in relays))
        self.log = self.folder / "out.txt"
        self.process = None

    def start(self, arguments=(), **settings):
        environment = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
        for key in ("DISPLAY", "WAYLAND_DISPLAY"):
            environment.pop(key, None)
        environment.update({
            "HALO_DATA_ROOT": str(self.folder / "data"), "HALO_SAVE_ROOT": str(self.folder / "saves"),
            "HALO_CONFIG_FILE": str(self.folder / "config.toml"), "HALO_LOG_ROOT": str(self.folder),
            "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "HALO_NO_AUDIO": "1",
            "HALO_NULL_RENDERER": "1", "HALO_HIDDEN_WINDOW": "1", "HALO_NO_CLIPBOARD": "1",
            "HALO_UPDATE_AUTO": "false", "HALO_DISCORD_APPLICATION": "", "HALO_NET_JOIN_FROM_CLIPBOARD": "0",
            "HALO_NET_ONLINE": "true", "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_PUBLIC_LOBBY": "false",
            "HALO_NET_STUN": f"{BROKER}:3478,{STUN_SECOND}:3478",
            "HALO_NET_BROKERS_FILE": str(self.folder / "brokers.txt"),
            "HALO_NET_RELAYS_FILE": str(self.folder / "relays.txt"),
            "HALO_NET_LISTINGS": "brokers", "HALO_NETWORK_TEST_NAME": self.name, "HALO_LATENCY_METER": "true",
        })
        environment.update({key: str(value) for key, value in settings.items()})
        command = ["ip", "netns", "exec", self.namespace, self.game, *arguments]
        self.process = subprocess.Popen(command, cwd=self.folder, env=environment, stdin=subprocess.DEVNULL,
                                        stdout=open(self.log, "w"), stderr=subprocess.STDOUT)
        return self

    def text(self):
        return self.log.read_text(errors="replace") if self.log.exists() else ""

    def wait_for(self, pattern, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            match = re.search(pattern, self.text())
            if match:
                return match
            if self.process.poll() is not None:
                return None
            time.sleep(0.25)
        return None

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def scenario_front(folder, game, maps, seconds, joiner_game):
    """the host behind a symmetric NAT registers with a front (halo --relay with server.front_ports) on the
    internet; a joiner behind another symmetric NAT, of joiner_game (OpenCE's, say), joins through the front's
    public port, knowing nothing of fronts; both play seconds with scripted bots, judged by net_compare.py"""
    internet = Internet()
    services = [subprocess.Popen(["ip", "netns", "exec", "svc", sys.executable, __file__, flag, address])
                for flag, address in (("--stun", BROKER), ("--stun", STUN_SECOND), ("--broker", BROKER))]
    time.sleep(1)
    machines = []
    try:
        internet.behind_nat("host", f"{BRIDGE_NET}.101", "10.1.0")
        internet.behind_nat("joiner", f"{BRIDGE_NET}.102", "10.2.0")
        internet.port("relay", [RELAY])
        host = Machine("host", "host", folder, game, maps)
        # (the host's publisher key, beside its config.toml; its public key goes to the front)
        environment = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
        environment["HALO_CONFIG_FILE"] = str(host.folder / "config.toml")
        keygen = subprocess.run([game, "--keygen"], capture_output=True, text=True, env=environment,
                                cwd=host.folder)
        key = re.search(r"public key: (ed25519:[0-9a-f]{64})", keygen.stdout)
        if not key:
            return {"error": "no publisher key", "keygen": (keygen.stdout + keygen.stderr).splitlines()}
        relay = Machine("relay", "relay", folder, game, maps)
        machines.append(relay)
        (relay.folder / "front_operators.txt").write_text(f"# the host\n{key.group(1)}\n")
        relay.start(["--relay"], HALO_NET_TUNNEL_PORT=2302, HALO_SERVER_FRONT_PORTS="2310-2311",
                    HALO_SERVER_FRONT_OPERATORS_FILE=str(relay.folder / "front_operators.txt"))
        if not relay.wait_for(r"Relay: front on UDP ports 2310 to 2311", 20):
            return {"error": "the front did not start", "relay_tail": relay.text().splitlines()[-20:]}
        bot = {"HALO_NETWORK_TEST_START": 8, "HALO_EXIT_AFTER": seconds + 200}
        machines.append(host)
        host.start(HALO_NETWORK_TEST="host:bloodgulch", HALO_TEST_INPUT="bot:1", HALO_NET_FRONT=f"{RELAY}:2302",
                   HALO_NET_GAME_MODE="legacy", **bot)
        if not host.wait_for(r"front \S+ registered this machine", 30):
            return {"error": "the host did not register", "host_tail": host.text().splitlines()[-30:]}
        invite = host.wait_for(r"network test: invite (\S+)", 60)
        if not invite:
            return {"error": "the host made no invite", "host_tail": host.text().splitlines()[-30:]}
        link = invite.group(1)
        if joiner_game != game:
            link = link.replace("openh1://", "halo://")
        joiner = Machine("joiner", "joiner", folder, joiner_game, maps)
        machines.append(joiner)
        joiner.start([link], HALO_NETWORK_TEST="join", HALO_TEST_INPUT="bot:2", HALO_NET_GAME_MODE="legacy", **bot)
        match = joiner.wait_for(r"connected to host \w+ at (\S+)", 45)
        result = {"reached": bool(match), "host_at": match.group(1) if match else None,
                  "via_front": bool(match) and match.group(1).endswith(":2310")}
        if match:
            deadline = time.time() + seconds + 120
            while time.time() < deadline and all(m.process.poll() is None for m in machines):
                ticks = [int(value) for value in re.findall(r"network test: tick (\d+)", joiner.text())]
                if ticks and (max(ticks) - min(ticks)) // 30 >= seconds + 4:
                    break
                time.sleep(1)
        result["crashed"] = [m.name for m in machines if m.process.poll() not in (None, 0)]
        result["front_log"] = [f"{m.name}: {line.split('Internet play: ', 1)[-1]}" for m in machines
                               for line in m.text().splitlines()
                               if ("Internet play: " in line and ("front" in line or "relay" in line or
                                                                  "connected to" in line)) or "Relay:" in line][:20]
        for machine in machines:
            machine.stop()
        result["relay_counts"] = relay.text().splitlines()[-3:]
        if match:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            import net_compare
            arguments = argparse.Namespace(seconds=seconds, skip=3, latency=False)
            judged = net_compare.judge_run(host.log, joiner.log, arguments, dict(net_compare.TOLERANCES))
            result["net_compare"] = "PASS" if not judged["divergences"] and not judged["problems"] else "FAIL"
            result["net_compare_lines"] = net_compare.describe(judged)
        return result
    finally:
        for machine in machines:
            machine.stop()
        for service in services:
            service.kill()
            service.wait()
        internet.close()


def scenario(folder, game, maps, seconds):
    """a host and a joiner, each behind a symmetric NAT, and a relay of halo --relay on the internet that both
    name in relays.txt: the joiner joins through it and both play seconds with scripted bots; then net_compare.py
    judges the two copies' logs as it judges its own games"""
    internet = Internet()
    services = [subprocess.Popen(["ip", "netns", "exec", "svc", sys.executable, __file__, flag, address])
                for flag, address in (("--stun", BROKER), ("--stun", STUN_SECOND), ("--broker", BROKER))]
    time.sleep(1)
    machines = []
    relay_address = f"{RELAY}:2302"
    try:
        internet.behind_nat("host", f"{BRIDGE_NET}.101", "10.1.0")
        internet.behind_nat("joiner", f"{BRIDGE_NET}.102", "10.2.0")
        internet.port("relay", [RELAY])
        relay = Machine("relay", "relay", folder, game, maps)
        machines.append(relay)
        relay.start(["--relay"], HALO_NET_TUNNEL_PORT=2302)
        if not relay.wait_for(r"Relay: relaying on UDP port 2302", 20):
            return {"error": "the relay did not start", "relay_tail": relay.text().splitlines()[-20:]}
        bot = {"HALO_NETWORK_TEST_START": 8, "HALO_EXIT_AFTER": seconds + 200}
        host = Machine("host", "host", folder, game, maps, [relay_address])
        machines.append(host)
        host.start(HALO_NETWORK_TEST="host:bloodgulch", HALO_TEST_INPUT="bot:1", **bot)
        invite = host.wait_for(r"network test: invite (\S+)", 60)
        if not invite:
            return {"error": "the host made no invite", "host_tail": host.text().splitlines()[-30:]}
        joiner = Machine("joiner", "joiner", folder, game, maps)
        machines.append(joiner)
        joiner.start([invite.group(1)], HALO_NETWORK_TEST="join", HALO_TEST_INPUT="bot:2", **bot)
        match = joiner.wait_for(r"connected to host \w+ at \S+( through a relay)? \((\d+) ms after the join", 45)
        result = {"reached": bool(match), "via": (match.group(1) or " directly").strip() if match else None,
                  "join_ms": int(match.group(2)) if match else None}
        if match:
            # (the game played: the joiner's ticks logged for seconds past its start)
            deadline = time.time() + seconds + 120
            while time.time() < deadline and all(m.process.poll() is None for m in machines):
                ticks = [int(value) for value in re.findall(r"network test: tick (\d+)", joiner.text())]
                if ticks and (max(ticks) - min(ticks)) // 30 >= seconds + 4:
                    break
                time.sleep(1)
        pings = [int(value) for value in re.findall(r"network test: ping own (-?\d+)", joiner.text())]
        result["pings_ms"] = [ping for ping in pings if ping >= 0][-10:]
        result["crashed"] = [m.name for m in machines if m.process.poll() not in (None, 0)]
        result["relay_log"] = [f"{m.name}: {line.split('Internet play: ', 1)[-1]}" for m in machines
                               for line in m.text().splitlines()
                               if ("Internet play: " in line and "relay" in line) or "Relay:" in line][:16]
        for machine in machines:
            machine.stop()
        result["relay_counts"] = relay.text().splitlines()[-3:]
        if match:
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            import net_compare
            arguments = argparse.Namespace(seconds=seconds, skip=3, latency=False)
            judged = net_compare.judge_run(host.log, joiner.log, arguments, dict(net_compare.TOLERANCES))
            result["net_compare"] = "PASS" if not judged["divergences"] and not judged["problems"] else "FAIL"
            result["net_compare_lines"] = net_compare.describe(judged)
        return result
    finally:
        for machine in machines:
            machine.stop()
        for service in services:
            service.kill()
            service.wait()
        internet.close()


def inside(arguments):
    folder = Path(tempfile.mkdtemp(prefix="relay-", dir=arguments.work))
    if arguments.front:
        result = scenario_front(folder, arguments.game, arguments.maps, arguments.seconds,
                                arguments.joiner_game or arguments.game)
    else:
        result = scenario(folder, arguments.game, arguments.maps, arguments.seconds)
    for key, value in result.items():
        if isinstance(value, list):
            print(f"{key}:")
            for line in value:
                print(f"  {line}")
        else:
            print(f"{key}: {value}")
    if arguments.front:
        return 0 if result.get("net_compare") == "PASS" and result.get("via_front") else 1
    return 0 if result.get("net_compare") == "PASS" and result.get("via") == "through a relay" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", default="build/linux_x86_64/halo")
    parser.add_argument("--maps", default=str(Path.home() / "halo-x86-maps/maps"))
    parser.add_argument("--seconds", type=int, default=30, help="seconds of play judged")
    parser.add_argument("--work", default=None)
    parser.add_argument("--front", action="store_true", help="the host behind a front (p2p_front.c)")
    parser.add_argument("--joiner-game", default=None, help="the joiner's executable (--front; OpenCE's, say)")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--broker", help=argparse.SUPPRESS)
    parser.add_argument("--stun", help=argparse.SUPPRESS)
    arguments = parser.parse_args()
    if arguments.broker:
        serve_broker(arguments.broker)
        return 0
    if arguments.stun:
        serve_stun(arguments.stun)
        return 0
    arguments.game = str(Path(arguments.game).resolve())
    arguments.maps = str(Path(arguments.maps).expanduser().resolve())
    if arguments.joiner_game:
        arguments.joiner_game = str(Path(arguments.joiner_game).expanduser().resolve())
    if not arguments.inside:
        work = tempfile.mkdtemp(prefix="relay-netns-")
        try:
            return subprocess.call(["unshare", "-Urnm", "--propagation", "private", "sh", "-c",
                                    "mount -t tmpfs none /run && exec \"$@\"", "-", sys.executable, __file__,
                                    "--inside", "--game", arguments.game, "--maps", arguments.maps, "--work", work,
                                    "--seconds", str(arguments.seconds)] +
                                   (["--front"] if arguments.front else []) +
                                   (["--joiner-game", arguments.joiner_game] if arguments.joiner_game else []))
        finally:
            if os.environ.get("RELAY_NETNS_KEEP"):
                print(f"kept {work}")
            else:
                shutil.rmtree(work, ignore_errors=True)
    return inside(arguments)


if __name__ == "__main__":
    sys.exit(main())
