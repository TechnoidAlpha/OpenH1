"""The player's own view on foot between the steps (render_interpolation.c's render_interpolation_newest_camera): its
eye moved on by its crouch's change for the time since the newest step (bipeds.c's biped_eye_rise_ahead), so a frame
a fraction of a tick after a step draws the eye that fraction of the way to the next tick's, as the crouch changes it
(biped_physics_prepare's own lines), and never past where it stops."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["crouching", "standing", "reversed", "still"]
CONTROLS = [
    # the eye moved the other way
    ("return (biped->biped.crouch - ahead) *", "return (ahead - biped->biped.crouch) *", "crouching"),
    # on past where the crouch stops
    ("ahead = MIN(biped->biped.crouch + change, 1.f);", "ahead = biped->biped.crouch + change;", "crouching"),
    # down whatever the seat
    ("if (biped->unit.animation.base_seat_index == _unit_base_seat_crouch)", "if (1)", "standing"),
    # the whole eye's height, not the crouch's part of it
    ("(definition->biped.standing_camera_height - definition->biped.crouching_camera_height)",
     "definition->biped.standing_camera_height", "crouching"),
]


def crouch_block(code):
    """biped_physics_prepare's change of the crouch, a tick's: its block, by matching braces"""
    start = code.index("\t{\n\t\treal crouch_delta;")
    end = code.index("{", start) + 1
    depth = 1
    while depth:
        depth += (code[end] == "{") - (code[end] == "}")
        end += 1
    return code[start:end]


def generated(fault=None):
    code = read("source/units/bipeds.c")
    config = enum_with(read("source/units/units.h"), "_unit_base_seat_crouch") + "\n"
    rise = function(code, "biped_eye_rise_ahead")
    if fault:
        rise = mutated(rise, *fault)
    text = "\n".join([
        "static void crouch_tick(struct biped_datum *biped, struct biped_definition *definition)\n{\n"
        "\tstruct { real crouch_velocity; } physics;\n\n" + crouch_block(code) + "\n\t(void)physics;\n}\n",
        rise,
    ])
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("crouch_eye", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("crouch_eye", generated((before, after))), case)
    assert status == CHECK_FAILED, output
