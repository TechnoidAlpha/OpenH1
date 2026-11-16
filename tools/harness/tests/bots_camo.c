/*
BOTS_CAMO.C (test)

A bot against active camouflage (bots.c's bots_camo_*): it notices a
camouflaged enemy by a chance that falls with the distance, grows with the
enemy's motion (across its sight more than along it), with the centre of
its view and with its skill, and much more with a giveaway (a shot, the
camouflage's flicker, a hit, a blow); a still one at range it rarely
notices, and none it notices surely at once; it loses one it fights the
more as it crosses its sight and goes quiet, the less the more skilled the
bot; its aim at one is blurred, larger and slower to settle, plain again
as the enemy gives itself away. test_bots_camo.py takes the constants,
structures and skill table (config.inc) and the code (under_test.inc).
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

#define RUN BOTS_CAMO_RUN_SPEED
#define WALK (BOTS_CAMO_RUN_SPEED * 0.45f)

static struct bots_bot bot;

static void reset(unsigned long seed)
{
	memset(&bot, 0, sizeof(bot));
	bot.random = seed;
	bot.form = 1.0f;
	bot.motion_change_tick = NONE;
}

/* a whole camouflage, in the centre of the view, quiet, crossing the
bot's sight at that speed */
static struct bots_camo_sight sight_at(real distance, real across)
{
	struct bots_camo_sight sight;

	memset(&sight, 0, sizeof(sight));
	sight.camouflage = 1.0f;
	sight.distance = distance;
	sight.across = across;
	return sight;
}

/* the share of many tries in which the bot notices (tracked: loses) the
enemy within that many seconds of looks, a look every third tick */
static real share_within(struct bots_skill_definition const *skill, struct bots_camo_sight const *sight,
	boolean tracked, real seconds)
{
	long tries = 4000, try, happened = 0;
	long looks = (long)(seconds * TICKS_PER_SECOND / 3.0f + 0.5f);

	reset(12345);
	for (try = 0; try < tries; try++)
	{
		long look;

		for (look = 0; look < looks; look++)
		{
			boolean seen = bots_camo_seen(&bot, skill, sight, tracked, 3.0f / TICKS_PER_SECOND);

			if (tracked ? !seen : seen)
			{
				happened++;
				break;
			}
		}
	}
	return (real)happened / (real)tries;
}

/* the further, the less noticed, for every skill and motion */
static void case_distance(void)
{
	static real const distances[] = { 2.0f, 5.0f, 10.0f, 20.0f, 40.0f };
	static real const speeds[] = { 0.0f, WALK, RUN };
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		unsigned speed, index;

		for (speed = 0; speed < NUMBEROF(speeds); speed++)
		{
			for (index = 1; index < NUMBEROF(distances); index++)
			{
				struct bots_camo_sight near = sight_at(distances[index - 1], speeds[speed]);
				struct bots_camo_sight far = sight_at(distances[index], speeds[speed]);

				CHECK(bots_camo_notice_rate(&bots_skills[skill], &far) < bots_camo_notice_rate(&bots_skills[skill], &near),
					"%s notices one at %.0f units no less than at %.0f", bots_skills[skill].name, distances[index],
					distances[index - 1]);
			}
		}
	}
}

/* the faster, the more noticed; across the bot's sight more than along it */
static void case_motion(void)
{
	static real const distances[] = { 3.0f, 10.0f, 25.0f };
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *definition = &bots_skills[skill];
		unsigned index;

		for (index = 0; index < NUMBEROF(distances); index++)
		{
			struct bots_camo_sight still = sight_at(distances[index], 0.0f);
			struct bots_camo_sight walking = sight_at(distances[index], WALK);
			struct bots_camo_sight running = sight_at(distances[index], RUN);
			struct bots_camo_sight towards = sight_at(distances[index], 0.0f);

			towards.along = RUN;
			CHECK(bots_camo_notice_rate(definition, &still) < bots_camo_notice_rate(definition, &walking) &&
				bots_camo_notice_rate(definition, &walking) < bots_camo_notice_rate(definition, &running),
				"%s at %.0f units: still %.3f, walking %.3f, running %.3f a second", definition->name, distances[index],
				bots_camo_notice_rate(definition, &still), bots_camo_notice_rate(definition, &walking),
				bots_camo_notice_rate(definition, &running));
			CHECK(bots_camo_notice_rate(definition, &running) > bots_camo_notice_rate(definition, &towards) &&
				bots_camo_notice_rate(definition, &towards) > bots_camo_notice_rate(definition, &still),
				"%s: a run across its sight not plainer than one towards it, or that not plainer than standing",
				definition->name);
			/* (a run plainer by far) */
			CHECK(bots_camo_notice_rate(definition, &running) > 5.0f * bots_camo_notice_rate(definition, &still),
				"%s: a run not five times as plain as standing still", definition->name);
		}
	}
}

