/*
MENU_SPARTAN_FIDGETS.C (test)

The menus' Spartans' fidgets and their own looks (port/linux/game/
menu_spartan.c): when each place fidgets on the menus' clock (its own
times, stable, never two starting together), each place's turn, nearness
and idle time (stable, within their bounds), and a fidget's blend from the
idle and back (no snap). test_menu_spartan_fidgets.py takes the code.
*/

#include "harness.h"

#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

enum { MAXIMUM_MENU_SPARTANS = 12, PLACES = 8 };

typedef struct { real i, j, k, w; } real_quaternion;
typedef struct
{
	real_quaternion rotation;
	real_point3d translation;
	real scale;
} real_orientation;

/* (real_math.c's, as a straight blend: enough to tell from from to) */
static void orientations_interpolate(real_orientation const *a, real_orientation const *b, real t,
	real_orientation *result)
{
	result->rotation.i = a->rotation.i + (b->rotation.i - a->rotation.i) * t;
	result->rotation.j = a->rotation.j + (b->rotation.j - a->rotation.j) * t;
	result->rotation.k = a->rotation.k + (b->rotation.k - a->rotation.k) * t;
	result->rotation.w = a->rotation.w + (b->rotation.w - a->rotation.w) * t;
	result->translation.x = a->translation.x + (b->translation.x - a->translation.x) * t;
	result->translation.y = a->translation.y + (b->translation.y - a->translation.y) * t;
	result->translation.z = a->translation.z + (b->translation.z - a->translation.z) * t;
	result->scale = a->scale + (b->scale - a->scale) * t;
}

#include "under_test.inc"

/* ---------- the cases */

#define HOUR 3600000UL
#define STEP 16UL

static struct spartan_schedule schedule, other;

/* every start of every place over an hour of 16 ms frames */
static long starts[MAXIMUM_MENU_SPARTANS][400];
static short kinds[MAXIMUM_MENU_SPARTANS][400];
static long start_counts[MAXIMUM_MENU_SPARTANS];

static void schedule_case(void)
{
	unsigned long at, first_next[MAXIMUM_MENU_SPARTANS];
	long all[MAXIMUM_MENU_SPARTANS * 400];
	long all_count = 0;
	short place, kind;
	long count, a, b;

	memset(&schedule, 0, sizeof(schedule));
	spartan_schedule_advance(&schedule, 0);
	/* (each place's first its own) */
	for (place = 0; place < MAXIMUM_MENU_SPARTANS; place++)
	{
		first_next[place] = schedule.next[place];
		CHECK(first_next[place] >= SPARTAN_FIDGET_FIRST_MINIMUM && first_next[place] <= SPARTAN_FIDGET_FIRST_MAXIMUM,
			"place %d's first at %lu", place, first_next[place]);
		for (a = 0; a < place; a++)
			CHECK(first_next[a] != first_next[place], "places %ld and %d first fidget at once (%lu)", a, place,
				first_next[place]);
	}
	for (at = 0; at <= HOUR; at += STEP)
	{
		spartan_schedule_advance(&schedule, at);
		for (place = 0; place < MAXIMUM_MENU_SPARTANS; place++)
		{
			count = schedule.count[place];
			if (count > start_counts[place])
			{
				CHECK(count == start_counts[place] + 1, "place %d started %ld at once", place,
					count - start_counts[place]);
				CHECK(count <= 400, "place %d too often", place);
				starts[place][count - 1] = (long)schedule.start[place];
				kinds[place][count - 1] = schedule.kind[place];
				all[all_count++] = (long)schedule.start[place];
				start_counts[place] = count;
			}
		}
	}
	for (place = 0; place < MAXIMUM_MENU_SPARTANS; place++)
	{
		boolean used[NUMBER_OF_SPARTAN_FIDGETS] = { 0 };

		/* (every 10 to 20 s, a little later at most where put off) */
		CHECK(start_counts[place] >= (long)(HOUR / SPARTAN_FIDGET_EVERY_MAXIMUM) - 2 &&
			start_counts[place] <= (long)(HOUR / SPARTAN_FIDGET_EVERY_MINIMUM) + 1,
			"place %d fidgeted %ld times in an hour", place, start_counts[place]);
		for (count = 1; count < start_counts[place]; count++)
		{
			long gap = starts[place][count] - starts[place][count - 1];

			CHECK(gap >= (long)SPARTAN_FIDGET_EVERY_MINIMUM &&
				gap <= (long)(SPARTAN_FIDGET_EVERY_MAXIMUM + 4 * SPARTAN_FIDGET_APART),
				"place %d's fidgets %ld ms apart", place, gap);
			CHECK(kinds[place][count] != kinds[place][count - 1], "place %d did %d twice running", place,
				kinds[place][count]);
		}
		for (count = 0; count < start_counts[place]; count++)
			used[kinds[place][count]] = TRUE;
		for (kind = 0; kind < NUMBER_OF_SPARTAN_FIDGETS; kind++)
			CHECK(used[kind], "place %d never did %d", place, kind);
	}
	/* (no two places' starting together: never in step) */
	for (a = 0; a < all_count; a++)
	{
		for (b = a + 1; b < all_count; b++)
		{
			long apart = all[a] > all[b] ? all[a] - all[b] : all[b] - all[a];

			CHECK(apart >= (long)SPARTAN_FIDGET_APART, "two fidgets start %ld ms apart (at %ld)", apart, all[a]);
		}
	}
	/* (and places' gaps their own: no two places' first five the same) */
	for (a = 0; a < MAXIMUM_MENU_SPARTANS; a++)
	{
		for (b = a + 1; b < MAXIMUM_MENU_SPARTANS; b++)
		{
			boolean same = TRUE;

			for (count = 1; count < 5; count++)
				same &= starts[a][count] - starts[a][count - 1] == starts[b][count] - starts[b][count - 1];
			CHECK(!same, "places %ld and %ld fidget in step", a, b);
		}
	}
	/* (stable: asked in big jumps, or again from the start as the clock
	goes back, the same as asked every frame) */
	memset(&other, 0, sizeof(other));
	for (at = 0; at <= HOUR; at += 7777UL)
		spartan_schedule_advance(&other, at);
	spartan_schedule_advance(&other, HOUR);
	for (place = 0; place < MAXIMUM_MENU_SPARTANS; place++)
	{
		CHECK(other.start[place] == schedule.start[place] && other.kind[place] == schedule.kind[place] &&
			other.count[place] == schedule.count[place] && other.next[place] == schedule.next[place],
			"place %d asked in jumps: %lu %d, every frame: %lu %d", place, other.start[place], other.kind[place],
			schedule.start[place], schedule.kind[place]);
	}
	spartan_schedule_advance(&other, 5000UL);
	spartan_schedule_advance(&other, HOUR);
	for (place = 0; place < MAXIMUM_MENU_SPARTANS; place++)
		CHECK(other.start[place] == schedule.start[place] && other.kind[place] == schedule.kind[place],
			"place %d after the clock went back", place);
}

