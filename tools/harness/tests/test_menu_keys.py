"""The menus' prompts for the keyboard (menu_keys.c): a controller button of the menus' text (the scores' A and B
after a game, a screen's A) is drawn as a key that presses that button in the menus (xinput_sdl.c's keyboard_gamepad),
with the game's own code for both."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["keys", "scores", "unnamed", "browser"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # B as Space, which is A in the menus
    "b-as-space": ("menu", ('return "Esc";', 'return "Space";'), "keys"),
    # the scores' A as the controller's
    "a-unnamed": ("menu", ('\tcase _icon_a_button:\n\t\treturn "Enter";\n', ""), "scores"),
    # the keyboard's B without Escape
    # Tab as X (delete) in the Profiles screen, as the browsers'
    "tab-deletes": ("keyboard", ("(browser ? k[SDL_SCANCODE_TAB] : k[SDL_SCANCODE_E])", "k[SDL_SCANCODE_TAB]"),
                    "browser"),
    # the browsers' Filters (the left trigger) drawn as L, which presses nothing
    "l-not-left-trigger": ("keyboard", ("analog(k[SDL_SCANCODE_L])", "analog(0)"), "keys"),
    "escape-not-b": ("keyboard", ("k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_BACKSPACE]", "k[SDL_SCANCODE_BACKSPACE]"),
                     "keys"),
}


def generated(fault=None):
    inputs = read("port/linux/src/xinput_sdl.c")
    widgets = read("source/interface/ui_widget.c")
    keyboard = function(inputs, "keyboard_gamepad")
    menu = function(widgets, "ui_widget_port_menu_key")
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        if where == "menu":
            menu = mutated(menu, *change)
        else:
            keyboard = mutated(keyboard, *change)
    names = set(re.findall(r"SDL_SCANCODE_\w+", keyboard + function(inputs, "typing_gamepad")))
    names |= {"SDL_SCANCODE_RETURN", "SDL_SCANCODE_ESCAPE", "SDL_SCANCODE_E", "SDL_SCANCODE_F", "SDL_SCANCODE_Q",
              "SDL_SCANCODE_L", "SDL_SCANCODE_F1"}
    config = "\n".join(re.findall(r"^#define XINPUT_GAMEPAD_\w+ [^\n]+", read("port/include/xdk/xdk_xbox.h"), re.M))
    config += "\nenum {" + ", ".join(sorted(names)) + ", TEST_SCANCODE_COUNT};\n"
    config += enum_with(widgets, "_icon_a_button") + "\n"
    code = "\n".join(function(inputs, name) for name in ("analog", "typing_gamepad")) + "\n" + keyboard + "\n" + menu
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_keys", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("menu_keys", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
