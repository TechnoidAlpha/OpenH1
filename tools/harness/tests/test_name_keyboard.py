"""The name keyboard's screen in the port's menus' look (source/interface/virtual_keyboard.c, issue 667): its footer's
prompts on the screen at 4:3, 16:10 and 16:9, in the footer's band at the wide layout's right, apart; a click on a
prompt where it is drawn is BACK's cancel or ENTER's Done, and the touch targets name those boxes; each key's look
(chosen, pressed, held on). And the screen drawn in that look where the port's menus are up: the backdrop, title and
panels its own, not the Xbox's 4:3 picture, and its prompts the device's (draw_string_and_hack_in_icons)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

SOURCE = "source/interface/virtual_keyboard.c"
CASES = ["prompts", "click", "look"]

NEGATIVE_CONTROLS = {
    # the prompts over each other
    "prompts-overlap": (("enter->x1 = (short)(back->x0 - VIRTUAL_KEYBOARD_PORT_PROMPT_GAP);",
                         "enter->x1 = (short)(back->x0 + VIRTUAL_KEYBOARD_PORT_PROMPT_GAP);"), "prompts"),
    # the prompts where the 640's right is, not the wide layout's
    "prompts-at-640": (("back->x1 = (short)(right - VIRTUAL_KEYBOARD_PORT_PROMPT_MARGIN);",
                        "back->x1 = (short)(640 - VIRTUAL_KEYBOARD_PORT_PROMPT_MARGIN);"), "prompts"),
    # a click looked for at the Xbox's picture's legend, not the prompt drawn
    "click-xbox-legend": (("if (x >= keyboard_enter_legend_rect.x0 && x < keyboard_enter_legend_rect.x1 &&",
                           "if (x >= keyboard_enter_legend_xbox_rect.x0 && x < keyboard_enter_legend_xbox_rect.x1 &&"),
                          "click"),
    # Shift's held state not shown
    "held-not-shown": (("(key_index == _vkey_shift && virtual_keyboard_globals.shift_active) ||", ""), "look"),
}


def generated(fault=None):
    source = read(SOURCE)
    config = "\n".join(enum_with(source, member) for member in
                       ("VIRTUAL_KEYBOARD_ROW_COUNT", "_vkey_done", "_event_key_select",
                        "VIRTUAL_KEYBOARD_PORT_TITLE_LINE", "_virtual_keyboard_port_key_plain"))
    for name in ("virtual_keyboard_layout_table", "keyboard_rect"):
        start = source.index(f"{name}[")
        start = source.rindex("static", 0, start)
        config += "\n" + source[start:source.index("\n};", start) + 3]
    config += "\n" + "\n".join(re.findall(r"^static rectangle2d (?:const )?keyboard_\w+_legend_\w*rect=[^\n]+;$",
                                          source, re.M)) + "\n"
    code = "\n\n".join(function(source, name) for name in
                       ("virtual_keyboard_port_prompt_boxes", "virtual_keyboard_port_key_look",
                        "virtual_keyboard_target_rectangles", "virtual_keyboard_click"))
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("name_keyboard", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("name_keyboard", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