/* the more skilled, the more it notices and the less it loses */
static void case_skill(void)
{
	struct bots_camo_sight sights[3];
	short skill;
	unsigned index;

	sights[0] = sight_at(10.0f, 0.0f);
	sights[1] = sight_at(10.0f, RUN);
	sights[2] = sight_at(20.0f, WALK);
	for (skill = 1; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		for (index = 0; index < NUMBEROF(sights); index++)
		{
			CHECK(bots_camo_notice_rate(&bots_skills[skill], &sights[index]) >
				bots_camo_notice_rate(&bots_skills[skill - 1], &sights[index]),
				"%s notices no more than %s", bots_skills[skill].name, bots_skills[skill - 1].name);
			CHECK(bots_camo_lose_rate(&bots_skills[skill], &sights[index]) <
				bots_camo_lose_rate(&bots_skills[skill - 1], &sights[index]),
				"%s loses no less than %s", bots_skills[skill].name, bots_skills[skill - 1].name);
		}
	}
	/* (a fair spread: the legendary bot notices about three times as fast) */
	CHECK(bots_camo_notice_rate(&bots_skills[NUMBER_OF_BOTS_SKILLS - 1], &sights[1]) >
		2.0f * bots_camo_notice_rate(&bots_skills[0], &sights[1]), "the skills notice alike");
}

/* each giveaway makes a still enemy at range much plainer: a shot, the
camouflage's flicker (a pistol's ding, 0.1 off), a hit, a blow */
static void case_giveaways(void)
{
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *definition = &bots_skills[skill];
		struct bots_camo_sight quiet = sight_at(12.0f, 0.0f);
		struct bots_camo_sight fired = quiet, flicker = quiet, hurt = quiet, melee = quiet;
		real base = bots_camo_notice_rate(definition, &quiet);

		fired.fired = 1.0f;
		flicker.camouflage = 0.9f;
		hurt.hurt = 1.0f;
		melee.melee = TRUE;
		printf("%s at 12 units: quiet %.3f, firing %.3f, flicker %.3f, hit %.3f, a blow %.3f a second\n",
			definition->name, base, bots_camo_notice_rate(definition, &fired),
			bots_camo_notice_rate(definition, &flicker), bots_camo_notice_rate(definition, &hurt),
			bots_camo_notice_rate(definition, &melee));
		CHECK(bots_camo_notice_rate(definition, &fired) > 10.0f * base, "%s: a shot gives little away", definition->name);
		CHECK(bots_camo_notice_rate(definition, &flicker) > 10.0f * base, "%s: the flicker gives little away",
			definition->name);
		CHECK(bots_camo_notice_rate(definition, &hurt) > 10.0f * base, "%s: a hit gives little away", definition->name);
		CHECK(bots_camo_notice_rate(definition, &melee) > 10.0f * base, "%s: a blow gives little away",
			definition->name);
		/* (the giveaway of a moment never sure at one look) */
		{
			struct bots_camo_sight both = fired;

			both.camouflage = 0.9f;
			CHECK(share_within(definition, &both, FALSE, 0.1f) < 0.9f, "%s notices one shot surely",
				definition->name);
		}
	}
}

