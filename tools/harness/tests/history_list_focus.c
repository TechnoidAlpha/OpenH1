/*
HISTORY_LIST_FOCUS.C (test)

A screen opened again from the menus' history (B or Back on the screen
after it) gives the focus back to the widget that opened the next one
(ui_widget.c's widget_instance_set_focused_child_by_index). The map and
gametype lists open it themselves (their own handler, not a row's), so the
list is that widget: it must come back focused on a row of its own, the one
its "created" handler focused (the map chosen last), else its first that
takes events, so that A and Start act at once. Giving the focus clears the
old focus's path, and the list, which has handlers of its own, was left with
no row: no row lit, and A did nothing until the D-pad moved.
test_history_list_focus.py takes the game's code.
*/

#include "harness.h"

#define _ui_widget_type_container 0
#define _ui_widget_type_text_box 1
#define _ui_widget_type_spinner_list 2
#define _ui_widget_type_column_list 3

#define _widget_pass_unhandled_events_to_children_bit 0
#define _widget_dpad_updown_tabs_thru_children_bit 3
#define _widget_dpad_leftright_tabs_thru_children_bit 4
#define _widget_dont_focus_a_specific_child_bit 7

struct widget_instance
{
	long definition_tag_index;
	short type;
	boolean disabled;
	boolean visible;
	struct widget_instance *parent;
	struct widget_instance *child;
	struct widget_instance *next;
	struct widget_instance *previous;
	struct widget_instance *focused_child;
	struct { struct { short selected_index; } list; } parameters;
};

struct ui_widget_definition
{
	long flags;
	struct { long count; } event_handlers;
	struct { long count; } child_widgets;
};

/* the screen's widgets' definitions, by tag */
enum { TAG_WRAPPER, TAG_SCREEN, TAG_BACKGROUND, TAG_LIST, TAG_ROW, TAG_PLAIN_LIST, TAG_BAR, NUMBER_OF_TAGS };
static struct ui_widget_definition definitions[NUMBER_OF_TAGS];

static struct ui_widget_definition *ui_widget_definition_get(long tag_index)
{
	return &definitions[tag_index];
}

static boolean pc_menu_tag(long tag_index)
{
	return TRUE;
}

#include "under_test.inc"

#define ROWS 5
static struct widget_instance wrapper, screen, background, list, rows[ROWS], bar;

static void link(struct widget_instance *parent, struct widget_instance *child)
{
	struct widget_instance **at = &parent->child;
	struct widget_instance *last = NULL;

	while (*at)
	{
		last = *at;
		at = &(*at)->next;
	}
	*at = child;
	child->previous = last;
	child->parent = parent;
}

/* Select Map as it opens: a wrapper, its screen, a background and the list
(its rows: the map kind's chooser, the maps, and the button bar), its
"created" focusing the map used last; the screen's focus on the list, as
the screen was made */
static void open_map_select(long list_tag, short focused_row)
{
	long index;

	memset(&wrapper, 0, sizeof(wrapper));
	memset(&screen, 0, sizeof(screen));
	memset(&background, 0, sizeof(background));
	memset(&list, 0, sizeof(list));
	memset(&bar, 0, sizeof(bar));
	memset(rows, 0, sizeof(rows));
	memset(definitions, 0, sizeof(definitions));
	definitions[TAG_WRAPPER].flags = 1 << _widget_pass_unhandled_events_to_children_bit;
	definitions[TAG_SCREEN].flags = 1 << _widget_pass_unhandled_events_to_children_bit;
	definitions[TAG_SCREEN].event_handlers.count = 3;
	definitions[TAG_LIST].flags = (1 << _widget_pass_unhandled_events_to_children_bit) |
		(1 << _widget_dpad_updown_tabs_thru_children_bit);
	definitions[TAG_LIST].event_handlers.count = 5;
	definitions[TAG_PLAIN_LIST].flags = 1 << _widget_pass_unhandled_events_to_children_bit;
	definitions[TAG_PLAIN_LIST].event_handlers.count = 5;
	definitions[TAG_ROW].event_handlers.count = 3;
	definitions[TAG_BAR].flags = 1 << _widget_pass_unhandled_events_to_children_bit;
	wrapper.definition_tag_index = TAG_WRAPPER;
	screen.definition_tag_index = TAG_SCREEN;
	background.definition_tag_index = TAG_BACKGROUND;
	list.definition_tag_index = list_tag;
	list.type = _ui_widget_type_column_list;
	bar.definition_tag_index = TAG_BAR;
	bar.type = _ui_widget_type_column_list;
	wrapper.visible = screen.visible = background.visible = list.visible = bar.visible = TRUE;
	link(&wrapper, &screen);
	link(&screen, &background);
	link(&screen, &list);
	for (index = 0; index < ROWS; index++)
	{
		rows[index].definition_tag_index = TAG_ROW;
		rows[index].visible = TRUE;
		link(&list, &rows[index]);
	}
	link(&list, &bar);
	wrapper.focused_child = &screen;
	screen.focused_child = &list;
	if (focused_row != NONE)
	{
		list.focused_child = &rows[focused_row];
		list.parameters.list.selected_index = focused_row;
	}
}

/* the screen's place in the history: the list opened the next screen, so
its parent's tag and its index there */
static void back(void)
{
	widget_instance_set_focused_child_by_index(TAG_SCREEN, &wrapper, 1);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "kept"))
	{
		/* (the map used last: the third row) */
		open_map_select(TAG_LIST, 3);
		back();
		CHECK(wrapper.focused_child == &screen && screen.focused_child == &list, "the list has the focus again");
		CHECK(list.focused_child == &rows[3], "the list's own row (the map used last) has the focus");
		CHECK(list.parameters.list.selected_index == 3, "the list's index is its row's");
		return 0;
	}
	if (!strcmp(name, "first"))
	{
		/* (none focused as it was made; the chooser row hidden) */
		open_map_select(TAG_LIST, NONE);
		rows[0].visible = FALSE;
		back();
		CHECK(screen.focused_child == &list, "the list has the focus again");
		CHECK(list.focused_child == &rows[1], "the list's first row that takes events has the focus");
		CHECK(list.parameters.list.selected_index == 1, "the list's index is its row's");
		return 0;
	}
	if (!strcmp(name, "other-list"))
	{
		/* (a list the d-pad does not step through its children keeps the
		game's own way) */
		open_map_select(TAG_PLAIN_LIST, NONE);
		back();
		CHECK(screen.focused_child == &list, "the list has the focus again");
		CHECK(list.focused_child == NULL, "a list that steps no children is given none");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
