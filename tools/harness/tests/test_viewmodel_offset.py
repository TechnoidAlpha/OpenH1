"""display.viewmodel_offset_x, _y and _z (view_fov.c's viewmodel_offset_apply): the first-person weapon's and hands'
node matrices moved along the view's right, up and forward, in centimeters, every node alike, so the weapon goes right,
up or away on the screen at any field of view; none by default, held at 20 centimeters. Applied once the pose is
interpolated, with the markers' matrices, but not in the VR mode; and the settings' rows, sliders and defaults."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, inline, mutated, read, run, structure  # noqa: E402

CASES = ["stock", "right", "up_and_forward", "turned_view", "on_the_screen", "held_at_the_range", "changed",
         "slider_steps"]
CONTROLS = [
    # the world's axes, not the view's
    ("\tcross_product3d(&camera->forward, &camera->up, &right);",
     "\tright.i = 0.0f; right.j = -1.0f; right.k = 0.0f;", "turned_view"),
    # left for right
    ("\tcross_product3d(&camera->forward, &camera->up, &right);",
     "\tcross_product3d(&camera->up, &camera->forward, &right);", "right"),
    # up and forward swapped
    ("camera->up.k * offset.j + camera->forward.k * offset.k", "camera->up.k * offset.k + camera->forward.k * offset.j",
     "up_and_forward"),
    # the root node alone: the hands and the gun part
    ("for (node_index = 0; node_index < node_count; node_index++)", "for (node_index = 0; node_index < 1; node_index++)",
     "right"),
    # inches, not centimeters
    ("#define CENTIMETERS_PER_WORLD_UNIT 304.8", "#define CENTIMETERS_PER_WORLD_UNIT 120.0", "right"),
    # not held at the range
    ("\tif (centimeters > VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS)\n", "\tif (FALSE)\n", "held_at_the_range"),
    # read once, not on a change
    ("if (!initialized || read_at != changes)\n\t{\n\t\tglobal.i", "if (!initialized || !read_at)\n\t{\n\t\tglobal.i", "changed"),
    # a track's step rounded toward 0 below it, as before
    ("steps < 0.0 ? steps - 0.5 : steps + 0.5", "steps + 0.5", "slider_steps"),
]


def generated(fault=None, slider_fault=None):
    code = read("port/linux/game/view_fov.c")
    section = code[code.index("/* ---------- where the first-person weapon is drawn */"):
                   code.index("/* ---------- whether the first-person weapon is drawn */")]
    slider = function(read("port/linux/game/menu_functions.c"), "settings_slider_round")
    if fault:
        if fault[0] in section:
            section = mutated(section, *fault)
        else:
            slider = mutated(slider, *fault)
    math = read("source/math/real_math.h")
    types = structure(math, "real_matrix4x3") + "\n" + inline(math, "cross_product3d") + "\n"
    return (("types.inc", types), ("under_test.inc", section + "\n" + slider + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("viewmodel_offset", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("viewmodel_offset", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_applied_with_the_pose():
    # in first_person_weapon_build_node_matrices, after the interpolation (which keeps the pose relative to the camera:
    # the offset is not stored and added again) and on the matrices the markers and both models are read from; not in
    # the VR mode's
    weapons = read("source/interface/first_person_weapons.c")
    build_matrices = function(weapons, "first_person_weapon_build_node_matrices")
    interpolation = build_matrices.index("render_interpolation_first_person(")
    applied = build_matrices.index("viewmodel_offset_apply(")
    assert interpolation < applied < build_matrices.index("vr_hands_pose(")
    assert build_matrices.rindex("#ifndef HALO_VR", 0, applied) > interpolation
    assert "first_person_weapon->node_matrices," in build_matrices[applied:applied + 200]
    assert "&render.camera);" in build_matrices[applied:applied + 300]
    assert weapons.count("viewmodel_offset_apply(") == 1
    # the markers: from the same matrices
    markers = function(weapons, "first_person_weapon_get_marker_by_name")
    assert "first_person_weapon->node_matrices" in markers


def test_the_settings():
    config = read("port/linux/src/port_config.c")
    readme = read("port/linux/README.md")
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import port_settings

    rows = {row[1]: row for page in port_settings.PAGES.values() for row in page["rows"]
            if row[0] != port_settings.SECTION}
    for axis in "xyz":
        name = f"display.viewmodel_offset_{axis}"
        assert re.search(rf'\{{ "{re.escape(name)}", _config_real, "0\.0", "HALO_VIEWMODEL_OFFSET_{axis.upper()}"',
                         config), name
        assert f"| `{name}` | `0.0` | `HALO_VIEWMODEL_OFFSET_{axis.upper()}` |" in readme, name
        assert rows[name][2] == port_settings.VIEWMODEL_OFFSETS, name
    # the sliders go half the range typed, which is view_fov.c's
    assert port_settings.VIEWMODEL_OFFSETS.typed_most == 20 and port_settings.VIEWMODEL_OFFSETS.typed_least == -20
    assert "#define VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS 20.0" in read("port/linux/game/view_fov.c")
