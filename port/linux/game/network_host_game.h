/*
NETWORK_HOST_GAME.H

The next game of a network game a player hosts (network_host_game.c): a
map and gametype changed now or after this game, and the host's playlist
(network.host_playlist, in the dedicated server's rotation grammar), which
Host Controls (host_controls_menu.inc), the lobby (lobby_menu.inc) and the
console's host_map and host_playlist use.
*/

#ifndef __NETWORK_HOST_GAME_H
#define __NETWORK_HOST_GAME_H
#pragma once

#include "../src/server_rotation.h" /* port: port/linux/src/server_rotation.c */

/* ---------- constants */

/* when a change plays */
enum
{
	/* now: in the lobby at once; in a game, the game ended (no scores) and
	the new one started from the lobby */
	_host_game_now,
	/* after this game: its scores, then the lobby with it, which starts */
	_host_game_after,
};

enum
{
	/* a map's name, and the maps listed at most */
	HOST_GAME_MAP_SIZE = SERVER_MAP_SIZE,
	MAXIMUM_HOST_GAME_MAPS = 512,
	/* the gametypes listed at most: the built-in and OpenCE's, then those
	this machine's player saved (server_gametype_saved_list) */
	MAXIMUM_HOST_GAME_GAMETYPES = 160,
	/* a gametype's name as shown */
	HOST_GAME_GAMETYPE_SHOWN_SIZE = 24,
};

/* the ways of arranging the teams (network_host_game_teams_plan) */
enum
{
	/* at random, as many on each */
	_host_game_shuffle,
	/* as many on each, as few moved as can be; by their scores where they
	have some */
	_host_game_balance,
};

/* what a lobby takes after a game (network_host_game_lobby_takes: a bit
each) */
enum
{
	/* the change asked for */
	_host_game_take_change,
	/* the playlist's next (or, the change failing, in its place) */
	_host_game_take_playlist,
};

/* ---------- structures */

struct host_controls_player;
struct network_game;
struct game_variant;
struct game_variant_options;

/* a multiplayer map there is (the maps folder's, custom_maps') */
struct host_game_map
{
	/* its name (bloodgulch, hugeass) and level (levels\test\bloodgulch\bloodgulch,
	custom_maps\hugeass) */
	char name[HOST_GAME_MAP_SIZE];
	char level[HOST_GAME_MAP_SIZE];
	boolean custom;
};

/* a gametype by its name (the rotation's) and as shown */
struct host_game_gametype
{
	char name[SERVER_GAMETYPE_SIZE];
	wchar_t shown[HOST_GAME_GAMETYPE_SHOWN_SIZE];
};

/* ---------- prototypes/NETWORK_HOST_GAME.C */

/* the pure parts (tools/harness/tests/test_host_game.py): the playlist's
entry after the one at index (NONE: before the first) that can be played,
NONE if none can */
long network_host_game_playlist_after(struct server_rotation const *playlist, long index);
/* ... the first of those after it that playable takes (each tried once),
NONE if none does; whether an entry's text fits the playlist (its games
and its text, joined, as network.host_playlist holds them) */
/* what the lobby after a game takes (_host_game_take_*): none in co-op;
a change asked for (on a restart, one asked for now alone); the playlist's
next, but on a restart */
unsigned long network_host_game_lobby_takes(boolean cooperative, boolean has_next, boolean next_now,
	boolean restarting);
long network_host_game_playlist_try(struct server_rotation const *playlist, long index,
	boolean (*playable)(struct server_entry const *entry, void *context), void *context);
boolean network_host_game_playlist_fits(struct server_rotation const *playlist, char const *text);
/* the console's ban line: the player's name (quoted, or up to a length with
its unit), the length in seconds (0 for ever, none given) and the reason
(its quotes off); FALSE without a name */
boolean network_host_game_ban_parse(char const *text, char *name, long name_size, long *seconds, char *reason,
	long reason_size);
/* whether a change's entry text gives a map alone (no gametype: the game's
own is kept, its score to win, time limit and rules too); the lobby's
variant and options a change keeps (its gametype emptied by
network_host_game_change: TRUE), else FALSE (its gametype's are built); the
gametype of the list shown as the game's is (its variant's name), NONE if
none is */
boolean network_host_game_entry_keeps(char const *text);
boolean network_host_game_variant_kept(struct server_entry const *entry, struct network_game const *game,
	struct game_variant *variant, struct game_variant_options *options);
short network_host_game_gametype_find(struct host_game_gametype const *gametypes, short count, wchar_t const *shown);
/* a duration's text ("30m", "2h", "7d", "1w", "forever"; a number alone:
minutes) in seconds: 0 for ever, NONE if it is none */
long network_host_game_duration_parse(char const *text);
/* the teams (0 red, 1 blue) of count players on the current ones (another
value: none), arranged _host_game_* (scores: NULL, or each one's; seed: a
shuffle's) */
void network_host_game_teams_plan(short count, long const *scores, char const *current, short how,
	unsigned long seed, char *teams);

