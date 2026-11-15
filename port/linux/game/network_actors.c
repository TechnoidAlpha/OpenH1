/*
NETWORK_ACTORS.C

AI sync (port/linux/NETCODE.md).

Only the host runs the AI (game.c skips ai_update on clients). An actor
drives its unit the way a player does, through unit_control. The clients
have the units (network_objects.c); this file makes them move like the
host's.

Each tick the host records the control every actor gave its unit (a hook
in unit_control, and the bursts it holds the trigger for) and sends it to
each client with the unit's state: position, seat, shields and health,
camouflage, and whether it flees in a panic. Units near a client's players
are sent every tick, others less often, but a unit starting a one-tick
action (a jump, a grenade) goes to everyone that tick. In an OpenH1 game a
unit whose control makes a press (a trigger pulled or let go, a start, a
stop, a sharp turn) goes to every OpenH1 client that tick too, and a unit
aiming at a client's player, or that player's aim on it, goes to that
client every tick. Each tick a client feeds the latest control into
unit_control, the same path that drives a remote player's unit, so the unit
walks, aims, fires and throws as it does on the host. The state corrects
any drift.

What the AI starts once is numbered, so a client plays each once however
many times it receives it: animation impulses (dives, vaults), melee
attacks and leaps (which the AI starts directly, not through the control),
speech, and custom animations (outside co-op, whose events carry them).
Pain and death sounds aren't sent: a client plays those itself as it
replays the host's damage.

A client creates no actors of its own (actors.c): the host's are the only
ones. Deaths need nothing here: network_damage.c kills a client's copy of
an AI unit the same way it kills a player's.
*/

/* ---------- headers */

#include "cseries.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "objects/objects.h"
#include "objects/damage.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "units/units.h"
#include "units/unit_control_data.h"
#include "network_coop.h"
#include "network_distributed.h"
#include "network_legacy.h"
#include "network_standard.h"
#include "vehicle_riding.h"

#include <math.h>

/* ---------- constants */

enum
{
	/* AI units tracked at once: every actor (halo_port_capacity.h's
	HALO_PORT_MAXIMUM_ACTORS) and the units the cutscenes' recorded
	animations drive */
	MAXIMUM_NETWORK_ACTORS = HALO_PORT_MAXIMUM_ACTORS + 32,
	MAXIMUM_ENTRIES_PER_MESSAGE = 64,
	/* A client keeps applying a control for this long after the host last
	sent one. After that the unit is left alone: the actor let go of it, or
	the host went quiet. */
	CONTROL_HELD_TICKS = 2 * TICKS_PER_SECOND,
	/* an impulse is repeated in this many entries, in case one is lost */
	IMPULSE_REPEAT_TICKS = 3,
	/* the number of unit_start_animation_impulse impulses (private to units.c) */
	NUMBER_OF_UNIT_ANIMATION_IMPULSES = 14,
	NO_IMPULSE = 0xFF,
	NO_TEAM = 0xFF,

	/* An entry's impulse is one of the animation impulses or, past them, an
	action the AI starts directly: a melee attack (unit_melee_attack_begin)
	or a leap (unit_leap_begin). */
	_actor_action_melee = NUMBER_OF_UNIT_ANIMATION_IMPULSES,
	_actor_action_leap,
	NUMBER_OF_ACTOR_ACTIONS,

	/* control flags a unit acts on in one tick: a unit that sets one goes to
	every client that tick, however far away, so the action isn't missed */
	ONE_SHOT_CONTROL_FLAGS = FLAG(_unit_control_jump_bit) | FLAG(_unit_control_action_bit) |
		FLAG(_unit_control_weapon_reload_bit) | FLAG(_unit_control_throw_grenade_bit) |
		FLAG(_unit_control_swap_weapons_bit),

	/* a press (actor_state_pressed): the trigger or a stick (of 255 and of
	127) moved by a quarter of its reach, or the aim turned by 1/32 of a
	turn (11 degrees, of the 65536 of the packed angles) */
	PRESSED_TRIGGER_STEP = 64,
	PRESSED_STICK_STEP = 32,
	PRESSED_AIM_STEP = 2048,
};

/* ---------- structures */

/* the damage an AI unit is taking: its shield's flare and its body's wounds
are drawn by it */
struct distributed_actor_damage
{
	long unit_index;
	word current_shield_damage;
	word recent_shield_damage;
	word current_body_damage;
	word recent_body_damage;
};

struct distributed_actor_damage_message
{
	struct distributed_message_header header;
	struct distributed_actor_damage units[MAXIMUM_ENTRIES_PER_MESSAGE];
};

/* host: the control of the entry last sent to every client at once (an
OpenH1 game's press is told against it) */
struct actor_pressed
{
	boolean valid;
	byte animation_state;
	byte primary_trigger;
	word control_flags;
	signed char throttle[3];
	short aiming[NUMBER_OF_ANGLES];
};

/* host: what an actor gave its unit this tick */
struct host_actor
{
	long unit_index;
	/* got a control since the last send (noted during game_tick, sent after it) */
	boolean noted;
	/* its damage was sent last tick (and once more when it ends) */
	boolean damaged;
	struct unit_control_data control;
	/* the control flags of the last tick's entry */
	word sent_control_flags;
	/* the control last sent to every client at once */
	struct actor_pressed pressed;
	/* the latest impulse, how many more entries carry it, and its number */
	short impulse;
	short impulse_sends;
	byte impulse_number;
	boolean impulse_aligned;
	real_vector2d impulse_alignment;
	/* the latest speech, how many more entries carry it, and its number */
	long speech_sound;
	short speech_sends;
	byte speech_number;
	/* the latest custom animation, the same way */
	long user_animation_graph;
	short user_animation;
	boolean user_animation_interpolate;
	short user_animation_sends;
	byte user_animation_number;
};

/* client: the latest the host sent about an AI unit */
struct client_actor
{
	long unit_index;
	long received_time;
	struct unit_control_data control;
	/* the impulse to play, and the number of the last one played */
	short impulse;
	byte impulse_number;
	byte played_impulse_number;
	boolean impulse_aligned;
	real_vector2d impulse_alignment;
	/* the speech to play (NONE when there is none), and the number of the last played */
	long speech_sound;
	byte speech_number;
	byte played_speech_number;
	/* the custom animation to play (graph NONE when there is none), and the
	number of the last played */
	long user_animation_graph;
	short user_animation;
	boolean user_animation_interpolate;
	byte user_animation_number;
	byte played_user_animation_number;
	boolean driven;
};

/* ---------- globals */

