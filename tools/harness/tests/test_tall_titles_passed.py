"""A tall screen's column titles are the pointer's alone with a controller's prompts (menu_screen_tall.inc's
tall_titles_passed; the controller audit's "A sorts the list instead of joining"): the d-pad passes over them, down
from the tabs to the first row, up from the rows to the tabs; the keyboard's arrows and the pointer keep them."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["down", "up", "kept"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the code before the change: the titles a stop of the d-pad
    "titles-a-stop": (("\tif (focused && focused == tall->titles &&", "\tif (FALSE && focused == tall->titles &&"),
                      "down"),
    # every way taken as down
    "always-down": (("\t\t\tdown |= each == tall->focus_last;", "\t\t\tdown = TRUE;"), "up"),
    # the hidden and disabled blocks not passed over
    "hidden-taken": (("\t\t\telse if (each->visible && !each->disabled)", "\t\t\telse if (TRUE)"), "up"),
    # with the keyboard's prompts too
    "keyboard-passed": (("\t\t!halo_keyboard_prompts() && ", "\t\t"), "kept"),
}


def generated(fault=None):
    code = function(read("port/linux/game/menu_screen_tall.inc"), "tall_titles_passed")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][0])
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("tall_titles_passed", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("tall_titles_passed", generated(control)), NEGATIVE_CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
