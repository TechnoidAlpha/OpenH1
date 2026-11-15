/*
LOBBY_COUNTDOWN.C (test)

The real pregame countdown of source/networking/network_server_manager.c:
the host's START GAME (the lobby's "faster" request,
_network_game_server_countdown_event_player_joined, from the host's own
machine) starts the game at once, with one press, for a host alone, a host
with split screen players, and machines together; the other machines'
requests count down as the Xbox game's did; and a dedicated server's rule,
its director's (port/linux/game/server_director.c's
server_director_ok_to_countdown: server.min_players, server.team_start,
and its own state), whose countdown a client machine's START NOW takes
five seconds off and never starts. test_lobby_countdown.py takes the constants, enums and
timer structures (config.inc) and the code (under_test.inc).
*/

#include "harness.h"

#define NETWORK_SERVER_MANAGER_FILE "server"
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MILLISECONDS_PER_SECOND 1000
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define CEILING(n, ceiling) ((n) > (ceiling) ? (ceiling) : (n))
#define PIN(n, floor, ceiling) ((n) < (floor) ? (floor) : CEILING((n), (ceiling)))

#include "config.inc"

struct network_player
{
	boolean valid;
	signed char machine_index;
	signed char team_index;
};

struct network_game_server_client_machine
{
	void *connection;
	short machine_index;
	boolean joined;
};

struct network_game_server
{
	short state;
	struct
	{
		short player_count;
		short minimum_players;
		struct
		{
			struct
			{
				boolean teams;
			} universal_variant;
		} variant;
		struct network_player players[MAXIMUM_NETWORK_PLAYER_COUNT];
	} game;
	struct network_game_server_client_machine client_machines[MAXIMUM_NETWORK_MACHINE_COUNT];
	struct network_game_server_countdown_state countdown_state;
	boolean sent_start_game_message;
};

static struct network_game_server server;
static int connection;
static boolean network_game_server_countdown_slowed[MAXIMUM_NETWORK_MACHINE_COUNT];

static unsigned long system_milliseconds(void) { return 1000; }
static boolean network_game_should_accept_remote_connections(void) { return TRUE; }
static boolean splitscreen;
static boolean network_game_is_splitscreen_local(void) { return splitscreen; }
static boolean network_player_is_valid(struct network_player *player) { return player->valid; }
static boolean network_game_server_client_machine_is_joined_to_game(
	struct network_game_server *s,
	struct network_game_server_client_machine *machine)
{
	return machine->joined;
}
/* machine 0 is the host's */
static boolean network_game_server_client_machine_is_local(
	struct network_game_server *s,
	struct network_game_server_client_machine *machine)
{
	return machine->joined && machine->machine_index == 0;
}
static void network_game_server_countdown_started(struct network_game_server *s)
{
	memset(network_game_server_countdown_slowed, 0, sizeof(network_game_server_countdown_slowed));
}
static void network_event(char const *text) { }

/* a dedicated server (port/linux/game/server_director.c): its settings, and
its director's state (test_lobby_countdown.py names it director: this
file's server is the game's) */
static int dedicated;
static long dedicated_minimum_players = 1;
static char const *dedicated_team_start = "one";
static int halo_server_mode(void) { return dedicated; }
static long config_integer(char const *name)
{
	return !strcmp(name, "server.min_players") ? dedicated_minimum_players : 0;
}
static char const *config_string(char const *name)
{
	return !strcmp(name, "server.team_start") ? dedicated_team_start : "";
}
static struct
{
	boolean quitting;
	short state;
	boolean entry_applied;
} director = { FALSE, _server_state_lobby, TRUE };

#include "under_test.inc"

static void join_machine(
	short machine_index)
{
	server.client_machines[machine_index].connection = &connection;
	server.client_machines[machine_index].machine_index = machine_index;
	server.client_machines[machine_index].joined = TRUE;
}

/* machine 0 is the host's; no players yet */
static void lobby(
	short machines)
{
	short index;

	memset(&server, 0, sizeof(server));
	memset(network_game_server_countdown_slowed, 0, sizeof(network_game_server_countdown_slowed));
	splitscreen = FALSE;
	server.state = _network_game_server_state_pregame;
	server.game.minimum_players = 2;
	for (index = 0; index < machines; index++)
		join_machine(index);
}

static void add_player(
	short machine_index,
	short team_index)
{
	struct network_player *player = &server.game.players[server.game.player_count++];

	player->valid = TRUE;
	player->machine_index = machine_index;
	player->team_index = team_index;
}

/* a machine's game start request, as network_server_message_handler.c
takes it: FALSE if it is ignored */
static boolean request(
	short machine_index,
	short request_type)
{
	short countdown_event = network_game_server_game_start_request_event(&server,
		&server.client_machines[machine_index], request_type);

	if (countdown_event == NONE)
		return FALSE;
	network_game_server_update_countdown(&server, countdown_event);
	return TRUE;
}

