/*
MENU_TAGS.C

The menus written in XML (halo_menus.h, read by port/linux/src/menu_files.c),
built into the tags the game's widgets are made of when ui.map has loaded
(scenario_tags_load), and added to its tag table: a widget definition
('DeLa') for each <widget>, a unicode string list ('ustr') for each
<strings> and each widget's own text, and a bitmap group ('bitm') for each
<bitmap>, whose textures are drawn from their PNGs (menu_art_texture,
xbox_textures.c). The game's widgets then run them as their own:
main_screen_shell_load opens pc_menus_root_name()'s.

Each name the files use is looked up here: our widgets (named pc\<name>),
the map's tags (a path, with its backslashes), the game's event handler and
game data functions, and the port's own (menu_functions.c, from
PC_MENU_FUNCTION_BASE), which include the PC version's (most of which do
nothing yet). Its mouse events are kept, though this engine never sends
them (the port's mouse support clicks with A). Anything that does not resolve, or that the widget
code would stop the game for (a spinner's children, a list from strings
with children), is logged with its file and line and nothing is added: the
game keeps its own menus.

The tag table cannot grow where it is (the tags' names follow it), so it
is copied, with ours after it; every existing tag keeps its index. All of it
is let go in scenario_tags_unload, before the next map's tags load.

A multiplayer map gets them too, for its in-game pause menu's SETTINGS
(pause_patch): there the map's tags they name that it has not (ui.map's
pictures, sounds and screens) are none, a missing picture drawn blank, and
no screen pauses the game (a network game's clock runs on).

So does a single-player campaign's map (pause_campaign_patch), whose screens
pause it as its pause menu does, and a network co-op game's, whose pause
menu gets FRIENDLY FIRE too.

Under display.menu_engine "new" the menu engine draws the widgets of the
menus' files from their looks in the model instead ("the menu engine's
looks", below).
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "tag_files/tag_groups.h"
#include "text/font_group.h"
#include "text/text_group.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"

#include "halo_menus.h"
#include "menu_engine.h"
#include "mcc_ui.h"
#include "mcc_cache.h"
#include "mcc_menu_preview.h"
#include "custom_edition_cache.h"
#include "network_admin.h"
#include "network_host_controls.h"
#include "basic_training.h"
#include "halo_keyboard.h"
#include "menu_models.h"
#include "menu_view.h"

#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src) */
void platform_log(char const *format, ...);
char const *config_string(char const *name);
int config_boolean(char const *name);
unsigned long config_changes(void);
int platform_display_resolutions(long *widths, long *heights, int maximum);
int platform_display_refresh_rates(long *rates, int maximum);
int platform_window_sizes(long *widths, long *heights, int maximum);
/* (sdl_platform.c's PLATFORM_AUDIO_DEVICE_NAME_SIZE) */
int platform_audio_devices(int recording, char (*names)[128], int maximum);
/* (sdl_platform.h's PLATFORM_VRR_*) */
int platform_vrr_detected(void);

/* the game's network server (this machine hosts: network_game_globals.c) */
void *global_network_game_server_get(void);
boolean network_game_is_active(void);
boolean network_game_is_splitscreen_local(void);

/* cache_files.c's (port) */
void *cache_files_tag_instances(long *count);
void cache_files_set_tag_instances(void *instances, long count);

/* the game's (port) */
char *tag_get_name(long tag_index);
char const *ui_widget_event_handler_function_name(long function_index);

void menu_tags_loaded(char const *map_name);
void menu_tags_unloaded(void);
char const *pc_menus_root_name(void);
char const *pc_menu_function_name(long function_index);
char const *pc_menu_game_data_input_name(long function_index);
boolean pc_menu_tag(long tag_index);
boolean pc_menu_frame_placement(struct bitmap_data const *bitmap, short *x, short *y, short *width, short *height);
boolean pc_menu_scaled_frame_at(unsigned long address, long width, long height);
boolean pc_menu_map_frame_at(unsigned long address, long width, long height, long *tag_index, long *frame);
short pc_menu_widget_radius(long definition_index);
short pc_menu_legend_room(long definition_index);
void *pc_menu_look_definition(long tag_index);
/* (port/linux/game/menu_screen.c: a hand-written screen's building block's
text scale, at the wide layout's extra width) */
boolean menu_screen_text_scale(long definition_index, short wide_extra, real *scale);
struct widget_instance;
struct event_record;
void ui_widget_port_handler_run(struct widget_instance *widget, struct event_record *event, void *handler,
	boolean *deleted);
/* ui_widget.c: how much wider a legends' row is drawn while the keyboard
plays than the Xbox's pictures and words were */
short ui_widget_port_key_legend_growth(long definition_index);

/* ---------- constants */

#define PC_MENU_TAG_PREFIX "pc\\"
/* the multiplayer maps' widget collection, whose pause screens pause_patch
adds to */
#define MULTIPLAYER_COLLECTION "ui\\shell\\multiplayer"
/* the campaign's full screen pause screen, which pause_campaign_patch adds to */
#define CAMPAIGN_PAUSE_SCREEN "ui\\shell\\solo_game\\pause_game\\pause_game"
/* the campaign's split screen pause screen, which shares its box */
#define CAMPAIGN_PAUSE_SPLIT_SCREEN "ui\\shell\\solo_game\\pause_game\\pause_game_split_screen"
/* the tags pause_patch or pause_campaign_patch adds: three buttons at most
and their texts (SETTINGS; HOST CONTROLS, END GAME or CHAPTERS; network
co-op's FRIENDLY FIRE, and its host's HOST CONTROLS) */
#define PAUSE_PATCH_TAGS 6
/* what a frame of ui.map's is drawn as in a game map, which has not got it */
#define BLANK_PNG "ce/port/blank__0.png"
#define UI_WIDGET_DEFINITION_TAG 'DeLa'
#define UNICODE_STRING_LIST_TAG 'ustr'
#define FONT_TAG 'font'
#define SOUND_TAG 'snd!'
#define MAXIMUM_STRINGS 64
#define MAXIMUM_TEXT 2048
/* the size of a menu bitmap's D3D texture, which only stands for its PNG */
#define PLACEHOLDER_SIZE 4

enum
{
	_bitmap_type_2d = 0,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_has_power_of_two_dimensions_bit = 0,
};

enum
{
	_event_handler_close_current_widget_bit = 0,
	_event_handler_close_other_widget_bit,
	_event_handler_close_all_widgets_bit,
	_event_handler_open_widget_bit,
	_event_handler_reload_self_bit,
	_event_handler_reload_other_widget_bit,
	_event_handler_give_focus_to_widget_bit,
	_event_handler_run_function_bit,
	_event_handler_replace_self_with_widget_bit,
	_event_handler_go_back_to_previous_widget_bit,
	_event_handler_run_scenario_script_bit,
	_event_handler_try_to_branch_on_failure_bit,
};

enum
{
	_widget_type_container,
	_widget_type_text_box,
	_widget_type_spinner_list,
	_widget_type_column_list,
};

enum
{
	_list_items_generated_from_string_list_tag_bit = 1,
};

enum
{
	_child_widget_use_custom_controller_index_bit = 0,
};

/* ---------- structures */

/* the widget definition, as ui_widget.c has it */
struct ui_widget_event_handler_reference
{
	long flags;
	short event_type;
	short function;
	struct tag_reference widget_tag;
	struct tag_reference sound_effect;
	char script[32];
};

struct ui_widget_child_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	short vertical_offset;
	short horizontal_offset;
	byte unknown03A[0x50 - 0x3A];
};

struct ui_widget_conditional_reference
{
	struct tag_reference widget_tag;
	char name[32];
	long flags;
	short custom_controller_index;
	byte unknown036[0x50 - 0x36];
};

struct ui_widget_search_and_replace_reference
{
	char search_string[32];
	short replace_function;
};

struct ui_widget_game_data_input_reference
{
	short function;
	byte unknown002[0x24 - 0x02];
};

struct ui_widget_definition
{
	short type;
	short controller_index;
	char name[32];
	rectangle2d bounds;
	long flags;
	long milliseconds_to_auto_close;
	long auto_close_fade_time;
	struct tag_reference background_bitmap;
	struct tag_block game_data_inputs;
	struct tag_block event_handlers;
	struct tag_block search_and_replace_functions;
	byte unknown06C[0xEC - 0x6C];
	struct tag_reference text_label_string_list;
	struct tag_reference text_font;
	real_argb_color text_color;
	short justification;
	word text_box_flags;
	byte unknown120[0x12E - 0x120];
	short string_list_index;
	short horizontal_offset;
	short vertical_offset;
	byte unknown134[0x150 - 0x134];
	long list_flags;
	struct tag_reference list_header_bitmap;
	struct tag_reference list_footer_bitmap;
	rectangle2d list_header_bounds;
	rectangle2d list_footer_bounds;
	byte unknown184[0x1A4 - 0x184];
	struct tag_reference extended_description_widget;
	byte unknown1B4[0x2D4 - 0x1B4];
	struct tag_block conditional_widgets;
	byte unknown2E0[0x3E0 - 0x2E0];
	struct tag_block child_widgets;
};

typedef char verify_ui_widget_event_handler_reference_size[
	sizeof(struct ui_widget_event_handler_reference) == 0x48 ? 1 : -1];
