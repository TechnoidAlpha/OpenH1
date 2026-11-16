#!/usr/bin/env python3
"""Pictures of the game in a named situation, taken without a person at it.

    python tools/capture.py --scene sniper-zoom-10x --out shots/
    python tools/capture.py --scene spawn --set display.fov=100 --crop 760,340,400,400
    python tools/capture.py --scene warthog-passenger --compare ../other/build/macos
    python tools/capture.py --scene "camera:20 -60 2 90 -5" --map bloodgulch
    python tools/capture.py --scene hud --frames 30 --every 2
    python tools/capture.py --scene spawn --script "2 aim=0,-60; 2.5-4 move=0,1" --at 3.5

One run of a hidden, muted game, in folders of its own that are deleted
afterwards (its data root with the maps linked in, its saves, HOME, TMPDIR,
XDG_CACHE_HOME), gets the player there by the automated test settings
(debug.network_test "local:<map>", its _pickup, _vehicle and _cues,
debug.test_input's "script:"), and writes the frame (debug.screenshot_every
from a second of the scenario's simulation clock) as a PNG in --out, with
--crop's part as another; the paths are printed. --compare runs the same
scene with another build at the same time and writes the two side by side
(the other build's on the left, before | after). --list prints the scenes.

macOS: build/macos (halo, openh1.assets, shaders.metallib, libSDL3.0.dylib,
brokers.txt are copied into the run's folder, whose config.toml is then the
run's). Linux: build/linux_x86_64 or build/linux_arm64, with --xvfb on a
virtual display of its own (/usr/bin/Xvfb), else SDL's offscreen video.
The maps are --data's, else ~/dev/halo-macos-data/maps on macOS when it is
there, else assets/maps. The game's random seed is fixed (--seed), so that
two runs, and the two builds of --compare, spawn the player in one place.
The scoreboard and pause menu scenes need a build whose "script:" has back
and start, and the Warthog's other seats one with
debug.network_test_vehicle_seat (a build without seats its driver).

--remote <host> takes the pictures on another computer over ssh (a Linux
box's GPU, from a Mac): this script is sent to it with the other options,
and runs there in a folder of its own in the remote's /tmp against
--remote-tree's clone (~/openh1-vulkan-check; its build folders, built
there) and --remote-data's maps (~/halo-x86-maps/maps). --build and
--compare are then the remote's paths, relative to that clone. The PNG files
come back as a tar on ssh's output, into --out, and the remote folder is
deleted. The commands go to the remote's bash on its input, so that its
login shell (fish, say) reads none of them.

    python tools/capture.py --remote 5900x.fabric --scene sniper-zoom-10x --xvfb
    python tools/capture.py --remote 5900x.fabric --scene hud --compare ../master/build/linux_x86_64
"""
import argparse
import io
import os
import re
import shlex
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAC_MAPS = Path.home() / "dev" / "halo-macos-data" / "maps"
# what a run's folder takes from the build folder, those of them it has
BUILD_FILES = ("halo", "prepared.records", "openh1.assets", "shaders.metallib", "libSDL3.0.dylib", "libSDL3.so.0", "brokers.txt")


@dataclass
class Scene:
    """a situation: the network test's game, the scripted player, and the second of the
    simulation's clock (from the scenario's start) the picture is taken at"""
    description: str
    map: str = "bloodgulch"
    at: float = 3.0
    script: str = ""
    environment: dict = field(default_factory=dict)
    # the variant of "local:<map>:<variant>"
    variant: str = ""
    # the local players (split screen)
    players: int = 1


# the sniper rifle picked up (two weapons first, then the rifle stood on at 2 s, X held
# from 3 s), the zoom clicked at 7 s and 9 s
_SNIPER = {"HALO_NETWORK_TEST_PICKUP": "2", "HALO_NETWORK_TEST_PICKUP_WEAPON": "sniper"}

