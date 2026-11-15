"""display.viewmodel_offset_x, _y and _z (view_fov.c's viewmodel_offset_apply): the first-person weapon's and hands'
node matrices moved along the view's right, up and forward, in centimeters, every node alike, so the weapon goes right,
up or away on the screen at any field of view; none by default, held at 20 centimeters. With display.viewmodel_fov,
the moves right and up are scaled by the tangents' ratio of the weapon's drawn angle to its stock one, so a value goes as
far across the screen at any weapon field of view (#509); the move away is not scaled. Applied once the pose is
interpolated, with the markers' matrices, but not in the VR mode; and the settings' rows, sliders and defaults."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, inline, mutated, player_settings_slots, read, run, structure  # noqa: E402

CASES = ["stock", "right", "up_and_forward", "forward_recorded", "turned_view", "on_the_screen", "weapon_fov_across", "weapon_fov_depth",
         "weapon_fov_unusable", "held_at_the_range", "changed", "turn_pitch", "turn_yaw", "turn_roll", "turn_and_move",
         "weapon_own", "weapon_own_changed", "slider_steps", "players", "weapon_players"]
CONTROLS = [
    # the move toward the eye not recorded for the weapon's near plane, or kept once gone
    ("\t\tapplied_forwards[local_player_index] = offset.forward;", "", "forward_recorded"),
    ("\t\tapplied_forwards[local_player_index] = 0.0f;", "", "forward_recorded"),
    # the world's axes, not the view's
    ("\tcross_product3d(&camera->forward, &camera->up, &right);",
     "\tright.i = 0.0f; right.j = -1.0f; right.k = 0.0f;", "turned_view"),
    # left for right
    ("\tcross_product3d(&camera->forward, &camera->up, &right);",
     "\tcross_product3d(&camera->up, &camera->forward, &right);", "right"),
    # up and forward swapped
    ("camera->up.k * offset.up + camera->forward.k * offset.forward",
     "camera->up.k * offset.forward + camera->forward.k * offset.up",
     "up_and_forward"),
    # the root node alone: the hands and the gun part
    ("for (node_index = 0; node_index < node_count; node_index++)\n\t{\n\t\tnode_matrices[node_index].position.x += move.i;",
     "for (node_index = 0; node_index < 1; node_index++)\n\t{\n\t\tnode_matrices[node_index].position.x += move.i;",
     "right"),
    # inches, not centimeters
    ("#define CENTIMETERS_PER_WORLD_UNIT 304.8", "#define CENTIMETERS_PER_WORLD_UNIT 120.0", "right"),
    # not held at the range
    ("\tif (value > maximum)\n", "\tif (FALSE)\n", "held_at_the_range"),
    # read once, not on a change
    ("if (!players[slot].initialized || players[slot].read_at != changes)\n\t{\n\t\tplayers[slot].global.right",
     "if (!players[slot].initialized || !players[slot].read_at)\n\t{\n\t\tplayers[slot].global.right", "changed"),
    # one offset kept for every player
    ("short slot = PLAYER_SETTINGS_SLOT(local_player_index);\n\tunsigned long changes = config_changes();\n\n\tif (!players[slot]",
     "short slot = 0;\n\tunsigned long changes = config_changes();\n\n\tif (!players[slot]", "players"),
    # config.toml's for every player
    ("\tviewmodel_offset_global(local_player_index, offset);", "\tviewmodel_offset_global(NONE, offset);", "players"),
    # the weapon field of view's scale left out: the move set, as far in the world, shorter on the screen
    ("\t\toffset.right *= scale;\n\t\toffset.up *= scale;\n", "", "weapon_fov_across"),
    # the scale the other way about (the stock angle's over the drawn one's)
    ("return tanf(drawn * 0.5f) / tanf(stock * 0.5f);", "return tanf(stock * 0.5f) / tanf(drawn * 0.5f);",
     "weapon_fov_across"),
    # the angles' ratio, not their tangents'
    ("return tanf(drawn * 0.5f) / tanf(stock * 0.5f);", "return drawn / stock;", "weapon_fov_across"),
    # the whole angle's tangents, not the half's
    ("return tanf(drawn * 0.5f) / tanf(stock * 0.5f);", "return tanf(drawn) / tanf(stock);", "weapon_fov_across"),
    # the depth scaled too (as #509 first proposed): the weapon pushed through the eye at a wide angle
    ("\t\toffset.up *= scale;\n", "\t\toffset.up *= scale;\n\t\toffset.forward *= scale;\n", "weapon_fov_depth"),
    # only the horizontal scaled
    ("\t\toffset.up *= scale;\n", "", "weapon_fov_across"),
    # another view's angles: the local player's not passed on
    ("if (viewmodel_angles(local_player_index, &drawn, &stock))", "if (viewmodel_angles(1, &drawn, &stock))",
     "weapon_fov_across"),
    # angles that are none taken
    ("\tif (!(drawn > 0.0f && drawn < _pi && stock > 0.0f && stock < _pi))\n\t\treturn 1.0f;\n", "",
     "weapon_fov_unusable"),
    # the turns held at the moves' 20, not 15 degrees
    ("viewmodel_offset_held(config_player_real(local_player_index, name),\n\t\tVIEWMODEL_ROTATION_MAXIMUM_DEGREES)",
     "viewmodel_offset_held(config_player_real(local_player_index, name),\n\t\tVIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS)",
     "held_at_the_range"),
    # turned about the eye, not the weapon's middle: it swings across the view
    ("\t\tpivot.x /= node_count;\n\t\tpivot.y /= node_count;\n\t\tpivot.z /= node_count;\n",
     "\t\tpivot = camera->position;\n", "turn_pitch"),
    # the places turned but not the nodes' axes: the gun's parts each pointing as before
    ("\t\t\tviewmodel_rotation_vector(&offset, &right, camera, &node->forward);\n", "", "turn_pitch"),
    # pitch the other way (and so not a turn)
    ("\ta = *y * cosine + *z * sine;", "\ta = *y * cosine - *z * sine;", "turn_pitch"),
    # yaw and roll the other way
    ("\tsine = sinf(offset->yaw);", "\tsine = -sinf(offset->yaw);", "turn_yaw"),
    ("\tsine = sinf(offset->roll);", "\tsine = -sinf(offset->roll);", "turn_roll"),
    # the weapon's own left out
    ("total[0] = offset->right * CENTIMETERS_PER_WORLD_UNIT + own[0];",
     "total[0] = offset->right * CENTIMETERS_PER_WORLD_UNIT;", "weapon_own"),
    ("total[4] = offset->yaw * (180.0 / _pi) + own[4];", "total[4] = offset->yaw * (180.0 / _pi);", "weapon_own"),
    # the sum not held at the range
    ("\t\ttotal[value] = viewmodel_offset_held(total[value],", "\t\ttotal[value] = 0 * viewmodel_offset_held(total[value],", "weapon_own"),
    # a change written not seen; another weapon given the last one's
    ("definition_index != cached[slot].definition || cached[slot].at != changes)",
     "definition_index != cached[slot].definition || (cached[slot].at != changes && 0))", "weapon_own_changed"),
    ("definition_index != cached[slot].definition || cached[slot].at != changes)",
     "(definition_index != cached[slot].definition && 0) || cached[slot].at != changes)", "weapon_own_changed"),
    # looked up every frame
    ("if (!cached[slot].valid || definition_index", "if (TRUE || definition_index", "weapon_own_changed"),
    # one weapon's offsets kept for every player, or config.toml's for all
    ("\tshort slot = PLAYER_SETTINGS_SLOT(local_player_index);\n\tdouble const *own",
     "\tshort slot = 0;\n\tdouble const *own", "weapon_players"),
    ("config_player_viewmodel_weapon(local_player_index, viewmodel_weapon_path(definition_index)",
     "config_player_viewmodel_weapon(0, viewmodel_weapon_path(definition_index)", "weapon_players"),
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
    pi = re.search(r"#define _pi .*", math).group(0)
    types = pi + "\n" + structure(math, "real_matrix4x3") + "\n" + inline(math, "cross_product3d") + "\n" + \
        inline(math, "dot_product3d") + "\n" + structure(read("port/linux/game/view_fov.h"), "viewmodel_offset") + "\n"
    return (("types.inc", types), ("under_test.inc", player_settings_slots() + section + "\n" + slider + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("viewmodel_offset", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("viewmodel_offset", generated((before, after))), case)
    assert status == CHECK_FAILED, output
