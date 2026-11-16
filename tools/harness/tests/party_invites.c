/*
PARTY_INVITES.C (test)

Parties made in a network game (port/linux/game/network_party.c): what the
scoreboard's player menu offers whom (an OpenH1 machine's player that can
party; never an OpenCE machine's, a bot, or anyone in a Legacy game), the
messages (their format and guards: the host takes a client's only from its
own stream and its own players, at most a few at once, and sends an invite
on only to an OpenH1 machine of the game that told it it can party, never to
an OpenCE one), and an invite accepted, which joins the party as its link
does (p2p_party_join_link); and the room held for a party joining the game
(the leader's ask of the host and the host's answer, the holds as the
host's server asks of them, a party's machines told apart, the holds let
go). test_party_invites.py takes the code
(under_test.inc: network_party.c from its constants to its drawing) and the
constants (config.inc).
*/

#include "harness.h"

#include <stdarg.h>
#include <wchar.h>

typedef unsigned short word;
#define csmemcpy memcpy
#define csmemset memset
#define csmemcmp memcmp
#define csstrcpy strcpy
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define DATUM_INDEX_NEW(absolute_index, identifier) \
	((long)(((unsigned long)(unsigned short)(identifier) << 16) | (unsigned long)(absolute_index)))
#define error(level, ...) ((void)(level))
/* (network_party.h's drawing takes one; nothing here draws) */
typedef struct { short y0, x0, y1, x1; } rectangle2d;

#include "config.inc"

/* ---------- the world */

enum
{
	_game_connection_local,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
};

enum
{
	_error_log,
	MAXIMUM_TRACKED_PLAYERS = HALO_PORT_MAXIMUM_NETWORK_PLAYERS,
	NO_PLAYER = 0xFF,
	MACHINES = 8,
	SENT = 32,
	BOTS_FIRST_MACHINE = HALO_PORT_MAXIMUM_NETWORK_MACHINES - 15,
};

/* the machines of the test's game: the host's own is 0, OpenH1 clients 1, 2
and 4 (4 an older OpenH1, which never tells the host anything), an OpenCE
client 3; a bot */
enum
{
	HOST_MACHINE = 0,
	OPENH1_MACHINE = 1,
	OTHER_MACHINE = 2,
	OPENCE_MACHINE = 3,
	OLDER_MACHINE = 4,
	BOT_MACHINE = BOTS_FIRST_MACHINE,
};

/* each machine's player, by absolute index */
enum
{
	HOST_PLAYER = 0,
	OPENH1_PLAYER = 1,
	OTHER_PLAYER = 2,
	OPENCE_PLAYER = 3,
	OLDER_PLAYER = 4,
	BOT_PLAYER = 5,
	NUMBER_OF_TEST_PLAYERS,
};

/* (8 bytes, as on the wire: network_distributed.h asserts it) */
struct distributed_message_header
{
	word header;
	byte type;
	byte count;
	int game_time;
};

struct player_datum
{
	short identifier;
	short local_player_index;
	wchar_t name[12];
	struct
	{
		char machine_index;
	} network_player_data;
};

static short connection = _game_connection_network_server;
static long agreed_rate = 60;
static boolean online = TRUE;
static unsigned long now = 1000;
static struct player_datum players[MAXIMUM_TRACKED_PLAYERS];
static boolean machine_present[MACHINES];
static boolean muted[MACHINES];
/* (a client: which players its host says are OpenCE's) */
static boolean opence_heard[MAXIMUM_TRACKED_PLAYERS];
static boolean in_game = TRUE;
static unsigned long keys;
static unsigned long pad;

/* the messages sent: to whom (NONE: the host), of which type, the message */
static struct
{
	long machine_index;
	byte type;
	short count;
	byte bytes[1200];
	word size;
} sent[SENT];
static short sent_count;

/* this machine's party: its link, and the link it was asked to join */
static char party_link[64];
static char joined_link[64];
static short invite_links_asked;

static int config_boolean(const char *name)
{
	return !strcmp(name, "network.online") && online;
}

/* (moves as the setting changes, as config.toml's count does) */
static unsigned long config_changes(void)
{
	return (unsigned long)online;
}

static unsigned long system_milliseconds(void)
{
	return now;
}

static short game_connection(void)
{
	return connection;
}

static long network_standard_agreed_rate(void)
{
	return agreed_rate;
}

static boolean network_standard_machine_is_opence(long machine_index)
{
	return connection == _game_connection_network_server && machine_index == OPENCE_MACHINE;
}

static boolean network_standard_machine_is_openh1(long machine_index)
{
	return connection == _game_connection_network_server && machine_index >= 0 && machine_index < MACHINES &&
		machine_index != OPENCE_MACHINE;
}

static boolean network_standard_player_is_opence(long player_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

	if (connection == _game_connection_network_server)
		return absolute_index == OPENCE_PLAYER;
	return opence_heard[absolute_index];
}

static boolean game_in_progress(void)
{
	return in_game;
}

static int bots_machine_is_bot(long machine_index)
{
	return machine_index >= BOTS_FIRST_MACHINE && machine_index < BOTS_FIRST_MACHINE + 15;
}

static boolean network_voice_machine_muted(long machine_index)
{
	return machine_index >= 0 && machine_index < MACHINES && muted[machine_index];
}

static struct player_datum *distributed_player(short player_index)
{
	return player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS && players[player_index].identifier ?
		&players[player_index] : NULL;
}

static struct player_datum *player_try_and_get(long player_index)
{
	struct player_datum *player = player_index != NONE ?
		distributed_player((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index)) : NULL;

	return player && DATUM_INDEX_NEW(DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), player->identifier) == player_index ?
		player : NULL;
}

static byte distributed_player_to_byte(long player_index)
{
	return player_index != NONE ? (byte)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NO_PLAYER;
}

static long distributed_player_from_byte(byte player_index)
{
	struct player_datum *player = player_index != NO_PLAYER ? distributed_player(player_index) : NULL;

	return player ? DATUM_INDEX_NEW(player_index, player->identifier) : NONE;
}

static boolean distributed_player_is_local(long player_index)
{
	struct player_datum *player = player_try_and_get(player_index);

	return player && player->local_player_index != NONE;
}

static short distributed_client_machines(long *machine_indices, short maximum)
{
	short count = 0;
	long index;

	for (index = 1; index < MACHINES && count < maximum; index++)
	{
		if (machine_present[index])
			machine_indices[count++] = index;
	}
	return count;
}

static boolean distributed_machine_has_player(long machine_index, short player_index)
{
	return player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS && players[player_index].identifier &&
		players[player_index].network_player_data.machine_index == machine_index;
}

static void sent_note(long machine_index, void *message, byte type, short count, word size)
{
	CHECK(sent_count < SENT && size <= sizeof(sent[0].bytes), "too many messages sent");
	sent[sent_count].machine_index = machine_index;
	sent[sent_count].type = type;
	sent[sent_count].count = count;
	sent[sent_count].size = size;
	memcpy(sent[sent_count].bytes, message, size);
	sent_count++;
}

/* (network_distributed.c's: their own guard, network_standard_message_goes_to,
is test_mixed_lobby.py's; here the sends are recorded as they are asked for) */
static void distributed_send(void *message, byte type, short count, word size, short destination)
{
	CHECK(destination == _distributed_to_host_reliably, "a client's party message goes to the host reliably");
	sent_note(NONE, message, type, count, size);
}

static void distributed_send_to_machine_reliably(long machine_index, void *message, byte type, short count,
	word size)
{
	sent_note(machine_index, message, type, count, size);
}

static short local_player_get_next(short local_player_index)
{
	return local_player_index == NONE ? 0 : NONE;
}

/* (the machine's own player: the test's machine is the host, or the
OpenH1 client) */
static long local_player_get_player_index(short local_player_index)
{
	short index;

	for (index = 0; index < NUMBER_OF_TEST_PLAYERS; index++)
	{
		if (players[index].local_player_index == local_player_index)
			return DATUM_INDEX_NEW(index, players[index].identifier);
	}
	return NONE;
}

