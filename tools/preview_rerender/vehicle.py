#!/usr/bin/env python3
"""A vehicle (with a rider) fitted into a found camera pose, unmasked:
its place (ahead of the camera, across) and heading searched with the camera
held, each sample placed and let come to rest; then the camera polished with
the vehicle held; the pose file then lists the objects.

    vehicle.py <in folder with pose.json> <out folder> --vehicle TAG [--rider TAG --seat LABEL]
"""
import argparse
import itertools
import json
import math
import sys
import time
from pathlib import Path

import cma
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import view as rr_view  # noqa: E402
import search as S  # noqa: E402
from game import Game  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--vehicle", required=True)
    ap.add_argument("--rider", default=None)
    ap.add_argument("--seat", default=None)
    ap.add_argument("--color", nargs=3, type=float, default=None)
    ap.add_argument("--across", nargs="+", type=float, default=[-20, -15, -10, -5], help="degrees off the view's centre")
    ap.add_argument("--ahead", nargs="+", type=float, default=[1.2, 1.5, 1.8, 2.2])
    ap.add_argument("--headings", type=int, default=20, help="degrees between the headings tried")
    ap.add_argument("--region", nargs=4, type=int, default=None,
                    help="x0 y0 x1 y1 of the picture the vehicle is scored on (its own part)")
    ap.add_argument("--start", nargs=3, type=float, default=None, help="ahead across heading: no grid")
    ap.add_argument("--settle", type=float, default=1.5)
    ap.add_argument("--evals", type=int, default=60)
    ap.add_argument("--port", type=int, default=23501)
    a = ap.parse_args()
    found = json.loads((Path(a.inp) / "pose.json").read_text())
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    target = S.load_target(found["target"])
    Image.fromarray(target).save(out / "target.png")
    tf = rr_view.features(target, S.PICTURE)
    if a.region:
        x0, y0, x1, y1 = a.region
        rf = rr_view.features(target[y0:y1, x0:x1], (x1 - x0, y1 - y0))
    p = found["pose"]
    cam = [p["x"], p["y"], p["z"], p["yaw"], p["pitch"], p["roll"], p["vfov"]]
    aspect = found["picture"]["source_aspect"]
    log = open(out / "search_log.txt", "a")

    def say(*parts):
        line = " ".join(str(v) for v in parts)
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    def objects(vx, vy, vz, vyaw):
        o = [{"tag": a.vehicle, "x": vx, "y": vy, "z": vz, "yaw": vyaw % 360, "pitch": 0.0, "roll": 0.0}]
        if a.rider:
            r = {"tag": a.rider, "seat": a.seat}
            if a.color:
                r["color"] = a.color
            o.append(r)
        return o

    def place_from(c, ahead, across, rel_yaw):
        """the vehicle's place: ahead along the view's level heading, turned
        across degrees; its heading: the camera's plus rel_yaw"""
        heading = math.radians(c[3] - across)  # (yaw counter-clockwise; right of centre is minus)
        vx = c[0] + ahead * math.cos(heading)
        vy = c[1] + ahead * math.sin(heading)
        return vx, vy, c[2] + 0.4, c[3] + rel_yaw

    game = Game(out / "run", found["scenario"], *S.WINDOW, port=a.port)
    t0 = time.time()
    evals = [0]
    try:
        game.connect()
        game.wait_frames(400, timeout=600)
        game.prepare_scene()

        def score_objects(obj, c):
            game.place_objects(obj, frames=2, settle_seconds=a.settle)
            frame = game.render(tuple(c), settle=3)
            evals[0] += 1
            pic = S.frame_to_picture(frame, aspect, S.PICTURE)
            if a.region:
                x0, y0, x1, y1 = a.region
                return rr_view.score(rf, rr_view.features(pic[y0:y1, x0:x1], (x1 - x0, y1 - y0)))[0], pic
            return rr_view.score(tf, rr_view.features(pic, S.PICTURE))[0], pic

        # ----- coarse: place and heading
        best = (-1, None)
        with game.burst():
            s0, _ = score_objects([], cam)
            say("camera alone, unmasked: %.4f" % s0)
            grid = [tuple(a.start)] if a.start else itertools.product(a.ahead, a.across, range(0, 360, a.headings))
            for ahead, across, rel in grid:
                v = place_from(cam, ahead, across, rel)
                s, _ = score_objects(objects(*v), cam)
                if s > best[0]:
                    best = (s, (ahead, across, float(rel)))
                    say("  %.4f ahead %.1f across %.0f heading %+d" % (s, ahead, across, rel))
        say("coarse", best)

        # ----- refine the vehicle (camera held)
        x0 = np.array(best[1])
        es = cma.CMAEvolutionStrategy(np.zeros(3), 1.0, {"CMA_stds": [0.2, 2.0, 10.0], "maxfevals": a.evals,
                                                          "popsize": 8, "verbose": -9, "seed": 3})
        with game.burst():
            while not es.stop():
                cands = es.ask()
                scores = []
                for c in cands:
                    q = x0 + c
                    s, _ = score_objects(objects(*place_from(cam, *q)), cam)
                    scores.append(s)
                    if s > best[0]:
                        best = (s, tuple(q))
                es.tell(cands, [-s for s in scores])
        say("vehicle refined %.4f" % best[0], np.round(best[1], 3).tolist())
        vehicle = objects(*place_from(cam, *best[1]))

        # ----- the camera polished with the vehicle held (placed once)
        x = np.array(cam + [aspect])
        cbest = (-1, x)
        with game.burst():
            game.place_objects(vehicle, frames=2, settle_seconds=a.settle * 2)

            def ev(q):
                frame = game.render(tuple(q[:7]))
                pic = S.frame_to_picture(frame, q[7], S.PICTURE)
                return rr_view.score(tf, rr_view.features(pic, S.PICTURE))[0]
            cbest = (ev(x), x)
            es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
                "CMA_stds": [0.15, 0.15, 0.08, 1.0, 1.0, 0.7, 1.5, 0.02], "maxfevals": 200, "popsize": 10,
                "verbose": -9, "seed": 4})
            while not es.stop():
                cands = es.ask()
                scores = [ev(x + c) for c in cands]
                es.tell(cands, [-s for s in scores])
                for s, c in zip(scores, cands):
                    if s > cbest[0]:
                        cbest = (s, x + c)
            say("camera polished %.4f" % cbest[0], np.round(cbest[1], 3).tolist())
            q = [float(v) for v in cbest[1]]
            frame = game.render(tuple(q[:7]), settle=3)
        pic = S.frame_to_picture(frame, q[7], S.PICTURE)
        Image.fromarray(pic).save(out / "best_small.png")
        s, parts = rr_view.score(tf, rr_view.features(pic, S.PICTURE))
        result = dict(found)
        result.update({
            "picture": {"width": S.PICTURE[0], "height": S.PICTURE[1], "source_aspect": q[7], "masks": []},
            "pose": {"x": q[0], "y": q[1], "z": q[2], "yaw": q[3], "pitch": q[4], "roll": q[5], "vfov": q[6],
                     "game_fov": math.degrees(rr_view.fov_from_vfov(q[6]))},
            "objects": vehicle,
            "score": float(s),
            "score_parts": dict(zip(("gray_rank", "edge_strength", "edge_direction"), map(float, parts))),
            "ssim_gray": float(rr_view.ssim_gray(target, pic)),
            "camera_search": {"from": str(Path(a.inp) / "pose.json"), "score_masked": found["score"]},
            "vehicle_search": {"renders": evals[0], "seconds": time.time() - t0},
        })
        result.pop("final", None)
        (out / "pose.json").write_text(json.dumps(result, indent=1))
        say("score %.4f" % s)
    finally:
        game.close()


if __name__ == "__main__":
    main()
