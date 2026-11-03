"""display.lowered_reticle (lowered_reticle.c): the view turned up from the aim so that the aim lands the setting's
fraction of the view's height below the middle at any field of view, the HUD's reticle drawn where the aim lands
(through the view as drawn, at the HUD's reticle scale), the first-person weapon posed along the aim, a scope's zoom
bringing the reticle back to the middle, nothing turned off or away from the player's aim. And where it is wired:
the view alone turns (main.c), never the observer, the aim or anything the simulation reads; the HUD and the weapon;
the settings' rows, sliders and defaults."""
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
CASES = ["off", "lands_on_the_reticle", "screen_offset", "hud_scale", "aim_camera", "zoom", "not_the_aim", "range",
         "changed"]
CONTROLS = [
    # the setting's switch unread: lowered when off
    ('config_boolean("display.lowered_reticle") ? ', "1 ? ", "off"),
    # the lift as a fraction of the half height, not of the whole
    ("atanf(2.0f * fraction * tanf(", "atanf(fraction * tanf(", "lands_on_the_reticle"),
    # the drawn field of view's angle taken as the half angle
    ("tanf(vertical_field_of_view * 0.5f)", "tanf(vertical_field_of_view)", "lands_on_the_reticle"),
    # the safe frame's height taken for the screen's: short of the fraction on a safe frame
    ("* viewport_height / window_height);", ");", "lands_on_the_reticle"),
    # the view turned down, not up
    ("turned_forward.i = forward->i * cosine_angle + up->i * sine_angle;",
     "turned_forward.i = forward->i * cosine_angle - up->i * sine_angle;", "lands_on_the_reticle"),
    # up left as it was: the frame no longer square
    ("\t*up = turned_up;\n", "\n", "lands_on_the_reticle"),
    # the HUD's offset from the view's middle, not the aim's
    ("\tlowered_reticle_turn(&aim, &up, -angle);\n", "\tlowered_reticle_turn(&aim, &up, 0.0f);\n", "screen_offset"),
    # the reticle moved by the screen's offset, then scaled by the HUD with it
    ("floorf(offset.y / reticle_scale + 0.5f)", "floorf(offset.y + 0.5f)", "hud_scale"),
    # the weapon posed at the view, above the reticle
    ("lowered_reticle_turn(&aim_camera->forward, &aim_camera->up, -angle);", "", "aim_camera"),
    # the zoom ignored: a scope's picture in the middle, its reticle below it
    ("fraction * lowered_reticle_zoom(unit, unit_index, game_field_of_view)", "fraction", "zoom"),
    # a view without a scope kept lowered however narrow
    ("\t\tzoomed_ratio = 0.5f;\n", "\t\tzoomed_ratio = 0.0f;\n", "zoom"),
    # the last view's lift kept on a view that is not the aim's
    ("\tlift_angles[local_player_index] = angle;\n", "\tif (angle != 0.0f) lift_angles[local_player_index] = angle;\n",
     "not_the_aim"),
    # not held at the range
    ("\t\tif (percent > LOWERED_RETICLE_MAXIMUM_PERCENT)\n", "\t\tif (FALSE)\n", "range"),
    # read once, not on a change
    ("if (!initialized || read_at != changes)", "if (!initialized || !read_at)", "changed"),
]


