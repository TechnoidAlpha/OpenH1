/*
MENU_FUNCTIONS.C

The event handler functions the menus written in XML can run besides the
game's (menu_tags.c names them; ui_widget_event_handler_function_invoke
calls them from PC_MENU_FUNCTION_BASE on):

- "port quit game" (and the PC version's "main menu quit game") quits, as
  closing the window does;
- the PC version's "profile set edit begin" begins editing the first player
  profile, as its settings screens need, and fails with none: the main
  menu's then asks for a new one's name, as the menu's other items do, and
  its handlers open the screens that make the profile ("new campaign
  decision", after which Settings' is the one being edited);
- "gamespy screen init" hides the server browser's error and filter panels,
  and the title of the mode "mp type set mode" did not choose (Internet or
  LAN);
- its buttons' events: "mouse emit back event" and "mouse emit x event" push
  B and X (as the mouse does), "emit custom activation event" (an OK
  button) runs the screen's custom activation handler, "single prev cl item
  activated" (an item chosen) its list's, and its back handlers go back;
- "port setting load", on a spinner's created event, shows the value its
  setting has (or the nearest of its values): config.toml's, or for
  "profile.<field>" the controller's in the profile being edited;
- "port setting save", on its deleted event, writes the value shown back,
  if the player changed it;
- the settings screens' (tools/port_settings.py): "port settings save"
  (OK) writes those changed and applies the window's (the rest follow
  config.toml themselves), "port settings defaults" shows the defaults,
  "port settings help" the help of the row chosen (and Video Setup's
  Resolution or Window Size, for the display mode shown, Graphics' rows
  at the values of the preset chosen, display.graphics, and its SIMULATION
  row locked at ORIGINAL (30) where game.sim_rate is set aside);
- Controls Setup's: the keyboard and mouse's controls, two bindings each,
  shown a group at a time ("controls update menu"); "controls begin
  binding" takes the next key or button pressed for the binding left and
  right choose ("controls binding slot"), or the pointer is over or clicked
  (ui_widget_port_take_point, take_click); OK ("controls screen change set")
  writes them, "controls screen defaults" shows the defaults.

A spinner's values are its strings' (setting= and values= in its <widget>,
menu_tags.c's pc_menu_setting).

The PC version's campaign menus, on player 1's profile (the active one, else
the one last used, else the first) and the game's saved game in it:
- "campaign menu continue" goes on with the saved game;
- New Game's list of levels ("initialize sp level list solo", "solo map
  list update", "solo level set map") has those the profile has reached, and
  marks those it has finished on Normal, Heroic and Legendary, as the
  Xbox's list does; a level with the saved game in it goes on with it, at
  its difficulty. Its first row's chooser shows MULTIPLAYER's maps
  instead, one chosen played alone at once;
- the difficulty menu: "difficulty item select" (a difficulty chosen) and
  "set difficulty" (its OK button: the difficulty shown) start the game;
- Load Game's list ("load game menu init", "load game list update", "load
  game menu activated") has each profile's saved game, and "load game menu
  delete request" and "delete finish" delete one.
The Xbox's functions of those names take the Xbox's widgets (a spinner of
levels, the difficulty list itself), so ours run instead (menu_tags.c).

Co-op (the Xbox's Cooperative Play, Multiplayer's CO-OP CAMPAIGN): "port coop
begin" makes two players, player 1 on its profile; "port coop player 2"
gives player 2 the profile chosen and the controller that chose it; then the
campaign's New Game and difficulty start the game for both.

The update question (the self-updater's, port/linux/src/updater.c, on the
Steam Frame and under gamescope, where it can show no system dialog):
"port update question", the main menu's QUIT item's game data input, notes
that the main menu is shown; pc_menu_update_question_open, from the updater
between frames, then opens main_menu/update/update_screen over it, whose
UPDATE runs "port update now" (updater_update_now: on the Frame, the game
quits, and its launcher installs the update; under gamescope, the game puts
the update in place and starts it in its own process).

The rest are the PC version's, which its menus (port/assets/menus/ce)
name and the Xbox's has not: they do nothing yet, and succeed, so that what
their handlers open opens.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "interface/ui_widget_event_handler_functions.h"
#include "main/main.h"
#include "networking/network_game_manager.h"
#include "saved games/player_profile.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "text/unicode.h"

#include "halo_menus.h"
#include "controller_buttons.h"
#include "controller_remap.h"
#include "custom_edition_cache.h"
#include "custom_edition_maps.h"
#include "game_modes.h"
#include "network_voice.h"
#include "network_chat.h"
#include "network_host_controls.h"
#include "network_admin.h"
#include "network_host_rules.h"
#include "network_host_game.h"
#include "sim_rate.h"
#include "network_standard.h"
#include "bots.h"
#include "network_map_vote.h"
#include "text/draw_string.h"
#include "halo_keyboard.h"
/* (internet play's server browser: the platform layer's) */
#include "../src/p2p.h"
#include "../src/browser_favorites.h"
/* (the server browser's master servers, and Server Setup's publisher key) */
#include "../src/master_list.h"
#include "../src/player_link.h" /* port: port/linux/src/player_link.c */
#include "../src/server_master.h"
#include "text_filter.h"
/* (the party: party_menu.inc) */
#include "party.h"
/* (matchmaking: matchmaking_menu.inc, matchmaking.c) */
#include "matchmaking.h"
/* (the lobby's bots: lobby_menu.inc) */
#include "bots.h"
#include "basic_training.h"
#include "bungie_net/network/transport.h"

#include <stdlib.h>
#include <string.h>
#include <xtl.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
void platform_request_quit(void);
char const *pc_menu_function_name(long function_index);
char const *pc_menu_game_data_input_name(long function_index);
void event_manager_post_button(short controller_index, short button_index);
int config_text(char const *name, char *text, unsigned int size);
int config_write(char const *name, char const *value);
int config_boolean(char const *name);
long config_integer(char const *name);
int config_default(char const *name, char *text, unsigned int size);
int config_graphics_value(char const *graphics, char const *name, char *text, unsigned int size);
char const *config_graphics_setting(int entry);
long pc_menu_widget_string_list(long definition_index);
boolean ui_widget_port_switch(struct widget_instance *widget, char const *name, boolean *widget_deleted);
char const *config_string(char const *name);
void platform_display_apply(void);
void platform_binding_capture_begin(void);
int platform_binding_capture_poll(int *input);
void halo_input_name(int input, char *name, unsigned int size);
void halo_input_shown_name(char const *binding, char *shown, unsigned int size);
short pc_menu_string_index(long definition_index);
void updater_update_now(void);
/* main.c's: this build's number, configuration, repository and label */
void main_build_identity(long *number, char *flavor, size_t flavor_size, char *repository, size_t repository_size,
	char *version, size_t version_size);
/* cseries_windows.c's */
unsigned long system_milliseconds(void);

/* the game's (port) */
boolean ui_widget_port_dispatch_event(struct widget_instance *widget, short event_type, short controller_index,
	boolean *deleted);
void ui_widget_port_go_back(struct widget_instance *widget);
short ui_widget_port_list_index(struct widget_instance *list_widget);
long ui_widget_port_text_box_font(struct widget_instance *widget, short *width);
boolean ui_widget_port_saved_game(char const **map_name, short *level, short *difficulty);
char const *ui_widget_event_handler_function_name(long function_index);
short main_get_solo_level_from_name(char const *name);
boolean player_name_clean(wchar_t *name, long count);
short players_port_local_player_count(void);

boolean pc_menu_event_function_invoke(struct widget_instance *widget, struct event_record *event,
	long function_index, boolean *widget_deleted);
void pc_menu_game_data_function_invoke(struct widget_instance *widget, long function);

/* ---------- constants */

#define MAXIMUM_STRINGS 64
/* the PC version's custom activation event (this engine never sends it) */
#define EVENT_CUSTOM_ACTIVATION 32
#define BUTTON_A 0
#define BUTTON_B 1
#define BUTTON_X 2
#define BUTTON_Y 3
#define BUTTON_START 12

enum
{
	_pc_menu_function_quit_game,
	_pc_menu_function_setting_load,
	_pc_menu_function_setting_save,
	NUMBER_OF_PC_MENU_FUNCTIONS
};

/* ---------- structures */

/* a widget, as ui_widget.c has it */
struct widget_instance
{
	long definition_tag_index;
	char const *name;
	short local_player_index;
	short horizontal_offset;
	short vertical_offset;
	short type;
	boolean visible;
	boolean render_regardless_of_controller_index;
	boolean disabled;
	boolean pause_game_time;
	boolean delete_recursion_lock;
	boolean widget_is_error_dialog;
	boolean close_if_local_player_controller_present;
	byte pad17;
	long creation_time;
	unsigned long milliseconds_to_auto_close;
	unsigned long auto_close_fade_time;
	real alpha_modifier;
	struct widget_instance *previous;
	struct widget_instance *next;
	struct widget_instance *parent;
	struct widget_instance *child;
	struct widget_instance *focused_child;
	union
	{
		struct
		{
			wchar_t *text;
			short string_list_index;
		} text_box;
		struct
		{
			short selected_index;
			short last_list_tab_direction;
			void *list_items;
			word number_of_items;
			struct widget_instance *extended_description;
			wchar_t *item_text;
		} list;
	} parameters;
	struct
	{
		short current_frame_index;
		short first_frame_index;
		short last_frame_index;
		short number_of_sprite_frames;
	} animation;
};

typedef char verify_widget_instance_size[
	sizeof(struct widget_instance) == 0x58 ? 1 : -1];
typedef char verify_widget_instance_child_offset[
	offsetof(struct widget_instance, child) == 0x34 ? 1 : -1];
typedef char verify_widget_instance_parent_offset[
	offsetof(struct widget_instance, parent) == 0x30 ? 1 : -1];
typedef char verify_widget_instance_selected_index_offset[
	offsetof(struct widget_instance, parameters.list.selected_index) == 0x3C ? 1 : -1];
typedef char verify_widget_instance_number_of_items_offset[
	offsetof(struct widget_instance, parameters.list.number_of_items) == 0x44 ? 1 : -1];
typedef char verify_widget_instance_animation_offset[
	offsetof(struct widget_instance, animation) == 0x50 ? 1 : -1];

/* menu_tags.c's */
struct pc_menu_setting
{
	long definition_index;
	char const *setting;
	long value_count;
	char const *values[MAXIMUM_STRINGS];
	short loaded_index;
};

struct pc_menu_setting *pc_menu_setting_get(long definition_index);
/* sdl_platform.c's: whether fullscreen can set the display's refresh rate */
int platform_refresh_rates_settable(void);
/* the menus' pointer, their latest click on an item and the latest place
the pointer chose one at (ui_widget.c) */
boolean ui_widget_port_pointer(short *x);
boolean ui_widget_port_take_click(short *x, short *y);
boolean ui_widget_port_take_point(short *x, short *y);

/* ---------- private code */

static boolean campaign_profile(short controller, struct player_profile *profile);
/* (a text field: Settings' sliders' numbers, below) */
static boolean text_field_editing(struct widget_instance *row);
static void text_field_end(boolean keep);
static void settings_sliders_show(struct widget_instance *list);
static void settings_sliders_default(struct widget_instance *screen);
static boolean settings_slider_function(char const *name, struct widget_instance *row);
static short settings_slider_of(struct widget_instance const *row);
static double settings_slider_value(short slider);
static void settings_slider_text(short slider, double value, char *text, unsigned int size);
static short settings_slider_stop_nearest(short slider);

static boolean text_is_number(char const *text, double *number)
{
	char *end;

	*number = strtod(text, &end);
	return *text && !*end;
}

/* whether the widget is one of a spinner's items, which the game makes of
its strings from its own definition, handlers and all (ui_widget.c,
ui_widget_load_children_recursive) */
static boolean spinner_item(struct widget_instance const *widget)
{
	return widget->parent && widget->parent->definition_tag_index == widget->definition_tag_index;
}

/* the value shown for the setting's: the same one, else the nearest number */
static short setting_value_index(struct pc_menu_setting const *setting, char const *current)
{
	double current_number, value_number, distance = 0.0;
	short index, nearest = 0;

	for (index = 0; index < setting->value_count; index++)
	{
		if (!_stricmp(setting->values[index], current))
			return index;
	}
	if (!text_is_number(current, &current_number))
		return 0;
	for (index = 0; index < setting->value_count; index++)
	{
		if (text_is_number(setting->values[index], &value_number))
		{
			double gap = value_number > current_number ? value_number - current_number : current_number - value_number;

			if (index == 0 || gap < distance)
			{
				distance = gap;
				nearest = index;
			}
		}
	}
	return nearest;
}

/* the profile's settings ("profile.<field>"): those of the controller in
the profile being edited */
static struct
{
	char const *name;
	short field;
	/* its default (player_profile.c's new profile's) */
	byte default_value;
} const profile_settings[] =
{
	{ "profile.look_sensitivity", 0, 3 },
	{ "profile.invert_look", 1, FALSE },
	{ "profile.flight_inversion", 2, FALSE },
	{ "profile.autocenter", 3, FALSE },
	{ "profile.button_preset", 4, 0 },
	{ "profile.joystick_preset", 5, 0 },
	{ "profile.vibration", 6, TRUE },
	{ "profile.ingame_help", 7, TRUE },
};

static byte *profile_setting_field(struct player_profile *profile, short field)
{
	struct player_profile_controller_settings *controls = &profile->controller_settings;

	switch (field)
	{
	case 0: return &controls->look_sensitivity;
	case 1: return (byte *)&controls->invert_look;
	case 2: return (byte *)&controls->flight_stick_aircraft_controls;
	case 3: return (byte *)&controls->autocenter;
	case 4: return &controls->button_preset;
	case 5: return &controls->joystick_preset;
	case 6: return (byte *)&controls->vibration_disabled;
	case 7: return (byte *)&controls->ingame_help_disabled;
	}
	return NULL;
}

static boolean profile_setting_boolean(short field)
{
	return field != 0 && field != 4 && field != 5;
}

/* a setting's value as text, or (default_value) its default: config.toml's,
or the profile's */
static boolean setting_text(char const *name, char *text, unsigned int size, boolean default_value)
{
	short index;

	if (strncmp(name, "profile.", 8))
	{
		if (!(default_value ? config_default(name, text, size) : config_text(name, text, size)))
			return FALSE;
		/* (display.mode empty: display.fullscreen's, as the window has it:
		sdl_platform.c) */
		if (!strcmp(name, "display.mode") && !text[0])
			snprintf(text, size, "%s", default_value || config_boolean("display.fullscreen") ? "borderless" : "windowed");
		/* (display.window_size empty: 640x480 times display.window_scale, as
		older versions set it) */
		if (!strcmp(name, "display.window_size") && !text[0])
		{
			char scale_text[32] = "";
			long scale;

			if (default_value)
				config_default("display.window_scale", scale_text, sizeof(scale_text));
			else
				config_text("display.window_scale", scale_text, sizeof(scale_text));
			scale = atol(scale_text);
			if (scale < 1)
				scale = 1;
			snprintf(text, size, "%ldx%ld", 640 * scale, 480 * scale);
		}
		return TRUE;
	}
	for (index = 0; index < NUMBEROF(profile_settings); index++)
	{
		struct player_profile *profile = player_ui_get_edit_player_profile();
		short field = profile_settings[index].field;
		long value;

		if (strcmp(name, profile_settings[index].name))
			continue;
		if (default_value)
			value = profile_settings[index].default_value;
		else if (profile && profile_setting_field(profile, field))
			value = *profile_setting_field(profile, field);
		else
			return FALSE;
		/* (vibration and help are kept as their opposites) */
		if (!default_value && (field == 6 || field == 7))
			value = !value;
		if (profile_setting_boolean(field))
			snprintf(text, size, "%s", value ? "true" : "false");
		else
			snprintf(text, size, "%ld", value);
		return TRUE;
	}
	return FALSE;
}

static boolean setting_write(char const *name, char const *value)
{
	short index;

	if (strncmp(name, "profile.", 8))
		return config_write(name, value);
	for (index = 0; index < NUMBEROF(profile_settings); index++)
	{
		struct player_profile *profile = player_ui_get_edit_player_profile();
		short field = profile_settings[index].field;
		byte *place;
		long number;

		if (strcmp(name, profile_settings[index].name))
			continue;
		if (!profile || !(place = profile_setting_field(profile, field)))
			return FALSE;
		number = profile_setting_boolean(field) ? !strcmp(value, "true") : atol(value);
		if (field == 6 || field == 7)
			number = !number;
		*place = (byte)number;
		return TRUE;
	}
	return FALSE;
}

/* Video's Quality spinner, and the value it showed, since it was made
(graphics_preset_show) */
static struct
{
	struct widget_instance *spinner;
	short shown;
} graphics_preset = { NULL, NONE };

/* ---------- Settings' changes kept (tools/port_settings.py's PAGES)

What a tab or a page of Settings shows changed is kept here, by setting,
until Settings' OK, or a page's, writes it all (settings_pending_write), or
Settings' Cancel forgets it: a tab or a page made again shows the changes
kept (setting_load), whichever of them made them. Each page notes its rows'
as they change (settings_page_update). A page's Cancel puts back what was
kept when it opened (settings_snapshot). */

#define SETTINGS_PENDING_COUNT 160

struct settings_pending
{
	char name[48];
	char value[80];
};

static struct
{
	struct settings_pending entries[SETTINGS_PENDING_COUNT];
	short count;
} settings_pending, settings_snapshot;

static short settings_pending_find(char const *name)
{
	short index;

	for (index = 0; index < settings_pending.count; index++)
	{
		if (!strcmp(settings_pending.entries[index].name, name))
			return index;
	}
	return NONE;
}

static char const *settings_pending_value(char const *name)
{
	short index = settings_pending_find(name);

	return index == NONE ? NULL : settings_pending.entries[index].value;
}

static void settings_pending_set(char const *name, char const *value)
{
	short index = settings_pending_find(name);

	if (index == NONE)
	{
		if (settings_pending.count >= SETTINGS_PENDING_COUNT || strlen(name) >= sizeof(settings_pending.entries[0].name))
			return;
		index = settings_pending.count++;
		snprintf(settings_pending.entries[index].name, sizeof(settings_pending.entries[index].name), "%s", name);
	}
	snprintf(settings_pending.entries[index].value, sizeof(settings_pending.entries[index].value), "%s", value);
}

static void settings_pending_remove(char const *name)
{
	short index = settings_pending_find(name);

	if (index == NONE)
		return;
	settings_pending.entries[index] = settings_pending.entries[--settings_pending.count];
}

static void settings_pending_clear(void)
{
	settings_pending.count = 0;
	settings_snapshot.count = 0;
}

static boolean setting_load(struct widget_instance *widget)
{
	struct pc_menu_setting *setting = pc_menu_setting_get(widget->definition_tag_index);
	char current[300];
	char const *pending;

	if (spinner_item(widget))
		return TRUE;
	if (!setting || !setting_text(setting->setting, current, sizeof(current), FALSE))
		return FALSE;
	setting->loaded_index = setting_value_index(setting, current);
	if (setting->loaded_index < (short)widget->parameters.list.number_of_items)
		widget->parameters.list.selected_index = setting->loaded_index;
	/* (Settings' change kept, shown as the page is made again) */
	pending = settings_pending_value(setting->setting);
	if (pending && setting_value_index(setting, pending) < (short)widget->parameters.list.number_of_items)
		widget->parameters.list.selected_index = setting_value_index(setting, pending);
	/* (Graphics' preset: the rows it chooses follow it from here) */
	if (!strcmp(setting->setting, "display.graphics"))
	{
		graphics_preset.spinner = widget;
		graphics_preset.shown = widget->parameters.list.selected_index;
	}
	return TRUE;
}

static boolean setting_save(struct widget_instance *widget)
{
	struct pc_menu_setting *setting = pc_menu_setting_get(widget->definition_tag_index);

	if (spinner_item(widget))
		return TRUE;
	if (!setting || setting->loaded_index == NONE || widget->parameters.list.selected_index < 0 ||
		widget->parameters.list.selected_index >= setting->value_count)
		return FALSE;
	if (widget->parameters.list.selected_index == setting->loaded_index)
		return TRUE;
	if (!config_write(setting->setting, setting->values[widget->parameters.list.selected_index]))
	{
		platform_log("menus: could not write %s to config.toml", setting->setting);
		return FALSE;
	}
	platform_log("menus: %s = %s (from the next start)", setting->setting, setting->values[widget->parameters.list.selected_index]);
	setting->loaded_index = widget->parameters.list.selected_index;
	return TRUE;
}

static short controller_of(struct widget_instance const *widget)
{
	return widget->local_player_index >= 0 && widget->local_player_index < 4 ? widget->local_player_index : 0;
}

/* the controller the event came from, else the widget's */
static short event_controller(struct widget_instance const *widget, struct event_record const *event)
{
	return event && event->controller_index >= 0 && event->controller_index < 4 ?
		event->controller_index : controller_of(widget);
}

/* runs the custom activation handler of the widget's list, or the nearest
of its parents' that has one (widget_deleted: the widget went with its
screen) */
static boolean item_activated(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	struct widget_instance *parent;

	for (parent = widget->parent; parent; parent = parent->parent)
	{
		boolean deleted;

		if (ui_widget_port_dispatch_event(parent, EVENT_CUSTOM_ACTIVATION, controller, &deleted))
		{
			if (deleted)
				*widget_deleted = TRUE;
			return TRUE;
		}
	}
	return FALSE;
}

/* whether the multiplayer menu's choice was LAN, not Internet (mp type set
mode) */
static boolean lan_mode;

/* hides the descendants of the widget with the name */
static void hide_named(struct widget_instance *widget, char const *name)
{
	struct widget_instance *child;

	for (child = widget->child; child; child = child->next)
	{
		if (!strcmp(child->name, name))
			child->visible = FALSE;
		hide_named(child, name);
	}
}

/* the server browser's panels for errors and filters are hidden until they
are wanted, and its title is Internet's or LAN's (gamespy screen init) */
static void server_browser_initialize(struct widget_instance *screen)
{
	struct widget_instance *child;

	hide_named(screen, lan_mode ? "header_internet" : "header_lan");
	/* (a widget's name is its definition's: the last part of ours) */
	for (child = screen->child; child; child = child->next)
	{
		if (!strcmp(child->name, "gamespy_error_fullscreen") || !strcmp(child->name, "filters_screen"))
		{
			child->visible = FALSE;
			if (screen->focused_child == child)
				screen->focused_child = NULL;
		}
	}
	for (child = screen->child; child && !screen->focused_child; child = child->next)
	{
		if (child->visible && child->type == 3)
			screen->focused_child = child;
	}
}

/* begins editing player 1's profile (as the campaign has it); FALSE if
there is none */
boolean pc_menu_profile_edit_begin(void)
{
	struct player_profile profile;

	if (!campaign_profile(0, &profile))
		return FALSE;
	player_ui_begin_editing_profile(player_ui_get_active_player_profile_index(0));
	return TRUE;
}

/* the game's own function of the name (as its tags name it), run for the
widget: for ours of a name of the game's, which do more than it */
static boolean game_function(char const *name, struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted)
{
	char const *function;
	long index;

	for (index = 0; (function = ui_widget_event_handler_function_name(index)) != NULL; index++)
	{
		if (!strcmp(function, name))
			return ui_widget_event_handler_function_invoke(widget, event, (word)index, widget_deleted);
	}
	return FALSE;
}

/* "profile set edit begin": Settings, the main menu's and a game's pause
menu's. With no profile, the main menu's asks for a new one's name, as the
menu's other items do ("new game if no plyr profiles"), and fails: the
screens its handler then opens make the profile and edit it
(new_profile_decision). Without the name asked, they made none, and showed
"new campaign decision" as failed. The pause menu's has no such screens,
and only fails. */
static boolean profile_edit_begin(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted)
{
	if (pc_menu_profile_edit_begin())
		return TRUE;
	/* (a press: the profile is made on its controller) */
	if (main_menu_is_active() && event && event->controller_index >= 0 && event->controller_index < 4)
		game_function("new campaign chosen", widget, event, widget_deleted);
	return FALSE;
}

/* "new campaign decision" (the screen after a new profile's name, each of
the main menu's items having its own): the game's makes the profile, if the
name was entered. Settings' then begins editing it: the PC version's screens
open Settings with nothing more run, and its screen closes at once with no
profile being edited ("close if not editing profile"). */
static boolean new_profile_decision(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted)
{
	if (!game_function("new campaign decision", widget, event, widget_deleted))
		return FALSE;
	return strcmp(widget->name, "settings_creating_profile") || pc_menu_profile_edit_begin();
}

/* ---------- the campaign */

/* the rows of its lists (main_menu/new_select/list_item_0 to 10) */
#define MAXIMUM_ROWS 11
/* its strings' and sp_levels' entries past the levels': a level not
reached, and (map_data) a saved game on the level */
#define LEVEL_UNAVAILABLE 10
#define LEVEL_IN_PROGRESS 11
#define ROW_TEXT_LENGTH 64
#define MAXIMUM_PROFILES 32
#define SOUND_ERROR 4
#define SOUND_CURSOR 1
#define SOUND_FORWARD 2
#define SOUND_BACK 3

/* (an index whose profile reads: saved_game_files.c's) */
#define PROFILE_VALID_BIT 0x80000000UL

struct campaign_level
{
	boolean available;
	/* finished on Normal, Heroic, Legendary (difficulty_options_small's
	frames 1 to 3) */
	boolean finished[3];
	/* finished on each difficulty, Easy's the first bit (the profile's
	single_player_map_flags: the campaign's cards, campaign_menu.inc) */
	byte beaten;
};

struct campaign_saved_game
{
	long profile_index;
	wchar_t profile_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];
	short level;
	short difficulty;
};

static struct
{
	struct campaign_level levels[NUMBER_OF_SINGLE_PLAYER_LEVELS];
	/* player 1's saved game's level, else NONE */
	short saved_level;
	/* the level and saved game the lists' descriptions last showed */
	short shown_level;
	short shown_saved_game;
	/* Load Game's */
	struct campaign_saved_game saved_games[MAXIMUM_ROWS];
	short saved_game_count;
	short saved_game_to_delete;
} campaign;

/* the descendant of the widget with the name (a widget's name is its
definition's: the last part of ours), the nth of them */
static struct widget_instance *descendant(struct widget_instance *widget, char const *name, long *nth)
{
	struct widget_instance *child;

	for (child = widget->child; child; child = child->next)
	{
		struct widget_instance *found;

		if (!strcmp(child->name, name) && (*nth)-- == 0)
			return child;
		found = descendant(child, name, nth);
		if (found)
			return found;
	}
	return NULL;
}

static struct widget_instance *named(struct widget_instance *widget, char const *name, long nth)
{
	return widget ? descendant(widget, name, &nth) : NULL;
}

/* a widget's left edge on the screen (its offsets and its parents', as the
menus draw them: each widget's in its parent's, and its move in the wide
layout, ui_widget_port_wide_offset) */
static short widget_screen_x(struct widget_instance *widget)
{
	short x = 0;

	for (; widget; widget = widget->parent)
		x = (short)(x + widget->horizontal_offset + ui_widget_port_wide_offset(widget->definition_tag_index));
	return x;
}

/* the text box's own text (as the server list sets its items'), in a
buffer of length characters (made the first time: a text box is always
given the same length) */
static void text_set_length(struct widget_instance *text_box, wchar_t const *text, short length)
{
	if (!text_box)
		return;
	if (!text_box->parameters.text_box.text)
	{
		text_box->parameters.text_box.text = ui_widget_realloc(NULL, length * sizeof(wchar_t),
			__FILE__, __LINE__);
	}
	if (text_box->parameters.text_box.text)
	{
		ustrncpy(text_box->parameters.text_box.text, text, length - 1);
		text_box->parameters.text_box.text[length - 1] = 0;
	}
}

static void text_set(struct widget_instance *text_box, wchar_t const *text)
{
	text_set_length(text_box, text, ROW_TEXT_LENGTH);
}

/* one of our string lists' strings, on one line (its line breaks spaces) */
static void string_get(char const *list, short index, wchar_t *text)
{
	long tag_index = tag_loaded('ustr', list);
	wchar_t const *string = tag_index != NONE ? unicode_string_list_get_string(tag_index, index) : NULL;
	long length = 0;

	for (; string && *string && length < ROW_TEXT_LENGTH - 1; string++)
	{
		wchar_t character = *string == '\r' || *string == '\n' ? ' ' : *string;

		if (character != ' ' || (length && text[length - 1] != ' '))
			text[length++] = character;
	}
	while (length && text[length - 1] == ' ')
		length--;
	text[length] = 0;
}

/* the list's row that has the focus, else NONE (its buttons) */
static short focused_row(struct widget_instance *list)
{
	struct widget_instance *child;
	short index = 0;

	for (child = list->child; child && index < MAXIMUM_ROWS; child = child->next, index++)
	{
		if (child == list->focused_child)
			return strncmp(child->name, "list_item_", 10) ? NONE : index;
	}
	return NONE;
}

static void focus_row(struct widget_instance *list, short row)
{
	struct widget_instance *child = list->child;
	short index;

	for (index = 0; child && child->next && index < row; index++)
		child = child->next;
	if (row == NONE)
	{
		/* (its buttons: the last child) */
		while (child && child->next)
			child = child->next;
	}
	if (child)
	{
		list->focused_child = child;
		list->parameters.list.selected_index = row == NONE ? 0 : row;
	}
}

/* shows the first count rows, with their texts, the arrows on the one that
has the focus, and no scroll buttons (the rows are enough) */
static void rows_update(struct widget_instance *list, short count, void (*row_text)(short row, wchar_t *text))
{
	struct widget_instance *row;
	short index = 0;

	for (row = list->child; row && index < MAXIMUM_ROWS; row = row->next, index++)
	{
		wchar_t text[ROW_TEXT_LENGTH];
		struct widget_instance *arrows = named(row, "list_item_arrows", 0);

		if (strncmp(row->name, "list_item_", 10))
			break;
		row->visible = index < count;
		if (index < count)
		{
			row_text(index, text);
			text_set(named(row, "list_item_text", 0), text);
		}
		if (arrows)
			arrows->visible = row == list->focused_child;
		if (named(row, "scroll_up_button", 0))
			named(row, "scroll_up_button", 0)->visible = FALSE;
		if (named(row, "scroll_down_button", 0))
			named(row, "scroll_down_button", 0)->visible = FALSE;
	}
}

/* player 1's profile, read again (the active one, else the one last used,
else the first), on the controller (in co-op, on the one that chose co-op:
coop_begin); FALSE if there is none */
static boolean campaign_profile(short controller, struct player_profile *profile)
{
	long profile_index = player_ui_get_active_player_profile_index(0);

	if (profile_index == NONE || !player_profile_get(profile_index, profile))
	{
		profile_index = player_ui_get_player1_last_used_profile_index();
		if (profile_index == NONE || !player_profile_get(profile_index, profile))
		{
			word count = 1;

			profile_index = NONE;
			player_profiles_enumerate_available_to_local_player_index(NONE, &count, &profile_index, FALSE);
			if ((short)count <= 0 || profile_index == NONE || !player_profile_get(profile_index, profile))
				return FALSE;
		}
	}
	player_ui_set_active_player_profile(0, profile_index, profile);
	if (player_spawn_count < 2)
		player_ui_set_single_player_local_player_controller(0, controller);
	return TRUE;
}

/* the levels the profile has reached (as the Xbox's list has them: those
it has played, the one after the last it finished, the first) and finished,
and its saved game's; in co-op, those either player's has reached and
finished, and no saved game (a game of one player's does not go on with
two: game_state.c's game_state_header_valid) */
static void campaign_levels_read(struct player_profile const *profile)
{
	char const *map_name;
	struct player_profile player2;
	short highest_level, highest_difficulty, difficulty, level, player;

	for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS; level++)
		memset(&campaign.levels[level], 0, sizeof(campaign.levels[level]));
	if (player_spawn_count >= 2)
		player_ui_get_active_player_profile(1, &player2);
	for (player = 0; player < (player_spawn_count >= 2 ? 2 : 1); player++)
	{
		struct player_profile const *reader = player == 0 ? profile : &player2;

		player_profile_get_highest_completed_solo_level((struct player_profile *)reader, &highest_level,
			&highest_difficulty);
		for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS; level++)
		{
			byte flags = reader->single_player_map_flags[level];
			short marker;

			campaign.levels[level].available |= flags || level == highest_level + 1 || level == 0;
			campaign.levels[level].beaten |= flags & 0xF;
			for (marker = 0; marker < 3; marker++)
				campaign.levels[level].finished[marker] |= (flags >> (marker + 1)) & 1;
		}
	}
	if (player_spawn_count >= 2 || !ui_widget_port_saved_game(&map_name, &campaign.saved_level, &difficulty))
		campaign.saved_level = NONE;
}

/* plays the map, at the difficulty (a saved game in it goes on: main.c's
main_new_map, if its difficulty is this one) */
static void campaign_start(char const *map_name, short difficulty, short controller)
{
	if (player_spawn_count < 2)
		player_ui_set_single_player_local_player_controller(0, controller);
	main_set_map_name(map_name);
	main_set_difficulty(difficulty);
	game_connection_set(0);
	main_menu_switch_to_single_player();
	player_ui_remember_player1_profile(TRUE);
	ui_play_audio_feedback_sound(SOUND_FORWARD);
}

static boolean campaign_fail(void)
{
	ui_play_audio_feedback_sound(SOUND_ERROR);
	return FALSE;
}

/* the description's level: its name, picture and words, and the
difficulties it has been finished on (finished: New Game's, NULL for a level
not reached), or (saved_difficulty, not NONE: Load Game's) the saved game's
difficulty */
static void level_description(struct widget_instance *description, char const *prefix, short level, boolean in_progress,
	boolean const *finished, short saved_difficulty)
{
	char name[64];
	struct widget_instance *widget;
	short marker;

	snprintf(name, sizeof(name), "%s_right_name", prefix);
	if ((widget = named(description, name, 0)) != NULL)
		widget->parameters.text_box.string_list_index = level;
	snprintf(name, sizeof(name), "%s_right_pic", prefix);
	if ((widget = named(description, name, 0)) != NULL)
		widget->animation.current_frame_index = level;
	snprintf(name, sizeof(name), "%s_right_data", prefix);
	if ((widget = named(description, name, 0)) != NULL && saved_difficulty != NONE)
	{
		wchar_t text[ROW_TEXT_LENGTH];

		string_get("pc\\main_menu\\player_profiles_select\\difficulty_names", saved_difficulty, text);
		text_set(widget, text);
	}
	else if (widget)
		widget->parameters.text_box.string_list_index = in_progress ? LEVEL_IN_PROGRESS : level;
	for (marker = 0; marker < 3; marker++)
	{
		if ((widget = named(description, "difficulty_indicator", marker)) != NULL)
		{
			widget->visible = saved_difficulty != NONE ? marker == 0 : finished && finished[marker];
			widget->animation.current_frame_index = saved_difficulty != NONE ? saved_difficulty : marker + 1;
		}
	}
}

/* player 1's profile's name (the active one, else the one used last: the
main menu clears the active ones), on the descriptions'
current_profile_name */
static void profile_name_show(struct widget_instance *description)
{
	/* (the profile read from its file at most once a second) */
	static struct player_profile profile;
	static long read_index = NONE;
	static unsigned long read_time;
	static boolean read_good;
	long index = player_ui_get_active_player_profile_index(0);

	if (!description)
		return;
	if (index != NONE)
	{
		player_ui_get_active_player_profile(0, &profile);
		read_index = NONE;
	}
	else if ((index = player_ui_get_player1_last_used_profile_index()) == NONE)
		return;
	else
	{
		/* (one that cannot be read too: tried again a second later) */
		if (index != read_index || system_milliseconds() - read_time > 1000)
		{
			read_index = index;
			read_good = player_profile_get(index, &profile);
			read_time = system_milliseconds();
		}
		if (!read_good)
			return;
	}
	text_set(named(description, "current_profile_name", 0), profile.player_name);
}

/* (Campaign's menu) "campaign menu init": player 1's profile and saved
game, for its items */
static boolean campaign_menu_initialize(short controller)
{
	struct player_profile profile;

	if (campaign_profile(controller, &profile))
		campaign_levels_read(&profile);
	return TRUE;
}

/* "campaign menu continue": the saved game goes on */
static boolean campaign_continue(short controller)
{
	struct player_profile profile;
	char const *map_name;
	short level, difficulty;

	if (!campaign_profile(controller, &profile) || !ui_widget_port_saved_game(&map_name, &level, &difficulty))
		return campaign_fail();
	campaign_start(map_name, difficulty, controller);
	return TRUE;
}

/* ---- the map lists (New Game's and the Map screen's): their first row's
chooser of SINGLEPLAYER (the campaign's levels), MULTIPLAYER maps (the
Xbox's), CUSTOM SINGLEPLAYER or CUSTOM MULTIPLAYER maps (the Custom Edition
maps of the custom_maps folder, by the scenario type their files give:
custom_edition_maps.c) (port_settings.MAP_KIND_CHOOSER), then the kind's
rows, scrolling as list_scroll scrolls. A singleplayer kind's map is played
as the campaign is (alone, or hosted as network co-op), a multiplayer kind's
as multiplayer maps are. */

enum
{
	MAP_KIND_SINGLEPLAYER,
	MAP_KIND_MULTIPLAYER,
	MAP_KIND_CUSTOM_SINGLEPLAYER,
	MAP_KIND_CUSTOM_MULTIPLAYER,
	NUMBER_OF_MAP_KINDS
};

/* the rows after the chooser */
#define MAP_KIND_ROWS 10

static void visible_set(struct widget_instance *widget, boolean visible);
/* (party_menu.inc's) */
static void party_screen_noted(struct widget_instance *widget);
static void party_hub_note(wchar_t *text, short size);
static void party_hub_status(wchar_t *headline, short headline_size, wchar_t *detail, short detail_size);
/* (matchmaking_menu.inc's) */
static void matchmaking_hub_note(wchar_t *text, short size);
static void matchmaking_hub_status(wchar_t *headline, short headline_size, wchar_t *detail, short detail_size);
/* (friends_menu.inc's) */
static void friends_hub_note(wchar_t *text, short size);
static void friends_hub_status(wchar_t *headline, short headline_size, wchar_t *detail, short detail_size);
short ui_widget_port_multiplayer_maps(char const *const **names, short *last_used);

static boolean map_kind_singleplayer(short kind)
{
	return kind == MAP_KIND_SINGLEPLAYER || kind == MAP_KIND_CUSTOM_SINGLEPLAYER;
}

/* The kind the chooser shows, kept to the multiplayer kinds unless
`singleplayer`: a singleplayer kind it was turned to becomes the next
multiplayer one the way it turned from `previous`. */
static short map_kind_shown(struct widget_instance *list, boolean singleplayer, short previous)
{
	struct widget_instance *spinner = named(list, "list_item_0_map_kind_spinner", 0);
	short kind;

	if (!spinner)
		return singleplayer ? MAP_KIND_SINGLEPLAYER : MAP_KIND_MULTIPLAYER;
	kind = (short)PIN(spinner->parameters.list.selected_index, 0, NUMBER_OF_MAP_KINDS - 1);
	if (!singleplayer && map_kind_singleplayer(kind))
	{
		short step = kind == (previous + NUMBER_OF_MAP_KINDS - 1) % NUMBER_OF_MAP_KINDS ? -1 : 1;

		do
			kind = (short)((kind + step + NUMBER_OF_MAP_KINDS) % NUMBER_OF_MAP_KINDS);
		while (map_kind_singleplayer(kind));
		spinner->parameters.list.selected_index = kind;
	}
	return kind;
}

static void map_kind_set(struct widget_instance *list, short kind)
{
	struct widget_instance *spinner = named(list, "list_item_0_map_kind_spinner", 0);

	if (spinner)
		spinner->parameters.list.selected_index = kind;
}

/* the first of count entries shown, for the entry chosen to be in the middle */
static short map_kind_first(short chosen, short count)
{
	return (short)PIN(chosen - MAP_KIND_ROWS / 2, 0, MAX(count - MAP_KIND_ROWS, 0));
}

static boolean list_scrolls_at_end(void);

static void map_kind_focus(struct widget_instance *list, short first, short chosen)
{
	focus_row(list, (short)(1 + chosen - first));
}

/* the rows after the chooser: count entries from *first's, scrolled on at
their ends; the entry focused, NONE when the chooser or the buttons have the
focus */
static short map_kind_rows_update(struct widget_instance *list, short *first, short count,
	void (*entry_text)(short entry, wchar_t *text))
{
	short shown = (short)MIN(count, MAP_KIND_ROWS);
	short row = focused_row(list), index = 0;
	struct widget_instance *child;

	if (row != NONE && row >= 1 && row == shown && *first + shown < count && list_scrolls_at_end())
	{
		(*first)++;
		focus_row(list, --row);
	}
	else if (row == 1 && *first > 0 && list_scrolls_at_end())
	{
		(*first)--;
		focus_row(list, ++row);
	}
	for (child = list->child; child; child = child->next, index++)
	{
		wchar_t text[ROW_TEXT_LENGTH];

		if (index < 1)
			continue;
		if (strncmp(child->name, "list_item_", 10))
			break;
		child->visible = index - 1 < shown;
		if (index - 1 < shown)
		{
			entry_text((short)(*first + index - 1), text);
			text_set(named(child, "list_item_text", 0), text);
		}
		visible_set(named(child, "list_item_arrows", 0), child == list->focused_child);
		visible_set(named(child, "scroll_up_button", 0), FALSE);
		visible_set(named(child, "scroll_down_button", 0), FALSE);
	}
	return row == NONE || row < 1 || row > shown ? NONE : (short)(*first + row - 1);
}

/* the Xbox's multiplayer maps: the first of the multiplayer maps
(ui_widget_port_multiplayer_maps), which go on with the Custom Edition ones */
static short xbox_multiplayer_map_count(short multiplayer_map_count)
{
	return (short)MAX(multiplayer_map_count - custom_edition_maps_count(FALSE), 0);
}

static void multiplayer_map_text(short map, wchar_t *text)
{
	string_get("pc\\main_menu\\mp_map_list", map, text);
}

/* a Custom Edition map's name (the menus find its name, picture and
description by its display index: custom_edition_maps.c) */
static void custom_map_text(boolean campaign, short map, wchar_t *text)
{
	wchar_t const *name = custom_edition_maps_name(custom_edition_maps_display_index_of(campaign, map));

	ustrncpy(text, name ? name : L"", ROW_TEXT_LENGTH - 1);
	text[ROW_TEXT_LENGTH - 1] = 0;
}

static void custom_campaign_map_text(short map, wchar_t *text)
{
	custom_map_text(TRUE, map, text);
}

static void custom_multiplayer_map_text(short map, wchar_t *text)
{
	custom_map_text(FALSE, map, text);
}

/* the description's map (each list's has both kinds' widgets): a campaign
level's picture, name and words (level_description sets them), or a map's
by its display index (a multiplayer map's, or a Custom Edition map's), or
(both NONE) neither */
static void map_description_show(struct widget_instance *description, short level, short map)
{
	struct widget_instance *widget;
	short marker;

	visible_set(named(description, "replay_level_right_name", 0), level != NONE);
	visible_set(named(description, "replay_level_right_pic", 0), level != NONE);
	visible_set(named(description, "replay_level_right_data", 0), level != NONE);
	visible_set(named(description, "mp_map_right_name", 0), map != NONE);
	visible_set(named(description, "mp_map_right_pic", 0), map != NONE);
	visible_set(named(description, "mp_map_right_data", 0), map != NONE);
	if (level == NONE)
	{
		for (marker = 0; marker < 3; marker++)
			visible_set(named(description, "difficulty_indicator", marker), FALSE);
	}
	if (map == NONE)
		return;
	if ((widget = named(description, "mp_map_right_name", 0)) != NULL)
		widget->parameters.text_box.string_list_index = map;
	if ((widget = named(description, "mp_map_right_pic", 0)) != NULL)
		widget->animation.current_frame_index = map;
	if ((widget = named(description, "mp_map_right_data", 0)) != NULL)
		widget->parameters.text_box.string_list_index = map;
}

/* New Game's list: SINGLEPLAYER's levels, the profile's reached, or
MULTIPLAYER's maps, played alone to walk around (no game engine: a campaign
game on the map); CUSTOM SINGLEPLAYER's maps played as the campaign's levels
are, at the difficulty chosen next, and CUSTOM MULTIPLAYER's walked around
as MULTIPLAYER's are */
static struct
{
	short kind;
	short first, chosen;
	/* the multiplayer maps (ui_widget_port_multiplayer_maps) */
	char const *const *map_names;
	short map_count;
} level_list;

/* a map list's entries of a kind (the Map screen's difficulties aside) */
static short map_kind_count(short kind, short multiplayer_map_count)
{
	switch (kind)
	{
	case MAP_KIND_SINGLEPLAYER:
		return NUMBER_OF_SINGLE_PLAYER_LEVELS;
	case MAP_KIND_MULTIPLAYER:
		return xbox_multiplayer_map_count(multiplayer_map_count);
	case MAP_KIND_CUSTOM_SINGLEPLAYER:
		return custom_edition_maps_count(TRUE);
	default:
		return custom_edition_maps_count(FALSE);
	}
}

/* the display index of a multiplayer or Custom Edition kind's entry, for its
description (map_description_show) */
static short map_kind_display_index(short kind, short entry)
{
	switch (kind)
	{
	case MAP_KIND_CUSTOM_SINGLEPLAYER:
		return custom_edition_maps_display_index_of(TRUE, entry);
	case MAP_KIND_CUSTOM_MULTIPLAYER:
		return custom_edition_maps_display_index_of(FALSE, entry);
	default:
		return entry;
	}
}

/* the level name of a Custom Edition kind's entry, or NULL */
static char const *custom_map_level_name(short kind, short entry)
{
	return custom_edition_maps_level_name(map_kind_display_index(kind, entry));
}

/* "initialize sp level list solo" starts on the level last played */
static boolean level_list_initialize(struct widget_instance *list, short controller)
{
	struct player_profile profile;
	short level, last_used;

	if (!campaign_profile(controller, &profile))
		return FALSE;
	campaign_levels_read(&profile);
	level = PIN(profile.last_single_player_map_played, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);
	if (campaign.saved_level != NONE)
		level = campaign.saved_level;
	if (!campaign.levels[level].available)
		level = 0;
	campaign.shown_level = level;
	level_list.map_count = ui_widget_port_multiplayer_maps(&level_list.map_names, &last_used);
	level_list.kind = MAP_KIND_SINGLEPLAYER;
	level_list.chosen = level;
	level_list.first = map_kind_first(level, NUMBER_OF_SINGLE_PLAYER_LEVELS);
	map_kind_set(list, level_list.kind);
	map_kind_focus(list, level_list.first, level_list.chosen);
	return TRUE;
}

static void level_row_text(short level, wchar_t *text)
{
	string_get("pc\\main_menu\\map_list", campaign.levels[level].available ? level : LEVEL_UNAVAILABLE, text);
}

/* "solo map list update" */
static void level_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	short kind = map_kind_shown(list, TRUE, level_list.kind), count, entry, level;

	if (kind != level_list.kind)
	{
		level_list.kind = kind;
		level_list.first = level_list.chosen = 0;
	}
	count = map_kind_count(kind, level_list.map_count);
	entry = map_kind_rows_update(list, &level_list.first, count,
		kind == MAP_KIND_SINGLEPLAYER ? level_row_text :
		kind == MAP_KIND_MULTIPLAYER ? multiplayer_map_text :
		kind == MAP_KIND_CUSTOM_SINGLEPLAYER ? custom_campaign_map_text : custom_multiplayer_map_text);
	if (entry != NONE)
		level_list.chosen = entry;
	if (kind != MAP_KIND_SINGLEPLAYER)
	{
		map_description_show(description, NONE,
			level_list.chosen < count ? map_kind_display_index(kind, level_list.chosen) : NONE);
		profile_name_show(description);
		return;
	}
	map_description_show(description, 0, NONE);
	campaign.shown_level = level_list.chosen;
	level = campaign.shown_level;
	if (!campaign.levels[level].available)
		level = LEVEL_UNAVAILABLE;
	level_description(description, "replay_level", level, level == campaign.saved_level,
		level == LEVEL_UNAVAILABLE ? NULL : campaign.levels[level].finished, NONE);
	profile_name_show(description);
}

/* "solo level set map": the level shown, if reached, or the Custom Edition
campaign map, for the difficulty menu; or the multiplayer map, played at
once (FALSE: no difficulty menu). The list is read again first: a tap on
the touchscreen focuses a row and activates it at once, before the list's
update has read which row it is (it started the level shown before, Pillar
of Autumn on a new profile). The map and gametype lists do the same. */
static boolean level_choose(struct widget_instance *list, short controller)
{
	short level, count;

	level_list_update(list);
	level = campaign.shown_level;
	count = map_kind_count(level_list.kind, level_list.map_count);

	if (level_list.kind != MAP_KIND_SINGLEPLAYER && (level_list.chosen < 0 || level_list.chosen >= count))
		return campaign_fail();
	if (level_list.kind == MAP_KIND_MULTIPLAYER || level_list.kind == MAP_KIND_CUSTOM_MULTIPLAYER)
	{
		struct player_profile profile;
		char const *map_name = level_list.kind == MAP_KIND_MULTIPLAYER ? level_list.map_names[level_list.chosen] :
			custom_map_level_name(level_list.kind, level_list.chosen);

		if (!map_name || !campaign_profile(controller, &profile))
			return campaign_fail();
		campaign_start(map_name, main_get_difficulty(), controller);
		return FALSE;
	}
	if (level_list.kind == MAP_KIND_CUSTOM_SINGLEPLAYER)
	{
		char const *map_name = custom_map_level_name(level_list.kind, level_list.chosen);

		if (!map_name)
			return campaign_fail();
		main_set_map_name(map_name);
		main_defer_map_map_change();
		return TRUE;
	}
	if (level < 0 || level >= NUMBER_OF_SINGLE_PLAYER_LEVELS || !campaign.levels[level].available)
		return campaign_fail();
	/* (not yet: setting it at the main menu changes map, as the Xbox's has it
	not) */
	main_set_map_name(main_get_solo_level_name(level));
	main_defer_map_map_change();
	return TRUE;
}

/* the difficulty menu: "difficulty item select" starts the game at the
item's; "set difficulty" (its OK button) at the one its description shows */
static boolean difficulty_start(short difficulty, short controller)
{
	struct player_profile profile;
	char const *map_name = main_get_map_name();

	if (!campaign_profile(controller, &profile))
		return campaign_fail();
	/* (a campaign level, or a Custom Edition campaign map) */
	if (!custom_edition_maps_level_campaign(map_name))
		map_name = main_get_solo_level_name(0);
	campaign_start(map_name, PIN(difficulty, 0, 3), controller);
	return TRUE;
}

static short difficulty_shown(struct widget_instance *widget)
{
	for (; widget; widget = widget->parent)
	{
		if (!strcmp(widget->name, "difficulty_select_list") && widget->parameters.list.extended_description)
		{
			struct widget_instance *picture = named(widget->parameters.list.extended_description,
				"difficulty_options", 0);

			return picture ? picture->animation.current_frame_index : main_get_difficulty();
		}
	}
	return main_get_difficulty();
}

static short sibling_index(struct widget_instance *widget)
{
	struct widget_instance *child;
	short index = 0;

	for (child = widget->parent ? widget->parent->child : widget; child && child != widget; child = child->next)
		index++;
	return index;
}

/* Load Game's list: each profile's saved game (read with the profile as
player 1's, which then is again) */
static void saved_games_read(short controller)
{
	struct player_profile profile, active;
	long profiles[MAXIMUM_PROFILES];
	long active_index;
	word count = MAXIMUM_PROFILES;
	short index;

	campaign.saved_game_count = 0;
	if (!campaign_profile(controller, &active))
		return;
	active_index = player_ui_get_active_player_profile_index(0);
	player_profiles_enumerate_available_to_local_player_index(NONE, &count, profiles, FALSE);
	for (index = 0; index < (short)count && campaign.saved_game_count < MAXIMUM_ROWS; index++)
	{
		struct campaign_saved_game *saved_game = &campaign.saved_games[campaign.saved_game_count];
		char const *map_name;

		if (!((unsigned long)profiles[index] & PROFILE_VALID_BIT) || !player_profile_get(profiles[index], &profile))
			continue;
		player_ui_set_active_player_profile(0, profiles[index], &profile);
		if (ui_widget_port_saved_game(&map_name, &saved_game->level, &saved_game->difficulty))
		{
			saved_game->profile_index = profiles[index];
			ustrncpy(saved_game->profile_name, profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
			saved_game->profile_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
			campaign.saved_game_count++;
		}
	}
	player_ui_set_active_player_profile(0, active_index, &active);
	campaign_levels_read(&active);
}

/* "load game menu init" */
static boolean saved_game_list_initialize(struct widget_instance *list, short controller)
{
	short index;

	saved_games_read(controller);
	campaign.shown_saved_game = 0;
	for (index = 0; index < campaign.saved_game_count; index++)
	{
		if (campaign.saved_games[index].profile_index == player_ui_get_active_player_profile_index(0))
			campaign.shown_saved_game = index;
	}
	focus_row(list, campaign.saved_game_count ? campaign.shown_saved_game : NONE);
	return TRUE;
}

static void saved_game_row_text(short row, wchar_t *text)
{
	ustrncpy(text, campaign.saved_games[row].profile_name, ROW_TEXT_LENGTH - 1);
	text[ROW_TEXT_LENGTH - 1] = 0;
}

/* "load game list update" */
static void saved_game_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *item = named(description, "load_level_right_item", 0);
	short row = focused_row(list);

	rows_update(list, campaign.saved_game_count, saved_game_row_text);
	if (row != NONE && row < campaign.saved_game_count)
		campaign.shown_saved_game = row;
	if (campaign.shown_saved_game >= campaign.saved_game_count)
		campaign.shown_saved_game = campaign.saved_game_count - 1;
	if (item)
		item->visible = campaign.saved_game_count > 0;
	if (campaign.saved_game_count > 0)
	{
		struct campaign_saved_game const *saved_game = &campaign.saved_games[campaign.shown_saved_game];

		level_description(description, "load_level", saved_game->level, FALSE, NULL, saved_game->difficulty);
	}
	else if (row != NONE)
	{
		focus_row(list, NONE);
	}
	profile_name_show(description);
}

/* "load game menu activated": the saved game goes on, with its profile as
player 1's */
static boolean saved_game_continue(short controller)
{
	struct campaign_saved_game const *saved_game;
	struct player_profile profile;

	if (campaign.shown_saved_game < 0 || campaign.shown_saved_game >= campaign.saved_game_count)
		return campaign_fail();
	saved_game = &campaign.saved_games[campaign.shown_saved_game];
	if (!player_profile_get(saved_game->profile_index, &profile))
		return campaign_fail();
	player_ui_set_active_player_profile(0, saved_game->profile_index, &profile);
	campaign_start(main_get_solo_level_name(saved_game->level), saved_game->difficulty, controller);
	return TRUE;
}

/* "load game menu delete request" (before its question) and "delete
finish" (its OK): the saved game shown goes */
static boolean saved_game_delete_request(void)
{
	if (campaign.shown_saved_game < 0 || campaign.shown_saved_game >= campaign.saved_game_count)
		return campaign_fail();
	campaign.saved_game_to_delete = campaign.shown_saved_game;
	return TRUE;
}

static boolean saved_game_delete(short controller)
{
	char path[256];

	if (campaign.saved_game_to_delete < 0 || campaign.saved_game_to_delete >= campaign.saved_game_count ||
		!player_profile_get_enclosing_directory_path(campaign.saved_games[campaign.saved_game_to_delete].profile_index,
			path) || strlen(path) + strlen("savegame.bin") >= sizeof(path))
	{
		return campaign_fail();
	}
	strcat(path, "savegame.bin");
	if (!DeleteFileA(path))
		platform_log("menus: could not delete %s", path);
	campaign.saved_game_to_delete = NONE;
	saved_games_read(controller);
	return TRUE;
}

/* the settings' screens (tools/port_settings.py): every spinner of a
setting in the screen */
static void settings_each(struct widget_instance *widget, boolean (*visit)(struct widget_instance *spinner,
	struct pc_menu_setting *setting))
{
	struct widget_instance *child;

	for (child = widget->child; child; child = child->next)
	{
		struct pc_menu_setting *setting = child->type == 2 && !spinner_item(child) ?
			pc_menu_setting_get(child->definition_tag_index) : NULL;

		if (setting)
			visit(child, setting);
		else
			settings_each(child, visit);
	}
}

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget->parent)
		widget = widget->parent;
	return widget;
}

/* "port settings save" (OK): those changed written, and applied */
static boolean setting_changed_save(struct widget_instance *spinner, struct pc_menu_setting *setting)
{
	short index = spinner->parameters.list.selected_index;

	/* (a row locked shows a value that is not the setting's: sim_rate_row_show) */
	if (spinner->disabled || index < 0 || index >= setting->value_count || index == setting->loaded_index)
		return TRUE;
	if (!setting_write(setting->setting, setting->values[index]))
	{
		platform_log("menus: could not set %s", setting->setting);
		return FALSE;
	}
	platform_log("menus: %s = %s", setting->setting, setting->values[index]);
	setting->loaded_index = index;
	return TRUE;
}

/* "port settings defaults" */
static boolean setting_default_show(struct widget_instance *spinner, struct pc_menu_setting *setting)
{
	char text[300];

	if (setting_text(setting->setting, text, sizeof(text), TRUE))
		spinner->parameters.list.selected_index = setting_value_index(setting, text);
	return TRUE;
}

/* "port settings help": the line of the row chosen (by its label's string:
the buttons' is the first) */
static void settings_help(struct widget_instance *list)
{
	struct widget_instance *help = list->parameters.list.extended_description;
	struct widget_instance *row = list->focused_child;
	short index = 0;

	if (!help)
		return;
	if (row && row->child && row->child->type == 1 && strncmp(row->name, "button", 6))
	{
		short label = pc_menu_string_index(row->child->definition_tag_index);

		if (label != NONE)
			index = label + 1;
	}
	help->parameters.text_box.string_list_index = index;
}

/* Video Setup's Resolution and Window Size, in the one row's place
(tools/port_settings.py): the one the display mode shown uses (Window Size
the window's, Resolution fullscreen's and borderless's) is shown, and the
list passes over the other, which takes no focus while it is hidden */
static void video_rows_show(struct widget_instance *list)
{
	struct widget_instance *mode = named(list, "mode_spinner", 0);
	struct widget_instance *resolution = named(list, "op_resolution", 0);
	struct widget_instance *window_size = named(list, "op_window_size", 0);
	struct pc_menu_setting *setting = mode ? pc_menu_setting_get(mode->definition_tag_index) : NULL;
	struct widget_instance *shown, *child;
	short index;

	if (!setting || !resolution || !window_size)
		return;
	index = mode->parameters.list.selected_index;
	shown = index >= 0 && index < setting->value_count && !strcmp(setting->values[index], "windowed") ?
		window_size : resolution;
	resolution->visible = shown == resolution;
	window_size->visible = shown == window_size;
	/* (the focus on the one hidden goes to the one shown: Defaults can
	change the mode while it has it) */
	if (list->focused_child == (shown == resolution ? window_size : resolution))
	{
		for (index = 0, child = list->child; child && child != shown; child = child->next)
			index++;
		list->focused_child = shown;
		list->parameters.list.selected_index = index;
	}
}

/* Video Setup's Refresh Rate, shown while Window shows FULLSCREEN where
fullscreen can set the display's rate (sdl_platform.c); the rows below close
the gap (settings_rows_pack), and a focus on it hidden goes to the first row
shown (settings_focus_shown) */
static void refresh_rate_row_show(struct widget_instance *list)
{
	struct widget_instance *mode = named(list, "mode_spinner", 0);
	struct widget_instance *row = named(list, "op_refresh_rate", 0);
	struct pc_menu_setting *setting = mode ? pc_menu_setting_get(mode->definition_tag_index) : NULL;
	short index;

	if (!row)
		return;
	index = mode ? mode->parameters.list.selected_index : NONE;
	row->visible = setting && index >= 0 && index < setting->value_count &&
		!strcmp(setting->values[index], "fullscreen") && platform_refresh_rates_settable();
}

/* Interface's button style, shown while the HUD shown is the high-res one
(Defaults too); the rows below close the gap (settings_rows_pack) */
static void hud_button_style_row_show(struct widget_instance *list)
{
	struct widget_instance *hud = named(list, "high_res_hud_spinner", 0);
	struct widget_instance *row = named(list, "op_hud_button_style", 0);
	struct pc_menu_setting *setting = hud ? pc_menu_setting_get(hud->definition_tag_index) : NULL;
	short index;

	if (!setting || !row)
		return;
	index = hud->parameters.list.selected_index;
	row->visible = index >= 0 && index < setting->value_count && !strcmp(setting->values[index], "true");
}

/* Mouse's Menu Cursor (display.menu_cursor), hidden while VR Setup's row is
there (the game in the headset, whose menus have no desktop pointer): that
row takes its room on the page (tools/port_settings.py) */
static void menu_cursor_row_show(struct widget_instance *list)
{
	struct widget_instance *row = named(list, "op_menu_cursor", 0);

	if (row)
		row->visible = !named(list, "op_vr_setup", 0);
}

/* Brightness's Gamma (display.gamma), hidden in the VR build, whose frames
are never curved (d3d8_device.c's gamma_send) */
static void gamma_row_show(struct widget_instance *list)
{
#ifdef HALO_VR
	struct widget_instance *row = named(list, "op_gamma", 0);

	if (row)
		row->visible = FALSE;
#else
	(void)list;
#endif
}

/* Video's Frame Rate Limit, shown while VSync shows OFF (Defaults too):
with vsync on, the display paces the frames and the limit does nothing;
the rows below close the gap (settings_rows_pack) */
static void max_fps_row_show(struct widget_instance *list)
{
	struct widget_instance *vsync = named(list, "vsync_spinner", 0);
	struct widget_instance *row = named(list, "op_max_fps", 0);
	struct pc_menu_setting *setting = vsync ? pc_menu_setting_get(vsync->definition_tag_index) : NULL;
	short index;

	if (!setting || !row)
		return;
	index = vsync->parameters.list.selected_index;
	row->visible = index >= 0 && index < setting->value_count && !strcmp(setting->values[index], "false");
}

/* a setting's value as Settings shows it: the change kept, else
config.toml's (or the profile's) */
static boolean settings_value(char const *name, char *text, unsigned int size)
{
	char const *pending = settings_pending_value(name);

	if (pending)
	{
		snprintf(text, size, "%s", pending);
		return TRUE;
	}
	return setting_text(name, text, size, FALSE);
}

static boolean settings_values_same(char const *a, char const *b)
{
	double number_a, number_b;

	if (!_stricmp(a, b))
		return TRUE;
	return text_is_number(a, &number_a) && text_is_number(b, &number_b) && number_a == number_b;
}

/* whether a setting Quality (display.graphics) chooses is not at the value
the preset gives it: Quality shows CUSTOM */
static boolean graphics_preset_custom(char const *preset)
{
	char const *name;
	int entry;

	for (entry = 0; (name = config_graphics_setting(entry)) != NULL; entry++)
	{
		char given[300], shown[300];

		if (config_graphics_value(preset, name, given, sizeof(given)) &&
			settings_value(name, shown, sizeof(shown)) && !settings_values_same(given, shown))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* the preset chosen, Classic or Enhanced: each setting it chooses kept at
its value (those on another page too), which Settings' OK writes after
display.graphics (as config_write, leaving them to the preset), and those
on the page shown at it */
static char const *graphics_preset_choice;

static boolean graphics_preset_row_show(struct widget_instance *spinner, struct pc_menu_setting *setting)
{
	char text[300];

	if (config_graphics_value(graphics_preset_choice, setting->setting, text, sizeof(text)))
		spinner->parameters.list.selected_index = setting_value_index(setting, text);
	return TRUE;
}

static void graphics_preset_choose(struct widget_instance *list, char const *preset)
{
	char const *name;
	int entry;

	settings_pending_set("display.graphics", preset);
	for (entry = 0; (name = config_graphics_setting(entry)) != NULL; entry++)
	{
		char given[300];

		if (config_graphics_value(preset, name, given, sizeof(given)))
			settings_pending_set(name, given);
	}
	graphics_preset_choice = preset;
	settings_each(screen_of(list), graphics_preset_row_show);
}

/* Video's Quality (display.graphics): CLASSIC or ENHANCED, or CUSTOM while
any setting it chooses (on this page or Advanced Graphics) is another
value. Classic or Enhanced chosen (Defaults too) puts them all at the
preset's values; CUSTOM is passed over, the way the spinner went, unless it
is so */
static void graphics_preset_show(struct widget_instance *list)
{
	struct widget_instance *spinner = named(list, "graphics_spinner", 0);
	struct pc_menu_setting *setting = spinner ? pc_menu_setting_get(spinner->definition_tag_index) : NULL;
	short index, custom, count;
	char base[64];

	if (!setting || spinner != graphics_preset.spinner)
		return;
	count = (short)setting->value_count;
	custom = setting_value_index(setting, "custom");
	if (strcmp(setting->values[custom], "custom") || !settings_value("display.graphics", base, sizeof(base)))
		return;
	index = spinner->parameters.list.selected_index;
	if (index < 0 || index >= count)
		return;
	if (index != graphics_preset.shown)
	{
		if (index == custom && !graphics_preset_custom(base))
		{
			short step = graphics_preset.shown != NONE && (graphics_preset.shown + 1) % count == index ? 1 : count - 1;

			index = (short)((index + step) % count);
			spinner->parameters.list.selected_index = index;
		}
		if (index != custom)
			graphics_preset_choose(list, setting->values[index]);
		graphics_preset.shown = index;
		return;
	}
	if (index != custom)
		snprintf(base, sizeof(base), "%s", setting->values[index]);
	if (graphics_preset_custom(base) != (index == custom))
	{
		index = index == custom ? setting_value_index(setting, base) : custom;
		spinner->parameters.list.selected_index = index;
		graphics_preset.shown = index;
	}
}

/* Graphics' SIMULATION row (game.sim_rate): where the game sets the setting
aside (sim_rate_locked: a network game of other machines, a film's
playback), the row is locked. It shows the rate the game takes (ORIGINAL
(30), or a Standard game's agreed rate), dimmed, the
list and the pointer pass over it, and OK leaves the setting as it was
(setting_changed_save passes over a disabled spinner). Unlocked, it shows
the setting's value again */
static void sim_rate_row_show(struct widget_instance *list)
{
	struct widget_instance *row = named(list, "op_sim_rate", 0);
	struct widget_instance *spinner = named(list, "sim_rate_spinner", 0);
	struct pc_menu_setting *setting = spinner ? pc_menu_setting_get(spinner->definition_tag_index) : NULL;
	boolean locked = sim_rate_locked();
	struct widget_instance *child;
	short index;

	if (!row || !setting || spinner->disabled == locked)
		return;
	row->disabled = locked;
	spinner->disabled = locked;
	/* (dimmed: its label, its spinner and its background, as a row fading
	out is) */
	row->alpha_modifier = locked ? 0.4f : 1.0f;
	if (locked)
	{
		char classic[16];

		/* (the rate the game takes: a Legacy game's 30, a Standard game's
		agreed rate) */
		snprintf(classic, sizeof(classic), "%ld", sim_rate_next());
		spinner->parameters.list.selected_index = setting_value_index(setting, classic);
	}
	else if (setting->loaded_index >= 0 && setting->loaded_index < setting->value_count)
	{
		spinner->parameters.list.selected_index = setting->loaded_index;
	}
	/* (the focus on it goes to the buttons: it is the last row) */
	if (locked && list->focused_child == row)
	{
		for (index = 0, child = list->child; child && strncmp(child->name, "button_bar", 10); child = child->next)
			index++;
		if (child)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
		}
	}
}

/* (Controller's Button Remapping, a page of Settings') */
#include "controller_remap_menu.inl"

/* ---------- Settings' tabs and pages (tools/port_settings.py's PAGES)

"port settings begin" (the main menu's and the pause menu's SETTINGS) forgets
what the last Settings kept and edits player 1's profile, if there is one:
Settings opens on its first tab with no profile too. A tab's "port settings
tab" (A or a click on one of the tabs), "port settings tab next" and
"previous" (RB and LB, E and Q) open another tab in its place, out of the
history, so that B and Cancel ("port settings cancel": nothing kept, the
profile's changes forgotten) leave Settings from any tab, and its OK ("port
settings ok") writes every change kept, saves the profile's and applies the
window's. A page a row opens writes them too at its OK ("port settings
keep": an OK saves, wherever it is), and its Cancel ("port settings page
cancel") puts back what was kept when it opened. "port settings new profile" (CREATE PROFILE) asks for a new
profile's name, as the main menu's items do. "port settings profiles"
(MANAGE PROFILES, before the profile manager opens) writes every change
kept, as an OK does, and the PROFILES tab, shown again, edits player 1's
profile then: the one chosen there, made or left after a delete.

"port settings page" (a tab's or a page's list, each frame) shows the rows
that are for this game (settings_rows_show), packs them at the steps
tools/port_settings.py laid them out at, notes the changes, keeps the tabs on
the one shown, and fills in the panel for the row chosen (settings_panel_show):
its name, its value in large type, and its words wrapped to the panel. */

/* as tools/port_settings.py lays the rows out (SETTINGS_ROW_STEP,
SETTINGS_SECTION_STEP, SETTINGS_SECTION_GAP, SETTINGS_TOP, PAGE_TOP), and the panel's words'
places under its name (_settings_shared) */
#define SETTINGS_ROW_STEP 21
#define SETTINGS_SECTION_STEP 20
#define SETTINGS_SECTION_GAP 6
#define SETTINGS_TABS_ROWS_TOP 104
#define SETTINGS_PAGE_ROWS_TOP 80
#define SETTINGS_PANEL_TEXT_TOP 44
#define SETTINGS_PANEL_TEXT_UNDER_STILLS 122
#define SETTINGS_PANEL_TEXT_UNDER_PICTURE 146
#define SETTINGS_PANEL_TEXT_LENGTH 512
#define SETTINGS_FOLDER "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\"
#define SETTINGS_SHARED "pc\\main_menu\\settings\\"
/* the link rows' sign, after their labels (U+203A) */
#define SETTINGS_LINK_SIGN 0x203A

/* the tabs in order: each one's folder (below the profile menu's, whose
screens they replace) and screen */
static struct
{
	char const *folder;
	char const *screen;
} const settings_tabs[] =
{
	{ "video_settings", "video_settings_screen" },
	{ "audio_settings", "audio_settings_screen" },
	{ "mouse_settings", "mouse_settings_screen" },
	{ "network_setup", "network_settings_screen" },
	{ "interface_settings", "interface_settings_screen" },
	{ "profile_settings", "profile_settings_screen" },
};

/* the rows hidden in a game (not on the main menu): those that would drop
it or are the main menu's (internet play, the router, the clipboard, the
updates, the profiles, the mods), and those whose screens are ui.map's (the
name's keyboard, the colours' pictures) or make a profile */
static char const *const settings_in_game_hidden[] =
{
	"op_online", "op_allow_upnp", "op_join_from_clipboard", "op_auto", "op_name", "op_color", "op_create_profile",
	"op_profiles", "op_mods",
};

static struct
{
	/* the screen a page update last saw (a page made: its snapshot) */
	struct widget_instance *screen;
	long screen_tag;
	long screen_time;
	/* MANAGE PROFILES opened: the profile to edit again, as a tab shows */
	boolean profiles_opened;
} settings_page = { NULL, NONE, 0, FALSE };

static short settings_tab_of(struct widget_instance const *screen)
{
	short tab;

	for (tab = 0; tab < (short)NUMBEROF(settings_tabs); tab++)
	{
		if (screen->name && !strcmp(screen->name, settings_tabs[tab].screen))
			return tab;
	}
	return NONE;
}

/* a change on the page noted (Quality's CUSTOM is not a value of its own) */
static boolean setting_stash(struct widget_instance *spinner, struct pc_menu_setting *setting)
{
	short index = spinner->parameters.list.selected_index;

	if (spinner->disabled || index < 0 || index >= setting->value_count || setting->loaded_index == NONE)
		return TRUE;
	if (!strcmp(setting->setting, "display.graphics") && !strcmp(setting->values[index], "custom"))
		return TRUE;
	if (index == setting->loaded_index)
		settings_pending_remove(setting->setting);
	else
		settings_pending_set(setting->setting, setting->values[index]);
	return TRUE;
}

/* Settings' OK, and a page's: every change kept written (display.graphics
first, which leaves those it chooses to it: those after it, at its values,
stay so), the window's applied, and the profile's saved; a page's goes on
editing the profile, as saved, for Settings */
static void settings_pending_write(boolean finish)
{
	long profile_index = player_ui_get_edit_profile_index();

	short pass, index;

	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < settings_pending.count; index++)
		{
			struct settings_pending const *entry = &settings_pending.entries[index];

			if ((pass == 0) != !strcmp(entry->name, "display.graphics"))
				continue;
			if (setting_write(entry->name, entry->value))
				platform_log("menus: %s = %s", entry->name, entry->value);
			else
				platform_log("menus: could not set %s", entry->name);
		}
	}
	settings_pending_clear();
	platform_display_apply();
	if (player_ui_get_edit_player_profile())
	{
		if (player_ui_edit_profile_is_dirty() && !player_ui_save_profile())
			platform_log("menus: could not save the profile's changes");
		player_ui_end_editing_profile();
		if (!finish)
			player_ui_begin_editing_profile(profile_index);
	}
}

/* another tab in the place of this one (out of the history) */
static boolean settings_tab_open(struct widget_instance *widget, short tab, boolean *widget_deleted)
{
	struct widget_instance *screen = screen_of(widget);
	short count = (short)NUMBEROF(settings_tabs);
	char name[256];

	tab = (short)((tab + count) % count);
	if (tab == settings_tab_of(screen))
		return TRUE;
	settings_each(screen, setting_stash);
	snprintf(name, sizeof(name), SETTINGS_FOLDER "%s\\%s", settings_tabs[tab].folder, settings_tabs[tab].screen);
	ui_play_audio_feedback_sound(SOUND_CURSOR);
	return ui_widget_port_switch(widget, name, widget_deleted);
}

/* CREATE PROFILE: the new profile's name asked (a press: the profile is made
on its controller); FALSE, for its row's conditional to make it */
static boolean settings_profile_new(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted)
{
	if (!main_menu_is_active() || !event || event->controller_index < 0 || event->controller_index >= 4)
		return TRUE;
	settings_each(screen_of(widget), setting_stash);
	game_function("new campaign chosen", widget, event, widget_deleted);
	return FALSE;
}

/* "port settings <name>": FALSE if it is not one of these */
static boolean settings_function(char const *name, struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted, boolean *result)
{
	struct widget_instance *screen = screen_of(widget);

	*result = TRUE;
	/* (a slider's number typed into: B and Escape forget what was typed
	and stay, failing so that the page is not left; OK keeps it) */
	if (text_field_editing(NULL) && (!strcmp(name, "cancel") || !strcmp(name, "page cancel")))
	{
		text_field_end(FALSE);
		*result = FALSE;
		return TRUE;
	}
	if (text_field_editing(NULL) && (!strcmp(name, "ok") || !strcmp(name, "keep") || !strncmp(name, "tab", 3)))
		text_field_end(TRUE);
	if (!strcmp(name, "begin"))
	{
		settings_pending_clear();
		pc_menu_profile_edit_begin();
	}
	else if (!strcmp(name, "tab"))
	{
		*result = settings_tab_open(widget, sibling_index(widget), widget_deleted);
	}
	else if (!strcmp(name, "tab next") || !strcmp(name, "tab previous"))
	{
		short tab = settings_tab_of(screen);

		if (tab != NONE)
			*result = settings_tab_open(widget, (short)(tab + (!strcmp(name, "tab next") ? 1 : -1)), widget_deleted);
	}
	else if (!strcmp(name, "ok"))
	{
		settings_each(screen, setting_stash);
		settings_pending_write(TRUE);
	}
	else if (!strcmp(name, "cancel"))
	{
		settings_pending_clear();
		player_ui_end_editing_profile();
	}
	else if (!strcmp(name, "keep"))
	{
		settings_each(screen, setting_stash);
		settings_pending_write(FALSE);
		pad_editor_save(screen);
	}
	else if (!strcmp(name, "page cancel"))
	{
		/* (Button Remapping's button being taken, or its replacement
		asked: dropped, the page kept) */
		if (!pad_editor_cancel(screen))
		{
			*result = FALSE;
			return TRUE;
		}
		settings_pending = settings_snapshot;
	}
	else if (!strcmp(name, "new profile"))
	{
		*result = settings_profile_new(widget, event, widget_deleted);
	}
	else if (!strcmp(name, "profiles"))
	{
		/* (MANAGE PROFILES: the profile manager may change player 1's
		profile, or delete the one edited, so the changes are written to
		this one first) */
		settings_each(screen, setting_stash);
		settings_pending_write(TRUE);
		settings_page.profiles_opened = TRUE;
	}
	else
	{
		return FALSE;
	}
	return TRUE;
}

/* whether the row sets one of the profile's settings ("profile.<field>"),
or opens what is the profile's (its name, its colour, the other profiles) */
static boolean settings_row_profile(struct widget_instance *row)
{
	struct widget_instance *child;

	if (!strcmp(row->name, "op_name") || !strcmp(row->name, "op_color") || !strcmp(row->name, "op_profiles"))
		return TRUE;
	for (child = row->child; child; child = child->next)
	{
		struct pc_menu_setting *setting = child->type == 2 && !spinner_item(child) ?
			pc_menu_setting_get(child->definition_tag_index) : NULL;

		if (setting && !strncmp(setting->setting, "profile.", 8))
			return TRUE;
	}
	return FALSE;
}

/* the rows that are for this game: in a game, not those of
settings_in_game_hidden; with no profile, not the profile's, but CREATE
PROFILE (on the main menu) */
static void settings_rows_show(struct widget_instance *list)
{
	boolean in_game = !main_menu_is_active();
	boolean profile = player_ui_get_edit_player_profile() != NULL;
	struct widget_instance *row;

	for (row = list->child; row; row = row->next)
	{
		short index;

		if (!row->name || strncmp(row->name, "op_", 3))
			continue;
		if (!strcmp(row->name, "op_create_profile"))
		{
			row->visible = !profile && !in_game;
			continue;
		}
		if (!profile && settings_row_profile(row))
			row->visible = FALSE;
		for (index = 0; in_game && index < (short)NUMBEROF(settings_in_game_hidden); index++)
		{
			if (!strcmp(row->name, settings_in_game_hidden[index]))
				row->visible = FALSE;
		}
	}
}

/* the rows shown, a step apart from the top, each section's title over them
(hidden with none shown), as tools/port_settings.py lays them out for every
row */
static void settings_rows_pack(struct widget_instance *list, boolean tab)
{
	short y = tab ? SETTINGS_TABS_ROWS_TOP : SETTINGS_PAGE_ROWS_TOP;
	struct widget_instance *child;
	boolean rows_above = FALSE;

	for (child = list->child; child; child = child->next)
	{
		if (!child->name)
			continue;
		if (!strncmp(child->name, "section_", 8))
		{
			struct widget_instance *row;

			/* (a title the list passes over) */
			child->disabled = TRUE;
			child->visible = FALSE;
			for (row = child->next; row && (!row->name || strncmp(row->name, "section_", 8)); row = row->next)
			{
				if (row->name && !strncmp(row->name, "op_", 3) && row->visible)
					child->visible = TRUE;
			}
			if (child->visible)
			{
				/* (a gap above a section after rows) */
				if (rows_above)
					y += SETTINGS_SECTION_GAP;
				child->vertical_offset = (short)(list->vertical_offset + y);
				y += SETTINGS_SECTION_STEP;
			}
		}
		else if (!strncmp(child->name, "op_", 3) && child->visible)
		{
			child->vertical_offset = (short)(list->vertical_offset + y);
			y += SETTINGS_ROW_STEP;
			rows_above = TRUE;
		}
	}
}

/* the focus on a row hidden goes to the first row shown */
static void settings_focus_shown(struct widget_instance *list)
{
	struct widget_instance *child;
	short index = 0;

	if (!list->focused_child || (list->focused_child->visible &&
		(!list->focused_child->name || strncmp(list->focused_child->name, "section_", 8))))
		return;
	for (child = list->child; child; child = child->next, index++)
	{
		if (child->visible && child->name && !strncmp(child->name, "op_", 3) && !child->disabled)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
			return;
		}
	}
}

/* the tabs' focus, while they have not the list's, on the tab shown */
static void settings_tabs_show(struct widget_instance *list, short tab)
{
	struct widget_instance *tabs = named(list, "tabs", 0);
	struct widget_instance *item;
	short index = 0;

	if (!tabs || tab == NONE || list->focused_child == tabs)
		return;
	for (item = tabs->child; item && index < tab; item = item->next)
		index++;
	if (item)
	{
		tabs->focused_child = item;
		tabs->parameters.list.selected_index = tab;
	}
}

/* a widget's own text: its label's string (from its list), or the value a
spinner shows */
static wchar_t const *settings_widget_text(struct widget_instance *widget, short index)
{
	long list = widget ? pc_menu_widget_string_list(widget->definition_tag_index) : NONE;

	if (list == NONE)
		return NULL;
	if (index == NONE)
		index = pc_menu_string_index(widget->definition_tag_index);
	return index >= 0 ? unicode_string_list_get_string(list, index) : NULL;
}

static wchar_t const *settings_string(char const *list, short index)
{
	long tag_index = tag_loaded('ustr', list);

	return tag_index != NONE && index >= 0 ? unicode_string_list_get_string(tag_index, index) : NULL;
}

/* the words for the row, from the help list beside its labels' */
static wchar_t const *settings_row_help(struct widget_instance *label)
{
	long labels = label ? pc_menu_widget_string_list(label->definition_tag_index) : NONE;
	short index = label ? pc_menu_string_index(label->definition_tag_index) : NONE;
	char name[256];
	char *last;

	if (labels == NONE || index == NONE)
		return NULL;
	snprintf(name, sizeof(name), "%s", tag_get_name(labels));
	last = strrchr(name, '\\');
	if (!last || strcmp(last + 1, "labels"))
		return NULL;
	snprintf(last + 1, sizeof(name) - (size_t)(last + 1 - name), "help_strings");
	return settings_string(name, (short)(index + 1));
}

static void settings_wide_copy(wchar_t *to, wchar_t const *from, short size)
{
	short length = 0;

	for (; from && *from && length < size - 1; from++)
		to[length++] = *from;
	to[length] = 0;
}

/* how wide the text is drawn in the font */
static short settings_text_width(long font, wchar_t const *text)
{
	static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };
	rectangle2d bounds, text_bounds, cursor;

	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 0x3FFF;
	bounds.y1 = 480;
	draw_string_set_draw_mode(font, NONE, 0, 0, &white);
	draw_unicode_string_compute_bounds(&bounds, text, &text_bounds, &cursor);
	return (short)(MAX(text_bounds.x1, cursor.x0) - bounds.x0);
}

/* the words in lines that fit across width in the font (the game's line
breaks, \r\n), broken at spaces; as they are while the font is not known */
static short settings_text_wrap(wchar_t const *text, wchar_t *wrapped, short size, long font, short width)
{
	short length = 0, line_start = 0, lines = 1;

	wrapped[0] = 0;
	while (text && *text && length < size - 3)
	{
		short word = 0, mark = length;

		while (text[word] == ' ')
			word++;
		while (text[word] && text[word] != ' ')
			word++;
		if (length + word + 2 >= size)
			break;
		memcpy(wrapped + length, text, word * sizeof(wchar_t));
		wrapped[length + word] = 0;
		if (font != NONE && mark > line_start && settings_text_width(font, wrapped + line_start) > width)
		{
			/* (the word to the next line, without its space) */
			short skip = 0;

			while (text[skip] == ' ')
				skip++;
			wrapped[mark] = '\r';
			wrapped[mark + 1] = '\n';
			length = (short)(mark + 2);
			line_start = length;
			memcpy(wrapped + length, text + skip, (word - skip) * sizeof(wchar_t));
			length = (short)(length + word - skip);
			lines++;
		}
		else
		{
			length = (short)(length + word);
		}
		wrapped[length] = 0;
		text += word;
	}
	return lines;
}

/* a row's picture of each value (tools/port_settings.py's
SETTINGS_VALUE_PICTURES: panel_picture_<the row's key>) shown, at the value
shown (its spinner's index, or a slider's stop nearest it: frame), the
others hidden; FALSE if the row has none */
static boolean settings_value_picture_show(struct widget_instance *panel, struct widget_instance *row,
	short frame)
{
	struct widget_instance *child;
	boolean shown = FALSE;

	for (child = panel->child; child; child = child->next)
	{
		if (!child->name || strncmp(child->name, "panel_picture_", 14))
			continue;
		child->visible = row && frame != NONE && row->name && !strncmp(row->name, "op_", 3) &&
			!strcmp(child->name + 14, row->name + 3);
		if (child->visible)
		{
			child->animation.current_frame_index = frame;
			shown = TRUE;
		}
	}
	return shown;
}

/* ---------- Online's DISCORD ACCOUNT (tools/port_settings.py's
DISCORD_ACCOUNT; port/linux/src/player_link.c)

The Online tab's row opens the page, its value the account's state. The page
has the account's row (its state; the panel names the account and how many
master servers it is linked with), UNLINK (shown while there is something to
forget: a first press asks, a second within DISCORD_UNLINK_MILLISECONDS
forgets it), a row for each master server of masters.txt (its host and the
account's state with it; A or a click opens its link page in the browser at
once, and a waiting row shows the page's code), and SHOW TO SERVERS
(network.link_share). The page opens on the master a server that refused
this machine for its VPN is listed by, if there is one. */

#define DISCORD_UNLINK_MILLISECONDS 5000

static struct
{
	/* the page's screen last seen (a new one: the needed master focused) */
	long screen_tag;
	long screen_time;
	/* UNLINK's first press, 0: none */
	unsigned long unlink_asked;
} discord_account = { NONE, 0, 0 };

static void discord_widen(char const *text, wchar_t *wide, short size)
{
	short length = 0;

	for (; text && *text && length < size - 1; text++)
		wide[length++] = (wchar_t)(unsigned char)*text;
	wide[length] = 0;
}

static void discord_text_set(struct widget_instance *text_box, char const *text)
{
	wchar_t wide[ROW_TEXT_LENGTH];

	discord_widen(text, wide, ROW_TEXT_LENGTH);
	text_set(text_box, wide);
}

/* a state's word for a row's value */
static char const *discord_state_word(int state)
{
	switch (state)
	{
	case _player_link_state_linked:
		return "LINKED";
	case _player_link_state_waiting:
		return "WAITING";
	case _player_link_state_revoked:
		return "REVOKED";
	case _player_link_state_expired:
		return "EXPIRED";
	case _player_link_state_needed:
		return "LINK NEEDED";
	}
	return "NOT LINKED";
}

/* a master's host as its row and the panel show it: without "master.", to
fit */
static char const *discord_host_shown(char const *host)
{
	return strncmp(host, "master.", 7) || !host[7] ? host : host + 7;
}

static boolean discord_unlink_asked(void)
{
	return discord_account.unlink_asked &&
		system_milliseconds() - discord_account.unlink_asked < DISCORD_UNLINK_MILLISECONDS;
}

/* the master's index of a row's name ("op_link_master_<n>"), NONE if not
one */
static int discord_master_of(char const *name)
{
	int master;

	if (!name || strncmp(name, "op_link_master_", 15))
		return NONE;
	master = atoi(name + 15) - 1;
	return master >= 0 && master < PLAYER_LINK_MAXIMUM_MASTERS ? master : NONE;
}

/* each frame of Online's list and of the page's: the rows' values, the
masters' hosts, the rows shown */
static void discord_account_show(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *row;
	char name[PLAYER_LINK_NAME_SIZE + 1], code[PLAYER_LINK_CODE_SIZE], host[80], text[ROW_TEXT_LENGTH];
	int state, linked, masters, master, needed = player_link_needed_master();
	boolean page = named(list, "op_link_status", 0) != NULL;

	if (!named(list, "op_discord_account", 0) && !page)
		return;
	state = player_link_state(name, sizeof(name), code, sizeof(code), &linked, &masters);
	if (state != _player_link_state_linked && state != _player_link_state_waiting && needed >= 0)
		state = _player_link_state_needed;
	discord_text_set(named(list, "discord_account_value", 0), discord_state_word(state));
	if (!page)
		return;
	discord_text_set(named(list, "link_status_value", 0), discord_state_word(state));
	if ((row = named(list, "op_link_unlink", 0)) != NULL)
	{
		row->visible = state != _player_link_state_none && state != _player_link_state_needed;
		if (!row->visible)
			discord_account.unlink_asked = 0;
		discord_text_set(named(row, "link_unlink_value", 0), discord_unlink_asked() ? "SELECT AGAIN" : "");
	}
	for (master = 0; master < PLAYER_LINK_MAXIMUM_MASTERS; master++)
	{
		char row_name[32];
		int master_state = player_link_master_state(master, host, sizeof(host), name, sizeof(name), code,
			sizeof(code));

		snprintf(row_name, sizeof(row_name), "op_link_master_%d", master + 1);
		if (!(row = named(list, row_name, 0)))
			continue;
		row->visible = host[0] != 0;
		snprintf(row_name, sizeof(row_name), "link_master_%d_label", master + 1);
		discord_text_set(named(row, row_name, 0), discord_host_shown(host));
		if (master_state == _player_link_state_waiting)
			snprintf(text, sizeof(text), "CODE %s", code);
		else
			snprintf(text, sizeof(text), "%s", master_state == _player_link_state_none ? "LINK" :
				discord_state_word(master_state));
		snprintf(row_name, sizeof(row_name), "link_master_%d_value", master + 1);
		discord_text_set(named(row, row_name, 0), text);
	}
	/* (a page opened: on the master to link with, if a server asked) */
	if (screen && (screen->definition_tag_index != discord_account.screen_tag ||
		screen->creation_time != discord_account.screen_time))
	{
		discord_account.screen_tag = screen->definition_tag_index;
		discord_account.screen_time = screen->creation_time;
		discord_account.unlink_asked = 0;
		if (needed >= 0)
		{
			short index = 0;

			for (row = list->child; row; row = row->next, index++)
			{
				if (discord_master_of(row->name) == needed && row->visible)
				{
					list->focused_child = row;
					list->parameters.list.selected_index = index;
				}
			}
		}
	}
}

/* each frame of Advanced Graphics' list: the DISPLAY PEAK row's value,
the display's peak as the backend says it now (it follows the window to
another display, and the system's brightness) */
static void hdr_display_show(struct widget_instance *list)
{
	struct widget_instance *value = named(list, "hdr_display_value", 0);
	char text[ROW_TEXT_LENGTH];

	if (!value)
		return;
	halo_hdr_display_text(0, text, sizeof(text));
	discord_text_set(value, text);
}

/* the DISPLAY PEAK row's panel: its value and words, the display's peak and
SDR white now (the others': FALSE) */
static boolean hdr_display_panel(struct widget_instance *row, wchar_t *value, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char text[SETTINGS_PANEL_TEXT_LENGTH];

	if (!row->name || strcmp(row->name, "op_hdr_display") || !halo_hdr_display_text(0, text, sizeof(text)))
		return FALSE;
	discord_widen(text, value, ROW_TEXT_LENGTH);
	halo_hdr_display_text(1, text, sizeof(text));
	discord_widen(text, words, SETTINGS_PANEL_TEXT_LENGTH);
	*help = words;
	return TRUE;
}

/* Profile's ABOUT row's panel: this build (its number, or a local build's),
and in the note its label and the Xbox build the game's sources were
decompiled from, which the main menu used to show (the others': FALSE) */
static boolean about_panel(struct widget_instance *row, wchar_t *value, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char flavor[32], repository[160], version[96], text[SETTINGS_PANEL_TEXT_LENGTH];
	char *bar;
	long number;
	short length;

	if (!row->name || strcmp(row->name, "op_about"))
		return FALSE;
	main_build_identity(&number, flavor, sizeof(flavor), repository, sizeof(repository), version, sizeof(version));
	/* (the label's " | ", a comma) */
	if ((bar = strstr(version, " | ")) != NULL)
	{
		bar[0] = ',';
		memmove(bar + 1, bar + 2, strlen(bar + 2) + 1);
	}
	if (number > 0)
		snprintf(text, sizeof(text), "BUILD %ld", number);
	else
		snprintf(text, sizeof(text), "LOCAL BUILD");
	discord_widen(text, value, ROW_TEXT_LENGTH);
	settings_wide_copy(words, *help, SETTINGS_PANEL_TEXT_LENGTH);
	for (length = 0; words[length]; length++)
	{
	}
	snprintf(text, sizeof(text), "|%s. Made from the Xbox game's build 01.01.14.2342.", version);
	discord_widen(text, words + length, (short)(SETTINGS_PANEL_TEXT_LENGTH - length));
	*help = words;
	return TRUE;
}

/* (the server browsers' Filters page's SEARCH: below) */
static void browser_search_show(struct widget_instance *list);
static boolean browser_search_panel(struct widget_instance *row, wchar_t *value);

/* the panel's name, value and words for a row of the page (the others':
FALSE) */
static boolean discord_account_panel(struct widget_instance *row, wchar_t *name, wchar_t *value,
	wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char account[PLAYER_LINK_NAME_SIZE + 1], code[PLAYER_LINK_CODE_SIZE], host[80];
	char text[SETTINGS_PANEL_TEXT_LENGTH], shown[ROW_TEXT_LENGTH];
	int master = discord_master_of(row->name), state, linked, masters;

	if (row->name && !strcmp(row->name, "op_discord_account"))
	{
		/* (Online's row: its words its own, the value the account's state) */
		state = player_link_state(account, sizeof(account), code, sizeof(code), &linked, &masters);
		if (state == _player_link_state_linked)
			snprintf(shown, sizeof(shown), "%s", account);
		else
			snprintf(shown, sizeof(shown), "%s", discord_state_word(state != _player_link_state_waiting &&
				player_link_needed_master() >= 0 ? _player_link_state_needed : state));
		discord_widen(shown, value, ROW_TEXT_LENGTH);
		return TRUE;
	}
	if (master != NONE)
	{
		state = player_link_master_state(master, host, sizeof(host), account, sizeof(account), code, sizeof(code));
		discord_widen(discord_host_shown(host), name, ROW_TEXT_LENGTH);
		snprintf(shown, sizeof(shown), "%s", discord_state_word(state));
		/* (the panel's name is its host: the words say "this master server") */
		switch (state)
		{
		case _player_link_state_linked:
			snprintf(shown, sizeof(shown), "%s", account);
			snprintf(text, sizeof(text), "Linked as %s. The servers this master server lists let you in while you "
				"use a VPN. Select to link again.", account);
			break;
		case _player_link_state_waiting:
			snprintf(shown, sizeof(shown), "CODE %s", code);
			snprintf(text, sizeof(text), "Finish in your browser: check that the page shows the code %s, then "
				"sign in with Discord. Select to open the page again.", code);
			break;
		case _player_link_state_revoked:
			snprintf(text, sizeof(text), "This master server revoked the link. Select to link your Discord "
				"account again in your browser.");
			break;
		case _player_link_state_expired:
			snprintf(shown, sizeof(shown), "%s", account);
			snprintf(text, sizeof(text), "Linked as %s, but this master server has not renewed the link. It "
				"renews by itself when the master answers; select to link again.", account);
			break;
		case _player_link_state_needed:
			snprintf(text, sizeof(text), "A server you tried to join blocks VPNs and is listed by this master "
				"server. Select to link your Discord account with it in your browser, then join again.");
			break;
		default:
			snprintf(text, sizeof(text), "Select to open this master server's link page in your browser and "
				"sign in with Discord. The servers it lists then let you in while you use a VPN.");
			break;
		}
	}
	else if (row->name && !strcmp(row->name, "op_link_status"))
	{
		int needed = player_link_needed_master();

		state = player_link_state(account, sizeof(account), code, sizeof(code), &linked, &masters);
		snprintf(shown, sizeof(shown), "%s", discord_state_word(state));
		if (state == _player_link_state_linked)
		{
			snprintf(shown, sizeof(shown), "%s", account);
			snprintf(text, sizeof(text), "Linked as %s with %d of the %d master servers. The servers they list "
				"let you in while you use a VPN.", account, linked, masters);
		}
		else if (state == _player_link_state_waiting)
		{
			snprintf(shown, sizeof(shown), "CODE %s", code);
			snprintf(text, sizeof(text), "Finish in your browser: check that the page shows the code %s, then "
				"sign in with Discord. This page shows the link once the master server confirms it.", code);
		}
		else if (state == _player_link_state_expired)
		{
			snprintf(text, sizeof(text), "Linked as %s, but no master server has renewed the link. It renews "
				"by itself when one answers.", account);
		}
		else if (state == _player_link_state_revoked)
		{
			snprintf(text, sizeof(text), "A master server revoked the link. Select it below to link again.");
		}
		else if (needed >= 0 && player_link_master_state(needed, host, sizeof(host), account, sizeof(account),
			code, sizeof(code)) == _player_link_state_needed)
		{
			snprintf(shown, sizeof(shown), "%s", discord_state_word(_player_link_state_needed));
			snprintf(text, sizeof(text), "A server you tried to join blocks VPNs. Link your Discord account with "
				"%s, below, to join it while you use one.", host);
		}
		else
		{
			snprintf(text, sizeof(text), "Not linked. Select a master server below to link your Discord account "
				"with it in your browser: servers that block VPNs then let you in while you use one.");
		}
	}
	else if (row->name && !strcmp(row->name, "op_link_unlink") && discord_unlink_asked())
	{
		snprintf(shown, sizeof(shown), "SELECT AGAIN");
		snprintf(text, sizeof(text), "Select UNLINK again to forget the linked account on this device. You can "
			"link it again at any time.");
	}
	else
	{
		return FALSE;
	}
	discord_widen(shown, value, ROW_TEXT_LENGTH);
	discord_widen(text, words, SETTINGS_PANEL_TEXT_LENGTH);
	*help = words;
	return TRUE;
}

/* "port discord link" (a master's row): its link page opened in the browser */
static boolean discord_link(struct widget_instance *row)
{
	int master = discord_master_of(row ? row->name : NULL);

	if (master == NONE || !player_link_begin(master))
	{
		ui_play_audio_feedback_sound(SOUND_ERROR);
		return FALSE;
	}
	return TRUE;
}

/* "port discord unlink": the first press asks, the second forgets */
static boolean discord_unlink(void)
{
	if (!discord_unlink_asked())
	{
		discord_account.unlink_asked = system_milliseconds();
		if (!discord_account.unlink_asked)
			discord_account.unlink_asked = 1;
		return TRUE;
	}
	discord_account.unlink_asked = 0;
	player_link_unlink();
	return TRUE;
}

/* the panel: the row chosen's name, its value in large type and its words
(after a "|", a note under them); Quality's pictures, the preset shown
framed, or a picture of the value shown (settings_value_picture_show); the
tabs' and the buttons' own words */
static void settings_panel_show(struct widget_instance *list, short tab)
{
	struct widget_instance *panel = list->parameters.list.extended_description;
	struct widget_instance *row = list->focused_child;
	struct widget_instance *text, *note;
	long text_font, note_font;
	short text_width, note_width;
	wchar_t name[ROW_TEXT_LENGTH] = { 0 }, value[ROW_TEXT_LENGTH] = { 0 };
	wchar_t words[SETTINGS_PANEL_TEXT_LENGTH] = { 0 }, wrapped[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t const *help = NULL;
	wchar_t *bar;
	short still = NONE, length, lines, index;
	boolean stills = FALSE, picture = FALSE;
	struct widget_instance *row_spinner = NULL;
	char const *const stills_names[] = { "panel_classic", "panel_enhanced" };

	if (!panel || !row || !row->name)
		return;
	if (!strcmp(row->name, "tabs") && row->focused_child)
	{
		index = sibling_index(row->focused_child);
		settings_wide_copy(name, L"SETTINGS", ROW_TEXT_LENGTH);
		settings_wide_copy(value, settings_string(SETTINGS_SHARED "tab_names", index), ROW_TEXT_LENGTH);
		help = settings_string(SETTINGS_SHARED "tab_about", index);
	}
	else if (!strcmp(row->name, "button_bar") && row->focused_child)
	{
		index = sibling_index(row->focused_child);
		settings_wide_copy(name, settings_widget_text(row->focused_child, NONE), ROW_TEXT_LENGTH);
		help = settings_string(SETTINGS_SHARED "button_help", (short)((tab == NONE ? 3 : 0) + index));
	}
	else if (!strncmp(row->name, "op_", 3))
	{
		struct widget_instance *label = row->child && row->child->type == 1 ? row->child : NULL;
		struct widget_instance *spinner;
		struct player_profile *profile = player_ui_get_edit_player_profile();

		settings_wide_copy(name, settings_widget_text(label, NONE), ROW_TEXT_LENGTH);
		for (length = 0; name[length]; length++)
		{
		}
		/* (a link's name without its sign) */
		while (length > 0 && (name[length - 1] == SETTINGS_LINK_SIGN || name[length - 1] == ' '))
			name[--length] = 0;
		for (spinner = row->child; spinner && spinner->type != 2; spinner = spinner->next)
		{
		}
		if (spinner)
		{
			settings_wide_copy(value, settings_widget_text(spinner, spinner->parameters.list.selected_index),
				ROW_TEXT_LENGTH);
		}
		else if (settings_slider_of(row) != NONE)
		{
			/* (a slider's: its number) */
			char number[32];

			settings_slider_text(settings_slider_of(row), settings_slider_value(settings_slider_of(row)), number,
				sizeof(number));
			for (length = 0; number[length] && length < ROW_TEXT_LENGTH - 1; length++)
				value[length] = (wchar_t)number[length];
			value[length] = 0;
		}
		else if (profile && !strcmp(row->name, "op_name"))
		{
			ustrncpy(value, profile->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
			value[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
		}
		else if (profile && !strcmp(row->name, "op_color"))
		{
			string_get("pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\colors_list",
				profile->primary_color_index, value);
		}
		help = settings_row_help(label);
		/* (the Discord account's page: its words of the state now) */
		discord_account_panel(row, name, value, &help);
		/* (the display's peak: its words of the display now) */
		hdr_display_panel(row, value, &help);
		/* (Profile's ABOUT: this build) */
		about_panel(row, value, &help);
		/* (the browsers' SEARCH: its words) */
		browser_search_panel(row, value);
		/* (Button Remapping's: a button being taken, a replacement asked) */
		if (pad_editor_page(screen_of(list)))
			pad_editor_panel(row, name, value, &help);
		row_spinner = spinner;
		/* (Quality: CLASSIC or ENHANCED framed, neither for CUSTOM) */
		if (!strcmp(row->name, "op_graphics") && spinner)
		{
			struct pc_menu_setting *setting = pc_menu_setting_get(spinner->definition_tag_index);
			short shown = spinner->parameters.list.selected_index;

			stills = TRUE;
			if (setting && shown >= 0 && shown < setting->value_count)
				still = !strcmp(setting->values[shown], "classic") ? 0 : !strcmp(setting->values[shown], "enhanced") ? 1 : NONE;
		}
	}
	picture = settings_value_picture_show(panel, row, row_spinner ? row_spinner->parameters.list.selected_index :
		settings_slider_of(row) != NONE ? settings_slider_stop_nearest(settings_slider_of(row)) : NONE) && !stills;
	settings_wide_copy(words, help, SETTINGS_PANEL_TEXT_LENGTH);
	for (bar = words; *bar && *bar != '|'; bar++)
	{
	}
	if (*bar)
		*bar++ = 0;
	text_set_length(named(panel, "panel_name", 0), name, ROW_TEXT_LENGTH);
	text_set_length(named(panel, "panel_value", 0), value, ROW_TEXT_LENGTH);
	/* (wrapped in the text boxes' fonts, taken from their definitions in the
	map loaded now: a font's tag index is not kept, as the next map's tags
	are numbered again) */
	text = named(panel, "panel_text", 0);
	note = named(panel, "panel_note", 0);
	text_font = ui_widget_port_text_box_font(text, &text_width);
	note_font = ui_widget_port_text_box_font(note, &note_width);
	lines = settings_text_wrap(words, wrapped, SETTINGS_PANEL_TEXT_LENGTH, text_font, text_width);
	text_set_length(text, wrapped, SETTINGS_PANEL_TEXT_LENGTH);
	/* (under the name, or under Quality's pictures) */
	if (text && named(panel, "panel_name", 0))
		text->vertical_offset = (short)(named(panel, "panel_name", 0)->vertical_offset +
			(stills ? SETTINGS_PANEL_TEXT_UNDER_STILLS : picture ? SETTINGS_PANEL_TEXT_UNDER_PICTURE :
			SETTINGS_PANEL_TEXT_TOP));
	settings_text_wrap(bar, wrapped, SETTINGS_PANEL_TEXT_LENGTH, note_font, note_width);
	text_set_length(note, wrapped, SETTINGS_PANEL_TEXT_LENGTH);
	/* (the note a line under the words) */
	if (note && text)
	{
		short line = 12;

		if (text_font != NONE)
		{
			rectangle2d bounds, text_bounds, cursor;
			static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };

			bounds.x0 = 0;
			bounds.y0 = 0;
			bounds.x1 = 0x3FFF;
			bounds.y1 = 480;
			/* (a line's height: two lines' less one's) */
			draw_string_set_draw_mode(text_font, NONE, 0, 0, &white);
			draw_unicode_string_compute_bounds(&bounds, L"X\r\nX", &text_bounds, &cursor);
			line = (short)(text_bounds.y1 - text_bounds.y0);
			draw_unicode_string_compute_bounds(&bounds, L"X", &text_bounds, &cursor);
			line = (short)(line - (text_bounds.y1 - text_bounds.y0));
		}
		note->vertical_offset = (short)(text->vertical_offset + lines * line + line / 2);
	}
	for (index = 0; index < 2; index++)
	{
		char frame_name[32];
		struct widget_instance *widget;

		if ((widget = named(panel, stills_names[index], 0)) != NULL)
			widget->visible = stills;
		snprintf(frame_name, sizeof(frame_name), "%s_frame", stills_names[index]);
		if ((widget = named(panel, frame_name, 0)) != NULL)
		{
			widget->visible = stills;
			widget->animation.current_frame_index = (short)(still == index);
		}
		snprintf(frame_name, sizeof(frame_name), "%s_caption", stills_names[index]);
		if ((widget = named(panel, frame_name, 0)) != NULL)
			widget->visible = stills;
	}
}

/* "port settings page": each frame of a tab's or a page's list */
static void settings_page_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	short tab = settings_tab_of(screen);

	/* (a page made: what was kept as it opened, for its Cancel) */
	if (screen != settings_page.screen || screen->definition_tag_index != settings_page.screen_tag ||
		screen->creation_time != settings_page.screen_time)
	{
		settings_page.screen = screen;
		settings_page.screen_tag = screen->definition_tag_index;
		settings_page.screen_time = screen->creation_time;
		if (tab == NONE)
			settings_snapshot = settings_pending;
		/* (back from the profile manager: player 1's profile edited, the
		one chosen there; none, if every profile was deleted) */
		if (tab != NONE && settings_page.profiles_opened)
		{
			settings_page.profiles_opened = FALSE;
			if (!player_ui_get_edit_player_profile())
				pc_menu_profile_edit_begin();
		}
	}
	video_rows_show(list);
	refresh_rate_row_show(list);
	hud_button_style_row_show(list);
	max_fps_row_show(list);
	menu_cursor_row_show(list);
	gamma_row_show(list);
	settings_rows_show(list);
	discord_account_show(list);
	hdr_display_show(list);
	pad_editor_update(list);
	graphics_preset_show(list);
	sim_rate_row_show(list);
	settings_each(screen, setting_stash);
	settings_sliders_show(list);
	browser_search_show(list);
	settings_rows_pack(list, tab != NONE);
	settings_focus_shown(list);
	settings_tabs_show(list, tab);
	settings_panel_show(list, tab);
}

/* ---------- Change Color: the profile's colour, from a list of the
game's colours (more than its rows: it scrolls), by the Xbox's names for
its spinner's functions */

#define COLOR_COUNT 18
#define COLOR_ROWS 11

static struct
{
	/* the colour on the first row, and the colour chosen */
	short first;
	short color;
} color_list;

static void color_row_text(short row, wchar_t *text)
{
	string_get("pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\colors_list",
		(short)(color_list.first + row), text);
}

/* "color picker menu initialize": on the profile's colour */
static boolean color_list_initialize(struct widget_instance *list)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();

	if (!profile)
		return FALSE;
	color_list.color = (short)PIN(profile->primary_color_index, 0, COLOR_COUNT - 1);
	color_list.first = (short)PIN(color_list.color - COLOR_ROWS / 2, 0, COLOR_COUNT - COLOR_ROWS);
	focus_row(list, (short)(color_list.color - color_list.first));
	return TRUE;
}

/* (ui_widget.c) whether the pointer moved the menus' focus last */
boolean ui_widget_port_pointer_focused(void);
boolean ui_widget_port_hover_focused(void);

/* Whether a list whose focus is on its first or last row moves on: when the
d-pad put it there. The pointer resting there would make it move every
frame; the wheel scrolls it instead. */
static boolean list_scrolls_at_end(void)
{
	return !ui_widget_port_pointer_focused();
}

/* a list of more items than its rows: the row chosen kept off its ends
while there are more past them, the list moving instead; the item chosen,
or NONE (its buttons) */
static short list_scroll(struct widget_instance *list, short *first, short count, short rows)
{
	short row = focused_row(list);

	if (row == rows - 1 && *first + rows < count && list_scrolls_at_end())
	{
		(*first)++;
		focus_row(list, --row);
	}
	else if (row == 0 && *first > 0 && list_scrolls_at_end())
	{
		(*first)--;
		focus_row(list, ++row);
	}
	return row == NONE || row >= rows ? NONE : (short)(*first + row);
}

/* "color picker update": the list scrolled on at its ends, the colour's
name and picture */
static void color_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *widget;
	short color = list_scroll(list, &color_list.first, COLOR_COUNT, COLOR_ROWS);

	if (color != NONE)
		color_list.color = color;
	rows_update(list, COLOR_ROWS, color_row_text);
	if ((widget = named(description, "color_right_name", 0)) != NULL)
		widget->parameters.text_box.string_list_index = color_list.color;
	if ((widget = named(description, "color_right_pic", 0)) != NULL)
		widget->animation.current_frame_index = color_list.color;
	profile_name_show(description);
}

/* "color picker select color" */
static boolean color_choose(void)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();

	if (!profile)
		return FALSE;
	profile->primary_color_index = color_list.color;
	return TRUE;
}

/* ---------- Edit Profile Settings: its picture of Gamepad Setup, the
profile's button settings as the Xbox's Controller Setup shows them */

/* the picture's frames (the bitmap's: tools/port_settings.py's
BITMAP_FRAMES): Gamepad Setup's row's, as the list numbers its rows
(player_profile_edit_select_menu_update_extended_description), and after
the PC version's nine and the VR Setup row's (FRAMES_ADDED) the Xbox's five
of the button settings */
#define PROFILE_GAMEPAD_FRAME 2
#define PROFILE_FIRST_LAYOUT_FRAME 10
#define PROFILE_LAYOUT_FRAMES (_button_preset_swap_b_and_right_thumb + 1)

/* "port gamepad layout preview" (the picture's own): on Gamepad Setup's
row, the edited profile's button settings; the port's own (Bumper Jumper),
of which the Xbox has no picture, keeps the row's */
static void profile_gamepad_layout(struct widget_instance *picture)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();
	short frame = picture->animation.current_frame_index;

	if (frame == PROFILE_GAMEPAD_FRAME ||
		(frame >= PROFILE_FIRST_LAYOUT_FRAME && frame < PROFILE_FIRST_LAYOUT_FRAME + PROFILE_LAYOUT_FRAMES))
	{
		byte preset = profile ? profile->controller_settings.button_preset : (byte)_button_preset_standard;

		picture->animation.current_frame_index = preset < PROFILE_LAYOUT_FRAMES ?
			(short)(PROFILE_FIRST_LAYOUT_FRAME + preset) : PROFILE_GAMEPAD_FRAME;
	}
}

/* ---------- Profiles: the player profiles, and a row to make one (as the
PC version's list has them, but by the Xbox's names for its spinner's
functions); choosing one makes it player 1's, the profile the campaign and
Settings use; deleting one asks first */

#define PROFILE_ROWS 11
#define PROFILE_DEFAULT_BIT 0x40000000UL

static struct
{
	long indices[MAXIMUM_PROFILES];
	struct player_profile profiles[MAXIMUM_PROFILES];
	/* the profiles; the row after them makes a new one */
	short count;
	short first;
	short chosen;
	long to_delete;
} profile_list;

/* the profiles, read again when they are not those read last */
static void profile_list_read(boolean always)
{
	long indices[MAXIMUM_PROFILES];
	word count = MAXIMUM_PROFILES;
	short index, valid = 0;

	player_profiles_enumerate_available_to_local_player_index(NONE, &count, indices, FALSE);
	for (index = 0; index < (short)count; index++)
	{
		if ((unsigned long)indices[index] & PROFILE_VALID_BIT)
			indices[valid++] = indices[index];
	}
	if (!always && valid == profile_list.count &&
		!memcmp(indices, profile_list.indices, valid * sizeof(indices[0])))
	{
		return;
	}
	profile_list.count = 0;
	for (index = 0; index < valid; index++)
	{
		if (player_profile_get(indices[index], &profile_list.profiles[profile_list.count]))
			profile_list.indices[profile_list.count++] = indices[index];
	}
	profile_list.chosen = (short)PIN(profile_list.chosen, 0, profile_list.count);
}

static short profile_list_rows(void)
{
	return (short)MIN(profile_list.count + 1, PROFILE_ROWS);
}

/* "player profile list initialize": on player 1's profile */
static boolean profile_list_initialize(struct widget_instance *list)
{
	long active = player_ui_get_active_player_profile_index(0);
	short index;

	profile_list_read(TRUE);
	if (active == NONE)
		active = player_ui_get_player1_last_used_profile_index();
	profile_list.chosen = 0;
	for (index = 0; index < profile_list.count; index++)
	{
		if (profile_list.indices[index] == active)
			profile_list.chosen = index;
	}
	profile_list.first = (short)PIN(profile_list.chosen - PROFILE_ROWS / 2, 0,
		MAX(0, profile_list.count + 1 - PROFILE_ROWS));
	focus_row(list, (short)(profile_list.chosen - profile_list.first));
	return TRUE;
}

static void profile_row_text(short row, wchar_t *text)
{
	short item = (short)(profile_list.first + row);

	if (item < profile_list.count)
	{
		ustrncpy(text, profile_list.profiles[item].player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
		text[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
	}
	else
	{
		string_get("pc\\main_menu\\player_profiles_select\\profile_description_labels", 4, text);
	}
}

static void visible_set(struct widget_instance *widget, boolean visible)
{
	if (widget)
		widget->visible = visible;
}

/* "3wide player profile list update": the rows, and the profile chosen:
its name, colour, current level, best difficulty and controls */
static void profile_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *widget;
	short item;
	boolean profile;

	profile_list_read(FALSE);
	item = list_scroll(list, &profile_list.first, (short)(profile_list.count + 1), profile_list_rows());
	if (item != NONE)
		profile_list.chosen = item;
	rows_update(list, profile_list_rows(), profile_row_text);
	profile = profile_list.chosen < profile_list.count;
	text_set(named(description, "player_profile_right_name", 0),
		profile ? profile_list.profiles[profile_list.chosen].player_name : L"");
	visible_set(named(description, "empty_profile_slot_text", 0), !profile);
	visible_set(named(description, "qtr_screen_profile_color_pic", 0), profile);
	visible_set(named(description, "current_level_label", 0), profile);
	visible_set(named(description, "current_level", 0), profile);
	visible_set(named(description, "joystick_controls_label", 0), profile);
	visible_set(named(description, "joystick_controls", 0), profile);
	visible_set(named(description, "skill_level_label", 0), FALSE);
	visible_set(named(description, "skill_level", 0), FALSE);
	if (profile)
	{
		struct player_profile *chosen = &profile_list.profiles[profile_list.chosen];
		short level, difficulty;

		if ((widget = named(description, "qtr_screen_profile_color_pic", 0)) != NULL)
			widget->animation.current_frame_index = (short)PIN(chosen->primary_color_index, 0, COLOR_COUNT - 1);
		if ((widget = named(description, "current_level", 0)) != NULL)
		{
			widget->parameters.text_box.string_list_index =
				(short)PIN(chosen->last_single_player_map_played, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);
		}
		if ((widget = named(description, "joystick_controls", 0)) != NULL)
			widget->parameters.text_box.string_list_index = chosen->controller_settings.invert_look ? 1 : 0;
		player_profile_get_highest_completed_solo_level(chosen, &level, &difficulty);
		if (level != NONE && (widget = named(description, "skill_level", 0)) != NULL)
		{
			widget->parameters.text_box.string_list_index = difficulty;
			widget->visible = TRUE;
			visible_set(named(description, "skill_level_label", 0), TRUE);
		}
	}
	profile_name_show(description);
}

/* "profile manager select": the profile chosen made player 1's (and the
one used last); FALSE for the row that makes one (its handler then asks) */
static boolean profile_choose(short controller)
{
	struct player_profile profile;

	if (profile_list.chosen >= profile_list.count ||
		!player_profile_get(profile_list.indices[profile_list.chosen], &profile))
	{
		return FALSE;
	}
	player_ui_set_active_player_profile(0, profile_list.indices[profile_list.chosen], &profile);
	player_ui_set_single_player_local_player_controller(0, controller);
	player_ui_remember_player1_profile(TRUE);
	return TRUE;
}

/* "request del player profile" (before its question) */
static boolean profile_delete_request(void)
{
	if (profile_list.chosen >= profile_list.count)
		return campaign_fail();
	profile_list.to_delete = profile_list.indices[profile_list.chosen];
	return TRUE;
}

/* "final del player profile" (its OK): deleted, and if it was player 1's,
the first left is */
static boolean profile_delete(void)
{
	long index = profile_list.to_delete;
	boolean active = index == player_ui_get_active_player_profile_index(0);

	profile_list.to_delete = NONE;
	if (index == NONE || ((unsigned long)index & PROFILE_DEFAULT_BIT))
		return campaign_fail();
	player_profile_delete(index);
	profile_list_read(TRUE);
	if (active)
	{
		struct player_profile profile;

		if (profile_list.count && player_profile_get(profile_list.indices[0], &profile))
		{
			player_ui_set_active_player_profile(0, profile_list.indices[0], &profile);
			player_ui_remember_player1_profile(TRUE);
		}
		else
		{
			memset(&profile, 0, sizeof(profile));
			player_ui_set_active_player_profile(0, NONE, &profile);
		}
	}
	return TRUE;
}

/* ---------- Co-op: the campaign for two players on this machine, in split
screen (the Xbox's Cooperative Play, which the PC version has not):
Multiplayer's CO-OP CAMPAIGN ("port coop begin"), player 2's profile, chosen
with player 2's controller ("port coop player 2"), then New Game's levels
(those either has reached) and difficulty, with either player's controller
(ui_widget.c, widget_takes_events_of_controller). The main menu and
Multiplayer go back to one player (main_menu_initialize,
multiplayer_type_menu_initialize).
With one gamepad, it is player 2's (pc_menu_split_players) */

/* "port coop begin": two players, player 1 on its profile (campaign_profile)
and the controller that chose co-op */
static boolean coop_begin(short controller)
{
	struct player_profile profile;

	player_spawn_count = 1;
	player_ui_reset_single_player_local_player_controllers();
	if (!campaign_profile(controller, &profile))
	{
		/* (said, the menu staying as it was: its item opened nothing else,
		and failing left a blank menu) */
		display_error_text_deferred(L"Choose a player profile\r\nfirst (Profiles, on the\r\nmain menu).", NONE);
		return campaign_fail();
	}
	player_spawn_count = 2;
	return TRUE;
}

/* "port coop player 2 list initialize": on player 2's profile, else the first
that is not player 1's */
static boolean coop_player2_list_initialize(struct widget_instance *list)
{
	long player1 = player_ui_get_active_player_profile_index(0);
	long player2 = player_ui_get_active_player_profile_index(1);
	short index;

	profile_list_read(TRUE);
	profile_list.chosen = 0;
	for (index = profile_list.count - 1; index >= 0; index--)
	{
		if (profile_list.indices[index] != player1)
			profile_list.chosen = index;
	}
	for (index = 0; index < profile_list.count && player2 != NONE; index++)
	{
		if (profile_list.indices[index] == player2)
			profile_list.chosen = index;
	}
	profile_list.first = (short)PIN(profile_list.chosen - PROFILE_ROWS / 2, 0,
		MAX(0, profile_list.count + 1 - PROFILE_ROWS));
	focus_row(list, (short)(profile_list.chosen - profile_list.first));
	return TRUE;
}

/* "port coop player 2": the profile chosen made player 2's, on the controller
that chose it, which must not be player 1's */
static boolean coop_player2_choose(short controller)
{
	struct player_profile profile;

	if (player_spawn_count < 2 || profile_list.chosen >= profile_list.count ||
		!player_profile_get(profile_list.indices[profile_list.chosen], &profile))
	{
		return campaign_fail();
	}
	if (controller == player_ui_get_single_player_local_player_controller(0))
	{
		display_error_text_deferred(L"Player 2 chooses their\r\nprofile with their own\r\ncontroller.",
			NONE);
		return campaign_fail();
	}
	player_ui_set_single_player_local_player_controller(1, controller);
	player_ui_set_active_player_profile(1, profile_list.indices[profile_list.chosen], &profile);
	return TRUE;
}

/* ---------- Controls Setup: the keyboard and mouse's controls
(port/linux/include/halo_keyboard.h), shown a group at a time, two
bindings each */

#define CONTROL_BINDINGS 2
#define CONTROL_ROWS 7
#define CONTROL_NAME_LENGTH 32

static struct
{
	char const *setting;
	wchar_t const *label;
	short group;
} const controls[] =
{
	{ "controls.move_forward", L"MOVE FORWARD", 0 },
	{ "controls.move_backward", L"MOVE BACKWARD", 0 },
	{ "controls.strafe_left", L"STRAFE LEFT", 0 },
	{ "controls.strafe_right", L"STRAFE RIGHT", 0 },
	{ "controls.jump", L"JUMP", 0 },
	{ "controls.crouch", L"CROUCH", 0 },
	{ "controls.fire", L"FIRE", 1 },
	{ "controls.throw_grenade", L"THROW GRENADE", 1 },
	{ "controls.melee", L"MELEE", 1 },
	{ "controls.reload", L"RELOAD", 1 },
	{ "controls.zoom", L"ZOOM", 1 },
	{ "controls.switch_weapon", L"SWITCH WEAPON", 1 },
	{ "controls.switch_grenade", L"SWITCH GRENADE", 1 },
	{ "controls.action", L"ACTION", 2 },
	{ "controls.flashlight", L"FLASHLIGHT", 2 },
	{ "controls.scoreboard", L"SHOW SCORES", 2 },
	{ "controls.pause", L"PAUSE MENU", 2 },
	{ "controls.screenshot", L"SCREENSHOT", 2 },
	{ "controls.push_to_talk", L"PUSH TO TALK", 3 },
	{ "controls.chat", L"CHAT", 3 },
	{ "controls.team_chat", L"TEAM CHAT", 3 },
	{ "controls.party_accept", L"ACCEPT INVITE", 3 },
	{ "controls.vote_yes", L"VOTE YES", 3 },
	{ "controls.vote_no", L"VOTE NO", 3 },
};

static struct
{
	/* the bindings shown, until OK writes them */
	char bindings[NUMBEROF(controls)][CONTROL_BINDINGS][CONTROL_NAME_LENGTH];
	/* the binding left and right choose, and the one being set */
	short slot;
	short capturing_control;
	short capturing_slot;
} controls_screen = { { { { 0 } } }, 0, NONE, 0 };

/* a setting's bindings ("Left Ctrl, C") into the control's */
static void control_bindings_read(short control, char const *text)
{
	short slot;

	for (slot = 0; slot < CONTROL_BINDINGS; slot++)
	{
		char *binding = controls_screen.bindings[control][slot];
		unsigned int length;

		while (*text == ' ' || *text == ',')
			text++;
		length = (unsigned int)strcspn(text, ",");
		if (length >= CONTROL_NAME_LENGTH)
			length = CONTROL_NAME_LENGTH - 1;
		memcpy(binding, text, length);
		binding[length] = 0;
		while (length && binding[length - 1] == ' ')
			binding[--length] = 0;
		text += strcspn(text, ",");
	}
}

static void control_bindings_text(short control, char *text, unsigned int size)
{
	char const *first = controls_screen.bindings[control][0];
	char const *second = controls_screen.bindings[control][1];

	snprintf(text, size, "%s%s%s", *first ? first : second, *first && *second ? ", " : "", *first ? second : "");
}

/* "controls screen init" (and "controls screen defaults": the defaults) */
static boolean controls_load(boolean defaults)
{
	short control;

	for (control = 0; control < NUMBEROF(controls); control++)
	{
		char text[128];

		if (defaults)
			config_default(controls[control].setting, text, sizeof(text));
		else
			snprintf(text, sizeof(text), "%s", config_string(controls[control].setting));
		control_bindings_read(control, text);
	}
	controls_screen.capturing_control = NONE;
	return TRUE;
}

/* "controls screen change set" (OK) */
static boolean controls_save(void)
{
	short control;
	boolean result = TRUE;

	for (control = 0; control < NUMBEROF(controls); control++)
	{
		char text[128];

		control_bindings_text(control, text, sizeof(text));
		if (strcmp(text, config_string(controls[control].setting)))
		{
			if (config_write(controls[control].setting, text))
				platform_log("menus: %s = %s", controls[control].setting, text);
			else
				result = FALSE;
		}
	}
	return result;
}

static struct widget_instance *control_row(struct widget_instance *widget)
{
	for (; widget; widget = widget->parent)
	{
		if (!strncmp(widget->name, "op_command_", 11))
			return widget;
	}
	return NULL;
}

/* the control on the row (op_command_<n>) in the group shown, else NONE */
static short control_of_row(struct widget_instance *row, short group)
{
	short wanted = row ? (short)(atoi(row->name + 11) - 1) : NONE;
	short control;

	for (control = 0; control < NUMBEROF(controls) && wanted >= 0; control++)
	{
		if (controls[control].group == group && wanted-- == 0)
			return control;
	}
	return NONE;
}

static short controls_group(struct widget_instance *screen)
{
	struct widget_instance *spinner = named(screen, "group_spinner", 0);

	return spinner ? spinner->parameters.list.selected_index : 0;
}

/* the binding of a row at a place on the screen: the second from its left
edge on, else the first */
static short control_slot_at(struct widget_instance *row, short x)
{
	struct widget_instance *second = named(row, "command_binding", 1);

	return (short)(second && x >= widget_screen_x(second));
}

/* "controls begin binding" (A on a row, or a click on it: the binding
clicked): the next key or button pressed */
static boolean control_capture_begin(struct widget_instance *widget)
{
	struct widget_instance *row = control_row(widget);
	short control = control_of_row(row, controls_group(screen_of(widget)));
	short x, y;

	if (control == NONE)
		return FALSE;
	if (ui_widget_port_take_click(&x, &y))
		controls_screen.slot = control_slot_at(row, x);
	controls_screen.capturing_control = control;
	controls_screen.capturing_slot = controls_screen.slot;
	platform_binding_capture_begin();
	return TRUE;
}

/* the key or button taken (then bound to no other control), or cleared */
static void control_capture_poll(void)
{
	short control = controls_screen.capturing_control;
	short slot = controls_screen.capturing_slot;
	int input = -1;
	int result;

	if (control == NONE || !(result = platform_binding_capture_poll(&input)))
		return;
	controls_screen.capturing_control = NONE;
	if (result == 1)
	{
		char name[CONTROL_NAME_LENGTH];
		short other, other_slot;

		halo_input_name(input, name, sizeof(name));
		if (!*name)
			return;
		for (other = 0; other < NUMBEROF(controls); other++)
		{
			for (other_slot = 0; other_slot < CONTROL_BINDINGS; other_slot++)
			{
				if (!_stricmp(controls_screen.bindings[other][other_slot], name))
					controls_screen.bindings[other][other_slot][0] = 0;
			}
		}
		snprintf(controls_screen.bindings[control][slot], CONTROL_NAME_LENGTH, "%s", name);
	}
	else if (result == 2)
	{
		controls_screen.bindings[control][slot][0] = 0;
	}
}

/* "controls update menu": the group's rows, their bindings (the one left
and right choose, or the pointer is over, marked on the row chosen), and the
help */
static void controls_update(struct widget_instance *list)
{
	short group = controls_group(list);
	struct widget_instance *row;
	struct widget_instance *focused_row = control_row(list->focused_child);
	short x, y;

	control_capture_poll();
	if (ui_widget_port_take_point(&x, &y) && focused_row && controls_screen.capturing_control == NONE)
		controls_screen.slot = control_slot_at(focused_row, x);
	for (row = list->child; row; row = row->next)
	{
		short control = control_of_row(row, group);
		short slot;

		if (strncmp(row->name, "op_command_", 11))
			continue;
		row->visible = control != NONE;
		if (control == NONE)
		{
			if (row == focused_row)
				focus_row(list, 0);
			continue;
		}
		text_set(named(row, "command_label", 0), controls[control].label);
		for (slot = 0; slot < CONTROL_BINDINGS; slot++)
		{
			char const *binding = controls_screen.bindings[control][slot];
			wchar_t text[ROW_TEXT_LENGTH];
			char name[CONTROL_NAME_LENGTH];
			char shown[64];
			short index;

			/* (the key as the keyboard's layout labels it: xinput_sdl.c) */
			if (*binding)
				halo_input_shown_name(binding, name, sizeof(name));
			else
				snprintf(name, sizeof(name), "%s", "-");
			if (controls_screen.capturing_control == control && controls_screen.capturing_slot == slot)
				snprintf(shown, sizeof(shown), "%s", "PRESS A KEY");
			else if (row == focused_row && controls_screen.slot == slot)
				snprintf(shown, sizeof(shown), "> %s <", name);
			else
				snprintf(shown, sizeof(shown), "%s", name);
			for (index = 0; shown[index] && index < ROW_TEXT_LENGTH - 1; index++)
				text[index] = (wchar_t)(unsigned char)shown[index];
			text[index] = 0;
			text_set(named(row, "command_binding", slot), text);
		}
	}
	if (list->parameters.list.extended_description)
	{
		list->parameters.list.extended_description->parameters.text_box.string_list_index =
			controls_screen.capturing_control != NONE ? 2 : focused_row ? 1 : 0;
	}
}

/* ---------- Mods (the main menu's: tools/port_settings.py's _mods), laid
out as the lobby is: the mods folder's mods, a row each (op_mod_<n>), as a
resource pack list is, the one that plays over the others at the top (the
load order upside down: the mod loaded last first), turned on or off and
moved up or down there, and in the frame beside them the mod chosen's
details; OK keeps it (port/linux/src/port_mods.c's load_order.toml) and
plays them on the map now (mods.c), Cancel forgets it */

#define MOD_ROWS 11
#define MOD_DETAILS_LENGTH 1024
/* the details' lines: as many as fit down the frame */
#define MOD_DETAILS_LINES 13
/* (from the shadow of each box's last character) */
#define MOD_TEXT_MARGIN 8

/* the text boxes whose lines are measured: the details' mod name and the
rest, and the rows' names */
enum
{
	_mod_details_name,
	_mod_details_text,
	_mod_row_name,
	NUMBER_OF_MOD_TEXT_BOXES
};

/* the text boxes' names, and as many characters as fit across each when
its font is not known */
static char const *const mod_text_box_names[NUMBER_OF_MOD_TEXT_BOXES] = { "mod_details_name", "mod_details_text",
	"mod_name" };
static short const mod_text_box_characters[NUMBER_OF_MOD_TEXT_BOXES] = { 17, 30, 24 };

/* (port/linux/src/port_mods.c, port_mods.h's) */
enum
{
	_port_mod_fine,
	_port_mod_out_of_order,
	_port_mod_incompatible,
	_port_mod_unplayable,
};

long port_mods_count(void);
long port_mods_at(long position);
char const *port_mods_name(long index);
int port_mods_on(long index);
int port_mods_status(long index, char *text, unsigned long size);
int port_mods_details(long index, char *text, unsigned long size);
long port_mods_move(long position, long places);
void port_mods_turn(long index, int on);
int port_mods_save(void);
void port_mods_revert(void);
/* (mods.c) */
void mods_apply(void);

static struct
{
	/* the place on the list (0 the top: the mod loaded last) of the first
	row, and of the mod chosen last (MOVE UP and MOVE DOWN's, which take the
	focus from the rows) */
	short first;
	short chosen;
	/* the text boxes' fonts and widths, which their lines are measured in:
	taken from their definitions each update (mods_fonts_take), as a font's
	tag index is good only in the map loaded now; NONE without one */
	long fonts[NUMBER_OF_MOD_TEXT_BOXES];
	short widths[NUMBER_OF_MOD_TEXT_BOXES];
	/* what the details show (they are wrapped again when it changes), and
	the mod each row's name is of, in which font (NONE: none yet) */
	char shown[128];
	long row_mods[MOD_ROWS];
	long row_fonts[MOD_ROWS];
} mods_screen;

/* the row (op_mod_<n>) a widget is or is in, else NULL */
static struct widget_instance *mod_row(struct widget_instance *widget)
{
	for (; widget; widget = widget->parent)
	{
		if (!strncmp(widget->name, "op_mod_", 7))
			return widget;
	}
	return NULL;
}

/* the place on the list of the mod on a row, else NONE */
static short mod_row_place(struct widget_instance *row)
{
	short place = row ? (short)(mods_screen.first + atoi(row->name + 7) - 1) : NONE;

	return place >= 0 && place < port_mods_count() ? place : NONE;
}

/* the mod at a place on the list (its place in the load order counted from
the end), else NONE */
static long mod_at_place(short place)
{
	long count = port_mods_count();

	return place >= 0 && place < count ? port_mods_at(count - 1 - place) : NONE;
}

/* UTF-8 text (mod.toml's) as the menus' (a line break the game's \r\n; a
character the menus' UTF-16 cannot hold, or a broken one, a ?) */
static void mods_wide_text(char const *text, wchar_t *wide, short length)
{
	short index = 0;

	while (*text && index < length - 2)
	{
		unsigned char first = (unsigned char)*text++;
		unsigned long character = first;
		short following = first >= 0xF0 ? 3 : first >= 0xE0 ? 2 : first >= 0xC0 ? 1 : 0;

		if (first >= 0x80)
		{
			character = first & (0x3F >> following);
			for (; following > 0 && ((unsigned char)*text & 0xC0) == 0x80; following--)
				character = (character << 6) | ((unsigned char)*text++ & 0x3F);
			if (following || first < 0xC0 || character > 0xFFFF)
				character = '?';
		}
		if (character == '\n')
			wide[index++] = '\r';
		wide[index++] = (wchar_t)character;
	}
	wide[index] = 0;
}

/* the start of the UTF-8 character before `at` in text */
static size_t mods_character_before(char const *text, size_t at)
{
	if (at > 0)
		at--;
	while (at > 0 && ((unsigned char)text[at] & 0xC0) == 0x80)
		at--;
	return at;
}

/* whether `count` bytes of text fit across a text box (box): measured in
its font, from its left as it is drawn (leading spaces and all), else
counted */
static boolean mods_text_fits(short box, char const *text, size_t count)
{
	static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };
	char part[MOD_DETAILS_LENGTH];
	wchar_t wide[MOD_DETAILS_LENGTH];
	rectangle2d bounds;
	rectangle2d text_bounds;
	rectangle2d cursor;

	if (!count)
		return TRUE;
	if (mods_screen.fonts[box] == NONE)
		return count <= (size_t)mod_text_box_characters[box];
	if (count >= sizeof(part))
		return FALSE;
	memcpy(part, text, count);
	part[count] = 0;
	mods_wide_text(part, wide, NUMBEROF(wide));
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 0x3FFF;
	bounds.y1 = 480;
	draw_string_set_draw_mode(mods_screen.fonts[box], NONE, 0, 0, &white);
	draw_unicode_string_compute_bounds(&bounds, wide, &text_bounds, &cursor);
	return MAX(text_bounds.x1, cursor.x0) - bounds.x0 <= mods_screen.widths[box] - MOD_TEXT_MARGIN;
}

/* `count` bytes of text, as much of them as fits across a text box (box)
with "..." after it, into `shortened` */
static void mods_text_shorten(short box, char const *text, size_t count, char *shortened, size_t size)
{
	size_t kept = count;

	for (;;)
	{
		snprintf(shortened, size, "%.*s...", (int)kept, text);
		if (!kept || mods_text_fits(box, shortened, strlen(shortened)))
			return;
		kept = mods_character_before(text, kept);
	}
}

/* text, after what `wrapped` holds, in lines that fit across a text box
(box), broken at spaces and at its own line breaks (word by word: a word
too long for a line broken where it fits); `lines` lines in all at most,
the last cut short with "..." when there is more: how many it holds now */
static short mods_wrap(char const *text, char *wrapped, size_t size, short box, short lines, short line)
{
	size_t length = strlen(wrapped);

	while (line < lines && *text)
	{
		char const *end = strchr(text, '\n');
		size_t count = end ? (size_t)(end - text) : strlen(text);
		size_t cut = 0;
		boolean more;

		for (;;)
		{
			size_t next = cut;

			while (next < count && text[next] == ' ')
				next++;
			while (next < count && text[next] != ' ')
				next++;
			if (next == cut || !mods_text_fits(box, text, next))
				break;
			cut = next;
		}
		if (!cut && count)
		{
			for (cut = 0; cut < count && text[cut] == ' '; cut++)
			{
			}
			while (cut < count && text[cut] != ' ')
				cut++;
			while (cut > 1 && !mods_text_fits(box, text, cut))
				cut = mods_character_before(text, cut);
			if (!cut)
				cut = 1;
		}
		more = cut < count || (text[count] == '\n' && text[count + 1]);
		if (line == lines - 1 && more)
		{
			char shortened[MOD_DETAILS_LENGTH];

			mods_text_shorten(box, text, cut, shortened, sizeof(shortened));
			snprintf(wrapped + length, size - length, "%s%s", length ? "\n" : "", shortened);
			return lines;
		}
		snprintf(wrapped + length, size - length, "%s%.*s", length ? "\n" : "", (int)cut, text);
		length = strlen(wrapped);
		line++;
		text += cut;
		while (*text == ' ')
			text++;
		if (*text == '\n')
			text++;
	}
	return line;
}

/* "port mods init" (the list's created), and "port mods close" (its
deleted: what OK has not kept is forgotten) */
static boolean mods_screen_reset(void)
{
	short index;

	port_mods_revert();
	mods_screen.first = 0;
	mods_screen.chosen = 0;
	mods_screen.shown[0] = 0;
	for (index = 0; index < NUMBER_OF_MOD_TEXT_BOXES; index++)
	{
		mods_screen.fonts[index] = NONE;
		mods_screen.widths[index] = 0;
	}
	for (index = 0; index < MOD_ROWS; index++)
		mods_screen.row_mods[index] = NONE;
	return TRUE;
}

/* "port mods toggle" (A on a row; a mod that cannot be played stays as it
is, so that it is still off when it can be) */
static boolean mods_toggle(struct widget_instance *widget)
{
	short place = mod_row_place(mod_row(widget));
	long mod = mod_at_place(place);

	if (place == NONE)
		return FALSE;
	if (port_mods_status(mod, NULL, 0) != _port_mod_unplayable)
		port_mods_turn(mod, !port_mods_on(mod));
	return TRUE;
}

/* "port mods move up" and "port mods move down" (left and right on a row,
and the buttons): the mod of the row, else the one chosen last, moved one
place on the list (up: later in the load order, over one more mod); the
focus follows it on its row */
static boolean mods_move(struct widget_instance *widget, short places)
{
	struct widget_instance *row = mod_row(widget);
	short count = (short)port_mods_count();
	short place = row ? mod_row_place(row) : mods_screen.chosen;
	short moved;

	/* (no mods: nothing to move) */
	if (!count)
		return TRUE;
	if (place == NONE || place >= count)
		return FALSE;
	moved = (short)(count - 1 - port_mods_move(count - 1 - place, -places));
	mods_screen.chosen = moved;
	if (moved < mods_screen.first)
		mods_screen.first = moved;
	else if (moved >= mods_screen.first + MOD_ROWS)
		mods_screen.first = (short)(moved - MOD_ROWS + 1);
	if (row && row->parent)
		focus_row(row->parent, (short)(moved - mods_screen.first));
	return TRUE;
}

/* the details of the mod at `place` on the list, in the frame (the list's
description): its name; what is wrong with it, beside its icon; whether it
is on and its priority (1 the top, the mod that wins); and what its
mod.toml says */
static void mods_details_show(struct widget_instance *details, short place, short count)
{
	long mod = mod_at_place(place);
	struct widget_instance *icon = named(details, "mod_details_status", 0);
	char problem[MOD_DETAILS_LENGTH];
	char about[MOD_DETAILS_LENGTH];
	char text[MOD_DETAILS_LENGTH];
	wchar_t wide[MOD_DETAILS_LENGTH];
	int status;
	short line;
	char shown[sizeof(mods_screen.shown)];

	/* (wrapped again only when what they show, or its fonts, change) */
	snprintf(shown, sizeof(shown), "%ld %d %d %d %d %ld %d %ld %d", mod, port_mods_status(mod, NULL, 0),
		port_mods_on(mod), place, count, mods_screen.fonts[_mod_details_name],
		mods_screen.widths[_mod_details_name], mods_screen.fonts[_mod_details_text],
		mods_screen.widths[_mod_details_text]);
	if (!strcmp(shown, mods_screen.shown))
		return;
	strcpy(mods_screen.shown, shown);
	if (mod < 0)
	{
		/* (no mods: where they go) */
		mods_wide_text(count ? "" : "No mods", wide, NUMBEROF(wide));
		text_set_length(named(details, "mod_details_name", 0), wide, NUMBEROF(wide));
		text[0] = 0;
		if (!count)
		{
			mods_wrap("Each mod is a folder of its own in the mods folder, beside the maps folder.", text,
				sizeof(text), _mod_details_text, MOD_DETAILS_LINES, 0);
		}
		mods_wide_text(text, wide, NUMBEROF(wide));
		text_set_length(named(details, "mod_details_text", 0), wide, NUMBEROF(wide));
		visible_set(icon, FALSE);
		return;
	}
	status = port_mods_status(mod, problem, sizeof(problem));
	text[0] = 0;
	mods_wrap(port_mods_name(mod), text, sizeof(text), _mod_details_name, 2, 0);
	mods_wide_text(text, wide, NUMBEROF(wide));
	text_set_length(named(details, "mod_details_name", 0), wide, NUMBEROF(wide));
	visible_set(icon, status != _port_mod_fine);
	if (icon)
		icon->animation.current_frame_index = status == _port_mod_out_of_order ? 1 : 2;
	text[0] = 0;
	line = 0;
	if (status != _port_mod_fine)
	{
		/* (its first line indented, for the icon) */
		snprintf(text, sizeof(text), "     %s", problem);
		strcpy(problem, text);
		text[0] = 0;
		line = mods_wrap(problem, text, sizeof(text), _mod_details_text, MOD_DETAILS_LINES, line);
		line = mods_wrap(" ", text, sizeof(text), _mod_details_text, MOD_DETAILS_LINES, line);
	}
	snprintf(about, sizeof(about), "%s\nPriority: %d of %d", status == _port_mod_unplayable ? "Cannot be played" :
		port_mods_on(mod) ? "On" : "Off", place + 1, count);
	line = mods_wrap(about, text, sizeof(text), _mod_details_text, MOD_DETAILS_LINES, line);
	line = mods_wrap(" ", text, sizeof(text), _mod_details_text, MOD_DETAILS_LINES, line);
	port_mods_details(mod, about, sizeof(about));
	mods_wrap(about, text, sizeof(text), _mod_details_text, MOD_DETAILS_LINES, line);
	mods_wide_text(text, wide, NUMBEROF(wide));
	text_set_length(named(details, "mod_details_text", 0), wide, NUMBEROF(wide));
}

/* "port mods update": the rows (the list moving on at its ends while there
are more mods past them), each mod's name (cut short where it is too long),
ON or OFF and its icon (out of the order it asks for; another says it cannot
be played with it, or it cannot be played), and the details of the mod
chosen */
static void mods_update(struct widget_instance *list)
{
	struct widget_instance *row;
	struct widget_instance *focused = mod_row(list->focused_child);
	short count = (short)port_mods_count();
	short focused_index = focused ? (short)(atoi(focused->name + 7) - 1) : NONE;
	short box;

	/* (the text boxes' fonts, from their definitions in this map) */
	for (box = 0; box < NUMBER_OF_MOD_TEXT_BOXES; box++)
	{
		mods_screen.fonts[box] = ui_widget_port_text_box_font(named(box == _mod_row_name ? list :
			list->parameters.list.extended_description, mod_text_box_names[box], 0), &mods_screen.widths[box]);
	}

	if (focused_index == MOD_ROWS - 1 && mods_screen.first + MOD_ROWS < count && list_scrolls_at_end())
	{
		mods_screen.first++;
		focus_row(list, --focused_index);
	}
	else if (focused_index == 0 && mods_screen.first > 0 && list_scrolls_at_end())
	{
		mods_screen.first--;
		focus_row(list, ++focused_index);
	}
	if (mods_screen.first > 0 && mods_screen.first + MOD_ROWS > count)
		mods_screen.first = (short)MAX(0, count - MOD_ROWS);
	if (focused_index != NONE && mods_screen.first + focused_index < count)
		mods_screen.chosen = (short)(mods_screen.first + focused_index);
	for (row = list->child; row; row = row->next)
	{
		short place;
		short row_index;
		long mod;
		struct widget_instance *icon;
		int status;

		if (strncmp(row->name, "op_mod_", 7))
			continue;
		/* (its background: the focused one's outlined frame, which the
		engine shows only on a bitmap of two frames, as the browser's) */
		row->animation.current_frame_index = row == focused ? 1 : 0;
		place = mod_row_place(row);
		row->visible = place != NONE;
		if (place == NONE)
		{
			if (row == focused)
				focus_row(list, count ? (short)(count - 1 - mods_screen.first) : NONE);
			continue;
		}
		mod = mod_at_place(place);
		status = port_mods_status(mod, NULL, 0);
		row_index = (short)PIN(atoi(row->name + 7) - 1, 0, MOD_ROWS - 1);
		if (mods_screen.row_mods[row_index] != mod ||
			mods_screen.row_fonts[row_index] != mods_screen.fonts[_mod_row_name])
		{
			char const *name = port_mods_name(mod);
			char shortened[MOD_DETAILS_LENGTH];
			wchar_t wide[ROW_TEXT_LENGTH];

			if (mods_text_fits(_mod_row_name, name, strlen(name)))
				snprintf(shortened, sizeof(shortened), "%s", name);
			else
				mods_text_shorten(_mod_row_name, name, strlen(name), shortened, sizeof(shortened));
			mods_wide_text(shortened, wide, ROW_TEXT_LENGTH);
			text_set(named(row, "mod_name", 0), wide);
			mods_screen.row_mods[row_index] = mod;
			mods_screen.row_fonts[row_index] = mods_screen.fonts[_mod_row_name];
		}
		text_set(named(row, "mod_state", 0), port_mods_on(mod) ? L"ON" : L"OFF");
		icon = named(row, "mod_status", 0);
		visible_set(icon, status != _port_mod_fine);
		if (icon)
			icon->animation.current_frame_index = status == _port_mod_out_of_order ? 1 : 2;
	}
	mods_details_show(list->parameters.list.extended_description, count ? mods_screen.chosen : NONE, count);
}

/* ---------- the update question (updater.c's, the Steam Frame's and gamescope's) */

static struct
{
	/* the main menu drawn since the updater last looked, its tag, and the
	tags' generation that is of (cache_files_tags_generation: a map loaded
	since has numbered its tags again) */
	boolean main_menu_shown;
	long main_menu_tag;
	unsigned long main_menu_generation;
} update_question = { FALSE, NONE, 0 };

/* "port update question": the QUIT item, drawn on the main menu */
static void update_question_note(struct widget_instance *widget)
{
	struct widget_instance *screen = widget;

	while (screen->parent)
		screen = screen->parent;
	update_question.main_menu_shown = TRUE;
	update_question.main_menu_tag = screen->definition_tag_index;
	update_question.main_menu_generation = cache_files_tags_generation();
	/* (the Party screen may open over it: party_menu.inc) */
	party_screen_noted(screen);
}

/* the updater's, between frames (not while the widgets are drawn, which
opening a screen would free): the question opened over the main menu, B
going back to it, if the main menu was drawn last; 1 when it was opened,
or cannot be */
int pc_menu_update_question_open(void)
{
	boolean shown = update_question.main_menu_shown;

	update_question.main_menu_shown = FALSE;
	if (!shown || update_question.main_menu_generation != cache_files_tags_generation())
		return 0;
	if (!ui_widget_load_by_name_or_tag("pc\\main_menu\\update\\update_screen", NONE, NULL, NONE,
		update_question.main_menu_tag, NONE, NONE))
	{
		platform_log("update: the menus have no update question (main_menu/update/update_screen)");
	}
	return 1;
}

/* updater.c's, the macOS app's: 1 while a game is played (its pause menu
too), 0 at the main menu, where the app's updater may show its window
(port/macos/host_app.m) */
int pc_menu_game_playing(void)
{
	/* (asked from the first frame, updater_poll's: before the game's clock
	is made, game_in_progress asserts) */
	return game_time_initialized() && game_in_progress() && !main_menu_is_active();
}

/* "port mods save" (OK): kept, and played at once (port_mods_save keeps
what it cannot write until the game quits) */
static boolean mods_save(void)
{
	port_mods_save();
	mods_apply();
	return TRUE;
}

/* ---------- Multiplayer (port/assets/menus' PLAN: Create Game > Internet
or LAN; Join Game > Server Browser (blank, for now), LAN, Direct Link): the
Xbox's networking, run by the engine's port entry points
(ui_widget_event_handler_functions.c) on our lists */

#define MAXIMUM_ADVERTISED_GAMES 9
#define GAMETYPE_ROWS 10
#define MAXIMUM_GAMETYPES 100
#define BROWSER_ROWS 15
/* the computer's lobby's rows in each of its columns, and its columns
(lobby_menu.inc; tools/port_settings.py's LOBBY_ROWS); the rows with a
speaker icon, either lobby's */
#define LOBBY_BOARD_ROWS 13
#define LOBBY_BOARD_COLUMNS 2
#define LOBBY_ICON_ROWS (LOBBY_BOARD_ROWS * LOBBY_BOARD_COLUMNS)
#define TEXT_FIELD_LENGTH 128
#define PLAYLIST_READ_ONLY_BIT 0x40000000UL
/* (a key stroke's modifier, as input_xbox.c has them: shift, control) */
#define KEY_MODIFIER_CONTROL_BIT 1
#define LOBBY_NAME "pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen"
#define PREVIEW_NAME "pc\\main_menu\\multiplayer_type_select\\lobby\\preview_screen"
/* the server browser's password screen (tools/port_settings.py), and the
longest password */
#define PASSWORD_NAME "pc\\main_menu\\multiplayer_type_select\\join_game\\password\\password_screen"
#define PASSWORD_LENGTH 32
/* the lobby's panel's lines (the gametype, the players, the countdown, the
invite) */
#define LOBBY_TEXT_LENGTH (ROW_TEXT_LENGTH * 4)

enum
{
	_multiplayer_mode_server_browser,
	_multiplayer_mode_lan,
	_multiplayer_mode_direct_link,
	_multiplayer_mode_host_internet,
	_multiplayer_mode_host_lan,
	/* a game of this machine alone (Create Game's OFFLINE): split screen,
	and bots as in every game */
	_multiplayer_mode_host_local,
};

enum
{
	_client_state_searching,
	_client_state_joining,
	_client_state_pregame,
	_client_state_ingame,
	_client_state_postgame,
};

/* a game the client found, as network_client_manager.c has it */
struct advertised_game
{
	byte key_id[8];
	byte key[16];
	/* XNADDR: size, flags, abEnet (the port's machine identifier), ina */
	byte xnaddr[12];
	byte nonce[8];
	unsigned long update_time;
	wchar_t game_name[16];
	long map_version;
	char map_name[0x80];
	short engine_type;
	word machine_count;
	word player_count;
	short maximum_player_count;
	short unknown100;
	short platform;
	boolean open;
	boolean valid;
	boolean has_teams;
	boolean oddball_variant;
};

typedef char verify_advertised_game_platform_offset[offsetof(struct advertised_game, platform) == 0xDE ? 1 : -1];
typedef char verify_advertised_game_open_offset[offsetof(struct advertised_game, open) == 0xE0 ? 1 : -1];

/* the engine's (port) */
short ui_widget_port_multiplayer_maps(char const *const **names, short *last_used);
boolean ui_widget_port_multiplayer_map_choose(short level_index);
boolean ui_widget_port_cooperative_level_choose(char const *map_name, short difficulty);
short ui_widget_port_gametypes(long *indices, short maximum, short *last_used);
boolean ui_widget_port_gametype_choose(long profile_index);
boolean ui_widget_port_host(struct widget_instance *widget, struct event_record *event, boolean *widget_deleted);
boolean ui_widget_port_host_local(struct widget_instance *widget, struct event_record *event, boolean *widget_deleted);
boolean ui_widget_port_browse(struct widget_instance *widget, struct event_record *event, boolean *widget_deleted);
boolean ui_widget_port_open(struct widget_instance *widget, char const *name, boolean *widget_deleted);
boolean network_game_client_advertised_game_in_progress(void *client, struct advertised_game *game);
boolean network_game_client_advertised_game_dedicated(void *client, struct advertised_game *game);
byte network_game_client_advertised_game_kind(void *client, struct advertised_game *game);
long network_game_client_advertised_game_rate(void *client, struct advertised_game *game);
long network_game_client_advertised_game_bots(void *client, struct advertised_game *game);
boolean network_game_client_advertised_game_standard(void *client, struct advertised_game *game);
boolean network_game_client_advertised_game_mixed(void *client, struct advertised_game *game);
boolean network_game_client_advertised_game_compatible(void *client, struct advertised_game const *game,
	boolean tell);
void network_game_client_get_remote_server_address(void *client, struct transport_address *address);
boolean ui_widget_port_join(struct widget_instance *widget, void *advertised_game, char const *lobby_name,
	boolean *widget_deleted);
boolean ui_widget_port_multiplayer_player(short controller_index, long profile_index);
boolean ui_widget_port_unjoin_player(struct widget_instance *widget, struct event_record *event,
	boolean *widget_deleted);
void network_game_server_port_set_settings(wchar_t const *name, long maximum_players);
void network_game_server_port_set_cooperative_friendly_fire(short friendly_fire);
void network_game_server_port_set_cooperative_player_collisions(boolean player_collisions);
void network_game_server_port_set_cooperative_ally_friendly_fire(boolean ally_friendly_fire);
void *global_network_game_client_get(void);
void *global_network_game_server_get(void);
boolean network_game_is_splitscreen_local(void);
struct advertised_game *network_game_client_get_available_games(void *client);
boolean network_game_client_advertised_game_is_valid(struct advertised_game *game);
short network_game_client_get_state(void *client, short *state_data);
struct network_game *network_game_client_get_game(void *client);
struct network_game *network_game_server_get_game(void *server);
short network_game_client_get_local_machine_index(void);
short network_game_client_get_seconds_to_game_start(void *client);
/* (the lobby's team change and start, as the Xbox's pregame's: lobby_menu.inc) */
boolean network_game_client_update_local_player_data(void *client, struct network_player *player);
boolean network_game_client_request_start_time_change(void *client, short request_type);
boolean network_player_is_valid(struct network_player const *player);
boolean playlist_profile_get(long index, struct game_variant *variant);
boolean playlist_profile_get_display_name(long index, wchar_t *name);
boolean playlist_profile_get_options(long index, struct game_variant_options *options);
boolean input_get_key(struct key_stroke *key);
boolean game_engine_running(void);
void game_engine_end_game(void);
/* the platform layer's */
int p2p_join_invite(char const *text);
int p2p_join_listing(struct p2p_listing const *listing);
int p2p_invite_link(char *link, int size);
void p2p_set_hosting_allowed(int allowed);
int p2p_peer_address(unsigned char const *identifier, unsigned long *address);
int platform_clipboard_get(char *text, int size);
int platform_clipboard_has_invite(void);
void platform_clipboard_set(char const *text);
void platform_text_field(int typing, int password);
int config_boolean(char const *name);
void ui_widget_port_post_button(short controller_index, short button_index);

static wchar_t const *const engine_names[] = { L"", L"CTF", L"SLAYER", L"ODDBALL", L"KING OF THE HILL", L"RACE" };
static short const maximum_players[] = { 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 };

static struct
{
	short mode;
	/* Create Game's ONLINE CO-OP: the map list opens on the campaign's
	levels (map_list_initialize) */
	boolean campaign_first;
	/* Create Game's gametype list */
	long gametypes[MAXIMUM_GAMETYPES];
	short gametype_count;
	/* (those of the bank the list's spinner shows: STANDARD, OPENCE, CUSTOM) */
	short bank[MAXIMUM_GAMETYPES], bank_count, bank_shown;
	short gametype_first, gametype_chosen;
	/* the server settings */
	wchar_t game_name[16];
	short maximum_players_index;
	/* (co-op's own, so it leaves multiplayer's as it was: each game hosted
	starts with COOPERATIVE_DEFAULT_PLAYERS players at most) */
	short cooperative_maximum_players_index;
	boolean cooperative_maximum_players_set;
	/* the co-op game's FRIENDLY FIRE, and its PLAYER COLLISIONS OFF, as it
	starts (CO-OP OPTIONS': network.coop_friendly_fire's and
	network.coop_player_collisions': server_start) */
	short cooperative_friendly_fire;
	boolean cooperative_no_player_collisions;
	/* the browser's games */
	struct advertised_game *games[MAXIMUM_ADVERTISED_GAMES];
	short game_count, game_chosen;
	/* the game on the first row (the local network's and Join by Link's
	list, scrolled on at its ends as the server browser's is) */
	short game_first;
	/* the game under way whose lobby is shown before joining it: its key
	(the client's slot it is in may come to hold another game) */
	byte preview_key_id[8];
	byte preview_xnaddr[12];
	/* Server Setup's LISTING: PRIVATE (each new game starts with
	network.host_public's choice: PUBLIC unless set otherwise) */
	boolean game_private;
	/* Server Setup's PASSWORD (a public internet game's; empty: none), kept
	while the game runs, never written down */
	char game_password[PASSWORD_LENGTH + 1];
} multiplayer = { .maximum_players_index = NUMBEROF(maximum_players) - 1 };

/* ---- a text field (Direct Link's link, the game's name): the keyboard
types into it (Ctrl+V pastes), its row's A (enter) is done, B (escape)
cancels */
static struct
{
	struct widget_instance *row;
	char text[TEXT_FIELD_LENGTH];
	char before[TEXT_FIELD_LENGTH];
	short maximum;
	void (*done)(char const *text);
	/* a password's: shown as stars (text_field_begin_masked) */
	boolean masked;
} text_field;

/* when the field was last shown (a field not shown for a while is let go
of: its screen has gone) */
static unsigned long text_field_shown_time;

static boolean text_field_editing(struct widget_instance *row)
{
	return text_field.row && (!row || text_field.row == row);
}

static void text_field_open(struct widget_instance *row, char const *text, short maximum,
	void (*done)(char const *text), boolean masked)
{
	struct key_stroke key;

	text_field.row = row;
	snprintf(text_field.text, sizeof(text_field.text), "%s", text);
	snprintf(text_field.before, sizeof(text_field.before), "%s", text);
	text_field.maximum = (short)MIN(maximum, TEXT_FIELD_LENGTH - 1);
	text_field.done = done;
	text_field.masked = masked;
	text_field_shown_time = system_milliseconds();
	while (input_get_key(&key))
		;
	platform_text_field(TRUE, masked);
}

static void text_field_begin(struct widget_instance *row, char const *text, short maximum,
	void (*done)(char const *text))
{
	text_field_open(row, text, maximum, done, FALSE);
}

/* a password's field: as text_field_begin, its text shown as stars */
static void text_field_begin_masked(struct widget_instance *row, char const *text, short maximum,
	void (*done)(char const *text))
{
	text_field_open(row, text, maximum, done, TRUE);
}

static void text_field_end(boolean keep)
{
	void (*done)(char const *text) = text_field.done;

	platform_text_field(FALSE, FALSE);
	text_field.row = NULL;
	text_field.done = NULL;
	if (!keep)
		snprintf(text_field.text, sizeof(text_field.text), "%s", text_field.before);
	else if (done)
		done(text_field.text);
}

static void text_field_insert(char const *text)
{
	size_t length = strlen(text_field.text);

	for (; *text && length < (size_t)text_field.maximum; text++)
	{
		if (*text >= ' ' && *text <= '~')
			text_field.text[length++] = *text;
	}
	text_field.text[length] = 0;
}

/* the keys typed since the last frame, and the field shown (a caret
blinking while it is edited) */

static void text_field_show(struct widget_instance *value, char const *text, boolean editing)
{
	wchar_t shown[TEXT_FIELD_LENGTH + 2];
	short index;

	if (editing)
	{
		text_field_shown_time = system_milliseconds();
		struct key_stroke key;

		while (input_get_key(&key))
		{
			size_t length = strlen(text_field.text);

			if (key.key_code == _key_backspace)
			{
				if (length)
					text_field.text[length - 1] = 0;
			}
			else if (TEST_FLAG(key.modifier_flags, KEY_MODIFIER_CONTROL_BIT) && key.key_code == _key_v)
			{
				char clipboard[TEXT_FIELD_LENGTH];

				if (platform_clipboard_get(clipboard, sizeof(clipboard)))
					text_field_insert(clipboard);
			}
			else if ((unsigned char)key.ascii_code >= ' ' && (unsigned char)key.ascii_code <= '~')
			{
				char character[2] = { key.ascii_code, 0 };

				text_field_insert(character);
			}
		}
		text = text_field.text;
	}
	for (index = 0; text[index] && index < TEXT_FIELD_LENGTH; index++)
		shown[index] = editing && text_field.masked ? L'*' : (wchar_t)(unsigned char)text[index];
	if (editing && (system_milliseconds() / 500) % 2)
		shown[index++] = L'_';
	shown[index] = 0;
	text_set(value, shown);
	/* (what is being typed shown as it is: streamer mode hides nothing of it,
	text_filter.c) */
	if (editing && value)
		text_filter_exempt(value->parameters.text_box.text);
	else if (!text_field_editing(NULL))
		text_filter_exempt(NULL);
}

/* ---- Settings' sliders (tools/port_settings.py's Slider rows): a track
with its thumb at the value, and the value's number. Left and right step it
("port slider left", "right"); a click on the track sets it where it is, and
the mouse held drags it along; A, Start or a click elsewhere on the row
types a value into its number ("port slider edit": Enter, which is Start
while typing, keeps it, Escape forgets it). The value is a change kept, as a
spinner's is (settings_pending), which Settings' OK writes. A slider with
stops has them along its track, evenly spaced, in place of its range: left,
right and the track go from stop to stop, and a value typed between two
stops sits between them; a stop shown as a word (AUTO, NONE, DEFAULT) is a
value of its own, which may be typed as the word or as its number. A word's
stop may write a word of the setting's in place of its number
(display.vehicle_fov's "default" and "match"). */

/* a slider's stop: its value, the word it is shown as (NULL: its number),
and the setting's word it writes (NULL: its number) */
struct settings_slider_stop
{
	double value;
	char const *word;
	char const *text;
};

/* (tools/port_settings.py's FRAME_RATE_LIMITS and FIELDS_OF_VIEW) */
static struct settings_slider_stop const settings_frame_rate_stops[] =
{
	{ 0.0, "AUTO" }, { 30.0, NULL }, { 60.0, NULL }, { 75.0, NULL }, { 90.0, NULL }, { 100.0, NULL },
	{ 120.0, NULL }, { 144.0, NULL }, { 165.0, NULL }, { 200.0, NULL }, { 240.0, NULL }, { 280.0, NULL },
	{ 360.0, NULL }, { 480.0, NULL }, { 540.0, NULL }, { -1.0, "NONE" },
};
static struct settings_slider_stop const settings_fov_stops[] =
{
	{ 0.0, "DEFAULT" }, { 80.0, NULL }, { 85.0, NULL }, { 90.0, NULL }, { 95.0, NULL }, { 100.0, NULL },
	{ 105.0, NULL }, { 110.0, NULL }, { 115.0, NULL }, { 120.0, NULL }, { 125.0, NULL }, { 130.0, NULL },
	{ 135.0, NULL }, { 140.0, NULL }, { 145.0, NULL }, { 150.0, NULL },
};
/* (tools/port_settings.py's VEHICLE_FIELDS_OF_VIEW: the game's own view, and
display.fov's) */
static struct settings_slider_stop const settings_vehicle_fov_stops[] =
{
	{ -2.0, "DEFAULT", "default" }, { -1.0, "MATCH", "match" }, { 80.0, NULL }, { 85.0, NULL }, { 90.0, NULL },
	{ 95.0, NULL }, { 100.0, NULL }, { 105.0, NULL }, { 110.0, NULL }, { 115.0, NULL }, { 120.0, NULL },
	{ 125.0, NULL }, { 130.0, NULL }, { 135.0, NULL }, { 140.0, NULL }, { 145.0, NULL }, { 150.0, NULL },
};

#define SLIDER_STOPS(stops) stops, (short)NUMBEROF(stops)

/* each slider by its row: its setting, its track's range and step, the
values it may be typed, whether they are whole numbers, and its stops (as
tools/port_settings.py's Slider has them) */
static struct
{
	char const *row;
	char const *setting;
	double least, most, step;
	double typed_least, typed_most;
	boolean whole;
	struct settings_slider_stop const *stops;
	short stop_count;
} const settings_sliders[] =
{
	{ "op_mouse_sensitivity", "input.mouse_sensitivity", 0.1, 10.0, 0.05, 0.01, 100.0, FALSE, NULL, 0 },
	{ "op_max_fps", "display.max_fps", 30.0, 540.0, 1.0, 10.0, 1000.0, TRUE, SLIDER_STOPS(settings_frame_rate_stops) },
	{ "op_fov", "display.fov", 80.0, 150.0, 5.0, 20.0, 150.0, FALSE, SLIDER_STOPS(settings_fov_stops) },
	{ "op_viewmodel_fov", "display.viewmodel_fov", 80.0, 150.0, 5.0, 20.0, 150.0, FALSE,
		SLIDER_STOPS(settings_fov_stops) },
	{ "op_viewmodel_offset_x", "display.viewmodel_offset_x", -10.0, 10.0, 0.5, -20.0, 20.0, FALSE, NULL, 0 },
	{ "op_viewmodel_offset_y", "display.viewmodel_offset_y", -10.0, 10.0, 0.5, -20.0, 20.0, FALSE, NULL, 0 },
	{ "op_viewmodel_offset_z", "display.viewmodel_offset_z", -10.0, 10.0, 0.5, -20.0, 20.0, FALSE, NULL, 0 },
	{ "op_vehicle_fov", "display.vehicle_fov", 80.0, 150.0, 5.0, 20.0, 150.0, FALSE,
		SLIDER_STOPS(settings_vehicle_fov_stops) },
	/* (tools/port_settings.py's GAMMA) */
	{ "op_gamma", "display.gamma", 0.5, 2.0, 0.05, 0.5, 2.0, FALSE, NULL, 0 },
};

/* the track and the thumb, as tools/port_settings.py lays them out
(SLIDER_TRACK_WIDTH, SLIDER_THUMB_WIDTH) */
#define SLIDER_TRACK_WIDTH 66
#define SLIDER_THUMB_WIDTH 8

/* the slider the mouse drags (its row), and the one typed into */
static struct
{
	struct widget_instance *dragged;
	short typed;
} settings_slider = { NULL, NONE };

static short settings_slider_of(struct widget_instance const *row)
{
	short index;

	for (index = 0; row && row->name && index < (short)NUMBEROF(settings_sliders); index++)
	{
		if (!strcmp(row->name, settings_sliders[index].row))
			return index;
	}
	return NONE;
}

/* the stop shown as a word that has this value, or NONE */
static short settings_slider_word(short slider, double value)
{
	short stop;

	for (stop = 0; stop < settings_sliders[slider].stop_count; stop++)
	{
		if (settings_sliders[slider].stops[stop].word && settings_sliders[slider].stops[stop].value == value)
			return stop;
	}
	return NONE;
}

/* a setting's text as the slider's value: a stop's word of the setting's,
else its number */
static boolean settings_slider_text_value(short slider, char const *text, double *value)
{
	short stop;

	for (stop = 0; stop < settings_sliders[slider].stop_count; stop++)
	{
		if (settings_sliders[slider].stops[stop].text && !_stricmp(text, settings_sliders[slider].stops[stop].text))
		{
			*value = settings_sliders[slider].stops[stop].value;
			return TRUE;
		}
	}
	return text_is_number(text, value);
}

/* whether the slider takes the value: one it may be typed, or a word's */
static boolean settings_slider_valid(short slider, double value)
{
	return (value >= settings_sliders[slider].typed_least && value <= settings_sliders[slider].typed_most) ||
		settings_slider_word(slider, value) != NONE;
}

/* the slider's value as Settings shows it: the change kept, else the
setting's (else its default) */
static double settings_slider_value(short slider)
{
	char text[64];
	double value;

	if (!settings_value(settings_sliders[slider].setting, text, sizeof(text)) ||
		!settings_slider_text_value(slider, text, &value) || !settings_slider_valid(slider, value))
	{
		if (!setting_text(settings_sliders[slider].setting, text, sizeof(text), TRUE) ||
			!settings_slider_text_value(slider, text, &value))
		{
			value = settings_sliders[slider].least;
		}
	}
	return value;
}

/* a value as the slider writes it: a whole number, or at most three
decimals */
static void settings_slider_number(short slider, double value, char *text, unsigned int size)
{
	size_t length;

	if (settings_sliders[slider].whole)
	{
		snprintf(text, size, "%ld", (long)(value < 0.0 ? value - 0.5 : value + 0.5));
		return;
	}
	snprintf(text, size, "%.3f", value);
	length = strlen(text);
	while (length > 3 && text[length - 1] == '0' && text[length - 2] != '.')
		text[--length] = 0;
}

/* a value as the slider shows it: its stop's word, else its number */
static void settings_slider_text(short slider, double value, char *text, unsigned int size)
{
	short word = settings_slider_word(slider, value);

	if (word != NONE)
		snprintf(text, size, "%s", settings_sliders[slider].stops[word].word);
	else
		settings_slider_number(slider, value, text, size);
}

/* the value kept as a change, or none if it is the setting's */
static void settings_slider_set(short slider, double value)
{
	char text[64], current[64];
	short word = settings_slider_word(slider, value);

	if (word == NONE)
		value = PIN(value, settings_sliders[slider].typed_least, settings_sliders[slider].typed_most);
	if (word != NONE && settings_sliders[slider].stops[word].text)
		snprintf(text, sizeof(text), "%s", settings_sliders[slider].stops[word].text);
	else
		settings_slider_number(slider, value, text, sizeof(text));
	if (setting_text(settings_sliders[slider].setting, current, sizeof(current), FALSE) &&
		settings_values_same(current, text))
	{
		settings_pending_remove(settings_sliders[slider].setting);
	}
	else
	{
		settings_pending_set(settings_sliders[slider].setting, text);
	}
}

/* where the value is along a slider's stops, from 0 (the first) to the
last's index: a stop's own place, or between the two numbers it is between
(past the numbers' ends, at the end) */
static double settings_slider_stop_place(short slider, double value)
{
	struct settings_slider_stop const *stops = settings_sliders[slider].stops;
	short count = settings_sliders[slider].stop_count;
	short stop, first = NONE, last = NONE;

	for (stop = 0; stop < count; stop++)
	{
		if (stops[stop].value == value)
			return stop;
	}
	for (stop = 0; stop < count; stop++)
	{
		if (stops[stop].word)
			continue;
		if (first == NONE)
			first = stop;
		last = stop;
		if (stop + 1 < count && !stops[stop + 1].word && value > stops[stop].value && value < stops[stop + 1].value)
			return stop + (value - stops[stop].value) / (stops[stop + 1].value - stops[stop].value);
	}
	if (first == NONE)
		return 0.0;
	return value < stops[first].value ? first : last;
}

/* the stop nearest the slider's value (its picture's: settings_panel_show),
or NONE for a slider without stops */
static short settings_slider_stop_nearest(short slider)
{
	if (!settings_sliders[slider].stop_count)
		return NONE;
	return (short)(settings_slider_stop_place(slider, settings_slider_value(slider)) + 0.5);
}

/* the thumb's place along the track, from 0 to 1 */
static double settings_slider_fraction(short slider, double value)
{
	if (settings_sliders[slider].stop_count > 1)
		return settings_slider_stop_place(slider, value) / (settings_sliders[slider].stop_count - 1);
	return (value - settings_sliders[slider].least) / (settings_sliders[slider].most - settings_sliders[slider].least);
}

/* a value to the nearest step, either side of 0 */
static double settings_slider_round(double value, double step)
{
	double steps = value / step;

	return (double)(long)(steps < 0.0 ? steps - 0.5 : steps + 0.5) * step;
}

/* the value at a place on the track (its range, in steps) */
static void settings_slider_place(short slider, struct widget_instance *track, short x)
{
	double fraction = (double)(x - widget_screen_x(track) - SLIDER_THUMB_WIDTH / 2) /
		(SLIDER_TRACK_WIDTH - SLIDER_THUMB_WIDTH);
	double least = settings_sliders[slider].least, most = settings_sliders[slider].most;
	double step = settings_sliders[slider].step;
	double value = least + PIN(fraction, 0.0, 1.0) * (most - least);
	short count = settings_sliders[slider].stop_count;

	/* (with stops, the one nearest) */
	if (count > 1)
	{
		settings_slider_set(slider,
			settings_sliders[slider].stops[(short)(PIN(fraction, 0.0, 1.0) * (count - 1) + 0.5)].value);
		return;
	}
	settings_slider_set(slider, PIN(settings_slider_round(value, step), least, most));
}

/* (the number typed: kept if it is one, past 0 (or in a range that takes 0
and below), or a word's; or a word) */
static void settings_slider_typed(char const *text)
{
	short slider = settings_slider.typed;
	double value;
	short stop;

	settings_slider.typed = NONE;
	if (slider == NONE)
		return;
	if (text_is_number(text, &value) && (value > 0.0 || settings_sliders[slider].typed_least <= 0.0 ||
		settings_slider_word(slider, value) != NONE))
	{
		settings_slider_set(slider, value);
		return;
	}
	for (stop = 0; stop < settings_sliders[slider].stop_count; stop++)
	{
		if (settings_sliders[slider].stops[stop].word && !_stricmp(text, settings_sliders[slider].stops[stop].word))
		{
			settings_slider_set(slider, settings_sliders[slider].stops[stop].value);
			return;
		}
	}
}

static struct widget_instance *settings_slider_track(struct widget_instance *row)
{
	struct widget_instance *child;

	for (child = row ? row->child : NULL; child; child = child->next)
	{
		if (child->name && strstr(child->name, "_track"))
			return child;
	}
	return NULL;
}

/* "port slider <name>" (its row's) */
static boolean settings_slider_function(char const *name, struct widget_instance *row)
{
	short slider = settings_slider_of(row);
	struct widget_instance *track = settings_slider_track(row);

	if (slider == NONE || !track)
		return FALSE;
	if (!strcmp(name, "left") || !strcmp(name, "right"))
	{
		double step = settings_sliders[slider].step;
		double value = settings_slider_value(slider);
		short count = settings_sliders[slider].stop_count;

		/* (the arrow keys move nothing while the number is typed) */
		if (text_field_editing(row))
			return TRUE;
		/* (with stops, to the next one along: from between two, the one on
		that side) */
		if (count > 1)
		{
			double place = settings_slider_stop_place(slider, value);
			short at = (short)place;
			short stop = !strcmp(name, "left") ? (short)(place > at ? at : at - 1) : (short)(at + 1);

			settings_slider_set(slider, settings_sliders[slider].stops[PIN(stop, 0, count - 1)].value);
			ui_play_audio_feedback_sound(SOUND_CURSOR);
			return TRUE;
		}
		value = settings_slider_round(value, step) + (!strcmp(name, "left") ? -step : step);
		settings_slider_set(slider, PIN(value, settings_sliders[slider].least, settings_sliders[slider].most));
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	}
	if (!strcmp(name, "edit"))
	{
		char text[64];
		short x, y;

		if (text_field_editing(row))
		{
			text_field_end(TRUE);
			return TRUE;
		}
		/* (a click on the track sets the value there, and drags it) */
		if (ui_widget_port_take_click(&x, &y) && x >= widget_screen_x(track) - 4 &&
			x < widget_screen_x(track) + SLIDER_TRACK_WIDTH + 4)
		{
			settings_slider_place(slider, track, x);
			settings_slider.dragged = row;
			return TRUE;
		}
		if (text_field_editing(NULL))
			text_field_end(TRUE);
#ifdef HALO_ANDROID
		/* (Android's app raises no keyboard for a field: the track alone) */
		(void)text;
		return TRUE;
#else
		settings_slider.typed = slider;
		settings_slider_text(slider, settings_slider_value(slider), text, sizeof(text));
		text_field_begin(row, text, 7, settings_slider_typed);
		return TRUE;
#endif
	}
	return FALSE;
}

/* ui_widget.c's: a finger that went down on an item at x and then moved
sideways, taken when the item is a slider's row (or in one) and x is on its
track: the value goes there and the finger, held, drags it
(settings_sliders_show), as the mouse's button does; its drag then steps no
list. The row, which ui_widget.c chooses, or NULL */
struct widget_instance *menu_functions_finger_down(struct widget_instance *item, short x)
{
	struct widget_instance *row = item;
	struct widget_instance *track;
	short slider = NONE;
	short depth;

	for (depth = 0; row && depth < 3 && (slider = settings_slider_of(row)) == NONE; depth++)
		row = row->parent;
	track = slider != NONE ? settings_slider_track(row) : NULL;
	if (!track || x < widget_screen_x(track) - 4 || x >= widget_screen_x(track) + SLIDER_TRACK_WIDTH + 4)
		return NULL;
	/* (a number being typed is kept, and the finger places the value
	too, where the mouse's click would only end the typing) */
	if (text_field_editing(NULL))
		text_field_end(TRUE);
	settings_slider_place(slider, track, x);
	settings_slider.dragged = row;
	return row;
}

/* (each frame of a page) the sliders' thumbs at their values and their
numbers shown (typed into, the text typed); the one dragged follows the
mouse while its button is held; one typed into whose row lost the focus
keeps what was typed */
static void settings_sliders_show(struct widget_instance *list)
{
	struct widget_instance *row;

	for (row = list->child; row; row = row->next)
	{
		short slider = settings_slider_of(row);
		struct widget_instance *track = slider != NONE ? settings_slider_track(row) : NULL;
		struct widget_instance *thumb = track ? track->child : NULL;
		struct widget_instance *value_text;
		char text[64];
		double value, fraction;
		short x;

		if (!track)
			continue;
		if (settings_slider.dragged == row)
		{
			if (ui_widget_port_pointer(&x))
				settings_slider_place(slider, track, x);
			else
				settings_slider.dragged = NULL;
		}
		if (text_field_editing(row) && list->focused_child != row)
			text_field_end(TRUE);
		value = settings_slider_value(slider);
		fraction = settings_slider_fraction(slider, value);
		if (thumb)
			thumb->horizontal_offset = (short)(PIN(fraction, 0.0, 1.0) * (SLIDER_TRACK_WIDTH - SLIDER_THUMB_WIDTH) + 0.5);
		for (value_text = track->next; value_text && value_text->type != 1; value_text = value_text->next)
		{
		}
		settings_slider_text(slider, value, text, sizeof(text));
		text_field_show(value_text, text, text_field_editing(row));
		/* (typed into: its field lit, and the caret held on, not blinking,
		so that it is plain that the number takes what is typed) */
		if (value_text)
		{
			wchar_t *shown = value_text->parameters.text_box.text;

			value_text->animation.current_frame_index = (short)(text_field_editing(row) != FALSE);
			if (shown && text_field_editing(row))
			{
				short length = (short)ustrlen(shown);

				if ((!length || shown[length - 1] != L'_') && length < ROW_TEXT_LENGTH - 1)
				{
					shown[length] = L'_';
					shown[length + 1] = 0;
				}
			}
		}
	}
}

/* "port settings defaults": the sliders' settings at their defaults (what
is being typed forgotten) */
static void settings_sliders_default(struct widget_instance *screen)
{
	char text[64];
	double value;
	short slider;

	if (text_field_editing(NULL))
		text_field_end(FALSE);
	for (slider = 0; slider < (short)NUMBEROF(settings_sliders); slider++)
	{
		if (named(screen, settings_sliders[slider].row, 0) &&
			setting_text(settings_sliders[slider].setting, text, sizeof(text), TRUE) &&
			settings_slider_text_value(slider, text, &value))
		{
			settings_slider_set(slider, value);
		}
	}
}

/* ---- the server browsers' Filters page (tools/port_settings.py's
BROWSER_FILTERS, its rows the network.browser_* settings): opened by the
browsers' FILTERS (the left trigger; "port browser filters", which starts
it with no change kept: its OK writes its own, as a page of Settings' does)
or by Settings' Online. Its SEARCH row's words are typed into its value
("port browser search": A, Start or a click begins, Enter keeps, Escape
forgets), a change kept as a spinner's is. The browser reads them all again
as it opens (browser_filters_read) */

#define BROWSER_SEARCH_SETTING "network.browser_search"
#define BROWSER_SEARCH_TEXT 32

/* "port browser filters": not Join by Link's, whose games are not filtered */
static boolean browser_filters_open(void)
{
	if (multiplayer.mode == _multiplayer_mode_direct_link)
		return FALSE;
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	settings_pending_clear();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* (the words typed: a change kept, or none if they are the setting's) */
static void browser_search_typed(char const *text)
{
	char current[BROWSER_SEARCH_TEXT] = "";

	setting_text(BROWSER_SEARCH_SETTING, current, sizeof(current), FALSE);
	if (!strcmp(current, text))
		settings_pending_remove(BROWSER_SEARCH_SETTING);
	else
		settings_pending_set(BROWSER_SEARCH_SETTING, text);
}

/* "port browser search" (its row's A: begins, or ends) */
static boolean browser_search_edit(struct widget_instance *row)
{
	char text[BROWSER_SEARCH_TEXT] = "";

	if (text_field_editing(row))
	{
		text_field_end(TRUE);
		return TRUE;
	}
	if (text_field_editing(NULL))
		text_field_end(TRUE);
	settings_value(BROWSER_SEARCH_SETTING, text, sizeof(text));
	text_field_begin(row, text, BROWSER_SEARCH_TEXT - 1, browser_search_typed);
	return TRUE;
}

/* SEARCH's value as its row and the panel show it: the words, or OFF */
static void browser_search_text(char *text, unsigned int size)
{
	text[0] = 0;
	settings_value(BROWSER_SEARCH_SETTING, text, size);
	if (!text[0])
		snprintf(text, size, "OFF");
}

/* (each frame of a page) SEARCH's value, the words typed while they are;
typing whose row lost the focus keeps what was typed */
static void browser_search_show(struct widget_instance *list)
{
	struct widget_instance *row = named(list, "op_browser_search", 0);
	struct widget_instance *value = row ? named(row, "browser_search_value", 0) : NULL;
	char text[BROWSER_SEARCH_TEXT];

	if (!value)
		return;
	if (text_field_editing(row) && list->focused_child != row)
		text_field_end(TRUE);
	browser_search_text(text, sizeof(text));
	text_field_show(value, text, text_field_editing(row));
}

/* the SEARCH row's panel: its words (the others': FALSE) */
static boolean browser_search_panel(struct widget_instance *row, wchar_t *value)
{
	char text[BROWSER_SEARCH_TEXT];
	short index;

	if (!row->name || strcmp(row->name, "op_browser_search"))
		return FALSE;
	browser_search_text(text, sizeof(text));
	for (index = 0; text[index] && index < ROW_TEXT_LENGTH - 1; index++)
		value[index] = (wchar_t)(unsigned char)text[index];
	value[index] = 0;
	return TRUE;
}

/* "port settings defaults": no words searched for (what is typed forgotten) */
static void browser_search_default(struct widget_instance *screen)
{
	if (!named(screen, "op_browser_search", 0))
		return;
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	browser_search_typed("");
}

/* ---- the menu */

/* "mp type set mode": the item's (by its name) */
static void multiplayer_mode_set(struct widget_instance *widget)
{
	char const *name = widget->name;

	/* (an instance's name is cut to 31 characters:
	"multiplayer_type_create_interne") */
	multiplayer.campaign_first = strstr(name, "online_coop") != NULL;
	multiplayer.mode =
		strstr(name, "create_inter") || multiplayer.campaign_first ? _multiplayer_mode_host_internet :
		strstr(name, "create_lan") ? _multiplayer_mode_host_lan :
		strstr(name, "create_offline") || strstr(name, "create_bots") ? _multiplayer_mode_host_local :
		strstr(name, "join_lan") ? _multiplayer_mode_lan :
		strstr(name, "join_direct") ? _multiplayer_mode_direct_link : _multiplayer_mode_server_browser;
}

/* player 1's profile for the controller's player in a network game */
static boolean multiplayer_player(short controller)
{
	struct player_profile profile;

	if (!campaign_profile(controller, &profile))
		return campaign_fail();
	return ui_widget_port_multiplayer_player(controller, player_ui_get_active_player_profile_index(0));
}

/* Create Game's ("join controller to mp game", first): player 1 in, and
the server made: an internet one (an invite, Discord), a LAN one, or
(OFFLINE) one of this machine alone */
static boolean multiplayer_host(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
{
	multiplayer_mode_set(widget);
	if (!multiplayer_player(controller))
		return FALSE;
	p2p_set_hosting_allowed(multiplayer.mode == _multiplayer_mode_host_internet);
	/* (in the server browser, if PUBLIC: Server Setup's LISTING, which a
	new game starts with as network.host_public says) */
	multiplayer.game_private = !config_boolean("network.host_public");
	p2p_set_hosting_public(multiplayer.mode == _multiplayer_mode_host_internet && !multiplayer.game_private);
	multiplayer.cooperative_maximum_players_set = FALSE;
	if (multiplayer.mode == _multiplayer_mode_host_local)
		return ui_widget_port_host_local(widget, event, widget_deleted);
	return ui_widget_port_host(widget, event, widget_deleted);
}

/* ---- the map list (the Map screen's): its chooser's SINGLEPLAYER levels,
MULTIPLAYER maps, CUSTOM SINGLEPLAYER or CUSTOM MULTIPLAYER maps (the map
lists', above). A level or Custom Edition campaign map chosen lists the
difficulties (B goes back to the map) and is hosted as a network co-op game,
which goes to Server Setup; a multiplayer map goes on to the gametypes.
Split screen hosts only multiplayer maps, its chooser kept to the
multiplayer kinds. */

enum
{
	MAP_STEP_MAPS,
	MAP_STEP_DIFFICULTIES,
};

static struct
{
	short kind, step;
	/* hosting over the network, which co-op needs */
	boolean hosting;
	/* the multiplayer maps (ui_widget_port_multiplayer_maps): the Xbox's,
	then the Custom Edition ones */
	short map_count;
	short first, chosen;
	/* the level or Custom Edition campaign map (the kind's entry) whose
	difficulties are listed */
	short level;
} map_list;

#define SERVER_SETUP_NAME "pc\\main_menu\\multiplayer_type_select\\server_settings\\server_settings_screen"

static short map_step_count(void)
{
	if (map_list.step == MAP_STEP_DIFFICULTIES)
		return NUMBER_OF_GAME_DIFFICULTY_LEVELS;
	return map_kind_count(map_list.kind, map_list.map_count);
}

/* opens a step at the entry given */
static void map_step_open(struct widget_instance *list, short step, short entry)
{
	short count;

	map_list.step = step;
	count = map_step_count();
	map_list.chosen = (short)PIN(entry, 0, MAX(count - 1, 0));
	map_list.first = map_kind_first(map_list.chosen, count);
	map_kind_focus(list, map_list.first, map_list.chosen);
}

static void map_level_text(short level, wchar_t *text)
{
	string_get("pc\\main_menu\\map_list", level, text);
}

static void map_difficulty_text(short difficulty, wchar_t *text)
{
	string_get("pc\\main_menu\\player_profiles_select\\difficulty_names", difficulty, text);
}

/* "mp level list initialize": on the multiplayer map used last */
static boolean map_list_initialize(struct widget_instance *list)
{
	char const *const *names;
	short last_used = 0;

	map_list.hosting = global_network_game_server_get() != NULL && !network_game_is_splitscreen_local();
	/* (the maps folders looked for again as the list opens) */
	custom_edition_maps_look_again();
	map_list.map_count = ui_widget_port_multiplayer_maps(&names, &last_used);
	/* (a Custom Edition map's kind, when it was one) */
	map_list.kind = MAP_KIND_MULTIPLAYER;
	if (last_used >= xbox_multiplayer_map_count(map_list.map_count))
	{
		map_list.kind = MAP_KIND_CUSTOM_MULTIPLAYER;
		last_used -= xbox_multiplayer_map_count(map_list.map_count);
	}
	/* (ONLINE CO-OP's: the campaign's levels, from the first) */
	if (multiplayer.campaign_first && map_list.hosting)
	{
		map_list.kind = MAP_KIND_SINGLEPLAYER;
		last_used = 0;
	}
	multiplayer.campaign_first = FALSE;
	map_kind_set(list, map_list.kind);
	map_step_open(list, MAP_STEP_MAPS, last_used);
	return TRUE;
}

/* "mp map list update": the chooser's kind, the step's rows, and beside
them the level or map chosen: its picture, name and words */
static void map_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	short kind = map_kind_shown(list, map_list.hosting, map_list.kind), count, entry;
	short shown;

	if (kind != map_list.kind)
	{
		map_list.kind = kind;
		map_list.step = MAP_STEP_MAPS;
		map_list.first = map_list.chosen = 0;
	}
	count = map_step_count();
	entry = map_kind_rows_update(list, &map_list.first, count, map_list.step == MAP_STEP_DIFFICULTIES ?
		map_difficulty_text : kind == MAP_KIND_SINGLEPLAYER ? map_level_text :
		kind == MAP_KIND_MULTIPLAYER ? multiplayer_map_text :
		kind == MAP_KIND_CUSTOM_SINGLEPLAYER ? custom_campaign_map_text : custom_multiplayer_map_text);
	if (entry != NONE)
		map_list.chosen = entry;
	visible_set(named(description, "mp_map_right_item", 0), map_list.chosen < count);
	/* (the map whose difficulties are listed, else the one chosen) */
	shown = map_list.step == MAP_STEP_DIFFICULTIES ? map_list.level : map_list.chosen;
	if (kind == MAP_KIND_SINGLEPLAYER)
	{
		map_description_show(description, shown, NONE);
		level_description(description, "replay_level", shown, FALSE, NULL, NONE);
	}
	else
	{
		map_description_show(description, NONE,
			map_list.step == MAP_STEP_DIFFICULTIES || map_list.chosen < count ? map_kind_display_index(kind, shown) :
			NONE);
	}
	profile_name_show(description);
}

/* "mp level select" (the list's OK): the map chosen, or a level's
difficulties, or the level at the difficulty chosen. FALSE stays on the Map
screen (the gametypes open on TRUE). */
static boolean map_list_choose(struct widget_instance *list, boolean *widget_deleted)
{
	short chosen;

	map_list_update(list);
	chosen = map_list.chosen;

	if (chosen >= map_step_count())
		return campaign_fail();
	if (map_list.step == MAP_STEP_DIFFICULTIES)
	{
		char const *map_name = map_list.kind == MAP_KIND_SINGLEPLAYER ? main_get_solo_level_name(map_list.level) :
			custom_map_level_name(map_list.kind, map_list.level);

		/* the co-op game set up, then Server Setup in the gametypes' place */
		if (!ui_widget_port_cooperative_level_choose(map_name, chosen))
			return campaign_fail();
		return ui_widget_port_open(list, SERVER_SETUP_NAME, widget_deleted);
	}
	/* (a multiplayer map: the Xbox's, then the Custom Edition ones, in the
	multiplayer maps) */
	if (map_list.kind == MAP_KIND_MULTIPLAYER)
		return ui_widget_port_multiplayer_map_choose(chosen);
	if (map_list.kind == MAP_KIND_CUSTOM_MULTIPLAYER)
		return ui_widget_port_multiplayer_map_choose((short)(xbox_multiplayer_map_count(map_list.map_count) + chosen));
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	map_list.level = chosen;
	map_step_open(list, MAP_STEP_DIFFICULTIES, (short)PIN(main_get_difficulty(), 0, NUMBER_OF_GAME_DIFFICULTY_LEVELS - 1));
	return FALSE;
}

/* "port map list back" (B on the list): from the difficulties back to the
level, else out of the Map screen */
static boolean map_list_back(struct widget_instance *list, boolean *widget_deleted)
{
	ui_play_audio_feedback_sound(SOUND_BACK);
	if (map_list.step == MAP_STEP_DIFFICULTIES)
	{
		map_step_open(list, MAP_STEP_MAPS, map_list.level);
		return TRUE;
	}
	ui_widget_port_go_back(list);
	*widget_deleted = TRUE;
	return TRUE;
}

/* (the gametype editor's: Server Setup's copy of the game's gametype) */
static void gametype_setup_begin(void);
static void gametype_setup_end(void);
static boolean gametype_setup_apply(void);
static void gametype_setup_type(wchar_t *text);

/* ---- the gametype list: its spinner's bank (STANDARD: the Xbox's built-in
ones, OPENCE: OpenCE's built-in ones, game_modes.c, CUSTOM: those saved) in
its first row, then the bank's gametypes */

enum
{
	GAMETYPE_BANK_STANDARD = 0,
	GAMETYPE_BANK_OPENCE,
	GAMETYPE_BANK_CUSTOM,
	NUMBER_OF_GAMETYPE_BANKS
};

/* a gametype's bank */
static short gametype_bank(long profile_index)
{
	struct game_variant variant;
	struct game_variant_options options;

	if (!((unsigned long)profile_index & PLAYLIST_READ_ONLY_BIT))
		return GAMETYPE_BANK_CUSTOM;
	if (playlist_profile_get(profile_index, &variant) && playlist_profile_get_options(profile_index, &options) &&
		game_modes_variant_is_opence(&variant, &options))
	{
		return GAMETYPE_BANK_OPENCE;
	}
	return GAMETYPE_BANK_STANDARD;
}

static void gametype_bank_read(short bank)
{
	short index;

	multiplayer.bank_shown = bank;
	multiplayer.bank_count = 0;
	for (index = 0; index < multiplayer.gametype_count; index++)
	{
		if (gametype_bank(multiplayer.gametypes[index]) == bank)
			multiplayer.bank[multiplayer.bank_count++] = index;
	}
	multiplayer.gametype_first = 0;
	multiplayer.gametype_chosen = 0;
}

/* a gametype's kind and rules, for the description beside a list: its game
engine's name (or its OpenCE gametype's), teams, and what wins */
static void gametype_rules_text(long profile_index, wchar_t *text, short size)
{
	struct game_variant variant;
	struct game_variant_options options;
	wchar_t const *kind;
	long engine;

	text[0] = 0;
	if (!playlist_profile_get(profile_index, &variant))
		return;
	if (!playlist_profile_get_options(profile_index, &options))
		game_variant_options_default(&variant, &options);
	engine = PIN(variant.game_engine_index, 0, 5);
	kind = game_modes_variant_kind_name(&variant, &options);
	switch (game_modes_variant_mode(&variant, &options))
	{
	case _game_mode_infection:
		if (options.time_limit > 0)
			usnprintf(text, size - 1, L"%s\r\nTeams\r\nRound time: %d min", kind, options.time_limit);
		else
			usnprintf(text, size - 1, L"%s\r\nTeams\r\nUntil no human is left", kind);
		break;
	case _game_mode_gun_game:
		usnprintf(text, size - 1, L"%s\r\nFree for all\r\nA kill with each weapon", kind);
		break;
	default:
		usnprintf(text, size - 1, L"%s\r\n%s\r\nScore to win: %ld", kind ? kind : engine_names[engine],
			variant.universal_variant.teams ? L"Teams" : L"Free for all", variant.universal_variant.score_to_win);
		break;
	}
	text[size - 1] = 0;
}

/* "mp profiles list initialize": on the gametype used last */
static boolean gametype_list_initialize(struct widget_instance *list)
{
	struct widget_instance *spinner = named(list, "list_item_0_chooser_spinner", 0);
	short last, index;

	multiplayer.gametype_count = ui_widget_port_gametypes(multiplayer.gametypes, MAXIMUM_GAMETYPES, &last);
	gametype_bank_read(multiplayer.gametype_count ? gametype_bank(multiplayer.gametypes[last]) : GAMETYPE_BANK_STANDARD);
	for (index = 0; index < multiplayer.bank_count; index++)
	{
		if (multiplayer.bank[index] == last)
			multiplayer.gametype_chosen = index;
	}
	multiplayer.gametype_first = (short)PIN(multiplayer.gametype_chosen - GAMETYPE_ROWS / 2, 0,
		MAX(0, multiplayer.bank_count - GAMETYPE_ROWS));
	if (spinner)
		spinner->parameters.list.selected_index = multiplayer.bank_shown;
	focus_row(list, (short)(1 + multiplayer.gametype_chosen - multiplayer.gametype_first));
	return TRUE;
}

/* a gametype's name, in a row's text (the name's own buffer is longer:
playlist_profile_get_display_name fills MAX_GAMENAME characters) */
static void gametype_display_name(long profile_index, wchar_t *text)
{
	wchar_t name[MAX_GAMENAME];

	text[0] = 0;
	if (playlist_profile_get_display_name(profile_index, name))
	{
		ustrncpy(text, name, ROW_TEXT_LENGTH - 1);
		text[ROW_TEXT_LENGTH - 1] = 0;
	}
}

static void gametype_name(short item, wchar_t *text)
{
	text[0] = 0;
	if (item >= 0 && item < multiplayer.bank_count)
		gametype_display_name(multiplayer.gametypes[multiplayer.bank[item]], text);
}

/* "gt select list update": the bank shown, its rows (from the second), and
the gametype chosen: its name, picture and rules */
static void gametype_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *spinner = named(list, "list_item_0_chooser_spinner", 0);
	struct widget_instance *row;
	struct widget_instance *widget;
	short row_index = 0, focused = NONE, count;

	if (spinner && spinner->parameters.list.selected_index != multiplayer.bank_shown)
		gametype_bank_read((short)PIN(spinner->parameters.list.selected_index, 0, NUMBER_OF_GAMETYPE_BANKS - 1));
	count = (short)MIN(multiplayer.bank_count, GAMETYPE_ROWS);
	/* (the rows after the spinner's: scrolled as list_scroll does) */
	for (row = list->child; row; row = row->next, row_index++)
	{
		if (row == list->focused_child && row_index >= 1 && row_index <= GAMETYPE_ROWS)
			focused = (short)(row_index - 1);
	}
	if (focused == count - 1 && multiplayer.gametype_first + count < multiplayer.bank_count && list_scrolls_at_end())
	{
		multiplayer.gametype_first++;
		focus_row(list, (short)focused);
		focused--;
	}
	else if (focused == 0 && multiplayer.gametype_first > 0 && list_scrolls_at_end())
	{
		multiplayer.gametype_first--;
		focus_row(list, 2);
		focused++;
	}
	if (focused != NONE)
		multiplayer.gametype_chosen = (short)(multiplayer.gametype_first + focused);
	for (row = list->child, row_index = 0; row; row = row->next, row_index++)
	{
		wchar_t text[ROW_TEXT_LENGTH];

		if (row_index < 1 || row_index > GAMETYPE_ROWS)
			continue;
		row->visible = row_index - 1 < count;
		gametype_name((short)(multiplayer.gametype_first + row_index - 1), text);
		text_set(named(row, "list_item_text", 0), text);
		if (named(row, "list_item_arrows", 0))
			named(row, "list_item_arrows", 0)->visible = row == list->focused_child;
		if (named(row, "scroll_up_button", 0))
			named(row, "scroll_up_button", 0)->visible = FALSE;
		if (named(row, "scroll_down_button", 0))
			named(row, "scroll_down_button", 0)->visible = FALSE;
	}
	visible_set(named(description, "locked_gametype_icon", 0), FALSE);
	visible_set(named(description, "gametype_right_item", 0), multiplayer.bank_count > 0);
	if (multiplayer.gametype_chosen < multiplayer.bank_count)
	{
		struct game_variant variant;
		wchar_t text[ROW_TEXT_LENGTH * 2];

		gametype_name(multiplayer.gametype_chosen, text);
		text_set(named(description, "gametype_right_name", 0), text);
		if (playlist_profile_get(multiplayer.gametypes[multiplayer.bank[multiplayer.gametype_chosen]], &variant))
		{
			long engine = PIN(variant.game_engine_index, 0, 5);

			if ((widget = named(description, "gametype_right_pic", 0)) != NULL)
				widget->animation.current_frame_index = (short)engine;
			gametype_rules_text(multiplayer.gametypes[multiplayer.bank[multiplayer.gametype_chosen]], text,
				NUMBEROF(text));
			text_set(named(description, "gametype_right_data", 0), text);
		}
	}
	profile_name_show(description);
}

/* "mp profile set for game" */
static boolean gametype_choose(struct widget_instance *list)
{
	gametype_list_update(list);
	gametype_setup_end();
	if (multiplayer.gametype_chosen >= multiplayer.bank_count)
		return campaign_fail();
	return ui_widget_port_gametype_choose(multiplayer.gametypes[multiplayer.bank[multiplayer.gametype_chosen]]);
}

/* ---- the server settings */

static void game_name_done(char const *text)
{
	short index;

	for (index = 0; text[index] && index < NUMBEROF(multiplayer.game_name) - 1; index++)
		multiplayer.game_name[index] = (wchar_t)(unsigned char)text[index];
	multiplayer.game_name[index] = 0;
}

/* Whether a network game is co-op: a campaign level or a Custom Edition
campaign map, with no game engine (set up by
ui_widget_port_cooperative_level_choose). A Custom Edition map this machine
has not is taken to be one: co-op is the only game with no game engine. */
static boolean game_cooperative(struct network_game const *game)
{
	return game && !game->variant.game_engine_index &&
		(custom_edition_maps_level_campaign(game->map.name) ||
		(custom_edition_level_name(game->map.name) && custom_edition_maps_display_index(game->map.name) == NONE));
}

/* the same, for the game this machine is hosting */
static boolean hosting_cooperative(void)
{
	void *server = global_network_game_server_get();

	return server && game_cooperative(network_game_server_get_game(server));
}

/* Server Setup's gametype rows, which co-op hides */
static char const *const server_settings_gametype_rows[] =
{
	"op_game_type", "op_player_options", "op_item_options", "op_vehicle_options", "op_indicator_options",
	"op_team_options",
};

/* the most players a co-op game hosted starts with (maximum_players') */
#define COOPERATIVE_DEFAULT_PLAYERS 16
/* Server Setup's help for co-op's options' rows (CO-OP OPTIONS, VOICE AND
VOTING), PASSWORD's and MAP SHARING's (its help_strings, tools/port_settings.py) */
#define COOPERATIVE_OPTIONS_HELP 12
#define COOPERATIVE_VOICE_OPTIONS_HELP 13
#define SERVER_PASSWORD_HELP 14
#define MAP_SHARE_OPTIONS_HELP 15
/* ... and GAME's first choice's (each choice its own) */
#define GAME_MODE_HELP 16

/* Server Setup's GAME: the kind of game hosted (network.game_mode) and its
tick rate (network.sim_rate), as tools/port_settings.py's GAME_MODES have
them, in this order; a rate of 0 is Legacy (OpenCE's, at 30) */
static long const server_settings_game_modes[] = { 30, 60, 120, 0 };
#define DEFAULT_GAME_MODE 1

/* the choice network.game_mode and network.sim_rate make */
static short server_settings_game_mode(void)
{
	char const *mode = config_string("network.game_mode");
	long rate = config_integer("network.sim_rate");
	short index;

	if (mode && !strcmp(mode, "legacy"))
		return NUMBEROF(server_settings_game_modes) - 1;
	for (index = 0; index < NUMBEROF(server_settings_game_modes) - 1; index++)
	{
		if (server_settings_game_modes[index] == rate)
			return index;
	}
	return DEFAULT_GAME_MODE;
}

/* ... written, if it is another */
static void server_settings_game_mode_set(short index)
{
	long rate;
	char text[16];

	if (index < 0 || index >= NUMBEROF(server_settings_game_modes) || index == server_settings_game_mode())
		return;
	rate = server_settings_game_modes[index];
	if (!config_write("network.game_mode", rate ? "openh1" : "legacy"))
		platform_log("menus: could not write network.game_mode to config.toml");
	if (rate)
	{
		snprintf(text, sizeof(text), "%ld", rate);
		if (!config_write("network.sim_rate", text))
			platform_log("menus: could not write network.sim_rate to config.toml");
	}
}

/* co-op's EXTRA ENEMIES' choices (network.coop_enemies_mode's values, as
gametype_options' coop_extra_enemies_spinner has them, in this order): its
amount's row is the choice's */
enum
{
	_cooperative_enemies_per_player,
	_cooperative_enemies_multiplier,
	_cooperative_enemies_none,
	NUMBER_OF_COOPERATIVE_ENEMIES_MODES
};

/* co-op's FRIENDLY FIRE's choices (network.coop_friendly_fire's values, as
gametype_options' coop_friendly_fire_spinner and server_start have them, in
this order) */
static short const cooperative_friendly_fire_modes[] =
{
	_friendly_fire_off, _friendly_fire_on, _friendly_fire_shields_only, _friendly_fire_explosives_only
};

/* the most players Server Setup shows and sets: the multiplayer game's, or
the co-op game's (COOPERATIVE_DEFAULT_PLAYERS the first time in each game
hosted: multiplayer_host) */
static short *server_settings_maximum_players_index(void)
{
	short index;

	if (!hosting_cooperative())
		return &multiplayer.maximum_players_index;
	if (!multiplayer.cooperative_maximum_players_set)
	{
		multiplayer.cooperative_maximum_players_index = NUMBEROF(maximum_players) - 1;
		for (index = 0; index < NUMBEROF(maximum_players); index++)
		{
			if (maximum_players[index] == COOPERATIVE_DEFAULT_PLAYERS)
				multiplayer.cooperative_maximum_players_index = index;
		}
		multiplayer.cooperative_maximum_players_set = TRUE;
	}
	return &multiplayer.cooperative_maximum_players_index;
}

/* Server Setup's LISTING, PRIVATE: the multiplayer game's (as
network.host_public started it), or co-op's, saved in network.coop_public */
static boolean server_settings_private(void)
{
	return hosting_cooperative() ? !config_boolean("network.coop_public") : multiplayer.game_private;
}

static void server_settings_private_set(boolean private_game)
{
	if (!hosting_cooperative())
		multiplayer.game_private = private_game;
	else if (private_game != server_settings_private() &&
		!config_write("network.coop_public", private_game ? "false" : "true"))
	{
		platform_log("menus: could not write network.coop_public to config.toml");
	}
}

/* "server settings init": the game's name (player 1's, else the one given
last), the most players, the gametype's copy (once: the screen is made
again on coming back from an option's screen) */
static boolean server_settings_initialize(struct widget_instance *list)
{
	struct widget_instance *spinner = named(list, "max_players_spinner", 0);

	/* (co-op has no gametype to edit) */
	if (!hosting_cooperative())
		gametype_setup_begin();
	if (!multiplayer.game_name[0] && player_ui_get_active_player_profile_index(0) != NONE)
	{
		struct player_profile profile;

		player_ui_get_active_player_profile(0, &profile);
		ustrncpy(multiplayer.game_name, profile.player_name, NUMBEROF(multiplayer.game_name) - 1);
	}
	if (spinner)
		spinner->parameters.list.selected_index = *server_settings_maximum_players_index();
	/* (PUBLIC or PRIVATE: this game's; the screen is made again on coming
	back from an option's screen) */
	if ((spinner = named(list, "listing_spinner", 0)) != NULL)
		spinner->parameters.list.selected_index = server_settings_private() ? 1 : 0;
	/* (GAME: as network.game_mode and network.sim_rate have it) */
	if ((spinner = named(list, "game_mode_spinner", 0)) != NULL)
		spinner->parameters.list.selected_index = server_settings_game_mode();
	p2p_set_hosting_public(multiplayer.mode == _multiplayer_mode_host_internet && !server_settings_private());
	return TRUE;
}

static void wide_to_text(wchar_t const *wide, char *text, short size)
{
	short index;

	for (index = 0; wide[index] && index < size - 1; index++)
		text[index] = wide[index] < 0x80 ? (char)wide[index] : '?';
	text[index] = 0;
}

/* Server Setup in a game of this machine alone (Create Game's OFFLINE): the
network's rows (GAME, INVITE LINK, MAP SHARING) hidden, and in their place a
line saying what the game is (tools/port_settings.py's LOCAL_NOTE). Its bots
are the lobby's, as every game's (lobby_menu.inc) */
static char const *const server_settings_network_rows[] = { "op_game_mode", "op_invite", "op_map_share_options" };

static void server_settings_local(struct widget_instance *list, boolean local)
{
	short index;

	for (index = 0; index < NUMBEROF(server_settings_network_rows); index++)
		visible_set(named(list, server_settings_network_rows[index], 0), !local);
	visible_set(named(screen_of(list), "local_note", 0), local);
}

/* "server settings update" */
static void server_settings_update(struct widget_instance *list)
{
	struct widget_instance *spinner = named(list, "max_players_spinner", 0);
	struct widget_instance *row = named(list, "op_server_name", 0);
	char text[TEXT_FIELD_LENGTH];

	if (spinner)
		*server_settings_maximum_players_index() = (short)PIN(spinner->parameters.list.selected_index, 0,
			NUMBEROF(maximum_players) - 1);
	wide_to_text(multiplayer.game_name, text, sizeof(text));
	text_field_show(named(list, "server_name_value", 0), text, text_field_editing(row));
	if (multiplayer.mode != _multiplayer_mode_host_internet)
		snprintf(text, sizeof(text), "NONE: LOCAL NETWORK ONLY");
	else if (!config_boolean("network.online"))
		snprintf(text, sizeof(text), "INTERNET PLAY IS OFF (SETTINGS)");
	else if (!p2p_invite_link(text, sizeof(text)))
		snprintf(text, sizeof(text), "MADE WHEN THE GAME STARTS");
	text_field_show(named(list, "invite_value", 0), text, FALSE);
	{
		wchar_t type[ROW_TEXT_LENGTH];

		gametype_setup_type(type);
		text_set(named(list, "game_type_value", 0), type);
	}
	settings_help(list);
	{
		boolean cooperative = hosting_cooperative();
		short row;

		for (row = 0; row < NUMBEROF(server_settings_gametype_rows); row++)
			visible_set(named(list, server_settings_gametype_rows[row], 0), !cooperative);
		/* co-op's options in their place, rows opening their screens
		(tools/port_settings.py's COOP_SETUP_SCREENS), each its help */
		{
			struct widget_instance *help = list->parameters.list.extended_description;

			visible_set(named(list, "op_coop_options", 0), cooperative);
			visible_set(named(list, "op_voice_options", 0), cooperative);
			if (help && list->focused_child == named(list, "op_coop_options", 0))
				help->parameters.text_box.string_list_index = COOPERATIVE_OPTIONS_HELP;
			else if (help && list->focused_child == named(list, "op_voice_options", 0))
				help->parameters.text_box.string_list_index = COOPERATIVE_VOICE_OPTIONS_HELP;
			/* (MAP SHARING's, any game's: map_share.c) */
			else if (help && list->focused_child == named(list, "op_map_share_options", 0))
				help->parameters.text_box.string_list_index = MAP_SHARE_OPTIONS_HELP;
		}
	}
	/* (a game of this machine alone: none of the network's rows) */
	{
		void *server = global_network_game_server_get();

		server_settings_local(list, server && network_game_is_splitscreen_local());
	}
	/* LISTING (an internet game's): PUBLIC, listed in everyone's server
	browser, or PRIVATE, for this game. Its help is its choice's */
	visible_set(named(list, "op_listing", 0), multiplayer.mode == _multiplayer_mode_host_internet &&
		config_boolean("network.online") && config_boolean("network.public_lobby"));
	if ((spinner = named(list, "listing_spinner", 0)) != NULL && named(list, "op_listing", 0) &&
		named(list, "op_listing", 0)->visible)
	{
		boolean public = spinner->parameters.list.selected_index == 0;
		struct widget_instance *help = list->parameters.list.extended_description;

		server_settings_private_set(!public);
		p2p_set_hosting_public(public);
		if (help && list->focused_child == named(list, "op_listing", 0))
			help->parameters.text_box.string_list_index = (short)(10 + spinner->parameters.list.selected_index);
	}
	/* GAME: OpenH1 at a tick rate, or Legacy (network.game_mode,
	network.sim_rate), written as it is chosen. Its help is its choice's */
	if ((spinner = named(list, "game_mode_spinner", 0)) != NULL)
	{
		struct widget_instance *help = list->parameters.list.extended_description;
		short index = (short)PIN(spinner->parameters.list.selected_index, 0, NUMBEROF(server_settings_game_modes) - 1);

		server_settings_game_mode_set(index);
		if (help && list->focused_child == named(list, "op_game_mode", 0))
			help->parameters.text_box.string_list_index = (short)(GAME_MODE_HELP + index);
	}
	/* PASSWORD (a PUBLIC internet game's): its stars, NONE if it has none */
	row = named(list, "op_password", 0);
	visible_set(row, named(list, "op_listing", 0) && named(list, "op_listing", 0)->visible &&
		!server_settings_private());
	if (row && !row->visible && text_field_editing(row))
		text_field_end(FALSE);
	if (row && row->visible)
	{
		struct widget_instance *help = list->parameters.list.extended_description;
		short index;

		for (index = 0; multiplayer.game_password[index] && index < (short)sizeof(text) - 1; index++)
			text[index] = '*';
		text[index] = 0;
		text_field_show(named(list, "password_value", 0), index ? text : "NONE", text_field_editing(row));
		if (help && list->focused_child == row)
			help->parameters.text_box.string_list_index = SERVER_PASSWORD_HELP;
	}
}

static void server_password_done(char const *text)
{
	snprintf(multiplayer.game_password, sizeof(multiplayer.game_password), "%s", text);
}

/* "ss edit server password" (its row's A: begins, or ends) */
static boolean server_password_edit(struct widget_instance *row)
{
	if (text_field_editing(row))
	{
		text_field_end(TRUE);
		return TRUE;
	}
	text_field_begin_masked(row, multiplayer.game_password, PASSWORD_LENGTH, server_password_done);
	return TRUE;
}

/* "ss edit server name" (its row's A: begins, or ends) */
static boolean server_name_edit(struct widget_instance *row)
{
	char text[TEXT_FIELD_LENGTH];

	if (text_field_editing(row))
	{
		text_field_end(TRUE);
		return TRUE;
	}
	wide_to_text(multiplayer.game_name, text, sizeof(text));
	text_field_begin(row, text, NUMBEROF(multiplayer.game_name) - 1, game_name_done);
	return TRUE;
}

/* "ss copy invite" */
static boolean invite_copy(void)
{
	char link[TEXT_FIELD_LENGTH];

	if (!p2p_invite_link(link, sizeof(link)))
		return campaign_fail();
	platform_clipboard_set(link);
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "ss start game": the settings given the server, then the lobby */
/* the co-op game's settings, as CO-OP OPTIONS left them in config.toml
(network.coop_friendly_fire, in cooperative_friendly_fire_modes' order,
network.coop_player_collisions and network.coop_ally_friendly_fire), given
the game's server: Server Setup's START, and a dedicated server's co-op
entry (server_director.c's server_apply_entry) */
void ui_widget_port_cooperative_settings_apply(void)
{
	static char const *const friendly_fire_names[] = { "off", "on", "shields_only", "explosives_only" };
	char const *friendly_fire = config_string("network.coop_friendly_fire");
	short index;

	multiplayer.cooperative_friendly_fire = _friendly_fire_on;
	for (index = 0; friendly_fire && index < (short)NUMBEROF(friendly_fire_names); index++)
	{
		if (!strcmp(friendly_fire, friendly_fire_names[index]))
			multiplayer.cooperative_friendly_fire = cooperative_friendly_fire_modes[index];
	}
	multiplayer.cooperative_no_player_collisions = !config_boolean("network.coop_player_collisions");
	network_game_server_port_set_cooperative_friendly_fire(multiplayer.cooperative_friendly_fire);
	network_game_server_port_set_cooperative_player_collisions(!multiplayer.cooperative_no_player_collisions);
	network_game_server_port_set_cooperative_ally_friendly_fire(config_boolean("network.coop_ally_friendly_fire"));
}

static boolean server_start(void)
{
	if (text_field_editing(NULL))
		text_field_end(TRUE);
	network_game_server_port_set_settings(multiplayer.game_name,
		maximum_players[PIN(*server_settings_maximum_players_index(), 0, NUMBEROF(maximum_players) - 1)]);
	/* (GAME as chosen, if no other machine has joined yet:
	network_standard.c) */
	network_standard_server_mode_read();
	/* (its listing's password: a PUBLIC internet game's, else none) */
	p2p_set_hosting_password(multiplayer.mode == _multiplayer_mode_host_internet && !server_settings_private() ?
		multiplayer.game_password : NULL);
	/* the gametype as Server Setup's options left it (co-op keeps its own,
	and its FRIENDLY FIRE) */
	if (hosting_cooperative())
	{
		gametype_setup_end();
		ui_widget_port_cooperative_settings_apply();
	}
	else if (!gametype_setup_apply())
		return campaign_fail();
	return global_network_game_server_get() != NULL;
}

/* ---- the browser (Join Game > Server Browser, LAN, Direct Link): the
games found on the LAN, or (Direct Link) from internet play's peers
(reached by an invite link, the clipboard, or Discord) */

static boolean game_from_peer(struct advertised_game const *game)
{
	unsigned long address;

	return p2p_peer_address(game->xnaddr + 2, &address) != 0;
}

/* (network_test.c's probe of an invite's game, halo --probe) whether the
client's index'th advertised game came from internet play's peers */
boolean menu_advertised_game_from_peer(long index)
{
	void *client = global_network_game_client_get();
	struct advertised_game *games = client ? network_game_client_get_available_games(client) : NULL;

	return games && index >= 0 && index < MAXIMUM_ADVERTISED_GAMES && game_from_peer(&games[index]);
}

static boolean advertised_in_progress(struct advertised_game *game)
{
	return network_game_client_advertised_game_in_progress(global_network_game_client_get(), game);
}

/* whether the advertised game's host is a dedicated server (OpenCE's halo
--server: HALO_PORT_ADVERTISED_DEDICATED_FLAG), with no player of its own */
static boolean advertised_dedicated(struct advertised_game *game)
{
	return network_game_client_advertised_game_dedicated(global_network_game_client_get(), game);
}

/* whether the game this machine joined last is a dedicated server's, as its
advertisement said: the lobby's lines (lobby_menu.inc) */
static boolean lobby_joined_dedicated;

/* the advertised game joined (ui_widget_port_join), into the lobby */
static boolean advertised_join(struct widget_instance *widget, struct advertised_game *game,
	boolean *widget_deleted)
{
	lobby_joined_dedicated = game && advertised_dedicated(game);
	return ui_widget_port_join(widget, game, LOBBY_NAME, widget_deleted);
}

static void browser_games_read(void)
{
	void *client = global_network_game_client_get();
	struct advertised_game *games = client ? network_game_client_get_available_games(client) : NULL;
	short pass, index;

	multiplayer.game_count = 0;
	if (!games || multiplayer.mode == _multiplayer_mode_server_browser)
		return;
	/* (the open games, then those under way) */
	for (pass = 0; pass < 2; pass++)
	{
		for (index = 0; index < MAXIMUM_ADVERTISED_GAMES; index++)
		{
			struct advertised_game *game = &games[index];

			if (network_game_client_advertised_game_is_valid(game) && !advertised_in_progress(game) == (pass == 0) &&
				game_from_peer(game) == (multiplayer.mode == _multiplayer_mode_direct_link))
			{
				multiplayer.games[multiplayer.game_count++] = game;
			}
		}
	}
	/* (the game chosen kept among the rows: local_browser_favorites) */
}

static short browser_row_index(struct widget_instance *row)
{
	return row && !strncmp(row->name, "server_item_", 12) ? (short)(atoi(row->name + 12) - 1) : NONE;
}

/* the rows a browser's list has (tools/port_settings.py's BROWSER_ROWS on
the computer, its touchscreen's fewer and taller: TOUCH_BROWSER_ROWS), at
most BROWSER_ROWS; the last list's, which browser_chosen_row reads */
static short browser_shown_rows = BROWSER_ROWS;

static short browser_rows_of(struct widget_instance *list)
{
	struct widget_instance *child;
	short rows = 0;

	for (child = list ? list->child : NULL; child; child = child->next)
	{
		if (browser_row_index(child) != NONE && rows < BROWSER_ROWS)
			rows++;
	}
	browser_shown_rows = rows ? rows : BROWSER_ROWS;
	return browser_shown_rows;
}

/* ---- the menus' shared parts (tools/port_settings.py's PARTS, in Settings'
look): text fitted to its box, cut short with "..." (menu_text_fit), or
wrapped to a number of lines; a strip of tabs, the current one framed and
its words white (menu_tabs_show); a footer's key prompts, the buttons'
pictures in their words drawn as the device in use has them (ui_widget.c),
some shown (menu_prompts_show). The server browser and Multiplayer use
them; a screen to come can, by the generator's names */

#define MENU_TEXT_ELLIPSIS_LENGTH 3

/* the first `kept` characters of text (without the spaces at their end)
and "...", into cut (size characters) */
static void menu_text_cut(wchar_t const *text, short kept, wchar_t *cut, short size)
{
	short index;

	kept = (short)MIN(kept, size - MENU_TEXT_ELLIPSIS_LENGTH - 1);
	while (kept > 0 && text[kept - 1] == ' ')
		kept--;
	for (index = 0; index < kept; index++)
		cut[index] = text[index];
	for (index = 0; index < MENU_TEXT_ELLIPSIS_LENGTH; index++)
		cut[kept + index] = '.';
	cut[kept + MENU_TEXT_ELLIPSIS_LENGTH] = 0;
}

/* text into fitted (size characters): all of it if it fits across width in
the font, else as much as fits with "..." after it (the most characters
that do, found by halves: a text is no narrower for more characters); all
of it while the font is not known */
static void menu_text_fit(long font, wchar_t const *text, wchar_t *fitted, short size, short width)
{
	short length = 0, low = 0, high;

	while (text && text[length] && length < size - 1)
		length++;
	settings_wide_copy(fitted, text, size);
	if (font == NONE || width <= 0 || settings_text_width(font, fitted) <= width)
		return;
	/* (low characters fit with the dots, high do not) */
	high = length;
	while (high - low > 1)
	{
		short middle = (short)((low + high) / 2);

		menu_text_cut(text, middle, fitted, size);
		if (settings_text_width(font, fitted) <= width)
			low = middle;
		else
			high = middle;
	}
	menu_text_cut(text, low, fitted, size);
}

/* a text box's text fitted to it (to `width` if that is less, and more
than 0), returning how wide it is drawn (0 while its font is not known) */
static short menu_text_box_fit(struct widget_instance *box, wchar_t const *text, short width)
{
	wchar_t fitted[ROW_TEXT_LENGTH];
	short box_width;
	long font = ui_widget_port_text_box_font(box, &box_width);

	if (!box)
		return 0;
	menu_text_fit(font, text, fitted, NUMBEROF(fitted), (short)(width > 0 ? MIN(width, box_width) : box_width));
	text_set(box, fitted);
	return font != NONE ? settings_text_width(font, fitted) : 0;
}

/* a text box's text wrapped to its width, `lines` lines at most: the last
of them, if there is more, the rest of the text fitted to it with "..." */
static void menu_text_box_wrap(struct widget_instance *box, wchar_t const *text, short lines)
{
	wchar_t wrapped[SETTINGS_PANEL_TEXT_LENGTH], last[SETTINGS_PANEL_TEXT_LENGTH];
	short width, line = 1, index, start = 0;
	long font = ui_widget_port_text_box_font(box, &width);

	if (!box)
		return;
	settings_text_wrap(text, wrapped, NUMBEROF(wrapped), font, width);
	for (index = 0; wrapped[index]; index++)
	{
		if (wrapped[index] != '\r' || wrapped[index + 1] != '\n')
			continue;
		if (line == lines)
		{
			short length = 0;

			/* (the rest on one line, fitted) */
			for (index = start; wrapped[index] && length < NUMBEROF(last) - 1; index++)
			{
				if (wrapped[index] == '\r')
					continue;
				last[length++] = wrapped[index] == '\n' ? ' ' : wrapped[index];
			}
			last[length] = 0;
			menu_text_fit(font, last, wrapped + start, (short)(NUMBEROF(wrapped) - start), width);
			break;
		}
		line++;
		start = (short)(index + 2);
	}
	text_set_length(box, wrapped, SETTINGS_PANEL_TEXT_LENGTH);
}

/* whether a column list is a strip of tabs (port_settings.py's _tab_strip:
its name ends in "_tabs") */
static boolean menu_tab_strip(struct widget_instance const *list)
{
	size_t length = list && list->name ? strlen(list->name) : 0;

	return length > 5 && !strcmp(list->name + length - 5, "_tabs");
}

/* a strip of tabs' pictures: each tab's frame of PARTS_TAB, the current one
framed (its last two frames) and its words white (pc_menu_text_color), the
one with the focus outlined */
static void menu_tabs_show(struct widget_instance *strip, short current)
{
	struct widget_instance *tab;
	boolean strip_focused = strip && strip->parent && strip->parent->focused_child == strip;
	short index = 0;

	for (tab = strip ? strip->child : NULL; tab; tab = tab->next, index++)
	{
		tab->animation.current_frame_index =
			(short)((index == current ? 2 : 0) + (strip_focused && tab == strip->focused_child ? 1 : 0));
	}
}

static void focus_off_hidden(struct widget_instance *bar);

/* a footer's prompt shown or hidden (its bar's focus off it, if hidden) */
static void menu_prompts_show(struct widget_instance *bar, char const *name, boolean shown)
{
	visible_set(named(bar, name, 0), shown);
	focus_off_hidden(bar);
}

/* the browsers' rows' state, for their colours (pc_menu_text_color):
whether each row's game is closed (full, or starting: its players grey), a
favorite, and a favorite offline (its text all grey); its ping's
milliseconds (-1: none), and whether it is being measured ("..." faded) */
static boolean browser_row_closed[BROWSER_ROWS];
#define PLAYERS_COLOR_CLOSED 0x7F8A99FFUL
static boolean browser_row_favorite[BROWSER_ROWS];
static boolean browser_row_offline[BROWSER_ROWS];
static short browser_row_pings[BROWSER_ROWS];
static boolean browser_row_measuring[BROWSER_ROWS];
/* a ping's bars (port_settings.py's PING_BARS) and its number's colour:
good under PING_GOOD milliseconds, fair under PING_FAIR, poor above */
enum
{
	PING_GOOD = 80,
	PING_FAIR = 160,
};
#define PING_COLOR_GOOD 0x5ED38CFFUL
#define PING_COLOR_FAIR 0xE8C547FFUL
#define PING_COLOR_POOR 0xE86A5AFFUL
#define PING_COLOR_MEASURING 0x4A5A78FFUL

static boolean browser_sort_header_current(struct widget_instance const *widget);
/* (Host Controls' team colours: host_controls_menu.inc) */
static boolean host_controls_text_color(struct widget_instance *widget, real_argb_color *color);

/* ui_widget.c: a text's colour in place of its own (FALSE: its own): a
strip of tabs' current one white, and the browsers' column title they are
sorted by; in the server browsers' rows, a ping by
how good it is ("..." faded while it is measured), a closed game's players
grey, and an offline favorite's row grey */
boolean pc_menu_text_color(struct widget_instance *widget, real_argb_color *color)
{
	short row = widget->parent ? browser_row_index(widget->parent) : NONE;
	unsigned long rgba;

	if (host_controls_text_color(widget, color))
		return TRUE;
	if ((menu_tab_strip(widget->parent) && widget->animation.current_frame_index >= 2) ||
		browser_sort_header_current(widget))
	{
		color->red = color->green = color->blue = 1.0f;
		return TRUE;
	}
	if (row < 0 || row >= BROWSER_ROWS)
		return FALSE;
	if (browser_row_offline[row])
		rgba = PLAYERS_COLOR_CLOSED;
	else if (!strcmp(widget->name, "server_item_ping_ms"))
	{
		short ping = browser_row_pings[row];

		if (ping < 0 && !browser_row_measuring[row])
			return FALSE;
		rgba = ping < 0 ? PING_COLOR_MEASURING : ping < PING_GOOD ? PING_COLOR_GOOD : ping < PING_FAIR ?
			PING_COLOR_FAIR : PING_COLOR_POOR;
	}
	else
	{
		if (strcmp(widget->name, "server_item_players") || !browser_row_closed[row])
			return FALSE;
		rgba = PLAYERS_COLOR_CLOSED;
	}
	color->red = (real)((rgba >> 24) & 0xFF) / 255.0f;
	color->green = (real)((rgba >> 16) & 0xFF) / 255.0f;
	color->blue = (real)((rgba >> 8) & 0xFF) / 255.0f;
	return TRUE;
}

/* a bar of buttons' focus, off one hidden: on its first one shown */
static void focus_off_hidden(struct widget_instance *bar)
{
	struct widget_instance *button;
	short button_index = 0;

	for (button = bar && bar->focused_child && !bar->focused_child->visible ? bar->child : NULL; button;
		button = button->next, button_index++)
	{
		if (button->visible)
		{
			bar->focused_child = button;
			bar->parameters.list.selected_index = button_index;
			break;
		}
	}
}

/* whether the browser's list's child can take focus (once visible) */
static boolean browser_item_usable(struct widget_instance *child)
{
	/* (and the column titles, which sort: up from the first row; the tabs
	above them; JOIN GAME, in the panel) */
	return browser_row_index(child) != NONE || !strcmp(child->name, "join_game_button_bar") ||
		!strcmp(child->name, "join_game_header") || !strcmp(child->name, "browser_tabs") ||
		!strcmp(child->name, "browser_join_bar");
}

/* the browser's focus, off what cannot have it (at the start, or a row whose
game went): the first game, else the buttons (off those hidden); the row of
the game chosen is `chosen_row` (NONE: none shown) */
static void browser_focus(struct widget_instance *list, short chosen_row)
{
	char const *const choices[] = { "server_item_1", "join_game_button_bar" };
	struct widget_instance *focused = list->focused_child;
	struct widget_instance *row;
	short index;

	/* (the rows' backgrounds, port_settings.py's PARTS row: the chosen
	game's brighter, the focused one's outlined. The engine shows a list
	item's focus only on a bitmap of two frames, and theirs has three:
	normal, focused, chosen) */
	for (row = list->child; row; row = row->next)
	{
		short row_index = browser_row_index(row);

		if (row_index != NONE)
			row->animation.current_frame_index = row_index == chosen_row ? 2 : row == focused ? 1 : 0;
	}
	focus_off_hidden(named(list, "join_game_button_bar", 0));
	if (focused && focused->visible && !focused->disabled)
		return;
	for (index = 0; index < NUMBEROF(choices); index++)
	{
		struct widget_instance *child;
		short child_index = 0;

		for (child = list->child; child; child = child->next, child_index++)
		{
			if (!strcmp(child->name, choices[index]) && child->visible)
			{
				list->focused_child = child;
				list->parameters.list.selected_index = child_index;
				return;
			}
		}
	}
}

/* (the server browser's: below) */
static void lobby_browser_begin(void);
static boolean direct_link_from_clipboard(void);
static void lobby_browser_update(struct widget_instance *list);
static boolean lobby_browser_select(struct widget_instance *widget, short controller, short row,
	boolean *widget_deleted);
static void browser_prompts_show(struct widget_instance *list);
static void browser_filters_refresh(void);

/* "gamespy screen init": the mode's title and parts; the games' client */
static boolean browser_initialize(struct widget_instance *screen, struct event_record *event,
	boolean *widget_deleted)
{
	char const *title = multiplayer.mode == _multiplayer_mode_lan ? "header_lan" :
		multiplayer.mode == _multiplayer_mode_direct_link ? "header_direct_link" : "header_server_browser";
	char const *const titles[] = { "header_lan", "header_direct_link", "header_server_browser" };
	boolean link = multiplayer.mode == _multiplayer_mode_direct_link;
	short index;

	server_browser_initialize(screen);
	/* (the filters as Filters left them: browser_filters_read, below) */
	browser_filters_refresh();
	for (index = 0; index < NUMBEROF(titles); index++)
		visible_set(named(screen, titles[index], 0), !strcmp(titles[index], title));
	/* (the tabs and their prompts, but for Join by Link's list, whose games
	are its invites) */
	visible_set(named(screen, "browser_tabs", 0), !link);
	visible_set(named(screen, "tab_prompt_previous", 0), !link);
	visible_set(named(screen, "tab_prompt_next", 0), !link);
	{
		struct widget_instance *list = named(screen, "join_game_items_list", 0);
		struct widget_instance *child;

		for (child = list ? list->child : NULL; child; child = child->next)
			child->disabled = !browser_item_usable(child);
		if (list)
		{
			browser_prompts_show(list);
			browser_focus(list, NONE);
		}
	}
	/* (the columns' sort arrows, both of each over its title: the sorted
	column's shown, its way, each frame: browser_sort_headers_show) */
	for (index = 0; named(screen, "header_sort_arrows", index); index++)
		visible_set(named(screen, "header_sort_arrows", index), FALSE);
	multiplayer.game_chosen = 0;
	multiplayer.game_first = 0;
	/* (the server browser's games are internet play's listings; joining
	one reaches its host, whose game the client then finds as Direct Link's) */
	if (multiplayer.mode == _multiplayer_mode_server_browser)
		lobby_browser_begin();
	/* (JOIN BY LINK with an invite link copied: joined at once, as PASTE
	LINK would) */
	if (multiplayer.mode == _multiplayer_mode_direct_link && platform_clipboard_has_invite())
		direct_link_from_clipboard();
	return ui_widget_port_browse(screen, event, widget_deleted);
}

/* a scenario's name without its path */
static char const *scenario_name(char const *path)
{
	char const *name = strrchr(path, '\\');

	return name ? name + 1 : path;
}

/* the campaign level whose scenario a path or name is (a network co-op
game's map), else NONE */
static short campaign_level_of(char const *map_name)
{
	short level;

	/* (a Custom Edition map, custom_maps\a30, is never one) */
	if (custom_edition_level_name(map_name))
		return NONE;
	for (level = 0; level < NUMBER_OF_SINGLE_PLAYER_LEVELS; level++)
	{
		if (!_stricmp(scenario_name(main_get_solo_level_name(level)), scenario_name(map_name)))
			return level;
	}
	return NONE;
}

/* a game's type as the browsers and lobby show it: its engine's name, or
CO-OP for one with none (network co-op: ui_widget_port_cooperative_level_choose) */
static wchar_t const *game_type_name(long engine_type)
{
	return engine_type == 0 ? L"CO-OP" : engine_names[PIN(engine_type, 0, 5)];
}

/* ... or its OpenCE gametype's kind (its advertisement's or listing's:
game_modes.h), "HC" for Hardcore in the browsers' narrow column */
static wchar_t const *game_kind_name(long engine_type, byte kind, boolean abbreviated)
{
	wchar_t const *name = game_modes_kind_name(engine_type, kind, abbreviated);

	return name ? name : game_type_name(engine_type);
}

/* an advertised (LAN, Direct Link) game's kind */
static byte advertised_game_kind(struct advertised_game *game)
{
	return network_game_client_advertised_game_kind(global_network_game_client_get(), game);
}

/* ... its rate (a Standard game's, a Legacy game's 30), and whether it is a
Standard game (port/linux/game/network_standard.c) */
static long advertised_game_rate(struct advertised_game *game)
{
	return network_game_client_advertised_game_rate(global_network_game_client_get(), game);
}

static boolean advertised_game_standard(struct advertised_game *game)
{
	return network_game_client_advertised_game_standard(global_network_game_client_get(), game);
}

static boolean advertised_game_mixed(struct advertised_game *game)
{
	return network_game_client_advertised_game_mixed(global_network_game_client_get(), game);
}

/* a map's name as the menus show it (a campaign level's too, hosted as
network co-op; its scenario's name, if not one of theirs), from its
scenario's path or name */
static void map_display_name(char const *map_name, wchar_t *text)
{
	char const *const *names;
	short index;
	short count = xbox_multiplayer_map_count(ui_widget_port_multiplayer_maps(&names, NULL));

	/* (a Custom Edition map's, if this machine has it: custom_edition_maps.c) */
	if (custom_edition_level_name(map_name))
	{
		short display_index = custom_edition_maps_display_index(map_name);
		wchar_t const *name = display_index != NONE ? custom_edition_maps_name(display_index) : NULL;

		if (name)
		{
			ustrncpy(text, name, ROW_TEXT_LENGTH - 1);
			text[ROW_TEXT_LENGTH - 1] = 0;
			return;
		}
		map_name = scenario_name(map_name);
		count = 0;
	}
	for (index = 0; index < count; index++)
	{
		if (!_stricmp(scenario_name(names[index]), scenario_name(map_name)))
		{
			string_get("pc\\main_menu\\mp_map_list", index, text);
			return;
		}
	}
	if ((index = campaign_level_of(map_name)) != NONE)
	{
		string_get("pc\\main_menu\\map_list", index, text);
		return;
	}
	/* (a stock map's variant, named <map>@<variant> (bloodgulch@ce+): the
	map's own name) */
	if (strchr(scenario_name(map_name), '@'))
	{
		char base[ROW_TEXT_LENGTH];
		char const *name = scenario_name(map_name);
		size_t length = MIN((size_t)(strchr(name, '@') - name), sizeof(base) - 1);

		memcpy(base, name, length);
		base[length] = 0;
		for (index = 0; length && index < count; index++)
		{
			if (!_stricmp(scenario_name(names[index]), base))
			{
				string_get("pc\\main_menu\\mp_map_list", index, text);
				return;
			}
		}
	}
	for (index = 0; map_name[index] && index < ROW_TEXT_LENGTH - 1; index++)
		text[index] = (wchar_t)(unsigned char)map_name[index];
	text[index] = 0;
}

/* a map's name as the menus show it, at most size characters with its end
(game_engine.c's carnage report) */
void ui_widget_port_map_name(char const *map_name, wchar_t *text, short size)
{
	wchar_t name[ROW_TEXT_LENGTH];

	map_display_name(map_name, name);
	ustrncpy(text, name, size - 1);
	text[size - 1] = 0;
}

static void game_map_name(struct advertised_game const *game, wchar_t *text)
{
	map_display_name(game->map_name, text);
}

/* ---- the browsers' favorites (port/linux/src/browser_favorites.c): a game
starred (Y, F on the keyboard, or a click on its row's star) is kept on this
machine, known by its host's internet-play identifier (an internet or Direct
Link game) or its address (a local network game), not by its name. The
favorites found go to the top of the list, the FAVORITES tab shows only
them, and those not found stay listed after the others (on it and ALL),
grey, by their last known name, map and gametype. A host's identifier is
new each time it starts, so an internet favorite not found by it is found
again by its last known name, and takes the new one. Nothing is sent */

/* the favorites listed offline, after the games found (their indices) */
static short browser_offline[BROWSER_FAVORITES_MAXIMUM];
static short browser_offline_count;
/* how many of the games found are favorites (the first ones listed) */
static short browser_found_favorites;
/* the game starred or unstarred last, whose row takes the focus once the
list is ordered again (empty: none) */
static char browser_follow_key[BROWSER_FAVORITE_KEY_SIZE];

static void listing_favorite(struct p2p_listing const *game, struct browser_favorite *favorite)
{
	csmemset(favorite, 0, sizeof(*favorite));
	browser_favorite_key_internet(game->identifier, favorite->key);
	snprintf(favorite->name, sizeof(favorite->name), "%s", game->name);
	snprintf(favorite->map, sizeof(favorite->map), "%s", game->map);
	snprintf(favorite->gametype, sizeof(favorite->gametype), "%s", game->gametype);
	favorite->engine_type = game->engine_type;
	favorite->kind = game->kind;
}

/* a local network game's, by its host's address; a Direct Link game's by
its host's identifier, as the server browser's */
static void advertised_favorite(struct advertised_game *game, struct browser_favorite *favorite)
{
	unsigned long address;
	short index;

	csmemset(favorite, 0, sizeof(*favorite));
	if (game_from_peer(game))
		browser_favorite_key_internet(game->xnaddr + 2, favorite->key);
	else
	{
		csmemcpy(&address, game->xnaddr + 8, sizeof(address));
		browser_favorite_key_address(address, favorite->key);
	}
	for (index = 0; index < NUMBEROF(game->game_name) && game->game_name[index] &&
		index < (short)sizeof(favorite->name) - 1; index++)
	{
		favorite->name[index] = game->game_name[index] >= 0x20 && game->game_name[index] < 0x7F ?
			(char)game->game_name[index] : '?';
	}
	snprintf(favorite->map, sizeof(favorite->map), "%s", scenario_name(game->map_name));
	favorite->engine_type = game->engine_type;
	favorite->kind = advertised_game_kind(game);
}

/* the favorites of the games found (each one's index, else NONE), those
found again by name given their new keys, each seen now; then the
favorites not found whose keys are of the kind the list shows ("net:",
"lan:"; NULL: none), its offline ones */
static void browser_favorites_match(struct browser_favorite const *games, short count, short *favorite_of,
	char const *offline_kind)
{
	boolean seen[BROWSER_FAVORITES_MAXIMUM];
	short favorites = (short)browser_favorites_count();
	short index, favorite;

	csmemset(seen, 0, sizeof(seen));
	for (index = 0; index < count; index++)
	{
		favorite_of[index] = (short)browser_favorites_find(games[index].key);
		if (favorite_of[index] != NONE)
			seen[favorite_of[index]] = TRUE;
	}
	for (index = 0; index < count; index++)
	{
		if (favorite_of[index] != NONE || strncmp(games[index].key, "net:", 4) || !games[index].name[0])
			continue;
		for (favorite = 0; favorite < favorites; favorite++)
		{
			struct browser_favorite const *known = browser_favorites_get(favorite);

			if (!seen[favorite] && !strncmp(known->key, "net:", 4) && !strcmp(known->name, games[index].name))
			{
				browser_favorites_rekey(favorite, games[index].key);
				seen[favorite] = TRUE;
				favorite_of[index] = favorite;
				break;
			}
		}
	}
	for (index = 0; index < count; index++)
	{
		if (favorite_of[index] != NONE)
			browser_favorites_seen(favorite_of[index], &games[index]);
	}
	browser_offline_count = 0;
	for (favorite = 0; offline_kind && favorite < favorites; favorite++)
	{
		if (!seen[favorite] && !strncmp(browser_favorites_get(favorite)->key, offline_kind, 4))
			browser_offline[browser_offline_count++] = favorite;
	}
}

/* ---- the browsers' order and tabs (Join Game's three lists): a column's
title chosen (a click, a tap, or A on it: up from the first row reaches the
titles, left and right go between them) sorts the games by it, again the
other way, the title brighter with its arrow; X (Tab on the keyboard) sorts
by the next column's. The favorites stay first, each part sorted alike; in
the order the lists open in (the most players first), the dedicated servers
come before the games players host. The tabs
(LB and RB, Q and E, A or a click on one) show every game, the dedicated
servers (on the LAN by their advertisement's word, on the internet by a
master server's: port/linux/src/master_list.c), OpenH1's own, Legacy ones
(OpenCE's netcode: port/linux/game/network_standard.c), co-op games, or the
favorites. Nothing is sent: it is the lists', and kept while
the game runs */

enum
{
	_browser_sort_players,
	_browser_sort_ping,
	_browser_sort_name,
	_browser_sort_map,
	_browser_sort_mode,
	NUMBER_OF_BROWSER_SORTS
};

/* the tabs, in port_settings.py's BROWSER_TABS' order */
enum
{
	_browser_tab_all,
	_browser_tab_dedicated,
	_browser_tab_openh1,
	_browser_tab_legacy,
	_browser_tab_coop,
	_browser_tab_favorites,
	NUMBER_OF_BROWSER_TABS
};

#define BROWSER_MODE_LENGTH 24

/* what a game is sorted and filtered by */
struct browser_sort_entry
{
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	wchar_t mode[BROWSER_MODE_LENGTH];
	short players;
	/* milliseconds, NONE if not known (last whichever way) */
	short ping;
	short rate;
	boolean openh1;
	/* the campaign's (network co-op: no gametype) */
	boolean coop;
	/* a dedicated server's */
	boolean dedicated;
	/* what the filters see of it (browser_filters_show): its most players
	(0: not known), its game engine (0: co-op's none), its password, and
	its map's name as the rows show it */
	short maximum_players;
	short engine_type;
	boolean locked;
	wchar_t map_shown[ROW_TEXT_LENGTH];
};

static short browser_sort_column = _browser_sort_players;
static boolean browser_sort_descending = TRUE;
static short browser_tab = _browser_tab_all;

/* each column's title (op_sort_header_*) and its words (port_settings.py's
BROWSER_HEADERS), and the way it sorts first (more players first) */
static struct
{
	char const *header;
	wchar_t const *title;
	boolean descending;
} const browser_sorts[NUMBER_OF_BROWSER_SORTS] =
{
	{ "op_sort_header_players", L"PLAYERS", TRUE },
	{ "op_sort_header_ping", L"PING", FALSE },
	{ "op_sort_header_server_name", L"SERVER", FALSE },
	{ "op_sort_header_map", L"MAP", FALSE },
	{ "op_sort_header_type", L"MODE", FALSE },
};
/* the space a title's arrow leaves before its art (bitmaps/arrow_sm_up's,
in the middle of its 16 units) */
#define BROWSER_ARROW_GAP 2

/* text compared without case */
static int browser_text_compare(char const *a, char const *b)
{
	for (; *a && *b; a++, b++)
	{
		int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
		int y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;

		if (x != y)
			return x - y;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

static int browser_wide_compare(wchar_t const *a, wchar_t const *b)
{
	for (; *a && *b; a++, b++)
	{
		int x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
		int y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;

		if (x != y)
			return x - y;
	}
	return (int)*a - (int)*b;
}

/* whether game a goes before game b, as the list is sorted */
static boolean browser_sort_before(struct browser_sort_entry const *a, struct browser_sort_entry const *b)
{
	int difference = 0;

	/* (the order the lists open in: the OpenH1 games first, the better
	game and the fewer, then the dedicated servers first in each) */
	if (browser_sort_column == _browser_sort_players && browser_sort_descending && a->openh1 != b->openh1)
		return a->openh1;
	if (browser_sort_column == _browser_sort_players && browser_sort_descending && a->dedicated != b->dedicated)
		return a->dedicated;
	switch (browser_sort_column)
	{
	case _browser_sort_players:
		difference = a->players - b->players;
		break;
	case _browser_sort_ping:
		/* (a ping not known after the others, whichever way) */
		if ((a->ping < 0) != (b->ping < 0))
			return b->ping < 0;
		difference = a->ping - b->ping;
		break;
	case _browser_sort_name:
		difference = browser_text_compare(a->name, b->name);
		break;
	case _browser_sort_map:
		difference = browser_text_compare(a->map, b->map);
		break;
	case _browser_sort_mode:
		difference = browser_wide_compare(a->mode, b->mode);
		break;
	default:
		return FALSE;
	}
	return browser_sort_descending ? difference > 0 : difference < 0;
}

/* whether the tab shown lists the game (a favorite or not) */
static boolean browser_tab_shows(struct browser_sort_entry const *entry, boolean favorite)
{
	switch (browser_tab)
	{
	case _browser_tab_dedicated:
		return entry->dedicated;
	case _browser_tab_openh1:
		return entry->openh1;
	case _browser_tab_legacy:
		return !entry->openh1;
	case _browser_tab_coop:
		return entry->coop;
	case _browser_tab_favorites:
		return favorite;
	default:
		return TRUE;
	}
}

/* whether the tab shown lists the favorites not found (offline), after the
games: ALL's and FAVORITES' */
static boolean browser_tab_lists_offline(void)
{
	return browser_tab == _browser_tab_all || browser_tab == _browser_tab_favorites;
}

/* ---- the browsers' filters (the Filters page, tools/port_settings.py's
BROWSER_FILTERS: the network.browser_* settings, read as a list opens and
kept): full games, empty ones and those with a password listed or not, only
the games whose ping is under a limit (one whose ping is not known is
listed: a local network game's, a master server's dedicated server's, one
being timed), the gametypes listed (a co-op game has none: CO-OP is its
tab), and words a game's name or map must have. The server browser's and
the local network's lists are filtered, not Join by Link's, whose games are
the invites taken. Nothing is sent: the lists' own */

#define BROWSER_SEARCH_LENGTH 31
#define BROWSER_ALL_ENGINES 0x3E

struct browser_filters
{
	boolean show_full, show_empty, show_password;
	/* milliseconds; 0: any */
	short maximum_ping;
	/* a bit for each game engine listed (1 << 1 CTF to 1 << 5 Race) */
	short engines;
	char search[BROWSER_SEARCH_LENGTH + 1];
};

static struct browser_filters browser_filters = { TRUE, TRUE, TRUE, 0, BROWSER_ALL_ENGINES, "" };
/* whether the list being ordered is filtered, and how many of the games its
tab lists the filters hid */
static boolean browser_filtering;
static short browser_filtered_count;

/* whether text has the words, in any case (ASCII's) */
static boolean browser_text_has(wchar_t const *text, char const *words)
{
	short start, index;

	for (start = 0; text[start]; start++)
	{
		for (index = 0; words[index] && text[start + index]; index++)
		{
			int x = text[start + index] >= 'a' && text[start + index] <= 'z' ? text[start + index] - 32 :
				text[start + index];
			int y = words[index] >= 'a' && words[index] <= 'z' ? words[index] - 32 : words[index];

			if (x != y)
				break;
		}
		if (!words[index])
			return TRUE;
	}
	return !words[0];
}

/* whether the filters list the game */
static boolean browser_filters_show(struct browser_filters const *filters, struct browser_sort_entry const *entry)
{
	wchar_t text[ROW_TEXT_LENGTH];
	short index;

	if (!filters->show_full && entry->maximum_players > 0 && entry->players >= entry->maximum_players)
		return FALSE;
	if (!filters->show_empty && entry->players <= 0)
		return FALSE;
	if (!filters->show_password && entry->locked)
		return FALSE;
	/* (a ping not known, NONE, under any limit) */
	if (filters->maximum_ping > 0 && entry->ping >= filters->maximum_ping)
		return FALSE;
	if (entry->engine_type >= 1 && entry->engine_type <= 5 && !(filters->engines & (1 << entry->engine_type)))
		return FALSE;
	if (!filters->search[0])
		return TRUE;
	/* (its name, its map as shown (Blood Gulch), and its map's own name
	(bloodgulch)) */
	for (index = 0; entry->name[index] && index < ROW_TEXT_LENGTH - 1; index++)
		text[index] = (wchar_t)(unsigned char)entry->name[index];
	text[index] = 0;
	if (browser_text_has(text, filters->search) || browser_text_has(entry->map_shown, filters->search))
		return TRUE;
	for (index = 0; entry->map[index] && index < ROW_TEXT_LENGTH - 1; index++)
		text[index] = (wchar_t)(unsigned char)entry->map[index];
	text[index] = 0;
	return browser_text_has(text, filters->search);
}

/* the filters as the settings have them (the words without the spaces
around them) */
static void browser_filters_read(struct browser_filters *filters)
{
	static char const *const engines[] =
	{
		NULL, "network.browser_show_ctf", "network.browser_show_slayer", "network.browser_show_oddball",
		"network.browser_show_king", "network.browser_show_race",
	};
	char text[BROWSER_SEARCH_LENGTH + 1];
	char const *search = config_string("network.browser_search");
	short index, length;

	filters->show_full = config_boolean("network.browser_show_full") != 0;
	filters->show_empty = config_boolean("network.browser_show_empty") != 0;
	filters->show_password = config_boolean("network.browser_show_password") != 0;
	filters->maximum_ping = (short)PIN(config_integer("network.browser_ping"), 0, 9999);
	filters->engines = 0;
	for (index = 1; index < (short)NUMBEROF(engines); index++)
	{
		if (config_boolean(engines[index]))
			filters->engines |= (short)(1 << index);
	}
	for (search = search ? search : ""; *search == ' '; search++)
	{
	}
	snprintf(text, sizeof(text), "%s", search);
	for (length = (short)strlen(text); length > 0 && text[length - 1] == ' '; length--)
		text[length - 1] = 0;
	snprintf(filters->search, sizeof(filters->search), "%s", text);
}

static void browser_filters_refresh(void)
{
	browser_filters_read(&browser_filters);
}

/* the order of the games found: the favorites first, then the others, those
the tab shows, each part sorted by the column chosen; into `order`,
returning how many */
static short browser_favorites_order(short const *favorite_of, struct browser_sort_entry const *entries,
	short count, short *order)
{
	short pass, index;
	short written = 0;

	browser_found_favorites = 0;
	browser_filtered_count = 0;
	for (pass = 0; pass < 2; pass++)
	{
		short start = written;
		short sorted;

		for (index = 0; index < count; index++)
		{
			if ((favorite_of[index] != NONE) != (pass == 0) || !browser_tab_shows(&entries[index], pass == 0))
				continue;
			/* (those the filters hide counted, for the words over the list) */
			if (browser_filtering && !browser_filters_show(&browser_filters, &entries[index]))
				browser_filtered_count++;
			else
				order[written++] = index;
		}
		/* (each part sorted, alike ones kept in the order found) */
		for (sorted = (short)(start + 1); sorted < written; sorted++)
		{
			short moving = order[sorted];
			short place = sorted;

			while (place > start && browser_sort_before(&entries[moving], &entries[order[place - 1]]))
			{
				order[place] = order[place - 1];
				place--;
			}
			order[place] = moving;
		}
		if (pass == 0)
			browser_found_favorites = written;
	}
	if (!browser_tab_lists_offline())
		browser_offline_count = 0;
	return written;
}

/* the list from its top again, its first game chosen (below: the server
browser's list) */
static void browser_list_restart(void);

/* the column titles' arrows: the sorted column's, its way, after its words
(past the end of a title whose words end at its right, Players' and Ping's);
its words brighter (pc_menu_text_color) */
static void browser_sort_headers_show(struct widget_instance *list)
{
	short sort;

	for (sort = 0; sort < NUMBER_OF_BROWSER_SORTS; sort++)
	{
		struct widget_instance *header = named(list, browser_sorts[sort].header, 0);
		struct widget_instance *arrows = header ? named(header, "header_sort_arrows", 0) : NULL;
		short width, words;
		long font = ui_widget_port_text_box_font(header, &width);

		if (!arrows)
			continue;
		visible_set(arrows, sort == browser_sort_column);
		visible_set(named(arrows, "header_sort_up_arrow", 0), !browser_sort_descending);
		visible_set(named(arrows, "header_sort_down_arrow", 0), browser_sort_descending);
		if (font != NONE)
		{
			words = settings_text_width(font, browser_sorts[sort].title);
			arrows->horizontal_offset = (short)(sort == _browser_sort_players || sort == _browser_sort_ping ?
				width - BROWSER_ARROW_GAP : words);
		}
	}
}

/* a column sorted by (its title chosen: the other way if it sorts already;
X: the next column, after the last the first) */
static boolean browser_sort_choose(short sort, boolean next)
{
	if (next)
	{
		sort = (short)((browser_sort_column + 1) % NUMBER_OF_BROWSER_SORTS);
		browser_sort_descending = browser_sorts[sort].descending;
	}
	else if (sort == browser_sort_column)
		browser_sort_descending = !browser_sort_descending;
	else
		browser_sort_descending = browser_sorts[sort].descending;
	browser_sort_column = sort;
	browser_list_restart();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* whether the widget is the column title the browsers are sorted by (its
words brighter: pc_menu_text_color) */
static boolean browser_sort_header_current(struct widget_instance const *widget)
{
	return widget->name && !strncmp(widget->name, "op_sort_header_", 15) &&
		!strcmp(widget->name, browser_sorts[browser_sort_column].header);
}

/* "gamespy select header": a column's title */
static boolean browser_sort_header(struct widget_instance *widget)
{
	short sort;

	for (sort = 0; sort < NUMBER_OF_BROWSER_SORTS; sort++)
	{
		if (!strcmp(widget->name, browser_sorts[sort].header))
			return browser_sort_choose(sort, FALSE);
	}
	return campaign_fail();
}

/* "port browser tab" (A or a click on one), "port browser tab next" and
"previous" (RB and LB, E and Q): the tab shown, the list from its top
(Join by Link's list has none) */
static boolean browser_tab_choose(short tab)
{
	if (multiplayer.mode == _multiplayer_mode_direct_link)
		return TRUE;
	tab = (short)((tab + NUMBER_OF_BROWSER_TABS) % NUMBER_OF_BROWSER_TABS);
	if (tab != browser_tab)
	{
		browser_tab = tab;
		browser_list_restart();
		ui_play_audio_feedback_sound(SOUND_CURSOR);
	}
	return TRUE;
}

/* the words in the list's place while it has no rows: what it shows, why
there are none and what to do (the mode's, multiplayer.mode; the tab's;
the server browser's: internet play off, or its list, still being looked
for, newer games it cannot join; the games its filters hid), into text */
static void browser_empty_text(short mode, short tab, boolean online, boolean public_lobby, boolean looking,
	int newer, short filtered, wchar_t *text, short size)
{
	static wchar_t const *const kinds[NUMBER_OF_BROWSER_TABS] =
	{
		L"", L"dedicated server ", L"OpenH1 ", L"Legacy ", L"co-op ", L"",
	};
	boolean server_browser = mode == _multiplayer_mode_server_browser;

	if (mode == _multiplayer_mode_direct_link)
	{
		usnprintf(text, size - 1, L"Copy an invite link, then choose PASTE LINK below,\r\n"
			L"or accept a Discord invite.");
	}
	else if (server_browser && !online)
		usnprintf(text, size - 1, L"Internet play is off.\r\nTurn it on in Settings, Online.");
	else if (server_browser && !public_lobby)
		usnprintf(text, size - 1, L"The server browser is off\r\n(network.public_lobby).");
	else if (server_browser && looking)
		usnprintf(text, size - 1, L"Looking for public games...");
	else if (filtered > 0)
	{
		usnprintf(text, size - 1, L"%d %s hidden by your filters.\r\nChoose FILTERS to change them.", filtered,
			filtered == 1 ? L"game is" : L"games are");
	}
	else if (tab == _browser_tab_favorites)
	{
		usnprintf(text, size - 1, server_browser ? L"None of your favorites is online.\r\n%s" :
			L"None of your favorites is open.\r\n%s", L"Choose FAVORITE on a game to add it.");
	}
	else if (tab != _browser_tab_all)
	{
		usnprintf(text, size - 1, server_browser ? L"No %sgames right now." : L"No %sgames on your network.",
			kinds[PIN(tab, 0, NUMBER_OF_BROWSER_TABS - 1)]);
	}
	else if (!server_browser)
		usnprintf(text, size - 1, L"No games on your network yet.\r\nHost one from Create Game.");
	else if (newer)
	{
		/* (an old build sees none of the games: it says why) */
		usnprintf(text, size - 1, L"%d %s on a newer version.\r\nUpdate OpenH1 to join %s.", newer,
			newer == 1 ? L"game is" : L"games are", newer == 1 ? L"it" : L"them");
	}
	else
		usnprintf(text, size - 1, L"No public games found.");
	text[size - 1] = 0;
}

/* ---- the browsers' rows and details (port_settings.py's _browser): each
row a game's star, its name with its tick rate's badge (an OpenH1 game's)
and its lock (a password) after it, its map, its mode and its players, each
cut short with "..." where it is too long (menu_text_fit), and its ping's
bars; the panel beside them the game chosen's whole name, map and mode, its
kind (OPENH1 or LEGACY), tick rate and whether OpenCE's players join it,
its players and ping, a line of how it is, and JOIN GAME */

enum
{
	/* a row's ping: measured, being measured, never (a game with a
	password: its host is not probed until it is unlocked), its host not
	reached, none (a local network game's, found on this network) */
	_browser_ping_measured,
	_browser_ping_measuring,
	_browser_ping_locked,
	_browser_ping_failed,
	_browser_ping_local,
};

static void text_to_wide(char const *text, wchar_t *wide, short size);

/* what a game's row and panel say of its room for this machine's party
(browser_party_room): nothing (it leads no party of more than itself); room
for the whole party, which its host holds as the party follows its leader
in (an OpenH1 game of a build that holds room: port/linux/game/network_party.c);
room, but no guarantee (a Legacy game's host, or an older OpenH1 one's,
holds none: the members join as they can); no room for the whole party */
enum
{
	_browser_party_none,
	_browser_party_held,
	_browser_party_unheld,
	_browser_party_no_room,
};

/* this machine's party as the server browser counts it: its members of
this version (itself too) if it leads one, else 0; read once a frame */
static short browser_party_size;

/* a ping's column while it is measured, and while it is not known */
#define BROWSER_PING_MEASURING L"\x2026"
#define BROWSER_PING_UNKNOWN L"\x2013"

/* what a row and the panel show of a game */
struct browser_row
{
	wchar_t name[ROW_TEXT_LENGTH];
	wchar_t map[ROW_TEXT_LENGTH];
	/* its gametype's kind (SLAYER, CTF, CO-OP), and its own name if it has
	one ("snipers") */
	wchar_t mode[ROW_TEXT_LENGTH];
	wchar_t gametype[ROW_TEXT_LENGTH];
	short players, maximum_players;
	/* the bots its host plays (port/linux/game/bots.c), which players and
	maximum_players leave out */
	short bots;
	/* milliseconds (_browser_ping_measured), and else why not */
	short ping, ping_state;
	short rate;
	boolean openh1, mixed, locked, open, in_progress, offline;
	/* a dedicated server's (halo --server: no player of its own), and who
	runs it and where, as a master server lists it (empty: not said) */
	boolean dedicated;
	wchar_t publisher[P2P_LISTING_PUBLISHER_SIZE + 1];
	wchar_t region[P2P_LISTING_REGION_SIZE + 1];
	/* a local network game's machines */
	short machines;
	/* its room for this machine's party (_browser_party_*), and the
	party's size */
	short party_room, party_size;
	/* the matchmaking playlist it was hosted for (1 up; 0: none) */
	short playlist;
};

/* a game's room for a party of size (itself too; 1 or less: none) whose
leader joins it, with players of maximum in it (those its host holds slots
for counted), its host holding room (holds) or not (_browser_party_*) */
static short browser_party_room(short size, short players, short maximum, boolean holds)
{
	if (size <= 1)
		return _browser_party_none;
	if (maximum > 0 && players + size > maximum)
		return _browser_party_no_room;
	return holds ? _browser_party_held : _browser_party_unheld;
}

/* this machine's party's size, if it leads one (browser_party_size) */
static short browser_party_members(void)
{
	struct p2p_party_status status;
	short count = 0;
	short index;

	p2p_party_status(&status);
	if (status.state != _p2p_party_member || !status.leader)
		return 0;
	for (index = 0; index < status.count; index++)
	{
		if (!status.members[index].other_version)
			count++;
	}
	return count;
}

/* a public game's (the server browser's) */
static void browser_listing_row(struct p2p_listing const *game, struct browser_row *row)
{
	csmemset(row, 0, sizeof(*row));
	text_to_wide(game->name, row->name, ROW_TEXT_LENGTH);
	map_display_name(game->map, row->map);
	settings_wide_copy(row->mode, game_kind_name(game->engine_type, game->kind, FALSE), ROW_TEXT_LENGTH);
	text_to_wide(game->gametype, row->gametype, ROW_TEXT_LENGTH);
	row->players = game->player_count;
	row->maximum_players = game->maximum_player_count;
	row->bots = game->bots;
	row->ping = game->ping;
	/* (a master server's game is not probed: the players' addresses are
	not told to every server listed, p2p_lobby.c) */
	row->ping_state = game->failed ? _browser_ping_failed : game->ping >= 0 ? _browser_ping_measured :
		game->locked || game->master ? _browser_ping_locked : _browser_ping_measuring;
	row->dedicated = game->dedicated != 0;
	text_to_wide(game->publisher, row->publisher, NUMBEROF(row->publisher));
	text_to_wide(game->region, row->region, NUMBEROF(row->region));
	row->rate = game->rate;
	row->openh1 = game->standard != 0;
	row->mixed = game->mixed != 0;
	row->locked = game->locked != 0;
	row->open = game->open != 0;
	row->in_progress = game->in_progress != 0;
	row->party_size = browser_party_size;
	row->playlist = game->playlist;
	row->party_room = browser_party_room(browser_party_size, game->player_count, game->maximum_player_count,
		game->holds_party != 0);
}

/* a local network game's, or Join by Link's (its host's ping, the
tunnel's) */
static void browser_advertised_row(struct advertised_game *game, struct browser_row *row)
{
	int ping = multiplayer.mode == _multiplayer_mode_direct_link ? p2p_peer_ping(game->xnaddr + 2) : -1;

	csmemset(row, 0, sizeof(*row));
	ustrncpy(row->name, game->game_name, MIN(NUMBEROF(game->game_name), ROW_TEXT_LENGTH) - 1);
	game_map_name(game, row->map);
	settings_wide_copy(row->mode, game_kind_name(game->engine_type, advertised_game_kind(game), FALSE),
		ROW_TEXT_LENGTH);
	row->players = game->player_count;
	row->maximum_players = game->maximum_player_count;
	row->bots = (short)network_game_client_advertised_game_bots(global_network_game_client_get(), game);
	row->ping = (short)ping;
	row->ping_state = ping >= 0 ? _browser_ping_measured : multiplayer.mode == _multiplayer_mode_direct_link ?
		_browser_ping_measuring : _browser_ping_local;
	row->rate = (short)advertised_game_rate(game);
	row->openh1 = advertised_game_standard(game);
	row->mixed = advertised_game_mixed(game);
	row->open = game->open;
	row->in_progress = advertised_in_progress(game);
	row->dedicated = advertised_dedicated(game);
	row->machines = game->machine_count;
}

/* an offline favorite's: its last known name, map and gametype */
static void browser_offline_row(struct browser_favorite const *favorite, struct browser_row *row)
{
	csmemset(row, 0, sizeof(*row));
	text_to_wide(favorite->name, row->name, ROW_TEXT_LENGTH);
	map_display_name(favorite->map, row->map);
	settings_wide_copy(row->mode, game_kind_name(favorite->engine_type, (byte)favorite->kind, FALSE),
		ROW_TEXT_LENGTH);
	text_to_wide(favorite->gametype, row->gametype, ROW_TEXT_LENGTH);
	row->ping = -1;
	row->ping_state = _browser_ping_failed;
	row->offline = TRUE;
}

/* the panel's line for an offline favorite: when it was last seen */
static void browser_offline_line(struct browser_favorite const *favorite, wchar_t *text, short size)
{
	char when[32];
	wchar_t wide[32];
	short index;

	browser_favorite_last_seen_text(favorite->last_seen, browser_favorites_now(), when, sizeof(when));
	for (index = 0; when[index] && index < NUMBEROF(wide) - 1; index++)
		wide[index] = (wchar_t)(unsigned char)when[index];
	wide[index] = 0;
	usnprintf(text, size - 1, L"Offline: last seen %s.", wide);
	text[size - 1] = 0;
}

/* a row's star (server_item_favorite): gold for a favorite, grey for one
offline, an outline on the row focused that is neither (a click on it stars
the game), else nothing */
static void browser_row_star(struct widget_instance *row, short row_index, boolean focused)
{
	struct widget_instance *star = named(row, "server_item_favorite", 0);

	if (!star)
		return;
	star->visible = TRUE;
	star->animation.current_frame_index = browser_row_offline[row_index] ? 2 :
		browser_row_favorite[row_index] ? 1 : focused ? 3 : 0;
}

/* a game's ping as its bars' frame (port_settings.py's PING_BARS: none
lit, then poor, fair and good) */
static short browser_ping_bars(short ping)
{
	return (short)(ping < 0 ? 0 : ping < PING_GOOD ? 3 : ping < PING_FAIR ? 2 : 1);
}

/* a row's ping as its column shows it: its milliseconds, a faded "..."
while it is measured, a dash while it is not known (a game with a password,
its host not reached, a local network game's) */
static void browser_ping_cell(short ping_state, short ping, wchar_t *text, short size)
{
	if (ping_state == _browser_ping_measured)
	{
		usnprintf(text, size - 1, L"%d", ping);
		text[size - 1] = 0;
	}
	else if (ping_state == _browser_ping_measuring)
		settings_wide_copy(text, BROWSER_PING_MEASURING, size);
	else
		settings_wide_copy(text, BROWSER_PING_UNKNOWN, size);
}

/* the gap between a row's name and its badge and lock, and between the
ping's bars and its number */
#define BROWSER_NAME_GAP 4
#define BROWSER_LOCK_WIDTH 14
/* (port_settings.py's BROWSER_PING_BARS) */
#define BROWSER_PING_BARS_WIDTH 24
#define BROWSER_PING_GAP 2

/* a game's row (row_index: 0 to BROWSER_ROWS - 1) */
static void browser_row_show(struct widget_instance *row_widget, short row_index, struct browser_row const *row,
	boolean favorite)
{
	struct widget_instance *name = named(row_widget, "server_item_server_name", 0);
	struct widget_instance *tick = named(row_widget, "server_item_tick", 0);
	struct widget_instance *lock = named(row_widget, "server_item_locked", 0);
	struct widget_instance *ping = named(row_widget, "server_item_ping", 0);
	struct widget_instance *ping_ms = named(row_widget, "server_item_ping_ms", 0);
	short badge_width = 0, room = 0, drawn, x;
	wchar_t text[ROW_TEXT_LENGTH];

	browser_row_favorite[row_index] = favorite || row->offline;
	browser_row_offline[row_index] = row->offline;
	/* (and one without room for this machine's whole party) */
	browser_row_closed[row_index] = (!row->open && !row->offline) || row->party_room == _browser_party_no_room;
	browser_row_star(row_widget, row_index, row_widget == row_widget->parent->focused_child);
	/* (the name, room left for its badge and lock after it) */
	if (tick && row->openh1 && !row->offline)
	{
		ui_widget_port_text_box_font(tick, &badge_width);
		room = (short)(badge_width + BROWSER_NAME_GAP);
	}
	if (row->locked)
		room = (short)(room + BROWSER_LOCK_WIDTH + BROWSER_NAME_GAP);
	ui_widget_port_text_box_font(name, &drawn);
	drawn = menu_text_box_fit(name, row->name, (short)(drawn - room));
	x = (short)((name ? name->horizontal_offset : 0) + drawn + BROWSER_NAME_GAP);
	visible_set(tick, room && badge_width);
	if (tick && badge_width && row->openh1 && !row->offline)
	{
		usnprintf(text, NUMBEROF(text) - 1, L"%d", row->rate);
		text[NUMBEROF(text) - 1] = 0;
		text_set(tick, text);
		tick->horizontal_offset = x;
		tick->animation.current_frame_index = 0;
		x = (short)(x + badge_width + BROWSER_NAME_GAP);
	}
	/* (the lock's bitmap: the PC version's, whose first frame is empty and
	next the lock in the menus' blue) */
	visible_set(lock, row->locked);
	if (lock)
	{
		lock->horizontal_offset = (short)(x - 2);
		lock->animation.current_frame_index = 1;
	}
	menu_text_box_fit(named(row_widget, "server_item_map", 0), row->map, 0);
	menu_text_box_fit(named(row_widget, "server_item_type", 0), row->mode, 0);
	if (row->offline)
		usnprintf(text, NUMBEROF(text) - 1, L"OFFLINE");
	else
		usnprintf(text, NUMBEROF(text) - 1, L"%d/%d", row->players, row->maximum_players);
	text[NUMBEROF(text) - 1] = 0;
	menu_text_box_fit(named(row_widget, "server_item_players", 0), text, 0);
	/* (its ping: its number at the column's right, the bars before it if
	both fit; none for an offline favorite) */
	browser_row_pings[row_index] = row->ping_state == _browser_ping_measured ? row->ping : -1;
	browser_row_measuring[row_index] = row->ping_state == _browser_ping_measuring;
	browser_ping_cell(row->ping_state, row->ping, text, NUMBEROF(text));
	drawn = menu_text_box_fit(ping_ms, row->offline ? L"" : text, 0);
	ui_widget_port_text_box_font(ping_ms, &x);
	visible_set(ping, !row->offline && drawn + BROWSER_PING_GAP + BROWSER_PING_BARS_WIDTH <= x);
	if (ping)
		ping->animation.current_frame_index = browser_ping_bars(browser_row_pings[row_index]);
}

/* the panel's ping: its milliseconds, or why there are none */
static void browser_ping_text(struct browser_row const *row, wchar_t *text, short size)
{
	switch (row->ping_state)
	{
	case _browser_ping_measured:
		usnprintf(text, size - 1, L"%d ms", row->ping);
		break;
	case _browser_ping_measuring:
		usnprintf(text, size - 1, L"Measuring...");
		break;
	case _browser_ping_locked:
		usnprintf(text, size - 1, L"Not measured");
		break;
	case _browser_ping_local:
		usnprintf(text, size - 1, L"This network");
		break;
	default:
		usnprintf(text, size - 1, L"Not reached");
		break;
	}
	text[size - 1] = 0;
}

/* the panel (the list's description): the game chosen's details and its
status line (row NULL: none chosen, the panel `title` and `status` alone),
JOIN GAME shown with a game to join */
static void browser_panel_show(struct widget_instance *list, struct browser_row const *row, wchar_t const *title,
	wchar_t const *status)
{
	struct widget_instance *panel = list->parameters.list.extended_description;
	char const *const details[] = { "panel_where", "panel_kind", "panel_tick", "panel_players_label",
		"panel_players", "panel_ping_label", "panel_ping" };
	struct widget_instance *pill;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	short index;

	visible_set(named(list, "browser_join_bar", 0), row && !row->offline);
	if (!panel)
		return;
	for (index = 0; index < NUMBEROF(details); index++)
		visible_set(named(panel, details[index], 0), row != NULL);
	visible_set(named(panel, "panel_opence", 0), row && !row->offline && (!row->openh1 || row->mixed));
	menu_text_box_wrap(named(panel, "panel_title", 0), row ? row->name : title, 2);
	/* (the touchscreen's panel is narrower: a line more for the status) */
	menu_text_box_wrap(named(panel, "panel_status", 0), status ? status : L"", ui_widget_port_touch_menus() ? 4 : 3);
	if (!row)
		return;
	/* (its map and mode, and its gametype's own name if it has another) */
	if (row->gametype[0] && browser_wide_compare(row->gametype, row->mode))
		usnprintf(text, NUMBEROF(text) - 1, L"%s \x00B7 %s (%s)", row->map, row->mode, row->gametype);
	else
		usnprintf(text, NUMBEROF(text) - 1, L"%s \x00B7 %s", row->map, row->mode);
	text[NUMBEROF(text) - 1] = 0;
	menu_text_box_wrap(named(panel, "panel_where", 0), text, 2);
	if ((pill = named(panel, "panel_kind", 0)) != NULL)
	{
		pill->animation.current_frame_index = row->openh1 ? 0 : 1;
		text_set(pill, row->openh1 ? L"OPENH1" : L"LEGACY");
	}
	if ((pill = named(panel, "panel_tick", 0)) != NULL)
	{
		pill->animation.current_frame_index = 2;
		usnprintf(text, NUMBEROF(text) - 1, L"%d TICK", row->rate ? row->rate : 30);
		text[NUMBEROF(text) - 1] = 0;
		text_set(pill, text);
	}
	if ((pill = named(panel, "panel_opence", 0)) != NULL)
	{
		pill->animation.current_frame_index = 2;
		text_set(pill, L"OPENCE CAN JOIN");
	}
	visible_set(named(panel, "panel_kind", 0), !row->offline);
	visible_set(named(panel, "panel_tick", 0), !row->offline);
	text_set(named(panel, "panel_players_label", 0), L"PLAYERS");
	text_set(named(panel, "panel_ping_label", 0), L"PING");
	if (row->offline)
		usnprintf(text, NUMBEROF(text) - 1, L"Offline");
	/* (its players, then its bots apart) */
	else if (row->bots)
		usnprintf(text, NUMBEROF(text) - 1, L"%d/%d \x00B7 %d BOT%s", row->players, row->maximum_players, row->bots,
			row->bots == 1 ? L"" : L"S");
	else
		usnprintf(text, NUMBEROF(text) - 1, L"%d/%d", row->players, row->maximum_players);
	text[NUMBEROF(text) - 1] = 0;
	text_set(named(panel, "panel_players", 0), text);
	browser_ping_text(row, text, ROW_TEXT_LENGTH);
	text_set(named(panel, "panel_ping", 0), text);
}

/* the panel's line for a game: how it is (under way, full, a password) */
static void browser_status_text(struct browser_row const *row, wchar_t *text, short size)
{
	wchar_t machines[32];
	wchar_t server[P2P_LISTING_PUBLISHER_SIZE + P2P_LISTING_REGION_SIZE + 32];
	wchar_t party[80];
	wchar_t playlist[MATCHMAKING_PLAYLIST_NAME_SIZE * 2 + 16];

	machines[0] = 0;
	if (row->machines > 1)
		usnprintf(machines, NUMBEROF(machines) - 1, L"On %d machines.", row->machines);
	machines[NUMBEROF(machines) - 1] = 0;
	/* (its room for this machine's party, first) */
	switch (row->open ? row->party_room : _browser_party_none)
	{
	case _browser_party_held:
		usnprintf(party, NUMBEROF(party) - 1, L"Room for your party (%d). ", row->party_size);
		break;
	case _browser_party_unheld:
		usnprintf(party, NUMBEROF(party) - 1, L"Room for your party (%d), but places aren't held. ",
			row->party_size);
		break;
	case _browser_party_no_room:
		usnprintf(party, NUMBEROF(party) - 1, L"No room for your party of %d. ", row->party_size);
		break;
	default:
		party[0] = 0;
		break;
	}
	party[NUMBEROF(party) - 1] = 0;
	/* (a matchmaking game's playlist: matchmaking.c's) */
	playlist[0] = 0;
	if (row->playlist > 0)
	{
		wchar_t name[MATCHMAKING_PLAYLIST_NAME_SIZE * 2];

		matchmaking_playlist_text((short)(row->playlist - 1), name, NUMBEROF(name), NULL, 0);
		if (name[0])
			usnprintf(playlist, NUMBEROF(playlist) - 1, L"Matchmaking: %s. ", name);
		playlist[NUMBEROF(playlist) - 1] = 0;
	}
	/* (a dedicated server's, who runs it and where, first) */
	server[0] = 0;
	if (row->dedicated)
	{
		usnprintf(server, NUMBEROF(server) - 1, L"A dedicated server%s%s%s%s. ", row->publisher[0] ? L" by " : L"",
			row->publisher, row->region[0] ? L", " : L"", row->region);
		server[NUMBEROF(server) - 1] = 0;
	}
	usnprintf(text, size - 1, L"%s%s%s%s%s%s", server, playlist, party,
		!row->open ? L"Full or starting. " :
		row->in_progress ? L"Under way: its lobby shows first. " : L"",
		row->locked ? L"It has a password. " : L"", machines);
	text[size - 1] = 0;
}

/* the footer's prompts for the list's mode: REFRESH the server browser's,
PASTE LINK Join by Link's in its place; FILTERS but Join by Link's */
static void browser_prompts_show(struct widget_instance *list)
{
	struct widget_instance *bar = named(list, "join_game_button_bar", 0);

	menu_prompts_show(bar, "prompt_refresh", multiplayer.mode == _multiplayer_mode_server_browser);
	menu_prompts_show(bar, "prompt_paste", multiplayer.mode == _multiplayer_mode_direct_link);
	menu_prompts_show(bar, "prompt_filters", multiplayer.mode != _multiplayer_mode_direct_link);
}

/* what the list shows above its rows: its tabs, the sort, its games and
their players (the tab's), the words in its place while it has no rows */
static void browser_header_show(struct widget_instance *list, short games, long players, short rows,
	wchar_t const *empty)
{
	struct widget_instance *screen = list;
	wchar_t text[ROW_TEXT_LENGTH];

	static short tab_shown = NONE;
	struct widget_instance *strip = named(list, "browser_tabs", 0);

	while (screen->parent)
		screen = screen->parent;
	/* (a tab chosen by LB or RB takes the strip's focus) */
	if (strip && tab_shown != browser_tab)
	{
		struct widget_instance *tab = strip->child;
		short index;

		for (index = 0; tab && index < browser_tab; index++)
			tab = tab->next;
		if (tab)
		{
			strip->focused_child = tab;
			strip->parameters.list.selected_index = browser_tab;
		}
	}
	tab_shown = browser_tab;
	menu_tabs_show(strip, browser_tab);
	browser_sort_headers_show(list);
	/* (the touchscreen's SORT button names the sort, as it has no column
	titles to mark: tools/port_settings.py's _touch_browser) */
	if (ui_widget_port_touch_menus())
	{
		usnprintf(text, NUMBEROF(text) - 1, L"SORT: %s", browser_sorts[browser_sort_column].title);
		text[NUMBEROF(text) - 1] = 0;
		text_set(named(list, "prompt_sort", 0), text);
	}
	usnprintf(text, NUMBEROF(text) - 1, L"%d %s    %ld %s", games, games == 1 ? L"GAME" : L"GAMES", players,
		players == 1 ? L"PLAYER" : L"PLAYERS");
	/* (and how many the filters hide) */
	if (browser_filtering && browser_filtered_count > 0)
	{
		short length = (short)ustrlen(text);

		usnprintf(text + length, NUMBEROF(text) - 1 - length, L"    %d HIDDEN", browser_filtered_count);
	}
	text[NUMBEROF(text) - 1] = 0;
	text_set(named(screen, "browser_counts", 0), text);
	visible_set(named(screen, "browser_empty", 0), rows == 0);
	if (!rows)
		menu_text_box_wrap(named(screen, "browser_empty", 0), empty, 3);
}
/* ---- Join Game > Server Browser: internet play's public games (their
hosts' listings, p2p_lobby.c). Joining one joins its invite (as Direct
Link's PASTE LINK); once its host is reached its game is among the client's,
and an A press (posted) joins it, or shows its lobby if under way */

#define LOBBY_BROWSER_GAMES 256
/* milliseconds: a host not reached in this long is given up (p2p's own
wait is longer) */
#define LOBBY_BROWSER_JOIN_TIMEOUT 30000
/* how long the browser looks before it says it found none, and shows a
join that failed */
#define LOBBY_BROWSER_LOOK_TIME 6000
#define LOBBY_BROWSER_FAILED_TIME 8000

static struct
{
	struct p2p_listing games[LOBBY_BROWSER_GAMES];
	/* the games found, the first on the rows, the one last focused (Join's) */
	short count, first, chosen;
	/* whether it has shown a game (the first has the focus) */
	boolean shown;
	unsigned long begin_time;
	/* a game being joined: its host, its name, since when, the controller
	that asked; ready: its game reached (the A press posted) */
	boolean joining, ready;
	unsigned char identifier[6];
	char name[P2P_LISTING_NAME_SIZE + 1];
	unsigned long join_time;
	short controller;
	/* when the press was posted (again if it was lost) */
	unsigned long ready_time;
	/* the last that failed, and when */
	char failed_name[P2P_LISTING_NAME_SIZE + 1];
	unsigned long failed_time;
	boolean failed_nat_strict;
	/* why, in a sentence (connect_failure_text's line: CONNECT_LINE_LENGTH) */
	wchar_t failed_line[64];
	/* a game with a password chosen: the password screen's (its own copy:
	the rows' are read again each frame), and a join it began, which the
	browser takes up again as it comes back */
	struct p2p_listing password_game;
	boolean password_joined;
} lobby_browser;

static void browser_list_restart(void)
{
	lobby_browser.first = lobby_browser.chosen = 0;
	multiplayer.game_chosen = multiplayer.game_first = 0;
}

/* ---- internet play's connecting box (tools/port_settings.py's
_connect_screen): opened over the browser as a game is joined (a server
browser's row, Direct Link's PASTE LINK), it names the step the join is at,
from p2p_join_status: finding the host (this machine's public address, then
the brokers' answer), opening a path to it (the host answered: both send to
each other's addresses, a round of them each try), joining (reached: the
browser joins its game). Given up after CONNECT_TIMEOUT, it says why, from
what internet play knows, and what would help; RETRY asks again, BACK (B)
gives the join up */

#define CONNECT_NAME "pc\\main_menu\\multiplayer_type_select\\join_game\\connect\\connect_screen"
/* milliseconds: a host not reached in this long is given up; this long
while relays are being tried for it (p2p_relay.c: they start a few
seconds in) */
#define CONNECT_TIMEOUT 10000
#define CONNECT_RELAY_TIMEOUT 20000
#define CONNECT_TIMED_OUT(status, elapsed) \
	((elapsed) > ((status)->relaying ? CONNECT_RELAY_TIMEOUT : CONNECT_TIMEOUT))
#define CONNECT_HEADLINE_LENGTH 32
#define CONNECT_DETAIL_LENGTH 160
#define CONNECT_LINE_LENGTH 64
/* a phone's or a tablet's: what it is told to try when a network blocks a
join (connect_failure_text) */
#if defined(HALO_ANDROID) || defined(HALO_IOS)
#define CONNECT_ON_PHONE TRUE
#else
#define CONNECT_ON_PHONE FALSE
#endif

static struct
{
	/* what is joined: a server browser's game, or Direct Link's invite */
	boolean listing;
	struct p2p_listing game;
	char link[TEXT_FIELD_LENGTH];
	/* the controller that asked */
	short controller;
	unsigned long start_time;
	/* given up, its reason shown; or reached, the A press posted (to
	whichever button has the focus) to go back and join */
	boolean failed, proceed;
	wchar_t headline[CONNECT_HEADLINE_LENGTH];
	wchar_t detail[CONNECT_DETAIL_LENGTH];
} connect_dialog;

/* whether the server browser's join has run past its time (longer while
relays are tried) */
static boolean lobby_browser_join_timed_out(unsigned long elapsed)
{
	struct p2p_join_status status;

	if (elapsed <= CONNECT_TIMEOUT)
		return FALSE;
	p2p_join_status(&status);
	return CONNECT_TIMED_OUT(&status, elapsed);
}

/* the step a join is at, for the box and the server browser's line */
static void connect_stage_text(struct p2p_join_status const *status, wchar_t *text, short length)
{
	if (status->stage == _p2p_join_reached)
		usnprintf(text, length - 1, status->relayed ? L"Connected through a relay.\r\nJoining..." : L"Joining...");
	else if (status->stage == _p2p_join_opening && status->relaying)
		usnprintf(text, length - 1, L"Trying a relay...\r\n(try %d)", status->tries + 1);
	else if (status->stage == _p2p_join_opening)
		usnprintf(text, length - 1, L"Opening a path to the host...\r\n(try %d)", status->tries + 1);
	else
		usnprintf(text, length - 1, L"Finding the host...");
	text[length - 1] = 0;
}

/* why a join gave up, as far as internet play can tell, and what would
help, in words for anyone (the cause in the log: connect_failure_log): the
box's headline and its lines (broken for it), and the server browser's
panel's sentence. Where the cause is known, the advice is for it: internet
play off, no internet (no signalling broker), no answer (the game ended, or
its host offline: a server browser's game, or an invite's), this network
(its NAT gives each destination its own port, and no router forwards one),
the host's, or one between them. A phone (on_phone) is told to try Wi-Fi
rather than mobile data, where such networks mostly are; no one is told of
ports, only of the setting that asks the router for one */
static void connect_failure_text(struct p2p_join_status const *status, boolean listing, boolean on_phone,
	wchar_t *headline, wchar_t *detail, wchar_t *line)
{
	if (!status->online)
	{
		usnprintf(headline, CONNECT_HEADLINE_LENGTH - 1, L"INTERNET PLAY IS OFF");
		usnprintf(detail, CONNECT_DETAIL_LENGTH - 1, L"Turn it on in Settings,\r\nOnline.");
		usnprintf(line, CONNECT_LINE_LENGTH - 1, L"Internet play is off.");
	}
	else if (!status->brokers)
	{
		usnprintf(headline, CONNECT_HEADLINE_LENGTH - 1, L"CAN'T GET ONLINE");
		usnprintf(detail, CONNECT_DETAIL_LENGTH - 1, L"Check your internet\r\nconnection, then RETRY.");
		usnprintf(line, CONNECT_LINE_LENGTH - 1, L"Check your internet connection.");
	}
	else if (status->stage != _p2p_join_opening)
	{
		usnprintf(headline, CONNECT_HEADLINE_LENGTH - 1, L"GAME NOT FOUND");
		usnprintf(detail, CONNECT_DETAIL_LENGTH - 1, listing ?
			L"The game may have ended,\r\nor its host is offline.\r\nTry another game." :
			L"The game may have ended,\r\nor its host is offline.\r\nAsk the host for a new\r\ninvite link.");
		usnprintf(line, CONNECT_LINE_LENGTH - 1, L"The game may have ended.");
	}
	else if (status->nat_strict)
	{
		usnprintf(headline, CONNECT_HEADLINE_LENGTH - 1, L"NETWORK BLOCKED");
		usnprintf(detail, CONNECT_DETAIL_LENGTH - 1, !config_boolean("network.allow_upnp") ?
			L"Your network is blocking\r\nthe connection. Turn on\r\nOPEN ROUTER PORT in\r\nSettings, Online." : on_phone ?
			L"Your network is blocking\r\nthe connection. Try\r\nanother network (Wi-Fi\r\ninstead of mobile data)." :
			L"Your network is blocking\r\nthe connection. Try\r\nanother network, or ask\r\nsomeone else to host.");
		usnprintf(line, CONNECT_LINE_LENGTH - 1, L"Your network is blocking it.");
	}
	else if (status->host_strict)
	{
		usnprintf(headline, CONNECT_HEADLINE_LENGTH - 1, L"CAN'T REACH THE HOST");
		usnprintf(detail, CONNECT_DETAIL_LENGTH - 1,
			L"The host's network is\r\nblocking the connection.\r\nAsk the host to turn on\r\nOPEN ROUTER PORT.");
		usnprintf(line, CONNECT_LINE_LENGTH - 1, L"The host's network is blocking it.");
	}
	else
	{
		usnprintf(headline, CONNECT_HEADLINE_LENGTH - 1, L"CAN'T REACH THE HOST");
		usnprintf(detail, CONNECT_DETAIL_LENGTH - 1, on_phone ?
			L"A network is blocking the\r\nconnection. Try Wi-Fi\r\ninstead of mobile data,\r\nthen RETRY." :
			L"A network is blocking the\r\nconnection. RETRY, or ask\r\nthe host to turn on\r\nOPEN ROUTER PORT.");
		usnprintf(line, CONNECT_LINE_LENGTH - 1, L"A network is blocking it.");
	}
	headline[CONNECT_HEADLINE_LENGTH - 1] = detail[CONNECT_DETAIL_LENGTH - 1] = line[CONNECT_LINE_LENGTH - 1] = 0;
}

/* the log's line of a join given up */
static void connect_failure_log(char const *name, struct p2p_join_status const *status)
{
	platform_log("menus: could not reach %s after %lu ms (%s; %s, %d tries; brokers %s; this network %s, the "
		"host's %s)", name, status->elapsed, !status->online ? "internet play is off" :
		!status->brokers ? "no signalling broker" : status->stage == _p2p_join_opening ? "the host answered" :
		"no answer from the host", status->stage == _p2p_join_opening ? "no path" : "no session", status->tries,
		status->brokers ? "connected" : "not connected", status->nat_strict ? "strict" : "not known strict",
		status->host_strict ? "strict" : "not known strict");
}

/* the box opened over the browser (its B returns to it), if the join is
under way (a host reached already is joined as before) */
static boolean connect_open(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	struct p2p_join_status status;

	p2p_join_status(&status);
	if (status.stage != _p2p_join_finding && status.stage != _p2p_join_opening)
		return TRUE;
	connect_dialog.controller = controller;
	connect_dialog.start_time = system_milliseconds();
	connect_dialog.failed = connect_dialog.proceed = FALSE;
	return ui_widget_port_open(widget, CONNECT_NAME, widget_deleted);
}

/* "port connect update": the step, or why it gave up */
static void connect_update(struct widget_instance *screen)
{
	struct p2p_join_status status;
	unsigned long now = system_milliseconds();
	struct widget_instance *retry = named(screen, "button_retry", 0);
	wchar_t text[CONNECT_DETAIL_LENGTH];

	p2p_join_status(&status);
	if (!connect_dialog.failed && !connect_dialog.proceed)
	{
		if (status.stage == _p2p_join_reached)
		{
			connect_dialog.proceed = TRUE;
			ui_widget_port_post_button(connect_dialog.controller, BUTTON_A);
		}
		else if (status.stage == _p2p_join_idle || CONNECT_TIMED_OUT(&status, now - connect_dialog.start_time))
		{
			char name[P2P_LISTING_NAME_SIZE + 1];
			wchar_t line[CONNECT_LINE_LENGTH];

			connect_failure_text(&status, connect_dialog.listing, CONNECT_ON_PHONE, connect_dialog.headline,
				connect_dialog.detail, line);
			snprintf(name, sizeof(name), "%s", connect_dialog.listing ? connect_dialog.game.name : "the invite's host");
			connect_failure_log(name, &status);
			p2p_join_cancel();
			if (connect_dialog.listing)
				p2p_lobby_mark_failed(connect_dialog.game.identifier);
			connect_dialog.failed = TRUE;
			ui_play_audio_feedback_sound(SOUND_ERROR);
		}
	}
	if (connect_dialog.failed)
	{
		text_set(named(screen, "header_connect", 0), connect_dialog.headline);
		text_set_length(screen, connect_dialog.detail, CONNECT_DETAIL_LENGTH);
	}
	else
	{
		text_set(named(screen, "header_connect", 0), L"CONNECTING");
		connect_stage_text(&status, text, NUMBEROF(text));
		text_set_length(screen, text, CONNECT_DETAIL_LENGTH);
	}
	if (retry)
	{
		retry->visible = connect_dialog.failed;
		retry->disabled = !connect_dialog.failed;
	}
}

/* back to the browser: a game reached is joined there (as the password
screen's join is) */
static boolean connect_return(struct widget_instance *widget, boolean *widget_deleted)
{
	if (connect_dialog.proceed && connect_dialog.listing)
	{
		lobby_browser.password_joined = TRUE;
		lobby_browser.join_time = system_milliseconds();
	}
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* "port connect back" (BACK, B): the join given up, unless it reached the
host (the A press posted) */
static boolean connect_back(struct widget_instance *widget, boolean *widget_deleted)
{
	if (!connect_dialog.proceed && !connect_dialog.failed)
		p2p_join_cancel();
	return connect_return(widget, widget_deleted);
}

/* "port connect retry" (RETRY, once it gave up): asked again */
static boolean connect_retry(struct widget_instance *widget, boolean *widget_deleted)
{
	int joined;

	if (connect_dialog.proceed)
		return connect_return(widget, widget_deleted);
	if (!connect_dialog.failed)
		return TRUE;
	joined = connect_dialog.listing ? p2p_join_listing(&connect_dialog.game) : p2p_join_invite(connect_dialog.link);
	if (!joined)
		return campaign_fail();
	connect_dialog.start_time = system_milliseconds();
	connect_dialog.failed = FALSE;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* the row of the browser's chosen game, NONE if it is not on the rows */
static short browser_chosen_row(void)
{
	boolean server_browser = multiplayer.mode == _multiplayer_mode_server_browser;
	short chosen = server_browser ? lobby_browser.chosen : multiplayer.game_chosen;
	short row = (short)(chosen - (server_browser ? lobby_browser.first : multiplayer.game_first));

	/* (the favorites offline after the games found are rows too) */
	return chosen < (server_browser ? lobby_browser.count : multiplayer.game_count) + browser_offline_count &&
		row >= 0 && row < browser_shown_rows ? row : NONE;
}

static void lobby_browser_begin(void)
{
	lobby_browser.count = 0;
	lobby_browser.first = 0;
	lobby_browser.chosen = 0;
	lobby_browser.shown = FALSE;
	/* (going on with the join the password screen began) */
	lobby_browser.joining = lobby_browser.password_joined;
	lobby_browser.ready = FALSE;
	lobby_browser.password_joined = FALSE;
	lobby_browser.begin_time = system_milliseconds();
	p2p_lobby_browse(TRUE);
}

/* "gamespy screen dispose" */
static void lobby_browser_end(void)
{
	lobby_browser.joining = lobby_browser.ready = FALSE;
	p2p_lobby_browse(FALSE);
}

static void text_to_wide(char const *text, wchar_t *wide, short size)
{
	short index;

	for (index = 0; text[index] && index < size - 1; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
}

/* the games whose names are valid, as a host keeps the names of the
machines and players that join it (network_game_server_clean_name): each
name cleaned (player_name_clean), and a game whose name has nothing left
that names it left out; returns how many are left */
static short lobby_browser_valid_games(struct p2p_listing *games, short count)
{
	short read;
	short written = 0;

	for (read = 0; read < count; read++)
	{
		wchar_t name[P2P_LISTING_NAME_SIZE + 1];
		short index;

		text_to_wide(games[read].name, name, NUMBEROF(name));
		if (!player_name_clean(name, NUMBEROF(name)))
			continue;
		/* (ASCII still: the listing's names are) */
		for (index = 0; name[index]; index++)
			games[read].name[index] = (char)name[index];
		games[read].name[index] = 0;
		if (written != read)
			games[written] = games[read];
		written++;
	}
	return written;
}

/* the games found in the favorites' order (browser_favorites_order), the
favorites offline after them; returns how many found are listed */
static short lobby_browser_favorites(short count)
{
	static struct browser_favorite favorites[LOBBY_BROWSER_GAMES];
	static struct p2p_listing ordered[LOBBY_BROWSER_GAMES];
	short favorite_of[LOBBY_BROWSER_GAMES], order[LOBBY_BROWSER_GAMES];
	short index, listed;

	static struct browser_sort_entry entries[LOBBY_BROWSER_GAMES];

	for (index = 0; index < count; index++)
	{
		struct p2p_listing const *game = &lobby_browser.games[index];

		listing_favorite(game, &favorites[index]);
		snprintf(entries[index].name, sizeof(entries[index].name), "%s", game->name);
		snprintf(entries[index].map, sizeof(entries[index].map), "%s", game->map);
		entries[index].players = game->player_count;
		entries[index].ping = game->failed ? NONE : game->ping;
		entries[index].rate = game->rate;
		entries[index].openh1 = game->standard != 0;
		entries[index].coop = game->engine_type == 0;
		/* (on a master server's word alone: p2p_lobby.c) */
		entries[index].dedicated = game->dedicated != 0;
		settings_wide_copy(entries[index].mode, game_kind_name(game->engine_type, game->kind, FALSE),
			BROWSER_MODE_LENGTH);
		entries[index].maximum_players = game->maximum_player_count;
		entries[index].engine_type = (short)game->engine_type;
		entries[index].locked = game->locked != 0;
		/* (the map's name as shown, for the words searched for alone) */
		entries[index].map_shown[0] = 0;
		if (browser_filters.search[0])
			map_display_name(game->map, entries[index].map_shown);
	}
	browser_favorites_match(favorites, count, favorite_of, "net:");
	browser_filtering = TRUE;
	listed = browser_favorites_order(favorite_of, entries, count, order);
	for (index = 0; index < listed; index++)
		ordered[index] = lobby_browser.games[order[index]];
	csmemcpy(lobby_browser.games, ordered, listed * sizeof(ordered[0]));
	return listed;
}

/* the rows the list has: the games found, then the favorites offline */
static short lobby_browser_rows(void)
{
	return (short)(lobby_browser.count + browser_offline_count);
}

/* the game being joined, once its host is reached (the client's game from
it), else NULL */
static struct advertised_game *lobby_browser_joined_game(void)
{
	void *client = global_network_game_client_get();
	struct advertised_game *games = client ? network_game_client_get_available_games(client) : NULL;
	short index;

	for (index = 0; games && index < MAXIMUM_ADVERTISED_GAMES; index++)
	{
		if (network_game_client_advertised_game_is_valid(&games[index]) &&
			!memcmp(games[index].xnaddr + 2, lobby_browser.identifier, sizeof(lobby_browser.identifier)))
		{
			return &games[index];
		}
	}
	return NULL;
}

/* the focus on a game's row (0 to BROWSER_ROWS - 1) */
static void lobby_browser_focus_row(struct widget_instance *list, short row)
{
	struct widget_instance *child;
	short child_index = 0;
	char name[16];

	sprintf(name, "server_item_%d", row + 1);
	for (child = list->child; child; child = child->next, child_index++)
	{
		if (!strcmp(child->name, name))
		{
			list->focused_child = child;
			list->parameters.list.selected_index = child_index;
			return;
		}
	}
}

/* "gamespy screen update" for the server browser: the rows (scrolled on
at their ends), the panel, the counts and tabs over them */
static void lobby_browser_update(struct widget_instance *list)
{
	struct widget_instance *row;
	short focused = browser_row_index(list->focused_child);
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	unsigned long now = system_milliseconds();
	short chosen, rows, shown = browser_rows_of(list);

	/* (this machine's party, as the rows count it) */
	browser_party_size = browser_party_members();
	lobby_browser.count = lobby_browser_valid_games(lobby_browser.games,
		(short)p2p_lobby_games(lobby_browser.games, LOBBY_BROWSER_GAMES));
	lobby_browser.count = lobby_browser_favorites(lobby_browser.count);
	rows = lobby_browser_rows();
	/* (the game starred or unstarred, moved by it: its row focused) */
	if (browser_follow_key[0])
	{
		short index;

		for (index = 0; index < rows; index++)
		{
			struct browser_favorite favorite;

			if (index < lobby_browser.count)
				listing_favorite(&lobby_browser.games[index], &favorite);
			else
				favorite = *browser_favorites_get(browser_offline[index - lobby_browser.count]);
			if (!strcmp(favorite.key, browser_follow_key))
			{
				lobby_browser.chosen = index;
				lobby_browser.first = (short)PIN(lobby_browser.first, index - shown + 1, index);
				focused = (short)(index - lobby_browser.first);
				lobby_browser_focus_row(list, focused);
				break;
			}
		}
		browser_follow_key[0] = 0;
	}
	if (focused == shown - 1 && lobby_browser.first + shown < rows && list_scrolls_at_end())
	{
		lobby_browser.first++;
		lobby_browser_focus_row(list, --focused);
	}
	else if (focused == 0 && lobby_browser.first > 0 && list_scrolls_at_end())
	{
		lobby_browser.first--;
		lobby_browser_focus_row(list, ++focused);
	}
	lobby_browser.first = (short)PIN(lobby_browser.first, 0, MAX(0, rows - shown));
	/* (the first game found takes the focus from the buttons, which had it
	while there were none) */
	if (rows && !lobby_browser.shown)
	{
		struct widget_instance *first_row = named(list, "server_item_1", 0);

		lobby_browser.shown = TRUE;
		if (first_row)
		{
			first_row->visible = TRUE;
			lobby_browser_focus_row(list, 0);
			focused = 0;
			lobby_browser.chosen = lobby_browser.first;
		}
	}
	if (focused != NONE && !ui_widget_port_hover_focused())
		lobby_browser.chosen = (short)(lobby_browser.first + focused);
	chosen = lobby_browser.chosen;
	/* (its host's tunnel opened and kept while it is chosen: joining it is
	then at once) */
	if (chosen < lobby_browser.count && !lobby_browser.joining && lobby_browser.games[chosen].open &&
		!lobby_browser.games[chosen].failed)
	{
		p2p_probe_keep(&lobby_browser.games[chosen]);
	}
	for (row = list->child; row; row = row->next)
	{
		short index = browser_row_index(row);
		short row_index = index;
		struct browser_row shown;

		if (index == NONE)
			continue;
		index = (short)(index + lobby_browser.first);
		row->visible = index < rows;
		if (index >= rows)
			continue;
		if (index >= lobby_browser.count)
			browser_offline_row(browser_favorites_get(browser_offline[index - lobby_browser.count]), &shown);
		else
			browser_listing_row(&lobby_browser.games[index], &shown);
		browser_row_show(row, row_index, &shown, index < browser_found_favorites);
	}
	/* joining: once the host is reached, its game joined (by an A press, on
	its row); given up after a while */
	if (lobby_browser.ready && now - lobby_browser.ready_time > 3000 &&
		now - lobby_browser.join_time <= LOBBY_BROWSER_JOIN_TIMEOUT)
	{
		lobby_browser.ready_time = now;
		ui_widget_port_post_button(lobby_browser.controller, BUTTON_A);
	}
	if (lobby_browser.joining)
	{
		if (lobby_browser.ready)
		{
			/* (its press posted: waiting for it, or for the timeout) */
			if (now - lobby_browser.join_time > LOBBY_BROWSER_JOIN_TIMEOUT)
				lobby_browser.joining = lobby_browser.ready = FALSE;
		}
		else if (lobby_browser_joined_game())
		{
			short index;

			lobby_browser.ready = TRUE;
			for (index = 0; index < lobby_browser.count; index++)
			{
				if (!memcmp(lobby_browser.games[index].identifier, lobby_browser.identifier, 6) &&
					index >= lobby_browser.first && index < lobby_browser.first + shown)
				{
					lobby_browser_focus_row(list, (short)(index - lobby_browser.first));
				}
			}
			lobby_browser.ready_time = now;
			ui_widget_port_post_button(lobby_browser.controller, BUTTON_A);
		}
		else if (lobby_browser_join_timed_out(now - lobby_browser.join_time))
		{
			struct p2p_join_status status;
			wchar_t headline[CONNECT_HEADLINE_LENGTH], detail[CONNECT_DETAIL_LENGTH];

			p2p_join_status(&status);
			connect_failure_log(lobby_browser.name, &status);
			connect_failure_text(&status, TRUE, CONNECT_ON_PHONE, headline, detail, lobby_browser.failed_line);
			p2p_join_cancel();
			p2p_lobby_mark_failed(lobby_browser.identifier);
			csmemcpy(lobby_browser.failed_name, lobby_browser.name, sizeof(lobby_browser.failed_name));
			lobby_browser.failed_time = now ? now : 1;
			lobby_browser.failed_nat_strict = p2p_nat_strict();
			lobby_browser.joining = FALSE;
			platform_log("menus: could not reach the server browser's game %s", lobby_browser.name);
			ui_play_audio_feedback_sound(SOUND_ERROR);
		}
	}
	/* the panel: the game chosen, how it is, or the join under way, or why
	it failed */
	{
		wchar_t name[P2P_LISTING_NAME_SIZE + 1];
		struct browser_row shown;
		long players = 0;
		short index;

		text[0] = 0;
		if (chosen < lobby_browser.count)
			browser_listing_row(&lobby_browser.games[chosen], &shown);
		else if (chosen < rows)
			browser_offline_row(browser_favorites_get(browser_offline[chosen - lobby_browser.count]), &shown);
		if (lobby_browser.joining)
		{
			struct p2p_join_status status;
			wchar_t stage[ROW_TEXT_LENGTH];

			p2p_join_status(&status);
			connect_stage_text(&status, stage, NUMBEROF(stage));
			for (index = 0; stage[index]; index++)
			{
				if (stage[index] == '\r' || stage[index] == '\n')
					stage[index] = ' ';
			}
			text_to_wide(lobby_browser.name, name, NUMBEROF(name));
			usnprintf(text, NUMBEROF(text) - 1, L"Connecting to %s: %s", name, stage);
		}
		else if (lobby_browser.failed_time && now - lobby_browser.failed_time < LOBBY_BROWSER_FAILED_TIME)
		{
			text_to_wide(lobby_browser.failed_name, name, NUMBEROF(name));
			/* (and why, in words for anyone: connect_failure_text; the game
			named only when it is not the one the panel shows, for the
			panel's three lines) */
			if (chosen < lobby_browser.count &&
				!memcmp(lobby_browser.games[chosen].identifier, lobby_browser.identifier, 6))
			{
				usnprintf(text, NUMBEROF(text) - 1, L"Couldn't join. %s", lobby_browser.failed_line);
			}
			else
				usnprintf(text, NUMBEROF(text) - 1, L"Couldn't reach %s. %s", name, lobby_browser.failed_line);
		}
		else if (chosen >= lobby_browser.count && chosen < rows)
		{
			browser_offline_line(browser_favorites_get(browser_offline[chosen - lobby_browser.count]), text,
				NUMBEROF(text));
		}
		else if (chosen < rows)
			browser_status_text(&shown, text, NUMBEROF(text));
		else
		{
			usnprintf(text, NUMBEROF(text) - 1, L"Every public game on the Internet, OpenH1 and Legacy.");
		}
		text[NUMBEROF(text) - 1] = 0;
		browser_panel_show(list, chosen < rows ? &shown : NULL, L"SERVER BROWSER", text);
		for (index = 0; index < lobby_browser.count; index++)
			players += lobby_browser.games[index].player_count;
		browser_empty_text(multiplayer.mode, browser_tab, config_boolean("network.online"),
			config_boolean("network.public_lobby"), now - lobby_browser.begin_time < LOBBY_BROWSER_LOOK_TIME,
			p2p_lobby_newer_games(), browser_filtered_count, text, NUMBEROF(text));
		browser_header_show(list, lobby_browser.count, players, rows, text);
	}
	browser_focus(list, browser_chosen_row());
}

/* the game's host reached by its invite (joined, once it is reached:
lobby_browser_update) */
static boolean lobby_browser_join(struct p2p_listing const *game, short controller)
{
	/* (a public game: the text chat is off in it, whatever its host says:
	network_chat.c) */
	if (!p2p_join_listing(game))
		return FALSE;
	csmemcpy(lobby_browser.identifier, game->identifier, sizeof(lobby_browser.identifier));
	csmemcpy(lobby_browser.name, game->name, sizeof(lobby_browser.name));
	lobby_browser.joining = TRUE;
	lobby_browser.ready = FALSE;
	lobby_browser.join_time = system_milliseconds();
	lobby_browser.controller = controller;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* the server browser's rows and JOIN GAME: a row (or JOIN GAME) joins its
game's invite, a game with a password once its
screen has it; the A press posted once its host is reached joins its game
(or shows its lobby, if under way) */
static boolean lobby_browser_select(struct widget_instance *widget, short controller, short row,
	boolean *widget_deleted)
{
	struct p2p_listing const *game;
	short index;

	/* (the press posted: whatever has the focus) */
	if (lobby_browser.ready)
	{
		struct advertised_game *found = lobby_browser_joined_game();

		lobby_browser.joining = lobby_browser.ready = FALSE;
		if (!found)
			return campaign_fail();
		if (advertised_in_progress(found))
		{
			/* (player 1 joining it: others join them there) */
			if (!multiplayer_player(controller))
				return FALSE;
			csmemcpy(multiplayer.preview_key_id, found->key_id, sizeof(multiplayer.preview_key_id));
			csmemcpy(multiplayer.preview_xnaddr, found->xnaddr, sizeof(multiplayer.preview_xnaddr));
			return ui_widget_port_open(widget, PREVIEW_NAME, widget_deleted);
		}
		if (!found->open)
			return campaign_fail();
		if (!multiplayer_player(controller))
			return FALSE;
		return advertised_join(widget, found, widget_deleted);
	}
	if (row == NONE && !strstr(widget->name, "button_join"))
		return TRUE;
	if (lobby_browser.joining)
		return TRUE;
	index = row != NONE ? (short)(lobby_browser.first + row) : lobby_browser.chosen;
	if (index >= lobby_browser.count)
		return campaign_fail();
	game = &lobby_browser.games[index];
	if (!game->open || !config_boolean("network.online"))
		return campaign_fail();
	/* (a game with a password: its screen asks for it, then joins) */
	if (game->locked)
	{
		lobby_browser.password_game = *game;
		lobby_browser.controller = controller;
		return ui_widget_port_open(widget, PASSWORD_NAME, widget_deleted);
	}
	if (!lobby_browser_join(game, controller))
		return campaign_fail();
	connect_dialog.listing = TRUE;
	connect_dialog.game = *game;
	return connect_open(widget, controller, widget_deleted);
}

/* ---- the server browser's password screen (a game with a password
chosen: lobby_browser_select, tools/port_settings.py): its password typed at
once, shown as stars; JOIN GAME, or Enter, joins the game with it if it is
the game's (the browser, back again, goes on with the join), else its help
says it is not and the typing begins again */

/* its help's lines (tools/port_settings.py's) */
enum
{
	_password_help_ask = 1,
	_password_help_wrong,
	_password_help_failed,
};

static struct
{
	char text[PASSWORD_LENGTH + 1];
	short help;
} password_screen;

static void password_screen_done(char const *text)
{
	snprintf(password_screen.text, sizeof(password_screen.text), "%s", text);
}

/* "port password init" */
static boolean password_screen_initialize(struct widget_instance *list)
{
	struct widget_instance *row = named(list, "op_password", 0);

	password_screen.text[0] = 0;
	password_screen.help = _password_help_ask;
	if (row)
		text_field_begin_masked(row, "", PASSWORD_LENGTH, password_screen_done);
	return TRUE;
}

/* "port password update" */
static void password_screen_update(struct widget_instance *list)
{
	struct widget_instance *help = list->parameters.list.extended_description;
	char stars[PASSWORD_LENGTH + 1];
	short index;

	for (index = 0; password_screen.text[index] && index < PASSWORD_LENGTH; index++)
		stars[index] = '*';
	stars[index] = 0;
	text_field_show(named(list, "password_value", 0), stars, text_field_editing(named(list, "op_password", 0)));
	if (help)
		help->parameters.text_box.string_list_index = password_screen.help;
}

/* "port password join" (JOIN GAME, and Enter: password_screen_edit) */
static boolean password_screen_join(struct widget_instance *widget, boolean *widget_deleted)
{
	struct p2p_listing game = lobby_browser.password_game;
	struct widget_instance *row = named(screen_of(widget), "op_password", 0);

	if (text_field_editing(NULL))
		text_field_end(TRUE);
	if (!p2p_listing_unlock(&game, password_screen.text))
	{
		password_screen.help = _password_help_wrong;
		password_screen.text[0] = 0;
		if (row)
			text_field_begin_masked(row, "", PASSWORD_LENGTH, password_screen_done);
		return campaign_fail();
	}
	if (!lobby_browser_join(&game, lobby_browser.controller))
	{
		password_screen.help = _password_help_failed;
		return campaign_fail();
	}
	password_screen.text[0] = 0;
	lobby_browser.password_joined = TRUE;
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* "port password back" (B, Escape): back to the browser at once, though the
password is being typed */
static boolean password_screen_back(struct widget_instance *widget, boolean *widget_deleted)
{
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	password_screen.text[0] = 0;
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* "port password edit" (the field's row's A: the typing begins; Enter, its
end, joins) */
static boolean password_screen_edit(struct widget_instance *row, boolean *widget_deleted)
{
	if (text_field_editing(row))
		return password_screen_join(row, widget_deleted);
	text_field_begin_masked(row, password_screen.text, PASSWORD_LENGTH, password_screen_done);
	return TRUE;
}

/* the Local Network and Direct Link lists' games in the favorites' order
(browser_favorites_order), the local network's favorites offline after them
(Direct Link's games are invites': none is listed offline); the game
starred or unstarred last chosen again (scrolled to), and the game chosen
kept among them; returns how many there are */
static short local_browser_favorites(struct widget_instance *list)
{
	static struct browser_favorite favorites[MAXIMUM_ADVERTISED_GAMES];
	static struct browser_sort_entry entries[MAXIMUM_ADVERTISED_GAMES];
	struct advertised_game *games[MAXIMUM_ADVERTISED_GAMES];
	short favorite_of[MAXIMUM_ADVERTISED_GAMES], order[MAXIMUM_ADVERTISED_GAMES];
	short index, rows;

	for (index = 0; index < multiplayer.game_count; index++)
	{
		struct advertised_game *game = multiplayer.games[index];

		advertised_favorite(game, &favorites[index]);
		games[index] = game;
		snprintf(entries[index].name, sizeof(entries[index].name), "%s", favorites[index].name);
		snprintf(entries[index].map, sizeof(entries[index].map), "%s", favorites[index].map);
		entries[index].players = game->player_count;
		/* (a Direct Link game's host's: the tunnel's) */
		entries[index].ping = multiplayer.mode == _multiplayer_mode_direct_link ?
			(short)p2p_peer_ping(game->xnaddr + 2) : NONE;
		entries[index].rate = (short)advertised_game_rate(game);
		entries[index].openh1 = advertised_game_standard(game);
		entries[index].coop = game->engine_type == 0;
		entries[index].dedicated = advertised_dedicated(game);
		settings_wide_copy(entries[index].mode, game_kind_name(game->engine_type, favorites[index].kind, FALSE),
			BROWSER_MODE_LENGTH);
		entries[index].maximum_players = game->maximum_player_count;
		entries[index].engine_type = (short)game->engine_type;
		entries[index].locked = FALSE;
		entries[index].map_shown[0] = 0;
		if (browser_filters.search[0])
			game_map_name(game, entries[index].map_shown);
	}
	browser_favorites_match(favorites, multiplayer.game_count, favorite_of,
		multiplayer.mode == _multiplayer_mode_lan ? "lan:" : NULL);
	/* (Join by Link's games are the invites taken: never hidden) */
	browser_filtering = multiplayer.mode != _multiplayer_mode_direct_link;
	multiplayer.game_count = browser_favorites_order(favorite_of, entries, multiplayer.game_count, order);
	for (index = 0; index < multiplayer.game_count; index++)
		multiplayer.games[index] = games[order[index]];
	rows = (short)(multiplayer.game_count + browser_offline_count);
	if (browser_follow_key[0])
	{
		for (index = 0; index < rows; index++)
		{
			char const *key = index < multiplayer.game_count ? favorites[order[index]].key :
				browser_favorites_get(browser_offline[index - multiplayer.game_count])->key;

			if (!strcmp(key, browser_follow_key))
			{
				multiplayer.game_chosen = index;
				multiplayer.game_first = (short)PIN(multiplayer.game_first, index - browser_shown_rows + 1, index);
				lobby_browser_focus_row(list, (short)(index - multiplayer.game_first));
				break;
			}
		}
		browser_follow_key[0] = 0;
	}
	if (multiplayer.game_chosen >= rows)
		multiplayer.game_chosen = (short)MAX(0, rows - 1);
	return rows;
}

/* "gamespy screen update": the games' rows, the one chosen's details in the
panel, the counts and tabs over them (the server browser's: above) */
static void browser_update(struct widget_instance *list)
{
	struct widget_instance *row;
	short focused = browser_row_index(list->focused_child);
	short rows, chosen;
	struct browser_row shown;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	long players = 0;
	short index, shown_rows = browser_rows_of(list);

	/* (the browsers' keys, while one is shown: Tab sorts, E is the next tab
	alone; halo_keyboard_browser_keys) */
	halo_keyboard_browser_keys();
	/* (the Party screen may open over it: a party's link joined by JOIN BY
	LINK, party_menu.inc) */
	party_screen_noted(list);
	if (multiplayer.mode == _multiplayer_mode_server_browser)
	{
		lobby_browser_update(list);
		return;
	}
	browser_games_read();
	rows = local_browser_favorites(list);
	/* (more than its rows: scrolled on at their ends, as the server
	browser's) */
	if (focused == shown_rows - 1 && multiplayer.game_first + shown_rows < rows && list_scrolls_at_end())
	{
		multiplayer.game_first++;
		lobby_browser_focus_row(list, --focused);
	}
	else if (focused == 0 && multiplayer.game_first > 0 && list_scrolls_at_end())
	{
		multiplayer.game_first--;
		lobby_browser_focus_row(list, ++focused);
	}
	multiplayer.game_first = (short)PIN(multiplayer.game_first, 0, MAX(0, rows - shown_rows));
	if (focused != NONE && multiplayer.game_first + focused < rows && !ui_widget_port_hover_focused())
		multiplayer.game_chosen = (short)(multiplayer.game_first + focused);
	chosen = multiplayer.game_chosen;
	for (row = list->child; row; row = row->next)
	{
		short row_index = browser_row_index(row);

		if (row_index == NONE)
			continue;
		index = (short)(multiplayer.game_first + row_index);
		row->visible = index < rows;
		if (index >= rows)
			continue;
		if (index >= multiplayer.game_count)
			browser_offline_row(browser_favorites_get(browser_offline[index - multiplayer.game_count]), &shown);
		else
			browser_advertised_row(multiplayer.games[index], &shown);
		browser_row_show(row, row_index, &shown, index < browser_found_favorites);
	}
	if (chosen < multiplayer.game_count)
	{
		browser_advertised_row(multiplayer.games[chosen], &shown);
		browser_status_text(&shown, text, NUMBEROF(text));
	}
	else if (chosen < rows)
	{
		browser_offline_row(browser_favorites_get(browser_offline[chosen - multiplayer.game_count]), &shown);
		browser_offline_line(browser_favorites_get(browser_offline[chosen - multiplayer.game_count]), text,
			NUMBEROF(text));
	}
	else
	{
		usnprintf(text, NUMBEROF(text) - 1, multiplayer.mode == _multiplayer_mode_direct_link ?
			L"Games you were invited to, by a link or on Discord." :
			L"Games hosted on your Wi-Fi or local network.");
		text[NUMBEROF(text) - 1] = 0;
	}
	browser_panel_show(list, chosen < rows ? &shown : NULL,
		multiplayer.mode == _multiplayer_mode_direct_link ? L"JOIN BY LINK" : L"LOCAL NETWORK", text);
	for (index = 0; index < multiplayer.game_count; index++)
		players += multiplayer.games[index]->player_count;
	browser_empty_text(multiplayer.mode, browser_tab, TRUE, TRUE, FALSE, 0, browser_filtered_count, text,
		NUMBEROF(text));
	browser_header_show(list, multiplayer.game_count, players, rows, text);
	browser_focus(list, browser_chosen_row());
}

/* ---- Multiplayer's panel: in the place of the PC version's pictures, what
is live for the item chosen ("port multiplayer status", each frame): the
games online now (internet play's listings, looked for from the moment the
menu opens: the server browser then opens with them), an invite link on the
clipboard, whether others can reach this machine, the campaign's next level,
the gametypes. A headline (white) and a line below it; the item's
description stays below them. */

/* the items (and their descriptions', tools/port_settings.py's
HUB_DESCRIPTIONS, in this order) */
enum
{
	_multiplayer_item_server_browser,
	_multiplayer_item_join_local,
	_multiplayer_item_join_link,
	_multiplayer_item_create_internet,
	_multiplayer_item_create_local,
	_multiplayer_item_online_coop,
	_multiplayer_item_split_screen_coop,
	_multiplayer_item_offline,
	_multiplayer_item_gametypes,
	/* (CREATE GAME's and CO-OP CAMPAIGN's, which open their pages) */
	_multiplayer_item_create,
	_multiplayer_item_coop,
	/* (PARTY, which opens the party's screen: party_menu.inc) */
	_multiplayer_item_party,
	/* (MATCHMAKING, which opens its screen: matchmaking_menu.inc) */
	_multiplayer_item_matchmaking,
	/* (FRIENDS, which opens its screen: friends_menu.inc) */
	_multiplayer_item_friends,
};

/* Multiplayer's items and its pages' (port_settings.py's _hub), by their
names */
static struct
{
	char const *name;
	short item;
} const multiplayer_items[] =
{
	{ "hub_join_internet", _multiplayer_item_server_browser },
	{ "hub_join_lan_item", _multiplayer_item_join_local },
	{ "hub_join_direct", _multiplayer_item_join_link },
	{ "hub_create", _multiplayer_item_create },
	{ "hub_coop", _multiplayer_item_coop },
	{ "hub_gametypes", _multiplayer_item_gametypes },
	{ "hub_create_inter", _multiplayer_item_create_internet },
	{ "hub_create_lan_item", _multiplayer_item_create_local },
	{ "hub_create_offline", _multiplayer_item_offline },
	{ "hub_online_coop", _multiplayer_item_online_coop },
	{ "hub_split_coop", _multiplayer_item_split_screen_coop },
	{ "hub_party", _multiplayer_item_party },
	{ "hub_matchmaking", _multiplayer_item_matchmaking },
	{ "hub_friends", _multiplayer_item_friends },
};

#define MULTIPLAYER_DESCRIPTIONS "pc\\main_menu\\multiplayer_type_select\\hub\\descriptions"

/* how often the games online and the clipboard are read again */
#define MULTIPLAYER_STATUS_READ_TIME 500
/* the games with players the panel names, and how much of each name */
#define MULTIPLAYER_STATUS_GAMES 4
#define MULTIPLAYER_STATUS_NAME 18

static struct
{
	/* the menu's own looking for games (lobby_browser_begin's is the
	browser's) */
	boolean browsing;
	unsigned long browse_time;
	/* the last read */
	unsigned long read_time;
	short games, players;
	boolean invite;
} multiplayer_status;

/* "port multiplayer status end" (the menu's deleted): its looking stopped
(the server browser, opened next, looks for itself) */
static void multiplayer_status_end(void)
{
	if (multiplayer_status.browsing)
		p2p_lobby_browse(FALSE);
	multiplayer_status.browsing = FALSE;
}

/* whether internet play's public games can be looked for */
static boolean multiplayer_status_online(void)
{
	return config_boolean("network.online") && config_boolean("network.public_lobby");
}

static void multiplayer_status_read(void)
{
	unsigned long now = system_milliseconds();

	if (multiplayer_status.read_time && now - multiplayer_status.read_time < MULTIPLAYER_STATUS_READ_TIME)
		return;
	multiplayer_status.read_time = now;
	if (multiplayer_status_online() && !multiplayer_status.browsing)
	{
		p2p_lobby_browse(TRUE);
		multiplayer_status.browsing = TRUE;
		multiplayer_status.browse_time = now;
		multiplayer_status.games = multiplayer_status.players = 0;
	}
	if (multiplayer_status.browsing)
	{
		short count = lobby_browser_valid_games(lobby_browser.games,
			(short)p2p_lobby_games(lobby_browser.games, LOBBY_BROWSER_GAMES));
		short index;

		multiplayer_status.games = count;
		multiplayer_status.players = 0;
		for (index = 0; index < count; index++)
			multiplayer_status.players = (short)(multiplayer_status.players + lobby_browser.games[index].player_count);
	}
	multiplayer_status.invite = platform_clipboard_has_invite() != 0;
}

/* the campaign's next level for player 1's profile (the one after the last
it finished), else NONE: without changing the profile chosen */
static short multiplayer_status_next_level(void)
{
	struct player_profile profile;
	long profile_index = player_ui_get_active_player_profile_index(0);
	short level, difficulty;

	if (profile_index == NONE)
		profile_index = player_ui_get_player1_last_used_profile_index();
	if (profile_index == NONE || !player_profile_get(profile_index, &profile))
		return NONE;
	player_profile_get_highest_completed_solo_level(&profile, &level, &difficulty);
	return (short)PIN(level + 1, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1);
}

/* the width of a digit of the counts (port_settings.DIGIT_SIZE), and how
many places a count has */
#define MULTIPLAYER_STATUS_DIGIT_WIDTH 24
#define MULTIPLAYER_STATUS_DIGIT_PLACES 3
#define MULTIPLAYER_STATUS_BEST_GAMES 3

/* a count in the titles' type: its digits (online_<name>_<place>, frames of
the digits' bitmap) from the left, and its label after them */
static void multiplayer_status_number(struct widget_instance *description, char const *name, long value,
	wchar_t const *label)
{
	char widget_name[32];
	char digits[8];
	short places, place;
	struct widget_instance *label_widget;

	snprintf(digits, sizeof(digits), "%ld", PIN(value, 0, 999));
	places = (short)strlen(digits);
	for (place = 0; place < MULTIPLAYER_STATUS_DIGIT_PLACES; place++)
	{
		struct widget_instance *digit;

		snprintf(widget_name, sizeof(widget_name), "online_%s_%d", name, place);
		digit = named(description, widget_name, 0);
		visible_set(digit, place < places);
		if (digit && place < places)
			digit->animation.current_frame_index = (short)(digits[place] - '0');
	}
	snprintf(widget_name, sizeof(widget_name), "online_%s_label", name);
	label_widget = named(description, widget_name, 0);
	if (label_widget)
		label_widget->horizontal_offset = (short)(places * MULTIPLAYER_STATUS_DIGIT_WIDTH + 6);
	text_set(label_widget, label);
}

/* how worth joining a game is: open, with room, the fuller the better, a
near host better than a far one */
static long multiplayer_status_game_score(struct p2p_listing const *game)
{
	if (!game->open || game->locked || game->failed || game->player_count >= game->maximum_player_count ||
		!game->maximum_player_count)
	{
		return -1;
	}
	return (long)game->player_count * 1000 / game->maximum_player_count - (game->ping >= 0 ? game->ping : 150);
}

/* BEST FOR YOU: the games most worth joining, their names, maps and
gametypes, players and pings */
static void multiplayer_status_best(struct widget_instance *description)
{
	short chosen[MULTIPLAYER_STATUS_BEST_GAMES];
	short count = 0, row;

	while (count < MULTIPLAYER_STATUS_BEST_GAMES)
	{
		short index, best = NONE;
		long best_score = -1;

		for (index = 0; index < multiplayer_status.games; index++)
		{
			long score = multiplayer_status_game_score(&lobby_browser.games[index]);
			short taken;

			for (taken = 0; taken < count && chosen[taken] != index; taken++)
			{
			}
			if (taken == count && score > best_score)
			{
				best = index;
				best_score = score;
			}
		}
		if (best == NONE)
			break;
		chosen[count++] = best;
	}
	visible_set(named(description, "best_label", 0), count > 0);
	for (row = 0; row < MULTIPLAYER_STATUS_BEST_GAMES; row++)
	{
		struct p2p_listing const *game = row < count ? &lobby_browser.games[chosen[row]] : NULL;
		char name[32];
		wchar_t text[ROW_TEXT_LENGTH], map[ROW_TEXT_LENGTH], gametype[ROW_TEXT_LENGTH];
		struct widget_instance *ping, *badge, *name_widget;
		char const *const parts[] = { "name", "badge", "about", "players", "ping" };
		short part, badge_width = 0, drawn;

		for (part = 0; part < NUMBEROF(parts); part++)
		{
			snprintf(name, sizeof(name), "best_%d_%s", row, parts[part]);
			visible_set(named(description, name, 0), game != NULL);
		}
		if (!game)
			continue;
		/* (its name, cut short with "..." to leave room for its badge after
		it: its tick rate, an OpenH1 game's, else LEGACY) */
		snprintf(name, sizeof(name), "best_%d_badge", row);
		badge = named(description, name, 0);
		ui_widget_port_text_box_font(badge, &badge_width);
		snprintf(name, sizeof(name), "best_%d_name", row);
		name_widget = named(description, name, 0);
		text_to_wide(game->name, text, NUMBEROF(text));
		ui_widget_port_text_box_font(name_widget, &drawn);
		drawn = menu_text_box_fit(name_widget, text, (short)(drawn - (badge_width ? badge_width + 6 : 0)));
		if (badge)
		{
			badge->horizontal_offset = (short)(drawn + 6);
			badge->animation.current_frame_index = game->standard ? 0 : 1;
			usnprintf(text, NUMBEROF(text) - 1, L"%d TICK", game->rate);
			text[NUMBEROF(text) - 1] = 0;
			text_set(badge, game->standard ? text : L"LEGACY");
		}
		map_display_name(game->map, map);
		if (game->gametype[0])
			text_to_wide(game->gametype, gametype, NUMBEROF(gametype));
		else
			usnprintf(gametype, NUMBEROF(gametype) - 1, L"%s", game_type_name(game->engine_type));
		gametype[NUMBEROF(gametype) - 1] = 0;
		usnprintf(text, NUMBEROF(text) - 1, L"%s \x00B7 %s", map, gametype);
		text[NUMBEROF(text) - 1] = 0;
		snprintf(name, sizeof(name), "best_%d_about", row);
		menu_text_box_fit(named(description, name, 0), text, 0);
		usnprintf(text, NUMBEROF(text) - 1, L"%d/%d", game->player_count, game->maximum_player_count);
		snprintf(name, sizeof(name), "best_%d_players", row);
		text_set(named(description, name, 0), text);
		snprintf(name, sizeof(name), "best_%d_ping", row);
		if ((ping = named(description, name, 0)) != NULL)
		{
			ping->animation.current_frame_index = (short)(game->ping < 0 ? 0 : game->ping < PING_GOOD ? 3 :
				game->ping < PING_FAIR ? 2 : 1);
		}
	}
}

/* the games the local network's client has found (one made by the Local
Network list before), else NONE: none is looked for from here */
static short multiplayer_status_local_games(void)
{
	void *client = global_network_game_client_get();
	struct advertised_game *games;
	short index, count = 0;

	if (!client || (games = network_game_client_get_available_games(client)) == NULL)
		return NONE;
	for (index = 0; index < MAXIMUM_ADVERTISED_GAMES; index++)
	{
		if (network_game_client_advertised_game_is_valid(&games[index]) && !game_from_peer(&games[index]))
			count++;
	}
	return count;
}

/* the notes at the end of the items, beside their names: the public games
online, the local games found (once the local network has been looked
at), an invite link copied */
static void multiplayer_status_notes(struct widget_instance *list)
{
	wchar_t text[ROW_TEXT_LENGTH];
	short local = multiplayer_status_local_games();

	text[0] = 0;
	if (multiplayer_status.browsing && multiplayer_status.games)
	{
		usnprintf(text, NUMBEROF(text) - 1, multiplayer_status.games == 1 ? L"1 game" : L"%d games",
			multiplayer_status.games);
	}
	text[NUMBEROF(text) - 1] = 0;
	text_set(named(list, "browser_note", 0), text);
	text[0] = 0;
	if (local == 0)
		usnprintf(text, NUMBEROF(text) - 1, L"None found");
	else if (local != NONE)
		usnprintf(text, NUMBEROF(text) - 1, L"%d found", local);
	text[NUMBEROF(text) - 1] = 0;
	text_set(named(list, "item_note", 0), text);
	text_set(named(list, "invite_note", 0), multiplayer_status.invite ? L"INVITE READY" : L"");
	party_hub_note(text, NUMBEROF(text));
	text_set(named(list, "party_note", 0), text);
	matchmaking_hub_note(text, NUMBEROF(text));
	text_set(named(list, "matchmaking_note", 0), text);
	friends_hub_note(text, NUMBEROF(text));
	text_set(named(list, "friends_note", 0), text);
}

/* the item chosen: Multiplayer's list's (the computer's and the
touchscreen's: the same names), and its pages', by its name */
static short multiplayer_status_item(struct widget_instance *list)
{
	short index;

	for (index = 0; list->focused_child && index < NUMBEROF(multiplayer_items); index++)
	{
		if (!strcmp(list->focused_child->name, multiplayer_items[index].name))
			return multiplayer_items[index].item;
	}
	return _multiplayer_item_server_browser;
}

static void multiplayer_status_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	wchar_t headline[ROW_TEXT_LENGTH * 2], detail[ROW_TEXT_LENGTH * 8];
	short item = multiplayer_status_item(list);

	boolean counts;

	/* (the Party screen may open over it: party_menu.inc) */
	party_screen_noted(list);
	if (!description)
		return;
	multiplayer_status_read();
	multiplayer_status_notes(list);
	/* (the PC version's pictures, which these take the place of) */
	visible_set(named(description, "multiplayer_options_pic", 0), FALSE);
	/* (the item's description, port_settings.py's HUB_DESCRIPTIONS, wrapped
	to the panel) */
	menu_text_box_wrap(named(description, "multiplayer_options_txt", 0), settings_string(MULTIPLAYER_DESCRIPTIONS, item), 4);
	/* (the Server Browser's, once games are found: ONLINE NOW's counts and
	BEST FOR YOU) */
	counts = item == _multiplayer_item_server_browser && multiplayer_status.browsing && multiplayer_status.games;
	{
		char const *const names[] = { "online_now_label", "online_games_label", "online_players_label" };
		short index;

		for (index = 0; index < NUMBEROF(names); index++)
			visible_set(named(description, names[index], 0), counts);
		visible_set(named(description, "multiplayer_status", 0), !counts);
		visible_set(named(description, "multiplayer_status_detail", 0), !counts);
	}
	if (counts)
	{
		multiplayer_status_number(description, "games", multiplayer_status.games, L"GAMES");
		multiplayer_status_number(description, "players", multiplayer_status.players, L"PLAYERS");
		multiplayer_status_best(description);
		return;
	}
	{
		char name[32];
		short place, row;
		char const *const parts[] = { "name", "badge", "about", "players", "ping" };

		for (place = 0; place < MULTIPLAYER_STATUS_DIGIT_PLACES; place++)
		{
			snprintf(name, sizeof(name), "online_games_%d", place);
			visible_set(named(description, name, 0), FALSE);
			snprintf(name, sizeof(name), "online_players_%d", place);
			visible_set(named(description, name, 0), FALSE);
		}
		visible_set(named(description, "best_label", 0), FALSE);
		for (row = 0; row < MULTIPLAYER_STATUS_BEST_GAMES; row++)
		{
			for (place = 0; place < NUMBEROF(parts); place++)
			{
				snprintf(name, sizeof(name), "best_%d_%s", row, parts[place]);
				visible_set(named(description, name, 0), FALSE);
			}
		}
	}
	headline[0] = detail[0] = 0;
	switch (item)
	{
	case _multiplayer_item_server_browser:
		if (!config_boolean("network.online"))
		{
			usnprintf(headline, NUMBEROF(headline) - 1, L"INTERNET PLAY IS OFF");
			usnprintf(detail, NUMBEROF(detail) - 1, L"Turn it on in Settings,\r\nOnline.");
		}
		else if (!multiplayer_status.browsing)
			usnprintf(headline, NUMBEROF(headline) - 1, L"PUBLIC GAMES ARE HIDDEN");
		else if (!multiplayer_status.games &&
			system_milliseconds() - multiplayer_status.browse_time < LOBBY_BROWSER_LOOK_TIME)
		{
			usnprintf(headline, NUMBEROF(headline) - 1, L"LOOKING FOR GAMES...");
		}
		else
		{
			usnprintf(headline, NUMBEROF(headline) - 1, multiplayer_status.games == 1 ? L"1 GAME ONLINE" :
				L"%d GAMES ONLINE", multiplayer_status.games);
			usnprintf(detail, NUMBEROF(detail) - 1, multiplayer_status.players == 1 ? L"1 player in them now" :
				L"%d players in them now", multiplayer_status.players);
		}
		break;
	case _multiplayer_item_join_local:
		usnprintf(headline, NUMBEROF(headline) - 1, L"WI-FI AND LAN");
		usnprintf(detail, NUMBEROF(detail) - 1, L"Games hosted on this\r\nnetwork appear as they open.");
		break;
	case _multiplayer_item_join_link:
		if (multiplayer_status.invite)
		{
			usnprintf(headline, NUMBEROF(headline) - 1, L"INVITE LINK COPIED");
			usnprintf(detail, NUMBEROF(detail) - 1, L"Press %%a-button to join it.");
		}
		else
		{
			usnprintf(headline, NUMBEROF(headline) - 1, L"NO INVITE LINK YET");
			usnprintf(detail, NUMBEROF(detail) - 1, L"Copy the link your friend\r\nsent you, then come back.");
		}
		break;
	case _multiplayer_item_create_internet:
	case _multiplayer_item_online_coop:
		if (!config_boolean("network.online"))
		{
			usnprintf(headline, NUMBEROF(headline) - 1, L"INTERNET PLAY IS OFF");
			usnprintf(detail, NUMBEROF(detail) - 1, L"Turn it on in Settings,\r\nOnline.");
		}
		else if (p2p_nat_strict())
		{
			/* (this network's NAT gives each destination its own port, and no
			router forwards one: players behind strict networks too cannot
			reach the game) */
			usnprintf(headline, NUMBEROF(headline) - 1, L"NOT EVERYONE CAN JOIN");
			usnprintf(detail, NUMBEROF(detail) - 1, config_boolean("network.allow_upnp") ?
				L"Some players can't reach\r\nyou. Hosting on another\r\nnetwork may help." :
				L"Some players can't reach\r\nyou. Turn on OPEN ROUTER\r\nPORT in Settings, Online.");
		}
		else if (item == _multiplayer_item_online_coop && multiplayer_status_next_level() != NONE)
		{
			wchar_t level[ROW_TEXT_LENGTH];

			string_get("pc\\main_menu\\map_list", multiplayer_status_next_level(), level);
			usnprintf(headline, NUMBEROF(headline) - 1, L"NEXT LEVEL");
			usnprintf(detail, NUMBEROF(detail) - 1, L"%s", level);
		}
		else
		{
			usnprintf(headline, NUMBEROF(headline) - 1, L"INVITE BY LINK");
			usnprintf(detail, NUMBEROF(detail) - 1, L"Your link is copied when\r\nthe game opens.");
		}
		break;
	case _multiplayer_item_create_local:
		usnprintf(headline, NUMBEROF(headline) - 1, L"WI-FI AND LAN");
		usnprintf(detail, NUMBEROF(detail) - 1, L"Players on this network\r\nsee it in LOCAL NETWORK.");
		break;
	case _multiplayer_item_split_screen_coop:
		if (multiplayer_status_next_level() != NONE)
		{
			wchar_t level[ROW_TEXT_LENGTH];

			string_get("pc\\main_menu\\map_list", multiplayer_status_next_level(), level);
			usnprintf(headline, NUMBEROF(headline) - 1, L"NEXT LEVEL");
			usnprintf(detail, NUMBEROF(detail) - 1, L"%s", level);
		}
		else
			usnprintf(headline, NUMBEROF(headline) - 1, L"TWO PLAYERS, ONE SCREEN");
		break;
	case _multiplayer_item_create:
		usnprintf(headline, NUMBEROF(headline) - 1, L"HOST A GAME");
		usnprintf(detail, NUMBEROF(detail) - 1, L"On the Internet, or for\r\nyour local network only.");
		break;
	case _multiplayer_item_coop:
		usnprintf(headline, NUMBEROF(headline) - 1, L"THE CAMPAIGN TOGETHER");
		usnprintf(detail, NUMBEROF(detail) - 1, L"Online, or split screen\r\non this computer.");
		break;
	case _multiplayer_item_party:
		party_hub_status(headline, NUMBEROF(headline), detail, NUMBEROF(detail));
		break;
	case _multiplayer_item_matchmaking:
		matchmaking_hub_status(headline, NUMBEROF(headline), detail, NUMBEROF(detail));
		break;
	case _multiplayer_item_friends:
		friends_hub_status(headline, NUMBEROF(headline), detail, NUMBEROF(detail));
		break;
	case _multiplayer_item_offline:
		usnprintf(headline, NUMBEROF(headline) - 1, L"THIS COMPUTER ONLY");
		usnprintf(detail, NUMBEROF(detail) - 1, L"No network. Add bots in\r\nthe lobby; friends join\r\nwith a controller.");
		break;
	case _multiplayer_item_gametypes:
	{
		long indices[MAXIMUM_GAMETYPES];
		short last_used;
		short count = ui_widget_port_gametypes(indices, MAXIMUM_GAMETYPES, &last_used);

		usnprintf(headline, NUMBEROF(headline) - 1, L"%d GAMETYPES", count);
		usnprintf(detail, NUMBEROF(detail) - 1, L"Slayer, CTF, Oddball, King\r\nof the Hill, Race and yours.");
		break;
	}
	}
	headline[NUMBEROF(headline) - 1] = detail[NUMBEROF(detail) - 1] = 0;
	text_set(named(description, "multiplayer_status", 0), headline);
	/* (the touchscreen's panel is narrower than the lines broken here for
	the computer's: its words wrapped to it instead) */
	if (ui_widget_port_touch_menus())
	{
		short from, to = 0;

		for (from = 0; detail[from]; from++)
		{
			wchar_t c = detail[from] == '\r' || detail[from] == '\n' ? L' ' : detail[from];

			if (c != L' ' || (to && detail[to - 1] != L' '))
				detail[to++] = c;
		}
		detail[to] = 0;
		menu_text_box_wrap(named(description, "multiplayer_status_detail", 0), detail, 5);
	}
	else
		text_set(named(description, "multiplayer_status_detail", 0), detail);
}

/* "direct ip connect go" (Direct Link's PASTE LINK): the invite
link on the clipboard reached, its game then in the list */
static boolean direct_link_from_clipboard(void)
{
	char text[TEXT_FIELD_LENGTH];
	char *link = text;
	size_t length;

	if (!platform_clipboard_get(text, sizeof(text)))
		text[0] = 0;
	while (*link == ' ' || *link == '\t' || *link == '\r' || *link == '\n')
		link++;
	for (length = strlen(link); length && (link[length - 1] == ' ' || link[length - 1] == '\t' ||
		link[length - 1] == '\r' || link[length - 1] == '\n'); length--)
		link[length - 1] = 0;
	if (!*link || !p2p_join_invite(link))
	{
		/* (an older version's invite is logged as such: p2p.c) */
		platform_log("menus: the clipboard has no invite link this version joins");
		display_error_text_deferred(!*link ?
			L"No link is copied. Copy\r\nthe invite link you were\r\nsent, then PASTE LINK." :
			L"That link can't be joined.\r\nCopy the whole invite link\r\nagain, or ask the host for\r\na new one.", NONE);
		return campaign_fail();
	}
	connect_dialog.listing = FALSE;
	snprintf(connect_dialog.link, sizeof(connect_dialog.link), "%s", link);
	return TRUE;
}

/* joining the game chosen: player 1 in, then the lobby */
static boolean browser_join(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	if (multiplayer.game_chosen >= multiplayer.game_count)
		return campaign_fail();
	if (!multiplayer_player(controller))
		return FALSE;
	return advertised_join(widget, multiplayer.games[multiplayer.game_chosen], widget_deleted);
}

/* "port browser favorite" (Y on a row, F on the keyboard) and a click on a
row's star: the row's game (NONE: the one chosen) made a favorite, or no
longer one; its row keeps the focus as it moves */
static boolean browser_favorite_toggle(short row)
{
	boolean server_browser = multiplayer.mode == _multiplayer_mode_server_browser;
	short count = server_browser ? lobby_browser.count : multiplayer.game_count;
	struct browser_favorite game;
	short index;
	int starred;

	if (row == NONE)
		row = browser_chosen_row();
	if (row == NONE)
		return campaign_fail();
	index = server_browser ? (short)(lobby_browser.first + row) : row;
	if (index < count && server_browser)
		listing_favorite(&lobby_browser.games[index], &game);
	else if (index < count)
		advertised_favorite(multiplayer.games[index], &game);
	else if (index - count < browser_offline_count)
		game = *browser_favorites_get(browser_offline[index - count]);
	else
		return campaign_fail();
	starred = browser_favorites_toggle(&game);
	if (starred < 0)
		return campaign_fail();
	snprintf(browser_follow_key, sizeof(browser_follow_key), "%s", game.key);
	ui_play_audio_feedback_sound(starred ? SOUND_FORWARD : SOUND_BACK);
	return TRUE;
}

/* whether a click (the pointer's, or a tap) on a row was on its star */
static boolean browser_click_on_star(struct widget_instance *row)
{
	struct widget_instance *star = named(row, "server_item_favorite", 0);
	short x, y, left;

	if (!star || !ui_widget_port_take_click(&x, &y))
		return FALSE;
	left = widget_screen_x(star);
	/* (a little wider than the star, for a finger) */
	return x >= left - 4 && x < left + 20;
}

/* "port browser refresh" (the right trigger, R on the keyboard, and its
prompt): the server browser asks the public games' hosts again; Join by
Link's joins the invite link copied (its PASTE LINK); the local network's
list looks all the while */
static boolean browser_refresh(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	if (multiplayer.mode == _multiplayer_mode_direct_link)
		return direct_link_from_clipboard() && connect_open(widget, controller, widget_deleted);
	if (multiplayer.mode != _multiplayer_mode_server_browser)
		return TRUE;
	p2p_lobby_refresh();
	lobby_browser.begin_time = system_milliseconds();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "gamespy select item" (a row: chooses its game, or joins it once chosen)
and "gamespy select button" (Join) */
static boolean browser_select(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
{
	short row = browser_row_index(widget);

	if (row != NONE && !lobby_browser.ready && browser_click_on_star(widget))
		return browser_favorite_toggle(row);

	/* (a row not chosen, clicked after the pointer passed over it: chosen,
	as Custom Edition's; a click on it chosen, a double click, A or JOIN
	joins its game. Not the A press a join posts: lobby_browser_select) */
	if (row != NONE && row != browser_chosen_row() && !lobby_browser.ready)
	{
		if (multiplayer.mode == _multiplayer_mode_server_browser)
			lobby_browser.chosen = (short)(lobby_browser.first + row);
		else
			multiplayer.game_chosen = (short)(multiplayer.game_first + row);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return TRUE;
	}
	if (multiplayer.mode == _multiplayer_mode_server_browser)
		return lobby_browser_select(widget, controller, row, widget_deleted);
	if (row != NONE)
		multiplayer.game_chosen = (short)(multiplayer.game_first + row);
	if (row != NONE || strstr(widget->name, "button_join"))
	{
		/* (a game under way: its lobby first, then JOIN GAME) */
		if (multiplayer.game_chosen < multiplayer.game_count &&
			advertised_in_progress(multiplayer.games[multiplayer.game_chosen]))
		{
			/* (player 1 joining it: others join them there) */
			if (!multiplayer_player(controller))
				return FALSE;
			csmemcpy(multiplayer.preview_key_id, multiplayer.games[multiplayer.game_chosen]->key_id,
				sizeof(multiplayer.preview_key_id));
			csmemcpy(multiplayer.preview_xnaddr, multiplayer.games[multiplayer.game_chosen]->xnaddr,
				sizeof(multiplayer.preview_xnaddr));
			return ui_widget_port_open(widget, PREVIEW_NAME, widget_deleted);
		}
		return browser_join(widget, controller, widget_deleted);
	}
	return TRUE;
}

/* "player profile save changes" (Settings' OK, the profile being edited):
saved if it has changes, as the Xbox's (the saving screen follows); if it
has none (the settings' own screens write theirs to config.toml as their OK
is chosen), editing ends and the previous screen comes back, as CANCEL
(the Xbox's called that a failure, and closed every screen) */
static boolean profile_save_changes(struct widget_instance *widget, boolean *widget_deleted)
{
	if (player_ui_edit_profile_is_dirty())
	{
		if (player_ui_save_profile())
			return TRUE;
		platform_log("menus: could not save the profile's changes");
		return campaign_fail();
	}
	player_ui_end_editing_profile();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* ---------- Basic Training's chapters (port/linux/game/basic_training.c;
tools/port_settings.py's _training): the main menu's VR TUTORIAL, the
pause menu's CHAPTERS */

/* the chapter of a row (op_chapter_1 is chapter 0), or of the widget in
one; NONE */
static short training_row_chapter(struct widget_instance *widget)
{
	for (; widget; widget = widget->parent)
	{
		if (!strncmp(widget->name, "op_chapter_", 11))
			return (short)(atoi(widget->name + 11) - 1);
	}
	return NONE;
}

/* "port training chapter" (A on a row): the chapter played (from the main
menu, a training game started at it; in one, jumped to); a chapter not made
yet fails */
static boolean training_chapter_choose(struct widget_instance *widget)
{
	short chapter = training_row_chapter(widget);

	return chapter != NONE && basic_training_choose(chapter);
}

/* a narrow text as the menus' (Latin-1) */
static void training_wide(char const *text, wchar_t *wide, short length)
{
	short index;

	for (index = 0; index < length - 1 && text[index]; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
}

/* "port training update": each row's number and name, DONE for a chapter
finished, SOON for one not made yet (dimmed); the chapter chosen's name and
what it is about in the frame */
static void training_update(struct widget_instance *list)
{
	struct widget_instance *row;
	struct widget_instance *details = list->parameters.list.extended_description;
	short chosen = training_row_chapter(list->focused_child);

	for (row = list->child; row; row = row->next)
	{
		short chapter = training_row_chapter(row);
		wchar_t text[ROW_TEXT_LENGTH];
		char narrow[ROW_TEXT_LENGTH];

		if (chapter == NONE || strncmp(row->name, "op_chapter_", 11))
			continue;
		row->animation.current_frame_index = row == list->focused_child ? 1 : 0;
		snprintf(narrow, sizeof(narrow), "%d   %s", chapter, basic_training_chapter_name(chapter));
		training_wide(narrow, text, (short)NUMBEROF(text));
		text_set(named(row, "chapter_name", 0), text);
		text_set(named(row, "chapter_state", 0), !basic_training_chapter_ready(chapter) ? L"SOON" :
			basic_training_chapter_finished(chapter) ? L"DONE" : L"");
	}
	if (details && chosen != NONE)
	{
		wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];

		training_wide(basic_training_chapter_name(chosen), text, (short)NUMBEROF(text));
		menu_text_box_wrap(named(details, "chapter_details_name", 0), text, 2);
		/* (each of its lines wrapped to the box, a blank line between) */
		{
			struct widget_instance *box = named(details, "chapter_details_text", 0);
			char const *about = basic_training_chapter_about(chosen);
			wchar_t all[SETTINGS_PANEL_TEXT_LENGTH];
			short width, used = 0;
			long font = ui_widget_port_text_box_font(box, &width);

			all[0] = 0;
			while (*about && used < (short)NUMBEROF(all) - 8)
			{
				char line[SETTINGS_PANEL_TEXT_LENGTH];
				char const *end = strchr(about, '\n');
				size_t count = end ? (size_t)(end - about) : strlen(about);
				wchar_t wrapped[SETTINGS_PANEL_TEXT_LENGTH];
				short index;

				snprintf(line, sizeof(line), "%.*s", (int)count, about);
				training_wide(line, text, (short)NUMBEROF(text));
				settings_text_wrap(text, wrapped, (short)NUMBEROF(wrapped), font, width);
				if (used)
				{
					all[used++] = '\r';
					all[used++] = '\n';
					all[used++] = '\r';
					all[used++] = '\n';
				}
				for (index = 0; wrapped[index] && used < (short)NUMBEROF(all) - 1; index++)
					all[used++] = wrapped[index];
				all[used] = 0;
				about += count;
				if (*about == '\n')
					about++;
			}
			text_set_length(box, all, (short)NUMBEROF(all));
		}
	}
}

/* "port pause end game" (the in-game pause menu's END GAME, the host's:
menu_tags.c's pause_patch): the game ends as its time limit would, its
players staying for the next (the carnage report, then the host's PICK GAME) */
static boolean pause_end_game(void)
{
	if (!global_network_game_server_get() || !game_engine_running())
		return campaign_fail();
	game_engine_end_game();
	return TRUE;
}

/* "port coop friendly fire" (network co-op's pause menu's FRIENDLY FIRE,
menu_tags.c): switches this machine's own friendly fire, from now on */
static boolean coop_friendly_fire_change(void)
{
	char const *choice = config_boolean("network.coop_own_friendly_fire") ? "false" : "true";

	if (!config_write("network.coop_own_friendly_fire", choice))
		platform_log("menus: could not write network.coop_own_friendly_fire to config.toml");
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port coop friendly fire text": the button's text, ON or OFF */
static void coop_friendly_fire_text(struct widget_instance *button)
{
	button->parameters.text_box.string_list_index = config_boolean("network.coop_own_friendly_fire") ? 0 : 1;
}

/* ---- the lobby: the game's players (up to the port's 128), its map and
gametype, the countdown */

/* (the lobby's players, as lobby_menu.inc last counted them) */
static short lobby_player_count;

/* split screen: up to 4 players on this machine. In the lobby, a controller
not playing presses START to join ("port lobby join"), then chooses a
profile (its screen, "port lobby player choose"); ADD PLAYER ("port lobby
add player") gives one gamepad its own controller first (as it shares
player 1's: pc_menu_split_players). A player's B leaves the game alone ("port
lobby leave"; the machine's last leaves it). In game, a player's pause menu's
QUIT is theirs (the Xbox's "mp game player quit") */
static struct
{
	/* ADD PLAYER chosen: one gamepad is the new player's */
	boolean adding;
	/* the controller choosing its profile, NONE if none */
	short controller;
} lobby_join = { FALSE, NONE };

/* the client's player of the controller on this machine, NULL if none */
static struct network_player *lobby_local_player(short controller)
{
	void *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short machine_index = network_game_client_get_local_machine_index();
	short index;

	for (index = 0; game && machine_index != NONE && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		struct network_player *player = &game->players[index];

		if (network_player_is_valid(player) && (short)player->machine_index == machine_index &&
			(short)player->controller_index == controller)
		{
			return player;
		}
	}
	return NULL;
}

/* whether the controller plays (or is to: joined, not yet added) */
static boolean lobby_controller_playing(short controller)
{
	return player_ui_local_player_wants_to_play_multiplayer(controller) || lobby_local_player(controller);
}

/* this machine's players in the network game (or to be) */
static short lobby_local_player_count(void)
{
	short controller, count = 0;

	if (!global_network_game_client_get())
		return 0;
	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
		count += lobby_controller_playing(controller) ? 1 : 0;
	return count;
}

/* (xinput_sdl.c) whether this machine has, or is adding, a second player: co-op,
or split screen in a network game (a player who quit keeps their part of the
screen until the game ends). Then one gamepad is its own controller, not
sharing player 1's with the keyboard */
unsigned char pc_menu_split_players(void)
{
	return player_spawn_count >= 2 || lobby_join.adding || lobby_join.controller != NONE ||
		lobby_local_player_count() >= 2 || players_port_local_player_count() >= 2;
}

/* the lobby's widget that has the focus (the one a press goes to) */
static struct widget_instance *focused_leaf(struct widget_instance *widget)
{
	while (widget->parent)
		widget = widget->parent;
	while (widget->focused_child)
		widget = widget->focused_child;
	return widget;
}

/* "port lobby open" (the lobby made, or come back to): no player being added */
static boolean lobby_join_reset(void)
{
	lobby_join.adding = FALSE;
	lobby_join.controller = NONE;
	return TRUE;
}

/* "port lobby add player" (ADD PLAYER): the next START of another controller
joins (one gamepad leaves the keyboard's controller for its own) */
static boolean lobby_add_player(void)
{
	if (lobby_local_player_count() >= MAXIMUM_LOCAL_PLAYERS)
		return campaign_fail();
	lobby_join.adding = TRUE;
	return TRUE;
}

/* "port lobby join" (START): a controller not playing joins, choosing its
profile next (FALSE: no profile screen); a player's START is the focused
button's */
static boolean lobby_join_start(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	void *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short state_data;

	if (lobby_controller_playing(controller))
	{
		ui_widget_port_dispatch_event(focused_leaf(widget), BUTTON_START, controller, widget_deleted);
		return FALSE;
	}
	if (!game || network_game_client_get_state(client, &state_data) != _client_state_pregame)
		return campaign_fail();
	if (lobby_player_count >= game->maximum_players)
	{
		display_error_text_deferred(L"The game is full: there's\r\nno room for another\r\nplayer.", NONE);
		return campaign_fail();
	}
	lobby_join.controller = controller;
	return TRUE;
}

/* "port lobby leave" (B): a player leaves the game, the machine's last
leaving it (TRUE: back from the lobby); a controller not playing cancels
ADD PLAYER */
static boolean lobby_leave(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
{
	if (!lobby_controller_playing(controller))
	{
		lobby_join_reset();
		return FALSE;
	}
	return ui_widget_port_unjoin_player(widget, event, widget_deleted);
}

/* "port lobby player list initialize": on the joining controller's profile,
else the first no other player of this machine has */
static boolean lobby_player_list_initialize(struct widget_instance *list)
{
	long mine = lobby_join.controller != NONE ? player_ui_get_active_player_profile_index(lobby_join.controller) : NONE;
	short index, controller;

	profile_list_read(TRUE);
	profile_list.chosen = 0;
	for (index = profile_list.count - 1; index >= 0; index--)
	{
		boolean taken = FALSE;

		for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
		{
			taken |= controller != lobby_join.controller && lobby_controller_playing(controller) &&
				player_ui_get_active_player_profile_index(controller) == profile_list.indices[index];
		}
		if (!taken)
			profile_list.chosen = index;
	}
	for (index = 0; index < profile_list.count && mine != NONE; index++)
	{
		if (profile_list.indices[index] == mine)
			profile_list.chosen = index;
	}
	profile_list.first = (short)PIN(profile_list.chosen - PROFILE_ROWS / 2, 0,
		MAX(0, profile_list.count + 1 - PROFILE_ROWS));
	focus_row(list, (short)(profile_list.chosen - profile_list.first));
	return TRUE;
}

/* "port lobby player choose": the joining controller's player on the profile
chosen (the lobby's "net splitscreen prejoin players" adds them) */
static boolean lobby_player_choose(void)
{
	short controller = lobby_join.controller;

	if (controller == NONE || profile_list.chosen >= profile_list.count ||
		!ui_widget_port_multiplayer_player(controller, profile_list.indices[profile_list.chosen]))
	{
		return campaign_fail();
	}
	return TRUE;
}

/* the lobby's line under its players: how another player joins */
static void lobby_join_help(struct widget_instance *list)
{
	struct widget_instance *help = named(screen_of(list), "lobby_join_help", 0);
	wchar_t const *text = L"";
	short controller;

	if (lobby_join.adding)
	{
		text = L"New player: press START.";
	}
	else if (lobby_local_player_count() < MAXIMUM_LOCAL_PLAYERS)
	{
		for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
		{
			if (input_has_gamepad(controller) && !lobby_controller_playing(controller))
				text = L"Another controller: START joins.";
		}
	}
	text_set(help, text);
}

/* the lobby's panel's map: its picture and name */
static void lobby_map_show(struct widget_instance *description, char const *map_name)
{
	char const *const *names;
	short count = xbox_multiplayer_map_count(ui_widget_port_multiplayer_maps(&names, NULL)), map = 19, index;
	short level = campaign_level_of(map_name);
	struct widget_instance *widget;

	/* (by the scenario's own name: a map vote's choices name only that) */
	for (index = 0; index < count; index++)
	{
		if (!_stricmp(scenario_name(names[index]), scenario_name(map_name)))
			map = index;
	}
	/* (a Custom Edition map's picture and name, multiplayer or campaign, by
	its display index, if this machine has it: custom_edition_maps.c) */
	if (custom_edition_level_name(map_name) && custom_edition_maps_display_index(map_name) != NONE)
		map = custom_edition_maps_display_index(map_name);
	/* (a network co-op game's level: its picture and name, sp_levels' and
	map_list's, in the place of the map's) */
	visible_set(named(description, "lobby_map_pic", 0), level == NONE);
	visible_set(named(description, "lobby_map_name", 0), level == NONE);
	visible_set(named(description, "replay_level_right_pic", 0), level != NONE);
	visible_set(named(description, "replay_level_right_name", 0), level != NONE);
	if ((widget = named(description, "lobby_map_pic", 0)) != NULL)
		widget->animation.current_frame_index = map;
	if ((widget = named(description, "lobby_map_name", 0)) != NULL)
		widget->parameters.text_box.string_list_index = map;
	if (level == NONE)
		return;
	if ((widget = named(description, "replay_level_right_pic", 0)) != NULL)
		widget->animation.current_frame_index = level;
	if ((widget = named(description, "replay_level_right_name", 0)) != NULL)
		widget->parameters.text_box.string_list_index = level;
}

/* ---- a dedicated server's map vote (network_map_vote.c, OpenCE's screen):
the lobby opens its screen as a vote begins (a posted Y: the lobby's Y opens
it again while the vote runs, and copies the invite otherwise), the choices a
row each with their votes, the focused one's map and the time left at the
right; A votes (again, for another). Once the vote is over it shows the
winner a moment, and goes back to the lobby */

/* the winner shown this long before the screen goes back */
#define MAP_VOTE_WINNER_MILLISECONDS 3000

static struct
{
	/* the vote whose screen the lobby opened, and the one it closed for */
	short opened_round;
	short closed_round;
	/* when the winner was first shown (0: not yet) */
	unsigned long winner_at;
	short winner_round;
	struct network_map_vote_status status;
} map_vote_screen = { -1, -1 };

/* (the lobby's update: lobby_board_update) a vote begun since: its screen
opened */
static void map_vote_lobby_check(void)
{
	struct network_map_vote_status status;

	if (network_map_vote_status_get(&status) && status.winner == NONE && status.round != map_vote_screen.opened_round)
	{
		map_vote_screen.opened_round = status.round;
		ui_widget_port_post_button(0, BUTTON_Y);
	}
}

/* "port map vote open" (the lobby's Y): only while a vote runs */
static boolean map_vote_open(void)
{
	struct network_map_vote_status status;

	return network_map_vote_status_get(&status) && status.winner == NONE;
}

/* "port map vote choose" (a row's A) */
static boolean map_vote_choose(struct widget_instance *widget)
{
	if (!network_map_vote_choose(sibling_index(widget)))
		return campaign_fail();
	return TRUE;
}

/* a choice's gametype, upper case */
static void map_vote_gametype(short row, wchar_t *gametype)
{
	struct network_map_vote_status const *status = &map_vote_screen.status;
	short index;

	for (index = 0; status->gametypes[row][index] && index < NETWORK_MAP_VOTE_GAMETYPE_SIZE - 1; index++)
	{
		char character = status->gametypes[row][index];

		gametype[index] = (wchar_t)(unsigned char)(character >= 'a' && character <= 'z' ? character - 'a' + 'A' :
			character);
	}
	gametype[index] = 0;
}

static void map_vote_row_text(short row, wchar_t *text)
{
	struct network_map_vote_status const *status = &map_vote_screen.status;
	wchar_t map[ROW_TEXT_LENGTH];
	wchar_t gametype[NETWORK_MAP_VOTE_GAMETYPE_SIZE];

	map_display_name(status->map_names[row], map);
	map_vote_gametype(row, gametype);
	usnprintf(text, ROW_TEXT_LENGTH - 1, L"%s%s  %s  (%d)", row == status->chosen ? L"* " : L"", map, gametype,
		status->votes[row]);
	text[ROW_TEXT_LENGTH - 1] = 0;
}

/* "port map vote update" */
static void map_vote_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct network_map_vote_status *status = &map_vote_screen.status;
	wchar_t text[LOBBY_TEXT_LENGTH];
	wchar_t name[ROW_TEXT_LENGTH];
	short row;
	boolean heard = network_map_vote_status_get(status);
	unsigned long now = system_milliseconds();

	/* (no vote, or its winner shown long enough: back to the lobby, once) */
	if (heard && status->winner != NONE && map_vote_screen.winner_round != status->round)
	{
		map_vote_screen.winner_round = status->round;
		map_vote_screen.winner_at = now;
	}
	if (!heard || (status->winner != NONE && now - map_vote_screen.winner_at >= MAP_VOTE_WINNER_MILLISECONDS))
	{
		if (map_vote_screen.closed_round != (heard ? status->round : map_vote_screen.opened_round))
		{
			map_vote_screen.closed_round = heard ? status->round : map_vote_screen.opened_round;
			ui_widget_port_post_button(0, BUTTON_B);
		}
		if (!heard)
		{
			rows_update(list, 0, map_vote_row_text);
			visible_set(named(description, "lobby_right_item", 0), FALSE);
			return;
		}
	}
	rows_update(list, status->count, map_vote_row_text);
	row = focused_row(list);
	if (status->winner != NONE)
		row = status->winner;
	else if (row == NONE || row >= status->count)
		row = status->chosen != NONE ? status->chosen : 0;
	visible_set(named(description, "lobby_right_item", 0), TRUE);
	lobby_map_show(description, status->map_names[row]);
	if (status->winner != NONE)
	{
		map_display_name(status->map_names[status->winner], name);
		usnprintf(text, NUMBEROF(text) - 1, L"Next game:\r\n%s\r\n%d vote%s", name, status->votes[status->winner],
			status->votes[status->winner] == 1 ? L"" : L"s");
	}
	else
	{
		wchar_t gametype[NETWORK_MAP_VOTE_GAMETYPE_SIZE];

		map_vote_gametype(row, gametype);
		usnprintf(text, NUMBEROF(text) - 1, L"%s\r\n%d vote%s\r\n\r\n%d s left\r\n\r\n%s", gametype,
			status->votes[row], status->votes[row] == 1 ? L"" : L"s", status->seconds_left,
			status->chosen == NONE ? L"A: vote" : row == status->chosen ? L"Your vote" : L"A: change vote");
	}
	text[NUMBEROF(text) - 1] = 0;
	text_set_length(named(description, "lobby_game_data", 0), text, LOBBY_TEXT_LENGTH);
	profile_name_show(description);
}

/* the lobby's rows' texts as last updated, and their players' machines,
for their speaker icons (menu_functions_text_box_drawn); and when */
static struct
{
	struct widget_instance *texts[LOBBY_ICON_ROWS];
	long machines[LOBBY_ICON_ROWS];
	short count;
	unsigned long updated_at;
} lobby_icons;

/* ui_widget.c's: a text box drawn, at the bounds its text was drawn in
(its font and justification still set): a lobby row's player talking in
voice chat (or muted) has a speaker just left of their name
(port/linux/game/network_voice.c) */
void menu_functions_text_box_drawn(struct widget_instance *widget, rectangle2d const *bounds)
{
	short row;

	/* (as the lobby last updated them: the rows' widgets go with it) */
	if (!lobby_icons.count || system_milliseconds() - lobby_icons.updated_at > 250)
		return;
	for (row = 0; row < lobby_icons.count; row++)
	{
		long machine_index = lobby_icons.machines[row];
		rectangle2d icon;
		short size;

		if (lobby_icons.texts[row] != widget)
			continue;
		if (!network_voice_machine_speaking(machine_index) && !network_voice_machine_muted(machine_index))
			return;
		size = (short)MIN(14, bounds->y1 - bounds->y0);
		icon.x1 = (short)(bounds->x1 - 4);
		icon.y0 = (short)((bounds->y0 + bounds->y1 - size) / 2);
		/* (the name's ink: the icon just left of it, centred on its
		capitals) */
		if (widget->parameters.text_box.text && widget->parameters.text_box.text[0])
		{
			rectangle2d text;
			rectangle2d cursor;

			draw_unicode_string_compute_bounds(bounds, widget->parameters.text_box.text, &text, &cursor);
			if (text.x0 - size - 6 >= bounds->x0)
				icon.x1 = (short)(text.x0 - 6);
			icon.y0 = (short)(draw_unicode_string_capital_middle(bounds, widget->parameters.text_box.text) - size / 2);
		}
		icon.x0 = (short)(icon.x1 - size);
		icon.y1 = (short)(icon.y0 + size);
		network_voice_draw_icon(&icon, network_voice_machine_muted(machine_index), 1.0f);
		return;
	}
}

/* ---- an in-progress game's lobby, before joining it (Direct Link and
LAN's rows of games under way): what its advertisement tells (no players'
names: they come with joining), JOIN GAME. Split screen players join here
as in the lobby (lobby_join), for the game starts at once for a machine
that joins it: player 1 as it opens, the others with START */

/* the previewed game, found again among the client's (NULL: gone) */
static struct advertised_game *preview_game(void)
{
	void *client = global_network_game_client_get();
	struct advertised_game *games = client ? network_game_client_get_available_games(client) : NULL;
	short index;

	for (index = 0; games && index < MAXIMUM_ADVERTISED_GAMES; index++)
	{
		if (network_game_client_advertised_game_is_valid(&games[index]) &&
			!csmemcmp(games[index].key_id, multiplayer.preview_key_id, sizeof(multiplayer.preview_key_id)) &&
			!csmemcmp(games[index].xnaddr, multiplayer.preview_xnaddr, sizeof(multiplayer.preview_xnaddr)))
		{
			return &games[index];
		}
	}
	return NULL;
}

/* the preview's status's end: the players joining from here, when there
are more than one (split screen) */
static void preview_players_text(wchar_t *text, short size)
{
	short controller;
	size_t length;

	if (lobby_local_player_count() < 2)
		return;
	length = ustrlen(text);
	usnprintf(text + length, size - 1 - length, L"\r\n\r\nJoining from here:");
	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
	{
		struct player_profile profile;
		wchar_t name[NUMBEROF(profile.player_name) + 1];

		if (!player_ui_local_player_wants_to_play_multiplayer(controller))
			continue;
		player_ui_get_active_player_profile(controller, &profile);
		ustrncpy(name, profile.player_name, NUMBEROF(profile.player_name));
		name[NUMBEROF(profile.player_name)] = 0;
		text[size - 1] = 0;
		length = ustrlen(text);
		usnprintf(text + length, size - 1 - length, L"\r\n%s  [P%d]", name, controller + 1);
	}
	text[size - 1] = 0;
}

/* "port lobby preview update" */
static void preview_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct advertised_game *game = preview_game();
	boolean valid = game != NULL;
	wchar_t text[LOBBY_TEXT_LENGTH], name[NUMBEROF(game->game_name) + 1];

	visible_set(named(description, "lobby_right_item", 0), valid);
	if (valid)
	{
		lobby_map_show(description, game->map_name);
		usnprintf(text, NUMBEROF(text) - 1, L"%s\r\n%d of %d players\r\non %d machines",
			game_kind_name(game->engine_type, advertised_game_kind(game), FALSE), game->player_count,
			game->maximum_player_count, game->machine_count);
		text[NUMBEROF(text) - 1] = 0;
		text_set_length(named(description, "lobby_game_data", 0), text, LOBBY_TEXT_LENGTH);
		ustrncpy(name, game->game_name, NUMBEROF(game->game_name));
		name[NUMBEROF(game->game_name)] = 0;
		/* (the text box does not wrap: lines of up to 28 characters) */
		usnprintf(text, NUMBEROF(text) - 1, L"%s\r\n\r\nThis game is under way.\r\n\r\n%s", name,
			game->open ? L"JOIN GAME joins it now;\r\nits players show then." :
			L"It cannot be joined now:\r\nit is loading, over or full.");
		text[NUMBEROF(text) - 1] = 0;
		preview_players_text(text, NUMBEROF(text));
	}
	else
		usnprintf(text, NUMBEROF(text) - 1, L"The game is gone.");
	text[NUMBEROF(text) - 1] = 0;
	text_set_length(named(list, "preview_status", 0), text, LOBBY_TEXT_LENGTH);
	lobby_join_help(list);
	profile_name_show(description);
}

/* "port lobby preview add" (START): as the lobby's ("port lobby join"), a
controller not joining chooses its profile, to join with the others; a
joining player's START is the focused button's */
static boolean preview_add(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	struct advertised_game *game = preview_game();
	short count = lobby_local_player_count();

	if (lobby_controller_playing(controller))
	{
		ui_widget_port_dispatch_event(focused_leaf(widget), BUTTON_START, controller, widget_deleted);
		return FALSE;
	}
	if (!game || !game->open || count >= MAXIMUM_LOCAL_PLAYERS)
		return campaign_fail();
	if (game->player_count + count >= game->maximum_player_count)
	{
		display_error_text_deferred(L"The game is full: there's\r\nno room for another\r\nplayer.", NONE);
		return campaign_fail();
	}
	lobby_join.controller = controller;
	return TRUE;
}

/* "port lobby preview leave" (B): a joining player stays out, the last of
them backing out (TRUE); a controller not joining cancels ADD PLAYER, else
backs out for them all (the game is not joined yet: none is kept here) */
static boolean preview_leave(short controller)
{
	short index;

	if (!lobby_controller_playing(controller))
	{
		if (lobby_join.adding)
		{
			lobby_join_reset();
			return FALSE;
		}
		for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
			player_ui_local_player_left_multiplayer_game(index);
		return TRUE;
	}
	player_ui_local_player_left_multiplayer_game(controller);
	return lobby_local_player_count() == 0;
}

/* "port lobby preview join": with the players joining from here (player 1,
as the preview opened, and those added), whom the lobby asks the host for
all at once ("net splitscreen prejoin players"): it starts the machine in
the game once it has them all */
static boolean preview_join(struct widget_instance *widget, short controller, boolean *widget_deleted)
{
	struct advertised_game *game = preview_game();

	if (!game || !game->open)
		return campaign_fail();
	if (!lobby_local_player_count() && !multiplayer_player(controller))
		return FALSE;
	return advertised_join(widget, game, widget_deleted);
}

/* ---- the gametype editor: Edit Gametypes' list (the built-in gametypes
and those saved), the gametype being edited (player_ui's) or Server Setup's
(a copy of the game's gametype), its options' screens: each option a
spinner, found by its name (the PC version's screens put them in another
order than the Xbox's, whose functions of the same names walk them by
place) */

boolean ui_widget_port_gametype_edit_begin(long profile_index);
boolean ui_widget_port_gametype_save(struct widget_instance *widget, boolean *widget_deleted);
boolean ui_widget_port_game_variant_set(struct game_variant *variant, struct game_variant_options const *options);
boolean ui_widget_port_gametype_delete(long profile_index);

#define GAMETYPE_EDIT_ROWS 11

static struct
{
	/* the list's */
	long gametypes[MAXIMUM_GAMETYPES];
	short count, first, chosen;
	boolean stale;
	long deleting;
	/* Server Setup's copy of the game's gametype, edited in place of
	player_ui's (setup) */
	boolean setup;
	struct game_variant setup_variant;
	struct game_variant_options setup_options;
	/* the vehicle screen's side shown */
	short vehicle_side;
} gametype_edit = { { 0 }, 0, 0, 0, TRUE, NONE, FALSE };

static struct game_variant *edit_variant(void)
{
	return gametype_edit.setup ? &gametype_edit.setup_variant : player_ui_get_edit_playlist_profile();
}

static struct game_variant_options *edit_options(void)
{
	return gametype_edit.setup ? &gametype_edit.setup_options : player_ui_get_edit_playlist_options();
}

enum
{
	_option_long,		/* a long of the variant */
	_option_byte,		/* a byte of the variant */
	_option_flag,		/* a bit of the variant's flags (argument), set by value 1 */
	_option_health,		/* the variant's health, tenths */
	_option_short,		/* a short of the options */
	_option_option_byte,	/* a byte of the options */
	_option_radar,		/* the options' radar players, and the variant's flag */
	_option_setting,	/* the host's own (config.toml): Server Setup's copy only */
	_option_byte_value	/* a byte of the variant, its number (not on or off) */
};

struct gametype_option
{
	char const *spinner;
	short kind;
	short offset;
	unsigned long argument;
	short count;
	long values[16];
	/* (_option_setting: the setting, and its values' text) */
	char const *setting;
	char const *setting_values[16];
};

#define VARIANT_FIELD(field) (short)offsetof(struct game_variant, field)
#define OPTIONS_FIELD(field) (short)offsetof(struct game_variant_options, field)
#define TIME_LIMITS { 0, 10, 15, 20, 25, 30, 45 }

static struct gametype_option const gametype_options[] =
{
	/* OPENCE OPTIONS (OpenCE's gametypes: game_modes.c) */
	{ "oce_mode_spinner", _option_option_byte, OPTIONS_FIELD(mode), 0, 4,
		{ _game_mode_none, _game_mode_swat, _game_mode_infection, _game_mode_gun_game } },
	{ "oce_hardcore_spinner", _option_option_byte, OPTIONS_FIELD(hardcore), 0, 2, { FALSE, TRUE } },
	{ "inf_zombies_spinner", _option_option_byte, OPTIONS_FIELD(infection_initial_zombies), 0, 6, { 1, 2, 3, 4, 8, 16 } },
	{ "inf_speed_spinner", _option_option_byte, OPTIONS_FIELD(infection_zombie_speed), 0, 6,
		{ 100, 110, 125, 150, 175, 200 } },
	{ "inf_damage_spinner", _option_option_byte, OPTIONS_FIELD(infection_skull_damage), 0, 4, { 100, 150, 200, 250 } },
	{ "inf_respawn_spinner", _option_option_byte, OPTIONS_FIELD(infection_respawn_time), 0, 6, { 1, 3, 5, 10, 15, 30 } },
	{ "inf_countdown_spinner", _option_option_byte, OPTIONS_FIELD(infection_countdown), 0, 5, { 0, 5, 10, 15, 30 } },
	{ "inf_shields_spinner", _option_option_byte, OPTIONS_FIELD(infection_zombie_shields), 0, 2, { FALSE, TRUE } },
	{ "gg_order_spinner", _option_option_byte, OPTIONS_FIELD(gun_game_order), 0, 3,
		{ _gun_game_order_strongest_first, _gun_game_order_weakest_first, _gun_game_order_random } },
	{ "gg_melee_spinner", _option_option_byte, OPTIONS_FIELD(gun_game_melee_demotes), 0, 2, { FALSE, TRUE } },
	/* player options */
	{ "number_of_lives_spinner", _option_long, VARIANT_FIELD(universal_variant.lives), 0, 4, { 0, 1, 3, 5 } },
	{ "maximum_health_spinner", _option_health, 0, 0, 6, { 5, 10, 15, 20, 30, 40 } },
	{ "shields_spinner", _option_flag, 0, FLAG(_game_variant_no_shields_bit), 2, { 0, 1 } },
	{ "respawn_time_spinner", _option_long, VARIANT_FIELD(universal_variant.respawn_time), 0, 4, { 0, 150, 300, 450 } },
	{ "respawn_time_growth_spinner", _option_long, VARIANT_FIELD(universal_variant.respawn_time_growth), 0, 4,
		{ 0, 150, 300, 450 } },
	{ "odd_man_out_spinner", _option_byte, VARIANT_FIELD(universal_variant.odd_man_out), 0, 2, { 1, 0 } },
	{ "invisible_players_spinner", _option_flag, 0, FLAG(_game_variant_always_invisible_bit), 2, { 1, 0 } },
	{ "suicide_penalty_spinner", _option_long, VARIANT_FIELD(universal_variant.suicide_penalty), 0, 4,
		{ 0, 150, 300, 450 } },
	/* item options (weapon sets: the PC's list, then the Xbox's NO GRENADES) */
	{ "item_options_infinite_grenades_spinner", _option_flag, 0, FLAG(_game_variant_infinite_grenades_bit), 2,
		{ 1, 0 } },
	{ "item_options_weapon_set_spinner", _option_long, VARIANT_FIELD(universal_variant.weapon_set), 0, 14,
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 10 } },
	{ "item_options_starting_equipment_spinner", _option_flag, 0, FLAG(_game_variant_generic_starting_equipment_bit),
		2, { 0, 1 } },
	{ "map_weapons_spinner", _option_option_byte, OPTIONS_FIELD(no_map_weapons), 0, 2, { 0, 1 } },
	/* (the loadout: the weapon set's, or each player's two weapons) */
	{ "loadout_spinner", _option_option_byte, OPTIONS_FIELD(loadout), 0, 2, { _loadout_category, _loadout_custom } },
	{ "primary_weapon_spinner", _option_option_byte, OPTIONS_FIELD(primary_weapon), 0, NUMBER_OF_LOADOUT_WEAPONS,
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } },
	{ "secondary_weapon_spinner", _option_option_byte, OPTIONS_FIELD(secondary_weapon), 0, NUMBER_OF_LOADOUT_WEAPONS,
		{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } },
	/* indicator options */
	{ "indicator_options_radar display_spinner", _option_long, VARIANT_FIELD(universal_variant.goal_radar), 0, 3,
		{ 0, 1, 2 } },
	{ "indicator_options_players_on_radar_spinner", _option_radar, OPTIONS_FIELD(radar_players), 0, 3,
		{ _radar_players_all, _radar_players_friends, _radar_players_none } },
	{ "indicator_options_friends_on_screen_spinner", _option_flag, 0, FLAG(_game_variant_allow_friendly_navpoints_bit),
		2, { 1, 0 } },
	/* capture the flag */
	{ "assault_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.ctf.assault), 0, 2, { 1, 0 } },
	{ "single_flag_spinner", _option_long, VARIANT_FIELD(game_engine_variant.ctf.single_flag_time), 0, 6,
		{ 0, 1800, 3600, 5400, 9000, 18000 } },
	{ "flag_must_reset_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.ctf.flag_must_reset), 0, 2,
		{ 1, 0 } },
	{ "flag_at_home_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.ctf.flag_at_home_to_score), 0, 2,
		{ 1, 0 } },
	{ "captures_to_win_spinner", _option_long, VARIANT_FIELD(universal_variant.score_to_win), 0, 5,
		{ 1, 3, 5, 10, 15 } },
	/* (port: kills score too, and a capture is worth its points: game_engine_ctf.c) */
	{ "kills_score_spinner", _option_long, VARIANT_FIELD(game_engine_variant.ctf.kills_score), 0, 2, { 1, 0 } },
	{ "kills_per_capture_spinner", _option_long, VARIANT_FIELD(game_engine_variant.ctf.points_per_capture), 0, 10,
		{ 5, 10, 15, 20, 25, 30, 40, 50, 75, 100 } },
	{ "time_limit_spinner", _option_short, OPTIONS_FIELD(time_limit), 0, 7, TIME_LIMITS },
	/* king of the hill */
	{ "koth_moving_hill_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.king.moving_hill), 0, 2, { 1, 0 } },
	/* (port: Multi-Hill's hills at once, game_engine_king.c) */
	{ "koth_multi_hill_spinner", _option_byte_value, VARIANT_FIELD(game_engine_variant.king.multi_hill), 0, 5,
		{ 0, 2, 3, 4, 5 } },
	{ "koth_score_to_win_spinner", _option_long, VARIANT_FIELD(universal_variant.score_to_win), 0, 5,
		{ 1, 2, 5, 10, 15 } },
	{ "koth_team_play_spinner", _option_byte, VARIANT_FIELD(universal_variant.teams), 0, 2, { 1, 0 } },
	/* oddball */
	{ "trait_with_ball_spinner", _option_long, VARIANT_FIELD(game_engine_variant.oddball.trait_with_ball), 0, 4,
		{ 0, 1, 2, 3 } },
	{ "trait_without_ball_spinner", _option_long, VARIANT_FIELD(game_engine_variant.oddball.trait_without_ball), 0, 4,
		{ 0, 1, 2, 3 } },
	{ "speed_with_ball_spinner", _option_long, VARIANT_FIELD(game_engine_variant.oddball.speed_with_ball), 0, 3,
		{ 1, 0, 2 } },
	{ "ball_type_spinner", _option_long, VARIANT_FIELD(game_engine_variant.oddball.oddball_ball_type), 0, 3,
		{ 0, 1, 2 } },
	{ "random_start_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.oddball.random_start), 0, 2, { 1, 0 } },
	{ "ball_spawn_count_spinner", _option_long, VARIANT_FIELD(game_engine_variant.oddball.ball_spawn_count), 0, 16,
		{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 } },
	{ "score_to_win_spinner", _option_long, VARIANT_FIELD(universal_variant.score_to_win), 0, 5, { 1, 2, 5, 10, 15 } },
	/* (Slayer's, Oddball's and Race's) */
	{ "team_play_spinner", _option_byte, VARIANT_FIELD(universal_variant.teams), 0, 2, { 1, 0 } },
	/* race */
	{ "team_scoring_spinner", _option_long, VARIANT_FIELD(game_engine_variant.race.team_scoring), 0, 3, { 0, 1, 2 } },
	{ "race_type_spinner", _option_long, VARIANT_FIELD(game_engine_variant.race.race_type), 0, 3, { 0, 1, 2 } },
	{ "laps_to_win_spinner", _option_long, VARIANT_FIELD(universal_variant.score_to_win), 0, 6,
		{ 1, 3, 5, 10, 15, 25 } },
	/* slayer (the PC's rows' names and labels are crossed: kill_penalty's
	row is labelled KILL IN ORDER, kill_in_order's KILL PENALTY; each sets
	what its label says) */
	{ "death_bonus_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.slayer.no_death_bonus), 0, 2, { 0, 1 } },
	{ "kill_penalty_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.slayer.kill_in_order), 0, 2, { 1, 0 } },
	{ "kill_in_order_spinner", _option_byte, VARIANT_FIELD(game_engine_variant.slayer.no_kill_penalty), 0, 2,
		{ 0, 1 } },
	/* (up to 500, for big games: tools/port_settings.py's STRING_INSERTS) */
	{ "kills_to_win_spinner", _option_long, VARIANT_FIELD(universal_variant.score_to_win), 0, 11,
		{ 5, 10, 15, 25, 50, 75, 100, 150, 200, 250, 500 } },
	/* team options */
	{ "friendly_fire_spinner", _option_short, OPTIONS_FIELD(friendly_fire), 0, 4,
		{ _friendly_fire_off, _friendly_fire_on, _friendly_fire_shields_only, _friendly_fire_explosives_only } },
	{ "friendly_fire_penalty_spinner", _option_short, OPTIONS_FIELD(friendly_fire_penalty), 0, 4, { 0, 5, 10, 15 } },
	{ "autobalance_spinner", _option_option_byte, OPTIONS_FIELD(auto_team_balance), 0, 2, { 0, 1 } },
	/* (port: spawns as CTF's, game_engine.c; the first of
	tools/port_settings.py's TEAMPLAY_ROWS, the gametype's own) */
	{ "force_team_spawns_spinner", _option_option_byte, OPTIONS_FIELD(force_team_spawns), 0, 2, { 0, 1 } },
	/* (the host's voice chat, below them: port/linux/game/network_voice.c;
	tools/port_settings.py's TEAMPLAY_ROWS) */
	{ "voice_mode_spinner", _option_setting, 0, 0, 5, { 0 }, "network.voice_mode",
		{ "off", "team_proximity", "team_enemy_proximity", "team_global", "team_global_enemy_proximity" } },
	{ "voice_lobby_spinner", _option_setting, 0, 0, 2, { 0 }, "network.voice_lobby", { "true", "false" } },
	{ "voice_kbps_spinner", _option_setting, 0, 0, 7, { 0 }, "network.voice_kbps",
		{ "8", "12", "16", "24", "32", "48", "64" } },
	{ "voice_proximity_spinner", _option_setting, 0, 0, 6, { 0 }, "network.voice_proximity",
		{ "5", "10", "15", "20", "30", "50" } },
	/* (and the players' votes to kick: port/linux/game/network_votekick.c) */
	{ "votekick_spinner", _option_setting, 0, 0, 2, { 0 }, "network.votekick", { "true", "false" } },
	/* (and the text chat: port/linux/game/network_chat.c) */
	{ "text_chat_spinner", _option_setting, 0, 0, 3, { 0 }, "network.text_chat", { "on", "links", "off" } },
	{ "aim_assist_spinner", _option_setting, 0, 0, 2, { 0 }, "network.block_mouse_aim_assist", { "false", "true" } },
	/* (the shield effects every machine draws: network_shield_effects.c) */
	{ "shield_effects_spinner", _option_setting, 0, 0, 7, { 0 }, "network.shield_effects",
		{ "stock", "glow", "light_show", "overshield", "glow_overshield", "light_show_overshield", "players" } },
	/* co-op's options (Server Setup's CO-OP OPTIONS: tools/port_settings.py's
	COOP_SETUP_SCREENS; coop_enemies.c) */
	{ "coop_friendly_fire_spinner", _option_setting, 0, 0, 4, { 0 }, "network.coop_friendly_fire",
		{ "off", "on", "shields_only", "explosives_only" } },
	{ "coop_extra_enemies_spinner", _option_setting, 0, 0, 3, { 0 }, "network.coop_enemies_mode",
		{ "per_player", "multiplier", "none" } },
	{ "coop_extra_pickups_spinner", _option_setting, 0, 0, 3, { 0 }, "network.coop_pickups",
		{ "per_player", "multiplier", "none" } },
	{ "coop_extra_vehicles_spinner", _option_setting, 0, 0, 3, { 0 }, "network.coop_vehicles",
		{ "per_player", "multiplier", "none" } },
	{ "coop_enemies_per_player_spinner", _option_setting, 0, 0, 5, { 0 }, "network.coop_enemies",
		{ "25", "50", "100", "150", "200" } },
	{ "coop_enemies_multiplier_spinner", _option_setting, 0, 0, 5, { 0 }, "network.coop_enemies_multiplier",
		{ "2", "4", "8", "16", "32" } },
	{ "coop_player_collisions_spinner", _option_setting, 0, 0, 2, { 0 }, "network.coop_player_collisions",
		{ "true", "false" } },
	{ "coop_ally_friendly_fire_spinner", _option_setting, 0, 0, 2, { 0 }, "network.coop_ally_friendly_fire",
		{ "true", "false" } },
	/* map sharing's (Server Setup's MAP SHARING: tools/port_settings.py's
	MAP_SHARE_SETUP; map_share.c) */
	{ "map_share_spinner", _option_setting, 0, 0, 2, { 0 }, "network.map_share", { "true", "false" } },
	{ "map_share_upload_spinner", _option_setting, 0, 0, 8, { 0 }, "network.map_share_upload",
		{ "256", "512", "1024", "2048", "4096", "8192", "16384", "20480" } },
	/* vehicle options (the side's set and counts: vehicles_update) */
	{ "vehicles_respawn_spinner", _option_short, OPTIONS_FIELD(vehicle_respawn_time), 0, 7,
		{ 0, 30, 60, 90, 120, 180, 300 } },
};

/* an option of the host's own setting: the value shown for it (the one
set, else the nearest) */
static short gametype_option_setting_index(struct gametype_option const *option)
{
	struct pc_menu_setting setting;
	char text[64];
	short index;

	csmemset(&setting, 0, sizeof(setting));
	setting.setting = option->setting;
	setting.value_count = option->count;
	for (index = 0; index < option->count; index++)
		setting.values[index] = option->setting_values[index];
	if (!setting_text(option->setting, text, sizeof(text), FALSE))
		return 0;
	return setting_value_index(&setting, text);
}

/* (an instance's name is its definition's cut to 31 characters:
"item_options_infinite_grenades_") */
#define WIDGET_NAME_LENGTH 31

static struct gametype_option const *gametype_option_named(char const *spinner)
{
	short index;

	for (index = 0; index < NUMBEROF(gametype_options); index++)
	{
		if (!strncmp(gametype_options[index].spinner, spinner, WIDGET_NAME_LENGTH))
			return &gametype_options[index];
	}
	return NULL;
}

static long gametype_option_value(struct gametype_option const *option, struct game_variant *variant,
	struct game_variant_options *options)
{
	byte *v = (byte *)variant;
	byte *o = (byte *)options;

	switch (option->kind)
	{
	case _option_long: return *(long *)(v + option->offset);
	case _option_byte: return v[option->offset] != 0;
	case _option_flag: return (variant->universal_variant.flags & option->argument) != 0;
	case _option_health: return (long)(variant->universal_variant.health * 10.0f + 0.5f);
	case _option_short: return *(short *)(o + option->offset);
	case _option_option_byte: return o[option->offset];
	case _option_byte_value: return v[option->offset];
	case _option_radar: return options->radar_players;
	}
	return 0;
}

static void gametype_option_value_set(struct gametype_option const *option, long value, struct game_variant *variant,
	struct game_variant_options *options)
{
	byte *v = (byte *)variant;
	byte *o = (byte *)options;

	switch (option->kind)
	{
	case _option_long: *(long *)(v + option->offset) = value; break;
	case _option_byte: v[option->offset] = (byte)value; break;
	case _option_flag:
		if (value)
			variant->universal_variant.flags |= option->argument;
		else
			variant->universal_variant.flags &= ~option->argument;
		break;
	case _option_health: variant->universal_variant.health = (real)value / 10.0f; break;
	case _option_short: *(short *)(o + option->offset) = (short)value; break;
	case _option_option_byte: o[option->offset] = (byte)value; break;
	case _option_byte_value: v[option->offset] = (byte)value; break;
	case _option_radar:
		/* (and the Xbox's flag: other players on the tracker or not) */
		options->radar_players = (byte)value;
		if (value == _radar_players_none)
			variant->universal_variant.flags &= ~FLAG(_game_variant_draw_object_in_motion_sensor_bit);
		else
			variant->universal_variant.flags |= FLAG(_game_variant_draw_object_in_motion_sensor_bit);
		break;
	}
}

/* the value's place in the option's list: its own, else the nearest */
static short gametype_option_index(struct gametype_option const *option, long value)
{
	short index, best = 0;
	long best_distance = 0x7FFFFFFF;

	for (index = 0; index < option->count; index++)
	{
		long distance = option->values[index] > value ? option->values[index] - value : value - option->values[index];

		if (distance < best_distance)
		{
			best_distance = distance;
			best = index;
		}
	}
	return best;
}

/* each of the screen's option rows' spinner, with the option */
static void gametype_options_each(struct widget_instance *list, boolean save)
{
	struct game_variant *variant = edit_variant();
	struct game_variant_options *options = edit_options();
	struct widget_instance *row;

	if (!variant || !options)
		return;
	for (row = list->child; row; row = row->next)
	{
		struct widget_instance *spinner;

		for (spinner = row->child; spinner; spinner = spinner->next)
		{
			/* (a spinner: its label's name, cut to 31 characters, can be
			the same as its own) */
			struct gametype_option const *option = spinner->type == 2 ? gametype_option_named(spinner->name) : NULL;

			if (!option)
				continue;
			/* (the host's own settings: Server Setup's alone, their rows
			hidden in Edit Gametypes', which edits a gametype for any game;
			written on OK, as the gametype's are kept) */
			if (option->kind == _option_setting)
			{
				short index = (short)PIN(spinner->parameters.list.selected_index, 0, option->count - 1);

				visible_set(row, gametype_edit.setup);
				if (!gametype_edit.setup)
					continue;
				if (!save)
				{
					spinner->parameters.list.selected_index = gametype_option_setting_index(option);
				}
				else if (index != gametype_option_setting_index(option) &&
					!setting_write(option->setting, option->setting_values[index]))
				{
					platform_log("menus: could not set %s", option->setting);
				}
				continue;
			}
			if (save)
			{
				short index = (short)PIN(spinner->parameters.list.selected_index, 0, option->count - 1);

				gametype_option_value_set(option, option->values[index], variant, options);
			}
			else
				spinner->parameters.list.selected_index =
					gametype_option_index(option, gametype_option_value(option, variant, options));
		}
	}
}

/* each of a screen's rows of the host's own settings (_option_setting:
co-op's options' screens, tools/port_settings.py's _setup_option_screen),
shown, or (save) set */
static void setting_options_each(struct widget_instance *list, boolean save)
{
	struct widget_instance *row;

	for (row = list->child; row; row = row->next)
	{
		struct widget_instance *spinner;

		for (spinner = row->child; spinner; spinner = spinner->next)
		{
			struct gametype_option const *option = spinner->type == 2 ? gametype_option_named(spinner->name) : NULL;
			short index;

			if (!option || option->kind != _option_setting)
				continue;
			index = (short)PIN(spinner->parameters.list.selected_index, 0, option->count - 1);
			if (!save)
			{
				spinner->parameters.list.selected_index = gametype_option_setting_index(option);
			}
			else if (index != gametype_option_setting_index(option) &&
				!setting_write(option->setting, option->setting_values[index]))
			{
				platform_log("menus: could not set %s", option->setting);
			}
		}
	}
}

/* the options' list of a button of its bar (OK's): the one above it whose
rows are its options */
static struct widget_instance *gametype_options_list(struct widget_instance *widget)
{
	struct widget_instance *list;

	for (list = widget; list; list = list->parent)
	{
		struct widget_instance *child;

		for (child = list->child; child; child = child->next)
		{
			if (!strncmp(child->name, "op_", 3))
				return list;
		}
	}
	return NULL;
}

/* ---- the vehicle options: a side's (red's, blue's) set and counts, on
the spinners while its side is shown */

static char const *const vehicle_spinners[NUMBER_OF_VARIANT_VEHICLES] =
{
	"warthog_spinner", "ghost_spinner", "scorpion_spinner", "rwarthog_spinner", "banshee_spinner", "cgturret_spinner"
};
/* (the presets' spinner: the vehicle sets 0 to 7, then PC and CUSTOM) */
#define VEHICLE_PRESET_PC 8
#define VEHICLE_PRESET_CUSTOM 9

static struct widget_instance *vehicle_spinner(struct widget_instance *list, char const *name)
{
	struct widget_instance *row;

	for (row = list->child; row; row = row->next)
	{
		struct widget_instance *spinner = named(row, name, 0);

		if (spinner)
			return spinner;
	}
	return NULL;
}

static void vehicles_show(struct widget_instance *list)
{
	struct game_variant_options *options = edit_options();
	struct widget_instance *spinner;
	short side = gametype_edit.vehicle_side, index;

	if (!options)
		return;
	if ((spinner = vehicle_spinner(list, "team_spinner")) != NULL)
		spinner->parameters.list.selected_index = side;
	if ((spinner = vehicle_spinner(list, "vehicle_presets_spinner")) != NULL)
		spinner->parameters.list.selected_index = options->vehicle_set[side] == VARIANT_VEHICLE_SET_CUSTOM ?
			VEHICLE_PRESET_CUSTOM : options->vehicle_set[side] == VARIANT_VEHICLE_SET_PC ?
			VEHICLE_PRESET_PC : (short)MIN(options->vehicle_set[side], VEHICLE_PRESET_PC - 1);
	for (index = 0; index < NUMBER_OF_VARIANT_VEHICLES; index++)
	{
		if ((spinner = vehicle_spinner(list, vehicle_spinners[index])) != NULL)
			spinner->parameters.list.selected_index =
				(short)MIN(options->vehicle_counts[side][index], MAXIMUM_VARIANT_VEHICLE_COUNT);
	}
}

/* the side shown's spinners kept (and the Xbox's vehicle set: red's, if it
is one of the Xbox's) */
static void vehicles_keep(struct widget_instance *list)
{
	struct game_variant *variant = edit_variant();
	struct game_variant_options *options = edit_options();
	struct widget_instance *spinner;
	short side = gametype_edit.vehicle_side, index;

	if (!options || !variant)
		return;
	if ((spinner = vehicle_spinner(list, "vehicle_presets_spinner")) != NULL)
		options->vehicle_set[side] = spinner->parameters.list.selected_index >= VEHICLE_PRESET_CUSTOM ?
			VARIANT_VEHICLE_SET_CUSTOM : spinner->parameters.list.selected_index == VEHICLE_PRESET_PC ?
			VARIANT_VEHICLE_SET_PC : (byte)spinner->parameters.list.selected_index;
	for (index = 0; index < NUMBER_OF_VARIANT_VEHICLES; index++)
	{
		if ((spinner = vehicle_spinner(list, vehicle_spinners[index])) != NULL)
			options->vehicle_counts[side][index] =
				(byte)PIN(spinner->parameters.list.selected_index, 0, MAXIMUM_VARIANT_VEHICLE_COUNT);
	}
	variant->universal_variant.vehicle_set = options->vehicle_set[0] <= 4 ? options->vehicle_set[0] : 0;
}

/* "mp prof vehicles update": the side changed (the one left kept, the other
shown); a count changed makes the side's set CUSTOM */
static void vehicles_update(struct widget_instance *list)
{
	struct game_variant_options *options = edit_options();
	struct widget_instance *team = vehicle_spinner(list, "team_spinner");
	struct widget_instance *preset = vehicle_spinner(list, "vehicle_presets_spinner");
	short index;

	if (!options)
		return;
	if (team && team->parameters.list.selected_index != gametype_edit.vehicle_side)
	{
		vehicles_keep(list);
		gametype_edit.vehicle_side = (short)PIN(team->parameters.list.selected_index, 0, 1);
		vehicles_show(list);
		return;
	}
	for (index = 0; preset && index < NUMBER_OF_VARIANT_VEHICLES; index++)
	{
		struct widget_instance *spinner = vehicle_spinner(list, vehicle_spinners[index]);

		if (spinner && spinner->parameters.list.selected_index !=
			options->vehicle_counts[gametype_edit.vehicle_side][index])
		{
			options->vehicle_counts[gametype_edit.vehicle_side][index] = (byte)spinner->parameters.list.selected_index;
			preset->parameters.list.selected_index = VEHICLE_PRESET_CUSTOM;
		}
	}
}

/* "mp profile init X" (the list's creation): its spinners from the gametype */
static boolean gametype_options_init(struct widget_instance *list)
{
	if (!edit_variant())
		return campaign_fail();
	gametype_options_each(list, FALSE);
	if (named(list, "op_team", 0))
		vehicles_show(list);
	return TRUE;
}

/* "mp profile set X" (OK): the gametype from its spinners */
static boolean gametype_options_save(struct widget_instance *widget)
{
	struct widget_instance *list = gametype_options_list(widget);

	if (!list || !edit_variant())
		return campaign_fail();
	if (named(list, "op_team", 0))
		vehicles_keep(list);
	gametype_options_each(list, TRUE);
	/* (OPENCE OPTIONS: Infection is played on teams, Gun Game free for all:
	game_modes_variant_mode) */
	if (named(list, "oce_mode_spinner", 0) && edit_variant()->game_engine_index == game_engine_slayer)
	{
		if (edit_options()->mode == _game_mode_infection)
			edit_variant()->universal_variant.teams = TRUE;
		else if (edit_options()->mode == _game_mode_gun_game)
			edit_variant()->universal_variant.teams = FALSE;
	}
	return TRUE;
}

/* ---- Server Setup's options: a copy of the game's gametype (the one chosen
before), edited by the editor's screens, the game's on START GAME */

static void gametype_setup_begin(void)
{
	if (gametype_edit.setup)
		return;
	if (!player_ui_game_variant_specified(&gametype_edit.setup_variant))
		return;
	gametype_edit.setup_options = *player_ui_get_game_variant_options();
	gametype_edit.setup = TRUE;
	gametype_edit.vehicle_side = 0;
}

static void gametype_setup_end(void)
{
	gametype_edit.setup = FALSE;
}

static boolean gametype_setup_apply(void)
{
	boolean applied = !gametype_edit.setup ||
		ui_widget_port_game_variant_set(&gametype_edit.setup_variant, &gametype_edit.setup_options);

	gametype_edit.setup = FALSE;
	return applied;
}

/* the game type row's: the gametype's name and type */
static void gametype_setup_type(wchar_t *text)
{
	wchar_t name[NUMBEROF(gametype_edit.setup_variant.human_readable_game_description) + 1];
	wchar_t const *kind;

	text[0] = 0;
	if (!gametype_edit.setup)
		return;
	ustrncpy(name, gametype_edit.setup_variant.human_readable_game_description, NUMBEROF(name) - 1);
	name[NUMBEROF(name) - 1] = 0;
	kind = game_modes_variant_kind_name(&gametype_edit.setup_variant, &gametype_edit.setup_options);
	usnprintf(text, ROW_TEXT_LENGTH - 1, L"%s (%s%s)", name,
		kind ? kind : engine_names[PIN(gametype_edit.setup_variant.game_engine_index, 0, 5)],
		gametype_edit.setup_variant.universal_variant.teams ? L", TEAMS" : L"");
	text[ROW_TEXT_LENGTH - 1] = 0;
}

/* "port setup edit" (a Server Setup option's row): its screen edits the copy */
static boolean gametype_setup_edit(void)
{
	gametype_setup_begin();
	return gametype_edit.setup || campaign_fail();
}

/* ---- Edit Gametypes' list */

static void gametype_edit_read(void)
{
	short last;

	gametype_edit.count = ui_widget_port_gametypes(gametype_edit.gametypes, MAXIMUM_GAMETYPES, &last);
	gametype_edit.chosen = (short)MIN(gametype_edit.chosen, MAX(0, gametype_edit.count - 1));
	gametype_edit.stale = FALSE;
}

/* "mp profiles list initialize" (Edit Gametypes'): on the gametype used last */
static boolean gametype_edit_list_initialize(struct widget_instance *list)
{
	short last;

	gametype_edit.setup = FALSE;
	gametype_edit.count = ui_widget_port_gametypes(gametype_edit.gametypes, MAXIMUM_GAMETYPES, &last);
	gametype_edit.chosen = last;
	gametype_edit.first = (short)MAX(0, MIN(last - GAMETYPE_EDIT_ROWS / 2, gametype_edit.count - GAMETYPE_EDIT_ROWS));
	gametype_edit.stale = FALSE;
	focus_row(list, (short)(gametype_edit.chosen - gametype_edit.first));
	return TRUE;
}

static void gametype_edit_row_text(short row, wchar_t *text)
{
	gametype_display_name(gametype_edit.gametypes[gametype_edit.first + row], text);
}

/* "gt edit list update": the rows (scrolled), the gametype chosen's
picture, name and rules */
static void gametype_edit_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *widget;
	short focused;

	if (gametype_edit.stale)
		gametype_edit_read();
	focused = list_scroll(list, &gametype_edit.first, gametype_edit.count, GAMETYPE_EDIT_ROWS);
	if (focused != NONE)
		gametype_edit.chosen = (short)MAX(0, MIN(focused, gametype_edit.count - 1));
	rows_update(list, (short)MIN(gametype_edit.count, GAMETYPE_EDIT_ROWS), gametype_edit_row_text);
	visible_set(named(description, "gametype_right_item", 0), gametype_edit.count > 0);
	if (gametype_edit.chosen < gametype_edit.count)
	{
		long profile_index = gametype_edit.gametypes[gametype_edit.chosen];
		struct game_variant variant;
		wchar_t text[ROW_TEXT_LENGTH * 2];

		visible_set(named(description, "locked_gametype_icon", 0),
			((unsigned long)profile_index & PLAYLIST_READ_ONLY_BIT) != 0);
		gametype_display_name(profile_index, text);
		text_set(named(description, "gametype_right_name", 0), text);
		if (playlist_profile_get(profile_index, &variant))
		{
			long engine = PIN(variant.game_engine_index, 0, 5);

			if ((widget = named(description, "gametype_right_pic", 0)) != NULL)
				widget->animation.current_frame_index = (short)engine;
			gametype_rules_text(profile_index, text, NUMBEROF(text));
			text_set(named(description, "gametype_right_data", 0), text);
		}
	}
	profile_name_show(description);
}

/* "port playlist edit desc" (after the PC version's "playlist settings menu
update desc"): the gametype editor's OPENCE OPTIONS, its eighth row, has its
own words (the list's ninth) over GAME OPTIONS' picture, and the buttons
after it SAVE CHANGES' */
#define PLAYLIST_EDIT_OPENCE_ROW 7
#define PLAYLIST_EDIT_OPENCE_WORDS 8
#define PLAYLIST_EDIT_GAME_OPTIONS_PICTURE 1

static void playlist_edit_description(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	short index = list->focused_child ? ui_widget_port_list_index(list) : NONE;

	if (!description || !description->child || !description->child->next || index < PLAYLIST_EDIT_OPENCE_ROW)
		return;
	description->child->parameters.text_box.string_list_index =
		index == PLAYLIST_EDIT_OPENCE_ROW ? PLAYLIST_EDIT_OPENCE_WORDS : (short)(index - 1);
	description->child->next->animation.current_frame_index =
		index == PLAYLIST_EDIT_OPENCE_ROW ? PLAYLIST_EDIT_GAME_OPTIONS_PICTURE : (short)(index - 1);
}

/* "mp profile begin editing" (the list's custom activation: OK, a row's A) */
static boolean gametype_edit_begin(void)
{
	if (gametype_edit.chosen >= gametype_edit.count)
		return campaign_fail();
	gametype_edit.setup = FALSE;
	gametype_edit.vehicle_side = 0;
	return ui_widget_port_gametype_edit_begin(gametype_edit.gametypes[gametype_edit.chosen]);
}

/* "request del playlist profile" (X, DELETE): not a built-in one */
static boolean gametype_delete_request(void)
{
	long profile_index = gametype_edit.chosen < gametype_edit.count ? gametype_edit.gametypes[gametype_edit.chosen] :
		NONE;

	if (profile_index == NONE || ((unsigned long)profile_index & PLAYLIST_READ_ONLY_BIT))
		return campaign_fail();
	gametype_edit.deleting = profile_index;
	return TRUE;
}

/* "final del playlist profile" */
static boolean gametype_delete_final(void)
{
	boolean deleted = gametype_edit.deleting != NONE && ui_widget_port_gametype_delete(gametype_edit.deleting);

	gametype_edit.deleting = NONE;
	gametype_edit.stale = TRUE;
	return deleted;
}

/* ---- the gametype's screen and the gametype type's */

/* "get edit game settings name": the gametype's name */
static void gametype_edit_name(struct widget_instance *text_box)
{
	struct game_variant *variant = edit_variant();
	wchar_t name[NUMBEROF(variant->human_readable_game_description) + 1];

	if (!variant)
		return;
	ustrncpy(name, variant->human_readable_game_description, NUMBEROF(name) - 1);
	name[NUMBEROF(name) - 1] = 0;
	text_set(text_box, name);
}

/* "game settings lists text update": the help of the option with the
focus, for its value (the description's strings are each option's values'
helps in turn), as the Xbox's, which stops the game on the buttons' row */
static void gametype_option_help(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *row;
	short index = 0;

	/* (Item Options: the weapon set's row for a category loadout, the two
	weapons' for a custom one) */
	if (named(list, "loadout_spinner", 0))
	{
		boolean custom = named(list, "loadout_spinner", 0)->parameters.list.selected_index == _loadout_custom;

		visible_set(named(list, "op_weapon_set", 0), !custom);
		visible_set(named(list, "op_primary_weapon", 0), custom);
		visible_set(named(list, "op_secondary_weapon", 0), custom);
	}
	/* (OPENCE OPTIONS: the mode's own rows, Infection's or Gun Game's, below
	MODE and HARDCORE) */
	if (named(list, "oce_mode_spinner", 0))
	{
		short mode = named(list, "oce_mode_spinner", 0)->parameters.list.selected_index;
		short place = 73 + 2 * 30;

		for (row = list->child; row; row = row->next)
		{
			boolean infection = !strncmp(row->name, "op_inf_", 7);

			if (!infection && strncmp(row->name, "op_gg_", 6))
				continue;
			row->visible = infection ? mode == _game_mode_infection : mode == _game_mode_gun_game;
			if (row->visible)
			{
				row->vertical_offset = place;
				place += 30;
			}
		}
	}
	/* CO-OP OPTIONS: enemies, pickups and vehicles share the amount rows.
	Mixed modes may need both rows at once. */
	if (named(list, "coop_extra_enemies_spinner", 0))
	{
		short mode = named(list, "coop_extra_enemies_spinner", 0)->parameters.list.selected_index;

		struct widget_instance *pickups = named(list, "coop_extra_pickups_spinner", 0);
		struct widget_instance *vehicles = named(list, "coop_extra_vehicles_spinner", 0);
		boolean per_player = mode == _cooperative_enemies_per_player ||
			(pickups && pickups->parameters.list.selected_index == _cooperative_enemies_per_player) ||
			(vehicles && vehicles->parameters.list.selected_index == _cooperative_enemies_per_player);
		boolean multiplier = mode == _cooperative_enemies_multiplier ||
			(pickups && pickups->parameters.list.selected_index == _cooperative_enemies_multiplier) ||
			(vehicles && vehicles->parameters.list.selected_index == _cooperative_enemies_multiplier);

		visible_set(named(list, "op_coop_enemies_per_player", 0), per_player);
		visible_set(named(list, "op_coop_enemies_multiplier", 0), multiplier);

		/* Hidden amounts must not reserve a blank row. Offsets are relative
		to the parent: moving only the row carries its labels and spinners
		with it when widget_instance_render_recursive draws and notes targets. */
		{
			short y = 73;

			for (row = list->child; row; row = row->next)
				if (row->visible && !strncmp(row->name, "op_coop_", 8))
				{
					row->vertical_offset = y;
					y += 30;
				}
		}
	}
	/* (the server browser's filters, hidden: their helps are fewer than
	their values) */
	if (!description || !list->focused_child || !strncmp(list->name, "filters", 7))
		return;
	for (row = list->child; row; row = row->next)
	{
		struct widget_instance *spinner;

		for (spinner = row->child; spinner && spinner->type != 2 /* spinner */; spinner = spinner->next)
			;
		if (row == list->focused_child)
		{
			if (spinner)
				description->parameters.text_box.string_list_index = (short)(index + spinner->parameters.list.selected_index);
			return;
		}
		if (spinner)
			index += spinner->parameters.list.number_of_items;
	}
}

static char const *const engine_items[] =
{
	/* (the type list's rows, in order: game_engine_ctf and so on) */
	"gametype_select_ctf_item", "gametype_select_koth_item", "gametype_select_slayer_item",
	"gametype_select_oddball_item", "gametype_select_race_item"
};
static long const engine_of_item[] = { 1, 4, 2, 3, 5 };

/* "mp profile init game engine": the gametype's type focused */
static boolean gametype_engine_init(struct widget_instance *list)
{
	struct game_variant *variant = edit_variant();
	short index;

	if (!variant)
		return campaign_fail();
	for (index = 0; index < NUMBEROF(engine_items); index++)
	{
		if (engine_of_item[index] == variant->game_engine_index)
			focus_row(list, index);
	}
	return TRUE;
}

/* "mp profile set game engine" (a type's A): another type clears the type's
own rules (as the Xbox's) */
static boolean gametype_engine_set(struct widget_instance *item)
{
	struct game_variant *variant = edit_variant();
	short index;

	if (!variant)
		return campaign_fail();
	for (index = 0; index < NUMBEROF(engine_items); index++)
	{
		if (!strcmp(item->name, engine_items[index]))
		{
			if (variant->game_engine_index != engine_of_item[index])
				csmemset(&variant->game_engine_variant, 0, sizeof(variant->game_engine_variant));
			variant->game_engine_index = engine_of_item[index];
			return TRUE;
		}
	}
	return campaign_fail();
}

/* "mp edit profile set rule text": the gametype's type (its string in
ui\multiplayer_game_text, as the Xbox's: capture the flag 3, slayer 4,
oddball 5, king of the hill 6, race 7, unknown 8) */
static void gametype_engine_name(struct widget_instance *text_box)
{
	static short const strings[] = { 8, 3, 4, 5, 6, 7 };
	struct game_variant *variant = edit_variant();

	if (variant)
		text_box->parameters.text_box.string_list_index = strings[PIN(variant->game_engine_index, 0, 5)];
}

/* ---------- the party: Multiplayer's PARTY and its screen */

#include "party_menu.inc"
#include "matchmaking_menu.inc"
#include "friends_menu.inc"
#include "lobby_menu.inc"
/* (the campaign's screens: the Campaign menu, Choose Mission, Choose Difficulty) */
#include "campaign_menu.inc"

/* ---------- Host Controls: the pause menu's HOST CONTROLS, the host's */

#include "host_controls_menu.inc"

/* ---------- public code */

/* (network_host_game.c's) a map's name as the lobby shows it */
void menu_map_shown(char const *level, wchar_t *text, short size)
{
	lobby_board_map_text(level, text, size);
}

boolean pc_menu_event_function_invoke(
	struct widget_instance *widget,
	struct event_record *event,
	long function_index,
	boolean *widget_deleted)
{
	short controller = event_controller(widget, event);

	switch (function_index)
	{
	case _pc_menu_function_quit_game:
		platform_request_quit();
		return TRUE;
	case _pc_menu_function_setting_load:
		return setting_load(widget);
	case _pc_menu_function_setting_save:
		return setting_save(widget);
	}
	{
		char const *name = pc_menu_function_name(function_index);
		boolean result = TRUE;

		if (!name)
			return FALSE;
		if (!strncmp(name, "port controller ", 16))
			return pad_editor_event(widget, name);
		if (!strncmp(name, "port campaign ", 14))
			return campaign_menu_event(widget, name + 14, controller);
		if (!strcmp(name, "main menu quit game"))
		{
			platform_request_quit();
		}
		else if (!strcmp(name, "profile set edit begin"))
		{
			return profile_edit_begin(widget, event, widget_deleted);
		}
		else if (!strcmp(name, "new campaign decision"))
		{
			return new_profile_decision(widget, event, widget_deleted);
		}
		/* (the press posted is the controller's that chose the button: a
		split screen player's LEAVE is theirs) */
		else if (!strcmp(name, "mouse emit accept event"))
		{
			event_manager_post_button(controller, BUTTON_A);
		}
		else if (!strcmp(name, "mouse emit back event"))
		{
			event_manager_post_button(controller, BUTTON_B);
		}
		else if (!strcmp(name, "mouse emit x event"))
		{
			event_manager_post_button(controller, BUTTON_X);
		}
		else if (!strcmp(name, "emit custom activation event"))
		{
			struct widget_instance *top = widget;

			while (top->parent)
				top = top->parent;
			boolean deleted;

			ui_widget_port_dispatch_event(top, EVENT_CUSTOM_ACTIVATION, event_controller(widget, event), &deleted);
			if (deleted)
				*widget_deleted = TRUE;
		}
		else if (!strcmp(name, "mp type set mode"))
		{
			lan_mode = strstr(widget->name, "_lan_") != NULL;
			multiplayer_mode_set(widget);
			/* (Create's: the map list only for a game made, "join controller
			to mp game" before; it fails with the game's port in use, by
			another copy of the game) */
			if ((multiplayer.mode == _multiplayer_mode_host_internet ||
				multiplayer.mode == _multiplayer_mode_host_lan ||
				multiplayer.mode == _multiplayer_mode_host_local) && !global_network_game_server_get())
			{
				platform_log("menus: the game could not be made (is its port in use?)");
				/* (said on the screen too: not a silent dead end) */
				display_error_text_deferred(player_ui_get_active_player_profile_index(0) == NONE ?
					L"Choose a player profile\r\nfirst (Profiles, on the\r\nmain menu)." :
					L"Couldn't start the game.\r\nClose any other copy of\r\nOpenH1 or Halo, then try\r\nagain.",
					NONE);
				return campaign_fail();
			}
		}
		else if (!strcmp(name, "gamespy screen init"))
		{
			return browser_initialize(widget, event, widget_deleted);
		}
		else if (!strcmp(name, "gamespy select item") || !strcmp(name, "gamespy select button"))
		{
			return browser_select(widget, event, controller, widget_deleted);
		}
		else if (!strcmp(name, "port browser favorite"))
		{
			return browser_favorite_toggle(browser_row_index(widget));
		}
		/* (the browsers' sort: a column's title; X on a row, the next) */
		else if (!strcmp(name, "gamespy select header"))
		{
			return browser_sort_header(widget);
		}
		else if (!strcmp(name, "port browser sort"))
		{
			return browser_sort_choose(_browser_sort_players, TRUE);
		}
		/* (the browsers' tabs: one chosen, LB and RB the one before and after) */
		else if (!strcmp(name, "port browser tab"))
		{
			return browser_tab_choose(sibling_index(widget));
		}
		else if (!strcmp(name, "port browser tab next") || !strcmp(name, "port browser tab previous"))
		{
			return browser_tab_choose((short)(browser_tab + (!strcmp(name, "port browser tab next") ? 1 : -1)));
		}
		else if (!strcmp(name, "port browser refresh"))
		{
			return browser_refresh(widget, controller, widget_deleted);
		}
		else if (!strcmp(name, "port browser filters"))
		{
			return browser_filters_open();
		}
		else if (!strcmp(name, "port browser search"))
		{
			return browser_search_edit(widget);
		}
		else if (!strcmp(name, "gamespy back handler") && text_field_editing(NULL))
		{
			text_field_end(FALSE);
		}
		else if (!strcmp(name, "port setup edit"))
		{
			return gametype_setup_edit();
		}
		/* (co-op's options' screens: their settings shown, and OK's) */
		else if (!strcmp(name, "port setup options init"))
		{
			setting_options_each(widget, FALSE);
		}
		else if (!strcmp(name, "port setup options save"))
		{
			struct widget_instance *list = gametype_options_list(widget);

			if (list)
				setting_options_each(list, TRUE);
		}
		else if (!strcmp(name, "mp profile save changes"))
		{
			return ui_widget_port_gametype_save(widget, widget_deleted);
		}
		else if (!strcmp(name, "mp profile begin editing"))
		{
			return gametype_edit_begin();
		}
		else if (!strcmp(name, "request del playlist profile"))
		{
			return gametype_delete_request();
		}
		else if (!strcmp(name, "final del playlist profile"))
		{
			return gametype_delete_final();
		}
		else if (!strcmp(name, "mp profile init game engine"))
		{
			return gametype_engine_init(widget);
		}
		else if (!strcmp(name, "mp profile set game engine"))
		{
			return gametype_engine_set(widget);
		}
		else if (!strncmp(name, "mp profile init ", 16) || !strcmp(name, "mp prof init teamplay options") ||
			!strcmp(name, "mp prof init vehicle options"))
		{
			return gametype_options_init(widget);
		}
		else if ((!strncmp(name, "mp profile set ", 15) && strcmp(name, "mp profile set for game")) ||
			!strcmp(name, "mp prof save teamplay options") || !strcmp(name, "mp prof save vehicle options"))
		{
			return gametype_options_save(widget);
		}
		else if (!strcmp(name, "port lobby preview join"))
		{
			return preview_join(widget, controller, widget_deleted);
		}
		else if (!strcmp(name, "gamespy screen dispose"))
		{
			lobby_browser_end();
		}
		else if (!strcmp(name, "port pause end game"))
		{
			return pause_end_game();
		}
		else if (!strcmp(name, "port coop friendly fire"))
		{
			return coop_friendly_fire_change();
		}
		else if (!strcmp(name, "player profile save changes"))
		{
			return profile_save_changes(widget, widget_deleted);
		}
		else if (!strncmp(name, "port settings ", 14) && settings_function(name + 14, widget, event, widget_deleted,
			&result))
		{
			return result;
		}
		else if (!strcmp(name, "port discord link"))
		{
			return discord_link(widget);
		}
		else if (!strcmp(name, "port discord unlink"))
		{
			return discord_unlink();
		}
		else if (!strcmp(name, "direct ip connect go"))
		{
			return direct_link_from_clipboard() && connect_open(widget, controller, widget_deleted);
		}
		else if (!strcmp(name, "port connect back"))
		{
			return connect_back(widget, widget_deleted);
		}
		else if (!strcmp(name, "port connect retry"))
		{
			return connect_retry(widget, widget_deleted);
		}
		else if (!strcmp(name, "join controller to mp game"))
		{
			return multiplayer_host(widget, event, controller, widget_deleted);
		}
		else if (!strcmp(name, "mp level list initialize"))
		{
			return map_list_initialize(widget);
		}
		else if (!strcmp(name, "mp level select"))
		{
			return map_list_choose(widget, widget_deleted);
		}
		else if (!strcmp(name, "port map list back"))
		{
			return map_list_back(widget, widget_deleted);
		}
		else if (!strcmp(name, "mp profiles list initialize"))
		{
			if (!strcmp(widget->name, "playlist_select_list"))
				return gametype_edit_list_initialize(widget);
			return gametype_list_initialize(widget);
		}
		else if (!strcmp(name, "mp profile set for game"))
		{
			return gametype_choose(widget);
		}
		else if (!strcmp(name, "server settings init"))
		{
			return server_settings_initialize(widget);
		}
		else if (!strcmp(name, "ss edit server name"))
		{
			return server_name_edit(widget);
		}
		else if (!strcmp(name, "ss edit server password"))
		{
			return server_password_edit(widget);
		}
		else if (!strcmp(name, "port password init"))
		{
			return password_screen_initialize(widget);
		}
		else if (!strcmp(name, "port password edit"))
		{
			return password_screen_edit(widget, widget_deleted);
		}
		else if (!strcmp(name, "port password join"))
		{
			return password_screen_join(widget, widget_deleted);
		}
		else if (!strcmp(name, "port password back"))
		{
			return password_screen_back(widget, widget_deleted);
		}
		else if (!strcmp(name, "ss copy invite"))
		{
			return invite_copy();
		}
		else if (!strcmp(name, "ss start game"))
		{
			return server_start();
		}
		else if (!strcmp(name, "single prev cl item activated"))
		{
			item_activated(widget, event_controller(widget, event), widget_deleted);
		}
		else if (!strcmp(name, "port settings save"))
		{
			settings_each(screen_of(widget), setting_changed_save);
			platform_display_apply();
		}
		else if (!strcmp(name, "port settings defaults"))
		{
			settings_each(screen_of(widget), setting_default_show);
			settings_sliders_default(screen_of(widget));
			browser_search_default(screen_of(widget));
			pad_editor_defaults(screen_of(widget));
		}
		else if (!strncmp(name, "port slider ", 12))
		{
			return settings_slider_function(name + 12, widget);
		}
		else if (!strcmp(name, "port mods init") || !strcmp(name, "port mods close"))
		{
			return mods_screen_reset();
		}
		else if (!strcmp(name, "port mods toggle"))
		{
			return mods_toggle(widget);
		}
		else if (!strcmp(name, "port mods move up") || !strcmp(name, "port mods move down"))
		{
			return mods_move(widget, (short)(!strcmp(name, "port mods move up") ? -1 : 1));
		}
		else if (!strcmp(name, "port mods save"))
		{
			return mods_save();
		}
		else if (!strcmp(name, "port training chapter"))
		{
			return training_chapter_choose(widget);
		}
		else if (!strcmp(name, "port training open"))
		{
			/* (the pause menu's CHAPTERS: its screen opened by the button) */
		}
		else if (!strcmp(name, "port update now"))
		{
			updater_update_now();
		}
		else if (!strcmp(name, "port multiplayer status end"))
		{
			multiplayer_status_end();
		}
		else if (!strcmp(name, "port multiplayer refresh"))
		{
			p2p_lobby_refresh();
			multiplayer_status.read_time = 0;
			ui_play_audio_feedback_sound(SOUND_FORWARD);
		}
		/* (the party: party_menu.inc) */
		else if (!strcmp(name, "port party open"))
		{
			return party_screen_open_event(widget, event, controller, widget_deleted);
		}
		else if (!strcmp(name, "port party button"))
		{
			return party_screen_button(widget, controller, widget_deleted);
		}
		/* (matchmaking: matchmaking_menu.inc) */
		else if (!strcmp(name, "port matchmaking button"))
		{
			return matchmaking_screen_button(widget, controller);
		}
		else if (!strcmp(name, "port matchmaking x"))
		{
			return matchmaking_screen_key(widget, controller);
		}
		/* (friends: friends_menu.inc) */
		else if (!strncmp(name, "port friends ", 13))
		{
			return friends_menu_event(name + 13, widget, controller, widget_deleted);
		}
		else if (!strcmp(name, "port party x") || !strcmp(name, "port party y"))
		{
			return party_screen_key(widget, !strcmp(name, "port party y"), controller);
		}
		/* (Host Controls: host_controls_menu.inc) */
		else if (!strcmp(name, "port host open"))
		{
			return host_controls_open();
		}
		else if (!strcmp(name, "port host init"))
		{
			return host_controls_initialize();
		}
		else if (!strcmp(name, "port host tab") || !strcmp(name, "port host tab next") ||
			!strcmp(name, "port host tab previous"))
		{
			return host_controls_tab(widget, (short)(!strcmp(name, "port host tab next") ? 1 :
				!strcmp(name, "port host tab previous") ? -1 : 0));
		}
		else if (!strcmp(name, "port host row"))
		{
			return host_controls_row(widget);
		}
		else if (!strcmp(name, "port host left") || !strcmp(name, "port host right"))
		{
			return host_controls_step(widget, (short)(!strcmp(name, "port host left") ? -1 : 1));
		}
		else if (!strcmp(name, "port host action"))
		{
			return host_controls_action(widget);
		}
		else if (!strcmp(name, "port host back"))
		{
			return host_controls_back();
		}
		/* (the computer's lobby: lobby_menu.inc) */
		else if (!strcmp(name, "port lobby team red") || !strcmp(name, "port lobby team blue") ||
			!strcmp(name, "port lobby team toggle"))
		{
			return lobby_board_team(widget, controller, (short)(!strcmp(name, "port lobby team red") ? 0 :
				!strcmp(name, "port lobby team blue") ? 1 : NONE));
		}
		else if (!strcmp(name, "port lobby team click"))
		{
			return lobby_board_click(widget, controller);
		}
		else if (!strcmp(name, "port lobby start"))
		{
			return lobby_board_start();
		}
		else if (!strcmp(name, "port lobby bots load"))
		{
			return lobby_board_bots_load(widget);
		}
		else if (!strcmp(name, "port lobby invite"))
		{
			return lobby_board_invite_copy();
		}
		else if (!strcmp(name, "port lobby settings"))
		{
			return lobby_board_settings(widget, widget_deleted);
		}
		else if (!strcmp(name, "controls screen init") || !strcmp(name, "controls screen defaults"))
		{
			return controls_load(!strcmp(name, "controls screen defaults"));
		}
		else if (!strcmp(name, "controls screen change set"))
		{
			return controls_save();
		}
		else if (!strcmp(name, "controls begin binding"))
		{
			return control_capture_begin(widget);
		}
		else if (!strcmp(name, "player profile list initialize"))
		{
			return profile_list_initialize(widget);
		}
		else if (!strcmp(name, "profile manager select"))
		{
			return profile_choose(controller);
		}
		else if (!strcmp(name, "port lobby open"))
		{
			return lobby_join_reset();
		}
		else if (!strcmp(name, "port map vote open"))
		{
			return map_vote_open();
		}
		else if (!strcmp(name, "port map vote choose"))
		{
			return map_vote_choose(widget);
		}
		else if (!strcmp(name, "port lobby add player"))
		{
			return lobby_add_player();
		}
		else if (!strcmp(name, "port lobby join"))
		{
			return lobby_join_start(widget, controller, widget_deleted);
		}
		else if (!strcmp(name, "port lobby leave"))
		{
			return lobby_leave(widget, event, controller, widget_deleted);
		}
		else if (!strcmp(name, "port lobby player list initialize"))
		{
			return lobby_player_list_initialize(widget);
		}
		else if (!strcmp(name, "port lobby player choose"))
		{
			return lobby_player_choose();
		}
		else if (!strcmp(name, "port lobby preview add"))
		{
			return preview_add(widget, controller, widget_deleted);
		}
		else if (!strcmp(name, "port lobby preview leave"))
		{
			return preview_leave(controller);
		}
		else if (!strcmp(name, "port coop begin"))
		{
			return coop_begin(controller);
		}
		else if (!strcmp(name, "port coop player 2 list initialize"))
		{
			return coop_player2_list_initialize(widget);
		}
		else if (!strcmp(name, "port coop player 2"))
		{
			return coop_player2_choose(controller);
		}
		else if (!strcmp(name, "request del player profile"))
		{
			return profile_delete_request();
		}
		else if (!strcmp(name, "final del player profile"))
		{
			return profile_delete();
		}
		else if (!strcmp(name, "color picker menu initialize"))
		{
			return color_list_initialize(widget);
		}
		else if (!strcmp(name, "color picker select color"))
		{
			return color_choose();
		}
		else if (!strcmp(name, "controls binding slot"))
		{
			controls_screen.slot = (short)!controls_screen.slot;
		}
		else if (!strcmp(name, "campaign menu init"))
		{
			return campaign_menu_initialize(controller);
		}
		else if (!strcmp(name, "campaign menu continue"))
		{
			return campaign_continue(controller);
		}
		else if (!strcmp(name, "initialize sp level list solo"))
		{
			return level_list_initialize(widget, controller);
		}
		else if (!strcmp(name, "solo level set map"))
		{
			return level_choose(widget, controller);
		}
		else if (!strcmp(name, "difficulty item select"))
		{
			return difficulty_start(sibling_index(widget), controller);
		}
		else if (!strcmp(name, "set difficulty"))
		{
			main_set_difficulty(PIN(difficulty_shown(widget), 0, 3));
		}
		else if (!strcmp(name, "load game menu init"))
		{
			return saved_game_list_initialize(widget, controller);
		}
		else if (!strcmp(name, "load game menu activated"))
		{
			return saved_game_continue(controller);
		}
		else if (!strcmp(name, "load game menu delete request"))
		{
			return saved_game_delete_request();
		}
		else if (!strcmp(name, "load game menu delete finish"))
		{
			return saved_game_delete(controller);
		}
		else if (!strcmp(name, "controls back handler") || !strcmp(name, "gamespy back handler") ||
			!strcmp(name, "gamespy dismiss error") || !strcmp(name, "gamespy dismiss filters"))
		{
			ui_widget_port_go_back(widget);
			*widget_deleted = TRUE;
		}
		return TRUE;
	}
}

void pc_menu_game_data_function_invoke(
	struct widget_instance *widget,
	long function)
{
	char const *name = pc_menu_game_data_input_name(function);

	if (!name)
		return;
	/* (a text field whose screen has gone: let go of) */
	if (text_field.row && system_milliseconds() - text_field_shown_time > 500)
		text_field_end(FALSE);
	if (!strncmp(name, "port campaign ", 14))
		campaign_menu_update(widget, name + 14);
	else if (!strcmp(name, "solo map list update"))
		level_list_update(widget);
	else if (!strcmp(name, "mp map list update"))
		map_list_update(widget);
	else if (!strcmp(name, "gt select list update"))
		gametype_list_update(widget);
	else if (!strcmp(name, "server settings update"))
		server_settings_update(widget);
	else if (!strcmp(name, "port password update"))
		password_screen_update(widget);
	else if (!strcmp(name, "gamespy screen update"))
		browser_update(widget);
	else if (!strcmp(name, "gt edit list update"))
		gametype_edit_list_update(widget);
	else if (!strcmp(name, "get edit game settings name"))
		gametype_edit_name(widget);
	else if (!strcmp(name, "game settings lists text update"))
		gametype_option_help(widget);
	else if (!strcmp(name, "mp edit profile set rule text"))
		gametype_engine_name(widget);
	else if (!strcmp(name, "mp prof vehicles update"))
		vehicles_update(widget);
	else if (!strcmp(name, "port lobby preview update"))
		preview_update(widget);
	else if (!strcmp(name, "port lobby board update"))
		lobby_board_update(widget);
	else if (!strcmp(name, "port map vote update"))
		map_vote_update(widget);
	else if (!strcmp(name, "port settings help"))
	{
		sim_rate_row_show(widget);
		settings_help(widget);
	}
	else if (!strcmp(name, "port settings page"))
		settings_page_update(widget);
	else if (!strcmp(name, "controls update menu"))
		controls_update(widget);
	else if (!strcmp(name, "port mods update"))
		mods_update(widget);
	else if (!strcmp(name, "port training update"))
		training_update(widget);
	else if (!strcmp(name, "port update question"))
		update_question_note(widget);
	else if (!strcmp(name, "port connect update"))
		connect_update(widget);
	else if (!strcmp(name, "color picker update"))
		color_list_update(widget);
	else if (!strcmp(name, "3wide player profile list update"))
		profile_list_update(widget);
	else if (!strcmp(name, "load game list update"))
		saved_game_list_update(widget);
	else if (!strcmp(name, "port gamepad layout preview"))
		profile_gamepad_layout(widget);
	else if (!strcmp(name, "port coop friendly fire text"))
		coop_friendly_fire_text(widget);
	else if (!strcmp(name, "port multiplayer status"))
		multiplayer_status_update(widget);
	else if (!strcmp(name, "port party update"))
		party_screen_update(widget);
	else if (!strcmp(name, "port matchmaking update"))
		matchmaking_screen_update(widget);
	else if (!strcmp(name, "port friends update"))
		friends_screen_update(widget);
	else if (!strcmp(name, "port friends code update"))
		friends_add_update(widget);
	else if (!strcmp(name, "port host update"))
		host_controls_update(widget);
	else if (!strcmp(name, "port playlist edit desc"))
		playlist_edit_description(widget);
}
