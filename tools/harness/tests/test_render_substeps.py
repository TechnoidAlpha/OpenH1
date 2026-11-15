"""The frames between the ticks at 60 and 120 Hz (port/linux/game/render_interpolation.c, on sim_rate.c's
sim_substep_blend): an object of the micro lane, and the first-person view of a player's biped, are drawn between
the poses its sub-steps left (a frame in sub-step j of the tick drawn: between the end of j - 1, or of the tick
before, and the end of j), what it carries with it; at 30 the frames are the line between the ticks' snapshots, as
they were. A projectile is drawn from its newest sub-steps: its head start and hold from half a tick's motion back,
turning between its sub-steps' rotations as it moves between their places, and the VR mode's grenade let go of in a sub-step from the hand, with no dart and no pop. In a network game another machine's
player is drawn between its two newest sub-steps (the open tick's), not a tick behind, gliding to the tick drawn's
path and back when that is let go of and taken again; at 30 it is drawn as before."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["classic", "substeps", "substeps_60", "carried", "camera_slow", "projectile", "projectile_late",
         "projectile_turning", "vr_release", "respawn", "respawn_120", "scripted_camera", "scripted_camera_120",
         "scripted_camera_fast", "scripted_camera_fast_120", "scripted_camera_cut", "steady_part", "lead",
         "lead_60", "lead_glide", "lead_classic"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the micro lane drawn on the line between the ticks, as before: a tick's sub-steps' path not followed
    "no-substep-path": (("\t\tif (!jumped && render_interpolation_substep_offset(object_index, snap, &offset))",
                         "\t\tif (0)"), "substeps"),
    # the sub-step drawn the frame's fraction of the tick, not of its sub-step
    "tick-fraction": (("point_lerp(before, &substeps->positions[buffer][interpolation_substep_index], "
                       "interpolation_substep_fraction,",
                       "point_lerp(before, &substeps->positions[buffer][interpolation_substep_index], "
                       "interpolation_fraction,"), "substeps"),
    # what is carried left on the line
    "carried-left": (("\troot_index = object_get_ultimate_parent(object_index);",
                      "\troot_index = object_index;"), "carried"),
    # the view left on the line between the ticks
    "camera-on-the-line": (("\tif (substep_unit_index != NONE && render_interpolation_substep_offset(substep_unit_index, "
                            "FALSE, &offset))",
                            "\tif (0)"), "substeps"),
    # the view's snapshot taken with the next tick's first sub-steps in it (a frame slower than a sub-step)
    "camera-unshifted": (("\t\t\trender_interpolation_camera_at_tick(substep_unit_index, &camera->latest);",
                          "\t\t\t;"), "camera_slow"),
    # a projectile drawn a tick behind, not from its newest sub-steps
    "projectile-a-tick-behind": (("\tif ((pending = render_interpolation_projectile_nodes(object_index)) != NULL)",
                                  "\tif ((pending = NULL) != NULL)"), "projectile"),
    # ... from where its head start put it at once: it darts out and waits
    # a projectile turned as its newest sub-step left it, a step each sub-step, not between the two drawn
    "projectile-turns-in-steps": (("\trotated = rotated && now.is_rotation;", "\trotated = FALSE;"),
                                  "projectile_turning"),
    "head-start-drawn": (("\tif (to_step - substeps->made_step < count &&", "\tif (0 &&"), "projectile"),
    # ... and the hold drawn from the eye, a whole tick back
    "from-the-eye": (("\t\t\t\t\t1.0f : 0.5f;", "\t\t\t\t\t1.0f : 1.0f;"), "projectile_late"),
    # ... or a sub-step later than the frame's time: past the newest the clock ran
    "projectile-past-newest": (("\treturn interpolation_tick * interpolation_substep_count + "
                                "render_interpolation_lead_newest();",
                                "\treturn interpolation_tick * interpolation_substep_count + "
                                "render_interpolation_lead_newest() + 1;"), "projectile_late"),
    # a respawn blended from the dead camera into the new unit's eye (nearer and turned less than a cut's)
    "respawn-glides": (("\t\tcamera->has_previous = camera->valid && camera->unit_index == unit_index;",
                        "\t\tcamera->has_previous = camera->valid;"), "respawn"),
    "respawn-glides-120": (("\t\tcamera->has_previous = camera->valid && camera->unit_index == unit_index;",
                            "\t\tcamera->has_previous = camera->valid;"), "respawn_120"),
    # a scripted camera's snapshots blended as if each were taken at its tick's end: it lurches at each tick (#805)
    "scripted-camera-untimed": (("\t\tcamera->latest_time = timed ? now : (double)interpolation_tick;",
                                 "\t\tcamera->latest_time = (double)interpolation_tick;"), "scripted_camera"),
    "scripted-camera-untimed-120": (("\t\tcamera->latest_time = timed ? now : (double)interpolation_tick;",
                                     "\t\tcamera->latest_time = (double)interpolation_tick;"), "scripted_camera_120"),
    # ... or the snapshot before the previous one not taken when the frame's moment falls before the previous
    "scripted-camera-no-before": (("\t\tif (drawn_time < from_time && camera->has_before)",
                                   "\t\tif (0)"), "scripted_camera"),
    # ... a fast one's snapshots judged a cut when further apart than a cut's distance, as the player's views are: drawn
    # from the latest a tick ahead at random ticks (#805)
    "scripted-camera-fast-cuts": (("\t\t(camera->timed ? camera->cut_to_latest : observer_cut(&camera->previous, "
                                   "&camera->latest)))",
                                   "\t\tobserver_cut(&camera->previous, &camera->latest))"), "scripted_camera_fast"),
    "scripted-camera-fast-cuts-120": (("\t\t(camera->timed ? camera->cut_to_latest : observer_cut(&camera->previous, "
                                       "&camera->latest)))",
                                       "\t\tobserver_cut(&camera->previous, &camera->latest))"),
                                      "scripted_camera_fast_120"),
    # ... or a cut to another point taken for the fast move it is not: glided through
    "scripted-camera-cut-glides": (("\treturn !(distance_squared(&expected, &b->position) <= CAMERA_CUT_DISTANCE * "
                                    "CAMERA_CUT_DISTANCE);",
                                    "\treturn FALSE;"), "scripted_camera_cut"),
    # ... or a fast camera's first move after a cut (faster than a cut's distance a tick, with no move since the cut to
    # carry on) taken for another cut: drawn from the latest, a tick ahead
    "scripted-camera-cut-twice": (("\t\treach += speed * ticks;", "\t\treach += 0.0f * ticks;"),
                                  "scripted_camera_cut"),
    # a part moving steadily further than a limb moves in a tick taken for a new pose each tick: drawn stepping
    "steady-part-snaps": (("NODE_SNAP_DISTANCE + (steady ? NODE_STEADY_GROWTH * moves_before[node_index] : 0.0f)",
                           "NODE_SNAP_DISTANCE"), "steady_part"),
    # ... or any node's jump allowed: a new pose blended
    "new-pose-blended": (("\t\t\tif (!(moved <= NODE_SNAP_DISTANCE + (steady ? NODE_STEADY_GROWTH * "
                          "moves_before[node_index] : 0.0f)))",
                          "\t\t\tif (0)"), "steady_part"),
    # another's player drawn from the tick drawn's sub-steps, a tick behind, not its newest
    "lead-a-tick-behind": (("\treturn (short)(interpolation_substep_index + interpolation_substep_count - 1);",
                            "\treturn interpolation_substep_index;"), "lead"),
    # ... one sub-step later than the frame's time: past the newest the clock ran
    "lead-past-newest": (("\treturn (short)(interpolation_substep_index + interpolation_substep_count - 1);",
                          "\treturn (short)(interpolation_substep_index + interpolation_substep_count);"), "lead_60"),
    # the lead let go of and taken at once: a jump of a sub-step's part of a tick's motion
    "lead-jumps": (("\tif (substeps->lead_frame == NONE || !(now >= substeps->lead_time) ||",
                    "\tif (1 ||"), "lead_glide"),
    # the grenade let go of in a sub-step drawn from half a tick back from its head start, not from the hand
    "release-from-the-body": (("\t\t\tif (iterator.index == interpolation_from.object_index)",
                               "\t\t\tif (0)"), "vr_release"),
}


# the camera's code of the view from a local player's vehicle drawn from its newest step
# (test_render_newest_vehicle.py), which the other tests stub: no vehicle is drawn so
NEWEST = ("vector_turned", "render_interpolation_newest_step", "rotation_turn", "render_interpolation_newest_history",
          "vector_at_most", "render_interpolation_newest_motion", "newest_motion_carried", "quaternion_turned",
          "newest_blend_left", "newest_blends_left",
          "render_interpolation_newest_blend", "render_interpolation_direct",
          "render_interpolation_look_late", "render_interpolation_direct_camera",
          "render_interpolation_newest_camera", "render_interpolation_newest_vehicle", "render_interpolation_glide",
          "render_interpolation_newest_among", "render_interpolation_vehicle_continues",
          "render_interpolation_newest_vehicles",
          "render_interpolation_newest_correction", "render_interpolation_newest_frame",
          "render_interpolation_seat_pitch", "view_frame",
          "render_interpolation_seat_camera", "render_interpolation_correct_object", "render_interpolation_camera",
          "render_interpolation_view_newest")
NEWEST_STUBS = """static void render_interpolation_newest_vehicles(boolean rendering) { (void)rendering; }
static boolean render_interpolation_newest_frame(long object_index, real_matrix4x3 const **frames,
	real_matrix4x3 const **drawn, real_vector3d *correction)
{
	(void)object_index; (void)frames; (void)drawn; (void)correction;
	return FALSE;
}
"""

# whether an object is drawn from its newest sub-steps (in a network game, another's: render_interpolation_lead_wanted)
# is the case's word; none is, but in the lead cases
LEAD_STUB = """static boolean lead_wanted_fake;
static boolean render_interpolation_lead_wanted(long root_index)
{
	(void)root_index;
	return lead_wanted_fake;
}
"""


def generated(fault=None, newest=False):
    header = read("port/linux/game/sim_rate.h")
    clock = read("port/linux/game/sim_rate.c")
    code = read("port/linux/game/render_interpolation.c")

    decls = enum_with(header, "SIM_RATE_CLASSIC") + "\n" + enum_with(header, "NUMBER_OF_SIM_SUBSTEP_SLOTS") + "\n"
    start = header.index("typedef void (*sim_substep_procedure)")
    decls += header[start:header.index(";", start) + 1] + "\n"

    # the constants and structures
    text = code[code.index("/* ---------- constants */"):code.index("/* ---------- structures */")] + "\n"
    for name in ("interpolation_quaternion", "interpolation_rotation", "interpolated_object", "interpolated_substeps",
                 "lead_projectile", "interpolated_camera", "newest_history", "newest_motion",
                 "newest_vehicle", "interpolated_shape"):
        text += structure(code, name) + "\n"
    # the globals, but for the first-person weapon's, the particles' and the ghosts' (the test's own)
    globals_text = code[code.index("static struct interpolated_object *interpolated_objects;"):
                        code.index("/* ---------- blending */")]
    text += "\n".join(line for line in globals_text.splitlines()
                      if not re.search(r"interpolated_first_person|later_particle|ghosts\[", line)) + "\n"
    # sim_substep_blend, on the test's clock
    text += function(clock, "sim_substep_blend").replace("sim_rate_globals.substep_count", "fake_substep_count") + "\n"
    text += "static void render_interpolation_substep(short substep_index, short substep_count, real seconds);\n"
    text += ("static void render_interpolation_substep_rotation(long object_index, struct object_datum const *object,"
             " struct interpolated_substeps *substeps, short buffer, short substep_index);\n")
    text += "static void render_interpolation_camera_anchors(void);\n"
    if newest:
        enum_start = code.index("/* the views display.direct_camera takes")
        text += code[enum_start:code.index("static struct observer_result const *render_interpolation_blended_camera(")]
        text += ("static struct observer_result const *render_interpolation_blended_camera(short, "
                 "struct observer_result const *);\n")
        text += "static void render_interpolation_newest_vehicles(boolean rendering);\n"
        text += ("static boolean render_interpolation_newest_frame(long object_index, real_matrix4x3 const **frames, "
                 "real_matrix4x3 const **drawn, real_vector3d *correction);\n")
        # (no lift carries a rider here: render_interpolation_rider_carry's machines are not in the fake world)
        text += ("static boolean render_interpolation_rider_carry(long unit_index, real_vector3d *carry)\n"
                 "{\n\t(void)unit_index; (void)carry;\n\treturn FALSE;\n}\n")
    else:
        text += NEWEST_STUBS
    text += LEAD_STUB
    for name in ("lerp", "point_lerp", "vector_length", "vector_nlerp", "basis_is_rotation", "quaternion_from_basis",
                 "basis_from_quaternion", "rotation_from_matrix", "quaternion_multiply", "quaternion_nlerp",
                 "vector_turned_by", "matrix_blend_rotations", "correction_significant",
                 "correction_advance", "correction_drawn", "correction_add", "distance_squared", "vector_in_frame",
                 "vector_from_frame", "point_in_frame", "point_from_frame", "matrix_in_frame", "matrix_from_frame",
                 "frame_blend", "render_interpolation_previous_values", "render_interpolation_register",
                 "render_interpolation_tick",
                 "render_interpolation_object_from", "render_interpolation_substep",
                 "render_interpolation_substep_rotation",
                 "render_interpolation_lead_weight", "render_interpolation_lead_step",
                 "render_interpolation_lead_newest", "render_interpolation_kept_position",
                 "render_interpolation_kept_rotation",
                 "render_interpolation_lead_to_step", "render_interpolation_projectile_lead",
                 "render_interpolation_projectile_nodes", "render_interpolation_ghost_part",
                 "render_interpolation_lead_frame",
                 "render_interpolation_substep_offset", "render_interpolation_substep_frame",
                 "render_interpolation_frame_begin", "render_interpolation_frame_end",
                 "render_interpolation_pending_from", "render_interpolation_blend_in_frame",
                 "render_interpolation_object_node_matrices", "observer_blend", "observer_cut", "observer_timed_cut",
                 "render_interpolation_camera_timed",
                 "render_interpolation_camera_substep_unit", "render_interpolation_camera_at_tick",
                 "render_interpolation_camera_anchors", "render_interpolation_camera_substep_vehicle",
                 "render_interpolation_camera_at_tick_in_vehicle", "render_interpolation_camera_vehicle_offset",
                 "render_interpolation_blended_camera"):
        text += function(code, name) + "\n"
    if newest:
        globals_text = code[code.index("static struct observer_result direct_cameras["):
                            code.index("/* the views display.direct_camera takes")]
        text += globals_text + "\n"
        for name in NEWEST:
            text += function(code, name) + "\n"
    if fault:
        text = mutated(text, *fault)
    return (("decls.inc", decls), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("render_substeps", generated()), case)
    assert status == 0, output


# the cases whose frames come at times drawn from a seed, run from many seeds (the test's own generator: the same
# frames on every system), so that a rare frame timing cannot pass on one machine and fail on another
SEEDED = ["scripted_camera", "scripted_camera_120", "scripted_camera_fast", "scripted_camera_fast_120",
          "scripted_camera_cut"]
SEEDS = 200


@pytest.mark.parametrize("case", SEEDED)
def test_case_seeds(case):
    executable = build("render_substeps", generated())
    for seed in range(1, SEEDS + 1):
        status, output = run(executable, f"{case}@{seed}")
        assert status == 0, f"seed {seed}: {output}"


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    executable = build("render_substeps", generated(fault))
    # (a seeded case's fault seen from any of its seeds: some faults show only at some frames' timings)
    for name in [f"{case}@{seed}" for seed in range(1, SEEDS + 1)] if case in SEEDED else [case]:
        status, output = run(executable, name)
        if status == CHECK_FAILED:
            return
    assert False, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
