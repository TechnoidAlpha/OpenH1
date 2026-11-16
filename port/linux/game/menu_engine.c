/*
MENU_ENGINE.C

The port's menu engine (docs/menu_engine.md), under display.menu_engine
"new": the generated screens of the PC menus (port/assets/menus, the widgets
menu_tags.c builds) run from the menus' model (port/assets/menus/model,
halo_menus.h), the in-game pause menu a hand-written screen in place of the
map's (menu_screen.c), and the Xbox's menus (display.menus "xbox") still the
game's.

A screen is one widget of the game's with no parent, its stand-in, which
ui_widget.c opens, keeps in its history and closes as its own (its hooks:
menu_engine_screen_opened and _closed). What is in it is the engine's: it
lays it out and draws it (menu_engine_screen_draw), each widget with the
game's own drawing (ui_widget_port_render_node: its picture, text, spinner
or list, as ui_widget.c draws them, so the look is the same), and takes its
events (menu_engine_screen_event). The widgets are the game's struct
widget_instance (menu_widget.h), so that the menus' functions
(menu_functions.c), which read and write them by name, run alike on both.

Each widget is drawn from its look in the model (model/look: its box, wide
layout, places, pictures, text and its style, and where its children are),
not its definition: ui_widget.c finds every widget's definition through
menu_engine_definition, which gives it the one menu_tags.c made from the
model (pc_menu_look_definition). From the model the engine also takes what
the widgets cannot say: a settings screen's rows and sections, their order
and the geometry they are laid out by (menu_engine_settings_layout, in place
of settings_rows_pack's packing), each row's help and note
(menu_engine_row_help, in place of the help found by its label's index + 1);
and how each widget takes its events and what they do, its behaviour
(model/behaviour: its flags and its <on> handlers, which menu_tags.c makes
the game's), in place of its definition's.
*/

#include "cseries.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "interface/ui_widget_definitions.h"
#include "networking/network_connection.h"
#include "tag_files/tag_groups.h"

#include "halo_menus.h"
#include "menu_engine.h"
#include "menu_spartan.h"
#include "menu_search.h"
#include "menu_widget.h"

#include <stdio.h>
#include <string.h>

char const *config_string(char const *name);
unsigned long config_changes(void);
void *pc_menu_look_definition(long tag_index);
char *tag_get_name(long tag_index);
boolean pc_menu_tag(long tag_index);
void platform_log(char const *format, ...);
void main_goto_main_menu(void);
/* ui_widget.c's: whether the pointer moved the focus last, the frame's tap
targets noted so far and those from the first cut to a clip; menu_shapes.c's
bar */
boolean ui_widget_port_pointer_focused(void);
long ui_widget_port_targets_noted(void);
void ui_widget_port_targets_clip(long first, rectangle2d const *clip);
void menu_shape_caret(real x, real y0, real y1, real width, pixel32 color);
void ui_widget_port_cut_set(rectangle2d const *cut);
/* menu_tags.c's: the model's widgets' behaviours made the game's */
long menu_tags_behaviour_flags(struct halo_menu_model_behaviour const *behaviour);
void const *menu_tags_behaviour_handlers(struct halo_menu_model const *model,
	struct halo_menu_model_behaviour const *behaviour, long *count);
short menu_tags_handler_event(void const *handlers, long index);
void *menu_tags_handler(void const *handlers, long index);
/* ui_widget.c's: a widget of a tag made a child of a widget, before another */
struct widget_instance *ui_widget_port_add_child(struct widget_instance *parent, long tag_index,
	struct widget_instance *before);

/* ui_widget.c's widget types (its enum) */
#define ENGINE_WIDGET_SPINNER 2
#define ENGINE_WIDGET_COLUMN_LIST 3

/* the event manager's kinds of event (ui_widget.c's enum of them) */
#define ENGINE_EVENT_LEFT_STICK 1
#define ENGINE_EVENT_RIGHT_STICK 2
#define ENGINE_EVENT_BUTTON 3

/* the events a widget's handlers are for past the buttons: the sticks pushed
(ui_widget.c's _widget_event_left_stick_up, ...) */
#define ENGINE_HANDLER_LEFT_STICK_UP NUMBER_OF_GAMEPAD_BUTTONS
#define ENGINE_HANDLER_RIGHT_STICK_UP (NUMBER_OF_GAMEPAD_BUTTONS + 4)

/* a widget definition's flags' bits that its events are taken by
(ui_widget.c's _widget_..._bit) */
#define ENGINE_PASS_UNHANDLED_TO_FOCUSED_CHILD_BIT 0
#define ENGINE_UP_DOWN_TABS_CHILDREN_BIT 3
#define ENGINE_LEFT_RIGHT_TABS_CHILDREN_BIT 4
#define ENGINE_UP_DOWN_TABS_ITEMS_BIT 5
#define ENGINE_LEFT_RIGHT_TABS_ITEMS_BIT 6
#define ENGINE_NO_FOCUSED_CHILD_BIT 7
#define ENGINE_PASS_UNHANDLED_TO_ALL_CHILDREN_BIT 8
#define ENGINE_PASS_HANDLED_TO_ALL_CHILDREN_BIT 10
#define ENGINE_RETURN_TO_MAIN_MENU_BIT 11

/* the sounds the menus play (ui_widget.c's _ui_audio_feedback_...) */
#define ENGINE_SOUND_NONE 0
#define ENGINE_SOUND_CURSOR 1
#define ENGINE_SOUND_BACK 3

/* a held D-pad direction repeats no faster than this (ui_widget.c's
DPAD_EVENT_REPEAT_MILLISECONDS) */
#define ENGINE_DPAD_REPEAT_MILLISECONDS 250

/* the screens open (one for each player's stack, and room for the screen
being opened as the one it replaces closes) */
#define ENGINE_MAXIMUM_SCREENS 8

/* a settings list's rows scrolled (a geometry that scrolls: the tall
layout's), the scroll eased this long towards where it is going */
#define ENGINE_SCROLL_EASE_MILLISECONDS 90
/* the scroll's position track at the rows' right: its gap from them, its
width, and its colours (the menus' blue, dim and lit) */
#define ENGINE_SCROLL_TRACK_GAP 4
#define ENGINE_SCROLL_TRACK_WIDTH 2
#define ENGINE_SCROLL_TRACK_COLOR 0x402995FF
#define ENGINE_SCROLL_THUMB_COLOR 0xD02995FF

struct engine_screen
{
	struct widget_instance *widget;
	/* its model (NULL: a screen the model has nothing for, drawn and run as
	its widgets are) */
	struct halo_menu_model_screen const *model;
	/* its list's rows scrolled: how far they are scrolled up and to where,
	when it was eased last, whether it has been laid out once (the first
	time, where it is going at once), and the rows' room and how far they
	go (the list's units), for the track */
	real scroll, scroll_to;
	unsigned long scroll_time;
	boolean scroll_laid;
	short rows_top, rows_bottom, rows_end;
	/* its widgets a hand-written screen's (menu_screen.c), in place of its
	own */
	boolean hand_written;
};

