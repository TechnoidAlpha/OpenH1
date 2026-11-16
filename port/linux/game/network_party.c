/*
NETWORK_PARTY.C

Parties made in a network game (the party itself is port/linux/src/
p2p_party.c's): a player picked on the scoreboard (game_engine.c's player
menu) may be invited to this machine's party (one made for it, this machine
its leader, if it is in none), or, in a party of their own, asked to let
this machine into theirs. The player invited sees a line at the top of the
view, "Bob invites you to their party", with the key that joins it
(controls.party_accept, J; a controller's D-pad Down: the key or the button
as the player plays, input.button_prompts), for PARTY_PROMPT_MILLISECONDS;
let go, it fades. A player asked sees "Bob wants to join your party", whose
key sends Bob the invite, which Bob's machine then takes without asking (it
asked). Joining is a party link's (p2p_party_join_link): the same as opening
openh1://party/<token>. The scoreboard's menu has the accept too.

The link goes only to the player it is for, through the game's connection
(port/linux/NETCODE.md), in messages of OpenH1's own (120 and after), of a
format of their own (PARTY_FORMAT: a machine takes its own alone):

- A client tells the host (_distributed_message_party_up, reliably, over
  its own stream) whether it can party (network.online) and is in a party
  (with the party's tag: p2p_party_tag, 32 bits of its topic, by which the
  machines tell who is in the same party; never the link), when that
  changes and every PARTY_RETELL_MILLISECONDS; its invite to a player (with the party's token) and its ask, as
  its player makes them.
- The host takes them only from the machine's own stream, from one of that
  machine's players, at most PARTY_BURST at once and one each
  PARTY_REFILL_MILLISECONDS after; it sends an invite or an ask on to the
  machine of the player it is for (_distributed_message_party_down,
  reliably), or takes it itself (its own player's). It sends it only to an
  OpenH1 machine of this game (network_standard_machine_is_openh1) that
  told it it can party, so never to an OpenCE machine (nor does
  network_standard_message_goes_to let any of OpenH1's messages go to one).
- The host tells the machines that told it they can party, when it changes,
  who can (_distributed_message_party_players, reliably): each such player,
  and whether they are in a party, and its tag. A player of an OpenCE
  machine, a bot, or a machine of an older OpenH1 (which drops these kinds
  as unknown) is never in it, so no machine offers them anything.

All of this is in OpenH1's games alone (network_standard_agreed_rate): in a
Legacy game a client cannot tell whether its host, or another player, is
OpenH1's. A player muted on this machine (network_voice_machine_muted) is
not heard: their invites are let go.

The pregame lobby and the carnage report have no player menu yet: when they
do, network_party_player_actions and network_party_player_act are theirs too.

Room held for a party (NETCODE.md, "Parties in public games"): a party's
leader joins a game first, and its members follow it in
(port/linux/game/party_menu.inc). So that strangers do not take the slots
meanwhile and split the party, the leader, a client of an OpenH1 game,
asks its host to hold room for its members (_distributed_message_party_hold,
reliably, over its own stream: the hash of each member's tunnel key, which
the party knows it by, p2p_party_member_key_hashes, and which the host knows
the machine joining by, p2p_peer_key_hash), and again when its members
change. The host holds as many as it has room for (its slots free of
players and of other parties' holds, and those of bots that would make room
for players), for PARTY_HOLD_MILLISECONDS, and answers with how many
(_distributed_message_party_held). A host that leads a party holds room for
its own members the same way, without asking. Until a member's player is in
the game, or its hold lapses, its slot is its alone: a machine of no hold is
refused as for a full game (network_party_slot_open, at its join and at each
player it adds), and the advertisement and the server browser's listing
count the slots held as taken. A member's player added takes up its hold,
and in a team game the host puts it on its party's team, if that team has
room (network_party_team_choice). A party's machines are told apart by the
holds they took up, and by their party's tag (as told the host). A machine
asks for holds at most PARTY_HOLD_GRANTS times a game, which each replace
the last. A Legacy host, and an OpenH1 one of an older build (which drops
the kinds as unknown), hold nothing: the browser says there is no room
guarantee (p2p_lobby.c's stamp says which hosts hold), and the members join
as they can.

A Discord friend's ask to join (port/linux/src/p2p_discord.c) is shown the
same way, in the game and over the menus alike (network_party_discord_draw,
from ui_widget.c's render_ui_widgets): "Bob asks to join your party", with
its keys as the player plays: the accept's (J) and Esc, else the
controller's D-pad Down and Up. port/linux/src/xinput_sdl.c answers it,
taking those keys from the game and the menus while it is shown.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/players.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "interface/hud.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "main/console.h"
#include "cutscene/cinematics.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "network_voice.h"
#include "network_chat.h"
#include "network_party.h"
#include "halo_keyboard.h"
#include "bots.h"
#include "networking/network_client_manager.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include "networking/network_server_manager_internal.h"
/* (a Discord friend's ask to join: p2p_discord_join_request) */
#include "../src/p2p.h"

#include <stdio.h>

/* the platform layer's, port_config.c's */
unsigned long system_milliseconds(void);
int config_boolean(const char *name);
unsigned long config_changes(void);
/* p2p_party.c's (port/linux/src/p2p.h) */
int p2p_party_invite_link(char *link, int size);
int p2p_party_join_link(const char *text);
int p2p_party_parse_link(const char *text, unsigned char *token);
unsigned long p2p_party_tag(void);
/* network_chat.c's: its on-screen keyboard is up */
unsigned char network_chat_keyboard_open(void);
/* hud_key_prompts.c's */
boolean hud_key_prompt_draw_key(char const *name, rectangle2d *bounds, rectangle2d *cursor, pixel32 color);
void hud_key_prompt_action_name(short action, char *name, long size);

/* ---------- constants */

/* a party's link: this, then its token in hexadecimal (p2p.h's) */
#define PARTY_LINK_PREFIX "openh1://party/"

enum
{
	/* the messages' format: a machine takes its own alone */
	PARTY_FORMAT = 1,
	/* a party's token (p2p.h's P2P_TOKEN_SIZE) and its link, with its end */
	PARTY_TOKEN_SIZE = 16,
	PARTY_LINK_SIZE = sizeof(PARTY_LINK_PREFIX) + 2 * PARTY_TOKEN_SIZE,
	PARTY_NAME_SIZE = 12,
	MAXIMUM_PARTY_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,

	/* what a client's word to the host is, and the host's sent on */
	_party_request_status = 0,
	_party_request_invite,
	_party_request_ask,
	NUMBER_OF_PARTY_REQUESTS,

	/* a status's flags (and the host's list's): the machine can party
	(network.online), is in a party, and its token is its player's friend
	code (port/linux/src/p2p_friends.c: builds before friends send none) */
	_party_status_online_bit = 0,
	_party_status_in_party_bit,
	_party_status_friend_code_bit,
	/* an invite's: the answer to the player's ask */
	_party_invite_answer_bit = 0,

	/* milliseconds: an invite shown, and taken; an ask's answer taken
	without asking; what was done shown; this machine's party read */
	PARTY_PROMPT_MILLISECONDS = 30000,
	PARTY_ASK_MILLISECONDS = 60000,
	PARTY_NOTICE_MILLISECONDS = 5000,
	PARTY_FADE_MILLISECONDS = 1000,
	PARTY_STATUS_MILLISECONDS = 500,
	/* a client's status told again this often (the host lets a machine's
	go while it is not in the game yet) */
	PARTY_RETELL_MILLISECONDS = 5000,
	/* the host: a machine's invites and asks at once, and the time it gains
	another in */
	PARTY_BURST = 3,
	PARTY_REFILL_MILLISECONDS = 5000,
	/* the host: its list made again this often */
	PARTY_LIST_MILLISECONDS = 500,

	/* room held for a party: the messages' format; a tunnel key's hash
	(p2p.h's P2P_PLAYER_KEY_HASH_SIZE); a party's other members (its most,
	less its leader); the holds at once, of every party joining */
	PARTY_HOLD_FORMAT = 1,
	PARTY_KEY_HASH_SIZE = P2P_PLAYER_KEY_HASH_SIZE,
	MAXIMUM_PARTY_HOLD_MEMBERS = P2P_PARTY_MAXIMUM_MEMBERS - 1,
	MAXIMUM_PARTY_HOLDS = 32,
	/* milliseconds a hold waits for its member, from its grant */
	PARTY_HOLD_MILLISECONDS = 20000,
	/* the grants a machine may have in a game (each replacing the last) */
	PARTY_HOLD_GRANTS = 3,
	/* a leader's members read this often, and asked for again (changed) no
	sooner than this; an ask the host has not answered made again this
	often, so many times (a host that holds nothing never answers) */
	PARTY_HOLD_READ_MILLISECONDS = 500,
	PARTY_HOLD_ASK_MILLISECONDS = 2000,
	PARTY_HOLD_RETRY_MILLISECONDS = 5000,
	PARTY_HOLD_ASKS = 3,

	/* friends: the host's list's format, a code (p2p.h's
	P2P_FRIEND_CODE_BYTES), the game's players noted for the recent players
	this often */
	FRIEND_FORMAT = 1,
	/* (the lobby's: an entry's player its slot of the game's record, the
	lobby's players having no datum yet; an older build drops it as another
	format) */
	FRIEND_FORMAT_LOBBY = 2,
	FRIEND_CODE_SIZE = P2P_FRIEND_CODE_BYTES,
	FRIEND_RECENT_MILLISECONDS = 3000,
	/* a friends' notice shown this long */
	FRIEND_TOAST_MILLISECONDS = 5000,
};

/* ---------- structures */

/* a client's word to the host (_distributed_message_party_up), and an
invite or ask the host sends on (_distributed_message_party_down): the
format, what it is, the player it is from (up: one of the sender's own),
the one it is for (NO_PLAYER for a status), the flags, a status's tag (the
party's, network byte order) and an invite's token */
struct distributed_party_request
{
	byte format;
	byte kind;
	byte player_index;
	byte target_index;
	byte flags;
	byte reserved[3];
	byte tag[4];
	byte token[PARTY_TOKEN_SIZE];
};

