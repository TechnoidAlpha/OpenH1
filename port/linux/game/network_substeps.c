/*
NETWORK_SUBSTEPS.C

An OpenH1 game's sub-step inputs (NETCODE.md, "Sub-step inputs"). At 60
and 120 Hz a tick runs 2 or 4 sub-steps (sim_rate.h), and a local player's
unit takes a new reading of the controller in each sub-step after the
tick's first (player_control.c): its held controls, movement, trigger and
facing. Every machine replays the other players from the input it is given,
so each machine needs each player's sub-steps too, not only their ticks:

- Recorded: what a local player's unit was given in each sub-step past the
  first (network_substeps_local_given), as the unit has it after the
  sub-step's reading: its held controls, throttle, trigger, and its facing
  if its aim turned.
- Up: a client sends its own players' sub-steps of the tick just run with
  its input of that tick (_distributed_message_player_substeps, unreliable,
  in the same datagram), keyed by the client's tick, at full precision.
- Replayed: in the next tick that runs the input of a tick, each sub-step
  past the first gives the player's unit that sub-step's controls, before
  the units and bipeds move (network_substeps_replay). A record is taken
  only if it is of the tick whose input the tick took (keys latched as the
  tick begins: player_queues_new.c's update_queues_distributed_input_key),
  and only for a unit no local player controls. Without one (lost, or a
  player whose sub-steps changed nothing), the tick's input holds through
  its sub-steps, as at 30.
- Relayed: the host sends each client, with the players' input it relays
  (_distributed_message_relayed_actions), the sub-steps its tick ran for
  them (_distributed_message_relayed_substeps), keyed by the host's update,
  as compact as the relayed input (angles in 16 bits, the sticks of 127, the
  trigger of 255), and none for a player whose sub-steps changed nothing.

- Taken at the tick (NETCODE.md, "Input at its tick"): a client sends its
  input of a tick as the tick's first sub-step ends, with its live controls
  (below), and the host's tick takes it, with the client's prediction of the
  tick before, as it begins (network_distributed.c). The tick then runs
  that input: each later sub-step gives the player's unit the client's
  controls of that sub-step of it as they came (host_live_apply), or holds
  the latest; a tick that took no new input holds the latest it has.

Nothing here runs in a game of one sub-step a tick (a Legacy game, an OpenH1
game at 30), nor sends anything: those games' traffic is as it was.
Anything another machine sends is checked: its player (the sender's own, to
the host), its count of sub-steps (the game's), and every number finite and
within a controller's reach.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "game/player_queues_new.h"
#include "units/units.h"
#include "networking/network_game_globals.h"
#include "network_distributed.h"
#include "network_substeps.h"
#include "sim_rate.h"

#include <math.h>
#include <string.h>

/* source/game/player_queues_new.c */
long update_queues_distributed_input_key(long absolute_player_index);

/* ---------- constants */

enum
{
	MAXIMUM_LATER_SUBSTEPS = MAXIMUM_SIM_SUBSTEPS - 1,
	/* a relayed record's bytes on the wire: its player and count, then each
	later sub-step's held controls (2), whether it turned (1), sticks (2),
	trigger (1) and facing (4) */
	RELAYED_SUBSTEP_SIZE = 10,
	RELAYED_RECORD_MAXIMUM_SIZE = 2 + RELAYED_SUBSTEP_SIZE * MAXIMUM_LATER_SUBSTEPS,
	/* a relayed message's entries, at most, in bytes (well within a datagram) */
	RELAYED_MESSAGE_ENTRIES_SIZE = 640,
	/* (the host) the ticks of each client player's live controls it keeps,
	by their sub-step (a power of two): the input its tick runs, and the
	ticks after it that came meanwhile */
	HOST_LIVE_TICKS = 4,
	/* (the host) how far a player's sticks (of 127) or trigger (of 255)
	move before their controls go to a client its plan does not send the
	player's every control to: a push or a pull, not a stick's drift
	(network_substeps_live_far_due) */
	LIVE_CONTROL_FAR_STICK_STEP = 32,
	LIVE_CONTROL_FAR_TRIGGER_STEP = 64,
};

/* the unit controls a sub-step's reading changes (player_control.c's
PLAYER_CONTROL_SUBSTEP_HELD_FLAGS) */
#define SUBSTEP_HELD_FLAGS ( \
	FLAG(_unit_control_crouch_modifier_bit) | \
	FLAG(_unit_control_jump_bit) | \
	FLAG(_unit_control_weapon_primary_trigger_bit) | \
	FLAG(_unit_control_weapon_secondary_trigger_bit) | \
	FLAG(_unit_control_throw_grenade_bit))

/* ---------- structures */

/* what a unit was given in one sub-step past its tick's first */
struct substep_control
{
	unsigned long held_flags;
	real_vector2d throttle;
	real primary_trigger;
	real_euler_angles2d facing;
	boolean turned;
};

/* a player's sub-steps of one tick: of the tick (or host update) key, for
count sub-steps, those past the first */
struct substep_record
{
	boolean valid;
	long key;
	short count;
	long unit_index;
	/* (being recorded: the sub-steps given, a bit each) */
	word given;
	struct substep_control controls[MAXIMUM_LATER_SUBSTEPS];
};

/* on the wire, to the host: a client player's sub-steps of its tick */
struct distributed_substep_input_control
{
	word held_flags;
	byte turned;
	byte pad;
	real throttle_i;
	real throttle_j;
	real primary_trigger;
	real yaw;
	real pitch;
};

struct distributed_substep_input
{
	byte player_index;
	byte substep_count;
	word pad;
	long tick;
	struct distributed_substep_input_control controls[MAXIMUM_LATER_SUBSTEPS];
};

/* a player's controls at the end of one of its sub-steps, sent as each runs
(network_substeps_substep_end): on the wire, to the host */
struct distributed_live_control
{
	byte player_index;
	byte substep_index;
	byte substep_count;
	byte pad;
	long tick;
	word held_flags;
	word pad2;
	real throttle_i;
	real throttle_j;
	real primary_trigger;
	real aim_i;
	real aim_j;
	real aim_k;
};

typedef char distributed_live_control_size_assert[sizeof(struct distributed_live_control) == 36 ? 1 : -1];

/* ... as this machine has them: of the player's tick (a client's player's) or
the host's update (the host's own player's), the sub-step and their count */
struct live_control
{
	boolean valid;
	boolean host_update;
	/* (the host) not yet sent on */
	boolean pending;
	long key;
	short substep;
	short count;
	unsigned long held_flags;
	real_vector2d throttle;
	real primary_trigger;
	real_vector3d aim;
};

typedef char forward_maximum_size_assert[
	1 + RELAYED_SUBSTEP_SIZE * MAXIMUM_LATER_SUBSTEPS == NETWORK_SUBSTEPS_FORWARD_MAXIMUM_SIZE ? 1 : -1];
