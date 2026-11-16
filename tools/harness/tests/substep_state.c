/* A sub-step's state of a player on foot, written as its difference from the
tick's state the client was last sent of them (distributed_substep_state_write,
OpenH1 protocol version 22), and read back against the tick's state the
client took (distributed_substep_state_read): the position within an 8192th
of a world unit, the rest exactly; the full state where there is no tick's
state to write it against or a part does not fit; a difference from a tick's
state the client has not passed over, the entries after it read as sent. */
#include "harness.h"
#include <math.h>
typedef unsigned short word;
enum { MAXIMUM_TRACKED_PLAYERS = 4, HALO_PORT_MAXIMUM_NETWORK_MACHINES = 4 };
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define csmemset memset
#define csmemcpy memcpy
struct distributed_vector { short i; short j; short k; };
#include "config.inc"
static struct distributed_substep_base
	distributed_sent_substep_bases[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS];
static struct distributed_substep_base distributed_taken_substep_bases[MAXIMUM_TRACKED_PLAYERS][SUBSTEP_BASE_TICKS];

static long now_time;
static long game_time_get(void) { return now_time; }
static boolean distributed_point_valid(real_point3d const *point, real bound) {
	return fabsf(point->x) <= bound && fabsf(point->y) <= bound && fabsf(point->z) <= bound;
}
static boolean distributed_scripted_pose_valid(struct distributed_unit_state const *state) { (void)state; return TRUE; }
/* (vehicle_riding.c's checks of a deck part: none of these players rides one) */
static boolean vehicle_riding_deck_values_valid(struct vehicle_riding_deck const *deck) { (void)deck; return TRUE; }
static boolean vehicle_riding_deck_vehicle_named(long vehicle_index) { (void)vehicle_index; return FALSE; }
static short biped_riding_vehicle_node_count(long vehicle_index) { (void)vehicle_index; return NONE; }
static boolean vehicle_riding_deck_valid(struct vehicle_riding_deck const *deck, short node_count) {
	(void)deck; (void)node_count; return FALSE;
}
#include "under_test.inc"

/* a player on foot of a client (predicted), at the client's tick predicted */
static struct distributed_unit_state on_foot(byte player, long unit, real x, real y, real z, short predicted)
{
	struct distributed_unit_state state;

	memset(&state, 0, sizeof(state));
	state.player_index = player;
	state.flags = FLAG(_distributed_unit_alive_bit) | FLAG(_distributed_unit_placed_bit) |
		FLAG(_distributed_unit_predicted_bit);
	state.killing_player_index = NO_PLAYER;
	state.unit_index = unit;
	state.vehicle_index = NONE;
	state.seat_index = NONE;
	state.position.x = x;
	state.position.y = y;
	state.position.z = z;
	state.velocity.i = 300;
	state.velocity.j = -40;
	state.forward.i = 32000;
	state.forward.j = 2000;
	state.up.k = (short)DISTRIBUTED_UNIT_SCALE;
	state.predicted_time = predicted;
	state.body_vitality = 16384;
	state.shield_vitality = 16384;
	return state;
}

