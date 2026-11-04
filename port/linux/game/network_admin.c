/*
NETWORK_ADMIN.C

A dedicated server's admins in the game (network_admin.h;
docs/dedicated_server.md, "Admins in the game"; port/linux/NETCODE.md). A
player who plays on a dedicated server whose owner made their admin key an
admin (admins.txt, server.admins, sv_admin, the web page) gets the pause
menu's and the scoreboard's HOST CONTROLS, as a player who hosts has them:
its screens show the server's game, and what the admin does there is a
request the server runs as its own command (server_commands.c), at the
admin role, as the web page's and the RCON's are. Owner's commands (the
server's settings and passwords, stopping it) are never asked.

- The key is proven, never claimed: the server sends each OpenH1 machine of
  its game its id (a random one it keeps, admin_id.txt) and a nonce of its
  own (_network_admin_down_challenge), and the machine signs
  "openh1-admin-proof-1", the nonce and its connection's binding (the
  internet play tunnel's session secret, as a Discord pass's proof is bound:
  player_link.c) with a key made for that server alone from its admin
  secret (admin.key, made as the first server asks): no two servers see one
  player's same key, and none sees the admin key. The server checks the
  signature; a machine that proved its key is an admin while the key (or its
  id) is in admins.txt or server.admins.
- The server tells each admin's machine the game once a second
  (_network_admin_down_state, then its players), and the lists (maps,
  gametypes, the rotation, the bans) when it asks; and the answer to each
  request, which the menu shows.
- A request (_network_admin_up_request) is a verb, a player's slot with
  their name as the admin saw it (refused if the slot holds another player
  now, as the web's are), a number and a text, every field checked
  (network_admin_request_line), from the machine's own stream, a few a second
  at most (network_admin_rate_take). Its command is logged and is an event
  with the admin's name and key id; the players see what the server's own
  commands show them.

Both messages are OpenH1's own (_distributed_message_admin_down and _up:
protocol version 15), sent to and taken from OpenH1 machines of a dedicated
server's game alone. A player's own host never sends them: its Host
Controls are its own (network_host_controls.c). The pure parts are
tools/harness/tests/test_network_admin.py's.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_client_manager.h"
#include "bungie_net/network/transport.h"
#include "networking/network_connection.h"
#include "networking/network_server_manager.h"
#include "text/unicode.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "network_host_controls.h"
#include "network_host_game.h"
#include "network_link.h"
#include "network_chat.h"
#include "network_admin.h"
#include "server_director.h"
#include "server_gametypes.h"

#include "../src/player_link.h" /* port: port/linux/src/player_link.c */
#include "../src/server_console.h" /* port: port/linux/src/server_console.c */
#include "../src/server_events.h" /* port: port/linux/src/server_events.c */
#include "../src/server_json.h" /* port: port/linux/src/server_json.c */
#include "../src/server_log.h" /* port: port/linux/src/server_log.c */
#include "../src/posix_server.h" /* port: port/linux/src/posix_server.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the netcode's (network_game_globals.c, network_server_message_handler.c,
network_messages.c), the platform's, port_config.c's, server_commands.c's */
boolean network_distributed_client_send_reliably(void *message, word size);
boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, word size);
short network_distributed_server_machines(long *machine_indices, short maximum);
struct network_game *network_game_server_get_game(struct network_game_server *server);
unsigned long system_milliseconds(void);
void posix_random_bytes(void *buffer, unsigned int size);
void platform_log(char const *format, ...);
const char *config_string(char const *name);
void platform_translate_path(char const *xbox_path, char *host_path, unsigned long host_path_size);
void server_commands_set_actor(char const *actor);
long server_commands_bans_read(void);
char const *server_commands_ban_line(long index);
void network_host_game_gametype_text(char const *name, wchar_t *text, short size);
void console_printf(boolean clear, const char *format, ...);
void console_warning(const char *format, ...);

/* ---------- constants */

enum
{
	MAXIMUM_ADMIN_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	MAXIMUM_ADMIN_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	ADMIN_FORMAT = 1,
	/* the admins told the game this often, and admins.txt read again */
	ADMIN_STATE_INTERVAL = 1000,
	/* an admin's machine forgets the server's word it has not heard again
	this long */
	ADMIN_STATE_TIMEOUT = 3500,
	/* the players in a message, at most; a list's text in one */
	ADMIN_PLAYERS_PER_PAGE = 32,
	ADMIN_LINES_TEXT_SIZE = 3000,
	ADMIN_KEY_HEX_SIZE = 2 * PLAYER_LINK_KEY_SIZE + 1,
	/* admins.txt, at most */
	ADMIN_FILE_SIZE = 16384,
	MAXIMUM_ADMIN_BANS = 128,
	ADMIN_BAN_LINE_SIZE = 512,
};

#define ADMINS_FILE "d:\\admins.txt"
#define ADMIN_ID_FILE "d:\\admin_id.txt"

/* the messages' kinds */
enum
{
	_network_admin_down_challenge,
	_network_admin_down_state,
	_network_admin_down_players,
	_network_admin_down_lines,
	_network_admin_down_result,
	NUMBER_OF_NETWORK_ADMIN_DOWN_KINDS
};
enum
{
	_network_admin_up_proof,
	_network_admin_up_request,
	NUMBER_OF_NETWORK_ADMIN_UP_KINDS
};

#define NONE_BYTE 0xFF

/* ---------- structures (on the wire, after the message's header) */

struct admin_wire_challenge
{
	byte format;
	byte kind;
	byte pad[2];
	/* the server's id, which the machine's key for it is made from */
	byte server_id[PLAYER_LINK_SERVER_ID_SIZE];
	byte nonce[PLAYER_LINK_NONCE_SIZE];
};

struct admin_wire_proof
{
	byte format;
	byte kind;
	byte pad[2];
	byte key[PLAYER_LINK_KEY_SIZE];
	byte signature[PLAYER_LINK_SIGNATURE_SIZE];
};

struct admin_wire_request
{
	byte format;
	byte kind;
	byte verb;
	/* NONE_BYTE: none */
	byte slot;
	word id;
	word pad;
	long number;
	char name[NETWORK_ADMIN_NAME_SIZE];
	char text[NETWORK_ADMIN_TEXT_SIZE];
};

struct admin_wire_state
{
	byte format;
	byte kind;
	word flags;
	short rate;
	short rotation_index;
	char current[NETWORK_ADMIN_ENTRY_SIZE];
	char next[NETWORK_ADMIN_ENTRY_SIZE];
};

struct admin_wire_player
{
	byte slot;
	/* the player's absolute index in the game played (NONE_BYTE: none, the
	lobby's) */
	byte absolute_index;
	/* NONE_BYTE: no team */
	byte team;
	byte tag;
	byte actions;
	byte muted;
	short ping;
	char name[NETWORK_ADMIN_NAME_SIZE];
};

struct admin_wire_players
{
	byte format;
	byte kind;
	byte first;
	byte count;
	byte total;
	byte pad[3];
	struct admin_wire_player players[ADMIN_PLAYERS_PER_PAGE];
};

struct admin_wire_lines
{
	byte format;
	byte kind;
	byte list;
	byte pad;
	word first;
	word count;
	word total;
	word size;
	/* count entries, each ended, their fields separated by tabs */
	char text[ADMIN_LINES_TEXT_SIZE];
};

struct admin_wire_result
{
	byte format;
	byte kind;
	byte ok;
	byte pad;
	word id;
	word pad2;
	char text[NETWORK_ADMIN_RESULT_SIZE];
};

typedef char admin_wire_challenge_size_assert[sizeof(struct admin_wire_challenge) == 68 ? 1 : -1];
typedef char admin_wire_proof_size_assert[sizeof(struct admin_wire_proof) == 100 ? 1 : -1];
typedef char admin_wire_request_size_assert[sizeof(struct admin_wire_request) == 156 ? 1 : -1];
typedef char admin_wire_state_size_assert[sizeof(struct admin_wire_state) == 264 ? 1 : -1];
typedef char admin_wire_player_size_assert[sizeof(struct admin_wire_player) == 24 ? 1 : -1];
typedef char admin_wire_players_size_assert[sizeof(struct admin_wire_players) == 776 ? 1 : -1];
typedef char admin_wire_lines_size_assert[sizeof(struct admin_wire_lines) == 3012 ? 1 : -1];
typedef char admin_wire_result_size_assert[sizeof(struct admin_wire_result) == 168 ? 1 : -1];
typedef char admin_slots_assert[NETWORK_ADMIN_MAXIMUM_SLOTS == HALO_PORT_MAXIMUM_NETWORK_PLAYERS ? 1 : -1];
typedef char admin_ban_line_size_assert[ADMIN_BAN_LINE_SIZE == HOST_CONTROLS_BAN_LINE_SIZE ? 1 : -1];

