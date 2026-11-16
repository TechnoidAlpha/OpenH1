"""A text box's own text (menu_functions.c's text_set_length) in a buffer at least as long as each text it is given:
one box is given different lengths (Choose Mission's panel line, 64 on HALO and 256 on CUSTOM) and the game sizes a
string list's text to the string, so a buffer made only the first time was overrun, and the widgets' pool failed its
check as the screen was backed out of (#553)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["longer", "game-sized", "same-length"]

GROWN = "\tbuffer = ui_widget_realloc(text_box->parameters.text_box.text, length * sizeof(wchar_t), __FILE__, __LINE__);\n"

# a fault in text_set_length, and the cases that must catch it
NEGATIVE_CONTROLS = {
    # the code before #553: the buffer made only the first time, at the first length given
    "made-once": ((GROWN, "\tbuffer = text_box->parameters.text_box.text ? text_box->parameters.text_box.text :\n"
                          "\t\tui_widget_realloc(NULL, length * sizeof(wchar_t), __FILE__, __LINE__);\n"),
                  ("longer", "game-sized")),
    # a new block every time: the old one never given back
    "always-new": ((GROWN, "\tbuffer = ui_widget_realloc(NULL, length * sizeof(wchar_t), __FILE__, __LINE__);\n"),
                   ("longer", "same-length")),
}


def generated(fault=None):
    code = function(read("port/linux/game/menu_functions.c"), "text_set_length")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("text_box_lengths", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, cases = NEGATIVE_CONTROLS[control]
    for case in cases:
        status, output = run(build("text_box_lengths", generated(fault)), case)
        assert status == CHECK_FAILED, f"{control} was not caught by '{case}': {output}"
