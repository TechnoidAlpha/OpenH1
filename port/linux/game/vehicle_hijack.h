/*
VEHICLE_HIJACK.H

Halo 2's boarding in OpenH1's games (vehicle_hijack.c): a player beside an
enemy's seat they could get into holds the action button, leaps at it,
strikes, throws the rider out and takes the seat.
*/

#ifndef VEHICLE_HIJACK_H
#define VEHICLE_HIJACK_H

struct player_action;

/* ---------- constants */

/* a vehicle's kind, by its definition's vehicle type (vehicles.c's) */
enum
{
	_vehicle_hijack_kind_none = 0,
	/* the Warthogs */
	_vehicle_hijack_kind_jeep,
	/* the Ghost */
	_vehicle_hijack_kind_scout,
	/* the Scorpion */
	_vehicle_hijack_kind_tank,
	/* the Banshee */
	_vehicle_hijack_kind_flyer,
	/* a gun turret, a boat, a plane */
	_vehicle_hijack_kind_other,
};

/* a seat's part: its flags' (unit_seat_is_driver, unit_seat_is_gunner) */
enum
{
	_vehicle_hijack_seat_driver = 0,
	_vehicle_hijack_seat_gunner,
	_vehicle_hijack_seat_passenger,
};

/* the sequence, in ticks from the hold's end (30 a second, at any rate):
the leap to the seat's side, the blow as it comes down, the rider thrown
out and the seat taken, the thrown rider's own controls back once they
land (or this long after, at the most) */
enum
{
	VEHICLE_HIJACK_VAULT_TICKS = 7,
	VEHICLE_HIJACK_STRIKE_TICKS = 7,
	VEHICLE_HIJACK_YANK_TICKS = 11,
	VEHICLE_HIJACK_THROWN_TICKS = 2 * 30,
};

/* a flyer (the Banshee) is boarded only this near the ground (world units
from its origin down to the level), and any vehicle only this slow (world
units a tick: 3 a second, a walk) */
#define VEHICLE_HIJACK_FLYER_GROUND_CLEARANCE 1.5f
#define VEHICLE_HIJACK_MAXIMUM_SPEED 0.1f

/* ---------- structures */

/* what decides whether a rider's seat may be hijacked
(vehicle_hijack_seat_allowed) */
struct vehicle_hijack_seat_facts
{
	/* the game's rule (network_standard_vehicle_hijack) */
	boolean rule;
	/* the rider is the jacker's enemy (game_team_is_enemy), and alive */
	boolean enemy;
	boolean rider_alive;
	/* either of them a player of an OpenCE machine (a mixed game's), whose
	machine plays no hijack */
	boolean opence;
	/* the vehicle: its kind, the seat's part, whether it is upright and
	how fast it moves (world units a tick); a flyer's way down to the
	level (REAL_MAX: none found) */
	short kind;
	short part;
	boolean upright;
	real speed;
	real ground_clearance;
};

/* ---------- prototypes/VEHICLE_HIJACK.C */

/* the decision, of the facts alone (tools/harness/tests/test_vehicle_hijack.py):
the rule on, an enemy's living rider, neither an OpenCE machine's; a
Ghost's or a Warthog's driver, a Warthog's gunner, a Banshee's pilot on or
near the ground; never the Scorpion, a turret, a passenger; upright and no
faster than a walk */
boolean vehicle_hijack_seat_allowed(struct vehicle_hijack_seat_facts const *facts);
/* a vehicle's kind */
short vehicle_hijack_vehicle_kind(long vehicle_index);

/* players.c's action: the seat the player could hijack of the vehicle
(their reach of it as for an empty seat, the facts above), else NONE */
short vehicle_hijack_seat_offered(long player_index, long vehicle_index);
/* the player's action button held out on it (the hold that swaps a
weapon): the hijack begun, where this machine decides the game (a
client waits for the host's word). TRUE: the hold is taken */
boolean vehicle_hijack_held(long player_index, long vehicle_index, short seat_index);
/* each tick, a player's input before it reaches their unit: none while
they leap or are thrown, the action button not again until let go once
the seat is theirs */
void vehicle_hijack_filter_action(long player_index, struct player_action *action);
/* whether a unit's controls are held (a jacker's leap, a thrown rider in
the air): player_control.c's sub-steps leave them */
boolean vehicle_hijack_unit_held(long unit_index);
/* whether the vehicle's rider in that seat passes on no controls (a seat
being hijacked: units.c's unit_update) */
boolean vehicle_hijack_seat_held(long vehicle_index, long rider_index);
/* whether a unit may be put in a seat of a vehicle an enemy rides (a
hijacker beside an enemy gunner, as the host has them: units.c's
unit_can_enter_seat) */
boolean vehicle_hijack_enemies_aboard_allowed(void);
void vehicle_hijack_set_enemies_aboard_allowed(boolean allowed);

/* each tick, after its sub-steps (game.c): the sequences played on */
void vehicle_hijack_tick(void);
/* a new map, a game's start: nothing under way */
void vehicle_hijack_reset(void);

/* the host's word (network_hijack.c): a hijack begun (on this machine as
the host's: its own player leaps, a thrown player of its own is thrown),
or called off */
void vehicle_hijack_heard(long jacker_unit_index, long rider_unit_index, long vehicle_index, short seat_index);
void vehicle_hijack_heard_cancel(long jacker_unit_index);

/* debug.network_test_hijack (network_test.c): the vehicle's rider in the
seat holds still (the rider drives nowhere) for that many ticks */
void vehicle_hijack_test_hold(long vehicle_index, long ticks);

#endif
