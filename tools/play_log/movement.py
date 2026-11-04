"""Movement and tactics from a debug.play_log: people against bots."""
import math
import sys
from collections import defaultdict

path = sys.argv[1]
rate = 30
frames = defaultdict(dict)
kinds = {}
with open(path, errors="replace") as f:
    for line in f:
        t = line.split()
        if t and t[0] == "start":
            rate = int(t[1])
        elif t and t[0] == "p" and len(t) >= 22 and t[20] in ("0", "-"):
            idx = int(t[2])
            kinds[idx] = t[3]
            frames[int(t[1])][idx] = (int(t[4]), tuple(map(float, t[5:8])), tuple(map(float, t[8:11])),
                                     tuple(map(float, t[11:14])), int(t[16], 16), float(t[19]))
ticks = sorted(frames)
G = lambda i: "bot" if kinds[i] == "B" else "human"
S = defaultdict(lambda: defaultdict(list))
hist = defaultdict(list)   # player -> [(tick, state, nearest enemy dist, nearest mate dist, in fight)]

for tick in ticks:
    fr = frames[tick]
    for i, (team, pos, vel, aim, flags, shield) in fr.items():
        en = mate = None
        en_err = 180
        for j, (t2, p2, *_r) in fr.items():
            if j == i:
                continue
            d = math.dist(pos, p2)
            if t2 == team:
                mate = d if mate is None else min(mate, d)
            else:
                dx = [p2[k] - pos[k] for k in range(3)]
                n = math.sqrt(sum(x * x for x in dx)) or 1
                a = sum(x * y for x, y in zip(aim, dx)) / n
                err = math.degrees(math.acos(max(-1, min(1, a))))
                if d < 40 and err < en_err:
                    en_err, en = err, d
        fight = en is not None and en_err < 30
        hist[i].append((tick, flags, vel, aim, shield, en, mate, fight, pos))

for i, h in hist.items():
    g = G(i)
    s = S[g]
    lat_sign, last_flip = 0, None
    for n, (tick, flags, vel, aim, shield, en, mate, fight, pos) in enumerate(h):
        ctx = "fight" if fight else "calm"
        sp = math.hypot(vel[0], vel[1]) * rate
        ah = math.hypot(aim[0], aim[1]) or 1
        fwd = (vel[0] * aim[0] + vel[1] * aim[1]) / ah * rate
        lat = (vel[0] * -aim[1] + vel[1] * aim[0]) / ah * rate
        s[ctx + "_n"].append(1)
        s[ctx + "_still"].append(sp < 0.3)
        s[ctx + "_jump"].append(bool(flags & 2))
        s[ctx + "_crouch"].append(bool(flags & 1))
        s[ctx + "_cj"].append(bool(flags & 1) and bool(flags & 2))
        if sp > 0.5:
            s[ctx + "_strafe"].append(abs(lat) > abs(fwd))
            s[ctx + "_back"].append(fwd < -0.5 and abs(fwd) > abs(lat))
        if fight:
            if flags & 0x800:
                s["fire_still"].append(sp < 0.3)
                s["fire_crouch"].append(bool(flags & 1))
            # strafe direction changes, with hysteresis
            sg = 1 if lat > 0.8 else -1 if lat < -0.8 else 0
            if sg and lat_sign and sg != lat_sign:
                if last_flip is not None and tick - last_flip < rate * 3:
                    s["flip_gap"].append((tick - last_flip) / rate)
                last_flip = tick
            if sg:
                lat_sign = sg
        if mate is not None:
            s["mate"].append(mate)
        # after shields drop below 30%: moving away from the nearest enemy?
        if n >= 15 and shield < 0.3 and h[n - 15][4] >= 0.3 and en is not None:
            later = [x for x in h[n:n + 45] if x[5] is not None]
            if later:
                s["low_away"].append(later[-1][5] > en + 1.0)
                s["low_jump"].append(any(x[1] & 2 for x in h[n:n + 45]))
        # jumps: what surrounds them
        if flags & 2 and not (h[n - 1][1] & 2 if n else 0):
            s["jumps"].append(1)
            s["jump_in_fight"].append(fight)
            s["jump_still"].append(sp < 0.3)
            s["jump_crouch_near"].append(any(x[1] & 1 for x in h[max(0, n - 10):n + 10]))
            s["jump_gap"].append((tick - s["_lastjump_" + str(i)][-1]) / rate if s["_lastjump_" + str(i)] else None)
            s["_lastjump_" + str(i)].append(tick)

pct = lambda v, p: sorted(v)[min(len(v) - 1, int(p / 100 * len(v)))] if v else float("nan")
m = lambda v: 100 * sum(v) / len(v) if v else float("nan")
for g in ("human", "bot"):
    s = S[g]
    print(f"\n== {g}")
    for ctx in ("calm", "fight"):
        print(f"  {ctx}: {len(s[ctx + '_n'])} samples; still {m(s[ctx + '_still']):.0f}%, "
              f"strafing {m(s[ctx + '_strafe']):.0f}% / backing {m(s[ctx + '_back']):.0f}% of moving, "
              f"jump held {m(s[ctx + '_jump']):.1f}%, crouch {m(s[ctx + '_crouch']):.1f}%, crouch+jump {m(s[ctx + '_cj']):.2f}%")
    fg = s["flip_gap"]
    print(f"  strafe flips in fights: {len(fg)}, gap median {pct(fg, 50):.2f}s p25 {pct(fg, 25):.2f} p75 {pct(fg, 75):.2f}")
    print(f"  firing: standing still {m(s['fire_still']):.0f}%, crouched {m(s['fire_crouch']):.0f}%")
    j = len(s["jumps"]); mins = sum(len(hist[i]) for i in hist if G(i) == g) / rate / 60
    gaps = [x for x in s["jump_gap"] if x is not None]
    print(f"  jumps: {j} ({j / max(mins, 1e-9):.1f} per player-minute), in fights {m(s['jump_in_fight']):.0f}%, "
          f"from standing {m(s['jump_still']):.0f}%, with a crouch within 1/3 s {m(s['jump_crouch_near']):.0f}%, "
          f"gap median {pct(gaps, 50):.1f}s, under 1 s {m([x < 1 for x in gaps]):.0f}%")
    print(f"  nearest teammate: median {pct(s['mate'], 50):.1f} wu, p25 {pct(s['mate'], 25):.1f}")
    print(f"  shields broken: {len(s['low_away'])}; then backed off {m(s['low_away']):.0f}%, jumped {m(s['low_jump']):.0f}%")
