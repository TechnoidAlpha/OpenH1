/*
MENU_SCREENS.C

The menus' hand-written screens (menu_screens.h, docs/menu_screens.md): the
files of port/assets/menus/screens (embedded, or a menus folder's of the same
name, as the menus' files are: menu_files.c) read with Expat into screens of
building blocks, for the menu engine (port/linux/game/menu_screen.c), which
makes them into widgets.

A file is checked whole as it is read: its elements and attributes, what an
item must have, the ids a list's focus names, and its actions and bindings
against the names the game gave (menu_screens_names). A file with a problem
is logged with its line, and its screen is not used: the one read before it,
if any, stays.

Hot reload: a folder (debug.menu_screens, else debug.menu_files' screens/)
is looked at twice a second (menu_screens_poll), and a file changed in it is
read again; the engine rebuilds a screen that is open when its reading
changes (menu_screens_generation).
*/

#include "halo_menus.h"
#include "menu_screens.h"
#include "platform.h"
#include "port_config.h"

#include "expat.h"

#ifndef HALO_ANDROID
#include <SDL3/SDL.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the folder in the menus' files that holds the screens, and its file of
the building blocks' widgets (read by menu_files.c, not here) */
#define SCREENS_FOLDER "screens/"
#define SCREENS_BLOCKS "screens/blocks.xml"
/* the watched folder looked at no more often than this */
#define SCREENS_POLL_MILLISECONDS 500
#define SCREENS_MAXIMUM_DEPTH 6

/* ---------- reading */

enum reader_element
{
	_element_screen,
	_element_title_art,
	_element_list,
	_element_item,
	_element_hint,
	_element_space,
	_element_buttons,
	_element_button,
	_element_cards,
	_element_card,
	_element_header,
	_element_colors,
	_element_spartan,
	_element_tabs,
	_element_tab,
	_element_table,
	_element_column,
	_element_search,
	_element_panel,
	_element_panel_heading,
	_element_panel_text,
	_element_pills,
	_element_pill,
	_element_pair,
	_element_lane,
	_element_choice,
	_element_chat,
	_element_box,
	_element_legend,
	_element_objectives,
	_element_picture,
	_element_dialog
};

struct reader
{
	struct menu_screen *screen;
	XML_Parser parser;
	int failed;
	char *problem;
	unsigned long problem_size;
	int depth;
	int elements[SCREENS_MAXIMUM_DEPTH];
	/* (the block each open element is: -1 for none) */
	int blocks[SCREENS_MAXIMUM_DEPTH];
	/* the open <hint>'s text */
	char text[MENU_SCREEN_MAXIMUM_TEXT];
	unsigned long text_length;
	int text_overflow;
};

static struct menu_screen_names const *screen_names;

void menu_screens_names(struct menu_screen_names const *names)
{
	screen_names = names;
}

static void reader_problem(struct reader *reader, char const *format, char const *detail)
{
	char message[256];

	if (reader->failed)
		return;
	reader->failed = 1;
	snprintf(message, sizeof(message), format, detail ? detail : "");
	snprintf(reader->problem, reader->problem_size, "%s:%lu: %s", reader->screen->file ? reader->screen->file : "?",
		reader->parser ? (unsigned long)XML_GetCurrentLineNumber(reader->parser) : 0UL, message);
	if (reader->parser)
		XML_StopParser(reader->parser, XML_FALSE);
}

static char const *reader_string(struct reader *reader, char const *text)
{
	struct menu_screen *screen = reader->screen;
	char *copy;

	if (screen->string_count >= MENU_SCREEN_MAXIMUM_STRINGS)
	{
		reader_problem(reader, "more than %s texts", "640");
		return NULL;
	}
	copy = strdup(text);
	if (!copy)
	{
		reader_problem(reader, "out of memory", NULL);
		return NULL;
	}
	screen->strings[screen->string_count++] = copy;
	return copy;
}

static int name_known(char const *const *names, long count, char const *name, unsigned long length)
{
	long index;

	if (!names)
		return 1;
	for (index = 0; index < count; index++)
	{
		if (strlen(names[index]) == length && !strncmp(names[index], name, length))
			return 1;
	}
	return 0;
}

/* a text's bindings, each a name the game knows */
static void text_check(struct reader *reader, char const *text)
{
	char const *at = text;

	while (!reader->failed && (at = strchr(at, '{')) != NULL)
	{
		char const *end = strchr(at, '}');
		char name[64];

		if (!end)
		{
			reader_problem(reader, "a { with no } in \"%s\"", text);
			return;
		}
		if (end == at + 1 || (unsigned long)(end - at - 1) >= sizeof(name))
		{
			reader_problem(reader, "a binding with no name, or too long, in \"%s\"", text);
			return;
		}
		snprintf(name, sizeof(name), "%.*s", (int)(end - at - 1), at + 1);
		if (screen_names && !name_known(screen_names->bindings, screen_names->binding_count, name, strlen(name)))
		{
			reader_problem(reader, "there is no binding {%s}", name);
			return;
		}
		at = end + 1;
	}
	if (!reader->failed && strchr(text, '}') && !strchr(text, '{'))
		reader_problem(reader, "a } with no { in \"%s\"", text);
}

static void action_check(struct reader *reader, char const *action)
{
	if (screen_names && !name_known(screen_names->actions, screen_names->action_count, action, strlen(action)))
		reader_problem(reader, "there is no action \"%s\"", action);
}

/* a binding named alone (an if, a tabs' current...), one the game knows */
static void binding_check(struct reader *reader, char const *name)
{
	if (!name[0])
		reader_problem(reader, "a binding's name is empty", NULL);
	else if (screen_names && !name_known(screen_names->bindings, screen_names->binding_count, name, strlen(name)))
		reader_problem(reader, "there is no binding {%s}", name);
}

static void source_check(struct reader *reader, char const *source)
{
	if (screen_names && !name_known(screen_names->sources, screen_names->source_count, source, strlen(source)))
		reader_problem(reader, "there is no row source \"%s\"", source);
}

/* a number of an attribute's, from least to most (0, a problem) */
static short number_read(struct reader *reader, char const *text, char const *what, long least, long most)
{
	char *end = NULL;
	long number = strtol(text, &end, 10);

	if (!end || end == text || *end || number < least || number > most)
	{
		char detail[128];

		snprintf(detail, sizeof(detail), "%s is a number, %ld to %ld: \"%.32s\"", what, least, most, text);
		reader_problem(reader, "%s", detail);
		return 0;
	}
	return (short)number;
}

/* whether a key is one a <button> may name */
static int key_known(char const *key)
{
	static char const *const keys[] = { "accept", "back", "x", "y", "lb", "rb", "lt", "rt", "r3", "view", "dpad" };
	unsigned index;

	for (index = 0; key && index < sizeof(keys) / sizeof(keys[0]); index++)
	{
		if (!strcmp(keys[index], key))
			return 1;
	}
	return 0;
}

/* a when: platforms the menus' files know (halo_menus_platform) */
static void when_check(struct reader *reader, char const *when)
{
	char const *name = when;

	while (*name && !reader->failed)
	{
		unsigned long length = (unsigned long)strcspn(name, " ");

		if (length && halo_menus_platform(name, length) < 0)
			reader_problem(reader, "when=\"%s\" names one that is not desktop, android, vr, hdr, touch, pointer or "
				"computer", when);
		name += length;
		while (*name == ' ')
			name++;
	}
}

