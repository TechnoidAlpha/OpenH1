"""An OpenH1 game's client clock led ahead of the host's by its trip there (#287, stage 2e; network_clock.c): the host's
figure of each second's inputs, a client's plan for it (whole ticks skipped, the rest at once ahead, else run faster or
slower), and the loop of them settling just before the host's tick from a game's start on networks of 0 to 61 ms."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

SOURCE = "port/linux/game/network_clock.c"
FUNCTIONS = ("network_clock_window_figure", "network_clock_plan_report", "network_clock_frame_part")
CASES = ["figure", "start", "nearest", "lead-limit", "near", "far-ahead", "frame", "converge-20", "converge-31",
         "converge-61", "converge-lan", "frames-server", "frames-60"]

# a fault in the code under test and the case that must catch it
NEGATIVE_CONTROLS = {
    # the earliest input of the window: one held up moves the clock
    "figure-earliest": (("samples[count * CLOCK_REPORT_PERCENTILE / 100]", "samples[0]"), "figure"),
    # the whole ticks rounded down: nearly a tick run at once every report
    "ticks-floor": (("(error + CLOCK_TICK_MICROSECONDS / 2) / CLOCK_TICK_MICROSECONDS",
                     "error / CLOCK_TICK_MICROSECONDS"), "nearest"),
    # no limit to the lead
    "no-lead-limit": (("PIN(ticks, 0, MAX(0, CLOCK_LEAD_LIMIT_TICKS - ticks_ahead))", "ticks"), "lead-limit"),
    # the whole difference planned each report: the clock overshoots and swings
    "no-gain": (("plan->seconds = (real)error / 1000000.0f * CLOCK_GAIN;",
                 "plan->seconds = (real)error / 1000000.0f * 2.5f;"), "converge-31"),
    # no ticks skipped at the start: the clock only slews there
    "no-jump": (("if (error > CLOCK_JUMP_MICROSECONDS)", "if (FALSE)"), "start"),
    # held to the target alone: back and forth across a host's frame, every report
    "no-hold": (("else if (early > CLOCK_EARLY_HIGH_MICROSECONDS)", "else if (TRUE)"), "near"),
    # the slew not limited
    "no-slew": (("part = PIN(*seconds, -limit, limit);", "part = *seconds;"), "frame"),
}


def generated(fault=None):
    source = read(SOURCE)
    code = "\n".join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    header = read("port/linux/game/network_clock.h")
    config = re.search(r"(enum\s*\{\s*/\* a tick, in microseconds.*?\};)", header, re.S)[1] + "\n"
    config += "\n".join(re.findall(r"^#define CLOCK_\w+ .*$", header, re.M)) + "\n"
    config += re.search(r"(struct network_clock_plan\s*\{.*?\};)", header, re.S)[1] + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("clock_phase", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("clock_phase", generated(fault)), case)
    assert status == CHECK_FAILED, output
