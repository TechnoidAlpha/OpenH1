/*
INPUT_ABSTRACTION.C

symbols in this file:
000BDA30 0020:
	_input_abstraction_dispose (0000)
000BDA50 0010:
	_input_abstraction_reset_controller_detection_timer (0000)
000BDA60 0080:
	_input_abstraction_get_local_player_preferences (0000)
000BDAE0 00b0:
	_input_abstraction_update_local_player_preferences (0000)
000BDB90 0050:
	_input_abstraction_get_input_state (0000)
000BDBE0 0080:
	_input_abstraction_update_device_changes (0000)
000BDC60 00e0:
	_code_000bdc60 (0000)
000BDD40 0050:
	_code_000bdd40 (0000)
000BDD90 00a0:
	_input_abstraction_initialize (0000)
000BDE30 0950:
	_input_abstraction_update (0000)
0026F3A0 0014:
	_rdata_0026f3a0 (0000)
0026F3B4 000c:
	??_C@_0M@OAMDCHEH@preferences?$AA@ (0000)
0026F3C0 0041:
	??_C@_0EB@NBKADHOE@?$CIlocal_player_index?$DO?$DN0?$CJ?5?$CG?$CG?5?$CIloca@ (0000)
0026F404 0029:
	??_C@_0CJ@KPJFDJEG@c?3?2halo?2SOURCE?2input?2input_abstr@ (0000)
0026F430 0041:
	??_C@_0EB@LOCNLGGH@invalid?5controller?5preferences?$DL?5@ (0000)
0026F474 003d:
	??_C@_0DN@PDKODJDH@?$CIcontroller_index?$DO?$DN0?$CJ?5?$CG?$CG?5?$CIcontro@ (0000)
0026F4B4 0039:
	??_C@_0DJ@DKGJGJAM@stopping?5bink?5playback?5to?5due?5to@ (0000)
0026F4F0 004c:
	??_C@_0EM@DKLJMFDI@?$CIcontroller_index?$DO?$DN0?$CJ?5?$CG?$CG?5?$CIcontro@ (0000)
0026F53C 0018:
	??_C@_0BI@DEJBIOHJ@unknown?5joystick?5preset?$AA@ (0000)
0026F558 0008:
	__real@3fc6571840000000 (0000)
0026F560 0008:
	__real@3ffa313e30a3879f (0000)
0026F568 0008:
	__real@3fe38c3550000000 (0000)
004535C0 00e0:
	_input_abstraction_globals (0000)
*/

/* ---------- headers */

#include "cseries/cseries.h"
#include "bink/bink_playback.h"
#include "cseries/errors.h"
#include "cseries/cseries_windows.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "input/input.h"
#include "input/input_abstraction.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "interface/virtual_keyboard.h"
#include "math/real_math.h"
#include "networking/network_game_globals.h"
#include "scenario/scenario.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"
#include "halo_keyboard.h" /* port */

/* ---------- constants */

enum
{
	_game_control_jump,
	_game_control_switch_grenades,
	_game_control_action,
	_game_control_switch_weapons,
	_game_control_melee,
	_game_control_flashlight,
	_game_control_grenade,
	_game_control_primary_trigger,
	_game_control_start,
	_game_control_back,
	_game_control_crouch,
	_game_control_zoom,
	NUMBER_OF_GAME_CONTROLS,
};

enum
{
	_joystick_controls_default,
	_joystick_controls_southpaw,
	_joystick_controls_legacy,
	_joystick_controls_legacy_southpaw,
	NUMBER_OF_JOYSTICK_CONTROLS,
};

/* port: input_xbox.c's GAMEPAD_STICK_DEAD_RANGE, for a sub-step's reading
(input_abstraction_port_substep_state) */
enum
{
	SUBSTEP_GAMEPAD_STICK_DEAD_RANGE = 9000,
};

enum
{
	_vehicle_type_human_plane = 3,
	_vehicle_type_alien_fighter = 5,
};

/* January promotes the rounded 45- and 10-degree single-precision constants
 * before subtracting them; the 35-degree window and its reciprocal are double. */
#define STICK_DIAGONAL_ANGLE 0.7853981852531433f
#define STICK_SECOND_QUADRANT_DIAGONAL_ANGLE 2.356194496154785f
#define RIGHT_STICK_DIAGONAL_SNAP_ANGLE ((double)0.1745329201221466f)
#define LEFT_STICK_DIAGONAL_SNAP_ANGLE ((double)STICK_DIAGONAL_ANGLE - RIGHT_STICK_DIAGONAL_SNAP_ANGLE)
#define STICK_DIAGONAL_BLEND_SCALE (1.0 / LEFT_STICK_DIAGONAL_SNAP_ANGLE)

/* ---------- macros */

/* ---------- structures */

struct game_input_state
{
	byte buttons[NUMBER_OF_GAME_CONTROLS];
	real forward_movement;
	real strafe;
	real yaw;
	real pitch;
};

/* Vehicle tags remain opaque in the shared header. This authenticated prefix
 * is sufficient for the aircraft-type check; it is not a complete tag layout. */
struct vehicle_definition
{
	struct unit_definition unit;
	unsigned long flags;
	short vehicle_type;
};