/* sometimes fooled and sometimes not: a still enemy at range rarely
noticed, but now and then; one running close by noticed soon, but not
always at once */
static void case_still_at_range(void)
{
	struct bots_camo_sight still_far = sight_at(20.0f, 0.0f);
	struct bots_camo_sight running_close = sight_at(5.0f, RUN);
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *definition = &bots_skills[skill];
		real still = share_within(definition, &still_far, FALSE, 5.0f);
		real running = share_within(definition, &running_close, FALSE, 5.0f);
		real at_once = share_within(definition, &running_close, FALSE, 0.1f);

		printf("%s: a still one at 20 units noticed within 5 seconds %.0f%%, a running one at 5 units %.0f%% "
			"(at the first look %.0f%%)\n", definition->name, still * 100.0f, running * 100.0f, at_once * 100.0f);
		CHECK(still < 0.25f, "%s notices a still one at range %.2f of the time", definition->name, still);
		CHECK(still > 0.01f, "%s never notices a still one at range", definition->name);
		CHECK(running > 0.8f, "%s notices a running one close by only %.2f of the time", definition->name, running);
		CHECK(at_once < 0.5f, "%s notices a running one at once %.2f of the time", definition->name, at_once);
	}
}

/* the centre of the view more than its edge; out of it, nothing, but one
close by and, for a bot hurt, one firing */
static void case_view(void)
{
	short skill;

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *definition = &bots_skills[skill];
		struct bots_camo_sight centre = sight_at(10.0f, WALK), edge = centre, behind = centre, close = centre;
		struct bots_camo_sight shooting = centre;

		edge.off_view = 0.95f;
		behind.off_view = 2.0f;
		close.off_view = 2.0f;
		close.distance = 2.0f;
		shooting.off_view = 2.0f;
		shooting.fired = 1.0f;
		CHECK(bots_camo_notice_rate(definition, &centre) > 2.0f * bots_camo_notice_rate(definition, &edge),
			"%s: the view's centre not plainer than its edge", definition->name);
		CHECK(bots_camo_notice_rate(definition, &edge) > 0.0f, "%s: the view's edge blind", definition->name);
		CHECK(bots_camo_notice_rate(definition, &behind) == 0.0f, "%s sees one behind it", definition->name);
		CHECK(bots_camo_notice_rate(definition, &close) > 0.0f, "%s: one close behind it unheard", definition->name);
		CHECK(bots_camo_notice_rate(definition, &shooting) == 0.0f, "%s sees one firing behind it unhurt",
			definition->name);
		shooting.bot_hurt = TRUE;
		CHECK(bots_camo_notice_rate(definition, &shooting) > 0.0f, "%s: shot from behind, never turns to it",
			definition->name);
	}
}

/* losing one it fights: more as it crosses the sight, quiet, and further;
much less while it gives itself away; less for the more skilled */
static void case_losing(void)
{
	short skill;
	real still_kept[NUMBER_OF_BOTS_SKILLS], strafe_lost[NUMBER_OF_BOTS_SKILLS];

	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *definition = &bots_skills[skill];
		struct bots_camo_sight still = sight_at(10.0f, 0.0f), strafing = sight_at(10.0f, RUN);
		struct bots_camo_sight far = sight_at(25.0f, RUN), firing = strafing;
		real firing_lost;

		firing.fired = 1.0f;
		firing.camouflage = 0.9f;
		CHECK(bots_camo_lose_rate(definition, &strafing) > 3.0f * bots_camo_lose_rate(definition, &still),
			"%s loses a strafing one no sooner than a still one", definition->name);
		CHECK(bots_camo_lose_rate(definition, &far) > bots_camo_lose_rate(definition, &strafing),
			"%s loses one further no sooner", definition->name);
		CHECK(bots_camo_lose_rate(definition, &firing) < 0.2f * bots_camo_lose_rate(definition, &strafing),
			"%s loses one firing as soon as a quiet one", definition->name);
		still_kept[skill] = 1.0f - share_within(definition, &still, TRUE, 3.0f);
		strafe_lost[skill] = share_within(definition, &strafing, TRUE, 3.0f);
		firing_lost = share_within(definition, &firing, TRUE, 3.0f);
		printf("%s over 3 seconds at 10 units: a still one kept %.0f%%, a strafing one lost %.0f%%, a firing one "
			"lost %.0f%%\n", definition->name, still_kept[skill] * 100.0f, strafe_lost[skill] * 100.0f,
			firing_lost * 100.0f);
		CHECK(strafe_lost[skill] > 0.3f && strafe_lost[skill] < 1.0f, "%s loses a strafing one %.2f of the time",
			definition->name, strafe_lost[skill]);
		CHECK(still_kept[skill] > 0.5f && still_kept[skill] < 1.0f, "%s keeps a still one %.2f of the time",
			definition->name, still_kept[skill]);
		CHECK(firing_lost < 0.2f, "%s loses a firing one %.2f of the time", definition->name, firing_lost);
	}
	CHECK(strafe_lost[0] > 0.8f, "the easy bot keeps a strafing one %.2f of the time", 1.0f - strafe_lost[0]);
	CHECK(still_kept[NUMBER_OF_BOTS_SKILLS - 1] > 0.8f, "the legendary bot loses a still one %.2f of the time",
		1.0f - still_kept[NUMBER_OF_BOTS_SKILLS - 1]);
}

