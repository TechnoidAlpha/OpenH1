/*
NETWORK_LEGACY.C

The Legacy encodings of the host's per-tick streams (network_legacy.h,
port/linux/NETCODE.md, "The Legacy encodings"): network version 44's, which
OpenCE reads. Nothing here may change a byte: the golden tests
(tools/harness/tests/test_legacy_golden.py) hold each encoder to the bytes
recorded for fixed states, and tools/network_format.py to the structures.
OpenH1's own encodings live beside the streams they serve
(network_distributed.c's distributed_unit_difference_write, network_actors.c's
actor_state_compact_write).
*/

/* ---------- headers */

#include "cseries.h"
#include "game/players.h"
#include "network_distributed.h"
#include "network_legacy.h"
#include "network_standard.h"
#include "vehicle_riding.h"

#include <math.h>

/* ---------- constants */

enum
{
	MAXIMUM_ENTRIES_PER_MESSAGE = 64,
};

/* ---------- structures */

struct distributed_actor_state_message
{
	struct distributed_message_header header;
	struct distributed_actor_state states[MAXIMUM_ENTRIES_PER_MESSAGE];
};

/* ---------- globals */

/* (the host) the actors' states being sent the machine
(network_legacy_actor_state_add) */
static struct distributed_actor_state_message legacy_actor_message;
static short legacy_actor_count;

/* ---------- private code */

static byte *legacy_put(
	byte *cursor,
	void const *data,
	short size)
{
	csmemcpy(cursor, data, size);
	return cursor + size;
}

