/*
VEHICLE_HIJACK.C

Halo 2's boarding, in OpenH1's games (game.vehicle_hijack, off by default; a
Legacy game's machines never play it: network_standard_vehicle_hijack). A
player on foot beside a seat of an enemy's vehicle they could get into were
it empty (the reach of players.c's examination of a nearby vehicle and
units.c's unit_find_nearby_seat) is offered it: the action prompt reads
"Hold <action> to hijack" (hud.c), and the hold that swaps a weapon
(player_control.c's minimum_weapon_swap_ticks, so that a tap never does it)
begins it. The seats: a Ghost's or a Warthog's driver, a Warthog's gunner, a
Banshee's pilot on or near the ground; never the Scorpion's, a turret's or a
passenger's. An enemy is another team's (free for all: anyone else's; the
campaign: the Covenant's, never a marine or another player).

What follows is the game's own, in about a second (30 ticks a second at any
rate, VEHICLE_HIJACK_*_TICKS), and nothing new is drawn:

1. The vault: a low, quick arc (a quarter of a world unit over the line,
   VEHICLE_HIJACK_VAULT_TICKS) from where the jacker stood to beside the
   seat: its entrance (unit_get_seat_entrance_point, where its enter
   animation begins), moved out along the seat's way in until a body
   standing there is clear of the vehicle (its physics, which keeps a
   biped out, and its model), on the floor; over the vehicle's side, low,
   where the line crosses it. The jacker is set on the arc at each tick's end,
   moving along it between, airborne (the biped's own airborne pose), with
   its controls held, and lands there.
2. The blow, as they come down: the melee's action (the third person's
   overlay, the first person's melee of the weapon in hand), which deals
   no damage: no melee's timer is started.
3. The throw: the rider's seat passes no controls from the leap on
   (unit_update's, vehicles.c's sub-steps), then the rider is put out as the
   vehicle's own ejection puts a rider out when it flips (unit_exit_seat_end),
   stood clear of the vehicle at a spot the level has room for on the
   entrance's side (else where the ejection left them), drawn moving there
   from the seat, and flung up and away, airborne: the biped's flying pose,
   its landing. An AI is told it left the vehicle and fights on foot; a
   player's controls are held until they land, and their view jolts as a
   melee's hit jolts it (the jacker's weapon's melee damage's effect, only
   seen: player_effect_start).
4. The seat taken: unit_enter_seat, whose enter animation glides the jacker
   from where they are into it; another enemy aboard (a Warthog's gunner)
   stays. Their action button then does nothing until let go, so that the
   held button does not take them straight out.

It is the simulation, and deterministic: the units' states, the times, no
random number but those the game's own code draws (an airborne biped's
spin). It is called off before the throw (both on foot, the jacker landing
where the leap took them) if the vehicle dies, flips or goes, or the rider
or the jacker dies or leaves.

Where this machine decides the game (a local game, a host, a demo's
playback) it plays all of it; the host tells its OpenH1 clients
(network_hijack.c), each of which plays it for its own units, which are its
to move (its own player's leap and seat; its own player thrown), and the
blow for the others: the seats and the units' places reach it from the host
as every seat's does. The sequences are kept here, not in the game state:
one that a checkpoint's revert leaves behind finds its units elsewhere and is
let go.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "ai/ai.h"
#include "effects/player_effects.h"
#include "interface/first_person_weapons.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "physics/collisions.h"
#include "physics/physics.h"
#include "scenario/scenario.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "units/vehicle_definitions.h"
#include "bots_vehicles.h"
#include "infection.h"
#include "network_hijack.h"
#include "network_standard.h"
#include "vehicle_hijack.h"

/* network_game_globals.c's */
boolean network_game_distributed_client(void);
/* network_objects.c's */
void network_objects_set_seat(long unit_index, long vehicle_index, short seat_index);
/* bipeds.c's action (its enum) */
enum
{
	VEHICLE_HIJACK_MELEE_ACTION = 7,
};

/* ---------- constants */

enum
{
	MAXIMUM_VEHICLE_HIJACKS = 16,
	MAXIMUM_THROWN_RIDERS = 16,
	MAXIMUM_AWAITING_RELEASE = 16,
	MAXIMUM_TEST_HOLDS = 4,
	/* the thrown rider drawn moving from the seat to where they stand, as
	unit_enter_seat draws a unit moving into one */
	VEHICLE_HIJACK_INTERPOLATION_TICKS = 6,
	/* a thrown rider's landing seen no sooner than this */
	VEHICLE_HIJACK_THROWN_MINIMUM_TICKS = 3,
	/* the throw's ways tried: out on the entrance's side, swung back
	along the vehicle, then the other side */
	VEHICLE_HIJACK_THROW_WAYS = 7,
};

/* the leap's height over the straight line to the entrance (world units)
and its least (a step up) */
#define VEHICLE_HIJACK_VAULT_HEIGHT 0.25f
/* ... and over a vehicle in the way (its side, from its front round) */
#define VEHICLE_HIJACK_VAULT_CLEARING_HEIGHT 0.5f
/* a body (its half width and height, tested at that many levels) kept clear
of the vehicle at the landing, looked for out from the seat's entrance a
step at a time, so far at most; how far down a floor is looked for (world
units) */
#define VEHICLE_HIJACK_BODY_RADIUS 0.25f
#define VEHICLE_HIJACK_BODY_HEIGHT 0.7f
enum { VEHICLE_HIJACK_BODY_LEVELS = 4 };
#define VEHICLE_HIJACK_LANDING_STEP 0.1f
#define VEHICLE_HIJACK_LANDING_FARTHEST 1.2f
#define VEHICLE_HIJACK_FLOOR_PROBE 3.0f
/* a biped's origin over the floor it stands on, where none is measured */
#define VEHICLE_HIJACK_STANDING 0.45f
/* the reach of a seat, as unit_find_nearby_seat's */
#define VEHICLE_HIJACK_REACH 1.0f
/* the throw: out and up (world units a tick: 2.1 and 1.05 a second), a
knock off to the side: with the level's gravity (global_gravity, 0.0036 a
tick) up about a fifth of a unit, down again some two thirds of a second
on, about a unit and a half out; and how far past the vehicle's sphere the
rider stands */
#define VEHICLE_HIJACK_THROW_OUT 0.07f
#define VEHICLE_HIJACK_THROW_UP 0.035f
#define VEHICLE_HIJACK_THROW_MARGIN 0.15f
/* a biped's size when its definition has none */
#define VEHICLE_HIJACK_RIDER_RADIUS 0.3f
/* the jolt's scale of a melee's hit */
#define VEHICLE_HIJACK_JOLT_SCALE 0.75f
/* how far down a flyer looks for the level */
#define VEHICLE_HIJACK_GROUND_PROBE 4.0f

