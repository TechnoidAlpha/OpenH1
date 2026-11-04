/*
NEXT_GAME_LOBBY.C (test)

A network game's end and the next game: the host's PICK GAME on the scores
(game_engine.c's network_game_server_reset_to_pregame) sends every machine
to the next game's lobby, where each loads the main menu's map
(network_game_reset_for_next_round, main.c's main_load_ui_scenario) behind
the loading screen; the lobby must then be drawn, the loading screen ended
(the first game picture ends it: main_loop's halo_loading_end), and the
next game play at the rate agreed for it. test_next_game_lobby.py takes the
code: the game's clock (game_time.c), the sub-steps (sim_rate.c), the
Standard games' rate and their OpenCE machines (network_standard.c), the
main menu's map (main.c) and the reset (network_game_manager.c).

A case is a role and a game: "host:<rate>" or "client:<rate>", an OpenH1
game at 30, 60 or 120 ticks a second, or "<role>:legacy", OpenCE's netcode
at 30. The host's next game is at another rate, chosen in the lobby.
"mixed-host:<rate>" and "mixed-client:<rate>" are a mixed game's
(network.allow_opence): its host, with an OpenH1 client (machine 1) and an
OpenCE one (machine 2), or that OpenH1 client. Their players change places
in the next game, as a game's players may.
*/

#include "harness.h"

#include <math.h>

typedef unsigned short word;
typedef word message_header;

#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define csmemset memset
#define csmemcpy memcpy
#define csmemcmp memcmp
#define csstrcmp strcmp
#define csstrncmp strncmp
/* (a player's host: a dedicated server reads server.game_mode, network_standard.c) */
#define halo_server_mode() 0
#define csstrncpy strncpy
#define match_vassert(file, line, condition, ...) ((void)0)
#define MAXIMUM_LOCAL_PLAYERS 4

#include "config.inc"

/* ---------- the fake world */

/* the record's pickup bit (network_game_manager.h) */
enum { _network_game_hold_to_pick_up_bit = 3 };
#define SET_FLAG(f, b, v) ((v) ? ((f) |= (unsigned)FLAG(b)) : ((f) &= (unsigned)~FLAG(b)))

struct network_game
{
	short machine_count;
	short standard_rate;
	/* (the host's way of picking up a second weapon: network_standard.c's
	_network_game_hold_to_pick_up_bit) */
	byte cooperative_flags;
	struct
	{
		boolean game_objects_loaded;
		long reserved;
	} local_data;
};

struct network_game_server
{
	struct network_game game;
};

struct game_options
{
	char map_name[256];
};

static struct
{
	boolean main_menu_scenario_loaded;
	boolean load_last_solo_level;
	boolean reset_map, defer_map_change, revert_map, skip_cinematic, saving_map, won_map, lost_map, respawn;
	boolean save_core, load_core, load_core_at_startup, allow_persistent_storage;
	short switch_to_structure_bsp_index;
} main_globals;

static char load_trace_map[256];
static int load_trace_first_frame;

/* the machine: its connection, its server (the host's) and its client's
game record */
static short connection = _game_connection_local;
static struct network_game_server server;
static boolean hosting;
static struct network_game client_game;
/* config.toml's network.game_mode, network.sim_rate and
network.allow_opence (the host's) */
static char const *game_mode = "openh1";
static long network_rate = 60;
static boolean allow_opence;
/* the loading screen (sdl_platform.c's halo_loading_begin and _end) */
static boolean loading;
/* the main menu's map, which main_load_ui_scenario loads */
static int ui_loads;
/* each machine's players (players.c's machine_get_player_list): machine 0
the host's, 1 an OpenH1 client's, 2 an OpenCE one's */
static long machine_players[3][MAXIMUM_LOCAL_PLAYERS];
static long empty_players[MAXIMUM_LOCAL_PLAYERS] = { NONE, NONE, NONE, NONE };

static short game_connection(void)
{
	return connection;
}

static void game_connection_set(short new_connection)
{
	connection = new_connection;
}