typedef char distributed_substep_input_size_assert[
	sizeof(struct distributed_substep_input) == 8 + 24 * MAXIMUM_LATER_SUBSTEPS ? 1 : -1];

/* ---------- globals */

static struct
{
	/* the latest heard of each player (by absolute index): the host's of a
	client's players, keyed by the client's tick; a client's of the others,
	by the host's update */
	struct substep_record heard[MAXIMUM_TRACKED_PLAYERS];
	/* ... and a client's of the other clients' players, as the host sent
	them on with their input as they came (network_distributed.c's forwarded
	input), keyed by their client's tick */
	struct substep_record forwarded[MAXIMUM_TRACKED_PLAYERS];
	/* this tick's: the record replayed, and what the tick ran (a local
	player's as given, a remote one's as replayed) */
	struct substep_record replaying[MAXIMUM_TRACKED_PLAYERS];
	struct substep_record ran[MAXIMUM_TRACKED_PLAYERS];
	/* what each player's unit had as the tick's sub-steps began: a tick
	whose sub-steps changed none of it has nothing to send */
	struct substep_control start[MAXIMUM_TRACKED_PLAYERS];
	boolean start_valid[MAXIMUM_TRACKED_PLAYERS];
	/* the controls of a replayed unit that went down in the running sub-step */
	long pressed_unit[MAXIMUM_TRACKED_PLAYERS];
	unsigned long pressed_flags[MAXIMUM_TRACKED_PLAYERS];
	/* each player's latest controls as their sub-steps ran: the host's of its
	clients' players and its own, to send on; a client's of the others, as
	the host sent them on. And the key of the input each player's tick took,
	and whether a forwarded one (a client's tick, not the host's update) */
	struct live_control live[MAXIMUM_TRACKED_PLAYERS];
	long tick_key[MAXIMUM_TRACKED_PLAYERS];
	boolean tick_forwarded[MAXIMUM_TRACKED_PLAYERS];
	/* this machine's own players' controls at their last sub-step's end */
	struct live_control own[MAXIMUM_LOCAL_PLAYERS];
	/* (the host) each player's controls as its own sub-steps ran them, keyed
	by its update, as they were at their latest press (and what of them
	makes one): to send on to the clients its plan does not send the
	player's every control to (network_substeps_live_far_due) */
	struct live_control ran_live[MAXIMUM_TRACKED_PLAYERS];
	struct live_control_far ran_far[MAXIMUM_TRACKED_PLAYERS];
	/* (the host) each client player's live controls of its last ticks, by
	the tick (key & (HOST_LIVE_TICKS - 1)) and the sub-step (given: a bit
	each); the input its tick before took, and whether this tick's is new
	(it runs that input's sub-steps) or the same (it holds the latest) */
	struct host_live_tick
	{
		long key;
		word given;
		struct live_control controls[MAXIMUM_SIM_SUBSTEPS];
	} host_live[MAXIMUM_TRACKED_PLAYERS][HOST_LIVE_TICKS];
	long host_previous_key[MAXIMUM_TRACKED_PLAYERS];
	boolean host_fresh[MAXIMUM_TRACKED_PLAYERS];
	long bytes_sent;
} network_substeps_globals;

/* ---------- private code */

/* the unit's sub-step controls as it has them now */
static void substep_control_from_unit(
	struct unit_datum const *unit,
	struct substep_control *control)
{
	control->held_flags = unit->unit.control_flags & SUBSTEP_HELD_FLAGS;
	control->throttle.i = unit->unit.throttle.i;
	control->throttle.j = unit->unit.throttle.j;
	control->primary_trigger = unit->unit.primary_trigger;
}

/* whether a tick's record changed nothing of what its unit began with */
static boolean substep_record_unchanged(
	struct substep_record const *record,
	struct substep_control const *start)
{
	short index;

