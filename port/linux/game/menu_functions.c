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
- Key Bindings' page (a page of Settings'): the keyboard and mouse's
  controls, two bindings each, shown a group at a time (controls_update,
  from the page's "port settings page"); "controls begin binding" takes the
  next key or button pressed for the binding left and right choose
  ("controls binding slot"), or the pointer is over or clicked
  (ui_widget_port_take_point, take_click); the page's OK writes them, its
  Defaults shows the defaults. The game's own controls (moving, the
  weapons, action, flashlight, pause) each keep a key: a change that would
  leave one with none is refused.

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

The crash question (port/linux/src/report.c's: the last run closed
unexpectedly), opened over the main menu as the update question is
(pc_menu_crash_question_open, main_menu/crash/crash_screen): REPORT runs
"port crash report", SKIP (and B) "port crash dismiss".

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
#include "interface/ui_widget_game_data_input_functions.h"
#include "main/main.h"
#include "networking/network_game_manager.h"
#include "saved games/player_profile.h"
#include "saved games/game_state.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "text/unicode.h"

#include "halo_menus.h"
#include "controller_buttons.h"
#include "controller_remap.h"
#include "custom_edition_cache.h"
#include "mcc_ui.h"
#include "mcc_pause.h"
#include "mcc_maps.h"
#include "mcc_progress.h"
#include "mcc_menu_text.h"
#include "mcc_share.h"
#include "mcc_share_menu.h"
#include "mcc_cache.h"
#include "custom_edition_maps.h"
#include "game_modes.h"
#include "network_voice.h"
#include "network_chat.h"
#include "network_party.h"
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
#include "menu_search.h"
#include "menu_table.h"
/* (internet play's server browser: the platform layer's) */
#include "../src/p2p.h"
#include "../src/browser_favorites.h"
/* (the server browser's master servers, and Server Setup's publisher key) */
#include "../src/master_list.h"
#include "../src/player_link.h" /* port: port/linux/src/player_link.c */
#include "../src/server_master.h"
#include "../src/report.h"
#include "../src/game_caches.h" /* port: port/linux/src/game_caches.c */
#include "text_filter.h"
/* (the party: party_menu.inc) */
#include "party.h"
/* (matchmaking: matchmaking_menu.inc, PRO 2V2's; matchmaking.c) */
#include "matchmaking.h"
/* (the lobby's bots: lobby_menu.inc) */
#include "bots.h"
#include "basic_training.h"
#include "bungie_net/network/transport.h"

#include <ctype.h>
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
/* each weapon's own first-person offsets (port_config.h) */
#define CONFIG_WEAPON_VALUES 6
int config_viewmodel_weapon(char const *path, double values[CONFIG_WEAPON_VALUES]);
int config_viewmodel_weapon_write(char const *path, double const values[CONFIG_WEAPON_VALUES]);
int config_profile_viewmodel_weapon(char const *folder, char const *path, double values[CONFIG_WEAPON_VALUES]);
int config_profile_viewmodel_weapon_write(char const *folder, char const *path,
	double const values[CONFIG_WEAPON_VALUES]);
/* a player profile's own values of the settings that are each player's
(port_config.h; player_settings.c names its folder) */
int config_per_player(char const *name);
int config_keyboard_setting(char const *name);
int config_profile_text(char const *folder, char const *name, char *text, unsigned int size);
int config_profile_write(char const *folder, char const *name, char const *value);
int player_settings_profile_folder(long profile_index, char *folder, unsigned long size);
long pc_menu_widget_string_list(long definition_index);
boolean ui_widget_port_switch(struct widget_instance *widget, char const *name, boolean *widget_deleted);
char const *config_string(char const *name);
void platform_display_apply(void);
void platform_binding_capture_begin(void);
void platform_binding_capture_cancel(void);
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
#include "menu_widget.h"
#include "menu_engine.h"

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
long pc_menu_setting_values(char const *setting, char const **values, long count);
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
static struct settings_slider const *settings_slider_of(struct widget_instance const *row);
static double settings_slider_value(struct settings_slider const *slider);
static void settings_slider_text(struct settings_slider const *slider, double value, char *text, unsigned int size);
static short settings_slider_stop_nearest(struct settings_slider const *slider);
static boolean settings_slider_text_value(struct settings_slider const *slider, char const *text, double *value);
static char const *settings_slider_setting(struct settings_slider const *slider);

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

/* the settings of Settings' spinners, from their rows in the menus' model
(docs/menu_engine.md): each a choice row's setting, its values its choices'
(and the platform's, as the spinners' are), and its value as it was loaded
kept with the row, not with the spinner's widget definition; made the first
time a row's is asked for, and made again with the next tags, as the widget
definitions' are (menu_tags.c) */
#define SETTINGS_MAXIMUM_CHOICES 512
/* the size of a widget's name as its instance has it: its definition's,
cut short (menu_tags.c's definitions' name[32]: a long key's spinner,
mouse_vertical_sensitivity_spinner, is mouse_vertical_sensitivity_spin) */
#define SETTINGS_WIDGET_NAME_SIZE 32

/* a row's widget's name (its prefix, key and suffix) as its instance has it */
static void settings_widget_name(char *name, size_t size, char const *prefix, char const *key, char const *suffix)
{
	snprintf(name, size < SETTINGS_WIDGET_NAME_SIZE ? size : SETTINGS_WIDGET_NAME_SIZE, "%s%s%s", prefix, key, suffix);
}

static struct
{
	struct halo_menu_model const *model;
	/* (and its read: the menu studio reads it again in the same place) */
	unsigned long reads;
	unsigned long generation;
	struct halo_menu_model_row const *rows[SETTINGS_MAXIMUM_CHOICES];
	struct pc_menu_setting settings[SETTINGS_MAXIMUM_CHOICES];
	long count;
} settings_rows;

static struct pc_menu_setting *settings_row_setting(struct halo_menu_model_row const *row)
{
	struct halo_menu_model const *model = menu_engine_model();
	struct pc_menu_setting *setting;
	long index, choice;

	if (!model || !row || !row->setting || strcmp(row->kind, "choice"))
		return NULL;
	if (settings_rows.model != model || settings_rows.reads != halo_menus_model_reads())
	{
		settings_rows.model = model;
		settings_rows.reads = halo_menus_model_reads();
		settings_rows.count = 0;
	}
	if (settings_rows.generation != cache_files_tags_generation())
	{
		settings_rows.generation = cache_files_tags_generation();
		settings_rows.count = 0;
	}
	for (index = 0; index < settings_rows.count; index++)
	{
		if (settings_rows.rows[index] == row)
			return &settings_rows.settings[index];
	}
	if (settings_rows.count == SETTINGS_MAXIMUM_CHOICES)
		return NULL;
	index = settings_rows.count++;
	settings_rows.rows[index] = row;
	setting = &settings_rows.settings[index];
	memset(setting, 0, sizeof(*setting));
	setting->definition_index = NONE;
	setting->setting = row->setting;
	setting->loaded_index = NONE;
	for (choice = 0; choice < row->choice_count && choice < MAXIMUM_STRINGS; choice++)
		setting->values[choice] = model->choices[row->first_choice + choice].value;
	/* (and those the platform adds, as the spinners' are: menu_tags.c) */
	setting->value_count = pc_menu_setting_values(row->setting, setting->values, choice);
	return setting;
}

/* a spinner's setting: its row's in the model (the spinner <key>_spinner of
the row op_<key>), else its widget definition's (the lobby's, whose screen
the model has not: menu_tags.c's) */
static struct pc_menu_setting *settings_setting_of(struct widget_instance *spinner)
{
	struct halo_menu_model_row const *row;
	struct pc_menu_setting *setting = NULL;
	char name[80];

