/* bots_vehicles.h: the vehicles as the bots see them (port/linux/game/bots_vehicles.c)

What bots.c needs to know of a vehicle to board, drive, ride or fly it: its
kind (its definition's vehicle type: the Warthog's jeep, the Ghost's scout,
the Scorpion's tank, the Banshee's flyer), each seat's part (the driver's,
the gunner's, a passenger's), a seat of a part a unit may take and where it
gets in, the weapon a unit in a seat fires, and for a flyer the room ahead
of it. Everything here reads the game state at the tick; nothing is kept. */
#ifndef HALO_BOTS_VEHICLES_H
#define HALO_BOTS_VEHICLES_H

/* a vehicle's kind, as the bots use it */
enum
{
	_bots_vehicle_none = 0,
	/* the Warthogs (the rocket Warthog too): a driver, a gunner, a passenger */
	_bots_vehicle_jeep,
	/* the Ghost: its driver steers and fires */
	_bots_vehicle_scout,
	/* the Scorpion: its driver steers and fires the cannon */
	_bots_vehicle_tank,
	/* the Banshee: flown, its driver fires */
	_bots_vehicle_flyer,
	/* a gun turret, a boat, a plane: not used */
	_bots_vehicle_other,
	NUMBER_OF_BOTS_VEHICLE_KINDS,
};

/* a seat's part */
enum
{
	_bots_seat_none = 0,
	_bots_seat_driver,
	/* fires the vehicle's weapon (the Warthog's gun) */
	_bots_seat_gunner,
	/* rides, firing its own weapon */
	_bots_seat_passenger,
};

/* the vehicle's kind (_bots_vehicle_none for an object that is not one) */
short bots_vehicle_kind(long vehicle_index);
/* the seat's part */
short bots_vehicle_seat_part(long vehicle_index, short seat_index);
/* the unit's part in the vehicle it is in (_bots_seat_none: on foot) */
short bots_vehicle_unit_part(long unit_index);
/* a seat of that part of the vehicle the unit can get into now (empty, no
enemy in the vehicle, a seat its biped has the animations of), where it
gets in (its entrance); NONE for none */
short bots_vehicle_free_seat(long unit_index, long vehicle_index, short part, real_point3d *entrance);
/* the weapon the unit fires: in a seat that guns its vehicle, the
vehicle's; in a driver's seat that does not (a Warthog's), none; else its
own. NONE for none */
long bots_vehicle_unit_weapon(long unit_index);
/* the vehicle's driver, gunner: units in it of another player, NONE */
long bots_vehicle_occupant(long vehicle_index, short part);
/* the vehicle is on its wheels (not on its side or roof) */
boolean bots_vehicle_upright(long vehicle_index);
/* how far along the way from the point the level or scenery is, and the
other vehicles than that one (NONE: not the vehicles); the way's length
for nothing */
real bots_vehicle_room(real_point3d const *point, real_vector3d const *way, long vehicle_index);
/* the point is under the open sky (nothing of the level within 25 units
above): a place a vehicle may be driven to */
boolean bots_vehicle_outdoors(real_point3d const *point);

#endif