	for (index = 0; index < record->count - 1; index++)
	{
		struct substep_control const *control = &record->controls[index];

		if (control->turned || control->held_flags != start->held_flags ||
			control->throttle.i != start->throttle.i || control->throttle.j != start->throttle.j ||
			control->primary_trigger != start->primary_trigger)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* whether a record being made has every sub-step past the first */
static boolean substep_record_complete(
	struct substep_record const *record)
{
	return record->count > 1 && record->count <= MAXIMUM_SIM_SUBSTEPS &&
		record->given == (word)(((1 << record->count) - 1) & ~1);
}

/* the game's sub-steps a tick, as the agreed rate has them */
static short network_substeps_count(
	void)
{
	return sim_substep_count();
}

/* a heard record kept (of heard, or of forwarded), if newer than the one kept */
static void substep_record_heard(
	struct substep_record *records,
	short player_index,
	struct substep_record const *record)
{
	struct substep_record *kept = &records[player_index];

	if (kept->valid && record->key <= kept->key &&
		/* (a key far behind: the sender's game began again) */
		kept->key - record->key < TICKS_PER_SECOND * 10)
	{
		return;
	}
	*kept = *record;
	kept->valid = TRUE;
}

static real substep_pin(
	real value,
	real minimum,
	real maximum)
{
	if (!valid_real(value))
		return 0.0f;
	return PIN(value, minimum, maximum);
}

/* (the host) a client's players' sub-steps */
static void network_substeps_handle_inputs(
	long machine_index,
	struct distributed_substep_input const *inputs,
	short count)
{
	short index;

	for (index = 0; index < count; index++)
	{
		struct distributed_substep_input input;
		struct substep_record record;
		short substep;

		csmemcpy(&input, &inputs[index], sizeof(input));
		if (input.player_index >= MAXIMUM_TRACKED_PLAYERS ||
			!distributed_machine_has_player(machine_index, input.player_index) ||
			input.substep_count != network_substeps_count() || input.substep_count < 2)
		{
			continue;
		}
		csmemset(&record, 0, sizeof(record));
		record.key = input.tick;
		record.count = input.substep_count;
		record.unit_index = NONE;
		for (substep = 0; substep < record.count - 1; substep++)
		{
			struct distributed_substep_input_control const *wire = &input.controls[substep];
			struct substep_control *control = &record.controls[substep];

			control->held_flags = wire->held_flags & SUBSTEP_HELD_FLAGS;
			control->turned = wire->turned != 0;
			control->throttle.i = substep_pin(wire->throttle_i, -1.0f, 1.0f);
			control->throttle.j = substep_pin(wire->throttle_j, -1.0f, 1.0f);
			control->primary_trigger = substep_pin(wire->primary_trigger, 0.0f, 1.0f);
			control->facing.yaw = substep_pin(wire->yaw, -4.0f * _pi, 4.0f * _pi);
			control->facing.pitch = substep_pin(wire->pitch, -_pi, _pi);
		}
		substep_record_heard(network_substeps_globals.heard, input.player_index, &record);
	}
}

/* a record's later sub-steps' bytes, as compact as the relayed input
(RELAYED_SUBSTEP_SIZE each) */
static byte *substep_controls_write(
	struct substep_record const *record,
	byte *cursor)
{
	short index;

	for (index = 0; index < record->count - 1; index++)
	{
		struct substep_control const *control = &record->controls[index];
		word held = (word)control->held_flags;
		real throttle_i = PIN(control->throttle.i, -1.0f, 1.0f);
		real throttle_j = PIN(control->throttle.j, -1.0f, 1.0f);
		real trigger = PIN(control->primary_trigger, 0.0f, 1.0f);
		short yaw = distributed_angle_pack(control->facing.yaw);
		short pitch = distributed_angle_pack(control->facing.pitch);

		csmemcpy(cursor, &held, sizeof(held));
		cursor += sizeof(held);
		*cursor++ = (byte)control->turned;
		*cursor++ = (byte)(signed char)(long)floor(throttle_i * 127.0f + 0.5f);
		*cursor++ = (byte)(signed char)(long)floor(throttle_j * 127.0f + 0.5f);
		*cursor++ = (byte)(long)floor(trigger * 255.0f + 0.5f);
		csmemcpy(cursor, &yaw, sizeof(yaw));
		cursor += sizeof(yaw);
		csmemcpy(cursor, &pitch, sizeof(pitch));
		cursor += sizeof(pitch);
	}
	return cursor;
}

/* ... and read (record->count set), no further than end: the bytes past
them, NULL for too few */
static byte const *substep_controls_read(
	struct substep_record *record,
	byte const *data,
	byte const *end)
{
	short substep;

	if (record->count < 2 || record->count > MAXIMUM_SIM_SUBSTEPS ||
		end - data < (long)(RELAYED_SUBSTEP_SIZE * (record->count - 1)))
	{
		return NULL;
	}
	for (substep = 0; substep < record->count - 1; substep++)
	{
		struct substep_control *control = &record->controls[substep];
		word held;
		short yaw, pitch;

		csmemcpy(&held, data, sizeof(held));
		control->held_flags = held & SUBSTEP_HELD_FLAGS;
		control->turned = data[2] != 0;
		control->throttle.i = PIN((real)(signed char)data[3] / 127.0f, -1.0f, 1.0f);
		control->throttle.j = PIN((real)(signed char)data[4] / 127.0f, -1.0f, 1.0f);
		control->primary_trigger = (real)data[5] / 255.0f;
		csmemcpy(&yaw, data + 6, sizeof(yaw));
		csmemcpy(&pitch, data + 8, sizeof(pitch));
		control->facing.yaw = distributed_angle_unpack(yaw, FALSE);
		control->facing.pitch = distributed_angle_unpack(pitch, TRUE);
		data += RELAYED_SUBSTEP_SIZE;
	}
	return data;
}

/* a relayed record's bytes (at most RELAYED_RECORD_MAXIMUM_SIZE) */
static word relayed_record_write(
	short player_index,
	struct substep_record const *record,
	byte *buffer)
{
	byte *cursor = buffer;

	*cursor++ = (byte)player_index;
	*cursor++ = (byte)record->count;
	cursor = substep_controls_write(record, cursor);
	return (word)(cursor - buffer);
}

/* (a client) the host's relayed records, of its update */
static void network_substeps_handle_relayed(
	byte const *data,
	byte const *end,
	short count)
{
	long update_number;
	short index;

	if (end - data < (long)sizeof(update_number))
		return;
	csmemcpy(&update_number, data, sizeof(update_number));
	data += sizeof(update_number);
	for (index = 0; index < count; index++)
	{
		struct substep_record record;
		short player_index;

		if (end - data < 2)
			return;
		player_index = data[0];
		csmemset(&record, 0, sizeof(record));
		record.key = update_number;
		record.count = data[1];
		record.unit_index = NONE;
		data = substep_controls_read(&record, data + 2, end);
		if (!data)
			return;
		/* (a count other than the game's, or a player not tracked: passed
		over, its bytes read) */
		if (player_index < MAXIMUM_TRACKED_PLAYERS && record.count == network_substeps_count())
			substep_record_heard(network_substeps_globals.heard, player_index, &record);
	}
}

/* whether a live control of key and sub-step is later than one of the
other's */
static boolean live_control_later(
	long key,
	short substep,
	long other_key,
	short other_substep)
{
	return key > other_key || (key == other_key && substep > other_substep);
}

/* a unit's controls now, as a live control's */
static void live_control_from_unit(
	struct unit_datum const *unit,
	struct live_control *live)
{
	live->held_flags = unit->unit.control_flags & SUBSTEP_HELD_FLAGS;
	live->throttle.i = unit->unit.throttle.i;
	live->throttle.j = unit->unit.throttle.j;
	live->primary_trigger = unit->unit.primary_trigger;
	live->aim = unit->unit.desired_aiming_vector;
}

static boolean live_control_same(
	struct live_control const *a,
	struct live_control const *b)
{
	return a->held_flags == b->held_flags && a->throttle.i == b->throttle.i && a->throttle.j == b->throttle.j &&
		a->primary_trigger == b->primary_trigger && a->aim.i == b->aim.i && a->aim.j == b->aim.j &&
		a->aim.k == b->aim.k;
}

/* what of a live control makes a press: its held controls, and its sticks
and trigger as they go on the wire */
static void live_control_far_from(
	struct live_control const *live,
	struct live_control_far *far)
{
	far->valid = TRUE;
	far->held_flags = (word)live->held_flags;
	far->throttle_i = (short)floor(PIN(live->throttle.i, -1.0f, 1.0f) * 127.0f + 0.5f);
	far->throttle_j = (short)floor(PIN(live->throttle.j, -1.0f, 1.0f) * 127.0f + 0.5f);
	far->primary_trigger = (short)floor(PIN(live->primary_trigger, 0.0f, 1.0f) * 255.0f + 0.5f);
}

/* whether b is a press after a: its held controls changed, or a stick or
the trigger moved far (not a stick's drift) */
static boolean live_control_far_pressed(
	struct live_control_far const *a,
	struct live_control_far const *b)
{
	return !a->valid || a->held_flags != b->held_flags ||
		ABS(b->throttle_i - a->throttle_i) >= LIVE_CONTROL_FAR_STICK_STEP ||
		ABS(b->throttle_j - a->throttle_j) >= LIVE_CONTROL_FAR_STICK_STEP ||
		ABS(b->primary_trigger - a->primary_trigger) >= LIVE_CONTROL_FAR_TRIGGER_STEP;
}

/* a heard live control kept, if later than the one kept (or of the other
kind of key) */
static boolean live_control_heard(
	short player_index,
	struct live_control const *live)
{
	struct live_control *kept = &network_substeps_globals.live[player_index];

	if (kept->valid && kept->host_update == live->host_update &&
		!live_control_later(live->key, live->substep, kept->key, kept->substep) &&
		/* (a key far behind: its sender's game began again) */
		live->key >= kept->key - TICKS_PER_SECOND * 10)
	{
		return FALSE;
	}
	*kept = *live;
	kept->valid = TRUE;
	return TRUE;
}

/* a live control given the player's unit at this sub-step (NONE: the tick's
own, its actions just given), a press in it seen there */
static void live_control_give(
	short player_index,
	struct player_datum const *player,
	struct live_control const *live,
	short substep_index)
{
	struct unit_datum *unit = unit_get(player->unit_index);
	unsigned long before = unit->unit.control_flags;

	unit->unit.control_flags = (before & ~SUBSTEP_HELD_FLAGS) | (live->held_flags & SUBSTEP_HELD_FLAGS);
	unit->unit.throttle.i = live->throttle.i;
	unit->unit.throttle.j = live->throttle.j;
	unit->unit.throttle.k = 0.0f;
	unit->unit.primary_trigger = live->primary_trigger;
	unit->unit.desired_aiming_vector = live->aim;
	unit->unit.desired_facing_vector = live->aim;
	unit->unit.desired_looking_vector = live->aim;
	if (substep_index != NONE)
	{
		network_substeps_globals.pressed_unit[player_index] = player->unit_index;
		network_substeps_globals.pressed_flags[player_index] = live->held_flags & ~before & SUBSTEP_HELD_FLAGS;
	}
}

/* (a client) the player's latest live control, given its unit at this
sub-step when it is later than the sub-step of the input the tick took
(sub-step NONE: the tick's own, its actions just given), or of that input's
own tick at all when no record of it came (a client's input goes as its
tick's first sub-step ends, with its controls of that sub-step, so the rest
of its tick's controls come after it, and its record after them); whether
it was */
static boolean live_control_apply(
	short player_index,
	short substep_index)
{
	struct live_control const *live = &network_substeps_globals.live[player_index];
	struct player_datum *player;
	long key = network_substeps_globals.tick_key[player_index];

	if (game_connection() != _game_connection_network_client || !live->valid || key == NONE ||
		live->count != network_substeps_count() ||
		/* (of the kind of key the tick's input was: a client's player's
		while its forwarded input drives it, the host's own player's) */
		live->host_update == network_substeps_globals.tick_forwarded[player_index] ||
		(!live_control_later(live->key, live->substep, key, substep_index == NONE ? 0 : substep_index) &&
			(live->key != key || substep_index == NONE || network_substeps_globals.replaying[player_index].valid)))
	{
		return FALSE;
	}
	player = distributed_player(player_index);
	if (!player || player->local_player_index != NONE || distributed_living_unit(player) == NONE)
		return FALSE;
	live_control_give(player_index, player, live, substep_index);
	return TRUE;
}

/* (the host) a client's player whose input its tick took as it began
(distributed_prediction_at_tick_start): in a tick that runs a new input of
its (distributed_host_run_time), that input's controls of each sub-step as
the client sent them as it ran (the latest of it at or before the sub-step;
none: the latest of the input it took, or held), unless its record came; in a tick that took none new (its input late, or
lost), the latest it has, at its tick's start too, as a client gives the
other players' (live_control_apply). The sub-step's, the tick's record for
the clients it relays the player to. Whether one was given. */
static boolean host_live_apply(
	short player_index,
	short substep_index)
{
	long key = network_substeps_globals.tick_key[player_index];
	struct live_control const *live = NULL;
	struct player_datum *player;

	if (game_connection() != _game_connection_network_server || key == NONE ||
		!distributed_prediction_at_tick_start(player_index))
	{
		return FALSE;
	}
	if (network_substeps_globals.host_fresh[player_index])
	{
		struct host_live_tick const *tick =
			&network_substeps_globals.host_live[player_index][key & (HOST_LIVE_TICKS - 1)];
		short substep;

		if (network_substeps_globals.replaying[player_index].valid)
			return FALSE;
		for (substep = substep_index == NONE ? 0 : substep_index; tick->key == key && substep >= 0 && !live;
			substep--)
		{
			if (TEST_FLAG(tick->given, substep))
				live = &tick->controls[substep];
		}
		/* (none of the tick it runs come by the sub-step, in a tick that runs
		the client's tick after the input it took (its prediction came before
		that tick's input): the unit was given that input's actions as the
		tick began, though the tick before ran its later sub-steps' controls.
		Held, the latest of that input or after it, before the tick run) */
		if (!live)
		{
			long input_key = update_queues_distributed_input_key(player_index);
			long earlier;

			for (earlier = key - 1; input_key != NONE && earlier >= input_key && earlier > key - HOST_LIVE_TICKS &&
				!live; earlier--)
			{
				tick = &network_substeps_globals.host_live[player_index][earlier & (HOST_LIVE_TICKS - 1)];
				for (substep = MAXIMUM_SIM_SUBSTEPS - 1; tick->key == earlier && substep >= 0 && !live; substep--)
				{
					if (TEST_FLAG(tick->given, substep))
						live = &tick->controls[substep];
				}
			}
		}
	}
	else
	{
		live = &network_substeps_globals.live[player_index];
		if (!live->valid || live->host_update ||
			!live_control_later(live->key, live->substep, key, substep_index == NONE ? 0 : substep_index))
		{
			live = NULL;
		}
	}
	if (!live || live->count != network_substeps_count())
		return FALSE;
	player = distributed_player(player_index);
	if (!player || player->local_player_index != NONE || distributed_living_unit(player) == NONE)
		return FALSE;
	{
		struct unit_datum *unit = unit_get(player->unit_index);
		real_vector3d aim = unit->unit.desired_aiming_vector;
		struct substep_record *ran = &network_substeps_globals.ran[player_index];

		live_control_give(player_index, player, live, substep_index);
		/* (what the tick ran, for the host to relay: the facing of an aim
		turned on foot, where the aim is the facing's) */
		if (substep_index >= 1 && substep_index < ran->count && ran->unit_index == player->unit_index)
		{
			struct substep_control *control = &ran->controls[substep_index - 1];

			substep_control_from_unit(unit, control);
			control->turned = unit->object.parent_object_index == NONE &&
				(aim.i != live->aim.i || aim.j != live->aim.j || aim.k != live->aim.k);
			if (control->turned)
				euler_angles2d_from_vector3d(&control->facing, &live->aim);
			ran->given |= (word)FLAG(substep_index);
		}
	}
	return TRUE;
}

/* (the host) a client's players' live controls */
static void network_substeps_handle_live(
	long machine_index,
	struct distributed_live_control const *entries,
	short count)
{
	short index;

	for (index = 0; index < count; index++)
	{
		struct distributed_live_control entry;
		struct live_control live;

		csmemcpy(&entry, &entries[index], sizeof(entry));
		if (entry.player_index >= MAXIMUM_TRACKED_PLAYERS ||
			!distributed_machine_has_player(machine_index, entry.player_index) ||
			entry.substep_count != network_substeps_count() || entry.substep_index >= entry.substep_count)
		{
			continue;
		}
		csmemset(&live, 0, sizeof(live));
		live.key = entry.tick;
		live.substep = entry.substep_index;
		live.count = entry.substep_count;
		live.held_flags = entry.held_flags & SUBSTEP_HELD_FLAGS;
		live.throttle.i = substep_pin(entry.throttle_i, -1.0f, 1.0f);
		live.throttle.j = substep_pin(entry.throttle_j, -1.0f, 1.0f);
		live.primary_trigger = substep_pin(entry.primary_trigger, 0.0f, 1.0f);
		live.aim.i = substep_pin(entry.aim_i, -1.0f, 1.0f);
		live.aim.j = substep_pin(entry.aim_j, -1.0f, 1.0f);
		live.aim.k = substep_pin(entry.aim_k, -1.0f, 1.0f);
		/* (a unit vector, or none taken) */
		if (normalize3d(&live.aim) == 0.0f)
			continue;
		/* (each sub-step's of the latest ticks, for the host's own run of
		them: host_live_apply) */
		{
			struct host_live_tick *tick =
				&network_substeps_globals.host_live[entry.player_index][live.key & (HOST_LIVE_TICKS - 1)];

			if (tick->key != live.key)
			{
				tick->key = live.key;
				tick->given = 0;
			}
			tick->controls[live.substep] = live;
			tick->controls[live.substep].valid = TRUE;
			tick->given |= (word)FLAG(live.substep);
		}
		live.pending = TRUE;
		live_control_heard(entry.player_index, &live);
	}
}

/* (a client) the live controls the host sent on: entries of
LIVE_CONTROL_FORWARD_SIZE (network_substeps_live_forward_write) */
static void network_substeps_handle_forwarded_live(
	byte const *data,
	byte const *end,
	short count)
{
	short index;

	for (index = 0; index < count && end - data >= LIVE_CONTROL_FORWARD_SIZE; index++, data += LIVE_CONTROL_FORWARD_SIZE)
	{
		struct live_control live;
		struct distributed_vector aim;
		short player_index = data[0];
		word held;

		csmemset(&live, 0, sizeof(live));
		live.substep = data[1];
		live.count = data[2];
		live.host_update = (data[3] & 1) != 0;
		csmemcpy(&live.key, data + 4, sizeof(live.key));
		csmemcpy(&held, data + 8, sizeof(held));
		live.held_flags = held & SUBSTEP_HELD_FLAGS;
		live.throttle.i = PIN((real)(signed char)data[10] / 127.0f, -1.0f, 1.0f);
		live.throttle.j = PIN((real)(signed char)data[11] / 127.0f, -1.0f, 1.0f);
		live.primary_trigger = (real)data[12] / 255.0f;
		csmemcpy(&aim, data + 13, sizeof(aim));
		distributed_unit_vector_unpack(&aim, &live.aim);
		if (player_index >= MAXIMUM_TRACKED_PLAYERS || live.count != network_substeps_count() ||
			live.substep >= live.count ||
			(distributed_player(player_index) && distributed_player(player_index)->local_player_index != NONE) ||
			normalize3d(&live.aim) == 0.0f)
		{
			continue;
		}
		live_control_heard(player_index, &live);
	}
}

static void network_substeps_substep_end(short substep_index, short substep_count, real seconds);

/* ---------- public code */

boolean network_substeps_active(
	void)
{
	short connection = game_connection();

	return (connection == _game_connection_network_client || connection == _game_connection_network_server) &&
		!sim_rate_follows_setting() && sim_substep_count() > 1;
}

void network_substeps_new_game(
	void)
{
	short player_index;

	csmemset(&network_substeps_globals, 0, sizeof(network_substeps_globals));
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		short tick;

		network_substeps_globals.pressed_unit[player_index] = NONE;
		network_substeps_globals.tick_key[player_index] = NONE;
		network_substeps_globals.host_previous_key[player_index] = NONE;
		for (tick = 0; tick < HOST_LIVE_TICKS; tick++)
			network_substeps_globals.host_live[player_index][tick].key = NONE;
	}
	sim_substep_register(_sim_substep_network, network_substeps_substep_end);
}

void network_substeps_tick_begun(
	short substep_count)
{
	boolean active = network_substeps_active();
	short player_index;

	/* (the inputs this tick took, as the tests' reports have them: a tick's
	sub-steps may be spread over frames, between which newer inputs come) */
	network_distributed_tick_begun();
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct substep_record *replaying = &network_substeps_globals.replaying[player_index];
		struct substep_record *ran = &network_substeps_globals.ran[player_index];
		struct substep_record const *heard = update_client_input_forwarded(player_index) ?
			&network_substeps_globals.forwarded[player_index] : &network_substeps_globals.heard[player_index];
		struct player_datum *player = active ? distributed_player(player_index) : NULL;
		long unit_index = player ? player->unit_index : NONE;

		replaying->valid = FALSE;
		ran->valid = FALSE;
		ran->given = 0;
		ran->count = substep_count;
		ran->unit_index = unit_index;
		network_substeps_globals.start_valid[player_index] = FALSE;
		network_substeps_globals.pressed_unit[player_index] = NONE;
		network_substeps_globals.tick_key[player_index] = game_connection() == _game_connection_network_server ?
			distributed_host_run_time(player_index) : update_queues_distributed_input_key(player_index);
		network_substeps_globals.tick_forwarded[player_index] = update_client_input_forwarded(player_index);
		if (unit_index == NONE)
			continue;
		substep_control_from_unit(unit_get(unit_index), &network_substeps_globals.start[player_index]);
		network_substeps_globals.start_valid[player_index] = TRUE;
		/* (a remote player's: the record of the input this tick took. The
		host's of a player whose input its tick takes as it begins, as the
		actions are given (network_substeps_actions_given): a tick that took
		none new holds the latest controls, not the record of the input
		before) */
		if (player->local_player_index == NONE && heard->valid && heard->count == substep_count &&
			heard->key == network_substeps_globals.tick_key[player_index] &&
			!distributed_prediction_at_tick_start(player_index))
		{
			*replaying = *heard;
			replaying->unit_index = unit_index;
		}
	}
}

