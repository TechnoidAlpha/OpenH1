"""Summarise a debug.play_log: how people play against how the bots do."""
import math
import sys
from collections import defaultdict

path = sys.argv[1]
rate = 30
frames = defaultdict(dict)       # tick -> player -> state
kinds, names = {}, {}
deaths = defaultdict(int)
spawns = defaultdict(int)

with open(path, errors="replace") as f:
    for line in f:
        t = line.split()
        if not t:
            continue
        if t[0] == "start":
            rate = int(t[1])
        elif t[0] in ("spawn", "death"):
            (spawns if t[0] == "spawn" else deaths)[int(t[2])] += 1
        elif t[0] == "p" and len(t) >= 22:
            tick, idx = int(t[1]), int(t[2])
            kinds[idx] = t[3]
            names[idx] = t[-1]
            frames[tick][idx] = dict(
                team=int(t[4]), pos=tuple(map(float, t[5:8])), vel=tuple(map(float, t[8:11])),
                aim=tuple(map(float, t[11:14])), thr=(float(t[14]), float(t[15])),
                flags=int(t[16], 16), body=float(t[18]), shield=float(t[19]),
                veh=t[20] not in ("0", "-"), weapon=" ".join(t[21:-1]))

ticks = sorted(frames)
teams = len({s["team"] for fr in frames.values() for s in fr.values()}) > 1


def ang(a, b):
    d = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a)) or 1
    nb = math.sqrt(sum(x * x for x in b)) or 1
    return math.degrees(math.acos(max(-1, min(1, d / na / nb))))


def pct(v, p):
    if not v:
        return float("nan")
    v = sorted(v)
    return v[min(len(v) - 1, int(p / 100 * len(v)))]


stats = defaultdict(lambda: defaultdict(list))
prev = {}
for tick in ticks:
    fr = frames[tick]
    for i, s in fr.items():
        g = "bot" if kinds[i] == "B" else "human"
        st = stats[g]
        if s["veh"]:
            st["veh"].append(1)
            prev.pop(i, None)
            continue
        st["veh"].append(0)
        speed = math.hypot(s["vel"][0], s["vel"][1]) * rate
        st["speed"].append(speed)
        st["crouch"].append(s["flags"] & 1)
        st["jump"].append(1 if s["flags"] & 2 else 0)
        st["fire"].append(1 if s["flags"] & 0x800 else 0)
        st["nade"].append(1 if s["flags"] & 0x2000 else 0)
        # the nearest enemy and the aim's error to it
        best = None
        for j, o in fr.items():
            if j == i or o["veh"] or (teams and o["team"] == s["team"]):
                continue
            d = [o["pos"][k] - s["pos"][k] for k in range(3)]
            dist = math.sqrt(sum(x * x for x in d))
            if dist < 0.5 or dist > 60:
                continue
            err = ang(s["aim"], (d[0], d[1], d[2] + 0.5))
            if best is None or err < best[0]:
                best = (err, dist, j)
        if i in prev:
            p, ptick = prev[i]
            dt = (tick - ptick) / rate
            if 0 < dt <= 0.1:
                st["turn"].append(ang(p["aim"], s["aim"]) / dt)
                # lateral direction reversals (strafes): sign change of velocity across the aim
                side = lambda q: q["vel"][0] * -q["aim"][1] + q["vel"][1] * q["aim"][0]
                a, b = side(p), side(s)
                if abs(a) * rate > 0.5 and abs(b) * rate > 0.5:
                    st["strafe_flip"].append(1 if a * b < 0 else 0)
        if best and best[0] < 10:
            st["engaged_err"].append(best[0])
            st["engaged_dist"].append(best[1])
            if s["flags"] & 0x800:
                st["fire_err"].append(best[0])
                st["fire_dist"].append(best[1])
        prev[i] = (s, tick)