static struct host_actor host_actors[MAXIMUM_NETWORK_ACTORS];
static short host_actor_count;
/* host: the numbers of every unit's impulses, speech and animations, from
one counter, so a unit that drops out of the table and comes back can't
repeat the number a client last played */
static byte host_event_number;

static struct client_actor client_actors[MAXIMUM_NETWORK_ACTORS];
static short client_actor_count;

/* host: the bytes of actors' states sent since the tests last asked, and of
those the entries an OpenH1 game's press or aim alone sent */
static long actors_bytes_sent;
static long actors_pressed_bytes_sent;
static long actors_aimed_bytes_sent;

/* ---------- compact states
An OpenH1 game's host sends its OpenH1 clients the actors' states without
the parts at their defaults (_distributed_message_actor_states_compact,
actor_state_compact_write), which most are: an actor rides nothing, starts
no impulse, says nothing, plays no custom animation, does not flee, is not
camouflaged, stands up straight and looks where it aims. A part comes
whole where any of its values is not its default, and the state read is the
state written. Legacy games and OpenCE machines take the whole structure
as before. */

/* the parts of a compact state that are there (16 bits, after the unit) */
enum
{
	_actor_part_riding_bit = 0,
	_actor_part_impulse_bit,
	_actor_part_speech_bit,
	_actor_part_animation_bit,
	_actor_part_run_blindly_bit,
	_actor_part_camouflage_bit,
	_actor_part_looking_bit,
	_actor_part_facing_bit,
	_actor_part_up_bit,
	_actor_part_velocity_bit,
	_actor_part_throttle_bit,
	_actor_part_control_bit,
	_actor_part_trigger_bit,
};

enum
{
	/* its least bytes: the parts, the unit, its flags, animation state,
	aiming speed and team, its aim, shields and health, where it is and
	which way it faces */
	ACTOR_COMPACT_MINIMUM_SIZE = 2 + 4 + 4 + 2 * NUMBER_OF_ANGLES + 4 + 12 + 6,
	ACTOR_COMPACT_MAXIMUM_SIZE = ACTOR_COMPACT_MINIMUM_SIZE + (4 + 2) + (1 + 1 + 2) + (1 + 4) + (4 + 2 + 1 + 1) + 2 +
		1 + 2 * 2 * NUMBER_OF_ANGLES + 6 + 6 + 3 + 2 + 1,
};

static byte *actor_put(
	byte *cursor,
	void const *data,
	short size)
{
	csmemcpy(cursor, data, size);
	return cursor + size;
}

static boolean actor_take(
	byte const **cursor,
	byte const *end,
	void *data,
	short size)
{
	if (end - *cursor < size)
		return FALSE;
	csmemcpy(data, *cursor, size);
	*cursor += size;
	return TRUE;
}

/* a state's bytes, compact (at most ACTOR_COMPACT_MAXIMUM_SIZE) */
static word actor_state_compact_write(
	struct distributed_actor_state const *state,
	byte *buffer)
{
	word parts = 0;
	byte *cursor = buffer + 2;

	cursor = actor_put(cursor, &state->unit_index, sizeof(state->unit_index));
	*cursor++ = state->flags;
	*cursor++ = state->animation_state;
	*cursor++ = state->aiming_speed;
	*cursor++ = state->team;
	cursor = actor_put(cursor, state->aiming, sizeof(state->aiming));
	cursor = actor_put(cursor, &state->body_vitality, sizeof(state->body_vitality));
	cursor = actor_put(cursor, &state->shield_vitality, sizeof(state->shield_vitality));
	cursor = actor_put(cursor, &state->position, sizeof(state->position));
	cursor = actor_put(cursor, &state->forward, sizeof(state->forward));
	if (state->vehicle_index != NONE || state->seat_index != NONE)
	{
		SET_FLAG(parts, _actor_part_riding_bit, TRUE);
		cursor = actor_put(cursor, &state->vehicle_index, sizeof(state->vehicle_index));
		cursor = actor_put(cursor, &state->seat_index, sizeof(state->seat_index));
	}
	if (state->impulse != NO_IMPULSE || state->impulse_number || state->impulse_alignment)
	{
		SET_FLAG(parts, _actor_part_impulse_bit, TRUE);
		*cursor++ = state->impulse;
		*cursor++ = state->impulse_number;
		cursor = actor_put(cursor, &state->impulse_alignment, sizeof(state->impulse_alignment));
	}
	if (state->speech_sound != NONE || state->speech_number)
	{
		SET_FLAG(parts, _actor_part_speech_bit, TRUE);
		*cursor++ = state->speech_number;
		cursor = actor_put(cursor, &state->speech_sound, sizeof(state->speech_sound));
	}
	if (state->user_animation_graph != NONE || state->user_animation || state->user_animation_number ||
		state->user_animation_interpolate)
	{
		SET_FLAG(parts, _actor_part_animation_bit, TRUE);
		cursor = actor_put(cursor, &state->user_animation_graph, sizeof(state->user_animation_graph));
		cursor = actor_put(cursor, &state->user_animation, sizeof(state->user_animation));
		*cursor++ = state->user_animation_number;
		*cursor++ = state->user_animation_interpolate;
	}
	if (state->run_blindly_angle)
	{
		SET_FLAG(parts, _actor_part_run_blindly_bit, TRUE);
		cursor = actor_put(cursor, &state->run_blindly_angle, sizeof(state->run_blindly_angle));
	}
	if (state->active_camouflage)
	{
		SET_FLAG(parts, _actor_part_camouflage_bit, TRUE);
		*cursor++ = state->active_camouflage;
	}
	if (csmemcmp(state->looking, state->aiming, sizeof(state->aiming)))
	{
		SET_FLAG(parts, _actor_part_looking_bit, TRUE);
		cursor = actor_put(cursor, state->looking, sizeof(state->looking));
	}
	if (csmemcmp(state->facing, state->aiming, sizeof(state->aiming)))
	{
		SET_FLAG(parts, _actor_part_facing_bit, TRUE);
		cursor = actor_put(cursor, state->facing, sizeof(state->facing));
	}
	if (state->up.i || state->up.j || state->up.k != (short)DISTRIBUTED_UNIT_SCALE)
	{
		SET_FLAG(parts, _actor_part_up_bit, TRUE);
		cursor = actor_put(cursor, &state->up, sizeof(state->up));
	}
	if (state->velocity.i || state->velocity.j || state->velocity.k)
	{
		SET_FLAG(parts, _actor_part_velocity_bit, TRUE);
		cursor = actor_put(cursor, &state->velocity, sizeof(state->velocity));
	}
	if (state->throttle[0] || state->throttle[1] || state->throttle[2])
	{
		SET_FLAG(parts, _actor_part_throttle_bit, TRUE);
		cursor = actor_put(cursor, state->throttle, sizeof(state->throttle));
	}
	if (state->control_flags)
	{
		SET_FLAG(parts, _actor_part_control_bit, TRUE);
		cursor = actor_put(cursor, &state->control_flags, sizeof(state->control_flags));
	}
	if (state->primary_trigger)
	{
		SET_FLAG(parts, _actor_part_trigger_bit, TRUE);
		*cursor++ = state->primary_trigger;
	}
	csmemcpy(buffer, &parts, sizeof(parts));
	return (word)(cursor - buffer);
}

