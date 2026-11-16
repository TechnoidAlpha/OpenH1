"""The Legacy encodings of the host's per-tick streams (network_legacy.c), frozen byte for byte: fixed players' states
(on foot, riding, tilted, dead with a killer and without, hurt, camouflaged, with powerups, with a prediction's fine
velocity) written as a Legacy machine is sent them (_distributed_message_unit_states, with the client's tick and
without), and fixed actors' (standing, riding, moving, with an impulse, a speech, a custom animation, fleeing,
camouflaged) in their whole structures' messages (_distributed_message_actor_states), as many as a datagram holds a
message; and the same states as an OpenH1 client is sent them (a unit state's difference, whole and from a state the
client took; an actor's compact state). Each is held to the bytes recorded below, which a Legacy machine (OpenCE, at
network version 44) reads: a change to any of them is one it cannot read, whatever else agrees. The OpenH1 encodings
may change with OpenH1's protocol version; record their bytes again then (GOLDEN_RECORD=1 prints them)."""
import hashlib
import os
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, needs_32_bit_abi, read, run, structure

pytestmark = needs_32_bit_abi("the states' longs and structures go on the wire as the game's, 4 bytes")

LEGACY = "port/linux/game/network_legacy.c"
LEGACY_HEADER = "port/linux/game/network_legacy.h"
DISTRIBUTED = "port/linux/game/network_distributed.c"
DISTRIBUTED_HEADER = "port/linux/game/network_distributed.h"
ACTORS = "port/linux/game/network_actors.c"
CASES = ["players", "player-differences", "actors", "actor-messages", "machines"]

LEGACY_FUNCTIONS = ("legacy_put", "legacy_take", "legacy_actor_states_send", "network_legacy_machine",
                    "distributed_unit_state_write", "distributed_unit_state_read", "network_legacy_actor_state_add",
                    "network_legacy_actor_states_end")
DISTRIBUTED_FUNCTIONS = ("distributed_put", "distributed_take", "distributed_real_valid",
                         "distributed_vector_pack", "distributed_vector_unpack", "distributed_deck_put",
                         "distributed_deck_take",
                         "distributed_vector_difference", "distributed_grid_steps", "distributed_unit_state_snap",
                         "distributed_unit_state_as_written", "distributed_unit_difference_write")
ACTOR_FUNCTIONS = ("actor_put", "actor_take", "actor_state_compact_write", "actor_state_compact_read")

# a fault in an encoding and the case whose bytes must show it
NEGATIVE_CONTROLS = {
    # the body's health and the shields' swapped
    "vitality-order": (("cursor = legacy_put(cursor, &state->body_vitality, sizeof(state->body_vitality));\n"
                        "\t\tcursor = legacy_put(cursor, &state->shield_vitality, sizeof(state->shield_vitality));",
                        "cursor = legacy_put(cursor, &state->shield_vitality, sizeof(state->shield_vitality));\n"
                        "\t\tcursor = legacy_put(cursor, &state->body_vitality, sizeof(state->body_vitality));"),
                       "players"),
    # the client's tick sent where not predicted
    "predicted-always": (("\tif (!predicted)\n\t\tSET_FLAG(flags, _distributed_unit_predicted_bit, FALSE);\n", ""),
                         "players"),
    # an up straight up sent anyway
    "up-always": (("if (state->up.i || state->up.j || state->up.k != (short)DISTRIBUTED_UNIT_SCALE)\n\t\t\t{\n"
                   "\t\t\t\tSET_FLAG(parts, _distributed_unit_part_up_bit",
                   "if (1)\n\t\t\t{\n\t\t\t\tSET_FLAG(parts, _distributed_unit_part_up_bit"), "players"),
    # a killer's part dropped
    "killer-dropped": (("else if (state->killing_player_index != NO_PLAYER)", "else if (0)"), "players"),
    # a datagram's whole structures past what one holds
    "datagram-ignored": (("legacy_actor_count == MAXIMUM_ENTRIES_PER_MESSAGE ||\n"
                          "\t\tlegacy_actor_count == DATAGRAM_ENTRIES(struct distributed_actor_state)",
                          "legacy_actor_count == MAXIMUM_ENTRIES_PER_MESSAGE"), "actor-messages"),
    # an OpenCE machine of an OpenH1 game sent OpenH1's encodings
    "opence-as-openh1": (("return !network_standard_hosting() || !network_standard_machine_is_openh1(machine_index);",
                          "return !network_standard_hosting();"), "machines"),
}