typedef char verify_input_vehicle_type_offset[
	offsetof(struct vehicle_definition, vehicle_type) == 0x2F4 ? 1 : -1];

struct input_abstraction_runtime_globals
{
	struct game_input_preferences player_control_preferences[MAXIMUM_GAMEPADS];
	struct game_input_state input_states[MAXIMUM_GAMEPADS];
	unsigned long device_enumeration_startup_timer;
	boolean controller_available[MAXIMUM_GAMEPADS];
	boolean initialized;
	unsigned long time_of_first_device_insertion;
};

typedef char verify_game_input_state_size[
	sizeof(struct game_input_state) == 0x1C ? 1 : -1];
typedef char verify_input_abstraction_input_states_offset[
	offsetof(struct input_abstraction_runtime_globals, input_states) == 0x60 ? 1 : -1];
typedef char verify_input_abstraction_device_timer_offset[
	offsetof(struct input_abstraction_runtime_globals, device_enumeration_startup_timer) == 0xD0 ? 1 : -1];
typedef char verify_input_abstraction_controller_available_offset[
	offsetof(struct input_abstraction_runtime_globals, controller_available) == 0xD4 ? 1 : -1];
typedef char verify_input_abstraction_initialized_offset[
	offsetof(struct input_abstraction_runtime_globals, initialized) == 0xD8 ? 1 : -1];
typedef char verify_input_abstraction_first_insertion_offset[
	offsetof(struct input_abstraction_runtime_globals, time_of_first_device_insertion) == 0xDC ? 1 : -1];
typedef char verify_input_abstraction_runtime_globals_size[
	sizeof(struct input_abstraction_runtime_globals) == 0xE0 ? 1 : -1];

/* ---------- prototypes */

static void set_default_game_input_preferences(
	struct game_input_preferences *preferences);
static boolean local_player_is_piloting_aircraft(
	short controller_index);

/* ---------- globals */

struct input_abstraction_runtime_globals input_abstraction_globals = {0};
static real const gamepad_axis_normalization_scale = 1.f / SHORT_MAX;
static real const stick_direction_angles[] =
{
	STICK_DIAGONAL_ANGLE,
	STICK_SECOND_QUADRANT_DIAGONAL_ANGLE,
	-STICK_DIAGONAL_ANGLE,
	-STICK_SECOND_QUADRANT_DIAGONAL_ANGLE,
};

/* port: the keyboard and mouse's own controls (port/linux/include/
halo_keyboard.h): the actions held, taken as the controller's game controls
(held for as many ticks as input_xbox.c counts a button), its movement, and
reloading; Start and Back they press on the controller itself */
static signed char const keyboard_game_controls[NUMBER_OF_GAME_CONTROLS] =
{
	HALO_KEYBOARD_JUMP,
	HALO_KEYBOARD_SWITCH_GRENADE,
	HALO_KEYBOARD_ACTION,
	HALO_KEYBOARD_SWITCH_WEAPON,
	HALO_KEYBOARD_MELEE,
	HALO_KEYBOARD_FLASHLIGHT,
	HALO_KEYBOARD_THROW_GRENADE,
	HALO_KEYBOARD_FIRE,
	-1,
	-1,
	HALO_KEYBOARD_CROUCH,
	HALO_KEYBOARD_ZOOM,
};

static struct
{
	byte ticks[NUMBER_OF_GAME_CONTROLS];
	long down_times[NUMBER_OF_GAME_CONTROLS];
	byte reload_ticks;
	long reload_down_time;
} keyboard_controls[MAXIMUM_GAMEPADS];

static void keyboard_hold_ticks(
	byte *ticks,
	long *down_time,
	boolean down)
{
	long now = (long)system_milliseconds();

	if (!down)
	{
		*ticks = 0;
	}
	else if (*ticks == 0)
	{
		*ticks = 1;
		*down_time = now;
	}
	else
	{
		long held = now - *down_time;

		held = held < 0 ? 0 : MIN(held, (long)UNSIGNED_CHAR_MAX * 1000 / TICKS_PER_SECOND);
		*ticks = (byte)PIN(1 + held * TICKS_PER_SECOND / 1000, 2, UNSIGNED_CHAR_MAX);
	}
	return;
}

/* port: a crouched player's move kept below the full stick's 0.98 that
stands a grounded biped up (player_control.c): its length brought down to
0.97, a full diagonal's too (the touch controls' stick gives a square's,
up to 1.41 long) */
static void crouch_walk(
	real *forward,
	real *strafe)
{
	real length = square_root(*forward * *forward + *strafe * *strafe);

	if (length > 0.97f)
	{
		*forward *= 0.97f / length;
		*strafe *= 0.97f / length;
	}
	return;
}

/* port: input.crouch "toggle": a crouch let go within CROUCH_TAP_MS of
its press stays on until crouch is pressed again (that press stands up),
Jump is pressed, or the player dies or takes a seat
(input_abstraction_crouch_release); one held longer crouches while it is
held, as "hold" does. The touch controls' own toggle holds the button for
as long as it is on, so it is a hold here, unchanged */
#define CROUCH_TAP_MS 300

static struct
{
	boolean latched;
	/* the press that stood a latched crouch up: no crouch until let go */
	boolean standing;
	boolean down;
	boolean jump_down;
	long down_time;
} crouch_toggles[MAXIMUM_GAMEPADS];