void network_substeps_replay(
	short substep_index,
	short substep_count)
{
	short player_index;

	if (!network_substeps_active() || substep_index < 1 || substep_index >= substep_count)
		return;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		struct substep_record const *replaying = &network_substeps_globals.replaying[player_index];
		struct substep_record *ran = &network_substeps_globals.ran[player_index];
		struct substep_control const *control;
		struct player_datum *player;
		struct unit_datum *unit;
		unsigned long before;

		network_substeps_globals.pressed_unit[player_index] = NONE;
		/* (a client: another player's controls of a later sub-step than this
		one of the input the tick took, as they ran, when the host sent them
		on as they came: the latest) */
		if (live_control_apply(player_index, substep_index))
			continue;
		/* (the host, of a client's player whose input it took as its tick
		began: the record of that input, if it came since, else the controls
		of the sub-step as they came; neither, held) */
		if (distributed_prediction_at_tick_start(player_index) && !replaying->valid)
		{
			struct substep_record const *heard = &network_substeps_globals.heard[player_index];

			player = distributed_player(player_index);
			if (network_substeps_globals.host_fresh[player_index] && player && player->local_player_index == NONE &&
				player->unit_index != NONE && heard->valid && heard->count == substep_count &&
				heard->key == network_substeps_globals.tick_key[player_index])
			{
				network_substeps_globals.replaying[player_index] = *heard;
				network_substeps_globals.replaying[player_index].unit_index = player->unit_index;
			}
			else
			{
				if (!host_live_apply(player_index, substep_index) && player &&
					player->local_player_index == NONE && distributed_living_unit(player) != NONE &&
					ran->unit_index == player->unit_index && substep_index < ran->count)
				{
					/* (held: as the sub-step before) */
					substep_control_from_unit(unit_get(player->unit_index), &ran->controls[substep_index - 1]);
					ran->controls[substep_index - 1].turned = FALSE;
					ran->given |= (word)FLAG(substep_index);
				}
				continue;
			}
		}
		if (!replaying->valid || replaying->count != substep_count)
			continue;
		player = distributed_player(player_index);
		if (!player || player->unit_index != replaying->unit_index || player->local_player_index != NONE)
			continue;
		unit = unit_get(player->unit_index);
		control = &replaying->controls[substep_index - 1];
		before = unit->unit.control_flags;
		unit->unit.control_flags = (before & ~SUBSTEP_HELD_FLAGS) | (control->held_flags & SUBSTEP_HELD_FLAGS);
		unit->unit.throttle.i = control->throttle.i;
		unit->unit.throttle.j = control->throttle.j;
		unit->unit.throttle.k = 0.0f;
		unit->unit.primary_trigger = control->primary_trigger;
		if (control->turned)
		{
			real_vector3d aiming_vector;

			player_aiming_vector_from_facing(DATUM_INDEX_NEW(player_index, player->identifier), &aiming_vector,
				&control->facing);
			unit->unit.desired_aiming_vector = aiming_vector;
			unit->unit.desired_facing_vector = aiming_vector;
			unit->unit.desired_looking_vector = aiming_vector;
		}
		network_substeps_globals.pressed_unit[player_index] = player->unit_index;
		network_substeps_globals.pressed_flags[player_index] = control->held_flags & ~before & SUBSTEP_HELD_FLAGS;
		/* (what the tick ran, for the host to relay) */
		ran->controls[substep_index - 1] = *control;
		ran->given |= (word)FLAG(substep_index);
	}
}

