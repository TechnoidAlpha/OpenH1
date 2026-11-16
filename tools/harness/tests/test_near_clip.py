"""The near clip plane of the local players' views (port/linux/game/near_clip.c's render_near_clip_distance): the
game's own at the stock view and any whose corners are within the reach (the cyborg's collision radius), so the
default is drawn as ever; nearer at a wider view, its farthest corner at the reach, so a wall beside the player is
not cut (#604); from the game's own frustum bounds (split screen's views off their middles). And where it is wired:
the render camera and the rasterizer's, from the whole view's bounds, not the VR mode's eyes."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["stock", "wide", "tall", "split_screen", "nearer_kept"]
CONTROLS = [
    # moved nearer whatever the view: the stock view no longer the game's
    ("return reached < camera->z_near ? reached : camera->z_near;", "return reached;", "stock"),
    # nearer than the game's stock distance, not than the camera's: a script's nearer one (or a mirror's) replaced
    ("return reached < camera->z_near ? reached : camera->z_near;",
     "return reached < 0.0625f ? reached : camera->z_near;", "nearer_kept"),
    # the plane's top and bottom only, not its sides
    ("1.0 + (double)across * across + (double)up * up", "1.0 + (double)across * 0.0 + (double)up * up", "wide"),
    # the sides' tangent taken across the viewport's height, not its width
    ("* viewport_width / viewport_height * tangent;", "* tangent;", "wide"),
    # the top's left out on a portrait screen
    ("(double)up * up", "(double)up * up * 0.0", "tall"),
    # the bounds' half width, not their farther side: a view off its window's middle
    ("MAX(fabsf(frustum_bounds->x0), fabsf(frustum_bounds->x1))",
     "(frustum_bounds->x1 - frustum_bounds->x0) * 0.5f", "split_screen"),
    ("MAX(fabsf(frustum_bounds->y0), fabsf(frustum_bounds->y1))",
     "(frustum_bounds->y1 - frustum_bounds->y0) * 0.5f", "split_screen"),
    # the half angle taken as the whole
    ("tan(camera->vertical_field_of_view * 0.5f)", "tan(camera->vertical_field_of_view)", "stock"),
]


def generated(fault=None):
    code = read("port/linux/game/near_clip.c")
    text = "\n".join(re.findall(r"^#define NEAR_CLIP_\w+ .*$", code, re.M)) + "\n" + \
        function(code, "near_clip_corner") + "\n" + function(code, "render_near_clip_distance")
    if fault:
        text = mutated(text, *fault)
    bounds = function(read("source/render/render_cameras.c"), "render_camera_build_frustum_bounds")
    return (("under_test.inc", bounds + "\n" + text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("near_clip", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("near_clip", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_reach():
    # the test's reach is the code's (the cyborg's collision radius), and the game's near distance its
    code = read("port/linux/game/near_clip.c")
    assert "#define NEAR_CLIP_REACH 0.2f" in code
    assert "#define REACH 0.2f" in read("tools/harness/tests/near_clip.c")
    assert re.search(r"rasterizer_global_defaults =\s*\{\s*0\.0625f,", read("source/rasterizer/rasterizer.c"))
