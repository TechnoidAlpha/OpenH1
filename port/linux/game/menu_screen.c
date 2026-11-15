/*
MENU_SCREEN.C

The menus' hand-written screens (docs/menu_screens.md), under
display.menu_engine "new": a screen of the menus' files that a hand-written
one takes the place of (screen_places) is opened by ui_widget.c as ever, its
stand-in kept in its history, but its widgets are made here from the
hand-written screen's file (port/assets/menus/screens, read by
port/linux/src/menu_screens.c) in place of its own: its building blocks are
the widgets of port/assets/menus/screens/blocks.xml, each given its words and
place. The engine (menu_engine.c) then draws them and takes their events as
any screen's widgets, so focus, the controller, the mouse, touch and the
menus' sounds are the game's own.

What the screens name is C's, here: an item's action (do="open multiplayer") is a
name in screen_actions, and a binding in its words ({campaign.level}) a name
in screen_bindings; the files never name the game's widgets. A file read
again (hot reload: menu_screens_poll) makes its open screen again, the focus
kept on the item of the same id.
*/

#include "cseries.h"
#include "input/input.h"
#include "interface/event_manager.h"
#include "interface/ui_widget.h"
#include "interface/ui_widget_event_handler_functions.h"
#include "math/integer_math.h"
#include "text/draw_string.h"
#include "tag_files/tag_groups.h"
#include "text/text_group.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"

#include "halo_menus.h"
#include "menu_engine.h"
#include "menu_screens.h"
#include "menu_table.h"
#include "menu_widget.h"
#include "menu_spartan.h"
#include "network_host_controls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
char *tag_get_name(long tag_index);
long pc_menu_event_function_index(char const *name);
boolean menu_tags_open(struct widget_instance *widget, struct event_record *event, char const *screen,
	boolean *deleted);
void menu_tags_back(struct widget_instance *widget, struct event_record *event, boolean *deleted);
long menu_tags_background(long tag_index);
void menu_tags_background_set(long tag_index, long background_tag_index);
long menu_tags_flags(long tag_index);
short menu_tags_legend_growth(long legends_tag);
unsigned long menu_tags_pause_rows(short local_player_index);
int halo_keyboard_prompts(void);
boolean config_boolean(char const *name);
struct widget_instance *ui_widget_port_add_child(struct widget_instance *parent, long tag_index,
	struct widget_instance *before);
/* (menu_functions.c) */
boolean pc_menu_campaign_saved(char *level, char *difficulty, long size);
void pc_menu_browser_visit(char const *tab);
int pc_menu_browser_visit_kind(void);
void pc_menu_main_menu_shown(struct widget_instance *screen);
boolean pc_menu_edit_leave(short local_player_index, boolean save);
long ui_widget_port_previous_screen(short local_player_index);
int halo_keyboard_prompts(void);
long ui_widget_port_text_box_font(struct widget_instance *widget, short *width);
short ui_widget_port_past_move(void);
boolean ui_widget_port_pointer_focused(void);
void event_manager_post_button(short controller_index, short button_index);
short ui_widget_port_wide_scale(short value, short extra);
short ui_widget_port_past_scale(short value, short past);
short ui_widget_port_wide_extra(void);
short ui_widget_port_past_extra(void);
short ui_widget_port_foot_lift(void);
int ui_widget_port_touch_menus(void);
boolean ui_widget_port_hover_focused(void);
boolean ui_widget_port_take_click(short *x, short *y);
/* (menu_functions.c: the server browser's actions, for the hand-written
browser's) */
boolean pc_menu_browser_tab(char const *word);
boolean pc_menu_browser_sort(char const *word);
boolean pc_menu_browser_favorite(short row);
boolean pc_menu_browser_select(struct widget_instance *widget, struct event_record *event, short row,
	boolean *deleted);
/* (menu_functions.c's coop_menu.inc: co-op's screens' bindings and
actions, WHO CAN JOIN's choice kept, the co-op list's fix a host's, co-op's
screen opened) */
int pc_menu_coop_value(char const *name, char *value, unsigned long size);
int pc_menu_coop_action(struct widget_instance *widget, struct event_record *event, char const *name, int *deleted);
char const *pc_menu_coop_who_word(void);
int pc_menu_coop_fix_hosts(void);
void pc_menu_coop_opened(void);

/* ---------- constants */

#define UI_WIDGET_DEFINITION_TAG 'DeLa'

/* the screens open at once (as the engine's: ENGINE_MAXIMUM_SCREENS), and
the most items a screen's list makes */
#define SCREEN_MAXIMUM_OPEN 8
#define SCREEN_MAXIMUM_ITEMS 24
#define SCREEN_ID_LENGTH 32
#define SCREEN_ACTION_LENGTH 48
#define SCREEN_MAXIMUM_BUTTONS 4

/* the layout, in the menus' 640 by 480 units: the title art's room at the
top (its picture TITLE_ART_TOP down in it, as the main menu has it), an
item's height and the room under it for its hint (each item's, so that the
focus moving does not move the items), the button-hint bar's room at the
foot, and the blocks' widths (blocks.xml's) */
#define SCREEN_HEIGHT 480
#define SCREEN_WIDTH 640
#define TITLE_ART_ROOM 247
#define TITLE_ART_TOP 28
#define TITLE_ART_LEAST_ROOM 160
#define ITEM_WIDTH 256
#define ITEM_HEIGHT 38
#define HINT_WIDTH 440
#define HINT_TOP 34
#define ITEM_STEP 54
/* an item with no hint: the PC version's main menu's step, and an item drawn
as a picture: the PC version's items' box (256 by 33) */
#define ITEM_STEP_PLAIN 36
#define PICTURE_HEIGHT 33
#define BITMAP_GROUP_TAG 'bitm'
/* cards (blocks.xml's screens/card: the campaign's mission card, 158 by
240): their gap, and their row's room, below them too */
#define CARD_WIDTH 158
#define CARD_GAP 24
#define CARDS_ROOM 260
/* (the card picture's own height, and the margin above and below the cards
filling their room) */
#define CARD_HEIGHT 240
#define CARD_MARGIN 10
/* a card's title drawn larger than its font (large_ui's) */
#define CARD_TITLE_SCALE 1.4f
/* an inner screen's header's room (its picture where the campaign menu's is),
or the tall layout's band's (port_settings.py's TALL_BAND) */
#define HEADER_ROOM 80
#define HEADER_ROOM_TALL 40
/* the header's words drawn larger than their font (the heading's), as the
tall layout's titles (TALL_TITLE_SCALE) */
#define HEADER_TITLE_SCALE 1.4f
/* the button bar: its room at the foot (port_settings.py's TALL_FOOTER), and
its prompts as the tall layout's footer's (Choose Mission's: port_settings.py's
_missions), from the room's top, the screen's margin (WIDE_MARGIN) at either
side, each as wide as its block (BACK's at the left, the rest's at the
right, this far apart) */
#define BUTTONS_ROOM 37
#define BUTTONS_MARGIN 24
#define PROMPTS_TOP 5
#define PROMPT_TOP 1
#define PROMPT_WIDTH 124
#define PROMPT_LEFT_WIDTH 96
#define PROMPT_GAP 8
/* an item's label drawn larger than its font (large_ui's): at 4:3, and
at 16:9 and wider */
#define ITEM_SCALE_LEAST 1.45f
#define ITEM_SCALE_MOST 1.6f
/* (menu_tags.c's WIDE_LAYOUT_EXTRA: 16:9's width past 640) */
#define WIDE_LAYOUT_EXTRA 212
/* the profile's colors (<colors>): the PC version's picker's, as many as
the game has, each an item of blocks.xml's screens/color, one under another
at the screen's left; their names the picker's list's */
#define SCREEN_COLOR_COUNT 18
#define SCREEN_COLOR_STEP 19
#define SCREEN_COLOR_X 96
#define SCREEN_COLORS_NAMES "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\colors_list"
/* the original picture of a color (blocks.xml's screens/spartan: ui.map's
player_color_marine_large, 143 by 294, a frame a color and the last
random), drawn where no Spartan can be */
#define SCREEN_SPARTAN_PICTURE_WIDTH 143
#define SCREEN_SPARTAN_PICTURE_HEIGHT 294
#define SCREEN_SPARTAN_RANDOM_FRAME 18
/* (menu_functions.c's BUTTON_B, BUTTON_X, BUTTON_Y: the buttons a prompt presses) */
/* a <box>'s layout, the Xbox's pause menu's as the port has it (menu_tags.c's
PAUSE_BOX_*): its first row this far below its top, its rows this far apart
and this tall, its legend this far below its last row, its foot this far
below the legend's top; the frame's left and right pieces' width, its first
frame's rows; its rows' width; and the objectives' box beside it: this far
from its right, as wide, its frame's first rows, its title's and its text's
places in it */
#define BOX_ROWS_TOP 9
#define BOX_ROW_PITCH 30
#define BOX_ROW_HEIGHT 27
#define BOX_LEGEND_GAP 14
#define BOX_LEGEND_BOTTOM 25
#define BOX_SIDE 16
#define BOX_FIRST_ROWS 3
#define BOX_FRAMES 5
#define BOX_ROW_WIDTH 202
#define OBJECTIVES_GAP 5
#define OBJECTIVES_MIDDLE 253
#define OBJECTIVES_FIRST_ROWS 5
#define OBJECTIVES_FRAMES 3
#define OBJECTIVES_X 15
#define OBJECTIVES_TITLE_Y 4
#define OBJECTIVES_TEXT_Y 33
/* the parts a box moves as its legend grows (its frame's three, the
objectives' five) */
#define BOX_MAXIMUM_PARTS 8
/* a widget definition's flag: the game paused while it is open */
#define SCREEN_PAUSE_GAME_FLAG 0x2
/* the Xbox's legend row under its pause menu, which a <legend> shows */
#define BOX_LEGENDS "ui\\shell\\main_menu\\button_key_sm"
/* the Xbox's pause menu's rows' frame, framing the focused one */
#define BOX_ROW_PICTURE "ui\\shell\\bitmaps\\menu_bkds"

/* a <dialog>'s layout, the PC version's Quit question's (quit_screen): its box
(alert_bkd) as wide as Quit's at least and else as its choices need, its
picture's transparent right this wide; as tall as its words need, in the
middle of the screen: its top band this tall (to its line), its words this
far below it and above its bottom band, a line of them this tall, at most
this many, and its bottom band (its foot, dialog_foot's, the box's picture
cut above it) this tall, from the foot's top this far down; its title and
words this far in from its left (its title's top this far down), and its
choices (each Quit's OK's button) this wide side by side in its bottom band,
this far above the box's foot */
#define DIALOG_LEAST_WIDTH 280
#define DIALOG_PICTURE_MARGIN 18
#define DIALOG_HEAD 29
#define DIALOG_TEXT_ROOM 7
#define DIALOG_LINE 21
#define DIALOG_MOST_LINES 4
#define DIALOG_FOOT 32
#define DIALOG_FOOT_BAND 30
#define DIALOG_TEXT_X 8
#define DIALOG_TITLE_Y 4
#define DIALOG_CHOICE_WIDTH 128
#define DIALOG_CHOICES_UP 27
#define DIALOG_CHOICES_ROOM 5

#define SCREEN_BUTTON_B 1
#define SCREEN_BUTTON_X 2
#define SCREEN_BUTTON_Y 3
/* (menu_engine.c's ENGINE_EVENT_BUTTON: the event manager's button event) */
#define SCREEN_EVENT_BUTTON 3
/* (and its ENGINE_EVENT_LEFT_STICK: the left stick pushed to an edge) */
#define SCREEN_EVENT_LEFT_STICK 1

/* ---------- the registry: what the files may name */

/* an action: the function the item's own widget ran (NULL: none) and the
one before it (the mode the server browser is in), then the screen opened
if it succeeded, else the one opened if it failed (the profile asked for:
NULL, none) */
struct screen_action
{
	char const *name;
	/* (the server browser's tab for this visit: "all", "coop"; NULL, the
	player's own) */
	char const *browser_tab;
	char const *before;
	char const *run;
	/* (run once run has succeeded, before the screen opens: the campaign's
	state read, as its menu's opening read it) */
	char const *then;
	char const *open;
	char const *otherwise;
	/* (C's own, run in place of the rest: an action of a hand-written
	screen's blocks, its name given, and the row of a table it is for,
	screen_action_row) */
	boolean (*call)(struct widget_instance *widget, struct event_record *event, char const *name, boolean *deleted);
};

