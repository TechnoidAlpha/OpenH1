/*
COOP_ENEMIES.C

Network co-op's extra enemies: Server Setup's EXTRA ENEMIES
(network.coop_enemies_mode). PER PLAYER (network.coop_enemies, a
percentage): for each player past the first, each squad of the players'
enemies that a level places (encounters.c's encounter_create) gets that
much of its count more; at 100%, two players meet twice the squad, four
players four times it, but no squad grows past COOP_ENEMIES_MAXIMUM_GROWTH
times its size (at 100%, 64 players made squads 64 times as large: more
than the ground round them, the clients' objects or the host's ticks could
take). STATIC MULTIPLIER (network.coop_enemies_multiplier):
each squad is that many times as large, for any number of players.

Extra enemies are placed in widening rings around the squad's starting
locations, on a spot on the same floor, reachable without passing through
a wall, crate or machine, with room to stand and nobody already there: the
rings, and the room kept between two enemies, as wide as the enemy (a
Hunter's wider than a Grunt's). A starting location with no room left
passes its enemy to the squad's next one. If none has room, the enemy goes
to the nearest of the encounter's firing positions (the places its
designers marked for the AI to stand) near its starting location with
nobody there, and if there is none it is not placed: a squad gets fewer
enemies rather than enemies standing inside one another. The places the
encounter's squads start at are kept free for those placed after. An
enemy that starts in the air (a Sentinel, a Drone) goes on rings at its
height, and a swarm's (Infection forms) there too when the ground has no
room. Each faces a little away from its starting location's facing, by
an amount from its encounter, squad and number: none of this draws on the
game's random seed, so the level's random numbers stay as they were.

A script often places a squad of the next part of a level before its
structure is the one loaded (switch_bsp), when no spot there can be tested:
the extra enemies wait, and are placed as above once it is loaded (those
of a squad with nobody left are forgotten). They were stacked on the
squad's starting locations.

In network co-op with extra enemies, an AI's biped, on the host and on
the clients alike, begins its idle animation (a sleeping Grunt's sleep,
an Elite's watch) at a point of its own whenever it settles into it, so
that a squad and its extra enemies don't breathe and turn in step: the
point comes from the
unit's object index, which a client's copy shares, so every machine has
the same one. Without extra enemies (EXTRA ENEMIES off, or PER PLAYER
with one player) the host's game is the stock one; a client, which isn't
told the setting, varies its copies anyway, which changes only how they
look.

A squad a script loads into a dropship (vehicle_load_magic) can be larger
than the dropship's seats. The riders left without a seat are erased, and
kept: once the dropship's own riders get out (vehicle_unload, or any other
way), they are placed beside those riders, a few ticks apart, and drop and
fight as they do. A dropship gone first takes them with it.

Only the host runs the AI, so all but the idle animations is the host's.
*/

#include "cseries.h"
#include "ai/actor_definitions.h"
#include "ai/actor_placement.h"
#include "ai/actors.h"
#include "ai/ai_scenario_definitions.h"
#include "ai/encounters.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "models/model_animation_definitions.h"
#include "units/biped_definitions.h"
#include "units/units.h"
#include "units/unit_definitions.h"

#include "coop_enemies.h"
#include "network_coop.h"

#include <math.h>
#include <string.h>

/* port_config.c's */
long config_integer(char const *name);
char const *config_string(char const *name);

/* ---------- constants */

enum
{
	/* the actors a level keeps for its own placements, which the extra
	enemies never take: as many as the Xbox's whole pool */
	COOP_ENEMIES_LEVEL_ACTORS = 256,
	/* EXTRA ENEMIES' amounts, as Server Setup has them: 25 to 200% per
	player, or 2 to 32 times (none is its NONE) */
	COOP_ENEMIES_MINIMUM_PERCENT = 25,
	COOP_ENEMIES_MAXIMUM_PERCENT = 200,
	COOP_ENEMIES_MINIMUM_MULTIPLIER = 2,
	COOP_ENEMIES_MAXIMUM_MULTIPLIER = 32,
	/* the largest PER PLAYER makes a squad, in times its size */
	COOP_ENEMIES_MAXIMUM_GROWTH = 8,
	/* the rings tried around a starting location, the first holding
	SPREAD_PLACES_PER_RING places and each further one that many more */
	SPREAD_RINGS = 5,
	SPREAD_PLACES_PER_RING = 6,
	/* a squad's starting locations whose taken places are remembered while
	its extra enemies are placed */
	MAXIMUM_SPREAD_LOCATIONS = 32,
	/* who is kept clear of while a squad's extra enemies are placed: the
	bipeds near it, the starting locations of its encounter, the enemies
	placed */
	MAXIMUM_STANDERS = 512,
	/* extra enemies waiting for their squad's ground to be loaded, and how
	often it is looked for */
	MAXIMUM_WAITING_EXTRAS = 1024,
	WAITING_EXTRAS_TICKS = 15,
	MAXIMUM_RIDING_VEHICLES = 32,
	MAXIMUM_SEATED_RIDERS = 16,
	MAXIMUM_KEPT_RIDERS = 512,
	/* a kept rider placed every this many ticks, as the seated ones
	jumped out */
	RIDER_RELEASE_TICKS = 4,
};

/* (coop_enemies.h's count of the places on all the rings) */
typedef char coop_enemies_spread_spots_assert[
	COOP_ENEMIES_SPREAD_SPOTS == SPREAD_PLACES_PER_RING * SPREAD_RINGS * (SPREAD_RINGS + 1) / 2 ? 1 : -1];

