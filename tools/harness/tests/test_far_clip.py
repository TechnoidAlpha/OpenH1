"""display.draw_distance (port/linux/game/far_clip.c's render_far_clip_distance): the far clip plane the local views
are drawn to, the game's own by default; and where the game takes it (the projection, the structure's culling) and
what keeps the game's own (the teammates' marks' fade)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, player_settings_slots, read, run  # noqa: E402

CASES = ["default", "farther", "out_of_range", "script_nearer", "script_farther", "players"]
_KEPT = "!(players[slot].distance > game_distance)"
CONTROLS = [
    # the setting unread
    ("\treturn players[slot].distance;\n}", "\treturn game_distance;\n}", "farther"),
    # a script's nearer far clip overridden by the setting
    (f"if (!(game_distance >= rasterizer_global_defaults.far_clip_distance) || {_KEPT})",
     f"if ({_KEPT})", "script_nearer"),
    # the setting in place of a script's farther one
    (f"if (!(game_distance >= rasterizer_global_defaults.far_clip_distance) || {_KEPT})",
     "if (!(game_distance >= rasterizer_global_defaults.far_clip_distance) || !players[slot].distance)",
     "script_farther"),
    # read once, never again
    ("if (!players[slot].initialized || players[slot].read_at != changes)",
     "if (!players[slot].initialized || !players[slot].read_at)", "farther"),
    # no upper bound
    ("setting <= DRAW_DISTANCE_MOST", "TRUE", "out_of_range"),
    # one value kept for every player
    ("short slot = PLAYER_SETTINGS_SLOT(local_player_index);", "short slot = 0;", "players"),
    # config.toml's for every player
    ("config_player_real(local_player_index, \"display.draw_distance\")",
     "config_player_real(NONE, \"display.draw_distance\")", "players"),
]


def generated(fault=None):
    code = read("port/linux/game/far_clip.c")
    text = player_settings_slots() + "\n".join(re.findall(r"^#define DRAW_DISTANCE_\w+ .*$", code, re.M)) + "\n" + \
        function(code, "render_far_clip_distance")
    if fault:
        text = mutated(text, *fault)
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("far_clip", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("far_clip", generated((before, after))), case)
    assert status == CHECK_FAILED, output
