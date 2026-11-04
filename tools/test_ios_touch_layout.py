#!/usr/bin/env python3
"""Checks the iOS touch controls' default layout (port/ios/touch_controls.m)
against the game's HUD on the screens the app runs on: no control, nor the
toolbar, covers the HUD's places (the ammunition and grenades at the upper
left, the messages under them, the shields at the upper right, the motion
tracker at the lower left), and each lies inside the screen's safe area.

    python -m pytest -q tools/test_ios_touch_layout.py
    python tools/test_ios_touch_layout.py          # the same, as a report

The layout is read from the file's DEFAULTS and RADII and placed as the view
places it (its layoutControls: the 960 x 540 grid scaled to the safe area's
height, its places stretched to the safe area's width, a radius at least a
finger's). The HUD is drawn over the whole screen, its 480 lines widened to
the screen's shape, each part at the edge it belongs to (port/android/README.md,
"Widescreen"); its places below are in those lines, measured from the game's
frames at 19.5:9, with a margin.
"""

import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/ios/touch_controls.m"

GRID_WIDTH, GRID_HEIGHT = 960.0, 540.0
# the lines a taller display adds its height between (touch_controls.m's TOP_ROW and LOWER_PART)
TOP_ROW, LOWER_PART = (float(re.search(rf"#define {name} ([0-9.]+)f", SOURCE.read_text()).group(1))
                       for name in ("TOP_ROW", "LOWER_PART"))
MINIMUM_RADIUS_POINTS = 24.0
MAX_MINIMUM_RADIUS = 34.0
STICK = 16
HIDDEN = {17}  # the second fire, hidden by default
TOOLBAR_TOGGLE_RADIUS, OPTIONS_RADIUS = 28.0, 34.0

# (name, edge the part is drawn from, x0, x1, y0, y1) in the HUD's lines: x
# from the left edge ("left") or the right edge ("right"), y from the top
HUD = [
    ("ammunition and grenades", "left", 70, 205, 25, 82),
    ("messages", "left", 70, 340, 88, 126),
    ("shields and health", "right", 75, 215, 25, 82),
    ("motion tracker", "left", 66, 175, 350, 455),
]

# (name, screen width and height in points, safe area's left and right insets)
SCREENS = [
    ("iPhone 15 Pro Max (19.5:9)", 932, 430, 59, 59),
    ("iPhone 15 (19.5:9)", 852, 393, 59, 59),
    ("iPhone SE (16:9)", 667, 375, 0, 0),
    ("iPad (4:3)", 1024, 768, 0, 0),
    ("iPad 10.2-inch (4:3)", 1080, 810, 0, 0),
    ("iPad mini (1.52)", 1133, 744, 0, 0),
    ("iPad and iPad Air 11-inch (1.43)", 1180, 820, 0, 0),
    ("iPad Pro 11-inch (1.43)", 1194, 834, 0, 0),
    ("iPad Pro 11-inch M4 (1.45)", 1210, 834, 0, 0),
    ("iPad Air 13-inch, iPad Pro 12.9-inch (4:3)", 1366, 1024, 0, 0),
    ("iPad Pro 13-inch M4 (4:3)", 1376, 1032, 0, 0),
]


def read_layout():
    text = SOURCE.read_text(encoding="utf-8")
    defaults = re.search(r"DEFAULTS\[BASE_COUNT\]\[2\] =\s*\{(.*?)\};", text, re.S).group(1)
    radii = re.search(r"RADII\[BASE_COUNT\] =\s*\{(.*?)\};", text, re.S).group(1)
    places = [tuple(float(v) for v in pair) for pair in re.findall(r"\{\s*([\d.]+),\s*([\d.]+)\s*\}", defaults)]
    sizes = [float(v) for v in re.findall(r"[\d.]+", radii)]
    toolbar = re.search(r"- \(float\)optionsX\s*\{\s*return (.*?);", text, re.S).group(1)
    return places, sizes, toolbar


def display_y(line, height):
    """a line of the grid on a display this high (touch_controls.m's display_y): the top row keeps its distance
    from the top of a taller display, the rest its distance from the bottom"""
    extra = height - GRID_HEIGHT
    if extra <= 0:
        return line * height / GRID_HEIGHT
    if line <= TOP_ROW:
        return line
    if line >= LOWER_PART:
        return line + extra
    return TOP_ROW + (line - TOP_ROW) * (LOWER_PART - TOP_ROW + extra) / (LOWER_PART - TOP_ROW)


def placed(screen, places, radii, toolbar):
    """each control's centre and radius, and the toolbar's, in screen points"""
    _, width, height, left, right = screen
    safe_width = width - left - right
    scale = max(0.01, min(safe_width / GRID_WIDTH, height / GRID_HEIGHT))
    logical_width, logical_height = safe_width / scale, height / scale
    minimum = min(MINIMUM_RADIUS_POINTS / scale, MAX_MINIMUM_RADIUS)
    circles = []
    for index, ((x, y), radius) in enumerate(zip(places, radii)):
        if index in HIDDEN:
            continue
        radius = max(radius, minimum)
        x = x * logical_width / GRID_WIDTH
        y = display_y(y, logical_height)
        x = max(radius, min(logical_width - radius, x))
        y = max(radius, min(logical_height - radius, y))
        circles.append((f"control {index}", left + x * scale, y * scale, radius * scale))
    toolbar_y = 38.0
    options_x = eval(toolbar.replace("logical_width", str(logical_width)).replace("f", ""))  # noqa: S307
    circles.append(("hide toggle", left + logical_width / 2 * scale, toolbar_y * scale,
                    max(TOOLBAR_TOGGLE_RADIUS, minimum) * scale))
    circles.append(("options", left + options_x * scale, toolbar_y * scale, max(OPTIONS_RADIUS, minimum) * scale))
    return circles


def hud_rects(screen):
    _, width, height, _, _ = screen
    line = height / 480.0
    rects = []
    for name, edge, x0, x1, y0, y1 in HUD:
        if edge == "left":
            rects.append((name, x0 * line, x1 * line, y0 * line, y1 * line))
        else:
            rects.append((name, width - x1 * line, width - x0 * line, y0 * line, y1 * line))
    return rects


def overlaps(circle, rect):
    _, cx, cy, radius = circle
    _, x0, x1, y0, y1 = rect
    nearest_x = min(max(cx, x0), x1)
    nearest_y = min(max(cy, y0), y1)
    return math.hypot(cx - nearest_x, cy - nearest_y) < radius


def problems():
    places, radii, toolbar = read_layout()
    found = []
    for screen in SCREENS:
        _, width, height, left, right = screen
        circles = placed(screen, places, radii, toolbar)
        for circle in circles:
            name, cx, cy, radius = circle
            if cx - radius < left - 0.5 or cx + radius > width - right + 0.5 or cy - radius < -0.5 or \
                    cy + radius > height + 0.5:
                found.append(f"{screen[0]}: {name} leaves the safe area")
            for rect in hud_rects(screen):
                if overlaps(circle, rect):
                    found.append(f"{screen[0]}: {name} covers the HUD's {rect[0]}")
        for i, a in enumerate(circles):
            for b in circles[i + 1:]:
                if math.hypot(a[1] - b[1], a[2] - b[2]) < (a[3] + b[3]) * 0.9:
                    found.append(f"{screen[0]}: {a[0]} and {b[0]} overlap")
    return found


def test_defaults_clear_the_hud():
    assert problems() == []


if __name__ == "__main__":
    found = problems()
    print("\n".join(found) if found else "the default layout clears the HUD and the safe area on every screen")
    sys.exit(1 if found else 0)
