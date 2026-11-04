/*
BOTS_TACTICS.C

The bots of a team playing together (bots_tactics.h). bots.c plays each bot;
this file is what the bots of a team know of each other and of the fight as
a whole, and asks of them:

- Spacing: a walking bot turns away from a teammate within 1.6 units (about
  five metres) and eases off behind one ahead of it; a fighting one strafes
  away from a teammate that close. A bot further than 25 units from every
  teammate goes back to the nearest (a human first) until it is within 12.
- Crossfire: a bot that fights the enemy a teammate fights strafes round
  that enemy away from the teammate, while their angles on it are within 60
  degrees.
- Focus fire: the enemy that a teammate (a bot or a human) hurt within the
  last second and a half, by the damage the game records on its unit, is a
  better target.
- Trading: a player of the team killed by an enemy, the nearest bot of the
  team goes to where its teammate fell, then where it
  knows the killer is (its own sight, or a teammate's sighting shared a
  callout's delay late: bots_knowledge_known), and
  every bot of the team favours the killer as a target, until the killer's
  shields are back as a bot of the team with it as its target saw them (six
  seconds at most). Where the killer was when it killed is not the team's
  to know, nor its shields while no bot of the team is on it.
- Collapsing and falling back: the team kills an enemy, its bots within 30
  units close in on the teammate that made the kill (where it stands, which
  the team sees; not where the enemy fell) for five seconds; the team loses
  two players more than it kills in ten seconds, its bots near the fight
  (those fighting too, still facing their enemy: bots.c) fall back together
  to a point between the team and where it spawns, for six.
- Roles: every two seconds, each power item (bots_knowledge_item's power) is
  the best placed bot's of each team (the nearest, the one hurt or holding a
  power weapon already less so; not one in a fight, on a trade or falling
  back), and the others leave it, for four seconds: a bot chosen that has
  not gone for it by then lets the next take it. Each human of the team has
  the nearest bot with it as its escort, while the team keeps a bot free.

Their game's objective (a flag, a ball, a hill: bots_objectives.h) comes
before the team's asks but its trades and falling back. What the team knows is what its players could: where its own players are
(the motion tracker and the HUD show teammates), the kill feed, the damage
its own players did; of the enemies only what its bots saw.

debug.bots_tactics_team gives the team play to one team's bots alone, so
that a game measures it against bots that play on their own.

The counts of the minute line are kept with bots.tactics off as well: the
teams' kills and deaths, the trades (a killer of the team's killed within six
seconds), the targets a teammate was hurting, the angles between two bots on
one enemy and the distance to the nearest teammate, sampled each second.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "bots.h"
#include "bots_knowledge.h"
#include "bots_tactics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* port/linux/src/port_config.c */
int config_boolean(char const *name);
long config_integer(char const *name);

#define TACTICS_PI 3.14159265f
#define TACTICS_DEGREES (TACTICS_PI / 180.0f)
#define TACTICS_PLAYERS HALO_PORT_MAXIMUM_NETWORK_PLAYERS
#define TACTICS_TEAMS 2
/* the kills and deaths of a team kept, for whether it loses the fight */
#define TACTICS_EVENTS 8

/* (world units: one is 3.048 metres) */
#define TACTICS_SPACING 1.6f
#define TACTICS_SUPPORT_FAR 25.0f
#define TACTICS_SUPPORT_NEAR 12.0f
#define TACTICS_ESCORT_NEAR 4.0f
#define TACTICS_COLLAPSE_REACH 30.0f
#define TACTICS_FALL_BACK_REACH 40.0f
#define TACTICS_TRADE_REACH 40.0f
#define TACTICS_ITEM_REACH 50.0f
#define TACTICS_CROSSFIRE_DEGREES 60.0f

struct tactics_player
{
	/* its datum, NONE for none; the tick it was last seen in the players */
	long datum;
	long seen_tick;
	long unit_index;
	/* 0 or 1, NONE without teams */
	short team;
	boolean alive;
	boolean bot;
	real_point3d position;
	/* its shield, and when a teammate of its enemies last hurt it */
	real shield;
};

struct tactics_bot
{
	/* its player (its datum), NONE for none; the tick of its last tick */
	long player_index;
	long done_tick;
	/* the enemy it fought at its last tick (its datum, NONE), whether it
	fired */
	long target_player_index;
	boolean firing;
	/* the team's ask of it, where, until its next look at it */
	short tactic;
	real_point3d point;
	long decide_tick;
	/* it goes back to a teammate (support), until within reach */
	boolean supporting;
	/* the human it escorts (its datum, NONE) */
	long escort_player_index;
};

struct tactics_team
{
	long death_ticks[TACTICS_EVENTS];
	long kill_ticks[TACTICS_EVENTS];
	long death_next;
	long kill_next;
	real_point3d last_death_point;
	/* a teammate's killer gone after: its datum (NONE for none), where the
	team knew it last, from when to when, the bot sent */
	long trade_killer;
	real_point3d trade_point;
	long trade_start_tick;
	long trade_until_tick;
	long trade_bot;
	/* the place of the team's last kill, closed in on until then */
	real_point3d pick_point;
	long pick_until_tick;
	/* falling back to the rally point until then, not again before the
	next */
	real_point3d rally_point;
	long fall_back_until_tick;
	long fall_back_next_tick;
	/* where its players spawn, summed */
	real_point3d home_sum;
	long home_count;
	/* the bot best placed for each power item (bot number, NONE), and
	since when it is */
	long item_bots[BOTS_KNOWLEDGE_MAXIMUM_ITEMS];
	long item_bot_ticks[BOTS_KNOWLEDGE_MAXIMUM_ITEMS];
	/* the killer's shield as the team last saw it (a bot of the team on it
	as its target), the tick of that */
	real trade_seen_shield;
	long trade_seen_tick;
};

