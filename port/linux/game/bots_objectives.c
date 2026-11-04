/*
BOTS_OBJECTIVES.C

The bots in the objective games (bots_objectives.h). bots.c plays each bot;
this file reads the game's objective each tick and tells each bot what it
does for it, as decent players do:

- Capture the Flag: a team's bots split into attackers and defenders by
  the team's size (a third of its players defend, rounded: none of one, one
  of two to four, two of five to seven), the last bots of the team its
  defenders. Attackers go to the enemy's flag, take it and run it home,
  fighting less and running more (bots.c: through a fight, never firing the
  flag, which drops it, but striking with it); the attackers without it
  escort their carrier. Defenders keep about their flag. The team's flag
  taken, its bots hunt the enemy carrying it, but its carrier and the
  attackers nearer the enemy's flag (theirs taken too, all hunt); dropped,
  the defenders and every bot near it go and touch it to return it. A team
  with no flag of its own (one flag, or Assault) attacks with every bot;
  one with no enemy flag to take defends with every bot.
- Oddball: a ball lying free, every bot goes for the nearest; its holder
  runs from its enemies to places away from them and stays alive, striking
  with the ball those who close in; the others hunt an enemy holder, or
  escort a teammate holding one. Juggernaut: the juggernaut fights as in
  Slayer, and every other bot hunts the juggernaut.
- King of the Hill: every bot goes to the nearest hill the game marks
  (Multi-Hill's several, a moving hill as it moves), stays in it, moving
  about inside it, and fights from it; an enemy in the hill is a better
  target.

What the bots know of the objective is what the HUD shows a player: where
the flags, the balls and their carriers are, and the hills. The flags'
places are the scenario's flags, as the game's are (ctf_globals, Assault's
swapped). The minute line counts the bots' grabs, captures, returns, time
with a ball and on a hill, from the game's statistics of each player.
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/items.h"
#include "items/weapons.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"

#include "bots.h"
#include "bots_objectives.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* game_engine_oddball.c's: the player holds a ball (Juggernaut's: is the
juggernaut) */
boolean player_has_ball(long player_index);

/* ---------- constants */

#define OBJECTIVES_PLAYERS HALO_PORT_MAXIMUM_NETWORK_PLAYERS
#define OBJECTIVES_TEAMS 2
#define OBJECTIVES_BALLS 16
#define OBJECTIVES_HILLS 16
#define OBJECTIVES_HILL_POINTS 12
#define OBJECTIVES_GOALS 32

/* a bot this near the flag of its team lying out goes to return it, any
part; this near the enemy's lying out, it goes as urgently as an attacker */
#define OBJECTIVES_RETURN_REACH 30.0f
#define OBJECTIVES_TAKE_REACH 30.0f
/* through a fight within these of the object: a flag or ball to take, a
hill to get to */
#define OBJECTIVES_TAKE_PRESS 10.0f
#define OBJECTIVES_HILL_PRESS 15.0f
/* a defender's ground about its flag, an escort's distance behind its
carrier */
#define OBJECTIVES_GUARD_RADIUS 10.0f
#define OBJECTIVES_ESCORT_RADIUS 4.0f
/* a flag this near its place is at home */
#define OBJECTIVES_HOME_RADIUS 1.5f

enum
{
	/* game_engine_oddball.c's ball types: Juggernaut's */
	_objectives_ball_terminator = 2,
};

/* ---------- structures */

struct objectives_hill
{
	long hill_id;
	long point_count;
	/* its points about its centre, by their angle; the centre the box's
	(find_hill's) */
	real_point3d points[OBJECTIVES_HILL_POINTS];
	real_point3d center;
	real bottom;
	real top;
};

/* ---------- globals */

static struct
{
	long now;
	/* the game engine this tick (NONE: none the bots play the objective of) */
	long engine;
	boolean teams;
	boolean map_read;

	/* Capture the Flag: each team's flag (the one it defends: its owner
	team's), NONE for none in play; where it is, who carries it, its place
	(the game's ctf_globals.flags[team]: where it resets to, and where that
	team's carrier scores) */
	long flags[OBJECTIVES_TEAMS];
	real_point3d flag_positions[OBJECTIVES_TEAMS];
	long flag_carriers[OBJECTIVES_TEAMS];
	boolean flag_home_known[OBJECTIVES_TEAMS];
	real_point3d flag_homes[OBJECTIVES_TEAMS];
	boolean flag_at_home[OBJECTIVES_TEAMS];
	/* the bots of each team that defend (by bot number) */
	boolean defenders[BOTS_MAXIMUM];

