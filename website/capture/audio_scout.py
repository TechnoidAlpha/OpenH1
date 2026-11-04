"""Scout local games with bots for the site's 3D audio clip: the first player
played by a bot of its own on foot (debug.bot_player), run without saving a
frame, and the windows where the most is heard around it found from the sim
trace: a driven vehicle passing close (its distance falling, then rising: the
Doppler's turn), one in the air above the player (a Banshee overhead), one
close behind, and enemy rounds passing within a world unit of the player's
eye. A window with the player dead, driving or respawned is no good (the
camera leaves the body or jumps).

usage: audio_scout.py <map:variant,...> [--seeds 7,8] [--bots 15] [--seconds 180] [--out audio-scout]

The matches play the same again, so audio_clip.py films a window by running
its match again with the frames saved only in it, once for each audio.spatial."""
import argparse, json, math, os, subprocess, sys, zlib
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = os.environ.get('RUNS', os.path.expanduser('~/halo-capture-runs'))
sys.path.insert(0, HERE)
from scout import match_env

def run_name(map_variant, seed):
    return f"{map_variant.split(':')[0].split(chr(92))[-1]}-{zlib.crc32(map_variant.encode()) % 10000:04d}-{seed}"

def read_trace(path):
    """each tick's listener (the player's eye and aim, alive or not, driving or
    not), driven vehicles and enemy projectiles"""
    ticks, owners, fire = {}, {}, []
    now = 0.0
    for line in open(path):
        f = line.split()
        if not f:
            continue
        if f[0] == 's':
            now = float(f[1])
            ticks.setdefault(round(now * 30), {'v': {}, 'o': {}})
        elif f[0] == 'p' and f[1] == '0':
            t = ticks.setdefault(round(now * 30), {'v': {}, 'o': {}})
            t['p'] = (float(f[2]), float(f[3]), float(f[15]), float(f[8]), float(f[9]), float(f[12]))
        elif f[0] == 'v':
            t = ticks.setdefault(round(now * 30), {'v': {}, 'o': {}})
            t['v'][f[2]] = (int(f[1]), tuple(float(x) for x in f[3:9]), int(f[18]))
        elif f[0] == 'o':
            t = ticks.setdefault(round(now * 30), {'v': {}, 'o': {}})
            t['o'][f[1]] = tuple(float(x) for x in f[2:8])
        elif f[:2] == ['e', 'new']:
            owners[f[2]] = int(f[-1])
        elif f[:3] == ['e', 'fire', '0']:
            fire.append(now)
    return ticks, owners, fire

def features(ticks, owners, deathless=False):
    """per tick: the listener's state and what is heard around it"""
    out = {}
    last_d = {}
    closest = {}
    prev_pos = None
    for k in sorted(ticks):
        t = ticks[k]
        p = t.get('p')
        row = {'alive': bool(p and (p[5] > 0 or deathless) and p[2] > -50), 'driving': False, 'jump': False,
               'passes': 0.0, 'over': 0, 'behind': 0, 'whiz': 0}
        out[k] = row
        if not p:
            prev_pos = None
            continue
        x, y, z, ax, ay = p[0], p[1], p[2], p[3], p[4]
        if prev_pos and math.dist(prev_pos, (x, y)) > 1.5:
            row['jump'] = True
        prev_pos = (x, y)
        yaw = math.atan2(ay, ax)
        for vid, (rider, (vx, vy, vz, wx, wy, wz), ground) in t['v'].items():
            if rider == 0:
                row['driving'] = True
                continue
            if rider < 0:
                continue
            dx, dy, dz = vx - x, vy - y, vz - z
            d = math.sqrt(dx * dx + dy * dy + dz * dz)
            speed = math.sqrt(wx * wx + wy * wy + wz * wz)
            radial = (dx * wx + dy * wy + dz * wz) / max(d, 1e-3)
            # (the Doppler's turn: closing, then opening, close and fast)
            before = last_d.get(vid)
            if before is not None and before < 0 <= radial and d < 6 and speed > 2.5:
                row['passes'] += min(3.0, speed / max(d, 1.0))
            last_d[vid] = radial
            h = math.hypot(dx, dy)
            if not ground and dz > 1.0 and h < 6:
                row['over'] += 1
            az = math.atan2(dy, dx) - yaw
            az = (az + math.pi) % (2 * math.pi) - math.pi
            if d < 8 and abs(az) > math.radians(115):
                row['behind'] += 1
        for oid, (ox, oy, oz, ux, uy, uz) in t['o'].items():
            if owners.get(oid, -1) == 0:
                continue
            # (the nearest it came in the step after: a round moves metres a step)
            rx, ry, rz = x - ox, y - oy, z - oz
            uu = ux * ux + uy * uy + uz * uz
            s = min(1 / 30, max(0.0, (rx * ux + ry * uy + rz * uz) / uu)) if uu > 0 else 0.0
            d = math.dist((ox + ux * s, oy + uy * s, oz + uz * s), (x, y, z))
            c = closest.get(oid)
            if c is None or d < c[0]:
                closest[oid] = (d, k)
    for oid, (d, k) in closest.items():
        if d < 1.0 and k in out:
            out[k]['whiz'] += 1
    return out