/* the aim at one: no blur for none; three times quiet; plain again as it
gives itself away; the error larger and settling slower, the wander
larger */
static void case_aim(void)
{
	struct bots_camo_sight none = sight_at(10.0f, 0.0f), quiet = sight_at(10.0f, 0.0f), fired, flicker;
	short skill;

	none.camouflage = 0.0f;
	fired = quiet;
	fired.fired = 1.0f;
	flicker = quiet;
	flicker.camouflage = 0.95f;
	CHECK(bots_camo_blur(&none) == 1.0f, "a target not camouflaged blurs the aim %.3f", bots_camo_blur(&none));
	CHECK(fabsf(bots_camo_blur(&quiet) - 3.0f) < 0.01f, "a quiet camouflaged target blurs it %.3f",
		bots_camo_blur(&quiet));
	CHECK(bots_camo_blur(&fired) == 1.0f, "one firing blurs it %.3f", bots_camo_blur(&fired));
	CHECK(bots_camo_blur(&flicker) < bots_camo_blur(&quiet) && bots_camo_blur(&flicker) > bots_camo_blur(&fired),
		"a flicker's blur %.3f not between", bots_camo_blur(&flicker));
	for (skill = 0; skill < NUMBER_OF_BOTS_SKILLS; skill++)
	{
		struct bots_skill_definition const *definition = &bots_skills[skill];
		real error[2], wander[2];
		short blurred;

		for (blurred = 0; blurred < 2; blurred++)
		{
			long tick;

			reset(7);
			bot.aim_error_yaw = definition->aim_error_degrees * BOTS_DEGREES;
			wander[blurred] = 0.0f;
			for (tick = 0; tick < 4 * TICKS_PER_SECOND; tick++)
			{
				bots_aim_settle(&bot, definition, 1.0f, blurred ? bots_camo_blur(&quiet) : 1.0f, 0.0f);
				if (tick == TICKS_PER_SECOND / 2)
					error[blurred] = bot.aim_error_yaw;
				wander[blurred] = MAX(wander[blurred], sqrtf(bot.wander_yaw * bot.wander_yaw +
					bot.wander_pitch * bot.wander_pitch));
			}
		}
		printf("%s: its error half a second on %.2f degrees (%.2f camouflaged), its wander %.2f (%.2f)\n",
			definition->name, error[0] / BOTS_DEGREES, error[1] / BOTS_DEGREES, wander[0] / BOTS_DEGREES,
			wander[1] / BOTS_DEGREES);
		CHECK(error[1] > 1.2f * error[0], "%s: its error settles as fast on a camouflaged target", definition->name);
		CHECK(wander[1] > 2.5f * wander[0], "%s: its wander no larger on a camouflaged target", definition->name);
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("distance") case_distance();
	else CASE("motion") case_motion();
	else CASE("skill") case_skill();
	else CASE("giveaways") case_giveaways();
	else CASE("still-at-range") case_still_at_range();
	else CASE("view") case_view();
	else CASE("losing") case_losing();
	else CASE("aim") case_aim();
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
