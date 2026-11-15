"""The touch controls' swipe and gyroscope read late in a frame for the drawn view (port/linux/src/touch_input.c's
touch_input_look_late, display.direct_camera): what came since the frame's input read them, kept for the next
frame's input (touch_input_look), so the player turns by every swipe once, as before."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CONTROLS = [
    # what the view read late lost to the player
    ("\t\tdelta[index] += look_carried[index];", "", "frames"),
    # ... or turned by twice
    ("\t\tlook_carried[index] = 0.0f;\n\t}", "\t}", "frames"),
    # the view turned the other way by the late swipe than the player is
    ("\t*yaw = -look_carried[0] * scale;", "\t*yaw = look_carried[0] * scale;", "frames"),
]


def generated(fault=None):
    code = read("port/linux/src/touch_input.c")
    carried = re.search(r"^static float look_carried\[4\];$", code, re.M).group(0)
    text = "\n".join([carried, function(code, "touch_input_look"), function(code, "touch_input_look_late")])
    if fault:
        text = mutated(text, *fault)
    return (("under_test.inc", text),)


def test_frames():
    status, output = run(build("touch_look_late", generated()), "frames")
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("touch_look_late", generated((before, after))), case)
    assert status == CHECK_FAILED, output
