#!/usr/bin/env python3
"""A camera pose and one vehicle (with a rider) refined together, unmasked,
by turns: the vehicle with the camera held (each sample placed and let come
to rest), then the camera with the vehicle held; from a start of both.
The vehicle's place is kept relative to the start camera (the anchor), so the
camera's moves do not move it.

    joint.py <pose folder> <out> --vehicle T [--rider T --seat S --color r g b]
        --start-camera dz dpitch --start-vehicle ahead across heading [--rounds 2]
"""
import argparse
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
    ap.add_argument("--rider")
    ap.add_argument("--seat")
    ap.add_argument("--color", nargs=3, type=float)
    ap.add_argument("--start-camera", nargs=2, type=float, default=[0, 0], metavar=("DZ", "DPITCH"))
    ap.add_argument("--start-vehicle", nargs=3, type=float, required=True, metavar=("AHEAD", "ACROSS", "HEADING"))
    ap.add_argument("--rounds", type=int, default=2)
    ap.add_argument("--vehicle-evals", type=int, default=50)
    ap.add_argument("--camera-evals", type=int, default=200)
    ap.add_argument("--settle", type=float, default=1.5)
    ap.add_argument("--port", type=int, default=23531)
    a = ap.parse_args()
    found = json.loads((Path(a.inp) / "pose.json").read_text())
    p = found["pose"]
    anchor = [p["x"], p["y"], p["z"], p["yaw"]]
    cam = np.array([p["x"], p["y"], p["z"] + a.start_camera[0], p["yaw"], p["pitch"] + a.start_camera[1],
                    p["roll"], p["vfov"], found["picture"]["source_aspect"]])
    veh = np.array(a.start_vehicle, float)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    target = S.load_target(found["target"])
    Image.fromarray(target).save(out / "target.png")
    tf = rr_view.features(target, S.PICTURE)
    log = open(out / "search_log.txt", "a")

    def say(*parts):
        line = " ".join(str(v) for v in parts)
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    def objects(v):
        ahead, across, heading = v
        h = math.radians(anchor[3] - across)
        o = [{"tag": a.vehicle, "x": anchor[0] + ahead * math.cos(h), "y": anchor[1] + ahead * math.sin(h),
              "z": anchor[2] + 0.4, "yaw": (anchor[3] + heading) % 360, "pitch": 0.0, "roll": 0.0}]
        if a.rider:
            r = {"tag": a.rider, "seat": a.seat}
            if a.color:
                r["color"] = list(a.color)
            o.append(r)
        return o

    game = Game(out / "run", found["scenario"], *S.WINDOW, port=a.port)
    t0 = time.time()
    try:
        game.connect()
        game.wait_frames(400, timeout=600)
        game.prepare_scene()

        def shot(c):
            frame = game.render(tuple(c[:7]))
            pic = S.frame_to_picture(frame, c[7], S.PICTURE)
            return rr_view.score(tf, rr_view.features(pic, S.PICTURE))[0], pic

        def placed(v):
            game.place_objects(objects(v), frames=2, settle_seconds=a.settle)

        with game.burst():
            placed(veh)
            best = shot(cam)[0]
        say("start %.4f" % best, np.round(cam, 3).tolist(), veh.tolist())
        for rnd in range(a.rounds):
            # the vehicle, camera held
            es = cma.CMAEvolutionStrategy(np.zeros(3), 1.0, {"CMA_stds": [0.2, 1.5, 8.0], "popsize": 8,
                                                              "maxfevals": a.vehicle_evals, "verbose": -9,
                                                              "seed": 10 + rnd})
            with game.burst():
                while not es.stop():
                    cands = es.ask()
                    scores = []
                    for c in cands:
                        placed(veh + c)
                        scores.append(shot(cam)[0])
                    es.tell(cands, [-s for s in scores])
                    i = int(np.argmax(scores))
                    if scores[i] > best:
                        best, veh = scores[i], veh + cands[i]
                placed(veh)
                # (the score again, the vehicle placed afresh: its rest varies a little)
                best = shot(cam)[0]
            say("round %d vehicle %.4f" % (rnd, best), np.round(veh, 3).tolist())
            # the camera, vehicle held
            es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
                "CMA_stds": [0.12, 0.12, 0.08, 1.0, 1.0, 0.7, 1.5, 0.02], "popsize": 10,
                "maxfevals": a.camera_evals, "verbose": -9, "seed": 20 + rnd})
            with game.burst():
                while not es.stop():
                    cands = es.ask()
                    scores = [shot(cam + c)[0] for c in cands]
                    es.tell(cands, [-s for s in scores])
                    i = int(np.argmax(scores))
                    if scores[i] > best:
                        best, cam = scores[i], cam + cands[i]
            say("round %d camera %.4f" % (rnd, best), np.round(cam, 3).tolist())
        with game.burst():
            s, pic = shot(cam)
        Image.fromarray(pic).save(out / "best_small.png")
        _, parts = rr_view.score(tf, rr_view.features(pic, S.PICTURE))
        result = dict(found)
        result.pop("final", None)
        c = [float(v) for v in cam]
        result.update({
            "picture": {"width": S.PICTURE[0], "height": S.PICTURE[1], "source_aspect": c[7], "masks": []},
            "pose": {"x": c[0], "y": c[1], "z": c[2], "yaw": c[3], "pitch": c[4], "roll": c[5], "vfov": c[6],
                     "game_fov": math.degrees(rr_view.fov_from_vfov(c[6]))},
            "objects": objects(veh),
            "score": float(s),
            "score_parts": dict(zip(("gray_rank", "edge_strength", "edge_direction"), map(float, parts))),
            "ssim_gray": float(rr_view.ssim_gray(target, pic)),
            "camera_search": {"from": str(Path(a.inp) / "pose.json"), "score_masked": found["score"]},
            "joint_search_seconds": time.time() - t0,
        })
        (out / "pose.json").write_text(json.dumps(result, indent=1))
        say("score %.4f" % s)
    finally:
        game.close()


if __name__ == "__main__":
    main()
