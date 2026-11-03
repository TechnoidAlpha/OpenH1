"""StageKit's tests' tools (README.md): build the generator, write a scenario's map, validate it, and play it.

The generator lays its tags out at the game's 32-bit addresses, so it is built as a guest image of the game's own
host, as the game is (its guest's objects' flags and link): the game's host runs it in place of the game
(HALO_GUEST_IMAGE). So it runs wherever the game does: the 64-bit Linux builds (linux_x86_64, linux_arm64) and the
macOS one.
"""

import ctypes
import os
import platform
import re
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
STAGEKIT = ROOT / "tools/stagekit"
# the builds with a guest, by the processor and system each configures on (tools/linux_guest_build.py,
# tools/macos_build.py)
GUEST_BUILDS = ("linux_x86_64", "linux_arm64", "macos")


def default_build():
    """the build StageKit plays on: HALO_STAGEKIT_BUILD, else this computer's own guest build, if built"""
    if os.environ.get("HALO_STAGEKIT_BUILD"):
        return os.environ["HALO_STAGEKIT_BUILD"]
    if sys.platform == "darwin":
        return "macos"
    return "linux_arm64" if platform.machine().lower() in ("aarch64", "arm64") else "linux_x86_64"


BUILD_NAME = default_build()
BUILD = ROOT / "build" / BUILD_NAME
# the game: its host, with the game's guest image inside it
GAME = Path(os.environ.get("HALO_GAME", BUILD / "halo"))
# the scenarios (scenarios/<name>.c), each played as its test module (test_<name>.py) says
SCENARIOS = sorted(path.stem for path in (STAGEKIT / "scenarios").glob("*.c"))
# the game's ticks a second
TICKS_PER_SECOND = 30
# the game's start and the map's loading, before it plays
LOADING_SECONDS = 10
# a map file's first words: 'head' and the version, the Xbox's 5 (xbox_files.c's map_file_version)
MAP_HEADER_SIGNATURE = 0x68656164
XBOX_MAP_VERSION = 5
# what the runtime of a guest image is, of the game's guest's objects (port/android/guest/runtime), and the host's
# imports (gen/imports.o); the tag schemas (port/linux/game/tag_schema_*.c), which set what the generator leaves
# unset to none
RUNTIME = re.compile(r"/guest/runtime/guest_(misc|start|string|syscall|thread)\.o$|/gen/imports\.o$")
TAG_SCHEMAS = re.compile(r"/port/linux/game/tag_schema[^/]*\.o$")
TEMPLATE = "source/scenario/scenario"


def can_build():
    """None if the generator can be built here, else why not"""
    if not shutil.which("ninja") or not (ROOT / "build.ninja").is_file():
        return "needs ninja and configure.py's build.ninja"
    if not GAME.is_file():
        return f"needs the game ({GAME.relative_to(ROOT) if GAME.is_relative_to(ROOT) else GAME}: ninja {BUILD_NAME})"
    return None


