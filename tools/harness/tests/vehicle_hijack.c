/*
VEHICLE_HIJACK.C (test)

The real vehicle_hijack_seat_allowed (port/linux/game/vehicle_hijack.c), the
decision whether a rider's seat may be hijacked, and network_standard.c's
rule (network_standard_vehicle_hijack) over a fake game: an enemy's living
rider of a Ghost's or a Warthog's driver's seat, a Warthog's gunner's, a
Banshee's on or near the ground; never an ally, the Scorpion, a turret, a
passenger, a flying Banshee, an overturned or fast vehicle, an OpenCE
machine's player; the rule an OpenH1 host's (its record and its word to
its clients), never a Legacy game's, a game of this machine's own its
setting, a demo's its recording's. test_vehicle_hijack.py takes the code
(under_test.inc) and the constants and structures (config.inc).
*/

#include "harness.h"

#define SET_FLAG(f, b, v) ((v) ? ((f) |= (unsigned)FLAG(b)) : ((f) &= (unsigned)~FLAG(b)))
#define REAL_MAX 3.4e38f

#include "config.inc"

enum
{
	_game_connection_local = 0,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
};

/* the game record, as much as the code reads */
struct network_game
{
	byte cooperative_flags;
	byte standard_flags;
};

/* the game: its connection, network_standard.c's state, the record, a demo */
static short connection;
static short game_connection(void) { return connection; }
static struct
{
	boolean hosting;
	boolean hosting_vehicle_hijack;
	boolean joined;
	boolean local_vehicle_hijack;
	short vehicle_hijack_heard;
} network_standard_globals = { FALSE, FALSE, FALSE, FALSE, NONE };
static struct network_game record;
static struct network_game *network_game_get_game(void) { return &record; }
static boolean playing;
static unsigned long demo_rules;
static struct network_game *demo_game;
static boolean demo_playback_rules(unsigned long *rules)
{
	if (!playing)
		return FALSE;
	*rules = demo_rules;
	return TRUE;
}
static struct network_game *demo_playback_recorded_game(void) { return demo_game; }

#include "under_test.inc"

/* a Ghost's driver, an enemy, alive, upright, still, with the rule on */
static struct vehicle_hijack_seat_facts ghost(void)
{
	struct vehicle_hijack_seat_facts facts;

	memset(&facts, 0, sizeof(facts));
	facts.rule = TRUE;
	facts.enemy = TRUE;
	facts.rider_alive = TRUE;
	facts.kind = _vehicle_hijack_kind_scout;
	facts.part = _vehicle_hijack_seat_driver;
	facts.upright = TRUE;
	facts.speed = 0.02f;
	facts.ground_clearance = 0.0f;
	return facts;
}

