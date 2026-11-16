/*
MENU_STUDIO.C

debug.menu_studio: the game as the menus' builder, so that what is seen
working on them is what the game draws (no other renderer to drift from
it). The game starts on the setting's screen of the menus (as
debug.menu_open, pc_menus_root_name), and then:

- the screen is reloaded within a second of the menu files changing (their
  folder, debug.menu_files' or the menus folder beside config.toml, looked
  at twice a second: halo_menus_files_stamp), or on F7: the screens are
  closed, the menus' tags let go and built again from the files read anew
  (menu_tags.c, menu_files.c), and the screen opened again with the focus
  where it was. Each reload is logged with its time, and, with
  debug.screenshot_directory, its first frame saved (studio-<n>.bmp);
- F3 steps the input the menus are drawn for: the keyboard and mouse's
  prompts, the controller's, the touchscreen's layout (debug.touch_menus,
  its screens: a reload) with the controller's prompts;
- F4 steps the window's shape through the gallery's (tools/menu_gallery.py):
  16:9, 16:10, a phone's 20:9, 4:3, 21:9;
- F5 makes every label of the menus longer by half (each word's first
  letters again), to see where a longer one would not fit;
- F6 outlines each widget's box and the problems the layout's checks find
  (menu_layout.c's menu_layout_overlay: tools/menu_gallery.py's checks);
- F9 leaves a note: click a widget (or press A on the focused one; a click
  on no widget notes the screen as a whole), type it, Enter saves and Escape
  cancels. Each note is a JSON line in the notes file (debug.menu_notes:
  halo_menus_notes_paths), with the screen, the widget's name, path, words
  and box, the input and the window's shape, and a PNG of the screen with
  the widget outlined; a widget with an open note is marked, and the line
  counts them (tools/menu_notes.py lists them and marks them done).

A line at the top says which, and the problems' count. The settings these
change are this run's alone (config_set_for_run). In a build without
HALO_RELEASE the keys work in any game's menus too, without the setting.
*/

#include "cseries.h"
#include "math/integer_math.h"
#include "menu_layout.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

int config_set_for_run(const char *name, const char *value);
int config_boolean(char const *name);
char const *config_string(char const *name);
void platform_log(char const *format, ...);
int platform_menu_studio_keys(void);
void platform_display_apply(void);
void halo_screenshot_named(const char *name);
unsigned long system_milliseconds(void);
void halo_menus_reload(void);
unsigned long long halo_menus_files_stamp(void);
void menu_tags_unloaded(void);
void menu_tags_loaded(char const *map_name);
void saving_screens_tags_loaded(void);
void menu_tags_files_changed(void);
boolean ui_widgets_active(void);
/* (ui_widget.c) */
char const *ui_widget_port_screen_name(void);
long ui_widget_port_focus_path(short *path, long maximum);
void ui_widget_port_focus_restore(short const *path, long count);
boolean ui_widget_port_open_screen(char const *tag_name);
void const *ui_widget_port_focused_widget(void);
long halo_screen_width(void);
void platform_menu_studio_typing(int typing);
int platform_menu_studio_typed(char *typed, int size);
void halo_screenshot_png(const char *directory);
int halo_screenshot_png_taken(char *name, unsigned long size);
int halo_frames_unseen(void);
void halo_menus_notes_paths(char *notes, unsigned long notes_size, char *pictures, unsigned long pictures_size);
int halo_menus_note_append(char const *path, char const *line);
char *halo_menus_notes_read(char const *path);
void halo_menus_notes_free(char *text);

/* the frames between two looks at the files (twice a second at 60) */
#define MENU_STUDIO_WATCH_FRAMES 30
#define MENU_STUDIO_FOCUS_DEPTH 16

enum
{
	_menu_studio_keyboard,
	_menu_studio_controller,
	_menu_studio_touch,
	NUMBER_OF_MENU_STUDIO_INPUTS
};

static char const *const menu_studio_input_names[NUMBER_OF_MENU_STUDIO_INPUTS] =
{
	"keyboard", "controller", "touch"
};

