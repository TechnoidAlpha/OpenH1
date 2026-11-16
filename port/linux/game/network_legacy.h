/*
NETWORK_LEGACY.H

The Legacy encodings of the host's per-tick streams (port/linux/NETCODE.md,
"The Legacy encodings"): network version 44's, OpenCE's, which a Legacy
game and an OpenCE machine of an OpenH1 game take, frozen byte for byte
(tools/harness/tests/test_legacy_golden.py). network_legacy_machine is the
one place that says which machines take them; the players' states and the
actors' are sent through it (network_distributed.c's
distributed_tick_states_begin, network_actors.c's actor_states_begin).
*/

#ifndef __NETWORK_LEGACY_H
#define __NETWORK_LEGACY_H
#pragma once

#include "game/players.h"
#include "network_distributed.h"
#include "vehicle_riding.h"

/* ---------- constants */

/* struct distributed_unit_state flags */
enum
{
	/* the player has a unit that is alive */
	_distributed_unit_alive_bit = 0,
	/* ... and not riding (its position is its own) */
	_distributed_unit_placed_bit,
	/* (dead) how it died: killed by a teammate, or by an empty vehicle */
	_distributed_unit_friendly_fire_bit,
	_distributed_unit_killed_by_vehicle_bit,
	/* (alive) its shields: down, charging, overcharging */
	_distributed_unit_shield_depleted_bit,
	_distributed_unit_shield_charging_bit,
	_distributed_unit_shield_over_charging_bit,
	/* (alive) the host has the unit where the client's own prediction had
	it at the tick in predicted_time */
	_distributed_unit_predicted_bit,
};

/* struct distributed_unit_state unit flags */
enum
{
	/* (alive) camouflaged, and doubly so */
	_distributed_unit_camouflaged_bit = 0,
	_distributed_unit_super_camouflaged_bit,
	/* (alive) the unit is suspended (OpenCE's, network version 42) */
	_distributed_unit_suspended_bit,
	/* (alive, placed: OpenH1's alone, never sent an OpenCE machine) its
	velocity at full precision follows the state's other parts
	(fine_velocity): it was a ninth part, which the parts' byte, all eight of
	its bits OpenCE's from version 43, has no room for. A state with it
	always has the camouflage part, which carries these flags */
	_distributed_unit_fine_velocity_bit = 7,
};

/* a unit state's parts on the wire: which of those that are not always
there it has (distributed_unit_state_write); one it has not is its default */
enum
{
	/* (alive) the vehicle it rides and the seat */
	_distributed_unit_part_riding_bit = 0,
	/* (alive, placed) its up, when not straight up */
	_distributed_unit_part_up_bit,
	/* (alive) the damage its shields and body show, when any */
	_distributed_unit_part_damage_bit,
	/* (alive) how long its powerups have left, when any */
	_distributed_unit_part_powerups_bit,
	/* (alive) its camouflage, when any */
	_distributed_unit_part_camouflage_bit,
	/* (dead) who killed it, when anyone */
	_distributed_unit_part_killer_bit,
	/* (alive) host-controlled facing, aim and look while input is inhibited
	(network version 42) */
	_distributed_unit_part_scripted_pose_bit,
	/* (alive, placed) where on a vehicle's deck it is, in that vehicle's
	window (vehicle_riding.h's struct vehicle_riding_deck; network version 43) */
	_distributed_unit_part_deck_bit,
	NUMBER_OF_DISTRIBUTED_UNIT_PARTS
};

/* the parts are the bits of one byte on the wire: a ninth would be lost
without a word (an OpenH1 game's velocity at full precision is a unit flag:
_distributed_unit_fine_velocity_bit) */
typedef char distributed_unit_parts_assert[NUMBER_OF_DISTRIBUTED_UNIT_PARTS <= 8 ? 1 : -1];

/* a scripted pose's vectors (_distributed_unit_part_scripted_pose_bit) */
enum
{
	_distributed_pose_facing,
	_distributed_pose_aiming,
	_distributed_pose_looking,
	_distributed_pose_current_aiming,
	_distributed_pose_current_looking,
	NUMBER_OF_DISTRIBUTED_POSE_VECTORS
};

enum
{
	/* a unit state's bytes on the wire: its player, flags and parts, and
	the most it has */
	DISTRIBUTED_UNIT_STATE_MINIMUM_SIZE = 3,
	/* (a deck part: its vehicle, node, place, velocity, ticks, flags and peak) */
	DISTRIBUTED_DECK_SIZE = 4 + 2 + 12 + 6 + 1 + 1 + 2,
	DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE = 3 + 4 + (4 + 2) + (12 + 3 * 6) + DISTRIBUTED_DECK_SIZE + 2 + 2 * 2 + 4 * 2 +
		2 * NUMBER_OF_PLAYER_POWERUPS + 2 + 1 + NUMBER_OF_DISTRIBUTED_POSE_VECTORS * 6,
};

/* struct distributed_actor_state flags */
enum
{
	_distributed_actor_rides_bit = 0,
	_distributed_actor_shield_depleted_bit,
	_distributed_actor_shield_charging_bit,
	_distributed_actor_shield_over_charging_bit,
	_distributed_actor_camouflaged_bit,
	_distributed_actor_super_camouflaged_bit,
	/* the impulse turns the unit to impulse_alignment */
	_distributed_actor_impulse_aligned_bit,
	/* fleeing in a panic, toward run_blindly_angle (unit_start_running_blindly) */
	_distributed_actor_running_blindly_bit,
};