	/* Oddball: the balls lying free and where; the players holding one
	(Juggernaut's juggernaut), by absolute index */
	long ball_count;
	long balls[OBJECTIVES_BALLS];
	real_point3d ball_positions[OBJECTIVES_BALLS];
	boolean holders[OBJECTIVES_PLAYERS];
	boolean juggernaut;

	/* King of the Hill: the map's hills, and those in play now (the game's
	goals) */
	long map_hill_count;
	struct objectives_hill map_hills[OBJECTIVES_HILLS];
	long hill_count;
	struct objectives_hill const *hills[OBJECTIVES_GOALS];
} bots_objectives;

/* ---------- helpers */

static real objectives_distance(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y, dz = a->z - b->z;

	return (real)sqrt(dx * dx + dy * dy + dz * dz);
}

static real objectives_distance2d(real_point3d const *a, real_point3d const *b)
{
	real dx = a->x - b->x, dy = a->y - b->y;

	return (real)sqrt(dx * dx + dy * dy);
}

/* the player (its datum) alive, its unit's position; NULL for none */
static struct player_datum *objectives_live_player(long player_index, real_point3d *position)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	if (!player || player->unit_index == NONE)
		return NULL;
	if (position)
		*position = object_get(player->unit_index)->object.position;
	return player;
}

static boolean objectives_enemies(struct player_datum const *player, struct player_datum const *other)
{
	if (player == other)
		return FALSE;
	return !bots_objectives.teams || player->team_index != other->team_index;
}

/* the point (x, y) is in the triangle */
static boolean objectives_in_triangle(real x, real y, real_point3d const *a, real_point3d const *b,
	real_point3d const *c)
{
	real d1 = (x - b->x) * (a->y - b->y) - (a->x - b->x) * (y - b->y);
	real d2 = (x - c->x) * (b->y - c->y) - (b->x - c->x) * (y - c->y);
	real d3 = (x - a->x) * (c->y - a->y) - (c->x - a->x) * (y - a->y);
	boolean negative = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
	boolean positive = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;

	return !(negative && positive);
}