typedef char verify_ui_widget_child_reference_size[
	sizeof(struct ui_widget_child_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_conditional_reference_size[
	sizeof(struct ui_widget_conditional_reference) == 0x50 ? 1 : -1];
typedef char verify_ui_widget_search_and_replace_reference_size[
	sizeof(struct ui_widget_search_and_replace_reference) == 0x22 ? 1 : -1];
typedef char verify_ui_widget_game_data_input_reference_size[
	sizeof(struct ui_widget_game_data_input_reference) == 0x24 ? 1 : -1];
typedef char verify_ui_widget_definition_text_color_offset[
	offsetof(struct ui_widget_definition, text_color) == 0x10C ? 1 : -1];
typedef char verify_ui_widget_definition_list_flags_offset[
	offsetof(struct ui_widget_definition, list_flags) == 0x150 ? 1 : -1];
typedef char verify_ui_widget_definition_conditional_widgets_offset[
	offsetof(struct ui_widget_definition, conditional_widgets) == 0x2D4 ? 1 : -1];
typedef char verify_ui_widget_definition_size[
	sizeof(struct ui_widget_definition) == 0x3EC ? 1 : -1];

/* a frame of ui.map's that a menu file scales: where in its widget, and at
what size, it is drawn (pc_menu_frame_placement) */
struct menu_frame_placement
{
	struct bitmap_data const *bitmap;
	short x, y;
	short width, height;
	/* the map's bitmap and frame it is a copy of (menu_pictures_render.c) */
	long source_tag_index;
	long source_frame;
};

/* a value of the port's for a tag (pc_menu_widget_radius,
pc_menu_legend_room) */
#define MENU_TAG_VALUES 64
struct menu_tag_value
{
	long tag_index;
	short value;
};

/* a widget's wide layout (its wide attribute): how far it moves (and what
is in it) and how much wider it is on a 16:9 screen, and its definition's
right as the menus' 640 have it (ui_widget_port_wide_layout) */
struct menu_wide
{
	struct ui_widget_definition *definition;
	short dx, dw;
	/* (and past 16:9, a look's past: its move and growth at 21:9 past 16:9's,
	ui_widget_port_past_scale) */
	short past_dx, past_dw;
	short x1;
	/* a spinner's right arrow's sides (its footer's), which move with its
	right side */
	short footer_x0, footer_x1;
};

/* a widget of the menus' files as the menu engine draws it
(pc_menu_look_definition): its name, its definition made from its look in
the model once it is asked for (made; NULL if none could be), its wide
layout and its picture's corners' radius */
struct menu_look
{
	char const *name;
	struct ui_widget_definition *definition;
	boolean made;
	struct menu_wide wide;
	short radius;
	/* its text's scale at the menus' 640 and at 16:9 and wider (its text's
	scale: pc_menu_text_scale; 1 and 1, its font's size) */
	real text_scale_least, text_scale_most;
};

/* a spinner bound to a setting (menu_functions.c) */
struct pc_menu_setting
{
	long definition_index;
	char const *setting;
	long value_count;
	char const *values[MAXIMUM_STRINGS];
	short loaded_index;
};

/* ---------- globals */

/* the names the files may use, by their values */
static char const *const event_names[] =
{
	"a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
	"up", "down", "left", "right", "start", "back", "left_thumb", "right_thumb",
	"stick_up", "stick_down", "stick_left", "stick_right",
	"right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right",
	"created", "deleted",
	/* (the PC version's, which this engine never sends) */
	"get_focus", "lose_focus", "left_mouse", "middle_mouse", "right_mouse", "double_click", "custom_activation",
	"post_render",
};

static char const *const widget_flag_names[] =
{
	"pass_unhandled_to_focused_child", "pause_game", "flash_bitmap", "up_down_tabs_children",
	"left_right_tabs_children", "up_down_tabs_items", "left_right_tabs_items", "no_focused_child",
	"pass_unhandled_to_all_children", "render_any_controller", "pass_handled_to_all_children",
	"main_menu_if_no_history", "tag_controller_index", "nifty_fx", "no_history",
	/* (the PC version's, which this engine has no use for) */
	"force_handle_mouse", "no_widescreen_fill",
	/* (the port's: ui_widget_port_anchor_offset, ui_widget_port_stretch,
	ui_widget_port_keep_ends, ui_widget_port_wheel_sideways,
	ui_widget_port_foot_offset; above_foot, look_make) */
	"anchor_left", "anchor_right", "stretch", "keep_ends", "wheel_sideways", "foot", "above_foot",
};

/* the port's widget flags' bits (widget_flag_names) */
#define WIDGET_FLAG_ANCHOR_LEFT 17
#define WIDGET_FLAG_ANCHOR_RIGHT 18
#define WIDGET_FLAG_STRETCH 19
#define WIDGET_FLAG_KEEP_ENDS 20
#define WIDGET_FLAG_WHEEL_SIDEWAYS 21
#define WIDGET_FLAG_FOOT 22
#define WIDGET_FLAG_ABOVE_FOOT 23

/* how far the tall layout's footer is raised on a touchscreen
(port_settings.py's TALL_FOOT_LIFT): its prompts' words, which end 11 units
above the screen's foot, to above a phone's safe area there (an iPhone's 21
points: menu_layout.c's MENU_LAYOUT_PHONE_FOOT, 25.6 units) */
#define TALL_FOOT_LIFT 16
/* (a picture's name and _touch) */
#define HALO_MENU_TOUCH_NAME 300

/* whether the menus are laid out for a touchscreen: the touchscreens'
builds, or debug.touch_menus (read again when a setting changes: the menu
studio's F5 changes it and reloads the menus, menu_files.c's MENU_TOUCH) */
int ui_widget_port_touch_menus(void)
{
#if !defined(HALO_ANDROID) && !defined(HALO_IOS)
	static int touch_menus = -1;
	static unsigned long read_at;

	if (touch_menus < 0 || read_at != config_changes())
	{
		read_at = config_changes();
		touch_menus = config_boolean("debug.touch_menus") != 0;
	}
	return touch_menus;
#else
	return 1;
#endif
}

/* how far the screen reaches past each side of the menus' 640 where its
widgets are anchored to its edges: on the touchscreens' builds (the phones'
wide screens, which the menus' 640 would leave with wide empty sides, or
debug.touch_menus), else 0, the menus centred as the PC version has them */
short ui_widget_port_anchor_extra(void)
{
	if (!ui_widget_port_touch_menus())
		return 0;
	/* (the canvas the menus are drawn in: menu_view.c's) */
	return (short)MAX(menu_view_side(), 0);
}

/* anchor_left: the widget (and what is in it) moved to the screen's left
edge by that much; anchor_right: to its right */
short ui_widget_port_anchor_offset(long flags)
{
	short extra = ui_widget_port_anchor_extra();

	if (!extra)
		return 0;
	if (flags & FLAG(WIDGET_FLAG_ANCHOR_LEFT))
		return (short)-extra;
	if (flags & FLAG(WIDGET_FLAG_ANCHOR_RIGHT))
		return extra;
	return 0;
}

/* stretch: the widget's picture drawn wider by both sides' width, from its
left (a panel anchored left that reaches the right edge) */
short ui_widget_port_stretch(long flags)
{
	return flags & FLAG(WIDGET_FLAG_STRETCH) ? (short)(2 * ui_widget_port_anchor_extra()) : 0;
}

/* keep_ends: the widget's picture drawn at the widget's width with its ends
as they are, its middle stretched or shortened (a tab narrower than its
picture: ui_widget.c's draw_bitmap_widened_in_rect) */
boolean ui_widget_port_keep_ends(long flags)
{
	return (flags & FLAG(WIDGET_FLAG_KEEP_ENDS)) != 0;
}

/* wheel_sideways: a row its own left and right step through (Choose
Mission's cards), which the desktop's wheel steps as the d-pad's left and
right (ui_widget.c's ui_mouse_wheel_widget) */
boolean ui_widget_port_wheel_sideways(long flags)
{
	return (flags & FLAG(WIDGET_FLAG_WHEEL_SIDEWAYS)) != 0;
}

/* how far the tall layout's footer is raised: TALL_FOOT_LIFT on a
touchscreen, else 0 (the computer's layout as it is) */
short ui_widget_port_foot_lift(void)
{
	return ui_widget_port_touch_menus() ? TALL_FOOT_LIFT : 0;
}

/* foot: the widget (and what is in it) moved up by the footer's lift, as it
is drawn and tapped (the tall layout's footer: its prompts and buttons) */
short ui_widget_port_foot_offset(long flags)
{
	return flags & FLAG(WIDGET_FLAG_FOOT) ? (short)-ui_widget_port_foot_lift() : 0;
}

static char const *const widget_type_names[] = { "container", "text", "spinner", "column_list" };
static char const *const list_flag_names[] = { "items_in_code", "items_from_strings", "one_tooltip", "single_preview" };
/* (wrap: the port's, its text fitted to its box, ui_widget.c's
ui_widget_port_text_fit; unbreak, with wrap: its written line breaks made
spaces first, ui_widget_port_text_unbreak) */
static char const *const text_flag_names[] = { "editable", "password", "flashing", "no_focus_test", "wrap",
	"unbreak" };
static char const *const align_names[] = { "left", "right", "center" };
/* (build_number and pid are the PC version's, which this engine has not) */
static char const *const replace_names[] = { "none", "controller", "build_number", "pid" };

/* the game data functions, as the tags name them
(ui_widget_game_data_input_functions.c) */
static char const *const game_data_input_names[] =
{
	"NULL", "player settings menu update desc", "unused", "playlist settings menu update desc",
	"gametype select menu update desc", "multiplayer type menu update desc", "solo level select update",
	"difficulty menu update desc", "build number textbox only", "server list update",
	"network pregame status update", "splitscreen pregame status update", "net splitscreen prejoin players",
	"mp profile list update", "3wide player profile list update", "plyr prof edit select menu upd8",
	"player profile small menu update", "game settings lists text update", "solo game objective text",
	"color picker update", "game settings lists pic update", "main menu fake animate",
	"mp level select update", "get active plyr profile name", "get edit plyr profile name",
	"get edit game settings name", "get active plyr profile color", "mp set textbox map name",
	"mp set textbox game ruleset", "mp set textbox teams noteams", "mp set textbox score limit",
	"mp set textbox score limit type", "mp set bitmap for map", "mp set bitmap for ruleset",
	"mp set textbox", "mp edit profile set rule text", "system link status check", "mp game directions",
	"teams no teams bitmap update", "warn if diff will nuke saved game", "dim if no net cable",
};

/* the port's event handler functions (menu_functions.c), from
PC_MENU_FUNCTION_BASE: its own, then the PC version's that the Xbox's have
not (as its tags name them), then its own of the Xbox's names, which our
widgets run instead of the Xbox's (those take the Xbox's widgets: a spinner
of levels, the difficulty list itself) */
static char const *const port_function_names[] =
{
	"port quit game", "port setting load", "port setting save", "unwired",
	"exit gracefully to xbox dashboard", "pause game invert pitch", "start new coop game",
	"pause game invert spinner get", "pause game invert spinner set", "main menu quit game",
	"mouse emit accept event", "mouse emit back event", "mouse emit dpad left event",
	"mouse emit dpad right event", "mouse spinner 3wide click", "controls screen init", "video screen init",
	"controls begin binding", "gamespy screen init", "gamespy screen dispose", "gamespy select header",
	"gamespy select item", "gamespy select button", "plr prof init mouse set", "plr prof change mouse set",
	"plr prof init audio set", "plr prof change audio set", "plr prof change video set",
	"controls screen dispose", "controls screen change set", "mouse emit x event", "gamepad screen init",
	"gamepad screen dispose", "gamepad screen change gamepads", "gamepad screen select item",
	"mouse screen defaults", "audio screen defaults", "video screen defaults", "controls screen defaults",
	"profile set edit begin", "profile manager delete", "profile manager select", "gamespy dismiss error",
	"server settings init", "ss edit server name", "ss edit server password", "ss start game",
	"video test dialog init", "video test dialog dispose", "video test dialog accept",
	"gamespy dismiss filters", "gamespy update filter settings", "gamespy back handler",
	"mouse spinner 1wide click", "controls back handler", "controls advanced launch", "controls advanced ok",
	"mp pause menu open", "mp game options open", "mp choose team", "mp prof init vehicle options",
	"mp prof save vehicle options", "single prev cl item activated", "mp prof init teamplay options",
	"mp prof save teamplay options", "mp game options choose", "emit custom activation event",
	"plr prof cancel audio set", "plr prof init network options", "plr prof save network options",
	"difficulty item select",
	"gamespy get patch", "video screen dispose", "campaign menu init", "campaign menu continue",
	"load game menu init", "load game menu dispose", "load game menu activated", "solo menu save checkpoint",
	"mp type set mode", "direct ip connect init",
	"direct ip connect go", "direct ip edit field", "network settings edit a port", "network settings defaults",
	"load game menu delete request", "load game menu delete finish",
	"initialize sp level list solo", "dispose sp level list", "solo level set map", "set difficulty",
	"port settings save", "port settings defaults", "controls binding slot",
	"color picker menu initialize", "color picker menu dispose", "color picker select color",
	"player profile list initialize", "player profile list dispose", "request del player profile",
	"final del player profile",
	"ss copy invite", "join controller to mp game", "mp level list initialize", "mp level list dispose",
	"mp level select", "mp profiles list initialize", "mp profiles list dispose", "mp profile set for game",
	"port lobby preview join",
	"port setup edit",
	"port setup options init", "port setup options save",
	"port map list back",
	"port mcc solo folder back", "port mcc host folder back",
	"port mcc share download", "port mcc share workshop", "port mcc share browser", "port mcc share cancel", "port mcc share closed",
	"port mcc downloads open", "port mcc downloads previous", "port mcc downloads next", "port mcc downloads remove",
	"port pause end game", "port coop friendly fire",
	"port coop begin", "port coop player 2 list initialize", "port coop player 2",
	"port lobby open", "port lobby add player", "port lobby join", "port lobby leave",
	"port lobby player list initialize", "port lobby player choose",
	"port lobby preview add", "port lobby preview leave",
	/* (the gametype editor's: the Xbox's walk their rows by place, which the
	PC version's screens changed) */
	"mp profile begin editing", "mp profile save changes", "request del playlist profile", "final del playlist profile",
	"mp profile init game engine", "mp profile set game engine", "mp profile init ctf rules",
	"mp profile init koth rules", "mp profile init slayer rules", "mp profile init oddball rules",
	"mp profile init racing rules", "mp profile init player opts", "mp profile init item options",
	"mp profile init indicator opts", "mp profile set ctf rules", "mp profile set koth rules",
	"mp profile set slayer rules", "mp profile set oddball rules", "mp profile set racing rules",
	"mp profile set player options", "mp profile set item options", "mp profile set indicator opts",
	/* (the gametype editor's OPENCE OPTIONS: OpenCE's gametypes, game_modes.c) */
	"mp profile init opence options", "mp profile set opence options",
	/* (Settings' OK: the Xbox's fails when the profile has no changes, the
	settings' screens having written theirs to config.toml) */
	"player profile save changes",
	/* (the server browser's password screen) */
	"port password init", "port password edit", "port password join", "port password back",
	/* (a new profile's screens: Settings' is then being edited, which the
	Xbox's leaves to its own screens) */
	"new campaign decision",
	/* (the Mods screen, Settings' MODS tab's MANAGE MODS: menu_functions.c) */
	"port mods init", "port mods close", "port mods toggle", "port mods move up", "port mods move down",
	"port mods save",
	/* (the update question, the Steam Frame's and gamescope's: menu_functions.c) */
	"port update now",
	/* (the crash question, report.c's: menu_functions.c) */
	"port crash report", "port crash dismiss",
	/* (Multiplayer's panel: menu_functions.c's multiplayer_status) */
	"port multiplayer status end", "port multiplayer refresh",
	/* (Settings' tabs and pages: menu_functions.c's settings_*) */
	"port settings begin", "port settings tab", "port settings tab next", "port settings tab previous",
	"port settings ok", "port settings cancel", "port settings keep", "port settings page cancel",
	"port settings new profile", "port settings profiles", "port settings report", "port settings weapon reset",
	"port settings gyro calibrate", "port settings clear caches",
	/* (Settings' search, the menu engine's: settings_search_menu.inc) */
	"port settings search", "port settings search edit", "port settings search back",
	/* (Online's Discord account: menu_functions.c's discord_link and
	discord_unlink) */
	"port discord link", "port discord unlink",
	/* (Settings' sliders: menu_functions.c's settings_slider_*) */
	"port slider left", "port slider right", "port slider edit",
	/* (Settings' segmented rows: menu_functions.c's settings_segment_pick) */
	"port setting segment",
	/* (the server browsers' favorites: menu_functions.c's browser_favorite_toggle) */
	"port browser favorite",
	/* (internet play's connecting box: menu_functions.c's connect_dialog) */
	"port connect retry", "port connect back",
	/* (the browsers' sort, X on a row: menu_functions.c's browser_sort_choose) */
	"port browser sort",
	/* (the browsers' tabs and refresh: menu_functions.c's browser_tab_choose,
	browser_refresh) */
	"port browser tab", "port browser tab next", "port browser tab previous", "port browser refresh",
	/* (the browsers' Filters page, and the searches of their SERVER and MAP
	columns: menu_functions.c's browser_filters_open, browser_search_edit,
	browser_search_next, browser_search_clear_chosen) */
	"port browser filters", "port browser search", "port browser search next", "port browser search clear",
	/* (Multiplayer's PARTY and its screen: party_menu.inc) */
	"port party open", "port party button", "port party x", "port party y",
	/* (the main menu's PRO 2V2 screen: matchmaking_menu.inc) */
	"port matchmaking button", "port matchmaking x",
	/* (Multiplayer's FRIENDS and its screens: friends_menu.inc) */
	"port friends open", "port friends button", "port friends x", "port friends y",
	"port friends code init", "port friends code edit", "port friends code send", "port friends code back",
	/* (the pause menu's HOST CONTROLS and its screen: host_controls_menu.inc) */
	"port host open", "port host init", "port host tab", "port host tab next", "port host tab previous",
	"port host row", "port host left", "port host right", "port host action", "port host back",
	/* (the computer's lobby: lobby_menu.inc) */
	"port lobby team red", "port lobby team blue", "port lobby team toggle", "port lobby team click",
	"port lobby start", "port lobby invite", "port lobby settings", "port lobby bots load",
	/* (a dedicated server's map vote: menu_functions.c) */
	"port map vote open", "port map vote choose",
	/* (Basic Training's chapters: menu_functions.c's training_*) */
	"port training chapter", "port training open",
	/* (Controller's Button Remapping: controller_remap_menu.inl) */
	"port controller bind", "port controller left", "port controller right", "port controller clear",
	/* (the campaign's screens: campaign_menu.inc) */
	"port campaign init", "port campaign continue", "port campaign missions", "port campaign missions init",
	"port campaign previous", "port campaign next", "port campaign tab", "port campaign tab previous",
	"port campaign tab next", "port campaign choose", "port campaign difficulty init", "port campaign play",
	"port campaign difficulty previous", "port campaign difficulty next",
	"port campaign up", "port campaign down",
	/* (a mission in progress started again: campaign_menu.inc's campaign_restart_*) */
	"port campaign restart ask", "port campaign missions restart", "port campaign restart",
	"port campaign restart cancel",
	/* (the main menu's 2V2 browser: doubles_browser.inc) */
	"port doubles init", "port doubles dispose", "port doubles host", "port doubles find", "port doubles friend",
	"port doubles join", "port doubles invite", "port doubles x",
	/* (a hand-written screen's item: its action; and a prompt of its button
	bar: its key; left and right on a row or a choice, menu_screen.c) */
	"port screen do", "port screen key", "port screen left", "port screen right",
	/* (B on an editor: back, or the question when it has changes not saved:
	menu_functions.c's edit_leave_ask) */
	"port edit leave",
	/* (co-op's screens, the new engine's: HOST's choosing begun and over,
	the co-op list's screen made and let go of: coop_menu.inc) */
	"port coop host begin", "port coop host end", "port coop join init", "port coop join dispose",
};

/* the PC version's game data functions that the Xbox's have not, from
PC_MENU_FUNCTION_BASE (menu_functions.c) */
static char const *const port_game_data_input_names[] =
{
	"unwired",
	"pause game set textbox inverted", "dim unless two controllers", "controls update menu",
	"video menu update", "gamespy screen update", "common button bar update", "gamepad update menu",
	"server settings update", "audio menu update", "mp prof vehicles update", "solo map list update",
	"mp map list update", "gt select list update", "gt edit list update", "load game list update",
	"direct ip connect update", "network settings update",
	"port settings help", "color picker update", "3wide player profile list update",
	"port lobby preview update",
	/* (the gametype editor's: the Xbox's stops the game on the buttons'
	row; the Xbox's read only player_ui's gametype, not Server Setup's) */
	"game settings lists text update", "get edit game settings name", "mp edit profile set rule text",
	"port password update",
	/* (the profile settings' picture: the Xbox's of the button settings,
	on Gamepad Setup's row) */
	"port gamepad layout preview",
	/* (the Mods screen's list; the main menu's Quit notes the main menu
	shown, for the update question) */
	"port mods update", "port update question",
	/* (the MCC packages' download and downloads screens: their rows shown, their text) */
	"port mcc share update", "port mcc share text",
	"port mcc downloads update", "port mcc downloads text",
	/* (network co-op's pause menu's FRIENDLY FIRE: its text, ON or OFF) */
	"port coop friendly fire text",
	/* (Multiplayer's panel, what is live for the item chosen) */
	"port multiplayer status",
	/* (the gametype editor's description, with OPENCE OPTIONS' row) */
	"port playlist edit desc",
	/* (Settings' tabs and pages: the rows shown, the panel) */
	"port settings page",
	/* (internet play's connecting box) */
	"port connect update",
	/* (the Party screen: party_menu.inc) */
	"port party update",
	"port matchmaking update",
	"port friends update", "port friends code update",
	/* (Host Controls: host_controls_menu.inc) */
	"port host update",
	/* (the computer's lobby: lobby_menu.inc) */
	"port lobby board update",
	/* (a dedicated server's map vote's screen) */
	"port map vote update",
	/* (Basic Training's chapters) */
	"port training update",
	/* (the campaign's screens: campaign_menu.inc) */
	"port campaign update", "port campaign missions update", "port campaign difficulty update",
	"port campaign restart update",
	/* (the main menu's 2V2 browser: doubles_browser.inc) */
	"port doubles update",
	/* (Settings' band's right: the profile it edits) */
	"port settings profile",
	/* (the gametype editor's Step 1: its BACK's description, the gametype's
	type's) */
	"port gametype select desc",
};

/* a pause box made wider while the keyboard plays (pause_boxes_fit) */
#define PAUSE_BOXES 4
struct pause_box_growth
{
	struct ui_widget_definition *box;
	struct ui_widget_definition *screen;
	struct ui_widget_definition *split_screen;
	short growth;
};

static void pause_boxes_fit(void);

static struct
{
	/* what is let go when the map unloads */
	void **blocks;
	long block_count;
	struct bitmap_data **bitmaps;
	long bitmap_count;
	struct menu_frame_placement *placements;
	long placement_count;
	struct cache_file_tag_instance *original_instances;
	long original_count;
	struct pc_menu_setting *settings;
	long setting_count;
	/* the wide layouts of the tags added, by their index after the map's
	(original_count), and the extra width they are laid out for now */
	struct menu_wide *wides;
	long wide_count;
	/* each widget's generator source (its source attribute: NULL none),
	by its tag's index after the map's, as the wide layouts are
	(debug.render_marks: pc_menu_widget_source) */
	char const **sources;
	long source_count;
	short wide_extra;
	/* (and the width past 16:9's they are laid out for now:
	ui_widget_port_past_scale) */
	short past_extra;
	boolean loaded;
	char root[300];
	/* the tags' generation they were added to (cache_files_tags_generation):
	the settings' definitions are tag indices of that map's */
	unsigned long generation;
	/* the widgets whose pictures' corners are cut round (a <widget>'s
	radius), and the
	legends' rows whose panel was made wider for them (pause_box_widen), by
	tag index */
	struct menu_tag_value radii[MENU_TAG_VALUES];
	long radius_count;
	struct menu_tag_value legend_rooms[MENU_TAG_VALUES];
	long legend_room_count;
	/* the menu engine's definitions of the menus' files' widgets, by their
	tags' indices after the map's, as the wide layouts are
	(pc_menu_look_definition), and what the fonts are found by at run time,
	as font_tag finds them when the tags are built */
	struct menu_look *looks;
	long look_count;
	long heading_tag;
	long map_font;
	boolean game_map;
	/* the pause boxes made wider for the keys' caps of the legends under
	them while the keyboard plays (pause_boxes_fit), each with the screen
	whose mission objectives move with it (or NULL), and whether they are
	wider now */
	struct pause_box_growth pause_boxes[PAUSE_BOXES];
	long pause_box_count;
	boolean pause_boxes_wide;
	/* the game's event handlers of the model's widgets' behaviours
	(menu_tags_behaviour_handlers), made with the widgets (behaviours_build),
	by their index in the model's behaviours, and the model they are of */
	struct halo_menu_model const *behaviour_model;
	struct ui_widget_event_handler_reference **behaviour_handlers;
	long *behaviour_handler_counts;
} menu_tags;

/* the build under way */
static struct
{
	struct halo_menus const *menus;
	long first_index;
	long first_salt;
	long next;
	/* each widget's, bitmap's and string list's tag index, and each
	widget's own text's and strings' (NONE: none) */
	long *widget_tags;
	long *bitmap_tags;
	/* the menus' heading font (pc\heading: large_ui's measures, drawn in
	OpenCE by the high-res text, port/assets/fonts/fonts.json), else NONE */
	long heading_tag;
	long *strings_tags;
	long *text_tags;
	long *spinner_tags;
	boolean failed;
	/* a game map's build (the pause menu's SETTINGS): what the map has not
	got is none, not a failure */
	boolean game_map;
	/* a single-player campaign's map's, whose screens pause the game */
	boolean campaign;
	/* the strings taken from the Custom Edition's loc.map */
	long map_strings;
	/* a network co-op game's map's: its pause menu gets FRIENDLY FIRE too */
	boolean coop;
	/* a game map's font for the menus' fonts it has not by name (a Custom
	Edition map's protected names): its pause menu's, else NONE */
	long map_font;
} build;

/* ---------- the wide layout of the port's own screens

The port's screens (Settings, the server browser, Multiplayer, the lobby)
use the screen's width up to 16:9; the game's own
menus stay in the centred 640. A widget's wide attribute, "dx dw"
(tools/port_settings.py's wide), moves it (and what is in it, as an anchor
does) by dx and makes it dw wider on a 16:9 screen, WIDE_LAYOUT_WIDTH of the
menus' units at 480 lines, and by a part of that on a narrower one, down to
nothing at the menus' 640 (4:3); a wider screen (21:9, 32:9) has 16:9's
layout, centred. Each frame's widgets are drawn (and clicked) at the width
of the view they are drawn in (ui_widget_port_wide_layout: a split screen's
quarter has the 640's); a widget anchored on a touchscreen (anchor_left,
anchor_right, stretch) keeps its anchor instead. */
#define WIDE_LAYOUT_WIDTH 852
#define WIDE_LAYOUT_EXTRA (WIDE_LAYOUT_WIDTH - 640)
/* A look in the model (the menu engine's: tools/port_settings.py's tall
layout) may say how it is laid out past 16:9 too: its past, "dx dw", moves
it dx and makes it dw wider at 21:9 (WIDE_LAYOUT_PAST_WIDTH) than at 16:9,
and by its part of that on a screen between them, and more on a wider one,
so that its screen is used edge to edge */
#define WIDE_LAYOUT_PAST_WIDTH 1138
#define WIDE_LAYOUT_PAST_EXTRA (WIDE_LAYOUT_PAST_WIDTH - WIDE_LAYOUT_WIDTH)
/* On a touchscreen a widget moved past 16:9 and not made wider (a title, a
prompt, a tab) is moved less, to the phone's safe area's side (72, an iPhone's
59 points: menu_layout.c's MENU_LAYOUT_PHONE_SIDE) from the screen's edge, not
the layout's margin (24): so much less of the width past 16:9 each side */
#define WIDE_LAYOUT_TOUCH_INSET (72 - 24)

/* the extra width a view this wide lays the port's screens out in: 0 at the
menus' 640 or less, WIDE_LAYOUT_EXTRA at 16:9 and wider */
short ui_widget_port_wide_extra_of(long width)
{
	return (short)(width <= 640 ? 0 : width >= WIDE_LAYOUT_WIDTH ? WIDE_LAYOUT_EXTRA : width - 640);
}

/* a wide attribute's move or growth at an extra width (its 16:9 value's
part of it, rounded) */
short ui_widget_port_wide_scale(short value, short extra)
{
	long scaled = (long)value * extra;

	return (short)((scaled + (scaled < 0 ? -WIDE_LAYOUT_EXTRA / 2 : WIDE_LAYOUT_EXTRA / 2)) / WIDE_LAYOUT_EXTRA);
}

/* the width of a view this wide past 16:9's (none at 16:9 or narrower) */
static short wide_past_of(long width)
{
	return (short)(width <= WIDE_LAYOUT_WIDTH ? 0 : MIN(width - WIDE_LAYOUT_WIDTH, 4 * WIDE_LAYOUT_PAST_EXTRA));
}

/* a past attribute's move or growth at a width past 16:9's (its 21:9
value's part of it, or more, rounded) */
short ui_widget_port_past_scale(short value, short past)
{
	long scaled = (long)value * past;

	return (short)((scaled + (scaled < 0 ? -WIDE_LAYOUT_PAST_EXTRA / 2 : WIDE_LAYOUT_PAST_EXTRA / 2)) /
		WIDE_LAYOUT_PAST_EXTRA);
}

/* whether a widget with a wide layout keeps its touchscreen's anchor */
static boolean wide_anchored(struct menu_wide const *wide)
{
	return ui_widget_port_anchor_offset(wide->definition->flags) || ui_widget_port_stretch(wide->definition->flags);
}

/* a widget definition's wide layout, NULL if it has none (or is anchored on
a touchscreen) */
static struct menu_wide const *wide_of(long definition_index)
{
	long index;
	struct menu_wide const *wide;

	if (!menu_tags.wides || definition_index == NONE)
		return NULL;
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(definition_index) - menu_tags.original_count;
	if (index < 0 || index >= menu_tags.wide_count)
		return NULL;
	/* (the menu engine's, from the model's look, where it draws the widget) */
	wide = menu_engine_looks() && pc_menu_look_definition(definition_index) ? &menu_tags.looks[index].wide :
		&menu_tags.wides[index];
	return wide->definition && !wide_anchored(wide) ? wide : NULL;
}

/* a widget's definition as much wider as its wide layout says at an extra
width (and past 16:9's; a spinner's right arrow with its right side) */
static void wide_apply(struct menu_wide const *wide, short extra, short past)
{
	if (wide->definition && (wide->dw || wide->past_dw))
	{
		short grown = wide_anchored(wide) ? 0 :
			(short)(ui_widget_port_wide_scale(wide->dw, extra) + ui_widget_port_past_scale(wide->past_dw, past));

		wide->definition->bounds.x1 = (short)(wide->x1 + grown);
		if (wide->definition->type == _widget_type_spinner_list)
		{
			wide->definition->list_footer_bounds.x0 = (short)(wide->footer_x0 + grown);
			wide->definition->list_footer_bounds.x1 = (short)(wide->footer_x1 + grown);
		}
	}
}

/* the port's screens laid out for a view this wide: each widget's
definition as much wider as its wide attribute says (its move is
ui_widget_port_wide_offset's, as it is drawn) */
void ui_widget_port_wide_layout(long width)
{
	short extra = ui_widget_port_wide_extra_of(width);
	short past = wide_past_of(width);
	long index;

	pause_boxes_fit();
	if ((extra == menu_tags.wide_extra && past == menu_tags.past_extra) || !menu_tags.wides)
		return;
	menu_tags.wide_extra = extra;
	menu_tags.past_extra = past;
	for (index = 0; index < menu_tags.wide_count; index++)
		wide_apply(&menu_tags.wides[index], extra, past);
	/* (and the menu engine's definitions made) */
	for (index = 0; index < menu_tags.look_count; index++)
		wide_apply(&menu_tags.looks[index].wide, extra, past);
}

/* the extra width the port's screens are laid out in now (the view's past
the menus' 640: ui_widget_port_wide_layout), half of it each side */
short ui_widget_port_wide_extra(void)
{
	return menu_tags.wide_extra;
}

short ui_widget_port_past_extra(void)
{
	return menu_tags.past_extra;
}

/* the width past 16:9 a widget moved and not made wider is moved by: on a
touchscreen, short of the safe area's sides (WIDE_LAYOUT_TOUCH_INSET) */
short ui_widget_port_past_move(void)
{
	return ui_widget_port_touch_menus() ?
		(short)MAX(0, menu_tags.past_extra - 2 * WIDE_LAYOUT_TOUCH_INSET) : menu_tags.past_extra;
}

/* how far a widget (and what is in it) is moved where it is drawn, in the
layout now */
short ui_widget_port_wide_offset(long definition_index)
{
	struct menu_wide const *wide = wide_of(definition_index);

	return wide ? (short)(ui_widget_port_wide_scale(wide->dx, menu_tags.wide_extra) +
		ui_widget_port_past_scale(wide->past_dx, wide->past_dw ? menu_tags.past_extra : ui_widget_port_past_move())) : 0;
}

/* how much wider than its picture a widget is drawn in the layout now: its
picture's middle stretched (ui_widget.c) */
short ui_widget_port_wide_stretch(long definition_index)
{
	struct menu_wide const *wide = wide_of(definition_index);

	return wide ? (short)(ui_widget_port_wide_scale(wide->dw, menu_tags.wide_extra) +
		ui_widget_port_past_scale(wide->past_dw, menu_tags.past_extra)) : 0;
}

/* the scale a widget's text is drawn at in the layout now (its look's text
scale, between its least at the menus' 640 and its most at 16:9 and wider;
1 for a widget without one) */
real pc_menu_text_scale(long definition_index)
{
	long index;
	struct menu_look const *look;
	real block_scale;

	/* (a hand-written screen's building block's: menu_screen.c) */
	if (definition_index != NONE && menu_screen_text_scale(definition_index, menu_tags.wide_extra, &block_scale))
		return block_scale;
	if (!menu_tags.looks || definition_index == NONE || !menu_engine_looks())
		return 1.0f;
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(definition_index) - menu_tags.original_count;
	if (index < 0 || index >= menu_tags.look_count || !menu_tags.looks[index].definition)
		return 1.0f;
	look = &menu_tags.looks[index];
	return look->text_scale_least + (look->text_scale_most - look->text_scale_least) *
		(real)menu_tags.wide_extra / (real)WIDE_LAYOUT_EXTRA;
}

/* ---------- private code */

/* (the game's allocator, which malloc and free are here: cseries.h) */
static void *allocate(long size)
{
	void *block = malloc(size > 0 ? size : 1);

	if (!block)
	{
		build.failed = TRUE;
		return NULL;
	}
	memset(block, 0, size > 0 ? size : 1);
	{
		void **blocks = realloc(menu_tags.blocks, (menu_tags.block_count + 1) * sizeof(*menu_tags.blocks));

		if (!blocks)
		{
			free(block);
			build.failed = TRUE;
			return NULL;
		}
		menu_tags.blocks = blocks;
	}
	menu_tags.blocks[menu_tags.block_count++] = block;
	return block;
}

static void problem(char const *file, long line, char const *message, char const *detail)
{
	if (!build.failed)
		halo_menus_log(file, line, message, detail);
	build.failed = TRUE;
}

static long name_index(char const *name, char const *const *names, long count)
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name, names[index]))
			return index;
	}
	return NONE;
}

