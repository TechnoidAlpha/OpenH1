/*
MENU_SCREEN_TALL.INC

A hand-written screen of the tall layout (docs/menu_screens.md): one with a
<table>, laid out as Settings' and the browsers' screens are, the screen's
whole height (tools/port_settings.py's BROWSER_TALL): its <header>'s title in
the band at the top (its detail at the band's right, its button in it), its
<tabs> under the band, the <table> under them at the left (its columns'
titles, the line, its rows; its words for no rows, and its button, in their
place) and its <panel> beside it at the right, a <search> of its own at the
top's right, and the <buttons> at the foot. On a touchscreen, its touch
layout: the tabs chips, a row two lines of its cells (each column's touch
place), no columns' titles (a <search> of the screen's at the top's right,
SORT naming the sort), the buttons a touchscreen's.

Each block's widgets are made once, as the screen is made (menu_screen.c's
screen_build), their places, widths and pictures kept here (screen_box): each
frame they are moved and made wider on a wide screen (16:9's layout: each
box's "dx dw", and past 16:9 the tall layout's past), and the table's rows,
the panel's lines and the rest are shown from the bindings and the table's
row source. Included by menu_screen.c.
*/

/* ---------- the layout, in the menus' 640 by 480 units (tools/port_settings.py's
BROWSER_TALL, _browser and _touch_browser) */

/* the band, its title's detail at its right, a button in it */
#define TALL_BAND 40
#define TALL_DETAIL_X 300
#define TALL_DETAIL_Y 11
#define TALL_DETAIL_WIDTH 312
#define TALL_BUTTON_X 276
#define TALL_BUTTON_Y 8
#define TALL_BUTTON_WIDTH 148
/* the tabs: their strip's top, the first's left, each's step and width;
their keys' prompts at the screen's margin and after the last */
#define TALL_TABS_Y 46
#define TALL_TABS_X 56
#define TALL_TAB_STEP 88
#define TALL_TAB_WIDTH 86
#define TALL_TAB_KEY_X 24
/* the table: its left and width, its titles' top, the line under them, the
rows' top, height and step, the bottom they end over (the panel's) */
#define TALL_TABLE_X 28
#define TALL_TABLE_WIDTH 404
#define TALL_TITLES_Y 74
#define TALL_LINE_Y 88
#define TALL_ROWS_Y 92
#define TALL_ROW_HEIGHT 24
#define TALL_ROWS_BOTTOM 436
/* a group's heading among the rows (the server browser's, the co-op list's):
the blocks' dim blue (blocks.xml's card subtitle's), 0xRRGGBBAA */
#define TALL_HEADING_COLOR 0x6F9CD0FFUL
#define TALL_TABLE_GROW 144
/* a row's cells: the first's left, the cells'
pictures' top in a row (16 units tall, in its middle; the gap between two
columns is theirs, <column gap>), the gap before a
pill or a lock after a cell's words, a lock's room, a ping's bars' width
and the gap before its number */
#define TALL_CELLS_X 6
#define TALL_CELL_ICON_Y 4
#define TALL_BADGE_GAP 4
#define TALL_BADGE_WIDTH 30
#define TALL_LOCK_WIDTH 14
#define TALL_BARS_WIDTH 24
#define TALL_BARS_GAP 2
/* (a title's arrow's room, and the gap before a search's glass) */
#define TALL_TITLE_ROOM 20
#define TALL_TITLE_RIGHT_ROOM 6
#define TALL_ARROW_GAP 2
#define TALL_SEARCH_GAP 2
/* the words for no rows, the button under them */
#define TALL_EMPTY_Y 220
#define TALL_EMPTY_HEIGHT 80
#define TALL_EMPTY_INSET 24
#define TALL_TABLE_BUTTON_Y 308
#define TALL_TABLE_BUTTON_WIDTH 164
/* the panel, its inner margin, its button's room at its foot */
#define TALL_PANEL_X 440
#define TALL_PANEL_Y 74
#define TALL_PANEL_WIDTH 172
#define TALL_PANEL_HEIGHT 362
#define TALL_PANEL_MOVE 38
#define TALL_PANEL_GROW 68
#define TALL_PANEL_INSET 12
#define TALL_PANEL_BUTTON_FOOT 10
#define TALL_PILL_HEIGHT 16
#define TALL_PILL_GAP 4
/* the bar: its top (the tall layout's footer: TALL_FOOTER from the foot),
its prompts' margin and the gap between them, their height and words' left */
#define TALL_BAR_Y 448
#define TALL_PROMPT_MARGIN 24
#define TALL_PROMPT_GAP 2
#define TALL_PROMPT_HEIGHT 24
#define TALL_PROMPT_TEXT_X 5
#define TALL_PROMPT_RIGHT_ROOM 4

/* a touchscreen's (_touch_browser's): a tap target's size, the gap between
two, the chips' top and left, the rows' top and width, the panel beside
them, the search at the top's right, the bar's top and its buttons' widths */
#define TOUCH_TAP 52
#define TOUCH_GAP 4
#define TOUCH_CHIPS_Y 76
#define TOUCH_CHIP_WIDTH 92
#define TOUCH_CHIP_STEP 98
#define TOUCH_CHIP_GROW 35
#define TOUCH_ROWS_Y 134
#define TOUCH_ROW_STEP 56
#define TOUCH_TABLE_WIDTH 364
#define TOUCH_ROWS_BOTTOM 410
#define TOUCH_DETAIL_X 66
#define TOUCH_DETAIL_Y 46
#define TOUCH_EMPTY_Y 230
#define TOUCH_TABLE_BUTTON_Y 316
#define TOUCH_TABLE_BUTTON_WIDTH 200
#define TOUCH_PANEL_X 400
#define TOUCH_PANEL_WIDTH 212
#define TOUCH_PANEL_HEIGHT 276
#define TOUCH_SEARCH_X 380
#define TOUCH_SEARCH_Y 18
#define TOUCH_SEARCH_WIDTH 176
#define TOUCH_SEARCH_MOVE (-102)
#define TOUCH_SEARCH_GROW 208
#define TOUCH_SEARCH_ICON 20
#define TOUCH_BAR_Y 414
#define TOUCH_PROMPT_WIDTH 136
#define TOUCH_BACK_WIDTH 110
#define TOUCH_PROMPT_GAP 6
#define TOUCH_MARGIN 28
/* (a heading's words in a touchscreen's row, on its first line) */
#define TOUCH_ROW_HEADING_Y 7

/* the panel's lines, one after another from its top (desktop, touch): the
first's gap, a heading's line, a text's line (a short text's and a long
text's), the gaps before pills (after a text), a pair (after
pills) and a text after a pair, a pill row's step, a pair's step */
#define PANEL_FIRST_GAP 12
#define TOUCH_PANEL_FIRST_GAP 10
#define PANEL_HEADING_LINE 24
#define TOUCH_PANEL_HEADING_LINE 22
#define PANEL_TEXT_LINE 22
#define PANEL_LONG_LINE 20
#define TOUCH_PANEL_TEXT_LINE 15
#define TOUCH_PANEL_LONG_LINE 21
#define PANEL_PILLS_GAP 4
#define TOUCH_PANEL_PILLS_GAP 2
#define PANEL_PAIR_GAP 3
#define PANEL_AFTER_PAIR_GAP 7
#define PANEL_PILL_STEP 20
#define PANEL_PAIR_STEP 22
#define PANEL_PAIR_LABEL_Y 3
#define PANEL_PAIR_LABEL_WIDTH 70
/* a table's lanes (lanes="2", the lobby's teams), side by side, as the
generated lobby's columns (tools/port_settings.py's LOBBY_*): the gap
between two, each its panel from the titles' top (a touchscreen's, the
chips') to the rows' bottom, its title and detail at its top, its rows under
them, each 8 in from its sides; a row's cells' words and dot in it */
#define TALL_LANE_GAP 12
#define TALL_LANE_TITLE_X 12
#define TALL_LANE_TITLE_Y 8
#define TALL_LANE_DETAIL_Y 12
#define TALL_LANE_DETAIL_WIDTH 90
#define TALL_LANE_ROWS_TOP 36
#define TALL_LANE_ROW_STEP 20
#define TALL_LANE_ROW_HEIGHT 18
#define TALL_LANE_ROW_INSET 8
#define TALL_LANE_CELL_Y (-3)
#define TALL_LANE_DOT 8
/* (a lane's title's colour, by its tone: red's, blue's; the third, its
own) */
#define TALL_LANE_RED 0xFF6B5EFFUL
#define TALL_LANE_BLUE 0x5AA9FFFFUL
#define TALL_MAXIMUM_LANES 4
/* a panel's choice: its height, its label's width, its value's place
between the arrows from its left, the arrows' size; a touchscreen's
height; the panel's buttons' gap one over another */
#define PANEL_CHOICE_HEIGHT 22
#define TOUCH_PANEL_CHOICE_HEIGHT 32
#define PANEL_CHOICE_LABEL_X 8
#define PANEL_CHOICE_LABEL_WIDTH 58
#define PANEL_CHOICE_VALUE_X 58
#define PANEL_CHOICE_ARROW 6
#define PANEL_CHOICE_GAP 0
#define PANEL_BUTTON_GAP 4
#define TALL_MAXIMUM_PANEL_BUTTONS 4
/* a chat (<chat>, the lobby's) under the table, across it: its line's
height, the line typed's (a touchscreen's), its inner margin, the gap over
it; the on-screen keyboard's rows' height over the panel's foot */
#define TALL_CHAT_LINE 15
#define TOUCH_CHAT_LINE 16
#define TALL_CHAT_INPUT 20
#define TOUCH_CHAT_INPUT 40
#define TALL_CHAT_INSET 6
#define TALL_CHAT_GAP 8
#define TALL_CHAT_NAME_GAP 6
#define TALL_CHAT_KEYBOARD_ROW 30
#define TALL_MAXIMUM_CHAT_LINES 12
#define TALL_MAXIMUM_PANELS MENU_SCREEN_MAXIMUM_PANELS
/* a panel's <picture>: its least height (less room left, it is gone), its
shape (the menus' pictures', 140 by 114: menu_pictures.h) */
#define PANEL_PICTURE_LEAST 40
#define PANEL_PICTURE_WIDTH 140
#define PANEL_PICTURE_HEIGHT 114
#define PANEL_PICTURE_GAP 6
/* a tall screen's own <spartan>, a lineup over its table's lanes: its most
height, and the gap under it */
#define TALL_LINEUP_MOST 160
#define TALL_LINEUP_GAP 4
/* (its places' names: the line under the Spartans' feet, in its height,
and the gap between two places' names) */
#define TALL_LINEUP_NAME 14
#define TALL_LINEUP_NAME_GAP 4
/* (a <spartan>'s line: the gap before and after it) */
#define PANEL_SPARTAN_GAP 6
#define PANEL_SPARTAN_FEET 25

/* (the menus' sounds: menu_functions.c's) */
#define TALL_SOUND_FORWARD 2
#define TALL_SOUND_BACK 3

/* (16:9's width past the 640, and 21:9's past 16:9's: menu_tags.c's) */
#define TALL_PAST_EXTRA 286
/* (a ping's colours by how good: menu_functions.c's) */
#define TALL_MAXIMUM_ROWS 40
#define TALL_MAXIMUM_COLUMNS 8
#define TALL_MAXIMUM_SEARCHES 4
#define TALL_MAXIMUM_TABS 8
#define TALL_MAXIMUM_LINES 48
#define TALL_MAXIMUM_BOXES 760
#define TALL_MAXIMUM_TARGETS 128
#define TALL_MAXIMUM_PROMPTS 10
/* (an arrow's width in a key's cap, < or >: small_ui's) */
#define TALL_ARROW_LETTERS 6

/* small_ui's letters' advances (tools/port_settings.py's SMALL_UI_ADVANCES:
ui.map's font tag's), printable ASCII from the space, for the layout's
widths as the generator works them out */
static unsigned char const tall_small_advances[95] =
{
	4,3,7,10,8,13,9,6,5,5,7,8,3,5,3,6,9,6,9,9,10,9,9,8,9,9,3,3,6,7,6,8,11,10,10,9,10,8,7,10,10,4,9,9,8,11,10,10,9,
	10,10,9,9,11,9,12,9,10,9,5,6,4,8,7,5,8,8,7,9,7,5,9,9,4,5,7,4,12,8,8,8,9,6,8,6,9,7,11,7,8,7,6,4,5,8
};

static short tall_small_width(char const *text)
{
	short width = 0;

	for (; text && *text; text++)
	{
		unsigned char letter = (unsigned char)*text;

		width = (short)(width + (letter >= 32 && letter < 127 ? tall_small_advances[letter - 32] :
			tall_small_advances['W' - 32]));
	}
	return width;
}

/* ---------- the boxes: each widget's place and size, moved and grown on a
wide screen */

struct screen_box
{
	struct widget_instance *widget;
	/* its place at 4:3, and its move and growth at 16:9 (past it, as much
	again for each unit, the tall layout's past, unless centred) */
	short x, y;
	short dx, dw;
	boolean centred;
	/* its box (0: its definition's), its picture (NONE: its definition's) */
	short width, height;
	long picture;
	/* its words' colour, 0xRRGGBBAA (0: its own) */
	unsigned long color;
	/* (the box it is in, NONE: the screen; and where it is now, on the
	screen: a widget's offsets are from its parent's) */
	long parent;
	short at_x, at_y;
};

/* what a widget of the screen does when A, Start, a click or a tap is on it */
enum
{
	_target_tab,
	_target_title,
	_target_search_icon,
	_target_search_field,
	_target_search_clear,
	_target_row,
	_target_button,
	_target_choice,
	_target_chat,
};

struct screen_target
{
	struct widget_instance *widget;
	short kind;
	short index;
	char const *action;
};

struct screen_column
{
	struct menu_screen_block const *block;
	/* its place in a row and its title's, at 4:3, and their moves and
	growth at 16:9 */
	short x, width, move, grow;
	short title_x, title_width;
	struct widget_instance *title, *arrow_up, *arrow_down;
	/* (its search's: NONE for none) */
	short search;
};

struct screen_row_widgets
{
	/* (its lane, 0 for a table without) */
	short lane;
	struct widget_instance *row, *heading;
	struct widget_instance *text[TALL_MAXIMUM_COLUMNS], *badge[TALL_MAXIMUM_COLUMNS], *lock[TALL_MAXIMUM_COLUMNS],
		*picture[TALL_MAXIMUM_COLUMNS];
};

struct screen_search
{
	struct menu_screen_block const *block;
	/* its parts: the glass (or the touchscreen's bar), the field typed
	into, its X; the title it takes the place of (NULL: none) */
	struct widget_instance *icon, *field, *clear, *title;
	/* the list its parts are in */
	struct widget_instance *list;
	short field_width_base, field_grow;
};

struct screen_line
{
	struct menu_screen_block const *block;
	struct widget_instance *widget, *label;
	/* (a choice's: its value and arrows, in its row) */
	struct widget_instance *value, *arrows[2];
	short width, grow;
	/* (a panel's line: its top as made, and its room, which the lines
	after it take while its if has no value; its panel; a picture's most
	height, its room as made, and a <spartan>'s, its screen's <spartan>) */
	short y, room;
	short panel;
	short most;
	long spartan;
	/* (a text's lines it does not fill now, in a panel with a picture: the
	picture's room, the lines after it moved up into it) */
	short spare;
};


struct screen_prompt
{
	struct menu_screen_block const *block;
	struct widget_instance *widget;
	char key[8];
	/* (the bar's: its width, and with the controller's pictures) */
	short width, grown;
};

/* a <panel> (one of a screen's, the first whose if has a value shown): its
block, its picture, its buttons, where its lines end and its buttons begin
as made */
struct screen_panel
{
	struct menu_screen_block const *block;
	struct widget_instance *widget;
	struct widget_instance *bars[TALL_MAXIMUM_PANEL_BUTTONS];
	struct screen_prompt buttons[TALL_MAXIMUM_PANEL_BUTTONS];
	long button_count;
	short lines_bottom;
};

struct screen_tall
{
	struct screen_box boxes[TALL_MAXIMUM_BOXES];
	long box_count;
	struct screen_target targets[TALL_MAXIMUM_TARGETS];
	long target_count;
	/* the values of the bindings, read once as the screen is made (the
	campaign's), and the rest each frame */
	struct screen_values values;
	/* the screen (the parts that take no focus), and its body (the rest) */
	struct widget_instance *owner, *body;
	/* the header's detail and the band's button (in a bar of its own, the
	body's first: the d-pad's and the stick's, up from the tabs, as well as
	the pointer's and its key's) */
	struct menu_screen_block const *header;
	struct widget_instance *detail;
	struct widget_instance *header_bar;
	struct screen_prompt header_button;
	/* the tabs */
	struct menu_screen_block const *tabs;
	struct widget_instance *strip;
	struct menu_screen_block const *tab_blocks[TALL_MAXIMUM_TABS];
	struct widget_instance *tab_widgets[TALL_MAXIMUM_TABS];
	long tab_count;
	short tab_shown;
	struct widget_instance *tab_keys[2];
	/* the table */
	struct menu_screen_block const *table;
	struct menu_table_source const *source;
	struct widget_instance *titles, *line, *empty;
	struct screen_column columns[TALL_MAXIMUM_COLUMNS];
	long column_count;
	struct screen_row_widgets rows[TALL_MAXIMUM_ROWS];
	short row_count;
	short row_height, row_step, rows_y;
	struct widget_instance *table_bar;
	struct screen_prompt table_button;
	struct menu_table_view view;
	/* its lanes: how many, the rows each has, and each one's block, panel,
	title and detail; the panel across them all, while a lane's label has
	no value (one lane then) */
	short lanes, lane_rows;
	struct menu_screen_block const *lane_blocks[TALL_MAXIMUM_LANES];
	struct widget_instance *lane_panels[TALL_MAXIMUM_LANES], *lane_titles[TALL_MAXIMUM_LANES],
		*lane_details[TALL_MAXIMUM_LANES];
	struct widget_instance *lane_span;
	/* (the focus put on the bar for want of a row: back on the first row
	that comes, unless it has been moved since) */
	boolean focus_waiting;
	/* its chat: its block and source, its height (the table's foot over it),
	its panel, each line's name and words, the line typed's place, and the
	layer its on-screen keyboard is drawn on, over the rest */
	struct menu_screen_block const *chat;
	struct menu_chat_source const *chat_source;
	short chat_height;
	struct widget_instance *chat_panel, *chat_names[TALL_MAXIMUM_CHAT_LINES], *chat_texts[TALL_MAXIMUM_CHAT_LINES];
	struct widget_instance *chat_input, *chat_keyboard;
	/* the searches (a column's, or the screen's), the one typed into */
	struct screen_search searches[TALL_MAXIMUM_SEARCHES];
	long search_count;
	short search_editing;
	/* the panels, their lines; the one shown (NONE before the first frame)
	and its picture; the focus before another than the first was shown */
	struct screen_line lines[TALL_MAXIMUM_LINES];
	long line_count;
	struct screen_panel panels[TALL_MAXIMUM_PANELS];
	long panel_count;
	short panel_shown;
	struct widget_instance *panel_widget;
	struct widget_instance *panel_focus_before;
	/* its own <spartan>, a lineup over the table's lanes: the widget its
	Spartans are drawn at, its height, its screen's <spartan> (NONE: none) */
	struct widget_instance *lineup;
	short lineup_height;
	long lineup_spartan;
	/* (its places' names, under their Spartans: its names' binding, NULL
	for none) */
	char const *lineup_names_binding;
	struct widget_instance *lineup_names[MAXIMUM_MENU_SPARTAN_PLACES];
	/* the bar's prompts */
	struct widget_instance *bar;
	struct screen_prompt prompts[TALL_MAXIMUM_PROMPTS];
	long prompt_count;
	/* (the bar's prompts shown as it was last laid out, a bit of each) */
	unsigned long bar_shown;
};

