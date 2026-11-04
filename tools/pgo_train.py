#!/usr/bin/env python3
"""Record the profile of profile-guided optimisation, by playing an instrumented guest build.

The guest builds share one profile, pgo/halo_linux.profdata. A build
configured with --pgo=generate (configure.py) compiles the guest with
clang's IR instrumentation, and its profile runtime
(port/android/guest/runtime/guest_profile.c) writes the counters at exit to
the file HALO_PGO_FILE names. This plays such a build through a few short
games and merges what they recorded into the profile:

- system link games on several multiplayer maps: a host with bots and one
  joined client on this computer, both drawing (in hidden windows) and both
  scripted players (debug.network_test, debug.test_input), with hits, kills
  and a vehicle, so that the renderer, its command stream, the netcode, the
  bots and the game's simulation are all in it;
- the start of a campaign level (its cutscene, then its AI).

Every run is silent and hidden, keeps off the clipboard, and has its own
data root (its maps folder linked to the game data's) in the work folder.
It takes about five minutes.

Usage: pgo_train.py --binary build/macos/halo --data <game data> [--output pgo/halo_linux.profdata]

The host and its client need two addresses: on Linux two loopback ones; a
Mac's loopback has 127.0.0.1 alone, so there the host takes the computer's
network address (--host-address, --client-address).
"""

import argparse
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional

# the system link games: (map, game type); each the host's and its client's
GAMES = [("bloodgulch", "ctf"), ("chillout", "team_slayer"), ("damnation", "slayer")]
# the campaign level, played from its start
CAMPAIGN = "b30"
# seconds of each game after it starts, and of the campaign level
GAME_SECONDS = 50
CAMPAIGN_SECONDS = 75
# seconds after hosting that a game starts (the client has joined by then)
START_SECONDS = 10
# seconds a run may take beyond its own time to load and quit
MARGIN = 90
# what guest_profile.c logs once it has written the profile, and the seconds
# its process then has to end
PROFILE_WRITTEN = "profile: wrote "
LINGER_SECONDS = 10


def network_address() -> str:
    """this computer's address on its network (a UDP socket's own address
    once it has a route: nothing is sent)"""
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect(("192.0.2.1", 9))
        return probe.getsockname()[0]


def data_root(maps: Path, root: Path) -> Path:
    """a data root of its own (its debug.txt, init.txt and saves), its maps
    linked to the game data's"""
    shutil.rmtree(root, ignore_errors=True)
    (root / "data").mkdir(parents=True)
    (root / "save").mkdir()
    (root / "data" / "maps").symlink_to(maps.resolve())
    return root


def environment(root: Path, raw: Path, name: str, seconds: int, settings: Dict[str, str]) -> Dict[str, str]:
    result = {key: value for key, value in os.environ.items() if not key.startswith("HALO_")}
    result.update({
        "HALO_DATA_ROOT": str((root / "data").resolve()),
        "HALO_SAVE_ROOT": str((root / "save").resolve()),
        "HALO_EXIT_AFTER": str(seconds),
        "HALO_HIDDEN_WINDOW": "1",
        "HALO_FULLSCREEN": "0",
        "HALO_NO_VSYNC": "1",
        "HALO_NO_CLIPBOARD": "1",
        "HALO_UPDATE_AUTO": "false",
        # (no internet play, nor its registering of invite links)
        "HALO_NET_ONLINE": "false",
        "HALO_NET_PUBLIC_LOBBY": "false",
        "HALO_DISCORD_APPLICATION": "",
        "SDL_AUDIO_DRIVER": "dummy",
        "HALO_PGO_FILE": str((raw / f"{name}-%p.profraw").resolve()),
    })
    result.update(settings)
    return result