/* the gallery's shapes, as windows (points) that fit a laptop's screen */
static struct
{
	char const *name;
	char const *size;
} const menu_studio_shapes[] =
{
	{ "16:9", "1280x720" },
	{ "16:10", "1280x800" },
	{ "phone 20:9", "1600x720" },
	{ "4:3", "1024x768" },
	{ "21:9", "1680x720" },
};

static struct
{
	boolean started;
	boolean on;
	long frame;
	unsigned long long stamp;
	short input;
	short shape;
	boolean long_text;
	boolean overlay;
	boolean drawing;
	long reloads;
	long shot_in;
	unsigned long reloaded_milliseconds;
} menu_studio;

/* ---------- notes (F9) */

#define MENU_STUDIO_NOTE_LENGTH 200
#define MENU_STUDIO_MAXIMUM_OPEN_NOTES 256

enum
{
	_menu_studio_comment_off,
	_menu_studio_comment_picking,
	_menu_studio_comment_typing,
	_menu_studio_comment_shooting
};

static struct
{
	short state;
	/* the widget noted (its record in the kept frame; -1, the screen), and
	what the note says of it, taken as it was picked */
	long picked;
	char picked_name[160];
	char picked_json[2048];
	char note[MENU_STUDIO_NOTE_LENGTH];
	long length;
	long saved;
	/* the open notes: their screens' and widgets' tag names */
	boolean read;
	long open_count;
	char (*open_screens)[160];
	char (*open_widgets)[160];
	char path[512];
	char pictures[512];
} menu_studio_notes;

boolean menu_studio_on(
	void)
{
	return config_string("debug.menu_studio")[0] != 0;
}

/* whether the keys work: the studio, or a build with its assertions in the
menus */
static boolean menu_studio_keys_work(
	void)
{
#ifdef HALO_RELEASE
	return menu_studio_on();
#else
	return menu_studio_on() || ui_widgets_active();
#endif
}

/* the open screen built again from its files, the focus where it was */
static void menu_studio_reload(
	char const *why)
{
	unsigned long started = system_milliseconds();
	char name[256];
	short path[MENU_STUDIO_FOCUS_DEPTH];
	long depth;

	snprintf(name, sizeof(name), "%s", ui_widget_port_screen_name());
	depth = ui_widget_port_focus_path(path, MENU_STUDIO_FOCUS_DEPTH);
	ui_widget_port_open_screen(NULL);
	menu_tags_unloaded();
	halo_menus_reload();
	menu_tags_files_changed();
	menu_tags_loaded("ui");
	/* (their saving screens skipped, as when the map's tags load) */
	saving_screens_tags_loaded();
	if (!name[0] || !ui_widget_port_open_screen(name))
	{
		platform_log("menus: studio: %s could not be opened again; the main menu", name[0] ? name : "the screen");
		ui_widget_port_open_screen("pc\\main_menu\\main_menu");
	}
	else
	{
		ui_widget_port_focus_restore(path, depth);
	}
	menu_studio.stamp = halo_menus_files_stamp();
	menu_studio.reloads++;
	menu_studio.reloaded_milliseconds = system_milliseconds() - started;
	menu_studio.shot_in = 2;
	platform_log("menus: studio: %s reloaded (%s) in %lu ms", name, why, menu_studio.reloaded_milliseconds);
}

static void menu_studio_input_apply(
	void)
{
	boolean touch = menu_studio.input == _menu_studio_touch;
	boolean was_touch = config_boolean("debug.touch_menus") != 0;

	config_set_for_run("input.button_prompts",
		menu_studio.input == _menu_studio_keyboard ? "keyboard" : "controller");
	config_set_for_run("debug.touch_menus", touch ? "true" : "false");
	if (touch != was_touch)
		menu_studio_reload("input");
}

/* a JSON string's characters, escaped, at the end of a buffer */
static void menu_studio_json_string(
	char *json,
	long size,
	char const *text)
{
	long length = (long)strlen(json);
	long index;

	for (index = 0; text[index] && length < size - 8; index++)
	{
		unsigned char character = (unsigned char)text[index];

		if (character == '"' || character == '\\')
		{
			json[length++] = '\\';
			json[length++] = (char)character;
		}
		else if (character < 0x20)
		{
			length += snprintf(json + length, (size_t)(size - length), "\\u%04x", character);
		}
		else
		{
			json[length++] = (char)character;
		}
	}
	json[length] = 0;
}

