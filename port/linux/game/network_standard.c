/*
NETWORK_STANDARD.C

OpenH1's two kinds of network game (NETCODE.md, "Standard and Legacy
games"):

- Legacy: OpenCE's netcode at HALO_PORT_NETWORK_VERSION, at 30 ticks a
  second, which OpenCE builds join. Nothing of it changes here: its
  advertisement, listing, join token and game record are as they were.
- Standard: OpenH1's own (network.game_mode "openh1", or "standard" as
  it was first named; the default when hosting), which players see as
  "OpenH1 <rate> Tick", whose host picks the simulation's rate (network.sim_rate: 30,
  60 or 120, sim_rate.h). Its host advertises
  HALO_PORT_STANDARD_ADVERTISED_VERSION where a Legacy host advertises the
  network version, and the rate beside it (HALO_PORT_ADVERTISED_RATE_OFFSET,
  the internet listing's stamp), so a build that knows nothing of Standard
  games (OpenCE, an older OpenH1) takes it for a newer version and does
  not join. Its host takes a join only with a Standard game's join token
  (network_standard_join_token), so nothing else gets in either way.

The rate is agreed before the game's first tick and holds for its length:
the host reads network.sim_rate in the lobby, and takes the last it read as
the game starts (network_game_server_start_network_game sends the game
record then); the record carries it to every machine, a late joiner's
too, in a short the Xbox game left unused (struct network_game's
standard_rate, which a Legacy host sends as it always has). sim_rate.c's
sim_rate_network is the one place that turns it into the rate a tick
takes.

Mixed games (network.allow_opence; NETCODE.md, "OpenCE players in an
OpenH1 game"): a Standard game that OpenCE players join too. Its host
advertises the network version, as a Legacy host does, so that OpenCE joins
it, with the rate and the OpenH1 protocol version in reserved bytes OpenCE
never reads; an OpenH1 machine of that protocol joins it as a Standard game.
The host takes either join token, and so knows each machine's kind
(network_standard_machine_is_opence). Everything the host sends an OpenCE
machine is what a Legacy host sends: its game record without the rate
(network_standard_game_for_machine), and none of OpenH1's own messages
(network_standard_message_goes_to). Each player is stepped at their own
rate: an OpenCE player's biped, and the vehicle they drive, are of the macro
lane (objects.h) on every OpenH1 machine, so they move once a tick, as on
their own machine, while OpenH1 players move in the game's sub-steps. The
host tells its OpenH1 clients which players are OpenCE's
(_distributed_message_opence_players).
*/

#include "cseries.h"
#include "game/game.h"
#include "networking/network_game_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_server_manager_internal.h"
#include "sim_rate.h"
#include "network_standard.h"
#include "network_distributed.h"
#include "halo_port_limits.h"

#include <stdlib.h>

/* port/linux/src/port_config.c */
char const *config_string(char const *name);
long config_integer(char const *name);
int config_boolean(char const *name);

/* ---------- constants */

enum
{
	MAXIMUM_STANDARD_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	OPENCE_PLAYER_MASK_SIZE = (MAXIMUM_TRACKED_PLAYERS + 7) / 8,
	/* how often the host tells its OpenH1 clients which players are OpenCE's
	(and at once when that changes) */
	OPENCE_PLAYERS_INTERVAL_TICKS = TICKS_PER_SECOND,
	/* a join token's bytes, at most */
	MAXIMUM_JOIN_TOKEN_SIZE = 64,
};

/* ---------- structures */

/* on the wire, to an OpenH1 client: a bit for each player (by absolute
index) of an OpenCE machine */
struct distributed_opence_players
{
	struct distributed_message_header header;
	byte players[OPENCE_PLAYER_MASK_SIZE];
};

/* ---------- globals */

