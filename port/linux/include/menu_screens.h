/*
MENU_SCREENS.H

The menus' hand-written screens (port/assets/menus/screens,
docs/menu_screens.md): one file a screen, its building blocks and what each
does, read by the platform layer (port/linux/src/menu_screens.c) and made
into widgets by the menu engine (port/linux/game/menu_screen.c). Plain C89
types, for both.

A screen file holds <screen> (name, back), and in it, from the top of the
screen down: <title-art/>, <header> (title, picture, detail, a <button>),
<list> (focus) holding <item>s (id, label, hint or <hint>s, do, when),
<cards> holding <card>s, <tabs> (current) holding <tab>s, <table> (source,
sorted, do, empty, lanes, left, right) holding <lane>s, <column>s (each a <search> in its title) and a
<button>, <search>, <panel>s (each its if) holding <heading>, <text>, <pills> of <pill>s,
<pair>, <choice>, <picture>, <spartan> and <button>s, <chat> (source, lines, do, if), <space/>, <box> (x, y, width, align) holding a
<list> and a <legend> (x), <objectives> (title), <dialog> (title, text) holding a <list> of its
choices, and <buttons> holding
<button>s (key, label, do, width, if). Texts may hold bindings, {name}, filled from C; a do and the
screen's back are actions, a table's source a row source, an if, a current,
a sorted and a search's value bindings: names the game knows. All are
checked as the file is read against the names the game gave
(menu_screens_names).
*/

#ifndef MENU_SCREENS_H
#define MENU_SCREENS_H

/* the most a screen has of each, and the longest of its texts */
#define MENU_SCREEN_MAXIMUM_BLOCKS 96
#define MENU_SCREEN_MAXIMUM_HINTS 4
#define MENU_SCREEN_MAXIMUM_STRINGS 640
#define MENU_SCREEN_MAXIMUM_TEXT 256
#define MENU_SCREEN_MAXIMUM_SCREENS 16
/* a screen's <panel>s (each its if: the first whose if has a value shown),
and its <spartan>s (one of its own, one in each panel) */
#define MENU_SCREEN_MAXIMUM_PANELS 4
#define MENU_SCREEN_MAXIMUM_SPARTANS (MENU_SCREEN_MAXIMUM_PANELS + 1)

enum menu_screen_block_kind
{
	MENU_SCREEN_TITLE_ART,
	MENU_SCREEN_LIST,
	MENU_SCREEN_ITEM,
	MENU_SCREEN_SPACE,
	MENU_SCREEN_BUTTONS,
	MENU_SCREEN_BUTTON,
	/* (cards side by side: each its picture, title and subtitle) */
	MENU_SCREEN_CARDS,
	MENU_SCREEN_CARD,
	/* (an inner screen's header: its lettered picture, its title the words) */
	MENU_SCREEN_HEADER,
	/* (the player profile's colors, one item each, and random: the screen's
	list) */
	MENU_SCREEN_COLORS,
	/* (the Spartan, drawn in the menus' scene where it is: menu_spartan.h) */
	MENU_SCREEN_SPARTAN,
	/* (a strip of tabs, the current one framed; each tab its label and do) */
	MENU_SCREEN_TABS,
	MENU_SCREEN_TAB,
	/* (a live table: its rows from a source of C's, its columns' titles
	sorting it; a column's search typed into in its title's place) */
	MENU_SCREEN_TABLE,
	MENU_SCREEN_COLUMN,
	MENU_SCREEN_SEARCH,
	/* (the details of a table's chosen row, beside it: its heading, lines
	of text, pills, label and value pairs, a button) */
	MENU_SCREEN_PANEL,
	MENU_SCREEN_PANEL_HEADING,
	MENU_SCREEN_PANEL_TEXT,
	MENU_SCREEN_PILLS,
	MENU_SCREEN_PILL,
	MENU_SCREEN_PAIR,
	/* (a table's lane: its rows side by side with the other lanes', each
	lane its panel, title and detail) */
	MENU_SCREEN_LANE,
	/* (a panel's choice: its label, its value between arrows, stepped) */
	MENU_SCREEN_CHOICE,
	/* (a chat's lines, under a tall screen's table, and the line typed) */
	MENU_SCREEN_CHAT,
	/* (a framed box, the pause menu's: its <list> in it, its rows as many
	as are shown, and its <legend> under them; the box as tall as they need,
	from the top or about its middle (align: side), its x and width the
	menus' (box_x, box_width), its top or middle box_y) */
	MENU_SCREEN_BOX,
	/* (a box's legend row: B = CANCEL, A = SELECT, as the Xbox's pause menu
	has it under its rows, its left box_x into the box) */
	MENU_SCREEN_LEGEND,
	/* (a framed box beside the box before it, as tall: its title (label)
	and the mission's objectives, a line each) */
	MENU_SCREEN_OBJECTIVES,
	/* (a panel's picture of one of the menus' sets: a map's, a mission's,
	its frame a binding's; as tall as its height, or the room the panel's
	other lines leave) */
	MENU_SCREEN_PICTURE,
	/* (a pop-up's box in the middle of the screen, the Quit question's: its
	title (label) in its band, its words (text) under it, and its <list>'s
	items its choices, side by side at its foot) */
	MENU_SCREEN_DIALOG
};

