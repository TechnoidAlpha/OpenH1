/*
NETWORK_HOST_CONTROLS.H

The host's controls of a network game (network_host_controls.c): what the
pause menu's HOST CONTROLS (menu_functions.c's host_controls_menu.inc), the
scoreboard's player menu (game_engine.c) and the console (hs.c) ask of it.
*/

#ifndef __NETWORK_HOST_CONTROLS_H
#define __NETWORK_HOST_CONTROLS_H
#pragma once

/* ---------- constants */

/* what the host may do to a player (network_host_controls_player_actions:
a bit each) */
enum
{
	_host_controls_kick,
	_host_controls_ban,
	_host_controls_move_team,
	_host_controls_mute,
	NUMBER_OF_HOST_CONTROLS_ACTIONS
};

/* a player's tag on Host Controls' rows */
enum
{
	_host_controls_tag_none,
	_host_controls_tag_host,
	_host_controls_tag_bot,
	_host_controls_tag_openh1,
	_host_controls_tag_opence,
	NUMBER_OF_HOST_CONTROLS_TAGS
};

/* a restart's steps (network_host_controls_restart_step) and what each asks */
enum
{
	_host_controls_restart_none,
	/* the game ended, its scores not yet shown */
	_host_controls_restart_ending,
	/* back to the lobby, which is not yet loaded */
	_host_controls_restart_lobby,
};
enum
{
	_host_controls_restart_wait,
	/* the scores' A, the host's: every machine to the lobby */
	_host_controls_restart_to_lobby,
	/* the lobby shown in the map choice's place, its countdown let go */
	_host_controls_restart_start,
};

enum
{
	/* a line of the host's bans (bans.txt), at most, and its parts */
	HOST_CONTROLS_BAN_LINE_SIZE = 512,
	HOST_CONTROLS_BAN_WHEN_SIZE = 20,
	HOST_CONTROLS_BAN_ADDRESS_SIZE = 32,
	HOST_CONTROLS_BAN_HARDWARE_ID_SIZE = 40,
	HOST_CONTROLS_BAN_TEXT_SIZE = 96,
	/* the bans Host Controls lists, at most */
	MAXIMUM_HOST_CONTROLS_BANS = 128,
};

/* the BOTS tab's rows (network_host_controls_bot_rows) */
enum
{
	_host_bot_row_count,
	_host_bot_row_skill,
	_host_bot_row_teams,
	_host_bot_row_balance,
	_host_bot_row_tactics,
	NUMBER_OF_HOST_BOT_ROWS,
	/* bots.skill's, easy to legendary */
	HOST_BOT_SKILLS = 4,
};

/* the BOTS choices' kinds (network_host_controls_bot_choice) */
enum
{
	_host_bots_none,
	_host_bots_add,
	_host_bots_fill,
};

/* the world behind Host Controls, dimmed (pixel32: black, three quarters
over it) */
#define NETWORK_HOST_CONTROLS_BACKDROP 0xC0000000

/* ---------- structures */

/* who looks: whether Host Controls is this machine's */
struct host_controls_seat
{
	/* this machine runs the game's server */
	boolean server;
	/* game_connection() */
	short connection;
	/* a game's map is loaded and its game under way (not the menus) */
	boolean playing;
	/* the lobby of this machine's network game (network_host_game_in_lobby):
	the players there, before it starts */
	boolean lobby;
};

/* what the host knows of a player, for what it may do to them */
struct host_controls_player_view
{
	/* the host's machine's (its own players) */
	boolean own;
	/* a bot (bots.c's machines) */
	boolean bot;
	/* ... which the host may take out (its network game's: bots.c) */
	boolean bot_removable;
	/* the game has teams, and teams may move in it
	(network_host_controls_team_moves_for) */
	boolean teams;
	boolean team_moves;
};

/* what a game is, for moving its players between teams */
struct host_controls_team_view
{
	/* an OpenH1 game (network_standard_hosting): a Legacy game's machines
	hear nothing new */
	boolean standard;
	/* the gametype has teams */
	boolean teams;
	/* OpenCE machines in it, which would not hear of the move */
	short opence_machines;
};

/* a restart's world */
struct host_controls_restart_view
{
	boolean server;
	/* the game's scores shown (game_engine_showing_postgame) */
	boolean postgame;
	/* the main menu's map loaded, the server in its lobby */
	boolean menu_loaded;
	boolean pregame;
};

