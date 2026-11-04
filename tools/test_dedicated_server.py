"""The dedicated server (halo --server: docs/dedicated_server.md) with the game: its refusals and exit codes, a
rotation played by a joined client (lobby, game, postgame and the next entry), the HTTP API (status, players, kick,
ban and unban, say, the rotation), the events, standard input's commands, the owner's sign-in from loopback, a ban of
an address, drains (sv_stop empty, sv_restart game), its shutdown by quit, SIGTERM and SIGINT, and OpenH1 60 and 120
Tick games (server.game_mode) played by OpenH1 clients and an OpenCE-mode one in step with it. Needs the retail
maps in assets/maps and ninja linux_x86_64's build/linux_x86_64/halo (HALO_TEST_MAPS, HALO_TEST_GAME); every machine runs from a folder of its own under pytest's
tmp_path (its config.toml, data root, saves and home), on loopback addresses of its own, offline (system link), with
no window, sound, update check or Discord."""

import base64
import codecs
import hashlib
import json
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
# (OpenH1's: the 64-bit x86 build, its SDL beside it; HALO_TEST_GAME and
# HALO_TEST_MAPS name others, and HALO_TEST_NET_PORT a system link port of
# the run's own, to keep off another game's on the computer)
RETAIL_MAPS = Path(os.environ.get("HALO_TEST_MAPS", ROOT / "assets/maps"))
GAME = Path(os.environ.get("HALO_TEST_GAME", ROOT / "build/linux_x86_64/halo"))
SDL = GAME.parent / "libSDL3.so.0"
SERVER_ADDRESS = "127.0.0.201"
CLIENT_ADDRESSES = ("127.0.0.202", "127.0.0.203")
PASSWORD = "test-password-1"


def _needs_game(*maps):
    if not GAME.is_file():
        pytest.skip("needs ninja linux_x86_64's build/linux_x86_64/halo")
    for name in maps:
        if not (RETAIL_MAPS / f"{name}.map").is_file():
            pytest.skip(f"needs {name}.map in assets/maps")


def _free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Machine:
    """a machine's folder (bin/ with the game and its config.toml, data/, saves/, home/) and its process"""

    def __init__(self, base, name, maps=True):
        self.folder = base / name
        self.name = name
        for part in ("bin", "data", "saves", "home", "xdg"):
            (self.folder / part).mkdir(parents=True, exist_ok=True)
        shutil.copy(GAME, self.folder / "bin" / "halo")
        if SDL.is_file():
            shutil.copy(SDL, self.folder / "bin" / SDL.name)
        if maps:
            (self.folder / "data" / "maps").symlink_to(RETAIL_MAPS)
        self.process = None
        self.output = self.folder / "out.txt"

    def environment(self, **settings):
        environment = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
        environment.update({
            # (what marks the processes this test started, should one need stopping by hand)
            "HALO_TEST_MARKER": f"test_dedicated_server-{self.name}",
            "HALO_DATA_ROOT": str(self.folder / "data"), "HALO_SAVE_ROOT": str(self.folder / "saves"),
            "HOME": str(self.folder / "home"), "XDG_DATA_HOME": str(self.folder / "xdg"),
            "SDL_VIDEO_DRIVER": "dummy", "SDL_AUDIO_DRIVER": "dummy", "HALO_NO_AUDIO": "1",
            "HALO_UPDATE_AUTO": "false",
            "HALO_NET_ONLINE": "false", "HALO_DISCORD_APPLICATION": "", "HALO_NET_JOIN_FROM_CLIPBOARD": "0",
        })
        if os.environ.get("HALO_TEST_NET_PORT"):
            environment["HALO_NET_PORT"] = os.environ["HALO_TEST_NET_PORT"]
        environment.update({key: str(value) for key, value in settings.items()})
        return environment

    def start(self, arguments=(), **settings):
        self.log = open(self.output, "w")
        self.process = subprocess.Popen([str(self.folder / "bin" / "halo"), *arguments], cwd=self.folder,
                                        env=self.environment(**settings), stdin=subprocess.PIPE,
                                        stdout=self.log, stderr=subprocess.STDOUT, text=True)
        return self

    def run(self, arguments=(), timeout=30, **settings):
        """the game run to its end: (exit status, its output)"""
        result = subprocess.run([str(self.folder / "bin" / "halo"), *arguments], cwd=self.folder,
                                env=self.environment(**settings), stdin=subprocess.DEVNULL, capture_output=True,
                                text=True, errors="replace", timeout=timeout)
        return result.returncode, result.stdout + result.stderr

    def text(self):
        return self.output.read_text(errors="replace") if self.output.is_file() else ""

    def debug(self):
        path = self.folder / "data" / "debug.txt"
        return path.read_text(errors="replace") if path.is_file() else ""

    def send(self, line):
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def wait(self, timeout):
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return None

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        if self.process:
            self.log.close()


@pytest.fixture
def machines(tmp_path):
    made = []

    def make(name, maps=True):
        machine = Machine(tmp_path, name, maps)
        made.append(machine)
        return machine

    yield make
    for machine in made:
        machine.stop()


def _server_settings(web_port, **settings):
    values = {
        "HALO_NET_ADDRESS": SERVER_ADDRESS, "HALO_NET_BROADCAST": ",".join(CLIENT_ADDRESSES),
        "HALO_SERVER_WEB_PORT": web_port, "HALO_SERVER_WEB_PASSWORD": PASSWORD,
        "HALO_SERVER_LOBBY_SECONDS": 3, "HALO_SERVER_POSTGAME_SECONDS": 3, "HALO_SERVER_NAME": "Test Server",
        "HALO_SERVER_ROTATION": "bloodgulch:slayer, prisoner:slayer",
    }
    values.update(settings)
    return values


def _client(machine, address, name):
    return machine.start(HALO_NULL_RENDERER=1, HALO_NETWORK_TEST="join", HALO_NETWORK_TEST_NAME=name,
                         HALO_NET_ADDRESS=address, HALO_NET_BROADCAST=SERVER_ADDRESS, HALO_NO_AUDIO=1)


def _until(condition, timeout, what, machine=None):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = condition()
        if value:
            return value
        if machine and machine.process.poll() is not None:
            break
        time.sleep(0.5)
    detail = f"\n--- {machine.name}'s output:\n{machine.text()[-3000:]}" if machine else ""
    pytest.fail(f"{what} not within {timeout} s{detail}")


class Api:
    def __init__(self, port):
        self.base = f"http://127.0.0.1:{port}"
        self.token = None

    def call(self, method, path, body=None, token=True):
        request = urllib.request.Request(self.base + path, method=method,
                                         data=json.dumps(body).encode() if body is not None else None)
        if body is not None:
            request.add_header("Content-Type", "application/json")
        if token and self.token:
            request.add_header("Authorization", f"Bearer {self.token}")
        try:
            with urllib.request.urlopen(request, timeout=10) as answer:
                return answer.status, json.loads(answer.read() or b"null")
        except urllib.error.HTTPError as failure:
            text = failure.read()
            try:
                return failure.code, json.loads(text)
            except ValueError:
                return failure.code, text.decode(errors="replace")
        except (urllib.error.URLError, ConnectionError, TimeoutError):
            return None, None

    def login(self):
        status, answer = self.call("POST", "/api/login", {"password": PASSWORD}, token=False)
        assert status == 200 and answer["ok"], answer
        self.token = answer["token"]

    def status(self):
        status, answer = self.call("GET", "/api/status")
        return answer if status == 200 else None

    def events(self):
        status, answer = self.call("GET", "/api/events?after=0")
        assert status == 200, answer
        return answer["events"]


def _event_names(api):
    return [event.get("event") for event in api.events()]


# ---------- refusals

def test_version_and_help(machines):
    _needs_game()
    machine = machines("version", maps=False)
    status, output = machine.run(["--version"], HALO_CONFIG_FILE=machine.folder / "config.toml")
    assert status == 0, output
    assert re.search(r"network version \d+", output), output
    status, output = machine.run(["--help"], HALO_CONFIG_FILE=machine.folder / "config.toml")
    assert status == 0 and "--server" in output, output


def test_no_maps_exits_2(machines):
    _needs_game()
    machine = machines("nomaps", maps=False)
    started = time.monotonic()
    status, output = machine.run(["--server"], HALO_SERVER_WEB_PORT=0)
    assert status == 2, output
    assert time.monotonic() - started < 10, "the refusal took more than 10 s"
    assert "no usable maps" in output, output


def test_rotation_of_missing_maps_exits_2(machines):
    _needs_game("bloodgulch")
    machine = machines("missingmaps")
    status, output = machine.run(["--server"], HALO_SERVER_WEB_PORT=0, HALO_SERVER_ROTATION="nosuchmap:slayer")
    assert status == 2, output


def test_rotation_of_unknown_gametypes_exits_3(machines):
    _needs_game("bloodgulch")
    machine = machines("badrotation")
    status, output = machine.run(["--server"], HALO_SERVER_WEB_PORT=0,
                                 HALO_SERVER_ROTATION="bloodgulch:nosuchgametype, bloodgulch:slayer+score=-5")
    assert status == 3, output
    assert "nothing playable" in output, output


