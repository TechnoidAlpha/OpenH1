/*
UNSEEN_FRAMES.C

A run whose frames nobody sees (debug.null_renderer, or no display: a
network test's headless host or client, a probe) draws none (main.c), and
would otherwise run its frames back to back, thousands a second, each with
no step of the game's clock to run: the clock steps only as its time comes
(game_time_update). So each frame waits, before its time is measured, until
the clock's next step falls due (game_time_seconds_to_next_step): one step
a frame, a tick at game.sim_rate 30 and a sub-step at 60 and 120, as a
display of that rate would pace it. The frames' time is measured as before,
so the game plays as it did; only the frames that ran no step are gone.
While the clock does not run (loading, a pause), a frame waits a step of
game.sim_rate. A dedicated server paces its frames itself
(server_mode_pace_frame), and runs whose frames run fixed times
(debug.frame_time, debug.fixed_step, a movie) are not paced: they run as fast
as they can (main.c).
*/

#include "cseries.h"
#include "game/game.h"
#include "sim_rate.h"

/* (the most a frame waits, in seconds: the console, the network and the
events are seen to at least this often) */
#define UNSEEN_FRAME_LONGEST_WAIT (1.0/30.0)

/* the platform layer's */
unsigned long long SDL_GetTicksNS(void);
void platform_delay_nanoseconds(unsigned long long nanoseconds);

static unsigned long long unseen_frame_last;

void unseen_frame_wait(
	void)
{
	unsigned long long now = SDL_GetTicksNS();
	double wait = (double)game_time_seconds_to_next_step();

	if (wait < 0.0)
		wait = 1.0/(double)sim_rate_next();
	if (wait > UNSEEN_FRAME_LONGEST_WAIT)
		wait = UNSEEN_FRAME_LONGEST_WAIT;
	/* (less the time since the last frame's was measured, which the clock
	counts already) */
	if (unseen_frame_last && now > unseen_frame_last)
		wait -= (double)(now - unseen_frame_last)/1e9;
	if (wait > 0.0)
	{
		/* (to the next whole microsecond: the step is then due, not a
		rounding short of it) */
		platform_delay_nanoseconds(((unsigned long long)(wait*1e6) + 1)*1000ULL);
	}
	unseen_frame_last = SDL_GetTicksNS();
}
