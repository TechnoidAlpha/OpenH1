"""A map that cannot be loaded, named at the script console (map_name) or in init.txt, is refused once: the game logs
why, stays where it was (the main menu here) and still loads the next map it is given. The Xbox's precache took a
missing map for a damaged disc and stopped the game for good (display_error_damaged_media, main_loop_of_death); a
map file that did not copy, or whose tags did not load, stopped it too (main_new_map's _error_immediate).

The names: a stock map's typo, no name, a Custom Edition map that is not there, a file that is no map, a map file cut
short (its copy into the cache fails: game_precache_new_map_refusable) and a map whose tags are damaged
(game_load fails: main_new_map). Then bloodgulch must load and draw its first frame.

Needs a build (build/macos/halo; or build/linux_x86_64/halo or build/linux_arm64/halo, drawn offscreen) and the
retail maps in assets/maps. The run has its own folders, no sound and no network; the map cache keeps the Xbox's
budget, so that no map is copied ahead (map_cache_prefill.c)."""
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
import zlib
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
MAPS = ROOT / "assets" / "maps"
CONSOLE_PORT = 2391
# (seconds into the run, the command): two seconds apart, the main menu up by the first
BAD_NAMES = [
    "levels\\test\\bloodgluch\\bloodgluch",
    "",
    "custom_maps\\not_there",
    "levels\\test\\junk\\junk",
    "levels\\test\\prisoner\\prisoner",
    "levels\\test\\putput\\putput",
]
GOOD_NAME = "levels\\test\\bloodgulch\\bloodgulch"


def build_folder():
    if sys.platform == "darwin":
        return ROOT / "build" / "macos"
    for name in ("linux_x86_64", "linux_arm64"):
        if (ROOT / "build" / name / "halo").is_file():
            return ROOT / "build" / name
    return None


@pytest.fixture(scope="module")
def game(tmp_path_factory) -> Path:
    """the build's executable, copied with what it loads beside it to a folder of its own (its config.toml is the
    test's)"""
    folder = build_folder()
    needed = ("ui.map", "bloodgulch.map", "prisoner.map", "putput.map")
    if not folder or not (folder / "halo").is_file() or not all((MAPS / name).is_file() for name in needed):
        pytest.skip("needs a macOS or Linux build and the retail maps in assets/maps")
    copy = tmp_path_factory.mktemp("game")
    for name in ("halo", "libSDL3.0.dylib", "libSDL3.so.0", "shaders.metallib", "openh1.assets", "prepared.records",
                 "brokers.txt", "masters.txt", "relays.txt"):
        if (folder / name).is_file():
            shutil.copy2(folder / name, copy / name)
    return copy / "halo"


def damaged_maps(folder: Path):
    """the retail maps, but junk.map (no map), prisoner.map cut short and putput.map with its tag header damaged"""
    folder.mkdir()
    for path in MAPS.glob("*.map"):
        if path.name not in ("prisoner.map", "putput.map"):
            (folder / path.name).symlink_to(path.resolve())
    (folder / "junk.map").write_bytes(bytes(range(256)) * 8)
    with open(MAPS / "prisoner.map", "rb") as source:
        (folder / "prisoner.map").write_bytes(source.read(3 << 20))
    # (an Xbox map: a 2 KB header, then the rest deflated; the tags' header at the header's offset 16)
    data = (MAPS / "putput.map").read_bytes()
    header, body = data[:2048], bytearray(zlib.decompress(data[2048:]))
    tags = int.from_bytes(header[16:20], "little") - 2048
    body[tags:tags + 64] = b"\xff" * 64
    (folder / "putput.map").write_bytes(header + zlib.compress(bytes(body), 1))


