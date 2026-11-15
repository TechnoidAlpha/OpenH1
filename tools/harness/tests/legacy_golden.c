/* The Legacy encodings of the host's per-tick streams (network_legacy.c),
frozen: fixed states of players and actors written as a Legacy machine is
sent them (the whole unit states of _distributed_message_unit_states, the
whole actor structures of _distributed_message_actor_states, in their
messages), and as an OpenH1 client is (a unit state's difference, whole
and from a state it took; an actor's compact state), each printed as hex
for test_legacy_golden.py to hold to the bytes it recorded. And which
machines take them (network_legacy_machine). */
#include "harness.h"
#include <math.h>
typedef unsigned short word;
typedef word message_header;
enum { MAXIMUM_TRACKED_PLAYERS = 4, HALO_PORT_MAXIMUM_NETWORK_MACHINES = 4, DATAGRAM_MAXIMUM_SIZE = 1200 };
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define csmemset memset
#define csmemcpy memcpy
#define csmemcmp memcmp
#define UNIT_WORLD_BOUND 32768.0f
#include "config.inc"

/* ---------- what the encoders call */

/* (the machines' kinds, as network_standard.c has them) */
static boolean hosting;
static boolean openh1_machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static boolean network_standard_hosting(void) { return hosting; }
static boolean network_standard_machine_is_openh1(long machine_index) { return hosting && openh1_machines[machine_index]; }

/* the messages sent, as their bytes past the header */
static int sent_count;
static void distributed_send_to_machine(long machine_index, void *message, byte type, short count, word size)
{
	byte const *bytes = (byte const *)message + sizeof(struct distributed_message_header);
	word index;

	printf("message %ld %u %d %u ", machine_index, type, count, size);
	for (index = sizeof(struct distributed_message_header); index < size; index++)
		printf("%02x", bytes[index - sizeof(struct distributed_message_header)]);
	printf("\n");
	sent_count++;
}

