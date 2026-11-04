#!/usr/bin/env python3
"""Finds the camera pose of a menu preview picture in a map, by rendering it.

    search.py --scenario 'levels\\b30\\b30' --target originals/sp_levels_03.png --out b30 \\
        --box x0 x1 y0 y1 --heights 1.5 4 --step 7.5

A: panoramas (a ring of wide frames at pitch 0) from a grid of places;
B: at each place, the orientation and field of view found by remaking views
   from its panorama (no rendering), scored against the target;
C: CMA-ES over the whole pose (place, yaw, pitch, roll, vertical field of
   view, the picture's pixel shape) from the best places, each sample
   rendered by the live game.
The picture is the target's top-left 140x114 (the menus draw that much)."""
import argparse
import json
import math
import sys
import time
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import cma
import cv2
import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import view as rr_view  # noqa: E402

PICTURE = (140, 114)
COARSE = (70, 57)
PANO_VFOV = 72.0
PANO_COUNT = 6
WINDOW = (1024, 576)


def load_target(path):
    return np.asarray(Image.open(path).convert("RGB"))[:PICTURE[1], :PICTURE[0]]


def frame_to_picture(frame, aspect, size):
    """the window's frame cut to the picture's shape (aspect: the source's
    width over height before it was squeezed to 140x114) and resized"""
    return cv2.resize(rr_view.crop_aspect(frame, aspect), size, interpolation=cv2.INTER_AREA)


# ---------- B: orientation from a panorama

_target_coarse = None
# (the orientation search's pitches tried, and its limit; set for pictures looking up)
PITCHES = (-8, 0, 8)
PITCH_CLIP = 25
MASKS = []  # (rectangles of the picture, x0 y0 x1 y1 in its 140x114, left out of the score: what the map lacks)


def masked(image):
    """the masks filled flat grey, in the target and every view alike"""
    if not MASKS:
        return image
    image = image.copy()
    h, w = image.shape[:2]
    for x0, y0, x1, y1 in MASKS:
        image[int(y0 * h / PICTURE[1]):int(y1 * h / PICTURE[1]), int(x0 * w / PICTURE[0]):int(x1 * w / PICTURE[0])] = 128
    return image


def _init_worker(target, masks):
    global _target_coarse
    cv2.setNumThreads(1)
    MASKS[:] = masks
    _target_coarse = rr_view.features(masked(target), COARSE)


