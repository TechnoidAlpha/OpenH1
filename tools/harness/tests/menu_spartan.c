/*
MENU_SPARTAN.C (test)

The menus' Spartan's color: menu_screen.c's screen_spartan_frame (which
Spartans a <spartan> asks for, and where the original picture is shown)
against a fake screen, and menu_spartan.c's menu_spartan_color (a Spartan's
armor's color at a time of the menus' frame clock) against a fake color
table. test_menu_spartan.py takes the code.
*/

#include "harness.h"

#include <math.h>

typedef struct { real red, green, blue; } real_rgb_color;

/* (cseries.c's: the team games' colors) */
static real_rgb_color const team_red = { 1.0f, 0.0f, 0.0f }, team_blue = { 0.0f, 0.0f, 1.0f };
static real_rgb_color const *const global_real_rgb_red = &team_red;
static real_rgb_color const *const global_real_rgb_blue = &team_blue;

#include "menu_spartan_types.inc"

/* ---------- the fake game */

#define COLORS 18

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

enum { MENU_SCREEN_SPARTAN_FOCUS, MENU_SCREEN_SPARTAN_PROFILE, MENU_SCREEN_SPARTAN_BINDING };

struct menu_screen_block
{
	int box_x, box_y, box_width, box_height;
	int players;
	int color;
};

struct widget_instance
{
	boolean visible;
	struct { short current_frame_index; } animation;
};

struct screen_spartan
{
	struct menu_screen_block block;
	struct widget_instance *picture;
	short first;
	short places;
	boolean shown;
};

struct screen_open
{
	struct screen_spartan spartans[2];
	long spartan_count;
};

/* each player's color, as screen_spartan_colors gives them */
static short player_colors[MAXIMUM_MENU_SPARTANS];
static boolean available = TRUE;
/* the Spartans asked for (NONE: none), and their colors */
static short placed[MAXIMUM_MENU_SPARTANS];
static real placed_x[MAXIMUM_MENU_SPARTANS];

static short screen_spartan_colors(struct screen_open const *open, struct screen_spartan const *spartan, long focused,
	short *colors)
{
	short place;

	(void)open;
	(void)focused;
	for (place = 0; place < spartan->places; place++)
		colors[place] = player_colors[place];
	return spartan->places;
}

static short menu_spartan_test_colors(short colors[MAXIMUM_MENU_SPARTAN_PLACES])
{
	(void)colors;
	return 0;
}

static boolean menu_spartan_available(void)
{
	return available;
}

static void menu_spartan_place(short index, short color_index, real x, real bottom, real height, boolean lineup)
{
	(void)bottom;
	(void)height;
	(void)lineup;
	placed[index] = color_index;
	placed_x[index] = x;
}

static unsigned short player_profile_number_of_available_primary_colors(void)
{
	return COLORS;
}

/* (each color its own: its index in each channel) */
static real_rgb_color player_profile_get_rgb_color(long color_index)
{
	real_rgb_color color;

	color_index = color_index < 0 ? 0 : color_index >= COLORS ? COLORS - 1 : color_index;
	color.red = (real)color_index / (COLORS - 1);
	color.green = 1.0f - (real)color_index / (COLORS - 1);
	color.blue = (real)(color_index % 3) / 2.0f;
	return color;
}

#include "under_test.inc"

/* ---------- the cases */

static boolean same(real_rgb_color a, real_rgb_color b)
{
	return fabsf(a.red - b.red) < 1e-4f && fabsf(a.green - b.green) < 1e-4f && fabsf(a.blue - b.blue) < 1e-4f;
}

static struct widget_instance picture;
static struct screen_open open_screen;

static void frame(int players)
{
	short index;

	for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
		placed[index] = -100;
	open_screen.spartan_count = 1;
	open_screen.spartans[0].block.box_x = 380;
	open_screen.spartans[0].block.box_y = 84;
	open_screen.spartans[0].block.box_width = 200;
	open_screen.spartans[0].block.box_height = 300;
	open_screen.spartans[0].block.players = players;
	open_screen.spartans[0].block.color = MENU_SCREEN_SPARTAN_PROFILE;
	open_screen.spartans[0].picture = &picture;
	open_screen.spartans[0].first = 0;
	open_screen.spartans[0].places = (short)players;
	open_screen.spartans[0].shown = TRUE;
	picture.visible = TRUE;
	picture.animation.current_frame_index = 0;
	screen_spartan_frame(&open_screen, NONE);
}

