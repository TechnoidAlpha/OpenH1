"""The touchscreen's skip button during a cinematic: the real touch_cinematic.c draws it where the touch controls would
show (SKIP, and in network co-op VOTED once this machine has voted) in the letterbox's bar and says whether a tap
presses A, only one on it while it shows; touch_game.c says a cinematic can be skipped, or voted to skip by a network
co-op client, whose own cinematic never can be."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["solo-button", "no-button-any-tap", "not-over-a-menu", "stale-button", "coop-vote",
         "window-inside-the-screen", "skippable"]
BUTTON = ("touch_cinematic_label", "touch_cinematic_button_enabled", "touch_cinematic_draw", "touch_cinematic_tap")
GAME = ("touch_game_cinematic_skippable",)

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "button-takes-any-tap": (("if (!touch_cinematic.drawn ||", "if (TRUE ||"), "touch_cinematic_tap", "solo-button"),
    "no-slack-for-a-finger": (("button.x0 - slack - viewport->x0", "button.x0 - viewport->x0"), "touch_cinematic_draw",
                              "solo-button"),
    "button-under-the-bar": (("window_height * TOUCH_CINEMATIC_TOP / 1000", "window_height / 8"),
                             "touch_cinematic_draw", "solo-button"),
    "button-never-goes": (("system_milliseconds() - touch_cinematic.drawn_at > TOUCH_CINEMATIC_MILLISECONDS", "FALSE"),
                          "touch_cinematic_tap", "stale-button"),
    "button-over-a-menu": ((" || ui_widgets_active()", ""), "touch_cinematic_draw", "not-over-a-menu"),
    "button-before-the-vote": (("if (network_coop_active())", "if (FALSE)"), "touch_cinematic_label", "coop-vote"),
    "voted-not-shown": (('(*voted ? L"VOTED" : L"SKIP")', 'L"SKIP"'), "touch_cinematic_label", "coop-vote"),
    "button-at-the-screen-edge": (("button.x1 = (short)(window->x1 -", "button.x1 = (short)(viewport->x1 -"),
                                  "touch_cinematic_draw", "window-inside-the-screen"),
    # as before: a client's tap could not vote
    "client-cannot-vote": (("(cinematic_can_be_skipped() || network_coop_skip_offered())", "cinematic_can_be_skipped()"),
                           "touch_game_cinematic_skippable", "skippable"),
}


def generated(fault=None, faulty_function=None):
    button = read("port/linux/game/touch_cinematic.c")
    game = read("port/linux/game/touch_game.c")
    state = button[button.index("static struct\n{"):button.index("} touch_cinematic;") + len("} touch_cinematic;")]
    config = enum_with(button, "TOUCH_CINEMATIC_WIDTH") + "\n" + state + "\n"
    text = ""
    for source, names in ((button, BUTTON), (game, GAME)):
        for name in names:
            code = function(source, name)
            if fault and name == faulty_function:
                code = mutated(code, *fault)
            text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("touch_cinematic", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("touch_cinematic", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
