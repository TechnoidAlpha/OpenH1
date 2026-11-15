"""display.ambient_occlusion "world"'s object proxies (port/linux/include/world_proxies.h,
port/linux/game/world_proxies.c, docs/world-ao.md): each kind's analytic occlusion against a numerical integral of the
hemisphere, its reach, a window's culling, order and owner slots, the shapes made from a skeleton and placed on its
nodes, and the shapes' GPU layout alike on both sides."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["sphere", "capsule", "box", "reach", "bound", "far", "culling", "skeleton", "owner", "baked", "field", "none", "drawn"]
HEADER = "port/linux/include/world_proxies.h"
SOURCE = "port/linux/include/world_proxies_source.h"
GAME = "port/linux/game/world_proxies.c"
CONTROLS = [
    # a vehicle whose field is on its way shaded by its boxes meanwhile (six times an Apple M2's frame, #651)
    ("boxless = fielded && instance->field_awaited;", "boxless = 0;", "field"),
    # the static scenery's shapes taken among the dynamic objects' by their distance alone
    ("return baked != candidate->baked ? !baked : distance < candidate->distance;",
     "return distance < candidate->distance;", "baked"),
    # the sphere's horizon left out
    ("if (k2 > 0.001f)", "if (k2 > 1.0f)", "sphere"),
    # the capsule's tube half as wide
    ("line = 2.0f * radius * h / WORLD_PROXIES_PI * (f1 - f0);",
     "line = radius * h / WORLD_PROXIES_PI * (f1 - f0);", "capsule"),
    # the box's form factor twice over, then its back faces counted
    ("return clampf(fabsf(occlusion) / (2.0f * WORLD_PROXIES_PI), 0.0f, 1.0f);",
     "return clampf(fabsf(occlusion) / WORLD_PROXIES_PI, 0.0f, 1.0f);", "box"),
    # the far patches' area halved
    ("occlusion += 4.0f * extents[u] * extents[v] * out * facing", "occlusion += 2.0f * extents[u] * extents[v] * out * facing",
     "far"),
    # the faces not taken the same way round (a shared edge's terms then do not cancel)
    ("occlusion += (float)side * face;", "occlusion += face;", "box"),
    ("if ((float)side * local[axis] <= extents[axis])", "if ((float)side * local[axis] <= -extents[axis])", "box"),
    # a box's reach too short
    ("return sqrtf(x * x + y * y + z * z) + sqrtf(WORLD_PROXIES_LEAST * 4.0f / WORLD_PROXIES_PI * areas);",
     "return sqrtf(x * x + y * y + z * z) + sqrtf(WORLD_PROXIES_LEAST / WORLD_PROXIES_PI * areas);", "reach"),
    # a box's bound without its faces' areas' 4 (under its occlusion), or its height's cosine taken as all
    # of it the wrong way (H / D squared)
    ("bound = WORLD_PROXIES_LEAST * 4.0f / WORLD_PROXIES_PI * faces",
     "bound = WORLD_PROXIES_LEAST * 1.0f / WORLD_PROXIES_PI * faces", "bound"),
    ("faces * (height < distance ? height : distance) /", "faces * (height < distance ? height * height / distance : distance) /",
     "bound"),
    # culled by its centre, not its reach; the farthest kept first
    ("if (dot3(planes[plane], centre) - planes[plane][3] > reach)",
     "if (dot3(planes[plane], centre) - planes[plane][3] > 0.0f * reach)", "culling"),
    ("distance = length3(offset);", "distance = -length3(offset);", "culling"),
    # a slot per shape rather than per owner
    ("proxies[i].v[1][3] = j < i ? proxies[j].v[1][3] : (float)slots++;",
     "proxies[i].v[1][3] = (float)slots++;", "owner"),
    # a vehicle's field counted as one slot (its three then cut apart by a window's end)
    ("slot_count = WORLD_PROXY_FIELD_SLOTS;", "slot_count = 1;", "field"),
    # its boxes kept beside its field
    ("if (boxless && world_proxy_set_fielded(set, shape_index))\n\t\t\t\t\tcontinue;", "", "field"),
    # a definition's one sphere kept, not its field in its place (#666's dark discs)
    ("return set->shapes[shape_index].kind == _world_proxy_box || set->count == 1;",
     "return set->shapes[shape_index].kind == _world_proxy_box;", "field"),
    # a baked placement's box kept (its pages baked with its field: a page without a layer walked the box)
    ("fielded = fielded && !boxless && world_proxies_source_field(instance->definition, &field);",
     "fielded = fielded && !instance->baked && !boxless && world_proxies_source_field(instance->definition, &field);",
     "field"),
    # the rows to its voxels without the frame's scale squared
    ("over = 1.0f / (scale * scale * field->voxel);", "over = 1.0f / (scale * field->voxel);", "field"),
    # its reach the field's alone (its box's bound not taken)
    ("proxies[0].v[2][3] = box_reach < proxies[0].v[2][3] ? box_reach : proxies[0].v[2][3];",
     "proxies[0].v[2][3] = box_reach < 0.0f ? box_reach : proxies[0].v[2][3];", "field"),
    # the atlas's half texel left out
    ("proxies[2].v[0][axis] = (float)field->place[axis] + 0.5f;", "proxies[2].v[0][axis] = (float)field->place[axis];",
     "field"),
    # a box without collision geometry its bounding sphere, as before #773 (a grenade's and a floor marker's discs)
    ("\t\tif (!any && source->drawn)\n\t\t{\n\t\t\tmemcpy(bounds, source->drawn_bounds, sizeof(bounds));\n"
     "\t\t\tany = 1;\n\t\t}\n\t\tif (any)\n\t\t{\n\t\t\tbox_shape(0, (float const (*)[3])bounds, &shape);\n"
     "\t\t\tset_add(set, &shape);\n\t\t}",
     "\t\tif (any)\n\t\t\tbox_shape(0, (float const (*)[3])bounds, &shape);\n\t\telse\n"
     "\t\t\tsphere_shape(source, &shape);\n\t\tset_add(set, &shape);", "drawn"),
    # what it draws not taken: a grenade without shapes
    ("if (!any && source->drawn)", "if (!any && 0)", "drawn"),
    # a capsule's end carried as a direction, not a point
    ("frame_point(frame, shape->b, proxy->v[1]);", "frame_vector(frame, shape->b, proxy->v[1]);", "skeleton"),
]


def generated(fault=None):
    game = read(GAME)
    if fault:
        game = mutated(game, *fault)
    return (("world_proxies.h", read(HEADER)), ("world_proxies_source.h", read(SOURCE)), ("under_test.inc", game))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("world_proxies", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("world_proxies", generated((before, after))), case)
    assert status == CHECK_FAILED, output