static boolean screen_call_browser(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_search(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_profile(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_color(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_lobby(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_close(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_edit(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_coop(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static boolean screen_call_add_player(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
/* the row of the table an action is run for (0 up, on the table), NONE for
none (a key's, a button's) */
static short screen_action_row = NONE;

static struct screen_action const screen_actions[] =
{
	/* (the Campaign screen, screens/campaign.xml) */
	{ "open campaign", NULL, NULL, NULL, NULL, "screens/campaign", NULL },
	/* (Choose Mission, the campaign's missions as cards, as the campaign
	menu's CHOOSE MISSION opens it once the menu has read the profile's
	campaign: a profile made first if there is none, as the PC version's
	CAMPAIGN, and then Choose Mission at once: port_settings.py's
	_solo_new_profile) */
	{ "open solo", NULL, "port coop host end", "new game if no plyr profiles", "port campaign init", "main_menu/campaign/mission_screen",
		"main_menu/campaign/solo_creating_profile" },
	/* (the server browser, as Multiplayer's SERVER BROWSER opens it: its mode
	the browser's; every game this visit) */
	{ "open multiplayer", "all", "mp type set mode", "new game if no plyr profiles", NULL,
		"main_menu/multiplayer_type_select/join_game/join_game_screen",
		"main_menu/profile_manager/multiplayer_creating_profile" },
	/* (co-op's one screen, screens/coop.xml, every way in: Campaign's
	CO-OP, the server browser's CO-OP tab, Multiplayer's CO-OP CAMPAIGN) */
	{ "open coop", NULL, NULL, NULL, NULL, "screens/coop", NULL },
	/* (the browser's HOST & MORE: the PC version's Multiplayer screen, the
	hub; co-op's own screen) */
	{ "open host", NULL, NULL, NULL, NULL, "main_menu/multiplayer_type_select/multiplayer_type_select_screen", NULL },
	{ "open host coop", NULL, NULL, NULL, NULL, "screens/coop", NULL },
	/* (co-op's, screens/coop.xml: HOST, Choose Mission and Choose
	Difficulty as the campaign's (a profile made first if there is none),
	HOST's choosing begun; JOIN, the co-op list (screens/coop_join.xml);
	SPLIT SCREEN, as the hub's SPLIT-SCREEN CO-OP. WHO CAN JOIN's cards
	(screens/coop_who.xml) host the game; the co-op list's fix and JOIN BY
	LINK, the lobby's fix: coop_menu.inc) */
	{ "coop host", NULL, "port coop host begin", "new game if no plyr profiles", "port campaign init",
		"main_menu/campaign/mission_screen", "main_menu/campaign/solo_creating_profile" },
	/* (a profile made first if there is none, then the co-op list at once,
	as HOST goes on to Choose Mission: port_settings.py's
	_coop_join_new_profile) */
	{ "coop join", NULL, NULL, "new game if no plyr profiles", NULL, "screens/coop_join",
		"main_menu/campaign/coop_join_creating_profile" },
	{ "coop split", NULL, NULL, "port coop begin", NULL, "main_menu/multiplayer_type_select/coop/player_2_profile_screen",
		NULL },
	{ "coop host friends", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_coop },
	{ "coop host anyone", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_coop },
	{ "coop host invite", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_coop },
	{ "coop fix", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_coop },
	{ "coop join link", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_coop },
	{ "lobby fix", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_coop },
	{ "open settings", NULL, NULL, "port settings begin", NULL,
		"main_menu/settings_select/player_setup/player_profile_edit/video_settings/video_settings_screen", NULL },
	{ "open training", NULL, NULL, NULL, NULL, "main_menu/training/training_screen", NULL },
	/* (the quit question, as QUIT and B on the main menu open it, and its OK:
	screens/quit.xml) */
	{ "quit", NULL, NULL, NULL, NULL, "main_menu/quit_select/quit_screen", NULL },
	{ "quit game", NULL, NULL, "main menu quit game", NULL, NULL, NULL },
	/* (back to the screen before, as B goes: a pop-up's BACK and CANCEL) */
	{ "back", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_close },
	/* (the question as a screen that edits a profile or a gametype is left
	with changes: screens/save_changes.xml's SAVE and DON'T SAVE, back to the
	editor, which saves them as its OK does, or leaves them) */
	{ "edit save", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_edit },
	{ "edit discard", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_edit },
	/* (a screen's searches, <search>: the next one typed into, and every
	one's words cleared) */
	{ "search next", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_search },
	{ "search clear", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_search },
	/* (the server browser's, screens/browser.xml: its back (a search typed
	into cleared, else the browser's own back), its HOST button (the hub,
	or co-op's page), its tabs, its sort (a column's, X the next), a game
	made a favorite, joined (a row, or JOIN GAME), the list asked for again
	(Join by Link's: the link copied joined), its Filters page) */
	{ "browser back", NULL, NULL, "gamespy back handler", NULL, NULL, NULL },
	{ "browser host", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser tab all", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser tab dedicated", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser tab openh1", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser tab legacy", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser tab coop", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser tab favorites", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser sort players", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser sort ping", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser sort server", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser sort map", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser sort mode", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser sort next", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser favorite", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser join", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_browser },
	{ "browser refresh", NULL, NULL, "port browser refresh", NULL, NULL, NULL },
	{ "browser filters", NULL, NULL, "port browser filters", NULL,
		"main_menu/settings_select/player_setup/player_profile_edit/network_setup/browser_filters/browser_filters_screen",
		NULL },
	/* (Select Profile's, screens/profile_select.xml, as the generated
	screen's: a row's profile made player 1's and back to the screen
	before, or on the row that makes one its question; CREATE NEW's (Y) a
	new profile's name asked; DELETE's (X) its question, for the chosen
	row's profile) */
	{ "profile select", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_profile },
	{ "profile new", NULL, NULL, "new campaign chosen", NULL, "main_menu/profile_manager/pm_empty_creating_profile",
		NULL },
	{ "profile delete", NULL, NULL, "request del player profile", NULL,
		"error/confirm_delete_profile_modal_fullscreen", NULL },
	/* (Profile Color's, screens/profile_color.xml: the row's color made
	the edited profile's, and back to the screen before) */
	{ "profile color", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_color },
	/* (the lobby's, screens/lobby.xml, as the generated lobby's
	(menu_functions.c's lobby_menu.inc): a row chosen (a click its lane's
	team, else the host's start), the start, a team (left and right on the
	players, CHANGE TEAM), the invite copied, Server Setup, Host Controls,
	another player of this machine, and the bots' choices stepped) */
	{ "lobby row", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	/* (its Start and START GAME: a controller not in the game joins, on Add
	Player's screen; a player's starts the host's game) */
	{ "lobby start", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_add_player },
	{ "lobby team red", NULL, NULL, "port lobby team red", NULL, NULL, NULL },
	{ "lobby team blue", NULL, NULL, "port lobby team blue", NULL, NULL, NULL },
	{ "lobby team toggle", NULL, NULL, "port lobby team toggle", NULL, NULL, NULL },
	{ "lobby invite", NULL, NULL, "port lobby invite", NULL, NULL, NULL },
	{ "lobby settings", NULL, NULL, "port lobby settings", NULL, NULL, NULL },
	{ "lobby host", NULL, NULL, "port host open", NULL, "main_menu/host_controls/host_controls_screen", NULL },
	{ "lobby add player", NULL, NULL, "port lobby add player", NULL, NULL, NULL },
	{ "lobby bots left", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby bots right", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby skill left", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby skill right", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby teams left", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby teams right", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	/* (the lobby's chat: a line begun, the on-screen keyboard's on a
	controller or a touchscreen) */
	{ "lobby chat", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	/* (the lobby's panels: a player's card opened (a row's) and its ADD
	FRIEND, MUTE and KICK; INVITE FRIENDS, its friend stepped and invited;
	either closed) */
	{ "lobby card", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby card friend", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby card mute", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby card kick", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby friends", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby friend left", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby friend right", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby friend invite", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	{ "lobby close", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_lobby },
	/* (Add Player's, screens/add_player.xml (menu_functions.c's): those joining
	brought into the game (DONE, a player's START), or let go of (B, BACK);
	either back to the screen before) */
	{ "add player done", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_add_player },
	{ "add player back", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_add_player },
	/* (the pause menu's, screens/pause.xml and pause_campaign.xml, as its
	rows did on the map's: the screen closed (RESUME GAME), or every screen
	(B; the campaign's RESUME GAME); the player out of a multiplayer game
	(LEAVE GAME, the screen closed); the host's Host Controls, END GAME and
	Basic Training's CHAPTERS (its first local player's alone);
	network co-op's FRIENDLY FIRE; the campaign's REVERT TO SAVED and
	RESTART LEVEL, each its question first, and SAVE AND QUIT) */
	{ "close", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_close },
	{ "close all", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_close },
	{ "pause leave", NULL, NULL, NULL, NULL, NULL, NULL, screen_call_close },
	{ "open host controls", NULL, NULL, "port host open", NULL, "main_menu/host_controls/host_controls_screen", NULL },
	{ "pause end game", NULL, NULL, "port pause end game", NULL, NULL, NULL },
	{ "pause chapters", NULL, NULL, "port training open", NULL, "main_menu/training/training_screen", NULL },
	{ "pause friendly fire", NULL, NULL, "port coop friendly fire", NULL, NULL, NULL },
	{ "pause revert", NULL, NULL, NULL, NULL, "ui\\shell\\error\\confirm_restartatsave_modal_fullscreen", NULL },
	{ "pause restart", NULL, NULL, NULL, NULL, "ui\\shell\\error\\confirm_restartlevel_modal_fullscreen", NULL },
	{ "pause quit", NULL, NULL, "pause game return to main menu", NULL, NULL, NULL },
};

static char const *const screen_action_names[] =
{
	"open campaign", "open solo", "open multiplayer", "open coop", "open host", "open host coop", "open settings",
	"coop host", "coop join", "coop split", "coop host friends", "coop host anyone", "coop host invite", "coop fix",
	"coop join link", "lobby fix",
	"open training", "quit", "quit game", "back", "edit save", "edit discard", "search next", "search clear", "browser back", "browser host", "browser tab all",
	"browser tab dedicated", "browser tab openh1", "browser tab legacy", "browser tab coop", "browser tab favorites",
	"browser sort players", "browser sort ping", "browser sort server", "browser sort map", "browser sort mode",
	"browser sort next", "browser favorite", "browser join", "browser refresh", "browser filters", "profile select",
	"profile new", "profile delete", "profile color", "lobby row", "lobby start", "lobby team red", "lobby team blue",
	"lobby team toggle", "lobby invite", "lobby settings", "lobby host", "lobby add player", "lobby bots left",
	"lobby bots right", "lobby skill left", "lobby skill right", "lobby teams left", "lobby teams right",
	"lobby chat", "lobby card", "lobby card friend", "lobby card mute", "lobby card kick", "lobby friends",
	"lobby friend left", "lobby friend right", "lobby friend invite", "lobby close",
	"add player done", "add player back",
	"close", "close all", "pause leave", "open host controls", "pause end game", "pause chapters",
	"pause friendly fire", "pause revert", "pause restart", "pause quit",
};

/* a binding's value: the saved game's mission and difficulty (player 1's
profile's), none without one; the screen's local player (its widget's: a
split screen's pause menu is its player's), whose the pause.* rows are */
struct screen_values
{
	boolean read;
	boolean saved;
	char level[64];
	char difficulty[64];
	short local_player_index;
};

static char const *const screen_binding_names[] =
{
	"campaign.level", "campaign.difficulty", "browser.title", "browser.counts", "browser.empty", "browser.searched",
	"browser.host", "browser.tab", "browser.sort", "browser.sort.title", "browser.search.server", "browser.search.map",
	"browser.search.any", "browser.panel.title", "browser.panel.where", "browser.panel.kind",
	"browser.panel.kind.tone", "browser.panel.tick", "browser.panel.opence", "browser.panel.players",
	"browser.panel.ping", "browser.panel.status", "browser.joinable", "browser.server", "browser.link",
	"browser.filtered", "browser.fix", "coop.listing", "coop.link", "coop.problem", "coop.fix", "profile.title", "profile.name", "profile.level", "profile.difficulty", "profile.controls",
	"profile.color", "profile.chosen", "profile.current", "color.profile", "color.title", "color.spartan",
	"color.current", "color.about", "lobby.title", "lobby.subtitle", "lobby.subtitle.short", "lobby.red",
	"lobby.red.count", "lobby.red.tone", "lobby.blue", "lobby.blue.count", "lobby.blue.tone", "lobby.gametype",
	"lobby.map", "lobby.limits", "lobby.kind", "lobby.kind.tone", "lobby.tick", "lobby.opence", "lobby.after",
	"lobby.bots", "lobby.bots.shown", "lobby.bots.some", "lobby.bots.locked", "lobby.bots.count.locked", "lobby.skill", "lobby.teams",
	"lobby.status", "lobby.start", "lobby.host", "lobby.settings", "lobby.controls", "lobby.waiting",
	"lobby.team.change", "lobby.invite", "lobby.add", "lobby.chat", "lobby.details", "lobby.card", "lobby.inviting",
	"lobby.panel.open", "lobby.card.name", "lobby.card.color", "lobby.card.ping", "lobby.card.platform",
	"lobby.card.party", "lobby.card.note", "lobby.card.facts", "lobby.card.friend",
	"lobby.card.mutable", "lobby.card.mute", "lobby.card.kick", "lobby.friends", "lobby.friends.count",
	"lobby.friend", "lobby.friend.where", "lobby.friends.said", "lobby.map.picture", "lobby.description",
	"lobby.where", "lobby.lineup", "lobby.lineup.names", "add.count", "add.title", "add.profile", "add.color", "add.help",
	"add.done", "pause.friendly_fire", "pause.host_controls", "pause.end_game",
	"pause.chapters",
};

/* the row sources a table may name (menu_table.h), and their names */
static struct menu_table_source const *const screen_sources[] =
{
	&pc_menu_browser_source,
	&pc_menu_profile_source,
	&pc_menu_lobby_source,
	&pc_menu_color_source,
	&pc_menu_add_player_source,
};

static char const *const screen_source_names[] =
{
	"browser.games",
	"profiles",
	"lobby.players",
	"profile.colors",
	"add.players",
};

/* the chats a <chat> may name (menu_table.h), and their names */
static struct menu_chat_source const *const screen_chats[] =
{
	&pc_menu_lobby_chat,
};

static char const *const screen_chat_names[] =
{
	"lobby.chat",
};

static int screen_value(void *context, char const *name, char *value, unsigned long size)
{
	struct screen_values *values = context;

	if (!values->read && !strncmp(name, "campaign.", 9))
	{
		values->read = TRUE;
		values->saved = pc_menu_campaign_saved(values->level, values->difficulty, (long)sizeof(values->level));
	}
	/* (the server browser's: menu_functions.c's) */
	if (!strncmp(name, "browser.", 8))
		return pc_menu_browser_value(name, value, size);
	/* (Select Profile's: menu_functions.c's) */
	if (!strncmp(name, "profile.", 8))
		return pc_menu_profile_value(name, value, size);
	/* (Profile Color's: menu_functions.c's) */
	if (!strncmp(name, "color.", 6))
		return pc_menu_color_value(name, value, size);
	/* (the lobby's: menu_functions.c's lobby_menu.inc) */
	if (!strncmp(name, "lobby.", 6))
		return pc_menu_lobby_value(name, value, size);
	/* (Add Player's: menu_functions.c's) */
	if (!strncmp(name, "add.", 4))
		return pc_menu_add_player_value(name, value, size);
	/* (co-op's, the lobby's WHO CAN JOIN and what keeps people from the
	game: coop_menu.inc) */
	if (!strncmp(name, "coop.", 5))
		return pc_menu_coop_value(name, value, size);
	/* (the pause menu's: the rows this machine's has for the screen's local
	player, as menu_tags.c adds them to the map's, the host's for its first
	local player alone; FRIENDLY FIRE's words, this machine's choice) */
	if (!strncmp(name, "pause.", 6))
	{
		unsigned long rows = menu_tags_pause_rows(values->local_player_index);

		if (!strcmp(name, "pause.friendly_fire") && TEST_FLAG(rows, _host_controls_pause_friendly_fire))
			snprintf(value, size, config_boolean("network.coop_own_friendly_fire") ? "FRIENDLY FIRE ON" :
				"FRIENDLY FIRE OFF");
		else if ((!strcmp(name, "pause.host_controls") && TEST_FLAG(rows, _host_controls_pause_host_controls)) ||
			(!strcmp(name, "pause.end_game") && TEST_FLAG(rows, _host_controls_pause_end_game)) ||
			(!strcmp(name, "pause.chapters") && TEST_FLAG(rows, _host_controls_pause_chapters)))
		{
			snprintf(value, size, "yes");
		}
		else
			return 0;
		return 1;
	}
	if (!strcmp(name, "campaign.level") && values->saved)
		snprintf(value, size, "%s", values->level);
	else if (!strcmp(name, "campaign.difficulty") && values->saved)
		snprintf(value, size, "%s", values->difficulty);
	else
		return 0;
	return value[0] != 0;
}

/* a binding's value set (a search's words typed: <search value=>) */
static void screen_value_set(char const *name, char const *text)
{
	/* (the server browser's searches: menu_functions.c's) */
	if (name && !strncmp(name, "browser.search.", 15))
		pc_menu_browser_search_set(name, text);
}

static struct menu_screen_names const screen_names =
{
	screen_action_names, NUMBEROF(screen_action_names),
	screen_binding_names, NUMBEROF(screen_binding_names),
	screen_source_names, NUMBEROF(screen_source_names),
	screen_chat_names, NUMBEROF(screen_chat_names),
};

/* the menus' screens a hand-written one takes the place of, by its name, and
what it does each frame it is drawn (the main menu: the update and crash
questions may open over it) */
struct screen_place
{
	char const *screen;
	char const *widget;
	void (*shown)(struct widget_instance *screen);
	/* (whether it takes the screen's place this time: NULL, always) */
	int (*takes)(void);
	/* (a controller's key (MENU_SCREEN_KEY_*), before the screen's own and its
	focus take it: MENU_SCREEN_KEY_FREE, theirs; NULL, all theirs) */
	int (*key)(short controller, int key);
};

static struct screen_place const screen_places[] =
{
	{ "main", "main_menu\\main_menu", pc_menu_main_menu_shown, NULL },
	{ "campaign", "screens\\campaign", NULL, NULL },
	/* (co-op's one screen: its own, and the hub's co-op page, which
	Multiplayer's CO-OP CAMPAIGN opens; HOST's WHO CAN JOIN; the co-op
	list) */
	{ "coop", "screens\\coop", NULL, NULL },
	{ "coop", "main_menu\\multiplayer_type_select\\hub\\coop_screen", NULL, NULL },
	{ "coop_who", "screens\\coop_who", NULL, NULL },
	{ "coop_join", "screens\\coop_join", NULL, NULL },
	/* (Join Game's, in each of its modes: the server browser, the local
	network's list, Join by Link) */
	{ "browser", "main_menu\\multiplayer_type_select\\join_game\\join_game_screen", NULL, NULL },
	/* (Settings' COLOR: the profile's color, its Spartan beside it) */
	{ "profile_color", "main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\color_select_screen",
		NULL, NULL },
	/* (Settings' MANAGE PROFILES: the profiles, the chosen one's Spartan
	beside them) */
	{ "profile_select", "main_menu\\profile_manager\\player_profile_manager", NULL, NULL },
	/* (the lobby, a network game's and a game of this machine alone's: its
	generated screen's own handlers kept (B leaves, START joins another
	player, X adds one, Y the map vote or the invite), and what its list did
	each frame done: pc_menu_lobby_frame) */
	{ "lobby", "main_menu\\multiplayer_type_select\\lobby\\lobby_screen", pc_menu_lobby_frame, NULL },
	/* (Add Player, as the lobby's and the preview's ADD PLAYER and another
	controller's START open it: each controller not in the game its own
	keys, menu_functions.c's) */
	{ "add_player", "screens\\add_player", NULL, NULL, pc_menu_add_player_key },
	/* (the pause menu, in place of the map's: menu_screen_pause_name opens
	these, a campaign's or a multiplayer game's, pausing the game or not) */
	{ "pause", "screens\\pause", NULL, NULL },
	{ "pause", "screens\\pause_live", NULL, NULL },
	{ "pause_campaign", "screens\\pause_campaign", NULL, NULL },
	{ "pause_campaign", "screens\\pause_campaign_live", NULL, NULL },
	/* (the pop-ups: the Quit question, in place of the PC version's; the
	question as an editor is left with changes, a screen of its own) */
	{ "quit", "main_menu\\quit_select\\quit_screen", NULL, NULL },
	{ "save_changes", "screens\\save_changes", NULL, NULL },
};

/* ---------- the screens open */

/* a <spartan> of an open screen: its block (its box, in a tall screen, set
each frame), the widget it is drawn at (its original picture, drawn where
no Spartan is: a lineup shows none), its Spartans' first index and their
most, and whether it is shown now */
struct screen_spartan
{
	struct menu_screen_block block;
	struct widget_instance *picture;
	short first;
	short places;
	boolean shown;
};

struct screen_open
{
	struct widget_instance *widget;
	struct screen_place const *place;
	unsigned long generation;
	struct widget_instance *list;
	struct widget_instance *items[SCREEN_MAXIMUM_ITEMS];
	struct widget_instance *hints[SCREEN_MAXIMUM_ITEMS];
	/* (each item's picture's bitmap tag, NONE for its label; a card's, its
	picture part's) */
	long pictures[SCREEN_MAXIMUM_ITEMS];
	long card_pictures[SCREEN_MAXIMUM_ITEMS];
	char ids[SCREEN_MAXIMUM_ITEMS][SCREEN_ID_LENGTH];
	char actions[SCREEN_MAXIMUM_ITEMS][SCREEN_ACTION_LENGTH];
	long item_count;
	char back[SCREEN_ACTION_LENGTH];
	char start[SCREEN_ACTION_LENGTH];
	/* (its header, and the picture its file names for it, NONE for the
	block's own) */
	struct widget_instance *header;
	long header_picture;
	/* (its <colors>: its items the colors, in order; its <spartan>, and the
	original picture drawn where no Spartan is) */
	boolean colors;
	struct screen_spartan spartans[MENU_SCREEN_MAXIMUM_SPARTANS];
	long spartan_count;
	/* (its cards' scale, to fill their room: 0 for their own size; and their
	titles', smaller than CARD_TITLE_SCALE where one would be wider than its
	card at it, every title of the row the same: 0 for CARD_TITLE_SCALE) */
	real card_scale;
	real card_title_scale;
	/* (its button bar's prompts, each its key, and its place in the 640
	and its side, which past 16:9 moves it to the screen's edge each frame) */
	struct widget_instance *prompts[SCREEN_MAXIMUM_BUTTONS];
	char prompt_keys[SCREEN_MAXIMUM_BUTTONS][SCREEN_ID_LENGTH];
	short prompt_x[SCREEN_MAXIMUM_BUTTONS];
	boolean prompt_right[SCREEN_MAXIMUM_BUTTONS];
	long prompt_count;
	/* (its <box>'s: its items' blocks, whose labels' bindings are read
	again each frame (FRIENDLY FIRE's ON and OFF); the parts that move as
	its legend grows while the keyboard's keys are drawn in it (its frame's
	left and middle left by half of it, the middle as much wider, its right
	and the objectives' box right by half), each its place and way, the
	middle's width, and the growth they have now) */
	boolean box;
	struct menu_screen_block const *item_blocks[SCREEN_MAXIMUM_ITEMS];
	struct widget_instance *box_parts[BOX_MAXIMUM_PARTS];
	short box_part_x[BOX_MAXIMUM_PARTS];
	short box_part_way[BOX_MAXIMUM_PARTS];
	long box_part_count;
	struct widget_instance *box_middle;
	struct widget_instance *box_legend;
	short box_growth;
	/* (its <dialog>'s: its box, title and words, drawn as wide as its
	choices need) */
	struct widget_instance *dialog_box;
	struct widget_instance *dialog_title;
	struct widget_instance *dialog_text;
	struct widget_instance *dialog_foot;
	/* (the box and its foot drawn under them twice more, each pair a box
	and its foot: its picture's translucent bands made nearly opaque over
	the screen behind) */
	struct widget_instance *dialog_under[4];
	short dialog_width, dialog_height;
};

static struct
{
	boolean named;
	boolean blocks_found;
	struct screen_open open[SCREEN_MAXIMUM_OPEN];
	/* each screen's item focused last, kept for its next opening (back from
	the screen its item opened) */
	char focused[NUMBEROF(screen_places)][SCREEN_ID_LENGTH];
	/* the blocks' tags, as the menus' tags were last built */
	long prompts_tag;
	long color_tag, spartan_tag;
	long title_art_tag, list_tag, item_tag, hint_tag, prompt_left_tag, button_tag, cards_tag, card_tag, card_title_tag, card_art_tag, card_subtitle_tag, header_tag, prompt_tag, header_text_tag;
	/* (a <box>'s, its <legend>'s and its <objectives>') */
	long box_left_tag, box_center_tag, box_right_tag, box_list_tag, box_item_tag, legend_tag;
	long objectives_left_tag, objectives_center_tag, objectives_right_tag, objectives_title_tag, objectives_text_tag;
	/* (a <dialog>'s) */
	long dialog_tag, dialog_title_tag, dialog_text_tag, dialog_list_tag, dialog_choice_tag, dialog_foot_tag,
		dialog_dim_tag;
} screens = { FALSE };

static void screens_names_give(void)
{
	if (!screens.named)
	{
		screens.named = TRUE;
		menu_screens_names(&screen_names);
	}
}

static struct screen_open *screen_open_of(struct widget_instance const *widget)
{
	long index;

	for (index = 0; widget && index < SCREEN_MAXIMUM_OPEN; index++)
	{
		if (screens.open[index].widget == widget)
			return &screens.open[index];
	}
	return NULL;
}

static struct screen_place const *screen_place_of(long tag_index)
{
	char const *name = tag_index != NONE ? tag_get_name(tag_index) : NULL;
	long index;

	if (!name || strncmp(name, "pc\\", 3))
		return NULL;
	for (index = 0; index < NUMBEROF(screen_places); index++)
	{
		if (!strcmp(name + 3, screen_places[index].widget) &&
			(!screen_places[index].takes || screen_places[index].takes()))
		{
			return &screen_places[index];
		}
	}
	return NULL;
}

static long screen_block_tag(char const *name)
{
	return tag_loaded(UI_WIDGET_DEFINITION_TAG, name);
}

/* the blocks' tags found (FALSE, logged, if one is not there) */
static boolean screen_blocks_found(void)
{
	screens.color_tag = screen_block_tag("pc\\screens\\color");
	screens.spartan_tag = screen_block_tag("pc\\screens\\spartan");
	screens.title_art_tag = screen_block_tag("pc\\screens\\title_art");
	screens.list_tag = screen_block_tag("pc\\screens\\list");
	screens.item_tag = screen_block_tag("pc\\screens\\item");
	screens.hint_tag = screen_block_tag("pc\\screens\\hint");
	screens.prompt_left_tag = screen_block_tag("pc\\screens\\prompt_left");
	screens.button_tag = screen_block_tag("pc\\screens\\button");
	screens.cards_tag = screen_block_tag("pc\\screens\\cards");
	screens.card_tag = screen_block_tag("pc\\screens\\card");
	screens.card_title_tag = screen_block_tag("pc\\screens\\card_title");
	screens.card_art_tag = screen_block_tag("pc\\screens\\card_art");
	screens.header_tag = screen_block_tag("pc\\screens\\header");
	screens.card_subtitle_tag = screen_block_tag("pc\\screens\\card_subtitle");
	screens.prompt_tag = screen_block_tag("pc\\screens\\prompt");
	screens.prompts_tag = screen_block_tag("pc\\screens\\prompts");
	screens.header_text_tag = screen_block_tag("pc\\screens\\header_text");
	screens.box_left_tag = screen_block_tag("pc\\screens\\box_left");
	screens.box_center_tag = screen_block_tag("pc\\screens\\box_center");
	screens.box_right_tag = screen_block_tag("pc\\screens\\box_right");
	screens.box_list_tag = screen_block_tag("pc\\screens\\box_list");
	screens.box_item_tag = screen_block_tag("pc\\screens\\box_item");
	screens.legend_tag = screen_block_tag("pc\\screens\\legend");
	screens.objectives_left_tag = screen_block_tag("pc\\screens\\objectives_left");
	screens.objectives_center_tag = screen_block_tag("pc\\screens\\objectives_center");
	screens.objectives_right_tag = screen_block_tag("pc\\screens\\objectives_right");
	screens.objectives_title_tag = screen_block_tag("pc\\screens\\objectives_title");
	screens.objectives_text_tag = screen_block_tag("pc\\screens\\objectives_text");
	screens.dialog_tag = screen_block_tag("pc\\screens\\dialog");
	screens.dialog_title_tag = screen_block_tag("pc\\screens\\dialog_title");
	screens.dialog_text_tag = screen_block_tag("pc\\screens\\dialog_text");
	screens.dialog_list_tag = screen_block_tag("pc\\screens\\dialog_list");
	screens.dialog_choice_tag = screen_block_tag("pc\\screens\\dialog_choice");
	screens.dialog_foot_tag = screen_block_tag("pc\\screens\\dialog_foot");
	screens.dialog_dim_tag = screen_block_tag("pc\\screens\\dialog_dim");
	if (screens.title_art_tag == NONE || screens.list_tag == NONE || screens.item_tag == NONE ||
		screens.hint_tag == NONE || screens.prompt_left_tag == NONE || screens.button_tag == NONE ||
		screens.cards_tag == NONE || screens.card_tag == NONE || screens.card_title_tag == NONE ||
		screens.card_art_tag == NONE || screens.header_tag == NONE ||
		screens.card_subtitle_tag == NONE || screens.prompt_tag == NONE || screens.prompts_tag == NONE ||
		screens.header_text_tag == NONE || screens.color_tag == NONE || screens.spartan_tag == NONE ||
		screens.box_left_tag == NONE || screens.box_center_tag == NONE || screens.box_right_tag == NONE ||
		screens.box_list_tag == NONE || screens.box_item_tag == NONE || screens.legend_tag == NONE ||
		screens.objectives_left_tag == NONE || screens.objectives_center_tag == NONE ||
		screens.objectives_right_tag == NONE || screens.objectives_title_tag == NONE ||
		screens.objectives_text_tag == NONE || screens.dialog_tag == NONE || screens.dialog_title_tag == NONE ||
		screens.dialog_text_tag == NONE || screens.dialog_list_tag == NONE || screens.dialog_choice_tag == NONE ||
		screens.dialog_foot_tag == NONE || screens.dialog_dim_tag == NONE)
	{
		platform_log("menus: the screens' blocks (screens/blocks.xml) are not all there");
		screens.blocks_found = FALSE;
		return FALSE;
	}
	screens.blocks_found = TRUE;
	return TRUE;
}

/* a text box's words, from UTF-8 */
static void screen_text_set(struct widget_instance *widget, char const *utf8)
{
	unsigned short text[MENU_SCREEN_MAXIMUM_TEXT];
	long length = halo_menus_utf16(utf8, text, NUMBEROF(text));
	wchar_t *buffer = ui_widget_realloc(widget->parameters.text_box.text, (word)(length * (long)sizeof(wchar_t)),
		__FILE__, __LINE__);

	if (!buffer)
		return;
	csmemcpy(buffer, text, length * (long)sizeof(wchar_t));
	widget->parameters.text_box.text = buffer;
}

/* a text box's words, from the game's own (a string list's) */
static void screen_text_set_wide(struct widget_instance *widget, wchar_t const *words)
{
	long length = 0;
	wchar_t *buffer;

	while (words[length] && length < MENU_SCREEN_MAXIMUM_TEXT - 1)
		length++;
	buffer = ui_widget_realloc(widget->parameters.text_box.text, (word)((length + 1) * (long)sizeof(wchar_t)),
		__FILE__, __LINE__);
	if (!buffer)
		return;
	csmemcpy(buffer, words, length * (long)sizeof(wchar_t));
	buffer[length] = 0;
	widget->parameters.text_box.text = buffer;
}

/* a picture of the menus' files' bitmap tag (its name as the files have it,
"main_menu/menu_versus"), NONE (logged) if there is none */
static long screen_picture_tag(char const *name)
{
	char tag_name[256];
	long index, tag_index;

	snprintf(tag_name, sizeof(tag_name), "pc\\%s", name);
	for (index = 0; tag_name[index]; index++)
	{
		if (tag_name[index] == '/')
			tag_name[index] = '\\';
	}
	tag_index = tag_loaded(BITMAP_GROUP_TAG, tag_name);
	if (tag_index == NONE)
		platform_log("menus: there is no picture %s; the item is drawn as its label", name);
	return tag_index;
}

/* a block's widget made in the parent, at x and y on the screen */
static struct widget_instance *screen_widget_add(struct widget_instance *parent, long tag_index, short x, short y)
{
	struct widget_instance *widget = ui_widget_port_add_child(parent, tag_index, NULL);

	if (widget)
	{
		widget->horizontal_offset = (short)(parent->horizontal_offset + x);
		widget->vertical_offset = (short)(parent->vertical_offset + y);
	}
	return widget;
}

/* the bindings' values unread, for the open screen's local player */
static void screen_values_clear(struct screen_values *values, struct screen_open const *open)
{
	memset(values, 0, sizeof(*values));
	values->local_player_index = open && open->widget ? open->widget->local_player_index : NONE;
}

/* whether an item is made: its when holds, and its label's bindings have
values */
static boolean screen_item_shown(struct menu_screen_block const *item, struct screen_values *values, char *label,
	unsigned long size)
{
	char shown[MENU_SCREEN_MAXIMUM_TEXT];

	return menu_screen_when(item->when) && menu_screen_text(item->label, screen_value, values, label, size) &&
		(!item->condition || screen_value(values, item->condition, shown, sizeof(shown)));
}

/* a button's side of the bar: back's the left, the rest's the right (or as
its side says) */
static boolean screen_button_right(struct menu_screen_block const *button)
{
	return button->side ? !strcmp(button->side, "right") : strcmp(button->key, "back") != 0;
}

static boolean tall_build(struct screen_open *open, struct menu_screen const *screen);
static boolean box_build(struct screen_open *open, struct menu_screen const *screen);
static boolean dialog_build(struct screen_open *open, struct menu_screen const *screen);

/* a <spartan> of the screen's made (the widget it is drawn at: its
picture), its Spartans' indices after those of the screen's <spartan>s
before it: a binding's of the screen's own as many as a lineup has (each
place its color), one in a panel; a profile's its players */
static struct screen_spartan *screen_spartan_add(struct screen_open *open, struct menu_screen_block const *block,
	struct widget_instance *picture)
{
	struct screen_spartan *spartan;
	short first = 0;
	long index;

	if (open->spartan_count >= MENU_SCREEN_MAXIMUM_SPARTANS)
		return NULL;
	for (index = 0; index < open->spartan_count; index++)
		first = (short)(first + open->spartans[index].places);
	spartan = &open->spartans[open->spartan_count];
	memset(spartan, 0, sizeof(*spartan));
	spartan->block = *block;
	spartan->picture = picture;
	spartan->first = first;
	spartan->places = (short)(block->color == MENU_SCREEN_SPARTAN_BINDING ? (block->parent < 0 ?
		MAXIMUM_MENU_SPARTAN_PLACES : 1) : block->color == MENU_SCREEN_SPARTAN_PROFILE ? block->players : 1);
	if (first + spartan->places > MAXIMUM_MENU_SPARTANS)
	{
		platform_log("menus: a screen's <spartan>s are more than %d Spartans; the last is left out",
			(int)MAXIMUM_MENU_SPARTANS);
		return NULL;
	}
	spartan->shown = TRUE;
	open->spartan_count++;
	return spartan;
}

/* the screen's widgets made from the hand-written screen, in place of
whatever it had: FALSE (nothing changed) if they cannot be */
/* a card's title measured: the row's titles drawn smaller than
CARD_TITLE_SCALE (all of them, so that they match) where this one would be
wider than its box at it (SPLIT SCREEN's) */
#define CARD_TITLE_MARGIN 6
static void screen_card_title_fit(struct screen_open *open, struct widget_instance *title, char const *label)
{
	unsigned short wide[MENU_SCREEN_MAXIMUM_TEXT];
	short index, width, room;
	real scale;

	for (index = 0; label[index] && index < MENU_SCREEN_MAXIMUM_TEXT - 1; index++)
		wide[index] = (unsigned char)label[index];
	wide[index] = 0;
	width = pc_menu_text_width(title, wide);
	room = (short)(CARD_WIDTH - 2 * CARD_TITLE_MARGIN - 2 * CARD_TITLE_MARGIN);
	if (width <= 0 || width * CARD_TITLE_SCALE <= room)
		return;
	scale = (real)room / (real)width;
	if (scale < 1.0f)
		scale = 1.0f;
	if (open->card_title_scale <= 0.0f || scale < open->card_title_scale)
		open->card_title_scale = scale;
}

static boolean screen_build(struct screen_open *open, struct menu_screen const *screen)
{
	struct widget_instance *widget = open->widget;
	struct screen_values values;
	char label[MENU_SCREEN_MAXIMUM_TEXT];
	char text[MENU_SCREEN_MAXIMUM_TEXT];
	boolean shown[MENU_SCREEN_MAXIMUM_BLOCKS];
	short heights[MENU_SCREEN_MAXIMUM_BLOCKS];
	char const *focus_id;
	short fixed = 0, spaces = 0, spaces_left, extra, top, shrink = 0;
	long index, place_index = open->place - screen_places;
	struct widget_instance *focus = NULL;
	long focus_index = 0;

	if (!screen_blocks_found())
		return FALSE;
	/* (a screen with a table: the tall layout's, menu_screen_tall.inc) */
	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].kind == MENU_SCREEN_TABLE)
			return tall_build(open, screen);
		/* (a screen with a <box>: the pause menu's layout) */
		if (screen->blocks[index].kind == MENU_SCREEN_BOX)
			return box_build(open, screen);
		/* (a pop-up: the Quit question's box) */
		if (screen->blocks[index].kind == MENU_SCREEN_DIALOG)
			return dialog_build(open, screen);
	}
	screen_values_clear(&values, open);
	/* (each block's room, from the top down: a space shares what is left) */
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *block = &screen->blocks[index];

		heights[index] = 0;
		shown[index] = TRUE;
		switch (block->kind)
		{
		case MENU_SCREEN_TITLE_ART:
			heights[index] = TITLE_ART_ROOM;
			break;
		case MENU_SCREEN_CARDS:
			heights[index] = CARDS_ROOM;
			break;
		case MENU_SCREEN_HEADER:
			heights[index] = block->picture ? HEADER_ROOM : HEADER_ROOM_TALL;
			break;
		case MENU_SCREEN_CARD:
		case MENU_SCREEN_ITEM:
			shown[index] = screen_item_shown(block, &values, label, sizeof(label));
			if (block->kind == MENU_SCREEN_CARD)
				break;
			if (shown[index])
				heights[block->parent] = (short)(heights[block->parent] +
					(block->hint_count ? ITEM_STEP : ITEM_STEP_PLAIN));
			break;
		case MENU_SCREEN_SPACE:
			spaces++;
			break;
		case MENU_SCREEN_BUTTONS:
			heights[index] = BUTTONS_ROOM;
			break;
		case MENU_SCREEN_COLORS:
			heights[index] = SCREEN_COLOR_COUNT * SCREEN_COLOR_STEP;
			break;
		}
	}
	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].parent < 0)
			fixed = (short)(fixed + heights[index]);
	}
	extra = (short)(SCREEN_HEIGHT - fixed);
	/* (too much for the screen: the title art's room the less) */
	if (extra < 0)
	{
		shrink = (short)MIN(-extra, TITLE_ART_ROOM - TITLE_ART_LEAST_ROOM);
		extra = 0;
	}
	/* the screen's own widgets let go of, and its own made */
	widget->focused_child = NULL;
	while (widget->child)
		ui_widget_delete(widget->child);
	open->list = NULL;
	open->item_count = 0;
	open->colors = FALSE;
	open->spartan_count = 0;
	focus_id = screens.focused[place_index][0] ? screens.focused[place_index] : NULL;
	top = 0;
	spaces_left = spaces;
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *block = &screen->blocks[index];
		short height = heights[index];

		if (block->parent >= 0)
			continue;
		switch (block->kind)
		{
		case MENU_SCREEN_TITLE_ART:
			screen_widget_add(widget, screens.title_art_tag, 0, (short)(top + TITLE_ART_TOP - shrink));
			height = (short)(height - shrink);
			break;
		case MENU_SCREEN_SPACE:
			/* (the last the rest of it, so that a bar after it is at the
			screen's foot) */
			spaces_left--;
			height = spaces ? (short)(spaces_left ? extra / spaces : extra - (spaces - 1) * (extra / spaces)) : 0;
			break;
		case MENU_SCREEN_HEADER:
		{
			/* (its picture where the campaign menu has its own; or its
			title's words in the tall layout's band, as Settings' and the
			browsers' titles) */
			struct widget_instance *header = screen_widget_add(widget,
				block->picture ? screens.header_tag : screens.header_text_tag, 0, top);

			open->header_picture = block->picture ? screen_picture_tag(block->picture) : NONE;
			open->header = header;
			if (header && !block->picture)
			{
				menu_screen_text(block->label, screen_value, &values, label, sizeof(label));
				screen_text_set(header, label);
			}
			break;
		}
		case MENU_SCREEN_CARDS:
		{
			long card_index, count = 0;
			short x, card_width, card_gap;

			for (card_index = 0; card_index < screen->block_count; card_index++)
				count += screen->blocks[card_index].kind == MENU_SCREEN_CARD &&
					screen->blocks[card_index].parent == index && shown[card_index];
			open->list = screen_widget_add(widget, screens.cards_tag, 0, 0);
			if (!open->list)
				break;
			if (!focus_id)
				focus_id = block->focus;
			/* (at their own size: a scale, 0 for none, would draw them larger) */
			open->card_scale = 0.0f;
			open->card_title_scale = 0.0f;
			card_width = CARD_WIDTH;
			card_gap = CARD_GAP;
			x = (short)((SCREEN_WIDTH - count * card_width - (count - 1) * card_gap) / 2);
			for (card_index = 0; card_index < screen->block_count; card_index++)
			{
				struct menu_screen_block const *card = &screen->blocks[card_index];
				struct widget_instance *made, *part;

				if (card->kind != MENU_SCREEN_CARD || card->parent != index || !shown[card_index] ||
					open->item_count >= SCREEN_MAXIMUM_ITEMS)
				{
					continue;
				}
				made = ui_widget_port_add_child(open->list, screens.card_tag, NULL);
				if (!made)
					break;
				open->card_pictures[open->item_count] = NONE;
				/* (its place: its parts' are in it) */
				made->horizontal_offset = (short)(open->list->horizontal_offset + x);
				made->vertical_offset = (short)(open->list->vertical_offset + top - shrink + CARD_MARGIN);
				/* (its parts: the picture, the title, the subtitle) */
				part = made->child;
				if (part)
				{
					long picture = card->picture ? screen_picture_tag(card->picture) : NONE;

					part->animation.current_frame_index = (short)(card->frame >= 0 ? card->frame : 0);
					if (picture != NONE)
						open->card_pictures[open->item_count] = picture;
					part = part->next;
				}
				menu_screen_text(card->label, screen_value, &values, label, sizeof(label));
				if (part)
				{
					screen_text_set(part, label);
					screen_card_title_fit(open, part, label);
					part = part->next;
				}
				if (part)
				{
					text[0] = 0;
					if (card->hint_count)
						menu_screen_text(card->hints[0], screen_value, &values, text, sizeof(text));
					screen_text_set(part, text);
				}
				open->items[open->item_count] = made;
				open->hints[open->item_count] = NULL;
				open->pictures[open->item_count] = NONE;
				snprintf(open->ids[open->item_count], SCREEN_ID_LENGTH, "%s", card->id ? card->id : "");
				snprintf(open->actions[open->item_count], SCREEN_ACTION_LENGTH, "%s", card->action);
				if (!focus || (focus_id && card->id && !strcmp(card->id, focus_id)))
				{
					focus = made;
					focus_index = open->item_count;
				}
				open->item_count++;
				x = (short)(x + card_width + card_gap);
			}
			break;
		}
		case MENU_SCREEN_LIST:
		{
			long item_index;
			short y = top;

			open->list = screen_widget_add(widget, screens.list_tag, 0, 0);
			if (!open->list)
				break;
			if (!focus_id)
				focus_id = block->focus;
			for (item_index = 0; item_index < screen->block_count; item_index++)
			{
				struct menu_screen_block const *item = &screen->blocks[item_index];
				struct widget_instance *made;
				long hint;

				if (item->kind != MENU_SCREEN_ITEM || item->parent != index || !shown[item_index] ||
					open->item_count >= SCREEN_MAXIMUM_ITEMS)
				{
					continue;
				}
				made = screen_widget_add(open->list, screens.item_tag, (short)((SCREEN_WIDTH - ITEM_WIDTH) / 2), y);
				if (!made)
					break;
				menu_screen_text(item->label, screen_value, &values, label, sizeof(label));
				open->pictures[open->item_count] = item->picture ? screen_picture_tag(item->picture) : NONE;
				/* (a picture in place of its words, which it stands for) */
				screen_text_set(made, open->pictures[open->item_count] != NONE ? "" : label);
				open->items[open->item_count] = made;
				open->hints[open->item_count] = NULL;
				snprintf(open->ids[open->item_count], SCREEN_ID_LENGTH, "%s", item->id ? item->id : "");
				snprintf(open->actions[open->item_count], SCREEN_ACTION_LENGTH, "%s", item->action);
				/* (its hint: the first whose bindings all have values) */
				for (hint = 0; hint < item->hint_count; hint++)
				{
					if (menu_screen_text(item->hints[hint], screen_value, &values, text, sizeof(text)))
					{
						struct widget_instance *line = screen_widget_add(widget, screens.hint_tag,
							(short)((SCREEN_WIDTH - HINT_WIDTH) / 2), (short)(y + HINT_TOP));

						if (line)
						{
							screen_text_set(line, text);
							line->visible = FALSE;
						}
						open->hints[open->item_count] = line;
						break;
					}
				}
				if (!focus || (focus_id && item->id && !strcmp(item->id, focus_id)))
				{
					focus = made;
					focus_index = open->item_count;
				}
				open->item_count++;
				y = (short)(y + (item->hint_count ? ITEM_STEP : ITEM_STEP_PLAIN));
			}
			break;
		}
		case MENU_SCREEN_COLORS:
		{
			struct player_profile *profile = player_ui_get_edit_player_profile();
			long names = tag_loaded('ustr', SCREEN_COLORS_NAMES);
			long color;

			open->list = screen_widget_add(widget, screens.list_tag, 0, 0);
			if (!open->list)
				break;
			open->colors = TRUE;
			for (color = 0; color < SCREEN_COLOR_COUNT && open->item_count < SCREEN_MAXIMUM_ITEMS; color++)
			{
				struct widget_instance *made = screen_widget_add(open->list, screens.color_tag, SCREEN_COLOR_X,
					(short)(top + color * SCREEN_COLOR_STEP));
				wchar_t const *name = names != NONE ? unicode_string_list_get_string(names, (short)color) : NULL;

				if (!made)
					break;
				if (name)
					screen_text_set_wide(made, name);
				open->items[open->item_count] = made;
				open->hints[open->item_count] = NULL;
				open->pictures[open->item_count] = NONE;
				snprintf(open->ids[open->item_count], SCREEN_ID_LENGTH, "color%ld", color);
				snprintf(open->actions[open->item_count], SCREEN_ACTION_LENGTH, "color %ld", color);
				/* (first, the profile's own color) */
				if (!focus || (profile && profile->primary_color_index == color))
				{
					focus = made;
					focus_index = open->item_count;
				}
				open->item_count++;
			}
			break;
		}
		case MENU_SCREEN_SPARTAN:
			/* (over the screen's other blocks, where its box is: the original
			picture made at the box's foot, shown where no Spartan is) */
			if (!menu_screen_when(block->when))
				break;
			screen_spartan_add(open, block, screen_widget_add(widget, screens.spartan_tag,
				(short)(block->box_x + (block->box_width - SCREEN_SPARTAN_PICTURE_WIDTH) / 2),
				(short)(block->box_y + block->box_height - SCREEN_SPARTAN_PICTURE_HEIGHT)));
			break;
		case MENU_SCREEN_BUTTONS:
		{
			long button;
			short left_x = BUTTONS_MARGIN, right_x = (short)(SCREEN_WIDTH - BUTTONS_MARGIN - PROMPT_WIDTH);
			struct widget_instance *bar =
				screen_widget_add(widget, screens.prompts_tag, 0, (short)(top + PROMPTS_TOP));

			/* (each a prompt, its key's picture, "%a-button": the menus' key
			for it while the keyboard plays, and its word; back at the left,
			the rest from the right, on the footer's line) */
			open->prompt_count = 0;
			for (button = 0; button < screen->block_count && open->prompt_count < SCREEN_MAXIMUM_BUTTONS; button++)
			{
				struct menu_screen_block const *block_button = &screen->blocks[button];
				boolean right;
				struct widget_instance *prompt;
				long made = open->prompt_count;

				if (!bar || block_button->kind != MENU_SCREEN_BUTTON || block_button->parent != index)
					continue;
				right = screen_button_right(block_button);
				/* (in the bar, where it is) */
				prompt = ui_widget_port_add_child(bar, right ? screens.prompt_tag : screens.prompt_left_tag, NULL);
				if (!prompt)
					continue;
				prompt->horizontal_offset = right ? right_x : left_x;
				prompt->vertical_offset = PROMPT_TOP;
				snprintf(text, sizeof(text), "%s %s", !strcmp(block_button->key, "accept") ? "%a-button" :
					!strcmp(block_button->key, "back") ? "%b-button" : !strcmp(block_button->key, "x") ? "%x-button" :
					"%y-button", block_button->label);
				screen_text_set(prompt, text);
				open->prompts[made] = prompt;
				open->prompt_x[made] = right ? right_x : left_x;
				open->prompt_right[made] = right;
				snprintf(open->prompt_keys[made], SCREEN_ID_LENGTH, "%s", block_button->key);
				open->prompt_count++;
				if (right)
					right_x = (short)(right_x - PROMPT_GAP - PROMPT_WIDTH);
				else
					left_x = (short)(left_x + PROMPT_LEFT_WIDTH + PROMPT_GAP);
			}
			break;
		}
		}
		top = (short)(top + height);
	}
	/* (the focus: the item focused last, else the list's focus, else its
	first) */
	if (open->list && focus)
	{
		widget->focused_child = open->list;
		open->list->focused_child = focus;
		open->list->parameters.list.selected_index = (short)focus_index;
	}
	snprintf(open->back, sizeof(open->back), "%s", screen->back ? screen->back : "");
	snprintf(open->start, sizeof(open->start), "%s", screen->start ? screen->start : "");
	open->generation = menu_screens_generation(screen->name);
	return TRUE;
}

static boolean screen_action_run(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted);
static void screen_spartan_frame(struct screen_open *open, long focused);

#include "menu_screen_tall.inc"

/* ---------- a <box> (the pause menu's): the Xbox's box of rows as the port
has it, its legend under them, and the objectives' box beside it */

/* a part of a box made where it is, moved the way the legend's growth
moves it (-1: left by half the growth, the box's middle (way 0): left by
half and as much wider, 1: right by half) */
static struct widget_instance *box_part_add(struct screen_open *open, long tag_index, short x, short y, short way,
	short frame)
{
	struct widget_instance *part = screen_widget_add(open->widget, tag_index, x, y);

	if (!part)
		return NULL;
	part->animation.current_frame_index = frame;
	if (open->box_part_count < BOX_MAXIMUM_PARTS)
	{
		open->box_parts[open->box_part_count] = part;
		open->box_part_x[open->box_part_count] = x;
		open->box_part_way[open->box_part_count] = way;
		open->box_part_count++;
	}
	return part;
}

/* the box's widgets made: its rows those shown (each its if's and its
label's bindings with values), as tall as they need, its frame the one for
that many rows, at its x and from its y (or about it: align="middle"); its
legend under them; the objectives' box beside it, as tall. FALSE if the
screen has no <box> with a <list> */
static boolean box_build(struct screen_open *open, struct menu_screen const *screen)
{
	struct widget_instance *widget = open->widget;
	struct screen_values values;
	char label[MENU_SCREEN_MAXIMUM_TEXT];
	struct menu_screen_block const *box = NULL, *list = NULL, *legend = NULL, *objectives = NULL;
	long index, rows = 0, list_index = NONE;
	short height, top, x, frame;
	struct widget_instance *focus = NULL;
	char const *focus_id;
	long focus_index = 0;

	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *block = &screen->blocks[index];

		if (block->kind == MENU_SCREEN_BOX)
			box = block;
		else if (block->kind == MENU_SCREEN_LIST)
		{
			list = block;
			list_index = index;
		}
		else if (block->kind == MENU_SCREEN_LEGEND)
			legend = block;
		else if (block->kind == MENU_SCREEN_OBJECTIVES)
			objectives = block;
	}
	if (!box || !list)
		return FALSE;
	screen_values_clear(&values, open);
	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].kind == MENU_SCREEN_ITEM && screen->blocks[index].parent == list_index &&
			screen_item_shown(&screen->blocks[index], &values, label, sizeof(label)))
		{
			rows++;
		}
	}
	rows = MAX(rows, 1);
	height = (short)(BOX_ROWS_TOP + (rows - 1) * BOX_ROW_PITCH + BOX_ROW_HEIGHT + BOX_LEGEND_GAP + BOX_LEGEND_BOTTOM);
	top = (short)(box->side && !strcmp(box->side, "middle") ? box->box_y - height / 2 : box->box_y);
	x = (short)box->box_x;
	frame = (short)PIN(rows - BOX_FIRST_ROWS, 0, BOX_FRAMES - 1);
	focus_id = screens.focused[open->place - screen_places][0] ? screens.focused[open->place - screen_places] :
		list->focus;
	/* the screen's own widgets let go of, and its own made */
	widget->focused_child = NULL;
	while (widget->child)
		ui_widget_delete(widget->child);
	open->list = NULL;
	open->item_count = 0;
	open->colors = FALSE;
	open->spartan_count = 0;
	open->header = NULL;
	open->prompt_count = 0;
	open->box = TRUE;
	open->box_part_count = 0;
	open->box_growth = 0;
	/* (its frame: the middle as wide as the box less its sides) */
	box_part_add(open, screens.box_left_tag, x, top, -1, frame);
	open->box_middle = box_part_add(open, screens.box_center_tag, (short)(x + BOX_SIDE), top, 0, frame);
	box_part_add(open, screens.box_right_tag, (short)(x + box->box_width - BOX_SIDE), top, 1, frame);
	/* (its rows, in the middle of it) */
	open->list = screen_widget_add(widget, screens.box_list_tag, 0, 0);
	if (!open->list)
		return FALSE;
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *item = &screen->blocks[index];
		struct widget_instance *made;

		if (item->kind != MENU_SCREEN_ITEM || item->parent != list_index ||
			!screen_item_shown(item, &values, label, sizeof(label)) || open->item_count >= SCREEN_MAXIMUM_ITEMS)
		{
			continue;
		}
		made = screen_widget_add(open->list, screens.box_item_tag, (short)(x + (box->box_width - BOX_ROW_WIDTH) / 2),
			(short)(top + BOX_ROWS_TOP + open->item_count * BOX_ROW_PITCH));
		if (!made)
			break;
		screen_text_set(made, label);
		open->items[open->item_count] = made;
		open->item_blocks[open->item_count] = item;
		open->hints[open->item_count] = NULL;
		open->pictures[open->item_count] = NONE;
		snprintf(open->ids[open->item_count], SCREEN_ID_LENGTH, "%s", item->id ? item->id : "");
		snprintf(open->actions[open->item_count], SCREEN_ACTION_LENGTH, "%s", item->action);
		/* (the item focused last, else the list's focus, else its first) */
		if (!focus || (focus_id && item->id && !strcmp(item->id, focus_id)))
		{
			focus = made;
			focus_index = open->item_count;
		}
		open->item_count++;
	}
	/* (its legend, under its last row) */
	open->box_legend = NULL;
	if (legend)
	{
		long legends = tag_loaded(UI_WIDGET_DEFINITION_TAG, BOX_LEGENDS);

		open->box_legend = screen_widget_add(widget, screens.legend_tag, (short)(x + legend->box_x),
			(short)(top + BOX_ROWS_TOP + (rows - 1) * BOX_ROW_PITCH + BOX_ROW_HEIGHT + BOX_LEGEND_GAP));
		if (open->box_legend && legends != NONE)
		{
			struct widget_instance *row = ui_widget_port_add_child(open->box_legend, legends, NULL);

			/* (where its parent is: children are drawn from their parent's
			place) */
			if (row)
			{
				row->horizontal_offset = 0;
				row->vertical_offset = 0;
			}
		}
	}
	/* (the objectives' box beside it, as tall: its frame for that many
	rows, its title and the game's objectives in it) */
	if (objectives)
	{
		short beside = (short)(x + box->box_width + OBJECTIVES_GAP);
		short frame_beside = (short)PIN(rows - OBJECTIVES_FIRST_ROWS, 0, OBJECTIVES_FRAMES - 1);
		struct widget_instance *title;

		box_part_add(open, screens.objectives_left_tag, beside, top, 1, frame_beside);
		box_part_add(open, screens.objectives_center_tag, (short)(beside + BOX_SIDE), top, 1, frame_beside);
		box_part_add(open, screens.objectives_right_tag, (short)(beside + BOX_SIDE + OBJECTIVES_MIDDLE), top, 1,
			frame_beside);
		title = box_part_add(open, screens.objectives_title_tag, (short)(beside + OBJECTIVES_X),
			(short)(top + OBJECTIVES_TITLE_Y), 1, 0);
		if (title && objectives->label && menu_screen_text(objectives->label, screen_value, &values, label,
			sizeof(label)))
		{
			screen_text_set(title, label);
		}
		box_part_add(open, screens.objectives_text_tag, (short)(beside + OBJECTIVES_X),
			(short)(top + OBJECTIVES_TEXT_Y), 1, 0);
	}
	if (focus)
	{
		widget->focused_child = open->list;
		open->list->focused_child = focus;
		open->list->parameters.list.selected_index = (short)focus_index;
	}
	snprintf(open->back, sizeof(open->back), "%s", screen->back ? screen->back : "");
	open->generation = menu_screens_generation(screen->name);
	return TRUE;
}