SCENES = {
    "spawn": Scene("Blood Gulch, on foot at a spawn point, looking ahead", at=2.5),
    "hud": Scene("the combat HUD: a few rounds fired, the ammo, grenades and shields shown",
                 script="2-2.5 fire", at=3.5),
    "pause-menu": Scene("the pause menu over a game (--prompts keyboard or controller)",
                        script="2.5 start", at=4.0),
    "scoreboard": Scene("the scoreboard, Back held", script="2.5-6 back", at=4.0),
    "sniper-zoom-2x": Scene("the sniper rifle's scope at 2x", script="3-5 action; 7 zoom", at=8.5,
                            environment=_SNIPER),
    "sniper-zoom-10x": Scene("the sniper rifle's scope at 10x", script="3-5 action; 7 zoom; 8.5 zoom", at=10.0,
                             environment=_SNIPER),
    "warthog-driver": Scene("a Warthog's driver's seat (third person)", at=5.0,
                            environment={"HALO_NETWORK_TEST_VEHICLE": "2",
                                         "HALO_NETWORK_TEST_VEHICLE_SEAT": "warthog:driver"}),
    "warthog-passenger": Scene("a Warthog's passenger's seat (first person)", at=5.0,
                               environment={"HALO_NETWORK_TEST_VEHICLE": "2",
                                            "HALO_NETWORK_TEST_VEHICLE_SEAT": "warthog:passenger"}),
    "warthog-gunner": Scene("a Warthog's gun (third person)", at=5.0,
                            environment={"HALO_NETWORK_TEST_VEHICLE": "2",
                                         "HALO_NETWORK_TEST_VEHICLE_SEAT": "warthog:gunner"}),
    "split-screen": Scene("two local players in split screen", players=2, at=2.5),
}
# made from their argument: campaign:<level> (a10, levels\\a10\\a10), camera:<x y z yaw pitch>
PATTERNS = {
    "campaign:<level>": "a campaign level (a10, b30, ...) as network co-op on its own, from its start (--at)",
    "camera:<x y z yaw pitch>": "the debug camera at that place and aim (degrees) on --map, as report.md's"
                                " \"Where\" gives debug_camera_set",
}


# the campaign's levels, which a network test plays as co-op (campaign:<level>), not as a multiplayer map
CAMPAIGN_LEVELS = ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")


def campaign_level(map_name: str) -> str:
    """a campaign level's full name (levels\\a50\\a50) for --map's a50 or levels\\a50\\a50; empty for any other map"""
    name = map_name.replace("/", "\\").strip()
    short = name.rsplit("\\", 1)[-1].lower()
    if short in CAMPAIGN_LEVELS and (name.lower() == short or name.lower() == f"levels\\{short}\\{short}"):
        return f"levels\\{short}\\{short}"
    return ""


def scene_for(name: str, map_name: str = "") -> Scene:
    """the scene of a name, or of a pattern's"""
    if name in SCENES:
        scene = SCENES[name]
        if map_name:
            scene = Scene(**{**scene.__dict__, "map": map_name})
        return scene
    if name.startswith("campaign:"):
        level = name[len("campaign:"):].strip()
        if not level:
            raise ValueError("campaign: needs a level (campaign:a10)")
        if "\\" not in level and "/" not in level:
            level = f"levels\\{level}\\{level}"
        # (the opening cinematic skipped: A votes to skip it, and the one player's vote is all of them)
        return Scene(f"the campaign level {level}", map=level.replace("/", "\\"), variant="coop", at=10.0,
                     script="1 jump; 2 jump; 3 jump")
    if name.startswith("camera:"):
        numbers = re.split(r"[\s,]+", name[len("camera:"):].strip())
        try:
            values = [float(number) for number in numbers if number]
        except ValueError:
            values = []
        if len(values) not in (5, 6, 7):
            raise ValueError("camera: takes <x y z yaw pitch> (and a roll and a field of view)")
        x, y, z, yaw, pitch = values[:5]
        roll = values[5] if len(values) > 5 else 0.0
        fov = values[6] if len(values) > 6 else 70.0
        pose = " ".join(f"{value:g}" for value in (x, y, z, yaw, pitch, roll, fov))
        level = campaign_level(map_name)
        if level:
            # (a campaign level is played as network co-op on its own, as campaign:<level>: its opening
            # cinematic skipped, then the camera put in place once the level is under way)
            return Scene(f"the debug camera at {pose} on {level}", map=level, variant="coop", at=25.0,
                         script="1 jump; 2 jump; 3 jump",
                         # (set again every few seconds: a cinematic, or its end, takes the camera back)
                         environment={"HALO_NETWORK_TEST_CUES": "|".join(
                             f'{second} debug_camera_set "{pose}"' for second in range(2, 25, 4))})
        return Scene(f"the debug camera at {pose} on {map_name or 'bloodgulch'}", map=map_name or "bloodgulch",
                     at=2.5, environment={"HALO_NETWORK_TEST_CUES": f'1 debug_camera_set "{pose}"'})
    raise ValueError(f"no scene {name!r} (--list lists them)")


