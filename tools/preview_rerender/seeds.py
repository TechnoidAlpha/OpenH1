#!/usr/bin/env python3
"""The pose search from chosen seeds: views named outright (a cutscene's
cameras, views around a placed object such as a crashed dropship) instead of
the places a player stands, for pictures taken where no one walks.

    seeds.py places --scenario S --out dir
        every structure's places, cameras (also those outside the structure)
        and placed objects, written to dir/places_<bsp>.txt
    seeds.py search --scenario S --target T --out dir --seeds seeds.json
        [--top 6] [--evals 300] [--prepare LINE ...] [--objects objects.json]
        [--no-fog]
        seeds.json: [{"bsp": b, "pose": [x, y, z, yaw, pitch, roll, vfov]}, ...];
        each rendered and scored, then CMA-ES from the best few; writes
        pose.json as auto.py does (for final.py)

--no-fog scores with the structure's fog off (the original's fog is its own
darkness, the edges are what agree), the final render has it again."""
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

VFOV_MAX = 72.0


def start(out, scenario, port, prepare=(), objects=None, bsp=None, wait=400):
    game = Game(out / "run", scenario, *S.WINDOW, port=port)
    game.connect()
    game.wait_frames(wait, timeout=600)
    game.prepare_scene()
    if bsp is not None:
        game.command("switch_bsp %d" % bsp, frames=30)
        game.render((0, 0, 0, 0, 0, 0, 50), settle=4)
    for line in prepare:
        print("prepare:", line, game.command(line, frames=3).strip(), flush=True)
    if objects:
        print(game.place_objects(objects), flush=True)
    return game