struct distributed_party_request_message
{
	struct distributed_message_header header;
	struct distributed_party_request request;
};

/* the host's list (_distributed_message_party_players): a player who can
party, an entry each */
struct distributed_party_player
{
	byte format;
	byte player_index;
	byte flags;
	byte reserved;
	byte tag[4];
};

struct distributed_party_players_message
{
	struct distributed_message_header header;
	struct distributed_party_player players[MAXIMUM_TRACKED_PLAYERS];
};

/* a leader's ask of the host (_distributed_message_party_hold): an entry
for each member to hold room for, the hash of its tunnel key */
struct distributed_party_hold
{
	byte format;
	byte reserved[3];
	byte key_hash[PARTY_KEY_HASH_SIZE];
};

struct distributed_party_hold_message
{
	struct distributed_message_header header;
	struct distributed_party_hold members[MAXIMUM_PARTY_HOLD_MEMBERS];
};

/* the host's answer (_distributed_message_party_held): the members asked
for, those it holds room for, and for how many seconds */
struct distributed_party_held
{
	byte format;
	byte asked;
	byte held;
	byte seconds;
};

struct distributed_party_held_message
{
	struct distributed_message_header header;
	struct distributed_party_held answer;
};

/* the host's list of friend codes (_distributed_message_friend_players): an
entry for each player whose machine told one */
struct distributed_friend_player
{
	byte format;
	byte player_index;
	byte reserved[2];
	byte code[8];
};

struct distributed_friend_players_message
{
	struct distributed_message_header header;
	struct distributed_friend_player players[MAXIMUM_TRACKED_PLAYERS];
};

typedef char distributed_friend_player_size_assert[sizeof(struct distributed_friend_player) == 12 ? 1 : -1];
typedef char friend_code_size_assert[FRIEND_CODE_SIZE <= 8 && FRIEND_CODE_SIZE <= PARTY_TOKEN_SIZE ? 1 : -1];
typedef char distributed_party_request_size_assert[sizeof(struct distributed_party_request) == 28 ? 1 : -1];
typedef char distributed_party_hold_size_assert[sizeof(struct distributed_party_hold) == 20 ? 1 : -1];
typedef char distributed_party_held_size_assert[sizeof(struct distributed_party_held) == 4 ? 1 : -1];
typedef char distributed_party_player_size_assert[sizeof(struct distributed_party_player) == 8 ? 1 : -1];

/* a player's, as the host says (or, on the host, as their machine told) */
struct party_player
{
	byte flags;
	unsigned long tag;
};

/* (the host) a slot held for a party's member: the hash of its tunnel key,
the party's (its leader's machine, by its index in the game), and when it
was granted */
struct party_hold
{
	boolean used;
	short group;
	unsigned long time;
	byte key_hash[PARTY_KEY_HASH_SIZE];
};

/* ---------- globals */

static struct
{
	/* in an OpenH1 network game at the last update */
	boolean active;
	/* this machine: can party, is in one (_party_status_*), its tag; when
	read; what was told the host (a client), and whether it was */
	byte own_flags;
	unsigned long own_tag;
	unsigned long status_time;
	boolean told;
	byte told_flags;
	unsigned long told_tag;
	unsigned long told_time;
	/* the host: each machine's word (by its index in the game), its
	invites' count, and whether it has the list */
	struct
	{
		boolean heard;
		byte flags;
		unsigned long tag;
		short burst;
		unsigned long refill_time;
		boolean listed;
		/* its party, as the holds tell (the leader's machine index + 1; 0:
		none), and the holds granted it this game */
		short group;
		short hold_grants;
		/* its player's friend code, as its status told; whether it has the
		friends' list */
		boolean has_code;
		byte code[FRIEND_CODE_SIZE];
		boolean friends_listed;
	} machines[MAXIMUM_PARTY_MACHINES];
	/* this machine's player's friend code (none: no profile), as told */
	boolean has_own_code;
	byte own_code[FRIEND_CODE_SIZE];
	byte told_code[FRIEND_CODE_SIZE];
	/* every machine: each player's friend code (by absolute index) */
	struct
	{
		boolean has;
		byte code[FRIEND_CODE_SIZE];
	} codes[MAXIMUM_TRACKED_PLAYERS];
	/* ... and each of the game's record's players' (by its slot: the
	lobby's, a player card's ADD FRIEND) */
	struct
	{
		boolean has;
		byte code[FRIEND_CODE_SIZE];
	} slot_codes[MAXIMUM_TRACKED_PLAYERS];
	/* the host: the friends' list as last sent; the players last noted for
	the recent players, when */
	struct distributed_friend_player friends_sent[MAXIMUM_TRACKED_PLAYERS];
	short friends_sent_count;
	boolean friends_ever_sent;
	unsigned long recent_time;
	/* the host: room held for parties joining */
	struct party_hold holds[MAXIMUM_PARTY_HOLDS];
	/* a leader (a client, or the host): its members as last read, and as
	last asked for (or held, the host's own), and when */
	unsigned long hold_read_time;
	short hold_asked_count;
	byte hold_asked[MAXIMUM_PARTY_HOLD_MEMBERS * PARTY_KEY_HASH_SIZE];
	unsigned long hold_asked_time;
	/* ... how many times they were (a client), and whether the host
	answered */
	short hold_asks;
	boolean hold_answered;
	unsigned long list_time;
	struct distributed_party_player sent[MAXIMUM_TRACKED_PLAYERS];
	short sent_count;
	boolean ever_sent;
	/* every machine: each player's (by absolute index) */
	struct party_player players[MAXIMUM_TRACKED_PLAYERS];
	/* the invite (or ask) to answer: what it is (NONE: none), from whom
	(a datum index), their name, the party's token, when */
	struct
	{
		short kind;
		long player_index;
		wchar_t name[PARTY_NAME_SIZE + 1];
		byte token[PARTY_TOKEN_SIZE];
		unsigned long time;
	} prompt;
	/* the player this machine asked to join (a datum index; NONE), and when */
	long asked_player_index;
	unsigned long asked_time;
	/* what was done, shown a while */
	wchar_t notice[80];
	unsigned long notice_time;
	boolean notice_shown;
	/* the accept's key and button, held at the last update */
	boolean key_held;
	boolean button_held;
	/* a message of another format heard (noted once) */
	boolean other_format_noted;
} party_globals;

/* ---------- private code */

/* (room held for a party, below) */
static short party_holds_release(struct party_hold *holds, short group);

/* in an OpenH1 game (a Standard one, mixed or not) on the network */
static boolean party_game(
	void)
{
	short connection = game_connection();

	return (connection == _game_connection_network_client || connection == _game_connection_network_server) &&
		network_standard_agreed_rate() != 0;
}

static boolean party_host(
	void)
{
	return game_connection() == _game_connection_network_server;
}

static void party_put_tag(
	byte *bytes,
	unsigned long tag)
{
	bytes[0] = (byte)(tag >> 24);
	bytes[1] = (byte)(tag >> 16);
	bytes[2] = (byte)(tag >> 8);
	bytes[3] = (byte)tag;
}

