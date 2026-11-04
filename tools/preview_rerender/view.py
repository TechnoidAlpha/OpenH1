"""Camera math and image scoring for the re-render search.

Pose: x, y, z (world units), yaw, pitch, roll (degrees), vfov (vertical
field of view, degrees). The game's own number is its observer field of view
(radians), horizontal at 4:3; the port keeps the vertical angle for other
window shapes, so vfov = 2 atan(tan(fov/2) * 3/4) is the same at any window
shape."""
import math

import numpy as np
import cv2


def fov_from_vfov(vfov_deg):
    return 2 * math.atan(math.tan(math.radians(vfov_deg) / 2) * 4 / 3)


def vfov_from_fov(fov_rad):
    return math.degrees(2 * math.atan(math.tan(fov_rad / 2) * 3 / 4))


def basis(yaw, pitch, roll):
    """forward, up, right (z up), angles in degrees"""
    yaw, pitch, roll = map(math.radians, (yaw, pitch, roll))
    cp, sp, cy, sy = math.cos(pitch), math.sin(pitch), math.cos(yaw), math.sin(yaw)
    forward = np.array([cy * cp, sy * cp, sp])
    up0 = np.array([-cy * sp, -sy * sp, cp])
    right0 = np.cross(forward, up0)
    up = math.cos(roll) * up0 + math.sin(roll) * right0
    right = np.cross(forward, up)
    return forward, up, right


def crop_aspect(image, aspect):
    """the centre of an image cut to width/height = aspect"""
    h, w = image.shape[:2]
    if w / h > aspect:
        cw = int(round(h * aspect))
        x = (w - cw) // 2
        return image[:, x:x + cw]
    ch = int(round(w / aspect))
    y = (h - ch) // 2
    return image[y:y + ch]


# ---------- views from one place, made into others

def pano_view(views, yaw, pitch, roll, vfov, width, height):
    """a view (RGB float) at this orientation made from whole frames taken
    from the same place: (image, yaw, pitch, vfov) each, no roll; each pixel
    from the frame it is nearest the centre of"""
    forward, up, right = basis(yaw, pitch, roll)
    ty = math.tan(math.radians(vfov) / 2)
    tx = ty * width / height
    xs = (np.arange(width) + 0.5) / width * 2 - 1
    ys = 1 - (np.arange(height) + 0.5) / height * 2
    X, Y = np.meshgrid(xs * tx, ys * ty)
    rays = forward[None, None] + X[..., None] * right[None, None] + Y[..., None] * up[None, None]
    rays /= np.linalg.norm(rays, axis=2, keepdims=True)
    out = np.zeros((height, width, 3), np.float32)
    best = np.full((height, width), -np.inf)
    for image, vyaw, vpitch, vvfov in views:
        f, u, r = basis(vyaw, vpitch, 0)
        h, w = image.shape[:2]
        vty = math.tan(math.radians(vvfov) / 2)
        vtx = vty * w / h
        d = rays @ f
        su = (rays @ r) / np.maximum(d, 1e-6) / vtx
        sv = (rays @ u) / np.maximum(d, 1e-6) / vty
        inside = (d > 0) & (np.abs(su) <= 1) & (np.abs(sv) <= 1) & (d > best)
        if not inside.any():
            continue
        mx = ((su + 1) / 2 * w - 0.5).astype(np.float32)
        my = ((1 - sv) / 2 * h - 0.5).astype(np.float32)
        sampled = cv2.remap(image.astype(np.float32), mx, my, cv2.INTER_LINEAR, borderMode=cv2.BORDER_REPLICATE)
        out[inside] = sampled[inside]
        best = np.where(inside, d, best)
    return out


# ---------- scores (higher is better, 1 a perfect match)

def gray(image):
    image = image.astype(np.float32)
    return image[..., 0] * 0.299 + image[..., 1] * 0.587 + image[..., 2] * 0.114


def normalize(a):
    a = a - a.mean()
    return a / (np.sqrt((a * a).mean()) + 1e-6)


def features(image, size):
    """what a view is compared by, at size (w, h): its grey, equalized and
    blurred a little; its gradient magnitude; its gradient's direction"""
    g = cv2.resize(gray(image), size, interpolation=cv2.INTER_AREA)
    g = cv2.GaussianBlur(g, (0, 0), max(0.6, size[0] / 140))
    gx = cv2.Sobel(g, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(g, cv2.CV_32F, 0, 1, ksize=3)
    mag = np.sqrt(gx * gx + gy * gy)
    # (equalize by rank: robust to the original's colour and contrast)
    flat = g.ravel()
    ranks = np.empty_like(flat)
    ranks[np.argsort(flat, kind="stable")] = np.linspace(0, 1, flat.size)
    return {"rank": normalize(ranks.reshape(g.shape)), "mag": normalize(np.sqrt(mag)),
            "gx": gx, "gy": gy, "magraw": mag}


def score(target, view):
    """mean of: correlation of the equalized greys, correlation of the edge
    strengths, agreement of the edge directions (weighted)"""
    a = (target["rank"] * view["rank"]).mean()
    b = (target["mag"] * view["mag"]).mean()
    w = target["magraw"] * view["magraw"]
    dot = (target["gx"] * view["gx"] + target["gy"] * view["gy"]) / (target["magraw"] * view["magraw"] + 1e-6)
    c = float((w * dot * dot).sum() / (w.sum() + 1e-6))  # cos^2: edge sign may flip with lighting
    return (a + b + c) / 3, (a, b, c)


def ssim_gray(a, b):
    from skimage.metrics import structural_similarity
    return structural_similarity(gray(a), gray(b), data_range=255)
