/*
MIXED_LOBBY.C (test)

A mixed game's host (port/linux/game/network_standard.c): an OpenH1 game
that OpenCE players join too (network.allow_opence). What the host
advertises, which join token it takes from whom, what it sends an OpenCE
machine (a Legacy host's record, none of OpenH1's messages) and which players
it steps at 30 (an OpenCE machine's). A Legacy host and an OpenH1-only host
stay as they were. test_mixed_lobby.py takes the code (under_test.inc:
network_standard.c from its constants on) and the constants (config.inc).
*/

#include "harness.h"

typedef unsigned short word;
#define csmemcpy memcpy
#define csmemset memset
#define csmemcmp memcmp
#define csstrcmp strcmp
#define csstrncmp strncmp
#define DATUM_INDEX_NEW(absolute_index, identifier) \
	((long)(((unsigned long)(unsigned short)(identifier) << 16) | (unsigned long)(absolute_index)))

#include "config.inc"

/* ---------- the host's world */

enum
{
	_game_connection_local,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
};

enum
{
	TICKS_PER_SECOND = 30,
	SIM_RATE_CLASSIC = 30,
	MAXIMUM_LOCAL_PLAYERS = 4,
	MAXIMUM_TRACKED_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	JOIN_TOKEN_LENGTH = 16,
	MACHINES = 8,
	SENT = 64,
};

struct distributed_message_header
{
	word header;
	byte type;
	byte count;
	long game_time;
};

/* the record, as far as the host changes it: everything else is copied */
/* the record's pickup bit (network_game_manager.h) */
enum { _network_game_hold_to_pick_up_bit = 3 };
#define SET_FLAG(f, b, v) ((v) ? ((f) |= (unsigned)FLAG(b)) : ((f) &= (unsigned)~FLAG(b)))

struct network_game
{
	wchar_t name[16];
	short machine_count;
	byte machines[64];
	short standard_rate;
	/* (the host's way of picking up a second weapon: network_standard.c's
	_network_game_hold_to_pick_up_bit) */
	byte cooperative_flags;
	unsigned long random_seed;
	byte rest[32];
};

struct network_game_server
{
	struct network_game game;
};

struct player_datum
{
	short identifier;
};

static short connection = _game_connection_network_server;
static char game_mode[16] = "openh1";
static long sim_rate_setting = 60;
static boolean allow_opence = TRUE;
static long now = 100;
static struct network_game_server server;
static boolean serving = TRUE;
static struct network_game client_game;
static boolean in_game = TRUE;
/* each machine's players (datum indices) */
static long player_lists[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_LOCAL_PLAYERS];
static struct player_datum players[MAXIMUM_TRACKED_PLAYERS];
static boolean machine_present[MACHINES];

/* the distributed messages sent: to whom, of which type, the message */
static struct
{
	long machine_index;
	byte type;
	byte bytes[64];
	word size;
} sent[SENT];
static short sent_count;

/* a dedicated server (halo --server), which reads server.game_mode in place
of network.game_mode and network.sim_rate */
static int server_mode;
static char server_game_mode[16] = "legacy";

static int halo_server_mode(void)
{
	return server_mode;
}

static char const *config_string(char const *name)
{
	if (!strcmp(name, "server.game_mode"))
		return server_game_mode;
	return !strcmp(name, "network.game_mode") ? game_mode : "";
}

static long config_integer(char const *name)
{
	return !strcmp(name, "network.sim_rate") ? sim_rate_setting : 0;
}

static int config_boolean(char const *name)
{
	return !strcmp(name, "network.allow_opence") && allow_opence;
}

static short game_connection(void)
{
	return connection;
}

static long game_time_get(void)
{
	return now;
}

static boolean game_in_progress(void)
{
	return in_game;
}

static struct network_game_server *global_network_game_server_get(void)
{
	return serving ? &server : NULL;
}

static struct network_game *network_game_server_get_game(struct network_game_server *game_server)
{
	return &game_server->game;
}

static struct network_game *network_game_get_game(void)
{
	return &client_game;
}

static long *machine_get_player_list(long machine_index)
{
	return player_lists[machine_index];
}

static struct player_datum *distributed_player(short player_index)
{
	return player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS && players[player_index].identifier ?
		&players[player_index] : NULL;
}