/* the lobby's START GAME, the host's (the pregame screen's faster) */
static void start_now(void)
{
	request(0, _network_game_server_countdown_event_player_joined);
}

static long time_remaining(void)
{
	return countdown_timer_get_time_remaining(&server.countdown_state.timer);
}

/* the host's game starts now: the server starts it once every machine has
the map (network_game_server_idle_pregame_tasks) */
#define CHECK_STARTS_NOW(what) \
	do \
	{ \
		CHECK(server.countdown_state.active, "%s never starts the game", what); \
		CHECK(time_remaining() == 0, "%s: %ld ms to go, not a start at once", what, time_remaining()); \
	} while (0)

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("split-screen-start-now")
	{
		lobby(1);
		add_player(0, 0);
		add_player(0, 1);
		start_now();
		CHECK_STARTS_NOW("one machine's two players");
		return 0;
	}
	CASE("alone-start-now")
	{
		lobby(1);
		add_player(0, 0);
		start_now();
		CHECK_STARTS_NOW("the host alone");
		return 0;
	}
	CASE("empty-lobby-waits")
	{
		lobby(1);
		CHECK(!server_ok_to_countdown(&server), "an empty lobby may count down");
		start_now();
		CHECK(!server.countdown_state.active, "an empty lobby started the countdown");
		return 0;
	}
	CASE("two-machines-start-now")
	{
		lobby(2);
		add_player(0, 0);
		add_player(1, 1);
		start_now();
		CHECK_STARTS_NOW("two machines' host");
		return 0;
	}
	CASE("two-machines-team-game-needs-both-teams")
	{
		lobby(2);
		server.game.variant.universal_variant.teams = TRUE;
		add_player(0, 0);
		add_player(1, 0);
		CHECK(!server_ok_to_countdown(&server), "two machines' players of one team may count down");
		start_now();
		CHECK(!server.countdown_state.active, "two machines' players of one team started the countdown");
		return 0;
	}
	/* server_ok_to_countdown is checked each tick, and a failure stops the countdown */
	CASE("playerless-machine-keeps-countdown")
	{
		lobby(1);
		add_player(0, 0);
		start_now();
		CHECK(server.countdown_state.active, "the host alone no longer starts the countdown");
		join_machine(1);
		CHECK(server_ok_to_countdown(&server), "a machine joining without a player stops the countdown");
		return 0;
	}
	/* intended: one machine skips the team rule, as a lone host does */
	CASE("split-screen-team-game-one-team")
	{
		lobby(1);
		server.game.variant.universal_variant.teams = TRUE;
		add_player(0, 0);
		add_player(0, 0);
		start_now();
		CHECK_STARTS_NOW("one machine's players of one team");
		return 0;
	}
	/* a countdown another machine started (its faster, the Xbox game's): the
	host's one press starts the game, even in the tick the countdown changed */
	CASE("host-start-during-countdown")
	{
		lobby(2);
		add_player(0, 0);
		add_player(1, 1);
		CHECK(request(1, _network_game_server_countdown_event_player_joined), "another machine's faster is ignored");
		CHECK(request(1, _network_game_server_countdown_event_player_joined), "another machine's faster is ignored");
		CHECK(server.countdown_state.active && server.countdown_state.adjusted_time_this_tick,
			"another machine's fasters did not change the countdown this tick");
		start_now();
		CHECK_STARTS_NOW("the host's press after another machine's faster");
		return 0;
	}
	/* the other machines' requests are the Xbox game's: a faster starts the
	countdown and takes time off it, a slower adds to it (once), a stop or a
	start at once is not theirs */
	CASE("other-machines-count-down")
	{
		lobby(2);
		add_player(0, 0);
		add_player(1, 1);
		CHECK(!request(1, _network_game_server_countdown_event_start_immediately),
			"another machine may start the game at once");
		CHECK(!request(1, _network_game_server_countdown_event_stop), "another machine may stop the countdown");
		CHECK(!server.countdown_state.active, "another machine's start at once or stop started the countdown");
		request(1, _network_game_server_countdown_event_player_joined);
		CHECK(time_remaining() == NETWORK_GAME_COUNTDOWN_TIME,
			"another machine's faster: %ld ms to go, not the Xbox game's countdown", time_remaining());
		server.countdown_state.adjusted_time_this_tick = FALSE;
		request(1, _network_game_server_countdown_event_player_joined);
		CHECK(time_remaining() == NETWORK_GAME_COUNTDOWN_TIME - NETWORK_GAME_COUNTDOWN_ADJUSTMENT,
			"another machine's second faster: %ld ms to go", time_remaining());
		server.countdown_state.adjusted_time_this_tick = FALSE;
		CHECK(request(1, _network_game_server_countdown_event_player_left), "another machine's first slower is ignored");
		server.countdown_state.adjusted_time_this_tick = FALSE;
		CHECK(!request(1, _network_game_server_countdown_event_player_left), "another machine's second slower is taken");
		return 0;
	}
	/* the host's start at once (a script's) and its stop are still its own */
	CASE("host-stop-and-start-at-once")
	{
		lobby(1);
		add_player(0, 0);
		CHECK(request(0, _network_game_server_countdown_event_start_immediately), "the host's start at once is ignored");
		CHECK_STARTS_NOW("the host's start at once");
		server.countdown_state.adjusted_time_this_tick = FALSE;
		CHECK(request(0, _network_game_server_countdown_event_stop), "the host's stop is ignored");
		CHECK(!server.countdown_state.active, "the host's stop did not stop the countdown");
		return 0;
	}
	/* a dedicated server's host machine has no player: two machines' players
	of one team start (server.team_start "one"), as Halo PC's servers do */
	CASE("dedicated-one-team-starts")
	{
		dedicated = 1;
		lobby(3);
		server.game.variant.universal_variant.teams = TRUE;
		add_player(1, 0);
		add_player(2, 0);
		CHECK(server_ok_to_countdown(&server), "a dedicated server's players of one team may not count down");
		return 0;
	}
	CASE("dedicated-both-teams-waits")
	{
		dedicated = 1;
		dedicated_team_start = "both";
		lobby(3);
		server.game.variant.universal_variant.teams = TRUE;
		add_player(1, 0);
		add_player(2, 0);
		CHECK(!server_ok_to_countdown(&server), "server.team_start both counted down with one team");
		server.game.players[1].team_index = 1;
		CHECK(server_ok_to_countdown(&server), "server.team_start both did not count down with both teams");
		return 0;
	}
	CASE("dedicated-below-minimum-waits")
	{
		dedicated = 1;
		dedicated_minimum_players = 2;
		lobby(2);
		add_player(1, 0);
		CHECK(!server_ok_to_countdown(&server), "a dedicated server below server.min_players counted down");
		add_player(1, 1);
		CHECK(server_ok_to_countdown(&server), "a dedicated server at server.min_players did not count down");
		return 0;
	}
	/* server.min_players is held to 1 to 16 */
	CASE("dedicated-minimum-clamped")
	{
		short index;

		dedicated = 1;
		dedicated_minimum_players = 0;
		lobby(3);
		CHECK(!server_ok_to_countdown(&server), "server.min_players 0 counted down with no player");
		add_player(1, 0);
		CHECK(server_ok_to_countdown(&server), "server.min_players 0 did not count down with one player");
		dedicated_minimum_players = 17;
		for (index = 1; index < 16; index++)
			add_player(1 + index % 2, 0);
		CHECK(server.game.player_count == 16, "%d players", server.game.player_count);
		CHECK(server_ok_to_countdown(&server), "server.min_players 17 did not count down with 16 players");
		return 0;
	}
	/* the director's own state: stopping, not in its lobby, or its entry not
	set yet, the lobby never counts down */
	CASE("dedicated-director-refuses")
	{
		dedicated = 1;
		lobby(2);
		add_player(1, 0);
		CHECK(server_ok_to_countdown(&server), "the lobby did not count down");
		director.quitting = TRUE;
		CHECK(!server_ok_to_countdown(&server), "a server stopping counted down");
		director.quitting = FALSE;
		director.state = _server_state_loading;
		CHECK(!server_ok_to_countdown(&server), "a server out of its lobby counted down");
		director.state = _server_state_lobby;
		director.entry_applied = FALSE;
		CHECK(!server_ok_to_countdown(&server), "a lobby with no entry set counted down");
		director.entry_applied = TRUE;
		CHECK(server_ok_to_countdown(&server), "the lobby did not count down again");
		return 0;
	}
	/* a dedicated server's director starts its countdown (server.lobby_seconds):
	a client machine's START NOW in a lobby whose countdown the tick stopped (its teams short,
	server.team_start "both") starts none, and takes five seconds off the
	director's */
	CASE("dedicated-start-now-five-seconds")
	{
		dedicated = 1;
		dedicated_team_start = "both";
		lobby(3);
		server.game.variant.universal_variant.teams = TRUE;
		add_player(1, 0);
		add_player(2, 1);
		network_game_server_port_countdown_begin(&server, 14999);
		server.game.players[1].team_index = 0;
		CHECK(!server_ok_to_countdown(&server), "server.team_start both counted down with one team");
		memset(&server.countdown_state, 0, sizeof(server.countdown_state));
		server.game.players[1].team_index = 1;
		request(1, _network_game_server_countdown_event_player_joined);
		CHECK(!server.countdown_state.active, "a START NOW started a %ld ms countdown",
			server.countdown_state.timer.time_remaining);
		network_game_server_port_countdown_begin(&server, 14999);
		request(1, _network_game_server_countdown_event_player_joined);
		CHECK(server.countdown_state.timer.time_remaining == 14999 - NETWORK_GAME_COUNTDOWN_ADJUSTMENT,
			"%ld ms to go after a START NOW", server.countdown_state.timer.time_remaining);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
