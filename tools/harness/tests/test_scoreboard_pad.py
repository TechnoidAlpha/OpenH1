"""The scoreboard's player menu by controller and keyboard (scoreboard_pad.c): the game's light, pin and menu
(game_engine.c's scoreboard_pad_update) and the controller's side (xinput_sdl.c's scoreboard_pad_read and
halo_scoreboard_pad), with the game's own code for both."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["light", "pin", "menu", "prompts", "read", "keys"]

ENGINE_FUNCTIONS = ["scoreboard_pad_first", "scoreboard_pad_move", "scoreboard_pad_update", "scoreboard_pad_hold",
                    "scoreboard_pad_prompts"]
INPUT_FUNCTIONS = ["scoreboard_pad_bits", "scoreboard_pad_keys", "scoreboard_pad_port_hold", "scoreboard_pad_read",
                   "halo_scoreboard_pad"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the light on this machine's own players too
    "lights-own-player": ("scoreboard_pad_first", ("\t\tif (!slots[index].pickable)\n\t\t\tcontinue;\n", ""),
                          "light"),
    # up and down to the nearest either way
    "down-any": ("scoreboard_pad_move", ("(direction == HALO_SCOREBOARD_PAD_UP ? slot->position >= at->position : "
                                         "slot->position <= at->position)", "FALSE"), "light"),
    # the page left where it was
    "no-scroll": ("scoreboard_pad_update", ("\t\t\tview->scroll = slots[lit].position - span + 1;\n", ""), "light"),
    # the stick moving the light while the player moves with it
    "stick-unpinned": ("scoreboard_pad_update", ("\tif (view->pinned)\n\t{\n\t\tdirections |=", "\t{\n\t\tdirections |="),
                       "pin"),
    # A (the game's jump) opening the menu while not pinned
    "a-unpinned": ("scoreboard_pad_update", ("(view->pinned && TEST_FLAG(pressed, HALO_SCOREBOARD_PAD_ACCEPT))",
                                             "TEST_FLAG(pressed, HALO_SCOREBOARD_PAD_ACCEPT)"), "pin"),
    # the presses of a lapse taken
    "lapse-ignored": ("scoreboard_pad_update", ("\t\t\tview->player_index = NONE;\n\t\treturn _scoreboard_pad_nothing;",
                                                "\t\t\tview->player_index = NONE;"), "pin"),
    # the menu's items past its last
    "menu-unclamped": ("scoreboard_pad_update", ("MIN(view->item + 1, item_count - 1));\n\t\t\tview->item = (short)"
                                                 "(view->item == NONE ? NONE : PIN(view->item, 0, item_count - 1));",
                                                 "view->item + 1);"), "menu"),
    # B closing the scoreboard with the menu
    "b-closes-all": ("scoreboard_pad_update", ("\t\t\tview->item = NONE;\n\t\t}\n\t\telse if (item_count > 0)",
                                               "\t\t\tview->item = NONE;\n\t\t\tview->pinned = FALSE;\n\t\t}\n"
                                               "\t\telse if (item_count > 0)"), "menu"),
    # the X prompt while pinned
    "prompt-x-pinned": ("scoreboard_pad_prompts", ("\t\tprompts[0].icon = _gamepad_analog_button_a;\n"
                                                   "\t\tprompts[0].key = \"Enter\";\n\t\tprompts[0].label = L\"Player",
                                                   "\t\tprompts[0].icon = _gamepad_analog_button_x;\n"
                                                   "\t\tprompts[0].key = \"Enter\";\n\t\tprompts[0].label = L\"Player"),
                        "prompts"),
    # X left to the game (a reload) while offered
    "x-not-taken": ("scoreboard_pad_read", ("\t\ttake_analog = (unsigned char)(1 << XINPUT_GAMEPAD_X);\n", ""), "read"),
    # the B that closed it a melee
    "no-hold-over": ("scoreboard_pad_read", ("\ttake_buttons |= (WORD)(pad->held_buttons & reading->wButtons);\n"
                                             "\ttake_analog |= (unsigned char)(pad->held_analog & analog_down);\n", ""),
                     "read"),
    # the controller kept once the game stops asking
    "never-let-go": ("scoreboard_pad_port_hold", ("menus || pad->readings > SCOREBOARD_PAD_READINGS", "menus"), "read"),
    # Enter not choosing
    "enter-opens-only": ("scoreboard_pad_keys", ("(1UL << HALO_SCOREBOARD_PAD_OPEN) | (1UL << HALO_SCOREBOARD_PAD_ACCEPT)",
                                                 "(1UL << HALO_SCOREBOARD_PAD_OPEN)"), "keys"),
}


def generated(fault=None):
    engine = read("source/game/game_engine.c")
    inputs = read("port/linux/src/xinput_sdl.c")
    keyboard = read("port/linux/include/halo_keyboard.h")
    config = "\n".join(re.findall(r"^#define XINPUT_GAMEPAD_\w+ [^\n]+", read("port/include/xdk/xdk_xbox.h"), re.M))
    config += "\n" + enum_with(read("source/input/input.h"), "_gamepad_analog_button_a") + "\n"
    config += enum_with(keyboard, "HALO_KEYBOARD_MOVE_FORWARD") + "\n"
    config += enum_with(keyboard, "HALO_SCOREBOARD_PAD_UP") + "\n" + enum_with(keyboard, "HALO_SCOREBOARD_PAD_FREE") + "\n"
    config += "enum {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_RETURN, " \
              "SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_SPACE, SDL_SCANCODE_W, " \
              "TEST_SCANCODE_COUNT};\n"
    for name in ("scoreboard_view", "scoreboard_slot", "scoreboard_prompt"):
        config += structure(engine, name) + "\n"
    config += enum_with(engine, "_scoreboard_pad_nothing") + "\n"
    config += "\n".join(re.findall(r"^#define SCOREBOARD_PAD_\w+ [^\n]+", inputs, re.M)) + "\n"
    pads = re.search(r"^static struct scoreboard_pad\n\{.*?^\} scoreboard_pads\[PORT_COUNT\];\n"
                     r"static pthread_mutex_t scoreboard_pad_lock = PTHREAD_MUTEX_INITIALIZER;", inputs, re.M | re.S)
    assert pads, "xinput_sdl.c's scoreboard_pads"
    config += pads.group(0) + "\n"
    code = ""
    for source, names in ((engine, ENGINE_FUNCTIONS), (inputs, INPUT_FUNCTIONS)):
        for name in names:
            text = function(source, name)
            if fault and NEGATIVE_CONTROLS[fault][0] == name:
                text = mutated(text, *NEGATIVE_CONTROLS[fault][1])
            code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("scoreboard_pad", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("scoreboard_pad", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
