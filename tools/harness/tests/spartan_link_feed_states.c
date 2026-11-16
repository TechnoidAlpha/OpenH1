/*
SPARTAN_LINK_FEED_STATES.C

A Spartan link feed's first-person weapon (first_person_weapons.c's feeds'
slots): each teammate a feed shows gets a slot of its own beside the local
players', which the same messages of their weapon and unit drive as a local
player's slot (a shot, a melee, a reload, a throw, a weapon put away and
readied, overheating, a charge), and their unit as this machine has it each
tick (the weapon in their hand). The game's own functions run here against a
fake world of units and weapons; a slot's animations are not stepped (the
update is counted only), the end of one is first_person_weapon_next_state's.
Drawing only: a feed's slot starts no effect and unzooms nobody.
*/

#include "harness.h"
#include <math.h>

#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define MAXIMUM_WEAPONS_PER_UNIT 4
#define MAXIMUM_NODES_PER_ANIMATION 64
#define TICKS_PER_SECOND 30
#define VALID_INDEX(index, count) ((index)>=0 && (index)<(count))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CEILING(n, ceiling) ((n) > (ceiling) ? (ceiling) : (n))
#define PIN(n, floor, ceiling) ((n) < (floor) ? (floor) : CEILING((n), (ceiling)))
#define csmemset memset
#define csmemcpy memcpy
#define _object_mask_unit 1
#define _object_mask_weapon 2