/* a field's value ("screen":"...") in a line of the notes file, or "" */
static void menu_studio_json_field(
	char const *line,
	char const *name,
	char *value,
	long size)
{
	char key[48];
	char const *found;
	long length = 0;

	value[0] = 0;
	snprintf(key, sizeof(key), "\"%s\":\"", name);
	found = strstr(line, key);
	if (!found)
		return;
	for (found += strlen(key); *found && *found != '"' && length < size - 1; found++)
	{
		if (*found == '\\' && found[1])
			found++;
		value[length++] = *found;
	}
	value[length] = 0;
}

/* the notes file's open notes, read again (the markers) */
static void menu_studio_notes_read(
	void)
{
	char *text, *line;

	if (!menu_studio_notes.path[0])
		halo_menus_notes_paths(menu_studio_notes.path, sizeof(menu_studio_notes.path), menu_studio_notes.pictures,
			sizeof(menu_studio_notes.pictures));
	if (!menu_studio_notes.open_screens)
	{
		menu_studio_notes.open_screens = malloc(MENU_STUDIO_MAXIMUM_OPEN_NOTES * sizeof(*menu_studio_notes.open_screens));
		menu_studio_notes.open_widgets = malloc(MENU_STUDIO_MAXIMUM_OPEN_NOTES * sizeof(*menu_studio_notes.open_widgets));
		if (!menu_studio_notes.open_screens || !menu_studio_notes.open_widgets)
			return;
	}
	menu_studio_notes.read = TRUE;
	menu_studio_notes.open_count = 0;
	text = halo_menus_notes_read(menu_studio_notes.path);
	for (line = text; line && *line && menu_studio_notes.open_count < MENU_STUDIO_MAXIMUM_OPEN_NOTES; )
	{
		char *end = strchr(line, '\n');

		if (end)
			*end = 0;
		if (strstr(line, "\"status\":\"open\""))
		{
			menu_studio_json_field(line, "screen_tag", menu_studio_notes.open_screens[menu_studio_notes.open_count],
				sizeof(menu_studio_notes.open_screens[0]));
			menu_studio_json_field(line, "widget", menu_studio_notes.open_widgets[menu_studio_notes.open_count],
				sizeof(menu_studio_notes.open_widgets[0]));
			menu_studio_notes.open_count++;
		}
		line = end ? end + 1 : NULL;
	}
	if (text)
		halo_menus_notes_free(text);
}

boolean menu_studio_commenting(
	void)
{
	return menu_studio_notes.state != _menu_studio_comment_off;
}

boolean menu_studio_typing(
	void)
{
	return menu_studio_notes.state == _menu_studio_comment_typing ||
		menu_studio_notes.state == _menu_studio_comment_shooting;
}

boolean menu_studio_shooting(
	void)
{
	return menu_studio_notes.state == _menu_studio_comment_shooting;
}

static void menu_studio_comment_toggle(
	void)
{
	if (menu_studio_notes.state == _menu_studio_comment_off)
	{
		menu_studio_notes_read();
		menu_studio_notes.state = _menu_studio_comment_picking;
		platform_log("menus: studio: notes to %s", menu_studio_notes.path);
	}
	else if (menu_studio_notes.state != _menu_studio_comment_shooting)
	{
		platform_menu_studio_typing(FALSE);
		menu_studio_notes.state = _menu_studio_comment_off;
	}
}

/* a widget picked (its record in the kept frame; -1: the screen): its note
typed */
static void menu_studio_comment_pick(
	long id)
{
	if (menu_studio_notes.state != _menu_studio_comment_picking)
		return;
	menu_studio_notes.picked = id;
	snprintf(menu_studio_notes.picked_name, sizeof(menu_studio_notes.picked_name), "%s", menu_layout_widget_name(id));
	menu_layout_widget_json(id, menu_studio_notes.picked_json, sizeof(menu_studio_notes.picked_json));
	menu_studio_notes.note[0] = 0;
	menu_studio_notes.length = 0;
	menu_studio_notes.state = _menu_studio_comment_typing;
	platform_menu_studio_typing(TRUE);
}

