"""The menus' Spartan's color (port/linux/game/menu_screen.c's screen_spartan_frame, port/linux/game/menu_spartan.c's
menu_spartan_color, docs/menu_screens.md's <spartan>): a random color gives a live Spartan, not the original "?"
picture, whose armor goes through the profile colors in their list's order on the menus' frame clock, each Spartan
side by side a part of the round on from the one before; a profile's own color stays its own, and the picture is
kept where there is no Spartan (no tags) or no profile."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["random", "cycle", "offset", "fixed", "fallbacks", "lineup"]

SCREEN = "port/linux/game/menu_screen.c"
SPARTAN = "port/linux/game/menu_spartan.c"
HEADER = "port/linux/game/menu_spartan.h"

# a fault in the code, the file it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    # a lineup's places all at the box's middle
    "places-stacked": (SCREEN, "(real)block->box_width * ((real)place + 0.5f) / (real)places",
                       "(real)block->box_width * 0.5f", "lineup"),
    # random given the original picture, as before
    "random-pictured": (SCREEN, "(color != MENU_SPARTAN_RANDOM && !team && (color < 0 || color >= SCREEN_COLOR_COUNT))",
                        "(color < 0 || color >= SCREEN_COLOR_COUNT)", "random"),
    # the clock not read: one color for good
    "clock-ignored": (SPARTAN, "at = (milliseconds % round +", "at = (0 % round +", "cycle"),
    # every Spartan side by side the same color at once
    "not-offset": (SPARTAN, "(unsigned long)(index & 7)", "0UL", "offset"),
    # a color passed over in the round
    "color-skipped": (SPARTAN, "color = (long)(at / step);", "color = (long)(at / step) * 2 % count;", "cycle"),
}


def defines(text, *names):
    return "".join(re.search(r"^#define " + name + r"\b.*\n", text, re.M).group(0) for name in names)


def generated(screen, spartan):
    header = read(HEADER)
    enum = header[header.index("enum\n{"):header.index("};", header.index("enum\n{")) + 2]
    return (("menu_spartan_types.inc", enum + "\n" +
             defines(screen, "SCREEN_COLOR_COUNT", "SCREEN_SPARTAN_RANDOM_FRAME", "SCREEN_SPARTAN_NONE",
                     "SCREEN_SPARTAN_NO_PROFILE", "SCREEN_SPARTAN_EMPTY") +
             defines(spartan, "SPARTAN_RANDOM_HOLD", "SPARTAN_RANDOM_BLEND")),
            ("under_test.inc", function(screen, "screen_spartan_frame") + "\n\n" +
             function(spartan, "menu_spartan_color") + "\n"))


def executable(screen, spartan):
    return build("menu_spartan", generated(screen, spartan))


@pytest.mark.parametrize("case", CASES)
def test_menu_spartan(case):
    status, output = run(executable(read(SCREEN), read(SPARTAN)), case)
    assert status == 0, output


@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    path, before, after, case = NEGATIVE_CONTROLS[name]
    screen, spartan = read(SCREEN), read(SPARTAN)
    if path == SCREEN:
        screen = mutated(screen, before, after)
    else:
        spartan = mutated(spartan, before, after)
    status, output = run(executable(screen, spartan), case)
    assert status == CHECK_FAILED, f"{name}: {case} did not catch it ({status}): {output}"