static boolean objectives_hill_holds(struct objectives_hill const *hill, real_point3d const *point)
{
	long index;

	if (point->z < hill->bottom - 0.5f || point->z > hill->top + 1.0f)
		return FALSE;
	/* (the fan of triangles from its centre: its outline, its points
	taken round by their angles) */
	for (index = 0; index < hill->point_count; index++)
	{
		if (objectives_in_triangle(point->x, point->y, &hill->center, &hill->points[index],
			&hill->points[(index + 1) % hill->point_count]))
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------- the map */

/* the scenario's hills (each id's points, find_hill's way) and the flags'
places, once a map */
static void objectives_read_map(void)
{
	struct scenario *scenario = global_scenario_get();
	long index, team;

	bots_objectives.map_read = TRUE;
	bots_objectives.map_hill_count = 0;
	if (!scenario)
		return;
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
			struct scenario_netgame_flag);
		struct objectives_hill *hill = NULL;
		long hill_index;

		if (flag->type != _netgame_flag_hill)
			continue;
		for (hill_index = 0; hill_index < bots_objectives.map_hill_count; hill_index++)
		{
			if (bots_objectives.map_hills[hill_index].hill_id == flag->team_index)
				hill = &bots_objectives.map_hills[hill_index];
		}
		if (!hill)
		{
			if (bots_objectives.map_hill_count >= OBJECTIVES_HILLS)
				continue;
			hill = &bots_objectives.map_hills[bots_objectives.map_hill_count++];
			csmemset(hill, 0, sizeof(*hill));
			hill->hill_id = flag->team_index;
		}
		if (hill->point_count < OBJECTIVES_HILL_POINTS)
			hill->points[hill->point_count++] = flag->position;
	}
	for (index = 0; index < bots_objectives.map_hill_count; index++)
	{
		struct objectives_hill *hill = &bots_objectives.map_hills[index];
		real_point3d minimum, maximum;
		long point, other;

		/* (one point: a square two units across, as find_hill makes it) */
		if (hill->point_count == 1)
		{
			real_point3d center = hill->points[0];

			for (point = 0; point < 4; point++)
			{
				hill->points[point] = center;
				hill->points[point].x += point == 0 || point == 3 ? -1.0f : 1.0f;
				hill->points[point].y += point < 2 ? -1.0f : 1.0f;
			}
			hill->point_count = 4;
		}
		minimum = maximum = hill->points[0];
		for (point = 1; point < hill->point_count; point++)
		{
			minimum.x = MIN(minimum.x, hill->points[point].x);
			minimum.y = MIN(minimum.y, hill->points[point].y);
			minimum.z = MIN(minimum.z, hill->points[point].z);
			maximum.x = MAX(maximum.x, hill->points[point].x);
			maximum.y = MAX(maximum.y, hill->points[point].y);
			maximum.z = MAX(maximum.z, hill->points[point].z);
		}
		hill->center.x = 0.5f * (minimum.x + maximum.x);
		hill->center.y = 0.5f * (minimum.y + maximum.y);
		hill->center.z = 0.5f * (minimum.z + maximum.z);
		hill->bottom = minimum.z - 0.1f;
		hill->top = maximum.z + 0.8f;
		/* (round by their angles about the centre) */
		for (point = 1; point < hill->point_count; point++)
		{
			for (other = point; other > 0; other--)
			{
				real_point3d *a = &hill->points[other - 1], *b = &hill->points[other];

				if ((real)atan2(a->y - hill->center.y, a->x - hill->center.x) <=
					(real)atan2(b->y - hill->center.y, b->x - hill->center.x))
				{
					break;
				}
				{
					real_point3d swap = *a;

					*a = *b;
					*b = swap;
				}
			}
		}
	}
	/* (the flags' places: each team's own, Assault's the other's) */
	for (team = 0; team < OBJECTIVES_TEAMS; team++)
	{
		long slot = team;
		long flag_index = find_netgame_flag(NULL, 0.0f, 0.0f, _netgame_flag_ctf_flag, (short)team);

		if (game_engine_get_variant()->game_engine_variant.ctf.assault)
			slot = (team + 1) % OBJECTIVES_TEAMS;
		bots_objectives.flag_home_known[slot] = flag_index != NONE;
		if (flag_index != NONE)
		{
			bots_objectives.flag_homes[slot] = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flag_index,
				struct scenario_netgame_flag)->position;
		}
	}
}

/* ---------- the tick */

int bots_objectives_play(long game_engine_index)
{
	return game_engine_index == game_engine_ctf || game_engine_index == game_engine_oddball ||
		game_engine_index == game_engine_king;
}

void bots_objectives_reset(void)
{
	csmemset(&bots_objectives, 0, sizeof(bots_objectives));
	bots_objectives.engine = NONE;
}

/* the team's bots' parts: a third of the team's players defend (rounded),
its last bots */
static void objectives_choose_defenders(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long team;

	csmemset(bots_objectives.defenders, 0, sizeof(bots_objectives.defenders));
	for (team = 0; team < OBJECTIVES_TEAMS; team++)
	{
		long players = 0, bots = 0, defenders, bot_numbers[BOTS_MAXIMUM];

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->team_index != team || player->quit_out_of_game)
				continue;
			players++;
			if (bots_player_is_bot(iterator.datum_index) && bots < BOTS_MAXIMUM)
				bot_numbers[bots++] = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		}
		/* (no flag of its own: all attack; none to take: all defend) */
		if (bots_objectives.flags[team] == NONE)
			defenders = 0;
		else if (bots_objectives.flags[(team + 1) % OBJECTIVES_TEAMS] == NONE)
			defenders = bots;
		else
			defenders = MIN((players + 1) / 3, bots);
		while (defenders-- > 0)
		{
			long number = bot_numbers[bots - 1 - defenders];

			if (number >= 0 && number < BOTS_MAXIMUM)
				bots_objectives.defenders[number] = TRUE;
		}
	}
}