static struct
{
	/* hosting: Standard (network.game_mode as the server was made), and
	the rate as last read in the lobby */
	boolean hosting;
	long hosting_rate;
	/* ... and whether its players hold the action button to pick up a second
	weapon (game.auto_pickup false), as last read in the lobby, as the rate
	is */
	boolean hosting_hold_to_pick_up;
	/* ... OpenCE players may join it (network.allow_opence as the server
	was made, or as Host Controls' Allow OpenCE players has it since); and
	OpenCE players may be in it, having been let join since the server was
	made (stepped and sent to as a mixed game's: Host Controls' closing it
	keeps those in it) */
	boolean mixed;
	boolean mixed_since;
	/* joining: the game joined is Standard (its advertisement said so) */
	boolean joined;
	/* hosting: the token each machine joined with (_network_standard_join_*),
	by its index in the game */
	byte machine_kinds[MAXIMUM_STANDARD_MACHINES];
	/* every OpenH1 machine: whether each player (by absolute index) is an
	OpenCE machine's, taken as the tick opence_players_time began and held
	for it (a lane holds for a tick, whose sub-steps the frames between may
	spread: sim_rate.h): the host's from its machines' tokens, a client's
	from what its host last said (opence_players_heard) */
	boolean opence_players[MAXIMUM_TRACKED_PLAYERS];
	long opence_players_time;
	boolean opence_players_heard[MAXIMUM_TRACKED_PLAYERS];
	/* the host: the players last told, and when */
	byte opence_players_sent[OPENCE_PLAYER_MASK_SIZE];
	long opence_players_sent_time;
	/* a client that joined as OpenCE does (debug.join_legacy, or any Legacy
	join): what of OpenH1's own it heard, which must be nothing */
	long openh1_messages_heard;
	long record_rate_heard;
} network_standard_globals = { FALSE, SIM_RATE_CLASSIC, FALSE, FALSE };

/* ---------- private code */

/* a dedicated server's server.game_mode (docs/dedicated_server.md): the rate
of the OpenH1 game it names ("openh1_<rate>"), 0 for a Legacy game ("legacy",
its default, and anything else) */
static long network_standard_server_setting(
	void)
{
	char const *mode = config_string("server.game_mode");
	long rate;

	if (!mode || csstrncmp(mode, "openh1_", 7))
		return 0;
	rate = strtol(mode + 7, NULL, 10);
	return sim_rate_supported(rate) ? rate : 0;
}

/* network.sim_rate (a dedicated server's server.game_mode), a rate
sim_rate.c steps at, else 30 */
static long network_standard_rate_setting(
	void)
{
	long rate;

	if (halo_server_mode())
	{
		/* (a Legacy setting while an OpenH1 game is hosted, which it is until
		the server is empty: the rate it has) */
		rate = network_standard_server_setting();
		return rate ? rate : network_standard_globals.hosting_rate;
	}
	rate = config_integer("network.sim_rate");
	return sim_rate_supported(rate) ? rate : SIM_RATE_CLASSIC;
}

/* ---------- public code */

void network_standard_server_created(
	void)
{
	char const *mode = config_string("network.game_mode");

	/* (anything but "legacy" is the default, OpenH1's: "openh1", and
	"standard", its first name; a dedicated server's server.game_mode,
	"legacy" by default) */
	network_standard_globals.hosting = halo_server_mode() ? network_standard_server_setting() != 0 :
		!(mode && !csstrcmp(mode, "legacy"));
	network_standard_globals.hosting_rate = network_standard_rate_setting();
	network_standard_globals.hosting_hold_to_pick_up = !config_boolean("game.auto_pickup");
	network_standard_globals.mixed = network_standard_globals.hosting && config_boolean("network.allow_opence");
	network_standard_globals.mixed_since = network_standard_globals.mixed;
	csmemset(network_standard_globals.machine_kinds, 0, sizeof(network_standard_globals.machine_kinds));
}

void network_standard_server_mode_read(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	if (server && network_game_server_get_game(server)->machine_count <= 1)
		network_standard_server_created();
}

boolean network_standard_hosting(
	void)
{
	return network_standard_globals.hosting;
}

long network_standard_server_hosted(
	boolean *opence)
{
	/* (a dedicated server not hosting yet: what it will host, as its
	server.game_mode says) */
	if (halo_server_mode() && !global_network_game_server_get())
	{
		long rate = network_standard_server_setting();

		if (opence)
			*opence = !rate || config_boolean("network.allow_opence");
		return rate;
	}
	/* (OpenCE joins a Legacy game; a mixed game while it takes them) */
	if (opence)
		*opence = !network_standard_globals.hosting || network_standard_globals.mixed;
	return network_standard_globals.hosting ? network_standard_globals.hosting_rate : 0;
}

boolean network_standard_hosting_mixed(
	void)
{
	return network_standard_globals.hosting && (network_standard_globals.mixed || network_standard_globals.mixed_since);
}

