"""A sub-step's state of a player on foot as its difference from the tick's state the client was last sent of them
(distributed_substep_state_write and distributed_substep_state_read, OpenH1 protocol version 22): read back within an
8192th of a world unit of where the host has the player and the rest exact, the full state where no tick's state
serves or a part does not fit, and a difference from a tick's state the client has not passed over, the entries after
it read as sent."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, needs_32_bit_abi, read, run, structure

pytestmark = needs_32_bit_abi("a unit state's longs go on the wire as the game's, 4 bytes")

SOURCE = "port/linux/game/network_distributed.c"
HEADER = "port/linux/game/network_distributed.h"
# (the unit state's bytes, the Legacy encoding OpenH1's messages carry whole)
LEGACY = "port/linux/game/network_legacy.c"
LEGACY_HEADER = "port/linux/game/network_legacy.h"
CASES = ["difference", "parts-full", "full", "unknown", "short"]

# a fault in the encoding and the case that must catch it
NEGATIVE_CONTROLS = {
    # the position's difference truncated, not rounded: up to a 4096th off
    "position-truncated": (("(short)(difference >= 0.0f ? difference + 0.5f : difference - 0.5f)", "(short)difference"),
                           "difference"),
    # the position's difference read at another scale
    "position-scale": (("(real)position[axis] / SUBSTEP_POSITION_SCALE", "(real)position[axis] / 4000.0f"),
                       "difference"),
    # a velocity's change past a byte written as a byte
    "velocity-overflow": (("if (i < -128 || i > 127 || j < -128 || j > 127 || k < -128 || k > 127)\n\t\treturn FALSE;",
                           ""), "parts-full"),
    # the up left out
    "up-ignored": (("if (state->up.i != base->up.i || state->up.j != base->up.j || state->up.k != base->up.k)",
                    "if (0)"), "parts-full"),
    # a tick's state as old as the client keeps no longer written against
    "age-unbounded": (("age < 0 || age >= SUBSTEP_BASE_TICKS ||", "age < 0 ||"), "full"),
    # another unit's tick's state written against
    "unit-ignored": (("base->unit_index != state->unit_index ||", ""), "full"),
    # a difference read against whichever tick's state the client keeps in the slot
    "base-time-ignored": (("if (!base || base->time != time - age ||", "if (!base ||"), "unknown"),
    # the client's tick's difference left out
    "predicted-ignored": (("(word)(short)predicted_difference", "0"), "difference"),
}

FUNCTIONS = ("distributed_put", "distributed_take", "distributed_real_valid", "legacy_put", "legacy_take",
             "distributed_vector_pack", "distributed_vector_unpack", "distributed_deck_put", "distributed_deck_take",
             "distributed_unit_state_write",
             "distributed_unit_state_read", "distributed_substep_base_from_state", "distributed_vector_difference",
             "distributed_substep_state_write", "distributed_substep_state_read")


def generated(fault=None):
    source = read(SOURCE)
    header = read(HEADER)
    legacy_header = read(LEGACY_HEADER)
    code = "\n".join(function(source + read(LEGACY), name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    config = "enum { NUMBER_OF_PLAYER_POWERUPS = 2, NO_PLAYER = 0xFF };\n"
    config += "#define DISTRIBUTED_UNIT_SCALE " + re.search(r"#define DISTRIBUTED_UNIT_SCALE\s+(\S+)", header)[1] + "\n"
    config += "#define SUBSTEP_POSITION_SCALE " + re.search(r"#define SUBSTEP_POSITION_SCALE\s+(\S+)", source)[1] + "\n"
    config += "#define DISTRIBUTED_VELOCITY_SCALE " + re.search(r"#define DISTRIBUTED_VELOCITY_SCALE\s+(\S+)", header)[1] + "\n"
    for member in ("_distributed_unit_alive_bit", "_distributed_unit_camouflaged_bit",
                   "_distributed_unit_part_riding_bit", "_distributed_pose_facing"):
        config += enum_with(legacy_header, member) + "\n"
    for member in ("SUBSTEP_BASE_TICKS", "_substep_state_difference_bit"):
        config += enum_with(source, member) + "\n"
    config += enum_with(legacy_header, "DISTRIBUTED_UNIT_STATE_MINIMUM_SIZE") + "\n"
    config += structure(read("port/linux/game/vehicle_riding.h"), "vehicle_riding_deck") + "\n"
    config += structure(legacy_header, "distributed_unit_state") + "\n"
    config += structure(source, "distributed_substep_base") + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("substep_state", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("substep_state", generated(fault)), case)
    assert status == CHECK_FAILED, output
