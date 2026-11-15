"""The menus' Spartans' fidgets and looks (port/linux/game/menu_spartan.c, docs/menu_screens.md's <spartan>): each
place fidgets on the menus' clock at its own times (10 to 20 s apart, stable however often it is asked, never two
starting together, never the same fidget twice running), a lineup's places each turned, nearer or further and idling
at their own time (stable and bounded; one alone hardly turned), and a fidget blended in from the idle and back out
with no snap."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["schedule", "variation", "blend"]

SPARTAN = "port/linux/game/menu_spartan.c"

FUNCTIONS = ["spartan_random", "spartan_random_part", "spartan_variation_find", "spartan_fidget_kind",
             "spartan_schedule_advance", "spartan_ease", "spartan_fidget_length", "spartan_look_turn",
             "spartan_fidget_progress", "spartan_orientations_blend"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # every place's first fidget from the same number: all at once
    "first-in-step": ("spartan_random(index, 5)", "spartan_random(0, 5)", "schedule"),
    # nothing keeping two places' starts apart
    "not-apart": ("at = schedule->last_start + SPARTAN_FIDGET_APART;", "(void)0;", "schedule"),
    # a place's look new each time it is asked (each frame)
    "look-per-frame": ("variation->nearer = spartan_random_part(index, 3) * 2.0f - 1.0f;",
                       "variation->nearer = (real)rand() / (real)RAND_MAX * 2.0f - 1.0f;", "variation"),
    # a lineup turned far past a few degrees
    "turn-unbounded": ("(SPARTAN_LINEUP_TURN_MAXIMUM - SPARTAN_LINEUP_TURN_MINIMUM) *",
                       "10.0f * (SPARTAN_LINEUP_TURN_MAXIMUM - SPARTAN_LINEUP_TURN_MINIMUM) *", "variation"),
    # a fidget snapped in, not blended from the idle
    "snapped": ("pose->weight = MIN(spartan_ease((real)elapsed / blend_in),",
                "pose->weight = 1.0f; (void)MIN(spartan_ease((real)elapsed / blend_in),", "blend"),
}


def generated(spartan):
    defines = "".join(re.findall(r"^#define (?:SPARTAN|NUMBER_OF_SPARTAN)_\w+ .*\n", spartan, re.M))
    structures = "\n".join(structure(spartan, name) for name in
                           ("spartan_variation", "spartan_fidget_pose", "spartan_schedule"))
    functions = "\n\n".join(function(spartan, name) for name in FUNCTIONS)
    return (("under_test.inc", defines + "\n" + structures + "\n\n" + functions + "\n"),)


def executable(spartan):
    return build("menu_spartan_fidgets", generated(spartan))


@pytest.mark.parametrize("case", CASES)
def test_menu_spartan_fidgets(case):
    status, output = run(executable(read(SPARTAN)), case)
    assert status == 0, output


@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    before, after, case = NEGATIVE_CONTROLS[name]
    status, output = run(executable(mutated(read(SPARTAN), before, after)), case)
    assert status == CHECK_FAILED, f"{name}: {case} did not catch it ({status}): {output}"