static unsigned long party_get_tag(
	byte const *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

static boolean party_token_empty(
	byte const *token)
{
	short index;

	for (index = 0; index < PARTY_TOKEN_SIZE; index++)
	{
		if (token[index])
			return FALSE;
	}
	return TRUE;
}

/* a player's name, ended (a player's has none at its 12 characters) */
static void party_player_name(
	struct player_datum const *player,
	wchar_t *name)
{
	short index;

	for (index = 0; index < PARTY_NAME_SIZE && player && player->name[index]; index++)
		name[index] = player->name[index];
	name[index] = 0;
}

/* ... in ASCII, for the log */
static void party_name_ascii(
	wchar_t const *name,
	char *text,
	short size)
{
	short index;

	for (index = 0; index < size - 1 && name[index]; index++)
		text[index] = name[index] >= 0x20 && name[index] < 0x7F ? (char)name[index] : '?';
	text[index] = 0;
}

static void party_notice(
	wchar_t const *format,
	wchar_t const *name)
{
	char text[80];
	wchar_t logged[80];

	usnprintf(party_globals.notice, NUMBEROF(party_globals.notice), format, name);
	party_globals.notice[NUMBEROF(party_globals.notice) - 1] = 0;
	party_globals.notice_time = system_milliseconds();
	party_globals.notice_shown = TRUE;
	/* (the log's without the player's name, which a report would carry:
	port/linux/src/report.c) */
	usnprintf(logged, NUMBEROF(logged), format, L"a player");
	logged[NUMBEROF(logged) - 1] = 0;
	party_name_ascii(logged, text, sizeof(text));
	error(_error_log, "party: %s", text);
}

/* everything of a game let go */
static void party_reset(
	void)
{
	csmemset(&party_globals, 0, sizeof(party_globals));
	party_globals.prompt.kind = NONE;
	party_globals.prompt.player_index = NONE;
	party_globals.asked_player_index = NONE;
}

/* what a player (a datum index) of another machine may be offered, from
what this machine knows of them: none outside an OpenH1 game, for one of
its own players, a bot, an OpenCE machine's player, or a player whose
machine has not said it can party; nor while this machine cannot (or they
are in its party already) */
static unsigned long party_actions(
	boolean game,
	boolean online,
	struct player_datum const *player,
	boolean opence,
	struct party_player const *known,
	unsigned long own_tag,
	boolean prompted)
{
	unsigned long actions = 0;

	if (!game || !online || !player || player->local_player_index != NONE || opence ||
		bots_machine_is_bot(player->network_player_data.machine_index) ||
		!TEST_FLAG(known->flags, _party_status_online_bit))
	{
		return 0;
	}
	if (own_tag && TEST_FLAG(known->flags, _party_status_in_party_bit) && known->tag == own_tag)
		return 0;
	SET_FLAG(actions, _network_party_action_invite, TRUE);
	if (TEST_FLAG(known->flags, _party_status_in_party_bit))
		SET_FLAG(actions, _network_party_action_join, TRUE);
	if (prompted)
		SET_FLAG(actions, _network_party_action_accept, TRUE);
	return actions;
}

/* the client machine (the host's) of a player (absolute index), NONE for
none (or the host's own) */
static long party_player_machine(
	short player_index)
{
	long machine_indices[MAXIMUM_PARTY_MACHINES];
	short count = distributed_client_machines(machine_indices, MAXIMUM_PARTY_MACHINES);
	short index;

	for (index = 0; index < count; index++)
	{
		if (machine_indices[index] >= 0 && machine_indices[index] < MAXIMUM_PARTY_MACHINES &&
			distributed_machine_has_player(machine_indices[index], player_index))
		{
			return machine_indices[index];
		}
	}
	return NONE;
}

/* (the host) whether a machine is sent the party's messages: an OpenH1
machine of this game that told it it can party */
static boolean party_machine_told(
	long machine_index)
{
	return machine_index >= 0 && machine_index < MAXIMUM_PARTY_MACHINES &&
		party_globals.machines[machine_index].heard && network_standard_machine_is_openh1(machine_index);
}

static void party_join_token(
	byte const *token,
	wchar_t const *name)
{
	char link[PARTY_LINK_SIZE];
	short index;

	csstrcpy(link, PARTY_LINK_PREFIX);
	for (index = 0; index < PARTY_TOKEN_SIZE; index++)
		snprintf(link + sizeof(PARTY_LINK_PREFIX) - 1 + 2 * index, 3, "%02x", token[index]);
	if (p2p_party_join_link(link))
		party_notice(L"Joining %s's party", name);
}

/* an invite or ask for this machine's player, from the host (or the host's
own) */
static void party_heard(
	struct distributed_party_request const *request)
{
	long from = distributed_player_from_byte(request->player_index);
	long to = distributed_player_from_byte(request->target_index);
	struct player_datum *sender = from != NONE ? player_try_and_get(from) : NULL;
	unsigned long now = system_milliseconds();
	wchar_t name[PARTY_NAME_SIZE + 1];

	if (request->format != PARTY_FORMAT ||
		(request->kind != _party_request_invite && request->kind != _party_request_ask) ||
		!sender || sender->local_player_index != NONE || to == NONE || !distributed_player_is_local(to) ||
		!TEST_FLAG(party_globals.own_flags, _party_status_online_bit) ||
		network_voice_machine_muted(sender->network_player_data.machine_index))
	{
		return;
	}
	if (request->kind == _party_request_invite && party_token_empty(request->token))
		return;
	party_player_name(sender, name);
	/* (the answer to this machine's ask: joined, as it asked) */
	if (request->kind == _party_request_invite && TEST_FLAG(request->flags, _party_invite_answer_bit) &&
		party_globals.asked_player_index == from && now - party_globals.asked_time < PARTY_ASK_MILLISECONDS)
	{
		party_globals.asked_player_index = NONE;
		party_join_token(request->token, name);
		return;
	}
	party_globals.prompt.kind = request->kind;
	party_globals.prompt.player_index = from;
	csmemcpy(party_globals.prompt.name, name, sizeof(name));
	csmemcpy(party_globals.prompt.token, request->token, PARTY_TOKEN_SIZE);
	party_globals.prompt.time = now;
	party_globals.notice_shown = FALSE;
	/* (not the player's name: a report would carry it, port/linux/src/report.c) */
	error(_error_log, "party: a player %s", request->kind == _party_request_invite ? "invites this machine" :
		"asks to join this machine's party");
}

/* (the host) an invite or ask sent on: to the machine of the player it is
for, or taken here (the host's own player's) */
static void party_host_route(
	struct distributed_party_request const *request)
{
	struct distributed_party_request_message message;
	short target = request->target_index;
	struct player_datum *player = target != NO_PLAYER ? distributed_player(target) : NULL;
	long machine_index;

	if (!player || target == request->player_index)
		return;
	if (player->local_player_index != NONE)
	{
		party_heard(request);
		return;
	}
	machine_index = party_player_machine(target);
	if (!party_machine_told(machine_index))
		return;
	csmemset(&message, 0, sizeof(message));
	message.request = *request;
	distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_party_down, 1,
		sizeof(message));
}

/* a request of this machine's: to the host, or (the host) sent on */
static void party_send(
	struct distributed_party_request const *request)
{
	struct distributed_party_request_message message;

	if (party_host())
	{
		party_host_route(request);
		return;
	}
	csmemset(&message, 0, sizeof(message));
	message.request = *request;
	distributed_send(&message, _distributed_message_party_up, 1, sizeof(message), _distributed_to_host_reliably);
}

/* the first local player (absolute index), NO_PLAYER for none */
static byte party_own_player(
	void)
{
	short local_player_index = local_player_get_next(NONE);

	return local_player_index != NONE ?
		distributed_player_to_byte(local_player_get_player_index(local_player_index)) : NO_PLAYER;
}

/* this machine's invite to a player (a datum index): its party's (one made
for it if none); answer: to their ask */
static boolean party_invite(
	long player_index,
	boolean answer)
{
	struct distributed_party_request request;
	char link[PARTY_LINK_SIZE + 16];

	csmemset(&request, 0, sizeof(request));
	if (!p2p_party_invite_link(link, sizeof(link)) || !p2p_party_parse_link(link, request.token))
		return FALSE;
	request.format = PARTY_FORMAT;
	request.kind = _party_request_invite;
	request.player_index = party_own_player();
	request.target_index = distributed_player_to_byte(player_index);
	request.flags = (byte)(answer ? FLAG(_party_invite_answer_bit) : 0);
	if (request.player_index == NO_PLAYER || request.target_index == NO_PLAYER)
		return FALSE;
	party_send(&request);
	/* (in a party now: the host told at once) */
	party_globals.status_time = 0;
	return TRUE;
}

/* this machine's party, read; told the host (a client) when it changed */
static void party_status_update(
	unsigned long now)
{
	if (party_globals.status_time && now - party_globals.status_time < PARTY_STATUS_MILLISECONDS)
		return;
	party_globals.status_time = now | 1;
	party_globals.own_flags = 0;
	party_globals.own_tag = 0;
	party_globals.has_own_code = FALSE;
	if (config_boolean("network.online"))
	{
		SET_FLAG(party_globals.own_flags, _party_status_online_bit, TRUE);
		party_globals.own_tag = p2p_party_tag();
		SET_FLAG(party_globals.own_flags, _party_status_in_party_bit, party_globals.own_tag != 0);
		/* (its player's friend code: p2p_friends.c, a profile's) */
		party_globals.has_own_code = p2p_friends_own_code(party_globals.own_code) != 0;
		SET_FLAG(party_globals.own_flags, _party_status_friend_code_bit, party_globals.has_own_code);
	}
	if (!party_host() &&
		(!party_globals.told || party_globals.told_flags != party_globals.own_flags ||
			party_globals.told_tag != party_globals.own_tag ||
			(party_globals.has_own_code && csmemcmp(party_globals.told_code, party_globals.own_code,
				FRIEND_CODE_SIZE)) ||
			now - party_globals.told_time >= PARTY_RETELL_MILLISECONDS))
	{
		struct distributed_party_request request;

		csmemset(&request, 0, sizeof(request));
		request.format = PARTY_FORMAT;
		request.kind = _party_request_status;
		request.player_index = party_own_player();
		request.target_index = NO_PLAYER;
		request.flags = party_globals.own_flags;
		party_put_tag(request.tag, party_globals.own_tag);
		/* (a status's token, unused before friends: the friend code) */
		if (party_globals.has_own_code)
			csmemcpy(request.token, party_globals.own_code, FRIEND_CODE_SIZE);
		party_send(&request);
		party_globals.told = TRUE;
		party_globals.told_flags = party_globals.own_flags;
		party_globals.told_tag = party_globals.own_tag;
		csmemcpy(party_globals.told_code, party_globals.own_code, FRIEND_CODE_SIZE);
		party_globals.told_time = now;
	}
}

/* (the host) each player's friend code (its own player's, and those its
machines told), made again with party_host_list; sent on to the machines
that told it they can party, when it changed (and to a machine new to it).
A machine of an older build never tells one, and drops the message's kind
as one it does not know */
static void party_host_friends_list(
	long const *machine_indices,
	short machine_count)
{
	struct distributed_friend_players_message message;
	short count = 0;
	short player_index;
	short index;
	boolean changed;

	csmemset(&message, 0, sizeof(message));
	csmemset(party_globals.codes, 0, sizeof(party_globals.codes));
	csmemset(party_globals.slot_codes, 0, sizeof(party_globals.slot_codes));
	/* (the game's record's players, by their slots: the lobby's, which have
	no datum) */
	{
		struct network_game_server *server = global_network_game_server_get();
		struct network_game *game = server ? network_game_server_get_game(server) : NULL;
		long own = network_game_client_get_local_machine_index();
		short slot;

		for (slot = 0; game && slot < HALO_PORT_MAXIMUM_NETWORK_PLAYERS && count < MAXIMUM_TRACKED_PLAYERS; slot++)
		{
			struct network_player const *player = &game->players[slot];
			long machine_index = player->machine_index;
			byte const *code = NULL;

			if (!network_player_is_valid(player) || bots_machine_is_bot(machine_index))
				continue;
			if (machine_index == own)
				code = party_globals.has_own_code ? party_globals.own_code : NULL;
			else if (machine_index >= 0 && machine_index < MAXIMUM_PARTY_MACHINES && party_machine_told(machine_index) &&
				party_globals.machines[machine_index].has_code)
			{
				code = party_globals.machines[machine_index].code;
			}
			if (!code)
				continue;
			party_globals.slot_codes[slot].has = TRUE;
			csmemcpy(party_globals.slot_codes[slot].code, code, FRIEND_CODE_SIZE);
			message.players[count].format = FRIEND_FORMAT_LOBBY;
			message.players[count].player_index = (byte)slot;
			csmemcpy(message.players[count].code, code, FRIEND_CODE_SIZE);
			count++;
		}
	}
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS && count < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);
		byte const *code = NULL;
		long machine_index;

		if (!player || bots_machine_is_bot(player->network_player_data.machine_index))
			continue;
		if (player->local_player_index != NONE)
			code = party_globals.has_own_code ? party_globals.own_code : NULL;
		else if (party_machine_told(machine_index = party_player_machine(player_index)) &&
			party_globals.machines[machine_index].has_code)
		{
			code = party_globals.machines[machine_index].code;
		}
		if (!code)
			continue;
		party_globals.codes[player_index].has = TRUE;
		csmemcpy(party_globals.codes[player_index].code, code, FRIEND_CODE_SIZE);
		message.players[count].format = FRIEND_FORMAT;
		message.players[count].player_index = (byte)player_index;
		csmemcpy(message.players[count].code, code, FRIEND_CODE_SIZE);
		count++;
	}
	changed = !party_globals.friends_ever_sent || count != party_globals.friends_sent_count ||
		csmemcmp(message.players, party_globals.friends_sent, count * sizeof(message.players[0]));
	csmemcpy(party_globals.friends_sent, message.players, count * sizeof(message.players[0]));
	party_globals.friends_sent_count = count;
	party_globals.friends_ever_sent = TRUE;
	for (index = 0; index < machine_count; index++)
	{
		long machine_index = machine_indices[index];

		if (!party_machine_told(machine_index) || (!changed && party_globals.machines[machine_index].friends_listed))
			continue;
		party_globals.machines[machine_index].friends_listed = TRUE;
		distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_friend_players, count,
			(word)(sizeof(message.header) + count * sizeof(message.players[0])));
	}
}