static short distributed_client_machines(long *machine_indices, short maximum)
{
	short count = 0;
	long index;

	/* (the host's own machine, 0, is not a client) */
	for (index = 1; index < MACHINES && count < maximum; index++)
	{
		if (machine_present[index])
			machine_indices[count++] = index;
	}
	return count;
}

/* (network_distributed.c's: a datagram's message to a machine, recorded as
it was sent: its guard, network_standard_message_goes_to, is a case of its
own) */
static void distributed_send_to_machine(long machine_index, void *message, byte type, short count, word size)
{
	(void)count;
	CHECK(sent_count < SENT && size <= sizeof(sent[0].bytes), "too many messages sent");
	sent[sent_count].machine_index = machine_index;
	sent[sent_count].type = type;
	sent[sent_count].size = size;
	memcpy(sent[sent_count].bytes, message, size);
	((struct distributed_message_header *)sent[sent_count].bytes)->type = type;
	sent_count++;
}

static boolean sim_rate_supported(long rate)
{
	return rate == 30 || rate == 60 || rate == 120;
}

#include "under_test.inc"

/* ---------- the cases */

/* a machine at the index in the game, with one player (absolute index) */
static void machine_add(long machine_index, short player_index)
{
	short local;

	machine_present[machine_index] = TRUE;
	for (local = 0; local < MAXIMUM_LOCAL_PLAYERS; local++)
		player_lists[machine_index][local] = NONE;
	players[player_index].identifier = (short)(0x8000 | (0x100 + player_index));
	player_lists[machine_index][0] = DATUM_INDEX_NEW(player_index, players[player_index].identifier);
}

static long player_datum(short player_index)
{
	return DATUM_INDEX_NEW(player_index, players[player_index].identifier);
}

/* the host made (network.game_mode and the rest read) */
static void host(char const *mode, boolean opence)
{
	long machine_index;
	short local;

	snprintf(game_mode, sizeof(game_mode), "%s", mode);
	allow_opence = opence;
	for (machine_index = 0; machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine_index++)
	{
		for (local = 0; local < MAXIMUM_LOCAL_PLAYERS; local++)
			player_lists[machine_index][local] = NONE;
	}
	network_standard_server_created();
	network_standard_new_game();
}

/* the advertisement's reserved bytes */
static void advertised(byte *reserved)
{
	memset(reserved, 0, 0x1A);
	network_standard_advertise(reserved, TRUE);
}

static word advertised_version(byte const *reserved)
{
	return (word)(reserved[HALO_PORT_ADVERTISED_VERSION_OFFSET] |
		reserved[HALO_PORT_ADVERTISED_VERSION_OFFSET + 1] << 8);
}