def scene_file_name(name: str) -> str:
    """a scene's name as a file's: campaign:a10 is campaign-a10, camera:... camera"""
    if name.startswith("camera:"):
        return "camera"
    return re.sub(r"[^A-Za-z0-9_.-]+", "-", name).strip("-")


def parse_size(text: str):
    match = re.fullmatch(r"(\d+)x(\d+)", text)
    if not match or int(match.group(1)) < 64 or int(match.group(2)) < 64:
        raise argparse.ArgumentTypeError(f"--size takes <width>x<height>, not {text!r}")
    return int(match.group(1)), int(match.group(2))


def parse_crop(text: str):
    parts = text.split(",")
    if len(parts) != 4 or not all(part.strip().isdigit() for part in parts):
        raise argparse.ArgumentTypeError(f"--crop takes x,y,w,h, not {text!r}")
    x, y, width, height = (int(part) for part in parts)
    if width <= 0 or height <= 0:
        raise argparse.ArgumentTypeError("--crop's width and height must be above 0")
    return x, y, width, height


def parse_setting(text: str):
    if "=" not in text or not text.split("=", 1)[0].strip():
        raise argparse.ArgumentTypeError(f"--set takes <setting>=<value>, not {text!r}")
    name, value = text.split("=", 1)
    return name.strip(), value.strip()


def setting_variables(tree: Path = ROOT) -> dict:
    """each setting's environment variable, from port_config.c's table"""
    source = tree / "port" / "linux" / "src" / "port_config.c"
    pattern = re.compile(r'\{\s*"([a-z0-9_]+\.[a-z0-9_.]+)",\s*_config_\w+,\s*(?:"(?:[^"\\]|\\.)*"|\w+),\s*"(HALO_[A-Z0-9_]+)"')
    return dict(pattern.findall(source.read_text(errors="replace")))