/* (a client) the host's list of friend codes */
static void party_friends_heard(
	struct distributed_friend_player const *players,
	short count)
{
	short index;

	csmemset(party_globals.codes, 0, sizeof(party_globals.codes));
	csmemset(party_globals.slot_codes, 0, sizeof(party_globals.slot_codes));
	for (index = 0; index < count; index++)
	{
		if (players[index].format == FRIEND_FORMAT_LOBBY && players[index].player_index < MAXIMUM_TRACKED_PLAYERS)
		{
			party_globals.slot_codes[players[index].player_index].has = TRUE;
			csmemcpy(party_globals.slot_codes[players[index].player_index].code, players[index].code,
				FRIEND_CODE_SIZE);
			continue;
		}
		if (players[index].format != FRIEND_FORMAT || players[index].player_index >= MAXIMUM_TRACKED_PLAYERS)
			continue;
		party_globals.codes[players[index].player_index].has = TRUE;
		csmemcpy(party_globals.codes[players[index].player_index].code, players[index].code, FRIEND_CODE_SIZE);
	}
}

/* (the host) who can party, made again; sent on to the machines that told
it they can, when it changed (and to a machine new to it) */
static void party_host_list(
	unsigned long now)
{
	struct distributed_party_players_message message;
	long machine_indices[MAXIMUM_PARTY_MACHINES];
	boolean present[MAXIMUM_PARTY_MACHINES];
	short machine_count;
	short count = 0;
	short player_index;
	short index;
	boolean changed;

	if (party_globals.list_time && now - party_globals.list_time < PARTY_LIST_MILLISECONDS)
		return;
	party_globals.list_time = now | 1;
	/* (a machine gone has its word let go: a machine new to its slot tells
	its own) */
	machine_count = distributed_client_machines(machine_indices, MAXIMUM_PARTY_MACHINES);
	csmemset(present, 0, sizeof(present));
	for (index = 0; index < machine_count; index++)
	{
		if (machine_indices[index] >= 0 && machine_indices[index] < MAXIMUM_PARTY_MACHINES)
			present[machine_indices[index]] = TRUE;
	}
	/* (and the host's own, whose party's holds are its own) */
	{
		short own = network_game_client_get_local_machine_index();

		if (own >= 0 && own < MAXIMUM_PARTY_MACHINES)
			present[own] = TRUE;
	}
	for (index = 0; index < MAXIMUM_PARTY_MACHINES; index++)
	{
		if (present[index])
			continue;
		csmemset(&party_globals.machines[index], 0, sizeof(party_globals.machines[index]));
		/* (a leader gone: its party follows it elsewhere) */
		if (party_holds_release(party_globals.holds, index))
			error(_error_log, "party: machine #%d left; the room held for its party is let go", index);
	}
	csmemset(&message, 0, sizeof(message));
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);
		struct party_player *known = &party_globals.players[player_index];
		long machine_index;

		known->flags = 0;
		known->tag = 0;
		if (!player || bots_machine_is_bot(player->network_player_data.machine_index))
			continue;
		if (player->local_player_index != NONE)
		{
			known->flags = party_globals.own_flags;
			known->tag = party_globals.own_tag;
		}
		else if (party_machine_told(machine_index = party_player_machine(player_index)))
		{
			known->flags = party_globals.machines[machine_index].flags;
			known->tag = party_globals.machines[machine_index].tag;
		}
		if (!TEST_FLAG(known->flags, _party_status_online_bit))
			continue;
		message.players[count].format = PARTY_FORMAT;
		message.players[count].player_index = (byte)player_index;
		message.players[count].flags = known->flags;
		party_put_tag(message.players[count].tag, known->tag);
		count++;
	}
	changed = !party_globals.ever_sent || count != party_globals.sent_count ||
		csmemcmp(message.players, party_globals.sent, count * sizeof(message.players[0]));
	csmemcpy(party_globals.sent, message.players, count * sizeof(message.players[0]));
	party_globals.sent_count = count;
	party_globals.ever_sent = TRUE;
	for (index = 0; index < machine_count; index++)
	{
		long machine_index = machine_indices[index];

		if (!party_machine_told(machine_index) || (!changed && party_globals.machines[machine_index].listed))
			continue;
		party_globals.machines[machine_index].listed = TRUE;
		distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_party_players, count,
			(word)(sizeof(message.header) + count * sizeof(message.players[0])));
	}
	party_host_friends_list(machine_indices, machine_count);
}

/* (the host) a client's word: its status, or an invite or ask of one of
its players' */
static void party_host_heard(
	long machine_index,
	struct distributed_party_request const *request,
	unsigned long now)
{
	if (machine_index < 0 || machine_index >= MAXIMUM_PARTY_MACHINES ||
		!network_standard_machine_is_openh1(machine_index))
	{
		return;
	}
	if (request->kind == _party_request_status)
	{
		party_globals.machines[machine_index].heard = TRUE;
		party_globals.machines[machine_index].flags = request->flags;
		party_globals.machines[machine_index].tag = party_get_tag(request->tag);
		party_globals.machines[machine_index].has_code = TEST_FLAG(request->flags, _party_status_friend_code_bit);
		csmemcpy(party_globals.machines[machine_index].code, request->token, FRIEND_CODE_SIZE);
		party_globals.list_time = 0;
		return;
	}
	if (request->player_index >= MAXIMUM_TRACKED_PLAYERS ||
		!distributed_machine_has_player(machine_index, request->player_index))
	{
		return;
	}
	/* (counted: PARTY_BURST at once, one more each PARTY_REFILL_MILLISECONDS) */
	{
		short *burst = &party_globals.machines[machine_index].burst;
		unsigned long *refill_time = &party_globals.machines[machine_index].refill_time;

		if (!*refill_time)
		{
			*burst = PARTY_BURST;
			*refill_time = now;
		}
		while (*burst < PARTY_BURST && now - *refill_time >= PARTY_REFILL_MILLISECONDS)
		{
			(*burst)++;
			*refill_time += PARTY_REFILL_MILLISECONDS;
		}
		if (*burst >= PARTY_BURST)
			*refill_time = now;
		if (*burst <= 0)
			return;
		(*burst)--;
	}
	party_host_route(request);
}

/* (a client) the host's list of who can party */
static void party_list_heard(
	struct distributed_party_player const *players,
	short count)
{
	short index;

	csmemset(party_globals.players, 0, sizeof(party_globals.players));
	for (index = 0; index < count; index++)
	{
		if (players[index].format != PARTY_FORMAT || players[index].player_index >= MAXIMUM_TRACKED_PLAYERS)
			continue;
		party_globals.players[players[index].player_index].flags = players[index].flags;
		party_globals.players[players[index].player_index].tag = party_get_tag(players[index].tag);
	}
}

/* the invite to answer, if it still is */
static boolean party_prompted(
	unsigned long now)
{
	if (party_globals.prompt.kind == NONE)
		return FALSE;
	if (now - party_globals.prompt.time >= PARTY_PROMPT_MILLISECONDS ||
		!player_try_and_get(party_globals.prompt.player_index))
	{
		party_globals.prompt.kind = NONE;
		party_globals.prompt.player_index = NONE;
		return FALSE;
	}
	return TRUE;
}

/* the accept's key (controls.party_accept) or the controller's D-pad Down
pressed, in the game (not while a line is typed, a menu or the console is
up, or the chat's on-screen keyboard is) */
static void party_keys_update(
	unsigned long now)
{
	boolean key = (halo_keyboard_actions(0) & FLAG(HALO_KEYBOARD_PARTY_ACCEPT)) != 0;
	boolean button = (halo_chat_pad_buttons() & FLAG(HALO_CHAT_PAD_DOWN)) != 0;
	boolean pressed = (key && !party_globals.key_held) || (button && !party_globals.button_held);

	party_globals.key_held = key;
	party_globals.button_held = button;
	if (pressed && party_prompted(now) && game_in_progress() && !ui_widgets_active() && !console_is_active() &&
		!network_chat_typing() && !network_chat_keyboard_open())
	{
		network_party_accept();
	}
}

/* ---------- room held for a party: the holds themselves (the host's) */

/* the slots held */
static short party_holds_count(
	struct party_hold const *holds)
{
	short count = 0;
	short index;

	for (index = 0; index < MAXIMUM_PARTY_HOLDS; index++)
	{
		if (holds[index].used)
			count++;
	}
	return count;
}

/* the hold of a key's hash, NONE for none (or no hash) */
static short party_holds_find(
	struct party_hold const *holds,
	byte const *key_hash)
{
	short index;

	for (index = 0; key_hash && index < MAXIMUM_PARTY_HOLDS; index++)
	{
		if (holds[index].used && !csmemcmp(holds[index].key_hash, key_hash, PARTY_KEY_HASH_SIZE))
			return index;
	}
	return NONE;
}

