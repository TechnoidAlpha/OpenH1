/*
TALL_TITLES_PASSED.C (test)

A tall screen's column titles (the server browser's SERVER, MAP, ...) are
the pointer's alone with a controller's prompts (menu_screen_tall.inc's
tall_titles_passed): X sorts, and their row lit nothing, so A there sorted
the list in place of joining the game the row and the panel showed. The
d-pad's focus moved onto them goes on the way it came: down from the tabs to
the first row shown, up from the rows to the tabs, over what is hidden or
disabled, round the ends. With the keyboard's prompts, or the pointer's
focus, they stay a stop. test_tall_titles_passed.py takes the game's code.
*/

#include "harness.h"

struct widget_instance
{
	boolean visible;
	boolean disabled;
	struct widget_instance *parent, *child, *next, *previous, *focused_child;
};

struct screen_tall
{
	struct widget_instance *body, *titles, *focus_last;
};

static boolean pointer_focused, keyboard_prompts, search_editing;

static boolean ui_widget_port_pointer_focused(void) { return pointer_focused; }
static int halo_keyboard_prompts(void) { return keyboard_prompts; }
static boolean tall_search_editing(struct screen_tall *tall) { return search_editing; }
static void tall_focus(struct widget_instance *list, struct widget_instance *child) { list->focused_child = child; }

#include "under_test.inc"

/* the server browser's body: its band's button (hidden: the local
network's), its tabs, its titles, three rows (the last hidden: no game),
its panel's JOIN GAME and the bar */
enum { HEADER, TABS, TITLES, ROW_1, ROW_2, ROW_3, JOIN, BAR, BLOCKS };
static struct widget_instance body, blocks[BLOCKS];
static struct screen_tall tall;

static void make(void)
{
	long index;

	memset(&body, 0, sizeof(body));
	memset(blocks, 0, sizeof(blocks));
	for (index = 0; index < BLOCKS; index++)
	{
		blocks[index].visible = TRUE;
		blocks[index].parent = &body;
		blocks[index].previous = index ? &blocks[index - 1] : NULL;
		blocks[index].next = index < BLOCKS - 1 ? &blocks[index + 1] : NULL;
	}
	blocks[HEADER].visible = FALSE;
	blocks[ROW_3].visible = FALSE;
	blocks[ROW_3].disabled = TRUE;
	body.child = &blocks[HEADER];
	tall.body = &body;
	tall.titles = &blocks[TITLES];
	pointer_focused = keyboard_prompts = search_editing = FALSE;
}

/* the focus on one block, a frame, then moved by the d-pad to another, a
frame */
static struct widget_instance *moved(long from, long to)
{
	body.focused_child = &blocks[from];
	tall_titles_passed(&tall);
	body.focused_child = &blocks[to];
	tall_titles_passed(&tall);
	return body.focused_child;
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	make();
	if (!strcmp(name, "down"))
	{
		CHECK(moved(TABS, TITLES) == &blocks[ROW_1], "down from the tabs: the first row");
		return 0;
	}
	if (!strcmp(name, "up"))
	{
		CHECK(moved(ROW_1, TITLES) == &blocks[TABS], "up from the rows: the tabs");
		/* (round the top, over the hidden band's button, with no tabs) */
		make();
		blocks[TABS].disabled = TRUE;
		CHECK(moved(ROW_1, TITLES) == &blocks[BAR], "up with nothing above: round to the bar");
		return 0;
	}
	if (!strcmp(name, "kept"))
	{
		keyboard_prompts = TRUE;
		CHECK(moved(TABS, TITLES) == &blocks[TITLES], "the keyboard's arrows still stop on the titles");
		make();
		pointer_focused = TRUE;
		CHECK(moved(TABS, TITLES) == &blocks[TITLES], "the pointer still focuses the titles");
		make();
		/* (staying on them, as a search typed into keeps them) */
		search_editing = TRUE;
		CHECK(moved(TABS, TITLES) == &blocks[TITLES], "a search typed into keeps them");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