/* the tall screens open, by their screen_open's place */
static struct screen_tall *tall_screens[SCREEN_MAXIMUM_OPEN];
static struct screen_tall tall_storage[2];

static struct screen_tall *tall_of(struct screen_open const *open)
{
	return tall_screens[open - screens.open];
}

/* a move or growth of dx at 16:9 now (and past 16:9 as much again for each
unit, the tall layout's past: by the screen's edges' move for a move alone,
grows: by its whole width), none for a centred box */
static short tall_wide(short dx, boolean grows, boolean centred)
{
	short moved = ui_widget_port_wide_scale(dx, ui_widget_port_wide_extra());
	long past = (long)dx * TALL_PAST_EXTRA;

	if (centred || ui_widget_port_touch_menus())
		return moved;
	past = (past + (past < 0 ? -WIDE_LAYOUT_EXTRA / 2 : WIDE_LAYOUT_EXTRA / 2)) / WIDE_LAYOUT_EXTRA;
	return (short)(moved + ui_widget_port_past_scale((short)past, grows ? ui_widget_port_past_extra() :
		ui_widget_port_past_move()));
}

static struct screen_box *tall_box_of(struct screen_tall *tall, struct widget_instance const *widget)
{
	long index;

	for (index = 0; widget && index < tall->box_count; index++)
	{
		if (tall->boxes[index].widget == widget)
			return &tall->boxes[index];
	}
	return NULL;
}

/* a block's widget made in the parent at x, y (moved dx and dw wider at
16:9), as wide and tall as width and height (0: its own), kept as a box */
static struct widget_instance *tall_add(struct screen_tall *tall, struct widget_instance *parent, long tag, short x,
	short y, short dx, short dw, short width, short height)
{
	struct widget_instance *widget;
	struct screen_box *box;

	if (tag == NONE || !parent || tall->box_count >= TALL_MAXIMUM_BOXES)
		return NULL;
	widget = ui_widget_port_add_child(parent, tag, NULL);
	if (!widget)
		return NULL;
	box = &tall->boxes[tall->box_count++];
	memset(box, 0, sizeof(*box));
	box->widget = widget;
	box->x = x;
	box->y = y;
	box->dx = dx;
	box->dw = dw;
	box->width = width;
	box->height = height;
	box->picture = NONE;
	box->parent = NONE;
	{
		long index;

		for (index = 0; index < tall->box_count - 1; index++)
		{
			if (tall->boxes[index].widget == parent)
				box->parent = index;
		}
	}
	widget->horizontal_offset = x;
	widget->vertical_offset = y;
	return widget;
}

static void tall_target(struct screen_tall *tall, struct widget_instance *widget, short kind, short index,
	char const *action)
{
	if (!widget || tall->target_count >= TALL_MAXIMUM_TARGETS)
		return;
	tall->targets[tall->target_count].widget = widget;
	tall->targets[tall->target_count].kind = kind;
	tall->targets[tall->target_count].index = index;
	tall->targets[tall->target_count].action = action;
	tall->target_count++;
}

/* a picture of the menus' parts by its name and size ("main_menu/parts/
row_%dx%d"), NONE if there is none (the widget's own then) */
static long tall_picture(char const *format, short width, short height)
{
	char name[128];
	char tag_name[160];
	long index;

	snprintf(name, sizeof(name), format, width, height);
	snprintf(tag_name, sizeof(tag_name), "pc\\%s", name);
	for (index = 0; tag_name[index]; index++)
	{
		if (tag_name[index] == '/')
			tag_name[index] = '\\';
	}
	return tag_loaded(BITMAP_GROUP_TAG, tag_name);
}

static void tall_box_picture(struct screen_tall *tall, struct widget_instance *widget, char const *format, short width,
	short height)
{
	struct screen_box *box = tall_box_of(tall, widget);

	if (box)
		box->picture = tall_picture(format, width, height);
}

static void tall_centred(struct screen_tall *tall, struct widget_instance *widget)
{
	struct screen_box *box = tall_box_of(tall, widget);

	if (box)
		box->centred = TRUE;
}

/* a text's value now (UTF-8): its bindings filled; FALSE (empty) if one has
none */
static boolean tall_text(struct screen_tall *tall, char const *text, char *out, unsigned long size)
{
	if (!text)
	{
		out[0] = 0;
		return FALSE;
	}
	if (!menu_screen_text(text, screen_value, &tall->values, out, size))
	{
		out[0] = 0;
		return FALSE;
	}
	return TRUE;
}

/* a binding's value now ("" for none) */
static char const *tall_binding(struct screen_tall *tall, char const *name, char *out, unsigned long size)
{
	out[0] = 0;
	if (name && !screen_value(&tall->values, name, out, size))
		out[0] = 0;
	return out;
}

/* whether a block is shown now: its when holds, its if has a value */
static boolean tall_shown(struct screen_tall *tall, struct menu_screen_block const *block)
{
	char value[MENU_SCREEN_MAXIMUM_TEXT];

	if (!menu_screen_when(block->when))
		return FALSE;
	return !block->condition || tall_binding(tall, block->condition, value, sizeof(value))[0];
}

static void tall_text_set(struct widget_instance *widget, char const *utf8)
{
	if (widget)
		screen_text_set(widget, utf8);
}

static void visible_set_safe(struct widget_instance *widget, boolean visible)
{
	if (widget)
		widget->visible = visible;
}

/* the key's picture's name in a prompt's words ("%a-button") */
static char const *tall_key_picture(char const *key)
{
	static struct
	{
		char const *key;
		char const *picture;
	} const pictures[] =
	{
		{ "accept", "%a-button" }, { "back", "%b-button" }, { "x", "%x-button" }, { "y", "%y-button" },
		{ "lb", "%white-button" }, { "rb", "%black-button" }, { "lt", "%left-trigger" },
		{ "rt", "%right-trigger" }, { "r3", "%right-thumb" }, { "view", "%back-button" },
		{ "dpad", "%dpad-left%dpad-right" },
	};
	long index;

	/* (the D-pad's left and right: on the keyboard, its arrows' keys,
	ui_widget_port_menu_key) */
	for (index = 0; index < NUMBEROF(pictures); index++)
	{
		if (!strcmp(pictures[index].key, key))
			return pictures[index].picture;
	}
	return "%a-button";
}

/* the button a key is (input.h's gamepad buttons) */
static short tall_key_button(char const *key)
{
	static struct
	{
		char const *key;
		short button;
	} const buttons[] =
	{
		{ "accept", _gamepad_analog_button_a }, { "back", _gamepad_analog_button_b },
		{ "x", _gamepad_analog_button_x }, { "y", _gamepad_analog_button_y },
		{ "lb", _gamepad_analog_button_white }, { "rb", _gamepad_analog_button_black },
		{ "lt", _gamepad_analog_button_left_trigger }, { "rt", _gamepad_analog_button_right_trigger },
		{ "r3", _gamepad_binary_button_right_thumb }, { "view", _gamepad_binary_button_back },
	};
	long index;

	for (index = 0; key && index < NUMBEROF(buttons); index++)
	{
		if (!strcmp(buttons[index].key, key))
			return buttons[index].button;
	}
	return NONE;
}

/* the keyboard's key a prompt's button is drawn as (tools/port_settings.py's
BROWSER_KEYS: a screen with a table has the browsers' keys), for its width */
static char const *tall_keyboard_key(char const *key)
{
	static struct
	{
		char const *key;
		char const *name;
	} const names[] =
	{
		{ "accept", "Enter" }, { "back", "Esc" }, { "x", "Tab" }, { "y", "F" }, { "lb", "Q" }, { "rb", "E" },
		{ "lt", "L" }, { "rt", "R" }, { "r3", "/" }, { "view", "F1" },
	};
	long index;

	for (index = 0; index < NUMBEROF(names); index++)
	{
		if (!strcmp(names[index].key, key))
			return names[index].name;
	}
	return "E";
}

/* where a prompt's words end with the keyboard's key (port_settings.py's
prompt_words_end) and with the controller's picture (controller_prompt_end) */
static short tall_prompt_end(char const *key, char const *label, boolean controller)
{
	char spaced[MENU_SCREEN_MAXIMUM_TEXT];
	short advance;

	snprintf(spaced, sizeof(spaced), " %s", label);
	if (controller)
	{
		advance = !strcmp(key, "rt") ? 69 : !strcmp(key, "lt") ? 62 : !strcmp(key, "r3") ? 65 : !strcmp(key, "dpad") ?
			2 * 9 : 9;
	}
	else if (!strcmp(key, "dpad"))
	{
		/* (two keys' caps, the left arrow's and the right's, < and >:
		TALL_ARROW_LETTERS) */
		short width = (short)MAX(TALL_ARROW_LETTERS + 2 * 3, 19);

		width = (short)(width + (width - TALL_ARROW_LETTERS) % 2);
		advance = (short)(2 * (4 + width + 3));
	}
	else
	{
		short letters = tall_small_width(tall_keyboard_key(key));
		short width = (short)MAX(letters + 2 * 3, 19);

		width = (short)(width + (width - letters) % 2);
		advance = (short)(4 + width + 3);
	}
	return (short)(advance + tall_small_width(spaced));
}

/* ---------- making the screen */

static long tall_tag(char const *name)
{
	char tag_name[96];

	snprintf(tag_name, sizeof(tag_name), "pc\\screens\\%s", name);
	return tag_loaded(UI_WIDGET_DEFINITION_TAG, tag_name);
}

/* the band: its detail, its button */
static void tall_build_header(struct screen_tall *tall, struct widget_instance *screen,
	struct menu_screen const *file, long header_index)
{
	struct menu_screen_block const *header = &file->blocks[header_index];
	boolean touch = ui_widget_port_touch_menus() != 0;
	long index;

	tall->header = header;
	if (header->empty)
	{
		tall->detail = touch ?
			tall_add(tall, screen, tall_tag("header_detail_left"), TOUCH_DETAIL_X, TOUCH_DETAIL_Y, -106, 0, 0, 0) :
			tall_add(tall, screen, tall_tag("header_detail"), TALL_DETAIL_X, TALL_DETAIL_Y, 106, 0, TALL_DETAIL_WIDTH, 0);
	}
	for (index = 0; index < file->block_count; index++)
	{
		struct menu_screen_block const *button = &file->blocks[index];
		struct widget_instance *made;

		if (button->kind != MENU_SCREEN_BUTTON || button->parent != header_index || !menu_screen_when(button->when))
			continue;
		/* (in a bar, as the table's and the panel's buttons: the pointer
		clicks and taps only a list's items, and the screen is none; the
		body's first, so that the d-pad and the stick reach it as they reach
		every other focusable block, up from the tabs (or the first row), and
		A presses it) */
		tall->header_bar = tall_add(tall, tall->body, tall_tag("bar"), 0, TALL_BUTTON_Y, 0, 0, 0, 0);
		made = tall_add(tall, tall->header_bar, screens.button_tag, TALL_BUTTON_X, TALL_BUTTON_Y, 0, 0, 0, 0);
		tall_centred(tall, made);
		if (made)
			tall->header_bar->focused_child = made;
		tall->header_button.block = button;
		tall->header_button.widget = made;
		snprintf(tall->header_button.key, sizeof(tall->header_button.key), "%s", button->key);
		if (button->action)
			tall_target(tall, made, _target_button, NONE, button->action);
		break;
	}
}

/* the tabs: a strip of them (a touchscreen's chips), spread on a wide
screen, and their keys' prompts at either end */
static void tall_build_tabs(struct screen_tall *tall, struct widget_instance *screen, struct menu_screen const *file,
	long tabs_index)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	long index, count = 0, made_index = 0;
	short y = touch ? TOUCH_CHIPS_Y : TALL_TABS_Y;

	tall->tabs = &file->blocks[tabs_index];
	tall->tab_shown = NONE;
	tall->strip = tall_add(tall, tall->body, tall_tag("tabs"), 0, y, 0, 0, 0, touch ? TOUCH_TAP : 0);
	for (index = 0; index < file->block_count; index++)
		count += file->blocks[index].kind == MENU_SCREEN_TAB && file->blocks[index].parent == tabs_index &&
			menu_screen_when(file->blocks[index].when);
	for (index = 0; tall->strip && index < file->block_count && made_index < TALL_MAXIMUM_TABS; index++)
	{
		struct menu_screen_block const *tab = &file->blocks[index];
		struct widget_instance *made;
		char label[MENU_SCREEN_MAXIMUM_TEXT];

		if (tab->kind != MENU_SCREEN_TAB || tab->parent != tabs_index || !menu_screen_when(tab->when))
			continue;
		if (touch)
		{
			made = tall_add(tall, tall->strip, tall_tag("chip"), (short)(TOUCH_MARGIN + made_index * TOUCH_CHIP_STEP), y,
				(short)(-106 + made_index * TOUCH_CHIP_GROW), TOUCH_CHIP_GROW, 0, 0);
		}
		else
		{
			made = tall_add(tall, tall->strip, tall_tag("tab"), (short)(TALL_TABS_X + made_index * TALL_TAB_STEP), y,
				0, 0, 0, 0);
			/* (each moved by its part of 16:9's room, as Settings' tabs:
			rounded as the generator rounds them) */
			if (made)
			{
				double exact = -106.0 + 212.0 * (double)made_index / (double)(count > 1 ? count - 1 : 1);

				tall_box_of(tall, made)->dx = (short)(exact < 0 ? -(long)(-exact + 0.5) : (long)(exact + 0.5));
			}
			tall_centred(tall, made);
		}
		if (!made)
			break;
		tall_text(tall, tab->label, label, sizeof(label));
		tall_text_set(made, label);
		tall->tab_blocks[made_index] = tab;
		tall->tab_widgets[made_index] = made;
		tall_target(tall, made, _target_tab, (short)made_index, tab->action);
		made_index++;
	}
	tall->tab_count = made_index;
	/* (the keys that step them: the computer's, beside them) */
	if (!touch && made_index)
	{
		struct widget_instance *key;

		key = tall_add(tall, screen, tall_tag("tab_key"), TALL_TAB_KEY_X, y, -106, 0, 0, 0);
		tall_centred(tall, key);
		tall_text_set(key, "%white-button");
		tall->tab_keys[0] = key;
		key = tall_add(tall, screen, tall_tag("tab_key"),
			(short)(TALL_TABS_X + (made_index - 1) * TALL_TAB_STEP + TALL_TAB_WIDTH + 4), y, 106, 0, 0, 0);
		tall_centred(tall, key);
		tall_text_set(key, "%black-button");
		tall->tab_keys[1] = key;
	}
}

/* a search's parts: in a column's title (the glass after the title's
words, the field in the column's place, its X at the column's right), or
the screen's own at the top's right (a touchscreen's: its bar, its field,
its X beside them) */
static void tall_build_search(struct screen_tall *tall, struct menu_screen_block const *block,
	struct screen_column *column)
{
	struct screen_search *search;
	short index;

	if (tall->search_count >= TALL_MAXIMUM_SEARCHES || !menu_screen_when(block->when))
		return;
	index = (short)tall->search_count++;
	search = &tall->searches[index];
	memset(search, 0, sizeof(*search));
	search->block = block;
	if (column)
	{
		short y = TALL_TITLES_Y;
		short field_width = (short)(column->width - 16 - TALL_SEARCH_GAP);

		search->list = tall->titles;
		search->title = column->title;
		search->icon = tall_add(tall, tall->titles, tall_tag("search_icon"),
			(short)(TALL_TABLE_X + column->title_x + column->title_width), y, (short)(-106 + column->move), 0, 0, 0);
		search->field = tall_add(tall, tall->titles, tall_tag("search_field"), (short)(TALL_TABLE_X + column->x), y,
			(short)(-106 + column->move), column->grow, field_width, 0);
		tall_box_picture(tall, search->field, "main_menu/parts/outline_%dx%d", field_width, 16);
		search->clear = tall_add(tall, tall->titles, tall_tag("search_clear"),
			(short)(TALL_TABLE_X + column->x + column->width - 16), y, (short)(-106 + column->move + column->grow), 0,
			0, 0);
		search->field_width_base = field_width;
		search->field_grow = column->grow;
		column->search = index;
	}
	else
	{
		char label[MENU_SCREEN_MAXIMUM_TEXT];
		short icon_y = (TOUCH_TAP - TOUCH_SEARCH_ICON) / 2;

		search->list = tall_add(tall, tall->body, tall_tag("titles"), 0, TOUCH_SEARCH_Y, 0, 0, 0, TOUCH_TAP);
		search->icon = tall_add(tall, search->list, tall_tag("touch_search"), TOUCH_SEARCH_X, TOUCH_SEARCH_Y,
			TOUCH_SEARCH_MOVE, TOUCH_SEARCH_GROW, 0, 0);
		tall_add(tall, search->icon, tall_tag("touch_search_glass"), (short)(TOUCH_SEARCH_X + icon_y),
			(short)(TOUCH_SEARCH_Y + icon_y), TOUCH_SEARCH_MOVE, 0, 0, 0);
		tall_text(tall, block->label ? block->label : "SEARCH", label, sizeof(label));
		tall_text_set(search->icon, label);
		search->field = tall_add(tall, search->list, tall_tag("touch_search_field"), TOUCH_SEARCH_X, TOUCH_SEARCH_Y,
			TOUCH_SEARCH_MOVE, TOUCH_SEARCH_GROW, 0, 0);
		tall_add(tall, search->field, tall_tag("touch_search_glass"), (short)(TOUCH_SEARCH_X + icon_y),
			(short)(TOUCH_SEARCH_Y + icon_y), TOUCH_SEARCH_MOVE, 0, 0, 0);
		search->clear = tall_add(tall, search->list, tall_tag("touch_search_clear"),
			(short)(TOUCH_SEARCH_X + TOUCH_SEARCH_WIDTH + TOUCH_GAP), TOUCH_SEARCH_Y, 106, 0, 0, 0);
		tall_add(tall, search->clear, tall_tag("touch_search_cross"),
			(short)(TOUCH_SEARCH_X + TOUCH_SEARCH_WIDTH + TOUCH_GAP + icon_y), (short)(TOUCH_SEARCH_Y + icon_y), 106, 0,
			0, 0);
		search->field_width_base = TOUCH_SEARCH_WIDTH;
		search->field_grow = TOUCH_SEARCH_GROW;
	}
	tall_target(tall, search->icon, _target_search_icon, index, NULL);
	tall_target(tall, search->field, _target_search_field, index, NULL);
	tall_target(tall, search->clear, _target_search_clear, index, NULL);
	if (search->field)
		search->field->visible = FALSE;
	if (search->clear)
		search->clear->visible = FALSE;
}