/* facing, aiming and looking vectors are sent as yaw and pitch */
enum
{
	_angle_yaw,
	_angle_pitch,
	NUMBER_OF_ANGLES
};

/* ---------- structures */

/* a player's unit, as this machine has it (its bytes on the wire are fewer:
distributed_unit_state_write) */
struct distributed_unit_state
{
	byte player_index;
	byte flags;
	/* (dead) the player who killed it, NO_PLAYER for none */
	byte killing_player_index;
	byte unit_flags;
	/* the player's unit (the host's), NONE for none */
	long unit_index;
	/* the vehicle it rides and its seat, NONE for none */
	long vehicle_index;
	short seat_index;
	/* how camouflaged the unit is, of 255 */
	byte active_camouflage;
	byte pad;
	real_point3d position;
	struct distributed_vector velocity;
	struct distributed_vector forward;
	struct distributed_vector up;
	/* (_distributed_unit_predicted_bit) the client's tick, its low 16 bits */
	short predicted_time;
	/* (VITALITY_SCALE) */
	word body_vitality;
	word shield_vitality;
	/* what the shields' and the HUD's effects show */
	word current_body_damage;
	word recent_body_damage;
	word current_shield_damage;
	word recent_shield_damage;
	/* the player's powerups: how long each has left */
	short powerup_durations[NUMBER_OF_PLAYER_POWERUPS];
	boolean scripted_pose;
	struct distributed_vector pose[NUMBER_OF_DISTRIBUTED_POSE_VECTORS];
	/* (alive, placed) its player in a vehicle's window: the deck part, when
	deck_present (never on the wire: distributed_deck_put) */
	boolean deck_present;
	struct vehicle_riding_deck deck;
	/* (_distributed_unit_fine_velocity_bit) how fast it moves, at full
	precision, and whether it is sent so */
	real_vector3d fine_velocity;
	boolean fine;
};

/* an AI unit's control and state at the end of the host's tick (like
struct distributed_unit_state for players) */
struct distributed_actor_state
{
	long unit_index;
	long vehicle_index;
	short seat_index;
	byte flags;
	/* the impulse's sequence number; impulse is NO_IMPULSE when there is none */
	byte impulse_number;
	byte impulse;
	byte animation_state;
	byte aiming_speed;
	byte primary_trigger;
	word control_flags;
	signed char throttle[3];
	byte active_camouflage;
	/* the speech's sequence number; 0 when there is none */
	byte speech_number;
	/* the unit's team (NO_TEAM for none): the AI sets it after the unit is
	made, so a client's copy would keep the team it was made with */
	byte team;
	short facing[NUMBER_OF_ANGLES];
	short aiming[NUMBER_OF_ANGLES];
	short looking[NUMBER_OF_ANGLES];
	short impulse_alignment;
	short run_blindly_angle;
	word body_vitality;
	word shield_vitality;
	/* the sound the unit started saying (a sound tag) */
	long speech_sound;
	/* the custom animation it started (unit_start_user_animation), and its
	sequence number; the graph is NONE when there is none */
	long user_animation_graph;
	short user_animation;
	byte user_animation_number;
	byte user_animation_interpolate;
	real_point3d position;
	struct distributed_vector velocity;
	struct distributed_vector forward;
	struct distributed_vector up;
};

/* (what goes on the wire whole, as it is on every machine) */
typedef char distributed_actor_state_size_assert[sizeof(struct distributed_actor_state) == 88 ? 1 : -1];

/* ---------- prototypes/NETWORK_LEGACY.C */

/* (the host) whether the machine takes the Legacy encodings: every machine
of a Legacy game, and in an OpenH1 game every machine but an OpenH1 one
(an OpenCE machine, or one that joined as OpenCE does) */
boolean network_legacy_machine(long machine_index);

/* a player's unit state's bytes on the wire (at most
DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE), with the client's tick where
predicted, and the state read back from them no further than end (0 for
none). A Legacy machine's tick states (_distributed_message_unit_states)
and every client's predictions are these; OpenH1's messages carry them
whole too (a difference's or a sub-step's whole form). */
word distributed_unit_state_write(struct distributed_unit_state const *state, boolean predicted, byte *buffer);
word distributed_unit_state_read(byte const *buffer, byte const *end, struct distributed_unit_state *state);
/* (network_distributed.c's) a deck part's bytes, and from the wire (FALSE
when cut short); whether a scripted pose's vectors are unit vectors */
byte *distributed_deck_put(byte *cursor, struct vehicle_riding_deck const *deck);
boolean distributed_deck_take(byte const **cursor, byte const *end, boolean *present, struct vehicle_riding_deck *deck);
boolean distributed_scripted_pose_valid(struct distributed_unit_state const *state);

/* (the host) an actor's state to a machine that takes the Legacy encoding,
whole (_distributed_message_actor_states, its message sent as it fills): the
bytes it takes; and the rest sent */
word network_legacy_actor_state_add(long machine_index, struct distributed_actor_state const *state);
void network_legacy_actor_states_end(long machine_index);

#endif
