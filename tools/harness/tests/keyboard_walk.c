/*
KEYBOARD_WALK.C

The keys' Walk (controls.walk, input.walk, input.walk_speed): the movement
keys' throttle (input_abstraction.c's keyboard_controls_apply, with its walk
and crouch), the push it walks at (port/linux/game/keyboard_walk.c's
keyboard_walk_throttle, keyboard_walk_tracker_throttle), the biped's
movement a tick that throttle gives (bipeds.c's biped_player_movement, with
the speeds biped_physics_prepare works out) and whether the motion trackers
draw a unit moving that fast (motion_sensor.c's should_draw_object): the
game's own code, in a fake world of one player on foot.
*/

#include "harness.h"
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) MIN(MAX((x), (lo)), (hi))
#define square_root(x) ((real)sqrt(x))
#define TICKS_PER_SECOND 30
#define MAXIMUM_GAMEPADS 4
#define UNSIGNED_CHAR_MAX 255

typedef struct { real i, j; } real_vector2d;

#include "config.inc"

struct game_input_state
{
	byte buttons[NUMBER_OF_GAME_CONTROLS];
	real forward_movement;
	real strafe;
	real yaw;
	real pitch;
};

/* (the keyboard's game controls: none held in these cases) */
static signed char const keyboard_game_controls[NUMBER_OF_GAME_CONTROLS] =
	{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static struct
{
	byte ticks[NUMBER_OF_GAME_CONTROLS];
} keyboard_controls[MAXIMUM_GAMEPADS];

/* the settings */
static char const *setting_walk = "hold";
static char const *setting_walk_speed = "tracker";
static unsigned long setting_changes = 1;

const char *config_string(const char *name)
{
	if (!strcmp(name, "input.walk"))
		return setting_walk;
	if (!strcmp(name, "input.walk_speed"))
		return setting_walk_speed;
	return "";
}

/* (each player's, their profile's: config.toml's here) */
const char *config_player_string(int local_player, const char *name)
{
	(void)local_player;
	return config_string(name);
}

unsigned long config_changes(void)
{
	return setting_changes;
}

void platform_log(char const *format, ...)
{
	(void)format;
}

int halo_linux_touch_move(short controller_index, real *forward, real *strafe)
{
	(void)controller_index;
	(void)forward;
	(void)strafe;
	return 0;
}

/* the world: one player, its unit a biped on foot or in a seat */
struct tag_reference
{
	unsigned long group_tag;
	char *name;
	long name_length;
	long index;
};

struct tag_block
{
	long count;
	void *address;
	void *definition;
};

#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])

#include "structures.inc"

struct game_globals
{
	struct tag_block player_information;
};

struct player_datum
{
	long unit_index;
	real speed_multiplier;
};

struct unit_datum
{
	long definition_index;
	struct
	{
		long parent_object_index;
		real_vector3d translational_velocity;
	} object;
	struct
	{
		unsigned long control_flags;
		short grenade_throw_state;
		unsigned long flags;
		struct
		{
			short base_seat_index;
		} animation;
	} unit;
};

struct biped_definition
{
	struct
	{
		real downhill_velocity_scale;
		real uphill_velocity_scale;
	} biped;
};

struct hud_globals_definition
{
	struct
	{
		real motion_sensor_velocity_sensitivity;
	} defaults;
};

static struct game_globals_player_information fake_information;
static struct game_globals fake_globals;
static struct player_datum fake_player;
static struct unit_datum fake_unit;
static struct biped_definition fake_definition;
static struct hud_globals_definition fake_hud_globals;
static struct hud_globals_definition *hud_globals = &fake_hud_globals;
static boolean fake_double_speed;
static boolean debug_motion_sensor_draw_all_units;

static long local_player_get_player_index(short local_player_index)
{
	return local_player_index == 0 ? 0 : NONE;
}

#define player_get(index) (&fake_player)
#define unit_try_and_get(index) ((index) == 7 ? &fake_unit : NULL)
#define unit_get(index) (&fake_unit)
#define biped_try_and_get(index) ((index) == 7 ? &fake_unit : NULL)
#define biped_definition_get(index) (&fake_definition)
#define scenario_get_game_globals() (&fake_globals)
#define game_players_are_double_speed() fake_double_speed
#define game_engine_draw_object_in_motion_sensor(index) TRUE
#define game_engine_running() TRUE

static void object_get_velocities(long object_index, real_vector3d *velocity, real_vector3d *angular)
{
	(void)object_index;
	(void)angular;
	*velocity = fake_unit.object.translational_velocity;
}

static real magnitude_squared3d(real_vector3d const *v)
{
	return v->i * v->i + v->j * v->j + v->k * v->k;
}

static void scale_vector3d(real_vector3d const *v, real scale, real_vector3d *result)
{
	result->i = v->i * scale;
	result->j = v->j * scale;
	result->k = v->k * scale;
}

#include "under_test.inc"

/* Halo's multiplayer cyborg as Blood Gulch's globals and HUD globals have
it (the game's log in a run there: the threshold 0.0141 world units a tick,
runs of 2.25 forward and 2 back and sideways, the biped's slope scale 1.25),
with a crouch's speeds of 0.873 */
static void world_setup(void)
{
	memset(&fake_information, 0, sizeof(fake_information));
	fake_information.run_forward_speed = 2.25f;
	fake_information.run_backward_speed = 2.f;
	fake_information.run_sideways_speed = 2.f;
	fake_information.sneak_forward_speed = 0.873f;
	fake_information.sneak_backward_speed = 0.873f;
	fake_information.sneak_sideways_speed = 0.873f;
	fake_information.double_speed_multiplier = 2.f;
	fake_globals.player_information.count = 1;
	fake_globals.player_information.address = &fake_information;
	memset(&fake_unit, 0, sizeof(fake_unit));
	fake_unit.object.parent_object_index = NONE;
	fake_player.unit_index = 7;
	fake_player.speed_multiplier = 1.f;
	fake_definition.biped.downhill_velocity_scale = 1.25f;
	fake_definition.biped.uphill_velocity_scale = 1.f;
	fake_hud_globals.defaults.motion_sensor_velocity_sensitivity = 0.0002f;
	fake_double_speed = FALSE;
	setting_walk = "hold";
	setting_walk_speed = "tracker";
	setting_changes++;
	memset(walk_toggles, 0, sizeof(walk_toggles));
}

static unsigned long keys(int a, int b, int c, int d)
{
	unsigned long held = 0;

	if (a >= 0) held |= 1UL << a;
	if (b >= 0) held |= 1UL << b;
	if (c >= 0) held |= 1UL << c;
	if (d >= 0) held |= 1UL << d;
	return held;
}

/* the keys' throttle, read by a frame and then by each of the frame's
sub-steps (game.sim_rate 60: 2 of a tick, 120: 4), as the fast phase reads
them again (input_abstraction_port_substep_state), which must agree */
static void throttle_of(unsigned long held, int substeps, real *forward, real *strafe)
{
	struct game_input_state state;
	int substep;

	memset(&state, 0, sizeof(state));
	walk_toggle_update(0, held);
	keyboard_controls_apply(0, held, &state);
	*forward = state.forward_movement;
	*strafe = state.strafe;
	for (substep = 1; substep < substeps; substep++)
	{
		struct game_input_state again;

		memset(&again, 0, sizeof(again));
		keyboard_controls_apply(0, held, &again);
		CHECK(again.forward_movement == *forward && again.strafe == *strafe,
			"sub-step %d's throttle %f %f, the frame's %f %f", substep, again.forward_movement, again.strafe,
			*forward, *strafe);
	}
}

/* the biped's velocity a tick for that throttle (crouched that far), on a
slope that scales its speed by slope, and whether the trackers show it */
static real velocity_of(real forward, real strafe, real crouch, real slope, boolean *shown)
{
	struct biped_player_speeds speeds;
	real_vector3d throttle;
	real_vector3d movement;
	real speed;

	memset(&speeds, 0, sizeof(speeds));
	biped_speeds(&fake_information, &fake_unit, crouch, &speeds);
	speeds.stun_scale = fake_player.speed_multiplier;
	speeds.double_speed = fake_double_speed;
	speeds.double_speed_multiplier = fake_information.double_speed_multiplier;
	throttle.i = forward;
	throttle.j = strafe;
	throttle.k = 0.f;
	biped_player_movement(&speeds, &throttle, &movement);
	/* (on the ground its velocity is that long, along the ground, times a
	slope's scale: biped_update_physics's) */
	speed = (real)sqrt(magnitude_squared3d(&movement)) * slope;
	fake_unit.object.translational_velocity.i = speed;
	fake_unit.object.translational_velocity.j = 0.f;
	fake_unit.object.translational_velocity.k = 0.f;
	*shown = should_draw_object(7);
	return speed;
}

static int const directions[][2] =
{
	{ HALO_KEYBOARD_MOVE_FORWARD, -1 },
	{ HALO_KEYBOARD_MOVE_BACKWARD, -1 },
	{ HALO_KEYBOARD_STRAFE_LEFT, -1 },
	{ HALO_KEYBOARD_STRAFE_RIGHT, -1 },
	{ HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_STRAFE_LEFT },
	{ HALO_KEYBOARD_MOVE_BACKWARD, HALO_KEYBOARD_STRAFE_RIGHT },
};

/* walking, every direction stays off the trackers, on the flat and down the
steepest slope, at each rate's sub-steps; as fast as it can, within the
margin and a step; running, every direction shows */
static void tracker_case(int substeps)
{
	real threshold;
	real fastest_walk = 0.f;
	unsigned int index;

	world_setup();
	threshold = square_root(fake_hud_globals.defaults.motion_sensor_velocity_sensitivity);
	for (index = 0; index < sizeof(directions) / sizeof(directions[0]); index++)
	{
		real forward, strafe, speed;
		boolean shown;

		throttle_of(keys(directions[index][0], directions[index][1], HALO_KEYBOARD_WALK, -1), substeps,
			&forward, &strafe);
		CHECK(forward != 0.f || strafe != 0.f, "walking moves (direction %u)", index);
		speed = velocity_of(forward, strafe, 0.f, 1.f, &shown);
		CHECK(!shown, "walking on the flat shows (direction %u: %f a tick, the threshold %f)", index, speed,
			threshold);
		speed = velocity_of(forward, strafe, 0.f, fake_definition.biped.downhill_velocity_scale, &shown);
		CHECK(!shown, "walking downhill shows (direction %u: %f a tick, the threshold %f)", index, speed,
			threshold);
		fastest_walk = MAX(fastest_walk, speed);

		throttle_of(keys(directions[index][0], directions[index][1], -1, -1), substeps, &forward, &strafe);
		speed = velocity_of(forward, strafe, 0.f, 1.f, &shown);
		CHECK(shown, "running does not show (direction %u: %f a tick)", index, speed);
	}
	/* (the fastest: the margin's 95% of the threshold, less a step) */
	CHECK(fastest_walk >= 0.9f * threshold, "walking is slower than it need be: %f a tick at most, the threshold %f",
		fastest_walk, threshold);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("tracker_30") tracker_case(1);
	CASE("tracker_60") tracker_case(2);
	CASE("tracker_120") tracker_case(4);
	CASE("full_speed")
	{
		/* not walking, the keys' push is as it was: a full one, a diagonal on
		the unit circle, a crouch's 0.97 */
		real forward, strafe;

		world_setup();
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f && strafe == 0.f, "forward %f %f", forward, strafe);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_STRAFE_RIGHT, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 0.70710678f && strafe == -0.70710678f, "diagonal %f %f", forward, strafe);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_CROUCH, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f * 0.97f, "crouched %.9f", forward);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_STRAFE_LEFT, HALO_KEYBOARD_CROUCH, -1), 1,
			&forward, &strafe);
		CHECK(forward == 0.70710678f * 0.97f && strafe == 0.70710678f * 0.97f, "crouched diagonal %.9f %.9f",
			forward, strafe);
		/* (Walk alone moves nothing) */
		throttle_of(keys(HALO_KEYBOARD_WALK, -1, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 0.f && strafe == 0.f, "walk alone %f %f", forward, strafe);
	}
	CASE("crouch")
	{
		/* walking crouched: the slower of the two pushes, as a light push of
		a stick crouched is: slower than either alone */
		real walk_forward, crouch_forward, both_forward, strafe;
		real walk_speed, crouch_speed, both_speed;
		boolean shown;

		world_setup();
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &walk_forward, &strafe);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_CROUCH, -1, -1), 1, &crouch_forward, &strafe);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, HALO_KEYBOARD_CROUCH, -1), 1,
			&both_forward, &strafe);
		CHECK(both_forward == walk_forward, "walking crouched pushes %f, walking %f", both_forward, walk_forward);
		walk_speed = velocity_of(walk_forward, 0.f, 0.f, 1.f, &shown);
		crouch_speed = velocity_of(crouch_forward, 0.f, 1.f, 1.f, &shown);
		both_speed = velocity_of(both_forward, 0.f, 1.f, 1.f, &shown);
		CHECK(both_speed < walk_speed && both_speed < crouch_speed, "walking crouched %f, walking %f, crouched %f",
			both_speed, walk_speed, crouch_speed);
		CHECK(!shown, "walking crouched shows");
		/* (a walk faster than a crouch's push: the crouch's 0.97, which keeps
		it crouched) */
		setting_walk_speed = "100";
		setting_changes++;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, HALO_KEYBOARD_CROUCH, -1), 1,
			&both_forward, &strafe);
		CHECK(both_forward <= 0.97f && both_forward > 0.96f, "walking at 100%% crouched pushes %f", both_forward);
	}
	CASE("toggle")
	{
		/* input.walk "toggle": a press walks until the next press; the
		sub-steps' readings never toggle it */
		real forward, strafe, full;

		world_setup();
		setting_walk = "toggle";
		setting_changes++;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 1, &full, &strafe);
		CHECK(full == 1.f, "before the press %f", full);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 4, &forward, &strafe);
		CHECK(forward < 0.5f, "pressed %f", forward);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 4, &forward, &strafe);
		CHECK(forward < 0.5f, "still held %f", forward);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 4, &forward, &strafe);
		CHECK(forward < 0.5f, "let go: still walking %f", forward);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f, "pressed again: running %f", forward);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f, "let go again %f", forward);
		/* (back to "hold": walks only while held) */
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		setting_walk = "hold";
		setting_changes++;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f, "hold, let go %f", forward);
		/* (and "toggle" again starts off) */
		setting_walk = "toggle";
		setting_changes++;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f, "toggle again, not pressed %f", forward);
	}
	CASE("hold")
	{
		real forward, strafe;

		world_setup();
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		CHECK(forward < 0.5f, "held %f", forward);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, -1, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f, "let go %f", forward);
	}
	CASE("steps")
	{
		/* a walking throttle is on the 127 steps a network game sends it in,
		never above the push: what the other machines play (rounded to the
		nearest step, network_distributed.c) is what this one does */
		unsigned int index;

		world_setup();
		for (index = 0; index < sizeof(directions) / sizeof(directions[0]); index++)
		{
			real forward, strafe;
			real sent_forward, sent_strafe;
			real push = keyboard_walk_throttle(0);

			throttle_of(keys(directions[index][0], directions[index][1], HALO_KEYBOARD_WALK, -1), 1,
				&forward, &strafe);
			sent_forward = (real)(signed char)(long)floor(PIN(forward, -1.0f, 1.0f) * 127.0f + 0.5f) / 127.0f;
			sent_strafe = (real)(signed char)(long)floor(PIN(strafe, -1.0f, 1.0f) * 127.0f + 0.5f) / 127.0f;
			CHECK(sent_forward == forward && sent_strafe == strafe, "sent %f %f, played %f %f", sent_forward,
				sent_strafe, forward, strafe);
			CHECK(forward * forward + strafe * strafe <= push * push, "%f %f past the push %f", forward, strafe,
				push);
		}
	}
	CASE("percent")
	{
		/* input.walk_speed: a percentage of the full push, else the
		tracker's */
		real forward, strafe, tracker;

		world_setup();
		tracker = keyboard_walk_throttle(0);
		setting_walk_speed = "50";
		setting_changes++;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 63.f / 127.f, "50%% pushes %f", forward);
		setting_walk_speed = "fast";
		setting_changes++;
		CHECK(keyboard_walk_throttle(0) == tracker, "a word not a number: %f, the tracker's %f",
			keyboard_walk_throttle(0), tracker);
		setting_walk_speed = "0";
		setting_changes++;
		CHECK(keyboard_walk_throttle(0) == tracker, "0%%: %f", keyboard_walk_throttle(0));
	}
	CASE("seat")
	{
		/* in a vehicle's seat, or with no unit, the keys drive as before */
		real forward, strafe;

		world_setup();
		fake_unit.object.parent_object_index = 3;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		CHECK(forward == 1.f, "seated %f", forward);
		world_setup();
		fake_player.unit_index = NONE;
		CHECK(keyboard_walk_throttle(0) == 1.f, "no unit %f", keyboard_walk_throttle(0));
	}
	CASE("faster_game")
	{
		/* a game type's speed (an oddball carrier's 1.25, double speed):
		walking slower to stay off the trackers */
		real forward, strafe, speed;
		boolean shown;

		world_setup();
		fake_player.speed_multiplier = 1.25f;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		speed = velocity_of(forward, strafe, 0.f, fake_definition.biped.downhill_velocity_scale, &shown);
		CHECK(!shown, "walking at 1.25 shows: %f a tick", speed);
		world_setup();
		fake_double_speed = TRUE;
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &forward, &strafe);
		speed = velocity_of(forward, strafe, 0.f, fake_definition.biped.downhill_velocity_scale, &shown);
		CHECK(!shown, "walking at double speed shows: %f a tick", speed);
	}
	CASE("rates")
	{
		/* the threshold is on a tick's velocity (a 30 Hz tick's, whatever
		the rate) and the throttle the same each sub-step: the tracker's
		push does not depend on game.sim_rate */
		real at_30, at_120, strafe;

		world_setup();
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 1, &at_30, &strafe);
		throttle_of(keys(HALO_KEYBOARD_MOVE_FORWARD, HALO_KEYBOARD_WALK, -1, -1), 4, &at_120, &strafe);
		CHECK(at_30 == at_120, "30 Hz %f, 120 Hz %f", at_30, at_120);
	}
	fprintf(stderr, "ran %s\n", case_name);
	return 0;
}