static struct
{
	boolean on;
	/* debug.bots_tactics_team: the one team whose bots play together (NONE:
	both) */
	short only_team;
	long now;
	long roles_tick;
	struct tactics_player players[TACTICS_PLAYERS];
	struct tactics_bot bots[BOTS_MAXIMUM];
	struct tactics_team teams[TACTICS_TEAMS];

	/* the minute line's counts (kept with bots.tactics off too) */
	long kills[TACTICS_TEAMS];
	long deaths[TACTICS_TEAMS];
	long trade_chances;
	long trades;
	long focus_picks;
	long crossfire_pairs;
	real crossfire_degrees;
	long crossfire_narrow;
	long spacing_samples;
	real spacing_sum;
	long spacing_close;
	long spacing_alone;
	/* what tactics did (bots.tactics on) */
	long tactic_counts[NUMBER_OF_BOTS_TACTICS];
	long crossfire_strafes;
	long spacing_strafes;
	long steer_ticks;
	long items_deferred;
	long escorts;
	long fall_backs;
} bots_tactics;

/* ---------- helpers */

static real tactics_angle_difference(real a, real b)
{
	real difference = a - b;

	while (difference > TACTICS_PI)
		difference -= 2.0f * TACTICS_PI;
	while (difference < -TACTICS_PI)
		difference += 2.0f * TACTICS_PI;
	return difference;
}

static real tactics_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

/* the player's entry (its absolute index), NULL for one not in the game
this tick */
static struct tactics_player *tactics_player(long player_index)
{
	long absolute_index;
	struct tactics_player *entry;

	if (player_index == NONE)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (absolute_index < 0 || absolute_index >= TACTICS_PLAYERS)
		return NULL;
	entry = &bots_tactics.players[absolute_index];
	return entry->datum == player_index && entry->seen_tick == bots_tactics.now ? entry : NULL;
}

/* the bot's own entry and team, NULL for a bot not in a team now */
static struct tactics_player *tactics_bot_player(long bot_index, struct tactics_team **team)
{
	struct tactics_player *player;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return NULL;
	player = tactics_player(bots_tactics.bots[bot_index].player_index);
	if (!player || player->team == NONE || !player->alive)
		return NULL;
	if (team)
		*team = &bots_tactics.teams[player->team];
	return player;
}

/* whether the team play is the bot's (bots.tactics, and
debug.bots_tactics_team) */
static boolean tactics_on(struct tactics_player const *player)
{
	return bots_tactics.on && player &&
		(bots_tactics.only_team == NONE || player->team == bots_tactics.only_team);
}

/* the number of a team's events (kills or deaths) within the last seconds */
static long tactics_recent(long const *ticks, long now, long seconds)
{
	long index, count = 0;

	for (index = 0; index < TACTICS_EVENTS; index++)
	{
		if (ticks[index] > 0 && now - ticks[index] <= seconds * TICKS_PER_SECOND)
			count++;
	}
	return count;
}

/* a team loses the fight: two of its players more killed than it killed in
ten seconds */
static boolean tactics_losing(long const *death_ticks, long const *kill_ticks, long now)
{
	return tactics_recent(death_ticks, now, 10) - tactics_recent(kill_ticks, now, 10) >= 2;
}

/* the side a bot facing its target strafes to (1: its left, towards the
target's direction turned 90 degrees anticlockwise; -1: its right) away
from a teammate */
static real tactics_side_away(real_point3d const *position, real_point3d const *target, real_point3d const *mate)
{
	real to_target = (real)atan2(target->y - position->y, target->x - position->x);

	return (mate->x - position->x) * (real)cos(to_target + TACTICS_PI * 0.5f) +
		(mate->y - position->y) * (real)sin(to_target + TACTICS_PI * 0.5f) > 0.0f ? -1.0f : 1.0f;
}

/* the angle (radians, -pi to pi) of the bot round the target from the
teammate */
static real tactics_angle_round(real_point3d const *position, real_point3d const *target, real_point3d const *mate)
{
	return tactics_angle_difference((real)atan2(position->y - target->y, position->x - target->x),
		(real)atan2(mate->y - target->y, mate->x - target->x));
}

/* the side that takes the bot round the target away from the teammate: a
strafe to the left (facing the target) turns it clockwise round the target;
first_of_two decides when both stand on one line from it */
static real tactics_crossfire_side(real_point3d const *position, real_point3d const *target, real_point3d const *mate,
	boolean first_of_two)
{
	real difference = tactics_angle_round(position, target, mate);

	if (difference > 0.0f || (difference == 0.0f && !first_of_two))
		return -1.0f;
	return 1.0f;
}

