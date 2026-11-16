"""Video Setup > Graphics' SIMULATION row (game.sim_rate): the row tools/port_settings.py writes, and
port/linux/game/menu_functions.c's sim_rate_row_show, which locks it at the game's rate (ORIGINAL (30), or a Standard game's agreed rate) where the game sets the setting
aside (sim_rate.c's sim_rate_locked: a network game of other machines, a film's playback), with OK passing over the locked
spinner."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["unlocked", "locked", "locked-agreed", "unlock", "unrelated"]

NEGATIVE_CONTROLS = {
    "ok-writes-locked": (("\tif (spinner->disabled || index < 0", "\tif (index < 0"), "locked"),
    "never-disabled": (("\trow->disabled = locked;\n", ""), "locked"),
    "not-dimmed": (("locked ? 0.4f : 1.0f", "1.0f"), "locked"),
    "shows-setting-locked": (("\t\tspinner->parameters.list.selected_index = setting_value_index(setting, classic);\n",
                              ""), "locked"),
    "locked-shows-30": (('"%ld", sim_rate_next()', '"%d", SIM_RATE_CLASSIC'), "locked-agreed"),
    "focus-kept": (("\tif (locked && list->focused_child == row)", "\tif (0)"), "locked"),
    "unlock-keeps-30": (("\t\tspinner->parameters.list.selected_index = setting->loaded_index;\n", ""), "unlock"),
}


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    config = enum_with(read("port/linux/game/sim_rate.h"), "SIM_RATE_CLASSIC") + "\n"
    config += structure(menus, "pc_menu_setting") + "\n"
    code = "\n\n".join(function(menus, name) for name in (
        "text_is_number", "setting_value_index", "setting_changed_save", "setting_default_show", "sim_rate_row_show"))
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("sim_rate_menu", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("sim_rate_menu", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