def send(commands, start):
    """each (seconds, command) to the script console (debug.telnet_console) that many seconds after start"""
    for seconds, command in commands:
        time.sleep(max(0.0, start + seconds - time.monotonic()))
        try:
            with socket.create_connection(("127.0.0.1", CONSOLE_PORT), timeout=5) as console:
                console.sendall(f"{command}\r\n".encode())
                time.sleep(0.5)
        except OSError:
            pass


def play(game: Path, tmp_path: Path, seconds: float, commands=(), init=None):
    """the game run for that many seconds, sent commands: (exit status, its output, debug.txt)"""
    data, saves, home = tmp_path / "data", tmp_path / "saves", tmp_path / "home"
    for folder in (data, saves, home):
        folder.mkdir()
    damaged_maps(data / "maps")
    if init is not None:
        (data / "init.txt").write_text(init)
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
        "HALO_DATA_ROOT": str(data), "HALO_SAVE_ROOT": str(saves), "HALO_MAP_CACHE_MEGABYTES": "730",
        "HALO_EXIT_AFTER": str(seconds), "HALO_HIDDEN_WINDOW": "1", "HALO_NO_CLIPBOARD": "1", "HALO_LOAD_TRACE": "1",
        "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": "640x480", "HALO_VOLUME": "0", "HALO_NO_AUDIO": "1",
        "SDL_AUDIO_DRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "HALO_UPDATE_AUTO": "false",
        "HALO_NET_ONLINE": "false", "HALO_NET_ADDRESS": "127.0.0.1", "HALO_NET_BROADCAST": "127.255.255.255",
        "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_PUBLIC_LOBBY": "false",
        "HALO_TELNET_CONSOLE": "1", "HALO_TELNET_CONSOLE_PORT": str(CONSOLE_PORT),
    }
    if sys.platform != "darwin":
        environment["SDL_VIDEODRIVER"] = "offscreen"
    start = time.monotonic()
    sender = threading.Thread(target=send, args=(commands, start), daemon=True)
    sender.start()
    result = subprocess.run([str(game)], cwd=game.parent, env=environment, capture_output=True, text=True,
                            errors="replace", timeout=seconds + 60)
    sender.join(timeout=1)
    debug = data / "debug.txt"
    return result.returncode, result.stdout + result.stderr, debug.read_text(errors="replace") if debug.is_file() else ""


def first_frame(output, map_name):
    """whether the map drew its first frame (debug.load_trace)"""
    return any("first_game_frame.submitted" in line and line.rstrip().endswith(f"detail={map_name}")
               for line in output.splitlines())


def test_map_name_refuses_a_map_it_cannot_load(game, tmp_path):
    commands = [(10 + 2 * index, f'map_name "{name}"') for index, name in enumerate(BAD_NAMES)]
    commands.append((10 + 2 * len(BAD_NAMES) + 2, f'map_name "{GOOD_NAME}"'))
    status, output, debug = play(game, tmp_path, 40, commands)
    assert status == 0, output[-3000:]
    assert "EXCEPTION" not in debug, debug[-3000:]
    for name in BAD_NAMES:
        assert f"can't load map '{name}'" in debug, f"map_name {name!r} not refused:\n{debug[-3000:]}"
    assert first_frame(output, GOOD_NAME), f"bloodgulch did not load after the refused maps:\n{debug[-3000:]}"


def test_init_txt_map_name_refused_for_the_main_menu(game, tmp_path):
    status, output, debug = play(game, tmp_path, 25, [(15, f'map_name "{GOOD_NAME}"')],
                                 init='map_name "levels\\test\\bloodgluch\\bloodgluch"\n')
    assert status == 0, output[-3000:]
    assert "EXCEPTION" not in debug, debug[-3000:]
    assert "can't load map 'levels\\test\\bloodgluch\\bloodgluch'" in debug, debug[-3000:]
    assert first_frame(output, "levels\\ui\\ui"), f"the main menu did not load:\n{debug[-3000:]}"
    assert first_frame(output, GOOD_NAME), f"bloodgulch did not load after:\n{debug[-3000:]}"
