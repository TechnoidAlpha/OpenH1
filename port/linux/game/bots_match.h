/* bots_match.h: a match run from the bot control (port/linux/game/bots_match.c; docs/bot_control.md)

The host's "bots match" commands: start a game (a map and gametype, the
bots of each team, a seed, its limits), restart it, end it, run it faster
than real time where nobody else plays, and read its results. A coach (a
script or an AI agent) runs a whole training loop from them: start, watch,
order, results, again.

A match plays the same twice from the same seed and the same lines at the
same ticks: its results list each line the bots' tick applied, and when
(match at <tick> <line> gives one for a tick), so a match's lines played
again at their ticks (goath1 bots match start ... --replay results.json)
replay it. Its speed changes how many ticks a frame runs, never what a tick
does. */
#ifndef HALO_BOTS_MATCH_H
#define HALO_BOTS_MATCH_H

enum
{
	/* a match's map and gametype names */
	BOTS_MATCH_NAME_SIZE = 64,
	/* the lines a match keeps for its ticks (match at) and lists in its
	results */
	BOTS_MATCH_LINES = 256,
	/* the fastest a match runs (times real time) */
	BOTS_MATCH_SPEED_MAXIMUM = 30,
};

/* a match asked for (match start's words) */
struct bots_match_options
{
	char map[BOTS_MATCH_NAME_SIZE];
	char gametype[BOTS_MATCH_NAME_SIZE];
	/* each team's bots (NONE: as they are), the seed (0: none of its own),
	the score to win and minutes (NONE: the gametype's) */
	long bots_per_team;
	unsigned long seed;
	long score;
	long minutes;
};

/* the pure parts (tools/harness/tests/test_bots_match.py): match start's
words after "start" (map, gametype, --bots N, --seed S, --score N, --time
M) read, FALSE and why for words it does not take; the rotation entry it
plays (map:gametype+score_to_win=N+time_limit=M); whether a speed may run
now (a dedicated server, or a game no other machine is in) and the speed
asked kept to the most there is */
boolean bots_match_parse_start(char **words, long count, struct bots_match_options *options, char *error,
	long error_size);
void bots_match_entry_text(struct bots_match_options const *options, char *text, long size);
boolean bots_match_speed_allowed(boolean server_mode, long remote_machines);
real bots_match_speed_kept(real asked);

/* "match <...>" (bots_command.c's line, its words after "match"): its
answer into reply (its lines), TRUE if it did what it said, else FALSE and
why in error. team: a coach's (0 red, 1 blue), who reads the match and
runs none of it; NONE: the host's */
boolean bots_match_command(char **words, long count, boolean json, short team, char *reply, long reply_size,
	char *error, long error_size);

/* whether the bots join a game with no player in it: once match start ran
(a match of bots alone, a dedicated server's too); else a lobby takes its
bots once a player is in, as always (network_server_manager.c) */
boolean bots_match_bots_alone(void);

/* each frame (bot_control.c, a dedicated server's too): a match asked for
set up in the lobby and started, its seed given, its speed run, its end
found (match_over) and its results kept */
void bots_match_frame(void);
/* each tick that runs the bots (bots_command_tick_begin), and whether the
game could score in it: the match's first and last ticks */
void bots_match_note_tick(long tick, boolean scoring);
/* the start of a tick that runs the bots (bots_command_tick_begin): the
next line that match at gave this tick (TRUE, line), else FALSE */
boolean bots_match_due(long tick, char *line, long size);
/* a line the tick applied (bots_command_apply), for the results' orders:
the teams of the bots it named (a bit each: 1 red, 2 blue) */
void bots_match_note_line(long tick, char const *line, unsigned long teams);
/* a kill (bots_command_note_kill): the knowledge's last kills */
void bots_match_note_kill(long tick, long killer, long victim, char const *weapon);
/* the last kills a team's coach knows of, as JSON (bots_command.c's
knowledge): killer, victim, weapon, tick */
struct server_json;
void bots_match_kills_json(struct server_json *json, char const *key);

#endif