/* each player holding a flag (game_engine_ctf.c's ctf_find_flag_carrier) */
static long objectives_carrier(long weapon_index)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index != NONE && unit_has_weapon(player->unit_index, weapon_index))
			return iterator.datum_index;
	}
	return NONE;
}

void bots_objectives_tick_begin(void)
{
	struct game_variant *variant = game_engine_running() ? game_engine_get_variant() : NULL;
	struct object_iterator iterator;
	long team, index;

	bots_objectives.now = game_time_get();
	bots_objectives.engine = variant && bots_objectives_play(variant->game_engine_index) ?
		variant->game_engine_index : NONE;
	if (bots_objectives.engine == NONE)
		return;
	bots_objectives.teams = game_engine_has_teams();
	if (!bots_objectives.map_read)
		objectives_read_map();

	/* the flags and balls: the weapons that are the game's (weapon_is_flag),
	lying in the map or carried */
	for (team = 0; team < OBJECTIVES_TEAMS; team++)
	{
		bots_objectives.flags[team] = NONE;
		bots_objectives.flag_carriers[team] = NONE;
		bots_objectives.flag_at_home[team] = FALSE;
	}
	bots_objectives.ball_count = 0;
	object_iterator_new(&iterator, _object_mask_weapon, 0);
	while (object_iterator_next(&iterator))
	{
		struct item_datum *item = item_get(iterator.index);
		real_point3d position;
		long carrier;

		if (!weapon_is_flag(iterator.index))
			continue;
		item_get_position_even_if_in_inventory(iterator.index, &position);
		carrier = TEST_FLAG(item->item.flags, _item_attached_to_unit_bit) ? objectives_carrier(iterator.index) : NONE;
		if (bots_objectives.engine == game_engine_ctf)
		{
			team = item->object.owner_team_index;
			if (team < 0 || team >= OBJECTIVES_TEAMS)
				continue;
			bots_objectives.flags[team] = iterator.index;
			bots_objectives.flag_positions[team] = position;
			bots_objectives.flag_carriers[team] = carrier;
			bots_objectives.flag_at_home[team] = carrier == NONE && bots_objectives.flag_home_known[team] &&
				objectives_distance(&position, &bots_objectives.flag_homes[team]) < OBJECTIVES_HOME_RADIUS;
		}
		else if (bots_objectives.engine == game_engine_oddball && carrier == NONE &&
			item->object.parent_object_index == NONE &&
			TEST_FLAG(item->object.flags, _object_connected_to_map_bit) &&
			bots_objectives.ball_count < OBJECTIVES_BALLS)
		{
			bots_objectives.balls[bots_objectives.ball_count] = iterator.index;
			bots_objectives.ball_positions[bots_objectives.ball_count++] = position;
		}
	}
	if (bots_objectives.engine == game_engine_ctf)
		objectives_choose_defenders();

	/* the ball's holders (Juggernaut's juggernaut) */
	csmemset(bots_objectives.holders, 0, sizeof(bots_objectives.holders));
	bots_objectives.juggernaut = FALSE;
	if (bots_objectives.engine == game_engine_oddball)
	{
		struct data_iterator players;
		struct player_datum *player;

		bots_objectives.juggernaut =
			variant->game_engine_variant.oddball.oddball_ball_type == _objectives_ball_terminator;
		data_iterator_new(&players, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&players)) != NULL)
		{
			long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(players.datum_index);

			if (absolute_index >= 0 && absolute_index < OBJECTIVES_PLAYERS && player->unit_index != NONE &&
				player_has_ball(players.datum_index))
			{
				bots_objectives.holders[absolute_index] = TRUE;
			}
		}
	}

	/* the hills in play: the goals the game marks them by (their centres,
	a little above), each the map's hill of that centre */
	bots_objectives.hill_count = 0;
	if (bots_objectives.engine == game_engine_king)
	{
		short goal;

		for (goal = 0; goal < OBJECTIVES_GOALS; goal++)
		{
			real_point3d center;
			struct objectives_hill const *nearest = NULL;
			real nearest_distance = 2.0f;

			if (!game_engine_get_goal_in_use(goal))
				continue;
			game_engine_get_goal_position(&center, goal);
			for (index = 0; index < bots_objectives.map_hill_count; index++)
			{
				real distance = objectives_distance2d(&bots_objectives.map_hills[index].center, &center);

				if (distance < nearest_distance)
				{
					nearest_distance = distance;
					nearest = &bots_objectives.map_hills[index];
				}
			}
			if (nearest)
				bots_objectives.hills[bots_objectives.hill_count++] = nearest;
		}
	}
}

