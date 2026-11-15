"""A screen opened again from the menus' history focuses the widget that opened the next one
(ui_widget.c's widget_instance_set_focused_child_by_index): the map and gametype lists (up_down_tabs_children)
open it with their own handler, so they come back as that widget, and must come back focused on a row of their own
(the map used last, else their first that takes events), so that A and Start act at once (the controller audit's
"focus disappears after Back": the list itself kept the focus, no row lit, and A did nothing until the D-pad
moved)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["kept", "first", "other-list"]

NAMES = ("widget_instance_get_topmost_parent", "widget_instance_port_is_label",
         "widget_instance_find_by_tag_index_recursive", "widget_instance_can_receive_events",
         "widget_instance_refocus_passthrough", "widget_instance_give_focus_directly",
         "widget_instance_port_refocus_list", "widget_instance_set_focused_child_by_index")

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the code before the change: the list given the focus back alone
    "list-not-refocused": ("widget_instance_set_focused_child_by_index",
                           ("\t\t\t\twidget_instance_port_refocus_list(child, kept); /* port */\n", ""), "kept"),
    # the row the list had as it was made forgotten: its first row instead
    "row-forgotten": ("widget_instance_port_refocus_list",
                      ("for (child = list->child, index = 0; child && child != kept; child = child->next, index++)",
                       "for (child = NULL, index = 0; child && child != kept; child = child->next, index++)"), "kept"),
    # any list given a row, not only those the d-pad steps through their children
    "every-list": ("widget_instance_port_refocus_list",
                   ("\tif (!TEST_FLAG(definition->flags, _widget_dpad_updown_tabs_thru_children_bit) &&\n"
                    "\t\t!TEST_FLAG(definition->flags, _widget_dpad_leftright_tabs_thru_children_bit))",
                    "\tif (definition->flags < 0)"), "other-list"),
}


def generated(fault=None):
    widgets = read("source/interface/ui_widget.c")
    code = {name: function(widgets, name) for name in NAMES}
    code["widget_instance_can_handle_events"] = function(widgets, "widget_instance_can_handle_events").replace(
        "static __inline", "static", 1)
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        code[where] = mutated(code[where], *change)
    order = ("widget_instance_can_handle_events",) + NAMES
    under_test = "\n".join(code[name] for name in order) + "\n"
    under_test = under_test.replace("struct widget_instance *widget_instance_get_topmost_parent(",
                                    "static struct widget_instance *widget_instance_get_topmost_parent(", 1)
    return (("under_test.inc", under_test),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("history_list_focus", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("history_list_focus", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