/* a state from its compact bytes, no further than end: the bytes it took, 0
for none that make one */
static word actor_state_compact_read(
	byte const *buffer,
	byte const *end,
	struct distributed_actor_state *state)
{
	byte const *cursor = buffer;
	word parts;

	csmemset(state, 0, sizeof(*state));
	state->vehicle_index = NONE;
	state->seat_index = NONE;
	state->impulse = NO_IMPULSE;
	state->speech_sound = NONE;
	state->user_animation_graph = NONE;
	state->up.k = (short)DISTRIBUTED_UNIT_SCALE;
	if (!actor_take(&cursor, end, &parts, sizeof(parts)) ||
		!actor_take(&cursor, end, &state->unit_index, sizeof(state->unit_index)) ||
		!actor_take(&cursor, end, &state->flags, sizeof(state->flags)) ||
		!actor_take(&cursor, end, &state->animation_state, sizeof(state->animation_state)) ||
		!actor_take(&cursor, end, &state->aiming_speed, sizeof(state->aiming_speed)) ||
		!actor_take(&cursor, end, &state->team, sizeof(state->team)) ||
		!actor_take(&cursor, end, state->aiming, sizeof(state->aiming)) ||
		!actor_take(&cursor, end, &state->body_vitality, sizeof(state->body_vitality)) ||
		!actor_take(&cursor, end, &state->shield_vitality, sizeof(state->shield_vitality)) ||
		!actor_take(&cursor, end, &state->position, sizeof(state->position)) ||
		!actor_take(&cursor, end, &state->forward, sizeof(state->forward)))
	{
		return 0;
	}
	csmemcpy(state->looking, state->aiming, sizeof(state->aiming));
	csmemcpy(state->facing, state->aiming, sizeof(state->aiming));
	if ((TEST_FLAG(parts, _actor_part_riding_bit) &&
			(!actor_take(&cursor, end, &state->vehicle_index, sizeof(state->vehicle_index)) ||
				!actor_take(&cursor, end, &state->seat_index, sizeof(state->seat_index)))) ||
		(TEST_FLAG(parts, _actor_part_impulse_bit) &&
			(!actor_take(&cursor, end, &state->impulse, sizeof(state->impulse)) ||
				!actor_take(&cursor, end, &state->impulse_number, sizeof(state->impulse_number)) ||
				!actor_take(&cursor, end, &state->impulse_alignment, sizeof(state->impulse_alignment)))) ||
		(TEST_FLAG(parts, _actor_part_speech_bit) &&
			(!actor_take(&cursor, end, &state->speech_number, sizeof(state->speech_number)) ||
				!actor_take(&cursor, end, &state->speech_sound, sizeof(state->speech_sound)))) ||
		(TEST_FLAG(parts, _actor_part_animation_bit) &&
			(!actor_take(&cursor, end, &state->user_animation_graph, sizeof(state->user_animation_graph)) ||
				!actor_take(&cursor, end, &state->user_animation, sizeof(state->user_animation)) ||
				!actor_take(&cursor, end, &state->user_animation_number, sizeof(state->user_animation_number)) ||
				!actor_take(&cursor, end, &state->user_animation_interpolate,
					sizeof(state->user_animation_interpolate)))) ||
		(TEST_FLAG(parts, _actor_part_run_blindly_bit) &&
			!actor_take(&cursor, end, &state->run_blindly_angle, sizeof(state->run_blindly_angle))) ||
		(TEST_FLAG(parts, _actor_part_camouflage_bit) &&
			!actor_take(&cursor, end, &state->active_camouflage, sizeof(state->active_camouflage))) ||
		(TEST_FLAG(parts, _actor_part_looking_bit) &&
			!actor_take(&cursor, end, state->looking, sizeof(state->looking))) ||
		(TEST_FLAG(parts, _actor_part_facing_bit) &&
			!actor_take(&cursor, end, state->facing, sizeof(state->facing))) ||
		(TEST_FLAG(parts, _actor_part_up_bit) && !actor_take(&cursor, end, &state->up, sizeof(state->up))) ||
		(TEST_FLAG(parts, _actor_part_velocity_bit) &&
			!actor_take(&cursor, end, &state->velocity, sizeof(state->velocity))) ||
		(TEST_FLAG(parts, _actor_part_throttle_bit) &&
			!actor_take(&cursor, end, state->throttle, sizeof(state->throttle))) ||
		(TEST_FLAG(parts, _actor_part_control_bit) &&
			!actor_take(&cursor, end, &state->control_flags, sizeof(state->control_flags))) ||
		(TEST_FLAG(parts, _actor_part_trigger_bit) &&
			!actor_take(&cursor, end, &state->primary_trigger, sizeof(state->primary_trigger))))
	{
		return 0;
	}
	return (word)(cursor - buffer);
}

/* ---------- the actors' states to a client
In the encoding the client takes (network_legacy_machine, the one place
that decides): to an OpenH1 client of an OpenH1 game compact
(_distributed_message_actor_states_compact), as many as the whole
structures' message holds of bytes; to any other the whole structures, as
a Legacy host sends them (_distributed_message_actor_states,
network_legacy.c). */

struct actor_states_out
{
	long machine_index;
	boolean legacy;
	short count;
	word size;
};

/* (an OpenH1 client's: the same bytes as the whole structures' at most) */
static struct
{
	struct distributed_message_header header;
	byte data[DATAGRAM_ENTRIES(struct distributed_actor_state) * sizeof(struct distributed_actor_state)];
} actor_compact_message;

static void actor_states_begin(
	struct actor_states_out *out,
	long machine_index)
{
	out->machine_index = machine_index;
	out->legacy = network_legacy_machine(machine_index);
	out->count = 0;
	out->size = 0;
}

