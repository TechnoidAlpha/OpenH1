"""The first-person body's lower body's own heading (port/linux/game/first_person_legs.c's
first_person_legs_lower_turn, #772): standing, it stays planted until the aim leaves a dead zone, then turns after
it, smoothly, never twisted past its most; walking, it faces the way the player moves, walking forward or backward,
or sidesteps, so that each walk goes the way the player moves; turned by each frame's time, the same at any frame
rate."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["planted", "catch_up", "rates", "walks", "keep"]
CONTROLS = [
    # no dead zone: the lower body turns after every turn of the aim, its feet sliding
    ("\t\tif ((real)fabs(off) > FIRST_PERSON_LEGS_DEAD_ZONE)\n", "\t\tif (TRUE)\n", "planted"),
    # turned a part of the way each frame: faster at a higher frame rate
    ("\t\treal decay = (real)exp(-w * seconds);", "\t\treal decay = 0.8f;", "rates"),
    # forward and backward walks only: a sideways way walked forward at an angle, the feet sliding
    ("for (lower->walk = 0, walk = 1; walk < 4; walk++)", "for (lower->walk = 0, walk = 1; walk < 2; walk++)",
     "walks"),
    # no walk kept: wavering about halfway, the legs swing between two walks
    ("0.25f * _pi + FIRST_PERSON_LEGS_WALK_KEEP)\n", "0.25f * _pi)\n", "keep"),
]


def generated(fault=None):
    code = read("port/linux/game/first_person_legs.c")
    defines = "\n".join(line for line in re.findall(r"^#define FIRST_PERSON_LEGS_\w+ .*$", code, re.M))
    limits = re.search(r"^enum\s*\{\s*FIRST_PERSON_LEGS_MAXIMUM_NODES[^}]*\};", code, re.M).group(0)
    text = "\n".join([defines, limits, structure(code, "first_person_legs_lower")] +
                     [function(code, name) for name in ["first_person_legs_angle", "first_person_legs_lower_turn"]])
    if fault:
        text = mutated(text, *fault)
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("first_person_legs_lower", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("first_person_legs_lower", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_only_the_drawing():
    # the lower body is the local player's drawing alone: nothing of the unit is written, nor the game's seeds taken
    code = read("port/linux/game/first_person_legs.c")
    for name in ["first_person_legs_lower_turn", "first_person_legs_lower_get", "first_person_legs_moving_pose"]:
        body = function(code, name)
        assert not re.search(r"\b(unit|biped|object)->[\w.]+\s*[-+*/]?=[^=]", body), name
        assert "random" not in body, name