/* a walking direction (radians) turned away from a teammate (dx, dy from
the bot, distance away, within TACTICS_SPACING): the nearer, the more, 40
degrees at most; the throttle eased behind one ahead and close */
static real tactics_steer_away(real direction, real dx, real dy, real distance, real *throttle)
{
	real along = dx * (real)cos(direction) + dy * (real)sin(direction);
	real across = dy * (real)cos(direction) - dx * (real)sin(direction);
	real turn = 40.0f * TACTICS_DEGREES * (1.0f - distance / TACTICS_SPACING);

	if (along > 0.3f * distance && distance < 1.2f)
		*throttle *= 0.6f;
	return direction + (across > 0.0f ? -turn : turn);
}

/* a teammate of the player of that entry (not the player) hurt the unit
within the last second and a half */
static boolean tactics_hurt_by_team(long unit_index, struct tactics_player const *player)
{
	struct unit_datum *unit = unit_index != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit) : NULL;
	short index;

	if (!unit)
		return FALSE;
	for (index = 0; index < MAXIMUM_ATTACKERS_PER_UNIT; index++)
	{
		struct unit_attacker const *attacker = &unit->unit.attackers[index];
		struct tactics_player const *other;

		if (attacker->game_time_stamp == (unsigned long)NONE ||
			bots_tactics.now - (long)attacker->game_time_stamp > TICKS_PER_SECOND * 3 / 2 ||
			attacker->player_index == player->datum)
		{
			continue;
		}
		other = tactics_player(attacker->player_index);
		if (other && other->team == player->team)
			return TRUE;
	}
	return FALSE;
}

/* ---------- deaths and spawns */

/* who killed the player whose unit that was: the enemy that hurt it last
(the game's record of its attackers), within five seconds; NULL for none (a
fall, a teammate's, its own) */
static struct tactics_player *tactics_killer(struct tactics_player const *dead, long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit) : NULL;
	struct tactics_player *killer = NULL;
	long latest = NONE;
	short index;

	if (!unit)
		return NULL;
	for (index = 0; index < MAXIMUM_ATTACKERS_PER_UNIT; index++)
	{
		struct unit_attacker const *attacker = &unit->unit.attackers[index];

		if (attacker->game_time_stamp == (unsigned long)NONE ||
			bots_tactics.now - (long)attacker->game_time_stamp > TICKS_PER_SECOND * 5 ||
			(long)attacker->game_time_stamp <= latest)
		{
			continue;
		}
		latest = (long)attacker->game_time_stamp;
		killer = tactics_player(attacker->player_index);
	}
	if (!killer || killer->datum == dead->datum || killer->team == dead->team || killer->team == NONE)
		return NULL;
	return killer;
}

/* the nearest bot of the team, alive, within reach of the point; NONE for
none */
static long tactics_nearest_free_bot(short team, real_point3d const *point, real reach)
{
	long index, best = NONE;
	real best_distance = reach;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct tactics_player *player = tactics_bot_player(index, NULL);
		real distance;

		if (!player || player->team != team)
			continue;
		distance = tactics_distance(&player->position, point);
		if (distance < best_distance)
		{
			best_distance = distance;
			best = index;
		}
	}
	return best;
}

/* the rally point of a team falling back: its living players' middle, moved
towards where it spawns by 10 units at most */
static void tactics_rally_point(short team, real_point3d *point)
{
	struct tactics_team *side = &bots_tactics.teams[team];
	long index, count = 0;
	real_point3d sum = { 0.0f, 0.0f, 0.0f };

	for (index = 0; index < TACTICS_PLAYERS; index++)
	{
		struct tactics_player *player = &bots_tactics.players[index];

		if (player->seen_tick != bots_tactics.now || !player->alive || player->team != team)
			continue;
		sum.x += player->position.x;
		sum.y += player->position.y;
		sum.z += player->position.z;
		count++;
	}
	if (!count)
	{
		*point = side->last_death_point;
		return;
	}
	point->x = sum.x / count;
	point->y = sum.y / count;
	point->z = sum.z / count;
	if (side->home_count > 0)
	{
		real_point3d home;
		real distance, step;

		home.x = side->home_sum.x / side->home_count;
		home.y = side->home_sum.y / side->home_count;
		home.z = side->home_sum.z / side->home_count;
		distance = tactics_distance(point, &home);
		step = distance > 10.0f ? 10.0f / distance : 1.0f;
		point->x += (home.x - point->x) * step;
		point->y += (home.y - point->y) * step;
		point->z += (home.z - point->z) * step;
	}
}

