"""The menus' LB and RB are drawn as the pad's shoulder buttons (controller_glyphs.c's controller_glyph_asset_index;
the controller audit's white and black discs on the browser's and Host Controls' tabs), the HUD's as
display.hud_bumper_style says."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["menus", "view"]

NEGATIVE_CONTROLS = {
    # the code before the change: the setting alone, so the menus' discs
    "menus-as-the-setting": ("controller_glyph_asset_index",
                             ("(controller_glyph_in_menus() || !strcmp(", "(FALSE || !strcmp("), "menus"),
    # the HUD's too
    "hud-always-modern": ("controller_glyph_in_menus", ("return render.local_player_index < 0 ||", "return TRUE ||"),
                          "view"),
}


def generated(fault=None):
    glyphs = read("port/linux/game/controller_glyphs.c")
    code = {name: function(glyphs, name) for name in ("controller_glyph_in_menus", "controller_glyph_asset_index")}
    if fault:
        where, change, _ = NEGATIVE_CONTROLS[fault]
        code[where] = mutated(code[where], *change)
    return (("under_test.inc", code["controller_glyph_in_menus"] + "\n" + code["controller_glyph_asset_index"] + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_bumper_glyphs", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("menu_bumper_glyphs", generated(control)), NEGATIVE_CONTROLS[control][2])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
