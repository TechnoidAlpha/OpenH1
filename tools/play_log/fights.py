"""How players aim and strike in their fights, from play logs (debug.play_log): people against the bots, metric by
metric, for comparing the bots' aim with a person's.

    python tools/play_log/fights.py LOG... [GROUP=LOG...] [--json OUT]

A LOG's players are grouped as people (kinds L and R) and bots (B); GROUP=LOG takes that log's bots alone, as GROUP
(a bot game at one skill: legendary=play.log), leaving out its scripted player (debug.test_input's, kind L). The
table is Markdown, a column a group (--json writes it too). The metrics (degrees, world units, milliseconds):

    reaction      an enemy into 25 degrees of the aim (from beyond 40) to the aim within 4 of it
    flick         those same turns: how often the aim passes the enemy (overshoot), by how much, the turn's
                  corrective sub-movements (its speed's peaks), and the time to settle (within 3 degrees for a
                  fifth of a second)
    tracking      an enemy crossing the view at 15 degrees a second and more, the aim on it (within 8): the aim's
                  turn as a part of the enemy's (gain) and how far behind it the aim trails, by the crossing's speed
    on target     the shots (the trigger held) whose aim passed within 0.35 units of the enemy's middle (its body,
                  as the bots' fire cone has it), by distance, by the enemy's speed and by its crossing of the view;
                  and how tight the aim is while firing (its error's percentiles, and the share within half a
                  degree: a laser)
    burst         the aim's error in the first fifth of a second of a burst against later in it
    melee         each blow (the melee press, on foot): the nearest enemy's distance, the aim's error on it, the
                  time from it coming within 2.5 units to the blow, and a whiff (its shield and health not down by
                  a tenth within a third of a second; a blow with no enemy within 3 units, a wild one)

The aim's error is taken to the enemy's middle (its origin, 0.5 units up); a person's aim on the host is as the host
had it. Halo has no recoil that moves the view, so the burst's metric is the aim's own unsteadiness."""

import argparse
import json
import math
from collections import defaultdict

FIRE = 0x800
MELEE = 0x80


def pct(values, p):
    if not values:
        return float("nan")
    values = sorted(values)
    return values[min(len(values) - 1, int(p / 100 * len(values)))]


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


def read(path):
    """the log's games: each (rate, frames: tick -> player -> state, kinds)"""
    games = []
    frames, kinds, rate = None, {}, 30
    with open(path, errors="replace") as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == "start":
                frames, kinds, rate = defaultdict(dict), {}, int(t[1])
                games.append((rate, frames, kinds))
            elif t[0] == "p" and len(t) >= 22:
                if frames is None:
                    frames, kinds = defaultdict(dict), {}
                    games.append((rate, frames, kinds))
                try:
                    idx = int(t[2])
                    ax, ay, az = float(t[11]), float(t[12]), float(t[13])
                    state = (int(t[4]), float(t[5]), float(t[6]), float(t[7]), float(t[8]), float(t[9]),
                             math.atan2(ay, ax), math.atan2(az, math.hypot(ax, ay)), int(t[16], 16),
                             float(t[18]) + float(t[19]), t[20] not in ("0", "-"), " ".join(t[21:-1]))
                except ValueError:
                    # (a line cut short: the game stopped as it wrote it)
                    continue
                kinds[idx] = t[3]
                frames[int(t[1])][idx] = state
    return games


# a state's fields
TEAM, X, Y, Z, VX, VY, YAW, PITCH, FLAGS, HEALTH, VEH, WEAPON = range(12)


def direction(s, o):
    dx, dy, dz = o[X] - s[X], o[Y] - s[Y], o[Z] + 0.5 - s[Z] - 0.6
    h = math.hypot(dx, dy)
    return math.atan2(dy, dx), math.atan2(dz, h), math.sqrt(h * h + dz * dz)


def error(s, yaw, pitch):
    return math.degrees(math.hypot(wrap(yaw - s[YAW]) * math.cos(pitch), pitch - s[PITCH]))


def family(weapon):
    w = weapon.split("\\")[-1].replace("_", " ")
    for name in ("pistol", "assault rifle", "sniper rifle", "shotgun", "rocket", "plasma", "needler"):
        if name in w:
            return name.split()[0] if name != "assault rifle" else "rifle"
    return "other"


