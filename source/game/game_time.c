/*
GAME_TIME.C

symbols in this file:
000A4D00 0020:
	_game_time_initialized (0000)
000A4D20 0030:
	_game_time_initialize (0000)
000A4D50 0050:
	_game_time_initialize_for_new_map (0000)
000A4DA0 0020:
	_game_time_dispose_from_old_map (0000)
000A4DC0 0010:
	_game_time_dispose (0000)
000A4DD0 0040:
	_game_time_end (0000)
000A4E10 0040:
	_game_time_get (0000)
000A4E50 0040:
	_game_time_get_elapsed (0000)
000A4E90 0040:
	_local_time_get (0000)
000A4ED0 0040:
	_local_time_get_elapsed (0000)
000A4F10 0040:
	_game_predicting (0000)
000A4F50 0050:
	_game_in_progress (0000)
000A4FA0 0040:
	_game_time_get_paused (0000)
000A4FE0 0050:
	_game_time_set_paused (0000)
000A5030 0040:
	_game_time_get_speed (0000)
000A5070 0050:
	_game_time_set_speed (0000)
000A50C0 0010:
	_game_time_statistics_new (0000)
000A50D0 01e0:
	_game_time_statistics_frame (0000)
000A52B0 00e0:
	_game_time_start (0000)
000A5390 0360:
	_game_time_update (0000)
0025C480 0012:
	??_C@_0BC@ENHAGKAA@game?5time?5globals?$AA@ (0000)
0025C494 0035:
	??_C@_0DF@KKMNPDPK@game_time_globals?5?$CG?$CG?5?$CBgame_time_@ (0000)
0025C4CC 0020:
	??_C@_0CA@INLHHDGN@c?3?2halo?2SOURCE?2game?2game_time?4c?$AA@ (0000)
0025C4EC 0012:
	??_C@_0BC@NOJBGBLH@game_time_globals?$AA@ (0000)
0025C500 0034:
	??_C@_0DE@PPGKJIJK@game_time_globals?5?$CG?$CG?5game_time_g@ (0000)
0025C534 001b:
	??_C@_0BL@DECMFGMD@?$CBgame_time_globals?9?$DOactive?$AA@ (0000)
0025C550 0021:
	??_C@_0CB@OIFBECDL@ticks_elapsed?5?$DM?$DN?5maximum_actions@ (0000)
0025C578 004c:
	??_C@_0EM@FOLKNDLG@game_time_globals?9?$DOleftover_dt?$DO?$DN@ (0000)
0025C5C8 004c:
	??_C@_0EM@FBBKPKLD@update?5server?5is?5too?5far?5ahead?5o@ (0000)
0043EE04 002c:
	_bss_0043ee04 (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "real_math.h"
#include "game.h"
#include "player_queues_new.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager.h"
#include "saved games/game_state.h"
#include "scenario/scenario.h" /* port: game_time_menu_scene */
#include "scenario/scenario_definitions.h"
#include "profile_sections.h" /* port: port/linux/include/profile_sections.h */
#include "sim_rate.h" /* port: port/linux/game/sim_rate.c */
#include "network_standard.h" /* port: network_standard_agreed_rate (port/linux/game/network_standard.c) */
#include "mcc_game_speed.h" /* port: port/linux/game/mcc_game_speed.c */
/* port/linux/game/network_distributed.c's */
void network_distributed_tick(void);
/* port: port/linux/game/network_clock.c */
real network_clock_frame_seconds(real time_delta_sec);
/* port: port/linux/game/determinism_log.c */
void determinism_log_tick_begin(void);
void determinism_log_tick_end(long tick);
void determinism_log_tick_pause(void);
#include "demo.h" /* port: port/linux/game/demo.c */

/* port: the tick loop's steps, timed in the profiling build */
PROFILE_SECTION(game_time_network_distributed_tick_section, "network_distributed_tick")
PROFILE_SECTION(game_time_render_interpolation_tick_section, "render_interpolation_tick")
PROFILE_SECTION(game_time_game_frame_section, "game_frame")

/* ---------- constants */

enum
{
	SOME_LARGE_NUMBER_OF_TICKS = 1000, // 0x03E8
};

/* ---------- macros */

/* ---------- structures */