def test_public_web_port_needs_a_long_password(machines):
    _needs_game("bloodgulch")
    machine = machines("shortpassword")
    status, output = machine.run(["--server"], HALO_SERVER_WEB_PORT=_free_port(), HALO_SERVER_WEB_BIND="0.0.0.0",
                                 HALO_SERVER_WEB_PASSWORD="short")
    assert status == 3, output


def test_web_port_in_use_exits_4(machines):
    _needs_game("bloodgulch")
    machine = machines("portinuse")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as taken:
        taken.bind(("127.0.0.1", 0))
        taken.listen(1)
        status, output = machine.run(["--server"], HALO_SERVER_WEB_PORT=taken.getsockname()[1],
                                     HALO_SERVER_WEB_PASSWORD=PASSWORD, HALO_NET_ADDRESS=SERVER_ADDRESS,
                                     HALO_NET_BROADCAST=CLIENT_ADDRESSES[0])
    assert status == 4, output


# ---------- standard input, a read-only data root, signals

def test_quit_on_standard_input_with_a_read_only_data_root(machines, tmp_path):
    _needs_game("bloodgulch")
    machine = machines("readonly")
    logs = tmp_path / "logs"
    logs.mkdir()
    data = machine.folder / "data"
    port = _free_port()
    status_file = tmp_path / "status.json"
    # (read-only from the start: the server must not need to write there)
    data.chmod(0o555)
    try:
        before = sorted(path.name for path in data.iterdir())
        machine.start(["--server"], **_server_settings(port, HALO_LOG_ROOT=logs, HALO_SERVER_STATUS_FILE=status_file))
        _until(lambda: "server: ready" in machine.text(), 90, "server: ready", machine)
        machine.send("sv_status")
        _until(lambda: "lobby" in machine.text().split("server: ready", 1)[1], 15, "sv_status's answer", machine)
        # (the status file, by its host path, as /api/status gives it)
        _until(lambda: status_file.is_file(), 15, "the status file", machine)
        assert json.loads(status_file.read_text())["state"] == "lobby"
        machine.send("quit")
        assert machine.wait(15) == 0, machine.text()[-3000:]
        assert sorted(path.name for path in data.iterdir()) == before, "the server wrote to its read-only data root"
        assert (logs / "debug.txt").is_file(), "no debug.txt in paths.logs"
    finally:
        data.chmod(0o755)


def test_second_sigint_exits_130(machines):
    _needs_game("bloodgulch")
    machine = machines("sigint")
    machine.start(["--server"], **_server_settings(0))
    _until(lambda: "server: ready" in machine.text(), 90, "server: ready", machine)
    machine.process.send_signal(signal.SIGINT)
    # (the second once the first is taken: two at once are one pending signal)
    _until(lambda: "stopping (signal 2)" in machine.text(), 10, "the first SIGINT taken", machine)
    machine.process.send_signal(signal.SIGINT)
    assert machine.wait(15) == 130, machine.text()[-3000:]


# ---------- a rotation played

def test_rotation_moderation_and_shutdown(machines):
    """a client joins, plays the first entry (ended by sv_end_game), the postgame, then the second entry; the API
    kicks it, bans and unbans it, says a notice and refuses a bad rotation; SIGTERM shows the client the postgame
    and exits 0"""
    _needs_game("bloodgulch", "prisoner")
    port = _free_port()
    server = machines("server").start(["--server"], **_server_settings(port))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    status = api.status()
    assert status["name"] == "Test Server" and status["state"] == "lobby", status
    assert status["players"] == 0, "the host has a player of its own"

    # (with a mod on, which the client names to the host with its build)
    client_machine = machines("client")
    (client_machine.folder / "data" / "mods" / "testmod").mkdir(parents=True)
    mod_toml = client_machine.folder / "data" / "mods" / "testmod" / "mod.toml"
    mod_toml.write_text('name = "Test mod"\nversion = "1.2"\n')
    client = _client(client_machine, CLIENT_ADDRESSES[0], "Tester")
    _until(lambda: (api.status() or {}).get("state") == "playing", 120, "the first game", server)
    assert api.status()["map"] == "bloodgulch"
    players = api.call("GET", "/api/players")[1]["players"]
    assert [player["name"] for player in players] == ["Tester"], players
    # its machine's build, as the client told it (after its ready, reliably), and said once in the server's log
    _until(lambda: api.call("GET", "/api/players")[1]["players"][0].get("client"), 30, "the client's build", server)
    told = api.call("GET", "/api/players")[1]["players"][0]["client"]
    assert told["version"].startswith("OpenH1 Linux | ") and isinstance(told["build"], int), told
    assert told["flavor"] and re.fullmatch(r"([\w.-]+/[\w.-]+)?", told["repository"]), told
    assert told["mods"] == ["testmod 1.2"], told
    assert server.text().count("plays " + told["version"]) == 1, "the client's build not logged once"

    # the first game ended by a command, its postgame, the second entry's lobby and game
    server.send("sv_end_game")
    _until(lambda: "postgame" in _event_names(api), 30, "the postgame", server)
    _until(lambda: (api.status() or {}).get("map") == "prisoner" and api.status()["state"] == "playing", 120,
           "the second game, on prisoner", server)
    names = _event_names(api)
    for event in ("server_started", "lobby_opened", "player_joined", "game_started", "game_ended", "postgame"):
        assert event in names, f"no {event} event: {names}"
    assert names.count("game_started") >= 2, names
    _until(lambda: "prisoner" in client.debug(), 30, "the client on prisoner", client)

    # a notice, and a rotation entry the API refuses with nothing changed
    status, answer = api.call("POST", "/api/say", {"text": "hello from the test"})
    assert status == 200 and answer["ok"], answer
    rotation = api.call("GET", "/api/rotation")[1]
    status, answer = api.call("PUT", "/api/rotation", {"entries": ["bloodgulch:slayer", "nosuchmap:ctf"]})
    assert status == 400 and answer["error"]["code"] == "rejected", answer
    assert answer["error"]["rejected"][0]["index"] == 1, answer
    assert api.call("GET", "/api/rotation")[1]["entries"] == rotation["entries"], "a refused rotation changed it"

    # a kick by slot (the name checked), then a ban of an hour, which bans.txt keeps with its end
    slot = api.call("GET", "/api/players")[1]["players"][0]["slot"]
    status, answer = api.call("POST", f"/api/players/{slot}/kick", {"name": "Someone Else"})
    assert status != 200 or not answer.get("ok"), "a kick of another name than the slot's went through"
    status, answer = api.call("POST", f"/api/players/{slot}/kick", {"name": "Tester"})
    assert status == 200 and answer["ok"], answer
    _until(lambda: not api.call("GET", "/api/players")[1]["players"], 30, "the kicked player gone", server)
    assert "kick" in _event_names(api)
    # (the join test joins once: a new start of it joins again)
    client.stop()
    _client(client, CLIENT_ADDRESSES[0], "Tester")
    _until(lambda: api.call("GET", "/api/players")[1]["players"], 90, "the client back after its kick", server)
    slot = api.call("GET", "/api/players")[1]["players"][0]["slot"]
    status, answer = api.call("POST", f"/api/players/{slot}/ban",
                              {"name": "Tester", "duration": "1h", "reason": "a test"})
    assert status == 200 and answer["ok"], answer
    _until(lambda: not api.call("GET", "/api/players")[1]["players"], 30, "the banned player gone", server)
    bans = api.call("GET", "/api/bans")[1]["bans"]
    assert len(bans) == 1 and bans[0]["until"], bans
    assert "until=" in (server.folder / "data" / "bans.txt").read_text(errors="replace")
    client.stop()
    _client(client, CLIENT_ADDRESSES[0], "Tester")
    time.sleep(25)
    assert not api.call("GET", "/api/players")[1]["players"], "the banned client joined again"
    status, answer = api.call("DELETE", f"/api/bans/{bans[0]['n']}")
    assert status == 200 and answer["ok"], answer
    client.stop()
    _client(client, CLIENT_ADDRESSES[0], "Tester")
    _until(lambda: api.call("GET", "/api/players")[1]["players"], 120, "the client back after its unban", server)

    # SIGTERM in a game: the client shown the postgame, the server gone with exit code 0
    _until(lambda: (api.status() or {}).get("state") == "playing", 120, "a game to stop in", server)
    server.process.send_signal(signal.SIGTERM)
    assert server.wait(15) == 0, server.text()[-3000:]
    assert "stopped (exit code 0)" in server.text()
    assert not re.search(r"EXCEPTION|assert", server.debug()), "an exception or assert in the server's debug.txt"