static void tall_lanes_frame(struct screen_tall *tall);

/* the table's rows' foot: over its chat, if it has one */
static short tall_rows_bottom(struct screen_tall const *tall)
{
	short bottom = ui_widget_port_touch_menus() ? TOUCH_ROWS_BOTTOM : TALL_ROWS_BOTTOM;

	return (short)(tall->chat ? bottom - tall->chat_height - TALL_CHAT_GAP : bottom);
}

/* a table's lanes: their top (under the lineup, if the screen has one),
each one's width */
static short tall_lanes_base(struct screen_tall const *tall)
{
	(void)tall;
	return ui_widget_port_touch_menus() ? TOUCH_CHIPS_Y : TALL_TITLES_Y;
}

static short tall_lanes_top(struct screen_tall const *tall)
{
	return (short)(tall_lanes_base(tall) + (tall->lineup_height ? tall->lineup_height + TALL_LINEUP_GAP : 0));
}

static short tall_lane_width(short table_width, short lanes)
{
	return (short)((table_width - (lanes - 1) * TALL_LANE_GAP) / lanes);
}

/* a table's lanes (its <lane>s, in order): each its panel (a team's, by
its tone), its title at its top's left and its detail at its right; and
the panel across them all, shown while a lane's label has no value */
static void tall_build_lanes(struct screen_tall *tall, struct menu_screen const *file, long table_index)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short table_width = touch ? TOUCH_TABLE_WIDTH : TALL_TABLE_WIDTH;
	short lanes = tall->lanes, lane_width = tall_lane_width(table_width, lanes);
	short top = tall_lanes_top(tall), bottom = tall_rows_bottom(tall);
	short grow = (short)(TALL_TABLE_GROW / lanes);
	long index, lane = 0;

	tall->lane_span = tall_add(tall, tall->owner, tall_tag("lane"), TALL_TABLE_X, top, -106, TALL_TABLE_GROW,
		table_width, (short)(bottom - top));
	tall_box_picture(tall, tall->lane_span, "main_menu/parts/panel_%dx%d", table_width, (short)(bottom - top));
	visible_set_safe(tall->lane_span, FALSE);
	for (index = 0; index < file->block_count && lane < lanes; index++)
	{
		struct menu_screen_block const *block = &file->blocks[index];

		if (block->kind == MENU_SCREEN_LANE && block->parent == table_index)
			tall->lane_blocks[lane++] = block;
	}
	for (lane = 0; lane < lanes; lane++)
	{
		short x = (short)(TALL_TABLE_X + lane * (lane_width + TALL_LANE_GAP)), move = (short)(-106 + lane * grow);

		tall->lane_panels[lane] = tall_add(tall, tall->owner, tall_tag("lane"), x, top, move, grow, lane_width,
			(short)(bottom - top));
		tall_box_picture(tall, tall->lane_panels[lane], "main_menu/parts/team_panel_%dx%d", lane_width,
			(short)(bottom - top));
		tall->lane_titles[lane] = tall_add(tall, tall->owner, tall_tag("lane_title"), (short)(x + TALL_LANE_TITLE_X),
			(short)(top + TALL_LANE_TITLE_Y), move, 0, 0, 0);
		tall->lane_details[lane] = tall_add(tall, tall->owner, tall_tag("lane_detail"),
			(short)(x + lane_width - TALL_LANE_TITLE_X - TALL_LANE_DETAIL_WIDTH), (short)(top + TALL_LANE_DETAIL_Y),
			(short)(move + grow), 0, TALL_LANE_DETAIL_WIDTH, 0);
	}
}

/* the table: its columns' titles (the computer's), the line under them, its
rows (as many as fit over the panel's foot), each its cells, its words for
no rows and its button */
static void tall_build_table(struct screen_tall *tall, struct menu_screen const *file, long table_index)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	long index, column_index;
	short x = TALL_CELLS_X, move = 0;
	boolean first = TRUE;
	short table_width = touch ? TOUCH_TABLE_WIDTH : TALL_TABLE_WIDTH;

	tall->table = &file->blocks[table_index];
	/* (a table of lanes': its columns from its rows' left (in from its
	rounded end), with no titles) */
	if (tall->table->lanes > 1)
		x = TALL_LANE_ROW_INSET;
	for (index = 0; index < NUMBEROF(screen_source_names); index++)
	{
		if (tall->table->source && !strcmp(tall->table->source, screen_source_names[index]))
			tall->source = screen_sources[index];
	}
	/* (the columns' places: one after another, each moved by the growth of
	those before it) */
	for (index = 0; index < file->block_count && tall->column_count < TALL_MAXIMUM_COLUMNS; index++)
	{
		struct menu_screen_block const *block = &file->blocks[index];
		struct screen_column *column;
		short words;

		if (block->kind != MENU_SCREEN_COLUMN || block->parent != table_index || !menu_screen_when(block->when))
			continue;
		column = &tall->columns[tall->column_count++];
		memset(column, 0, sizeof(*column));
		if (!first)
			x = (short)(x + block->gap);
		first = FALSE;
		column->block = block;
		column->search = NONE;
		column->x = x;
		column->width = block->width;
		column->move = move;
		column->grow = block->grow;
		words = (short)(tall_small_width(block->label) + tall_small_width(block->label) % 2);
		if (block->right)
		{
			column->title_width = (short)MAX(block->width, words + TALL_TITLE_RIGHT_ROOM);
			column->title_x = (short)(x + block->width - column->title_width);
		}
		else
		{
			column->title_width = words;
			column->title_x = x;
		}
		x = (short)(x + block->width);
		move = (short)(move + block->grow);
	}
	/* (the titles, a searched column's with room for its glass) */
	if (!touch && tall->table->lanes < 2)
	{
		tall->titles = tall_add(tall, tall->body, tall_tag("titles"), 0, TALL_TITLES_Y, 0, 0, 0, 0);
		for (column_index = 0; column_index < tall->column_count; column_index++)
		{
			struct screen_column *column = &tall->columns[column_index];
			struct menu_screen_block const *block = column->block;
			long child;
			boolean searched = FALSE;
			char label[MENU_SCREEN_MAXIMUM_TEXT];

			for (child = 0; child < file->block_count; child++)
				searched |= file->blocks[child].kind == MENU_SCREEN_SEARCH && file->blocks[child].parent ==
					column->block - file->blocks && menu_screen_when(file->blocks[child].when);
			if (searched)
				column->title_width = (short)(column->title_width + TALL_TITLE_ROOM);
			if (block->title_width)
			{
				column->title_width = block->title_width;
				if (block->right)
					column->title_x = (short)(column->x + block->width - column->title_width);
			}
			if (!block->label || !block->label[0])
				continue;
			column->title = tall_add(tall, tall->titles, tall_tag(block->right ? "title_right" : "title"),
				(short)(TALL_TABLE_X + column->title_x), TALL_TITLES_Y, (short)(-106 + column->move),
				searched || block->right ? 0 : column->grow, column->title_width, 0);
			tall_text(tall, block->label, label, sizeof(label));
			tall_text_set(column->title, label);
			if (block->sort)
				tall_target(tall, column->title, _target_title, (short)column_index, block->sort);
			column->arrow_up = tall_add(tall, column->title, tall_tag("sort_up"), (short)(TALL_TABLE_X + column->title_x),
				TALL_TITLES_Y, (short)(-106 + column->move), 0, 0, 0);
			column->arrow_down = tall_add(tall, column->title, tall_tag("sort_down"),
				(short)(TALL_TABLE_X + column->title_x), TALL_TITLES_Y, (short)(-106 + column->move), 0, 0, 0);
			for (child = 0; child < file->block_count; child++)
			{
				if (file->blocks[child].kind == MENU_SCREEN_SEARCH && file->blocks[child].parent ==
					column->block - file->blocks)
				{
					tall_build_search(tall, &file->blocks[child], column);
				}
			}
		}
		/* (titles that sort nothing and hold no search: words alone, which
		the focus passes over) */
		if (tall->titles)
		{
			boolean takes = tall->search_count > 0;

			for (column_index = 0; column_index < tall->column_count; column_index++)
				takes |= tall->columns[column_index].title && tall->columns[column_index].block->sort;
			tall->titles->disabled = !takes;
		}
		tall->line = tall_add(tall, tall->owner, tall_tag("table_line"), TALL_TABLE_X, TALL_LINE_Y, -106,
			TALL_TABLE_GROW, TALL_TABLE_WIDTH, 0);
		tall_box_picture(tall, tall->line, "main_menu/parts/line_%d", TALL_TABLE_WIDTH, 0);
	}
	/* (the rows: a table of lanes', each lane's under its title, as many
	as fit over its foot) */
	tall->lanes = (short)MAX(1, MIN(tall->table->lanes, TALL_MAXIMUM_LANES));
	if (tall->lanes > 1)
		tall_build_lanes(tall, file, table_index);
	tall->rows_y = touch ? TOUCH_ROWS_Y : TALL_ROWS_Y;
	tall->row_height = touch ? TOUCH_TAP : TALL_ROW_HEIGHT;
	tall->row_step = touch ? TOUCH_ROW_STEP : TALL_ROW_HEIGHT;
	tall->row_count = (short)MIN(TALL_MAXIMUM_ROWS, (tall_rows_bottom(tall) - tall->rows_y +
		tall->row_step - tall->row_height) / tall->row_step);
	if (tall->lanes > 1)
	{
		tall->rows_y = (short)(tall_lanes_top(tall) + TALL_LANE_ROWS_TOP);
		tall->row_height = TALL_LANE_ROW_HEIGHT;
		tall->row_step = TALL_LANE_ROW_STEP;
		tall->lane_rows = (short)MIN(TALL_MAXIMUM_ROWS / tall->lanes, (tall_rows_bottom(tall) -
			tall->rows_y + tall->row_step - tall->row_height) / tall->row_step);
		tall->row_count = (short)(tall->lane_rows * tall->lanes);
	}
	for (index = 0; index < tall->row_count; index++)
	{
		struct screen_row_widgets *row = &tall->rows[index];
		short lane = (short)(tall->lanes > 1 ? index / tall->lane_rows : 0);
		short y = (short)(tall->rows_y + (tall->lanes > 1 ? index % tall->lane_rows : index) * tall->row_step);
		short row_x = TALL_TABLE_X, row_width = table_width, row_move = -106, row_grow = TALL_TABLE_GROW;
		boolean laned = tall->lanes > 1;

		memset(row, 0, sizeof(*row));
		row->lane = lane;
		if (laned)
		{
			short lane_width = tall_lane_width(table_width, tall->lanes);

			row_x = (short)(TALL_TABLE_X + lane * (lane_width + TALL_LANE_GAP) + TALL_LANE_ROW_INSET);
			row_width = (short)(lane_width - 2 * TALL_LANE_ROW_INSET);
			row_grow = (short)(TALL_TABLE_GROW / tall->lanes);
			row_move = (short)(-106 + lane * row_grow);
		}
		row->row = tall_add(tall, tall->body, tall_tag(laned ? "lane_row" : touch ? "touch_row" : "row"), row_x, y,
			row_move, row_grow, row_width, laned ? tall->row_height : 0);
		if (!row->row)
			break;
		tall_box_picture(tall, row->row, laned ? "main_menu/parts/lane_row_%dx%d" : touch ?
			"main_menu/parts/touch_row_%dx%d" : "main_menu/parts/row_%dx%d", row_width, tall->row_height);
		tall_target(tall, row->row, _target_row, (short)index, tall->table->action);
		row->row->visible = FALSE;
		for (column_index = 0; column_index < tall->column_count; column_index++)
		{
			struct screen_column const *column = &tall->columns[column_index];
			struct menu_screen_block const *block = column->block;
			short cx = (short)(TALL_TABLE_X + column->x), cy = y, cw = column->width;
			short cmove = (short)(-106 + column->move), cgrow = column->grow;
			short icon_y = TALL_CELL_ICON_Y;
			boolean large = FALSE;

			if (laned)
			{
				/* (in its lane's row, from the row's left: its columns' moves
				and growth the lane's; on a touchscreen, its touch place's x,
				width, move and growth, where it has one) */
				cx = (short)(row_x + column->x);
				cy = (short)(y + TALL_LANE_CELL_Y);
				cmove = (short)(row_move + column->move);
				icon_y = (short)(-TALL_LANE_CELL_Y + (tall->row_height - TALL_LANE_DOT) / 2);
				if (touch && block->touch_width)
				{
					cx = (short)(row_x + block->touch_x);
					cw = block->touch_width;
					cmove = (short)(row_move + block->touch_move);
					cgrow = block->touch_grow;
				}
			}
			else if (touch)
			{
				if (!block->touch_width)
					continue;
				cx = (short)(TALL_TABLE_X + block->touch_x);
				cy = (short)(y + block->touch_y);
				cw = block->touch_width;
				cmove = (short)(-106 + block->touch_move);
				cgrow = block->touch_grow;
				icon_y = 0;
				large = block->touch_y < 20 && block->cell != MENU_SCREEN_CELL_STAR;
			}
			switch (block->cell)
			{
			case MENU_SCREEN_CELL_STAR:
				row->picture[column_index] = tall_add(tall, row->row, tall_tag("cell_star"), cx, (short)(cy + icon_y),
					cmove, 0, 0, 0);
				break;
			case MENU_SCREEN_CELL_DOT:
				row->picture[column_index] = tall_add(tall, row->row, tall_tag("cell_dot"), cx, (short)(cy + icon_y),
					cmove, 0, 0, 0);
				break;
			case MENU_SCREEN_CELL_SWATCH:
				row->picture[column_index] = tall_add(tall, row->row, tall_tag("cell_swatch"), cx,
					(short)(cy + icon_y), cmove, 0, 0, 0);
				break;
			case MENU_SCREEN_CELL_PING:
				row->picture[column_index] = tall_add(tall, row->row, tall_tag("cell_bars"), cx,
					(short)(cy + icon_y - (touch && !laned ? 1 : 0)), cmove, 0, 0, 0);
				row->text[column_index] = tall_add(tall, row->row, tall_tag(touch && !laned ? "touch_cell_right" :
					"cell_right"), cx, cy, cmove, cgrow, cw, 0);
				break;
			default:
				row->text[column_index] = tall_add(tall, row->row, tall_tag(touch && !laned ? (large ? (block->right ?
					"touch_cell_large_right" : "touch_cell_large") : (block->right ? "touch_cell_right" : "touch_cell")) :
					block->right ? "cell_right" : "cell"), cx, cy, cmove, cgrow, cw, 0);
				row->badge[column_index] = tall_add(tall, row->row, tall_tag("cell_badge"), cx,
					(short)(cy + (touch && !laned ? 2 : icon_y)), cmove, 0, 0, 0);
				row->lock[column_index] = tall_add(tall, row->row, tall_tag("cell_lock"), cx,
					(short)(cy + (touch && !laned ? 2 : icon_y)), cmove, 0, 0, 0);
				break;
			}
		}
		/* (a heading's words, across the row from the first column of
		words; a table of lanes has none) */
		if (laned)
			continue;
		{
			short from = 0, to = table_width;

			for (column_index = 0; column_index < tall->column_count; column_index++)
			{
				if (tall->columns[column_index].block->cell != MENU_SCREEN_CELL_STAR)
				{
					from = touch ? tall->columns[column_index].block->touch_x : tall->columns[column_index].x;
					break;
				}
			}
			if (tall->column_count && !touch)
				to = (short)(tall->columns[tall->column_count - 1].x + tall->columns[tall->column_count - 1].width);
			row->heading = touch ?
				tall_add(tall, row->row, tall_tag("touch_cell_large"), (short)(TALL_TABLE_X + from),
					(short)(y + TOUCH_ROW_HEADING_Y), -106, TALL_TABLE_GROW, (short)(table_width - 2 * from), 0) :
				tall_add(tall, row->row, tall_tag("cell"), (short)(TALL_TABLE_X + from), y, -106, TALL_TABLE_GROW,
					(short)(to - from), 0);
			if (row->heading)
				row->heading->visible = FALSE;
		}
	}
	/* (its words for none, its button under them) */
	tall->empty = tall_add(tall, tall->owner, tall_tag("table_empty"), TALL_TABLE_X + TALL_EMPTY_INSET,
		touch ? TOUCH_EMPTY_Y : TALL_EMPTY_Y, -106, TALL_TABLE_GROW, (short)(table_width - 2 * TALL_EMPTY_INSET),
		TALL_EMPTY_HEIGHT);
	if (tall->empty)
		tall->empty->visible = FALSE;
}

/* the table's button (under its words for none: CLEAR SEARCH), in a bar of
its own in the body */
static void tall_build_table_button(struct screen_tall *tall, struct menu_screen const *file, long table_index)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short table_width = touch ? TOUCH_TABLE_WIDTH : TALL_TABLE_WIDTH;
	long index;

	for (index = 0; index < file->block_count; index++)
	{
		struct menu_screen_block const *button = &file->blocks[index];
		short width = touch ? TOUCH_TABLE_BUTTON_WIDTH : TALL_TABLE_BUTTON_WIDTH;
		short y = touch ? TOUCH_TABLE_BUTTON_Y : TALL_TABLE_BUTTON_Y;
		short x = (short)(TALL_TABLE_X + (table_width - width) / 2);
		char label[MENU_SCREEN_MAXIMUM_TEXT];
		char text[MENU_SCREEN_MAXIMUM_TEXT];

		if (button->kind != MENU_SCREEN_BUTTON || button->parent != table_index || !menu_screen_when(button->when))
			continue;
		tall->table_bar = tall_add(tall, tall->body, tall_tag("bar"), 0, y, 0, 0, 0, touch ? TOUCH_TAP : 0);
		tall->table_button.widget = tall_add(tall, tall->table_bar, tall_tag(touch ? "touch_button_do" : "table_button"),
			x, y, (short)(-106 + TALL_TABLE_GROW / 2), 0, width, 0);
		tall->table_button.block = button;
		snprintf(tall->table_button.key, sizeof(tall->table_button.key), "%s", button->key);
		tall_box_picture(tall, tall->table_button.widget, touch ? "main_menu/parts/touch_button_%dx%d" :
			"main_menu/parts/item_%dx%d", width, touch ? TOUCH_TAP : TALL_PROMPT_HEIGHT);
		tall_text(tall, button->label, label, sizeof(label));
		snprintf(text, sizeof(text), touch ? "%s%s" : "%s %s", touch ? "" : tall_key_picture(button->key), label);
		tall_text_set(tall->table_button.widget, text);
		if (button->action)
			tall_target(tall, tall->table_button.widget, _target_button, NONE, button->action);
		if (tall->table_bar)
		{
			tall->table_bar->visible = FALSE;
			tall->table_bar->focused_child = tall->table_button.widget;
		}
		break;
	}
}

