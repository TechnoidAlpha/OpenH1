"""The real biped_update_on_foot (bipeds.c) and units.c's unarmed blow: an unarmed player melees only where the map has
a blow to give (a multiplayer map's), not at a campaign's unarmed start, which stays the Xbox's, silent."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["campaign-unarmed", "multiplayer-unarmed", "biped-own-blow", "armed", "seated"]

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # (the unarmed melee wherever the player has no weapon: the hit's sound at a10's start)
    "unarmed-anywhere": (("biped->unit.parent_seat_index == NONE &&\n\t\t\t\t\tunit_unarmed_melee_damage(biped_index) != NONE)",
                          "biped->unit.parent_seat_index == NONE)"), "campaign-unarmed"),
    # (no unarmed melee at all)
    "unarmed-never": (("(weapon_index == NONE && biped->unit.parent_seat_index == NONE &&",
                       "(FALSE && weapon_index == NONE && biped->unit.parent_seat_index == NONE &&"),
                      "multiplayer-unarmed"),
    # (the biped's own blow forgotten)
    "own-blow-forgotten": (("if (unit_definition->unit.melee_damage.index!=NONE)\n\t\treturn",
                            "if (FALSE)\n\t\treturn"), "biped-own-blow"),
}


def generated(fault=None):
    bipeds = read("source/units/bipeds.c")
    units = read("source/units/units.c")
    config = "enum { UNARMED_MELEE_TICKS = %d };\n" % constant(bipeds, "UNARMED_MELEE_TICKS")
    code = "\n".join([
        function(units, "unarmed_melee_weapon_definition_index"),
        function(units, "unit_unarmed_melee_damage"),
        function(bipeds, "biped_update_on_foot"),
    ])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("unarmed_melee", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("unarmed_melee", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