static struct network_game_server *global_network_game_server_get(void)
{
	return hosting ? &server : NULL;
}

static void *global_network_game_client_get(void)
{
	return &client_game;
}

static struct network_game *network_game_server_get_game(struct network_game_server *game_server)
{
	return &game_server->game;
}

static struct network_game *network_game_get_game(void)
{
	return hosting ? &server.game : &client_game;
}

static boolean network_game_is_splitscreen_local(void)
{
	return FALSE;
}

static long *machine_get_player_list(long machine_index)
{
	return machine_index >= 0 && machine_index < 3 ? machine_players[machine_index] : empty_players;
}

long network_standard_agreed_rate(void);
void network_standard_new_game(void);

static long config_integer(char const *name)
{
	if (!strcmp(name, "network.sim_rate"))
		return network_rate;
	/* (game.sim_rate, set aside in a network game of other machines) */
	return 60;
}

static int config_boolean(char const *name)
{
	return !strcmp(name, "network.allow_opence") && allow_opence;
}

static unsigned long config_changes(void)
{
	return 1;
}

static char const *config_string(char const *name)
{
	return !strcmp(name, "network.game_mode") ? game_mode : NULL;
}

static real square_root(real x)
{
	return (real)sqrt(x);
}

/* the game's clock (game_time.c) and the maps */
void game_time_initialize_for_new_map(void);
void game_time_dispose_from_old_map(void);
void game_time_start(void);

static void update_server_start(void) {}
static void update_client_start(void) {}
static void error(int level, char const *format, ...) {}
static void halo_load_trace(char const *phase, char const *detail) {}
static void input_flush(void) {}
static boolean game_load(struct game_options *options) { return TRUE; }
/* (game.c's, for every map, the main menu's too: the new map's clock, and
nothing of the last game's netcode carried into it, network_distributed.c's
network_distributed_new_game, among the rest) */
static void game_initialize_for_new_map(void)
{
	game_time_initialize_for_new_map();
	network_standard_new_game();
}
static boolean errors_handle(void) { return FALSE; }
static void create_local_players(void) {}
static void game_initial_pulse(void) {}
static void game_state_try_and_load_from_persistent_storage(void) {}
static void ui_widgets_disable_pause_game(short ticks) {}
#define game_precache_new_map(map_name, blocking) ((void)0)
/* (game.c's: the loading screen begins, the old map's clock goes) */
static void game_dispose_from_old_map(void)
{
	loading = TRUE;
	game_time_dispose_from_old_map();
}
static void game_unload(void) {}
static void game_engine_dispose(void) {}
static void game_set_game_variant(void *variant) {}
static void game_options_new(struct game_options *options) { memset(options, 0, sizeof(*options)); }
static void director_script_camera(boolean on) {}
static void scripted_camera_set(short a, short b, long c) {}
static void main_menu_active(boolean active) {}
static void main_menu_precache_resources(void) { ui_loads++; }
enum { _error_immediate = 1 };

#include "under_test.inc"

/* ---------- the machine's frames */

/* a frame of the main loop: a game picture while a game is in progress
(the main menu's too), which ends the loading screen; else the pregame's,
drawn behind it */
static void frame(void)
{
	if (game_in_progress())
		loading = FALSE;
}

/* a map of a network game loaded and started (network_client_manager.c's
load, as main_new_map's) */
static void network_map_load(void)
{
	game_dispose_from_old_map();
	main_globals.main_menu_scenario_loaded = FALSE;
	game_initialize_for_new_map();
	game_time_start();
	client_game.local_data.game_objects_loaded = TRUE;
	frame();
}

/* a tick begun, and its first sub-step run: at 60 and 120 the tick is open
as the game ends */
static void tick(void)
{
	sim_rate_tick_begin();
	sim_substep_run(0);
}

/* the host's next game's rate, chosen in the lobby */
static long next_rate(long rate)
{
	return rate == 120 ? 30 : rate * 2;
}