# the bytes recorded, each case's lines: "<encoding> <state>[ predicted] <hex>", a message's "message <machine> <type>
# <count> <size> <its bytes past its header, in hex>" (a long one's as their SHA-256's first 16 digits: normalized)
GOLDEN = {
    "players": """
legacy-player on-foot predicted 02a3004200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e
legacy-player on-foot 0223004200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc0040e02e
legacy-player riding predicted 02a1014200341211002200030034120040e02e
legacy-player riding 022101420034121100220003000040e02e
legacy-player tilted predicted 02a3024200341200004d4200009bc20000403f2c0188ff0500007dd0077cfcb004d4febc7f34120040e02e
legacy-player tilted 0223024200341200004d4200009bc20000403f2c0188ff0500007dd0077cfcb004d4febc7f0040e02e
legacy-player dead-killer predicted 01042003
legacy-player dead-killer 01042003
legacy-player dead predicted 010400
legacy-player dead 010400
legacy-player hurt predicted 02b3044200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc341288130000bc022c0100400200
legacy-player hurt 0233044200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc88130000bc022c0100400200
legacy-player camouflaged predicted 02a3104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e03c8
legacy-player camouflaged 0223104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc0040e02e03c8
legacy-player powerups predicted 02a3084200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e84031f00
legacy-player powerups 0223084200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc0040e02e84031f00
legacy-player fine predicted 02a3104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e80000000963e0000f0bd0000a03b
legacy-player fine 0223104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc0040e02e80000000963e0000f0bd0000a03b
""",
    "player-differences": """
openh1-player-whole on-foot 0002a3004200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e
openh1-player-difference on-foot 010201900400020000000000280001
openh1-player-whole riding 0002a1014200341211002200030034120040e02e
openh1-player-difference riding 0102010904a111002200030001
openh1-player-whole tilted 0002a3024200341200004d4200009bc20000403f2c0188ff0500007dd0077cfcb004d4febc7f34120040e02e
openh1-player-difference tilted 0102019006000200000000002800b004d4febc7f01
openh1-player-whole dead-killer 0001042003
openh1-player-difference dead-killer 01010107180403ffffffff000000000000
openh1-player-whole dead 00010400
openh1-player-difference dead 010101051804ffffffff000000000000
openh1-player-whole hurt 0002b3044200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc341288130000bc022c0100400200
openh1-player-difference hurt 0102019134b30002000000000028000188130000bc022c0100400200
openh1-player-whole camouflaged 0002a3104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e03c8
openh1-player-difference camouflaged 01020190840002000000000028000103c8
openh1-player-whole powerups 0002a3084200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e84031f00
openh1-player-difference powerups 01020190440002000000000028000184031f00
openh1-player-whole fine 0002a3104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e80000000963e0000f0bd0000a03b
openh1-player-difference fine 0002a3104200341200004d4200009bc20000403f2c0188ff0500007dd0077cfc34120040e02e80000000963e0000f0bd0000a03b
""",
    "actors": """
legacy-actor standing message 2 64 1 100 13007856ffffffffffff0000ff0103000000000000000002b004d4feb004d4feb004d4fe000000000040401fffffffffffffffff00000000000048c10000214200004040000000000000007d24fa000000000000ff7f0000
openh1-actor standing 00001300785600010302b004d4fe0040401f000048c10000214200004040007d24fa0000
legacy-actor riding message 2 64 1 100 130078562100330001000100ff0103000000000000000002b004d4feb004d4feb004d4fe000000000040401fffffffffffffffff00000000000048c10000214200004040000000000000007d24fa000000000000ff7f0000
openh1-actor riding 01001300785601010302b004d4fe0040401f000048c10000214200004040007d24fa0000210033000100
legacy-actor moving message 2 64 1 100 13007856ffffffffffff0000ff0103ff02017fec00000002b0040000b004d4fe7805d4fe000000000040401fffffffffffffffff00000000000048c100002142000040407800d8ff0000007d24fa000000000000ff7f0000
openh1-actor moving c01e1300785600010302b004d4fe0040401f000048c10000214200004040007d24fa00007805d4feb00400007800d8ff00007fec000201ff
legacy-actor impulse message 2 64 1 100 13007856ffffffffffff4009040103000000000000000002b004d4feb004d4feb004d4fe18fc00000040401fffffffffffffffff00000000000048c10000214200004040000000000000007d24fa000000000000ff7f0000
openh1-actor impulse 02001300785640010302b004d4fe0040401f000048c10000214200004040007d24fa0000040918fc
legacy-actor speech message 2 64 1 100 13007856ffffffffffff0000ff0103000000000000000602b004d4feb004d4feb004d4fe000000000040401f0d0c0b0affffffff00000000000048c10000214200004040000000000000007d24fa000000000000ff7f0000
openh1-actor speech 04001300785600010302b004d4fe0040401f000048c10000214200004040007d24fa0000060d0c0b0a
legacy-actor animation message 2 64 1 100 13007856ffffffffffff0000ff0103000000000000000002b004d4feb004d4feb004d4fe000000000040401fffffffff0403020111000201000048c10000214200004040000000000000007d24fa000000000000ff7f0000
openh1-actor animation 08001300785600010302b004d4fe0040401f000048c10000214200004040007d24fa00000403020111000201
legacy-actor fleeing message 2 64 1 100 13007856ffffffffffff8000ff0103000000000000000002b004d4feb004d4feb004d4fe0000204e0040401fffffffffffffffff00000000000048c10000214200004040000000000000007d24fa000000000000ff7f0000
openh1-actor fleeing 10001300785680010302b004d4fe0040401f000048c10000214200004040007d24fa0000204e
legacy-actor camouflaged message 2 64 1 100 13007856ffffffffffff1800ff0103000000000000800002b004d4feb004d4feb004d4fe000000000040401fffffffffffffffff00000000000048c10000214200004040000000000000007d24fa000084030000007d0000
openh1-actor camouflaged 20011300785618010302b004d4fe0040401f000048c10000214200004040007d24fa00008084030000007d
""",
    "actor-messages": """
legacy-actor-messages message 3 64 13 1156 sha256:5df5e72ecc3930fb
message 3 64 13 1156 sha256:5c9598d5d7b4aad5
message 3 64 4 364 sha256:69550b0c2b25db5a
""",
}


