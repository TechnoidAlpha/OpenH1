"""The view a player's screen of the menus is drawn in (port/linux/game/menu_view.c, issue 887): at full screen as
before; in split screen a screen made for the menus' 640 by 480 scaled to fit the player's half or quarter, never
cut off, centred in it, the Xbox's own split screen pause menus at their size, centred, and a dim that covers the
screen's design across the whole view; the cameras and the menus' centring given back after each screen."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

SOURCE = "port/linux/game/menu_view.c"
CASES = ["full", "half", "quarter", "tall", "xbox", "pointer", "over"]
FUNCTIONS = ("menu_view_design", "menu_view_begin", "menu_view_end", "menu_view_side", "menu_view_canvas",
             "menu_view_scale", "menu_view_over_views")

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # issue 887: the screen laid out for the whole screen in a half, its foot cut off
    "not-scaled": (("\tscale = MIN(scale, 1.0f);\n", "\tscale = 1.0f;\n"), "half"),
    # the Xbox's split screen menus made larger than they were made
    "enlarged": (("\tscale = MIN(scale, 1.0f);\n", ""), "xbox"),
    # the dim left on the centred 640 (issue 887's third)
    "dim-centred": (("\tcanvas->x0 = (short)MIN(bounds->x0, -menu_view.left);",
                     "\tcanvas->x0 = bounds->x0;"), "half"),
    # the cameras left on the canvas for what is drawn after the screen
    "kept": (("\trender.camera.viewport_bounds = menu_view.render_viewport;\n", ""), "half"),
    # the pause menu drawn over every view, not in the player's own
    "pause-over": (("return name && strncmp(name, MENU_VIEW_PAUSE, sizeof(MENU_VIEW_PAUSE) - 1);",
                    "return name != NULL;"), "over"),
    # the screen not centred down a view taller than it needs
    "not-centred": (("\toffset->y = menu_view.top;\n", ""), "tall"),
}


def generated(fault=None):
    source = read(SOURCE)
    defines = "\n".join(re.findall(r"^#define MENU_VIEW_\w+ .+$", source, re.M))
    state = re.search(r"^static struct\n\{.*?^\} menu_view;", source, re.M | re.S).group(0)
    code = "\n\n".join([defines, state] + [function(source, name) for name in FUNCTIONS])
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_view", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("menu_view", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
