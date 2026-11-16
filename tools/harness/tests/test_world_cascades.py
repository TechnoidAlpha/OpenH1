"""display.ambient_occlusion "world"'s distance-field scene (port/linux/include/world_cascades.h,
port/linux/game/world_cascades.c, docs/world-ao.md "The distance-field scene"): the ray step through the cascades
against exact intersections, and the updates: an object found again only where it moved, the camera's step only
where it brings voxels in, each alike to the cascades found whole."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["trace", "update"]
HEADER = "port/linux/include/world_cascades.h"
GAME = "port/linux/game/world_cascades.c"
CONTROLS = [
    # the texel's place not wrapped about the side (the toroidal addressing): rays hit what is not there
    ("float place = at - (float)WORLD_CASCADE_SIDE * floorf(at / (float)WORLD_CASCADE_SIDE);",
     "float place = at - (float)WORLD_CASCADE_SIDE * floorf(at / (float)WORLD_CASCADE_SIDE + 0.5f);", "trace"),
    # a moved object found again where it is, not where it was (its old place keeps it)
    ("any = shapes_bounds(cascades->shapes + before->first, before->count, bounds, 0);", "any = 0;", "update"),
]


def generated(fault=None):
    game = read(GAME)
    if fault:
        game = mutated(game, *fault)
    return (("world_cascades.h", read(HEADER)), ("world_proxies.h", read("port/linux/include/world_proxies.h")),
            ("under_test.inc", game))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("world_cascades", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("world_cascades", generated((before, after))), case)
    assert status == CHECK_FAILED, output
