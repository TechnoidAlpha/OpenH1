#include "harness.h"
#include <math.h>

/* The player's turn a frame (source/game/player_control.c's get_local_player_input_blob, its camera's block): the
stick's look, which the aim assist's magnetism slows (friction) and drags along with a target (adhesion), and the
gyroscope's and the flick stick's turns (port/linux/src/xinput_sdl.c), which it must leave as they are, as it does
the mouse's. */

typedef struct { real yaw, pitch; } real_euler_angles2d;
typedef struct { real i, j; } real_vector2d;
#define TICKS_PER_SECOND 30
#define _real_epsilon 0.000001f
#define PIN(x, low, high) ((x) < (low) ? (low) : (x) > (high) ? (high) : (x))
#define DEGREES_TO_RADIANS(d) ((d) * 0.0174532925f)

struct game_globals_player_control
{
	real look_acceleration_time, look_acceleration_scale, look_pegging_threshold;
	real magnetism_friction, magnetism_adhesion;
};
struct player_control
{
	real look_acceleration_time;
	long target_object_index;
	real autoaim_level, magnetism_level;
	short zoom_level;
};
struct player_datum { long unit_index; };
struct input_blob { real_vector2d throttle; real_euler_angles2d facing_delta; };
struct game_input_state { int unused; };

/* the fake world: the target's magnetism and motion, the zoom's scale, and each device's turn this frame */
static boolean player_magnetism_flag = TRUE;
static real world_magnetism, world_target_yaw_rate, world_target_pitch_rate;
static real world_zoom_scale = 1.0f;
static real world_gyro_yaw, world_gyro_pitch, world_flick_yaw;
static boolean world_flick_stick;

static boolean player_control_camera_control_is_active(void) { return TRUE; }
/* (the stick's push each way as camera_turn was given it, in its clamped_yaw and clamped_pitch) */
static boolean player_control_stick_look(struct game_globals_player_control const *constants, short gamepad_index,
	struct game_input_state const *input_state, real yaw_rate, real pitch_rate, real_euler_angles2d *look,
	real *clamped_yaw, real *clamped_pitch)
{
	look->yaw = *clamped_yaw * yaw_rate;
	look->pitch = *clamped_pitch * pitch_rate;
	return *clamped_yaw >= constants->look_pegging_threshold;
}
static void player_control_scale_look(long unit_index, short zoom_level, real_euler_angles2d *look) {}
static long local_player_aim_assist(short local_player_index, real *autoaim, real *magnetism,
	real_euler_angles2d *position, real_euler_angles2d *velocity)
{
	*autoaim = world_magnetism;
	*magnetism = world_magnetism;
	position->yaw = position->pitch = 0.0f;
	velocity->yaw = world_target_yaw_rate;
	velocity->pitch = world_target_pitch_rate;
	return 7;
}
static real game_time_get_speed(void) { return 1.0f; }
static boolean game_players_are_double_speed(void) { return FALSE; }
static boolean input_abstraction_port_look_inverted(short gamepad_index) { return FALSE; }
static boolean input_abstraction_port_flick_stick(short gamepad_index) { return world_flick_stick; }
static real mouse_zoom_look_scale(short local_player_index, long unit_index, short zoom_level)
{
	return world_zoom_scale;
}
static int halo_linux_touch_look(short gamepad_index, real *yaw, real *pitch, real *gyro_yaw, real *gyro_pitch)
{
	return 0;
}
static int halo_linux_mouse_aiming(short gamepad_index) { return 0; }
static int player_control_aim_modern(short local_player_index, float *acceleration_tangent, float *acceleration_threshold)
{
	return 0;
}
static void input_trace_aim(int controller, int modern, int accelerating, float acceleration_seconds) {}
static int halo_linux_touch_aiming(short gamepad_index) { return 0; }
static int halo_linux_mouse_look(short gamepad_index, real *yaw, real *pitch) { return 0; }
static int halo_linux_gyro_look(short gamepad_index, real *yaw, real *pitch)
{
	*yaw = world_gyro_yaw;
	*pitch = world_gyro_pitch;
	return *yaw != 0.0f || *pitch != 0.0f;
}
static int halo_linux_flick_look(short gamepad_index, real *yaw)
{
	*yaw = world_flick_yaw;
	return *yaw != 0.0f;
}

#include "under_test.inc"

static int close_to(real a, real b) { return fabsf(a - b) < 1e-6f; }