def test_word_filter_keeps_a_name_out(machines):
    """with server.chat_filter "block", a player whose name has a word of the filter's list (streamer mode's) is
    refused at the door, and the server says why; the word is ROT13'd here, as in the filter's own tests"""
    _needs_game("bloodgulch")
    port = _free_port()
    server = machines("server").start(["--server"], **_server_settings(
        port, HALO_SERVER_CHAT_FILTER="block", HALO_SERVER_CHAT_FILTER_SWEARING="mild",
        HALO_SERVER_ROTATION="bloodgulch:slayer"))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    _client(machines("client"), CLIENT_ADDRESSES[0], codecs.decode("Shpxre", "rot13"))
    _until(lambda: "its name has words the server's filter keeps out" in server.debug(), 90, "the name refused",
           server)
    assert not api.call("GET", "/api/players")[1]["players"], "a player of a refused name joined"
    # (a name without them joins)
    _client(machines("other"), CLIENT_ADDRESSES[1], "Tester")
    _until(lambda: [player["name"] for player in api.call("GET", "/api/players")[1]["players"]] == ["Tester"], 90,
           "a player of a fine name in", server)


def _console(port, line):
    """a line run by a game's script console (debug.telnet_console)"""
    with socket.create_connection(("127.0.0.1", port), timeout=10) as console:
        console.settimeout(2)
        try:
            console.recv(4096)
        except socket.timeout:
            pass
        console.sendall(line.encode() + b"\r\n")
        time.sleep(1)


def test_an_admin_changes_the_map_from_the_game(machines):
    """a player's game proves its admin key for the server as it joins; its request to change the map is refused
    while the key is no admin's, and done once the owner makes it one (sv_admin add): the server's own sv_map, logged
    and an event with the admin's name and key id (docs/dedicated_server.md, "Admins in the game")"""
    _needs_game("bloodgulch", "prisoner")
    port = _free_port()
    server = machines("server").start(["--server"], **_server_settings(port, HALO_SERVER_ROTATION="bloodgulch:slayer"))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    console_port = _free_port()
    client = machines("client").start(HALO_NULL_RENDERER=1, HALO_NETWORK_TEST="join", HALO_NETWORK_TEST_NAME="Boss",
                                      HALO_NET_ADDRESS=CLIENT_ADDRESSES[0], HALO_NET_BROADCAST=SERVER_ADDRESS,
                                      HALO_TELNET_CONSOLE=1, HALO_TELNET_CONSOLE_PORT=console_port)
    _until(lambda: (api.status() or {}).get("state") == "playing", 120, "the game", server)
    _until(lambda: "proved admin key" in server.text(), 30, "the client's key proven", server)
    assert not api.call("GET", "/api/players")[1]["players"][0]["admin"], "an admin before the owner made one"

    # (no admin yet: the request reaches the server, which does nothing)
    _console(console_port, "admin map prisoner:slayer")
    time.sleep(5)
    assert api.status()["map"] == "bloodgulch" and "from the game" not in server.text(), "a player not an admin"

    # the owner makes the player an admin by the key their game proved
    server.send("sv_admin add Boss")
    _until(lambda: (server.folder / "data" / "admins.txt").is_file(), 15, "admins.txt", server)
    _until(lambda: api.call("GET", "/api/players")[1]["players"][0]["admin"], 15, "the player an admin", server)
    _until(lambda: "the server made this machine an admin" in client.debug() + client.text(), 15,
           "the admin's game told it is one", client)
    _console(console_port, "admin map prisoner:slayer")
    _until(lambda: (api.status() or {}).get("map") == "prisoner", 120, "the admin's map played", server)
    assert re.search(r"admin Boss \(key [0-9a-f]{16}\) ran \"sv_map prisoner:slayer\" from the game: done",
                     server.text()), server.text()[-3000:]
    command = next(event for event in api.events() if event.get("event") == "admin_command")
    assert command["name"] == "Boss" and command["ok"] and re.fullmatch(r"[0-9a-f]{16}", command["key_id"]), command


def test_empty_game_ends_and_join_in_progress(machines):
    """a game whose players leave ends after server.empty_seconds and the lobby opens again; a second client joins a
    game in progress"""
    _needs_game("bloodgulch")
    port = _free_port()
    server = machines("server").start(["--server"], **_server_settings(
        port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_SERVER_EMPTY_SECONDS=5))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    first = _client(machines("first"), CLIENT_ADDRESSES[0], "First")
    _until(lambda: (api.status() or {}).get("state") == "playing", 120, "the game", server)
    machines_by_name = {"second": _client(machines("second"), CLIENT_ADDRESSES[1], "Second")}
    _until(lambda: len(api.call("GET", "/api/players")[1]["players"]) == 2, 60, "the second client in the game",
           server)
    # (loaded, and playing: a game whose players all leave ends, a joiner still loading with it)
    _until(lambda: "local machine is loaded" in machines_by_name["second"].debug(), 60, "the second client loaded",
           machines_by_name["second"])
    time.sleep(5)
    first.stop()
    _until(lambda: len(api.call("GET", "/api/players")[1]["players"]) == 1, 60, "the first client gone", server)
    assert api.status()["state"] == "playing", "the game ended with a player in it"
    # the last client gone: the game ends as empty, and the lobby opens again
    machines_by_name["second"].stop()
    _until(lambda: any(event.get("event") == "game_ended" and event.get("reason") == "empty"
                       for event in api.events()), 90, "the empty game's end", server)
    _until(lambda: (api.status() or {}).get("state") == "lobby", 60, "the lobby again", server)
    # the next join starts a new game
    _client(machines("third"), CLIENT_ADDRESSES[0], "Third")
    _until(lambda: (api.status() or {}).get("state") == "playing", 120, "a new game for the next player", server)
    assert api.status()["games_hosted"] == 2, api.status()


def test_owner_from_loopback_and_a_drain_until_empty(machines, tmp_path):
    """the owner's password (from its file) signs in from loopback and sees addresses in the events; POST /api/bans
    bans an address with nobody of it present; sv_stop empty withdraws the server and refuses a new client, then stops
    with exit code 0 once the last player leaves"""
    _needs_game("bloodgulch")
    port = _free_port()
    owner_file = tmp_path / "owner-password"
    owner_file.write_text("owner-password-1\r\n")
    server = machines("server").start(["--server"], **_server_settings(
        port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_SERVER_WEB_OWNER_PASSWORD_FILE=owner_file,
        HALO_SERVER_INSTANCE="drain-test"))
    api, owner = Api(port), Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    status, answer = owner.call("POST", "/api/login", {"password": "owner-password-1"}, token=False)
    assert status == 200 and answer["role"] == "owner", answer
    owner.token = answer["token"]
    status = owner.status()
    assert status["instance"] == "drain-test" and status["pid"] == server.process.pid and status["drain"] is None, status
    assert {"owner_web", "ban_address", "stop", "event_addresses", "identity"} <= set(status["capabilities"]), status
    # (the owner's role alone stops the server; a ban needs no player present)
    status, answer = api.call("POST", "/api/stop", {"after": "empty"})
    assert status == 403 and answer["error"]["code"] == "forbidden", answer
    status, answer = api.call("POST", "/api/bans", {"ip": "198.51.100.77", "duration": "1h", "reason": "a test"})
    assert status == 200 and answer["ok"], answer
    assert api.call("GET", "/api/bans")[1]["bans"][answer["n"]]["ip"] == "198.51.100.77"

    first = _client(machines("first"), CLIENT_ADDRESSES[0], "First")
    _until(lambda: (api.status() or {}).get("state") == "playing", 120, "the game", server)
    joined = [event for event in owner.events() if event.get("event") == "player_joined"]
    assert joined and joined[0]["address"] == CLIENT_ADDRESSES[0] and "hwid" in joined[0], joined
    assert all("address" not in event for event in api.events() if event.get("event") == "player_joined")

    status, answer = owner.call("POST", "/api/stop", {"after": "empty", "notice": "maintenance"})
    assert status == 200 and answer["ok"], answer
    drain = owner.status()["drain"]
    assert drain["after"] == "empty" and drain["restart"] is False and drain["since"] > 0, drain
    assert any(event.get("event") == "drain" and event.get("after") == "empty" for event in owner.events())
    # a new machine is refused while the server drains
    second = _client(machines("second"), CLIENT_ADDRESSES[1], "Second")
    time.sleep(20)
    assert [player["name"] for player in api.call("GET", "/api/players")[1]["players"]] == ["First"]
    assert server.process.poll() is None, "the server stopped with a player on it"
    second.stop()
    # the last player gone: the server stops, exit code 0, its shutdown said to be the drain's
    first.stop()
    assert server.wait(60) == 0, server.text()[-3000:]
    assert "stopping (drained)" in server.text()