/* a party's holds (group: its leader's machine) let go; returns how many */
static short party_holds_release(
	struct party_hold *holds,
	short group)
{
	short count = 0;
	short index;

	for (index = 0; index < MAXIMUM_PARTY_HOLDS; index++)
	{
		if (holds[index].used && holds[index].group == group)
		{
			holds[index].used = FALSE;
			count++;
		}
	}
	return count;
}

/* room held for a party's members (count of them, a key's hash each) as
its leader asks (group: its leader's machine), the party's earlier holds
let go first: as many as the game has room for, its slots (maximum, for its
people: the bots apart, port/linux/game/bots.c) free of players and of other
parties' holds; a member held for another party already, or named twice, is
held once. Returns how many are held */
static short party_holds_grant(
	struct party_hold *holds,
	short group,
	byte const *key_hashes,
	short count,
	short players,
	short maximum,
	unsigned long now)
{
	short granted = 0;
	short room;
	short member;

	party_holds_release(holds, group);
	room = (short)(maximum - players - party_holds_count(holds));
	for (member = 0; member < count && granted < room; member++)
	{
		byte const *key_hash = key_hashes + member * PARTY_KEY_HASH_SIZE;
		short index;

		if (party_holds_find(holds, key_hash) != NONE)
			continue;
		for (index = 0; index < MAXIMUM_PARTY_HOLDS && holds[index].used; index++)
			;
		if (index == MAXIMUM_PARTY_HOLDS)
			break;
		holds[index].used = TRUE;
		holds[index].group = group;
		holds[index].time = now;
		csmemcpy(holds[index].key_hash, key_hash, PARTY_KEY_HASH_SIZE);
		granted++;
	}
	return granted;
}

/* holds whose member has not come in PARTY_HOLD_MILLISECONDS let go;
returns how many */
static short party_holds_expire(
	struct party_hold *holds,
	unsigned long now)
{
	short count = 0;
	short index;

	for (index = 0; index < MAXIMUM_PARTY_HOLDS; index++)
	{
		if (holds[index].used && now - holds[index].time >= PARTY_HOLD_MILLISECONDS)
		{
			holds[index].used = FALSE;
			count++;
		}
	}
	return count;
}

/* whether a machine (its tunnel key's hash; NULL: not reached through the
tunnel, a machine of this network) may take one of the game's slots
(maximum, players of them taken, its people's: the bots apart): the one
held for it, if one is, else one no party holds */
static boolean party_holds_admit(
	struct party_hold const *holds,
	byte const *key_hash,
	short players,
	short maximum)
{
	if (party_holds_find(holds, key_hash) != NONE)
		return players < maximum;
	return players + party_holds_count(holds) < maximum;
}

/* the team (0 or 1) for a player of a party joining a team game: the one
most of its party's players in the game are on (party_counts: each team's),
if it has room for one more (team_counts: each team's players; a team has
half the game's most players, rounded up); NONE if none of its party is in
the game yet, or that team is full (the smaller team, then, as anyone's) */
static short party_team_choice(
	short const *party_counts,
	short const *team_counts,
	short maximum)
{
	short team;

	if (party_counts[0] <= 0 && party_counts[1] <= 0)
		return NONE;
	team = (short)(party_counts[1] > party_counts[0] ? 1 : 0);
	return team_counts[team] < (maximum + 1) / 2 ? team : NONE;
}

/* ---------- room held for a party: asking, and holding */

/* (the host) the tunnel key's hash of the machine reached at this address
(host order, as the game keeps it: p2p_peer_key_hash): FALSE for none (a
machine of this network) */
static boolean party_address_key_hash(
	unsigned long address,
	byte *key_hash)
{
	byte bytes[4];
	byte identifier[6];
	unsigned long value;

	if (!address)
		return FALSE;
	bytes[0] = (byte)(address >> 24);
	bytes[1] = (byte)(address >> 16);
	bytes[2] = (byte)(address >> 8);
	bytes[3] = (byte)address;
	csmemcpy(&value, bytes, sizeof(value));
	return p2p_peer_key_hash(value, identifier, key_hash) != 0;
}

/* (the host) whether a key's hash is that of a machine in the game already,
or of this one */
static boolean party_key_hash_present(
	byte const *key_hash)
{
	long machine_indices[MAXIMUM_PARTY_MACHINES];
	short count = distributed_client_machines(machine_indices, MAXIMUM_PARTY_MACHINES);
	byte machine_hash[PARTY_KEY_HASH_SIZE];
	byte identifier[6];
	short index;

	for (index = 0; index < count; index++)
	{
		if (party_address_key_hash(network_game_server_machine_address(machine_indices[index]), machine_hash) &&
			!csmemcmp(machine_hash, key_hash, PARTY_KEY_HASH_SIZE))
		{
			return TRUE;
		}
	}
	/* (this machine's identifier is its key's hash, as p2p.c makes one) */
	csmemcpy(identifier, key_hash, sizeof(identifier));
	identifier[0] = (byte)((identifier[0] & 0xFC) | 0x02);
	return !csmemcmp(identifier, p2p_identifier(), sizeof(identifier));
}

/* (the host) the game's people and the most it takes of them, its bots
apart (port/linux/game/bots.c: a bot leaves for a player who comes, or the
game takes as many more as it has bots) */
static boolean party_host_game(
	short *players,
	short *maximum)
{
	struct network_game_server *server = global_network_game_server_get();
	long most = 0;

	if (!server)
		return FALSE;
	*players = (short)network_game_server_port_bots_humans(server, NULL, &most);
	*maximum = (short)most;
	return TRUE;
}

/* (the host) room held for the members a party's leader (its machine: group)
named, those not in the game already: returns how many are held */
static short party_host_hold(
	short group,
	byte const *key_hashes,
	short count,
	unsigned long now)
{
	byte wanted[MAXIMUM_PARTY_HOLD_MEMBERS * PARTY_KEY_HASH_SIZE];
	short wanted_count = 0;
	short players, maximum;
	short index;
	short held;

	if (group < 0 || group >= MAXIMUM_PARTY_MACHINES || !party_host_game(&players, &maximum))
		return 0;
	if (party_globals.machines[group].hold_grants >= PARTY_HOLD_GRANTS)
	{
		error(_error_log, "party: machine #%d asked for room for its party too often; none held", group);
		return 0;
	}
	party_globals.machines[group].hold_grants++;
	party_globals.machines[group].group = (short)(group + 1);
	for (index = 0; index < count && wanted_count < MAXIMUM_PARTY_HOLD_MEMBERS; index++)
	{
		if (!party_key_hash_present(key_hashes + index * PARTY_KEY_HASH_SIZE))
		{
			csmemcpy(wanted + wanted_count * PARTY_KEY_HASH_SIZE, key_hashes + index * PARTY_KEY_HASH_SIZE,
				PARTY_KEY_HASH_SIZE);
			wanted_count++;
		}
	}
	held = party_holds_grant(party_globals.holds, group, wanted, wanted_count, players, maximum, now);
	error(_error_log, "party: holding room for %d of the %d members of machine #%d's party not in the game "
		"(%d of %d players), for %d seconds", held, wanted_count, group, players, maximum,
		PARTY_HOLD_MILLISECONDS / 1000);
	return held;
}

/* (the host) a leader's ask: held, and answered */
static void party_host_hold_heard(
	long machine_index,
	struct distributed_party_hold const *members,
	short count,
	unsigned long now)
{
	struct distributed_party_held_message message;
	byte key_hashes[MAXIMUM_PARTY_HOLD_MEMBERS * PARTY_KEY_HASH_SIZE];
	short taken = 0;
	short index;
	short held;

	/* (an OpenH1 machine's alone) */
	if (!network_standard_machine_is_openh1(machine_index) || machine_index < 0 ||
		machine_index >= MAXIMUM_PARTY_MACHINES)
	{
		return;
	}
	for (index = 0; index < count; index++)
	{
		if (members[index].format != PARTY_HOLD_FORMAT)
			continue;
		csmemcpy(key_hashes + taken * PARTY_KEY_HASH_SIZE, members[index].key_hash, PARTY_KEY_HASH_SIZE);
		taken++;
	}
	held = party_host_hold((short)machine_index, key_hashes, taken, now);
	csmemset(&message, 0, sizeof(message));
	message.answer.format = PARTY_HOLD_FORMAT;
	message.answer.asked = (byte)taken;
	message.answer.held = (byte)held;
	message.answer.seconds = (byte)(PARTY_HOLD_MILLISECONDS / 1000);
	distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_party_held, 1,
		sizeof(message));
}

/* a leader: its party's members (into key_hashes, their count), read each
PARTY_HOLD_READ_MILLISECONDS; TRUE when they are new to this game, or have
changed since they were last asked for (no sooner than
PARTY_HOLD_ASK_MILLISECONDS after), or (a client) the host has not answered
in PARTY_HOLD_RETRY_MILLISECONDS, PARTY_HOLD_ASKS times at most */
static boolean party_hold_members_changed(
	byte *key_hashes,
	short *count,
	unsigned long now)
{
	if (party_globals.hold_read_time && now - party_globals.hold_read_time < PARTY_HOLD_READ_MILLISECONDS)
		return FALSE;
	if (party_globals.hold_asked_time && now - party_globals.hold_asked_time < PARTY_HOLD_ASK_MILLISECONDS)
		return FALSE;
	party_globals.hold_read_time = now | 1;
	*count = (short)p2p_party_member_key_hashes(key_hashes, MAXIMUM_PARTY_HOLD_MEMBERS);
	if (*count == party_globals.hold_asked_count &&
		!csmemcmp(key_hashes, party_globals.hold_asked, *count * PARTY_KEY_HASH_SIZE))
	{
		if (party_host() || !*count || party_globals.hold_answered || party_globals.hold_asks >= PARTY_HOLD_ASKS ||
			now - party_globals.hold_asked_time < PARTY_HOLD_RETRY_MILLISECONDS)
		{
			return FALSE;
		}
	}
	else
	{
		party_globals.hold_asks = 0;
		party_globals.hold_answered = FALSE;
	}
	party_globals.hold_asked_count = *count;
	csmemcpy(party_globals.hold_asked, key_hashes, *count * PARTY_KEY_HASH_SIZE);
	party_globals.hold_asked_time = now | 1;
	party_globals.hold_asks++;
	return *count > 0;
}