void network_substeps_local_given(
	long player_index,
	long unit_index,
	short substep_index,
	short substep_count,
	real_euler_angles2d const *facing,
	boolean turned)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	struct substep_record *ran;
	struct substep_control *control;

	if (!network_substeps_active() || absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED_PLAYERS ||
		substep_index < 1 || substep_index >= substep_count || substep_count > MAXIMUM_SIM_SUBSTEPS)
	{
		return;
	}
	ran = &network_substeps_globals.ran[absolute_index];
	if (ran->unit_index != unit_index || ran->count != substep_count)
		return;
	control = &ran->controls[substep_index - 1];
	substep_control_from_unit(unit_get(unit_index), control);
	control->facing = *facing;
	control->turned = turned;
	ran->given |= (word)FLAG(substep_index);
}

unsigned long network_substeps_pressed_flags(
	long unit_index)
{
	short player_index;

	if (unit_index == NONE)
		return 0;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		if (network_substeps_globals.pressed_unit[player_index] == unit_index)
			return network_substeps_globals.pressed_flags[player_index];
	}
	return 0;
}

void network_substeps_client_send(
	void)
{
	struct
	{
		struct distributed_message_header header;
		struct distributed_substep_input inputs[MAXIMUM_LOCAL_PLAYERS];
	} message;
	short count = 0;
	short local_player_index;

	if (!network_substeps_active())
		return;
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);
		long absolute_index = player_index != NONE ? DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;
		struct distributed_substep_input *input = &message.inputs[count];
		struct substep_record const *ran;
		struct player_action action;
		unsigned short control_flags[DISTRIBUTED_INPUT_HISTORY];
		long tick;
		short substep;

		if (absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED_PLAYERS)
			continue;
		ran = &network_substeps_globals.ran[absolute_index];
		/* (with the input of the same tick, which goes with it) */
		if (!substep_record_complete(ran) || !network_substeps_globals.start_valid[absolute_index] ||
			substep_record_unchanged(ran, &network_substeps_globals.start[absolute_index]) ||
			!update_client_distributed_input(local_player_index, &tick, &action, control_flags))
		{
			continue;
		}
		csmemset(input, 0, sizeof(*input));
		input->player_index = distributed_player_to_byte(player_index);
		input->substep_count = (byte)ran->count;
		input->tick = tick;
		for (substep = 0; substep < ran->count - 1; substep++)
		{
			struct substep_control const *control = &ran->controls[substep];

			input->controls[substep].held_flags = (word)control->held_flags;
			input->controls[substep].turned = (byte)control->turned;
			input->controls[substep].throttle_i = control->throttle.i;
			input->controls[substep].throttle_j = control->throttle.j;
			input->controls[substep].primary_trigger = control->primary_trigger;
			input->controls[substep].yaw = control->facing.yaw;
			input->controls[substep].pitch = control->facing.pitch;
		}
		count++;
	}
	if (count)
	{
		word size = (word)(sizeof(message.header) + count * sizeof(struct distributed_substep_input));

		distributed_send(&message, _distributed_message_player_substeps, count, size, _distributed_to_host);
		network_substeps_globals.bytes_sent += size;
	}
}

