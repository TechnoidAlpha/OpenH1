"""Another client's player is corrected against this machine's own history of its copy, at the tick the host names
(#287, stage 2c): running ahead on fresher input is not pulled back; a real disagreement is corrected by its size.
The host's word of one of its sub-steps is compared with the copy as the same sub-step ended (stage 2d)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run

SOURCE = "port/linux/game/network_distributed.c"
CASES = ["ahead", "wrapped", "apart", "no-history", "unpredicted", "relayed", "inactive", "substep-ahead",
         "substep-apart", "substep-no-history"]

# a fault in distributed_correct_remote_unit and the case that must catch it
NEGATIVE_CONTROLS = {
    # compared with the copy's tick after the one named: a tick ahead, pulled back
    "tick-after": (("(word)key) - 1;", "(word)key);"), "ahead"),
    # compared with where the copy is now, as before stage 2c
    "against-now": (("error.i = state->position.x - noted->position.x;",
                     "error.i = state->position.x - object->object.position.x;"), "ahead"),
    # a disagreement left as it is
    "never-corrected": (("distributed_statistics.remote_history_corrections++;", "return TRUE;"), "apart"),
    # the ticks since not moved with it, to be corrected again
    "history-kept": (("other->key >= time)\n\t\t{\n\t\t\tother->position.x += error.i;",
                      "other->key >= time)\n\t\t{\n\t\t\t(void)0;"), "apart"),
    # a sub-step's word compared with the tick's end (#287, stage 2d)
    "substep-against-tick": (("noted = substep_index == NONE ?", "noted = TRUE ?"), "substep-ahead"),
    # the sub-steps of the tick named before the one named moved too
    "substep-history-before": (("substep >= substep_index", "substep >= 0"), "substep-apart"),
    # the sub-steps since not moved with it
    "substep-history-kept": (("other->key > time || (other->key == time", "FALSE || (FALSE"), "substep-apart"),
}


def generated(fault=None):
    source = read(SOURCE)
    code = function(source, "distributed_correct_remote_unit")
    if fault:
        code = mutated(code, *fault)
    header = read("port/linux/game/network_distributed.h")
    config = ""
    for name in ("REMOTE_CORRECTION_TOLERANCE", "REMOTE_BLEND_DISTANCE"):
        config += f"#define {name} " + re.search(rf"^#define {name}\s+(\S+)", header, re.M)[1] + "\n"
    config += f"enum {{ REMOTE_POSITION_TICKS = {constant(source, 'REMOTE_POSITION_TICKS')} }};\n"
    config += "enum { MAXIMUM_SIM_SUBSTEPS = " + re.search(r"MAXIMUM_SIM_SUBSTEPS = (\d+)",
                                                           read("port/linux/game/sim_rate.h"))[1] + " };\n"
    config += re.search(r"static (struct distributed_remote_position\s*\{.*?\n\})", source, re.S)[1] + ";\n"
    config += "static struct distributed_remote_position distributed_remote_positions[MAXIMUM_TRACKED_PLAYERS]" \
              "[REMOTE_POSITION_TICKS];\n"
    config += "static struct distributed_remote_position distributed_remote_substep_positions" \
              "[MAXIMUM_TRACKED_PLAYERS][REMOTE_POSITION_TICKS][MAXIMUM_SIM_SUBSTEPS - 1];\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("remote_history", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("remote_history", generated(fault)), case)
    assert status == CHECK_FAILED, output