/* (ui_widget.c: a click in the menus while notes are picked, in their
coordinates) */
void menu_studio_pick_point(
	short x,
	short y)
{
	menu_studio_comment_pick(menu_layout_widget_at(x, y));
}

/* (ui_widget.c: A pressed while notes are picked) */
void menu_studio_pick_focused(
	void)
{
	menu_studio_comment_pick(menu_layout_widget_of(ui_widget_port_focused_widget()));
}

/* the note written: its line in the notes file, with its picture */
static void menu_studio_comment_write(
	char const *picture)
{
	static char line[4096];
	char screen[256], value[256];
	char const *tag = ui_widget_port_screen_name();
	time_t now = time(NULL);
	struct tm const *when;
	long index;

	snprintf(screen, sizeof(screen), "%s", strncmp(tag, "pc\\", 3) ? tag : tag + 3);
	for (index = 0; screen[index]; index++)
	{
		if (screen[index] == '\\')
			screen[index] = '/';
	}
	when = gmtime(&now);
	snprintf(line, sizeof(line), "{\"id\":\"%08lx%03lx\",\"status\":\"open\",\"time\":\"", (unsigned long)now,
		(unsigned long)(system_milliseconds() % 4096));
	value[0] = 0;
	if (when)
		strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", when);
	strncat(line, value, sizeof(line) - strlen(line) - 1);
	strncat(line, "\",\"note\":\"", sizeof(line) - strlen(line) - 1);
	menu_studio_json_string(line, sizeof(line), menu_studio_notes.note);
	strncat(line, "\",\"screen\":\"", sizeof(line) - strlen(line) - 1);
	menu_studio_json_string(line, sizeof(line), screen);
	strncat(line, "\",\"screen_tag\":\"", sizeof(line) - strlen(line) - 1);
	menu_studio_json_string(line, sizeof(line), tag);
	strncat(line, "\",", sizeof(line) - strlen(line) - 1);
	strncat(line, menu_studio_notes.picked_json, sizeof(line) - strlen(line) - 1);
	snprintf(value, sizeof(value), ",\"input\":\"%s\",\"shape\":\"%s\",\"screen_width\":%ld,\"picture\":",
		menu_studio_input_names[menu_studio.input], menu_studio_shapes[menu_studio.shape].name, halo_screen_width());
	strncat(line, value, sizeof(line) - strlen(line) - 1);
	if (picture[0])
	{
		strncat(line, "\"", sizeof(line) - strlen(line) - 1);
		menu_studio_json_string(line, sizeof(line), menu_studio_notes.pictures);
		strncat(line, "/", sizeof(line) - strlen(line) - 1);
		menu_studio_json_string(line, sizeof(line), picture);
		strncat(line, "\"}", sizeof(line) - strlen(line) - 1);
	}
	else
	{
		strncat(line, "null}", sizeof(line) - strlen(line) - 1);
	}
	if (halo_menus_note_append(menu_studio_notes.path, line))
	{
		menu_studio_notes.saved++;
		platform_log("menus: studio: note saved to %s", menu_studio_notes.path);
	}
	else
	{
		platform_log("menus: studio: the note could not be written to %s", menu_studio_notes.path);
	}
	menu_studio_notes_read();
}

static void menu_studio_comment_update(
	void)
{
	char typed[64], picture[128];
	int count, index;

	if (menu_studio_notes.state == _menu_studio_comment_shooting)
	{
		/* (the frame with the widget outlined alone, saved) */
		if (halo_frames_unseen() || halo_screenshot_png_taken(picture, sizeof(picture)))
		{
			if (halo_frames_unseen())
				picture[0] = 0;
			menu_studio_comment_write(picture);
			menu_studio_notes.state = _menu_studio_comment_picking;
		}
		return;
	}
	if (menu_studio_notes.state != _menu_studio_comment_typing)
		return;
	count = platform_menu_studio_typed(typed, sizeof(typed));
	for (index = 0; index < count; index++)
	{
		char character = typed[index];

		if (character == '\b')
		{
			if (menu_studio_notes.length)
				menu_studio_notes.note[--menu_studio_notes.length] = 0;
		}
		else if (character == '\033')
		{
			platform_menu_studio_typing(FALSE);
			menu_studio_notes.state = _menu_studio_comment_picking;
			return;
		}
		else if (character == '\n')
		{
			platform_menu_studio_typing(FALSE);
			if (!menu_studio_notes.length)
			{
				menu_studio_notes.state = _menu_studio_comment_picking;
				return;
			}
			menu_studio_notes.state = _menu_studio_comment_shooting;
			halo_screenshot_png(menu_studio_notes.pictures);
			return;
		}
		else if ((unsigned char)character >= 0x20 && menu_studio_notes.length < MENU_STUDIO_NOTE_LENGTH - 1)
		{
			menu_studio_notes.note[menu_studio_notes.length++] = character;
			menu_studio_notes.note[menu_studio_notes.length] = 0;
		}
	}
}