/* Settings' search's rows found the most, and the most groups of them (the
model's geometry's search_results and search_crumbs no more than these) */
#define ENGINE_SEARCH_ROWS 24
#define ENGINE_SEARCH_GROUPS 12

static struct
{
	struct engine_screen screens[ENGINE_MAXIMUM_SCREENS];
	/* the screen the engine is drawing or processing an event of now (its
	stand-in's own drawing and events are ui_widget.c's) */
	struct widget_instance *drawing, *processing;
	struct halo_menu_model const *model;
	/* Settings' search (below): the screen whose list has the rows found,
	what was typed for them, the rows made (from their own pages' widgets),
	each its model's row and its group (its crumb's), in the order shown; the
	row being made (its spinners' functions ask for its model's row as it is
	made); how many rows match in all, and the groups' crumbs */
	struct
	{
		struct widget_instance *screen;
		char typed[64];
		unsigned long reads;
		struct widget_instance *rows[ENGINE_SEARCH_ROWS];
		struct halo_menu_model_row const *model_rows[ENGINE_SEARCH_ROWS];
		struct halo_menu_model_screen const *model_screens[ENGINE_SEARCH_ROWS];
		short groups[ENGINE_SEARCH_ROWS];
		long count, matched;
		struct halo_menu_model_row const *making;
		char const *crumbs[ENGINE_SEARCH_GROUPS];
		long group_count;
	} search;
} engine;

/* ---------- the model */

static boolean engine_on(void)
{
	return !strcmp(config_string("display.menu_engine"), "new");
}

/* engine_on, read again only when a setting changes: every widget's
definition asks it (menu_engine_definition) */
boolean menu_engine_looks(void)
{
	static unsigned long read_at;
	static boolean read, on;

	if (!read || read_at != config_changes())
	{
		read = TRUE;
		read_at = config_changes();
		on = engine_on();
	}
	return on;
}

void *menu_engine_definition(long tag_index)
{
	void *look = tag_index != NONE && menu_engine_looks() ? pc_menu_look_definition(tag_index) : NULL;

	return look ? look : tag_get(UI_WIDGET_DEFINITION_TAG, tag_index);
}

/* a widget's name as the menus' files have it: its tag's, without "pc\" and
with slashes */
static boolean engine_widget_name(long tag_index, char *name, size_t size)
{
	char const *tag = tag_index != NONE ? tag_get_name(tag_index) : NULL;
	size_t index;

	if (!tag || strncmp(tag, "pc\\", 3))
		return FALSE;
	for (index = 0; tag[index + 3] && index + 1 < size; index++)
		name[index] = tag[index + 3] == '\\' ? '/' : tag[index + 3];
	name[index] = 0;
	return TRUE;
}

static struct halo_menu_model_screen const *engine_model_screen(long tag_index)
{
	char name[256];
	long index;

	if (!engine.model)
		engine.model = halo_menus_model();
	if (!engine.model || !engine_widget_name(tag_index, name, sizeof(name)))
		return NULL;
	for (index = 0; index < engine.model->screen_count; index++)
	{
		if (!strcmp(engine.model->screens[index].name, name))
			return &engine.model->screens[index];
	}
	return NULL;
}

static struct engine_screen *engine_screen_of(struct widget_instance const *widget)
{
	short index;

	for (index = 0; widget && index < ENGINE_MAXIMUM_SCREENS; index++)
	{
		if (engine.screens[index].widget == widget)
			return &engine.screens[index];
	}
	return NULL;
}

/* a row of the screen's model by its key (NULL: none) */
static struct halo_menu_model_row const *engine_model_row(struct halo_menu_model_screen const *screen,
	char const *key)
{
	long index;

	for (index = 0; screen && key && index < screen->row_count; index++)
	{
		struct halo_menu_model_row const *row = &engine.model->rows[screen->first_row + index];

		if (!strcmp(row->key, key))
			return row;
	}
	return NULL;
}

/* ---------- laying out */

/* the child of the list named so (NULL: none, as when its platform has it
not) */
static struct widget_instance *engine_child(struct widget_instance *list, char const *prefix, char const *key)
{
	struct widget_instance *child;
	size_t length = strlen(prefix);

	for (child = list->child; child; child = child->next)
	{
		if (child->name && !strncmp(child->name, prefix, length) && !strcmp(child->name + length, key))
			return child;
	}
	return NULL;
}

/* the geometry a settings screen of the model is laid out in where the
engine draws it: the one it names (the tall layout's), else the first */
static struct halo_menu_model_geometry const *engine_geometry(struct halo_menu_model_screen const *screen)
{
	struct halo_menu_model const *model = engine.model ? engine.model : (engine.model = halo_menus_model());
	long index;

	for (index = 1; model && screen && screen->geometry && index < model->geometry_count; index++)
	{
		if (!strcmp(model->geometries[index].name, screen->geometry))
			return &model->geometries[index];
	}
	return model ? &model->geometry : NULL;
}

/* where a list's rows are scrolled to: the focused row (with its section's
title over it, the first row of one) in the rows' room, the room past the
last row none, eased there from where they are (at once the first time,
and while the pointer moved the focus last: a row the pointer is on is shown
already, and moving under it would move the focus again) */
static void engine_scroll(struct engine_screen *screen, short focus_top, short focus_bottom)
{
	real room = (real)(screen->rows_bottom - screen->rows_top);
	real most = MAX((real)(screen->rows_end - screen->rows_top) - room, 0.0f);
	unsigned long now = ui_widget_port_milliseconds();
	real to = screen->scroll_to;

	if (focus_bottom > focus_top && !ui_widget_port_pointer_focused())
	{
		if ((real)(focus_top - screen->rows_top) < to)
			to = (real)(focus_top - screen->rows_top);
		if ((real)(focus_bottom - screen->rows_top) > to + room)
			to = (real)(focus_bottom - screen->rows_top) - room;
	}
	screen->scroll_to = PIN(to, 0.0f, most);
	if (!screen->scroll_laid)
	{
		screen->scroll_laid = TRUE;
		screen->scroll = screen->scroll_to;
	}
	else
	{
		real part = MIN((real)(now - screen->scroll_time) / (real)ENGINE_SCROLL_EASE_MILLISECONDS, 1.0f);

		screen->scroll += (screen->scroll_to - screen->scroll) * part;
		if (screen->scroll - screen->scroll_to < 0.5f && screen->scroll_to - screen->scroll < 0.5f)
			screen->scroll = screen->scroll_to;
	}
	screen->scroll_time = now;
}