boolean network_standard_opence_joins_open(
	void)
{
	return network_standard_globals.hosting && network_standard_globals.mixed;
}

void network_standard_set_opence_joins(
	boolean allowed)
{
	if (!network_standard_globals.hosting)
		return;
	/* (a game OpenCE's players did not join until now is mixed from now on:
	its machines all OpenH1's, each known by its token; one closed to them
	stays mixed for those in it) */
	network_standard_globals.mixed = allowed;
	if (allowed)
		network_standard_globals.mixed_since = TRUE;
}

void network_standard_advertise(
	byte *reserved,
	boolean in_lobby)
{
	/* (a mixed game's version is a Legacy game's, which OpenCE joins) */
	word version = network_standard_globals.hosting && !network_standard_globals.mixed ?
		HALO_PORT_STANDARD_ADVERTISED_VERSION : HALO_PORT_NETWORK_VERSION;

	reserved[HALO_PORT_ADVERTISED_VERSION_OFFSET] = (byte)(version & 0xFF);
	reserved[HALO_PORT_ADVERTISED_VERSION_OFFSET + 1] = (byte)(version >> 8);
	if (network_standard_globals.hosting)
		reserved[HALO_PORT_ADVERTISED_RATE_OFFSET] = (byte)network_standard_server_rate(in_lobby);
	if (network_standard_hosting_mixed())
		reserved[HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET] = (byte)HALO_PORT_STANDARD_NETWORK_VERSION;
}

short network_standard_join_kind(
	byte const *request_token,
	byte const *game_token,
	long token_size)
{
	byte standard_token[MAXIMUM_JOIN_TOKEN_SIZE];

	/* (a Legacy game's own token, as it always was) */
	if (!network_standard_globals.hosting)
		return csmemcmp(request_token, game_token, token_size) ? NONE : _network_standard_join_legacy;
	if (token_size <= 0 || token_size > MAXIMUM_JOIN_TOKEN_SIZE)
		return NONE;
	csmemcpy(standard_token, game_token, token_size);
	network_standard_join_token(standard_token, token_size);
	if (!csmemcmp(request_token, standard_token, token_size))
		return _network_standard_join_openh1;
	/* (a mixed game's: OpenCE's, the Legacy game's token) */
	if (network_standard_globals.mixed && !csmemcmp(request_token, game_token, token_size))
		return _network_standard_join_opence;
	return NONE;
}

void network_standard_machine_joined(
	long machine_index,
	short kind)
{
	if (machine_index >= 0 && machine_index < MAXIMUM_STANDARD_MACHINES)
	{
		network_standard_globals.machine_kinds[machine_index] =
			(byte)(kind == NONE ? _network_standard_join_legacy : kind);
	}
}

/* the token the machine joined this host with (_network_standard_join_*),
NONE when this machine hosts no Standard game (in the lobby too, before
the game's connection is the server's) */
static short network_standard_machine_kind(
	long machine_index)
{
	if (!network_standard_globals.hosting || !global_network_game_server_get() ||
		machine_index < 0 || machine_index >= MAXIMUM_STANDARD_MACHINES)
	{
		return NONE;
	}
	return network_standard_globals.machine_kinds[machine_index];
}

boolean network_standard_machine_is_opence(
	long machine_index)
{
	return network_standard_machine_kind(machine_index) == _network_standard_join_opence;
}

boolean network_standard_machine_is_openh1(
	long machine_index)
{
	return network_standard_machine_kind(machine_index) == _network_standard_join_openh1;
}

boolean network_standard_message_goes_to(
	long machine_index,
	byte type)
{
	return type < FIRST_OPENH1_DISTRIBUTED_MESSAGE || !network_standard_machine_is_opence(machine_index);
}

struct network_game const *network_standard_game_for_machine(
	struct network_game const *game,
	boolean opence)
{
	static struct network_game legacy_game;

	/* (an OpenCE machine's: as a Legacy host's, its rate unwritten) */
	if (!opence || !game)
		return game;
	csmemcpy(&legacy_game, game, sizeof(legacy_game));
	legacy_game.standard_rate = 0;
	SET_FLAG(legacy_game.cooperative_flags, _network_game_hold_to_pick_up_bit, FALSE);
	return &legacy_game;
}

