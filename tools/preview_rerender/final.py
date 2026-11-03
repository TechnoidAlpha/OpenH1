#!/usr/bin/env python3
"""Renders a found pose at 3840x2160, cut to the picture's shape, and its
colours matched to the original; writes the comparisons.

    final.py <out folder with pose.json> [--size 3840x2160]"""
import argparse
import json
import sys
import time
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw
from skimage.exposure import match_histograms

sys.path.insert(0, str(Path(__file__).parent))
import view as rr_view  # noqa: E402
from game import Game  # noqa: E402


def curve_match(source, reference, smooth=9):
    """a smooth per-channel tone curve (quantile mapping, smoothed and kept
    rising) taking the source's colours toward the reference's"""
    out = np.empty_like(source)
    q = np.linspace(0, 1, 257)
    for c in range(3):
        sq = np.quantile(source[..., c], q)
        rq = np.quantile(reference[..., c], q)
        sq, keep = np.unique(sq, return_index=True)
        rq = rq[keep]
        rq = np.convolve(np.pad(rq, smooth // 2, mode="edge"), np.ones(smooth) / smooth, mode="valid")
        rq = np.maximum.accumulate(rq)
        lut = np.interp(np.arange(256), sq, rq)
        out[..., c] = np.clip(lut[source[..., c]], 0, 255).astype(np.uint8)
    return out


def label(image, text):
    image = Image.fromarray(image)
    draw = ImageDraw.Draw(image)
    size = max(12, image.width // 40)
    try:
        from PIL import ImageFont
        font = ImageFont.load_default(size=size)
    except Exception:
        font = None
    draw.rectangle((0, 0, image.width, size + 12), fill=(0, 0, 0))
    draw.text((8, 4), text, fill=(255, 255, 255), font=font)
    return np.asarray(image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    parser.add_argument("--size", default="3840x2160")
    parser.add_argument("--port", type=int, default=23472)
    parser.add_argument("--wait-frames", type=int, default=400)
    arguments = parser.parse_args()
    out = Path(arguments.out)
    found = json.loads((out / "pose.json").read_text())
    width, height = map(int, arguments.size.split("x"))
    p = found["pose"]
    aspect = found["picture"]["source_aspect"]
    target = np.asarray(Image.open(out / "target.png").convert("RGB"))

    t0 = time.time()
    game = Game(out / "run_final", found["scenario"], width, height, port=arguments.port)
    try:
        game.connect()
        game.wait_frames(arguments.wait_frames, timeout=600)
        game.prepare_scene()
        # (always: a map may start in another structure than its first)
        if found.get("bsp") is not None:
            game.command("switch_bsp %d" % found["bsp"], frames=30)
            game.render((0, 0, 0, 0, 0, 0, 50), settle=4)
        # (a picture's own console lines: the map's scripted state put back)
        for line in found.get("prepare", []):
            print("prepare:", line, game.command(line, frames=3).strip())
        game.place_objects(found.get("objects", []), settle_seconds=found.get("settle_seconds", 3.0))
        load = time.time() - t0
        t = time.time()
        frame = game.render((p["x"], p["y"], p["z"], p["yaw"], p["pitch"], p["roll"], p["vfov"]), settle=3)
        render = time.time() - t
    finally:
        game.close()
    raw = rr_view.crop_aspect(frame, aspect)
    # (the picture's pixel shape: a source wider than 140:114 was squeezed)
    final_w = raw.shape[1]
    final_h = int(round(final_w * target.shape[0] / target.shape[1]))
    raw = cv2.resize(raw, (final_w, final_h), interpolation=cv2.INTER_AREA)
    t = time.time()
    small = cv2.resize(raw, (target.shape[1], target.shape[0]), interpolation=cv2.INTER_AREA)
    # the colours matched: a smooth curve from the picture-sized render to the
    # original, applied at full size; and plain histogram matching
    curved = curve_match(raw, target)
    histogram = match_histograms(raw, cv2.resize(target, (final_w, final_h), interpolation=cv2.INTER_CUBIC),
                                 channel_axis=-1).clip(0, 255).astype(np.uint8)
    colour = time.time() - t
    original_up = cv2.resize(target, (final_w, final_h), interpolation=cv2.INTER_LANCZOS4)
    Image.fromarray(raw).save(out / "render_4k_raw.png")
    Image.fromarray(curved).save(out / "render_4k_colour_curve.png")
    Image.fromarray(histogram).save(out / "render_4k_colour_histogram.png")
    Image.fromarray(original_up).save(out / "original_upscaled.png")
    Image.fromarray(frame).save(out / "render_full_frame_3840x2160.png")
    # side by side at a viewable size
    view_h = 720
    view_w = int(round(view_h * final_w / final_h))

    def v(image):
        return cv2.resize(image, (view_w, view_h), interpolation=cv2.INTER_AREA)
    gap = np.zeros((view_h, 12, 3), np.uint8)
    side = np.concatenate([label(v(original_up), "original (140x114, upscaled)"), gap,
                           label(v(raw), "re-render, raw"), gap,
                           label(v(curved), "re-render, colour curve")], 1)
    Image.fromarray(side).save(out / "compare_side_by_side.png")
    # edges of the original over the render
    edges = cv2.Canny(cv2.cvtColor(v(original_up), cv2.COLOR_RGB2GRAY), 40, 110)
    overlay = v(curved).copy()
    overlay[edges > 0] = (255, 40, 40)
    Image.fromarray(overlay).save(out / "compare_edges_overlay.png")
    # at the original's size, both
    Image.fromarray(np.concatenate([target, np.zeros((target.shape[0], 4, 3), np.uint8),
                                    curve_match(small, target)], 1)).resize(
        (2 * (target.shape[1] * 2 + 4), target.shape[0] * 4), Image.NEAREST).save(out / "compare_at_original_size.png")
    fs = rr_view.features(target, (140, 114))
    scores = {}
    for name, image in (("raw", small), ("colour_curve", curve_match(small, target))):
        s, parts = rr_view.score(fs, rr_view.features(image, (140, 114)))
        scores[name] = {"score": float(s), "ssim_gray": float(rr_view.ssim_gray(target, image)),
                        "mean_abs_rgb": float(np.abs(image.astype(int) - target.astype(int)).mean())}
    found["final"] = {"window": [width, height], "picture_pixels": [final_w, final_h],
                      "load_seconds": load, "render_seconds": render, "colour_seconds": colour, "scores": scores}
    (out / "pose.json").write_text(json.dumps(found, indent=1))
    print(json.dumps(found["final"], indent=1))


if __name__ == "__main__":
    main()