	if (!spinner)
		return NULL;
	row = spinner->parent ? menu_engine_model_row_of(spinner->parent) : NULL;
	if (row)
		settings_widget_name(name, sizeof(name), "", row->key, "_spinner");
	if (row && spinner->name && !strcmp(spinner->name, name))
		setting = settings_row_setting(row);
	return setting ? setting : pc_menu_setting_get(spinner->definition_tag_index);
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

/* ---------- Advanced Graphics' RENDER RESOLUTION row: two settings as one
spinner, a setting named "display.render_resolution" (tools/port_settings.py):
"original", display.resolution_scaling's Xbox 640x480, or the factor of
display.supersampling's that "native" draws at (1, the screen's pixels, to
2); written as both (render_resolution_write) */

#define RENDER_RESOLUTION_SETTING "display.render_resolution"

static boolean render_resolution_text(char *text, unsigned int size, boolean default_value)
{
	char scaling[32];

	if (!(default_value ? config_default("display.resolution_scaling", scaling, sizeof(scaling)) :
		config_text("display.resolution_scaling", scaling, sizeof(scaling))))
	{
		return FALSE;
	}
	if (!strcmp(scaling, "original"))
	{
		snprintf(text, size, "%s", "original");
		return TRUE;
	}
	return default_value ? config_default("display.supersampling", text, size) != 0 :
		config_text("display.supersampling", text, size) != 0;
}

/* (each setting written only where it changes, so that one at its default
stays at it) */
static boolean render_resolution_write(char const *value)
{
	char scaling[32] = "", factor[32] = "";
	double number, current;
	boolean original = !strcmp(value, "original");

	config_text("display.resolution_scaling", scaling, sizeof(scaling));
	if (strcmp(scaling, original ? "original" : "native") &&
		!config_write("display.resolution_scaling", original ? "original" : "native"))
	{
		return FALSE;
	}
	if (original)
		return TRUE;
	config_text("display.supersampling", factor, sizeof(factor));
	if (!text_is_number(value, &number))
		return FALSE;
	if (text_is_number(factor, &current) && current == number)
		return TRUE;
	return config_write("display.supersampling", value) != 0;
}

/* ---------- First Person's THIS WEAPON rows (Weapon Position's): the
offsets of the weapon the player who opened Settings holds, by its tag's
path (config.toml's [display.viewmodel_weapon."<path>"], view_fov.c), as
settings named "weapon.x", "weapon.y", "weapon.z", "weapon.pitch",
"weapon.yaw" and "weapon.roll"; shown in a game, with a weapon held
(the model's weapon_held: settings_rows_conditions) */

#define SETTINGS_WEAPON_PREFIX "weapon."

static char const *const settings_weapon_keys[CONFIG_WEAPON_VALUES] = { "x", "y", "z", "pitch", "yaw", "roll" };
/* the weapon the rows are for (the page's last frame's); "" for none */
static char settings_weapon[256];

static short settings_weapon_value(char const *name)
{
	short value;

	if (strncmp(name, SETTINGS_WEAPON_PREFIX, sizeof(SETTINGS_WEAPON_PREFIX) - 1))
		return NONE;
	for (value = 0; value < CONFIG_WEAPON_VALUES; value++)
	{
		if (!strcmp(name + sizeof(SETTINGS_WEAPON_PREFIX) - 1, settings_weapon_keys[value]))
			return value;
	}
	return NONE;
}

static boolean settings_profile_folder(char *folder, unsigned long size);

/* the weapon's offsets: the profile's edited, its own (each a player's, as
the settings below are), else config.toml's */
static void settings_weapon_values(double values[CONFIG_WEAPON_VALUES])
{
	char folder[1024];

	if (!settings_profile_folder(folder, sizeof(folder)) ||
		!config_profile_viewmodel_weapon(folder, settings_weapon, values))
	{
		config_viewmodel_weapon(settings_weapon, values);
	}
}

static boolean settings_weapon_text(short value, char *text, unsigned int size, boolean default_value)
{
	double values[CONFIG_WEAPON_VALUES];

	if (!settings_weapon[0])
		return FALSE;
	if (default_value)
		values[value] = 0.0;
	else
		settings_weapon_values(values);
	snprintf(text, size, "%g", values[value]);
	return TRUE;
}

static boolean settings_weapon_write(short value, char const *text)
{
	double values[CONFIG_WEAPON_VALUES];
	char folder[1024];

	if (!settings_weapon[0])
		return FALSE;
	settings_weapon_values(values);
	values[value] = atof(text);
	if (settings_profile_folder(folder, sizeof(folder)))
		return config_profile_viewmodel_weapon_write(folder, settings_weapon, values) != 0;
	return config_viewmodel_weapon_write(settings_weapon, values) != 0;
}

/* ---------- the settings that are each player's (port_config.c's table
marks them _scope_player: the field of view, First Person's): Settings
shows and sets the values of the profile it edits, the player's who opened
it (pc_menu_profile_edit_begin), their profile's own, kept in its folder;
config.toml's where it has none of its own, or no profile is edited, and
there they are set (every player without a value of their own has it) */

/* the folder of the profile being edited, FALSE with none */
static boolean settings_profile_folder(char *folder, unsigned long size)
{
	return player_ui_get_edit_player_profile() &&
		player_settings_profile_folder(player_ui_get_edit_profile_index(), folder, size);
}

/* the folder of the profile a setting that is each player's is set in: the
profile being edited's; the keyboard and mouse's, which only player 1
plays with, player 1's (config_keyboard_setting), whoever opened Settings;
and its name (whose: ROW_TEXT_LENGTH, or NULL); FALSE with none */
static boolean settings_setting_folder(char const *name, char *folder, unsigned long size, wchar_t *whose)
{
	struct player_profile profile, *edited;
	long profile_index;

	if (config_keyboard_setting(name))
	{
		profile_index = player_ui_get_active_player_profile_index(0);
		if (!player_settings_profile_folder(profile_index, folder, size))
			return FALSE;
		/* (the profile player 1 holds, not read again from its file: each frame
		the panel shows it) */
		if (whose)
		{
			player_ui_get_active_player_profile(0, &profile);
			ustrncpy(whose, profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
			whose[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
		}
		return TRUE;
	}
	if (!settings_profile_folder(folder, size))
		return FALSE;
	if (whose && (edited = player_ui_get_edit_player_profile()) != NULL)
	{
		ustrncpy(whose, edited->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
		whose[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
	}
	return TRUE;
}

/* a setting's value as text, or (default_value) its default: config.toml's,
or the profile's: its controller's, or its own of a setting that is each
player's */
static boolean setting_text(char const *name, char *text, unsigned int size, boolean default_value)
{
	char folder[1024];
	short index;

	if (!default_value && config_per_player(name) && settings_setting_folder(name, folder, sizeof(folder), NULL) &&
		config_profile_text(folder, name, text, size))
	{
		return TRUE;
	}
	if (settings_weapon_value(name) != NONE)
		return settings_weapon_text(settings_weapon_value(name), text, size, default_value);
	if (!strcmp(name, RENDER_RESOLUTION_SETTING))
		return render_resolution_text(text, size, default_value);
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
	char folder[1024];
	short index;

	if (config_per_player(name) && settings_setting_folder(name, folder, sizeof(folder), NULL))
		return config_profile_write(folder, name, value);
	if (settings_weapon_value(name) != NONE)
		return settings_weapon_write(settings_weapon_value(name), value);
	if (!strcmp(name, RENDER_RESOLUTION_SETTING))
		return render_resolution_write(value);
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
	struct pc_menu_setting *setting = settings_setting_of(widget);
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
	struct pc_menu_setting *setting = settings_setting_of(widget);

	if (spinner_item(widget))
		return TRUE;
	if (!setting || setting->loaded_index == NONE || widget->parameters.list.selected_index < 0 ||
		widget->parameters.list.selected_index >= setting->value_count)
		return FALSE;
	if (widget->parameters.list.selected_index == setting->loaded_index)
		return TRUE;
	if (!setting_write(setting->setting, setting->values[widget->parameters.list.selected_index]))
	{
		platform_log("menus: could not write %s", setting->setting);
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

/* the local player whose profile Settings edits: in a game, the one whose
controller opened it (split screen's players each their own); else player
1 */
static short settings_local_player;

static void settings_local_player_set(short controller)
{
	short local_player_index = NONE;

	if (!main_menu_is_active() && controller >= 0 && controller < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS)
	{
		local_player_index = player_ui_get_single_player_local_player_from_controller(controller);
		/* (a split screen game's players: by their controllers) */
		if (local_player_index == NONE && player_ui_get_active_player_profile_index(controller) != NONE)
			local_player_index = controller;
	}
	settings_local_player = local_player_index == NONE ? 0 : local_player_index;
}

/* begins editing the profile of the player Settings is for
(settings_local_player): player 1's as the campaign has it, another's
their own; FALSE if there is none */
boolean pc_menu_profile_edit_begin(void)
{
	struct player_profile profile;
	long profile_index;

	if (main_menu_is_active())
		settings_local_player = 0;
	if (settings_local_player > 0)
	{
		profile_index = player_ui_get_active_player_profile_index(settings_local_player);
		if (profile_index == NONE || !player_profile_get(profile_index, &profile))
			return FALSE;
		player_ui_begin_editing_profile(profile_index);
		return TRUE;
	}
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
	settings_local_player_set(event_controller(widget, event));
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
	/* player 1's saved game's level, else NONE, and its difficulty, read
	when a screen opens (campaign_levels_read): reading the saved game reads
	and checks the whole of its file, too long for each frame */
	short saved_level;
	short saved_difficulty;
	/* the level and saved game the lists' descriptions last showed */
	short shown_level;
	short shown_saved_game;
	/* Load Game's */
	struct campaign_saved_game saved_games[MAXIMUM_ROWS];
	short saved_game_count;
	short saved_game_to_delete;
} campaign;

/* MCC checkpoint identities are independent of the stock campaign's level
 * numbers. Only MCC rows use this additional storage. */
static char mcc_saved_game_names[MAXIMUM_ROWS][256];

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
buffer of at least length characters: grown to it each time (the pool
keeps a block already that large), since one box is given different
lengths (Choose Mission's panel: a fitted line, 64, on HALO, 256 on
CUSTOM, #553) and the game sizes a string list's text to the string; the
copy pads to length, so a buffer made smaller earlier was overrun, and
the pool's check failed as the screen was freed */
static void text_set_length(struct widget_instance *text_box, wchar_t const *text, short length)
{
	wchar_t *buffer;

	if (!text_box)
		return;
	/* (kept if it cannot grow: the pool keeps the old block) */
	buffer = ui_widget_realloc(text_box->parameters.text_box.text, length * sizeof(wchar_t), __FILE__, __LINE__);
	if (!buffer)
		return;
	text_box->parameters.text_box.text = buffer;
	ustrncpy(buffer, text, length - 1);
	buffer[length - 1] = 0;
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
static boolean coop_host_choosing(void);

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
	/* (none either while co-op's HOST chooses its mission: a saved game is
	the player's own, coop_menu.inc) */
	if (player_spawn_count >= 2 || coop_host_choosing() ||
		!ui_widget_port_saved_game(&map_name, &campaign.saved_level, &difficulty))
	{
		campaign.saved_level = NONE;
	}
	campaign.saved_difficulty = campaign.saved_level != NONE ? difficulty : NONE;
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

/* player 1's profile (the active one, else the one used last: the main menu
clears the active ones), NULL for none */
static struct player_profile const *profile_current(void)
{
	/* (the profile read from its file at most once a second) */
	static struct player_profile profile;
	static long read_index = NONE;
	static unsigned long read_time;
	static boolean read_good;
	long index = player_ui_get_active_player_profile_index(0);

	if (index != NONE)
	{
		player_ui_get_active_player_profile(0, &profile);
		read_index = NONE;
	}
	else if ((index = player_ui_get_player1_last_used_profile_index()) == NONE)
		return NULL;
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
			return NULL;
	}
	return &profile;
}

/* player 1's profile's name, on the descriptions' current_profile_name */
static void profile_name_show(struct widget_instance *description)
{
	struct player_profile const *profile = description ? profile_current() : NULL;

	if (profile)
		text_set(named(description, "current_profile_name", 0), profile->player_name);
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
static boolean mcc_saved_game_read(char name[256], short *difficulty)
{
	boolean corrupted = FALSE;

	name[0] = 0;
	if (player_ui_get_active_player_profile_index(0) == NONE ||
		!game_state_test_persistent_storage(name, difficulty, &corrupted) ||
		!mcc_level_name(name))
	{
		name[0] = 0;
		return FALSE;
	}
	*difficulty = (short)PIN(*difficulty, 0, NUMBER_OF_GAME_DIFFICULTY_LEVELS - 1);
	return TRUE;
}

static boolean campaign_continue(short controller)
{
	struct player_profile profile;
	char const *map_name;
	short level, difficulty;

	{
		char mcc_name[256];
		if (mcc_saved_game_read(mcc_name, &difficulty))
		{
			if (!campaign_profile(controller, &profile) || !mcc_cache_require(mcc_name, 0))
				return campaign_fail();
			campaign_start(mcc_name, difficulty, controller);
			return TRUE;
		}
	}
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
	MAP_KIND_MCC_SINGLEPLAYER,
	MAP_KIND_MCC_MULTIPLAYER,
	NUMBER_OF_MAP_KINDS
};

/* the rows after the chooser */
#define MAP_KIND_ROWS 10

static void visible_set(struct widget_instance *widget, boolean visible);
/* (party_menu.inc's) */
static void party_screen_noted(struct widget_instance *widget);
static void party_hub_note(wchar_t *text, short size);
static void party_hub_status(wchar_t *headline, short headline_size, wchar_t *detail, short detail_size);
/* (friends_menu.inc's) */
static void friends_hub_note(wchar_t *text, short size);
static void friends_hub_status(wchar_t *headline, short headline_size, wchar_t *detail, short detail_size);
short ui_widget_port_multiplayer_maps(char const *const **names, short *last_used);

static boolean map_kind_singleplayer(short kind)
{
	if (kind == MAP_KIND_MCC_SINGLEPLAYER)
		return TRUE;
	return kind == MAP_KIND_SINGLEPLAYER || kind == MAP_KIND_CUSTOM_SINGLEPLAYER;
}

static boolean map_kind_mcc(short kind)
{
	return kind == MAP_KIND_MCC_SINGLEPLAYER || kind == MAP_KIND_MCC_MULTIPLAYER;
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

	/* MCC folder descriptions have no map display index. */
	visible_set(named(description, "mcc_folder_name", 0), FALSE);
	visible_set(named(description, "mcc_folder_help", 0), FALSE);
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

/* MCC has its own catalog, selection state and namespace. Its name and
description use its reserved display indices; an absent screenshot uses
the stock unknown-map image without entering the CE picture provider. */
static struct
{
	short first, chosen;
	boolean campaign;
} mcc_solo_list;

static struct mcc_map_browser mcc_solo_browsers[2];

static void mcc_browser_focus(struct widget_instance *list, struct mcc_map_browser *browser,
	short *first, short *chosen)
{
	*chosen = browser->chosen;
	*first = map_kind_first(*chosen, browser->count);
	if (browser->count)
		map_kind_focus(list, *first, *chosen);
	else
		focus_row(list, 0);
}

static void mcc_map_description_show(struct widget_instance *description, short index)
{
	struct widget_instance *picture, *badge;
	short marker;
	long profile = player_ui_get_active_player_profile_index(0);
	unsigned flags = 0;

	map_description_show(description, NONE, index == NONE ? NONE : (short)(MCC_MAPS_FIRST_DISPLAY_INDEX + index));
	picture = named(description, "mp_map_right_pic", 0);
	if (picture)
		picture->animation.current_frame_index = index == NONE ? 13 : MCC_MAPS_FIRST_DISPLAY_INDEX + index;
	if (profile == NONE)
		profile = player_ui_get_player1_last_used_profile_index();
	if (index != NONE && mcc_maps_campaign(index))
		flags = mcc_progress_menu_flags(profile, mcc_maps_level_name(index), system_milliseconds());
	for (marker = 0; marker < 3; marker++)
	{
		badge = named(description, "difficulty_indicator", marker);
		if (badge)
		{
			badge->visible = (flags & (1u << (marker + 1))) != 0;
			badge->animation.current_frame_index = marker + 1;
		}
	}
}

static void mcc_solo_map_text(short row, wchar_t *text)
{
	mcc_map_browser_text(&mcc_solo_browsers[mcc_solo_list.campaign], row, text);
	mcc_menu_text_row(text);
}

static void mcc_browser_description(struct widget_instance *description, struct mcc_map_browser *browser)
{
	short index = mcc_map_browser_index(browser, browser->chosen);
	wchar_t text[ROW_TEXT_LENGTH];

	visible_set(named(description, "mp_map_right_item", 0), TRUE);
	mcc_map_description_show(description, index);
	if (index == NONE)
	{
		mcc_map_browser_text(browser, browser->chosen, text);
		text_set(named(description, "mcc_folder_name", 0), text);
		text_set(named(description, "mcc_folder_help", 0), browser->count ?
			L"Open folder to\r\nchoose a map." : L"No MCC maps found\r\nin this category.");
		visible_set(named(description, "mcc_folder_name", 0), TRUE);
		visible_set(named(description, "mcc_folder_help", 0), TRUE);
	}
}

static boolean mcc_solo_folder_choose(struct widget_instance *list)
{
	struct mcc_map_browser *browser = &mcc_solo_browsers[mcc_solo_list.campaign];

	if (!mcc_map_browser_enter(browser, mcc_solo_list.chosen))
		return FALSE;
	mcc_browser_focus(list, browser, &mcc_solo_list.first, &mcc_solo_list.chosen);
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

static void mcc_solo_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct mcc_map_browser *browser = &mcc_solo_browsers[mcc_solo_list.campaign];
	short row = map_kind_rows_update(list, &mcc_solo_list.first, browser->count, mcc_solo_map_text);

	if (row != NONE)
	{
		mcc_solo_list.chosen = row;
		mcc_map_browser_select(browser, row);
	}
	mcc_browser_description(description, browser);
	profile_name_show(description);
}

static boolean mcc_solo_choose(short controller)
{
	short index = mcc_map_browser_index(&mcc_solo_browsers[mcc_solo_list.campaign], mcc_solo_list.chosen);
	char const *level_name = mcc_maps_level_name(index);
	struct player_profile profile;

	if (!level_name || !campaign_profile(controller, &profile))
		return campaign_fail();
	if (mcc_maps_campaign(index))
	{
		main_set_map_name(level_name);
		main_defer_map_map_change();
		return TRUE;
	}
	campaign_start(level_name, main_get_difficulty(), controller);
	return FALSE;
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

/* Handle Back explicitly: a failed handler would propagate B to the
screen's default back action even after returning to the package list. */
static boolean mcc_solo_back(struct widget_instance *list, boolean *widget_deleted)
{
	struct widget_instance *child = named(list, "solo_level_select_list", 0);

	if (child)
		list = child;
	if (map_kind_mcc(level_list.kind) && mcc_map_browser_back(&mcc_solo_browsers[mcc_solo_list.campaign]))
	{
		mcc_browser_focus(list, &mcc_solo_browsers[mcc_solo_list.campaign],
			&mcc_solo_list.first, &mcc_solo_list.chosen);
		ui_play_audio_feedback_sound(SOUND_BACK);
		return TRUE;
	}
	ui_play_audio_feedback_sound(SOUND_BACK);
	ui_widget_port_go_back(list);
	*widget_deleted = TRUE;
	return TRUE;
}

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
	case MAP_KIND_MCC_SINGLEPLAYER:
		return mcc_maps_type_count(TRUE);
	case MAP_KIND_MCC_MULTIPLAYER:
		return mcc_maps_type_count(FALSE);
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
	short mcc_kind = level_list.kind;

	mcc_maps_rescan();
	mcc_solo_list.first = mcc_solo_list.chosen = 0;

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
	/* MCC keeps its folder when the difficulty screen recreates this list. */
	if (map_kind_mcc(mcc_kind))
	{
		level_list.kind = mcc_kind;
		mcc_solo_list.campaign = mcc_kind == MAP_KIND_MCC_SINGLEPLAYER;
		mcc_map_browser_open(&mcc_solo_browsers[mcc_solo_list.campaign], mcc_solo_list.campaign);
		map_kind_set(list, mcc_kind);
		mcc_browser_focus(list, &mcc_solo_browsers[mcc_solo_list.campaign],
			&mcc_solo_list.first, &mcc_solo_list.chosen);
	}
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
		if (map_kind_mcc(kind))
		{
			boolean chooser = focused_row(list) == 0;

			mcc_solo_list.campaign = kind == MAP_KIND_MCC_SINGLEPLAYER;
			mcc_map_browser_open(&mcc_solo_browsers[mcc_solo_list.campaign], mcc_solo_list.campaign);
			mcc_browser_focus(list, &mcc_solo_browsers[mcc_solo_list.campaign],
				&mcc_solo_list.first, &mcc_solo_list.chosen);
			if (chooser)
				focus_row(list, 0);
		}
		level_list.kind = kind;
		level_list.first = level_list.chosen = 0;
	}
	if (map_kind_mcc(kind))
	{
		mcc_solo_list_update(list);
		return;
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

	if (map_kind_mcc(level_list.kind))
		return mcc_solo_choose(controller);

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
	if (mcc_maps_level_campaign(map_name))
	{
		campaign_start(map_name, PIN(difficulty, 0, 3), controller);
		return TRUE;
	}
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
	memset(mcc_saved_game_names, 0, sizeof(mcc_saved_game_names));
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
		if (mcc_saved_game_read(mcc_saved_game_names[campaign.saved_game_count], &saved_game->difficulty))
		{
			saved_game->profile_index = profiles[index];
			saved_game->level = NONE;
			ustrncpy(saved_game->profile_name, profile.player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
			saved_game->profile_name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
			campaign.saved_game_count++;
			continue;
		}
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

		if (mcc_saved_game_names[campaign.shown_saved_game][0])
		{
			short index = mcc_maps_find(mcc_saved_game_names[campaign.shown_saved_game]);
			struct widget_instance *picture = named(description, "load_level_right_pic", 0);
			level_description(description, "load_level",
				index == NONE ? LEVEL_UNAVAILABLE : MCC_MAPS_FIRST_DISPLAY_INDEX + index,
				FALSE, NULL, saved_game->difficulty);
			if (picture) picture->animation.current_frame_index = LEVEL_UNAVAILABLE;
			profile_name_show(description);
			return;
		}
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
	if (mcc_saved_game_names[campaign.shown_saved_game][0])
	{
		char const *name = mcc_saved_game_names[campaign.shown_saved_game];
		if (!mcc_cache_require(name, 0))
			return campaign_fail();
		campaign_start(name, saved_game->difficulty, controller);
		return TRUE;
	}
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
setting in the screen, by its row in the screen's model (each choice row's
of a setting, in the rows' order, which is the widgets'), or for a screen
the model has none of, by its widgets' definitions */
static void settings_each_definition(struct widget_instance *widget,
	boolean (*visit)(struct widget_instance *spinner, struct pc_menu_setting *setting))
{
	struct widget_instance *child;

	for (child = widget->child; child; child = child->next)
	{
		struct pc_menu_setting *setting = child->type == 2 && !spinner_item(child) ?
			pc_menu_setting_get(child->definition_tag_index) : NULL;

		if (setting)
			visit(child, setting);
		else
			settings_each_definition(child, visit);
	}
}

static void settings_search_each(boolean (*visit)(struct widget_instance *spinner,
	struct pc_menu_setting *setting));

static void settings_each(struct widget_instance *widget, boolean (*visit)(struct widget_instance *spinner,
	struct pc_menu_setting *setting))
{
	struct halo_menu_model_screen const *screen = menu_engine_model_screen_of(widget);
	struct halo_menu_model const *model = menu_engine_model();
	boolean rows = FALSE;
	long index;

	/* (the search's: its rows found, each its own page's) */
	if (menu_engine_search_screen(widget))
	{
		settings_search_each(visit);
		return;
	}
	for (index = 0; screen && model && index < screen->row_count; index++)
	{
		struct halo_menu_model_row const *row = &model->rows[screen->first_row + index];
		struct pc_menu_setting *setting = settings_row_setting(row);
		struct widget_instance *spinner;
		char name[80];

		if (!setting)
			continue;
		rows = TRUE;
		settings_widget_name(name, sizeof(name), "", row->key, "_spinner");
		spinner = named(widget, name, 0);
		if (spinner && spinner->type == 2 && !spinner_item(spinner))
			visit(spinner, setting);
	}
	if (!rows)
		settings_each_definition(widget, visit);
}

static struct widget_instance *screen_of(struct widget_instance *widget)
{
	while (widget->parent)
		widget = widget->parent;
	return widget;
}

/* whether the widget's screen is its machine's first local player's, whose
alone the host's rows of the pause menu are (HOST CONTROLS, END GAME,
CHAPTERS: a split screen's player 1; network_host_controls_local_player),
the map's screens being every player's alike */
static boolean host_rows_player(struct widget_instance *widget)
{
	return widget && network_host_controls_local_player(screen_of(widget)->local_player_index);
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

/* a row dimmed, as a row whose model's enabled does not hold is
(settings_rows_conditions: Frame Rate & Sync's Variable Refresh on the Mac,
whose Metal and ProMotion set it, SET BY MACOS; its VRR Frame Cap while VRR
is not on), and as Tick Rate's locked row is (sim_rate_row_show): at 0.4,
its value held; but the row keeps the cursor, so that its panel says why */
int platform_vrr_detected(void);

static void row_dim(struct widget_instance *row, struct widget_instance *spinner, boolean dim)
{
	struct pc_menu_setting *setting;

	if (!row || !spinner)
		return;
	/* (the row still takes the cursor, so that its panel says why; its
	value held where it was loaded while it is dimmed: a press changes
	nothing) */
	setting = settings_setting_of(spinner);
	if (dim && setting && setting->loaded_index >= 0 && setting->loaded_index < setting->value_count)
		spinner->parameters.list.selected_index = setting->loaded_index;
	if (spinner->disabled == dim)
		return;
	spinner->disabled = dim;
	row->alpha_modifier = dim ? 0.4f : 1.0f;
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
	struct pc_menu_setting *setting = settings_setting_of(spinner);
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
/* Weapon Position's THIS WEAPON rows and RESET THIS WEAPON, for the weapon
the player who opened Settings holds: shown in a game, with one held (the
model's weapon_held: settings_rows_conditions), its name in RESET's value
(its tag's last name: "ASSAULT RIFLE"); none on the main menu, where the
panel says how to set them */
boolean viewmodel_held_weapon(short local_player_index, char *path, unsigned int size);
static void discord_text_set(struct widget_instance *text_box, char const *text);
static boolean settings_row_is(struct widget_instance const *row, char const *key);
static struct widget_instance *settings_part(struct widget_instance *widget, char const *role);
static boolean settings_is_part(struct widget_instance const *widget, char const *role);

/* a row of the list's screen's model, by its key: its widget (op_<key>),
NULL if the model has not the row or the screen not its widget (its
platform has it not) */
static struct widget_instance *settings_model_row(struct widget_instance *list, char const *key)
{
	struct halo_menu_model_screen const *screen = menu_engine_model_screen_of(list);
	struct halo_menu_model const *model = menu_engine_model();
	char name[80];
	long index;

	for (index = 0; screen && model && index < screen->row_count; index++)
	{
		if (!strcmp(model->rows[screen->first_row + index].key, key))
		{
			snprintf(name, sizeof(name), "op_%s", key);
			return named(list, name, 0);
		}
	}
	return NULL;
}

/* a status row's value (its <key>_value) written, the game's words: the
status rows' writers (tools/port_settings.py's SETTINGS_STATUS_ROWS) */
static void settings_status_set(struct widget_instance *list, char const *key, char const *text)
{
	struct widget_instance *row = settings_model_row(list, key);
	char name[80];

	if (!row)
		return;
	snprintf(name, sizeof(name), "%s_value", key);
	discord_text_set(named(row, name, 0), text);
}

static void viewmodel_weapon_rows_show(struct widget_instance *list)
{
	struct widget_instance *reset = settings_model_row(list, "viewmodel_weapon_reset");
	short local_player_index;
	char path[sizeof(settings_weapon)];
	boolean held;

	if (!reset)
		return;
	/* (the weapon of the player whose profile Settings edits) */
	local_player_index = settings_local_player;
	held = !main_menu_is_active() && viewmodel_held_weapon(local_player_index, path, sizeof(path));
	snprintf(settings_weapon, sizeof(settings_weapon), "%s", held ? path : "");
	if (held)
	{
		char const *name = strrchr(settings_weapon, '\\');
		char shown[ROW_TEXT_LENGTH];
		short length;

		name = name ? name + 1 : settings_weapon;
		for (length = 0; name[length] && length < (short)sizeof(shown) - 1; length++)
			shown[length] = name[length] == '_' ? ' ' : (char)toupper((unsigned char)name[length]);
		shown[length] = 0;
		settings_status_set(list, "viewmodel_weapon_reset", shown);
	}
}

/* Gyro & Flick Stick's rows (port/linux/src/xinput_sdl.c): GYRO says whether
a controller with a gyroscope is connected (or only Steam Input's virtual
ones, which pass none), and the gyroscope's rows are shown only then (the
model's conditions: settings_rows_conditions); CALIBRATE's value says it
measures, then that it is done */
int halo_gyro_found(void);
int halo_gyro_calibrating(void);
void halo_gyro_calibrate(void);
static boolean gyro_calibrated;

static void gyro_rows_show(struct widget_instance *list)
{
	int gyro;

	if (!settings_model_row(list, "gyro_found"))
		return;
	gyro = halo_gyro_found();
	settings_status_set(list, "gyro_found", gyro == 2 ? "CONNECTED" : gyro == 1 ? "STEAM INPUT HAS IT" :
		"NONE CONNECTED");
	if (halo_gyro_calibrating())
		gyro_calibrated = TRUE;
	settings_status_set(list, "gyro_calibrate", halo_gyro_calibrating() ? "HOLD STILL" : gyro_calibrated ? "DONE" :
		"");
}

static void sim_rate_row_show(struct widget_instance *list)
{
	struct widget_instance *row = named(list, "op_sim_rate", 0);
	struct widget_instance *spinner = named(list, "sim_rate_spinner", 0);
	struct pc_menu_setting *setting = settings_setting_of(spinner);
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

/* the rows a game's host holds at a value for every player, whatever their
own settings, which stay as they are and are theirs again after it: Mouse's
INSTANT AIM in a Pro 2v2 Classic game (network_standard_direct_camera_allowed),
and Video's DYNAMIC LIGHTS, SHIELD GLOW and GLOW INTENSITY, Weapon & Body's
OVERSHIELD and Mouse's MOUSE AIM ASSIST in either kind's
(network_standard_enhanced_lights_allowed: the Pro 2v2 rules; the shields'
are the host's stock, network_shield_effects.c; the mouse's assist the host
blocks, network_aim_assist.c). FIELD OF VIEW, a slider, is not held: its
note says a Pro 2v2 game caps it (view_fov.c).
Each shown at that value and dimmed, its value held (setting_changed_save
passes over a disabled spinner); its note says why. The row keeps the
cursor, so that its note is read */
static struct
{
	char const *row;
	char const *spinner;
	char const *value;
	boolean (*allowed)(void);
} const settings_held_rows[] =
{
	{ "op_direct_camera", "direct_camera_spinner", "false", network_standard_direct_camera_allowed },
	{ "op_dynamic_lights", "dynamic_lights_spinner", "classic", network_standard_enhanced_lights_allowed },
	{ "op_shield_glow", "shield_glow_spinner", "false", network_standard_enhanced_lights_allowed },
	{ "op_shield_glow_intensity", "shield_glow_intensity_spinner", "default",
		network_standard_enhanced_lights_allowed },
	{ "op_overshield_effect", "overshield_effect_spinner", "false", network_standard_enhanced_lights_allowed },
	/* (the host's: network_aim_assist.c, a Pro host's always) */
	{ "op_mouse_aim_assist", "mouse_aim_assist_spinner", "false", network_standard_enhanced_lights_allowed },
};

static void settings_held_rows_show(struct widget_instance *list)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(settings_held_rows); index++)
	{
		struct widget_instance *row = named(list, settings_held_rows[index].row, 0);
		struct widget_instance *spinner = named(list, settings_held_rows[index].spinner, 0);
		struct pc_menu_setting *setting = settings_setting_of(spinner);
		boolean held = !settings_held_rows[index].allowed();

		if (!row || !setting || spinner->disabled == held)
			continue;
		spinner->disabled = held;
		row->alpha_modifier = held ? 0.4f : 1.0f;
		if (held)
			spinner->parameters.list.selected_index = setting_value_index(setting, settings_held_rows[index].value);
		else if (setting->loaded_index >= 0 && setting->loaded_index < setting->value_count)
			spinner->parameters.list.selected_index = setting->loaded_index;
	}
}

/* (Controller's Button Remapping, a page of Settings') */
#include "controller_remap_menu.inl"

/* Key Bindings' page, as Button Remapping's (its code below, with the
keyboard and mouse's controls) */
static boolean controls_page(struct widget_instance *screen);
static void controls_page_update(struct widget_instance *list);
static boolean controls_page_save(struct widget_instance *screen);
static boolean controls_page_cancel(struct widget_instance *screen);
static void controls_page_defaults(struct widget_instance *screen);
static boolean controls_panel(struct widget_instance *row, wchar_t *name, wchar_t *value, wchar_t const **help);

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
that are for this game (their conditions in the model:
settings_rows_conditions), packs them at the steps
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
#define SETTINGS_PANEL_VALUE_UNDER_NAME 14
#define SETTINGS_PANEL_TEXT_UNDER_STILLS 122
#define SETTINGS_PANEL_TEXT_UNDER_PICTURE 146
#define SETTINGS_PANEL_TEXT_LENGTH 512
/* the panels' bottom (tools/port_settings.py's SETTINGS_PANEL_BOTTOM), and
the room kept clear above it, inside its rim */
#define SETTINGS_PANEL_BOTTOM 400
#define SETTINGS_PANEL_TEXT_MARGIN 6
#define SETTINGS_SHARED "pc\\main_menu\\settings\\"
/* the link rows' sign, after their labels (U+203A) */
#define SETTINGS_LINK_SIGN 0x203A

/* the tabs, in order: the model's screens laid out as tabs
(tools/port_settings.py's PAGES' tabs), the tab'th (NULL: none) */
static struct halo_menu_model_screen const *settings_tab_screen(short tab)
{
	struct halo_menu_model const *model = menu_engine_model();
	long index;

	for (index = 0; model && index < model->screen_count && tab >= 0; index++)
	{
		struct halo_menu_model_screen const *screen = &model->screens[index];

		if (screen->layout && !strcmp(screen->layout, "tab") && tab-- == 0)
			return screen;
	}
	return NULL;
}

static short settings_tab_count(void)
{
	short count = 0;

	while (settings_tab_screen(count))
		count++;
	return count;
}

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
	struct halo_menu_model_screen const *model = menu_engine_model_screen_of(screen);
	short tab;

	for (tab = 0; model && settings_tab_screen(tab); tab++)
	{
		if (settings_tab_screen(tab) == model)
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
		/* (MCC's pause menu's Settings keep the profile they opened: mcc_ui.c) */
		if (finish || !mcc_ui_settings_profile_reopen(profile_index))
		{
			player_ui_end_editing_profile();
			if (!finish)
				player_ui_begin_editing_profile(profile_index);
		}
	}
}

/* another tab in the place of this one (out of the history) */
static boolean settings_tab_open(struct widget_instance *widget, short tab, boolean *widget_deleted)
{
	struct widget_instance *screen = screen_of(widget);
	short count = settings_tab_count();
	char name[256];
	short index;

	if (!count)
		return FALSE;
	tab = (short)((tab + count) % count);
	if (tab == settings_tab_of(screen))
		return TRUE;
	settings_each(screen, setting_stash);
	/* (its tag's name: the model's, with the menus' prefix) */
	snprintf(name, sizeof(name), "pc\\%s", settings_tab_screen(tab)->name);
	for (index = 0; name[index]; index++)
	{
		if (name[index] == '/')
			name[index] = '\\';
	}
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

static void settings_weapon_reset(struct widget_instance *screen);
static void clear_caches_press(void);
/* Settings' search, the menu engine's (settings_search_menu.inc) */
static boolean settings_search_open(struct widget_instance *widget, boolean *widget_deleted);
static void settings_search_edit(struct widget_instance *widget);
static boolean settings_search_back(struct widget_instance *widget);
static void settings_search_left(void);
static void settings_search_keys(struct widget_instance *list);
static void settings_search_bars_show(struct widget_instance *list);
static void settings_search_update(struct widget_instance *list);
static boolean settings_search_term(char const *key, unsigned long key_length, char const *value,
	unsigned long value_length);
static wchar_t const *settings_search_about(void);
static wchar_t const *settings_search_field_help(struct widget_instance *list);
static boolean settings_search_returned(void);

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
		/* (the screen's player, where it has one; else the one whose press
		opened it: profile_edit_begin) */
		if (screen_of(widget)->local_player_index >= 0 && screen_of(widget)->local_player_index < 4)
			settings_local_player_set(screen_of(widget)->local_player_index);
		pc_menu_profile_edit_begin();
	}
	else if (!strcmp(name, "tab"))
	{
		/* (the search's tabs: after its field) */
		*result = settings_tab_open(widget, (short)(sibling_index(widget) -
			(widget->parent && settings_is_part(widget->parent, "search tabs") ? 1 : 0)), widget_deleted);
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
		controls_page_save(screen);
		/* (the search's OK: it is left) */
		if (menu_engine_search_screen(screen))
			settings_search_left();
	}
	else if (!strcmp(name, "search"))
	{
		*result = settings_search_open(widget, widget_deleted);
	}
	else if (!strcmp(name, "search edit"))
	{
		settings_search_edit(widget);
	}
	else if (!strcmp(name, "search back"))
	{
		*result = settings_search_back(widget);
	}
	else if (!strcmp(name, "page cancel"))
	{
		/* (Button Remapping's button being taken, or its replacement
		asked, or Key Bindings' key being taken: dropped, the page kept) */
		if (!pad_editor_cancel(screen) || !controls_page_cancel(screen))
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
	else if (!strcmp(name, "weapon reset"))
	{
		settings_weapon_reset(screen);
		ui_play_audio_feedback_sound(SOUND_CURSOR);
	}
	else if (!strcmp(name, "gyro calibrate"))
	{
		/* (Gyro & Flick Stick's CALIBRATE: at once, not kept for OK) */
		halo_gyro_calibrate();
		gyro_calibrated = FALSE;
		ui_play_audio_feedback_sound(SOUND_CURSOR);
	}
	else if (!strcmp(name, "clear caches"))
	{
		/* (CLEAR CACHES: asked, then asked for at the next start,
		port/linux/src/game_caches.c) */
		clear_caches_press();
	}
	else if (!strcmp(name, "report"))
	{
		/* (REPORT A PROBLEM: saved at the next frame and a GitHub issue
		opened for it, port/linux/src/report.c, which says where) */
		report_request(FALSE);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
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

/* Server Setup's states, a setup screen's nodes' terms (below) */
static boolean hosting_cooperative(void);
static boolean server_settings_private(void);
static boolean server_settings_internet_host(void);
static boolean server_settings_local_game(void);
/* (and the Discord page's: an account linked, or waiting for its code) */
static boolean discord_linked(void);

/* a term of a row's condition in the model (tools/port_settings.py's
SETTINGS_ROW_WHEN and SETTINGS_ROW_ENABLED, and a setup screen's node's,
SETUP_NODE_WHEN): a platform's, "<key>=<value>" (another row's value shown,
its spinner's; a spinner of the game's at its nth item, "<key>=<n>"), or a
state or a capability of the game's */
static boolean settings_condition_term(struct widget_instance *list, char const *term, unsigned long length)
{
	char name[64];
	char const *equals = memchr(term, '=', length);
	int platform = halo_menus_platform(term, length);

	if (platform >= 0)
		return platform != 0;
	if (equals)
	{
		unsigned long key = (unsigned long)(equals - term);
		struct widget_instance *spinner;
		struct pc_menu_setting *setting;
		short index;

		snprintf(name, sizeof(name), "%.*s_spinner", (int)key, term);
		spinner = named(list, name, 0);
		/* (a row found by the search, its page's other row not found with
		it: that row's setting's value) */
		if (!spinner && menu_engine_search_screen(list))
			return settings_search_term(term, key, equals + 1, length - key - 1);
		setting = settings_setting_of(spinner);
		index = spinner ? spinner->parameters.list.selected_index : NONE;
		if (spinner && !setting && length - key - 1 > 0 && length - key - 1 < 6 &&
			strspn(equals + 1, "0123456789") >= length - key - 1)
		{
			return index == (short)atoi(equals + 1);
		}
		return setting && index >= 0 && index < setting->value_count &&
			strlen(setting->values[index]) == length - key - 1 &&
			!strncmp(setting->values[index], equals + 1, length - key - 1);
	}
	snprintf(name, sizeof(name), "%.*s", (int)length, term);
	if (!strcmp(name, "in_game"))
		return !main_menu_is_active();
	if (!strcmp(name, "profile"))
		return player_ui_get_edit_player_profile() != NULL;
	if (!strcmp(name, "vr_build"))
	{
#ifdef HALO_VR
		return TRUE;
#else
		return FALSE;
#endif
	}
	if (!strcmp(name, "refresh_rates"))
		return platform_refresh_rates_settable() != 0;
	if (!strcmp(name, "vrr_found"))
		return platform_vrr_detected() == 1 /* PLATFORM_VRR_FOUND */;
	if (!strcmp(name, "vrr_system"))
		return platform_vrr_detected() == 3 /* PLATFORM_VRR_SYSTEM */;
	if (!strcmp(name, "gyro"))
		return halo_gyro_found() == 2;
	if (!strcmp(name, "weapon_held"))
	{
		char path[sizeof(settings_weapon)];

		return !main_menu_is_active() && viewmodel_held_weapon(settings_local_player, path, sizeof(path));
	}
	if (!strcmp(name, "discord_linked"))
		return discord_linked();
	if (!strcmp(name, "cooperative"))
		return hosting_cooperative();
	if (!strcmp(name, "local"))
		return server_settings_local_game();
	if (!strcmp(name, "internet_host"))
		return server_settings_internet_host();
	if (!strcmp(name, "online"))
		return config_boolean("network.online");
	if (!strcmp(name, "public_lobby"))
		return config_boolean("network.public_lobby");
	if (!strcmp(name, "private"))
		return server_settings_private();
	platform_log("menus: a row's condition names %s, which is no term of one", name);
	return FALSE;
}

/* a row's condition: its terms, "!" before one not, "+" between those that
all hold, a space between those of which one does */
static boolean settings_condition(struct widget_instance *list, char const *condition)
{
	while (*condition)
	{
		unsigned long length = (unsigned long)strcspn(condition, " ");
		char const *end = condition + length;
		boolean all = length > 0;

		while (condition < end)
		{
			unsigned long term = (unsigned long)strcspn(condition, "+ ");
			boolean not = *condition == '!';

			if (settings_condition_term(list, condition + not, term - not) == not)
				all = FALSE;
			condition += term;
			if (*condition == '+')
				condition++;
		}
		if (all)
			return TRUE;
		while (*condition == ' ')
			condition++;
	}
	return FALSE;
}

/* whether a condition has a term that is not a platform's (one the game
tests as the page is shown: a platform's holds as the files are read) */
static boolean settings_condition_runs(char const *condition)
{
	while (*condition)
	{
		unsigned long length = (unsigned long)strcspn(condition, "+ ");

		if (*condition == '!' || (length && halo_menus_platform(condition, length) < 0))
			return TRUE;
		condition += length;
		condition += strspn(condition, "+ ");
	}
	return FALSE;
}

/* the rows' conditions in the model (docs/menu_engine.md), as a page is
shown: each row shown while its when holds, and dimmed while its enabled
does not; a focus on a row hidden goes to the row in its place (Window
Size's and Resolution's), else to the first shown (settings_focus_shown) */
static void settings_rows_conditions(struct widget_instance *list)
{
	struct halo_menu_model_screen const *screen = menu_engine_model_screen_of(list);
	struct halo_menu_model const *model = menu_engine_model();
	struct widget_instance *focused = list->focused_child;
	struct widget_instance *child;
	long index;

	for (index = 0; screen && model && index < screen->row_count; index++)
	{
		struct halo_menu_model_row const *row = &model->rows[screen->first_row + index];
		struct widget_instance *widget;
		char name[80];

		if (!strcmp(row->kind, "section") || !strcmp(row->kind, "button"))
			continue;
		snprintf(name, sizeof(name), "op_%s", row->key);
		if ((widget = named(list, name, 0)) == NULL)
			continue;
		if (row->when && settings_condition_runs(row->when))
			widget->visible = settings_condition(list, row->when);
		if (row->enabled)
		{
			snprintf(name, sizeof(name), "%s_spinner", row->key);
			row_dim(widget, named(widget, name, 0), !settings_condition(list, row->enabled));
		}
	}
	if (!screen || !model || !focused || focused->visible || !focused->name || strncmp(focused->name, "op_", 3))
		return;
	for (index = 0; index < screen->row_count; index++)
	{
		struct halo_menu_model_row const *row = &model->rows[screen->first_row + index];
		struct halo_menu_model_row const *other = NULL;
		struct widget_instance *place;
		char name[80];
		short at;

		if (strcmp(row->key, focused->name + 3))
			continue;
		if (row->same_place && index > 0)
			other = row - 1;
		else if (index + 1 < screen->row_count && row[1].same_place)
			other = row + 1;
		snprintf(name, sizeof(name), "op_%s", other ? other->key : "");
		place = other ? named(list, name, 0) : NULL;
		if (!place || !place->visible)
			return;
		for (at = 0, child = list->child; child && child != place; child = child->next)
			at++;
		list->focused_child = place;
		list->parameters.list.selected_index = at;
		return;
	}
}

/* a setup screen's nodes' conditions in the model (docs/menu_engine.md;
tools/port_settings.py's SETUP_NODE_WHEN, model/nodes.xml), as the screen is
shown: each node with a condition shown while it holds */
static void setup_nodes_conditions(struct widget_instance *widget)
{
	struct widget_instance *screen = screen_of(widget);
	struct halo_menu_model_screen const *model_screen = menu_engine_model_screen_of(screen);
	struct halo_menu_model const *model = menu_engine_model();
	long index;

	for (index = 0; model_screen && model && index < model_screen->row_count; index++)
	{
		struct halo_menu_model_row const *node = &model->rows[model_screen->first_row + index];
		char const *leaf;

		if (strcmp(node->kind, "node") || !node->when || !settings_condition_runs(node->when))
			continue;
		leaf = strrchr(node->key, '/');
		visible_set(named(screen, leaf ? leaf + 1 : node->key, 0), settings_condition(screen, node->when));
	}
}

/* a setup screen's help (its list's description) for the row with the
focus: its node's place in the help in the model (Server Setup's,
tools/port_settings.py's SERVER_SETTINGS_HELP), and its spinner's choice
after it where each choice has its own; a row hidden, or one the model has
no place of, by its label's string (settings_help) */
static void setup_help(struct widget_instance *list)
{
	struct widget_instance *help = list->parameters.list.extended_description;
	struct widget_instance *row = list->focused_child;
	struct halo_menu_model_screen const *screen = menu_engine_model_screen_of(list);
	struct halo_menu_model const *model = menu_engine_model();
	long index;

	for (index = 0; help && row && row->name && row->visible && screen && model && index < screen->row_count;
		index++)
	{
		struct halo_menu_model_row const *node = &model->rows[screen->first_row + index];
		char const *leaf = strrchr(node->key, '/');
		struct widget_instance *spinner;

		if (strcmp(node->kind, "node") || node->help_index < 0 || strcmp(leaf ? leaf + 1 : node->key, row->name))
			continue;
		help->parameters.text_box.string_list_index = (short)node->help_index;
		for (spinner = row->child; node->help_choices && spinner && spinner->type != 2; spinner = spinner->next)
			;
		if (node->help_choices && spinner)
			help->parameters.text_box.string_list_index += spinner->parameters.list.selected_index;
		return;
	}
	settings_help(list);
}

/* the rows shown, a step apart from the top, each section's title over them
(hidden with none shown), as tools/port_settings.py lays them out for every
row */
static void settings_rows_pack(struct widget_instance *list, boolean tab)
{
	short y = tab ? SETTINGS_TABS_ROWS_TOP : SETTINGS_PAGE_ROWS_TOP;
	struct widget_instance *child;
	boolean rows_above = FALSE;

	/* (the menu engine lays them out from the model: menu_engine.c) */
	if (menu_engine_lays_out(list))
		return;

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
	struct widget_instance *tabs = settings_part(list, "tabs");
	struct widget_instance *search_tabs = settings_part(list, "search tabs");
	struct widget_instance *item;
	short index = 0;

	/* (the menu engine's tabs, after the search's field) */
	if (search_tabs && search_tabs->visible && tab != NONE)
	{
		tabs = search_tabs;
		tab++;
	}
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

/* the words for the row: the model's, where the menu engine runs the screen
(menu_engine.c), else from the help list beside its labels' */
static wchar_t const *settings_row_help(struct widget_instance *label)
{
	long labels = label ? pc_menu_widget_string_list(label->definition_tag_index) : NONE;
	short index = label ? pc_menu_string_index(label->definition_tag_index) : NONE;
	boolean found = FALSE;
	wchar_t const *help = label && label->parent ? menu_engine_row_help(label->parent, &found) : NULL;
	char name[256];
	char *last;

	if (found)
		return help;
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

/* a text of the model's in UTF-16, in one of two buffers (NULL: none) */
static wchar_t const *settings_model_text(char const *text, short buffer)
{
	static wchar_t texts[2][SETTINGS_PANEL_TEXT_LENGTH];

	if (!text || buffer < 0 || buffer > 1)
		return NULL;
	halo_menus_utf16(text, (unsigned short *)texts[buffer], SETTINGS_PANEL_TEXT_LENGTH);
	return texts[buffer];
}

/* the widget's descendant of the role in its look (model/look's role=,
tools/port_settings.py's settings_look_roles: Settings' tabs and button
bar, the panel's parts), NULL if none */
static struct widget_instance *settings_part(struct widget_instance *widget, char const *role)
{
	struct widget_instance *child;

	for (child = widget ? widget->child : NULL; child; child = child->next)
	{
		char const *found = menu_engine_look_role(child);
		struct widget_instance *part;

		if (found && !strcmp(found, role))
			return child;
		if ((part = settings_part(child, role)) != NULL)
			return part;
	}
	return NULL;
}

/* whether the widget is of the role in its look */
static boolean settings_is_part(struct widget_instance const *widget, char const *role)
{
	char const *found = menu_engine_look_role(widget);

	return found && !strcmp(found, role);
}

/* whether a row is the model's row of the key (on its screen) */
static boolean settings_row_is(struct widget_instance const *row, char const *key)
{
	struct halo_menu_model_row const *model_row = menu_engine_model_row_of(row);

	return model_row && !strcmp(model_row->key, key);
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

/* the height of the panel's words of so many lines, and of its note under
them (none: 0 lines) */
static short settings_panel_height(short lines, short note_lines, short line)
{
	return (short)(lines * line + (note_lines ? line / 2 + note_lines * line : 0));
}

/* wrapped words (settings_text_wrap's) cut to so many lines, the last one's
last word "..." if any were cut: the lines left */
static short settings_text_lines_cut(wchar_t *wrapped, short lines)
{
	wchar_t *end = wrapped, *space;
	short line = 1;

	for (; *end; end++)
	{
		if (end[0] == '\r' && end[1] == '\n' && ++line > lines)
			break;
	}
	if (!*end)
		return line;
	*end = 0;
	for (space = end; space > wrapped && space[-1] != ' ' && space[-1] != '\n'; space--)
	{
	}
	if (space > wrapped && space[-1] == ' ')
		end = space;
	if (end + 3 < wrapped + SETTINGS_PANEL_TEXT_LENGTH)
	{
		end[0] = end[1] = end[2] = '.';
		end[3] = 0;
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

/* the account's state, as the rows show it: one a server needs, unless
linked or waiting for its code */
static int discord_state_shown(char *name, int name_size, char *code, int code_size, int *linked,
	int *masters)
{
	int needed = player_link_needed_master();
	int state = player_link_state(name, name_size, code, code_size, linked, masters);

	if (state != _player_link_state_linked && state != _player_link_state_waiting && needed >= 0)
		state = _player_link_state_needed;
	return state;
}

/* "discord_linked", UNLINK's condition (tools/port_settings.py's
SETTINGS_ROW_WHEN) */
static boolean discord_linked(void)
{
	char name[PLAYER_LINK_NAME_SIZE + 1], code[PLAYER_LINK_CODE_SIZE];
	int linked, masters;
	int state = discord_state_shown(name, sizeof(name), code, sizeof(code), &linked, &masters);

	return state != _player_link_state_none && state != _player_link_state_needed;
}

/* each frame of Online's list and of the page's: the rows' values, the
masters' hosts, the rows shown (UNLINK by its condition in the model,
settings_rows_conditions, which comes first) */
static void discord_account_show(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	struct widget_instance *row;
	char name[PLAYER_LINK_NAME_SIZE + 1], code[PLAYER_LINK_CODE_SIZE], host[80], text[ROW_TEXT_LENGTH];
	int state, linked, masters, master, needed = player_link_needed_master();
	boolean page = settings_model_row(list, "link_status") != NULL;

	if (!settings_model_row(list, "discord_account") && !page)
		return;
	state = discord_state_shown(name, sizeof(name), code, sizeof(code), &linked, &masters);
	settings_status_set(list, "discord_account", discord_state_word(state));
	if (!page)
		return;
	settings_status_set(list, "link_status", discord_state_word(state));
	if ((row = settings_model_row(list, "link_unlink")) != NULL)
	{
		if (!row->visible)
			discord_account.unlink_asked = 0;
		settings_status_set(list, "link_unlink", discord_unlink_asked() ? "SELECT AGAIN" : "");
	}
	for (master = 0; master < PLAYER_LINK_MAXIMUM_MASTERS; master++)
	{
		char row_name[32];
		int master_state = player_link_master_state(master, host, sizeof(host), name, sizeof(name), code,
			sizeof(code));

		snprintf(row_name, sizeof(row_name), "link_master_%d", master + 1);
		if (!(row = settings_model_row(list, row_name)))
			continue;
		row->visible = host[0] != 0;
		snprintf(row_name, sizeof(row_name), "link_master_%d_label", master + 1);
		discord_text_set(named(row, row_name, 0), discord_host_shown(host));
		if (master_state == _player_link_state_waiting)
			snprintf(text, sizeof(text), "CODE %s", code);
		else
			snprintf(text, sizeof(text), "%s", master_state == _player_link_state_none ? "LINK" :
				discord_state_word(master_state));
		snprintf(row_name, sizeof(row_name), "link_master_%d", master + 1);
		settings_status_set(list, row_name, text);
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
	char text[ROW_TEXT_LENGTH];

	if (!settings_model_row(list, "hdr_display"))
		return;
	halo_hdr_display_text(0, text, sizeof(text));
	settings_status_set(list, "hdr_display", text);
}

/* the VARIABLE REFRESH row's panel: its words of what the system says, in
the help's body (the note below it holds two lines: more is cut) (the
others': FALSE) */
static boolean vrr_panel(struct widget_instance *row, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char const *text;

	if (!settings_row_is(row, "vrr"))
		return FALSE;
	switch (platform_vrr_detected())
	{
	case 0 /* PLATFORM_VRR_NONE */:
		text = "For a G-SYNC or FreeSync display: VSync on and the frame rate held just under the refresh rate, so "
			"each frame shows as soon as it is ready, without tearing. No variable refresh screen found. Turn it on "
			"in your display or driver settings.";
		break;
	case 1 /* PLATFORM_VRR_FOUND */:
		text = "For a G-SYNC or FreeSync display: VSync on and the frame rate held just under the refresh rate, so "
			"each frame shows as soon as it is ready, without tearing. A variable refresh screen was found: Auto "
			"has it on.";
		break;
	case 2 /* PLATFORM_VRR_UNKNOWN */:
		text = "For a G-SYNC or FreeSync display: VSync on and the frame rate held just under the refresh rate, so "
			"each frame shows as soon as it is ready, without tearing. Turn on if your screen has G-Sync or "
			"FreeSync enabled.";
		break;
	case 3 /* PLATFORM_VRR_SYSTEM */:
		text = "Set by macOS: Metal and ProMotion match the display's refresh to the frames themselves.";
		break;
	default:
		return FALSE;
	}
	discord_widen(text, words, SETTINGS_PANEL_TEXT_LENGTH);
	*help = words;
	return TRUE;
}

/* the GYRO row's panel while Steam Input has the controller (STEAM INPUT HAS
IT: only its virtual controllers, which pass no gyroscope): how to turn it
off for the game, on a Steam Deck (xinput_sdl.c's halo_steam_deck) or another
computer's Steam (the others': FALSE) */
int halo_steam_deck(void);

static boolean gyro_found_panel(struct widget_instance *row, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];

	if (!settings_row_is(row, "gyro_found") || halo_gyro_found() != 1)
		return FALSE;
	discord_widen(halo_steam_deck() ?
		"Steam Input has the controls: it passes no gyro to GOAT H1.|To aim with the Steam Deck's gyro: Steam > "
		"GOAT H1 > controller icon > Disable Steam Input, then start GOAT H1 again." :
		"Steam Input has the controller: it passes no gyro to GOAT H1.|To aim with its gyro: in Steam, GOAT H1's "
		"Properties > Controller > Disable Steam Input, then start GOAT H1 again.", words, SETTINGS_PANEL_TEXT_LENGTH);
	*help = words;
	return TRUE;
}

/* the DISPLAY PEAK row's panel: its value and words, the display's peak and
SDR white now (the others': FALSE) */
static boolean hdr_display_panel(struct widget_instance *row, wchar_t *value, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char text[SETTINGS_PANEL_TEXT_LENGTH];

	if (!settings_row_is(row, "hdr_display") || !halo_hdr_display_text(0, text, sizeof(text)))
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

	if (!settings_row_is(row, "about"))
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

/* ---------- Profiles' CLEAR CACHES (port/linux/src/game_caches.c)

The row's value is the caches' size, walked on the disk as the page shows
it and after each press, not each frame; SELECT AGAIN after a first press,
and AT NEXT START once their clearing is asked for, which a press then takes
back. The panel says what a press does now. */

#define CLEAR_CACHES_ASK_MILLISECONDS 5000

static struct
{
	/* the first press, 0: none */
	unsigned long asked;
	/* whether they were measured since the page showed, their bytes, and
	whether their clearing is asked for */
	boolean measured;
	unsigned long long bytes;
	boolean requested;
} clear_caches;

static boolean clear_caches_asked(void)
{
	return clear_caches.asked && system_milliseconds() - clear_caches.asked < CLEAR_CACHES_ASK_MILLISECONDS;
}

/* a size in words: "2.9 GB", "640 MB", "12 KB" */
static void clear_caches_size(unsigned long long bytes, char *text, int size)
{
	if (bytes >= 1000ULL * 1024 * 1024)
		snprintf(text, size, "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
	else if (bytes >= 1024ULL * 1024)
		snprintf(text, size, "%lu MB", (unsigned long)(bytes / (1024ULL * 1024)));
	else
		snprintf(text, size, "%lu KB", (unsigned long)((bytes + 1023) / 1024));
}

static void clear_caches_measure(void)
{
	clear_caches.bytes = game_caches_bytes();
	clear_caches.requested = game_caches_requested() != 0;
	clear_caches.measured = TRUE;
}

static void clear_caches_show(struct widget_instance *list)
{
	char text[ROW_TEXT_LENGTH];

	if (!settings_model_row(list, "clear_caches"))
	{
		clear_caches.measured = FALSE;
		clear_caches.asked = 0;
		return;
	}
	if (!clear_caches.measured)
		clear_caches_measure();
	if (clear_caches.requested)
		snprintf(text, sizeof(text), "AT NEXT START");
	else if (clear_caches_asked())
		snprintf(text, sizeof(text), "SELECT AGAIN");
	else
		clear_caches_size(clear_caches.bytes, text, sizeof(text));
	settings_status_set(list, "clear_caches", text);
}

/* the panel's value (the size) and words for CLEAR CACHES (the others':
FALSE) */
static boolean clear_caches_panel(struct widget_instance *row, wchar_t *value, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char size[32], text[SETTINGS_PANEL_TEXT_LENGTH];

	if (!settings_row_is(row, "clear_caches"))
		return FALSE;
	clear_caches_size(clear_caches.bytes, size, sizeof(size));
	discord_widen(size, value, ROW_TEXT_LENGTH);
	if (clear_caches.requested)
		snprintf(text, sizeof(text), "These %s are cleared the next time the game starts, before anything uses "
			"them, and made again as you play: that start takes longer.|Select CLEAR CACHES again to keep them.",
			size);
	else if (clear_caches_asked())
		snprintf(text, sizeof(text), "Select CLEAR CACHES again to clear these %s at the next start.", size);
	else
		return TRUE;
	discord_widen(text, words, SETTINGS_PANEL_TEXT_LENGTH);
	*help = words;
	return TRUE;
}

/* "port settings clear caches": the first press asks, the second asks for
the clearing at the next start; once asked for, a press takes it back */
static void clear_caches_press(void)
{
	if (!clear_caches.requested && !clear_caches_asked())
	{
		clear_caches.asked = system_milliseconds() | 1;
		ui_play_audio_feedback_sound(SOUND_CURSOR);
		return;
	}
	game_caches_request(!clear_caches.requested);
	clear_caches.asked = 0;
	clear_caches_measure();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
}

/* the panel's name, value and words for a row of the page (the others':
FALSE) */
static boolean discord_account_panel(struct widget_instance *row, wchar_t *name, wchar_t *value,
	wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	char account[PLAYER_LINK_NAME_SIZE + 1], code[PLAYER_LINK_CODE_SIZE], host[80];
	char text[SETTINGS_PANEL_TEXT_LENGTH], shown[ROW_TEXT_LENGTH];
	int master = discord_master_of(row->name), state, linked, masters;

	if (settings_row_is(row, "discord_account"))
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
	else if (settings_row_is(row, "link_status"))
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
	else if (settings_row_is(row, "link_unlink") && discord_unlink_asked())
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
/* the words a help says the host decides a setting by, online (taken out
of the words: the panel's facts say it) */
static wchar_t const settings_host_choice[] = L"Online: the host's choice.";

/* words without what a help says of the host's choice, and whether it did */
static boolean settings_host_choice_cut(wchar_t *words)
{
	short length = (short)(NUMBEROF(settings_host_choice) - 1), at, index;

	for (at = 0; words[at]; at++)
	{
		for (index = 0; index < length && words[at + index] == settings_host_choice[index]; index++)
		{
		}
		if (index == length)
		{
			short start = at;

			/* (and the space before it) */
			while (start > 0 && words[start - 1] == ' ')
				start--;
			for (index = 0; (words[start + index] = words[at + length + index]) != 0; index++)
			{
			}
			return TRUE;
		}
	}
	return FALSE;
}

/* words added to the end of others, ROW_TEXT_LENGTH at most */
static void settings_words_add(wchar_t *words, wchar_t const *more)
{
	short length = 0;

	while (length < ROW_TEXT_LENGTH - 1 && words[length])
		length++;
	while (length < ROW_TEXT_LENGTH - 1 && *more)
		words[length++] = *more++;
	words[length] = 0;
}

/* the panel's facts of a row: their names and values */
#define SETTINGS_FACTS_MOST 3
/* (the room kept between a fact's name and its value on one line) */
#define SETTINGS_FACTS_GAP 8
struct settings_facts
{
	wchar_t const *names[SETTINGS_FACTS_MOST];
	wchar_t values[SETTINGS_FACTS_MOST][ROW_TEXT_LENGTH];
	short count;
};

static void settings_fact_add(struct settings_facts *facts, wchar_t const *name, wchar_t const *value)
{
	if (facts->count >= SETTINGS_FACTS_MOST)
		return;
	facts->names[facts->count] = name;
	settings_wide_copy(facts->values[facts->count], value, ROW_TEXT_LENGTH);
	facts->count++;
}

/* a setting's fact of whose value it is, if it is each player's: the
profile's edited (first), or every player's, config.toml's (last) */
static void settings_profile_fact(char const *name, struct settings_facts *facts, boolean first)
{
	char folder[1024];
	wchar_t whose[ROW_TEXT_LENGTH] = { 0 };
	boolean own;

	/* (a weapon's own offsets are each player's too) */
	if (!name || (!config_per_player(name) && settings_weapon_value(name) == NONE))
		return;
	own = settings_setting_folder(name, folder, sizeof(folder), whose);
	if (own != first)
		return;
	if (!own)
		settings_wide_copy(whose, L"ALL PLAYERS", ROW_TEXT_LENGTH);
	settings_fact_add(facts, L"PROFILE", whose);
}

/* the facts of a row of a setting for the panel: the value its default
shows, the host's choice online where its help says so (taken out of its
words), and whose value it is of a setting that is each player's: the
profile's edited (settings_profile_folder), or every player's (config.toml's) */
static void settings_facts_of(struct widget_instance *row, struct widget_instance *spinner, wchar_t *words,
	struct settings_facts *facts)
{
	struct settings_slider const *slider = settings_slider_of(row);
	struct pc_menu_setting *setting = spinner ? settings_setting_of(spinner) : NULL;
	char const *name = setting && setting->setting ? setting->setting : slider ? settings_slider_setting(slider) : NULL;
	char text[300];
	wchar_t shown[ROW_TEXT_LENGTH] = { 0 };
	double number;

	facts->count = 0;
	/* (a profile's own first: whose value it is, where the panel has room
	for one fact) */
	settings_profile_fact(name, facts, TRUE);
	if (setting && setting->setting && setting_text(setting->setting, text, sizeof(text), TRUE))
		settings_wide_copy(shown, settings_widget_text(spinner, setting_value_index(setting, text)), ROW_TEXT_LENGTH);
	else if (slider && setting_text(settings_slider_setting(slider), text, sizeof(text), TRUE) &&
		settings_slider_text_value(slider, text, &number))
	{
		short length;

		settings_slider_text(slider, number, text, sizeof(text));
		for (length = 0; text[length] && length < ROW_TEXT_LENGTH - 1; length++)
			shown[length] = (wchar_t)text[length];
		shown[length] = 0;
	}
	if (shown[0])
		settings_fact_add(facts, L"DEFAULT", shown);
	if (settings_host_choice_cut(words))
		settings_fact_add(facts, L"ONLINE", L"HOST'S CHOICE");
	settings_profile_fact(name, facts, FALSE);
}

/* the width of words in a font, as drawn */
static short settings_words_width(long font, wchar_t const *words)
{
	rectangle2d bounds, text_bounds, cursor;
	static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };

	if (font == NONE)
		return 0;
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 0x3FFF;
	bounds.y1 = 480;
	draw_string_set_draw_mode(font, NONE, 0, 0, &white);
	draw_unicode_string_compute_bounds(&bounds, words, &text_bounds, &cursor);
	return (short)(text_bounds.x1 - text_bounds.x0);
}

/* the facts as the panel's two text boxes have them (names, and values at
the right), a line each, or a value on the line under its name where both
do not fit across the panel: the lines they take */
static short settings_facts_lines(struct settings_facts const *facts, long font, short width, wchar_t *names,
	wchar_t *values)
{
	short index, lines = 0;

	names[0] = 0;
	values[0] = 0;
	for (index = 0; index < facts->count; index++)
	{
		boolean under = settings_words_width(font, facts->names[index]) + SETTINGS_FACTS_GAP +
			settings_words_width(font, facts->values[index]) > width;

		if (lines)
		{
			settings_words_add(names, L"\r\n");
			settings_words_add(values, L"\r\n");
		}
		settings_words_add(names, facts->names[index]);
		if (under)
		{
			settings_words_add(names, L"\r\n");
			settings_words_add(values, L"\r\n");
			lines++;
		}
		settings_words_add(values, facts->values[index]);
		lines++;
	}
	return lines;
}

static short menu_text_wrap_lines(long font, wchar_t const *text, short width, short lines, wchar_t *wrapped);
static boolean menu_text_same(wchar_t const *a, wchar_t const *b);

/* the panel's name and value, each wrapped to its box (issue 899: a long
device's name whole, not cut short), as many lines at most, as they were
the last time while they are the same (each frame): the lines each takes
and its font's line's height */
#define SETTINGS_PANEL_NAME_LINES 2
#define SETTINGS_PANEL_VALUE_LINES 7
/* (the longest name or value: a device's whole name) */
#define SETTINGS_PANEL_VALUE_LENGTH 128

struct settings_panel_wrapping
{
	void const *box;
	long font;
	short width;
	short lines;
	short line;
	wchar_t text[SETTINGS_PANEL_VALUE_LENGTH];
	wchar_t wrapped[SETTINGS_PANEL_TEXT_LENGTH];
};

static struct settings_panel_wrapping settings_panel_name_wrapping, settings_panel_value_wrapping;

/* a font's line's height: two lines' less one's (12 while the font is not
known) */
static short settings_font_line(long font)
{
	static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };
	rectangle2d bounds, text_bounds, cursor;
	short line;

	if (font == NONE)
		return 12;
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 0x3FFF;
	bounds.y1 = 480;
	draw_string_set_draw_mode(font, NONE, 0, 0, &white);
	draw_unicode_string_compute_bounds(&bounds, L"X\r\nX", &text_bounds, &cursor);
	line = (short)(text_bounds.y1 - text_bounds.y0);
	draw_unicode_string_compute_bounds(&bounds, L"X", &text_bounds, &cursor);
	return (short)(line - (text_bounds.y1 - text_bounds.y0));
}

/* a box of the panel's text wrapped to it, `most` lines at most (the last
cut short with "..."): the lines it takes */
static short settings_panel_wrap(struct settings_panel_wrapping *wrapping, struct widget_instance *box,
	wchar_t const *text, short most)
{
	short width;
	long font = ui_widget_port_text_box_font(box, &width);

	if (!box)
		return 1;
	if (wrapping->box != box || wrapping->font != font || wrapping->width != width ||
		!menu_text_same(wrapping->text, text))
	{
		wrapping->box = box;
		wrapping->font = font;
		wrapping->width = width;
		settings_wide_copy(wrapping->text, text, SETTINGS_PANEL_VALUE_LENGTH);
		wrapping->lines = text[0] ? menu_text_wrap_lines(font, text, width, most, wrapping->wrapped) : 1;
		if (!text[0])
			wrapping->wrapped[0] = 0;
		wrapping->line = settings_font_line(font);
	}
	text_set_length(box, wrapping->wrapped, SETTINGS_PANEL_TEXT_LENGTH);
	return wrapping->lines;
}

static void settings_panel_show(struct widget_instance *list, short tab)
{
	struct widget_instance *panel = list->parameters.list.extended_description;
	struct widget_instance *row = list->focused_child;
	struct widget_instance *text, *note, *facts, *facts_values;
	struct settings_facts row_facts;
	wchar_t fact_names[ROW_TEXT_LENGTH] = { 0 }, fact_values[ROW_TEXT_LENGTH] = { 0 };
	short fact_lines = 0, facts_height, facts_width;
	long text_font, note_font;
	short text_width, note_width;
	wchar_t name[ROW_TEXT_LENGTH] = { 0 }, value[SETTINGS_PANEL_VALUE_LENGTH] = { 0 };
	wchar_t words[SETTINGS_PANEL_TEXT_LENGTH] = { 0 }, wrapped[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t noted[SETTINGS_PANEL_TEXT_LENGTH] = { 0 };
	wchar_t const *help = NULL;
	wchar_t *bar;
	short still = NONE, length, lines, note_lines, line = 12, index;
	short name_lines, value_lines, name_more, value_more;
	boolean stills = FALSE, picture = FALSE;
	struct widget_instance *row_spinner = NULL;
	char const *const still_names[] = { "classic", "enhanced" };

	if (!panel || !row || !row->name)
		return;
	if (settings_is_part(row, "search field") || (settings_is_part(row, "search tabs") && row->focused_child &&
		settings_is_part(row->focused_child, "search field")))
	{
		/* (the search's field: what it does, and how to open it) */
		settings_wide_copy(name, L"SETTINGS", ROW_TEXT_LENGTH);
		settings_wide_copy(value, L"SEARCH", ROW_TEXT_LENGTH);
		help = settings_search_field_help(list);
	}
	else if ((settings_is_part(row, "tabs") || settings_is_part(row, "search tabs")) && row->focused_child)
	{
		/* (a tab's name and words: its screen's in the model; the search's
		tabs after its field) */
		struct halo_menu_model_screen const *shown = settings_tab_screen((short)(sibling_index(row->focused_child) -
			(settings_is_part(row, "search tabs") ? 1 : 0)));

		settings_wide_copy(name, L"SETTINGS", ROW_TEXT_LENGTH);
		settings_wide_copy(value, settings_model_text(shown ? shown->tab : NULL, 0), ROW_TEXT_LENGTH);
		help = settings_model_text(shown ? shown->about : NULL, 1);
	}
	else if (settings_is_part(row, "buttons") && row->focused_child)
	{
		/* (a button's words: the screen's button's in the model, the
		index'th) */
		struct halo_menu_model_screen const *screen = menu_engine_model_screen_of(list);
		struct halo_menu_model const *model = menu_engine_model();
		short button = sibling_index(row->focused_child);

		settings_wide_copy(name, settings_widget_text(row->focused_child, NONE), ROW_TEXT_LENGTH);
		for (index = 0; screen && model && index < screen->row_count; index++)
		{
			struct halo_menu_model_row const *model_row = &model->rows[screen->first_row + index];

			if (!strcmp(model_row->kind, "button") && button-- == 0)
			{
				help = settings_model_text(model_row->help, 1);
				break;
			}
		}
		/* (a screen whose model has no buttons: the menus' words for a
		page's) */
		if (!help)
		{
			help = settings_string(SETTINGS_SHARED "button_help",
				(short)((tab == NONE ? 3 : 0) + sibling_index(row->focused_child)));
		}
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
				SETTINGS_PANEL_VALUE_LENGTH);
		}
		else if (settings_slider_of(row))
		{
			/* (a slider's: its number) */
			char number[32];

			settings_slider_text(settings_slider_of(row), settings_slider_value(settings_slider_of(row)), number,
				sizeof(number));
			for (length = 0; number[length] && length < ROW_TEXT_LENGTH - 1; length++)
				value[length] = (wchar_t)number[length];
			value[length] = 0;
		}
		else if (profile && settings_row_is(row, "name"))
		{
			ustrncpy(value, profile->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH);
			value[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
		}
		else if (profile && settings_row_is(row, "color") && profile->primary_color_index == NONE)
		{
			/* (a new profile's: no colour chosen, so the host gives the player
			one no one else has, network_server_manager.c's get_unique_random_color) */
			settings_wide_copy(value, L"Random", ROW_TEXT_LENGTH);
		}
		else if (profile && settings_row_is(row, "color"))
		{
			string_get("pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\colors_list",
				profile->primary_color_index, value);
		}
		help = settings_row_help(label);
		/* (the Discord account's page: its words of the state now) */
		discord_account_panel(row, name, value, &help);
		/* (the display's peak: its words of the display now) */
		hdr_display_panel(row, value, &help);
		/* (Gyro's STEAM INPUT HAS IT: how to turn it off) */
		gyro_found_panel(row, &help);
		/* (Variable Refresh: what the system says of it) */
		vrr_panel(row, &help);
		/* (Profile's ABOUT: this build) */
		about_panel(row, value, &help);
		/* (Profile's CLEAR CACHES: their size, and what a press does) */
		clear_caches_panel(row, value, &help);
		/* (Button Remapping's: a button being taken, a replacement asked) */
		if (pad_editor_page(screen_of(list)))
			pad_editor_panel(row, name, value, &help);
		/* (Key Bindings': an action's name, keys and words) */
		if (controls_page(screen_of(list)))
			controls_panel(row, name, value, &help);
		row_spinner = spinner;
		/* (Quality: CLASSIC or ENHANCED framed, neither for CUSTOM) */
		if (settings_row_is(row, "graphics") && spinner)
		{
			struct pc_menu_setting *setting = settings_setting_of(spinner);
			short shown = spinner->parameters.list.selected_index;

			stills = TRUE;
			if (setting && shown >= 0 && shown < setting->value_count)
				still = !strcmp(setting->values[shown], "classic") ? 0 : !strcmp(setting->values[shown], "enhanced") ? 1 : NONE;
		}
	}
	picture = settings_value_picture_show(panel, row, row_spinner ? row_spinner->parameters.list.selected_index :
		settings_slider_of(row) ? settings_slider_stop_nearest(settings_slider_of(row)) : NONE) && !stills;
	settings_wide_copy(words, help, SETTINGS_PANEL_TEXT_LENGTH);
	for (bar = words; *bar && *bar != '|'; bar++)
	{
	}
	if (*bar)
		*bar++ = 0;
	/* (a setting's: its default, the host's choice) */
	row_facts.count = 0;
	if (!strncmp(row->name, "op_", 3))
		settings_facts_of(row, row_spinner, words, &row_facts);
	/* (the name and the value whole, wrapped: the value under the name's
	lines, the words under the value's, issue 899) */
	name_lines = settings_panel_wrap(&settings_panel_name_wrapping, settings_part(panel, "panel name"), name,
		SETTINGS_PANEL_NAME_LINES);
	value_lines = settings_panel_wrap(&settings_panel_value_wrapping, settings_part(panel, "panel value"), value,
		SETTINGS_PANEL_VALUE_LINES);
	name_more = (short)((name_lines - 1) * settings_panel_name_wrapping.line);
	value_more = (short)((value_lines - 1) * settings_panel_value_wrapping.line);
	/* (wrapped in the text boxes' fonts, taken from their definitions in the
	map loaded now: a font's tag index is not kept, as the next map's tags
	are numbered again) */
	text = settings_part(panel, "panel text");
	note = settings_part(panel, "panel note");
	facts = settings_part(panel, "panel facts");
	facts_values = settings_part(panel, "panel facts values");
	text_font = ui_widget_port_text_box_font(text, &text_width);
	note_font = ui_widget_port_text_box_font(note, &note_width);
	lines = settings_text_wrap(words, wrapped, SETTINGS_PANEL_TEXT_LENGTH, text_font, text_width);
	note_lines = *bar ? settings_text_wrap(bar, noted, SETTINGS_PANEL_TEXT_LENGTH, note_font, note_width) : 0;
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
	if (facts && facts_values)
	{
		long facts_font = ui_widget_port_text_box_font(facts, &facts_width);

		fact_lines = settings_facts_lines(&row_facts, facts_font, facts_width, fact_names, fact_values);
	}
	/* (under the name, or under the pictures; the words, the note and the
	facts kept inside the panel, whatever the screen's width: the facts left
	out if all do not fit, then the pictures if the words do not fit under
	them, then the note, then the words' last lines) */
	if (text && settings_part(panel, "panel name"))
	{
		short name_top = settings_part(panel, "panel name")->vertical_offset;
		struct halo_menu_model_geometry const *geometry = menu_engine_settings_geometry(list);
		/* (the panel's bottom in the geometry its screen is laid out in: the
		tall layout's lower, and raised with the footer on a touchscreen) */
		short bottom = (short)(geometry && geometry->panel_bottom ?
			menu_engine_geometry_bottom(geometry, geometry->panel_bottom) : SETTINGS_PANEL_BOTTOM);
		short room = (short)(bottom - SETTINGS_PANEL_TEXT_MARGIN - name_top);
		short top = stills ? SETTINGS_PANEL_TEXT_UNDER_STILLS : picture ? SETTINGS_PANEL_TEXT_UNDER_PICTURE :
			SETTINGS_PANEL_TEXT_TOP;

		if (settings_part(panel, "panel value"))
		{
			settings_part(panel, "panel value")->vertical_offset =
				(short)(name_top + SETTINGS_PANEL_VALUE_UNDER_NAME + name_more);
		}
		/* (a name or a value of more lines: the words under them, the
		pictures, in their place, left out) */
		if (name_more + value_more > 0)
		{
			if (stills || picture)
			{
				settings_value_picture_show(panel, row, NONE);
				stills = FALSE;
				picture = FALSE;
				still = NONE;
			}
			top = (short)(SETTINGS_PANEL_TEXT_TOP + name_more + value_more);
		}

		/* (the last facts left out first: the first is kept where it fits) */
		while (facts && facts_values && row_facts.count > 1 &&
			settings_panel_height(lines, note_lines, line) + line / 2 + fact_lines * line > room - top)
		{
			row_facts.count--;
			fact_lines = settings_facts_lines(&row_facts, ui_widget_port_text_box_font(facts, &facts_width),
				facts_width, fact_names, fact_values);
		}
		if (!facts || !facts_values ||
			settings_panel_height(lines, note_lines, line) + line / 2 + fact_lines * line > room - top)
		{
			fact_lines = 0;
		}
		if ((stills || picture) && settings_panel_height(lines, note_lines, line) > room - top)
		{
			settings_value_picture_show(panel, row, NONE);
			stills = FALSE;
			still = NONE;
			top = (short)(SETTINGS_PANEL_TEXT_TOP + name_more + value_more);
		}
		if (note_lines && settings_panel_height(lines, note_lines, line) > room - top)
			note_lines = 0;
		if (settings_panel_height(lines, 0, line) > room - top)
			lines = settings_text_lines_cut(wrapped, (short)MAX((room - top) / line, 1));
		text->vertical_offset = (short)(name_top + top);
	}
	text_set_length(text, wrapped, SETTINGS_PANEL_TEXT_LENGTH);
	if (!note_lines)
		noted[0] = 0;
	text_set_length(note, noted, SETTINGS_PANEL_TEXT_LENGTH);
	/* (the note a line under the words) */
	if (note && text)
		note->vertical_offset = (short)(text->vertical_offset + lines * line + line / 2);
	/* (the facts a line under the words or the note) */
	if (!fact_lines)
	{
		fact_names[0] = 0;
		fact_values[0] = 0;
	}
	facts_height = (short)(text ? text->vertical_offset + settings_panel_height(lines, note_lines, line) + line / 2 : 0);
	if (facts)
	{
		text_set_length(facts, fact_names, ROW_TEXT_LENGTH);
		facts->vertical_offset = facts_height;
	}
	if (facts_values)
	{
		text_set_length(facts_values, fact_values, ROW_TEXT_LENGTH);
		facts_values->vertical_offset = facts_height;
	}
	for (index = 0; index < 2; index++)
	{
		char role[32];
		struct widget_instance *widget;

		snprintf(role, sizeof(role), "still %s", still_names[index]);
		if ((widget = settings_part(panel, role)) != NULL)
			widget->visible = stills;
		snprintf(role, sizeof(role), "still %s frame", still_names[index]);
		if ((widget = settings_part(panel, role)) != NULL)
		{
			widget->visible = stills;
			widget->animation.current_frame_index = (short)(still == index);
		}
		snprintf(role, sizeof(role), "still %s caption", still_names[index]);
		if ((widget = settings_part(panel, role)) != NULL)
			widget->visible = stills;
	}
}

/* ---------- a setting of two or three choices, segmented
(tools/port_settings.py's SEGMENTED_MOST): each choice a segment of its row,
<key>_seg<n>, the one chosen lit; its spinner holds the choice (its
setting loaded, saved, its defaults and the panel's value), hidden while
the segments show, which they do on a wide screen where they fit in the
row (4:3, and a narrow screen where they do not, has the spinner) */

#define SETTINGS_SEGMENTS_MOST 3
/* (the room kept between the last segment and the row's right end) */
#define SETTINGS_SEGMENTS_MARGIN 8

/* a row's segments (count of them), its spinner, and whether they show */
static short settings_segments(struct widget_instance *row, struct widget_instance **segments,
	struct widget_instance **spinner)
{
	struct halo_menu_model_row const *model_row = row ? menu_engine_model_row_of(row) : NULL;
	char name[64];
	short count = 0;

	*spinner = NULL;
	if (!model_row || !model_row->key)
		return 0;
	snprintf(name, sizeof(name), "%s_spinner", model_row->key);
	*spinner = named(row, name, 0);
	for (count = 0; count < SETTINGS_SEGMENTS_MOST; count++)
	{
		snprintf(name, sizeof(name), "%s_seg%d", model_row->key, count);
		if ((segments[count] = named(row, name, 0)) == NULL)
			break;
	}
	return *spinner ? count : 0;
}

/* whether a row's segments fit in it at the screen's width now (none at
the menus' 640, 4:3, which keeps its spinners) */
static boolean settings_segments_fit(struct widget_instance *row, struct widget_instance *last)
{
	struct halo_menu_model const *model = menu_engine_model();
	short width;

	if (!model || !ui_widget_port_wide_extra())
		return FALSE;
	ui_widget_port_text_box_font(last, &width);
	return widget_screen_x(last) + width + SETTINGS_SEGMENTS_MARGIN <= widget_screen_x(row) +
		model->geometry.row_width + ui_widget_port_wide_stretch(row->definition_tag_index);
}

/* (each frame of a page) each segmented row's segments shown where they fit,
the one chosen lit; else its spinner */
static void settings_segments_show(struct widget_instance *list)
{
	struct widget_instance *row;

	for (row = list->child; row; row = row->next)
	{
		struct widget_instance *segments[SETTINGS_SEGMENTS_MOST];
		struct widget_instance *spinner;
		short count = settings_segments(row, segments, &spinner);
		boolean shown;
		short index;

		if (!count || !row->visible)
			continue;
		shown = settings_segments_fit(row, segments[count - 1]);
		spinner->visible = !shown;
		for (index = 0; index < count; index++)
		{
			segments[index]->visible = shown;
			segments[index]->animation.current_frame_index =
				(short)(index == spinner->parameters.list.selected_index);
		}
	}
}

/* "port setting segment" (a segmented row's spinner's A, which a click on
the row presses as its spinner is hidden): the segment clicked chosen, or
with no click the next; nothing while the spinner shows (left and right
step it, as they do the segments) */
static boolean settings_segment_pick(struct widget_instance *widget)
{
	struct widget_instance *segments[SETTINGS_SEGMENTS_MOST];
	struct widget_instance *spinner;
	short count = settings_segments(widget ? widget->parent : NULL, segments, &spinner);
	short chosen, index, x, y;

	if (!count || spinner != widget || spinner->visible || spinner->disabled)
		return FALSE;
	chosen = (short)((spinner->parameters.list.selected_index + 1) % count);
	if (ui_widget_port_take_click(&x, &y))
	{
		chosen = NONE;
		for (index = 0; index < count; index++)
		{
			short width;

			ui_widget_port_text_box_font(segments[index], &width);
			if (x >= widget_screen_x(segments[index]) && x < widget_screen_x(segments[index]) + width)
				chosen = index;
		}
		if (chosen == NONE)
			return TRUE;
	}
	if (chosen != spinner->parameters.list.selected_index)
	{
		spinner->parameters.list.selected_index = chosen;
		ui_play_audio_feedback_sound(SOUND_CURSOR);
	}
	return TRUE;
}

/* "port settings page": each frame of a tab's or a page's list */
static void settings_page_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);
	short tab = settings_tab_of(screen);

	/* (the search's screen, the menu engine's: settings_search_menu.inc) */
	if (menu_engine_search_screen(screen))
	{
		settings_search_update(list);
		return;
	}
	/* (a page made: what was kept as it opened, for its Cancel; but made
	again as the search is left, what it kept as it first opened) */
	if (screen != settings_page.screen || screen->definition_tag_index != settings_page.screen_tag ||
		screen->creation_time != settings_page.screen_time)
	{
		boolean returned = settings_search_returned();

		settings_page.screen = screen;
		settings_page.screen_tag = screen->definition_tag_index;
		settings_page.screen_time = screen->creation_time;
		if (tab == NONE && !returned)
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
	settings_search_keys(list);
	settings_search_bars_show(list);
	settings_rows_conditions(list);
	discord_account_show(list);
	clear_caches_show(list);
	hdr_display_show(list);
	pad_editor_update(list);
	controls_page_update(list);
	graphics_preset_show(list);
	sim_rate_row_show(list);
	settings_held_rows_show(list);
	viewmodel_weapon_rows_show(list);
	gyro_rows_show(list);
	settings_each(screen, setting_stash);
	settings_segments_show(list);
	settings_sliders_show(list);
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

/* the profiles read again, player 1's chosen, and the list's first row with
it in its middle (of rows rows) */
static void profile_list_open(short rows)
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
	profile_list.first = (short)PIN(profile_list.chosen - rows / 2, 0, MAX(0, profile_list.count + 1 - rows));
}

/* "player profile list initialize": on player 1's profile */
static boolean profile_list_initialize(struct widget_instance *list)
{
	profile_list_open(PROFILE_ROWS);
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
#define CONTROL_ROWS 8
#define CONTROL_NAME_LENGTH 32

/* (a refused change's help shown this long: control_capture_poll) */
#define CONTROL_REFUSED_MILLISECONDS 4000

/* each control's setting, label, group, and whether it is the game's own,
which must keep a key (the game cannot be played without it) */
static struct
{
	char const *setting;
	wchar_t const *label;
	short group;
	boolean essential;
} const controls[] =
{
	{ "controls.move_forward", L"MOVE FORWARD", 0, TRUE },
	{ "controls.move_backward", L"MOVE BACKWARD", 0, TRUE },
	{ "controls.strafe_left", L"STRAFE LEFT", 0, TRUE },
	{ "controls.strafe_right", L"STRAFE RIGHT", 0, TRUE },
	{ "controls.jump", L"JUMP", 0, TRUE },
	{ "controls.crouch", L"CROUCH", 0, TRUE },
	{ "controls.fire", L"FIRE", 1, TRUE },
	{ "controls.throw_grenade", L"THROW GRENADE", 1, TRUE },
	{ "controls.melee", L"MELEE", 1, TRUE },
	{ "controls.reload", L"RELOAD", 1, TRUE },
	{ "controls.zoom", L"ZOOM", 1, TRUE },
	{ "controls.switch_weapon", L"SWITCH WEAPON", 1, TRUE },
	{ "controls.switch_grenade", L"SWITCH GRENADE", 1, TRUE },
	{ "controls.action", L"ACTION", 2, TRUE },
	{ "controls.flashlight", L"FLASHLIGHT", 2, TRUE },
	{ "controls.scoreboard", L"SHOW SCORES", 2, FALSE },
	{ "controls.pause", L"PAUSE MENU", 2, TRUE },
	{ "controls.screenshot", L"SCREENSHOT", 2, FALSE },
	{ "controls.report", L"SAVE REPORT", 2, FALSE },
	{ "controls.clip", L"CLIP", 2, FALSE },
	{ "controls.push_to_talk", L"PUSH TO TALK", 3, FALSE },
	{ "controls.chat", L"CHAT", 3, FALSE },
	{ "controls.team_chat", L"TEAM CHAT", 3, FALSE },
	{ "controls.party_accept", L"ACCEPT INVITE", 3, FALSE },
	{ "controls.vote_yes", L"VOTE YES", 3, FALSE },
	{ "controls.vote_no", L"VOTE NO", 3, FALSE },
	{ "controls.ping", L"PING", 3, FALSE },
	/* (MOVEMENT's last row, after CROUCH: a group's rows are its controls in
	this order) */
	{ "controls.walk", L"WALK", 0, FALSE },
	/* (COMMUNICATION's last row, after PING: the HUD's Spartan link,
	spartan_link.c) */
	{ "controls.spartan_link", L"SPARTAN LINK", 3, FALSE },
};

static struct
{
	/* the bindings shown, until OK writes them */
	char bindings[NUMBEROF(controls)][CONTROL_BINDINGS][CONTROL_NAME_LENGTH];
	/* the binding left and right choose, and the one being set */
	short slot;
	short capturing_control;
	short capturing_slot;
	/* the page the bindings were read for (a page opened reads them again) */
	long screen_tag, screen_time;
	/* when a change was refused last (its help shown a while), 0 for not */
	unsigned long refused_time;
} controls_screen = { { { { 0 } } }, 0, NONE, 0, NONE, 0, 0 };

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

		/* (the profile's edited, each a player's: setting_text) */
		if (defaults)
			config_default(controls[control].setting, text, sizeof(text));
		else if (!setting_text(controls[control].setting, text, sizeof(text), FALSE))
			text[0] = 0;
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
		char text[128], shown[128];

		control_bindings_text(control, text, sizeof(text));
		if (!setting_text(controls[control].setting, shown, sizeof(shown), FALSE))
			shown[0] = 0;
		if (strcmp(text, shown))
		{
			if (setting_write(controls[control].setting, text))
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

/* a row's text box of a binding (the first or the second) */
static struct widget_instance *control_binding_box(struct widget_instance *row, short slot)
{
	return named(row, slot ? "command_binding_second" : "command_binding", 0);
}

/* the binding of a row at a place on the screen: the second from its left
edge on, else the first */
static short control_slot_at(struct widget_instance *row, short x)
{
	struct widget_instance *second = control_binding_box(row, 1);

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

/* whether clearing the control's bindings that are name (NULL: the one in
slot) would take its last key: a game control must keep one */
static boolean control_left_without_key(short control, short slot, char const *name)
{
	boolean taken = FALSE;
	short other_slot;

	if (!controls[control].essential)
		return FALSE;
	for (other_slot = 0; other_slot < CONTROL_BINDINGS; other_slot++)
	{
		char const *binding = controls_screen.bindings[control][other_slot];

		if (!*binding)
			continue;
		if (name ? _stricmp(binding, name) != 0 : other_slot != slot)
			return FALSE;
		taken = TRUE;
	}
	return taken;
}

/* a change that would leave a game control with no key: not made, the
error's sound and the help's why (controls_update) */
static void control_refuse(void)
{
	controls_screen.refused_time = system_milliseconds() ? system_milliseconds() : 1;
	ui_play_audio_feedback_sound(SOUND_ERROR);
}

/* the key or button taken (then bound to no other control), or cleared;
not where a game control would be left with no key (its only key taken
for another control, or cleared) */
static void control_capture_poll(void)
{
	short control = controls_screen.capturing_control;
	short slot = controls_screen.capturing_slot;
	int input = -1;
	int result;

	if (control == NONE || !(result = platform_binding_capture_poll(&input)))
		return;
	controls_screen.capturing_control = NONE;
	controls_screen.refused_time = 0;
	if (result == 1)
	{
		char name[CONTROL_NAME_LENGTH];
		short other, other_slot;

		halo_input_name(input, name, sizeof(name));
		if (!*name)
			return;
		for (other = 0; other < NUMBEROF(controls); other++)
		{
			if (other != control && control_left_without_key(other, NONE, name))
			{
				control_refuse();
				return;
			}
		}
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
		if (control_left_without_key(control, slot, NULL))
		{
			control_refuse();
			return;
		}
		controls_screen.bindings[control][slot][0] = 0;
	}
}

/* each frame of Key Bindings' list (settings_page_update, after
controls_page_update): the group's rows, and their bindings (the one left
and right choose, or the pointer is over, marked on the row chosen) */
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
				snprintf(shown, sizeof(shown), "[ %s ]", name);
			else
				snprintf(shown, sizeof(shown), "%s", name);
			for (index = 0; shown[index] && index < ROW_TEXT_LENGTH - 1; index++)
				text[index] = (wchar_t)(unsigned char)shown[index];
			text[index] = 0;
			text_set(control_binding_box(row, slot), text);
		}
	}
}

/* whether the screen is Key Bindings' page (tools/port_settings.py's
KEY_BINDINGS: its rows of actions, op_command_<n>) */
static boolean controls_page(struct widget_instance *screen)
{
	return screen && named(screen, "op_command_1", 0) != NULL;
}

/* each frame of the page's list: the bindings read again as the page opens,
then its rows (controls_update) */
static void controls_page_update(struct widget_instance *list)
{
	struct widget_instance *screen = screen_of(list);

	if (!controls_page(screen))
		return;
	if (screen->definition_tag_index != controls_screen.screen_tag ||
		screen->creation_time != controls_screen.screen_time)
	{
		controls_screen.screen_tag = screen->definition_tag_index;
		controls_screen.screen_time = screen->creation_time;
		controls_screen.slot = 0;
		controls_load(FALSE);
	}
	controls_update(list);
}

/* the page's OK ("port settings keep"): the bindings written */
static boolean controls_page_save(struct widget_instance *screen)
{
	return !controls_page(screen) || controls_save();
}

/* the page's Cancel, B and Escape ("port settings page cancel"): a key
being taken is dropped, the page kept (FALSE); else the page is left, its
changes forgotten (read again when it opens) */
static boolean controls_page_cancel(struct widget_instance *screen)
{
	if (!controls_page(screen) || controls_screen.capturing_control == NONE)
		return TRUE;
	controls_screen.capturing_control = NONE;
	platform_binding_capture_cancel();
	return FALSE;
}

/* the page's Defaults ("port settings defaults"): every action's default
keys shown, written by OK */
static void controls_page_defaults(struct widget_instance *screen)
{
	if (controls_page(screen))
		controls_load(TRUE);
}

/* the panel's words for an action's row (controls_help_index's): how to
change its keys, while one is taken how to give it, and why a change was
refused */
static char const *const control_help[] =
{
	"Enter, A or a click takes the next key or button pressed for the binding chosen; left and right choose "
	"which of the two, as does the pointer.|A key put here leaves any other action it was on.",
	"Press a key or a mouse button, or turn the wheel.|Escape cancels; Delete leaves it without one.",
	"That was this action's only key, and the game needs one for it. Give it another key first.",
};

/* which of control_help's words the panel shows: while a key is taken, a
while after a change was refused, or else how to change them */
static short controls_help_index(void)
{
	boolean refused = controls_screen.refused_time &&
		system_milliseconds() - controls_screen.refused_time < CONTROL_REFUSED_MILLISECONDS;

	return (short)(controls_screen.capturing_control != NONE ? 1 : refused ? 2 : 0);
}

/* the panel's name, value and words for an action's row: its name, its
keys, and how to change them (while one is taken, how to give it) */
static boolean controls_panel(struct widget_instance *row, wchar_t *name, wchar_t *value, wchar_t const **help)
{
	static wchar_t words[SETTINGS_PANEL_TEXT_LENGTH];
	short control = control_of_row(row, controls_group(screen_of(row)));
	char text[2 * CONTROL_NAME_LENGTH + 8];
	short slot;

	if (!row->name || strncmp(row->name, "op_command_", 11) || control == NONE)
		return FALSE;
	ustrncpy(name, controls[control].label, ROW_TEXT_LENGTH - 1);
	name[ROW_TEXT_LENGTH - 1] = 0;
	/* (its keys as the keyboard's layout labels them, NONE without one) */
	text[0] = 0;
	for (slot = 0; slot < CONTROL_BINDINGS; slot++)
	{
		char shown[CONTROL_NAME_LENGTH];

		if (!controls_screen.bindings[control][slot][0])
			continue;
		halo_input_shown_name(controls_screen.bindings[control][slot], shown, sizeof(shown));
		snprintf(text + strlen(text), sizeof(text) - strlen(text), "%s%s", text[0] ? ", " : "", shown);
	}
	discord_widen(text[0] ? text : "NONE", value, ROW_TEXT_LENGTH);
	discord_widen(control_help[controls_help_index()], words, SETTINGS_PANEL_TEXT_LENGTH);
	*help = words;
	return TRUE;
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

/* ---------- the questions over the main menu: the update question
(updater.c's, the Steam Frame's and gamescope's) and the crash question
(report.c's) */

static struct
{
	/* the main menu drawn since a question last looked, its tag, and the
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

/* the main menu drawn: a hand-written one's, as the PC version's QUIT
item's "port update question" notes it (menu_screen.c) */
void pc_menu_main_menu_shown(struct widget_instance *screen)
{
	update_question_note(screen);
}

/* between frames (not while the widgets are drawn, which opening a screen
would free): a question's screen opened over the main menu, B going back to
it, if the main menu was drawn last; 1 when it was opened, or cannot be */
static int main_menu_question_open(char const *screen_name)
{
	boolean shown = update_question.main_menu_shown;

	update_question.main_menu_shown = FALSE;
	if (!shown || update_question.main_menu_generation != cache_files_tags_generation())
		return 0;
	if (!ui_widget_load_by_name_or_tag(screen_name, NONE, NULL, NONE, update_question.main_menu_tag, NONE, NONE))
		platform_log("menus: no screen %s for its question", screen_name);
	return 1;
}

/* the updater's: the update question */
int pc_menu_update_question_open(void)
{
	return main_menu_question_open("pc\\main_menu\\update\\update_screen");
}

/* report.c's: the crash question (the last run closed unexpectedly), whose
REPORT runs "port crash report" and SKIP "port crash dismiss" */
int pc_menu_crash_question_open(void)
{
	return main_menu_question_open("pc\\main_menu\\crash\\crash_screen");
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
/* (and the hand-written lobby's lanes' rows, at most: lobby_menu.inc's
lobby_board_lay_out_rows) */
#define LOBBY_LAYOUT_ROWS 16
#define LOBBY_ICON_ROWS (LOBBY_LAYOUT_ROWS * LOBBY_BOARD_COLUMNS)
/* (the game's data input "net splitscreen prejoin players", by its index:
menu_tags.c's game_data_input_names) */
#define LOBBY_PREJOIN_FUNCTION 12
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
types into it (Ctrl+V pastes), its row's A is done, B (escape) cancels.
Enter is its row's A too, posted to the menus (text_field_show), never the
controller's Start, which in a game closes the menus (ui_widget.c's
ui_check_for_pause_game) and would throw away what was typed and every
change on the screen (port/linux/src/xinput_sdl.c's typing_gamepad). A
value's field (a slider's number) starts with its value chosen: the first
key typed replaces it, the first backspace clears it, as a value box does
(text_field_begin_value) */
static struct
{
	struct widget_instance *row;
	char text[TEXT_FIELD_LENGTH];
	char before[TEXT_FIELD_LENGTH];
	short maximum;
	void (*done)(char const *text);
	/* a password's: shown as stars (text_field_begin_masked) */
	boolean masked;
	/* the text is the value as it was, chosen: what is typed first
	replaces it (text_field_begin_value) */
	boolean chosen;
	/* Enter ends the field itself, keeping what was typed, not as its row's
	A (a browser's search, whose row the pointer may have left: the A would
	be another's) */
	boolean enter_ends;
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
	text_field.enter_ends = FALSE;
	text_field.chosen = FALSE;
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

/* a value's field (a slider's number): as text_field_begin, its value
chosen, which the first key typed replaces */
static void text_field_begin_value(struct widget_instance *row, char const *text, short maximum,
	void (*done)(char const *text))
{
	text_field_open(row, text, maximum, done, FALSE);
	text_field.chosen = text[0] != 0;
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
	size_t length;

	/* (the value chosen: replaced) */
	if (text_field.chosen)
		text_field.text[0] = 0;
	text_field.chosen = FALSE;
	length = strlen(text_field.text);

	for (; *text && length < (size_t)text_field.maximum; text++)
	{
		if (*text >= ' ' && *text <= '~')
			text_field.text[length++] = *text;
	}
	text_field.text[length] = 0;
}

/* (ui_widget.c) a text box's caret, lit this frame or not: a bar after its
text, on its capitals' middle */
void ui_widget_port_caret_show(struct widget_instance *box, boolean shown);

/* the keys typed since the last frame, and the field shown (a caret
blinking while it is edited) */

static void text_field_show(struct widget_instance *value, char const *text, boolean editing)
{
	wchar_t shown[TEXT_FIELD_LENGTH + 2];
	short index;

	if (editing)
	{
		struct key_stroke key;

		text_field_shown_time = system_milliseconds();
		while (input_get_key(&key))
		{
			size_t length = strlen(text_field.text);

			/* (Enter: the row's A, which ends the field as the row ends
			it, keeping what was typed; no key after it is typed) */
			if (key.key_code == _key_return)
			{
				if (text_field.enter_ends)
					text_field_end(TRUE);
				else
					ui_widget_port_post_button(0, BUTTON_A);
				while (input_get_key(&key))
					;
				break;
			}
			if (key.key_code == _key_backspace)
			{
				/* (the value chosen: cleared) */
				if (text_field.chosen)
					text_field.text[0] = 0;
				else if (length)
					text_field.text[length - 1] = 0;
				text_field.chosen = FALSE;
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
	shown[index] = 0;
	text_set(value, shown);
	/* (the caret: drawn by the text box, after the text, on its capitals'
	middle; lit half the time, by the menus' clock: the frames' where it is
	fixed, debug.frame_time, so that two runs draw it alike,
	tools/menu_parity.py) */
	{
		unsigned long blink_time;

		if (!frames_clock(&blink_time))
			blink_time = system_milliseconds();
		ui_widget_port_caret_show(value, editing && (blink_time / 500) % 2);
	}
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
types a value into its number ("port slider edit": the number chosen, so
that what is typed replaces it, and a word's field empty; Enter, the row's A
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

/* the most stops a slider has */
#define SETTINGS_SLIDER_MAXIMUM_STOPS 32
/* the sliders made from the model at once (each screen's few) */
#define SETTINGS_MAXIMUM_SLIDERS 64

/* a slider, from its row of the model (tools/port_settings.py's Slider, in
port/assets/menus/model): its setting, its track's range and step, the
values it may be typed, whether they are whole numbers, and its stops. A
stop whose value is a word of the setting's (display.vehicle_fov's
"default" and "match") is placed below the numbers, at -1 for the last
such, -2 for the one before */
struct settings_slider
{
	struct halo_menu_model_row const *row;
	char const *setting;
	double least, most, step;
	double typed_least, typed_most;
	boolean whole;
	struct settings_slider_stop stops[SETTINGS_SLIDER_MAXIMUM_STOPS];
	short stop_count;
};

static struct
{
	struct halo_menu_model const *model;
	unsigned long reads;
	struct settings_slider sliders[SETTINGS_MAXIMUM_SLIDERS];
	short count;
} settings_slider_cache;

/* the track and the thumb, as tools/port_settings.py lays them out
(SLIDER_TRACK_WIDTH, SLIDER_THUMB_WIDTH); a wide screen's track is longer
(its wide layout: slider_track_width) */
#define SLIDER_TRACK_WIDTH 62
#define SLIDER_THUMB_WIDTH 8

/* a slider's track's width in the layout now */
static short slider_track_width(struct widget_instance *track)
{
	return (short)(SLIDER_TRACK_WIDTH + ui_widget_port_wide_stretch(track->definition_tag_index));
}

/* the slider the mouse drags (its row), and the one typed into */
static struct
{
	struct widget_instance *dragged;
	struct settings_slider const *typed;
} settings_slider = { NULL, NULL };

/* the slider of the model's row, made from it once (NULL: the row is no
slider's) */
static struct settings_slider const *settings_slider_made(struct halo_menu_model_row const *row)
{
	struct halo_menu_model const *model = menu_engine_model();
	struct settings_slider *slider;
	long index, words = 0;

	if (!model || !row || strcmp(row->kind, "slider"))
		return NULL;
	if (settings_slider_cache.model != model || settings_slider_cache.reads != halo_menus_model_reads())
	{
		settings_slider_cache.model = model;
		settings_slider_cache.reads = halo_menus_model_reads();
		settings_slider_cache.count = 0;
	}
	for (index = 0; index < settings_slider_cache.count; index++)
	{
		if (settings_slider_cache.sliders[index].row == row)
			return &settings_slider_cache.sliders[index];
	}
	if (settings_slider_cache.count == SETTINGS_MAXIMUM_SLIDERS)
		settings_slider_cache.count = 0;
	slider = &settings_slider_cache.sliders[settings_slider_cache.count++];
	memset(slider, 0, sizeof(*slider));
	slider->row = row;
	slider->setting = row->setting;
	slider->least = row->least;
	slider->most = row->most;
	slider->step = row->step;
	slider->typed_least = row->typed_least;
	slider->typed_most = row->typed_most;
	slider->whole = row->whole != 0;
	for (index = 0; index < row->choice_count; index++)
	{
		double value;

		if (!text_is_number(model->choices[row->first_choice + index].value, &value))
			words++;
	}
	for (index = 0; index < row->choice_count && slider->stop_count < SETTINGS_SLIDER_MAXIMUM_STOPS; index++)
	{
		struct halo_menu_model_choice const *choice = &model->choices[row->first_choice + index];
		struct settings_slider_stop *stop = &slider->stops[slider->stop_count++];
		double shown;

		if (text_is_number(choice->value, &stop->value))
		{
			stop->word = text_is_number(choice->shown, &shown) ? NULL : choice->shown;
			stop->text = NULL;
		}
		else
		{
			stop->value = (double)-words--;
			stop->word = choice->shown;
			stop->text = choice->value;
		}
	}
	return slider;
}

static struct settings_slider const *settings_slider_of(struct widget_instance const *row)
{
	return settings_slider_made(menu_engine_model_row_of(row));
}

/* the stop shown as a word that has this value, or NONE */
static short settings_slider_word(struct settings_slider const *slider, double value)
{
	short stop;

	for (stop = 0; stop < slider->stop_count; stop++)
	{
		if (slider->stops[stop].word && slider->stops[stop].value == value)
			return stop;
	}
	return NONE;
}

/* a setting's text as the slider's value: a stop's word of the setting's,
else its number */
/* a slider's setting */
static char const *settings_slider_setting(struct settings_slider const *slider)
{
	return slider->setting;
}

static boolean settings_slider_text_value(struct settings_slider const *slider, char const *text, double *value)
{
	short stop;

	for (stop = 0; stop < slider->stop_count; stop++)
	{
		if (slider->stops[stop].text && !_stricmp(text, slider->stops[stop].text))
		{
			*value = slider->stops[stop].value;
			return TRUE;
		}
	}
	return text_is_number(text, value);
}

/* whether the slider takes the value: one it may be typed, or a word's */
static boolean settings_slider_valid(struct settings_slider const *slider, double value)
{
	return (value >= slider->typed_least && value <= slider->typed_most) ||
		settings_slider_word(slider, value) != NONE;
}

/* the slider's value as Settings shows it: the change kept, else the
setting's (else its default) */
static double settings_slider_value(struct settings_slider const *slider)
{
	char text[64];
	double value;

	if (!settings_value(slider->setting, text, sizeof(text)) ||
		!settings_slider_text_value(slider, text, &value) || !settings_slider_valid(slider, value))
	{
		if (!setting_text(slider->setting, text, sizeof(text), TRUE) ||
			!settings_slider_text_value(slider, text, &value))
		{
			value = slider->least;
		}
	}
	return value;
}

/* a value as the slider writes it: a whole number, or at most three
decimals */
static void settings_slider_number(struct settings_slider const *slider, double value, char *text, unsigned int size)
{
	size_t length;

	if (slider->whole)
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
static void settings_slider_text(struct settings_slider const *slider, double value, char *text, unsigned int size)
{
	short word = settings_slider_word(slider, value);

	if (word != NONE)
		snprintf(text, size, "%s", slider->stops[word].word);
	else
		settings_slider_number(slider, value, text, size);
}

/* a value as the slider's field starts to be typed into: its number, or
nothing for a stop shown as a word */
static void settings_slider_field_text(struct settings_slider const *slider, double value, char *text, unsigned int size)
{
	if (settings_slider_word(slider, value) != NONE)
		text[0] = 0;
	else
		settings_slider_number(slider, value, text, size);
}

/* the value kept as a change, or none if it is the setting's */
static void settings_slider_set(struct settings_slider const *slider, double value)
{
	char text[64], current[64];
	short word = settings_slider_word(slider, value);

	if (word == NONE)
		value = PIN(value, slider->typed_least, slider->typed_most);
	if (word != NONE && slider->stops[word].text)
		snprintf(text, sizeof(text), "%s", slider->stops[word].text);
	else
		settings_slider_number(slider, value, text, sizeof(text));
	if (setting_text(slider->setting, current, sizeof(current), FALSE) &&
		settings_values_same(current, text))
	{
		settings_pending_remove(slider->setting);
	}
	else
	{
		settings_pending_set(slider->setting, text);
	}
}

/* where the value is along a slider's stops, from 0 (the first) to the
last's index: a stop's own place, or between the two numbers it is between
(past the numbers' ends, at the end) */
static double settings_slider_stop_place(struct settings_slider const *slider, double value)
{
	struct settings_slider_stop const *stops = slider->stops;
	short count = slider->stop_count;
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
static short settings_slider_stop_nearest(struct settings_slider const *slider)
{
	if (!slider->stop_count)
		return NONE;
	return (short)(settings_slider_stop_place(slider, settings_slider_value(slider)) + 0.5);
}

/* the thumb's place along the track, from 0 to 1 */
static double settings_slider_fraction(struct settings_slider const *slider, double value)
{
	if (slider->stop_count > 1)
		return settings_slider_stop_place(slider, value) / (slider->stop_count - 1);
	return (value - slider->least) / (slider->most - slider->least);
}

/* a value to the nearest step, either side of 0 */
static double settings_slider_round(double value, double step)
{
	double steps = value / step;

	return (double)(long)(steps < 0.0 ? steps - 0.5 : steps + 0.5) * step;
}

/* the value at a place on the track (its range, in steps) */
static void settings_slider_place(struct settings_slider const *slider, struct widget_instance *track, short x)
{
	double fraction = (double)(x - widget_screen_x(track) - SLIDER_THUMB_WIDTH / 2) /
		(slider_track_width(track) - SLIDER_THUMB_WIDTH);
	double least = slider->least, most = slider->most;
	double step = slider->step;
	double value = least + PIN(fraction, 0.0, 1.0) * (most - least);
	short count = slider->stop_count;

	/* (with stops, the one nearest) */
	if (count > 1)
	{
		settings_slider_set(slider,
			slider->stops[(short)(PIN(fraction, 0.0, 1.0) * (count - 1) + 0.5)].value);
		return;
	}
	settings_slider_set(slider, PIN(settings_slider_round(value, step), least, most));
}

/* (the number typed: kept if it is one, past 0 (or in a range that takes 0
and below), or a word's; or a word) */
static void settings_slider_typed(char const *text)
{
	struct settings_slider const *slider = settings_slider.typed;
	double value;
	short stop;

	settings_slider.typed = NULL;
	if (!slider)
		return;
	if (text_is_number(text, &value) && (value > 0.0 || slider->typed_least <= 0.0 ||
		settings_slider_word(slider, value) != NONE))
	{
		settings_slider_set(slider, value);
		return;
	}
	for (stop = 0; stop < slider->stop_count; stop++)
	{
		if (slider->stops[stop].word && !_stricmp(text, slider->stops[stop].word))
		{
			settings_slider_set(slider, slider->stops[stop].value);
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
	struct settings_slider const *slider = settings_slider_of(row);
	struct widget_instance *track = settings_slider_track(row);

	if (!slider || !track)
		return FALSE;
	if (!strcmp(name, "left") || !strcmp(name, "right"))
	{
		double step = slider->step;
		double value = settings_slider_value(slider);
		short count = slider->stop_count;

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

			settings_slider_set(slider, slider->stops[PIN(stop, 0, count - 1)].value);
			ui_play_audio_feedback_sound(SOUND_CURSOR);
			return TRUE;
		}
		value = settings_slider_round(value, step) + (!strcmp(name, "left") ? -step : step);
		settings_slider_set(slider, PIN(value, slider->least, slider->most));
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
			x < widget_screen_x(track) + slider_track_width(track) + 4)
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
		/* (its number, chosen, which typing replaces; a word (DEFAULT,
		AUTO, NONE, OFF) is no number to edit: an empty field, typed into
		at once) */
		settings_slider.typed = slider;
		settings_slider_field_text(slider, settings_slider_value(slider), text, sizeof(text));
		text_field_begin_value(row, text, 7, settings_slider_typed);
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
	struct settings_slider const *slider = NULL;
	short depth;

	for (depth = 0; row && depth < 3 && (slider = settings_slider_of(row)) == NULL; depth++)
		row = row->parent;
	track = slider ? settings_slider_track(row) : NULL;
	if (!track || x < widget_screen_x(track) - 4 || x >= widget_screen_x(track) + slider_track_width(track) + 4)
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
		struct settings_slider const *slider = settings_slider_of(row);
		struct widget_instance *track = slider ? settings_slider_track(row) : NULL;
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
			thumb->horizontal_offset = (short)(PIN(fraction, 0.0, 1.0) * (slider_track_width(track) - SLIDER_THUMB_WIDTH) + 0.5);
		for (value_text = track->next; value_text && value_text->type != 1; value_text = value_text->next)
		{
		}
		settings_slider_text(slider, value, text, sizeof(text));
		text_field_show(value_text, text, text_field_editing(row));
		/* (typed into: its field lit, and the caret held on, not blinking,
		so that it is plain that the number takes what is typed) */
		if (value_text)
		{
			value_text->animation.current_frame_index = (short)(text_field_editing(row) != FALSE);
			if (text_field_editing(row))
				ui_widget_port_caret_show(value_text, TRUE);
		}
	}
}

/* "port settings defaults": the sliders' settings at their defaults (what
is being typed forgotten) */
static void settings_sliders_default(struct widget_instance *screen)
{
	struct halo_menu_model_screen const *model = menu_engine_model_screen_of(screen);
	char text[64];
	double value;
	long index;

	if (text_field_editing(NULL))
		text_field_end(FALSE);
	for (index = 0; model && index < model->row_count; index++)
	{
		struct settings_slider const *slider = settings_slider_made(&menu_engine_model()->rows[model->first_row + index]);
		char name[64];

		if (!slider)
			continue;
		snprintf(name, sizeof(name), "op_%s", slider->row->key);
		if (named(screen, name, 0) && setting_text(slider->setting, text, sizeof(text), TRUE) &&
			settings_slider_text_value(slider, text, &value))
		{
			settings_slider_set(slider, value);
		}
	}
}

/* "port settings weapon reset" (RESET THIS WEAPON): the weapon held's
own offsets at 0, kept as changes (OK writes them: its table taken out of
config.toml; Cancel forgets them) */
static void settings_weapon_reset(struct widget_instance *screen)
{
	struct halo_menu_model_screen const *model = menu_engine_model_screen_of(screen);
	long index;

	if (text_field_editing(NULL))
		text_field_end(FALSE);
	for (index = 0; model && index < model->row_count; index++)
	{
		struct settings_slider const *slider = settings_slider_made(&menu_engine_model()->rows[model->first_row + index]);
		char name[64];

		if (!slider)
			continue;
		snprintf(name, sizeof(name), "op_%s", slider->row->key);
		if (settings_weapon_value(slider->setting) != NONE && named(screen, name, 0))
			settings_slider_set(slider, 0.0);
	}
}

/* ---- the server browsers' Filters page (tools/port_settings.py's
BROWSER_FILTERS, its rows the network.browser_* settings): opened by the
browsers' FILTERS (the left trigger; "port browser filters", which starts
it with no change kept: its OK writes its own, as a page of Settings' does)
or by Settings' Online. The browser reads them all again as it opens
(browser_filters_read); its searches, which are not settings, stay as they
were (browser_searches) */

/* (a browser's search being typed into: below) */
static boolean browser_search_editing(short *search);
static void browser_search_end(void);

/* "port browser filters": not Join by Link's, whose games are not filtered */
static boolean browser_filters_open(void)
{
	if (multiplayer.mode == _multiplayer_mode_direct_link)
		return FALSE;
	/* (a search being typed: what is typed kept) */
	if (browser_search_editing(NULL))
		browser_search_end();
	else if (text_field_editing(NULL))
		text_field_end(FALSE);
	settings_pending_clear();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
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
	p2p_friends_set_game_closed(FALSE);
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

static struct
{
	short first, chosen, step, level;
	boolean multiplayer_only;
	boolean campaign;
} mcc_host_list;

static struct mcc_map_browser mcc_host_browsers[2];

static void mcc_host_map_text(short row, wchar_t *text)
{
	mcc_map_browser_text(&mcc_host_browsers[mcc_host_list.campaign], row, text);
	mcc_menu_text_row(text);
}

static void mcc_host_list_update(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct mcc_map_browser *browser = &mcc_host_browsers[mcc_host_list.campaign];
	short count = mcc_host_list.step == MAP_STEP_DIFFICULTIES ? NUMBER_OF_GAME_DIFFICULTY_LEVELS :
		browser->count;
	short row = map_kind_rows_update(list, &mcc_host_list.first, count,
		mcc_host_list.step == MAP_STEP_DIFFICULTIES ? map_difficulty_text : mcc_host_map_text);

	if (row != NONE)
	{
		mcc_host_list.chosen = row;
		if (mcc_host_list.step != MAP_STEP_DIFFICULTIES)
			mcc_map_browser_select(browser, row);
	}
	if (mcc_host_list.step == MAP_STEP_DIFFICULTIES)
		mcc_map_description_show(description, mcc_host_list.level);
	else
		mcc_browser_description(description, browser);
	profile_name_show(description);
}

static boolean mcc_host_list_choose(struct widget_instance *list, boolean *widget_deleted)
{
	struct mcc_map_browser *browser = &mcc_host_browsers[mcc_host_list.campaign];
	short index = mcc_host_list.step == MAP_STEP_DIFFICULTIES ? mcc_host_list.level :
		mcc_map_browser_index(browser, mcc_host_list.chosen);
	char const *level_name = mcc_maps_level_name(index);

	if (mcc_host_list.step != MAP_STEP_DIFFICULTIES && mcc_map_browser_enter(browser, mcc_host_list.chosen))
	{
		mcc_browser_focus(list, browser, &mcc_host_list.first, &mcc_host_list.chosen);
		ui_play_audio_feedback_sound(SOUND_FORWARD);
		return FALSE;
	}
	if (!level_name)
		return campaign_fail();
	if (mcc_host_list.step == MAP_STEP_DIFFICULTIES)
	{
		if (!ui_widget_port_mcc_cooperative_level_choose(level_name, mcc_host_list.chosen))
			return campaign_fail();
		return ui_widget_port_open(list, SERVER_SETUP_NAME, widget_deleted);
	}
	if (!mcc_maps_campaign(index))
		return ui_widget_port_mcc_multiplayer_map_choose(level_name);
	if (mcc_host_list.multiplayer_only)
		return campaign_fail();
	mcc_map_browser_select(browser, mcc_host_list.chosen);
	mcc_host_list.level = index;
	mcc_host_list.step = MAP_STEP_DIFFICULTIES;
	mcc_host_list.chosen = (short)PIN(main_get_difficulty(), 0, NUMBER_OF_GAME_DIFFICULTY_LEVELS - 1);
	mcc_host_list.first = 0;
	map_kind_focus(list, 0, mcc_host_list.chosen);
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return FALSE;
}

/* "mp level list initialize": on the multiplayer map used last */
static boolean map_list_initialize(struct widget_instance *list)
{
	char const *const *names;
	short last_used = 0;
	short mcc_kind = map_list.kind;

	map_list.hosting = global_network_game_server_get() != NULL && !network_game_is_splitscreen_local();
	mcc_maps_rescan();
	csmemset(&mcc_host_list, 0, sizeof(mcc_host_list));
	mcc_host_list.multiplayer_only = !map_list.hosting;
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
	if (map_kind_mcc(mcc_kind) && (map_list.hosting || mcc_kind == MAP_KIND_MCC_MULTIPLAYER))
	{
		map_list.kind = mcc_kind;
		mcc_host_list.campaign = mcc_kind == MAP_KIND_MCC_SINGLEPLAYER;
		mcc_map_browser_open(&mcc_host_browsers[mcc_host_list.campaign], mcc_host_list.campaign);
		map_kind_set(list, mcc_kind);
		mcc_browser_focus(list, &mcc_host_browsers[mcc_host_list.campaign],
			&mcc_host_list.first, &mcc_host_list.chosen);
	}
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
		if (map_kind_mcc(kind))
		{
			boolean chooser = focused_row(list) == 0;

			csmemset(&mcc_host_list, 0, sizeof(mcc_host_list));
			mcc_host_list.multiplayer_only = !map_list.hosting;
			mcc_host_list.campaign = kind == MAP_KIND_MCC_SINGLEPLAYER;
			mcc_map_browser_open(&mcc_host_browsers[mcc_host_list.campaign], mcc_host_list.campaign);
			mcc_browser_focus(list, &mcc_host_browsers[mcc_host_list.campaign],
				&mcc_host_list.first, &mcc_host_list.chosen);
			if (chooser)
				focus_row(list, 0);
		}
		map_list.kind = kind;
		map_list.step = MAP_STEP_MAPS;
		map_list.first = map_list.chosen = 0;
	}
	if (map_kind_mcc(kind))
	{
		mcc_host_list_update(list);
		return;
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

	if (map_kind_mcc(map_list.kind))
		return mcc_host_list_choose(list, widget_deleted);

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
	if (map_kind_mcc(map_list.kind) && mcc_host_list.step == MAP_STEP_DIFFICULTIES)
	{
		mcc_host_list.step = MAP_STEP_MAPS;
		mcc_browser_focus(list, &mcc_host_browsers[mcc_host_list.campaign],
			&mcc_host_list.first, &mcc_host_list.chosen);
		ui_play_audio_feedback_sound(SOUND_BACK);
		return TRUE;
	}
	if (map_kind_mcc(map_list.kind) && mcc_map_browser_back(&mcc_host_browsers[mcc_host_list.campaign]))
	{
		mcc_browser_focus(list, &mcc_host_browsers[mcc_host_list.campaign],
			&mcc_host_list.first, &mcc_host_list.chosen);
		ui_play_audio_feedback_sound(SOUND_BACK);
		return TRUE;
	}
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

/* Screens consume B before forwarding events to their list children. */
static boolean mcc_host_back(struct widget_instance *widget, boolean *widget_deleted)
{
	struct widget_instance *list = named(widget, "mp_map_select_list_2", 0);

	if (list && map_kind_mcc(map_list.kind))
		return map_list_back(list, widget_deleted);
	ui_play_audio_feedback_sound(SOUND_BACK);
	ui_widget_port_go_back(widget);
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

/* the frame of a game engine's picture in the gametypes' pictures
(bitmaps/game_type_grafix, Game Options' rules_options): the PC version's
order (ui_widget_game_data_input_functions.c's multiplayer_game_bitmap_frame),
not the engines', with the unknown one's (game_type_grafix's "?") last */
enum
{
	_gametype_picture_ctf = 0,
	_gametype_picture_king,
	_gametype_picture_slayer,
	_gametype_picture_oddball,
	_gametype_picture_race,
	_gametype_picture_unknown,
};

static short gametype_picture_frame(long engine)
{
	switch (engine)
	{
	case game_engine_ctf:
		return _gametype_picture_ctf;
	case game_engine_king:
		return _gametype_picture_king;
	case game_engine_slayer:
		return _gametype_picture_slayer;
	case game_engine_oddball:
		return _gametype_picture_oddball;
	case game_engine_race:
		return _gametype_picture_race;
	default:
		return _gametype_picture_unknown;
	}
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
			if ((widget = named(description, "gametype_right_pic", 0)) != NULL)
				widget->animation.current_frame_index = gametype_picture_frame(variant.game_engine_index);
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
	if (game && mcc_level_name(game->map.name))
		return !game->variant.game_engine_index &&
			(mcc_maps_level_campaign(game->map.name) || mcc_maps_find(game->map.name) == NONE);
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

/* the most players a co-op game hosted starts with (maximum_players') */
#define COOPERATIVE_DEFAULT_PLAYERS 16

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
network.host_public started it), or co-op's, as network.coop_join keeps its
WHO CAN JOIN (listed for ANYONE alone) */
static boolean server_settings_private(void)
{
	return hosting_cooperative() ? strcmp(config_string("network.coop_join"), "anyone") != 0 :
		multiplayer.game_private;
}

/* an internet game hosted (Create Game's INTERNET) */
static boolean server_settings_internet_host(void)
{
	return multiplayer.mode == _multiplayer_mode_host_internet;
}

/* a game of this machine alone (Create Game's OFFLINE) */
static boolean server_settings_local_game(void)
{
	void *server = global_network_game_server_get();

	return server && network_game_is_splitscreen_local();
}

static void server_settings_private_set(boolean private_game)
{
	if (!hosting_cooperative())
		multiplayer.game_private = private_game;
	/* (co-op's WHO CAN JOIN: PUBLIC is ANYONE, PRIVATE FRIENDS, its
	friends offered JOIN again: coop_menu.inc) */
	else if (private_game != server_settings_private())
	{
		if (!config_write("network.coop_join", private_game ? "friends" : "anyone"))
			platform_log("menus: could not write network.coop_join to config.toml");
		p2p_friends_set_game_closed(FALSE);
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
	/* (co-op's INVITE ONLY: its friends offered no JOIN) */
	p2p_friends_set_game_closed(hosting_cooperative() && !strcmp(config_string("network.coop_join"), "invite"));
	return TRUE;
}

static void wide_to_text(wchar_t const *wide, char *text, short size)
{
	short index;

	for (index = 0; wide[index] && index < size - 1; index++)
		text[index] = wide[index] < 0x80 ? (char)wide[index] : '?';
	text[index] = 0;
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
	/* the rows shown by their conditions in the model (SETUP_NODE_WHEN):
	co-op's options in the place of the gametype's, none of the network's in
	a game of this machine alone (a line saying what the game is in their
	place, LOCAL_NOTE; its bots are the lobby's, as every game's), LISTING an
	internet game's, PASSWORD a public one's */
	setup_nodes_conditions(list);
	/* LISTING (an internet game's): PUBLIC, listed in everyone's server
	browser, or PRIVATE, for this game */
	if ((spinner = named(list, "listing_spinner", 0)) != NULL && named(list, "op_listing", 0) &&
		named(list, "op_listing", 0)->visible)
	{
		boolean public = spinner->parameters.list.selected_index == 0;

		server_settings_private_set(!public);
		p2p_set_hosting_public(public);
	}
	/* GAME: OpenH1 at a tick rate, or Legacy (network.game_mode,
	network.sim_rate), written as it is chosen */
	if ((spinner = named(list, "game_mode_spinner", 0)) != NULL)
	{
		short index = (short)PIN(spinner->parameters.list.selected_index, 0, NUMBEROF(server_settings_game_modes) - 1);

		server_settings_game_mode_set(index);
	}
	/* PASSWORD (a PUBLIC internet game's): its stars, NONE if it has none
	(its condition again, for the listing just chosen) */
	setup_nodes_conditions(list);
	row = named(list, "op_password", 0);
	if (row && !row->visible && text_field_editing(row))
		text_field_end(FALSE);
	if (row && row->visible)
	{
		short index;

		for (index = 0; multiplayer.game_password[index] && index < (short)sizeof(text) - 1; index++)
			text[index] = '*';
		text[index] = 0;
		text_field_show(named(list, "password_value", 0), index ? text : "NONE", text_field_editing(row));
	}
	/* the help of the row with the focus: its node's in the model, each
	choice's its own (LISTING's and GAME's) */
	setup_help(list);
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

/* ---- a line too long for its box (issue 899): the whole of it kept for
the text box a fit cut it short in (menu_text_box_fit_to), or for a row's
own text (a list's row's, a spinner's value) measured once as it is drawn;
drawn by ui_widget.c (pc_menu_text_line). While its row has the focus the
whole line is drawn, cut to its box, and scrolled: still at its start for a
pause, moved left at a steady speed to its end, still there for a pause,
then from its start again (menu_text_scroll_offset), on the menus' clock;
while not, the line cut short with "..." */

/* how long a line is still at each end, in milliseconds, and how fast it
moves, in the menus' units a second (a 640 by 480 screen's pixels) */
#define MENU_TEXT_SCROLL_PAUSE 1500
#define MENU_TEXT_SCROLL_SPEED 30
/* the lines kept (by their widget: a few screens' rows and cells), the
places a widget's may be in, and the longest line */
#define MENU_TEXT_LINES 128
#define MENU_TEXT_LINE_PROBES 8
#define MENU_TEXT_LINE_LENGTH 128
/* a focus gone longer than this, and the line's scroll begins again as its
row is focused (a row hidden and shown again) */
#define MENU_TEXT_SCROLL_GONE 250

struct menu_text_line
{
	/* (the text box or spinner; NULL: a free place) */
	void const *widget;
	long font;
	/* (the room it is drawn in, the whole line's width and its fitted
	form's: NONE, not measured yet) */
	short width;
	short full_width;
	short shown_width;
	/* (cut short by a fit: shown is what the box was given; else shown is
	the widget's own text, and cut its form cut short while it is too long) */
	boolean fitted;
	boolean focused;
	unsigned long focus_time;
	unsigned long drawn_time;
	wchar_t shown[MENU_TEXT_LINE_LENGTH];
	wchar_t full[MENU_TEXT_LINE_LENGTH];
	wchar_t cut[MENU_TEXT_LINE_LENGTH];
};

static struct menu_text_line menu_text_lines[MENU_TEXT_LINES];

/* how far a line `overflow` units wider than its box is moved left
(fractions of a unit too), `elapsed` milliseconds after its row took the
focus: none for the pause, then the speed's units a second until it shows
its end, all of the overflow for the pause, then none again, and over */
static real menu_text_scroll_offset(short overflow, unsigned long elapsed)
{
	unsigned long travel, at;

	if (overflow <= 0)
		return 0.0f;
	travel = (unsigned long)overflow * 1000UL / MENU_TEXT_SCROLL_SPEED;
	at = elapsed % (2UL * MENU_TEXT_SCROLL_PAUSE + travel);
	if (at < MENU_TEXT_SCROLL_PAUSE)
		return 0.0f;
	if (at >= MENU_TEXT_SCROLL_PAUSE + travel)
		return (real)overflow;
	return (real)(at - MENU_TEXT_SCROLL_PAUSE) * (real)MENU_TEXT_SCROLL_SPEED / 1000.0f;
}

static boolean menu_text_same(wchar_t const *a, wchar_t const *b)
{
	for (; *a && *a == *b; a++, b++)
	{
	}
	return *a == *b;
}

/* whether a text is one line (a row's of more lines is drawn as it is) */
static boolean menu_text_one_line(wchar_t const *text)
{
	for (; *text; text++)
	{
		if (*text == '\r' || *text == '\n')
			return FALSE;
	}
	return TRUE;
}

/* a widget's line (make: a place taken for it, a free one or else the one
drawn longest ago of those it may have); NULL */
static struct menu_text_line *menu_text_line_of(void const *widget, boolean make)
{
	unsigned long first = (unsigned long)(((size_t)widget >> 4) % MENU_TEXT_LINES);
	struct menu_text_line *oldest = NULL;
	long probe;

	if (!widget)
		return NULL;
	for (probe = 0; probe < MENU_TEXT_LINE_PROBES; probe++)
	{
		struct menu_text_line *line = &menu_text_lines[(first + probe) % MENU_TEXT_LINES];

		if (line->widget == widget)
			return line;
		if (!oldest || (oldest->widget && (!line->widget || line->drawn_time < oldest->drawn_time)))
			oldest = line;
	}
	if (!make)
		return NULL;
	memset(oldest, 0, sizeof(*oldest));
	oldest->widget = widget;
	oldest->full_width = NONE;
	oldest->shown_width = NONE;
	return oldest;
}

/* a text box's text fitted to `width` (its box's: 0 or less), kept whole
for its scroll where it is cut short; returning how wide it is drawn (0
while its font is not known). The same text fitted again (each frame) is
not measured again */
static short menu_text_box_fit_to(struct widget_instance *box, wchar_t const *text, short width)
{
	wchar_t fitted[MENU_TEXT_LINE_LENGTH];
	short box_width;
	long font = ui_widget_port_text_box_font(box, &box_width);
	struct menu_text_line *line;

	if (!box)
		return 0;
	if (width <= 0)
		width = box_width;
	line = menu_text_line_of(box, FALSE);
	if (line && line->fitted && line->font == font && line->width == width && text &&
		menu_text_same(line->full, text) && line->shown_width != NONE)
	{
		text_set(box, line->shown);
		return line->shown_width;
	}
	menu_text_fit(font, text, fitted, NUMBEROF(fitted), width);
	text_set(box, fitted);
	/* (cut short: the whole kept; whole: none) */
	if (text && !menu_text_same(fitted, text))
	{
		line = menu_text_line_of(box, TRUE);
		line->fitted = TRUE;
		line->font = font;
		line->width = width;
		line->full_width = NONE;
		settings_wide_copy(line->shown, fitted, MENU_TEXT_LINE_LENGTH);
		settings_wide_copy(line->full, text, MENU_TEXT_LINE_LENGTH);
		line->shown_width = font != NONE ? settings_text_width(font, fitted) : 0;
		return line->shown_width;
	}
	if (line)
		line->widget = NULL;
	return font != NONE ? settings_text_width(font, fitted) : 0;
}

/* (ui_widget.c, as it draws a line of a text box or a spinner's value, in
its font, in a box `width` units wide: whether it is a row's text (a list's
row's, a spinner's value), whether its row has the focus, the menus' clock)
the line to draw in place of the widget's text, how far it is moved left
(*scroll, at most *overflow) and the room it is cut to (*room): the whole of it, scrolled,
while its row has the focus, its form cut short with "..." while not; NULL
to draw the widget's text as it is (it fits, or is no row's and was not
cut short by a fit) */
wchar_t const *pc_menu_text_line(struct widget_instance *widget, long font, wchar_t const *text, short width,
	boolean row, boolean focused, unsigned long now, real *scroll, short *room, short *overflow)
{
	struct menu_text_line *line = menu_text_line_of(widget, FALSE);

	*scroll = 0.0f;
	*overflow = 0;
	*room = width;
	if (!text || !text[0] || font == NONE || width <= 0)
		return NULL;
	/* (a fit's, while the box still has what it was given) */
	if (line && line->fitted && (line->font != font || !menu_text_same(line->shown, text)))
	{
		line->widget = NULL;
		line = NULL;
	}
	if (!line || !line->fitted)
	{
		if (!row)
			return NULL;
		if (!line || line->font != font || line->width != width || !menu_text_same(line->shown, text))
		{
			line = menu_text_line_of(widget, TRUE);
			line->font = font;
			line->width = width;
			settings_wide_copy(line->shown, text, MENU_TEXT_LINE_LENGTH);
			settings_wide_copy(line->full, text, MENU_TEXT_LINE_LENGTH);
			line->full_width = menu_text_one_line(text) ? settings_text_width(font, line->full) : 0;
			if (line->full_width > width)
				menu_text_fit(font, line->full, line->cut, MENU_TEXT_LINE_LENGTH, width);
		}
	}
	if (line->full_width == NONE)
		line->full_width = settings_text_width(font, line->full);
	/* (scrolled from its start as its row takes the focus) */
	if (focused && (!line->focused || now - line->drawn_time > MENU_TEXT_SCROLL_GONE))
		line->focus_time = now;
	line->focused = focused;
	line->drawn_time = now;
	*room = line->width;
	if (line->full_width <= line->width)
		return NULL;
	*overflow = (short)(line->full_width - line->width);
	if (focused)
	{
		*scroll = menu_text_scroll_offset(*overflow, now - line->focus_time);
		return line->full;
	}
	return line->fitted ? NULL : line->cut;
}

/* a text box's text fitted to it (to `width` if that is less, and more
than 0), returning how wide it is drawn (0 while its font is not known) */
static short menu_text_box_fit(struct widget_instance *box, wchar_t const *text, short width)
{
	short box_width;

	ui_widget_port_text_box_font(box, &box_width);
	return menu_text_box_fit_to(box, text, (short)(width > 0 ? MIN(width, box_width) : box_width));
}

/* a text box's text wrapped to its width, `lines` lines at most: the last
of them, if there is more, the rest of the text fitted to it with "..." */
static void menu_text_box_wrap_width(struct widget_instance *box, wchar_t const *text, short lines, short width);

static void menu_text_box_wrap(struct widget_instance *box, wchar_t const *text, short lines)
{
	short width;

	ui_widget_port_text_box_font(box, &width);
	menu_text_box_wrap_width(box, text, lines, width);
}

/* ... to a width of its own (a hand-written screen's box, wider than its
definition on a wide screen) */
static void menu_text_box_wrap_width(struct widget_instance *box, wchar_t const *text, short lines, short width)
{
	wchar_t wrapped[SETTINGS_PANEL_TEXT_LENGTH];
	short box_width;
	long font = ui_widget_port_text_box_font(box, &box_width);

	if (!box)
		return;
	menu_text_wrap_lines(font, text, width, lines, wrapped);
	text_set_length(box, wrapped, SETTINGS_PANEL_TEXT_LENGTH);
}

/* text wrapped across width in the font into wrapped (SETTINGS_PANEL_TEXT_LENGTH
characters), `lines` lines at most: the last of them, if there is more, the
rest of the text fitted to it with "..."; the lines it takes */
static short menu_text_wrap_lines(long font, wchar_t const *text, short width, short lines, wchar_t *wrapped)
{
	wchar_t last[SETTINGS_PANEL_TEXT_LENGTH];
	short line = 1, index, start = 0;

	settings_text_wrap(text, wrapped, SETTINGS_PANEL_TEXT_LENGTH, font, width);
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
			menu_text_fit(font, last, wrapped + start, (short)(SETTINGS_PANEL_TEXT_LENGTH - start), width);
			break;
		}
		line++;
		start = (short)(index + 2);
	}
	return line;
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
/* a group's heading's row (the server browser's), its words white and its
background never lit */
static boolean browser_row_heading[BROWSER_ROWS];
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
	/* (a hand-written screen's: menu_screen.c) */
	if (menu_screen_text_color(widget, color))
		return TRUE;
	if ((menu_tab_strip(widget->parent) && widget->animation.current_frame_index >= 2) ||
		browser_sort_header_current(widget))
	{
		color->red = color->green = color->blue = 1.0f;
		return TRUE;
	}
	if (row < 0 || row >= BROWSER_ROWS)
		return FALSE;
	if (browser_row_heading[row])
	{
		color->red = color->green = color->blue = 1.0f;
		return TRUE;
	}
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
		!strcmp(child->name, "browser_join_bar") || !strcmp(child->name, "browser_clear_bar") ||
		/* (the 2V2 browser's HOST GAME and FIND GAME, its friends) */
		!strcmp(child->name, "doubles_actions") || !strncmp(child->name, "doubles_friend_", 15);
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
		{
			row->animation.current_frame_index = browser_row_heading[row_index] ? 0 : row_index == chosen_row ? 2 :
				row == focused ? 1 : 0;
		}
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
static void list_focus_child(struct widget_instance *list, struct widget_instance *focused);
static boolean direct_link_from_clipboard(void);
static void lobby_browser_update(struct widget_instance *list);
static boolean lobby_browser_select(struct widget_instance *widget, short controller, short row, boolean button,
	boolean *widget_deleted);
static void browser_prompts_show(struct widget_instance *list);
static void browser_filters_refresh(void);

/* (the co-op list's model off: coop_menu.inc) */
static void coop_join_end(void);

/* "gamespy screen init": the mode's title and parts; the games' client */
static boolean browser_initialize(struct widget_instance *screen, struct event_record *event,
	boolean *widget_deleted)
{
	char const *title = multiplayer.mode == _multiplayer_mode_lan ? "header_lan" :
		multiplayer.mode == _multiplayer_mode_direct_link ? "header_direct_link" : "header_server_browser";
	char const *const titles[] = { "header_lan", "header_direct_link", "header_server_browser" };
	boolean link = multiplayer.mode == _multiplayer_mode_direct_link;
	short index;

	/* (the server browser's own list, never the co-op list's) */
	coop_join_end();
	server_browser_initialize(screen);
	/* (the filters as Filters left them: browser_filters_read, below) */
	browser_filters_refresh();
	for (index = 0; index < NUMBEROF(titles); index++)
		visible_set(named(screen, titles[index], 0), !strcmp(titles[index], title));
	/* (the tabs and their prompts, but for Join by Link's list, whose games
	are its invites) */
	visible_set(named(screen, "browser_tabs", 0), !link);
	/* (CLEAR SEARCH as the searches empty the list: browser_clear_show) */
	visible_set(named(screen, "browser_clear_bar", 0), FALSE);
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
			/* (the list opens on its tabs, or on its first game, as the
			computer's does: not on the touchscreen's search over them, its
			first child) */
			if (list->focused_child && !strcmp(list->focused_child->name, "join_game_header"))
			{
				struct widget_instance *tabs = named(list, "browser_tabs", 0);

				if (tabs && tabs->visible)
					list_focus_child(list, tabs);
				else
				{
					list->focused_child = NULL;
					browser_focus(list, NONE);
				}
			}
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

	if (mcc_level_name(map_name))
		return NONE;

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

	if (mcc_level_name(map_name))
	{
		short mcc_index = mcc_maps_find(map_name);

		if (mcc_index != NONE)
			ustrncpy(text, mcc_maps_name(mcc_index), ROW_TEXT_LENGTH - 1);
		else
		{
			for (index = 0; map_name[index] && index < ROW_TEXT_LENGTH - 1; index++)
				text[index] = (wchar_t)(unsigned char)map_name[index];
			text[index] = 0;
		}
		text[ROW_TEXT_LENGTH - 1] = 0;
		return;
	}

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
/* the most a list orders (browser_favorites_order): the server browser's
games and its groups' headings (LOBBY_BROWSER_GAMES and
NUMBER_OF_BROWSER_GROUPS, below), more than the local network's */
#define BROWSER_LISTED_MAXIMUM (256 + 5)
/* which of those listed are favorites found (each group's first) */
static boolean browser_listed_favorite[BROWSER_LISTED_MAXIMUM];
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
favorites. Nothing is sent: it is the lists', and kept from one
run to the next (browser_view_read, below).

The server browser's list is in groups, each under its heading: OpenH1's
dedicated servers ("OpenH1 Dedicated Servers"), the OpenH1 games players
host ("Hosted Servers"), then the Legacy ones ("Legacy (Slow) Servers":
OpenCE's netcode, at 30 ticks), so that the games players are sent to are
OpenH1's first, and the Legacy ones still listed. In each group the
project's own servers come first (on its own master's word:
port/linux/src/master_list.c's master_list_project_server), then the
others, each part sorted alike with its favorites first. A heading is a row
of its own, which the focus passes over (lobby_browser_update), over a
group that has games (the LEGACY tab's, which lists nothing else, has
none). The local network's list has no groups */

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

/* the server browser's groups, in the order they are listed */
enum
{
	_browser_group_dedicated,
	_browser_group_hosted,
	_browser_group_legacy,
	/* (the co-op list's, coop_menu.inc: friends' games, then the public
	ones; no game of the server browser's own is in them) */
	_browser_group_friends,
	_browser_group_public,
	NUMBER_OF_BROWSER_GROUPS
};

/* an order's place for a group's heading (browser_favorites_order): below
0, apart from the games' indices */
#define BROWSER_HEADING(group) ((short)(-1 - (group)))
#define BROWSER_HEADING_GROUP(place) ((short)(-1 - (place)))

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
	/* a server of the project's own (p2p_listing's project) */
	boolean project;
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
	/* ... and its kind (game_modes_kind: its mode, and Hardcore), whether
	it is on teams, and whether it is PRO 2V2's (hosted for one of its
	matchmaking playlists: a listing's playlist) */
	byte kind;
	boolean has_teams;
	boolean pro_2v2;
};

static short browser_sort_column = _browser_sort_players;
static boolean browser_sort_descending = TRUE;
static short browser_tab = _browser_tab_all;
/* the tab the server browser shows this visit, not the player's own kept in
network.browser_tab (a hand-written screen's MULTIPLAYER: all; its CO-OP:
co-op: menu_screen.c), NONE for theirs; their own choice of a tab ends it */
static short browser_tab_visit = NONE;
/* whether the list being ordered is in groups (the server browser's) */
static boolean browser_grouped;
/* the 2V2 browser's (doubles_browser.inc): the server browser kept to the
games of a matchmaking playlist (0 up; NONE: the server browser itself),
every tab's, unfiltered (its searches kept), no favorites offline; and its
panel's title. Its words for a list of none of them */
static short browser_doubles_playlist = NONE;
static wchar_t const *browser_doubles_name = L"";
static void doubles_browser_empty(wchar_t *text, short size);

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
	/* (the order the lists open in, most players first: of games with as
	many players, the OpenH1 games first, the better game and the fewer,
	then the dedicated servers. Only between equal counts, so that a game
	players host is never put under emptier dedicated servers) */
	if (!difference && browser_sort_column == _browser_sort_players && browser_sort_descending)
	{
		if (a->openh1 != b->openh1)
			return a->openh1;
		if (a->dedicated != b->dedicated)
			return a->dedicated;
	}
	return browser_sort_descending ? difference > 0 : difference < 0;
}

/* the game's group (the first, if the list has none) */
static short browser_group_of(struct browser_sort_entry const *entry)
{
	if (!browser_grouped)
		return _browser_group_dedicated;
	if (!entry->openh1)
		return _browser_group_legacy;
	return entry->dedicated ? _browser_group_dedicated : _browser_group_hosted;
}

/* whether a group listed has its heading over it: each has (but the
Legacy ones on the LEGACY tab, which lists nothing else) */
static boolean browser_group_headed(short group)
{
	return group != _browser_group_legacy || browser_tab != _browser_tab_legacy;
}

/* a group's heading's words */
static wchar_t const *browser_group_heading(short group)
{
	switch (group)
	{
	case _browser_group_dedicated:
		return L"GOAT H1 Dedicated Servers";
	case _browser_group_hosted:
		return L"Hosted Servers";
	case _browser_group_friends:
		return L"YOUR FRIENDS' GAMES";
	case _browser_group_public:
		return L"PUBLIC CO-OP GAMES";
	default:
		return L"Legacy (Slow) Servers";
	}
}

/* whether the tab shown lists the game (a favorite or not) */
static boolean browser_tab_shows(struct browser_sort_entry const *entry, boolean favorite)
{
	/* (the 2V2 browser's games are its kind's, whatever the tab) */
	if (browser_doubles_playlist != NONE)
		return TRUE;
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
	if (browser_doubles_playlist != NONE)
		return FALSE;
	return browser_tab == _browser_tab_all || browser_tab == _browser_tab_favorites;
}

/* ---- the browsers' filters (the Filters page, tools/port_settings.py's
BROWSER_FILTERS: the network.browser_* settings, read as a list opens and
kept): full games, empty ones and those with a password listed or not, only
the games whose ping is under a limit (one whose ping is not known is
listed: a local network game's, a master server's dedicated server's, one
being timed), the tick rate (OpenH1's 30, 60 or 120, or Legacy: OpenCE's
netcode, the listing's standard and rate) and the game type (an engine's,
Team Slayer's teams, OpenCE's modes of Slayer, co-op's none, PRO 2V2's
playlists: what the listings carry, engine_type, has_teams, kind and
playlist; one at a time, or any). The searches (browser_searches: the SERVER and MAP columns', and
the touchscreen's one field for both) hide the games whose name or map has
not their words. The server browser's and
the local network's lists are filtered, not Join by Link's, whose games are
the invites taken. Nothing is sent: the lists' own */

#define BROWSER_SEARCH_LENGTH 31

/* the TICK RATE filter's choices (network.browser_tick's words, below) */
enum
{
	_browser_tick_any,
	_browser_tick_30,
	_browser_tick_60,
	_browser_tick_120,
	_browser_tick_legacy,
	NUMBER_OF_BROWSER_TICKS
};

/* the GAME TYPE filter's (network.browser_game_type's) */
enum
{
	_browser_type_any,
	_browser_type_slayer,
	_browser_type_team_slayer,
	_browser_type_ctf,
	_browser_type_oddball,
	_browser_type_king,
	_browser_type_race,
	_browser_type_swat,
	_browser_type_infection,
	_browser_type_gun_game,
	_browser_type_coop,
	_browser_type_pro_2v2,
	NUMBER_OF_BROWSER_TYPES
};

/* the settings' words for them, in their order (tools/port_settings.py's
BROWSER_TICKS and BROWSER_GAME_TYPES) */
static char const *const browser_tick_words[NUMBER_OF_BROWSER_TICKS] =
{
	"any", "30", "60", "120", "legacy",
};
static char const *const browser_type_words[NUMBER_OF_BROWSER_TYPES] =
{
	"any", "slayer", "team_slayer", "ctf", "oddball", "king", "race", "swat", "infection", "gun_game", "coop",
	"pro_2v2",
};

struct browser_filters
{
	boolean show_full, show_empty, show_password;
	/* milliseconds; 0: any */
	short maximum_ping;
	/* _browser_tick_* and _browser_type_* */
	short tick, game_type;
};

static struct browser_filters browser_filters = { TRUE, TRUE, TRUE, 0, _browser_tick_any, _browser_type_any };
/* whether the list being ordered is filtered, how many of the games its
tab lists the filters and the searches hid, and how many of those the
searches alone hid (the Filters' listed) */
static boolean browser_filtering;
static short browser_filtered_count, browser_searched_count;

/* the searches of the SERVER and MAP columns (their titles' magnifying
glass; R3, / on the keyboard), and the touchscreen's, which has no column
titles: one field at the top of its list for both (its words in the
server's name or in its map, as a phone's search finds them): the words
typed into each, kept while the browser is (Filters opened and closed too)
and forgotten as it is left; not settings. A game is listed if its name has
the server's words, its map (its name as shown, Blood Gulch, or its own,
bloodgulch) the map's, and one or the other the touchscreen's, as well as
the Filters listing it: each matched in any case, without spaces or
punctuation ("blood gulch" finds bloodgulch, "bg-snipers" BG Snipers) */
enum
{
	_browser_search_server,
	_browser_search_map,
	_browser_search_any,
	NUMBER_OF_BROWSER_SEARCHES
};

static char browser_searches[NUMBER_OF_BROWSER_SEARCHES][BROWSER_SEARCH_LENGTH + 1];

/* the letters and digits of text, those of ASCII in capitals, the others as
they are (spaces and punctuation left out): what a search compares; into
key (size characters), returning its length */
static short browser_search_key(wchar_t const *text, wchar_t *key, short size)
{
	short length = 0;

	for (; *text && length < size - 1; text++)
	{
		wchar_t character = *text >= 'a' && *text <= 'z' ? (wchar_t)(*text - 32) : *text;

		if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character > 0x7F)
			key[length++] = character;
	}
	key[length] = 0;
	return length;
}

/* whether text has the words, as a search compares them (words with no
letter or digit: every text) */
static boolean browser_text_has(wchar_t const *text, char const *words)
{
	wchar_t wide_words[BROWSER_SEARCH_LENGTH + 1], key[ROW_TEXT_LENGTH], words_key[BROWSER_SEARCH_LENGTH + 1];
	short index, start, length, words_length;

	for (index = 0; words[index] && index < BROWSER_SEARCH_LENGTH; index++)
		wide_words[index] = (wchar_t)(unsigned char)words[index];
	wide_words[index] = 0;
	words_length = browser_search_key(wide_words, words_key, NUMBEROF(words_key));
	length = browser_search_key(text, key, NUMBEROF(key));
	for (start = 0; start + words_length <= length; start++)
	{
		for (index = 0; index < words_length && key[start + index] == words_key[index]; index++)
		{
		}
		if (index == words_length)
			return TRUE;
	}
	return FALSE;
}

/* ... text of ASCII's */
static boolean browser_ascii_has(char const *text, char const *words)
{
	wchar_t wide[ROW_TEXT_LENGTH];
	short index;

	for (index = 0; text[index] && index < ROW_TEXT_LENGTH - 1; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
	return browser_text_has(wide, words);
}

/* whether the searches list a game of the name and map (its own name and
its name as shown) */
static boolean browser_search_shows(char const searches[NUMBER_OF_BROWSER_SEARCHES][BROWSER_SEARCH_LENGTH + 1],
	char const *name, char const *map, wchar_t const *map_shown)
{
	char const *map_words = searches[_browser_search_map];
	char const *any_words = searches[_browser_search_any];

	if (!browser_ascii_has(name, searches[_browser_search_server]))
		return FALSE;
	if (!browser_ascii_has(map, map_words) && !browser_text_has(map_shown, map_words))
		return FALSE;
	return browser_ascii_has(name, any_words) || browser_ascii_has(map, any_words) ||
		browser_text_has(map_shown, any_words);
}

/* whether a search reads the maps' names as shown (the map's, the
touchscreen's): the lists find them only then */
static boolean browser_search_reads_map_shown(void)
{
	return browser_searches[_browser_search_map][0] || browser_searches[_browser_search_any][0];
}

/* whether any search has words */
static boolean browser_searching(void)
{
	short search;

	for (search = 0; search < NUMBER_OF_BROWSER_SEARCHES; search++)
	{
		if (browser_searches[search][0])
			return TRUE;
	}
	return FALSE;
}

/* whether the TICK RATE filter lists the game: an OpenH1 game's rate (in
Hz: OpenH1 30 Tick is OpenH1's netcode at the Xbox's rate), or Legacy,
OpenCE's netcode (at 30 ticks, apart from OpenH1's 30) */
static boolean browser_tick_shows(short tick, struct browser_sort_entry const *entry)
{
	switch (tick)
	{
	case _browser_tick_30:
		return entry->openh1 && entry->rate == 30;
	case _browser_tick_60:
		return entry->openh1 && entry->rate == 60;
	case _browser_tick_120:
		return entry->openh1 && entry->rate == 120;
	case _browser_tick_legacy:
		return !entry->openh1;
	default:
		return TRUE;
	}
}

/* whether the GAME TYPE filter lists the game: by its game engine (1 CTF,
2 Slayer, 3 Oddball, 4 King of the Hill, 5 Race; 0 co-op's none), Slayer
free for all or on teams, OpenCE's modes of Slayer (SWAT, Infection, Gun
Game: their own, not Slayer's; Hardcore is rules over any, not a type),
PRO 2V2's playlists (Team Slayer's too) */
static boolean browser_type_shows(short game_type, struct browser_sort_entry const *entry)
{
	short mode = (short)(entry->kind & ~GAME_MODES_KIND_HARDCORE);
	boolean slayer = entry->engine_type == 2 && mode == _game_mode_none;

	switch (game_type)
	{
	case _browser_type_slayer:
		return slayer && !entry->has_teams;
	case _browser_type_team_slayer:
		return slayer && entry->has_teams;
	case _browser_type_ctf:
		return entry->engine_type == 1;
	case _browser_type_oddball:
		return entry->engine_type == 3;
	case _browser_type_king:
		return entry->engine_type == 4;
	case _browser_type_race:
		return entry->engine_type == 5;
	case _browser_type_swat:
		return entry->engine_type == 2 && mode == _game_mode_swat;
	case _browser_type_infection:
		return entry->engine_type == 2 && mode == _game_mode_infection;
	case _browser_type_gun_game:
		return entry->engine_type == 2 && mode == _game_mode_gun_game;
	case _browser_type_coop:
		return entry->coop;
	case _browser_type_pro_2v2:
		return entry->pro_2v2;
	default:
		return TRUE;
	}
}

/* a word's place among words (count of them), the first's if it is none
(a word the file has that this build does not know: any) */
static short browser_word_index(char const *word, char const *const *words, short count)
{
	short index;

	for (index = 0; word && index < count; index++)
	{
		if (!strcmp(word, words[index]))
			return index;
	}
	return 0;
}

/* whether the filters list the game */
static boolean browser_filters_show(struct browser_filters const *filters, struct browser_sort_entry const *entry)
{
	if (!filters->show_full && entry->maximum_players > 0 && entry->players >= entry->maximum_players)
		return FALSE;
	if (!filters->show_empty && entry->players <= 0)
		return FALSE;
	if (!filters->show_password && entry->locked)
		return FALSE;
	/* (a ping not known, NONE, under any limit) */
	if (filters->maximum_ping > 0 && entry->ping >= filters->maximum_ping)
		return FALSE;
	if (!browser_tick_shows(filters->tick, entry) || !browser_type_shows(filters->game_type, entry))
		return FALSE;
	return TRUE;
}

/* the filters as the settings have them */
static void browser_filters_read(struct browser_filters *filters)
{
	filters->show_full = config_boolean("network.browser_show_full") != 0;
	filters->show_empty = config_boolean("network.browser_show_empty") != 0;
	filters->show_password = config_boolean("network.browser_show_password") != 0;
	filters->maximum_ping = (short)PIN(config_integer("network.browser_ping"), 0, 9999);
	filters->tick = browser_word_index(config_string("network.browser_tick"), browser_tick_words,
		NUMBER_OF_BROWSER_TICKS);
	filters->game_type = browser_word_index(config_string("network.browser_game_type"), browser_type_words,
		NUMBER_OF_BROWSER_TYPES);
}

/* ---- the browsers' tab and order, kept from one run to the next
(network.browser_tab, network.browser_sort and
network.browser_sort_descending, as game.campaign_difficulty keeps the
difficulty chosen last): read as a list opens, written as they change */

/* the settings' words for the tabs and the columns, in their orders */
static char const *const browser_tab_words[NUMBER_OF_BROWSER_TABS] =
{
	"all", "dedicated", "openh1", "legacy", "coop", "favorites",
};
static char const *const browser_sort_words[NUMBER_OF_BROWSER_SORTS] =
{
	"players", "ping", "server", "map", "mode",
};

static void browser_view_read(void)
{
	browser_tab = browser_word_index(config_string("network.browser_tab"), browser_tab_words,
		NUMBER_OF_BROWSER_TABS);
	if (browser_tab_visit != NONE)
		browser_tab = browser_tab_visit;
	/* (the new engine's CO-OP tab opens co-op's own screen, its list not
	the browser's: the browser opens on ALL in its place) */
	if (browser_tab == _browser_tab_coop && menu_engine_looks())
		browser_tab = _browser_tab_all;
	browser_sort_column = browser_word_index(config_string("network.browser_sort"), browser_sort_words,
		NUMBER_OF_BROWSER_SORTS);
	browser_sort_descending = config_boolean("network.browser_sort_descending") != 0;
}

/* (only what changed: config_write writes config.toml) */
static void browser_view_write(void)
{
	if (browser_tab_visit == NONE && strcmp(config_string("network.browser_tab"), browser_tab_words[browser_tab]))
		config_write("network.browser_tab", browser_tab_words[browser_tab]);
	if (strcmp(config_string("network.browser_sort"), browser_sort_words[browser_sort_column]))
		config_write("network.browser_sort", browser_sort_words[browser_sort_column]);
	if ((config_boolean("network.browser_sort_descending") != 0) != (browser_sort_descending != FALSE))
		config_write("network.browser_sort_descending", browser_sort_descending ? "true" : "false");
}

/* the server browser's tab for the next visit (a hand-written screen's
action: "all" or "coop"; NULL, the player's own), and what this visit of the
browser is: 0 not the server browser (the local network's list, Join by
Link), 1 the server browser, 2 the server browser opened on co-op
(menu_screen.c's HOST button) */
void pc_menu_browser_visit(char const *tab)
{
	short index;

	browser_tab_visit = NONE;
	for (index = 0; tab && index < NUMBER_OF_BROWSER_TABS; index++)
	{
		if (!strcmp(tab, browser_tab_words[index]))
			browser_tab_visit = index;
	}
}

int pc_menu_browser_visit_kind(void)
{
	if (multiplayer.mode != _multiplayer_mode_server_browser)
		return 0;
	return browser_tab_visit == _browser_tab_coop ? 2 : 1;
}

static void browser_filters_refresh(void)
{
	browser_filters_read(&browser_filters);
	browser_view_read();
}

/* the games of a group the tab shows, the project's own or the others
(project; NONE: either), the favorites or the others, into order from
written, sorted by the column chosen (alike ones kept in the order found);
returns where they end */
static short browser_order_part(short const *favorite_of, struct browser_sort_entry const *entries, short count,
	short group, short project, boolean favorites, short *order, short written)
{
	short start = written;
	short index;

	for (index = 0; index < count; index++)
	{
		if ((favorite_of[index] != NONE) != favorites || !browser_tab_shows(&entries[index], favorites) ||
			browser_group_of(&entries[index]) != group || (project != NONE && entries[index].project != project))
		{
			continue;
		}
		/* (those the filters hide counted, for the words over the list) */
		if (browser_filtering && browser_doubles_playlist == NONE &&
			!browser_filters_show(&browser_filters, &entries[index]))
			browser_filtered_count++;
		else if (browser_filtering && !browser_search_shows(browser_searches, entries[index].name,
			entries[index].map, entries[index].map_shown))
		{
			browser_filtered_count++;
			browser_searched_count++;
		}
		else
			order[written++] = index;
	}
	for (index = (short)(start + 1); index < written; index++)
	{
		short moving = order[index];
		short place = index;

		while (place > start && browser_sort_before(&entries[moving], &entries[order[place - 1]]))
		{
			order[place] = order[place - 1];
			place--;
		}
		order[place] = moving;
	}
	for (index = start; index < written; index++)
		browser_listed_favorite[index] = favorites;
	return written;
}

/* the order of the games found, those the tab shows: in each group (the
server browser's: browser_grouped; else one) the project's own servers
first (the server browser's), the favorites first among them and among the
others, each part sorted by the column chosen, and a group's heading
(BROWSER_HEADING) before it if it has one; into `order` (count and
NUMBER_OF_BROWSER_GROUPS long), returning how many */
static short browser_favorites_order(short const *favorite_of, struct browser_sort_entry const *entries,
	short count, short *order)
{
	short group, index;
	short written = 0;

	browser_filtered_count = 0;
	browser_searched_count = 0;
	for (group = 0; group < (browser_grouped ? NUMBER_OF_BROWSER_GROUPS : 1); group++)
	{
		short start = written;

		short project;

		for (project = browser_grouped ? TRUE : NONE; project >= (browser_grouped ? FALSE : NONE); project--)
		{
			written = browser_order_part(favorite_of, entries, count, group, project, TRUE, order, written);
			written = browser_order_part(favorite_of, entries, count, group, project, FALSE, order, written);
		}
		/* (its heading before it, if it has games and one) */
		if (written > start && browser_grouped && browser_group_headed(group))
		{
			for (index = written; index > start; index--)
			{
				order[index] = order[index - 1];
				browser_listed_favorite[index] = browser_listed_favorite[index - 1];
			}
			order[start] = BROWSER_HEADING(group);
			browser_listed_favorite[start] = FALSE;
			written++;
		}
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
	browser_view_write();
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
		browser_tab_visit = NONE;
		browser_view_write();
		browser_list_restart();
		ui_play_audio_feedback_sound(SOUND_CURSOR);
	}
	return TRUE;
}

/* the words in the list's place while it has no rows: what it shows, why
there are none and what to do (the mode's, multiplayer.mode; the tab's;
the server browser's: internet play off, or its list, still being looked
for, newer games it cannot join; the games its filters and searches hid,
and of those its searches alone), into text */
static void browser_empty_text(short mode, short tab, boolean online, boolean public_lobby, boolean looking,
	int newer, short filtered, short searched, wchar_t *text, short size)
{
	static wchar_t const *const kinds[NUMBER_OF_BROWSER_TABS] =
	{
		L"", L"dedicated server ", L"GOAT H1 ", L"Legacy ", L"co-op ", L"",
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
	else if (searched > 0)
	{
		usnprintf(text, size - 1, L"No servers match your search.");
	}
	else if (filtered > 0)
	{
		usnprintf(text, size - 1, L"%d %s hidden by your filters.\r\nChoose FILTERS to change them.", filtered,
			filtered == 1 ? L"game is" : L"games are");
	}
	else if (tab == _browser_tab_coop && server_browser)
		usnprintf(text, size - 1, L"No co-op games right now \x00B7 Host one");
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
		usnprintf(text, size - 1, L"%d %s on a newer version.\r\nUpdate GOAT H1 to join %s.", newer,
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
	/* (a host being timed, or timed again after REFRESH, shows its timing,
	not the ping it had: p2p_lobby.c's probing and refreshing) */
	row->ping_state = game->failed ? _browser_ping_failed :
		(game->probing || game->refreshing) && !game->locked && !game->master ? _browser_ping_measuring :
		game->ping >= 0 ? _browser_ping_measured :
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

	browser_row_heading[row_index] = FALSE;
	visible_set(named(row_widget, "server_item_heading", 0), FALSE);
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

/* a group's heading's row (the server browser's: lobby_browser_favorites):
its words across the row (server_item_heading), nothing else */
static void browser_heading_show(struct widget_instance *row_widget, short row_index, wchar_t const *words)
{
	static char const *const emptied[] =
	{
		"server_item_server_name", "server_item_map", "server_item_type", "server_item_players", "server_item_ping_ms",
	};
	static char const *const hidden[] =
	{
		"server_item_tick", "server_item_locked", "server_item_ping", "server_item_favorite",
	};
	short index;

	browser_row_heading[row_index] = TRUE;
	browser_row_favorite[row_index] = browser_row_offline[row_index] = browser_row_closed[row_index] = FALSE;
	browser_row_pings[row_index] = -1;
	browser_row_measuring[row_index] = FALSE;
	row_widget->animation.current_frame_index = 0;
	visible_set(named(row_widget, "server_item_heading", 0), TRUE);
	menu_text_box_fit(named(row_widget, "server_item_heading", 0), words, 0);
	for (index = 0; index < NUMBEROF(emptied); index++)
		menu_text_box_fit(named(row_widget, emptied[index], 0), L"", 0);
	for (index = 0; index < NUMBEROF(hidden); index++)
		visible_set(named(row_widget, hidden[index], 0), FALSE);
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
		text_set(pill, row->openh1 ? L"GOAT H1" : L"LEGACY");
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
			usnprintf(playlist, NUMBEROF(playlist) - 1, L"Playlist: %s. ", name);
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
	menu_prompts_show(bar, "prompt_search", multiplayer.mode != _multiplayer_mode_direct_link);
}

/* ---- the browsers' searches (browser_searches): each searched column's
title has a magnifying glass after it (op_search_<column>), which A, Start
or a click turns into a field typed into in the title's place
(op_search_field_<column>, its caret blinking), the list filtered as each
letter is typed; Enter, A on the field, or the focus taken off it by the
D-pad or a click keeps the words, which the field then shows, with its X
(op_search_clear_<column>) to clear them. Esc (B, Back) while it is typed
into clears them and leaves it, the magnifying glass shown again; the other
column's search and the Filters stay. R3 (/ on the keyboard: the footer's
SEARCH) types into the server's search, again the map's, again the
server's; a controller's field brings up Steam's keyboard where there is
one (platform_text_field). A list the searches leave without a game says
so, with CLEAR SEARCH under it (browser_clear_show). Join by Link's list,
its invites, is not searched.

The touchscreen's list has no column titles: its search is one field at
the top (port_settings.py's _touch_browser), by the same parts' names (its
"any": op_search_any, a bar saying what it finds, which a tap turns into
op_search_field_any, with its X), the same functions and the same words
kept. A tap begins it, and brings up the system's keyboard (Android's,
iOS's: platform_text_field); a tap on it while it is typed into brings the
keyboard back (closed by its own key) rather than leaving it; the
keyboard's Enter, or a tap elsewhere, keeps the words */

/* the searches' columns' names (port_settings.py's BROWSER_SEARCHED, and
the touchscreen's TOUCH_SEARCHED), and their titles' */
static char const *const browser_search_columns[NUMBER_OF_BROWSER_SEARCHES] = { "server_name", "map", "any" };

/* the search being typed into, its field (NONE, NULL: none) */
static short browser_search_active = NONE;
static struct widget_instance *browser_search_field;

static boolean browser_search_editing(short *search)
{
	if (browser_search_active == NONE || !browser_search_field || !text_field_editing(browser_search_field))
		return FALSE;
	if (search)
		*search = browser_search_active;
	return TRUE;
}

/* (the words typed: the search's) */
static void browser_search_done(char const *text)
{
	if (browser_search_active != NONE)
		snprintf(browser_searches[browser_search_active], sizeof(browser_searches[0]), "%s", text);
	browser_search_active = NONE;
	browser_search_field = NULL;
}

/* the search typed into left, its words kept */
static void browser_search_end(void)
{
	if (browser_search_editing(NULL))
		text_field_end(TRUE);
	browser_search_active = NONE;
	browser_search_field = NULL;
}

/* a list's focus on its child */
static void list_focus_child(struct widget_instance *list, struct widget_instance *focused)
{
	struct widget_instance *child;
	short index = 0;

	for (child = list ? list->child : NULL; child; child = child->next, index++)
	{
		if (child == focused)
		{
			list->focused_child = child;
			list->parameters.list.selected_index = index;
			return;
		}
	}
}

/* the search's part (op_search, op_search_field, op_search_clear) in the
column titles, of the screen of widget */
static struct widget_instance *browser_search_part(struct widget_instance *widget, char const *part, short search)
{
	struct widget_instance *header = named(screen_of(widget), "join_game_header", 0);
	char name[48];

	snprintf(name, sizeof(name), "%s_%s", part, browser_search_columns[search]);
	return header ? named(header, name, 0) : NULL;
}

/* which search a part is of (its name's column) */
static short browser_search_of(struct widget_instance const *widget)
{
	short search;

	for (search = NUMBER_OF_BROWSER_SEARCHES - 1; search >= 0; search--)
	{
		char const *column = browser_search_columns[search];
		size_t length = strlen(column), name_length = widget->name ? strlen(widget->name) : 0;

		if (name_length >= length && !strcmp(widget->name + name_length - length, column))
			return search;
	}
	return NONE;
}

/* the search typed into, its field shown with the focus (the list's on the
titles, theirs on it) */
static boolean browser_search_begin(struct widget_instance *widget, short search)
{
	struct widget_instance *field = browser_search_part(widget, "op_search_field", search);
	struct widget_instance *header = field ? field->parent : NULL;

	if (!field || multiplayer.mode == _multiplayer_mode_direct_link)
		return FALSE;
	browser_search_end();
	if (text_field_editing(NULL))
		text_field_end(FALSE);
	field->visible = TRUE;
	list_focus_child(header, field);
	list_focus_child(header ? header->parent : NULL, header);
	browser_search_active = search;
	browser_search_field = field;
	text_field_begin(field, browser_searches[search], BROWSER_SEARCH_LENGTH, browser_search_done);
	text_field.enter_ends = TRUE;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* "port browser search" (a magnifying glass's A, Start or click, or its
field's: typed into, or if it is, left with its words; the touchscreen's
field typed into again, its keyboard brought back) */
static boolean browser_search_edit(struct widget_instance *widget)
{
	short search = browser_search_of(widget), editing;

	if (search == NONE)
		return FALSE;
	if (browser_search_editing(&editing) && editing == search && !ui_widget_port_touch_menus())
	{
		browser_search_end();
		return TRUE;
	}
	return browser_search_begin(widget, search);
}

/* "port browser search next" (R3, /, the footer's SEARCH): the next of the
searches the layout has typed into, from the first (the computer's: the
server's, then the map's; the touchscreen's: its one) */
static boolean browser_search_next(struct widget_instance *widget)
{
	short editing = NONE, step;

	if (!browser_search_editing(&editing))
		editing = NONE;
	for (step = 1; step <= NUMBER_OF_BROWSER_SEARCHES; step++)
	{
		short search = (short)((editing + step) % NUMBER_OF_BROWSER_SEARCHES);

		if (editing == NONE)
			search = (short)(step - 1);
		if (browser_search_part(widget, "op_search_field", search))
			return browser_search_begin(widget, search);
	}
	return FALSE;
}

/* a search's words cleared (left if it is typed into), its magnifying
glass with the focus again if its field had it */
static void browser_search_clear(struct widget_instance *widget, short search)
{
	short editing;
	struct widget_instance *field = browser_search_part(widget, "op_search_field", search);
	struct widget_instance *icon = browser_search_part(widget, "op_search", search);

	if (browser_search_editing(&editing) && editing == search)
	{
		text_field_end(FALSE);
		browser_search_active = NONE;
		browser_search_field = NULL;
	}
	browser_searches[search][0] = 0;
	if (icon && field && field->parent && (field->parent->focused_child == field ||
		field->parent->focused_child == browser_search_part(widget, "op_search_clear", search)))
	{
		icon->visible = TRUE;
		list_focus_child(field->parent, icon);
	}
}

/* "port browser search clear": a search's X (its words cleared), or the
list's CLEAR SEARCH (every search's, the focus on the first game) */
static boolean browser_search_clear_chosen(struct widget_instance *widget)
{
	short search = browser_search_of(widget);

	if (search != NONE)
		browser_search_clear(widget, search);
	else
	{
		for (search = 0; search < NUMBER_OF_BROWSER_SEARCHES; search++)
			browser_search_clear(widget, search);
	}
	ui_play_audio_feedback_sound(SOUND_BACK);
	return TRUE;
}

/* every search forgotten (the browser left) */
static void browser_searches_forget(void)
{
	if (browser_search_editing(NULL))
		text_field_end(FALSE);
	browser_search_active = NONE;
	browser_search_field = NULL;
	csmemset(browser_searches, 0, sizeof(browser_searches));
}

/* (each frame, before the list is ordered) the searched columns' titles:
each its magnifying glass, or its field and X while it has words or is
typed into (the words typed so far its search's, the list filtered by them
at once; the field left, its words kept, once the D-pad or a click, not the
pointer passing, took the focus off it); Join by Link's none */
static void browser_searches_show(struct widget_instance *list)
{
	struct widget_instance *header = named(list, "join_game_header", 0);
	boolean link = multiplayer.mode == _multiplayer_mode_direct_link;
	short search, editing = NONE;

	if (!header)
		return;
	if (browser_search_editing(&editing) && !ui_widget_port_hover_focused() &&
		(list->focused_child != header || header->focused_child != browser_search_field))
	{
		browser_search_end();
		editing = NONE;
	}
	else if (!browser_search_editing(NULL))
		editing = NONE;
	for (search = 0; search < NUMBER_OF_BROWSER_SEARCHES; search++)
	{
		char title[48];
		struct widget_instance *field = browser_search_part(list, "op_search_field", search);
		struct widget_instance *icon = browser_search_part(list, "op_search", search);
		boolean shown = !link && (editing == search || browser_searches[search][0]);

		snprintf(title, sizeof(title), "op_sort_header_%s", browser_search_columns[search]);
		visible_set(named(header, title, 0), !shown);
		visible_set(icon, !link && !shown);
		/* (the touchscreen's bar, whose picture has three frames, which the
		engine leaves to the game: lit while the list's focus is on it, not
		merely its titles' own focus, which is on it from the first) */
		if (icon && icon->animation.number_of_sprite_frames > 2)
			icon->animation.current_frame_index = (short)(list->focused_child == header && header->focused_child == icon);
		visible_set(field, shown);
		visible_set(browser_search_part(list, "op_search_clear", search), shown);
		if (!field || !shown)
			continue;
		text_field_show(field, browser_searches[search], editing == search);
		/* (the words typed so far, the search's: the list filtered by them
		as each letter is typed; Enter may have ended the field) */
		if (editing == search)
			snprintf(browser_searches[search], sizeof(browser_searches[0]), "%s", text_field.text);
		/* (words longer than the field: their end shown, by the caret) */
		{
			wchar_t *shown_text = field->parameters.text_box.text;
			short width;
			long font = ui_widget_port_text_box_font(field, &width);

			while (font != NONE && shown_text && shown_text[0] && shown_text[1] &&
				settings_text_width(font, shown_text) > width - 8)
			{
				memmove(shown_text, shown_text + 1, ustrlen(shown_text) * sizeof(wchar_t));
			}
			field->animation.current_frame_index = (short)(editing == search);
		}
	}
	focus_off_hidden(header);
}

/* the list's CLEAR SEARCH: shown while the searches hide every game it
would list (the focus on it as it comes, as there is no row, but for the
titles' while a search is typed into) */
static void browser_clear_show(struct widget_instance *list, short rows)
{
	struct widget_instance *bar = named(list, "browser_clear_bar", 0);
	boolean shown = rows == 0 && browser_searched_count > 0 && multiplayer.mode != _multiplayer_mode_direct_link;

	if (!bar)
		return;
	if (shown && !bar->visible && list->focused_child && strcmp(list->focused_child->name, "join_game_header"))
		list_focus_child(list, bar);
	bar->visible = shown;
	bar->disabled = !shown;
	if (!shown && list->focused_child == bar)
		browser_focus(list, NONE);
	/* (its highlight lit only while the list's focus is on it) */
	if (bar->child)
		bar->child->animation.current_frame_index = (short)(list->focused_child == bar);
}

/* the favorites offline the searches hide, left out (and counted) */
static void browser_offline_search(void)
{
	short index, kept = 0;

	for (index = 0; index < browser_offline_count; index++)
	{
		struct browser_favorite const *favorite = browser_favorites_get(browser_offline[index]);
		wchar_t shown[ROW_TEXT_LENGTH];

		shown[0] = 0;
		if (browser_search_reads_map_shown())
			map_display_name(favorite->map, shown);
		if (browser_search_shows(browser_searches, favorite->name, favorite->map, shown))
			browser_offline[kept++] = browser_offline[index];
		else
		{
			browser_filtered_count++;
			browser_searched_count++;
		}
	}
	browser_offline_count = kept;
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
	/* (and, the filters or the searches hiding some, of how many: "2 OF
	18 GAMES", which fits where "2 GAMES 16 HIDDEN" ran past the counts'
	box) */
	if (browser_filtering && browser_filtered_count > 0)
	{
		usnprintf(text, NUMBEROF(text) - 1, L"%d OF %d GAMES    %ld %s", games, games + browser_filtered_count,
			players, players == 1 ? L"PLAYER" : L"PLAYERS");
	}
	else
	{
		usnprintf(text, NUMBEROF(text) - 1, L"%d %s    %ld %s", games, games == 1 ? L"GAME" : L"GAMES", players,
			players == 1 ? L"PLAYER" : L"PLAYERS");
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

typedef char lobby_browser_listed_assert[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS <= BROWSER_LISTED_MAXIMUM ?
	1 : -1];

static struct
{
	/* the games found, as listed: the groups' headings among them, each
	a game of nothing (headed: its group + 1; 0, a game's) */
	struct p2p_listing games[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
	unsigned char headed[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
	short headings;
	/* the games listed (the headings too), the first on the rows, the one
	last focused (Join's) */
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

/* the co-op list (screens/coop_join.xml, coop_menu.inc): the server
browser's model with its games the co-op ones alone, the player's friends'
first (their presence's, a friend's listing taken into its row), and why
none are in plain words. On while its screen is open */
static boolean coop_join_listing(void);
static short coop_join_order(short count);
static boolean coop_join_party_row(short index);
static void coop_join_row_fix(short index, struct browser_row *row);
static void coop_join_status(short index, wchar_t *text, short size);
static void coop_join_empty(wchar_t *text, short size);
static boolean coop_join_friend(short index);
static boolean coop_join_value(char const *name, struct browser_row const *row, wchar_t *text, short size);

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
	lobby_browser.headings = 0;
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

/* whether a listing's playlist (1 up; 0: none) is one of PRO 2V2's */
static boolean browser_playlist_pro_2v2(short playlist)
{
	short kind;

	for (kind = 0; playlist > 0 && kind < NUMBER_OF_MATCHMAKING_DOUBLES_KINDS; kind++)
	{
		if (playlist - 1 == matchmaking_doubles_playlist(kind))
			return TRUE;
	}
	return FALSE;
}

/* the games found in the favorites' order (browser_favorites_order), in the
groups with their headings, the favorites offline after them; returns how
many found are listed, the headings too */
static short lobby_browser_favorites(short count)
{
	static struct browser_favorite favorites[LOBBY_BROWSER_GAMES];
	static struct p2p_listing ordered[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
	short favorite_of[LOBBY_BROWSER_GAMES], order[LOBBY_BROWSER_GAMES + NUMBER_OF_BROWSER_GROUPS];
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
		/* (on the project's master's word: master_list.c) */
		entries[index].project = game->project != 0;
		entries[index].coop = game->engine_type == 0;
		/* (on a master server's word alone: p2p_lobby.c) */
		entries[index].dedicated = game->dedicated != 0;
		settings_wide_copy(entries[index].mode, game_kind_name(game->engine_type, game->kind, FALSE),
			BROWSER_MODE_LENGTH);
		entries[index].maximum_players = game->maximum_player_count;
		entries[index].engine_type = (short)game->engine_type;
		entries[index].locked = game->locked != 0;
		entries[index].kind = game->kind;
		entries[index].has_teams = game->has_teams != 0;
		entries[index].pro_2v2 = browser_playlist_pro_2v2(game->playlist);
		/* (the map's name as shown, for the words searched for alone) */
		entries[index].map_shown[0] = 0;
		if (browser_search_reads_map_shown())
			map_display_name(game->map, entries[index].map_shown);
	}
	browser_favorites_match(favorites, count, favorite_of, "net:");
	browser_filtering = TRUE;
	browser_grouped = TRUE;
	listed = browser_favorites_order(favorite_of, entries, count, order);
	browser_grouped = FALSE;
	browser_offline_search();
	lobby_browser.headings = 0;
	for (index = 0; index < listed; index++)
	{
		lobby_browser.headed[index] = 0;
		if (order[index] < 0)
		{
			csmemset(&ordered[index], 0, sizeof(ordered[index]));
			ordered[index].ping = NONE;
			lobby_browser.headed[index] = (unsigned char)(BROWSER_HEADING_GROUP(order[index]) + 1);
			lobby_browser.headings++;
		}
		else
			ordered[index] = lobby_browser.games[order[index]];
	}
	csmemcpy(lobby_browser.games, ordered, listed * sizeof(ordered[0]));
	return listed;
}

/* whether the list's index is a group's heading (not a game) */
static boolean lobby_browser_heading(short index)
{
	return index >= 0 && index < lobby_browser.count && lobby_browser.headed[index] != 0;
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

/* what the browsers show besides their rows, as their model works it out each
frame (lobby_browser_model, local_browser_model) for their view: the
generated screen's list (lobby_browser_update, browser_update) or a
hand-written screen's table (pc_menu_browser_source): the panel's game (if
one is chosen), its title and status line, the words for no rows, and the
counts */
static struct
{
	boolean row_shown;
	struct browser_row row;
	wchar_t title[32];
	wchar_t status[SETTINGS_PANEL_TEXT_LENGTH];
	wchar_t empty[SETTINGS_PANEL_TEXT_LENGTH];
	short games;
	long players;
	short rows;
} browser_display;

/* a list's view (menu_table.h's) of the generated screen's list: its rows,
the row focused, whether its titles are shown */
static void browser_view_of(struct widget_instance *list, struct menu_table_view *view)
{
	struct widget_instance *titles = named(list, "join_game_header", 0);

	csmemset(view, 0, sizeof(*view));
	view->screen = list;
	view->shown = browser_rows_of(list);
	view->focused = browser_row_index(list->focused_child);
	view->hover = ui_widget_port_hover_focused();
	view->titles = titles && titles->visible;
	view->focus = NONE;
	view->chosen = NONE;
}

static void lobby_browser_focus_row(struct widget_instance *list, short row);

/* ... its answer: the row the model focused, or the titles */
static void browser_view_apply(struct widget_instance *list, struct menu_table_view const *view)
{
	if (view->focus_titles)
		list_focus_child(list, named(list, "join_game_header", 0));
	else if (view->focus != NONE)
	{
		char name[16];
		struct widget_instance *row;

		sprintf(name, "server_item_%d", view->focus + 1);
		row = named(list, name, 0);
		if (row)
			row->visible = TRUE;
		lobby_browser_focus_row(list, view->focus);
	}
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

/* the focus off a group's heading (a row of no game), as the controller or
keyboard moved it there: on the game after it, or the game before it if
the focus came up from below (over a heading at the top, the column
titles); the row then focused (NONE: the titles). A row the pointer is on
keeps it: the heading is not chosen (lobby_browser_update) */
static short lobby_browser_off_heading(struct menu_table_view *view, short focused, short shown)
{
	short index = (short)(lobby_browser.first + focused);

	if (focused == NONE || view->hover || !lobby_browser_heading(index))
		return focused;
	if (lobby_browser.chosen > index && index == 0 && view->titles)
	{
		view->focus_titles = TRUE;
		view->focus = NONE;
		return NONE;
	}
	/* (a heading is never the last: its group has a game) */
	index = (short)(lobby_browser.chosen > index && index > 0 ? index - 1 : index + 1);
	lobby_browser.first = (short)PIN(lobby_browser.first, index - shown + 1, index);
	focused = (short)(index - lobby_browser.first);
	view->focus = focused;
	view->focus_titles = FALSE;
	return focused;
}

/* the server browser's model, each frame: its games found and ordered, the
rows' first and the game chosen (scrolled on at the ends; the groups'
headings passed over), a join under way, and what the panel and the words
over the list show (browser_display); for its view's look this frame, which
it answers (the row it focused) */
static void lobby_browser_model(struct menu_table_view *view)
{
	short focused = view->focused;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	unsigned long now = system_milliseconds();
	short chosen, rows, shown = view->shown;

	browser_shown_rows = shown ? shown : BROWSER_ROWS;
	/* (this machine's party, as the rows count it) */
	browser_party_size = browser_party_members();
	lobby_browser.count = lobby_browser_valid_games(lobby_browser.games,
		(short)p2p_lobby_games(lobby_browser.games, LOBBY_BROWSER_GAMES));
	/* (the 2V2 browser's: its kind's games alone, as matchmaking's search
	takes them) */
	if (browser_doubles_playlist != NONE)
	{
		lobby_browser.count = matchmaking_playlist_games(lobby_browser.games, lobby_browser.count,
			browser_doubles_playlist);
	}
	/* (the co-op list's: its friends' games, then the public co-op ones,
	none hidden by the server browser's filters, searches or tab) */
	if (coop_join_listing())
		lobby_browser.count = coop_join_order(lobby_browser.count);
	else
		lobby_browser.count = lobby_browser_favorites(lobby_browser.count);
	/* (the hosts timed in the order shown: p2p_lobby.c's update_probes) */
	p2p_lobby_shown(lobby_browser.games, lobby_browser.count);
	rows = lobby_browser_rows();
	/* (the game starred or unstarred, moved by it: its row focused) */
	if (browser_follow_key[0])
	{
		short index;

		for (index = 0; index < rows; index++)
		{
			struct browser_favorite favorite;

			if (lobby_browser_heading(index))
				continue;
			if (index < lobby_browser.count)
				listing_favorite(&lobby_browser.games[index], &favorite);
			else
				favorite = *browser_favorites_get(browser_offline[index - lobby_browser.count]);
			if (!strcmp(favorite.key, browser_follow_key))
			{
				lobby_browser.chosen = index;
				lobby_browser.first = (short)PIN(lobby_browser.first, index - shown + 1, index);
				focused = (short)(index - lobby_browser.first);
				view->focus = focused;
				break;
			}
		}
		browser_follow_key[0] = 0;
	}
	if (focused == shown - 1 && lobby_browser.first + shown < rows && list_scrolls_at_end())
	{
		lobby_browser.first++;
		view->focus = --focused;
	}
	else if (focused == 0 && lobby_browser.first > 0 && list_scrolls_at_end())
	{
		lobby_browser.first--;
		view->focus = ++focused;
	}
	lobby_browser.first = (short)PIN(lobby_browser.first, 0, MAX(0, rows - shown));
	/* (the first game found takes the focus from the buttons, which had it
	while there were none) */
	if (rows && !lobby_browser.shown)
	{
		lobby_browser.shown = TRUE;
		if (shown)
		{
			view->focus = 0;
			view->focus_titles = FALSE;
			focused = 0;
			lobby_browser.chosen = lobby_browser.first;
		}
	}
	focused = lobby_browser_off_heading(view, focused, shown);
	if (focused != NONE && !ui_widget_port_hover_focused() &&
		!lobby_browser_heading((short)(lobby_browser.first + focused)))
	{
		lobby_browser.chosen = (short)(lobby_browser.first + focused);
	}
	/* (a game chosen, not a heading: the list moved under the choice) */
	if (lobby_browser_heading(lobby_browser.chosen))
		lobby_browser.chosen++;
	chosen = lobby_browser.chosen;
	/* (its host's tunnel opened and kept while it is chosen: joining it is
	then at once) */
	if (chosen < lobby_browser.count && !lobby_browser.joining && lobby_browser.games[chosen].open &&
		!lobby_browser.games[chosen].failed && !coop_join_party_row(chosen))
	{
		p2p_probe_keep(&lobby_browser.games[chosen]);
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
				if (!lobby_browser_heading(index) &&
					!memcmp(lobby_browser.games[index].identifier, lobby_browser.identifier, 6) &&
					index >= lobby_browser.first && index < lobby_browser.first + shown)
				{
					view->focus = (short)(index - lobby_browser.first);
					view->focus_titles = FALSE;
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
		{
			browser_listing_row(&lobby_browser.games[chosen], &shown);
			coop_join_row_fix(chosen, &shown);
		}
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
		/* (a friend's game the co-op list joins through their party) */
		else if (chosen < rows && coop_join_party_row(chosen))
			coop_join_status(chosen, text, NUMBEROF(text));
		else if (chosen < rows)
			browser_status_text(&shown, text, NUMBEROF(text));
		else if (coop_join_listing())
			usnprintf(text, NUMBEROF(text) - 1, L"Your friends' co-op games, then everyone's.");
		else
		{
			usnprintf(text, NUMBEROF(text) - 1, browser_doubles_playlist != NONE ?
				L"Choose a game to join it, or host one." : L"Every public game on the Internet, GOAT H1 and Legacy.");
		}
		text[NUMBEROF(text) - 1] = 0;
		browser_display.row_shown = chosen < rows;
		if (chosen < rows)
			browser_display.row = shown;
		settings_wide_copy(browser_display.title, browser_doubles_playlist != NONE ? browser_doubles_name :
			coop_join_listing() ? L"JOIN CO-OP" : L"SERVER BROWSER", NUMBEROF(browser_display.title));
		settings_wide_copy(browser_display.status, text, NUMBEROF(browser_display.status));
		for (index = 0; index < lobby_browser.count; index++)
			players += lobby_browser.games[index].player_count;
		browser_empty_text(multiplayer.mode, browser_tab, config_boolean("network.online"),
			config_boolean("network.public_lobby"), now - lobby_browser.begin_time < LOBBY_BROWSER_LOOK_TIME,
			p2p_lobby_newer_games(), browser_filtered_count, browser_searched_count, text, NUMBEROF(text));
		/* (the 2V2 browser's, once it has looked: none of its kind) */
		if (browser_doubles_playlist != NONE && config_boolean("network.online") &&
			config_boolean("network.public_lobby") && now - lobby_browser.begin_time >= LOBBY_BROWSER_LOOK_TIME &&
			!browser_searched_count)
		{
			doubles_browser_empty(text, NUMBEROF(text));
		}
		/* (the co-op list's: why none, and its fix) */
		if (coop_join_listing())
			coop_join_empty(text, NUMBEROF(text));
		settings_wide_copy(browser_display.empty, text, NUMBEROF(browser_display.empty));
		browser_display.games = (short)(lobby_browser.count - lobby_browser.headings);
		browser_display.players = players;
		browser_display.rows = rows;
	}
	view->count = rows;
	view->first = lobby_browser.first;
	view->chosen = chosen < rows ? chosen : NONE;
}

/* the server browser's rows, each its game's (or its group's heading), in
the generated screen's list */
static void lobby_browser_rows_show(struct widget_instance *list, short rows)
{
	struct widget_instance *row;

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
		if (lobby_browser_heading(index))
		{
			browser_heading_show(row, row_index, browser_group_heading((short)(lobby_browser.headed[index] - 1)));
			continue;
		}
		if (index >= lobby_browser.count)
			browser_offline_row(browser_favorites_get(browser_offline[index - lobby_browser.count]), &shown);
		else
			browser_listing_row(&lobby_browser.games[index], &shown);
		browser_row_show(row, row_index, &shown, index < lobby_browser.count && browser_listed_favorite[index]);
	}
}

/* "gamespy screen update" for the server browser: the model (above), the
rows, the panel, the counts and tabs over them */
static void lobby_browser_update(struct widget_instance *list)
{
	struct menu_table_view view;

	/* (the searches' words typed this frame: the list filtered by them) */
	browser_searches_show(list);
	browser_view_of(list, &view);
	lobby_browser_model(&view);
	browser_view_apply(list, &view);
	lobby_browser_rows_show(list, view.count);
	browser_panel_show(list, browser_display.row_shown ? &browser_display.row : NULL, browser_display.title,
		browser_display.status);
	browser_header_show(list, browser_display.games, browser_display.players, browser_display.rows,
		browser_display.empty);
	browser_clear_show(list, browser_display.rows);
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
static boolean lobby_browser_select(struct widget_instance *widget, short controller, short row, boolean button,
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
	if (row == NONE && !button)
		return TRUE;
	if (lobby_browser.joining)
		return TRUE;
	index = row != NONE ? (short)(lobby_browser.first + row) : lobby_browser.chosen;
	/* (a group's heading: nothing to join) */
	if (lobby_browser_heading(index))
		return TRUE;
	if (index >= lobby_browser.count)
		return campaign_fail();
	/* (a friend's game not listed: joined through their party, which
	follows them into it) */
	if (coop_join_party_row(index))
		return coop_join_friend(index);
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
static short local_browser_favorites(struct menu_table_view *view)
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
		entries[index].project = FALSE;
		entries[index].coop = game->engine_type == 0;
		entries[index].dedicated = advertised_dedicated(game);
		settings_wide_copy(entries[index].mode, game_kind_name(game->engine_type, favorites[index].kind, FALSE),
			BROWSER_MODE_LENGTH);
		entries[index].maximum_players = game->maximum_player_count;
		entries[index].engine_type = (short)game->engine_type;
		entries[index].locked = FALSE;
		entries[index].kind = favorites[index].kind;
		entries[index].has_teams = game->has_teams != 0;
		/* (a local network game's advertisement carries no playlist) */
		entries[index].pro_2v2 = FALSE;
		entries[index].map_shown[0] = 0;
		if (browser_search_reads_map_shown())
			game_map_name(game, entries[index].map_shown);
	}
	browser_favorites_match(favorites, multiplayer.game_count, favorite_of,
		multiplayer.mode == _multiplayer_mode_lan ? "lan:" : NULL);
	/* (Join by Link's games are the invites taken: never hidden) */
	browser_filtering = multiplayer.mode != _multiplayer_mode_direct_link;
	browser_grouped = FALSE;
	multiplayer.game_count = browser_favorites_order(favorite_of, entries, multiplayer.game_count, order);
	if (browser_filtering)
		browser_offline_search();
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
				view->focus = (short)(index - multiplayer.game_first);
				break;
			}
		}
		browser_follow_key[0] = 0;
	}
	if (multiplayer.game_chosen >= rows)
		multiplayer.game_chosen = (short)MAX(0, rows - 1);
	return rows;
}

/* the local network's and Join by Link's model, each frame (as
lobby_browser_model's): its games ordered, the rows' first and the game
chosen, and what the panel and the words over the list show */
static void local_browser_model(struct menu_table_view *view)
{
	short focused = view->focused;
	short rows, chosen;
	struct browser_row shown;
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	long players = 0;
	short index, shown_rows = view->shown;

	browser_shown_rows = shown_rows ? shown_rows : BROWSER_ROWS;
	browser_games_read();
	rows = local_browser_favorites(view);
	/* (more than its rows: scrolled on at their ends, as the server
	browser's) */
	if (focused == shown_rows - 1 && multiplayer.game_first + shown_rows < rows && list_scrolls_at_end())
	{
		multiplayer.game_first++;
		view->focus = --focused;
	}
	else if (focused == 0 && multiplayer.game_first > 0 && list_scrolls_at_end())
	{
		multiplayer.game_first--;
		view->focus = ++focused;
	}
	multiplayer.game_first = (short)PIN(multiplayer.game_first, 0, MAX(0, rows - shown_rows));
	if (focused != NONE && multiplayer.game_first + focused < rows && !view->hover)
		multiplayer.game_chosen = (short)(multiplayer.game_first + focused);
	chosen = multiplayer.game_chosen;
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
	browser_display.row_shown = chosen < rows;
	if (chosen < rows)
		browser_display.row = shown;
	settings_wide_copy(browser_display.title, multiplayer.mode == _multiplayer_mode_direct_link ? L"JOIN BY LINK" :
		L"LOCAL NETWORK", NUMBEROF(browser_display.title));
	settings_wide_copy(browser_display.status, text, NUMBEROF(browser_display.status));
	for (index = 0; index < multiplayer.game_count; index++)
		players += multiplayer.games[index]->player_count;
	browser_empty_text(multiplayer.mode, browser_tab, TRUE, TRUE, FALSE, 0, browser_filtered_count,
		browser_searched_count, text, NUMBEROF(text));
	settings_wide_copy(browser_display.empty, text, NUMBEROF(browser_display.empty));
	browser_display.games = multiplayer.game_count;
	browser_display.players = players;
	browser_display.rows = rows;
	view->count = rows;
	view->first = multiplayer.game_first;
	view->chosen = chosen < rows ? chosen : NONE;
}

/* "gamespy screen update": the games' rows, the one chosen's details in the
panel, the counts and tabs over them (the server browser's: above) */
static void browser_update(struct widget_instance *list)
{
	struct widget_instance *row;
	struct menu_table_view view;
	struct browser_row shown;
	short index;

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
	browser_searches_show(list);
	browser_view_of(list, &view);
	local_browser_model(&view);
	browser_view_apply(list, &view);
	for (row = list->child; row; row = row->next)
	{
		short row_index = browser_row_index(row);

		if (row_index == NONE)
			continue;
		index = (short)(multiplayer.game_first + row_index);
		row->visible = index < view.count;
		if (index >= view.count)
			continue;
		if (index >= multiplayer.game_count)
			browser_offline_row(browser_favorites_get(browser_offline[index - multiplayer.game_count]), &shown);
		else
			browser_advertised_row(multiplayer.games[index], &shown);
		browser_row_show(row, row_index, &shown, index < multiplayer.game_count && browser_listed_favorite[index]);
	}
	browser_panel_show(list, browser_display.row_shown ? &browser_display.row : NULL, browser_display.title,
		browser_display.status);
	browser_header_show(list, browser_display.games, browser_display.players, browser_display.rows,
		browser_display.empty);
	browser_clear_show(list, browser_display.rows);
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
	/* the campaign's next level for player 1's profile, else NONE (read
	from the profile's file with the rest, not each frame) */
	short next_level;
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
	multiplayer_status.next_level = multiplayer_status_next_level();
}

/* the width of a digit of the counts (port_settings.DIGIT_SIZE), and how
many places a count has */
#define MULTIPLAYER_STATUS_DIGIT_WIDTH 26
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
		else if (item == _multiplayer_item_online_coop && multiplayer_status.next_level != NONE)
		{
			wchar_t level[ROW_TEXT_LENGTH];

			string_get("pc\\main_menu\\map_list", multiplayer_status.next_level, level);
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
		if (multiplayer_status.next_level != NONE)
		{
			wchar_t level[ROW_TEXT_LENGTH];

			string_get("pc\\main_menu\\map_list", multiplayer_status.next_level, level);
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
	if (server_browser && lobby_browser_heading(index))
		return TRUE;
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
static boolean browser_select_row(struct widget_instance *widget, short controller, short row, boolean button,
	boolean *widget_deleted);

static boolean browser_select(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
{
	short row = browser_row_index(widget);

	(void)event;
	if (row != NONE && !lobby_browser.ready && browser_click_on_star(widget))
		return browser_favorite_toggle(row);
	return browser_select_row(widget, controller, row, strstr(widget->name, "button_join") != NULL, widget_deleted);
}

/* ... its row's (0 up, on the list: NONE for JOIN GAME's, button) */
static boolean browser_select_row(struct widget_instance *widget, short controller, short row, boolean button,
	boolean *widget_deleted)
{
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
		return lobby_browser_select(widget, controller, row, button, widget_deleted);
	if (row != NONE)
		multiplayer.game_chosen = (short)(multiplayer.game_first + row);
	if (row != NONE || button)
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

/* ---- the hand-written browser's (screens/browser.xml: menu_screen.c and
menu_table.h): the browsers' model as a table's row source, its bindings and
actions */

static void text_utf8(wchar_t const *text, char *out, long size);

/* the row of the list's index (a game found, or a favorite offline) as the
rows and the panel show it, and whether it is a favorite; FALSE for none */
static boolean browser_source_row(short index, struct browser_row *row, boolean *favorite)
{
	boolean server_browser = multiplayer.mode == _multiplayer_mode_server_browser;
	short count = server_browser ? lobby_browser.count : multiplayer.game_count;

	*favorite = FALSE;
	if (index < 0 || index >= count + browser_offline_count || (server_browser && lobby_browser_heading(index)))
		return FALSE;
	if (index >= count)
		browser_offline_row(browser_favorites_get(browser_offline[index - count]), row);
	else if (server_browser)
	{
		browser_listing_row(&lobby_browser.games[index], row);
		coop_join_row_fix(index, row);
	}
	else
		browser_advertised_row(multiplayer.games[index], row);
	*favorite = (index < count && browser_listed_favorite[index]) || row->offline;
	return TRUE;
}

/* a co-op game's difficulty, as its gametype's words end with it ("Co-op
Heroic": network_server_manager.c's listing, coop_menu.inc's friends'
rows); empty for none */
static wchar_t const *browser_coop_difficulty(struct browser_row const *row)
{
	static wchar_t const prefix[] = L"Co-op ";
	short index;

	for (index = 0; prefix[index]; index++)
	{
		if (row->gametype[index] != prefix[index])
			return L"";
	}
	return row->gametype + index;
}

static void browser_source_update(struct menu_table_view *view)
{
	halo_keyboard_browser_keys();
	if (view->screen)
		party_screen_noted(view->screen);
	if (multiplayer.mode == _multiplayer_mode_server_browser)
		lobby_browser_model(view);
	else
		local_browser_model(view);
}

static int browser_source_heading(short index, unsigned short *words, short size)
{
	if (multiplayer.mode != _multiplayer_mode_server_browser || !lobby_browser_heading(index))
		return FALSE;
	settings_wide_copy((wchar_t *)words, browser_group_heading((short)(lobby_browser.headed[index] - 1)), size);
	return TRUE;
}

/* a colour of menu_functions.c's (0xRRGGBBFF) for a cell */
static void browser_source_cell(short index, char const *column, int focused, struct menu_table_cell *cell)
{
	static short cached_index = NONE;
	static unsigned long cached_frame;
	static struct browser_row row;
	static boolean favorite, found;
	wchar_t *text = (wchar_t *)cell->text;

	/* (a row read once for its cells) */
	if (index != cached_index || cached_frame != system_milliseconds())
	{
		found = browser_source_row(index, &row, &favorite);
		cached_index = index;
		cached_frame = system_milliseconds();
	}
	cell->frame = NONE;
	if (!found)
		return;
	if (row.offline)
		cell->color = PLAYERS_COLOR_CLOSED;
	if (!strcmp(column, "favorite"))
		cell->frame = (short)(row.offline ? 2 : favorite ? 1 : focused ? 3 : 0);
	else if (!strcmp(column, "server"))
	{
		settings_wide_copy(text, row.name, MENU_TABLE_TEXT_LENGTH);
		if (row.openh1 && !row.offline)
		{
			usnprintf((wchar_t *)cell->badge, MENU_TABLE_BADGE_LENGTH - 1, L"%d", row.rate);
			cell->badge[MENU_TABLE_BADGE_LENGTH - 1] = 0;
		}
		cell->lock = row.locked;
	}
	/* (the co-op list's: the host, the mission, its difficulty) */
	else if (!strcmp(column, "host"))
	{
		settings_wide_copy(text, row.name, MENU_TABLE_TEXT_LENGTH);
		cell->lock = row.locked;
	}
	else if (!strcmp(column, "mission"))
		settings_wide_copy(text, row.map, MENU_TABLE_TEXT_LENGTH);
	else if (!strcmp(column, "difficulty"))
		settings_wide_copy(text, browser_coop_difficulty(&row), MENU_TABLE_TEXT_LENGTH);
	else if (!strcmp(column, "map"))
		settings_wide_copy(text, row.map, MENU_TABLE_TEXT_LENGTH);
	else if (!strcmp(column, "mode"))
		settings_wide_copy(text, row.mode, MENU_TABLE_TEXT_LENGTH);
	else if (!strcmp(column, "players"))
	{
		if (row.offline)
			usnprintf(text, MENU_TABLE_TEXT_LENGTH - 1, L"OFFLINE");
		else
			usnprintf(text, MENU_TABLE_TEXT_LENGTH - 1, L"%d/%d", row.players, row.maximum_players);
		text[MENU_TABLE_TEXT_LENGTH - 1] = 0;
		if ((!row.open && !row.offline) || row.party_room == _browser_party_no_room)
			cell->color = PLAYERS_COLOR_CLOSED;
	}
	else if (!strcmp(column, "ping"))
	{
		short ping = row.ping_state == _browser_ping_measured ? row.ping : -1;

		if (row.offline)
		{
			text[0] = 0;
			return;
		}
		browser_ping_cell(row.ping_state, row.ping, text, MENU_TABLE_TEXT_LENGTH);
		cell->frame = browser_ping_bars(ping);
		if (ping >= 0 || row.ping_state == _browser_ping_measuring)
			cell->color = ping < 0 ? PING_COLOR_MEASURING : ping < PING_GOOD ? PING_COLOR_GOOD : ping < PING_FAIR ?
				PING_COLOR_FAIR : PING_COLOR_POOR;
	}
}

struct menu_table_source const pc_menu_browser_source =
{
	browser_source_update, browser_source_heading, browser_source_cell,
};

/* the panel's line of where the game is: its map and mode, and its
gametype's own name if it has another */
static void browser_where_text(struct browser_row const *row, wchar_t *text, short size)
{
	if (row->gametype[0] && browser_wide_compare(row->gametype, row->mode))
		usnprintf(text, size - 1, L"%s \x00B7 %s (%s)", row->map, row->mode, row->gametype);
	else
		usnprintf(text, size - 1, L"%s \x00B7 %s", row->map, row->mode);
	text[size - 1] = 0;
}

int pc_menu_browser_value(char const *name, char *value, unsigned long size)
{
	wchar_t text[SETTINGS_PANEL_TEXT_LENGTH];
	struct browser_row const *row = browser_display.row_shown ? &browser_display.row : NULL;
	boolean link = multiplayer.mode == _multiplayer_mode_direct_link;
	short index;

	text[0] = 0;
	value[0] = 0;
	if (!strcmp(name, "browser.title"))
	{
		settings_wide_copy(text, multiplayer.mode == _multiplayer_mode_lan ? L"LOCAL NETWORK" : link ?
			L"JOIN BY LINK" : coop_join_listing() ? L"JOIN CO-OP" : L"SERVER BROWSER", NUMBEROF(text));
	}
	/* (the co-op list's: its counts, the chosen game's mission and
	difficulty, and the fix for an empty list) */
	else if (coop_join_listing() && (!strcmp(name, "browser.counts") || !strcmp(name, "browser.fix") ||
		!strcmp(name, "browser.panel.where")))
	{
		if (!coop_join_value(name, row, text, NUMBEROF(text)))
			return FALSE;
	}
	else if (!strcmp(name, "browser.counts"))
	{
		short games = browser_display.games;
		long players = browser_display.players;

		/* (as the generated list's: browser_header_show) */
		if (browser_filtering && browser_filtered_count > 0)
		{
			usnprintf(text, NUMBEROF(text) - 1, L"%d OF %d GAMES    %ld %s", games, games + browser_filtered_count,
				players, players == 1 ? L"PLAYER" : L"PLAYERS");
		}
		else
		{
			usnprintf(text, NUMBEROF(text) - 1, L"%d %s    %ld %s", games, games == 1 ? L"GAME" : L"GAMES", players,
				players == 1 ? L"PLAYER" : L"PLAYERS");
		}
	}
	else if (!strcmp(name, "browser.empty"))
		settings_wide_copy(text, browser_display.empty, NUMBEROF(text));
	else if (!strcmp(name, "browser.searched"))
	{
		if (browser_display.rows == 0 && browser_searched_count > 0 && !link)
			settings_wide_copy(text, L"1", NUMBEROF(text));
	}
	else if (!strcmp(name, "browser.host"))
		settings_wide_copy(text, pc_menu_browser_visit_kind() == 2 ? L"HOST CO-OP" : L"HOST & MORE", NUMBEROF(text));
	else if (!strcmp(name, "browser.tab"))
		snprintf(value, size, "%s", browser_tab_words[PIN(browser_tab, 0, NUMBER_OF_BROWSER_TABS - 1)]);
	else if (!strcmp(name, "browser.sort"))
		snprintf(value, size, "%s %s", browser_sort_words[PIN(browser_sort_column, 0, NUMBER_OF_BROWSER_SORTS - 1)],
			browser_sort_descending ? "down" : "up");
	else if (!strcmp(name, "browser.sort.title"))
		settings_wide_copy(text, browser_sorts[PIN(browser_sort_column, 0, NUMBER_OF_BROWSER_SORTS - 1)].title,
			NUMBEROF(text));
	else if (!strncmp(name, "browser.search.", 15))
	{
		for (index = 0; index < NUMBER_OF_BROWSER_SEARCHES; index++)
		{
			if (!strcmp(name + 15, browser_search_columns[index]) ||
				(index == _browser_search_server && !strcmp(name + 15, "server")))
				snprintf(value, size, "%s", browser_searches[index]);
		}
	}
	else if (!strcmp(name, "browser.panel.title"))
		settings_wide_copy(text, row ? row->name : browser_display.title, NUMBEROF(text));
	else if (!strcmp(name, "browser.panel.status"))
		settings_wide_copy(text, browser_display.status, NUMBEROF(text));
	else if (!strcmp(name, "browser.panel.where") && row)
		browser_where_text(row, text, NUMBEROF(text));
	else if (!strcmp(name, "browser.panel.kind") && row && !row->offline)
		settings_wide_copy(text, row->openh1 ? L"GOAT H1" : L"LEGACY", NUMBEROF(text));
	else if (!strcmp(name, "browser.panel.kind.tone") && row)
		settings_wide_copy(text, row->openh1 ? L"0" : L"1", NUMBEROF(text));
	else if (!strcmp(name, "browser.panel.tick") && row && !row->offline)
		usnprintf(text, NUMBEROF(text) - 1, L"%d TICK", row->rate ? row->rate : 30);
	else if (!strcmp(name, "browser.panel.opence") && row && !row->offline && (!row->openh1 || row->mixed))
		settings_wide_copy(text, L"OPENCE CAN JOIN", NUMBEROF(text));
	else if (!strcmp(name, "browser.panel.players") && row)
	{
		if (row->offline)
			usnprintf(text, NUMBEROF(text) - 1, L"Offline");
		else if (row->bots)
			usnprintf(text, NUMBEROF(text) - 1, L"%d/%d \x00B7 %d BOT%s", row->players, row->maximum_players,
				row->bots, row->bots == 1 ? L"" : L"S");
		else
			usnprintf(text, NUMBEROF(text) - 1, L"%d/%d", row->players, row->maximum_players);
	}
	else if (!strcmp(name, "browser.panel.ping") && row)
		browser_ping_text(row, text, NUMBEROF(text));
	else if (!strcmp(name, "browser.joinable"))
	{
		if (row && !row->offline)
			settings_wide_copy(text, L"1", NUMBEROF(text));
	}
	else if (!strcmp(name, "browser.server"))
	{
		if (multiplayer.mode == _multiplayer_mode_server_browser)
			settings_wide_copy(text, L"1", NUMBEROF(text));
	}
	else if (!strcmp(name, "browser.link"))
	{
		if (link)
			settings_wide_copy(text, L"1", NUMBEROF(text));
	}
	else if (!strcmp(name, "browser.filtered"))
	{
		if (!link)
			settings_wide_copy(text, L"1", NUMBEROF(text));
	}
	else
		return FALSE;
	text[NUMBEROF(text) - 1] = 0;
	if (text[0])
		text_utf8(text, value, (long)size);
	return value[0] != 0;
}

void pc_menu_browser_search_set(char const *name, char const *text)
{
	short index;

	if (!name || strncmp(name, "browser.search.", 15))
		return;
	for (index = 0; index < NUMBER_OF_BROWSER_SEARCHES; index++)
	{
		if (!strcmp(name + 15, browser_search_columns[index]) ||
			(index == _browser_search_server && !strcmp(name + 15, "server")))
			snprintf(browser_searches[index], sizeof(browser_searches[0]), "%s", text ? text : "");
	}
}

boolean pc_menu_browser_tab(char const *word)
{
	return browser_tab_choose(browser_word_index(word, browser_tab_words, NUMBER_OF_BROWSER_TABS));
}

boolean pc_menu_browser_sort(char const *word)
{
	if (!word)
		return browser_sort_choose(_browser_sort_players, TRUE);
	return browser_sort_choose(browser_word_index(word, browser_sort_words, NUMBER_OF_BROWSER_SORTS), FALSE);
}

boolean pc_menu_browser_favorite(short row)
{
	return browser_favorite_toggle(row);
}

boolean pc_menu_browser_select(struct widget_instance *widget, struct event_record *event, short row,
	boolean *deleted)
{
	*deleted = FALSE;
	return browser_select_row(widget, event_controller(widget, event), row, row == NONE, deleted);
}

/* ---- the hand-written Select Profile's (screens/profile_select.xml:
menu_screen.c and menu_table.h): the profile list's model (profile_list, as
the generated screen's list has it: "3wide player profile list update") as a
table's row source, the chosen profile's details as the profile.* bindings,
and the row an action is for chosen. The chosen row is the focused one, as
the generated list's description follows its focus */

static struct
{
	/* (the screen opened: its first frame puts the focus on the chosen row) */
	boolean opened;
} profile_source;

static void profile_source_opened(void)
{
	profile_source.opened = TRUE;
	profile_list_read(TRUE);
}

static void profile_source_update(struct menu_table_view *view)
{
	short shown = view->shown, focused = view->focused;
	short rows;

	profile_list_read(FALSE);
	rows = (short)(profile_list.count + 1);
	/* (as it opens: player 1's profile chosen, in the table's middle) */
	if (profile_source.opened && shown)
	{
		profile_source.opened = FALSE;
		profile_list_open(shown);
		focused = (short)(profile_list.chosen - profile_list.first);
		view->focus = focused;
	}
	/* (the focus kept off the table's ends while there is more past them,
	the rows moving instead: list_scroll's) */
	if (focused == shown - 1 && profile_list.first + shown < rows && list_scrolls_at_end())
	{
		profile_list.first++;
		view->focus = --focused;
	}
	else if (focused == 0 && profile_list.first > 0 && list_scrolls_at_end())
	{
		profile_list.first--;
		view->focus = ++focused;
	}
	profile_list.first = (short)PIN(profile_list.first, 0, MAX(0, rows - shown));
	if (focused != NONE && focused >= 0 && focused < shown)
		profile_list.chosen = (short)PIN(profile_list.first + focused, 0, rows - 1);
	profile_list.chosen = (short)PIN(profile_list.chosen, 0, rows - 1);
	view->count = rows;
	view->first = profile_list.first;
	view->chosen = profile_list.chosen;
}

static int profile_source_heading(short index, unsigned short *words, short size)
{
	(void)index;
	(void)words;
	(void)size;
	return FALSE;
}

/* a row's name: its profile's, or the row that makes one's words (the
generated list's: profile_row_text) */
static void profile_source_cell(short index, char const *column, int focused, struct menu_table_cell *cell)
{
	wchar_t text[ROW_TEXT_LENGTH];

	(void)focused;
	cell->frame = NONE;
	if (strcmp(column, "name") || index < 0 || index > profile_list.count)
		return;
	profile_row_text((short)(index - profile_list.first), text);
	settings_wide_copy((wchar_t *)cell->text, text, MENU_TABLE_TEXT_LENGTH);
}

struct menu_table_source const pc_menu_profile_source =
{
	profile_source_update, profile_source_heading, profile_source_cell, profile_source_opened,
};

/* a profile.* binding's value (UTF-8; nonzero if it has one): the chosen
row's profile's name, current level, best difficulty, controls and color
(the generated screen's description's: profile_list_update), the row's
title, whether a profile is chosen, and player 1's profile's name */
int pc_menu_profile_value(char const *name, char *value, unsigned long size)
{
	wchar_t text[ROW_TEXT_LENGTH];
	struct player_profile const *chosen = profile_list.chosen < profile_list.count ?
		&profile_list.profiles[profile_list.chosen] : NULL;

	text[0] = 0;
	value[0] = 0;
	if (!strcmp(name, "profile.title"))
	{
		if (chosen)
			settings_wide_copy(text, chosen->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1);
		else
			profile_row_text((short)(profile_list.chosen - profile_list.first), text);
	}
	else if (!strcmp(name, "profile.current"))
	{
		struct player_profile const *current = profile_current();

		if (current)
			settings_wide_copy(text, current->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1);
	}
	else if (!chosen)
		return FALSE;
	else if (!strcmp(name, "profile.name"))
		settings_wide_copy(text, chosen->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1);
	else if (!strcmp(name, "profile.chosen"))
		snprintf(value, size, "yes");
	else if (!strcmp(name, "profile.color"))
	{
		if (chosen->primary_color_index >= 0 && chosen->primary_color_index < COLOR_COUNT)
			snprintf(value, size, "%d", chosen->primary_color_index);
		else
			snprintf(value, size, "random");
	}
	else if (!strcmp(name, "profile.level"))
	{
		string_get("pc\\main_menu\\map_list",
			(short)PIN(chosen->last_single_player_map_played, 0, NUMBER_OF_SINGLE_PLAYER_LEVELS - 1), text);
	}
	else if (!strcmp(name, "profile.difficulty"))
	{
		short level, difficulty;

		player_profile_get_highest_completed_solo_level((struct player_profile *)chosen, &level, &difficulty);
		if (level != NONE)
			string_get("pc\\main_menu\\player_profiles_select\\difficulty_names", difficulty, text);
	}
	else if (!strcmp(name, "profile.controls"))
	{
		string_get("pc\\main_menu\\player_profiles_select\\joystick_set_defaults_descriptions",
			(short)(chosen->controller_settings.invert_look ? 1 : 0), text);
	}
	else
		return FALSE;
	text[NUMBEROF(text) - 1] = 0;
	if (text[0])
		text_utf8(text, value, (long)size);
	return value[0] != 0;
}

/* "profile select" (A, a click or a tap on a row, or OK): the row's
profile (the chosen one's, for NONE) made player 1's, as the generated
list's "profile manager select"; FALSE for the row that makes one */
int pc_menu_profile_choose(struct widget_instance *widget, struct event_record *event, short row)
{
	if (row != NONE)
		profile_list.chosen = (short)PIN(profile_list.first + row, 0, profile_list.count);
	return profile_choose(event_controller(widget, event));
}

/* ---- the hand-written Profile Color's (screens/profile_color.xml:
menu_screen.c and menu_table.h): the profile's colors as a table's row
source, the PC version's picker's 18 (colors_list's names, in their colors)
and after them Random (the profile's color NONE: a new profile's, the host
giving the player a color no one else has, network_server_manager.c's
get_unique_random_color); the focused row's color as the color.* bindings,
and the row an action is for chosen for the profile being edited. The chosen
row is the focused one, as Select Profile's */

#define COLOR_RANDOM_ROW COLOR_COUNT
#define COLOR_SOURCE_ROWS (COLOR_COUNT + 1)

static struct
{
	/* (the screen opened: its first frame puts the focus on the profile's
	color) */
	boolean opened;
	short first;
	short chosen;
} color_source;

/* a profile color's row: its own, Random's for NONE (or one past the
colors) */
static short color_source_row(short color)
{
	return color >= 0 && color < COLOR_COUNT ? color : COLOR_RANDOM_ROW;
}

/* the row of the edited profile's color: NONE without a profile */
static short color_source_saved(void)
{
	struct player_profile const *profile = player_ui_get_edit_player_profile();

	return profile ? color_source_row(profile->primary_color_index) : NONE;
}

static void color_source_name(short row, wchar_t *text)
{
	if (row == COLOR_RANDOM_ROW)
		settings_wide_copy(text, L"Random", ROW_TEXT_LENGTH);
	else
	{
		string_get("pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\colors_list",
			row, text);
	}
}

static void color_source_opened(void)
{
	color_source.opened = TRUE;
}

static void color_source_update(struct menu_table_view *view)
{
	short shown = view->shown, focused = view->focused;
	short rows = COLOR_SOURCE_ROWS;

	/* (as it opens: the profile's color chosen, the table from its top
where the focus is on it (off its last row, which would scroll), else the
chosen row in its middle) */
	if (color_source.opened && shown)
	{
		short saved = color_source_saved();

		color_source.opened = FALSE;
		color_source.chosen = saved != NONE ? saved : 0;
		color_source.first = color_source.chosen < shown - 1 ? 0 :
			(short)PIN(color_source.chosen - shown / 2, 0, MAX(0, rows - shown));
		focused = (short)(color_source.chosen - color_source.first);
		view->focus = focused;
	}
	/* (the focus kept off the table's ends while there is more past them,
	the rows moving instead: list_scroll's) */
	if (focused == shown - 1 && color_source.first + shown < rows && list_scrolls_at_end())
	{
		color_source.first++;
		view->focus = --focused;
	}
	else if (focused == 0 && color_source.first > 0 && list_scrolls_at_end())
	{
		color_source.first--;
		view->focus = ++focused;
	}
	color_source.first = (short)PIN(color_source.first, 0, MAX(0, rows - shown));
	if (focused != NONE && focused >= 0 && focused < shown)
		color_source.chosen = (short)(color_source.first + focused);
	color_source.chosen = (short)PIN(color_source.chosen, 0, rows - 1);
	view->count = rows;
	view->first = color_source.first;
	view->chosen = color_source.chosen;
}

static int color_source_heading(short index, unsigned short *words, short size)
{
	(void)index;
	(void)words;
	(void)size;
	return FALSE;
}

/* a row's swatch (its frame: the color's, Random's after them) and its
name in its color (black's lifted to a grey, to be read, as the <colors>
items'; Random's the table's own) */
static void color_source_cell(short index, char const *column, int focused, struct menu_table_cell *cell)
{
	wchar_t text[ROW_TEXT_LENGTH];

	(void)focused;
	cell->frame = NONE;
	if (index < 0 || index >= COLOR_SOURCE_ROWS)
		return;
	if (!strcmp(column, "swatch"))
		cell->frame = index;
	else if (!strcmp(column, "name"))
	{
		color_source_name(index, text);
		settings_wide_copy((wchar_t *)cell->text, text, MENU_TABLE_TEXT_LENGTH);
		if (index != COLOR_RANDOM_ROW)
		{
			real_rgb_color rgb = player_profile_get_rgb_color(index);

			cell->color = ((unsigned long)(MAX(rgb.red, 0.3f) * 255.0f + 0.5f) << 24) |
				((unsigned long)(MAX(rgb.green, 0.3f) * 255.0f + 0.5f) << 16) |
				((unsigned long)(MAX(rgb.blue, 0.3f) * 255.0f + 0.5f) << 8) | 0xFFUL;
		}
	}
}

struct menu_table_source const pc_menu_color_source =
{
	color_source_update, color_source_heading, color_source_cell, color_source_opened,
};

/* a color.* binding's value (UTF-8; nonzero if it has one): the edited
profile's name, the chosen row's color's name, its word for a <spartan>
(its number, or random), Current color while it is the profile's own, and a line
of what it does */
int pc_menu_color_value(char const *name, char *value, unsigned long size)
{
	wchar_t text[ROW_TEXT_LENGTH];
	struct player_profile const *profile = player_ui_get_edit_player_profile();
	short chosen = (short)PIN(color_source.chosen, 0, COLOR_SOURCE_ROWS - 1);

	text[0] = 0;
	value[0] = 0;
	if (!strcmp(name, "color.profile"))
	{
		if (profile)
			settings_wide_copy(text, profile->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1);
	}
	else if (!strcmp(name, "color.title"))
		color_source_name(chosen, text);
	else if (!strcmp(name, "color.spartan"))
	{
		if (chosen == COLOR_RANDOM_ROW)
			snprintf(value, size, "random");
		else
			snprintf(value, size, "%d", chosen);
	}
	else if (!strcmp(name, "color.current"))
	{
		if (chosen == color_source_saved())
			snprintf(value, size, "Current color");
	}
	else if (!strcmp(name, "color.about"))
	{
		snprintf(value, size, "%s", chosen == COLOR_RANDOM_ROW ?
			"Each game picks a color no one else has." : "Your armor in multiplayer games.");
	}
	else
		return FALSE;
	text[NUMBEROF(text) - 1] = 0;
	if (text[0])
		text_utf8(text, value, (long)size);
	return value[0] != 0;
}

/* "profile color" (A, a click or a tap on a row, or SELECT): the row's
color (the chosen one's, for NONE) made the edited profile's, as the PC
version's picker's player_profile_color_picker_select_color (Random: NONE,
a new profile's); FALSE with no profile being edited */
int pc_menu_color_choose(short row)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();

	if (row != NONE)
		color_source.chosen = (short)PIN(color_source.first + row, 0, COLOR_SOURCE_ROWS - 1);
	if (!profile)
		return FALSE;
	profile->primary_color_index = color_source.chosen == COLOR_RANDOM_ROW ? (short)NONE : color_source.chosen;
	return TRUE;
}

/* ---- the hand-written screens' (menu_screen.c and menu_table.h): the text
fields and text of the menus', for their searches and their tables */

void pc_menu_text_field_begin(struct widget_instance *field, char const *text, short maximum,
	void (*done)(char const *text))
{
	text_field_begin(field, text, maximum, done);
	text_field.enter_ends = TRUE;
}

int pc_menu_text_field_editing(struct widget_instance *field)
{
	return text_field_editing(field);
}

void pc_menu_text_field_end(int keep)
{
	text_field_end(keep != 0);
}

void pc_menu_text_field_show(struct widget_instance *field, char const *text, int editing)
{
	text_field_show(field, text, editing != 0);
}

char const *pc_menu_text_field_words(void)
{
	return text_field.text;
}

short pc_menu_text_fit(struct widget_instance *box, unsigned short const *text, short width)
{
	return menu_text_box_fit_to(box, (wchar_t const *)text, width);
}

void pc_menu_text_wrap(struct widget_instance *box, unsigned short const *text, short width, short lines)
{
	menu_text_box_wrap_width(box, (wchar_t const *)text, lines, width);
}

short pc_menu_text_lines(struct widget_instance *box, unsigned short const *text, short width, short lines)
{
	wchar_t wrapped[SETTINGS_PANEL_TEXT_LENGTH];
	short box_width;
	long font = ui_widget_port_text_box_font(box, &box_width);

	if (!box || !text || !text[0])
		return 0;
	return (short)MIN(settings_text_wrap((wchar_t const *)text, wrapped, NUMBEROF(wrapped), font,
		width > 0 ? width : box_width), lines);
}

short pc_menu_text_width(struct widget_instance *box, unsigned short const *text)
{
	short width;
	long font = ui_widget_port_text_box_font(box, &width);

	return font != NONE && text ? settings_text_width(font, (wchar_t const *)text) : 0;
}

/* "player profile save changes" (Settings' OK, the profile being edited):
saved if it has changes, as the Xbox's; if it has none (the settings' own
screens write theirs to config.toml as their OK is chosen), editing ends.
Either way the previous screen comes back, as CANCEL (the Xbox's called no
changes a failure, and closed every screen). The Xbox's saving screen
followed a save, and the screen came back after it only to close itself,
no longer editing ("close if not editing profile"): the save takes a few
hundredths of a second here (saving_screens.c) */
static boolean profile_save_changes(struct widget_instance *widget, boolean *widget_deleted)
{
	if (player_ui_edit_profile_is_dirty())
	{
		if (!player_ui_save_profile())
		{
			platform_log("menus: could not save the profile's changes");
			return campaign_fail();
		}
	}
	else
	{
		player_ui_end_editing_profile();
	}
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
players staying for the next (the carnage report, then the host's PICK GAME);
the host's first local player's alone (host_rows_player) */
static boolean pause_end_game(struct widget_instance *widget)
{
	if (!global_network_game_server_get() || !game_engine_running() || !host_rows_player(widget))
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

/* "port settings profile": Settings' band's right, the profile it edits:
PROFILE: and its name, as Select Profile's band has player 1's; in a split
screen game, the player whose it is, PLAYER 2 and its name; nothing with no
profile. Each frame, so a profile chosen or renamed shows at once */
static void settings_profile_text(struct widget_instance *widget)
{
	struct player_profile const *profile = player_ui_get_edit_player_profile();
	wchar_t text[ROW_TEXT_LENGTH];
	wchar_t name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1];
	char prefix[32];
	long length = 0, index;

	text[0] = 0;
	if (profile)
	{
		settings_wide_copy(name, profile->player_name, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1);
		name[MAXIMUM_PLAYER_PROFILE_NAME_LENGTH] = 0;
		if (!main_menu_is_active() && players_port_local_player_count() > 1)
			snprintf(prefix, sizeof(prefix), "PLAYER %d \xB7 ", settings_local_player + 1);
		else
			snprintf(prefix, sizeof(prefix), "PROFILE: ");
		for (index = 0; prefix[index] && length < ROW_TEXT_LENGTH - 1; index++)
			text[length++] = (wchar_t)(unsigned char)prefix[index];
		for (index = 0; name[index] && length < ROW_TEXT_LENGTH - 1; index++)
			text[length++] = name[index];
		text[length] = 0;
	}
	text_set(widget, text);
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
QUIT is theirs (the Xbox's "mp game player quit"). In the new menus that START
and ADD PLAYER open Add Player's screen instead (below), where each controller joins and
chooses its profile with its own keys */
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

static boolean add_player_open(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted);
static boolean lobby_board_start(short controller);

/* "port lobby add player" (ADD PLAYER): in the new menus Add Player's
screen, where each other controller joins and chooses its profile; in the
classic ones, the next START of another controller joins (one gamepad
leaves the keyboard's controller for its own) */
static boolean lobby_add_player(struct widget_instance *widget, struct event_record *event, boolean *widget_deleted)
{
	if (lobby_local_player_count() >= MAXIMUM_LOCAL_PLAYERS)
		return campaign_fail();
	if (add_player_open(widget, event, NONE, widget_deleted))
		return TRUE;
	lobby_join.adding = TRUE;
	return TRUE;
}

/* "port lobby join" (START): a controller not playing joins, choosing its
profile next (in the new menus on Add Player's screen; FALSE: no profile
screen); a player's START is the focused button's */
static boolean lobby_join_start(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
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
	/* (the new menus: Add Player's screen open, this controller joining
	there; the classic profile list not opened from the screen gone) */
	if (add_player_open(widget, event, controller, widget_deleted))
		return TRUE;
	lobby_join.controller = controller;
	return TRUE;
}

/* "lobby start" (the hand-written lobby's Start and its START GAME, A on
its players): a controller not in the game joins (Add Player's screen, it
choosing its profile there), as the generated lobby's START does ("port
lobby join"); a player's starts the host's game (FALSE where it does not:
a player not the host's, whose Start is then the screen's widgets') */
int pc_menu_lobby_start(struct widget_instance *widget, struct event_record *event, int *deleted)
{
	short controller = event_controller(widget, event);
	boolean widget_deleted = FALSE;
	boolean result;

	*deleted = FALSE;
	if (lobby_controller_playing(controller))
		return lobby_board_start(controller);
	/* (no Add Player's screen: the generated lobby's START, its profile
	list) */
	if (!menu_screen_open_ready("add_player"))
		return FALSE;
	result = lobby_join_start(widget, event, controller, &widget_deleted);
	*deleted = widget_deleted;
	return result;
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
	short mcc_index = mcc_maps_find(map_name);

	/* (an MCC map's picture and name, by its display index: mcc_maps.c) */
	if (mcc_index != NONE)
	{
		visible_set(named(description, "lobby_map_pic", 0), TRUE);
		visible_set(named(description, "lobby_map_name", 0), TRUE);
		visible_set(named(description, "replay_level_right_pic", 0), FALSE);
		visible_set(named(description, "replay_level_right_name", 0), FALSE);
		if ((widget = named(description, "lobby_map_pic", 0)) != NULL)
			widget->animation.current_frame_index = MCC_MAPS_FIRST_DISPLAY_INDEX + mcc_index;
		if ((widget = named(description, "lobby_map_name", 0)) != NULL)
			widget->parameters.text_box.string_list_index = MCC_MAPS_FIRST_DISPLAY_INDEX + mcc_index;
		return;
	}

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
static boolean preview_add(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
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
	/* (the new menus: Add Player's screen, as the lobby's) */
	if (add_player_open(widget, event, controller, widget_deleted))
		return TRUE;
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

/* ---- Add Player (screens/add_player.xml: menu_screen.c and menu_table.h),
the new menus' in place of the lobby's and the preview's ADD PLAYER and
START of another controller, as the Xbox's Select Profile of four (its
4way_start2join_screen) had it: a row for each of this machine's four
controllers. A controller not in the game joins with A or START (one at a
time or together), chooses its own profile with left and right, and is
ready with A; B goes back a step. Once every one joining is ready, A or
START of one of them (or DONE, or a player's START) brings them all into
the game, as the classic screen's choice did (the lobby's "net
splitscreen prejoin players" adds them), and goes back. The players in the
game keep the screen's focus, its B and its BACK; a controller joining
takes none of it. One gamepad sharing player 1's controller with the
keyboard has its own while the screen is open (lobby_join.adding:
pc_menu_split_players), and joins as the second */

enum
{
	ADD_PLAYER_EMPTY,
	ADD_PLAYER_CHOOSING,
	ADD_PLAYER_READY
};

static struct
{
	/* each controller's: ADD_PLAYER_*, and the profile it chose (its
	index, NONE for none) */
	short state[MAXIMUM_LOCAL_PLAYERS];
	long profile[MAXIMUM_LOCAL_PLAYERS];
	/* the controller that pressed last, whose row the panel shows (NONE:
	the focused row's), and whether the table's focus goes to it */
	short acted;
	boolean focus;
	short chosen;
} add_player;

/* whether the controller's player is in the game (or joining it from the
preview): theirs is the screen's focus, not a row of their own to choose */
static boolean add_player_in_game(short controller)
{
	return lobby_controller_playing(controller);
}

static short add_player_joining(void)
{
	short controller, count = 0;

	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
		count += add_player.state[controller] != ADD_PLAYER_EMPTY && !add_player_in_game(controller);
	return count;
}

/* how many more may join: this machine's four, and the game's room (the
lobby's players, or a game under way's as its preview has them) */
static short add_player_room(void)
{
	void *client = global_network_game_client_get();
	struct network_game *game = client ? network_game_client_get_game(client) : NULL;
	short state_data, local = 0, controller, room;

	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
		local += add_player_in_game(controller);
	room = (short)(MAXIMUM_LOCAL_PLAYERS - local - add_player_joining());
	if (game && network_game_client_get_state(client, &state_data) == _client_state_pregame)
		room = (short)MIN(room, game->maximum_players - lobby_player_count - add_player_joining());
	else
	{
		struct advertised_game *preview = preview_game();

		if (!preview || !preview->open)
			return 0;
		room = (short)MIN(room, preview->maximum_player_count - preview->player_count - local - add_player_joining());
	}
	return (short)MAX(room, 0);
}

/* a profile's place in the list (NONE: not there) */
static short add_player_place(long profile_index)
{
	short index;

	for (index = 0; index < profile_list.count; index++)
	{
		if (profile_list.indices[index] == profile_index)
			return index;
	}
	return NONE;
}

/* whether another of this machine's players has the profile (in the game,
or joining with it) */
static boolean add_player_taken(short controller, long profile_index)
{
	short other;

	for (other = 0; other < MAXIMUM_LOCAL_PLAYERS; other++)
	{
		if (other == controller)
			continue;
		if (add_player_in_game(other) && player_ui_get_active_player_profile_index(other) == profile_index)
			return TRUE;
		if (!add_player_in_game(other) && add_player.state[other] != ADD_PLAYER_EMPTY &&
			add_player.profile[other] == profile_index)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* a joining controller's first profile: its own (the one it had), else the
first no other player of this machine has, as the classic list's */
static long add_player_first_profile(short controller)
{
	long own = player_ui_get_active_player_profile_index(controller);
	short index;

	if (own != NONE && add_player_place(own) != NONE && !add_player_taken(controller, own))
		return own;
	for (index = 0; index < profile_list.count; index++)
	{
		if (!add_player_taken(controller, profile_list.indices[index]))
			return profile_list.indices[index];
	}
	return profile_list.count ? profile_list.indices[0] : NONE;
}

static void add_player_reset(void)
{
	short controller;

	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
	{
		add_player.state[controller] = ADD_PLAYER_EMPTY;
		add_player.profile[controller] = NONE;
	}
	add_player.acted = NONE;
	add_player.focus = FALSE;
	add_player.chosen = 0;
}

/* the controller's row the panel shows, and the focus moved there */
static void add_player_act(short controller)
{
	add_player.acted = controller;
	add_player.focus = TRUE;
}

/* a controller not in the game joins: its row choosing, on its first
profile (FALSE: no room, or no profile to choose) */
static boolean add_player_join(short controller)
{
	if (controller < 0 || controller >= MAXIMUM_LOCAL_PLAYERS || add_player_in_game(controller))
		return FALSE;
	if (add_player.state[controller] != ADD_PLAYER_EMPTY)
		return TRUE;
	profile_list_read(FALSE);
	add_player_act(controller);
	if (add_player_room() <= 0)
	{
		display_error_text_deferred(L"The game is full: there's\r\nno room for another\r\nplayer.", NONE);
		return campaign_fail();
	}
	add_player.profile[controller] = add_player_first_profile(controller);
	if (add_player.profile[controller] == NONE)
	{
		display_error_text_deferred(L"Make a player profile\r\nfirst (Settings, MANAGE\r\nPROFILES).", NONE);
		return campaign_fail();
	}
	add_player.state[controller] = ADD_PLAYER_CHOOSING;
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* a choosing controller's profile stepped (by one, either way, round from
the last to the first) */
static void add_player_step(short controller, short step)
{
	short place;

	profile_list_read(FALSE);
	if (!profile_list.count)
		return;
	place = add_player_place(add_player.profile[controller]);
	place = (short)(((place == NONE ? 0 : place + step) + profile_list.count) % profile_list.count);
	add_player.profile[controller] = profile_list.indices[place];
	add_player_act(controller);
	ui_play_audio_feedback_sound(SOUND_CURSOR);
}

/* whether every controller joining is ready (and one is) */
static boolean add_player_all_ready(void)
{
	short controller, ready = 0;

	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
	{
		if (add_player_in_game(controller))
			continue;
		if (add_player.state[controller] == ADD_PLAYER_CHOOSING)
			return FALSE;
		ready += add_player.state[controller] == ADD_PLAYER_READY;
	}
	return ready > 0;
}

/* the ready controllers brought into the game, each on its profile (the
lobby's "net splitscreen prejoin players" adds them; the preview's JOIN
GAME): FALSE while one still chooses, or one's profile cannot play (its
row back to choosing) */
static boolean add_player_finish(void)
{
	short controller;

	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
	{
		if (add_player.state[controller] == ADD_PLAYER_CHOOSING && !add_player_in_game(controller))
		{
			add_player_act(controller);
			return campaign_fail();
		}
	}
	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
	{
		if (add_player.state[controller] != ADD_PLAYER_READY || add_player_in_game(controller))
			continue;
		if (!ui_widget_port_multiplayer_player(controller, add_player.profile[controller]))
		{
			add_player.state[controller] = ADD_PLAYER_CHOOSING;
			add_player_act(controller);
			return FALSE;
		}
	}
	add_player_reset();
	ui_play_audio_feedback_sound(SOUND_FORWARD);
	return TRUE;
}

/* a controller's key on the screen (MENU_SCREEN_KEY_*): MENU_SCREEN_KEY_FREE
for a player in the game's (the screen's focus and its own keys take it),
else taken (MENU_SCREEN_KEY_TAKEN), or taken and the screen done
(MENU_SCREEN_KEY_DONE: the players brought in, back to the screen before).
A controller not in the game: A or START joins; choosing, left and right
step its profile, A or START is ready, B leaves; ready, A or START brings
everyone in once all are, B chooses again. Its other keys do nothing, nor
move the screen's focus */
int pc_menu_add_player_key(short controller, int key)
{
	short *state;

	if (controller < 0 || controller >= MAXIMUM_LOCAL_PLAYERS || add_player_in_game(controller))
		return MENU_SCREEN_KEY_FREE;
	state = &add_player.state[controller];
	switch (*state)
	{
	case ADD_PLAYER_EMPTY:
		if (key == MENU_SCREEN_KEY_A || key == MENU_SCREEN_KEY_START)
			add_player_join(controller);
		break;
	case ADD_PLAYER_CHOOSING:
		if (key == MENU_SCREEN_KEY_LEFT || key == MENU_SCREEN_KEY_RIGHT)
			add_player_step(controller, (short)(key == MENU_SCREEN_KEY_LEFT ? -1 : 1));
		else if (key == MENU_SCREEN_KEY_A || key == MENU_SCREEN_KEY_START)
		{
			*state = ADD_PLAYER_READY;
			add_player_act(controller);
			ui_play_audio_feedback_sound(SOUND_FORWARD);
		}
		else if (key == MENU_SCREEN_KEY_B)
		{
			*state = ADD_PLAYER_EMPTY;
			add_player.profile[controller] = NONE;
			add_player_act(controller);
			ui_play_audio_feedback_sound(SOUND_BACK);
		}
		break;
	case ADD_PLAYER_READY:
		if ((key == MENU_SCREEN_KEY_A || key == MENU_SCREEN_KEY_START) && add_player_all_ready())
			return add_player_finish() ? MENU_SCREEN_KEY_DONE : MENU_SCREEN_KEY_TAKEN;
		if (key == MENU_SCREEN_KEY_A || key == MENU_SCREEN_KEY_START)
		{
			/* (another still choosing: theirs shown) */
			add_player_finish();
		}
		else if (key == MENU_SCREEN_KEY_B)
		{
			*state = ADD_PLAYER_CHOOSING;
			add_player_act(controller);
			ui_play_audio_feedback_sound(SOUND_BACK);
		}
		break;
	}
	return MENU_SCREEN_KEY_TAKEN;
}

/* "add player done" (DONE, a player's START: TRUE, back to the screen
before) and "add player back" (B, BACK: those joining let go of, TRUE) */
int pc_menu_add_player_action(char const *name)
{
	if (!strcmp(name, "add player back"))
	{
		add_player_reset();
		return TRUE;
	}
	if (!strcmp(name, "add player done"))
	{
		/* (no one joining: back, as BACK) */
		if (!add_player_joining())
		{
			add_player_reset();
			return TRUE;
		}
		return add_player_finish();
	}
	return FALSE;
}

/* the screen opened (from the lobby or the preview): no one joining yet
but the controller given (NONE: none), whose A or START opened it; one
gamepad has its own controller while it is open (lobby_join.adding) */
static void add_player_begin(short controller)
{
	add_player_reset();
	lobby_join.adding = TRUE;
	lobby_join.controller = NONE;
	profile_list_read(TRUE);
	if (controller != NONE)
		add_player_join(controller);
}

/* ADD PLAYER, or a controller not in the game's START: the screen opened,
in the new menus (FALSE: not open, the classic way's then) */
static boolean add_player_open(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
{
	int deleted = FALSE;
	boolean opened;

	if (!menu_screen_open_ready("add_player"))
		return FALSE;
	add_player_begin(controller);
	opened = menu_screen_open_named(widget, event, "add_player", &deleted) != 0;
	*widget_deleted = deleted != 0;
	return opened;
}

/* the profile of a controller's row: the profile it chose, a player's own */
static boolean add_player_profile(short controller, struct player_profile *profile)
{
	if (add_player_in_game(controller))
	{
		if (player_ui_get_active_player_profile_index(controller) == NONE)
			return FALSE;
		player_ui_get_active_player_profile(controller, profile);
		return TRUE;
	}
	return add_player.state[controller] != ADD_PLAYER_EMPTY && add_player.profile[controller] != NONE &&
		player_profile_get(add_player.profile[controller], profile);
}

static void add_player_source_update(struct menu_table_view *view)
{
	profile_list_read(FALSE);
	view->count = MAXIMUM_LOCAL_PLAYERS;
	view->first = 0;
	/* (the controller that pressed last: its row focused and shown, else
	the focused row's) */
	if (add_player.focus && add_player.acted != NONE && add_player.acted < view->shown)
	{
		view->focus = add_player.acted;
		add_player.chosen = add_player.acted;
	}
	else if (view->focused != NONE && view->focused >= 0 && view->focused < MAXIMUM_LOCAL_PLAYERS)
		add_player.chosen = view->focused;
	add_player.focus = FALSE;
	view->chosen = add_player.chosen;
}

static int add_player_source_heading(short index, unsigned short *words, short size)
{
	(void)index;
	(void)words;
	(void)size;
	return FALSE;
}

/* a row's words: its controller's state, words for each (the four the
same as the Xbox's: Press A to Join, the profile chosen, Profile Selected) */
static void add_player_words(short controller, char const *column, wchar_t *text, short size)
{
	struct player_profile profile;
	boolean pad = controller == 0 || input_has_gamepad(controller);

	text[0] = 0;
	if (!strcmp(column, "controller"))
		usnprintf(text, size - 1, L"P%d", controller + 1);
	else if (!strcmp(column, "profile"))
	{
		struct network_player *player = add_player_in_game(controller) ? lobby_local_player(controller) : NULL;

		if (add_player_profile(controller, &profile))
			settings_wide_copy(text, profile.player_name, (short)MIN(size, MAXIMUM_PLAYER_PROFILE_NAME_LENGTH + 1));
		/* (a player in the game with no profile of this machine's: its name
		there) */
		else if (player)
		{
			ustrncpy(text, player->name, (short)MIN(size - 1, (short)NUMBEROF(player->name)));
			text[MIN(size - 1, (short)NUMBEROF(player->name))] = 0;
		}
		else
			settings_wide_copy(text, add_player_in_game(controller) ? L"" : pad ? L"Press A to join" :
				L"No controller", size);
	}
	else if (!strcmp(column, "state"))
	{
		settings_wide_copy(text, add_player_in_game(controller) ? L"IN THE GAME" :
			add_player.state[controller] == ADD_PLAYER_CHOOSING ? L"CHOOSING" :
			add_player.state[controller] == ADD_PLAYER_READY ? L"READY" : L"", size);
	}
	text[size - 1] = 0;
}

static void add_player_source_cell(short index, char const *column, int focused, struct menu_table_cell *cell)
{
	wchar_t text[MENU_TABLE_TEXT_LENGTH];
	struct player_profile profile;

	(void)focused;
	cell->frame = NONE;
	if (index < 0 || index >= MAXIMUM_LOCAL_PLAYERS)
		return;
	if (!strcmp(column, "color"))
	{
		if (add_player_profile(index, &profile))
		{
			cell->frame = (short)(profile.primary_color_index >= 0 && profile.primary_color_index < COLOR_COUNT ?
				profile.primary_color_index : COLOR_COUNT);
		}
		return;
	}
	add_player_words(index, column, text, NUMBEROF(text));
	settings_wide_copy((wchar_t *)cell->text, text, MENU_TABLE_TEXT_LENGTH);
	/* (a row with no controller dimmed; READY in the blocks' green) */
	if (!strcmp(column, "profile") && !add_player_profile(index, &profile))
		cell->color = index == 0 || input_has_gamepad(index) ? 0x8DB6E6FFUL : 0x6F9CD080UL;
	if (!strcmp(column, "state") && add_player.state[index] == ADD_PLAYER_READY && !add_player_in_game(index))
		cell->color = 0x7CD67CFFUL;
}

/* (this machine's players, in the game or joining, framed as its own) */
static int add_player_source_state(short index)
{
	return add_player_in_game(index) || add_player.state[index] != ADD_PLAYER_EMPTY ? MENU_TABLE_ROW_MARKED :
		MENU_TABLE_ROW_PLAIN;
}

struct menu_table_source const pc_menu_add_player_source =
{
	add_player_source_update, add_player_source_heading, add_player_source_cell, NULL, add_player_source_state,
};

/* an add.* binding's value (UTF-8; nonzero if it has one): how many more
may join, the chosen row's player, profile, color and what its controller
does next, and whether DONE brings them in */
int pc_menu_add_player_value(char const *name, char *value, unsigned long size)
{
	short controller = (short)PIN(add_player.chosen, 0, MAXIMUM_LOCAL_PLAYERS - 1);
	struct player_profile profile;
	boolean has = add_player_profile(controller, &profile);
	boolean pad = controller == 0 || input_has_gamepad(controller);
	wchar_t text[ROW_TEXT_LENGTH];

	value[0] = 0;
	text[0] = 0;
	if (!strcmp(name, "add.count"))
	{
		short room = add_player_room();

		snprintf(value, size, room == 1 ? "1 MORE CAN JOIN" : room ? "%d MORE CAN JOIN" : "NO ROOM FOR MORE", room);
	}
	else if (!strcmp(name, "add.title"))
		snprintf(value, size, "PLAYER %d", controller + 1);
	else if (!strcmp(name, "add.profile"))
	{
		if (has || add_player_in_game(controller))
			add_player_words(controller, "profile", text, NUMBEROF(text));
	}
	else if (!strcmp(name, "add.color"))
	{
		if (has && profile.primary_color_index >= 0 && profile.primary_color_index < COLOR_COUNT)
			snprintf(value, size, "%d", profile.primary_color_index);
		else if (has)
			snprintf(value, size, "random");
	}
	else if (!strcmp(name, "add.help"))
	{
		snprintf(value, size, "%s", add_player_in_game(controller) ?
			"In the game. Another controller: press A or START on it to join." :
			add_player.state[controller] == ADD_PLAYER_CHOOSING ?
			"Left and right: another profile. A: ready. B: leave." :
			add_player.state[controller] == ADD_PLAYER_READY ? (add_player_all_ready() ?
			"Ready. A or START adds the new players. B: choose again." : "Ready. Waiting for the others.") :
			pad ? "Press A or START on this controller to join." : "Connect a controller, then press A on it.");
	}
	else if (!strcmp(name, "add.done"))
	{
		if (add_player_all_ready())
			snprintf(value, size, "yes");
	}
	else
		return FALSE;
	text[NUMBEROF(text) - 1] = 0;
	if (text[0])
		text_utf8(text, value, (long)size);
	return value[0] != 0;
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
	_option_setting,	/* the host's own (config.toml): Server Setup's copy only, its setting and values the
				   model's (gametype_option_setting) */
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
	{ "voice_mode_spinner", _option_setting },
	{ "voice_lobby_spinner", _option_setting },
	{ "voice_kbps_spinner", _option_setting },
	{ "voice_proximity_spinner", _option_setting },
	/* (and the players' votes to kick: port/linux/game/network_votekick.c) */
	{ "votekick_spinner", _option_setting },
	/* (and the text chat: port/linux/game/network_chat.c) */
	{ "text_chat_spinner", _option_setting },
	/* (and the players' pings: port/linux/game/network_markers.c) */
	{ "pings_spinner", _option_setting },
	{ "aim_assist_spinner", _option_setting },
	/* (the shield effects every machine draws: network_shield_effects.c) */
	{ "shield_effects_spinner", _option_setting },
	/* co-op's options (Server Setup's CO-OP OPTIONS: tools/port_settings.py's
	COOP_SETUP_SCREENS; coop_enemies.c) */
	{ "coop_friendly_fire_spinner", _option_setting },
	{ "coop_team_wipe_spinner", _option_setting },
	{ "coop_extra_enemies_spinner", _option_setting },
	{ "coop_extra_pickups_spinner", _option_setting },
	{ "coop_extra_vehicles_spinner", _option_setting },
	{ "coop_enemies_per_player_spinner", _option_setting },
	{ "coop_enemies_multiplier_spinner", _option_setting },
	{ "coop_player_collisions_spinner", _option_setting },
	{ "coop_ally_friendly_fire_spinner", _option_setting },
	{ "coop_vehicle_riding_spinner", _option_setting },
	/* map sharing's (Server Setup's MAP SHARING: tools/port_settings.py's
	MAP_SHARE_SETUP; map_share.c) */
	{ "map_share_spinner", _option_setting },
	{ "map_share_upload_spinner", _option_setting },
	/* the vehicle options' VEHICLE RIDING, the gametype's (vehicle_riding.c) */
	{ "vehicle_riding_spinner", _option_flag, 0, FLAG(_game_variant_vehicle_riding_bit), 2, { 1, 0 } },
	/* vehicle options (the side's set and counts: vehicles_update) */
	{ "vehicles_respawn_spinner", _option_short, OPTIONS_FIELD(vehicle_respawn_time), 0, 7,
		{ 0, 30, 60, 90, 120, 180, 300 } },
};

/* an option of the host's own setting: its setting and values, its
spinner's node's in the menus' model (tools/port_settings.py's
SETUP_ROW_SETTINGS, model/nodes.xml); FALSE if the model has none */
static boolean gametype_option_setting(struct gametype_option const *option, struct pc_menu_setting *setting)
{
	struct halo_menu_model const *model = menu_engine_model();
	long index, choice;

	csmemset(setting, 0, sizeof(*setting));
	for (index = 0; model && index < model->row_count; index++)
	{
		struct halo_menu_model_row const *row = &model->rows[index];
		char const *leaf;

		if (strcmp(row->kind, "node") || !row->setting)
			continue;
		leaf = strrchr(row->key, '/');
		if (strcmp(leaf ? leaf + 1 : row->key, option->spinner))
			continue;
		setting->setting = row->setting;
		for (choice = 0; choice < row->choice_count && choice < MAXIMUM_STRINGS; choice++)
			setting->values[choice] = model->choices[row->first_choice + choice].value;
		setting->value_count = choice;
		return choice > 0;
	}
	return FALSE;
}

/* an option of the host's own setting: the value shown for it (the one
set, else the nearest) */
static short gametype_option_setting_index(struct gametype_option const *option)
{
	struct pc_menu_setting setting;
	char text[64];

	if (!gametype_option_setting(option, &setting) || !setting_text(setting.setting, text, sizeof(text), FALSE))
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
				struct pc_menu_setting setting;
				boolean found = gametype_option_setting(option, &setting);
				short index = (short)PIN(spinner->parameters.list.selected_index, 0, setting.value_count - 1);

				visible_set(row, gametype_edit.setup);
				if (!gametype_edit.setup || !found)
					continue;
				if (!save)
				{
					spinner->parameters.list.selected_index = gametype_option_setting_index(option);
				}
				else if (index != gametype_option_setting_index(option) &&
					!setting_write(setting.setting, setting.values[index]))
				{
					platform_log("menus: could not set %s", setting.setting);
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
			struct pc_menu_setting setting;
			short index;

			if (!option || option->kind != _option_setting || !gametype_option_setting(option, &setting))
				continue;
			index = (short)PIN(spinner->parameters.list.selected_index, 0, setting.value_count - 1);
			if (!save)
			{
				spinner->parameters.list.selected_index = gametype_option_setting_index(option);
			}
			else if (index != gametype_option_setting_index(option) &&
				!setting_write(setting.setting, setting.values[index]))
			{
				platform_log("menus: could not set %s", setting.setting);
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
			if ((widget = named(description, "gametype_right_pic", 0)) != NULL)
				widget->animation.current_frame_index = gametype_picture_frame(variant.game_engine_index);
			gametype_rules_text(profile_index, text, NUMBEROF(text));
			text_set(named(description, "gametype_right_data", 0), text);
		}
	}
	profile_name_show(description);
}

/* "port playlist edit desc" (after the PC version's "playlist settings menu
update desc"): the gametype editor's GOAT H1 OPTIONS, its eighth row, has
its own words (the list's ninth), and the buttons after it SAVE CHANGES';
the picture, on every row, is the gametype's own (Game Options' pictures:
tools/port_settings.py's WIDGET_PATCHES), none for an unknown engine */
#define PLAYLIST_EDIT_OPENCE_ROW 7
#define PLAYLIST_EDIT_OPENCE_WORDS 8

static void playlist_edit_description(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct widget_instance *picture;
	struct game_variant *variant = edit_variant();
	short index = list->focused_child ? ui_widget_port_list_index(list) : NONE;
	short frame;

	if (!description || !description->child || !description->child->next)
		return;
	picture = description->child->next;
	frame = variant ? gametype_picture_frame(variant->game_engine_index) : _gametype_picture_unknown;
	visible_set(picture, frame != _gametype_picture_unknown);
	picture->animation.current_frame_index = frame == _gametype_picture_unknown ? 0 : frame;
	if (index < PLAYLIST_EDIT_OPENCE_ROW)
		return;
	description->child->parameters.text_box.string_list_index =
		index == PLAYLIST_EDIT_OPENCE_ROW ? PLAYLIST_EDIT_OPENCE_WORDS : (short)(index - 1);
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

	/* (the rows shown, by their conditions in the model, SETUP_NODE_WHEN:
	Item Options' weapon set's row for a category loadout, the two weapons'
	for a custom one; OPENCE OPTIONS' mode's own rows, Infection's or Gun
	Game's; CO-OP OPTIONS' PER PLAYER and MULTIPLIER amounts, by the extras) */
	setup_nodes_conditions(list);
	/* (OPENCE OPTIONS: the mode's rows below MODE and HARDCORE) */
	if (named(list, "oce_mode_spinner", 0))
	{
		short place = 73 + 2 * 30;

		for (row = list->child; row; row = row->next)
		{
			if (strncmp(row->name, "op_inf_", 7) && strncmp(row->name, "op_gg_", 6))
				continue;
			if (row->visible)
			{
				row->vertical_offset = place;
				place += 30;
			}
		}
	}
	/* (CO-OP OPTIONS: enemies, pickups and vehicles share the amount rows,
	mixed modes both at once) */
	if (named(list, "coop_extra_enemies_spinner", 0))
	{
		/* Hidden amounts must not reserve a blank row. Offsets are relative
		to the parent: moving only the row carries its labels and spinners
		with it when widget_instance_render_recursive draws and notes targets. */
		{
			short y = 73;
			short visible = 0;
			short spacing = 30;

			for (row = list->child; row; row = row->next)
				if (row->visible && !strncmp(row->name, "op_coop_", 8))
					visible++;
			if (visible > 1)
				spacing = MIN(30, (341 - 1 - 73 - 28) / (visible - 1));

			for (row = list->child; row; row = row->next)
				if (row->visible && !strncmp(row->name, "op_coop_", 8))
				{
					row->vertical_offset = y;
					y += spacing;
				}
		}
	}
	/* (the server browser's filters, hidden: their helps are fewer than
	their values, filters_help) */
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

/* "port gametype select desc" (after the PC version's "gametype select menu
update desc"): Step 1's BACK, the list's sixth past its five types, shows the
gametype's own type's words and picture, not the PC version's sixth (its
SAVE CHANGES question's picture, of an item this list has not, which looked
like a question that BACK asked) */
static void gametype_select_description(struct widget_instance *list)
{
	struct widget_instance *description = list->parameters.list.extended_description;
	struct game_variant *variant = edit_variant();
	short index = list->focused_child ? ui_widget_port_list_index(list) : NONE;
	short item;

	if (!description || !description->child || !description->child->next)
		return;
	description->child->next->visible = TRUE;
	if (index < (short)NUMBEROF(engine_items))
		return;
	for (item = 0; variant && item < (short)NUMBEROF(engine_items); item++)
	{
		if (engine_of_item[item] == variant->game_engine_index)
		{
			description->child->parameters.text_box.string_list_index = item;
			description->child->next->animation.current_frame_index = item;
			return;
		}
	}
	/* (a gametype of no type: no picture) */
	description->child->next->visible = FALSE;
}

/* ---- leaving an editor with changes not saved (the gametype editor's B,
Back and CANCEL): the hand-written question, screens/save_changes.xml */

struct widget_instance *ui_widget_port_active_screen(short local_player_index);

/* "port edit leave" (B on a screen that edits a gametype or a profile, as
its CANCEL presses B; its handler opens the question where this succeeds):
with nothing changed, back to the screen before, as B went, the question not
asked; with changes, TRUE, the screen kept in the history for the question's
CANCEL */
static boolean edit_leave_ask(struct widget_instance *widget, boolean *widget_deleted)
{
	if (player_ui_edit_profile_is_dirty())
	{
		platform_log("menus: leaving the editor with changes not saved: asked");
		return TRUE;
	}
	ui_play_audio_feedback_sound(SOUND_BACK);
	ui_widget_port_go_back(widget);
	*widget_deleted = TRUE;
	return TRUE;
}

/* the question's SAVE and DON'T SAVE (menu_screen.c's "edit save" and "edit
discard"), on the editor it went back to: the changes saved as the editor's
OK saves them (a built-in gametype's new name asked first, the editor kept
open for its OK), or let go of, and the editor closed as its OK closes it */
boolean pc_menu_edit_leave(short local_player_index, boolean save)
{
	struct widget_instance *screen = ui_widget_port_active_screen(local_player_index);
	boolean profile = player_ui_get_edit_player_profile() != NULL;
	boolean deleted = FALSE;
	boolean done;

	if (!screen)
		return FALSE;
	if (!save)
		player_ui_end_editing_profile();
	done = profile ? profile_save_changes(screen, &deleted) : ui_widget_port_gametype_save(screen, &deleted);
	platform_log("menus: leaving the editor: %s", !save ? "its changes let go of" : done ? "its changes saved" :
		"a new name asked first");
	return done;
}

/* ---------- the party: Multiplayer's PARTY and its screen */

#include "party_menu.inc"
#include "matchmaking_menu.inc"
#include "friends_menu.inc"
#include "doubles_browser.inc"
#include "lobby_menu.inc"
/* (the campaign's screens: the Campaign menu, Choose Mission, Choose Difficulty) */
#include "campaign_menu.inc"
/* (co-op's screens, the new engine's: HOST, JOIN, the lobby's WHO CAN JOIN) */
#include "coop_menu.inc"

/* ---------- the hand-written screens' (menu_screen.c) */

/* text in UTF-8 (the fonts' 16-bit characters) */
static void text_utf8(wchar_t const *text, char *out, long size)
{
	long used = 0;

	for (; text && *text && used < size - 1; text++)
	{
		unsigned long code = (unsigned short)*text;

		if (code < 0x80)
		{
			out[used++] = (char)code;
		}
		else if (code < 0x800)
		{
			if (used + 2 >= size)
				break;
			out[used++] = (char)(0xC0 | (code >> 6));
			out[used++] = (char)(0x80 | (code & 0x3F));
		}
		else
		{
			if (used + 3 >= size)
				break;
			out[used++] = (char)(0xE0 | (code >> 12));
			out[used++] = (char)(0x80 | ((code >> 6) & 0x3F));
			out[used++] = (char)(0x80 | (code & 0x3F));
		}
	}
	out[used] = 0;
}

/* the saved game player 1's profile goes on with (the main menu's CAMPAIGN,
"Continue · Halo · Heroic"): its mission's name and its difficulty's, in
UTF-8; FALSE if there is none (no profile chosen, none saved, or co-op's
two players, which go on with none) */
boolean pc_menu_campaign_saved(char *level, char *difficulty, long size)
{
	char const *map_name;
	short saved_level, saved_difficulty;
	wchar_t text[ROW_TEXT_LENGTH];

	if (player_spawn_count >= 2 || player_ui_get_active_player_profile_index(0) == NONE ||
		!ui_widget_port_saved_game(&map_name, &saved_level, &saved_difficulty))
	{
		return FALSE;
	}
	campaign_level_title(saved_level, TRUE, text, ROW_TEXT_LENGTH);
	text_utf8(text, level, size);
	campaign_difficulty_name(saved_difficulty, text, ROW_TEXT_LENGTH);
	text_utf8(text, difficulty, size);
	return level[0] != 0;
}

/* ---------- Host Controls: the pause menu's HOST CONTROLS, the host's */

#include "host_controls_menu.inc"

/* ---------- Settings' search, the menu engine's */

#include "settings_search_menu.inc"

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
	short search_typed;

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
		/* (co-op's HOST: Choose Difficulty's PLAY goes on to WHO CAN JOIN;
		the co-op list's screen made and let go of: coop_menu.inc) */
		if (!strcmp(name, "port campaign play") && coop_host_choosing())
			return coop_host_difficulty(widget, widget_deleted);
		if (!strcmp(name, "port coop host begin") || !strcmp(name, "port coop host end"))
			return coop_host_begin(!strcmp(name, "port coop host begin"));
		if (!strcmp(name, "port coop join init"))
			return coop_join_initialize(widget, event, widget_deleted);
		if (!strcmp(name, "port coop join dispose"))
		{
			coop_join_dispose();
			return TRUE;
		}
		if (!strncmp(name, "port campaign ", 14))
			return campaign_menu_event(widget, name + 14, controller);
		if (!strcmp(name, "main menu quit game"))
		{
			platform_request_quit();
		}
		/* (a hand-written screen's item: menu_screen.c) */
		else if (!strcmp(name, "port screen do"))
		{
			return menu_screen_do(widget, event, widget_deleted);
		}
		else if (!strcmp(name, "port screen key"))
		{
			return menu_screen_key(widget, event, widget_deleted, controller);
		}
		else if (!strcmp(name, "port screen left") || !strcmp(name, "port screen right"))
		{
			return menu_screen_step(widget, event, widget_deleted, (short)(!strcmp(name, "port screen left") ? -1 : 1));
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
			boolean deleted;

			while (top->parent)
				top = top->parent;
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
					L"Couldn't start the game.\r\nClose any other copy of\r\nGOAT H1 or Halo, then try\r\nagain.",
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
		else if (!strcmp(name, "port browser search next"))
		{
			return browser_search_next(widget);
		}
		else if (!strcmp(name, "port browser search clear"))
		{
			return browser_search_clear_chosen(widget);
		}
		/* (Esc, B or Back while a search is typed into: its words cleared) */
		else if (!strcmp(name, "gamespy back handler") && browser_search_editing(&search_typed))
		{
			browser_search_clear(widget, search_typed);
			ui_play_audio_feedback_sound(SOUND_BACK);
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
		else if (!strcmp(name, "port edit leave"))
		{
			return edit_leave_ask(widget, widget_deleted);
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
		/* (the 2V2 browser: doubles_browser.inc) */
		else if (!strcmp(name, "port doubles init"))
		{
			return doubles_browser_initialize(widget, event, widget_deleted);
		}
		else if (!strcmp(name, "port doubles dispose"))
		{
			doubles_browser_dispose();
		}
		else if (!strcmp(name, "port doubles host"))
		{
			return doubles_browser_host(controller);
		}
		else if (!strcmp(name, "port doubles find"))
		{
			return doubles_browser_find(controller);
		}
		else if (!strcmp(name, "port doubles friend"))
		{
			return doubles_browser_friend(widget);
		}
		else if (!strcmp(name, "port doubles join") || !strcmp(name, "port doubles invite"))
		{
			return doubles_browser_friend_prompt(!strcmp(name, "port doubles join"));
		}
		else if (!strcmp(name, "port doubles x"))
		{
			return doubles_browser_x();
		}
		else if (!strcmp(name, "port pause end game"))
		{
			return pause_end_game(widget);
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
		else if (!strcmp(name, "port mcc host folder back"))
		{
			return mcc_host_back(widget, widget_deleted);
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
			pad_editor_defaults(screen_of(widget));
			controls_page_defaults(screen_of(widget));
		}
		else if (!strncmp(name, "port slider ", 12))
		{
			return settings_slider_function(name + 12, widget);
		}
		else if (!strcmp(name, "port setting segment"))
		{
			return settings_segment_pick(widget);
		}
        else if (!strcmp(name,"port mcc share closed"))
            mcc_share_game_prompt_closed();
        else if (!strcmp(name,"port mcc downloads open")) mcc_share_menu_open();
        else if (!strcmp(name,"port mcc downloads previous")) mcc_share_menu_step(-1);
        else if (!strcmp(name,"port mcc downloads next")) mcc_share_menu_step(1);
        else if (!strcmp(name,"port mcc downloads remove")) mcc_share_menu_remove();
        else if (!strcmp(name,"port mcc share download") || !strcmp(name,"port mcc share workshop") ||
            !strcmp(name,"port mcc share cancel") || !strcmp(name,"port mcc share browser")) {
            int action=!strcmp(name,"port mcc share download") ? 0 : !strcmp(name,"port mcc share workshop") ? 1 : !strcmp(name,"port mcc share browser") ? 3 : 2;
            *widget_deleted=TRUE;
            mcc_share_game_prompt_action(action);
            return TRUE;
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
			/* (the pause menu's CHAPTERS: its screen opened by the button,
			the host's first local player's alone) */
			if (!host_rows_player(widget))
			{
				ui_play_audio_feedback_sound(SOUND_ERROR);
				return FALSE;
			}
		}
		else if (!strcmp(name, "port update now"))
		{
			updater_update_now();
		}
		else if (!strcmp(name, "port crash report") || !strcmp(name, "port crash dismiss"))
		{
			/* (the crash question's: report.c) */
			report_crash_answer(!strcmp(name, "port crash report"));
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
			return matchmaking_screen_button(widget, controller, widget_deleted);
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
			return host_controls_open(widget);
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
			return lobby_board_start(controller);
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
			return lobby_add_player(widget, event, widget_deleted);
		}
		else if (!strcmp(name, "port lobby join"))
		{
			return lobby_join_start(widget, event, controller, widget_deleted);
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
			return preview_add(widget, event, controller, widget_deleted);
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
			if (map_kind_mcc(level_list.kind) && mcc_solo_folder_choose(widget))
				return FALSE;
			return level_choose(widget, controller);
		}
		else if (!strcmp(name, "port mcc solo folder back"))
		{
			return mcc_solo_back(widget, widget_deleted);
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
			/* (the browser left: its searches forgotten) */
			if (!strcmp(name, "gamespy back handler"))
				browser_searches_forget();
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
	else if (!strcmp(name, "port doubles update"))
		doubles_browser_update(widget);
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
    else if (!strcmp(name,"port mcc share text")) {
        wchar_t text[512];
        mcc_share_game_prompt_text(text);
        text_set_length(widget,text,512);
    }
    else if (!strcmp(name,"port mcc downloads text")) {
        wchar_t text[512];
        mcc_share_menu_text(text);
        text_set_length(widget,text,512);
    }
    else if (!strcmp(name,"port mcc downloads update")) {
        struct widget_instance *remove=named(widget,"remove",0);
        if (remove) {
            remove->visible=mcc_share_menu_can_remove();
            text_set(remove,mcc_share_menu_confirming() ? L"CONFIRM REMOVAL" : L"REMOVE SELECTED");
        }
        if (!widget->focused_child || !widget->focused_child->visible ||
            widget->parameters.list.selected_index<1 || widget->parameters.list.selected_index>4 ||
            (widget->parameters.list.selected_index==3 && !mcc_share_menu_can_remove()))
            focus_row(widget,4);
    }
    else if (!strcmp(name,"port mcc share update")) {
        struct widget_instance *download=named(widget,"download",0);
        struct widget_instance *workshop=named(widget,"workshop",0);
        struct widget_instance *browser=named(widget,"browser",0);
        boolean steam=mcc_share_game_has_workshop();
#ifdef HALO_ANDROID
        steam=FALSE;
#endif
        if (download) download->visible=mcc_share_game_can_download();
        if (workshop) workshop->visible=steam;
        if (browser) browser->visible=mcc_share_game_has_workshop();
        if (widget->parameters.list.selected_index<1 ||
            (widget->parameters.list.selected_index==1 && !mcc_share_game_can_download()) ||
            (widget->parameters.list.selected_index==2 && !steam) ||
            (widget->parameters.list.selected_index==3 && !mcc_share_game_has_workshop()))
            focus_row(widget,mcc_share_game_can_download() ? 1 : steam ? 2 : mcc_share_game_has_workshop() ? 3 : 4);
    }
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
	else if (!strcmp(name, "port settings profile"))
		settings_profile_text(widget);
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
	else if (!strcmp(name, "port gametype select desc"))
		gametype_select_description(widget);
}