def ninja(*arguments):
    return subprocess.run(["ninja", *arguments], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def build(folder):
    """the generator, a guest image (folder/stagekit.elf): StageKit's sources compiled as the game's guest's sources
    are (the command that compiles scenario.c, its source and object swapped), linked as the game's guest image is,
    with the guest's runtime and the tag schemas in place of the game. The tag schemas' calls into the game are left
    unresolved (never called); anything of StageKit's own unresolved fails the build"""
    image = f"build/{BUILD_NAME}/halo_guest.elf"
    inputs = ninja("-t", "query", image).split("outputs:")[0].split()
    template = next((word for word in inputs if word.endswith(f"/{TEMPLATE}.o")), None)
    if not template:
        raise RuntimeError(f"{image}'s inputs have no {TEMPLATE}.o: is build.ninja {BUILD_NAME}'s?")
    compile_command = ninja("-t", "commands", template).strip().splitlines()[-1]
    compile_command = re.sub(r" -fprofile-use=\S+", "", compile_command)
    sources = sorted(STAGEKIT.glob("*.c")) + sorted((STAGEKIT / "scenarios").glob("*.c"))
    objects = []
    for source in sources:
        target = folder / f"{source.parent.name}_{source.stem}.o"
        command = compile_command.replace(template, str(target)).replace(f"{TEMPLATE}.c", str(source))
        compiled = subprocess.run(command, shell=True, cwd=ROOT, capture_output=True, text=True)
        if compiled.returncode:
            raise RuntimeError(f"stagekit did not build: {source.name}:\n{compiled.stderr[-4000:]}")
        objects.append(str(target))
    objects += [str(ROOT / word) for word in inputs if RUNTIME.search(word) or TAG_SCHEMAS.search(word)]
    words = shlex.split(ninja("-t", "commands", image).strip().splitlines()[-1])
    if "-Map" in words:
        del words[words.index("-Map"):words.index("-Map") + 2]
    program = folder / "stagekit.elf"
    words[words.index("-o") + 1] = str(program)
    words = [word for part in words for word in (objects if part.startswith("@") else [part])]
    linked = subprocess.run([*words, "--warn-unresolved-symbols"], cwd=ROOT, capture_output=True, text=True)
    # (each undefined symbol, then the objects that refer to it)
    own = [block for block in re.split(r"(?=ld\.lld: warning: undefined symbol)", linked.stderr)
           if "undefined symbol" in block and str(folder) in block]
    if linked.returncode or own:
        raise RuntimeError(f"stagekit did not link:\n{linked.stderr[-4000:]}")
    return program


def run_tool(*command, **environment):
    """a tool's output; one that fails raises with what it said"""
    result = subprocess.run([str(word) for word in command], capture_output=True, text=True,
                            env=dict(os.environ, **environment) if environment else None)
    if result.returncode:
        raise RuntimeError(f"{' '.join(map(str, command))} failed ({result.returncode}):\n"
                           f"{result.stdout}{result.stderr}")
    return result.stdout


def generate(program, scenario, folder, parameters=None):
    """the scenario's map (custom_maps/void.map) under folder: the generator run by the game's host in place of the
    game; parameters, {name: value}, change what the scenario builds (main.c, stage_parameter)"""
    maps = folder / "custom_maps"
    maps.mkdir(parents=True, exist_ok=True)
    words = [scenario, "void.map"]
    if parameters:
        words.append(",".join(f"{name}={value}" for name, value in sorted(parameters.items())))
    environment = {name: value for name, value in os.environ.items() if not name.startswith("HALO_")}
    result = subprocess.run([str(GAME)], cwd=maps, capture_output=True, text=True, timeout=60,
                            env=dict(environment, HALO_GUEST_IMAGE=str(program), HALO_STAGEKIT="|".join(words),
                                     HOME=str(folder)))
    if result.returncode or "stagekit: " + scenario not in result.stdout:
        raise RuntimeError(f"stagekit {' '.join(words)} failed ({result.returncode}):\n{result.stdout}{result.stderr}")
    return maps / "void.map"


def map_validator(folder):
    """the tag validator alone (tools/map_validate.c, tools/x86_game_abi.py), which builds only on 64-bit x86 Linux
    with the C library's 32-bit files; None elsewhere"""
    sys.path.insert(0, str(ROOT))
    from tools import x86_game_abi

    if x86_game_abi.can_build(folder):
        return None
    return x86_game_abi.build_map_validate(ROOT / "build/map_validate")


def validate(validator, map_path):
    """map_validate's corrections"""
    match = re.search(r": (\d+) corrections", run_tool(validator, map_path))
    return int(match.group(1))


class Session:
    """a scenario played, from its slice's start (every player in the game): each second's {player: (x, y, z)}
    (a player not in the game that second absent), the machine each player plays on, and the game's log and
    debug.txt"""

    def __init__(self, folder, start, end, seconds, machines, log, debug):
        self.folder = folder
        self.start = start
        self.end = end
        self.seconds = seconds
        self.machines = machines
        self.log = log
        self.debug = debug

    def logged(self):
        """the test's log of each second of the slice, as the game wrote it"""
        return [line for line in self.log.splitlines() if (tick := TICK.search(line)) and
                self.start <= int(tick.group(1)) <= self.end]

    def tails(self, lines=25):
        """the end of each machine's logs: what a failed test shows"""
        return "\n".join(f"{path}:\n" + "\n".join(path.read_text(errors="replace").splitlines()[-lines:])
                         for path in sorted(self.folder.glob("**/game.log")) + sorted(self.folder.glob("**/debug.txt")))

    def places(self, player=0):
        """where the player was each second it was in the game"""
        return [second[player] for second in self.seconds if player in second]

    def deaths(self, player=0):
        """how many times the player has died, by the last second logged"""
        counts = re.findall(rf"player {player}: (?:\([^)]*\)[^|]*?|dead) s-?\d+ k\d+ d(\d+)", self.log)
        return int(counts[-1]) if counts else 0


# a second of the test's log (debug.network_test): its tick, then each player's place (or dead) and machine
TICK = re.compile(r"network test: tick (\d+) (.*)")
PLAYER = re.compile(r"player (\d+): (?:\(([-\w.]+) ([-\w.]+) ([-\w.]+)\)|dead).*? m(\d+)")


def ticks(log_path):
    """each second logged: (tick, {player: (x, y, z) or None when dead}, {player: machine})"""
    lines = log_path.read_text(errors="replace").splitlines() if log_path.exists() else []
    result = []
    for line in lines:
        tick = TICK.search(line)
        if tick:
            players = PLAYER.findall(tick.group(2))
            result.append((int(tick.group(1)),
                           {int(p): (float(x), float(y), float(z)) if x else None for p, x, y, z, _ in players},
                           {int(p): int(m) for p, _, _, _, m in players}))
    return result


def addresses(slot):
    """a run's own loopback addresses (the game's and its broadcast), so that runs go side by side: Linux answers on
    all of 127/8, other systems on 127.0.0.1 alone"""
    if sys.platform.startswith("linux"):
        return f"127.10.{slot}.10", f"127.10.{slot}.11"
    return "127.0.0.1", "127.0.0.1"


def clonefile():
    """macOS's clonefile(2) (a copy on write: a file of its own, no data copied), None elsewhere"""
    if sys.platform != "darwin":
        return None
    function = ctypes.CDLL(None, use_errno=True).clonefile
    function.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32]
    return function


