"""The bots (port/linux/game/bots.c, bots.count) in the game: a local Slayer game on a stock map, played without a
person (debug.network_test "local:", the scripted controller 1 of debug.test_input) for a minute and more, in which
every bot walks the map, the bots kill and die, and the game ends cleanly; and a system link game, whose host plays
them as a local game's; the bots in the vehicles; and the objective games (bots_objectives.c): they capture flags, hold
the ball and stay on the hill. Needs the retail maps in assets/maps (and, for the Banshees,
bloodgulch_all_vehicles.map with the Custom Edition's shared maps in assets/custom_maps) and a Linux build (ninja
linux_x86_64 or linux_arm64); the game draws offscreen (SDL's offscreen video driver), with no audio, internet play
or update check."""

import math
import os
import re
import shutil
import socket
import subprocess
import threading
import time
from pathlib import Path

import pytest

RETAIL_MAPS = Path("assets/maps")
# a Custom Edition map with every vehicle (its Banshees), and the Custom Edition's shared maps beside it
CUSTOM_MAPS = Path("assets/custom_maps")
ALL_VEHICLES = "bloodgulch_all_vehicles"
# (the Linux build of this computer's processor)
GAME = next((path for path in (Path("build/linux_x86_64/halo"), Path("build/linux_arm64/halo")) if path.is_file()),
            Path("build/linux_x86_64/halo"))
# a player's line of the network test's each second (network_test.c): alive or dead, its kills, deaths, team and
# machine (a bot's is bots.h's BOTS_FIRST_MACHINE, 64, and up)
PLAYER = re.compile(r"player (\d+): (?:\((-?[\d.]+) (-?[\d.]+) -?[\d.]+\)|dead).*? s-?\d+ k(-?\d+) d(-?\d+) f-?\d+ "
                    r"t-?\d+ m(\d+)")
BOTS_FIRST_MACHINE = 64


def _game(map_name):
    if not GAME.is_file() or not (RETAIL_MAPS / f"{map_name}.map").is_file():
        pytest.skip(f"needs {map_name}.map in assets/maps and a Linux build ({GAME})")


def _worker():
    """pytest-xdist's worker's number (0 without workers)"""
    worker = os.environ.get("PYTEST_XDIST_WORKER", "")
    return int(worker[2:]) if worker.startswith("gw") and worker[2:].isdigit() else 0


def _address():
    """the loopback address of this game: pytest-xdist's workers each their own, so that their games run at once"""
    return f"127.0.0.{1 + _worker()}"


def _console(port, commands):
    """each (seconds, command) sent to the game's script console (debug.telnet_console) that many seconds in"""
    start = time.monotonic()
    for seconds, command in commands:
        time.sleep(max(0.0, start + seconds - time.monotonic()))
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=5) as console:
                console.sendall(f"{command}\r\n".encode())
                time.sleep(0.5)
        except OSError:
            pass


def _play(tmp_path, test, seconds, console=(), **settings):
    """the game played for that many seconds, the script console sent console's (seconds, command) as it plays:
    (exit status, its output, debug.txt)"""
    (tmp_path / "data").mkdir()
    (tmp_path / "save").mkdir()
    (tmp_path / "data" / "maps").symlink_to(RETAIL_MAPS.resolve())
    if CUSTOM_MAPS.is_dir():
        (tmp_path / "data" / "custom_maps").symlink_to(CUSTOM_MAPS.resolve())
    game = tmp_path / "halo"
    shutil.copy(GAME, game)
    env = {**os.environ, "SDL_VIDEODRIVER": "offscreen", "SDL_AUDIODRIVER": "dummy",
           "HALO_DATA_ROOT": str(tmp_path / "data"), "HALO_SAVE_ROOT": str(tmp_path / "save"),
           "HALO_HIDDEN_WINDOW": "1", "HALO_FULLSCREEN": "0", "HALO_NO_VSYNC": "1", "HALO_NO_AUDIO": "1",
           "HALO_UPDATE_AUTO": "false", "HALO_NET_ONLINE": "false", "HALO_NET_ADDRESS": _address(),
           "HALO_NET_BROADCAST": "127.255.255.255", "HALO_NETWORK_TEST": test, "HALO_NETWORK_TEST_START": "5",
           "HALO_NETWORK_TEST_SCORE": "500", "HALO_TEST_INPUT": "bot:1", "HALO_EXIT_AFTER": str(seconds + 20)}
    env.update({name: str(value) for name, value in settings.items()})
    sender = None
    if console:
        port = 2400 + _worker()
        env.update(HALO_TELNET_CONSOLE="1", HALO_TELNET_CONSOLE_PORT=str(port))
        sender = threading.Thread(target=_console, args=(port, console), daemon=True)
        sender.start()
    result = subprocess.run([str(game)], cwd=tmp_path, env=env, capture_output=True, text=True,
                            errors="replace", timeout=seconds + 120)
    if sender:
        sender.join(timeout=1)
    debug = tmp_path / "data" / "debug.txt"
    return result.returncode, result.stdout + result.stderr, debug.read_text(errors="replace") if debug.is_file() else ""