/* an element's attributes: each one of the names (NULL-ended) it may have,
into the values (NULL where not written) */
static void attributes_read(struct reader *reader, char const *element, XML_Char const **attributes,
	char const *const *names, char const **values)
{
	int index, name;

	for (name = 0; names[name]; name++)
		values[name] = NULL;
	for (index = 0; attributes[index] && !reader->failed; index += 2)
	{
		for (name = 0; names[name] && strcmp(names[name], attributes[index]); name++)
			;
		if (!names[name])
		{
			char detail[96];

			snprintf(detail, sizeof(detail), "<%s> has no attribute %s", element, attributes[index]);
			reader_problem(reader, "%s", detail);
			return;
		}
		values[name] = reader_string(reader, attributes[index + 1]);
	}
}

static int block_add(struct reader *reader, int kind, int parent)
{
	struct menu_screen *screen = reader->screen;
	struct menu_screen_block *block;

	if (screen->block_count >= MENU_SCREEN_MAXIMUM_BLOCKS)
	{
		reader_problem(reader, "more than %s blocks", "96");
		return -1;
	}
	block = &screen->blocks[screen->block_count];
	memset(block, 0, sizeof(*block));
	block->frame = -1;
	block->kind = kind;
	block->parent = parent;
	block->line = (long)XML_GetCurrentLineNumber(reader->parser);
	return screen->block_count++;
}

static int kind_count(struct menu_screen const *screen, int kind)
{
	int index, count = 0;

	for (index = 0; index < screen->block_count; index++)
		count += screen->blocks[index].kind == kind;
	return count;
}

/* the screen's one block of a kind (a second refused) */
static void one_check(struct reader *reader, int kind, char const *element)
{
	if (!reader->failed && kind_count(reader->screen, kind))
		reader_problem(reader, "a screen has one <%s>", element);
}

/* a text attribute's bindings checked, if it has one */
static void text_attribute_check(struct reader *reader, char const *text)
{
	if (!reader->failed && text)
		text_check(reader, text);
}

/* a block's if (a binding) and when (platforms), if it has them */
static void shown_check(struct reader *reader, char const *condition, char const *when)
{
	if (!reader->failed && condition)
		binding_check(reader, condition);
	if (!reader->failed && when)
		when_check(reader, when);
}

/* a column's touch place, "x y width move grow" */
static void touch_read(struct reader *reader, struct menu_screen_block *column, char const *text)
{
	long numbers[5] = { 0, 0, 0, 0, 0 };
	int count = 0;
	char const *at = text;

	while (*at && count < 5)
	{
		char *end = NULL;

		numbers[count] = strtol(at, &end, 10);
		if (!end || end == at)
			break;
		count++;
		at = end;
		while (*at == ' ')
			at++;
	}
	if (count != 5 || *at || numbers[2] < 0 || numbers[2] > 640)
	{
		reader_problem(reader, "a <column>'s touch is \"x y width move grow\": \"%s\"", text);
		return;
	}
	column->touch_x = (short)numbers[0];
	column->touch_y = (short)numbers[1];
	column->touch_width = (short)numbers[2];
	column->touch_move = (short)numbers[3];
	column->touch_grow = (short)numbers[4];
}

/* a button (in <buttons>, a <header>, a <table> or a <panel>): its key, its
label, and what it does (do: an action; none, its key pressed) */
static int button_read(struct reader *reader, XML_Char const **attributes, int parent_block)
{
	static char const *const names[] = { "key", "label", "side", "do", "width", "if", "when", "touch-width", NULL };
	char const *values[9];
	struct menu_screen *screen = reader->screen;
	int block;

	attributes_read(reader, "button", attributes, names, values);
	block = block_add(reader, MENU_SCREEN_BUTTON, parent_block);
	if (block >= 0 && !reader->failed)
	{
		struct menu_screen_block *button = &screen->blocks[block];

		button->key = values[0];
		button->label = values[1];
		button->side = values[2];
		button->action = values[3];
		button->condition = values[5];
		button->when = values[6];
		if (!key_known(button->key))
			reader_problem(reader, "<button>'s key is accept, back, x, y, lb, rb, lt, rt, r3, view or dpad", NULL);
		else if (!button->label || !button->label[0])
			reader_problem(reader, "<button> has no label", NULL);
		else if (button->side && strcmp(button->side, "left") && strcmp(button->side, "right"))
			reader_problem(reader, "<button>'s side is left or right", NULL);
		text_attribute_check(reader, button->label);
		if (!reader->failed && button->action)
			action_check(reader, button->action);
		if (!reader->failed && values[4])
			button->width = number_read(reader, values[4], "a <button>'s width", 1, 640);
		if (!reader->failed && values[7])
			button->touch_width = number_read(reader, values[7], "a <button>'s touch-width", 1, 640);
		shown_check(reader, button->condition, button->when);
	}
	return block;
}

/* a <spartan>: on the screen, its box in the menus' 640 by 480 (x, y,
width, height); in a <panel>, a line of it (height, its width the panel's).
Its color the focused <colors> item's, the profile's, or a binding's (a
color's number, random, or none); a screen's own, its places' names (a
binding's, a line each: a lineup's, under its Spartans) */
static int spartan_read(struct reader *reader, XML_Char const **attributes, int parent_block)
{
	static char const *const names[] = { "x", "y", "width", "height", "color", "players", "touch-height", "when",
		"names", NULL };
	static char const *const panel_names[] = { "height", "color", "touch-height", "when", NULL };
	static long const defaults[] = { 440, 80, 160, 320 };
	char const *values[10];
	struct menu_screen *screen = reader->screen;
	int panel = parent_block >= 0;
	int block, index;

	if (panel)
	{
		char const *read[5];

		attributes_read(reader, "spartan", attributes, panel_names, read);
		values[0] = values[1] = values[2] = values[5] = values[8] = NULL;
		values[3] = read[0];
		values[4] = read[1];
		values[6] = read[2];
		values[7] = read[3];
	}
	else
		attributes_read(reader, "spartan", attributes, names, values);
	/* (one of the screen's own, one in each panel) */
	for (index = 0; index < screen->block_count && !reader->failed; index++)
	{
		if (screen->blocks[index].kind == MENU_SCREEN_SPARTAN && screen->blocks[index].parent == parent_block)
			reader_problem(reader, panel ? "a panel has one <spartan>" : "a screen has one <spartan> of its own", NULL);
	}
	block = block_add(reader, MENU_SCREEN_SPARTAN, parent_block);
	if (block >= 0 && !reader->failed)
	{
		struct menu_screen_block *spartan = &screen->blocks[block];
		long *box[4];
		int index;

		box[0] = &spartan->box_x;
		box[1] = &spartan->box_y;
		box[2] = &spartan->box_width;
		box[3] = &spartan->box_height;
		for (index = 0; index < 4 && !reader->failed; index++)
		{
			char *end = NULL;

			*box[index] = panel ? 0 : defaults[index];
			if (index == 3 && panel)
				*box[index] = 200;
			if (!values[index])
				continue;
			*box[index] = strtol(values[index], &end, 10);
			if (!end || *end || *box[index] < (index < 2 ? -480 : 1) || *box[index] > (panel ? 360 : 1280))
				reader_problem(reader, panel ? "a panel's <spartan>'s height is a number, 1 to 360: \"%s\"" :
					"a <spartan>'s x, y, width and height are numbers of the menus' 640 by 480: \"%s\"",
					values[index]);
		}
		spartan->color = MENU_SCREEN_SPARTAN_PROFILE;
		if (!reader->failed && values[4])
		{
			if (!strcmp(values[4], "focus"))
				spartan->color = MENU_SCREEN_SPARTAN_FOCUS;
			else if (strcmp(values[4], "profile"))
			{
				/* (a binding giving the color) */
				spartan->color = MENU_SCREEN_SPARTAN_BINDING;
				spartan->value = values[4];
				binding_check(reader, values[4]);
			}
		}
		spartan->players = 1;
		if (!reader->failed && values[5])
		{
			char *end = NULL;

			spartan->players = (int)strtol(values[5], &end, 10);
			if (!end || *end || spartan->players < 1 || spartan->players > 4)
				reader_problem(reader, "a <spartan>'s players are 1 to 4: \"%s\"", values[5]);
		}
		if (!reader->failed && spartan->color != MENU_SCREEN_SPARTAN_PROFILE && spartan->players != 1)
			reader_problem(reader, "a <spartan> of the focused color, or a binding's, is one player's", NULL);
		spartan->touch_box_height = spartan->box_height;
		if (!reader->failed && values[6])
			spartan->touch_box_height = number_read(reader, values[6], "a <spartan>'s touch-height", 1, 360);
		spartan->when = values[7];
		shown_check(reader, NULL, spartan->when);
		spartan->names = values[8];
		if (!reader->failed && spartan->names)
		{
			binding_check(reader, spartan->names);
			if (!reader->failed && spartan->color != MENU_SCREEN_SPARTAN_BINDING)
				reader_problem(reader, "a <spartan>'s names are a lineup's: its color a binding's", NULL);
		}
	}
	return block;
}

