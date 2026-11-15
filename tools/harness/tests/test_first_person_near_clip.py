"""The first-person weapon's near clip plane (port/linux/game/near_clip.c's render_first_person_near_clip_distance):
the game's own at the weapon's stock angle and place, so the default is drawn as ever; nearer where its projection is
widened (display.viewmodel_fov, display.panini's magnification), its farthest corner as far from the eye as the stock
angle's, so the weapon's rear brought onto the screen at a wide view is not cut (#604); nearer by as much where the
weapon is moved toward the eye (display.viewmodel_offset_z). And where it is wired: every pass that draws the weapon,
the weapon's move recorded, not the VR mode's."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["stock", "narrower", "widened", "tall", "split_screen", "moved", "moved_widened", "script"]
CONTROLS = [
    # the drawn angle's corner taken for the stock one's: never nearer
    ("near_clip_corner(camera, frustum_bounds, stock) /", "near_clip_corner(camera, frustum_bounds, drawn) /",
     "widened"),
    # the tangents' ratio, not the corners': the corner's distance not kept
    ("near_clip_corner(camera, frustum_bounds, stock) /\n\t\t\tnear_clip_corner(camera, frustum_bounds, drawn);",
     "stock / drawn;", "widened"),
    # widened where it is narrower
    ("drawn > stock", "drawn < stock", "widened"),
    # a narrower projection's too: the plane farther than the game's
    ("drawn > stock", "drawn != stock", "narrower"),
    # the move toward the eye left out
    ("if (forward_offset < 0.0f)", "if (0)", "moved"),
    # no least: the plane behind the eye
    ("MAX(z_near + forward_offset, z_near * FIRST_PERSON_NEAR_CLIP_LEAST)", "z_near + forward_offset", "moved"),
    # the move's, not the nearer of the two
    ("if (moved < result)\n\t\t\tresult = moved;", "result = moved;", "moved_widened"),
    # the plane's top and bottom only, not its sides
    ("1.0 + (double)across * across + (double)up * up", "1.0 + (double)across * 0.0 + (double)up * up", "widened"),
    # the top's left out on a portrait screen
    ("(double)up * up", "(double)up * up * 0.0", "tall"),
    # the bounds' half width, not their farther side: a view off its window's middle
    ("MAX(fabsf(frustum_bounds->x0), fabsf(frustum_bounds->x1))",
     "(frustum_bounds->x1 - frustum_bounds->x0) * 0.5f", "split_screen"),
    # a script's distance replaced by the game's
    ("real result = z_near;", "real result = 0.01171875f;", "script"),
]


def generated(fault=None):
    code = read("port/linux/game/near_clip.c")
    text = "\n".join(re.findall(r"^#define \w*NEAR_CLIP_\w+ .*$", code, re.M)) + "\n" + \
        function(code, "near_clip_corner") + "\n" + function(code, "render_first_person_near_clip_distance")
    if fault:
        text = mutated(text, *fault)
    bounds = function(read("source/render/render_cameras.c"), "render_camera_build_frustum_bounds")
    return (("under_test.inc", bounds + "\n" + text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("first_person_near_clip", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("first_person_near_clip", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_constants():
    # the test's game distance and world unit are the code's
    assert re.search(r"rasterizer_global_defaults =\s*\{\s*0\.0625f,\s*1024\.f,\s*0\.01171875f,",
                     read("source/rasterizer/rasterizer.c"))
    assert "#define CENTIMETERS_PER_WORLD_UNIT 304.8" in read("port/linux/game/view_fov.c")
    assert "#define FIRST_PERSON_NEAR_CLIP_LEAST (1.0f / 64.0f)" in read("port/linux/game/near_clip.c")
