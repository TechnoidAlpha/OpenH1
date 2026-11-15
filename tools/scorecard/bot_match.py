#!/usr/bin/env python3
"""A match run from the bot control, end to end (docs/bot_control.md, "Running a match from the command line"): a
dedicated server of this build (hidden, muted, loopback alone, never port 5150), a match started from the command line
(match start), run faster than real time (match speed), a team ordered, its end waited for and its results read
(match results --json). With --repeat the same match is played again on a new server from its seed and its results'
orders (match start --replay) and the two results must agree; --mutation seed plays the second from another seed,
which must part (the negative control).

    python3 tools/scorecard/bot_match.py --maps ~/goat-rig/maps                 one match, its results
    python3 tools/scorecard/bot_match.py --maps ~/goat-rig/maps --repeat        two, which must agree
    python3 tools/scorecard/bot_match.py --maps ~/goat-rig/maps --repeat --mutation seed   which must not

It needs the stock multiplayer maps (bloodgulch), so it runs on a test machine (goat-rig exec <branch> -- ...), not
in CI. Exit status: 0 when it did what it says (with --repeat: the results agree; with --mutation: they part), 1
otherwise.
"""

import argparse
import json
import os
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
# what two results must agree on (the clock's seconds, real_seconds, may not)
COMPARED = ("winner", "score", "ticks", "players", "teams", "orders", "seed")


def free_port():
    while True:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", 0))
            number = probe.getsockname()[1]
        if number not in (5150, 2302, 2303) and number > 20000:
            return number


class Server:
    """a dedicated server of the build in a folder of its own"""

    def __init__(self, game, maps, folder):
        self.game, self.folder = game, folder
        self.web = free_port()
        self.password = secrets.token_hex(12)
        (folder / "data").mkdir(parents=True)
        (folder / "home").mkdir()
        (folder / "data" / "maps").symlink_to(maps)
        for name in ("masters.txt", "brokers.txt", "relays.txt"):
            (folder / name).write_text("")
        home = folder / "home"
        self.env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
            "XDG_CONFIG_HOME": str(home), "XDG_DATA_HOME": str(home), "XDG_CACHE_HOME": str(home),
            "XDG_RUNTIME_DIR": str(home), "HALO_DATA_ROOT": str(folder / "data"), "HALO_SAVE_ROOT": str(folder / "saves"),
            "HALO_CONFIG_FILE": str(folder / "config.toml"), "SDL_AUDIO_DRIVER": "dummy", "SDL_VIDEO_DRIVER": "offscreen",
            "HALO_HIDDEN_WINDOW": "1", "HALO_VOLUME": "0", "HALO_NO_CLIPBOARD": "1", "HALO_PRIORITY": "inherit",
            "HALO_UPDATE_AUTO": "false", "HALO_UPDATE_ANSWER": "no", "HALO_SERVER_AUTO_UPDATE": "false",
            "HALO_NET_ADDRESS": "127.0.0.1", "HALO_NET_PORT": str(free_port()), "HALO_NET_BROADCAST": "127.255.255.255",
            "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_ONLINE": "false", "HALO_NET_PUBLIC_LOBBY": "false",
            "HALO_NET_MASTERS_FILE": str(folder / "masters.txt"), "HALO_NET_BROKERS_FILE": str(folder / "brokers.txt"),
            "HALO_NET_RELAYS_FILE": str(folder / "relays.txt"), "HALO_SERVER_MASTERS_FILE": str(folder / "masters.txt"),
            "HALO_SERVER_RELAYS_FILE": str(folder / "relays.txt"), "HALO_SERVER_MASTER_PUBLISH": "false",
            "HALO_NET_STUN": "127.0.0.9:3478", "HALO_DISCORD_APPLICATION": "", "HALO_RANDOM_SEED": "12345",
            "HALO_SERVER": "1", "HALO_SERVER_WEB_PORT": str(self.web), "HALO_SERVER_WEB_BIND": "127.0.0.1",
            "HALO_SERVER_WEB_PASSWORD": self.password, "HALO_SERVER_ROTATION": "bloodgulch:slayer",
            "HALO_SERVER_LOBBY_SECONDS": "1", "HALO_SERVER_POSTGAME_SECONDS": "600", "HALO_SERVER_EMPTY_SECONDS": "0",
        }
        self.log = open(folder / "server.log", "w")
        self.process = subprocess.Popen([str(game), "--server"], env=self.env, cwd=folder, stdout=self.log,
                                        stderr=subprocess.STDOUT)

    def bots(self, *words, check=True):
        """goath1 bots <words> --json against the server: its answer's JSON lines"""
        command = [str(self.game), "bots", "--http", f"127.0.0.1:{self.web}", "--password", self.password, *words,
                   "--json"]
        result = subprocess.run(command, env=self.env, capture_output=True, text=True, timeout=60)
        lines = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
        if check and (result.returncode or not lines or not lines[0].get("ok")):
            raise RuntimeError(f"bots {' '.join(words)}: {result.returncode} {result.stdout} {result.stderr}")
        return lines[0] if lines else {}

    def command(self, line):
        """a "bots <line> --json" over one HTTP session (the CLI signs in each time it runs, and the server allows
        so many sign-ins a minute): its answer's JSON"""
        import urllib.error
        import urllib.request

        def post(path, body):
            request = urllib.request.Request(f"http://127.0.0.1:{self.web}{path}", data=json.dumps(body).encode(),
                                             headers={"Content-Type": "application/json", **(
                                                 {"Authorization": f"Bearer {self.session}"} if self.session else {})})
            try:
                with urllib.request.urlopen(request, timeout=30) as answer:
                    return json.loads(answer.read())
            except urllib.error.HTTPError as refused:
                return json.loads(refused.read() or b"{}")
        if not getattr(self, "session", None):
            self.session = None
            self.session = post("/api/login", {"password": self.password, "by": "bot match"})["token"]
        answer = post("/api/command", {"line": f"bots {line} --json"})
        lines = [json.loads(text) for text in answer.get("lines", []) if text.startswith("{")]
        return lines[0] if lines else {}

    def stop(self):
        self.process.terminate()
        try:
            self.process.wait(10)
        except subprocess.TimeoutExpired:
            self.process.kill()
        self.log.close()


