"""An OpenH1 game's actors' states without the parts at their defaults (actor_state_compact_write and
actor_state_compact_read, OpenH1 protocol version 24): every state read back is the structure written, byte for byte,
with each part alone off its default and with all of them; a standing actor's is the least; cut short, none."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, needs_32_bit_abi, read, run, structure

pytestmark = needs_32_bit_abi("a state's longs go on the wire as the game's, 4 bytes")

SOURCE = "port/linux/game/network_actors.c"
HEADER = "port/linux/game/network_distributed.h"
# (the structure, the Legacy encoding's whole)
LEGACY_HEADER = "port/linux/game/network_legacy.h"
CASES = ["standing", "each-part", "short"]

# a fault in the encoding and the case that must catch it
NEGATIVE_CONTROLS = {
    # an impulse's number left at its default when there is no impulse
    "impulse-number-dropped": (("state->impulse != NO_IMPULSE || state->impulse_number || state->impulse_alignment",
                                "state->impulse != NO_IMPULSE"), "each-part"),
    # a speech's number dropped with no sound
    "speech-number-dropped": (("state->speech_sound != NONE || state->speech_number",
                               "state->speech_sound != NONE"), "each-part"),
    # the custom animation's interpolation ignored
    "animation-partial": (("state->user_animation_number ||\n\t\tstate->user_animation_interpolate)",
                           "state->user_animation_number)"), "each-part"),
    # the look taken for the aim on reading, but not compared on writing
    "looking-ignored": (("if (csmemcmp(state->looking, state->aiming, sizeof(state->aiming)))", "if (0)"),
                        "each-part"),
    # the up's default wrong on reading
    "up-default": (("state->up.k = (short)DISTRIBUTED_UNIT_SCALE;\n\tif (!actor_take", "state->up.k = 0;\n\tif (!actor_take"),
                   "standing"),
    # the trigger's pull left out
    "trigger-ignored": (("if (state->primary_trigger)", "if (0)"), "each-part"),
}

FUNCTIONS = ("actor_put", "actor_take", "actor_state_compact_write", "actor_state_compact_read")


def generated(fault=None):
    source = read(SOURCE)
    header = read(HEADER)
    code = "\n".join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    config = "enum { NO_IMPULSE = 0xFF };\n"
    config += "#define DISTRIBUTED_UNIT_SCALE " + re.search(r"#define DISTRIBUTED_UNIT_SCALE\s+(\S+)", header)[1] + "\n"
    legacy_header = read(LEGACY_HEADER)
    config += enum_with(legacy_header, "_angle_yaw") + "\n"
    for member in ("_actor_part_riding_bit", "ACTOR_COMPACT_MINIMUM_SIZE"):
        config += enum_with(source, member) + "\n"
    config += structure(legacy_header, "distributed_actor_state") + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("actor_compact", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("actor_compact", generated(fault)), case)
    assert status == CHECK_FAILED, output