def normalized(line):
    """a line as recorded"""
    head, _, tail = line.rpartition(" ")
    return f"{head} sha256:{hashlib.sha256(tail.encode()).hexdigest()[:16]}" if len(tail) > 256 else line


def golden_lines(output):
    return [normalized(line) for line in output.splitlines() if line]


def recorded(case):
    return GOLDEN.get(case, "").strip("\n").splitlines()


def generated(fault=None):
    legacy = read(LEGACY)
    legacy_header = read(LEGACY_HEADER)
    distributed = read(DISTRIBUTED)
    distributed_header = read(DISTRIBUTED_HEADER)
    actors = read(ACTORS)
    code = "\n".join(function(legacy, name) for name in LEGACY_FUNCTIONS)
    code += "\n" + "\n".join(function(distributed, name) for name in DISTRIBUTED_FUNCTIONS)
    code += "\n" + "\n".join(function(actors, name) for name in ACTOR_FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    config = "enum { NUMBER_OF_PLAYER_POWERUPS = 2, NO_PLAYER = 0xFF, NO_IMPULSE = 0xFF };\n"
    config += "#define DISTRIBUTED_UNIT_SCALE " + re.search(r"#define DISTRIBUTED_UNIT_SCALE\s+(\S+)",
                                                            distributed_header)[1] + "\n"
    config += "#define SUBSTEP_POSITION_SCALE " + re.search(r"#define SUBSTEP_POSITION_SCALE\s+(\S+)",
                                                            distributed)[1] + "\n"
    config += "#define DISTRIBUTED_VELOCITY_SCALE " + re.search(r"#define DISTRIBUTED_VELOCITY_SCALE\s+(\S+)",
                                                                distributed_header)[1] + "\n"
    config += re.search(r"#define DATAGRAM_ENTRIES\(type\) \\\n.*?\n.*?\n", distributed_header)[0]
    config += enum_with(distributed_header, "_distributed_message_player_prediction = 1") + "\n"
    config += structure(distributed_header, "distributed_vector") + "\n"
    config += structure(distributed_header, "distributed_message_header") + "\n"
    # (a rider's deck part, which the unit state carries: vehicle_riding.h's)
    config += structure(read("port/linux/game/vehicle_riding.h"), "vehicle_riding_deck") + "\n"
    # (the Legacy encodings' constants and structures, whole)
    config += legacy_header[legacy_header.index("/* ---------- constants */"):legacy_header.rindex("#endif")]
    config += "boolean distributed_real_valid(real value);\n"
    config += legacy[legacy.index("/* ---------- constants */"):legacy.index("/* ---------- private code */")]
    for member in ("UNIT_BASE_TICKS", "_unit_difference_form_whole", "_unit_difference_flags_bit"):
        config += enum_with(distributed, member) + "\n"
    for member in ("_actor_part_riding_bit", "ACTOR_COMPACT_MINIMUM_SIZE"):
        config += enum_with(actors, member) + "\n"
    return (("config.inc", config), ("under_test.inc", code))


def case_lines(case, fault=None):
    return run(build("legacy_golden", generated(fault)), case)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = case_lines(case)
    assert status == 0, output
    if os.environ.get("GOLDEN_RECORD"):
        print("\n" + output)
        return
    lines = golden_lines(output)
    expected = recorded(case)
    assert lines == expected, "\n".join(f"{a!r} != {b!r}" for a, b in zip(lines, expected) if a != b) or \
        f"{len(lines)} lines, {len(expected)} recorded"


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = case_lines(case, fault)
    assert status != 0 or golden_lines(output) != recorded(case), output