/* the bits of the space-separated names */
static long flags_parse(char const *text, char const *const *names, long count, char const *file, long line)
{
	long flags = 0;

	while (text && *text)
	{
		char word[64];
		long length, bit;

		text += strspn(text, " \t\r\n");
		length = (long)strcspn(text, " \t\r\n");
		if (!length)
			break;
		if (length >= (long)sizeof(word))
			length = sizeof(word) - 1;
		memcpy(word, text, length);
		word[length] = 0;
		text += strcspn(text, " \t\r\n");
		bit = name_index(word, names, count);
		if (bit == NONE)
			problem(file, line, "there is no flag", word);
		else
			flags |= 1L << bit;
	}
	return flags;
}

static void reference_clear(struct tag_reference *reference, long group_tag)
{
	reference->group_tag = group_tag;
	reference->name = "";
	reference->name_length = 0;
	reference->index = NONE;
}

static void reference_set(struct tag_reference *reference, long group_tag, long tag_index)
{
	if (tag_index == NONE)
	{
		reference_clear(reference, group_tag);
		return;
	}
	reference->group_tag = group_tag;
	reference->name = (char *)tag_get_name(tag_index);
	reference->name_length = (long)strlen(reference->name);
	reference->index = tag_index;
}

static long find(char const *name, long count, char const *(*name_of)(long))
{
	long index;

	for (index = 0; name && index < count; index++)
	{
		if (!strcmp(name_of(index), name))
			return index;
	}
	return NONE;
}

static char const *widget_name(long index) { return build.menus->widgets[index].name; }
static char const *bitmap_name(long index) { return build.menus->bitmaps[index].name; }
static char const *strings_name(long index) { return build.menus->string_lists[index].name; }

/* The widgets', bitmaps' and string lists' names, found by their hash: each
name's first index, as find has it. The files are read once
(halo_menus_load), and each of their thousands of names is looked up as
each map loads, by name (each widget's own, its children's, its
handlers'), which a walk of all of them made a fifth of a second. Made the
first time; empty (find walks them instead) if there is no memory. */
struct name_index
{
	unsigned long mask;
	long *slots; /* index + 1; 0: empty */
};

static struct
{
	struct halo_menus const *menus;
	struct name_index widgets, bitmaps, strings;
} name_indices;

static unsigned long name_hash(char const *name)
{
	unsigned long hash = 2166136261UL;

	while (*name)
		hash = (hash ^ (unsigned char)*name++) * 16777619UL;
	return hash;
}

static void name_index_make(struct name_index *index, long count, char const *(*name_of)(long))
{
	unsigned long size = 16, slot;
	long entry;

	while (size < (unsigned long)count * 2)
		size *= 2;
	/* (the game's allocator: cseries.h, which has no calloc and refuses to
	free NULL) */
	if (index->slots)
		free(index->slots);
	index->slots = malloc(size * sizeof(*index->slots));
	index->mask = size - 1;
	if (!index->slots)
		return;
	memset(index->slots, 0, size * sizeof(*index->slots));
	for (entry = 0; entry < count; entry++)
	{
		if (!name_of(entry))
			continue;
		for (slot = name_hash(name_of(entry)) & index->mask; index->slots[slot]; slot = (slot + 1) & index->mask)
		{
			/* (a name again: the first keeps it) */
			if (!strcmp(name_of(index->slots[slot] - 1), name_of(entry)))
				break;
		}
		if (!index->slots[slot])
			index->slots[slot] = entry + 1;
	}
}

static void name_indices_make(void)
{
	if (name_indices.menus == build.menus)
		return;
	name_indices.menus = build.menus;
	name_index_make(&name_indices.widgets, build.menus->widget_count, widget_name);
	name_index_make(&name_indices.bitmaps, build.menus->bitmap_count, bitmap_name);
	name_index_make(&name_indices.strings, build.menus->string_list_count, strings_name);
}

/* the files read again (the menu studio's reload: menu_studio.c): their
names indexed again, though the description is in the same place */
void menu_tags_files_changed(void)
{
	name_indices.menus = NULL;
}

static long named(struct name_index const *index, char const *name, long count, char const *(*name_of)(long))
{
	unsigned long slot;

	if (!index->slots || name_indices.menus != build.menus)
		return find(name, count, name_of);
	if (!name)
		return NONE;
	for (slot = name_hash(name) & index->mask; index->slots[slot]; slot = (slot + 1) & index->mask)
	{
		if (!strcmp(name_of(index->slots[slot] - 1), name))
			return index->slots[slot] - 1;
	}
	return NONE;
}

static long widget_named(char const *name)
{
	return named(&name_indices.widgets, name, build.menus->widget_count, widget_name);
}

static long bitmap_named(char const *name)
{
	return named(&name_indices.bitmaps, name, build.menus->bitmap_count, bitmap_name);
}

static long strings_named(char const *name)
{
	return named(&name_indices.strings, name, build.menus->string_list_count, strings_name);
}

static long map_tag(long group_tag, char const *name, char const *file, long line)
{
	long index = tag_loaded(group_tag, name);

	/* (a game map has few of ui.map's: those are none) */
	if (index == NONE && !build.game_map)
		problem(file, line, "the map has no tag", name);
	return index;
}

/* a tag: the map's by its path (with a backslash), else one of ours */
static long tag_named(long group_tag, char const *name, char const *file, long line)
{
	long index = NONE;

	if (strchr(name, '\\'))
		return map_tag(group_tag, name, file, line);
	switch (group_tag)
	{
	case UI_WIDGET_DEFINITION_TAG:
		index = widget_named(name);
		index = index == NONE ? NONE : build.widget_tags[index];
		break;
	case BITMAP_GROUP_TAG:
		index = bitmap_named(name);
		index = index == NONE ? NONE : build.bitmap_tags[index];
		break;
	case UNICODE_STRING_LIST_TAG:
		index = strings_named(name);
		index = index == NONE ? NONE : build.strings_tags[index];
		break;
	}
	if (index == NONE)
		problem(file, line, "there is nothing named", name);
	return index;
}

/* "unwired <name>": a function the PC version has for these widgets, not
yet written for this engine (port/assets/menus/UNWIRED.md), which does
nothing */
static boolean unwired(char const *name)
{
	return !strncmp(name, "unwired ", 8);
}

static long function_index(char const *name, char const *file, long line)
{
	char const *function_name;
	long index;

	if (unwired(name))
		name = "unwired";
	index = name_index(name, port_function_names, NUMBEROF(port_function_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	for (index = 0; (function_name = ui_widget_event_handler_function_name(index)) != NULL; index++)
	{
		if (!strcmp(name, function_name))
			return index;
	}
	problem(file, line, "there is no event handler function", name);
	return NONE;
}

/* an event handler function's index as a widget's handler of the name runs
it (ours first, then the game's); NONE for none (menu_tour.c's setup) */
long pc_menu_event_function_index(char const *name)
{
	char const *function_name;
	long index = name_index(name, port_function_names, NUMBEROF(port_function_names));

	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	for (index = 0; (function_name = ui_widget_event_handler_function_name(index)) != NULL; index++)
	{
		if (!strcmp(name, function_name))
			return index;
	}
	return NONE;
}

static long game_data_input_index(char const *name, char const *file, long line)
{
	long index;

	if (unwired(name))
		name = "unwired";
	/* (ours first, as for the event handlers) */
	index = name_index(name, port_game_data_input_names, NUMBEROF(port_game_data_input_names));
	if (index != NONE)
		return PC_MENU_FUNCTION_BASE + index;
	index = name_index(name, game_data_input_names, NUMBEROF(game_data_input_names));
	if (index != NONE)
		return index;
	problem(file, line, "there is no game data input", name);
	return NONE;
}

static long hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return NONE;
}

/* "#RRGGBB" or "#AARRGGBB" */
static boolean parse_color(char const *text, real_argb_color *color)
{
	long length = text ? (long)strlen(text) : 0;
	long components[4] = { 255, 0, 0, 0 };
	long index;

	if ((length != 7 && length != 9) || text[0] != '#')
		return FALSE;
	for (index = 0; index < (length - 1) / 2; index++)
	{
		long high = hex_digit(text[1 + 2 * index]), low = hex_digit(text[2 + 2 * index]);

		if (high == NONE || low == NONE)
			return FALSE;
		components[length == 7 ? index + 1 : index] = high * 16 + low;
	}
	color->alpha = components[0] / 255.0f;
	color->red = components[1] / 255.0f;
	color->green = components[2] / 255.0f;
	color->blue = components[3] / 255.0f;
	return TRUE;
}

/* "top left bottom right" */
static void parse_bounds(char const *text, rectangle2d *bounds, char const *file, long line)
{
	long top, left, bottom, right;
	char extra;

	if (text && sscanf(text, "%ld %ld %ld %ld %c", &top, &left, &bottom, &right, &extra) == 4)
	{
		bounds->y0 = (short)top;
		bounds->x0 = (short)left;
		bounds->y1 = (short)bottom;
		bounds->x1 = (short)right;
	}
	else if (text)
	{
		problem(file, line, "bounds are \"top left bottom right\":", text);
	}
}

/* a 'ustr' of the strings; each with an index in the list `map` (the
Custom Edition's in its language, or NULL) given by maps is its string
there */
static void *string_list_build(char const *const *strings, long const *maps, long count,
	struct custom_edition_string_list const *map)
{
	struct string_list *list = allocate(sizeof(struct string_list));
	struct string_list_entry *entries = allocate(count * sizeof(struct string_list_entry));
	long index;

	if (!list || !entries)
		return list;
	list->strings.count = count;
	list->strings.address = entries;
	for (index = 0; index < count; index++)
	{
		long length = (long)strlen(strings[index]);
		unsigned short *characters;
		long written;

		if (map && maps[index] != HALO_MENU_NONE && maps[index] < map->count)
		{
			length = map->lengths[maps[index]];
			characters = allocate(length * sizeof(unsigned short));
			if (!characters)
				return list;
			memcpy(characters, map->strings[maps[index]], length * sizeof(unsigned short));
			entries[index].string.size = length * (long)sizeof(unsigned short);
			entries[index].string.address = characters;
			build.map_strings++;
			continue;
		}
		characters = allocate((length * 2 + 2) * sizeof(unsigned short));
		if (!characters)
			return list;
		written = halo_menus_utf16(strings[index], characters, length * 2 + 2);
		entries[index].string.size = written * (long)sizeof(unsigned short);
		entries[index].string.address = characters;
	}
	return list;
}

/* the "|"-separated pieces of text, copied (at most MAXIMUM_STRINGS) */
static long split(char const *text, char const **pieces)
{
	long count = 0;

	while (text && count < MAXIMUM_STRINGS)
	{
		long length = (long)strcspn(text, "|");
		char *piece = allocate(length + 1);

		if (!piece)
			break;
		memcpy(piece, text, length);
		pieces[count++] = piece;
		if (!text[length])
			break;
		text += length + 1;
	}
	return count;
}

/* a spinner's strings, or its values (count of them in pieces), with those
the platform adds: Video Setup's
sizes are the display's (sdl_platform.c), shown "1920 x 1080" and set
"1920x1080": its resolutions after Resolution's own (NATIVE), and the
window sizes that fit it in place of Window Size's own; its refresh rates
after Refresh Rate's own (AUTO); and Audio Setup's
devices SDL finds, after their SYSTEM DEFAULT, each set by its name and
shown by it (in capitals, as the menus' words are, and cut short) */
static long spinner_expand(char const *setting, boolean values, char const **pieces, long count)
{
	long widths[MAXIMUM_STRINGS], heights[MAXIMUM_STRINGS];
	long added, index;

	/* (Frame Rate & Sync's Variable Refresh: its AUTO shown as what auto
	does here as the menus are made, AUTO: ON or AUTO: OFF, where the system
	is asked (sdl_platform.c's platform_vrr_detected); every choice SET BY
	MACOS on the Mac, where the system sets it: its row's condition in the
	model dims it, menu_functions.c's settings_rows_conditions) */
	if (!values && setting && !strcmp(setting, "display.vrr"))
	{
		int vrr = platform_vrr_detected();

		for (index = 0; index < count; index++)
		{
			if (vrr == 3 /* PLATFORM_VRR_SYSTEM */)
				pieces[index] = "SET BY MACOS";
			else if (!strcmp(pieces[index], "AUTO") && (vrr == 0 || vrr == 1))
				pieces[index] = vrr ? "AUTO: ON" : "AUTO: OFF";
		}
		return count;
	}

	if (setting && (!strcmp(setting, "audio.output_device") ||
		!strcmp(setting, "audio.input_device")))
	{
		static char names[MAXIMUM_STRINGS][128];

		added = platform_audio_devices(!strcmp(setting, "audio.input_device"), names,
			(int)(MAXIMUM_STRINGS - count));
		for (index = 0; index < added; index++)
		{
			char shown[sizeof(names[0])];
			char const *name = names[index];
			char *piece;
			long length;

			/* (the whole name, in capitals: a row too long for it cuts it
			short and scrolls it while it has the focus, and the panel wraps
			it, issue 899) */
			if (!values)
			{
				long at;

				for (at = 0; name[at] && at < (long)sizeof(shown) - 1; at++)
					shown[at] = name[at] >= 'a' && name[at] <= 'z' ? (char)(name[at] - 'a' + 'A') : name[at];
				shown[at] = 0;
				name = shown;
			}
			length = (long)strlen(name);
			piece = allocate(length + 1);
			if (!piece)
				break;
			strcpy(piece, name);
			pieces[count++] = piece;
		}
		return count;
	}
	if (setting && !strcmp(setting, "display.refresh_rate"))
	{
		/* (the display's rates after Refresh Rate's own, AUTO: shown "144 HZ",
		set "144") */
		added = platform_display_refresh_rates(widths, (int)(MAXIMUM_STRINGS - count));
		for (index = 0; index < added; index++)
		{
			char text[32];
			char *piece;

			snprintf(text, sizeof(text), values ? "%ld" : "%ld HZ", widths[index]);
			piece = allocate((long)strlen(text) + 1);
			if (!piece)
				break;
			strcpy(piece, text);
			pieces[count++] = piece;
		}
		return count;
	}
	if (setting && !strcmp(setting, "display.resolution"))
		added = platform_display_resolutions(widths, heights, (int)(MAXIMUM_STRINGS - count));
	else if (setting && !strcmp(setting, "display.window_size"))
	{
		added = platform_window_sizes(widths, heights, MAXIMUM_STRINGS);
		if (added > 0)
			count = 0;
	}
	else
		return count;
	for (index = 0; index < added; index++)
	{
		char text[32];
		char *piece;

		snprintf(text, sizeof(text), values ? "%ldx%ld" : "%ld x %ld", widths[index], heights[index]);
		piece = allocate((long)strlen(text) + 1);
		if (!piece)
			break;
		strcpy(piece, text);
		pieces[count++] = piece;
	}
	return count;
}

/* a spinner's strings, or its values, split, and expanded so */
static long spinner_split(struct halo_menu_widget const *widget, boolean values, char const **pieces)
{
	long count = split(values ? widget->values : widget->strings, pieces);

	return spinner_expand(widget->setting, values, pieces, count);
}

/* a setting's values as the menus' spinners have them (the menu engine's
model's rows', menu_functions.c's settings_row_setting): those given, and
those the platform adds as the menus are made (spinner_expand: the
displays' sizes and rates, the audio devices) */
long pc_menu_setting_values(
	char const *setting,
	char const **values,
	long count)
{
	return spinner_expand(setting, TRUE, values, count);
}

/* a tag's value in a table of them (MENU_TAG_VALUES at most: past that,
none) */
static void tag_value_set(struct menu_tag_value *values, long *count, long tag_index, short value)
{
	long index;

	for (index = 0; index < *count; index++)
	{
		if (values[index].tag_index == tag_index)
		{
			values[index].value = value;
			return;
		}
	}
	if (*count >= MENU_TAG_VALUES)
		return;
	values[*count].tag_index = tag_index;
	values[*count].value = value;
	(*count)++;
}

static short tag_value_get(struct menu_tag_value const *values, long count, long tag_index)
{
	long index;

	for (index = 0; tag_index != NONE && index < count; index++)
	{
		if (values[index].tag_index == tag_index)
			return values[index].value;
	}
	return 0;
}

static void *bitmap_build(struct halo_menu_bitmap const *source, long tag_index)
{
	struct bitmap_group *group = allocate(sizeof(struct bitmap_group));
	struct bitmap_group_sequence *sequence = allocate(sizeof(struct bitmap_group_sequence));
	struct bitmap_data *bitmaps = allocate(source->frame_count * sizeof(struct bitmap_data));
	long frame;

	if (build.failed)
		return group;
	if (!source->frame_count)
	{
		problem(source->file, source->line, "a bitmap has no frames:", source->name);
		return group;
	}
	sequence->first_bitmap_index = 0;
	sequence->bitmap_count = (short)source->frame_count;
	group->sequences.count = 1;
	group->sequences.address = sequence;
	group->bitmaps.count = source->frame_count;
	group->bitmaps.address = bitmaps;
	for (frame = 0; frame < source->frame_count; frame++)
	{
		struct halo_menu_frame const *data = &build.menus->frames[source->first_frame + frame];
		struct bitmap_data *bitmap = &bitmaps[frame];
		char const *png = data->png;
		long width = data->width, height = data->height;

		/* (a frame of the map's: a copy of its bitmap, which the texture cache
		loads from the map as its own; in a game map that has not got it,
		nothing) */
		if (data->map && build.game_map && tag_loaded(BITMAP_GROUP_TAG, data->map) == NONE)
		{
			png = BLANK_PNG;
			width = height = PLACEHOLDER_SIZE;
		}
		else if (data->map)
		{
			long group_index = map_tag(BITMAP_GROUP_TAG, data->map, source->file, source->line);
			struct bitmap_group *group_source;

			if (group_index == NONE)
				return group;
			group_source = bitmap_group_get(group_index);
			if (data->index < 0 || data->index >= group_source->bitmaps.count)
			{
				problem(source->file, source->line, "the map's bitmap has no such frame:", data->map);
				return group;
			}
			memcpy(bitmap, (struct bitmap_data *)group_source->bitmaps.address + data->index, sizeof(*bitmap));
			bitmap->cache_block_index = NONE;
			bitmap->base_address = NULL;
			/* (scaled to a size of the file's: the texture stays the map's,
			drawn smaller or larger, ui_widget.c) */
			if (data->width > 0 && data->height > 0 && data->width <= 2048 && data->height <= 2048)
			{
				struct menu_frame_placement *placements = realloc(menu_tags.placements,
					(menu_tags.placement_count + 1) * sizeof(*menu_tags.placements));

				if (placements)
				{
					menu_tags.placements = placements;
					placements[menu_tags.placement_count].bitmap = bitmap;
					placements[menu_tags.placement_count].x = (short)PIN(data->x, -2048, 2048);
					placements[menu_tags.placement_count].y = (short)PIN(data->y, -2048, 2048);
					placements[menu_tags.placement_count].width = (short)data->width;
					placements[menu_tags.placement_count].height = (short)data->height;
					placements[menu_tags.placement_count].source_tag_index = group_index;
					placements[menu_tags.placement_count].source_frame = data->index;
					menu_tags.placement_count++;
				}
			}
			continue;
		}

		if (width <= 0 || height <= 0 || width > 2048 || height > 2048)
		{
			problem(source->file, source->line, "a frame is 1 to 2048 wide and high:", png);
			return group;
		}
		/* (its D3D texture is a small one, which its PNG is drawn for: the
		game draws a bitmap in units of its size, to whatever texture it has) */
		bitmap->signature = BITMAP_GROUP_TAG;
		bitmap->width = PLACEHOLDER_SIZE;
		bitmap->height = PLACEHOLDER_SIZE;
		bitmap->depth = 1;
		bitmap->type = _bitmap_type_2d;
		bitmap->format = _bitmap_format_a8r8g8b8;
		bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit);
		bitmap->tag_index = tag_index;
		bitmap->cache_block_index = NONE;
		if (!rasterizer_bitmap_new(bitmap))
		{
			problem(source->file, source->line, "could not make the texture of bitmap", source->name);
			return group;
		}
		bitmap->width = (short)width;
		bitmap->height = (short)height;
		{
			struct bitmap_data **bitmaps = realloc(menu_tags.bitmaps,
				(menu_tags.bitmap_count + 1) * sizeof(*menu_tags.bitmaps));

			if (!bitmaps)
			{
				rasterizer_bitmap_delete(bitmap);
				problem(source->file, source->line, "out of memory for the bitmaps of", source->name);
				return group;
			}
			menu_tags.bitmaps = bitmaps;
		}
		menu_tags.bitmaps[menu_tags.bitmap_count++] = bitmap;
		/* (none without a renderer: debug.null_renderer) */
		if (bitmap->hardware_format)
			halo_menus_art_register(bitmap->hardware_format, png);
	}
	return group;
}

