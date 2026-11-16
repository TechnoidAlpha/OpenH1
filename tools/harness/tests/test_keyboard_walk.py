"""The keys' Walk (controls.walk, input.walk, input.walk_speed): while it is held (or toggled on) the movement keys
push the player part of the way, as a stick pushed a little does (input_abstraction.c's keyboard_controls_apply); by
default as fast as stays off the enemies' motion trackers (keyboard_walk.c's keyboard_walk_throttle: the threshold's
velocity over the fastest run, a slope's scale and the game type's speed with it), checked through the biped's own
movement (bipeds.c's biped_player_movement, the speeds biped_physics_prepare works out) and the trackers' own test
(motion_sensor.c's should_draw_object), in every direction, at each simulation rate's sub-steps; crouched, the slower
of the two; on the 127 steps a network game sends a throttle in; not in a seat."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["tracker_30", "tracker_60", "tracker_120", "full_speed", "crouch", "toggle", "hold", "steps", "percent",
         "seat", "faster_game", "rates"]
WALK = "port/linux/game/keyboard_walk.c"
INPUT = "source/input/input_abstraction.c"
# (source, before, after, the case that must fail)
CONTROLS = [
    # the threshold taken as a velocity, not the square of one: far faster than it allows
    (WALK, "square_root(velocity_sensitivity) * TICKS_PER_SECOND", "velocity_sensitivity * 100.f * TICKS_PER_SECOND",
     "tracker_30"),
    # a tick's velocity taken as a second's: far slower than it need be
    (WALK, "square_root(velocity_sensitivity) * TICKS_PER_SECOND / fastest_speed",
     "square_root(velocity_sensitivity) / fastest_speed", "tracker_60"),
    # no margin: on the threshold down the steepest slope's last step
    (WALK, "#define WALK_TRACKER_MARGIN 0.95f", "#define WALK_TRACKER_MARGIN 1.05f", "tracker_120"),
    # the slope's scale left out: downhill shows
    (WALK, "slope = MAX(1.f, MAX(definition->biped.downhill_velocity_scale, definition->biped.uphill_velocity_scale));",
     "slope = 1.f + 0.f * definition->biped.uphill_velocity_scale;", "tracker_30"),
    # the slowest run taken for the fastest: forward shows
    (WALK, "fastest = MAX(information->run_forward_speed,\n\t\tMAX(information->run_backward_speed, "
     "information->run_sideways_speed));", "fastest = information->run_backward_speed;", "tracker_30"),
    # the game type's speed left out
    (WALK, "\tif (player->speed_multiplier > 1.f)\n\t\tfastest *= player->speed_multiplier;\n", "", "faster_game"),
    # in a seat too
    (WALK, "if (!unit || unit->object.parent_object_index != NONE ||", "if (!unit ||", "seat"),
    # the percentage not read
    (WALK, "if (percentage > 0.f)\n\t\treturn", "if (percentage < 0.f)\n\t\treturn", "percent"),
    # the walk let go of crouched
    (INPUT, "\t\t\tpush = MIN(push, 0.97f);", "\t\t\tpush = 0.97f;", "crouch"),
    # walking whatever the key
    (INPUT, "real push = walking ? keyboard_walk_throttle((short)controller_index) : 1.f;",
     "real push = keyboard_walk_throttle((short)controller_index);", "full_speed"),
    # a crouch's push put on the steps too (it is not a walk's)
    (INPUT, "if (walking && push < 1.f)", "if (push < 1.f)", "full_speed"),
    # rounded to the nearest step: above the push
    (INPUT, "return value >= 0.f ? (real)floor(value * 127.f) / 127.f",
     "return value >= 0.f ? (real)floor(value * 127.f + 0.9f) / 127.f", "steps"),
    # toggle read as hold
    (INPUT, "return walk_settings.toggle ? walk_toggles[controller_index].on : TEST_FLAG(held, HALO_KEYBOARD_WALK);",
     "return TEST_FLAG(held, HALO_KEYBOARD_WALK);", "toggle"),
    # a toggle that stays on after the setting goes back to hold
    (INPUT, "\tif (!walk_settings.toggle)\n\t\twalk_toggles[controller_index].on = FALSE;\n\telse if",
     "\tif (!walk_settings.toggle)\n\t\t;\n\telse if", "toggle"),
]


def speeds_block(code):
    """biped_physics_prepare's speeds of a throttle back and forward: its loop, by matching braces"""
    start = code.index("\t\t\t\tfor (direction = 0; direction < 2; direction++)")
    end = code.index("{", start) + 1
    depth = 1
    while depth:
        depth += (code[end] == "{") - (code[end] == "}")
        end += 1
    return code[start:end]


def generated(fault=None):
    walk = read(WALK)
    source = read(INPUT)
    bipeds = read("source/units/bipeds.c")
    units = read("source/units/units.h")
    if fault:
        path, before, after = fault
        if path == WALK:
            walk = mutated(walk, before, after)
        else:
            source = mutated(source, before, after)
    config = "\n".join([
        enum_with(read("port/linux/include/halo_keyboard.h"), "HALO_KEYBOARD_WALK"),
        enum_with(source, "_game_control_crouch"),
        enum_with(units, "_unit_active_camouflaged_bit"),
        enum_with(units, "_unit_control_weapon_primary_trigger_bit"),
        enum_with(units, "_unit_grenade_throw_idle"),
        enum_with(units, "_unit_base_seat_alert"),
    ]) + "\n"
    structures = "\n".join([
        structure(read("source/game/game_globals.h"), "game_globals_player_information"),
        structure(bipeds, "biped_player_speeds"),
    ]) + "\n"
    defines = "\n".join(re.findall(r"^#define WALK_\w+ .*$", walk, re.M))
    text = "\n".join([
        defines,
        function(walk, "keyboard_walk_tracker_throttle"),
        function(walk, "walk_speed_setting"),
        function(walk, "keyboard_walk_throttle"),
        function(source, "crouch_walk"),
        "static struct\n{\n\tboolean on;\n\tboolean down;\n} walk_toggles[MAXIMUM_GAMEPADS];\n",
        "static struct\n{\n\tunsigned long read_at;\n\tboolean toggle;\n} walk_settings = { (unsigned long)-1, FALSE };\n",
        function(source, "walk_settings_read"),
        function(source, "walk_toggle_update"),
        function(source, "walk_held"),
        function(source, "walk_throttle_step"),
        function(source, "keyboard_controls_apply"),
        function(bipeds, "biped_player_movement"),
        "static void biped_speeds(struct game_globals_player_information *player_information,\n"
        "\tstruct unit_datum *biped, real crouch, struct biped_player_speeds *speeds)\n{\n"
        "\treal uncrouch = 1.f - crouch;\n\treal forward_speed;\n\treal sideways_speed;\n\tshort direction;\n\n"
        + speeds_block(bipeds) + "\n}\n",
        function(read("source/interface/motion_sensor.c"), "should_draw_object"),
    ])
    return (("config.inc", config), ("structures.inc", structures), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("keyboard_walk", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("path,before,after,case", CONTROLS)
def test_negative_control(path, before, after, case):
    status, output = run(build("keyboard_walk", generated((path, before, after))), case)
    assert status == CHECK_FAILED, output