static boolean crouch_toggles_on(
	void)
{
	extern const char *config_string(const char *name);
	extern unsigned long config_changes(void);
	static boolean on;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		const char *value = config_string("input.crouch");

		read_at = config_changes();
		on = value && !strcmp(value, "toggle");
	}
	return on;
}

void input_abstraction_crouch_release(
	long controller_index)
{
	if (controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS)
		crouch_toggles[controller_index].latched = FALSE;
	return;
}

/* port: crouch after every device has been read: input.crouch's toggle,
then any crouch kept below the stand-up push (crouch_walk) */
static void crouch_controls_update(
	long controller_index,
	struct game_input_state *state)
{
	boolean down = state->buttons[_game_control_crouch] != 0;
	boolean jump_down = state->buttons[_game_control_jump] != 0;

	if (crouch_toggles_on())
	{
		long now = (long)system_milliseconds();

		if (down && !crouch_toggles[controller_index].down)
		{
			crouch_toggles[controller_index].standing = crouch_toggles[controller_index].latched;
			crouch_toggles[controller_index].latched = FALSE;
			crouch_toggles[controller_index].down_time = now;
		}
		else if (!down && crouch_toggles[controller_index].down)
		{
			if (!crouch_toggles[controller_index].standing &&
				now - crouch_toggles[controller_index].down_time < CROUCH_TAP_MS)
			{
				crouch_toggles[controller_index].latched = TRUE;
			}
			crouch_toggles[controller_index].standing = FALSE;
		}
		if (jump_down && !crouch_toggles[controller_index].jump_down)
			crouch_toggles[controller_index].latched = FALSE;
		if (crouch_toggles[controller_index].standing)
			state->buttons[_game_control_crouch] = 0;
		else if (crouch_toggles[controller_index].latched && !down)
			state->buttons[_game_control_crouch] = 1;
	}
	else
	{
		crouch_toggles[controller_index].latched = FALSE;
		crouch_toggles[controller_index].standing = FALSE;
	}
	crouch_toggles[controller_index].down = down;
	crouch_toggles[controller_index].jump_down = jump_down;
	if (state->buttons[_game_control_crouch])
		crouch_walk(&state->forward_movement, &state->strafe);
	return;
}

/* the keys held as a game input state's (the keyboard's ticks counted as
keyboard_controls_update counted them) */
static void keyboard_controls_apply(
	long controller_index,
	unsigned long held,
	struct game_input_state *state)
{
	long control_index;
	long x, y;

	for (control_index = 0; control_index < NUMBER_OF_GAME_CONTROLS; control_index++)
	{
		if (keyboard_game_controls[control_index] < 0)
			continue;
		state->buttons[control_index] = MAX(state->buttons[control_index],
			keyboard_controls[controller_index].ticks[control_index]);
	}
	x = TEST_FLAG(held, HALO_KEYBOARD_STRAFE_RIGHT) - TEST_FLAG(held, HALO_KEYBOARD_STRAFE_LEFT);
	y = TEST_FLAG(held, HALO_KEYBOARD_MOVE_FORWARD) - TEST_FLAG(held, HALO_KEYBOARD_MOVE_BACKWARD);
	if (x || y)
	{
		/* (diagonals on the unit circle) */
		real length = x && y ? 0.70710678f : 1.f;

		/* port: crouched, below the full stick's 0.98 that stands a grounded
		biped up (player_control.c), as a stick's crouch walk is */
		if (TEST_FLAG(held, HALO_KEYBOARD_CROUCH))
			length *= 0.97f;

		state->forward_movement = y * length;
		state->strafe = -x * length;
	}
	{
		/* port: the touch controls' stick (port/linux/src/xinput_sdl.c)
		moves the player whatever the profile's sticks do, as the keys do */
		extern int halo_linux_touch_move(short controller_index, real *forward, real *strafe);
		real forward;
		real strafe;

		if (halo_linux_touch_move((short)controller_index, &forward, &strafe))
		{
			state->forward_movement = forward;
			state->strafe = strafe;
		}
	}
	return;
}

static void keyboard_controls_update(
	long controller_index,
	struct game_input_state *state)
{
	unsigned long held = halo_keyboard_actions((short)controller_index);
	long control_index;

	for (control_index = 0; control_index < NUMBER_OF_GAME_CONTROLS; control_index++)
	{
		if (keyboard_game_controls[control_index] < 0)
			continue;
		keyboard_hold_ticks(
			&keyboard_controls[controller_index].ticks[control_index],
			&keyboard_controls[controller_index].down_times[control_index],
			TEST_FLAG(held, keyboard_game_controls[control_index]));
	}
	keyboard_hold_ticks(
		&keyboard_controls[controller_index].reload_ticks,
		&keyboard_controls[controller_index].reload_down_time,
		TEST_FLAG(held, HALO_KEYBOARD_RELOAD));
	keyboard_controls_apply(controller_index, held, state);
	return;
}

/* port: whether the controller's player holds the keyboard's reload key, and
for how many ticks its jump key (which skips cutscenes, as the
controller's A does) */
boolean input_abstraction_port_reload(
	short controller_index)
{
	return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS &&
		keyboard_controls[controller_index].reload_ticks != 0;
}

