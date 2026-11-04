/*
BOTS_HABITS.C (test)

A fighting bot's habits (bots.c's bots_aimed_at, bots_strafe and
bots_strike_reach): it knows when its target's aim is on it; it strafes one
way and the other about every second while no one aims at it (people's
rhythm), and the other way once it reacts to an aim on
it (its skill's dodge reaction and chance), again a moment later while the
aim stays; a blow is struck at the engine's reach to the target's surface,
further charging in. test_bots_habits.py takes the constants, structures
and skill table (config.inc) and the code (under_test.inc).
*/

#include "harness.h"

#include <math.h>

#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define csmemset memset

/* (what struct bots_bot holds of the engine's: not read here) */
struct path_result
{
	boolean valid;
};

#include "config.inc"

#include "under_test.inc"

static struct bots_bot bot;

static void reset(unsigned long seed)
{
	memset(&bot, 0, sizeof(bot));
	bot.random = seed;
	bot.strafe_sign = 1.0f;
	bot.aimed_since_tick = NONE;
}

/* an aim on the bot, beside it, behind the one aiming */
static void case_aimed_at(void)
{
	real_point3d eye = { 0.0f, 0.0f, 0.6f }, bot_middle = { 10.0f, 0.0f, 0.4f };
	real_vector3d on = { 1.0f, 0.0f, -0.02f }, beside = { 1.0f, 0.2f, 0.0f }, away = { -1.0f, 0.0f, 0.0f };
	real_vector3d long_on = { 5.0f, 0.0f, -0.1f };

	CHECK(bots_aimed_at(&eye, &on, &bot_middle, 0.8f), "an aim on the bot not seen");
	CHECK(bots_aimed_at(&eye, &long_on, &bot_middle, 0.8f), "an aim of another length not seen");
	CHECK(!bots_aimed_at(&eye, &beside, &bot_middle, 0.8f), "an aim two units beside the bot taken for one on it");
	CHECK(!bots_aimed_at(&eye, &away, &bot_middle, 0.8f), "an aim away from the bot taken for one on it");
}

/* not aimed at: the other way about every second (the median of the
stretches 0.7 to 1.3 seconds), never within a third of a second nor after
more than two and a half; aimed at: the other way once it reacts, not
before, and again a moment later while the aim stays on it */
static void case_strafe(void)
{
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *skill_definition = &bots_skills[skill];
		long reaction = (long)(skill_definition->dodge_reaction_seconds * TICKS_PER_SECOND);
		long tick, flips = 0, dodges = 0, aims = 0, dodged_aims = 0, last_change = 0, stretches = 0;
		long counts[TICKS_PER_SECOND * 3] = { 0 };
		long least = TICKS_PER_SECOND * 3, most = 0, below = 0, median = 0;

		reset(11 + skill);
		bots_strafe(&bot, skill_definition, FALSE, 0);
		for (tick = 1; tick < TICKS_PER_SECOND * 1000; tick++)
		{
			real before = bot.strafe_sign;

			CHECK(!bots_strafe(&bot, skill_definition, FALSE, tick), "%s dodged no aim", skill_definition->name);
			if (bot.strafe_sign != before)
			{
				long stretch = tick - last_change;

				CHECK(bot.strafe_sign == -before, "%s changed its way but not about", skill_definition->name);
				if (stretch < (long)NUMBEROF(counts))
					counts[stretch]++;
				least = MIN(least, stretch);
				most = MAX(most, stretch);
				stretches++;
				last_change = tick;
			}
		}
		for (median = 0; median < (long)NUMBEROF(counts) && below * 2 < stretches; median++)
			below += counts[median];
		printf("%s: %ld stretches with no aim, %ld to %ld ticks, the median about %ld\n", skill_definition->name,
			stretches, least, most, median);
		CHECK(least >= TICKS_PER_SECOND / 3, "%s changed its way after %ld ticks, no one aiming at it",
			skill_definition->name, least);
		CHECK(most <= TICKS_PER_SECOND * 5 / 2 + 1, "%s kept its way %ld ticks, no one aiming at it",
			skill_definition->name, most);
		CHECK(median >= TICKS_PER_SECOND * 7 / 10 && median <= TICKS_PER_SECOND * 13 / 10,
			"%s changed its way every %ld ticks (the median), not about every second", skill_definition->name, median);
		reset(11 + skill);
		/* (aims on the bot of two seconds, each after two seconds of none) */
		for (tick = TICKS_PER_SECOND * 2; tick < TICKS_PER_SECOND * 2000; tick++)
		{
			boolean aimed = (tick / (TICKS_PER_SECOND * 2)) % 2 == 1;
			long since = tick % (TICKS_PER_SECOND * 2);
			real before = bot.strafe_sign;
			boolean dodged = bots_strafe(&bot, skill_definition, aimed, tick);

			if (!aimed)
			{
				flips += bot.strafe_sign != before;
				continue;
			}
			if (since == 0)
				aims++;
			if (since < reaction)
				CHECK(!dodged, "%s dodged %ld ticks into an aim, before its reaction (%ld)", skill_definition->name, since, reaction);
			if (dodged)
			{
				CHECK(bot.strafe_sign == -before, "%s dodged without turning about", skill_definition->name);
				dodges++;
				dodged_aims += since == reaction;
			}
		}
		printf("%s: %ld aims, %ld dodged at its reaction, %ld dodges in all; %ld changes of way with no aim\n",
			skill_definition->name, aims, dodged_aims, dodges, flips);
		CHECK((real)dodged_aims / (real)aims > skill_definition->dodge_chance - 0.08f &&
			(real)dodged_aims / (real)aims < skill_definition->dodge_chance + 0.08f,
			"%s dodged %ld of %ld aims at its reaction (chance %.2f)", skill_definition->name, dodged_aims, aims,
			skill_definition->dodge_chance);
		CHECK(dodges > dodged_aims, "%s dodged once an aim at most", skill_definition->name);
	}
	CHECK(bots_skills[NUMBER_OF_BOTS_SKILLS - 1].dodge_chance > bots_skills[0].dodge_chance,
		"the legendary bot dodges no more often than the easy one");
}

/* a blow at the engine's reach plus the target's middle; further charging */
static void case_strike_reach(void)
{
	CHECK(fabsf(bots_strike_reach(0.3f, FALSE) - (BOTS_MELEE_REACH + 0.3f)) < 0.001f, "the reach to a biped's middle %.2f",
		bots_strike_reach(0.3f, FALSE));
	CHECK(bots_strike_reach(0.3f, TRUE) > bots_strike_reach(0.3f, FALSE), "no further charging in");
	CHECK(bots_strike_reach(5.0f, FALSE) < 1.5f, "a large target's reach %.2f", bots_strike_reach(5.0f, FALSE));
	CHECK(BOTS_MELEE_REACH > 0.7f && BOTS_MELEE_REACH < 0.9f, "the blow's reach %.2f, not the engine's 0.8",
		BOTS_MELEE_REACH);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("aimed-at") case_aimed_at();
	else CASE("strafe") case_strafe();
	else CASE("strike-reach") case_strike_reach();
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
