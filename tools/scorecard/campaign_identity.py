#!/usr/bin/env python3
"""The campaign's 30 Hz gate (README.md): a campaign AI scene at 30 Hz plays as the original code did, tick for tick.

b30's beach battle from the level's start (cinematics skipped), the player standing still, a fixed seed, fast mode
(debug.fixed_step, the null renderer), in Classic (display.graphics, whose game.enhanced_animations is off), its global random seed
compared each tick with campaign_b30.txt: the seeds the original code took (40aba0cf9, the build before the
sub-steps), recorded so. The AI takes the global seed for its choices, so it parts at the first tick the AI decides
otherwise: as with #312's turn in the air, which the AI saw (parted at tick 1553 here; refs #544).

It needs the retail maps, which CI does not have: given none it skips (a note, exit 0). It runs where the maps are
(the scorecard computer), on any Linux x86-64 build, in about 15 seconds:

    python tools/scorecard/campaign_identity.py --maps <folder of b30.map ...>          (or HALO_RETAIL_MAPS)
    python tools/scorecard/campaign_identity.py --maps <folder> --record <reference>    the reference written again
    python tools/scorecard/campaign_identity.py --maps <folder> --mutation              Enhanced: must fail

Exit status: 0 when the seeds agree (or no maps), 1 when they part (the tick where), 2 when the game did not run
long enough.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
REFERENCE = HERE / "campaign_b30.txt"
LEVEL = "b30"
# the ticks compared: past #312's parting (1553), 80 seconds of the battle
TICKS = 2400
SEED = 12345


def play(game, maps, work, enhanced):
    """b30 played in work at 30 Hz until TICKS are logged, Classic (game.enhanced_animations as the preset has it,
    else as enhanced says); its global seeds, a tick each"""
    if work.exists():
        shutil.rmtree(work)
    for folder in ("maps", "runtime", "save"):
        (work / folder).mkdir(parents=True)
    (work / "runtime").chmod(0o700)
    for map_path in Path(maps).glob("*.map"):
        (work / "maps" / map_path.name).symlink_to(map_path.resolve())
    (work / "init.txt").write_text(f"map_name levels\\{LEVEL}\\{LEVEL}\n")
    environment = dict(PATH=os.environ.get("PATH", "/usr/bin:/bin"), HOME=str(work), XDG_RUNTIME_DIR=str(work / "runtime"),
                       HALO_DATA_ROOT=str(work), HALO_SAVE_ROOT=str(work / "save"), HALO_FATAL_ASSERTS="0",
                       HALO_NET_ONLINE="false", HALO_NO_AUDIO="1", SDL_AUDIODRIVER="dummy", SDL_VIDEODRIVER="offscreen",
                       HALO_NULL_RENDERER="1", HALO_NO_CLIPBOARD="1", HALO_UPDATE_AUTO="false",
                       HALO_NET_ADDRESS="127.10.83.10", HALO_NET_BROADCAST="127.10.83.11",
                       HALO_FIXED_STEP="1", HALO_RANDOM_SEED=str(SEED), HALO_SKIP_CINEMATICS="1", HALO_SIM_RATE="30",
                       HALO_GRAPHICS="classic", HALO_DETERMINISM_LOG="determinism.txt")
    # (Classic's own choice, unless asked: the original code's build, which has no preset of it, is told)
    if enhanced is not None:
        environment["HALO_ENHANCED_ANIMATIONS"] = "true" if enhanced else "false"
    log = work / "determinism.txt"
    with open(work / "game.log", "wb") as output:
        process = subprocess.Popen([str(Path(game).resolve())], cwd=work, env=environment, stdout=output,
                                   stderr=subprocess.STDOUT)
        try:
            # (fast mode plays some 300 ticks a second; stopped once TICKS are logged)
            for _ in range(240):
                try:
                    process.wait(timeout=0.5)
                    break
                except subprocess.TimeoutExpired:
                    pass
                if log.exists() and len(seeds(log)) > TICKS:
                    break
        finally:
            process.kill()
            process.wait()
    return seeds(log)[:TICKS] if log.exists() else []


def seeds(path):
    """a determinism log's global seed, a tick each ("t <tick> g <global> l <local> ...")"""
    result = []
    for line in path.read_text(errors="replace").splitlines():
        words = line.split()
        if len(words) >= 4 and words[0] == "t" and words[2] == "g":
            result.append(words[3])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--maps", default=os.environ.get("HALO_RETAIL_MAPS"), help="the retail maps' folder")
    parser.add_argument("--game", default=os.environ.get("HALO_GAME", str(ROOT / "build/linux_x86_64/halo")))
    parser.add_argument("--work", type=Path, default=ROOT / "build/scorecard/campaign")
    parser.add_argument("--record", type=Path, help="write the seeds played here, as the reference")
    parser.add_argument("--mutation", action="store_true",
                        help="play with game.enhanced_animations on (#312's turn), which the gate must catch")
    args = parser.parse_args()

    if not args.maps or not (Path(args.maps) / f"{LEVEL}.map").is_file():
        print(f"campaign identity: no retail {LEVEL}.map (--maps, HALO_RETAIL_MAPS): skipped")
        return 0
    played = play(args.game, args.maps, args.work, True if args.mutation else False if args.record else None)
    if len(played) < TICKS:
        print(f"campaign identity: the game logged {len(played)} ticks, not {TICKS} (see {args.work}/game.log)")
        return 2
    if args.record:
        args.record.write_text("\n".join(played) + "\n")
        print(f"campaign identity: {len(played)} ticks' seeds recorded in {args.record}")
        return 0
    reference = REFERENCE.read_text().split()
    parted = next((tick for tick, (a, b) in enumerate(zip(reference, played), start=1) if a != b), None)
    if parted is None:
        print(f"campaign identity: {LEVEL} at 30 Hz, {TICKS} ticks: the global seed is the original's every tick")
        return 0
    print(f"campaign identity: FAILED: {LEVEL} at 30 Hz parts from the original's global seed at tick {parted} "
          f"(reference {reference[parted - 1]}, played {played[parted - 1]}): something there changed what the "
          f"simulation (the AI) did; tools/determinism_compare.py on two runs' determinism logs finds what")
    return 1


if __name__ == "__main__":
    sys.exit(main())
