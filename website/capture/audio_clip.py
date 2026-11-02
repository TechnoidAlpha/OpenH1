"""Film the 3D audio clip: a window of a scouted match (audio_scout.py) played
twice side by side, the same input and seed, once with audio.spatial
"headphones" (the HRTF) and once "speakers" (the stereo pan), each recording
the game's mix in step with its frames (debug.audio_record with the fixed
frame time); the first's frames streamed to the GPU's encoder as the master.
Both mixes are cut at the first frame saved, to the clip's length, and the
two runs' sim traces compared: the same game, tick for tick.

usage: audio_clip.py <map:variant> <seed> <start> [--length 15] [--size 2560x1440] [--out audio-clip]

Then the page's files: encode-audio-web.sh."""
import argparse, filecmp, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
RUNS = os.environ.get('RUNS', os.path.expanduser('~/halo-capture-runs'))
sys.path.insert(0, HERE)
from scout import match_env

def run(job):
    spatial, index, a = job
    name = f'{a.out}-{spatial}'
    end = a.start + a.length
    env = dict(os.environ, **dict(match_env(a.map.split('+')[0], a.seed, argparse.Namespace(bots=a.bots, skill=a.skill), index),
                                  NETTEST=f'local:{a.map}', HIDE_HUD='1', CONSOLE=a.console,
                                  HUD_AFTER='bots: the general profile'),
               PORT=str(3800 + index), SIZE=a.size, TRACE_SECONDS=f'{end + 0.5:.2f}', AUDIO='1', SPATIAL=spatial,
               HALO_SCREENSHOT_FROM=f'{a.start:.3f}', HALO_SCREENSHOT_UNTIL=f'{end:.3f}')
    for setting in a.env:
        key, _, value = setting.partition('=')
        if value:
            env[key] = value
        else:
            env.pop(key, None)
    if spatial == 'headphones':
        env['STREAM_FPS'] = '60'
    else:
        env['HALO_SCREENSHOT_STREAM'] = '/dev/null'
    subprocess.run([f'{HERE}/capture.sh', name, 'openh1', '-', a.script, str(int(end) + 600)], env=env,
                   capture_output=True, text=True)
    root = f'{RUNS}/{name}'
    found = re.search(r"recorded sound's frame (\d+)", open(f'{root}/log.txt', errors='replace').read())
    if not found:
        return f'{name}: no sound'
    os.makedirs(f'{RUNS}/{a.out}', exist_ok=True)
    subprocess.run(['ffmpeg', '-nostdin', '-loglevel', 'error', '-y', '-ss', f'{int(found.group(1)) / 48000:.6f}',
                    '-t', f'{a.length:.6f}', '-i', f'{root}/sound.wav', f'{RUNS}/{a.out}/{spatial}.wav'], check=True)
    if spatial == 'headphones':
        os.replace(f'{root}/stream.mp4', f'{RUNS}/{a.out}/master.mp4')
    return f'{name}: sound from frame {found.group(1)}'

def main():
    p = argparse.ArgumentParser()
    p.add_argument('map'); p.add_argument('seed', type=int); p.add_argument('start', type=float)
    p.add_argument('--length', type=float, default=15); p.add_argument('--size', default='2560x1440')
    p.add_argument('--bots', type=int, default=15); p.add_argument('--skill', default='legendary')
    p.add_argument('--out', default='audio-clip'); p.add_argument('--console', default='')
    # (settings over the match's, NAME=value; NAME= removes one: HALO_BOT_PLAYER= leaves
    # the player to the script, standing where it spawned)
    p.add_argument('--env', action='append', default=[])
    # (the first player's input, debug.test_input's script: "0 aim=90,-5")
    p.add_argument('--script', default='')
    a = p.parse_args()
    with ThreadPoolExecutor(2) as pool:
        for line in pool.map(run, [('headphones', 0, a), ('speakers', 1, a)]):
            print(line, flush=True)
    same = filecmp.cmp(f'{RUNS}/{a.out}-headphones/data/trace.txt', f'{RUNS}/{a.out}-speakers/data/trace.txt', shallow=False)
    print('sim traces', 'the same' if same else 'DIFFER')

if __name__ == '__main__':
    main()