def test_a_drain_until_the_game_is_over_restarts(machines):
    """sv_restart game during a game: the game goes on to its end and its postgame, then the server stops with exit
    code 5"""
    _needs_game("bloodgulch")
    port = _free_port()
    server = machines("server").start(["--server"], **_server_settings(
        port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_SERVER_WEB_OWNER_PASSWORD="owner-password-1"))
    owner = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    status, answer = owner.call("POST", "/api/login", {"password": "owner-password-1"}, token=False)
    assert status == 200 and answer["role"] == "owner", answer
    owner.token = answer["token"]
    _client(machines("client"), CLIENT_ADDRESSES[0], "Player")
    _until(lambda: (owner.status() or {}).get("state") == "playing", 120, "the game", server)
    status, answer = owner.call("POST", "/api/stop", {"after": "game", "restart": True})
    assert status == 200 and answer["ok"], answer
    time.sleep(3)
    assert server.process.poll() is None and owner.status()["state"] == "playing", "the drain cut the game short"
    status, answer = owner.call("POST", "/api/command", {"line": "sv_end_game"})
    assert status == 200 and answer["ok"], answer
    assert server.wait(60) == 5, server.text()[-3000:]
    text = server.text()
    assert "stopping (drained)" in text and text.index("game: ended") < text.index("stopping (drained)"), text[-3000:]


def _cpu_seconds(pid):
    """a process's user and system time (/proc/<pid>/stat), in seconds"""
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def test_idle_server_spends_little_cpu(machines):
    """a server with nobody on it uses under 3 % of a core (it runs 10 frames a second)"""
    _needs_game("bloodgulch")
    if not Path("/proc/self/stat").is_file():
        pytest.skip("needs /proc")
    server = machines("idle").start(["--server"], **_server_settings(0))
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    time.sleep(5)
    started, used = time.monotonic(), _cpu_seconds(server.process.pid)
    time.sleep(30)
    share = (_cpu_seconds(server.process.pid) - used) / (time.monotonic() - started)
    assert share < 0.03, f"the idle server used {share:.1%} of a core"


def test_web_port_survives_junk(machines):
    """random bytes, heads cut short and left open, oversized heads and bodies, and bad WebSocket frames at the web
    port: the server keeps answering, and keeps running"""
    import random

    _needs_game("bloodgulch")
    port = _free_port()
    server = machines("junk").start(["--server"], **_server_settings(port))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    rng = random.Random(1)
    stalled = []
    for index in range(300):
        with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
            kind = index % 5
            if kind == 0:
                connection.sendall(bytes(rng.randrange(256) for _ in range(rng.randrange(1, 4096))))
            elif kind == 1:
                connection.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n" + b"X-Long: " + b"a" * 20000 + b"\r\n\r\n")
            elif kind == 2:
                connection.sendall(b"POST /api/login HTTP/1.1\r\nContent-Length: 999999\r\n\r\n" + b"{" * 70000)
            elif kind == 3:
                connection.sendall(b"GET /ws HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                                   b"Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                   b"Sec-WebSocket-Protocol: opence-rcon-1\r\n\r\n")
                connection.sendall(b"\x81\x7f" + b"\xff" * 8 + bytes(rng.randrange(256) for _ in range(64)))
            else:
                connection.sendall(b"GET /api/status?" + bytes(rng.choice(b"%/.\x00\xff?&=") for _ in range(200)) +
                                   b" HTTP/1.1\r\n\r\n")
            try:
                connection.recv(4096)
            except (socket.timeout, ConnectionError):
                pass
    for _ in range(4):
        stalled.append(socket.create_connection(("127.0.0.1", port), timeout=5))
        stalled[-1].sendall(b"GET /healthz HTTP/1.1\r\n")
    status, answer = api.call("GET", "/healthz", token=False)
    assert status == 200, (status, answer)
    time.sleep(12)
    for connection in stalled:
        connection.close()
    status, answer = api.call("GET", "/healthz", token=False)
    assert status == 200, (status, answer)
    assert server.process.poll() is None, server.text()[-3000:]
    server.send("quit")
    assert server.wait(15) == 0, server.text()[-3000:]


def test_settings_refuse_a_value_they_cannot_read(machines):
    """PUT /api/settings with a string it cannot read whole (300 characters and more, or one holding \\u0000): refused,
    the setting left as it was (it had been applied as the empty string, which took a game password away), as a
    value of 257 to 299 characters is; a key it cannot read refused with a message of its own"""
    _needs_game("bloodgulch")
    port = _free_port()
    server = machines("settings").start(["--server"], **_server_settings(port))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()

    def setting(name):
        status, answer = api.call("GET", "/api/settings")
        assert status == 200, answer
        return next(entry for entry in answer["settings"] if entry["name"] == name)

    status, answer = api.call("PUT", "/api/settings", {"server.motd": "Welcome", "server.password": "hunter2"})
    assert status == 200 and answer["applied"] == ["server.motd", "server.password"], answer
    assert setting("server.password")["set"]
    for value in ("a" * 300, "a" * 1000, "hi\u0000"):
        for name in ("server.motd", "server.password"):
            status, answer = api.call("PUT", "/api/settings", {name: value})
            assert status == 200 and answer["applied"] == [], (name, len(value), answer)
            assert answer["errors"][0]["name"] == name and "longer than 256 characters" in answer["errors"][0]["message"], \
                answer
    assert setting("server.motd")["value"] == "Welcome"
    assert setting("server.password")["set"], "the game password was taken away"
    status, answer = api.call("PUT", "/api/settings", {"server.motd": "a" * 260})
    assert answer["applied"] == [] and "longer than 256 characters" in answer["errors"][0]["message"], answer
    status, answer = api.call("PUT", "/api/settings", {"": 1})
    assert answer["applied"] == [] and answer["errors"][0]["code"] == "not_found", answer
    assert answer["errors"][0]["message"] == "a setting's name is a string of at most 63 characters", answer
    status, answer = api.call("PUT", "/api/settings", {"server.motd": "Hello"})
    assert answer["applied"] == ["server.motd"], answer
    assert setting("server.motd")["value"] == "Hello"
    server.send("quit")
    assert server.wait(15) == 0, server.text()[-3000:]


def _probe(prober):
    """halo --probe on the LAN, from the prober's machine: the game it prints"""
    result = subprocess.run([str(prober.folder / "bin" / "halo"), "--probe"], cwd=prober.folder,
                            env=prober.environment(HALO_NET_ADDRESS=CLIENT_ADDRESSES[0],
                                                   HALO_NET_BROADCAST=SERVER_ADDRESS, HALO_NET_ONLINE="false"),
                            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=90)
    assert result.returncode == 0, result.stdout + result.stderr[-2000:]
    return json.loads(result.stdout.strip().splitlines()[-1])


def test_probe_prints_the_server(machines):
    """halo --probe, on the LAN: the server's game as one line of JSON, nothing joined; an OpenH1 game's (the
    server's by default: server.game_mode) at 120, of the OpenH1 protocol"""
    _needs_game("prisoner")
    server = machines("probed").start(["--server"], **_server_settings(
        0, HALO_SERVER_NAME="Probe Me", HALO_SERVER_ROTATION="prisoner:ctf"))
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    prober = machines("prober")
    game = _probe(prober)
    assert game["ok"] and game["name"] == "Probe Me" and game["map"] == "prisoner", game
    assert game["gametype"] == "ctf" and game["dedicated"] and game["players"] == 0, game
    assert game["tick_rate"] == 120 and game["openh1_version"] > 0, game
    assert not (prober.folder / "bin" / "config.toml").exists(), "the probe wrote its settings"


# ---------- OpenH1 games (server.game_mode)

