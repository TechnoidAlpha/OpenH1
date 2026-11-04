"""An OpenH1 game's actors to far clients (network_actors.c's actor_state_pressed, network_objects.c's
distributed_aims_at; NETCODE.md "Actors to far clients"): a unit's press goes to every client at once, a steady turn or a
stick's drift does not until it adds up to one; a unit aims at a player by the line of its aim."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

ACTORS = "port/linux/game/network_actors.c"
OBJECTS = "port/linux/game/network_objects.c"
CASES = ["same", "flags", "trigger", "start", "drift", "turn", "wrap", "animation", "aims"]

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # the control flags not compared
    "flags": (("last->control_flags != state->control_flags ||", ""), "flags"),
    # a touch of the trigger from none not a pull
    "trigger": (("||\n\t\t(last->primary_trigger == 0) != (state->primary_trigger == 0)", ""), "trigger"),
    # every change of a stick a press
    "drift": ((">= PRESSED_STICK_STEP", ">= 1"), "drift"),
    # the aim compared without its wrap
    "wrap": (("ABS((long)(short)(word)((word)a - (word)b))", "ABS((long)a - (long)b)"), "wrap"),
    # what was sent not noted
    "not-noted": (("last->aiming, state->aiming, sizeof(last->aiming));", "last->aiming, state->aiming, 0);"),
                  "turn"),
    # behind the aimer too
    "behind": (("return along > 0.0f && (", "return (along > 0.0f || 1) && ("), "aims"),
}


def generated(fault=None):
    actors = read(ACTORS)
    objects = read(OBJECTS)
    code = "\n".join(function(actors, name) for name in ("angle_difference", "actor_state_pressed"))
    code += "\n" + function(objects, "distributed_aims_at")
    if fault:
        code = mutated(code, *fault)
    config = "enum { _angle_yaw, _angle_pitch, NUMBER_OF_ANGLES };\n"
    config += "enum { " + ", ".join(f"{name} = {constant(actors, name)}" for name in
                                  ("PRESSED_TRIGGER_STEP", "PRESSED_STICK_STEP", "PRESSED_AIM_STEP")) + " };\n"
    config += "".join(re.search(r"^#define " + name + r" .*\n", objects, re.M).group(0) for name in
                      ("AIMED_AT_DISTANCE", "AIMED_AT_COSINE"))
    config += structure(actors, "distributed_actor_state") + "\n"
    config += structure(actors, "actor_pressed") + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("actor_pressed", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("actor_pressed", generated(fault)), case)
    assert status == CHECK_FAILED, output
