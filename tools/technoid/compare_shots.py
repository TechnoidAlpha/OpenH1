#!/usr/bin/env python3
"""Comparison sheets from fold_test.sh's screenshots, made on a computer.

    python compare_shots.py <results folder> <sheets folder> RUN RUN [RUN ...]

The runs are folder names inside the results (p-b30-off p-b30-subtle
p-b30-strong). For each screenshot all the runs kept, one PNG 1600 pixels wide:
the first and last runs' whole frames on top, with a box where they differ
most, then that box at full size from every run, and the difference between
the first and last runs eight times brighter. Small things, a contact shadow
under a wheel, survive the close-up where a whole frame shrunk would lose them.

Needs Pillow and numpy.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

WIDTH = 1600
CROP_W, CROP_H = 800, 600


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def busiest_box(d):
    """The CROP_W x CROP_H box holding the most difference (integral image)."""
    h, w = d.shape
    cw, ch = min(CROP_W, w), min(CROP_H, h)
    s = np.zeros((h + 1, w + 1), np.int64)
    s[1:, 1:] = d.astype(np.int64).cumsum(0).cumsum(1)
    sums = s[ch:, cw:] - s[:-ch, cw:] - s[ch:, :-cw] + s[:-ch, :-cw]
    y, x = np.unravel_index(int(sums.argmax()), sums.shape)
    return int(x), int(y), cw, ch


def labelled(img, text):
    draw = ImageDraw.Draw(img)
    draw.rectangle((0, 0, 8 + 7 * len(text), 18), fill=(0, 0, 0))
    draw.text((4, 3), text, fill=(255, 255, 255))
    return img


def sheet(frames, names, shot):
    first, last = frames[0], frames[-1]
    d = np.abs(first - last).max(axis=2)
    x, y, cw, ch = busiest_box(d)
    h, w = d.shape
    tw = WIDTH // 2
    th = round(h * tw / w)
    tiles = []
    for frame, name in ((first, names[0]), (last, names[-1])):
        img = Image.fromarray(frame.astype(np.uint8)).resize((tw, th), Image.LANCZOS)
        k = tw / w
        ImageDraw.Draw(img).rectangle((x * k, y * k, (x + cw) * k, (y + ch) * k), outline=(255, 0, 255), width=2)
        tiles.append(labelled(img, f"{name} {shot}"))
    crops = [labelled(Image.fromarray(f[y:y + ch, x:x + cw].astype(np.uint8)), n) for f, n in zip(frames, names)]
    amp = np.clip(d[y:y + ch, x:x + cw] * 8, 0, 255).astype(np.uint8)
    crops.append(labelled(Image.fromarray(np.stack([amp] * 3, axis=2)), f"{names[0]} vs {names[-1]} x8"))
    rows = (len(crops) + 1) // 2
    out = Image.new("RGB", (WIDTH, th + rows * CROP_H), (24, 24, 24))
    out.paste(tiles[0], (0, 0))
    out.paste(tiles[1], (tw, 0))
    for i, c in enumerate(crops):
        out.paste(c, ((i % 2) * CROP_W, th + (i // 2) * CROP_H))
    changed = int((d > 0).sum())
    return out, changed, d.size, int(d.max())


def main():
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    root, out_dir, runs = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
    out_dir.mkdir(parents=True, exist_ok=True)
    shots = sorted(p.name for p in (root / runs[0]).glob("*.bmp"))
    for shot in shots:
        paths = [root / r / shot for r in runs]
        if not all(p.exists() for p in paths):
            continue
        img, changed, total, peak = sheet([load(p) for p in paths], runs, Path(shot).stem)
        name = f"{runs[0]}--{runs[-1]}-{Path(shot).stem}.png"
        img.save(out_dir / name, optimize=True)
        print(f"{name}: {100 * changed / total:.1f}% of pixels differ, largest {peak} levels")


if __name__ == "__main__":
    main()
