/*
COOP_HOST_TELEPORT.C (test)

Host Controls' BRING TO ME and GO TO in the host's simulation (players.c's
players_coop_host_can_teleport, players_coop_host_teleport): only a network
co-op host's, while the scripts leave the players their controls, for
another machine's living player who is not away, with the host's player in
the level, and GO TO only to a player inside it; the move beside the other
player (player_teleport, which leaves a seat first), else in them
(players_coop_teleport_stacked). test_coop_host_teleport.py takes the code
(under_test.inc).
*/

#include "harness.h"

enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };

struct player_datum { long unit_index; short local_player_index; };
struct object_datum { struct { real_point3d bounding_sphere_center; } object; struct { long player_index; } unit; };
#define unit_datum object_datum

/* the host's player (0, its unit 10), a client's (1, unit 11) */
#define HOST 0
#define CLIENT 1
#define PLAYERS 2

static struct player_datum players[PLAYERS];
static struct object_datum objects[16];
static struct
{
	boolean coop;
	int connection;
	boolean input;
	boolean away;
	boolean inside[PLAYERS];
	boolean room;
	boolean stack_room;
	/* what was done: the player moved, beside which unit, how */
	long moved;
	long beside;
	boolean stacked;
} world;

static struct player_datum *player_try_and_get(long index) { return index >= 0 && index < PLAYERS ? &players[index] : NULL; }
static struct object_datum *object_get(long index) { return &objects[index]; }
#define unit_get object_get
static boolean network_coop_active(void) { return world.coop; }
static int game_connection(void) { return world.connection; }
static boolean player_input_enabled(void) { return world.input; }
static boolean network_coop_player_away(long player_index) { return player_index == CLIENT && world.away; }
static boolean players_coop_unit_in_structure(long unit_index) { return world.inside[unit_index - 10]; }
/* (players.c's: the host's first player's unit in the level) */
static long players_coop_host_unit(void)
{
	return players[HOST].unit_index != NONE && world.inside[HOST] ? players[HOST].unit_index : NONE;
}
static boolean player_teleport(long player_index, long source_unit_index, real_point3d const *position)
{
	(void)position;
	if (!world.room)
		return FALSE;
	world.moved = player_index;
	world.beside = source_unit_index;
	return TRUE;
}
static boolean players_coop_teleport_stacked(long player_index, long source_unit_index, real_point3d const *position)
{
	(void)position;
	if (!world.stack_room)
		return FALSE;
	world.moved = player_index;
	world.beside = source_unit_index;
	world.stacked = TRUE;
	return TRUE;
}

#include "under_test.inc"

static void reset(void)
{
	memset(&world, 0, sizeof(world));
	world.coop = TRUE;
	world.connection = _game_connection_network_server;
	world.input = TRUE;
	world.inside[HOST] = world.inside[CLIENT] = TRUE;
	world.room = world.stack_room = TRUE;
	world.moved = world.beside = NONE;
	players[HOST].unit_index = 10;
	players[HOST].local_player_index = 0;
	players[CLIENT].unit_index = 11;
	players[CLIENT].local_player_index = NONE;
	objects[10].unit.player_index = HOST;
	objects[11].unit.player_index = CLIENT;
}

static boolean neither(void)
{
	return !players_coop_host_can_teleport(CLIENT, FALSE) && !players_coop_host_can_teleport(CLIENT, TRUE);
}

/* who may be moved */
static void case_who(void)
{
	reset();
	CHECK(players_coop_host_can_teleport(CLIENT, FALSE) && players_coop_host_can_teleport(CLIENT, TRUE),
		"a client's living player in co-op");
	CHECK(!players_coop_host_can_teleport(HOST, FALSE) && !players_coop_host_can_teleport(HOST, TRUE),
		"the host's own player");
	world.coop = FALSE;
	CHECK(neither(), "a versus game");
	reset();
	world.connection = _game_connection_network_client;
	CHECK(neither(), "a client");
	reset();
	players[CLIENT].unit_index = NONE;
	CHECK(neither(), "a dead player");
	reset();
	world.away = TRUE;
	CHECK(neither(), "a player away");
	reset();
	world.input = FALSE;
	CHECK(neither(), "a cutscene");
	reset();
	players[HOST].unit_index = NONE;
	CHECK(neither(), "the host's player dead");
	reset();
	world.inside[HOST] = FALSE;
	CHECK(neither(), "the host's player outside the level");
	reset();
	world.inside[CLIENT] = FALSE;
	CHECK(players_coop_host_can_teleport(CLIENT, FALSE) && !players_coop_host_can_teleport(CLIENT, TRUE),
		"one outside the level: brought in, never gone to");
}

/* BRING TO ME: the client's player beside the host's */
static void case_bring(void)
{
	long beside = NONE;

	reset();
	CHECK(players_coop_host_teleport(CLIENT, FALSE, &beside) == CLIENT && world.moved == CLIENT &&
		world.beside == 10 && beside == HOST && !world.stacked, "brought beside the host's player");
	reset();
	world.room = FALSE;
	CHECK(players_coop_host_teleport(CLIENT, FALSE, &beside) == CLIENT && world.stacked && world.beside == 10,
		"with no room beside, in them");
	reset();
	world.room = world.stack_room = FALSE;
	CHECK(players_coop_host_teleport(CLIENT, FALSE, &beside) == NONE, "nowhere: not moved");
	reset();
	players[CLIENT].unit_index = NONE;
	CHECK(players_coop_host_teleport(CLIENT, FALSE, &beside) == NONE && world.moved == NONE, "a dead player");
}

/* GO TO: the host's player beside the client's */
static void case_go_to(void)
{
	long beside = NONE;

	reset();
	CHECK(players_coop_host_teleport(CLIENT, TRUE, &beside) == HOST && world.moved == HOST && world.beside == 11 &&
		beside == CLIENT, "the host's player beside them");
	reset();
	world.inside[CLIENT] = FALSE;
	CHECK(players_coop_host_teleport(CLIENT, TRUE, &beside) == NONE && world.moved == NONE,
		"never to one outside the level");
	reset();
	world.coop = FALSE;
	CHECK(players_coop_host_teleport(CLIENT, TRUE, &beside) == NONE && world.moved == NONE, "never in versus");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("who") case_who();
	else CASE("bring") case_bring();
	else CASE("go-to") case_go_to();
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
