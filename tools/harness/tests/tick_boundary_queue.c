#include "harness.h"
#include <math.h>

/* game_time_update's frames with game.sim_rate's sub-steps (120: four a
tick), the frame's work waiting for a tick's boundary as main_loop's does
(game_time_tick_open_wait), and the players' updates of the ticks: each
built as its tick is counted begun (update_client_local_ticks, a host's
network_game_server_update_ticks), each taken by its tick's first sub-step
(update_client_dequeue). An update built for a tick the frame then did not
begin stayed queued, and every tick after took one built a tick earlier, its
input with it, until the map was loaded again (OpenH1 #590). */
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
#define csmemset memset
#define profile_scope(section, code) code
enum { TICKS_PER_SECOND = 30, SIM_RATE_CLASSIC = 30, SUBSTEPS = 4 };
enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server,
	_game_connection_film_playback };
/* (game_time.c's structures and constants, taken from it) */
#include "tick_boundary_defines.h"

static struct game_time_globals_struct globals, *game_time_globals = &globals;
static int connection = _game_connection_local;
static short next_substep;
/* the updates built and taken, the frame each was built in, the frame now */
static long built, taken, frame;
static long built_in[100000];
static long substeps_run;
static boolean work_pending;

static void sim_rate_frame_begin(void) {}
static long sim_rate_next(void) { return SIM_RATE_CLASSIC * SUBSTEPS; }
static short sim_substep_next(void) { return next_substep; }
static short sim_substep_count(void) { return SUBSTEPS; }
static int game_connection(void) { return connection; }
static real network_clock_frame_seconds(real seconds) { (void)seconds; return 0.0f; }
static boolean halo_fixed_step(void) { return FALSE; }
static boolean game_time_menu_scene(void) { return FALSE; }
static boolean game_time_held(void) { return FALSE; }
static void *global_network_game_server_get(void) { return NULL; }
static void render_interpolation_frame_begin(void) {}
static void render_interpolation_frame_end(void) {}
static void network_test_elevator_frame(void) {}
static void game_frame(real seconds) { (void)seconds; }
static real game_time_get_speed(void) { return 1.0f; }
/* (a local game: no MCC host's speed, no distributed client's catch-up toward a
host update: game_time.c's) */
static void mcc_game_speed_frame(void) {}
static real mcc_game_speed_client(real speed) { return speed; }
static boolean network_game_distributed_client(void) { return FALSE; }
static boolean network_standard_agreed_rate(void) { return FALSE; }
static long game_time_distributed_target = NONE;
static void build(short ticks)
{
	while (ticks-- > 0)
		built_in[built++] = frame;
}
static void update_client_local_ticks(short ticks) { CHECK(connection == _game_connection_local, "local"); build(ticks); }
static void network_game_server_update_ticks(void *server, short ticks)
{ (void)server; CHECK(connection == _game_connection_network_server, "host"); build(ticks); }
/* (the updates' numbers are the ticks': the next to take is the next tick's) */
static long update_client_get_maximum_possible_server_time(void) { return built; }

static boolean game_time_run_substep(void)
{
	substeps_run++;
	if (next_substep == 0)
	{
		CHECK(!game_time_substeps.boundary_wanted, "a tick begun while the frame's work waits for its boundary "
			"(frame %ld)", frame);
		CHECK(taken < built, "a tick begun with no update built for it (frame %ld)", frame);
		CHECK(built_in[taken] == frame, "tick %ld took the update built %ld frames before (frame %ld): its input "
			"stale", globals.server_time, frame - built_in[taken], frame);
		taken++;
	}
	next_substep = (short)((next_substep + 1) % SUBSTEPS);
	if (next_substep)
		return FALSE;
	globals.server_time++;
	globals.local_time++;
	return TRUE;
}

#include "under_test.inc"

