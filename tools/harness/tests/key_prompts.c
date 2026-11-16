/*
KEY_PROMPTS.C (test)

The HUD's button prompts for the keyboard and mouse (input.button_prompts):
which device the prompts follow (port/linux/src/xinput_sdl.c:
prompt_device_next, prompts_show_keys), the keyboard's action for a prompt's
game control or controller button (source/input/input_abstraction.c:
input_abstraction_port_keyboard_action, with the HUD's own icon to game
control table, source/interface/hud_messaging.c's button_mappings), and the
text a prompt shows (port/linux/game/hud_key_prompts.c: hud_key_prompt_text).
test_key_prompts.py takes the enums and tables (config.inc) and the code
(under_test.inc).
*/

#include "harness.h"

#include <wchar.h>

typedef int BOOL;

#define MAXIMUM_GAMEPADS 4
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define csstrcpy strcpy

#include "config.inc"

static struct
{
	struct game_input_preferences player_control_preferences[MAXIMUM_GAMEPADS];
} input_abstraction_globals;

/* the bindings' names, as xinput_sdl.c's halo_keyboard_action_name gives them */
static char binding_names[NUMBER_OF_HALO_KEYBOARD_ACTIONS][32];

static int halo_keyboard_action_name(int action, char *name, unsigned long size)
{
	name[0] = 0;
	if (action < 0 || action >= NUMBER_OF_HALO_KEYBOARD_ACTIONS)
		return 0;
	snprintf(name, size, "%s", binding_names[action]);
	return name[0] != 0;
}

/* the prompts' device (xinput_sdl.c's halo_keyboard_prompts: the keyboard's
keys, else the controller's buttons), and whether the map's message icons
have a picture of a button (hud_key_prompts.c's button_sprite_drawn) */
static int keyboard_prompts;
static boolean back_sprite;

static int halo_keyboard_prompts(void)
{
	return keyboard_prompts;
}

static boolean button_sprite_drawn(short gamepad_button)
{
	return gamepad_button == _icon_back_button && back_sprite;
}

#include "under_test.inc"

static void default_bindings(void)
{
	static char const *const names[NUMBER_OF_HALO_KEYBOARD_ACTIONS] =
	{
		[HALO_KEYBOARD_MOVE_FORWARD] = "W", [HALO_KEYBOARD_MOVE_BACKWARD] = "S",
		[HALO_KEYBOARD_STRAFE_LEFT] = "A", [HALO_KEYBOARD_STRAFE_RIGHT] = "D",
		[HALO_KEYBOARD_JUMP] = "Space", [HALO_KEYBOARD_CROUCH] = "C", [HALO_KEYBOARD_FIRE] = "Left Click",
		[HALO_KEYBOARD_THROW_GRENADE] = "G", [HALO_KEYBOARD_MELEE] = "F", [HALO_KEYBOARD_RELOAD] = "R",
		[HALO_KEYBOARD_ZOOM] = "Z", [HALO_KEYBOARD_SWITCH_WEAPON] = "1", [HALO_KEYBOARD_SWITCH_GRENADE] = "X",
		[HALO_KEYBOARD_ACTION] = "E", [HALO_KEYBOARD_FLASHLIGHT] = "Q", [HALO_KEYBOARD_SCOREBOARD] = "Tab",
		[HALO_KEYBOARD_PAUSE] = "Escape",
	};
	short action;

	for (action = 0; action < NUMBER_OF_HALO_KEYBOARD_ACTIONS; action++)
		snprintf(binding_names[action], sizeof(binding_names[action]), "%s", names[action] ? names[action] : "");
}

static void default_preferences(void)
{
	short index;

	memset(&input_abstraction_globals, 0, sizeof(input_abstraction_globals));
	for (index = 0; index < MAXIMUM_GAMEPADS; index++)
		set_default_game_input_preferences(&input_abstraction_globals.player_control_preferences[index]);
}

