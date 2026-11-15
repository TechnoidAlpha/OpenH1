#include "harness.h"

/* The lobby's Start and Add Player (port/linux/game/menu_functions.c's
pc_menu_lobby_start, lobby_join_start and add_player_*, lobby_menu.inc's
lobby_board_start; issues 1066 and 1067): the Start of a controller not yet
in the game joins it (Add Player's screen opened with it joining), never
starts the host's game, which a player's Start still does; on Add Player
each controller not in the game joins with A or START, steps its own
profile, is ready with A and backs off with B, its keys never the screen's;
once every one joining is ready, A or START brings them all into the game
on their profiles, and not before. A fake world: four controllers, which
of them play, the host's network game in its pregame, three profiles. */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define NUMBEROF(a) ((long)(sizeof(a) / sizeof((a)[0])))

#define MAXIMUM_LOCAL_PLAYERS 4
#define SOUND_ERROR 4
#define SOUND_CURSOR 1
#define SOUND_FORWARD 2
#define SOUND_BACK 3
#define BUTTON_START 9

struct widget_instance { int unused; };
struct event_record { short controller_index; };
struct network_game { short maximum_players; };
struct advertised_game { boolean open; short maximum_player_count, player_count; };
enum { _client_state_pregame = 2, _client_state_in_game = 3 };

#include "config.inc"

/* ---- the fake world */

static struct
{
	boolean host;
	boolean in_lobby;
	boolean playing[MAXIMUM_LOCAL_PLAYERS];
	long own_profile[MAXIMUM_LOCAL_PLAYERS];
	short client_state;
	struct network_game game;
	struct advertised_game preview;
	boolean previewing;
	/* what happened */
	short opened;
	short started;
	long joined[MAXIMUM_LOCAL_PLAYERS];
	short dispatched;
} world;

static int client_handle;
static short lobby_player_count;

static struct
{
	long indices[8];
	short count;
} profile_list;

static void profile_list_read(boolean always)
{
	(void)always;
}

static void *global_network_game_client_get(void)
{
	return world.in_lobby || world.previewing ? &client_handle : NULL;
}

static void *global_network_game_server_get(void)
{
	return world.host ? &client_handle : NULL;
}

static struct network_game *network_game_client_get_game(void *client)
{
	(void)client;
	return world.in_lobby ? &world.game : NULL;
}

static short network_game_client_get_state(void *client, short *data)
{
	(void)client;
	*data = 0;
	return world.client_state;
}

static boolean network_game_client_request_start_time_change(void *client, boolean start)
{
	(void)client;
	(void)start;
	world.started++;
	return TRUE;
}

static struct advertised_game *preview_game(void)
{
	return world.previewing ? &world.preview : NULL;
}

static boolean lobby_controller_playing(short controller)
{
	return controller >= 0 && controller < MAXIMUM_LOCAL_PLAYERS && world.playing[controller];
}

static short lobby_local_player_count(void)
{
	short controller, count = 0;

	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
		count += world.playing[controller];
	return count;
}

static long player_ui_get_active_player_profile_index(short controller)
{
	return world.own_profile[controller];
}

static boolean ui_widget_port_multiplayer_player(short controller, long profile_index)
{
	world.joined[controller] = profile_index;
	return TRUE;
}

static boolean campaign_fail(void)
{
	return FALSE;
}

#define ui_play_audio_feedback_sound(sound) ((void)(sound))
#define display_error_text_deferred(text, player) ((void)(player))

static struct widget_instance *focused_leaf(struct widget_instance *widget)
{
	return widget;
}

static boolean ui_widget_port_dispatch_event(struct widget_instance *widget, short event, short controller,
	boolean *deleted)
{
	(void)widget;
	(void)event;
	(void)controller;
	*deleted = FALSE;
	world.dispatched++;
	return TRUE;
}

static short event_controller(struct widget_instance const *widget, struct event_record const *event)
{
	(void)widget;
	return event->controller_index;
}

static int menu_screen_open_ready(char const *name)
{
	return !strcmp(name, "add_player");
}

static boolean add_player_open(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted);
static boolean lobby_board_start(short controller);

#include "under_test.inc"

