#!/usr/bin/env python3
"""The fixed benchmark: the same minute of a big game on any build, for before | after comparisons.

Blood Gulch, Slayer, 40 bots (bots.count), the enhanced graphics (display.graphics and display.dynamic_lights
"enhanced"), the network game's seed fixed (debug.random_seed) and every frame given the same game time
(debug.frame_time), so that the game's ticks are the same on every build; the player is the scripted one
(debug.test_input "bot:<seed>"), a bot of the player's own that runs the base and the field and fights, so the view
goes through firefights as a player's does. Its frames are logged (debug.frame_log: the frame's time, the GPU's, the
render thread's, the game thread's CPU) from --warmup to --warmup + --seconds of the scenario's clock, when the game
quits (debug.frame_log_seconds); the passes are timed on the GPU every --pass-times frames (debug.pass_times, Vulkan's;
HALO_METAL_PASS_TIMES, Metal's). A demo cannot do this: it plays back only on the build that recorded it.

    python tools/bench.py                                   # this computer's build (build/macos, build/linux_*)
    python tools/bench.py --game ../master/build/macos --runs 3
    python tools/bench.py --remote omarchy.fabric --game '~/openh1-bench/build/linux_x86_64' --maps '~/halo-x86-maps/maps'
    python tools/bench.py --winvm                           # the Windows VM's build (ssh bosgame.fabric winvm)
    python tools/bench.py --determinism a.txt               # the same game, its determinism log written
    python tools/determinism_compare.py --builds --game a.txt b.txt

It prints the frame times (mean, p50, p95, p99, 1% low), the GPU's time a frame and each kind of pass's, the game
thread's CPU a frame and the render thread's busy time a frame; --json writes them. --record writes a render
recording (HALO_RENDER_RECORD, with HALO_RENDER_MARKS) of the same game for the replay's per-draw cost
(docs/replay_protocol.md). The runs are hidden and muted, off the network and the clipboard, each with its own data
folder; the map cache (the save folder) and the home folder (the shaders' caches) are one per computer, kept between
runs (~/.cache/openh1-bench), so that a run does not prepare the maps again.
"""

import argparse
import json
import math
import os
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAC_MAPS = Path.home() / "dev" / "halo-macos-data" / "maps"
CACHE = Path(os.environ.get("OPENH1_BENCH_CACHE", Path.home() / ".cache" / "openh1-bench"))
WINVM = "bosgame.fabric"


def default_build() -> Path:
    if sys.platform == "darwin":
        return ROOT / "build" / "macos"
    for name in ("linux_x86_64", "linux_arm64"):
        if (ROOT / "build" / name / "halo").is_file():
            return ROOT / "build" / name
    return ROOT / "build" / "linux_x86_64"


def default_maps() -> Path:
    if sys.platform == "darwin" and (MAC_MAPS / "bloodgulch.map").is_file():
        return MAC_MAPS
    return ROOT / "assets" / "maps"


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def scenario(arguments) -> dict:
    """the benchmark's settings, the same on every platform"""
    frame_time = 1.0 / arguments.frame_rate
    settings = {
        "HALO_NETWORK_TEST": f"local:{arguments.map}:slayer", "HALO_NETWORK_TEST_START": "3",
        "HALO_NETWORK_TEST_SCORE": "1000", "HALO_BOTS": str(arguments.bots), "HALO_BOTS_MAKE_ROOM": "false",
        "HALO_TEST_INPUT": f"bot:{arguments.seed}", "HALO_RANDOM_SEED": str(arguments.seed),
        "HALO_FRAME_TIME": f"{frame_time:.9f}", "HALO_GRAPHICS": "enhanced", "HALO_DYNAMIC_LIGHTS": "enhanced",
        "HALO_DISPLAY_MODE": "windowed", "HALO_FULLSCREEN": "false", "HALO_WINDOW_SIZE": arguments.size,
        "HALO_RESOLUTION_SCALING": "native", "HALO_NO_VSYNC": "1", "HALO_MAX_FPS": "-1",
        "HALO_FRAME_LOG_SECONDS": f"{arguments.warmup + arguments.seconds:g}",
        "HALO_SKIP_CINEMATICS": "1",
    }
    if arguments.sim_rate:
        settings["HALO_SIM_RATE"] = str(arguments.sim_rate)
    if arguments.pass_times:
        settings["HALO_PASS_TIMES"] = str(arguments.pass_times)
        settings["HALO_METAL_PASS_TIMES"] = str(arguments.pass_times)
    for text in arguments.set:
        name, _, value = text.partition("=")
        settings[name] = value
    return settings


