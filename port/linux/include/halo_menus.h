/*
HALO_MENUS.H

The menus written in XML (port/assets/menus, and a menus folder beside
config.toml), as the platform layer reads them (port/linux/src/menu_files.c)
for the game, which builds them into widget tags when ui.map loads
(port/linux/game/menu_tags.c). Plain C89 types, for both: each element keeps
its attributes as written, and the file and line it came from; the game reads
their meaning.

A file holds <menus> with <bitmap> (holding <frame>s), <strings> (holding
<string>s) and <widget> elements; a <widget> holds <widget> and <child>
(its children), <on> (an event handler), <data> (a game data input),
<conditional> and <replace> elements. port/assets/menus/README.md describes
them.
*/

#ifndef HALO_MENUS_H
#define HALO_MENUS_H

#define HALO_MENU_NONE (-1L)
/* the port's event handler and game data functions (menu_functions.c) are
numbered from here, clear of the Xbox's (0 to 101 and 0 to 40) and the PC's
(to 189 and 58) */
#define PC_MENU_FUNCTION_BASE 256

/* a frame: a PNG drawn at width by height, or the map's bitmap's frame
(map: its bitmap group tag, index: the bitmap's number in it), drawn as the
map has it or, with a width and height, scaled to that size, x and y into
the widget */
struct halo_menu_frame
{
	char const *png;
	char const *map;
	long index;
	long width, height;
	long x, y;
};

struct halo_menu_bitmap
{
	char const *name;
	/* its frames: frame_count from first_frame in the frames */
	long first_frame, frame_count;
	char const *file;
	long line;
};

struct halo_menu_strings
{
	char const *name;
	/* its strings: count from first in the strings */
	long first, count;
	/* the PC version's list of the same strings (its tag's name), which a
	map in another language has in its language; NULL if none */
	char const *map;
	char const *file;
	long line;
};

struct halo_menu_handler
{
	char const *event, *run, *script, *open, *replace, *close, *widget, *focus, *reload, *sound, *otherwise, *label;
	long back, branch;
	long next;
	char const *file;
	long line;
};

struct halo_menu_input
{
	char const *input;
	long next;
	char const *file;
	long line;
};

/* a child: a <widget> inside its parent (nested), or one named (<child>) */
struct halo_menu_child
{
	long nested;
	char const *widget;
	long x, y;
	char const *controller;
	long next;
	char const *file;
	long line;
};

struct halo_menu_conditional
{
	char const *widget;
	long if_failed;
	long next;
	char const *file;
	long line;
};

struct halo_menu_replace
{
	char const *search, *function;
	long next;
	char const *file;
	long line;
};

struct halo_menu_widget
{
	char const *name, *type, *controller, *flags, *bitmap, *text, *strings, *values, *setting, *font, *color,
		*align, *description, *string_list, *list_flags, *text_flags, *header_bitmap, *footer_bitmap,
		*header_bounds, *footer_bounds, *wide;
	long x, y, left, top, width, height, text_x, text_y, auto_close, auto_close_fade, string_index;
	long has_width, has_height;
	/* its picture's corners cut round by this many units (0: square) */
	long radius;
	/* the generator's file, line and functions that wrote it (its source
	attribute, tools/menu_sources.py's; NULL none): debug.render_marks' */
	char const *source;
	/* indices in their arrays, or HALO_MENU_NONE */
	long parent, first_child, first_handler, first_input, first_conditional, first_replace;
	char const *file;
	long line;
};

struct halo_menus
{
	struct halo_menu_bitmap *bitmaps;
	long bitmap_count;
	struct halo_menu_frame *frames;
	long frame_count;
	struct halo_menu_strings *string_lists;
	long string_list_count;
	char const **strings;
	/* each string's index in its list's map list (HALO_MENU_NONE: the
	port's own, in English only) */
	long *string_maps;
	long string_count;
	struct halo_menu_widget *widgets;
	long widget_count;
	struct halo_menu_child *children;
	long child_count;
	struct halo_menu_handler *handlers;
	long handler_count;
	struct halo_menu_input *inputs;
	long input_count;
	struct halo_menu_conditional *conditionals;
	long conditional_count;
	struct halo_menu_replace *replaces;
	long replace_count;
	/* the main menu: <menus root="..."> (the last given), else "main_menu" */
	char const *root;
};