def _net_compare():
    """tools/net_compare.py, whose judging of the network test's logs the OpenH1 games' tests take"""
    import importlib.util

    spec = importlib.util.spec_from_file_location("net_compare", ROOT / "tools" / "net_compare.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _rates(machine, net_compare):
    """the rates a machine's network test logged it played at (each second's), its log's mixed-game lines"""
    return [line["rate"] for line in net_compare.parse_rates(machine.output)]


def _play_openh1_game(machines, rate, opence_client, shoot):
    """a server of OpenH1 <rate> Tick games (server.game_mode) and two headless clients joining it from its
    advertisement and playing it with the network test's bot: the second as OpenCE joins (debug.join_legacy, the
    tests' stand-in for an OpenCE machine) when opence_client. Each machine logs every second (the server its
    network test's "log", which hosts nothing), and with shoot (seconds; 0 for none) each client's player hits the
    next every so often, the server bringing them together first. The probe, the status and the events are checked; the
    server, the clients and the logs are returned once a minute of play is in them"""
    _needs_game("bloodgulch")
    port = _free_port()
    mode = f"openh1_{rate}"
    server = machines("server")
    # (in config.toml, not its environment variable, which would pin it: sv_set changes it below)
    (server.folder / "bin" / "config.toml").write_text(f'[server]\ngame_mode = "{mode}"\n')
    server.start(["--server"], **_server_settings(
        port, HALO_SERVER_ROTATION="bloodgulch:slayer+score=50", HALO_NETWORK_TEST="log",
        HALO_NETWORK_TEST_SHOOT=shoot))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    status = api.status()
    assert (status["game_mode"], status["tick_rate"], status["opence"]) == (mode, rate, True), status
    assert status["openh1_version"] >= 11 and "game_mode" in status["capabilities"], status
    assert f"hosting: OpenH1 {rate} Tick, OpenCE can join (server.game_mode)" in server.text(), server.text()[-2000:]
    started = next(event for event in api.events() if event["event"] == "server_started")
    assert started["game_mode"] == mode, started
    # (advertised as a mixed game is: OpenCE's network version, which OpenCE lists and joins, with the rate and the
    # OpenH1 protocol beside it)
    game = _probe(machines("prober"))
    assert game["dedicated"] and game["tick_rate"] == rate, game
    assert game["network_version"] == status["network_version"], game
    assert game["openh1_version"] == status["openh1_version"], game
    clients = []
    for index, name in enumerate(("Alpha", "Bravo")):
        settings = {"HALO_TEST_INPUT": f"bot:{index + 1}", "HALO_NETWORK_TEST_SHOOT": shoot}
        if opence_client and index == 1:
            settings["HALO_DEBUG_JOIN_LEGACY"] = "true"
        clients.append(machines(name.lower()).start(
            HALO_NULL_RENDERER=1, HALO_NETWORK_TEST="join", HALO_NETWORK_TEST_NAME=name,
            HALO_NET_ADDRESS=CLIENT_ADDRESSES[index], HALO_NET_BROADCAST=SERVER_ADDRESS, **settings))
    _until(lambda: len((api.call("GET", "/api/players")[1] or {}).get("players", [])) == 2 and
           (api.status() or {}).get("state") == "playing", 150, "both clients in the game", server)
    started = next(event for event in reversed(api.events()) if event["event"] == "game_started")
    assert (started["game_mode"], started["tick_rate"]) == (mode, rate), started
    # (a minute of both playing: the second client loads last)
    _until(lambda: "local machine is loaded" in clients[1].debug(), 90, "the second client loaded", clients[1])
    time.sleep(65)
    assert api.status()["state"] == "playing", api.status()
    return server, api, clients


def _judge_openh1_game(server, clients, rate, opence_client, shoot):
    """the logs of _play_openh1_game, judged as tools/net_compare.py judges a mixed game's (NETCODE.md, "OpenCE
    players in an OpenH1 game", "Comparing two builds"): each client's view of the players against the server's,
    the rate each machine played at, OpenH1's messages kept from an OpenCE client, the server's drift of each
    client's player (its run of a tick against the client's), and with shoot the clients' hits, none refused"""
    net_compare = _net_compare()
    tolerances = dict(net_compare.TOLERANCES)
    args = type("Args", (), {"seconds": 40, "skip": 5, "rate": rate, "events": bool(shoot), "latency": False})()
    # (the server's drift of each client's player, net_compare's tolerance being far wider: none apart but where
    # the player touched something, a few ticks in a hundred; brought together to shoot each other, they touch,
    # and more part, as in a player's host's game of the same play: a mixed one at 120, net_compare --events with
    # a hit every 4 seconds, had 4% and 10% apart. The hits come every 8 seconds here, so that they touch less)
    allowed = 0.2 if shoot else 0.02
    # (each machine's log under its own name, which net_compare's figures and messages give)
    logs = {}
    for machine in [server] + clients:
        logs[machine.name] = machine.folder.parent / f"{machine.name}.log"
        shutil.copy(machine.output, logs[machine.name])
    problems = []
    for client in clients:
        result = net_compare.judge_run(logs[server.name], logs[client.name], args, tolerances, drift_judged=False)
        problems += [f"{client.name}: {problem}" for problem in result["problems"]]
        problems += [f"{client.name}: {divergence}" for divergence in result["divergences"]]
        print(f"{client.name}: {result.get('figures', {}).get('position_max')} world units apart at most, "
              f"{result.get('client_corrections')} corrections")
    divergences, mixed_problems, figures = net_compare.judge_mixed(
        logs[server.name], [logs[client.name] for client in clients], [False, opence_client], args, tolerances)
    problems += mixed_problems + [str(divergence) for divergence in divergences]
    assert not problems, "\n".join(problems) + f"\n{figures}"
    drift = {}
    for line in net_compare.parse_rates(logs[server.name])[args.skip:]:
        for player, (apart, compared) in line["drift"].items():
            drift.setdefault(player, [0, 0])
            drift[player][0] += apart
            drift[player][1] += compared
    assert len(drift) == 2 and all(compared >= 300 and apart / compared <= allowed
                                   for apart, compared in drift.values()), drift
    # (the server played the game's rate, each OpenH1 client too, an OpenCE one 30)
    assert set(_rates(server, net_compare)[-20:]) == {rate}, _rates(server, net_compare)
    assert set(_rates(clients[0], net_compare)[-20:]) == {rate}
    assert set(_rates(clients[1], net_compare)[-20:]) == {30 if opence_client else rate}
    # (the server took its OpenH1 clients' predictions as its ticks began, at 60 and 120, and its runs of them
    # drifted from the clients' as little)
    takes = re.findall(r"network test: takes none \d+ latest (\d+) late \d+ earlier (\d+) .*\| drift after latest "
                       r"(\d+)/(\d+) late \d+/\d+ earlier (\d+)/(\d+)", server.text())
    assert takes, "the server logged no takes"
    latest, earlier, latest_apart, latest_compared, earlier_apart, earlier_compared = map(int, takes[-1])
    assert latest + earlier > 0, takes[-1]
    apart, compared = latest_apart + earlier_apart, latest_compared + earlier_compared
    assert compared > 0 and apart / compared <= allowed, takes[-1]
    figures["drift"] = {player: f"{apart} of {compared}" for player, (apart, compared) in drift.items()}
    figures["takes"] = takes[-1]
    return figures


@pytest.mark.parametrize("rate", [60, 120])
def test_openh1_tick_game(machines, rate):
    """server.game_mode openh1_60 and openh1_120: a dedicated server hosts an OpenH1 game at that tick rate, which
    two OpenH1 clients join from its advertisement and play in step with it (net_compare's checks, the server's
    drift of each one's player); then, with nobody on it, server.game_mode legacy is taken in its lobby"""
    server, api, clients = _play_openh1_game(machines, rate, opence_client=False, shoot=0)
    figures = _judge_openh1_game(server, clients, rate, opence_client=False, shoot=0)
    print(figures)
    # a change of kind waits for the server to be empty, in its lobby
    server.send("sv_set server.game_mode legacy")
    _until(lambda: "server.game_mode = legacy" in server.text(), 15, "sv_set's answer", server)
    time.sleep(3)
    assert api.status()["game_mode"] == f"openh1_{rate}", "the kind of game changed with players on the server"
    for client in clients:
        client.stop()
    _until(lambda: (api.status() or {}).get("state") == "lobby" and api.status()["game_mode"] == "legacy", 90,
           "the lobby's Legacy games once empty", server)
    assert (api.status()["tick_rate"], api.status()["opence"]) == (30, True), api.status()
    assert "hosting: Legacy (server.game_mode)" in server.text()
    # a client then joins a Legacy game: at 30, its game record without the OpenH1 games' rate (the server's record
    # lasts from game to game), and none of OpenH1's messages heard
    net_compare = _net_compare()
    late = machines("charlie").start(
        HALO_NULL_RENDERER=1, HALO_NETWORK_TEST="join", HALO_NETWORK_TEST_NAME="Charlie", HALO_TEST_INPUT="bot:3",
        HALO_NET_ADDRESS=CLIENT_ADDRESSES[0], HALO_NET_BROADCAST=SERVER_ADDRESS)
    _until(lambda: len(net_compare.parse_rates(late.output)) >= 10, 150, "the client playing the Legacy game", late)
    heard = net_compare.parse_rates(late.output)[-1]
    assert (heard["rate"], heard["heard"], heard["record"]) == (30, 0, 0), heard
    assert api.status()["game_mode"] == "legacy" and api.status()["state"] == "playing", api.status()
    late.stop()
    server.send("quit")
    assert server.wait(15) == 0, server.text()[-3000:]
    assert not re.search(r"EXCEPTION|assert", server.debug()), "an exception or assert in the server's debug.txt"


def test_openh1_game_closed_to_opence(machines):
    """an OpenH1 game that OpenCE players do not join (network.allow_opence false) is advertised with the OpenH1
    protocol version and its top bit (0x8000), which OpenCE takes for a newer version and does not list, and the
    status says so; sv_set network.allow_opence true opens it to them at once, advertised as a mixed game"""
    _needs_game("bloodgulch")
    port = _free_port()
    server = machines("server")
    (server.folder / "bin" / "config.toml").write_text('[server]\ngame_mode = "openh1_60"\n\n'
                                                       '[network]\nallow_opence = false\n')
    # (and a player host's network.sim_rate, which the server leaves aside, saying so)
    server.start(["--server"], **_server_settings(port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_NET_SIM_RATE=30))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    status = api.status()
    assert (status["game_mode"], status["tick_rate"], status["opence"]) == ("openh1_60", 60, False), status
    assert status["game_mode_name"] == "OpenH1 60 Tick", status
    assert "hosting: OpenH1 60 Tick (server.game_mode)" in server.text()
    assert "network.sim_rate = 30 is a player's host's setting, which a dedicated server leaves aside: " \
           "server.game_mode (openh1_60) says what it hosts" in server.text(), server.text()[-2000:]
    prober = machines("prober")
    game = _probe(prober)
    assert game["network_version"] == 0x8000 | status["openh1_version"], game
    assert (game["tick_rate"], game["openh1_version"], game["dedicated"]) == (60, status["openh1_version"], True), game
    status, answer = api.call("POST", "/api/command", {"line": "sv_set network.allow_opence true"})
    assert status == 200 and answer["ok"], answer
    assert api.status()["opence"], api.status()
    game = _probe(prober)
    assert game["network_version"] == api.status()["network_version"] and game["tick_rate"] == 60, game
    server.send("quit")
    assert server.wait(15) == 0, server.text()[-3000:]


@pytest.mark.parametrize("shoot", [0, 8])
def test_openh1_game_with_an_opence_player(machines, shoot):
    """a mixed game on a dedicated server (server.game_mode openh1_120, network.allow_opence): an OpenH1 client at
    120 and one joining as OpenCE does at 30, each player stepped at their own rate, the OpenCE client hearing none
    of OpenH1's messages, and both in step with the server: without hits, each player's drift 2% at most; with a
    hit every 8 seconds, each one's hits taken"""
    server, api, clients = _play_openh1_game(machines, 120, opence_client=True, shoot=shoot)
    figures = _judge_openh1_game(server, clients, 120, opence_client=True, shoot=shoot)
    print(figures)
    # (the server steps the OpenCE client's player at 30: its machine's slot)
    assert figures.get("bravo", "").startswith("OpenCE: at [30]"), figures
    server.send("quit")
    assert server.wait(15) == 0, server.text()[-3000:]
    assert not re.search(r"EXCEPTION|assert", server.debug()), "an exception or assert in the server's debug.txt"


# ---------- VPNs and linked Discord accounts (docs/dedicated_server.md; docs/master_protocol.md, "The netlist",
# "Player keys, links and passes")

MASTER_SEED = bytes(range(32))
PUBLISHER_SEED = bytes(range(32, 64))


class FakeMaster:
    """a master server on loopback: it serves a signed netlist and the passes of the player keys linked to it, and
    takes a server's publishes (ed25519_ref signs: tools/ed25519_ref.py)"""

    def __init__(self, ranges, seed=MASTER_SEED):
        import http.server
        import sys
        import threading

        sys.path.insert(0, str(ROOT / "tools"))
        import ed25519_ref

        self.ed25519 = ed25519_ref
        self.seed = seed
        self.public_key = ed25519_ref.public_key(seed)
        self.key_id = hashlib.sha256(self.public_key).digest()[:8]
        self.links = {}
        self.asked = []
        self.netlist = netlist_body(ranges, serial=1)
        master = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *arguments):
                pass

            def answer(self, status, body, content_type="application/json", headers=()):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                for name, value in headers:
                    self.send_header(name, value)
                self.end_headers()
                self.wfile.write(body)

            def do_PUT(self):
                self.rfile.read(int(self.headers.get("Content-Length", 0)))
                self.answer(200, json.dumps({"ok": True, "listed": True, "interval": 30, "ttl": 90,
                                             "probe": "skipped", "reachable": True,
                                             "master_time": int(time.time())}).encode())

            do_DELETE = do_PUT

            def do_GET(self):
                from urllib.parse import parse_qs, urlparse

                url = urlparse(self.path)
                master.asked.append(url.path)
                if url.path == "/v1/netlist":
                    signature = master.ed25519.sign(master.seed, master.netlist)
                    self.answer(200, master.netlist, "application/octet-stream", [
                        ("Signature", "ed25519=" + base64.b64encode(signature).decode()),
                        ("Signature-Key", master.key_id.hex())])
                elif url.path == "/v1/pass":
                    query = parse_qs(url.query)
                    key = bytes.fromhex(query["key"][0])
                    signed = f"opence-pass-get-1\n{master.key_id.hex()}\n{key.hex()}\n{query['ts'][0]}".encode()
                    if not master.ed25519.verify(key, signed, bytes.fromhex(query["sig"][0])):
                        self.answer(401, b'{"ok":false,"error":{"code":"bad_signature"}}')
                    elif key not in master.links:
                        self.answer(404, b'{"ok":false,"error":{"code":"not_linked"}}')
                    else:
                        self.answer(200, master.make_pass(key, *master.links[key]), "application/octet-stream")
                else:
                    self.answer(404, b'{"ok":false,"error":{"code":"not_found"}}')

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def masters_line(self):
        return f"{self.url} ed25519:{self.public_key.hex()}\n"

    def make_pass(self, player_key, discord_id, name, issued=None, flags=1):
        issued = int(time.time()) if issued is None else issued
        body = (b"OPCEPASS" + bytes([1, flags]) + self.key_id + player_key + struct.pack(">Q", discord_id) +
                name.encode().ljust(32, b"\0") + struct.pack(">QQ", issued, issued + 72 * 3600))
        return body + self.ed25519.sign(self.seed, b"opence-pass-1" + body)

    def close(self):
        self.server.shutdown()