/* a panel: its picture beside the table, its lines from its top, its
buttons at its foot. A screen's panels are in one place, each its own
lines, the first whose if has a value shown (tall_panel_frame) */
static void tall_build_panel(struct screen_tall *tall, struct screen_open *open, struct menu_screen const *file,
	long panel_index)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short left = touch ? TOUCH_PANEL_X : TALL_PANEL_X;
	short top = touch ? TOUCH_ROWS_Y : TALL_PANEL_Y;
	short width = touch ? TOUCH_PANEL_WIDTH : TALL_PANEL_WIDTH;
	short height = touch ? TOUCH_PANEL_HEIGHT : TALL_PANEL_HEIGHT;
	short inner_x, inner_width = (short)(width - 2 * TALL_PANEL_INSET);
	short y, previous = NONE;
	short button_height = touch ? TOUCH_TAP : TALL_PROMPT_HEIGHT, buttons = 0;
	struct screen_panel *made_panel;
	struct widget_instance *panel;
	long index;

	if (tall->panel_count >= TALL_MAXIMUM_PANELS)
		return;
	made_panel = &tall->panels[tall->panel_count];
	memset(made_panel, 0, sizeof(*made_panel));
	/* (beside a table of lanes on a touchscreen, from the lanes' top, which
	have no chips over them) */
	if (touch && tall->lanes > 1)
	{
		height = (short)(height + top - tall_lanes_base(tall));
		top = tall_lanes_base(tall);
	}
	inner_x = (short)(left + TALL_PANEL_INSET);
	y = top;
	for (index = 0; index < file->block_count; index++)
		buttons += file->blocks[index].kind == MENU_SCREEN_BUTTON && file->blocks[index].parent == panel_index &&
			menu_screen_when(file->blocks[index].when);
	buttons = (short)MIN(buttons, TALL_MAXIMUM_PANEL_BUTTONS);
	made_panel->block = &file->blocks[panel_index];
	panel = tall_add(tall, tall->owner, tall_tag("panel"), left, top, TALL_PANEL_MOVE, TALL_PANEL_GROW, width,
		height);
	tall_box_picture(tall, panel, "main_menu/parts/panel_%dx%d", width, height);
	made_panel->widget = panel;
	visible_set_safe(panel, FALSE);
	if (!tall->panel_widget)
		tall->panel_widget = panel;
	tall->panel_count++;
	for (index = 0; panel && index < file->block_count; index++)
	{
		struct menu_screen_block const *block = &file->blocks[index];
		struct screen_line *line;
		short gap = 0;

		if (block->parent != panel_index || !menu_screen_when(block->when))
			continue;
		if (block->kind == MENU_SCREEN_BUTTON)
		{
			/* (its buttons one over another up from its foot, the last at
			the foot: the panel's action, filled; those over it outlined) */
			long made = made_panel->button_count;
			boolean last = made == buttons - 1;
			short button_y = (short)(top + height - TALL_PANEL_BUTTON_FOOT - (buttons - made) * button_height -
				(buttons - made - 1) * PANEL_BUTTON_GAP);
			struct screen_prompt *button = &made_panel->buttons[made];
			char label[MENU_SCREEN_MAXIMUM_TEXT];
			char text[MENU_SCREEN_MAXIMUM_TEXT];

			if (made >= buttons)
				continue;
			made_panel->bars[made] = tall_add(tall, tall->body, tall_tag("bar"), 0, button_y, 0, 0, 0, button_height);
			button->widget = tall_add(tall, made_panel->bars[made], tall_tag(touch ? "touch_button_large" : last ?
				"panel_button" : "panel_button_outline"), inner_x, button_y, TALL_PANEL_MOVE, TALL_PANEL_GROW,
				inner_width, 0);
			tall_box_picture(tall, button->widget, touch ? "main_menu/parts/touch_button_%dx%d" : last ?
				"main_menu/parts/button_%dx%d" : "main_menu/parts/outline_%dx%d", inner_width, button_height);
			button->block = block;
			snprintf(button->key, sizeof(button->key), "%s", block->key);
			tall_text(tall, block->label, label, sizeof(label));
			/* (an outlined one its words alone) */
			snprintf(text, sizeof(text), touch || !last ? "%s%s" : "%s %s", touch || !last ? "" :
				tall_key_picture(block->key), label);
			tall_text_set(button->widget, text);
			if (block->action)
				tall_target(tall, button->widget, _target_button, NONE, block->action);
			if (made_panel->bars[made])
			{
				made_panel->bars[made]->visible = FALSE;
				made_panel->bars[made]->focused_child = button->widget;
			}
			made_panel->button_count++;
			continue;
		}
		if (tall->line_count >= TALL_MAXIMUM_LINES)
			break;
		/* (a choice: a row of its own in the body (the focus steps to it),
		its label at its left, its value between its arrows) */
		if (block->kind == MENU_SCREEN_CHOICE)
		{
			short choice_height = touch ? TOUCH_PANEL_CHOICE_HEIGHT : PANEL_CHOICE_HEIGHT;
			short value_x = (short)(inner_x + PANEL_CHOICE_VALUE_X), value_width = (short)(inner_width - PANEL_CHOICE_VALUE_X);
			char label[MENU_SCREEN_MAXIMUM_TEXT];

			y = (short)(y + (previous == NONE ? (touch ? TOUCH_PANEL_FIRST_GAP : PANEL_FIRST_GAP) : PANEL_CHOICE_GAP));
			line = &tall->lines[tall->line_count++];
			memset(line, 0, sizeof(*line));
			line->block = block;
			line->panel = (short)(tall->panel_count - 1);
			line->spartan = NONE;
			line->y = y;
			line->room = choice_height;
			line->width = inner_width;
			line->grow = TALL_PANEL_GROW;
			line->widget = tall_add(tall, tall->body, tall_tag("choice"), inner_x, y, TALL_PANEL_MOVE, TALL_PANEL_GROW,
				inner_width, choice_height);
			tall_box_picture(tall, line->widget, "main_menu/parts/item_%dx%d", inner_width, choice_height);
			line->label = tall_add(tall, line->widget, tall_tag("choice_label"), (short)(inner_x + PANEL_CHOICE_LABEL_X),
				(short)(y + (choice_height - 16) / 2), TALL_PANEL_MOVE, 0, PANEL_CHOICE_LABEL_WIDTH, 0);
			tall_text(tall, block->label, label, sizeof(label));
			tall_text_set(line->label, label);
			line->arrows[0] = tall_add(tall, line->widget, tall_tag("choice_left"), value_x,
				(short)(y + (choice_height - 12) / 2), TALL_PANEL_MOVE, 0, 0, 0);
			line->value = tall_add(tall, line->widget, tall_tag("choice_value"), value_x, (short)(y + (choice_height - 16) / 2),
				TALL_PANEL_MOVE, TALL_PANEL_GROW, value_width, 0);
			line->arrows[1] = tall_add(tall, line->widget, tall_tag("choice_right"),
				(short)(value_x + value_width - PANEL_CHOICE_ARROW - 4), (short)(y + (choice_height - 12) / 2),
				(short)(TALL_PANEL_MOVE + TALL_PANEL_GROW), 0, 0, 0);
			visible_set_safe(line->widget, FALSE);
			if (line->widget)
				line->widget->disabled = TRUE;
			tall_target(tall, line->widget, _target_choice, (short)(tall->line_count - 1), block->right_action);
			y = (short)(y + choice_height);
			previous = MENU_SCREEN_CHOICE;
			continue;
		}
		/* (the Spartan: a line as tall as it, the original picture in its
		place, centred each frame (tall_panel_frame); a picture: a line as tall
		as its height at most, as wide as its shape, centred too) */
		if (block->kind == MENU_SCREEN_SPARTAN || block->kind == MENU_SCREEN_PICTURE)
		{
			boolean spartan = block->kind == MENU_SCREEN_SPARTAN;
			short line_height = (short)(touch ? block->touch_box_height : block->box_height);
			short line_width = (short)(line_height * (spartan ? SCREEN_SPARTAN_PICTURE_WIDTH : PANEL_PICTURE_WIDTH) /
				(spartan ? SCREEN_SPARTAN_PICTURE_HEIGHT : PANEL_PICTURE_HEIGHT));

			y = (short)(y + (previous == NONE ? (touch ? TOUCH_PANEL_FIRST_GAP : PANEL_FIRST_GAP) :
				spartan ? PANEL_SPARTAN_GAP : 0));
			line = &tall->lines[tall->line_count++];
			memset(line, 0, sizeof(*line));
			line->block = block;
			line->panel = (short)(tall->panel_count - 1);
			line->spartan = NONE;
			line->y = y;
			line->most = line_height;
			line->width = line_width;
			line->widget = tall_add(tall, tall->owner, spartan ? screens.spartan_tag : tall_tag("picture"),
				(short)(left + (width - line_width) / 2), y, TALL_PANEL_MOVE, 0, line_width, line_height);
			if (spartan)
			{
				/* (its feet drawn about a 25th of its height under its line's
				foot: the line after it that much lower) */
				line->room = (short)(line_height + line_height / PANEL_SPARTAN_FEET + PANEL_SPARTAN_GAP);
				if (screen_spartan_add(open, block, line->widget))
					line->spartan = open->spartan_count - 1;
			}
			else
			{
				struct screen_box *box = tall_box_of(tall, line->widget);

				line->room = (short)(line_height + PANEL_PICTURE_GAP);
				if (box)
					box->picture = tag_loaded(BITMAP_GROUP_TAG, !strcmp(block->picture, "missions") ?
						"ui\\shell\\bitmaps\\sp_levels" : "ui\\shell\\bitmaps\\mp_map_grafix");
			}
			visible_set_safe(line->widget, FALSE);
			y = (short)(y + line->room);
			previous = (short)block->kind;
			continue;
		}
		/* (the gap before it, by what is before it) */
		if (previous == NONE)
			gap = touch ? TOUCH_PANEL_FIRST_GAP : PANEL_FIRST_GAP;
		else if (block->kind == MENU_SCREEN_PILLS)
			gap = previous == MENU_SCREEN_PANEL_TEXT ? (touch ? TOUCH_PANEL_PILLS_GAP : PANEL_PILLS_GAP) : 0;
		else if (block->kind == MENU_SCREEN_PAIR)
			gap = previous == MENU_SCREEN_PILLS ? PANEL_PAIR_GAP : 0;
		else if (previous == MENU_SCREEN_PAIR)
			gap = PANEL_AFTER_PAIR_GAP;
		y = (short)(y + gap);
		if (block->kind == MENU_SCREEN_PILLS)
		{
			long pill_index;
			short x = 0, row = 0;
			boolean row_first = TRUE;

			for (pill_index = 0; pill_index < file->block_count && tall->line_count < TALL_MAXIMUM_LINES; pill_index++)
			{
				struct menu_screen_block const *pill = &file->blocks[pill_index];

				if (pill->kind != MENU_SCREEN_PILL || pill->parent != index)
					continue;
				if (x && x + pill->width > inner_width)
				{
					x = 0;
					y = (short)(y + PANEL_PILL_STEP);
					row++;
					row_first = TRUE;
				}
				line = &tall->lines[tall->line_count++];
				memset(line, 0, sizeof(*line));
				line->block = pill;
				/* (a row's room its first pill's: gone with a row whose pills
				all have no words) */
				line->most = row;
				line->room = (short)(row_first ? PANEL_PILL_STEP : 0);
				row_first = FALSE;
				line->panel = (short)(tall->panel_count - 1);
				line->spartan = NONE;
				line->y = y;
				line->widget = tall_add(tall, tall->owner, tall_tag("pill"), (short)(inner_x + x), y, TALL_PANEL_MOVE, 0,
					pill->width, 0);
				tall_box_picture(tall, line->widget, "main_menu/parts/pill_%d", pill->width, 0);
				visible_set_safe(line->widget, FALSE);
				x = (short)(x + pill->width + TALL_PILL_GAP);
			}
			y = (short)(y + PANEL_PILL_STEP);
			previous = MENU_SCREEN_PILLS;
			continue;
		}
		line = &tall->lines[tall->line_count++];
		memset(line, 0, sizeof(*line));
		line->block = block;
		line->panel = (short)(tall->panel_count - 1);
		line->spartan = NONE;
		line->width = inner_width;
		line->grow = TALL_PANEL_GROW;
		line->y = y;
		if (block->kind == MENU_SCREEN_PAIR)
		{
			line->widget = tall_add(tall, tall->owner, tall_tag("pair_value"), inner_x, y, TALL_PANEL_MOVE,
				TALL_PANEL_GROW, inner_width, 0);
			line->label = tall_add(tall, tall->owner, tall_tag("pair_label"), inner_x, (short)(y + PANEL_PAIR_LABEL_Y),
				TALL_PANEL_MOVE, 0, PANEL_PAIR_LABEL_WIDTH, 0);
			if (line->label)
			{
				char label[MENU_SCREEN_MAXIMUM_TEXT];

				tall_text(tall, block->label, label, sizeof(label));
				tall_text_set(line->label, label);
			}
			line->room = PANEL_PAIR_STEP;
			y = (short)(y + PANEL_PAIR_STEP);
		}
		else
		{
			boolean heading = block->kind == MENU_SCREEN_PANEL_HEADING;
			short lines = touch ? block->touch_lines : block->lines;
			short line_height = heading ? (touch ? TOUCH_PANEL_HEADING_LINE : PANEL_HEADING_LINE) :
				touch ? (lines > 2 ? TOUCH_PANEL_LONG_LINE : TOUCH_PANEL_TEXT_LINE) :
				lines > 2 ? PANEL_LONG_LINE : PANEL_TEXT_LINE;

			line->widget = tall_add(tall, tall->owner, tall_tag(heading ? "panel_heading" : "panel_text"), inner_x, y,
				TALL_PANEL_MOVE, TALL_PANEL_GROW, inner_width, (short)(lines * line_height));
			line->room = (short)(lines * line_height);
			y = (short)(y + lines * line_height);
		}
		visible_set_safe(line->widget, FALSE);
		visible_set_safe(line->label, FALSE);
		previous = (short)block->kind;
	}
	made_panel->lines_bottom = y;
}

/* a computer's prompt's width: as wide as its words, or its width */
static short tall_prompt_width(struct screen_tall *tall, struct menu_screen_block const *button)
{
	char label[MENU_SCREEN_MAXIMUM_TEXT];

	tall_text(tall, button->label, label, sizeof(label));
	return (short)MAX(button->width,
		TALL_PROMPT_TEXT_X + tall_prompt_end(button->key, label, FALSE) + TALL_PROMPT_RIGHT_ROOM);
}

/* whether a computer's prompt is at the bar's right end (side="right") */
static boolean tall_prompt_right(struct menu_screen_block const *button)
{
	return button->side && !strcmp(button->side, "right") && !ui_widget_port_touch_menus();
}

/* the bar's prompts laid out, those shown alone (shown: a bit of each
prompt's): the computer's one after another from the screen's margin, those
of side="right" (after the rest) ending at its right margin, those of one
key in one place, so that a prompt hidden by its if leaves no gap (the
lobby's CHANGE TEAM with the other actions at the right, whether INVITE
FRIENDS is there or not); a touchscreen's in a row from the left, BACK at
the right. On a wide screen each at the edge it is nearest, or laid out again
from the edges, each as wide as the controller's pictures need */
static void tall_bar_lay_out(struct screen_tall *tall, unsigned long shown)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short x = touch ? TOUCH_MARGIN : TALL_PROMPT_MARGIN, place;
	short slot_x[TALL_MAXIMUM_PROMPTS], widths[TALL_MAXIMUM_PROMPTS], grown[TALL_MAXIMUM_PROMPTS];
	struct screen_prompt *prompts[TALL_MAXIMUM_PROMPTS];
	boolean on_left[TALL_MAXIMUM_PROMPTS];
	short right_width = 0;
	char const *previous_key = NULL;
	long index, count = 0;
	boolean any_grown = FALSE, right_begun = FALSE;

	tall->bar_shown = shown;
	/* (the right end's room: its prompts', and the gaps between them) */
	for (index = 0; index < tall->prompt_count; index++)
	{
		struct screen_prompt const *prompt = &tall->prompts[index];

		if (!(shown & (1UL << index)))
			continue;
		if (tall_prompt_right(prompt->block) && !(previous_key && !strcmp(previous_key, prompt->key)))
			right_width = (short)(right_width + (right_width ? TALL_PROMPT_GAP : 0) + prompt->width);
		previous_key = prompt->key;
	}
	previous_key = NULL;
	for (index = 0; index < tall->prompt_count; index++)
	{
		struct screen_prompt *prompt = &tall->prompts[index];
		struct screen_box *box = tall_box_of(tall, prompt->widget);
		boolean shared;

		if (!(shown & (1UL << index)))
			continue;
		/* (a button of the key before's shares its place: shown one at a
		time, by their ifs) */
		shared = previous_key && !strcmp(previous_key, prompt->key);
		/* (the right end's first: from where they end at the right margin) */
		if (tall_prompt_right(prompt->block) && !right_begun)
		{
			right_begun = TRUE;
			x = (short)MAX(x, 640 - TALL_PROMPT_MARGIN - right_width);
			shared = FALSE;
		}
		place = shared && count ? slot_x[count - 1] : x;
		if (touch && !strcmp(prompt->key, "back"))
			place = (short)(640 - TOUCH_MARGIN - prompt->width);
		prompts[count] = prompt;
		slot_x[count] = place;
		widths[count] = prompt->width;
		grown[count] = prompt->grown;
		any_grown |= prompt->grown > prompt->width;
		if (box)
		{
			box->x = place;
			box->dw = 0;
		}
		if (!shared)
			x = (short)(x + prompt->width + (touch ? TOUCH_PROMPT_GAP : TALL_PROMPT_GAP));
		previous_key = prompt->key;
		count++;
	}
	for (index = 0; index < count; index++)
	{
		struct screen_box *box = tall_box_of(tall, prompts[index]->widget);

		/* (side="right"'s at the right edge, wherever their place; on a
		touchscreen, BACK at the right edge, the rest in a row from the
		left) */
		on_left[index] = touch ? strcmp(prompts[index]->key, "back") != 0 :
			slot_x[index] + widths[index] / 2 < 320 && !tall_prompt_right(prompts[index]->block);
		if (!box)
			continue;
		box->centred = TRUE;
		box->dx = (short)(on_left[index] ? -106 : 106);
	}
	if (any_grown)
	{
		short cursor = (short)(slot_x[0] - 106);
		short right_end = 0;

		/* (the left ones from the left edge, the right ones from the right) */
		for (index = 0; index < count; index++)
		{
			short slot_grown = 0;
			long other;

			if (index && slot_x[index] == slot_x[index - 1])
				continue;
			if (!on_left[index])
				continue;
			for (other = index; other < count && slot_x[other] == slot_x[index]; other++)
				slot_grown = (short)MAX(slot_grown, grown[other]);
			for (other = index; other < count && slot_x[other] == slot_x[index]; other++)
			{
				struct screen_box *box = tall_box_of(tall, prompts[other]->widget);

				if (box)
				{
					box->dx = (short)(cursor - slot_x[other]);
					box->dw = (short)(grown[other] - widths[other]);
				}
			}
			cursor = (short)(cursor + slot_grown + TALL_PROMPT_GAP);
		}
		for (index = 0; index < count; index++)
		{
			long other;
			short most = 0;

			if (on_left[index])
				continue;
			for (other = 0; other < count; other++)
			{
				if (slot_x[other] == slot_x[index])
					most = (short)MAX(most, widths[other]);
			}
			right_end = (short)MAX(right_end, slot_x[index] + most);
		}
		cursor = (short)(right_end + 106);
		for (place = (short)(count - 1); place >= 0; place--)
		{
			short slot_grown = 0;
			long other;

			if (on_left[place] || (place && slot_x[place] == slot_x[place - 1]))
				continue;
			for (other = place; other < count && slot_x[other] == slot_x[place]; other++)
				slot_grown = (short)MAX(slot_grown, grown[other]);
			cursor = (short)(cursor - slot_grown);
			for (other = place; other < count && slot_x[other] == slot_x[place]; other++)
			{
				struct screen_box *box = tall_box_of(tall, prompts[other]->widget);

				if (box)
				{
					box->dx = (short)(cursor - slot_x[other]);
					box->dw = (short)(grown[other] - widths[other]);
				}
			}
			cursor = (short)(cursor - TALL_PROMPT_GAP);
		}
	}
}