/* (the menus' screen: opening it begins it, the joining controller's join
made there, as menu_functions.c's add_player_open) */
static boolean add_player_open(struct widget_instance *widget, struct event_record *event, short controller,
	boolean *widget_deleted)
{
	(void)widget;
	(void)event;
	*widget_deleted = TRUE;
	world.opened = (short)(controller == NONE ? 99 : controller);
	add_player_begin(controller);
	return TRUE;
}

/* the host's lobby: player 1 (controller 0, profile 100) playing, two more
profiles, room for 16 */
static void lobby(void)
{
	short controller;

	memset(&world, 0, sizeof(world));
	memset(&add_player, 0, sizeof(add_player));
	world.host = TRUE;
	world.in_lobby = TRUE;
	world.client_state = _client_state_pregame;
	world.game.maximum_players = 16;
	world.playing[0] = TRUE;
	world.opened = NONE;
	for (controller = 0; controller < MAXIMUM_LOCAL_PLAYERS; controller++)
	{
		world.own_profile[controller] = NONE;
		world.joined[controller] = NONE;
	}
	world.own_profile[0] = 100;
	lobby_player_count = 1;
	profile_list.count = 3;
	profile_list.indices[0] = 100;
	profile_list.indices[1] = 200;
	profile_list.indices[2] = 300;
	lobby_join.adding = FALSE;
	lobby_join.controller = NONE;
}