def _players(output):
    """each player's walk (world units), kills, deaths and machine at the end"""
    players, last = {}, {}
    for line in output.splitlines():
        if "network test: tick" not in line:
            continue
        for match in PLAYER.finditer(line):
            index = int(match.group(1))
            player = players.setdefault(index, {"walked": 0.0})
            player.update(kills=int(match.group(4)), deaths=int(match.group(5)), machine=int(match.group(6)))
            if match.group(2) is None:
                last.pop(index, None)
                continue
            point = (float(match.group(2)), float(match.group(3)))
            # (a step of more than 10 units in a second is a respawn or a teleporter)
            if index in last and math.dist(last[index], point) < 10.0:
                player["walked"] += math.dist(last[index], point)
            last[index] = point
    return players


@pytest.mark.parametrize("map_name,variant,count,skill", [
    ("bloodgulch", "slayer", 6, "normal"),
    ("chillout", "team_slayer", 5, "heroic"),
])
def test_bots_play_a_local_slayer_game(tmp_path, map_name, variant, count, skill):
    _game(map_name)
    status, output, debug = _play(tmp_path, f"local:{map_name}:{variant}", 75, HALO_BOTS=count, HALO_BOT_SKILL=skill)
    assert status == 0, output[-2000:]
    assert not re.search(r"assert|exception", output + debug, re.I), "an assert or exception in the game's log"
    bots = {index: player for index, player in _players(output).items() if player["machine"] >= BOTS_FIRST_MACHINE}
    assert len(bots) == count, f"{len(bots)} bots in the game, not {count}"
    for index, bot in bots.items():
        assert bot["walked"] >= 30.0, f"bot {index} walked {bot['walked']:.0f} units"
    kills = sum(bot["kills"] for bot in bots.values())
    deaths = sum(bot["deaths"] for bot in bots.values())
    assert kills >= count // 2 and deaths > 0, f"the bots killed {kills} and died {deaths} times"
    assert "bots: tick" in output, "no bots line a minute into the game"


# the bots' minute line of the vehicles (bots.c's bots_report)
VEHICLES = re.compile(r"bots: tick \d+, vehicles: driven (\d+) \(Warthog (\d+), Ghost (\d+), Scorpion (\d+), "
                      r"Banshee (\d+)\), gunned (\d+), ridden (\d+), flipped (\d+); seconds driving (\d+), "
                      r"riding (\d+); kills (\d+), deaths (\d+);.*; flying (\d+) seconds, deaths (\d+) "
                      r"\(their own (\d+)\); seconds getting unstuck (\d+)")
VEHICLE_FIELDS = ("driven", "warthog", "ghost", "scorpion", "banshee", "gunned", "ridden", "flipped", "driving",
                  "riding", "kills", "deaths", "flying", "flying_deaths", "crashes", "unstuck")