def places(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    game = start(out, a.scenario, a.port)
    try:
        game.command('debug_scenario_places "places_first.txt"', frames=4)
    finally:
        game.close()
    head = (out / "run" / "places_first.txt").read_text().splitlines()[0].split()
    first, count = int(head[1]), int(head[3])
    (out / ("places_%d.txt" % first)).write_text((out / "run" / "places_first.txt").read_text())
    for b in range(count):
        if b == first:
            continue
        game = start(out, a.scenario, a.port + 2 * (b + 1), bsp=b)
        try:
            game.command('debug_scenario_places "places_%d.txt"' % b, frames=4)
        finally:
            game.close()
        (out / ("places_%d.txt" % b)).write_text((out / "run" / ("places_%d.txt" % b)).read_text())
    print("structures:", count, "first:", first)


def search(a):
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    target = S.load_target(a.target)
    Image.fromarray(target).save(out / "target.png")
    S.MASKS[:] = a.mask
    tf = rr_view.features(S.masked(target), S.PICTURE)
    seeds = json.loads(Path(a.seeds).read_text())
    objects = json.loads(Path(a.objects).read_text()) if a.objects else []
    log = open(out / "search_log.txt", "a")

    def say(*parts):
        line = " ".join(str(p) for p in parts)
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    prepare = list(a.prepare) + (["rasterizer_fog_atmosphere 0", "rasterizer_fog_plane 0"] if a.no_fog else [])
    by_bsp = {}
    for s in seeds:
        by_bsp.setdefault(s["bsp"], []).append(s["pose"])
    renders = [0]

    def evaluate(game, params):
        x, y, z, yaw, pitch, roll, vfov, aspect = params
        vfov = min(max(vfov, a.vfov_min), VFOV_MAX)
        frame = game.render((x, y, z, yaw, pitch, roll, vfov))
        renders[0] += 1
        picture = S.frame_to_picture(frame, aspect, S.PICTURE)
        if rr_view.gray(picture).std() < 6:
            return 0.0, picture
        return rr_view.score(tf, rr_view.features(S.masked(picture), S.PICTURE))[0], picture

    port = a.port
    best = (-1, None, None)
    results = []
    for b, poses in by_bsp.items():
        game = start(out, a.scenario, port, prepare, objects, bsp=b)
        port += 2
        try:
            # ----- every seed scored
            scored = []
            aspect0 = S.PICTURE[0] / S.PICTURE[1]
            for p in poses:
                params = list(p[:7]) + [aspect0]
                s, _ = evaluate(game, params)
                scored.append((s, params))
            scored.sort(key=lambda t: -t[0])
            say("structure", b, "seeds", len(scored), "best:",
                ["%.4f" % s for s, _ in scored[:8]])
            # ----- CMA-ES from the best few, apart from each other
            starts = []
            for s, p in scored:
                if all(np.linalg.norm(np.array(p[:3]) - np.array(q[:3])) > 1.0 or abs((p[3] - q[3] + 180) % 360 - 180) > 15
                       for _, q in starts):
                    starts.append((s, p))
                if len(starts) == a.top:
                    break
            for s, p in starts:
                x = np.array(p, float)
                scale = np.array([a.step, a.step, a.step / 2, 4, 3, 2, 5, 0.06])
                es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
                    "CMA_stds": scale, "maxfevals": a.evals, "popsize": 12, "verbose": -9, "seed": 1,
                    "bounds": [[None] * 6 + [a.vfov_min - x[6], 1.12 - x[7]], [None] * 6 + [VFOV_MAX - x[6], 1.42 - x[7]]]})
                local = (s, x)
                while not es.stop():
                    cands = es.ask()
                    params = [x + c for c in cands]
                    vals = [evaluate(game, q)[0] for q in params]
                    es.tell(cands, [-v for v in vals])
                    for v, q in zip(vals, params):
                        if v > local[0]:
                            local = (v, q)
                say("C bsp", b, "from %.4f" % s, np.round(p, 2).tolist(), "-> %.4f" % local[0],
                    np.round(local[1], 3).tolist())
                results.append({"bsp": b, "seed": list(map(float, p)), "seed_score": float(s),
                                "score": float(local[0]), "pose": [float(v) for v in local[1]]})
                if local[0] > best[0]:
                    best = (local[0], local[1], b)
        finally:
            game.close()
    # ----- the best polished, and its picture
    game = start(out, a.scenario, port, prepare, objects, bsp=best[2])
    try:
        x = best[1]
        es = cma.CMAEvolutionStrategy(np.zeros(8), 1.0, {
            "CMA_stds": np.array([0.3, 0.3, 0.15, 1.0, 1.0, 0.7, 1.5, 0.02]), "maxfevals": a.evals,
            "popsize": 10, "verbose": -9, "seed": 2})
        while not es.stop():
            cands = es.ask()
            params = [x + c for c in cands]
            vals = [evaluate(game, q)[0] for q in params]
            es.tell(cands, [-v for v in vals])
            for v, q in zip(vals, params):
                if v > best[0]:
                    best = (v, q, best[2])
        say("polish -> %.4f" % best[0], np.round(best[1], 3).tolist())
        s, picture = evaluate(game, best[1])
        Image.fromarray(picture).save(out / "best_small.png")
    finally:
        game.close()
    x, y, z, yaw, pitch, roll, vfov, aspect = [float(v) for v in best[1]]
    vfov = min(max(vfov, a.vfov_min), VFOV_MAX)
    result = {
        "scenario": a.scenario, "target": a.target, "bsp": best[2],
        "picture": {"width": S.PICTURE[0], "height": S.PICTURE[1], "source_aspect": aspect, "masks": S.MASKS},
        "pose": {"x": x, "y": y, "z": z, "yaw": yaw, "pitch": pitch, "roll": roll, "vfov": vfov,
                 "game_fov": math.degrees(rr_view.fov_from_vfov(vfov))},
        "score": float(s), "prepare": list(a.prepare), "objects": objects,
        "starts": results, "renders": renders[0],
    }
    (out / "pose.json").write_text(json.dumps(result, indent=1))
    say(json.dumps(result["pose"]), "bsp", best[2], "score %.4f" % s)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="mode", required=True)
    p = sub.add_parser("places")
    p.add_argument("--scenario", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--port", type=int, default=23601)
    p = sub.add_parser("search")
    p.add_argument("--scenario", required=True)
    p.add_argument("--target", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--seeds", required=True)
    p.add_argument("--objects", default=None)
    p.add_argument("--prepare", action="append", default=[])
    p.add_argument("--mask", nargs=4, type=float, action="append", default=[])
    p.add_argument("--top", type=int, default=6)
    p.add_argument("--evals", type=int, default=300)
    p.add_argument("--step", type=float, default=1.5)
    p.add_argument("--vfov-min", type=float, default=8.0,
                   help="the narrowest view searched (a picture known not to be zoomed far in)")
    p.add_argument("--no-fog", action="store_true")
    p.add_argument("--port", type=int, default=23641)
    a = ap.parse_args()
    {"places": places, "search": search}[a.mode](a)


if __name__ == "__main__":
    main()