/* ---------- what a bot does */

static boolean objectives_ctf_goal(long bot_index, struct player_datum *player, long player_index,
	real_point3d const *position, struct bots_objective *objective)
{
	long team = player->team_index, enemy;
	long own_flag, enemy_flag;
	boolean defender = bot_index >= 0 && bot_index < BOTS_MAXIMUM && bots_objectives.defenders[bot_index];

	if (team < 0 || team >= OBJECTIVES_TEAMS)
		return FALSE;
	enemy = (team + 1) % OBJECTIVES_TEAMS;
	own_flag = bots_objectives.flags[team];
	enemy_flag = bots_objectives.flags[enemy];

	/* carrying the enemy's flag: home, through the fights */
	if (enemy_flag != NONE && bots_objectives.flag_carriers[enemy] == player_index)
	{
		if (!bots_objectives.flag_home_known[team])
			return FALSE;
		objective->kind = _bots_objective_carry;
		objective->point = bots_objectives.flag_homes[team];
		objective->object_index = enemy_flag;
		objective->urgent = TRUE;
		objective->press_on = TRUE;
		return TRUE;
	}
	/* its flag carried off: after the carrier, but an attacker nearer the
	enemy's flag lying out than the carrier, which it goes on to (a flag
	for a flag; with the enemy's flag carried, all after theirs) */
	if (own_flag != NONE && bots_objectives.flag_carriers[team] != NONE)
	{
		long carrier = bots_objectives.flag_carriers[team];

		if (objectives_live_player(carrier, &objective->point) &&
			(defender || enemy_flag == NONE || bots_objectives.flag_carriers[enemy] != NONE ||
				objectives_distance(position, &objective->point) <
					objectives_distance(position, &bots_objectives.flag_positions[enemy])))
		{
			objective->kind = _bots_objective_hunt;
			objective->player_index = carrier;
			objective->urgent = TRUE;
			return TRUE;
		}
	}
	/* its flag lying out: back to its place by a touch, the defenders and
	those near */
	if (own_flag != NONE && !bots_objectives.flag_at_home[team] && bots_objectives.flag_carriers[team] == NONE &&
		(defender || objectives_distance(position, &bots_objectives.flag_positions[team]) < OBJECTIVES_RETURN_REACH))
	{
		objective->kind = _bots_objective_take;
		objective->point = bots_objectives.flag_positions[team];
		objective->object_index = own_flag;
		objective->urgent = TRUE;
		objective->press_on = objectives_distance(position, &objective->point) < OBJECTIVES_TAKE_PRESS;
		return TRUE;
	}
	if (!defender && enemy_flag != NONE)
	{
		long carrier = bots_objectives.flag_carriers[enemy];

		/* the enemy's flag, to take */
		if (carrier == NONE)
		{
			real distance = objectives_distance(position, &bots_objectives.flag_positions[enemy]);

			objective->kind = _bots_objective_take;
			objective->point = bots_objectives.flag_positions[enemy];
			objective->object_index = enemy_flag;
			objective->urgent = distance < OBJECTIVES_TAKE_REACH;
			objective->press_on = distance < OBJECTIVES_TAKE_PRESS;
			return TRUE;
		}
		/* a teammate carrying it: with them */
		if (objectives_live_player(carrier, &objective->point))
		{
			objective->kind = _bots_objective_escort;
			objective->player_index = carrier;
			objective->radius = OBJECTIVES_ESCORT_RADIUS;
			return TRUE;
		}
	}
	/* defending: about its flag's place */
	if (own_flag != NONE && bots_objectives.flag_home_known[team])
	{
		objective->kind = _bots_objective_guard;
		objective->point = bots_objectives.flag_homes[team];
		objective->radius = OBJECTIVES_GUARD_RADIUS;
		return TRUE;
	}
	return FALSE;
}