/* the least distance between rings; a ring is further out for a wider
enemy, its two collision radii and SPREAD_MARGIN */
#define SPREAD_SPACING 0.8f
/* the room kept between two bipeds' collision cylinders */
#define SPREAD_MARGIN 0.25f
/* the size taken for an enemy whose unit is not a biped */
#define SPREAD_DEFAULT_RADIUS 0.3f
#define SPREAD_DEFAULT_HEIGHT 1.6f
/* height above the floor at which the way to a spot is checked */
#define SPREAD_STEP_HEIGHT 0.6f
/* how far a spot's floor may be above or below the starting location's:
a step, not a ledge */
#define SPREAD_FLOOR_DIFFERENCE 0.5f
/* two bipeds this far apart in height are on different floors */
#define SPREAD_STANDER_HEIGHT 1.5f
/* how far from a squad's starting locations the bipeds standing there are
gathered, beyond the rings and the firing positions it may use */
#define SPREAD_NEARBY 14.0f
/* the firing positions an extra enemy may go to: this near its starting
location, and on its floor */
#define FIRING_POSITION_REACH 10.0f
#define FIRING_POSITION_FLOOR_DIFFERENCE 1.0f
/* how far an extra enemy's facing may turn from its starting location's,
either way (20 degrees) */
#define FACING_VARIATION 0.35f
/* a vehicle's place beside a stander's: who stands in its bounding sphere */
#define SPREAD_SEARCH_RADIUS 1.0f
/* a kept rider's place beside a rider who got out, each a little apart */
#define RIDER_SPACING 0.5f

/* EXTRA ENEMIES' choices (network.coop_enemies_mode's values) */
enum
{
	_coop_enemies_none,
	_coop_enemies_per_player,
	_coop_enemies_multiplier,
};

/* ---------- structures */

/* a vehicle a script loaded riders into: its seated ones, and whether they
have begun to get out */
struct coop_riding_vehicle
{
	long vehicle_index;
	long seated_unit_indices[MAXIMUM_SEATED_RIDERS];
	short seat_indices[MAXIMUM_SEATED_RIDERS];
	short seated_count;
	boolean releasing;
	short released_count;
	long next_release_time;
};

/* an extra enemy whose squad's ground was not loaded as the squad was
placed, to place once it is */
struct coop_waiting_extra
{
	long encounter_index;
	short squad_index;
	short extra_number;
};

/* a rider left without a seat, to place once its vehicle's get out */
struct coop_kept_rider
{
	long vehicle_index;
	long variant_definition_index;
	long encounter_index;
	short squad_index;
};

/* ---------- globals */

static struct
{
	/* EXTRA ENEMIES, as the game began with it: its choice, and the amount
	of PER PLAYER (a percentage) and of STATIC MULTIPLIER */
	short mode;
	short percent;
	short multiplier;
	struct coop_riding_vehicle vehicles[MAXIMUM_RIDING_VEHICLES];
	short vehicle_count;
	struct coop_kept_rider riders[MAXIMUM_KEPT_RIDERS];
	short rider_count;
	struct coop_waiting_extra waiting[MAXIMUM_WAITING_EXTRAS];
	short waiting_count;
} coop_enemies;

/* a biped, or a place kept free, that an extra enemy is kept clear of */
struct coop_stander
{
	real_point3d position;
	real radius;
};

/* what is known while a squad's extra enemies are placed, in one tick
(encounter_create): who stands near, and the places found taken around
each starting location, which aren't tried again (placing enemies only
takes room) */
static struct
{
	long encounter_index;
	long time;
	short squad_index;
	/* the size the places were found taken for */
	real radius;
	unsigned long full;
	long taken[MAXIMUM_SPREAD_LOCATIONS][BIT_VECTOR_SIZE_IN_LONGS(COOP_ENEMIES_SPREAD_SPOTS)];
	short stander_count;
	struct coop_stander standers[MAXIMUM_STANDERS];
} coop_spread = { NONE, NONE, NONE };

/* (actors.c's) */
struct encounter_actor_iterator
{
	long encounter_index;
	long index;
	long next_index;
};

/* encounters.c's: one of a squad's extra enemies placed */
boolean encounter_place_extra_actor(long encounter_index, short squad_index, short extra_number);

/* ---------- private code */

static boolean coop_enemies_host(
	void)
{
	return game_connection() == _game_connection_network_server && network_coop_active();
}

/* whether this game has extra enemies (it is a co-op host's, with some
chosen) */
static boolean coop_enemies_on(
	void)
{
	return coop_enemies.mode != _coop_enemies_none && coop_enemies_host();
}

/* the players in the game, every machine's: the network game's (a level
places its first enemies before the players' units are made, as it loads:
game_initialize_for_new_map) */
static short coop_enemies_player_count(
	void)
{
	struct network_game *game = network_game_get_game();
	short count = 0;
	short index;

	for (index = 0; game && index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; index++)
	{
		if (network_player_is_valid(&game->players[index]))
			count++;
	}
	return count;
}

/* what a spot is tested against: the level, and the objects that block
the way (crates and other scenery, machines, vehicles) */
#define SPREAD_COLLISION_FLAGS (FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | \
	_collision_test_objects_sight_blocking_flags | FLAG(_collision_test_front_facing_surfaces_bit) | \
	FLAG(_collision_test_back_facing_surfaces_bit))

/* whether anything is in the way from `from`, along `vector` */
static boolean coop_enemies_blocked(
	real_point3d const *from,
	real_vector3d const *vector)
{
	struct collision_result collision;

	return collision_test_vector(SPREAD_COLLISION_FLAGS, from, vector, NONE, &collision);
}

/* the floor under a point, looked for from SPREAD_STEP_HEIGHT above it to
SPREAD_FLOOR_DIFFERENCE below it */
static boolean coop_enemies_floor(
	real_point3d const *point,
	real_point3d *floor)
{
	struct collision_result collision;
	real_point3d from = *point;
	real_vector3d down = { 0.0f, 0.0f, -(2.0f * SPREAD_STEP_HEIGHT + SPREAD_FLOOR_DIFFERENCE) };

	from.z += SPREAD_STEP_HEIGHT;
	if (!collision_test_vector(SPREAD_COLLISION_FLAGS, &from, &down, NONE, &collision))
		return FALSE;
	*floor = collision.point;
	return TRUE;
}

/* an enemy's size: its biped's collision radius and standing height */
struct coop_size
{
	real radius;
	real height;
};

static void coop_enemies_unit_size(
	long unit_definition_index,
	struct coop_size *size)
{
	size->radius = SPREAD_DEFAULT_RADIUS;
	size->height = SPREAD_DEFAULT_HEIGHT;
	if (unit_definition_index != NONE &&
		object_definition_get(unit_definition_index)->object.type == _object_type_biped)
	{
		struct biped_definition *definition = biped_definition_get(unit_definition_index);

		if (definition->biped.collision_radius > 0.0f)
			size->radius = definition->biped.collision_radius;
		if (definition->biped.collision_height_standing > 0.0f)
			size->height = definition->biped.collision_height_standing;
	}
}