def orient(job):
    """the best (score, yaw, pitch, vfov) at one place, from its panorama"""
    index, views = job
    views = [(v.astype(np.float32), yaw, pitch, vfov) for v, yaw, pitch, vfov in views]
    aspect = PICTURE[0] / PICTURE[1]
    w, h = COARSE[0] * 2, COARSE[1] * 2

    def score(yaw, pitch, vfov):
        view = rr_view.pano_view(views, yaw, pitch, 0, vfov, w, h)
        return rr_view.score(_target_coarse, rr_view.features(masked(view), COARSE))[0]

    results = []
    for yaw in np.arange(0, 360, 6):
        for pitch in PITCHES:
            for vfov in (38, 50, 62):
                results.append((score(yaw, pitch, vfov), yaw, pitch, vfov))
    results.sort(reverse=True)
    best = []
    for s, yaw, pitch, vfov in results[:3]:
        x0 = np.array([yaw, pitch, vfov], float)
        steps = np.array([3.0, 3.0, 4.0])
        current = s
        for _ in range(4):  # (a pattern search, steps halving)
            improved = True
            while improved:
                improved = False
                for k in range(3):
                    for sign in (-1, 1):
                        trial = x0.copy()
                        trial[k] += sign * steps[k]
                        trial[1] = np.clip(trial[1], -PITCH_CLIP, PITCH_CLIP)
                        trial[2] = np.clip(trial[2], 20, 72)
                        t = score(*trial)
                        if t > current:
                            current, x0, improved = t, trial, True
            steps /= 2
        best.append((current, *x0.tolist()))
    best.sort(reverse=True)
    del aspect
    return index, best[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--box", nargs=4, type=float, required=True)
    parser.add_argument("--heights", nargs="+", type=float, required=True)
    parser.add_argument("--step", type=float, default=7.5)
    parser.add_argument("--top", type=int, default=4)
    parser.add_argument("--evals", type=int, default=240)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--port", type=int, default=23471)
    parser.add_argument("--wait-frames", type=int, default=400)
    parser.add_argument("--mask", nargs=4, type=float, action="append", default=[],
                        help="x0 y0 x1 y1 of the 140x114 picture left out of the score")
    parser.add_argument("--prepare", action="append", default=[], help="extra console lines once the scene is up")
    arguments = parser.parse_args()
    out = Path(arguments.out)
    out.mkdir(parents=True, exist_ok=True)
    target = load_target(arguments.target)
    Image.fromarray(target).save(out / "target.png")
    MASKS[:] = arguments.mask
    target_full = rr_view.features(masked(target), PICTURE)
    timings = {}
    log = open(out / "search_log.txt", "a")

    def say(*parts):
        line = " ".join(str(p) for p in parts)
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    from game import Game
    t_start = time.time()
    game = Game(out / "run", arguments.scenario, *WINDOW, port=arguments.port)
    try:
        game.connect()
        game.wait_frames(arguments.wait_frames)
        game.prepare_scene()
        for line in arguments.prepare:
            say("prepare:", line, game.command(line, frames=3).strip())
        timings["load_seconds"] = time.time() - t_start

        # ----- A: panoramas
        x0, x1, y0, y1 = arguments.box
        places = [(x, y, z) for x in np.arange(x0, x1 + 1e-6, arguments.step)
                  for y in np.arange(y0, y1 + 1e-6, arguments.step) for z in arguments.heights]
        say(f"A: {len(places)} places")
        t = time.time()
        panoramas = []
        game.unlock()
        cache = out / "panoramas.npz"
        if cache.exists():
            data = np.load(cache)
            if data["places"].shape == np.array(places).shape and np.allclose(data["places"], places):
                yaws = [360.0 * k / PANO_COUNT for k in range(PANO_COUNT)]
                images = data["images"]  # (once: each subscript of the file reads it all)
                panoramas = [[(images[i, k], yaws[k], 0.0, PANO_VFOV) for k in range(PANO_COUNT)]
                             for i in range(len(places))]
                say("A: from the cache")
        for start in range(0, len(places) if not panoramas else 0, 40):
            with game.burst():
                for x, y, z in places[start:start + 40]:
                    views = []
                    for k in range(PANO_COUNT):
                        yaw = 360.0 * k / PANO_COUNT
                        frame = game.render((x, y, z, yaw, 0, 0, PANO_VFOV))
                        views.append((cv2.resize(frame, (512, 288), interpolation=cv2.INTER_AREA), yaw, 0.0,
                                      PANO_VFOV))
                    panoramas.append(views)
            time.sleep(1.0)  # (the lock given up a moment between bursts)
        if not cache.exists():
            np.savez(cache, places=np.array(places), images=np.array([[v[0] for v in p] for p in panoramas]))
        timings["A_panorama_seconds"] = time.time() - t
        timings["A_places"] = len(places)
        say("A done", round(timings["A_panorama_seconds"], 1), "s")

        # ----- B: orientation at each place, from its panorama (CPU only)
        t = time.time()
        if (out / "coarse.json").exists():
            ranked = [(c["score"], tuple(c["place"]), tuple(c["yaw_pitch_vfov"]))
                      for c in json.loads((out / "coarse.json").read_text())]
            say("B: from the cache")
        else:
            with ProcessPoolExecutor(arguments.jobs, initializer=_init_worker, initargs=(target, MASKS)) as pool:
                coarse = {i: tuple(float(v) for v in r) for i, r in pool.map(orient, enumerate(panoramas), chunksize=2)}
            ranked = sorted(((coarse[i][0], places[i], coarse[i][1:]) for i in range(len(places))), reverse=True)
        timings["B_orientation_seconds"] = time.time() - t
        (out / "coarse.json").write_text(json.dumps(
            [{"score": s, "place": list(map(float, p)), "yaw_pitch_vfov": list(map(float, o))} for s, p, o in ranked],
            indent=1))
        say("B done", round(timings["B_orientation_seconds"], 1), "s; best:")
        for s, p, o in ranked[:8]:
            say("  %.4f" % s, np.round(p, 2).tolist(), np.round(o, 1).tolist())
        del panoramas

        # ----- C: the whole pose, each sample rendered
        t = time.time()
        renders = 0

        def evaluate(params):
            nonlocal renders
            x, y, z, yaw, pitch, roll, vfov, aspect = params
            frame = game.render((x, y, z, yaw, pitch, roll, vfov))
            renders += 1
            picture = frame_to_picture(frame, aspect, PICTURE)
            return rr_view.score(target_full, rr_view.features(masked(picture), PICTURE))[0]

        # (distinct places among the best)
        starts = []
        for s, p, o in ranked:
            if all(np.linalg.norm(np.array(p[:2]) - np.array(q[:2])) > arguments.step * 1.5 for _, q, _ in starts):
                starts.append((s, p, o))
            if len(starts) == arguments.top:
                break
        best = (-1, None)
        results = []
        lower = [-np.inf, -np.inf, None, -np.inf, -30, -15, 15, 1.12]
        upper = [np.inf, np.inf, None, np.inf, 30, 15, 80, 1.42]
        for s, p, o in starts:
            x = [p[0], p[1], p[2], o[0], o[1], 0.0, o[2], PICTURE[0] / PICTURE[1]]
            scale = np.array([arguments.step / 2, arguments.step / 2, 1.5, 4, 3, 2, 5, 0.06])
            # (the height kept near the grid's, above the ground it was put at)
            lo = [(-1.0 if k == 2 else (v - xi if np.isfinite(v) else None)) for k, (v, xi) in enumerate(zip(lower, x))]
            hi = [(4.0 if k == 2 else (v - xi if np.isfinite(v) else None)) for k, (v, xi) in enumerate(zip(upper, x))]
            es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
                "CMA_stds": scale, "bounds": [lo, hi],
                "maxfevals": arguments.evals, "popsize": 12, "verbose": -9, "seed": 1})
            local_best = (-1, None)
            with game.burst():
                while not es.stop():
                    candidates = es.ask()
                    params = [np.array(x) + c for c in candidates]
                    scores = [evaluate(q) for q in params]
                    es.tell(candidates, [-v for v in scores])
                    for v, q in zip(scores, params):
                        if v > local_best[0]:
                            local_best = (v, q)
            say("C from", np.round(p, 1).tolist(), "coarse %.4f" % s, "-> %.4f" % local_best[0],
                np.round(local_best[1], 3).tolist())
            results.append({"start_place": list(map(float, p)), "coarse_score": float(s), "score": float(local_best[0]),
                            "pose": local_best[1].tolist()})
            if local_best[0] > best[0]:
                best = local_best
            time.sleep(1.0)
        # (a last local polish from the best, smaller steps)
        x = best[1]
        scale = np.array([0.6, 0.6, 0.3, 1.0, 1.0, 0.7, 1.5, 0.02])
        es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {"CMA_stds": scale, "maxfevals": arguments.evals,
                                                          "popsize": 10, "verbose": -9, "seed": 2})
        with game.burst():
            while not es.stop():
                candidates = es.ask()
                params = [np.array(x) + c for c in candidates]
                scores = [evaluate(q) for q in params]
                es.tell(candidates, [-v for v in scores])
                for v, q in zip(scores, params):
                    if v > best[0]:
                        best = (v, q)
        say("C polish -> %.4f" % best[0], np.round(best[1], 3).tolist())
        timings["C_refine_seconds"] = time.time() - t
        timings["C_renders"] = renders
        timings["search_seconds_total"] = time.time() - t_start
        timings["gpu_lock_seconds"] = game.lock_held

        x, y, z, yaw, pitch, roll, vfov, aspect = [float(v) for v in best[1]]
        with game.burst():
            frame = game.render((x, y, z, yaw, pitch, roll, vfov), settle=3)
        picture = frame_to_picture(frame, aspect, PICTURE)
        Image.fromarray(picture).save(out / "best_small.png")
        s, parts = rr_view.score(target_full, rr_view.features(masked(picture), PICTURE))
        result = {
            "scenario": arguments.scenario,
            "target": arguments.target,
            "picture": {"width": PICTURE[0], "height": PICTURE[1], "source_aspect": aspect, "masks": MASKS},
            "pose": {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch, "roll": roll, "vfov": vfov,
                     "game_fov": math.degrees(rr_view.fov_from_vfov(vfov))},
            "score": float(s), "score_parts": {"gray_rank": float(parts[0]), "edge_strength": float(parts[1]),
                                               "edge_direction": float(parts[2])},
            "ssim_gray": float(rr_view.ssim_gray(target, picture)),
            "starts": results,
            "timings": timings,
        }
        (out / "pose.json").write_text(json.dumps(result, indent=1))
        say(json.dumps(result["pose"]), "score %.4f" % s, json.dumps(timings))
    finally:
        game.close()


if __name__ == "__main__":
    main()
