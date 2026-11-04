"""The host's plan of each client's players (distributed_host_plan_players, #287): a player aimed at from afar is in the
client's forwarded set (its input and live controls sent on as they come) on every tick of the aim, not only on the
ticks its distance would not send it; its state goes on the same ticks as before (a Legacy game's bytes). And which
players' states go to a client as each of the host's sub-steps ends (distributed_substep_state_due, stage 2d)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

SOURCE = "port/linux/game/network_distributed.c"
CASES = ["aimed", "legacy-sends", "unaimed", "scoped", "substep-due"]

# a fault in the plan and the case that must catch it
NEGATIVE_CONTROLS = {
    # the aim taken only on the ticks its distance does not send the player (the flicker)
    "aim-off-schedule": (("if (period > 1)\n\t\t\t\t{\n\t\t\t\t\taimed_period",
                          "if (period > 1 && (now + player_index) % period != 0)\n\t\t\t\t{\n\t\t\t\t\taimed_period"),
                         "aimed"),
    # the aim ignored
    "aim-ignored": (("aimed_period = MIN(period, distributed_aimed_period(", "aimed_period = MAX(period, distributed_aimed_period("),
                    "aimed"),
    # the state sent by the aim's period alone: a Legacy game's sends changed
    "sends-changed": (("(now + player_index) % period == 0 ||\n", ""), "legacy-sends"),
    # the scope not counted
    "scope-ignored": (("aimed_period = 1;", "aimed_period = MIN(aimed_period, 2);"), "scoped"),
    # far players' states at the sub-steps too
    "substep-far": (("period <= 2 && player_machine", "period <= 3 && player_machine"), "substep-due"),
    # riding players' states at the sub-steps
    "substep-riding": (("TEST_FLAG(state->flags, _distributed_unit_placed_bit) &&\n", ""), "substep-due"),
    # an OpenCE player's (the host runs no sub-steps of theirs as they come)
    "substep-opence": (("(player_machine == NONE || distributed_prediction_at_tick_start(player_index))", "TRUE"),
                       "substep-due"),
}


def generated(fault=None):
    source = read(SOURCE)
    code = "\n".join(function(source, name) for name in
                     ("distributed_aimed_period", "distributed_host_plan_players", "distributed_substep_state_due"))
    if fault:
        code = mutated(code, *fault)
    config = "#define MAX(a, b) ((a) > (b) ? (a) : (b))\n"
    for name in ("NEAR_PLAYER_DISTANCE", "MIDDLE_PLAYER_DISTANCE", "FAR_PLAYER_DISTANCE", "AIMED_AT_COSINE",
                 "SCOPED_AT_COSINE"):
        config += f"#define {name} " + re.search(rf"^#define {name}\s+(\S+)", source, re.M)[1] + "\n"
    for member in ("HIDDEN_PLAYER_PERIOD_TICKS", "_distributed_unit_alive_bit", "_distributed_send_state_bit"):
        config += enum_with(source, member) + "\n"
    config += re.search(r"static (struct distributed_viewer\s*\{.*?\n\} distributed_viewers\[.*?\];)", source,
                        re.S)[1] + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("plan_aim", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("plan_aim", generated(fault)), case)
    assert status == CHECK_FAILED, output
