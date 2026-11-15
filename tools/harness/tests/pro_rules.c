#include "harness.h"

/* The Pro 2v2 rules (test_pro_rules.py): a 2V2 host's, whatever its own
settings, and its game record's word to every OpenH1 machine. */
#include <math.h>
#include <string.h>
#include "config.inc"
#define MAXIMUM_STANDARD_MACHINES 16
#define SIM_RATE_CLASSIC 30
#define _pi 3.14159265358979323846
#define csstrcmp strcmp
#define csstrncmp strncmp
#define csmemset memset
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
struct network_game { byte standard_rate; byte standard_flags; unsigned long cooperative_flags; };
static struct network_game record;
static short connection;
static char const *game_mode = "openh1";
static char const *shield_setting = "light_show_overshield";
static long sim_rate = 120;
static boolean auto_pickup = TRUE, block_mouse = FALSE, coop = FALSE;
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
static char const *config_string(char const *name)
{
	return !strcmp(name, "network.game_mode") ? game_mode : !strcmp(name, "network.shield_effects") ? shield_setting : "";
}
static int config_boolean(char const *name)
{
	return !strcmp(name, "game.auto_pickup") ? auto_pickup : !strcmp(name, "network.block_mouse_aim_assist") ?
		block_mouse : FALSE;
}
static boolean network_coop_active(void) { return coop; }
static char const *const shield_effects_names[NUMBER_OF_SHIELD_EFFECTS] =
{
	"stock", "glow", "light_show", "overshield", "glow_overshield", "light_show_overshield", "players"
};
static short client_effects = _shield_effects_overshield;
static boolean network_standard_pro_hosting(void);
static short network_shield_effects_hosted(void);
#include "under_test.inc"