/* whether one of coop_spread's standers is in the way of an enemy of
`radius` standing at `point` */
static boolean coop_enemies_stander_near(
	real_point3d const *point,
	real radius)
{
	short index;

	for (index = 0; index < coop_spread.stander_count; index++)
	{
		struct coop_stander const *stander = &coop_spread.standers[index];
		real dx = stander->position.x - point->x;
		real dy = stander->position.y - point->y;
		real clearance = stander->radius + radius + SPREAD_MARGIN;

		if (fabsf(stander->position.z - point->z) < SPREAD_STANDER_HEIGHT &&
			dx * dx + dy * dy < clearance * clearance)
		{
			return TRUE;
		}
	}
	return FALSE;
}

static void coop_enemies_stander_add(
	real_point3d const *point,
	real radius)
{
	if (coop_spread.stander_count < MAXIMUM_STANDERS)
	{
		coop_spread.standers[coop_spread.stander_count].position = *point;
		coop_spread.standers[coop_spread.stander_count].radius = radius;
		coop_spread.stander_count++;
	}
}

/* Whether a spot on the floor is out of the level, inside a vehicle, or
too near a biped: with `standers`, coop_spread's (the bipeds near the
squad and the enemies placed since, whether or not they are in the
level's clusters yet); else the bipeds there, within SPREAD_SEARCH_RADIUS */
static boolean coop_enemies_occupied(
	real_point3d const *floor,
	struct coop_size const *size,
	boolean standers)
{
	real_point3d body = { floor->x, floor->y, floor->z + SPREAD_STEP_HEIGHT };
	struct location location;
	long object_indices[32];
	short object_count;
	short index;

	if (standers && coop_enemies_stander_near(floor, size->radius))
		return TRUE;
	scenario_location_from_point(&location, &body);
	if (location.cluster_index == NONE)
		return TRUE;
	object_count = objects_in_sphere(0, (standers ? 0 : _object_mask_biped) | _object_mask_vehicle, &location,
		&body, SPREAD_SEARCH_RADIUS + size->radius, object_indices, NUMBEROF(object_indices));
	for (index = 0; index < object_count; index++)
	{
		struct object_datum *object = object_get(object_indices[index]);

		if (object->object.type == _object_type_vehicle)
		{
			if (point_in_sphere(&body, &object->object.bounding_sphere_center, object->object.bounding_sphere_radius))
				return TRUE;
		}
		else
		{
			real dx = object->object.position.x - floor->x;
			real dy = object->object.position.y - floor->y;
			real clearance = 2.0f * size->radius + SPREAD_MARGIN;

			if (dx * dx + dy * dy < clearance * clearance)
				return TRUE;
		}
	}
	return FALSE;
}

/* Whether an enemy can stand at `spot`: reachable from `from` (beside the
starting location; NULL: not looked at, a firing position the AI walks to)
with nothing in the way, a floor within `floor_difference` of
`floor_height`, room above it for the enemy, and nobody there. The floor
goes in `position`. */
static boolean coop_enemies_spot_free(
	real_point3d const *from,
	real_point3d const *spot,
	real floor_height,
	real floor_difference,
	struct coop_size const *size,
	boolean standers,
	real_point3d *position)
{
	real_vector3d up = { 0.0f, 0.0f, size->height };
	real_point3d floor;
	real_point3d feet;

	/* (level, at step height: down to the starting location's floor, a spot
	whose floor is a little higher would end the look inside it) */
	if (from)
	{
		real_vector3d way = { spot->x - from->x, spot->y - from->y, 0.0f };

		if (coop_enemies_blocked(from, &way))
			return FALSE;
	}
	if (!coop_enemies_floor(spot, &floor) || fabsf(floor.z - floor_height) > floor_difference)
		return FALSE;
	/* (headroom is checked from just above the floor, so the floor itself
	doesn't count as in the way) */
	feet = floor;
	feet.z += 0.1f;
	if (coop_enemies_blocked(&feet, &up) || coop_enemies_occupied(&floor, size, standers))
		return FALSE;
	*position = floor;
	return TRUE;
}

/* A spot for the `number`th enemy (counting from 1) on rings around
`origin`, with coop_enemies_spot_free: the rings as far apart as the enemy
is wide. FALSE if the level's floor isn't found under `origin` (its
structure not loaded, or `origin` is in the air), or there's no room.
`taken` (COOP_ENEMIES_SPREAD_SPOTS bits, or NULL) as coop_enemies.h's
coop_enemies_spread_position. */
static boolean coop_enemies_ring_position(
	real_point3d const *origin,
	short number,
	struct coop_size const *size,
	boolean standers,
	long *taken,
	real_point3d *position)
{
	real_point3d from = *origin;
	real_point3d floor;
	real spacing = MAX(SPREAD_SPACING, 2.0f * size->radius + SPREAD_MARGIN);
	short ring, first_spot = 0;

	if (!coop_enemies_floor(origin, &floor))
		return FALSE;
	from.z = floor.z + SPREAD_STEP_HEIGHT;
	for (ring = 0; ring < SPREAD_RINGS; ring++)
	{
		short place_count = (short)(SPREAD_PLACES_PER_RING * (ring + 1));
		real radius = spacing * (real)(ring + 1);
		short place;

		/* (each enemy starts at a different place on the ring, so a squad
		spreads all the way round rather than filling one side first) */
		for (place = 0; place < place_count; place++)
		{
			short around = (short)((number + place) % place_count);
			short spot_index = (short)(first_spot + around);
			real angle = (real)around * (_pi * 2.0f / (real)place_count);
			real_point3d spot = from;

			if (taken && BIT_VECTOR_TEST_FLAG(taken, spot_index))
				continue;
			spot.x += (real)cos(angle) * radius;
			spot.y += (real)sin(angle) * radius;
			spot.z = floor.z;
			if (coop_enemies_spot_free(&from, &spot, floor.z, SPREAD_FLOOR_DIFFERENCE, size, standers, position))
				return TRUE;
			if (taken)
				BIT_VECTOR_SET_FLAG(taken, spot_index, TRUE);
		}
		first_spot = (short)(first_spot + place_count);
	}
	return FALSE;
}