def _vehicles(output):
    """the last minute line's vehicles: seats taken, seconds in them, kills and deaths there"""
    lines = VEHICLES.findall(output)
    assert lines, "no vehicles line a minute into the game"
    return dict(zip(VEHICLE_FIELDS, map(int, lines[-1])))


@pytest.mark.parametrize("variant,kind", [
    ("team_slayer", "warthog"),
    ("team_slayer+team_vehicles=pc", "ghost"),
])
def test_bots_use_the_vehicles(tmp_path, variant, kind):
    """bots.vehicles on Blood Gulch, two minutes of Team Slayer: the bots drive the Warthogs (and the Ghosts and the
    Scorpions, with the PC's vehicles), ride a teammate's Warthog, and kill from them"""
    _game("bloodgulch")
    status, output, debug = _play(tmp_path, f"local:bloodgulch:{variant}", 120, HALO_BOTS=8)
    assert status == 0, output[-2000:]
    assert not re.search(r"assert|exception", output + debug, re.I), "an assert or exception in the game's log"
    vehicles = _vehicles(output)
    assert vehicles[kind] >= 1, f"no bot drove a {kind}: {vehicles}"
    assert vehicles["driving"] >= 60, f"the bots drove {vehicles['driving']} seconds: {vehicles}"
    # (backing out of and turning away from what they drove into: a small part of the driving)
    assert vehicles["unstuck"] <= vehicles["driving"] * 0.15, f"the drivers were stuck a lot: {vehicles}"
    if kind == "warthog":
        assert vehicles["gunned"] + vehicles["ridden"] >= 1, f"no bot rode a teammate's Warthog: {vehicles}"
    else:
        # (the one Scorpion is taken, or not, by chance in two minutes: the PC's vehicles driven more than once)
        assert vehicles["scorpion"] >= 1 or vehicles["ghost"] >= 2, f"no bot drove a Scorpion, nor Ghosts: {vehicles}"
    assert vehicles["kills"] >= 1, f"no kill from a vehicle: {vehicles}"


def test_bots_fly_the_banshee(tmp_path):
    """bots.vehicles on a Custom Edition Blood Gulch with every vehicle, each of them made by the script console
    (cheat_all_vehicles) by the first player four times early on (once the game has started), three minutes of Team
    Slayer: the bots fly the Banshees, and no Banshee's pilot flies into the map"""
    _game("bloodgulch")
    if not (CUSTOM_MAPS / f"{ALL_VEHICLES}.map").is_file():
        pytest.skip(f"needs {ALL_VEHICLES}.map (and the Custom Edition's shared maps) in {CUSTOM_MAPS}")
    status, output, debug = _play(tmp_path, f"local:custom_maps\\{ALL_VEHICLES}:team_slayer", 180,
                                  console=[(seconds, "cheat_all_vehicles") for seconds in (25, 35, 45, 55)],
                                  HALO_BOTS=10)
    assert status == 0, output[-2000:]
    assert not re.search(r"assert|exception", output + debug, re.I), "an assert or exception in the game's log"
    vehicles = _vehicles(output)
    assert vehicles["banshee"] >= 1 and vehicles["flying"] >= 20, f"the bots hardly flew a Banshee: {vehicles}"
    assert vehicles["crashes"] == 0, f"a Banshee's pilot flew into the map: {vehicles}"


# the bots' minute line's part of their game's objective (bots_objectives.c's bots_objectives_report)
OBJECTIVES = re.compile(r"bots: tick \d+, \d+ bots .*?, (?:flags: grabs (\d+), captures (\d+), returns (\d+)|"
                        r"ball: seconds held (\d+), carrier kills (\d+)|hill: seconds (\d+) \((\d+) hills\))")
OBJECTIVE_FIELDS = ("grabs", "captures", "returns", "ball", "carrier_kills", "hill", "hills")


def _objectives(output):
    """the last minute line's objective: the bots' flag grabs, captures and returns, seconds with a ball, on a hill"""
    lines = OBJECTIVES.findall(output)
    assert lines, "no objective line a minute into the game"
    return {field: int(value) for field, value in zip(OBJECTIVE_FIELDS, lines[-1]) if value}


