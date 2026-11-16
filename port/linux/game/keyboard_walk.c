/*
KEYBOARD_WALK.C

The keys' Walk (controls.walk, Left Shift): while it is held (or toggled on,
input.walk), the movement keys push the player only part of the way, as a
controller's stick pushed a little does. A player shows on the enemies'
motion trackers while the square of its velocity (world units a 30 Hz
tick, whatever game.sim_rate is: the sub-steps move a share of a tick's
velocity, bipeds.c) is at least the HUD globals' motion sensor velocity
sensitivity (motion_sensor.c's should_draw_object), and on the ground a
biped's velocity is its throttle times its speed (biped_player_movement):
so a throttle below the threshold's velocity over the fastest the player
can run keeps it off them, in every direction, as a light push of a stick
does. input.walk_speed "tracker" is that throttle; a percentage, that part
of the full push.

It is only a throttle (source/input/input_abstraction.c's
keyboard_controls_apply): the player's movement as a stick's, sent and
played as any controller's, on the 127 steps a network game sends a
throttle in (network_distributed.c, network_substeps.c), which it is
rounded down to, so that no machine sees it faster than this one.
*/

#include "cseries.h"
#include "keyboard_walk.h"
#include "game/game.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#include "objects/objects.h"
#include "scenario/scenario.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/units.h"

#include <stdlib.h>
#include <string.h>

#include "player_settings.h"

void platform_log(char const *format, ...);

/* the share of the threshold's velocity walked at: below it by enough that
the throttle's steps, the floating point and the ground's own changes of
speed (an acceleration's last step) stay under it */
#define WALK_TRACKER_MARGIN 0.95f
/* a network game's throttle's steps (a stick's length in 127ths) */
#define WALK_THROTTLE_STEPS 127.f

real keyboard_walk_tracker_throttle(
	real velocity_sensitivity,
	real fastest_speed)
{
	real throttle;

	if (!(velocity_sensitivity > 0.f) || !(fastest_speed > 0.f))
		return 1.f;
	throttle = WALK_TRACKER_MARGIN * square_root(velocity_sensitivity) * TICKS_PER_SECOND / fastest_speed;
	throttle = PIN(throttle, 0.f, 1.f);
	return (real)floor(throttle * WALK_THROTTLE_STEPS) / WALK_THROTTLE_STEPS;
}

/* input.walk_speed: a percentage of the full push, else 0 for "tracker" */
static real walk_speed_setting(
	void)
{
	static unsigned long read_at = (unsigned long)-1;
	static real percentage;

	if (read_at != config_changes())
	{
		/* (the keys' player's, player 1's: their profile's) */
		char const *value = config_player_string(0, "input.walk_speed");
		char *end = NULL;
		double number = value ? strtod(value, &end) : 0.0;

		read_at = config_changes();
		percentage = 0.f;
		if (value && end != value && *end == 0 && number >= 1.0 && number <= 100.0)
			percentage = (real)number;
	}
	return percentage;
}

real keyboard_walk_throttle(
	short local_player_index)
{
	struct game_globals *globals;
	struct game_globals_player_information *information;
	struct biped_definition *definition;
	struct player_datum *player;
	struct unit_datum *unit;
	long player_index;
	real percentage = walk_speed_setting();
	real fastest;
	real slope;
	real throttle;
	static real logged_throttle = -1.f;

	player_index = local_player_get_player_index(local_player_index);
	if (player_index == NONE)
		return 1.f;
	player = player_get(player_index);
	unit = player->unit_index != NONE ? unit_try_and_get(player->unit_index) : NULL;
	/* (on foot only: in a seat the keys drive, or turn a turret) */
	if (!unit || unit->object.parent_object_index != NONE || !biped_try_and_get(player->unit_index))
		return 1.f;
	if (percentage > 0.f)
		return (real)floor(percentage / 100.f * WALK_THROTTLE_STEPS) / WALK_THROTTLE_STEPS;

	globals = scenario_get_game_globals();
	if (!hud_globals || !globals || globals->player_information.count < 1)
		return 1.f;
	information = TAG_BLOCK_GET_ELEMENT(&globals->player_information, 0, struct game_globals_player_information);
	definition = biped_definition_get(unit->definition_index);
	/* (the fastest it runs: forward, back or to the side, faster by the game
	type's speed (a player's multiplier, double speed), and down or up a
	slope by its biped's scale of either; slower crouched or stunned) */
	fastest = MAX(information->run_forward_speed,
		MAX(information->run_backward_speed, information->run_sideways_speed));
	if (player->speed_multiplier > 1.f)
		fastest *= player->speed_multiplier;
	if (game_players_are_double_speed() && information->double_speed_multiplier > 1.f)
		fastest *= information->double_speed_multiplier;
	slope = MAX(1.f, MAX(definition->biped.downhill_velocity_scale, definition->biped.uphill_velocity_scale));
	throttle = keyboard_walk_tracker_throttle(hud_globals->defaults.motion_sensor_velocity_sensitivity,
		fastest * slope);
	/* (said in the log as it changes: a map's, a game type's speed) */
	if (throttle != logged_throttle)
	{
		logged_throttle = throttle;
		platform_log("walk: %.4f of a full push (the motion trackers' threshold %.4f world units a tick, "
			"the fastest run %.3f a second)", throttle,
			square_root(hud_globals->defaults.motion_sensor_velocity_sensitivity), fastest * slope);
	}
	return throttle;
}