/* A spot for the `number`th enemy on rings around `origin` at its height,
for an enemy that starts in the air (a Sentinel, a Drone) or in a swarm
(Infection forms): the way to it open, in the level, and nobody near. No
floor is looked for. FALSE if there's no room. */
static boolean coop_enemies_air_position(
	real_point3d const *origin,
	short number,
	struct coop_size const *size,
	real_point3d *position)
{
	real spacing = MAX(SPREAD_SPACING, 2.0f * size->radius + SPREAD_MARGIN);
	short ring;

	for (ring = 0; ring < SPREAD_RINGS; ring++)
	{
		short place_count = (short)(SPREAD_PLACES_PER_RING * (ring + 1));
		real radius = spacing * (real)(ring + 1);
		short place;

		for (place = 0; place < place_count; place++)
		{
			short around = (short)((number + place) % place_count);
			real angle = (real)around * (_pi * 2.0f / (real)place_count);
			real_vector3d way = { (real)cos(angle) * radius, (real)sin(angle) * radius, 0.0f };
			real_point3d spot = { origin->x + way.i, origin->y + way.j, origin->z };
			struct location location;

			scenario_location_from_point(&location, &spot);
			if (location.cluster_index == NONE || coop_enemies_blocked(origin, &way) ||
				coop_enemies_stander_near(&spot, size->radius))
			{
				continue;
			}
			*position = spot;
			return TRUE;
		}
	}
	return FALSE;
}

/* The nearest of an encounter's firing positions to `origin`, within
FIRING_POSITION_REACH and on its floor, with nobody there (coop_spread's
standers), for an enemy of `size`; where the level's structure is loaded,
with its floor and room to stand (coop_enemies_spot_free, but not the way
from `origin`: the AI's own places, which it walks between). FALSE if none. */
static boolean coop_enemies_firing_position(
	struct encounter_definition const *encounter_definition,
	real_point3d const *origin,
	struct coop_size const *size,
	real_point3d *position)
{
	real_point3d floor;
	boolean loaded = coop_enemies_floor(origin, &floor);
	real best_distance = FIRING_POSITION_REACH * FIRING_POSITION_REACH;
	boolean found = FALSE;
	short index;

	for (index = 0; index < encounter_definition->firing_positions.count; index++)
	{
		struct firing_position_definition const *firing_position = TAG_BLOCK_GET_ELEMENT(
			&encounter_definition->firing_positions, index, struct firing_position_definition);
		real dx = firing_position->position.x - origin->x;
		real dy = firing_position->position.y - origin->y;
		real distance = dx * dx + dy * dy;
		real_point3d spot;

		if (distance >= best_distance ||
			fabsf(firing_position->position.z - origin->z) > FIRING_POSITION_FLOOR_DIFFERENCE ||
			coop_enemies_stander_near(&firing_position->position, size->radius))
		{
			continue;
		}
		spot = firing_position->position;
		if (loaded && !coop_enemies_spot_free(NULL, &firing_position->position, firing_position->position.z,
			SPREAD_FLOOR_DIFFERENCE, size, TRUE, &spot))
		{
			continue;
		}
		best_distance = distance;
		*position = spot;
		found = TRUE;
	}
	return found;
}

/* coop_spread begun for a squad's extra enemies: the places found taken
forgotten, and the standers gathered (the living bipeds near the squad's
starting locations, and every starting location of the encounter, kept
free for the squads placed after this one) */
static void coop_enemies_spread_begin(
	long encounter_index,
	short squad_index,
	struct encounter_definition const *encounter_definition,
	struct squad_definition const *squad_definition)
{
	real_rectangle3d bounds;
	struct object_iterator iterator;
	struct unit_datum *unit;
	short squad_number, index;

	coop_spread.encounter_index = encounter_index;
	coop_spread.squad_index = squad_index;
	coop_spread.time = game_time_get();
	coop_spread.radius = 0.0f;
	coop_spread.full = 0;
	coop_spread.stander_count = 0;
	csmemset(coop_spread.taken, 0, sizeof(coop_spread.taken));

	bounds.x0 = bounds.y0 = bounds.z0 = 1e30f;
	bounds.x1 = bounds.y1 = bounds.z1 = -1e30f;
	for (index = 0; index < squad_definition->starting_locations.count; index++)
	{
		real_point3d const *point = &TAG_BLOCK_GET_ELEMENT(&squad_definition->starting_locations, index,
			struct actor_starting_location)->position;

		bounds.x0 = MIN(bounds.x0, point->x - SPREAD_NEARBY);
		bounds.x1 = MAX(bounds.x1, point->x + SPREAD_NEARBY);
		bounds.y0 = MIN(bounds.y0, point->y - SPREAD_NEARBY);
		bounds.y1 = MAX(bounds.y1, point->y + SPREAD_NEARBY);
		bounds.z0 = MIN(bounds.z0, point->z - SPREAD_NEARBY);
		bounds.z1 = MAX(bounds.z1, point->z + SPREAD_NEARBY);
	}
	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((unit = (struct unit_datum *)object_iterator_next(&iterator)) != NULL)
	{
		real_point3d const *point = &unit->object.position;
		struct coop_size size;

		if (TEST_FLAG(unit->object.damage_flags, _object_dead_bit) || unit->object.parent_object_index != NONE ||
			point->x < bounds.x0 || point->x > bounds.x1 || point->y < bounds.y0 || point->y > bounds.y1 ||
			point->z < bounds.z0 || point->z > bounds.z1)
		{
			continue;
		}
		coop_enemies_unit_size(unit->definition_index, &size);
		coop_enemies_stander_add(point, size.radius);
	}
	for (squad_number = 0; squad_number < encounter_definition->squads.count; squad_number++)
	{
		struct squad_definition const *squad = TAG_BLOCK_GET_ELEMENT(&encounter_definition->squads, squad_number,
			struct squad_definition);

		for (index = 0; index < squad->starting_locations.count; index++)
		{
			coop_enemies_stander_add(&TAG_BLOCK_GET_ELEMENT(&squad->starting_locations, index,
				struct actor_starting_location)->position, SPREAD_DEFAULT_RADIUS);
		}
	}
}