def play(game, maps, folder, seed, speed, replay=None, minutes=4, score=3):
    """one match: started, sped up, ordered (or its orders replayed), waited for; its results and the speed it ran"""
    server = Server(game, maps, folder)
    try:
        for _ in range(120):
            if server.process.poll() is not None:
                raise RuntimeError(f"the server stopped ({server.process.returncode}): {folder / 'server.log'}")
            try:
                if server.command("match status").get("ok"):
                    break
            except OSError:
                pass
            time.sleep(0.5)
        else:
            raise RuntimeError("the server's HTTP never answered")
        start = ["match", "start", "bloodgulch", "ctf", "--bots", "4", "--seed", str(seed), "--score", str(score),
                 "--time", str(minutes)]
        if replay:
            start += ["--replay", str(replay)]
        # (the server hosts its lobby a few seconds after its HTTP answers)
        for _ in range(60):
            if server.command(" ".join(start[:-2] if replay else start)).get("ok"):
                break
            time.sleep(1)
        else:
            raise RuntimeError("match start never took")
        if replay:
            # (the CLI's --replay: the start again, with the orders for their ticks)
            server.bots(*start)
        server.bots("match", "speed", str(speed))
        began = time.monotonic()
        ordered = bool(replay)
        while time.monotonic() - began < 600:
            status = server.command("match status")
            ours = status["mode"] == "ctf"
            if ours and status["state"] == "playing" and not ordered and status["tick"] > 300:
                # (the coach's turn: red takes the flag, blue holds its own)
                server.bots("order", "red", "attack", "enemy", "flag")
                server.bots("order", "blue", "defend", "our", "flag")
                knowledge = server.bots("knowledge", "--team", "red")
                print("red's view:", json.dumps(knowledge)[:600])
                ordered = True
            if ours and status["state"] == "over" and ordered:
                break
            time.sleep(2)
        else:
            raise RuntimeError("the match did not end in 600 s")
        results = server.bots("match", "results")
        status = server.command("match status")
        return results, status
    finally:
        server.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--game", default=str(ROOT / "build/linux_x86_64/halo"))
    parser.add_argument("--maps", default=str(Path.home() / "goat-rig/maps"))
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--speed", default="20")
    parser.add_argument("--repeat", action="store_true")
    parser.add_argument("--mutation", choices=("seed",))
    parser.add_argument("--keep", action="store_true", help="keep the servers' folders")
    args = parser.parse_args()
    work = Path(tempfile.mkdtemp(prefix="bot-match-"))
    try:
        first, status = play(Path(args.game), Path(args.maps), work / "first", args.seed, args.speed)
        (work / "first.json").write_text(json.dumps(first, indent=1))
        print(json.dumps(first))
        print(f"match: {first['seconds']} s of game in {first['real_seconds']} s ({first['seconds'] / max(first['real_seconds'], 0.1):.1f}x"
              f" real time; status measured {status.get('speed_measured')}x at speed {status.get('speed')})")
        if not args.repeat:
            return 0
        seed = args.seed + 1 if args.mutation == "seed" else args.seed
        second, _ = play(Path(args.game), Path(args.maps), work / "second", seed, args.speed, replay=work / "first.json")
        parts = [key for key in COMPARED if first.get(key) != second.get(key)]
        if args.mutation:
            print(f"--mutation {args.mutation}: the results part on {parts}" if parts else
                  "--mutation: the results agree: the check cannot see it")
            return 0 if parts else 1
        print("repeat: the two matches' results agree" if not parts else f"repeat: the results part on {parts}")
        if parts:
            print(json.dumps(second))
        return 0 if not parts else 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print("kept", work)


if __name__ == "__main__":
    sys.exit(main())
