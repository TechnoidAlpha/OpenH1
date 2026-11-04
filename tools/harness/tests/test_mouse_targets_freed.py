"""The menus' mouse targets go with the widgets freed, and the pointer's clicks with the screen they were made on
(ui_widget.c's ui_mouse_forget_widget, ui_mouse_let_go_if_stale): Save and Quit's main menu, loaded at the next
frame's start, took a click resolved against the pause menu's freed widgets (#513)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

CASES = ["freed", "let-go", "drawn"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the code before the change: a freed widget's targets kept, to be resolved
    "targets-kept": ("ui_mouse_forget_widget", ("\tui_mouse_target_count = kept;\n", ""), "freed"),
    # the screen's going not noted: the next screen takes the click
    "never-stale": ("ui_mouse_forget_widget", ("\t\tui_mouse_targets_stale = TRUE;\n", ""), "let-go"),
    # a widget freed while the menus draw lets the frame's clicks go
    "stale-while-drawing": ("ui_mouse_forget_widget", ("\tif (!ui_mouse_rendering)\n\t", "\t"), "drawn"),
    # the click kept, the rest let go
    "click-kept": ("ui_mouse_let_go_if_stale", ("\tui_mouse_click_pending = FALSE;\n", ""), "let-go"),
}

NAMES = ("ui_mouse_forget_widget", "ui_mouse_let_go_if_stale", "ui_mouse_target_at")


def generated(fault=None):
    widgets = read("source/interface/ui_widget.c")
    code = {name: function(widgets, name) for name in NAMES}
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        code[where] = mutated(code[where], *change)
    config = f"#define UI_MOUSE_MAXIMUM_TARGETS {constant(widgets, 'UI_MOUSE_MAXIMUM_TARGETS')}\n"
    config += f"#define UI_MOUSE_MAXIMUM_PRESSES {constant(widgets, 'UI_MOUSE_MAXIMUM_PRESSES')}\n"
    config += structure(widgets, "ui_mouse_target") + "\n"
    # (ui_mouse_target_at first: the others are taken in their order in the game)
    under_test = code["ui_mouse_target_at"] + "\n" + code["ui_mouse_forget_widget"] + "\n" + \
        code["ui_mouse_let_go_if_stale"] + "\n"
    return (("config.inc", config), ("under_test.inc", under_test))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mouse_targets_freed", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("mouse_targets_freed", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