/* the unit an actor variant of a starting location (its own, else its
squad's) makes */
static long coop_enemies_location_unit(
	struct squad_definition const *squad_definition,
	struct actor_starting_location const *location)
{
	struct scenario *scenario = global_scenario_get();
	short palette_index = location->actor_variant_index != NONE ? location->actor_variant_index :
		squad_definition->actor_palette_index;
	struct tag_reference *entry;

	if (!VALID_INDEX(palette_index, scenario->ai_actor_palette.count))
		return NONE;
	entry = TAG_BLOCK_GET_ELEMENT(&scenario->ai_actor_palette, palette_index, struct tag_reference);
	if (entry->index == NONE)
		return NONE;
	return actor_variant_definition_get(entry->index)->unit_reference.index;
}

/* whether a starting location's actor variant is a swarm's (Infection
forms) */
static boolean coop_enemies_location_swarm(
	struct squad_definition const *squad_definition,
	struct actor_starting_location const *location)
{
	struct scenario *scenario = global_scenario_get();
	short palette_index = location->actor_variant_index != NONE ? location->actor_variant_index :
		squad_definition->actor_palette_index;
	struct tag_reference *entry;
	long actor_definition_index;

	if (!VALID_INDEX(palette_index, scenario->ai_actor_palette.count))
		return FALSE;
	entry = TAG_BLOCK_GET_ELEMENT(&scenario->ai_actor_palette, palette_index, struct tag_reference);
	if (entry->index == NONE)
		return FALSE;
	actor_definition_index = actor_variant_definition_get(entry->index)->actor_reference.index;
	return actor_definition_index != NONE &&
		TEST_FLAG(actor_definition_get(actor_definition_index)->flags, _actor_definition_swarm_actor_bit);
}

/* whether this game's squads get extra enemies now: a co-op host's, with
EXTRA ENEMIES on and, for PER PLAYER, a second player */
static boolean coop_enemies_growing(
	void)
{
	return coop_enemies_on() &&
		(coop_enemies.mode == _coop_enemies_multiplier || coop_enemies_player_count() > 1);
}

/* how far an extra enemy's facing turns from its starting location's: from
its encounter, squad and number, not a draw of the game's seed, whose later
numbers (the AI's choices, grenades' spreads) it would move */
static real coop_enemies_facing_offset(
	long encounter_index,
	short squad_index,
	short extra_number)
{
	unsigned long hash = ((unsigned long)DATUM_INDEX_TO_ABSOLUTE_INDEX(encounter_index) * 73856093UL) ^
		((unsigned long)squad_index * 19349663UL) ^ ((unsigned long)extra_number * 83492791UL);

	hash *= 2654435761UL;
	hash ^= hash >> 15;
	return FACING_VARIATION * (2.0f * (real)(hash % 1024) / 1023.0f - 1.0f);
}

/* A unit's animation begun at a point of its own through it, if it is a
base animation of its graph that loops: the same point for the unit on
every machine (from its object's index, which a client's copy shares),
not a random one, since the clients don't share the host's seed. */
static void coop_enemies_vary_animation(
	long unit_index)
{
	struct unit_datum *unit = (struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	struct animation_graph *graph;
	struct animation *animation;
	unsigned long hash = (unsigned long)unit_index * 2654435761UL;

	if (!unit || unit->object.animation.animation_graph_index == NONE)
		return;
	graph = animation_graph_definition_get(unit->object.animation.animation_graph_index);
	if (!VALID_INDEX(unit->object.animation.state.index, graph->animations.count))
		return;
	animation = TAG_BLOCK_GET_ELEMENT(&graph->animations, unit->object.animation.state.index, struct animation);
	if (animation->type != _animation_base || animation->frame_count <= 1)
		return;
	hash ^= hash >> 15;
	unit->object.animation.state.frame_index = (short)(hash % (unsigned long)animation->frame_count);
}

static struct coop_riding_vehicle *coop_enemies_riding_vehicle(
	long vehicle_index,
	boolean create)
{
	short index;

	for (index = 0; index < coop_enemies.vehicle_count; index++)
	{
		if (coop_enemies.vehicles[index].vehicle_index == vehicle_index)
			return &coop_enemies.vehicles[index];
	}
	if (!create || coop_enemies.vehicle_count >= MAXIMUM_RIDING_VEHICLES)
		return NULL;
	csmemset(&coop_enemies.vehicles[coop_enemies.vehicle_count], 0, sizeof(coop_enemies.vehicles[0]));
	coop_enemies.vehicles[coop_enemies.vehicle_count].vehicle_index = vehicle_index;
	return &coop_enemies.vehicles[coop_enemies.vehicle_count++];
}

/* the riders kept for a vehicle forgotten, and the vehicle with them */
static void coop_enemies_forget_vehicle(
	short vehicle_number)
{
	long vehicle_index = coop_enemies.vehicles[vehicle_number].vehicle_index;
	short index, kept = 0;

	for (index = 0; index < coop_enemies.rider_count; index++)
	{
		if (coop_enemies.riders[index].vehicle_index != vehicle_index)
			coop_enemies.riders[kept++] = coop_enemies.riders[index];
	}
	coop_enemies.rider_count = kept;
	coop_enemies.vehicles[vehicle_number] = coop_enemies.vehicles[--coop_enemies.vehicle_count];
}

/* whether a vehicle's seated riders have begun to get out: one alive and
out of it (one killed in its seat is not getting out) */
static boolean coop_enemies_riders_getting_out(
	struct coop_riding_vehicle const *vehicle)
{
	short index;

	for (index = 0; index < vehicle->seated_count; index++)
	{
		struct object_datum *unit = object_try_and_get_and_verify_type(vehicle->seated_unit_indices[index],
			_object_mask_unit);

		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
			unit->object.parent_object_index != vehicle->vehicle_index)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* where a vehicle's `number`th kept rider is placed: beside one of the
riders who got out of it (where it is now: out of the vehicle, in the
open), a ring further about them each round; else at a seat's marker, else
the vehicle's place */
static void coop_enemies_rider_place(
	struct coop_riding_vehicle const *vehicle,
	short number,
	real_point3d *position,
	real *facing)
{
	struct object_datum *object = object_get(vehicle->vehicle_index);
	struct unit_definition *definition = unit_definition_get(object->definition_index);
	long out_unit_indices[MAXIMUM_SEATED_RIDERS];
	short out_count = 0;
	short index, round;
	real angle;
	real_point3d spread;
	struct coop_size size;

	*position = object->object.position;
	*facing = (real)atan2(object->object.forward.j, object->object.forward.i);
	for (index = 0; index < vehicle->seated_count; index++)
	{
		struct object_datum *unit = object_try_and_get_and_verify_type(vehicle->seated_unit_indices[index],
			_object_mask_unit);

		if (unit && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) &&
			unit->object.parent_object_index != vehicle->vehicle_index)
		{
			out_unit_indices[out_count++] = vehicle->seated_unit_indices[index];
		}
	}
	if (out_count > 0)
	{
		*position = object_get(out_unit_indices[number % out_count])->object.position;
		round = (short)(number / out_count);
	}
	else
	{
		struct object_marker marker;

		round = number;
		if (vehicle->seated_count > 0 && VALID_INDEX(vehicle->seat_indices[0], definition->unit.seats.count))
		{
			struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, vehicle->seat_indices[0],
				struct unit_seat);

			if (seat->marker_name[0] &&
				object_get_marker_by_name(vehicle->vehicle_index, seat->marker_name, &marker, 1) > 0)
			{
				*position = marker.matrix.position;
			}
		}
	}
	/* free ground around that spot, as the squad's own extra enemies get;
	failing that, a ring around it, further out each round */
	coop_enemies_unit_size(NONE, &size);
	if (coop_enemies_ring_position(position, (short)(number + 1), &size, FALSE, NULL, &spread))
	{
		*position = spread;
		return;
	}
	angle = (real)number * (_pi * 2.0f / SPREAD_PLACES_PER_RING);
	position->x += (real)cos(angle) * RIDER_SPACING * (real)(round / SPREAD_PLACES_PER_RING + 1);
	position->y += (real)sin(angle) * RIDER_SPACING * (real)(round / SPREAD_PLACES_PER_RING + 1);
}