/* a player's index: an absolute index, with an identifier */
static long player(long absolute_index)
{
	return (long)(0xE100 + absolute_index) << 16 | absolute_index;
}

/* the game's players: the host's 0, then the OpenH1 client's and the
OpenCE one's, in one order or the other */
static void seat_players(boolean swapped)
{
	memset(machine_players, 0xFF, sizeof(machine_players));
	machine_players[0][0] = player(0);
	machine_players[1][0] = player(swapped ? 2 : 1);
	machine_players[2][0] = player(swapped ? 1 : 2);
}

/* the host's word to its OpenH1 clients of which players are OpenCE's
(network_standard_host_tick's message) */
static void hear_opence_players(long absolute_index)
{
	struct distributed_opence_players message;

	memset(&message, 0, sizeof(message));
	message.header.type = _distributed_message_opence_players;
	message.players[absolute_index / 8] |= (byte)FLAG(absolute_index % 8);
	network_standard_handle_message(NONE, (word const *)&message, sizeof(message));
}

/* a mixed game's players and machines, as each machine knows them: the
OpenCE machine's player stepped once a tick (an OpenCE player), the OpenH1
client's in the game's sub-steps, and what the host sends each */
static void check_mixed(boolean swapped, long rate, char const *game)
{
	long opence = swapped ? 1 : 2, openh1 = swapped ? 2 : 1;

	CHECK(network_standard_player_is_opence(player(opence)), "%s: the OpenCE player %ld stepped as OpenH1's", game,
		opence);
	CHECK(!network_standard_player_is_opence(player(openh1)) && !network_standard_player_is_opence(player(0)),
		"%s: an OpenH1 player stepped as OpenCE's", game);
	if (hosting)
	{
		CHECK(network_standard_machine_is_opence(2) && !network_standard_machine_is_openh1(2),
			"%s: the OpenCE machine not known as OpenCE", game);
		CHECK(network_standard_machine_is_openh1(1) && !network_standard_machine_is_opence(1),
			"%s: the OpenH1 client not known as OpenH1", game);
		CHECK(network_standard_game_for_machine(&server.game, TRUE)->standard_rate == 0,
			"%s: the OpenCE machine sent a rate", game);
		CHECK(network_standard_game_for_machine(&server.game, FALSE)->standard_rate == rate,
			"%s: the OpenH1 client sent the rate %d, not %ld", game,
			network_standard_game_for_machine(&server.game, FALSE)->standard_rate, rate);
	}
}

