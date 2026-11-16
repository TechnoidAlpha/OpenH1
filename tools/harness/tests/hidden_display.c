/* A window hidden on macOS (on another desktop, covered, minimized), whose
display link stops taking the game's frames: the host stands in for it with
refreshes of its own on the grid of the link's last ones (frame_pacing.h's
frame_pacing_next_latch, port/macos/host_metal.m's hidden_refresh), so that
the game goes on at the display's pace and phase, and the link's first
refresh back finds the frame started for it. */
#include "harness.h"
#include <stdint.h>
#define FRAME_PACING_UNUSED
#include "under_test.inc"

#define PERIOD 16666667ull
#define MARGIN 3000000ull

/* a stand-in that wakes for each take a little before its time, and late by
up to late_ns more (a timer's, the scheduler's): every latch taken once, a
refresh apart, on the link's grid */
static void hidden(char const *name, uint64_t late_ns)
{
	uint64_t first = 1000000000ull, last = first, now = first - MARGIN - 2000000ull;
	uint64_t seed = 12345;
	int refresh;

	for (refresh = 0; refresh < 600; refresh++)
	{
		uint64_t latch = frame_pacing_next_latch(last, PERIOD, now, MARGIN);

		CHECK(latch == last + PERIOD, "%s: refresh %d: latch %lld refreshes on, not 1", name, refresh,
			(long long)((int64_t)(latch - last) / (int64_t)PERIOD));
		CHECK((latch - first) % PERIOD == 0, "%s: refresh %d: the latch off the link's grid", name, refresh);
		last = latch;
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		now = latch + PERIOD - MARGIN - 2000000ull + (late_ns ? (seed >> 33) % late_ns : 0);
	}
}

int main(int argc, char **argv)
{
	char const *which = argc > 1 ? argv[1] : "";

	if (!strcmp(which, "even"))
	{
		hidden("on time", 0);
		hidden("late by up to 8 ms", 8000000ull);
	}
	else if (!strcmp(which, "early"))
	{
		/* (a wake a refresh before the link's last latch, or at it: the
		refresh after it, never that latch again) */
		uint64_t last = 5000000000ull;

		CHECK(frame_pacing_next_latch(last, PERIOD, last - PERIOD, MARGIN) == last + PERIOD,
			"a wake a refresh early: latch %lld ns on", (long long)(frame_pacing_next_latch(last, PERIOD,
			last - PERIOD, MARGIN) - last));
		CHECK(frame_pacing_next_latch(last, PERIOD, last, MARGIN) == last + PERIOD, "a wake at the last latch");
	}
	else if (!strcmp(which, "late"))
	{
		/* (a wake two and a half refreshes late: the latches passed are
		skipped, as a display skips them, on the grid) */
		uint64_t last = 5000000000ull, now = last + 2 * PERIOD + PERIOD / 2;
		uint64_t latch = frame_pacing_next_latch(last, PERIOD, now, MARGIN);

		CHECK(latch == last + 3 * PERIOD, "a late wake: latch %lld refreshes on, not 3",
			(long long)((latch - last) / PERIOD));
		/* (ten seconds on, the link stopped meanwhile: the latch nearest
		the take's time) */
		now = last + 10000000000ull;
		latch = frame_pacing_next_latch(last, PERIOD, now, MARGIN);
		CHECK(latch + PERIOD / 2 >= now + MARGIN && latch < now + MARGIN + PERIOD / 2 && (latch - last) % PERIOD == 0,
			"ten seconds on: latch %lld ns after the wake", (long long)(latch - now));
	}
	else if (!strcmp(which, "no-grid"))
	{
		CHECK(frame_pacing_next_latch(0, PERIOD, 1000, MARGIN) == 0, "no latch yet");
		CHECK(frame_pacing_next_latch(1000, 0, 1000, MARGIN) == 0, "no period yet");
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", which);
		return 2;
	}
	return 0;
}