/* what the throw's room is tested against: the level, and the objects that
block the way */
#define VEHICLE_HIJACK_ROOM_FLAGS (FLAG(_collision_test_structure_bit) | FLAG(_collision_test_objects_bit) | \
	_collision_test_objects_sight_blocking_flags | FLAG(_collision_test_front_facing_surfaces_bit) | \
	FLAG(_collision_test_back_facing_surfaces_bit))

/* ---------- structures */

struct vehicle_hijack_sequence
{
	long jacker_unit_index;
	long rider_unit_index;
	long vehicle_index;
	short seat_index;
	/* this machine decides it (else the host's word) */
	boolean authority;
	boolean struck;
	long start_time;
	/* the vault (where the jacker is this machine's to move): from where they
	stood to beside the seat, and its arc's height */
	boolean vaulting;
	real_point3d vault_start;
	real_point3d vault_landing;
	real vault_height;
};

struct vehicle_hijack_thrown
{
	long unit_index;
	long start_time;
};

struct vehicle_hijack_test_hold
{
	long vehicle_index;
	long until;
};

/* ---------- globals */

static struct
{
	struct vehicle_hijack_sequence sequences[MAXIMUM_VEHICLE_HIJACKS];
	struct vehicle_hijack_thrown thrown[MAXIMUM_THROWN_RIDERS];
	/* the players whose action button waits to be let go (datum indices) */
	long awaiting_release[MAXIMUM_AWAITING_RELEASE];
	struct vehicle_hijack_test_hold test_holds[MAXIMUM_TEST_HOLDS];
	boolean enemies_aboard_allowed;
	/* (its NONEs set: vehicle_hijack_reset) */
	boolean ready;
} vehicle_hijack_globals;

/* ---------- prototypes */

static void vehicle_hijack_ready(void);

/* ---------- the decision */

boolean vehicle_hijack_seat_allowed(
	struct vehicle_hijack_seat_facts const *facts)
{
	if (!facts->rule || facts->opence || !facts->enemy || !facts->rider_alive || !facts->upright)
		return FALSE;
	if (facts->speed > VEHICLE_HIJACK_MAXIMUM_SPEED)
		return FALSE;
	switch (facts->kind)
	{
	case _vehicle_hijack_kind_jeep:
		return facts->part == _vehicle_hijack_seat_driver || facts->part == _vehicle_hijack_seat_gunner;
	case _vehicle_hijack_kind_scout:
		return facts->part == _vehicle_hijack_seat_driver;
	case _vehicle_hijack_kind_flyer:
		return facts->part == _vehicle_hijack_seat_driver &&
			facts->ground_clearance <= VEHICLE_HIJACK_FLYER_GROUND_CLEARANCE;
	default:
		/* (the Scorpion, a turret, a boat, a plane) */
		return FALSE;
	}
}

short vehicle_hijack_vehicle_kind(
	long vehicle_index)
{
	switch (bots_vehicle_kind(vehicle_index))
	{
	case _bots_vehicle_jeep: return _vehicle_hijack_kind_jeep;
	case _bots_vehicle_scout: return _vehicle_hijack_kind_scout;
	case _bots_vehicle_tank: return _vehicle_hijack_kind_tank;
	case _bots_vehicle_flyer: return _vehicle_hijack_kind_flyer;
	case _bots_vehicle_none: return _vehicle_hijack_kind_none;
	default: return _vehicle_hijack_kind_other;
	}
}

/* ---------- private code */

static short vehicle_hijack_seat_part(
	long vehicle_index,
	short seat_index)
{
	if (unit_seat_is_driver(vehicle_index, seat_index))
		return _vehicle_hijack_seat_driver;
	if (unit_seat_is_gunner(vehicle_index, seat_index))
		return _vehicle_hijack_seat_gunner;
	return _vehicle_hijack_seat_passenger;
}

/* upright, as players.c's examination of a vehicle has it (not one to
flip) */
static boolean vehicle_hijack_upright(
	long vehicle_index)
{
	struct game_globals_player_control *player_control = TAG_BLOCK_GET_ELEMENT(
		&scenario_get_game_globals()->player_control, 0, struct game_globals_player_control);
	real flipping_angle = (_pi * 0.5f) - player_control->minimum_vehicle_flipping_angle;

	return object_get(vehicle_index)->object.up.k > (real)cos(flipping_angle);
}

/* a flyer's way down from its origin to the level (REAL_MAX: none in reach) */
static real vehicle_hijack_ground_clearance(
	long vehicle_index)
{
	struct collision_result collision;
	real_point3d from = object_get(vehicle_index)->object.position;
	real_vector3d down = { 0.0f, 0.0f, -VEHICLE_HIJACK_GROUND_PROBE };

	if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit), &from, &down, vehicle_index,
		&collision))
	{
		return REAL_MAX;
	}
	return from.z - collision.point.z;
}

static struct vehicle_hijack_sequence *vehicle_hijack_sequence_of_jacker(
	long unit_index)
{
	short index;

	for (index = 0; index < MAXIMUM_VEHICLE_HIJACKS; index++)
	{
		if (vehicle_hijack_globals.sequences[index].jacker_unit_index == unit_index && unit_index != NONE)
			return &vehicle_hijack_globals.sequences[index];
	}
	return NULL;
}

/* whether a vehicle, or a unit as jacker or rider, is in a hijack now */
static boolean vehicle_hijack_involved(
	long object_index)
{
	short index;

	for (index = 0; index < MAXIMUM_VEHICLE_HIJACKS; index++)
	{
		struct vehicle_hijack_sequence const *sequence = &vehicle_hijack_globals.sequences[index];

		if (sequence->jacker_unit_index != NONE && (sequence->jacker_unit_index == object_index ||
			sequence->rider_unit_index == object_index || sequence->vehicle_index == object_index))
		{
			return TRUE;
		}
	}
	return FALSE;
}

static boolean vehicle_hijack_player_is_opence(
	long player_index)
{
	return player_index != NONE && network_standard_player_is_opence(player_index);
}

/* the unit, on foot or in a seat, if it is one and alive */
static struct unit_datum *vehicle_hijack_living_unit(
	long unit_index)
{
	struct unit_datum *unit = unit_index != NONE ?
		(struct unit_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_unit) : NULL;

	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return NULL;
	return unit;
}

