"""The view from a local player's vehicle drawn from its newest step (port/linux/game/render_interpolation.c,
display.direct_camera): the vehicle, and what it carries, drawn where its newest step left it, carried on by its
velocity and its spin for the time since (less than none past a macro lane vehicle's first sub-step at 60 and 120),
its pose blended between the ticks in its frame; the view from its seat at the seat's camera point there, aimed where
the player aims (in the first person turned to the vehicle's up and rolled with it, as the first-person camera poses
it; following it, the player's aim), turned on by the aim's motion come since the frame's input, read once a frame. A
step that the motion carried on did not foresee (a climb begun at once) is blended in from where the vehicle was
drawn, never jumped to, but a start from standing is drawn at once; the view's pitch held at the seat's easing limit
moves evenly. A correction glides the vehicle and the view the same; getting in or out (dying, respawning in it)
glides the vehicle from one drawing to the other. Off (display.direct_camera false), or a seat whose view is the
gun's, it is drawn between the ticks as before."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import CHECK_FAILED, build, run  # noqa: E402
from test_render_substeps import generated  # noqa: E402

CASES = ["steady_30", "steady_60", "steady_120", "steady_macro_60", "steady_macro_120", "physics_apart",
         "physics_apart_30", "accelerating", "accelerating_30", "accelerating_macro", "following", "following_30",
         "aim_in_world", "late", "following_late", "following_pulled", "leaving", "leaving_30", "leaving_teleported",
         "kinked", "start", "pitch_held", "coming", "coming_30", "correction", "off", "gun_view"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the vehicle left where its newest step left it, not carried on by its velocity
    "not-carried": (("\t\tnewest->drawn.position.x += shift.i;\n\t\tnewest->drawn.position.y += shift.j;\n"
                     "\t\tnewest->drawn.position.z += shift.k;\n", ""), "steady_120"),
    # ... nor turned on by its spin (a turn a step at a time)
    "not-turned": (("\tif (angle > 1e-6f && angle <= NEWEST_TURN_MAXIMUM)", "\tif (0)"), "steady_30"),
    # carried on by the physics' velocity and spin, which a tick keeps apart from the motion, not the newest step's
    "physics-velocity": (("\tif (history->count >= 2)", "\tif (0)"), "physics_apart"),
    # ... at the steps' mean velocity, not on with their acceleration: speeding up, it steps back at each step
    "no-acceleration": (("\t\tif (history->count >= 3)", "\t\tif (0)"), "accelerating"),
    # ... the steps' times taken a sub-step apart at 30 (a tick)
    "steps-misread": (("\t\treturn (interpolation_tick + (next >= 1 ? 1 : 0)) * count;\n\treturn interpolation_tick * "
                       "count + next;", "\t\treturn (interpolation_tick + (next >= 1 ? 1 : 0)) * count;\n\treturn "
                       "interpolation_tick * 4 + next;"), "steady_30"),
    # the macro lane's newest step taken for a sub-step's, not the tick's whole motion in its first
    "macro-as-micro": (("\tif (count > 1 && object_get_lane(vehicle_index) != _object_lane_micro)", "\tif (0)"),
                       "steady_macro_120"),
    # what the vehicle carries left between the ticks, behind the vehicle drawn from its newest step
    "carried-behind": (("\tnewest = !jumped && render_interpolation_newest_frame(",
                        "\tnewest = !jumped && object_get(object_index)->object.parent_object_index == NONE &&\n"
                        "\t\trender_interpolation_newest_frame("), "steady_60"),
    # the view left at the seat as the newest step left it, not carried on with the vehicle
    "view-not-carried": (("\tpoint_from_frame(&newest->drawn, &newest->anchor, &anchor_drawn);",
                          "\tpoint_from_frame(&newest->root, &newest->anchor, &anchor_drawn);"), "following"),
    # the aim in the seat left the world's, not turned to the vehicle's up
    "aim-untilted": (("\t\tif (!newest->aim_in_world)", "\t\tif (0)"), "steady_60"),
    # ... or the view's up the world's, not rolled with the vehicle
    "unrolled": (("\t\tvector_from_frame(&frame, &up, &direct->up);",
                  "\t\tobserver_up_from_forward(&direct->forward, &direct->up);"), "aim_in_world"),
    # the late motion not taken in the seat
    "late-unread": (("\t\tif (render_interpolation_look_late(local_player_index, &yaw, &pitch))", "\t\tif (0)"),
                    "late"),
    # ... or following it, the swing not turned with the aim
    "swing-unturned": (("\t\tvector_from_frame(&aimed, &local, &swing);",
                        "\t\tvector_from_frame(&swung, &local, &swing);"),
                       "following_late"),
    # the following view's swing the frame's own (the camera's pull in stepping each frame), not between the ticks
    "swing-unblended": (("\t\tif (blended == &camera->blended)", "\t\tif (0)"), "following_pulled"),
    # the vehicle left by the newest step jumps back a tick's motion
    "no-glide-back": (("\t\t\trender_interpolation_glide(before->vehicle_index, &before->ahead);", ""), "leaving"),
    # ... or, teleported as the player leaves, is drawn gliding from far off
    "glide-over-a-cut": (("\t\t\trender_interpolation_vehicle_continues(before->vehicle_index))", "\t\t\tTRUE)"),
                         "leaving_teleported"),
    # the next step's misjudged motion jumped to, not blended in from where the vehicle was drawn
    "unblended": (("\t\t\t\thistory->blend_places[history->blend_count] = difference;",
                   "\t\t\t\thistory->blend_places[history->blend_count] = *global_zero_vector3d;"), "kinked"),
    # ... a start from standing blended in too, its latency given back
    "start-blended": (("velocity_before >= NEWEST_STANDING", "velocity_before >= -1.0f"), "start"),
    # the view's pitch held at the easing limit stepping with it
    "pitch-steps": (("\treturn (real)(weighed / (now - until)) - pitch;",
                     "\treturn (real)(0.0 * weighed / (now - until));"),
                    "pitch_held"),
    # ... and taken by it jumps ahead
    "no-glide-ahead": (("\t\t\trender_interpolation_glide(newest->vehicle_index, &back);", ""), "coming"),
    # the view in the seat not gliding with the corrected vehicle
    "view-unglided": (("\tdirect->position.x += correction.i;\n\tdirect->position.y += correction.j;\n"
                       "\tdirect->position.z += correction.k;\n\treturn direct;", "\treturn direct;"), "correction"),
    # a seat whose view is the gun's drawn from the newest step
    "gun-view-newest": (("\tif (gun_view)\n\t\treturn FALSE;", ""), "gun_view"),
}


def newest_generated(fault=None):
    return generated(fault, newest=True)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("render_newest_vehicle", newest_generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("render_newest_vehicle", newest_generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
