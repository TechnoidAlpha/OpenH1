#!/usr/bin/env python3
"""Renders a list of poses into a labelled contact sheet (for looking).

    shots.py --scenario S --out dir --poses poses.json [--objects objects.json] [--places]
poses.json: [[x, y, z, yaw, pitch, roll, vfov], ...] (an optional 8th item: the bsp)"""
import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
from game import Game  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--poses", default=None)
    ap.add_argument("--objects", default=None)
    ap.add_argument("--places", action="store_true")
    ap.add_argument("--port", type=int, default=23491)
    ap.add_argument("--cols", type=int, default=4)
    ap.add_argument("--cell", type=int, default=320)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    game = Game(out / "run", a.scenario, 1024, 576, port=a.port)
    try:
        game.connect()
        game.wait_frames(400, timeout=600)
        game.prepare_scene()
        if a.places:
            game.command('debug_scenario_places "places_0.txt"', frames=4)
            head = (out / "run" / "places_0.txt").read_text().splitlines()[0].split()
            for b in range(int(head[3])):
                game.command("switch_bsp %d" % b, frames=30)
                game.command('debug_scenario_places "places_%d.txt"' % b, frames=4)
        if a.objects:
            print(game.place_objects(json.loads(Path(a.objects).read_text())))
        poses = json.loads(Path(a.poses).read_text()) if a.poses else []
        cells = []
        bsp = None
        for i, p in enumerate(poses):
            if len(p) > 7 and p[7] != bsp:
                bsp = p[7]
                game.command("switch_bsp %d" % bsp, frames=30)
            frame = game.render(tuple(p[:7]), settle=3)
            w = a.cell
            h = int(w * 114 / 140)
            cell = cv2.resize(frame[:, (1024 - int(576 * 140 / 114)) // 2:][:, :int(576 * 140 / 114)], (w, h),
                              interpolation=cv2.INTER_AREA)
            im = Image.fromarray(cell)
            ImageDraw.Draw(im).text((3, 3), str(i), fill=(255, 255, 0))
            cells.append(np.asarray(im))
        if cells:
            rows = []
            for r in range(0, len(cells), a.cols):
                row = cells[r:r + a.cols]
                row += [np.zeros_like(cells[0])] * (a.cols - len(row))
                rows.append(np.concatenate(row, 1))
            Image.fromarray(np.concatenate(rows, 0)).save(out / "sheet.png")
        print(game.drain())
    finally:
        game.close()


if __name__ == "__main__":
    main()