/* a player of the game died since the last tick: the counts, a trade of the
killer, the killer's team closing in, the dead player's falling back */
static void tactics_player_died(struct tactics_player *dead, long unit_index)
{
	struct tactics_player *killer = tactics_killer(dead, unit_index);
	long now = bots_tactics.now;
	short team;

	if (dead->team == NONE)
		return;
	/* (the killer of a teammate of its enemies, killed while they go after
	it: traded) */
	for (team = 0; team < TACTICS_TEAMS; team++)
	{
		struct tactics_team *side = &bots_tactics.teams[team];

		if (side->trade_killer == dead->datum && now <= side->trade_until_tick)
		{
			if (killer && killer->team == team)
				bots_tactics.trades++;
			side->trade_killer = NONE;
		}
	}
	bots_tactics.deaths[dead->team]++;
	{
		struct tactics_team *side = &bots_tactics.teams[dead->team];

		side->death_ticks[side->death_next] = now;
		side->death_next = (side->death_next + 1) % TACTICS_EVENTS;
		side->last_death_point = dead->position;
		if (killer)
		{
			bots_tactics.trade_chances++;
			side->trade_killer = killer->datum;
			/* (where the teammate fell, which the team knows: not where
			the killer stood) */
			side->trade_point = dead->position;
			side->trade_start_tick = now;
			side->trade_until_tick = now + TICKS_PER_SECOND * 6;
			side->trade_seen_shield = 0.0f;
			side->trade_seen_tick = NONE;
			side->trade_bot = tactics_nearest_free_bot(dead->team, &dead->position, TACTICS_TRADE_REACH);
		}
		/* (two more lost than killed in ten seconds: the fight goes against
		it) */
		if (tactics_losing(side->death_ticks, side->kill_ticks, now) && now >= side->fall_back_next_tick)
		{
			side->fall_back_until_tick = now + TICKS_PER_SECOND * 6;
			side->fall_back_next_tick = now + TICKS_PER_SECOND * 15;
			side->pick_until_tick = 0;
			tactics_rally_point(dead->team, &side->rally_point);
			bots_tactics.fall_backs++;
		}
	}
	if (killer)
	{
		struct tactics_team *side = &bots_tactics.teams[killer->team];

		bots_tactics.kills[killer->team]++;
		side->kill_ticks[side->kill_next] = now;
		side->kill_next = (side->kill_next + 1) % TACTICS_EVENTS;
		/* (where the teammate that made the kill stands, which its team
		sees, not where the enemy fell, which only the killer saw) */
		if (now >= side->fall_back_until_tick)
		{
			side->pick_point = killer->position;
			side->pick_until_tick = now + TICKS_PER_SECOND * 5;
		}
	}
}

/* ---------- roles */

/* whether the unit holds a weapon whose tag's name has that in it */
static boolean tactics_holds(long unit_index, char const *part)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	short slot;

	if (!unit || !*part)
		return FALSE;
	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long held = unit->unit.weapon_object_indices[slot];
		char const *name = held != NONE ? tag_get_name(weapon_get(held)->definition_index) : NULL;

		if (name && strstr(name, part))
			return TRUE;
	}
	return FALSE;
}

/* each human of the team its escort, the bot nearest it (the escort it had
kept, unless another is nearer by 10 units), while the team keeps a bot of
its own */
static void tactics_choose_escorts(short team)
{
	long index, free_count = 0;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct tactics_bot *bot = &bots_tactics.bots[index];
		struct tactics_player *player = tactics_player(bot->player_index);
		struct tactics_player *human = tactics_player(bot->escort_player_index);

		if (!player || player->team != team)
			continue;
		/* (an escort dead, or whose human is gone, is free) */
		if (!player->alive || !human || !human->alive)
			bot->escort_player_index = NONE;
		if (player->alive && bot->escort_player_index == NONE)
			free_count++;
	}
	for (index = 0; index < TACTICS_PLAYERS; index++)
	{
		struct tactics_player *human = &bots_tactics.players[index];
		long bot_index, best = NONE, kept = NONE;
		real best_score = REAL_MAX, kept_score = REAL_MAX;

		if (human->seen_tick != bots_tactics.now || human->bot || !human->alive || human->team != team)
			continue;
		for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
		{
			struct tactics_player *player = tactics_bot_player(bot_index, NULL);
			struct tactics_bot *bot = &bots_tactics.bots[bot_index];
			struct unit_datum *unit;
			real score;

			if (!player || player->team != team ||
				(bot->escort_player_index != NONE && bot->escort_player_index != human->datum))
			{
				continue;
			}
			unit = (struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
			score = tactics_distance(&player->position, &human->position) +
				(unit && unit->object.body_vitality < 0.5f ? 15.0f : 0.0f);
			if (bot->escort_player_index == human->datum)
			{
				kept = bot_index;
				kept_score = score;
			}
			if (score < best_score)
			{
				best_score = score;
				best = bot_index;
			}
		}
		if (kept != NONE && best != NONE && kept_score <= best_score + 10.0f)
			best = kept;
		if (kept != NONE && best != kept)
		{
			bots_tactics.bots[kept].escort_player_index = NONE;
			free_count++;
		}
		if (best == NONE || (kept == NONE && free_count < 2))
			continue;
		if (best != kept)
		{
			bots_tactics.bots[best].escort_player_index = human->datum;
			bots_tactics.escorts++;
			free_count--;
		}
	}
}

/* whether the bot is in a fight (it fought an enemy at its last tick) or on
a goal of the team's that comes before an item (a trade, falling back) */
static boolean tactics_bot_busy(struct tactics_bot const *bot)
{
	return (bot->target_player_index != NONE && bots_tactics.now - bot->done_tick <= 1) ||
		bot->tactic == _bots_tactic_trade || bot->tactic == _bots_tactic_fall_back;
}

