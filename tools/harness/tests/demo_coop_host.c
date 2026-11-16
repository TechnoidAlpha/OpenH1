#include "harness.h"

/* A demo of a network co-op game this machine hosted plays its ticks as the
host's (demo.c's demo_playback_connection and demo_playback_network_game,
through main.c's game_connection and network_game_globals.c's
network_game_get_game): the co-op host's code in the ticks (its players'
spawns beside a teammate, the extra enemies by the network game's players)
runs as it did, where a playback as a game of this machine's own parted from
the recording at its first ticks. Outside the ticks, and for a multiplayer
game's ticks, the playback stays a game of this machine's own. */
#include "config.inc"
struct network_game { short player_count; };
struct scenario { short type; };
static struct scenario scenario = { _scenario_type_solo };
static struct scenario *global_scenario = &scenario;
static boolean engine_running;
static boolean game_engine_running(void) { return engine_running; }
static struct { short connection; } main_globals;
static struct
{
	boolean playing;
	long connection;
} demo_player;
static struct { boolean in_tick; } demo_recorder;
static struct network_game demo_network_game;
static struct network_game hosted_game;
static void *global_network_game_server, *global_network_game_client;
static struct network_game *network_game_server_get_game(void *server) { return &hosted_game; }
static struct network_game *network_game_client_get_game(void *client) { return &hosted_game; }
static boolean demo_playback_connection(short *connection);
static struct network_game *demo_playback_network_game(void);
static short game_connection(void);
#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	short connection;

	CASE("recording")
	{
		/* the host recording: its own connection and game */
		main_globals.connection = _game_connection_network_server;
		global_network_game_server = &hosted_game;
		demo_recorder.in_tick = TRUE;
		CHECK(game_connection() == _game_connection_network_server, "the host is not the host");
		CHECK(network_game_get_game() == &hosted_game, "the host's game not its own");
		CHECK(!demo_playback_connection(&connection), "a recording plays back");
	}
	CASE("coop_tick")
	{
		/* a playback (a game of this machine's own) of a co-op host's demo,
		inside a tick */
		main_globals.connection = _game_connection_local;
		demo_player.playing = TRUE;
		demo_player.connection = _game_connection_network_server;
		demo_recorder.in_tick = TRUE;
		CHECK(game_connection() == _game_connection_network_server, "a co-op host's ticks played as a local game's");
		CHECK(network_coop_active(), "a co-op host's ticks played as the campaign's");
		CHECK(network_game_get_game() == &demo_network_game, "a co-op host's ticks read no network game");
	}
	CASE("coop_frames")
	{
		/* ... between its ticks (the frames, the menus, the netcode) */
		main_globals.connection = _game_connection_local;
		demo_player.playing = TRUE;
		demo_player.connection = _game_connection_network_server;
		demo_recorder.in_tick = FALSE;
		CHECK(game_connection() == _game_connection_local, "a playback's frames played as a host's");
		CHECK(network_game_get_game() == NULL, "a playback's frames read a network game");
	}
	CASE("multiplayer_tick")
	{
		/* a hosted multiplayer game's ticks run as this machine's (what the
		host decided by its machines is recorded) */
		main_globals.connection = _game_connection_local;
		demo_player.playing = TRUE;
		demo_player.connection = _game_connection_network_server;
		demo_recorder.in_tick = TRUE;
		engine_running = TRUE;
		CHECK(game_connection() == _game_connection_local, "a multiplayer game's ticks played as its host's");
		CHECK(network_game_get_game() == NULL, "a multiplayer game's ticks read a network game");
	}
	CASE("local_tick")
	{
		/* a demo of a local game */
		main_globals.connection = _game_connection_local;
		demo_player.playing = TRUE;
		demo_player.connection = _game_connection_local;
		demo_recorder.in_tick = TRUE;
		CHECK(game_connection() == _game_connection_local, "a local game's ticks played as a host's");
		CHECK(!network_coop_active(), "a local game's demo played as co-op");
	}
	return 0;
}