class Stats:
    def __init__(self):
        self.v = defaultdict(list)
        self.n = defaultdict(int)


def analyse(games, groups, bot_group):
    for rate, frames, kinds in games:
        ticks = sorted(frames)
        if not ticks:
            continue
        teams = len({s[TEAM] for fr in frames.values() for s in fr.values()}) > 1
        # (a log named for a group: its bots only, not its scripted player)
        group = {i: (bot_group if k == "B" else None if bot_group != "bots" else "people") for i, k in kinds.items()}
        win = lambda seconds: max(1, int(round(seconds * rate)))

        def enemies(fr, i, s):
            for j, o in fr.items():
                if j != i and not o[VEH] and not (teams and o[TEAM] == s[TEAM]):
                    yield j, o

        acquire = {}      # (i, j) -> ("far"|"near", tick)
        flicks = []       # (group, i, j, start tick)
        burst_start = {}
        last_fire = {}
        near_since = {}   # (i, j) -> tick within 2.5
        blows = []        # (group, i, j, tick, distance, error, approach seconds)
        prev = {}
        history = {}
        for tick in ticks:
            fr = frames[tick]
            for i, s in fr.items():
                if group[i] is None:
                    continue
                g = groups[group[i]]
                if s[VEH]:
                    prev.pop(i, None)
                    continue
                best = None
                nearest = None
                for j, o in enemies(fr, i, s):
                    yaw, pitch, dist = direction(s, o)
                    if dist > 60 or dist < 0.3:
                        acquire.pop((i, j), None)
                        continue
                    err = error(s, yaw, pitch)
                    if best is None or err < best[0]:
                        best = (err, dist, j, yaw, pitch, o)
                    if nearest is None or dist < nearest[1]:
                        nearest = (j, dist, err, o)
                    # reaction and flick
                    k = (i, j)
                    if dist <= 40:
                        if err > 40:
                            acquire[k] = ("far", tick)
                        elif k in acquire and acquire[k][0] == "far" and err < 25:
                            acquire[k] = ("near", tick)
                            flicks.append((group[i], i, j, tick))
                    # melee approach
                    if dist < 2.5:
                        near_since.setdefault(k, tick)
                    else:
                        near_since.pop(k, None)
                firing = bool(s[FLAGS] & FIRE)
                if firing and i not in last_fire:
                    burst_start[i] = tick
                if firing:
                    last_fire[i] = tick
                elif i in last_fire and tick - last_fire[i] > win(0.15):
                    last_fire.pop(i, None)
                    burst_start.pop(i, None)
                if best and best[0] < 10 and firing:
                    err, dist, j, yaw, pitch, o = best
                    size = math.degrees(math.atan(0.35 / max(dist, 0.5)))
                    speed = math.hypot(o[VX], o[VY]) * rate
                    db = "<10" if dist < 10 else "10-20" if dist < 20 else "20-35" if dist < 35 else "35+"
                    sb = "still" if speed < 0.5 else "walk" if speed < 1.5 else "run"
                    on = 1 if err < size else 0
                    g.v["fire_err"].append(err)
                    g.v["on"].append(on)
                    g.v["on_d" + db].append(on)
                    g.v["on_s" + sb].append(on)
                    g.v["on_w" + family(s[WEAPON])].append(on)
                    if i in burst_start:
                        into = (tick - burst_start[i]) / rate
                        g.v["burst_early" if into < 0.2 else "burst_late"].append(err)
                # tracking: the enemy's crossing of the view against the aim's turn, over a fifth of a second
                track = history.setdefault(i, [])
                if best and best[0] < 8 and 3 < best[1] < 40:
                    track.append((tick, s[YAW], best[2], best[3]))
                else:
                    track.clear()
                while track and track[0][0] < tick - win(0.2):
                    track.pop(0)
                w = None
                if len(track) >= 2 and track[0][0] == tick - win(0.2) and all(e[2] == best[2] for e in track):
                    dt = win(0.2) / rate
                    w = math.degrees(wrap(track[-1][3] - track[0][3])) / dt
                    a = math.degrees(wrap(track[-1][1] - track[0][1])) / dt
                    if abs(w) > 15:
                        sb = "15-40" if abs(w) < 40 else "40-80" if abs(w) < 80 else "80+"
                        g.v["gain" + sb].append(a / w)
                        g.v["trail" + sb].append(math.degrees(wrap(best[3] - s[YAW])) * (1 if w > 0 else -1))
                if best and best[0] < 10 and firing:
                    err, dist = best[0], best[1]
                    db = "<10" if dist < 10 else "10-20" if dist < 20 else "20-35" if dist < 35 else "35+"
                    g.v["fire_err_d" + db].append(err)
                    if w is not None:
                        wb = "<10" if abs(w) < 10 else "10-30" if abs(w) < 30 else "30-60" if abs(w) < 60 else "60+"
                        g.v["fire_err_w" + wb].append(err)
                        g.v["on_x" + wb].append(1 if err < math.degrees(math.atan(0.35 / max(dist, 0.5))) else 0)
                prev[i] = (s, tick, best)
                # the blow
                if s[FLAGS] & MELEE and not (i in prev and prev.get(("m", i))):
                    if nearest and nearest[1] < 3:
                        j, dist, err, o = nearest
                        since = near_since.get((i, j))
                        blows.append((group[i], i, j, tick, dist, err, (tick - since) / rate if since else None))
                    else:
                        groups[group[i]].n["wild_blows"] += 1
                        groups[group[i]].n["blows"] += 1
                prev[("m", i)] = bool(s[FLAGS] & MELEE)

        # the flicks' paths
        for gname, i, j, start in flicks:
            g = groups[gname]
            path = []
            for tick in range(start, start + win(1.5)):
                fr = frames.get(tick)
                if not fr or i not in fr or j not in fr or fr[i][VEH] or fr[j][VEH]:
                    continue
                s, o = fr[i], fr[j]
                yaw, pitch, dist = direction(s, o)
                path.append((tick, math.degrees(wrap(yaw - s[YAW])), error(s, yaw, pitch), s[YAW]))
            if len(path) < win(0.5):
                continue
            reached = next((t for t, _, e, _ in path if e < 4), None)
            if reached is None:
                continue
            g.v["reaction"].append((reached - start) / rate * 1000)
            s0 = 1 if path[0][1] > 0 else -1
            # (the aim passing the enemy before it is on it for good: the turn, to 0.4 seconds after it reached)
            turn = [p for p in path if p[0] <= reached + win(0.4)]
            crossed = [t for t, e, _, _ in turn if e * s0 < 0]
            over = max([-e * s0 for t, e, _, _ in turn if crossed and t >= crossed[0]] or [0.0])
            g.v["overshoot_any"].append(1 if over > 1.0 else 0)
            if over > 1.0:
                g.v["overshoot"].append(over)
            # settle: within 3 for a fifth of a second
            hold = win(0.2)
            settle = None
            for n in range(len(path) - hold):
                if all(e < 3 for _, _, e, _ in path[n:n + hold]):
                    settle = (path[n][0] - start) / rate * 1000
                    break
            if settle is not None:
                g.v["settle"].append(settle)
            # sub-movements: the turn's speed's peaks over 30 degrees a second
            speeds = [abs(math.degrees(wrap(b[3] - a[3]))) * rate / max(1, b[0] - a[0]) for a, b in zip(path, path[1:])]
            stop = len(speeds) if settle is None else max(1, int((settle / 1000) * rate))
            peaks = sum(1 for n in range(1, min(stop, len(speeds) - 1))
                        if speeds[n] > 30 and speeds[n] >= speeds[n - 1] and speeds[n] > speeds[n + 1])
            g.v["peaks"].append(peaks)

        # the blows: a whiff when the enemy's health and shield are not down by a tenth in a third of a second
        for gname, i, j, tick, dist, err, approach in blows:
            g = groups[gname]
            g.n["blows"] += 1
            before = frames[tick][j][HEALTH]
            after = min((frames[t][j][HEALTH] for t in range(tick, tick + win(0.34)) if t in frames and j in frames[t]),
                        default=before)
            dead = any(j not in frames.get(t, {j: 0}) for t in range(tick + 1, tick + win(0.34)))
            hit = before - after >= 0.1 or dead
            g.v["blow_hit"].append(1 if hit else 0)
            g.v["blow_dist"].append(dist)
            g.v["blow_err"].append(err)
            if approach is not None:
                g.v["blow_approach"].append(approach * 1000)


