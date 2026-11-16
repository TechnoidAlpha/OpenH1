"""The pause boxes' width (port/linux/game/menu_tags.c's pause_boxes_fit): the Xbox's while a controller's buttons
are drawn under the rows (issue 751: the box had been left as wide as the keyboard's caps need whatever played), as
much wider as the keyboard's legends only while its keys are drawn (issue 596), the Xbox's again once a controller
plays; the campaign's mission objectives moved with the box's right, and back."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCE = "port/linux/game/menu_tags.c"
CASES = ["controller", "keyboard", "back", "none"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # issue 751: the box made wider whatever plays
    "wide-always": (("boolean wide = halo_keyboard_prompts() != 0;", "boolean wide = TRUE;"), "controller"),
    # widened again each frame the keyboard plays, not once
    "every-frame": (("if (wide == menu_tags.pause_boxes_wide)\n\t\treturn;", ""), "keyboard"),
    # never made narrower again once a controller plays
    "never-back": (("short growth = (short)(wide ? kept->growth : -kept->growth);",
                    "short growth = (short)(wide ? kept->growth : 0);"), "back"),
    # the objectives left where they were as the box grows
    "objectives-stay": (("\t\tif (kept->screen)\n\t\t\tpause_objectives_move(kept->screen, growth);\n", ""),
                        "keyboard"),
}


def generated(fault=None):
    source = read(SOURCE)
    boxes = re.search(r"^#define PAUSE_BOXES \d+$", source, re.M).group(0)
    growth = re.search(r"^struct pause_box_growth\n\{.*?^\};", source, re.M | re.S).group(0)
    members = re.search(r"^\tstruct pause_box_growth pause_boxes\[PAUSE_BOXES\];\n\tlong pause_box_count;\n"
                        r"\tboolean pause_boxes_wide;\n", source, re.M).group(0)
    code = "\n\n".join([boxes, growth, "static struct\n{\n" + members + "} menu_tags;"] +
                       [function(source, name) for name in ("pause_box_widen", "pause_objectives_move",
                                                            "pause_box_keep", "pause_boxes_fit")])
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("pause_box_fit", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("pause_box_fit", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
