"""Riding at game.sim_rate 30, 60 and 120: the riding code's own carry, seed and record (vehicle_riding.c) and the
riders' lanes (vehicle_riding_bipeds.c), run through a tick's order of updates (the vehicle's update, the rider's
after it, then the sub-steps of what moves in them) at one, two and four sub-steps a tick. A rider's carry, its
view's turn by the deck, a jump that lands back on the deck, stepping off with the deck's motion and a rider thrown
from a vehicle that rolls onto its side are the same at 60 and 120 as at 30, to the bit; with riding off nothing
rides at any rate."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402
from harness.riding import engine, without_includes  # noqa: E402

RIDING = "port/linux/game/vehicle_riding.c"
BIPEDS = "port/linux/game/vehicle_riding_bipeds.c"
CASES = ["ride", "jump", "step-off", "flipped", "drop-on", "riding-off"]
RIDING_FUNCTIONS = ("riding_period", "riding_clip", "riding_snapshot", "riding_body", "riding_anchor_body",
                    "riding_open_window", "riding_node_motion", "riding_node_judged",
                    "vehicle_riding_correction_identity", "riding_correction_matrix",
                    "vehicle_riding_correction_since", "vehicle_riding_clear", "vehicle_riding_drop",
                    "vehicle_riding_is_riding", "vehicle_riding_point_to_local", "vehicle_riding_point_to_world",
                    "vehicle_riding_yaw", "vehicle_riding_turn_vector", "vehicle_riding_deck_point_velocity",
                    "vehicle_riding_snapshot_step", "vehicle_riding_seed", "vehicle_riding_record",
                    "vehicle_riding_turn_relative")
LANE_FUNCTIONS = ("biped_riding_lanes_find", "biped_riding_lanes_latch", "biped_riding_lanes_hold",
                  "biped_riding_holds_lane", "vehicle_riding_holds_lane")

# (source, before, after, the case that must fail a CHECK)
CONTROLS = {
    # nothing held in the sub-steps: the merge's sub-step bipeds before riding at 60 and 120
    "nothing-held": ("bipeds", "if (sim_substep_count() <= 1 || object_index == NONE)", "if (TRUE)", "ride"),
    # the rider held to the tick, its deck left in the sub-steps (moved after the rider's carry)
    "deck-not-held": ("bipeds", "return biped_riding_lanes_hold(vehicle_index, TRUE);", "return FALSE;", "ride"),
    # a jump at 120 with the riders sub-stepped
    "jump-substepped": ("bipeds", "if (sim_substep_count() <= 1 || object_index == NONE)", "if (TRUE)", "jump"),
    # a rider sub-stepped where its deck rolls over
    "flip-substepped": ("bipeds", "if (sim_substep_count() <= 1 || object_index == NONE)", "if (TRUE)", "flipped"),
    # a rider in the air it left not held (its window's fall in the sub-steps)
    "window-not-held": ("bipeds", "if (biped && biped_riding_deferred(player->unit_index))",
                        "if (biped && biped_riding_deferred(player->unit_index) && "
                        "biped_try_and_get(player->unit_index)->biped.riding_ticks == RIDING_TICKS)", "step-off"),
    # the deck's motion not added back as the rider leaves it (no momentum kept)
    "no-momentum": ("riding", "\t\telse\n\t\t\tadd_vectors3d(new_velocity, frame, new_velocity);", "", "step-off"),
    # the deck's turn of the view not kept
    "no-turn": ("riding", "step->dyaw = vehicle_riding_yaw(&state->vehicle_forward, &state->vehicle_up, "
                "&vehicle->forward, &vehicle->up);", "step->dyaw = 0.f;", "ride"),
}


def generated(control=None):
    sources = {"riding": read(RIDING), "bipeds": read(BIPEDS)}
    if control:
        owner, before, after, _ = CONTROLS[control]
        sources[owner] = mutated(sources[owner], before, after)
    riding = sources["riding"]
    types = engine() + "\n" + without_includes(read("port/linux/game/vehicle_riding.h")) + "\n"
    for name in ("RIDING_NODE_SHIFT", "RIDING_NODE_TILT", "RIDING_SNAP_STEP"):
        types += re.search(r"^#define " + name + r" .*$", riding, re.M).group(0) + "\n"
    for name in ("riding_teleport_step", "riding_moved_alone"):
        types += re.search(r"^static real " + name + r" = .*$", riding, re.M).group(0) + "\n"
    lanes = re.search(r"^static struct\n\{\n\tlong time;.*?\} biped_riding_lanes = \{ NONE, 0 \};\n",
                      sources["bipeds"], re.M | re.S).group(0)
    code = "".join(function(riding, name) + "\n" for name in RIDING_FUNCTIONS)
    code += lanes + "".join(function(sources["bipeds"], name) + "\n" for name in LANE_FUNCTIONS)
    return (("types.inc", types), ("under_test.inc", code))


def test_camera_turn_drawn_with_the_tick():
    # (the frames between ticks draw the deck's turn the rest of the way: riding_rates.c's frames check models this
    # expression, of the tick's fraction, which a rider and its deck share at every rate as both move in the tick)
    camera = read("port/linux/game/render_interpolation.c")
    assert "vehicle_riding_turn_vector(&direct->forward, -(1.0f - render_interpolation_fraction())*kept_dyaw);" in camera
    assert "interpolation_fraction = game_time_get_tick_fraction();" in camera


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("riding_rates", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    case = CONTROLS[control][-1]
    status, output = run(build("riding_rates", generated(control)), case)
    assert status == CHECK_FAILED and "check failed:" in output, f"{control}: {status}: {output}"
