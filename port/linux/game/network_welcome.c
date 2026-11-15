/*
NETWORK_WELCOME.C

The host's welcome to OpenCE players (port/linux/NETCODE.md, "OpenH1 and
Legacy games"): two lines of the text chat that say this server runs
OpenH1, and where to get it.

Each line is a notice of the text chat (network_chat.c's
_chat_line_notice_bit in _distributed_message_chat_down), which OpenCE's
own chat shows as it is, from no player, so nothing new goes between the
machines. (OpenCE's client takes no line at all, notices too, in a public
game it joined from the server browser.) A Legacy game is OpenCE's in every
byte, so its host cannot tell an OpenCE machine from an OpenH1 one: it
welcomes every client machine, in any kind of game, and an OpenH1 machine
does not show the lines (network_welcome_line). Only OpenCE players see
them (and OpenH1 builds older than the welcome). An OpenH1 game's host
knows its OpenH1 machines by the token they joined with
(network_standard_machine_is_openh1), and sends them none.

A machine is welcomed once it has loaded the game (its client ready, which
it sends from its first tick: a line sent while it loads is let go as its
game starts), WELCOME_DELAY_MILLISECONDS later, so that its player sees
the lines in the game. Each player is welcomed once while this host runs,
by their machine's real address and hardware id: not again in the next
game, nor joining again. network.opence_welcome turns it off.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "network_distributed.h"
#include "network_chat.h"
#include "network_standard.h"

/* port_config.c's, cseries_windows.c's, network_server_manager.c's, p2p.c's */
int config_boolean(const char *name);
unsigned long system_milliseconds(void);
char const *network_game_server_machine_hardware_id(long machine_index);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* ---------- constants */

enum
{
	/* from a machine's client ready to its welcome */
	WELCOME_DELAY_MILLISECONDS = 3000,
	/* the players welcomed, remembered: the oldest forgotten first */
	MAXIMUM_WELCOMED_PLAYERS = 64,
	/* p2p.h's P2P_HARDWARE_ID_SIZE */
	WELCOME_HARDWARE_ID_SIZE = 33,
	MAXIMUM_WELCOME_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
};

/* the lines, each a notice of the text chat: no longer than its line
(network_chat.c's CHAT_TEXT_SIZE, 63 characters) */
static char const *const welcome_lines[] =
{
	"This server runs GOAT H1.",
	"Get it free at goath1.com for smoother 60 and 120 tick games.",
};

/* ---------- structures */

/* a player, as the host knows their machine */
struct welcome_player
{
	unsigned long address;
	char hardware_id[WELCOME_HARDWARE_ID_SIZE];
};

/* ---------- globals */

static struct
{
	/* the players welcomed, and the oldest's place once all are held */
	struct welcome_player welcomed[MAXIMUM_WELCOMED_PLAYERS];
	short welcomed_count;
	short oldest;
	/* each machine to welcome: who it was as it loaded, and when
	(system_milliseconds) */
	boolean pending[MAXIMUM_WELCOME_MACHINES];
	struct welcome_player pending_players[MAXIMUM_WELCOME_MACHINES];
	unsigned long loaded_at[MAXIMUM_WELCOME_MACHINES];
} welcome;

/* ---------- the rules (pure: tools/harness/tests/welcome.c) */

/* whether two players are one: their machines' address and hardware id
both the same */
static boolean welcome_same_player(
	struct welcome_player const *a,
	struct welcome_player const *b)
{
	return a->address == b->address && !csstrcmp(a->hardware_id, b->hardware_id);
}

/* whether a player is among those welcomed */
static boolean welcome_player_welcomed(
	struct welcome_player const *players,
	short count,
	struct welcome_player const *player)
{
	short index;

	for (index = 0; index < count; index++)
	{
		if (welcome_same_player(&players[index], player))
			return TRUE;
	}
	return FALSE;
}

/* a player remembered as welcomed: in the next place, or the oldest's once
every place is held */
static void welcome_player_remember(
	struct welcome_player *players,
	short *count,
	short *oldest,
	short maximum,
	struct welcome_player const *player)
{
	if (*count < maximum)
	{
		players[(*count)++] = *player;
		return;
	}
	players[*oldest] = *player;
	*oldest = (short)((*oldest + 1) % maximum);
}

/* whether a machine that loaded at loaded_at is due its welcome (the clock
wraps) */
static boolean welcome_due(
	unsigned long loaded_at,
	unsigned long now)
{
	return now - loaded_at >= WELCOME_DELAY_MILLISECONDS;
}

/* ---------- private code */

/* (the host) the player of a client machine */
static void welcome_machine_player(
	long machine_index,
	struct welcome_player *player)
{
	csmemset(player, 0, sizeof(*player));
	player->address = distributed_machine_real_address(machine_index);
	p2p_hardware_id_sanitize(player->hardware_id, WELCOME_HARDWARE_ID_SIZE,
		network_game_server_machine_hardware_id(machine_index));
}

/* ---------- public code */

void network_welcome_new_game(
	void)
{
	/* (a machine is welcomed in the game it loaded: the next it loads again) */
	csmemset(welcome.pending, 0, sizeof(welcome.pending));
}

void network_welcome_machine_loaded(
	long machine_index)
{
	/* (nor a machine that joined with an OpenH1 game's token: an OpenH1
	machine, network_standard.c) */
	if (game_connection() != _game_connection_network_server ||
		machine_index < 0 || machine_index >= MAXIMUM_WELCOME_MACHINES ||
		network_standard_machine_is_openh1(machine_index))
	{
		return;
	}
	welcome.pending[machine_index] = TRUE;
	welcome_machine_player(machine_index, &welcome.pending_players[machine_index]);
	welcome.loaded_at[machine_index] = system_milliseconds();
}

void network_welcome_host_tick(
	void)
{
	long machine_indices[MAXIMUM_WELCOME_MACHINES];
	short count = distributed_client_machines(machine_indices, MAXIMUM_WELCOME_MACHINES);
	unsigned long now = system_milliseconds();
	short index;

	for (index = 0; index < count; index++)
	{
		long machine_index = machine_indices[index];
		struct welcome_player player;
		short line;

		if (machine_index < 0 || machine_index >= MAXIMUM_WELCOME_MACHINES || !welcome.pending[machine_index] ||
			!welcome_due(welcome.loaded_at[machine_index], now))
		{
			continue;
		}
		welcome.pending[machine_index] = FALSE;
		/* (the machine that loaded, not one new at its index since; and a
		player not yet welcomed) */
		welcome_machine_player(machine_index, &player);
		if (!welcome_same_player(&player, &welcome.pending_players[machine_index]) ||
			welcome_player_welcomed(welcome.welcomed, welcome.welcomed_count, &player) ||
			!config_boolean("network.opence_welcome"))
		{
			continue;
		}
		welcome_player_remember(welcome.welcomed, &welcome.welcomed_count, &welcome.oldest,
			MAXIMUM_WELCOMED_PLAYERS, &player);
		for (line = 0; line < NUMBEROF(welcome_lines); line++)
			network_chat_host_notice(machine_index, welcome_lines[line]);
		error(_error_log, "welcome: machine %ld welcomed", machine_index);
	}
}

boolean network_welcome_line(
	char const *text)
{
	short line;

	for (line = 0; line < NUMBEROF(welcome_lines); line++)
	{
		if (!csstrcmp(text, welcome_lines[line]))
			return TRUE;
	}
	return FALSE;
}