static unsigned long halo_keyboard_actions(short controller_index)
{
	(void)controller_index;
	return keys;
}

static unsigned long halo_chat_pad_buttons(void)
{
	return pad;
}

static boolean ui_widgets_active(void)
{
	return FALSE;
}

static boolean console_is_active(void)
{
	return FALSE;
}

static boolean network_chat_typing(void)
{
	return FALSE;
}

static unsigned char network_chat_keyboard_open(void)
{
	return 0;
}

/* (the game's, MSVC's: %s is a wide string) */
static int usnprintf(wchar_t *buffer, long size, wchar_t const *format, ...)
{
	va_list arguments;
	long length = 0;

	va_start(arguments, format);
	for (; *format && length < size - 1; format++)
	{
		if (format[0] == '%' && format[1] == 's')
		{
			wchar_t const *text = va_arg(arguments, wchar_t const *);

			while (*text && length < size - 1)
				buffer[length++] = *text++;
			format++;
		}
		else if (format[0] == '%' && format[1] == '%')
		{
			buffer[length++] = '%';
			format++;
		}
		else
			buffer[length++] = *format;
	}
	buffer[length] = 0;
	va_end(arguments);
	return (int)length;
}

/* p2p_party.c's */
/* this machine's party's other members, as the party tells a leader: the
hashes of their tunnel keys */
static unsigned char party_members[16 * 16];
static int party_member_count;

static int p2p_party_member_key_hashes(unsigned char *hashes, int maximum_count)
{
	int count = party_member_count < maximum_count ? party_member_count : maximum_count;

	memcpy(hashes, party_members, (size_t)count * 16);
	return count;
}

/* the machines of the internet: their addresses (as the game keeps them,
10.0.0.<machine>) and their tunnel keys' hashes (each byte 0x40 + the
machine); 192.168.0.1 a machine of this network */
#define INTERNET_ADDRESS(machine) (0x0A000000UL | (unsigned long)(machine))
#define LAN_ADDRESS 0xC0A80001UL

static void key_hash_of(long machine_index, unsigned char *key_hash)
{
	memset(key_hash, 0x40 + (int)machine_index, 16);
}

