#include "harness.h"

/* The players' animation enhancements are a rule of the game, as every
machine of it has it (network_standard.c's network_standard_enhanced_animations
and network_standard_turns_with_aim): a client takes its host's word
whatever its own game.enhanced_animations, a game of one machine its setting
as its map began, a demo's playback its recording's, never the playback
machine's settings. */
#include "config.inc"
struct network_game { unsigned long cooperative_flags; byte standard_flags; };
static struct network_game record;
static short connection;
static boolean demo_is_playing;
static unsigned long demo_rules;
static struct
{
	boolean hosting;
	boolean hosting_classic_turning;
	/* (the hijacks' rule: test_vehicle_hijack.py's) */
	boolean hosting_vehicle_hijack;
	boolean joined;
	boolean local_enhanced_animations;
} network_standard_globals;
static short game_connection(void) { return connection; }
static struct network_game *network_game_get_game(void) { return &record; }
static boolean network_standard_player_is_opence(long player_index) { return FALSE; }
static boolean demo_playback_rules(unsigned long *rules)
{
	if (!demo_is_playing)
		return FALSE;
	*rules = demo_rules;
	return TRUE;
}
static boolean network_standard_enhanced_animations(void);
#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	/* this machine's own setting: off */
	network_standard_globals.local_enhanced_animations = FALSE;
	CASE("client")
	{
		/* a client of a host with the enhancements on: on, its own setting
		off; and off for a Classic host's, its own on */
		connection = _game_connection_network_client;
		network_standard_globals.joined = TRUE;
		CHECK(network_standard_enhanced_animations(), "a client took its own setting, not its host's");
		CHECK(network_standard_turns_with_aim(0), "a client's turning not its host's");
		record.standard_flags = FLAG(_network_game_classic_turning_bit);
		network_standard_globals.local_enhanced_animations = TRUE;
		CHECK(!network_standard_enhanced_animations(), "a Classic host's client has the enhancements");
		CHECK(!network_standard_turns_with_aim(0), "a Classic host's client turns");
	}
	CASE("host")
	{
		connection = _game_connection_network_server;
		network_standard_globals.hosting = TRUE;
		network_standard_globals.local_enhanced_animations = TRUE;
		network_standard_globals.hosting_classic_turning = TRUE;
		CHECK(!network_standard_enhanced_animations(), "a Classic host has the enhancements");
	}
	CASE("local")
	{
		connection = _game_connection_local;
		CHECK(!network_standard_enhanced_animations(), "a game of one machine not its setting");
		network_standard_globals.local_enhanced_animations = TRUE;
		CHECK(network_standard_enhanced_animations(), "a game of one machine not its setting");
	}
	CASE("playback")
	{
		/* a demo of a game with the enhancements and the turning, played
		where the settings have them off */
		connection = _game_connection_local;
		demo_is_playing = TRUE;
		demo_rules = FLAG(_demo_rule_enhanced_animations_bit) | FLAG(_demo_rule_turns_with_aim_bit);
		CHECK(network_standard_enhanced_animations(), "a playback took this machine's setting");
		CHECK(network_standard_turns_with_aim(0), "a playback's turning this machine's");
		/* ... and of a Legacy host's game: its players turn, without the
		other enhancements */
		demo_rules = FLAG(_demo_rule_turns_with_aim_bit);
		network_standard_globals.local_enhanced_animations = TRUE;
		CHECK(!network_standard_enhanced_animations(), "a playback took this machine's setting");
		CHECK(network_standard_turns_with_aim(0), "a playback of a Legacy game's players do not turn");
	}
	CASE("simulation-fixes")
	{
		/* OpenH1's fixes of the original's simulation (items.c's lift off
		a surface): in every game but a Legacy one, whose OpenCE machines
		play the original's; a playback's its recording's */
		connection = _game_connection_local;
		CHECK(network_standard_simulation_fixes(), "a game of one machine without the fixes");
		connection = _game_connection_network_server;
		network_standard_globals.hosting = TRUE;
		CHECK(network_standard_simulation_fixes(), "an OpenH1 host without the fixes");
		network_standard_globals.hosting = FALSE;
		CHECK(!network_standard_simulation_fixes(), "a Legacy host with the fixes");
		connection = _game_connection_network_client;
		network_standard_globals.joined = TRUE;
		CHECK(network_standard_simulation_fixes(), "an OpenH1 host's client without the fixes");
		network_standard_globals.joined = FALSE;
		CHECK(!network_standard_simulation_fixes(), "a Legacy host's client with the fixes");
		connection = _game_connection_local;
		demo_is_playing = TRUE;
		demo_rules = 0;
		CHECK(!network_standard_simulation_fixes(), "a Legacy game's playback with the fixes");
		demo_rules = FLAG(_demo_rule_simulation_fixes_bit);
		CHECK(network_standard_simulation_fixes(), "an OpenH1 game's playback without the fixes");
	}
	return 0;
}
