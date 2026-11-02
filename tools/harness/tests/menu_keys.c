/*
MENU_KEYS.C (test)

The menus' prompts for the keyboard (ui_widget.c's ui_widget_port_menu_key):
the key a controller button of the menus' text (the scores' "A pick game,
B quit", a screen's "Press A to select") is drawn as while the keyboard and
mouse play must be one that presses that button there
(port/linux/src/xinput_sdl.c's keyboard_gamepad, the controller the keyboard
is in the menus). test_menu_keys.py takes both.
*/

#include "harness.h"

typedef int BOOL;
typedef unsigned char BYTE;
typedef short SHORT;

#include "config.inc"

struct platform_input_state
{
	unsigned char keys[TEST_SCANCODE_COUNT];
	unsigned char mouse_buttons[8];
	BOOL mouse_released, menus;
};
#define SDL_SCANCODE_COUNT TEST_SCANCODE_COUNT
#define SDL_BUTTON_X1 4
typedef struct { unsigned short wButtons; BYTE bAnalogButtons[8]; SHORT sThumbLX, sThumbLY; } XINPUT_GAMEPAD;
static BOOL text_typing, text_typing_enter_armed;
/* whether a server browser is shown (xinput_sdl.c's halo_keyboard_browser_keys) */
static int browser_shown;

static int halo_keyboard_browser_keys_active(void)
{
	return browser_shown;
}

#include "under_test.inc"

/* a key cap's name as the key it names (the names ui_widget_port_menu_key
gives) */
static int scancode(char const *name)
{
	static struct { char const *name; int scancode; } const keys[] =
	{
		{ "Enter", SDL_SCANCODE_RETURN },
		{ "Esc", SDL_SCANCODE_ESCAPE },
		{ "E", SDL_SCANCODE_E },
		{ "F", SDL_SCANCODE_F },
		{ "Q", SDL_SCANCODE_Q },
		{ "F1", SDL_SCANCODE_F1 },
		{ "Tab", SDL_SCANCODE_TAB },
		{ "R", SDL_SCANCODE_R },
		{ "L", SDL_SCANCODE_L },
	};
	size_t index;

	for (index = 0; index < sizeof(keys) / sizeof(keys[0]); index++)
	{
		if (!strcmp(keys[index].name, name))
			return keys[index].scancode;
	}
	return -1;
}

/* whether the pad has a controller button (the HUD's icon of it) pressed */
static boolean pressed(XINPUT_GAMEPAD const *pad, short icon)
{
	switch (icon)
	{
	case _icon_a_button: return pad->bAnalogButtons[XINPUT_GAMEPAD_A] != 0;
	case _icon_b_button: return pad->bAnalogButtons[XINPUT_GAMEPAD_B] != 0;
	case _icon_x_button: return pad->bAnalogButtons[XINPUT_GAMEPAD_X] != 0;
	case _icon_y_button: return pad->bAnalogButtons[XINPUT_GAMEPAD_Y] != 0;
	case _icon_black_button: return pad->bAnalogButtons[XINPUT_GAMEPAD_BLACK] != 0;
	case _icon_white_button: return pad->bAnalogButtons[XINPUT_GAMEPAD_WHITE] != 0;
	case _icon_right_trigger: return pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] != 0;
	case _icon_left_trigger: return pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] != 0;
	case _icon_back_button: return (pad->wButtons & XINPUT_GAMEPAD_BACK) != 0;
	case _icon_start_button: return (pad->wButtons & XINPUT_GAMEPAD_START) != 0;
	default: return FALSE;
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	short icon;

	CASE("keys")
	{
		/* each button named a key: that key, alone, presses it in the menus,
		the other menus' and a server browser's */
		for (browser_shown = 0; browser_shown < 2; browser_shown++)
		{
			for (icon = _icon_a_button; icon <= _icon_right_thumb; icon++)
			{
				char const *name = ui_widget_port_menu_key(icon);
				struct platform_input_state input;
				XINPUT_GAMEPAD pad;
				int key;

				if (!name)
					continue;
				key = scancode(name);
				CHECK(key >= 0, "button %d drawn as %s, no key of the menus", icon, name);
				memset(&input, 0, sizeof(input));
				memset(&pad, 0, sizeof(pad));
				input.menus = TRUE;
				input.keys[key] = 1;
				keyboard_gamepad(&input, &pad);
				CHECK(pressed(&pad, icon), "button %d drawn as %s, which does not press it (in a browser: %d)",
					icon, name, browser_shown);
			}
		}
		return 0;
	}
	CASE("browser")
	{
		/* the other menus' keys as before the browsers': Tab is Y (Profiles'
		new profile), never X (their delete), and E is X; in a browser Tab is
		X (its sort) alone and E is RB alone (its next tab) */
		struct platform_input_state input;
		XINPUT_GAMEPAD pad;
		short shown;

		for (shown = 0; shown < 2; shown++)
		{
			browser_shown = shown;
			memset(&input, 0, sizeof(input));
			memset(&pad, 0, sizeof(pad));
			input.menus = TRUE;
			input.keys[SDL_SCANCODE_TAB] = 1;
			keyboard_gamepad(&input, &pad);
			CHECK(pressed(&pad, _icon_x_button) == shown && pressed(&pad, _icon_y_button) == !shown,
				"Tab in %s presses X %d, Y %d", shown ? "a browser" : "the other menus",
				pressed(&pad, _icon_x_button), pressed(&pad, _icon_y_button));
			memset(&input, 0, sizeof(input));
			memset(&pad, 0, sizeof(pad));
			input.menus = TRUE;
			input.keys[SDL_SCANCODE_E] = 1;
			keyboard_gamepad(&input, &pad);
			CHECK(pressed(&pad, _icon_black_button) && pressed(&pad, _icon_x_button) == !shown,
				"E in %s presses RB %d, X %d", shown ? "a browser" : "the other menus",
				pressed(&pad, _icon_black_button), pressed(&pad, _icon_x_button));
			CHECK(!strcmp(ui_widget_port_menu_key(_icon_x_button), shown ? "Tab" : "E"),
				"X drawn as %s", ui_widget_port_menu_key(_icon_x_button));
		}
		return 0;
	}
	CASE("scores")
	{
		/* the scores' prompts: A picks the next game, B quits (game_engine.c's
		game_engine_update_non_deterministic), each a key */
		CHECK(ui_widget_port_menu_key(_icon_a_button) && ui_widget_port_menu_key(_icon_b_button),
			"the scores' A or B drawn as the controller's");
		CHECK(strcmp(ui_widget_port_menu_key(_icon_a_button), ui_widget_port_menu_key(_icon_b_button)),
			"A and B drawn as one key");
		return 0;
	}
	CASE("unnamed")
	{
		/* (the D-pad, Start and the sticks keep the controller's) */
		CHECK(!ui_widget_port_menu_key(_icon_dpad_up) && !ui_widget_port_menu_key(_icon_start_button) &&
			!ui_widget_port_menu_key(_icon_left_thumb),
			"a button the keyboard has no menu key for named one");
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 1;
}