void network_substeps_host_send(
	long machine_index,
	long update_number,
	short const *player_indices,
	short count)
{
	struct
	{
		struct distributed_message_header header;
		long update_number;
		byte entries[RELAYED_MESSAGE_ENTRIES_SIZE];
	} message;
	word used = 0;
	short entries = 0;
	short index;

	if (!network_substeps_active() || update_number == NONE)
		return;
	for (index = 0; index <= count; index++)
	{
		short player_index = index < count ? player_indices[index] : NONE;
		struct substep_record const *ran = player_index != NONE ? &network_substeps_globals.ran[player_index] : NULL;

		/* (a full message, or the last, sent) */
		if (entries && (!ran || used + RELAYED_RECORD_MAXIMUM_SIZE > RELAYED_MESSAGE_ENTRIES_SIZE))
		{
			word size = (word)(sizeof(message.header) + sizeof(message.update_number) + used);

			message.update_number = update_number;
			distributed_send_to_machine(machine_index, &message, _distributed_message_relayed_substeps, entries, size);
			network_substeps_globals.bytes_sent += size;
			used = 0;
			entries = 0;
		}
		if (!ran || player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS || !substep_record_complete(ran) ||
			!network_substeps_globals.start_valid[player_index] ||
			substep_record_unchanged(ran, &network_substeps_globals.start[player_index]))
		{
			continue;
		}
		used = (word)(used + relayed_record_write(player_index, ran, message.entries + used));
		entries++;
	}
}