/* ---------- the art decoded ahead

A screen's art (its menus' PNGs, menu_files.c) is decoded on threads ahead
of its first draw: as a screen is shown, the art of each screen its
widgets open (and its own, if it is not yet), through its children,
conditional widgets and descriptions, so that the screen chosen next opens
with its art decoded. The menus' own bitmaps only: the maps' are
the game's. */

/* the most screens a screen shown looks through: itself, those it opens */
#define ART_AHEAD_SCREENS 64
/* the deepest a screen's widgets go */
#define ART_AHEAD_DEPTH 16

static void art_ahead_bitmap(struct tag_reference const *reference)
{
	struct bitmap_group *group;
	long index;

	if (reference->index == NONE || tag_get_group_tag(reference->index) != BITMAP_GROUP_TAG)
		return;
	group = bitmap_group_get(reference->index);
	for (index = 0; index < group->bitmaps.count; index++)
	{
		struct bitmap_data *bitmap = (struct bitmap_data *)group->bitmaps.address + index;

		if (bitmap->hardware_format)
			halo_menus_art_ahead(bitmap->hardware_format);
	}
}

/* a widget's art, and its descendants'; the screens its handlers open,
added to screens (when opens) */
static void art_ahead_widget(long tag_index, short depth, boolean opens, long *screens, short *screen_count)
{
	struct ui_widget_definition *definition;
	long index;

	if (tag_index == NONE || depth > ART_AHEAD_DEPTH || tag_get_group_tag(tag_index) != UI_WIDGET_DEFINITION_TAG)
		return;
	definition = (struct ui_widget_definition *)tag_get(UI_WIDGET_DEFINITION_TAG, tag_index);
	art_ahead_bitmap(&definition->background_bitmap);
	art_ahead_bitmap(&definition->list_header_bitmap);
	art_ahead_bitmap(&definition->list_footer_bitmap);
	for (index = 0; index < definition->child_widgets.count; index++)
	{
		art_ahead_widget(((struct ui_widget_child_reference *)definition->child_widgets.address)[index].widget_tag.index,
			(short)(depth + 1), opens, screens, screen_count);
	}
	for (index = 0; index < definition->conditional_widgets.count; index++)
	{
		art_ahead_widget(
			((struct ui_widget_conditional_reference *)definition->conditional_widgets.address)[index].widget_tag.index,
			(short)(depth + 1), opens, screens, screen_count);
	}
	art_ahead_widget(definition->extended_description_widget.index, (short)(depth + 1), opens, screens,
		screen_count);
	for (index = 0; opens && index < definition->event_handlers.count; index++)
	{
		long screen = ((struct ui_widget_event_handler_reference *)definition->event_handlers.address)[index].widget_tag.index;
		short other;

		for (other = 0; other < *screen_count && screens[other] != screen; other++)
			;
		if (screen != NONE && other == *screen_count && *screen_count < ART_AHEAD_SCREENS)
			screens[(*screen_count)++] = screen;
	}
}

void pc_menu_screen_shown(long tag_index)
{
	long screens[ART_AHEAD_SCREENS];
	short screen_count = 0, screen;

	/* (the screen's own art first: drawn now; then those it opens) */
	art_ahead_widget(tag_index, 0, TRUE, screens, &screen_count);
	for (screen = 0; screen < screen_count; screen++)
		art_ahead_widget(screens[screen], 0, FALSE, NULL, NULL);
}

/* adds a conditional widget to the definition, if it has not got it */
static void conditional_add(struct ui_widget_definition *definition, long tag_index, long flags)
{
	struct ui_widget_conditional_reference *conditionals =
		(struct ui_widget_conditional_reference *)definition->conditional_widgets.address;
	struct ui_widget_conditional_reference *grown;
	long count = definition->conditional_widgets.count, index;

	for (index = 0; index < count; index++)
	{
		if (conditionals[index].widget_tag.index == tag_index)
			return;
	}
	grown = allocate((count + 1) * sizeof(*grown));
	if (!grown)
		return;
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (count)
		memcpy(grown, conditionals, count * sizeof(*grown));
	reference_set(&grown[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
	grown[count].flags = flags;
	definition->conditional_widgets.address = grown;
	definition->conditional_widgets.count = count + 1;
}

static void handler_build(struct ui_widget_event_handler_reference *handler, struct halo_menu_handler const *source,
	long event_type, struct ui_widget_definition *definition)
{
	char const *targets[5];
	long target_count = 0;

	handler->event_type = (short)event_type;
	reference_clear(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG);
	reference_clear(&handler->sound_effect, SOUND_TAG);
	if (source->run)
	{
		handler->function = (short)function_index(source->run, source->file, source->line);
		SET_FLAG(handler->flags, _event_handler_run_function_bit, TRUE);
	}
	if (source->script)
	{
		strncpy(handler->script, source->script, sizeof(handler->script) - 1);
		SET_FLAG(handler->flags, _event_handler_run_scenario_script_bit, TRUE);
	}
	else if (source->label)
	{
		strncpy(handler->script, source->label, sizeof(handler->script) - 1);
	}
	if (source->open)
	{
		targets[target_count++] = source->open;
		SET_FLAG(handler->flags, _event_handler_open_widget_bit, TRUE);
	}
	if (source->replace)
	{
		targets[target_count++] = source->replace;
		SET_FLAG(handler->flags, _event_handler_replace_self_with_widget_bit, TRUE);
	}
	if (source->focus)
	{
		targets[target_count++] = source->focus;
		SET_FLAG(handler->flags, _event_handler_give_focus_to_widget_bit, TRUE);
	}
	if (source->back)
		SET_FLAG(handler->flags, _event_handler_go_back_to_previous_widget_bit, TRUE);
	if (source->branch)
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
	if (source->close)
	{
		if (!strcmp(source->close, "current"))
			SET_FLAG(handler->flags, _event_handler_close_current_widget_bit, TRUE);
		else if (!strcmp(source->close, "all"))
			SET_FLAG(handler->flags, _event_handler_close_all_widgets_bit, TRUE);
		else if (!strcmp(source->close, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_close_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "close is \"current\", \"all\" or \"other\" (with widget=)",
				source->close);
	}
	if (source->reload)
	{
		if (!strcmp(source->reload, "self"))
			SET_FLAG(handler->flags, _event_handler_reload_self_bit, TRUE);
		else if (!strcmp(source->reload, "other") && source->widget)
			SET_FLAG(handler->flags, _event_handler_reload_other_widget_bit, TRUE);
		else
			problem(source->file, source->line, "reload is \"self\" or \"other\" (with widget=)", source->reload);
	}
	if (source->widget)
		targets[target_count++] = source->widget;
	if (target_count > 1)
	{
		long index;

		/* (several flags may share the one widget) */
		for (index = 1; index < target_count; index++)
		{
			if (strcmp(targets[index], targets[0]))
				problem(source->file, source->line, "a handler names one widget (open, replace, focus or widget)",
					NULL);
		}
	}
	if (target_count)
	{
		reference_set(&handler->widget_tag, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, targets[0], source->file, source->line));
	}
	if (source->sound)
		reference_set(&handler->sound_effect, SOUND_TAG, map_tag(SOUND_TAG, source->sound, source->file, source->line));
	if (source->otherwise)
	{
		if (!source->run)
			problem(source->file, source->line, "otherwise needs run (the function whose failure it is for)", NULL);
		SET_FLAG(handler->flags, _event_handler_try_to_branch_on_failure_bit, TRUE);
		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, source->otherwise, source->file, source->line), 1);
	}
}

static void setting_add(struct halo_menu_widget const *source, long definition_index)
{
	struct pc_menu_setting *setting;
	struct pc_menu_setting *settings = realloc(menu_tags.settings,
		(menu_tags.setting_count + 1) * sizeof(*menu_tags.settings));

	if (!settings)
	{
		problem(source->file, source->line, "out of memory for the settings of", source->name);
		return;
	}
	menu_tags.settings = settings;
	setting = &menu_tags.settings[menu_tags.setting_count++];
	memset(setting, 0, sizeof(*setting));
	setting->definition_index = definition_index;
	setting->setting = source->setting;
	setting->loaded_index = NONE;
	setting->value_count = spinner_split(source, TRUE, setting->values);
}

/* a font: large, small, terminal, heading (the port's: large where it has
none), or the map's by its path */
static long font_tag(char const *font, char const *file, long line)
{
	char const *path;
	long index;

	if (!strcmp(font, "heading") && build.heading_tag != NONE)
		return build.heading_tag;
	path = !strcmp(font, "large") || !strcmp(font, "heading") ? "ui\\large_ui" :
		!strcmp(font, "small") ? "ui\\small_ui" :
		!strcmp(font, "terminal") ? "ui\\interstate" : font;
	index = map_tag(FONT_TAG, path, file, line);

	return index == NONE && build.game_map ? build.map_font : index;
}

/* the font of the game map's pause menu's RESUME GAME, else NONE */
static long pause_menu_font(void)
{
	static char const *const buttons[] =
	{
		"ui\\shell\\solo_game\\pause_game\\resume_game_button",
		"ui\\shell\\multiplayer_game\\pause_game\\resume_game_button",
	};
	long button;

	for (button = 0; button < NUMBEROF(buttons); button++)
	{
		long index = tag_loaded(UI_WIDGET_DEFINITION_TAG, buttons[button]);

		if (index != NONE)
		{
			struct ui_widget_definition const *definition = tag_get(UI_WIDGET_DEFINITION_TAG, index);

			if (definition->text_font.index != NONE)
				return definition->text_font.index;
		}
	}
	return NONE;
}

static void *widget_build(long widget_index)
{
	struct halo_menus const *menus = build.menus;
	struct halo_menu_widget const *source = &menus->widgets[widget_index];
	struct ui_widget_definition *definition = allocate(sizeof(struct ui_widget_definition));
	char const *leaf = strrchr(source->name, '/') ? strrchr(source->name, '/') + 1 : source->name;
	long child, handler, input, conditional, replace, count;

	if (!definition)
		return NULL;
	strncpy(definition->name, leaf, sizeof(definition->name) - 1);
	definition->type = _widget_type_container;
	if (source->type)
	{
		long type = name_index(source->type, widget_type_names, NUMBEROF(widget_type_names));

		if (type == NONE)
			problem(source->file, source->line, "there is no widget type", source->type);
		else
			definition->type = (short)type;
	}
	definition->controller_index = 4;
	if (source->controller && strcmp(source->controller, "any"))
	{
		if (strlen(source->controller) != 1 || source->controller[0] < '1' || source->controller[0] > '4')
			problem(source->file, source->line, "controller is 1 to 4, or \"any\":", source->controller);
		else
			definition->controller_index = (short)(source->controller[0] - '1');
	}
	definition->bounds.y0 = (short)source->top;
	definition->bounds.x0 = (short)source->left;
	definition->bounds.y1 = (short)(source->top + (source->has_height ? source->height : 480));
	definition->bounds.x1 = (short)(source->left + (source->has_width ? source->width : 640));
	definition->flags = flags_parse(source->flags, widget_flag_names, NUMBEROF(widget_flag_names),
		source->file, source->line);
	/* (in a network game the clock runs on) */
	if (build.game_map && !build.campaign)
		definition->flags &= ~FLAG(1); /* pause_game */
	definition->milliseconds_to_auto_close = source->auto_close;
	definition->auto_close_fade_time = source->auto_close_fade;
	reference_clear(&definition->background_bitmap, BITMAP_GROUP_TAG);
	if (source->bitmap)
	{
		char touch[HALO_MENU_TOUCH_NAME];

		/* (on a touchscreen, its <bitmap>_touch where the menus have one, as
		a look's picture is: look_make) */
		snprintf(touch, sizeof(touch), "%s_touch", source->bitmap);
		reference_set(&definition->background_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, ui_widget_port_foot_lift() && !strchr(source->bitmap, '\\') &&
				bitmap_named(touch) != NONE ? touch : source->bitmap, source->file, source->line));
	}
	/* the game data inputs */
	for (count = 0, input = source->first_input; input != HALO_MENU_NONE; input = menus->inputs[input].next)
		count++;
	if (count)
	{
		struct ui_widget_game_data_input_reference *inputs = allocate(count * sizeof(*inputs));

		definition->game_data_inputs.count = count;
		definition->game_data_inputs.address = inputs;
		for (count = 0, input = source->first_input; inputs && input != HALO_MENU_NONE; input = menus->inputs[input].next)
		{
			struct halo_menu_input const *data = &menus->inputs[input];

			inputs[count++].function = (short)game_data_input_index(data->input, data->file, data->line);
		}
	}
	/* search and replace */
	for (count = 0, replace = source->first_replace; replace != HALO_MENU_NONE; replace = menus->replaces[replace].next)
		count++;
	if (count)
	{
		struct ui_widget_search_and_replace_reference *replaces = allocate(count * sizeof(*replaces));

		definition->search_and_replace_functions.count = count;
		definition->search_and_replace_functions.address = replaces;
		for (count = 0, replace = source->first_replace; replaces && replace != HALO_MENU_NONE;
			replace = menus->replaces[replace].next)
		{
			struct halo_menu_replace const *item = &menus->replaces[replace];
			long function = item->function ? name_index(item->function, replace_names, NUMBEROF(replace_names)) : 0;

			if (function == NONE)
				problem(item->file, item->line, "a replace function is none or controller:", item->function);
			strncpy(replaces[count].search_string, item->search, sizeof(replaces[count].search_string) - 1);
			/* (the PC version's own are none here) */
			replaces[count++].replace_function = (short)(function == 1 ? 1 : 0);
		}
	}
	/* the text */
	reference_clear(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG);
	reference_clear(&definition->text_font, FONT_TAG);
	if ((source->text != NULL) + (source->strings != NULL) + (source->string_list != NULL) > 1)
		problem(source->file, source->line, "a widget has one of text, strings and string_list:", source->name);
	if (build.text_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.text_tags[widget_index]);
	}
	else if (build.spinner_tags[widget_index] != NONE)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, build.spinner_tags[widget_index]);
		definition->list_flags |= FLAG(_list_items_generated_from_string_list_tag_bit);
	}
	else if (source->string_list)
	{
		reference_set(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG,
			tag_named(UNICODE_STRING_LIST_TAG, source->string_list, source->file, source->line));
	}
	definition->string_list_index = (short)source->string_index;
	/* (our own text is white in the large font, unless given) */
	if (source->text || source->strings)
	{
		definition->text_color.alpha = definition->text_color.red = 1.0f;
		definition->text_color.green = definition->text_color.blue = 1.0f;
		if (!source->font)
			reference_set(&definition->text_font, FONT_TAG, font_tag("large", source->file, source->line));
	}
	if (source->color && !parse_color(source->color, &definition->text_color))
		problem(source->file, source->line, "a color is #RRGGBB or #AARRGGBB:", source->color);
	if (source->font)
		reference_set(&definition->text_font, FONT_TAG, font_tag(source->font, source->file, source->line));
	if (source->align)
	{
		long align = name_index(source->align, align_names, NUMBEROF(align_names));

		if (align == NONE)
			problem(source->file, source->line, "align is \"left\", \"right\" or \"center\":", source->align);
		else
			definition->justification = (short)align;
	}
	definition->text_box_flags = (word)flags_parse(source->text_flags, text_flag_names, NUMBEROF(text_flag_names),
		source->file, source->line);
	definition->horizontal_offset = (short)source->text_x;
	definition->vertical_offset = (short)source->text_y;
	parse_bounds(source->header_bounds, &definition->list_header_bounds, source->file, source->line);
	parse_bounds(source->footer_bounds, &definition->list_footer_bounds, source->file, source->line);
	/* its wide layout: "dx dw" (ui_widget_port_wide_layout) */
	if (source->wide)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[widget_index]) - menu_tags.original_count;
		long dx, dw;
		char end;

		if (sscanf(source->wide, "%ld %ld %c", &dx, &dw, &end) != 2 || dx < -2 * WIDE_LAYOUT_EXTRA ||
			dx > 2 * WIDE_LAYOUT_EXTRA || dw < -2 * WIDE_LAYOUT_EXTRA || dw > 2 * WIDE_LAYOUT_EXTRA)
		{
			problem(source->file, source->line, "wide is \"dx dw\", each -424 to 424:", source->wide);
		}
		else if (index >= 0 && index < menu_tags.wide_count)
		{
			menu_tags.wides[index].definition = definition;
			menu_tags.wides[index].dx = (short)dx;
			menu_tags.wides[index].dw = (short)dw;
			menu_tags.wides[index].x1 = definition->bounds.x1;
			menu_tags.wides[index].footer_x0 = definition->list_footer_bounds.x0;
			menu_tags.wides[index].footer_x1 = definition->list_footer_bounds.x1;
		}
	}
	/* the list */
	definition->list_flags |= flags_parse(source->list_flags, list_flag_names, NUMBEROF(list_flag_names),
		source->file, source->line);
	if (source->strings && (definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE))
		problem(source->file, source->line, "strings are for a spinner with no children:", source->name);
	if (TEST_FLAG(definition->list_flags, _list_items_generated_from_string_list_tag_bit) &&
		(definition->type != _widget_type_spinner_list || source->first_child != HALO_MENU_NONE ||
			definition->text_label_string_list.index == NONE))
		problem(source->file, source->line, "items_from_strings is for a spinner with strings and no children:",
			source->name);
	if (source->setting)
	{
		if (!source->strings || !source->values)
			problem(source->file, source->line, "a setting needs strings and values:", source->name);
		else
			setting_add(source, build.widget_tags[widget_index]);
	}
	reference_clear(&definition->list_header_bitmap, BITMAP_GROUP_TAG);
	reference_clear(&definition->list_footer_bitmap, BITMAP_GROUP_TAG);
	if (source->header_bitmap)
	{
		reference_set(&definition->list_header_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->header_bitmap, source->file, source->line));
	}
	if (source->footer_bitmap)
	{
		reference_set(&definition->list_footer_bitmap, BITMAP_GROUP_TAG,
			tag_named(BITMAP_GROUP_TAG, source->footer_bitmap, source->file, source->line));
	}
	reference_clear(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG);
	if (source->description)
	{
		if (definition->type != _widget_type_column_list)
			problem(source->file, source->line, "a description is for a column list:", source->name);
		reference_set(&definition->extended_description_widget, UI_WIDGET_DEFINITION_TAG,
			tag_named(UI_WIDGET_DEFINITION_TAG, source->description, source->file, source->line));
	}
	/* the children */
	for (count = 0, child = source->first_child; child != HALO_MENU_NONE; child = menus->children[child].next)
		count++;
	if (definition->type == _widget_type_spinner_list && count != 0 && count != 1 && count != 3)
		problem(source->file, source->line, "a spinner has 0, 1 or 3 children:", source->name);
	if (count)
	{
		struct ui_widget_child_reference *children = allocate(count * sizeof(*children));

		definition->child_widgets.count = count;
		definition->child_widgets.address = children;
		for (count = 0, child = source->first_child; children && child != HALO_MENU_NONE; child = menus->children[child].next)
		{
			struct halo_menu_child const *entry = &menus->children[child];
			long tag_index = entry->nested != HALO_MENU_NONE ? build.widget_tags[entry->nested] :
				tag_named(UI_WIDGET_DEFINITION_TAG, entry->widget, entry->file, entry->line);
			char const *name = entry->nested != HALO_MENU_NONE ? menus->widgets[entry->nested].name : entry->widget;
			char const *child_leaf = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;

			reference_set(&children[count].widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
			strncpy(children[count].name, child_leaf, sizeof(children[count].name) - 1);
			children[count].horizontal_offset = (short)entry->x;
			children[count].vertical_offset = (short)entry->y;
			if (entry->controller)
			{
				if (strlen(entry->controller) != 1 || entry->controller[0] < '1' || entry->controller[0] > '4')
					problem(entry->file, entry->line, "a child's controller is 1 to 4:", entry->controller);
				SET_FLAG(children[count].flags, _child_widget_use_custom_controller_index_bit, TRUE);
				children[count].custom_controller_index = (short)(entry->controller[0] - '1');
			}
			count++;
		}
	}
	/* the conditional widgets */
	for (conditional = source->first_conditional; conditional != HALO_MENU_NONE;
		conditional = menus->conditionals[conditional].next)
	{
		struct halo_menu_conditional const *item = &menus->conditionals[conditional];

		conditional_add(definition, tag_named(UI_WIDGET_DEFINITION_TAG, item->widget, item->file, item->line),
			item->if_failed ? 1 : 0);
	}
	/* the event handlers: one for each of each <on>'s events */
	for (count = 0, handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		count++;
	if (count)
	{
		long total = 0;

		/* (count the events) */
		for (handler = source->first_handler; handler != HALO_MENU_NONE; handler = menus->handlers[handler].next)
		{
			char const *text = menus->handlers[handler].event;

			while (text && *text)
			{
				long length;

				text += strspn(text, " \t\r\n");
				length = (long)strcspn(text, " \t\r\n");
				if (!length)
					break;
				total++;
				text += length;
			}
		}
		if (total)
		{
			struct ui_widget_event_handler_reference *handlers = allocate(total * sizeof(*handlers));

			definition->event_handlers.count = total;
			definition->event_handlers.address = handlers;
			for (count = 0, handler = source->first_handler; handlers && handler != HALO_MENU_NONE;
				handler = menus->handlers[handler].next)
			{
				struct halo_menu_handler const *on = &menus->handlers[handler];
				char const *text = on->event;

				while (text && *text)
				{
					char word[64];
					long length, event;

					text += strspn(text, " \t\r\n");
					length = (long)strcspn(text, " \t\r\n");
					if (!length)
						break;
					if (length >= (long)sizeof(word))
						length = sizeof(word) - 1;
					memcpy(word, text, length);
					word[length] = 0;
					text += strcspn(text, " \t\r\n");
					event = name_index(word, event_names, NUMBEROF(event_names));
					if (event == NONE)
						problem(on->file, on->line, "there is no event", word);
					handler_build(&handlers[count++], on, event, definition);
				}
			}
		}
	}
	return definition;
}

/* the tag table with count more entries, the existing ones as they are */
static struct cache_file_tag_instance *instances_grow(long count, long *first_index, long *first_salt)
{
	long existing, index, salt = 0;
	struct cache_file_tag_instance *instances = cache_files_tag_instances(&existing);
	struct cache_file_tag_instance *grown;

	if (!instances)
		return NULL;
	/* (every tag's absolute index fits a tag index's 16 bits, short of
	NONE's, and the table's size cannot wrap: the map's count was checked
	as it loaded, cache_files.c, and is again) */
	if (existing < 0 || count < 0 || existing > UNSIGNED_SHORT_MAX - count)
	{
		platform_log("menus: %ld tags and the map's %ld are too many for a tag table", count, existing);
		return NULL;
	}
	grown = allocate((existing + count) * sizeof(*grown));
	if (!grown)
		return NULL;
	memcpy(grown, instances, existing * sizeof(*grown));
	for (index = 0; index < existing; index++)
	{
		long instance_salt = (unsigned long)instances[index].tag_index >> 16;

		if (instance_salt > salt)
			salt = instance_salt;
	}
	menu_tags.original_instances = instances;
	menu_tags.original_count = existing;
	*first_index = existing;
	*first_salt = salt + 1;
	return grown;
}

static long next_tag(void)
{
	long tag_index = ((build.first_salt + build.next) << 16) | (build.first_index + build.next);

	build.next++;
	return tag_index;
}

static void instance_set(struct cache_file_tag_instance *instances, long group_tag, long tag_index, char const *name,
	char const *suffix, void *definition)
{
	struct cache_file_tag_instance *instance = &instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index)];
	char *copy = allocate((long)strlen(PC_MENU_TAG_PREFIX) + (long)strlen(name) + (long)strlen(suffix) + 1);

	if (copy)
	{
		char *character;

		strcpy(copy, PC_MENU_TAG_PREFIX);
		strcat(copy, name);
		strcat(copy, suffix);
		/* (named as the map's tags are) */
		for (character = copy; *character; character++)
		{
			if (*character == '/')
				*character = '\\';
		}
	}
	instance->group_tag = group_tag;
	instance->parent_group_tags[0] = NONE;
	instance->parent_group_tags[1] = NONE;
	instance->tag_index = tag_index;
	/* (the name is read by tag_loaded: of none, when the copy failed, which
	fails the build) */
	instance->name = copy ? copy : "";
	instance->base_address = definition;
}