/* the shown prompts of the bar now (a bit of each), and laid out again as
they change */
static void tall_bar_arrange(struct screen_tall *tall)
{
	unsigned long shown = 0;
	long index;

	for (index = 0; tall->bar && index < tall->prompt_count; index++)
	{
		if (tall->prompts[index].widget && tall_shown(tall, tall->prompts[index].block))
			shown |= 1UL << index;
	}
	if (tall->bar && shown != tall->bar_shown)
		tall_bar_lay_out(tall, shown);
}

/* the bar: its buttons as prompts (the computer's, each as wide as its
words or its width; a touchscreen's, its buttons), laid out by
tall_bar_lay_out */
static void tall_build_bar(struct screen_tall *tall, struct menu_screen const *file, long bar_index)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short y = touch ? TOUCH_BAR_Y : TALL_BAR_Y;
	long index, count = 0, last_right = NONE;

	tall->bar = tall_add(tall, tall->body, tall_tag(touch ? "bar" : "prompts"), 0, y, 0, 0, 0, touch ? TOUCH_TAP : 0);
	if (!tall->bar)
		return;
	for (index = 0; index < file->block_count && count < TALL_MAXIMUM_PROMPTS; index++)
	{
		struct menu_screen_block const *button = &file->blocks[index];
		struct screen_prompt *prompt = &tall->prompts[count];
		char label[MENU_SCREEN_MAXIMUM_TEXT];
		short width;

		if (button->kind != MENU_SCREEN_BUTTON || button->parent != bar_index || !menu_screen_when(button->when))
			continue;
		tall_text(tall, button->label, label, sizeof(label));
		if (touch)
			width = button->touch_width ? button->touch_width : !strcmp(button->key, "back") ? TOUCH_BACK_WIDTH :
				TOUCH_PROMPT_WIDTH;
		else
			width = tall_prompt_width(tall, button);
		prompt->width = width;
		/* (with the controller's pictures, wider on the Steam Deck's 16:10
		and on: port_settings.py's prompts_wide) */
		{
			short controller = (short)(TALL_PROMPT_TEXT_X + tall_prompt_end(button->key, label, TRUE) +
				TALL_PROMPT_RIGHT_ROOM);

			prompt->grown = width;
			if (!touch && controller > width)
			{
				double deck = (768.0 - 640.0) / 212.0;
				long extra = (long)((controller - width) / deck);

				if ((double)extra < (controller - width) / deck)
					extra++;
				prompt->grown = (short)(width + extra);
			}
		}
		prompt->block = button;
		snprintf(prompt->key, sizeof(prompt->key), "%s", button->key);
		prompt->widget = tall_add(tall, tall->bar, tall_tag(touch ? "touch_button" : "prompt_spread"), 0,
			(short)(y + (touch ? 0 : PROMPT_TOP)), 0, 0, width, 0);
		tall_box_picture(tall, prompt->widget, touch ? "main_menu/parts/touch_button_%dx%d" :
			"main_menu/parts/item_%dx%d", width, touch ? TOUCH_TAP : TALL_PROMPT_HEIGHT);
		if (button->action)
			tall_target(tall, prompt->widget, _target_button, NONE, button->action);
		if (tall_prompt_right(button))
			last_right = count;
		count++;
	}
	tall->prompt_count = count;
	/* (the bar's focus on its first, as a list's: A on the bar presses it;
	on the right end's last, the action, where BACK is at the left) */
	if (count)
		tall->bar->focused_child = tall->prompts[last_right != NONE ? last_right : 0].widget;
	/* (laid out with them all, then again by those shown each frame) */
	tall_bar_lay_out(tall, (1UL << count) - 1);
}

/* a chat: its panel across the table's foot, its lines (each its name,
then its words after it), the line typed's place (the focus steps to it: A,
a click or a tap its do), and the layer its on-screen keyboard is drawn on */
static void tall_build_chat(struct screen_tall *tall)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short width = touch ? TOUCH_TABLE_WIDTH : TALL_TABLE_WIDTH;
	short bottom = touch ? TOUCH_ROWS_BOTTOM : TALL_ROWS_BOTTOM;
	short top = (short)(bottom - tall->chat_height), y;
	short line_height = touch ? TOUCH_CHAT_LINE : TALL_CHAT_LINE;
	short input_height = touch ? TOUCH_CHAT_INPUT : TALL_CHAT_INPUT;
	short line;

	tall->chat_panel = tall_add(tall, tall->owner, tall_tag("chat"), TALL_TABLE_X, top, -106, TALL_TABLE_GROW, width,
		tall->chat_height);
	tall_box_picture(tall, tall->chat_panel, "main_menu/parts/panel_%dx%d", width, tall->chat_height);
	y = (short)(top + TALL_CHAT_INSET);
	for (line = 0; line < tall->chat->lines && line < TALL_MAXIMUM_CHAT_LINES; line++)
	{
		tall->chat_names[line] = tall_add(tall, tall->owner, tall_tag("chat_name"), TALL_TABLE_X + 2 * TALL_CHAT_INSET,
			y, -106, 0, 0, line_height);
		tall->chat_texts[line] = tall_add(tall, tall->owner, tall_tag("chat_text"), TALL_TABLE_X + 2 * TALL_CHAT_INSET,
			y, -106, TALL_TABLE_GROW, (short)(width - 4 * TALL_CHAT_INSET), line_height);
		visible_set_safe(tall->chat_names[line], FALSE);
		visible_set_safe(tall->chat_texts[line], FALSE);
		y = (short)(y + line_height);
	}
	tall->chat_input = tall_add(tall, tall->body, tall_tag(touch ? "chat_input_touch" : "chat_input"),
		TALL_TABLE_X + TALL_CHAT_INSET, (short)(bottom - TALL_CHAT_INSET - input_height), -106, TALL_TABLE_GROW,
		(short)(width - 2 * TALL_CHAT_INSET), input_height);
	tall_box_picture(tall, tall->chat_input, "main_menu/parts/item_%dx%d", (short)(width - 2 * TALL_CHAT_INSET),
		input_height);
	tall_target(tall, tall->chat_input, _target_chat, NONE, tall->chat->action);
}

/* the screen made: the tall layout's blocks in their places */
static boolean tall_build(struct screen_open *open, struct menu_screen const *screen)
{
	struct widget_instance *widget = open->widget;
	struct screen_tall *tall = NULL;
	long index, table_index = NONE, open_index = open - screens.open;

	for (index = 0; index < NUMBEROF(tall_storage) && !tall; index++)
	{
		long other;
		boolean used = FALSE;

		for (other = 0; other < SCREEN_MAXIMUM_OPEN; other++)
			used |= other != open_index && tall_screens[other] == &tall_storage[index];
		if (!used)
			tall = &tall_storage[index];
	}
	if (!tall)
		return FALSE;
	/* (a search typed into ends with the screen it was on) */
	if (pc_menu_text_field_editing(NULL))
		pc_menu_text_field_end(TRUE);
	memset(tall, 0, sizeof(*tall));
	tall->values.local_player_index = widget->local_player_index;
	tall->search_editing = NONE;
	tall_screens[open_index] = tall;
	widget->focused_child = NULL;
	while (widget->child)
		ui_widget_delete(widget->child);
	open->list = NULL;
	open->item_count = 0;
	open->prompt_count = 0;
	open->header = NULL;
	open->spartan_count = 0;
	tall->panel_shown = NONE;
	tall->lineup_spartan = NONE;
	/* (the tall layout's background, its band's and footer's lines, where
	the screen it takes the place of has none of its own (Select Profile's,
	whose generated screen's was its inner screen's); or in place of that
	screen's own where the file asks for it (Profile Color's, whose
	generated screen's is the PC version's gradient, its band's line lower:
	drawn over it, both lines would show) */
	if (screen->tall_background)
		menu_tags_background_set(widget->definition_tag_index, tall_tag("background"));
	if (menu_tags_background(widget->definition_tag_index) == NONE)
		tall_add(tall, widget, tall_tag("background"), 0, 0, 0, 0, 0, 0);
	/* (the parts that take no focus are the screen's, the rest the body's,
	in the order the focus steps through them) */
	tall->owner = tall_add(tall, widget, tall_tag("layer"), 0, 0, 0, 0, 0, 0);
	tall->body = tall_add(tall, widget, tall_tag("body"), 0, 0, 0, 0, 0, 0);
	if (!tall->owner || !tall->body)
		return FALSE;
	/* (its chat first found: the table ends over it) */
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *block = &screen->blocks[index];
		long source;

		if (block->kind != MENU_SCREEN_CHAT || !menu_screen_when(block->when))
			continue;
		for (source = 0; source < NUMBEROF(screen_chat_names); source++)
		{
			if (block->source && !strcmp(block->source, screen_chat_names[source]))
				tall->chat_source = screen_chats[source];
		}
		tall->chat = block;
		tall->chat_height = (short)(2 * TALL_CHAT_INSET + MIN(block->lines, TALL_MAXIMUM_CHAT_LINES) *
			(ui_widget_port_touch_menus() ? TOUCH_CHAT_LINE : TALL_CHAT_LINE) +
			(ui_widget_port_touch_menus() ? TOUCH_CHAT_INPUT : TALL_CHAT_INPUT) + 2);
	}
	/* (its own <spartan> first found, over a table of lanes: the lanes begin
	under it) */
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *block = &screen->blocks[index];

		if (block->kind == MENU_SCREEN_SPARTAN && block->parent < 0 && menu_screen_when(block->when))
		{
			tall->lineup_height = (short)MIN(ui_widget_port_touch_menus() ? block->touch_box_height : block->box_height,
				TALL_LINEUP_MOST);
		}
		if (block->kind == MENU_SCREEN_TABLE && block->lanes < 2)
			tall->lineup_height = 0;
	}
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *block = &screen->blocks[index];

		if (block->parent >= 0)
			continue;
		switch (block->kind)
		{
		case MENU_SCREEN_HEADER:
		{
			char label[MENU_SCREEN_MAXIMUM_TEXT];

			open->header = screen_widget_add(widget, screens.header_text_tag, 0, 0);
			open->header_picture = NONE;
			tall_text(tall, block->label, label, sizeof(label));
			tall_text_set(open->header, label);
			tall_build_header(tall, widget, screen, index);
			break;
		}
		case MENU_SCREEN_TABS:
			if (menu_screen_when(block->when))
				tall_build_tabs(tall, widget, screen, index);
			break;
		case MENU_SCREEN_SEARCH:
			tall_build_search(tall, block, NULL);
			break;
		case MENU_SCREEN_TABLE:
			table_index = index;
			tall_build_table(tall, screen, index);
			break;
		case MENU_SCREEN_PANEL:
			tall_build_panel(tall, open, screen, index);
			break;
		case MENU_SCREEN_SPARTAN:
		{
			/* (the lineup: as wide as the table, its Spartans drawn where its
			widget is, under the body) */
			boolean touch = ui_widget_port_touch_menus() != 0;
			short table_width = touch ? TOUCH_TABLE_WIDTH : TALL_TABLE_WIDTH;

			if (!tall->lineup_height || !menu_screen_when(block->when))
				break;
			tall->lineup = tall_add(tall, tall->owner, screens.spartan_tag, TALL_TABLE_X, tall_lanes_base(tall), -106,
				TALL_TABLE_GROW, table_width, tall->lineup_height);
			visible_set_safe(tall->lineup, FALSE);
			if (tall->lineup && screen_spartan_add(open, block, tall->lineup))
				tall->lineup_spartan = open->spartan_count - 1;
			/* (its places' names, over it: placed each frame under their
			Spartans) */
			if (tall->lineup_spartan != NONE && block->names)
			{
				short place;

				tall->lineup_names_binding = block->names;
				for (place = 0; place < MAXIMUM_MENU_SPARTAN_PLACES; place++)
				{
					tall->lineup_names[place] = tall_add(tall, tall->owner, tall_tag("lineup_name"), TALL_TABLE_X,
						tall_lanes_base(tall), 0, 0, 0, TALL_LINEUP_NAME);
					visible_set_safe(tall->lineup_names[place], FALSE);
				}
			}
			break;
		}
		}
	}
	/* (the chat's line typed after the panel's, the table's button, the bar
last) */
	if (tall->chat)
		tall_build_chat(tall);
	if (table_index != NONE)
		tall_build_table_button(tall, screen, table_index);
	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].kind == MENU_SCREEN_BUTTONS)
			tall_build_bar(tall, screen, index);
	}
	/* (the chat's on-screen keyboard's layer, over everything) */
	if (tall->chat)
		tall->chat_keyboard = tall_add(tall, widget, tall_tag("layer"), 0, 0, 0, 0, 0, 0);
	/* (the focus: the tabs, else the first row; the rows' source then
	moves it) */
	widget->focused_child = tall->body;
	tall->body->focused_child = tall->strip ? tall->strip : tall->rows[0].row;
	if (tall->strip && tall->tab_count)
		tall->strip->focused_child = tall->tab_widgets[0];
	if (tall->titles)
		tall->titles->focused_child = tall->titles->child;
	for (index = 0; index < tall->search_count; index++)
	{
		if (tall->searches[index].list && !tall->searches[index].list->focused_child)
			tall->searches[index].list->focused_child = tall->searches[index].icon;
	}
	snprintf(open->back, sizeof(open->back), "%s", screen->back ? screen->back : "");
	snprintf(open->start, sizeof(open->start), "%s", screen->start ? screen->start : "");
	open->generation = menu_screens_generation(screen->name);
	return TRUE;
}

/* ---------- each frame */

/* a list's focus on its child (its index chosen too) */
static void tall_focus(struct widget_instance *list, struct widget_instance *child)
{
	struct widget_instance *each;
	short index = 0;

	for (each = list ? list->child : NULL; each; each = each->next, index++)
	{
		if (each == child)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
			return;
		}
	}
}

/* a bar's focus off a button hidden: on its first one shown */
static void tall_focus_off_hidden(struct widget_instance *bar)
{
	struct widget_instance *child;

	if (!bar || !bar->focused_child || bar->focused_child->visible)
		return;
	for (child = bar->child; child; child = child->next)
	{
		if (child->visible)
		{
			tall_focus(bar, child);
			return;
		}
	}
}

/* the boxes moved and grown for the layout now */
static void tall_place(struct screen_tall *tall, struct widget_instance *screen)
{
	long index;

	for (index = 0; index < tall->box_count; index++)
	{
		struct screen_box *box = &tall->boxes[index];
		struct screen_box const *parent = box->parent != NONE ? &tall->boxes[box->parent] : NULL;

		box->at_x = (short)(box->x + tall_wide(box->dx, box->dw != 0, box->centred));
		box->at_y = box->y;
		box->widget->horizontal_offset = (short)(box->at_x - (parent ? parent->at_x : 0));
		box->widget->vertical_offset = (short)(box->at_y - (parent ? parent->at_y : 0));
	}
}

/* a box's width now (its growth too) */
static short tall_width(struct screen_tall *tall, struct widget_instance *widget)
{
	struct screen_box *box = tall_box_of(tall, widget);
	short width;

	if (!box)
		return 0;
	width = box->width;
	if (!width)
		ui_widget_port_text_box_font(widget, &width);
	return (short)(width + tall_wide(box->dw, TRUE, box->centred));
}

static void tall_color(struct screen_tall *tall, struct widget_instance *widget, unsigned long color)
{
	struct screen_box *box = tall_box_of(tall, widget);

	if (box)
		box->color = color;
}

static void tall_utf16(char const *utf8, unsigned short *text, long size)
{
	halo_menus_utf16(utf8, text, size);
}

/* the tabs: the current one framed and white, the one with the focus
outlined (a tab chosen by a key takes the strip's focus) */
static void tall_tabs_frame(struct screen_tall *tall)
{
	char current[MENU_SCREEN_MAXIMUM_TEXT];
	short shown = NONE;
	boolean strip_focused;
	long index;

	if (!tall->strip)
		return;
	tall->strip->visible = tall_shown(tall, tall->tabs);
	visible_set_safe(tall->tab_keys[0], tall->strip->visible);
	visible_set_safe(tall->tab_keys[1], tall->strip->visible);
	tall_binding(tall, tall->tabs->value, current, sizeof(current));
	for (index = 0; index < tall->tab_count; index++)
	{
		if (tall->tab_blocks[index]->id && !strcmp(tall->tab_blocks[index]->id, current))
			shown = (short)index;
	}
	if (shown != NONE && shown != tall->tab_shown)
		tall_focus(tall->strip, tall->tab_widgets[shown]);
	tall->tab_shown = shown;
	strip_focused = tall->body->focused_child == tall->strip;
	for (index = 0; index < tall->tab_count; index++)
	{
		struct widget_instance *tab = tall->tab_widgets[index];

		tab->animation.current_frame_index = (short)((index == shown ? 2 : 0) +
			(strip_focused && tab == tall->strip->focused_child ? 1 : 0));
		tall_color(tall, tab, index == shown ? 0xFFFFFFFFUL : 0);
	}
}