/* (a client) the host has taken this machine into its game: its join
accepted, its machine given its index (asked before, a hold is let go) */
static boolean party_client_accepted(
	void)
{
	struct network_game_client *client = global_network_game_client_get();

	return client && network_game_client_get_machine_index(client) != NONE;
}

/* a leader of a party: room for its members asked of the host (a client),
or held (the host, for its own party), when they change */
static void party_hold_update(
	unsigned long now)
{
	byte key_hashes[MAXIMUM_PARTY_HOLD_MEMBERS * PARTY_KEY_HASH_SIZE];
	short count = 0;

	if (!TEST_FLAG(party_globals.own_flags, _party_status_online_bit) || (!party_host() && !party_client_accepted()) ||
		!party_hold_members_changed(key_hashes, &count, now))
	{
		return;
	}
	if (party_host())
	{
		party_host_hold(network_game_client_get_local_machine_index(), key_hashes, count, now);
		return;
	}
	{
		struct distributed_party_hold_message message;
		short index;

		csmemset(&message, 0, sizeof(message));
		for (index = 0; index < count; index++)
		{
			message.members[index].format = PARTY_HOLD_FORMAT;
			csmemcpy(message.members[index].key_hash, key_hashes + index * PARTY_KEY_HASH_SIZE,
				PARTY_KEY_HASH_SIZE);
		}
		distributed_send(&message, _distributed_message_party_hold, count,
			(word)(sizeof(message.header) + count * sizeof(message.members[0])), _distributed_to_host_reliably);
		error(_error_log, "party: asked the host to hold room for this party's %d other members", count);
	}
}

/* (the host) a machine's party: the group of the hold it took up (or of
the party it leads), else 0; of one joining (machine_index NONE, or its
hold not taken up yet), its hold's, by its address */
static short party_machine_group(
	long machine_index,
	unsigned long address)
{
	byte key_hash[PARTY_KEY_HASH_SIZE];
	short index;

	if (machine_index >= 0 && machine_index < MAXIMUM_PARTY_MACHINES && party_globals.machines[machine_index].group)
		return party_globals.machines[machine_index].group;
	if (address && party_address_key_hash(address, key_hash) &&
		(index = party_holds_find(party_globals.holds, key_hash)) != NONE)
	{
		return (short)(party_globals.holds[index].group + 1);
	}
	return 0;
}

/* ---------- friends: codes and the recent players */

/* whether a player of this relation (port/linux/src/p2p.h's _p2p_friend_*)
may be sent a friend request: none yet, or one asked this machine (its
request accepted so) */
static boolean party_friend_addable(
	int relation)
{
	return relation == _p2p_friend_none || relation == _p2p_friend_request;
}

/* the players of the network game under way, noted for the recent players
(p2p_friends_saw_player) every FRIEND_RECENT_MILLISECONDS: each of another
machine, not a bot, with its friend code if the host told one (an OpenH1
game's), an OpenCE machine's said so */
static void party_recent_update(
	unsigned long now)
{
	/* (network.online, read again as the settings change: config_changes) */
	static unsigned long online_read_at = (unsigned long)-1;
	static boolean online;
	short connection = game_connection();
	short player_index;

	if (online_read_at != config_changes())
	{
		online_read_at = config_changes();
		online = config_boolean("network.online") != 0;
	}
	if ((connection != _game_connection_network_client && connection != _game_connection_network_server) ||
		!game_in_progress() || !online)
	{
		party_globals.recent_time = 0;
		return;
	}
	/* (the first a while into the game: the host has told the codes) */
	if (!party_globals.recent_time || now - party_globals.recent_time < FRIEND_RECENT_MILLISECONDS)
	{
		if (!party_globals.recent_time)
			party_globals.recent_time = now | 1;
		return;
	}
	party_globals.recent_time = now | 1;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct player_datum *player = distributed_player(player_index);
		wchar_t name[PARTY_NAME_SIZE + 1];
		char ascii[PARTY_NAME_SIZE + 1];
		boolean coded;

		if (!player || player->local_player_index != NONE ||
			bots_machine_is_bot(player->network_player_data.machine_index))
		{
			continue;
		}
		party_player_name(player, name);
		party_name_ascii(name, ascii, sizeof(ascii));
		coded = party_game() && party_globals.codes[player_index].has;
		p2p_friends_saw_player(coded ? party_globals.codes[player_index].code : NULL, ascii,
			!coded && network_standard_player_is_opence(player_index), NULL);
	}
}

/* ---------- public code */

void network_party_update(
	void)
{
	unsigned long now = system_milliseconds();

	party_recent_update(now);
	if (!party_game())
	{
		if (party_globals.active)
			party_reset();
		return;
	}
	if (!party_globals.active)
	{
		party_reset();
		party_globals.active = TRUE;
	}
	party_status_update(now);
	if (party_host())
	{
		short released = party_holds_expire(party_globals.holds, now);

		if (released)
			error(_error_log, "party: %d slots held for a party's members who did not come are let go", released);
		party_host_list(now);
	}
	party_hold_update(now);
	party_keys_update(now);
}

unsigned long network_party_player_actions(
	long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	short absolute_index = player ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;
	boolean prompted = party_prompted(system_milliseconds()) && party_globals.prompt.player_index == player_index;

	unsigned long actions;

	if (!player || absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED_PLAYERS || !party_globals.active)
		return 0;
	actions = party_actions(party_game(), TEST_FLAG(party_globals.own_flags, _party_status_online_bit), player,
		network_standard_player_is_opence(player_index) ||
			(party_host() && network_standard_machine_is_opence(party_player_machine(absolute_index))),
		&party_globals.players[absolute_index], party_globals.own_tag, prompted);
	/* (a friend request: a player of another machine whose code the host
	told, not a friend, nor asked or blocked; this machine with a profile) */
	if (party_game() && party_globals.has_own_code && player->local_player_index == NONE &&
		party_globals.codes[absolute_index].has &&
		party_friend_addable(p2p_friends_relation(party_globals.codes[absolute_index].code)))
	{
		SET_FLAG(actions, _network_party_action_add_friend, TRUE);
	}
	return actions;
}

int network_party_slot_friend(
	short slot)
{
	if (!party_globals.active || slot < 0 || slot >= MAXIMUM_TRACKED_PLAYERS || !party_globals.slot_codes[slot].has)
		return NONE;
	return p2p_friends_relation(party_globals.slot_codes[slot].code);
}

boolean network_party_slot_add_friend(
	short slot,
	wchar_t const *name)
{
	char ascii[PARTY_NAME_SIZE + 1];
	wchar_t named[PARTY_NAME_SIZE + 1];
	short index;

	if (!party_globals.has_own_code || !party_friend_addable(network_party_slot_friend(slot)))
		return FALSE;
	for (index = 0; index < PARTY_NAME_SIZE && name && name[index]; index++)
		named[index] = name[index];
	named[index] = 0;
	party_name_ascii(named, ascii, sizeof(ascii));
	return p2p_friends_add(party_globals.slot_codes[slot].code, ascii) == _p2p_friends_done;
}

void network_party_action_label(
	long player_index,
	short action,
	wchar_t *label,
	short size)
{
	wchar_t const *text = L"";

	switch (action)
	{
	case _network_party_action_invite:
		text = L"Invite to party";
		break;
	case _network_party_action_join:
		text = L"Join their party";
		break;
	case _network_party_action_accept:
		text = party_globals.prompt.kind == _party_request_ask ? L"Let into your party" : L"Accept party invite";
		break;
	case _network_party_action_add_friend:
		text = L"Add friend";
		break;
	}
	usnprintf(label, size, L"%s", text);
	label[size - 1] = 0;
}

void network_party_player_act(
	long player_index,
	short action)
{
	struct player_datum *player = player_try_and_get(player_index);
	wchar_t name[PARTY_NAME_SIZE + 1];

	if (!player || !TEST_FLAG(network_party_player_actions(player_index), action))
		return;
	party_player_name(player, name);
	switch (action)
	{
	case _network_party_action_invite:
		if (party_invite(player_index, FALSE))
			party_notice(L"Invited %s to your party", name);
		break;
	case _network_party_action_join:
	{
		struct distributed_party_request request;

		csmemset(&request, 0, sizeof(request));
		request.format = PARTY_FORMAT;
		request.kind = _party_request_ask;
		request.player_index = party_own_player();
		request.target_index = distributed_player_to_byte(player_index);
		if (request.player_index == NO_PLAYER || request.target_index == NO_PLAYER)
			break;
		party_send(&request);
		party_globals.asked_player_index = player_index;
		party_globals.asked_time = system_milliseconds();
		party_notice(L"Asked %s to let you into their party", name);
		break;
	}
	case _network_party_action_accept:
		network_party_accept();
		break;
	case _network_party_action_add_friend:
	{
		char ascii[PARTY_NAME_SIZE + 1];
		short absolute_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);

		party_name_ascii(name, ascii, sizeof(ascii));
		if (p2p_friends_add(party_globals.codes[absolute_index].code, ascii) == _p2p_friends_done)
			party_notice(L"Friend request sent to %s", name);
		break;
	}
	}
}

boolean network_party_accept(
	void)
{
	wchar_t name[PARTY_NAME_SIZE + 1];
	short kind;
	long player_index;

	if (!party_prompted(system_milliseconds()))
		return FALSE;
	kind = party_globals.prompt.kind;
	player_index = party_globals.prompt.player_index;
	csmemcpy(name, party_globals.prompt.name, sizeof(name));
	party_globals.prompt.kind = NONE;
	party_globals.prompt.player_index = NONE;
	if (kind == _party_request_invite)
	{
		party_join_token(party_globals.prompt.token, name);
		return TRUE;
	}
	if (party_invite(player_index, TRUE))
	{
		party_notice(L"Letting %s into your party", name);
		return TRUE;
	}
	return FALSE;
}