static int lobby_start(short controller)
{
	struct widget_instance widget = { 0 };
	struct event_record event;
	int deleted = FALSE;

	event.controller_index = controller;
	return pc_menu_lobby_start(&widget, &event, &deleted);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	lobby();
	if (!strcmp(name, "extra-start-joins"))
	{
		/* (issue 1066: controller 2's Start in the host's lobby joins it, on
		Add Player, choosing; the game is not started) */
		lobby_start(1);
		CHECK(world.started == 0, "the Start of a controller not in the game started the game");
		CHECK(world.opened == 1, "Add Player not opened for controller 2 (%d)", world.opened);
		CHECK(add_player.state[1] == ADD_PLAYER_CHOOSING && add_player.profile[1] == 200,
			"controller 2 joining: state %d, profile %ld", add_player.state[1], add_player.profile[1]);
		CHECK(lobby_join.adding, "one gamepad not its own controller while Add Player is open");
	}
	else if (!strcmp(name, "host-start-starts"))
	{
		/* (the host's own Start still starts the game) */
		CHECK(lobby_start(0) && world.started == 1 && world.opened == NONE, "the host's Start: started %d, opened %d",
			world.started, world.opened);
	}
	else if (!strcmp(name, "player-start"))
	{
		/* (a player not the host: nothing of the lobby's own, its Start the
		screen's widgets'; a joiner of theirs still joins) */
		world.host = FALSE;
		CHECK(!lobby_start(0) && world.started == 0 && world.opened == NONE, "a player's Start did something");
		lobby_start(2);
		CHECK(world.opened == 2 && world.started == 0, "a player's lobby: controller 3's Start did not join");
	}
	else if (!strcmp(name, "board-start-refuses-joiner"))
	{
		/* (START GAME's own start, as A on it or a click: a controller not
		in the game never starts it) */
		CHECK(!lobby_board_start(1) && world.started == 0, "START GAME started by a controller not in the game");
		CHECK(lobby_board_start(0) && world.started == 1, "START GAME by the host's player");
	}
	else if (!strcmp(name, "full"))
	{
		/* (no room in the game: no one joins) */
		lobby_player_count = 16;
		lobby_start(1);
		CHECK(world.started == 0 && add_player.state[1] == ADD_PLAYER_EMPTY, "a full game took a player");
		add_player_open(NULL, NULL, NONE, &(boolean){ FALSE });
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_A);
		CHECK(add_player.state[1] == ADD_PLAYER_EMPTY, "a full game took a player on Add Player");
	}
	else if (!strcmp(name, "choose-ready-done"))
	{
		/* (issue 1067: ADD PLAYER, then controller 2's A joins, right steps
		its profile, A is ready, A again brings it in on that profile) */
		add_player_open(NULL, NULL, NONE, &(boolean){ FALSE });
		CHECK(pc_menu_add_player_key(1, MENU_SCREEN_KEY_A) == MENU_SCREEN_KEY_TAKEN &&
			add_player.state[1] == ADD_PLAYER_CHOOSING, "controller 2's A did not join");
		CHECK(add_player.profile[1] == 200, "its first profile %ld, not the first no one has", add_player.profile[1]);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_RIGHT);
		CHECK(add_player.profile[1] == 300, "right: profile %ld", add_player.profile[1]);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_RIGHT);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_LEFT);
		CHECK(add_player.profile[1] == 300, "right then left: profile %ld", add_player.profile[1]);
		CHECK(pc_menu_add_player_key(1, MENU_SCREEN_KEY_A) == MENU_SCREEN_KEY_TAKEN &&
			add_player.state[1] == ADD_PLAYER_READY && world.joined[1] == NONE, "A: not ready, or joined at once");
		CHECK(pc_menu_add_player_key(1, MENU_SCREEN_KEY_A) == MENU_SCREEN_KEY_DONE, "A when all are ready: not done");
		CHECK(world.joined[1] == 300, "controller 2 joined on profile %ld", world.joined[1]);
		CHECK(add_player.state[1] == ADD_PLAYER_EMPTY, "Add Player not let go of once done");
	}
	else if (!strcmp(name, "players-keys"))
	{
		/* (a player in the game's keys are the screen's: its focus, its B) */
		add_player_open(NULL, NULL, NONE, &(boolean){ FALSE });
		CHECK(pc_menu_add_player_key(0, MENU_SCREEN_KEY_A) == MENU_SCREEN_KEY_FREE &&
			pc_menu_add_player_key(0, MENU_SCREEN_KEY_B) == MENU_SCREEN_KEY_FREE &&
			pc_menu_add_player_key(0, MENU_SCREEN_KEY_OTHER) == MENU_SCREEN_KEY_FREE, "a player's key taken");
		/* (a controller not in the game: every key its own, none the screen's) */
		CHECK(pc_menu_add_player_key(2, MENU_SCREEN_KEY_OTHER) == MENU_SCREEN_KEY_TAKEN &&
			pc_menu_add_player_key(2, MENU_SCREEN_KEY_LEFT) == MENU_SCREEN_KEY_TAKEN &&
			add_player.state[2] == ADD_PLAYER_EMPTY, "a controller not joining moved the screen, or joined");
	}
	else if (!strcmp(name, "waits-for-all"))
	{
		/* (two joining: the first ready one's A brings no one in while the
		other chooses; the other's START, once ready, brings both) */
		add_player_open(NULL, NULL, NONE, &(boolean){ FALSE });
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_START);
		pc_menu_add_player_key(2, MENU_SCREEN_KEY_A);
		CHECK(add_player.profile[1] != add_player.profile[2], "two joining on one profile at first");
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_A);
		CHECK(pc_menu_add_player_key(1, MENU_SCREEN_KEY_A) == MENU_SCREEN_KEY_TAKEN && world.joined[1] == NONE &&
			world.joined[2] == NONE, "brought in while another still chose");
		CHECK(!pc_menu_add_player_action("add player done") && world.joined[1] == NONE, "DONE while one chose");
		pc_menu_add_player_key(2, MENU_SCREEN_KEY_A);
		CHECK(pc_menu_add_player_key(2, MENU_SCREEN_KEY_START) == MENU_SCREEN_KEY_DONE, "START when all ready");
		CHECK(world.joined[1] != NONE && world.joined[2] != NONE, "joined %ld and %ld", world.joined[1],
			world.joined[2]);
	}
	else if (!strcmp(name, "b-backs-off"))
	{
		/* (B: ready to choosing, choosing to out; BACK lets go of them all) */
		add_player_open(NULL, NULL, NONE, &(boolean){ FALSE });
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_A);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_A);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_B);
		CHECK(add_player.state[1] == ADD_PLAYER_CHOOSING, "B when ready: state %d", add_player.state[1]);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_B);
		CHECK(add_player.state[1] == ADD_PLAYER_EMPTY, "B when choosing: state %d", add_player.state[1]);
		pc_menu_add_player_key(1, MENU_SCREEN_KEY_A);
		CHECK(pc_menu_add_player_action("add player back") && add_player.state[1] == ADD_PLAYER_EMPTY &&
			world.joined[1] == NONE, "BACK kept or joined the one joining");
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