/* each power item the best placed bot's of the team: within 50 units, not
holding it, no escort, not in a fight nor on a trade or falling back; the
nearest, one hurt or holding another power weapon less so */
static void tactics_choose_item_bots(short team)
{
	struct tactics_team *side = &bots_tactics.teams[team];
	short count = bots_knowledge_item_count(), item_index;

	for (item_index = 0; item_index < BOTS_KNOWLEDGE_MAXIMUM_ITEMS; item_index++)
	{
		struct bots_item item;
		long bot_index, best = NONE, before = side->item_bots[item_index];
		real best_score = REAL_MAX;

		side->item_bots[item_index] = NONE;
		if (item_index >= count || !bots_knowledge_item(item_index, &item, NULL) || !item.power)
			continue;
		for (bot_index = 0; bot_index < BOTS_MAXIMUM; bot_index++)
		{
			struct tactics_player *player = tactics_bot_player(bot_index, NULL);
			struct unit_datum *unit;
			real distance, score;
			short other;

			/* (a bot in a fight, or sent by the team elsewhere, would not go) */
			if (!player || player->team != team || bots_tactics.bots[bot_index].escort_player_index != NONE ||
				tactics_bot_busy(&bots_tactics.bots[bot_index]))
			{
				continue;
			}
			distance = tactics_distance(&player->position, &item.position);
			if (distance > TACTICS_ITEM_REACH ||
				(item.kind == _bots_item_weapon && tactics_holds(player->unit_index, item.name)))
			{
				continue;
			}
			unit = (struct unit_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit);
			score = distance;
			if (unit && unit->object.body_vitality < 0.5f)
				score += 15.0f;
			for (other = 0; other < count; other++)
			{
				struct bots_item held;

				if (other != item_index && bots_knowledge_item(other, &held, NULL) && held.power &&
					held.kind == _bots_item_weapon && tactics_holds(player->unit_index, held.name))
				{
					score += 25.0f;
					break;
				}
			}
			if (score < best_score)
			{
				best_score = score;
				best = bot_index;
			}
		}
		side->item_bots[item_index] = best;
		if (best != before)
			side->item_bot_ticks[item_index] = bots_tactics.now;
	}
}

/* ---------- the tick */

void bots_tactics_reset(void)
{
	long index;
	short team;

	csmemset(&bots_tactics, 0, sizeof(bots_tactics));
	for (index = 0; index < TACTICS_PLAYERS; index++)
	{
		bots_tactics.players[index].datum = NONE;
		bots_tactics.players[index].seen_tick = NONE;
	}
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_tactics.bots[index].player_index = NONE;
		bots_tactics.bots[index].target_player_index = NONE;
		bots_tactics.bots[index].escort_player_index = NONE;
	}
	for (team = 0; team < TACTICS_TEAMS; team++)
	{
		bots_tactics.teams[team].trade_killer = NONE;
		bots_tactics.teams[team].trade_bot = NONE;
		for (index = 0; index < BOTS_KNOWLEDGE_MAXIMUM_ITEMS; index++)
			bots_tactics.teams[team].item_bots[index] = NONE;
	}
}

/* each second: the distance of each bot to its nearest teammate, and the
angle between two bots of a team on one enemy */
static void tactics_sample(void)
{
	long index, other;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct tactics_player *player = tactics_bot_player(index, NULL);
		struct tactics_bot *bot = &bots_tactics.bots[index];
		struct tactics_player *target;
		real nearest = REAL_MAX;

		if (!player)
			continue;
		for (other = 0; other < TACTICS_PLAYERS; other++)
		{
			struct tactics_player *mate = &bots_tactics.players[other];

			if (mate != player && mate->seen_tick == bots_tactics.now && mate->alive && mate->team == player->team)
				nearest = MIN(nearest, tactics_distance(&mate->position, &player->position));
		}
		if (nearest < REAL_MAX)
		{
			bots_tactics.spacing_samples++;
			bots_tactics.spacing_sum += MIN(nearest, 60.0f);
			if (nearest < TACTICS_SPACING)
				bots_tactics.spacing_close++;
			if (nearest > TACTICS_SUPPORT_FAR)
				bots_tactics.spacing_alone++;
		}
		target = bot->done_tick == bots_tactics.now - 1 ? tactics_player(bot->target_player_index) : NULL;
		if (!target || !target->alive)
			continue;
		for (other = index + 1; other < BOTS_MAXIMUM; other++)
		{
			struct tactics_player *mate = tactics_bot_player(other, NULL);
			struct tactics_bot *mate_bot = &bots_tactics.bots[other];
			real separation;

			if (!mate || mate->team != player->team || mate_bot->done_tick != bots_tactics.now - 1 ||
				mate_bot->target_player_index != bot->target_player_index)
			{
				continue;
			}
			separation = (real)fabs(tactics_angle_round(&player->position, &target->position, &mate->position));
			bots_tactics.crossfire_pairs++;
			bots_tactics.crossfire_degrees += separation / TACTICS_DEGREES;
			if (separation < 30.0f * TACTICS_DEGREES)
				bots_tactics.crossfire_narrow++;
		}
	}
}