/* port: the keyboard's action (halo_keyboard.h) for a game control, or for
the game control the local player's profile puts on a controller button
(game_control NONE), as the HUD's prompts name it (port/linux/game/
hud_key_prompts.c); NONE for one the keyboard has no action for */
short input_abstraction_port_keyboard_action(
	short local_player_index,
	short game_control,
	short gamepad_button)
{
	if (game_control == NONE &&
		local_player_index >= 0 && local_player_index < MAXIMUM_GAMEPADS)
	{
		long control_index;

		for (control_index = 0; control_index < NUMBER_OF_GAME_CONTROLS; control_index++)
		{
			if (input_abstraction_globals.player_control_preferences[local_player_index].
				game_control_to_xbox_buttons[control_index] == gamepad_button)
			{
				game_control = (short)control_index;
				break;
			}
		}
	}
	if (game_control == _game_control_start)
		return HALO_KEYBOARD_PAUSE;
	if (game_control == _game_control_back)
		return HALO_KEYBOARD_SCOREBOARD;
	if (game_control < 0 || game_control >= NUMBER_OF_GAME_CONTROLS)
		return NONE;
	return keyboard_game_controls[game_control];
}

boolean input_abstraction_port_action_only(
	short controller_index)
{
	return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS &&
		keyboard_controls[controller_index].ticks[_game_control_action] != 0;
}

byte input_abstraction_port_accept(
	short controller_index)
{
	return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS ?
		keyboard_controls[controller_index].ticks[_game_control_jump] : 0;
}

/* port: how far the controller's player pulls the fire control, which a
trigger with an analog rate of fire reads (weapons.c): a game input state's
buttons count the ticks a control is held, not its pressure, so a click
reads as no pull at all. The keyboard and mouse pull all the way. */
static real primary_trigger_pull(
	short controller_index,
	struct gamepad_state const *gamepad)
{
	short button_index;

	if (keyboard_controls[controller_index].ticks[_game_control_primary_trigger])
		return 1.f;
	if (!gamepad)
		return 0.f;
	button_index = input_abstraction_globals.player_control_preferences[controller_index].game_control_to_xbox_buttons[_game_control_primary_trigger];
	if (button_index < NUMBER_OF_GAMEPAD_ANALOG_BUTTONS)
		return gamepad->analog_buttons[button_index] * (1.f / 255.f);
	return gamepad->buttons[button_index] ? 1.f : 0.f;
}

real input_abstraction_port_primary_trigger(
	short controller_index)
{
	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS)
		return 0.f;
	return primary_trigger_pull(controller_index, input_get_gamepad_state(controller_index));
}

/* port: the game control on each of the controller's buttons by its
profile's mapping (NONE for none), numbered as the gamepad's buttons are
(input.h): the touch controls name their buttons by it (touch_game.c) */
void input_abstraction_port_button_controls(
	short controller_index,
	short *controls)
{
	short button_index;
	short control_index;

	for (button_index = 0; button_index < NUMBER_OF_GAMEPAD_BUTTONS; button_index++)
	{
		controls[button_index] = NONE;
	}
	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS)
		return;
	for (control_index = 0; control_index < NUMBER_OF_GAME_CONTROLS; control_index++)
	{
		byte button_index_bound = input_abstraction_globals.player_control_preferences[controller_index].
			game_control_to_xbox_buttons[control_index];

		if (button_index_bound < NUMBER_OF_GAMEPAD_BUTTONS && controls[button_index_bound] == NONE)
		{
			controls[button_index_bound] = control_index;
		}
	}
}

/* port: whether the controller's player looks inverted, as its right stick
does (input_abstraction_update): its profile's invert, or flying an aircraft
with its profile's aircraft invert; the touch controls' swipe follows it
(player_control.c) */
boolean input_abstraction_port_look_inverted(
	short controller_index)
{
	struct game_input_preferences const *preferences;

	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS)
		return FALSE;
	preferences = &input_abstraction_globals.player_control_preferences[controller_index];
	return preferences->invert_look ||
		(preferences->invert_look_aircraft_control && local_player_is_piloting_aircraft(controller_index));
}

/* ---------- public code */

void input_abstraction_initialize(
	void)
{
	long controller_index;

	csmemset(
		&input_abstraction_globals,
		0,
		offsetof(struct input_abstraction_runtime_globals, time_of_first_device_insertion));
	for (controller_index = 0; controller_index < MAXIMUM_GAMEPADS; controller_index++)
	{
		set_default_game_input_preferences(
			&input_abstraction_globals.player_control_preferences[controller_index]);
		input_abstraction_globals.controller_available[controller_index] =
			input_has_gamepad((short)controller_index);
	}
	input_abstraction_reset_controller_detection_timer();
	input_abstraction_globals.initialized = TRUE;

	return;
}

void input_abstraction_dispose(
	void)
{
	csmemset(
		&input_abstraction_globals,
		0,
		offsetof(struct input_abstraction_runtime_globals, time_of_first_device_insertion));

	return;
}

void input_abstraction_reset_controller_detection_timer(
	void)
{
	input_abstraction_globals.device_enumeration_startup_timer = system_milliseconds();

	return;
}