int main(int argc, char **argv)
{
	static struct game_time_globals_struct game_clock;
	char const *case_name = argc > 1 ? argv[1] : "";
	char const *rate_name = strchr(case_name, ':');
	boolean legacy = rate_name && !strcmp(rate_name + 1, "legacy");
	boolean mixed = !strncmp(case_name, "mixed-", 6);
	char const *role = mixed ? case_name + 6 : case_name;
	long rate = legacy ? 30 : rate_name ? atol(rate_name + 1) : 0;
	long expected_next;

	hosting = !strncmp(role, "host:", 5);
	CHECK((hosting || !strncmp(role, "client:", 7)) && (rate == 30 || rate == 60 || rate == 120) &&
		!(mixed && legacy), "no such case: %s", case_name);
	game_time_globals = &game_clock;
	expected_next = legacy ? 30 : next_rate(rate);
	seat_players(FALSE);

	/* the game: hosted, or joined, at its rate (a mixed game's machines
	joined with their tokens: the host's own client and the OpenH1 one with a
	Standard game's, the OpenCE one with the Legacy game's) */
	if (hosting)
	{
		byte game_token[16] = "message in a bo";
		byte standard_token[16];

		game_mode = legacy ? "legacy" : "openh1";
		network_rate = rate;
		allow_opence = mixed;
		network_standard_server_created();
		memcpy(standard_token, game_token, sizeof(standard_token));
		network_standard_join_token(standard_token, sizeof(standard_token));
		network_standard_machine_joined(0, network_standard_join_kind(standard_token, game_token, sizeof(game_token)));
		if (mixed)
		{
			network_standard_machine_joined(1, network_standard_join_kind(standard_token, game_token, sizeof(game_token)));
			network_standard_machine_joined(2, network_standard_join_kind(game_token, game_token, sizeof(game_token)));
		}
		network_standard_stamp_game(&server.game, TRUE);
		network_standard_stamp_game(&server.game, FALSE);
		connection = _game_connection_network_server;
	}
	else
	{
		byte token[16] = { 0 };

		network_standard_client_joining(!legacy, token, sizeof(token));
		client_game.standard_rate = legacy ? 0 : (short)rate;
		connection = _game_connection_network_client;
	}
	network_map_load();
	CHECK(!loading, "the game was not drawn");
	tick();
	CHECK(sim_rate_get() == rate, "the game played at %ld, not %ld", sim_rate_get(), rate);
	CHECK(sim_substep_next() == (rate > 30 ? 1 : 0), "the tick open: next %d", sim_substep_next());
	if (mixed)
	{
		if (!hosting)
			hear_opence_players(2);
		check_mixed(FALSE, rate, "the game");
	}

	/* the scores, and the host's PICK GAME: the server's reset (its record,
	as the game's map is still loaded), then every machine's client's, which
	loads the main menu's map for the lobby */
	if (hosting)
	{
		network_game_reset_for_next_round(&server.game, FALSE);
		CHECK(!game_in_progress(), "the ended game's clock went on in the lobby");
	}
	network_game_reset_for_next_round(&client_game, TRUE);
	CHECK(ui_loads == 1 && main_menu_is_loaded(), "the lobby's map: loads %d", ui_loads);
	CHECK(load_trace_first_frame == 1 && !strcmp(load_trace_map, "levels\\ui\\ui"),
		"the lobby's map load not measured to its first frame: %d, %s", load_trace_first_frame, load_trace_map);
	CHECK(connection == (hosting ? _game_connection_network_server : _game_connection_network_client),
		"the connection changed: %d", connection);
	CHECK(sim_substep_next() == 0, "the ended game's tick left open in the lobby: next %d", sim_substep_next());
	CHECK(game_in_progress(), "the lobby's clock stopped: it is drawn only as the pregame");
	frame();
	CHECK(!loading, "the loading screen stayed over the lobby");
	frame();
	CHECK(!loading && game_in_progress(), "the lobby's frames stopped");
	if (mixed && hosting)
	{
		/* (the machines stay for the next game: each still of its kind) */
		CHECK(network_standard_hosting_mixed(), "the lobby's game no longer mixed");
		CHECK(network_standard_machine_is_opence(2) && network_standard_machine_is_openh1(1),
			"the lobby forgot its machines' kinds");
	}

	/* the next game: the host's choice of rate in the lobby, which the
	settings bring every machine; the players in other places */
	if (hosting)
	{
		network_rate = expected_next;
		network_standard_stamp_game(&server.game, TRUE);
		network_standard_stamp_game(&server.game, FALSE);
	}
	else if (!legacy)
		client_game.standard_rate = (short)expected_next;
	CHECK(!hosting || legacy || server.game.standard_rate == expected_next, "the record's rate %d, not %ld",
		server.game.standard_rate, expected_next);
	seat_players(TRUE);
	network_map_load();
	CHECK(!loading, "the next game was not drawn");
	tick();
	CHECK(sim_rate_get() == expected_next, "the next game played at %ld, not %ld", sim_rate_get(), expected_next);
	if (mixed)
	{
		/* (an OpenH1 client: none OpenCE's until its host says, then the
		next game's) */
		if (!hosting)
		{
			CHECK(!network_standard_player_is_opence(player(2)), "the last game's OpenCE player kept");
			hear_opence_players(1);
			/* (taken as the next tick begins) */
			game_time_globals->local_time++;
		}
		check_mixed(TRUE, expected_next, "the next game");
	}
	return 0;
}