/* a state to the client (its messages sent as they fill): the bytes it
takes */
static word actor_states_add(
	struct actor_states_out *out,
	struct distributed_actor_state const *state)
{
	byte entry[ACTOR_COMPACT_MAXIMUM_SIZE];
	word size;

	if (out->legacy)
		return network_legacy_actor_state_add(out->machine_index, state);
	size = actor_state_compact_write(state, entry);
	if (out->count &&
		(out->size + size > sizeof(actor_compact_message.data) || out->count == MAXIMUM_ENTRIES_PER_MESSAGE))
	{
		distributed_send_to_machine(out->machine_index, &actor_compact_message,
			_distributed_message_actor_states_compact, out->count,
			(word)(sizeof(actor_compact_message.header) + out->size));
		out->count = 0;
		out->size = 0;
	}
	csmemcpy(actor_compact_message.data + out->size, entry, size);
	out->size = (word)(out->size + size);
	out->count++;
	return size;
}

/* the rest of the states to the client sent */
static void actor_states_end(
	struct actor_states_out *out)
{
	if (out->legacy)
		network_legacy_actor_states_end(out->machine_index);
	else if (out->count)
	{
		distributed_send_to_machine(out->machine_index, &actor_compact_message,
			_distributed_message_actor_states_compact, out->count,
			(word)(sizeof(actor_compact_message.header) + out->size));
	}
}

/* ---------- private code */

static void angles_pack(
	real_vector3d const *vector,
	short *angles)
{
	angles[_angle_yaw] = distributed_angle_pack((real)atan2(vector->j, vector->i));
	angles[_angle_pitch] = distributed_angle_pack((real)asin(PIN(vector->k, -1.0f, 1.0f)));
}

static void angles_unpack(
	short const *angles,
	real_vector3d *vector)
{
	real yaw = distributed_angle_unpack(angles[_angle_yaw], FALSE);
	real pitch = distributed_angle_unpack(angles[_angle_pitch], TRUE);

	vector->i = (real)(cos(pitch) * cos(yaw));
	vector->j = (real)(cos(pitch) * sin(yaw));
	vector->k = (real)sin(pitch);
}

/* the difference of two packed angles, the short way round */
static long angle_difference(
	short a,
	short b)
{
	return ABS((long)(short)(word)((word)a - (word)b));
}

/* Host: whether an entry's control is a press against what every client
was last sent (last): the control flags or the animation state changed, the
trigger or a stick moved by a quarter of its reach, or the aim turned by
PRESSED_AIM_STEP. Then last is the entry's. A steady turn or a stick's drift
is no press until it adds up to one. */
static boolean actor_state_pressed(
	struct actor_pressed *last,
	struct distributed_actor_state const *state)
{
	short index;
	boolean pressed = !last->valid || last->control_flags != state->control_flags ||
		last->animation_state != state->animation_state ||
		ABS((long)last->primary_trigger - (long)state->primary_trigger) >= PRESSED_TRIGGER_STEP ||
		(last->primary_trigger == 0) != (state->primary_trigger == 0);

	for (index = 0; index < 3; index++)
	{
		if (ABS((long)last->throttle[index] - (long)state->throttle[index]) >= PRESSED_STICK_STEP)
			pressed = TRUE;
	}
	for (index = 0; index < NUMBER_OF_ANGLES; index++)
	{
		if (angle_difference(last->aiming[index], state->aiming[index]) >= PRESSED_AIM_STEP)
			pressed = TRUE;
	}
	if (!pressed)
		return FALSE;
	last->valid = TRUE;
	last->animation_state = state->animation_state;
	last->primary_trigger = state->primary_trigger;
	last->control_flags = state->control_flags;
	csmemcpy(last->throttle, state->throttle, sizeof(last->throttle));
	csmemcpy(last->aiming, state->aiming, sizeof(last->aiming));
	return TRUE;
}

/* host: the next event's number (never 0, which a client's new entry has
as played) */
static byte next_event_number(
	void)
{
	if (++host_event_number == 0)
		host_event_number = 1;
	return host_event_number;
}

/* the unit's entry, adding one if needed; NULL if the table is full */
static struct host_actor *host_actor_for(
	long unit_index)
{
	short index;

	for (index = 0; index < host_actor_count; index++)
	{
		if (host_actors[index].unit_index == unit_index)
			return &host_actors[index];
	}
	if (host_actor_count >= MAXIMUM_NETWORK_ACTORS)
		return NULL;
	csmemset(&host_actors[host_actor_count], 0, sizeof(host_actors[0]));
	host_actors[host_actor_count].unit_index = unit_index;
	host_actors[host_actor_count].impulse = NONE;
	host_actors[host_actor_count].speech_sound = NONE;
	host_actors[host_actor_count].user_animation_graph = NONE;
	return &host_actors[host_actor_count++];
}

static struct client_actor *client_actor_for(
	long unit_index)
{
	short index;

	for (index = 0; index < client_actor_count; index++)
	{
		if (client_actors[index].unit_index == unit_index)
			return &client_actors[index];
	}
	if (client_actor_count >= MAXIMUM_NETWORK_ACTORS)
		return NULL;
	csmemset(&client_actors[client_actor_count], 0, sizeof(client_actors[0]));
	client_actors[client_actor_count].unit_index = unit_index;
	client_actors[client_actor_count].impulse = NONE;
	client_actors[client_actor_count].speech_sound = NONE;
	client_actors[client_actor_count].user_animation_graph = NONE;
	return &client_actors[client_actor_count++];
}

/* whether the unit exists, is alive, and isn't a player's */
static boolean actor_unit_valid(
	long unit_index)
{
	struct unit_datum *unit;

	if (!object_try_and_get_and_verify_type(unit_index, _object_mask_unit))
		return FALSE;
	unit = unit_get(unit_index);
	return !TEST_FLAG(unit->object.damage_flags, _object_dead_bit) && unit->unit.player_index == NONE;
}

/* host: builds the entry to send for an AI unit */
static void actor_state_from_unit(
	struct host_actor const *actor,
	struct distributed_actor_state *state)
{
	struct unit_datum *unit = unit_get(actor->unit_index);
	struct damage_network_state damage;
	real camouflage = PIN(unit->unit.active_camouflage, 0.0f, 1.0f);

