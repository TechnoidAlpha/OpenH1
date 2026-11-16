"""display.ambient_occlusion "world"'s distance field (port/linux/include/world_sdf.h,
port/linux/game/world_sdf_bake.c, port/linux/game/world_sdf.c, docs/world-ao.md): the layout and plan within its
budget, the encoding's round trip, the sparse field's sample as the GPU reads it (the whole grid's it stands for,
exactly), its atlas, the bake against exact distances (a plane, a box room, a sphere, a box signed by a BSP's point
test, a soup of triangles against every triangle; the bricks' shared faces alike), and the cache file's round trip
and rejections. The objects' occlusion fields: their layout, a box's bake against the hemisphere's integral (its
inside never filtered out past its faces, its reach), a card's by its opacity (an alpha-tested leaf's: #666), their
bytes the same in any slices' order, half floats, the atlas's places, and their part of the file."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["layout", "encoding", "sample", "atlas", "bake", "plane", "room", "sphere", "bsp", "soup", "round_trip", "rejection",
         "object_layout", "object_bake", "object_opacity", "object_determinism", "object_cache", "object_half", "object_atlas", "threads", "pace"]
HEADER = "port/linux/include/world_sdf.h"
BAKE = "port/linux/game/world_sdf_bake.c"
SCHEDULE = "port/linux/game/world_sdf.c"
FILE_FUNCTIONS = ["sdf_put32", "sdf_get32", "sdf_put_float", "sdf_get_float", "sdf_crc", "sdf_header_write",
                  "sdf_header_read", "sdf_scenery_most_bytes", "sdf_scenery_size", "sdf_scenery_free",
                  "sdf_scenery_write", "sdf_scenery_read", "sdf_objects_most_bytes", "sdf_object_voxels",
                  "sdf_objects_size", "sdf_objects_free", "sdf_objects_write", "sdf_finite", "sdf_objects_read",
                  "sdf_header_good", "sdf_cells_size", "sdf_bricks_size", "sdf_cells_good",
                  "sdf_volume_free", "sdf_file_pack", "sdf_file_unpack", "sdf_file_write",
                  "sdf_file_read", "sdf_cache_load", "sdf_bsp_inside", "sdf_hash_bytes", "sdf_hash_long",
                  "sdf_object_hash", "sdf_object_cache_path", "sdf_object_cache_load", "sdf_object_cache_write"]
CONTROLS = [
    # the bake on every processor: the game's own threads left none (#651)
    (BAKE, "long threads = forced > 0 ? forced : processors - WORLD_SDF_RESERVED_PROCESSORS;",
     "long threads = forced > 0 ? forced : processors;", "threads"),
    # none on a machine of a few processors
    (BAKE, "\tif (threads < 1)\n\t\tthreads = 1;", "", "threads"),
    # forced past the most (the threads' table overrun)
    (BAKE, "\tif (threads > WORLD_SDF_MAXIMUM_THREADS)\n\t\tthreads = WORLD_SDF_MAXIMUM_THREADS;", "", "threads"),
    # the menus paced as play (the start's bakes slowed behind the main menu)
    (BAKE, "\tif (!playing)\n\t{", "\tif (0)\n\t{", "pace"),
    # frames late never a rest
    (BAKE, "return normal * WORLD_SDF_PACE_LATE + WORLD_SDF_PACE_SLACK_MS;", "return normal * 2.0f;", "pace"),
    # a fast machine's few tenths of a millisecond late
    (BAKE, "return normal * WORLD_SDF_PACE_LATE + WORLD_SDF_PACE_SLACK_MS;", "return normal * WORLD_SDF_PACE_LATE;",
     "pace"),
    # the frames' mean, not their median: a slow frame now and then a rest
    (BAKE, "return count ? sorted[count / 2] : 0.0f;", "return count ? sorted[count - 1] : 0.0f;", "pace"),
    # a rest no longer while frames keep running late
    (BAKE, "pace->next_rest = pace->next_rest * 2 < WORLD_SDF_PACE_MOST_REST ? pace->next_rest * 2 : "
     "WORLD_SDF_PACE_MOST_REST;", "", "pace"),
    # a heavier scene taken for the bake's lateness (its rests ever longer: the field never comes)
    (BAKE, "\t\t\tpace->next_rest = WORLD_SDF_PACE_LEAST_REST;\n\t\treturn 1;", "\t\t\t;\n\t\treturn 1;", "pace"),
    # the normal not learned again as it rests (the first one kept for good)
    (BAKE, "\t\tpace->normal = median;", "\t\tif (pace->normal <= 0.0f)\n\t\t\tpace->normal = median;", "pace"),
    # a cell marked only where a triangle is within range of its middle (not of each of its voxels)
    (BAKE, "float reach = header->range + 0.5f * step * 1.7320508f;", "float reach = header->range;", "soup"),
    # the table's kinds read the wrong way round
    (BAKE, "return entry[3] == WORLD_SDF_CELL_INSIDE ? -header->range : header->range;",
     "return entry[3] == WORLD_SDF_CELL_INSIDE ? header->range : -header->range;", "sample"),
    # a brick's local voxel taken from the wrong cell (its place as the next cell's)
    (BAKE, "local[axis] = coordinate - (float)(cell[axis] * WORLD_SDF_BRICK);",
     "local[axis] = coordinate - (float)(cell[axis] * WORLD_SDF_BRICK_SPAN);", "sample"),
    # the atlas's rows of bricks laid one voxel row apart
    (BAKE, "index / WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN * row;", "index / WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN;",
     "atlas"),
    # the encoding truncated the wrong way round
    (BAKE, "return (signed char)(value < 0.0f ? value - 0.5f : value + 0.5f);",
     "return (signed char)(value * 0.5f);", "encoding"),
    # the sample's weights for the far corner and the near swapped (along z, the test plane's)
    (BAKE, "(((corner >> 2) & 1) ? fraction[2] : 1.0f - fraction[2])",
     "(((corner >> 2) & 1) ? 1.0f - fraction[2] : fraction[2])", "sample"),
    # a nearer triangle never kept
    (BAKE, "if (distance < row_best[x] * row_best[x])", "if (distance > row_best[x] * row_best[x])", "plane"),
    # no sign: the solid outside the room as air
    (BAKE, "if (inside && inside(context, point))", "if (inside && 0)", "room"),
    # the rows cut to the plane's reach the wrong way (the walls' voxels near them missed)
    (BAKE, "else if (fabsf(plane_row) >= reach)", "else if (fabsf(plane_row) < reach)", "room"),
    # an edge's region taken as the face's (corners rounded wrongly)
    (BAKE, "if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)", "if (0)", "soup"),
    # the point test's children swapped
    (SCHEDULE, "- plane[3] >= 0.0f)];", "- plane[3] < 0.0f)];", "bsp"),
    # the CRC not checked
    (SCHEDULE, "sdf_get32(bytes + size - WORLD_SDF_FILE_CRC) != sdf_crc(0, bytes, size - WORLD_SDF_FILE_CRC)", "0",
     "rejection"),
    # a bad file kept
    (SCHEDULE, "\t\tremove(path);\n\t\tplatform_log(\"world sdf: %s is damaged", "\t\tplatform_log(\"world sdf: %s is damaged",
     "rejection"),
    # a cell's kind or its brick's place not checked
    (SCHEDULE, "if (entry[3] > WORLD_SDF_CELL_BRICK || (entry[3] == WORLD_SDF_CELL_BRICK &&",
     "if (0 && (entry[3] == WORLD_SDF_CELL_BRICK &&", "rejection"),
    # an object's voxels in solid kept as solid (not filled from outside): its darkness filtered past its faces
    (BAKE, "if (marks[other] == 0 || (marks[other] >= 2 && marks[other] < step))", "if (0)", "object_bake"),
    # the cosines' weights to the rays the wrong way round (the far side's occlusion)
    (BAKE, "weights[j * WORLD_SDF_OBJECT_NORMALS + k] = cosine > 0.0f ?\n\t\t\t\tcosine",
     "weights[j * WORLD_SDF_OBJECT_NORMALS + k] = cosine < 0.0f ?\n\t\t\t\t-cosine", "object_bake"),
    # a triangle's cone meeting too few buckets (rays it blocks missed)
    (BAKE, "cosine * buckets->cosines[bucket] - sine * buckets->sines[bucket]",
     "cosine * buckets->cosines[bucket] + sine * buckets->sines[bucket]", "object_bake"),
    # past the grid, its edge's occlusion kept whole (no falling off: the reach not met)
    (BAKE, "\tscale *= scale;\n\tfor (corner = 0; corner < 8; corner++)", "\tscale = 1.0f;\n\tfor (corner = 0; corner < 8; corner++)",
     "object_bake"),
    # the reach from the farthest edge voxel alone (not its occlusion)
    (BAKE, "object->reach = sqrtf(most) > farthest ? sqrtf(most) : farthest;", "object->reach = farthest;", "object_bake"),
    # the fit unweighted (the normals that see most of it as the others: its contact darkening short)
    (BAKE, "weight = WORLD_SDF_OBJECT_FIT_WEIGHT + occlusions[k];", "weight = 1.0;", "object_bake"),
    # a card's opacity taken as solid (an alpha-tested leaf's card blocking all that crosses it: #666's squares)
    (BAKE, "through[ray] = within ? through[ray] * passed : through[ray];",
     "through[ray] = within ? 0.0f : through[ray];", "object_opacity"),
    # each card crossed blocking its opacity of the whole, not of what the cards before it let through
    (BAKE, "through[ray] = within ? through[ray] * passed : through[ray];",
     "through[ray] = within ? through[ray] - (1.0f - passed) : through[ray];", "object_opacity"),
    # the share a direction is blocked counted whole whatever its cards' opacity
    (BAKE, "occlusions[k] += row[k] * blocked;", "occlusions[k] += row[k];", "object_opacity"),
    # the half's rounding truncated
    (BAKE, "if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))", "if (0)", "object_half"),
    # a column's next object put where the last began
    (BAKE, "\t\tz += object->depth;\n", "\n", "object_atlas"),
    # a row's next one put where the last began (over its columns)
    (BAKE, "\t\t\trow_y += row_height;\n", "\n", "object_atlas"),
    # the columns as deep as the atlas may be, in one row (a map's vehicles' and scenery's past the budget)
    (BAKE, "placed = sdf_objects_columns(objects, depth_cap, width_cap, size);",
     "placed = sdf_objects_columns(objects, WORLD_SDF_OBJECT_ATLAS_SIDE, WORLD_SDF_OBJECT_ATLAS_SIDE, size);",
     "object_atlas"),
    # a smaller object after one that does not fit taking its place (the vehicles' first)
    (BAKE, "\t\tif (object->depth > depth_cap || object->width > width_cap)\n\t\t\tbreak;",
     "\t\tif (object->depth > depth_cap || object->width > width_cap)\n\t\t\tcontinue;", "object_atlas"),
    # a kept object's field taken without its CRC checked (a damaged one drawn)
    (SCHEDULE, "sdf_get32(bytes + size - WORLD_SDF_FILE_CRC) == sdf_crc(0, bytes, size - WORLD_SDF_FILE_CRC) &&\n"
     "\t\tsdf_objects_read(", "sdf_objects_read(", "object_cache"),
    # its name made without its triangles' opacities (a leafy tree's taken for another's)
    (SCHEDULE, "\tif (copy->opacities)\n\t\thash = sdf_hash_bytes(hash, copy->opacities, (unsigned long)copy->triangle_count * "
     "sizeof(float));\n", "", "object_cache"),
    # an object's texels not checked for no number
    (SCHEDULE, "if ((object->texels[texel] & 0x7C00u) == 0x7C00u)", "if (0)", "rejection"),
    # an object's bounds read one word on
    (SCHEDULE, "object->bounds[texel] = sdf_get_float(bytes + at + 36 + texel * 4);",
     "object->bounds[texel] = sdf_get_float(bytes + at + 40 + texel * 4);", "round_trip"),
    # the header's fields out of order
    (SCHEDULE, "sdf_put32(bytes + 16, header->width);\n\tsdf_put32(bytes + 20, header->height);",
     "sdf_put32(bytes + 20, header->width);\n\tsdf_put32(bytes + 16, header->height);", "round_trip"),
]


def generated(fault=None):
    bake, schedule = read(BAKE), read(SCHEDULE)
    if fault and fault[0] == BAKE:
        bake = mutated(bake, *fault[1:])
    if fault and fault[0] == SCHEDULE:
        schedule = mutated(schedule, *fault[1:])
    file_code = "\n".join([enum_with(schedule, "WORLD_SDF_FILE_HEADER"), enum_with(schedule, "WORLD_SDF_OBJECT_FILE_MAGIC"),
                           structure(schedule, "sdf_bsp"), structure(schedule, "sdf_solid"),
                           structure(schedule, "sdf_object_copy")] +
                          [function(schedule, name) for name in FILE_FUNCTIONS])
    return (("world_sdf.h", read(HEADER)), ("under_test.inc", bake), ("file_under_test.inc", file_code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("world_sdf", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("path,before,after,case", CONTROLS)
def test_negative_control(path, before, after, case):
    status, output = run(build("world_sdf", generated((path, before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_bake_is_never_in_a_load():
    # (the owner's rule: a map loads as fast as without the feature) scenario_load only hands the cache's lookup to a
    # thread: no file read, no geometry copied, no bake
    scenario = read("source/scenario/scenario.c")
    assert "world_ao_map_loaded();" in scenario
    assert "world_sdf_bake_cell" not in scenario and "world_sdf_plan" not in scenario
    loaded = function(read(SCHEDULE), "world_sdf_map_loaded") + function(read(SCHEDULE), "sdf_current_set")
    for call in ("sdf_cache_load", "sdf_cache_present", "sdf_bake_begin", "world_scene_static_triangles", "fopen"):
        assert call not in loaded, call
