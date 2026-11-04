/*
NETWORK_ADMIN.H

A dedicated server's admins in the game (network_admin.c;
docs/dedicated_server.md, "Admins in the game"): the players whose player
keys the server's owner made admins, who get Host Controls from the server,
whose commands do what they ask.
*/

#ifndef __NETWORK_ADMIN_H
#define __NETWORK_ADMIN_H
#pragma once

/* ---------- constants */

/* what an admin asks of the server (a request's verb) */
enum
{
	/* the lists sent again: the maps, the gametypes, the rotation, the bans */
	_network_admin_lists,
	_network_admin_kick,
	_network_admin_ban,
	_network_admin_mute,
	_network_admin_unmute,
	_network_admin_move_team,
	_network_admin_end_game,
	_network_admin_restart,
	_network_admin_shuffle_teams,
	_network_admin_balance_teams,
	_network_admin_lock_teams,
	_network_admin_announce,
	_network_admin_map_now,
	_network_admin_map_after,
	_network_admin_rotation_add,
	_network_admin_rotation_remove,
	_network_admin_unban,
	NUMBER_OF_NETWORK_ADMIN_REQUESTS
};

/* the game as the server tells its admins (network_admin_state's flags) */
enum
{
	/* the machine told is an admin */
	_network_admin_flag_admin,
	/* a game is played (not the lobby, not its scores) */
	_network_admin_flag_playing,
	_network_admin_flag_lobby,
	_network_admin_flag_teams,
	_network_admin_flag_teams_locked,
	/* players may move between the teams of the game played */
	_network_admin_flag_team_moves,
	/* an OpenH1 game's */
	_network_admin_flag_standard,
	NUMBER_OF_NETWORK_ADMIN_FLAGS
};

/* the lists (network_admin_lines) */
enum
{
	_network_admin_list_maps,
	_network_admin_list_gametypes,
	_network_admin_list_rotation,
	_network_admin_list_bans,
	NUMBER_OF_NETWORK_ADMIN_LISTS
};

enum
{
	NETWORK_ADMIN_NAME_SIZE = 16,
	NETWORK_ADMIN_TEXT_SIZE = 128,
	NETWORK_ADMIN_ENTRY_SIZE = 128,
	NETWORK_ADMIN_RESULT_SIZE = 160,
	/* a key's id as text, with its end */
	NETWORK_ADMIN_KEY_ID_TEXT_SIZE = 17,
	/* a machine's requests: so many a second, so many at once at most */
	NETWORK_ADMIN_REQUESTS_PER_SECOND = 4,
	NETWORK_ADMIN_REQUEST_BURST = 8,
	/* a ban's longest (seconds: ten years, the commands' durations') and a
	mute's (minutes) */
	NETWORK_ADMIN_BAN_MOST_SECONDS = 10L * 365 * 24 * 60 * 60,
	NETWORK_ADMIN_MUTE_MOST_MINUTES = 24 * 60,
	/* the rotation's and bans.txt's numbers, at most */
	NETWORK_ADMIN_ROTATION_MOST = 64,
	/* the game's players' slots (HALO_PORT_MAXIMUM_NETWORK_PLAYERS) */
	NETWORK_ADMIN_MAXIMUM_SLOTS = 128,
	NETWORK_ADMIN_BAN_MOST = 2048,
};

/* ---------- structures */

/* a request, as it comes (every field untrusted until checked: the
server's network_admin_request_line) */
struct network_admin_request
{
	short verb;
	/* the player's slot (NONE: none), and their name as the admin's list
	had it, which must still be the slot's */
	short slot;
	long number;
	char name[NETWORK_ADMIN_NAME_SIZE];
	char text[NETWORK_ADMIN_TEXT_SIZE];
};

/* a machine's requests counted (network_admin_rate_take) */
struct network_admin_rate
{
	unsigned long time;
	long tokens;
};

