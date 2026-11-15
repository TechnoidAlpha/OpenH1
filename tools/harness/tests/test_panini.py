"""display.panini (port/linux/include/panini_projection.h, port/linux/game/panini.c): the Panini projection's forward
and inverse round trips at every strength, vertical compression and field of view; d 0 and s 0 the rectilinear view;
the view's sides at the screen's sides, vertical lines straight, the middle magnified by the scale (1.5 at 120 degrees,
2.43 at 150); the rectilinear view covering the whole screen through it; a thing on the horizon keeping its shape at
full strength; what the HUD places over a point (render_camera_view_to_screen) where the pass draws it, a split
screen's off-centre view too; the middle's magnification only while a window is drawn through it; off, nothing; the
waypoints' arrows. The pass's GLSL has the same arithmetic."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, player_settings_slots, read, run  # noqa: E402

CASES = ["round_trip", "identity", "edges", "coverage", "shape", "hud_matches_pass", "center", "off", "direction",
         "players"]
PROJECTION = "port/linux/include/panini_projection.h"
GAME = "port/linux/game/panini.c"
# (file, before, after, the case that must fail)
CONTROLS = [
    # the inverse's cos phi from the other root
    (PROJECTION, "float c = (sqrtf(1.0f + k * (1.0f - d * d)) - k * d) / (k + 1.0f);",
     "float c = (sqrtf(1.0f + k * (1.0f - d * d)) + k * d) / (k + 1.0f);", "round_trip"),
    # the vertical compression the wrong way (1 / cos phi for cos phi)
    (PROJECTION, "*screen_y = projection->scale * S * y * (projection->s + (1.0f - projection->s) * c);",
     "*screen_y = projection->scale * S * y * (projection->s + (1.0f - projection->s) / c);", "round_trip"),
    # no fit to the screen: the sides drawn inside it
    (PROJECTION, "projection->scale = (projection->d + c) / ((projection->d + 1.0f) * c);",
     "projection->scale = c > 2.0f ? c : 1.0f;", "edges"),
    # the fit from the height: the view's sides cut off
    (PROJECTION, "projection->scale = (projection->d + c) / ((projection->d + 1.0f) * c);",
     "projection->scale = (projection->d + c) / ((projection->d + 1.0f) * c) * 1.1f;", "coverage"),
    # the Panini projection's S with d's sign turned: no shape kept
    (PROJECTION, "float S = (projection->d + 1.0f) / (projection->d + c);\n\n\t*screen_x",
     "float S = (projection->d + 1.0f) / (projection->d + c * c);\n\n\t*screen_x", "shape"),
    # (#594) the widened window's magnification its own, the zoomed view's
    (GAME, "if (widening == 1.0f || !(widening > 0.0f)", "if (TRUE || !(widening > 0.0f)", "center"),
    # the HUD's place from the top for the bottom
    (GAME, "(window.top - y) / (window.top - window.bottom)", "(y - window.bottom) / (window.top - window.bottom)",
     "hud_matches_pass"),
    # the scale from the nearer side of an off-centre view
    (GAME, "MAX(-window.left, window.right),\n", "-window.left,\n", "hud_matches_pass"),
    # the HUD's marks placed by the rectilinear view while the pass curves it
    (GAME, "\tif (!panini_window_projects(camera, frustum))\n\t\treturn FALSE;\n\t*visible",
     "\tif (TRUE)\n\t\treturn FALSE;\n\t*visible", "hud_matches_pass"),
    # the magnification kept after the window
    (GAME, "void panini_window_end(void)\n{\n\twindow.active = window.journaled = FALSE;", "void panini_window_end(void)\n{\n",
     "center"),
    # on whatever the setting says
    (GAME, "\tif (!panini_settings(local_player_index, &d, &s))\n\t{\n\t\twindow.viewport = camera->viewport_bounds;",
     "\tif ((panini_settings(local_player_index, &d, &s), FALSE))\n\t{\n\t\twindow.viewport = "
     "camera->viewport_bounds;", "off"),
    # one setting kept for every player (the first asked)
    (GAME, "short slot = PLAYER_SETTINGS_SLOT(local_player_index);", "short slot = 0;", "players"),
    # every window drawn as player 1's settings say
    (GAME, "\tif (!panini_settings(local_player_index, &d, &s))\n\t{\n\t\twindow.viewport = camera->viewport_bounds;",
     "\tif (!panini_settings(0, &d, &s))\n\t{\n\t\twindow.viewport = camera->viewport_bounds;", "players"),
    # the arrows' direction kept rectilinear
    (GAME, "\t*x = across;\n\t*y = -up;", "\t(void)across;\n\t(void)up;", "direction"),
]


def generated(fault=None):
    projection = read(PROJECTION)
    game = read(GAME)
    if fault:
        file, before, after = fault
        if file == PROJECTION:
            projection = mutated(projection, before, after)
        else:
            game = mutated(game, before, after)
    # (the game's code after its includes: the stubs are the test's)
    game = game[game.index("/* (the window's rasterizer target"):]
    return (("projection.inc", projection), ("under_test.inc", player_settings_slots() + game))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("panini", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("file,before,after,case", CONTROLS)
def test_negative_control(file, before, after, case):
    status, output = run(build("panini", generated((file, before, after))), case)
    assert status == CHECK_FAILED, output