/* a <dialog>'s choices, at most */
#define MENU_SCREEN_MAXIMUM_CHOICES 4

/* a <spartan>'s color: the focused <colors> item's, the profile's, or a
binding's (its value, the block's value: a color's number, random, or none
for no Spartan) */
enum menu_screen_spartan_color
{
	MENU_SCREEN_SPARTAN_FOCUS,
	MENU_SCREEN_SPARTAN_PROFILE,
	MENU_SCREEN_SPARTAN_BINDING
};

/* a column's cells: words, words in white (the row's name), a ping (its
bars and its number), a star (a favorite) */
enum menu_screen_cell_kind
{
	MENU_SCREEN_CELL_TEXT,
	MENU_SCREEN_CELL_STRONG,
	MENU_SCREEN_CELL_PING,
	MENU_SCREEN_CELL_STAR,
	/* (a dot of a colour, by its frame: a team's) */
	MENU_SCREEN_CELL_DOT,
	/* (a swatch of a profile color, by its frame: the colors', then
	random's) */
	MENU_SCREEN_CELL_SWATCH
};

/* a building block: its kind, the block it is in (-1: the screen), and what
its kind has (NULL where it has none). An item's hints are alternatives: it
shows the first whose bindings all have values */
struct menu_screen_block
{
	int kind;
	int parent;
	char const *id;
	char const *label;
	char const *action;
	char const *when;
	char const *key;
	/* (an item drawn as a picture of the menus' files: its bitmap's name,
	"main_menu/menu_versus"; its label then the words it stands for) */
	char const *picture;
	/* (a button's side of the bar: "left" or "right"; NULL, back's left
	and the others' right) */
	char const *side;
	/* (a card's picture's frame: the campaign's stills by level; -1, none) */
	long frame;
	/* (a list's item focused first, by its id) */
	char const *focus;
	char const *hints[MENU_SCREEN_MAXIMUM_HINTS];
	int hint_count;
	/* (a <spartan>'s box in the menus' 640 by 480 (in a <panel>, its
	height alone: its width and place the panel's), its color's source and
	how many side by side) */
	long box_x, box_y, box_width, box_height;
	/* (a <spartan>'s or a <picture>'s height on a touchscreen) */
	long touch_box_height;
	int color;
	int players;
	/* (a binding named: a tabs' current, a table's sorted, a search's
	value, a <spartan>'s color; NULL, none) */
	char const *value;
	/* (a lineup's <spartan>'s names: the binding giving its places' names,
	a line each, under its Spartans; NULL, none) */
	char const *names;
	/* (the binding whose value must not be empty for the block to be
	shown, checked each frame: its if; NULL, always) */
	char const *condition;
	/* (a table's row source, and its words while it has no rows; a
	header's detail, at the band's right, and its fallback in source: its
	shorter forms, a line each, shown where the detail does not fit) */
	char const *source;
	char const *empty;
	/* (a column's title's action: sorts by it) */
	char const *sort;
	/* (a column's: its kind of cell, menu_screen_cell_kind; its width, how
	much wider it grows on a wide screen and the room before it (after the
	column before), in the menus' units; its
	words at its right; and on a touchscreen its place in a row of two
	lines, x y width and its wide move and growth, its touch_width 0 if it
	has none there) */
	int cell;
	short width, grow, gap;
	/* (its title's width, 0: as its words need) */
	short title_width;
	int right;
	short touch_x, touch_y, touch_width, touch_move, touch_grow;
	/* (a panel's text: the lines it wraps to, and on a touchscreen) */
	short lines, touch_lines;
	/* (a pill's frame, a number, or a binding's name giving one; a lane's) */
	char const *tone;
	/* (a table's lanes, 1 for none; the actions of left and right on its
	rows, and on a choice (right also A); a choice's binding whose value
	dims it, its steps then refused) */
	short lanes;
	char const *left_action;
	char const *right_action;
	char const *dim;
	/* (a <dialog>'s words, under its title) */
	char const *text;
	long line;
};

