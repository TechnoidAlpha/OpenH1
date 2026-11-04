"""The menus' typed values (port/linux/game/menu_functions.c's text fields and sliders; port/linux/src/xinput_sdl.c's
typing_gamepad): a slider's field starts empty where its value is a word (DEFAULT, AUTO, NONE, OFF, MATCH), so the
number is typed at once, and with its number chosen otherwise, which the first key typed replaces and the first
backspace clears; a name's field keeps its text; Enter ends a field as its row's A, posted to the menus, never as the
controller's Start, which in a game closes the menus and drops what was typed (#507, #508)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["word-empty", "replace", "backspace-clears", "names-append", "enter-is-a", "enter-not-start"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the word shown typed into, as before (DEFAULT left to delete)
    "word-kept": ("menus", ("\tif (settings_slider_word(slider, value) != NONE)\n\t\ttext[0] = 0;\n\telse\n"
                            "\t\tsettings_slider_number(slider, value, text, size);",
                            "\tsettings_slider_text(slider, value, text, size);"), "word-empty"),
    # typing appended to the number shown
    "number-appended": ("menus", ("\tif (text_field.chosen)\n\t\ttext_field.text[0] = 0;\n\ttext_field.chosen = FALSE;"
                                  "\n\tlength", "\ttext_field.chosen = FALSE;\n\tlength"), "replace"),
    # backspace takes one character of the number chosen
    "backspace-one": ("menus", ("\t\t\t\tif (text_field.chosen)\n\t\t\t\t\ttext_field.text[0] = 0;\n"
                                "\t\t\t\telse if (length)", "\t\t\t\tif (length)"), "backspace-clears"),
    # every field's text chosen, a name's too
    "names-replaced": ("menus", ("\ttext_field.chosen = FALSE;\n\ttext_field_shown_time",
                                 "\ttext_field.chosen = text[0] != 0;\n\ttext_field_shown_time"), "names-append"),
    # Enter not read by the field (it was the controller's Start)
    "enter-ignored": ("menus", ("\t\t\tif (key.key_code == _key_return)", "\t\t\tif (FALSE)"), "enter-is-a"),
    # Enter Start in a menu's field again
    "enter-start": ("keyboard", ("else if (text_typing_enter_armed && !text_typing_field)",
                                 "else if (text_typing_enter_armed)"), "enter-not-start"),
}


def _between(source, start, end):
    at = source.index(start)
    return source[at:source.index(end, at) + len(end)]


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    inputs = read("port/linux/src/xinput_sdl.c")
    where, change, _ = NEGATIVE_CONTROLS[fault] if fault else (None, None, None)
    names = set(re.findall(r"SDL_SCANCODE_\w+", function(inputs, "typing_gamepad")))
    config = "\n".join(re.findall(r"^#define XINPUT_GAMEPAD_\w+ [^\n]+", read("port/include/xdk/xdk_xbox.h"), re.M))
    config += "\nenum {" + ", ".join(sorted(names)) + ", TEST_SCANCODE_COUNT};\n"
    for name in ("TEXT_FIELD_LENGTH", "KEY_MODIFIER_CONTROL_BIT", "BUTTON_A"):
        config += re.search(rf"^#define {name} [^\n]+", menus, re.M).group(0) + "\n"
    config += "#define ROW_TEXT_LENGTH 64\n"
    field = _between(menus, "static struct\n{\n\tstruct widget_instance *row;\n\tchar text[TEXT_FIELD_LENGTH];",
                     "} text_field;")
    start = menus.index("struct settings_slider_stop\n{")
    sliders = menus[start:menus.index("\n};", menus.index("} const settings_sliders[] =")) + 3]
    code = "\n\n".join([field, "static unsigned long text_field_shown_time;", sliders] + [
        function(menus, name) for name in (
            "text_field_editing", "text_field_open", "text_field_begin", "text_field_begin_value",
            "text_field_insert", "text_field_show", "settings_slider_word", "settings_slider_number",
            "settings_slider_text", "settings_slider_field_text")])
    keyboard = function(inputs, "analog") + "\n\n" + function(inputs, "typing_gamepad")
    if where == "menus":
        code = mutated(code, *change)
    elif where == "keyboard":
        keyboard = mutated(keyboard, *change)
    return (("config.inc", config), ("under_test.inc", code + "\n\n" + keyboard + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("typed_values", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("typed_values", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