static boolean legacy_take(
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

static void legacy_actor_states_send(
	long machine_index)
{
	distributed_send_to_machine(machine_index, &legacy_actor_message, _distributed_message_actor_states,
		legacy_actor_count,
		(word)(sizeof(legacy_actor_message.header) + legacy_actor_count * sizeof(struct distributed_actor_state)));
	legacy_actor_count = 0;
}

/* ---------- public code */

boolean network_legacy_machine(
	long machine_index)
{
	return !network_standard_hosting() || !network_standard_machine_is_openh1(machine_index);
}

/* ---------- the players' tick states */

/* the state's bytes on the wire (at most DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE):
its player, flags and parts, then (alive) its unit, the vehicle and seat it
rides, (placed) where it is, how fast it moves and which way it faces,
(predicted) the client's tick, its shields and health, the damage they
show, its powerups and camouflage, or (dead) who killed it; what is its
default is left out (distributed_unit_state_read). The predicted tick only
where predicted: to its own client, and in an OpenH1 game of several
sub-steps a tick to the other OpenH1 clients. */
word distributed_unit_state_write(
	struct distributed_unit_state const *state,
	boolean predicted,
	byte *buffer)
{
	byte flags = state->flags;
	byte parts = 0;
	byte *cursor = buffer + DISTRIBUTED_UNIT_STATE_MINIMUM_SIZE;

	if (!predicted)
		SET_FLAG(flags, _distributed_unit_predicted_bit, FALSE);
	if (TEST_FLAG(flags, _distributed_unit_alive_bit))
	{
		cursor = legacy_put(cursor, &state->unit_index, sizeof(state->unit_index));
		if (state->vehicle_index != NONE)
		{
			SET_FLAG(parts, _distributed_unit_part_riding_bit, TRUE);
			cursor = legacy_put(cursor, &state->vehicle_index, sizeof(state->vehicle_index));
			cursor = legacy_put(cursor, &state->seat_index, sizeof(state->seat_index));
		}
		if (TEST_FLAG(flags, _distributed_unit_placed_bit))
		{
			cursor = legacy_put(cursor, &state->position, sizeof(state->position));
			cursor = legacy_put(cursor, &state->velocity, sizeof(state->velocity));
			cursor = legacy_put(cursor, &state->forward, sizeof(state->forward));
			if (state->up.i || state->up.j || state->up.k != (short)DISTRIBUTED_UNIT_SCALE)
			{
				SET_FLAG(parts, _distributed_unit_part_up_bit, TRUE);
				cursor = legacy_put(cursor, &state->up, sizeof(state->up));
			}
			/* (whole or not at all: never a value not finite) */
			if (state->deck_present && vehicle_riding_deck_values_valid(&state->deck))
			{
				SET_FLAG(parts, _distributed_unit_part_deck_bit, TRUE);
				cursor = distributed_deck_put(cursor, &state->deck);
			}
		}
		if (TEST_FLAG(flags, _distributed_unit_predicted_bit))
			cursor = legacy_put(cursor, &state->predicted_time, sizeof(state->predicted_time));
		cursor = legacy_put(cursor, &state->body_vitality, sizeof(state->body_vitality));
		cursor = legacy_put(cursor, &state->shield_vitality, sizeof(state->shield_vitality));
		if (state->current_body_damage || state->recent_body_damage || state->current_shield_damage ||
			state->recent_shield_damage)
		{
			SET_FLAG(parts, _distributed_unit_part_damage_bit, TRUE);
			cursor = legacy_put(cursor, &state->current_body_damage, sizeof(state->current_body_damage));
			cursor = legacy_put(cursor, &state->recent_body_damage, sizeof(state->recent_body_damage));
			cursor = legacy_put(cursor, &state->current_shield_damage, sizeof(state->current_shield_damage));
			cursor = legacy_put(cursor, &state->recent_shield_damage, sizeof(state->recent_shield_damage));
		}
		{
			short powerup_index;

			for (powerup_index = 0; powerup_index < NUMBER_OF_PLAYER_POWERUPS; powerup_index++)
			{
				if (state->powerup_durations[powerup_index])
					break;
			}
			if (powerup_index < NUMBER_OF_PLAYER_POWERUPS)
			{
				SET_FLAG(parts, _distributed_unit_part_powerups_bit, TRUE);
				cursor = legacy_put(cursor, state->powerup_durations, sizeof(state->powerup_durations));
			}
		}
		{
			boolean fine = state->fine && TEST_FLAG(flags, _distributed_unit_placed_bit);
			byte unit_flags = state->unit_flags;

			SET_FLAG(unit_flags, _distributed_unit_fine_velocity_bit, fine);
			if (unit_flags || state->active_camouflage)
			{
				SET_FLAG(parts, _distributed_unit_part_camouflage_bit, TRUE);
				cursor = legacy_put(cursor, &unit_flags, sizeof(unit_flags));
				cursor = legacy_put(cursor, &state->active_camouflage, sizeof(state->active_camouflage));
			}
			if (state->scripted_pose)
			{
				SET_FLAG(parts, _distributed_unit_part_scripted_pose_bit, TRUE);
				cursor = legacy_put(cursor, state->pose, sizeof(state->pose));
			}
			if (fine)
				cursor = legacy_put(cursor, &state->fine_velocity, sizeof(state->fine_velocity));
		}
	}
	else if (state->killing_player_index != NO_PLAYER)
	{
		SET_FLAG(parts, _distributed_unit_part_killer_bit, TRUE);
		cursor = legacy_put(cursor, &state->killing_player_index, sizeof(state->killing_player_index));
	}
	buffer[0] = state->player_index;
	buffer[1] = flags;
	buffer[2] = parts;
	return (word)(cursor - buffer);
}

/* a state from its bytes on the wire, no further than end: the bytes it
took, 0 for none that make one */
word distributed_unit_state_read(
	byte const *buffer,
	byte const *end,
	struct distributed_unit_state *state)
{
	byte const *cursor = buffer;
	byte parts;

	csmemset(state, 0, sizeof(*state));
	state->unit_index = NONE;
	state->vehicle_index = NONE;
	state->seat_index = NONE;
	state->killing_player_index = NO_PLAYER;
	state->up.k = (short)DISTRIBUTED_UNIT_SCALE;
	if (!legacy_take(&cursor, end, &state->player_index, sizeof(state->player_index)) ||
		!legacy_take(&cursor, end, &state->flags, sizeof(state->flags)) ||
		!legacy_take(&cursor, end, &parts, sizeof(parts)))
	{
		return 0;
	}
	if (TEST_FLAG(parts, _distributed_unit_part_scripted_pose_bit) &&
		!TEST_FLAG(state->flags, _distributed_unit_alive_bit))
		return 0;
	/* (a deck part where no place is: the bytes after it cannot be read, so
	neither can the state) */
	if (TEST_FLAG(parts, _distributed_unit_part_deck_bit) &&
		!(TEST_FLAG(state->flags, _distributed_unit_alive_bit) && TEST_FLAG(state->flags, _distributed_unit_placed_bit)))
	{
		return 0;
	}
	if (TEST_FLAG(state->flags, _distributed_unit_alive_bit))
	{
		if (!legacy_take(&cursor, end, &state->unit_index, sizeof(state->unit_index)))
			return 0;
		if (TEST_FLAG(parts, _distributed_unit_part_riding_bit) &&
			(!legacy_take(&cursor, end, &state->vehicle_index, sizeof(state->vehicle_index)) ||
				!legacy_take(&cursor, end, &state->seat_index, sizeof(state->seat_index))))
		{
			return 0;
		}
		if (TEST_FLAG(state->flags, _distributed_unit_placed_bit) &&
			(!legacy_take(&cursor, end, &state->position, sizeof(state->position)) ||
				!legacy_take(&cursor, end, &state->velocity, sizeof(state->velocity)) ||
				!legacy_take(&cursor, end, &state->forward, sizeof(state->forward)) ||
				(TEST_FLAG(parts, _distributed_unit_part_up_bit) &&
					!legacy_take(&cursor, end, &state->up, sizeof(state->up)))))
		{
			return 0;
		}
		if (TEST_FLAG(parts, _distributed_unit_part_deck_bit) &&
			!distributed_deck_take(&cursor, end, &state->deck_present, &state->deck))
		{
			return 0;
		}
		if (TEST_FLAG(state->flags, _distributed_unit_predicted_bit) &&
			!legacy_take(&cursor, end, &state->predicted_time, sizeof(state->predicted_time)))
		{
			return 0;
		}
		if (!legacy_take(&cursor, end, &state->body_vitality, sizeof(state->body_vitality)) ||
			!legacy_take(&cursor, end, &state->shield_vitality, sizeof(state->shield_vitality)))
		{
			return 0;
		}
		if (TEST_FLAG(parts, _distributed_unit_part_damage_bit) &&
			(!legacy_take(&cursor, end, &state->current_body_damage, sizeof(state->current_body_damage)) ||
				!legacy_take(&cursor, end, &state->recent_body_damage, sizeof(state->recent_body_damage)) ||
				!legacy_take(&cursor, end, &state->current_shield_damage, sizeof(state->current_shield_damage)) ||
				!legacy_take(&cursor, end, &state->recent_shield_damage, sizeof(state->recent_shield_damage))))
		{
			return 0;
		}
		if (TEST_FLAG(parts, _distributed_unit_part_powerups_bit) &&
			!legacy_take(&cursor, end, state->powerup_durations, sizeof(state->powerup_durations)))
		{
			return 0;
		}
		if (TEST_FLAG(parts, _distributed_unit_part_camouflage_bit) &&
			(!legacy_take(&cursor, end, &state->unit_flags, sizeof(state->unit_flags)) ||
				!legacy_take(&cursor, end, &state->active_camouflage, sizeof(state->active_camouflage))))
		{
			return 0;
		}
		if (TEST_FLAG(parts, _distributed_unit_part_scripted_pose_bit))
		{
			if (!legacy_take(&cursor, end, state->pose, sizeof(state->pose)))
				return 0;
			/* a pose that is not unit vectors (a unit's desired vectors are
			not normalized, and may be zero) is left out: the unit's other
			state, and the message's other units, are still taken */
			state->scripted_pose = distributed_scripted_pose_valid(state);
		}
		if (TEST_FLAG(state->unit_flags, _distributed_unit_fine_velocity_bit))
		{
			SET_FLAG(state->unit_flags, _distributed_unit_fine_velocity_bit, FALSE);
			if (!legacy_take(&cursor, end, &state->fine_velocity, sizeof(state->fine_velocity)))
				return 0;
			/* (finite, and no faster than the 16 bits' velocity can be) */
			state->fine = TEST_FLAG(state->flags, _distributed_unit_placed_bit) &&
				distributed_real_valid(state->fine_velocity.i) && distributed_real_valid(state->fine_velocity.j) &&
				distributed_real_valid(state->fine_velocity.k) &&
				fabs(state->fine_velocity.i) <= 32.0f && fabs(state->fine_velocity.j) <= 32.0f &&
				fabs(state->fine_velocity.k) <= 32.0f;
		}
	}
	else if (TEST_FLAG(parts, _distributed_unit_part_killer_bit) &&
		!legacy_take(&cursor, end, &state->killing_player_index, sizeof(state->killing_player_index)))
	{
		return 0;
	}
	return (word)(cursor - buffer);
}

/* ---------- the actors' states
Whole, the structure's 88 bytes as they are in memory (its last two
padding, left as actor_state_from_unit cleared them). */

word network_legacy_actor_state_add(
	long machine_index,
	struct distributed_actor_state const *state)
{
	legacy_actor_message.states[legacy_actor_count++] = *state;
	if (legacy_actor_count == MAXIMUM_ENTRIES_PER_MESSAGE ||
		legacy_actor_count == DATAGRAM_ENTRIES(struct distributed_actor_state))
	{
		legacy_actor_states_send(machine_index);
	}
	return sizeof(*state);
}

void network_legacy_actor_states_end(
	long machine_index)
{
	if (legacy_actor_count)
		legacy_actor_states_send(machine_index);
}