/* a server made under 2V2's rules (rate, Classic's aim, pro), its record
stamped, with this machine's settings: 120, the light show, auto pickup,
the mouse's assist allowed */
static void host_with_rules(long rate, boolean classic, boolean pro)
{
	changes++;
	memset(&record, 0, sizeof(record));
	network_standard_set_hosting_rules(rate, classic, pro);
	network_standard_server_created();
	network_standard_stamp_game(&record, TRUE);
	connection = _game_connection_network_server;
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("pro_host")
	{
		host_with_rules(120, TRUE, TRUE);
		CHECK(network_standard_pro_game(), "a Pro host's own game not Pro");
		CHECK(!network_standard_enhanced_lights_allowed(), "a Pro host draws enhanced lights");
		CHECK(!network_standard_teammate_feeds_allowed(), "a Pro host shows teammates' feeds");
	}
	CASE("pro_client")
	{
		host_with_rules(120, TRUE, TRUE);
		CHECK(TEST_FLAG(record.standard_flags, _network_game_pro_rules_bit), "the record does not say Pro");
		memset(&network_standard_globals, 0, sizeof(network_standard_globals));
		connection = _game_connection_network_client;
		network_standard_globals.joined = TRUE;
		CHECK(network_standard_pro_game(), "a Pro game's client not in a Pro game");
		CHECK(!network_standard_enhanced_lights_allowed(), "a Pro game's client draws enhanced lights");
		CHECK(!network_standard_teammate_feeds_allowed(), "a Pro game's client shows teammates' feeds");
	}
	CASE("crispy")
	{
		/* (the Pro rules both kinds', Instant Aim only Classic's) */
		host_with_rules(120, FALSE, TRUE);
		CHECK(network_standard_pro_game() && TEST_FLAG(record.standard_flags, _network_game_pro_rules_bit),
			"Crispy not Pro");
		CHECK(!network_standard_globals.hosting_no_direct_camera, "Crispy's Instant Aim off");
	}
	CASE("not_pro")
	{
		host_with_rules(0, FALSE, FALSE);
		CHECK(!network_standard_pro_game() && network_standard_enhanced_lights_allowed() &&
			network_standard_teammate_feeds_allowed(), "a game of no rules Pro");
		CHECK(!TEST_FLAG(record.standard_flags, _network_game_pro_rules_bit), "its record says Pro");
		connection = _game_connection_network_client;
		network_standard_globals.joined = FALSE;
		record.standard_flags = FLAG(_network_game_pro_rules_bit);
		CHECK(!network_standard_pro_game(), "a Legacy host's byte taken as OpenH1's");
		connection = _game_connection_local;
		CHECK(!network_standard_pro_game(), "a game of one machine Pro");
	}
	CASE("shields")
	{
		host_with_rules(120, TRUE, TRUE);
		CHECK(network_shield_effects() == _shield_effects_stock, "a Pro host's shields its own light show");
		host_with_rules(0, FALSE, FALSE);
		CHECK(network_shield_effects() == _shield_effects_light_show_overshield, "another game's not the host's");
	}
	CASE("aim_assist")
	{
		host_with_rules(120, FALSE, TRUE);
		CHECK(host_blocks_mouse(), "a Pro host lets the mouse's aim assist play");
		host_with_rules(0, FALSE, FALSE);
		CHECK(!host_blocks_mouse(), "another host's setting not taken");
	}
	CASE("pickup")
	{
		/* (as the server is made, before its lobby stamps the record) */
		changes++;
		network_standard_set_hosting_rules(120, FALSE, TRUE);
		network_standard_server_created();
		CHECK(network_standard_globals.hosting_hold_to_pick_up, "a Pro host walks over weapons (made)");
		host_with_rules(120, FALSE, TRUE);
		network_standard_stamp_game(&record, TRUE);
		CHECK(network_standard_globals.hosting_hold_to_pick_up, "a Pro host walks over weapons (its lobby)");
		host_with_rules(0, FALSE, FALSE);
		CHECK(!network_standard_globals.hosting_hold_to_pick_up, "another host's game.auto_pickup not taken");
	}
	CASE("mods")
	{
		char names[80];

		CHECK(distributed_pro_refused_mods("", names, sizeof(names)) == 0, "no mods refused");
		CHECK(distributed_pro_refused_mods("french 1.0 [text]\nlabels [text]", names, sizeof(names)) == 0,
			"text-only mods refused");
		CHECK(distributed_pro_refused_mods("hdtex 1.2 [bitmaps]\nfrench [text]\nhdaudio 2 [sounds]\nguns [models text]",
			names, sizeof(names)) == 3 && !strcmp(names, "hdtex, hdaudio, guns"),
			"bitmaps, sounds and models not all refused: %s", names);
		CHECK(distributed_pro_refused_mods("a [text]\n... [unlisted]", names, sizeof(names)) == 1,
			"mods the list could not hold not refused");
		CHECK(distributed_pro_refused_mods("old 1.0", names, sizeof(names)) == 1 && !strcmp(names, "old"),
			"a mod that does not say what it replaces taken as clean");
		CHECK(distributed_pro_refused_mods("x [sounds]\nyy [sounds]\nzzz [sounds]", names, 8) == 3 &&
			strlen(names) < 8, "the names past their room");
	}
	CASE("fov")
	{
		real cap = (real)(tan(110.0 * _pi / 360.0) * (9.0 / 16.0));

		CHECK(fabs(fov_pro_capped((real)(tan(150.0 * _pi / 360.0) * (9.0 / 16.0))) - cap) < 1e-5,
			"150 not capped at 110");
		CHECK(fov_pro_capped(0.5f) == 0.5f, "a narrower view widened");
		CHECK(fabs(2.0 * atan(cap * 16.0 / 9.0) * 180.0 / _pi - 110.0) < 0.01, "the cap is not 110 across 16:9");
	}
	CASE("collisions")
	{
		/* (a multiplayer game's players collide whatever the bit: only
		co-op's take it) */
		record.cooperative_flags = FLAG(_network_game_cooperative_no_player_collisions_bit);
		coop = FALSE;
		CHECK(network_coop_player_collisions(), "a multiplayer game's players pass through each other");
		coop = TRUE;
		CHECK(!network_coop_player_collisions(), "co-op's switch not taken");
	}
	return 0;
}