struct menu_screen
{
	char const *name;
	/* (B and Back: an action, NULL for the screen before; Start anywhere on
	the screen: an action, NULL for its widgets') */
	char const *back;
	char const *start;
	/* (a tall screen's background: the tall layout's over the generated
	screen's own, background="tall"; else the generated screen's, the tall
	layout's where it has none) */
	int tall_background;
	char const *file;
	struct menu_screen_block blocks[MENU_SCREEN_MAXIMUM_BLOCKS];
	int block_count;
	/* (the strings it owns) */
	char *strings[MENU_SCREEN_MAXIMUM_STRINGS];
	int string_count;
};

/* the names the game gives the files: its actions (a block's do, a screen's
back), its bindings ({name} in a text, an if, a current...) and its row
sources (a table's); a name not among them is a
problem in the file. NULL tables: any name passes */
struct menu_screen_names
{
	char const *const *actions;
	long action_count;
	char const *const *bindings;
	long binding_count;
	char const *const *sources;
	long source_count;
	/* (a <chat>'s sources) */
	char const *const *chats;
	long chat_count;
};

/* a binding's value as text (UTF-8) for menu_screen_text: nonzero if it has
one */
typedef int (*menu_screen_value)(void *context, char const *name, char *value, unsigned long size);

/* ---------- reading (menu_screens.c) */

/* the game's names, checked against as the files are read from now */
void menu_screens_names(struct menu_screen_names const *names);
/* a file read into *screen (nonzero), else its problem written to problem
("<file>:<line>: <what>") and nothing kept (zero) */
int menu_screen_read(char const *file, unsigned char const *data, unsigned long size, struct menu_screen *screen,
	char *problem, unsigned long problem_size);
/* what a screen read owns let go of */
void menu_screen_free(struct menu_screen *screen);
/* text with its bindings filled, in UTF-8 (nonzero if every binding had a
value; else out is as far as it got) */
int menu_screen_text(char const *text, menu_screen_value value, void *context, char *out, unsigned long size);
/* whether a when (platforms, separated by spaces: any of them) holds here
(halo_menus_platform's names); NULL holds */
int menu_screen_when(char const *when);

/* ---------- the screens (menu_screens.c) */

/* the screen of the name, its last good reading: NULL if no file has it */
struct menu_screen const *menu_screens_find(char const *name);
/* the number of the screen's reading (it changes when its file is read
again), 0 for none */
unsigned long menu_screens_generation(char const *name);
/* the watched folder looked at, at most twice a second (now: a clock in
milliseconds), and each file changed in it read again: a file with a problem
is logged and its screen keeps its last good reading */
void menu_screens_poll(unsigned long now);
/* the folder watched (debug.menu_screens, else debug.menu_files'
screens/), empty for none */
char const *menu_screens_folder(void);

#endif
