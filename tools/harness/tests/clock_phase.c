/* An OpenH1 game's client clock led ahead of the host's (network_clock.c):
the host's figure of a window's inputs, a client's plan for a report, the
part of it each frame takes, and the loop of them over a game's start on a
network of 20 to 61 ms each way. */
#include "harness.h"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
enum { TICKS_PER_SECOND = 30 };
#include "config.inc"
#include "under_test.inc"

static void expect_plan(char const *name, struct network_clock_plan const *plan, long ticks, real seconds,
	boolean at_once)
{
	CHECK(plan->ticks == ticks && plan->at_once == at_once && plan->seconds > seconds - 0.0001f &&
		plan->seconds < seconds + 0.0001f, "%s: %ld ticks, %f s%s, not %ld ticks, %f s%s", name, plan->ticks,
		plan->seconds, plan->at_once ? " at once" : "", ticks, seconds, at_once ? " at once" : "");
}

/* a game's start: the client's clock the host's from its first update (its
ticks a trip and a sub-step after the host's of their number), its inputs
taking delay_us to the host, give or take jitter_us; a frame of 1/60 s, the
host's report each second of the window's inputs, which comes a trip later.
The host reads its datagrams once a frame of frame_us (0: as they come),
its tick beginning just after a read: an input is late when it is read after
the tick of its number began. The client's inputs' figure at the end, the
ticks skipped after the first five seconds, and the inputs late since. */
static unsigned long seed = 12345;

static long jitter(long range)
{
	seed = seed * 1103515245UL + 12345UL;
	return range ? (long)((seed >> 8) % (unsigned long)(2 * range + 1)) - range : 0;
}

static void simulate(char const *name, long delay_us, long jitter_us, long frame_us)
{
	/* how far the client's tick of a number begins before the host's
	(microseconds): behind, at the start, by the trip and a sub-step */
	double lead = -(double)delay_us - 8333.0;
	struct network_clock_plan plan = { 0, 0.0f, FALSE };
	long samples[CLOCK_WINDOW_SAMPLES];
	short count = 0;
	long frame;
	long ticks_after_start = 0;
	long last_figure = 0;
	double tick_clock = 0.0;
	long pending_report = NONE;
	long report_due = -1;
	long late = 0;
	long timed = 0;

	for (frame = 0; frame < 60 * 12; frame++)
	{
		real dt = 1.0f / 60.0f;
		real part = network_clock_frame_part(&plan.seconds, &plan.at_once, dt);

		lead += part * 1000000.0;
		tick_clock += (dt + part) * TICKS_PER_SECOND;
		/* (the ticks this frame runs: each one's input timed on the host) */
		while (tick_clock >= 1.0)
		{
			tick_clock -= 1.0;
			long early = (long)(lead - delay_us + jitter(jitter_us));

			/* (read at the first frame's read at or after it came: those
			read a tiny while before each tick begins, every frame_us) */
			if (frame_us)
			{
				long tiny = 300;
				long frames = tiny - early > 0 ? (tiny - early + frame_us - 1) / frame_us : -((early - tiny) / frame_us);

				early = tiny - frame_us * frames;
			}
			if (frame >= 60 * 5)
			{
				timed++;
				late += early < 0;
			}
			if (count < CLOCK_WINDOW_SAMPLES)
				samples[count++] = early;
			/* (the ticks skipped at the tick's end) */
			if (plan.ticks)
			{
				lead += plan.ticks * (double)CLOCK_TICK_MICROSECONDS;
				if (frame >= 60 * 5)
					ticks_after_start += plan.ticks;
				plan.ticks = 0;
			}
		}
		/* (a report each second, a trip later) */
		if (frame % 60 == 59 && count >= CLOCK_REPORT_MINIMUM_SAMPLES)
		{
			pending_report = network_clock_window_figure(samples, count);
			report_due = frame + 1 + delay_us * 60 / 1000000;
			count = 0;
		}
		if (frame == report_due)
		{
			last_figure = pending_report;
			network_clock_plan_report(pending_report, 2, &plan);
		}
	}
	CHECK(last_figure >= 0 && last_figure <= CLOCK_EARLY_HIGH_MICROSECONDS + 2 * jitter_us + frame_us + 1500,
		"%s: the inputs' figure %ld us, not just before the host's tick", name, last_figure);
	CHECK(ticks_after_start == 0, "%s: %ld ticks skipped after the start", name, ticks_after_start);
	CHECK(late * 20 <= timed, "%s: %ld of %ld inputs late", name, late, timed);
}

