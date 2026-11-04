/*
BASIC_TRAINING.C (test)

The VR build's Basic Training's logic (port/linux/game/basic_training.c):
each step's check of what the player did, the chapters skipped to forwards
and back past those not made yet, the step a chapter jumped to begins at,
the steps' table (every chapter made ends in a challenge, a button for each
"%s"), the finished chapters' setting, and from port/linux/game/bots.c where
an ordered bot goes. test_basic_training.py takes the code (under_test.inc)
and the constants (config.inc).
*/

#include "harness.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define NUMBEROF(a) ((long)(sizeof(a) / sizeof((a)[0])))
#define TICKS_PER_SECOND 30

#include "config.inc"
#include "under_test.inc"

static struct basic_training_step step_of(short check, short amount)
{
	struct basic_training_step step;

	memset(&step, 0, sizeof(step));
	step.check = check;
	step.amount = amount;
	step.button = NONE;
	return step;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct basic_training_seen seen;
	struct basic_training_step step;
	short index;

	memset(&seen, 0, sizeof(seen));
	seen.vr = TRUE;

	if (!strcmp(case_name, "step-checks"))
	{
		/* (each waits with nothing done, and is done at its amount, not one short) */
		step = step_of(_training_check_turn, 90);
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "turn: nothing turned");
		seen.turned = 89.0f;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "turn: 89 degrees");
		seen.turned = 90.0f;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "turn: 90 degrees");
		step = step_of(_training_check_move, 4);
		seen.moved = 3.9f;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "move: short");
		seen.moved = 4.0f;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "move: far enough");
		step = step_of(_training_check_spots, 3);
		seen.spots = 2;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "spots: two of three");
		seen.spots = 3;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "spots: all three");
		step = step_of(_training_check_fire, 6);
		seen.rounds_fired = 5;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "fire: five rounds");
		seen.rounds_fired = 6;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "fire: six rounds");
		step = step_of(_training_check_loadout, 2);
		seen.sniper_held = seen.pistol_held = seen.sniper_hit = TRUE;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "loadout: no pistol hit");
		seen.pistol_hit = TRUE;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "loadout: both hit");
		step = step_of(_training_check_group, 2);
		seen.grenade_hits = 3;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "group: no blow yet");
		seen.melee_hits = 1;
		seen.grenade_hits = 1;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "group: one grenaded");
		seen.grenade_hits = 2;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "group: two and a blow");
		return 0;
	}
	if (!strcmp(case_name, "headset-steps"))
	{
		/* (the fit check and the foregrip wait for the headset's hands; out
		of it, they pass at once) */
		step = step_of(_training_check_fit, 0);
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "fit: screen open");
		seen.fitted = TRUE;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "fit: chosen");
		step = step_of(_training_check_grip, 1);
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "grip: not held");
		seen.vr = FALSE;
		seen.fitted = FALSE;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "grip: flat screen");
		step = step_of(_training_check_fit, 0);
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "fit: flat screen");
		return 0;
	}
	if (!strcmp(case_name, "targets-dry"))
	{
		step = step_of(_training_check_targets, 3);
		seen.targets_down = 2;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "two down, rounds left");
		seen.magazine_dry = TRUE;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_failed, "two down, dry: failed");
		seen.targets_down = 3;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "the last down with the last round");
		return 0;
	}
	if (!strcmp(case_name, "score"))
	{
		step = step_of(_training_check_score, 2);
		seen.kills = 1;
		seen.enemy_kills = 1;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_waiting, "1 to 1");
		seen.enemy_kills = 2;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_failed, "they reached two");
		seen.enemy_kills = 1;
		seen.kills = 2;
		CHECK(basic_training_step_result(&step, &seen) == _training_result_done, "the player reached two");
		return 0;
	}
	if (!strcmp(case_name, "chapters"))
	{
		CHECK(!basic_training_chapter_ready(TRAINING_CHAPTER_SNIPER) && !basic_training_chapter_ready(TRAINING_CHAPTER_WARTHOG),
			"5 and 6 are the next drop's");
		CHECK(!basic_training_chapter_ready(-1) && !basic_training_chapter_ready(BASIC_TRAINING_CHAPTERS), "out of range");
		CHECK(basic_training_next_chapter(0, 1) == 1, "0 -> 1");
		CHECK(basic_training_next_chapter(4, 1) == 7, "4 -> 7, past 5 and 6: %d", basic_training_next_chapter(4, 1));
		CHECK(basic_training_next_chapter(7, 1) == NONE, "7 is the last");
		CHECK(basic_training_next_chapter(7, -1) == 4, "back from 7 -> 4");
		CHECK(basic_training_next_chapter(0, -1) == NONE, "0 is the first");
		return 0;
	}
	if (!strcmp(case_name, "jumps"))
	{
		/* a jump to any chapter made starts at its first step (or the step
		asked for, kept within it), whatever was played before */
		short chapter;

		for (chapter = 0; chapter < BASIC_TRAINING_CHAPTERS; chapter++)
		{
			short count;
			short first = basic_training_first_step(chapter, &count);
			short at = basic_training_jump_step(chapter, 0);

			if (!basic_training_chapter_ready(chapter))
			{
				CHECK(at == NONE && count == 0, "chapter %d has no steps yet", chapter);
				continue;
			}
			CHECK(at == first && at != NONE && basic_training_steps[at].chapter == chapter, "chapter %d's first", chapter);
			CHECK(basic_training_jump_step(chapter, count - 1) == first + count - 1, "chapter %d's last", chapter);
			CHECK(basic_training_jump_step(chapter, count + 5) == first + count - 1,
				"chapter %d: a step past its last is its last (%d)", chapter, basic_training_jump_step(chapter, count + 5));
			CHECK(basic_training_jump_step(chapter, -3) == first, "chapter %d: before its first", chapter);
		}
		return 0;
	}
	if (!strcmp(case_name, "steps-table"))
	{
		short chapter;

		/* (in chapter order, each chapter's steps together) */
		for (index = 1; index < TRAINING_STEP_COUNT; index++)
		{
			CHECK(basic_training_steps[index].chapter >= basic_training_steps[index - 1].chapter,
				"step %d out of order", index);
		}
		for (index = 0; index < TRAINING_STEP_COUNT; index++)
		{
			struct basic_training_step const *each = &basic_training_steps[index];
			boolean named = strstr(each->words, "%s") != NULL;

			CHECK(!named || each->button != NONE,
				"step %d names a button it has not", index);
			CHECK(each->check >= 0 && each->check < NUMBER_OF_TRAINING_CHECKS, "step %d's check", index);
			CHECK(each->setup >= 0 && each->setup < NUMBER_OF_TRAINING_SETUPS, "step %d's set up", index);
			CHECK(each->button == NONE || (each->button >= 0 && each->button < NUMBER_OF_TRAINING_BUTTONS),
				"step %d's button", index);
			CHECK(strlen(each->done) > 0 && strlen(each->words) < 200, "step %d's words", index);
		}
		for (chapter = 0; chapter < BASIC_TRAINING_CHAPTERS; chapter++)
		{
			short count;
			short first = basic_training_first_step(chapter, &count);

			if (!basic_training_chapter_ready(chapter))
				continue;
			CHECK(first != NONE, "chapter %d has steps", chapter);
			CHECK(basic_training_steps[first + count - 1].challenge, "chapter %d ends with a challenge", chapter);
			CHECK(basic_training_chapters[chapter].name[0] && basic_training_chapters[chapter].about[0],
				"chapter %d's name", chapter);
		}
		return 0;
	}
	if (!strcmp(case_name, "finished"))
	{
		char text[64];

		CHECK(basic_training_finished_parse("") == 0, "none");
		CHECK(basic_training_finished_parse("0,1,4") == ((1UL << 0) | (1UL << 1) | (1UL << 4)), "0,1,4");
		CHECK(basic_training_finished_parse("7, x,8,99") == (1UL << 7),
			"only chapters that are (%lx)", basic_training_finished_parse("7, x,8,99"));
		basic_training_finished_text((1UL << 0) | (1UL << 3) | (1UL << 7), text, sizeof(text));
		CHECK(!strcmp(text, "0,3,7"), "written: %s", text);
		CHECK(basic_training_finished_parse(text) == ((1UL << 0) | (1UL << 3) | (1UL << 7)), "read back");
		basic_training_finished_text(0, text, sizeof(text));
		CHECK(!text[0], "none written");
		return 0;
	}
	if (!strcmp(case_name, "words"))
	{
		char text[128];

		basic_training_words("Pull %s to fire.", "Right Hand Trigger", text, sizeof(text));
		CHECK(!strcmp(text, "Pull Right Hand Trigger to fire."), "%s", text);
		basic_training_words("Move out.", "A", text, sizeof(text));
		CHECK(!strcmp(text, "Move out."), "%s", text);
		return 0;
	}
	if (!strcmp(case_name, "order-goal"))
	{
		struct bots_order order;
		real_point3d goal;
		boolean exposed;

		memset(&order, 0, sizeof(order));
		order.post.x = 1.0f;
		order.other.x = 9.0f;
		order.period_ticks = 60;
		order.kind = _bots_order_hold;
		bots_order_goal(&order, 75, &goal, &exposed);
		CHECK(goal.x == 1.0f && exposed, "hold: at its post");
		order.kind = _bots_order_strafe;
		bots_order_goal(&order, 10, &goal, &exposed);
		CHECK(goal.x == 9.0f, "strafe: out first");
		bots_order_goal(&order, 70, &goal, &exposed);
		CHECK(goal.x == 1.0f, "strafe: then back");
		bots_order_goal(&order, 130, &goal, &exposed);
		CHECK(goal.x == 9.0f, "strafe: and out again");
		order.kind = _bots_order_peek;
		bots_order_goal(&order, 10, &goal, &exposed);
		CHECK(goal.x == 1.0f && !exposed, "peek: behind cover first");
		bots_order_goal(&order, 70, &goal, &exposed);
		CHECK(goal.x == 9.0f && exposed, "peek: then out");
		order.period_ticks = 0;
		bots_order_goal(&order, 30, &goal, &exposed);
		CHECK(!exposed, "peek: two seconds by default");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
