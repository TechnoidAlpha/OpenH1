/*
CINEMATIC_TITLES.C (test)

The real cinematic_render (cinematics.c) over a fake scenario with one title, as
some Custom Edition maps show their name for the whole game: drawn in the game,
not over a menu (the screen's overlays are drawn after the players' menus), and
its time going on under one. On a screen wider than 640 columns, the edge the
title is justified to (its middle, if centered) keeps its place relative to the
screen's sides, as on the Xbox's 4:3 screen. test_cinematic_titles.py takes the
structures, the slots' count (config.inc) and the code (under_test.inc).
*/

#include <math.h>
#include "harness.h"

typedef unsigned short word;
typedef unsigned long pixel32;
typedef struct { short y0, x0, y1, x1; } rectangle2d;
typedef struct { real alpha, red, green, blue; } real_argb_color;
#define TAG_STRING_LENGTH 31
#define TICKS_PER_SECOND 30
#define _real_epsilon 0.0001f
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(n, low, high) ((n) < (low) ? (low) : ((n) > (high) ? (high) : (n)))
#define fast_ftol(value) ((long)(value))

#include "config.inc"

static struct cinematic_global_data globals_data;
static struct cinematic_global_data *cinematic_globals = &globals_data;

/* a menu (any local player's widget) is up or not */
static boolean menu_up;
static boolean ui_widgets_active(void) { return menu_up; }

/* the game's clock: one tick a frame */
static long ticks;
#define game_time_get() (ticks)
#define game_time_get_paused() FALSE
#define game_time_get_elapsed() 1
#define game_in_editor() FALSE

/* the letterbox's camera and bars */
static struct { struct { rectangle2d viewport_bounds; } camera; } render;
static long quads;
static void draw_quad(rectangle2d const *bounds, pixel32 color) { quads++; }
/* (debug.render_marks: nothing here) */
#define RENDER_MARK_CINEMATIC_LETTERBOX 15
static void render_marks_kind(int kind, char const *label) { (void)kind; (void)label; }

/* the HUD's font and the scenario's one title, its text in the ingame help text */
static struct
{
	struct { struct { long index; } single_player_font; } messaging;
	struct { rectangle2d default_title_bounds; } defaults;
} hud_globals_data = { { { 3 } } }, *hud_globals = &hud_globals_data;
struct tag_block { long count; void *address; };
static struct scenario_cutscene_title titles[1];
static struct
{
	struct tag_block cutscene_chapter_titles;
	struct { long index; } ingame_help_text;
} scenario = { { 1, titles }, { 7 } };
#define global_scenario_get() (&scenario)
#define TAG_BLOCK_GET_ELEMENT(block, element, type) (&((type *)(block)->address)[(element)])
struct string_list { struct { long count; } strings; };
static struct string_list help_text = { { 1 } };
#define unicode_string_list_definition_get(tag_index) (&help_text)
#define unicode_string_list_get_string(tag_index, string_index) NULL

/* the text drawn: recorded */
static long draws;
static real drawn_alpha;
static pixel32 shadow, drawn_shadow;
static void pixel32_to_real_argb_color(pixel32 pixel, real_argb_color *color)
{
	color->alpha = (real)((pixel >> 24) & 0xFF) / 255.0f;
	color->red = (real)((pixel >> 16) & 0xFF) / 255.0f;
	color->green = (real)((pixel >> 8) & 0xFF) / 255.0f;
	color->blue = (real)(pixel & 0xFF) / 255.0f;
}
static void draw_string_set_draw_mode(long font_index, short style, short justification, unsigned long flags,
	real_argb_color const *color)
{
	drawn_alpha = color->alpha;
}
static void rasterizer_text_set_shadow_color(pixel32 color) { shadow = color; }
/* the screen's width in the 480 lines (640 the Xbox's 4:3), and where the title was drawn */
static short screen_width = 640;
static short halo_screen_width(void) { return screen_width; }
static rectangle2d drawn_bounds;
#define rasterizer_draw_unicode_string(bounds, style, color, flags, string) \
	(draws++, drawn_shadow = shadow, drawn_bounds = *(bounds))

/* MCC title adaptation is inactive for these Xbox/CE cases. */
static boolean mcc_cache_tags_loaded(void) { return FALSE; }
static void mcc_hud_title_bounds(rectangle2d *bounds, short justification, unsigned long flags) { }
#include "under_test.inc"