/* a settings tab's or page's list (its options_menu) laid out from the model:
its rows shown, from the top, a step apart, each section's title over them
(hidden with none shown, a gap above it after rows), as
tools/port_settings.py lays them out for every row, in its screen's geometry,
and scrolled where it scrolls (engine_scroll); after the list's own function
(settings_page_update) has chosen the rows shown */
static void engine_settings_layout(struct engine_screen *screen, struct widget_instance *list)
{
	struct halo_menu_model_geometry const *geometry = engine_geometry(screen->model);
	struct halo_menu_model_screen const *model = screen->model;
	short top = (short)(!strcmp(model->layout, "tab") ? geometry->tab_top : geometry->page_top);
	short y = top;
	struct widget_instance *section = NULL;
	struct widget_instance *laid[256];
	short laid_y[256];
	long laid_count = 0;
	boolean rows_above = FALSE;
	boolean section_shown = FALSE;
	short section_y = 0;
	short focus_top = 0, focus_bottom = 0;
	short scrolled;
	long index;

	for (index = 0; index < model->row_count; index++)
	{
		struct halo_menu_model_row const *row = &engine.model->rows[model->first_row + index];
		struct widget_instance *widget;

		if (!strcmp(row->kind, "button"))
			continue;
		if (!strcmp(row->kind, "section"))
		{
			if (section)
				section->visible = section_shown;
			section = engine_child(list, "", row->key);
			section_shown = FALSE;
			if (section)
			{
				/* (a title the list passes over) */
				section->disabled = TRUE;
				section->visible = FALSE;
				section_y = (short)(y + (rows_above ? geometry->section_gap : 0));
			}
			continue;
		}
		widget = engine_child(list, "op_", row->key);
		if (!widget || !widget->visible)
			continue;
		if (section && !section_shown)
		{
			/* (the section's title, over its first row shown) */
			section_shown = TRUE;
			if (laid_count < (long)NUMBEROF(laid))
			{
				laid[laid_count] = section;
				laid_y[laid_count++] = section_y;
			}
			y = (short)(section_y + geometry->section_step);
			/* (a section's first row with the focus: its title shown with it) */
			if (widget == list->focused_child)
				focus_top = section_y;
		}
		else if (widget == list->focused_child)
			focus_top = y;
		if (widget == list->focused_child)
			focus_bottom = (short)(y + geometry->row_height);
		if (laid_count < (long)NUMBEROF(laid))
		{
			laid[laid_count] = widget;
			laid_y[laid_count++] = y;
		}
		y = (short)(y + geometry->row_step);
		rows_above = TRUE;
	}
	if (section)
		section->visible = section_shown;
	/* (scrolled: the rows up by as much, those out of the room drawn cut,
	or not at all: engine_draw) */
	screen->rows_top = top;
	screen->rows_bottom = (short)menu_engine_geometry_bottom(geometry, geometry->rows_bottom);
	screen->rows_end = (short)(y - geometry->row_step + geometry->row_height);
	if (geometry->scrolls)
		engine_scroll(screen, focus_top, focus_bottom);
	else
		screen->scroll = screen->scroll_to = 0.0f;
	scrolled = (short)(screen->scroll + 0.5f);
	for (index = 0; index < laid_count; index++)
		laid[index]->vertical_offset = (short)(list->vertical_offset + laid_y[index] - scrolled);
}

/* whether a settings list's rows scroll (more of them than their room) */
static boolean engine_scrolls(struct engine_screen const *screen)
{
	return screen->rows_end > screen->rows_bottom;
}

/* whether a child of a settings list is one of its rows or sections (not
its tabs or buttons) */
static boolean engine_is_row(struct widget_instance const *child)
{
	return child->name && (!strncmp(child->name, "op_", 3) || !strncmp(child->name, "section_", 8));
}

/* whether the model lays out what is in the widget: a settings tab's or
page's list of rows */
static boolean engine_lays_out(struct engine_screen const *screen, struct widget_instance const *widget)
{
	return screen && screen->model && !strcmp(screen->model->kind, "settings") && screen->model->layout &&
		strcmp(screen->model->layout, "options") && strcmp(screen->model->layout, "search") && widget->name &&
		!strcmp(widget->name, "options_menu");
}

/* ---------- Settings' search (docs/menu_engine.md, "Deliberate
differences"; tools/port_settings.py's SETTINGS_SEARCH): the rows of every
tab and page that match what is typed (menu_search.c), made in the search
screen's list from their own pages' widgets, so that each is the real row
(its spinner, slider or link, which menu_functions.c's functions run as on
its page: menu_engine_model_row_of gives them its model's row), and laid out
from the top under their groups' crumbs (the way to them: "VIDEO › FRAME RATE
& SYNC"), as many as fit, the words under them saying how many more */

/* whether the screen is the search's (its model's layout "search") */
static boolean engine_search_screen(struct engine_screen const *screen)
{
	return screen && screen->model && screen->model->layout && !strcmp(screen->model->layout, "search");
}

/* the rows made let go of (their screen gone), or deleted from its list */
static void engine_search_clear(boolean delete_rows)
{
	long index;

	for (index = 0; delete_rows && index < engine.search.count; index++)
	{
		struct widget_instance *row = engine.search.rows[index];

		if (row && row->parent && row->parent->focused_child == row)
		{
			row->parent->focused_child = NULL;
			row->parent->parameters.list.selected_index = NONE;
		}
		if (row)
			ui_widget_delete(row);
	}
	engine.search.count = 0;
	engine.search.matched = 0;
	engine.search.group_count = 0;
	engine.search.typed[0] = 0;
	if (!delete_rows)
		engine.search.screen = NULL;
}

/* a row's widget's tag: its page's op_<key> ("pc\\" and the page's folder,
the model's screen's name without its own) */
static long engine_search_row_tag(struct halo_menu_model_screen const *screen, struct halo_menu_model_row const *row)
{
	char name[256];
	char const *last = strrchr(screen->name, '/');
	size_t index;

	if (!last || (size_t)(last - screen->name) + strlen(row->key) + 9 >= sizeof(name))
		return NONE;
	snprintf(name, sizeof(name), "pc\\%.*s\\op_%s", (int)(last - screen->name), screen->name, row->key);
	for (index = 0; name[index]; index++)
	{
		if (name[index] == '/')
			name[index] = '\\';
	}
	return tag_loaded(UI_WIDGET_DEFINITION_TAG, name);
}

/* the list's child of the role (its button bar's: the rows go before it) */
static struct widget_instance *engine_child_of_role(struct widget_instance *list, char const *role)
{
	struct widget_instance *child;

	for (child = list->child; child; child = child->next)
	{
		char const *found = menu_engine_look_role(child);

		if (found && !strcmp(found, role))
			return child;
	}
	return NULL;
}

