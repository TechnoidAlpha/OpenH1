/*
SIM_RATE.C (test)

The simulation's two clocks (game.sim_rate, port/linux/game/sim_rate.c):
the rate a tick takes from the setting and the connection, the fast
phase's sub-steps and their slots, the conversions of per-tick quantities
(each exactly its argument at 30) and the frames' blend between sub-steps.
test_sim_rate.py takes the declarations of sim_rate.h (config.inc) and the
code (under_test.inc).
*/

#include "harness.h"

#include <math.h>

enum
{
	_game_connection_local = 0,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
};

/* the fake world: the setting, its changes, the connection */
static long setting = 30;
static unsigned long changes = 1;
static short connection = _game_connection_local;
/* a network server's game takes no other machine (split screen) */
static boolean splitscreen = FALSE;
/* the rate a network game agreed on (network_standard.c: a Standard
game's, 0 for a Legacy game's) */
static long agreed = 0;

long network_standard_agreed_rate(void)
{
	return agreed;
}

/* (port/linux/game/demo.c: no demo plays) */
long demo_playback_sim_rate(void)
{
	return 0;
}

long config_integer(char const *name)
{
	return strcmp(name, "game.sim_rate") ? 0 : setting;
}

unsigned long config_changes(void)
{
	return changes;
}

static short game_connection(void)
{
	return connection;
}

boolean network_game_is_splitscreen_local(void)
{
	return connection == _game_connection_network_server && splitscreen;
}

/* (real_math.h's) */
static real square_root(real x)
{
	return (real)sqrt(x);
}

#include "config.inc"
#include "under_test.inc"

static void set(
	long rate,
	short new_connection)
{
	setting = rate;
	changes++;
	connection = new_connection;
	sim_rate_tick_begin();
}

/* a tick's sub-steps, all of them (as a frame slower than a tick runs them) */
static void run_tick(
	void)
{
	short substep;

	for (substep = 0; substep < sim_substep_count(); substep++)
		sim_substep_run(substep);
}