/* each frame: its rows' labels with bindings read again (FRIENDLY FIRE's
ON or OFF), and its parts moved as wide as its legend needs while the
keyboard's keys are drawn in it (menu_tags.c's pause_boxes_fit), the
Xbox's width while a controller's buttons are */
static void box_frame(struct screen_open *open)
{
	struct screen_values values;
	char label[MENU_SCREEN_MAXIMUM_TEXT];
	short growth = 0;
	long index;

	screen_values_clear(&values, open);
	for (index = 0; index < open->item_count; index++)
	{
		struct menu_screen_block const *item = open->item_blocks[index];

		if (item && item->label && strchr(item->label, '{') &&
			menu_screen_text(item->label, screen_value, &values, label, sizeof(label)))
		{
			screen_text_set(open->items[index], label);
		}
	}
	if (open->box_legend && halo_keyboard_prompts())
		growth = menu_tags_legend_growth(tag_loaded(UI_WIDGET_DEFINITION_TAG, BOX_LEGENDS));
	open->box_growth = growth;
	for (index = 0; index < open->box_part_count; index++)
	{
		open->box_parts[index]->horizontal_offset = (short)(open->box_part_x[index] +
			(open->box_part_way[index] > 0 ? growth / 2 : -(growth / 2)));
	}
}