long menu_engine_search_find(struct widget_instance *list, char const *typed, menu_engine_search_shown shown,
	void *context)
{
	struct widget_instance *screen = list;
	struct engine_screen *owned;
	struct halo_menu_model_geometry const *geometry;
	struct menu_search_found found[ENGINE_SEARCH_ROWS];
	struct menu_search_found ordered[ENGINE_SEARCH_ROWS];
	struct widget_instance *buttons;
	long count, index, kept = 0, maximum, y, groups = 0;
	boolean rows_above = FALSE;

	while (screen && screen->parent)
		screen = screen->parent;
	owned = engine_screen_of(screen);
	if (!engine_search_screen(owned) || !typed)
		return 0;
	if (engine.search.screen == screen && engine.search.reads == halo_menus_model_reads() &&
		!strcmp(engine.search.typed, typed))
	{
		return engine.search.count;
	}
	if (engine.search.screen != screen)
	{
		engine.search.count = 0;
		engine.search.screen = screen;
	}
	engine_search_clear(TRUE);
	engine.search.screen = screen;
	engine.search.reads = halo_menus_model_reads();
	snprintf(engine.search.typed, sizeof(engine.search.typed), "%s", typed);
	if (!typed[0])
		return 0;
	/* (in the geometry the search's screen is laid out in: the tall layout's
	in the new engine, as its rows found are) */
	geometry = engine_geometry(owned->model);
	maximum = geometry->search_results > 0 && geometry->search_results < ENGINE_SEARCH_ROWS ?
		geometry->search_results : ENGINE_SEARCH_ROWS;
	count = menu_search_rank(engine.model, typed, shown, context, found, maximum);
	engine.search.matched = count;
	if (count > maximum)
		count = maximum;
	/* (each group's rows together, the groups in the order of their best) */
	for (index = 0; index < count; index++)
	{
		char const *crumb = engine.model->rows[found[index].row].crumb;
		long other;

		if (found[index].score < 0)
			continue;
		for (other = index; other < count; other++)
		{
			char const *its = engine.model->rows[found[other].row].crumb;

			if (found[other].score >= 0 && (its == crumb || (its && crumb && !strcmp(its, crumb))))
			{
				ordered[kept++] = found[other];
				found[other].score = -1;
			}
		}
	}
	/* (as many as fit above the list's bottom, a row left under them for the
	words that say how many more) */
	buttons = engine_child_of_role(list, "buttons");
	y = geometry->tab_top;
	for (index = 0; index < kept; index++)
	{
		struct halo_menu_model_row const *row = &engine.model->rows[ordered[index].row];
		struct halo_menu_model_screen const *page = &engine.model->screens[ordered[index].screen];
		boolean group = !index || !row->crumb || !engine.model->rows[ordered[index - 1].row].crumb ||
			strcmp(row->crumb, engine.model->rows[ordered[index - 1].row].crumb);
		long top = y + (group ? (rows_above ? geometry->section_gap : 0) + geometry->section_step : 0);
		long room = menu_engine_geometry_bottom(geometry, geometry->rows_bottom) -
			(index + 1 < engine.search.matched ? geometry->row_step : 0);
		long tag;
		struct widget_instance *made;

		if (top + geometry->row_height > room || (group && groups >= ENGINE_SEARCH_GROUPS) ||
			(group && geometry->search_crumbs > 0 && groups >= geometry->search_crumbs))
		{
			break;
		}
		tag = engine_search_row_tag(page, row);
		if (tag == NONE)
			continue;
		engine.search.making = row;
		made = ui_widget_port_add_child(list, tag, buttons);
		engine.search.making = NULL;
		if (!made)
			break;
		if (group)
			engine.search.crumbs[groups++] = row->crumb;
		engine.search.rows[engine.search.count] = made;
		engine.search.model_rows[engine.search.count] = row;
		engine.search.model_screens[engine.search.count] = page;
		engine.search.groups[engine.search.count] = (short)(groups - 1);
		engine.search.count++;
		y = top + geometry->row_step;
		rows_above = TRUE;
	}
	engine.search.group_count = groups;
	return engine.search.count;
}

long menu_engine_search_matched(void)
{
	return engine.search.matched;
}

struct widget_instance *menu_engine_search_found(long index)
{
	return index >= 0 && index < engine.search.count ? engine.search.rows[index] : NULL;
}

struct halo_menu_model_screen const *menu_engine_search_found_screen(long index)
{
	return index >= 0 && index < engine.search.count ? engine.search.model_screens[index] : NULL;
}

char const *menu_engine_search_crumb(long group)
{
	return group >= 0 && group < engine.search.group_count ? engine.search.crumbs[group] : NULL;
}

boolean menu_engine_search_screen(struct widget_instance const *widget)
{
	while (widget && widget->parent)
		widget = widget->parent;
	return engine_search_screen(engine_screen_of(widget));
}

/* the search's list laid out: each group's crumb (crumb_<n>, its role
"search crumb"), its rows a step apart under it, the hidden ones taking no
room, and the words (its "search note") under the last, or at the top for
none */
static void engine_search_layout(struct widget_instance *list)
{
	struct halo_menu_model_geometry const *geometry = menu_engine_settings_geometry(list);
	struct widget_instance *crumbs[ENGINE_SEARCH_GROUPS];
	struct widget_instance *note = engine_child_of_role(list, "search note");
	struct widget_instance *child;
	long crumb_count = 0, index;
	short y = (short)geometry->tab_top;
	short group = NONE;
	boolean rows_above = FALSE;

	for (child = list->child; child && crumb_count < ENGINE_SEARCH_GROUPS; child = child->next)
	{
		char const *role = menu_engine_look_role(child);

		if (role && !strcmp(role, "search crumb"))
		{
			crumbs[crumb_count++] = child;
			child->disabled = TRUE;
			child->visible = FALSE;
		}
	}
	for (index = 0; engine.search.screen && index < engine.search.count; index++)
	{
		struct widget_instance *row = engine.search.rows[index];

		if (!row->visible)
			continue;
		if (engine.search.groups[index] != group)
		{
			group = engine.search.groups[index];
			if (group < crumb_count)
			{
				struct widget_instance *crumb = crumbs[group];

				y = (short)(y + (rows_above ? geometry->section_gap : 0));
				crumb->visible = TRUE;
				crumb->vertical_offset = (short)(list->vertical_offset + y);
				y = (short)(y + geometry->section_step);
			}
		}
		row->horizontal_offset = (short)(list->horizontal_offset + geometry->row_x);
		row->vertical_offset = (short)(list->vertical_offset + y);
		y = (short)(y + geometry->row_step);
		rows_above = TRUE;
	}
	if (note)
		note->vertical_offset = (short)(list->vertical_offset + y);
}

/* after a widget's own drawing (its game data inputs run): what the model
lays out in it */
static void engine_layout(struct engine_screen *screen, struct widget_instance *widget)
{
	if (engine_lays_out(screen, widget))
		engine_settings_layout(screen, widget);
	else if (engine_search_screen(screen) && widget->name && !strcmp(widget->name, "options_menu"))
		engine_search_layout(widget);
}

/* ---------- drawing */

/* a widget and what is in it, as ui_widget.c's render does: the widget by
the game's drawing, then each child from where its parent puts them, the
focused one drawn focused (and with the plasma, in a list) */
static void engine_draw(struct engine_screen *screen, struct widget_instance *widget, rectangle2d *clip,
	point2d offset, boolean focus, boolean nifty);