static boolean same_bits(
	real a,
	real b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

static boolean near(
	real a,
	real b)
{
	return fabs(a - b) <= 1e-6 * (fabs(a) > 1.0 ? fabs(a) : 1.0);
}

/* the slots' calls, in order */
static struct
{
	short slot, index, count;
	real seconds;
	boolean last;
	real end_fraction;
	boolean first_of_frame;
} calls[64];
static int call_count;

#define SLOT_PROCEDURE(number) \
	static void slot_##number(short index, short count, real seconds) \
	{ \
		calls[call_count].slot = number; \
		calls[call_count].index = index; \
		calls[call_count].count = count; \
		calls[call_count].seconds = seconds; \
		calls[call_count].last = sim_substep_is_last(); \
		calls[call_count].end_fraction = sim_substep_end_fraction(); \
		calls[call_count].first_of_frame = sim_substep_first_of_frame(); \
		call_count++; \
	}
SLOT_PROCEDURE(0)
SLOT_PROCEDURE(1)
SLOT_PROCEDURE(4)

/* per-tick values: speeds, accelerations (gravity 0.0035651792 wu/tick²),
factors and chances, and one too small to scale without rounding */
static real const values[] = { 0.0035651792f, 0.07f, 0.255f, 0.00275f, 1.0f / 3.0f, 0.6f, 0.9f, 1.0f, 0.0f,
	-2.5f, 1234.5678f, 1e-30f };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	unsigned int index;

	/* at 30 every conversion is its argument, bit for bit, and the blend
	the tick's own fraction */
	if (!strcmp(case_name, "classic-identity"))
	{
		short substep;
		real fraction;

		set(30, _game_connection_local);
		CHECK(sim_rate_get() == 30 && sim_substep_count() == 1, "rate %ld count %d", sim_rate_get(), sim_substep_count());
		CHECK(sim_substep_index() == NONE && sim_substep_is_last() && sim_substep_end_fraction() == 1.0f, "outside");
		CHECK(same_bits(sim_substep_tick_fraction(), 1.0f), "tick fraction");
		CHECK(same_bits(sim_substep_seconds(), 1.0f / 30), "seconds");
		for (index = 0; index < NUMBEROF(values); index++)
		{
			real value = values[index];

			CHECK(same_bits(sim_per_substep(value), value), "linear %g", value);
			CHECK(same_bits(sim_per_substep_squared(value), value), "quadratic %g", value);
			CHECK(same_bits(sim_decay_per_substep(value), value), "decay %g", value);
			CHECK(same_bits(sim_chance_per_substep(value), value), "chance %g", value);
		}
		CHECK(sim_ticks_to_substeps(150) == 150, "ticks");
		for (index = 0; index <= 100; index++)
		{
			real tick_fraction = index / 100.0f;

			sim_substep_blend(tick_fraction, &substep, &fraction);
			CHECK(substep == 0 && same_bits(fraction, tick_fraction), "blend %g: %d %g", tick_fraction, substep, fraction);
		}
		return 0;
	}

	/* game.sim_rate: 30, 60 or 120, anything else 30, read again when the
	settings change */
	if (!strcmp(case_name, "rates"))
	{
		static long const asked[] = { 30, 60, 120, 0, 90, 128, 240, -60 };
		static long const taken[] = { 30, 60, 120, 30, 30, 30, 30, 30 };

		for (index = 0; index < NUMBEROF(asked); index++)
		{
			set(asked[index], _game_connection_local);
			CHECK(sim_rate_get() == taken[index] && sim_substep_count() == taken[index] / 30,
				"asked %ld: rate %ld count %d", asked[index], sim_rate_get(), sim_substep_count());
		}
		set(120, _game_connection_local);
		setting = 60; /* (no change counted: not read again) */
		sim_rate_tick_begin();
		CHECK(sim_rate_get() == 120, "read again without a change: %ld", sim_rate_get());
		return 0;
	}

	/* 30 in any network game, hosting or joining, and in a film's playback */
	if (!strcmp(case_name, "network-classic"))
	{
		static short const connections[] = { _game_connection_network_client, _game_connection_network_server,
			_game_connection_film_playback };

		for (index = 0; index < NUMBEROF(connections); index++)
		{
			set(120, connections[index]);
			CHECK(sim_rate_get() == 30 && sim_substep_count() == 1, "connection %d: rate %ld", connections[index],
				sim_rate_get());
		}
		set(120, _game_connection_local);
		CHECK(sim_rate_get() == 120, "local: %ld", sim_rate_get());
		/* split screen (and the bots' games): a network game of one machine,
		its own server's, that takes no other: the setting */
		splitscreen = TRUE;
		set(120, _game_connection_network_server);
		CHECK(sim_rate_get() == 120, "split screen: %ld", sim_rate_get());
		set(60, _game_connection_network_server);
		CHECK(sim_rate_get() == 60 && sim_substep_count() == 2, "split screen at 60: %ld", sim_rate_get());
		splitscreen = FALSE;
		set(120, _game_connection_network_server);
		CHECK(sim_rate_get() == 30, "a server that takes other machines: %ld", sim_rate_get());
		return 0;
	}

	/* a Standard game's agreed rate (network_standard.c), whatever the
	setting, on host and client alike; one the sub-steps do not step at, 30;
	a Legacy game's (none), 30; never a film's playback */
	if (!strcmp(case_name, "network-agreed"))
	{
		static short const connections[] = { _game_connection_network_client, _game_connection_network_server };
		static long const rates[] = { 30, 60, 120 };
		short rate_index;

		for (index = 0; index < NUMBEROF(connections); index++)
		{
			for (rate_index = 0; rate_index < NUMBEROF(rates); rate_index++)
			{
				agreed = rates[rate_index];
				set(rate_index ? 30 : 120, connections[index]);
				CHECK(sim_rate_get() == rates[rate_index] && sim_substep_count() == rates[rate_index] / 30,
					"connection %d agreed %ld: rate %ld", connections[index], agreed, sim_rate_get());
			}
			agreed = 90;
			set(120, connections[index]);
			CHECK(sim_rate_get() == 30, "connection %d agreed 90: %ld", connections[index], sim_rate_get());
			agreed = 240;
			set(120, connections[index]);
			CHECK(sim_rate_get() == 30, "connection %d agreed 240: %ld", connections[index], sim_rate_get());
		}
		agreed = 60;
		set(120, _game_connection_film_playback);
		CHECK(sim_rate_get() == 30, "film playback: %ld", sim_rate_get());
		agreed = 0;
		CHECK(sim_rate_supported(30) && sim_rate_supported(60) && sim_rate_supported(120) && !sim_rate_supported(0) &&
			!sim_rate_supported(90) && !sim_rate_supported(240) && !sim_rate_supported(-30), "supported rates");
		return 0;
	}

	/* the setting is set aside (the menus' row locked) exactly where the
	connection, not the setting, gives the rate: every connection but a game
	of this machine alone (its own connection's, or split screen's) */
	if (!strcmp(case_name, "locked"))
	{
		static short const connections[] = { _game_connection_local, _game_connection_network_client,
			_game_connection_network_server, _game_connection_film_playback };

		for (index = 0; index < NUMBEROF(connections); index++)
		{
			set(120, connections[index]);
			CHECK(sim_rate_locked() == (sim_rate_get() != 120), "connection %d: locked %d at rate %ld",
				connections[index], sim_rate_locked(), sim_rate_get());
			set(30, connections[index]);
			CHECK(sim_rate_locked() == (connections[index] != _game_connection_local),
				"connection %d at 30: locked %d", connections[index], sim_rate_locked());
		}
		splitscreen = TRUE;
		set(120, _game_connection_network_server);
		CHECK(!sim_rate_locked() && sim_rate_get() == 120, "split screen locked %d at %ld", sim_rate_locked(),
			sim_rate_get());
		return 0;
	}

	/* 120 is 4 x 30: the linear and quadratic conversions scale by a power of
	two, exactly */
	if (!strcmp(case_name, "exact-scaling"))
	{
		long rate;

		for (rate = 60; rate <= 120; rate *= 2)
		{
			real k = (real)(rate / 30);

			set(rate, _game_connection_local);
			CHECK(same_bits(sim_substep_tick_fraction() * k, 1.0f), "tick fraction at %ld", rate);
			CHECK(sim_ticks_to_substeps(150) == 150 * (long)k, "ticks at %ld", rate);
			for (index = 0; index < NUMBEROF(values) - 1; index++)
			{
				CHECK(same_bits(sim_per_substep(values[index]) * k, values[index]), "linear %g at %ld", values[index], rate);
				CHECK(same_bits(sim_per_substep_squared(values[index]) * k * k, values[index]), "quadratic %g at %ld",
					values[index], rate);
			}
		}
		return 0;
	}

	/* a factor or a chance per sub-step makes the tick's in count sub-steps;
	one outside 0 to 1 is left as it is */
	if (!strcmp(case_name, "decay-and-chance"))
	{
		long rate;

		for (rate = 60; rate <= 120; rate *= 2)
		{
			set(rate, _game_connection_local);
			for (index = 0; index < NUMBEROF(values); index++)
			{
				real value = values[index];
				real decay = sim_decay_per_substep(value);
				real chance = sim_chance_per_substep(value);
				real decayed = 1.0f, unhappened = 1.0f;
				short step;

				if (!(value >= 0.0f && value <= 1.0f))
				{
					CHECK(same_bits(decay, value) && same_bits(chance, value), "%g outside 0 to 1 at %ld", value, rate);
					continue;
				}
				for (step = 0; step < sim_substep_count(); step++)
				{
					decayed *= decay;
					unhappened *= 1.0f - chance;
				}
				CHECK(near(decayed, value), "decay %g at %ld: %.9g", value, rate, decayed);
				CHECK(near(1.0f - unhappened, value), "chance %g at %ld: %.9g", value, rate, 1.0f - unhappened);
			}
		}
		return 0;
	}

	/* each sub-step runs the slots registered, in the slots' order, told its
	index, the count and its seconds; outside the fast phase there is none */
	if (!strcmp(case_name, "substeps-run"))
	{
		long rate;

		sim_substep_register(4, slot_4);
		sim_substep_register(1, slot_1);
		sim_substep_register(0, slot_0);
		sim_substep_register(NUMBER_OF_SIM_SUBSTEP_SLOTS, slot_0); /* (refused) */
		sim_substep_register(-1, slot_0);
		for (rate = 30; rate <= 120; rate *= 2)
		{
			short count = (short)(rate / 30);
			int expected = 0;
			short step;

			set(rate, _game_connection_local);
			call_count = 0;
			run_tick();
			CHECK(call_count == 3 * count, "%d calls at %ld", call_count, rate);
			for (step = 0; step < count; step++)
			{
				static short const order[] = { 0, 1, 4 };
				int slot;

				for (slot = 0; slot < 3; slot++, expected++)
				{
					CHECK(calls[expected].slot == order[slot] && calls[expected].index == step &&
						calls[expected].count == count && same_bits(calls[expected].seconds, 1.0f / rate),
						"call %d at %ld: slot %d index %d", expected, rate, calls[expected].slot, calls[expected].index);
					CHECK(calls[expected].last == (step == count - 1), "last at %ld step %d", rate, step);
					CHECK(same_bits(calls[expected].end_fraction, (real)(step + 1) / count), "end at %ld step %d",
						rate, step);
				}
			}
			CHECK(sim_substep_index() == NONE && sim_substep_is_last(), "outside after the phase at %ld", rate);
		}
		sim_substep_register(1, NULL);
		set(120, _game_connection_local);
		call_count = 0;
		run_tick();
		CHECK(call_count == 8, "a slot taken away: %d calls", call_count);
		return 0;
	}

	/* a frame between the ticks, as the sub-step it is in and how far */
	if (!strcmp(case_name, "blend"))
	{
		short substep;
		real fraction;

		set(120, _game_connection_local);
		sim_substep_blend(0.6f, &substep, &fraction);
		CHECK(substep == 2 && near(fraction, 0.4f), "0.6: %d %g", substep, fraction);
		sim_substep_blend(1.0f, &substep, &fraction);
		CHECK(substep == 3 && fraction == 1.0f, "1: %d %g", substep, fraction);
		sim_substep_blend(0.0f, &substep, &fraction);
		CHECK(substep == 0 && fraction == 0.0f, "0: %d %g", substep, fraction);
		sim_substep_blend(-1.0f, &substep, &fraction);
		CHECK(substep == 0 && fraction == 0.0f, "-1: %d %g", substep, fraction);
		sim_substep_blend(7.0f, &substep, &fraction);
		CHECK(substep == 3 && fraction == 1.0f, "7: %d %g", substep, fraction);
		set(60, _game_connection_local);
		sim_substep_blend(0.25f, &substep, &fraction);
		CHECK(substep == 0 && same_bits(fraction, 0.5f), "0.25 at 60: %d %g", substep, fraction);
		sim_substep_blend(0.75f, &substep, &fraction);
		CHECK(substep == 1 && same_bits(fraction, 0.5f), "0.75 at 60: %d %g", substep, fraction);
		return 0;
	}

	/* the sub-steps one at a time, as the clock spreads them over the
	frames: the open tick's next, the first of each frame, the rate of the
	next sub-step (the open tick's, whatever the setting says meanwhile, then
	the next tick's), and an open tick given up */
	if (!strcmp(case_name, "spread"))
	{
		short step;

		sim_substep_register(0, slot_0);
		set(120, _game_connection_local);
		CHECK(sim_substep_next() == 0 && sim_rate_next() == 120, "before: next %d rate %ld", sim_substep_next(),
			sim_rate_next());
		for (step = 0; step < 4; step++)
		{
			call_count = 0;
			sim_rate_frame_begin();
			sim_substep_run(step);
			CHECK(call_count == 1 && calls[0].index == step && calls[0].count == 4 && calls[0].first_of_frame,
				"step %d: %d calls", step, call_count);
			CHECK(sim_substep_next() == (step < 3 ? step + 1 : 0), "after %d: next %d", step, sim_substep_next());
			CHECK(sim_substep_index() == NONE && !sim_substep_first_of_frame(), "between %d", step);
			if (step == 0)
			{
				/* (the setting changed in the open tick: its rate holds) */
				setting = 30;
				changes++;
			}
			CHECK(sim_rate_next() == (step < 3 ? 120 : 30), "rate after %d: %ld", step, sim_rate_next());
		}
		/* two sub-steps in one frame, then one in the next; a tick given up */
		set(120, _game_connection_local);
		call_count = 0;
		sim_rate_frame_begin();
		sim_substep_run(0);
		sim_substep_run(1);
		sim_rate_frame_begin();
		sim_substep_run(2);
		CHECK(call_count == 3 && calls[0].first_of_frame && !calls[1].first_of_frame && calls[2].first_of_frame,
			"first of frame: %d %d %d", calls[0].first_of_frame, calls[1].first_of_frame, calls[2].first_of_frame);
		CHECK(sim_substep_next() == 3, "open at 3: %d", sim_substep_next());
		sim_rate_tick_drop();
		CHECK(sim_substep_next() == 0 && sim_substep_index() == NONE, "dropped: next %d", sim_substep_next());
		/* at 30 a sub-step is the tick: none is ever open */
		set(30, _game_connection_local);
		sim_substep_run(0);
		CHECK(sim_substep_next() == 0 && sim_rate_next() == 30, "30: next %d", sim_substep_next());
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