/* ---------- the pure parts */

static boolean admin_hex_digit(
	char character)
{
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
		(character >= 'A' && character <= 'F');
}

static char admin_lower(
	char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

/* a word's character that is no hex digit */
static boolean admin_word_character(
	char character)
{
	return (character >= 'g' && character <= 'z') || (character >= 'G' && character <= 'Z') || character == '_';
}

boolean network_admin_listed(
	char const *key_hex,
	char const *key_id,
	char const *list)
{
	char const *cursor = list;

	if (!list || !key_hex || !key_id || strlen(key_hex) != 64 || strlen(key_id) != 16)
		return FALSE;
	while (*cursor)
	{
		long length = 0;
		long index;
		char const *match;

		if (*cursor == '#')
		{
			while (*cursor && *cursor != '\n')
				cursor++;
			continue;
		}
		if (!admin_hex_digit(*cursor))
		{
			cursor++;
			continue;
		}
		while (admin_hex_digit(cursor[length]))
			length++;
		/* (a run of hex digits alone: a key's 64, an id's 16; another word's
		digits, or a longer run, name no key) */
		match = length == 64 ? key_hex : length == 16 ? key_id : NULL;
		if (match && (cursor == list || !admin_word_character(cursor[-1])) && !admin_word_character(cursor[length]))
		{
			for (index = 0; index < length && admin_lower(cursor[index]) == admin_lower(match[index]); index++)
			{
			}
			if (index == length)
				return TRUE;
		}
		cursor += length;
	}
	return FALSE;
}

/* a text kept to printable ASCII, its quotes dropped (a word's: no spaces
either, FALSE if it had one); FALSE if nothing is left */
static boolean admin_text_clean(
	char *destination,
	long size,
	char const *source,
	long source_size,
	boolean word)
{
	long length = 0, index;

	for (index = 0; index < source_size && source[index] && length < size - 1; index++)
	{
		unsigned char character = (unsigned char)source[index];

		if (character < 0x20 || character >= 0x7F || character == '"')
			continue;
		if (word && character == ' ')
			return FALSE;
		destination[length++] = (char)character;
	}
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	destination[length] = 0;
	while (destination[0] == ' ')
		memmove(destination, destination + 1, (size_t)length--);
	return length > 0;
}

boolean network_admin_request_line(
	struct network_admin_request const *request,
	char const *slot_name,
	char const *current_entry,
	char *line,
	long size,
	char *why,
	long why_size)
{
	char text[NETWORK_ADMIN_TEXT_SIZE];
	char name[NETWORK_ADMIN_NAME_SIZE];
	boolean player = request->verb == _network_admin_kick || request->verb == _network_admin_ban ||
		request->verb == _network_admin_mute || request->verb == _network_admin_unmute ||
		request->verb == _network_admin_move_team;

	line[0] = 0;
	why[0] = 0;
	if (request->verb < 0 || request->verb >= NUMBER_OF_NETWORK_ADMIN_REQUESTS ||
		request->verb == _network_admin_lists)
	{
		snprintf(why, (size_t)why_size, "not a request of an admin's");
		return FALSE;
	}
	/* (a player's: their slot, holding the player the admin saw) */
	if (player)
	{
		admin_text_clean(name, sizeof(name), request->name, sizeof(request->name), FALSE);
		if (request->slot < 0 || request->slot >= NETWORK_ADMIN_MAXIMUM_SLOTS || !slot_name || !slot_name[0])
		{
			snprintf(why, (size_t)why_size, "that player has left");
			return FALSE;
		}
		if (strcmp(name, slot_name))
		{
			snprintf(why, (size_t)why_size, "that player has left (%s is in the slot now)", slot_name);
			return FALSE;
		}
	}
	switch (request->verb)
	{
	case _network_admin_kick:
		snprintf(line, (size_t)size, "kick %d", request->slot);
		break;
	case _network_admin_ban:
		if (request->number < 0 || request->number > NETWORK_ADMIN_BAN_MOST_SECONDS)
		{
			snprintf(why, (size_t)why_size, "a ban's length is ten years at most");
			return FALSE;
		}
		if (!admin_text_clean(text, sizeof(text), request->text, sizeof(request->text), FALSE))
			snprintf(text, sizeof(text), "banned by an admin");
		if (request->number)
			snprintf(line, (size_t)size, "ban %d %lds %s", request->slot, request->number, text);
		else
			snprintf(line, (size_t)size, "ban %d forever %s", request->slot, text);
		break;
	case _network_admin_mute:
		if (request->number < 1 || request->number > NETWORK_ADMIN_MUTE_MOST_MINUTES)
		{
			snprintf(why, (size_t)why_size, "a mute is a minute to a day");
			return FALSE;
		}
		snprintf(line, (size_t)size, "mute %d %ld", request->slot, request->number);
		break;
	case _network_admin_unmute:
		snprintf(line, (size_t)size, "unmute %d", request->slot);
		break;
	case _network_admin_move_team:
		snprintf(line, (size_t)size, "sv_team %d", request->slot);
		break;
	case _network_admin_end_game:
		snprintf(line, (size_t)size, "sv_end_game");
		break;
	case _network_admin_restart:
		/* (the game played, again: sv_map of its own entry) */
		if (!current_entry || !admin_text_clean(text, sizeof(text), current_entry, NETWORK_ADMIN_TEXT_SIZE, TRUE))
		{
			snprintf(why, (size_t)why_size, "no game is being played");
			return FALSE;
		}
		snprintf(line, (size_t)size, "sv_map %s", text);
		break;
	case _network_admin_shuffle_teams:
		snprintf(line, (size_t)size, "sv_teams shuffle");
		break;
	case _network_admin_balance_teams:
		snprintf(line, (size_t)size, "sv_teams balance");
		break;
	case _network_admin_lock_teams:
		if (request->number != 0 && request->number != 1)
		{
			snprintf(why, (size_t)why_size, "on or off");
			return FALSE;
		}
		snprintf(line, (size_t)size, "sv_lock_teams %ld", request->number);
		break;
	case _network_admin_announce:
		if (!admin_text_clean(text, sizeof(text), request->text, sizeof(request->text), FALSE))
		{
			snprintf(why, (size_t)why_size, "give the words");
			return FALSE;
		}
		snprintf(line, (size_t)size, "say %s", text);
		break;
	case _network_admin_map_now:
	case _network_admin_map_after:
	case _network_admin_rotation_add:
		/* (an entry of the rotation's grammar, one word: the commands check
		the rest) */
		if (!admin_text_clean(text, sizeof(text), request->text, sizeof(request->text), TRUE))
		{
			snprintf(why, (size_t)why_size, "give a map, and a gametype");
			return FALSE;
		}
		snprintf(line, (size_t)size, "%s %s", request->verb == _network_admin_map_now ? "sv_map" :
			request->verb == _network_admin_map_after ? "sv_map_after" : "sv_rotation_add", text);
		break;
	case _network_admin_rotation_remove:
		if (request->number < 0 || request->number >= NETWORK_ADMIN_ROTATION_MOST)
		{
			snprintf(why, (size_t)why_size, "no such entry of the rotation");
			return FALSE;
		}
		snprintf(line, (size_t)size, "sv_rotation_del %ld", request->number);
		break;
	case _network_admin_unban:
		if (request->number < 0 || request->number >= NETWORK_ADMIN_BAN_MOST)
		{
			snprintf(why, (size_t)why_size, "no such ban");
			return FALSE;
		}
		snprintf(line, (size_t)size, "unban %ld", request->number);
		break;
	default:
		snprintf(why, (size_t)why_size, "not a request of an admin's");
		return FALSE;
	}
	return TRUE;
}

boolean network_admin_rate_take(
	struct network_admin_rate *rate,
	unsigned long now)
{
	unsigned long interval = 1000 / NETWORK_ADMIN_REQUESTS_PER_SECOND;

	/* (a token each interval, up to the burst; the first request finds them
	all) */
	if (!rate->time)
	{
		rate->tokens = NETWORK_ADMIN_REQUEST_BURST;
		rate->time = now;
	}
	else if (now - rate->time >= interval)
	{
		unsigned long earned = (now - rate->time) / interval;

		if (earned >= (unsigned long)(NETWORK_ADMIN_REQUEST_BURST - rate->tokens))
		{
			rate->tokens = NETWORK_ADMIN_REQUEST_BURST;
			rate->time = now;
		}
		else
		{
			rate->tokens += (long)earned;
			rate->time += earned * interval;
		}
	}
	if (!rate->time)
		rate->time = 1;
	if (rate->tokens <= 0)
		return FALSE;
	rate->tokens--;
	return TRUE;
}

/* ---------- the messages' header */

static void admin_fill_header(
	struct distributed_message_header *header,
	byte type,
	word size)
{
	header->header = 0;
	build_message_header(&header->header, size, 2, 0);
	header->type = type;
	header->count = 1;
	header->game_time = game_in_progress() ? game_time_get() : 0;
}

/* ---------- the server */

#if HALO_SERVER_BUILD

static struct
{
	boolean present;
	boolean challenged;
	byte nonce[PLAYER_LINK_NONCE_SIZE];
	boolean proven;
	byte key[PLAYER_LINK_KEY_SIZE];
	char key_hex[ADMIN_KEY_HEX_SIZE];
	char key_id[NETWORK_ADMIN_KEY_ID_TEXT_SIZE];
	boolean admin;
	/* told it is one (FALSE: told once that it no longer is) */
	boolean told;
	boolean lists_asked;
	struct network_admin_rate rate;
} admin_machines[MAXIMUM_ADMIN_MACHINES];

static struct
{
	unsigned long told_at;
	/* admins.txt as last read */
	char file[ADMIN_FILE_SIZE];
	/* the server's id (admin_id.txt), read or made once */
	boolean has_id;
	byte id[PLAYER_LINK_SERVER_ID_SIZE];
} admin_server;

/* the server's id: admin_id.txt's 64 hex digits, made at random the first
time, so that each player's key for the server is the same from run to run */
static void admin_server_id(
	void)
{
	FILE *file;
	char text[2 * PLAYER_LINK_SERVER_ID_SIZE + 8] = "";
	short index;

	if (admin_server.has_id)
		return;
	if ((file = fopen(ADMIN_ID_FILE, "r")) != NULL)
	{
		if (!fgets(text, sizeof(text), file))
			text[0] = 0;
		fclose(file);
	}
	for (index = 0; index < PLAYER_LINK_SERVER_ID_SIZE; index++)
	{
		char pair[3] = { text[2 * index], text[2 * index + 1], 0 };

		if (!admin_hex_digit(pair[0]) || !admin_hex_digit(pair[1]))
			break;
		admin_server.id[index] = (byte)strtoul(pair, NULL, 16);
	}
	if (index < PLAYER_LINK_SERVER_ID_SIZE)
	{
		static char const digits[] = "0123456789abcdef";
		char path[1024];

		posix_random_bytes(admin_server.id, sizeof(admin_server.id));
		for (index = 0; index < PLAYER_LINK_SERVER_ID_SIZE; index++)
		{
			text[2 * index] = digits[admin_server.id[index] >> 4];
			text[2 * index + 1] = digits[admin_server.id[index] & 15];
		}
		text[2 * PLAYER_LINK_SERVER_ID_SIZE] = '\n';
		text[2 * PLAYER_LINK_SERVER_ID_SIZE + 1] = 0;
		platform_translate_path(ADMIN_ID_FILE, path, sizeof(path));
		if (!posix_server_write_file(path, text, (unsigned long)strlen(text)))
			server_log(_server_log_warning, "server", "admin_id.txt cannot be written: admins' keys change each run");
	}
	admin_server.has_id = TRUE;
}

void network_admin_machine_joined(
	long machine_index)
{
	if (machine_index >= 0 && machine_index < MAXIMUM_ADMIN_MACHINES)
		csmemset(&admin_machines[machine_index], 0, sizeof(admin_machines[machine_index]));
}

static void admin_file_path(
	char *path,
	unsigned long size)
{
	platform_translate_path(ADMINS_FILE, path, size);
}

static void admin_file_read(
	void)
{
	FILE *file = fopen(ADMINS_FILE, "r");
	size_t length = 0;

	admin_server.file[0] = 0;
	if (!file)
		return;
	length = fread(admin_server.file, 1, sizeof(admin_server.file) - 1, file);
	admin_server.file[length] = 0;
	fclose(file);
}

static boolean admin_key_listed(
	char const *key_hex,
	char const *key_id)
{
	return network_admin_listed(key_hex, key_id, admin_server.file) ||
		network_admin_listed(key_hex, key_id, config_string("server.admins"));
}

boolean network_admin_machine_key(
	long machine_index,
	char *key_hex,
	char *key_id,
	boolean *admin)
{
	if (machine_index < 0 || machine_index >= MAXIMUM_ADMIN_MACHINES || !admin_machines[machine_index].proven)
		return FALSE;
	if (key_hex)
		csstrcpy(key_hex, admin_machines[machine_index].key_hex);
	if (key_id)
		csstrcpy(key_id, admin_machines[machine_index].key_id);
	if (admin)
		*admin = admin_key_listed(admin_machines[machine_index].key_hex, admin_machines[machine_index].key_id);
	return TRUE;
}

/* the game's record (the server's) */
static struct network_game *admin_server_game(
	void)
{
	struct network_game_server *server = global_network_game_server_get();

	return server ? network_game_server_get_game(server) : NULL;
}

/* the machine's first player's name (the admin's, for the log) */
static void admin_machine_name(
	long machine_index,
	char *name,
	long size)
{
	struct network_game *game = admin_server_game();
	long slot;

	snprintf(name, (size_t)size, "machine %ld", machine_index);
	for (slot = 0; game && slot < MAXIMUM_ADMIN_PLAYERS; slot++)
	{
		if (network_player_is_valid(&game->players[slot]) && game->players[slot].machine_index == machine_index &&
			server_status_player_name(slot, name, size))
		{
			return;
		}
	}
}

static void admin_send_challenge(
	long machine_index)
{
	struct
	{
		struct distributed_message_header header;
		struct admin_wire_challenge challenge;
	} message;

	csmemset(&message, 0, sizeof(message));
	posix_random_bytes(admin_machines[machine_index].nonce, sizeof(admin_machines[machine_index].nonce));
	admin_fill_header(&message.header, _distributed_message_admin_down, (word)sizeof(message));
	message.challenge.format = ADMIN_FORMAT;
	message.challenge.kind = _network_admin_down_challenge;
	admin_server_id();
	csmemcpy(message.challenge.server_id, admin_server.id, sizeof(message.challenge.server_id));
	csmemcpy(message.challenge.nonce, admin_machines[machine_index].nonce, sizeof(message.challenge.nonce));
	if (network_distributed_server_send_to_machine_reliably(machine_index, &message, (word)sizeof(message)))
		admin_machines[machine_index].challenged = TRUE;
}

/* the game's flags, as an admin's machine is told them */
static word admin_state_flags(
	boolean admin)
{
	struct network_game *game = admin_server_game();
	short state = server_director_state();
	boolean playing = server_director_playing();
	boolean teams = playing ? game_engine_has_teams() : game && game->variant.universal_variant.teams;
	word flags = 0;

	SET_FLAG(flags, _network_admin_flag_admin, admin);
	SET_FLAG(flags, _network_admin_flag_playing, playing);
	SET_FLAG(flags, _network_admin_flag_lobby, state == _server_state_lobby);
	SET_FLAG(flags, _network_admin_flag_teams, teams);
	SET_FLAG(flags, _network_admin_flag_teams_locked, network_host_controls_teams_locked());
	SET_FLAG(flags, _network_admin_flag_team_moves, playing ? network_host_controls_team_moves() :
		teams && state == _server_state_lobby);
	SET_FLAG(flags, _network_admin_flag_standard, network_standard_hosting());
	return flags;
}

static void admin_send_state(
	long machine_index,
	boolean admin)
{
	struct
	{
		struct distributed_message_header header;
		struct admin_wire_state state;
	} message;
	struct server_entry const *current = server_director_current();
	struct server_entry const *next = server_director_next();

	csmemset(&message, 0, sizeof(message));
	admin_fill_header(&message.header, _distributed_message_admin_down, (word)sizeof(message));
	message.state.format = ADMIN_FORMAT;
	message.state.kind = _network_admin_down_state;
	message.state.flags = admin_state_flags(admin);
	message.state.rate = (short)network_standard_server_rate(FALSE);
	message.state.rotation_index = (short)server_director_index();
	if (admin && current)
		snprintf(message.state.current, sizeof(message.state.current), "%s", current->text);
	if (admin && next)
		snprintf(message.state.next, sizeof(message.state.next), "%s", next->text);
	network_distributed_server_send_to_machine_reliably(machine_index, &message, (word)sizeof(message));
}

/* the game's players as the admin's machine is told them, what it may do to
each */
static short admin_players_read(
	long admin_machine,
	struct admin_wire_player *players,
	short maximum)
{
	struct network_game *game = admin_server_game();
	boolean playing = server_director_playing();
	boolean lobby = server_director_state() == _server_state_lobby;
	boolean teams = game && game->variant.universal_variant.teams;
	short count = 0;
	long slot;

	for (slot = 0; game && slot < MAXIMUM_ADMIN_PLAYERS && count < maximum; slot++)
	{
		struct network_player const *player = &game->players[slot];
		struct admin_wire_player *entry = &players[count];
		struct player_datum *datum;
		unsigned long actions = 0;
		long machine_index = player->machine_index;

		if (!network_player_is_valid(player))
			continue;
		csmemset(entry, 0, sizeof(*entry));
		entry->slot = (byte)slot;
		server_status_player_name(slot, entry->name, sizeof(entry->name));
		datum = game_engine_running() ? distributed_player(player->player_list_index) : NULL;
		entry->absolute_index = datum ? (byte)player->player_list_index : NONE_BYTE;
		entry->team = teams ? (byte)player->team_index : NONE_BYTE;
		entry->ping = (short)(datum ? MIN(distributed_player_ping(player->player_list_index), 9999) : NONE);
		{
			wchar_t wide[12];

			csmemcpy(wide, player->name, sizeof(wide));
			entry->muted = network_chat_host_name_muted(wide) != 0;
		}
		/* (in a game what the host may do to them, as its Host Controls has it;
		in the lobby a kick, a ban and a mute, and a team move) */
		if (playing && datum)
			actions = network_host_controls_player_actions(distributed_player_from_byte((byte)player->player_list_index));
		else if (lobby)
		{
			SET_FLAG(actions, _host_controls_kick, TRUE);
			SET_FLAG(actions, _host_controls_ban, TRUE);
			SET_FLAG(actions, _host_controls_mute, TRUE);
			SET_FLAG(actions, _host_controls_move_team, teams);
		}
		if (machine_index == admin_machine)
		{
			/* (the admin's own: moved, never dropped) */
			actions &= FLAG(_host_controls_move_team);
			entry->tag = _host_controls_tag_host;
		}
		else if (network_standard_machine_is_opence(machine_index))
			entry->tag = _host_controls_tag_opence;
		else if (network_standard_machine_is_openh1(machine_index))
			entry->tag = _host_controls_tag_openh1;
		else
			entry->tag = _host_controls_tag_none;
		entry->actions = (byte)actions;
		count++;
	}
	return count;
}

static void admin_send_players(
	long machine_index)
{
	static struct admin_wire_player players[MAXIMUM_ADMIN_PLAYERS];
	short count = admin_players_read(machine_index, players, MAXIMUM_ADMIN_PLAYERS);
	short first = 0;

	/* (a message a page; an empty game's one, with none) */
	do
	{
		struct
		{
			struct distributed_message_header header;
			struct admin_wire_players page;
		} message;
		short page_count = (short)MIN(count - first, ADMIN_PLAYERS_PER_PAGE);
		word size = (word)(sizeof(message) - sizeof(message.page.players) +
			page_count * sizeof(struct admin_wire_player));

		csmemset(&message, 0, sizeof(message));
		admin_fill_header(&message.header, _distributed_message_admin_down, size);
		message.page.format = ADMIN_FORMAT;
		message.page.kind = _network_admin_down_players;
		message.page.first = (byte)first;
		message.page.count = (byte)page_count;
		message.page.total = (byte)count;
		csmemcpy(message.page.players, &players[first], page_count * sizeof(struct admin_wire_player));
		network_distributed_server_send_to_machine_reliably(machine_index, &message, size);
		first = (short)(first + page_count);
	}
	while (first < count);
}

/* a list's entries, sent in pages */
static struct
{
	long machine_index;
	byte list;
	short count;
	short first;
	struct
	{
		struct distributed_message_header header;
		struct admin_wire_lines page;
	} message;
	short entries_in_page;
} admin_lines;

static void admin_lines_send_page(
	short total)
{
	word size = (word)(sizeof(admin_lines.message) - sizeof(admin_lines.message.page.text) +
		admin_lines.message.page.size);

	admin_fill_header(&admin_lines.message.header, _distributed_message_admin_down, size);
	admin_lines.message.page.format = ADMIN_FORMAT;
	admin_lines.message.page.kind = _network_admin_down_lines;
	admin_lines.message.page.list = admin_lines.list;
	admin_lines.message.page.first = (word)admin_lines.first;
	admin_lines.message.page.count = (word)admin_lines.entries_in_page;
	admin_lines.message.page.total = (word)total;
	network_distributed_server_send_to_machine_reliably(admin_lines.machine_index, &admin_lines.message, size);
	admin_lines.first = (short)(admin_lines.first + admin_lines.entries_in_page);
	admin_lines.entries_in_page = 0;
	admin_lines.message.page.size = 0;
}

static void admin_lines_begin(
	long machine_index,
	byte list)
{
	csmemset(&admin_lines, 0, sizeof(admin_lines));
	admin_lines.machine_index = machine_index;
	admin_lines.list = list;
}

/* an entry added (its fields joined by tabs, cut to fit a page) */
static void admin_lines_add(
	char const *text,
	short total)
{
	long length = (long)MIN(strlen(text), 1000);

	if (admin_lines.message.page.size + length + 1 > ADMIN_LINES_TEXT_SIZE)
		admin_lines_send_page(total);
	csmemcpy(admin_lines.message.page.text + admin_lines.message.page.size, text, length);
	admin_lines.message.page.text[admin_lines.message.page.size + length] = 0;
	admin_lines.message.page.size = (word)(admin_lines.message.page.size + length + 1);
	admin_lines.entries_in_page++;
	admin_lines.count++;
}

static void admin_lines_end(
	short total)
{
	/* (the last page, or an empty list's) */
	if (admin_lines.entries_in_page || !total)
		admin_lines_send_page(total);
}

/* the maps (server_maps_each's) */
static short admin_map_count;
static void admin_map_counted(void *context, char const *name, char const *level, char const *kind)
{
	admin_map_count++;
}
static void admin_map_listed(void *context, char const *name, char const *level, char const *kind)
{
	char text[2 * SERVER_MAP_SIZE + 16];

	if (admin_lines.count >= MIN(admin_map_count, MAXIMUM_HOST_GAME_MAPS))
		return;
	snprintf(text, sizeof(text), "%s\t%s\t%s", name, level, kind);
	admin_lines_add(text, (short)MIN(admin_map_count, MAXIMUM_HOST_GAME_MAPS));
}

static void admin_send_lists(
	long machine_index)
{
	struct server_rotation const *rotation = server_director_rotation();
	char const *name;
	long index, count;
	short total;

	/* (the maps) */
	admin_map_count = 0;
	server_maps_each(admin_map_counted, NULL);
	admin_lines_begin(machine_index, _network_admin_list_maps);
	server_maps_each(admin_map_listed, NULL);
	admin_lines_end((short)MIN(admin_map_count, MAXIMUM_HOST_GAME_MAPS));
	/* (the gametypes: the built-in ones and OpenCE's) */
	for (count = 0; server_gametype_builtin_name(count) && count < MAXIMUM_HOST_GAME_GAMETYPES; count++)
	{
	}
	admin_lines_begin(machine_index, _network_admin_list_gametypes);
	for (index = 0; index < count && (name = server_gametype_builtin_name(index)) != NULL; index++)
		admin_lines_add(name, (short)count);
	admin_lines_end((short)count);
	/* (the rotation: each entry's text, and why it cannot be played) */
	total = (short)MIN(rotation->count, NETWORK_ADMIN_ROTATION_MOST);
	admin_lines_begin(machine_index, _network_admin_list_rotation);
	for (index = 0; index < total; index++)
	{
		char text[SERVER_ENTRY_TEXT_SIZE + SERVER_REASON_SIZE + 2];

		snprintf(text, sizeof(text), "%s\t%s", rotation->entries[index].text, rotation->entries[index].rejected);
		admin_lines_add(text, total);
	}
	admin_lines_end(total);
	/* (the bans: their numbers in bans.txt, the newest last) */
	count = server_commands_bans_read();
	total = (short)MIN(count, MAXIMUM_ADMIN_BANS);
	admin_lines_begin(machine_index, _network_admin_list_bans);
	for (index = count - total; index < count; index++)
	{
		struct host_controls_ban ban;
		char text[ADMIN_BAN_LINE_SIZE];

		if (!network_host_controls_ban_parse(server_commands_ban_line(index), &ban))
			csmemset(&ban, 0, sizeof(ban));
		snprintf(text, sizeof(text), "%ld\t%s\t%s\t%s\t%s", index, ban.when, ban.address, ban.players, ban.reason);
		admin_lines_add(text, total);
	}
	admin_lines_end(total);
}

static void admin_send_result(
	long machine_index,
	word id,
	boolean ok,
	char const *text)
{
	struct
	{
		struct distributed_message_header header;
		struct admin_wire_result result;
	} message;

	csmemset(&message, 0, sizeof(message));
	admin_fill_header(&message.header, _distributed_message_admin_down, (word)sizeof(message));
	message.result.format = ADMIN_FORMAT;
	message.result.kind = _network_admin_down_result;
	message.result.ok = ok ? 1 : 0;
	message.result.id = id;
	snprintf(message.result.text, sizeof(message.result.text), "%s", text);
	network_distributed_server_send_to_machine_reliably(machine_index, &message, (word)sizeof(message));
}

/* an admin's request run as the server's command, at the admin role */
static void admin_server_request(
	long machine_index,
	struct admin_wire_request const *wire)
{
	struct network_admin_request request;
	struct server_entry const *current = server_director_current();
	char slot_name[NETWORK_ADMIN_NAME_SIZE] = "";
	char line[SERVER_COMMAND_LINE_SIZE];
	char why[SERVER_REASON_SIZE];
	char name[NETWORK_ADMIN_NAME_SIZE];
	char actor[96];
	char said[NETWORK_ADMIN_RESULT_SIZE];
	char *output;
	boolean ok = FALSE;

	csmemset(&request, 0, sizeof(request));
	request.verb = wire->verb;
	request.slot = wire->slot == NONE_BYTE ? NONE : wire->slot;
	request.number = wire->number;
	csmemcpy(request.name, wire->name, sizeof(request.name));
	csmemcpy(request.text, wire->text, sizeof(request.text));
	if (request.verb == _network_admin_lists)
	{
		admin_machines[machine_index].lists_asked = TRUE;
		return;
	}
	if (request.slot != NONE)
		server_status_player_name(request.slot, slot_name, sizeof(slot_name));
	if (!network_admin_request_line(&request, slot_name, current && server_director_playing() ? current->text : NULL,
		line, sizeof(line), why, sizeof(why)))
	{
		admin_send_result(machine_index, wire->id, FALSE, why);
		return;
	}
	admin_machine_name(machine_index, name, sizeof(name));
	snprintf(actor, sizeof(actor), "admin %s (key %s)", name, admin_machines[machine_index].key_id);
	server_commands_set_actor(actor);
	output = server_commands_run(line, _server_role_admin, _server_origin_game, &ok);
	server_commands_set_actor(NULL);
	/* (its answer: the output's last line, else what it did) */
	said[0] = 0;
	if (output)
	{
		char *last = output;
		char *cursor;

		for (cursor = output; *cursor; cursor++)
		{
			if (*cursor == '\n' && cursor[1])
				last = cursor + 1;
		}
		snprintf(said, sizeof(said), "%.*s", (int)strcspn(last, "\n"), last);
		server_console_free(output);
	}
	server_log(_server_log_info, "moderation", "%s ran \"%s\" from the game: %s", actor, line,
		ok ? "done" : "refused");
	{
		struct server_json json;

		server_json_begin(&json);
		server_json_object_begin(&json, NULL);
		server_json_string(&json, "event", "admin_command");
		server_json_string(&json, "name", name);
		server_json_string(&json, "key_id", admin_machines[machine_index].key_id);
		server_json_string(&json, "line", line);
		server_json_boolean(&json, "ok", ok);
		server_json_object_end(&json);
		server_event_add("moderation", server_json_text(&json));
		server_json_free(&json);
	}
	admin_send_result(machine_index, wire->id, ok, said[0] ? said : ok ? "done" : "refused");
	/* (its lists, which it may have changed) */
	admin_machines[machine_index].lists_asked = TRUE;
}

static void admin_server_proof(
	long machine_index,
	struct admin_wire_proof const *proof)
{
	char name[NETWORK_ADMIN_NAME_SIZE];

	if (!admin_machines[machine_index].challenged || admin_machines[machine_index].proven)
		return;
	/* (one proof a nonce: a wrong one leaves the machine with none) */
	admin_machines[machine_index].challenged = FALSE;
	if (!player_link_admin_server_check(proof->key, admin_machines[machine_index].nonce,
		network_link_machine_virtual_address(machine_index), proof->signature))
	{
		admin_machine_name(machine_index, name, sizeof(name));
		server_log(_server_log_info, "player", "%s's admin key proof does not check", name);
		return;
	}
	admin_machines[machine_index].proven = TRUE;
	csmemcpy(admin_machines[machine_index].key, proof->key, sizeof(admin_machines[machine_index].key));
	{
		static char const digits[] = "0123456789abcdef";
		short index;

		for (index = 0; index < PLAYER_LINK_KEY_SIZE; index++)
		{
			admin_machines[machine_index].key_hex[2 * index] = digits[proof->key[index] >> 4];
			admin_machines[machine_index].key_hex[2 * index + 1] = digits[proof->key[index] & 15];
		}
		admin_machines[machine_index].key_hex[2 * PLAYER_LINK_KEY_SIZE] = 0;
	}
	player_link_key_id(proof->key, admin_machines[machine_index].key_id);
	admin_machines[machine_index].admin = admin_key_listed(admin_machines[machine_index].key_hex,
		admin_machines[machine_index].key_id);
	admin_machine_name(machine_index, name, sizeof(name));
	server_log(_server_log_info, "player", "%s proved admin key %s%s", name, admin_machines[machine_index].key_id,
		admin_machines[machine_index].admin ? ": an admin" : "");
	/* (told at once) */
	admin_server.told_at = 0;
}

void network_admin_server_update(
	void)
{
	long machines[MAXIMUM_ADMIN_MACHINES];
	boolean present[MAXIMUM_ADMIN_MACHINES];
	short count, index;
	unsigned long now = system_milliseconds();
	boolean tell;

	if (!halo_server_mode() || !global_network_game_server_get())
		return;
	count = network_distributed_server_machines(machines, MAXIMUM_ADMIN_MACHINES);
	csmemset(present, 0, sizeof(present));
	for (index = 0; index < count; index++)
	{
		if (machines[index] >= 0 && machines[index] < MAXIMUM_ADMIN_MACHINES)
			present[machines[index]] = TRUE;
	}
	tell = !admin_server.told_at || now - admin_server.told_at >= ADMIN_STATE_INTERVAL;
	if (tell)
	{
		admin_server.told_at = now ? now : 1;
		admin_file_read();
	}
	for (index = 0; index < MAXIMUM_ADMIN_MACHINES; index++)
	{
		if (!present[index])
		{
			if (admin_machines[index].present)
				csmemset(&admin_machines[index], 0, sizeof(admin_machines[index]));
			continue;
		}
		admin_machines[index].present = TRUE;
		/* (OpenH1's machines alone hear OpenH1's messages) */
		if (!network_standard_machine_is_openh1(index))
			continue;
		if (!admin_machines[index].challenged && !admin_machines[index].proven)
			admin_send_challenge(index);
		if (!admin_machines[index].proven || !tell)
			continue;
		admin_machines[index].admin = admin_key_listed(admin_machines[index].key_hex, admin_machines[index].key_id);
		if (admin_machines[index].admin)
		{
			admin_send_state(index, TRUE);
			admin_send_players(index);
			admin_machines[index].told = TRUE;
		}
		else if (admin_machines[index].told)
		{
			/* (an admin no longer: its Host Controls gone) */
			admin_send_state(index, FALSE);
			admin_machines[index].told = FALSE;
		}
	}
	for (index = 0; index < MAXIMUM_ADMIN_MACHINES; index++)
	{
		if (admin_machines[index].lists_asked && admin_machines[index].admin)
			admin_send_lists(index);
		admin_machines[index].lists_asked = FALSE;
	}
}

void network_admin_list_text(
	char *text,
	long size)
{
	long length = 0;

	admin_file_read();
	length += snprintf(text + length, (size_t)(size - length), "admins.txt:\n%s%s", admin_server.file[0] ?
		admin_server.file : "(none)\n", admin_server.file[0] && admin_server.file[strlen(admin_server.file) - 1] != '\n' ?
		"\n" : "");
	if (length < size)
	{
		snprintf(text + length, (size_t)(size - length), "server.admins: %s", config_string("server.admins")[0] ?
			config_string("server.admins") : "(none)");
	}
}

/* admins.txt written whole */
static boolean admin_file_write(
	char const *text)
{
	char path[1024];

	admin_file_path(path, sizeof(path));
	return posix_server_write_file(path, text, (unsigned long)strlen(text)) != 0;
}

boolean network_admin_add(
	char const *key_hex,
	char const *name,
	char *why,
	long why_size)
{
	static char text[ADMIN_FILE_SIZE];
	unsigned char key[PLAYER_LINK_KEY_SIZE];
	char key_id[NETWORK_ADMIN_KEY_ID_TEXT_SIZE];
	char lowered[ADMIN_KEY_HEX_SIZE];
	char clean[NETWORK_ADMIN_NAME_SIZE * 2];
	short index;

	why[0] = 0;
	if (strlen(key_hex) != 64)
	{
		snprintf(why, (size_t)why_size, "a admin key is 64 hex digits");
		return FALSE;
	}
	for (index = 0; index < 64; index++)
	{
		if (!admin_hex_digit(key_hex[index]))
		{
			snprintf(why, (size_t)why_size, "a admin key is 64 hex digits");
			return FALSE;
		}
		lowered[index] = admin_lower(key_hex[index]);
	}
	lowered[64] = 0;
	for (index = 0; index < PLAYER_LINK_KEY_SIZE; index++)
	{
		char pair[3] = { lowered[2 * index], lowered[2 * index + 1], 0 };

		key[index] = (unsigned char)strtoul(pair, NULL, 16);
	}
	player_link_key_id(key, key_id);
	admin_file_read();
	if (network_admin_listed(lowered, key_id, admin_server.file))
	{
		snprintf(why, (size_t)why_size, "key %s is an admin already", key_id);
		return FALSE;
	}
	if (!admin_text_clean(clean, sizeof(clean), name ? name : "", (long)(name ? strlen(name) : 0), FALSE))
		clean[0] = 0;
	snprintf(text, sizeof(text), "%s%s%s %s\n", admin_server.file, admin_server.file[0] &&
		admin_server.file[strlen(admin_server.file) - 1] != '\n' ? "\n" : "", lowered, clean);
	if (strlen(text) >= sizeof(text) - 1 || !admin_file_write(text))
	{
		snprintf(why, (size_t)why_size, "admins.txt cannot be written");
		return FALSE;
	}
	admin_server.told_at = 0;
	return TRUE;
}

boolean network_admin_remove(
	char const *key_or_id,
	char *why,
	long why_size)
{
	static char text[ADMIN_FILE_SIZE];
	char const *line;
	long length = 0;
	boolean found = FALSE;

	why[0] = 0;
	admin_file_read();
	for (line = admin_server.file; *line;)
	{
		long line_length = (long)strcspn(line, "\n");
		char first[80];
		long word = (long)strcspn(line, " \t\n#");
		boolean drop;

		snprintf(first, sizeof(first), "%.*s", (int)MIN(word, (long)sizeof(first) - 1), line);
		/* (the line's key, or its key's id, as given) */
		drop = first[0] && !csstrcasecmp(first, key_or_id);
		if (!drop && strlen(first) == 64 && strlen(key_or_id) == 16)
		{
			unsigned char key[PLAYER_LINK_KEY_SIZE];
			char id[NETWORK_ADMIN_KEY_ID_TEXT_SIZE];
			short index;

			for (index = 0; index < PLAYER_LINK_KEY_SIZE; index++)
			{
				char pair[3] = { first[2 * index], first[2 * index + 1], 0 };

				key[index] = (unsigned char)strtoul(pair, NULL, 16);
			}
			player_link_key_id(key, id);
			drop = !csstrcasecmp(id, key_or_id);
		}
		if (drop)
			found = TRUE;
		else
		{
			csmemcpy(text + length, line, line_length);
			length += line_length;
			text[length++] = '\n';
		}
		line += line_length + (line[line_length] ? 1 : 0);
	}
	text[length] = 0;
	if (!found)
	{
		snprintf(why, (size_t)why_size, !config_string("server.admins")[0] ? "admins.txt names no such key" :
			"admins.txt names no such key (server.admins may: it is config.toml's)");
		return FALSE;
	}
	if (!admin_file_write(text))
	{
		snprintf(why, (size_t)why_size, "admins.txt cannot be written");
		return FALSE;
	}
	admin_server.told_at = 0;
	return TRUE;
}

#else

void network_admin_machine_joined(long machine_index) { }
void network_admin_server_update(void) { }
boolean network_admin_machine_key(long machine_index, char *key_hex, char *key_id, boolean *admin) { return FALSE; }
void network_admin_list_text(char *text, long size) { if (size > 0) text[0] = 0; }
boolean network_admin_add(char const *key_hex, char const *name, char *why, long why_size) { return FALSE; }
boolean network_admin_remove(char const *key_or_id, char *why, long why_size) { return FALSE; }

#endif

/* ---------- an admin's machine */

static struct
{
	/* the state as last heard (0: none) */
	unsigned long heard_at;
	struct network_admin_state state;
	/* the players */
	struct admin_wire_player players[MAXIMUM_ADMIN_PLAYERS];
	short player_count;
	/* the lists */
	struct host_game_map maps[MAXIMUM_HOST_GAME_MAPS];
	short map_count;
	struct host_game_gametype gametypes[MAXIMUM_HOST_GAME_GAMETYPES];
	short gametype_count;
	struct server_rotation rotation;
	struct host_controls_ban bans[MAXIMUM_ADMIN_BANS];
	long ban_numbers[MAXIMUM_ADMIN_BANS];
	short ban_count;
	/* the requests' ids, and the last answer not yet taken */
	word next_id;
	boolean result_waiting;
	boolean result_ok;
	char result[NETWORK_ADMIN_RESULT_SIZE];
	/* the admin key's id this machine proved to the server */
	char key_id[NETWORK_ADMIN_KEY_ID_TEXT_SIZE];
} admin_client;

boolean network_admin_remote(
	void)
{
	return admin_client.heard_at && system_milliseconds() - admin_client.heard_at < ADMIN_STATE_TIMEOUT &&
		!global_network_game_server_get() && global_network_game_client_get() &&
		TEST_FLAG(admin_client.state.flags, _network_admin_flag_admin);
}

boolean network_admin_was_remote(
	void)
{
	/* (heard this connection, however long ago: a map's loading holds the
	messages back) */
	return admin_client.heard_at && !global_network_game_server_get() && global_network_game_client_get() &&
		TEST_FLAG(admin_client.state.flags, _network_admin_flag_admin);
}

boolean network_admin_remote_state(
	struct network_admin_state *state)
{
	*state = admin_client.state;
	return network_admin_remote();
}

short network_admin_players(
	struct host_controls_player *players,
	short maximum)
{
	short count = 0, index;

	if (!network_admin_remote())
		return 0;
	for (index = 0; index < admin_client.player_count && count < maximum; index++)
	{
		struct admin_wire_player const *wire = &admin_client.players[index];
		struct host_controls_player *entry = &players[count];
		short character;

		csmemset(entry, 0, sizeof(*entry));
		/* (the game's player on this machine, for its score) */
		entry->player_index = wire->absolute_index != NONE_BYTE && game_engine_running() ?
			distributed_player_from_byte(wire->absolute_index) : NONE;
		if (entry->player_index != NONE && !player_try_and_get(entry->player_index))
			entry->player_index = NONE;
		entry->slot = wire->slot;
		/* (the slot stands for the machine: the menu matches by it) */
		entry->machine_index = wire->slot;
		for (character = 0; character < (short)NUMBEROF(entry->name) - 1 && wire->name[character]; character++)
			entry->name[character] = (wchar_t)(unsigned char)wire->name[character];
		entry->team = wire->team == NONE_BYTE ? NONE : wire->team;
		entry->ping = wire->ping < 0 ? NONE : wire->ping;
		entry->tag = wire->tag < NUMBER_OF_HOST_CONTROLS_TAGS ? wire->tag : _host_controls_tag_none;
		entry->actions = wire->actions & (FLAG(NUMBER_OF_HOST_CONTROLS_ACTIONS) - 1);
		count++;
	}
	return count;
}

boolean network_admin_player(
	long player_index,
	struct host_controls_player *player)
{
	static struct host_controls_player players[MAXIMUM_ADMIN_PLAYERS];
	short count = network_admin_players(players, MAXIMUM_ADMIN_PLAYERS);
	short index;

	for (index = 0; player_index != NONE && index < count; index++)
	{
		if (players[index].player_index == player_index)
		{
			*player = players[index];
			return TRUE;
		}
	}
	return FALSE;
}

boolean network_admin_player_muted(
	short slot)
{
	short index;

	for (index = 0; index < admin_client.player_count; index++)
	{
		if (admin_client.players[index].slot == slot)
			return admin_client.players[index].muted != 0;
	}
	return FALSE;
}

short network_admin_maps(
	struct host_game_map const **maps)
{
	*maps = admin_client.maps;
	return admin_client.map_count;
}

short network_admin_gametypes(
	struct host_game_gametype const **gametypes)
{
	*gametypes = admin_client.gametypes;
	return admin_client.gametype_count;
}

struct server_rotation const *network_admin_rotation(
	void)
{
	return &admin_client.rotation;
}

short network_admin_bans(
	struct host_controls_ban *bans,
	char (*lines)[512],
	short maximum)
{
	short index;

	for (index = 0; index < admin_client.ban_count && index < maximum; index++)
	{
		bans[index] = admin_client.bans[index];
		snprintf(lines[index], 512, "%ld", admin_client.ban_numbers[index]);
	}
	return index;
}

/* a request sent to the host (a client's), whether or not it said this
machine is an admin (the console's: the host decides) */
static boolean admin_send(
	short verb,
	long slot,
	wchar_t const *name,
	long number,
	char const *text)
{
	struct
	{
		struct distributed_message_header header;
		struct admin_wire_request request;
	} message;
	short index;

	if (global_network_game_server_get() || !global_network_game_client_get() || verb < 0 ||
		verb >= NUMBER_OF_NETWORK_ADMIN_REQUESTS)
	{
		return FALSE;
	}
	csmemset(&message, 0, sizeof(message));
	admin_fill_header(&message.header, _distributed_message_admin_up, (word)sizeof(message));
	message.request.format = ADMIN_FORMAT;
	message.request.kind = _network_admin_up_request;
	message.request.verb = (byte)verb;
	message.request.slot = slot >= 0 && slot < MAXIMUM_ADMIN_PLAYERS ? (byte)slot : NONE_BYTE;
	message.request.id = ++admin_client.next_id;
	message.request.number = number;
	for (index = 0; name && index < NETWORK_ADMIN_NAME_SIZE - 1 && name[index]; index++)
		message.request.name[index] = (char)player_name_character_ascii(name[index]);
	if (text)
		snprintf(message.request.text, sizeof(message.request.text), "%s", text);
	return network_distributed_client_send_reliably(&message, (word)sizeof(message));
}

boolean network_admin_send(
	short verb,
	long slot,
	wchar_t const *name,
	long number,
	char const *text)
{
	return network_admin_remote() && admin_send(verb, slot, name, number, text);
}

boolean network_admin_take_result(
	boolean *ok,
	char *text,
	long size)
{
	if (!admin_client.result_waiting)
		return FALSE;
	admin_client.result_waiting = FALSE;
	*ok = admin_client.result_ok;
	snprintf(text, (size_t)size, "%s", admin_client.result);
	return TRUE;
}

/* the host's virtual address as this machine reaches it (network byte
order; 0: a LAN's), as a join's pass is bound (network_client_manager.c) */
static unsigned long admin_host_virtual_address(
	void)
{
	struct network_game_client *client = global_network_game_client_get();
	struct transport_address address;
	unsigned long host;

	if (!client)
		return 0;
	csmemset(&address, 0, sizeof(address));
	network_connection_get_address(network_game_client_get_connection(client), &address, NULL);
	host = address.address.long_words[0];
	return (host & 0xFFC00000) == 0x64400000 ?
		(host >> 24) | ((host >> 8) & 0xFF00) | ((host << 8) & 0xFF0000) | (host << 24) : 0;
}

static void admin_client_challenge(
	struct admin_wire_challenge const *challenge)
{
	struct
	{
		struct distributed_message_header header;
		struct admin_wire_proof proof;
	} message;

	csmemset(&message, 0, sizeof(message));
	if (!player_link_admin_sign(admin_host_virtual_address(), challenge->server_id, challenge->nonce,
		message.proof.key, message.proof.signature))
	{
		return;
	}
	admin_fill_header(&message.header, _distributed_message_admin_up, (word)sizeof(message));
	message.proof.format = ADMIN_FORMAT;
	message.proof.kind = _network_admin_up_proof;
	player_link_key_id(message.proof.key, admin_client.key_id);
	network_distributed_client_send_reliably(&message, (word)sizeof(message));
}

boolean network_admin_console(
	char const *expression,
	boolean *result)
{
	static struct
	{
		char const *word;
		short verb;
	} const verbs[] =
	{
		{ "map", _network_admin_map_now },
		{ "map_after", _network_admin_map_after },
		{ "rotation_add", _network_admin_rotation_add },
		{ "end", _network_admin_end_game },
		{ "restart", _network_admin_restart },
		{ "say", _network_admin_announce },
	};
	char text[NETWORK_ADMIN_TEXT_SIZE];
	long length;
	short index;

	while (*expression == ' ' || *expression == '\t' || *expression == '(')
		expression++;
	if (strncmp(expression, "admin", 5) || (expression[5] && expression[5] != ' ' && expression[5] != ')'))
		return FALSE;
	expression += 5;
	while (*expression == ' ' || *expression == '"')
		expression++;
	/* (alone: whether this machine is an admin here, and its key's id) */
	if (!*expression || *expression == ')')
	{
		console_printf(FALSE, "admin: %s (admin key %s)", network_admin_remote() ? "an admin of this server" :
			"not an admin here", admin_client.key_id[0] ? admin_client.key_id : "not proven to this host");
		*result = TRUE;
		return TRUE;
	}
	for (index = 0; index < (short)NUMBEROF(verbs); index++)
	{
		long word_length = (long)strlen(verbs[index].word);

		if (!strncmp(expression, verbs[index].word, word_length) && (!expression[word_length] ||
			expression[word_length] == ' ' || expression[word_length] == ')' || expression[word_length] == '"'))
		{
			expression += word_length;
			break;
		}
	}
	if (index == (short)NUMBEROF(verbs))
	{
		console_warning("admin: map, map_after, rotation_add <map[:gametype]>; end; restart; say <text>");
		*result = FALSE;
		return TRUE;
	}
	while (*expression == ' ' || *expression == '"')
		expression++;
	snprintf(text, sizeof(text), "%s", expression);
	length = (long)strlen(text);
	while (length > 0 && (text[length - 1] == ')' || text[length - 1] == '"' || text[length - 1] == ' '))
		text[--length] = 0;
	*result = admin_send(verbs[index].verb, NONE, NULL, 0, text);
	if (!*result)
		console_warning("admin: only a client of a dedicated server asks it");
	else if (!network_admin_remote())
		console_printf(FALSE, "admin: asked, but the server takes it of its admins alone");
	return TRUE;
}

/* the next of a page's entries (ended within the page), else NULL */
static char const *admin_page_entry(
	struct admin_wire_lines const *page,
	long *offset)
{
	char const *entry;
	long length;

	if (*offset >= page->size || page->size > ADMIN_LINES_TEXT_SIZE)
		return NULL;
	entry = page->text + *offset;
	for (length = 0; *offset + length < page->size && entry[length]; length++)
	{
	}
	if (*offset + length >= page->size)
		return NULL;
	*offset += length + 1;
	return entry;
}

/* the field'th of an entry's tab-separated fields, cut to size */
static void admin_field(
	char const *entry,
	short field,
	char *text,
	long size)
{
	long length;

	while (field-- > 0 && entry)
	{
		entry = strchr(entry, '\t');
		if (entry)
			entry++;
	}
	if (!entry)
	{
		text[0] = 0;
		return;
	}
	length = (long)strcspn(entry, "\t");
	snprintf(text, (size_t)size, "%.*s", (int)MIN(length, size - 1), entry);
}

static void admin_client_lines(
	struct admin_wire_lines const *page)
{
	long offset = 0;
	long index = page->first;
	char const *entry;
	short taken = 0;

	while (taken < page->count && (entry = admin_page_entry(page, &offset)) != NULL)
	{
		taken++;
		switch (page->list)
		{
		case _network_admin_list_maps:
			if (index < MAXIMUM_HOST_GAME_MAPS)
			{
				struct host_game_map *map = &admin_client.maps[index];
				char kind[16];

				admin_field(entry, 0, map->name, sizeof(map->name));
				admin_field(entry, 1, map->level, sizeof(map->level));
				admin_field(entry, 2, kind, sizeof(kind));
				map->custom = !strcmp(kind, "custom");
			}
			break;
		case _network_admin_list_gametypes:
			if (index < MAXIMUM_HOST_GAME_GAMETYPES)
			{
				struct host_game_gametype *gametype = &admin_client.gametypes[index];

				admin_field(entry, 0, gametype->name, sizeof(gametype->name));
				network_host_game_gametype_text(gametype->name, gametype->shown, NUMBEROF(gametype->shown));
			}
			break;
		case _network_admin_list_rotation:
			if (index < SERVER_ROTATION_MAXIMUM_ENTRIES)
			{
				struct server_entry *rotation_entry = &admin_client.rotation.entries[index];
				char text[SERVER_ENTRY_TEXT_SIZE];

				admin_field(entry, 0, text, sizeof(text));
				csmemset(rotation_entry, 0, sizeof(*rotation_entry));
				if (!server_rotation_parse_entry(text, (int)strlen(text), rotation_entry))
					snprintf(rotation_entry->text, sizeof(rotation_entry->text), "%s", text);
				if (!rotation_entry->map_name[0])
					snprintf(rotation_entry->map_name, sizeof(rotation_entry->map_name), "%s", rotation_entry->map);
				admin_field(entry, 1, rotation_entry->rejected, sizeof(rotation_entry->rejected));
			}
			break;
		case _network_admin_list_bans:
			if (index < MAXIMUM_ADMIN_BANS)
			{
				struct host_controls_ban *ban = &admin_client.bans[index];
				char number[16];

				admin_field(entry, 0, number, sizeof(number));
				admin_client.ban_numbers[index] = strtol(number, NULL, 10);
				admin_field(entry, 1, ban->when, sizeof(ban->when));
				admin_field(entry, 2, ban->address, sizeof(ban->address));
				admin_field(entry, 3, ban->players, sizeof(ban->players));
				admin_field(entry, 4, ban->reason, sizeof(ban->reason));
			}
			break;
		default:
			return;
		}
		index++;
	}
	/* (the list as long as its total says, once its last page came) */
	if (page->first + page->count >= page->total)
	{
		switch (page->list)
		{
		case _network_admin_list_maps:
			admin_client.map_count = (short)MIN(page->total, MAXIMUM_HOST_GAME_MAPS);
			break;
		case _network_admin_list_gametypes:
			admin_client.gametype_count = (short)MIN(page->total, MAXIMUM_HOST_GAME_GAMETYPES);
			break;
		case _network_admin_list_rotation:
			admin_client.rotation.count = (int)MIN(page->total, SERVER_ROTATION_MAXIMUM_ENTRIES);
			break;
		case _network_admin_list_bans:
			admin_client.ban_count = (short)MIN(page->total, MAXIMUM_ADMIN_BANS);
			break;
		}
	}
}

/* ---------- the messages */

boolean network_admin_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_admin_down || header.type == _distributed_message_admin_up;
}