struct game_time_globals_struct
{
	boolean initialized;
	boolean active;
	boolean paused;
	/* (these three unused: kept for the game state's layout) */
	short monitor_state;
	short monitor_counter;
	short monitor_latency;
	long local_time;
	short last_local_time_elapsed;
	long server_time;
	real speed;
	real leftover_dt;
};

/* ---------- prototypes */

static boolean game_time_run_substep(void);

/* ---------- globals */

static struct game_time_globals_struct *game_time_globals;
/* port: a reliable host update's time, not part of the saved world. */
static long game_time_distributed_target = NONE;

/* port: game.sim_rate's sub-steps spread over the frames (game_time_update;
port/linux/game/sim_rate.h), outside the game state: a game is saved
between two ticks, never with one open */
static struct
{
	/* sub-steps run before their time (game_time_finish_tick), taken from the
	clock's next, and the ticks they ended, the next update's elapsed ones */
	long substeps_ahead;
	short ticks_ended_ahead;
	/* what the frames do to the game as a whole waits for the open tick's end
	(game_time_tick_open_wait): the clock begins no other until it has run,
	and the frames that have waited since a sub-step last ran */
	boolean boundary_wanted;
	short boundary_waits;
} game_time_substeps;

/* the frames in a row that run no sub-step while the frames' work waits
for a tick's end, before the tick is ended at once: a clock that runs a
sub-step every 8 ms runs one within a frame or a few, one that has stopped
(paused, the console, a menu over the game) never does */
#define GAME_TIME_MAXIMUM_BOUNDARY_WAITS 8

/* ---------- public code */

/* port: a tick left open by the clock given up, and what the clock owes
(a map begun or ended, a game state loaded over the game: the state is one
saved between two ticks) */
void game_time_port_drop_substeps(
	void)
{
	/* (not from inside a sub-step, which ends as it began) */
	if (sim_substep_index() != NONE)
		return;
	sim_rate_tick_drop();
	csmemset(&game_time_substeps, 0, sizeof(game_time_substeps));

	return;
}

boolean game_time_initialized(
	void)
{
	return (game_time_globals && game_time_globals->initialized);
}

void game_time_initialize(
	void)
{
	game_time_globals = (struct game_time_globals_struct *)game_state_malloc("game time globals", NULL, sizeof(*game_time_globals));
	memset(game_time_globals, 0, sizeof(*game_time_globals));
	game_time_distributed_target = NONE; /* port: no previous session's target */

	return;
}

void game_time_initialize_for_new_map(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 131, game_time_globals && !game_time_globals->initialized);
	memset(game_time_globals, 0, sizeof(*game_time_globals));
	game_time_globals->initialized = TRUE;
	game_time_port_drop_substeps();
	game_time_distributed_target = NONE; /* port: no previous map's target */

	return;
}

void game_time_dispose_from_old_map(
	void)
{
	game_time_distributed_target = NONE; /* port: discard the departing map's target */
	if (game_time_globals)
	{
		game_time_globals->initialized = FALSE;
		game_time_globals->active = FALSE;
	}
	game_time_port_drop_substeps();

	return;
}

void game_time_dispose(
	void)
{
	return;
}

void game_time_end(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 196, game_time_globals);
	game_time_globals->active = FALSE;
	game_time_distributed_target = NONE; /* port: discard the finished game's target */

	return;
}

void game_time_set_distributed(
	long time)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 0, game_time_globals && game_time_globals->initialized);

	game_time_globals->local_time = time;
	game_time_globals->server_time = time;
	game_time_globals->leftover_dt = 0.f;
	game_time_port_drop_substeps();
	game_time_distributed_target = NONE; /* port: a new clock invalidates its target */

	return;
}

/* port: an OpenH1 game's client's clock moved on so many ticks between two,
none of them run (port/linux/game/network_clock.c: its ticks lead the
host's by its trip there); never back, which the host would take for old
messages */
void game_time_port_advance(
	long ticks)
{
	if (!game_time_globals || !game_time_globals->initialized || ticks <= 0 || sim_substep_next())
		return;
	game_time_globals->local_time += ticks;
	game_time_globals->server_time += ticks;

	return;
}