int main(int argc, char **argv) {
	char const *name = argc > 1 ? argv[1] : "";
	struct network_clock_plan plan;

	if (!strcmp(name, "figure"))
	{
		/* (one input held up, far behind: the second earliest of 30) */
		long samples[30];
		short index;

		for (index = 0; index < 30; index++)
			samples[index] = 5000 + (29 - index) * 100;
		samples[7] = -90000;
		CHECK(network_clock_window_figure(samples, 30) == 5000, "figure %ld", samples[1]);
	}
	else if (!strcmp(name, "start"))
	{
		/* (82 ms behind: two ticks skipped, the rest run at once) */
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS - 82000, 1, &plan);
		expect_plan(name, &plan, 2, (82000 - 2 * CLOCK_TICK_MICROSECONDS) / 1000000.0f, TRUE);
	}
	else if (!strcmp(name, "nearest"))
	{
		/* (just short of three ticks behind: three skipped, and the clock
		then a little ahead, slowed; not two and nearly a tick run at once,
		every report) */
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS - (3 * CLOCK_TICK_MICROSECONDS - 2000), 1,
			&plan);
		expect_plan(name, &plan, 3, -0.002f * CLOCK_GAIN, FALSE);
	}
	else if (!strcmp(name, "lead-limit"))
	{
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS - 82000, CLOCK_LEAD_LIMIT_TICKS - 1, &plan);
		expect_plan(name, &plan, 1, (82000 - 2 * CLOCK_TICK_MICROSECONDS) / 1000000.0f, TRUE);
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS - 82000, CLOCK_LEAD_LIMIT_TICKS + 3, &plan);
		CHECK(plan.ticks == 0, "%s: %ld ticks past the limit", name, plan.ticks);
	}
	else if (!strcmp(name, "near"))
	{
		/* (8 ms too early: half of it, slowed; 8 ms late: all of it, sped
		up; just past the target: as it is) */
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS + 8000, 1, &plan);
		expect_plan(name, &plan, 0, -0.008f * CLOCK_GAIN, FALSE);
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS - 8000, 1, &plan);
		expect_plan(name, &plan, 0, 0.008f, FALSE);
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS + 1500, 1, &plan);
		expect_plan(name, &plan, 0, 0.0f, FALSE);
	}
	else if (!strcmp(name, "far-ahead"))
	{
		/* (far ahead, the round trip shorter: never back at once) */
		network_clock_plan_report(CLOCK_EARLY_TARGET_MICROSECONDS + 200000, 1, &plan);
		expect_plan(name, &plan, 0, -0.2f * CLOCK_GAIN, FALSE);
		network_clock_plan_report(5000000, 1, &plan);
		CHECK(plan.seconds >= -(real)CLOCK_SAMPLE_LIMIT_MICROSECONDS / 1000000.0f, "%s: %f s", name, plan.seconds);
	}
	else if (!strcmp(name, "frame"))
	{
		real seconds = 0.01f;
		boolean at_once = FALSE;
		real part = network_clock_frame_part(&seconds, &at_once, 0.02f);

		CHECK(part > 0.02f * CLOCK_SLEW - 0.00001f && part < 0.02f * CLOCK_SLEW + 0.00001f, "slewed %f", part);
		seconds = -0.01f;
		part = network_clock_frame_part(&seconds, &at_once, 0.02f);
		CHECK(part < -0.02f * CLOCK_SLEW + 0.00001f && part > -0.02f * CLOCK_SLEW - 0.00001f, "slowed %f", part);
		seconds = 0.01f;
		at_once = TRUE;
		part = network_clock_frame_part(&seconds, &at_once, 0.02f);
		CHECK(part == 0.01f && seconds == 0.0f && !at_once, "at once %f", part);
	}
	else if (!strcmp(name, "converge-20"))
		simulate(name, 20000, 3000, 0);
	else if (!strcmp(name, "converge-31"))
		simulate(name, 31000, 3000, 0);
	else if (!strcmp(name, "converge-61"))
		simulate(name, 61000, 5000, 0);
	else if (!strcmp(name, "converge-lan"))
		simulate(name, 200, 0, 0);
	else if (!strcmp(name, "frames-server"))
		simulate(name, 200, 0, 8333);
	else if (!strcmp(name, "frames-60"))
		simulate(name, 25500, 3000, 16667);
	else
		CHECK(0, "no case %s", name);
	return 0;
}