def quiet_environment(folder: Path) -> dict:
    """hidden, muted, off the network, the clipboard and the updates; this computer's one home and save folder"""
    home, saves = CACHE / "home", CACHE / "saves"
    for path in (home / "cache", home / "config", saves, folder / "data"):
        path.mkdir(parents=True, exist_ok=True)
    return {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
        "XDG_CACHE_HOME": str(home / "cache"), "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_DATA_HOME": str(home / "share"),
        "HALO_DATA_ROOT": str(folder / "data"), "HALO_SAVE_ROOT": str(saves),
        "HALO_HIDDEN_WINDOW": "1", "SDL_AUDIO_DRIVER": "dummy", "HALO_VOLUME": "0", "HALO_NO_CLIPBOARD": "1",
        "HALO_NET_ONLINE": "false", "HALO_UPDATE_AUTO": "false", "HALO_CRASH_ANSWER": "no",
        "HALO_NET_PORT": str(free_port()), "HALO_NET_ADDRESS": "127.0.0.1", "HALO_NET_BROADCAST": "127.255.255.255",
        "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_PUBLIC_LOBBY": "false",
    }


# ---------- reading a run

def percentile(sorted_values, fraction):
    if not sorted_values:
        return math.nan
    position = fraction * (len(sorted_values) - 1)
    low = int(math.floor(position))
    high = min(low + 1, len(sorted_values) - 1)
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (position - low)