/* the color shown most of the way through a color's step (held, before its
blend into the next) */
#define STEP (SPARTAN_RANDOM_HOLD + SPARTAN_RANDOM_BLEND)
#define HELD(step) ((unsigned long)(step) * STEP + SPARTAN_RANDOM_HOLD / 2)

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* a random color: a live Spartan, of random, and no "?" picture */
	CASE("random")
	{
		player_colors[0] = MENU_SPARTAN_RANDOM;
		frame(1);
		CHECK(placed[0] == MENU_SPARTAN_RANDOM, "random asked for no Spartan (%d)", placed[0]);
		CHECK(!picture.visible, "random's picture shown over its Spartan");
		/* (several players, each random one live) */
		player_colors[0] = 3;
		player_colors[1] = MENU_SPARTAN_RANDOM;
		player_colors[2] = MENU_SPARTAN_RANDOM;
		frame(3);
		CHECK(placed[0] == 3 && placed[1] == MENU_SPARTAN_RANDOM && placed[2] == MENU_SPARTAN_RANDOM,
			"players asked for %d %d %d", placed[0], placed[1], placed[2]);
		return 0;
	}

	/* a lineup (a binding's places, the lobby's): each place's Spartan in its
	color (a team's), at its place across the box, an empty place none, and no
	original picture */
	CASE("lineup")
	{
		short index;

		for (index = 0; index < MAXIMUM_MENU_SPARTAN_PLACES; index++)
			player_colors[index] = SCREEN_SPARTAN_EMPTY;
		player_colors[0] = MENU_SPARTAN_TEAM_RED;
		player_colors[1] = MENU_SPARTAN_TEAM_RED;
		player_colors[4] = MENU_SPARTAN_TEAM_BLUE;
		open_screen.spartans[0].block.color = MENU_SCREEN_SPARTAN_BINDING;
		for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
			placed[index] = -100;
		open_screen.spartan_count = 1;
		open_screen.spartans[0].block.box_x = 380;
		open_screen.spartans[0].block.box_y = 84;
		open_screen.spartans[0].block.box_width = 200;
		open_screen.spartans[0].block.box_height = 80;
		open_screen.spartans[0].picture = &picture;
		open_screen.spartans[0].first = 0;
		open_screen.spartans[0].places = MAXIMUM_MENU_SPARTAN_PLACES;
		open_screen.spartans[0].shown = TRUE;
		picture.visible = TRUE;
		screen_spartan_frame(&open_screen, NONE);
		CHECK(placed[0] == MENU_SPARTAN_TEAM_RED && placed[1] == MENU_SPARTAN_TEAM_RED &&
			placed[4] == MENU_SPARTAN_TEAM_BLUE, "the places' colors: %d %d %d", placed[0], placed[1], placed[4]);
		CHECK(placed[2] == -100 && placed[3] == -100 && placed[5] == -100, "an empty place asked for a Spartan");
		CHECK(fabsf(placed_x[0] - (380.0f + 200.0f * 0.5f / 8.0f)) < 0.01f &&
			fabsf(placed_x[4] - (380.0f + 200.0f * 4.5f / 8.0f)) < 0.01f, "the places across the box: %.2f %.2f",
			placed_x[0], placed_x[4]);
		CHECK(!picture.visible, "a lineup showed the original picture");
		return 0;
	}

	/* its color changes over time: each profile color in the list's order,
	about a second each, none passed over, round again; between two, a
	blend of them */
	CASE("cycle")
	{
		unsigned long round = (unsigned long)COLORS * STEP;
		real_rgb_color at_start = menu_spartan_color(0, MENU_SPARTAN_RANDOM, 0);
		real_rgb_color blended;
		short step;

		CHECK(STEP >= 500 && STEP <= 1500, "a color's step %d ms, not about a second", STEP);
		for (step = 0; step < COLORS; step++)
		{
			real_rgb_color color = menu_spartan_color(0, MENU_SPARTAN_RANDOM, HELD(step));

			CHECK(same(color, player_profile_get_rgb_color(step)), "at %lu ms not color %d (%.3f %.3f %.3f)",
				HELD(step), step, color.red, color.green, color.blue);
			CHECK(same(color, menu_spartan_color(0, MENU_SPARTAN_RANDOM, HELD(step) + round)),
				"color %d not the same a round later", step);
		}
		CHECK(!same(at_start, menu_spartan_color(0, MENU_SPARTAN_RANDOM, HELD(1))), "no change after a second");
		blended = menu_spartan_color(0, MENU_SPARTAN_RANDOM, SPARTAN_RANDOM_HOLD + SPARTAN_RANDOM_BLEND / 2);
		CHECK(blended.red > player_profile_get_rgb_color(0).red && blended.red < player_profile_get_rgb_color(1).red,
			"halfway through the blend %.3f, not between the two colors", blended.red);
		return 0;
	}

	/* Spartans side by side, each random: not the same color at once */
	CASE("offset")
	{
		short a, b;

		for (a = 0; a < MAXIMUM_MENU_SPARTAN_PLACES; a++)
		{
			for (b = (short)(a + 1); b < MAXIMUM_MENU_SPARTAN_PLACES; b++)
			{
				CHECK(!same(menu_spartan_color(a, MENU_SPARTAN_RANDOM, HELD(2)),
					menu_spartan_color(b, MENU_SPARTAN_RANDOM, HELD(2))), "Spartans %d and %d the same color", a, b);
			}
		}
		return 0;
	}

	/* a profile's own color: its own, whatever the time */
	CASE("fixed")
	{
		short color;

		for (color = 0; color < COLORS; color++)
		{
			CHECK(same(menu_spartan_color(1, color, HELD(color + 3)), player_profile_get_rgb_color(color)),
				"color %d not its own", color);
		}
		player_colors[0] = 5;
		frame(1);
		CHECK(placed[0] == 5 && !picture.visible, "color 5 asked for %d, picture %d", placed[0], picture.visible);
		return 0;
	}

	/* the "?" picture where there is no Spartan: no tags (a refused file,
	the pictures off), or no profile; none at all for a binding's none */
	CASE("fallbacks")
	{
		player_colors[0] = MENU_SPARTAN_RANDOM;
		available = FALSE;
		frame(1);
		CHECK(placed[0] == -100, "a Spartan asked for without its tags");
		CHECK(picture.visible && picture.animation.current_frame_index == SCREEN_SPARTAN_RANDOM_FRAME,
			"no tags: picture %d frame %d", picture.visible, picture.animation.current_frame_index);
		available = TRUE;
		player_colors[0] = SCREEN_SPARTAN_NO_PROFILE;
		frame(1);
		CHECK(placed[0] == -100, "a Spartan asked for with no profile");
		CHECK(picture.visible && picture.animation.current_frame_index == SCREEN_SPARTAN_RANDOM_FRAME,
			"no profile: picture %d frame %d", picture.visible, picture.animation.current_frame_index);
		player_colors[0] = SCREEN_SPARTAN_NONE;
		frame(1);
		CHECK(placed[0] == -100 && !picture.visible, "a binding's none shows something");
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