/* a scrolled settings list's rows, each in the rows' room (cut to it, its
tap targets too; one wholly out of it not drawn), and the scroll's track at
their right */
static void engine_draw_rows(struct engine_screen *screen, struct widget_instance *list, rectangle2d *clip,
	point2d children_offset)
{
	struct halo_menu_model_geometry const *geometry = engine_geometry(screen->model);
	short base = (short)(children_offset.y + list->vertical_offset);
	rectangle2d room;
	struct widget_instance *child, *right_row = NULL;
	short right = 0;

	if (clip)
		room = *clip;
	else
		set_rectangle2d(&room, SHORT_MIN, SHORT_MIN, SHORT_MAX, SHORT_MAX);

	room.y0 = (short)MAX(room.y0, base + screen->rows_top);
	room.y1 = (short)MIN(room.y1, base + screen->rows_bottom);
	for (child = list->child; child; child = child->next)
	{
		boolean child_focus = child == list->focused_child;
		short height = (short)(child->name && !strncmp(child->name, "section_", 8) ? geometry->section_height :
			geometry->row_height);
		short child_top = (short)(children_offset.y + child->vertical_offset);
		long first;

		if (!engine_is_row(child))
			continue;
		if (child->visible && !right_row && !strncmp(child->name, "op_", 3))
			right_row = child;
		if (child_top >= room.y1 || child_top + height <= room.y0)
			continue;
		/* (its words clipped to the room, its pictures cut to it) */
		first = ui_widget_port_targets_noted();
		ui_widget_port_cut_set(&room);
		engine_draw(screen, child, &room, children_offset, child_focus, child_focus);
		ui_widget_port_cut_set(NULL);
		ui_widget_port_targets_clip(first, &room);
	}
	/* (the track: the room's height, the thumb the rows shown's part of
	them, where they are among them) */
	if (right_row)
	{
		real length = (real)(screen->rows_end - screen->rows_top);
		real shown = (real)(screen->rows_bottom - screen->rows_top);
		real track_top = (real)room.y0, track_bottom = (real)room.y1;
		real thumb_top = track_top + (track_bottom - track_top) * screen->scroll / length;
		real thumb_bottom = thumb_top + (track_bottom - track_top) * MIN(shown / length, 1.0f);

		right = (short)(children_offset.x + right_row->horizontal_offset + geometry->row_width +
			ui_widget_port_wide_offset(right_row->definition_tag_index) +
			ui_widget_port_wide_stretch(right_row->definition_tag_index) + ENGINE_SCROLL_TRACK_GAP);
		menu_shape_caret((real)right, track_top, track_bottom, (real)ENGINE_SCROLL_TRACK_WIDTH,
			ENGINE_SCROLL_TRACK_COLOR);
		menu_shape_caret((real)right, thumb_top, thumb_bottom, (real)ENGINE_SCROLL_TRACK_WIDTH,
			ENGINE_SCROLL_THUMB_COLOR);
	}
}

static void engine_draw(struct engine_screen *screen, struct widget_instance *widget, rectangle2d *clip,
	point2d offset, boolean focus, boolean nifty)
{
	struct widget_instance *child;
	point2d children_offset;
	boolean children;
	boolean rows;

	/* (a hand-written screen's own draws in a widget's place: menu_screen.c) */
	if (screen->hand_written)
		menu_screen_widget_drawing(widget, offset);
	if (!ui_widget_port_render_node(widget, clip, offset, focus, nifty, &children_offset, &children))
		return;
	engine_layout(screen, widget);
	if (!children)
		return;
	/* (a settings list whose rows scroll: they in their room) */
	rows = engine_lays_out(screen, widget) && engine_scrolls(screen);
	if (rows)
		engine_draw_rows(screen, widget, clip, children_offset);
	for (child = widget->child; child; child = child->next)
	{
		boolean child_focus = child == widget->focused_child;

		if (rows && engine_is_row(child))
			continue;
		engine_draw(screen, child, clip, children_offset, child_focus, child_focus &&
			(widget->type == ENGINE_WIDGET_SPINNER || widget->type == ENGINE_WIDGET_COLUMN_LIST));
	}
}

/* ---------- input

An event for a screen the engine runs is taken here: the focus moved, a
held direction repeated, the widgets' actions run, the event passed down
the focus, as the game's menus take one (ui_widget.c's
widget_instance_process_one_event_recursive), its steps the same; what the
widgets do and how they are moved through (their definitions' flags and
event handlers) read from ui_widget.c (ui_widget_port_...). */

/* how a widget takes its events: its flags that are behaviour and its event
handlers, the model's (model/behaviour: a widget it has nothing for has
neither), else its definition's (a widget not of the generated menus) */
struct engine_behaviour
{
	long flags;
	void const *handlers;
	long handler_count;
	/* (the definition's handlers, not the model's) */
	boolean definition;
};

static void engine_behaviour_of(struct widget_instance const *widget, struct engine_behaviour *behaviour)
{
	struct halo_menu_model const *model = engine.model ? engine.model : (engine.model = halo_menus_model());
	char name[256];

	memset(behaviour, 0, sizeof(*behaviour));
	/* (a hand-written screen's building blocks take their events by their
	own definitions: menu_screen.c) */
	if (model && engine_widget_name(widget->definition_tag_index, name, sizeof(name)) && strncmp(name, "screens/", 8))
	{
		struct halo_menu_model_behaviour const *found = halo_menus_model_behaviour(model, name);

		if (found)
		{
			behaviour->flags = menu_tags_behaviour_flags(found);
			behaviour->handlers = menu_tags_behaviour_handlers(model, found, &behaviour->handler_count);
		}
		return;
	}
	behaviour->flags = ui_widget_port_definition_flags(widget);
	behaviour->handler_count = ui_widget_port_handler_count(widget);
	behaviour->definition = TRUE;
}

static short engine_handler_event(struct widget_instance const *widget, struct engine_behaviour const *behaviour,
	long index)
{
	return behaviour->definition ? ui_widget_port_handler_event_type(widget, index) :
		menu_tags_handler_event(behaviour->handlers, index);
}

static void engine_handler_run(struct widget_instance *widget, struct engine_behaviour const *behaviour,
	struct event_record *event, long index, boolean *deleted)
{
	if (behaviour->definition)
		ui_widget_port_handler_dispatch(widget, event, index, deleted);
	else
		ui_widget_port_handler_run(widget, event, menu_tags_handler(behaviour->handlers, index), deleted);
}

/* whether a widget takes events: one enabled with handlers of its own, or
a list (ui_widget.c's widget_instance_can_handle_events) */
static boolean engine_takes_events(struct widget_instance const *widget)
{
	struct engine_behaviour behaviour;

	if (widget->disabled)
		return FALSE;
	if (widget->type == ENGINE_WIDGET_SPINNER || widget->type == ENGINE_WIDGET_COLUMN_LIST)
		return TRUE;
	engine_behaviour_of(widget, &behaviour);
	return behaviour.handler_count > 0;
}

