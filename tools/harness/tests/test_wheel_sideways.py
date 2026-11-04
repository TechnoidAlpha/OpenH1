"""The mouse's wheel on Choose Mission's cards (#542): a card a notch, not too fast (wheel_sideways.c: ui_widget.c's
ui_mouse_wheel_sideways_steps), the row flagged wheel_sideways (tools/port_settings.py, the checked-in file), the
flag's name menu_tags.c's bit, and the wheel taking a row so flagged sideways (ui_mouse_wheel_widget,
ui_widgets_process_mouse)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402
from harness import menu_layout  # noqa: E402

CAMPAIGN = menu_layout.port_settings.CAMPAIGN

CASES = ["notch-a-card", "many-at-once", "trackpad-flick", "no-notch"]

# a fault in ui_mouse_wheel_sideways_steps, and the case that must catch it
NEGATIVE_CONTROLS = {
    # every notch a card, however fast (a trackpad's flick runs the row to its end)
    "no-interval": ("(*last != NONE && now - (unsigned long)*last < UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS)", "FALSE"),
    # the notches turned at once each a card
    "all-at-once": ("\treturn 1;\n}", "\treturn notches;\n}"),
    # the time never kept
    "time-not-kept": ("\t*last = (long)now;\n", ""),
    # the interval from the first notch ever, not the last step: one card only, ever
    "first-only": ("(*last != NONE && now - (unsigned long)*last <", "(*last != NONE && 0 <"),
    # a notch with no time kept stepping nothing
    "notch-dropped-first": ("\tif (notches <= 0 ||", "\tif (notches <= 1 ||"),
}

CONTROL_CASES = {"no-interval": "many-at-once", "all-at-once": "many-at-once", "time-not-kept": "many-at-once",
                 "first-only": "notch-a-card", "notch-dropped-first": "notch-a-card"}


def generated(fault=None):
    widgets = read("source/interface/ui_widget.c")
    code = function(widgets, "ui_mouse_wheel_sideways_steps")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault])
    config = "#define ABS(x) ((x) < 0 ? -(x) : (x))\n"
    interval = constant(widgets, "UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS")
    config += f"#define UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS {interval}\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("wheel_sideways", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("wheel_sideways", generated(control)), CONTROL_CASES[control])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_the_row_is_flagged():
    """Choose Mission's row is flagged wheel_sideways, in the generator and the checked-in file; nothing else is (the
    other sideways lists tab with the d-pad's flags, which the wheel already takes; a vertical list with left and
    right on its rows keeps its wheel up and down)."""
    every = menu_layout.widgets()
    assert "wheel_sideways" in every[f"{CAMPAIGN}/mission_cards"].attributes["flags"].split()
    flagged = sorted(name for name, widget in every.items()
                     if "wheel_sideways" in widget.attributes.get("flags", "").split())
    assert flagged == [f"{CAMPAIGN}/mission_cards"]
    generated_text = "".join(menu_layout.port_settings._missions())
    assert re.search(r'name="[^"]*mission_cards"[^>]*flags="[^"]*\bwheel_sideways\b', generated_text)


def test_flag_bit_is_its_name():
    """The flag's bit (WIDGET_FLAG_WHEEL_SIDEWAYS) is its name's place in menu_tags.c's widget_flag_names."""
    tags = read("port/linux/game/menu_tags.c")
    names = re.search(r"widget_flag_names\[\] =\s*\{(.*?)\};", tags, re.S).group(1)
    names = re.sub(r"/\*.*?\*/", "", names, flags=re.S)
    order = re.findall(r'"(\w+)"', names)
    assert order.index("wheel_sideways") == constant(tags, "WIDGET_FLAG_WHEEL_SIDEWAYS")
    assert order.index("keep_ends") == constant(tags, "WIDGET_FLAG_KEEP_ENDS")


def test_the_wheel_takes_the_row_sideways():
    """The wheel's widget may be a row flagged so (not for a touch drag), and its presses are then left and right,
    as many as ui_mouse_wheel_sideways_steps gives."""
    widgets = read("source/interface/ui_widget.c")
    chooser = function(widgets, "ui_mouse_wheel_widget")
    assert "(!skip_settings && ui_widget_port_wheel_sideways(definition->flags))" in chooser
    process = function(widgets, "ui_widgets_process_mouse")
    sideways = process[process.index("if (widget && !touch &&\n\t\t\t\tui_widget_port_wheel_sideways"):]
    sideways = sideways[:sideways.index("}") + 1]
    assert "back = _widget_event_dpad_left;" in sideways and "forward = _widget_event_dpad_right;" in sideways
    assert "steps = ui_mouse_wheel_sideways_steps(" in sideways
    assert "for (step = 0; step < steps && step < 4" in process
