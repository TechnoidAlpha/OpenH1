"""Key Bindings' rows (port/linux/game/menu_functions.c's control_capture_begin and controls_update): each control's
two bindings, the one set chosen by left and right, by a click on it, or by the pointer over it (ui_widget.c's
ui_widget_port_take_click and ui_widget_port_take_point), where the bindings are drawn (the wide layout's move too)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["click", "keys", "hover", "wide", "keep-a-key"]

NEGATIVE_CONTROLS = {
    "click-ignored": (("\t\tcontrols_screen.slot = control_slot_at(row, x);\n", ""), "click"),
    "pointer-ignored": (("\t\tcontrols_screen.slot = control_slot_at(focused_row, x);\n", ""), "hover"),
    "pointer-while-setting": ((" && controls_screen.capturing_control == NONE)", ")"), "hover"),
    "click-always-first": (("x >= widget_screen_x(second)", "x < 0"), "click"),
    "not-wide": ((" + ui_widget_port_wide_offset(widget->definition_tag_index)", ""), "wide"),
    # a game control's only key taken for another control
    "only-key-taken": (("if (other != control && control_left_without_key(other, NONE, name))", "if (FALSE)"),
                       "keep-a-key"),
    # a game control's only key cleared
    "only-key-cleared": (("\t\tif (control_left_without_key(control, slot, NULL))", "\t\tif (FALSE)"), "keep-a-key"),
    # a control with no key at all refusing every change
    "no-key-blocks": (("\treturn taken;", "\treturn taken || TRUE;"), "keep-a-key"),
    # every control the game's
    "all-essential": (("\tif (!controls[control].essential)\n\t\treturn FALSE;\n", ""), "keep-a-key"),
    # no why
    "refusal-unexplained": (("refused ? 2 : ", ""), "keep-a-key"),
}


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    start = menus.index("#define CONTROL_BINDINGS")
    declarations = menus[start:menus.index("/* a setting's bindings", start)]
    code = declarations + "\n\n" + "\n\n".join(function(menus, name) for name in (
        "widget_screen_x", "control_row", "control_of_row", "control_binding_box", "control_slot_at",
        "control_capture_begin", "control_left_without_key", "control_refuse", "control_capture_poll",
        "controls_update", "controls_help_index"))
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
    """Key Bindings' rows have their two bindings, the first left of the second, and A (a click's too) begins a
    binding."""
    text = read("port/assets/menus/ce/main_menu.settings_select.player_setup.player_profile_edit.controls_setup.xml")
    row = re.search(r'<widget name="[^"]*/op_command_1"[^>]*>(.*?)</widget>', text, re.S).group(1)
    assert '<on event="a" run="controls begin binding"/>' in row
    first = re.findall(r'<child widget="[^"]*/command_binding" x="(\d+)"/>', row)
    second = re.findall(r'<child widget="[^"]*/command_binding_second" x="(\d+)"/>', row)
    assert len(first) == 1 and len(second) == 1 and int(first[0]) < int(second[0])
