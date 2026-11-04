"""The players' presses as the host's sub-steps ran them, to the clients it does not send those players' every control
to (network_substeps_live_far_due, NETCODE.md "Presses to far clients"): a press goes as it comes, a stick's drift or a
turn of the aim does not."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

SOURCE = "port/linux/game/network_substeps.c"
HEADER = "port/linux/game/network_substeps.h"
CASES = ["first", "same", "turned", "drift", "push", "strafe", "pull", "held", "sent", "player"]

# a fault in network_substeps_live_far_due and the case that must catch it
NEGATIVE_CONTROLS = {
    # the held controls not compared
    "held": (("a->held_flags != b->held_flags ||", ""), "held"),
    # the sticks not compared
    "sticks": (("ABS(b->throttle_i - a->throttle_i) >= LIVE_CONTROL_FAR_STICK_STEP ||", ""), "push"),
    # the trigger not compared
    "trigger": (("||\n\t\tABS(b->primary_trigger - a->primary_trigger) >= LIVE_CONTROL_FAR_TRIGGER_STEP", ""), "pull"),
    # every change sent: a stick's drift too
    "drift": ((">= LIVE_CONTROL_FAR_STICK_STEP ||\n\t\tABS(b->throttle_j", ">= 1 ||\n\t\tABS(b->throttle_j"), "drift"),
    # what was sent not noted
    "not-noted": (("*last = now;", ""), "push"),
}


def generated(fault=None):
    source = read(SOURCE)
    code = "\n".join(function(source, name) for name in
                     ("live_control_far_from", "live_control_far_pressed", "network_substeps_live_far_due"))
    if fault:
        code = mutated(code, *fault)
    config = "enum { " + ", ".join(f"{name} = {constant(source, name)}" for name in
                                  ("LIVE_CONTROL_FAR_STICK_STEP", "LIVE_CONTROL_FAR_TRIGGER_STEP")) + " };\n"
    config += structure(source, "live_control") + "\n"
    config += structure(read(HEADER), "live_control_far") + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("far_live_due", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("far_live_due", generated(fault)), case)
    assert status == CHECK_FAILED, output
