"""display.shield_color (port/linux/game/shield_color.c): each local player's own unit's shield in their own chosen
color (their profile's, #894), any other unit's in its own."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["players"]
CONTROLS = [
    # one color for every player: player 1's
    ('config_player_string(local_player_index, "display.shield_color")', 'config_player_string(0, "display.shield_color")',
     "players"),
    # the color of the marker's player not the one drawn: the first chosen's for all
    ("*chosen = player_profile_get_rgb_color(shield_color_indices[local_player_index]);",
     "*chosen = player_profile_get_rgb_color(shield_color_indices[0] != NONE ? shield_color_indices[0] : "
     "shield_color_indices[1]);", "players"),
    # read once, not again as the settings change
    ("if (shield_color_changes == config_changes())", "if (shield_color_changes != (unsigned long)-1)", "players"),
]


def generated(fault=None):
    code = read("port/linux/game/shield_color.c")
    code = code[code.index("/* the multiplayer armor colors"):]
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("shield_color", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("shield_color", generated((before, after))), case)
    assert status == CHECK_FAILED, output
