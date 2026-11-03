"""The HUD's button prompts for the keyboard and mouse (input.button_prompts): the device they follow, the key a
prompt's game control or controller button is (a profile's layout moving the button, not the key), and the text the
prompt shows; with the game's own tables (the HUD's icons' game controls, the keyboard's actions), not copies."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["device", "icons", "buttons", "remapped", "text"]

# a fault in the code and the cases that must catch it
NEGATIVE_CONTROLS = {
    # the keyboard wins a reading with both
    "keyboard-first": ("xinput", ("if (controller_used)\n\t\treturn PROMPT_DEVICE_CONTROLLER;\n\tif (keyboard_used)\n"
                                  "\t\treturn PROMPT_DEVICE_KEYBOARD;",
                                  "if (keyboard_used)\n\t\treturn PROMPT_DEVICE_KEYBOARD;\n\tif (controller_used)\n"
                                  "\t\treturn PROMPT_DEVICE_CONTROLLER;"), ["device"]),
    # keys before anything is used, a pad connected or not
    "keys-at-start": ("xinput", ("return !controller_connected && !touch_controls;", "return 1;"), ["device"]),
    # Start named as the scores
    "start-is-back": ("input", ("return HALO_KEYBOARD_PAUSE;", "return HALO_KEYBOARD_SCOREBOARD;"), ["buttons"]),
    # a button's control from the default layout, not the player's
    "default-layout": ("input", ("player_control_preferences[local_player_index].",
                                 "player_control_preferences[0]."), ["remapped"]),
    # the movement keys always separated
    "movement-slashes": ("prompts", ("if (!letters && index > 0", "if (index > 0"), ["text"]),
    # an action with no key falls back to the controller's button
    "unbound-is-button": ("prompts", ("\t\t\thud_key_prompt_action_name(action, name, sizeof(name));",
                                      "\t\t\thalo_keyboard_action_name(action, name, sizeof(name));"), ["text"]),
    "unbound-movement-is-stick": ("prompts", ("hud_key_prompt_action_name(NONE, keys[index], sizeof(keys[index]));",
                                              "return;"), ["text"]),
}


def generated(fault=None):
    inputs = read("port/linux/src/xinput_sdl.c")
    abstraction = read("source/input/input_abstraction.c")
    messaging = read("source/interface/hud_messaging.c")
    prompts = read("port/linux/game/hud_key_prompts.c")
    config = "\n".join([
        enum_with(read("source/input/input.h"), "_gamepad_analog_button_a"),
        enum_with(abstraction, "_game_control_jump"),
        enum_with(abstraction, "_joystick_controls_default"),
        structure(read("source/input/input_abstraction.h"), "game_input_preferences"),
        enum_with(read("port/linux/include/halo_keyboard.h"), "HALO_KEYBOARD_MOVE_FORWARD"),
        enum_with(messaging, "_icon_a_button"),
        enum_with(inputs, "PROMPT_DEVICE_NONE"),
        enum_with(prompts, "HUD_KEY_PROMPT_LEFT_STICK"),
    ]) + "\n"
    start = messaging.index("static char button_mappings[")
    config += messaging[start:messaging.index("};", start) + 2] + "\n"
    start = abstraction.index("static signed char const keyboard_game_controls[")
    config += abstraction[start:abstraction.index("};", start) + 2] + "\n"
    xinput = "\n".join(function(inputs, name) for name in ["prompt_device_next", "prompts_show_keys"])
    code = "\n".join(function(abstraction, name) for name in
                     ["set_default_game_input_preferences", "input_abstraction_port_keyboard_action"])
    text = "\n".join(function(prompts, name) for name in [
        "latin1_to_wide", "hud_key_prompt_action_name", "movement_name", "hud_key_prompt_text"])
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        if where == "xinput":
            xinput = mutated(xinput, *change)
        elif where == "input":
            code = mutated(code, *change)
        else:
            text = mutated(text, *change)
    return (("config.inc", config), ("under_test.inc", "\n".join([xinput, code, text]) + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_key_prompts(case):
    status, output = run(build("key_prompts", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [(fault, case) for fault, (_, _, cases) in NEGATIVE_CONTROLS.items()
                                        for case in cases])
def test_fault_detected(fault, case):
    status, output = run(build("key_prompts", generated(fault)), case)
    assert status == CHECK_FAILED, output