/* where a unit reaches a seat from, and how far it is (as
unit_find_nearby_seat measures it); FALSE if it cannot get into it */
static boolean vehicle_hijack_reach(
	long unit_index,
	long vehicle_index,
	short seat_index,
	real_point3d *entrance,
	real *distance)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
		&unit_definition_get(object_get(vehicle_index)->definition_index)->unit.seats, seat_index, struct unit_seat);
	real_point3d seat_position;
	real entrance_distance, seat_distance;

	if (!seat->label[0] || !unit_has_animation_to_enter_seat(unit_index, vehicle_index, seat_index) ||
		!unit_get_seat_entrance_point(unit_index, vehicle_index, seat_index, entrance, &seat_position, NULL))
	{
		return FALSE;
	}
	entrance_distance = distance3d(entrance, &unit->object.bounding_sphere_center);
	seat_distance = distance3d(&seat_position, &unit->object.bounding_sphere_center);
	*distance = MIN(entrance_distance, seat_distance);
	return TRUE;
}

/* the rider in a seat, NONE for none */
static long vehicle_hijack_rider(
	long vehicle_index,
	short seat_index)
{
	long child_index = object_get(vehicle_index)->object.first_child_object_index;

	while (child_index != NONE)
	{
		struct object_datum *child = object_get(child_index);

		if (TEST_FLAG(_object_mask_unit, child->object.type) &&
			unit_get(child_index)->unit.parent_seat_index == seat_index)
		{
			return child_index;
		}
		child_index = child->object.next_object_index;
	}
	return NONE;
}

static boolean vehicle_hijack_seat_offered_to_unit(
	long unit_index,
	long vehicle_index,
	short seat_index,
	long rider_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct unit_datum *rider = unit_get(rider_index);
	struct vehicle_hijack_seat_facts facts;

	facts.rule = network_standard_vehicle_hijack();
	facts.enemy = game_team_is_enemy(unit->object.owner_team_index, rider->object.owner_team_index);
	facts.rider_alive = !TEST_FLAG(rider->object.damage_flags, _object_dead_bit) &&
		rider->object.type == _object_type_biped;
	facts.opence = vehicle_hijack_player_is_opence(unit->unit.player_index) ||
		vehicle_hijack_player_is_opence(rider->unit.player_index);
	facts.kind = vehicle_hijack_vehicle_kind(vehicle_index);
	facts.part = vehicle_hijack_seat_part(vehicle_index, seat_index);
	facts.upright = vehicle_hijack_upright(vehicle_index);
	facts.speed = magnitude3d(&object_get(vehicle_index)->object.translational_velocity);
	facts.ground_clearance = facts.kind == _vehicle_hijack_kind_flyer ?
		vehicle_hijack_ground_clearance(vehicle_index) : 0.0f;
	return vehicle_hijack_seat_allowed(&facts);
}

/* ---------- the offer */

short vehicle_hijack_seat_offered(
	long player_index,
	long vehicle_index)
{
	struct player_datum *player = player_get(player_index);
	struct unit_datum *unit;
	struct unit_datum *vehicle;
	struct unit_definition *vehicle_definition;
	short best_seat_index = NONE;
	real best_distance = REAL_MAX;
	long child_index;

	/* (cheap first: the rule, then the jacker, then the vehicle) */
	vehicle_hijack_ready();
	if (!network_standard_vehicle_hijack() || player->unit_index == NONE)
		return NONE;
	unit = vehicle_hijack_living_unit(player->unit_index);
	vehicle = vehicle_hijack_living_unit(vehicle_index);
	if (!unit || !vehicle || unit->object.type != _object_type_biped || unit->object.parent_object_index != NONE ||
		vehicle->object.type != _object_type_vehicle ||
		TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit) ||
		infection_unit_is_zombie(player->unit_index) || vehicle_hijack_involved(player->unit_index) ||
		vehicle_hijack_involved(vehicle_index))
	{
		return NONE;
	}
	vehicle_definition = unit_definition_get(vehicle->definition_index);
	for (child_index = vehicle->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		struct unit_datum *rider;
		short seat_index;
		real_point3d entrance;
		real distance;

		if (!TEST_FLAG(_object_mask_unit, object_get(child_index)->object.type))
			continue;
		rider = unit_get(child_index);
		seat_index = rider->unit.parent_seat_index;
		if (seat_index < 0 || seat_index >= vehicle_definition->unit.seats.count)
			continue;
		if (!vehicle_hijack_seat_offered_to_unit(player->unit_index, vehicle_index, seat_index, child_index) ||
			!vehicle_hijack_reach(player->unit_index, vehicle_index, seat_index, &entrance, &distance) ||
			distance >= VEHICLE_HIJACK_REACH)
		{
			continue;
		}
		if (distance < best_distance)
		{
			best_distance = distance;
			best_seat_index = seat_index;
		}
	}
	return best_seat_index;
}

/* ---------- the sequence */

/* the floor under a point, looked for from a little above it to
VEHICLE_HIJACK_FLOOR_PROBE below, the vehicle's own surfaces passed through:
FALSE if there is none in reach */
static boolean vehicle_hijack_floor(
	real_point3d const *point,
	long vehicle_index,
	real *floor)
{
	struct collision_result collision;
	real_point3d from = *point;
	real_vector3d down = { 0.0f, 0.0f, -VEHICLE_HIJACK_FLOOR_PROBE };

	from.z += 0.5f;
	if (!collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit), &from, &down, vehicle_index,
		&collision))
	{
		return FALSE;
	}
	*floor = collision.point.z;
	return TRUE;
}