static int p2p_peer_key_hash(unsigned long virtual_address, unsigned char *identifier, unsigned char *key_hash)
{
	unsigned char const *bytes = (unsigned char const *)&virtual_address;
	unsigned long address = (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 |
		(unsigned long)bytes[2] << 8 | bytes[3];

	if ((address & 0xFF000000UL) != 0x0A000000UL)
		return 0;
	memset(identifier, 0, 6);
	key_hash_of((long)(address & 0xFF), key_hash);
	return 1;
}

static const unsigned char *p2p_identifier(void)
{
	static unsigned char identifier[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

	return identifier;
}

/* the host's game, as its server has it: its people and the most it takes
of them, and its bots, apart (network_game_server_port_bots_humans) */
struct network_game_server
{
	int unused;
};

static struct
{
	short player_count;
	short maximum_players;
	short bots;
} host_game = { 5, 16, 0 };
static struct network_game_server host_server;

static struct network_game_server *global_network_game_server_get(void)
{
	return connection == _game_connection_network_server ? &host_server : NULL;
}

static long network_game_server_port_bots_humans(struct network_game_server *server, long *bots, long *maximum)
{
	(void)server;
	if (bots)
		*bots = host_game.bots;
	if (maximum)
		*maximum = host_game.maximum_players;
	return host_game.player_count;
}

/* the game's record (the lobby's players, for the host's list of their
friend codes by their slots): none here, the players' datums the test's */
struct network_player
{
	wchar_t name[12];
	short primary_color_index;
	short icon_index;
	char machine_index;
	char controller_index;
	char team_index;
	char player_list_index;
};

struct network_game
{
	struct network_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
};

static struct network_game *network_game_server_get_game(struct network_game_server *server)
{
	(void)server;
	return NULL;
}

static boolean network_player_is_valid(struct network_player const *player)
{
	return player && player->name[0];
}

static unsigned long network_game_server_machine_address(long machine_index)
{
	return machine_index > 0 && machine_index < MACHINES ? INTERNET_ADDRESS(machine_index) : 0;
}

static short network_game_client_get_local_machine_index(void)
{
	return connection == _game_connection_network_server ? HOST_MACHINE : OPENH1_MACHINE;
}

/* this machine's client, and whether its host has accepted it (its join) */
struct network_game_client
{
	int unused;
};

static struct network_game_client this_client;
static boolean client_accepted = TRUE;

static struct network_game_client *global_network_game_client_get(void)
{
	return &this_client;
}

static short network_game_client_get_machine_index(struct network_game_client *client)
{
	(void)client;
	return client_accepted ? network_game_client_get_local_machine_index() : NONE;
}

static int p2p_party_invite_link(char *link, int size)
{
	invite_links_asked++;
	if (!party_link[0])
		snprintf(party_link, sizeof(party_link), "openh1://party/00112233445566778899aabbccddeeff");
	snprintf(link, (size_t)size, "%s", party_link);
	return 1;
}

static int p2p_party_join_link(const char *text)
{
	snprintf(joined_link, sizeof(joined_link), "%s", text);
	return 1;
}

static int p2p_party_parse_link(const char *text, unsigned char *token)
{
	const char *hex = strstr(text, "openh1://party/");
	int index;

	if (!hex || strlen(hex + 15) < 32)
		return 0;
	for (index = 0; index < 16; index++)
	{
		unsigned int value;

		sscanf(hex + 15 + 2 * index, "%2x", &value);
		token[index] = (unsigned char)value;
	}
	return 1;
}

static unsigned long p2p_party_tag(void)
{
	return party_link[0] ? 0x1234ABCDUL : 0;
}

/* p2p_friends.c's: this machine's profile's code (none: no profile), what
a code is to it, the requests sent, the players noted for the recent ones */
static boolean own_has_code;
static int friend_relation = _p2p_friend_none;
static unsigned char friend_added[P2P_FRIEND_CODE_BYTES];
static char friend_added_name[32];
static short friends_added;
static struct
{
	boolean coded;
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[16];
	boolean opence;
} seen_players[16];
static short seen_count;

static void own_code_of(unsigned char *code)
{
	memset(code, 0xC0, P2P_FRIEND_CODE_BYTES);
}

/* (a machine's player's code: each byte 0xC0 + the machine) */
static void code_of_machine(long machine_index, unsigned char *code)
{
	memset(code, 0xC0 + (int)machine_index, P2P_FRIEND_CODE_BYTES);
}

static int p2p_friends_own_code(unsigned char *code)
{
	if (own_has_code)
		own_code_of(code);
	return own_has_code;
}

static int p2p_friends_relation(const unsigned char *code)
{
	(void)code;
	return friend_relation;
}

static int p2p_friends_add(const unsigned char *code, const char *name)
{
	memcpy(friend_added, code, P2P_FRIEND_CODE_BYTES);
	snprintf(friend_added_name, sizeof(friend_added_name), "%s", name);
	friends_added++;
	return _p2p_friends_done;
}

static void p2p_friends_saw_player(const unsigned char *code, const char *name, int opence, const char *game)
{
	(void)game;
	if (seen_count >= 16)
		return;
	seen_players[seen_count].coded = code != NULL;
	if (code)
		memcpy(seen_players[seen_count].code, code, P2P_FRIEND_CODE_BYTES);
	snprintf(seen_players[seen_count].name, sizeof(seen_players[seen_count].name), "%s", name);
	seen_players[seen_count].opence = opence != 0;
	seen_count++;
}

#include "under_test.inc"

/* ---------- the test's game */

static void player_add(short player_index, long machine_index, boolean local, wchar_t const *name)
{
	memset(&players[player_index], 0, sizeof(players[player_index]));
	players[player_index].identifier = (short)(0x8000 | (0x100 + player_index));
	players[player_index].local_player_index = local ? 0 : NONE;
	players[player_index].network_player_data.machine_index = (char)machine_index;
	wcsncpy(players[player_index].name, name, 12);
	if (machine_index < MACHINES)
		machine_present[machine_index] = machine_index != HOST_MACHINE;
}

static long datum(short player_index)
{
	return DATUM_INDEX_NEW(player_index, players[player_index].identifier);
}

/* the game: this machine the host (or the OpenH1 client) */
static void game(short as)
{
	memset(players, 0, sizeof(players));
	memset(machine_present, 0, sizeof(machine_present));
	memset(muted, 0, sizeof(muted));
	memset(opence_heard, 0, sizeof(opence_heard));
	connection = as;
	agreed_rate = 60;
	online = TRUE;
	in_game = TRUE;
	keys = pad = 0;
	party_link[0] = joined_link[0] = 0;
	invite_links_asked = 0;
	sent_count = 0;
	party_member_count = 0;
	client_accepted = TRUE;
	host_game.player_count = 5;
	host_game.maximum_players = 16;
	host_game.bots = 0;
	own_has_code = FALSE;
	friend_relation = _p2p_friend_none;
	friends_added = 0;
	seen_count = 0;
	player_add(HOST_PLAYER, HOST_MACHINE, as == _game_connection_network_server, L"Hosty");
	player_add(OPENH1_PLAYER, OPENH1_MACHINE, as == _game_connection_network_client, L"Keyes");
	player_add(OTHER_PLAYER, OTHER_MACHINE, FALSE, L"Cortana");
	player_add(OPENCE_PLAYER, OPENCE_MACHINE, FALSE, L"Classic");
	player_add(OLDER_PLAYER, OLDER_MACHINE, FALSE, L"Oldie");
	player_add(BOT_PLAYER, BOT_MACHINE, FALSE, L"Johnson");
	party_reset();
	network_party_update();
}

static void request_make(struct distributed_party_request_message *message, byte type, byte kind, short from,
	short target, byte flags, unsigned long tag, byte token_fill)
{
	memset(message, 0, sizeof(*message));
	message->header.type = type;
	message->header.count = 1;
	message->request.format = PARTY_FORMAT;
	message->request.kind = kind;
	message->request.player_index = (byte)from;
	message->request.target_index = (byte)target;
	message->request.flags = flags;
	party_put_tag(message->request.tag, tag);
	memset(message->request.token, token_fill, sizeof(message->request.token));
}

/* a client's word heard by the host, over its stream or not */
static void host_hears(long machine_index, byte kind, short from, short target, byte flags, unsigned long tag,
	boolean from_stream)
{
	struct distributed_party_request_message message;

	request_make(&message, _distributed_message_party_up, kind, from, target, flags, tag, 0x5A);
	network_party_handle_message(machine_index, (word const *)&message, sizeof(message), from_stream);
}

static void status_told(long machine_index, short player_index, byte flags, unsigned long tag)
{
	host_hears(machine_index, _party_request_status, player_index, NO_PLAYER, flags, tag, TRUE);
}

/* the host's word heard by the client */
static void client_hears(byte kind, short from, short target, byte flags, byte token_fill, boolean from_stream)
{
	struct distributed_party_request_message message;

	request_make(&message, _distributed_message_party_down, kind, from, target, flags, 0, token_fill);
	network_party_handle_message(NONE, (word const *)&message, sizeof(message), from_stream);
}

/* the host's list heard by the client: each player (absolute index) with
its flags and tag */
static void client_hears_list(short count, short const *player_indices, byte const *flags, unsigned long const *tags)
{
	struct distributed_party_players_message message;
	short index;

	memset(&message, 0, sizeof(message));
	message.header.type = _distributed_message_party_players;
	message.header.count = (byte)count;
	for (index = 0; index < count; index++)
	{
		message.players[index].format = PARTY_FORMAT;
		message.players[index].player_index = (byte)player_indices[index];
		message.players[index].flags = flags[index];
		party_put_tag(message.players[index].tag, tags[index]);
	}
	network_party_handle_message(NONE, (word const *)&message,
		(word)(sizeof(message.header) + count * sizeof(message.players[0])), TRUE);
}

static short sent_to(long machine_index, byte type)
{
	short index, count = 0;

	for (index = 0; index < sent_count; index++)
		count += sent[index].machine_index == machine_index && sent[index].type == type;
	return count;
}

static struct distributed_party_request const *sent_request(short index)
{
	return &((struct distributed_party_request_message const *)sent[index].bytes)->request;
}

/* (the host's count of a machine's invites full again) */
static void refill(void)
{
	now += (PARTY_BURST + 1) * PARTY_REFILL_MILLISECONDS;
}

static void frame(unsigned long milliseconds)
{
	now += milliseconds;
	network_party_update();
}

static void press_key(void)
{
	keys = FLAG(HALO_KEYBOARD_PARTY_ACCEPT);
	frame(16);
	keys = 0;
	frame(16);
}

static void press_button(void)
{
	pad = FLAG(HALO_CHAT_PAD_DOWN);
	frame(16);
	pad = 0;
	frame(16);
}

/* ---------- the cases */

#define ONLINE FLAG(_party_status_online_bit)
#define IN_PARTY (FLAG(_party_status_online_bit) | FLAG(_party_status_in_party_bit))

/* what the scoreboard offers whom (on a client, from the host's list) */
static void case_actions(void)
{
	short listed[] = { HOST_PLAYER, OTHER_PLAYER, OPENCE_PLAYER, BOT_PLAYER };
	byte flags[] = { IN_PARTY, ONLINE, ONLINE, ONLINE };
	unsigned long tags[] = { 0x77777777UL, 0, 0, 0 };
	unsigned long invite = FLAG(_network_party_action_invite);
	unsigned long join = FLAG(_network_party_action_join);
	unsigned long accept = FLAG(_network_party_action_accept);

	game(_game_connection_network_client);
	opence_heard[OPENCE_PLAYER] = TRUE;
	client_hears_list(4, listed, flags, tags);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == invite, "an OpenH1 player who can party: Invite");
	CHECK(network_party_player_actions(datum(HOST_PLAYER)) == (invite | join),
		"one in a party: Invite, and Join their party");
	CHECK(network_party_player_actions(datum(OPENCE_PLAYER)) == 0, "never an OpenCE machine's player, even listed");
	CHECK(network_party_player_actions(datum(BOT_PLAYER)) == 0, "never a bot, even listed");
	CHECK(network_party_player_actions(datum(OLDER_PLAYER)) == 0,
		"nor a player whose machine never said it can party (an older OpenH1)");
	CHECK(network_party_player_actions(datum(OPENH1_PLAYER)) == 0, "nor this machine's own player");
	CHECK(network_party_player_actions(NONE) == 0, "nor no player");
	/* (in the same party: nothing to do) */
	snprintf(party_link, sizeof(party_link), "openh1://party/00112233445566778899aabbccddeeff");
	tags[0] = 0x1234ABCDUL;
	client_hears_list(4, listed, flags, tags);
	frame(PARTY_STATUS_MILLISECONDS + 1);
	CHECK(network_party_player_actions(datum(HOST_PLAYER)) == 0, "one in this machine's party: nothing");
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == invite, "the others still: Invite");
	/* (an invite from them: Accept too) */
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0x33, TRUE);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == (invite | accept), "their invite: Accept too");
	/* (this machine offline: nothing) */
	online = FALSE;
	frame(PARTY_STATUS_MILLISECONDS + 1);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == 0, "this machine cannot party: nothing");
	/* (a Legacy game: nothing at all, whatever was heard) */
	game(_game_connection_network_client);
	client_hears_list(4, listed, flags, tags);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == invite, "(listed again)");
	agreed_rate = 0;
	frame(16);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == 0, "a Legacy game: nothing");
	/* (a local game: nothing) */
	game(_game_connection_local);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == 0, "a local game: nothing");
	/* (on the host: from its machines' words) */
	game(_game_connection_network_server);
	status_told(OTHER_MACHINE, OTHER_PLAYER, ONLINE, 0);
	status_told(OPENCE_MACHINE, OPENCE_PLAYER, ONLINE, 0);
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) == invite, "the host: an OpenH1 machine that told it");
	CHECK(network_party_player_actions(datum(OPENCE_PLAYER)) == 0, "the host: never an OpenCE machine's player");
	CHECK(network_party_player_actions(datum(OPENH1_PLAYER)) == 0, "the host: not one whose machine never told it");
}