void bots_tactics_tick_begin(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long now = game_time_get();
	/* the players who died since the last tick, and their units: taken once
	every player's entry is this tick's (a killer after its victim in the
	players' order) */
	struct tactics_player *dead[TACTICS_PLAYERS];
	long dead_units[TACTICS_PLAYERS];
	long dead_count = 0, index;
	short team;

	bots_tactics.on = config_boolean("bots.tactics");
	{
		long only_team = config_integer("debug.bots_tactics_team");

		bots_tactics.only_team = only_team == 0 || only_team == 1 ? (short)only_team : (short)NONE;
	}
	bots_tactics.now = now;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct tactics_player *entry;
		struct object_datum *object;
		boolean alive;

		if (absolute_index < 0 || absolute_index >= TACTICS_PLAYERS)
			continue;
		entry = &bots_tactics.players[absolute_index];
		if (entry->datum != iterator.datum_index)
		{
			csmemset(entry, 0, sizeof(*entry));
			entry->datum = iterator.datum_index;
			entry->unit_index = NONE;
		}
		entry->seen_tick = now;
		entry->team = game_engine_has_teams() && player->team_index >= 0 && player->team_index < TACTICS_TEAMS ?
			(short)player->team_index : (short)NONE;
		entry->bot = bots_player_is_bot(iterator.datum_index);
		object = player->unit_index != NONE ?
			(struct object_datum *)object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit) : NULL;
		alive = object && !TEST_FLAG(object->object.damage_flags, _object_dead_bit);
		if (entry->alive && (!alive || player->unit_index != entry->unit_index))
		{
			dead[dead_count] = entry;
			dead_units[dead_count] = entry->unit_index;
			dead_count++;
		}
		if (alive)
		{
			struct object_datum *parent = object->object.parent_object_index != NONE ?
				object_try_and_get(object->object.parent_object_index) : NULL;

			/* (spawned: where its team spawns) */
			if (!entry->alive && entry->team != NONE)
			{
				struct tactics_team *side = &bots_tactics.teams[entry->team];

				side->home_sum.x += object->object.position.x;
				side->home_sum.y += object->object.position.y;
				side->home_sum.z += object->object.position.z;
				side->home_count++;
			}
			/* (in a vehicle: where the vehicle is) */
			entry->position = parent ? parent->object.position : object->object.position;
			entry->shield = object->object.shield_vitality;
		}
		entry->alive = alive;
		entry->unit_index = alive ? player->unit_index : NONE;
	}
	for (index = 0; index < dead_count; index++)
		tactics_player_died(dead[index], dead_units[index]);
	for (team = 0; team < TACTICS_TEAMS; team++)
	{
		struct tactics_team *side = &bots_tactics.teams[team];
		struct tactics_player *killer = tactics_player(side->trade_killer);

		/* (a trade ends with its time, the killer gone, or the killer's
		shields back as a bot of the team saw them: not the shields of a
		killer no one sees) */
		if (side->trade_killer != NONE && (!killer || !killer->alive || now > side->trade_until_tick ||
			(now - side->trade_start_tick > TICKS_PER_SECOND * 2 && side->trade_seen_tick != NONE &&
				now - side->trade_seen_tick <= 1 && side->trade_seen_shield >= 1.0f)))
		{
			side->trade_killer = NONE;
		}
	}
	if (now >= bots_tactics.roles_tick && game_engine_has_teams())
	{
		bots_tactics.roles_tick = now + TICKS_PER_SECOND * 2;
		for (team = 0; team < TACTICS_TEAMS; team++)
		{
			tactics_choose_escorts(team);
			tactics_choose_item_bots(team);
		}
	}
	if (now % TICKS_PER_SECOND == 0 && game_engine_has_teams())
		tactics_sample();
}

void bots_tactics_bot_done(long bot_index, long player_index, long target_player_index, boolean firing)
{
	struct tactics_bot *bot;
	struct tactics_player *player, *target;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return;
	bot = &bots_tactics.bots[bot_index];
	if (bot->player_index != player_index)
	{
		csmemset(bot, 0, sizeof(*bot));
		bot->player_index = player_index;
		bot->escort_player_index = NONE;
		bot->target_player_index = NONE;
	}
	player = tactics_player(player_index);
	target = tactics_player(target_player_index);
	/* (a new target a teammate was hurting: a focused pick) */
	if (target && player && player->team != NONE && target_player_index != bot->target_player_index &&
		tactics_hurt_by_team(target->unit_index, player))
	{
		bots_tactics.focus_picks++;
	}
	/* (the bot that goes after the killer sees it: where it is now; a
	teammate's sight reaches it only as bots_knowledge_known shares it) */
	if (target && player && player->team != NONE && bots_tactics.teams[player->team].trade_killer == target_player_index)
	{
		struct tactics_team *side = &bots_tactics.teams[player->team];

		if (side->trade_bot == bot_index)
			side->trade_point = target->position;
		side->trade_seen_shield = target->shield;
		side->trade_seen_tick = bots_tactics.now;
	}
	bot->target_player_index = target_player_index;
	bot->firing = firing;
	bot->done_tick = bots_tactics.now;
}

/* ---------- what the bots ask */