/* a line of bans.txt, as distributed_write_player_record writes it (when,
then "key=value" parts separated by tabs), or one a host wrote by hand */
struct host_controls_ban
{
	char when[HOST_CONTROLS_BAN_WHEN_SIZE];
	char address[HOST_CONTROLS_BAN_ADDRESS_SIZE];
	char hardware_id[HOST_CONTROLS_BAN_HARDWARE_ID_SIZE];
	char players[HOST_CONTROLS_BAN_TEXT_SIZE];
	char reason[HOST_CONTROLS_BAN_TEXT_SIZE];
};

/* a player of the game as Host Controls lists them */
struct host_controls_player
{
	/* its datum index (NONE: a lobby's player, which has none) */
	long player_index;
	/* its slot of the game's record, and its machine (NONE: the host's own,
	in a game) */
	long slot;
	long machine_index;
	wchar_t name[12];
	short team;
	long ping;
	short tag;
	unsigned long actions;
};

/* ---------- prototypes/NETWORK_HOST_CONTROLS.C */

/* the pure parts (tools/harness/tests/test_host_controls.py) */
boolean network_host_controls_shown_for(struct host_controls_seat const *seat);
unsigned long network_host_controls_actions_for(struct host_controls_player_view const *view);
boolean network_host_controls_team_moves_for(struct host_controls_team_view const *view);
char network_host_controls_requested_team(boolean locked, boolean host_machine, char requested, char current);
short network_host_controls_restart_step(short stage, struct host_controls_restart_view const *view, short *action);
short network_host_controls_maximum_step(short const *values, short count, short current, short present,
	short step);
boolean network_host_controls_ban_parse(char const *line, struct host_controls_ban *ban);
/* BOTS stepped (no wrapping round): no fewer than none, no more than
BOTS_MAXIMUM or than the game holds (most_players) beside its players; the
count left once the host takes out a bot of those playing (one fewer than
play, or than were asked for); bots.skill's name (in either case), else
NONE */
long network_host_controls_bot_count_step(long count, short step, long humans, long most_players);
long network_host_controls_bot_count_removed(long count, long playing);
/* the BOTS choices (the lobby's BOTS, Host Controls' BOTS, in this order):
NONE; ADD n (bots.count n, bots.make_room off: the bots stay, and a network
game takes as many more players); FILL TO n (bots.make_room on, bots.fill_to
n, bots.count BOTS_MAXIMUM: bots fill the game's empty places to n players
and bots, and leave as players join). n goes one at a time to 16, then by 4s
to BOTS_MAXIMUM, FILL TO's from 2. Their count; a choice's kind
(_host_bots_*) and n; its words ("NONE", "ADD 4", "FILL TO 8"); the choice
settings make (of their kind, the most not past them: settings written by
hand fall between the choices); a choice stepped (-1 or 1), at the ends no
further (never round from the most to none) */
short network_host_controls_bot_choice_count(void);
short network_host_controls_bot_choice(short choice, long *number);
void network_host_controls_bot_choice_words(short choice, wchar_t *text, short size);
short network_host_controls_bot_choice_of(long count, boolean make_room, long fill_to);
short network_host_controls_bot_choice_step(short choice, short step);
short network_host_controls_bot_skill_named(char const *text);
/* whether a listed player is the one chosen: a game's by its datum index, a
lobby's (player_index NONE) by its slot, machine and name (12 characters) */
boolean network_host_controls_same_player(struct host_controls_player const *player, long player_index, long slot,
	long machine_index, wchar_t const *name);

/* whether this machine hosts a network game under way, or is in its lobby
(Host Controls is its, its pause menu's, the lobby's and the scoreboard's) */
boolean network_host_controls_available(void);
/* ... in the lobby (its players those of the game's record) */
boolean network_host_controls_in_lobby(void);
/* the game's players (but those who quit), their count */
short network_host_controls_players(struct host_controls_player *players, short maximum);
/* what the host may do to the player (datum index): a bit of each
_host_controls_* */
unsigned long network_host_controls_player_actions(long player_index);
/* (the host) the player's machine kicked or banned, as the console's kick
and ban do (network_votekick_host_kick, network_server_manager.c) */
boolean network_host_controls_kick(long player_index, boolean ban);
/* (the host) the player put on the other team (a living one's unit killed
first, without statistics, as a quit player's is): every machine told, the
game's OpenH1 machines alone hear it (network_host_controls_team_moves) */
boolean network_host_controls_move_team(long player_index);
/* whether teams may move in this game */
boolean network_host_controls_team_moves(void);