/* the host: an invite sent on, only to the machine it is for, an OpenH1
one that told it it can party */
static void case_route(void)
{
	short index;

	game(_game_connection_network_server);
	status_told(OPENH1_MACHINE, OPENH1_PLAYER, ONLINE, 0);
	status_told(OTHER_MACHINE, OTHER_PLAYER, ONLINE, 0);
	status_told(OPENCE_MACHINE, OPENCE_PLAYER, ONLINE, 0);
	CHECK(!party_globals.machines[OPENCE_MACHINE].heard, "an OpenCE machine's word is not taken");
	frame(PARTY_LIST_MILLISECONDS + 1);
	sent_count = 0;
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 1 && sent[0].machine_index == OTHER_MACHINE &&
		sent[0].type == _distributed_message_party_down, "sent on to the player's machine alone");
	CHECK(sent[0].type >= FIRST_OPENH1_DISTRIBUTED_MESSAGE, "as one of OpenH1's own messages");
	CHECK(sent_request(0)->format == PARTY_FORMAT && sent_request(0)->kind == _party_request_invite &&
		sent_request(0)->player_index == OPENH1_PLAYER && sent_request(0)->target_index == OTHER_PLAYER &&
		sent_request(0)->token[0] == 0x5A && sent_request(0)->token[PARTY_TOKEN_SIZE - 1] == 0x5A,
		"with its format, from and to whom, and the party's token");
	CHECK(sent[0].size == sizeof(struct distributed_party_request_message) && sent[0].count == 1, "one entry");
	sent_count = 0;
	refill();
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, OPENCE_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0, "never to an OpenCE machine");
	refill();
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, OLDER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0, "nor to a machine that never told it it can party");
	refill();
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, BOT_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0, "nor to a bot");
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, OTHER_PLAYER, 0, 0, FALSE);
	CHECK(sent_count == 0, "not from a datagram (anyone could send one as from the machine)");
	host_hears(OPENH1_MACHINE, _party_request_invite, HOST_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0, "nor as from another machine's player");
	host_hears(OPENCE_MACHINE, _party_request_invite, OPENCE_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0, "nor from an OpenCE machine");
	{
		struct distributed_party_request_message message;

		request_make(&message, _distributed_message_party_up, _party_request_invite, OPENH1_PLAYER, OTHER_PLAYER, 0,
			0, 0x5A);
		message.request.format = PARTY_FORMAT + 1;
		network_party_handle_message(OPENH1_MACHINE, (word const *)&message, sizeof(message), TRUE);
		CHECK(sent_count == 0, "nor of another format");
		request_make(&message, _distributed_message_party_up, _party_request_invite, OPENH1_PLAYER, OTHER_PLAYER, 0,
			0, 0x5A);
		network_party_handle_message(OPENH1_MACHINE, (word const *)&message, sizeof(message) - 1, TRUE);
		CHECK(sent_count == 0, "nor of another size");
		message.header.type = _distributed_message_party_down;
		network_party_handle_message(OPENH1_MACHINE, (word const *)&message, sizeof(message), TRUE);
		CHECK(sent_count == 0, "nor one of the host's from a client");
	}
	/* (counted: a burst, then one each so often) */
	refill();
	for (index = 0; index < PARTY_BURST + 2; index++)
		host_hears(OPENH1_MACHINE, _party_request_ask, OPENH1_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == PARTY_BURST, "a burst at most: %d sent", sent_count);
	now += PARTY_REFILL_MILLISECONDS - 1;
	host_hears(OPENH1_MACHINE, _party_request_ask, OPENH1_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == PARTY_BURST, "none more till its time");
	now += 1;
	host_hears(OPENH1_MACHINE, _party_request_ask, OPENH1_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == PARTY_BURST + 1, "then one more");
	/* (to the host's own player: taken there) */
	sent_count = 0;
	refill();
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, HOST_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0 && party_globals.prompt.kind == _party_request_invite &&
		party_globals.prompt.player_index == datum(OPENH1_PLAYER), "the host's own player is invited there");
	/* (a machine gone, and another in its slot: its word let go) */
	machine_present[OTHER_MACHINE] = FALSE;
	frame(PARTY_LIST_MILLISECONDS + 1);
	machine_present[OTHER_MACHINE] = TRUE;
	sent_count = 0;
	refill();
	host_hears(OPENH1_MACHINE, _party_request_invite, OPENH1_PLAYER, OTHER_PLAYER, 0, 0, TRUE);
	CHECK(sent_count == 0, "a machine new to the slot is sent nothing till it tells");
}

/* the host's list: who can party, only to the OpenH1 machines that told it,
when it changes */
static void case_list(void)
{
	short index;
	struct distributed_party_players_message const *message;

	game(_game_connection_network_server);
	status_told(OPENH1_MACHINE, OPENH1_PLAYER, ONLINE, 0);
	status_told(OTHER_MACHINE, OTHER_PLAYER, IN_PARTY, 0xCAFEF00DUL);
	status_told(OPENCE_MACHINE, OPENCE_PLAYER, ONLINE, 0);
	/* (whatever the host held for an OpenCE machine's slot: nothing goes to
	it, and its player is not listed) */
	party_globals.machines[OPENCE_MACHINE].heard = TRUE;
	party_globals.machines[OPENCE_MACHINE].flags = ONLINE;
	sent_count = 0;
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(sent_to(OPENH1_MACHINE, _distributed_message_party_players) == 1 &&
		sent_to(OTHER_MACHINE, _distributed_message_party_players) == 1, "sent to the machines that told it");
	CHECK(sent_to(OPENCE_MACHINE, _distributed_message_party_players) == 0, "never to an OpenCE machine");
	CHECK(sent_to(OLDER_MACHINE, _distributed_message_party_players) == 0, "nor to an older OpenH1 one");
	message = (struct distributed_party_players_message const *)sent[0].bytes;
	CHECK(sent[0].count == 3 && sent[0].size == sizeof(message->header) + 3 * sizeof(message->players[0]),
		"the host's player and the two that can party: %d", sent[0].count);
	for (index = 0; index < sent[0].count; index++)
	{
		byte player_index = message->players[index].player_index;

		CHECK(message->players[index].format == PARTY_FORMAT, "each of the format");
		CHECK(player_index == HOST_PLAYER || player_index == OPENH1_PLAYER || player_index == OTHER_PLAYER,
			"no OpenCE player, older OpenH1 player or bot: %d", player_index);
		if (player_index == OTHER_PLAYER)
		{
			CHECK(message->players[index].flags == IN_PARTY &&
				party_get_tag(message->players[index].tag) == 0xCAFEF00DUL, "in a party, with its tag");
		}
	}
	sent_count = 0;
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(sent_count == 0, "not again, unchanged");
	status_told(OPENH1_MACHINE, OPENH1_PLAYER, IN_PARTY, 0x11111111UL);
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(sent_to(OPENH1_MACHINE, _distributed_message_party_players) == 1 &&
		sent_to(OTHER_MACHINE, _distributed_message_party_players) == 1 && sent_count == 2, "again, changed");
	/* (a client: the list taken whole, over the host's stream alone) */
	game(_game_connection_network_client);
	{
		short listed[] = { OTHER_PLAYER };
		byte flags[] = { ONLINE };
		unsigned long tags[] = { 0 };
		struct distributed_party_players_message copy;

		client_hears_list(1, listed, flags, tags);
		CHECK(party_globals.players[OTHER_PLAYER].flags == ONLINE, "a client takes the host's list");
		memset(&copy, 0, sizeof(copy));
		copy.header.type = _distributed_message_party_players;
		copy.header.count = 0;
		network_party_handle_message(NONE, (word const *)&copy, sizeof(copy.header), FALSE);
		CHECK(party_globals.players[OTHER_PLAYER].flags == ONLINE, "but not from a datagram");
		network_party_handle_message(NONE, (word const *)&copy, sizeof(copy.header), TRUE);
		CHECK(party_globals.players[OTHER_PLAYER].flags == 0, "an empty list: no one");
	}
}