/* the columns' titles: the one sorted by white, its arrow after its words
(past the end of a title whose words end at its right), its way */
static void tall_titles_frame(struct screen_tall *tall)
{
	char sorted[MENU_SCREEN_MAXIMUM_TEXT];
	long index;

	tall_binding(tall, tall->table->value, sorted, sizeof(sorted));
	for (index = 0; index < tall->column_count; index++)
	{
		struct screen_column *column = &tall->columns[index];
		size_t length = column->block->id ? strlen(column->block->id) : 0;
		boolean current = length && !strncmp(sorted, column->block->id, length) &&
			(sorted[length] == ' ' || !sorted[length]);
		boolean down = current && strstr(sorted + length, "down") != NULL;
		short x;

		if (!column->title)
			continue;
		tall_color(tall, column->title, current ? 0xFFFFFFFFUL : 0);
		visible_set_safe(column->arrow_up, current && !down && column->title->visible);
		visible_set_safe(column->arrow_down, current && down && column->title->visible);
		if (column->block->right)
			x = (short)(column->title_width - TALL_ARROW_GAP);
		else
			x = pc_menu_text_width(column->title, column->title->parameters.text_box.text ?
				(unsigned short const *)column->title->parameters.text_box.text : NULL);
		tall_box_of(tall, column->arrow_up)->x = (short)(TALL_TABLE_X + column->title_x + x);
		tall_box_of(tall, column->arrow_down)->x = (short)(TALL_TABLE_X + column->title_x + x);
	}
}

/* the searches: each its glass (or bar), or its field and X while it has
words or is typed into (the words typed so far its value's, at once; the
field left, its words kept, once the d-pad or a click, not the pointer
passing, took the focus off it) */
static struct screen_tall *tall_search_screen;

static void tall_search_done(char const *text)
{
	struct screen_tall *tall = tall_search_screen;

	if (tall && tall->search_editing != NONE)
		screen_value_set(tall->searches[tall->search_editing].block->value, text);
	if (tall)
		tall->search_editing = NONE;
}

static boolean tall_search_editing(struct screen_tall *tall)
{
	if (tall->search_editing != NONE && !pc_menu_text_field_editing(tall->searches[tall->search_editing].field))
		tall->search_editing = NONE;
	return tall->search_editing != NONE;
}

static void tall_search_end(struct screen_tall *tall)
{
	if (tall_search_editing(tall))
		pc_menu_text_field_end(TRUE);
	tall->search_editing = NONE;
}

static void tall_search_begin(struct screen_tall *tall, short index)
{
	struct screen_search *search = &tall->searches[index];
	char words[MENU_SCREEN_MAXIMUM_TEXT];

	if (!tall_shown(tall, search->block))
		return;
	tall_search_end(tall);
	if (pc_menu_text_field_editing(NULL))
		pc_menu_text_field_end(FALSE);
	search->field->visible = TRUE;
	tall_focus(search->list, search->field);
	tall_focus(tall->body, search->list);
	tall_search_screen = tall;
	tall->search_editing = index;
	tall_binding(tall, search->block->value, words, sizeof(words));
	pc_menu_text_field_begin(search->field, words, 31, tall_search_done);
	ui_play_audio_feedback_sound(TALL_SOUND_FORWARD);
}

/* a search's words cleared (left if typed into), its glass with the focus
again if its field had it */
static void tall_search_clear(struct screen_tall *tall, short index)
{
	struct screen_search *search = &tall->searches[index];

	if (tall_search_editing(tall) && tall->search_editing == index)
	{
		pc_menu_text_field_end(FALSE);
		tall->search_editing = NONE;
	}
	screen_value_set(search->block->value, "");
	if (search->list && (search->list->focused_child == search->field || search->list->focused_child == search->clear))
	{
		search->icon->visible = TRUE;
		tall_focus(search->list, search->icon);
	}
}

static void tall_searches_frame(struct screen_tall *tall)
{
	long index;

	if (tall_search_editing(tall))
	{
		struct screen_search *search = &tall->searches[tall->search_editing];

		if (!ui_widget_port_hover_focused() &&
			(tall->body->focused_child != search->list || search->list->focused_child != search->field))
		{
			tall_search_end(tall);
		}
	}
	for (index = 0; index < tall->search_count; index++)
	{
		struct screen_search *search = &tall->searches[index];
		char words[MENU_SCREEN_MAXIMUM_TEXT];
		boolean editing = tall->search_editing == index;
		boolean shown;

		boolean usable = tall_shown(tall, search->block);

		tall_binding(tall, search->block->value, words, sizeof(words));
		shown = usable && (editing || words[0]);
		visible_set_safe(search->title, !shown);
		visible_set_safe(search->icon, usable && !shown);
		if (search->list && search->list != tall->titles)
			search->list->visible = usable;
		/* (a touchscreen's bar, lit while the focus is on it) */
		if (search->icon && search->icon->animation.number_of_sprite_frames > 2)
			search->icon->animation.current_frame_index = (short)(tall->body->focused_child == search->list &&
				search->list->focused_child == search->icon);
		visible_set_safe(search->field, shown);
		visible_set_safe(search->clear, shown);
		if (!search->field || !shown)
			continue;
		pc_menu_text_field_show(search->field, words, editing);
		/* (the words typed so far: its value's, filtering as each is typed) */
		if (editing && tall_search_editing(tall))
			screen_value_set(search->block->value, pc_menu_text_field_words());
		/* (words longer than the field: their end shown) */
		{
			wchar_t *text = search->field->parameters.text_box.text;
			short width = (short)(search->field_width_base + tall_wide(search->field_grow, TRUE, FALSE) - 8);

			while (text && text[0] && text[1] && pc_menu_text_width(search->field, (unsigned short const *)text) > width)
				{
				long length = 0;

				while (text[length])
					length++;
				memmove(text, text + 1, length * sizeof(wchar_t));
			}
			search->field->animation.current_frame_index = (short)editing;
		}
	}
	tall_focus_off_hidden(tall->titles);
	for (index = 0; index < tall->search_count; index++)
		tall_focus_off_hidden(tall->searches[index].list);
}

/* the rows: the source's, from its first, each its cells or a heading;
each row's picture normal, focused or chosen */
static void tall_rows_frame(struct screen_tall *tall, struct widget_instance *screen)
{
	struct menu_table_view *view = &tall->view;
	boolean touch = ui_widget_port_touch_menus() != 0;
	long index, column_index;
	short focused = NONE;

	for (index = 0; index < tall->row_count; index++)
	{
		if (tall->rows[index].row && tall->body->focused_child == tall->rows[index].row)
			focused = (short)index;
	}
	memset(view, 0, sizeof(*view));
	view->screen = screen;
	view->shown = tall->row_count;
	view->focused = focused;
	view->hover = ui_widget_port_hover_focused();
	view->titles = tall->titles && tall->titles->visible;
	view->lanes = tall->lanes;
	view->chosen = NONE;
	view->focus = NONE;
	if (tall->source)
		tall->source->update(view);
	if (view->focus != NONE && view->focus >= 0 && view->focus < tall->row_count && tall->rows[view->focus].row)
	{
		tall->rows[view->focus].row->visible = TRUE;
		tall_focus(tall->body, tall->rows[view->focus].row);
	}
	if (view->focus_titles && tall->titles && tall->titles->visible)
		tall_focus(tall->body, tall->titles);
	if (tall->lanes > 1)
		tall_lanes_frame(tall);
	for (index = 0; index < tall->row_count; index++)
	{
		struct screen_row_widgets *row = &tall->rows[index];
		short row_index = (short)(view->first + index);
		unsigned short words[MENU_TABLE_TEXT_LENGTH];
		boolean heading;
		boolean row_focused = tall->body->focused_child == row->row;
		int state = MENU_TABLE_ROW_PLAIN;

		if (!row->row)
			continue;
		/* (a table of lanes': every place its own row, shown while the
		source has a row there; a row marked framed, the brighter with the
		focus) */
		if (tall->lanes > 1)
			row_index = (short)index;
		if (tall->source && tall->source->state && row_index < view->count)
			state = tall->source->state(row_index);
		row->row->visible = row_index < view->count && state != MENU_TABLE_ROW_NONE;
		row->row->disabled = !row->row->visible;
		if (!row->row->visible)
			continue;
		heading = tall->lanes < 2 && tall->source && tall->source->heading(row_index, words, MENU_TABLE_TEXT_LENGTH);
		visible_set_safe(row->heading, heading);
		row->row->animation.current_frame_index = (short)(heading ? 0 : row_index == view->chosen ? 2 :
			row_focused ? 1 : 0);
		if (tall->lanes > 1)
			row->row->animation.current_frame_index = (short)((state == MENU_TABLE_ROW_MARKED ? 2 : 0) + row_focused);
		if (heading)
		{
			pc_menu_text_fit(row->heading, words, tall_width(tall, row->heading));
			/* (dimmer than the rows under it, a section's label, not a row) */
			tall_color(tall, row->heading, TALL_HEADING_COLOR);
		}
		for (column_index = 0; column_index < tall->column_count; column_index++)
		{
			struct screen_column const *column = &tall->columns[column_index];
			struct menu_table_cell cell;
			short width, drawn, room = 0, x;
			struct screen_box *box;

			if (heading)
			{
				visible_set_safe(row->text[column_index], FALSE);
				visible_set_safe(row->badge[column_index], FALSE);
				visible_set_safe(row->lock[column_index], FALSE);
				visible_set_safe(row->picture[column_index], FALSE);
				continue;
			}
			memset(&cell, 0, sizeof(cell));
			cell.frame = NONE;
			cell.speaker = NONE;
			if (tall->source)
				tall->source->cell(row_index, column->block->id, row_focused, &cell);
			if (row->picture[column_index])
			{
				row->picture[column_index]->visible = cell.frame != NONE;
				row->picture[column_index]->animation.current_frame_index = (short)MAX(cell.frame, 0);
			}
			if (!row->text[column_index])
				continue;
			row->text[column_index]->visible = TRUE;
			width = tall_width(tall, row->text[column_index]);
			/* (room after the words for its pill and its lock) */
			if (row->badge[column_index] && cell.badge[0])
				room = (short)(TALL_BADGE_WIDTH + TALL_BADGE_GAP);
			if (cell.lock)
				room = (short)(room + TALL_LOCK_WIDTH + TALL_BADGE_GAP);
			/* (its shorter words where its own would be cut short) */
			if (cell.fallback[0] && pc_menu_text_width(row->text[column_index], cell.text) > width - room)
				csmemcpy(cell.text, cell.fallback, sizeof(cell.text));
			drawn = pc_menu_text_fit(row->text[column_index], cell.text, (short)(width - room));
			if (cell.speaker != NONE)
				pc_menu_speaker_note(row->text[column_index], cell.speaker);
			tall_color(tall, row->text[column_index], cell.color ? cell.color :
				column->block->cell == MENU_SCREEN_CELL_STRONG ? 0xFFFFFFFFUL : 0);
			box = tall_box_of(tall, row->text[column_index]);
			x = (short)(box->x + drawn + TALL_BADGE_GAP);
			if (row->badge[column_index])
			{
				char badge[MENU_TABLE_BADGE_LENGTH * 3];
				long letter;

				for (letter = 0; letter < MENU_TABLE_BADGE_LENGTH - 1 && cell.badge[letter]; letter++)
					badge[letter] = (char)cell.badge[letter];
				badge[letter] = 0;
				row->badge[column_index]->visible = room && cell.badge[0];
				tall_text_set(row->badge[column_index], badge);
				tall_box_of(tall, row->badge[column_index])->x = x;
				row->badge[column_index]->animation.current_frame_index = 0;
				if (cell.badge[0])
					x = (short)(x + TALL_BADGE_WIDTH + TALL_BADGE_GAP);
			}
			if (row->lock[column_index])
			{
				row->lock[column_index]->visible = cell.lock != 0;
				tall_box_of(tall, row->lock[column_index])->x = (short)(x - 2);
				row->lock[column_index]->animation.current_frame_index = 1;
			}
			/* (a ping's bars before its number, if both fit) */
			if (column->block->cell == MENU_SCREEN_CELL_PING && row->picture[column_index])
				row->picture[column_index]->visible = cell.frame != NONE &&
					drawn + TALL_BARS_GAP + TALL_BARS_WIDTH <= width;
		}
		(void)touch;
	}
	/* (the words for no rows) */
	if (tall->empty)
	{
		char text[MENU_SCREEN_MAXIMUM_TEXT * 2];
		unsigned short wide_text[MENU_SCREEN_MAXIMUM_TEXT * 2];

		tall->empty->visible = view->count == 0;
		if (!view->count)
		{
			tall_text(tall, tall->table->empty, text, sizeof(text));
			tall_utf16(text, wide_text, NUMBEROF(wide_text));
			pc_menu_text_wrap(tall->empty, wide_text, tall_width(tall, tall->empty), 3);
		}
	}
	/* (its button: shown as its if says, the focus on it as it comes, as
	there is no row, but for the titles' while a search is typed into) */
	if (tall->table_bar)
	{
		boolean shown = tall_shown(tall, tall->table_button.block);
		struct widget_instance *bar = tall->table_bar;

		if (shown && !bar->visible && tall->body->focused_child != tall->titles &&
			!(tall->search_count && tall->body->focused_child == tall->searches[0].list))
			tall_focus(tall->body, bar);
		bar->visible = shown;
		bar->disabled = !shown;
		if (!shown && tall->body->focused_child == bar)
			tall->body->focused_child = NULL;
		/* (its words again, a binding's that may change: the co-op list's
		fix, which follows why it is empty) */
		if (shown && tall->table_button.widget && tall->table_button.block->label &&
			strchr(tall->table_button.block->label, '{'))
		{
			char label[MENU_SCREEN_MAXIMUM_TEXT];
			char text[MENU_SCREEN_MAXIMUM_TEXT];
			boolean touch = ui_widget_port_touch_menus() != 0;

			tall_text(tall, tall->table_button.block->label, label, sizeof(label));
			snprintf(text, sizeof(text), touch ? "%s%s" : "%s %s", touch ? "" :
				tall_key_picture(tall->table_button.block->key), label);
			tall_text_set(tall->table_button.widget, text);
		}
		if (bar->child)
			bar->child->animation.current_frame_index = (short)(tall->body->focused_child == bar);
	}
}

/* a table's lanes: each its title (its label's words, in its tone's
colour) and its detail; a lane whose label has no value gone, its panel's
place the lane's before it's (the panel across them all for one lane) */
static void tall_lanes_frame(struct screen_tall *tall)
{
	short lane, shown = 0;

	for (lane = 0; lane < tall->lanes; lane++)
	{
		struct menu_screen_block const *block = tall->lane_blocks[lane];
		char text[MENU_SCREEN_MAXIMUM_TEXT];
		char tone_text[16];
		boolean titled = block && tall_text(tall, block->label, text, sizeof(text));
		short tone = (short)lane;

		if (block && block->tone)
			tone = (short)atoi(block->tone[0] >= '0' && block->tone[0] <= '9' ? block->tone :
				tall_binding(tall, block->tone, tone_text, sizeof(tone_text)));
		shown += titled;
		visible_set_safe(tall->lane_titles[lane], titled);
		visible_set_safe(tall->lane_panels[lane], titled && tone < 2);
		if (tall->lane_panels[lane])
			tall->lane_panels[lane]->animation.current_frame_index = (short)MIN(MAX(tone, 0), 1);
		if (titled)
		{
			tall_text_set(tall->lane_titles[lane], text);
			tall_color(tall, tall->lane_titles[lane], tone == 0 ? TALL_LANE_RED : tone == 1 ? TALL_LANE_BLUE : 0);
		}
		if (tall->lane_details[lane])
		{
			boolean detailed = block && block->empty && tall_text(tall, block->empty, text, sizeof(text)) && text[0];

			tall->lane_details[lane]->visible = detailed;
			if (detailed)
				tall_text_set(tall->lane_details[lane], text);
		}
	}
	/* (one lane titled, or a lane of no team's: the panel across them) */
	{
		boolean span = shown < tall->lanes;

		for (lane = 0; lane < tall->lanes && !span; lane++)
			span = tall->lane_panels[lane] && !tall->lane_panels[lane]->visible;
		visible_set_safe(tall->lane_span, span);
		for (lane = 0; lane < tall->lanes && span; lane++)
			visible_set_safe(tall->lane_panels[lane], FALSE);
	}
}

/* a panel's choice: moved up by the lines gone before it, its value's
words, dimmed (and passed over by the focus) while its dim has a value */
static void tall_choice_frame(struct screen_tall *tall, struct screen_line *line, short lift)
{
	struct menu_screen_block const *block = line->block;
	struct widget_instance *parts[4];
	char text[MENU_SCREEN_MAXIMUM_TEXT * 2];
	char dim[MENU_SCREEN_MAXIMUM_TEXT];
	boolean dimmed = block->dim && tall_binding(tall, block->dim, dim, sizeof(dim))[0];
	short index;

	parts[0] = line->label;
	parts[1] = line->value;
	parts[2] = line->arrows[0];
	parts[3] = line->arrows[1];
	{
		struct screen_box *box = tall_box_of(tall, line->widget);
		short moved = (short)(line->y - lift - (box ? box->y : 0));

		if (box)
			box->y = (short)(box->y + moved);
		for (index = 0; index < 4; index++)
		{
			struct screen_box *part = tall_box_of(tall, parts[index]);

			if (part)
				part->y = (short)(part->y + moved);
		}
	}
	line->widget->visible = TRUE;
	visible_set_safe(line->label, TRUE);
	line->widget->disabled = dimmed;
	line->widget->alpha_modifier = dimmed ? 0.4f : 1.0f;
	for (index = 0; index < 4; index++)
	{
		if (parts[index])
			parts[index]->alpha_modifier = dimmed ? 0.4f : 1.0f;
	}
	if (line->value && tall_text(tall, block->hints[0], text, sizeof(text)))
	{
		unsigned short wide_text[MENU_SCREEN_MAXIMUM_TEXT * 2];

		tall_utf16(text, wide_text, NUMBEROF(wide_text));
		pc_menu_text_fit(line->value, wide_text, (short)(tall_width(tall, line->value) - 2 * (PANEL_CHOICE_ARROW + 2)));
	}
	if (dimmed && tall->body->focused_child == line->widget)
		tall->body->focused_child = NULL;
}