real bots_tactics_target_bonus(long bot_index, long target_player_index)
{
	struct tactics_team *side;
	struct tactics_player *player = tactics_bot_player(bot_index, &side);
	struct tactics_player *target;
	real bonus = 0.0f;

	if (!tactics_on(player))
		return 0.0f;
	target = tactics_player(target_player_index);
	if (!target || !target->alive)
		return 0.0f;
	if (side->trade_killer == target_player_index)
		bonus += 10.0f;
	if (tactics_hurt_by_team(target->unit_index, player))
		bonus += 6.0f;
	return bonus;
}

real bots_tactics_strafe_side(long bot_index, long target_player_index, real_point3d const *position,
	real_point3d const *target_position)
{
	struct tactics_player *player = tactics_bot_player(bot_index, NULL);
	real best_separation = TACTICS_CROSSFIRE_DEGREES * TACTICS_DEGREES;
	real side = 0.0f;
	long index;

	if (!tactics_on(player))
		return 0.0f;
	/* (a teammate close by: away from it) */
	for (index = 0; index < TACTICS_PLAYERS; index++)
	{
		struct tactics_player *mate = &bots_tactics.players[index];

		if (mate == player || mate->seen_tick != bots_tactics.now || !mate->alive || mate->team != player->team ||
			tactics_distance(&mate->position, position) >= TACTICS_SPACING)
		{
			continue;
		}
		bots_tactics.spacing_strafes++;
		return tactics_side_away(position, target_position, &mate->position);
	}
	/* (a bot of the team on the same enemy: round it away from that bot, the
	one nearest in angle) */
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct tactics_player *mate = index != bot_index ? tactics_bot_player(index, NULL) : NULL;
		struct tactics_bot *mate_bot = &bots_tactics.bots[index];
		real difference;

		if (!mate || mate->team != player->team || mate_bot->target_player_index != target_player_index ||
			bots_tactics.now - mate_bot->done_tick > 1)
		{
			continue;
		}
		difference = (real)fabs(tactics_angle_round(position, target_position, &mate->position));
		if (difference < best_separation)
		{
			best_separation = difference;
			side = tactics_crossfire_side(position, target_position, &mate->position, bot_index < index);
		}
	}
	if (side != 0.0f)
		bots_tactics.crossfire_strafes++;
	return side;
}

void bots_tactics_steer(long bot_index, real_point3d const *position, real *direction, real *throttle)
{
	struct tactics_player *player = tactics_bot_player(bot_index, NULL);
	struct tactics_player *nearest = NULL;
	real nearest_distance = TACTICS_SPACING;
	long index;

	if (!tactics_on(player))
		return;
	for (index = 0; index < TACTICS_PLAYERS; index++)
	{
		struct tactics_player *mate = &bots_tactics.players[index];
		real distance;

		if (mate == player || mate->seen_tick != bots_tactics.now || !mate->alive || mate->team != player->team ||
			(real)fabs(mate->position.z - position->z) > 1.5f)
		{
			continue;
		}
		distance = tactics_distance(&mate->position, position);
		if (distance < nearest_distance)
		{
			nearest_distance = distance;
			nearest = mate;
		}
	}
	if (!nearest || nearest_distance < 0.01f)
		return;
	*direction = tactics_steer_away(*direction, nearest->position.x - position->x, nearest->position.y - position->y,
		nearest_distance, throttle);
	bots_tactics.steer_ticks++;
}

/* a place of its own for the bot about a point the team gathers at, so that
its bots do not stand on one spot: that far off it, at the bot's own angle
(each bot's the last's turned by the golden angle) */
static void tactics_spread(long bot_index, real_point3d const *point, real distance, real_point3d *spread)
{
	real angle = (real)bot_index * 2.39996323f;

	spread->x = point->x + (real)cos(angle) * distance;
	spread->y = point->y + (real)sin(angle) * distance;
	spread->z = point->z;
}

