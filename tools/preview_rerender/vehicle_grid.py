#!/usr/bin/env python3
"""A labelled sheet of vehicle placements at a found camera pose (for looking).
    vehicle_grid.py <pose folder> <out> --vehicle T --rider T --seat S --ahead .. --across .. --rel .."""
import argparse
import itertools
import json
import math
import sys
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).parent))
import view as rr_view  # noqa: E402
import search as S  # noqa: E402
from game import Game  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("inp")
ap.add_argument("out")
ap.add_argument("--vehicle", required=True)
ap.add_argument("--rider")
ap.add_argument("--seat")
ap.add_argument("--color", nargs=3, type=float)
ap.add_argument("--ahead", nargs="+", type=float, required=True)
ap.add_argument("--across", nargs="+", type=float, required=True)
ap.add_argument("--rel", nargs="+", type=float, required=True)
ap.add_argument("--dz", nargs="+", type=float, default=[0.0])
ap.add_argument("--dpitch", type=float, default=0.0)
ap.add_argument("--port", type=int, default=23521)
a = ap.parse_args()
found = json.loads((Path(a.inp) / "pose.json").read_text())
p = found["pose"]
cam0 = [p["x"], p["y"], p["z"], p["yaw"], p["pitch"], p["roll"], p["vfov"]]
aspect = found["picture"]["source_aspect"]
out = Path(a.out)
out.mkdir(parents=True, exist_ok=True)
target = S.load_target(found["target"])
tf = rr_view.features(target, S.PICTURE)
game = Game(out / "run", found["scenario"], *S.WINDOW, port=a.port)
cells = [cv2.resize(target, (280, 228), interpolation=cv2.INTER_CUBIC)]
rows = []
try:
    game.connect()
    game.wait_frames(400, timeout=600)
    game.prepare_scene()
    with game.burst():
        for dz, ahead, across, rel in itertools.product(a.dz, a.ahead, a.across, a.rel):
            cam = list(cam0)
            cam[2] += dz
            cam[4] += a.dpitch
            h = math.radians(cam[3] - across)
            o = [{"tag": a.vehicle, "x": cam[0] + ahead * math.cos(h), "y": cam[1] + ahead * math.sin(h),
                  "z": cam0[2] + 0.4, "yaw": (cam[3] + rel) % 360, "pitch": 0.0, "roll": 0.0}]
            if a.rider:
                o.append({"tag": a.rider, "seat": a.seat, **({"color": a.color} if a.color else {})})
            game.place_objects(o, frames=2, settle_seconds=1.5)
            pic = S.frame_to_picture(game.render(tuple(cam), settle=3), aspect, S.PICTURE)
            s = rr_view.score(tf, rr_view.features(pic, S.PICTURE))[0]
            im = Image.fromarray(cv2.resize(pic, (280, 228), interpolation=cv2.INTER_CUBIC))
            ImageDraw.Draw(im).text((3, 3), "%d: %.2f %.1f %.0f %.0f  %.3f" % (len(cells), dz, ahead, across, rel, s),
                                    fill=(255, 255, 0))
            cells.append(np.asarray(im))
            rows.append([dz, ahead, across, rel, float(s)])
finally:
    game.close()
cols = 4
grid = []
for r in range(0, len(cells), cols):
    row = cells[r:r + cols] + [np.zeros_like(cells[0])] * (cols - len(cells[r:r + cols]))
    grid.append(np.concatenate(row, 1))
Image.fromarray(np.concatenate(grid, 0)).save(out / "sheet.png")
(out / "grid.json").write_text(json.dumps(rows))
print(json.dumps(rows))