	csmemset(state, 0, sizeof(*state));
	state->unit_index = actor->unit_index;
	state->vehicle_index = NONE;
	state->seat_index = NONE;
	if (unit->object.parent_object_index != NONE && unit->unit.parent_seat_index != NONE)
	{
		SET_FLAG(state->flags, _distributed_actor_rides_bit, TRUE);
		state->vehicle_index = unit->object.parent_object_index;
		state->seat_index = unit->unit.parent_seat_index;
	}
	state->animation_state = (byte)actor->control.animation_state;
	state->aiming_speed = (byte)actor->control.aiming_speed;
	state->primary_trigger = (byte)(long)floor(PIN(actor->control.primary_trigger, 0.0f, 1.0f) * 255.0f + 0.5f);
	state->control_flags = actor->control.control_flags;
	/* for a burst the AI holds the trigger for (unit_persistent_control),
	send the flags and trigger the unit's update produced this tick */
	if (unit->unit.persistent_control_timer > 0)
	{
		state->control_flags = (word)(unit->unit.control_flags & (FLAG(NUMBER_OF_UNIT_CONTROL_FLAGS) - 1));
		state->primary_trigger = (byte)(long)floor(PIN(unit->unit.primary_trigger, 0.0f, 1.0f) * 255.0f + 0.5f);
	}
	state->throttle[0] = (signed char)(long)floor(PIN(actor->control.throttle.i, -1.0f, 1.0f) * 127.0f + 0.5f);
	state->throttle[1] = (signed char)(long)floor(PIN(actor->control.throttle.j, -1.0f, 1.0f) * 127.0f + 0.5f);
	state->throttle[2] = (signed char)(long)floor(PIN(actor->control.throttle.k, -1.0f, 1.0f) * 127.0f + 0.5f);
	angles_pack(&actor->control.facing_vector, state->facing);
	angles_pack(&actor->control.aiming_vector, state->aiming);
	angles_pack(&actor->control.looking_vector, state->looking);
	state->impulse = NO_IMPULSE;
	if (actor->impulse != NONE && actor->impulse_sends > 0)
	{
		state->impulse = (byte)actor->impulse;
		state->impulse_number = actor->impulse_number;
		SET_FLAG(state->flags, _distributed_actor_impulse_aligned_bit, actor->impulse_aligned);
		if (actor->impulse_aligned)
		{
			state->impulse_alignment = distributed_angle_pack(
				(real)atan2(actor->impulse_alignment.j, actor->impulse_alignment.i));
		}
	}
	state->speech_sound = NONE;
	if (actor->speech_sound != NONE && actor->speech_sends > 0)
	{
		state->speech_sound = actor->speech_sound;
		state->speech_number = actor->speech_number;
	}
	state->user_animation_graph = NONE;
	if (actor->user_animation_graph != NONE && actor->user_animation_sends > 0)
	{
		state->user_animation_graph = actor->user_animation_graph;
		state->user_animation = actor->user_animation;
		state->user_animation_interpolate = (byte)actor->user_animation_interpolate;
		state->user_animation_number = actor->user_animation_number;
	}
	if (TEST_FLAG(unit->unit.flags, _unit_running_blindly_bit))
	{
		SET_FLAG(state->flags, _distributed_actor_running_blindly_bit, TRUE);
		state->run_blindly_angle = distributed_angle_pack(unit->unit.run_blindly_angle);
	}
	damage_get_network_state(actor->unit_index, &damage);
	SET_FLAG(state->flags, _distributed_actor_shield_depleted_bit, damage.shield_depleted);
	SET_FLAG(state->flags, _distributed_actor_shield_charging_bit, damage.shield_charging);
	SET_FLAG(state->flags, _distributed_actor_shield_over_charging_bit, damage.shield_over_charging);
	state->body_vitality = distributed_vitality_pack(damage.body_vitality);
	state->shield_vitality = distributed_vitality_pack(damage.shield_vitality);
	SET_FLAG(state->flags, _distributed_actor_camouflaged_bit,
		TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit));
	SET_FLAG(state->flags, _distributed_actor_super_camouflaged_bit,
		TEST_FLAG(unit->unit.flags, _unit_super_camouflaged_bit));
	state->active_camouflage = (byte)(long)floor(camouflage * 255.0f + 0.5f);
	state->team = unit->object.owner_team_index >= 0 && unit->object.owner_team_index < NO_TEAM ?
		(byte)unit->object.owner_team_index : NO_TEAM;
	state->position = unit->object.position;
	distributed_vector_pack(&unit->object.translational_velocity, DISTRIBUTED_VELOCITY_SCALE, &state->velocity);
	distributed_vector_pack(&unit->object.forward, DISTRIBUTED_UNIT_SCALE, &state->forward);
	distributed_vector_pack(&unit->object.up, DISTRIBUTED_UNIT_SCALE, &state->up);
}

