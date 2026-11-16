"""display.hud_button_style (port/linux/game/hud_hires_tags.c): each split screen player's view draws its buttons in
its own player's style, their profile's (#892), each style's pictures kept apart, so that the views do not make each
other's again every frame."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, player_settings_slots, read, run  # noqa: E402

CASES = ["players"]
CONTROLS = [
    # one style kept for every player
    ("short slot = PLAYER_SETTINGS_SLOT(local_player_index);", "short slot = 0;", "players"),
    # read once, not again as the settings change
    ("if (!players[slot].read || players[slot].read_at != config_changes())", "if (!players[slot].read)", "players"),
]


def generated(fault=None):
    code = function(read("port/linux/game/hud_hires_tags.c"), "hud_button_style")
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", player_settings_slots() + code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hud_button_style", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("hud_button_style", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_each_view_draws_its_own():
    code = read("port/linux/game/hud_hires_tags.c")
    draw = function(code, "hud_hires_button_draw")
    # (the view's player's style, each style's pictures their own: no style change forgets the others')
    assert "style = hud_button_style(render.local_player_index >= 0" in draw
    assert "cache = &hud_buttons[slot >= 6 ? 0 : style][slot];" in draw
    assert "hud_buttons_forget" not in draw
    # (the keyboard's prompts follow player 1's profile, the keyboard's player)
    assert 'config_player_string(0, "input.button_prompts")' in read("port/linux/src/xinput_sdl.c")
