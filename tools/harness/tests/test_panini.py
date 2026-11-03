"""display.panini (port/linux/include/panini_projection.h, port/linux/game/panini.c): the Panini projection's forward
and inverse round trips at every strength, vertical compression and field of view; d 0 and s 0 the rectilinear view;
the view's sides at the screen's sides, vertical lines straight, the middle magnified by the scale (1.5 at 120 degrees,
2.43 at 150); the rectilinear view covering the whole screen through it; a thing on the horizon keeping its shape at
full strength; what the HUD places over a point (render_camera_view_to_screen) where the pass draws it, a split
screen's off-centre view too; the middle's magnification only while a window is drawn through it; off, nothing; the
waypoints' arrows. The pass's GLSL has the same arithmetic."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, mutated, read, run  # noqa: E402

CASES = ["round_trip", "identity", "edges", "coverage", "shape", "hud_matches_pass", "center", "off", "direction"]
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
    # the HUD's place from the top for the bottom
    (GAME, "(window.top - y) / (window.top - window.bottom)", "(y - window.bottom) / (window.top - window.bottom)",
     "hud_matches_pass"),
    # the scale from the nearer side of an off-centre view
    (GAME, "MAX(-window.left, window.right)", "-window.left", "hud_matches_pass"),
    # the HUD's marks placed by the rectilinear view while the pass curves it
    (GAME, "\tif (!panini_window_projects(camera, frustum))\n\t\treturn FALSE;\n\t*visible",
     "\tif (TRUE)\n\t\treturn FALSE;\n\t*visible", "hud_matches_pass"),
    # the magnification kept after the window
    (GAME, "void panini_window_end(void)\n{\n\twindow.active = FALSE;", "void panini_window_end(void)\n{\n",
     "center"),
    # on whatever the setting says
    (GAME, "if (!panini_settings(&d, &s) || ", "if ((panini_settings(&d, &s), FALSE) || ", "off"),
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
    game = game.replace("double config_real(const char *name);\nunsigned long config_changes(void);\n", "")
    return (("projection.inc", projection), ("under_test.inc", game))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("panini", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("file,before,after,case", CONTROLS)
def test_negative_control(file, before, after, case):
    status, output = run(build("panini", generated((file, before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_pass_has_the_same_arithmetic():
    # tools/vulkan_post_shaders.py's PANINI_SOURCE (Metal's is compiled from it): the inverse of the header's
    shader = (ROOT / "tools/vulkan_post_shaders.py").read_text()
    source = shader[shader.index("PANINI_SOURCE = ("):]
    source = "".join(re.findall(r'"((?:\\.|[^"\\])*)"', source[:source.index("\n)\n")]))
    for line in ("float k = projected.x * projected.x / ((d + 1.0) * (d + 1.0));",
                 "float c = max((sqrt(1.0 + k * (1.0 - d * d)) - k * d) / (k + 1.0), 1.0e-6);",
                 "float S = (d + 1.0) / (d + c);",
                 "return vec2(projected.x / (S * c), projected.y / (S * (panini.y + (1.0 - panini.y) * c)));",
                 "vec2 projected = p / panini.z;"):
        assert line in source, line
    header = read(PROJECTION)
    for line in ("float k = u * u / ((d + 1.0f) * (d + 1.0f));",
                 "float c = (sqrtf(1.0f + k * (1.0f - d * d)) - k * d) / (k + 1.0f);",
                 "S = (d + 1.0f) / (d + c);", "*x = u / (S * c);",
                 "*y = v / (S * (projection->s + (1.0f - projection->s) * c));"):
        assert line in header, line


def test_the_hud_and_the_weapon_take_it():
    # the projection's hook where every HUD mark is placed, the reticle and the passenger's offset scaled by the
    # middle's magnification, the weapon's projection widened by it, the pass after the anti-aliasing
    assert "if (panini_view_to_screen(camera, frustum, view_point, screen_point, &result))" in \
        read("source/render/render_cameras.c")
    assert "render_fov_reticle_scale(render.local_player_index) * panini_center_scale() : 1.0f);" in \
        read("source/interface/hud_draw.c")
    assert "render_fov_reticle_scale(local_player_index) * panini_center_scale();" in \
        read("port/linux/game/passenger_camera.c")
    assert "angle = 2.0f * atanf(tanf(angle * 0.5f) * panini_scale);" in read("port/linux/game/view_fov.c")
    render = read("source/render/render.c")
    assert render.index("halo_screen_anti_alias(") < render.index("panini_window_draw();") < \
        render.index("interface_draw_screen();")


def test_the_settings():
    config = read("port/linux/src/port_config.c")
    # (off by default: its strength 0)
    assert '{ "display.panini", _config_real, "0.0", "HALO_PANINI",' in config
    assert '{ "display.panini_vertical", _config_real, "0.0", "HALO_PANINI_VERTICAL",' in config
    menu = read("port/linux/game/menu_functions.c")
    assert '{ "op_panini", "display.panini", 0.0, 1.0, 0.25, 0.0, 1.0, FALSE, SLIDER_STOPS(settings_panini_stops) },' \
        in menu
    assert '{ "op_panini_vertical", "display.panini_vertical", 0.0, 1.0, 0.05, 0.0, 1.0,' in menu