/* port: remember only validated reliable host time. Small stalls used to
leave clients behind indefinitely; game_time_update runs the missing ticks. */
void game_time_synchronize_distributed(
	long time)
{
	if (!game_time_globals || !game_time_globals->initialized ||
		!game_time_globals->active || !network_game_distributed_client() ||
		game_time_menu_scene() || time < 0 || time > 0x3FFFFFFF)
		return;
	if (time > game_time_globals->local_time && time > game_time_distributed_target)
		game_time_distributed_target = time;

	return;
}

long game_time_get(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 463, game_time_globals && game_time_globals->initialized);

	return game_time_globals->local_time;
}

short game_time_get_elapsed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 471, game_time_globals && game_time_globals->initialized);

	return game_time_globals->last_local_time_elapsed;
}

long local_time_get(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 479, game_time_globals && game_time_globals->initialized);

	return game_time_globals->local_time;
}

short local_time_get_elapsed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 487, game_time_globals && game_time_globals->initialized);

	return game_time_globals->last_local_time_elapsed;
}

boolean game_predicting(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 495, game_time_globals && game_time_globals->initialized);

	return FALSE;
}

boolean game_in_progress(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 505, game_time_globals);

	if (game_time_globals->initialized)
	{
		if (game_time_globals->active)
		{
			return TRUE;
		}

		/* bug? */
		if (game_time_globals->paused)
		{
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}

	return FALSE;
}

boolean game_time_get_paused(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 533, game_time_globals);

	return game_time_globals->paused;
}

void game_time_set_paused(
	boolean paused)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 541, game_time_globals);

	if (game_time_globals->initialized)
	{
		game_time_globals->active = !paused;
	}

	game_time_globals->paused = paused;

	return;
}

/* how far the clock has run into the next tick, 0 to 1: the native ports
draw frames between ticks (port/linux/game/render_interpolation.c) */
real game_time_get_tick_fraction(
	void)
{
	real fraction;

	if (!game_time_globals || !game_time_globals->active || game_time_globals->paused)
		return 1.0f;
	fraction = game_time_globals->leftover_dt * game_time_globals->speed * TICKS_PER_SECOND;
	/* port: MCC clients interpolate using only the host's approved speed. */
	if (network_game_distributed_client())
		fraction = game_time_globals->leftover_dt * mcc_game_speed_client(game_time_globals->speed) * TICKS_PER_SECOND;
	/* port: and the sub-steps of the open tick that have run (a frame drawn
	from the last of them on: sim_substep_blend), less those run before their
	time (game_time_finish_tick) */
	if (sim_substep_next() || game_time_substeps.substeps_ahead)
	{
		fraction += (real)(sim_substep_next() - game_time_substeps.substeps_ahead) /
			(real)sim_substep_count();
	}
	return PIN(fraction, 0.0f, 1.0f);
}

/* port: how long ago, in ticks, the clock's newest step (a tick at
game.sim_rate 30, a sub-step at 60 and 120) fell due: the time it has run
past it, which may be more than a step while a step waits (the frame's work
waiting for a tick's end: game_time_tick_open_wait), less the steps run
before their time (game_time_finish_tick), when it is less than none. 0
while the clock does not run. What is drawn from the newest step is carried
on by this much (port/linux/game/render_interpolation.c) */
real game_time_get_step_age(
	void)
{
	real age;

	if (!game_time_globals || !game_time_globals->active || game_time_globals->paused)
		return 0.0f;
	age = game_time_globals->leftover_dt * game_time_globals->speed * TICKS_PER_SECOND;
	if (game_time_substeps.substeps_ahead)
		age -= (real)game_time_substeps.substeps_ahead / (real)sim_substep_count();
	return age;
}

/* port: the seconds of the frames' time until the clock's next step (a
tick at game.sim_rate 30, a sub-step at 60 and 120) falls due, 0 when one is
due already; -1 while the clock does not run. A run that shows nothing waits
this long between frames (port/linux/game/unseen_frames.c) */
real game_time_seconds_to_next_step(
	void)
{
	real steps_per_second;
	real step_seconds;

	if (!game_time_globals || !game_time_globals->initialized || !game_time_globals->active ||
		game_time_globals->paused)
	{
		return -1.0f;
	}
	steps_per_second = game_time_globals->speed*(TICKS_PER_SECOND*(short)(sim_rate_next()/SIM_RATE_CLASSIC));
	if (steps_per_second <= 0.f)
		return -1.0f;
	step_seconds = 1.0f/steps_per_second;
	return PIN(step_seconds - game_time_globals->leftover_dt, 0.0f, step_seconds);
}