typedef unsigned short word;
typedef struct { real i, j; } real_vector2d;
typedef struct { real yaw, pitch; } real_euler_angles2d;
typedef struct { real i, j, k, w; } real_quaternion;
typedef struct { real_quaternion rotation; real_point3d translation; real scale; } real_orientation;
struct real_matrix4x3 { real scale; real_vector3d forward, left, up; real_point3d position; };
struct animation_state { short index; short frame_index; };
struct tag_block { long count; void *address; };
struct tag_reference { long group; long index; };
struct animation { short frame_count; };
struct animation_graph_animation_index { short animation_index; };
struct animation_graph { struct tag_block first_person_weapon_animations; struct tag_block animations; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (((type *)(block)->address)+(index))
#define animation_graph_animation_index_get(block) ((struct animation_graph_animation_index *)(block)->address)

struct weapon_magazine_definition { short rounds_loaded_maximum; };
struct weapon_definition
{
	struct
	{
		short weapon_type;
		struct tag_block magazines;
		struct { struct tag_reference first_person_model, first_person_animations; } interface_definition;
	} weapon;
};
struct weapon_magazine { short state; short rounds_loaded; short rounds_total; };
struct weapon_datum { long definition_index; struct { unsigned long flags; struct weapon_magazine magazines[2]; } weapon; };
struct unit_datum
{
	struct
	{
		short current_weapon_index;
		long weapon_object_indices[MAXIMUM_WEAPONS_PER_UNIT];
		short current_zoom_level;
	} unit;
};
struct player_datum { long unit_index; };
struct game_globals_first_person_interface { struct tag_reference hands; };
struct game_globals { struct tag_block first_person_interface; };
struct camera_command { real_vector3d forward; };
struct first_person_weapon_blend { real start; short frame_count; real update_phase; real drawn_phase;
	long update_pose_weapon_index; };

#include "config.inc"

/* ---------- the fake world */

enum { UNITS = 6, WEAPONS = 8, UNIT_BASE = 0x100, WEAPON_BASE = 0x200 };

static struct
{
	boolean units_exist[UNITS];
	struct unit_datum units[UNITS];
	boolean weapons_exist[WEAPONS];
	struct weapon_datum weapons[WEAPONS];
	struct weapon_definition definitions[NUMBER_OF_WEAPON_TYPES];
	struct weapon_magazine_definition magazine;
	struct animation_graph graph;
	struct animation_graph_first_person_weapon_animations first_person_animations;
	struct animation_graph_animation_index animation_indices[NUMBER_OF_FIRST_PERSON_WEAPON_ANIMATIONS];
	struct animation animations[NUMBER_OF_FIRST_PERSON_WEAPON_ANIMATIONS];
	struct game_globals globals;
	struct game_globals_first_person_interface interface;
	struct player_datum players[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	long local_players[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	long linked[FIRST_PERSON_WEAPON_FEEDS];
	short linked_count;
	/* what the slots did that a feed's must not, by slot */
	int effects_started[FIRST_PERSON_WEAPON_SLOTS];
	int effects_stopped[FIRST_PERSON_WEAPON_SLOTS];
	int unzooms;
	int sounds;
	int updates[FIRST_PERSON_WEAPON_SLOTS];
} world;

static struct first_person_weapon local_slots[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static struct first_person_weapon *first_person_weapons = local_slots;
static struct first_person_weapon first_person_weapon_feeds[FIRST_PERSON_WEAPON_FEEDS];
static unsigned long first_person_weapon_feed_seed = 1;
static long first_person_weapon_sound_indices[FIRST_PERSON_WEAPON_SLOTS];
static struct first_person_weapon_sways first_person_weapon_sways_before[FIRST_PERSON_WEAPON_SLOTS];
static struct first_person_weapon_blend first_person_weapon_blends[FIRST_PERSON_WEAPON_SLOTS];

static void *object_try_and_get_and_verify_type(long index, int mask)
{
	if ((mask & _object_mask_unit) && index >= UNIT_BASE && index < UNIT_BASE + UNITS && world.units_exist[index - UNIT_BASE])
		return &world.units[index - UNIT_BASE];
	if ((mask & _object_mask_weapon) && index >= WEAPON_BASE && index < WEAPON_BASE + WEAPONS &&
		world.weapons_exist[index - WEAPON_BASE])
		return &world.weapons[index - WEAPON_BASE];
	return NULL;
}

static struct unit_datum *unit_get(long index)
{
	struct unit_datum *unit = object_try_and_get_and_verify_type(index, _object_mask_unit);

	CHECK(unit != NULL, "unit_get of a unit that is not there: %lx", (unsigned long)index);
	return unit;
}

static struct weapon_datum *weapon_get(long index)
{
	struct weapon_datum *weapon = object_try_and_get_and_verify_type(index, _object_mask_weapon);

	CHECK(weapon != NULL, "weapon_get of a weapon that is not there: %lx", (unsigned long)index);
	return weapon;
}

#define weapon_try_and_get(index) ((struct weapon_datum *)object_try_and_get_and_verify_type(index, _object_mask_weapon))
#define weapon_definition_get(index) (&world.definitions[index])
#define animation_graph_definition_get(index) (&world.graph)
#define scenario_get_game_globals() (&world.globals)
#define model_build_remapping_table_for_animation_graph(model, graph, table) TRUE

static long unit_inventory_get_weapon(long unit_index, short index)
{
	struct unit_datum *unit = unit_get(unit_index);

	return index >= 0 && index < MAXIMUM_WEAPONS_PER_UNIT ? unit->unit.weapon_object_indices[index] : NONE;
}

#define local_player_get_player_index(local_player_index) (world.local_players[local_player_index])
#define player_get(index) (&world.players[index])
#define dead_spectate_eyes_unit(local_player_index) ((long)NONE)
#define dead_spectate_first_person_weapon_unit(local_player_index, unit_index) (unit_index)

static void effects_start_on_first_person_weapon(short slot, long weapon_index)
{
	(void)weapon_index;
	world.effects_started[slot]++;
}

static void effects_stop_on_first_person_weapon(short slot)
{
	world.effects_stopped[slot]++;
}

#define particles_stop_on_first_person_weapon(slot) ((void)0)
#define player_control_unzoom(unit_index) ((void)(unit_index), world.unzooms++)
#define sound_stop_impulse(index) ((void)(index))
#define weapon_play_first_person_weapon_sound(weapon_index, message_type) \
	((void)(weapon_index), (void)(message_type), world.sounds++)

static short spartan_link_linked_units(long *unit_indices, short maximum_count)
{
	short index;

	for (index = 0; index < world.linked_count && index < maximum_count; index++)
		unit_indices[index] = world.linked[index];
	return index;
}

#define spartan_link_feed_state_log(unit_index, state, velocity) ((void)(unit_index), (void)(state), (void)(velocity))
#define first_person_camera_fake(unit_index, command) ((command)->forward.i = 1.0f, (command)->forward.j = 0.0f, \
	(command)->forward.k = 0.0f)
#define euler_angles2d_from_vector3d(angles, vector) ((angles)->yaw = (vector)->j, (angles)->pitch = (vector)->k)

/* (not under test: a slot's poses and animations' steps) */
static void first_person_weapon_start_interpolation(short slot, short frame_count)
{
	(void)slot;
	(void)frame_count;
}

static void first_person_weapon_predict(short slot)
{
	(void)slot;
}

static void first_person_weapon_blend_update_begin(short slot)
{
	(void)slot;
}

static void first_person_weapon_update(short slot)
{
	world.updates[slot]++;
}

static void first_person_weapon_set_visibility(short local_player_index, boolean visible);
static void first_person_weapon_set_state(short local_player_index, short new_state, boolean reset_sounds);
static void first_person_weapon_switch_weapons(short local_player_index);
static void first_person_weapon_forget_weapon(short local_player_index);
static void first_person_weapon_new_unit(short local_player_index, long unit_index);
static void first_person_weapon_message(short local_player_index, short message_type);
static void first_person_weapon_next_state(short local_player_index);
static long first_person_weapon_state_from_weapon_message(short message_type);
static long first_person_animation_type_from_weapon_state(short state);
static struct first_person_weapon *first_person_weapon_get(short local_player_index);
static short first_person_weapon_index_from_weapon_index(long weapon_index);
static short first_person_weapon_index_from_unit_index(long unit_index);
static void first_person_weapons_feeds_update(void);
static void first_person_weapon_feeds_update(void);
static void first_person_weapon_feeds_message(long unit_index, long weapon_index, short message_type);
static short first_person_weapon_feed_from_unit_index(long unit_index);
void first_person_weapon_message_from_unit(long unit_index, short message_type);
void first_person_weapon_message_from_weapon(long weapon_index, short message_type);

#include "under_test.inc"

static void first_person_weapons_feeds_update(void)
{
	first_person_weapon_feeds_update();
}

/* ---------- the cases */

enum { U0 = UNIT_BASE, U1, U2, U3 };
enum { W0 = WEAPON_BASE, W1, W2, W3, W4 };

static void weapon_make(long weapon_index, short weapon_type)
{
	world.weapons_exist[weapon_index - WEAPON_BASE] = TRUE;
	world.weapons[weapon_index - WEAPON_BASE].definition_index = weapon_type;
	world.weapons[weapon_index - WEAPON_BASE].weapon.magazines[0].rounds_loaded = 10;
	world.weapons[weapon_index - WEAPON_BASE].weapon.magazines[0].rounds_total = 50;
}

static void unit_make(long unit_index, long in_hand, long pocket)
{
	struct unit_datum *unit = &world.units[unit_index - UNIT_BASE];
	short slot;

	world.units_exist[unit_index - UNIT_BASE] = TRUE;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		unit->unit.weapon_object_indices[slot] = NONE;
	unit->unit.weapon_object_indices[0] = in_hand;
	unit->unit.weapon_object_indices[1] = pocket;
	unit->unit.current_weapon_index = 0;
	unit->unit.current_zoom_level = NONE;
}

/* this machine's player in U0 (an assault rifle, W0), its teammates U1
(an assault rifle, W1, in hand, a pistol, W2, in the pocket) and U2 (a
plasma pistol, W3) in its link's feeds */
static void world_make(void)
{
	short index;

	memset(&world, 0, sizeof(world));
	memset(local_slots, 0, sizeof(local_slots));
	memset(first_person_weapon_feeds, 0, sizeof(first_person_weapon_feeds));
	for (index = 0; index < FIRST_PERSON_WEAPON_SLOTS; index++)
	{
		first_person_weapon_get(index)->unit_index = NONE;
		first_person_weapon_get(index)->weapon_index = NONE;
		first_person_weapon_sound_indices[index] = NONE;
	}
	for (index = 0; index < NUMBER_OF_FIRST_PERSON_WEAPON_ANIMATIONS; index++)
	{
		world.animation_indices[index].animation_index = index;
		world.animations[index].frame_count = 10;
	}
	world.first_person_animations.animations.count = NUMBER_OF_FIRST_PERSON_WEAPON_ANIMATIONS;
	world.first_person_animations.animations.address = world.animation_indices;
	world.graph.first_person_weapon_animations.count = 1;
	world.graph.first_person_weapon_animations.address = &world.first_person_animations;
	world.graph.animations.count = NUMBER_OF_FIRST_PERSON_WEAPON_ANIMATIONS;
	world.graph.animations.address = world.animations;
	world.magazine.rounds_loaded_maximum = 12;
	for (index = 0; index < NUMBER_OF_WEAPON_TYPES; index++)
	{
		world.definitions[index].weapon.weapon_type = index;
		world.definitions[index].weapon.magazines.count = 1;
		world.definitions[index].weapon.magazines.address = &world.magazine;
		world.definitions[index].weapon.interface_definition.first_person_model.index = 1;
		world.definitions[index].weapon.interface_definition.first_person_animations.index = 1;
	}
	world.interface.hands.index = 2;
	world.globals.first_person_interface.count = 1;
	world.globals.first_person_interface.address = &world.interface;
	for (index = 0; index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; index++)
		world.local_players[index] = NONE;

	weapon_make(W0, _weapon_type_undefined);
	weapon_make(W1, _weapon_type_undefined);
	weapon_make(W2, _weapon_type_undefined);
	weapon_make(W3, _weapon_type_plasma_pistol);
	weapon_make(W4, _weapon_type_needler);
	unit_make(U0, W0, NONE);
	unit_make(U1, W1, W2);
	unit_make(U2, W3, NONE);
	world.local_players[0] = 0;
	world.players[0].unit_index = U0;
	first_person_weapon_new_unit(0, U0);
	world.linked[0] = U1;
	world.linked[1] = U2;
	world.linked_count = 2;
	first_person_weapon_feeds_update();
}

static struct first_person_weapon *feed_of(long unit_index)
{
	short slot = first_person_weapon_feed_from_unit_index(unit_index);

	CHECK(slot != NONE, "no feed's slot animates unit %lx", (unsigned long)unit_index);
	CHECK(slot >= MAXIMUM_NUMBER_OF_LOCAL_PLAYERS && slot < FIRST_PERSON_WEAPON_SLOTS, "a feed in slot %d", slot);
	return first_person_weapon_get(slot);
}

static char const *state_name(short state)
{
	return state >= 0 && state < NUMBER_OF_FIRST_PERSON_WEAPON_STATES ? first_person_weapon_state_names[state] : "none";
}

#define CHECK_STATE(unit_index, expected) \
	CHECK(feed_of(unit_index)->state == (expected), "unit %lx's feed is %s, not %s", (unsigned long)(unit_index), \
		state_name(feed_of(unit_index)->state), state_name(expected))

/* nothing a feed's slot did touched the game: no effect started or stopped
for it, nobody unzoomed */
static void check_drawing_only(int unzooms)
{
	short slot;

	for (slot = MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; slot < FIRST_PERSON_WEAPON_SLOTS; slot++)
	{
		CHECK(!world.effects_started[slot] && !world.effects_stopped[slot],
			"slot %d (a feed's) started %d and stopped %d of the weapon's effects", slot, world.effects_started[slot],
			world.effects_stopped[slot]);
	}
	CHECK(world.unzooms == unzooms, "%d unzooms, not %d", world.unzooms, unzooms);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	world_make();
	if (!strcmp(name, "linked"))
	{
		/* each linked teammate in a slot of its own, idle, with the weapon in
		their hand, stepped with the tick; this machine's player's slot apart */
		CHECK(feed_of(U1) != feed_of(U2), "two teammates share a slot");
		CHECK(feed_of(U1)->weapon_index == W1 && feed_of(U2)->weapon_index == W3, "weapons %lx %lx",
			(unsigned long)feed_of(U1)->weapon_index, (unsigned long)feed_of(U2)->weapon_index);
		CHECK_STATE(U1, _first_person_weapon_state_idle);
		CHECK_STATE(U2, _first_person_weapon_state_idle);
		CHECK(world.updates[first_person_weapon_feed_from_unit_index(U1)] == 1, "a linked slot not stepped");
		CHECK(first_person_weapon_feed_from_unit_index(U0) == NONE, "this machine's own player in a feed's slot");
		CHECK(first_person_weapon_get(0)->unit_index == U0 && first_person_weapon_get(0)->weapon_index == W0,
			"this machine's player's slot changed");
		/* (a second tick: the same slots) */
		{
			struct first_person_weapon *before = feed_of(U1);

			first_person_weapon_feeds_update();
			CHECK(feed_of(U1) == before && feed_of(U1)->weapon_index == W1, "the slot moved or lost its weapon");
		}
		check_drawing_only(0);
	}
	else if (!strcmp(name, "fire"))
	{
		/* each shot of the weapon in a teammate's hand: the fire state and
		the firing push back's kick, theirs alone */
		first_person_weapon_message_from_weapon(W1, _first_person_weapon_message_primary_fire);
		CHECK_STATE(U1, _first_person_weapon_state_primary_fire);
		CHECK(fabsf(feed_of(U1)->firing_push_back_velocity - FIRST_PERSON_WEAPON_FIRING_PUSH_BACK_VELOCITY) < 1e-6f,
			"the push back's kick %f", feed_of(U1)->firing_push_back_velocity);
		CHECK_STATE(U2, _first_person_weapon_state_idle);
		CHECK(first_person_weapon_get(0)->state == _first_person_weapon_state_idle, "this machine's player fired");
		first_person_weapon_message_from_weapon(W3, _first_person_weapon_message_primary_fire);
		CHECK_STATE(U2, _first_person_weapon_state_primary_fire);
		/* (the weapon in a pocket fires nothing) */
		first_person_weapon_next_state(first_person_weapon_feed_from_unit_index(U1));
		first_person_weapon_message_from_weapon(W2, _first_person_weapon_message_primary_fire);
		CHECK_STATE(U1, _first_person_weapon_state_idle);
		/* (the feeds' own: the game's sound of the teammates' weapons, as
		before, once a shot) */
		CHECK(world.sounds == 3, "%d sounds", world.sounds);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "melee"))
	{
		first_person_weapon_message_from_unit(U1, _first_person_weapon_message_melee);
		CHECK_STATE(U1, _first_person_weapon_state_melee);
		CHECK_STATE(U2, _first_person_weapon_state_idle);
		first_person_weapon_next_state(first_person_weapon_feed_from_unit_index(U1));
		CHECK_STATE(U1, _first_person_weapon_state_idle);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "reload"))
	{
		/* a reload, empty or full, back to idle at its end; unzooming
		nobody (the teammate's own machine unzooms them) */
		first_person_weapon_message_from_weapon(W1, _first_person_weapon_message_reload_while_empty);
		CHECK_STATE(U1, _first_person_weapon_state_reload_while_empty);
		first_person_weapon_next_state(first_person_weapon_feed_from_unit_index(U1));
		CHECK_STATE(U1, _first_person_weapon_state_idle);
		first_person_weapon_message_from_weapon(W1, _first_person_weapon_message_reload_while_full);
		CHECK_STATE(U1, _first_person_weapon_state_reload_while_full);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "grenade"))
	{
		first_person_weapon_message_from_unit(U1, _first_person_weapon_message_throw_grenade);
		CHECK_STATE(U1, _first_person_weapon_state_throw_grenade);
		/* (thrown with the weapon overheated: the overheated throw) */
		world.weapons[W3 - WEAPON_BASE].weapon.flags |= FLAG(_weapon_overheated_bit);
		first_person_weapon_message_from_unit(U2, _first_person_weapon_message_throw_grenade);
		CHECK_STATE(U2, _first_person_weapon_state_throw_grenade_overheated);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "switch"))
	{
		/* the weapon put away, then the next readied in the hand: the slot
		takes it with the ready animation; the effects the weapons make
		stay the game's (the feed drawn: visible) */
		feed_of(U1)->visible = TRUE;
		first_person_weapon_message_from_weapon(W1, _first_person_weapon_message_put_away);
		CHECK_STATE(U1, _first_person_weapon_state_put_away);
		world.units[U1 - UNIT_BASE].unit.current_weapon_index = 1;
		first_person_weapon_message_from_weapon(W2, _first_person_weapon_message_ready);
		CHECK(feed_of(U1)->weapon_index == W2, "the slot holds %lx, not the pistol readied",
			(unsigned long)feed_of(U1)->weapon_index);
		CHECK_STATE(U1, _first_person_weapon_state_ready);
		CHECK(feed_of(U1)->visible, "the feed's weapon hidden by the switch");
		first_person_weapon_feeds_update();
		CHECK_STATE(U1, _first_person_weapon_state_ready);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "switch_unsaid"))
	{
		/* a weapon put in the teammate's hand with no message of it (the
		host's word of what they carry): readied at the next tick */
		world.units[U1 - UNIT_BASE].unit.current_weapon_index = 1;
		first_person_weapon_feeds_update();
		CHECK(feed_of(U1)->weapon_index == W2, "the slot holds %lx", (unsigned long)feed_of(U1)->weapon_index);
		CHECK_STATE(U1, _first_person_weapon_state_ready);
		/* (and none in it: none) */
		world.units[U1 - UNIT_BASE].unit.current_weapon_index = NONE;
		first_person_weapon_feeds_update();
		CHECK(feed_of(U1)->weapon_index == NONE, "the slot holds %lx with nothing in hand",
			(unsigned long)feed_of(U1)->weapon_index);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "overheat"))
	{
		/* the plasma pistol overheating, overheated at the animation's end;
		a charge, and the overcharged shot's overheat */
		world.weapons[W3 - WEAPON_BASE].weapon.flags |= FLAG(_weapon_overheated_bit);
		first_person_weapon_message_from_weapon(W3, _first_person_weapon_message_overheating);
		CHECK_STATE(U2, _first_person_weapon_state_overheating);
		first_person_weapon_next_state(first_person_weapon_feed_from_unit_index(U2));
		CHECK_STATE(U2, _first_person_weapon_state_overheated);
		world.weapons[W3 - WEAPON_BASE].weapon.flags = 0;
		first_person_weapon_message_from_weapon(W3, _first_person_weapon_message_charged);
		CHECK_STATE(U2, _first_person_weapon_state_charged);
		world.weapons[W3 - WEAPON_BASE].weapon.flags |= FLAG(_weapon_overheated_bit);
		first_person_weapon_message_from_weapon(W3, _first_person_weapon_message_overheating_super_recoil);
		CHECK_STATE(U2, _first_person_weapon_state_overheating_super_recoil);
		CHECK_STATE(U1, _first_person_weapon_state_idle);
		check_drawing_only(0);
	}
	else if (!strcmp(name, "unlinked"))
	{
		/* a teammate whose feed went: its slot freed, and their shots move
		no slot; one who died (the unit gone): freed too */
		world.linked_count = 1;
		first_person_weapon_feeds_update();
		CHECK(first_person_weapon_feed_from_unit_index(U2) == NONE, "an unlinked teammate keeps a slot");
		first_person_weapon_message_from_weapon(W3, _first_person_weapon_message_primary_fire);
		{
			short slot;

			for (slot = MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; slot < FIRST_PERSON_WEAPON_SLOTS; slot++)
			{
				CHECK(first_person_weapon_get(slot)->state != _first_person_weapon_state_primary_fire,
					"slot %d fired with an unlinked teammate's weapon", slot);
			}
		}
		world.units_exist[U1 - UNIT_BASE] = FALSE;
		first_person_weapon_feeds_update();
		CHECK(first_person_weapon_feed_from_unit_index(U1) == NONE, "a gone unit keeps a slot");
		check_drawing_only(0);
	}
	else if (!strcmp(name, "split"))
	{
		/* a split screen partner (local player 1, in U3 with a needler, W4)
		in this player's link: their own slot and their feed's both reload,
		and only their own unzooms them */
		unit_make(U3, W4, NONE);
		world.local_players[1] = 1;
		world.players[1].unit_index = U3;
		first_person_weapon_new_unit(1, U3);
		world.linked[2] = U3;
		world.linked_count = 3;
		first_person_weapon_feeds_update();
		first_person_weapon_message_from_weapon(W4, _first_person_weapon_message_reload_while_empty);
		CHECK(first_person_weapon_get(1)->state == _first_person_weapon_state_reload_while_empty,
			"the partner's own slot is %s", state_name(first_person_weapon_get(1)->state));
		CHECK_STATE(U3, _first_person_weapon_state_reload_while_empty);
		first_person_weapon_message_from_unit(U3, _first_person_weapon_message_melee);
		CHECK(first_person_weapon_get(1)->state == _first_person_weapon_state_melee, "the partner's own slot is %s",
			state_name(first_person_weapon_get(1)->state));
		CHECK_STATE(U3, _first_person_weapon_state_melee);
		CHECK_STATE(U1, _first_person_weapon_state_idle);
		/* (the partner's sound is their own slot's, as before: none here) */
		CHECK(world.sounds == 0, "%d sounds", world.sounds);
		check_drawing_only(1);
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