/* the game's event handlers of each of the model's widgets' behaviours (of
a widget built: a game map's have few of ui.map's), an <on> for several
events a handler for each, as a widget's (widget_build) */
static void behaviours_build(struct cache_file_tag_instance *instances)
{
	struct halo_menu_model const *model = halo_menus_model();
	long index;

	if (!model || !model->behaviour_count)
		return;
	menu_tags.behaviour_handlers = allocate(model->behaviour_count * (long)sizeof(*menu_tags.behaviour_handlers));
	menu_tags.behaviour_handler_counts =
		allocate(model->behaviour_count * (long)sizeof(*menu_tags.behaviour_handler_counts));
	if (!menu_tags.behaviour_handlers || !menu_tags.behaviour_handler_counts)
		return;
	for (index = 0; index < model->behaviour_count && !build.failed; index++)
	{
		struct halo_menu_model_behaviour const *behaviour = &model->behaviours[index];
		long widget = widget_named(behaviour->name);
		struct ui_widget_definition *definition;
		struct ui_widget_event_handler_reference *handlers;
		long total = 0, handler;

		if (widget == NONE)
			continue;
		definition = instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[widget])].base_address;
		for (handler = 0; handler < behaviour->handler_count; handler++)
		{
			char const *text = model->handlers[behaviour->first_handler + handler].event;

			while (text && *text)
			{
				text += strspn(text, " \t\r\n");
				if (!*text)
					break;
				total++;
				text += strcspn(text, " \t\r\n");
			}
		}
		if (!total || !definition || (handlers = allocate(total * (long)sizeof(*handlers))) == NULL)
			continue;
		for (total = 0, handler = 0; handler < behaviour->handler_count; handler++)
		{
			struct halo_menu_handler const *on = &model->handlers[behaviour->first_handler + handler];
			char const *text = on->event;

			while (text && *text)
			{
				char word[64];
				long length, event;

				text += strspn(text, " \t\r\n");
				length = (long)strcspn(text, " \t\r\n");
				if (!length)
					break;
				if (length >= (long)sizeof(word))
					length = sizeof(word) - 1;
				memcpy(word, text, length);
				word[length] = 0;
				text += strcspn(text, " \t\r\n");
				event = name_index(word, event_names, NUMBEROF(event_names));
				if (event == NONE)
					problem(on->file, on->line, "there is no event", word);
				else
					handler_build(&handlers[total++], on, event, definition);
			}
		}
		menu_tags.behaviour_handlers[index] = handlers;
		menu_tags.behaviour_handler_counts[index] = total;
	}
	menu_tags.behaviour_model = model;
}

/* ---------- the menu engine's looks

Under display.menu_engine "new" the engine (menu_engine.c) draws the
widgets of the menus' files from the model (model/look, written by
tools/menu_look.py) rather than from their definitions: ui_widget.c finds a
widget's definition through menu_engine_definition, which gives it the one
made here from the widget's look. Its look (its type, box, wide layout,
place and effect flags, pictures, list flags, text and its style, and where
its children are) is the model's alone; what it does (its events, game data
inputs, conditionals, description, controller, closing, the other flags)
and what its children are stay its definition's until the engine runs the
screens' behaviour (docs/menu_engine.md, step 4). A widget whose look the
model has not got, or whose look names what is not there, is drawn from its
definition, and logged (look_missing): tools/menu_parity.py fails on it. */

/* the widget flags that are its look (widget_flag_names): flash_bitmap,
nifty_fx, anchor_left, anchor_right, stretch, keep_ends, foot and
above_foot */
#define LOOK_FLAGS (FLAG(2) | FLAG(13) | FLAG(WIDGET_FLAG_ANCHOR_LEFT) | FLAG(WIDGET_FLAG_ANCHOR_RIGHT) | \
	FLAG(WIDGET_FLAG_STRETCH) | FLAG(WIDGET_FLAG_KEEP_ENDS) | FLAG(WIDGET_FLAG_FOOT) | \
	FLAG(WIDGET_FLAG_ABOVE_FOOT))

/* a widget drawn from its definition, not its look, and why */
static void look_missing(char const *name, char const *why, char const *detail)
{
	platform_log("menu engine: look from the widget definition: %s (%s%s%s)", name, why, detail ? " " : "",
		detail ? detail : "");
}

/* a tag a look names: one of the menus' (pc\<name>, with suffix), or the
map's by its path; NONE if there is none */
static long look_tag(long group_tag, char const *name, char const *suffix)
{
	char path[300];
	long index;

	if (strchr(name, '\\'))
		return tag_loaded(group_tag, name);
	snprintf(path, sizeof(path), "%s%s%s", PC_MENU_TAG_PREFIX, name, suffix);
	for (index = 0; path[index]; index++)
	{
		if (path[index] == '/')
			path[index] = '\\';
	}
	return tag_loaded(group_tag, path);
}

/* a look's font, as font_tag finds it when the tags are built */
static long look_font(char const *font)
{
	char const *path;
	long index;

	if (!strcmp(font, "heading") && menu_tags.heading_tag != NONE)
		return menu_tags.heading_tag;
	path = !strcmp(font, "large") || !strcmp(font, "heading") ? "ui\\large_ui" :
		!strcmp(font, "small") ? "ui\\small_ui" :
		!strcmp(font, "terminal") ? "ui\\interstate" : font;
	index = tag_loaded(FONT_TAG, path);
	return index == NONE && menu_tags.game_map ? menu_tags.map_font : index;
}

/* a reference to what a look names (FALSE: it names what is not there,
which a game map may: ui.map's pictures) */
static boolean look_reference(struct tag_reference *reference, long group_tag, char const *name, char const *suffix)
{
	long index = name ? look_tag(group_tag, name, suffix) : NONE;

	reference_set(reference, group_tag, index);
	return !name || index != NONE || menu_tags.game_map;
}

static void look_box(rectangle2d *bounds, long const *box)
{
	bounds->x0 = (short)box[0];
	bounds->y0 = (short)box[1];
	bounds->x1 = (short)box[2];
	bounds->y1 = (short)box[3];
}

/* a widget's name as the menus' files and the model have it: a tag of
ours without pc\ and with slashes, else the map's tag's path */
static void look_widget_name(long tag_index, char *name, size_t size)
{
	char const *tag = tag_get_name(tag_index);
	size_t index;
	boolean ours = !strncmp(tag, PC_MENU_TAG_PREFIX, strlen(PC_MENU_TAG_PREFIX));

	if (ours)
		tag += strlen(PC_MENU_TAG_PREFIX);
	for (index = 0; tag[index] && index + 1 < size; index++)
		name[index] = ours && tag[index] == '\\' ? '/' : tag[index];
	name[index] = 0;
}

/* the definition's children, each where the look's <at> of its name (the
same one of those of its name) puts it */
static boolean look_children(struct ui_widget_definition *definition, struct ui_widget_definition const *tag,
	struct halo_menu_model const *model, struct halo_menu_model_look const *look)
{
	struct ui_widget_child_reference const *from = tag->child_widgets.address;
	struct ui_widget_child_reference *children;
	long child, place;

	if (!tag->child_widgets.count)
		return TRUE;
	children = allocate(tag->child_widgets.count * sizeof(*children));
	if (!children)
		return FALSE;
	for (child = 0; child < tag->child_widgets.count; child++)
	{
		char name[256];
		long before = 0, earlier;
		boolean placed = FALSE;

		children[child] = from[child];
		look_widget_name(from[child].widget_tag.index, name, sizeof(name));
		for (earlier = 0; earlier < child; earlier++)
			before += from[earlier].widget_tag.index == from[child].widget_tag.index;
		for (place = 0; place < look->place_count && !placed; place++)
		{
			struct halo_menu_model_place const *at = &model->places[look->first_place + place];

			if (!strcmp(at->widget, name) && before-- == 0)
			{
				children[child].horizontal_offset = (short)at->x;
				children[child].vertical_offset = (short)at->y;
				placed = TRUE;
			}
		}
		if (!placed)
		{
			look_missing(look->name, "no place for its child", name);
			return FALSE;
		}
	}
	definition->child_widgets.count = tag->child_widgets.count;
	definition->child_widgets.address = children;
	return TRUE;
}

/* the look's text and its style, as widget_build makes a widget's */
static boolean look_text(struct ui_widget_definition *definition, struct halo_menu_model_look const *look)
{
	boolean found = TRUE;

	reference_clear(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG);
	reference_clear(&definition->text_font, FONT_TAG);
	if (!look->has_text)
		return TRUE;
	/* (a widget's own text and a spinner's items are the strings the tags
	were built with for it, by its name) */
	if (look->text)
		found = look_reference(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, look->name, " text");
	else if (look->items)
	{
		found = look_reference(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, look->name,
			" strings");
		definition->list_flags |= FLAG(_list_items_generated_from_string_list_tag_bit);
	}
	else if (look->string_list)
		found = look_reference(&definition->text_label_string_list, UNICODE_STRING_LIST_TAG, look->string_list, "");
	definition->string_list_index = (short)look->string_index;
	/* (our own text is white in the large font, unless given) */
	if (look->text || look->items)
	{
		definition->text_color.alpha = definition->text_color.red = 1.0f;
		definition->text_color.green = definition->text_color.blue = 1.0f;
		if (!look->font)
			reference_set(&definition->text_font, FONT_TAG, look_font("large"));
	}
	if (look->color && !parse_color(look->color, &definition->text_color))
		found = FALSE;
	if (look->font)
		reference_set(&definition->text_font, FONT_TAG, look_font(look->font));
	if (look->align)
		definition->justification = (short)MAX(name_index(look->align, align_names, NUMBEROF(align_names)), 0);
	definition->text_box_flags = (word)flags_parse(look->text_flags, text_flag_names, NUMBEROF(text_flag_names),
		look->file, look->line);
	definition->horizontal_offset = (short)look->text_x;
	definition->vertical_offset = (short)look->text_y;
	/* its replaces: "<search> <function>", separated by "|" */
	if (look->replace)
	{
		char const *text = look->replace;
		long count = 1, index;
		struct ui_widget_search_and_replace_reference *replaces;

		for (index = 0; text[index]; index++)
			count += text[index] == '|';
		replaces = allocate(count * sizeof(*replaces));
		if (!replaces)
			return FALSE;
		for (index = 0; index < count; index++)
		{
			char search[64], function[64];
			long length = (long)strcspn(text, "|");
			long chosen;

			if (length >= (long)sizeof(search) || sscanf(text, "%63s %63[^|]", search, function) != 2)
				return FALSE;
			chosen = name_index(function, replace_names, NUMBEROF(replace_names));
			strncpy(replaces[index].search_string, search, sizeof(replaces[index].search_string) - 1);
			/* (the PC version's own are none here) */
			replaces[index].replace_function = (short)(chosen == 1 ? 1 : 0);
			text += length + (text[length] == '|');
		}
		definition->search_and_replace_functions.count = count;
		definition->search_and_replace_functions.address = replaces;
	}
	return found;
}

/* the menu engine's definition of a widget, made from its look in the model
and what its own definition does; FALSE if there is none (logged) */
static boolean look_make(struct menu_look *made, long tag_index)
{
	struct halo_menu_model const *model = halo_menus_model();
	struct halo_menu_model_look const *look = halo_menus_model_look(model, made->name);
	struct ui_widget_definition const *tag = tag_get(UI_WIDGET_DEFINITION_TAG, tag_index);
	struct ui_widget_definition *definition;
	long type;

	if (!look)
	{
		look_missing(made->name, "the model has no look of it", NULL);
		return FALSE;
	}
	definition = allocate(sizeof(*definition));
	if (!definition)
		return FALSE;
	/* what it does, and what its children are: its definition's */
	memcpy(definition->name, tag->name, sizeof(definition->name));
	definition->controller_index = tag->controller_index;
	definition->flags = tag->flags & ~LOOK_FLAGS;
	definition->milliseconds_to_auto_close = tag->milliseconds_to_auto_close;
	definition->auto_close_fade_time = tag->auto_close_fade_time;
	definition->game_data_inputs = tag->game_data_inputs;
	definition->event_handlers = tag->event_handlers;
	definition->conditional_widgets = tag->conditional_widgets;
	definition->extended_description_widget = tag->extended_description_widget;
	/* its look: the model's */
	type = look->type ? name_index(look->type, widget_type_names, NUMBEROF(widget_type_names)) : _widget_type_container;
	if (type == NONE)
	{
		look_missing(made->name, "its type is none of the widgets'", look->type);
		return FALSE;
	}
	definition->type = (short)type;
	if (look->has_box)
		look_box(&definition->bounds, look->box);
	else
	{
		definition->bounds.x1 = 640;
		definition->bounds.y1 = 480;
	}
	definition->flags |= flags_parse(look->place, widget_flag_names, NUMBEROF(widget_flag_names), look->file,
		look->line) & LOOK_FLAGS;
	definition->flags |= flags_parse(look->effects, widget_flag_names, NUMBEROF(widget_flag_names), look->file,
		look->line) & LOOK_FLAGS;
	/* (on a touchscreen, its picture the menus' <picture>_touch where they
	have one: the tall layout's drawn for its footer's lift; and above_foot,
	a widget that reaches down to the footer, the lift shorter) */
	if (!(look->picture && ui_widget_port_foot_lift() &&
			look_tag(BITMAP_GROUP_TAG, look->picture, "_touch") != NONE &&
			look_reference(&definition->background_bitmap, BITMAP_GROUP_TAG, look->picture, "_touch")) &&
		!look_reference(&definition->background_bitmap, BITMAP_GROUP_TAG, look->picture, ""))
	{
		look_missing(made->name, "there is no picture", look->picture);
		return FALSE;
	}
	if (definition->flags & FLAG(WIDGET_FLAG_ABOVE_FOOT))
		definition->bounds.y1 = (short)(definition->bounds.y1 - ui_widget_port_foot_lift());
	made->radius = (short)look->radius;
	if (!look_text(definition, look))
	{
		look_missing(made->name, "its text names what is not there", NULL);
		return FALSE;
	}
	definition->list_flags |= flags_parse(look->list, list_flag_names, NUMBEROF(list_flag_names), look->file,
		look->line);
	if (!look_reference(&definition->list_header_bitmap, BITMAP_GROUP_TAG, look->header, "") ||
		!look_reference(&definition->list_footer_bitmap, BITMAP_GROUP_TAG, look->footer, ""))
	{
		look_missing(made->name, "there is no header or footer picture", NULL);
		return FALSE;
	}
	if (look->has_header_box)
		look_box(&definition->list_header_bounds, look->header_box);
	if (look->has_footer_box)
		look_box(&definition->list_footer_bounds, look->footer_box);
	if (!look_children(definition, tag, model, look))
		return FALSE;
	/* its wide layout: "dx dw", as the menus' 640 have it, and as wide now
	(ui_widget_port_wide_layout); and past 16:9, "dx dw" */
	if (look->wide || look->past)
	{
		long dx = 0, dw = 0, past_dx = 0, past_dw = 0;
		char end;

		if (look->wide && (sscanf(look->wide, "%ld %ld %c", &dx, &dw, &end) != 2 || dx < -2 * WIDE_LAYOUT_EXTRA ||
			dx > 2 * WIDE_LAYOUT_EXTRA || dw < -2 * WIDE_LAYOUT_EXTRA || dw > 2 * WIDE_LAYOUT_EXTRA))
		{
			look_missing(made->name, "its wide is not \"dx dw\", each -424 to 424:", look->wide);
			return FALSE;
		}
		if (look->past && (sscanf(look->past, "%ld %ld %c", &past_dx, &past_dw, &end) != 2 ||
			past_dx < -2 * WIDE_LAYOUT_PAST_EXTRA || past_dx > 2 * WIDE_LAYOUT_PAST_EXTRA ||
			past_dw < -2 * WIDE_LAYOUT_PAST_EXTRA || past_dw > 2 * WIDE_LAYOUT_PAST_EXTRA))
		{
			look_missing(made->name, "its past is not \"dx dw\", each -572 to 572:", look->past);
			return FALSE;
		}
		made->wide.definition = definition;
		made->wide.dx = (short)dx;
		made->wide.dw = (short)dw;
		made->wide.past_dx = (short)past_dx;
		made->wide.past_dw = (short)past_dw;
		made->wide.x1 = definition->bounds.x1;
		made->wide.footer_x0 = definition->list_footer_bounds.x0;
		made->wide.footer_x1 = definition->list_footer_bounds.x1;
		wide_apply(&made->wide, menu_tags.wide_extra, menu_tags.past_extra);
	}
	/* its text's scale: "least most", each a half to three */
	made->text_scale_least = made->text_scale_most = 1.0f;
	if (look->text_scale)
	{
		float least, most;
		char end;

		if (sscanf(look->text_scale, "%f %f %c", &least, &most, &end) != 2 || least < 0.5f || least > 3.0f ||
			most < 0.5f || most > 3.0f)
		{
			look_missing(made->name, "its text's scale is not \"least most\", each 0.5 to 3:", look->text_scale);
			return FALSE;
		}
		made->text_scale_least = least;
		made->text_scale_most = most;
	}
	made->definition = definition;
	return TRUE;
}