static boolean objectives_oddball_goal(struct player_datum *player, long player_index, real_point3d const *position,
	struct bots_objective *objective)
{
	struct data_iterator iterator;
	struct player_datum *other;
	long index, nearest = NONE, enemy_holder = NONE, mate_holder = NONE;
	real nearest_distance = REAL_MAX, enemy_distance = REAL_MAX, mate_distance = REAL_MAX;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	boolean holding = absolute_index >= 0 && absolute_index < OBJECTIVES_PLAYERS &&
		bots_objectives.holders[absolute_index];

	/* holding the ball: away from the enemies (the juggernaut: as in
	Slayer) */
	if (holding)
	{
		if (bots_objectives.juggernaut)
			return FALSE;
		objective->kind = _bots_objective_evade;
		objective->point = *position;
		objective->urgent = TRUE;
		objective->press_on = TRUE;
		return TRUE;
	}
	/* the nearest ball lying free */
	for (index = 0; index < bots_objectives.ball_count; index++)
	{
		real distance = objectives_distance(position, &bots_objectives.ball_positions[index]);

		if (distance < nearest_distance)
		{
			nearest_distance = distance;
			nearest = index;
		}
	}
	if (nearest != NONE)
	{
		objective->kind = _bots_objective_take;
		objective->point = bots_objectives.ball_positions[nearest];
		objective->object_index = bots_objectives.balls[nearest];
		objective->urgent = TRUE;
		objective->press_on = nearest_distance < OBJECTIVES_TAKE_PRESS;
		return TRUE;
	}
	/* the nearest holder: an enemy, after them; a teammate, with them */
	data_iterator_new(&iterator, player_data);
	while ((other = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long other_absolute = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		real_point3d other_position;
		real distance;

		if (other == player || other_absolute < 0 || other_absolute >= OBJECTIVES_PLAYERS ||
			!bots_objectives.holders[other_absolute] || !objectives_live_player(iterator.datum_index, &other_position))
		{
			continue;
		}
		distance = objectives_distance(position, &other_position);
		if (objectives_enemies(player, other) && distance < enemy_distance)
		{
			enemy_distance = distance;
			enemy_holder = iterator.datum_index;
		}
		else if (!objectives_enemies(player, other) && distance < mate_distance)
		{
			mate_distance = distance;
			mate_holder = iterator.datum_index;
		}
	}
	if (enemy_holder != NONE)
	{
		objectives_live_player(enemy_holder, &objective->point);
		objective->kind = _bots_objective_hunt;
		objective->player_index = enemy_holder;
		objective->urgent = !bots_objectives.juggernaut;
		return TRUE;
	}
	if (mate_holder != NONE)
	{
		objectives_live_player(mate_holder, &objective->point);
		objective->kind = _bots_objective_escort;
		objective->player_index = mate_holder;
		objective->radius = OBJECTIVES_ESCORT_RADIUS * 1.5f;
		return TRUE;
	}
	return FALSE;
}

static boolean objectives_king_goal(real_point3d const *position, struct bots_objective *objective)
{
	struct objectives_hill const *nearest = NULL;
	real nearest_distance = REAL_MAX;
	long index;

	for (index = 0; index < bots_objectives.hill_count; index++)
	{
		real distance = objectives_distance(position, &bots_objectives.hills[index]->center);

		if (distance < nearest_distance)
		{
			nearest_distance = distance;
			nearest = bots_objectives.hills[index];
		}
	}
	if (!nearest)
		return FALSE;
	objective->kind = _bots_objective_hill;
	objective->point = nearest->center;
	objective->radius = 0.0f;
	/* (its reach in from the centre: the nearest of its points, a little in) */
	for (index = 0; index < nearest->point_count; index++)
	{
		real reach = objectives_distance2d(&nearest->center, &nearest->points[index]) * 0.6f;

		if (index == 0 || reach < objective->radius)
			objective->radius = reach;
	}
	objective->urgent = TRUE;
	objective->press_on = !objectives_hill_holds(nearest, position) && nearest_distance < OBJECTIVES_HILL_PRESS;
	return TRUE;
}

boolean bots_objectives_goal(long bot_index, long player_index, real_point3d const *position,
	struct bots_objective *objective)
{
	struct player_datum *player = player_try_and_get(player_index);

