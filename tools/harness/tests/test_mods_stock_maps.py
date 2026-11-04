"""The menus' pictures are drawn from the maps as they shipped (port/linux/game/mods.c's mods_tags_loaded): a map loaded
for them plays no mod or loose sound; the main menu's and a game's do."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["pictures-stock", "others-modded"]

SOURCE = "port/linux/game/mods.c"

# a fault in mods.c and the case that must catch it
NEGATIVE_CONTROLS = {
    "pictures-modded": (("mods_globals.enabled = !menu_pictures_stock_maps();", "mods_globals.enabled = TRUE;"),
                        "pictures-stock"),
    "pictures-update-unguarded": (("if (mods_globals.enabled && (port_mods_played() ||",
                                   "if ((port_mods_played() ||"), "pictures-stock"),
    "pictures-loose-sounds": (("mods_globals.loose_sounds_enabled = mods_globals.enabled && ",
                               "mods_globals.loose_sounds_enabled = "), "pictures-stock"),
    "menu-stock": (("mods_globals.enabled = !menu_pictures_stock_maps();", "mods_globals.enabled = FALSE;"),
                   "others-modded"),
}


def generated(fault=None):
    source = read(SOURCE)
    start = source.index("static struct\n{\n\tboolean map_loaded;")
    globals_text = source[start:source.index("} mods_globals;", start) + len("} mods_globals;")] + "\n"
    code = function(source, "mods_tags_loaded")
    if fault:
        code = mutated(code, *fault)
    return (("globals.inc", globals_text), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("mods_stock_maps", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("mods_stock_maps", generated(fault)), case)
    assert status == CHECK_FAILED, f"mods.c with a fault ({control}) passed '{case}': the test cannot see it"
