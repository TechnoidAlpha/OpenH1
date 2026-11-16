/*
COOP_BSP_SWITCH_MOVES.C (test)

Who a structure BSP switch moves (players.c's players_reconnect_to_structure_bsp,
player_teleport_on_bsp_switch, players_coop_bsp_switch_trigger). In network
co-op only the players who must: those outside the new BSP and those on one
of its loading zones; a teammate inside the new BSP, at the seam, stays. Split
screen keeps the original's rule (everyone not on the crossed zone).
test_coop_bsp_switch_moves.py takes the code (under_test.inc).

The world is a line along x. BSP 0 is behind (x < 30), BSP 1 is x 0..100,
the overlap x 0..30. Zone 0 (BSP 0 to 1) is x 35..55, zone 1 (BSP 1 to 2)
x 85..100, zone 2 (BSP 1 to 0) x 0..5.
*/

#include "harness.h"
#include <limits.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SHORT_MAX 32767
#define match_vassert(file, line, condition, ...) ((void)0)
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])

enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };
enum
{
	_collision_test_front_facing_surfaces_bit,
	_collision_test_ignore_invisible_surfaces_bit,
	_collision_test_structure_bit,
	_collision_test_objects_scenery_bit
};

struct tag_block { long count; void *address; };
struct scenario_bsp_switch_trigger_volume
{
	short trigger_volume_index;
	short source_structure_bsp_index;
	short destination_structure_bsp_index;
	short cutscene_flag_index;
};
struct scenario_cutscene_flag { real_point3d position; };
struct scenario { struct tag_block bsp_switch_trigger_volumes, cutscene_flags; };
struct structure_leaf { short cluster_index; };
struct structure_bsp { struct tag_block leaves; };
struct player_datum { long unit_index; short local_player_index; long cluster_index; };
struct object_datum
{
	struct { real_point3d position, bounding_sphere_center; long parent_object_index; } object;
};
#define biped_datum object_datum
struct players_globals_t
{
	short local_player_count;
	short pending_teleport_starting_location_index;
	boolean respawn_failed;
};

#define PLAYERS 4
#define FIRST_UNIT 10

static struct player_datum players[PLAYERS];
static struct object_datum objects[FIRST_UNIT + PLAYERS];
static boolean moved[PLAYERS];
static struct players_globals_t players_globals_storage, *players_globals = &players_globals_storage;
static struct data_array *player_data;
static short global_structure_bsp_index;
static boolean coop;

static struct scenario_bsp_switch_trigger_volume zones[] =
{
	{0, 0, 1, NONE},
	{1, 1, 2, NONE},
	{2, 1, 0, NONE},
};
static struct { real low, high; } volumes[] = {{35.f, 55.f}, {85.f, 100.f}, {0.f, 5.f}};
static struct scenario scenario = {{3, zones}, {0, NULL}};
static struct structure_leaf leaf = {7};
static struct structure_bsp bsp = {{1, &leaf}};

struct data_iterator { long index, datum_index; };

static void data_iterator_new(struct data_iterator *iterator, void *data)
{
	(void)data;
	iterator->index = 0;
	iterator->datum_index = NONE;
}

static void *data_iterator_next(struct data_iterator *iterator)
{
	while (iterator->index < PLAYERS)
	{
		iterator->datum_index = iterator->index;
		return &players[iterator->index++];
	}
	return NULL;
}

static struct player_datum *player_get(long index) { return &players[index]; }
static struct object_datum *object_get(long index) { return &objects[index]; }
static struct object_datum *object_try_and_get(long index) { return index == NONE ? NULL : &objects[index]; }
#define biped_get object_get
#define biped_try_and_get object_try_and_get
static struct scenario *global_scenario_get(void) { return &scenario; }
static struct structure_bsp *global_structure_bsp_get(void) { return &bsp; }
static boolean network_coop_active(void) { return coop; }
static int game_connection(void) { return coop ? _game_connection_network_server : _game_connection_local; }

/* the new BSP (1) holds x 0..100 */
static long scenario_leaf_index_from_point(real_point3d const *point)
{
	return point->x >= 0.f && point->x <= 100.f ? 0 : NONE;
}

static boolean scenario_trigger_volume_test_point(short volume, real_point3d const *point)
{
	return point->x >= volumes[volume].low && point->x <= volumes[volume].high;
}

