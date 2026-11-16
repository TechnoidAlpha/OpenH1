"""A client keeps another player standing on its elevator at its own floor's height against the host's word, a
platform's phase away, and takes the host's word otherwise (port/linux/NETCODE.md, co-op's elevators)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

SOURCE = "port/linux/game/network_distributed.c"
HEADER = "port/linux/game/network_distributed.h"
CASES = ["riding", "riding-up", "jump", "walked-off", "on-foot", "far", "across"]

# a fault and the case that must catch it
NEGATIVE_CONTROLS = {
    # the support surface asked for, which a machine's floor never has
    "support-surface": (("biped->biped.elevator_ticks != 60)",
                         "biped->biped.support_surface_index == NONE || biped->biped.elevator_ticks != 60)"), "riding"),
    "no-phase-bound": (("!(error->k >= -ELEVATOR_PHASE_TOLERANCE && error->k <= ELEVATOR_PHASE_TOLERANCE)", "FALSE"),
                       "far"),
}


def generated(fault=None):
    source = read(SOURCE)
    code = function(source, "distributed_supported_elevator") + "\n" + \
        function(source, "distributed_remote_rider_height_kept")
    if fault:
        code = mutated(code, *fault)
    config = "#define ELEVATOR_PHASE_TOLERANCE " + \
        re.search(r"^#define ELEVATOR_PHASE_TOLERANCE\s+(\S+)", source, re.M)[1] + "\n"
    config += "#define REMOTE_BLEND_DISTANCE " + \
        re.search(r"^#define REMOTE_BLEND_DISTANCE\s+(\S+)", read(HEADER), re.M)[1] + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("elevator_remote_rider", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("elevator_remote_rider", generated(fault)), case)
    assert status == CHECK_FAILED, output
