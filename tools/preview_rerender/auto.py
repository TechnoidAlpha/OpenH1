#!/usr/bin/env python3
"""The pose search with its places taken from the map itself: for every
structure (BSP) of the scenario, the places debug_scenario_places writes
(player starts, cutscene flags and cameras, AI firing positions and starts),
thinned, each at an eye height above it; then rr_search's steps A (panoramas),
B (orientation from them) and C (CMA-ES, rendered) over all structures.

    auto.py --scenario 'levels\\a10\\a10' --target originals/sp_levels_00.png --out a10
"""
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
import search as S  # noqa: E402

EYE = 0.62
# (the game halts on a field of view past 90 degrees across at 4:3: 72.9 tall)
VFOV_MAX = 72.0  # (a standing player's eye above the ground, world units)


def read_places(path):
    lines = Path(path).read_text().splitlines()
    head = lines[0].split()
    out = []
    for line in lines[1:]:
        p = line.split()
        # (cameras outside the structure and placed objects are not places to
        # stand: seeds of their own, read by seeds.py)
        if p[0] in ("camera_outside", "object"):
            continue
        out.append((p[0], *map(float, p[1:8])))
    return int(head[1]), int(head[3]), out


def thin(points, spacing, cap):
    """points at least spacing apart, the first kept first (cameras, then
    starts, then the rest), at most cap"""
    kept = []
    for p in points:
        if all((p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + ((p[2] - q[2]) * 2) ** 2 >= spacing ** 2 for q in kept):
            kept.append(p)
        if len(kept) >= cap:
            break
    return kept


def _init_worker(target, masks, pitches, clip):
    S.PITCHES = pitches
    S.PITCH_CLIP = clip
    S._init_worker(target, masks)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--target", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--spacing", type=float, default=0.8)
    ap.add_argument("--cap", type=int, default=120, help="places per structure")
    ap.add_argument("--heights", nargs="+", type=float, default=[EYE, 2.0], help="above each place")
    ap.add_argument("--bsps", nargs="*", type=int, default=None)
    ap.add_argument("--top", type=int, default=4)
    ap.add_argument("--evals", type=int, default=240)
    ap.add_argument("--jobs", type=int, default=12)
    ap.add_argument("--port", type=int, default=23481)
    ap.add_argument("--wait-frames", type=int, default=400)
    ap.add_argument("--mask", nargs=4, type=float, action="append", default=[])
    ap.add_argument("--prepare", action="append", default=[])
    ap.add_argument("--up", type=float, default=None,
                    help="a second ring of views at this pitch, and pitches up to it searched (a picture looking up)")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    target = S.load_target(a.target)
    Image.fromarray(target).save(out / "target.png")
    S.MASKS[:] = a.mask
    target_full = rr_view.features(S.masked(target), S.PICTURE)
    log = open(out / "search_log.txt", "a")
    timings = {}

    def say(*parts):
        line = " ".join(str(p) for p in parts)
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    from game import Game
    t0 = time.time()
    G = [None]
    current_bsp = [0]
    loaded_bsp = [0]
    starts_made = [0]

    def start_game():
        """the game started afresh (at the scenario's first structure)"""
        if G[0] is not None:
            G[0].close()
        G[0] = Game(out / "run", a.scenario, *S.WINDOW, port=a.port + 2 * starts_made[0])
        starts_made[0] += 1
        G[0].connect()
        G[0].wait_frames(a.wait_frames, timeout=600)
        G[0].prepare_scene()
        for line in a.prepare:
            say("prepare:", line, G[0].command(line, frames=3).strip())
        current_bsp[0] = loaded_bsp[0]

    def to_bsp(b):
        """the structure b loaded: switched to once from the first, else the
        game started again (switching to and fro used up the device groups,
        and the game halted)"""
        if b == current_bsp[0]:
            return
        if current_bsp[0] != loaded_bsp[0]:
            start_game()
        if b != current_bsp[0]:
            G[0].command("switch_bsp %d" % b, frames=30)
            current_bsp[0] = b
            G[0].render((0, 0, 0, 0, 0, 0, 50), settle=4)

    class _Game:
        def __getattr__(self, name):
            return getattr(G[0], name)
    game = _Game()

    try:
        start_game()
        timings["load_seconds"] = time.time() - t0

        # ----- the places, every structure
        game.command('debug_scenario_places "places_0.txt"', frames=4)
        first, count, _ = read_places(out / "run" / "places_0.txt")
        current_bsp[0] = loaded_bsp[0] = first
        bsps = a.bsps if a.bsps is not None else list(range(count))
        places = []  # (bsp, x, y, z)
        cache = out / "places.json"
        if cache.exists():
            places = [tuple(p) for p in json.loads(cache.read_text())]
            say("places from the cache:", len(places))
        else:
            for b in bsps:
                with game.burst():
                    to_bsp(b)
                    game.command('debug_scenario_places "places_%d.txt"' % b, frames=4)
                _, _, raw = read_places(out / "run" / ("places_%d.txt" % b))
                order = {"camera": 0, "player": 1, "flag": 2, "actor": 3, "firing": 4}
                raw.sort(key=lambda p: order.get(p[0], 9))
                ground = [(p[1], p[2], p[3]) for p in raw if p[0] != "camera"]
                cams = [(p[1], p[2], p[3]) for p in raw if p[0] == "camera"]
                # (a spread over the whole structure: random order after the starts)
                rng = np.random.default_rng(b)
                ground = ground[:0] + [ground[i] for i in rng.permutation(len(ground))]
                # (the spacing grown until the structure's places fit the cap:
                # small rooms dense, open ground sparser)
                spacing = a.spacing
                while True:
                    chosen = thin(cams + ground, spacing, 10 ** 6)
                    if len(chosen) <= a.cap:
                        break
                    spacing *= 1.15
                say(f"structure {b}: spacing {spacing:.2f}")
                n0 = len(places)
                for p in chosen:
                    if p in cams:
                        places.append((b, p[0], p[1], p[2]))
                    else:
                        for h in a.heights:
                            places.append((b, p[0], p[1], p[2] + h))
                say(f"structure {b}: {len(raw)} places written, {len(places) - n0} searched")
            cache.write_text(json.dumps(places))

        # ----- A: panoramas
        t = time.time()
        pano_cache = out / "panoramas.npz"
        yaws = [360.0 * k / S.PANO_COUNT for k in range(S.PANO_COUNT)]
        rings = [0.0] + ([a.up] if a.up else [])
        ring = [(yaw, pitch) for pitch in rings for yaw in yaws]
        panoramas = []
        if pano_cache.exists():
            images = np.load(pano_cache)["images"]
            panoramas = [[(images[i, k], ring[k][0], ring[k][1], S.PANO_VFOV) for k in range(len(ring))]
                         for i in range(len(places))]
            say("A: from the cache")
        else:
            game.unlock()
            for start in range(0, len(places), 40):
                with game.burst():
                    for b, x, y, z in places[start:start + 40]:
                        to_bsp(b)
                        views = []
                        for yaw, pitch in ring:
                            frame = game.render((x, y, z, yaw, pitch, 0, S.PANO_VFOV))
                            views.append((cv2.resize(frame, (512, 288), interpolation=cv2.INTER_AREA), yaw, pitch,
                                          S.PANO_VFOV))
                        panoramas.append(views)
                time.sleep(0.5)
            np.savez(pano_cache, images=np.array([[v[0] for v in p] for p in panoramas]))
        timings["A_panorama_seconds"] = time.time() - t
        timings["A_places"] = len(places)
        say("A done", round(timings["A_panorama_seconds"], 1), "s,", len(places), "places")

        # ----- B
        t = time.time()
        pitches = (-8, 0, 8) + ((tuple(np.arange(16, a.up + 1, 8).tolist())) if a.up else ())
        clip = max(25, (a.up or 0) + 10)
        with ProcessPoolExecutor(a.jobs, initializer=_init_worker,
                                 initargs=(target, S.MASKS, pitches, clip)) as pool:
            coarse = {i: tuple(float(v) for v in r) for i, r in pool.map(S.orient, enumerate(panoramas), chunksize=2)}
        ranked = sorted(((coarse[i][0], places[i], coarse[i][1:]) for i in range(len(places))), reverse=True)
        timings["B_orientation_seconds"] = time.time() - t
        (out / "coarse.json").write_text(json.dumps(
            [{"score": s, "place": list(map(float, p)), "yaw_pitch_vfov": list(map(float, o))} for s, p, o in ranked],
            indent=1))
        say("B done", round(timings["B_orientation_seconds"], 1), "s; best:")
        for s, p, o in ranked[:10]:
            say("  %.4f" % s, np.round(p, 2).tolist(), np.round(o, 1).tolist())
        del panoramas

        # ----- C
        t = time.time()
        renders = 0

        def evaluate(params):
            nonlocal renders
            x, y, z, yaw, pitch, roll, vfov, aspect = params
            vfov = min(vfov, VFOV_MAX)
            frame = game.render((x, y, z, yaw, pitch, roll, vfov))
            renders += 1
            picture = S.frame_to_picture(frame, aspect, S.PICTURE)
            # (a nearly blank frame, inside a wall or facing the dark, is no match)
            if rr_view.gray(picture).std() < 6:
                return 0.0
            return rr_view.score(target_full, rr_view.features(S.masked(picture), S.PICTURE))[0]

        starts = []
        for s, p, o in ranked:
            if all(p[0] != q[0] or np.linalg.norm(np.array(p[1:3]) - np.array(q[1:3])) > a.spacing * 1.5
                   for _, q, _ in starts):
                starts.append((s, p, o))
            if len(starts) == a.top:
                break
        best = (-1, None, None)
        results = []
        for s, p, o in starts:
            b = int(p[0])
            x = [p[1], p[2], p[3], o[0], o[1], 0.0, o[2], S.PICTURE[0] / S.PICTURE[1]]
            scale = np.array([1.2, 1.2, 0.6, 4, 3, 2, 5, 0.06])
            pmax = max(30, (a.up or 0) + 15)
            lo = [None, None, None, None, -pmax - x[4], -15, 15 - x[6], 1.12 - x[7]]
            hi = [None, None, None, None, pmax - x[4], 15, VFOV_MAX - x[6], 1.42 - x[7]]
            es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
                "CMA_stds": scale, "bounds": [lo, hi], "maxfevals": a.evals, "popsize": 12,
                "verbose": -9, "seed": 1})
            local = (-1, None)
            with game.burst():
                to_bsp(b)
                while not es.stop():
                    cands = es.ask()
                    params = [np.array(x) + c for c in cands]
                    scores = [evaluate(q) for q in params]
                    es.tell(cands, [-v for v in scores])
                    for v, q in zip(scores, params):
                        if v > local[0]:
                            local = (v, q)
            say("C from", np.round(p, 1).tolist(), "coarse %.4f" % s, "-> %.4f" % local[0],
                np.round(local[1], 3).tolist())
            results.append({"bsp": b, "start_place": list(map(float, p[1:])), "coarse_score": float(s),
                            "score": float(local[0]), "pose": local[1].tolist()})
            if local[0] > best[0]:
                best = (local[0], local[1], b)
            time.sleep(0.5)
        x = best[1]
        es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
            "CMA_stds": np.array([0.4, 0.4, 0.2, 1.0, 1.0, 0.7, 1.5, 0.02]), "maxfevals": a.evals,
            "popsize": 10, "verbose": -9, "seed": 2})
        with game.burst():
            to_bsp(best[2])
            while not es.stop():
                cands = es.ask()
                params = [np.array(x) + c for c in cands]
                scores = [evaluate(q) for q in params]
                es.tell(cands, [-v for v in scores])
                for v, q in zip(scores, params):
                    if v > best[0]:
                        best = (v, q, best[2])
        say("C polish -> %.4f" % best[0], np.round(best[1], 3).tolist())
        timings["C_refine_seconds"] = time.time() - t
        timings["C_renders"] = renders
        timings["search_seconds_total"] = time.time() - t0
        timings["gpu_lock_seconds"] = game.lock_held

        x, y, z, yaw, pitch, roll, vfov, aspect = [float(v) for v in best[1]]
        vfov = min(vfov, VFOV_MAX)
        with game.burst():
            frame = game.render((x, y, z, yaw, pitch, roll, vfov), settle=3)
        picture = S.frame_to_picture(frame, aspect, S.PICTURE)
        Image.fromarray(picture).save(out / "best_small.png")
        s, parts = rr_view.score(target_full, rr_view.features(S.masked(picture), S.PICTURE))
        result = {
            "scenario": a.scenario, "target": a.target, "bsp": best[2],
            "picture": {"width": S.PICTURE[0], "height": S.PICTURE[1], "source_aspect": aspect, "masks": S.MASKS},
            "pose": {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch, "roll": roll, "vfov": vfov,
                     "game_fov": math.degrees(rr_view.fov_from_vfov(vfov))},
            "score": float(s), "score_parts": dict(zip(("gray_rank", "edge_strength", "edge_direction"),
                                                       map(float, parts))),
            "ssim_gray": float(rr_view.ssim_gray(target, picture)),
            "starts": results, "timings": timings,
        }
        (out / "pose.json").write_text(json.dumps(result, indent=1))
        say(json.dumps(result["pose"]), "bsp", best[2], "score %.4f" % s)
    finally:
        game.close()


if __name__ == "__main__":
    main()