/* the new blocks' elements (<tabs>, <table>, <panel>, <search> and what is
in them): the element read into its block, or -2 if it is none of them */
static int element_more(struct reader *reader, char const *name, XML_Char const **attributes, int parent_element,
	int parent_block, int *element)
{
	struct menu_screen *screen = reader->screen;
	char const *values[16];
	int block = -1;

	if (parent_element == _element_screen && !strcmp(name, "tabs"))
	{
		static char const *const names[] = { "id", "current", "if", "when", NULL };

		*element = _element_tabs;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_TABS, "tabs");
		block = block_add(reader, MENU_SCREEN_TABS, -1);
		if (block >= 0 && !reader->failed)
		{
			screen->blocks[block].id = values[0];
			screen->blocks[block].value = values[1];
			screen->blocks[block].condition = values[2];
			screen->blocks[block].when = values[3];
			if (values[1])
				binding_check(reader, values[1]);
			shown_check(reader, values[2], values[3]);
		}
	}
	else if (parent_element == _element_tabs && !strcmp(name, "tab"))
	{
		static char const *const names[] = { "id", "label", "do", "when", NULL };

		*element = _element_tab;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_TAB, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *tab = &screen->blocks[block];

			tab->id = values[0];
			tab->label = values[1];
			tab->action = values[2];
			tab->when = values[3];
			if (!tab->id || !tab->id[0])
				reader_problem(reader, "<tab> has no id", NULL);
			else if (!tab->label || !tab->label[0])
				reader_problem(reader, "<tab> has no label", NULL);
			else if (!tab->action)
				reader_problem(reader, "<tab> has no do", NULL);
			text_attribute_check(reader, tab->label);
			if (!reader->failed)
				action_check(reader, tab->action);
			shown_check(reader, NULL, tab->when);
		}
	}
	else if (parent_element == _element_screen && !strcmp(name, "table"))
	{
		static char const *const names[] = { "id", "source", "sorted", "do", "empty", "lanes", "left", "right", NULL };

		*element = _element_table;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_TABLE, "table");
		block = block_add(reader, MENU_SCREEN_TABLE, -1);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *table = &screen->blocks[block];

			table->id = values[0];
			table->source = values[1];
			table->value = values[2];
			table->action = values[3];
			table->empty = values[4];
			table->lanes = 1;
			table->left_action = values[6];
			table->right_action = values[7];
			if (!table->source || !table->source[0])
				reader_problem(reader, "<table> has no source", NULL);
			else
				source_check(reader, table->source);
			if (!reader->failed && table->value)
				binding_check(reader, table->value);
			if (!reader->failed && table->action)
				action_check(reader, table->action);
			text_attribute_check(reader, table->empty);
			if (!reader->failed && values[5])
				table->lanes = number_read(reader, values[5], "a <table>'s lanes", 1, 4);
			if (!reader->failed && table->left_action)
				action_check(reader, table->left_action);
			if (!reader->failed && table->right_action)
				action_check(reader, table->right_action);
		}
	}
	else if (parent_element == _element_table && !strcmp(name, "lane"))
	{
		static char const *const names[] = { "label", "detail", "tone", NULL };
		struct menu_screen_block const *table = &screen->blocks[parent_block];

		*element = _element_lane;
		attributes_read(reader, name, attributes, names, values);
		if (!reader->failed && table->lanes < 2)
			reader_problem(reader, "a <lane> is in a <table> of lanes=\"2\" or more", NULL);
		block = block_add(reader, MENU_SCREEN_LANE, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *lane = &screen->blocks[block];

			lane->label = values[0];
			lane->empty = values[1];
			lane->tone = values[2];
			if (!lane->label)
				reader_problem(reader, "<lane> has no label", NULL);
			text_attribute_check(reader, lane->label);
			text_attribute_check(reader, lane->empty);
			if (!reader->failed && lane->tone && (lane->tone[0] < '0' || lane->tone[0] > '9'))
				binding_check(reader, lane->tone);
			else if (!reader->failed && lane->tone)
				number_read(reader, lane->tone, "a <lane>'s tone", 0, 2);
		}
	}
	else if (parent_element == _element_table && !strcmp(name, "column"))
	{
		static char const *const names[] = { "id", "label", "kind", "width", "grow", "align", "sort", "do", "touch",
			"when", "gap", "title-width", NULL };
		static char const *const kinds[] = { "text", "strong", "ping", "star", "dot", "swatch" };

		*element = _element_column;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_COLUMN, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *column = &screen->blocks[block];
			int kind;

			column->id = values[0];
			column->label = values[1];
			column->sort = values[6];
			column->action = values[7];
			column->when = values[9];
			column->cell = -1;
			for (kind = 0; kind < 6 && values[2]; kind++)
			{
				if (!strcmp(values[2], kinds[kind]))
					column->cell = kind;
			}
			if (!values[2])
				column->cell = MENU_SCREEN_CELL_TEXT;
			if (!column->id || !column->id[0])
				reader_problem(reader, "<column> has no id", NULL);
			else if (column->cell < 0)
				reader_problem(reader, "a <column>'s kind is text, strong, ping, star, dot or swatch: \"%s\"", values[2]);
			else if (!values[3])
				reader_problem(reader, "<column> has no width", NULL);
			else
				column->width = number_read(reader, values[3], "a <column>'s width", 1, 640);
			if (!reader->failed && values[4])
				column->grow = number_read(reader, values[4], "a <column>'s grow", 0, 640);
			column->gap = 4;
			if (!reader->failed && values[10])
				column->gap = number_read(reader, values[10], "a <column>'s gap", 0, 640);
			if (!reader->failed && values[11])
				column->title_width = number_read(reader, values[11], "a <column>'s title-width", 1, 640);
			if (!reader->failed && values[5] && strcmp(values[5], "left") && strcmp(values[5], "right"))
				reader_problem(reader, "a <column>'s align is left or right", NULL);
			column->right = values[5] && !strcmp(values[5], "right");
			text_attribute_check(reader, column->label);
			if (!reader->failed && column->sort)
				action_check(reader, column->sort);
			if (!reader->failed && column->action)
				action_check(reader, column->action);
			if (!reader->failed && values[8])
				touch_read(reader, column, values[8]);
			shown_check(reader, NULL, column->when);
		}
	}
	else if ((parent_element == _element_column || parent_element == _element_screen) && !strcmp(name, "search"))
	{
		static char const *const names[] = { "value", "label", "when", "if", NULL };

		*element = _element_search;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_SEARCH, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *search = &screen->blocks[block];

			search->value = values[0];
			search->label = values[1];
			search->when = values[2];
			search->condition = values[3];
			if (!search->value)
				reader_problem(reader, "<search> has no value", NULL);
			else
				binding_check(reader, search->value);
			text_attribute_check(reader, search->label);
			shown_check(reader, search->condition, search->when);
		}
	}
	else if (parent_element == _element_screen && !strcmp(name, "panel"))
	{
		static char const *const names[] = { "id", "if", NULL };

		*element = _element_panel;
		attributes_read(reader, name, attributes, names, values);
		if (!reader->failed && kind_count(screen, MENU_SCREEN_PANEL) >= MENU_SCREEN_MAXIMUM_PANELS)
			reader_problem(reader, "a screen has at most %s <panel>s", "4");
		block = block_add(reader, MENU_SCREEN_PANEL, -1);
		if (block >= 0 && !reader->failed)
		{
			screen->blocks[block].id = values[0];
			screen->blocks[block].condition = values[1];
			shown_check(reader, values[1], NULL);
		}
	}
	else if (parent_element == _element_panel && (!strcmp(name, "heading") || !strcmp(name, "text")))
	{
		static char const *const names[] = { "text", "lines", "touch-lines", "when", "if", NULL };
		int heading = !strcmp(name, "heading");

		*element = heading ? _element_panel_heading : _element_panel_text;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, heading ? MENU_SCREEN_PANEL_HEADING : MENU_SCREEN_PANEL_TEXT, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *text = &screen->blocks[block];

			text->label = values[0];
			text->when = values[3];
			text->condition = values[4];
			text->lines = 1;
			if (!text->label)
				reader_problem(reader, "<%s> has no text", name);
			text_attribute_check(reader, text->label);
			if (!reader->failed && values[1])
				text->lines = number_read(reader, values[1], "a text's lines", 1, 8);
			text->touch_lines = text->lines;
			if (!reader->failed && values[2])
				text->touch_lines = number_read(reader, values[2], "a text's touch-lines", 1, 8);
			shown_check(reader, text->condition, text->when);
		}
	}
	else if (parent_element == _element_panel && !strcmp(name, "pills"))
	{
		static char const *const names[] = { NULL };

		*element = _element_pills;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_PILLS, parent_block);
	}
	else if (parent_element == _element_pills && !strcmp(name, "pill"))
	{
		static char const *const names[] = { "text", "tone", "width", NULL };

		*element = _element_pill;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_PILL, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *pill = &screen->blocks[block];

			pill->label = values[0];
			pill->tone = values[1];
			if (!pill->label)
				reader_problem(reader, "<pill> has no text", NULL);
			else if (!values[2])
				reader_problem(reader, "<pill> has no width", NULL);
			else
				pill->width = number_read(reader, values[2], "a <pill>'s width", 1, 640);
			text_attribute_check(reader, pill->label);
			/* (a tone: a frame's number, or a binding that gives one) */
			if (!reader->failed && pill->tone && (pill->tone[0] < '0' || pill->tone[0] > '9'))
				binding_check(reader, pill->tone);
			else if (!reader->failed && pill->tone)
				number_read(reader, pill->tone, "a <pill>'s tone", 0, 15);
		}
	}
	else if (parent_element == _element_panel && !strcmp(name, "pair"))
	{
		static char const *const names[] = { "label", "value", "when", NULL };

		*element = _element_pair;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_PAIR, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *pair = &screen->blocks[block];

			pair->label = values[0];
			pair->hints[0] = values[1];
			pair->hint_count = values[1] != NULL;
			pair->when = values[2];
			if (!pair->label || !pair->label[0])
				reader_problem(reader, "<pair> has no label", NULL);
			else if (!values[1])
				reader_problem(reader, "<pair> has no value", NULL);
			text_attribute_check(reader, pair->label);
			text_attribute_check(reader, values[1]);
			shown_check(reader, NULL, pair->when);
		}
	}
	else if (parent_element == _element_screen && !strcmp(name, "chat"))
	{
		static char const *const names[] = { "source", "lines", "do", "if", "when", NULL };

		*element = _element_chat;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_CHAT, "chat");
		block = block_add(reader, MENU_SCREEN_CHAT, -1);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *chat = &screen->blocks[block];

			chat->source = values[0];
			chat->action = values[2];
			chat->condition = values[3];
			chat->when = values[4];
			chat->lines = 5;
			if (!chat->source || !chat->source[0])
				reader_problem(reader, "<chat> has no source", NULL);
			else if (screen_names && !name_known(screen_names->chats, screen_names->chat_count, chat->source,
				strlen(chat->source)))
				reader_problem(reader, "there is no chat \"%s\"", chat->source);
			if (!reader->failed && values[1])
				chat->lines = number_read(reader, values[1], "a <chat>'s lines", 1, 12);
			if (!reader->failed && chat->action)
				action_check(reader, chat->action);
			shown_check(reader, chat->condition, chat->when);
		}
	}
	else if (parent_element == _element_panel && !strcmp(name, "choice"))
	{
		static char const *const names[] = { "label", "value", "left", "right", "if", "dim", "when", NULL };

		*element = _element_choice;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_CHOICE, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *choice = &screen->blocks[block];

			choice->label = values[0];
			choice->hints[0] = values[1];
			choice->hint_count = values[1] != NULL;
			choice->left_action = values[2];
			choice->right_action = values[3];
			choice->condition = values[4];
			choice->dim = values[5];
			choice->when = values[6];
			if (!choice->label || !choice->label[0])
				reader_problem(reader, "<choice> has no label", NULL);
			else if (!values[1])
				reader_problem(reader, "<choice> has no value", NULL);
			else if (!choice->left_action || !choice->right_action)
				reader_problem(reader, "<choice> has no left or no right", NULL);
			text_attribute_check(reader, choice->label);
			text_attribute_check(reader, values[1]);
			if (!reader->failed && choice->left_action)
				action_check(reader, choice->left_action);
			if (!reader->failed && choice->right_action)
				action_check(reader, choice->right_action);
			if (!reader->failed && choice->dim)
				binding_check(reader, choice->dim);
			shown_check(reader, choice->condition, choice->when);
		}
	}
	else if (parent_element == _element_panel && !strcmp(name, "picture"))
	{
		static char const *const names[] = { "set", "frame", "height", "touch-height", "if", "when", NULL };

		*element = _element_picture;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_PICTURE, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *picture = &screen->blocks[block];

			picture->picture = values[0];
			picture->value = values[1];
			picture->condition = values[4];
			picture->when = values[5];
			picture->box_height = 120;
			if (!picture->picture || (strcmp(picture->picture, "maps") && strcmp(picture->picture, "missions")))
				reader_problem(reader, "a <picture>'s set is maps or missions: \"%s\"", picture->picture ?
					picture->picture : "");
			else if (!picture->value)
				reader_problem(reader, "<picture> has no frame", NULL);
			else
				binding_check(reader, picture->value);
			if (!reader->failed && values[2])
				picture->box_height = number_read(reader, values[2], "a <picture>'s height", 24, 240);
			picture->touch_box_height = picture->box_height;
			if (!reader->failed && values[3])
				picture->touch_box_height = number_read(reader, values[3], "a <picture>'s touch-height", 24, 240);
			shown_check(reader, picture->condition, picture->when);
		}
	}
	else if (parent_element == _element_panel && !strcmp(name, "spartan"))
	{
		*element = _element_spartan;
		block = spartan_read(reader, attributes, parent_block);
	}
	else if ((parent_element == _element_header || parent_element == _element_table ||
		parent_element == _element_panel) && !strcmp(name, "button"))
	{
		*element = _element_button;
		block = button_read(reader, attributes, parent_block);
	}
	else
		return -2;
	return block;
}