/* whether a body standing at a point (its origin), on a floor, is clear of
the vehicle: points round it at a body's half width and in it, from the
floor to a body's height, none inside the vehicle's physics (which
physics.c keeps a biped out of) or its model */
static boolean vehicle_hijack_body_clear(
	real_point3d const *origin,
	real floor,
	long vehicle_index,
	long ignore_index)
{
	/* (the compass's eight ways and the middle: cosines and sines) */
	static real const ways[9][2] =
	{
		{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.7071068f, 0.7071068f }, { 0.0f, 1.0f }, { -0.7071068f, 0.7071068f },
		{ -1.0f, 0.0f }, { -0.7071068f, -0.7071068f }, { 0.0f, -1.0f }, { 0.7071068f, -0.7071068f },
	};
	unsigned long const flags = FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_vehicles_bit);
	real bottom = MIN(floor, origin->z) + 0.1f;
	short way, level;

	for (level = 0; level < VEHICLE_HIJACK_BODY_LEVELS; level++)
	{
		for (way = 0; way < 9; way++)
		{
			real_point3d point;

			point.x = origin->x + ways[way][0] * VEHICLE_HIJACK_BODY_RADIUS;
			point.y = origin->y + ways[way][1] * VEHICLE_HIJACK_BODY_RADIUS;
			point.z = bottom + VEHICLE_HIJACK_BODY_HEIGHT * (real)level / (real)(VEHICLE_HIJACK_BODY_LEVELS - 1);
			if (collision_test_point(flags | FLAG(_collision_test_use_vehicle_physics_bit), &point, ignore_index) ||
				collision_test_point(flags, &point, ignore_index))
			{
				return FALSE;
			}
		}
	}
	(void)vehicle_index;
	return TRUE;
}

/* the vault's arc at a tick of it (0 its start, VEHICLE_HIJACK_VAULT_TICKS
its landing): along the line from where the jacker stood to the landing,
raised by a parabola of the arc's height */
static void vehicle_hijack_vault_point(
	struct vehicle_hijack_sequence const *sequence,
	long tick,
	real_point3d *point)
{
	real u = (real)PIN(tick, 0, VEHICLE_HIJACK_VAULT_TICKS) / (real)VEHICLE_HIJACK_VAULT_TICKS;
	real smooth = u * u * (3.0f - 2.0f * u);

	/* (across eased, so the take-off and the landing are soft; up as a
	thrown body's arc) */
	point->x = sequence->vault_start.x + (sequence->vault_landing.x - sequence->vault_start.x) * smooth;
	point->y = sequence->vault_start.y + (sequence->vault_landing.y - sequence->vault_start.y) * smooth;
	point->z = sequence->vault_start.z + (sequence->vault_landing.z - sequence->vault_start.z) * u +
		4.0f * sequence->vault_height * u * (1.0f - u);
}

/* the jacker where the vault has them at a tick of it, moving on to the
next: an airborne biped, its velocity the arc's, so that the steps between
the ticks draw it along the arc */
static void vehicle_hijack_vault_place(
	struct vehicle_hijack_sequence const *sequence,
	long tick)
{
	struct biped_datum *biped = biped_try_and_get(sequence->jacker_unit_index);
	real_point3d point, next;

	if (!biped)
		return;
	vehicle_hijack_vault_point(sequence, tick, &point);
	vehicle_hijack_vault_point(sequence, tick + 1, &next);
	if (tick > 0)
		object_set_position(sequence->jacker_unit_index, &point, NULL, NULL);
	biped = biped_get(sequence->jacker_unit_index);
	vector_from_points3d(&point, &next, &biped->object.translational_velocity);
	/* (the level's gravity a tick, which the steps between take off) */
	if (tick < VEHICLE_HIJACK_VAULT_TICKS)
		biped->object.translational_velocity.k += 0.5f * global_gravity;
	SET_FLAG(biped->biped.flags, _biped_airborne_bit, tick < VEHICLE_HIJACK_VAULT_TICKS);
	biped->biped.jump_recovery_timer = 0;
	biped->biped.support_surface_index = NONE;
	SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);
}

static void vehicle_hijack_launch(
	struct vehicle_hijack_sequence *sequence)
{
	struct biped_datum *biped = biped_try_and_get(sequence->jacker_unit_index);
	struct object_datum *vehicle = object_get(sequence->vehicle_index);
	real_point3d entrance, landing, seat;
	real_vector3d out;
	real distance;
	real standing;
	real floor;
	real reach;

	sequence->vaulting = FALSE;
	if (!biped || !vehicle_hijack_reach(sequence->jacker_unit_index, sequence->vehicle_index, sequence->seat_index,
		&entrance, &distance))
	{
		return;
	}
	sequence->vault_start = biped->object.position;
	/* (how high the jacker's origin stands over its floor, where it can be
	found: the landing is at that height over the floor there) */
	/* (the jacker's, standing on the floor; else a biped's) */
	standing = VEHICLE_HIJACK_STANDING;
	if (!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		vehicle_hijack_floor(&sequence->vault_start, sequence->vehicle_index, &floor) &&
		sequence->vault_start.z - floor > 0.2f && sequence->vault_start.z - floor < 0.6f)
	{
		standing = sequence->vault_start.z - floor;
	}
	/* the landing: the seat's entrance (where its enter animation begins),
	moved out along the seat's way in until a body there is clear of the
	vehicle, on the floor */
	/* (the way out: from the seat (its marker) to its entrance, the way the
	enter animation comes in: a Ghost's or a Warthog driver's side, a
	Warthog gunner's back) */
	{
		struct unit_seat *seat_definition = TAG_BLOCK_GET_ELEMENT(
			&unit_definition_get(vehicle->definition_index)->unit.seats, sequence->seat_index, struct unit_seat);
		struct object_marker marker;

		seat = vehicle->object.bounding_sphere_center;
		if (object_get_marker_by_name(sequence->vehicle_index, seat_definition->marker_name, &marker, 1))
			seat = marker.matrix.position;
	}
	vector_from_points3d(&seat, &entrance, &out);
	out.k = 0.0f;
	if (normalize3d(&out) == 0.0f)
	{
		vector_from_points3d(&vehicle->object.bounding_sphere_center, &entrance, &out);
		out.k = 0.0f;
		if (normalize3d(&out) == 0.0f)
			out = *global_forward3d;
	}
	/* (out along the way in, a step at a time, to where the jacker's body
	(points about it, from the feet to the head) is clear of the vehicle:
	its physics, which physics.c keeps a biped out of, and its model) */
	for (reach = 0.0f; reach <= VEHICLE_HIJACK_LANDING_FARTHEST; reach += VEHICLE_HIJACK_LANDING_STEP)
	{
		landing.x = entrance.x + out.i * reach;
		landing.y = entrance.y + out.j * reach;
		landing.z = entrance.z;
		if (vehicle_hijack_floor(&landing, sequence->vehicle_index, &floor) && fabsf(floor - entrance.z) < 2.0f)
			landing.z = floor + standing;
		if (vehicle_hijack_body_clear(&landing, floor, sequence->vehicle_index, sequence->jacker_unit_index))
			break;
	}
	sequence->vault_landing = landing;
	/* (low: a quarter unit over the line; where the line at the waist
	crosses the vehicle (its physics, which a biped is kept out of: from
	its front round to the side), raised to clear it at the arc's top) */
	sequence->vault_height = VEHICLE_HIJACK_VAULT_HEIGHT;
	{
		struct collision_result collision;
		real_vector3d across;

		vector_from_points3d(&sequence->vault_start, &landing, &across);
		if (collision_test_vector(FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_vehicles_bit) |
			FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
			FLAG(_collision_test_use_vehicle_physics_bit), &sequence->vault_start, &across,
			sequence->jacker_unit_index, &collision) && collision.object_index == sequence->vehicle_index)
		{
			sequence->vault_height = VEHICLE_HIJACK_VAULT_CLEARING_HEIGHT;
		}
	}
	sequence->vaulting = TRUE;
	vehicle_hijack_vault_place(sequence, 0);
	error(_error_log, "hijack: vault of unit %lx from %.2f %.2f %.2f to %.2f %.2f %.2f (entrance %.2f %.2f %.2f, "
		"out %.2f %.2f, the vehicle's side %.2f out, standing %.2f)", sequence->jacker_unit_index, sequence->vault_start.x,
		sequence->vault_start.y, sequence->vault_start.z, landing.x, landing.y, landing.z, entrance.x, entrance.y,
		entrance.z, out.i, out.j, reach, standing);
}