def netlist_body(ranges, serial):
    """a netlist (docs/master_protocol.md) of (first, last) dotted IPv4 ranges"""
    def number(text):
        return struct.unpack(">I", socket.inet_aton(text))[0]

    pairs = sorted((number(first), number(last)) for first, last in ranges)
    return (b"OPCENET1" + bytes([1, 0, 0, 0]) + struct.pack(">QQI", int(time.time()), serial, len(pairs)) +
            b"".join(struct.pack(">II", first, last) for first, last in pairs))


def snowflake(created):
    """a Discord id made at the Unix time"""
    return (created * 1000 - 1420070400000) << 22


def linked_player(machine, master, seed, discord_id, name, masters=None):
    """a player machine with a player key linked at the master, and its pass already held (its masters.txt the
    master's, or those given, in their order)"""
    public = master.ed25519.public_key(seed)
    (machine.folder / "bin" / "masters.txt").write_text("".join(each.masters_line() for each in masters or [master]))
    (machine.folder / "bin" / "player.key").write_text(f"# test\n{seed.hex()}\n")
    (machine.folder / "bin" / f"pass-{master.key_id.hex()}.bin").write_bytes(
        master.make_pass(public, discord_id, name))
    master.links[public] = (discord_id, name)
    return machine


def test_vpn_policy_and_linked_discord_accounts(machines):
    """a server whose masters' netlist lists its clients' addresses (server.vpn_check_lan, as on the LAN): a client
    with no Discord account is refused (vpn_link_required, a join_refused event); one with a pass of an account a
    year old, linked at the master the server publishes to, is refused, joins again at once with its pass and plays
    as that verified account; a young account is refused (server.link_min_age_days), and so is the account's second
    machine (server.link_one_seat); with server.vpn_policy block a pass does not help; and a ban of the account drops
    it, its line in bans.txt a verified account's"""
    _needs_game("bloodgulch")
    clients = ("127.0.0.202", "127.0.0.203", "127.0.0.204", "127.0.0.205", "127.0.0.206")
    master = FakeMaster([("127.0.0.202", "127.0.0.206")])
    try:
        port = _free_port()
        server_machine = machines("server")
        (server_machine.folder / "bin" / "masters.txt").write_text(master.masters_line())
        server = server_machine.start(["--server"], **_server_settings(
            port, HALO_NET_BROADCAST=",".join(clients), HALO_SERVER_ROTATION="bloodgulch:slayer",
            HALO_SERVER_PUBLISHER_KEY=PUBLISHER_SEED.hex(), HALO_SERVER_VPN_CHECK_LAN="true",
            HALO_SERVER_LOG_ADDRESSES="true", HALO_SERVER_LOBBY_SECONDS=300))
        api = Api(port)
        _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
        _until(lambda: "netlist taken" in server.text(), 30, "the master's netlist", server)
        api.login()
        status, vpn = api.call("GET", "/api/vpn")
        assert status == 200 and vpn["policy"] == "link" and vpn["held"], vpn
        assert vpn["lists"][0]["ranges"] == 1 and vpn["lists"][0]["key_id"] == master.key_id.hex(), vpn
        assert api.call("POST", "/api/vpn/check", {"ip": "127.0.0.203"})[1]["by"] == "netlist"
        assert api.call("POST", "/api/vpn/check", {"ip": "127.0.0.9"})[1]["listed"] is False

        # (no Discord account: refused)
        _client(machines("unlinked"), clients[0], "Nobody")
        _until(lambda: "refused at the door (vpn_link_required)" in server.text(), 90, "the unlinked refused", server)
        refused = [event for event in api.events() if event.get("event") == "join_refused"]
        assert refused and refused[0]["reason"] == "vpn_link_required" and refused[0]["name"], refused
        assert not api.call("GET", "/api/players")[1]["players"], "a machine with no account joined"

        # (an account a year and more old: in, verified)
        old_id = snowflake(int(time.time()) - 400 * 86400)
        linked = linked_player(machines("linked"), master, bytes([7] * 32), old_id, "nelly")
        _client(linked, clients[1], "Linked")
        _until(lambda: [player for player in api.call("GET", "/api/players")[1]["players"]
                        if player["name"] == "Linked"], 90, "the linked player in", server)
        player = [p for p in api.call("GET", "/api/players")[1]["players"] if p["name"] == "Linked"][0]
        assert player["discord_verified"] is True and player["discord_name"] == "nelly", player
        assert player["discord_id"] == str(old_id), player
        assert "shows the Discord account nelly" in server.text()
        joined = [event for event in api.events() if event.get("event") == "player_joined" and event["name"] == "Linked"]
        assert joined and joined[0]["discord_verified"] is True, joined

        # (a young account: refused)
        young = linked_player(machines("young"), master, bytes([9] * 32), snowflake(int(time.time()) - 10 * 86400),
                              "fresh")
        _client(young, clients[2], "Young")
        _until(lambda: "refused at the door (vpn_link_too_new)" in server.text(), 90, "the young account refused",
               server)

        # (the linked account on a second machine while it plays: refused, server.link_one_seat)
        twin = linked_player(machines("twin"), master, bytes([7] * 32), old_id, "nelly")
        _client(twin, clients[4], "Twin")
        _until(lambda: "refused at the door (vpn_link_in_use)" in server.text(), 90, "the second seat refused", server)

        # (server.vpn_policy block: a pass does not help)
        status, answer = api.call("PUT", "/api/settings", {"server.vpn_policy": "block"})
        assert answer["applied"] == ["server.vpn_policy"], answer
        blocked = linked_player(machines("blocked"), master, bytes([11] * 32),
                                snowflake(int(time.time()) - 800 * 86400), "elder")
        _client(blocked, clients[3], "Blocked")
        _until(lambda: "refused at the door (vpn_blocked)" in server.text(), 90, "the VPN blocked", server)
        assert [p["name"] for p in api.call("GET", "/api/players")[1]["players"]] == ["Linked"]

        # (the account banned: dropped, and its line in bans.txt a verified one; last, since every machine of
        # this test has the same hardware id, which the ban keeps out too)
        status, answer = api.call("POST", "/api/bans", {"discord_id": str(old_id), "reason": "test"})
        assert status == 200 and answer["ok"], answer
        _until(lambda: not [p for p in api.call("GET", "/api/players")[1]["players"] if p["name"] == "Linked"], 30,
               "the banned account dropped", server)
        bans = api.call("GET", "/api/bans")[1]["bans"]
        assert bans[-1]["discord_id"] == str(old_id) and bans[-1]["discord_verified"] is True, bans
        assert f"discord_id={old_id}\t" in (server_machine.folder / "data" / "bans.txt").read_text()
        counts = api.call("GET", "/api/vpn")[1]["refused"]
        assert counts["vpn_link_required"] >= 1 and counts["vpn_link_too_new"] >= 1 and counts["vpn_blocked"] >= 1 and \
            counts["vpn_link_in_use"] >= 1, counts
        server.send("quit")
        assert server.wait(15) == 0, server.text()[-3000:]
    finally:
        master.close()


