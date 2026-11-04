/*
BSP_SWITCH_TEST.C

debug.bsp_switch_test: every that many seconds of a campaign level (of
its game time: a run with debug.frame_time switches at the same ticks), the
game switches to the scenario's next structure bsp (main_switch_structure_bsp,
as the level's scripts do at a load zone), and the first player is put on
the first cutscene flag inside it (hs_object_teleport), so that the frames
after the switch draw the new bsp as a player crossing into it would see it.
Each switch is logged with its frames' times: the frame the switch was in,
the longest of the frames after it (BSP_SWITCH_TEST_WATCH_SECONDS), and the
longest and the median of the frames before it, on SDL's clock, so that a
switch's hitch can be read against the level's ordinary frames:

	bsp switch test: 0 -> 1: switch frame 412.3 ms, after 18.1 ms (240 frames;
	the first 18.1, 8.4, 8.3 ms), before 8.4 ms (median 8.3 ms)

Called from the main loop every frame (network_test_update, network_test.c).
*/

#include "cseries.h"
#include "main/main.h"
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "hs/hs_library_external.h"

#include <stdlib.h>

/* the platform layer's (port/linux/src/port_config.c) */
double config_real(char const *name);
void platform_log(char const *format, ...);
unsigned long long SDL_GetTicksNS(void);

/* the frames after a switch watched, and the frames before it kept */
#define BSP_SWITCH_TEST_WATCH_SECONDS 3.0
#define BSP_SWITCH_TEST_FRAMES 1024

static struct
{
	unsigned long long last_frame_ns;
	long next_switch_tick;
	unsigned long long watch_until_ns;
	/* the frames' times since the last switch's watch ended, in ms */
	float before[BSP_SWITCH_TEST_FRAMES];
	long before_count;
	short from, to;
	boolean requested, watching;
	float switch_frame_ms, after_most_ms;
	/* the first frames after the switch's */
	float after_first_ms[3];
	long after_count;
} bsp_switch_test = { 0, NONE };

static int bsp_switch_test_compare(
	void const *a,
	void const *b)
{
	float x = *(float const *)a, y = *(float const *)b;

	return x < y ? -1 : x > y;
}

/* the first player onto the first cutscene flag inside the current bsp
(a cluster of it holds the flag), as a level's scripts put the player */
static void bsp_switch_test_place(
	void)
{
	struct scenario *scenario = global_scenario_get();
	long player_index = local_player_get_player_index(0);
	struct player_datum *player;
	short flag_index;

	if (player_index == NONE)
		return;
	player = player_get(player_index);
	if (player->unit_index == NONE)
		return;
	for (flag_index = 0; flag_index < scenario->cutscene_flags.count; flag_index++)
	{
		struct scenario_cutscene_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->cutscene_flags, flag_index,
			struct scenario_cutscene_flag);
		struct location location;

		scenario_location_from_point(&location, &flag->position);
		if (location.cluster_index != NONE)
		{
			hs_object_teleport(player->unit_index, flag_index);
			platform_log("bsp switch test: the player put on cutscene flag %s", flag->name);
			return;
		}
	}
	platform_log("bsp switch test: no cutscene flag in structure bsp %d", global_structure_bsp_index_get());
}

void bsp_switch_test_update(
	boolean main_menu_loaded)
{
	double seconds = config_real("debug.bsp_switch_test");
	unsigned long long now;
	float frame_ms;
	struct scenario *scenario;

	if (seconds <= 0.0)
		return;
	now = SDL_GetTicksNS();
	frame_ms = bsp_switch_test.last_frame_ns ? (float)(now - bsp_switch_test.last_frame_ns) / 1e6f : 0.0f;
	bsp_switch_test.last_frame_ns = now;
	scenario = main_menu_loaded || !game_in_progress() ? NULL : global_scenario_get();
	if (!scenario || scenario->structure_bsp_references.count < 2 || global_structure_bsp_index_get() == NONE)
	{
		bsp_switch_test.next_switch_tick = NONE;
		bsp_switch_test.before_count = 0;
		bsp_switch_test.requested = bsp_switch_test.watching = FALSE;
		return;
	}
	if (bsp_switch_test.next_switch_tick == NONE)
		bsp_switch_test.next_switch_tick = game_time_get() + (long)(seconds * TICKS_PER_SECOND);

	if (bsp_switch_test.requested)
	{
		/* (the switch is made at the next frame's start: main_loop) */
		if (global_structure_bsp_index_get() != bsp_switch_test.to)
			return;
		bsp_switch_test.requested = FALSE;
		bsp_switch_test.watching = TRUE;
		bsp_switch_test.switch_frame_ms = frame_ms;
		bsp_switch_test.after_most_ms = 0.0f;
		bsp_switch_test.after_count = 0;
		bsp_switch_test.watch_until_ns = now + (unsigned long long)(BSP_SWITCH_TEST_WATCH_SECONDS * 1e9);
		bsp_switch_test_place();
		return;
	}
	if (bsp_switch_test.watching)
	{
		float median = 0.0f, most = 0.0f;

		if (frame_ms > bsp_switch_test.after_most_ms)
			bsp_switch_test.after_most_ms = frame_ms;
		if (bsp_switch_test.after_count < NUMBEROF(bsp_switch_test.after_first_ms))
			bsp_switch_test.after_first_ms[bsp_switch_test.after_count] = frame_ms;
		bsp_switch_test.after_count++;
		if (now < bsp_switch_test.watch_until_ns)
			return;
		bsp_switch_test.watching = FALSE;
		if (bsp_switch_test.before_count)
		{
			qsort(bsp_switch_test.before, (size_t)bsp_switch_test.before_count, sizeof(float),
				bsp_switch_test_compare);
			median = bsp_switch_test.before[bsp_switch_test.before_count / 2];
			most = bsp_switch_test.before[bsp_switch_test.before_count - 1];
		}
		platform_log("bsp switch test: %d -> %d: switch frame %.1f ms, after %.1f ms (%ld frames; the first %.1f, "
			"%.1f, %.1f ms), before %.1f ms (median %.1f ms)",
			bsp_switch_test.from, bsp_switch_test.to, bsp_switch_test.switch_frame_ms,
			bsp_switch_test.after_most_ms, bsp_switch_test.after_count, bsp_switch_test.after_first_ms[0],
			bsp_switch_test.after_first_ms[1], bsp_switch_test.after_first_ms[2], most, median);
		bsp_switch_test.before_count = 0;
		return;
	}
	if (frame_ms > 0.0f && bsp_switch_test.before_count < BSP_SWITCH_TEST_FRAMES)
		bsp_switch_test.before[bsp_switch_test.before_count++] = frame_ms;
	if (game_time_get() < bsp_switch_test.next_switch_tick)
		return;
	bsp_switch_test.next_switch_tick = game_time_get() + (long)(seconds * TICKS_PER_SECOND);
	bsp_switch_test.from = global_structure_bsp_index_get();
	bsp_switch_test.to = (short)((bsp_switch_test.from + 1) % scenario->structure_bsp_references.count);
	bsp_switch_test.requested = TRUE;
	platform_log("bsp switch test: switching from structure bsp %d to %d", bsp_switch_test.from,
		bsp_switch_test.to);
	main_switch_structure_bsp(bsp_switch_test.to);
}