def windows(rows, fire, seconds, length):
    """windows of the match, length seconds, a half second apart, scored"""
    found = []
    keys = sorted(rows)
    if not keys:
        return found
    start = 4.0
    while start + length <= keys[-1] / 30 - 0.5:
        span = [rows[k] for k in range(round(start * 30), round((start + length) * 30)) if k in rows]
        if span and all(r['alive'] and not r['driving'] and not r['jump'] for r in span):
            passes = sum(r['passes'] for r in span)
            over = sum(r['over'] for r in span)
            behind = sum(r['behind'] for r in span)
            whiz = sum(r['whiz'] for r in span)
            shots = sum(1 for u in fire if start <= u < start + length)
            score = 6 * passes + 0.08 * min(over, 150) + 0.03 * min(behind, 300) + min(whiz, 12) - 0.03 * shots
            found.append({'start': round(start, 2), 'score': round(score, 2), 'passes': round(passes, 2), 'over': over,
                          'behind': behind, 'whiz': whiz, 'shots': shots})
        start += 0.5
    found.sort(key=lambda w: -w['score'])
    best = []
    for w in found:
        if all(abs(w['start'] - b['start']) >= length for b in best):
            best.append(w)
        if len(best) >= 4:
            break
    return best

def scout(job):
    (map_variant, seed), a, index = job
    name = run_name(map_variant, seed)
    # (a variant's rules after "+", as a rotation entry's: team_vehicles=custom+banshees=2)
    env = dict(os.environ, **dict(match_env(map_variant.split('+')[0], seed, a, index), NETTEST=f'local:{map_variant}', HIDE_HUD='1', CONSOLE=a.console,
                                  HUD_AFTER='bots: the general profile'),
               PORT=str(3700 + index),
               RUNS=f'{RUNS}/{a.out}', SIZE='640x360', TRACE_SECONDS=str(a.seconds), HALO_SCREENSHOT_FROM='999999')
    for setting in a.env:
        key, _, value = setting.partition('=')
        if value:
            env[key] = value
        else:
            env.pop(key, None)
    base = f'{RUNS}/{a.out}/{name}'
    if not (a.rescore and os.path.exists(f'{base}/data/trace.txt')):
        subprocess.run([f'{HERE}/capture.sh', name, 'openh1', '-', a.script, str(a.seconds + 120)], env=env,
                       capture_output=True, text=True)
    try:
        ticks, owners, fire = read_trace(f'{base}/data/trace.txt')
    except FileNotFoundError:
        return {'match': name, 'map': map_variant, 'seed': seed, 'error': 'no trace', 'windows': []}
    best = windows(features(ticks, owners, a.deathless), fire, a.seconds, a.length)
    for w in best:
        w.update(match=name, map=map_variant, seed=seed)
    return {'match': name, 'map': map_variant, 'seed': seed, 'windows': best}

def main():
    p = argparse.ArgumentParser()
    p.add_argument('maps'); p.add_argument('--seeds', default='7')
    p.add_argument('--bots', type=int, default=15); p.add_argument('--skill', default='legendary')
    p.add_argument('--seconds', type=float, default=180); p.add_argument('--length', type=float, default=15)
    p.add_argument('--out', default='audio-scout'); p.add_argument('--parallel', type=int, default=4)
    p.add_argument('--rescore', action='store_true')
    # (the player kept alive by cheat_deathless_player: its health 0 is not death)
    p.add_argument('--deathless', action='store_true')
    # (script commands sent once the game draws, as capture.sh's CONSOLE; timed on
    # the game's clock so that a match plays the same again: "(sleep_until (>=
    # (game_time) 1200) 1) (cheat_all_vehicles) " drops every vehicle by the player)
    p.add_argument('--console', default='')
    # (settings over the match's, NAME=value; NAME= removes one: HALO_BOT_PLAYER= leaves
    # the player to the script, standing where it spawned)
    p.add_argument('--env', action='append', default=[])
    # (the first player's input, debug.test_input's script: "0 aim=90,-5")
    p.add_argument('--script', default='')
    a = p.parse_args()
    jobs = [((m, int(s)), a, i) for i, (m, s) in enumerate((m, s) for m in a.maps.split(',') for s in a.seeds.split(','))]
    with ThreadPoolExecutor(a.parallel) as pool:
        results = list(pool.map(scout, jobs))
    found = sorted((w for r in results for w in r['windows']), key=lambda w: -w['score'])
    os.makedirs(f'{RUNS}/{a.out}', exist_ok=True)
    json.dump({'args': vars(a), 'windows': found, 'errors': [r for r in results if r.get('error')]},
              open(f'{RUNS}/{a.out}/windows.json', 'w'), indent=1)
    for r in results:
        if r.get('error'):
            print(r['match'], r['error'])
    for w in found[:15]:
        print(f"{w['score']:6}  {w['match']:28} {w['start']:6.1f}s  passes {w['passes']:5} over {w['over']:3} "
              f"behind {w['behind']:3} whiz {w['whiz']:2} shots {w['shots']}")

if __name__ == '__main__':
    main()