/* whether a squad has an actor alive (one erased, or killed, has none to
add extra enemies to) */
static boolean coop_enemies_squad_alive(
	long encounter_index,
	short squad_index)
{
	struct encounter_actor_iterator iterator;
	struct actor_datum *actor;

	encounter_actor_iterator_new(&iterator, encounter_index);
	while ((actor = encounter_actor_iterator_next(&iterator)) != NULL)
	{
		if (actor->meta.squad_index == squad_index)
			return TRUE;
	}
	return FALSE;
}

/* the extra enemies waiting whose squad's ground is loaded now placed
(those of a squad with nobody left forgotten) */
static void coop_enemies_place_waiting(
	void)
{
	struct scenario *scenario = global_scenario_get();
	long placed_encounter_index = NONE;
	short index, kept = 0;

	for (index = 0; index < coop_enemies.waiting_count; index++)
	{
		struct coop_waiting_extra waiting = coop_enemies.waiting[index];
		struct encounter_definition *encounter_definition;
		struct squad_definition *squad_definition;
		struct actor_starting_location *starting_location;
		struct location location;
		short count;

		if (!datum_try_and_get(encounter_data, waiting.encounter_index) ||
			!VALID_INDEX(DATUM_INDEX_TO_ABSOLUTE_INDEX(waiting.encounter_index), scenario->ai_encounters.count))
		{
			continue;
		}
		encounter_definition = TAG_BLOCK_GET_ELEMENT(&scenario->ai_encounters,
			DATUM_INDEX_TO_ABSOLUTE_INDEX(waiting.encounter_index), struct encounter_definition);
		if (!VALID_INDEX(waiting.squad_index, encounter_definition->squads.count))
			continue;
		squad_definition = TAG_BLOCK_GET_ELEMENT(&encounter_definition->squads, waiting.squad_index,
			struct squad_definition);
		count = squad_definition->starting_locations.count;
		/* (the actors kept for the level's own placements are not taken) */
		if (count <= 0 || !coop_enemies_squad_alive(waiting.encounter_index, waiting.squad_index) ||
			actor_data->actual_count >= MAXIMUM_ACTORS - COOP_ENEMIES_LEVEL_ACTORS)
		{
			continue;
		}
		starting_location = TAG_BLOCK_GET_ELEMENT(&squad_definition->starting_locations,
			(waiting.extra_number - 1) % count, struct actor_starting_location);
		scenario_location_from_point(&location, &starting_location->position);
		if (location.cluster_index == NONE)
		{
			coop_enemies.waiting[kept++] = waiting;
			continue;
		}
		/* (placed as the squad's others were: not waiting again, its ground
		is there) */
		encounter_place_extra_actor(waiting.encounter_index, waiting.squad_index, waiting.extra_number);
		if (placed_encounter_index != NONE && placed_encounter_index != waiting.encounter_index)
			encounter_update_status(placed_encounter_index);
		placed_encounter_index = waiting.encounter_index;
	}
	coop_enemies.waiting_count = kept;
	if (placed_encounter_index != NONE)
		encounter_update_status(placed_encounter_index);
}

/* ---------- public code */

void coop_enemies_new_game(
	void)
{
	char const *mode = config_string("network.coop_enemies_mode");

	csmemset(&coop_enemies, 0, sizeof(coop_enemies));
	coop_spread.encounter_index = NONE;
	/* (an amount past Server Setup's, as it shows it: its nearest choice's
	end) */
	coop_enemies.percent = (short)PIN(config_integer("network.coop_enemies"), COOP_ENEMIES_MINIMUM_PERCENT,
		COOP_ENEMIES_MAXIMUM_PERCENT);
	coop_enemies.multiplier = (short)PIN(config_integer("network.coop_enemies_multiplier"),
		COOP_ENEMIES_MINIMUM_MULTIPLIER, COOP_ENEMIES_MAXIMUM_MULTIPLIER);
	coop_enemies.mode = !strcmp(mode, "per_player") ? _coop_enemies_per_player :
		!strcmp(mode, "multiplier") ? _coop_enemies_multiplier : _coop_enemies_none;
}

