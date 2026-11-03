#!/usr/bin/env python3
"""A found pose refined again with wider steps (CMA-ES restarts from it),
for a place found right but framed off; the pose file then the best.

    polish.py <in folder with pose.json> <out folder> [--rounds 3 --evals 300]
"""
import argparse
import json
import math
import shutil
import sys
from pathlib import Path

import cma
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import view as rr_view  # noqa: E402
import search as S  # noqa: E402
from game import Game  # noqa: E402

VFOV_MAX = 72.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out")
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--evals", type=int, default=300)
    ap.add_argument("--port", type=int, default=24601)
    a = ap.parse_args()
    found = json.loads((Path(a.inp) / "pose.json").read_text())
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(Path(a.inp) / "target.png", out / "target.png")
    target = S.load_target(found["target"])
    tf = rr_view.features(target, S.PICTURE)
    p = found["pose"]
    x = np.array([p["x"], p["y"], p["z"], p["yaw"], p["pitch"], p["roll"], p["vfov"],
                  found["picture"]["source_aspect"]])
    game = Game(out / "run", found["scenario"], *S.WINDOW, port=a.port)
    try:
        game.connect()
        game.wait_frames(400, timeout=600)
        game.prepare_scene()
        if found.get("bsp") is not None:
            game.command("switch_bsp %d" % found["bsp"], frames=30)
            game.render((0, 0, 0, 0, 0, 0, 50), settle=4)

        def ev(q):
            q = q.copy()
            q[6] = min(max(q[6], 15), VFOV_MAX)
            q[7] = min(max(q[7], 1.12), 1.42)
            pic = S.frame_to_picture(game.render(tuple(q[:7])), q[7], S.PICTURE)
            if rr_view.gray(pic).std() < 6:
                return 0.0, pic
            return rr_view.score(tf, rr_view.features(pic, S.PICTURE))[0], pic

        with game.burst():
            best = (ev(x)[0], x)
            print("start %.4f" % best[0], flush=True)
            for r, sigma in enumerate([2.0, 1.0, 0.5][:a.rounds]):
                es = cma.CMAEvolutionStrategy(np.zeros(8), sigma, {
                    "CMA_stds": [1.5, 1.5, 0.6, 6, 5, 3, 6, 0.05], "maxfevals": a.evals, "popsize": 14,
                    "verbose": -9, "seed": 30 + r})
                x0 = best[1]
                while not es.stop():
                    cands = es.ask()
                    scores = [ev(x0 + c)[0] for c in cands]
                    es.tell(cands, [-s for s in scores])
                    i = int(np.argmax(scores))
                    if scores[i] > best[0]:
                        best = (scores[i], x0 + cands[i])
                print("round %d %.4f" % (r, best[0]), np.round(best[1], 3).tolist(), flush=True)
            s, pic = ev(best[1])
        Image.fromarray(pic).save(out / "best_small.png")
        q = [float(v) for v in best[1]]
        q[6] = min(max(q[6], 15), VFOV_MAX)
        result = dict(found)
        result.pop("final", None)
        result["picture"] = dict(found["picture"], source_aspect=min(max(q[7], 1.12), 1.42))
        result["pose"] = {"x": q[0], "y": q[1], "z": q[2], "yaw": q[3], "pitch": q[4], "roll": q[5], "vfov": q[6],
                          "game_fov": math.degrees(rr_view.fov_from_vfov(q[6]))}
        result["score"] = float(s)
        result["polished_from"] = {"folder": str(a.inp), "score": found["score"]}
        (out / "pose.json").write_text(json.dumps(result, indent=1))
        print("score %.4f" % s)
    finally:
        game.close()


if __name__ == "__main__":
    main()
