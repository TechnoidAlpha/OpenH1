/*
BOTS_VEHICLES.C

The vehicles as the bots see them (bots_vehicles.h): what kind each is, its
seats' parts and entrances, the weapon a seat fires, the room about a flyer.

A vehicle's kind is its definition's vehicle type (vehicles.c's: the human
jeep, the alien scout, the human tank, the alien fighter), so that a Custom
Edition map's own vehicles are known as the stock ones are. A seat's part is
its flags: a driver's seat steers (and guns the vehicle when it is the
gunner's too: the Ghost's, the Banshee's, the Scorpion's), a gunner's seat
fires the vehicle's weapon (the Warthog's gun), and any other seat is a
passenger's, who fires their own.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/vehicle_definitions.h"
#include "physics/collisions.h"
#include "tag_files/tag_groups.h"

#include "bots_vehicles.h"

#include <math.h>

/* ---------- constants */

/* vehicles.c's vehicle types */
enum
{
	_bots_vehicle_type_human_tank = 0,
	_bots_vehicle_type_human_jeep,
	_bots_vehicle_type_human_boat,
	_bots_vehicle_type_human_plane,
	_bots_vehicle_type_alien_scout,
	_bots_vehicle_type_alien_fighter,
	_bots_vehicle_type_turret,
};

/* ---------- structures */

/* the start of a vehicle's definition (vehicles.c's struct
vehicle_definition, which is that file's own) */
struct bots_vehicle_definition
{
	struct unit_definition unit;
	unsigned long flags;
	short vehicle_type;
};

/* ---------- code */

short bots_vehicle_kind(long vehicle_index)
{
	struct object_datum *object = object_try_and_get_and_verify_type(vehicle_index, _object_mask_vehicle);
	struct bots_vehicle_definition const *definition;

	if (!object)
		return _bots_vehicle_none;
	definition = (struct bots_vehicle_definition const *)tag_get(VEHICLE_DEFINITION_TAG, object->definition_index);
	switch (definition->vehicle_type)
	{
	case _bots_vehicle_type_human_jeep:
		return _bots_vehicle_jeep;
	case _bots_vehicle_type_alien_scout:
		return _bots_vehicle_scout;
	case _bots_vehicle_type_human_tank:
		return _bots_vehicle_tank;
	case _bots_vehicle_type_alien_fighter:
		return _bots_vehicle_flyer;
	}
	return _bots_vehicle_other;
}

short bots_vehicle_seat_part(long vehicle_index, short seat_index)
{
	if (unit_seat_is_driver(vehicle_index, seat_index))
		return _bots_seat_driver;
	if (unit_seat_is_gunner(vehicle_index, seat_index))
		return _bots_seat_gunner;
	return _bots_seat_passenger;
}

short bots_vehicle_unit_part(long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);

	if (unit->object.parent_object_index == NONE || unit->unit.parent_seat_index == NONE ||
		!object_try_and_get_and_verify_type(unit->object.parent_object_index, _object_mask_vehicle))
	{
		return _bots_seat_none;
	}
	return bots_vehicle_seat_part(unit->object.parent_object_index, unit->unit.parent_seat_index);
}

short bots_vehicle_free_seat(long unit_index, long vehicle_index, short part, real_point3d *entrance)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	struct unit_definition *definition = unit_definition_get(vehicle->definition_index);
	short seat_index;

	if (TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit) ||
		TEST_FLAG(vehicle->unit.flags, _unit_not_enterable_by_player_bit))
	{
		return NONE;
	}
	for (seat_index = 0; seat_index < definition->unit.seats.count; seat_index++)
	{
		long occupant = NONE;
		real_point3d seat_point;

		if (bots_vehicle_seat_part(vehicle_index, seat_index) != part ||
			!unit_can_enter_seat(unit_index, vehicle_index, seat_index, &occupant) ||
			!unit_get_seat_entrance_point(unit_index, vehicle_index, seat_index, entrance, &seat_point, NULL))
		{
			continue;
		}
		return seat_index;
	}
	return NONE;
}

long bots_vehicle_unit_weapon(long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct unit_datum *holder = unit;
	short part = bots_vehicle_unit_part(unit_index);

	if (part != _bots_seat_none)
	{
		struct unit_datum *vehicle = unit_get(unit->object.parent_object_index);

		if (vehicle->unit.gunner_object_index == unit_index)
			holder = vehicle;
		else if (part == _bots_seat_driver)
			return NONE;
	}
	if (holder->unit.current_weapon_index < 0 || holder->unit.current_weapon_index >= MAXIMUM_WEAPONS_PER_UNIT)
		return NONE;
	return holder->unit.weapon_object_indices[holder->unit.current_weapon_index];
}

long bots_vehicle_occupant(long vehicle_index, short part)
{
	struct unit_datum *vehicle = unit_get(vehicle_index);
	long child;

	if (part == _bots_seat_driver)
		return vehicle->unit.driver_object_index;
	for (child = vehicle->object.first_child_object_index; child != NONE; child = object_get(child)->object.next_object_index)
	{
		struct object_datum *object = object_get(child);

		if (TEST_FLAG(_object_mask_unit, object->object.type) && unit_get(child)->unit.parent_seat_index != NONE &&
			bots_vehicle_seat_part(vehicle_index, unit_get(child)->unit.parent_seat_index) == part)
		{
			return child;
		}
	}
	return NONE;
}

boolean bots_vehicle_upright(long vehicle_index)
{
	return object_get(vehicle_index)->object.up.k > 0.5f;
}

real bots_vehicle_room(real_point3d const *point, real_vector3d const *way, long vehicle_index)
{
	struct collision_result collision;
	real length = (real)sqrt(way->i * way->i + way->j * way->j + way->k * way->k);

	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_scenery_bit) |
		(vehicle_index != NONE ? FLAG(_collision_test_objects_vehicles_bit) : 0), point, way, vehicle_index, &collision))
	{
		return distance3d(point, &collision.point);
	}
	return length;
}

boolean bots_vehicle_outdoors(real_point3d const *point)
{
	struct collision_result collision;
	real_point3d start = *point;
	real_vector3d up = { 0.0f, 0.0f, 25.0f };

	start.z += 0.5f;
	return !collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_back_facing_surfaces_bit) |
		FLAG(_collision_test_front_facing_surfaces_bit), &start, &up, NONE, &collision);
}
