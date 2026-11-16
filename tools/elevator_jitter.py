#!/usr/bin/env python3
"""How smoothly an elevator and its riders are drawn: reads the frames a
machine logs with debug.network_test_elevator (port/linux/game/network_test.c:
"network test: elevator frame ...") and prints, for the elevator's platform,
each player and the machine's own player's view, while the elevator moves with
its riders on it:

- jitter: how far each frame's height is off the line between the frames
  either side of it (world units; a smooth path, however fast, is near 0, a
  frame drawn a step out of line is that step), its root mean square and
  largest;
- each rider's height above the platform as drawn: how far it ranges, and the
  largest change from one frame to the next (a rider carried smoothly keeps
  it).

python tools/elevator_jitter.py <debug.txt or the game's output> [...]
"""
import math
import re
import sys

FRAME = re.compile(r"network test: elevator frame (\d+) tick (\d+) f([\d.]+) pos ([-\d.]+) v([-\d.]+) drawn ([-\d.]+) "
                   r"tick ([-\d.]+)(.*)")
PLAYER = re.compile(r"\| p(\d+) drawn ([-\d.]+) tick ([-\d.]+)( on)?(?: \[[^]]*\])?(?: view ([-\d.]+))?")


def frames(path):
    with open(path, errors="replace") as log:
        for line in log:
            match = FRAME.search(line)
            if not match:
                continue
            players = {}
            for player in PLAYER.finditer(match.group(8)):
                players[int(player.group(1))] = (float(player.group(2)), player.group(4) is not None,
                                                 float(player.group(5)) if player.group(5) else None)
            yield {"time": int(match.group(1)) / 1e6, "tick": int(match.group(2)), "velocity": float(match.group(5)),
                   "drawn": float(match.group(6)), "players": players}


def jitter(series):
    """(time, value) pairs: each value's distance from the line through its neighbours"""
    out = []
    for (t0, z0), (t1, z1), (t2, z2) in zip(series, series[1:], series[2:]):
        if t2 <= t0:
            continue
        out.append(abs(z1 - (z0 + (z2 - z0) * (t1 - t0) / (t2 - t0))))
    return out


def summary(values):
    if not values:
        return "-"
    rms = math.sqrt(sum(value * value for value in values) / len(values))
    return f"rms {rms:.4f} max {max(values):.4f}"


def report(path):
    moving = [frame for frame in frames(path) if frame["velocity"] != 0.0]
    # (riding: every player on it; the frames since the elevator started, its riders settled)
    riding = [frame for frame in moving if frame["players"] and all(on for _, on, _ in frame["players"].values())]
    print(f"{path}: {len(riding)} frames riding of {len(moving)} moving")
    if len(riding) < 3:
        return
    # (runs of consecutive frames: a stop between two rides is a gap)
    runs, run = [], [riding[0]]
    for frame in riding[1:]:
        if frame["time"] - run[-1]["time"] > 0.1:
            runs.append(run)
            run = []
        run.append(frame)
    runs.append(run)
    seconds = sum(run[-1]["time"] - run[0]["time"] for run in runs)
    print(f"  {len(runs)} rides, {seconds:.1f} s, {len(riding) / max(seconds, 1e-6):.0f} frames a second")
    elevator = [value for run in runs for value in jitter([(frame["time"], frame["drawn"]) for frame in run])]
    print(f"  elevator jitter {summary(elevator)}")
    for index in sorted({index for frame in riding for index in frame["players"]}):
        drawn, view, above, steps = [], [], [], []
        for run in runs:
            series = [(frame["time"], frame["players"][index][0]) for frame in run if index in frame["players"]]
            drawn += jitter(series)
            views = [(frame["time"], frame["players"][index][2]) for frame in run
                     if index in frame["players"] and frame["players"][index][2] is not None]
            view += jitter(views)
            heights = [frame["players"][index][0] - frame["drawn"] for frame in run if index in frame["players"]]
            above += heights
            steps += [abs(b - a) for a, b in zip(heights, heights[1:])]
        line = f"  player {index}: jitter {summary(drawn)}"
        if view:
            line += f" | own view jitter {summary(view)}"
        line += f" | above the platform {min(above):.3f} to {max(above):.3f} (range {max(above) - min(above):.3f}," \
                f" largest step {max(steps):.4f})"
        print(line)


if __name__ == "__main__":
    for argument in sys.argv[1:]:
        report(argument)
