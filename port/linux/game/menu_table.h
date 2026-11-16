/*
MENU_TABLE.H

A hand-written screen's live table (docs/menu_screens.md, <table>): its rows
come from a row source of C's, named in the screen's file (source=), which
the screens' registry (port/linux/game/menu_screen.c) gives the table. The
table is the view: its row widgets, the focus among them, their cells drawn;
the source is what is listed: how many rows, which is first on the table
(the rows scroll), which is chosen, and each row's cells. The server
browser's and Select Profile's are menu_functions.c's (pc_menu_browser_source,
pc_menu_profile_source).
*/

#ifndef MENU_TABLE_H
#define MENU_TABLE_H

struct widget_instance;
struct event_record;

#define MENU_TABLE_TEXT_LENGTH 64
#define MENU_TABLE_BADGE_LENGTH 8

/* a frame's look of the table, which the source reads and answers */
struct menu_table_view
{
	/* (in) the screen; the rows the table has; the row focused (0 up, on
	the table: NONE, none), and whether the pointer resting on it put the
	focus there (not the d-pad, which scrolls at the ends); whether the
	columns' titles are shown, above the first row */
	struct widget_instance *screen;
	short shown;
	short focused;
	short hover;
	short titles;
	/* (in) a table of lanes' (lanes="2"): how many, its rows each lane's
	in turn (the first lane's, then the next's), all of them shown (first
	0); 1 for none */
	short lanes;
	/* (out) the rows in all; the first on the table; the row chosen (its
	details the panel's: NONE, none); the row of the table to give the focus
	(NONE: as it is), or the titles */
	short count;
	short first;
	short chosen;
	short focus;
	short focus_titles;
};

/* a row's cell, as the source says it is */
struct menu_table_cell
{
	/* its words (UTF-16), cut short with "..." to its column by the table */
	unsigned short text[MENU_TABLE_TEXT_LENGTH];
	/* its words' colour, 0xRRGGBBAA (0: the column's own) */
	unsigned long color;
	/* a picture cell's frame (a ping's bars, a star): NONE, not shown */
	short frame;
	/* a pill after the words (empty: none), and a lock after them */
	unsigned short badge[MENU_TABLE_BADGE_LENGTH];
	short lock;
	/* its shorter words, shown in place of its words where they would be
	cut short (empty: none) */
	unsigned short fallback[MENU_TABLE_TEXT_LENGTH];
	/* a network machine whose speaker (network_voice.c's: talking, or
	muted) is drawn just left of its words: NONE, none */
	long speaker;
};

/* a row's state (a table's source's state): not shown (a lane's place
with no row), a row, or a row marked as its own (framed: this machine's
players) */
enum
{
	MENU_TABLE_ROW_NONE,
	MENU_TABLE_ROW_PLAIN,
	MENU_TABLE_ROW_MARKED
};

struct menu_table_source
{
	/* the frame's view read and answered (each frame the table is drawn) */
	void (*update)(struct menu_table_view *view);
	/* whether the row is a heading across the table (its words), not a row
	of cells */
	int (*heading)(short index, unsigned short *words, short size);
	/* the row's cell of the column (by its id; focused: the row has the
	focus) */
	void (*cell)(short index, char const *column, int focused, struct menu_table_cell *cell);
	/* the table's screen opened (not made again by a hot reload): NULL,
	nothing to do */
	void (*opened)(void);
	/* the row's state (MENU_TABLE_ROW_*): NULL, every row up to the count
	a row */
	int (*state)(short index);
};

/* a chat's line, as its source says it is */
struct menu_chat_line
{
	/* its sender's name (empty: a notice), its words (UTF-16), the name's
	colour (0xRRGGBBAA) */
	unsigned short name[MENU_TABLE_BADGE_LENGTH * 2];
	unsigned short text[MENU_TABLE_TEXT_LENGTH];
	unsigned long color;
};

/* a chat's lines and what is typed (<chat>: the lobby's) */
struct menu_chat_source
{
	/* each frame the chat is drawn: its lines, the oldest first (at most
	maximum), their count */
	short (*lines)(struct menu_chat_line *lines, short maximum);
	/* the line being typed (NULL: none), and whether its on-screen keyboard
	is up */
	char const *(*typed)(int *keyboard);
	/* the on-screen keyboard drawn in its place (the menus' screen's units,
	width by height), in a font of the menus' (the line typed's) */
	void (*keyboard)(long font, short x0, short y0, short x1, short y1, short screen_width, short screen_height);
	/* the words the line typed's place shows while none is (empty: none) */
	char const *(*prompt)(void);
};

/* ---------- the server browser's (menu_functions.c) */

extern struct menu_table_source const pc_menu_browser_source;
/* a binding's value, UTF-8 ("browser.<name>": nonzero if it has one) */
int pc_menu_browser_value(char const *name, char *value, unsigned long size);
/* a search's words set ("browser.search.<column>") */
void pc_menu_browser_search_set(char const *name, char const *text);

/* ---------- Select Profile's (menu_functions.c) */