/* the title in its slot, at its start: up for up_time ticks, then fading out for fade_out_time */
static void queue_title(real up_time, real fade_out_time)
{
	short slot;

	for (slot = 0; slot < MAXIMUM_QUEUED_CINEMATIC_TITLES; slot++)
	{
		globals_data.queued_titles[slot].title_index = NONE;
		globals_data.queued_titles[slot].time = NONE;
	}
	globals_data.queued_titles[0].title_index = 0;
	globals_data.queued_titles[0].time = 0;
	titles[0].bounds.x0 = 400;
	titles[0].bounds.x1 = 640;
	titles[0].bounds.y0 = 440;
	titles[0].bounds.y1 = 480;
	titles[0].foreground_color = 0xFF80C0FF;
	titles[0].shadow_color = 0xFF000000;
	titles[0].fade_in_time = 0.0f;
	titles[0].up_time = up_time;
	titles[0].fade_out_time = fade_out_time;
	titles[0].justification = _cinematic_title_justified_right;
}

/* one frame: the draws it made */
static long frame(void)
{
	draws = 0;
	ticks++;
	cinematic_render();
	return draws;
}

/* a title (a10's chapter titles' bounds, 0-597, at a justification) drawn once on a screen that wide: its bounds */
static rectangle2d drawn_at(short width, short justification)
{
	queue_title(1000000.0f, 1.0f);
	titles[0].bounds.x0 = 0;
	titles[0].bounds.x1 = 597;
	titles[0].justification = justification;
	screen_width = width;
	CHECK(frame() == 1, "the title was not drawn");
	return drawn_bounds;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct cinematic_title const *slot = &globals_data.queued_titles[0];
	long index;

	/* a title for the whole game, no menu: drawn each frame, opaque */
	CASE("drawn-in-the-game")
	{
		queue_title(1000000.0f, 1.0f);
		for (index = 0; index < 3; index++)
			CHECK(frame() == 1, "the title was drawn %ld times in frame %ld", draws, index);
		CHECK(drawn_alpha > 0.99f, "the title was drawn with alpha %f", drawn_alpha);
		CHECK(drawn_shadow >> 24 == 0xFF, "the title's shadow was drawn with alpha %lu", drawn_shadow >> 24);
		CHECK(quads == 0, "%ld letterbox bars drawn without a letterbox", quads);
		return 0;
	}
	/* a menu up: the title is not drawn over it, and is again once it is gone */
	CASE("not-over-a-menu")
	{
		queue_title(1000000.0f, 1.0f);
		menu_up = TRUE;
		for (index = 0; index < 3; index++)
			CHECK(frame() == 0, "the title was drawn over a menu in frame %ld", index);
		menu_up = FALSE;
		CHECK(frame() == 1, "the title was not drawn after the menu was gone");
		return 0;
	}
	/* a menu up: the title's time goes on, so it ends when it would have */
	CASE("time-goes-on-under-a-menu")
	{
		queue_title(5.0f, 5.0f);
		menu_up = TRUE;
		for (index = 0; index < 3; index++)
			frame();
		CHECK(slot->time == 3, "the title's time is %d after 3 frames under a menu", slot->time);
		for (index = 0; index < 7; index++)
			frame();
		CHECK(slot->title_index == NONE, "the title's slot is still taken after its 10 ticks under a menu");
		menu_up = FALSE;
		CHECK(frame() == 0, "an ended title was drawn after the menu was gone");
		return 0;
	}
	/* the Xbox's 4:3: where the bounds say */
	CASE("xbox-screen")
	{
		short justification;

		for (justification = 0; justification < 3; justification++)
		{
			rectangle2d bounds = drawn_at(640, justification);

			CHECK(bounds.x0 == 0 && bounds.x1 == 597, "a title justified %d drawn at %d-%d on a 4:3 screen",
				justification, bounds.x0, bounds.x1);
		}
		return 0;
	}
	/* a wider screen (16:9's 852 columns, 21:9's 1138, 32:9's 1706, the widest's 1920): a right-justified title
	keeps its 43 columns from the letterbox's right end, a left-justified one its place from the left end, a
	centred one stays in the middle */
	CASE("wide-screens")
	{
		static short const widths[] = { 852, 1138, 1706, 1920 };
		short index;

		for (index = 0; index < 4; index++)
		{
			short width = widths[index];
			rectangle2d right = drawn_at(width, _cinematic_title_justified_right);
			rectangle2d left = drawn_at(width, _cinematic_title_justified_left);
			rectangle2d centre = drawn_at(width, _cinematic_title_justified_center);

			CHECK(width - right.x1 == 640 - 597 && right.x1 - right.x0 == 597,
				"a right-justified title drawn at %d-%d on a screen %d wide", right.x0, right.x1, width);
			CHECK(left.x0 == 0 && left.x1 == 597, "a left-justified title drawn at %d-%d on a screen %d wide",
				left.x0, left.x1, width);
			CHECK(centre.x0 + centre.x1 - width == 597 - 640 && centre.x1 - centre.x0 == 597,
				"a centred title drawn at %d-%d on a screen %d wide", centre.x0, centre.x1, width);
		}
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
