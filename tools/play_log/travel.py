"""Teleporters and vehicles in a debug.play_log: people against bots."""
import math, sys
from collections import defaultdict
last = {}; kinds = {}; tele = defaultdict(list); veh = defaultdict(lambda: [0, 0]); enter = defaultdict(int); dead = set()
pos_tele = defaultdict(int)
for line in open(sys.argv[1], errors="replace"):
    t = line.split()
    if not t: continue
    if t[0] == "death": dead.add(int(t[2])); last.pop(int(t[2]), None); continue
    if t[0] != "p" or len(t) < 22: continue
    tick, i = int(t[1]), int(t[2]); kinds[i] = t[3]; g = "bot" if t[3] == "B" else "human"
    inv = t[20] not in ("0", "-"); p = tuple(map(float, t[5:8]))
    veh[g][inv] += 1
    if i in last:
        lt, lp, linv = last[i]
        if inv and not linv: enter[g] += 1
        if not inv and not linv and tick - lt <= 2 and math.dist(p, lp) > 4:
            tele[g].append(tick); pos_tele[(round(lp[0] / 3) * 3, round(lp[1] / 3) * 3)] += 1
    last[i] = (tick, p, inv)
players = defaultdict(set)
for i, k in kinds.items(): players["bot" if k == "B" else "human"].add(i)
for g in ("human", "bot"):
    mins = (veh[g][0] + veh[g][1]) / 30 / 60
    print(f"{g}: teleports {len(tele[g])} ({len(tele[g]) / max(mins, 1e-9):.2f} per player-minute), vehicle entries {enter[g]} ({enter[g] / max(mins, 1e-9):.2f}/player-min), time in vehicles {100 * veh[g][1] / max(1, sum(veh[g])):.0f}%")
print("busiest teleporter entrances:", sorted(pos_tele.items(), key=lambda x: -x[1])[:4])