def make_parser() -> argparse.ArgumentParser:
    # (no abbreviations: --remote passes on the options it does not know by their names)
    parser = argparse.ArgumentParser(description="Pictures of the game in a named situation, without a person at it.",
                                     allow_abbrev=False)
    parser.add_argument("--scene", help="the situation (--list lists them)")
    parser.add_argument("--list", action="store_true", help="print the scenes and exit")
    parser.add_argument("--out", type=Path, default=Path("shots"), help="the folder the PNG files go to")
    parser.add_argument("--build", type=Path, help="the build folder (build/macos; build/linux_x86_64)")
    parser.add_argument("--compare", type=Path, metavar="BUILD",
                        help="another build folder: the same scene with it, and the two side by side (it | --build)")
    parser.add_argument("--data", type=Path, help="the maps folder")
    parser.add_argument("--map", default="", help="the map of a camera: scene (or another map for the others)")
    parser.add_argument("--set", type=parse_setting, action="append", default=[], metavar="SETTING=VALUE",
                        help="a setting for the run (display.graphics=classic), as many as wanted")
    parser.add_argument("--crop", type=parse_crop, help="x,y,w,h: also that part of the frame, as its own PNG")
    parser.add_argument("--size", type=parse_size, default=(1920, 1080), help="the window's size (1920x1080)")
    parser.add_argument("--prompts", choices=("auto", "keyboard", "controller"), default="auto",
                        help="the button prompts (input.button_prompts)")
    parser.add_argument("--script", help="debug.test_input's script for the player, in place of the scene's (\"2 aim=0,-60;"
                        " 2.5-4 move=0,1\": port/linux/src/xinput_sdl.c)")
    parser.add_argument("--at", type=float, help="the second of the scene's clock to take it at (the scene's own)")
    parser.add_argument("--frames", type=int, default=1, help="pictures to take (an animation)")
    parser.add_argument("--every", type=int, default=1, help="with --frames, every this many frames drawn")
    parser.add_argument("--seed", type=int, default=1,
                        help="the game's random seed (debug.random_seed: where the player spawns; 0 the clock's)")
    parser.add_argument("--xvfb", action="store_true", help="Linux: a virtual X display of its own")
    parser.add_argument("--timeout", type=float, default=90.0, help="seconds a run may take")
    parser.add_argument("--keep", action="store_true", help="keep the run's folder (its debug.txt) and say where")
    parser.add_argument("--name", help="the files' name (the scene's)")
    parser.add_argument("--tree", type=Path, default=ROOT,
                        help="the source tree whose build folders and settings are used (this script's)")
    parser.add_argument("--remote", metavar="HOST", help="take the pictures on that host over ssh (Linux)")
    parser.add_argument("--remote-tree", default="~/openh1-vulkan-check", metavar="PATH",
                        help="with --remote, the OpenH1 clone there (built)")
    parser.add_argument("--remote-data", default="~/halo-x86-maps/maps", metavar="PATH",
                        help="with --remote, the maps folder there")
    return parser


# the options --remote does not pass on (each with its value): its own, and --out (the remote's is its folder's)
REMOTE_OWN = ("--remote", "--remote-tree", "--remote-data", "--out")
# those whose value is a path on the remote
REMOTE_PATHS = ("--build", "--compare")


def check_arguments(parser, arguments):
    if arguments.list:
        return
    if not arguments.scene:
        parser.error("--scene is needed (--list lists them)")
    try:
        scene_for(arguments.scene, arguments.map)
    except ValueError as error:
        parser.error(str(error))
    if arguments.frames < 1 or arguments.every < 1:
        parser.error("--frames and --every must be 1 or more")
    if arguments.remote:
        if arguments.keep:
            parser.error("--keep keeps a run's folder on this computer; not with --remote")
        if arguments.data or arguments.tree != ROOT:
            parser.error("with --remote, the maps and the clone are --remote-data's and --remote-tree's")
    variables = setting_variables(arguments.tree)
    for name, _ in arguments.set:
        if name not in variables:
            parser.error(f"no setting {name!r} in port_config.c")


# pictures: 32-bit BMP files as the game writes them (xgpu_write_bmp), RGB in memory, PNG out

@dataclass
class Picture:
    width: int
    height: int
    rows: list  # each a bytes of width * 3, RGB, the top first

    def crop(self, x, y, width, height):
        x, y = max(0, min(x, self.width)), max(0, min(y, self.height))
        width, height = min(width, self.width - x), min(height, self.height - y)
        if width <= 0 or height <= 0:
            raise ValueError("--crop is outside the frame")
        return Picture(width, height, [row[x * 3:(x + width) * 3] for row in self.rows[y:y + height]])


def bmp_complete(path: Path) -> bool:
    """whether a BMP file has all the bytes its header says (the game may be writing it)"""
    try:
        with open(path, "rb") as file:
            header = file.read(6)
        return len(header) == 6 and header[:2] == b"BM" and path.stat().st_size >= struct.unpack_from("<I", header, 2)[0]
    except OSError:
        return False


