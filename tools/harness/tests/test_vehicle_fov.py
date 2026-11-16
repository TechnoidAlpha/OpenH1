"""display.vehicle_fov (view_fov.c's render_fov_vertical, render_fov_reticle_scale): a third-person seat's view (a
driver's, a gunner's, display.passenger_third_person's passenger) and the moments of entering and leaving it take the
vehicle's field of view ("default": the game's own, its reticle at the stock size; "match": display.fov's; a number:
its angle); on foot and in a first-person seat (a passenger's, the Shade's turret: #775) display.fov's. Getting in and
out, and from one kind of seat to the other, the view eases between them, and each frame's reticle is scaled from that
frame's view (#441); each local player has their own; death, the director's neutral view, a scripted camera and a
scope's full zoom keep the game's."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, player_settings_slots, read, run  # noqa: E402

CASES = ["seat_default", "seat_match", "seat_number", "third_person_seat", "first_person_seat", "turret_exit",
         "passenger_climbing_in", "passenger_camera", "seat_change", "split_screen", "players",
         "leaving_third_person", "match_exit", "entering", "turned_back", "respawn", "scoped_in_seat", "dead",
         "neutral", "scripted", "wider_with_panini", "past_the_widest", "zoomed_reticle_size", "unzooming"]
CONTROLS = [
    # display.vehicle_fov unread: the seat at display.fov's (#449's)
    ("\t\trequested_tangent = vehicle_tangent;", "\t\trequested_tangent = foot_tangent;", "seat_default"),
    ("\t\trequested_tangent = vehicle_tangent;", "\t\trequested_tangent = foot_tangent;", "seat_number"),
    # "match" read as "default"
    ("\t\t\tplayers[slot].vehicle_tangent = players[slot].foot_tangent;",
     "\t\t\tplayers[slot].vehicle_tangent = 0.0f;", "seat_match"),
    # a number unread
    ("\t\t\tplayers[slot].vehicle_tangent = fov_tangent(local_player_index, degrees);",
     "\t\t\tplayers[slot].vehicle_tangent = 0.0f;", "seat_number"),
    # (#894) one field of view kept for every local player: player 2's own unseen
    ("short slot = PLAYER_SETTINGS_SLOT(local_player_index);\n\tunsigned long changes = config_changes();\n\n"
     "\tif (!players[slot].initialized || players[slot].read_at != changes)\n\t{\n\t\tchar const *vehicle",
     "short slot = 0;\n\tunsigned long changes = config_changes();\n\n"
     "\tif (!players[slot].initialized || players[slot].read_at != changes)\n\t{\n\t\tchar const *vehicle",
     "players"),
    # every player's at config.toml's (player 1's profile's for all would be the same fault)
    ('double foot = config_player_real(local_player_index, "display.fov");',
     'double foot = config_player_real(NONE, "display.fov");', "players"),
    # (#775) every seat display.vehicle_fov's, a passenger's seen from the head too (as before)
    ("\tif (unit->object.parent_object_index == NONE)\n\t\treturn FALSE;\n",
     "\tif (unit->object.parent_object_index == NONE)\n\t\treturn FALSE;\n\treturn TRUE;\n", "first_person_seat"),
    # a passenger's getting in, seen in third person, taken for a third-person seat: the view dips to the vehicle's
    ("\treturn TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat)->flags,\n"
     "\t\t_unit_seat_third_person_camera_bit) ? TRUE : FALSE;",
     "\treturn TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat)->flags,\n"
     "\t\t_unit_seat_third_person_camera_bit) || director_get_perspective(local_player_index) == "
     "_director_perspective_third_person ? TRUE : FALSE;", "passenger_climbing_in"),
    # display.passenger_third_person's passenger taken for one seen from the head
    ("\tif (passenger_camera_enabled(local_player_index))\n\t\treturn TRUE;\n", "", "passenger_camera"),
    # one way for every local player: a split screen's views take each other's
    ("real weight = vehicle_fov_weight(local_player_index, unit);",
     "real weight = vehicle_fov_weight(0, unit);", "split_screen"),
    # no way between them: the view and its reticle jump as the player gets out or in, or changes seats
    ("real eased = progress * progress * (3.0f - 2.0f * progress);", "real eased = 1.0f;", "leaving_third_person"),
    ("real eased = progress * progress * (3.0f - 2.0f * progress);", "real eased = 1.0f;", "entering"),
    ("real eased = progress * progress * (3.0f - 2.0f * progress);", "real eased = 1.0f;", "seat_change"),
    # turning back from the way's end, not from where it is
    ("vehicle_blends[local_player_index].from = vehicle_blends[local_player_index].weight;\n\t\tvehicle_blends[local_player_index].start = now;\n\t}",
     "vehicle_blends[local_player_index].from = third_person ? 0.0f : 1.0f;\n\t\tvehicle_blends[local_player_index].start = now;\n\t}",
     "turned_back"),
    # the way kept across death: a respawned view eases from the seat's
    ("\t\tvehicle_blends[local_player_index].valid = FALSE;\n", "\n", "respawn"),
    # the first person alone: the following camera's seats at the game's view
    ("perspective != _director_perspective_third_person", "TRUE", "third_person_seat"),
    # above 150 without the projection
    ("strength > 0.0f || degrees <= FIELD_OF_VIEW_MOST", "TRUE", "wider_with_panini"),
    # not above 150 with it
    ("strength > 0.0f || degrees <= FIELD_OF_VIEW_MOST", "degrees <= FIELD_OF_VIEW_MOST", "wider_with_panini"),
    # no widest: any angle below 180
    ("degrees <= PANINI_FIELD_OF_VIEW_MOST))", "degrees < 180.0))", "past_the_widest"),
    # (#594) the reticle sized by the zoomed view: it grows as the scope zooms in
    ("\t\treticle_sizes[local_player_index] = reticle_size;", "\t\treticle_sizes[local_player_index] = scale;",
     "zoomed_reticle_size"),
    # display.panini's magnification asked of the zoomed view, not the unzoomed one
    ("\t\t\t\treticle_widenings[local_player_index] = widening;", "\t\t\t\treticle_widenings[local_player_index] = 1.0f;",
     "zoomed_reticle_size"),
    # its size the requested view's alone, not the stock view's against it
    ("*reticle_size = PIN(unzoomed_tangent / requested_tangent, 0.001f, 1000.0f);",
     "*reticle_size = PIN(1.0f / requested_tangent, 0.001f, 1000.0f);", "zoomed_reticle_size"),
    # left as the last frame's where the view is the game's own (death)
    ("\t*reticle_size = 1.0f;\n\t*unzoomed_drawn_tangent",
     "\t*reticle_size = reticle_sizes[local_player_index];\n\t*unzoomed_drawn_tangent", "dead"),
    # (#747) the zoom's easing out unseen: the reticle snaps back to its size as the scope is let go
    ("!scaled_scopes[local_player_index] && reticle_widenings[local_player_index] > 1.0001f;", "FALSE;",
     "unzooming"),
    # the unzoomed view taken for an easing one (the arctangent's hair over 1)
    ("reticle_widenings[local_player_index] > 1.0001f;", "reticle_widenings[local_player_index] > 0.0f;",
     "unzooming"),
    # the director's neutral view widened
    ("perspective != _director_perspective_first_person &&", "perspective == _director_perspective_scripted &&",
     "neutral"),
]


def generated(fault=None):
    code = read("port/linux/game/view_fov.c")
    world = code[code.index("/* ---------- the world's field of view */"):
                 code.index("/* ---------- the mouse's turn zoomed")]
    # (display.fov's widest, and above 150 with the projection alone: panini_projection.h)
    header = read("port/linux/include/panini_projection.h")
    widest = "\n".join(line for line in header.splitlines() if "FIELD_OF_VIEW_MOST " in line and
                       line.startswith("#define")) + "\n" + function(header, "panini_field_of_view").replace("static __inline", "static", 1)
    world = widest + "\n" + world
    if fault:
        world = mutated(world, *fault)
    config = "\n".join([
        enum_with(read("source/camera/director.h"), "_director_perspective_first_person"),
        enum_with(read("source/objects/objects.h"), "_object_dead_bit"),
        enum_with(read("source/units/unit_definitions.h"), "_unit_seat_third_person_camera_bit"),
    ]) + "\n"
    text = "\n".join([
        function(read("source/render/render_cameras.c"), "render_camera_get_adjusted_field_of_view_tangent"),
        # (the zoom's arithmetic, display.scale_scoped_fov's: the game's own header)
        read("port/linux/include/zoom_fov.h"),
        world,
    ])
    return (("config.inc", config), ("under_test.inc", player_settings_slots() + text))


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
    # the HUD's own size, so that scaling it lands it there, at any vehicle field of view
    hud = read("source/interface/hud_draw.c")
    # (display.panini's magnification of the middle too: test_panini.py; the weapon's reticle at its size, which a
    # zoom keeps, a scope's own pieces with the view, or with its picture under display.scale_scoped_fov: #594,
    # #731)
    assert "\t\treticle_scale = is_crosshair_bitmap == HUD_CROSSHAIR_SCOPE_PIECE ?\n" \
        "\t\t\trender_fov_scope_piece_scale(render.local_player_index) * panini_center_scale() :\n" \
        "\t\t\trender_fov_reticle_size(render.local_player_index);" in hud
    assert "\t\tcolor,\n\t\treticle_scale);" in hud
    # the passenger's and the lowered reticle's offsets are given at the scale the reticle is drawn at
    assert "!passenger_camera_reticle_offset(&point, reticle_scale, &reticle_rest))\n" \
        "\t\tlowered_reticle_offset_point(&point, reticle_scale);" in hud
    passenger = read("port/linux/game/passenger_camera.c")
    assert "real x = point->x + reticle_offset.x / reticle_scale;" in passenger
    assert "real y = point->y + reticle_offset.y / reticle_scale;" in passenger
    # (and what is past the whole units, at the scale it is drawn at: hud_draw.c draws the reticle moved by it)
    assert "rest->i = (x - point->x) * reticle_scale;" in passenger
    assert "rasterizer_screen_offset_set(reticle_rest.i, reticle_rest.j);" in hud
    # a scope's pieces are the items drawn only zoomed (the VR smart-link's zoomed layout's), and, zoomed, the
    # reticle too, which the tag draws with them (the pistol's circle under its range and diamond: #738)
    weapon = read("source/interface/hud_weapon.c")
    assert "(crosshair->states[_crosshair_state_zoom].value.reference_data > 0 ||\n" in weapon
    # (and, let go, while the view eases back out: #747)
    assert "render_fov_zoom_easing(render.local_player_index)) &&\n" in weapon
    # (with the Xbox's zoom; display.scale_scoped_fov's keeps the unzoomed size, #594)
    assert "render_fov_zoomed_reticle_with_scope(render.local_player_index) ?\n" in weapon
    assert "\treturn !(local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&\n" \
        "\t\tscaled_scopes[local_player_index]);" in read("port/linux/game/view_fov.c")
    assert "HUD_CROSSHAIR_SCOPE_PIECE : TRUE);" in weapon
