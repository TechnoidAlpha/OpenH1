/* An OpenH1 game's actors' states without the parts at their defaults
(actor_state_compact_write and actor_state_compact_read, OpenH1 protocol
version 24): read back, every state is the structure written, byte for
byte, whether its parts are at their defaults or not; a standing actor's
is its least; cut short, none. */
#include "harness.h"
typedef unsigned short word;
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))
#define csmemset memset
#define csmemcpy memcpy
#define csmemcmp memcmp
struct distributed_vector { short i; short j; short k; };
#include "config.inc"
#include "under_test.inc"

/* a standing actor's state, as actor_state_from_unit makes one */
static struct distributed_actor_state standing(void)
{
	struct distributed_actor_state state;

	memset(&state, 0, sizeof(state));
	state.unit_index = 0x12340042;
	state.vehicle_index = NONE;
	state.seat_index = NONE;
	state.impulse = NO_IMPULSE;
	state.speech_sound = NONE;
	state.user_animation_graph = NONE;
	state.team = 3;
	state.animation_state = 1;
	state.aiming[0] = 1200;
	state.aiming[1] = -300;
	state.looking[0] = 1200;
	state.looking[1] = -300;
	state.facing[0] = 1200;
	state.facing[1] = -300;
	state.body_vitality = 16384;
	state.shield_vitality = 16384;
	state.position.x = 51.25f;
	state.position.y = -77.5f;
	state.position.z = 0.75f;
	state.forward.i = 32000;
	state.up.k = (short)DISTRIBUTED_UNIT_SCALE;
	return state;
}

static word round_trip(struct distributed_actor_state const *state)
{
	byte buffer[ACTOR_COMPACT_MAXIMUM_SIZE];
	struct distributed_actor_state read;
	word size = actor_state_compact_write(state, buffer);

	CHECK(size >= ACTOR_COMPACT_MINIMUM_SIZE && size <= ACTOR_COMPACT_MAXIMUM_SIZE, "its bytes: %u", size);
	CHECK(actor_state_compact_read(buffer, buffer + size, &read) == size, "the reader takes the bytes written (%u)", size);
	CHECK(!memcmp(&read, state, sizeof(read)), "the structure read is the one written");
	return size;
}

int main(int argc, char **argv)
{
	const char *which = argc > 1 ? argv[1] : "";
	struct distributed_actor_state state = standing();

	if (!strcmp(which, "standing"))
	{
		CHECK(round_trip(&state) == ACTOR_COMPACT_MINIMUM_SIZE, "a standing actor's state is the least");
		return 0;
	}
	if (!strcmp(which, "each-part"))
	{
		/* (each part alone off its default, then all of them) */
		short part;

		for (part = 0; part < 16; part++)
		{
			state = standing();
			switch (part)
			{
			case 0: state.vehicle_index = 0x00220011; state.seat_index = 2; break;
			case 1: state.impulse = 4; state.impulse_number = 9; state.impulse_alignment = -1000; break;
			case 2: state.impulse_number = 7; break;
			case 3: state.speech_sound = 0x00ab0033; state.speech_number = 12; break;
			case 4: state.speech_number = 5; break;
			case 5: state.user_animation_graph = 0x00450001; state.user_animation = 3; state.user_animation_number = 2;
				state.user_animation_interpolate = 1; break;
			case 6: state.user_animation_interpolate = 1; break;
			case 7: state.run_blindly_angle = 12345; break;
			case 8: state.active_camouflage = 200; break;
			case 9: state.looking[1] = 900; break;
			case 10: state.facing[0] = -5000; break;
			case 11: state.up.i = 500; state.up.k = 32000; break;
			case 12: state.velocity.j = -77; break;
			case 13: state.throttle[0] = 127; state.throttle[2] = -3; break;
			case 14: state.control_flags = 0x0805; break;
			case 15: state.primary_trigger = 255; break;
			}
			CHECK(round_trip(&state) > ACTOR_COMPACT_MINIMUM_SIZE, "part %d off its default goes", part);
		}
		state = standing();
		state.vehicle_index = 0x00220011;
		state.seat_index = 2;
		state.impulse = 4;
		state.impulse_number = 9;
		state.impulse_alignment = -1000;
		state.speech_sound = 0x00ab0033;
		state.speech_number = 12;
		state.user_animation_graph = 0x00450001;
		state.user_animation = 3;
		state.user_animation_number = 2;
		state.user_animation_interpolate = 1;
		state.run_blindly_angle = 12345;
		state.active_camouflage = 200;
		state.looking[1] = 900;
		state.facing[0] = -5000;
		state.up.i = 500;
		state.velocity.j = -77;
		state.throttle[0] = 127;
		state.control_flags = 0x0805;
		state.primary_trigger = 255;
		CHECK(round_trip(&state) == ACTOR_COMPACT_MAXIMUM_SIZE, "every part: the most");
		return 0;
	}
	if (!strcmp(which, "short"))
	{
		byte buffer[ACTOR_COMPACT_MAXIMUM_SIZE];
		struct distributed_actor_state read;
		word size, cut;

		state.velocity.i = 40;
		state.speech_sound = 0x00ab0033;
		size = actor_state_compact_write(&state, buffer);
		for (cut = 0; cut < size; cut++)
			CHECK(!actor_state_compact_read(buffer, buffer + cut, &read), "cut at %u: none", cut);
		return 0;
	}
	fprintf(stderr, "no such case: %s\n", which);
	return 2;
}
