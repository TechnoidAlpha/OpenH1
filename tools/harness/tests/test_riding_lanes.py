"""Riding at game.sim_rate 60 and 120 (vehicle_riding_bipeds.c): a rider in a vehicle's window and the decks riders
name move once a tick, as at 30; nothing at 30 or with riding off; the lanes taken as the objects' update begins and
kept to the tick's end."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

SOURCE = "port/linux/game/vehicle_riding_bipeds.c"
CASES = ["held", "classic", "latched"]
CONTROLS = {
    # held at 30, where the lanes move in the tick
    "held-at-30": (("if (sim_substep_count() <= 1 || object_index == NONE)", "if (object_index == NONE)"), "classic"),
    # a deck held with riding off
    "deck-riding-off": (("if (!vehicle_riding_enabled())\n\t\treturn 0;", ""), "classic"),
    # the riders found again at every question, not latched for the tick
    "not-latched": (("if (biped_riding_lanes.time == game_time_get())", "if (0)"), "latched"),
    # a biped on foot's deck held
    "any-biped": (("if (biped && biped_riding_deferred(player->unit_index))", "if (biped)"), "held"),
    # the deck asked of the biped list
    "deck-from-bipeds": (("indices = vehicle ? biped_riding_lanes.vehicle_indices : biped_riding_lanes.biped_indices;",
                          "indices = biped_riding_lanes.biped_indices;"), "held"),
}


def generated(fault=None):
    source = read(SOURCE)
    lanes = re.search(r"^static struct\n\{\n\tlong time;.*?\} biped_riding_lanes = \{ NONE, 0 \};\n", source,
                      re.M | re.S).group(0)
    code = "\n".join([lanes] + [function(source, name) for name in (
        "biped_riding_lanes_find", "biped_riding_lanes_latch", "biped_riding_lanes_hold", "biped_riding_holds_lane",
        "vehicle_riding_holds_lane")])
    if fault:
        code = mutated(code, *fault)
    config = "enum { HALO_PORT_MAXIMUM_NETWORK_PLAYERS = 4 };\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("riding_lanes", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    fault, case = CONTROLS[control]
    status, output = run(build("riding_lanes", generated(fault)), case)
    assert status == CHECK_FAILED, output