/* a box's middle drawn as much wider as its legend's growth, and its rows
with the game map's frame of them (BOX_ROW_PICTURE: the menus have it on
the maps the pause menu is played on, not ui.map, which their files are
made on too): TRUE for them */
static boolean box_picture_of(struct widget_instance const *widget, long *bitmap_tag, rectangle2d *bounds)
{
	long open_index, index;

	for (open_index = 0; widget && open_index < SCREEN_MAXIMUM_OPEN; open_index++)
	{
		struct screen_open const *open = &screens.open[open_index];

		if (!open->widget || !open->box)
			continue;
		if (open->box_middle == widget)
		{
			*bitmap_tag = NONE;
			bounds->x1 = (short)(bounds->x1 + open->box_growth);
			return TRUE;
		}
		for (index = 0; index < open->item_count; index++)
		{
			if (open->items[index] == widget)
			{
				*bitmap_tag = tag_loaded(BITMAP_GROUP_TAG, BOX_ROW_PICTURE);
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* ---------- a <dialog> (a pop-up: the Quit question's, as the PC version's
quit_screen draws it): its box in the middle of the screen, its title, its
words and its choices side by side in its foot */

/* a screen's widgets and all in them taken out of the events and the
pointer's targets: drawn alone (a pop-up's screen behind it) */
static void dialog_still(struct widget_instance *widget)
{
	struct widget_instance *child;

	widget->disabled = TRUE;
	for (child = widget->child; child; child = child->next)
		dialog_still(child);
}

/* the dialog's widgets made: the screen it was opened from behind it, as it
was and dimmed, taking nothing (where that is a generated screen: an editor's
question over the editor; Quit's main menu is a hand-written one, so Quit's
is over the menus' scene, as before); its box as wide as Quit's, or as its
choices need (its picture's ends kept), as tall as its words need, in the
middle of the screen: its title in the box's top band, its words under it
(wrapped to the box), and its choices, each a button of Quit's, side by side
in its bottom band, the list's focus on the one it names (a hot reload's,
the one focused). FALSE if the screen has no <dialog> with a <list> */
static boolean dialog_build(struct screen_open *open, struct menu_screen const *screen)
{
	struct widget_instance *widget = open->widget;
	struct screen_values values;
	char label[MENU_SCREEN_MAXIMUM_TEXT];
	struct menu_screen_block const *dialog = NULL, *list = NULL;
	long index, choices = 0, list_index = NONE, focus_index = 0, behind_tag;
	short width, height, shown, left, top, x, lines = 1;
	struct widget_instance *focus = NULL;
	char const *focus_id;

	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].kind == MENU_SCREEN_DIALOG)
			dialog = &screen->blocks[index];
		else if (screen->blocks[index].kind == MENU_SCREEN_LIST)
		{
			list = &screen->blocks[index];
			list_index = index;
		}
	}
	if (!dialog || !list)
		return FALSE;
	screen_values_clear(&values, open);
	for (index = 0; index < screen->block_count; index++)
	{
		if (screen->blocks[index].kind == MENU_SCREEN_ITEM && screen->blocks[index].parent == list_index &&
			screen_item_shown(&screen->blocks[index], &values, label, sizeof(label)))
		{
			choices++;
		}
	}
	choices = PIN(choices, 1, MENU_SCREEN_MAXIMUM_CHOICES);
	width = (short)MAX(DIALOG_LEAST_WIDTH, choices * DIALOG_CHOICE_WIDTH + DIALOG_CHOICES_ROOM + DIALOG_PICTURE_MARGIN);
	shown = (short)(width - DIALOG_PICTURE_MARGIN);
	left = (short)((SCREEN_WIDTH - width) / 2);
	/* (its first opening on the list's focus, as each question asks anew; made
	again by a hot reload, on the choice focused) */
	focus_id = open->generation && screens.focused[open->place - screen_places][0] ?
		screens.focused[open->place - screen_places] : list->focus;
	/* the screen's own widgets let go of, and its own made */
	widget->focused_child = NULL;
	while (widget->child)
		ui_widget_delete(widget->child);
	open->list = NULL;
	open->item_count = 0;
	open->colors = FALSE;
	open->spartan_count = 0;
	open->header = NULL;
	open->prompt_count = 0;
	open->box = FALSE;
	/* (the screen it was opened from, behind it and dimmed) */
	behind_tag = ui_widget_port_previous_screen(widget->local_player_index);
	if (behind_tag != NONE && !screen_place_of(behind_tag))
	{
		struct widget_instance *behind = screen_widget_add(widget, behind_tag, 0, 0);

		if (behind)
		{
			dialog_still(behind);
			screen_widget_add(widget, screens.dialog_dim_tag, 0, 0);
		}
	}
	for (index = 0; index < NUMBEROF(open->dialog_under); index++)
	{
		open->dialog_under[index] = screen_widget_add(widget, index % 2 ? screens.dialog_foot_tag : screens.dialog_tag,
			left, 0);
		if (!open->dialog_under[index])
			return FALSE;
	}
	open->dialog_box = screen_widget_add(widget, screens.dialog_tag, left, 0);
	open->dialog_foot = screen_widget_add(widget, screens.dialog_foot_tag, left, 0);
	open->dialog_title = screen_widget_add(widget, screens.dialog_title_tag, (short)(left + DIALOG_TEXT_X), 0);
	open->dialog_text = screen_widget_add(widget, screens.dialog_text_tag, (short)(left + DIALOG_TEXT_X), 0);
	if (!open->dialog_box || !open->dialog_foot || !open->dialog_title || !open->dialog_text)
		return FALSE;
	if (dialog->label && menu_screen_text(dialog->label, screen_value, &values, label, sizeof(label)))
		screen_text_set(open->dialog_title, label);
	open->dialog_text->visible = FALSE;
	if (dialog->text && menu_screen_text(dialog->text, screen_value, &values, label, sizeof(label)) && label[0])
	{
		screen_text_set(open->dialog_text, label);
		open->dialog_text->visible = TRUE;
		lines = (short)MAX(1, pc_menu_text_lines(open->dialog_text,
			(unsigned short const *)open->dialog_text->parameters.text_box.text,
			(short)(shown - 2 * DIALOG_TEXT_X), DIALOG_MOST_LINES));
	}
	/* (as tall as its words, in the middle of the screen) */
	height = (short)(DIALOG_HEAD + DIALOG_TEXT_ROOM * 2 + lines * DIALOG_LINE + DIALOG_FOOT_BAND);
	top = (short)((SCREEN_HEIGHT - height) / 2);
	open->dialog_width = width;
	open->dialog_height = height;
	open->dialog_box->vertical_offset = (short)(widget->vertical_offset + top);
	open->dialog_foot->vertical_offset = (short)(widget->vertical_offset + top + height - DIALOG_FOOT);
	for (index = 0; index < NUMBEROF(open->dialog_under); index++)
	{
		open->dialog_under[index]->vertical_offset = index % 2 ? open->dialog_foot->vertical_offset :
			open->dialog_box->vertical_offset;
	}
	open->dialog_title->vertical_offset = (short)(widget->vertical_offset + top + DIALOG_TITLE_Y);
	open->dialog_text->vertical_offset = (short)(widget->vertical_offset + top + DIALOG_HEAD + DIALOG_TEXT_ROOM);
	/* (its choices, side by side in the middle of its bottom band) */
	open->list = screen_widget_add(widget, screens.dialog_list_tag, 0, 0);
	if (!open->list)
		return FALSE;
	x = (short)(left + (shown - choices * DIALOG_CHOICE_WIDTH) / 2);
	for (index = 0; index < screen->block_count; index++)
	{
		struct menu_screen_block const *item = &screen->blocks[index];
		struct widget_instance *made;

		if (item->kind != MENU_SCREEN_ITEM || item->parent != list_index ||
			!screen_item_shown(item, &values, label, sizeof(label)) || open->item_count >= choices)
		{
			continue;
		}
		made = screen_widget_add(open->list, screens.dialog_choice_tag,
			(short)(x + open->item_count * DIALOG_CHOICE_WIDTH), (short)(top + height - DIALOG_CHOICES_UP));
		if (!made)
			break;
		screen_text_set(made, label);
		open->items[open->item_count] = made;
		open->item_blocks[open->item_count] = item;
		open->hints[open->item_count] = NULL;
		open->pictures[open->item_count] = NONE;
		snprintf(open->ids[open->item_count], SCREEN_ID_LENGTH, "%s", item->id ? item->id : "");
		snprintf(open->actions[open->item_count], SCREEN_ACTION_LENGTH, "%s", item->action);
		if (!focus || (focus_id && item->id && !strcmp(item->id, focus_id)))
		{
			focus = made;
			focus_index = open->item_count;
		}
		open->item_count++;
	}
	if (focus)
	{
		widget->focused_child = open->list;
		open->list->focused_child = focus;
		open->list->parameters.list.selected_index = (short)focus_index;
	}
	snprintf(open->back, sizeof(open->back), "%s", screen->back ? screen->back : "");
	open->generation = menu_screens_generation(screen->name);
	return TRUE;
}

/* a dialog's box, its foot, title and words drawn as wide as its choices
make the box (its pictures' ends kept: keep_ends), the box cut above its
foot and its words as tall as their lines: TRUE for them */
static boolean dialog_picture_of(struct widget_instance const *widget, long *bitmap_tag, rectangle2d *bounds)
{
	long open_index;

	for (open_index = 0; widget && open_index < SCREEN_MAXIMUM_OPEN; open_index++)
	{
		struct screen_open const *open = &screens.open[open_index];
		short shown = (short)(open->dialog_width - DIALOG_PICTURE_MARGIN);

		if (!open->widget || !open->dialog_box)
			continue;
		if (widget == open->dialog_box || widget == open->dialog_under[0] || widget == open->dialog_under[2])
		{
			bounds->x1 = (short)(bounds->x0 + open->dialog_width);
			bounds->y1 = (short)(bounds->y0 + open->dialog_height - DIALOG_FOOT);
		}
		else if (widget == open->dialog_foot || widget == open->dialog_under[1] || widget == open->dialog_under[3])
			bounds->x1 = (short)(bounds->x0 + open->dialog_width);
		else if (widget == open->dialog_title)
			bounds->x1 = (short)(bounds->x0 + shown - 2 * DIALOG_TEXT_X);
		else if (widget == open->dialog_text)
		{
			bounds->x1 = (short)(bounds->x0 + shown - 2 * DIALOG_TEXT_X);
			bounds->y1 = (short)(bounds->y0 + open->dialog_height - DIALOG_HEAD - DIALOG_FOOT_BAND -
				DIALOG_TEXT_ROOM);
		}
		else
			continue;
		*bitmap_tag = NONE;
		return TRUE;
	}
	return FALSE;
}

/* "edit save" and "edit discard" (screens/save_changes.xml's SAVE and DON'T
SAVE): back to the editor the question was opened from (the screen before),
then its changes saved as its OK saves them, or let go of, and the editor
closed, as its OK closes it (menu_functions.c) */
static boolean screen_call_edit(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	boolean save = !strcmp(name, "edit save");
	short local_player_index = widget_instance_get_topmost_parent(widget)->local_player_index;

	menu_tags_back(widget, event, deleted);
	*deleted = TRUE;
	return pc_menu_edit_leave(local_player_index, save);
}

/* "close", "close all" and "pause leave": the screen closed (the pause
menu's RESUME GAME), every screen (B; the campaign's RESUME GAME), or the
player taken out of the game (the Xbox's "mp game player quit") and the
screen closed; "back": back to the screen before, as B goes (a pop-up's
BACK and CANCEL) */
static boolean screen_call_close(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	struct widget_instance *screen = widget_instance_get_topmost_parent(widget);

	*deleted = FALSE;
	if (!strcmp(name, "back"))
	{
		menu_tags_back(widget, event, deleted);
		return TRUE;
	}
	if (!strcmp(name, "pause leave"))
	{
		long index = pc_menu_event_function_index("mp game player quit");

		if (index == NONE || !ui_widget_event_handler_function_invoke(widget, event, (word)index, deleted))
			return FALSE;
		if (*deleted)
			return TRUE;
	}
	if (!strcmp(name, "close all"))
		ui_widgets_close_all();
	else
		ui_widget_delete(screen);
	*deleted = TRUE;
	return TRUE;
}

/* the screen the pause menu opens as, for the game's (name: the map's
pause screen): under the menu engine, the hand-written one in its place, a
campaign's or a multiplayer game's, pausing the game as the map's does or
not (screens/blocks.xml's stand-ins), its focus on its first row; else the
game's own (the classic engine; a file or its blocks missing or refused) */
char const *menu_screen_pause_name(char const *name)
{
	long tag_index = name ? tag_loaded(UI_WIDGET_DEFINITION_TAG, name) : NONE;
	boolean campaign = name && strstr(name, "\\solo_game\\") != NULL;
	char const *stand_in;
	long index;

	if (tag_index == NONE || !menu_engine_looks())
		return name;
	if (campaign)
		stand_in = menu_tags_flags(tag_index) & SCREEN_PAUSE_GAME_FLAG ? "pc\\screens\\pause_campaign" :
			"pc\\screens\\pause_campaign_live";
	else
		stand_in = menu_tags_flags(tag_index) & SCREEN_PAUSE_GAME_FLAG ? "pc\\screens\\pause" : "pc\\screens\\pause_live";
	screens_names_give();
	menu_screens_poll(ui_widget_port_milliseconds());
	if (tag_loaded(UI_WIDGET_DEFINITION_TAG, stand_in) == NONE ||
		!menu_screens_find(campaign ? "pause_campaign" : "pause") || !screen_blocks_found())
	{
		return name;
	}
	for (index = 0; index < NUMBEROF(screen_places); index++)
	{
		if (!strncmp(screen_places[index].screen, "pause", 5))
			screens.focused[index][0] = 0;
	}
	return stand_in;
}

/* ---------- the engine's calls */

boolean menu_screen_opened(struct widget_instance *widget)
{
	struct screen_place const *place = screen_place_of(widget->definition_tag_index);
	struct menu_screen const *screen;
	long index;

	if (!place)
		return FALSE;
	screens_names_give();
	menu_screens_poll(ui_widget_port_milliseconds());
	screen = menu_screens_find(place->screen);
	if (!screen)
		return FALSE;
	/* (co-op's screen: HOST's choosing over, as Choose Mission's B comes
	back to it; WHO CAN JOIN on the choice kept: coop_menu.inc) */
	if (!strcmp(place->screen, "coop"))
		pc_menu_coop_opened();
	if (!strcmp(place->screen, "coop_who"))
		snprintf(screens.focused[place - screen_places], SCREEN_ID_LENGTH, "%s", pc_menu_coop_who_word());
	for (index = 0; index < SCREEN_MAXIMUM_OPEN; index++)
	{
		struct screen_open *open = &screens.open[index];

		if (open->widget)
			continue;
		memset(open, 0, sizeof(*open));
		open->widget = widget;
		open->place = place;
		if (!screen_build(open, screen))
		{
			memset(open, 0, sizeof(*open));
			return FALSE;
		}
		/* (its table's rows told it opened: Select Profile's on player 1's
		profile) */
		if (tall_of(open) && tall_of(open)->source && tall_of(open)->source->opened)
			tall_of(open)->source->opened();
		return TRUE;
	}
	return FALSE;
}

void menu_screen_closed(struct widget_instance *widget)
{
	struct screen_open *open = screen_open_of(widget);

	if (!open)
		return;
	if (tall_of(open))
	{
		struct screen_tall *tall = tall_of(open);

		if (tall_search_editing(tall))
			pc_menu_text_field_end(FALSE);
		if (tall_search_screen == tall)
			tall_search_screen = NULL;
		tall_screens[open - screens.open] = NULL;
	}
	memset(open, 0, sizeof(*open));
}

/* the item focused now (its index, NONE for none) */
static long screen_focused(struct screen_open const *open)
{
	long index;

	for (index = 0; open->list && index < open->item_count; index++)
	{
		if (open->list->focused_child == open->items[index])
			return index;
	}
	return NONE;
}

/* a <spartan> of a binding's color whose binding has no value: none, nor its
picture (Select Profile's row that makes a profile) */
#define SCREEN_SPARTAN_NONE (-2)
/* a <spartan> of a profile's color where there is no profile: no Spartan,
the original picture of random */
#define SCREEN_SPARTAN_NO_PROFILE (-3)
/* a place of a lineup's with no one in it ("-") */
#define SCREEN_SPARTAN_EMPTY (-6)

/* a color's word in a <spartan>'s binding's value: a profile color's
number, random, a team's (red, blue), or "-" for an empty place */
static short screen_spartan_word(char const *word, long length)
{
	if (length == 6 && !strncmp(word, "random", 6))
		return MENU_SPARTAN_RANDOM;
	if (length == 3 && !strncmp(word, "red", 3))
		return MENU_SPARTAN_TEAM_RED;
	if (length == 4 && !strncmp(word, "blue", 4))
		return MENU_SPARTAN_TEAM_BLUE;
	if (word[0] >= '0' && word[0] <= '9')
		return (short)atoi(word);
	return SCREEN_SPARTAN_EMPTY;
}

/* the colors a <spartan>'s places show (their count: at most its places):
the focused color's, the profile's (the one being edited, else the
player's own; each player's side by side), or its binding's (a color's
word each, as many places); MENU_SPARTAN_RANDOM for random,
SCREEN_SPARTAN_NO_PROFILE for no profile, SCREEN_SPARTAN_NONE for a
binding's none, SCREEN_SPARTAN_EMPTY for an empty place */
static short screen_spartan_colors(struct screen_open const *open, struct screen_spartan const *spartan,
	long focused, short *colors)
{
	struct menu_screen_block const *block = &spartan->block;
	short player, count = 0;

	if (block->color == MENU_SCREEN_SPARTAN_FOCUS && open->colors && focused != NONE)
	{
		colors[0] = (short)focused;
		return 1;
	}
	if (block->color == MENU_SCREEN_SPARTAN_BINDING)
	{
		struct screen_values values;
		char value[MENU_SCREEN_MAXIMUM_TEXT];
		char const *word = value;

		screen_values_clear(&values, open);
		if (!block->value || !screen_value(&values, block->value, value, sizeof(value)))
		{
			colors[0] = SCREEN_SPARTAN_NONE;
			return 1;
		}
		while (*word && count < spartan->places)
		{
			long length = 0;

			while (*word == ' ')
				word++;
			while (word[length] && word[length] != ' ')
				length++;
			if (!length)
				break;
			colors[count++] = screen_spartan_word(word, length);
			word += length;
		}
		if (!count)
			colors[count++] = SCREEN_SPARTAN_NONE;
		return count;
	}
	for (player = 0; player < spartan->places; player++)
	{
		if (player == 0 && player_ui_get_edit_player_profile())
		{
			short color = player_ui_get_edit_player_profile()->primary_color_index;

			colors[count++] = color < 0 || color >= SCREEN_COLOR_COUNT ? MENU_SPARTAN_RANDOM : color;
		}
		else if (player_ui_get_active_player_profile_index(player) != NONE)
		{
			struct player_profile profile;

			player_ui_get_active_player_profile(player, &profile);
			colors[count++] = profile.primary_color_index < 0 || profile.primary_color_index >= SCREEN_COLOR_COUNT ?
				MENU_SPARTAN_RANDOM : profile.primary_color_index;
		}
		else
			colors[count++] = SCREEN_SPARTAN_NO_PROFILE;
	}
	return count;
}

/* the screen's <spartan>s' Spartans asked for this frame, each one's places
side by side in its box (a random color's cycling through the colors:
menu_spartan_color), and the original picture shown where there is none
(no Spartan's tags: menu_spartan_available; or no profile; a lineup shows
none) */
static void screen_spartan_frame(struct screen_open *open, long focused)
{
	long index;

	for (index = 0; index < open->spartan_count; index++)
	{
		struct screen_spartan *spartan = &open->spartans[index];
		struct menu_screen_block const *block = &spartan->block;
		boolean drawn = FALSE;
		boolean lineup = block->color == MENU_SCREEN_SPARTAN_BINDING && spartan->places > 1;
		short colors[MAXIMUM_MENU_SPARTAN_PLACES];
		short test_colors[MAXIMUM_MENU_SPARTAN_PLACES];
		short tested = index == 0 ? menu_spartan_test_colors(test_colors) : 0;
		short place, places, first;

		if (!spartan->shown)
		{
			if (spartan->picture)
				spartan->picture->visible = FALSE;
			continue;
		}
		places = screen_spartan_colors(open, spartan, focused, colors);
		/* (debug: HALO_MENU_SPARTAN_TEST's colors in place of the first's
		players') */
		if (tested)
		{
			places = (short)MIN(tested, MAX(spartan->places, 1));
			for (place = 0; place < places; place++)
				colors[place] = test_colors[place];
		}
		first = colors[0];
		for (place = 0; place < places; place++)
		{
			short color = colors[place];
			boolean team = color == MENU_SPARTAN_TEAM_RED || color == MENU_SPARTAN_TEAM_BLUE;

			if ((color != MENU_SPARTAN_RANDOM && !team && (color < 0 || color >= SCREEN_COLOR_COUNT)) ||
				!menu_spartan_available())
			{
				continue;
			}
			menu_spartan_place((short)(spartan->first + place), color,
				(real)block->box_x + (real)block->box_width * ((real)place + 0.5f) / (real)places,
				(real)(block->box_y + block->box_height),
				(real)block->box_height / (real)(!lineup && places > 2 ? 2 : 1), lineup);
			drawn |= place == 0;
		}
		if (spartan->picture && !lineup)
		{
			spartan->picture->visible = !drawn && first != SCREEN_SPARTAN_NONE;
			spartan->picture->animation.current_frame_index =
				(short)(first >= 0 && first < SCREEN_COLOR_COUNT ? first : SCREEN_SPARTAN_RANDOM_FRAME);
		}
		else if (spartan->picture)
			spartan->picture->visible = FALSE;
	}
}

void menu_screen_widget_drawing(struct widget_instance const *widget, point2d offset)
{
	long index;

	for (index = 0; widget && index < SCREEN_MAXIMUM_OPEN; index++)
	{
		struct screen_open const *open = &screens.open[index];
		long spartan;

		/* (its <spartan>s' Spartans, each in its view, where its picture is
		in the widgets' order: over what is drawn before, under what after) */
		for (spartan = 0; open->widget && spartan < open->spartan_count; spartan++)
		{
			struct screen_spartan const *each = &open->spartans[spartan];

			if (each->picture == widget && each->shown)
				menu_spartan_draw(offset.x, offset.y, each->first, each->places);
		}
		/* (its chat's on-screen keyboard, over everything) */
		if (open->widget && tall_screens[index] && tall_screens[index]->chat_keyboard == widget)
			tall_chat_keyboard_draw(tall_screens[index], offset);
	}
}

/* a color item's color chosen: the profile's (as the PC version's picker
chose it: player_profile_color_picker_select_color), and back to the
screen before */
static boolean screen_color_choose(struct widget_instance *widget, char const *which)
{
	struct player_profile *profile = player_ui_get_edit_player_profile();
	long color = strtol(which, NULL, 10);

	(void)widget;
	if (!profile || color < 0 || color >= SCREEN_COLOR_COUNT)
	{
		platform_log("menus: a color is chosen with no profile being edited");
		return FALSE;
	}
	profile->primary_color_index = (short)color;
	platform_log("menus: the profile's color %ld chosen", color);
	event_manager_post_button(0, SCREEN_BUTTON_B);
	return TRUE;
}

void menu_screen_frame(struct widget_instance *widget)
{
	struct screen_open *open = screen_open_of(widget);
	long focused, index;

	if (!open)
		return;
	/* (its file read again: made again, the focus on the same item) */
	menu_screens_poll(ui_widget_port_milliseconds());
	if (menu_screens_generation(open->place->screen) != open->generation)
	{
		struct menu_screen const *screen = menu_screens_find(open->place->screen);

		focused = screen_focused(open);
		if (focused != NONE)
			snprintf(screens.focused[open->place - screen_places], SCREEN_ID_LENGTH, "%s", open->ids[focused]);
		if (tall_of(open))
			tall_screens[open - screens.open] = NULL;
		if (screen && screen_build(open, screen))
			platform_log("menus: screen %s made again", screen->name);
		else
			open->generation = menu_screens_generation(open->place->screen);
	}
	/* (a box's: its rows' labels, its legend's room) */
	if (open->box)
		box_frame(open);
	/* (the tall layout's: its blocks each frame) */
	if (tall_of(open))
	{
		tall_frame(open);
		if (open->place->shown)
			open->place->shown(widget);
		return;
	}
	/* (past 16:9, the band's title and the bar's prompts to the screen's
	edges, as the tall layout's titles and footer's prompts) */
	if (open->header && open->header_picture == NONE)
		open->header->horizontal_offset = (short)(widget->horizontal_offset - ui_widget_port_past_move() / 2);
	for (index = 0; index < open->prompt_count; index++)
	{
		open->prompts[index]->horizontal_offset = (short)(open->prompt_x[index] +
			(open->prompt_right[index] ? 1 : -1) * (ui_widget_port_past_move() / 2));
	}
	focused = screen_focused(open);
	for (index = 0; index < open->item_count; index++)
	{
		if (open->hints[index])
			open->hints[index]->visible = index == focused;
		/* (a picture's frames: lit while focused, as the PC version's items) */
		open->items[index]->animation.current_frame_index = (short)(index == focused);
	}
	if (focused != NONE)
		snprintf(screens.focused[open->place - screen_places], SCREEN_ID_LENGTH, "%s", open->ids[focused]);
	if (open->spartan_count)
		screen_spartan_frame(open, focused);
	if (open->place->shown)
		open->place->shown(widget);
}

/* an action run from the widget (an item, or the screen) */
static boolean screen_action_run(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	struct screen_action const *action = NULL;
	boolean succeeded = TRUE;
	char const *open;
	long index;

	*deleted = FALSE;
	/* (a <colors> item's: its color chosen) */
	if (!strncmp(name, "color ", 6))
		return screen_color_choose(widget, name + 6);
	for (index = 0; index < NUMBEROF(screen_actions); index++)
	{
		if (!strcmp(screen_actions[index].name, name))
			action = &screen_actions[index];
	}
	if (!action)
	{
		platform_log("menus: there is no action \"%s\"", name);
		return FALSE;
	}
	if (action->call)
		return action->call(widget, event, name, deleted);
	if (action->browser_tab || action->before)
		pc_menu_browser_visit(action->browser_tab);
	if (action->before)
	{
		index = pc_menu_event_function_index(action->before);
		if (index != NONE)
			ui_widget_event_handler_function_invoke(widget, event, (word)index, deleted);
		if (*deleted)
			return TRUE;
	}
	if (action->run)
	{
		index = pc_menu_event_function_index(action->run);
		/* (a failure that opens its other screen is expected, not warned
		of, as a handler's with a conditional) */
		ui_widget_function_failure_expected = action->otherwise != NULL;
		succeeded = index != NONE && ui_widget_event_handler_function_invoke(widget, event, (word)index, deleted);
		ui_widget_function_failure_expected = FALSE;
		if (*deleted)
			return TRUE;
	}
	if (succeeded && action->then)
	{
		index = pc_menu_event_function_index(action->then);
		if (index != NONE)
			ui_widget_event_handler_function_invoke(widget, event, (word)index, deleted);
		if (*deleted)
			return TRUE;
	}
	open = succeeded ? action->open : action->otherwise;
	if (open && !menu_tags_open(widget, event, open, deleted))
		platform_log("menus: action \"%s\": there is no screen %s", name, open);
	return TRUE;
}

/* the server browser's actions (menu_functions.c's), for screens/browser.xml */
static boolean screen_call_browser(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	*deleted = FALSE;
	if (!strcmp(name, "browser host"))
	{
		return screen_action_run(widget, event, pc_menu_browser_visit_kind() == 2 ? "open host coop" : "open host",
			deleted);
	}
	if (!strncmp(name, "browser tab ", 12))
		return pc_menu_browser_tab(name + 12);
	if (!strcmp(name, "browser sort next"))
		return pc_menu_browser_sort(NULL);
	if (!strncmp(name, "browser sort ", 13))
		return pc_menu_browser_sort(name + 13);
	if (!strcmp(name, "browser favorite"))
		return pc_menu_browser_favorite(screen_action_row);
	if (!strcmp(name, "browser join"))
		return pc_menu_browser_select(widget, event, screen_action_row, deleted);
	return FALSE;
}

/* Select Profile's "profile select" (menu_functions.c's model): the row's
profile (the chosen one's, for OK) made player 1's and back to the screen
before, as the generated list's "profile manager select" with its back; on
the row that makes one, the generated list's question (pm_empty_selected) */
#define SCREEN_PROFILE_EMPTY "main_menu/profile_manager/pm_empty_selected"

static boolean screen_call_profile(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	*deleted = FALSE;
	if (strcmp(name, "profile select"))
		return FALSE;
	if (pc_menu_profile_choose(widget, event, screen_action_row))
	{
		menu_tags_back(widget, event, deleted);
		return TRUE;
	}
	if (!menu_tags_open(widget, event, SCREEN_PROFILE_EMPTY, deleted))
		platform_log("menus: action \"%s\": there is no screen %s", name, SCREEN_PROFILE_EMPTY);
	return TRUE;
}

/* Profile Color's "profile color" (menu_functions.c's model): the row's
color (the chosen one's, for SELECT) made the edited profile's and back to
the screen before, as the PC version's picker's A */
static boolean screen_call_color(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	*deleted = FALSE;
	if (strcmp(name, "profile color"))
		return FALSE;
	if (!pc_menu_color_choose(screen_action_row))
	{
		platform_log("menus: a color is chosen with no profile being edited");
		return FALSE;
	}
	{
		char word[16];

		if (!pc_menu_color_value("color.spartan", word, sizeof(word)))
			word[0] = 0;
		platform_log("menus: the profile's color %s chosen", word);
	}
	menu_tags_back(widget, event, deleted);
	return TRUE;
}

/* the lobby's actions of C's own (menu_functions.c's lobby_menu.inc), the
row given */
static boolean screen_call_lobby(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	int lobby_deleted = FALSE;
	boolean result = pc_menu_lobby_action(widget, event, name, screen_action_row, &lobby_deleted) != 0;

	*deleted = lobby_deleted != 0;
	return result;
}

/* the lobby's start (menu_functions.c's: a controller not in the game
joins, on Add Player's screen), and Add Player's DONE and BACK (those
joining brought in, or let go of; then back to the screen before) */
static boolean screen_call_add_player(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	int call_deleted = FALSE;
	boolean result;

	*deleted = FALSE;
	if (!strcmp(name, "lobby start"))
	{
		result = pc_menu_lobby_start(widget, event, &call_deleted) != 0;
		*deleted = call_deleted != 0;
		return result;
	}
	if (!pc_menu_add_player_action(name))
		return TRUE;
	menu_tags_back(widget, event, deleted);
	return TRUE;
}

/* (menu_table.h's) the place of a hand-written screen by its name, where
the new menus run it and its file is read (NULL: none) */
static struct screen_place const *screen_place_named(char const *name)
{
	char tag_name[256];
	long index;

	if (!menu_engine_looks())
		return NULL;
	screens_names_give();
	for (index = 0; index < NUMBEROF(screen_places); index++)
	{
		if (strcmp(screen_places[index].screen, name) || !menu_screens_find(name))
			continue;
		snprintf(tag_name, sizeof(tag_name), "pc\\%s", screen_places[index].widget);
		if (tag_loaded(UI_WIDGET_DEFINITION_TAG, tag_name) != NONE)
			return &screen_places[index];
	}
	return NULL;
}

int menu_screen_open_ready(char const *name)
{
	return screen_place_named(name) != NULL;
}

int menu_screen_open_named(struct widget_instance *widget, struct event_record *event, char const *name,
	int *deleted)
{
	struct screen_place const *place = screen_place_named(name);
	boolean open_deleted = FALSE;
	boolean opened;
	char tag_name[256];

	*deleted = FALSE;
	if (!place)
		return FALSE;
	snprintf(tag_name, sizeof(tag_name), "pc\\%s", place->widget);
	opened = menu_tags_open(widget, event, tag_name, &open_deleted);
	*deleted = open_deleted != 0;
	return opened;
}

/* a controller's key on a screen that takes keys by controller (a press,
not a held one's repeat: menu_table.h's MENU_SCREEN_KEY_*); NONE for none
(an event that is no key: a stick let go of) */
static int screen_key_of(struct event_record const *event)
{
	if (event->type == SCREEN_EVENT_LEFT_STICK)
	{
		return event->data.stick.x == SHORT_MIN ? MENU_SCREEN_KEY_LEFT :
			event->data.stick.x == SHORT_MAX ? MENU_SCREEN_KEY_RIGHT :
			event->data.stick.y == SHORT_MIN || event->data.stick.y == SHORT_MAX ? MENU_SCREEN_KEY_OTHER : NONE;
	}
	if (event->type != SCREEN_EVENT_BUTTON)
		return NONE;
	if (event->data.button.value != 1)
		return MENU_SCREEN_KEY_OTHER;
	switch (event->data.button.index)
	{
	case _gamepad_analog_button_a: return MENU_SCREEN_KEY_A;
	case _gamepad_analog_button_b: return MENU_SCREEN_KEY_B;
	case _gamepad_binary_button_start: return MENU_SCREEN_KEY_START;
	case _gamepad_binary_button_dpad_left: return MENU_SCREEN_KEY_LEFT;
	case _gamepad_binary_button_dpad_right: return MENU_SCREEN_KEY_RIGHT;
	}
	return MENU_SCREEN_KEY_OTHER;
}

/* co-op's actions of C's own (menu_functions.c's coop_menu.inc): WHO CAN
JOIN's cards, the co-op list's fix (HOST CO-OP's: the HOST card's own
action) and JOIN BY LINK, the lobby's fix */
static boolean screen_call_coop(struct widget_instance *widget, struct event_record *event, char const *name,
	boolean *deleted)
{
	int coop_deleted = FALSE;
	boolean result;

	*deleted = FALSE;
	if (!strcmp(name, "coop fix") && pc_menu_coop_fix_hosts())
		return screen_action_run(widget, event, "coop host", deleted);
	result = pc_menu_coop_action(widget, event, name, &coop_deleted) != 0;
	*deleted = coop_deleted != 0;
	return result;
}

boolean menu_screen_step(struct widget_instance *widget, struct event_record *event, boolean *deleted, short step)
{
	struct screen_open *open = screen_open_of(widget_instance_get_topmost_parent(widget));

	*deleted = FALSE;
	return open && tall_of(open) && tall_step(open, widget, event, deleted, step);
}

boolean menu_screen_do(struct widget_instance *item, struct event_record *event, boolean *deleted)
{
	struct screen_open *open = screen_open_of(widget_instance_get_topmost_parent(item));
	long index;

	*deleted = FALSE;
	if (open && tall_of(open))
		return tall_do(open, item, event, deleted);
	for (index = 0; open && index < open->item_count; index++)
	{
		if (open->items[index] == item)
		{
			snprintf(screens.focused[open->place - screen_places], SCREEN_ID_LENGTH, "%s", open->ids[index]);
			return screen_action_run(item, event, open->actions[index], deleted);
		}
	}
	return FALSE;
}

boolean menu_screen_key(struct widget_instance *prompt, struct event_record *event, boolean *deleted, short controller)
{
	struct screen_open *open = screen_open_of(widget_instance_get_topmost_parent(prompt));
	long index;

	*deleted = FALSE;
	if (open && tall_of(open))
		return tall_prompt(open, prompt, event, deleted, controller);
	for (index = 0; open && index < open->prompt_count; index++)
	{
		char const *key = open->prompt_keys[index];

		if (open->prompts[index] != prompt)
			continue;
		/* (accept: the item the list has chosen, as A on it; the others their
		button pressed, which the screen takes as it takes the controller's) */
		if (!strcmp(key, "accept"))
			return open->list && open->list->focused_child && menu_screen_do(open->list->focused_child, event, deleted);
		event_manager_post_button(controller, !strcmp(key, "back") ? SCREEN_BUTTON_B :
			!strcmp(key, "x") ? SCREEN_BUTTON_X : SCREEN_BUTTON_Y);
		return TRUE;
	}
	return FALSE;
}

boolean menu_screen_back(struct widget_instance *widget, struct event_record *event, boolean *deleted)
{
	struct screen_open *open = screen_open_of(widget);

	*deleted = FALSE;
	/* (a screen that takes keys by controller (Add Player's: a controller
	not in the game joining with its own), before its focus and its own
	keys: those it takes, and its done, back to the screen before) */
	if (open && open->place->key && event->controller_index >= 0)
	{
		int key = screen_key_of(event);
		int taken = key == NONE ? MENU_SCREEN_KEY_FREE : open->place->key(event->controller_index, key);

		if (taken == MENU_SCREEN_KEY_DONE)
		{
			menu_tags_back(widget, event, deleted);
			return TRUE;
		}
		if (taken != MENU_SCREEN_KEY_FREE)
			return TRUE;
	}
	/* (Start anywhere on a screen of its own start: that, where it does
	something) */
	if (open && open->start[0] && event->type == SCREEN_EVENT_BUTTON && event->data.button.value == 1 &&
		event->data.button.index == _gamepad_binary_button_start &&
		screen_action_run(widget, event, open->start, deleted))
	{
		return TRUE;
	}
	/* (a tall screen's keys: its buttons' and its tabs') */
	if (open && tall_of(open) && tall_key(open, widget, event, deleted))
		return TRUE;
	if (!open || !open->back[0] || event->type != SCREEN_EVENT_BUTTON || event->data.button.value != 1 ||
		(event->data.button.index != _gamepad_analog_button_b &&
			event->data.button.index != _gamepad_binary_button_back))
	{
		return FALSE;
	}
	/* (a tall screen whose band has a button of Back's: that, not back) */
	if (tall_of(open) && event->data.button.index == _gamepad_binary_button_back && tall_of(open)->header_button.widget)
		return FALSE;
	if (!screen_action_run(widget, event, open->back, deleted))
		return FALSE;
	return TRUE;
}

/* a block's text scale (menu_tags.c's pc_menu_text_scale): an item's label
the larger, from 4:3 to 16:9 */
boolean menu_screen_text_scale(long definition_index, short wide_extra, real *scale)
{
	if (screens.blocks_found && definition_index == screens.header_text_tag)
	{
		*scale = HEADER_TITLE_SCALE;
		return TRUE;
	}
	if (screens.blocks_found && (definition_index == screens.card_title_tag ||
		definition_index == screens.card_subtitle_tag))
	{
		real card_scale = 1.0f, title_scale = CARD_TITLE_SCALE;
		long open_index;

		for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
		{
			if (screens.open[open_index].widget && screens.open[open_index].card_scale > 0.0f)
				card_scale = screens.open[open_index].card_scale;
			/* (a row whose titles are drawn smaller to fit: screen_card_title_fit) */
			if (screens.open[open_index].widget && screens.open[open_index].card_title_scale > 0.0f)
				title_scale = screens.open[open_index].card_title_scale;
		}
		*scale = (definition_index == screens.card_title_tag ? title_scale : 1.0f) * card_scale;
		return TRUE;
	}
	if (!screens.blocks_found || definition_index != screens.item_tag)
		return FALSE;
	*scale = ITEM_SCALE_LEAST + (ITEM_SCALE_MOST - ITEM_SCALE_LEAST) * (real)MIN(wide_extra, WIDE_LAYOUT_EXTRA) /
		(real)WIDE_LAYOUT_EXTRA;
	return TRUE;
}

/* a block's text colour (menu_functions.c's pc_menu_text_color): a focused
item's label white, as the main menu's lit pictures were */
boolean menu_screen_text_color(struct widget_instance const *widget, real_argb_color *color)
{
	/* (a color's name in its color, white while focused; black's grey, to be
	read) */
	if (screens.blocks_found && widget->definition_tag_index == screens.color_tag && widget->parent)
	{
		long open_index, index;

		if (widget->parent->focused_child == widget)
		{
			color->red = color->green = color->blue = 1.0f;
			return TRUE;
		}
		for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
		{
			struct screen_open const *open = &screens.open[open_index];

			for (index = 0; open->widget && open->colors && index < open->item_count; index++)
			{
				if (open->items[index] == widget)
				{
					real_rgb_color rgb = player_profile_get_rgb_color(index);

					color->red = MAX(rgb.red, 0.3f);
					color->green = MAX(rgb.green, 0.3f);
					color->blue = MAX(rgb.blue, 0.3f);
					return TRUE;
				}
			}
		}
		return FALSE;
	}
	if (screens.blocks_found && tall_color_of(widget, color))
		return TRUE;
	if (!screens.blocks_found || widget->definition_tag_index != screens.item_tag || !widget->parent ||
		widget->parent->focused_child != widget)
	{
		return FALSE;
	}
	color->red = color->green = color->blue = 1.0f;
	return TRUE;
}

boolean menu_screen_picture_stretched(struct widget_instance const *widget)
{
	return widget && screens.blocks_found && tall_stretched(widget);
}

boolean menu_screen_item_picture(struct widget_instance const *widget, long *bitmap_tag, rectangle2d *bounds)
{
	long open_index, index;

	/* (a tall screen's blocks: their places' widths and pictures) */
	if (screens.blocks_found && tall_picture_of(widget, bitmap_tag, bounds))
		return TRUE;
	/* (a box's middle: as wide as its legend needs) */
	if (screens.blocks_found && box_picture_of(widget, bitmap_tag, bounds))
		return TRUE;
	/* (a dialog's box, title and words: as wide as its choices make it) */
	if (screens.blocks_found && dialog_picture_of(widget, bitmap_tag, bounds))
		return TRUE;
	/* (a header: the picture its file names) */
	if (screens.blocks_found && widget->definition_tag_index == screens.header_tag)
	{
		for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
		{
			struct screen_open const *open = &screens.open[open_index];

			if (open->widget && open->header == widget && open->header_picture != NONE)
			{
				*bitmap_tag = open->header_picture;
				return TRUE;
			}
		}
		return FALSE;
	}
	/* (a card and its parts: scaled to the cards' room, from the card's
	corner; its picture part, its card's picture where its file names one) */
	if (screens.blocks_found && widget->parent && (widget->definition_tag_index == screens.card_tag ||
		widget->definition_tag_index == screens.card_art_tag || widget->definition_tag_index == screens.card_title_tag ||
		widget->definition_tag_index == screens.card_subtitle_tag))
	{
		struct widget_instance const *card = widget->definition_tag_index == screens.card_tag ? widget : widget->parent;

		for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
		{
			struct screen_open const *open = &screens.open[open_index];

			for (index = 0; open->widget && index < open->item_count; index++)
			{
				real scale = open->card_scale;

				if (open->items[index] != card)
					continue;
				*bitmap_tag = widget->definition_tag_index == screens.card_art_tag ? open->card_pictures[index] : NONE;
				if (scale > 0.0f)
				{
					bounds->x0 = (short)(bounds->x0 * scale);
					bounds->y0 = (short)(bounds->y0 * scale);
					bounds->x1 = (short)(bounds->x1 * scale + 0.5f);
					bounds->y1 = (short)(bounds->y1 * scale + 0.5f);
				}
				return scale > 0.0f || *bitmap_tag != NONE;
			}
		}
		return FALSE;
	}
	if (!screens.blocks_found || widget->definition_tag_index != screens.item_tag)
		return FALSE;
	for (open_index = 0; open_index < SCREEN_MAXIMUM_OPEN; open_index++)
	{
		struct screen_open const *open = &screens.open[open_index];

		for (index = 0; open->widget && index < open->item_count; index++)
		{
			if (open->items[index] == widget && open->pictures[index] != NONE)
			{
				*bitmap_tag = open->pictures[index];
				bounds->x0 = 0;
				bounds->y0 = 0;
				bounds->x1 = ITEM_WIDTH;
				bounds->y1 = PICTURE_HEIGHT;
				return TRUE;
			}
		}
	}
	return FALSE;
}