/* the menu engine's definition of a widget of the menus' files (made the
first time it is asked for), NULL for any other tag or if it has none */
void *pc_menu_look_definition(
	long tag_index)
{
	long index;
	struct menu_look *made;

	if (!menu_tags.looks || tag_index == NONE)
		return NULL;
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) - menu_tags.original_count;
	if (index < 0 || index >= menu_tags.look_count || !menu_tags.looks[index].name)
		return NULL;
	made = &menu_tags.looks[index];
	if (!made->made)
	{
		made->made = TRUE;
		if (!look_make(made, tag_index))
			made->definition = NULL;
	}
	return made->definition;
}

static void menu_tags_release(void)
{
	long index;

	for (index = 0; index < menu_tags.bitmap_count; index++)
	{
		if (menu_tags.bitmaps[index]->hardware_format)
			rasterizer_bitmap_delete(menu_tags.bitmaps[index]);
	}
	halo_menus_art_forget();
	/* (the menus' models' tags, with ours: menu_models.c) */
	menu_models_tags_release();
	if (menu_tags.original_instances)
		cache_files_set_tag_instances(menu_tags.original_instances, menu_tags.original_count);
	/* (the game's free takes no NULL: settings is, with no setting spinners) */
	for (index = 0; index < menu_tags.block_count; index++)
		free(menu_tags.blocks[index]);
	if (menu_tags.blocks)
		free(menu_tags.blocks);
	if (menu_tags.bitmaps)
		free(menu_tags.bitmaps);
	if (menu_tags.placements)
		free(menu_tags.placements);
	if (menu_tags.settings)
		free(menu_tags.settings);
	memset(&menu_tags, 0, sizeof(menu_tags));
}

/* ---------- public code */

/* ---------- the menu engine's widgets' behaviour (menu_engine.c): the
model's (model/behaviour, tools/menu_behaviour.py), whose flags and <on>
handlers are those of the widgets' own, made into the game's as theirs are
(flags_parse, handler_build), as the widgets are built (behaviours_build) */

long menu_tags_behaviour_flags(
	struct halo_menu_model_behaviour const *behaviour)
{
	return flags_parse(behaviour->flags, widget_flag_names, NUMBEROF(widget_flag_names), behaviour->file,
		behaviour->line);
}

void const *menu_tags_behaviour_handlers(
	struct halo_menu_model const *model,
	struct halo_menu_model_behaviour const *behaviour,
	long *count)
{
	long index = (long)(behaviour - model->behaviours);

	*count = 0;
	if (!menu_tags.loaded || model != menu_tags.behaviour_model || index < 0 || index >= model->behaviour_count)
		return NULL;
	*count = menu_tags.behaviour_handler_counts[index];
	return menu_tags.behaviour_handlers[index];
}

/* the model's behaviours made (menu_tags_behaviour_handlers_at's indices) */
long menu_tags_behaviour_count(
	void)
{
	return menu_tags.loaded && menu_tags.behaviour_model && menu_tags.behaviour_handlers ?
		menu_tags.behaviour_model->behaviour_count : 0;
}

/* the handlers of the model's behaviour of the index, by which the engine
takes their widget's events, and the widget's name: saving_screens.c's,
which skips their saving screens as it does the widgets' own (NULL: none) */
void *menu_tags_behaviour_handlers_at(
	long index,
	long *count,
	char const **name)
{
	struct halo_menu_model const *model = menu_tags.behaviour_model;

	*count = 0;
	*name = NULL;
	if (index < 0 || index >= menu_tags_behaviour_count())
		return NULL;
	*count = menu_tags.behaviour_handler_counts[index];
	*name = model->behaviours[index].name;
	return menu_tags.behaviour_handlers[index];
}

/* the screen of the name (the menus' files', "main_menu/campaign/campaign_screen")
opened from the widget, as a handler's open opens it (its history, its sound):
the hand-written screens' actions (menu_screen.c); FALSE if there is no such
screen */
boolean menu_tags_open(
	struct widget_instance *widget,
	struct event_record *event,
	char const *screen,
	boolean *deleted)
{
	struct ui_widget_event_handler_reference handler;
	char name[256];
	long tag_index;
	size_t index;

	*deleted = FALSE;
	/* (a map's own screen by its tag's name, with its backslashes: the
	campaign's questions, which the pause menu opens) */
	snprintf(name, sizeof(name), strchr(screen, '\\') ? "%s" : PC_MENU_TAG_PREFIX "%s", screen);
	for (index = 0; name[index]; index++)
	{
		if (name[index] == '/')
			name[index] = '\\';
	}
	tag_index = tag_loaded(UI_WIDGET_DEFINITION_TAG, name);
	if (tag_index == NONE)
		return FALSE;
	memset(&handler, 0, sizeof(handler));
	reference_clear(&handler.sound_effect, SOUND_TAG);
	reference_set(&handler.widget_tag, UI_WIDGET_DEFINITION_TAG, tag_index);
	SET_FLAG(handler.flags, _event_handler_open_widget_bit, TRUE);
	ui_widget_port_handler_run(widget, event, &handler, deleted);
	return TRUE;
}

/* a widget definition's flags (its pause_game: whether a pause menu
pauses the game, menu_screen.c's menu_screen_pause_name), 0 for none */
long menu_tags_flags(
	long tag_index)
{
	struct ui_widget_definition const *definition = tag_index != NONE ?
		tag_get(UI_WIDGET_DEFINITION_TAG, tag_index) : NULL;

	return definition ? definition->flags : 0;
}

/* a widget's background's bitmap tag, NONE for none: the hand-written tall
screens' (menu_screen.c), which make the tall layout's where the screen they
take the place of has none */
long menu_tags_background(
	long tag_index)
{
	struct ui_widget_definition const *definition = tag_index != NONE ?
		tag_get(UI_WIDGET_DEFINITION_TAG, tag_index) : NULL;

	return definition ? definition->background_bitmap.index : NONE;
}

/* a widget's background made another's (a hand-written tall screen's of
background="tall": the tall layout's in place of the PC version's
gradient, whose band's line is lower, so that one band's line is drawn, as
the server browser's generated screen has it) */
void menu_tags_background_set(
	long tag_index,
	long background_tag_index)
{
	struct ui_widget_definition *definition = tag_index != NONE ?
		tag_get(UI_WIDGET_DEFINITION_TAG, tag_index) : NULL;
	struct ui_widget_definition const *background = background_tag_index != NONE ?
		tag_get(UI_WIDGET_DEFINITION_TAG, background_tag_index) : NULL;
	/* (and the menu engine's, which it draws: its look's) */
	struct ui_widget_definition *look = tag_index != NONE ? pc_menu_look_definition(tag_index) : NULL;

	if (!background)
		return;
	if (definition)
		definition->background_bitmap = background->background_bitmap;
	if (look)
		look->background_bitmap = background->background_bitmap;
}

/* the screen before the widget's gone back to, as a handler's back goes (its
history, its sound): the hand-written screens' actions (menu_screen.c) */
void menu_tags_back(
	struct widget_instance *widget,
	struct event_record *event,
	boolean *deleted)
{
	struct ui_widget_event_handler_reference handler;

	*deleted = FALSE;
	memset(&handler, 0, sizeof(handler));
	reference_clear(&handler.sound_effect, SOUND_TAG);
	reference_clear(&handler.widget_tag, UI_WIDGET_DEFINITION_TAG);
	SET_FLAG(handler.flags, _event_handler_go_back_to_previous_widget_bit, TRUE);
	ui_widget_port_handler_run(widget, event, &handler, deleted);
}

short menu_tags_handler_event(
	void const *handlers,
	long index)
{
	return ((struct ui_widget_event_handler_reference const *)handlers)[index].event_type;
}

void *menu_tags_handler(
	void const *handlers,
	long index)
{
	return (void *)&((struct ui_widget_event_handler_reference const *)handlers)[index];
}

/* ---------- the in-game pause menu (a multiplayer map's)

Its list (RESUME GAME, LEAVE GAME; a custom map's own buttons too) gets
SETTINGS, the menus' settings (as the main menu's SETTINGS opens them), and
for the host of a network game HOST CONTROLS (menu_functions.c's
host_controls_menu.inc: its players, the game, the bans; END GAME is its
Game's), or for the host of a game of this machine alone END GAME, which
ends the game as its time limit would: its players stay, and the host picks
the next map after the carnage report; in Basic Training's game, CHAPTERS
in its place, which opens its chapters (basic_training.c).
They are copies of the map's LEAVE GAME button (its look), put before it.
The map's pause screens move to fit them: on the Xbox's own (its box of
pausebox2 pieces), the rows are spaced as the campaign's pause menu's are
(issue 690), and the box is the port's (pause/pausebox_*:
tools/port_settings.py) as tall as they and the legends under them need,
centred where the old one was; on a custom map's, what is below the list
moves down. The Xbox's box keeps the Xbox's width while a controller's
buttons are drawn under it (issue 751), and is made as much wider as the
legends under it are only while the keyboard's keys are (pause_boxes_fit:
issue 596), here and in the campaign's pause menu. */

/* (Settings' first tab: tools/port_settings.py) */
#define PAUSE_SETTINGS_SCREEN "main_menu/settings_select/player_setup/player_profile_edit/video_settings/video_settings_screen"
/* (Host Controls: tools/port_settings.py's _host_controls) */
#define PAUSE_HOST_CONTROLS_SCREEN "main_menu/host_controls/host_controls_screen"
/* (Basic Training's chapters: tools/port_settings.py's _training) */
#define PAUSE_TRAINING_SCREEN "main_menu/training/training_screen"
#define PAUSE_BUTTON_SPACING 35
/* the port's box's frames (pause/pausebox_*: tools/port_settings.py's
pause_box_height), one for each number of rows from this, and its layout:
the campaign's pause menu's (the Xbox's 159-unit box of four rows, at the
PC version's pitch, PAUSE_CAMPAIGN_ROW_PITCH): its first row this far below
the box's top, the legends this far below its last row, and the box's
bottom this far below their top */
#define PAUSE_BOX_FIRST_ROWS 3
#define PAUSE_BOX_ROWS_TOP 9
#define PAUSE_BOX_LEGENDS_GAP 14
#define PAUSE_BOX_LEGENDS_BOTTOM 25
/* the Xbox's box's height, which the port's is centred where it was */
#define PAUSE_BOX_STOCK_HEIGHT 159
/* the Xbox's rows' height (its pause lists' buttons) */
#define PAUSE_BOX_ROW_HEIGHT 27
/* the PC version's pause menu's row pitch (30 apart) */
#define PAUSE_CAMPAIGN_ROW_PITCH 30

static long ui_function_named(char const *name)
{
	long index;

	for (index = 0; index < 512; index++)
	{
		char const *function = ui_widget_event_handler_function_name(index);

		if (!function)
			break;
		if (!strcmp(function, name))
			return index;
	}
	return NONE;
}

static boolean tag_name_ends(long tag_index, char const *end)
{
	char const *name = tag_index == NONE ? NULL : tag_get_name(tag_index);
	size_t length = name ? strlen(name) : 0, end_length = strlen(end);

	return name && length >= end_length && !strcmp(name + length - end_length, end);
}

/* the index of the list's button that runs the function (LEAVE GAME's),
else NONE */
static long pause_quit_button(struct ui_widget_definition const *list, long quit_function)
{
	struct ui_widget_child_reference const *children = list->child_widgets.address;
	long child;

	for (child = 0; child < list->child_widgets.count; child++)
	{
		struct ui_widget_definition const *button;
		struct ui_widget_event_handler_reference const *handlers;
		long handler;

		if (children[child].widget_tag.index == NONE)
			continue;
		button = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		handlers = button->event_handlers.address;
		for (handler = 0; handler < button->event_handlers.count; handler++)
		{
			if (handlers[handler].function == quit_function &&
				TEST_FLAG(handlers[handler].flags, _event_handler_run_function_bit))
			{
				return child;
			}
		}
	}
	return NONE;
}

/* a button like the model (its look), its own text, each of the model's
events (its A and Start) running the function and opening the widget (if
not NONE); with other_text, a second text, which the game data input (input)
chooses between */
static void *pause_button(struct cache_file_tag_instance *instances, struct ui_widget_definition const *model,
	char const *name, char const *text, long function, long widget_tag, char const *other_text, long input)
{
	struct ui_widget_definition *button = allocate(sizeof(*button));
	struct ui_widget_event_handler_reference *handlers =
		allocate(model->event_handlers.count * sizeof(struct ui_widget_event_handler_reference));
	long text_tag = next_tag(), handler;
	char const *texts[2];

	if (!button || !handlers)
		return NULL;
	memcpy(button, model, sizeof(*button));
	texts[0] = text;
	texts[1] = other_text;
	instance_set(instances, UNICODE_STRING_LIST_TAG, text_tag, name, " text",
		string_list_build(texts, NULL, other_text ? 2 : 1, NULL));
	reference_set(&button->text_label_string_list, UNICODE_STRING_LIST_TAG, text_tag);
	button->string_list_index = 0;
	button->game_data_inputs.count = 0;
	button->game_data_inputs.address = NULL;
	if (other_text)
	{
		struct ui_widget_game_data_input_reference *inputs = allocate(sizeof(*inputs));

		if (!inputs)
			return NULL;
		inputs->function = (short)input;
		button->game_data_inputs.count = 1;
		button->game_data_inputs.address = inputs;
	}
	/* (the game's memcpy asserts on a NULL source, even for nothing) */
	if (model->event_handlers.count)
		memcpy(handlers, model->event_handlers.address, model->event_handlers.count * sizeof(*handlers));
	for (handler = 0; handler < model->event_handlers.count; handler++)
	{
		handlers[handler].flags = FLAG(_event_handler_run_function_bit);
		handlers[handler].function = (short)function;
		handlers[handler].script[0] = 0;
		reference_set(&handlers[handler].widget_tag, UI_WIDGET_DEFINITION_TAG, widget_tag);
		if (widget_tag != NONE)
		{
			handlers[handler].flags |= FLAG(_event_handler_open_widget_bit) |
				FLAG(_event_handler_try_to_branch_on_failure_bit);
		}
	}
	button->event_handlers.address = handlers;
	return button;
}

/* the list's buttons: SETTINGS, and network co-op's FRIENDLY FIRE (whose
text says the machine's choice: menu_functions.c), a network game's host's
HOST CONTROLS (network co-op's too), or the host's END GAME, put before
LEAVE GAME or REVERT TO SAVED (quit; network_host_controls_pause_rows_for);
returns how many were added, and how much taller the list grew for them
(growth) */
static long pause_list_patch(struct cache_file_tag_instance *instances, struct ui_widget_definition *list, long quit,
	boolean host, short *growth)
{
	long host_controls_widget = widget_named(PAUSE_HOST_CONTROLS_SCREEN);
	long training_widget = widget_named(PAUSE_TRAINING_SCREEN);
	struct host_controls_pause_view view;
	unsigned long rows;
	struct ui_widget_child_reference *children = list->child_widgets.address;
	long count = list->child_widgets.count, added = 1, child;
	struct ui_widget_child_reference *grown;
	struct ui_widget_definition const *model = tag_get(UI_WIDGET_DEFINITION_TAG, children[quit].widget_tag.index);
	long settings_widget = widget_named(PAUSE_SETTINGS_SCREEN);
	short spacing = count >= 2 ? (short)(children[count - 1].vertical_offset - children[count - 2].vertical_offset) :
		PAUSE_BUTTON_SPACING;
	char const *names[3];
	void *buttons[3];
	long tags[3];

	view.host = host;
	view.coop = build.coop;
	view.local = network_game_is_splitscreen_local();
	view.training = basic_training_active();
	view.host_controls_screen = host_controls_widget != NONE;
	view.training_screen = training_widget != NONE;
	/* (the map's screen is every local player's: the functions of its host's
	rows refuse the others, menu_functions.c) */
	view.first_player = TRUE;
	rows = network_host_controls_pause_rows_for(&view);
	for (child = 0; child < NUMBER_OF_HOST_CONTROLS_PAUSE_ROWS; child++)
		added += TEST_FLAG(rows, child) ? 1 : 0;
	if (added > (long)NUMBEROF(names))
		return 0;
	grown = allocate((count + added) * sizeof(struct ui_widget_child_reference));
	if (!grown || settings_widget == NONE || spacing <= 0)
		return 0;
	names[0] = "pause/settings_button";
	tags[0] = next_tag();
	buttons[0] = pause_button(instances, model, names[0], "SETTINGS",
		function_index("port settings begin", "pause", 0), build.widget_tags[settings_widget], NULL, NONE);
	added = 1;
	if (TEST_FLAG(rows, _host_controls_pause_friendly_fire))
	{
		names[added] = "pause/friendly_fire_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/friendly_fire_button", "FRIENDLY FIRE ON",
			function_index("port coop friendly fire", "pause", 0), NONE, "FRIENDLY FIRE OFF",
			game_data_input_index("port coop friendly fire text", "pause", 0));
	}
	if (TEST_FLAG(rows, _host_controls_pause_chapters))
	{
		names[added] = "pause/training_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/training_button", "CHAPTERS",
			function_index("port training open", "pause", 0), build.widget_tags[training_widget], NULL, NONE);
	}
	if (TEST_FLAG(rows, _host_controls_pause_host_controls))
	{
		names[added] = "pause/host_controls_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/host_controls_button", "HOST CONTROLS",
			function_index("port host open", "pause", 0), build.widget_tags[host_controls_widget], NULL, NONE);
	}
	if (TEST_FLAG(rows, _host_controls_pause_end_game))
	{
		names[added] = "pause/end_game_button";
		tags[added] = next_tag();
		buttons[added++] = pause_button(instances, model, "pause/end_game_button", "END GAME",
			function_index("port pause end game", "pause", 0), NONE, NULL, NONE);
	}
	for (child = 0; child < added; child++)
	{
		if (!buttons[child])
			return 0;
	}
	for (child = 0; child < added; child++)
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, tags[child], names[child], "", buttons[child]);
	if (quit)
		memcpy(grown, children, quit * sizeof(*grown));
	for (child = 0; child < added; child++)
	{
		grown[quit + child] = children[quit];
		reference_set(&grown[quit + child].widget_tag, UI_WIDGET_DEFINITION_TAG, tags[child]);
		grown[quit + child].vertical_offset = (short)(children[quit].vertical_offset + child * spacing);
	}
	for (child = quit; child < count; child++)
	{
		grown[child + added] = children[child];
		grown[child + added].vertical_offset = (short)(children[child].vertical_offset + added * spacing);
	}
	list->child_widgets.address = grown;
	list->child_widgets.count = count + added;
	/* (a list with room for them, Halo PC's of five rows that Custom Edition
	maps keep two of: the buttons centred in it; else it grows, as the
	Xbox's of two rows does, and draws within its bounds) */
	{
		short room = (short)(list->bounds.y1 - list->bounds.y0);
		short column = (short)((count + added - 1) * spacing + model->bounds.y1 - model->bounds.y0);

		*growth = 0;
		if (column <= room)
		{
			for (child = 0; child < count + added; child++)
				grown[child].vertical_offset = (short)((room - column) / 2 + child * spacing);
		}
		else
		{
			*growth = (short)(added * PAUSE_BUTTON_SPACING);
			list->bounds.y1 = (short)(list->bounds.y1 + added * spacing);
		}
	}
	return added;
}

/* the Xbox's own box: a widget of the three pausebox2 pieces */
static boolean pause_box_stock(struct ui_widget_definition const *box)
{
	struct ui_widget_child_reference const *pieces = box->child_widgets.address;

	return box->child_widgets.count == 3 && tag_name_ends(pieces[0].widget_tag.index, "\\pausebox2_left") &&
		tag_name_ends(pieces[1].widget_tag.index, "\\pausebox2_left_center") &&
		tag_name_ends(pieces[2].widget_tag.index, "\\pausebox2_right");
}