static void vehicle_hijack_strike(
	struct vehicle_hijack_sequence *sequence)
{
	sequence->struck = TRUE;
	/* (the third person's melee and the first person's, with its sound: no
	damage, which only a melee's timer deals: bipeds.c) */
	unit_animation_start_action(sequence->jacker_unit_index, VEHICLE_HIJACK_MELEE_ACTION);
	if (unit_inventory_get_weapon(sequence->jacker_unit_index, unit_get(sequence->jacker_unit_index)->unit.current_weapon_index) !=
		NONE)
	{
		first_person_weapon_message_from_unit(sequence->jacker_unit_index, _first_person_weapon_message_melee);
	}
}

/* the melee's damage of the jacker's hand (its weapon's, or its unarmed
one), for the thrown player's jolt */
static long vehicle_hijack_jolt_definition(
	long jacker_unit_index)
{
	struct unit_datum *jacker = vehicle_hijack_living_unit(jacker_unit_index);
	long weapon_index;

	if (!jacker)
		return NONE;
	weapon_index = unit_inventory_get_weapon(jacker_unit_index, jacker->unit.current_weapon_index);
	if (weapon_index != NONE)
		return weapon_definition_get(object_get(weapon_index)->definition_index)->weapon.melee_attack_damage.index;
	return unit_unarmed_melee_damage(jacker_unit_index);
}