static boolean scenario_trigger_volume_test_object(short volume, long object_index)
{
	return scenario_trigger_volume_test_point(volume, &objects[object_index].object.bounding_sphere_center);
}

static boolean is_player_in_trigger(short zone, long unit_index)
{
	return scenario_trigger_volume_test_object(zones[zone].trigger_volume_index, unit_index);
}

static boolean collision_test_point(unsigned long flags, real_point3d const *point, long ignore)
{
	(void)flags; (void)point; (void)ignore;
	return FALSE;
}

static void biped_get_physics_pill(long unit_index, real_point3d *base, real *height, real *width)
{
	*base = objects[unit_index].object.position;
	*height = 0.7f;
	*width = 0.2f;
}

static void unit_exit_seat_end(long unit_index) { objects[unit_index].object.parent_object_index = NONE; }

static void place(long unit_index, real_point3d const *position)
{
	objects[unit_index].object.position = *position;
	objects[unit_index].object.bounding_sphere_center = *position;
}

static boolean player_teleport_internal(long player_index, long source_unit_index, real_point3d const *position)
{
	(void)source_unit_index;
	place(players[player_index].unit_index, position);
	moved[player_index] = TRUE;
	return TRUE;
}

static boolean player_place_beside_teammate(long player_index) { (void)player_index; return FALSE; }

static boolean players_coop_teleport_stacked(long player_index, long source_unit_index, real_point3d const *position)
{
	return player_teleport_internal(player_index, source_unit_index, position);
}

/* (the game's code assigns in its loops' conditions) */
#pragma clang diagnostic ignored "-Wparentheses"

#include "under_test.inc"

/* the host (player 0) crossed zone 0 at x 50 into BSP 1; teammates at x
40 (inside BSP 1, on zone 0 too), -50 (behind, outside BSP 1) and 90 (on
zone 1, which would switch on to BSP 2) */
static void setup(boolean network_coop)
{
	static real const xs[PLAYERS] = {50.f, 40.f, -50.f, 90.f};
	long index;

	coop = network_coop;
	global_structure_bsp_index = 1;
	players_globals->local_player_count = network_coop ? 1 : PLAYERS;
	players_globals->pending_teleport_starting_location_index = 0;
	players_globals->respawn_failed = FALSE;
	for (index = 0; index < PLAYERS; index++)
	{
		real_point3d position = {xs[index], 0.f, 0.f};

		players[index].unit_index = FIRST_UNIT + index;
		players[index].local_player_index = index == 0 || !network_coop ? (short)index : NONE;
		players[index].cluster_index = 3;
		objects[FIRST_UNIT + index].object.parent_object_index = NONE;
		place(FIRST_UNIT + index, &position);
		moved[index] = FALSE;
	}
	players_reconnect_to_structure_bsp();
	for (index = 0; index < PLAYERS; index++)
		CHECK(players[index].cluster_index == NONE, "player %ld kept the old BSP's cluster", index);
	CHECK(players_globals->pending_teleport_starting_location_index == NONE, "the switch was left pending");
	CHECK(!moved[0], "the crosser was moved");
}

static void case_overlap(void)
{
	setup(TRUE);
	CHECK(!moved[1], "a teammate inside the new BSP was moved");
	CHECK(objects[FIRST_UNIT + 1].object.position.x == 40.f, "the teammate inside the new BSP is not where it stood");
}

static void case_outside(void)
{
	setup(TRUE);
	CHECK(moved[2], "a teammate outside the new BSP was left there");
	CHECK(objects[FIRST_UNIT + 2].object.position.x == 50.f, "the teammate outside was not brought to the crosser");
}

static void case_switch_back(void)
{
	setup(TRUE);
	CHECK(moved[3], "a teammate on one of the new BSP's loading zones was left on it");
	CHECK(objects[FIRST_UNIT + 3].object.position.x == 50.f, "the teammate on the zone was not brought to the crosser");
}

/* split screen: the original's rule, everyone not on the crossed zone */
static void case_split_screen(void)
{
	setup(FALSE);
	CHECK(!moved[1], "split screen moved a player on the crossed zone");
	CHECK(moved[2] && moved[3], "split screen left a player off the crossed zone");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("overlap") case_overlap();
	else CASE("outside") case_outside();
	else CASE("switch-back") case_switch_back();
	else CASE("split-screen") case_split_screen();
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