def fit(picture: Picture, size) -> Picture:
    """a picture of a whole multiple of the size asked for (a Retina display's window) made that size, each
    pixel the average of those it covers"""
    width, height = size
    factor = picture.width // width
    if factor <= 1 or picture.width != width * factor or picture.height != height * factor:
        return picture
    rows = []
    for index in range(height):
        sums = [0] * (width * 3)
        for row in picture.rows[index * factor:(index + 1) * factor]:
            for offset in range(factor):
                for channel in range(3):
                    column = row[offset * 3 + channel::factor * 3]
                    sums[channel::3] = [a + b for a, b in zip(sums[channel::3], column)]
        area = factor * factor
        rows.append(bytes(value // area for value in sums))
    return Picture(width, height, rows)


def window_size(size):
    """the window's size, in the display's points, that draws the pixels asked for (macOS: half on a Retina
    display), at least 640x480"""
    width, height = size
    if sys.platform != "darwin":
        return size
    try:
        scale = int(float(subprocess.run(
            ["osascript", "-l", "JavaScript", "-e", 'ObjC.import("AppKit"); $.NSScreen.mainScreen.backingScaleFactor'],
            capture_output=True, text=True, timeout=10).stdout.strip() or 1))
    except (OSError, ValueError, subprocess.SubprocessError):
        scale = 1
    if scale > 1 and width % scale == 0 and height % scale == 0 and width // scale >= 640 and height // scale >= 480:
        return width // scale, height // scale
    return size


def read_bmp(path: Path) -> Picture:
    data = path.read_bytes()
    if data[:2] != b"BM" or len(data) < 54:
        raise ValueError(f"{path} is no BMP file")
    offset, = struct.unpack_from("<I", data, 10)
    width, height, _, bits = struct.unpack_from("<iiHH", data, 18)
    if bits not in (24, 32):
        raise ValueError(f"{path}: {bits}-bit BMP")
    top_down = height < 0
    height = abs(height)
    step = bits // 8
    stride = (width * step + 3) & ~3
    if len(data) < offset + stride * height:
        raise ValueError(f"{path} is cut short")
    rows = []
    for index in range(height):
        start = offset + stride * (index if top_down else height - 1 - index)
        source = data[start:start + width * step]
        row = bytearray(width * 3)
        row[0::3] = source[2::step]
        row[1::3] = source[1::step]
        row[2::3] = source[0::step]
        rows.append(bytes(row))
    return Picture(width, height, rows)


def write_png(picture: Picture, path: Path):
    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)

    raw = b"".join(b"\x00" + row for row in picture.rows)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", picture.width, picture.height,
                                                                       8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def side_by_side(left: Picture, right: Picture, gap: int = 8) -> Picture:
    """the two in a row, a dark gap between, the shorter's foot dark"""
    height = max(left.height, right.height)
    blank = b"\x10\x10\x10"
    rows = []
    for index in range(height):
        a = left.rows[index] if index < left.height else blank * left.width
        b = right.rows[index] if index < right.height else blank * right.width
        rows.append(a + blank * gap + b)
    return Picture(left.width + gap + right.width, height, rows)


# runs

def default_build(tree: Path = ROOT) -> Path:
    if sys.platform == "darwin":
        return tree / "build" / "macos"
    for name in ("linux_x86_64", "linux_arm64"):
        if (tree / "build" / name / "halo").is_file():
            return tree / "build" / name
    return tree / "build" / "linux_x86_64"


def default_maps(tree: Path = ROOT) -> Path:
    if sys.platform == "darwin" and (MAC_MAPS / "bloodgulch.map").is_file():
        return MAC_MAPS
    return tree / "assets" / "maps"


def free_port(kind=socket.SOCK_DGRAM) -> int:
    with socket.socket(socket.AF_INET, kind) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def free_display(start: int = 90) -> int:
    for number in range(start, 400):
        if not Path(f"/tmp/.X11-unix/X{number}").exists() and not Path(f"/tmp/.X{number}-lock").exists():
            return number
    raise RuntimeError("no free X display")


def start_xvfb(size):
    """/usr/bin/Xvfb on a free display, once its socket is there (another run, --compare's, may take the
    display first: then the next): the process and the display's number"""
    if not Path("/usr/bin/Xvfb").is_file():
        raise RuntimeError("--xvfb needs /usr/bin/Xvfb")
    width, height = size
    number = 90
    for _ in range(8):
        number = free_display(number)
        xvfb = subprocess.Popen(["/usr/bin/Xvfb", f":{number}", "-screen", "0", f"{width}x{height}x24",
                                 "-nolisten", "tcp"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, start_new_session=True)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline and xvfb.poll() is None:
            if Path(f"/tmp/.X11-unix/X{number}").exists():
                return xvfb, number
            time.sleep(0.05)
        if xvfb.poll() is None:
            xvfb.kill()
        xvfb.wait()
        number += 1
    raise RuntimeError("Xvfb did not start")


def run_environment(scene: Scene, arguments, folder: Path, network_test: str, at: float) -> dict:
    """the run's environment: its own folders, hidden, muted, off the network and the clipboard"""
    home, data, saves = folder / "home", folder / "data", folder / "saves"
    shots = folder / "shots"
    for path in (home, data, saves, shots, home / "cache"):
        path.mkdir(parents=True, exist_ok=True)
    width, height = window_size(arguments.size)
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
        "XDG_CACHE_HOME": str(home / "cache"), "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_DATA_HOME": str(home / "share"),
        "HALO_DATA_ROOT": str(data), "HALO_SAVE_ROOT": str(saves),
        "HALO_HIDDEN_WINDOW": "1", "SDL_AUDIO_DRIVER": "dummy", "HALO_VOLUME": "0",
        "HALO_NO_CLIPBOARD": "1", "HALO_NET_ONLINE": "false", "HALO_UPDATE_AUTO": "false",
        "HALO_NET_PORT": str(free_port()), "HALO_NET_ADDRESS": "127.0.0.1",
        "HALO_NET_BROADCAST": "127.255.255.255", "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_PUBLIC_LOBBY": "false",
        "HALO_MAP_CACHE_MEGABYTES": "730", "HALO_EXIT_AFTER": str(int(arguments.timeout)),
        "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": f"{width}x{height}",
        "HALO_RESOLUTION_SCALING": "native", "HALO_BUTTON_PROMPTS": arguments.prompts,
        "HALO_NETWORK_TEST": network_test, "HALO_NETWORK_TEST_START": "3",
        "HALO_NETWORK_TEST_PLAYERS": str(scene.players),
        "HALO_SCREENSHOT_DIR": str(shots), "HALO_SCREENSHOT_EVERY": str(arguments.every),
        "HALO_SCREENSHOT_FROM": f"{at:g}", "HALO_RANDOM_SEED": str(arguments.seed),
    }
    if scene.script:
        environment["HALO_TEST_INPUT"] = "script:" + scene.script
    environment.update(scene.environment)
    variables = setting_variables(arguments.tree)
    for name, value in arguments.set:
        environment[variables[name]] = value
    return environment


def take(build: Path, scene: Scene, arguments, maps: Path, label: str):
    """one run: its frames' paths, read as pictures, and how long it took"""
    started = time.monotonic()
    if not (build / "halo").is_file():
        raise RuntimeError(f"no {build / 'halo'}")
    folder = Path(tempfile.mkdtemp(prefix=f"capture-{label}-"))
    xvfb = None
    process = None
    try:
        game = folder / "game"
        game.mkdir()
        for name in BUILD_FILES:
            if (build / name).is_file():
                shutil.copy2(build / name, game / name)
        if sys.platform != "darwin":
            for library in build.glob("*.so*"):
                shutil.copy2(library, game / library.name)
        at = scene.at if arguments.at is None else arguments.at
        network_test = f"local:{scene.map}" + (f":{scene.variant}" if scene.variant else "")
        environment = run_environment(scene, arguments, folder, network_test, at)
        (folder / "data" / "maps").symlink_to(maps.resolve())
        if sys.platform != "darwin":
            if arguments.xvfb:
                xvfb, display = start_xvfb(arguments.size)
                environment.update({"DISPLAY": f":{display}", "SDL_VIDEO_DRIVER": "x11"})
            elif os.environ.get("DISPLAY") and os.environ.get("SDL_VIDEO_DRIVER") == "x11":
                environment.update({"DISPLAY": os.environ["DISPLAY"], "SDL_VIDEO_DRIVER": "x11"})
            else:
                environment["SDL_VIDEO_DRIVER"] = "offscreen"
        log = open(folder / "halo.log", "w")
        process = subprocess.Popen([str(game / "halo")], cwd=game, env=environment, stdin=subprocess.DEVNULL,
                                   stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        shots = folder / "shots"
        complete = []
        while time.monotonic() - started < arguments.timeout + 10:
            # (the last one may be being written)
            complete = [frame for frame in sorted(shots.glob("*.bmp")) if bmp_complete(frame)]
            if len(complete) >= arguments.frames:
                break
            if process.poll() is not None:
                break
            time.sleep(0.05)
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        log.close()
        if len(complete) < arguments.frames:
            tail = (folder / "halo.log").read_text(errors="replace")[-3000:]
            raise RuntimeError(f"{label}: {len(complete)} of {arguments.frames} frames in {arguments.timeout:g} s"
                               f" (the scene's clock never reached {at:g} s?)\n{tail}")
        pictures = [fit(read_bmp(frame), arguments.size) for frame in complete[:arguments.frames]]
        return pictures, time.monotonic() - started, folder
    finally:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        if xvfb:
            xvfb.terminate()
            xvfb.wait()
        if not arguments.keep:
            shutil.rmtree(folder, ignore_errors=True)


def save(pictures, arguments, out: Path, stem: str):
    """each picture (and its crop) as PNG files: their paths"""
    paths = []
    for index, picture in enumerate(pictures):
        suffix = f"-{index:03d}" if len(pictures) > 1 else ""
        path = out / f"{stem}{suffix}.png"
        write_png(picture, path)
        paths.append(path)
        if arguments.crop:
            path = out / f"{stem}{suffix}-crop.png"
            write_png(picture.crop(*arguments.crop), path)
            paths.append(path)
    return paths


# --remote

def remote_path(text: str) -> str:
    """a path for the remote's bash: ~ its HOME, the rest quoted"""
    if text == "~":
        return '"$HOME"'
    if text.startswith("~/"):
        return '"$HOME"/' + shlex.quote(text[2:])
    return shlex.quote(text)


def remote_arguments(argv) -> list:
    """the command line's options for the remote's capture.py, quoted for its bash: --remote's own and --out
    left out, --build's and --compare's paths the remote's"""
    words, index = [], 0
    while index < len(argv):
        word = argv[index]
        name, equals, value = word.partition("=")
        index += 1
        if name in REMOTE_OWN:
            if not equals:
                index += 1
            continue
        if name in REMOTE_PATHS:
            if not equals:
                value = argv[index] if index < len(argv) else ""
                index += 1
            words += [name, remote_path(value)]
            continue
        words.append(shlex.quote(word))
    return words


def remote_script(arguments, argv, source: str) -> str:
    """the bash script that takes the pictures on the remote: this script written to a folder of its own in
    /tmp, run from the clone, its output on stderr, the PNG files as a tar on stdout, the folder deleted. (A
    function, read whole before it runs, so that what it runs cannot read the rest of the script from the
    input.)"""
    end = "CAPTURE_PY_END"
    assert f"\n{end}\n" not in source
    command = " ".join(["python3", '"$folder/capture.py"', "--tree", '"$tree"', "--data",
                        remote_path(arguments.remote_data), "--out", '"$folder/out"']
                       + remote_arguments(argv))
    return f"""capture_remote() {{
folder=$(mktemp -d /tmp/capture-remote-XXXXXX) || exit 1
trap 'rm -rf "$folder"' EXIT
mkdir "$folder/out"
cat > "$folder/capture.py" <<'{end}'
{source}
{end}
tree={remote_path(arguments.remote_tree)}
cd "$tree" || exit 1
{command} 1>&2
status=$?
COPYFILE_DISABLE=1 tar -C "$folder/out" -cf - .
exit $status
}}
capture_remote < /dev/null
"""


def remote(arguments, argv) -> int:
    """the pictures taken on --remote's host, its PNG files put in --out: the exit status"""
    script = remote_script(arguments, argv, Path(__file__).read_text())
    started = time.monotonic()
    # (as a compare's two runs at most, and the copying)
    limit = arguments.timeout * 2 + 300
    try:
        result = subprocess.run(["ssh", "-o", "BatchMode=yes", arguments.remote, "bash -s"],
                                input=script.encode(), stdout=subprocess.PIPE, timeout=limit)
    except subprocess.TimeoutExpired:
        print(f"capture: {arguments.remote} took over {limit:g} s", file=sys.stderr)
        return 1
    arguments.out.mkdir(parents=True, exist_ok=True)
    paths = []
    try:
        with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
            for member in archive.getmembers():
                name = member.name[2:] if member.name.startswith("./") else member.name
                if not member.isfile() or not name.endswith(".png") or "/" in name or name.startswith("."):
                    continue
                path = arguments.out / name
                path.write_bytes(archive.extractfile(member).read())
                paths.append(path)
    except tarfile.TarError:
        print(f"capture: no pictures came back from {arguments.remote}", file=sys.stderr)
        return result.returncode or 1
    print(f"capture: {arguments.remote}: {time.monotonic() - started:.1f} s in all", file=sys.stderr)
    for path in sorted(paths):
        print(path)
    if not paths:
        print(f"capture: no pictures came back from {arguments.remote}", file=sys.stderr)
        return result.returncode or 1
    return result.returncode


def main(argv=None) -> int:
    parser = make_parser()
    argv = sys.argv[1:] if argv is None else list(argv)
    arguments = parser.parse_args(argv)
    check_arguments(parser, arguments)
    if arguments.list:
        for name, scene in SCENES.items():
            print(f"{name:20} {scene.description}")
        for name, description in PATTERNS.items():
            print(f"{name:20} {description}")
        return 0
    if arguments.remote:
        return remote(arguments, argv)
    scene = scene_for(arguments.scene, arguments.map)
    if arguments.script is not None:
        scene = Scene(**{**scene.__dict__, "script": arguments.script})
    tree = arguments.tree.resolve()
    build = (arguments.build or default_build(tree)).resolve()
    maps = (arguments.data or default_maps(tree)).resolve()
    if not (maps / "bloodgulch.map").is_file() and not (maps / "ui.map").is_file():
        parser.error(f"no maps in {maps} (--data)")
    arguments.out.mkdir(parents=True, exist_ok=True)
    stem = arguments.name or scene_file_name(arguments.scene)

    runs = {"after": build}
    if arguments.compare:
        runs = {"before": arguments.compare.resolve(), "after": build}
    results, errors = {}, {}

    def run(label, folder):
        try:
            results[label] = take(folder, scene, arguments, maps, label)
        except Exception as error:  # (reported below, with the other run's result)
            errors[label] = error

    threads = [threading.Thread(target=run, args=item) for item in runs.items()]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    for label, error in errors.items():
        print(f"capture: {label} ({runs[label]}): {error}", file=sys.stderr)
    if errors:
        return 1

    paths = []
    for label, (pictures, seconds, folder) in results.items():
        name = stem if not arguments.compare else f"{stem}-{label}"
        paths += save(pictures, arguments, arguments.out, name)
        print(f"capture: {label}: {seconds:.1f} s" + (f", its folder {folder}" if arguments.keep else ""),
              file=sys.stderr)
    if arguments.compare:
        before, after = results["before"][0], results["after"][0]
        for index, (left, right) in enumerate(zip(before, after)):
            suffix = f"-{index:03d}" if len(before) > 1 else ""
            path = arguments.out / f"{stem}{suffix}-compare.png"
            write_png(side_by_side(left, right), path)
            paths.append(path)
            if arguments.crop:
                path = arguments.out / f"{stem}{suffix}-compare-crop.png"
                write_png(side_by_side(left.crop(*arguments.crop), right.crop(*arguments.crop)), path)
                paths.append(path)
    for path in paths:
        print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
