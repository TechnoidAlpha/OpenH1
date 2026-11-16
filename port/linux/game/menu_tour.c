/*
MENU_TOUR.C

debug.menu_tour: one run of the game walks the menu screens a list names
(a file in the data root, a screen a line, as port/assets/menus names it:
main_menu/settings_select/..., and after it, a tab before each, the event
handler functions the game runs as it opens the screen: menu_tour_setup),
for tools/menu_gallery.py. Once the menus are up, each screen in turn is
opened alone (the others closed, a player profile being edited and the
line's functions run, as debug.menu_open opens one), the first at the same
game time in every run (MENU_TOUR_START_TICKS), shown for
debug.menu_tour_frames frames, and then its layout written
(menu_layout.c: tour/<n>.json in the data root, n its line, from 0) and,
where frames are drawn, its picture saved (debug.screenshot_directory's
tour-<n>.bmp); after the last the game quits. A screen that cannot be
opened is logged and has neither. Its run is its own: the screens are
opened as no player could open them, one over the state another left.

And a scripted run of the menus (debug.test_input "menu:ready ...",
tools/menu_parity.py's behaviour scripts): its presses counted from when
the screen opened is up, and where the focus is after each logged
("menus: focus <second>: <screen>: <widgets>"), for two runs' to be
compared; past its last, the game quits.
*/

#include "cseries.h"
#include "game/game.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "interface/ui_widget_event_handler_functions.h"
#include "interface/terminal.h"
#include "interface/virtual_keyboard.h"
#include "menu_layout.h"
#include "menu_widget.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int config_integer(char const *name);
char const *config_string(char const *name);
void platform_log(char const *format, ...);
void halo_screenshot_named(const char *name);
extern boolean pc_menu_profile_edit_begin(void);
void platform_test_menu_start(void);
int platform_test_menu_waiting(void);
int platform_test_menu_step(void);
int platform_test_menu_done(void);
char const *ui_widget_port_screen_name(void);
long pc_menu_event_function_index(char const *name);
void ui_widget_port_pointer_forget(void);
void const *ui_widget_port_focused_widget(void);

#define MENU_TOUR_MAXIMUM_SCREENS 512
/* the game time (ticks) the first screen opens at: every run's alike, past
the menus' load, however many frames that took, so that the scene behind
the menus is where it was in another run (tools/menu_parity.py compares
two runs' pictures, pixel for pixel) */
#define MENU_TOUR_START_TICKS 150
#define MENU_TOUR_NAME_LENGTH 256

static struct
{
	int read;
	long count;
	long index;
	long frame;
	char (*names)[MENU_TOUR_NAME_LENGTH];
} menu_tour;

/* the list's screens, read once; FALSE for no tour */
static boolean menu_tour_read(
	void)
{
	char const *path;
	char line[MENU_TOUR_NAME_LENGTH];
	FILE *file;

	if (menu_tour.read)
		return menu_tour.count > 0;
	menu_tour.read = 1;
	path = config_string("debug.menu_tour");
	if (!path[0])
		return FALSE;
	file = fopen(path, "r");
	if (!file)
	{
		platform_log("menus: debug.menu_tour: cannot read %s", path);
		return FALSE;
	}
	menu_tour.names = malloc(MENU_TOUR_MAXIMUM_SCREENS * sizeof(*menu_tour.names));
	while (menu_tour.names && menu_tour.count < MENU_TOUR_MAXIMUM_SCREENS && fgets(line, sizeof(line), file))
	{
		size_t length = strcspn(line, "\r\n");

		line[length] = 0;
		if (length)
			snprintf(menu_tour.names[menu_tour.count++], MENU_TOUR_NAME_LENGTH, "%s", line);
	}
	fclose(file);
	platform_log("menus: debug.menu_tour: %ld screens", menu_tour.count);
	return menu_tour.count > 0;
}

/* the event handler functions the game runs as it opens a screen, before
it does (a tab before each: a tour's line's, or debug.menu_open's, after
the screen's name), run as the first player's press runs them: a new
gametype made and edited ("create&edit playlist profile"), the keyboard
launched on a name ("player profile change name"), the screens that the
game shows only then, and that close at once without it ("close if not
editing profile"). "close keyboard" closes the keyboard one of them left
up, the name as it was, as a player's Cancel does */
void menu_tour_setup(
	char const *functions)
{
	char function[MENU_TOUR_NAME_LENGTH];
	struct widget_instance widget;
	struct event_record event;
	boolean deleted;
	long index;

	while (functions && *functions == '\t')
	{
		size_t length = strcspn(++functions, "\t");

		snprintf(function, sizeof(function), "%.*s", (int)length, functions);
		functions += length;
		if (!strcmp(function, "close keyboard"))
		{
			if (virtual_keyboard_active())
				virtual_keyboard_close();
			continue;
		}
		index = pc_menu_event_function_index(function);
		if (index == NONE)
		{
			platform_log("menus: menu tour: there is no event handler function \"%s\"", function);
			continue;
		}
		/* (a widget of the first player's menus, and its press) */
		memset(&widget, 0, sizeof(widget));
		widget.definition_tag_index = NONE;
		widget.name = "menu tour";
		widget.local_player_index = 0;
		memset(&event, 0, sizeof(event));
		event.controller_index = 0;
		deleted = FALSE;
		if (!ui_widget_event_handler_function_invoke(&widget, &event, (word)index, &deleted))
			platform_log("menus: menu tour: \"%s\" failed", function);
	}
}