static boolean allowed(struct vehicle_hijack_seat_facts facts)
{
	return vehicle_hijack_seat_allowed(&facts);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	struct vehicle_hijack_seat_facts facts;

	/* enemies and allies */
	CASE("enemy")
	{
		CHECK(allowed(ghost()), "an enemy's Ghost could not be hijacked");
		facts = ghost();
		facts.enemy = FALSE;
		CHECK(!allowed(facts), "an ally's Ghost was hijacked");
		facts = ghost();
		facts.rider_alive = FALSE;
		CHECK(!allowed(facts), "a dead rider's seat was hijacked");
		facts = ghost();
		facts.opence = TRUE;
		CHECK(!allowed(facts), "an OpenCE machine's player was in a hijack");
		return 0;
	}
	/* the seats: a Warthog's driver and gunner, never a passenger; the
	Scorpion and a turret never */
	CASE("seat")
	{
		facts = ghost();
		facts.kind = _vehicle_hijack_kind_jeep;
		CHECK(allowed(facts), "a Warthog's driver could not be hijacked");
		facts.part = _vehicle_hijack_seat_gunner;
		CHECK(allowed(facts), "a Warthog's gunner could not be hijacked");
		facts.part = _vehicle_hijack_seat_passenger;
		CHECK(!allowed(facts), "a Warthog's passenger was hijacked");
		facts = ghost();
		facts.part = _vehicle_hijack_seat_passenger;
		CHECK(!allowed(facts), "a Ghost's other seat was hijacked");
		facts = ghost();
		facts.kind = _vehicle_hijack_kind_tank;
		CHECK(!allowed(facts), "the Scorpion's driver was hijacked");
		facts.part = _vehicle_hijack_seat_gunner;
		CHECK(!allowed(facts), "the Scorpion's gunner was hijacked");
		facts = ghost();
		facts.kind = _vehicle_hijack_kind_other;
		CHECK(!allowed(facts), "a turret was hijacked");
		facts.kind = _vehicle_hijack_kind_none;
		CHECK(!allowed(facts), "something not a vehicle was hijacked");
		return 0;
	}
	/* the Banshee: on or near the ground, never flying; any vehicle upright
	and slow */
	CASE("banshee")
	{
		facts = ghost();
		facts.kind = _vehicle_hijack_kind_flyer;
		facts.ground_clearance = 0.6f;
		CHECK(allowed(facts), "a landed Banshee could not be hijacked");
		facts.ground_clearance = VEHICLE_HIJACK_FLYER_GROUND_CLEARANCE;
		CHECK(allowed(facts), "a Banshee just off the ground could not be hijacked");
		facts.ground_clearance = 3.0f;
		CHECK(!allowed(facts), "a flying Banshee was hijacked");
		facts.ground_clearance = REAL_MAX;
		CHECK(!allowed(facts), "a Banshee with no ground below was hijacked");
		facts = ghost();
		facts.upright = FALSE;
		CHECK(!allowed(facts), "an overturned Ghost was hijacked");
		facts = ghost();
		facts.speed = 0.3f;
		CHECK(!allowed(facts), "a Ghost at speed was hijacked");
		return 0;
	}
	/* the rule off: nothing */
	CASE("rule-off")
	{
		facts = ghost();
		facts.rule = FALSE;
		CHECK(!allowed(facts), "a Ghost was hijacked with the rule off");
		facts.kind = _vehicle_hijack_kind_jeep;
		CHECK(!allowed(facts), "a Warthog was hijacked with the rule off");
		return 0;
	}
	/* an OpenH1 host: its setting; a Legacy host: never */
	CASE("host")
	{
		connection = _game_connection_network_server;
		network_standard_globals.hosting = TRUE;
		network_standard_globals.hosting_vehicle_hijack = TRUE;
		CHECK(network_standard_vehicle_hijack(), "an OpenH1 host with the rule on played none");
		network_standard_globals.hosting_vehicle_hijack = FALSE;
		CHECK(!network_standard_vehicle_hijack(), "an OpenH1 host with the rule off played it");
		network_standard_globals.hosting = FALSE;
		network_standard_globals.hosting_vehicle_hijack = TRUE;
		CHECK(!network_standard_vehicle_hijack(), "a Legacy host played it");
		return 0;
	}
	/* an OpenH1 client: the record's word, then the host's during the game;
	a Legacy game's: never */
	CASE("client")
	{
		connection = _game_connection_network_client;
		network_standard_globals.joined = TRUE;
		network_standard_globals.local_vehicle_hijack = TRUE;
		CHECK(!network_standard_vehicle_hijack(), "a client took its own setting, not its host's record");
		SET_FLAG(record.standard_flags, _network_game_vehicle_hijack_bit, TRUE);
		CHECK(network_standard_vehicle_hijack(), "a client did not take its host's record");
		network_standard_globals.vehicle_hijack_heard = FALSE;
		CHECK(!network_standard_vehicle_hijack(), "a client did not take its host's word during the game");
		network_standard_globals.vehicle_hijack_heard = TRUE;
		SET_FLAG(record.standard_flags, _network_game_vehicle_hijack_bit, FALSE);
		CHECK(network_standard_vehicle_hijack(), "a client took the record over its host's later word");
		network_standard_globals.joined = FALSE;
		SET_FLAG(record.standard_flags, _network_game_vehicle_hijack_bit, TRUE);
		CHECK(!network_standard_vehicle_hijack(), "a Legacy game's client played it");
		return 0;
	}
	/* a game of this machine's own: its setting; a demo's playback: its
	recording's (a host's record each tick, else its header's) */
	CASE("local")
	{
		network_standard_globals.local_vehicle_hijack = TRUE;
		CHECK(network_standard_vehicle_hijack(), "a local game ignored its setting (on)");
		network_standard_globals.local_vehicle_hijack = FALSE;
		CHECK(!network_standard_vehicle_hijack(), "a local game ignored its setting (off)");
		playing = TRUE;
		SET_FLAG(demo_rules, _demo_rule_vehicle_hijack_bit, TRUE);
		CHECK(network_standard_vehicle_hijack(), "a demo did not play its header's rule");
		demo_game = &record;
		CHECK(!network_standard_vehicle_hijack(), "a host's demo did not play its record's rule");
		SET_FLAG(record.standard_flags, _network_game_vehicle_hijack_bit, TRUE);
		network_standard_globals.local_vehicle_hijack = FALSE;
		CHECK(network_standard_vehicle_hijack(), "a host's demo played this machine's setting");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