def report(groups):
    rows = []

    def row(name, f):
        rows.append((name, {g: f(st) for g, st in groups.items()}))

    mean = lambda v: sum(v) / len(v) * 100 if v else float("nan")
    row("reaction ms (median)", lambda s: pct(s.v["reaction"], 50))
    row("reaction ms (p25-p75)", lambda s: f"{pct(s.v['reaction'], 25):.0f}-{pct(s.v['reaction'], 75):.0f}")
    row("flicks (n)", lambda s: len(s.v["reaction"]))
    row("flick overshoots %", lambda s: mean(s.v["overshoot_any"]))
    row("overshoot deg (median)", lambda s: pct(s.v["overshoot"], 50))
    row("sub-movements (mean)", lambda s: sum(s.v["peaks"]) / len(s.v["peaks"]) if s.v["peaks"] else float("nan"))
    row("settle ms (median)", lambda s: pct(s.v["settle"], 50))
    for b in ("15-40", "40-80", "80+"):
        row(f"track gain {b} deg/s", lambda s, b=b: pct(s.v["gain" + b], 50))
        row(f"trail deg {b} deg/s", lambda s, b=b: pct(s.v["trail" + b], 50))
    row("fire err deg p10", lambda s: pct(s.v["fire_err"], 10))
    row("fire err deg p50", lambda s: pct(s.v["fire_err"], 50))
    row("fire err deg p90", lambda s: pct(s.v["fire_err"], 90))
    row("fire within 0.5 deg %", lambda s: mean([1 if e < 0.5 else 0 for e in s.v["fire_err"]]))
    row("on target %", lambda s: mean(s.v["on"]))
    for b in ("<10", "10-20", "20-35", "35+"):
        row(f"on target % at {b}", lambda s, b=b: mean(s.v["on_d" + b]))
    for b in ("still", "walk", "run"):
        row(f"on target % enemy {b}", lambda s, b=b: mean(s.v["on_s" + b]))
    for b in ("pistol", "rifle", "sniper"):
        row(f"on target % {b}", lambda s, b=b: mean(s.v["on_w" + b]))
    for b in ("<10", "10-20", "20-35", "35+"):
        row(f"fire err deg p50 at {b}", lambda s, b=b: pct(s.v["fire_err_d" + b], 50))
    for b in ("<10", "10-30", "30-60", "60+"):
        row(f"fire err deg p50 crossing {b} deg/s", lambda s, b=b: pct(s.v["fire_err_w" + b], 50))
        row(f"on target % crossing {b} deg/s", lambda s, b=b: mean(s.v["on_x" + b]))
    row("burst err first 0.2 s", lambda s: pct(s.v["burst_early"], 50))
    row("burst err later", lambda s: pct(s.v["burst_late"], 50))
    row("blows (n)", lambda s: s.n["blows"])
    row("blow whiff % (enemy <3)", lambda s: 100 - mean(s.v["blow_hit"]))
    row("blows wild % (none <3)", lambda s: s.n["wild_blows"] / s.n["blows"] * 100 if s.n["blows"] else float("nan"))
    row("blow distance p10/p50/p90", lambda s: "/".join(f"{pct(s.v['blow_dist'], p):.2f}" for p in (10, 50, 90)))
    row("blow aim err deg (median)", lambda s: pct(s.v["blow_err"], 50))
    row("blow wind-up ms (median)", lambda s: pct(s.v["blow_approach"], 50))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", help="LOG (its people, and its bots as bots) or GROUP=LOG (its bots "
                        "alone, as GROUP)")
    parser.add_argument("--json", help="the table as JSON")
    arguments = parser.parse_args()
    groups = defaultdict(Stats)
    groups["people"]
    for spec in arguments.logs:
        name, _, path = spec.rpartition("=")
        analyse(read(path), groups, name or "bots")
    rows = report(groups)
    names = list(groups)
    fmt = lambda v: f"{v:.1f}" if isinstance(v, float) else str(v)
    print("| metric | " + " | ".join(names) + " |")
    print("| --- |" + " --- |" * len(names))
    for name, values in rows:
        print(f"| {name} | " + " | ".join(fmt(values[g]) for g in names) + " |")
    if arguments.json:
        with open(arguments.json, "w") as f:
            json.dump({name: {g: values[g] for g in names} for name, values in rows}, f, indent=1)


if __name__ == "__main__":
    main()
