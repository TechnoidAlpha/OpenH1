/*
PAUSE_BOX_FIT.C (test)

The pause boxes' width (port/linux/game/menu_tags.c's pause_boxes_fit): the
Xbox's while a controller's buttons are drawn under the rows (issue 751),
made wider for the keyboard's keys' caps only while they are drawn (issue
596), and the Xbox's again once a controller plays; the mission objectives
beside the campaign's box moved with its right, and back.
test_pause_box_fit.py takes the code.
*/

#include "harness.h"

/* the fake world: the widgets the code reads (a box of its three pieces,
the screen the objectives are in), each tag a widget of a name */
struct tag_reference { long index; };
struct ui_widget_child_reference { struct tag_reference widget_tag; short horizontal_offset; };
struct rectangle { short y0, x0, y1, x1; };
struct widget_block { long count; void *address; };
struct ui_widget_definition { struct rectangle bounds; struct widget_block child_widgets; };

enum
{
	_left, _center, _right, _box, _objective_text, _objectives_caption, _screen, _split_screen, TAGS
};
static char const *const names[TAGS] = {
	"ui\\shell\\bitmaps\\pausebox2_left", "ui\\shell\\bitmaps\\pausebox2_left_center",
	"ui\\shell\\bitmaps\\pausebox2_right", "ui\\shell\\box", "ui\\shell\\mission_objective_text",
	"ui\\shell\\mission_objectives_caption", "ui\\shell\\pause_game", "ui\\shell\\pause_game_split_screen",
};
static struct ui_widget_definition widgets[TAGS];

#define UI_WIDGET_DEFINITION_TAG 0
#define tag_get(group, tag) (&widgets[(tag)])

static boolean tag_name_ends(long tag, char const *end)
{
	size_t length = strlen(names[tag]), end_length = strlen(end);

	return length >= end_length && !strcmp(names[tag] + length - end_length, end);
}

/* (__attribute__((unused)): the box read under test may not ask; newer clangs warn of a global set and never read) */
static int keyboard_prompts __attribute__((unused));
#define halo_keyboard_prompts() keyboard_prompts

#include "under_test.inc"

/* the Xbox's box (the multiplayer pause menu's: 226 wide) and the screen
whose objectives are beside it */
static struct ui_widget_child_reference pieces[3] = { { { _left }, 0 }, { { _center }, 16 }, { { _right }, 210 } };
static struct ui_widget_child_reference screen_children[2] = { { { _objective_text }, 240 },
	{ { _objectives_caption }, 250 } };
static struct ui_widget_child_reference split_children[2] = { { { _objective_text }, 240 },
	{ { _objectives_caption }, 250 } };

static void world_reset(void)
{
	memset(widgets, 0, sizeof(widgets));
	widgets[_center].bounds.x1 = 194;
	widgets[_box].bounds.x1 = 226;
	widgets[_box].child_widgets.count = 3;
	widgets[_box].child_widgets.address = pieces;
	widgets[_screen].child_widgets.count = 2;
	widgets[_screen].child_widgets.address = screen_children;
	widgets[_split_screen].child_widgets.count = 2;
	widgets[_split_screen].child_widgets.address = split_children;
	pieces[0].horizontal_offset = 0;
	pieces[1].horizontal_offset = 16;
	pieces[2].horizontal_offset = 210;
	screen_children[0].horizontal_offset = split_children[0].horizontal_offset = 240;
	screen_children[1].horizontal_offset = split_children[1].horizontal_offset = 250;
	memset(&menu_tags, 0, sizeof(menu_tags));
}

/* the box's drawn width: its left piece's left to its right piece's right
(the right piece 16 wide) */
static short box_width(void)
{
	return (short)(pieces[2].horizontal_offset + 16 - pieces[0].horizontal_offset);
}

static boolean box_stock(void)
{
	return pieces[0].horizontal_offset == 0 && pieces[1].horizontal_offset == 16 &&
		pieces[2].horizontal_offset == 210 && widgets[_center].bounds.x1 == 194 && widgets[_box].bounds.x1 == 226 &&
		screen_children[0].horizontal_offset == 240 && split_children[1].horizontal_offset == 250;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("controller")
	{
		/* kept at the map's load, drawn while a controller plays: the
		Xbox's box, however many frames */
		world_reset();
		pause_box_keep(&widgets[_box], &widgets[_screen], &widgets[_split_screen], 56);
		CHECK(box_stock(), "the box changed as it was kept: %d wide", box_width());
		keyboard_prompts = 0;
		pause_boxes_fit();
		pause_boxes_fit();
		CHECK(box_stock(), "the box %d wide while a controller plays, not the Xbox's 226", box_width());
		return 0;
	}
	CASE("keyboard")
	{
		/* while the keyboard plays: as much wider as its legends, half
		either side, the centre strip with it, the objectives with the
		right; once only, however many frames */
		world_reset();
		pause_box_keep(&widgets[_box], &widgets[_screen], &widgets[_split_screen], 56);
		keyboard_prompts = 1;
		pause_boxes_fit();
		pause_boxes_fit();
		CHECK(box_width() == 226 + 56, "the box %d wide while the keyboard plays, not %d", box_width(), 226 + 56);
		CHECK(pieces[0].horizontal_offset == -28 && pieces[2].horizontal_offset == 210 + 28,
			"not half either side: %d and %d", pieces[0].horizontal_offset, pieces[2].horizontal_offset);
		CHECK(widgets[_center].bounds.x1 == 194 + 56, "the centre strip %d wide", widgets[_center].bounds.x1);
		CHECK(screen_children[0].horizontal_offset == 240 + 28 && split_children[1].horizontal_offset == 250 + 28,
			"the objectives at %d and %d", screen_children[0].horizontal_offset, split_children[1].horizontal_offset);
		return 0;
	}
	CASE("back")
	{
		/* the keyboard, then a controller: the Xbox's box again, exactly */
		world_reset();
		pause_box_keep(&widgets[_box], &widgets[_screen], &widgets[_split_screen], 56);
		keyboard_prompts = 1;
		pause_boxes_fit();
		keyboard_prompts = 0;
		pause_boxes_fit();
		CHECK(box_stock(), "the box %d wide once a controller plays again", box_width());
		keyboard_prompts = 1;
		pause_boxes_fit();
		CHECK(box_width() == 226 + 56, "the box %d wide when the keyboard plays again", box_width());
		return 0;
	}
	CASE("none")
	{
		/* no box kept (legends no wider, or a custom map's): nothing moves */
		world_reset();
		pause_box_keep(&widgets[_box], NULL, NULL, 0);
		keyboard_prompts = 1;
		pause_boxes_fit();
		CHECK(box_stock(), "a box kept with no growth is %d wide", box_width());
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