/* Client: stores the entry's control and applies its state to the unit.
Returns FALSE for an entry with invalid values. */
static boolean actor_state_apply(
	struct distributed_actor_state const *state,
	long now)
{
	struct client_actor *actor;
	struct unit_datum *unit;
	struct damage_network_state damage;
	real_vector3d velocity;
	real_vector3d forward;
	real_vector3d up;

	if (!distributed_object_index_valid(state->unit_index) || !network_objects_client_has(state->unit_index))
		return FALSE;
	/* (a body the host's unit got back up from: a Flood combat form that
	feigned death, which it does only once its body is at rest. Left dead,
	the ragdoll was dragged about by the host's positions. The host sends
	only living units.) */
	if (object_try_and_get_and_verify_type(state->unit_index, _object_mask_unit) &&
		TEST_FLAG(unit_get(state->unit_index)->object.damage_flags, _object_dead_bit) &&
		TEST_FLAG(unit_get(state->unit_index)->object.flags, _object_at_rest_bit) &&
		unit_get(state->unit_index)->unit.player_index == NONE && state->body_vitality > 0)
	{
		unit_port_resurrect(state->unit_index);
	}
	if (!actor_unit_valid(state->unit_index) ||
		state->animation_state >= NUMBER_OF_UNIT_ANIMATION_STATES ||
		state->aiming_speed >= NUMBER_OF_UNIT_AIMING_SPEEDS ||
		!VALID_FLAGS(state->control_flags, NUMBER_OF_UNIT_CONTROL_FLAGS) ||
		(state->impulse != NO_IMPULSE && state->impulse >= NUMBER_OF_ACTOR_ACTIONS) ||
		(state->speech_sound != NONE && !distributed_tag_of_group(state->speech_sound, SOUND_DEFINITION_TAG)) ||
		(state->user_animation_graph != NONE &&
			!distributed_graph_animation(state->user_animation_graph, state->user_animation)) ||
		!distributed_point_valid(&state->position, UNIT_WORLD_BOUND))
	{
		return FALSE;
	}
	distributed_vector_unpack(&state->velocity, DISTRIBUTED_VELOCITY_SCALE, &velocity);
	distributed_vector_unpack(&state->forward, DISTRIBUTED_UNIT_SCALE, &forward);
	distributed_vector_unpack(&state->up, DISTRIBUTED_UNIT_SCALE, &up);
	if (!distributed_axes_make_valid(&forward, &up))
		return FALSE;
	actor = client_actor_for(state->unit_index);
	if (!actor)
		return TRUE;

	/* the control, applied every tick until the next entry */
	actor->received_time = now;
	actor->control.animation_state = (char)state->animation_state;
	actor->control.aiming_speed = (char)state->aiming_speed;
	actor->control.control_flags = state->control_flags;
	actor->control.weapon_index = NONE;
	actor->control.grenade_index = NONE;
	actor->control.zoom_level = NONE;
	actor->control.throttle.i = (real)state->throttle[0] / 127.0f;
	actor->control.throttle.j = (real)state->throttle[1] / 127.0f;
	actor->control.throttle.k = (real)state->throttle[2] / 127.0f;
	actor->control.primary_trigger = (real)state->primary_trigger / 255.0f;
	angles_unpack(state->facing, &actor->control.facing_vector);
	angles_unpack(state->aiming, &actor->control.aiming_vector);
	angles_unpack(state->looking, &actor->control.looking_vector);
	if (state->impulse != NO_IMPULSE && state->impulse_number != actor->played_impulse_number)
	{
		real yaw = distributed_angle_unpack(state->impulse_alignment, FALSE);

		actor->impulse = state->impulse;
		actor->impulse_number = state->impulse_number;
		actor->impulse_aligned = TEST_FLAG(state->flags, _distributed_actor_impulse_aligned_bit);
		actor->impulse_alignment.i = (real)cos(yaw);
		actor->impulse_alignment.j = (real)sin(yaw);
	}

	if (state->speech_sound != NONE && state->speech_number != actor->played_speech_number)
	{
		actor->speech_sound = state->speech_sound;
		actor->speech_number = state->speech_number;
	}
	if (state->user_animation_graph != NONE && state->user_animation_number != actor->played_user_animation_number)
	{
		actor->user_animation_graph = state->user_animation_graph;
		actor->user_animation = state->user_animation;
		actor->user_animation_interpolate = state->user_animation_interpolate != 0;
		actor->user_animation_number = state->user_animation_number;
	}

	/* the state: seat, shields and health, camouflage, running blindly, and
	position (a unit in a vehicle goes where the vehicle goes) */
	unit = unit_get(state->unit_index);
	if (TEST_FLAG(state->flags, _distributed_actor_rides_bit))
	{
		if (unit->object.parent_object_index != state->vehicle_index ||
			unit->unit.parent_seat_index != state->seat_index)
		{
			network_objects_set_seat(state->unit_index, state->vehicle_index, state->seat_index);
		}
	}
	else if (unit->object.parent_object_index != NONE)
	{
		network_objects_set_seat(state->unit_index, NONE, NONE);
	}
	damage_get_network_state(state->unit_index, &damage);
	damage.shield_depleted = TEST_FLAG(state->flags, _distributed_actor_shield_depleted_bit);
	damage.shield_charging = TEST_FLAG(state->flags, _distributed_actor_shield_charging_bit);
	damage.shield_over_charging = TEST_FLAG(state->flags, _distributed_actor_shield_over_charging_bit);
	damage.body_vitality = distributed_vitality_unpack(state->body_vitality);
	damage.shield_vitality = distributed_vitality_unpack(state->shield_vitality);
	damage_set_network_state(state->unit_index, &damage);
	SET_FLAG(unit->unit.flags, _unit_active_camouflaged_bit,
		TEST_FLAG(state->flags, _distributed_actor_camouflaged_bit));
	SET_FLAG(unit->unit.flags, _unit_super_camouflaged_bit,
		TEST_FLAG(state->flags, _distributed_actor_super_camouflaged_bit));
	unit->unit.active_camouflage = (real)state->active_camouflage / 255.0f;
	/* (the game's 10 teams, game_allegiance.c) */
	if (state->team != NO_TEAM && state->team < 10)
		unit->object.owner_team_index = state->team;
	SET_FLAG(unit->unit.flags, _unit_running_blindly_bit,
		TEST_FLAG(state->flags, _distributed_actor_running_blindly_bit));
	if (TEST_FLAG(state->flags, _distributed_actor_running_blindly_bit))
		unit->unit.run_blindly_angle = distributed_angle_unpack(state->run_blindly_angle, FALSE);
	/* vehicles are placed only by the host's object states (network_objects.c) */
	if (unit->object.parent_object_index == NONE && unit->object.type != _object_type_vehicle)
	{
		real dx = state->position.x - unit->object.position.x;
		real dy = state->position.y - unit->object.position.y;
		real dz = state->position.z - unit->object.position.z;

		if (dx * dx + dy * dy + dz * dz > REMOTE_CORRECTION_TOLERANCE * REMOTE_CORRECTION_TOLERANCE &&
			network_objects_reconcile(state->unit_index, &state->position, &forward, &up, &velocity, NULL,
				REMOTE_BLEND_DISTANCE, _network_vehicle_write_state))
		{
			distributed_count_correction();
		}
	}
	return TRUE;
}

/* Client: plays a sound at the unit's head, as unit_dialogue_update starts
a unit's speech. */
static void speech_play(
	long unit_index,
	long sound_definition_index)
{
	struct object_marker marker;
	real_point3d position = *global_origin3d;
	real_vector3d forward = *global_forward3d;
	short node_index = 0;

	if (object_get_marker_by_name(unit_index, "head", &marker, 1))
	{
		node_index = marker.node_index;
		position = marker.node_matrix.position;
		forward = marker.node_matrix.forward;
	}
	object_impulse_sound_new(unit_index, sound_definition_index, node_index, &position, &forward, 1.0f);
}

/* ---------- public code */

void network_actors_new_game(
	void)
{
	host_actor_count = 0;
	client_actor_count = 0;
	actors_bytes_sent = 0;
	actors_pressed_bytes_sent = 0;
	actors_aimed_bytes_sent = 0;
}

void network_actors_bytes_sent(
	long *all,
	long *pressed,
	long *aimed)
{
	*all = actors_bytes_sent;
	*pressed = actors_pressed_bytes_sent;
	*aimed = actors_aimed_bytes_sent;
	actors_bytes_sent = 0;
	actors_pressed_bytes_sent = 0;
	actors_aimed_bytes_sent = 0;
}

/* host: unit_control calls this when an actor drives its unit */
void network_actors_note_control(
	long unit_index,
	struct unit_control_data const *control_data)
{
	struct host_actor *actor;

