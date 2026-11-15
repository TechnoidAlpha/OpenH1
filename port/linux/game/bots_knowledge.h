/* bots_knowledge.h: what a bot knows (port/linux/game/bots_knowledge.c)

A bot knows what a player in its place could: the enemies its own sight
test saw (bots.c's look, and its survey of what is in its view every third
of a second), and with bots.share_sightings its team's, each a callout's
delay (bots.share_delay) after a teammate saw it; the map's item spawns and
their turns by the game's clock, which every player can time; whether an
item lies at its spawn only as the bot itself last saw it there or not, and
that it comes back at the spawn's next turn. bots.c goes and looks where it
knows enemies were, and goes for the items it believes are there or about
to be; it aims and fires only at what its own sight test sees now. Nothing
here is a motion tracker: a bot knows of no enemy it (or its team) did not
see.

Everything is the host's, in the tick that runs the bots, from the game
state at the tick: two runs of the same input know the same. Bot numbers are
bots.h's (0 up, by machine).

Needs cseries.h (real, boolean, TICKS_PER_SECOND) and math/real_math.h
(real_point3d) before it. */
#ifndef HALO_BOTS_KNOWLEDGE_H
#define HALO_BOTS_KNOWLEDGE_H

enum
{
	/* the item spawns kept (the map's first of this game's type) */
	BOTS_KNOWLEDGE_MAXIMUM_ITEMS = 48,
	/* the enemies a bot's knowledge gives at most */
	BOTS_KNOWLEDGE_MAXIMUM_KNOWN = 16,
	/* how long an enemy seen is remembered */
	BOTS_KNOWLEDGE_SEEN_MEMORY_TICKS = 10 * TICKS_PER_SECOND,
	/* a name's size (a tag's name's last part: "rocket launcher") */
	BOTS_KNOWLEDGE_NAME_SIZE = 32,
};

/* an item's kind */
enum
{
	_bots_item_weapon = 0,
	/* overshield, active camouflage */
	_bots_item_powerup,
	_bots_item_health,
	_bots_item_grenades,
	_bots_item_vehicle,
	NUMBER_OF_BOTS_ITEM_KINDS
};

/* an item spawn of the map */
struct bots_item
{
	/* _bots_item_* */
	short kind;
	/* a power weapon (rocket launcher, sniper rifle, fuel rod gun,
	flamethrower, shotgun) or a powerup */
	boolean power;
	/* the tag's name's last part ("rocket launcher", "overshield"), of the
	item there now if one is, else of what the spawn gives */
	char name[BOTS_KNOWLEDGE_NAME_SIZE];
	/* where it spawns */
	real_point3d position;
	/* the ticks between the spawn's turns on the game's clock */
	long period_ticks;
};

/* an enemy as a bot knows of it */
struct bots_known_enemy
{
	/* the player (its datum) */
	long player_index;
	/* when it was seen, where, the way it faced */
	long tick;
	real_point3d position;
	real heading;
	/* the bot saw it itself (else its team's report told it) */
	boolean own;
};

/* a map's start and end: everything forgotten */
void bots_knowledge_reset(void);
/* the start of a tick that runs the bots (bots_update_actions, before the
first bot): the settings read, the bots' players taken, the team reports on
their way passed on, the items at their spawns found */
void bots_knowledge_tick_begin(void);

/* an enemy (its datum) the bot's sight test reached this tick, there */
void bots_knowledge_saw(long bot_index, long player_index, real_point3d const *position);
/* an item's spawn the bot's sight test reached this tick, and whether the
item lay there */
void bots_knowledge_saw_item(long bot_index, long item_index, boolean present);

/* the enemies the bot knows of now: those it saw itself and
(bots.share_sightings) those its teammates saw, each known to it
bots.share_delay after it was seen; the latest of each, within
BOTS_KNOWLEDGE_SEEN_MEMORY_TICKS, alive in the life seen. How many (at most
maximum) */
short bots_knowledge_known(long bot_index, struct bots_known_enemy *known, short maximum);

/* the map's item spawns: how many, and one of them with the item lying
there now (its object, NONE for none: the truth, which a bot acts on only
once it saw it there, bots_knowledge_item_believed); FALSE for no such
spawn */
short bots_knowledge_item_count(void);
boolean bots_knowledge_item(long item_index, struct bots_item *item, long *object_index);
/* whether the bot believes the item lies at its spawn: as it last saw the
spawn (one never seen: there, as the game started it), or back since by
the spawn's turn; not, the ticks until its next turn there */
boolean bots_knowledge_item_believed(long bot_index, long item_index, long *ticks_until);

/* what a team (0 red, 1 blue) knows, for its coach (bots_command.c's
knowledge, bots --team): the last sighting of each enemy its bots saw (at
most maximum; tick 0 entries left out), how many; and an item's spawn as
its bots last saw it: FALSE if none of them ever did, else when (seen_tick),
whether the item lay there, and (gone) the ticks until the spawn's next
turn, NONE if it is there or back already */
short bots_knowledge_team_known(short team, struct bots_known_enemy *known, short maximum);
boolean bots_knowledge_team_item(short team, long item_index, long *seen_tick, boolean *present, long *ticks_until);

/* the minute line's part: the sightings and reports */
void bots_knowledge_report(char *line, long size);

#endif