word network_substeps_forward_write(
	short player_index,
	long key,
	byte *buffer)
{
	struct substep_record const *heard;

	buffer[0] = 0;
	if (!network_substeps_active() || player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return 1;
	heard = &network_substeps_globals.heard[player_index];
	if (!heard->valid || heard->key != key || heard->count != network_substeps_count() || heard->count < 2)
		return 1;
	buffer[0] = (byte)heard->count;
	return (word)(substep_controls_write(heard, buffer + 1) - buffer);
}

word network_substeps_forward_read(
	short player_index,
	long key,
	byte const *data,
	byte const *end)
{
	struct substep_record record;
	byte const *after;

	if (end - data < 1)
		return 0;
	if (!data[0])
		return 1;
	csmemset(&record, 0, sizeof(record));
	record.key = key;
	record.count = data[0];
	record.unit_index = NONE;
	after = substep_controls_read(&record, data + 1, end);
	if (!after)
		return 0;
	if (network_substeps_active() && player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS &&
		record.count == network_substeps_count())
	{
		substep_record_heard(network_substeps_globals.forwarded, player_index, &record);
	}
	return (word)(after - data);
}

boolean network_substeps_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_player_substeps || header.type == _distributed_message_relayed_substeps ||
		header.type == _distributed_message_player_live_controls ||
		header.type == _distributed_message_forwarded_live_controls;
}

void network_substeps_handle_message(
	long machine_index,
	word const *message,
	word size)
{
	struct distributed_message_header header;
	byte const *entries = (byte const *)message + sizeof(header);
	byte const *end = (byte const *)message + size;

	if (size < sizeof(header) || !game_in_progress() || !network_substeps_active())
		return;
	csmemcpy(&header, message, sizeof(header));
	if (header.type == _distributed_message_player_substeps)
	{
		/* (the host's, from a client machine) */
		if (game_connection() != _game_connection_network_server || machine_index == NONE ||
			size < sizeof(header) + header.count * sizeof(struct distributed_substep_input))
		{
			return;
		}
		network_substeps_handle_inputs(machine_index, (struct distributed_substep_input const *)entries,
			header.count);
	}
	else if (header.type == _distributed_message_relayed_substeps)
	{
		/* (a client's, from the host) */
		if (game_connection() != _game_connection_network_client || machine_index != NONE)
			return;
		network_substeps_handle_relayed(entries, end, header.count);
	}
	else if (header.type == _distributed_message_player_live_controls)
	{
		/* (the host's, from a client machine) */
		if (game_connection() != _game_connection_network_server || machine_index == NONE ||
			size < sizeof(header) + header.count * sizeof(struct distributed_live_control))
		{
			return;
		}
		network_substeps_handle_live(machine_index, (struct distributed_live_control const *)entries, header.count);
	}
	else if (header.type == _distributed_message_forwarded_live_controls)
	{
		/* (a client's, from the host) */
		if (game_connection() != _game_connection_network_client || machine_index != NONE)
			return;
		network_substeps_handle_forwarded_live(entries, end, header.count);
	}
}

void network_substeps_actions_given(
	void)
{
	short player_index;

	if (!network_substeps_active())
		return;
	if (game_connection() == _game_connection_network_server)
	{
		/* (the predictions of the tick before the input the tick takes,
		before the input runs: network_distributed.c) */
		network_distributed_host_actions_given();
		for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
		{
			long key = distributed_host_run_time(player_index);

			network_substeps_globals.tick_key[player_index] = key;
			network_substeps_globals.tick_forwarded[player_index] = FALSE;
			network_substeps_globals.host_fresh[player_index] =
				key != NONE && key != network_substeps_globals.host_previous_key[player_index];
			network_substeps_globals.host_previous_key[player_index] = key;
			/* (the record of a new input this tick runs, come already) */
			if (network_substeps_globals.host_fresh[player_index] &&
				distributed_prediction_at_tick_start(player_index))
			{
				struct substep_record const *heard = &network_substeps_globals.heard[player_index];
				struct substep_record *replaying = &network_substeps_globals.replaying[player_index];
				struct player_datum *player = distributed_player(player_index);

				if (player && player->local_player_index == NONE && player->unit_index != NONE &&
					player->unit_index == network_substeps_globals.ran[player_index].unit_index &&
					heard->valid && heard->count == network_substeps_globals.ran[player_index].count &&
					heard->key == key)
				{
					*replaying = *heard;
					replaying->unit_index = player->unit_index;
				}
			}
			host_live_apply(player_index, NONE);
		}
		return;
	}
	if (game_connection() != _game_connection_network_client)
		return;
	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		network_substeps_globals.tick_key[player_index] = update_queues_distributed_input_key(player_index);
		network_substeps_globals.tick_forwarded[player_index] = update_client_input_forwarded(player_index);
		live_control_apply(player_index, NONE);
	}
}