/* a client's word to the host: what it can, when it changes, and only in
an OpenH1 game */
static void case_status(void)
{
	game(_game_connection_network_client);
	CHECK(sent_count == 1 && sent[0].machine_index == NONE && sent[0].type == _distributed_message_party_up &&
		sent_request(0)->kind == _party_request_status && sent_request(0)->flags == ONLINE &&
		sent_request(0)->player_index == OPENH1_PLAYER, "told the host it can party");
	frame(PARTY_STATUS_MILLISECONDS + 1);
	CHECK(sent_count == 1, "not again, unchanged");
	snprintf(party_link, sizeof(party_link), "openh1://party/00112233445566778899aabbccddeeff");
	frame(PARTY_STATUS_MILLISECONDS + 1);
	CHECK(sent_count == 2 && sent_request(1)->flags == IN_PARTY && party_get_tag(sent_request(1)->tag) == 0x1234ABCDUL,
		"in a party, with its tag");
	{
		static byte const none[PARTY_TOKEN_SIZE];

		CHECK(!memcmp(sent_request(1)->token, none, PARTY_TOKEN_SIZE), "never the link in a status");
	}
	frame(PARTY_RETELL_MILLISECONDS + 1);
	CHECK(sent_count == 3, "told again in a while");
	/* (a Legacy game, where the host may be OpenCE's: nothing) */
	agreed_rate = 0;
	sent_count = 0;
	frame(PARTY_RETELL_MILLISECONDS + 1);
	frame(PARTY_RETELL_MILLISECONDS + 1);
	CHECK(sent_count == 0, "nothing sent in a Legacy game");
}

/* a client invited: the line, the key, and the party joined by its link */
static void case_accept(void)
{
	game(_game_connection_network_client);
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xAB, TRUE);
	CHECK(party_globals.prompt.kind == _party_request_invite &&
		party_globals.prompt.player_index == datum(OTHER_PLAYER) &&
		!wcscmp(party_globals.prompt.name, L"Cortana"), "the invite to answer, from Cortana");
	press_key();
	CHECK(!strcmp(joined_link, "openh1://party/abababababababababababababababab"),
		"its key joins the party by its link: %s", joined_link);
	CHECK(party_globals.prompt.kind == NONE, "answered");
	/* (the controller's D-pad Down) */
	joined_link[0] = 0;
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xCD, TRUE);
	press_button();
	CHECK(!strcmp(joined_link, "openh1://party/cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd"), "and the D-pad Down: %s", joined_link);
	/* (ignored: it fades, and the key then does nothing) */
	joined_link[0] = 0;
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xEF, TRUE);
	frame(PARTY_PROMPT_MILLISECONDS + 1);
	press_key();
	CHECK(!joined_link[0], "an invite let go is not taken later");
	/* (not taken: from a datagram, a muted player, for another's player, no
	token, this machine offline, not in a game) */
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xAB, FALSE);
	CHECK(party_globals.prompt.kind == NONE, "not from a datagram");
	muted[OTHER_MACHINE] = TRUE;
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xAB, TRUE);
	CHECK(party_globals.prompt.kind == NONE, "not from a player muted here");
	muted[OTHER_MACHINE] = FALSE;
	client_hears(_party_request_invite, OTHER_PLAYER, HOST_PLAYER, 0, 0xAB, TRUE);
	CHECK(party_globals.prompt.kind == NONE, "not one for another machine's player");
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0, TRUE);
	CHECK(party_globals.prompt.kind == NONE, "not one without a party");
	client_hears(_party_request_invite, OPENH1_PLAYER, OPENH1_PLAYER, 0, 0xAB, TRUE);
	CHECK(party_globals.prompt.kind == NONE, "not one from itself");
	online = FALSE;
	frame(PARTY_STATUS_MILLISECONDS + 1);
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xAB, TRUE);
	CHECK(party_globals.prompt.kind == NONE, "not while this machine cannot party");
	online = TRUE;
	frame(PARTY_STATUS_MILLISECONDS + 1);
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, 0, 0xAB, TRUE);
	in_game = FALSE;
	press_key();
	CHECK(!joined_link[0], "the key does nothing outside the game");
	in_game = TRUE;
	CHECK(network_party_accept() && joined_link[0], "the scoreboard's Accept joins too");
	/* (the answer to this machine's ask: joined at once; one not asked for
	is an invite like any other) */
	joined_link[0] = 0;
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, FLAG(_party_invite_answer_bit), 0x12, TRUE);
	CHECK(!joined_link[0] && party_globals.prompt.kind == _party_request_invite, "an answer not asked for: asked");
	party_globals.prompt.kind = NONE;
	{
		short listed[] = { OTHER_PLAYER };
		byte flags[] = { IN_PARTY };
		unsigned long tags[] = { 0x55555555UL };

		client_hears_list(1, listed, flags, tags);
	}
	sent_count = 0;
	network_party_player_act(datum(OTHER_PLAYER), _network_party_action_join);
	CHECK(sent_count == 1 && sent_request(0)->kind == _party_request_ask &&
		sent_request(0)->target_index == OTHER_PLAYER && sent_request(0)->token[0] == 0 && !invite_links_asked,
		"Join their party asks them (no link of this machine's sent)");
	client_hears(_party_request_invite, HOST_PLAYER, OPENH1_PLAYER, FLAG(_party_invite_answer_bit), 0x34, TRUE);
	CHECK(!joined_link[0], "another player's answer is not this ask's");
	party_globals.prompt.kind = NONE;
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, FLAG(_party_invite_answer_bit), 0x56, TRUE);
	CHECK(!strcmp(joined_link, "openh1://party/56565656565656565656565656565656"), "their answer: joined at once");
	joined_link[0] = 0;
	client_hears(_party_request_invite, OTHER_PLAYER, OPENH1_PLAYER, FLAG(_party_invite_answer_bit), 0x78, TRUE);
	CHECK(!joined_link[0], "once");
}

