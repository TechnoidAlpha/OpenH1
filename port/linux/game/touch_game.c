/*
TOUCH_GAME.C

What the touch controls (port/linux/src/touch_input.c) need to know of the
game. The platform layer cannot see the game's types, so it asks here
(this file is compiled as the game's own sources are).
*/

#include "cseries.h"
#include "game/game.h"
#include "cutscene/cinematics.h"
#include "input/input.h"
#include "input/input_abstraction.h"
#include "interface/ui_widget.h"
#include "game/players.h"
#include "items/weapons.h"
#include "units/units.h"
#include "network_coop.h"

/* asks whether a cinematic is playing that A would skip
(player_control.c, player_control_action_test_check_reset_input_blob), or
in network co-op vote to skip: a client's own cinematic is never skippable,
as only the host runs the scripts that make it so (network_coop.c); tests
cinematic_globals first because the controller is read before the game exists
during input_initialize and shell_initialize, and game_initialize makes it
after the game time globals that game_in_progress reads (game.c) */
int touch_game_cinematic_skippable(void)
{
	return cinematic_globals && game_in_progress() && cinematic_in_progress() &&
		(cinematic_can_be_skipped() || network_coop_skip_offered());
}

/* asks whether a cinematic is playing, skippable or not: the player has no
controls, so the on-screen touch controls hide (touch_input.c) */
int touch_game_cinematic_playing(void)
{
	return cinematic_globals && game_in_progress() && cinematic_in_progress();
}

/* asks whether a game is being played: not while the game starts, nor
between maps, nor at the main menu (whose ui.map is a game too), where the
on-screen touch controls do not show (touch_input.c); cinematic_globals
first, as touch_game_cinematic_skippable has it */
int touch_game_playing(void)
{
	return cinematic_globals && game_in_progress() && !main_menu_is_active();
}

/* (touch_input.c's TOUCH_BUTTONS and the overlay count 16 buttons) */
typedef char verify_touch_buttons[NUMBER_OF_GAMEPAD_BUTTONS == 16 ? 1 : -1];

/* the game control on each of player 1's 16 controller buttons, as the
gamepad's buttons are numbered (input.h), -1 for none: the touch controls
name their buttons by them, so a profile's own mapping shows */
void touch_game_button_controls(int *controls)
{
	short buttons[NUMBER_OF_GAMEPAD_BUTTONS];
	short index;

	input_abstraction_port_button_controls(0, buttons);
	for (index = 0; index < NUMBER_OF_GAMEPAD_BUTTONS; index++)
	{
		controls[index] = buttons[index];
	}
}

/* ---------- what the buttons show of player 1 (touch_input.c's scene bits) */

enum
{
	/* the HUD prompts an action (pick up a weapon, enter a vehicle, touch a
	device: hud.c, hud_show_action_response): the action button glows */
	_touch_context_action = 1 << 0,
	/* the weapon's magazine wants reloading: the reload button pulses */
	_touch_context_reload = 1 << 1,
	/* none of the current grenade type: the grenade button dims */
	_touch_context_no_grenades = 1 << 2,
	/* in a vehicle's seat: jump and crouch hide, the action button exits */
	_touch_context_vehicle = 1 << 3,
	/* player 1 has a unit (not dead, nor between lives): a crouch tapped
	on stands up without it */
	_touch_context_alive = 1 << 4,
};

/* hud.c's, which weapon_build_weapon_interface_state fills in (weapons.c) */
struct weapon_interface_magazine_state
{
	boolean reloading;
	boolean can_fire;
	short rounds_loaded;
	short rounds_loaded_maximum;
	short rounds_remaining;
	short rounds_remaining_maximum;
};

struct weapon_interface_state
{
	real heat;
	real age;
	boolean overheated;
	byte pad09;
	short magazine_count;
	struct weapon_interface_magazine_state magazines[2];
};

typedef char touch_weapon_interface_state_size_assert[
	sizeof(struct weapon_interface_state) == 0x20 ? 1 : -1];

/* whether a magazine wants reloading: under a quarter loaded (or empty) with
rounds in reserve to load, and not already reloading; never a weapon
without a magazine (a plasma weapon's charge: its maximum is 0) */
int touch_game_reload_due(
	struct weapon_interface_magazine_state const *magazine)
{
	return magazine->rounds_loaded_maximum > 0 && !magazine->reloading &&
		magazine->rounds_remaining > 0 &&
		magazine->rounds_loaded * 4 < magazine->rounds_loaded_maximum;
}

/* player 1's state as the touch controls show it, _touch_context_* bits;
0 while there is no living player (touch_input.c asks only in a game) */
int touch_game_context(void)
{
	long player_index = local_player_get_player_index(0);
	struct player_datum *player;
	struct unit_datum *unit;
	long weapon_index;
	short grenade;
	int context = 0;

	if (player_index == NONE)
		return 0;
	player = player_get(player_index);
	if (player->unit_index == NONE)
		return 0;
	context |= _touch_context_alive;
	unit = unit_get(player->unit_index);
	/* (in a seat the action is to exit, which the vehicle bit shows) */
	if (player->action_result != _player_action_result_reload &&
		player->action_result != _player_action_result_exit_vehicle)
	{
		context |= _touch_context_action;
	}
	if (unit->object.parent_object_index != NONE && unit->unit.parent_seat_index != NONE)
		context |= _touch_context_vehicle;
	weapon_index = unit->unit.current_weapon_index == NONE ? NONE :
		unit_inventory_get_weapon(player->unit_index, unit->unit.current_weapon_index);
	if (weapon_index != NONE)
	{
		struct weapon_interface_state state;

		weapon_build_weapon_interface_state(weapon_index, &state);
		if (state.magazine_count > 0 && touch_game_reload_due(&state.magazines[0]))
			context |= _touch_context_reload;
	}
	grenade = unit->unit.current_grenade_index;
	if (grenade >= 0 && grenade < NUMBER_OF_UNIT_GRENADE_TYPES && unit->unit.grenade_counts[grenade] <= 0)
	{
		context |= _touch_context_no_grenades;
	}
	return context;
}