/* whether a rider of that radius has room standing at the spot, reached
from the seat */
static boolean vehicle_hijack_room(
	long rider_index,
	long vehicle_index,
	real_point3d const *from,
	real_point3d const *spot,
	real radius)
{
	struct collision_result collision;
	struct location location;
	real_vector3d way;
	long object_indices[16];
	short object_count;
	short index;

	scenario_location_from_point(&location, spot);
	if (location.cluster_index == NONE)
		return FALSE;
	/* (nothing of the level or another object between the seat and it) */
	vector_from_points3d(from, spot, &way);
	if (collision_test_vector(VEHICLE_HIJACK_ROOM_FLAGS, from, &way, vehicle_index, &collision) &&
		collision.object_index != rider_index)
	{
		return FALSE;
	}
	/* (no biped or other vehicle there) */
	object_count = objects_in_sphere(0, _object_mask_biped | _object_mask_vehicle, &location, spot, radius + 1.0f,
		object_indices, NUMBEROF(object_indices));
	for (index = 0; index < object_count; index++)
	{
		struct object_datum *object = object_get(object_indices[index]);

		if (object_indices[index] == rider_index || object_indices[index] == vehicle_index)
			continue;
		if (object->object.type == _object_type_vehicle)
		{
			if (distance3d(spot, &object->object.bounding_sphere_center) < object->object.bounding_sphere_radius + radius)
				return FALSE;
		}
		else if (distance3d(spot, &object->object.bounding_sphere_center) < 2.0f * radius)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* the rider thrown out of the seat, clear of the vehicle, alive and unhurt */
static void vehicle_hijack_throw(
	struct vehicle_hijack_sequence *sequence)
{
	/* (the ways tried: the cosine and sine of each turn from out of the
	entrance's side, toward the vehicle's back first) */
	static real const turns[VEHICLE_HIJACK_THROW_WAYS][2] =
	{
		{ 1.0f, 0.0f }, { 0.8191520f, -0.5735764f }, { 0.8191520f, 0.5735764f }, { 0.3420201f, -0.9396926f },
		{ 0.3420201f, 0.9396926f }, { -0.5f, -0.8660254f }, { -1.0f, 0.0f },
	};
	long rider_index = sequence->rider_unit_index;
	long vehicle_index = sequence->vehicle_index;
	struct unit_datum *rider = unit_get(rider_index);
	struct object_datum *vehicle = object_get(vehicle_index);
	struct biped_datum *biped;
	real_point3d seated, entrance, spot;
	real_vector3d out, back, way, velocity, offset;
	real radius = VEHICLE_HIJACK_RIDER_RADIUS;
	real reach;
	real distance;
	boolean placed = FALSE;
	short index;

	if (rider->object.type == _object_type_biped)
	{
		struct biped_definition *definition = biped_definition_get(rider->definition_index);

		if (definition->biped.collision_radius > 0.0f)
			radius = definition->biped.collision_radius;
	}
	seated = rider->object.bounding_sphere_center;
	/* (an AI told it leaves, as unit_try_and_exit_seat tells it) */
	if (rider->unit.actor_index != NONE)
		ai_handle_exit_vehicle(rider_index, vehicle_index);
	unit_exit_seat_end(rider_index);
	rider = unit_get(rider_index);
	if (rider->object.parent_object_index != NONE)
		return;

	/* out of the entrance's side (the jacker's), level */
	if (!vehicle_hijack_reach(sequence->jacker_unit_index, vehicle_index, sequence->seat_index, &entrance, &distance))
		entrance = object_get(sequence->jacker_unit_index)->object.position;
	vector_from_points3d(&vehicle->object.bounding_sphere_center, &entrance, &out);
	out.k = 0.0f;
	if (normalize3d(&out) == 0.0f)
	{
		out = vehicle->object.forward;
		out.k = 0.0f;
		if (normalize3d(&out) == 0.0f)
			out = *global_forward3d;
	}
	/* (and the vehicle's back, level, at a right angle to it) */
	back.i = out.j;
	back.j = -out.i;
	back.k = 0.0f;
	if (dot_product3d(&back, &vehicle->object.forward) > 0.0f)
		scale_vector3d(&back, -1.0f, &back);
	reach = vehicle->object.bounding_sphere_radius + radius + VEHICLE_HIJACK_THROW_MARGIN;
	for (index = 0; index < VEHICLE_HIJACK_THROW_WAYS && !placed; index++)
	{
		way.i = out.i * turns[index][0] - back.i * turns[index][1];
		way.j = out.j * turns[index][0] - back.j * turns[index][1];
		way.k = 0.0f;
		spot.x = vehicle->object.bounding_sphere_center.x + way.i * reach;
		spot.y = vehicle->object.bounding_sphere_center.y + way.j * reach;
		spot.z = MAX(seated.z, entrance.z + radius);
		if (vehicle_hijack_room(rider_index, vehicle_index, &seated, &spot, radius))
		{
			real_point3d origin;

			placed = TRUE;
			/* (the unit's origin, which the sphere's centre is above) */
			origin.x = spot.x;
			origin.y = spot.y;
			origin.z = spot.z - (rider->object.bounding_sphere_center.z - rider->object.position.z);
			/* (drawn moving from the seat to it: the offset to the nodes'
			first pose, in the unit's own frame, added to the blend the exit
			began from the seated pose (unit_exit_seat_end's change of
			animation state); one begun here would take the pose that has
			already left the seat for the first, a pop) */
			if (!rider->object.animation.interpolation_frame_count)
				object_start_interpolation(rider_index, VEHICLE_HIJACK_INTERPOLATION_TICKS);
			vector_from_points3d(&origin, &rider->object.position, &offset);
			object_set_position(rider_index, &origin, NULL, NULL);
			{
				real_matrix4x3 matrix;

				object_get_world_matrix(rider_index, &matrix);
				matrix4x3_inverse_transform_vector(&matrix, &offset, &offset);
			}
			object_offset_interpolation(rider_index, &offset);
			out = way;
		}
	}
	rider = unit_get(rider_index);
	/* flung up and away (with the vehicle's own way), airborne */
	velocity = vehicle->object.translational_velocity;
	velocity.i += out.i * VEHICLE_HIJACK_THROW_OUT;
	velocity.j += out.j * VEHICLE_HIJACK_THROW_OUT;
	velocity.k += VEHICLE_HIJACK_THROW_UP;
	rider->object.translational_velocity = velocity;
	SET_FLAG(rider->object.flags, _object_at_rest_bit, FALSE);
	biped = biped_try_and_get(rider_index);
	if (biped)
	{
		SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
		biped->biped.jump_recovery_timer = 0;
		biped->biped.support_surface_index = NONE;
	}
	/* (held until they land) */
	for (index = 0; index < MAXIMUM_THROWN_RIDERS; index++)
	{
		if (vehicle_hijack_globals.thrown[index].unit_index == NONE ||
			vehicle_hijack_globals.thrown[index].unit_index == rider_index)
		{
			vehicle_hijack_globals.thrown[index].unit_index = rider_index;
			vehicle_hijack_globals.thrown[index].start_time = game_time_get();
			break;
		}
	}
	/* (a player's view jolted, only seen) */
	if (rider->unit.player_index != NONE && player_get(rider->unit.player_index)->local_player_index != NONE)
	{
		long definition_index = vehicle_hijack_jolt_definition(sequence->jacker_unit_index);

		if (definition_index != NONE)
		{
			struct damage_data damage;
			real_vector3d direction = out;

			damage_data_new(&damage, definition_index);
			damage.owner_object_index = sequence->jacker_unit_index;
			player_effect_start(rider->unit.player_index, &damage, &direction, VEHICLE_HIJACK_JOLT_SCALE, 0.0f);
		}
	}
	error(_error_log, "hijack: unit %lx thrown out of vehicle %lx to %.2f %.2f %.2f%s", rider_index, vehicle_index,
		rider->object.position.x, rider->object.position.y, rider->object.position.z,
		placed ? "" : " (no room clear of it: where the ejection left them)");
}

static void vehicle_hijack_await_release(
	long unit_index)
{
	struct unit_datum *unit = vehicle_hijack_living_unit(unit_index);
	short index;

	if (!unit || unit->unit.player_index == NONE)
		return;
	for (index = 0; index < MAXIMUM_AWAITING_RELEASE; index++)
	{
		if (vehicle_hijack_globals.awaiting_release[index] == NONE ||
			vehicle_hijack_globals.awaiting_release[index] == unit->unit.player_index)
		{
			vehicle_hijack_globals.awaiting_release[index] = unit->unit.player_index;
			return;
		}
	}
}

/* the seat taken (the jacker beside an enemy who stays, a Warthog's gunner) */
static boolean vehicle_hijack_enter(
	struct vehicle_hijack_sequence *sequence)
{
	boolean entered;

	vehicle_hijack_globals.enemies_aboard_allowed = TRUE;
	if (sequence->authority)
	{
		entered = unit_enter_seat(sequence->jacker_unit_index, sequence->vehicle_index, sequence->seat_index);
	}
	else
	{
		/* (a client's own player, as its host's word will have it) */
		network_objects_set_seat(sequence->jacker_unit_index, sequence->vehicle_index, sequence->seat_index);
		entered = unit_get(sequence->jacker_unit_index)->object.parent_object_index == sequence->vehicle_index;
	}
	vehicle_hijack_globals.enemies_aboard_allowed = FALSE;
	vehicle_hijack_await_release(sequence->jacker_unit_index);
	return entered;
}

static boolean vehicle_hijack_unit_is_local(
	long unit_index)
{
	struct unit_datum *unit = vehicle_hijack_living_unit(unit_index);

	return unit && unit->unit.player_index != NONE &&
		player_get(unit->unit.player_index)->local_player_index != NONE;
}

static void vehicle_hijack_yank(
	struct vehicle_hijack_sequence *sequence)
{
	if (!sequence->struck)
		vehicle_hijack_strike(sequence);
	if (sequence->authority || vehicle_hijack_unit_is_local(sequence->rider_unit_index))
		vehicle_hijack_throw(sequence);
	/* (the seat taken where the jacker is this machine's to move; a
	client's other units' seats are the host's word, as any seat's) */
	if (sequence->authority || vehicle_hijack_unit_is_local(sequence->jacker_unit_index))
	{
		boolean entered = vehicle_hijack_enter(sequence);

		error(_error_log, "hijack: unit %lx %s seat %d of vehicle %lx%s", sequence->jacker_unit_index,
			entered ? "takes" : "did not take", sequence->seat_index, sequence->vehicle_index,
			sequence->authority ? "" : " (as the host's word will have it)");
	}
}

/* whether a sequence holds on: its units where it left them (the jacker on
foot, the rider in the seat), alive, the vehicle upright */
static boolean vehicle_hijack_holds(
	struct vehicle_hijack_sequence const *sequence,
	char const **reason)
{
	struct unit_datum *jacker = vehicle_hijack_living_unit(sequence->jacker_unit_index);
	struct unit_datum *rider = vehicle_hijack_living_unit(sequence->rider_unit_index);
	struct unit_datum *vehicle = vehicle_hijack_living_unit(sequence->vehicle_index);

	*reason = NULL;
	if (!vehicle || vehicle->object.type != _object_type_vehicle)
		*reason = "the vehicle is gone";
	else if (!vehicle_hijack_upright(sequence->vehicle_index))
		*reason = "the vehicle flipped";
	else if (!jacker || jacker->object.parent_object_index != NONE)
		*reason = "the jacker died or left";
	else if (!rider || rider->object.parent_object_index != sequence->vehicle_index ||
		rider->unit.parent_seat_index != sequence->seat_index)
	{
		*reason = "the rider died or left";
	}
	return *reason == NULL;
}

static void vehicle_hijack_end(
	struct vehicle_hijack_sequence *sequence)
{
	csmemset(sequence, 0, sizeof(*sequence));
	sequence->jacker_unit_index = NONE;
	sequence->rider_unit_index = NONE;
	sequence->vehicle_index = NONE;
	sequence->seat_index = NONE;
}

static struct vehicle_hijack_sequence *vehicle_hijack_begin(
	long jacker_unit_index,
	long rider_unit_index,
	long vehicle_index,
	short seat_index,
	boolean authority)
{
	struct vehicle_hijack_sequence *sequence = vehicle_hijack_sequence_of_jacker(jacker_unit_index);
	short index;

	for (index = 0; !sequence && index < MAXIMUM_VEHICLE_HIJACKS; index++)
	{
		if (vehicle_hijack_globals.sequences[index].jacker_unit_index == NONE)
			sequence = &vehicle_hijack_globals.sequences[index];
	}
	if (!sequence)
		return NULL;
	sequence->jacker_unit_index = jacker_unit_index;
	sequence->rider_unit_index = rider_unit_index;
	sequence->vehicle_index = vehicle_index;
	sequence->seat_index = seat_index;
	sequence->authority = authority;
	sequence->struck = FALSE;
	sequence->start_time = game_time_get();
	sequence->vaulting = FALSE;
	/* (a test's hold on the vehicle let go: the jacker drives it) */
	for (index = 0; index < MAXIMUM_TEST_HOLDS; index++)
	{
		if (vehicle_hijack_globals.test_holds[index].vehicle_index == vehicle_index)
			vehicle_hijack_globals.test_holds[index].vehicle_index = NONE;
	}
	/* (a leap of the jacker where they are this machine's to move) */
	if (authority || vehicle_hijack_unit_is_local(jacker_unit_index))
		vehicle_hijack_launch(sequence);
	return sequence;
}

/* ---------- public code */

boolean vehicle_hijack_held(
	long player_index,
	long vehicle_index,
	short seat_index)
{
	struct player_datum *player = player_get(player_index);
	long rider_index;

	vehicle_hijack_ready();
	/* (a client waits for its host's word: network_hijack.c) */
	if (network_game_distributed_client())
		return TRUE;
	if (vehicle_index == NONE || seat_index == NONE ||
		vehicle_hijack_seat_offered(player_index, vehicle_index) != seat_index)
	{
		return FALSE;
	}
	rider_index = vehicle_hijack_rider(vehicle_index, seat_index);
	if (rider_index == NONE ||
		!vehicle_hijack_begin(player->unit_index, rider_index, vehicle_index, seat_index, TRUE))
	{
		return FALSE;
	}
	network_hijack_send_start(player->unit_index, rider_index, vehicle_index, seat_index);
	error(_error_log, "hijack: player %lx's unit %lx leaps at seat %d of vehicle %lx, ridden by unit %lx%s",
		player_index, player->unit_index, seat_index, vehicle_index, rider_index,
		unit_get(rider_index)->unit.player_index != NONE ? " (a player's)" : " (an AI's)");
	return TRUE;
}

void vehicle_hijack_heard(
	long jacker_unit_index,
	long rider_unit_index,
	long vehicle_index,
	short seat_index)
{
	struct unit_datum *vehicle = vehicle_hijack_living_unit(vehicle_index);

	vehicle_hijack_ready();
	if (!vehicle_hijack_living_unit(jacker_unit_index) || !vehicle_hijack_living_unit(rider_unit_index) || !vehicle ||
		vehicle->object.type != _object_type_vehicle || seat_index < 0 ||
		seat_index >= unit_definition_get(vehicle->definition_index)->unit.seats.count)
	{
		return;
	}
	if (vehicle_hijack_begin(jacker_unit_index, rider_unit_index, vehicle_index, seat_index, FALSE))
	{
		error(_error_log, "hijack: the host's unit %lx leaps at seat %d of vehicle %lx, ridden by unit %lx",
			jacker_unit_index, seat_index, vehicle_index, rider_unit_index);
	}
}

void vehicle_hijack_heard_cancel(
	long jacker_unit_index)
{
	struct vehicle_hijack_sequence *sequence;

	vehicle_hijack_ready();
	sequence = vehicle_hijack_sequence_of_jacker(jacker_unit_index);
	if (sequence && !sequence->authority)
	{
		error(_error_log, "hijack: the host called off unit %lx's", jacker_unit_index);
		vehicle_hijack_end(sequence);
	}
}

void vehicle_hijack_tick(
	void)
{
	long now = game_time_get();
	short index;

	vehicle_hijack_ready();
	for (index = 0; index < MAXIMUM_VEHICLE_HIJACKS; index++)
	{
		struct vehicle_hijack_sequence *sequence = &vehicle_hijack_globals.sequences[index];
		long age = now - sequence->start_time;
		char const *reason;

		if (sequence->jacker_unit_index == NONE)
			continue;
		/* (a revert to before it began) */
		if (age < 0)
		{
			vehicle_hijack_end(sequence);
			continue;
		}
		if (!vehicle_hijack_holds(sequence, &reason))
		{
			error(_error_log, "hijack: unit %lx's called off: %s", sequence->jacker_unit_index, reason);
			if (sequence->authority)
				network_hijack_send_cancel(sequence->jacker_unit_index);
			vehicle_hijack_end(sequence);
			continue;
		}
		/* (the vault's arc, where the jacker is this machine's: on it at each
		tick's end, landed at its last) */
		if (sequence->vaulting)
		{
			vehicle_hijack_vault_place(sequence, age);
			if (age >= VEHICLE_HIJACK_VAULT_TICKS)
				sequence->vaulting = FALSE;
		}
		if (age >= VEHICLE_HIJACK_STRIKE_TICKS && !sequence->struck)
			vehicle_hijack_strike(sequence);
		if (age >= VEHICLE_HIJACK_YANK_TICKS)
		{
			vehicle_hijack_yank(sequence);
			vehicle_hijack_end(sequence);
		}
	}
	/* the thrown, held until they land */
	for (index = 0; index < MAXIMUM_THROWN_RIDERS; index++)
	{
		struct vehicle_hijack_thrown *thrown = &vehicle_hijack_globals.thrown[index];
		struct biped_datum *biped;
		long age = now - thrown->start_time;

		if (thrown->unit_index == NONE)
			continue;
		biped = biped_try_and_get(thrown->unit_index);
		if (!biped || age < 0 || age >= VEHICLE_HIJACK_THROWN_TICKS ||
			(age >= VEHICLE_HIJACK_THROWN_MINIMUM_TICKS && !TEST_FLAG(biped->biped.flags, _biped_airborne_bit)))
		{
			thrown->unit_index = NONE;
		}
	}
	for (index = 0; index < MAXIMUM_TEST_HOLDS; index++)
	{
		if (vehicle_hijack_globals.test_holds[index].vehicle_index != NONE &&
			now >= vehicle_hijack_globals.test_holds[index].until)
		{
			vehicle_hijack_globals.test_holds[index].vehicle_index = NONE;
		}
	}
}

void vehicle_hijack_reset(
	void)
{
	short index;

	for (index = 0; index < MAXIMUM_VEHICLE_HIJACKS; index++)
		vehicle_hijack_end(&vehicle_hijack_globals.sequences[index]);
	for (index = 0; index < MAXIMUM_THROWN_RIDERS; index++)
		vehicle_hijack_globals.thrown[index].unit_index = NONE;
	for (index = 0; index < MAXIMUM_AWAITING_RELEASE; index++)
		vehicle_hijack_globals.awaiting_release[index] = NONE;
	for (index = 0; index < MAXIMUM_TEST_HOLDS; index++)
		vehicle_hijack_globals.test_holds[index].vehicle_index = NONE;
	vehicle_hijack_globals.enemies_aboard_allowed = FALSE;
	vehicle_hijack_globals.ready = TRUE;
}

static void vehicle_hijack_ready(
	void)
{
	if (!vehicle_hijack_globals.ready)
		vehicle_hijack_reset();
}

boolean vehicle_hijack_unit_held(
	long unit_index)
{
	short index;

	vehicle_hijack_ready();
	if (unit_index == NONE)
		return FALSE;
	for (index = 0; index < MAXIMUM_VEHICLE_HIJACKS; index++)
	{
		if (vehicle_hijack_globals.sequences[index].jacker_unit_index == unit_index)
			return TRUE;
	}
	for (index = 0; index < MAXIMUM_THROWN_RIDERS; index++)
	{
		if (vehicle_hijack_globals.thrown[index].unit_index == unit_index)
			return TRUE;
	}
	return FALSE;
}

void vehicle_hijack_filter_action(
	long player_index,
	struct player_action *action)
{
	struct player_datum *player = player_get(player_index);
	short index;

	if (vehicle_hijack_unit_held(player->unit_index))
	{
		/* (the facing kept: the view is theirs) */
		action->control_flags = 0;
		action->throttle.i = 0.0f;
		action->throttle.j = 0.0f;
		action->primary_trigger = 0.0f;
	}
	for (index = 0; index < MAXIMUM_AWAITING_RELEASE; index++)
	{
		if (vehicle_hijack_globals.awaiting_release[index] != player_index)
			continue;
		if (TEST_FLAG(action->control_flags, _unit_control_action_bit))
		{
			SET_FLAG(action->control_flags, _unit_control_action_bit, FALSE);
			SET_FLAG(action->control_flags, _unit_control_swap_weapons_bit, FALSE);
		}
		else
		{
			vehicle_hijack_globals.awaiting_release[index] = NONE;
		}
	}
}

boolean vehicle_hijack_seat_held(
	long vehicle_index,
	long rider_index)
{
	short index;

	vehicle_hijack_ready();
	for (index = 0; index < MAXIMUM_VEHICLE_HIJACKS; index++)
	{
		struct vehicle_hijack_sequence const *sequence = &vehicle_hijack_globals.sequences[index];

		if (sequence->jacker_unit_index != NONE && sequence->vehicle_index == vehicle_index &&
			sequence->rider_unit_index == rider_index)
		{
			return TRUE;
		}
	}
	for (index = 0; index < MAXIMUM_TEST_HOLDS; index++)
	{
		if (vehicle_hijack_globals.test_holds[index].vehicle_index == vehicle_index && vehicle_index != NONE)
			return TRUE;
	}
	return FALSE;
}

boolean vehicle_hijack_enemies_aboard_allowed(
	void)
{
	return vehicle_hijack_globals.enemies_aboard_allowed;
}

void vehicle_hijack_set_enemies_aboard_allowed(
	boolean allowed)
{
	vehicle_hijack_globals.enemies_aboard_allowed = allowed;
}

void vehicle_hijack_test_hold(
	long vehicle_index,
	long ticks)
{
	short index;

	vehicle_hijack_ready();
	for (index = 0; index < MAXIMUM_TEST_HOLDS; index++)
	{
		if (vehicle_hijack_globals.test_holds[index].vehicle_index == NONE ||
			vehicle_hijack_globals.test_holds[index].vehicle_index == vehicle_index)
		{
			vehicle_hijack_globals.test_holds[index].vehicle_index = vehicle_index;
			vehicle_hijack_globals.test_holds[index].until = game_time_get() + ticks;
			return;
		}
	}
}