/* (_sim_substep_network, at each sub-step's end) a client's own players'
controls sent to the host at once where they changed since the sub-step
before, and its hits; the host's own players' sent on to its clients */
static void network_substeps_substep_end(
	short substep_index,
	short substep_count,
	real seconds)
{
	struct
	{
		struct distributed_message_header header;
		struct distributed_live_control entries[MAXIMUM_LOCAL_PLAYERS];
	} message;
	short connection = game_connection();
	short count = 0;
	short local_player_index;

	(void)seconds;
	if (!network_substeps_active() || substep_count > MAXIMUM_SIM_SUBSTEPS)
		return;
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);
		short absolute_index = player_index != NONE ? (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index) : NONE;
		struct live_control *own = &network_substeps_globals.own[local_player_index];
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		long unit_index = player ? distributed_living_unit(player) : NONE;
		struct live_control now;

		if (unit_index == NONE || absolute_index < 0 || absolute_index >= MAXIMUM_TRACKED_PLAYERS)
		{
			own->valid = FALSE;
			continue;
		}
		csmemset(&now, 0, sizeof(now));
		live_control_from_unit(unit_get(unit_index), &now);
		now.valid = TRUE;
		now.substep = substep_index;
		now.count = substep_count;
		if (connection == _game_connection_network_server)
		{
			now.host_update = TRUE;
			now.key = update_server_ticked_update_number();
		}
		else
		{
			now.key = game_time_get();
		}
		/* (unchanged since the sub-step before: nothing new to tell) */
		if (own->valid && live_control_same(own, &now))
		{
			own->key = now.key;
			own->substep = now.substep;
			continue;
		}
		*own = now;
		if (now.key == NONE)
			continue;
		if (connection == _game_connection_network_server)
		{
			now.pending = TRUE;
			live_control_heard(absolute_index, &now);
		}
		else
		{
			struct distributed_live_control *entry = &message.entries[count++];

			csmemset(entry, 0, sizeof(*entry));
			entry->player_index = (byte)absolute_index;
			entry->substep_index = (byte)substep_index;
			entry->substep_count = (byte)substep_count;
			entry->tick = now.key;
			entry->held_flags = (word)now.held_flags;
			entry->throttle_i = now.throttle.i;
			entry->throttle_j = now.throttle.j;
			entry->primary_trigger = now.primary_trigger;
			entry->aim_i = now.aim.i;
			entry->aim_j = now.aim.j;
			entry->aim_k = now.aim.k;
		}
	}
	if (connection == _game_connection_network_client)
	{
		if (count)
		{
			word size = (word)(sizeof(message.header) + count * sizeof(struct distributed_live_control));

			distributed_send(&message, _distributed_message_player_live_controls, count, size, _distributed_to_host);
			network_substeps_globals.bytes_sent += size;
		}
		/* (an OpenH1 game's input of the tick, as its first sub-step ends,
		which the host's tick takes as it begins: network_distributed.c) */
		if (substep_index == 0)
			network_distributed_client_substep_inputs();
		/* (where its copies of the others are, which the host's states of
		its sub-steps name: network_distributed.c) */
		network_distributed_client_substep_ended(substep_index, substep_count);
		/* (the hits its sub-step made, at once: network_damage.c) */
		network_damage_client_tick();
		/* (now, but for the last sub-step's, which the tick's own messages
		follow at once) */
		if (substep_index < substep_count - 1)
			distributed_flush_to_host();
	}
	else if (connection == _game_connection_network_server)
	{
		short player_index;

		/* (every player's controls as the sub-step ran them, where they
		make a press, for the clients it sends the player less often) */
		for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
		{
			struct live_control *ran = &network_substeps_globals.ran_live[player_index];
			struct live_control_far *far = &network_substeps_globals.ran_far[player_index];
			struct player_datum *player = distributed_player(player_index);
			long unit_index = player ? distributed_living_unit(player) : NONE;
			struct live_control_far pressed;
			struct live_control now;

			if (unit_index == NONE || update_server_ticked_update_number() == NONE)
			{
				ran->valid = FALSE;
				far->valid = FALSE;
				continue;
			}
			csmemset(&now, 0, sizeof(now));
			live_control_from_unit(unit_get(unit_index), &now);
			live_control_far_from(&now, &pressed);
			if (ran->valid && !live_control_far_pressed(far, &pressed))
				continue;
			now.valid = TRUE;
			now.pending = TRUE;
			now.host_update = TRUE;
			now.key = update_server_ticked_update_number();
			now.substep = substep_index;
			now.count = substep_count;
			*ran = now;
			*far = pressed;
		}
		/* (the states of the players near each client, or aimed at, of
		each sub-step but the last, with the controls) */
		network_distributed_host_substep_ended(substep_index, substep_count);
	}
}

/* a live control's bytes as the host sends it on (0: none to send) */
static word live_control_forward_write(
	short player_index,
	struct live_control const *live,
	byte *buffer)
{
	struct distributed_vector aim;
	word held;

	if (!live->valid || !live->pending)
		return 0;
	held = (word)live->held_flags;
	distributed_vector_pack(&live->aim, DISTRIBUTED_UNIT_SCALE, &aim);
	buffer[0] = (byte)player_index;
	buffer[1] = (byte)live->substep;
	buffer[2] = (byte)live->count;
	buffer[3] = (byte)(live->host_update ? 1 : 0);
	csmemcpy(buffer + 4, &live->key, sizeof(live->key));
	csmemcpy(buffer + 8, &held, sizeof(held));
	buffer[10] = (byte)(signed char)(long)floor(PIN(live->throttle.i, -1.0f, 1.0f) * 127.0f + 0.5f);
	buffer[11] = (byte)(signed char)(long)floor(PIN(live->throttle.j, -1.0f, 1.0f) * 127.0f + 0.5f);
	buffer[12] = (byte)(long)floor(PIN(live->primary_trigger, 0.0f, 1.0f) * 255.0f + 0.5f);
	csmemcpy(buffer + 13, &aim, sizeof(aim));
	return LIVE_CONTROL_FORWARD_SIZE;
}

word network_substeps_live_forward_write(
	short player_index,
	byte *buffer)
{
	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return 0;
	return live_control_forward_write(player_index, &network_substeps_globals.live[player_index], buffer);
}

boolean network_substeps_live_own(
	short player_index)
{
	return player_index >= 0 && player_index < MAXIMUM_TRACKED_PLAYERS &&
		network_substeps_globals.live[player_index].valid;
}

word network_substeps_live_far_write(
	short player_index,
	byte *buffer)
{
	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS)
		return 0;
	return live_control_forward_write(player_index, &network_substeps_globals.ran_live[player_index], buffer);
}

boolean network_substeps_live_far_due(
	short player_index,
	struct live_control_far *last)
{
	struct live_control const *live;
	struct live_control_far now;

	if (player_index < 0 || player_index >= MAXIMUM_TRACKED_PLAYERS || !last)
		return FALSE;
	live = &network_substeps_globals.ran_live[player_index];
	if (!live->valid || !live->pending)
		return FALSE;
	live_control_far_from(live, &now);
	if (!live_control_far_pressed(last, &now))
		return FALSE;
	*last = now;
	return TRUE;
}

void network_substeps_live_forwarded(
	void)
{
	short player_index;

	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		network_substeps_globals.live[player_index].pending = FALSE;
		network_substeps_globals.ran_live[player_index].pending = FALSE;
	}
}

boolean network_substeps_live_pending(
	void)
{
	short player_index;

	for (player_index = 0; player_index < MAXIMUM_TRACKED_PLAYERS; player_index++)
	{
		if (network_substeps_globals.live[player_index].pending ||
			network_substeps_globals.ran_live[player_index].pending)
		{
			return TRUE;
		}
	}
	return FALSE;
}

long network_substeps_forwarded_key(
	short player_index)
{
	if (game_connection() != _game_connection_network_client || player_index < 0 ||
		player_index >= MAXIMUM_TRACKED_PLAYERS || !network_substeps_globals.tick_forwarded[player_index])
	{
		return NONE;
	}
	return network_substeps_globals.tick_key[player_index];
}

long network_substeps_bytes_sent(
	void)
{
	long bytes = network_substeps_globals.bytes_sent;

	network_substeps_globals.bytes_sent = 0;
	return bytes;
}
