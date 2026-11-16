"""A loose item against a wall steeper than 45 degrees (source/items/items.c's item_move_substep, #974): it falls on
down the wall to rest on the floor, where the original pinned it, rattling and its velocity growing; a Legacy game
keeps the original's; a drop on the floor, and a strike from away from a wall, move tick for tick as before."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["steep-wall", "legacy", "unchanged"]

FIX = "if (collision.t <= 0.f && network_standard_simulation_fixes())"
# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the original's: an item struck where it lay stays on the surface
    "no-lift": ((FIX, "if (FALSE)"), "steep-wall"),
    # every bounce lifted off, not only a strike at the line's start: the floor's bounces move otherwise
    "lift-every-bounce": ((FIX, "if (network_standard_simulation_fixes())"), "unchanged"),
    # the fix in a Legacy game too
    "fixed-in-legacy": ((FIX, "if (collision.t <= 0.f)"), "legacy"),
}


def generated(fault=None):
    config = enum_with(read("source/physics/collisions.h"), "_collision_result_structure") + "\n"
    code = function(read("source/items/items.c"), "item_move_substep")
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("item_steep_wall", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("item_steep_wall", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