	csmemset(objective, 0, sizeof(*objective));
	objective->kind = _bots_objective_none;
	objective->object_index = NONE;
	objective->player_index = NONE;
	if (bots_objectives.engine == NONE || bots_objectives.now != game_time_get() || !player ||
		player->unit_index == NONE)
	{
		return FALSE;
	}
	switch (bots_objectives.engine)
	{
	case game_engine_ctf:
		return bots_objectives.teams && objectives_ctf_goal(bot_index, player, player_index, position, objective);
	case game_engine_oddball:
		return objectives_oddball_goal(player, player_index, position, objective);
	case game_engine_king:
		return objectives_king_goal(position, objective);
	}
	return FALSE;
}

real bots_objectives_target_bonus(long player_index, long target_player_index)
{
	struct player_datum *player = player_try_and_get(player_index);
	struct player_datum *target = player_try_and_get(target_player_index);
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(target_player_index);
	real_point3d position;
	long index;

	if (bots_objectives.engine == NONE || !player || !target || target->unit_index == NONE ||
		!objectives_enemies(player, target))
	{
		return 0.0f;
	}
	switch (bots_objectives.engine)
	{
	case game_engine_ctf:
		/* (the one carrying its team's flag) */
		if (player->team_index >= 0 && player->team_index < OBJECTIVES_TEAMS &&
			bots_objectives.flag_carriers[player->team_index] == target_player_index)
		{
			return 20.0f;
		}
		break;
	case game_engine_oddball:
		if (absolute_index >= 0 && absolute_index < OBJECTIVES_PLAYERS && bots_objectives.holders[absolute_index])
			return 15.0f;
		break;
	case game_engine_king:
		position = object_get(target->unit_index)->object.position;
		for (index = 0; index < bots_objectives.hill_count; index++)
		{
			if (objectives_hill_holds(bots_objectives.hills[index], &position))
				return 8.0f;
		}
		break;
	}
	return 0.0f;
}

boolean bots_objectives_carrying(long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct unit_datum *unit;
	long weapon_index;

	if (bots_objectives.engine == NONE || !player || player->unit_index == NONE)
		return FALSE;
	unit = unit_get(player->unit_index);
	if (unit->unit.current_weapon_index < 0 || unit->unit.current_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)
		return FALSE;
	weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
	return weapon_index != NONE && weapon_is_flag(weapon_index);
}

boolean bots_objectives_in_hill(real_point3d const *point)
{
	long index;

	for (index = 0; index < bots_objectives.hill_count; index++)
	{
		if (objectives_hill_holds(bots_objectives.hills[index], point))
			return TRUE;
	}
	return FALSE;
}

void bots_objectives_report(char *line, long size)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long grabs = 0, captures = 0, returns = 0, ball_ticks = 0, carrier_kills = 0, hill_ticks = 0;

	if (size <= 0)
		return;
	line[0] = 0;
	if (bots_objectives.engine == NONE)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		union multiplayer_statistics const *statistics = &player->statistics.multiplayer_statistics;

		if (!bots_player_is_bot(iterator.datum_index))
			continue;
		switch (bots_objectives.engine)
		{
		case game_engine_ctf:
			grabs += statistics->ctf_statistics.flag_grabs;
			captures += statistics->ctf_statistics.flag_scores;
			returns += statistics->ctf_statistics.flag_returns;
			break;
		case game_engine_oddball:
			ball_ticks += (unsigned short)statistics->oddball_statistics.time_with_the_ball;
			carrier_kills += statistics->oddball_statistics.ball_carrier_kills;
			break;
		case game_engine_king:
			hill_ticks += (unsigned short)statistics->king_statistics.time_on_hill;
			break;
		}
	}
	switch (bots_objectives.engine)
	{
	case game_engine_ctf:
		snprintf(line, (size_t)size, ", flags: grabs %ld, captures %ld, returns %ld", grabs, captures, returns);
		break;
	case game_engine_oddball:
		snprintf(line, (size_t)size, ", ball: seconds held %ld, carrier kills %ld", ball_ticks / TICKS_PER_SECOND,
			carrier_kills);
		break;
	case game_engine_king:
		snprintf(line, (size_t)size, ", hill: seconds %ld (%ld hills)", hill_ticks / TICKS_PER_SECOND,
			bots_objectives.hill_count);
		break;
	}
}