def run(binary: Path, runs: List[Dict[str, str]], logs: List[Path], timeout: int) -> List[Optional[int]]:
    """the runs started together (a second apart: the host first); their
    exit statuses, None for one that did not write its profile in time.
    A run that wrote its profile but has not ended a few seconds on is
    stopped, and counts as 0: its game is over (a macOS host has been seen
    to linger after the game's exit)."""
    processes = []
    try:
        for settings, log in zip(runs, logs):
            if processes:
                time.sleep(1.0)
            with open(log, "w") as output:
                processes.append(subprocess.Popen([str(binary)], env=settings, stdout=output,
                                                  stderr=subprocess.STDOUT, start_new_session=True))
        deadline = time.monotonic() + timeout
        statuses: List[Optional[int]] = [None] * len(processes)
        written: List[Optional[float]] = [None] * len(processes)
        while time.monotonic() < deadline and None in statuses:
            time.sleep(0.5)
            for index, (process, log) in enumerate(zip(processes, logs)):
                if statuses[index] is not None:
                    continue
                if process.poll() is not None:
                    statuses[index] = process.returncode
                elif written[index] is None:
                    if PROFILE_WRITTEN in log.read_text(errors="replace"):
                        written[index] = time.monotonic()
                elif time.monotonic() - written[index] > LINGER_SECONDS:
                    print(f"pgo: {log.name}: the game wrote its profile but did not end: stopped", flush=True)
                    process.kill()
                    process.wait()
                    statuses[index] = 0
        return statuses
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", type=Path, required=True,
                        help="the instrumented build's executable (configure.py --pgo=generate)")
    parser.add_argument("--data", type=Path, default=Path("assets"), help="the game data (its maps folder)")
    parser.add_argument("--output", type=Path, default=Path("pgo/halo_linux.profdata"))
    parser.add_argument("--work", type=Path, default=Path("build/pgo_train"), help="the runs' folder")
    parser.add_argument("--llvm-profdata", help="(default: the guests' pinned LLVM's, tools/guest_toolchain.py)")
    darwin = sys.platform == "darwin"
    parser.add_argument("--host-address", default=None,
                        help="the host's address (default: 127.0.0.210; on a Mac its network address)")
    parser.add_argument("--client-address", default=None,
                        help="the client's address (default: 127.0.0.211; on a Mac 127.0.0.1)")
    parser.add_argument("--port", type=int, default=5370,
                        help="the games' network.port, off the default's, so that the runs keep off another game's")
    args = parser.parse_args()

    maps = args.data / "maps"
    needed = [name for name, _ in GAMES] + [CAMPAIGN]
    missing = [name for name in needed if not (maps / f"{name}.map").is_file()]
    if missing:
        sys.exit(f"pgo: {', '.join(missing)} not in {maps}")
    if not args.binary.is_file():
        sys.exit(f"pgo: no {args.binary}: build it after configure.py --pgo=generate")
    if not darwin and not os.environ.get("WAYLAND_DISPLAY") and not os.environ.get("DISPLAY"):
        sys.exit("pgo: the runs draw, and need a display (a virtual one: Xvfb, with SDL_VIDEO_DRIVER=x11)")
    profdata = args.llvm_profdata
    if not profdata:
        toolchain = subprocess.run([sys.executable, "tools/guest_toolchain.py"], capture_output=True, text=True,
                                   check=True).stdout.strip().splitlines()[-1]
        profdata = str(Path(toolchain) / "llvm-profdata")
    host_address = args.host_address or (network_address() if darwin else "127.0.0.210")
    client_address = args.client_address or ("127.0.0.1" if darwin else "127.0.0.211")

    work = args.work
    raw = work / "raw"
    shutil.rmtree(raw, ignore_errors=True)
    raw.mkdir(parents=True)
    host_root = data_root(maps, work / "host")
    client_root = data_root(maps, work / "client")

    network = {"HALO_NET_PORT": str(args.port), "HALO_NET_MAP_SHARE": "false", "HALO_NET_MAP_DOWNLOAD": "false",
               "HALO_NETWORK_TEST_START": str(START_SECONDS)}
    for map_name, variant in GAMES:
        seconds = START_SECONDS + GAME_SECONDS
        host = environment(host_root, raw, f"{map_name}-host", seconds, {
            **network, "HALO_NET_ADDRESS": host_address, "HALO_NET_BROADCAST": client_address,
            "HALO_NETWORK_TEST": f"host:{map_name}:{variant}", "HALO_TEST_INPUT": "bot:1", "HALO_BOTS": "4",
            "HALO_NETWORK_TEST_SHOOT": "4", "HALO_NETWORK_TEST_KILL": "17", "HALO_NETWORK_TEST_VEHICLE": "20",
        })
        client = environment(client_root, raw, f"{map_name}-client", seconds, {
            **network, "HALO_NET_ADDRESS": client_address, "HALO_NET_BROADCAST": host_address,
            "HALO_NETWORK_TEST": "join", "HALO_TEST_INPUT": "bot:2",
        })
        print(f"pgo: {map_name} ({variant}), a host with bots and a client, {GAME_SECONDS} s", flush=True)
        statuses = run(args.binary, [host, client], [work / f"{map_name}-host.log", work / f"{map_name}-client.log"],
                       seconds + MARGIN)
        if statuses != [0, 0]:
            sys.exit(f"pgo: {map_name}: the runs ended {statuses} (logs in {work})")

    (host_root / "data" / "init.txt").write_text(f"map_name levels\\{CAMPAIGN}\\{CAMPAIGN}\n",
                                                 encoding="ascii")
    print(f"pgo: {CAMPAIGN}, {CAMPAIGN_SECONDS} s", flush=True)
    statuses = run(args.binary, [environment(host_root, raw, CAMPAIGN, CAMPAIGN_SECONDS, {})],
                   [work / f"{CAMPAIGN}.log"], CAMPAIGN_SECONDS + MARGIN)
    (host_root / "data" / "init.txt").unlink()
    if statuses != [0]:
        sys.exit(f"pgo: {CAMPAIGN}: the run ended {statuses} (log in {work})")

    profiles = sorted(str(path) for path in raw.glob("*.profraw"))
    if len(profiles) != 2 * len(GAMES) + 1:
        sys.exit(f"pgo: {len(profiles)} profiles written, not {2 * len(GAMES) + 1}: was the build configured "
                 "with --pgo=generate?")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([profdata, "merge", "-o", str(args.output), *profiles], check=True)
    print(f"pgo: {args.output} from {len(profiles)} runs")


if __name__ == "__main__":
    main()