void network_admin_handle_message(
	long machine_index,
	word const *message,
	word size,
	boolean from_stream)
{
	struct distributed_message_header header;
	byte const *body = (byte const *)message + sizeof(header);
	long length = (long)size - (long)sizeof(header);

	if (!network_admin_handles_message(message, size) || !from_stream || length < 2 || body[0] != ADMIN_FORMAT)
		return;
	csmemcpy(&header, message, sizeof(header));
	if (header.type == _distributed_message_admin_up)
	{
#if HALO_SERVER_BUILD
		/* (a dedicated server's, from an OpenH1 machine of its game) */
		if (!halo_server_mode() || machine_index < 0 || machine_index >= MAXIMUM_ADMIN_MACHINES ||
			!admin_machines[machine_index].present || !network_standard_machine_is_openh1(machine_index))
		{
			return;
		}
		if (body[1] == _network_admin_up_proof && length == (long)sizeof(struct admin_wire_proof))
		{
			struct admin_wire_proof proof;

			csmemcpy(&proof, body, sizeof(proof));
			admin_server_proof(machine_index, &proof);
		}
		else if (body[1] == _network_admin_up_request && length == (long)sizeof(struct admin_wire_request))
		{
			struct admin_wire_request request;

			csmemcpy(&request, body, sizeof(request));
			/* (an admin's alone, a few a second) */
			if (!admin_machines[machine_index].proven || !admin_machines[machine_index].admin ||
				!network_admin_rate_take(&admin_machines[machine_index].rate, system_milliseconds()))
			{
				return;
			}
			request.name[sizeof(request.name) - 1] = 0;
			request.text[sizeof(request.text) - 1] = 0;
			admin_server_request(machine_index, &request);
		}
#endif
		return;
	}
	/* (the host's, to this machine, a client) */
	if (machine_index != NONE || global_network_game_server_get())
		return;
	switch (body[1])
	{
	case _network_admin_down_challenge:
		if (length == (long)sizeof(struct admin_wire_challenge))
		{
			struct admin_wire_challenge challenge;

			csmemcpy(&challenge, body, sizeof(challenge));
			/* (a new connection's: nothing of another server's kept) */
			csmemset(&admin_client, 0, sizeof(admin_client));
			admin_client_challenge(&challenge);
		}
		break;
	case _network_admin_down_state:
		if (length == (long)sizeof(struct admin_wire_state))
		{
			struct admin_wire_state state;

			csmemcpy(&state, body, sizeof(state));
			if (TEST_FLAG(state.flags, _network_admin_flag_admin) !=
				TEST_FLAG(admin_client.state.flags, _network_admin_flag_admin))
			{
				platform_log(TEST_FLAG(state.flags, _network_admin_flag_admin) ?
					"admin: the server made this machine an admin (admin key %s): Host Controls" :
					"admin: the server no longer has this machine an admin (admin key %s)", admin_client.key_id);
			}
			admin_client.state.flags = state.flags;
			admin_client.state.rate = state.rate;
			admin_client.state.rotation_index = state.rotation_index;
			state.current[sizeof(state.current) - 1] = 0;
			state.next[sizeof(state.next) - 1] = 0;
			admin_text_clean(admin_client.state.current, sizeof(admin_client.state.current), state.current,
				sizeof(state.current), TRUE);
			admin_text_clean(admin_client.state.next, sizeof(admin_client.state.next), state.next, sizeof(state.next),
				TRUE);
			admin_client.heard_at = system_milliseconds();
			if (!admin_client.heard_at)
				admin_client.heard_at = 1;
		}
		break;
	case _network_admin_down_players:
	{
		struct admin_wire_players page;
		long fixed = (long)(sizeof(page) - sizeof(page.players));

		if (length < fixed || length > (long)sizeof(page))
			break;
		csmemset(&page, 0, sizeof(page));
		csmemcpy(&page, body, length);
		if (page.count > ADMIN_PLAYERS_PER_PAGE || length != fixed + page.count * (long)sizeof(struct admin_wire_player) ||
			page.first + page.count > MAXIMUM_ADMIN_PLAYERS || page.first + page.count > page.total)
		{
			break;
		}
		{
			short index;

			for (index = 0; index < page.count; index++)
			{
				struct admin_wire_player *player = &admin_client.players[page.first + index];

				*player = page.players[index];
				player->name[sizeof(player->name) - 1] = 0;
			}
		}
		if (page.first + page.count >= page.total)
			admin_client.player_count = page.total;
		break;
	}
	case _network_admin_down_lines:
	{
		static struct admin_wire_lines page;
		long fixed = (long)(sizeof(page) - sizeof(page.text));

		if (length < fixed || length > (long)sizeof(page))
			break;
		csmemset(&page, 0, sizeof(page));
		csmemcpy(&page, body, length);
		if (page.list >= NUMBER_OF_NETWORK_ADMIN_LISTS || length != fixed + page.size ||
			page.size > ADMIN_LINES_TEXT_SIZE || page.first + page.count > page.total)
		{
			break;
		}
		admin_client_lines(&page);
		break;
	}
	case _network_admin_down_result:
		if (length == (long)sizeof(struct admin_wire_result))
		{
			struct admin_wire_result result;

			csmemcpy(&result, body, sizeof(result));
			result.text[sizeof(result.text) - 1] = 0;
			admin_text_clean(admin_client.result, sizeof(admin_client.result), result.text, sizeof(result.text),
				FALSE);
			admin_client.result_ok = result.ok != 0;
			admin_client.result_waiting = TRUE;
		}
		break;
	default:
		break;
	}
}