static void XMLCALL element_start(void *data, XML_Char const *name, XML_Char const **attributes)
{
	struct reader *reader = data;
	struct menu_screen *screen = reader->screen;
	int parent_element = reader->depth ? reader->elements[reader->depth - 1] : -1;
	int parent_block = reader->depth ? reader->blocks[reader->depth - 1] : -1;
	int element = -1;
	int block = -1;
	char const *values[16];

	if (reader->failed)
		return;
	if (reader->depth >= SCREENS_MAXIMUM_DEPTH)
	{
		reader_problem(reader, "<%s> is too deep", name);
		return;
	}
	if (!strcmp(name, "screen") && parent_element == -1)
	{
		static char const *const names[] = { "name", "back", "start", "background", NULL };

		element = _element_screen;
		attributes_read(reader, name, attributes, names, values);
		screen->name = values[0];
		screen->back = values[1];
		screen->start = values[2];
		if (!reader->failed && (!screen->name || !screen->name[0]))
			reader_problem(reader, "<screen> has no name", NULL);
		if (!reader->failed && screen->back)
			action_check(reader, screen->back);
		if (!reader->failed && screen->start)
			action_check(reader, screen->start);
		if (!reader->failed && values[3] && strcmp(values[3], "own") && strcmp(values[3], "tall"))
			reader_problem(reader, "a <screen>'s background is own or tall: \"%s\"", values[3]);
		screen->tall_background = values[3] && !strcmp(values[3], "tall");
	}
	else if (parent_element == _element_screen && (!strcmp(name, "title-art") || !strcmp(name, "space")))
	{
		static char const *const names[] = { NULL };

		element = !strcmp(name, "space") ? _element_space : _element_title_art;
		attributes_read(reader, name, attributes, names, values);
		if (element == _element_title_art && kind_count(screen, MENU_SCREEN_TITLE_ART))
			reader_problem(reader, "a screen has one <title-art>", NULL);
		block = block_add(reader, element == _element_space ? MENU_SCREEN_SPACE : MENU_SCREEN_TITLE_ART, -1);
	}
	else if ((parent_element == _element_screen || parent_element == _element_box ||
		parent_element == _element_dialog) && !strcmp(name, "list"))
	{
		static char const *const names[] = { "id", "focus", NULL };

		element = _element_list;
		attributes_read(reader, name, attributes, names, values);
		if (kind_count(screen, MENU_SCREEN_LIST) || kind_count(screen, MENU_SCREEN_CARDS) || kind_count(screen, MENU_SCREEN_COLORS))
			reader_problem(reader, "a screen has one <list>, <cards> or <colors>", NULL);
		block = block_add(reader, MENU_SCREEN_LIST, parent_element == _element_screen ? -1 : parent_block);
		if (block >= 0)
		{
			screen->blocks[block].id = values[0];
			screen->blocks[block].focus = values[1];
		}
	}
	else if (parent_element == _element_list && !strcmp(name, "item"))
	{
		static char const *const names[] = { "id", "label", "hint", "do", "when", "picture", "if", NULL };

		element = _element_item;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_ITEM, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *item = &screen->blocks[block];

			item->id = values[0];
			item->label = values[1];
			item->action = values[3];
			item->when = values[4];
			item->picture = values[5];
			item->condition = values[6];
			if (values[2])
				item->hints[item->hint_count++] = values[2];
			if (!item->label || !item->label[0])
				reader_problem(reader, "<item> has no label", NULL);
			else if (!item->action)
				reader_problem(reader, "<item> has no do", NULL);
			if (!reader->failed)
				text_check(reader, item->label);
			if (!reader->failed && values[2])
				text_check(reader, values[2]);
			if (!reader->failed)
				action_check(reader, item->action);
			if (!reader->failed && item->when)
				when_check(reader, item->when);
			if (!reader->failed && item->condition)
				binding_check(reader, item->condition);
		}
	}
	else if (parent_element == _element_item && !strcmp(name, "hint"))
	{
		static char const *const names[] = { NULL };

		element = _element_hint;
		attributes_read(reader, name, attributes, names, values);
		reader->text_length = 0;
		reader->text_overflow = 0;
		reader->text[0] = 0;
	}
	else if (parent_element == _element_screen && !strcmp(name, "header"))
	{
		static char const *const names[] = { "title", "picture", "detail", "fallback", NULL };

		element = _element_header;
		attributes_read(reader, name, attributes, names, values);
		if (!reader->failed && kind_count(screen, MENU_SCREEN_HEADER))
			reader_problem(reader, "a screen has one <header>", NULL);
		if (!reader->failed && (!values[0] || !values[0][0]))
			reader_problem(reader, "<header> has no title", NULL);
		text_attribute_check(reader, values[0]);
		text_attribute_check(reader, values[2]);
		text_attribute_check(reader, values[3]);
		if (!reader->failed && values[3] && !values[2])
			reader_problem(reader, "<header> has a fallback and no detail", NULL);
		block = block_add(reader, MENU_SCREEN_HEADER, -1);
		if (block >= 0)
		{
			screen->blocks[block].label = values[0];
			screen->blocks[block].picture = values[1];
			screen->blocks[block].empty = values[2];
			screen->blocks[block].source = values[3];
		}
	}
	else if (parent_element == _element_screen && !strcmp(name, "cards"))
	{
		static char const *const names[] = { "id", "focus", NULL };

		element = _element_cards;
		attributes_read(reader, name, attributes, names, values);
		if (kind_count(screen, MENU_SCREEN_CARDS) || kind_count(screen, MENU_SCREEN_LIST) || kind_count(screen, MENU_SCREEN_COLORS))
			reader_problem(reader, "a screen has one <list>, <cards> or <colors>", NULL);
		block = block_add(reader, MENU_SCREEN_CARDS, -1);
		if (block >= 0)
		{
			screen->blocks[block].id = values[0];
			screen->blocks[block].focus = values[1];
		}
	}
	else if (parent_element == _element_cards && !strcmp(name, "card"))
	{
		static char const *const names[] = { "id", "title", "subtitle", "picture", "frame", "do", "when", NULL };

		element = _element_card;
		attributes_read(reader, name, attributes, names, values);
		block = block_add(reader, MENU_SCREEN_CARD, parent_block);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *card = &screen->blocks[block];
			char *end = NULL;

			card->id = values[0];
			card->label = values[1];
			if (values[2])
				card->hints[card->hint_count++] = values[2];
			card->picture = values[3];
			card->action = values[5];
			card->when = values[6];
			if (values[4])
			{
				card->frame = strtol(values[4], &end, 10);
				if (!end || *end || card->frame < 0 || card->frame > 255)
					reader_problem(reader, "a <card>'s frame is a number, 0 to 255: \"%s\"", values[4]);
			}
			if (!reader->failed && (!card->label || !card->label[0]))
				reader_problem(reader, "<card> has no title", NULL);
			else if (!reader->failed && !card->action)
				reader_problem(reader, "<card> has no do", NULL);
			if (!reader->failed)
				text_check(reader, card->label);
			if (!reader->failed && values[2])
				text_check(reader, values[2]);
			if (!reader->failed)
				action_check(reader, card->action);
			if (!reader->failed && card->when)
				when_check(reader, card->when);
		}
	}
	else if (parent_element == _element_screen && !strcmp(name, "colors"))
	{
		static char const *const names[] = { "id", NULL };

		element = _element_colors;
		attributes_read(reader, name, attributes, names, values);
		if (kind_count(screen, MENU_SCREEN_COLORS) || kind_count(screen, MENU_SCREEN_LIST) ||
			kind_count(screen, MENU_SCREEN_CARDS))
		{
			reader_problem(reader, "a screen has one <list>, <cards> or <colors>", NULL);
		}
		block = block_add(reader, MENU_SCREEN_COLORS, -1);
		if (block >= 0)
			screen->blocks[block].id = values[0];
	}
	else if (parent_element == _element_screen && !strcmp(name, "spartan"))
	{
		element = _element_spartan;
		block = spartan_read(reader, attributes, -1);
	}
	else if (parent_element == _element_screen && !strcmp(name, "box"))
	{
		static char const *const names[] = { "x", "y", "width", "align", NULL };

		element = _element_box;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_BOX, "box");
		if (!reader->failed && (!values[0] || !values[1] || !values[2]))
			reader_problem(reader, "a <box> has its x, y and width", NULL);
		if (!reader->failed && values[3] && strcmp(values[3], "top") && strcmp(values[3], "middle"))
			reader_problem(reader, "a <box>'s align is top or middle: \"%s\"", values[3]);
		block = block_add(reader, MENU_SCREEN_BOX, -1);
		if (block >= 0 && !reader->failed)
		{
			struct menu_screen_block *box = &screen->blocks[block];

			box->box_x = number_read(reader, values[0], "a <box>'s x", 0, 640);
			if (!reader->failed)
				box->box_y = number_read(reader, values[1], "a <box>'s y", 0, 480);
			if (!reader->failed)
				box->box_width = number_read(reader, values[2], "a <box>'s width", 64, 640);
			box->side = values[3];
		}
	}
	else if (parent_element == _element_box && !strcmp(name, "legend"))
	{
		static char const *const names[] = { "x", NULL };

		element = _element_legend;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_LEGEND, "legend");
		block = block_add(reader, MENU_SCREEN_LEGEND, parent_block);
		if (block >= 0 && !reader->failed && values[0])
			screen->blocks[block].box_x = number_read(reader, values[0], "a <legend>'s x", 0, 640);
	}
	else if (parent_element == _element_screen && !strcmp(name, "objectives"))
	{
		static char const *const names[] = { "title", NULL };

		element = _element_objectives;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_OBJECTIVES, "objectives");
		if (!reader->failed && !kind_count(screen, MENU_SCREEN_BOX))
			reader_problem(reader, "<objectives> is beside a <box> before it", NULL);
		text_attribute_check(reader, values[0]);
		block = block_add(reader, MENU_SCREEN_OBJECTIVES, -1);
		if (block >= 0)
			screen->blocks[block].label = values[0];
	}
	else if (parent_element == _element_screen && !strcmp(name, "dialog"))
	{
		static char const *const names[] = { "title", "text", NULL };

		element = _element_dialog;
		attributes_read(reader, name, attributes, names, values);
		one_check(reader, MENU_SCREEN_DIALOG, "dialog");
		if (!reader->failed && (kind_count(screen, MENU_SCREEN_BOX) || kind_count(screen, MENU_SCREEN_TABLE) ||
			kind_count(screen, MENU_SCREEN_LIST)))
			reader_problem(reader, "a <dialog> is a screen's alone, with no <box>, <table> or <list> beside it", NULL);
		if (!reader->failed && (!values[0] || !values[0][0]))
			reader_problem(reader, "a <dialog> has its title", NULL);
		text_attribute_check(reader, values[0]);
		text_attribute_check(reader, values[1]);
		block = block_add(reader, MENU_SCREEN_DIALOG, -1);
		if (block >= 0)
		{
			screen->blocks[block].label = values[0];
			screen->blocks[block].text = values[1];
		}
	}
	else if (parent_element == _element_screen && !strcmp(name, "buttons"))
	{
		static char const *const names[] = { NULL };

		element = _element_buttons;
		attributes_read(reader, name, attributes, names, values);
		if (kind_count(screen, MENU_SCREEN_BUTTONS))
			reader_problem(reader, "a screen has one <buttons>", NULL);
		block = block_add(reader, MENU_SCREEN_BUTTONS, -1);
	}
	else if (parent_element == _element_buttons && !strcmp(name, "button"))
	{
		element = _element_button;
		block = button_read(reader, attributes, parent_block);
	}
	else if ((block = element_more(reader, name, attributes, parent_element, parent_block, &element)) != -2)
	{
	}
	else
	{
		char detail[96];

		snprintf(detail, sizeof(detail), "<%s> is not one of what %s may hold", name,
			parent_element == -1 ? "a file" : "its element");
		reader_problem(reader, "%s", detail);
		return;
	}
	reader->elements[reader->depth] = element;
	reader->blocks[reader->depth] = block;
	reader->depth++;
}

