"""Who a structure BSP switch moves (source/game/players.c's players_reconnect_to_structure_bsp and
player_teleport_on_bsp_switch): in network co-op only a teammate outside the new BSP or on one of its loading zones
is brought to the crosser; one inside the new BSP, at the seam, stays where it is. Split screen keeps the original's
rule. Every player's old cluster is let go."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["overlap", "outside", "switch-back", "split-screen"]

# a fault, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (the leaf test gone: a teammate outside the new BSP left to fall)
    "no-leaf-test": (("if (scenario_leaf_index_from_point(\n\t\t\t&biped->object.bounding_sphere_center) == NONE ||",
                      "if (FALSE ||"), "outside"),
    # (the loading-zone test gone: a teammate left on a zone that switches on)
    "no-zone-test": (("outside_switch_trigger = players_coop_bsp_switch_trigger(unit_index) != NONE;",
                      "outside_switch_trigger = FALSE;"), "switch-back"),
    # (the rule before: the whole team moved)
    "everyone-moved": (("outside_switch_trigger = players_coop_bsp_switch_trigger(unit_index) != NONE;",
                        "outside_switch_trigger = TRUE;"), "overlap"),
    # (the zones of the old BSP tested, not the new one's)
    "old-bsp-zones": (("volume->source_structure_bsp_index == global_structure_bsp_index &&\n"
                       "\t\t\tscenario_trigger_volume_test_object", "TRUE &&\n\t\t\tscenario_trigger_volume_test_object"),
                      "overlap"),
}


def generated(fault=None):
    source = read("source/game/players.c")
    code = "\n".join(function(source, name) for name in (
        "players_coop_bsp_switch_trigger", "player_teleport_on_bsp_switch", "players_reconnect_to_structure_bsp"))
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("coop_bsp_switch_moves", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("coop_bsp_switch_moves", generated(fault)), case)
    assert status == CHECK_FAILED, f"players.c with a fault ({control}) passed '{case}': the test cannot see it"