/* ---------- the menu engine's model (port/assets/menus/model, written by
tools/port_settings.py and tools/menu_look.py: docs/menu_engine.md), read by
menu_model.c */

/* the model's files' folder, beside the menus' (whose reading skips it) */
#define HALO_MENU_MODEL_FOLDER "model/"

/* a choice of a row (a spinner's value, or a slider's stop) */
struct halo_menu_model_choice
{
	char const *shown, *value;
};

/* a row of a screen, a section's title over rows, or a button: kind is
"section", "button", or the row's ("choice", "slider", "link", "binding",
"status", "stepped"); its widgets are named by key (op_<key> and
<key>_label for a row, as tools/port_settings.py names them) */
struct halo_menu_model_row
{
	char const *kind, *key, *setting, *label, *title, *when, *open, *run, *help, *note;
	/* (when it is enabled, else dimmed: a condition as when's) */
	char const *enabled;
	/* (Settings' search, the menu engine's: the way to the row, "VIDEO ›
	DISPLAY"; the other words it is found by, as its label is; its choices'
	words it is found by, the common ones left out: tools/port_settings.py's
	SETTINGS_SEARCH) */
	char const *crumb, *search, *search_choices;
	/* (a node's, kind "node": its key its widget's name, its parent's name,
	whether the focus may take it, and whether it takes it first) */
	char const *parent;
	long focusable, initial;
	/* (and its place in its screen's help, its choices' after it each its
	own with help_choices; NONE: none) */
	long help_index, help_choices;
	/* (in the place of the row before it; hidden while the game plays in
	the headset; a label the game writes; a status row's value beside it) */
	long same_place, not_in_vr, named, status;
	/* a slider's track, step and the values it may be typed (whole: in whole
	numbers); its stops, or a spinner's choices, from first_choice */
	double least, most, step, typed_least, typed_most;
	long whole;
	long first_choice, choice_count;
	char const *file;
	long line;
};

/* a screen: kind "settings", laid out as layout says ("tab", "page" or
"options": rows spacing apart), in the geometry named so where the menu
engine draws it (NULL: the first, the classic engine's); its rows from
first_row */
struct halo_menu_model_screen
{
	char const *name, *kind, *layout, *geometry, *tab, *title, *about;
	long spacing, platform_places, profile;
	long first_row, row_count;
	char const *file;
	long line;
};

/* a geometry the settings rows are laid out by (tools/port_settings.py's
SETTINGS_*, and SettingsLayout's), in the menus' 640x480 units: its name
(NULL for the first, the classic engine's), its rows' and sections' places,
where the rows and the panel end, and whether rows past the end are
scrolled to (the tall layout's: menu_engine.c) */
#define HALO_MENU_MODEL_MAXIMUM_GEOMETRIES 4
struct halo_menu_model_geometry
{
	char const *name;
	long row_x, row_width, row_height, row_step, section_height, section_step, section_gap, tab_top, page_top,
		rows_bottom, spinner_x, spinner_width, panel_bottom;
	long scrolls;
	/* (Settings' search: the least extra width of a wide screen that shows
	the field's word, the most rows found made, the most groups of them) */
	long search_word_extra, search_results, search_crumbs;
};

/* where a widget's child is in it (a look's <at>) */
struct halo_menu_model_place
{
	char const *widget;
	long x, y;
};

/* a widget's look (model/look, written by tools/menu_look.py): its type,
its box (x0 y0 x1 y1 in its parent's units; 0 0 640 480 if has_box is 0),
its wide layout ("dx dw"), its place and effect flags (as the widgets' flags
name them), its picture and its corners' radius, its list's flags, header
and footer; its text (has_text: a <text>) and its text's style; and its
children's places, from first_place. Names are the menus' (a <bitmap>, a
<strings>, a widget), or a map's tag with a backslash */
struct halo_menu_model_look
{
	char const *name, *type, *wide, *past, *place, *picture, *effects, *list, *header, *footer;
	/* (what the widget is to the game's code, which finds it by it: "tabs",
	"buttons", "panel name", ...; tools/port_settings.py's
	settings_look_roles) */
	char const *role;
	long box[4], header_box[4], footer_box[4];
	long has_box, has_header_box, has_footer_box;
	long radius;
	long has_text;
	char const *font, *color, *align, *text_flags, *string_list, *text, *items, *replace;
	long text_x, text_y, string_index;
	/* (its text's scale at the menus' 640 and at 16:9 and wider, "least
	most": NULL, its font's size) */
	char const *text_scale;
	long first_place, place_count;
	char const *file;
	long line;
};

