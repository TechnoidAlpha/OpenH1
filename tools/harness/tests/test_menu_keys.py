"""The menus' prompts for the keyboard (menu_keys.c): a controller button of the menus' text (the scores' A and B
after a game, a screen's A) is drawn as a key that presses that button in the menus (xinput_sdl.c's keyboard_gamepad),
with the game's own code for both. The mouse's Back button (sdl_platform.h's platform_mouse_button_backs) is B in the
menus and while typing, held over a change of what the keys drive counting only once let go of, as Escape (#541)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["keys", "scores", "unnamed", "browser", "settings", "mouse-back", "back-buttons", "back-held-over-switch"]

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
    "l-not-left-trigger": ("keyboard", ("analog(!settings && k[SDL_SCANCODE_L])", "analog(0)"), "keys"),
    # the D-pad's left drawn as the right arrow (the lobby's CHANGE TEAM)
    "dpad-arrows-swapped": ("menu", ('return "<";', 'return ">";'), "keys"),
    # the browsers' SEARCH (R3) drawn as /, which presses nothing; / as R3 in every menu
    "slash-not-r3": ("keyboard", ("if (browser && k[SDL_SCANCODE_SLASH])", "if (FALSE)"), "keys"),
    "slash-everywhere": ("keyboard", ("if (browser && k[SDL_SCANCODE_SLASH])", "if (k[SDL_SCANCODE_SLASH])"),
                         "browser"),
    # Settings' letters still the stick and buttons: "w" moves the focus as the search is typed into
    "settings-wasd": ("keyboard", ("\tif (!settings)\n\t{\n\t\tif (k[SDL_SCANCODE_D]) x++;",
                                   "\tif (TRUE)\n\t{\n\t\tif (k[SDL_SCANCODE_D]) x++;"), "settings"),
    # the search's Backspace as B: it leaves the search for a letter taken off
    "search-backspace-b": ("keyboard", ("(!(settings && settings_keys_search) && k[SDL_SCANCODE_BACKSPACE])",
                                        "k[SDL_SCANCODE_BACKSPACE]"), "settings"),
    "escape-not-b": ("keyboard", ("analog(k[SDL_SCANCODE_ESCAPE] ||\n", "analog(\n"),
                     "keys"),
    # the code before #541: the Back button only as X1 held, which the menus' pointer never lets through
    "back-not-b": ("keyboard", ("input->menu_back ||\n\t\t(mouse && m[SDL_BUTTON_X1])", "\n\t\t(mouse && m[SDL_BUTTON_X1])"),
                   "mouse-back"),
    # while typing, the Back button cancels nothing
    "back-not-typing-b": ("typing", ("k[SDL_SCANCODE_ESCAPE] || input->menu_back", "k[SDL_SCANCODE_ESCAPE]"),
                          "mouse-back"),
    # the Back button as forward too (X2)
    "forward-backs": ("buttons", ("(button) == SDL_BUTTON_X1 ||",
                                  "(button) == SDL_BUTTON_X1 || (button) == SDL_BUTTON_X2 ||"), "back-buttons"),
    # a Linux mouse's BTN_BACK (7) not taken
    "no-linux-back": ("buttons", ("#define PLATFORM_MOUSE_BUTTON_LINUX_BACK 7",
                                  "#define PLATFORM_MOUSE_BUTTON_LINUX_BACK 4"), "back-buttons"),
    # the Back button held over the switch kept: backs out of the menu too
    "back-held-kept": ("switch", ("\telse if (back_held)\n\t\tinput->menu_back = FALSE;",
                                  "\telse if (back_held)\n\t\t;"), "back-held-over-switch"),
}


def generated(fault=None):
    inputs = read("port/linux/src/xinput_sdl.c")
    widgets = read("source/interface/ui_widget.c")
    keyboard = function(inputs, "keyboard_gamepad")
    menu = function(widgets, "ui_widget_port_menu_key")
    typing = function(inputs, "typing_gamepad")
    switch = function(inputs, "keys_held_over_switch")
    # (the Back button's numbering: sdl_platform.h's)
    buttons = "\n".join(re.findall(r"^#define PLATFORM_MOUSE_BUTTON_LINUX_BACK [^\n]+|"
                                   r"^#define platform_mouse_button_backs\(button\)[^\n]*\n[^\n]+",
                                   read("port/linux/src/sdl_platform.h"), re.M))
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        if where == "menu":
            menu = mutated(menu, *change)
        elif where == "typing":
            typing = mutated(typing, *change)
        elif where == "buttons":
            buttons = mutated(buttons, *change)
        elif where == "switch":
            switch = mutated(switch, *change)
        else:
            keyboard = mutated(keyboard, *change)
    names = set(re.findall(r"SDL_SCANCODE_\w+", keyboard + function(inputs, "typing_gamepad")))
    names |= {"SDL_SCANCODE_RETURN", "SDL_SCANCODE_ESCAPE", "SDL_SCANCODE_E", "SDL_SCANCODE_F", "SDL_SCANCODE_Q",
              "SDL_SCANCODE_L", "SDL_SCANCODE_F1", "SDL_SCANCODE_SLASH"}
    config = "\n".join(re.findall(r"^#define XINPUT_GAMEPAD_\w+ [^\n]+", read("port/include/xdk/xdk_xbox.h"), re.M))
    config += "\nenum {" + ", ".join(sorted(names)) + ", TEST_SCANCODE_COUNT};\n"
    config += enum_with(widgets, "_icon_a_button") + "\n"
    config += buttons + "\n"
    code = function(inputs, "analog") + "\n" + typing + "\n" + keyboard + "\n" + switch + "\n" + menu
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_keys", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("menu_keys", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