static void variation_case(void)
{
	struct spartan_variation lineup[PLACES], again, single;
	boolean left = FALSE, right = FALSE;
	real nearest = -2.0f, furthest = 2.0f;
	short place, other_place;

	for (place = 0; place < PLACES; place++)
	{
		spartan_variation_find(place, TRUE, &lineup[place]);
		/* (stable frame to frame) */
		spartan_variation_find(place, TRUE, &again);
		CHECK(!memcmp(&again, &lineup[place], sizeof(again)), "place %d's look changed", place);
		/* (bounded: a few degrees, a few parts nearer or further) */
		CHECK(fabsf(lineup[place].turn_degrees) >= SPARTAN_LINEUP_TURN_MINIMUM - 0.001f &&
			fabsf(lineup[place].turn_degrees) <= SPARTAN_LINEUP_TURN_MAXIMUM + 0.001f,
			"place %d turned %.2f degrees", place, lineup[place].turn_degrees);
		CHECK(lineup[place].nearer >= -1.0f && lineup[place].nearer <= 1.0f, "place %d nearer %.2f", place,
			lineup[place].nearer);
		CHECK(lineup[place].idle_phase < SPARTAN_IDLE_PHASES, "place %d's idle at %lu", place,
			lineup[place].idle_phase);
		left |= lineup[place].turn_degrees < 0.0f;
		right |= lineup[place].turn_degrees > 0.0f;
		nearest = MAX(nearest, lineup[place].nearer);
		furthest = MIN(furthest, lineup[place].nearer);
		for (other_place = 0; other_place < place; other_place++)
		{
			CHECK(lineup[other_place].turn_degrees != lineup[place].turn_degrees,
				"places %d and %d turned alike", other_place, place);
			CHECK(lineup[other_place].idle_phase != lineup[place].idle_phase, "places %d and %d idle in step",
				other_place, place);
		}
		/* (one alone hardly turned, where it was asked) */
		spartan_variation_find(place, FALSE, &single);
		CHECK(fabsf(single.turn_degrees) <= SPARTAN_SINGLE_TURN_MAXIMUM + 0.001f && single.nearer == 0.0f,
			"place %d alone turned %.2f, nearer %.2f", place, single.turn_degrees, single.nearer);
	}
	CHECK(left && right, "a lineup all turned one way");
	CHECK(nearest - furthest > 0.5f, "a lineup all as near (%.2f to %.2f)", furthest, nearest);
}