/* port: whether what the frames do to the game as a whole (save it, load a
saved game, revert, change the map or its structure: main.c's work at a
frame's start) must wait, a tick being open (its sub-steps spread over the
frames: game_time_update). The clock then begins no other tick until the
open one has run to its end; one that has stopped ends it at once, as the
frames would wait for it for good. */
boolean game_time_tick_open_wait(
	void)
{
	if (!sim_substep_next())
		return FALSE;
	if (game_time_globals && game_time_globals->initialized && game_time_globals->active &&
		!game_time_globals->paused && game_time_globals->speed > 0.0f &&
		game_time_substeps.boundary_waits < GAME_TIME_MAXIMUM_BOUNDARY_WAITS)
	{
		game_time_substeps.boundary_wanted = TRUE;
		game_time_substeps.boundary_waits++;
		return TRUE;
	}
	game_time_finish_tick();
	return FALSE;
}

/* port: the open tick run to its end now, before its sub-steps' time (the
clock takes them from its next); the frames' input is the latest read */
void game_time_finish_tick(
	void)
{
	if (!game_time_globals || !game_time_globals->initialized)
		return;
	while (sim_substep_next())
	{
		game_time_substeps.substeps_ahead++;
		if (game_time_run_substep())
			game_time_substeps.ticks_ended_ahead++;
	}
	game_time_substeps.boundary_wanted = FALSE;
	game_time_substeps.boundary_waits = 0;

	return;
}

real game_time_get_speed(
	void)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 555, game_time_globals);

	/* port: a client of another's game runs at the host's speed (its own
	set before it joined too: cheats_network_client_enforce) */
	if (network_game_distributed_client())
		return mcc_game_speed_client(1.0f); /* port: legacy clients remain at one */
	return game_time_globals->speed;
}

void game_time_set_speed(
	real speed)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 562, game_time_globals);

	/* port: a speed (a map's script's, game_speed) that is a number, not
	negative and not past a hundred: a NaN or an infinity stopped the game's
	time for good */
	if (!(speed >= 0.0f && speed <= 100.0f))
		return;
	game_time_globals->speed = speed;

	return;
}

/* port: the game's own speed put back; whether it was another
(cheats_network_client_enforce) */
boolean game_time_reset_speed(
	void)
{
	boolean changed;

	if (!game_time_globals)
		return FALSE;
	changed = game_time_globals->speed != 1.0f;
	game_time_globals->speed = 1.0f;

	return changed;
}

/* whether a client's clock waits for the host's first game update, which
brings the host's time (the host ticks only once every machine has
loaded) */
boolean game_time_held(
	void)
{
	struct network_game_client *client;

	if (game_connection() != _game_connection_network_client)
		return FALSE;
	client = global_network_game_client_get();
	return client && !network_game_client_server_has_started_game(client);
}

/* port: whether the main menu's scene is what ticks. It runs on this
machine's clock and takes no player input: a tick waits for the players'
input, an action for each, and the menus change who the local players are
(profiles), and a lobby's network game owns the queues, so with none the
ticks stopped and the scene froze behind the menus (players.c gives its
players idle actions) */
boolean game_time_menu_scene(
	void)
{
	return global_scenario && global_scenario->type == _scenario_type_main_menu;
}

void game_time_start(
	void)
{
	short connection;

	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 162, game_time_globals && game_time_globals->initialized);
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 163, !game_time_globals->active);

	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 562, game_time_globals);

	game_time_globals->speed = 1.f;
	game_time_globals->leftover_dt = 0;
	game_time_globals->active = TRUE;
	game_time_port_drop_substeps();

	connection = game_connection();

	switch (connection)
	{
	case _game_connection_local:
	case _game_connection_network_server:
	{
		update_server_start();
		break;
	}
	case _game_connection_network_client:
	case _game_connection_film_playback:
	{
		update_client_start();
		break;
	}
	}

	return;
}

