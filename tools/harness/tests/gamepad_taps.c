/*
GAMEPAD_TAPS.C (test)

A controller's taps (port/linux/src/sdl_platform.c's gamepad presses, which
its event pump records, and port/linux/src/xinput_sdl.c's sdl_gamepad_state
and sdl_gamepad_paddles, which read them): a button or trigger pressed and let
go of between two readings is seen at the next reading once, as the keyboard's
keys are, and not held at the one after; each gamepad's are its own; a removed
gamepad's and those the window's focus loss drops are not seen.
test_gamepad_taps.py takes the code.
*/

#include "harness.h"

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef short SHORT;
typedef unsigned int Uint32;
typedef short Sint16;
typedef struct
{
	WORD wButtons;
	BYTE bAnalogButtons[8];
	SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} XINPUT_GAMEPAD;

#include "config.inc"

/* a gamepad as SDL holds it now: its buttons down, its axes */
typedef struct
{
	unsigned char buttons[SDL_GAMEPAD_BUTTON_COUNT];
	Sint16 axes[SDL_GAMEPAD_AXIS_COUNT];
} SDL_Gamepad;

static BOOL SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
	return gamepad->buttons[button] != 0;
}

static Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
	return gamepad->axes[axis];
}

#include "under_test.inc"

/* a reading of a gamepad, as XInputGetState makes it */
static XINPUT_GAMEPAD reading(SDL_Gamepad *gamepad, Uint32 id, unsigned int *paddles)
{
	struct gamepad_taps taps;
	XINPUT_GAMEPAD pad;

	memset(&pad, 0, sizeof(pad));
	gamepad_presses_take(id, &taps.buttons, &taps.triggers);
	sdl_gamepad_state(gamepad, &taps, &pad);
	if (paddles)
		*paddles = sdl_gamepad_paddles(gamepad, &taps);
	return pad;
}

/* a tap of A, its press and release both between two readings */
static void case_tap(void)
{
	SDL_Gamepad gamepad;
	XINPUT_GAMEPAD pad;

	memset(&gamepad, 0, sizeof(gamepad));
	gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_SOUTH);
	pad = reading(&gamepad, 7, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_A] == 0xff, "the tap's A seen: %d", pad.bAnalogButtons[XINPUT_GAMEPAD_A]);
	pad = reading(&gamepad, 7, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_A] == 0, "the tap's A not held: %d", pad.bAnalogButtons[XINPUT_GAMEPAD_A]);
	/* (and a digital button's, the D-pad's) */
	gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_DPAD_UP);
	pad = reading(&gamepad, 7, NULL);
	CHECK(pad.wButtons == XINPUT_GAMEPAD_DPAD_UP, "the D-pad's tap seen: %x", pad.wButtons);
	pad = reading(&gamepad, 7, NULL);
	CHECK(pad.wButtons == 0, "the D-pad's tap not held: %x", pad.wButtons);
	/* (a held button is still held, the tap or not) */
	gamepad.buttons[SDL_GAMEPAD_BUTTON_EAST] = 1;
	gamepad_button_pressed(7, SDL_GAMEPAD_BUTTON_EAST);
	pad = reading(&gamepad, 7, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_B] == 0xff, "B pressed");
	pad = reading(&gamepad, 7, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_B] == 0xff, "B held");
}

/* a trigger pulled past the threshold and let go of between readings; a pull
still held keeps its own depth */
static void case_trigger(void)
{
	SDL_Gamepad gamepad;
	XINPUT_GAMEPAD pad;

	memset(&gamepad, 0, sizeof(gamepad));
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 4000);
	pad = reading(&gamepad, 3, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] == 0, "a touch short of the threshold is no pull");
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
	pad = reading(&gamepad, 3, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] == 255, "the pull seen: %d",
		pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER]);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] == 0, "the other trigger not pulled");
	pad = reading(&gamepad, 3, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] == 0, "the pull not held");
	/* (held half way: its depth, and moving while held is no new pull) */
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 16384);
	gamepad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 16384;
	pad = reading(&gamepad, 3, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] == 16384 * 255 / 32767, "a held pull's depth: %d",
		pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]);
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 20000);
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 16384);
	pad = reading(&gamepad, 3, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] == 16384 * 255 / 32767, "no new pull while held: %d",
		pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]);
	/* (a pull held over a reading, moved and let go of: no pull of its own
	after it) */
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 24000);
	gamepad_trigger_moved(3, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
	gamepad.axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 0;
	pad = reading(&gamepad, 3, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] == 0, "a held pull let go of is no new pull: %d",
		pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER]);
}

/* the back paddles' taps, for the remapping */
static void case_paddles(void)
{
	SDL_Gamepad gamepad;
	unsigned int paddles;

	memset(&gamepad, 0, sizeof(gamepad));
	gamepad_button_pressed(9, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
	reading(&gamepad, 9, &paddles);
	CHECK(paddles == 2, "the right paddle's tap seen: %x", paddles);
	reading(&gamepad, 9, &paddles);
	CHECK(paddles == 0, "the paddle's tap not held: %x", paddles);
}

/* two gamepads (split screen): each reading takes its own taps */
static void case_ports(void)
{
	SDL_Gamepad first, second;
	XINPUT_GAMEPAD pad;

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	gamepad_button_pressed(11, SDL_GAMEPAD_BUTTON_NORTH);
	pad = reading(&second, 12, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_Y] == 0, "the other gamepad's tap not this one's");
	pad = reading(&first, 11, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_Y] == 0xff, "its own tap seen");
}

/* a gamepad removed, and the window's focus lost: their taps dropped */
static void case_dropped(void)
{
	SDL_Gamepad gamepad;
	XINPUT_GAMEPAD pad;

	memset(&gamepad, 0, sizeof(gamepad));
	gamepad_button_pressed(5, SDL_GAMEPAD_BUTTON_SOUTH);
	gamepad_presses_forget(5);
	pad = reading(&gamepad, 5, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_A] == 0, "a removed gamepad's tap dropped");
	gamepad_button_pressed(5, SDL_GAMEPAD_BUTTON_SOUTH);
	gamepad_trigger_moved(5, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767);
	gamepad_presses_clear();
	pad = reading(&gamepad, 5, NULL);
	CHECK(pad.bAnalogButtons[XINPUT_GAMEPAD_A] == 0 && pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] == 0,
		"the focus lost: the taps dropped");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "tap")) case_tap();
	else if (!strcmp(name, "trigger")) case_trigger();
	else if (!strcmp(name, "paddles")) case_paddles();
	else if (!strcmp(name, "ports")) case_ports();
	else if (!strcmp(name, "dropped")) case_dropped();
	else
	{
		fprintf(stderr, "unknown case: %s\n", name);
		return 2;
	}
	return 0;
}