/* a widget's behaviour (model/behaviour, tools/menu_behaviour.py): its
flags that are behaviour (how it takes its events: the menus' widget flags,
but the look's), and its actions, the menus' <on> handlers, from
first_handler */
struct halo_menu_model_behaviour
{
	char const *name, *flags;
	long first_handler, handler_count;
	char const *file;
	long line;
};

struct halo_menu_model
{
	/* (the first geometry, and every one, by their names) */
	struct halo_menu_model_geometry geometry;
	struct halo_menu_model_geometry geometries[HALO_MENU_MODEL_MAXIMUM_GEOMETRIES];
	long geometry_count;
	struct halo_menu_model_screen *screens;
	long screen_count;
	struct halo_menu_model_row *rows;
	long row_count;
	struct halo_menu_model_choice *choices;
	long choice_count;
	struct halo_menu_model_look *looks;
	long look_count;
	struct halo_menu_model_place *places;
	long place_count;
	struct halo_menu_model_behaviour *behaviours;
	long behaviour_count;
	struct halo_menu_handler *handlers;
	long handler_count;
};

/* whether a platform of the menus' (a platform attribute's, a model's when's:
desktop android vr hdr touch pointer computer) is this one: 1 or 0, -1 for a
name that is not one's (menu_files.c) */
int halo_menus_platform(const char *name, unsigned long length);

/* the model, read from its files once the menus are (halo_menus_load);
NULL if there is none or a file has a problem (logged) */
struct halo_menu_model const *halo_menus_model(void);
/* a widget's look in the model by its name, NULL if it has none */
struct halo_menu_model_look const *halo_menus_model_look(struct halo_menu_model const *model, char const *name);
/* the model's behaviour of a widget by its name (NULL: none, a widget with no
behaviour flags nor actions) */
struct halo_menu_model_behaviour const *halo_menus_model_behaviour(struct halo_menu_model const *model,
	char const *name);
/* the model read again by the next halo_menus_model (halo_menus_reload's) */
void halo_menus_model_forget(void);
/* the number of the model's reads (one more each time it is read again: a
cache of anything in it is kept for one) */
unsigned long halo_menus_model_reads(void);
/* a file gathered for the menus (halo_menus_load's): its path and data,
FALSE past the last */
int halo_menus_file_at(long index, char const **path, unsigned char const **data, unsigned long *size);

/* the menus, read from the files (once; the same each call after); NULL if
there are none, or a file has an error (logged) */
struct halo_menus const *halo_menus_load(void);
/* the files read on a thread of their own, from now, for halo_menus_load
(which waits for it) */
void halo_menus_read_ahead(void);
/* the files read again by the next halo_menus_load, and a number that
changes when the menus folder's files do (the menu studio:
port/linux/game/menu_studio.c) */
void halo_menus_reload(void);
unsigned long long halo_menus_files_stamp(void);
/* the menu studio's notes (menu_studio.c): the notes file's path and its
pictures' folder; a line added to it; the file read whole, and let go */
void halo_menus_notes_paths(char *notes, unsigned long notes_size, char *pictures, unsigned long pictures_size);
int halo_menus_note_append(char const *path, char const *line);
char *halo_menus_notes_read(char const *path);
void halo_menus_notes_free(char *text);
/* text as UTF-16, its terminator included, "\n" written as the game's line
breaks ("\r\n"): the number of characters written (at most capacity) */
long halo_menus_utf16(char const *utf8, unsigned short *out, long capacity);
/* logs a problem in a file's element */
void halo_menus_log(char const *file, long line, char const *message, char const *detail);

/* menu art: the D3D texture (as the game's bitmap holds it) a frame's PNG is
drawn for, until halo_menus_art_forget */
void halo_menus_art_register(void const *texture, char const *png);
void halo_menus_art_forget(void);
/* the PNG of the menu art drawn for the texture decoded ahead, on a thread
(none if it is not menu art, or is decoded or drawn already) */
void halo_menus_art_ahead(void const *texture);

#endif
