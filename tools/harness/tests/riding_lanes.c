/* Riding at game.sim_rate 60 and 120 (vehicle_riding_bipeds.c's
biped_riding_holds_lane and vehicle_riding_holds_lane): a player's biped in
a vehicle's window, and every vehicle such a biped names, move once a tick
(objects.h's macro lane), as at 30; nothing is held at 30 or with riding
off; the riders are taken as the objects' update begins and kept to the
tick's end, a window that ends in the rider's update changing nothing until
the next tick. The functions are compiled as they are, over fake players. */
#include "harness.h"
#include "config.inc"

enum { PLAYERS = 4 };
struct player_datum { long unit_index; };
struct biped_datum { struct { long riding_object_index; } biped; };
struct data_iterator { short index; long datum_index; };

static struct player_datum players[PLAYERS];
static struct biped_datum bipeds[PLAYERS];
static boolean in_window[PLAYERS];
static void *player_data = players;
static short substeps = 4;
static boolean riding_enabled = TRUE;
static long now = 100;

static short sim_substep_count(void) { return substeps; }
static boolean vehicle_riding_enabled(void) { return riding_enabled; }
static long game_time_get(void) { return now; }
static void data_iterator_new(struct data_iterator *iterator, void *data) { (void)data; iterator->index = 0; }
static void *data_iterator_next(struct data_iterator *iterator)
{
	while (iterator->index < PLAYERS)
	{
		short index = iterator->index++;

		if (players[index].unit_index != 0)
		{
			iterator->datum_index = index;
			return &players[index];
		}
	}
	return NULL;
}
/* (a biped's index is 1000 + its player's) */
static struct biped_datum *biped_try_and_get(long biped_index)
{
	return biped_index >= 1000 && biped_index < 1000 + PLAYERS ? &bipeds[biped_index - 1000] : NULL;
}
static boolean biped_riding_deferred(long biped_index) { return in_window[biped_index - 1000]; }

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";
	short index;

	for (index = 0; index < PLAYERS; index++)
	{
		players[index].unit_index = index < 3 ? 1000 + index : NONE;
		bipeds[index].biped.riding_object_index = NONE;
	}
	/* player 0 rides vehicle 50, player 1 is in vehicle 60's window (in the
	air it left), player 2 is on foot */
	in_window[0] = TRUE;
	bipeds[0].biped.riding_object_index = 50;
	in_window[1] = TRUE;
	bipeds[1].biped.riding_object_index = 60;
	bipeds[2].biped.riding_object_index = 70;

	if (!strcmp(name, "held"))
	{
		biped_riding_lanes_latch();
		CHECK(biped_riding_holds_lane(1000), "a rider in the sub-steps");
		CHECK(biped_riding_holds_lane(1001), "a biped in a window in the sub-steps");
		CHECK(!biped_riding_holds_lane(1002), "a biped on foot held on the tick");
		CHECK(vehicle_riding_holds_lane(50) && vehicle_riding_holds_lane(60), "a ridden deck in the sub-steps");
		CHECK(!vehicle_riding_holds_lane(70), "a deck named by a biped in no window held on the tick");
		CHECK(!vehicle_riding_holds_lane(NONE) && !biped_riding_holds_lane(NONE), "nothing held");
		substeps = 2;
		now++;
		CHECK(biped_riding_holds_lane(1000) && vehicle_riding_holds_lane(50), "at 60 not held");
		CHECK(!vehicle_riding_holds_lane(70), "at 60, a deck named by a biped in no window held");
		return 0;
	}
	if (!strcmp(name, "classic"))
	{
		/* (at 30, the lanes move in the tick: nothing changes) */
		substeps = 1;
		biped_riding_lanes_latch();
		CHECK(!biped_riding_holds_lane(1000) && !vehicle_riding_holds_lane(50), "held at 30");
		substeps = 4;
		riding_enabled = FALSE;
		now++;
		CHECK(!vehicle_riding_holds_lane(50), "a deck held with riding off");
		return 0;
	}
	if (!strcmp(name, "latched"))
	{
		biped_riding_lanes_latch();
		/* (the rider's window ends in its update this tick: held to the tick's end) */
		in_window[0] = FALSE;
		CHECK(biped_riding_holds_lane(1000) && vehicle_riding_holds_lane(50), "a rider let go within its tick");
		/* (a biped that lands on a deck at its last sub-step rides from the next) */
		in_window[2] = TRUE;
		CHECK(!biped_riding_holds_lane(1002) && !vehicle_riding_holds_lane(70), "a new rider held within its tick");
		now++;
		CHECK(!biped_riding_holds_lane(1000) && biped_riding_holds_lane(1002), "the next tick's riders not taken");
		biped_riding_lanes_latch();
		CHECK(!vehicle_riding_holds_lane(50) && vehicle_riding_holds_lane(70), "the next tick's decks not taken");
		return 0;
	}
	CHECK(FALSE, "no case %s", name);
	return 0;
}