boolean network_party_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_party_up || header.type == _distributed_message_party_down ||
		header.type == _distributed_message_party_players || header.type == _distributed_message_party_hold ||
		header.type == _distributed_message_party_held || header.type == _distributed_message_friend_players;
}

void network_party_handle_message(
	long machine_index,
	word const *message,
	word size,
	boolean from_stream)
{
	struct distributed_message_header header;
	byte const *entries = (byte const *)message + sizeof(header);

	csmemcpy(&header, message, sizeof(header));
	if (!from_stream || !party_globals.active || !party_game())
		return;
	switch (header.type)
	{
	case _distributed_message_party_up:
	case _distributed_message_party_down:
	{
		struct distributed_party_request request;

		if (header.count != 1 || size != sizeof(header) + sizeof(request))
			return;
		csmemcpy(&request, entries, sizeof(request));
		if (request.format != PARTY_FORMAT || request.kind >= NUMBER_OF_PARTY_REQUESTS)
		{
			if (!party_globals.other_format_noted)
				error(_error_log, "party: a message of another format (%d), not taken", request.format);
			party_globals.other_format_noted = TRUE;
			return;
		}
		/* (up: the host's, from a client; down: a client's, from the host) */
		if (header.type == _distributed_message_party_up && party_host() && machine_index != NONE)
			party_host_heard(machine_index, &request, system_milliseconds());
		else if (header.type == _distributed_message_party_down && !party_host() && machine_index == NONE)
			party_heard(&request);
		break;
	}
	case _distributed_message_party_players:
		if (party_host() || machine_index != NONE || header.count > MAXIMUM_TRACKED_PLAYERS ||
			size != sizeof(header) + header.count * sizeof(struct distributed_party_player))
		{
			return;
		}
		{
			struct distributed_party_player players[MAXIMUM_TRACKED_PLAYERS];

			csmemcpy(players, entries, header.count * sizeof(players[0]));
			party_list_heard(players, header.count);
		}
		break;
	case _distributed_message_friend_players:
		if (party_host() || machine_index != NONE || header.count > MAXIMUM_TRACKED_PLAYERS ||
			size != sizeof(header) + header.count * sizeof(struct distributed_friend_player))
		{
			return;
		}
		{
			struct distributed_friend_player players[MAXIMUM_TRACKED_PLAYERS];

			csmemcpy(players, entries, header.count * sizeof(players[0]));
			party_friends_heard(players, header.count);
		}
		break;
	case _distributed_message_party_hold:
		if (!party_host() || machine_index == NONE || header.count > MAXIMUM_PARTY_HOLD_MEMBERS ||
			size != sizeof(header) + header.count * sizeof(struct distributed_party_hold))
		{
			return;
		}
		{
			struct distributed_party_hold members[MAXIMUM_PARTY_HOLD_MEMBERS];

			csmemcpy(members, entries, header.count * sizeof(members[0]));
			party_host_hold_heard(machine_index, members, header.count, system_milliseconds());
		}
		break;
	case _distributed_message_party_held:
	{
		struct distributed_party_held answer;

		if (party_host() || machine_index != NONE || header.count != 1 || size != sizeof(header) + sizeof(answer))
			return;
		csmemcpy(&answer, entries, sizeof(answer));
		if (answer.format != PARTY_HOLD_FORMAT)
			return;
		party_globals.hold_answered = TRUE;
		error(_error_log, "party: the host holds room for %d of this party's %d other members, for %d seconds",
			answer.held, answer.asked, answer.seconds);
		break;
	}
	}
}

/* ---------- room held for a party: the host's server */

boolean network_party_slot_open(
	unsigned long address)
{
	byte key_hash[PARTY_KEY_HASH_SIZE];
	boolean reached;
	short players, maximum;

	if (!party_globals.active || !party_host() || !party_host_game(&players, &maximum))
		return TRUE;
	reached = party_address_key_hash(address, key_hash);
	return party_holds_admit(party_globals.holds, reached ? key_hash : NULL, players, maximum);
}

void network_party_slot_taken(
	long machine_index,
	unsigned long address)
{
	byte key_hash[PARTY_KEY_HASH_SIZE];
	short index;

	if (!party_globals.active || !party_host() || machine_index < 0 || machine_index >= MAXIMUM_PARTY_MACHINES ||
		!party_address_key_hash(address, key_hash) ||
		(index = party_holds_find(party_globals.holds, key_hash)) == NONE)
	{
		return;
	}
	party_globals.machines[machine_index].group = (short)(party_globals.holds[index].group + 1);
	party_globals.holds[index].used = FALSE;
	error(_error_log, "party: machine #%ld took up the slot held for it (machine #%d's party)", machine_index,
		party_globals.holds[index].group);
}

boolean network_party_machines_together(
	long machine_index,
	unsigned long address,
	long other_machine_index)
{
	short group, other_group;

	if (!party_globals.active || !party_host())
		return FALSE;
	group = party_machine_group(machine_index, address);
	other_group = party_machine_group(other_machine_index, 0);
	if (group && group == other_group)
		return TRUE;
	/* (else as their parties' tags say: told by each, a client's) */
	if (machine_index < 0 || machine_index >= MAXIMUM_PARTY_MACHINES || other_machine_index < 0 ||
		other_machine_index >= MAXIMUM_PARTY_MACHINES)
	{
		return FALSE;
	}
	return party_machine_told(machine_index) && party_machine_told(other_machine_index) &&
		TEST_FLAG(party_globals.machines[machine_index].flags, _party_status_in_party_bit) &&
		party_globals.machines[machine_index].tag &&
		party_globals.machines[machine_index].tag == party_globals.machines[other_machine_index].tag;
}

boolean network_party_machine_in_party(
	long machine_index)
{
	if (!party_globals.active || !party_host() || machine_index < 0 || machine_index >= MAXIMUM_PARTY_MACHINES)
		return FALSE;
	return party_globals.machines[machine_index].group ||
		(party_machine_told(machine_index) &&
			TEST_FLAG(party_globals.machines[machine_index].flags, _party_status_in_party_bit));
}

short network_party_held_slots(
	void)
{
	return party_globals.active && party_host() ? party_holds_count(party_globals.holds) : 0;
}

short network_party_team_choice(
	short const *party_counts,
	short const *team_counts,
	short maximum)
{
	return party_team_choice(party_counts, team_counts, maximum);
}

/* ---------- drawing */

/* how much of a line shows (fading out at its end) */
static real party_line_alpha(
	unsigned long elapsed,
	unsigned long shown)
{
	if (elapsed >= shown)
		return 0.0f;
	if (shown - elapsed < PARTY_FADE_MILLISECONDS)
		return (real)(shown - elapsed) / PARTY_FADE_MILLISECONDS;
	return 1.0f;
}

