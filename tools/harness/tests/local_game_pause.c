/*
LOCAL_GAME_PAUSE.C (test)

A campaign started from CAMPAIGN after its CO-OP's server browser was opened
and backed out of: the browser's client (ui_widget_port_browse) was left, so
the game took itself for a network game (network_game_is_active) and Start
asked for a multiplayer game's pause screen the campaign's map has not, for
any controller. main.c's main_local_game_leave_network lets the menus'
client and server go as a local game's map loads. test_local_game_pause.py
takes the code (under_test.inc): ui_widget.c's ui_check_for_pause_game,
network_game_globals.c's client and server and main.c's helper.
*/

#include "harness.h"

#include "config.inc"

#define _error_silent 0
#define FLOOR(value, floor) ((value) < (floor) ? (floor) : (value))
#define TICKS_PER_SECOND 30
#define UI_WIDGET_DEFINITION_TAG 0x44654c61

struct network_game { int unused; };
struct network_game_client { struct network_game game; };
struct network_game_server { int unused; };
struct widget_instance { int unused; };
struct gamepad_state { unsigned char buttons[NUMBER_OF_GAMEPAD_BUTTONS]; };

static struct
{
	struct network_game_server *server;
	struct network_game_client *client;
	boolean quickstart_local, client_started;
} bss_004566dc;

static struct
{
	struct widget_instance *active_widgets[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	long pause_disabled_ticks;
} widget_globals;

static boolean we_are_at_the_main_menu;
static short connection = _game_connection_local;
static boolean local_players[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static struct gamepad_state pads[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static boolean coop, pictures_preparing;
static struct network_game_client browser_client;
static struct network_game_server lobby_server;
static struct widget_instance opened;
static char const *loaded_name;
static short loaded_controller = NONE;
static int errors, clients_disposed, servers_disposed;

/* the map's screens: a campaign's, which has no multiplayer pause screen */
static long tag_loaded(long group, char const *name)
{
	return group == UI_WIDGET_DEFINITION_TAG && !strcmp(name, "ui\\shell\\solo_game\\pause_game\\pause_game") ? 7 : NONE;
}

static boolean ui_widget_load_by_name_or_tag(char const *name, long tag, void *parent, short controller,
	long a, long b, long c)
{
	if (tag_loaded(UI_WIDGET_DEFINITION_TAG, name) == NONE)
		return FALSE;
	loaded_name = name;
	loaded_controller = controller;
	widget_globals.active_widgets[controller] = &opened;
	return TRUE;
}

#define game_in_progress() TRUE
#define cinematic_in_progress() FALSE
#define game_connection() connection
#define game_time_get_paused() FALSE
#define game_engine_allow_pause() TRUE
#define input_has_gamepad(index) TRUE
#define local_player_exists(index) local_players[index]
#define input_get_gamepad_state(index) (&pads[index])
#define network_coop_active() coop
#define menu_screen_pause_name(name) (name)
#define main_get_seconds_elapsed() (1.f / 60.f)
#define network_game_client_get_game(client) (&(client)->game)
#define network_game_client_get_machine_index(client) 0
#define network_game_client_dispose(client) (clients_disposed++)
#define network_game_server_dispose(server) (servers_disposed++)
#define ui_widget_delete(widget) ((void)0)
#define ui_widgets_close_all() ((void)0)
#define ui_widgets_active() FALSE
/* not an MCC map, whose own pause menus the port opens (mcc_pause_runtime.c) */
#define mcc_pause_runtime_active() FALSE
#define mcc_pause_runtime_screen(count, first) NONE
#define ui_widgets_close_all_for_local_player(controller) ((void)0)

static void error(int level, char const *text, ...) { errors++; }

/* (a function, not a macro over the flag: the "pictures" negative control
takes main.c's only call away, and the flag must still be read) */
static boolean menu_pictures_preparing(void)
{
	return pictures_preparing;
}

static short local_player_get_next(short index)
{
	for (index++; index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; index++)
	{
		if (local_players[index])
			return index;
	}
	return NONE;
}

#include "under_test.inc"

/* the menus: CAMPAIGN's CO-OP opened (the server browser's client, as
ui_widget_port_browse makes it, searching) and backed out of; or a lobby
hosted and left */
static void browser_opened_and_left(void)
{
	bss_004566dc.client = &browser_client;
	connection = _game_connection_network_client;
}

/* a mission started from CAMPAIGN (menu_functions.c's campaign_start: the
connection local), its map loaded (main_change_map_name) with the player
at a controller */
static void campaign_started(short controller)
{
	connection = _game_connection_local;
	main_local_game_leave_network();
	memset(local_players, 0, sizeof(local_players));
	local_players[controller] = TRUE;
}

/* Start pressed on a controller: the screen it opened, if any */
static char const *start_pressed(short controller)
{
	loaded_name = NULL;
	loaded_controller = NONE;
	memset(widget_globals.active_widgets, 0, sizeof(widget_globals.active_widgets));
	pads[controller].buttons[_gamepad_binary_button_start] = 1;
	ui_check_for_pause_game();
	pads[controller].buttons[_gamepad_binary_button_start] = 0;
	return loaded_name;
}

#define SOLO_PAUSE "ui\\shell\\solo_game\\pause_game\\pause_game"

int main(
	int argc,
	char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	/* the report's: the browser left, then a campaign, its controller the
	fourth (and the first's) */
	CASE("after-browser")
	{
		short controller;

		for (controller = 0; controller < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; controller++)
		{
			browser_opened_and_left();
			campaign_started(controller);
			CHECK(start_pressed(controller) && !strcmp(loaded_name, SOLO_PAUSE),
				"Start pauses the campaign (controller %d): %s", controller, loaded_name ? loaded_name : "nothing");
			CHECK(!network_game_is_active(), "a campaign alone is no network game (controller %d)", controller);
			CHECK(loaded_controller == controller, "the pause screen is the presser's: %d", loaded_controller);
		}
		CHECK(!errors, "no screen failed to load: %d", errors);
		return 0;
	}
	/* a lobby hosted and left the same way: its server and client let go */
	CASE("after-lobby")
	{
		bss_004566dc.server = &lobby_server;
		browser_opened_and_left();
		campaign_started(3);
		CHECK(!bss_004566dc.server && !bss_004566dc.client && servers_disposed == 1 && clients_disposed == 1,
			"the lobby's server and client let go of");
		CHECK(start_pressed(3) && !strcmp(loaded_name, SOLO_PAUSE), "Start pauses the campaign");
		return 0;
	}
	/* no menus' network: nothing let go of, Start pauses as before */
	CASE("fresh")
	{
		campaign_started(3);
		CHECK(!clients_disposed && !servers_disposed, "nothing to let go of");
		CHECK(start_pressed(3) && !strcmp(loaded_name, SOLO_PAUSE), "Start pauses the campaign");
		return 0;
	}
	/* the menus' pictures' maps load behind the menus: the browser's
	client kept */
	CASE("pictures")
	{
		browser_opened_and_left();
		pictures_preparing = TRUE;
		main_local_game_leave_network();
		CHECK(bss_004566dc.client == &browser_client && !clients_disposed, "the browser's client kept");
		return 0;
	}
	/* a network co-op game (never a local one's load): its own pause */
	CASE("network-coop")
	{
		browser_opened_and_left();
		coop = TRUE;
		local_players[3] = TRUE;
		CHECK(network_game_is_active(), "a network game");
		CHECK(start_pressed(3) && !strcmp(loaded_name, SOLO_PAUSE), "co-op's pause screen, the campaign's");
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