# reaction: an enemy entering 25 degrees of the aim (from beyond 40) to within 4
react = defaultdict(list)
state = {}
for tick in ticks:
    fr = frames[tick]
    for i, s in fr.items():
        if s["veh"]:
            continue
        for j, o in fr.items():
            if j == i or o["veh"] or (teams and o["team"] == s["team"]):
                continue
            d = [o["pos"][k] - s["pos"][k] for k in range(3)]
            dist = math.sqrt(sum(x * x for x in d))
            if dist > 40:
                state.pop((i, j), None)
                continue
            err = ang(s["aim"], (d[0], d[1], d[2] + 0.5))
            k = (i, j)
            if err > 40:
                state[k] = ("far", tick)
            elif k in state and state[k][0] == "far" and err < 25:
                state[k] = ("near", tick)
            elif k in state and state[k][0] == "near":
                if err < 4:
                    react["bot" if kinds[i] == "B" else "human"].append((tick - state[k][1]) / rate * 1000)
                    state[k] = ("done", tick)
                elif tick - state[k][1] > rate * 2:
                    state[k] = ("done", tick)

secs = (ticks[-1] - ticks[0]) / rate if ticks else 0
print(f"{path}: {secs / 60:.1f} min, {len(kinds)} players "
      f"({sum(1 for k in kinds.values() if k != 'B')} people, {sum(1 for k in kinds.values() if k == 'B')} bots), "
      f"{rate} ticks/s, teams {teams}")
for g in ("human", "bot"):
    st = stats[g]
    n = len(st["speed"]) or 1
    mean = lambda k: sum(st[k]) / max(1, len(st[k]))
    print(f"\n== {g}")
    print(f"  on foot samples {len(st['speed'])}, in a vehicle {100 * mean('veh'):.0f}%")
    print(f"  speed wu/s: median {pct(st['speed'], 50):.2f}, p90 {pct(st['speed'], 90):.2f}; "
          f"standing still {100 * sum(1 for v in st['speed'] if v < 0.3) / n:.0f}%")
    print(f"  crouching {100 * mean('crouch'):.1f}%, jump presses {100 * mean('jump'):.2f}% of ticks, "
          f"firing {100 * mean('fire'):.1f}%, grenade {100 * mean('nade'):.2f}%")
    print(f"  turn deg/s: median {pct(st['turn'], 50):.0f}, p90 {pct(st['turn'], 90):.0f}, p99 {pct(st['turn'], 99):.0f}")
    print(f"  strafe reversals: {100 * mean('strafe_flip'):.2f}% of moving ticks "
          f"({mean('strafe_flip') * rate:.2f}/s while strafing)")
    print(f"  aim error on the nearest enemy (<10 deg): median {pct(st['engaged_err'], 50):.1f}, "
          f"while firing median {pct(st['fire_err'], 50):.1f} p90 {pct(st['fire_err'], 90):.1f}")
    print(f"  firing distance: median {pct(st['fire_dist'], 50):.1f}, p10 {pct(st['fire_dist'], 10):.1f}, "
          f"p90 {pct(st['fire_dist'], 90):.1f}")
    r = react[g]
    print(f"  reaction (enemy into 25 deg -> aim within 4): n {len(r)}, median {pct(r, 50):.0f} ms, "
          f"p25 {pct(r, 25):.0f}, p75 {pct(r, 75):.0f}")
print("\nweapons (human samples):")
w = defaultdict(int)
for fr in frames.values():
    for i, s in fr.items():
        if kinds[i] != "B":
            w[s["weapon"].split("\\")[-1]] += 1
for k, v in sorted(w.items(), key=lambda x: -x[1])[:8]:
    print(f"  {k}: {100 * v / sum(w.values()):.0f}%")
print("\ndeaths by player:", ", ".join(f"{names.get(i, i)}({kinds.get(i)}) {d}" for i, d in sorted(deaths.items(), key=lambda x: -x[1])[:12]))
