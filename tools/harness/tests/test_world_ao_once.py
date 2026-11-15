"""display.ambient_occlusion "world" found once a pixel (port/linux/game/world_ao_shading.c's world_ao_pass_* and
world_ao_upsample, docs/world-ao.md "Found once a pixel"): a pixel's position found again from its distance along the
camera's ray, as the pass finds it; and the occlusion found at a quarter of the pixels upsampled with the edges kept.
Each with a negative control. The emitters' text of the same is checked against these in tools/test_world_ao_light.py's
way: by the CPU reference's operations, the shaders' named alike."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["position", "upsample"]
GAME = "port/linux/game/world_ao_shading.c"
CONTROLS = [
    # the ray not made a unit long: the distance taken along the view's axis
    ("\t\tpoint[axis] = eye[axis] + distance * ray[axis] / length;", "\t\tpoint[axis] = eye[axis] + distance * ray[axis] / (1.0f + 0.0f * length);",
     "position"),
    # plain bilinear upsampling: the wall's occlusion bleeds onto the post's edges, and the post's onto the wall
    ("\treturn along / (1.0f + relative * relative);", "\treturn 1.0f + 0.0f * relative * along;", "upsample"),
]


def generated(fault=None):
    game = read(GAME)
    if fault:
        game = mutated(game, *fault)
    headers = [(name, read(f"port/linux/include/{name}"))
               for name in ("world_ao.h", "world_proxies.h", "world_sdf.h")]
    return tuple(headers) + (("under_test.inc", game),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("world_ao_once", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("world_ao_once", generated((before, after))), case)
    assert status == CHECK_FAILED, output