/* the game as the admin's machine last heard of it */
struct network_admin_state
{
	unsigned long flags;
	/* an OpenH1 game's tick rate */
	short rate;
	/* the rotation's entry played (NONE: none, or sv_map's) */
	short rotation_index;
	char current[NETWORK_ADMIN_ENTRY_SIZE];
	char next[NETWORK_ADMIN_ENTRY_SIZE];
};

struct host_controls_player;
struct host_controls_ban;
struct host_game_map;
struct host_game_gametype;
struct server_rotation;

/* ---------- prototypes/NETWORK_ADMIN.C */

/* the pure parts (tools/harness/tests/test_network_admin.py): whether a key
(its 64 hex digits, and its id's 16) is in a list of keys or key ids
(64 or 16 hex digits each, in either case, separated by commas, spaces or
lines; "#" begins a comment to the line's end); a request's server command,
checked (the slot's name now: NULL for none; the game's entry played, for a
restart): TRUE with the line, else FALSE and why; a request counted: TRUE if
the machine may make it now */
boolean network_admin_listed(char const *key_hex, char const *key_id, char const *list);
boolean network_admin_request_line(struct network_admin_request const *request, char const *slot_name,
	char const *current_entry, char *line, long size, char *why, long why_size);
boolean network_admin_rate_take(struct network_admin_rate *rate, unsigned long now);

/* (the server: network_link.c) a new machine in the slot: nothing of the one
before kept */
void network_admin_machine_joined(long machine_index);
/* (the server, each frame: server_director.c) the nonces sent, the admins
told the game */
void network_admin_server_update(void);
/* (the server: server_commands.c) a machine's proven key's id and whether it
is an admin now: FALSE if it proved none */
boolean network_admin_machine_key(long machine_index, char *key_hex, char *key_id, boolean *admin);
/* (the server: sv_admin) the admins listed: admins.txt's keys, then
server.admins'; each line "<key or id> <name>", into text */
void network_admin_list_text(char *text, long size);
/* ... a key (64 hex digits) added to admins.txt with a name, or taken out
(by its key or id): FALSE and why if it cannot be */
boolean network_admin_add(char const *key_hex, char const *name, char *why, long why_size);
boolean network_admin_remove(char const *key_or_id, char *why, long why_size);

/* (an admin's machine) whether the server it plays on made it an admin, as
it last said (a moment ago); the game as it said it */
boolean network_admin_remote(void);
/* ... whether it did, however long ago, on this connection (the pause menu
made as a map loads, which holds the server's word back) */
boolean network_admin_was_remote(void);
boolean network_admin_remote_state(struct network_admin_state *state);
/* ... the lists as last heard: the players (their datum index, NONE in the
lobby; their slot the server's), the maps, the gametypes, the rotation, the
bans (each line its number in bans.txt) */
short network_admin_players(struct host_controls_player *players, short maximum);
/* ... the game's player (datum index) as the server listed them: FALSE if
it did not */
boolean network_admin_player(long player_index, struct host_controls_player *player);
/* ... whether the player of the slot is muted for everyone */
boolean network_admin_player_muted(short slot);
short network_admin_maps(struct host_game_map const **maps);
short network_admin_gametypes(struct host_game_gametype const **gametypes);
struct server_rotation const *network_admin_rotation(void);
short network_admin_bans(struct host_controls_ban *bans, char (*lines)[512], short maximum);
/* ... a request sent: FALSE if it could not be */
boolean network_admin_send(short verb, long slot, wchar_t const *name, long number, char const *text);
/* ... the server's answer to the last request not yet taken (its words),
FALSE if none came */
boolean network_admin_take_result(boolean *ok, char *text, long size);

/* (an admin's machine) the console's admin: alone, whether this machine is
an admin of the server and its key's id; "admin map <entry>", "map_after",
"rotation_add", "end", "restart", "say <text>", asked of the server
(network_host_game.c's console calls it) */
boolean network_admin_console(char const *expression, boolean *result);

/* the messages (network_distributed.c) */
boolean network_admin_handles_message(word const *message, word size);
void network_admin_handle_message(long machine_index, word const *message, word size, boolean from_stream);

#endif // __NETWORK_ADMIN_H