/* the host's tick state of the player sent to machine 1 and taken by it,
at tick time */
static void tick_state(struct distributed_unit_state const *state, long time)
{
	byte buffer[DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
	struct distributed_unit_state taken;
	word size = distributed_unit_state_write(state, TRUE, buffer);

	distributed_substep_base_from_state(state, TRUE, time, &distributed_sent_substep_bases[1][state->player_index]);
	CHECK(distributed_unit_state_read(buffer, buffer + size, &taken) == size, "the tick's state reads back");
	distributed_substep_base_from_state(&taken, TRUE, time,
		&distributed_taken_substep_bases[taken.player_index][time & (SUBSTEP_BASE_TICKS - 1)]);
}

static real distance(real a, real b) { return a > b ? a - b : b - a; }

/* the sub-step's state written to machine 1 at the host's tick time and read
back: its bytes, and whether it was a difference */
static word round_trip(struct distributed_unit_state const *state, long time, struct distributed_unit_state *read,
	boolean *difference, boolean *known)
{
	byte buffer[1 + DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
	word size;

	now_time = time;
	size = distributed_substep_state_write(1, state, buffer);
	*difference = TEST_FLAG(buffer[0], _substep_state_difference_bit);
	CHECK(distributed_substep_state_read(buffer, buffer + size, time, read, known) == size,
		"the reader takes the bytes the writer wrote (%u)", size);
	return size;
}

static void check_same(struct distributed_unit_state const *state, struct distributed_unit_state const *read,
	real position_tolerance)
{
	CHECK(read->player_index == state->player_index && read->unit_index == state->unit_index, "the player's unit");
	CHECK(TEST_FLAG(read->flags, _distributed_unit_alive_bit) && TEST_FLAG(read->flags, _distributed_unit_placed_bit),
		"alive and on foot");
	CHECK(TEST_FLAG(read->flags, _distributed_unit_predicted_bit) ==
		TEST_FLAG(state->flags, _distributed_unit_predicted_bit), "predicted as written");
	CHECK(read->vehicle_index == NONE, "on foot");
	CHECK(distance(read->position.x, state->position.x) <= position_tolerance &&
		distance(read->position.y, state->position.y) <= position_tolerance &&
		distance(read->position.z, state->position.z) <= position_tolerance,
		"the position within %g: (%f %f %f) for (%f %f %f)", position_tolerance, read->position.x, read->position.y,
		read->position.z, state->position.x, state->position.y, state->position.z);
	CHECK(!memcmp(&read->velocity, &state->velocity, sizeof(state->velocity)), "the velocity exactly");
	CHECK(!memcmp(&read->forward, &state->forward, sizeof(state->forward)), "the facing exactly");
	CHECK(!memcmp(&read->up, &state->up, sizeof(state->up)), "the up exactly");
	CHECK(!TEST_FLAG(state->flags, _distributed_unit_predicted_bit) || read->predicted_time == state->predicted_time,
		"the client's tick exactly (%d for %d)", read->predicted_time, state->predicted_time);
}

static void reset(void)
{
	short player, tick;
	long machine;

	for (player = 0; player < MAXIMUM_TRACKED_PLAYERS; player++)
	{
		for (tick = 0; tick < SUBSTEP_BASE_TICKS; tick++)
			distributed_taken_substep_bases[player][tick].time = NONE;
		for (machine = 0; machine < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine++)
			distributed_sent_substep_bases[machine][player].time = NONE;
	}
}

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "";
	struct distributed_unit_state state, read;
	boolean difference, known;
	word size;

	reset();
	if (!strcmp(which, "difference"))
	{
		/* (a sub-step on: moved a little, turned, the client's tick on) */
		state = on_foot(2, 0x12340005, 51.25f, -77.5f, 0.75f, 1000);
		tick_state(&state, 500);
		state.position.x += 0.137f;
		state.position.y -= 0.0441f;
		state.position.z += 0.0123f;
		state.velocity.i += 37;
		state.velocity.k -= 90;
		state.forward.j += 120;
		state.predicted_time = 1001;
		size = round_trip(&state, 500, &read, &difference, &known);
		CHECK(difference && known, "written as a difference and read against the tick's state");
		CHECK(size <= 16, "a difference's bytes: %u", size);
		check_same(&state, &read, 1.0f / 8192.0f + 1e-6f);
		/* (two ticks on from the tick's state, the client's tick past 16 bits) */
		state = on_foot(1, 0x22220001, -10.0f, 4.0f, 2.0f, 32767);
		tick_state(&state, 700);
		state.position.x -= 3.9f;
		state.predicted_time = (short)-32767;
		size = round_trip(&state, 702, &read, &difference, &known);
		CHECK(difference && known, "a difference two ticks on");
		check_same(&state, &read, 1.0f / 8192.0f + 1e-6f);
		return 0;
	}
	if (!strcmp(which, "parts-full"))
	{
		/* (a velocity's and a facing's change past a byte, and a new up: those
		parts whole, the rest a difference) */
		state = on_foot(0, 0x33330002, 5.0f, 5.0f, 5.0f, 10);
		tick_state(&state, 40);
		state.velocity.k += 3000;
		state.forward.i -= 9000;
		state.up.i = 400;
		state.up.k = 32000;
		size = round_trip(&state, 40, &read, &difference, &known);
		CHECK(difference && known, "still a difference");
		check_same(&state, &read, 1.0f / 8192.0f + 1e-6f);
		return 0;
	}
	if (!strcmp(which, "full"))
	{
		struct distributed_unit_state base = on_foot(3, 0x44440003, 0.0f, 0.0f, 0.0f, 200);

		/* (no tick's state sent: the full state, read whole) */
		state = base;
		size = round_trip(&state, 10, &read, &difference, &known);
		CHECK(!difference && known, "no tick's state: the full state");
		check_same(&state, &read, 0.0f);
		/* (further than 8 world units from the tick's state) */
		tick_state(&base, 20);
		state = base;
		state.position.y += 8.5f;
		round_trip(&state, 20, &read, &difference, &known);
		CHECK(!difference && known, "too far: the full state");
		check_same(&state, &read, 0.0f);
		/* (another unit: respawned) */
		state = base;
		state.unit_index = 0x44450003;
		round_trip(&state, 20, &read, &difference, &known);
		CHECK(!difference && known, "another unit: the full state");
		check_same(&state, &read, 0.0f);
		/* (the tick's state older than the client keeps) */
		state = base;
		round_trip(&state, 20 + SUBSTEP_BASE_TICKS, &read, &difference, &known);
		CHECK(!difference && known, "an old tick's state: the full state");
		/* (the client's tick further than a byte from the tick state's) */
		state = base;
		state.predicted_time = 200 + 200;
		round_trip(&state, 20, &read, &difference, &known);
		CHECK(!difference && known, "the client's tick far on: the full state");
		check_same(&state, &read, 0.0f);
		/* (a host's player, not predicted, against a predicted tick's state) */
		state = base;
		state.flags &= ~FLAG(_distributed_unit_predicted_bit);
		round_trip(&state, 20, &read, &difference, &known);
		CHECK(!difference && known, "predicted no longer: the full state");
		return 0;
	}
	if (!strcmp(which, "unknown"))
	{
		byte buffer[2 * (1 + DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE)];
		struct distributed_unit_state second = on_foot(1, 0x55550001, 9.0f, 9.0f, 9.0f, 7);
		word first_size, second_size;

		/* (the tick's state sent but lost: the client took none, or another
		tick's) */
		state = on_foot(0, 0x55550000, 1.0f, 2.0f, 3.0f, 70);
		now_time = 90;
		distributed_substep_base_from_state(&state, TRUE, 90, &distributed_sent_substep_bases[1][0]);
		distributed_substep_base_from_state(&state, TRUE, 82, &distributed_taken_substep_bases[0][90 & 7]);
		state.position.x += 0.5f;
		first_size = distributed_substep_state_write(1, &state, buffer);
		CHECK(TEST_FLAG(buffer[0], _substep_state_difference_bit), "written as a difference");
		second_size = distributed_substep_state_write(1, &second, buffer + first_size);
		{
			byte const *cursor = buffer;
			byte const *end = buffer + first_size + second_size;
			word taken = distributed_substep_state_read(cursor, end, 90, &read, &known);

			CHECK(taken == first_size && !known, "a difference from a tick's state not taken: passed over");
			cursor += taken;
			taken = distributed_substep_state_read(cursor, end, 90, &read, &known);
			CHECK(taken == second_size && known, "the entry after it read as sent");
			check_same(&second, &read, 0.0f);
		}
		return 0;
	}
	if (!strcmp(which, "short"))
	{
		byte buffer[1 + DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
		word cut;

		/* (cut short anywhere: no entry) */
		state = on_foot(2, 0x66660002, 3.0f, 3.0f, 3.0f, 5);
		tick_state(&state, 300);
		state.position.z += 0.25f;
		now_time = 300;
		size = distributed_substep_state_write(1, &state, buffer);
		for (cut = 0; cut < size; cut++)
			CHECK(!distributed_substep_state_read(buffer, buffer + cut, 300, &read, &known), "cut at %u: none", cut);
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", which);
	return 2;
}
