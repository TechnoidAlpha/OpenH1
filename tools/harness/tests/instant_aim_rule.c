#include "harness.h"

/* 2V2 Classic's Instant Aim is a rule of the game (network_standard.c): the
host made under a Classic playlist's hosting rules hosts an OpenH1 game at
120, closed to OpenCE's players, whatever its settings, and stamps its game
record with _network_game_no_direct_camera_bit; every OpenH1 machine of it
then draws its views as with display.direct_camera off
(network_standard_direct_camera_allowed), the setting itself untouched. */
#include <string.h>
#include "config.inc"
#define MAXIMUM_STANDARD_MACHINES 16
#define SIM_RATE_CLASSIC 30
#define csstrcmp strcmp
#define csstrncmp strncmp
#define csmemset memset
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
struct network_game { byte standard_rate; byte standard_flags; unsigned long cooperative_flags; };
static struct network_game record;
static short connection;
static char const *game_mode = "openh1";
static long sim_rate = 60;
static boolean allow_opence = TRUE;
static unsigned long changes = 1;
static struct
{
	boolean hosting;
	long hosting_rate;
	boolean hosting_hold_to_pick_up;
	boolean hosting_classic_turning;
	/* (the hijacks' rule: test_vehicle_hijack.py's) */
	boolean hosting_vehicle_hijack;
	/* (hit sounds: hit_sounds.c's host rule) */
	boolean hosting_no_hit_sounds;
	boolean mixed;
	boolean mixed_since;
	boolean joined;
	unsigned char machine_kinds[MAXIMUM_STANDARD_MACHINES];
	long rules_rate;
	boolean rules_no_direct_camera;
	boolean hosting_no_direct_camera;
	boolean rules_pro;
	boolean hosting_pro;
} network_standard_globals;
static short game_connection(void) { return connection; }
static struct network_game *network_game_get_game(void) { return &record; }
static boolean halo_server_mode(void) { return FALSE; }
static boolean sim_rate_supported(long rate) { return rate == 30 || rate == 60 || rate == 120; }
static unsigned long config_changes(void) { return changes; }
static long config_integer(char const *name) { (void)name; return sim_rate; }
static char const *config_string(char const *name) { return !strcmp(name, "network.game_mode") ? game_mode : ""; }
static boolean config_boolean(char const *name) { return !strcmp(name, "network.allow_opence") ? allow_opence : FALSE; }
#include "under_test.inc"

/* a server made under the rules (rate, Classic's aim), its record stamped
in the lobby, with this machine's settings: Legacy, 60, OpenCE allowed */
static void host_with_rules(long rate, boolean no_direct_camera)
{
	game_mode = "legacy";
	sim_rate = 60;
	changes++;
	memset(&record, 0, sizeof(record));
	network_standard_set_hosting_rules(rate, no_direct_camera, rate != 0);
	network_standard_server_created();
	network_standard_stamp_game(&record, TRUE);
	connection = _game_connection_network_server;
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("classic_host")
	{
		host_with_rules(120, TRUE);
		CHECK(network_standard_globals.hosting, "a Classic game hosted as Legacy (network.game_mode legacy)");
		CHECK(network_standard_server_rate(TRUE) == 120 && record.standard_rate == 120,
			"a Classic game at the settings' 60, not 120");
		CHECK(!network_standard_globals.mixed, "a Classic game open to OpenCE's players");
		CHECK(!network_standard_direct_camera_allowed(), "the Classic host draws its views from the newest step");
	}
	CASE("classic_client")
	{
		host_with_rules(120, TRUE);
		/* (the record as a client has it: the host's, sent) */
		memset(&network_standard_globals, 0, sizeof(network_standard_globals));
		connection = _game_connection_network_client;
		network_standard_globals.joined = TRUE;
		CHECK(TEST_FLAG(record.standard_flags, _network_game_no_direct_camera_bit), "the record says nothing");
		CHECK(!network_standard_direct_camera_allowed(), "a Classic game's client keeps its own Instant Aim");
	}
	CASE("crispy")
	{
		host_with_rules(120, FALSE);
		CHECK(network_standard_globals.hosting && network_standard_server_rate(TRUE) == 120,
			"a Crispy game not OpenH1 120 Tick");
		CHECK(!network_standard_globals.mixed, "a Crispy game open to OpenCE's players");
		CHECK(network_standard_direct_camera_allowed(), "a Crispy host loses its own Instant Aim");
		CHECK(!TEST_FLAG(record.standard_flags, _network_game_no_direct_camera_bit), "its record forces it off");
		connection = _game_connection_network_client;
		network_standard_globals.joined = TRUE;
		CHECK(network_standard_direct_camera_allowed(), "a Crispy client loses its own Instant Aim");
	}
	CASE("rules_cleared")
	{
		/* (matchmaking's server gone: the next one hosted is the settings') */
		host_with_rules(120, TRUE);
		network_standard_set_hosting_rules(0, FALSE, FALSE);
		game_mode = "openh1";
		changes++;
		network_standard_server_created();
		network_standard_stamp_game(&record, TRUE);
		CHECK(network_standard_server_rate(TRUE) == 60, "the settings' 60 not back");
		CHECK(network_standard_globals.mixed, "OpenCE's players not let in again");
		CHECK(network_standard_direct_camera_allowed(), "Instant Aim still forced off");
		CHECK(!TEST_FLAG(record.standard_flags, _network_game_no_direct_camera_bit), "the record still says so");
		game_mode = "legacy";
		network_standard_server_created();
		CHECK(!network_standard_globals.hosting, "a Legacy setting not Legacy again");
	}
	CASE("legacy_client")
	{
		/* (a Legacy host's game, the bit's byte whatever it is: its own) */
		connection = _game_connection_network_client;
		network_standard_globals.joined = FALSE;
		record.standard_flags = FLAG(_network_game_no_direct_camera_bit);
		CHECK(network_standard_direct_camera_allowed(), "a Legacy host's record taken as OpenH1's");
	}
	CASE("out_of_game")
	{
		connection = _game_connection_local;
		network_standard_globals.hosting = TRUE;
		network_standard_globals.hosting_no_direct_camera = TRUE;
		CHECK(network_standard_direct_camera_allowed(), "a game of one machine lost its Instant Aim");
	}
	return 0;
}