/* the keyboard's action for one of the HUD's game control icons, as
hud_messaging.c passes it */
static short icon_action(short icon)
{
	return input_abstraction_port_keyboard_action(0, button_mappings[icon - _icon_action], NONE);
}

static void check_text(short game_control, short button, wchar_t const *expected)
{
	wchar_t text[64];
	boolean shown = hud_key_prompt_text(0, game_control, button, text, NUMBEROF(text));

	if (!expected)
	{
		CHECK(!shown, "control %d, button %d shows a key", game_control, button);
		return;
	}
	CHECK(shown, "control %d, button %d shows no key", game_control, button);
	CHECK(!wcscmp(text, expected), "control %d, button %d shows %ls, not %ls", game_control, button, text, expected);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	default_bindings();
	default_preferences();
	CASE("device")
	{
		/* the last device used decides; a reading with both, the controller */
		CHECK(prompt_device_next(PROMPT_DEVICE_NONE, TRUE, FALSE) == PROMPT_DEVICE_KEYBOARD, "keys used");
		CHECK(prompt_device_next(PROMPT_DEVICE_KEYBOARD, FALSE, TRUE) == PROMPT_DEVICE_CONTROLLER, "pad used");
		CHECK(prompt_device_next(PROMPT_DEVICE_CONTROLLER, TRUE, FALSE) == PROMPT_DEVICE_KEYBOARD, "keys again");
		CHECK(prompt_device_next(PROMPT_DEVICE_KEYBOARD, TRUE, TRUE) == PROMPT_DEVICE_CONTROLLER, "both");
		CHECK(prompt_device_next(PROMPT_DEVICE_KEYBOARD, FALSE, FALSE) == PROMPT_DEVICE_KEYBOARD, "idle keeps");
		CHECK(prompt_device_next(PROMPT_DEVICE_CONTROLLER, FALSE, FALSE) == PROMPT_DEVICE_CONTROLLER, "idle keeps");
		/* "auto" follows it; before either is used, the keys unless a pad
		is connected or the touch controls are there */
		CHECK(prompts_show_keys("auto", PROMPT_DEVICE_KEYBOARD, TRUE, TRUE), "auto, keys used");
		CHECK(!prompts_show_keys("auto", PROMPT_DEVICE_CONTROLLER, FALSE, FALSE), "auto, pad used");
		CHECK(prompts_show_keys("auto", PROMPT_DEVICE_NONE, FALSE, FALSE), "auto, a desktop with no pad");
		CHECK(!prompts_show_keys("auto", PROMPT_DEVICE_NONE, TRUE, FALSE), "auto, a pad connected");
		CHECK(!prompts_show_keys("auto", PROMPT_DEVICE_NONE, FALSE, TRUE), "auto, the touch controls");
		CHECK(!prompts_show_keys("controller", PROMPT_DEVICE_KEYBOARD, FALSE, FALSE), "controller");
		CHECK(prompts_show_keys("keyboard", PROMPT_DEVICE_CONTROLLER, TRUE, TRUE), "keyboard");
		CHECK(!prompts_show_keys("", PROMPT_DEVICE_KEYBOARD, FALSE, FALSE), "an unknown setting is stock");
		return 0;
	}
	CASE("icons")
	{
		/* the icons the HUD names a game control by, as the keys name it */
		CHECK(icon_action(_icon_action) == HALO_KEYBOARD_ACTION, "action");
		CHECK(icon_action(_icon_throw_grenade) == HALO_KEYBOARD_THROW_GRENADE, "throw grenade");
		CHECK(icon_action(_icon_primary_trigger) == HALO_KEYBOARD_FIRE, "primary trigger");
		CHECK(icon_action(_icon_integrated_light) == HALO_KEYBOARD_FLASHLIGHT, "flashlight");
		CHECK(icon_action(_icon_jump) == HALO_KEYBOARD_JUMP, "jump");
		CHECK(icon_action(_icon_use_equipment) == HALO_KEYBOARD_MELEE, "use equipment (melee)");
		CHECK(icon_action(_icon_rotate_weapons) == HALO_KEYBOARD_SWITCH_WEAPON, "rotate weapons");
		CHECK(icon_action(_icon_rotate_grenades) == HALO_KEYBOARD_SWITCH_GRENADE, "rotate grenades");
		return 0;
	}
	CASE("buttons")
	{
		short icon;

		/* a controller button is the game control the profile puts on it */
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_x_button) == HALO_KEYBOARD_ACTION, "X");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_y_button) == HALO_KEYBOARD_SWITCH_WEAPON, "Y");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_white_button) == HALO_KEYBOARD_FLASHLIGHT,
			"white");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_start_button) == HALO_KEYBOARD_PAUSE, "start");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_back_button) == HALO_KEYBOARD_SCOREBOARD,
			"back");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_dpad_up) == NONE, "the D-pad has no key");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, NONE) == NONE, "no button");
		CHECK(input_abstraction_port_keyboard_action(0, NUMBER_OF_GAME_CONTROLS, NONE) == NONE, "no control");
		/* the key of each control icon is the key of the button the
		controller's prompt draws for it */
		for (icon = _icon_action; icon <= _icon_zoom; icon++)
		{
			short button = input_abstraction_globals.player_control_preferences[0].game_control_to_xbox_buttons[
				(int)button_mappings[icon - _icon_action]];

			CHECK(icon_action(icon) == input_abstraction_port_keyboard_action(0, NONE, button),
				"icon %d: key %d, its button %d's %d", icon, icon_action(icon), button,
				input_abstraction_port_keyboard_action(0, NONE, button));
		}
		return 0;
	}
	CASE("remapped")
	{
		/* a profile's layout moves the button, not the key */
		struct game_input_preferences *preferences = &input_abstraction_globals.player_control_preferences[0];

		preferences->game_control_to_xbox_buttons[_game_control_jump] = _gamepad_analog_button_b;
		preferences->game_control_to_xbox_buttons[_game_control_melee] = _gamepad_analog_button_a;
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_b_button) == HALO_KEYBOARD_JUMP, "B jumps");
		CHECK(input_abstraction_port_keyboard_action(0, NONE, _icon_a_button) == HALO_KEYBOARD_MELEE, "A melees");
		CHECK(icon_action(_icon_jump) == HALO_KEYBOARD_JUMP, "the jump icon is still jump's key");
		CHECK(input_abstraction_port_keyboard_action(1, NONE, _icon_a_button) == HALO_KEYBOARD_JUMP,
			"another player's layout");
		return 0;
	}
	CASE("text")
	{
		check_text(button_mappings[_icon_action - _icon_action], NONE, L"E");
		check_text(button_mappings[_icon_primary_trigger - _icon_action], NONE, L"Left Click");
		check_text(NONE, _icon_x_button, L"E");
		check_text(NONE, _icon_start_button, L"Escape");
		/* moving and looking */
		check_text(NONE, HUD_KEY_PROMPT_LEFT_STICK, L"WASD");
		check_text(NONE, HUD_KEY_PROMPT_RIGHT_STICK, L"Mouse");
		snprintf(binding_names[HALO_KEYBOARD_MOVE_FORWARD], 32, "Up");
		snprintf(binding_names[HALO_KEYBOARD_STRAFE_LEFT], 32, "Left");
		snprintf(binding_names[HALO_KEYBOARD_MOVE_BACKWARD], 32, "Down");
		snprintf(binding_names[HALO_KEYBOARD_STRAFE_RIGHT], 32, "Right");
		check_text(NONE, HUD_KEY_PROMPT_LEFT_STICK, L"Up/Left/Down/Right");
		/* an action with no key is named Unbound, never the controller's
		button (the keyboard and mouse are what the player holds) */
		binding_names[HALO_KEYBOARD_STRAFE_RIGHT][0] = 0;
		check_text(NONE, HUD_KEY_PROMPT_LEFT_STICK, L"Up/Left/Down/Unbound");
		binding_names[HALO_KEYBOARD_MOVE_FORWARD][0] = 0;
		binding_names[HALO_KEYBOARD_STRAFE_LEFT][0] = 0;
		binding_names[HALO_KEYBOARD_MOVE_BACKWARD][0] = 0;
		check_text(NONE, HUD_KEY_PROMPT_LEFT_STICK, L"Unbound");
		binding_names[HALO_KEYBOARD_FLASHLIGHT][0] = 0;
		check_text(button_mappings[_icon_integrated_light - _icon_action], NONE, L"Unbound");
		binding_names[HALO_KEYBOARD_FIRE][0] = 0;
		check_text(button_mappings[_icon_primary_trigger - _icon_action], NONE, L"Unbound");
		check_text(NONE, _icon_right_trigger, L"Unbound");
		/* what the keyboard has no action for keeps the button */
		check_text(NONE, NONE, NULL);
		check_text(NONE, _icon_dpad_left, NULL);
		/* a Latin-1 label (a German keyboard's) */
		snprintf(binding_names[HALO_KEYBOARD_ACTION], 32, "%c", 0xD6);
		check_text(button_mappings[_icon_action - _icon_action], NONE, L"Ö");
		/* the sticks' icons are the HUD's */
		CHECK(HUD_KEY_PROMPT_LEFT_STICK == _icon_left_stick && HUD_KEY_PROMPT_RIGHT_STICK == _icon_right_stick,
			"the sticks' icons moved");
		return 0;
	}
	CASE("score-hint")
	{
		/* the multiplayer start's "Hold BACK for score" (the map's string;
		a Custom Edition map's 'Hold "%s" for score' converted to it): its
		BACK the Back button's icon, which the HUD draws as the prompts draw
		a button, and Back is the scoreboard's key on the keyboard */
		wchar_t text[64] = L"Hold BACK for score";
		wchar_t tight[20] = L"Hold BACK for score";
		wchar_t other[64] = L"FALLBACK BACKPACK BACK";
		wchar_t shown[64];

		hud_key_prompt_button_word(text, NUMBEROF(text), L"BACK", L"%back-button");
		CHECK(!wcscmp(text, L"Hold %back-button for score"), "%ls", text);
		/* (a word that only starts with it is no button; one that does not
		fit is left as it is) */
		hud_key_prompt_button_word(other, NUMBEROF(other), L"BACK", L"%back-button");
		CHECK(!wcscmp(other, L"FALLBACK BACKPACK %back-button"), "%ls", other);
		hud_key_prompt_button_word(tight, NUMBEROF(tight), L"BACK", L"%back-button");
		CHECK(!wcscmp(tight, L"Hold BACK for score"), "%ls", tight);
		CHECK(hud_key_prompt_text(0, NONE, _icon_back_button, shown, NUMBEROF(shown)) && !wcscmp(shown, L"Tab"),
			"Back's key on the keyboard: %ls", shown);
		return 0;
	}
	CASE("score-hint-controller")
	{
		/* the score hint's BACK made the icon only where the HUD draws
		something for it: the keyboard's key, or a picture of Back, which
		the Xbox's message icons have not (the word stays, as the Xbox
		wrote it) */
		wchar_t text[64];

		CHECK(HUD_KEY_PROMPT_BACK_BUTTON == _icon_back_button, "the Back button's icon moved");
		keyboard_prompts = 1;
		wcscpy(text, L"Hold BACK for score");
		hud_key_prompt_score_hint(0, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"Hold %back-button for score"), "keyboard: %ls", text);
		/* (a second player's prompts are never keys) */
		wcscpy(text, L"Hold BACK for score");
		hud_key_prompt_score_hint(1, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"Hold BACK for score"), "second player: %ls", text);
		keyboard_prompts = 0;
		wcscpy(text, L"Hold BACK for score");
		hud_key_prompt_score_hint(0, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"Hold BACK for score"), "controller: %ls", text);
		back_sprite = TRUE;
		wcscpy(text, L"Hold BACK for score");
		hud_key_prompt_score_hint(0, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"Hold %back-button for score"), "controller, a picture of Back: %ls", text);
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