void *coop_enemies_port_demo_state(
	short part,
	long *size)
{
	switch (part)
	{
	case 0: *size = sizeof(coop_enemies); return &coop_enemies;
	case 1: *size = sizeof(coop_spread); return &coop_spread;
	}
	return NULL;
}

void coop_enemies_reset(
	void)
{
	coop_enemies.vehicle_count = 0;
	coop_enemies.rider_count = 0;
	coop_enemies.waiting_count = 0;
}

short coop_enemies_extra_count(
	long encounter_index,
	short count)
{
	struct encounter_datum *encounter;
	long extra, room;
	short players;

	if (count <= 0 || !coop_enemies_on())
		return 0;
	encounter = encounter_get(encounter_index);
	if (!game_team_is_enemy(_game_team_player, encounter->team_index))
		return 0;
	if (coop_enemies.mode == _coop_enemies_multiplier)
		extra = (long)count * (coop_enemies.multiplier - 1);
	else
	{
		players = coop_enemies_player_count();
		if (players <= 1)
			return 0;
		extra = ((long)count * coop_enemies.percent * (players - 1) + 50) / 100;
		extra = MIN(extra, (long)count * (COOP_ENEMIES_MAXIMUM_GROWTH - 1));
	}
	/* (the extra enemies waiting take room too) */
	room = MAXIMUM_ACTORS - COOP_ENEMIES_LEVEL_ACTORS - actor_data->actual_count - coop_enemies.waiting_count;
	return (short)PIN(extra, 0, MAX(room, 0));
}

boolean coop_enemies_extra_location(
	long encounter_index,
	short squad_index,
	short starting_location_index,
	short extra_number,
	struct actor_starting_location *location)
{
	struct encounter_definition *encounter_definition = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
		DATUM_INDEX_TO_ABSOLUTE_INDEX(encounter_index), struct encounter_definition);
	struct squad_definition *squad_definition = TAG_BLOCK_GET_ELEMENT(&encounter_definition->squads, squad_index,
		struct squad_definition);
	short count = squad_definition->starting_locations.count;
	struct actor_starting_location *starting_location;
	struct coop_size size;
	real_point3d floor;
	short tried;

	if (!VALID_INDEX(starting_location_index, count))
		return FALSE;
	if (extra_number == 1 || coop_spread.encounter_index != encounter_index ||
		coop_spread.squad_index != squad_index || coop_spread.time != game_time_get())
	{
		coop_enemies_spread_begin(encounter_index, squad_index, encounter_definition, squad_definition);
	}
	starting_location = TAG_BLOCK_GET_ELEMENT(&squad_definition->starting_locations, starting_location_index,
		struct actor_starting_location);
	coop_enemies_unit_size(coop_enemies_location_unit(squad_definition, starting_location), &size);
	/* (the places found taken were for an enemy of another size) */
	if (size.radius != coop_spread.radius)
	{
		coop_spread.radius = size.radius;
		coop_spread.full = 0;
		csmemset(coop_spread.taken, 0, sizeof(coop_spread.taken));
	}
	*location = *starting_location;
	/* (the level's structure there not loaded: none of its ground can be
	tested, the enemy is placed once it is) */
	{
		struct location point_location;

		scenario_location_from_point(&point_location, &starting_location->position);
		if (point_location.cluster_index == NONE)
		{
			if (coop_enemies.waiting_count < MAXIMUM_WAITING_EXTRAS)
			{
				struct coop_waiting_extra *waiting = &coop_enemies.waiting[coop_enemies.waiting_count++];

				waiting->encounter_index = encounter_index;
				waiting->squad_index = squad_index;
				waiting->extra_number = extra_number;
			}
			return FALSE;
		}
	}
	/* (one that starts in the air: on rings around it at its height) */
	if (!coop_enemies_floor(&starting_location->position, &floor))
	{
		if (!coop_enemies_air_position(&starting_location->position, extra_number, &size, &location->position) &&
			!coop_enemies_firing_position(encounter_definition, &starting_location->position, &size,
				&location->position))
		{
			return FALSE;
		}
		coop_enemies_stander_add(&location->position, size.radius);
		location->facing += coop_enemies_facing_offset(encounter_index, squad_index, extra_number);
		return TRUE;
	}
	/* (free ground around its starting location, else around the squad's
	others in turn) */
	for (tried = 0; tried < count; tried++)
	{
		short candidate_index = (short)((starting_location_index + tried) % count);
		struct actor_starting_location *candidate = TAG_BLOCK_GET_ELEMENT(&squad_definition->starting_locations,
			candidate_index, struct actor_starting_location);
		boolean tracked = candidate_index < MAXIMUM_SPREAD_LOCATIONS;

		if (tracked && TEST_FLAG(coop_spread.full, candidate_index))
			continue;
		if (coop_enemies_ring_position(&candidate->position, extra_number, &size, TRUE,
			tracked ? coop_spread.taken[candidate_index] : NULL, &location->position))
		{
			break;
		}
		if (tracked)
			SET_FLAG(coop_spread.full, candidate_index, TRUE);
	}
	/* (else the nearest free firing position; else, for a swarm, free room
	around it, whatever the ground; else none) */
	if (tried == count &&
		!coop_enemies_firing_position(encounter_definition, &starting_location->position, &size, &location->position) &&
		!(coop_enemies_location_swarm(squad_definition, starting_location) &&
			coop_enemies_air_position(&starting_location->position, extra_number, &size, &location->position)))
	{
		return FALSE;
	}
	coop_enemies_stander_add(&location->position, size.radius);
	location->facing += coop_enemies_facing_offset(encounter_index, squad_index, extra_number);
	return TRUE;
}