extern struct menu_table_source const pc_menu_profile_source;
/* a binding's value, UTF-8 ("profile.<name>": nonzero if it has one) */
int pc_menu_profile_value(char const *name, char *value, unsigned long size);
/* the row's profile (NONE: the chosen one's) made player 1's: zero for the
row that makes one */
int pc_menu_profile_choose(struct widget_instance *widget, struct event_record *event, short row);

/* ---------- Profile Color's (menu_functions.c) */

extern struct menu_table_source const pc_menu_color_source;
/* a binding's value, UTF-8 ("color.<name>": nonzero if it has one) */
int pc_menu_color_value(char const *name, char *value, unsigned long size);
/* the row's color (NONE: the chosen one's) made the edited profile's:
zero with no profile being edited */
int pc_menu_color_choose(short row);

/* ---------- the lobby's (menu_functions.c's lobby_menu.inc) */

extern struct menu_table_source const pc_menu_lobby_source;
/* a binding's value, UTF-8 ("lobby.<name>": nonzero if it has one) */
int pc_menu_lobby_value(char const *name, char *value, unsigned long size);
/* an action of the lobby's ("lobby <what>"; row: the table's row it is for,
NONE for none): FALSE if it did nothing */
int pc_menu_lobby_action(struct widget_instance *widget, struct event_record *event, char const *name, short row,
	int *deleted);
/* each frame the lobby's screen is drawn: what its generated screen's list
did each frame (the split screen's players joining, the map vote's screen) */
void pc_menu_lobby_frame(struct widget_instance *screen);
/* "lobby start" (the lobby's Start, START GAME): a controller not in the
game joins (Add Player's screen opened, *deleted the lobby gone), a
player's starts the host's game; FALSE if it did nothing */
int pc_menu_lobby_start(struct widget_instance *widget, struct event_record *event, int *deleted);
/* the lobby's chat (network_chat.c's lobby lines) */
extern struct menu_chat_source const pc_menu_lobby_chat;
/* a text box drawn with a machine's speaker beside it (the voice chat's:
menu_functions_text_box_drawn), this frame */
void pc_menu_speaker_note(struct widget_instance *text, long machine);

/* ---------- Add Player's (menu_functions.c): each of this machine's
controllers a row, one not in the game joining with its own keys */

/* a controller's key on a screen that takes them by controller (menu_screen.c
tells them: a press, not a held one's repeat) */
enum
{
	MENU_SCREEN_KEY_OTHER,
	MENU_SCREEN_KEY_A,
	MENU_SCREEN_KEY_B,
	MENU_SCREEN_KEY_START,
	MENU_SCREEN_KEY_LEFT,
	MENU_SCREEN_KEY_RIGHT
};

/* what became of a controller's key: the screen's as ever (its focus, its
B), taken, or taken and the screen done (back to the one before) */
enum
{
	MENU_SCREEN_KEY_FREE,
	MENU_SCREEN_KEY_TAKEN,
	MENU_SCREEN_KEY_DONE
};

extern struct menu_table_source const pc_menu_add_player_source;
/* a binding's value, UTF-8 ("add.<name>": nonzero if it has one) */
int pc_menu_add_player_value(char const *name, char *value, unsigned long size);
/* a controller's key (MENU_SCREEN_KEY_*): MENU_SCREEN_KEY_FREE for a player
in the game's */
int pc_menu_add_player_key(short controller, int key);
/* "add player done" and "add player back": nonzero to go back */
int pc_menu_add_player_action(char const *name);

/* (menu_screen.c's) whether the hand-written screen of the name is ready
to open (the new menus, its file read), and it opened from the widget as a
handler's open opens a screen (zero if it is not) */
int menu_screen_open_ready(char const *name);
int menu_screen_open_named(struct widget_instance *widget, struct event_record *event, char const *name,
	int *deleted);

/* ---------- the menus' text fields and text (menu_functions.c), for the
hand-written screens' searches */

/* a field typed into (its words to begin with; done: its words when it is
left with them kept), Enter ending it */
void pc_menu_text_field_begin(struct widget_instance *field, char const *text, short maximum,
	void (*done)(char const *text));
/* whether the field is typed into (NULL: any) */
int pc_menu_text_field_editing(struct widget_instance *field);
/* the field typed into left (keep: its words kept, its done told) */
void pc_menu_text_field_end(int keep);
/* the field shown (its caret blinking while it is typed into), the keys
typed taken; the words typed so far */
void pc_menu_text_field_show(struct widget_instance *field, char const *text, int editing);
char const *pc_menu_text_field_words(void);
/* a text box's words fitted to width (cut short with "..."), returning how
wide they are drawn; wrapped to width in lines at most */
short pc_menu_text_fit(struct widget_instance *box, unsigned short const *text, short width);
void pc_menu_text_wrap(struct widget_instance *box, unsigned short const *text, short width, short lines);
/* how many lines text wraps to across width in a text box's font, lines at
most */
short pc_menu_text_lines(struct widget_instance *box, unsigned short const *text, short width, short lines);
/* text's width in a text box's font (0: not known) */
short pc_menu_text_width(struct widget_instance *box, unsigned short const *text);

#endif