/* the invites this machine sends: Invite to party (a party made for it),
and an ask let in */
static void case_invite(void)
{
	short listed[] = { OTHER_PLAYER };
	byte flags[] = { ONLINE };
	unsigned long tags[] = { 0 };

	game(_game_connection_network_client);
	client_hears_list(1, listed, flags, tags);
	sent_count = 0;
	network_party_player_act(datum(OTHER_PLAYER), _network_party_action_invite);
	CHECK(invite_links_asked == 1, "the party's link (made if none)");
	CHECK(sent_count == 1 && sent[0].machine_index == NONE && sent[0].type == _distributed_message_party_up &&
		sent_request(0)->kind == _party_request_invite && sent_request(0)->player_index == OPENH1_PLAYER &&
		sent_request(0)->target_index == OTHER_PLAYER && sent_request(0)->token[0] == 0x00 &&
		sent_request(0)->token[1] == 0x11 && sent_request(0)->token[15] == 0xFF &&
		!TEST_FLAG(sent_request(0)->flags, _party_invite_answer_bit), "the invite to the host, with the token");
	frame(16);
	CHECK(sent_count == 2 && sent_request(1)->kind == _party_request_status && sent_request(1)->flags == IN_PARTY,
		"and the host told this machine is in a party now");
	sent_count = 0;
	network_party_player_act(datum(OPENCE_PLAYER), _network_party_action_invite);
	network_party_player_act(datum(OLDER_PLAYER), _network_party_action_invite);
	CHECK(sent_count == 0, "no invite to a player not offered one");
	/* (an ask let in: the invite, its answer) */
	client_hears(_party_request_ask, OTHER_PLAYER, OPENH1_PLAYER, 0, 0, TRUE);
	CHECK(party_globals.prompt.kind == _party_request_ask, "Cortana asks to join");
	press_key();
	CHECK(sent_count == 1 && sent_request(0)->kind == _party_request_invite &&
		TEST_FLAG(sent_request(0)->flags, _party_invite_answer_bit) && sent_request(0)->target_index == OTHER_PLAYER &&
		sent_request(0)->token[1] == 0x11, "let in: the invite, as the answer");
	CHECK(!joined_link[0], "this machine joins nothing");
	/* (the host's own invite: sent on, from no machine) */
	game(_game_connection_network_server);
	status_told(OTHER_MACHINE, OTHER_PLAYER, ONLINE, 0);
	frame(PARTY_LIST_MILLISECONDS + 1);
	sent_count = 0;
	network_party_player_act(datum(OTHER_PLAYER), _network_party_action_invite);
	CHECK(sent_count == 1 && sent[0].machine_index == OTHER_MACHINE &&
		sent[0].type == _distributed_message_party_down &&
		sent_request(0)->player_index == HOST_PLAYER, "the host's invite goes to the machine");
}

/* the party's members (machines first to first + count - 1) as the party
tells this machine, its leader */
static void members_are(long first, short count)
{
	short index;

	party_member_count = count;
	for (index = 0; index < count; index++)
		key_hash_of(first + index, party_members + index * 16);
}

/* the last message sent of a type, its index; NONE */
static short sent_last(byte type)
{
	short index;

	for (index = (short)(sent_count - 1); index >= 0; index--)
	{
		if (sent[index].type == type)
			return index;
	}
	return NONE;
}

/* a leader's ask for its members (machines first to first + count - 1)
heard by the host, over its stream or not */
static void host_hears_hold(long machine_index, long first, short count, boolean from_stream)
{
	struct distributed_party_hold_message message;
	short index;

	memset(&message, 0, sizeof(message));
	message.header.type = _distributed_message_party_hold;
	message.header.count = (byte)count;
	for (index = 0; index < count; index++)
	{
		message.members[index].format = PARTY_HOLD_FORMAT;
		key_hash_of(first + index, message.members[index].key_hash);
	}
	network_party_handle_message(machine_index, (word const *)&message,
		(word)(sizeof(message.header) + count * sizeof(message.members[0])), from_stream);
}

/* whether the machine at the address may join the host's game of players
of most (its people, the bots apart) */
static boolean slot_open(unsigned long address, short players, short maximum)
{
	host_game.player_count = players;
	host_game.maximum_players = maximum;
	return network_party_slot_open(address);
}

static struct distributed_party_held const *sent_answer(short index)
{
	return &((struct distributed_party_held_message const *)sent[index].bytes)->answer;
}