void game_time_update(
	real time_delta_sec)
{
	match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 205, game_time_globals);
	/* port: MCC speed updates must continue when simulation is stopped. */
	mcc_game_speed_frame();

	/* port: the frame's input read (the main loop's player_control_update,
	before this) */
	sim_rate_frame_begin();
	if (game_time_globals->active)
	{
		long connection;
		long steps_elapsed;
		/* port: the clock counts the sub-steps of game.sim_rate
		(port/linux/game/sim_rate.h): 1, 2 or 4 a tick, the open tick's, else
		the next's. At 30 they are the ticks themselves. Each runs when its
		time has come (game_time_run_substep), so a tick's are spread over the
		frames that pass meanwhile, each with the input its frame read: at 120
		on a display of 120 Hz, one a frame. The tick's own work runs with its
		first sub-step, and what follows its fast phase with its last. */
		short substep_count = (short)(sim_rate_next() / SIM_RATE_CLASSIC);
		real steps_per_second = game_time_globals->speed*(TICKS_PER_SECOND*substep_count);
		/* port: preserve native client cheat enforcement; use MCC host state separately. */
		if (network_game_distributed_client())
			steps_per_second = mcc_game_speed_client(game_time_globals->speed)*(TICKS_PER_SECOND*substep_count);

		/* The native builds draw several frames per tick
		(port/linux/game/render_interpolation.c). A frame that runs no tick has
		elapsed no game time: without this, the ticks of the last frame that
		ran some would count again on every frame after it, and whatever
		advances by game_time_get_elapsed() once a frame (chapter titles, HUD
		messages and flashes, screen flashes, camera shake and impulses) would
		run as many times too fast as there are frames per tick. On the Xbox
		every frame ran at least one tick. */
		game_time_globals->last_local_time_elapsed = 0;

		if (steps_per_second > 0.f)
		{
			boolean discard_leftover_time;
			real game_time;
			real steps_elapsed_real;

			connection = game_connection();
			switch (connection)
			{
			case _game_connection_film_playback:
				connection = TICKS_PER_SECOND;
				discard_leftover_time = FALSE;
				goto calculate_elapsed_ticks;
			case _game_connection_network_client:
			case _game_connection_network_server:
				/* (the distributed netcode: every machine ticks on its own
				clock, and the host waits for nobody) */
				connection = TICKS_PER_SECOND;
				break;
			case _game_connection_local:
				connection = 7;
				break;
			default:
				connection = 7;
				break;
			}
			discard_leftover_time = TRUE;

		calculate_elapsed_ticks:
			/* (the most ticks a frame runs, in sub-steps) */
			connection *= substep_count;
			game_time = time_delta_sec + game_time_globals->leftover_dt;
			/* port: an OpenH1 game's client's clock run a little faster or
			slower, or part of a tick at once, to lead the host's by its trip
			there (port/linux/game/network_clock.c; none on another machine) */
			game_time += network_clock_frame_seconds(time_delta_sec);
			steps_elapsed_real = (real)floor(game_time*steps_per_second);
			steps_elapsed = (long)(steps_elapsed_real <= (real)SOME_LARGE_NUMBER_OF_TICKS ?
				steps_elapsed_real : (real)SOME_LARGE_NUMBER_OF_TICKS);
			if (steps_elapsed > connection)
			{
				steps_elapsed = connection;
				if (discard_leftover_time)
					game_time = steps_elapsed_real/steps_per_second;
			}

			game_time_globals->leftover_dt = game_time - steps_elapsed_real/steps_per_second;
			if (game_time_globals->leftover_dt < 0.f)
				game_time_globals->leftover_dt = 0.f;
			/* port: debug.fixed_step, a game of this machine alone: one step (a
			tick at 30, a sub-step at 60 and 120) a frame that has time (main.c
			gives it a step's; a 30th of a second in a float is not one tick
			exactly), so that what runs once a frame (aiming) sees the game's
			time; game_speed has no effect */
			if (halo_fixed_step() && game_connection() == _game_connection_local && time_delta_sec > 0.f)
			{
				steps_elapsed = 1;
				game_time_globals->leftover_dt = 0.f;
			}
			match_assert("c:\\halo\\SOURCE\\game\\game_time.c", 306,
				game_time_globals->leftover_dt>=0.f && game_time_globals->leftover_dt<100.f);
			/* port: recover a small clock deficit by simulating, not skipping,
			ticks. Keep the fractional time above and the frame's existing cap;
			at most double its normal work, never extrapolating the host's time
			(a Legacy game's client: an OpenH1 game's paces its clock to the
			host's itself, network_clock.c) */
			if (steps_elapsed > 0 && network_game_distributed_client() && !network_standard_agreed_rate() &&
				!game_time_held() && !game_time_menu_scene() && game_time_globals->local_time >= 0 &&
				game_time_distributed_target > game_time_globals->local_time)
			{
				long steps_behind = (game_time_distributed_target - game_time_globals->local_time)*substep_count -
					sim_substep_next();
				long extra_steps = MIN(steps_elapsed, steps_behind - steps_elapsed);

				if (extra_steps > 0)
					steps_elapsed += MIN(extra_steps, TICKS_PER_SECOND*substep_count - steps_elapsed);
			}
			/* port: the sub-steps already run before their time
			(game_time_finish_tick) are these */
			if (game_time_substeps.substeps_ahead > 0 && steps_elapsed > 0)
			{
				long owed = MIN(steps_elapsed, game_time_substeps.substeps_ahead);

				steps_elapsed -= owed;
				game_time_substeps.substeps_ahead -= owed;
			}
			/* port: at a boundary the frame's work has had its turn
			(game_time_tick_open_wait) */
			if (!sim_substep_next())
				game_time_substeps.boundary_wanted = FALSE;
			/* port: else, while it waits for the boundary, the steps run only
			to the open tick's end, and the time of the rest is the next
			frame's (only then: a frame whose steps crossed every tick's end
			left the next tick's first sub-step a frame late, its time kept
			over, and at 120 and 60 fps the frames ran 1 and 3 sub-steps in
			turn: OpenH1 #706). So
			no tick is begun here, nor its update built (update_client_local_ticks,
			network_game_server_update_ticks, below): a tick's update built and
			its tick not begun stayed queued, and every tick after took the
			update one older, its input with it, until the map was loaded again:
			a frame's wait at each checkpoint, structure switch and death added
			a tick to the delay of every press (OpenH1 #590) */
			else if (game_time_substeps.boundary_wanted && steps_elapsed > substep_count - sim_substep_next())
			{
				long open_steps = substep_count - sim_substep_next();

				game_time_globals->leftover_dt += (real)(steps_elapsed - open_steps)/steps_per_second;
				steps_elapsed = open_steps;
			}

			if (steps_elapsed > 0)
			{
				/* port: the open tick's next sub-step, the ticks these steps
				begin and end (at 30 each step begins and ends one), and those
				that did end */
				short next_substep = sim_substep_next();
				long ticks_begun = (next_substep + steps_elapsed - 1)/substep_count + (next_substep == 0);
				long ticks_ended = (next_substep + steps_elapsed)/substep_count;
				long ticks_ended_run = 0;
				/* (the first tick these steps would begin) */
				long first_server_time = game_time_globals->server_time + (next_substep != 0);
				long final_local_time;
				long maximum_possible_server_time;
				long server_updates = 0;
				long step_index;

				final_local_time = game_time_globals->local_time + (next_substep != 0) + ticks_begun;
				switch (game_connection())
				{
				case _game_connection_local:
					if (ticks_begun > 0)
						update_client_local_ticks((short)ticks_begun);
					break;
				case _game_connection_network_server:
					if (ticks_begun > 0)
						network_game_server_update_ticks(global_network_game_server_get(), (short)ticks_begun);
					break;
				}

				/* (a client of the distributed netcode ticks on its own clock,
				with its own input and the latest the host relayed, from the
				host's first game update, which brings the host's time: the
				host ticks only once every machine has loaded) */
				/* port: the main menu's scene ticks on this machine's clock,
				in a lobby too (game_time_menu_scene) */
				if (game_time_menu_scene())
				{
					maximum_possible_server_time = final_local_time;
				}
				else if (game_connection() == _game_connection_network_client)
				{
					maximum_possible_server_time = game_time_held() ?
						game_time_globals->server_time : final_local_time;
				}
				else
					maximum_possible_server_time = update_client_get_maximum_possible_server_time();
				if (maximum_possible_server_time > first_server_time)
				{
					long final_server_time = MIN(maximum_possible_server_time, final_local_time);

					server_updates = final_server_time - first_server_time;
				}

				/* (the open tick's sub-steps, which need no update, then a tick
				begun for each update, its sub-steps run as far as the steps
				go) */
				for (step_index = 0; step_index < steps_elapsed; step_index++)
				{
					if (!sim_substep_next())
					{
						if (server_updates <= 0)
							break;
						server_updates--;
					}
					if (game_time_run_substep())
						ticks_ended_run++;
					game_time_substeps.boundary_waits = 0;
				}

				/* (none while a client's clock waits: a frame that runs no tick
				reports none; port: with sub-steps, the ticks that ended) */
				game_time_globals->last_local_time_elapsed = game_time_held() ? 0 :
					(short)(substep_count > 1 ? ticks_ended_run : ticks_ended);
			}
		}
		/* port: and those that ended before their time in this frame
		(game_time_finish_tick) */
		game_time_globals->last_local_time_elapsed += game_time_substeps.ticks_ended_ahead;
		game_time_substeps.ticks_ended_ahead = 0;

		/* The effects and the widgets that move every frame (game_frame) hang
		off objects: they read the pose between the last two ticks, as the
		renderer does, and the ticks above have taken their snapshots already
		(port/linux/game/render_interpolation.c). The frames of one tick share
		the fraction, so what they hang off is drawn where they put it. */
		render_interpolation_frame_begin();
		profile_scope(game_time_game_frame_section, game_frame(game_time_get_speed()*time_delta_sec);)
		network_test_elevator_frame(); /* port: debug.network_test_elevator (port/linux/game/network_test.c) */
		render_interpolation_frame_end();
	}
	else
	{
		game_time_globals->last_local_time_elapsed = 0;
	}

	return;
}