/* a frame's turn: the stick at stick_yaw (of its range), the devices' turns as the world has them */
static struct input_blob frame(real stick_yaw, short zoom_level)
{
	struct game_globals_player_control constants = { 1.0f, 1.0f, 0.99f, 0.5f, 0.5f };
	struct player_control control = { 0.0f, -1, 0.0f, 0.0f, 0 };
	struct player_datum player = { 3 };
	struct game_input_state state = { 0 };
	struct input_blob input;

	memset(&input, 0, sizeof(input));
	control.zoom_level = zoom_level;
	camera_turn(&constants, &state, &control, &player, &input, stick_yaw, 0.0f, 0.1f, 0.05f, 1.0f / 30.0f, 0, 0);
	return input;
}

static void clear(void)
{
	world_magnetism = world_target_yaw_rate = world_target_pitch_rate = 0.0f;
	world_gyro_yaw = world_gyro_pitch = world_flick_yaw = 0.0f;
	world_zoom_scale = 1.0f;
	world_flick_stick = FALSE;
}

/* over a target, the magnetism slows and drags the stick's turn, and the gyroscope's turn is added as it came; with
no magnetism the gyroscope's turn is added the same */
static void case_magnetism(void)
{
	struct input_blob stick_only, with_gyro, plain_stick, plain_gyro;

	clear();
	world_magnetism = 1.0f;
	world_target_yaw_rate = 0.02f;
	world_target_pitch_rate = 0.01f;
	stick_only = frame(0.5f, -1);
	world_gyro_yaw = 0.3f;
	world_gyro_pitch = -0.2f;
	with_gyro = frame(0.5f, -1);
	/* (the stick's own turn is slowed by the friction and dragged by the adhesion) */
	CHECK(close_to(stick_only.facing_delta.yaw, 0.02f * 0.5f + 0.05f * 0.5f),
		"the stick's turn over a target was %f, of %f", stick_only.facing_delta.yaw, 0.02f * 0.5f + 0.05f * 0.5f);
	CHECK(close_to(with_gyro.facing_delta.yaw - stick_only.facing_delta.yaw, 0.3f) &&
		close_to(with_gyro.facing_delta.pitch - stick_only.facing_delta.pitch, -0.2f),
		"over a target the gyroscope turned the player %f, %f, of 0.3, -0.2",
		with_gyro.facing_delta.yaw - stick_only.facing_delta.yaw,
		with_gyro.facing_delta.pitch - stick_only.facing_delta.pitch);
	/* (the gyroscope alone, the stick at rest: the magnetism drags nothing then, the player not moving) */
	world_gyro_yaw = world_gyro_pitch = 0.0f;
	plain_stick = frame(0.0f, -1);
	world_gyro_yaw = 0.3f;
	world_gyro_pitch = -0.2f;
	plain_gyro = frame(0.0f, -1);
	CHECK(close_to(plain_gyro.facing_delta.yaw - plain_stick.facing_delta.yaw, 0.3f) &&
		close_to(plain_gyro.facing_delta.yaw, 0.3f),
		"over a target, the stick at rest, the gyroscope turned the player %f, of 0.3", plain_gyro.facing_delta.yaw);
}

/* zoomed, the gyroscope's turn is scaled as the mouse's; the flick stick's is not, and counts only as the flick
stick (the default stick layout) */
static void case_zoom_and_flick(void)
{
	struct input_blob input;

	clear();
	world_gyro_yaw = 0.4f;
	world_zoom_scale = 0.25f;
	input = frame(0.0f, 0);
	CHECK(close_to(input.facing_delta.yaw, 0.1f), "zoomed, the gyroscope turned %f, of 0.1", input.facing_delta.yaw);
	clear();
	world_flick_yaw = -1.5f;
	world_zoom_scale = 0.25f;
	input = frame(0.0f, 0);
	CHECK(close_to(input.facing_delta.yaw, 0.0f), "not the flick stick, its turn counted: %f", input.facing_delta.yaw);
	world_flick_stick = TRUE;
	world_magnetism = 1.0f;
	input = frame(0.0f, 0);
	CHECK(close_to(input.facing_delta.yaw, -1.5f), "the flick stick turned %f, of -1.5", input.facing_delta.yaw);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "magnetism"))
		case_magnetism();
	else if (!strcmp(name, "zoom_and_flick"))
		case_zoom_and_flick();
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
