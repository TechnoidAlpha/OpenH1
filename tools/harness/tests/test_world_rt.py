"""display.ambient_occlusion "ray_traced"'s scene (port/linux/include/world_rt.h, port/linux/game/world_rt.c,
docs/world-ao.md "Hardware ray queries"): a mesh's triangles turned to face out, the box, the instances' transforms
and owners; a fake world's frames: the meshes made once a map as they are first needed, the instances on the objects'
nodes, the static scenery left out, everything let go of at the map's end."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["arithmetic", "frame"]
GAME = "port/linux/game/world_rt.c"
CONTROLS = [
    # a mesh wound inward kept so (the backend's front faces then the inside)
    ("\tif (volume < -(double)least)\n", "\tif (volume > (double)least)\n", "arithmetic"),
    # an owner that is the datum index itself (object 0's the structure's 0)
    ("(unsigned long)(object_index & 0xFFFF) + 1;", "(unsigned long)(object_index & 0xFFFF);", "arithmetic"),
    # the box's centre left at the node's origin
    ("\t\t\tposition += frame->axes[column][row] * sphere[column];\n", "", "arithmetic"),
    # the static scenery the field and its pages hold traced too (its occlusion twice)
    ("\t\tif (object->baked)\n\t\t\tcontinue;\n", "", "frame"),
    # a definition's meshes made again each time it is drawn
    ("\t\tif (entry->definition == definition)\n\t\t\treturn entry;\n", "", "frame"),
    # the structure's mesh not made again for another BSP
    ("\tif (bsp_index == world_rt.bsp_index)\n\t\treturn;", "\tif (world_rt.structure)\n\t\treturn;", "frame"),
    # a box for anything with no collision geometry (an emitter's, the sky's ring's sphere)
    ("\tif (!any && item && entry->node_count > 0", "\tif (!any && entry->node_count > 0", "frame"),
    # the instances handed again in the same frame
    ("\tif (world_rt.framed && world_rt.frame == frame)\n\t\treturn;", "", "frame"),
    # the meshes kept past the map's end
    ("\tif (world_rt.sent)\n\t\thalo_world_ao_mesh(0, 0, NULL);", "", "frame"),
]


def generated(fault=None):
    game = read(GAME)
    if fault:
        game = mutated(game, *fault)
    headers = [(name, read(f"port/linux/include/{name}"))
               for name in ("world_ao.h", "world_proxies_source.h", "world_rt.h")]
    return tuple(headers) + (("under_test.inc", game),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("world_rt", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("world_rt", generated((before, after))), case)
    assert status == CHECK_FAILED, output