/* the stock box's pieces drawn with the port's box: its frame for the number
of rows */
static void pause_box_redraw(struct ui_widget_definition const *box, long rows)
{
	static char const *const names[] = { "pause/pausebox_left", "pause/pausebox_center", "pause/pausebox_right" };
	struct ui_widget_child_reference const *pieces = box->child_widgets.address;
	long piece;

	for (piece = 0; piece < 3; piece++)
	{
		struct ui_widget_definition *definition = tag_get(UI_WIDGET_DEFINITION_TAG, pieces[piece].widget_tag.index);
		long bitmap = bitmap_named(names[piece]);
		struct bitmap_group *group;
		struct bitmap_group_sequence *sequence;

		if (bitmap == NONE)
			continue;
		group = bitmap_group_get(build.bitmap_tags[bitmap]);
		sequence = group->sequences.address;
		/* (one frame: the one for the number of rows) */
		sequence->first_bitmap_index = (short)PIN(rows - PAUSE_BOX_FIRST_ROWS, 0, group->bitmaps.count - 1);
		sequence->bitmap_count = 1;
		reference_set(&definition->background_bitmap, BITMAP_GROUP_TAG, build.bitmap_tags[bitmap]);
		definition->bounds.y1 = (short)(definition->bounds.y0 + 256);
	}
}

/* the port's box's height for a number of rows (tools/port_settings.py's
pause_box_height) */
static short pause_box_height(long rows)
{
	return (short)(PAUSE_BOX_ROWS_TOP + (rows - 1) * PAUSE_CAMPAIGN_ROW_PITCH + PAUSE_BOX_ROW_HEIGHT +
		PAUSE_BOX_LEGENDS_GAP + PAUSE_BOX_LEGENDS_BOTTOM);
}

/* whether a screen's child is the legends' row under a pause menu (the
Xbox's "(B) =CANCEL (A) =SELECT") */
static boolean pause_legends(long tag_index)
{
	return tag_name_ends(tag_index, "\\button_key_sm") || tag_name_ends(tag_index, "\\button_key");
}

/* the Xbox's pause box (the pausebox2 pieces, and the campaign's mission
objectives' box beside it: pausebox_*) made `growth` units wider for the
legends' row under it (ui_widget_port_key_legend_growth: its keys' caps
wider than the controller's pictures, issue 596), half of it either side,
so that the room either side of the row is the Xbox's; the objectives' box
moves right by half of it, as the box's right does, and the screens move
what is in it (pause_objectives_move). A negative growth makes it narrower
again (pause_boxes_fit) */
static void pause_box_widen(struct ui_widget_definition *box, short growth)
{
	struct ui_widget_child_reference *pieces = box->child_widgets.address;
	long piece;

	if (!growth)
		return;
	for (piece = 0; piece < box->child_widgets.count; piece++)
	{
		long tag = pieces[piece].widget_tag.index;

		if (tag_name_ends(tag, "\\pausebox2_left"))
			pieces[piece].horizontal_offset = (short)(pieces[piece].horizontal_offset - growth / 2);
		else if (tag_name_ends(tag, "\\pausebox2_left_center"))
		{
			struct ui_widget_definition *center = tag_get(UI_WIDGET_DEFINITION_TAG, tag);

			pieces[piece].horizontal_offset = (short)(pieces[piece].horizontal_offset - growth / 2);
			center->bounds.x1 = (short)(center->bounds.x1 + growth);
		}
		else if (tag_name_ends(tag, "\\pausebox2_right") || tag_name_ends(tag, "\\pausebox_left") ||
			tag_name_ends(tag, "\\pausebox_right_center") || tag_name_ends(tag, "\\pausebox_center") ||
			tag_name_ends(tag, "\\pausebox_right"))
		{
			pieces[piece].horizontal_offset = (short)(pieces[piece].horizontal_offset + growth / 2);
		}
	}
	box->bounds.x1 = (short)(box->bounds.x1 + growth);
}

/* what a pause screen shows in the mission objectives' box (its caption and
its text) moved with the box (pause_box_widen) */
static void pause_objectives_move(struct ui_widget_definition *screen, short growth)
{
	struct ui_widget_child_reference *children = screen->child_widgets.address;
	long child;

	for (child = 0; growth && child < screen->child_widgets.count; child++)
	{
		long tag = children[child].widget_tag.index;

		if (tag_name_ends(tag, "\\mission_objective_text") || tag_name_ends(tag, "\\mission_objectives_caption"))
			children[child].horizontal_offset = (short)(children[child].horizontal_offset + growth / 2);
	}
}

/* a pause box to be made `growth` units wider while the keyboard plays
(pause_boxes_fit), with the screens whose mission objectives move with it
(NULL for none) */
static void pause_box_keep(struct ui_widget_definition *box, struct ui_widget_definition *screen,
	struct ui_widget_definition *split_screen, short growth)
{
	struct pause_box_growth *kept;

	if (growth <= 0 || menu_tags.pause_box_count >= PAUSE_BOXES)
		return;
	kept = &menu_tags.pause_boxes[menu_tags.pause_box_count++];
	kept->box = box;
	kept->screen = screen;
	kept->split_screen = split_screen;
	kept->growth = growth;
}

/* the pause boxes as wide as the legends under them are drawn: the Xbox's
width while a controller's buttons are drawn in them (the pictures it had
room for: issue 751), and as much wider as the keys' caps need while the
keyboard's keys are (input.button_prompts: issue 596). Each frame the menus
are drawn (ui_widget_port_wide_layout) */
static void pause_boxes_fit(void)
{
	boolean wide = halo_keyboard_prompts() != 0;
	long index;

	if (wide == menu_tags.pause_boxes_wide)
		return;
	menu_tags.pause_boxes_wide = wide;
	for (index = 0; index < menu_tags.pause_box_count; index++)
	{
		struct pause_box_growth const *kept = &menu_tags.pause_boxes[index];
		short growth = (short)(wide ? kept->growth : -kept->growth);

		pause_box_widen(kept->box, growth);
		if (kept->screen)
			pause_objectives_move(kept->screen, growth);
		if (kept->split_screen)
			pause_objectives_move(kept->split_screen, growth);
	}
}

/* how much wider the box above a legends' row is made for it (an even
number of units: half each side), the row given that room
(pc_menu_legend_room) */
static short pause_legends_growth(long legends_tag)
{
	short growth = ui_widget_port_key_legend_growth(legends_tag);

	growth = (short)(growth + growth % 2);
	if (growth > 0)
		tag_value_set(menu_tags.legend_rooms, &menu_tags.legend_room_count, legends_tag, growth);
	return growth;
}

/* how much wider a pause box is made for the legends' row under it while
the keyboard's keys are drawn in it (pause_legends_growth), for the
hand-written pause menu's box (menu_screen.c) */
short menu_tags_legend_growth(
	long legends_tag)
{
	return legends_tag == NONE ? 0 : pause_legends_growth(legends_tag);
}

/* the rows the pause menu adds for this machine, as pause_patch and
pause_campaign_patch add them to the map's (network_host_controls_pause_rows_for):
the hand-written pause menu's (menu_screen.c's pause.* bindings), its
local player's: the host's rows the machine's first local player's alone */
static boolean network_coop_campaign_map(void);

unsigned long menu_tags_pause_rows(
	short local_player_index)
{
	struct host_controls_pause_view view;
	/* (a network co-op game's map: its pause menu's FRIENDLY FIRE too) */
	boolean coop = network_coop_campaign_map();
	/* (the screens by their tags: the menus' files are let go of once their
	tags are made) */
	boolean host_controls = tag_loaded(UI_WIDGET_DEFINITION_TAG,
		PC_MENU_TAG_PREFIX "main_menu\\host_controls\\host_controls_screen") != NONE;
	boolean training = tag_loaded(UI_WIDGET_DEFINITION_TAG, PC_MENU_TAG_PREFIX "main_menu\\training\\training_screen") != NONE;

	view.host = coop ? global_network_game_server_get() != NULL : global_network_game_server_get() != NULL ||
		(network_admin_was_remote() && host_controls);
	view.coop = coop;
	view.local = network_game_is_splitscreen_local();
	view.training = basic_training_active();
	view.host_controls_screen = host_controls;
	view.training_screen = training;
	view.first_player = network_host_controls_local_player(local_player_index);
	return network_host_controls_pause_rows_for(&view);
}

static void pause_patch(struct cache_file_tag_instance *instances)
{
	long collection = tag_loaded('Soul', MULTIPLAYER_COLLECTION);
	long quit_function = ui_function_named("mp game player quit");
	/* (a network game's host, or a dedicated server's admin, whose Host
	Controls are the server's: network_admin.c) */
	boolean host = global_network_game_server_get() != NULL ||
		(network_admin_was_remote() && widget_named(PAUSE_HOST_CONTROLS_SCREEN) != NONE);
	struct tag_block const *screens;
	long patched_list = NONE, added = 0, buttons = 0, screen;
	short grow = 0, growth = 0;
	boolean box_redrawn = FALSE, rows_spaced = FALSE;

	/* MCC supplies its own Settings and game-options buttons. */
	if (mcc_cache_tags_loaded())
		return;
	if (collection == NONE || quit_function == NONE)
		return;
	screens = tag_get('Soul', collection);
	for (screen = 0; screen < screens->count; screen++)
	{
		long screen_tag = ((struct tag_reference const *)screens->address)[screen].index;
		struct ui_widget_definition *definition;
		struct ui_widget_child_reference *children;
		long child, list_child = NONE, box_child = NONE;
		short list_top;

		if (screen_tag == NONE)
			continue;
		definition = tag_get(UI_WIDGET_DEFINITION_TAG, screen_tag);
		children = definition->child_widgets.address;
		for (child = 0; child < definition->child_widgets.count && list_child == NONE; child++)
		{
			struct ui_widget_definition *list;
			long quit;

			if (children[child].widget_tag.index == NONE)
				continue;
			list = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
			if (list->type != _widget_type_column_list)
				continue;
			/* (one list, which the screens share: patched once) */
			if (children[child].widget_tag.index == patched_list)
			{
				list_child = child;
				continue;
			}
			quit = pause_quit_button(list, quit_function);
			if (quit == NONE || patched_list != NONE)
				continue;
			added = pause_list_patch(instances, list, quit, host, &grow);
			if (!added)
				return;
			buttons = list->child_widgets.count;
			patched_list = children[child].widget_tag.index;
			list_child = child;
		}
		if (list_child == NONE)
			continue;
		for (child = 0; child < definition->child_widgets.count; child++)
		{
			if (child != list_child && children[child].widget_tag.index != NONE &&
				pause_box_stock(tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index)))
			{
				box_child = child;
			}
		}
		/* the stock box: the port's, its rows as the campaign's pause menu
		spaces them, its legends under them, as tall as they need, centred
		where the old one was, and as wide as the legends need
		(pause_boxes_fit); else only
		what is below the list moves (as far as the list grew) */
		if (box_child != NONE)
		{
			struct ui_widget_definition *list = tag_get(UI_WIDGET_DEFINITION_TAG, patched_list);
			struct ui_widget_child_reference *rows = list->child_widgets.address;
			short height = pause_box_height(buttons);
			short top = (short)(children[box_child].vertical_offset + (PAUSE_BOX_STOCK_HEIGHT - height) / 2);
			long row;

			if (!rows_spaced)
			{
				for (row = 0; row < list->child_widgets.count; row++)
					rows[row].vertical_offset = (short)(row * PAUSE_CAMPAIGN_ROW_PITCH);
				list->bounds.y1 = (short)(list->bounds.y0 + (buttons - 1) * PAUSE_CAMPAIGN_ROW_PITCH +
					PAUSE_BOX_ROW_HEIGHT);
				rows_spaced = TRUE;
			}
			children[box_child].vertical_offset = top;
			children[list_child].vertical_offset = (short)(top + PAUSE_BOX_ROWS_TOP);
			for (child = 0; child < definition->child_widgets.count; child++)
			{
				if (!pause_legends(children[child].widget_tag.index))
					continue;
				children[child].vertical_offset = (short)(top + height - PAUSE_BOX_LEGENDS_BOTTOM);
				if (!box_redrawn)
					growth = pause_legends_growth(children[child].widget_tag.index);
			}
			if (!box_redrawn)
			{
				struct ui_widget_definition *box = tag_get(UI_WIDGET_DEFINITION_TAG,
					children[box_child].widget_tag.index);

				pause_box_redraw(box, buttons);
				pause_box_keep(box, NULL, NULL, growth);
				box_redrawn = TRUE;
			}
			continue;
		}
		list_top = children[list_child].vertical_offset;
		for (child = 0; child < definition->child_widgets.count; child++)
		{
			if (children[child].vertical_offset > list_top)
				children[child].vertical_offset += grow;
		}
	}
	if (patched_list != NONE)
		platform_log("menus: the pause menu has SETTINGS%s", !host ? "" : basic_training_active() ? " and CHAPTERS" :
			network_game_is_splitscreen_local() ? " and END GAME" : " and HOST CONTROLS");
}

/* ---------- the campaign's pause menu

Its list gets the same SETTINGS (pause_list_patch) before REVERT TO SAVED, a
copy of it, and in network co-op FRIENDLY FIRE after SETTINGS, then the
host's HOST CONTROLS (as a multiplayer game's host has it). The rows use
the PC version's pause menu's pitch, so a highlight sits clear of the next
label. The prompts (B and A) stay that same gap under the last row, and the
box beside the mission objectives grows with them, and the box is as wide as
the legends need (pause_campaign_widen). Settings there hides the rows that
do not work in a game (their conditions in the model: menu_functions.c's
settings_rows_conditions). */

/* the pause box's frames from the third (pause_box_height's of five, six and
seven rows), and the mission objectives' box's, are the stock 159 grown by
the 36 that five rows at the PC version's pitch add (SETTINGS), by the 66
that six add (network co-op's FRIENDLY FIRE too), then by the 96 that seven
add (its host's HOST CONTROLS too: tools/port_settings.py) */
#define PAUSE_CAMPAIGN_BOX_FRAME 2
static short const pause_campaign_box_heights[] = { 195, 225, 255 };

/* the port box piece for one of the map's pause box pieces, else NULL */
static char const *pause_campaign_box_bitmap(long tag_index)
{
	if (tag_name_ends(tag_index, "\\pausebox2_left_center"))
		return "pause/pausebox_center";
	if (tag_name_ends(tag_index, "\\pausebox2_left"))
		return "pause/pausebox_left";
	if (tag_name_ends(tag_index, "\\pausebox2_right"))
		return "pause/pausebox_right";
	/* The objectives' pieces carry a header, not the button-key footer. */
	if (tag_name_ends(tag_index, "\\pausebox_right_center") || tag_name_ends(tag_index, "\\pausebox_center"))
		return "pause/objectives_center";
	if (tag_name_ends(tag_index, "\\pausebox_left"))
		return "pause/objectives_left";
	if (tag_name_ends(tag_index, "\\pausebox_right"))
		return "pause/objectives_right";
	return NULL;
}

/* the screen offset of the prompts under the list (B and A), else NONE */
static short pause_campaign_prompts(struct ui_widget_definition const *screen)
{
	struct ui_widget_child_reference const *children = screen->child_widgets.address;
	long child;

	for (child = 0; child < screen->child_widgets.count; child++)
	{
		if (tag_name_ends(children[child].widget_tag.index, "\\button_key_sm") ||
			tag_name_ends(children[child].widget_tag.index, "\\button_key"))
		{
			return children[child].vertical_offset;
		}
	}
	return NONE;
}

/* the prompts move down with the last row */
static void pause_campaign_shift_prompts(struct ui_widget_definition *widget, short shift)
{
	struct ui_widget_child_reference *children = widget->child_widgets.address;
	long child;

	for (child = 0; child < widget->child_widgets.count; child++)
	{
		if (children[child].widget_tag.index == NONE)
			continue;
		if (tag_name_ends(children[child].widget_tag.index, "\\button_key_sm") ||
			tag_name_ends(children[child].widget_tag.index, "\\button_key"))
		{
			children[child].vertical_offset = (short)(children[child].vertical_offset + shift);
		}
		else if (!pause_campaign_box_bitmap(children[child].widget_tag.index))
		{
			pause_campaign_shift_prompts(tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index), shift);
		}
	}
}

/* the box (the menu's, and the mission objectives' beside it) grows by the
same amount, using the frame drawn for that height */
static void pause_campaign_grow_box(struct ui_widget_definition *widget, short shift)
{
	struct ui_widget_child_reference *children;
	long child;
	boolean piece_here = FALSE;

	if (!widget || shift <= 0 || widget->child_widgets.count <= 0)
		return;
	children = widget->child_widgets.address;
	for (child = 0; child < widget->child_widgets.count; child++)
	{
		struct ui_widget_definition *child_widget;
		char const *piece_bitmap;

		if (children[child].widget_tag.index == NONE)
			continue;
		child_widget = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		piece_bitmap = pause_campaign_box_bitmap(children[child].widget_tag.index);
		if (!piece_bitmap)
		{
			pause_campaign_grow_box(child_widget, shift);
			continue;
		}
		piece_here = TRUE;
		{
			/* (bitmap_name is the lookup, not piece_bitmap: find calls it) */
			long bitmap = bitmap_named(piece_bitmap);
			short height = (short)(child_widget->bounds.y1 - child_widget->bounds.y0);
			short frame = NONE, size;

			for (size = 0; size < (short)NUMBEROF(pause_campaign_box_heights); size++)
			{
				if ((short)(height + shift) == pause_campaign_box_heights[size])
					frame = (short)((strstr(piece_bitmap, "objectives_") ? 0 : PAUSE_CAMPAIGN_BOX_FRAME) + size);
			}
			if (bitmap != NONE && frame != NONE)
			{
				struct bitmap_group *group = bitmap_group_get(build.bitmap_tags[bitmap]);
				struct bitmap_group_sequence *sequence = group ? group->sequences.address : NULL;

				if (sequence && frame < group->bitmaps.count)
				{
					sequence->first_bitmap_index = frame;
					sequence->bitmap_count = 1;
					reference_set(&child_widget->background_bitmap, BITMAP_GROUP_TAG, build.bitmap_tags[bitmap]);
					child_widget->bounds.y1 = (short)(child_widget->bounds.y0 + height + shift);
				}
			}
			else
			{
				child_widget->bounds.y1 = (short)(child_widget->bounds.y1 + shift);
			}
		}
	}
	if (piece_here)
		widget->bounds.y1 = (short)(widget->bounds.y1 + shift);
}

/* the campaign's pause box (the box of its pieces, which the split screen's
pause screen shares) as wide as the legends under it need while the
keyboard plays (pause_box_keep), the mission objectives' box and what is in
it moved with its right */
static void pause_campaign_widen(struct ui_widget_definition *screen)
{
	struct ui_widget_child_reference *children = screen->child_widgets.address;
	long split_screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, CAMPAIGN_PAUSE_SPLIT_SCREEN);
	/* (the objectives move with the first box: once) */
	struct ui_widget_definition *moved = screen;
	struct ui_widget_definition *split_moved = split_screen == NONE ? NULL :
		tag_get(UI_WIDGET_DEFINITION_TAG, split_screen);
	short growth = 0;
	long child;

	for (child = 0; child < screen->child_widgets.count && !growth; child++)
	{
		if (pause_legends(children[child].widget_tag.index))
			growth = pause_legends_growth(children[child].widget_tag.index);
	}
	for (child = 0; growth > 0 && child < screen->child_widgets.count; child++)
	{
		struct ui_widget_definition *box;
		struct ui_widget_child_reference const *pieces;
		long piece;

		if (children[child].widget_tag.index == NONE)
			continue;
		box = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		pieces = box->child_widgets.address;
		for (piece = 0; piece < box->child_widgets.count; piece++)
		{
			if (tag_name_ends(pieces[piece].widget_tag.index, "\\pausebox2_left"))
			{
				pause_box_keep(box, moved, split_moved, growth);
				moved = split_moved = NULL;
				break;
			}
		}
	}
}

static void pause_campaign_patch(struct cache_file_tag_instance *instances)
{
	long screen = tag_loaded(UI_WIDGET_DEFINITION_TAG, CAMPAIGN_PAUSE_SCREEN);
	struct ui_widget_definition *definition;
	struct ui_widget_child_reference *children;
	long child;

	if (screen == NONE)
		return;
	definition = tag_get(UI_WIDGET_DEFINITION_TAG, screen);
	children = definition->child_widgets.address;
	for (child = 0; child < definition->child_widgets.count; child++)
	{
		struct ui_widget_definition *list;
		struct ui_widget_child_reference const *rows;
		short span, top, grow, bottom;
		long revert, added;

		if (children[child].widget_tag.index == NONE)
			continue;
		list = tag_get(UI_WIDGET_DEFINITION_TAG, children[child].widget_tag.index);
		if (list->type != _widget_type_column_list || list->child_widgets.count < 2)
			continue;
		rows = list->child_widgets.address;
		/* (REVERT TO SAVED, by its tag: it opens its confirmation rather
		than running a function, and a map's list need not have it second) */
		for (revert = 0; revert < list->child_widgets.count; revert++)
		{
			if (tag_name_ends(rows[revert].widget_tag.index, "\\restart_at_save_point_button"))
				break;
		}
		if (revert == list->child_widgets.count)
			return;
		span = (short)(rows[list->child_widgets.count - 1].vertical_offset - rows[0].vertical_offset);
		top = rows[0].vertical_offset;
		bottom = list->bounds.y1;
		/* (network co-op's host's HOST CONTROLS too, as a multiplayer
		game's host has them) */
		added = pause_list_patch(instances, list, revert, build.coop && global_network_game_server_get() != NULL,
			&grow);
		if (added)
		{
			struct ui_widget_child_reference *placed = list->child_widgets.address;
			struct ui_widget_definition const *button =
				tag_get(UI_WIDGET_DEFINITION_TAG, placed[0].widget_tag.index);
			short height = (short)(button->bounds.y1 - button->bounds.y0);
			short prompts = pause_campaign_prompts(definition);
			short count = (short)list->child_widgets.count;
			short row, new_last, need = 0;

			for (row = 0; row < count; row++)
				placed[row].vertical_offset = (short)(top + row * PAUSE_CAMPAIGN_ROW_PITCH);
			new_last = placed[count - 1].vertical_offset;
			list->bounds.y1 = bottom;
			if (list->bounds.y1 < (short)(new_last + height))
				list->bounds.y1 = (short)(new_last + height);
			/* keep the gap the last row had above the prompts, and grow the
			box by the same amount */
			if (prompts != NONE)
			{
				short old_bottom = (short)(children[child].vertical_offset + top + span + height);
				short new_bottom = (short)(children[child].vertical_offset + new_last + height);
				short clearance = (short)(prompts - old_bottom);

				if (clearance < 0)
					clearance = 0;
				need = (short)(new_bottom + clearance - prompts);
			}
			if (need > 0)
			{
				pause_campaign_shift_prompts(definition, need);
				pause_campaign_grow_box(definition, need);
			}
			pause_campaign_widen(definition);
			platform_log("menus: the pause menu has SETTINGS%s%s", build.coop ? " and FRIENDLY FIRE" : "",
				added > (build.coop ? 2 : 1) ? " and HOST CONTROLS" : "");
		}
		return;
	}
}