static struct distributed_unit_state distributed_tick_states[MAXIMUM_TRACKED_PLAYERS][UNIT_BASE_TICKS];
static long distributed_tick_state_times[MAXIMUM_TRACKED_PLAYERS][UNIT_BASE_TICKS];
static long distributed_unit_sent_times[HALO_PORT_MAXIMUM_NETWORK_MACHINES][MAXIMUM_TRACKED_PLAYERS][UNIT_SENT_TICKS];
static long distributed_unit_acked_times[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static long now_time;
static long game_time_get(void) { return now_time; }
static boolean distributed_point_valid(real_point3d const *point, real bound) {
	return fabsf(point->x) <= bound && fabsf(point->y) <= bound && fabsf(point->z) <= bound;
}
/* (vehicle_riding.c's checks of a deck part: none of the recorded players rides one) */
static boolean vehicle_riding_deck_values_valid(struct vehicle_riding_deck const *deck) { (void)deck; return TRUE; }
static boolean vehicle_riding_deck_vehicle_named(long vehicle_index) { (void)vehicle_index; return FALSE; }
static short biped_riding_vehicle_node_count(long vehicle_index) { (void)vehicle_index; return NONE; }
boolean distributed_scripted_pose_valid(struct distributed_unit_state const *state) { (void)state; return TRUE; }
static boolean vehicle_riding_deck_valid(struct vehicle_riding_deck const *deck, short node_count) {
	(void)deck; (void)node_count; return FALSE;
}
#include "under_test.inc"

/* ---------- the states */

static void hex(char const *name, byte const *bytes, word size)
{
	word index;

	printf("%s ", name);
	for (index = 0; index < size; index++)
		printf("%02x", bytes[index]);
	printf("\n");
}

/* a player on foot, a client's (predicted) */
static struct distributed_unit_state on_foot(void)
{
	struct distributed_unit_state state;

	memset(&state, 0, sizeof(state));
	state.player_index = 2;
	state.flags = FLAG(_distributed_unit_alive_bit) | FLAG(_distributed_unit_placed_bit) |
		FLAG(_distributed_unit_predicted_bit) | FLAG(_distributed_unit_shield_charging_bit);
	state.killing_player_index = NO_PLAYER;
	state.unit_index = 0x12340042;
	state.vehicle_index = NONE;
	state.seat_index = NONE;
	state.position.x = 51.25f;
	state.position.y = -77.5f;
	state.position.z = 0.75f;
	state.velocity.i = 300;
	state.velocity.j = -120;
	state.velocity.k = 5;
	state.forward.i = 32000;
	state.forward.j = 2000;
	state.forward.k = -900;
	state.up.k = (short)DISTRIBUTED_UNIT_SCALE;
	state.predicted_time = 0x1234;
	state.body_vitality = 16384;
	state.shield_vitality = 12000;
	return state;
}

static struct distributed_unit_state player_state(char const *name)
{
	struct distributed_unit_state state = on_foot();

	if (!strcmp(name, "riding"))
	{
		SET_FLAG(state.flags, _distributed_unit_placed_bit, FALSE);
		state.vehicle_index = 0x00220011;
		state.seat_index = 3;
	}
	else if (!strcmp(name, "tilted"))
	{
		state.up.i = 1200;
		state.up.j = -300;
		state.up.k = 32700;
	}
	else if (!strcmp(name, "dead-killer") || !strcmp(name, "dead"))
	{
		memset(&state, 0, sizeof(state));
		state.player_index = 1;
		state.flags = FLAG(_distributed_unit_friendly_fire_bit);
		state.killing_player_index = !strcmp(name, "dead") ? NO_PLAYER : 3;
		state.unit_index = NONE;
		state.vehicle_index = NONE;
		state.seat_index = NONE;
		state.up.k = (short)DISTRIBUTED_UNIT_SCALE;
	}
	else if (!strcmp(name, "hurt"))
	{
		SET_FLAG(state.flags, _distributed_unit_shield_depleted_bit, TRUE);
		state.shield_vitality = 0;
		state.body_vitality = 5000;
		state.current_body_damage = 700;
		state.recent_body_damage = 300;
		state.current_shield_damage = 16384;
		state.recent_shield_damage = 2;
	}
	else if (!strcmp(name, "camouflaged"))
	{
		state.unit_flags = FLAG(_distributed_unit_camouflaged_bit) | FLAG(_distributed_unit_super_camouflaged_bit);
		state.active_camouflage = 200;
	}
	else if (!strcmp(name, "powerups"))
	{
		state.powerup_durations[0] = 900;
		state.powerup_durations[1] = 31;
	}
	else if (!strcmp(name, "fine"))
	{
		state.fine = TRUE;
		state.fine_velocity.i = 0.29296875f;
		state.fine_velocity.j = -0.1171875f;
		state.fine_velocity.k = 0.0048828125f;
	}
	return state;
}

static char const *const player_names[] = {
	"on-foot", "riding", "tilted", "dead-killer", "dead", "hurt", "camouflaged", "powerups", "fine",
};

/* an actor standing, as actor_state_from_unit makes one */
static struct distributed_actor_state standing(void)
{
	struct distributed_actor_state state;

	memset(&state, 0, sizeof(state));
	state.unit_index = 0x56780013;
	state.vehicle_index = NONE;
	state.seat_index = NONE;
	state.impulse = NO_IMPULSE;
	state.speech_sound = NONE;
	state.user_animation_graph = NONE;
	state.team = 2;
	state.animation_state = 1;
	state.aiming_speed = 3;
	state.aiming[_angle_yaw] = 1200;
	state.aiming[_angle_pitch] = -300;
	state.looking[_angle_yaw] = 1200;
	state.looking[_angle_pitch] = -300;
	state.facing[_angle_yaw] = 1200;
	state.facing[_angle_pitch] = -300;
	state.body_vitality = 16384;
	state.shield_vitality = 8000;
	state.position.x = -12.5f;
	state.position.y = 40.25f;
	state.position.z = 3.0f;
	state.forward.i = 32000;
	state.forward.j = -1500;
	state.up.k = (short)DISTRIBUTED_UNIT_SCALE;
	return state;
}

static struct distributed_actor_state actor_state(char const *name)
{
	struct distributed_actor_state state = standing();

	if (!strcmp(name, "riding"))
	{
		SET_FLAG(state.flags, _distributed_actor_rides_bit, TRUE);
		state.vehicle_index = 0x00330021;
		state.seat_index = 1;
	}
	else if (!strcmp(name, "moving"))
	{
		state.velocity.i = 120;
		state.velocity.j = -40;
		state.throttle[0] = 127;
		state.throttle[1] = -20;
		state.control_flags = 0x0102;
		state.primary_trigger = 255;
		state.looking[_angle_yaw] = 1400;
		state.facing[_angle_pitch] = 0;
	}
	else if (!strcmp(name, "impulse"))
	{
		SET_FLAG(state.flags, _distributed_actor_impulse_aligned_bit, TRUE);
		state.impulse = 4;
		state.impulse_number = 9;
		state.impulse_alignment = -1000;
	}
	else if (!strcmp(name, "speech"))
	{
		state.speech_sound = 0x0a0b0c0d;
		state.speech_number = 6;
	}
	else if (!strcmp(name, "animation"))
	{
		state.user_animation_graph = 0x01020304;
		state.user_animation = 17;
		state.user_animation_number = 2;
		state.user_animation_interpolate = 1;
	}
	else if (!strcmp(name, "fleeing"))
	{
		SET_FLAG(state.flags, _distributed_actor_running_blindly_bit, TRUE);
		state.run_blindly_angle = 20000;
	}
	else if (!strcmp(name, "camouflaged"))
	{
		SET_FLAG(state.flags, _distributed_actor_camouflaged_bit, TRUE);
		SET_FLAG(state.flags, _distributed_actor_shield_over_charging_bit, TRUE);
		state.active_camouflage = 128;
		state.up.i = 900;
		state.up.k = 32000;
	}
	return state;
}

static char const *const actor_names[] = {
	"standing", "riding", "moving", "impulse", "speech", "animation", "fleeing", "camouflaged",
};

#define COUNT(array) ((short)(sizeof(array) / sizeof((array)[0])))

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "";
	char name[64];
	short index;

	for (index = 0; index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; index++)
		distributed_unit_acked_times[index] = NONE;
	memset(distributed_tick_state_times, 0xFF, sizeof(distributed_tick_state_times));
	memset(distributed_unit_sent_times, 0xFF, sizeof(distributed_unit_sent_times));
	if (!strcmp(which, "players"))
	{
		/* (the Legacy whole states, with the client's tick and without, and
		read back as written) */
		for (index = 0; index < COUNT(player_names); index++)
		{
			struct distributed_unit_state state = player_state(player_names[index]);
			struct distributed_unit_state read;
			byte buffer[DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
			short predicted;

			for (predicted = 1; predicted >= 0; predicted--)
			{
				word size = distributed_unit_state_write(&state, (boolean)predicted, buffer);

				CHECK(size >= DISTRIBUTED_UNIT_STATE_MINIMUM_SIZE && size <= DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE,
					"%s: its bytes, %u", player_names[index], size);
				CHECK(distributed_unit_state_read(buffer, buffer + size, &read) == size,
					"%s: the reader takes the bytes written", player_names[index]);
				CHECK(!size || distributed_unit_state_read(buffer, buffer + size - 1, &read) == 0,
					"%s: cut short, none", player_names[index]);
				sprintf(name, "legacy-player %s%s", player_names[index], predicted ? " predicted" : "");
				hex(name, buffer, size);
			}
		}
		return 0;
	}
	if (!strcmp(which, "player-differences"))
	{
		/* (OpenH1's: whole where the client took nothing; from the state of
		tick 10 it took, at 11) */
		for (index = 0; index < COUNT(player_names); index++)
		{
			struct distributed_unit_state state = player_state(player_names[index]);
			struct distributed_unit_state base = on_foot();
			byte buffer[1 + DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
			short player = state.player_index;
			word size;

			distributed_unit_state_snap(&state);
			now_time = 11;
			distributed_unit_acked_times[1] = NONE;
			size = distributed_unit_difference_write(1, player, &state, TRUE, buffer);
			sprintf(name, "openh1-player-whole %s", player_names[index]);
			hex(name, buffer, size);
			base.player_index = (byte)player;
			base.position.x -= 0.125f;
			base.forward.j -= 40;
			base.predicted_time -= 1;
			distributed_unit_state_snap(&base);
			distributed_tick_states[player][10] = base;
			distributed_tick_state_times[player][10] = 10;
			distributed_unit_sent_times[1][player][0] = 10;
			distributed_unit_acked_times[1] = 10;
			size = distributed_unit_difference_write(1, player, &state, TRUE, buffer);
			sprintf(name, "openh1-player-difference %s", player_names[index]);
			hex(name, buffer, size);
			distributed_tick_state_times[player][10] = NONE;
			distributed_unit_sent_times[1][player][0] = NONE;
		}
		return 0;
	}
	if (!strcmp(which, "actors"))
	{
		/* (the Legacy whole structures, one a message; and OpenH1's compact) */
		for (index = 0; index < COUNT(actor_names); index++)
		{
			struct distributed_actor_state state = actor_state(actor_names[index]);
			byte buffer[ACTOR_COMPACT_MAXIMUM_SIZE];
			struct distributed_actor_state read;
			word size;

			printf("legacy-actor %s ", actor_names[index]);
			CHECK(network_legacy_actor_state_add(2, &state) == sizeof(state), "a whole structure's bytes");
			network_legacy_actor_states_end(2);
			size = actor_state_compact_write(&state, buffer);
			CHECK(actor_state_compact_read(buffer, buffer + size, &read) == size && !memcmp(&read, &state, sizeof(read)),
				"%s: the compact state read is the one written", actor_names[index]);
			sprintf(name, "openh1-actor %s", actor_names[index]);
			hex(name, buffer, size);
		}
		return 0;
	}
	if (!strcmp(which, "actor-messages"))
	{
		/* (30 actors to a Legacy machine: as many as a datagram holds a
		message, the rest in the last) */
		struct distributed_actor_state state = standing();

		printf("legacy-actor-messages ");
		for (index = 0; index < 30; index++)
		{
			state.unit_index = 0x56780000 + index;
			network_legacy_actor_state_add(3, &state);
		}
		network_legacy_actor_states_end(3);
		network_legacy_actor_states_end(3);
		CHECK(sent_count == 3, "the messages: %d", sent_count);
		return 0;
	}
	if (!strcmp(which, "machines"))
	{
		/* (every machine of a Legacy game; of an OpenH1 game, all but OpenH1's) */
		hosting = FALSE;
		openh1_machines[1] = TRUE;
		CHECK(network_legacy_machine(1) && network_legacy_machine(2), "a Legacy game's machines take the Legacy encodings");
		hosting = TRUE;
		CHECK(!network_legacy_machine(1), "an OpenH1 game's OpenH1 machine takes OpenH1's");
		CHECK(network_legacy_machine(2), "an OpenH1 game's OpenCE machine takes the Legacy encodings");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", which);
	return 2;
}