	if (game_connection() != _game_connection_network_server)
		return;
	actor = host_actor_for(unit_index);
	if (!actor)
		return;
	actor->noted = TRUE;
	actor->control = *control_data;
}

/* host: an impulse or action for an AI unit, sent in the next few entries */
static void note_action(
	long unit_index,
	short action,
	real_vector2d const *alignment_vector)
{
	struct host_actor *actor;

	if (game_connection() != _game_connection_network_server)
		return;
	actor = host_actor_for(unit_index);
	if (!actor)
		return;
	actor->impulse = action;
	actor->impulse_sends = IMPULSE_REPEAT_TICKS;
	actor->impulse_number = next_event_number();
	actor->impulse_aligned = alignment_vector != NULL;
	if (alignment_vector)
		actor->impulse_alignment = *alignment_vector;
}

/* host: unit_start_animation_impulse calls this for an AI unit */
void network_actors_note_impulse(
	long unit_index,
	short animation_impulse,
	real_vector2d const *alignment_vector)
{
	note_action(unit_index, animation_impulse, alignment_vector);
}

/* host: unit_melee_attack_begin calls this when an AI unit starts a melee attack */
void network_actors_note_melee(
	long unit_index,
	real_vector2d const *alignment_vector)
{
	note_action(unit_index, _actor_action_melee, alignment_vector);
}

/* host: unit_leap_begin calls this when an AI unit leaps */
void network_actors_note_leap(
	long unit_index,
	real_vector2d const *alignment_vector)
{
	note_action(unit_index, _actor_action_leap, alignment_vector);
}

/* host: unit_dialogue_update calls this when an AI unit starts saying
something (not a pain or death sound, which clients play themselves) */
void network_actors_note_speech(
	long unit_index,
	long sound_definition_index)
{
	struct host_actor *actor;

	if (game_connection() != _game_connection_network_server)
		return;
	actor = host_actor_for(unit_index);
	if (!actor)
		return;
	actor->speech_sound = sound_definition_index;
	actor->speech_sends = IMPULSE_REPEAT_TICKS;
	actor->speech_number = next_event_number();
}

/* host: unit_start_user_animation calls this when an AI unit starts a
custom animation (co-op's events carry them in co-op) */
void network_actors_note_user_animation(
	long unit_index,
	long animation_graph_index,
	short animation_index,
	boolean interpolate)
{
	struct host_actor *actor;

	if (game_connection() != _game_connection_network_server || network_coop_active())
		return;
	actor = host_actor_for(unit_index);
	if (!actor)
		return;
	actor->user_animation_graph = animation_graph_index;
	actor->user_animation = animation_index;
	actor->user_animation_interpolate = interpolate;
	actor->user_animation_sends = IMPULSE_REPEAT_TICKS;
	actor->user_animation_number = next_event_number();
}

/* host: the damage of the AI units taking some, to every client (and once
more when it ends, so a flare goes) */
static void host_send_damage(
	void)
{
	struct distributed_actor_damage_message message;
	short count = 0;
	short index;

	for (index = 0; index < host_actor_count; index++)
	{
		struct host_actor *actor = &host_actors[index];
		struct damage_network_state damage;
		boolean damaged;

		if (!actor_unit_valid(actor->unit_index))
			continue;
		damage_get_network_state(actor->unit_index, &damage);
		damaged = damage.current_shield_damage > 0.0f || damage.recent_shield_damage > 0.0f ||
			damage.current_body_damage > 0.0f || damage.recent_body_damage > 0.0f;
		if (!damaged && !actor->damaged)
			continue;
		actor->damaged = damaged;
		message.units[count].unit_index = actor->unit_index;
		message.units[count].current_shield_damage = distributed_vitality_pack(damage.current_shield_damage);
		message.units[count].recent_shield_damage = distributed_vitality_pack(damage.recent_shield_damage);
		message.units[count].current_body_damage = distributed_vitality_pack(damage.current_body_damage);
		message.units[count].recent_body_damage = distributed_vitality_pack(damage.recent_body_damage);
		if (++count == MAXIMUM_ENTRIES_PER_MESSAGE)
		{
			distributed_send(&message, _distributed_message_actor_damage, count,
				(word)(sizeof(message.header) + count * sizeof(message.units[0])), _distributed_to_clients);
			count = 0;
		}
	}
	if (count)
	{
		distributed_send(&message, _distributed_message_actor_damage, count,
			(word)(sizeof(message.header) + count * sizeof(message.units[0])), _distributed_to_clients);
	}
}