static void case_holds(void)
{
	short index;

	/* a client leading a party asks its host to hold room for its members,
	once the host has taken it in */
	game(_game_connection_network_client);
	client_accepted = FALSE;
	members_are(5, 2);
	sent_count = 0;
	frame(PARTY_HOLD_READ_MILLISECONDS + 1);
	CHECK(sent_last(_distributed_message_party_hold) == NONE, "not while its join is not taken yet");
	client_accepted = TRUE;
	frame(PARTY_HOLD_READ_MILLISECONDS + 1);
	index = sent_last(_distributed_message_party_hold);
	CHECK(index != NONE && sent[index].machine_index == NONE && sent[index].count == 2 &&
		sent[index].size == 8 + 2 * sizeof(struct distributed_party_hold), "the leader asks the host for its two");
	{
		struct distributed_party_hold_message const *message = (void const *)sent[index].bytes;
		unsigned char key_hash[16];

		key_hash_of(6, key_hash);
		CHECK(message->members[0].format == PARTY_HOLD_FORMAT && !memcmp(message->members[1].key_hash, key_hash, 16),
			"each by its tunnel key's hash, with its format");
	}
	CHECK(_distributed_message_party_hold >= FIRST_OPENH1_DISTRIBUTED_MESSAGE, "as one of OpenH1's own messages");
	sent_count = 0;
	frame(PARTY_HOLD_ASK_MILLISECONDS + 1);
	CHECK(sent_last(_distributed_message_party_hold) == NONE, "not again at once while its members are the same");
	frame(PARTY_HOLD_RETRY_MILLISECONDS);
	CHECK(sent_last(_distributed_message_party_hold) != NONE, "asked again, the host not answering");
	frame(PARTY_HOLD_RETRY_MILLISECONDS + 1);
	sent_count = 0;
	frame(PARTY_HOLD_RETRY_MILLISECONDS + 1);
	frame(PARTY_HOLD_RETRY_MILLISECONDS + 1);
	CHECK(sent_last(_distributed_message_party_hold) == NONE, "but PARTY_HOLD_ASKS times at most");
	members_are(5, 3);
	frame(PARTY_HOLD_ASK_MILLISECONDS + 1);
	index = sent_last(_distributed_message_party_hold);
	CHECK(index != NONE && sent[index].count == 3, "again when they change");
	{
		struct distributed_party_held_message answer;

		memset(&answer, 0, sizeof(answer));
		answer.header.type = _distributed_message_party_held;
		answer.header.count = 1;
		answer.answer.format = PARTY_HOLD_FORMAT;
		answer.answer.asked = 3;
		answer.answer.held = 3;
		network_party_handle_message(NONE, (word const *)&answer, sizeof(answer), TRUE);
	}
	sent_count = 0;
	frame(PARTY_HOLD_RETRY_MILLISECONDS + 1);
	CHECK(sent_last(_distributed_message_party_hold) == NONE, "not again once answered");
	game(_game_connection_network_client);
	sent_count = 0;
	frame(PARTY_HOLD_READ_MILLISECONDS + 1);
	CHECK(sent_last(_distributed_message_party_hold) == NONE, "a member, or a party of one, asks for none");
	agreed_rate = 0;
	members_are(5, 2);
	frame(PARTY_HOLD_ASK_MILLISECONDS + 1);
	CHECK(sent_last(_distributed_message_party_hold) == NONE, "nor anyone in a Legacy game");

	/* the host: a game of 7 with 5 players holds room for 2 of the 3 */
	game(_game_connection_network_server);
	host_game.player_count = 5;
	host_game.maximum_players = 7;
	sent_count = 0;
	host_hears_hold(OPENH1_MACHINE, 5, 3, TRUE);
	index = sent_last(_distributed_message_party_held);
	CHECK(index != NONE && sent[index].machine_index == OPENH1_MACHINE && sent_answer(index)->format ==
		PARTY_HOLD_FORMAT && sent_answer(index)->asked == 3 && sent_answer(index)->held == 2 &&
		sent_answer(index)->seconds == PARTY_HOLD_MILLISECONDS / 1000, "answered: 2 of the 3 held, for how long");
	CHECK(network_party_held_slots() == 2, "the advertisement counts the 2");
	CHECK(slot_open(INTERNET_ADDRESS(5), 5, 7) && slot_open(INTERNET_ADDRESS(6), 5, 7),
		"the members held may join");
	CHECK(!slot_open(INTERNET_ADDRESS(7), 5, 7), "the third, not held, finds the game full");
	CHECK(!slot_open(LAN_ADDRESS, 5, 7), "as does a machine of this network");
	CHECK(network_party_machines_together(NONE, INTERNET_ADDRESS(5), OPENH1_MACHINE),
		"a member joining is of its leader's party");
	CHECK(!network_party_machines_together(NONE, INTERNET_ADDRESS(5), OTHER_MACHINE), "and not of another");
	CHECK(network_party_machine_in_party(OPENH1_MACHINE) && !network_party_machine_in_party(OTHER_MACHINE),
		"the leader's machine is in a party");
	/* (machine 5's player added) */
	machine_present[5] = TRUE;
	network_party_slot_taken(5, INTERNET_ADDRESS(5));
	CHECK(network_party_held_slots() == 1, "its slot taken up");
	CHECK(network_party_machines_together(5, INTERNET_ADDRESS(5), OPENH1_MACHINE) &&
		network_party_machine_in_party(5), "and it one of its leader's party from now on");
	CHECK(slot_open(INTERNET_ADDRESS(7), 5, 7), "a slot beyond the one held is anyone's");
	/* (asks no machine may make) */
	sent_count = 0;
	host_hears_hold(OPENCE_MACHINE, 1, 1, TRUE);
	CHECK(sent_last(_distributed_message_party_held) == NONE && network_party_held_slots() == 1,
		"an OpenCE machine's ask is not taken");
	host_hears_hold(OTHER_MACHINE, 1, 1, FALSE);
	CHECK(sent_last(_distributed_message_party_held) == NONE && network_party_held_slots() == 1,
		"nor one that came as a datagram");
	/* (a member in the game already is not held again) */
	host_hears_hold(OPENH1_MACHINE, 5, 2, TRUE);
	index = sent_last(_distributed_message_party_held);
	CHECK(index != NONE && sent_answer(index)->held == 1 && network_party_held_slots() == 1,
		"the party's ask again: its earlier holds replaced, its member in the game not held");
	host_hears_hold(OPENH1_MACHINE, 5, 2, TRUE);
	index = sent_last(_distributed_message_party_held);
	CHECK(PARTY_HOLD_GRANTS == 3 && index != NONE && sent_answer(index)->held == 1, "a third ask held too");
	host_hears_hold(OPENH1_MACHINE, 5, 2, TRUE);
	index = sent_last(_distributed_message_party_held);
	CHECK(index != NONE && sent_answer(index)->held == 0 && network_party_held_slots() == 1,
		"no more than PARTY_HOLD_GRANTS asks a game (the last held kept)");
	/* lapsing */
	frame(PARTY_HOLD_MILLISECONDS - 1000);
	CHECK(network_party_held_slots() == 1, "kept a while");
	frame(1001);
	CHECK(network_party_held_slots() == 0, "let go when its member has not come");
	CHECK(slot_open(INTERNET_ADDRESS(7), 6, 7), "its slot anyone's again");
	/* the leader gone: its party's holds let go */
	game(_game_connection_network_server);
	host_game.player_count = 5;
	host_game.maximum_players = 16;
	host_hears_hold(OPENH1_MACHINE, 5, 2, TRUE);
	CHECK(network_party_held_slots() == 2, "held");
	machine_present[OPENH1_MACHINE] = FALSE;
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(network_party_held_slots() == 0, "the leader left: its party's holds let go");
	/* the host's own party: held without asking */
	game(_game_connection_network_server);
	members_are(6, 2);
	sent_count = 0;
	frame(PARTY_HOLD_READ_MILLISECONDS + 1);
	CHECK(network_party_held_slots() == 2 && sent_last(_distributed_message_party_hold) == NONE,
		"a host leading a party holds room for its members itself");
	CHECK(network_party_machines_together(NONE, INTERNET_ADDRESS(6), HOST_MACHINE),
		"its members are of the host's party");
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(network_party_held_slots() == 2, "kept while the host is in its game");
	/* nothing in a Legacy game */
	game(_game_connection_network_server);
	agreed_rate = 0;
	frame(PARTY_LIST_MILLISECONDS + 1);
	host_hears_hold(OPENH1_MACHINE, 5, 2, TRUE);
	CHECK(network_party_held_slots() == 0 && slot_open(LAN_ADDRESS, 15, 16),
		"a Legacy game's host holds nothing");
	/* bots: a game of 16 the bots fill (12 people and 4 bots that make room
	for players) holds room for a party of 3, its people counted alone */
	game(_game_connection_network_server);
	host_game.player_count = 12;
	host_game.maximum_players = 16;
	host_game.bots = 4;
	host_hears_hold(OPENH1_MACHINE, 5, 3, TRUE);
	CHECK(network_party_held_slots() == 3, "a game the bots fill holds room for a party: the bots make room");
	CHECK(slot_open(INTERNET_ADDRESS(5), 12, 16), "its member is let in, a bot to leave for it");
	CHECK(slot_open(INTERNET_ADDRESS(9), 12, 16), "and a stranger the last slot");
	CHECK(!slot_open(INTERNET_ADDRESS(9), 13, 16), "but no slot held");
	/* the team for a party's player */
	{
		short party_counts[2] = { 0, 3 }, team_counts[2] = { 4, 3 };

		CHECK(network_party_team_choice(party_counts, team_counts, 16) == 1, "its party's team");
	}
}

/* a client's word with its player's friend code (flag: its friend code
bit; the token carries the code) */
static void status_told_code(long machine_index, short player_index, byte flags)
{
	struct distributed_party_request_message message;

	request_make(&message, _distributed_message_party_up, _party_request_status, player_index, NO_PLAYER, flags, 0,
		0);
	code_of_machine(machine_index, message.request.token);
	network_party_handle_message(machine_index, (word const *)&message, sizeof(message), TRUE);
}

/* the host's friends' list heard by the client: each player (absolute
index) with its machine's code */
static void client_hears_codes(short count, short const *player_indices, long const *machines, byte format,
	boolean from_stream, short size_change)
{
	struct distributed_friend_players_message message;
	short index;

	memset(&message, 0, sizeof(message));
	message.header.type = _distributed_message_friend_players;
	message.header.count = (byte)count;
	for (index = 0; index < count; index++)
	{
		message.players[index].format = format;
		message.players[index].player_index = (byte)player_indices[index];
		code_of_machine(machines[index], message.players[index].code);
	}
	network_party_handle_message(NONE, (word const *)&message,
		(word)(sizeof(message.header) + count * sizeof(message.players[0]) + size_change), from_stream);
}

/* the player noted for the recent players by its name, NULL if none */
static short seen_index(char const *name)
{
	short index;

	for (index = 0; index < seen_count; index++)
	{
		if (!strcmp(seen_players[index].name, name))
			return index;
	}
	return NONE;
}