/* the notes' outlines: the widget being noted, and a mark on each widget of
the screen with an open note (ui_widget.c's overlay) */
void menu_studio_notes_overlay(
	menu_layout_outline_proc outline)
{
	char const *tag = ui_widget_port_screen_name();
	rectangle2d box;
	long index;

	if (menu_studio_notes.state == _menu_studio_comment_typing || menu_studio_notes.state == _menu_studio_comment_shooting)
	{
		if (menu_layout_widget_box(menu_layout_widget_named(menu_studio_notes.picked_name), &box))
			outline(&box, _menu_layout_picked);
	}
	if (menu_studio_notes.state == _menu_studio_comment_shooting)
		return;
	if (!menu_studio_notes.read && menu_studio.on)
		menu_studio_notes_read();
	for (index = 0; index < menu_studio_notes.open_count; index++)
	{
		if (strcmp(menu_studio_notes.open_screens[index], tag) ||
			!menu_layout_widget_box(menu_layout_widget_named(menu_studio_notes.open_widgets[index]), &box))
		{
			continue;
		}
		box.x0 = (short)MAX(box.x0, box.x1 - 10);
		box.y1 = (short)MIN(box.y1, box.y0 + 10);
		outline(&box, _menu_layout_note);
	}
}

/* the line under the studio's while a note is typed: what it is on, and
what it says */
boolean menu_studio_note_field(
	wchar_t *line,
	long size)
{
	char text[320];
	char const *name = menu_studio_notes.picked_name;
	char const *last = strrchr(name, '\\');
	long index;

	if (menu_studio_notes.state != _menu_studio_comment_typing)
		return FALSE;
	snprintf(text, sizeof(text), "NOTE ON %s:  %s_      (Enter saves, Esc cancels)",
		menu_studio_notes.picked >= 0 ? (last ? last + 1 : name) : "THE SCREEN", menu_studio_notes.note);
	for (index = 0; text[index] && index < size - 1; index++)
		line[index] = (unsigned char)text[index];
	line[index] = 0;

	return TRUE;
}

/* (each frame of the menus, as they update: ui_widget.c) */
void menu_studio_update(
	void)
{
	int keys;

	if (!menu_studio.started)
	{
		menu_studio.started = TRUE;
		menu_studio.on = menu_studio_on();
		if (menu_studio.on)
		{
			menu_studio.stamp = halo_menus_files_stamp();
			platform_log("menus: studio: F3 input, F4 shape, F5 long labels, F6 boxes, F7 reload");
		}
	}
	keys = platform_menu_studio_keys();
	if (keys && menu_studio_keys_work())
	{
		if (keys & (1 << 0))
		{
			menu_studio.input = (short)((menu_studio.input + 1) % NUMBER_OF_MENU_STUDIO_INPUTS);
			menu_studio_input_apply();
		}
		if (keys & (1 << 1))
		{
			menu_studio.shape = (short)((menu_studio.shape + 1) % NUMBEROF(menu_studio_shapes));
			config_set_for_run("display.window_size", menu_studio_shapes[menu_studio.shape].size);
			platform_display_apply();
		}
		if (keys & (1 << 2))
			menu_studio.long_text = !menu_studio.long_text;
		if (keys & (1 << 3))
			menu_studio.overlay = !menu_studio.overlay;
		if (keys & (1 << 4))
			menu_studio_reload("F7");
		if (keys & (1 << 6))
			menu_studio_comment_toggle();
	}
	menu_layout_keep(menu_studio.overlay || menu_studio.on || menu_studio_notes.state != _menu_studio_comment_off);
	menu_studio_comment_update();
	if (menu_studio.on && ++menu_studio.frame % MENU_STUDIO_WATCH_FRAMES == 0 &&
		halo_menus_files_stamp() != menu_studio.stamp)
	{
		menu_studio_reload("its files changed");
	}
	/* (a reload's first frame drawn, saved: its time from the files'
	change to the screen, measured) */
	if (menu_studio.shot_in && !--menu_studio.shot_in && config_string("debug.screenshot_directory")[0])
	{
		char name[32];

		snprintf(name, sizeof(name), "studio-%ld", menu_studio.reloads);
		halo_screenshot_named(name);
	}
}