def _objective_game(tmp_path, test, seconds, count):
    _game(test.split(":")[1])
    status, output, debug = _play(tmp_path, test, seconds, HALO_BOTS=count)
    assert status == 0, output[-2000:]
    assert not re.search(r"assert|exception", output + debug, re.I), "an assert or exception in the game's log"
    bots = {index: player for index, player in _players(output).items() if player["machine"] >= BOTS_FIRST_MACHINE}
    assert len(bots) == count, f"{len(bots)} bots in the game, not {count}"
    return _objectives(output)


def test_bots_capture_the_flag(tmp_path):
    """Capture the Flag on Chill Out: the bots take the enemy's flag, run it home and score, and return their own"""
    flags = _objective_game(tmp_path, "local:chillout:ctf", 120, 6)
    assert flags["grabs"] >= 2, f"the bots hardly took a flag: {flags}"
    assert flags["captures"] >= 1, f"no bot captured a flag: {flags}"
    assert flags["returns"] >= 1, f"no bot returned its flag: {flags}"


def test_bots_take_the_flag_on_blood_gulch(tmp_path):
    """Capture the Flag on Blood Gulch, three minutes with the vehicles: the bots cross the map and take a flag"""
    flags = _objective_game(tmp_path, "local:bloodgulch:ctf", 180, 8)
    assert flags["grabs"] >= 1, f"no bot took a flag: {flags}"


@pytest.mark.parametrize("variant", ["oddball", "team_oddball"])
def test_bots_play_oddball(tmp_path, variant):
    """Oddball: the bots take the ball and hold it, and hunt its holder"""
    ball = _objective_game(tmp_path, f"local:chillout:{variant}", 90, 6)
    # (of the first minute's; most games 30 to 45, the holder hunted by every other bot)
    assert ball["ball"] >= 15, f"the bots held the ball {ball['ball']} seconds: {ball}"


@pytest.mark.parametrize("variant", ["king", "team_king"])
def test_bots_play_king_of_the_hill(tmp_path, variant):
    """King of the Hill: the bots get to the hill and stay in it"""
    hill = _objective_game(tmp_path, f"local:chillout:{variant}", 90, 6)
    assert hill["hills"] >= 1, f"no hill: {hill}"
    assert hill["hill"] >= 30, f"the bots were on the hill {hill['hill']} seconds: {hill}"


def test_bots_in_a_system_link_game(tmp_path):
    """a system link game's host plays its bots as a local game's does: each a player of a machine of its own"""
    _game("bloodgulch")
    status, output, _ = _play(tmp_path, "host:bloodgulch:slayer", 75, HALO_BOTS=6)
    assert status == 0, output[-2000:]
    players = _players(output)
    assert players, "the system link game did not start"
    bots = {index: player for index, player in players.items() if player["machine"] >= BOTS_FIRST_MACHINE}
    assert len(bots) == 6, f"{len(bots)} bots in the system link game, not 6"
    assert sum(bot["walked"] for bot in bots.values()) >= 6 * 30.0, "the bots stood still"
    assert "bots: tick" in output, "no bots line a minute into the game"


def test_many_bots_in_a_system_link_game(tmp_path):
    """up to bots.h's 64 bots in one game (ADD 48 in a game of 64), each a machine of its own from
    BOTS_FIRST_MACHINE up, playing a minute"""
    _game("bloodgulch")
    status, output, debug = _play(tmp_path, "host:bloodgulch:slayer", 75, HALO_BOTS=48, HALO_BOTS_MAKE_ROOM="false",
                                  HALO_NETWORK_TEST_MOST_PLAYERS=64)
    assert status == 0, output[-2000:]
    assert not re.search(r"assert|exception", output + debug, re.I), "an assert or exception in the game's log"
    lines = re.findall(r"bots: tick \d+, (\d+) bots", output)
    assert lines and int(lines[-1]) == 48, f"bots playing a minute in: {lines}"