/* the chat: its newest lines (as many as it has places), each its name in
its colour and its words after it; the line typed in its place with the
caret (or the source's prompt), lit while it has the focus */
static void tall_chat_frame(struct screen_tall *tall)
{
	struct menu_chat_line lines[TALL_MAXIMUM_CHAT_LINES];
	short shown = tall->chat ? (short)MIN(tall->chat->lines, TALL_MAXIMUM_CHAT_LINES) : 0;
	short count = 0, line, first;
	boolean visible = tall->chat && tall_shown(tall, tall->chat);
	int keyboard = FALSE;
	char const *typed = NULL;

	if (!tall->chat)
		return;
	visible_set_safe(tall->chat_panel, visible);
	visible_set_safe(tall->chat_input, visible);
	if (tall->chat_input)
		tall->chat_input->disabled = !visible;
	if (visible && tall->chat_source)
	{
		count = tall->chat_source->lines(lines, TALL_MAXIMUM_CHAT_LINES);
		typed = tall->chat_source->typed(&keyboard);
	}
	first = (short)MAX(0, count - shown);
	for (line = 0; line < shown; line++)
	{
		struct menu_chat_line const *said = first + line < count ? &lines[first + line] : NULL;
		struct widget_instance *name = tall->chat_names[line], *words = tall->chat_texts[line];
		short name_width = 0;

		visible_set_safe(name, visible && said && said->name[0]);
		visible_set_safe(words, visible && said);
		if (!said || !words)
			continue;
		if (name && said->name[0])
		{
			unsigned short spoken[MENU_TABLE_BADGE_LENGTH * 2 + 2];
			short letter;

			for (letter = 0; said->name[letter] && letter < MENU_TABLE_BADGE_LENGTH * 2; letter++)
				spoken[letter] = said->name[letter];
			spoken[letter++] = ':';
			spoken[letter] = 0;
			name_width = pc_menu_text_fit(name, spoken, 0);
			tall_color(tall, name, said->color);
		}
		{
			struct screen_box *box = tall_box_of(tall, words), *name_box = tall_box_of(tall, name);

			if (box && name_box)
				box->x = (short)(name_box->x + (name_width ? name_width + TALL_CHAT_NAME_GAP : 0));
			pc_menu_text_fit(words, said->text, (short)(tall_width(tall, words) - (name_width ? name_width +
				TALL_CHAT_NAME_GAP : 0)));
			if (!said->name[0])
				tall_color(tall, words, said->color);
			else
				tall_color(tall, words, 0);
		}
	}
	if (tall->chat_input && visible)
	{
		boolean focused = tall->body->focused_child == tall->chat_input;
		char text[MENU_SCREEN_MAXIMUM_TEXT];

		if (typed)
			pc_menu_text_field_show(tall->chat_input, typed, TRUE);
		else
		{
			char const *prompt = tall->chat_source && tall->chat_source->prompt ? tall->chat_source->prompt() : "";

			snprintf(text, sizeof(text), "%s", prompt ? prompt : "");
			tall_text_set(tall->chat_input, text);
		}
		tall->chat_input->animation.current_frame_index = (short)(focused || typed);
	}
}

/* whether a panel's line is shown now: its if (a picture's frame too) has
a value */
static boolean tall_line_shown(struct screen_tall *tall, struct screen_line const *line)
{
	char value[MENU_SCREEN_MAXIMUM_TEXT];

	if (line->block->condition && !tall_shown(tall, line->block))
		return FALSE;
	/* (a pill: while a pill of its row has words) */
	if (line->block->kind == MENU_SCREEN_PILL)
	{
		long index;

		for (index = 0; index < tall->line_count; index++)
		{
			struct screen_line const *other = &tall->lines[index];

			if (other->panel == line->panel && other->block->kind == MENU_SCREEN_PILL &&
				other->block->parent == line->block->parent && other->most == line->most &&
				tall_text(tall, other->block->label, value, sizeof(value)) && value[0])
			{
				return TRUE;
			}
		}
		return FALSE;
	}
	return line->block->kind != MENU_SCREEN_PICTURE || tall_binding(tall, line->block->value, value, sizeof(value))[0];
}

/* the first of a panel's (a choice, else a button) the focus can take,
NULL if none */
static struct widget_instance *tall_panel_first(struct screen_tall *tall, short panel)
{
	long index;

	for (index = 0; index < tall->line_count; index++)
	{
		struct screen_line const *line = &tall->lines[index];

		if (line->panel == panel && line->block->kind == MENU_SCREEN_CHOICE && line->widget && line->widget->visible &&
			!line->widget->disabled)
		{
			return line->widget;
		}
	}
	for (index = 0; index < tall->panels[panel].button_count; index++)
	{
		if (tall->panels[panel].bars[index] && tall->panels[panel].bars[index]->visible)
			return tall->panels[panel].bars[index];
	}
	return NULL;
}

/* the room a panel's texts leave now (each line's spare): their lines
they do not fill, where the panel has a picture to take it (the lobby's
next game: a host's with bots, whose panel is full, has its map's picture
where its status is a line); else none, the panel as made */
static short tall_panel_spare(struct screen_tall *tall, short shown_panel)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	boolean picture = FALSE;
	short spare = 0;
	long index;

	for (index = 0; index < tall->line_count; index++)
	{
		struct screen_line const *line = &tall->lines[index];

		picture |= line->panel == shown_panel && line->block->kind == MENU_SCREEN_PICTURE &&
			tall_line_shown(tall, line);
	}
	for (index = 0; index < tall->line_count; index++)
	{
		struct screen_line *line = &tall->lines[index];
		struct menu_screen_block const *block = line->block;
		short lines = touch ? block->touch_lines : block->lines;
		char text[MENU_SCREEN_MAXIMUM_TEXT * 2];
		unsigned short wide_text[MENU_SCREEN_MAXIMUM_TEXT * 2];
		short used;

		line->spare = 0;
		if (!picture || line->panel != shown_panel || !line->widget || lines <= 1 ||
			(block->kind != MENU_SCREEN_PANEL_TEXT && block->kind != MENU_SCREEN_PANEL_HEADING) ||
			!tall_line_shown(tall, line) || !tall_text(tall, block->label, text, sizeof(text)) || !text[0])
		{
			continue;
		}
		tall_utf16(text, wide_text, NUMBEROF(wide_text));
		used = pc_menu_text_lines(line->widget, wide_text, tall_width(tall, line->widget), lines);
		if (used > 0 && used < lines)
			line->spare = (short)(line->room / lines * (lines - used));
		spare = (short)(spare + line->spare);
	}
	return spare;
}

/* the panels: the first whose if has a value shown (its picture, lines,
buttons and Spartan), the others' hidden. Its lines: each its binding's
words (a heading's and a text's wrapped to their lines), shown while they
have some; one whose if has none gone, those after it moved up into its
room; a picture as tall as the room the panel's lines and buttons leave
it, at most its height (gone below PANEL_PICTURE_LEAST). As another panel
than the first is shown, the focus goes to its first choice or button, and
back where it was as the first comes again */
static void tall_panel_frame(struct screen_tall *tall, struct screen_open *open)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	short lift = 0, shown_panel = NONE, room = 0;
	long index;

	for (index = 0; index < tall->panel_count && shown_panel == NONE; index++)
	{
		if (!tall->panels[index].block->condition || tall_shown(tall, tall->panels[index].block))
			shown_panel = (short)index;
	}
	for (index = 0; index < tall->panel_count; index++)
		visible_set_safe(tall->panels[index].widget, index == shown_panel);
	if (shown_panel != NONE)
		tall->panel_widget = tall->panels[shown_panel].widget;
	/* (the room the shown panel's lines leave over its buttons shown: its
	pictures' to take) */
	if (shown_panel != NONE)
	{
		struct screen_panel const *panel = &tall->panels[shown_panel];
		short buttons_top = NONE, gone = 0;

		for (index = 0; index < panel->button_count; index++)
		{
			if (panel->bars[index] && tall_shown(tall, panel->buttons[index].block))
			{
				struct screen_box const *bar = tall_box_of(tall, panel->bars[index]);

				buttons_top = (short)(buttons_top == NONE ? bar->y : MIN(buttons_top, bar->y));
			}
		}
		if (buttons_top == NONE)
		{
			struct screen_box const *box = tall_box_of(tall, panel->widget);

			buttons_top = (short)(box->y + box->height - TALL_PANEL_BUTTON_FOOT + PANEL_BUTTON_GAP);
		}
		for (index = 0; index < tall->line_count; index++)
		{
			if (tall->lines[index].panel == shown_panel && !tall_line_shown(tall, &tall->lines[index]))
				gone = (short)(gone + tall->lines[index].room);
		}
		gone = (short)(gone + tall_panel_spare(tall, shown_panel));
		room = (short)(buttons_top - PANEL_BUTTON_GAP - (panel->lines_bottom - gone));
	}
	for (index = 0; index < open->spartan_count; index++)
	{
		if (index != tall->lineup_spartan)
			open->spartans[index].shown = FALSE;
	}
	for (index = 0; index < tall->line_count; index++)
	{
		struct screen_line *line = &tall->lines[index];
		struct menu_screen_block const *block = line->block;
		char text[MENU_SCREEN_MAXIMUM_TEXT * 2];
		unsigned short wide_text[MENU_SCREEN_MAXIMUM_TEXT * 2];
		boolean shown;

		if (!line->widget)
			continue;
		/* (another panel's: hidden) */
		if (line->panel != shown_panel)
		{
			line->widget->visible = FALSE;
			visible_set_safe(line->label, FALSE);
			if (block->kind == MENU_SCREEN_CHOICE)
				line->widget->disabled = TRUE;
			continue;
		}
		/* (a line whose if has no value is gone, the lines after it moved
		up into its room; one whose text has none keeps its room) */
		if (!tall_line_shown(tall, line))
		{
			line->widget->visible = FALSE;
			if (block->kind == MENU_SCREEN_CHOICE)
				line->widget->disabled = TRUE;
			lift = (short)(lift + line->room);
			continue;
		}
		if (block->kind == MENU_SCREEN_CHOICE)
		{
			tall_choice_frame(tall, line, lift);
			continue;
		}
		/* (a picture: as tall as its room, at most its height; a Spartan's
		line: shown, its Spartan placed in the middle of the panel by
		tall_frame) */
		if (block->kind == MENU_SCREEN_PICTURE || block->kind == MENU_SCREEN_SPARTAN)
		{
			struct screen_box *box = tall_box_of(tall, line->widget);
			short height = line->most;

			if (block->kind == MENU_SCREEN_PICTURE)
			{
				char frame[16];

				height = (short)MIN(line->most, line->most + MIN(room, 0));
				if (height < PANEL_PICTURE_LEAST)
				{
					line->widget->visible = FALSE;
					lift = (short)(lift + line->room);
					continue;
				}
				line->widget->animation.current_frame_index =
					(short)atoi(tall_binding(tall, block->value, frame, sizeof(frame)));
				line->widget->visible = TRUE;
			}
			else if (line->spartan != NONE)
				open->spartans[line->spartan].shown = TRUE;
			if (box)
			{
				box->y = (short)(line->y - lift);
				box->height = height;
				box->width = (short)(height * line->width / MAX(line->most, 1));
			}
			lift = (short)(lift + line->most - height);
			continue;
		}
		{
			struct screen_box *box = tall_box_of(tall, line->widget);
			struct screen_box *label = line->label ? tall_box_of(tall, line->label) : NULL;

			if (box)
				box->y = (short)(line->y - lift);
			if (label)
				label->y = (short)(line->y + PANEL_PAIR_LABEL_Y - lift);
		}
		shown = tall_text(tall, block->kind == MENU_SCREEN_PAIR ? block->hints[0] : block->label, text, sizeof(text)) &&
			text[0];
		line->widget->visible = shown;
		if (line->label)
			line->label->visible = shown;
		if (!shown)
			continue;
		tall_utf16(text, wide_text, NUMBEROF(wide_text));
		switch (block->kind)
		{
		case MENU_SCREEN_PANEL_HEADING:
		case MENU_SCREEN_PANEL_TEXT:
			pc_menu_text_wrap(line->widget, wide_text, tall_width(tall, line->widget),
				touch ? block->touch_lines : block->lines);
			lift = (short)(lift + line->spare);
			break;
		case MENU_SCREEN_PAIR:
			pc_menu_text_fit(line->widget, wide_text, (short)(tall_width(tall, line->widget) - PANEL_PAIR_LABEL_WIDTH));
			break;
		case MENU_SCREEN_PILL:
		{
			char tone[16];

			tall_text_set(line->widget, text);
			line->widget->animation.current_frame_index = 0;
			if (block->tone && block->tone[0] >= '0' && block->tone[0] <= '9')
				line->widget->animation.current_frame_index = (short)atoi(block->tone);
			else if (block->tone)
				line->widget->animation.current_frame_index = (short)atoi(tall_binding(tall, block->tone, tone,
					sizeof(tone)));
			break;
		}
		default:
			tall_text_set(line->widget, text);
			break;
		}
	}
	for (index = 0; index < tall->panel_count; index++)
	{
		struct screen_panel *panel = &tall->panels[index];
		long button;

		for (button = 0; button < panel->button_count; button++)
		{
			struct screen_prompt *prompt = &panel->buttons[button];
			boolean shown = index == shown_panel && tall_shown(tall, prompt->block);

			if (!panel->bars[button])
				continue;
			panel->bars[button]->visible = shown;
			panel->bars[button]->disabled = !shown;
			/* (its words again: they may have bindings, START GAME's countdown) */
			if (shown && prompt->widget)
			{
				char label[MENU_SCREEN_MAXIMUM_TEXT];
				char text[MENU_SCREEN_MAXIMUM_TEXT];
				boolean plain = touch || button < panel->button_count - 1;

				tall_text(tall, prompt->block->label, label, sizeof(label));
				snprintf(text, sizeof(text), plain ? "%s%s" : "%s %s", plain ? "" : tall_key_picture(prompt->key), label);
				tall_text_set(prompt->widget, text);
			}
		}
	}
	/* (another panel shown: the focus to it, and back) */
	if (shown_panel != tall->panel_shown)
	{
		struct widget_instance *focused = tall->body->focused_child;

		if (tall->panel_shown != NONE && shown_panel > 0)
		{
			struct widget_instance *first = tall_panel_first(tall, shown_panel);

			if (tall->panel_shown == 0)
				tall->panel_focus_before = focused;
			if (first)
				tall_focus(tall->body, first);
		}
		else if (tall->panel_shown > 0 && shown_panel == 0 && tall->panel_focus_before &&
			tall->panel_focus_before->visible && !tall->panel_focus_before->disabled)
		{
			tall_focus(tall->body, tall->panel_focus_before);
		}
		tall->panel_shown = shown_panel;
	}
}

/* the bar's prompts: shown as their ifs say (their words again, which may
have bindings) */
static void tall_bar_frame(struct screen_tall *tall)
{
	boolean touch = ui_widget_port_touch_menus() != 0;
	long index;

	for (index = 0; index < tall->prompt_count; index++)
	{
		struct screen_prompt *prompt = &tall->prompts[index];
		char label[MENU_SCREEN_MAXIMUM_TEXT];
		char text[MENU_SCREEN_MAXIMUM_TEXT];

		if (!prompt->widget)
			continue;
		prompt->widget->visible = tall_shown(tall, prompt->block);
		tall_text(tall, prompt->block->label, label, sizeof(label));
		snprintf(text, sizeof(text), touch ? "%s%s" : "%s %s", touch ? "" : tall_key_picture(prompt->key), label);
		tall_text_set(prompt->widget, text);
	}
	tall_focus_off_hidden(tall->bar);
	/* (no prompt lit while the focus is not on the bar: a column list lights
	its focused child as it updates, ui_widget.c's column_list_update, which
	it does on the focus's path alone, so the one lit last stayed lit as the
	focus left the bar) */
	if (tall->bar && tall->body->focused_child != tall->bar)
	{
		struct widget_instance *child;

		for (child = tall->bar->child; child; child = child->next)
		{
			if (child->animation.number_of_sprite_frames == 2)
				child->animation.current_frame_index = 0;
		}
	}
	if (tall->header_button.widget)
	{
		char label[MENU_SCREEN_MAXIMUM_TEXT];
		char text[MENU_SCREEN_MAXIMUM_TEXT];
		boolean shown = tall_shown(tall, tall->header_button.block);

		tall->header_button.widget->visible = shown;
		tall->header_bar->visible = shown;
		tall->header_bar->disabled = !shown;
		tall_text(tall, tall->header_button.block->label, label, sizeof(label));
		snprintf(text, sizeof(text), "%s %s", tall_key_picture(tall->header_button.key), label);
		tall_text_set(tall->header_button.widget, text);
		/* (lit while the d-pad, the stick or the pointer has it, as the
		table's button) */
		tall->header_button.widget->animation.current_frame_index = (short)(tall->body->focused_child == tall->header_bar);
		if (!shown && tall->body->focused_child == tall->header_bar)
			tall->body->focused_child = NULL;
	}
}

/* the lineup's places' names: each place's (its names' binding's line)
under its Spartan, centred under its feet (lower or higher as its place
stands: menu_spartan_feet_lower) in the line under the Spartans, as wide as
its place (cut short where it is wider), in its team's colour (red's,
blue's; white without teams); an empty place's ("-", or no name) none, nor
any where no Spartan is drawn */
static void tall_lineup_names_frame(struct screen_tall *tall, struct screen_open *open)
{
	struct screen_spartan const *spartan;
	struct menu_screen_block const *block;
	char colors[MENU_SCREEN_MAXIMUM_TEXT], names[MENU_SCREEN_MAXIMUM_TEXT];
	char const *color = colors, *name = names;
	short place;

	if (tall->lineup_spartan == NONE || !tall->lineup_names_binding)
		return;
	spartan = &open->spartans[tall->lineup_spartan];
	block = &spartan->block;
	tall_binding(tall, block->value, colors, sizeof(colors));
	tall_binding(tall, tall->lineup_names_binding, names, sizeof(names));
	/* (the places as screen_spartan_frame counts them: its colors' words) */
	{
		short places = 0;
		char const *word = colors;

		while (*word && places < spartan->places)
		{
			while (*word == ' ')
				word++;
			if (!*word)
				break;
			while (*word && *word != ' ')
				word++;
			places++;
		}
		for (place = 0; place < MAXIMUM_MENU_SPARTAN_PLACES; place++)
		{
			struct widget_instance *widget = tall->lineup_names[place];
			struct screen_box *box = tall_box_of(tall, widget);
			char line[MENU_SCREEN_MAXIMUM_TEXT];
			unsigned short text[MENU_SCREEN_MAXIMUM_TEXT];
			long length = 0, word_length = 0;
			boolean shown;

			/* (this place's color's word and name's line, and on to the next's) */
			while (*color == ' ')
				color++;
			while (color[word_length] && color[word_length] != ' ')
				word_length++;
			while (name[length] && name[length] != '\n')
				length++;
			length = MIN(length, (long)sizeof(line) - 1);
			memcpy(line, name, (size_t)length);
			line[length] = 0;
			shown = box && place < places && spartan->shown && menu_spartan_available() && line[0] &&
				!(word_length == 1 && color[0] == '-');
			visible_set_safe(widget, shown);
			if (shown)
			{
				real width = (real)block->box_width / (real)places;
				real middle = (real)block->box_x + width * ((real)place + 0.5f);
				real feet = (real)(block->box_y + block->box_height) +
					(real)block->box_height * menu_spartan_feet_lower((short)(spartan->first + place), spartan->places > 1);

				box->width = (short)MAX(width - TALL_LINEUP_NAME_GAP, 8.0f);
				box->at_x = (short)(middle - (real)box->width * 0.5f);
				box->at_y = (short)(feet + 0.5f);
				widget->horizontal_offset = (short)(box->at_x - (box->parent != NONE ? tall->boxes[box->parent].at_x : 0));
				widget->vertical_offset = (short)(box->at_y - (box->parent != NONE ? tall->boxes[box->parent].at_y : 0));
				tall_utf16(line, text, NUMBEROF(text));
				pc_menu_text_fit(widget, text, box->width);
				tall_color(tall, widget, word_length == 3 && !strncmp(color, "red", 3) ? TALL_LANE_RED :
					word_length == 4 && !strncmp(color, "blue", 4) ? TALL_LANE_BLUE : 0);
			}
			color += word_length;
			name += length;
			if (*name == '\n')
				name++;
		}
	}
}