/* whether a list's item is one its focus passes over: a label (taking no
events, with nothing in it that does), or hidden or disabled
(ui_widget.c's widget_instance_port_is_label) */
static boolean engine_is_label(struct widget_instance const *widget)
{
	char const *role;

	/* (and Settings' search's field in the tabs' row, which Y, typing and the
	pointer reach, not the tabs' steps: the row steps as the classic
	engine's tabs do) */
	if ((role = menu_engine_look_role(widget)) != NULL && !strcmp(role, "search field") && widget->parent &&
		(role = menu_engine_look_role(widget->parent)) != NULL && !strcmp(role, "search tabs"))
	{
		return TRUE;
	}
	return (!engine_takes_events(widget) && !widget->child) || !widget->visible || widget->disabled;
}

/* a widget that only passes events on, given the focus, focused on its first
child that takes events again (ui_widget.c's
widget_instance_refocus_passthrough) */
static void engine_refocus_passthrough(struct widget_instance *widget)
{
	struct engine_behaviour behaviour;
	struct widget_instance *child;

	if (!widget || widget->focused_child || engine_takes_events(widget))
		return;
	engine_behaviour_of(widget, &behaviour);
	if (!TEST_FLAG(behaviour.flags, ENGINE_PASS_UNHANDLED_TO_FOCUSED_CHILD_BIT) ||
		TEST_FLAG(behaviour.flags, ENGINE_NO_FOCUSED_CHILD_BIT))
	{
		return;
	}
	for (child = widget->child; child; child = child->next)
	{
		if (engine_takes_events(child))
		{
			widget->focused_child = child;
			break;
		}
	}
}

/* whether a child of the widget may take its focus: one that takes events or
passes them on, or any of a list's but a label of the generated menus' lists
(ui_widget.c's widget_instance_tab_to_next_valid_widget's test) */
static boolean engine_focusable(struct widget_instance const *widget, struct widget_instance const *child)
{
	struct engine_behaviour behaviour;
	boolean list = widget->type == ENGINE_WIDGET_SPINNER || widget->type == ENGINE_WIDGET_COLUMN_LIST;

	engine_behaviour_of(child, &behaviour);
	return (behaviour.handler_count > 0 || TEST_FLAG(behaviour.flags, ENGINE_PASS_UNHANDLED_TO_FOCUSED_CHILD_BIT) ||
		list) && !(widget->type == ENGINE_WIDGET_COLUMN_LIST && pc_menu_tag(widget->definition_tag_index) &&
		engine_is_label(child));
}

/* the focus moved to the widget's next child that may take it (or its
previous), round from the last to the first, as ui_widget.c's
widget_instance_tab_to_next_valid_widget and _previous_ move it */
static void engine_tab(struct widget_instance *widget, boolean next)
{
	struct widget_instance *child, *tail;

	for (tail = widget->child; tail && tail->next; tail = tail->next)
		;
	if (next)
		child = widget->focused_child && widget->focused_child->next ? widget->focused_child->next : widget->child;
	else if (widget->focused_child)
		child = widget->focused_child->previous ? widget->focused_child->previous : tail;
	else
		child = widget->child && widget->child->previous ? widget->child->previous : widget->child;
	while (child && child != widget->focused_child)
	{
		if (engine_focusable(widget, child))
		{
			widget->focused_child = child;
			engine_refocus_passthrough(child);
			break;
		}
		if (next)
			child = child->next ? child->next : widget->child;
		else
			child = child->previous ? child->previous : tail;
	}
}

/* the focus moved to a list's next item (or its previous), round from the
last to the first, over its labels: for a list of the generated menus whose
items are its children (not a list made in code), as ui_widget.c's
widget_event_function_list_widget_goto_next_item and _previous_item move it
there; any other list is moved by them */
static void engine_list_step(struct widget_instance *widget, boolean next, struct event_record *event,
	boolean *deleted)
{
	struct widget_instance *child = NULL;
	long item_index = 0, tries = 0;

	if (widget->type != ENGINE_WIDGET_COLUMN_LIST || !pc_menu_tag(widget->definition_tag_index) ||
		(widget->parameters.list.list_items && widget->parameters.list.number_of_items > 0) || !widget->child)
	{
		if (next)
			widget_event_function_list_widget_goto_next_item(widget, event, deleted);
		else
			widget_event_function_list_widget_goto_previous_item(widget, event, deleted);
		return;
	}
	if (widget->focused_child)
	{
		item_index = widget->parameters.list.selected_index + (next ? 1 : -1);
		child = next ? widget->focused_child->next : widget->focused_child->previous;
		if (next && item_index == widget->parameters.list.number_of_items)
			child = NULL;
	}
	if (!child)
	{
		child = widget->child;
		item_index = 0;
		while (!next && child->next)
		{
			child = child->next;
			item_index++;
		}
	}
	while (engine_is_label(child) && tries++ < 256)
	{
		child = next ? child->next : child->previous;
		item_index += next ? 1 : -1;
		if (!child)
		{
			child = widget->child;
			item_index = 0;
			while (!next && child->next)
			{
				child = child->next;
				item_index++;
			}
		}
	}
	ui_widget_port_give_focus(widget, child);
	widget->parameters.list.selected_index = (short)item_index;
	widget->parameters.list.last_list_tab_direction = next ? 15 : -15;
}

/* the direction of a D-pad button (0 up, 1 down, 2 left, 3 right), or NONE */
static short engine_dpad_direction(struct event_record const *event)
{
	if (event->type != ENGINE_EVENT_BUTTON || event->data.button.index < _gamepad_binary_button_dpad_up ||
		event->data.button.index > _gamepad_binary_button_dpad_right)
	{
		return NONE;
	}
	return (short)(event->data.button.index - _gamepad_binary_button_dpad_up);
}

/* whether the event is a press (a button's first, or a held one's repeat) */
static boolean engine_pressed(struct event_record const *event)
{
	return event->type == ENGINE_EVENT_BUTTON && event->data.button.value == 1;
}

/* whether the widget has an event handler for the event type */
static boolean engine_has_handler(struct widget_instance const *widget, struct engine_behaviour const *behaviour,
	short event_type)
{
	long index;

	for (index = 0; index < behaviour->handler_count; index++)
	{
		if (engine_handler_event(widget, behaviour, index) == event_type)
			return TRUE;
	}
	return FALSE;
}

