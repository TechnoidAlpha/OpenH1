"""display.ambient_occlusion "world"'s scene (port/linux/include/world_scene.h, port/linux/game/world_scene.c,
docs/world-ao.md): the whole file against a small fake game. The static triangles' copy of a hand-built collision
BSP; ids stable over frames and never another live object's; an object's instance following its node as drawn between
the ticks; the light list."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, mutated, read, run, structure  # noqa: E402
from harness import skip_without_32_bit_abi  # noqa: E402

CASES = ["empty", "triangles", "ids", "follows", "lights"]
GAME = "port/linux/game/world_scene.c"
CONTROLS = [
    # every edge's left side taken: the triangle's ring, which starts on the shared edge's right, is lost
    ("right = edge->surface_indices[1] == surface_index;", "right = 0;", "triangles"),
    # fanned from the wrong corner
    ("long const fan[3] = { ring[0], ring[corner], ring[corner + 1] };",
     "long const fan[3] = { ring[corner - 1], ring[corner], ring[corner + 1] };", "triangles"),
    # the surface's material lost
    ("materials[count] = surface->material_index;", "materials[count] = 0;", "triangles"),
    # the bounds' first vertex not taken
    ("if (!index || value < bounds[axis])", "if (value < bounds[axis])", "triangles"),
    # an object's last frame never kept
    ("else if (history->frame + 1 == world_scene.frame)", "else if (0)", "follows"),
    # ... or kept though the frame before didn't list it
    ("else if (history->frame + 1 == world_scene.frame)", "else if (1)", "follows"),
    # another object's history taken over by the one made in its slot
    ("if (history->id != id)\n", "if (0)\n", "ids"),
    # the menus' Spartan, drawn in a view of its own, and the rifle it holds, listed as the scene's
    ("menu_spartan_object(object_get_ultimate_parent(object_index))", "0", "ids"),
    # an instance found by its slot alone
    ("&& world_scene.instances[slot].id == id ?", "?", "ids"),
    # the bounding sphere left where the tick has it
    ("\t\trender_interpolation_object_point(object_index, &center);\n", "", "follows"),
    # the lighting point found again each frame
    ("if (!history->lit || history->lit_from.x != center->x", "if (1 || history->lit_from.x != center->x", "follows"),
    # the axes without the node's scale
    ("transform[row][column] = matrix->n[column][row] * matrix->scale;",
     "transform[row][column] = matrix->n[column][row];", "follows"),
    # a dead light listed
    ("|| !datum_try_and_get(light_data, light))", ")", "lights"),
    # a fixture of another BSP listed
    ("if (!TEST_FLAG(fixture->object.on_bsp_flags, world_scene.bsp_index))\n\t\t\tcontinue;", "", "lights"),
]


def game():
    header = read("source/physics/collision_bsp_definitions.h")
    types = read("source/objects/object_types.h")
    objects = read("source/objects/objects.h")
    return "\n".join([enum_with(header, "MAXIMUM_EDGES_PER_COLLISION_SURFACE")] +
                     [structure(header, name) for name in ["collision_surface", "collision_edge", "collision_vertex"]] +
                     [enum_with(types, "_object_mask_all"),
                      enum_with(objects, "_object_invisible_bit")])


def generated(fault=None):
    code = read(GAME)
    if fault:
        code = mutated(code, *fault)
    # (the game's headers left out: the test's fake game stands for them)
    code = re.sub(r'^#include "(?!world_scene\.h).*"$', "", code, flags=re.M)
    return (("world_scene.h", read("port/linux/include/world_scene.h")), ("game.inc", game()),
            ("under_test.inc", code))


# what only the game's 32-bit ABI shows (harness.NATIVE_64_BIT)
ABI_DEPENDENT = {
    case: "an object's id is its datum handle, negative (its salt's high bit) only in the game's 32-bit long"
    for case in ("ids", "follows")
}


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    skip_without_32_bit_abi(ABI_DEPENDENT, case)
    status, output = run(build("world_scene", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    skip_without_32_bit_abi(ABI_DEPENDENT, case)
    status, output = run(build("world_scene", generated((before, after))), case)
    assert status == CHECK_FAILED, output