/* friends: the host tells its OpenH1 machines that told it each player's
friend code (each client's own in its status), Add friend offered for a
player of a code not a friend, and the game's players noted for the recent
players */
static void case_friends(void)
{
	unsigned long add = FLAG(_network_party_action_add_friend);
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	struct distributed_friend_players_message const *message;
	short index;

	/* (the host: the codes its machines told, to the machines that told it,
	never an OpenCE one) */
	game(_game_connection_network_server);
	own_has_code = TRUE;
	frame(PARTY_STATUS_MILLISECONDS + 1);
	status_told_code(OPENH1_MACHINE, OPENH1_PLAYER, ONLINE | FLAG(_party_status_friend_code_bit));
	status_told_code(OTHER_MACHINE, OTHER_PLAYER, ONLINE);
	status_told_code(OPENCE_MACHINE, OPENCE_PLAYER, ONLINE | FLAG(_party_status_friend_code_bit));
	sent_count = 0;
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(sent_to(OPENH1_MACHINE, _distributed_message_friend_players) == 1 &&
		sent_to(OTHER_MACHINE, _distributed_message_friend_players) == 1, "the codes to the machines that told it");
	CHECK(sent_to(OPENCE_MACHINE, _distributed_message_friend_players) == 0 &&
		sent_to(OLDER_MACHINE, _distributed_message_friend_players) == 0, "never to an OpenCE or older machine");
	for (index = 0; index < sent_count && sent[index].type != _distributed_message_friend_players; index++)
		;
	CHECK(index < sent_count && sent[index].type >= FIRST_OPENH1_DISTRIBUTED_MESSAGE, "one of OpenH1's own messages");
	message = (struct distributed_friend_players_message const *)sent[index].bytes;
	CHECK(sent[index].count == 2 && sent[index].size == sizeof(message->header) + 2 * sizeof(message->players[0]),
		"the host's own player's code and the one told with its bit: %d", sent[index].count);
	own_code_of(code);
	CHECK(message->players[0].format == FRIEND_FORMAT && message->players[0].player_index == HOST_PLAYER &&
		!memcmp(message->players[0].code, code, P2P_FRIEND_CODE_BYTES), "the host's own player's code");
	code_of_machine(OPENH1_MACHINE, code);
	CHECK(message->players[1].player_index == OPENH1_PLAYER &&
		!memcmp(message->players[1].code, code, P2P_FRIEND_CODE_BYTES), "the client's, as its status told");
	sent_count = 0;
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(sent_to(OPENH1_MACHINE, _distributed_message_friend_players) == 0, "not again, unchanged");
	status_told_code(OTHER_MACHINE, OTHER_PLAYER, ONLINE | FLAG(_party_status_friend_code_bit));
	frame(PARTY_LIST_MILLISECONDS + 1);
	CHECK(sent_to(OPENH1_MACHINE, _distributed_message_friend_players) == 1, "again, changed");
	/* (the host's scoreboard: Add friend for a coded player not a friend) */
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) & add, "Add friend for a player of a code");
	CHECK(!(network_party_player_actions(datum(OPENCE_PLAYER)) & add), "never for an OpenCE machine's player");
	CHECK(!(network_party_player_actions(datum(OLDER_PLAYER)) & add), "nor one of no code");
	CHECK(!(network_party_player_actions(datum(HOST_PLAYER)) & add), "nor this machine's own");
	friend_relation = _p2p_friend_online;
	CHECK(!(network_party_player_actions(datum(OTHER_PLAYER)) & add), "nor a friend");
	friend_relation = _p2p_friend_sent;
	CHECK(!(network_party_player_actions(datum(OTHER_PLAYER)) & add), "nor one asked already");
	friend_relation = _p2p_friend_blocked;
	CHECK(!(network_party_player_actions(datum(OTHER_PLAYER)) & add), "nor one blocked");
	friend_relation = _p2p_friend_request;
	CHECK(network_party_player_actions(datum(OTHER_PLAYER)) & add, "one who asked: its request accepted");
	friend_relation = _p2p_friend_none;
	network_party_player_act(datum(OTHER_PLAYER), _network_party_action_add_friend);
	code_of_machine(OTHER_MACHINE, code);
	CHECK(friends_added == 1 && !memcmp(friend_added, code, P2P_FRIEND_CODE_BYTES) &&
		!strcmp(friend_added_name, "Cortana"), "Add friend asks for its code, with its name");
	own_has_code = FALSE;
	frame(PARTY_STATUS_MILLISECONDS + 1);
	CHECK(!(network_party_player_actions(datum(OTHER_PLAYER)) & add), "no Add friend with no profile");
	/* (a client: its code in its status; the list taken over the host's
	stream alone, of its format and size) */
	game(_game_connection_network_client);
	own_has_code = TRUE;
	sent_count = 0;
	frame(PARTY_STATUS_MILLISECONDS + 1);
	own_code_of(code);
	CHECK(sent_count == 1 && (sent_request(0)->flags & FLAG(_party_status_friend_code_bit)) &&
		!memcmp(sent_request(0)->token, code, P2P_FRIEND_CODE_BYTES), "its status tells its code");
	{
		static byte const none[PARTY_TOKEN_SIZE];

		CHECK(!memcmp(sent_request(0)->token + P2P_FRIEND_CODE_BYTES, none, PARTY_TOKEN_SIZE - P2P_FRIEND_CODE_BYTES),
			"and nothing else (never the party's link)");
	}
	{
		short listed[] = { HOST_PLAYER, OTHER_PLAYER };
		long machines[] = { HOST_MACHINE, OTHER_MACHINE };

		client_hears_codes(2, listed, machines, FRIEND_FORMAT, FALSE, 0);
		CHECK(!party_globals.codes[OTHER_PLAYER].has, "not from a datagram");
		client_hears_codes(2, listed, machines, FRIEND_FORMAT, TRUE, -1);
		CHECK(!party_globals.codes[OTHER_PLAYER].has, "nor of another size");
		client_hears_codes(2, listed, machines, FRIEND_FORMAT + 2, TRUE, 0);
		CHECK(!party_globals.codes[OTHER_PLAYER].has && !party_globals.slot_codes[OTHER_PLAYER].has,
			"an entry of another format is let go");
		/* (the lobby's: its players by their slots of the game's record, not
		as the game's datums) */
		client_hears_codes(2, listed, machines, FRIEND_FORMAT_LOBBY, TRUE, 0);
		CHECK(!party_globals.codes[OTHER_PLAYER].has && party_globals.slot_codes[OTHER_PLAYER].has,
			"the lobby's list taken by slots");
		client_hears_codes(2, listed, machines, FRIEND_FORMAT, TRUE, 0);
		CHECK(party_globals.codes[OTHER_PLAYER].has && party_globals.codes[HOST_PLAYER].has, "the host's list taken");
		CHECK(network_party_player_actions(datum(OTHER_PLAYER)) & add, "Add friend on a client too");
	}
	/* (the recent players: the others, not bots, OpenCE's said so, codes
	where the host told them; none at the game's start, before the host has
	told the codes) */
	CHECK(seen_count == 0, "none noted at once: %d", seen_count);
	seen_count = 0;
	opence_heard[OPENCE_PLAYER] = TRUE;
	frame(FRIEND_RECENT_MILLISECONDS + 1);
	code_of_machine(OTHER_MACHINE, code);
	CHECK(seen_index("Cortana") != NONE && seen_players[seen_index("Cortana")].coded &&
		!memcmp(seen_players[seen_index("Cortana")].code, code, P2P_FRIEND_CODE_BYTES), "a player noted with its code");
	CHECK(seen_index("Classic") != NONE && !seen_players[seen_index("Classic")].coded &&
		seen_players[seen_index("Classic")].opence, "an OpenCE player noted as such, of no code");
	CHECK(seen_index("Oldie") != NONE && !seen_players[seen_index("Oldie")].coded, "an older OpenH1's, of no code");
	CHECK(seen_index("Keyes") == NONE, "not this machine's own player");
	CHECK(seen_index("Johnson") == NONE, "nor a bot");
	index = seen_count;
	frame(FRIEND_RECENT_MILLISECONDS / 2);
	CHECK(seen_count == index, "not every frame");
	/* (a Legacy game: noted, of no code, whatever was heard) */
	agreed_rate = 0;
	seen_count = 0;
	frame(FRIEND_RECENT_MILLISECONDS + 1);
	CHECK(seen_index("Cortana") != NONE && !seen_players[seen_index("Cortana")].coded, "a Legacy game: no codes");
	/* (a local game: none) */
	game(_game_connection_local);
	seen_count = 0;
	frame(FRIEND_RECENT_MILLISECONDS + 1);
	CHECK(seen_count == 0, "a local game's players are not noted");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("actions") case_actions();
	else CASE("route") case_route();
	else CASE("list") case_list();
	else CASE("status") case_status();
	else CASE("accept") case_accept();
	else CASE("invite") case_invite();
	else CASE("holds") case_holds();
	else CASE("friends") case_friends();
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	printf("%s: passed\n", case_name);
	return 0;
}
