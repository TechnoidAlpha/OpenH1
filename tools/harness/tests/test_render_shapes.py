"""What is drawn from the simulation between the ticks besides an object's nodes (port/linux/game/render_interpolation.c).
A widget's shape: a cloth flag is stepped in the tick (flags.c's flags_tick; game_frame's widgets_update only counts
the frames it goes undrawn), which leaves its shape seen from its pole's marker, and a frame draws it between the last
two ticks' shapes hung from the marker as the frame draws it, turning with it; at 60 and 120 the open tick's shape
waits for the frames to reach it (an antenna's chain, the same way). And a point worked out from an object's state: a
unit's camera (a waypoint's distance and size, the motion tracker's centre) where the frame draws it, on foot (its
origin is the tick's) and seated (its seat's marker is the frame's already: not moved twice)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["frames_dont_step", "cloth_between_ticks", "open_tick_waits", "turns_with_pole", "gap", "camera_on_foot",
         "camera_seated"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the frames step the cloth again (as game_frame did): several pulls a tick
    "frames-step": (("\t\tflag->updates_since_last_render++;\n",
                     "\t\tflag->updates_since_last_render++;\n"
                     "\t\tflag_update(flag, flag_definition_get(flag->definition_index), delta);\n"),
                    "frames_dont_step"),
    # the cloth drawn as the last tick left it, a tick's step at a time
    "cloth-as-ticked": (("render_interpolation_shape(FLAG_TAG, flag_index, &marker, count, drawn_points)", "0"),
                        "cloth_between_ticks"),
    # ... the newest shape alone, not blended from the one before
    "no-blend": (("\t\tif (previous != NONE)\n", "\t\tif (0)\n"), "cloth_between_ticks"),
    # ... the open tick's shape, kept by the tick it is run in, drawn before the frames reach it
    "open-tick-early": (("\tlong tick = interpolation_tick + 1;", "\tlong tick = interpolation_tick;"),
                        "open_tick_waits"),
    # ... hung from where the marker is, not turned with it
    "not-turned": (("\t\tpoint_from_frame(&frame, &local, &drawn[point_index]);",
                    "\t\tdrawn[point_index].x = frame.position.x + local.x, "
                    "drawn[point_index].y = frame.position.y + local.y, "
                    "drawn[point_index].z = frame.position.z + local.z;"), "turns_with_pole"),
    # ... after a gap, blended from a shape ticks old
    "blend-over-gap": (("\tprevious = shape_snapshot(shape, interpolation_tick - 1, count);",
                        "\tprevious = latest == 0 ? 1 : 0;"), "gap"),
    # a unit's camera as units.c has it in a frame: seated, moved twice
    "camera-mixed": (("\tinterpolation_rendering = FALSE;\n\tunit_get_camera_position(unit_index, position);",
                      "\tunit_get_camera_position(unit_index, position);"), "camera_seated"),
    # ... on foot, left where the tick put it
    "camera-unmoved": (("\tinterpolation_rendering = rendering;\n\trender_interpolation_object_point(unit_index, position);",
                        "\tinterpolation_rendering = rendering;"), "camera_on_foot"),
}


def generated(fault=None):
    code = read("port/linux/game/render_interpolation.c")
    flags = read("source/objects/widgets/flags.c")

    decls = "".join(line + "\n" for line in re.findall(
        r"^#define (?:MAXIMUM_INTERPOLATED_SHAPES|MAXIMUM_SHAPE_POINTS|SHAPE_SNAPSHOTS) .*$", code, re.M))
    decls += structure(code, "interpolated_shape") + "\n"
    decls += enum_with(flags, "MAXIMUM_FLAG_VERTICES") + "\n"
    decls += "#define FLAG_TAG 0x666C6167\n"
    for name in ("flag_datum_prefix", "flag_attachment_point", "flag_definition"):
        decls += structure(flags, name) + "\n"
    decls += "void render_interpolation_shape_forget(unsigned long owner, long index);\n"
    decls += ("static boolean flag_attachment_marker(struct flag_datum_prefix *flag, struct flag_definition *definition, "
              "real_matrix4x3 *matrix);\n")
    decls += ("static void flag_record_shape(long flag_index, struct flag_datum_prefix *flag, "
              "struct flag_definition *definition);\n")

    text = ""
    for name in ("lerp", "point_lerp", "vector_length", "vector_in_frame", "vector_from_frame", "point_in_frame",
                 "point_from_frame", "render_interpolation_object_origin", "render_interpolation_unit_camera_position",
                 "render_interpolation_shape_find", "shape_frame", "render_interpolation_shape_tick",
                 "render_interpolation_shape_forget", "shape_snapshot", "render_interpolation_shape"):
        text += function(code, name) + "\n"
    for name in ("flags_update", "flags_tick", "flag_render", "flag_attachment_marker", "flag_record_shape"):
        text += function(flags, name) + "\n"
    if fault:
        text = mutated(text, *fault)
    return (("decls.inc", decls), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("render_shapes", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("render_shapes", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_stepped_in_the_tick():
    # the tick steps the cloth (and the antennas), the frames only count
    game = read("source/game/game.c")
    assert game.index("antennas_update(seconds_per_tick);") < game.index("flags_tick(seconds_per_tick);")
    assert "flags_tick" not in function(game, "game_frame")
    # the antennas' shapes are kept and drawn the same way
    antenna = read("source/objects/widgets/antenna.c")
    assert "render_interpolation_shape_tick(ANTENNA_TAG" in antenna
    assert "render_interpolation_shape(ANTENNA_TAG" in antenna


def test_drawn_points():
    # the waypoints' distance and size, the motion tracker's centre and the sniper's numbers measure from the
    # camera as drawn; a game engine flag's waypoint is drawn over its object as drawn
    nav = read("source/interface/hud_nav_points.c")
    draw = function(nav, "custom_render_nav_point")
    assert "render_interpolation_unit_camera_position(" in draw and "unit_get_camera_position(" not in draw
    assert "game_engine_get_goal_position(" not in function(nav, "hud_render_nav_points")
    sensor = read("source/interface/motion_sensor.c")
    assert "render_interpolation_unit_camera_position(unit_index, &drawn_camera_position);" in sensor
    assert "render_interpolation_object_point(unit_index, &drawn_camera_position)" not in sensor
    weapon = read("source/interface/hud_weapon.c")
    assert "render_interpolation_unit_camera_position(unit_index, &position);" in weapon