/* (the host) which players are OpenCE machines', from its machines' tokens */
static void network_standard_note_opence_players(
	void)
{
	long machine_index;

	csmemset(network_standard_globals.opence_players, 0, sizeof(network_standard_globals.opence_players));
	for (machine_index = 0; machine_index < MAXIMUM_STANDARD_MACHINES; machine_index++)
	{
		long *player_list;
		short local_player_index;

		if (network_standard_globals.machine_kinds[machine_index] != _network_standard_join_opence)
			continue;
		player_list = machine_get_player_list(machine_index);
		for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		{
			long absolute_index = player_list[local_player_index] != NONE ?
				DATUM_INDEX_TO_ABSOLUTE_INDEX(player_list[local_player_index]) : NONE;

			if (absolute_index >= 0 && absolute_index < MAXIMUM_TRACKED_PLAYERS)
				network_standard_globals.opence_players[absolute_index] = TRUE;
		}
	}
}

boolean network_standard_player_is_opence(
	long player_index)
{
	long absolute_index = player_index != NONE ? DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;

	if (absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED_PLAYERS)
		return FALSE;
	switch (game_connection())
	{
	case _game_connection_network_server:
		if (!network_standard_hosting_mixed())
			return FALSE;
		break;
	case _game_connection_network_client:
		if (!network_standard_globals.joined)
			return FALSE;
		break;
	default:
		return FALSE;
	}
	/* (taken as a tick begins, and held for it) */
	if (network_standard_globals.opence_players_time != game_time_get())
	{
		if (game_connection() == _game_connection_network_server)
			network_standard_note_opence_players();
		else
		{
			csmemcpy(network_standard_globals.opence_players, network_standard_globals.opence_players_heard,
				sizeof(network_standard_globals.opence_players));
		}
		network_standard_globals.opence_players_time = game_time_get();
	}
	return network_standard_globals.opence_players[absolute_index];
}

void network_standard_new_game(
	void)
{
	csmemset(network_standard_globals.opence_players, 0, sizeof(network_standard_globals.opence_players));
	network_standard_globals.opence_players_time = NONE;
	csmemset(network_standard_globals.opence_players_heard, 0, sizeof(network_standard_globals.opence_players_heard));
	csmemset(network_standard_globals.opence_players_sent, 0, sizeof(network_standard_globals.opence_players_sent));
	network_standard_globals.opence_players_sent_time = NONE;
}

void network_standard_host_tick(
	void)
{
	struct distributed_opence_players message;
	long machine_indices[MAXIMUM_STANDARD_MACHINES];
	short machine_count;
	short index;
	short player_index;

	if (!network_standard_hosting_mixed())
		return;
	csmemset(&message, 0, sizeof(message));
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);

		if (player && network_standard_player_is_opence(DATUM_INDEX_NEW(player_index, player->identifier)))
			message.players[player_index / 8] |= (byte)FLAG(player_index % 8);
	}
	/* (when they change, and each so often, as a datagram may be lost) */
	if (network_standard_globals.opence_players_sent_time != NONE &&
		!csmemcmp(message.players, network_standard_globals.opence_players_sent, sizeof(message.players)) &&
		game_time_get() - network_standard_globals.opence_players_sent_time < OPENCE_PLAYERS_INTERVAL_TICKS)
	{
		return;
	}
	csmemcpy(network_standard_globals.opence_players_sent, message.players, sizeof(message.players));
	network_standard_globals.opence_players_sent_time = game_time_get();
	machine_count = distributed_client_machines(machine_indices, MAXIMUM_STANDARD_MACHINES);
	for (index = 0; index < machine_count; index++)
	{
		/* (to the OpenH1 machines: an OpenCE machine is sent none of
		OpenH1's messages, network_standard_message_goes_to) */
		if (network_standard_machine_is_openh1(machine_indices[index]))
		{
			distributed_send_to_machine(machine_indices[index], &message, _distributed_message_opence_players, 1,
				sizeof(message));
		}
	}
}

boolean network_standard_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_opence_players;
}