CLONEFILE = clonefile()


def place(source, target):
    """source's file at target, whole from the moment it is there (made beside it, then renamed over it). On Linux a
    hard link: an executable's $ORIGIN is the path it was started by (/proc/self/exe). On macOS a file of its own (a
    clone): dyld finds @executable_path (the game's rpath, where its SDL is) from the executable's vnode, which, for
    a file of many links, may name any of them, so that runs side by side would load their libraries from (and name)
    a neighbouring run's folder, where they may not be yet ("Library not loaded")"""
    temporary = target.with_name(f".{target.name}.{os.getpid()}.{threading.get_ident()}")
    temporary.unlink(missing_ok=True)
    if CLONEFILE:
        if CLONEFILE(os.fsencode(source), os.fsencode(temporary), 0):
            shutil.copy2(source, temporary)
    else:
        try:
            os.link(source, temporary)
        except OSError:
            shutil.copy2(source, temporary)
    os.replace(temporary, target)


def launch(machine, input, address, broadcast, test, init="", game=None, **environment):
    """the game (GAME, or another build's: game) in machine (a folder with its maps), with no window or sound,
    drawing nothing: debug.network_test test, the scripted input on the controller"""
    game = Path(game or GAME)
    (machine / "init.txt").write_text("debug_no_drawing 1\n" + init)
    for name in ("debug.txt", "game.log"):
        (machine / name).unlink(missing_ok=True)
    (machine / "runtime").mkdir(mode=0o700, exist_ok=True)
    # (the game, and the libraries it finds beside it, in machine's own folder: a desktop build keeps its
    # config.toml beside the executable, so each machine has its own settings)
    for source in [game, *game.parent.glob("lib*.so*"), *game.parent.glob("lib*.dylib")]:
        place(source, machine / ("halo" if source == game else source.name))
    # (none of the caller's own settings; system link on the loopback only, no internet play; a failed assertion
    # ends the run, with its place, which play_slice then shows, rather than a release build carrying on past it or
    # a debug build halting: debug.fatal_asserts)
    environment = dict({name: value for name, value in os.environ.items() if not name.startswith("HALO_")},
                       HALO_FATAL_ASSERTS="1", HALO_NET_ONLINE="false", SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen", HALO_NO_AUDIO="1",
                       HALO_NULL_RENDERER="1", HALO_NO_CLIPBOARD="1", HALO_UPDATE_AUTO="false", HALO_TEST_INPUT=input,
                       HALO_SAVE_ROOT=str(machine / "save"), XDG_RUNTIME_DIR=str(machine / "runtime"),
                       HALO_DATA_ROOT=str(machine), HOME=str(machine), HALO_NETWORK_TEST=test,
                       HALO_NET_ADDRESS=address, HALO_NET_BROADCAST=broadcast, **environment)
    with open(machine / "game.log", "w") as log:
        return subprocess.Popen([str(machine / "halo")], cwd=machine, env=environment, stdout=log,
                                stderr=subprocess.STDOUT)