void input_abstraction_get_local_player_preferences(
	short local_player_index,
	struct game_input_preferences *preferences)
{
	match_assert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		495,
		(local_player_index>=0) && (local_player_index<MAXIMUM_GAMEPADS));
	match_assert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		496,
		preferences);

	csmemcpy(
		preferences,
		&input_abstraction_globals.player_control_preferences[local_player_index],
		sizeof(*preferences));

	return;
}

void input_abstraction_update_local_player_preferences(
	short controller_index,
	struct game_input_preferences const *preferences)
{
	match_assert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		507,
		(controller_index>=0) && (controller_index<MAXIMUM_GAMEPADS));
	match_assert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		508,
		preferences);
	match_vassert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		511,
		preferences->game_control_to_xbox_buttons[8] == _gamepad_binary_button_start &&
			preferences->game_control_to_xbox_buttons[9] == _gamepad_binary_button_back,
		"invalid controller preferences; can't remap start & back buttons");

	csmemcpy(
		&input_abstraction_globals.player_control_preferences[controller_index],
		preferences,
		sizeof(*preferences));

	return;
}

struct game_input_state *input_abstraction_get_input_state(
	short local_player_index)
{
	match_assert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		521,
		(local_player_index>=0) && (local_player_index<MAXIMUM_GAMEPADS));

	return &input_abstraction_globals.input_states[local_player_index];
}

void input_abstraction_update_device_changes(
	unsigned long device_change_flags)
{
	if (input_abstraction_globals.initialized)
	{
		if (device_change_flags &&
			(system_milliseconds() - input_abstraction_globals.device_enumeration_startup_timer >= 2000 ||
			(input_abstraction_globals.time_of_first_device_insertion &&
			system_milliseconds() - input_abstraction_globals.time_of_first_device_insertion >= 2000)))
		{
			error(
				_error_silent,
				"stopping bink playback to due to change in input devices");
			bink_playback_stop();
		}

		if ((device_change_flags & 0xFFF000) &&
			!input_abstraction_globals.time_of_first_device_insertion)
		{
			input_abstraction_globals.time_of_first_device_insertion = system_milliseconds();
		}
	}

	return;
}

/* port: a controller's game input state from its gamepad's, by its
profile (input_abstraction_update's, and a sub-step's reading:
input_abstraction_port_substep_state) */
static void input_abstraction_state_from_gamepad(
	long controller_index,
	struct gamepad_state const *gamepad,
	struct game_input_state *state)
{
	real left_angle;
	real right_angle;
	real_point2d left_stick;
	real_point2d right_stick;
	long control_index;
	boolean invert_look;
	/* port: whether the pad's left direction moves (strafes or turns): not
	when the button layout gives it an action (Fishsticks' grenade switch) */
	boolean dpad_left_moves = TRUE;

	{
		real scale;

		left_angle = arctangent(gamepad->sticks[_gamepad_stick_left].y, gamepad->sticks[_gamepad_stick_left].x);
		scale = 1.0 / MAX(fabs(sine(left_angle)), fabs(cosine(left_angle)));
		left_stick.x = PIN(gamepad->sticks[_gamepad_stick_left].x * gamepad_axis_normalization_scale * scale, -1.f, 1.f);
		left_stick.y = PIN(gamepad->sticks[_gamepad_stick_left].y * gamepad_axis_normalization_scale * scale, -1.f, 1.f);
	}
	{
		real scale;

		right_angle = arctangent(gamepad->sticks[_gamepad_stick_right].y, gamepad->sticks[_gamepad_stick_right].x);
		scale = 1.0 / MAX(fabs(sine(right_angle)), fabs(cosine(right_angle)));
		right_stick.x = PIN(gamepad->sticks[_gamepad_stick_right].x * gamepad_axis_normalization_scale * scale, -1.f, 1.f);
		right_stick.y = PIN(gamepad->sticks[_gamepad_stick_right].y * gamepad_axis_normalization_scale * scale, -1.f, 1.f);
	}
	for (control_index = 0; control_index < NUMBER_OF_GAME_CONTROLS; control_index++)
	{
		state->buttons[control_index] =
			gamepad->buttons[input_abstraction_globals.player_control_preferences[controller_index].game_control_to_xbox_buttons[control_index]];
	}