/* (the host) the players' own team changes in the lobby refused (but the
host machine's) */
boolean network_host_controls_teams_locked(void);
void network_host_controls_lock_teams(boolean locked);
/* (the host: network_server_message_handler.c) the team a lobby's player
settings ask for, as the lock keeps it */
char network_host_controls_lobby_team(boolean host_machine, char requested, char current);
/* (the host) the game ended now, as its time limit ends it; or ended and
started again on its map and gametype, through the lobby */
boolean network_host_controls_end_game(void);
boolean network_host_controls_restart(void);
boolean network_host_controls_restarting(void);
/* (the host) the bans (bans.txt), read again; one taken out (its line),
and its address no longer kept out */
short network_host_controls_bans(struct host_controls_ban *bans, char (*lines)[HOST_CONTROLS_BAN_LINE_SIZE],
	short maximum);
boolean network_host_controls_unban(char const *line);
/* (the host) OpenCE players may join the game from now on (an OpenH1 game's) */
boolean network_host_controls_opence_joins(void);
void network_host_controls_set_opence_joins(boolean allowed);
/* the BOTS tab (bots.c's host-owned bots of a network game): whether the
host has it (Host Controls', not in co-op), and whether the game's gametype
is one the bots play (the rows apply to the next one that is); its rows
(_host_bot_row_*: BALANCE TEAMS in a team game whose teams may be
arranged), their texts, and a row's value stepped (left -1,
right or A 1) and written to config.toml, which the game takes at once:
FALSE if it cannot be (BOTS at its ends, a matchmaking game's count, room and
fill, BALANCE TEAMS, which is Host Controls' own) */
boolean network_host_controls_bots_tab(void);
boolean network_host_controls_bots_play(void);
short network_host_controls_bot_rows(short *rows, short maximum);
void network_host_controls_bot_row(short row, wchar_t *label, wchar_t *value, wchar_t *help, short size);
boolean network_host_controls_bot_row_step(short row, short step);
/* (the host) bots.count and bots.skill set (the console's host_bots and
host_bot_skill): FALSE past the game's limits, in a matchmaking game (the
count), or without the BOTS tab */
boolean network_host_controls_set_bots(long count);
/* the BOTS choice the settings make now (a matchmaking game's playlist's
too), and a choice's settings written (any n of its kind up to
BOTS_MAXIMUM: the console's host_bots; a game of this machine alone's lobby
too): FALSE in a matchmaking game, or if they cannot be written */
short network_host_controls_bots_chosen(void);
boolean network_host_controls_choose_bots(short kind, long number);
boolean network_host_controls_set_bot_skill(short skill);
/* the bots of the game (or the lobby) as the Players tab lists them */
short network_host_controls_bots(struct host_controls_player *bots, short maximum);
/* (a bot taken out by the host) bots.count one fewer than play */
void network_host_controls_bots_one_fewer(void);
/* Host Controls opened on the Players tab, the player chosen (NONE: none),
for the local player (the scoreboard's "Host controls...", the console's
host_controls) */
boolean network_host_controls_open(long player_index, short tab);
/* the console's host_controls: its tab ("players", "game", "maps", "playlist", "bans", "bots", "rules"; none:
Players) and a player chosen by their name's start; or "end" and "restart",
Game's End game now and Restart round; or "unban <players or address>" */
boolean network_host_controls_open_command(char const *text);
/* whether Host Controls is shown now (its update ran a moment ago): the
HUD hidden under it, the world dimmed (interface.c) */
boolean network_host_controls_shown(void);
/* (the screen's update, each frame) the player (NONE: none) and tab an
opening asked for, taken once; FALSE: none asked */
boolean network_host_controls_take_opening(long *player_index, short *tab);
/* each frame (main.c): a restart's steps, the lock forgotten once the
server goes */
void network_host_controls_update(void);
/* the team-move message (an OpenH1 game's: network_distributed.c) */
boolean network_host_controls_handles_message(word const *message, word size);
void network_host_controls_handle_message(long machine_index, word const *message, word size, boolean from_stream);

#endif // __NETWORK_HOST_CONTROLS_H
