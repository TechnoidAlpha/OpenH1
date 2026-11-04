/*
UNARMED_MELEE.C (test)

The real biped_update_on_foot (bipeds.c) and units.c's unarmed blow over a fake
player's biped holding no weapon: where the map has a blow to give (a
multiplayer map's first weapon's, for a gametype's loadout of none) the player
melees, and their blow lands once; where it has none (a campaign's unarmed
start, a10's) the player does not melee at all, as on the Xbox, so no hit and no
hit sound. A weapon in hand, or a biped with a blow of its own, melees as it
always did. test_unarmed_melee.py takes the code (under_test.inc) and the
unarmed melee's length (config.inc).
*/

#include "harness.h"

#include "config.inc"

enum
{
	_object_dead_bit = 0,
	_biped_airborne_bit = 0,
	_biped_slipping_bit = 1,
	_unit_control_use_equipment_bit = 4,
	_unit_animation_action_melee = 7,
	_first_person_weapon_message_melee = 2,
	_first_person_weapon_animation_melee = 3,
	_weapon_first_person_animation_time_frame_count = 0,
	_weapon_first_person_animation_time_private_key_frame = 1,
	/* the weapon in hand, its frames and the blow's frame (as a pistol's) */
	WEAPON = 40,
	WEAPON_FRAMES = 20,
	WEAPON_KEY_FRAME = 8,
	/* the first multiplayer weapon's blow, and a biped's own */
	FIRST_WEAPON_BLOW = 71,
	BIPED_BLOW = 72,
};

struct unit_animation_update_data { int unused; };

/* the player's biped */
struct biped_datum
{
	long definition_index;
	struct { unsigned long damage_flags; } object;
	struct
	{
		long player_index;
		unsigned long control_flags;
		short current_weapon_index;
		short parent_seat_index;
		short current_zoom_level;
	} unit;
	struct { unsigned long flags; short landing; short player_melee_ticks, player_melee_attack_tick; } biped;
};
static struct biped_datum biped;
static struct biped_datum *biped_get(long index) { CHECK(index == 3, "wrong biped %ld", index); return &biped; }
#define unit_get(index) biped_get(index)

/* the biped's definition: its own blow, or NONE (a player's has none) */
struct unit_definition { struct { struct { long index; } melee_damage; } unit; };
static struct unit_definition biped_definition;
#define unit_definition_get(index) (&biped_definition)

/* the weapons: the one in hand (NONE: unarmed) and the globals' multiplayer list */
static long weapon_in_hand;
struct weapon_definition { struct { struct { long index; } melee_attack_damage; } weapon; };
static struct weapon_definition first_weapon = { { { FIRST_WEAPON_BLOW } } };
#define weapon_definition_get(index) (&first_weapon)
struct game_globals { struct { long count; } weapon_list; };
static struct game_globals globals;
#define scenario_get_game_globals() (&globals)
static long list_index_to_weapon_definition_index(short index)
{
	CHECK(globals.weapon_list.count > index, "element %d of an empty weapon list", index);
	return 9;
}
#define unit_inventory_get_weapon(unit, slot) (weapon_in_hand)
#define weapon_prevents_melee_attack(weapon) FALSE
static long weapon_get_first_person_animation_time(long weapon, short kind, short animation, long unused)
{
	return kind == _weapon_first_person_animation_time_frame_count ? WEAPON_FRAMES : WEAPON_KEY_FRAME;
}

/* what the update does: the melee's animation started, its blow caused */
static int melee_starts, blows;
#define unit_animation_start_action(unit, action) (melee_starts += (action) == _unit_animation_action_melee)
#define unit_cause_player_melee_damage(unit) (blows++)
#define weapon_stop_reload(weapon) ((void)0)
#define first_person_weapon_message_from_unit(unit, message) ((void)0)
#define biped_update_dead(biped, animation) ((void)0)
#define biped_update_airborne(biped, animation) ((void)0)
#define biped_update_landing(biped, animation) ((void)0)
#define biped_update_slipping(biped, animation) ((void)0)
#define biped_verify_object_vectors(biped, label) ((void)0)
#define biped_try_to_make_footsteps(biped) ((void)0)
#define biped_falling_danger(biped) ((void)0)
#define biped_check_discard(biped) ((void)0)

#include "under_test.inc"

/* the melee button held for a tick, then the ticks a melee could take */
static void melee(void)
{
	struct unit_animation_update_data animation;
	int tick;

	biped.unit.control_flags = FLAG(_unit_control_use_equipment_bit);
	biped_update_on_foot(3, &animation);
	biped.unit.control_flags = 0;
	for (tick = 0; tick < 2 * WEAPON_FRAMES; tick++)
		biped_update_on_foot(3, &animation);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	biped.unit.player_index = 0;
	biped.unit.current_weapon_index = NONE;
	biped.unit.parent_seat_index = NONE;
	biped.unit.current_zoom_level = NONE;
	biped.biped.landing = NONE;
	biped_definition.unit.melee_damage.index = NONE;
	weapon_in_hand = NONE;
	/* a campaign's unarmed start (a10's): no multiplayer weapons, no blow of
	the biped's own: no melee, so no hit and no hit's sound */
	CASE("campaign-unarmed")
	{
		melee();
		CHECK(melee_starts == 0, "an unarmed campaign player meleed (%d times)", melee_starts);
		CHECK(blows == 0, "an unarmed campaign player's blow was caused %d times", blows);
		CHECK(biped.biped.player_melee_ticks == 0, "the melee's ticks are %d", biped.biped.player_melee_ticks);
		return 0;
	}
	/* a gametype's loadout of none: the first multiplayer weapon's blow, once */
	CASE("multiplayer-unarmed")
	{
		globals.weapon_list.count = 1;
		melee();
		CHECK(melee_starts == 1, "an unarmed multiplayer player meleed %d times", melee_starts);
		CHECK(blows == 1, "an unarmed multiplayer player's blow was caused %d times", blows);
		CHECK(unit_unarmed_melee_damage(3) == FIRST_WEAPON_BLOW, "the unarmed blow is %ld",
			unit_unarmed_melee_damage(3));
		return 0;
	}
	/* a biped with a blow of its own melees unarmed anywhere */
	CASE("biped-own-blow")
	{
		biped_definition.unit.melee_damage.index = BIPED_BLOW;
		melee();
		CHECK(melee_starts == 1 && blows == 1, "a biped with its own blow meleed %d times, %d blows", melee_starts,
			blows);
		return 0;
	}
	/* a weapon in hand melees as it always did, in the campaign too */
	CASE("armed")
	{
		weapon_in_hand = WEAPON;
		melee();
		CHECK(melee_starts == 1 && blows == 1, "an armed player meleed %d times, %d blows", melee_starts, blows);
		return 0;
	}
	/* in a vehicle's seat, no melee */
	CASE("seated")
	{
		globals.weapon_list.count = 1;
		biped.unit.parent_seat_index = 0;
		melee();
		CHECK(melee_starts == 0 && blows == 0, "a seated player meleed %d times, %d blows", melee_starts, blows);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