	if (input_abstraction_globals.player_control_preferences[controller_index].joystick_controls == _joystick_controls_legacy ||
		input_abstraction_globals.player_control_preferences[controller_index].joystick_controls == _joystick_controls_legacy_southpaw)
	{
		short left_quadrant = (left_stick.x < 0.f ? 1 : 0) | (left_stick.y < 0.f ? 2 : 0);
		short right_quadrant = (right_stick.x < 0.f ? 1 : 0) | (right_stick.y < 0.f ? 2 : 0);
		real left_difference = left_angle - stick_direction_angles[left_quadrant];
		real right_difference = right_angle - stick_direction_angles[right_quadrant];
		real left_magnitude = square_root(left_stick.x * left_stick.x + left_stick.y * left_stick.y);
		real right_magnitude = square_root(right_stick.x * right_stick.x + right_stick.y * right_stick.y);

		if (fabs(left_difference) < LEFT_STICK_DIAGONAL_SNAP_ANGLE)
		{
			real absolute_angle = fabs(left_angle);

			if (absolute_angle < STICK_DIAGONAL_ANGLE ||
				absolute_angle > STICK_SECOND_QUADRANT_DIAGONAL_ANGLE)
			{
				left_stick.x = (left_stick.x < 0.f ? -1 : 1) * left_magnitude;
				left_stick.y = (left_stick.y < 0.f ? -1 : 1) * left_magnitude *
					(1.0 - fabs(left_difference) * STICK_DIAGONAL_BLEND_SCALE);
			}
			else
			{
				left_stick.y = (left_stick.y < 0.f ? -1 : 1) * left_magnitude;
				left_stick.x = (left_stick.x < 0.f ? -1 : 1) * left_magnitude *
					(1.0 - fabs(left_difference) * STICK_DIAGONAL_BLEND_SCALE);
			}
		}
		else
		{
			if (fabs(left_stick.x) > fabs(left_stick.y))
			{
				left_stick.x = (left_stick.x < 0.f ? -1 : 1) * left_magnitude;
				left_stick.y = 0.f;
			}
			else
			{
				left_stick.y = (left_stick.y < 0.f ? -1 : 1) * left_magnitude;
				left_stick.x = 0.f;
			}
		}

		if (fabs(right_difference) < RIGHT_STICK_DIAGONAL_SNAP_ANGLE)
		{
			real absolute_angle = fabs(right_angle);

			if (absolute_angle < STICK_DIAGONAL_ANGLE ||
				absolute_angle > STICK_SECOND_QUADRANT_DIAGONAL_ANGLE)
			{
				right_stick.x = (right_stick.x < 0.f ? -1 : 1) * right_magnitude;
				right_stick.y = (right_stick.y < 0.f ? -1 : 1) * right_magnitude *
					(1.0 - fabs(right_difference) * STICK_DIAGONAL_BLEND_SCALE);
			}
			else
			{
				right_stick.y = (right_stick.y < 0.f ? -1 : 1) * right_magnitude;
				right_stick.x = (right_stick.x < 0.f ? -1 : 1) * right_magnitude *
					(1.0 - fabs(right_difference) * STICK_DIAGONAL_BLEND_SCALE);
			}
		}
		else
		{
			if (fabs(right_stick.x) > fabs(right_stick.y))
			{
				right_stick.x = (right_stick.x < 0.f ? -1 : 1) * right_magnitude;
				right_stick.y = 0.f;
			}
			else
			{
				right_stick.y = (right_stick.y < 0.f ? -1 : 1) * right_magnitude;
				right_stick.x = 0.f;
			}
		}
	}

	for (control_index = 0; control_index < NUMBER_OF_GAME_CONTROLS; control_index++)
	{
		if (input_abstraction_globals.player_control_preferences[controller_index].game_control_to_xbox_buttons[control_index] ==
			_gamepad_binary_button_dpad_left)
		{
			dpad_left_moves = FALSE;
		}
	}
	invert_look = input_abstraction_globals.player_control_preferences[controller_index].invert_look;
	if (!invert_look && input_abstraction_globals.player_control_preferences[controller_index].invert_look_aircraft_control)
	{
		invert_look = local_player_is_piloting_aircraft((short)controller_index);
	}
	switch (input_abstraction_globals.player_control_preferences[controller_index].joystick_controls)
	{
		case _joystick_controls_default:
			if (dpad_left_moves && gamepad->buttons[_gamepad_binary_button_dpad_left])
			{
				state->strafe = 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_right])
			{
				state->strafe = -1.f;
			}
			else
			{
				state->strafe = -left_stick.x;
			}
			if (gamepad->buttons[_gamepad_binary_button_dpad_up])
			{
				state->forward_movement = 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_down])
			{
				state->forward_movement = -1.f;
			}
			else
			{
				state->forward_movement = left_stick.y;
			}
			state->yaw = -right_stick.x;
			state->pitch = (invert_look ? -1.f : 1.f) * right_stick.y;
			break;
		case _joystick_controls_southpaw:
			if (dpad_left_moves && gamepad->buttons[_gamepad_binary_button_dpad_left])
			{
				state->yaw = 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_right])
			{
				state->yaw = -1.f;
			}
			else
			{
				state->yaw = -left_stick.x;
			}
			if (gamepad->buttons[_gamepad_binary_button_dpad_up])
			{
				state->pitch = invert_look ? -1.f : 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_down])
			{
				state->pitch = invert_look ? 1.f : -1.f;
			}
			else
			{
				state->pitch = (invert_look ? -1.f : 1.f) * left_stick.y;
			}
			state->forward_movement = right_stick.y;
			state->strafe = -right_stick.x;
			break;
		case _joystick_controls_legacy:
			if (dpad_left_moves && gamepad->buttons[_gamepad_binary_button_dpad_left])
			{
				state->yaw = 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_right])
			{
				state->yaw = -1.f;
			}
			else
			{
				state->yaw = -left_stick.x;
			}
			if (gamepad->buttons[_gamepad_binary_button_dpad_up])
			{
				state->forward_movement = 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_down])
			{
				state->forward_movement = -1.f;
			}
			else
			{
				state->forward_movement = left_stick.y;
			}
			state->strafe = -right_stick.x;
			state->pitch = (invert_look ? -1.f : 1.f) * right_stick.y;
			break;
		case _joystick_controls_legacy_southpaw:
			if (dpad_left_moves && gamepad->buttons[_gamepad_binary_button_dpad_left])
			{
				state->strafe = 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_right])
			{
				state->strafe = -1.f;
			}
			else
			{
				state->strafe = -left_stick.x;
			}
			if (gamepad->buttons[_gamepad_binary_button_dpad_up])
			{
				state->pitch = invert_look ? -1.f : 1.f;
			}
			else if (gamepad->buttons[_gamepad_binary_button_dpad_down])
			{
				state->pitch = invert_look ? 1.f : -1.f;
			}
			else
			{
				state->pitch = (invert_look ? -1.f : 1.f) * left_stick.y;
			}
			state->forward_movement = right_stick.y;
			state->yaw = -right_stick.x;
			break;
		default:
			error(_error_silent, "unknown joystick preset");
			break;
	}

	return;
}

