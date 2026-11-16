"""The local player's own flashlight in the first person on foot, where no first-person weapon carries it (none in
hand, or zoomed): it shines along the view drawn, from where the body has it seen from the view between the ticks
(port/linux/game/render_interpolation.c's render_interpolation_view_flashlight, #589), not from the head's marker a
tick behind the view and turned with the head's look; any other unit or view is left as the game has it."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["follows_the_view", "still", "left_alone"]
CONTROLS = [
    # the body's axis kept: the pool swings with the head's look
    ("\t*forward = to->forward;\n", "\n", "follows_the_view"),
    # the origin left on the body a tick behind
    ("\tto = &drawn_views[local_player_index];", "\tto = &blended_views[local_player_index];", "follows_the_view"),
    # another player's flashlight moved to this view
    ("player_control_get_unit_index(local_player_index) != unit_index ||", "FALSE ||", "left_alone"),
    # a seat's (the vehicle's own view) moved too
    ("object_get(unit_index)->object.parent_object_index != NONE)", "FALSE)", "left_alone"),
]


def generated(fault=None):
    code = function(read("port/linux/game/render_interpolation.c"), "render_interpolation_view_flashlight")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("view_flashlight", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("view_flashlight", generated((before, after))), case)
    assert status == CHECK_FAILED, output