def test_vpn_policy_off_and_no_list(machines):
    """server.vpn_fail closed refuses machines from the internet's (here the LAN's, server.vpn_check_lan) while no
    netlist is held; vpn_allow.txt lets one in; server.vpn_policy off lets everyone in"""
    _needs_game("bloodgulch")
    port = _free_port()
    server_machine = machines("server")
    (server_machine.folder / "data" / "vpn_allow.txt").write_text("# the second client\n127.0.0.203\n")
    server = server_machine.start(["--server"], **_server_settings(
        port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_SERVER_VPN_CHECK_LAN="true",
        HALO_SERVER_VPN_FAIL="closed", HALO_SERVER_MASTER_PUBLISH="false", HALO_SERVER_LOBBY_SECONDS=300))
    api = Api(port)
    _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
    api.login()
    assert api.call("GET", "/api/vpn")[1]["held"] is False
    first = _client(machines("first"), CLIENT_ADDRESSES[0], "First")
    _until(lambda: "refused at the door (vpn_blocked)" in server.text(), 90, "the first refused", server)
    _client(machines("second"), CLIENT_ADDRESSES[1], "Second")
    _until(lambda: [p for p in api.call("GET", "/api/players")[1]["players"] if p["name"] == "Second"], 90,
           "the allowed machine in", server)
    status, answer = api.call("PUT", "/api/settings", {"server.vpn_policy": "off"})
    assert answer["applied"] == ["server.vpn_policy"], answer
    # (a refused test client gives up: another, of the same address, joins)
    first.stop()
    _client(machines("again"), CLIENT_ADDRESSES[0], "Again")
    _until(lambda: [p for p in api.call("GET", "/api/players")[1]["players"] if p["name"] == "Again"], 120,
           "a machine of that address in once the policy is off", server)
    server.send("quit")
    assert server.wait(15) == 0, server.text()[-3000:]


def test_vpn_link_with_one_of_a_servers_masters(machines):
    """a server that publishes to two masters takes a pass of either; a player linked with the second alone (its
    masters.txt naming both, the first before) joins with that one's pass: the first's, which it has none of, is
    not asked for"""
    _needs_game("bloodgulch")
    first, second = FakeMaster([("127.0.0.202", "127.0.0.203")]), \
        FakeMaster([("127.0.0.202", "127.0.0.203")], seed=bytes(range(64, 96)))
    try:
        port = _free_port()
        server_machine = machines("server")
        (server_machine.folder / "bin" / "masters.txt").write_text(first.masters_line() + second.masters_line())
        server = server_machine.start(["--server"], **_server_settings(
            port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_SERVER_PUBLISHER_KEY=PUBLISHER_SEED.hex(),
            HALO_SERVER_VPN_CHECK_LAN="true", HALO_SERVER_LOG_ADDRESSES="true", HALO_SERVER_LOBBY_SECONDS=300))
        api = Api(port)
        _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
        _until(lambda: server.text().count("netlist taken") == 2, 30, "both masters' netlists", server)
        api.login()
        assert len(api.call("GET", "/api/vpn")[1]["lists"]) == 2
        linked = linked_player(machines("linked"), second, bytes([5] * 32), snowflake(int(time.time()) - 900 * 86400),
                               "second_only", masters=[first, second])
        _client(linked, CLIENT_ADDRESSES[0], "Linked")
        _until(lambda: [p for p in api.call("GET", "/api/players")[1]["players"] if p["name"] == "Linked"], 90,
               "the player linked with the second master in", server)
        player = [p for p in api.call("GET", "/api/players")[1]["players"] if p["name"] == "Linked"][0]
        assert player["discord_verified"] and player["discord_name"] == "second_only", player
        assert "joining again with " + second.url + "'s pass" in linked.text(), linked.text()[-2000:]
        server.send("quit")
        assert server.wait(15) == 0, server.text()[-3000:]
    finally:
        first.close()
        second.close()



# ---------- updates (server.auto_update: port/linux/src/updater.c's updater_server_*)

def _network_versions():
    import sys

    sys.path.insert(0, str(ROOT / "tools"))
    import ci_build

    versions = ci_build.network_versions()
    return versions["HALO_PORT_NETWORK_VERSION"], versions["HALO_PORT_STANDARD_NETWORK_VERSION"]


class FakeReleases:
    """GitHub's releases on loopback (debug.update_url): the latest release's answer, its notes giving its network
    versions as the release job writes them (tools/ci_build.py notes), its zip, whose first downloads may be refused,
    its SHA256SUMS, wrong for the zip's first wrong_sums downloads, and their signature, SHA256SUMS.sig, by
    UPDATE_SEED, the key the server is given (debug.update_key: ed25519_ref signs, tools/ed25519_ref.py), unless
    signed is False"""

    def __init__(self, zip_bytes):
        import http.server
        import sys
        import threading

        sys.path.insert(0, str(ROOT / "tools"))
        import ed25519_ref

        self.build = 0
        self.signed = True
        self.network_version, self.standard_version = _network_versions()
        self.zip_bytes = zip_bytes
        self.refusals = 0
        self.wrong_sums = 0
        self.downloads = []
        releases = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *arguments):
                pass

            def answer(self, status, body, content_type="application/json"):
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                if self.path == "/repos/OpenH1/OpenH1/releases/latest":
                    if not releases.build:
                        self.answer(404, b'{"message": "Not Found"}')
                        return
                    # (a subject that names a network version, which the notes' own line comes after)
                    notes = (f"Network version: 1 is not this\n\nCommit: x\nBuild: y\n"
                             f"Network version: {releases.network_version}\n"
                             f"Standard network version: {releases.standard_version}\n\nThe release builds...")
                    self.answer(200, json.dumps({"tag_name": f"build-{releases.build}", "body": notes}).encode())
                    return
                prefix = f"/OpenH1/OpenH1/releases/download/build-{releases.build}/"
                if releases.build and self.path == prefix + "SHA256SUMS":
                    self.answer(200, releases.sums().encode(), "text/plain")
                    return
                if releases.build and releases.signed and self.path == prefix + "SHA256SUMS.sig":
                    signature = ed25519_ref.sign(UPDATE_SEED, releases.sums().encode()).hex()
                    self.answer(200, (signature + "\n").encode(), "text/plain")
                    return
                if releases.build and self.path.startswith(prefix) and self.path.endswith(".zip"):
                    releases.downloads.append(self.path[len(prefix):])
                    if releases.refusals > 0:
                        releases.refusals -= 1
                        self.answer(503, b"busy", "text/plain")
                    else:
                        self.answer(200, releases.zip_bytes, "application/zip")
                    return
                self.answer(404, b"", "text/plain")

        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def sums(self):
        """the SHA256SUMS of the zip's latest download: wrong for its first wrong_sums"""
        zips = len([name for name in self.downloads if name.endswith(".zip")])
        digest = hashlib.sha256(self.zip_bytes if zips > self.wrong_sums else b"another").hexdigest()
        return "".join(f"{digest}  {name}\n" for name in ("OpenH1-Linux.zip", "OpenH1-Linux-debug.zip"))

    def close(self):
        self.server.shutdown()
        self.server.server_close()