/* main_loop's frame: its work waits while a tick is open, else runs */
static void main_frame(real seconds)
{
	if (work_pending && game_time_tick_open_wait())
	{
	}
	else
		work_pending = FALSE;
	game_time_update(seconds);
	frame++;
	CHECK(built == taken, "%ld updates left queued after frame %ld: each tick after takes one built that much "
		"earlier", built - taken, frame);
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";
	long index;
	double seconds = 0.0;

	globals.initialized = globals.active = TRUE;
	globals.speed = 1.0f;
	if (!strcmp(case_name, "host"))
		connection = _game_connection_network_server;
	if (!strcmp(case_name, "local") || !strcmp(case_name, "host"))
	{
		/* (45 frames a second at 120: 2.67 sub-steps a frame, so that frames
		end at every phase of a tick; work every 7th) */
		for (index = 0; index < 4000; index++)
		{
			if (index % 7 == 3)
				work_pending = TRUE;
			main_frame(1.0f / 45.0f);
			seconds += 1.0 / 45.0;
		}
		/* (and no time lost to the waits: the steps' time kept for the next
		frame) */
		CHECK(fabs(substeps_run - seconds * 120.0) <= SUBSTEPS + 1, "%ld sub-steps run in %.2f s", substeps_run,
			seconds);
		printf("%s: %ld frames, %ld ticks, no update left queued\n", case_name, frame, taken);
	}
	else CASE("view")
	{
		/* (60 frames a second at 120, each a little off as a real frame's
		time is; the player running a world unit a tick: the newest step
		left the view a quarter of one past the step before, and the view
		drawn is carried on by the time since it. Then the frame's work
		waiting at every 7th frame, a step held meanwhile) */
		double first = 0.0;
		double view = 0.0, last_view = 0.0;
		real step, last_step = 0.0f;

		for (index = 0; index < 1200; index++)
		{
			real dt = 1.0f / 60.0f * (1.0f + 0.002f * (real)((index * 7) % 5 - 2));

			if (index >= 600 && index % 7 == 3)
				work_pending = TRUE;
			main_frame(dt);
			seconds += dt;
			view = (double)substeps_run / SUBSTEPS + (double)game_time_get_step_age();
			if (index == 0)
				first = view - seconds * TICKS_PER_SECOND;
			CHECK(index >= 600 || game_time_get_step_age() < 1.0f / SUBSTEPS + 1e-4f,
				"frame %ld: a sub-step whose time had come left for the next frame (%.3f of a tick since the "
				"newest step)", index, (double)game_time_get_step_age());
			CHECK(fabs(view - seconds * TICKS_PER_SECOND - first) < 2e-3,
				"frame %ld: the view %.4f world units from where the frames' time has it", index,
				view - seconds * TICKS_PER_SECOND - first);
			step = (real)(view - last_view);
			if (index > 0 && index < 600)
			{
				CHECK(fabsf(step - dt * TICKS_PER_SECOND) < 2e-3f, "frame %ld: the view stepped %.4f (%.4f the "
					"frame before), the frame's time %.4f", index, (double)step, (double)last_step,
					(double)(dt * TICKS_PER_SECOND));
			}
			last_view = view;
			last_step = step;
		}
		printf("view: %ld frames, the view as far each frame as its time\n", frame);
	}
	else CASE("stall")
	{
		/* (a frame of two seconds, a shader compiled, with work waiting: the
		time past the frame's most ticks is dropped, and none is owed after) */
		for (index = 0; index < 600; index++)
		{
			long before = substeps_run;

			if (index == 300)
				work_pending = TRUE;
			main_frame(index == 301 ? 2.0f : 1.0f / 60.0f);
			if (index > 302)
				CHECK(substeps_run - before <= 3, "frame %ld ran %ld sub-steps: time owed from the stall", index,
					substeps_run - before);
		}
		printf("stall: %ld frames, %ld ticks, no update left queued\n", frame, taken);
	}
	return 0;
}