/* Host, after each tick: sends each client the AI units driven this tick.
Units near the client's players go every tick, others less often. A unit
with a fresh impulse, action, speech or animation goes to everyone in each
of the next IMPULSE_REPEAT_TICKS ticks, and one starting a one-tick action
goes that tick. In an OpenH1 game a unit whose control made a press
(actor_state_pressed) goes to every OpenH1 client that tick, and one aiming
at an OpenH1 client's player, or that player's aim on it, goes to that client
every tick: a far sniper's or a Banshee's shot leaves it where it is aimed
as the host fires it, not some ticks later. A unit no actor drove this tick
is dropped from the table, unless it is fleeing or seated: fleeing units move
without their actor's control (dropped, a unit running blindly stood running
in place on the clients), and an idle seated actor still needs its seat sent
to the clients. */
void network_actors_host_tick(
	void)
{
	static struct distributed_actor_state states[MAXIMUM_NETWORK_ACTORS];
	/* sent to every client this tick (_urgent_to_all), or every OpenH1
	client (_urgent_pressed), whatever its period */
	static byte urgent[MAXIMUM_NETWORK_ACTORS];
	static real_vector3d aims[MAXIMUM_NETWORK_ACTORS];
	enum
	{
		_urgent_none,
		_urgent_to_all,
		_urgent_pressed,
	};
	boolean openh1 = network_standard_hosting();
	long machine_indices[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	short machine_count = distributed_client_machines(machine_indices, HALO_PORT_MAXIMUM_NETWORK_MACHINES);
	long now = game_time_get();
	short state_count = 0;
	short index;
	short machine_number;

	for (index = 0; index < host_actor_count; index++)
	{
		struct host_actor *actor = &host_actors[index];

		if (!actor_unit_valid(actor->unit_index) ||
			(!actor->noted && !TEST_FLAG(unit_get(actor->unit_index)->unit.flags, _unit_running_blindly_bit) &&
			(!vehicle_riding_enabled() || unit_get(actor->unit_index)->object.parent_object_index == NONE ||
			unit_get(actor->unit_index)->unit.parent_seat_index == NONE)))
		{
			host_actors[index--] = host_actors[--host_actor_count];
			continue;
		}
		actor_state_from_unit(actor, &states[state_count]);
		urgent[state_count] = states[state_count].impulse != NO_IMPULSE || states[state_count].speech_sound != NONE ||
			states[state_count].user_animation_graph != NONE ||
			(states[state_count].control_flags & ~actor->sent_control_flags & ONE_SHOT_CONTROL_FLAGS) != 0 ?
			_urgent_to_all : _urgent_none;
		/* (the press told against what went to every client, so one that
		went to every client for its own reason is noted too) */
		if (openh1 && actor_state_pressed(&actor->pressed, &states[state_count]) &&
			urgent[state_count] == _urgent_none)
		{
			urgent[state_count] = _urgent_pressed;
		}
		aims[state_count] = actor->control.aiming_vector;
		actor->sent_control_flags = states[state_count].control_flags;
		state_count++;
		actor->noted = FALSE;
		if (actor->impulse_sends > 0)
			actor->impulse_sends--;
		if (actor->speech_sends > 0)
			actor->speech_sends--;
		if (actor->user_animation_sends > 0)
			actor->user_animation_sends--;
	}
	host_send_damage();
	for (machine_number = 0; machine_number < machine_count; machine_number++)
	{
		long machine_index = machine_indices[machine_number];
		boolean machine_openh1 = openh1 && network_standard_machine_is_openh1(machine_index);
		struct actor_states_out out;

		actor_states_begin(&out, machine_index);
		for (index = 0; index < state_count; index++)
		{
			struct distributed_actor_state const *state = &states[index];
			short period = network_objects_send_period(machine_index, &state->position);

			if (urgent[index] != _urgent_to_all && period > 1 &&
				(now + DATUM_INDEX_TO_ABSOLUTE_INDEX(state->unit_index)) % period != 0)
			{
				if (machine_openh1 && urgent[index] == _urgent_pressed)
					actors_pressed_bytes_sent += sizeof(*state);
				else if (machine_openh1 &&
					network_objects_aims_with_machine(machine_index, &state->position, &aims[index]))
				{
					actors_aimed_bytes_sent += sizeof(*state);
				}
				else
					continue;
			}
			actors_bytes_sent += actor_states_add(&out, state);
		}
		actor_states_end(&out);
	}
}

/* client: the host's AI units' compact states (an OpenH1 game's) */
void network_actors_handle_compact_states(
	void const *data,
	void const *end,
	short count)
{
	byte const *cursor = data;
	long now = game_time_get();
	short index;

	for (index = 0; index < count; index++)
	{
		struct distributed_actor_state state;
		word size = actor_state_compact_read(cursor, end, &state);

		if (!size)
			break;
		cursor += size;
		actor_state_apply(&state, now);
	}
}

word network_actors_compact_minimum_size(
	void)
{
	return ACTOR_COMPACT_MINIMUM_SIZE;
}

word network_actors_damage_entry_size(
	void)
{
	return sizeof(struct distributed_actor_damage);
}

/* client: the damage the host's AI units are taking */
void network_actors_handle_damage(
	void const *entries,
	short count)
{
	struct distributed_actor_damage const *units = entries;
	short index;

	for (index = 0; index < count; index++)
	{
		struct damage_network_state damage;

		if (!distributed_object_index_valid(units[index].unit_index) ||
			!network_objects_client_has(units[index].unit_index) || !actor_unit_valid(units[index].unit_index))
		{
			continue;
		}
		damage_get_network_state(units[index].unit_index, &damage);
		damage.current_shield_damage = distributed_vitality_unpack(units[index].current_shield_damage);
		damage.recent_shield_damage = distributed_vitality_unpack(units[index].recent_shield_damage);
		damage.current_body_damage = distributed_vitality_unpack(units[index].current_body_damage);
		damage.recent_body_damage = distributed_vitality_unpack(units[index].recent_body_damage);
		damage_set_network_state(units[index].unit_index, &damage);
	}
}

word network_actors_entry_size(
	void)
{
	return sizeof(struct distributed_actor_state);
}

/* client: entries from the host */
void network_actors_handle_states(
	void const *entries,
	short count)
{
	struct distributed_actor_state const *states = entries;
	long now = game_time_get();
	short index;

	for (index = 0; index < count; index++)
		actor_state_apply(&states[index], now);
}

/* Client: called at the point in the tick where the host runs the AI.
Drives each AI unit with the last control the host sent. */
void network_actors_drive(
	void)
{
	long now = game_time_get();
	short index;

	for (index = 0; index < client_actor_count; index++)
	{
		struct client_actor *actor = &client_actors[index];

		if (!actor_unit_valid(actor->unit_index) || now - actor->received_time > CONTROL_HELD_TICKS)
		{
			if (actor->driven && object_try_and_get_and_verify_type(actor->unit_index, _object_mask_unit))
				unit_set_actively_controlled(actor->unit_index, FALSE);
			client_actors[index--] = client_actors[--client_actor_count];
			continue;
		}
		/* vehicles follow the host's transform instead (network_objects.c) */
		if (object_get(actor->unit_index)->object.type == _object_type_vehicle)
			continue;
		if (!actor->driven)
		{
			unit_set_actively_controlled(actor->unit_index, TRUE);
			actor->driven = TRUE;
		}
		unit_control(actor->unit_index, &actor->control);
		if (actor->impulse != NONE && actor->impulse_number != actor->played_impulse_number)
		{
			real_vector2d *alignment = actor->impulse_aligned ? &actor->impulse_alignment : NULL;

			if (actor->impulse == _actor_action_melee)
				unit_melee_attack_begin(actor->unit_index, FALSE, alignment);
			else if (actor->impulse == _actor_action_leap)
				unit_leap_begin(actor->unit_index, alignment);
			else
				unit_start_animation_impulse(actor->unit_index, actor->impulse, alignment);
			actor->played_impulse_number = actor->impulse_number;
			actor->impulse = NONE;
		}
		if (actor->speech_sound != NONE)
		{
			speech_play(actor->unit_index, actor->speech_sound);
			actor->played_speech_number = actor->speech_number;
			actor->speech_sound = NONE;
		}
		if (actor->user_animation_graph != NONE)
		{
			unit_port_play_user_animation(actor->unit_index, actor->user_animation_graph, actor->user_animation,
				actor->user_animation_interpolate, 0);
			actor->played_user_animation_number = actor->user_animation_number;
			actor->user_animation_graph = NONE;
		}
	}
}