void coop_enemies_idle_started(
	long unit_index,
	short old_state)
{
	struct unit_datum *unit;

	/* (a turn on the spot goes back to the idle it left, a frame apart: not
	a new start) */
	if (old_state == _unit_state_idle || old_state == _unit_state_turn_left || old_state == _unit_state_turn_right ||
		!network_coop_active())
	{
		return;
	}
	/* (the host's game, the one that counts, as it was unless the squads
	grow; a client, which isn't told whether they do, varies its copies
	anyway: only how they look) */
	if (game_connection() == _game_connection_network_server && !coop_enemies_growing())
		return;
	unit = unit_get(unit_index);
	if (unit->object.type == _object_type_biped && unit->unit.player_index == NONE)
		coop_enemies_vary_animation(unit_index);
}

void coop_enemies_rider_seated(
	long vehicle_index,
	long unit_index)
{
	struct coop_riding_vehicle *vehicle;
	struct unit_datum *unit;

	if (!coop_enemies_on())
		return;
	vehicle = coop_enemies_riding_vehicle(vehicle_index, TRUE);
	unit = unit_get(unit_index);
	if (!vehicle)
		return;
	/* (a vehicle loaded again: the riders that got out of it before are not
	this load's) */
	{
		short index, kept = 0;

		for (index = 0; index < vehicle->seated_count; index++)
		{
			struct object_datum *seated = object_try_and_get_and_verify_type(vehicle->seated_unit_indices[index],
				_object_mask_unit);

			if (seated && seated->object.parent_object_index == vehicle_index)
			{
				vehicle->seated_unit_indices[kept] = vehicle->seated_unit_indices[index];
				vehicle->seat_indices[kept++] = vehicle->seat_indices[index];
			}
		}
		vehicle->seated_count = kept;
	}
	if (vehicle->seated_count >= MAXIMUM_SEATED_RIDERS)
		return;
	vehicle->seated_unit_indices[vehicle->seated_count] = unit_index;
	vehicle->seat_indices[vehicle->seated_count] = unit->unit.parent_seat_index;
	vehicle->seated_count++;
	/* (a vehicle loaded again, after its riders got out: they get out again) */
	vehicle->releasing = FALSE;
}

boolean coop_enemies_rider_unseated(
	long vehicle_index,
	long unit_index)
{
	struct unit_datum *unit;
	struct actor_datum *actor;
	struct coop_kept_rider *rider;
	struct coop_riding_vehicle *vehicle;

	if (!coop_enemies_on() || coop_enemies.rider_count >= MAXIMUM_KEPT_RIDERS)
		return FALSE;
	unit = unit_get(unit_index);
	vehicle = coop_enemies_riding_vehicle(vehicle_index, FALSE);
	/* (kept only for a vehicle with riders seated, whose getting out places
	it: one of a load that seated nobody is left where it is) */
	if (unit->object.type != _object_type_biped || unit->unit.actor_index == NONE || !vehicle ||
		vehicle->seated_count == 0)
	{
		return FALSE;
	}
	actor = actor_get(unit->unit.actor_index);
	if (actor->meta.swarm || actor->meta.encounter_index == NONE ||
		!game_team_is_enemy(_game_team_player, encounter_get(actor->meta.encounter_index)->team_index))
	{
		return FALSE;
	}
	rider = &coop_enemies.riders[coop_enemies.rider_count++];
	rider->vehicle_index = vehicle_index;
	rider->variant_definition_index = actor->meta.variant_definition_index;
	rider->encounter_index = actor->meta.encounter_index;
	rider->squad_index = actor->meta.squad_index;
	return TRUE;
}

void coop_enemies_update(
	void)
{
	long now = game_time_get();
	short vehicle_number;

	if (!coop_enemies_host())
		return;
	if (now % WAITING_EXTRAS_TICKS == 0)
		coop_enemies_place_waiting();
	for (vehicle_number = coop_enemies.vehicle_count - 1; vehicle_number >= 0; vehicle_number--)
	{
		struct coop_riding_vehicle *vehicle = &coop_enemies.vehicles[vehicle_number];
		struct object_datum *object = object_try_and_get_and_verify_type(vehicle->vehicle_index, _object_mask_unit);
		short index;

		/* (the vehicle's next kept rider) */
		for (index = 0; index < coop_enemies.rider_count; index++)
		{
			if (coop_enemies.riders[index].vehicle_index == vehicle->vehicle_index)
				break;
		}
		/* (a vehicle gone, or destroyed, takes its kept riders with it; one
		with none kept, every one loaded seated or all placed, is done with) */
		if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit) || index >= coop_enemies.rider_count)
		{
			coop_enemies_forget_vehicle(vehicle_number);
			continue;
		}
		if (!vehicle->releasing)
		{
			if (!coop_enemies_riders_getting_out(vehicle))
				continue;
			vehicle->releasing = TRUE;
			vehicle->released_count = 0;
			vehicle->next_release_time = now;
		}
		if (now < vehicle->next_release_time)
			continue;
		{
			struct coop_kept_rider rider = coop_enemies.riders[index];
			struct encounter_definition *encounter_definition;
			struct squad_definition *squad_definition;
			struct actor_starting_location location;

			coop_enemies.riders[index] = coop_enemies.riders[--coop_enemies.rider_count];
			encounter_definition = TAG_BLOCK_GET_ELEMENT(&global_scenario_get()->ai_encounters,
				DATUM_INDEX_TO_ABSOLUTE_INDEX(rider.encounter_index), struct encounter_definition);
			squad_definition = TAG_BLOCK_GET_ELEMENT(&encounter_definition->squads, rider.squad_index,
				struct squad_definition);
			/* (as the squad's: its states, no command list of a starting
			location's) */
			csmemset(&location, 0, sizeof(location));
			coop_enemies_rider_place(vehicle, vehicle->released_count++, &location.position, &location.facing);
			location.cluster_index = NONE;
			location.noncombat_sequence_id = NONE;
			location.default_state = squad_definition->default_state;
			location.initial_state = squad_definition->initial_state;
			location.actor_variant_index = NONE;
			location.command_list_index = NONE;
			actor_place(rider.variant_definition_index, rider.encounter_index, rider.squad_index, &location, FALSE, 0);
			encounter_update_status(rider.encounter_index);
		}
		vehicle->next_release_time = now + RIDER_RELEASE_TICKS;
	}
}