/* the focus moved through the widget's children or its list's items, by its
flags, for a direction pressed or a stick pushed: TRUE if it was */
static boolean engine_move(struct widget_instance *widget, long flags, struct event_record *event, boolean *deleted)
{
	boolean pressed = engine_pressed(event);
	boolean stick = event->type == ENGINE_EVENT_LEFT_STICK;
	/* (-1 back, 1 on, 0 none: up and down, left and right) */
	short vertical = 0, horizontal = 0;
	boolean list = widget->type == ENGINE_WIDGET_SPINNER || widget->type == ENGINE_WIDGET_COLUMN_LIST;

	if (pressed)
	{
		switch (engine_dpad_direction(event))
		{
		case 0: vertical = -1; break;
		case 1: vertical = 1; break;
		case 2: horizontal = -1; break;
		case 3: horizontal = 1; break;
		}
	}
	else if (stick)
	{
		vertical = event->data.stick.y == SHORT_MIN ? 1 : event->data.stick.y == SHORT_MAX ? -1 : 0;
		horizontal = event->data.stick.x == SHORT_MIN ? -1 : event->data.stick.x == SHORT_MAX ? 1 : 0;
	}
	if (!vertical && !horizontal)
		return FALSE;
	if (vertical && TEST_FLAG(flags, ENGINE_UP_DOWN_TABS_CHILDREN_BIT) && widget->focused_child)
	{
		engine_tab(widget, vertical > 0);
		return TRUE;
	}
	if (horizontal && TEST_FLAG(flags, ENGINE_LEFT_RIGHT_TABS_CHILDREN_BIT) && widget->focused_child)
	{
		engine_tab(widget, horizontal > 0);
		return TRUE;
	}
	if (vertical && TEST_FLAG(flags, ENGINE_UP_DOWN_TABS_ITEMS_BIT) && list)
	{
		engine_list_step(widget, vertical > 0, event, deleted);
		return TRUE;
	}
	if (horizontal && TEST_FLAG(flags, ENGINE_LEFT_RIGHT_TABS_ITEMS_BIT) && list)
	{
		engine_list_step(widget, horizontal > 0, event, deleted);
		return TRUE;
	}
	return FALSE;
}

/* whether a handler of the widget's is for the event */
static boolean engine_handler_matches(short handler, struct event_record const *event)
{
	short base;

	switch (event->type)
	{
	case ENGINE_EVENT_LEFT_STICK:
	case ENGINE_EVENT_RIGHT_STICK:
		base = event->type == ENGINE_EVENT_LEFT_STICK ? ENGINE_HANDLER_LEFT_STICK_UP : ENGINE_HANDLER_RIGHT_STICK_UP;
		return (handler == base && event->data.stick.y == SHORT_MAX) ||
			(handler == base + 1 && event->data.stick.y == SHORT_MIN) ||
			(handler == base + 2 && event->data.stick.x == SHORT_MIN) ||
			(handler == base + 3 && event->data.stick.x == SHORT_MAX);
	case ENGINE_EVENT_BUTTON:
		return handler == event->data.button.index && event->data.button.value == 1;
	}
	return FALSE;
}

/* an event for a widget of a screen the engine runs, and on down the focus */
static void engine_event(struct widget_instance *widget, struct event_record *event, boolean *return_deleted)
{
	struct engine_behaviour behaviour;
	long flags;
	boolean handled = FALSE;
	boolean deleted = FALSE;
	boolean for_this = ui_widget_port_takes_events(widget, event->controller_index);
	short sound = ENGINE_SOUND_NONE;
	short direction = engine_dpad_direction(event);
	boolean player = event->controller_index >= 0 && event->controller_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS;
	long index;

	engine_behaviour_of(widget, &behaviour);
	flags = behaviour.flags;
	/* (a direction pressed: the pointer is not what moved the focus last; a
	held one repeats, as a press, once it has waited long enough) */
	if (direction != NONE)
		ui_widget_port_pointer_not_last();
	if (direction != NONE && player && event->data.button.value > 1 &&
		ui_widget_port_milliseconds() - *ui_widget_port_dpad_time(event->controller_index, direction) >=
			ENGINE_DPAD_REPEAT_MILLISECONDS)
	{
		event->data.button.value = 1;
	}
	if (widget->close_if_local_player_controller_present)
	{
		short controller;

		for (controller = 0; controller < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS && !deleted; controller++)
		{
			if ((widget->local_player_index == NONE || widget->local_player_index == controller) &&
				input_has_gamepad(controller))
			{
				ui_widget_delete(widget_instance_get_topmost_parent(widget));
				deleted = TRUE;
			}
		}
	}
	/* (B or Back with no handler of its own: the screen before) */
	if (for_this && !deleted && engine_pressed(event) &&
		(event->data.button.index == _gamepad_analog_button_b ||
			event->data.button.index == _gamepad_binary_button_back) &&
		!engine_has_handler(widget, &behaviour, event->data.button.index))
	{
		ui_widget_port_go_back(widget);
		sound = ENGINE_SOUND_BACK;
		deleted = TRUE;
		handled = TRUE;
	}
	if (!deleted && widget->milliseconds_to_auto_close > 0)
	{
		unsigned long now = ui_widget_port_milliseconds();

		if (now - widget->creation_time >= widget->auto_close_fade_time + widget->milliseconds_to_auto_close)
		{
			ui_widget_delete(widget_instance_get_topmost_parent(widget));
			deleted = TRUE;
		}
		else if (widget->auto_close_fade_time > 0)
		{
			long faded = (long)(now - widget->creation_time - widget->milliseconds_to_auto_close);

			if (faded > 0)
				widget->alpha_modifier = 1.0f - (real)faded / (real)widget->auto_close_fade_time;
		}
	}
	if (!deleted)
	{
		ui_widget_port_lists_update(widget);
		if (for_this && !handled && engine_move(widget, flags, event, &deleted))
		{
			sound = ENGINE_SOUND_CURSOR;
			handled = TRUE;
		}
	}
	for (index = 0; for_this && !deleted && index < behaviour.handler_count; index++)
	{
		if (engine_handler_matches(engine_handler_event(widget, &behaviour, index), event))
		{
			handled = TRUE;
			engine_handler_run(widget, &behaviour, event, index, &deleted);
		}
	}
	if ((TEST_FLAG(flags, ENGINE_PASS_HANDLED_TO_ALL_CHILDREN_BIT) || !handled) && (TEST_FLAG(flags, ENGINE_PASS_UNHANDLED_TO_FOCUSED_CHILD_BIT) || TEST_FLAG(flags, ENGINE_PASS_UNHANDLED_TO_ALL_CHILDREN_BIT)) && !deleted)
	{
		if (TEST_FLAG(flags, ENGINE_PASS_UNHANDLED_TO_ALL_CHILDREN_BIT))
		{
			struct widget_instance *child;

			for (child = widget->child; child && !deleted; child = child->next)
			{
				if (ui_widget_port_takes_events(child, event->controller_index))
					engine_event(child, event, &deleted);
			}
		}
		else if (widget->focused_child && ui_widget_port_takes_events(widget->focused_child, event->controller_index))
			engine_event(widget->focused_child, event, &deleted);
	}
	if (deleted && TEST_FLAG(flags, ENGINE_RETURN_TO_MAIN_MENU_BIT) && ui_widget_port_no_screens())
		main_goto_main_menu();
	if (direction != NONE && player && event->data.button.value == 1)
		*ui_widget_port_dpad_time(event->controller_index, direction) = ui_widget_port_milliseconds();
	ui_play_audio_feedback_sound(sound);
	*return_deleted = deleted;
}

