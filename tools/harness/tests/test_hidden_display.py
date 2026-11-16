"""A game window hidden on macOS (another desktop, covered, minimized; #646): the display link takes no frames, and the
host stands in for it with refreshes on the grid of its last ones (frame_pacing.h's frame_pacing_next_latch), each
latch once, a refresh apart, the ones passed skipped, never the link's last again."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

SOURCE = "port/linux/src/frame_pacing.h"
CASES = ["even", "early", "late", "no-grid"]

# a fault in the code under test and the case that must catch it
NEGATIVE_CONTROLS = {
    # the steps rounded down: a late wake takes a latch already passed
    "floor": (("+ period / 2) / period", ") / period"), "late"),
    # the steps rounded up: a wake a little late skips a refresh
    "ceiling": (("+ period / 2) / period", "+ period - 1) / period"), "even"),
    # a wake before the next latch counted from the last: the link's last latch, or one before it
    "unguarded": (("if (now + margin > last + period)", "if (1)"), "early"),
}


def generated(fault=None):
    code = function(read(SOURCE), "frame_pacing_next_latch")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hidden_display", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hidden_display", generated(fault)), case)
    assert status == CHECK_FAILED, output
