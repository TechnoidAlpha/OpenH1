"""The frames between the ticks at 60 and 120 Hz for a vehicle a player drives (port/linux/game/render_interpolation.c):
it moves and turns in the sub-steps (vehicles.c), and is drawn between the frames (its root node's place and
rotation) its sub-steps left, the rotation nlerped the shorter way; its nodes, and those of what it carries (its
rider, its turret's gun), blended between the ticks in its frame and placed where the sub-steps' frame has it. The
view from its seat, in the first person or following it, hangs on it at the unit's camera position: taken back to
where the tick left that point (a frame slower than a sub-step has the next tick's first in the observer), and moved
with it onto the vehicle's path. At 30, and for a vehicle of the macro lane, the frames are the line between the
ticks' snapshots, as they were."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import CHECK_FAILED, build, run  # noqa: E402
from test_render_substeps import generated  # noqa: E402

CASES = ["classic", "substeps", "substeps_60", "following", "camera_slow", "following_slow", "macro", "respawn",
         "respawn_following"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # a vehicle's frame not kept: it is drawn as a biped is, its place on the path and its rotation on the tick
    "no-frame": (("\t\t\tsubsteps->framed[buffer] = TEST_FLAG(_object_mask_vehicle, object->object.type) &&",
                  "\t\t\tsubsteps->framed[buffer] = FALSE &&"), "substeps"),
    # the frame drawn the frame's fraction of the tick, not of its sub-step
    "frame-tick-fraction": (("\t\t\t\tinterpolation_substep_fraction,\n\t\t\t\t&substeps->frame_drawn);",
                             "\t\t\t\tinterpolation_fraction,\n\t\t\t\t&substeps->frame_drawn);"), "substeps_60"),
    # the tick before's pose seen from the tick drawn's frame: the pose not blended in the vehicle's frame
    "previous-in-latest-frame": (("matrix_in_frame(frames[0], &previous[node_index], &previous_local);",
                                  "matrix_in_frame(frames[1], &previous[node_index], &previous_local);"), "substeps"),
    # what the vehicle carries left as a biped's carried things are: moved by its place alone, not turned with it
    "carried-unturned": (("\t\telse if (!jumped && render_interpolation_substep_frame(object_index, frames, &frame))",
                          "\t\telse if (!jumped && object_get(object_index)->object.parent_object_index == NONE &&\n"
                          "\t\t\trender_interpolation_substep_frame(object_index, frames, &frame))"), "substeps"),
    # rotations nlerped the long way round when their quaternions' signs differ
    "long-way": (("\t\tif (qa.i * qb.i + qa.j * qb.j + qa.k * qb.k + qa.w * qb.w < 0.0f)", "\t\tif (0)"), "substeps"),
    # the view in the seat left on the line between the ticks
    "seat-view-on-the-line": (("\tif (substep_vehicle_index != NONE && render_interpolation_camera_vehicle_offset("
                               "camera, t, &offset))", "\tif (0)"), "substeps"),
    # ... and following the vehicle
    "following-view-on-the-line": (("\tif (substep_vehicle_index != NONE && render_interpolation_camera_vehicle_offset("
                                    "camera, t, &offset))", "\tif (0)"), "following"),
    # the view's snapshot taken with the next tick's first sub-steps in it (a frame slower than a sub-step)
    "seat-view-unshifted": (("\t\t\trender_interpolation_camera_at_tick_in_vehicle(camera, &camera->latest);",
                             "\t\t\t;"), "camera_slow"),
    # ... and following
    "following-view-unshifted": (("\t\t\trender_interpolation_camera_at_tick_in_vehicle(camera, &camera->latest);",
                                  "\t\t\t;"), "following_slow"),
    # a respawn into the seat blended from the dead camera (nearer and turned less than a cut's)
    "respawn-glides": (("\t\tcamera->has_previous = camera->valid && camera->unit_index == unit_index;",
                        "\t\tcamera->has_previous = camera->valid;"), "respawn"),
    "respawn-glides-following": (("\t\tcamera->has_previous = camera->valid && camera->unit_index == unit_index;",
                                  "\t\tcamera->has_previous = camera->valid;"), "respawn_following"),
    # the view hung on the vehicle's root, not where it is on the vehicle: off as the vehicle turns
    "anchor-at-root": (("\t\tunit_get_camera_position(unit_index, &anchor);",
                        "\t\tanchor = record->nodes[record->latest * record->node_capacity].position;"), "following"),
    # the view's anchor taken back by the place the vehicle moved alone, not turned
    "unshifted-turn": (("\tpoint_from_frame(now, &camera->anchor, &at_now);",
                        "\tpoint_from_frame(record->nodes + record->latest * record->node_capacity, &camera->anchor, "
                        "&at_now);\n\tat_now.x += now->position.x - record->nodes[record->latest * "
                        "record->node_capacity].position.x;\n\tat_now.y += now->position.y - record->nodes[record->latest"
                        " * record->node_capacity].position.y;\n\tat_now.z += now->position.z - record->nodes["
                        "record->latest * record->node_capacity].position.z;"), "camera_slow"),
}


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("render_vehicle_substeps", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("render_vehicle_substeps", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