/* ---------- private code */

/* port: the next sub-step of game.sim_rate run (port/linux/game/sim_rate.h):
the open tick's, or the first of a new tick, which begins with the tick's own
work; the last ends it, and the tick is counted. Whether it did. */
static boolean game_time_run_substep(
	void)
{
	boolean tick_ended;

	/* port: (debug.determinism_log), and the local seed's takers in the
	tick take the ticks' (random_math.c) */
	/* port: a tick begun: the demo being recorded or played (port/linux/
	game/demo.c), as nothing of the tick has run */
	if (sim_substep_next() == 0 && !game_time_menu_scene())
		demo_tick_begin(game_time_globals->server_time);
	/* port: (or a later sub-step: what the frames put in while the tick
	was open) */
	else if (!game_time_menu_scene())
		demo_substep_resume(sim_substep_next());
	determinism_log_tick_begin();
	random_math_port_set_ticking(TRUE);
	LATENCY_LOG(latency_log_substep_begin()); /* port: debug.latency_log */
	tick_ended = game_tick_substep(sim_substep_next());
	LATENCY_LOG(latency_log_substep_end()); /* port: debug.latency_log */
	/* port: (and ended, before the netcode's work of the tick, which a
	demo has as what reached the game state from outside the ticks) */
	if (tick_ended && !game_time_menu_scene())
		demo_tick_end(game_time_globals->server_time + 1);
	if (tick_ended)
	{
		sim_trace_step(game_time_globals->server_time, sim_substep_count() - 1, sim_substep_count()); /* port: debug.trace */
		profile_scope(game_time_render_interpolation_tick_section, render_interpolation_tick();)
		game_time_globals->server_time++;
		game_time_globals->local_time++;
		/* the distributed netcode's per-tick state (port: none of the main
		menu's scene, which a lobby's machines do not share) */
		if (!game_time_menu_scene())
		{
			profile_scope(game_time_network_distributed_tick_section, network_distributed_tick();)
		}
		random_math_port_set_ticking(FALSE);
		determinism_log_tick_end(game_time_globals->server_time);
	}
	else
	{
		/* (the frames before the tick's next sub-step take the frames' seed,
		and are not logged as the tick's) */
		random_math_port_set_ticking(FALSE);
		determinism_log_tick_pause();
		/* port: (and the demo's, port/linux/game/demo.c) */
		if (!game_time_menu_scene())
			demo_tick_pause();
	}

	return tick_ended;
}
