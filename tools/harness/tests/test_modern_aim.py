"""A controller's aim (input.aim_style, source/game/player_control.c's player_control_stick_aim), fed stick pushes at
0, 30, 45, 60 and 90 degrees and several lengths: Classic is the original's to the bit (a diagonal faster, and
accelerating); Modern turns as fast whichever way the stick points, as Classic straight across, and accelerates only
near side to side; its tunables move its zone; the look stick's round dead zone (source/input/input_xbox.c) reads a
full diagonal as a full push."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["classic", "modern_speed", "modern_acceleration", "modern_tunables", "round_dead_zone"]

CONTROLS = [
    # no angle gate: a full diagonal accelerates under Modern, as under Classic
    (" && absolute_pitch <= absolute_yaw * aim->acceleration_tangent;", ";", "modern_acceleration"),
    # the push's length the square's (the per-axis stretch kept): a diagonal turns faster
    ("\tpush = MIN(MAX(absolute_yaw, absolute_pitch), 1.f);", "\tpush = MIN(length, 1.f);", "modern_speed"),
    # each direction curved on its own, as Classic: the speed changes with the angle
    ("\t*curved_pitch = speed * (pitch / length);",
     "\t*curved_pitch = evaluate_piecewise_linear_function(look_function_count, look_function, pitch);",
     "modern_speed"),
    # Modern's threshold ignored
    ("\t\tpegging_threshold = aim->acceleration_threshold;", "", "modern_tunables"),
    # Classic touched: its diagonal no longer stretched to the square
    ("\tif (absolute_pitch > 0.1f && absolute_yaw > 0.1f)", "\tif (0)", "classic"),
    # the round dead zone's push not taken from its edge
    ("(MIN(length, (real)SHORT_MAX) - dead_range)", "(MIN(length, (real)SHORT_MAX))", "round_dead_zone"),
]


def generated(fault=None):
    control = read("source/game/player_control.c")
    xbox = read("source/input/input_xbox.c")
    code = "\n".join([
        function(control, "evaluate_piecewise_linear_function"),
        function(control, "player_control_clamp_look"),
        structure(control, "player_control_aim"),
        function(control, "player_control_stick_aim"),
        function(xbox, "fix_round_dead_zone"),
    ]) + "\n"
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_modern_aim(case):
    status, output = run(build("modern_aim", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_modern_aim_negative_control(before, after, case):
    status, output = run(build("modern_aim", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_one_aim_for_the_tick_and_the_sub_step():
    # the frame's turn and a sub-step's both take the stick's look and its acceleration from the one aim
    code = read("source/game/player_control.c")
    assert code.count("player_control_stick_look(constants, ") == 2
    assert code.count("player_control_look_acceleration(constants, control)") == 2
    assert "look_pegging_threshold" not in function(code, "get_local_player_input_blob")
    assert "look_pegging_threshold" not in function(code, "player_control_substep_read")
