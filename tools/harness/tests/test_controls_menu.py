"""Controls Setup's rows (port/linux/game/menu_functions.c's control_capture_begin and controls_update): each control's
two bindings, the one set chosen by left and right, by a click on it, or by the pointer over it (ui_widget.c's
ui_widget_port_take_click and ui_widget_port_take_point), where the bindings are drawn (the wide layout's move too)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["click", "keys", "hover", "wide"]

NEGATIVE_CONTROLS = {
    "click-ignored": (("\t\tcontrols_screen.slot = control_slot_at(row, x);\n", ""), "click"),
    "pointer-ignored": (("\t\tcontrols_screen.slot = control_slot_at(focused_row, x);\n", ""), "hover"),
    "pointer-while-setting": ((" && controls_screen.capturing_control == NONE)", ")"), "hover"),
    "click-always-first": (("x >= widget_screen_x(second)", "x < 0"), "click"),
    "not-wide": ((" + ui_widget_port_wide_offset(widget->definition_tag_index)", ""), "wide"),
}


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    start = menus.index("#define CONTROL_BINDINGS")
    declarations = menus[start:menus.index("/* a setting's bindings", start)]
    code = declarations + "\n\n" + "\n\n".join(function(menus, name) for name in (
        "widget_screen_x", "control_row", "control_of_row", "control_slot_at", "control_capture_begin",
        "controls_update"))
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("controls_menu", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("controls_menu", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_rows_layout():
    """The screen's rows have their two bindings where the test has them, and A (a click's too) begins a binding."""
    text = read("port/assets/menus/ce/main_menu.settings_select.player_setup.player_profile_edit.controls_setup.xml")
    row = re.search(r'<widget name="[^"]*/op_command_1"[^>]*>(.*?)</widget>', text, re.S).group(1)
    assert '<on event="a" run="controls begin binding"/>' in row
    assert re.findall(r'<child widget="[^"]*/command_binding" x="(\d+)"/>', row) == ["200", "356"]