/* ---------- ui_widget.c's calls */

void menu_engine_screen_opened(struct widget_instance *screen)
{
	short index;

	if (!engine_on() || !pc_menu_tag(screen->definition_tag_index) || engine_screen_of(screen))
		return;
	for (index = 0; index < ENGINE_MAXIMUM_SCREENS; index++)
	{
		if (!engine.screens[index].widget)
		{
			engine.screens[index].widget = screen;
			engine.screens[index].model = engine_model_screen(screen->definition_tag_index);
			/* (or a hand-written screen's in its place: menu_screen.c) */
			engine.screens[index].hand_written = menu_screen_opened(screen);
			platform_log("menu engine: %s%s", screen->name ? screen->name : "?",
				engine.screens[index].hand_written ? " (hand-written)" :
				engine.screens[index].model ? " (its model's)" : "");
			return;
		}
	}
	platform_log("menu engine: more than %d screens open; %s is the game's", ENGINE_MAXIMUM_SCREENS,
		screen->name ? screen->name : "?");
}

void menu_engine_screen_closed(struct widget_instance *screen)
{
	struct engine_screen *owned = engine_screen_of(screen);

	if (owned && owned->hand_written)
		menu_screen_closed(screen);
	if (owned)
		memset(owned, 0, sizeof(*owned));
	/* (the search's rows found go with their screen) */
	if (screen && screen == engine.search.screen)
		engine_search_clear(FALSE);
}

boolean menu_engine_screen_draw(struct widget_instance *screen, rectangle2d *clip, point2d offset, boolean focus,
	boolean nifty)
{
	struct engine_screen *owned = engine_screen_of(screen);
	struct widget_instance *previous = engine.drawing;

	/* (the menus' Spartans asked for in the frame before made and placed,
	those no screen asked for deleted: menu_spartan.c) */
	menu_spartan_frame();
	if (!owned || engine.drawing == screen)
		return FALSE;
	engine.drawing = screen;
	/* (a hand-written screen: made again if its file changed, its hints
	shown for its focus) */
	if (owned->hand_written)
		menu_screen_frame(screen);
	engine_draw(owned, screen, clip, offset, focus, nifty);
	engine.drawing = previous;
	return TRUE;
}

boolean menu_engine_screen_owned(struct widget_instance const *screen)
{
	return screen && engine_screen_of(screen) != NULL;
}

boolean menu_engine_screen_event(struct widget_instance *screen, struct event_record *event, boolean *deleted)
{
	struct engine_screen *owned = engine_screen_of(screen);
	struct widget_instance *previous = engine.processing;

	if (!owned || engine.processing == screen)
		return FALSE;
	engine.processing = screen;
	*deleted = FALSE;
	/* (a hand-written screen's back: its own action) */
	if (!owned->hand_written || !menu_screen_back(screen, event, deleted))
		engine_event(screen, event, deleted);
	engine.processing = previous;
	return TRUE;
}

/* ---------- the menus' functions' (menu_functions.c) */

/* a settings row's help and its note after a "|" (as the menus' help
strings have them), from the model, in UTF-16 (*found: the engine runs the
row's screen and the model has the row; NULL if it has no help, or its
label is the game's to write, which has no help string either) */
wchar_t const *menu_engine_row_help(struct widget_instance *row, boolean *found)
{
	static wchar_t help[1024];
	struct widget_instance *screen = row;
	struct engine_screen *owned;
	struct halo_menu_model_row const *model_row;
	char text[2048];

	*found = FALSE;
	while (screen && screen->parent)
		screen = screen->parent;
	owned = engine_screen_of(screen);
	if (!owned || !owned->model || !row->name || strncmp(row->name, "op_", 3))
		return NULL;
	model_row = engine_search_screen(owned) ? menu_engine_model_row_of(row) : engine_model_row(owned->model,
		row->name + 3);
	if (!model_row)
		return NULL;
	*found = TRUE;
	if (model_row->named)
		return NULL;
	snprintf(text, sizeof(text), "%s%s%s", model_row->help ? model_row->help : "", model_row->note ? "|" : "",
		model_row->note ? model_row->note : "");
	halo_menus_utf16(text, (unsigned short *)help, NUMBEROF(help));
	return help;
}

struct halo_menu_model const *menu_engine_model(void)
{
	if (!engine.model)
		engine.model = halo_menus_model();
	return engine.model;
}

struct halo_menu_model_screen const *menu_engine_model_screen_of(struct widget_instance const *widget)
{
	while (widget && widget->parent)
		widget = widget->parent;
	return widget ? engine_model_screen(widget->definition_tag_index) : NULL;
}

char const *menu_engine_look_role(struct widget_instance const *widget)
{
	struct halo_menu_model_look const *look;
	char name[256];

	if (!engine.model)
		engine.model = halo_menus_model();
	if (!widget || !engine.model || !engine_widget_name(widget->definition_tag_index, name, sizeof(name)))
		return NULL;
	look = halo_menus_model_look(engine.model, name);
	return look ? look->role : NULL;
}

struct halo_menu_model_row const *menu_engine_model_row_of(struct widget_instance const *row)
{
	long index;

	if (!row || !row->name || strncmp(row->name, "op_", 3))
		return NULL;
	/* (a row the search found: its own page's, or the one being made) */
	for (index = 0; index < engine.search.count; index++)
	{
		if (engine.search.rows[index] == row)
			return engine.search.model_rows[index];
	}
	if (engine.search.making && !strcmp(row->name + 3, engine.search.making->key) &&
		menu_engine_search_screen(row))
	{
		return engine.search.making;
	}
	return engine_model_row(menu_engine_model_screen_of(row), row->name + 3);
}

boolean menu_engine_lays_out(struct widget_instance *widget)
{
	struct widget_instance *screen = widget;

	while (screen && screen->parent)
		screen = screen->parent;
	return engine_lays_out(engine_screen_of(screen), widget);
}

long menu_engine_geometry_bottom(struct halo_menu_model_geometry const *geometry, long bottom)
{
	return geometry->scrolls ? bottom - ui_widget_port_foot_lift() : bottom;
}

struct halo_menu_model_geometry const *menu_engine_settings_geometry(struct widget_instance *widget)
{
	struct widget_instance *screen = widget;
	struct engine_screen *owned;

	while (screen && screen->parent)
		screen = screen->parent;
	owned = engine_screen_of(screen);
	if (owned && owned->model)
		return engine_geometry(owned->model);
	if (!engine.model)
		engine.model = halo_menus_model();
	return engine.model ? &engine.model->geometry : NULL;
}