/* whether this machine is a player's host of a network game, in its lobby or
playing it (not a dedicated server, not a game of this machine alone) */
boolean network_host_game_hosting(void);
/* ... of a co-op game (its levels in the campaign's order: no change, no
playlist; network_host_game_hosting is FALSE for it) */
boolean network_host_game_cooperative(void);
/* ... and in the lobby, the game's start not yet sent (co-op's too) */
boolean network_host_game_in_lobby(void);
/* (the host) the game of the entry text (the rotation's: map[:gametype
[+rule=value...]]; a map alone, not a playlist's, keeps the game's
gametype, score to win, time limit and rules) played now or after this game (_host_game_*), from
the playlist's entry at playlist_index (NONE: none); FALSE and why */
boolean network_host_game_change(char const *text, short when, long playlist_index, char *reason, long reason_size);
/* (the host) the game that plays next: one asked for (now: being started),
else the playlist's; FALSE: none (the host picks it) */
boolean network_host_game_next(struct server_entry *entry, boolean *now);
/* (the host) a change asked for, forgotten */
void network_host_game_cancel(void);
/* the playlist (network.host_playlist, read again when it changes), and its
entry played last (NONE: none yet) */
struct server_rotation const *network_host_game_playlist(void);
long network_host_game_playlist_index(void);
/* the playlist replaced (written to network.host_playlist), an entry added
at its end, one taken out (its index), or all: FALSE and why */
boolean network_host_game_playlist_set(char const *text, char *reason, long reason_size);
boolean network_host_game_playlist_add(char const *text, char *reason, long reason_size);
boolean network_host_game_playlist_remove(long index);
/* (the host) the score to win and the minutes (NONE: as they are) of the
lobby's game: set at once in the lobby, else in the next game's lobby; what
waits for it */
boolean network_host_game_set_limits(long score, short minutes);
boolean network_host_game_limits_waiting(long *score, short *minutes);
/* (the host) the lobby the host's machine shows after a game, or (a
restart: network_host_controls.c) after the game it ended: its next game
set in it (a change asked for; else, not restarting, the playlist's); TRUE
if one was */
boolean network_host_game_lobby_take(boolean restarting);
/* an entry's map and gametype as shown ("Blood Gulch \x00B7 CTF") */
/* a gametype's name (the rotation's) as shown: its variant's, else the name
(an admin's Host Controls, of the server's gametypes: network_admin.c) */
void network_host_game_gametype_text(char const *name, wchar_t *text, short size);
void network_host_game_entry_shown(struct server_entry const *entry, wchar_t *text, short size);
/* the multiplayer maps there are, and the gametypes the rotation names
(the built-in ones, OpenCE's), read once a while; their counts */
short network_host_game_maps(struct host_game_map const **maps);
/* the maps past MAXIMUM_HOST_GAME_MAPS the list leaves out */
long network_host_game_maps_left_out(void);
short network_host_game_gametypes(struct host_game_gametype const **gametypes);
/* each frame (network_host_controls_update): the next game said to every
machine as a game's scores show */
void network_host_game_update(void);

/* the lobby's players (network_host_controls_players' there), as its
record has them */
short network_host_game_lobby_players(struct host_controls_player *players, short maximum);
/* (the host) a player of Host Controls' list (in a game or the lobby):
kicked or banned (for seconds, 0 for ever; with a reason, NULL or empty:
the host's), put on the other team, or muted for everyone for minutes (0:
unmuted); whether they are muted (a game's: the minutes left) */
boolean network_host_game_kick_player(struct host_controls_player const *player, boolean ban, long seconds,
	char const *reason);
boolean network_host_game_move_player(struct host_controls_player const *player);
boolean network_host_game_mute_player(struct host_controls_player const *player, short minutes);
short network_host_game_player_mute_minutes(struct host_controls_player const *player);
/* (the host) the teams shuffled or balanced (_host_game_*): in the lobby
its human players' (the bots follow them), in a game everyone's where teams
may move; how many moved, NONE if they cannot be */
short network_host_game_teams_arrange(short how);
boolean network_host_game_teams_arrangeable(void);
/* (the host) a notice every machine shows, as the dedicated server's say
(a game under way's: in the lobby no machine hears one); FALSE (said) */
boolean network_host_game_announce(char const *text);
/* the console's "ban <player> [30m|2h|7d|forever] [reason]", the host's */
boolean network_host_game_ban_command(char const *text);
/* the console's host_map, host_map_next, host_playlist,
host_playlist_add, host_playlist_remove, host_playlist_clear: TRUE if the
expression was one (its result) */
boolean network_host_game_command(char const *expression, boolean *result);
/* ... and host_say, host_shuffle_teams, host_balance_teams,
host_score_limit, host_time_limit, host_bots, host_bot_skill (hs.c) */
boolean network_host_game_console(char const *expression, boolean *result);

#endif // __NETWORK_HOST_GAME_H