/* the team's ask of the bot, looked at once a second */
static short tactics_decide(long bot_index, struct tactics_player *player, struct tactics_team *side,
	real_point3d *point)
{
	struct tactics_bot *bot = &bots_tactics.bots[bot_index];
	long now = bots_tactics.now, index;
	struct tactics_player *human, *nearest = NULL;
	real nearest_score = REAL_MAX;

	if (side->trade_killer != NONE && side->trade_bot == bot_index)
	{
		struct bots_known_enemy known[BOTS_KNOWLEDGE_MAXIMUM_KNOWN];
		short count = bots_knowledge_known(bot_index, known, BOTS_KNOWLEDGE_MAXIMUM_KNOWN), at;

		/* (where it knows the killer was, when it knows: its own sighting
		or its team's shared one) */
		for (at = 0; at < count; at++)
		{
			if (known[at].player_index == side->trade_killer && known[at].tick >= side->trade_start_tick)
				side->trade_point = known[at].position;
		}
		*point = side->trade_point;
		return _bots_tactic_trade;
	}
	if (now < side->fall_back_until_tick &&
		tactics_distance(&player->position, &side->last_death_point) < TACTICS_FALL_BACK_REACH &&
		tactics_distance(&player->position, &side->rally_point) > 5.0f)
	{
		tactics_spread(bot_index, &side->rally_point, TACTICS_SPACING * 1.5f, point);
		return _bots_tactic_fall_back;
	}
	if (now < side->pick_until_tick && tactics_distance(&player->position, &side->pick_point) < TACTICS_COLLAPSE_REACH &&
		tactics_distance(&player->position, &side->pick_point) > 3.0f)
	{
		tactics_spread(bot_index, &side->pick_point, TACTICS_SPACING * 1.5f, point);
		return _bots_tactic_collapse;
	}
	human = tactics_player(bot->escort_player_index);
	if (human && human->alive)
	{
		/* (near it: where it stands, looking about) */
		if (tactics_distance(&player->position, &human->position) > TACTICS_ESCORT_NEAR)
			tactics_spread(bot_index, &human->position, TACTICS_SPACING * 1.25f, point);
		else
			*point = player->position;
		return _bots_tactic_escort;
	}
	for (index = 0; index < TACTICS_PLAYERS; index++)
	{
		struct tactics_player *mate = &bots_tactics.players[index];
		real score;

		if (mate == player || mate->seen_tick != now || !mate->alive || mate->team != player->team)
			continue;
		score = tactics_distance(&mate->position, &player->position) * (mate->bot ? 1.0f : 0.5f);
		if (score < nearest_score)
		{
			nearest_score = score;
			nearest = mate;
		}
	}
	if (nearest)
	{
		real distance = tactics_distance(&nearest->position, &player->position);

		if (distance > TACTICS_SUPPORT_FAR)
			bot->supporting = TRUE;
		else if (distance < TACTICS_SUPPORT_NEAR)
			bot->supporting = FALSE;
	}
	else
		bot->supporting = FALSE;
	if (bot->supporting)
	{
		*point = nearest->position;
		return _bots_tactic_support;
	}
	return _bots_tactic_none;
}

short bots_tactics_goal(long bot_index, real_point3d const *position, real_point3d *point)
{
	struct tactics_team *side;
	struct tactics_player *player = tactics_bot_player(bot_index, &side);
	struct tactics_bot *bot;

	if (!tactics_on(player))
		return _bots_tactic_none;
	bot = &bots_tactics.bots[bot_index];
	if (bots_tactics.now >= bot->decide_tick)
	{
		short tactic;

		bot->decide_tick = bots_tactics.now + TICKS_PER_SECOND;
		tactic = tactics_decide(bot_index, player, side, &bot->point);
		if (tactic != _bots_tactic_none && tactic != bot->tactic)
			bots_tactics.tactic_counts[tactic]++;
		bot->tactic = tactic;
	}
	(void)position;
	*point = bot->point;
	return bot->tactic;
}

boolean bots_tactics_may_claim(long bot_index, long item_index)
{
	struct tactics_team *side;
	struct tactics_player *player = tactics_bot_player(bot_index, &side);
	long best;

	if (!tactics_on(player) || item_index < 0 || item_index >= BOTS_KNOWLEDGE_MAXIMUM_ITEMS)
		return TRUE;
	best = side->item_bots[item_index];
	if (best == NONE || best == bot_index || !tactics_bot_player(best, NULL))
		return TRUE;
	/* (the bot chosen for it has had four seconds and claimed nothing,
	bots.c asking only when no teammate goes for the item: the next may) */
	if (bots_tactics.now - side->item_bot_ticks[item_index] >= TICKS_PER_SECOND * 4)
		return TRUE;
	bots_tactics.items_deferred++;
	return FALSE;
}

void bots_tactics_report(char *line, long size)
{
	long pairs = bots_tactics.crossfire_pairs, samples = bots_tactics.spacing_samples;

	if (size <= 0)
		return;
	line[0] = 0;
	if (!game_engine_has_teams())
		return;
	snprintf(line, (size_t)size, ", tactics %s: team kills %ld/%ld, trades %ld of %ld, focused %ld, "
		"crossfire %ld pairs %.0f deg (%ld narrow), nearest mate %.1f (%ld%% close, %ld%% alone); "
		"trade %ld collapse %ld fall back %ld (%ld) escort %ld (%ld) support %ld, strafes %ld/%ld, steer %lds, "
		"items left %ld",
		!bots_tactics.on ? "off" : bots_tactics.only_team == 0 ? "on (red only)" :
			bots_tactics.only_team == 1 ? "on (blue only)" : "on", bots_tactics.kills[0], bots_tactics.kills[1], bots_tactics.trades,
		bots_tactics.trade_chances, bots_tactics.focus_picks, pairs,
		pairs ? (double)(bots_tactics.crossfire_degrees / pairs) : 0.0, bots_tactics.crossfire_narrow,
		samples ? (double)(bots_tactics.spacing_sum / samples) : 0.0,
		samples ? bots_tactics.spacing_close * 100 / samples : 0L, samples ? bots_tactics.spacing_alone * 100 / samples : 0L,
		bots_tactics.tactic_counts[_bots_tactic_trade], bots_tactics.tactic_counts[_bots_tactic_collapse],
		bots_tactics.tactic_counts[_bots_tactic_fall_back], bots_tactics.fall_backs,
		bots_tactics.tactic_counts[_bots_tactic_escort], bots_tactics.escorts,
		bots_tactics.tactic_counts[_bots_tactic_support], bots_tactics.crossfire_strafes, bots_tactics.spacing_strafes,
		bots_tactics.steer_ticks / TICKS_PER_SECOND, bots_tactics.items_deferred);
}