void network_party_draw(
	short local_player_index)
{
	long font_index = hud_get_font_index();
	unsigned long now = system_milliseconds();
	rectangle2d window = render.camera.window_bounds;
	rectangle2d bounds, cursor, measured;
	real_argb_color color;
	wchar_t text[96];
	wchar_t const *action = NULL;
	struct font_header *font;
	short line_height;
	short width;
	real alpha;
	boolean prompt;

	if (font_index == NONE || local_player_index != local_player_get_next(NONE) || ui_widgets_active() ||
		cinematic_in_progress())
	{
		return;
	}
	/* (a Discord friend's ask, above the line) */
	{
		rectangle2d view = window;

		offset_rectangle2d(&view, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
		network_party_discord_draw(&view, font_index, (short)(view.y0 + (view.y1 - view.y0) / 8));
	}
	if (!party_globals.active)
		return;
	prompt = party_prompted(now);
	if (prompt)
	{
		alpha = party_line_alpha(now - party_globals.prompt.time, PARTY_PROMPT_MILLISECONDS);
		usnprintf(text, NUMBEROF(text), party_globals.prompt.kind == _party_request_ask ?
			L"%s wants to join your party   " : L"%s invites you to their party   ", party_globals.prompt.name);
		action = party_globals.prompt.kind == _party_request_ask ? L" Let in" : L" Join";
	}
	else if (party_globals.notice_shown)
	{
		alpha = party_line_alpha(now - party_globals.notice_time, PARTY_NOTICE_MILLISECONDS);
		usnprintf(text, NUMBEROF(text), L"%s", party_globals.notice);
	}
	else
	{
		return;
	}
	text[NUMBEROF(text) - 1] = 0;
	if (alpha <= 0.0f)
		return;
	offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	color.alpha = alpha;
	color.red = 0.81f;
	color.green = 0.89f;
	color.blue = 1.0f;
	draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
	/* (centered, a fifth of the way down the view: the text measured, and
	room for the key and its word) */
	measured = window;
	cursor = window;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_bounds(&measured, text, &bounds, &cursor);
	width = (short)(bounds.x1 - bounds.x0 + (action ? 5 * line_height : 0));
	bounds.x0 = (short)MAX(window.x0, (window.x0 + window.x1 - width) / 2);
	bounds.x1 = window.x1;
	bounds.y0 = (short)(window.y0 + (window.y1 - window.y0) / 5);
	bounds.y1 = (short)(bounds.y0 + 2 * line_height);
	{
		rectangle2d panel;
		long panel_alpha = (long)(160 * alpha);

		panel.x0 = (short)(bounds.x0 - line_height / 2);
		panel.x1 = (short)MIN(window.x1, bounds.x0 + width + line_height / 2);
		panel.y0 = (short)(bounds.y0 - line_height / 4);
		panel.y1 = (short)(bounds.y0 + line_height + line_height / 4);
		draw_quad(&panel, ((pixel32)panel_alpha << 24) | 0x00061430);
	}
	if (!action)
	{
		rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
		return;
	}
	/* (the key as the player plays: the keyboard's, else the controller's
	D-pad Down, input.button_prompts) */
	{
		char key[32];
		pixel32 key_color = real_argb_color_to_pixel32(&color);

		cursor = bounds;
		draw_string_set_indents(0, 0);
		draw_unicode_string_compute_bounds(&bounds, text, &measured, &cursor);
		cursor.x0 -= 3;
		measured.x0 = bounds.x0;
		rasterizer_draw_unicode_string(&measured, NULL, NULL, 0, text);
		hud_key_prompt_action_name(HALO_KEYBOARD_PARTY_ACCEPT, key, sizeof(key));
		if (hud_key_prompt_draw_key(key, &bounds, &cursor, key_color))
		{
			rectangle2d rest = bounds;

			draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
			draw_string_set_indents((short)(cursor.x0 - bounds.x0), 0);
			draw_unicode_string_compute_bounds(&bounds, action, &rest, &cursor);
			rest.x0 = bounds.x0;
			rasterizer_draw_unicode_string(&rest, NULL, NULL, 0, action);
		}
		else
		{
			wchar_t button[32];
			rectangle2d rest = bounds;

			usnprintf(button, NUMBEROF(button), L"%%dpad-down%s", action);
			button[NUMBEROF(button) - 1] = 0;
			rest.x0 = cursor.x0;
			draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
			draw_string_and_hack_in_icons(&rest, NULL, NULL, 0, button, FALSE);
		}
	}
	draw_string_set_indents(0, 0);
}

/* a part of a line, after the cursor (where the last left it) */
static void party_line_part(
	rectangle2d *bounds,
	rectangle2d *cursor,
	wchar_t const *text)
{
	rectangle2d rest = *bounds;

	draw_string_set_indents((short)(cursor->x0 - bounds->x0), 0);
	draw_unicode_string_compute_bounds(bounds, text, &rest, cursor);
	rest.x0 = bounds->x0;
	rasterizer_draw_unicode_string(&rest, NULL, NULL, 0, text);
}

/* a friend's toast (port/linux/src/p2p_friends.c): its invite to its party,
with its keys as the player plays (as a Discord friend's ask: the accept's
and Esc, else the controller's D-pad Down and Up; on a touchscreen, where
to answer it), or the newest notice (a friend online, a request, a request
accepted) for FRIEND_TOAST_MILLISECONDS, fading; centered in window at y */
static void party_friends_draw(
	rectangle2d const *window,
	long font_index,
	short y)
{
	static unsigned long notice_serial, notice_time;
	struct p2p_friends_notice notice;
	char latin1[P2P_FRIEND_NAME_SIZE];
	wchar_t name[P2P_FRIEND_NAME_SIZE];
	wchar_t text[112];
	rectangle2d bounds, cursor, measured;
	real_argb_color color;
	struct font_header *font;
	short line_height, width, index;
	unsigned long now = system_milliseconds();
	boolean invite = p2p_friends_invite_prompt(latin1, sizeof(latin1)) != 0;
	char key[32];
	boolean keys;
	real alpha = 1.0f;

	if (!invite)
	{
		if (!p2p_friends_notice(&notice))
			return;
		/* (a notice new since the last frame: shown from now) */
		if (notice.serial != notice_serial)
		{
			notice_serial = notice.serial;
			notice_time = now;
		}
		alpha = party_line_alpha(now - notice_time, FRIEND_TOAST_MILLISECONDS);
		if (alpha <= 0.0f)
			return;
		csmemcpy(latin1, notice.name, sizeof(latin1));
	}
	for (index = 0; index < P2P_FRIEND_NAME_SIZE - 1 && latin1[index]; index++)
		name[index] = (wchar_t)(unsigned char)latin1[index];
	name[index] = 0;
	if (invite)
	{
		usnprintf(text, NUMBEROF(text), ui_widget_port_touch_menus() ?
			L"%s invites you to their party: join from Multiplayer, Friends" : L"%s invites you to their party   ",
			name);
	}
	else
	{
		usnprintf(text, NUMBEROF(text), notice.kind == _p2p_friends_notice_online ? L"%s is online" :
			notice.kind == _p2p_friends_notice_request ? L"%s sent you a friend request: see Multiplayer, Friends" :
			L"%s accepted your friend request", name);
	}
	text[NUMBEROF(text) - 1] = 0;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	keys = halo_keyboard_prompts();
	hud_key_prompt_action_name(HALO_KEYBOARD_PARTY_ACCEPT, key, sizeof(key));
	color.alpha = alpha;
	color.red = 0.81f;
	color.green = 0.89f;
	color.blue = 1.0f;
	draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
	measured = *window;
	cursor = *window;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_bounds(&measured, text, &bounds, &cursor);
	width = (short)(bounds.x1 - bounds.x0 + (invite && !ui_widget_port_touch_menus() ? 10 * line_height : 0));
	bounds.x0 = (short)MAX(window->x0, (window->x0 + window->x1 - width) / 2);
	bounds.x1 = window->x1;
	bounds.y0 = y;
	bounds.y1 = (short)(bounds.y0 + 2 * line_height);
	{
		rectangle2d panel;

		panel.x0 = (short)(bounds.x0 - line_height / 2);
		panel.x1 = (short)MIN(window->x1, bounds.x0 + width + line_height / 2);
		panel.y0 = (short)(bounds.y0 - line_height / 4);
		panel.y1 = (short)(bounds.y0 + line_height + line_height / 4);
		draw_quad(&panel, ((pixel32)(200 * alpha) << 24) | 0x00061430);
	}
	cursor = bounds;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_bounds(&bounds, text, &measured, &cursor);
	measured.x0 = bounds.x0;
	rasterizer_draw_unicode_string(&measured, NULL, NULL, 0, text);
	if (!invite || ui_widget_port_touch_menus())
	{
		draw_string_set_indents(0, 0);
		return;
	}
	if (keys)
	{
		pixel32 key_color = real_argb_color_to_pixel32(&color);

		cursor.x0 -= 3;
		if (hud_key_prompt_draw_key(key, &bounds, &cursor, key_color))
		{
			draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
			party_line_part(&bounds, &cursor, L" Join   ");
			if (hud_key_prompt_draw_key("Esc", &bounds, &cursor, key_color))
			{
				draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
				party_line_part(&bounds, &cursor, L" Not now");
			}
			draw_string_set_indents(0, 0);
			return;
		}
	}
	{
		rectangle2d rest = bounds;

		rest.x0 = cursor.x0;
		draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
		draw_string_set_indents(0, 0);
		draw_string_and_hack_in_icons(&rest, NULL, NULL, 0, L"%dpad-down Join   %dpad-up Not now", FALSE);
	}
	draw_string_set_indents(0, 0);
}

void network_party_discord_draw(
	rectangle2d const *window,
	long font_index,
	short y)
{
	char latin1[P2P_DISCORD_REQUEST_NAME_SIZE];
	wchar_t name[P2P_DISCORD_REQUEST_NAME_SIZE];
	wchar_t text[96];
	rectangle2d bounds, cursor, measured;
	real_argb_color color;
	struct font_header *font;
	short line_height, width, index;
	char key[32];
	boolean keys;
	int game = FALSE;

	if (font_index == NONE)
		return;
	/* (no Discord friend's ask: a friend's toast) */
	if (!p2p_discord_join_request(latin1, sizeof(latin1), &game))
	{
		party_friends_draw(window, font_index, y);
		return;
	}
	for (index = 0; index < P2P_DISCORD_REQUEST_NAME_SIZE - 1 && latin1[index]; index++)
		name[index] = (wchar_t)(unsigned char)latin1[index];
	name[index] = 0;
	usnprintf(text, NUMBEROF(text), game ? L"%s asks to join your game   " : L"%s asks to join your party   ", name);
	text[NUMBEROF(text) - 1] = 0;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0)
		return;
	keys = halo_keyboard_prompts();
	hud_key_prompt_action_name(HALO_KEYBOARD_PARTY_ACCEPT, key, sizeof(key));
	color.alpha = 1.0f;
	color.red = 0.81f;
	color.green = 0.89f;
	color.blue = 1.0f;
	draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
	/* (centered at y: the text measured, and room for the keys and their
	words) */
	measured = *window;
	cursor = *window;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_bounds(&measured, text, &bounds, &cursor);
	width = (short)(bounds.x1 - bounds.x0 + 10 * line_height);
	bounds.x0 = (short)MAX(window->x0, (window->x0 + window->x1 - width) / 2);
	bounds.x1 = window->x1;
	bounds.y0 = y;
	bounds.y1 = (short)(bounds.y0 + 2 * line_height);
	{
		rectangle2d panel;

		panel.x0 = (short)(bounds.x0 - line_height / 2);
		panel.x1 = (short)MIN(window->x1, bounds.x0 + width + line_height / 2);
		panel.y0 = (short)(bounds.y0 - line_height / 4);
		panel.y1 = (short)(bounds.y0 + line_height + line_height / 4);
		draw_quad(&panel, ((pixel32)200 << 24) | 0x00061430);
	}
	cursor = bounds;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_bounds(&bounds, text, &measured, &cursor);
	measured.x0 = bounds.x0;
	rasterizer_draw_unicode_string(&measured, NULL, NULL, 0, text);
	if (keys)
	{
		pixel32 key_color = real_argb_color_to_pixel32(&color);

		cursor.x0 -= 3;
		if (hud_key_prompt_draw_key(key, &bounds, &cursor, key_color))
		{
			draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
			party_line_part(&bounds, &cursor, L" Accept   ");
			if (hud_key_prompt_draw_key("Esc", &bounds, &cursor, key_color))
			{
				draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
				party_line_part(&bounds, &cursor, L" Decline");
			}
			draw_string_set_indents(0, 0);
			return;
		}
	}
	{
		rectangle2d rest = bounds;

		rest.x0 = cursor.x0;
		draw_string_set_draw_mode(font_index, NONE, 0 /* left */, 0, &color);
		draw_string_set_indents(0, 0);
		draw_string_and_hack_in_icons(&rest, NULL, NULL, 0, L"%dpad-down Accept   %dpad-up Decline", FALSE);
	}
	draw_string_set_indents(0, 0);
}
