"""A controller the test script plays is read as a connected one is (xinput_sdl.c): its presses are the controller's
own, so the stock layouts' D-pad Down shows and hides the Spartan link and D-pad Right pings, on a split screen
player's port (split_screen_pad_read) and on the first (XInputGetState), with the port's own code."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["link", "ping", "closed", "buttons"]

FUNCTIONS = ["chat_pad_bits", "chat_pad_record", "ping_pad_read", "halo_ping_pad_pressed", "spartan_link_pad_read",
             "halo_spartan_link_pad_pressed", "test_script_parse", "test_script_axis", "test_script_port_gamepad",
             "split_screen_pad_read"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (dev before the fix) a scripted split screen port's presses not the controller's own
    "presses-not-own": ("split_screen_pad_read",
                        ("\t\tchat_physical_pad[port] = chat_pad_bits(&test_script_pressed[port]);\n", ""), "link"),
    # the script's D-pad not recorded as pressed
    "dpad-not-recorded": ("test_script_port_gamepad", ("\t\tpressed->wButtons |= (WORD)event->digital;\n", ""), "link"),
    # the script's presses kept from one reading to the next
    "presses-kept": ("test_script_port_gamepad", ("\tmemset(pressed, 0, sizeof(*pressed));\n", ""), "buttons"),
    # the link's reading skipped
    "link-unread": ("split_screen_pad_read", ("\tspartan_link_pad_read(port, pad, reserved);\n", ""), "link"),
}


def generated(fault=None):
    inputs = read("port/linux/src/xinput_sdl.c")
    keyboard = read("port/linux/include/halo_keyboard.h")
    remap = read("port/linux/include/controller_remap.h")
    config = "\n".join(re.findall(r"^#define XINPUT_GAMEPAD_\w+ [^\n]+", read("port/include/xdk/xdk_xbox.h"), re.M))
    config += "\n" + "\n".join(re.findall(r"^#define CONTROLLER_REMAP_RESERVE_\w+ [^\n]+", remap, re.M))
    config += "\n" + enum_with(remap, "CONTROLLER_REMAP_ACTIONS") + "\n"
    config += enum_with(keyboard, "NUMBER_OF_HALO_KEYBOARD_ACTIONS") + "\n"
    config += enum_with(keyboard, "HALO_CHAT_PAD_STICK_RIGHT") + "\n"
    code = re.search(r"^static const char \*const binding_settings\[.*?^\};\n", inputs, re.M | re.S).group(0)
    code += re.search(r"^static unsigned long chat_pad\[PORT_COUNT\][^\n]*\n", inputs, re.M).group(0)
    code += "".join(re.findall(r"^static unsigned char (?:ping|spartan_link)_pad_\w+\[PORT_COUNT\];\n", inputs, re.M))
    script = re.search(r"^enum\n\{\n\tTEST_SCRIPT_EVENTS.*?^static int test_script_gyro;\n", inputs, re.M | re.S)
    assert script, "xinput_sdl.c's test script's state"
    code += script.group(0)
    for name in FUNCTIONS:
        text = function(inputs, name)
        if fault and NEGATIVE_CONTROLS[fault][0] == name:
            text = mutated(text, *NEGATIVE_CONTROLS[fault][1])
        code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("scripted_pad", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("scripted_pad", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"


def test_the_reading_uses_them():
    """XInputGetState reads a split screen port the script plays through split_screen_pad_read, as a connected one,
    and takes the script's presses on the first controller as its own before the D-pad's pings and link read them."""
    state = function(read("port/linux/src/xinput_sdl.c"), "XInputGetState")
    assert "else if (connected || (test_script_ports & (1U << port)))\n\t\tsplit_screen_pad_read(" in state
    first = state.index("chat_physical_pad[0] |= chat_pad_bits(&test_script_pressed[0]);")
    assert state.index("memset(&test_script_pressed[0], 0") < state.index("test_input_gamepad(&state->Gamepad)") < first
    assert first < state.index("ping_pad_read(0, &state->Gamepad, reserved)")
    assert first < state.index("spartan_link_pad_read(0, &state->Gamepad, reserved)")