# the test's release key, which signs FakeReleases' SHA256SUMS: the server trusts it beside OpenH1's
# (debug.update_key) while its debug.update_url is set
UPDATE_SEED = bytes(range(96, 128))
NEW_GAME = b"#!/bin/sh\necho the new build\n"


def _release_zip():
    """the new build's tree: its game (a stand-in) and a file the old one has not"""
    import io
    import zipfile

    data = io.BytesIO()
    with zipfile.ZipFile(data, "w", zipfile.ZIP_DEFLATED) as archive:
        game = zipfile.ZipInfo("bin/halo")
        game.external_attr = 0o100755 << 16
        game.compress_type = zipfile.ZIP_DEFLATED
        archive.writestr(game, NEW_GAME)
        archive.writestr("share/openh1/new-build.txt", "the new build\n")
    return data.getvalue()


def _tree(machine):
    """the machine's folder as the Linux release's tree (bin/ beside share/openh1: install_layout.c), which a
    server updates"""
    (machine.folder / "share" / "openh1").mkdir(parents=True, exist_ok=True)
    return machine


def _update_settings(releases, **settings):
    import sys

    sys.path.insert(0, str(ROOT / "tools"))
    import ed25519_ref

    values = {"HALO_UPDATE_URL": releases.url, "HALO_UPDATE_KEY": ed25519_ref.public_key(UPDATE_SEED).hex(),
              "HALO_UPDATE_TIME_SCALE": 100, "HALO_SERVER_AUTO_UPDATE": "install",
              "HALO_SERVER_UPDATE_CHECK_MINUTES": 5}
    values.update(settings)
    return values


def _installed(machine):
    folder = machine.folder
    return ((folder / "bin" / "halo").read_bytes() == NEW_GAME and (folder / "bin" / "halo.old").is_file() and
            (folder / "share" / "openh1" / "new-build.txt").is_file())


def test_update_of_the_same_network_version_once_empty(machines):
    """a newer release of the same network versions is found, its download refused once (the install tried again
    at the next check), installed while the server runs, and the server, empty, stops with exit code 5 (after 10
    minutes, on debug.update_time_scale's clock)"""
    _needs_game("bloodgulch")
    releases = FakeReleases(_release_zip())
    releases.build = 7
    releases.refusals = 1
    try:
        port = _free_port()
        server = _tree(machines("server")).start(["--server"], **_server_settings(
            port, HALO_SERVER_ROTATION="bloodgulch:slayer", **_update_settings(releases)))
        assert server.wait(150) == 5, server.text()[-3000:]
        text = server.text()
        assert "build 7 is out" in text and "a new network version" not in text, text[-3000:]
        assert text.count("installing build 7") == 2, text[-3000:]
        assert "build 7 could not be installed" in text and "server answered 503" in text, text[-3000:]
        assert text.index("could not be installed") < text.index("build 7 installed: started once the server")
        assert "stopping to be started again (empty for 10 minutes)" in text, text[-3000:]
        assert len(releases.downloads) == 2 and all(name.startswith("OpenH1-Linux") and name.endswith(".zip")
                                                    for name in releases.downloads), releases.downloads
        assert _installed(server), sorted(str(path) for path in server.folder.rglob("*"))
        assert "bin/halo.old" in (server.folder / "update-old.txt").read_text()
        assert not list(server.folder.glob("update.partial*")), "the update's folder was left"
    finally:
        releases.close()


def test_update_of_a_new_network_version_after_the_game(machines):
    """a release of a new network version (the Standard games', the notes' second line) found during a game: the
    players are told the server updates after this game, the game is ended after server.update_game_minutes, and
    the server stops with exit code 5 after its postgame"""
    _needs_game("bloodgulch")
    releases = FakeReleases(_release_zip())
    releases.standard_version += 1
    try:
        port = _free_port()
        server = _tree(machines("server")).start(["--server"], **_server_settings(
            port, HALO_SERVER_ROTATION="bloodgulch:slayer", HALO_SERVER_UPDATE_GAME_MINUTES=1,
            **_update_settings(releases)))
        api = Api(port)
        _until(lambda: "server: ready" in server.text(), 90, "server: ready", server)
        api.login()
        _client(machines("client"), CLIENT_ADDRESSES[0], "Player")
        _until(lambda: (api.status() or {}).get("state") == "playing", 120, "the game", server)
        # (released once the game is under way: the next check finds it)
        releases.build = 8
        assert server.wait(90) == 5, server.text()[-3000:]
        text = server.text()
        assert "build 8 is out" in text and "a new network version" in text, text[-3000:]
        assert "build 8 installed: a new network version, started once this game is over" in text, text[-3000:]
        assert "since the update was installed" in text, text[-3000:]
        assert "stopping to be started again (a new network version, after the game)" in text, text[-3000:]
        assert text.index("game: ended") < text.index("stopping to be started again"), text[-3000:]
        assert _installed(server)
    finally:
        releases.close()


def test_a_build_outside_a_tree_does_not_update(machines):
    """a server that is not in a release's tree says once that it does not update itself, and plays on"""
    _needs_game("bloodgulch")
    releases = FakeReleases(_release_zip())
    releases.build = 9
    try:
        port = _free_port()
        server = machines("server").start(["--server"], **_server_settings(
            port, HALO_SERVER_ROTATION="bloodgulch:slayer", **_update_settings(releases)))
        _until(lambda: "does not update itself" in server.text(), 90, "the refusal", server)
        time.sleep(5)
        text = server.text()
        assert text.count("does not update itself") == 1 and "not in a release's tree" in text, text[-3000:]
        assert server.process.poll() is None and not releases.downloads
        server.send("quit")
        assert server.wait(15) == 0, server.text()[-3000:]
    finally:
        releases.close()


def test_update_whose_checksum_does_not_match_is_refused(machines):
    """a release whose SHA256SUMS does not match its zip is refused before anything is unpacked: the server plays on,
    and the next check downloads it again, whose sums match, installs it, and stops with exit code 5 once empty"""
    _needs_game("bloodgulch")
    releases = FakeReleases(_release_zip())
    releases.build = 7
    releases.wrong_sums = 1
    try:
        port = _free_port()
        server = _tree(machines("server")).start(["--server"], **_server_settings(
            port, HALO_SERVER_ROTATION="bloodgulch:slayer", **_update_settings(releases)))
        _until(lambda: "build 7 could not be installed" in server.text(), 90, "the refusal", server)
        text = server.text()
        assert "build 7 was refused" in text and "its SHA256SUMS lists" in text, text[-3000:]
        # (nothing of the refused download put in place, and the server plays on)
        assert server.process.poll() is None
        assert not (server.folder / "bin" / "halo.old").exists() and \
            not (server.folder / "share" / "openh1" / "new-build.txt").exists()
        assert server.wait(150) == 5, server.text()[-3000:]
        text = server.text()
        assert text.count("installing build 7") == 2, text[-3000:]
        assert text.index("was refused") < text.index("build 7 installed: started once the server"), text[-3000:]
        assert len(releases.downloads) == 2, releases.downloads
        assert _installed(server)
    finally:
        releases.close()


def test_unsigned_update_is_refused(machines):
    """a release with no SHA256SUMS.sig is refused before anything is unpacked (release_signature.h:
    RELEASE_SIGNATURE_REQUIRED), and the server plays on"""
    _needs_game("bloodgulch")
    releases = FakeReleases(_release_zip())
    releases.build = 7
    releases.signed = False
    try:
        port = _free_port()
        server = _tree(machines("server")).start(["--server"], **_server_settings(
            port, HALO_SERVER_ROTATION="bloodgulch:slayer", **_update_settings(releases)))
        _until(lambda: "build 7 could not be installed" in server.text(), 90, "the refusal", server)
        text = server.text()
        assert "build 7 was refused" in text and "not signed" in text, text[-3000:]
        assert server.process.poll() is None
        assert not (server.folder / "bin" / "halo.old").exists() and \
            not (server.folder / "share" / "openh1" / "new-build.txt").exists()
        server.send("quit")
        assert server.wait(15) == 0, server.text()[-3000:]
    finally:
        releases.close()
