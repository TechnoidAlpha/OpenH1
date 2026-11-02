"""display.fov in a vehicle's seat (view_fov.c's render_fov_vertical, render_fov_reticle_scale): a seat's view, first
or third person, and the moments of entering and leaving it are widened as on foot, so the reticle is scaled as on
foot and keeps its size as the player gets in and out; death, the director's neutral view, a scripted camera and a
scope's full zoom keep the game's."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["turret", "third_person_seat", "leaving", "scoped_in_seat", "dead", "neutral", "scripted"]
CONTROLS = [
    # the game's own view in a vehicle again (the reticle at its stock size in the turret)
    ("if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))",
     "if (!unit || unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))",
     "turret"),
    # the first person alone: the following camera's seats and leaving them at the game's view
    ("perspective != _director_perspective_third_person", "TRUE", "third_person_seat"),
    ("perspective != _director_perspective_third_person", "TRUE", "leaving"),
    # the director's neutral view widened
    ("perspective != _director_perspective_first_person &&", "perspective == _director_perspective_scripted &&",
     "neutral"),
]


def generated(fault=None):
    code = read("port/linux/game/view_fov.c")
    world = code[code.index("static real reticle_scales[MAXIMUM_LOCAL_PLAYERS];"):
                 code.index("/* ---------- the mouse's turn zoomed")]
    if fault:
        world = mutated(world, *fault)
    config = "\n".join([
        enum_with(read("source/camera/director.h"), "_director_perspective_first_person"),
        enum_with(read("source/objects/objects.h"), "_object_dead_bit"),
    ]) + "\n"
    text = "\n".join([
        function(read("source/render/render_cameras.c"), "render_camera_get_adjusted_field_of_view_tangent"),
        world,
    ])
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("vehicle_fov", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("vehicle_fov", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_reticle_takes_it():
    # the HUD scales a centred reticle by it, and the passenger view's offset (a place in the drawn view) is given at
    # the HUD's own size, so that scaling it lands it there
    hud = read("source/interface/hud_draw.c")
    assert "render_fov_reticle_scale(render.local_player_index) : 1.0f);" in hud
    passenger = read("port/linux/game/passenger_camera.c")
    assert "reticle_scale = render_fov_reticle_scale(local_player_index);" in passenger
    assert passenger.count("/\n\t\treticle_scale;") == 2