/* the screen of the menu files' name opened alone, its line's functions run
first; FALSE where it is not one of theirs */
static boolean menu_tour_open(
	char const *line)
{
	char tag_name[MENU_TOUR_NAME_LENGTH + 8];
	size_t length = strcspn(line, "\t");
	long index;

	snprintf(tag_name, sizeof(tag_name), "pc\\%.*s", (int)length, line);
	for (index = 0; tag_name[index]; index++)
	{
		if (tag_name[index] == '/')
			tag_name[index] = '\\';
	}
	if (virtual_keyboard_active())
		virtual_keyboard_close();
	/* (what the screen before printed on the screen, its warnings, not on
	this one's picture) */
	terminal_clear();
	ui_widgets_close_all();
	ui_widget_port_pointer_forget();
	pc_menu_profile_edit_begin();
	menu_tour_setup(line + length);
	return ui_widget_load_by_name_or_tag(tag_name, NONE, NULL, NONE, NONE, NONE, NONE) != NULL;
}

/* the frames a scripted run's screen is shown before its presses are
counted (its fade in over) */
#define MENU_SCRIPT_SETTLE_FRAMES 30

/* a widget's name without its folders */
static char const *menu_script_leaf(
	char const *name)
{
	char const *leaf = name ? strrchr(name, '\\') : NULL;

	return leaf ? leaf + 1 : name ? name : "?";
}

/* debug.test_input "menu:ready ...": its clock started, the screen up; and
the focus logged at the end of each second (platform_test_menu_step's),
where its press left it */
static void menu_script_update(
	void)
{
	static long frames;
	static int logged = -1;
	struct widget_instance const *focused;
	struct widget_instance const *widgets[16];
	char path[1024];
	size_t used = 0;
	int step;
	int count = 0;

	if (platform_test_menu_waiting())
	{
		/* (a name typed in: the game's keyboard, over no screen) */
		if (game_time_get() < MENU_TOUR_START_TICKS ||
			(!ui_widget_port_screen_name()[0] && !virtual_keyboard_active()))
			return;
		if (++frames < MENU_SCRIPT_SETTLE_FRAMES)
			return;
		platform_test_menu_start();
	}
	step = platform_test_menu_step();
	if (step < 0 || step == logged)
		return;
	logged = step;
	for (focused = ui_widget_port_focused_widget(); focused && count < (int)NUMBEROF(widgets);
		focused = focused->parent)
		widgets[count++] = focused;
	path[0] = 0;
	while (count-- > 0 && used + 1 < sizeof(path))
	{
		int wrote = snprintf(path + used, sizeof(path) - used, "%s%s", used ? "/" : "",
			menu_script_leaf(widgets[count]->name));

		if (wrote < 0)
			break;
		used += (size_t)wrote;
		if (used >= sizeof(path))
			used = sizeof(path) - 1;
	}
	platform_log("menus: focus %d: %s: %s%s", step, menu_script_leaf(ui_widget_port_screen_name()), path,
		virtual_keyboard_active() ? " (the keyboard)" : "");
	if (platform_test_menu_done())
	{
		platform_log("menus: debug.test_input: done");
		exit(EXIT_SUCCESS);
	}
}

/* (each frame of the menus, as they update: ui_widget.c) */
void menu_tour_update(
	void)
{
	long frames;
	char name[64];

	menu_script_update();
	if (!menu_tour_read())
		return;
	frames = config_integer("debug.menu_tour_frames");
	if (frames < 1)
		frames = 1;
	if (menu_tour.index >= menu_tour.count)
	{
		/* (the last picture saved as the frame before was presented) */
		platform_log("menus: debug.menu_tour: done");
		exit(EXIT_SUCCESS);
	}
	if (menu_tour.index == 0 && menu_tour.frame == 0 && game_time_get() < MENU_TOUR_START_TICKS)
		return;
	if (menu_tour.frame == 0 && !menu_tour_open(menu_tour.names[menu_tour.index]))
	{
		platform_log("menus: debug.menu_tour: cannot open %s", menu_tour.names[menu_tour.index]);
		menu_tour.index++;
		return;
	}
	menu_tour.frame++;
	if (menu_tour.frame == frames)
	{
		/* (this frame's drawing: its layout and its picture) */
		snprintf(name, sizeof(name), "tour/%ld.json", menu_tour.index);
		menu_layout_request(name);
		snprintf(name, sizeof(name), "tour-%ld", menu_tour.index);
		halo_screenshot_named(name);
	}
	else if (menu_tour.frame > frames)
	{
		/* (a screen that drew no menus, under the keyboard: none) */
		menu_layout_request(NULL);
		menu_tour.frame = 0;
		menu_tour.index++;
	}
}