void network_standard_handle_message(
	long machine_index,
	word const *message,
	word size)
{
	struct distributed_opence_players received;
	short player_index;

	/* (a client's of an OpenH1 game, from its host) */
	if (machine_index != NONE || game_connection() != _game_connection_network_client ||
		!network_standard_globals.joined || size != sizeof(received) || !game_in_progress())
	{
		return;
	}
	csmemcpy(&received, message, sizeof(received));
	/* (taken by the next tick to begin: network_standard_player_is_opence) */
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		network_standard_globals.opence_players_heard[player_index] =
			TEST_FLAG(received.players[player_index / 8], player_index % 8) != 0;
	}
}

void network_standard_message_heard(
	byte type)
{
	if (game_connection() == _game_connection_network_client && !network_standard_globals.joined &&
		type >= FIRST_OPENH1_DISTRIBUTED_MESSAGE)
	{
		network_standard_globals.openh1_messages_heard++;
	}
}

void network_standard_legacy_heard(
	long *openh1_messages,
	long *record_rate)
{
	struct network_game *game = network_game_get_game();

	/* (a record's rate is a Standard host's word to its OpenH1 machines) */
	if (game_connection() == _game_connection_network_client && !network_standard_globals.joined && game &&
		game->standard_rate)
	{
		network_standard_globals.record_rate_heard = game->standard_rate;
	}
	*openh1_messages = network_standard_globals.openh1_messages_heard;
	*record_rate = network_standard_globals.record_rate_heard;
}

long network_standard_server_rate(
	boolean in_lobby)
{
	if (!network_standard_globals.hosting)
		return 0;
	if (in_lobby)
		network_standard_globals.hosting_rate = network_standard_rate_setting();
	return network_standard_globals.hosting_rate;
}

void network_standard_stamp_game(
	struct network_game *game,
	boolean in_lobby)
{
	/* (a Legacy game's 0, as the server made it: written too, so that a
	dedicated server's switch from an OpenH1 game to a Legacy one, whose
	record lasts, leaves no rate in it) */
	if (game)
		game->standard_rate = network_standard_globals.hosting ? (short)network_standard_server_rate(in_lobby) : 0;
	/* (and how its players pick up a second weapon, read as the rate is) */
	if (in_lobby)
		network_standard_globals.hosting_hold_to_pick_up = !config_boolean("game.auto_pickup");
	if (game)
	{
		SET_FLAG(game->cooperative_flags, _network_game_hold_to_pick_up_bit,
			network_standard_globals.hosting && network_standard_globals.hosting_hold_to_pick_up);
	}
}

boolean network_standard_walk_over_pickup(
	long player_index)
{
	struct network_game *game;

	switch (game_connection())
	{
	case _game_connection_network_server:
		/* (a Legacy game's, and an OpenCE machine's player's, OpenCE's way:
		their machines walk over weapons, and hear nothing of the host's) */
		if (!network_standard_globals.hosting || network_standard_player_is_opence(player_index))
			return TRUE;
		return !network_standard_globals.hosting_hold_to_pick_up;
	case _game_connection_network_client:
		/* (the host's word, as its record brought it; nothing from a Legacy
		host, OpenCE's way) */
		game = network_game_get_game();
		if (!network_standard_globals.joined || !game || network_standard_player_is_opence(player_index))
			return TRUE;
		return !TEST_FLAG(game->cooperative_flags, _network_game_hold_to_pick_up_bit);
	default:
		return config_boolean("game.auto_pickup") != 0;
	}
}

void network_standard_join_token(
	byte *join_token,
	long token_size)
{
	/* (its last two bytes: "S" and the protocol's version) */
	if (join_token && token_size >= 2)
	{
		join_token[token_size - 1] ^= 'S';
		join_token[token_size - 2] ^= (byte)HALO_PORT_STANDARD_NETWORK_VERSION;
	}
}

void network_standard_client_joining(
	boolean standard,
	byte *join_token,
	long token_size)
{
	network_standard_globals.joined = standard;
	if (standard)
		network_standard_join_token(join_token, token_size);
}

long network_standard_agreed_rate(
	void)
{
	switch (game_connection())
	{
	case _game_connection_network_server:
		return network_standard_globals.hosting ? network_standard_globals.hosting_rate : 0;
	case _game_connection_network_client:
	{
		struct network_game *game = network_game_get_game();

		/* (the host's word: a rate this machine steps at, else 30) */
		if (!network_standard_globals.joined || !game)
			return 0;
		return sim_rate_supported(game->standard_rate) ? game->standard_rate : SIM_RATE_CLASSIC;
	}
	default:
		return 0;
	}
}
