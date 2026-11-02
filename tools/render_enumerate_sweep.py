#!/usr/bin/env python3
"""The maps' enumerations checked in play (port/linux/game/render_enumerate.c,
port/assets/render/README.md): each map of a list is played for a while, a
game at a time on several cores, straight into the map (init.txt's map_name,
no menus) with the scripted player (debug.test_input "bot"), offscreen
(SDL's offscreen video driver: no window), recording its draws
(debug.render_manifest); then tools/render_manifest.py check reads the
recordings. A run fails on an EXCEPTION in its debug.txt or a nonzero exit,
on a number that was not one on its way to the GPU (the command stream's
encoder made it 0: xgpu_stream_encode.c's stream_finite, said in its output;
or a light the rasterizer refused, rasterizer_lights.c, in debug.txt), and
the check on any draw of play the map's enumeration does not have.

    python tools/render_enumerate_sweep.py --data <folder of maps/> [--list maps.txt] [--seconds 90] [--jobs 8]
        [--out <folder>] [--halo build/linux_x86_64/halo] [--seed 1] [--env NAME=VALUE ...]

The list holds a scenario a line (levels\\a10\\a10, levels\\test\\bloodgulch\\
bloodgulch), or "ui" for the main menu; without one, every stock map's. Each
map is run in a folder of its own under --out (its data root and save root,
debug.txt, the recording manifest.txt, the game's output stdout.txt).
"""
import argparse
import concurrent.futures
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

STOCK_MAPS = ["ui"] + [rf"levels\{name}\{name}" for name in
                       ("a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40")] + \
    [rf"levels\test\{name}\{name}" for name in
     ("beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation", "hangemhigh", "longest",
      "prisoner", "putput", "ratrace", "sidewinder", "wizard")]


def run(scenario, arguments):
    """plays one map; its folder and the problem, or None"""
    name = scenario.replace("\\", "/").rsplit("/", 1)[-1]
    folder = Path(arguments.out) / name
    subprocess.run(["rm", "-rf", str(folder)], check=True)
    (folder / "saves").mkdir(parents=True)
    (folder / "maps").symlink_to(Path(arguments.data).resolve() / "maps")
    if scenario != "ui":
        (folder / "init.txt").write_text(f"map_name {scenario}\n")
    environment = dict(os.environ, HALO_DATA_ROOT=str(folder), HALO_SAVE_ROOT=str(folder / "saves"),
                       HALO_NET_ONLINE="false", HALO_EXIT_AFTER=str(arguments.seconds),
                       HALO_RENDER_MANIFEST=str(folder / "manifest.txt"), HALO_TEST_INPUT=f"bot:{arguments.seed}",
                       SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")
    for setting in arguments.env:
        key, _, value = setting.partition("=")
        environment[key] = value
    with open(folder / "stdout.txt", "w") as output:
        try:
            status = subprocess.run([str(Path(arguments.halo).resolve())], cwd=folder, env=environment, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=arguments.seconds + 120).returncode
        except subprocess.TimeoutExpired:
            status = "a timeout"
    debug = folder / "debug.txt"
    exceptions = [line.strip() for line in debug.read_text(errors="replace").splitlines()
                  if "EXCEPTION" in line] if debug.exists() else []
    if exceptions:
        return folder, exceptions[0]
    # (a value not a number on its way to the GPU, made finite where it was caught)
    caught = [line.strip() for path in (folder / "stdout.txt", debug) if path.exists()
              for line in path.read_text(errors="replace").splitlines()
              if "drawn as 0" in line or "a light not a number" in line]
    if caught:
        return folder, caught[0]
    if status != 0:
        return folder, f"exit {status}"
    return folder, None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", required=True, help="the folder holding maps/")
    parser.add_argument("--list", help="a file of scenarios, one a line (default: every stock map)")
    parser.add_argument("--seconds", type=int, default=90)
    parser.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) // 4))
    parser.add_argument("--out", default=str(ROOT / "build" / "render_enumerate_sweep"))
    parser.add_argument("--halo", default=str(ROOT / "build" / "linux_x86_64" / "halo"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--env", action="append", default=[], help="NAME=VALUE: a setting's variable for every run")
    arguments = parser.parse_args()
    scenarios = STOCK_MAPS if not arguments.list else \
        [line.strip() for line in Path(arguments.list).read_text().splitlines() if line.strip()]

    failed = False
    folders = []
    with concurrent.futures.ThreadPoolExecutor(arguments.jobs) as pool:
        for folder, problem in pool.map(lambda scenario: run(scenario, arguments), scenarios):
            folders.append(folder)
            print(f"{'FAIL' if problem else 'ok'} {folder.name}{': ' + problem if problem else ''}", flush=True)
            failed = failed or problem is not None
    image = Path(arguments.halo).resolve().parent / "halo_guest.elf"
    check = [sys.executable, str(ROOT / "tools" / "render_manifest.py"), "check"]
    if image.exists():
        check += ["--image", str(image)]
    check += [str(folder / "manifest.txt") for folder in folders if (folder / "manifest.txt").exists()]
    failed = subprocess.run(check).returncode != 0 or failed
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
