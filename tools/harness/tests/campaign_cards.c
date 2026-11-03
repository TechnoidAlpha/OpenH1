/*
CAMPAIGN_CARDS.C (test)

Choose Mission's row of cards (port/linux/game/campaign_menu.inc): which
three are shown for the chosen one (in the middle, but at the row's ends),
where each slot's card goes (the chosen one the larger, the row in the
middle, wider apart on a wide screen), a step left or right (kept to the
row), and the slide of a moved row (eased out to nothing in its time).
test_campaign_cards.py takes the code (under_test.inc) and its constants
(config.inc).
*/

#include "harness.h"

#define EXPECT(condition) CHECK(condition, "%s", "")

#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#include "config.inc"
#include "under_test.inc"

#define LEVELS 10

static void first_centred(void)
{
	short chosen;

	for (chosen = 1; chosen < LEVELS - 1; chosen++)
		EXPECT(mission_cards_first(chosen, LEVELS) == chosen - 1);
}

static void first_at_ends(void)
{
	EXPECT(mission_cards_first(0, LEVELS) == 0);
	EXPECT(mission_cards_first(LEVELS - 1, LEVELS) == LEVELS - MISSION_SLOTS);
	/* (fewer missions than slots: all shown from the first) */
	EXPECT(mission_cards_first(1, 2) == 0);
	EXPECT(mission_cards_first(0, 0) == 0);
}

static void place_chosen_larger(void)
{
	short x[MISSION_SLOTS];
	short slot;

	for (slot = 0; slot < MISSION_SLOTS; slot++)
	{
		short total, gap = MISSION_CARD_GAP;

		mission_cards_place(MISSION_SLOTS, slot, 0, x);
		total = (short)(MISSION_BIG_WIDTH + 2 * MISSION_SMALL_WIDTH + 2 * gap);
		/* (the row in the middle of the 640) */
		EXPECT(x[0] == 320 - total / 2);
		EXPECT(x[1] - x[0] == (slot == 0 ? MISSION_BIG_WIDTH : MISSION_SMALL_WIDTH) + gap);
		EXPECT(x[2] - x[1] == (slot == 1 ? MISSION_BIG_WIDTH : MISSION_SMALL_WIDTH) + gap);
	}
}

static void place_wide_apart(void)
{
	short narrow[MISSION_SLOTS], wide[MISSION_SLOTS];

	mission_cards_place(MISSION_SLOTS, 1, 0, narrow);
	mission_cards_place(MISSION_SLOTS, 1, 212, wide);
	/* (wider apart, the middle card where it was) */
	EXPECT(wide[1] - wide[0] > narrow[1] - narrow[0]);
	EXPECT(wide[1] == narrow[1]);
	/* (the row inside the 640 at 4:3, clear of the arrows' 24 + 24 units) */
	EXPECT(narrow[0] >= 24 + 24 + 4);
	EXPECT(narrow[2] + MISSION_SMALL_WIDTH <= 640 - 24 - 24 - 4);
}

static void step_kept_to_row(void)
{
	EXPECT(mission_cards_step(0, -1, LEVELS) == 0);
	EXPECT(mission_cards_step(0, 1, LEVELS) == 1);
	EXPECT(mission_cards_step(LEVELS - 1, 1, LEVELS) == LEVELS - 1);
	EXPECT(mission_cards_step(3, -1, LEVELS) == 2);
	EXPECT(mission_cards_step(0, 1, 0) == 0);
}

static void slide_eases(void)
{
	real start = mission_cards_slide(100.0f, 0);
	real middle = mission_cards_slide(100.0f, MISSION_SLIDE_MILLISECONDS / 2);

	EXPECT(start == 100.0f);
	/* (eased out: most of the way in the first half) */
	EXPECT(middle > 0.0f && middle < 25.0f);
	EXPECT(mission_cards_slide(100.0f, MISSION_SLIDE_MILLISECONDS) == 0.0f);
	EXPECT(mission_cards_slide(-80.0f, MISSION_SLIDE_MILLISECONDS / 4) < 0.0f);
	/* (no longer than about a sixth of a second) */
	EXPECT(MISSION_SLIDE_MILLISECONDS >= 120 && MISSION_SLIDE_MILLISECONDS <= 200);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "first-centred"))
		first_centred();
	else if (!strcmp(name, "first-at-ends"))
		first_at_ends();
	else if (!strcmp(name, "place-chosen-larger"))
		place_chosen_larger();
	else if (!strcmp(name, "place-wide-apart"))
		place_wide_apart();
	else if (!strcmp(name, "step-kept-to-row"))
		step_kept_to_row();
	else if (!strcmp(name, "slide-eases"))
		slide_eases();
	else
		return 2;
	return 0;
}
