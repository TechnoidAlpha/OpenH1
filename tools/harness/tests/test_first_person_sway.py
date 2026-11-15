"""The first-person weapon's sways between their steps (first_person_weapons.c's first_person_weapon_springs_at): the
position's (strafing, walking) and the turning's drawn from where each was before its step to where the step left it,
so that the frames meet each step where it begins (first_person_weapon_update's own lines step them here); the
firing push back's on the way to its next step, a shot's kick at once."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, inline, mutated, read, run, structure  # noqa: E402

CASES = ["strafing", "turning", "fired"]
CONTROLS = [
    # the sway moved on by its velocity alone, as before: it jumps at each step while it speeds up or slows down
    ("position->i= before->position.i+(first_person_weapon->position.i-before->position.i)*phase;",
     "position->i= first_person_weapon->position.i+first_person_weapon->position_velocity.i*phase;", "strafing"),
    ("turning->i= before->turning.i+(first_person_weapon->turning.i-before->turning.i)*phase;",
     "turning->i= first_person_weapon->turning.i+first_person_weapon->turning_velocity.i*phase;", "turning"),
    # where the sway was before its step not kept
    ("first_person_weapon_sways_before[local_player_index].position= first_person_weapon->position;", "", "strafing"),
    # the push back held where its step left it until the next: the kick a tick late
    ("*firing_push_back= first_person_weapon->firing_push_back+\n"
     "\t\t(next_firing_push_back-first_person_weapon->firing_push_back)*phase;",
     "*firing_push_back= first_person_weapon->firing_push_back;", "fired"),
]


def sway_block(code):
    """first_person_weapon_update's step of the sways, from where it keeps them before the step to the push back's"""
    start = code.index("\t\t/* port: (where the frames until the next step draw the sways from:")
    end = code.index("\t\tif (first_person_weapon->interpolation_frame_count>0)", start)
    return code[start:end]


def generated(fault=None):
    code = read("source/interface/first_person_weapons.c")
    defines = "".join(re.search(rf"^#define {name}\b.*\n", code, re.M).group(0) for name in (
        "FIRST_PERSON_WEAPON_FIRING_PUSH_BACK_VELOCITY", "FIRST_PERSON_WEAPON_FIRING_PUSH_BACK_ACCELERATION",
        "FIRST_PERSON_WEAPON_FIRING_PUSH_BACK_SPEED"))
    config = defines + structure(code, "first_person_weapon_sways") + "\n"
    step = sway_block(code)
    springs_at = function(code, "first_person_weapon_springs_at")
    if fault:
        before, after, _ = fault
        if before in step:
            step = mutated(step, before, after)
        else:
            springs_at = mutated(springs_at, before, after)
    text = "\n".join([
        "static real square_root(real x)\n{\n\treturn sqrtf(x);\n}\n",
        inline(read("source/math/real_math.h"), "signed_angular_difference"),
        function(read("source/math/real_math.c"), "accelerate_to_position"),
        "static void sways_step(struct first_person_weapon *first_person_weapon, struct unit_datum *unit,\n"
        "\tshort local_player_index)\n{\n" + step + "}\n",
        springs_at,
    ])
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("first_person_sway", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("first_person_sway", generated((before, after, case))), case)
    assert status == CHECK_FAILED, output
