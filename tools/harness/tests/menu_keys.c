/*
MENU_KEYS.C (test)

The menus' prompts for the keyboard (ui_widget.c's ui_widget_port_menu_key):
the key a controller button of the menus' text (the scores' "A pick game,
B quit", a screen's "Press A to select") is drawn as while the keyboard and
mouse play must be one that presses that button there
(port/linux/src/xinput_sdl.c's keyboard_gamepad, the controller the keyboard
is in the menus). The mouse's Back button (sdl_platform.h's
platform_mouse_button_backs: X1, and a Linux mouse's BTN_BACK) is B there,
as Escape is, typing too, and held over a change of what the keys drive it
counts only once let go of, as Escape does (keys_held_over_switch).
test_menu_keys.py takes both.
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
	BOOL menu_back;
};
#define SDL_SCANCODE_COUNT TEST_SCANCODE_COUNT
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_X1 4
#define SDL_BUTTON_X2 5
typedef struct { unsigned short wButtons; BYTE bAnalogButtons[8]; SHORT sThumbLX, sThumbLY; } XINPUT_GAMEPAD;
static BOOL text_typing, text_typing_enter_armed;
/* (a menu's text field typed into: xinput_sdl.c's typing_gamepad) */
BOOL text_typing_field;
/* whether a server browser is shown (xinput_sdl.c's halo_keyboard_browser_keys) */
static int browser_shown;

static int halo_keyboard_browser_keys_active(void)
{
	return browser_shown;
}

/* (keys_held_over_switch's other contexts: none open) */
static unsigned char console_is_active(void)
{
	return 0;
}

static unsigned char network_chat_typing(void)
{
	return 0;
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
	CASE("mouse-back")
	{
		/* the mouse's Back button presses B alone in the menus, as Escape
		does, and while a field is typed into, as Escape does there */
		struct platform_input_state input;
		XINPUT_GAMEPAD pad;
		short typing;

		for (typing = 0; typing < 2; typing++)
		{
			text_typing = typing;
			memset(&input, 0, sizeof(input));
			memset(&pad, 0, sizeof(pad));
			input.menus = TRUE;
			input.menu_back = TRUE;
			/* (the menus' pointer has the mouse: no button of it held) */
			keyboard_gamepad(&input, &pad);
			CHECK(pressed(&pad, _icon_b_button), "the Back button does not press B (typing: %d)", typing);
			CHECK(!pressed(&pad, _icon_a_button) && !pressed(&pad, _icon_x_button) &&
				!pressed(&pad, _icon_y_button) && !pad.wButtons,
				"the Back button presses more than B (typing: %d)", typing);
			memset(&input, 0, sizeof(input));
			memset(&pad, 0, sizeof(pad));
			input.menus = TRUE;
			keyboard_gamepad(&input, &pad);
			CHECK(!pressed(&pad, _icon_b_button), "B pressed with nothing held (typing: %d)", typing);
		}
		text_typing = FALSE;
		return 0;
	}
	CASE("back-buttons")
	{
		/* SDL's numbering: X1 (Windows' XBUTTON1, macOS's fourth button,
		Linux's BTN_SIDE) and BTN_BACK (7) go back; forward (X2, BTN_FORWARD's
		6) and the others do not */
		int button;

		for (button = 0; button < 8; button++)
		{
			boolean backs = button == SDL_BUTTON_X1 || button == 7;

			CHECK(!!platform_mouse_button_backs(button) == backs, "mouse button %d goes back: %d", button,
				!!platform_mouse_button_backs(button));
		}
		return 0;
	}
	CASE("back-held-over-switch")
	{
		/* the Back button held as what the keys drive changes (typing ends:
		its B let the field go) counts only once let go of, as Escape:
		it does not back out of the menu too */
		struct platform_input_state input;
		XINPUT_GAMEPAD pad;
		int read;

		text_typing = TRUE;
		memset(&input, 0, sizeof(input));
		input.menus = TRUE;
		keys_held_over_switch(&input, FALSE);
		for (read = 0; read < 2; read++)
		{
			memset(&input, 0, sizeof(input));
			input.menus = TRUE;
			input.menu_back = TRUE;
			keys_held_over_switch(&input, FALSE);
			CHECK(input.menu_back, "the Back button held while typing let go of (reading %d)", read);
		}
		text_typing = FALSE;
		memset(&input, 0, sizeof(input));
		input.menus = TRUE;
		input.menu_back = TRUE;
		keys_held_over_switch(&input, FALSE);
		memset(&pad, 0, sizeof(pad));
		keyboard_gamepad(&input, &pad);
		CHECK(!pressed(&pad, _icon_b_button), "the Back button held over the switch presses B");
		memset(&input, 0, sizeof(input));
		input.menus = TRUE;
		keys_held_over_switch(&input, FALSE);
		input.menu_back = TRUE;
		keys_held_over_switch(&input, FALSE);
		memset(&pad, 0, sizeof(pad));
		keyboard_gamepad(&input, &pad);
		CHECK(pressed(&pad, _icon_b_button), "the Back button, let go of and pressed again, does not press B");
		/* (Escape alike) */
		text_typing = TRUE;
		memset(&input, 0, sizeof(input));
		input.menus = TRUE;
		input.keys[SDL_SCANCODE_ESCAPE] = 1;
		keys_held_over_switch(&input, FALSE);
		text_typing = FALSE;
		keys_held_over_switch(&input, FALSE);
		CHECK(!input.keys[SDL_SCANCODE_ESCAPE], "Escape held over the switch kept");
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