static void XMLCALL element_end(void *data, XML_Char const *name)
{
	struct reader *reader = data;
	int element;

	(void)name;
	if (reader->failed || !reader->depth)
		return;
	element = reader->elements[--reader->depth];
	if (element == _element_hint)
	{
		struct menu_screen_block *item = &reader->screen->blocks[reader->blocks[reader->depth - 1]];
		char const *hint;

		if (reader->text_overflow)
		{
			reader_problem(reader, "a <hint> longer than %s bytes", "255");
			return;
		}
		if (item->hint_count >= MENU_SCREEN_MAXIMUM_HINTS)
		{
			reader_problem(reader, "an <item> has no more than %s hints", "4");
			return;
		}
		hint = reader_string(reader, reader->text);
		if (hint)
		{
			text_check(reader, hint);
			item->hints[item->hint_count++] = hint;
		}
	}
	else if (element == _element_tabs || element == _element_table || element == _element_panel ||
		element == _element_pills)
	{
		struct menu_screen *screen = reader->screen;
		int self = reader->blocks[reader->depth];
		int index, other, children = 0;
		int child_kind = element == _element_tabs ? MENU_SCREEN_TAB : element == _element_table ? MENU_SCREEN_COLUMN :
			element == _element_pills ? MENU_SCREEN_PILL : -1;

		for (index = 0; index < screen->block_count && !reader->failed; index++)
		{
			struct menu_screen_block const *child = &screen->blocks[index];

			if (child->parent != self || (child_kind >= 0 && child->kind != child_kind))
				continue;
			children++;
			for (other = index + 1; child->id && child_kind >= 0 && other < screen->block_count; other++)
			{
				if (screen->blocks[other].parent == self && screen->blocks[other].id &&
					!strcmp(screen->blocks[other].id, child->id))
					reader_problem(reader, "two of its blocks' id is \"%s\"", child->id);
			}
		}
		if (!reader->failed && element == _element_table)
		{
			int lanes = 0;

			for (index = 0; index < screen->block_count; index++)
				lanes += screen->blocks[index].parent == self && screen->blocks[index].kind == MENU_SCREEN_LANE;
			if (lanes && lanes != screen->blocks[self].lanes)
				reader_problem(reader, "a <table> of lanes has a <lane> for each lane, or none", NULL);
		}
		if (!reader->failed && !children)
		{
			reader_problem(reader, element == _element_tabs ? "a <tabs> has no <tab>" : element == _element_table ?
				"a <table> has no <column>" : element == _element_pills ? "a <pills> has no <pill>" :
				"a <panel> has nothing in it", NULL);
		}
	}
	else if (element == _element_box)
	{
		struct menu_screen *screen = reader->screen;

		if (!kind_count(screen, MENU_SCREEN_LIST))
			reader_problem(reader, "a <box> has a <list>", NULL);
	}
	/* (a dialog's choices: its list's items, 1 to MENU_SCREEN_MAXIMUM_CHOICES,
	side by side at its foot) */
	else if (element == _element_dialog)
	{
		struct menu_screen *screen = reader->screen;
		int index, list = -1, choices = 0;

		for (index = 0; index < screen->block_count; index++)
		{
			if (screen->blocks[index].kind == MENU_SCREEN_LIST)
				list = index;
		}
		for (index = 0; list >= 0 && index < screen->block_count; index++)
			choices += screen->blocks[index].kind == MENU_SCREEN_ITEM && screen->blocks[index].parent == list;
		if (list < 0)
			reader_problem(reader, "a <dialog> has a <list> of its choices", NULL);
		else if (choices > MENU_SCREEN_MAXIMUM_CHOICES)
			reader_problem(reader, "a <dialog> has at most %s choices", "4");
	}
	else if (element == _element_list || element == _element_cards)
	{
		struct menu_screen *screen = reader->screen;
		struct menu_screen_block const *list = &screen->blocks[reader->blocks[reader->depth]];
		int index, items = 0, focus_found = !list->focus;

		for (index = 0; index < screen->block_count && !reader->failed; index++)
		{
			struct menu_screen_block const *item = &screen->blocks[index];
			int other;

			if ((item->kind != MENU_SCREEN_ITEM && item->kind != MENU_SCREEN_CARD) ||
				item->parent != reader->blocks[reader->depth])
				continue;
			items++;
			if (list->focus && item->id && !strcmp(item->id, list->focus))
				focus_found = 1;
			for (other = index + 1; item->id && other < screen->block_count; other++)
			{
				if (screen->blocks[other].id && !strcmp(screen->blocks[other].id, item->id))
					reader_problem(reader, "two items' id is \"%s\"", item->id);
			}
		}
		if (!reader->failed && !items)
			reader_problem(reader, "a <list> has no <item>, or <cards> no <card>", NULL);
		if (!reader->failed && !focus_found)
			reader_problem(reader, "the <list>'s focus names no item's id: \"%s\"", list->focus);
	}
}

