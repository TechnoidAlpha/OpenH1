/* bots_objectives.h: the bots in the objective games (port/linux/game/bots_objectives.c)

What the bots play for besides kills: Capture the Flag's flags, Oddball's
balls (and Juggernaut's juggernaut), King of the Hill's hills. Each tick
this file reads where the objects of the game are (the flags and balls, who
carries them, the hills the game marks) from the game's own state, gives a
team's bots their parts (attack and defence in Capture the Flag, by the
team's size) and tells bots.c where each bot goes for them. bots.c walks
there, picks up, fights and carries as it does for its other goals.

Nothing random and nothing from another machine: the game state at the tick
and the bots' order, so two runs of the same input play the same. Bot
numbers are bots.h's (0 up, by machine). */
#ifndef HALO_BOTS_OBJECTIVES_H
#define HALO_BOTS_OBJECTIVES_H

/* what a bot goes to do for its game's objective */
enum
{
	_bots_objective_none = 0,
	/* to an object of the game lying in the open (a flag, a ball), to pick
	it up (an enemy's flag, a ball) or touch it (its own team's flag, to
	return it) */
	_bots_objective_take,
	/* carrying a flag: to its team's flag's place, to score */
	_bots_objective_carry,
	/* holding a ball: to a place away from its enemies, kept moving */
	_bots_objective_evade,
	/* after the enemy carrying a flag or a ball (the HUD shows it) */
	_bots_objective_hunt,
	/* with a teammate carrying one, a little behind */
	_bots_objective_escort,
	/* about its team's flag, defending it */
	_bots_objective_guard,
	/* to a hill, and about in it once there */
	_bots_objective_hill,
	NUMBER_OF_BOTS_OBJECTIVES,
};

struct bots_objective
{
	short kind;
	/* before the bot's own wants (its health, a weapon, its team's trades
	and falling back); else after them, before its roaming */
	boolean urgent;
	/* kept to while it fights (it fights as it goes, facing its enemy) */
	boolean press_on;
	/* where to go: the object's, the carrier's, the place to defend or the
	hill's centre */
	real_point3d point;
	/* the object to pick up or touch (_bots_objective_take), the player
	hunted or escorted (their datum); NONE for none */
	long object_index;
	long player_index;
	/* about the point, this far (a guard's ground, a hill's) */
	real radius;
};

/* the game engines the bots play the objectives of (Capture the Flag,
Oddball, King of the Hill) */
int bots_objectives_play(long game_engine_index);

/* a map's start and end: everything forgotten */
void bots_objectives_reset(void);
/* the start of a tick that runs the bots (bots_update_actions): the flags,
balls and hills, and the teams' parts */
void bots_objectives_tick_begin(void);
/* where the bot (its player's datum, where it stands) goes for its game's
objective; FALSE for none (a Slayer game, nothing for it to do) */
boolean bots_objectives_goal(long bot_index, long player_index, real_point3d const *position,
	struct bots_objective *objective);
/* how much lower (better) an enemy's score as the bot's target is: the one
carrying its team's flag, a ball, standing in a hill */
real bots_objectives_target_bonus(long player_index, long target_player_index);
/* the player holds a flag or ball in its hands (it fires it as a blow, and
must not put it away) */
boolean bots_objectives_carrying(long player_index);
/* the point is in a hill now */
boolean bots_objectives_in_hill(real_point3d const *point);

/* the minute line's part (empty in a Slayer game) */
void bots_objectives_report(char *line, long size);

#endif
