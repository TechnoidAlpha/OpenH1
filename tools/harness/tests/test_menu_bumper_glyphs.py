"""The menus' LB and RB are drawn as the pad's shoulder buttons (controller_glyphs.c's controller_glyph_asset_index,
while controller_glyph_menu_bumper_draw draws for ui_widget.c's render_state_bitmap; the controller audit's white and
black discs on the browser's and Host Controls' tabs), the HUD's as display.hud_bumper_style says."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["menus", "view"]

NEGATIVE_CONTROLS = {
    # the code before the change: the setting alone, so the menus' discs
    "menus-as-the-setting": (("(controller_glyph_menu_bumpers || !strcmp(", "(FALSE || !strcmp("), "menus"),
    # the HUD's too
    "hud-always-modern": (("(controller_glyph_menu_bumpers || !strcmp(", "(TRUE || !strcmp("), "view"),
}


def generated(fault=None):
    code = function(read("port/linux/game/controller_glyphs.c"), "controller_glyph_asset_index")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][0])
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("menu_bumper_glyphs", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    status, output = run(build("menu_bumper_glyphs", generated(control)), NEGATIVE_CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} was not caught: {output}"