/* the game's token (network_game_generate_join_game_token), and a Standard
game's made of it, as a joining OpenH1 machine makes it */
static void tokens(byte *legacy, byte *standard)
{
	short index;

	for (index = 0; index < JOIN_TOKEN_LENGTH; index++)
		legacy[index] = (byte)(0x40 + index * 7);
	memcpy(standard, legacy, JOIN_TOKEN_LENGTH);
	network_standard_join_token(standard, JOIN_TOKEN_LENGTH);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	byte reserved[0x1A];
	byte legacy_token[JOIN_TOKEN_LENGTH];
	byte standard_token[JOIN_TOKEN_LENGTH];
	byte other_token[JOIN_TOKEN_LENGTH];

	tokens(legacy_token, standard_token);
	memcpy(other_token, legacy_token, JOIN_TOKEN_LENGTH);
	other_token[3] ^= 1;

	/* a Legacy host: OpenCE's advertisement, byte for byte, and its own
	token alone; it knows no machine's kind */
	CASE("legacy-host")
	{
		short index;

		host("legacy", TRUE);
		advertised(reserved);
		CHECK(advertised_version(reserved) == HALO_PORT_NETWORK_VERSION, "version %u", advertised_version(reserved));
		for (index = 2; index < (short)sizeof(reserved); index++)
			CHECK(reserved[index] == 0, "reserved byte %d is %u", index, reserved[index]);
		CHECK(network_standard_join_kind(legacy_token, legacy_token, JOIN_TOKEN_LENGTH) ==
			_network_standard_join_legacy, "its own token refused");
		CHECK(network_standard_join_kind(standard_token, legacy_token, JOIN_TOKEN_LENGTH) == NONE,
			"a Standard token taken");
		CHECK(network_standard_join_kind(other_token, legacy_token, JOIN_TOKEN_LENGTH) == NONE, "another token taken");
		network_standard_machine_joined(1, _network_standard_join_legacy);
		CHECK(!network_standard_machine_is_opence(1) && !network_standard_machine_is_openh1(1), "a kind known");
		CHECK(!network_standard_hosting_mixed(), "mixed");
		return 0;
	}

	/* an OpenH1-only host (network.allow_opence false): the Standard
	version and its rate, the Standard token alone */
	CASE("openh1-host")
	{
		host("openh1", FALSE);
		advertised(reserved);
		CHECK(advertised_version(reserved) == HALO_PORT_STANDARD_ADVERTISED_VERSION, "version %x",
			advertised_version(reserved));
		CHECK(reserved[HALO_PORT_ADVERTISED_RATE_OFFSET] == 60, "rate %u", reserved[HALO_PORT_ADVERTISED_RATE_OFFSET]);
		CHECK(reserved[HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET] == 0, "a mixed game's version");
		CHECK(network_standard_join_kind(standard_token, legacy_token, JOIN_TOKEN_LENGTH) ==
			_network_standard_join_openh1, "the Standard token refused");
		CHECK(network_standard_join_kind(legacy_token, legacy_token, JOIN_TOKEN_LENGTH) == NONE,
			"OpenCE's token taken");
		CHECK(!network_standard_hosting_mixed(), "mixed");
		return 0;
	}

	/* a mixed host: the Legacy game's version (OpenCE joins it), the rate
	and the OpenH1 protocol beside it in bytes OpenCE does not read, and
	both tokens, each machine known by its own */
	CASE("mixed-host")
	{
		host("openh1", TRUE);
		sim_rate_setting = 120;
		advertised(reserved);
		CHECK(advertised_version(reserved) == HALO_PORT_NETWORK_VERSION, "version %x", advertised_version(reserved));
		CHECK(reserved[HALO_PORT_ADVERTISED_RATE_OFFSET] == 120, "rate %u", reserved[HALO_PORT_ADVERTISED_RATE_OFFSET]);
		CHECK(reserved[HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET] == HALO_PORT_STANDARD_NETWORK_VERSION,
			"protocol %u", reserved[HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET]);
		CHECK(network_standard_join_kind(standard_token, legacy_token, JOIN_TOKEN_LENGTH) ==
			_network_standard_join_openh1, "the Standard token");
		CHECK(network_standard_join_kind(legacy_token, legacy_token, JOIN_TOKEN_LENGTH) ==
			_network_standard_join_opence, "OpenCE's token");
		CHECK(network_standard_join_kind(other_token, legacy_token, JOIN_TOKEN_LENGTH) == NONE, "another token taken");
		network_standard_machine_joined(1, _network_standard_join_opence);
		network_standard_machine_joined(2, _network_standard_join_openh1);
		CHECK(network_standard_machine_is_opence(1) && !network_standard_machine_is_openh1(1), "machine 1");
		CHECK(network_standard_machine_is_openh1(2) && !network_standard_machine_is_opence(2), "machine 2");
		/* (a machine new at an index takes its own kind) */
		network_standard_machine_joined(1, _network_standard_join_openh1);
		CHECK(!network_standard_machine_is_opence(1), "machine 1's old kind kept");
		return 0;
	}

	/* an OpenCE machine's record is a Legacy host's: no rate, all else the
	same, byte for byte; an OpenH1 machine's has the rate */
	CASE("record")
	{
		struct network_game const *record;
		struct network_game legacy;

		host("openh1", TRUE);
		memset(&server.game, 0x5A, sizeof(server.game));
		network_standard_stamp_game(&server.game, TRUE);
		CHECK(server.game.standard_rate == 60, "stamped %d", server.game.standard_rate);
		record = network_standard_game_for_machine(&server.game, FALSE);
		CHECK(record == &server.game, "an OpenH1 machine's record is not the game's");
		record = network_standard_game_for_machine(&server.game, TRUE);
		memcpy(&legacy, &server.game, sizeof(legacy));
		legacy.standard_rate = 0;
		CHECK(!memcmp(record, &legacy, sizeof(legacy)), "an OpenCE machine's record is not a Legacy host's");
		CHECK(server.game.standard_rate == 60, "the game's own record changed");
		return 0;
	}

	/* none of OpenH1's messages to an OpenCE machine, every other one */
	CASE("messages")
	{
		host("openh1", TRUE);
		network_standard_machine_joined(1, _network_standard_join_opence);
		network_standard_machine_joined(2, _network_standard_join_openh1);
		CHECK(network_standard_message_goes_to(1, FIRST_OPENH1_DISTRIBUTED_MESSAGE - 1), "a Legacy message held");
		CHECK(!network_standard_message_goes_to(1, FIRST_OPENH1_DISTRIBUTED_MESSAGE), "sub-steps to OpenCE");
		CHECK(!network_standard_message_goes_to(1, _distributed_message_opence_players), "players to OpenCE");
		CHECK(network_standard_message_goes_to(2, FIRST_OPENH1_DISTRIBUTED_MESSAGE), "sub-steps held from OpenH1");
		/* (and none held by a host of no kind of the machine) */
		host("legacy", TRUE);
		network_standard_machine_joined(1, _network_standard_join_legacy);
		CHECK(network_standard_message_goes_to(1, FIRST_OPENH1_DISTRIBUTED_MESSAGE), "held by a Legacy host");
		return 0;
	}

	/* the host steps an OpenCE machine's players at 30, the others at the
	game's rate, and tells its OpenH1 clients which are which, not its
	OpenCE ones, when they change and each second */
	CASE("players")
	{
		short index;
		struct distributed_message_header header;

		host("openh1", TRUE);
		machine_add(0, 0);
		machine_add(1, 1);
		machine_add(2, 2);
		network_standard_machine_joined(1, _network_standard_join_opence);
		network_standard_machine_joined(2, _network_standard_join_openh1);
		CHECK(!network_standard_player_is_opence(player_datum(0)), "the host's player");
		CHECK(network_standard_player_is_opence(player_datum(1)), "the OpenCE player");
		CHECK(!network_standard_player_is_opence(player_datum(2)), "the OpenH1 player");
		CHECK(!network_standard_player_is_opence(NONE), "no player");
		network_standard_host_tick();
		CHECK(sent_count == 1 && sent[0].machine_index == 2 && sent[0].type == _distributed_message_opence_players,
			"%d sent (to %ld)", sent_count, sent_count ? sent[0].machine_index : NONE);
		memcpy(&header, sent[0].bytes, sizeof(header));
		CHECK(sent[0].bytes[sizeof(header)] == FLAG(1), "players %x", sent[0].bytes[sizeof(header)]);
		for (index = 1; index < 10; index++)
		{
			now++;
			network_standard_host_tick();
		}
		CHECK(sent_count == 1, "sent again unchanged");
		machine_add(1, 3);
		now++;
		network_standard_host_tick();
		CHECK(sent_count == 2 && sent[1].bytes[sizeof(header)] == FLAG(3), "a change not sent at once");
		now += TICKS_PER_SECOND;
		network_standard_host_tick();
		CHECK(sent_count == 3, "not sent again each second");
		/* (a client of the game takes them from its host's word) */
		connection = _game_connection_network_client;
		network_standard_client_joining(TRUE, standard_token, JOIN_TOKEN_LENGTH);
		memset(players, 0, sizeof(players));
		network_standard_new_game();
		CHECK(!network_standard_player_is_opence(DATUM_INDEX_NEW(3, 0x8103)), "known before told");
		network_standard_handle_message(NONE, (word const *)sent[1].bytes, sent[1].size);
		/* (heard in a tick, taken as the next begins: a lane holds for a tick) */
		CHECK(!network_standard_player_is_opence(DATUM_INDEX_NEW(3, 0x8103)), "taken in the tick it was heard");
		now++;
		CHECK(network_standard_player_is_opence(DATUM_INDEX_NEW(3, 0x8103)), "player 3 not taken");
		CHECK(!network_standard_player_is_opence(DATUM_INDEX_NEW(1, 0x8101)), "player 1 kept");
		return 0;
	}

	/* an OpenH1-only game, and a Legacy one, step no player at 30 */
	CASE("players-unmixed")
	{
		host("openh1", FALSE);
		machine_add(1, 1);
		network_standard_machine_joined(1, _network_standard_join_opence);
		CHECK(!network_standard_player_is_opence(player_datum(1)), "an OpenH1-only game's player at 30");
		network_standard_host_tick();
		CHECK(sent_count == 0, "an OpenH1-only host told of OpenCE players");
		host("legacy", TRUE);
		machine_add(1, 1);
		CHECK(!network_standard_player_is_opence(player_datum(1)), "a Legacy game's player at 30");
		return 0;
	}

	/* a client that joined as OpenCE joins counts what of OpenH1's it hears
	(the tests' check), one that joined as OpenH1 does not */
	/* a dedicated server: server.game_mode, not network.game_mode and
	network.sim_rate (a player's host's OpenH1 120 by default), says the kind
	and the rate; "legacy" by default. A Legacy setting taken while an OpenH1
	game is hosted (the director takes it only once the server is empty)
	keeps its rate */
	CASE("dedicated-server")
	{
		long rate;
		boolean opence;

		server_mode = TRUE;
		sim_rate_setting = 120;
		host("openh1", TRUE);
		advertised(reserved);
		CHECK(!network_standard_hosting(), "a server's default hosted an OpenH1 game");
		CHECK(advertised_version(reserved) == HALO_PORT_NETWORK_VERSION && !reserved[HALO_PORT_ADVERTISED_RATE_OFFSET],
			"a Legacy server's advertisement: version %x rate %u", advertised_version(reserved),
			reserved[HALO_PORT_ADVERTISED_RATE_OFFSET]);
		rate = network_standard_server_hosted(&opence);
		CHECK(rate == 0 && opence, "hosted %ld, opence %d", rate, opence);
		snprintf(server_game_mode, sizeof(server_game_mode), "openh1_60");
		host("legacy", TRUE);
		advertised(reserved);
		CHECK(network_standard_hosting() && network_standard_hosting_mixed(), "server.game_mode openh1_60 not hosted");
		CHECK(reserved[HALO_PORT_ADVERTISED_RATE_OFFSET] == 60, "rate %u", reserved[HALO_PORT_ADVERTISED_RATE_OFFSET]);
		CHECK(network_standard_server_rate(TRUE) == 60, "rate %ld", network_standard_server_rate(TRUE));
		snprintf(server_game_mode, sizeof(server_game_mode), "openh1_120");
		CHECK(network_standard_server_rate(TRUE) == 120, "the lobby's rate %ld", network_standard_server_rate(TRUE));
		snprintf(server_game_mode, sizeof(server_game_mode), "legacy");
		CHECK(network_standard_server_rate(TRUE) == 120, "a Legacy setting changed the rate to %ld",
			network_standard_server_rate(TRUE));
		allow_opence = FALSE;
		snprintf(server_game_mode, sizeof(server_game_mode), "openh1_120");
		host("openh1", FALSE);
		advertised(reserved);
		CHECK(advertised_version(reserved) == HALO_PORT_STANDARD_ADVERTISED_VERSION, "version %x",
			advertised_version(reserved));
		rate = network_standard_server_hosted(&opence);
		CHECK(rate == 120 && !opence, "hosted %ld, opence %d", rate, opence);
		network_standard_stamp_game(&server.game, TRUE);
		CHECK(server.game.standard_rate == 120, "stamped %d", server.game.standard_rate);
		snprintf(server_game_mode, sizeof(server_game_mode), "openh1_90");
		host("openh1", TRUE);
		CHECK(!network_standard_hosting(), "an unknown rate hosted");
		/* (the server's record lasts from its OpenH1 games to its Legacy
		ones: their rate goes from it) */
		network_standard_stamp_game(&server.game, TRUE);
		CHECK(server.game.standard_rate == 0, "a Legacy game's record kept the rate %d", server.game.standard_rate);
		return 0;
	}

	CASE("legacy-heard")
	{
		long messages, rate;

		connection = _game_connection_network_client;
		network_standard_client_joining(FALSE, legacy_token, JOIN_TOKEN_LENGTH);
		network_standard_message_heard(FIRST_OPENH1_DISTRIBUTED_MESSAGE - 1);
		network_standard_legacy_heard(&messages, &rate);
		CHECK(messages == 0 && rate == 0, "%ld %ld", messages, rate);
		network_standard_message_heard(FIRST_OPENH1_DISTRIBUTED_MESSAGE + 1);
		client_game.standard_rate = 60;
		network_standard_legacy_heard(&messages, &rate);
		CHECK(messages == 1 && rate == 60, "%ld %ld", messages, rate);
		return 0;
	}

	fprintf(stderr, "no case '%s'\n", case_name);
	return 2;
}