/* (render_ui_widgets: the menus being drawn, not the studio's own line) */
void menu_studio_drawing(
	boolean drawing)
{
	menu_studio.drawing = drawing;
}

boolean menu_studio_boxes_on(
	void)
{
	return menu_studio.overlay;
}

boolean menu_studio_overlay_on(
	void)
{
	return menu_studio.overlay || menu_studio.on || menu_studio_notes.state != _menu_studio_comment_off;
}

/* a label the menus draw, made longer by half where F5 asks: each word's
first letters again after it (rasterizer_text.c) */
wchar_t const *menu_studio_text(
	wchar_t const *text)
{
	static wchar_t longer[1024];
	long in, out = 0, word = 0;

	if (!menu_studio.long_text || !menu_studio.drawing || !text)
		return text;
	for (in = 0; text[in] && out < (long)NUMBEROF(longer) - 1; in++)
	{
		boolean letter = text[in] != ' ' && text[in] != '\r' && text[in] != '\n' && text[in] != '%' &&
			text[in] != '\t';

		if (!letter && word)
		{
			long extra, start = in - word;

			for (extra = 0; extra < (word + 1) / 2 && out < (long)NUMBEROF(longer) - 1; extra++)
				longer[out++] = text[start + extra];
			word = 0;
		}
		/* (an icon's %name stays as it is) */
		if (text[in] == '%')
		{
			while (text[in] && text[in] != ' ' && out < (long)NUMBEROF(longer) - 1)
				longer[out++] = text[in++];
			in--;
			continue;
		}
		word = letter ? word + 1 : 0;
		longer[out++] = text[in];
	}
	if (word)
	{
		long extra, start = in - word;

		for (extra = 0; extra < (word + 1) / 2 && out < (long)NUMBEROF(longer) - 1; extra++)
			longer[out++] = text[start + extra];
	}
	longer[out] = 0;

	return longer;
}

/* the studio's line: what it draws the menus for, and the problems */
void menu_studio_status(
	wchar_t *line,
	long size,
	long problems)
{
	char text[320];
	long index;
	long here = 0;

	for (index = 0; index < menu_studio_notes.open_count; index++)
		here += !strcmp(menu_studio_notes.open_screens[index], ui_widget_port_screen_name());
	if (menu_studio_notes.state != _menu_studio_comment_off)
	{
		snprintf(text, sizeof(text), "NOTES  click a widget or press A on it (none: the screen); F9 leaves  |  "
			"open: %ld here, %ld in all%s", here, menu_studio_notes.open_count, menu_studio_notes.saved ? "  |  saved" : "");
	}
	else
	{
		snprintf(text, sizeof(text), "STUDIO  %s  %s%s  |  F3 input  F4 shape  F5 long  F6 boxes  F7 reload  F9 notes"
			" (%ld)%s", menu_studio_input_names[menu_studio.input], menu_studio_shapes[menu_studio.shape].name,
			menu_studio.long_text ? "  long labels" : "", menu_studio_notes.open_count,
			menu_studio.overlay ? (problems ? "  |  problems: " : "  |  no problems") : "");
	}
	for (index = 0; text[index] && index < size - 16; index++)
		line[index] = (unsigned char)text[index];
	if (menu_studio.overlay && problems)
	{
		char count[16];
		long digit;

		snprintf(count, sizeof(count), "%ld", problems);
		for (digit = 0; count[digit] && index < size - 1; digit++)
			line[index++] = count[digit];
	}
	line[index] = 0;
}