static void XMLCALL text_data(void *data, XML_Char const *text, int length)
{
	struct reader *reader = data;

	if (reader->failed || !reader->depth || reader->elements[reader->depth - 1] != _element_hint)
		return;
	if (reader->text_length + (unsigned long)length >= sizeof(reader->text))
	{
		reader->text_overflow = 1;
		return;
	}
	memcpy(reader->text + reader->text_length, text, (size_t)length);
	reader->text_length += (unsigned long)length;
	reader->text[reader->text_length] = 0;
}

void menu_screen_free(struct menu_screen *screen)
{
	int index;

	for (index = 0; index < screen->string_count; index++)
		free(screen->strings[index]);
	memset(screen, 0, sizeof(*screen));
}

int menu_screen_read(char const *file, unsigned char const *data, unsigned long size, struct menu_screen *screen,
	char *problem, unsigned long problem_size)
{
	struct reader reader;
	char const *file_copy;

	memset(screen, 0, sizeof(*screen));
	memset(&reader, 0, sizeof(reader));
	reader.screen = screen;
	reader.problem = problem;
	reader.problem_size = problem_size;
	if (problem_size)
		problem[0] = 0;
	file_copy = reader_string(&reader, file ? file : "?");
	screen->file = file_copy;
	reader.parser = XML_ParserCreate("UTF-8");
	if (!reader.parser)
	{
		reader_problem(&reader, "out of memory", NULL);
		menu_screen_free(screen);
		return 0;
	}
	XML_SetUserData(reader.parser, &reader);
	XML_SetElementHandler(reader.parser, element_start, element_end);
	XML_SetCharacterDataHandler(reader.parser, text_data);
	if (XML_Parse(reader.parser, (char const *)data, (int)size, XML_TRUE) == XML_STATUS_ERROR && !reader.failed)
		reader_problem(&reader, "%s", XML_ErrorString(XML_GetErrorCode(reader.parser)));
	if (!reader.failed && !screen->name)
		reader_problem(&reader, "the file has no <screen>", NULL);
	if (!reader.failed && !screen->block_count)
		reader_problem(&reader, "the <screen> has nothing in it", NULL);
	/* (a tall screen's <spartan> of its own: a lineup over its table's
	lanes) */
	if (!reader.failed && kind_count(screen, MENU_SCREEN_TABLE))
	{
		int index, lanes = 1;

		for (index = 0; index < screen->block_count; index++)
		{
			if (screen->blocks[index].kind == MENU_SCREEN_TABLE)
				lanes = screen->blocks[index].lanes;
		}
		for (index = 0; index < screen->block_count && !reader.failed; index++)
		{
			if (screen->blocks[index].kind == MENU_SCREEN_SPARTAN && screen->blocks[index].parent < 0 && lanes < 2)
			{
				char where[32];

				snprintf(where, sizeof(where), "%ld", screen->blocks[index].line);
				reader_problem(&reader, "a tall screen's own <spartan> (line %s) stands over a table of lanes", where);
			}
		}
	}
	XML_ParserFree(reader.parser);
	if (reader.failed)
	{
		menu_screen_free(screen);
		return 0;
	}
	return 1;
}