static void tall_frame(struct screen_open *open)
{
	struct screen_tall *tall = tall_of(open);
	struct widget_instance *screen = open->widget;
	struct widget_instance *focused;

	if (!tall)
		return;
	tall_bar_arrange(tall);
	tall_place(tall, screen);
	if (tall->detail)
	{
		char text[MENU_SCREEN_MAXIMUM_TEXT];
		char shorter[MENU_SCREEN_MAXIMUM_TEXT];
		char forms[2 * MENU_SCREEN_MAXIMUM_TEXT];
		unsigned short wide_forms[2 * MENU_SCREEN_MAXIMUM_TEXT];
		short width = tall_width(tall, tall->detail);
		unsigned short *form;

		/* (its words, then its fallback's shorter forms, each on a line of
		its own: the first that fits its place shown, else the last cut
		short with "...") */
		tall_text(tall, tall->header->empty, text, sizeof(text));
		if (tall_text(tall, tall->header->source, shorter, sizeof(shorter)) && shorter[0])
			snprintf(forms, sizeof(forms), "%s\n%s", text, shorter);
		else
			snprintf(forms, sizeof(forms), "%s", text);
		tall_utf16(forms, wide_forms, NUMBEROF(wide_forms));
		form = wide_forms;
		for (;;)
		{
			unsigned short *end = form;

			while (*end && *end != '\n')
				end++;
			if (!*end)
				break;
			*end = 0;
			if (pc_menu_text_width(tall->detail, form) <= width)
				break;
			form = end + 1;
		}
		pc_menu_text_fit(tall->detail, form, width);
	}
	if (open->header)
	{
		char text[MENU_SCREEN_MAXIMUM_TEXT];

		tall_text(tall, tall->header->label, text, sizeof(text));
		tall_text_set(open->header, text);
		open->header->horizontal_offset = (short)(screen->horizontal_offset - ui_widget_port_past_move() / 2);
	}
	tall_tabs_frame(tall);
	tall_searches_frame(tall);
	if (tall->table)
	{
		tall_titles_frame(tall);
		tall_rows_frame(tall, screen);
	}
	tall_panel_frame(tall, open);
	tall_chat_frame(tall);
	tall_bar_frame(tall);
	/* (the focus back to the body once the pointer is not what gave another
	part it: the d-pad steps the body, the band's button among its blocks) */
	if (screen->focused_child != tall->body && !ui_widget_port_pointer_focused())
		screen->focused_child = tall->body;
	/* (the focus off what cannot have it: the first row, else the bar) */
	focused = tall->body->focused_child;
	if (tall->focus_waiting && focused != tall->bar)
		tall->focus_waiting = FALSE;
	if (!focused || !focused->visible || focused->disabled || tall->focus_waiting)
	{
		short row;

		for (row = 0; row < tall->row_count && !(tall->rows[row].row && tall->rows[row].row->visible); row++)
			;
		if (row < tall->row_count && (row == 0 || tall->lanes > 1))
		{
			tall_focus(tall->body, tall->rows[row].row);
			tall->focus_waiting = FALSE;
		}
		else if (tall->bar && focused != tall->bar)
		{
			tall_focus(tall->body, tall->bar);
			tall->focus_waiting = tall->lanes > 1;
		}
	}
	tall_place(tall, screen);
	/* (the panel's Spartan and pictures: in the middle of the panel as wide
	as it is now, at their lines' places; the Spartan in its color now) */
	if (tall->panel_widget)
	{
		struct screen_box *panel = tall_box_of(tall, tall->panel_widget);
		long index;

		for (index = 0; panel && index < tall->line_count; index++)
		{
			struct screen_line const *line = &tall->lines[index];
			struct screen_box *picture = line->widget ? tall_box_of(tall, line->widget) : NULL;

			if (!picture || line->panel != tall->panel_shown || (line->block->kind != MENU_SCREEN_SPARTAN &&
				line->block->kind != MENU_SCREEN_PICTURE))
			{
				continue;
			}
			picture->at_x = (short)(panel->at_x + (tall_width(tall, tall->panel_widget) - picture->width) / 2);
			line->widget->horizontal_offset = (short)(picture->at_x -
				(picture->parent != NONE ? tall->boxes[picture->parent].at_x : 0));
			if (line->spartan != NONE)
			{
				struct menu_screen_block *block = &open->spartans[line->spartan].block;

				block->box_x = picture->at_x;
				block->box_y = picture->at_y;
				block->box_width = picture->width;
				block->box_height = picture->height;
			}
		}
	}
	/* (the lineup: across the table as wide as it is now, its feet at its
	foot) */
	if (tall->lineup_spartan != NONE && tall->lineup)
	{
		struct screen_box *lineup = tall_box_of(tall, tall->lineup);
		struct menu_screen_block *block = &open->spartans[tall->lineup_spartan].block;

		block->box_x = lineup->at_x;
		block->box_y = lineup->at_y;
		block->box_width = tall_width(tall, tall->lineup);
		/* (with names, the Spartans over their line: the lineup's height
		theirs and their names' together) */
		block->box_height = (short)(tall->lineup_height - (tall->lineup_names_binding ? TALL_LINEUP_NAME : 0));
		open->spartans[tall->lineup_spartan].shown = TRUE;
	}
	if (open->spartan_count)
		screen_spartan_frame(open, NONE);
	tall_lineup_names_frame(tall, open);
}

/* the chat's on-screen keyboard, where it is up: over the panel's lower
part (on a touchscreen, across the screen's foot over the bar), its keys'
place noted for taps (the screen's units: menu_spartan.c's) */
long halo_screen_width(void);

static void tall_chat_keyboard_draw(struct screen_tall *tall, point2d offset)
{
	int keyboard = FALSE;
	short screen_width = (short)halo_screen_width();
	short ui_offset = (short)((screen_width - SCREEN_WIDTH) / 2);
	struct screen_box const *panel = tall->panel_widget ? tall_box_of(tall, tall->panel_widget) : NULL;
	short x0, x1, y1, y0;

	if (!tall->chat_source || !tall->chat_source->keyboard || !tall->chat_source->typed(&keyboard) || !keyboard)
		return;
	if (ui_widget_port_touch_menus() || !panel)
	{
		x0 = (short)(SCREEN_WIDTH / 2 - 260);
		x1 = (short)(SCREEN_WIDTH / 2 + 260);
		y1 = (short)(SCREEN_HEIGHT - 8);
	}
	else
	{
		x0 = panel->at_x;
		x1 = (short)(panel->at_x + tall_width(tall, tall->panel_widget));
		y1 = (short)(TALL_PANEL_Y + TALL_PANEL_HEIGHT - 4);
	}
	y0 = (short)(y1 - 5 * TALL_CHAT_KEYBOARD_ROW);
	{
		short font_width;
		long font = tall->chat_input ? ui_widget_port_text_box_font(tall->chat_input, &font_width) : NONE;

		tall->chat_source->keyboard(font, (short)(offset.x + x0), (short)(offset.y + y0), (short)(offset.x + x1),
			(short)(offset.y + y1), screen_width, (short)SCREEN_HEIGHT);
	}
	(void)ui_offset;
}

/* ---------- events */

/* a target's action run: a row's (the table's do, or a cell's of a column
with its own do, clicked), a title's (its sort), a tab's, a button's; a
search's part begun, ended or cleared */
static boolean tall_do(struct screen_open *open, struct widget_instance *widget, struct event_record *event,
	boolean *deleted)
{
	struct screen_tall *tall = tall_of(open);
	long index;

	*deleted = FALSE;
	for (index = 0; tall && index < tall->target_count; index++)
	{
		struct screen_target const *target = &tall->targets[index];
		boolean result;

		if (target->widget != widget)
			continue;
		switch (target->kind)
		{
		case _target_search_icon:
		case _target_search_field:
			/* (typed into, or if it is, left with its words; a touchscreen's
			typed into again, its keyboard back) */
			if (target->kind == _target_search_field && tall_search_editing(tall) &&
				tall->search_editing == target->index && !ui_widget_port_touch_menus())
			{
				tall_search_end(tall);
				return TRUE;
			}
			tall_search_begin(tall, target->index);
			return TRUE;
		case _target_search_clear:
			tall_search_clear(tall, target->index);
			ui_play_audio_feedback_sound(TALL_SOUND_BACK);
			return TRUE;
		case _target_row:
		{
			long column_index;
			short x, y;

			/* (a click on a cell of a column of its own do: that) */
			for (column_index = 0; column_index < tall->column_count; column_index++)
			{
				struct widget_instance *cell = tall->rows[target->index].picture[column_index];

				if (!tall->columns[column_index].block->action || !cell || !cell->visible)
					continue;
				if (ui_widget_port_take_click(&x, &y))
				{
					short left = tall_box_of(tall, cell)->at_x;

					if (x >= left - 4 && x < left + 20)
					{
						screen_action_row = target->index;
						result = screen_action_run(widget, event, tall->columns[column_index].block->action, deleted);
						screen_action_row = NONE;
						return result;
					}
				}
				break;
			}
			if (!target->action)
				return FALSE;
			screen_action_row = target->index;
			result = screen_action_run(widget, event, target->action, deleted);
			screen_action_row = NONE;
			return result;
		}
		case _target_chat:
			return target->action && screen_action_run(widget, event, target->action, deleted);
		case _target_choice:
		{
			/* (A, or a click or a tap on its right half: its right; on its
			left half, its left) */
			struct screen_line const *line = &tall->lines[target->index];
			short x, y;
			char const *action = line->block->right_action;

			if (line->widget->disabled)
				return FALSE;
			if (ui_widget_port_take_click(&x, &y))
			{
				struct screen_box const *box = tall_box_of(tall, line->widget);

				if (box && x < box->at_x + tall_width(tall, line->widget) / 2 + PANEL_CHOICE_VALUE_X / 2)
					action = line->block->left_action;
			}
			result = screen_action_run(widget, event, action, deleted);
			if (result && !*deleted)
				ui_play_audio_feedback_sound(TALL_SOUND_FORWARD);
			return result;
		}
		default:
			return target->action && screen_action_run(widget, event, target->action, deleted);
		}
	}
	return FALSE;
}

/* left or right (step -1 or 1) on a widget of the screen: a choice's left
or right, a row's its table's (its row given), FALSE for what it leaves */
static boolean tall_step(struct screen_open *open, struct widget_instance *widget, struct event_record *event,
	boolean *deleted, short step)
{
	struct screen_tall *tall = tall_of(open);
	long index;

	*deleted = FALSE;
	for (index = 0; tall && index < tall->target_count; index++)
	{
		struct screen_target const *target = &tall->targets[index];
		char const *action = NULL;
		boolean result;

		if (target->widget != widget)
			continue;
		if (target->kind == _target_choice)
		{
			struct screen_line const *line = &tall->lines[target->index];

			if (line->widget->disabled)
				return FALSE;
			action = step < 0 ? line->block->left_action : line->block->right_action;
		}
		else if (target->kind == _target_row)
			action = step < 0 ? tall->table->left_action : tall->table->right_action;
		if (!action)
			return FALSE;
		screen_action_row = target->kind == _target_row ? target->index : NONE;
		result = screen_action_run(widget, event, action, deleted);
		screen_action_row = NONE;
		if (result && !*deleted && target->kind == _target_choice)
			ui_play_audio_feedback_sound(TALL_SOUND_FORWARD);
		return result;
	}
	return FALSE;
}

/* a key pressed on the screen: a button's of its own do (the bar's, the
band's, a panel's or a table's: the key whose button is shown), LB and RB
the tabs either side (their do), B while a search is typed into its words
cleared; FALSE for what the screen leaves to its widgets */
static boolean tall_key(struct screen_open *open, struct widget_instance *screen, struct event_record *event,
	boolean *deleted)
{
	struct screen_tall *tall = tall_of(open);
	short button;
	long index;

	*deleted = FALSE;
	if (!tall || event->type != SCREEN_EVENT_BUTTON || event->data.button.value != 1)
		return FALSE;
	button = event->data.button.index;
	if (tall_search_editing(tall) && (button == _gamepad_analog_button_b || button == _gamepad_binary_button_back))
	{
		tall_search_clear(tall, tall->search_editing);
		ui_play_audio_feedback_sound(TALL_SOUND_BACK);
		return TRUE;
	}
	if (tall->strip && tall->strip->visible && tall->tab_count &&
		(button == _gamepad_analog_button_white || button == _gamepad_analog_button_black))
	{
		short step = button == _gamepad_analog_button_black ? 1 : -1;
		short at = (short)((tall->tab_shown == NONE ? 0 : tall->tab_shown) + step);

		at = (short)((at + tall->tab_count) % tall->tab_count);
		return screen_action_run(screen, event, tall->tab_blocks[at]->action, deleted);
	}
	{
		struct screen_prompt *prompts[TALL_MAXIMUM_PROMPTS + TALL_MAXIMUM_PANELS * TALL_MAXIMUM_PANEL_BUTTONS + 2];
		long count = 0, made;

		for (index = 0; index < tall->prompt_count; index++)
			prompts[count++] = &tall->prompts[index];
		prompts[count++] = &tall->header_button;
		for (index = 0; index < tall->panel_count; index++)
		{
			for (made = 0; made < tall->panels[index].button_count; made++)
				prompts[count++] = &tall->panels[index].buttons[made];
		}
		prompts[count++] = &tall->table_button;
		for (index = 0; index < count; index++)
		{
			struct screen_prompt *prompt = prompts[index];

			if (!prompt->widget || !prompt->block || !prompt->block->action || !prompt->widget->visible ||
				(prompt->widget->parent && !prompt->widget->parent->visible) ||
				tall_key_button(prompt->key) != button || !strcmp(prompt->key, "accept"))
			{
				continue;
			}
			return screen_action_run(screen, event, prompt->block->action, deleted);
		}
	}
	return FALSE;
}

/* a prompt of the bar chosen (A, a click or a tap on it): its do, else its
key pressed */
static boolean tall_prompt(struct screen_open *open, struct widget_instance *widget, struct event_record *event,
	boolean *deleted, short controller)
{
	struct screen_tall *tall = tall_of(open);
	long index;

	*deleted = FALSE;
	for (index = 0; tall && index < tall->prompt_count; index++)
	{
		struct screen_prompt *prompt = &tall->prompts[index];

		if (prompt->widget != widget)
			continue;
		if (prompt->block->action)
			return screen_action_run(widget, event, prompt->block->action, deleted);
		if (!strcmp(prompt->key, "accept"))
		{
			struct widget_instance *focused = tall->body->focused_child;

			return focused && focused != tall->bar && tall_do(open, focused, event, deleted);
		}
		event_manager_post_button(controller, tall_key_button(prompt->key));
		return TRUE;
	}
	return FALSE;
}

/* "search next" (the next of the screen's searches typed into, from the
first) and "search clear" (every one's words cleared, the focus on the
first row) */
static boolean screen_call_search(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	struct screen_open *open = screen_open_of(widget_instance_get_topmost_parent(widget));
	struct screen_tall *tall = open ? tall_of(open) : NULL;
	short index;

	(void)event;
	*deleted = FALSE;
	if (!tall || !tall->search_count)
		return FALSE;
	if (!strcmp(name, "search next"))
	{
		short next = tall_search_editing(tall) ? (short)((tall->search_editing + 1) % tall->search_count) : 0;

		tall_search_begin(tall, next);
		return TRUE;
	}
	for (index = 0; index < tall->search_count; index++)
		tall_search_clear(tall, index);
	ui_play_audio_feedback_sound(TALL_SOUND_BACK);
	return TRUE;
}

/* a panel's Spartan's original picture and a panel's <picture>: drawn
whole, shrunk or grown to their boxes (menu_screen_picture_stretched) */
static boolean tall_stretched(struct widget_instance const *widget)
{
	long open_index, index;

	for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
	{
		struct screen_tall *tall = tall_screens[open_index];

		for (index = 0; tall && screens.open[open_index].widget && index < tall->line_count; index++)
		{
			if (tall->lines[index].widget == widget && (tall->lines[index].block->kind == MENU_SCREEN_SPARTAN ||
				tall->lines[index].block->kind == MENU_SCREEN_PICTURE))
			{
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* the hooks' boxes: a block's width and picture where its screen says */
static boolean tall_picture_of(struct widget_instance const *widget, long *bitmap_tag, rectangle2d *bounds)
{
	long open_index;

	for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
	{
		struct screen_tall *tall = tall_screens[open_index];
		struct screen_box const *box;

		if (!tall || !screens.open[open_index].widget)
			continue;
		box = tall_box_of(tall, widget);
		if (!box)
			continue;
		*bitmap_tag = box->picture;
		if (box->width)
			bounds->x1 = (short)(bounds->x0 + box->width);
		bounds->x1 = (short)(bounds->x1 + tall_wide(box->dw, TRUE, box->centred));
		if (box->height)
			bounds->y1 = (short)(bounds->y0 + box->height);
		return box->width || box->height || box->dw || box->picture != NONE;
	}
	return FALSE;
}

static boolean tall_color_of(struct widget_instance const *widget, real_argb_color *color)
{
	long open_index;

	for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
	{
		struct screen_tall *tall = tall_screens[open_index];
		struct screen_box const *box;

		if (!tall || !screens.open[open_index].widget)
			continue;
		box = tall_box_of(tall, widget);
		if (!box || !box->color)
			continue;
		color->red = (real)((box->color >> 24) & 0xFF) / 255.0f;
		color->green = (real)((box->color >> 16) & 0xFF) / 255.0f;
		color->blue = (real)((box->color >> 8) & 0xFF) / 255.0f;
		return TRUE;
	}
	return FALSE;
}
