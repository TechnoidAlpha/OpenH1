/* bots_command.h: the host directs its bots (port/linux/game/bots_command.c; docs/bot_control.md)

The host of a game with bots tells them what to do and how to play, as a
commander tells a squad: orders (attack, defend, hold, follow, push,
retreat, regroup, go to a place, clear) and traits (skill, aggression,
teamwork, a weapon and the vehicles they like, a style), each for one bot,
a team or all of them; and it adds, removes and fills bots and moves them
between teams. The bots still aim, move and see for themselves: an order is
a goal (bots_objectives.h's struct, in place of the game's objective), and
they know no more of their enemies than a player in their place would.

The commands come as lines of text, the same from the host's console, a
dedicated server's console, RCON and HTTP API (its admin role: "bots
<command>"), and the local control connection (bot_control.c, the CLI's:
"goath1 bots <command>"). Queries are answered at once from the game
state. Orders, traits and team moves are queued and applied at the next
tick that runs the bots, inside the simulation, in the order they came, and
the demo records each as applied (demo.c's W record), so a game plays the
same however the lines came.

Bot numbers are bots.h's (0 up, by machine); the commands show them from
1, as halo.log does. */
#ifndef HALO_BOTS_COMMAND_H
#define HALO_BOTS_COMMAND_H

struct bots_objective;

/* an order's kind */
enum
{
	/* none: its own play */
	_bots_command_free = 0,
	_bots_command_attack,
	_bots_command_defend,
	_bots_command_hold,
	_bots_command_follow,
	_bots_command_goto,
	_bots_command_retreat,
	_bots_command_regroup,
	_bots_command_push,
	NUMBER_OF_BOTS_COMMAND_ORDERS,
};

/* the longest line, and the most lines waiting for a tick */
enum
{
	BOTS_COMMAND_LINE_SIZE = 256,
	BOTS_COMMAND_QUEUE = 32,
};

/* the map's start and end: every order forgotten (the traits are kept) */
void bots_command_reset(void);
/* the start of a tick that runs the bots, on the host: the lines queued
since the last applied, then the flags' events */
void bots_command_tick_begin(void);

/* the goal of the bot's order (its player's datum, where it stands), in
place of its game's objective; FALSE: no order, or none now (it plays its
own game, the game's objective) */
boolean bots_command_objective(long bot_index, long player_index, real_point3d const *position,
	struct bots_objective *objective);
/* the bot's traits: its skill (NONE: bots.skill's), aggression and
teamwork (0 to 1; 0.5 and 1 play as the bots always did), the weapon it
goes for ("" for none of its own), whether it takes a vehicle of the kind
(bots_vehicles.h's) */
short bots_command_skill(long bot_index);
real bots_command_aggression(long bot_index);
real bots_command_teamwork(long bot_index);
char const *bots_command_weapon(long bot_index);
boolean bots_command_vehicle_allowed(long bot_index, short vehicle_kind);
/* the team bot number bot_index joins as (bots add <n> <team>), NONE for
the game's choice (bots_team) */
short bots_command_join_team(long bot_index);
/* how much lower (better) an enemy's score as the bot's target is: the
player it was told to attack */
real bots_command_target_bonus(long bot_index, long target_player_index);

/* a player killed (network_distributed.c, the host's): the watch's event,
and an attack order on the dead player done */
void bots_command_note_kill(long killing_player_index, long killing_object_index, long dead_player_index);

/* a command's line run (without "bots"): its answer's lines into reply
(each ending in a newline; one line of JSON with --json), TRUE if it did
what it said. Not on a client: the host's alone */
boolean bots_command_run(char const *line, char *reply, long reply_size);
/* the console's "bots <command>" (hs.c, a game this machine hosts): TRUE if
the line was one, its answer printed */
boolean bots_command_console(char const *expression, boolean *result);

#endif
