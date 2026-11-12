#!/usr/bin/env python3
"""fold_test.sh's plans on a computer, with the release's halo.exe (or bin/openh1).

    python laptop_test.py --game <folder with halo.exe> --data <folder with maps> [--label iris-xe] ao [rounds]

The same runs as on the phone: a test config.toml next to the game and an
init.txt in the data root, the game started and left to quit at
debug.exit_after, the log kept, the GPU time per frame from the 600-frame stats
lines, the screenshots' checksums. The config.toml and init.txt that were there
are put back at the end. The game draws in a 1920x1080 window (--size), muted.

The game takes the discrete GPU when there is one switched on, so a laptop with
two is tested once with the discrete GPU off and once with it on (--label names
the results).

Results: openh1-tests/<label>-<plan>-<time>/ beside this script's working
folder (--out), summary.txt the answer, and comparison sheets in sheets/.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

AO_MAPS = [r"levels\b30\b30", r"levels\a10\a10"]
GPU_LINE = re.compile(r"GPU ([0-9.]+) ms a frame, ([0-9.]+) ms between frames")


def short(map_name):
    return map_name.replace("/", "\\").rsplit("\\", 1)[-1]


class Runner:
    def __init__(self, args):
        self.args = args
        self.game = Path(args.game)
        self.exe = next(p for p in (self.game / "halo.exe", self.game / "bin" / "openh1") if p.exists())
        self.data = Path(args.data)
        stamp = time.strftime("%Y%m%d-%H%M%S")
        self.out = Path(args.out) / f"{args.label}-{args.plan}-{stamp}"
        self.out.mkdir(parents=True)
        self.config = self.game / "config.toml"
        self.init = self.data / "init.txt"
        self.kept = {}
        for path in (self.config, self.init):
            self.kept[path] = path.read_bytes() if path.exists() else None
        self.device = None

    def restore(self):
        for path, data in self.kept.items():
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)
        self.say("the config.toml and init.txt that were there are back")

    def say(self, text):
        line = time.strftime("%H:%M:%S ") + text
        print(line, flush=True)
        with open(self.out / "run.txt", "a", encoding="utf-8") as f:
            f.write(line + "\n")

    def write_config(self, aa, mode, seconds, ao, view, shots):
        lines = ["[display]", 'mode = "windowed"', f'window_size = "{self.args.size}"', f'anti_aliasing = "{aa}"']
        if mode == "timing":
            lines.append("vsync = false")
        if ao:
            lines.append(f'ambient_occlusion = "{ao}"')
        lines += ["", "[audio]", "volume = 0.0", "", "[paths]", f"data = '{self.data}'", "", "[debug]",
                  "gpu_stats = true", "skip_cinematics = true", f'test_input = "bot:{self.args.seed}"',
                  f"exit_after = {seconds}", 'update_answer = "no"']
        if view:
            lines.append("ambient_occlusion_view = true")
        if mode == "pictures":
            lines += ["frame_time = 0.0166667", f"screenshot_directory = '{shots}'", "screenshot_every = 60",
                      "screenshot_from = 3.0", "screenshot_until = 20.0"]
        self.config.write_bytes(("\n".join(lines) + "\n").encode())

    def run(self, name, map_name, aa, mode, seconds, ao=None, view=False):
        run_dir = self.out / name
        shots = run_dir / "shots"
        shots.mkdir(parents=True)
        self.write_config(aa, mode, seconds, ao, view, shots)
        self.init.write_bytes(f"map_name {map_name}\n".encode())
        shutil.copy(self.config, run_dir / "config.toml")
        self.say(f"run {name}: {short(map_name)}, {aa}, ao {ao or 'default'}{' view' if view else ''}, {mode}, {seconds}s")
        env = dict(os.environ, HALO_VULKAN_STATS="1")
        with open(run_dir / "log.txt", "wb") as log:
            try:
                subprocess.run([str(self.exe)], cwd=self.game, env=env, stdout=log, stderr=subprocess.STDOUT,
                               timeout=seconds + 180)
            except subprocess.TimeoutExpired:
                self.say("  still running after the time allowed: stopped")
        text = (run_dir / "log.txt").read_text("utf-8", "replace")
        device = re.search(r"vulkan: (.+?) \(Vulkan", text)
        if device and not self.device:
            self.device = device.group(1)
        for pattern in ("vulkan: swapchain", "discards on", "ambient occlusion:"):
            line = next((l for l in text.splitlines() if pattern in l), None)
            if line:
                self.say("  " + line.split(": ", 1)[1])
        windows = [(float(g), float(b)) for g, b in GPU_LINE.findall(text)]
        (run_dir / "gpu.txt").write_text("".join(f"{g} {b}\n" for g, b in windows))
        rest = windows[2:]
        mean = (sum(g for g, _ in rest) / len(rest), sum(b for _, b in rest) / len(rest), len(rest)) if rest else None
        (run_dir / "gpu_mean.txt").write_text("%.3f %.3f %d\n" % mean if mean else "- - 0\n")
        if mean:
            self.say(f"  GPU {mean[0]:.3f} ms a frame, {mean[1]:.3f} ms between frames ({mean[2]} windows of 600 frames)")
        if mode == "pictures":
            files = sorted(shots.glob("*.bmp"))
            sums = {f.name: hashlib.md5(f.read_bytes()).hexdigest() for f in files}
            (run_dir / "shots.md5").write_text("".join(f"{h}  {n}\n" for n, h in sums.items()))
            for i, f in enumerate(files):
                if i % self.args.keep == 0:
                    shutil.move(str(f), run_dir / f.name)
            shutil.rmtree(shots)
            self.say(f"  {len(files)} screenshots")
        else:
            shutil.rmtree(shots)
        return mean

    def sums(self, name):
        path = self.out / name / "shots.md5"
        return dict(reversed(l.split()) for l in path.read_text().splitlines()) if path.exists() else {}

    def differ(self, a, b):
        sa, sb = self.sums(a), self.sums(b)
        both = [n for n in sa if n in sb]
        return f"{sum(sa[n] != sb[n] for n in both)} of {len(both)}"

    def plan_smoke(self):
        self.run("smoke", AO_MAPS[0], "smaa", "timing", 40)
        (self.out / "summary.txt").write_text(f"smoke test on {self.device}\n" + (self.out / "smoke" / "gpu.txt").read_text())

    def plan_ao(self):
        maps = self.args.maps.split()
        for m in maps:
            if not (self.data / "maps" / f"{short(m)}.map").exists():
                sys.exit(f"no {short(m)}.map in {self.data / 'maps'}")
        timed = short(maps[0])
        rows = []
        for r in range(1, self.args.rounds + 1):
            order = ("off", "strong") if r % 2 else ("strong", "off")
            for aa in self.args.aa.split():
                for ao in order:
                    mean = self.run(f"t{r}-{timed}-{aa}-{ao}", maps[0], aa, "timing", self.args.timing, ao)
                    rows.append((aa, ao, r, mean))
        for m in maps:
            s = short(m)
            for ao in ("off", "subtle", "strong"):
                self.run(f"p-{s}-{ao}", m, "smaa", "pictures", self.args.pictures, ao)
            self.run(f"p-{s}-view", m, "smaa", "pictures", self.args.pictures, "strong", view=True)
        logs = "".join((d / "log.txt").read_text("utf-8", "replace") for d in self.out.iterdir() if (d / "log.txt").exists())
        errors = [l for l in logs.splitlines() if "error" in l.lower()]
        warned = logs.count("ambient_occlusion's passes cannot be made")
        out = [f"OpenH1 ambient occlusion (display.ambient_occlusion), bot:{self.args.seed}",
               f"device: {self.device}, window {self.args.size}", "",
               f"GPU time per frame on {timed} (mean of the 600-frame windows after the first two):",
               "AA      AO      round  GPU ms/frame  ms between frames  windows"]
        for aa, ao, r, mean in sorted(rows, key=lambda x: (x[0], x[1], x[2])):
            g, b, n = mean if mean else ("-", "-", 0)
            out.append(f"{aa:<7} {ao:<7} {r:<6} {g if isinstance(g, str) else f'{g:.3f}':<13} "
                       f"{b if isinstance(b, str) else f'{b:.3f}':<18} {n}")
        out += ["", "Pictures (SMAA, frame_time fixed, a shot every 60 frames from 3 s to 20 s);",
                "off and the others should differ, as that is the occlusion laid:"]
        for m in maps:
            s = short(m)
            out.append(f"  {s}: off vs subtle {self.differ(f'p-{s}-off', f'p-{s}-subtle')} differ, "
                       f"off vs strong {self.differ(f'p-{s}-off', f'p-{s}-strong')}, "
                       f"strong vs the occlusion alone {self.differ(f'p-{s}-strong', f'p-{s}-view')}")
        out += ["", "From the logs:",
                f"  'passes cannot be made' warnings: {warned}",
                f"  lines with 'error': {len(errors)}"] + ["    " + l for l in errors[:5]]
        (self.out / "summary.txt").write_text("\n".join(out) + "\n")
        sheets = Path(__file__).with_name("compare_shots.py")
        for m in maps:
            s = short(m)
            subprocess.run([sys.executable, str(sheets), str(self.out), str(self.out / "sheets"),
                            f"p-{s}-off", f"p-{s}-subtle", f"p-{s}-strong", f"p-{s}-view"])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--game", required=True)
    p.add_argument("--data", required=True)
    p.add_argument("--label", default="laptop")
    p.add_argument("--out", default="openh1-tests")
    p.add_argument("--size", default="1920x1080")
    p.add_argument("--seed", type=int, default=3)
    p.add_argument("--timing", type=int, default=90)
    p.add_argument("--pictures", type=int, default=35)
    p.add_argument("--keep", type=int, default=2)
    p.add_argument("--maps", default=" ".join(AO_MAPS))
    # "smaa" alone on a GPU whose driver hangs on MSAA (an Iris Xe froze on 4 Nov 2026)
    p.add_argument("--aa", default="smaa msaa4x", help="the timing runs' anti-aliasing, separated by spaces")
    p.add_argument("plan", choices=("smoke", "ao"))
    p.add_argument("rounds", nargs="?", type=int, default=2)
    args = p.parse_args()
    runner = Runner(args)
    try:
        getattr(runner, "plan_" + args.plan)()
    finally:
        runner.restore()
    runner.say("done")
    print()
    print((runner.out / "summary.txt").read_text())
    print("All of it:", runner.out.resolve())


if __name__ == "__main__":
    main()