def read_frame_log(text: str, start: float, end: float) -> dict:
    """debug.frame_log's frames from start to end seconds of the scenario: the summary (milliseconds)"""
    frames, gpu, render, waited, cpu = [], [], [], [], []
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) < 6:
            continue
        seconds = float(fields[0])
        if seconds < start or seconds > end + 1e-6:
            continue
        frames.append(int(fields[1]) / 1000.0)
        if int(fields[2]) >= 0:
            gpu.append(int(fields[2]) / 1000.0)
        render.append(int(fields[3]) / 1000.0)
        waited.append(int(fields[4]) / 1000.0)
        if int(fields[5]) >= 0:
            cpu.append(int(fields[5]) / 1000.0)
    if not frames:
        raise RuntimeError("the frame log has no frames of the scenario")
    ordered = sorted(frames)
    slowest = ordered[-max(1, len(ordered) // 100):]

    def mean(values):
        return sum(values) / len(values) if values else math.nan

    def spread(values):
        values = sorted(values)
        return {"mean": mean(values), "p50": percentile(values, 0.5), "p95": percentile(values, 0.95),
                "p99": percentile(values, 0.99)}

    return {
        "frames": len(frames), "seconds": sum(frames) / 1000.0,
        "frame": {**spread(frames), "low_1": mean(slowest), "low_1_fps": 1000.0 / mean(slowest),
                  "fps": 1000.0 / mean(frames)},
        "gpu": spread(gpu) if gpu else None, "render": spread(render), "waited": spread(waited),
        "game_cpu": spread(cpu) if cpu else None,
    }


VULKAN_PASSES = re.compile(r"vulkan: pass times, ([\d.]+) ms a frame of (\d+) frames(.*)")
VULKAN_PASS = re.compile(r"; (.+?) ([\d.]+) ms \(\d+%, ([\d.]+) a frame\)")
METAL_TABLE = re.compile(r"metal: pass times over (\d+) frames")
METAL_ROW = re.compile(r"metal:\s+(\S+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)(?:\s+([\d.]+))?")


def read_pass_times(log: str, timed_frames: int) -> dict:
    """the passes' GPU times from the game's log (Vulkan's lines, Metal's tables), of the last tables that cover
    timed_frames frames (the measured part: the game quits at its end): kind -> (ms a frame, passes a frame)"""
    tables = []
    lines = log.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        match = VULKAN_PASSES.search(line)
        if match:
            passes = {label: (float(ms), float(count)) for label, ms, count in VULKAN_PASS.findall(match.group(3))}
            tables.append((int(match.group(2)), float(match.group(1)), passes))
        match = METAL_TABLE.search(line)
        if match:
            count, passes, busy = int(match.group(1)), {}, math.nan
            index += 1
            while index < len(lines) and (row := METAL_ROW.search(lines[index])):
                if row.group(1) == "busy":
                    busy = float(row.group(3))
                else:
                    passes[row.group(1)] = (float(row.group(3)), float(row.group(5) or 0))
                index += 1
            if not math.isnan(busy):  # (a table the game's exit cut short is left out)
                tables.append((count, busy, passes))
            continue
        index += 1
    taken, frames = [], 0
    for table in reversed(tables):
        if frames >= timed_frames:
            break
        taken.append(table)
        frames += table[0]
    if not taken:
        return {}
    summed, total = {}, 0.0
    for count, busy, passes in taken:
        total += busy * count
        for label, (ms, per_frame) in passes.items():
            old = summed.get(label, (0.0, 0.0))
            summed[label] = (old[0] + ms * count, old[1] + per_frame * count)
    return {"timed_frames": frames, "total": total / frames,
            "passes": {label: {"ms": ms / frames, "count": count / frames}
                       for label, (ms, count) in sorted(summed.items(), key=lambda item: -item[1][0])}}


def summarize(frame_log: str, log: str, arguments) -> dict:
    result = read_frame_log(frame_log, arguments.warmup, arguments.warmup + arguments.seconds)
    if arguments.pass_times:
        result["pass_times"] = read_pass_times(log, max(1, result["frames"] // arguments.pass_times))
    device = re.search(r"vulkan: (?:device|GPU)[: ]+(.+)", log) or re.search(r"metal: (?:device|GPU)[: ]+(.+)", log)
    result["device"] = device.group(1).strip() if device else ""
    return result


# ---------- one run on this computer

def run_local(arguments, label: str) -> dict:
    build = Path(arguments.game).expanduser()
    executable = build / ("halo.exe" if os.name == "nt" else "halo") if build.is_dir() else build
    if not executable.is_file():
        raise RuntimeError(f"no {executable}")
    maps = Path(arguments.maps).expanduser() if arguments.maps else default_maps()
    if not (maps / f"{arguments.map}.map").is_file():
        raise RuntimeError(f"no {arguments.map}.map in {maps}")
    folder = Path(tempfile.mkdtemp(prefix=f"bench-{label}-"))
    process = None
    try:
        environment = quiet_environment(folder)
        (folder / "data" / "maps").symlink_to(maps.resolve())
        environment.update(scenario(arguments))
        environment["HALO_FRAME_LOG"] = str(folder / "frames.txt")
        environment["HALO_EXIT_AFTER"] = str(int(arguments.timeout))
        if arguments.determinism:
            # (in the data folder, then copied: the log is opened before an absolute path can be)
            environment["HALO_DETERMINISM_LOG"] = "determinism.txt"
        if arguments.record:
            environment["HALO_RENDER_RECORD"] = str(Path(arguments.record).resolve())
            environment["HALO_RENDER_MARKS"] = "1"
        if sys.platform.startswith("linux"):
            if not os.environ.get("SDL_VIDEO_DRIVER") and not arguments.xvfb:
                environment["SDL_VIDEO_DRIVER"] = "offscreen"
            elif os.environ.get("DISPLAY"):
                environment.update({"DISPLAY": os.environ["DISPLAY"], "SDL_VIDEO_DRIVER": "x11"})
            for name in ("LD_LIBRARY_PATH", "VK_ICD_FILENAMES", "VK_DRIVER_FILES"):
                if os.environ.get(name):
                    environment[name] = os.environ[name]
        started = time.monotonic()
        with open(folder / "halo.log", "w") as log:
            process = subprocess.Popen([str(executable.resolve())], cwd=executable.resolve().parent, env=environment,
                                       stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            try:
                process.wait(timeout=arguments.timeout + 30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        text = (folder / "halo.log").read_text(errors="replace")
        if arguments.determinism and (folder / "data" / "determinism.txt").is_file():
            shutil.copyfile(folder / "data" / "determinism.txt", arguments.determinism)
        frames = (folder / "frames.txt").read_text() if (folder / "frames.txt").is_file() else ""
        if not frames and arguments.determinism and (folder / "data" / "determinism.txt").is_file():
            # (a build without debug.frame_log: its determinism log alone, till --timeout)
            return {"determinism_only": True, "wall_seconds": time.monotonic() - started}
        if not frames:
            raise RuntimeError(f"{label}: no frame log (exit {process.returncode})\n{text[-3000:]}")
        result = summarize(frames, text, arguments)
        result["wall_seconds"] = time.monotonic() - started
        if arguments.keep:
            result["folder"] = str(folder)
        return result
    finally:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        if not arguments.keep:
            shutil.rmtree(folder, ignore_errors=True)


# ---------- a run on another computer

def run_remote(arguments, argv) -> int:
    """this script run on --remote's host (Linux) with its --game and --maps, over ssh"""
    own = {"--remote"}
    words, skip = [], False
    for word in argv:
        if skip:
            skip = False
            continue
        if word in own:
            skip = True
            continue
        if word.startswith("--remote="):
            continue
        words.append(word)
    quoted = " ".join(shlex.quote(word) if not word.startswith("~") else word for word in words)
    script = (f"folder=$(mktemp -d /tmp/bench-remote-XXXXXX) && cat > $folder/bench.py && "
              f"python3 $folder/bench.py {quoted}; status=$?; rm -rf $folder; exit $status")
    return subprocess.run(["ssh", "-o", "BatchMode=yes", arguments.remote, f"bash -c {shlex.quote(script)}"],
                          input=Path(__file__).read_bytes()).returncode


def run_winvm(arguments, label: str) -> dict:
    """one run in the Windows VM (winvm run, ~/winvm on bosgame): its build, as winvm fetch put it there"""
    settings = scenario(arguments)
    for name in ("HALO_NETWORK_TEST", "HALO_BOTS"):
        settings.pop(name)
    settings["HALO_FRAME_LOG"] = "C:\\openh1\\frames.txt"
    if arguments.record:
        settings["HALO_RENDER_RECORD"] = "C:\\openh1\\bench.record"
        settings["HALO_RENDER_MARKS"] = "1"
    if arguments.determinism:
        settings["HALO_DETERMINISM_LOG"] = "C:\\openh1\\determinism.txt"
    command = ["winvm", "ps", "Remove-Item C:\\openh1\\frames.txt -ErrorAction SilentlyContinue"]
    subprocess.run(["ssh", arguments.winvm_host] + [shlex.quote(word) for word in command], check=True,
                   stdout=subprocess.DEVNULL)
    command = ["winvm", "run", "--redirect", "--map", arguments.map, "--bots", str(arguments.bots),
               "--seconds", str(int(arguments.timeout))] + [f"{name}={value}" for name, value in settings.items()]
    started = time.monotonic()
    run = subprocess.run(["ssh", arguments.winvm_host, " ".join(shlex.quote(word) for word in command)],
                         capture_output=True, text=True)
    folder = re.search(r"^run: (\S+)", run.stdout, re.M)
    fetch = "winvm get C:/openh1/frames.txt /tmp/bench-frames.txt >/dev/null && cat /tmp/bench-frames.txt"
    frames = subprocess.run(["ssh", arguments.winvm_host, fetch], capture_output=True, text=True).stdout
    log = ""
    if folder:
        log = subprocess.run(["ssh", arguments.winvm_host, f"tr -d '\\r' < {folder.group(1)}/halo.log"],
                             capture_output=True, text=True).stdout
    if not frames.strip():
        raise RuntimeError(f"{label}: no frame log from the VM\n{run.stdout[-2000:]}{run.stderr[-2000:]}")
    for name, remote in (("determinism", "C:/openh1/determinism.txt"), ("record", "C:/openh1/bench.record")):
        local = getattr(arguments, name)
        if local:
            subprocess.run(["ssh", arguments.winvm_host, f"winvm get {remote} /tmp/bench-{name} >/dev/null && "
                            f"cat /tmp/bench-{name} && rm -f /tmp/bench-{name}"],
                           stdout=open(local, "wb"), check=True)
    result = summarize(frames, log, arguments)
    result["wall_seconds"] = time.monotonic() - started
    result["vm_run"] = folder.group(1) if folder else ""
    return result


# ---------- the report

def report(results, arguments) -> str:
    lines = []

    def row(name, values, unit="ms"):
        return f"  {name:<22}" + "".join(f"{value:>10.3f}" for value in values) + f"  {unit}"

    for index, result in enumerate(results):
        frame = result["frame"]
        lines.append(f"run {index + 1}: {result['frames']} frames ({arguments.seconds:g} s of the scenario at "
                     f"{arguments.frame_rate:g} frames a second of game time), {result['wall_seconds']:.0f} s"
                     + (f"; {result['device']}" if result.get("device") else ""))
        lines.append(f"  {'':<22}{'mean':>10}{'p50':>10}{'p95':>10}{'p99':>10}{'1% low':>10}")
        lines.append(row("frame", [frame["mean"], frame["p50"], frame["p95"], frame["p99"], frame["low_1"]]))
        lines.append(f"  {'':<22}{frame['fps']:>10.1f}{'':>30}{frame['low_1_fps']:>10.1f}  fps")
        for name, key in (("GPU", "gpu"), ("game thread CPU", "game_cpu"), ("render thread busy", "render"),
                          ("game waits", "waited")):
            if result.get(key):
                values = result[key]
                lines.append(row(name, [values["mean"], values["p50"], values["p95"], values["p99"]]))
        passes = result.get("pass_times")
        if passes and passes.get("passes"):
            lines.append(f"  GPU by pass ({passes['timed_frames']} frames timed; {passes['total']:.3f} ms a frame "
                         f"timed):")
            for label, values in list(passes["passes"].items())[:arguments.passes_shown]:
                lines.append(f"    {values['ms']:8.3f} ms  {values['count']:5.1f} a frame  {label}")
    if len(results) > 1:
        lines.append(f"over {len(results)} runs (mean; spread: (max - min) / mean):")
        for name, pick in (("frame mean", lambda r: r["frame"]["mean"]), ("frame p99", lambda r: r["frame"]["p99"]),
                           ("1% low", lambda r: r["frame"]["low_1"]),
                           ("GPU mean", lambda r: r["gpu"]["mean"] if r.get("gpu") else math.nan),
                           ("game CPU mean", lambda r: r["game_cpu"]["mean"] if r.get("game_cpu") else math.nan),
                           ("render busy mean", lambda r: r["render"]["mean"])):
            values = [pick(result) for result in results]
            if any(math.isnan(value) for value in values):
                continue
            mean = sum(values) / len(values)
            lines.append(f"  {name:<22}{mean:>10.3f} ms  ±{100 * (max(values) - min(values)) / mean / 2:5.1f}% "
                         f"({', '.join(f'{value:.3f}' for value in values)})")
    return "\n".join(lines)


def make_parser():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                     formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False)
    parser.add_argument("--game", default=str(default_build()), help="the build's folder or its executable")
    parser.add_argument("--maps", help="the maps folder (default: assets/maps; the Mac's ~/dev/halo-macos-data/maps)")
    parser.add_argument("--map", default="bloodgulch")
    parser.add_argument("--bots", type=int, default=40)
    parser.add_argument("--seconds", type=float, default=60.0, help="the scenario's seconds measured")
    parser.add_argument("--warmup", type=float, default=5.0, help="the scenario's seconds before them, not measured")
    parser.add_argument("--seed", type=int, default=1, help="the game's seed and the scripted player's")
    parser.add_argument("--frame-rate", type=float, default=60.0, help="frames a second of game time (debug.frame_time)")
    parser.add_argument("--sim-rate", type=int, default=0, help="game.sim_rate (default: the build's)")
    parser.add_argument("--size", default="1280x720", help="the window's size")
    parser.add_argument("--pass-times", type=int, default=10, help="time the passes every this many frames (0 never)")
    parser.add_argument("--passes-shown", type=int, default=12)
    parser.add_argument("--runs", type=int, default=1, help="run it this many times (the spread)")
    parser.add_argument("--set", action="append", default=[], metavar="VARIABLE=VALUE", help="another setting")
    parser.add_argument("--determinism", metavar="FILE", help="write the run's determinism log there")
    parser.add_argument("--record", metavar="FILE", help="write a render recording (with marks) there")
    parser.add_argument("--timeout", type=float, default=600.0, help="seconds a run may take at most")
    parser.add_argument("--xvfb", action="store_true", help="Linux: the run's DISPLAY's X server, not offscreen")
    parser.add_argument("--json", metavar="FILE", help="write the results there")
    parser.add_argument("--keep", action="store_true", help="keep each run's folder (its logs)")
    parser.add_argument("--remote", metavar="HOST", help="run on that Linux host over ssh (--game, --maps its paths)")
    parser.add_argument("--winvm", action="store_true", help="run in the Windows VM (winvm, on --winvm-host)")
    parser.add_argument("--winvm-host", default=WINVM)
    return parser


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    parser = make_parser()
    arguments = parser.parse_args(argv)
    if arguments.remote:
        return run_remote(arguments, argv)
    results = []
    for index in range(arguments.runs):
        label = f"run{index + 1}"
        result = run_winvm(arguments, label) if arguments.winvm else run_local(arguments, label)
        if result.get("determinism_only"):
            print(f"bench: {arguments.determinism} written (the build has no debug.frame_log)", file=sys.stderr)
            return 0
        results.append(result)
        print(f"bench: run {index + 1} of {arguments.runs}: {result['frame']['mean']:.3f} ms a frame", file=sys.stderr)
    print(report(results, arguments))
    if arguments.json:
        Path(arguments.json).write_text(json.dumps({"settings": scenario(arguments), "runs": results}, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