void input_abstraction_update(
	void)
{
	long controller_index;

	TAG_BLOCK_GET_ELEMENT(
		&scenario_get_game_globals()->player_control,
		0,
		struct game_globals_player_control);
	for (controller_index = 0; controller_index < MAXIMUM_GAMEPADS; controller_index++)
	{
		struct gamepad_state const *gamepad = input_get_gamepad_state((short)controller_index);

		if (gamepad)
		{
			struct game_input_state *state = &input_abstraction_globals.input_states[controller_index];

			player_look_yaw_rate[controller_index] = input_abstraction_globals.player_control_preferences[controller_index].yaw_rate;
			player_look_pitch_rate[controller_index] = input_abstraction_globals.player_control_preferences[controller_index].pitch_rate;
			input_abstraction_state_from_gamepad(controller_index, gamepad, state);
			keyboard_controls_update(controller_index, state);
			crouch_controls_update(controller_index, state); /* port */
			input_abstraction_globals.controller_available[controller_index] = TRUE;
		}
		else
		{
			if (input_abstraction_globals.controller_available[controller_index])
			{
				short error_controller = (short)controller_index;
				short error_code;
				boolean pause_game;
				boolean show_error = TRUE;

				if (main_menu_is_active())
				{
					long available_controllers = 0;
					long index = 0;

					do
					{
						if (input_abstraction_globals.controller_available[index])
						{
							available_controllers++;
						}
						index++;
					}
					while (index < MAXIMUM_GAMEPADS);
					pause_game = FALSE;
					error_code = _error_controller_unplugged;
					if (available_controllers >= 2)
					{
						if (player_ui_get_single_player_local_player_controller(0) != controller_index &&
							player_ui_get_single_player_local_player_controller(1) != controller_index &&
							player_ui_get_single_player_local_player_controller(2) != controller_index &&
							player_ui_get_single_player_local_player_controller(3) != controller_index &&
							!player_ui_local_player_wants_to_play_multiplayer((short)controller_index))
						{
							show_error = FALSE;
						}
					}
					else
					{
						if (player_ui_get_single_player_local_player_controller(0) != controller_index &&
							player_ui_get_single_player_local_player_controller(1) != controller_index &&
							player_ui_get_single_player_local_player_controller(2) != controller_index &&
							player_ui_get_single_player_local_player_controller(3) != controller_index &&
							!player_ui_local_player_wants_to_play_multiplayer((short)controller_index))
						{
							error_controller = NONE;
						}
					}
				}
				else if (global_network_game_client_get())
				{
					pause_game = FALSE;
					error_code = _error_controller_unplugged;
					show_error = local_player_exists(controller_index);
				}
				else
				{
					pause_game = TRUE;
					error_code = _error_controller_unplugged_start_to_continue;
					show_error = local_player_exists(controller_index);
				}
				if (show_error == TRUE)
				{
					if (virtual_keyboard_active())
					{
						virtual_keyboard_close();
					}
					display_error_deferred(error_code, error_controller, pause_game, pause_game);
				}
			}
			input_abstraction_globals.controller_available[controller_index] = FALSE;
		}
	}

	return;
}

