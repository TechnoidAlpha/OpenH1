/*
WEAPON_PICKUP.C (test)

The real unit_should_autopick_weapon (players.c) and network_standard.c's
pickup way (network_standard_walk_over_pickup, the record's stamp and an OpenCE
machine's copy of it) over a fake game: whether a player with one weapon picks
up a second by walking over it. The campaign always does (the Xbox's); in
multiplayer, as the Xbox did, they hold the action button unless game.auto_pickup
asks for Custom Edition's way; an OpenH1 game takes its host's (the record
carries it), a Legacy game and an OpenCE machine's player walk over, as OpenCE
plays. test_weapon_pickup.py takes the code (under_test.inc) and the record's
bit (config.inc).
*/

#include "harness.h"

#define SET_FLAG(f, b, v) ((v) ? ((f) |= (unsigned)FLAG(b)) : ((f) &= (unsigned)~FLAG(b)))
#define csmemcpy memcpy

#include "config.inc"

enum
{
	_game_connection_local = 0,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
	_weapon_doesnt_count_toward_maximum_bit = 3,
	/* the weapon walked over, and the player's unit */
	WEAPON = 20,
	UNIT = 5,
	PLAYER = 0x10002,
	OPENCE_PLAYER = 0x10003,
};

/* the game record, as much as the code reads */
struct network_game
{
	byte cooperative_flags;
	short standard_rate;
};

/* the game: its connection, whether it is multiplayer, this machine's setting */
static short connection;
static boolean engine_running;
static boolean auto_pickup_setting;
static short game_connection(void) { return connection; }
static boolean game_engine_running(void) { return engine_running; }
static int config_boolean(char const *name)
{
	CHECK(!strcmp(name, "game.auto_pickup"), "an unexpected setting %s", name);
	return auto_pickup_setting;
}

/* network_standard.c's state: hosting or joined a Standard game, and the record */
static struct
{
	boolean hosting;
	long hosting_rate;
	boolean hosting_hold_to_pick_up;
	boolean joined;
} network_standard_globals;
static struct network_game record;
static struct network_game *network_game_get_game(void) { return &record; }
static long network_standard_server_rate(boolean in_lobby) { return network_standard_globals.hosting ? 60 : 0; }
static boolean network_standard_player_is_opence(long player_index) { return player_index == OPENCE_PLAYER; }

/* the player's unit, its weapons, the weapon walked over */
static long weapon_count;
static long unit_player = PLAYER;
struct unit_datum { struct { long player_index; } unit; };
static struct unit_datum unit;
static struct unit_datum *unit_get(long index) { CHECK(index == UNIT, "wrong unit %ld", index); unit.unit.player_index = unit_player; return &unit; }
struct weapon_datum { long definition_index; };
static struct weapon_datum weapon = { 9 };
struct weapon_definition { struct { unsigned long flags; } weapon; };
static struct weapon_definition weapon_definition;
static struct weapon_datum *weapon_try_and_get(long index)
{
	CHECK(index == WEAPON, "wrong weapon %ld", index);
	return &weapon;
}
#define weapon_definition_get(index) (&weapon_definition)
#define unit_get_weapon_count(index) (weapon_count)
#define unit_approve_weapon_pickup(unit, weapon) TRUE
#define game_engine_force_autopickup(unit, weapon) FALSE

#include "under_test.inc"

/* a player with one weapon (or as many as given) walks over another */
static boolean walks_over(long count)
{
	weapon_count = count;
	return unit_should_autopick_weapon(UNIT, WEAPON);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	/* the campaign: a second weapon on walking over it, whatever the setting */
	CASE("campaign")
	{
		CHECK(walks_over(1), "the campaign held the action button for a second weapon");
		auto_pickup_setting = TRUE;
		CHECK(walks_over(1), "the campaign held the action button for a second weapon (auto_pickup)");
		return 0;
	}
	/* a game of this machine's own: the Xbox's way by default, Custom Edition's with the setting */
	CASE("local")
	{
		engine_running = TRUE;
		CHECK(!walks_over(1), "multiplayer walked over a second weapon with game.auto_pickup false");
		CHECK(walks_over(0), "a player with no weapon held the action button for one");
		CHECK(!walks_over(2), "a player with two weapons walked over a third");
		auto_pickup_setting = TRUE;
		CHECK(walks_over(1), "multiplayer held the action button with game.auto_pickup true");
		return 0;
	}
	/* an OpenH1 host: its setting as read in the lobby, held through the game, and
	an OpenCE machine's player walks over */
	CASE("host")
	{
		engine_running = TRUE;
		connection = _game_connection_network_server;
		network_standard_globals.hosting = TRUE;
		network_standard_stamp_game(&record, TRUE);
		CHECK(!walks_over(1), "the host's players walked over a second weapon (Xbox's way set)");
		CHECK(TEST_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit),
			"the record does not say hold to pick up");
		unit_player = OPENCE_PLAYER;
		CHECK(walks_over(1), "an OpenCE machine's player held the action button");
		unit_player = PLAYER;
		/* (a change during the game waits for the lobby) */
		auto_pickup_setting = TRUE;
		network_standard_stamp_game(&record, FALSE);
		CHECK(!walks_over(1), "the host's way changed during the game");
		CHECK(TEST_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit),
			"the record's way changed during the game");
		network_standard_stamp_game(&record, TRUE);
		CHECK(walks_over(1), "the host's way did not change in the lobby");
		CHECK(!TEST_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit),
			"the record still says hold to pick up");
		return 0;
	}
	/* an OpenCE machine's copy of the record: no bit of OpenH1's */
	CASE("opence-record")
	{
		struct network_game const *copy;

		network_standard_globals.hosting = TRUE;
		network_standard_stamp_game(&record, TRUE);
		copy = network_standard_game_for_machine(&record, TRUE);
		CHECK(!TEST_FLAG(copy->cooperative_flags, _network_game_hold_to_pick_up_bit),
			"an OpenCE machine's record says hold to pick up");
		CHECK(network_standard_game_for_machine(&record, FALSE) == &record, "an OpenH1 machine's record is a copy");
		return 0;
	}
	/* a Legacy host: OpenCE's way, and nothing in its record */
	CASE("legacy-host")
	{
		engine_running = TRUE;
		connection = _game_connection_network_server;
		network_standard_stamp_game(&record, TRUE);
		CHECK(walks_over(1), "a Legacy game's players held the action button");
		CHECK(!TEST_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit),
			"a Legacy host's record says hold to pick up");
		return 0;
	}
	/* a client: its host's word, whatever its own setting; a Legacy host's, walking over */
	CASE("client")
	{
		engine_running = TRUE;
		connection = _game_connection_network_client;
		auto_pickup_setting = TRUE;
		network_standard_globals.joined = TRUE;
		SET_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit, TRUE);
		CHECK(!walks_over(1), "a client walked over a second weapon in a game whose host holds");
		unit_player = OPENCE_PLAYER;
		CHECK(walks_over(1), "a client took an OpenCE player to hold the action button");
		unit_player = PLAYER;
		auto_pickup_setting = FALSE;
		SET_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit, FALSE);
		CHECK(walks_over(1), "a client held the action button in a game whose host walks over");
		network_standard_globals.joined = FALSE;
		SET_FLAG(record.cooperative_flags, _network_game_hold_to_pick_up_bit, TRUE);
		CHECK(walks_over(1), "a Legacy game's client held the action button");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