/* The full-screen campaign pause menu also serves network co-op. Local
split-screen keeps its separate layout. */
static boolean full_screen_campaign_map(void)
{
	return tag_loaded('Soul', MULTIPLAYER_COLLECTION) == NONE &&
		tag_loaded(UI_WIDGET_DEFINITION_TAG, CAMPAIGN_PAUSE_SCREEN) != NONE &&
		(game_connection() != _game_connection_local || network_game_is_active() || !game_is_cooperative());
}

/* of those, a network game's: network co-op, whose pause menu gets FRIENDLY
FIRE too */
static boolean network_coop_campaign_map(void)
{
	return full_screen_campaign_map() && game_connection() != _game_connection_local && network_game_is_active();
}

/* whether display.menus asks for the PC version's menus: "pc" in any case
(a value neither "pc" nor "xbox" is said once in the log, and is the Xbox's) */
static boolean menus_pc_chosen(void)
{
	static boolean said;
	char const *value = config_string("display.menus");

	if (!csstrcasecmp(value, "pc"))
		return TRUE;
	if (csstrcasecmp(value, "xbox") && !said)
	{
		platform_log("menus: display.menus \"%s\" is not \"xbox\" or \"pc\": the Xbox's", value);
		said = TRUE;
	}
	return FALSE;
}

/* whether the PC menus' text is the language of the maps' Custom Edition
(display.menu_language), else English */
static boolean menus_language_maps(void)
{
	static boolean said;
	char const *value = config_string("display.menu_language");

	if (!csstrcasecmp(value, "maps"))
		return TRUE;
	if (csstrcasecmp(value, "english") && !said)
	{
		platform_log("menus: display.menu_language \"%s\" is not \"maps\" or \"english\": English", value);
		said = TRUE;
	}
	return FALSE;
}

void menu_tags_loaded(
	char const *map_name)
{
	struct halo_menus const *menus;
	struct cache_file_tag_instance *instances;
	long widget_count, own_lists = 0, total, index, large_font, model_count = 0;
	long model_tags[MENU_MODELS_MAXIMUM_TAGS];
	boolean locale;

	boolean game_map = strcmp(map_name, "ui") != 0;
	boolean campaign = game_map && full_screen_campaign_map();
	boolean coop = game_map && network_coop_campaign_map();

	/* MCC owns its pause menus and uses these native Settings screens.
	Do not patch embedded MCC campaign widgets. */
	if (mcc_cache_tags_loaded())
	{
		if (mcc_ui_settings_needed(map_name))
		{
			campaign = FALSE;
			goto mcc_settings;
		}
		return;
	}
	/* (ui.map, and a multiplayer map: its pause menu's SETTINGS; a
	campaign's too, single player's or network co-op's, which also gets
	FRIENDLY FIRE) */
	if ((game_map && tag_loaded('Soul', MULTIPLAYER_COLLECTION) == NONE && !campaign) ||
		!menus_pc_chosen())
	{
		return;
	}
mcc_settings:
	menus = halo_menus_load();
	if (!menus)
		return;
	memset(&build, 0, sizeof(build));
	build.menus = menus;
	build.game_map = game_map;
	name_indices_make();
	/* Settings must not pause the simulation in a network campaign. */
	build.campaign = campaign && game_connection() == _game_connection_local &&
		!network_game_is_active() && !game_is_cooperative();
	/* MCC owns this decision; only its local campaign Settings pause time. */
	if (mcc_cache_tags_loaded())
		build.campaign = mcc_ui_settings_pause_game();
	build.coop = coop;
	build.map_font = game_map ? pause_menu_font() : NONE;
	widget_count = menus->widget_count;
	build.widget_tags = malloc((widget_count + 1) * sizeof(long));
	build.text_tags = malloc((widget_count + 1) * sizeof(long));
	build.spinner_tags = malloc((widget_count + 1) * sizeof(long));
	build.bitmap_tags = malloc((menus->bitmap_count + 1) * sizeof(long));
	build.strings_tags = malloc((menus->string_list_count + 1) * sizeof(long));
	if (!build.widget_tags || !build.text_tags || !build.spinner_tags || !build.bitmap_tags || !build.strings_tags)
		goto failed;
	for (index = 0; index < widget_count; index++)
	{
		own_lists += (menus->widgets[index].text != NULL) + (menus->widgets[index].strings != NULL);
		/* (names are unique) */
		if (widget_named(menus->widgets[index].name) != index)
			problem(menus->widgets[index].file, menus->widgets[index].line, "two widgets are named",
				menus->widgets[index].name);
	}
	/* (the heading font: a copy of large_ui's tag, which the high-res text
	knows by its own name, and draws in OpenCE where it is on; the bitmap
	font's letters, as large_ui's, where it is off) */
	large_font = tag_loaded(FONT_TAG, "ui\\large_ui");
	/* (ui.map's: the menus' models' tags, the Spartan's, read and checked,
	menu_models.c) */
	model_count = game_map ? 0 : menu_models_tags_load();
	total = (large_font != NONE) + widget_count + own_lists + menus->string_list_count + menus->bitmap_count +
		model_count + (game_map ? PAUSE_PATCH_TAGS : 0);
	instances = build.failed ? NULL : instances_grow(total, &build.first_index, &build.first_salt);
	if (!instances)
		goto failed;
	/* (the widgets' wide layouts, by their tags' indices after the map's) */
	menu_tags.wides = allocate(total * sizeof(*menu_tags.wides));
	menu_tags.wide_count = menu_tags.wides ? total : 0;
	menu_tags.sources = allocate(total * sizeof(*menu_tags.sources));
	menu_tags.source_count = menu_tags.sources ? total : 0;
	menu_tags.looks = allocate(total * sizeof(*menu_tags.looks));
	menu_tags.look_count = menu_tags.looks ? total : 0;
	/* each new tag's index */
	for (index = 0; index < widget_count; index++)
	{
		build.widget_tags[index] = next_tag();
		build.text_tags[index] = menus->widgets[index].text ? next_tag() : NONE;
		build.spinner_tags[index] = menus->widgets[index].strings ? next_tag() : NONE;
	}
	for (index = 0; index < menus->string_list_count; index++)
		build.strings_tags[index] = next_tag();
	for (index = 0; index < menus->bitmap_count; index++)
		build.bitmap_tags[index] = next_tag();
	build.heading_tag = NONE;
	if (large_font != NONE)
	{
		struct font_header *heading = allocate(sizeof(*heading));

		if (heading)
		{
			memcpy(heading, font_definition_get(large_font), sizeof(*heading));
			build.heading_tag = next_tag();
			instance_set(instances, FONT_TAG, build.heading_tag, "heading", "", heading);
		}
	}
	/* (the menus' models', checked: theirs at once) */
	if (model_count > 0 && model_count <= MENU_MODELS_MAXIMUM_TAGS)
	{
		for (index = 0; index < model_count; index++)
			model_tags[index] = next_tag();
		menu_models_tags_add(instances, model_tags);
	}
	/* the tags: the bitmaps and strings first, which the widgets name; the
	widgets' names in the table before any is built, which they find there */
	for (index = 0; index < menus->bitmap_count && !build.failed; index++)
	{
		instance_set(instances, BITMAP_GROUP_TAG, build.bitmap_tags[index], menus->bitmaps[index].name, "",
			bitmap_build(&menus->bitmaps[index], build.bitmap_tags[index]));
	}
	/* (the PC version's strings in the language of the Custom Edition the
	maps come from, its loc.map's, unless English is chosen) */
	locale = menus_language_maps() && custom_edition_locale_open();
	for (index = 0; index < menus->string_list_count && !build.failed; index++)
	{
		struct halo_menu_strings const *list = &menus->string_lists[index];
		struct custom_edition_string_list *map = locale && list->map ? custom_edition_locale_strings(list->map) : NULL;

		instance_set(instances, UNICODE_STRING_LIST_TAG, build.strings_tags[index], list->name, "",
			string_list_build(menus->strings + list->first, menus->string_maps + list->first, list->count, map));
		if (map)
			free(map);
	}
	if (locale)
	{
		custom_edition_locale_close();
		if (!game_map)
			platform_log("menus: %ld strings from the Custom Edition's loc.map", build.map_strings);
	}
	for (index = 0; index < widget_count && !build.failed; index++)
	{
		struct halo_menu_widget const *widget = &menus->widgets[index];

		if (widget->text)
		{
			instance_set(instances, UNICODE_STRING_LIST_TAG, build.text_tags[index], widget->name, " text",
				string_list_build(&widget->text, NULL, 1, NULL));
		}
		if (widget->strings)
		{
			char const *pieces[MAXIMUM_STRINGS];
			long count = spinner_split(widget, FALSE, pieces);

			instance_set(instances, UNICODE_STRING_LIST_TAG, build.spinner_tags[index], widget->name, " strings",
				string_list_build(pieces, NULL, count, NULL));
		}
		instance_set(instances, UI_WIDGET_DEFINITION_TAG, build.widget_tags[index], widget->name, "", NULL);
		{
			long source_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[index]) - menu_tags.original_count;

			if (source_index >= 0 && source_index < menu_tags.source_count)
				menu_tags.sources[source_index] = widget->source;
			/* (its look in the model found by its name: the menus' files' are
			kept until they are read again; but the hand-written screens'
			building blocks, screens/..., which are drawn from their own
			definitions: menu_screen.c) */
			if (source_index >= 0 && source_index < menu_tags.look_count && strncmp(widget->name, "screens/", 8))
				menu_tags.looks[source_index].name = widget->name;
		}
		if (widget->radius > 0)
			tag_value_set(menu_tags.radii, &menu_tags.radius_count, build.widget_tags[index], (short)widget->radius);
	}
	if (build.failed)
		goto failed;
	/* (pause_patch's, until it makes them: nothing a name finds) */
	for (index = build.first_index + build.next; index < build.first_index + total; index++)
	{
		instances[index].group_tag = NONE;
		instances[index].tag_index = NONE;
		instances[index].name = "";
	}
	cache_files_set_tag_instances(instances, build.first_index + total);
	for (index = 0; index < widget_count && !build.failed; index++)
		instances[DATUM_INDEX_TO_ABSOLUTE_INDEX(build.widget_tags[index])].base_address = widget_build(index);
	/* (and the model's behaviours of them, the menu engine's) */
	behaviours_build(instances);
	if (build.failed)
		goto failed;
	if (game_map)
	{
		pause_patch(instances);
		if (campaign)
			pause_campaign_patch(instances);
		if (build.failed)
			goto failed;
		/* (those it made) */
		cache_files_set_tag_instances(instances, build.first_index + build.next);
	}
	else if (widget_named(menus->root) != NONE)
	{
		snprintf(menu_tags.root, sizeof(menu_tags.root), "%s%s", PC_MENU_TAG_PREFIX, menus->root);
		for (index = 0; menu_tags.root[index]; index++)
		{
			if (menu_tags.root[index] == '/')
				menu_tags.root[index] = '\\';
		}
	}
	else
	{
		platform_log("menus: there is no widget named %s, the main menu", menus->root);
	}
	menu_tags.loaded = TRUE;
	menu_tags.generation = cache_files_tags_generation();
	menu_tags.heading_tag = build.heading_tag;
	menu_tags.map_font = build.map_font;
	menu_tags.game_map = game_map;
	platform_log("menus: %ld widgets, %ld string lists and %ld bitmaps added to the map's %ld tags",
		widget_count, own_lists + menus->string_list_count, menus->bitmap_count, build.first_index);
	goto done;

failed:
	platform_log("menus: not added; using the game's own menus");
	menu_tags_release();

done:
	free(build.widget_tags);
	free(build.text_tags);
	free(build.spinner_tags);
	free(build.strings_tags);
	free(build.bitmap_tags);
	memset(&build, 0, sizeof(build));
}

void menu_tags_unloaded(
	void)
{
	mcc_menu_preview_clear();
	if (menu_tags.loaded || menu_tags.block_count)
		menu_tags_release();
}

char const *pc_menus_root_name(
	void)
{
	/* (debug.menu_open: a screen to see, a profile being edited for the
	settings' screens; or debug.menu_studio's, menu_studio.c) */
	char const *open = config_string("debug.menu_open")[0] ? config_string("debug.menu_open") :
		config_string("debug.menu_studio");

	if (menu_tags.loaded && *open && build.menus == NULL)
	{
		static char name[300];
		long index;
		/* (the screen's name, and after it the functions the game runs as it
		opens the screen, a tab before each: menu_tour_setup's) */
		int length = (int)strcspn(open, "\t");

		snprintf(name, sizeof(name), "%s%.*s", PC_MENU_TAG_PREFIX, length, open);
		for (index = 0; name[index]; index++)
		{
			if (name[index] == '/')
				name[index] = '\\';
		}
		if (tag_loaded(UI_WIDGET_DEFINITION_TAG, name) != NONE)
		{
			extern boolean pc_menu_profile_edit_begin(void);

			extern void menu_tour_setup(char const *functions);

			pc_menu_profile_edit_begin();
			menu_tour_setup(open + length);
			return name;
		}
		platform_log("menus: debug.menu_open: there is no widget named %.*s", length, open);
	}
	return menu_tags.loaded && menu_tags.root[0] ? menu_tags.root : "ui\\shell\\main_menu\\main_menu";
}

boolean pc_menu_tag(
	long tag_index)
{
	/* MCC's independently generated pause widgets use stock widget behavior. */
	{
		extern boolean mcc_pause_owns(long);
		if (mcc_pause_owns(tag_index)) return FALSE;
	}
	return menu_tags.loaded && tag_index != NONE &&
		DATUM_INDEX_TO_ABSOLUTE_INDEX(tag_index) >= menu_tags.original_count;
}

/* where in its widget, and how large, a frame of ui.map's that a menu file
scales is drawn (<frame map=... width= height= x= y=>); FALSE for any other
bitmap, drawn as it is (ui_widget.c) */
boolean pc_menu_frame_placement(
	struct bitmap_data const *bitmap,
	short *x,
	short *y,
	short *width,
	short *height)
{
	long index;

	for (index = 0; index < menu_tags.placement_count; index++)
	{
		if (menu_tags.placements[index].bitmap == bitmap)
		{
			*x = menu_tags.placements[index].x;
			*y = menu_tags.placements[index].y;
			*width = menu_tags.placements[index].width;
			*height = menu_tags.placements[index].height;
			return TRUE;
		}
	}
	return FALSE;
}

/* whether the texture at this address, this size, is a frame of ui.map's
the menus draw scaled (the campaign's levels' pictures and badges), loaded:
hud_hires_upscale_at upscales it, as the HUD's sheets without redraws are */
boolean pc_menu_scaled_frame_at(
	unsigned long address,
	long width,
	long height)
{
	long index;

	for (index = 0; index < menu_tags.placement_count; index++)
	{
		struct bitmap_data const *bitmap = menu_tags.placements[index].bitmap;

		if (bitmap->base_address && (unsigned long)bitmap->base_address == address && bitmap->width == width &&
			bitmap->height == height)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* the radius a menu widget's picture's corners are cut round by, in units
(its radius attribute: ui_widget.c draws it as menu_shapes.c's rounded
picture), 0 for square */
short pc_menu_widget_radius(
	long definition_index)
{
	if (menu_engine_looks() && pc_menu_look_definition(definition_index))
		return menu_tags.looks[DATUM_INDEX_TO_ABSOLUTE_INDEX(definition_index) - menu_tags.original_count].radius;
	return tag_value_get(menu_tags.radii, menu_tags.radius_count, definition_index);
}

/* the generator source that wrote a menu file's widget, by its definition
(its source attribute: tools/menu_sources.py's), NULL for none or a map's
own widget (debug.render_marks: port/linux/game/render_marks.c) */
char const *pc_menu_widget_source(
	long definition_index)
{
	long index;

	if (!menu_tags.sources || definition_index == NONE)
		return NULL;
	index = DATUM_INDEX_TO_ABSOLUTE_INDEX(definition_index) - menu_tags.original_count;
	return index >= 0 && index < menu_tags.source_count ? menu_tags.sources[index] : NULL;
}

/* the room a legends' row's panel was made wider by for it (pause_box_widen:
ui_widget.c lays the row out in it, not narrowing its gaps), by the row's
widget definition; 0 for none */
short pc_menu_legend_room(
	long definition_index)
{
	return tag_value_get(menu_tags.legend_rooms, menu_tags.legend_room_count, definition_index);
}

/* the frame of a map's bitmap that a menu file scales whose texture is at
this address, this size, loaded: its bitmap's tag and its index there
(menu_pictures_render.c: the menus' pictures drawn again) */
boolean pc_menu_map_frame_at(
	unsigned long address,
	long width,
	long height,
	long *tag_index,
	long *frame)
{
	long index;

	for (index = 0; index < menu_tags.placement_count; index++)
	{
		struct bitmap_data const *bitmap = menu_tags.placements[index].bitmap;

		if (bitmap->base_address && (unsigned long)bitmap->base_address == address && bitmap->width == width &&
			bitmap->height == height)
		{
			*tag_index = menu_tags.placements[index].source_tag_index;
			*frame = menu_tags.placements[index].source_frame;
			return TRUE;
		}
	}
	return FALSE;
}

char const *pc_menu_function_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_function_names) ?
		port_function_names[function_index] : NULL;
}

/* a widget definition's list of texts (its label's, a spinner's values'),
else NONE (Settings' panel: menu_functions.c's settings_panel_show) */
long pc_menu_widget_string_list(
	long definition_index)
{
	struct ui_widget_definition *definition = menu_engine_definition(definition_index);

	return definition ? definition->text_label_string_list.index : NONE;
}

/* a widget definition's string_index (port settings help: its label's) */
short pc_menu_string_index(
	long definition_index)
{
	struct ui_widget_definition *definition = menu_engine_definition(definition_index);

	return definition ? definition->string_list_index : NONE;
}

/* the screen the engine opens by name after a network game (the Xbox's),
or ours where the PC version's menus are up: the host's map select, the
lobby (a network game's, and a game of this machine alone's in place of the
Xbox's split screen Select Teams) */
char const *pc_menus_screen(
	char const *name)
{
	static struct
	{
		char const *game_name;
		char const *menu_name;
	} const screens[] =
	{
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_postgame_wrapper",
			"pc\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_wrapper" },
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen",
			"pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen" },
		/* (a game of this machine alone's: the map select and the lobby
		too, its bots in it) */
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\splitscreen_map_select_postgame_wrapper",
			"pc\\main_menu\\multiplayer_type_select\\connected\\connected_map_select_wrapper" },
		{ "ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\pregame\\splitscreen_pregame_wrapper_normal",
			"pc\\main_menu\\multiplayer_type_select\\lobby\\lobby_screen" },
	};
	short index;

	if (!menu_tags.loaded)
		return name;
	for (index = 0; index < NUMBEROF(screens); index++)
	{
		if (!strcmp(name, screens[index].game_name))
			return screens[index].menu_name;
	}
	return name;
}

char const *pc_menu_game_data_input_name(
	long function_index)
{
	return function_index >= 0 && function_index < (long)NUMBEROF(port_game_data_input_names) ?
		port_game_data_input_names[function_index] : NULL;
}

struct pc_menu_setting *pc_menu_setting_get(
	long definition_index)
{
	long index;

	/* (the settings are let go with their map's tags, menu_tags_unloaded:
	a checked build stops on any kept past them) */
	if (menu_tags.setting_count && menu_tags.generation != cache_files_tags_generation() &&
		cache_files_kept_tag_index(menu_tags.settings[0].definition_index, menu_tags.generation,
		"the menus' settings (menu_tags.c)") == NONE)
	{
		return NULL;
	}
	for (index = 0; index < menu_tags.setting_count; index++)
	{
		if (menu_tags.settings[index].definition_index == definition_index)
			return &menu_tags.settings[index];
	}
	return NULL;
}