int menu_screen_text(char const *text, menu_screen_value value, void *context, char *out, unsigned long size)
{
	unsigned long used = 0;
	int complete = 1;

	if (!size)
		return 0;
	out[0] = 0;
	while (text && *text)
	{
		char const *end;

		if (*text == '{' && (end = strchr(text, '}')) != NULL)
		{
			char name[64], found[MENU_SCREEN_MAXIMUM_TEXT];
			unsigned long length;

			snprintf(name, sizeof(name), "%.*s", (int)(end - text - 1), text + 1);
			found[0] = 0;
			if (!value || !value(context, name, found, sizeof(found)) || !found[0])
				complete = 0;
			length = (unsigned long)strlen(found);
			if (used + length >= size)
				length = size - used - 1;
			memcpy(out + used, found, length);
			used += length;
			text = end + 1;
			continue;
		}
		if (used + 1 < size)
			out[used++] = *text;
		text++;
	}
	out[used] = 0;
	return complete;
}

int menu_screen_when(char const *when)
{
	char const *name = when;

	if (!when)
		return 1;
	while (*name)
	{
		unsigned long length = (unsigned long)strcspn(name, " ");

		if (length && halo_menus_platform(name, length) > 0)
			return 1;
		name += length;
		while (*name == ' ')
			name++;
	}
	return 0;
}

