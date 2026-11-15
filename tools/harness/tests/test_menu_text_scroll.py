"""A menu's line too long for its box (issue 899; port/linux/game/menu_functions.c): while its row has the focus the
whole line scrolls (still at its start, moved at a steady speed, still at its end, again), cut to its box; while not,
it is cut short with "..."; a line a fit cut short keeps its whole text for it; the settings panel wraps a long value
onto more lines, whole; and none of it is measured again as it is drawn each frame. ui_widget.c draws each text box's
and spinner's line through it, the high-res text by fractions of a unit (rasterizer_text.c)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["scroll", "row", "as_it_is", "fitted", "refocus", "wrap"]

NEGATIVE_CONTROLS = {
    # the line moves as soon as its row has the focus: no pause at its start
    "no-first-pause": (("\tif (at < MENU_TEXT_SCROLL_PAUSE)\n\t\treturn 0.0f;\n", ""), "scroll"),
    # the line jumps a unit at a time rather than moving smoothly
    "whole-units": (("return (real)(at - MENU_TEXT_SCROLL_PAUSE) * (real)MENU_TEXT_SCROLL_SPEED / 1000.0f;",
                     "return (real)((at - MENU_TEXT_SCROLL_PAUSE) * MENU_TEXT_SCROLL_SPEED / 1000 / 8 * 8);"),
                    "scroll"),
    # every row's line scrolls, focused or not
    "unfocused-scrolls": (("\tif (focused)\n\t{\n\t\t*scroll", "\tif (TRUE)\n\t{\n\t\t*scroll"), "row"),
    # a row's line measured again each frame it is drawn
    "measured-each-frame": (("line->width != width || !menu_text_same(line->shown, text))",
                             "line->width != width || TRUE)"), "row"),
    # a screen's own long text (not a row's, not cut by a fit) cut or scrolled
    "any-text-scrolls": (("\t\tif (!row)\n\t\t\treturn NULL;\n", ""), "as_it_is"),
    # a fit measures its text again each frame
    "fit-measured-each-frame": (("\tif (line && line->fitted && line->font == font && line->width == width && text &&",
                                 "\tif (FALSE && line && line->fitted && line->font == font && line->width == width && "
                                 "text &&"), "fitted"),
    # a fit forgets the whole of what it cut short
    "fit-not-kept": (("\tif (text && !menu_text_same(fitted, text))\n\t{",
                      "\tif (FALSE && text && !menu_text_same(fitted, text))\n\t{"), "fitted"),
    # a row hidden and shown again goes on where it was
    "hidden-goes-on": ((" || now - line->drawn_time > MENU_TEXT_SCROLL_GONE", ""), "refocus"),
    # the panel cuts its value to one line
    "panel-cuts": (("menu_text_wrap_lines(font, text, width, most, wrapping->wrapped)",
                    "menu_text_wrap_lines(font, text, width, 1, wrapping->wrapped)"), "wrap"),
}


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    defines = "\n".join(re.search(rf"^#define {name} .+$", menus, re.M).group(0) for name in (
        "ROW_TEXT_LENGTH", "SETTINGS_PANEL_TEXT_LENGTH", "SETTINGS_PANEL_VALUE_LENGTH","MENU_TEXT_ELLIPSIS_LENGTH", "MENU_TEXT_SCROLL_PAUSE",
        "MENU_TEXT_SCROLL_SPEED", "MENU_TEXT_LINES", "MENU_TEXT_LINE_PROBES", "MENU_TEXT_LINE_LENGTH",
        "MENU_TEXT_SCROLL_GONE"))
    code = "\n\n".join([defines, structure(menus, "menu_text_line"), structure(menus, "settings_panel_wrapping"),
                        "static struct menu_text_line menu_text_lines[MENU_TEXT_LINES];"] +
                       [function(menus, name) for name in (
                           "settings_wide_copy", "menu_text_same", "menu_text_one_line", "settings_text_wrap",
                           "menu_text_cut", "menu_text_fit", "menu_text_wrap_lines", "menu_text_scroll_offset",
                           "menu_text_line_of", "menu_text_box_fit_to", "pc_menu_text_line", "settings_panel_wrap")])
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_text_scroll", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("menu_text_scroll", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_panel_value_place():
    """the panel's value is SETTINGS_PANEL_VALUE_UNDER_NAME under its name, as tools/port_settings.py places it, and
    its boxes have room for their lines"""
    menus = read("port/linux/game/menu_functions.c")
    settings = read("tools/port_settings.py")
    name_y = int(re.search(r'panel_name" x="\{left\}" y="\{top \+ (\d+)\}"', settings).group(1))
    value_y = int(re.search(r'panel_value" x="\{left\}" y="\{top \+ (\d+)\}"', settings).group(1))
    under = int(re.search(r"#define SETTINGS_PANEL_VALUE_UNDER_NAME (\d+)", menus).group(1))
    assert value_y - name_y == under
    more = re.search(r"SETTINGS_PANEL_NAME_MORE, SETTINGS_PANEL_VALUE_MORE = (\d+) \* 20, (\d+) \* 26", settings)
    assert int(more.group(1)) + 1 == int(re.search(r"#define SETTINGS_PANEL_NAME_LINES (\d+)", menus).group(1))
    assert int(more.group(2)) + 1 == int(re.search(r"#define SETTINGS_PANEL_VALUE_LINES (\d+)", menus).group(1))