def generated(fault=None):
    code = read("port/linux/game/lowered_reticle.c")
    section = code[code.index("/* the farthest below the middle the reticle goes"):]
    if fault:
        section = mutated(section, *fault)
    return (("under_test.inc", section),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("lowered_reticle", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("lowered_reticle", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_view_turns_not_the_aim():
    # set_window_camera_values: the window's camera from the observer turned, after display.fov's angle (the drawn
    # angle the lift is found at), before the camera's shake; the observer itself, which aim assist and the sound read,
    # is never written
    camera = function(read("source/main/main.c"), "set_window_camera_values")
    fov = camera.index("render_fov_vertical(")
    turned = camera.index("lowered_reticle_view(window->local_player_index, observer->field_of_view,")
    assert camera.index("window->rasterizer_camera.up = observer->up;") < fov < turned
    assert "&window->rasterizer_camera);" in camera[turned:turned + 150]
    assert turned < camera.index("player_effect_get_camera_effect_matrix(")
    assert "/* port: (the view's, above the aim) */\n\t\t\t\t&window->rasterizer_camera.forward,\n" \
        "\t\t\t\t&window->rasterizer_camera.up);" in camera
    assert "observer->forward =" not in camera and "observer->up =" not in camera
    assert "&observer->forward" not in camera


def test_called_only_by_the_drawing():
    # nothing the simulation runs (the player's control, the unit's aim, the weapons, aim assist, the network) asks
    # for the lowered view: only the window's camera, the HUD's reticle and the first-person weapon's pose
    names = ["lowered_reticle_view", "lowered_reticle_lift", "lowered_reticle_screen_offset",
             "lowered_reticle_offset_point", "lowered_reticle_aim_camera"]
    found = subprocess.run(["git", "grep", "-l", "-w", *[part for name in names for part in ("-e", name)], "--",
                            "*.c", "*.h"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
    assert sorted(name for name in found if not name.startswith("tools/")) == [
        "port/linux/game/lowered_reticle.c", "port/linux/game/lowered_reticle.h",
        "source/interface/first_person_weapons.c",
        "source/interface/hud_draw.c", "source/main/main.c"]
    hud = function(read("source/interface/hud_draw.c"), "hud_draw_bitmap_with_meter")
    # the weapon's reticle only, and not on top of the passenger's shot, which is projected through the turned view
    assert "if (is_crosshair_bitmap && !passenger_camera_reticle_offset(&point))\n\t\tlowered_reticle_offset_point(" \
        "&point, reticle_scale);" in hud
    assert hud.index("reticle_scale = is_crosshair_bitmap") < hud.index("lowered_reticle_offset_point(")
    assert "\t\treticle_scale);" in hud[hud.index("hud_draw_bitmap_internal("):]


def test_the_weapon_posed_along_the_aim():
    # the pose, its interpolation (kept relative to the same camera) and display.viewmodel_offset_* (along the aim's
    # axes) all at the camera turned back to the aim
    build_matrices = function(read("source/interface/first_person_weapons.c"), "first_person_weapon_build_node_matrices")
    pose = build_matrices.index("pose_camera= lowered_reticle_aim_camera(")
    assert pose < build_matrices.index("&pose_camera->position,\n\t\t\t&pose_camera->forward,\n\t\t\t&pose_camera->up);")
    interpolation = build_matrices.index("render_interpolation_first_person(")
    assert "\t\t\tpose_camera);" in build_matrices[interpolation:interpolation + 300]
    offset = build_matrices.index("viewmodel_offset_apply(")
    assert "\t\t\tpose_camera);" in build_matrices[offset:offset + 300]


def test_the_settings():
    config = read("port/linux/src/port_config.c")
    readme = read("port/linux/README.md")
    sys.path.insert(0, str(ROOT / "tools"))
    import port_settings

    assert re.search(r'\{ "display\.lowered_reticle", _config_boolean, "false", "HALO_LOWERED_RETICLE"', config)
    assert re.search(r'\{ "display\.lowered_reticle_amount", _config_real, "10\.0", "HALO_LOWERED_RETICLE_AMOUNT"',
                     config)
    assert "| `display.lowered_reticle` | `false` | `HALO_LOWERED_RETICLE` |" in readme
    assert "| `display.lowered_reticle_amount` | `10.0` | `HALO_LOWERED_RETICLE_AMOUNT` |" in readme
    rows = {row[1]: (page, row) for page in port_settings.PAGES.values() for row in page["rows"]
            if row[0] != port_settings.SECTION}
    page, row = rows["display.lowered_reticle"]
    assert row[2] == port_settings.ON_OFF and page["title"] == "FIRST PERSON"
    page, row = rows["display.lowered_reticle_amount"]
    assert row[2] == port_settings.LOWERED_RETICLE_AMOUNTS and page["title"] == "FIRST PERSON"
    # the slider's range typed is the one the code holds it to
    assert port_settings.LOWERED_RETICLE_AMOUNTS.typed_most == 30 and port_settings.LOWERED_RETICLE_AMOUNTS.typed_least == 0
    assert "#define LOWERED_RETICLE_MAXIMUM_PERCENT 30.0" in read("port/linux/game/lowered_reticle.c")