# the first tick every player has a unit, from which a scenario's seconds (and cues) count (network_test.c)
SLICE = re.compile(r"network test: cues from tick (\d+)")


def cue_settings(cues, players):
    """debug.network_test_cues: the game runs each (second, console line) at its tick of the slice"""
    return dict(HALO_NETWORK_TEST_CUES="|".join(f"{second} {line}" for second, line in sorted(cues)),
                HALO_NETWORK_TEST_CUE_PLAYERS=str(players))


def play_slice(log_path, seconds, games):
    """the slice's start, once the game has played seconds of it; a game that stops first, or never gets there
    in time, fails the run with the end of its logs"""
    deadline = time.monotonic() + LOADING_SECONDS + seconds + 60
    while time.monotonic() < deadline and all(game.poll() is None for game in games):
        text = log_path.read_text(errors="replace") if log_path.exists() else ""
        start = SLICE.search(text)
        logged = ticks(log_path) if start else []
        if logged and logged[-1][0] >= int(start.group(1)) + seconds * TICKS_PER_SECOND:
            return int(start.group(1))
        time.sleep(0.05)
    stopped = [game.returncode for game in games if game.poll() is not None]
    raise RuntimeError(f"the game {'stopped ' + str(stopped) if stopped else 'ran out of time'} before {seconds} "
                       "seconds of its slice:\n" + Session(log_path.parent, 0, -1, [], {}, "", "").tails())


def session(folder, start, seconds):
    """the Session of the game logged in folder, from its slice's start, for seconds"""
    end = start + seconds * TICKS_PER_SECOND
    logged = [(places, machines) for tick, places, machines in ticks(folder / "game.log") if start <= tick <= end]
    debug = folder / "debug.txt"
    return Session(folder, start, end, [{player: place for player, place in places.items() if place is not None}
                                        for places, _ in logged], logged[-1][1] if logged else {},
                   (folder / "game.log").read_text(errors="replace"),
                   debug.read_text(errors="replace") if debug.exists() else "")


def prepare(folder):
    """folder's maps folder, as the game asks for to start with no game data at all: only the header of an Xbox
    ui.map (its check of the maps folder reads no more of it, and map_name skips the menus)"""
    (folder / "maps").mkdir(exist_ok=True)
    (folder / "maps" / "ui.map").write_bytes(struct.pack("<II", MAP_HEADER_SIGNATURE, XBOX_MAP_VERSION))


def play(folder, seconds, slot=0, cues=(), input="bot:1"):
    """the game on folder's custom_maps/void.map on one machine for seconds of game time (a Session); slot sets
    the run's addresses apart, so runs go side by side; the game runs the cues' (second, console line) at their
    ticks; input is debug.test_input's.

    It plays the map in single player with no game data at all (prepare), logged by debug.network_test "log", a
    tick a frame (debug.fixed_step): as fast as the machine goes, and the same every time."""
    prepare(folder)
    game = launch(folder, input, *addresses(slot), "log",
                  init="map_name custom_maps\\void\n", HALO_FIXED_STEP="1", **cue_settings(cues, 1))
    # (stopped here: debug.exit_after is read with the window's events, and the null renderer opens no window)
    try:
        start = play_slice(folder / "game.log", seconds, [game])
    finally:
        game.kill()
        game.wait()
    return session(folder, start, seconds)