static void blend_case(void)
{
	static short const frame_counts[NUMBER_OF_SPARTAN_FIDGETS] = { 71, 27, 0, 0 };
	short kind;

	for (kind = 0; kind < NUMBER_OF_SPARTAN_FIDGETS; kind++)
	{
		unsigned long length = spartan_fidget_length(kind, frame_counts[kind]);
		struct spartan_fidget_pose pose, before;
		real most = 0.0f, least_frame = 1000.0f, most_look = 0.0f, least_look = 0.0f;
		unsigned long elapsed;

		CHECK(length > 1000UL && length < 10000UL, "fidget %d %lu ms long", kind, length);
		CHECK(spartan_fidget_progress(kind, 0, frame_counts[kind], &before), "fidget %d not begun", kind);
		CHECK(before.weight == 0.0f, "fidget %d begins at %.2f, not from the idle", kind, before.weight);
		for (elapsed = 1; elapsed < length; elapsed++)
		{
			CHECK(spartan_fidget_progress(kind, elapsed, frame_counts[kind], &pose), "fidget %d over at %lu", kind,
				elapsed);
			/* (no snap: a millisecond moves it a little) */
			CHECK(fabsf(pose.weight - before.weight) < 0.01f, "fidget %d's weight jumps %.3f at %lu", kind,
				pose.weight - before.weight, elapsed);
			CHECK(fabsf(pose.frame - before.frame) < 0.05f, "fidget %d's frame jumps %.3f at %lu", kind,
				pose.frame - before.frame, elapsed);
			CHECK(fabsf(pose.look - before.look) < 0.01f, "fidget %d's look jumps at %lu", kind, elapsed);
			CHECK(pose.weight >= 0.0f && pose.weight <= 1.0f, "fidget %d's weight %.2f", kind, pose.weight);
			most = MAX(most, pose.weight);
			least_frame = MIN(least_frame, pose.frame);
			most_look = MAX(most_look, pose.look);
			least_look = MIN(least_look, pose.look);
			before = pose;
		}
		CHECK(before.weight < 0.01f, "fidget %d ends at %.2f, not back to the idle", kind, before.weight);
		CHECK(most > 0.999f, "fidget %d never wholly drawn (%.2f)", kind, most);
		CHECK(!spartan_fidget_progress(kind, length, frame_counts[kind], &pose) && pose.weight == 0.0f,
			"fidget %d not over at its end", kind);
		if (kind == SPARTAN_FIDGET_WEAPON_CHECK)
		{
			spartan_fidget_progress(kind, 0, frame_counts[kind], &pose);
			CHECK(pose.frame == frame_counts[kind] - 1 && least_frame == 0.0f && before.frame > frame_counts[kind] - 1.5f,
				"a weapon check from %.1f to %.1f to %.1f, not the ready backwards and forwards", pose.frame,
				least_frame, before.frame);
		}
		if (kind == SPARTAN_FIDGET_LOOK)
			CHECK(most_look > 0.99f && least_look < -0.99f && fabsf(before.look) < 0.01f,
				"a look around to %.2f and %.2f, ending at %.2f", most_look, least_look, before.look);
	}
	/* (the blend: none of the fidget's, all of it, half way) */
	{
		real_orientation from[2], to[2], blended[2];
		short node;

		memset(from, 0, sizeof(from));
		memset(to, 0, sizeof(to));
		for (node = 0; node < 2; node++)
		{
			from[node].rotation.w = 1.0f;
			to[node].rotation.k = 1.0f;
			to[node].translation.z = -0.2f;
			from[node].scale = to[node].scale = 1.0f;
		}
		memcpy(blended, to, sizeof(to));
		spartan_orientations_blend(2, from, blended, 0.0f);
		CHECK(!memcmp(blended, from, sizeof(from)), "weight 0 not the idle's");
		memcpy(blended, to, sizeof(to));
		spartan_orientations_blend(2, from, blended, 1.0f);
		CHECK(!memcmp(blended, to, sizeof(to)), "weight 1 not the fidget's");
		memcpy(blended, to, sizeof(to));
		spartan_orientations_blend(2, from, blended, 0.5f);
		CHECK(fabsf(blended[1].translation.z + 0.1f) < 0.001f, "weight 0.5 at %.3f", blended[1].translation.z);
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	if (!strcmp(case_name, "schedule"))
		schedule_case();
	else if (!strcmp(case_name, "variation"))
		variation_case();
	else if (!strcmp(case_name, "blend"))
		blend_case();
	else
	{
		fprintf(stderr, "unknown case: %s\n", case_name);
		return 2;
	}
	printf("%s: ok\n", case_name);
	return 0;
}
