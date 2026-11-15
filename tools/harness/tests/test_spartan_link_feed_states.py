"""A Spartan link feed's first-person weapon (first_person_weapons.c's feeds' slots): each linked teammate's weapon
and arms in a slot of their own, driven by the messages of their weapon and unit as a local player's slot is (a shot
and its kick, a melee, a reload, a throw, a weapon put away and readied, overheating, a charge) and by their unit as
this machine has it each tick (the weapon in their hand). Drawing only: a feed's slot starts none of the weapon's
effects, unzooms nobody, makes no sound and takes no seed."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["linked", "fire", "melee", "reload", "grenade", "switch", "switch_unsaid", "overheat", "unlinked", "split"]
CONTROLS = [
    # the weapon's messages not given to the feeds: a teammate's shot shows nothing
    ("\tfirst_person_weapon_feeds_message(NONE, weapon_index, message_type);\n", "", "fire"),
    # the unit's not given: no melee
    ("\tfirst_person_weapon_feeds_message(unit_index, NONE, message_type);\n", "", "melee"),
    # a unit's message given to every feed: the other teammate melees too
    ("first_person_weapon->unit_index!=unit_index :", "FALSE :", "melee"),
    # a weapon's message found by the weapon the slot holds, not the one in the hand: the weapon readied is missed
    ("unit->unit.weapon_object_indices[unit->unit.current_weapon_index]!=weapon_index))",
     "first_person_weapon->weapon_index!=weapon_index))", "switch"),
    # a feed's reload unzooms its teammate on this machine
    ("if (!FIRST_PERSON_WEAPON_FEED_SLOT(local_player_index))\n\t\t\t\t{\n\t\t\t\t\tplayer_control_unzoom",
     "if (TRUE)\n\t\t\t\t{\n\t\t\t\t\tplayer_control_unzoom", "reload"),
    # a feed's slot moves the weapon's effects to itself
    ("(FIRST_PERSON_WEAPON_FEED_SLOT(local_player_index) || dead_spectate_eyes_unit(local_player_index)!=NONE))",
     "(dead_spectate_eyes_unit(local_player_index)!=NONE))", "switch"),
    # a weapon put in the hand with no message of it not taken
    ("\t\t\t\tfirst_person_weapon_message(slot, _first_person_weapon_message_ready);\n", "", "switch_unsaid"),
    # a slot kept after its feed went
    ("if (!linked || !object_try_and_get_and_verify_type(first_person_weapon->unit_index, _object_mask_unit))",
     "if ((void)linked, FALSE)", "unlinked"),
    # the feeds' slots the local players' (a feed's state is a local player's)
    ("return &first_person_weapon_feeds[local_player_index-MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];",
     "return &first_person_weapons[local_player_index%MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];", "split"),
]

FUNCTIONS = [
    "first_person_weapon_message_from_unit", "first_person_weapon_message_from_weapon", "first_person_weapon_message",
    "first_person_weapon_set_state", "first_person_weapon_next_state", "first_person_weapon_state_from_weapon_message",
    "first_person_animation_type_from_weapon_state", "first_person_weapon_switch_weapons",
    "first_person_weapon_new_unit", "first_person_weapon_forget_weapon", "first_person_weapon_set_visibility",
    "first_person_weapon_get", "first_person_weapon_index_from_weapon_index",
    "first_person_weapon_index_from_unit_index", "first_person_weapon_feed_from_unit_index",
    "first_person_weapon_feeds_message", "first_person_weapon_feeds_update",
]


def generated(fault=None):
    code = read("source/interface/first_person_weapons.c")
    header = read("source/interface/first_person_weapons.h")
    weapons = read("source/items/weapons.h")
    config = "\n".join([
        enum_with(weapons, "_first_person_weapon_message_primary_fire"),
        enum_with(weapons, "_weapon_type_shotgun"),
        enum_with(weapons, "_shotgun_reload_type_first_round"),
        enum_with(weapons, "_first_person_weapon_animation_idle"),
        enum_with(read("source/items/weapon_datum_flags.h"), "_weapon_overheated_bit"),
        enum_with(header, "FIRST_PERSON_WEAPON_FEEDS"),
        enum_with(code, "NUMBER_OF_FIRST_PERSON_WEAPON_OVERLAY_FRAMES"),
        enum_with(code, "_first_person_weapon_state_idle"),
        re.search(r"^#define FIRST_PERSON_WEAPON_FIRING_PUSH_BACK_VELOCITY\b.*$", code, re.M).group(0),
        re.search(r"^#define FIRST_PERSON_WEAPON_FEED_SLOT\(.*$", code, re.M).group(0),
        structure(code, "animation_state_continuous"),
        structure(code, "animation_graph_first_person_weapon_animations"),
        structure(code, "first_person_weapon"),
        structure(code, "first_person_weapon_sways"),
        re.search(r"^static char const \*const first_person_weapon_state_names\[.*?\};", code, re.M | re.S).group(0),
    ]) + "\n"
    text = "\n".join(function(code, name) for name in FUNCTIONS)
    if fault:
        text = mutated(text, fault[0], fault[1])
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("spartan_link_feed_states", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("before,after,case", CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build("spartan_link_feed_states", generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_drawing_only():
    code = read("source/interface/first_person_weapons.c")
    # a feed's idle poses' times from its own generator, not the game's local seed
    assert re.search(r"FIRST_PERSON_WEAPON_FEED_SLOT\(local_player_index\) \?\s*"
                     r"real_seed_random\(&first_person_weapon_feed_seed\) :\s*real_local_random\(\)",
                     function(code, "first_person_weapon_random"))
    update = function(code, "first_person_weapon_update")
    assert "real_local_random" not in update
    # no sound of a feed's animations' key frames
    assert re.search(r"if \(sound_definition_index!=NONE &&\s*!FIRST_PERSON_WEAPON_FEED_SLOT\(local_player_index\) &&",
                     update)
    # the feeds' own functions call nothing that makes an effect or a sound, or changes the game
    feeds = "\n".join(function(code, name) for name in (
        "first_person_weapon_feeds_update", "first_person_weapon_feeds_message",
        "first_person_weapon_build_feed_node_matrices", "first_person_weapon_feed_from_unit_index"))
    feeds = re.sub(r"/\*.*?\*/", "", feeds, flags=re.S)
    called = set(re.findall(r"\b([a-z_][a-z0-9_]*)\s*\(", feeds))
    touching = [name for name in called if re.search(r"random|seed|effect|sound|unzoom|_new$|damage|delete", name)]
    assert not touching, touching
    # a feed is drawn from its own slot, at its frame's time
    assert "first_person_weapon_draw_posed" not in code
    build_feed = function(code, "first_person_weapon_build_feed_node_matrices")
    assert "render_interpolation_first_person_feed_phase()" in build_feed