/* port: a sub-step's reading of a controller (port/linux/game/sim_rate.h,
player_control.c): its game input state at the running sub-step's start,
and how far its fire control is pulled, from the controller read again then
(halo_linux_substep_gamepad, port/linux/src/xinput_sdl.c) as input_xbox.c
reads one, the keys as the frame counted them. FALSE, and nothing written,
when the reading is the frame's: the devices are read once a frame and a
tick's sub-steps run within one, so only what the scripted player plays
(debug.test_input "script:", on the simulation's clock) changes there. A
button down that the frame saw down keeps the frame's count of the ticks it
has been held; one the frame saw up counts 1, as a frame counts a press;
one let go, 0. Nothing the frame keeps is changed: the next frame reads the
controller as it would have. */
boolean input_abstraction_port_substep_state(
	short controller_index,
	struct game_input_state *state,
	real *primary_trigger)
{
	extern int halo_linux_substep_gamepad(short gamepad_index, XINPUT_GAMEPAD *pad);
	struct gamepad_state const *frame;
	struct gamepad_state gamepad;
	XINPUT_GAMEPAD pad;
	short button_index;

	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS)
		return FALSE;
	frame = input_get_gamepad_state(controller_index);
	if (!frame || !halo_linux_substep_gamepad(controller_index, &pad))
		return FALSE;
	gamepad = *frame;
	/* (an XInput reading's analog buttons and its digital buttons' bits are
	in the order of the gamepad's: xdk_xbox.h, input_xbox.c's tables) */
	for (button_index = 0; button_index < NUMBER_OF_GAMEPAD_BUTTONS; button_index++)
	{
		boolean down;
		byte *ticks = &gamepad.buttons[button_index];

		if (button_index < NUMBER_OF_GAMEPAD_ANALOG_BUTTONS)
		{
			gamepad.analog_buttons[button_index] = pad.bAnalogButtons[button_index];
			down = gamepad.analog_buttons[button_index] > frame->analog_button_thresholds[button_index];
		}
		else
		{
			down = TEST_FLAG(pad.wButtons, button_index - FIRST_GAMEPAD_BINARY_BUTTON);
		}
		if (!down)
			*ticks = 0;
		else if (!*ticks)
			*ticks = 1;
	}
	gamepad.sticks[_gamepad_stick_left].x = fix_dead_zone(pad.sThumbLX, SUBSTEP_GAMEPAD_STICK_DEAD_RANGE);
	gamepad.sticks[_gamepad_stick_left].y = fix_dead_zone(pad.sThumbLY, SUBSTEP_GAMEPAD_STICK_DEAD_RANGE);
	gamepad.sticks[_gamepad_stick_right].x = fix_dead_zone(pad.sThumbRX, SUBSTEP_GAMEPAD_STICK_DEAD_RANGE);
	gamepad.sticks[_gamepad_stick_right].y = fix_dead_zone(pad.sThumbRY, SUBSTEP_GAMEPAD_STICK_DEAD_RANGE);
	input_abstraction_state_from_gamepad(controller_index, &gamepad, state);
	keyboard_controls_apply(controller_index, halo_keyboard_actions(controller_index), state);
	*primary_trigger = primary_trigger_pull(controller_index, &gamepad);
	return TRUE;
}

/* ---------- private code */

static boolean local_player_is_piloting_aircraft(
	short controller_index)
{
	boolean piloting_aircraft = FALSE;
	long player_index;

	match_assert(
		"c:\\halo\\SOURCE\\input\\input_abstraction.c",
		569,
		(controller_index>=0) && (controller_index<MAXIMUM_NUMBER_OF_LOCAL_PLAYERS));
	player_index = local_player_get_player_index(controller_index);
	if (player_index != NONE)
	{
		struct player_datum *player = player_try_and_get(player_index);

		if (player)
		{
			struct unit_datum *unit = unit_try_and_get(player->unit_index);

			if (unit && unit->object.parent_object_index != NONE && unit->unit.parent_seat_index != NONE)
			{
				struct unit_datum *vehicle = vehicle_get(unit->object.parent_object_index);
				struct vehicle_definition *definition = vehicle_specific_definition_get(vehicle->definition_index);

				if (definition->vehicle_type == _vehicle_type_human_plane ||
					definition->vehicle_type == _vehicle_type_alien_fighter)
				{
					struct unit_seat *seat = TAG_BLOCK_GET_ELEMENT(
						&definition->unit.unit.seats,
						unit->unit.parent_seat_index,
						struct unit_seat);

					if (TEST_FLAG(seat->flags, _unit_seat_driver_bit))
					{
						piloting_aircraft = TRUE;
					}
				}
			}
		}
	}

	return piloting_aircraft;
}

static void set_default_game_input_preferences(
	struct game_input_preferences *preferences)
{
	preferences->pitch_rate = 60.f;
	preferences->yaw_rate = 120.f;
	preferences->game_control_to_xbox_buttons[_game_control_jump] = _gamepad_analog_button_a;
	preferences->game_control_to_xbox_buttons[_game_control_switch_grenades] = _gamepad_analog_button_black;
	preferences->game_control_to_xbox_buttons[_game_control_action] = _gamepad_analog_button_x;
	preferences->game_control_to_xbox_buttons[_game_control_switch_weapons] = _gamepad_analog_button_y;
	preferences->game_control_to_xbox_buttons[_game_control_melee] = _gamepad_analog_button_b;
	preferences->game_control_to_xbox_buttons[_game_control_flashlight] = _gamepad_analog_button_white;
	preferences->game_control_to_xbox_buttons[_game_control_grenade] = _gamepad_analog_button_left_trigger;
	preferences->game_control_to_xbox_buttons[_game_control_primary_trigger] = _gamepad_analog_button_right_trigger;
	preferences->game_control_to_xbox_buttons[_game_control_start] = _gamepad_binary_button_start;
	preferences->game_control_to_xbox_buttons[_game_control_back] = _gamepad_binary_button_back;
	preferences->game_control_to_xbox_buttons[_game_control_crouch] = _gamepad_binary_button_left_thumb;
	preferences->game_control_to_xbox_buttons[_game_control_zoom] = _gamepad_binary_button_right_thumb;
	preferences->joystick_controls = _joystick_controls_default;
	preferences->invert_look = FALSE;
	preferences->invert_look_aircraft_control = FALSE;

	return;
}