/* ---------- the screens */

static struct
{
	int loaded;
	struct menu_screen screens[MENU_SCREEN_MAXIMUM_SCREENS];
	/* (each one's file, as the menus' files name it or the watched folder's
	file name, and the stamp it was read at) */
	char paths[MENU_SCREEN_MAXIMUM_SCREENS][128];
	unsigned long generations[MENU_SCREEN_MAXIMUM_SCREENS];
	unsigned long generation;
	/* the watched folder's files' stamps, read or not (a file with a
	problem is not read again until it changes) */
	char stamped[MENU_SCREEN_MAXIMUM_SCREENS * 2][128];
	unsigned long long stamps[MENU_SCREEN_MAXIMUM_SCREENS * 2];
	int stamped_count;
	unsigned long polled_at;
	char folder[1024];
	int folder_read;
} screens;

/* a screen read kept: in place of the one of its name, else of its file,
else in a free place; FALSE (it let go) where there is none */
static int screens_keep(char const *path, struct menu_screen *screen)
{
	int index, place = -1;

	for (index = 0; index < MENU_SCREEN_MAXIMUM_SCREENS && place < 0; index++)
	{
		if (screens.screens[index].name && !strcmp(screens.screens[index].name, screen->name))
			place = index;
	}
	for (index = 0; index < MENU_SCREEN_MAXIMUM_SCREENS && place < 0; index++)
	{
		if (screens.screens[index].name && !strcmp(screens.paths[index], path))
			place = index;
	}
	for (index = 0; index < MENU_SCREEN_MAXIMUM_SCREENS && place < 0; index++)
	{
		if (!screens.screens[index].name)
			place = index;
	}
	if (place < 0)
	{
		platform_log("menus: more than %d screens; %s is not read", MENU_SCREEN_MAXIMUM_SCREENS, path);
		menu_screen_free(screen);
		return 0;
	}
	if (screens.screens[place].name)
		menu_screen_free(&screens.screens[place]);
	screens.screens[place] = *screen;
	snprintf(screens.paths[place], sizeof(screens.paths[place]), "%s", path);
	screens.generations[place] = ++screens.generation;
	return 1;
}

/* a file read: kept, or its problem logged (keep: what was there stays) */
static void screens_read(char const *path, unsigned char const *data, unsigned long size, int reload)
{
	struct menu_screen screen;
	char problem[512];

	if (!menu_screen_read(path, data, size, &screen, problem, sizeof(problem)))
	{
		platform_log("menus: %s%s", problem, reload ? "; the screen stays as it was" : "; the screen is not used");
		return;
	}
	if (screens_keep(path, &screen))
		platform_log("menus: screen %s %s from %s", screen.name, reload ? "read again" : "read", path);
}

static void screens_load(void)
{
	char const *path;
	unsigned char const *data;
	unsigned long size;
	long index;

	if (screens.loaded)
		return;
	screens.loaded = 1;
	if (!halo_menus_load())
		return;
	for (index = 0; halo_menus_file_at(index, &path, &data, &size); index++)
	{
		size_t length = strlen(path);

		if (!strncmp(path, SCREENS_FOLDER, strlen(SCREENS_FOLDER)) && strcmp(path, SCREENS_BLOCKS) && length > 4 &&
			!strcmp(path + length - 4, ".xml"))
			screens_read(path, data, size, 0);
	}
}

char const *menu_screens_folder(void)
{
	if (!screens.folder_read)
	{
		char const *files = config_string("debug.menu_files");

		screens.folder_read = 1;
		if (config_string("debug.menu_screens")[0])
			snprintf(screens.folder, sizeof(screens.folder), "%s", config_string("debug.menu_screens"));
		else if (files[0])
			snprintf(screens.folder, sizeof(screens.folder), "%s/screens", files);
	}
	return screens.folder;
}

struct menu_screen const *menu_screens_find(char const *name)
{
	int index;

	screens_load();
	for (index = 0; name && index < MENU_SCREEN_MAXIMUM_SCREENS; index++)
	{
		if (screens.screens[index].name && !strcmp(screens.screens[index].name, name))
			return &screens.screens[index];
	}
	return NULL;
}

unsigned long menu_screens_generation(char const *name)
{
	int index;

	for (index = 0; name && index < MENU_SCREEN_MAXIMUM_SCREENS; index++)
	{
		if (screens.screens[index].name && !strcmp(screens.screens[index].name, name))
			return screens.generations[index];
	}
	return 0;
}

#ifndef HALO_ANDROID
static unsigned char *screens_file_read(char const *path, unsigned long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *data = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0)
	{
		data = malloc((size_t)length + 1);
		if (data && fread(data, 1, (size_t)length, file) == (size_t)length)
		{
			*size = (unsigned long)length;
		}
		else
		{
			free(data);
			data = NULL;
		}
	}
	fclose(file);
	return data;
}

/* the stamp a file was last seen at (its place in stamped, made if new) */
static unsigned long long *screens_stamp(char const *name)
{
	int index;

	for (index = 0; index < screens.stamped_count; index++)
	{
		if (!strcmp(screens.stamped[index], name))
			return &screens.stamps[index];
	}
	if (screens.stamped_count >= (int)(sizeof(screens.stamps) / sizeof(screens.stamps[0])))
		return NULL;
	snprintf(screens.stamped[screens.stamped_count], sizeof(screens.stamped[0]), "%s", name);
	screens.stamps[screens.stamped_count] = 0;
	return &screens.stamps[screens.stamped_count++];
}
#endif

void menu_screens_poll(unsigned long now)
{
#ifdef HALO_ANDROID
	(void)now;
#else
	char const *folder = menu_screens_folder();
	char **names;
	int count = 0, name;

	if (!folder[0] || (screens.polled_at && now - screens.polled_at < SCREENS_POLL_MILLISECONDS))
		return;
	screens.polled_at = now ? now : 1;
	screens_load();
	names = SDL_GlobDirectory(folder, "*.xml", 0, &count);
	for (name = 0; names && name < count; name++)
	{
		char path[1200];
		SDL_PathInfo info;
		unsigned long long stamp, *seen;

		if (!strcmp(names[name], "blocks.xml"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, names[name]);
		if (!SDL_GetPathInfo(path, &info))
			continue;
		stamp = (unsigned long long)info.modify_time * 1099511628211ULL + info.size + 1;
		seen = screens_stamp(names[name]);
		if (!seen || *seen == stamp)
			continue;
		/* (the first look at a file the menus' files read already: as it
		was then, not read again) */
		if (!*seen)
		{
			char embedded[160];
			int index, known = 0;

			snprintf(embedded, sizeof(embedded), "%s%s", SCREENS_FOLDER, names[name]);
			for (index = 0; index < MENU_SCREEN_MAXIMUM_SCREENS; index++)
				known |= screens.screens[index].name && !strcmp(screens.paths[index], embedded) &&
					!config_string("debug.menu_screens")[0];
			if (known)
			{
				*seen = stamp;
				continue;
			}
		}
		*seen = stamp;
		{
			unsigned long size = 0;
			unsigned char *data = screens_file_read(path, &size);
			char named[160];

			if (!data)
			{
				platform_log("menus: %s could not be read", path);
				continue;
			}
			snprintf(named, sizeof(named), "%s%s", SCREENS_FOLDER, names[name]);
			screens_read(named, data, size, 1);
			free(data);
		}
	}
	SDL_free(names);
#endif
}
