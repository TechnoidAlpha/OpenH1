"""An OpenH1 game's tick states as differences (distributed_unit_difference_write and distributed_unit_difference_read,
OpenH1 protocol version 23): each state read back is exactly the whole state's bytes, through a chain of ticks whose
acks lag (running, turning, hurt, killed, respawned); whole where the client asked for it, nothing was sent at or before
its ack, a part does not fit or the ack is older than the client keeps; a difference from a state the client has not
passed over, the entries after it read as sent; cut short, no entry."""
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
CASES = ["chain", "not-predicted", "whole", "unknown", "short", "deck"]

# a fault in the encoding and the case that must catch it
NEGATIVE_CONTROLS = {
    # the grid's steps truncated, not rounded
    "grid-truncated": (("return (long)(steps >= 0.0f ? steps + 0.5f : steps - 0.5f);", "return (long)steps;"),
                       "chain"),
    # a state sent after the client's ack written against
    "ack-ignored": (("sent != NONE && sent <= acked &&", "sent != NONE &&"), "unknown"),
    # the client's own ticks' state taken for the tick named
    "base-time-ignored": (("distributed_unit_base_times[player_index][(time - age) & (UNIT_BASE_TICKS - 1)] == time - age ?",
                           "TRUE ?"), "unknown"),
    # the shields' change left out
    "vitality-ignored": (("if (written.body_vitality != base.body_vitality || written.shield_vitality != base.shield_vitality)",
                          "if (0)"), "chain"),
    # the client's tick's difference read backwards
    "predicted-sign": (("(word)read.predicted_time + (word)(short)small", "(word)read.predicted_time - (word)(short)small"),
                       "chain"),
    # a facing's turn past a byte written as a byte
    "forward-overflow": (("if (distributed_vector_difference(&written.forward, &base.forward, small))",
                          "if (distributed_vector_difference(&written.forward, &base.forward, small) || 1)"), "chain"),
    # a teleport's step past 16 bits wrapped
    "position-overflow": (("difference < -32767 || difference > 32767 ||", ""), "whole"),
    # a rider's deck part left out of the differences (OpenH1 protocol 31): the base's kept
    "deck-ignored": (("if (written.deck_present || base.deck_present)", "if (0)"), "deck"),
    # a deck part's going away not sent
    "deck-gone-ignored": (("*cursor++ = written.deck_present ? 1 : 0;", "*cursor++ = 1;"), "deck"),
    # a deck part read without its presence checked
    "deck-presence-unchecked": (("|| deck_present > 1)", ")"), "deck"),
}

FUNCTIONS = ("distributed_put", "distributed_take", "distributed_real_valid", "legacy_put", "legacy_take",
             "distributed_vector_pack", "distributed_vector_unpack", "distributed_deck_put", "distributed_deck_take",
             "distributed_unit_state_write",
             "distributed_unit_state_read", "distributed_vector_difference", "distributed_grid_steps",
             "distributed_unit_state_snap", "distributed_unit_state_as_written", "distributed_unit_difference_write",
             "distributed_unit_difference_read")


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
    for member in ("UNIT_BASE_TICKS", "_unit_difference_form_whole", "_unit_difference_flags_bit"):
        config += enum_with(source, member) + "\n"
    config += enum_with(legacy_header, "DISTRIBUTED_UNIT_STATE_MINIMUM_SIZE") + "\n"
    config += structure(read("port/linux/game/vehicle_riding.h"), "vehicle_riding_deck") + "\n"
    config += structure(legacy_header, "distributed_unit_state") + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("unit_difference", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("unit_difference", generated(fault)), case)
    assert status == CHECK_FAILED, output
