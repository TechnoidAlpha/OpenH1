/*
XINPUT_SDL.C

Xbox controllers and the debug keyboard for the Linux build.

Port 0 is always connected: it is the keyboard and mouse, merged with the
first SDL gamepad when one is present. Further SDL gamepads take ports 1-3.

In the game the keyboard and mouse are a control scheme of their own
(port/linux/include/halo_keyboard.h): config.toml's [controls] bind each of
the player's actions to up to two keys, mouse buttons or wheel turns
(Settings > Controls Setup changes them), and the game takes the actions
held (halo_keyboard_actions) with the controller's. They press none of its
buttons, but for the pause menu's and the scoreboard's (Start and Back).

In the menus the keys drive the controller, to move about them:
	arrows           D-pad               W A S D          left stick
	space, enter     A                   escape, backspace B
	delete, E        X                   tab              Y
	F1               back
(keys held as the game and the menus switch count only once let go of), the
on-screen keyboard takes what is typed, and the mouse is free and drives a
pointer
(port/linux/include/halo_ui_pointer.h, source/interface/ui_widget.c).
Screenshot is a normal bound action (default F10), also available in the
menus. F11 switches between fullscreen and the window, and F12 releases
or recaptures the mouse, always.

Mouse aim does not go through the right stick: the game's look code asks
halo_linux_mouse_look for the motion since its last call and adds it to the
stick's facing change, so aiming is direct rather than rate based.

The game's debug keyboard exists only for the console. Backquote (which
opens it) always reaches the keystroke queue, everything else only while
the console is open, since the game also polls a few keys directly (escape
returns to the main menu). While the console is open the keyboard does not
drive the controller.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "touch_input.h"
#include "mouse_sensitivity.h"
/* port/linux/game/network_aim_assist.c */
extern unsigned char network_aim_assist_block_mouse(void);
#include "halo_keyboard.h"
#include "p2p.h"
#ifdef HALO_VR
#include "vr.h"
#endif
#include "controller_buttons.h"
#include "controller_remap.h"
#include "gyro_aim.h"
#include "report.h"
#include "input_trace.h"
/* port/linux/game/demo.c: the clip key pressed */
void demo_clip_request(void);

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT_COUNT 4
#define VK_OEM_3_BACKQUOTE 0xc0

/* ---------- game hooks */

/* main/console.c */
extern unsigned char console_is_active(void);
/* port/linux/game/network_chat.c: a chat line is typed, which takes the
keyboard as the console does */
extern unsigned char network_chat_typing(void);
/* ... and its on-screen keyboard is up, which takes the first controller
and the touch controls' stick and look */
extern unsigned char network_chat_keyboard_open(void);
/* port/linux/game/network_votekick.c: the D-pad's left and right vote now */
extern unsigned char network_vote_pad_open(void);
/* port/linux/game/network_markers.c: the port's player pings with the stock
layouts' D-pad Right now */
extern unsigned char network_markers_pad_open(short port);
/* port/linux/game/spartan_link.c */
extern unsigned char spartan_link_pad_open(short port);
/* port/linux/game/menu_functions.c: two or more players on this machine
(co-op, or split screen in a network game) */
extern unsigned char pc_menu_split_players(void);
/* port/linux/game/network_test.c: the game's time in seconds while one runs,
else -1 */
extern double network_test_game_seconds(void);
/* main/main.c: the menus' map is loaded (the menus are up, the lobby too) */
extern unsigned char main_menu_is_loaded(void);
/* port/linux/game/sim_trace.c: the simulation's clock, seconds from the
scenario's start to the step to come (negative before it starts); the
player's view turned to a yaw and pitch, in degrees (debug.test_input
"script:") */
extern double sim_trace_scenario_seconds(void);
extern void sim_trace_aim(double yaw, double pitch);
/* port/linux/game/bots.c: the view turned toward the nearest enemy in
sight, and how the player moves; TRUE once on it (the script's "hunt") */
extern unsigned char bots_hunt(float *forward, float *strafe);
/* port/linux/game/latency_log.c: the first controller's reading, its fire
control's pull and left stick (debug.latency_log) */
extern void latency_log_read(int trigger, int stick_x, int stick_y);
/* ... and when the scripted player pressed what it reads ("latency:") */
extern void latency_log_pressed(unsigned long long when_ns);
/* ... and the mouse's: its fire binding held, its motion read; the view
turned by it, by the frame's input or by the drawn view's own late reading
(halo_linux_mouse_look_late) */
extern void latency_log_mouse_read(int fire, float dx, float dy);
extern void latency_log_mouse_turned(void);
extern void latency_log_mouse_late(float dx, float dy);
/* ... a press in a menu (the D-pad, A or the left stick, a menu up), and the
first controller's motors written */
extern void latency_log_menu_read(int pressed, int menus);
extern void latency_log_rumble_written(unsigned left, unsigned right);

/* ---------- device tables */

XPP_DEVICE_TYPE XDEVICE_TYPE_GAMEPAD_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_MEMORY_UNIT_TABLE;
XPP_DEVICE_TYPE XDEVICE_TYPE_DEBUG_KEYBOARD_TABLE;

struct controller
{
	BOOL open;
	DWORD packet_number;
	XINPUT_GAMEPAD previous;
};

static struct controller controllers[PORT_COUNT];
static struct controller keyboard_device;
static DWORD reported_gamepads = 0;
static BOOL reported_keyboard = FALSE;

static BOOL gamepad_idle(SDL_Gamepad *gamepad);

/* ---------- mouse */

static pthread_mutex_t mouse_lock = PTHREAD_MUTEX_INITIALIZER;
static float mouse_pending_x, mouse_pending_y;
/* the motion the frame's input took for the player's facing
(halo_linux_mouse_look), since the first controller was read last: the
drawn view may then take what has come since (halo_linux_mouse_look_late) */
static BOOL mouse_look_taken;
static unsigned long mouse_polls_unconsumed = 0;
static float mouse_wheel_accumulated = 0.0f;
/* the wheel's notches turned while a chat line is typed, not yet taken
(halo_chat_wheel_steps) */
static int chat_wheel_steps = 0;
/* the wheel's switch (wheel_update): when the wheel last moved, until when
Y is held, and whether a scroll is under way */
static Uint64 wheel_moved_ms = 0;
static Uint64 wheel_press_until_ms = 0;
static BOOL wheel_scrolling = FALSE;
/* the way the scroll under way turns: 1 up (away), -1 down */
static int wheel_direction = 0;
/* when port 0's aim last moved, by the mouse, by the right stick, and by
the touch controls (their swipe, gyroscope or stick: halo_linux_mouse_aiming,
halo_linux_touch_aiming) */
static Uint64 mouse_aimed_ms = 0;
static Uint64 stick_aimed_ms = 0;
#ifdef HALO_TOUCH_CONTROLS
static Uint64 touch_aimed_ms = 0;
#endif

/* the right stick's deflection that counts as aiming with it, clear of a
worn stick's drift */
#define STICK_AIMING_DEFLECTION 8000

/* (input.mouse_vertical_sensitivity, read with it: a multiplier of the
horizontal, as CS2's m_pitch is of m_yaw) */
static float vertical_sensitivity = 1.0f;

/* input.mouse_sensitivity, in CS2's units (mouse_sensitivity.h) */
static float mouse_sensitivity(void)
{
	static float sensitivity;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		/* (the keyboard and mouse's player's, player 1's: their profile's) */
		sensitivity = (float)config_player_real(0, "input.mouse_sensitivity");
		if (!(sensitivity > 0.0f))
			sensitivity = (float)MOUSE_SENSITIVITY_DEFAULT;
		vertical_sensitivity = (float)config_player_real(0, "input.mouse_vertical_sensitivity");
		if (!(vertical_sensitivity > 0.0f))
			vertical_sensitivity = 1.0f;
	}
	return sensitivity;
}

/* the game's own dead zone of each stick, of 32767 (source/input/input_xbox.c) */
#define STICK_DEAD_RANGE 9000
/* the most a stick's dead zone setting takes, in percent */
#define STICK_DEAD_ZONE_MAXIMUM 50

/* each gamepad's player's settings: the local player of the gamepad's
index (as controller_remap.c's ports are), their profile's; another
gamepad's config.toml's */
#define PAD_PLAYERS 4
#define PAD_PLAYER_SLOT(gamepad) ((gamepad) >= 0 && (gamepad) < PAD_PLAYERS ? (gamepad) : PAD_PLAYERS)

/* the deflection a stick reads as none, of 32767, for the game's sticks
(stick 0 the left, 1 the right) of a gamepad: its player's
input.left_stick_dead_zone and input.right_stick_dead_zone as a
percentage, else the game's own */
short halo_stick_dead_range(short gamepad, short stick)
{
	static const char *const settings[2] = { "input.left_stick_dead_zone", "input.right_stick_dead_zone" };
	static struct
	{
		short ranges[2];
		unsigned long read_at;
		int read;
	} pads[PAD_PLAYERS + 1];
	int slot = PAD_PLAYER_SLOT(gamepad);
	int index;

	if (!pads[slot].read || pads[slot].read_at != config_changes())
	{
		pads[slot].read = 1;
		pads[slot].read_at = config_changes();
		for (index = 0; index < 2; index++)
		{
			long percent = config_player_integer(slot < PAD_PLAYERS ? slot : -1, settings[index]);

			pads[slot].ranges[index] = percent < 0 || percent > STICK_DEAD_ZONE_MAXIMUM
				? STICK_DEAD_RANGE : (short)(percent * 32767 / 100);
		}
	}
	return pads[slot].ranges[stick ? 1 : 0];
}

/* whether a gamepad's look turns the other way side to side (its player's
input.invert_look_horizontal; the profile's INVERT LOOK is up and down) */
int halo_look_horizontal_inverted(short gamepad)
{
	static struct
	{
		unsigned long read_at;
		int read, inverted;
	} pads[PAD_PLAYERS + 1];
	int slot = PAD_PLAYER_SLOT(gamepad);

	if (!pads[slot].read || pads[slot].read_at != config_changes())
	{
		pads[slot].read = 1;
		pads[slot].read_at = config_changes();
		pads[slot].inverted = config_player_boolean(slot < PAD_PLAYERS ? slot : -1, "input.invert_look_horizontal");
	}
	return pads[slot].inverted;
}

/* radians of yaw and pitch for the mouse motion since the last call: each
count turns the view 0.022 degrees times the sensitivity, as CS2's does
(mouse_sensitivity.h); the game adds these to the facing change of the
player on gamepad 0, after its zoom has scaled them (view_fov.c's
mouse_zoom_look_scale) */
int halo_linux_mouse_look(short gamepad_index, float *yaw, float *pitch)
{
	static int invert;
	static unsigned long read_at = (unsigned long)-1;
	float x, y, radians;

	*yaw = 0.0f;
	*pitch = 0.0f;
	if (gamepad_index != 0)
		return FALSE;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		invert = config_player_boolean(0, "input.invert_mouse");
	}
	pthread_mutex_lock(&mouse_lock);
	x = mouse_pending_x;
	y = mouse_pending_y;
	mouse_pending_x = 0.0f;
	mouse_pending_y = 0.0f;
	mouse_polls_unconsumed = 0;
	mouse_look_taken = TRUE;
	pthread_mutex_unlock(&mouse_lock);
	if (x == 0.0f && y == 0.0f)
		return FALSE;
	radians = mouse_radians_per_count(mouse_sensitivity());
	*yaw = -x * radians;
	*pitch = (invert ? y : -y) * radians * vertical_sensitivity;
	latency_log_mouse_turned();
	return TRUE;
}

/* The drawn view's own reading of the mouse (render_interpolation.c, as the
frame takes the player's view, in first person on foot: display.direct_camera),
late in the frame: the radians of yaw and pitch of the motion come since the
frame's input took it (halo_linux_mouse_look), as that would turn them. Only
the view drawn turns by them: the motion stays the next frame's input, which
turns the player's facing by it then, so the game is as it was (nothing sent,
nothing simulated changes). FALSE, and no turn, when the frame's input took
no motion (the mouse not aiming the player: a menu, the console, the
scoreboard's pointer, F12) or nothing has come. Once a frame. */
int halo_linux_mouse_look_late(short gamepad_index, float *yaw, float *pitch)
{
	static unsigned long read_at = (unsigned long)-1;
	static int invert;
	struct platform_input_state input;
	BOOL taken;
	float radians;

	*yaw = 0.0f;
	*pitch = 0.0f;
	if (gamepad_index != 0)
		return FALSE;
	pthread_mutex_lock(&mouse_lock);
	taken = mouse_look_taken;
	mouse_look_taken = FALSE;
	pthread_mutex_unlock(&mouse_lock);
	if (!taken)
		return FALSE;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		invert = config_player_boolean(0, "input.invert_mouse");
	}
	/* (the window's events come now, as the next frame's reading would
	take them) */
	platform_pump_events();
	platform_input_read(&input, FALSE);
	if (input.mouse_released || (input.mouse_dx == 0.0f && input.mouse_dy == 0.0f))
		return FALSE;
	latency_log_mouse_late(input.mouse_dx, input.mouse_dy);
	radians = mouse_radians_per_count(mouse_sensitivity());
	*yaw = -input.mouse_dx * radians;
	*pitch = (invert ? input.mouse_dy : -input.mouse_dy) * radians * vertical_sensitivity;
	return TRUE;
}

#ifdef HALO_TOUCH_CONTROLS
/* the touch controls moved the player or the aim (halo_linux_touch_aiming) */
static void touch_used(void)
{
	pthread_mutex_lock(&mouse_lock);
	touch_aimed_ms = SDL_GetTicks();
	pthread_mutex_unlock(&mouse_lock);
}
#endif

/* ---------- the text chat's on-screen keyboard (halo_keyboard.h) */

/* Each controller's buttons, before the on-screen keyboard consumes them. */
static unsigned long chat_pad[PORT_COUNT], chat_physical_pad[PORT_COUNT];

/* a controller's buttons as the chat's (HALO_CHAT_PAD_*) */
static unsigned long chat_pad_bits(const XINPUT_GAMEPAD *pad)
{
	/* (an analog button pressed half way; the stick most of the way) */
	enum { PRESSED = 0x80, PUSHED = 22000 };
	unsigned long buttons = 0;

	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) buttons |= 1UL << HALO_CHAT_PAD_UP;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) buttons |= 1UL << HALO_CHAT_PAD_DOWN;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) buttons |= 1UL << HALO_CHAT_PAD_LEFT;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) buttons |= 1UL << HALO_CHAT_PAD_RIGHT;
	if (pad->wButtons & XINPUT_GAMEPAD_START) buttons |= 1UL << HALO_CHAT_PAD_START;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_A] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_A;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_B] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_B;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_X] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_X;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_Y] >= PRESSED) buttons |= 1UL << HALO_CHAT_PAD_Y;
	if (pad->sThumbLY > PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_UP;
	if (pad->sThumbLY < -PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_DOWN;
	if (pad->sThumbLX < -PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_LEFT;
	if (pad->sThumbLX > PUSHED) buttons |= 1UL << HALO_CHAT_PAD_STICK_RIGHT;
	return buttons;
}

static void chat_pad_record(short port, const XINPUT_GAMEPAD *pad)
{
	chat_pad[port] = chat_pad_bits(pad);
}

extern short network_chat_controller(void);
unsigned long halo_chat_pad_buttons_for_port(short port)
{
	return port >= 0 && port < PORT_COUNT ? chat_physical_pad[port] : 0;
}

unsigned long halo_chat_pad_buttons(void)
{
	short port = network_chat_controller();
	return chat_pad[port >= 0 && port < PORT_COUNT ? port : 0];
}

int halo_chat_taps(float *x, float *y)
{
	return touch_input_chat_taps(x, y);
}

int halo_chat_wheel_steps(void)
{
	int steps;

	pthread_mutex_lock(&mouse_lock);
	steps = chat_wheel_steps;
	chat_wheel_steps = 0;
	pthread_mutex_unlock(&mouse_lock);
	return steps;
}

int halo_chat_touch_layout(void)
{
#if defined(HALO_ANDROID)
	return TRUE;
#elif defined(HALO_IOS)
	int used;

	pthread_mutex_lock(&mouse_lock);
	used = touch_aimed_ms != 0;
	pthread_mutex_unlock(&mouse_lock);
	return used;
#else
	return FALSE;
#endif
}

/* the first controller's buttons held as the on-screen keyboard closed (the
B that closed it, the Start or the A that sent the line), which the game
gets none of till each is let go: it would see a new press of it, a pause
menu opened, a melee, a jump (the triggers are analog buttons too) */
static WORD chat_held_buttons[PORT_COUNT];
static unsigned char chat_held_analog[PORT_COUNT];

static void chat_pad_hold_over(short port, XINPUT_GAMEPAD *pad)
{
	int index;

	if (network_chat_controller() == port)
	{
		chat_held_buttons[port] = pad->wButtons;
		chat_held_analog[port] = 0;
		for (index = 0; index < 8; index++)
		{
			if (pad->bAnalogButtons[index])
				chat_held_analog[port] |= (unsigned char)(1 << index);
		}
		memset(pad, 0, sizeof(*pad));
		return;
	}
	chat_held_buttons[port] &= pad->wButtons;
	pad->wButtons &= (WORD)~chat_held_buttons[port];
	for (index = 0; index < 8; index++)
	{
		if (!(chat_held_analog[port] & (1 << index)))
			continue;
		if (pad->bAnalogButtons[index])
			pad->bAnalogButtons[index] = 0;
		else
			chat_held_analog[port] &= (unsigned char)~(1 << index);
	}
}

/* ---------- the scoreboard's player menu (halo_keyboard.h) */

/* a controller held for the scoreboard while the game asked within this
many readings of it (it reads each once a frame, and asks every frame the
scoreboard shows: counted in frames, not time, so that a frame slow to draw,
a screenshot's, lets nothing go) */
#define SCOREBOARD_PAD_READINGS 3

static struct scoreboard_pad
{
	/* what the game last said, and how many readings ago; whether a menu
	was up as the controller was read */
	int hold;
	int readings;
	BOOL menus;
	/* the buttons (halo_scoreboard_pad_button bits) the last reading had,
	and those pressed since the game last asked */
	unsigned long buttons;
	unsigned long pressed;
	/* what the scoreboard took that is still held (the game gets none of
	it till let go of) */
	WORD held_buttons;
	unsigned char held_analog;
} scoreboard_pads[PORT_COUNT];
static pthread_mutex_t scoreboard_pad_lock = PTHREAD_MUTEX_INITIALIZER;

/* (an analog button down: pressed a little way, a trigger at rest not) */
#define SCOREBOARD_PAD_ANALOG_DOWN 0x20

/* a reading's buttons for the scoreboard (the keyboard's are the caller's) */
static unsigned long scoreboard_pad_bits(const XINPUT_GAMEPAD *pad)
{
	/* (an analog button pressed half way; the stick most of the way) */
	enum { PRESSED = 0x80, PUSHED = 22000 };
	unsigned long bits = 0;

	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_UP) bits |= 1UL << HALO_SCOREBOARD_PAD_UP;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_DOWN) bits |= 1UL << HALO_SCOREBOARD_PAD_DOWN;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_LEFT) bits |= 1UL << HALO_SCOREBOARD_PAD_LEFT;
	if (pad->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) bits |= 1UL << HALO_SCOREBOARD_PAD_RIGHT;
	if (pad->sThumbLY > PUSHED) bits |= 1UL << HALO_SCOREBOARD_PAD_STICK_UP;
	if (pad->sThumbLY < -PUSHED) bits |= 1UL << HALO_SCOREBOARD_PAD_STICK_DOWN;
	if (pad->sThumbLX < -PUSHED) bits |= 1UL << HALO_SCOREBOARD_PAD_STICK_LEFT;
	if (pad->sThumbLX > PUSHED) bits |= 1UL << HALO_SCOREBOARD_PAD_STICK_RIGHT;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_X] >= PRESSED) bits |= 1UL << HALO_SCOREBOARD_PAD_OPEN;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_A] >= PRESSED) bits |= 1UL << HALO_SCOREBOARD_PAD_ACCEPT;
	if (pad->bAnalogButtons[XINPUT_GAMEPAD_B] >= PRESSED) bits |= 1UL << HALO_SCOREBOARD_PAD_CANCEL;
	if (pad->wButtons & XINPUT_GAMEPAD_BACK) bits |= 1UL << HALO_SCOREBOARD_PAD_CLOSE;
	return bits;
}

/* the keyboard's: the arrows, Enter (open and choose), Escape and Backspace,
and the scoreboard's key among the actions held */
static unsigned long scoreboard_pad_keys(const unsigned char *keys, unsigned long held)
{
	unsigned long bits = 0;

	if (keys[SDL_SCANCODE_UP]) bits |= 1UL << HALO_SCOREBOARD_PAD_UP;
	if (keys[SDL_SCANCODE_DOWN]) bits |= 1UL << HALO_SCOREBOARD_PAD_DOWN;
	if (keys[SDL_SCANCODE_LEFT]) bits |= 1UL << HALO_SCOREBOARD_PAD_LEFT;
	if (keys[SDL_SCANCODE_RIGHT]) bits |= 1UL << HALO_SCOREBOARD_PAD_RIGHT;
	if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER])
		bits |= (1UL << HALO_SCOREBOARD_PAD_OPEN) | (1UL << HALO_SCOREBOARD_PAD_ACCEPT);
	if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_BACKSPACE]) bits |= 1UL << HALO_SCOREBOARD_PAD_CANCEL;
	if (held & (1UL << HALO_KEYBOARD_SCOREBOARD)) bits |= 1UL << HALO_SCOREBOARD_PAD_CLOSE;
	return bits;
}

/* how the scoreboard holds a port's controller as it is read: as the game
last said, while it says so every frame and no menu is up */
static int scoreboard_pad_port_hold(int port, BOOL menus)
{
	struct scoreboard_pad *pad = &scoreboard_pads[port];
	int hold;

	pthread_mutex_lock(&scoreboard_pad_lock);
	pad->menus = menus;
	if (pad->readings <= SCOREBOARD_PAD_READINGS)
		pad->readings++;
	hold = menus || pad->readings > SCOREBOARD_PAD_READINGS ? HALO_SCOREBOARD_PAD_FREE : pad->hold;
	pthread_mutex_unlock(&scoreboard_pad_lock);
	return hold;
}

/* a port's reading (keys: the keyboard's bits, port 0's): its presses kept
for the game while the scoreboard asks, and what the scoreboard takes taken
from it */
static void scoreboard_pad_read(int port, XINPUT_GAMEPAD *reading, unsigned long keys, int hold)
{
	struct scoreboard_pad *pad = &scoreboard_pads[port];
	unsigned long bits = scoreboard_pad_bits(reading) | keys;
	WORD take_buttons = 0;
	unsigned char take_analog = 0;
	unsigned char analog_down = 0;
	int index;

	pthread_mutex_lock(&scoreboard_pad_lock);
	if (hold != HALO_SCOREBOARD_PAD_FREE)
		pad->pressed |= bits & ~pad->buttons;
	else
		pad->pressed = 0;
	pad->buttons = bits;
	for (index = 0; index < 8; index++)
	{
		if (reading->bAnalogButtons[index] >= SCOREBOARD_PAD_ANALOG_DOWN)
			analog_down |= (unsigned char)(1 << index);
	}
	if (hold == HALO_SCOREBOARD_PAD_HOLDS)
	{
		take_buttons = (WORD)~0;
		take_analog = 0xff;
		reading->sThumbLX = reading->sThumbLY = reading->sThumbRX = reading->sThumbRY = 0;
	}
	else if (hold == HALO_SCOREBOARD_PAD_OFFERED)
	{
		take_buttons = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
			XINPUT_GAMEPAD_DPAD_RIGHT;
		take_analog = (unsigned char)(1 << XINPUT_GAMEPAD_X);
	}
	/* (and what it took that is still held) */
	take_buttons |= (WORD)(pad->held_buttons & reading->wButtons);
	take_analog |= (unsigned char)(pad->held_analog & analog_down);
	pad->held_buttons = (WORD)(take_buttons & reading->wButtons);
	pad->held_analog = (unsigned char)(take_analog & analog_down);
	pthread_mutex_unlock(&scoreboard_pad_lock);
	reading->wButtons &= (WORD)~take_buttons;
	for (index = 0; index < 8; index++)
	{
		if (take_analog & (1 << index))
			reading->bAnalogButtons[index] = 0;
	}
}

unsigned long halo_scoreboard_pad(short controller_index, int hold)
{
	struct scoreboard_pad *pad;
	unsigned long pressed;

	if (controller_index < 0 || controller_index >= PORT_COUNT)
		return 1UL << HALO_SCOREBOARD_PAD_LAPSED;
	pad = &scoreboard_pads[controller_index];
	pthread_mutex_lock(&scoreboard_pad_lock);
	if (pad->hold == HALO_SCOREBOARD_PAD_FREE || pad->menus || pad->readings > SCOREBOARD_PAD_READINGS)
		pressed = 1UL << HALO_SCOREBOARD_PAD_LAPSED;
	else
		pressed = pad->pressed;
	pad->pressed = 0;
	pad->hold = hold;
	pad->readings = 0;
	pthread_mutex_unlock(&scoreboard_pad_lock);
	return pressed;
}

/* the first controller's D-pad left and right held as a vote took them,
which the game gets none of till each is let go: else a vote's end would
hand it a press (the Fishsticks layout switches grenades with left) */
static WORD vote_held_buttons;

static void vote_pad_hold_over(XINPUT_GAMEPAD *pad)
{
	const WORD sides = XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT;

	if (network_vote_pad_open())
		vote_held_buttons |= (WORD)(pad->wButtons & sides);
	else
		vote_held_buttons &= pad->wButtons;
	if (network_vote_pad_open())
		pad->wButtons &= (WORD)~sides;
	else
		pad->wButtons &= (WORD)~vote_held_buttons;
}

/* each port's stock layouts' ping (D-pad Right), held and pressed: while
pings can be sent (network_markers_pad_open) and no overlay reads it, a
press is the ping's (halo_ping_pad_pressed) and the game gets none of that
direction (its strafe); a custom layout's ping is its PING's */
static unsigned char ping_pad_held[PORT_COUNT];
static unsigned char ping_pad_pressed[PORT_COUNT];

static void ping_pad_read(int port, XINPUT_GAMEPAD *pad, unsigned int reserved)
{
	unsigned char layout[CONTROLLER_REMAP_ACTIONS];
	unsigned char held = (chat_physical_pad[port] & (1UL << HALO_CHAT_PAD_RIGHT)) != 0;

	if (controller_remap_read((short)port, layout) || (reserved & CONTROLLER_REMAP_RESERVE_DPAD_RIGHT) ||
		!network_markers_pad_open((short)port))
	{
		ping_pad_held[port] = held;
		return;
	}
	if (held && !ping_pad_held[port])
		ping_pad_pressed[port] = 1;
	ping_pad_held[port] = held;
	pad->wButtons &= (WORD)~XINPUT_GAMEPAD_DPAD_RIGHT;
}

int halo_ping_pad_pressed(short port)
{
	int pressed;

	if (port < 0 || port >= PORT_COUNT)
		return 0;
	pressed = ping_pad_pressed[port];
	ping_pad_pressed[port] = 0;
	return pressed;
}

/* each port's stock layouts' Spartan link (D-pad Down), as the ping's
above: while the link can be in its player's game (spartan_link_pad_open)
and no overlay reads it, a press is the link's (halo_spartan_link_pad_pressed)
and the game gets none of that direction */
static unsigned char spartan_link_pad_held[PORT_COUNT];
static unsigned char spartan_link_pad_pressed_ports[PORT_COUNT];

static void spartan_link_pad_read(int port, XINPUT_GAMEPAD *pad, unsigned int reserved)
{
	unsigned char layout[CONTROLLER_REMAP_ACTIONS];
	unsigned char held = (chat_physical_pad[port] & (1UL << HALO_CHAT_PAD_DOWN)) != 0;

	if (controller_remap_read((short)port, layout) || (reserved & CONTROLLER_REMAP_RESERVE_DPAD_DOWN) ||
		!spartan_link_pad_open((short)port))
	{
		spartan_link_pad_held[port] = held;
		return;
	}
	if (held && !spartan_link_pad_held[port])
		spartan_link_pad_pressed_ports[port] = 1;
	spartan_link_pad_held[port] = held;
	pad->wButtons &= (WORD)~XINPUT_GAMEPAD_DPAD_DOWN;
}

int halo_spartan_link_pad_pressed(short port)
{
	int pressed;

	if (port < 0 || port >= PORT_COUNT)
		return 0;
	pressed = spartan_link_pad_pressed_ports[port];
	spartan_link_pad_pressed_ports[port] = 0;
	return pressed;
}

/* the touch controls' stick as the player's movement (input_abstraction.c's
keyboard_controls_update), forward and strafe -1..1, whatever the
profile's sticks do; nonzero while it is pushed. Nothing but on Android and
iOS. */
int halo_linux_touch_move(short controller_index, float *forward, float *strafe)
{
	*forward = 0.0f;
	*strafe = 0.0f;
	if (controller_index != 0)
		return FALSE;
#ifdef HALO_TOUCH_CONTROLS
	if (!touch_input_move(forward, strafe) || network_chat_controller() == 0)
	{
		*forward = 0.0f;
		*strafe = 0.0f;
		return FALSE;
	}
	touch_used();
	return TRUE;
#else
	return FALSE;
#endif
}

/* the touch controls' swipe and gyroscope since the last call, in radians at
the mouse's rate per pixel (the view applies its own sensitivity), each
apart: player_control.c inverts the swipe as the profile inverts the stick,
not the gyroscope, which turns as the phone does, and makes them the
stick's rate before the magnetism. Nothing but on Android and iOS. */
int halo_linux_touch_look(short gamepad_index, float *yaw, float *pitch, float *gyro_yaw, float *gyro_pitch)
{
	*yaw = 0.0f;
	*pitch = 0.0f;
	*gyro_yaw = 0.0f;
	*gyro_pitch = 0.0f;
	if (gamepad_index != 0)
		return FALSE;
#ifdef HALO_TOUCH_CONTROLS
	touch_input_look(0.0022f, yaw, pitch, gyro_yaw, gyro_pitch);
	/* (none while the chat's on-screen keyboard is up: what was swiped is
	dropped, not saved for after) */
	if (network_chat_keyboard_open() && network_chat_controller() == 0)
	{
		*yaw = 0.0f;
		*pitch = 0.0f;
		*gyro_yaw = 0.0f;
		*gyro_pitch = 0.0f;
	}
	if (*yaw == 0.0f && *pitch == 0.0f && *gyro_yaw == 0.0f && *gyro_pitch == 0.0f)
		return FALSE;
	touch_used();
	return TRUE;
#else
	return FALSE;
#endif
}

/* The drawn view's own reading of the touch controls' swipe and gyroscope
(player_control.c's player_control_touch_look_late, for render_interpolation.c,
display.direct_camera), late in the frame: what has come since the frame's
input took them (halo_linux_touch_look), in its units. Only the view drawn
turns by it: the next frame's input takes it as its own (touch_input_look_late
keeps it), so the game is as it was. Nothing but on Android and iOS. */
int halo_linux_touch_look_late(short gamepad_index, float *yaw, float *pitch, float *gyro_yaw, float *gyro_pitch)
{
	*yaw = 0.0f;
	*pitch = 0.0f;
	*gyro_yaw = 0.0f;
	*gyro_pitch = 0.0f;
	if (gamepad_index != 0)
		return FALSE;
#ifdef HALO_TOUCH_CONTROLS
	/* (none while the chat's on-screen keyboard is up, whose frames drop it) */
	if (network_chat_keyboard_open() && network_chat_controller() == 0)
		return FALSE;
	touch_input_look_late(0.0022f, yaw, pitch, gyro_yaw, gyro_pitch);
	return *yaw != 0.0f || *pitch != 0.0f || *gyro_yaw != 0.0f || *gyro_pitch != 0.0f;
#else
	return FALSE;
#endif
}

/* whether port 0's player aims with the touch controls without their aim
assist (input.touch_aim_assist false): no magnetism, as for the mouse
(player_control.c), until a stick moves the aim again */
int halo_linux_touch_aiming(short gamepad_index)
{
#ifdef HALO_TOUCH_CONTROLS
	int aiming;

	if (gamepad_index != 0 || touch_input_aim_assist())
		return FALSE;
	pthread_mutex_lock(&mouse_lock);
	aiming = touch_aimed_ms != 0 && touch_aimed_ms >= stick_aimed_ms;
	pthread_mutex_unlock(&mouse_lock);
	return aiming;
#else
	(void)gamepad_index;
	return FALSE;
#endif
}

/* Port zero defaults to keyboard/mouse until the stick or touch aims.
   Merely having a controller connected does not enable mouse magnetism. */
static int mouse_is_aim_device(short gamepad_index)
{
    int aiming;
    if (gamepad_index != 0) return FALSE;
    pthread_mutex_lock(&mouse_lock);
    aiming = stick_aimed_ms == 0 || mouse_aimed_ms >= stick_aimed_ms;
#if defined(HALO_ANDROID) || defined(HALO_IOS)
    if (touch_aimed_ms != 0 && touch_aimed_ms >= mouse_aimed_ms &&
        touch_aimed_ms >= stick_aimed_ms) aiming = FALSE;
#endif
    pthread_mutex_unlock(&mouse_lock);
    return aiming;
}

/* whether the player on the gamepad aims with the mouse (it moved after the
right stick last did) and input.mouse_aim_assist is off: then the view's
magnetism leaves them be (player_control.c); the bullets' autoaim stays */
int halo_linux_mouse_aiming(short gamepad_index)
{
	static int aim_assist;
	static unsigned long read_at = (unsigned long)-1;
	int aiming;

	if (gamepad_index != 0)
		return FALSE;
#ifdef HALO_VR
	/* the head aims in VR: a view dragged by magnetism would sicken (the
	gamepad's aim, vr.aim = "gamepad", keeps it, as on the flat screen) */
	if (halo_vr_aiming() && !halo_vr_gamepad_aiming())
		return TRUE;
#endif
	if (read_at != config_changes())
	{
		read_at = config_changes();
		aim_assist = config_player_boolean(0, "input.mouse_aim_assist");
	}
	if (network_aim_assist_block_mouse() && mouse_is_aim_device(gamepad_index))
		return TRUE;
	if (aim_assist)
		return FALSE;
	pthread_mutex_lock(&mouse_lock);
	aiming = mouse_aimed_ms != 0 && mouse_aimed_ms >= stick_aimed_ms;
	pthread_mutex_unlock(&mouse_lock);
	return aiming;
}

/* collects the motion the game has not asked for yet; motion that nobody
consumes for a few polls (menus, cutscenes) is dropped so it cannot jerk
the view later */
static void mouse_poll(const struct platform_input_state *input)
{
	pthread_mutex_lock(&mouse_lock);
	if (++mouse_polls_unconsumed > 4)
	{
		mouse_pending_x = 0.0f;
		mouse_pending_y = 0.0f;
	}
	if (!input->mouse_released)
	{
		mouse_pending_x += input->mouse_dx;
		mouse_pending_y += input->mouse_dy;
		if (input->mouse_dx != 0.0f || input->mouse_dy != 0.0f)
			mouse_aimed_ms = SDL_GetTicks();
		mouse_wheel_accumulated += input->mouse_wheel;
		if (input->mouse_wheel != 0.0f)
			wheel_moved_ms = SDL_GetTicks();
	}
	pthread_mutex_unlock(&mouse_lock);
}

/* ---------- keyboard and mouse as a controller */

static BYTE analog(BOOL down)
{
	return down ? 0xff : 0x00;
}

/* the game's on-screen keyboard is up (platform_text_typing): the keys type
into it (XInputDebugGetKeystroke passes them to the game), but for the
arrows, which move about it, enter (Done, once let go of since it came up)
and escape (cancel). A menu's text field (platform_text_field) takes its
Enter as a key, once let go of since the field began, which the field
makes its row's A (menu_functions.c's text_field_show): not Start, which in
a game closes the menus and drops what was typed with every change on the
screen */
static BOOL text_typing;
static BOOL text_typing_enter_armed;
/* (the on-screen keyboard's, and a menu's text field's: menu_functions.c) */
static BOOL text_typing_keyboard, text_typing_field;

static void text_typing_update(void)
{
	BOOL typing = text_typing_keyboard || text_typing_field;

	if (typing && !text_typing)
		text_typing_enter_armed = FALSE;
	text_typing = typing;
}

void platform_text_typing(int typing)
{
	text_typing_keyboard = typing != 0;
	text_typing_update();
#ifdef HALO_ANDROID
	/* (on a touchscreen: Android's on-screen keyboard as well, sdl_platform.c) */
	platform_screen_keyboard(text_typing_keyboard, FALSE);
#endif
}

void platform_text_field(int typing, int password)
{
	text_typing_field = typing != 0;
	text_typing_update();
	/* (the system's on-screen keyboard, where there is one: Steam's,
	Android's, iOS's; sdl_platform.c) */
	platform_screen_keyboard(text_typing_field, typing && password);
}

static void typing_gamepad(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	const unsigned char *k = input->keys;
	BOOL enter = k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];

	if (k[SDL_SCANCODE_UP]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (k[SDL_SCANCODE_DOWN]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (k[SDL_SCANCODE_LEFT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (!enter)
		text_typing_enter_armed = TRUE;
	else if (text_typing_enter_armed && !text_typing_field)
		pad->wButtons |= XINPUT_GAMEPAD_START;
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_ESCAPE] || input->menu_back);
}

/* when a server browser was shown last (halo_keyboard_browser_keys): its
keys while it was, within a quarter of a second */
static Uint64 browser_keys_time;
#define BROWSER_KEYS_MILLISECONDS 250

void halo_keyboard_browser_keys(void)
{
	browser_keys_time = SDL_GetTicks() + 1;
}

int halo_keyboard_browser_keys_active(void)
{
	return browser_keys_time && SDL_GetTicks() + 1 - browser_keys_time < BROWSER_KEYS_MILLISECONDS;
}

/* when a Settings screen of the menu engine's was shown last
(halo_keyboard_settings_keys): its letters and digits type into its search
while it was, within a quarter of a second */
static Uint64 settings_keys_time;
/* (and it was the search's own screen: Backspace takes a letter off what is
typed there, not Esc) */
static BOOL settings_keys_search;

void halo_keyboard_settings_keys(int search)
{
	settings_keys_time = SDL_GetTicks() + 1;
	settings_keys_search = search != 0;
}

int halo_keyboard_settings_keys_active(void)
{
	return settings_keys_time && SDL_GetTicks() + 1 - settings_keys_time < BROWSER_KEYS_MILLISECONDS;
}

static void keyboard_gamepad(const struct platform_input_state *input, XINPUT_GAMEPAD *pad)
{
	const unsigned char *k = input->keys;
	BOOL browser = halo_keyboard_browser_keys_active();
	/* (on Settings, the menu engine's, W, A, S, D, F, R and L are letters that
	start its search, not the stick and buttons: the arrows move the focus) */
	BOOL settings = halo_keyboard_settings_keys_active();
	BOOL mouse = !input->mouse_released;
	const unsigned char *m = input->mouse_buttons;
	int x = 0, y = 0;

	if (text_typing)
	{
		typing_gamepad(input, pad);
		return;
	}

	if (!settings)
	{
		if (k[SDL_SCANCODE_D]) x++;
		if (k[SDL_SCANCODE_A]) x--;
		if (k[SDL_SCANCODE_W]) y++;
		if (k[SDL_SCANCODE_S]) y--;
	}
	if (x || y)
	{
		/* full deflection, diagonals on the unit circle */
		float length = (x && y) ? 0.70710678f : 1.0f;

		pad->sThumbLX = (SHORT)(x * 32767 * length);
		pad->sThumbLY = (SHORT)(y * 32767 * length);
	}

	if (k[SDL_SCANCODE_UP]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_UP;
	if (k[SDL_SCANCODE_DOWN]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
	if (k[SDL_SCANCODE_LEFT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) pad->wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
	if (k[SDL_SCANCODE_F1]) pad->wButtons |= XINPUT_GAMEPAD_BACK;

	/* (escape backs out, as backspace and the mouse's Back button do: the
	pause menu's B resumes the game, the main menu's asks to quit; Start
	would choose, as A does. The Back button comes as menu_back, the menus'
	pointer taking the mouse's buttons (sdl_platform.c); with none, Android's,
	as X1) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_A] |= analog(k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] ||
		k[SDL_SCANCODE_KP_ENTER]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_ESCAPE] ||
		(!(settings && settings_keys_search) && k[SDL_SCANCODE_BACKSPACE]) || input->menu_back ||
		(mouse && m[SDL_BUTTON_X1]));
#ifdef HALO_ANDROID
	/* the system back key (gesture or button) backs out of menus */
	pad->bAnalogButtons[XINPUT_GAMEPAD_B] |= analog(k[SDL_SCANCODE_AC_BACK]);
#endif
	/* (Delete and E as X: Load Game's, Profiles' and the gametypes' delete;
	in a server browser Tab instead, its sort, drawn as Tab in the menus'
	prompts while the keyboard and mouse play: ui_widget.c) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_X] |= analog(k[SDL_SCANCODE_DELETE] ||
		(browser ? k[SDL_SCANCODE_TAB] : k[SDL_SCANCODE_E]));
	/* (Q and E as LB and RB, the white and black buttons: Settings' and the
	server browsers' tabs) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_WHITE] |= analog(k[SDL_SCANCODE_Q]);
	pad->bAnalogButtons[XINPUT_GAMEPAD_BLACK] |= analog(k[SDL_SCANCODE_E]);
	/* (F as Y: the server browsers' favorite, drawn as F; Tab too but in a
	server browser: the Profiles screen's new profile) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_Y] |= analog((!settings && k[SDL_SCANCODE_F]) ||
		(!browser && k[SDL_SCANCODE_TAB]));
	/* (R as the right trigger: Multiplayer's and the server browser's
	refresh, drawn as R) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] |= analog(!settings && k[SDL_SCANCODE_R]);
	/* (L as the left trigger: the server browsers' Filters, drawn as L) */
	pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] |= analog(!settings && k[SDL_SCANCODE_L]);
	/* (/ as R3 in a server browser: its SEARCH, drawn as /) */
	if (browser && k[SDL_SCANCODE_SLASH])
		pad->wButtons |= XINPUT_GAMEPAD_RIGHT_THUMB;
}

/* the keys held when the game and the menus switch, or typing or the
console starts or ends, count as up until let go of: the escape that opens
the pause menu does not also back out of it, nor the one that closes it (or
the console) pause the game again, nor the Enter that ends typing press the
next screen's A, nor the Escape that closes the scoreboard's player menu
(scoreboard_holds) open the pause menu */
static void keys_held_over_switch(struct platform_input_state *input, BOOL scoreboard_holds)
{
	static unsigned char held[SDL_SCANCODE_COUNT];
	/* (and the mouse's Back button, which is Escape in the menus) */
	static BOOL back_held;
	static int context = -1;
	int next_context = (input->menus != FALSE) | (text_typing ? 2 : 0) | (console_is_active() ? 4 : 0) |
		(network_chat_typing() ? 8 : 0) | (scoreboard_holds ? 16 : 0);
	int scancode;

	if (context != next_context)
	{
		context = next_context;
		memcpy(held, input->keys, sizeof(held));
		back_held = input->menu_back;
	}
	if (!input->menu_back)
		back_held = FALSE;
	else if (back_held)
		input->menu_back = FALSE;
	for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
	{
		if (!input->keys[scancode])
			held[scancode] = 0;
		else if (held[scancode])
			input->keys[scancode] = 0;
	}
}

/* ---------- the keyboard and mouse's own controls */

#define MAXIMUM_BINDINGS 2

static const char *const binding_settings[NUMBER_OF_HALO_KEYBOARD_ACTIONS] =
{
	"controls.move_forward", "controls.move_backward", "controls.strafe_left", "controls.strafe_right",
	"controls.jump", "controls.crouch", "controls.fire", "controls.throw_grenade", "controls.melee",
	"controls.reload", "controls.zoom", "controls.switch_weapon", "controls.switch_grenade", "controls.action",
	"controls.flashlight", "controls.scoreboard", "controls.pause", "controls.screenshot",
	"controls.report", "controls.push_to_talk", "controls.chat", "controls.team_chat", "controls.party_accept",
	"controls.vote_yes", "controls.vote_no", "controls.clip", "controls.ping", "controls.walk",
	"controls.spartan_link",
};

static const struct
{
	const char *name;
	int input;
} named_inputs[] =
{
	/* (SDL's names are "," and "Keypad ,", which a list of bindings, split
	at commas, cannot hold) */
	{ "Comma", SDL_SCANCODE_COMMA },
	{ "Keypad Comma", SDL_SCANCODE_KP_COMMA },
	{ "Mouse Left", INPUT_MOUSE + SDL_BUTTON_LEFT },
	{ "Mouse Right", INPUT_MOUSE + SDL_BUTTON_RIGHT },
	{ "Mouse Middle", INPUT_MOUSE + SDL_BUTTON_MIDDLE },
	{ "Mouse 4", INPUT_MOUSE + SDL_BUTTON_X1 },
	{ "Mouse 5", INPUT_MOUSE + SDL_BUTTON_X2 },
	{ "Wheel", INPUT_WHEEL },
	{ "Wheel Up", INPUT_WHEEL_UP },
	{ "Wheel Down", INPUT_WHEEL_DOWN },
};

/* the bindings, read again when config.toml changes; -1 for none */
static int bindings[NUMBER_OF_HALO_KEYBOARD_ACTIONS][MAXIMUM_BINDINGS];
static unsigned long bindings_read_at = (unsigned long)-1;
static unsigned long keyboard_actions_held;

/* (the C library's case: SDL's string functions are not the Android
guest's) */
static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		if ((*a >= 'a' && *a <= 'z' ? *a - 32 : *a) != (*b >= 'a' && *b <= 'z' ? *b - 32 : *b))
			return FALSE;
	}
	return *a == *b;
}

int halo_input_from_name(const char *name)
{
	SDL_Scancode scancode;
	size_t index;

	for (index = 0; index < sizeof(named_inputs) / sizeof(named_inputs[0]); index++)
	{
		if (same_name(name, named_inputs[index].name))
			return named_inputs[index].input;
	}
	/* (the other mouse buttons, as halo_input_name names them) */
	if (!strncmp(name, "Mouse ", 6) && name[6] >= '1' && name[6] <= '9' && !name[7] &&
		name[6] - '0' < PLATFORM_MOUSE_BUTTON_COUNT)
	{
		return INPUT_MOUSE + (name[6] - '0');
	}
	scancode = SDL_GetScancodeFromName(name);
	return scancode != SDL_SCANCODE_UNKNOWN ? (int)scancode : -1;
}

void halo_input_name(int input, char *name, size_t size)
{
	size_t index;

	for (index = 0; index < sizeof(named_inputs) / sizeof(named_inputs[0]); index++)
	{
		if (named_inputs[index].input == input)
		{
			snprintf(name, size, "%s", named_inputs[index].name);
			return;
		}
	}
	if (input >= 0 && input < SDL_SCANCODE_COUNT && *SDL_GetScancodeName((SDL_Scancode)input))
		snprintf(name, size, "%s", SDL_GetScancodeName((SDL_Scancode)input));
	else if (input >= INPUT_MOUSE && input < INPUT_WHEEL)
		snprintf(name, size, "Mouse %d", input - INPUT_MOUSE);
	else
		snprintf(name, size, "%s", "");
}

/* a binding's name as the keyboard's layout labels the key: config.toml
names a key by where it sits (its scancode, named as on a US keyboard), so
WASD stays under the fingers on any layout, as Halo PC's DirectInput keys
did, but the menus show what is printed on it: the zoom key "Z" (the key
right of the left Shift) shows as Y on a German keyboard. A key with no
character of its own (Space, Left Shift, the keypad's) keeps its name, as
does one whose character the menus' font has not (beyond Latin-1). */
void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	int input = halo_input_from_name(binding);
	SDL_Keycode key;

	snprintf(shown, size, "%s", binding);
#ifdef HALO_GUEST
	/* (a guest's SDL bridges no keyboard layouts to ask, guest_sdl.c: the
	US names, which match a hardware keyboard's usual labels on Android, and
	most desktops') */
	(void)input;
	(void)key;
	return;
#else
	if (input < 0 || input >= SDL_SCANCODE_COUNT)
		return;
	/* (as in key events: the French number row shows its numbers, and a
	non-Latin layout's letters the US ones) */
	key = SDL_GetKeyFromScancode((SDL_Scancode)input, SDL_KMOD_NONE, true);
	if (key == ',')
		snprintf(shown, size, "%s", "Comma");
	else if (key > ' ' && key < 0x7f)
		snprintf(shown, size, "%c", key >= 'a' && key <= 'z' ? (char)(key - 'a' + 'A') : (char)key);
	else if (key >= 0xa1 && key <= 0xff && size >= 2)
	{
		/* (Latin-1, a byte the menus take as its character; the small
		letters made capitals but for sharp s and y with diaeresis) */
		if (key >= 0xe0 && key <= 0xfe && key != 0xf7)
			key -= 0x20;
		shown[0] = (char)key;
		shown[1] = 0;
	}
#endif
}

static void bindings_read(void)
{
	int action;

	if (bindings_read_at == config_changes())
		return;
	bindings_read_at = config_changes();
	for (action = 0; action < NUMBER_OF_HALO_KEYBOARD_ACTIONS; action++)
	{
		/* (the keyboard and mouse's player's, player 1's: their profile's) */
		const char *text = config_player_string(0, binding_settings[action]);
		int slot;

		for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
		{
			char name[64];
			size_t length;

			bindings[action][slot] = -1;
			while (*text == ' ' || *text == ',')
				text++;
			length = strcspn(text, ",");
			if (!length)
				continue;
			snprintf(name, sizeof(name), "%.*s", (int)length, text);
			while (*name && name[strlen(name) - 1] == ' ')
				name[strlen(name) - 1] = 0;
			bindings[action][slot] = halo_input_from_name(name);
			if (bindings[action][slot] < 0)
				platform_log("controls: %s has no key or button named \"%s\"", binding_settings[action], name);
			text += length;
		}
	}
}

/* Prompt readers use the same parsed bindings and refresh generation as play.
 * No parallel setting table: a rebind is visible on the next lookup. */
int halo_keyboard_binding_label(int action, int slot, char *shown, unsigned int size)
{
	char name[64];
	if (!shown || !size) return 0;
	shown[0] = 0;
	if (action < 0 || action >= NUMBER_OF_HALO_KEYBOARD_ACTIONS || slot < 0 || slot >= MAXIMUM_BINDINGS)
		return 0;
	bindings_read();
	if (bindings[action][slot] < 0) return 0;
	halo_input_name(bindings[action][slot], name, sizeof(name));
	halo_input_shown_name(name, shown, size);
	return shown[0] != 0;
}

static BOOL input_held(const struct platform_input_state *input, int code)
{
	BOOL wheel = SDL_GetTicks() < wheel_press_until_ms;

	if (code < 0)
		return FALSE;
	if (code < SDL_SCANCODE_COUNT)
		return input->keys[code] != 0;
	if (code < INPUT_WHEEL)
		return !input->mouse_released && input->mouse_buttons[code - INPUT_MOUSE];
	if (code == INPUT_WHEEL)
		return wheel;
	return wheel && wheel_direction == (code == INPUT_WHEEL_UP ? 1 : -1);
}

/* Shared binding lookup for gameplay and Screenshot, including in menus. */
static unsigned long keyboard_bound_actions(const struct platform_input_state *input)
{
	unsigned long held = 0;
	int action, slot;

	bindings_read();
	for (action = 0; action < NUMBER_OF_HALO_KEYBOARD_ACTIONS; action++)
	{
		for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
		{
			if (input_held(input, bindings[action][slot]))
				held |= 1UL << action;
		}
	}
#ifdef HALO_ANDROID
	/* the system back key (gesture or button) pauses the game, as Escape
	does; in the menus it is B (keyboard_gamepad) */
	if (input->keys[SDL_SCANCODE_AC_BACK])
		held |= 1UL << HALO_KEYBOARD_PAUSE;
#endif
	return held;
}

/* One capture per press, regardless of how long the binding is held; and
a report (controls.report: saved, and its GitHub issue opened, report.c)
the same. */
static void keyboard_screenshot(unsigned long held)
{
	static BOOL was_down, report_was_down, clip_was_down;
	BOOL down = (held & (1UL << HALO_KEYBOARD_SCREENSHOT)) != 0;
	BOOL report_down = (held & (1UL << HALO_KEYBOARD_REPORT)) != 0;
	BOOL clip_down = (held & (1UL << HALO_KEYBOARD_CLIP)) != 0;

	if (down && !was_down)
		platform_screenshot_request();
	if (report_down && !report_was_down)
		report_request(1);
	/* (the clip key: the demo being recorded marked, port/linux/game/demo.c) */
	if (clip_down && !clip_was_down)
		demo_clip_request();
	was_down = down;
	report_was_down = report_down;
	clip_was_down = clip_down;
}

/* in the game: the actions held, and Start/Back for pause/scores */
static void keyboard_controls(unsigned long held, XINPUT_GAMEPAD *pad)
{
	if (held & (1UL << HALO_KEYBOARD_PAUSE))
		pad->wButtons |= XINPUT_GAMEPAD_START;
	if (held & (1UL << HALO_KEYBOARD_SCOREBOARD))
		pad->wButtons |= XINPUT_GAMEPAD_BACK;
	keyboard_actions_held = held;
}

unsigned long halo_keyboard_actions(short controller_index)
{
	return controller_index == 0 ? keyboard_actions_held : 0;
}

/* an action's name as config.toml's binding of it is ("move_forward"), for
the input trace; NULL if none */
const char *halo_keyboard_action_setting(int action)
{
	if (action < 0 || action >= NUMBER_OF_HALO_KEYBOARD_ACTIONS)
		return NULL;
	return binding_settings[action] + sizeof("controls.") - 1;
}

int halo_push_to_talk_held(void)
{
	struct platform_input_state input;
	int slot;

	/* (the window losing the focus lets every key go: sdl_platform.c) */
	bindings_read();
	platform_input_read(&input, FALSE);
	for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
	{
		if (input_held(&input, bindings[HALO_KEYBOARD_PUSH_TO_TALK][slot]))
			return 1;
	}
	return controller_remap_talk();
}

/* ---------- a Discord friend's ask to join (p2p_discord.c) */

/* answered before the game and the menus see the keys, wherever the player
is (port/linux/game/network_party.c shows the ask): the party's accept
(controls.party_accept, J) or a controller's D-pad Down lets the friend in,
Escape or the D-pad's Up does not. A key pressed for it is taken from the
game and the menus until it is let go of (Escape does not also back out of
a menu, nor pause the game). Not while the console is open or a line is
typed. With no Discord ask, a friend's invite to its party is answered the
same way (port/linux/src/p2p_friends.c): accepted, it joins the party */

/* the ask answered is a friend's invite (its serial p2p_friends.c's) */
static BOOL ask_is_friends;

static unsigned long discord_ask_serial(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	unsigned long serial;

	ask_is_friends = FALSE;
	if (console_is_active() || network_chat_typing() || text_typing)
		return 0;
	serial = p2p_discord_join_request(name, sizeof(name), NULL);
	if (!serial)
	{
		serial = p2p_friends_invite_prompt(NULL, 0);
		ask_is_friends = serial != 0;
	}
	return serial;
}

static void ask_answer(unsigned long serial, int accept)
{
	if (ask_is_friends)
		p2p_friends_answer(serial, accept);
	else
		p2p_discord_answer(serial, accept);
}

static void input_take(struct platform_input_state *input, int code)
{
	if (code >= 0 && code < SDL_SCANCODE_COUNT)
		input->keys[code] = 0;
	else if (code >= INPUT_MOUSE && code < INPUT_WHEEL)
		input->mouse_buttons[code - INPUT_MOUSE] = 0;
}

static void discord_ask_keys(struct platform_input_state *input, unsigned long serial)
{
	static BOOL accept_held, decline_held, accept_taken, decline_taken;
	BOOL accept = FALSE, decline = input->keys[SDL_SCANCODE_ESCAPE] != 0;
	int slot;

	bindings_read();
	for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
		accept = accept || input_held(input, bindings[HALO_KEYBOARD_PARTY_ACCEPT][slot]);
	if (serial && accept && !accept_held)
	{
		ask_answer(serial, 1);
		accept_taken = TRUE;
	}
	else if (serial && decline && !decline_held)
	{
		ask_answer(serial, 0);
		decline_taken = TRUE;
	}
	accept_held = accept;
	decline_held = decline;
	accept_taken = accept_taken && accept;
	decline_taken = decline_taken && decline;
	if (accept_taken || (serial && accept))
	{
		for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
			input_take(input, bindings[HALO_KEYBOARD_PARTY_ACCEPT][slot]);
	}
	if (decline_taken || (serial && decline))
		input->keys[SDL_SCANCODE_ESCAPE] = 0;
}

/* the controller's (its own D-pad, not the arrow keys the menus press it
with) */
static void discord_ask_pad(SDL_Gamepad *gamepad, unsigned long serial, XINPUT_GAMEPAD *pad)
{
	static BOOL accept_held, decline_held, accept_taken, decline_taken;
	BOOL accept = gamepad && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
	BOOL decline = gamepad && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);

	if (serial && accept && !accept_held)
	{
		ask_answer(serial, 1);
		accept_taken = TRUE;
	}
	else if (serial && decline && !decline_held)
	{
		ask_answer(serial, 0);
		decline_taken = TRUE;
	}
	accept_held = accept;
	decline_held = decline;
	accept_taken = accept_taken && accept;
	decline_taken = decline_taken && decline;
	if (accept_taken || (serial && accept))
		pad->wButtons &= (WORD)~XINPUT_GAMEPAD_DPAD_DOWN;
	if (decline_taken || (serial && decline))
		pad->wButtons &= (WORD)~XINPUT_GAMEPAD_DPAD_UP;
}

/* ---------- the HUD's button prompts (halo_keyboard_prompts) */

/* what the first player last played with: the keyboard and mouse (a key
or button bound to an action, the captured mouse moved), or a controller
(its buttons, sticks and triggers; the touch controls, which draw its
buttons); nothing yet */
enum
{
	PROMPT_DEVICE_NONE,
	PROMPT_DEVICE_KEYBOARD,
	PROMPT_DEVICE_CONTROLLER
};

static int prompt_device = PROMPT_DEVICE_NONE;
static BOOL prompt_controller_connected;
static Uint64 prompt_mouse_seen_ms;
#ifdef HALO_TOUCH_CONTROLS
static Uint64 prompt_touch_seen_ms;
#endif

/* the device a reading leaves the prompts to: the one used in it, the
controller if both were (a controller's stick at rest is often a little
off its centre, which gamepad_idle allows for), else the one before */
static int prompt_device_next(int device, BOOL keyboard_used, BOOL controller_used)
{
	if (controller_used)
		return PROMPT_DEVICE_CONTROLLER;
	if (keyboard_used)
		return PROMPT_DEVICE_KEYBOARD;
	return device;
}

/* whether the prompts show keys: input.button_prompts "keyboard", or
"auto" and the keyboard and mouse were used last (before either is: unless
a controller is connected, or the touch controls are there) */
static int prompts_show_keys(const char *setting, int device, BOOL controller_connected, BOOL touch_controls)
{
	if (!strcmp(setting, "keyboard"))
		return 1;
	if (strcmp(setting, "auto"))
		return 0;
	if (device == PROMPT_DEVICE_NONE)
		return !controller_connected && !touch_controls;
	return device == PROMPT_DEVICE_KEYBOARD;
}

int halo_keyboard_prompts(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static char setting[16];
	BOOL touch_controls = FALSE;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		/* (the keyboard's player's, player 1's: their profile's) */
		snprintf(setting, sizeof(setting), "%s", config_player_string(0, "input.button_prompts"));
	}
#ifdef HALO_VR
	/* (in the headset, the hands' controllers: port/linux/game/
	hud_key_prompts.c names theirs) */
	if (halo_vr_running())
		return 0;
#endif
#ifdef HALO_TOUCH_CONTROLS
	touch_controls = TRUE;
#endif
	return prompts_show_keys(setting, prompt_device, prompt_controller_connected, touch_controls);
}

/* whether the first player last used a controller: the prompts' device
(prompt_device_update), which the vote's box follows too */
int halo_gamepad_last_used(void)
{
	return prompt_device == PROMPT_DEVICE_CONTROLLER;
}

/* a binding's name as a prompt shows it: the mouse's buttons by their
clicks, a key by its label (halo_input_shown_name) */
static void prompt_binding_name(int input, char *name, size_t size)
{
	static const struct
	{
		int input;
		const char *name;
	} clicks[] =
	{
		{ INPUT_MOUSE + SDL_BUTTON_LEFT, "Left Click" },
		{ INPUT_MOUSE + SDL_BUTTON_RIGHT, "Right Click" },
		{ INPUT_MOUSE + SDL_BUTTON_MIDDLE, "Middle Click" },
	};
	char binding[64];
	size_t index;

	for (index = 0; index < sizeof(clicks) / sizeof(clicks[0]); index++)
	{
		if (clicks[index].input == input)
		{
			snprintf(name, size, "%s", clicks[index].name);
			return;
		}
	}
	halo_input_name(input, binding, sizeof(binding));
	halo_input_shown_name(binding, name, size);
}

int halo_keyboard_action_name(int action, char *name, unsigned long size)
{
	int slot, pass;

	if (size)
		name[0] = 0;
	if (action < 0 || action >= NUMBER_OF_HALO_KEYBOARD_ACTIONS || !size)
		return 0;
	bindings_read();
	/* (a key or button before the wheel: "Press 1", not "Press Wheel") */
	for (pass = 0; pass < 2; pass++)
	{
		for (slot = 0; slot < MAXIMUM_BINDINGS; slot++)
		{
			int input = bindings[action][slot];

			if (input >= 0 && (pass || input < INPUT_WHEEL))
			{
				prompt_binding_name(input, name, size);
				return name[0] != 0;
			}
		}
	}
	return 0;
}

/* what port 0's reading used (XInputGetState): the actions bound to keys
and mouse buttons held, the captured mouse moved, the gamepad off rest, the
touch controls changed the reading or moved the player or the aim */
static void prompt_device_update(unsigned long held, SDL_Gamepad *gamepad, int gamepads, BOOL touched)
{
	BOOL keyboard_used = held != 0;
	BOOL controller_used = gamepad && !gamepad_idle(gamepad);

	pthread_mutex_lock(&mouse_lock);
	if (mouse_aimed_ms != prompt_mouse_seen_ms)
	{
		prompt_mouse_seen_ms = mouse_aimed_ms;
		keyboard_used = TRUE;
	}
#ifdef HALO_TOUCH_CONTROLS
	if (touch_aimed_ms != prompt_touch_seen_ms)
	{
		prompt_touch_seen_ms = touch_aimed_ms;
		touched = TRUE;
	}
#endif
	pthread_mutex_unlock(&mouse_lock);
	prompt_controller_connected = gamepads > 0;
	prompt_device = prompt_device_next(prompt_device, keyboard_used, controller_used || touched);
}

/* A scroll of the wheel switches weapons once: it holds Y for WHEEL_PRESS_MS
once the wheel has turned a notch, and the scroll lasts until the wheel has
been still for WHEEL_SCROLL_GAP_MS. One notch often arrives as several events
over a few tens of milliseconds (high-resolution and smooth-scrolling
wheels), and one flick turns several notches; switching for each would bring
the same weapon straight back. Timed in milliseconds, not polls: polls come
once a frame, at the display's refresh rate. */
#define WHEEL_PRESS_MS 50
#define WHEEL_SCROLL_GAP_MS 200
/* how far the wheel turns before it switches: a notch */
#define WHEEL_NOTCH 1.0f

/* debug.test_input "bot:<seed>": a scripted player for the automated
network tests (port/linux/game/network_test.c), different for each seed:
it walks and strafes in circles, turns, fires every few seconds, jumps now
and then and throws a grenade every seven seconds; "look:<seed>" stands
still, only turning and looking up and down (where remote players aim and
whether they stand); "walk:<seed>" plays as "bot" but never turns (its
input is then the same each tick at any frame rate: a turn is the stick's
rate over each frame's time) */
static int test_input_holding_action;
static double test_input_holding_action_since;

/* the scripted player's clock: the game's time while one runs (the main
menu's too), so that it plays alike at any speed and tick for tick
(debug.fixed_step; and two runs of the same game alike, the determinism
check, debug.determinism_log), else real time */
static double test_input_seconds(void)
{
	double seconds = network_test_game_seconds();

	return seconds >= 0.0 ? seconds : (double)SDL_GetTicks() / 1000.0;
}

/* the automated tests (port/linux/game/network_test.c): the scripted player
stands still, holding the action button (X: picking up, swapping weapons)
after a second */
void test_input_hold_action(int hold)
{
	if (hold && !test_input_holding_action)
		test_input_holding_action_since = test_input_seconds();
	test_input_holding_action = hold;
}

/* "pad": the VR mode's gamepad (vr.aim = "gamepad") put through its paces,
in a cycle of phases that the log names as each begins */
static const struct test_pad_phase
{
	const char *name;
	double seconds;
	float lx, ly, rx, ry;
	int right_trigger, left_trigger, back;
} test_pad_phases[] =
{
	{ "still", 4.0 },
	{ "turn right", 3.0, 0.0f, 0.0f, 0.6f },
	{ "still", 2.0 },
	{ "look up", 1.0, 0.0f, 0.0f, 0.0f, 0.8f },
	{ "still", 2.0 },
	{ "look down", 2.0, 0.0f, 0.0f, 0.0f, -0.8f },
	{ "still", 2.0 },
	{ "look up", 1.0, 0.0f, 0.0f, 0.0f, 0.8f },
	{ "fire", 1.5, 0.0f, 0.0f, 0.0f, 0.0f, 1 },
	{ "throw a grenade", 0.3, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1 },
	{ "still", 3.0 },
	{ "hold back (recentre)", 1.5, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 1 },
	{ "still", 2.0 },
	{ "walk forward, turn left", 3.0, 0.0f, 0.8f, -0.5f },
	{ "still", 3.0 },
};
static int test_input_back;

static void test_pad_gamepad(XINPUT_GAMEPAD *pad)
{
	static Uint64 start;
	static int last_phase = -1;
	double cycle = 0.0, t;
	int phase, count = (int)(sizeof(test_pad_phases) / sizeof(test_pad_phases[0]));
	const struct test_pad_phase *p;

	if (!start)
		start = SDL_GetTicks();
	for (phase = 0; phase < count; phase++)
		cycle += test_pad_phases[phase].seconds;
	t = fmod((double)(SDL_GetTicks() - start) / 1000.0, cycle);
	for (phase = 0; phase < count - 1 && t >= test_pad_phases[phase].seconds; phase++)
		t -= test_pad_phases[phase].seconds;
	p = &test_pad_phases[phase];
	if (phase != last_phase)
	{
		platform_log("test input: pad: %s", p->name);
		last_phase = phase;
	}
	pad->sThumbLX = (SHORT)(p->lx * 32767.0f);
	pad->sThumbLY = (SHORT)(p->ly * 32767.0f);
	pad->sThumbRX = (SHORT)(p->rx * 32767.0f);
	pad->sThumbRY = (SHORT)(p->ry * 32767.0f);
	if (p->right_trigger)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	if (p->left_trigger)
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;
	if (p->back)
		pad->wButtons |= XINPUT_GAMEPAD_BACK;
	test_input_back = p->back;
}

/* the automated tests (port/linux/game/network_test.c): the controller's
buttons (halo_chat_pad_button bits) held for the reads to come, as a player
types on the text chat's on-screen keyboard; the scripted player does
nothing else while that keyboard is up */
static unsigned long test_input_pressed;

void test_input_press(unsigned long buttons)
{
	test_input_pressed = buttons;
}

/* debug.test_input "menu:<buttons>": the buttons pressed one a second, from
the first poll, for testing the menus: a, b, x, y, up, down, left, right,
start, back, lb and rb (the white and black buttons), lt and rt (the
triggers), forward (the left
stick pushed forward, which moves a player whom no menu has), stick_left
and stick_right (the left stick pushed sideways), screenshot
(the Screenshot key's capture, once), or wait (none), separated by spaces
or commas; two or more joined by + ("back+x") hold the first through the
second and press the others half way through it; "hold:<button>" holds it
the whole second (one after another, held on: the menus' repeat); one
after "p<n>:" ("p2:start") is played on controller n (2 to 4: another
player's, connected for the script) rather than the first. A script
that starts with "ready" waits for the menus' screen to be up
(platform_test_menu_start, menu_tour.c's) and counts its seconds from then:
tools/menu_parity.py's, whose runs share the computer */
static char test_input_menu[2048];
static Uint64 test_input_menu_since;
/* ("ready": the seconds not counted yet) */
static int test_input_menu_waiting;

int frames_clock(unsigned long *milliseconds);

/* the script's clock: the frames' with their time fixed (debug.frame_time,
port/linux/game/frames_clock.c), which the menus' clock is then too, so that
a press lasts as many of the menus' frames in every run; else the
computer's */
static Uint64 test_input_menu_now(void)
{
	unsigned long milliseconds;

	return frames_clock(&milliseconds) ? (Uint64)milliseconds : SDL_GetTicks();
}

/* the time into the script (FALSE for none, or one waiting for the menus) */
static int test_input_menu_elapsed(Uint64 *elapsed)
{
	if (!test_input_menu[0] || test_input_menu_waiting)
		return 0;
	*elapsed = test_input_menu_now() - test_input_menu_since;
	return 1;
}

void platform_test_menu_start(void)
{
	if (!test_input_menu_waiting)
		return;
	test_input_menu_waiting = 0;
	test_input_menu_since = test_input_menu_now();
}

int platform_test_menu_step(void)
{
	Uint64 elapsed;

	/* (a second's last 100 ms counted as the next's: where its press left
	the menus is logged at its end, menu_tour.c's) */
	return test_input_menu_elapsed(&elapsed) ? (int)((elapsed + 100) / 1000) : -1;
}

int platform_test_menu_waiting(void)
{
	return test_input_menu_waiting;
}

int platform_test_menu_done(void)
{
	const char *token = test_input_menu;
	int count = 0;

	for (;;)
	{
		size_t length;

		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length)
			break;
		count++;
		token += length;
	}
	return platform_test_menu_step() >= count;
}

/* the ports past the first that the menu script plays (p<n>:), a bit each */
static unsigned int test_input_menu_ports;

/* whether a token of the menu script is another controller's, "p<n>:" */
static int test_input_menu_port_token(const char *token, size_t length)
{
	return length > 3 && token[0] == 'p' && token[1] >= '2' && token[1] <= '0' + PORT_COUNT && token[2] == ':';
}

/* (the menu script's controllers: each "p<n>:" token's port) */
static void test_input_menu_ports_read(void)
{
	const char *token = test_input_menu;

	for (;;)
	{
		size_t length;

		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length)
			break;
		if (test_input_menu_port_token(token, length))
			test_input_menu_ports |= 1U << (token[1] - '1');
		token += length;
	}
}

static void test_input_menu_gamepad(XINPUT_GAMEPAD *pad, int port)
{
	static const struct
	{
		const char *name;
		int analog;
		WORD digital;
	} buttons[] =
	{
		{ "a", XINPUT_GAMEPAD_A, 0 },
		{ "b", XINPUT_GAMEPAD_B, 0 },
		{ "x", XINPUT_GAMEPAD_X, 0 },
		{ "y", XINPUT_GAMEPAD_Y, 0 },
		{ "up", -1, XINPUT_GAMEPAD_DPAD_UP },
		{ "down", -1, XINPUT_GAMEPAD_DPAD_DOWN },
		{ "left", -1, XINPUT_GAMEPAD_DPAD_LEFT },
		{ "right", -1, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ "start", -1, XINPUT_GAMEPAD_START },
		{ "back", -1, XINPUT_GAMEPAD_BACK },
		{ "lb", XINPUT_GAMEPAD_WHITE, 0 },
		{ "rb", XINPUT_GAMEPAD_BLACK, 0 },
		{ "lt", XINPUT_GAMEPAD_LEFT_TRIGGER, 0 },
		{ "rt", XINPUT_GAMEPAD_RIGHT_TRIGGER, 0 },
	};
	Uint64 elapsed;
	Uint64 step;
	Uint64 into;
	const char *token = test_input_menu;
	size_t length;
	size_t part;
	unsigned int index;
	BOOL held = FALSE;

	if (!test_input_menu_elapsed(&elapsed))
		return;
	step = elapsed / 1000;
	into = elapsed % 1000;
	for (;;)
	{
		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length)
			return;
		if (!step)
			break;
		step--;
		token += length;
	}
	/* (another controller's, "p<n>:": played on that port alone) */
	if (test_input_menu_port_token(token, length))
	{
		if (token[1] - '1' != port)
			return;
		token += 3;
		length -= 3;
	}
	else if (port)
		return;
	/* (Save Report's, as its key asks: one in its second) */
	if (length == 6 && !strncmp(token, "report", 6))
	{
		static Uint64 report_second = (Uint64)-1;

		if (report_second != elapsed / 1000 && into < 150)
		{
			report_request(1);
			report_second = elapsed / 1000;
		}
		return;
	}
	if (length == 10 && !strncmp(token, "screenshot", 10))
	{
		static Uint64 shot_second = (Uint64)-1;

		if (shot_second != elapsed / 1000 && into < 150)
		{
			platform_screenshot_request();
			shot_second = elapsed / 1000;
		}
		return;
	}
	if (length > 5 && !strncmp(token, "hold:", 5))
	{
		held = TRUE;
		token += 5;
		length -= 5;
	}
	/* (a button pressed for the first 150 ms of its second; "<held>+<pressed>"
	holds the first through its second and presses the others 500 ms into it,
	as one hand holds Back while the other presses X) */
	for (part = 0; length; part++)
	{
		size_t name_length = strcspn(token, "+ ,");
		BOOL down;

		if (name_length > length)
			name_length = length;
		if (part == 0)
			down = held || (name_length < length && token[name_length] == '+') ? TRUE : into < 150;
		else
			down = into >= 500 && into < 650;
		if (down && name_length == 7 && !strncmp(token, "forward", 7))
			pad->sThumbLY = 32767;
		if (down && name_length == 10 && !strncmp(token, "stick_left", 10))
			pad->sThumbLX = -32768;
		if (down && name_length == 11 && !strncmp(token, "stick_right", 11))
			pad->sThumbLX = 32767;
		for (index = 0; down && index < sizeof(buttons) / sizeof(buttons[0]); index++)
		{
			if (strlen(buttons[index].name) == name_length && !strncmp(token, buttons[index].name, name_length))
			{
				if (buttons[index].analog >= 0)
					pad->bAnalogButtons[buttons[index].analog] = 255;
				else
					pad->wButtons |= buttons[index].digital;
			}
		}
		token += name_length;
		length -= name_length;
		if (length && *token == '+')
		{
			token++;
			length--;
		}
	}
}

/* debug.test_input "menu:...": its "click:<x>:<y>" (x and y, 0 to 1 of the
window) clicks the menus' pointer there, once, in its second
(platform_ui_pointer_read, sdl_platform.c: the mouse's own path through the
menus), and its "point:<x>:<y>" moves the pointer there without a click (a
hover); returns 1 for a click, 2 for a move */
int platform_test_menu_click(float *x, float *y)
{
	static Uint64 clicked_step = (Uint64)-1;
	Uint64 step, elapsed;
	const char *token = test_input_menu;
	size_t length;

	if (!test_input_menu_elapsed(&elapsed))
		return 0;
	step = elapsed / 1000;
	for (length = 0;; step--)
	{
		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length || !step)
			break;
		token += length;
	}
	step = elapsed / 1000;
	if (!length || (strncmp(token, "click:", 6) && strncmp(token, "point:", 6)) || step == clicked_step ||
		sscanf(token + 6, "%f:%f", x, y) != 2)
		return 0;
	clicked_step = step;
	return *token == 'c' ? 1 : 2;
}

/* debug.test_input "menu:...": its "mouse:<button>", a click of that mouse
button (SDL's numbering: 4 is X1, the side button a browser goes back with),
and its "wheel:<notches>[*<events>]", the wheel turned that far (away from
the player positive, as SDL's), in that many events (1 by default: a
trackpad's or a high-resolution wheel's come in fractions), once in its
second, as the mouse's own events (sdl_platform.c's test_menu_mouse) */
int platform_test_menu_mouse(int *button, float *wheel, int *wheel_events)
{
	static Uint64 played_step = (Uint64)-1;
	Uint64 step, elapsed;
	const char *token = test_input_menu;
	size_t length = 0;

	*button = 0;
	*wheel = 0.0f;
	*wheel_events = 0;
	if (!test_input_menu_elapsed(&elapsed))
		return 0;
	for (step = elapsed / 1000;; step--)
	{
		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length || !step)
			break;
		token += length;
	}
	step = elapsed / 1000;
	if (!length || step == played_step)
		return 0;
	if (!strncmp(token, "mouse:", 6) && sscanf(token + 6, "%d", button) == 1 && *button > 0)
	{
		played_step = step;
		return 1;
	}
	*button = 0;
	if (!strncmp(token, "wheel:", 6) && sscanf(token + 6, "%f", wheel) == 1)
	{
		const char *times = memchr(token, '*', length);

		*wheel_events = times ? atoi(times + 1) : 1;
		if (*wheel_events < 1)
			*wheel_events = 1;
		played_step = step;
		return 1;
	}
	*wheel = 0.0f;
	return 0;
}

/* debug.test_input "menu:...": its "type:<letters and digits>" typed once in
its second, and its "enter", the Return key held for the first 150 ms of
its second and its keystroke once, through the keyboard's own path
(sdl_platform.c's keystroke queue and keys held): *typed the letters to type
now (empty for none), *enter whether Return is held; 1 when the keystrokes
are to be queued now */
int platform_test_menu_keys(char *typed, int size, int *enter)
{
	static Uint64 typed_step = (Uint64)-1;
	Uint64 elapsed, step, into;
	const char *token = test_input_menu;
	size_t length = 0;
	int queue;

	typed[0] = 0;
	*enter = 0;
	if (size <= 0 || !test_input_menu_elapsed(&elapsed))
		return 0;
	into = elapsed % 1000;
	for (step = elapsed / 1000;; step--)
	{
		token += strspn(token, " ,");
		length = strcspn(token, " ,");
		if (!length || !step)
			break;
		token += length;
	}
	if (!length)
		return 0;
	queue = typed_step != elapsed / 1000;
	if (length == 5 && !strncmp(token, "enter", 5))
	{
		*enter = into < 150;
		typed_step = elapsed / 1000;
		return queue;
	}
	if (length <= 5 || strncmp(token, "type:", 5) || !queue)
		return 0;
	typed_step = elapsed / 1000;
	length -= 5;
	if (length > (size_t)size - 1)
		length = (size_t)size - 1;
	memcpy(typed, token + 5, length);
	typed[length] = 0;
	return 1;
}

/* debug.test_input "script:<event>;<event>...": the controller played on
the simulation's clock (sim_trace_scenario_seconds), in seconds from the
scenario's start, so that one script plays alike at 30, 60 and 120 steps a
second (tools/scorecard). An event is "<start>[-<end>] <action>":

    fire, throw, jump, melee, action (X), switch (Y), crouch, zoom,
    flashlight (white), back, start
                         the button held from start to end (0.1 seconds
                         without an end)
    move=<x>,<y>         the left stick (-1 to 1 each way), start to end
    look=<x>,<y>         the right stick, start to end
    aim=<yaw>,<pitch>    the view turned there (degrees) at start, once
    hunt                 from start to end, the view turned toward the
                         nearest enemy in sight and fire held once on it,
                         the player moving to fight it (bots_hunt: the
                         campaign's footage); a move= after it overrides
                         the moving
    gyro=<x>,<y>,<z>     a controller's gyroscope turning at these rates
                         (degrees a second about its axes, SDL's: x to its
                         right, y up, z toward the player), start to end;
                         the events at a time add up. Its samples, a
                         thousand a second, go through what a real
                         controller's do (gyro_test_feed)
    tilt=<x>,<y>,<z>     its accelerometer reading up along this direction
                         (its axes), start to end; level (0,1,0) else
    gyro_button          its gyroscope button (input.gyro_button) held
    stick_touch          the thumb on its right stick (its touch, as a
                         Steam Deck's), start to end (input.gyro_aim's
                         "touch")
    gyro_calibrate       its calibration asked for at start, once
    key=<action>         a keyboard action held (its config.toml name,
                         controls.<action>: move_forward, walk), start
                         to end, as its key would be (halo_keyboard_actions)
    up, down, left, right
                         the D-pad held, start to end (a menu's moves)

An action after "p<n> " is played on controller n (2 to 4: a split screen
player's, connected for the script while it has any) rather than the
first: its buttons, D-pad and sticks ("4 p2 start; 5 p2 down; 6 p2 jump").
A script's controller is read as a connected one is (the first's too): its
presses its own (test_script_pressed), which the D-pad's pings and Spartan
link take as a real controller's.

A time on the 30 Hz grid (a multiple of 1/30) falls on a step at every
rate. */
enum
{
	TEST_SCRIPT_EVENTS = 64,
};

static struct test_script_event
{
	double start, end;
	int analog, digital;
	int stick;
	float x, y;
	int aim, aimed;
	int hunt;
	/* (mouse=: its motion in counts a second, played with the window's
	events, halo_test_input_pump) */
	int mouse;
	/* (gyro=, tilt=: the rates or the direction in x, y and z) */
	int gyro, tilt, gyro_button, stick_touch, gyro_calibrate;
	float z;
	/* (key=: the keyboard action held, plus one) */
	int key;
	/* (p<n>: the controller's port, 0 for the first) */
	int port;
} test_script[TEST_SCRIPT_EVENTS];
static int test_script_count;
static int test_script_on;
/* the ports past the first that the script plays (p<n>), a bit each */
static unsigned int test_script_ports;
/* the buttons and D-pad the script presses on each controller at this
reading: a controller's own presses (chat_physical_pad), as a connected
one's are, for the D-pad's pings and Spartan link */
static XINPUT_GAMEPAD test_script_pressed[PORT_COUNT];
/* the script plays a controller's gyroscope (gyro_test_feed) */
static int test_script_gyro;

static void test_script_parse(const char *script)
{
	static const struct
	{
		const char *name;
		int analog, digital;
	} buttons[] =
	{
		{ "fire", XINPUT_GAMEPAD_RIGHT_TRIGGER, 0 },
		{ "throw", XINPUT_GAMEPAD_LEFT_TRIGGER, 0 },
		{ "jump", XINPUT_GAMEPAD_A, 0 },
		{ "melee", XINPUT_GAMEPAD_B, 0 },
		{ "action", XINPUT_GAMEPAD_X, 0 },
		{ "switch", XINPUT_GAMEPAD_Y, 0 },
		{ "crouch", -1, XINPUT_GAMEPAD_LEFT_THUMB },
		{ "zoom", -1, XINPUT_GAMEPAD_RIGHT_THUMB },
		{ "flashlight", XINPUT_GAMEPAD_WHITE, 0 },
		{ "back", -1, XINPUT_GAMEPAD_BACK },
		{ "start", -1, XINPUT_GAMEPAD_START },
		{ "up", -1, XINPUT_GAMEPAD_DPAD_UP },
		{ "down", -1, XINPUT_GAMEPAD_DPAD_DOWN },
		{ "left", -1, XINPUT_GAMEPAD_DPAD_LEFT },
		{ "right", -1, XINPUT_GAMEPAD_DPAD_RIGHT },
	};

	test_script_on = 1;
	while (*script && test_script_count < TEST_SCRIPT_EVENTS)
	{
		struct test_script_event *event = &test_script[test_script_count];
		char action[32];
		char *end;
		size_t length;
		unsigned int index;

		script += strspn(script, " ;");
		if (!*script)
			break;
		length = strcspn(script, ";");
		memset(event, 0, sizeof(*event));
		event->analog = -1;
		event->start = strtod(script, &end);
		event->end = event->start + 0.1;
		if (*end == '-')
			event->end = strtod(end + 1, &end);
		end += strspn(end, " ");
		snprintf(action, sizeof(action), "%.*s", (int)(script + length - end), end);
		script += length;
		/* (another controller's: p<n>) */
		if (action[0] == 'p' && action[1] >= '1' && action[1] <= '0' + PORT_COUNT && action[2] == ' ')
		{
			event->port = action[1] - '1';
			memmove(action, action + 3, strlen(action + 3) + 1);
			if (event->port)
				test_script_ports |= 1U << event->port;
		}
		for (index = 0; index < sizeof(buttons) / sizeof(buttons[0]); index++)
		{
			if (!strcmp(action, buttons[index].name))
			{
				event->analog = buttons[index].analog;
				event->digital = buttons[index].digital;
				break;
			}
		}
		if (index < sizeof(buttons) / sizeof(buttons[0]))
			;
		else if (sscanf(action, "move=%f,%f", &event->x, &event->y) == 2)
			event->stick = 1;
		else if (sscanf(action, "look=%f,%f", &event->x, &event->y) == 2)
			event->stick = 2;
		else if (sscanf(action, "aim=%f,%f", &event->x, &event->y) == 2)
			event->aim = 1;
		else if (sscanf(action, "mouse=%f,%f", &event->x, &event->y) == 2)
			event->mouse = 1;
		else if (!strcmp(action, "hunt"))
			event->hunt = 1;
		else if (sscanf(action, "gyro=%f,%f,%f", &event->x, &event->y, &event->z) == 3)
			event->gyro = test_script_gyro = 1;
		else if (sscanf(action, "tilt=%f,%f,%f", &event->x, &event->y, &event->z) == 3)
			event->tilt = test_script_gyro = 1;
		else if (!strcmp(action, "gyro_button"))
			event->gyro_button = test_script_gyro = 1;
		else if (!strcmp(action, "stick_touch"))
			event->stick_touch = test_script_gyro = 1;
		else if (!strcmp(action, "gyro_calibrate"))
			event->gyro_calibrate = test_script_gyro = 1;
		else if (!strncmp(action, "key=", 4))
		{
			int key;

			for (key = 0; key < NUMBER_OF_HALO_KEYBOARD_ACTIONS; key++)
			{
				if (!strcmp(action + 4, binding_settings[key] + sizeof("controls.") - 1))
					event->key = key + 1;
			}
			if (!event->key)
			{
				platform_log("test input: script: no keyboard action \"%s\": left out", action + 4);
				continue;
			}
		}
		else
		{
			platform_log("test input: script: no action \"%s\": left out", action);
			continue;
		}
		test_script_count++;
	}
	platform_log("test input: script of %d events", test_script_count);
}

static SHORT test_script_axis(float value)
{
	return (SHORT)(value < -1.0f ? -32767 : value > 1.0f ? 32767 : value * 32767.0f);
}

static void test_script_port_gamepad(int port, XINPUT_GAMEPAD *pad)
{
	/* (a hair before a step's time is that step's: 1/30 in a double is not
	exact) */
	double t = sim_trace_scenario_seconds() + 1e-6;
	XINPUT_GAMEPAD *pressed = &test_script_pressed[port];
	int index;

	memset(pressed, 0, sizeof(*pressed));
	if (t < 0.0)
		return;
	for (index = 0; index < test_script_count; index++)
	{
		struct test_script_event *event = &test_script[index];

		if (event->port != port || (port && (event->aim || event->hunt || event->key)))
			continue;
		if (event->aim)
		{
			if (!event->aimed && t >= event->start)
			{
				event->aimed = 1;
				sim_trace_aim(event->x, event->y);
			}
			continue;
		}
		/* (the gyroscope's are gyro_test_feed's) */
		if (event->gyro || event->tilt || event->gyro_button || event->stick_touch || event->gyro_calibrate)
			continue;
		if (t < event->start || t >= event->end)
			continue;
		if (event->hunt)
		{
			float forward, strafe;

			if (bots_hunt(&forward, &strafe))
				pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
			/* (its moves, which a later move in the script overrides) */
			pad->sThumbLX = test_script_axis(strafe);
			pad->sThumbLY = test_script_axis(forward);
			continue;
		}
		if (event->analog >= 0)
			pad->bAnalogButtons[event->analog] = pressed->bAnalogButtons[event->analog] = 255;
		pad->wButtons |= (WORD)event->digital;
		pressed->wButtons |= (WORD)event->digital;
		if (event->stick == 1)
		{
			pad->sThumbLX = test_script_axis(event->x);
			pad->sThumbLY = test_script_axis(event->y);
		}
		else if (event->stick == 2)
		{
			pad->sThumbRX = test_script_axis(event->x);
			pad->sThumbRY = test_script_axis(event->y);
		}
	}
}

/* the keyboard actions the script holds now (key=), a bit each */
static unsigned long test_script_keys(void)
{
	double t = sim_trace_scenario_seconds() + 1e-6;
	unsigned long held = 0;
	int index;

	if (!test_script_on || t < 0.0)
		return 0;
	for (index = 0; index < test_script_count; index++)
	{
		if (test_script[index].key && !test_script[index].port && t >= test_script[index].start &&
			t < test_script[index].end)
			held |= 1UL << (test_script[index].key - 1);
	}
	return held;
}

/* debug.test_input "latency:<seed>[:<kinds>]": in turn, the controller's
fire control pulled for 60 ms (fire), its left stick pushed forward for
200 ms (move), the mouse's left button held for 60 ms (click) and the mouse
moved LATENCY_TURN_COUNTS counts at once, left and right by turns (turn),
each 350 to 650 ms after the last let go, on the real clock: presses that
fall anywhere in a tick, as a person's do, for debug.latency_log
(port/linux/game/latency_log.c). <kinds> names those played, separated by
commas (all four by default). The player otherwise stands still. (A weapon
that fires as the control is pulled: SWAT's pistol, debug.network_test
"local:<map>:swat"; the plasma pistol, a multiplayer game's otherwise,
fires as it is let go.) The mouse's come with the window's events
(halo_test_input_pump), the controller's with its reading, each read by
the first frame to start after its time, as a person's is */
#define LATENCY_TURN_COUNTS 200.0f

enum
{
	_test_latency_fire,
	_test_latency_move,
	_test_latency_click,
	_test_latency_turn,
	_test_latency_menu,
	NUMBER_OF_TEST_LATENCY_KINDS,

	/* the kinds played with none named: not the menu's D-pad */
	TEST_LATENCY_DEFAULT_KINDS = (1u << _test_latency_menu) - 1
};

static struct
{
	/* 0: not read yet; 1: on; -1: off */
	int state;
	unsigned kinds;
	unsigned long random_state;
	/* the press under way (until until_ns), and when the next comes */
	int kind;
	Uint64 next_ns, until_ns;
	/* the presses begun, and the last the mouse played (halo_test_input_pump) */
	unsigned long pressed, mouse_played;
	int turn_direction;
} test_latency;

static BOOL test_latency_on(void)
{
	if (!test_latency.state)
	{
		static const char *const names[NUMBER_OF_TEST_LATENCY_KINDS] = { "fire", "move", "click", "turn", "menu" };
		const char *setting = config_string("debug.test_input");
		const char *kinds;
		int kind;

		test_latency.state = -1;
		if (strncmp(setting, "latency:", 8))
			return FALSE;
		test_latency.state = 1;
		test_latency.random_state = 12345u + (unsigned long)atoi(setting + 8) * 7919u;
		kinds = strchr(setting + 8, ':');
		for (kind = 0; kind < NUMBER_OF_TEST_LATENCY_KINDS; kind++)
		{
			if (kinds && strstr(kinds + 1, names[kind]))
				test_latency.kinds |= 1u << kind;
		}
		if (!test_latency.kinds)
			test_latency.kinds = TEST_LATENCY_DEFAULT_KINDS;
		test_latency.kind = -1;
		test_latency.turn_direction = 1;
	}
	return test_latency.state > 0;
}

/* the press under way at this time (its kind), or -1 */
static int test_latency_advance(Uint64 now)
{
	if (!test_latency_on())
		return -1;
	if (!test_latency.next_ns)
		test_latency.next_ns = now + 1000000000ull;
	if (now >= test_latency.next_ns)
	{
		do
			test_latency.kind = (test_latency.kind + 1) % NUMBER_OF_TEST_LATENCY_KINDS;
		while (!(test_latency.kinds & (1u << test_latency.kind)));
		/* (pressed when its time came, which this reading may be a frame
		after) */
		latency_log_pressed(test_latency.next_ns);
		test_latency.pressed++;
		test_latency.until_ns = test_latency.next_ns +
			(test_latency.kind == _test_latency_move ? 200000000ull :
			test_latency.kind == _test_latency_turn ? 1000000ull : 60000000ull);
		test_latency.random_state = test_latency.random_state * 1103515245u + 12345u;
		test_latency.next_ns = test_latency.until_ns + 350000000ull +
			(Uint64)((test_latency.random_state >> 8) % 300000u) * 1000ull;
	}
	return now < test_latency.until_ns ? test_latency.kind : -1;
}

static void test_latency_gamepad(XINPUT_GAMEPAD *pad)
{
	int kind = test_latency_advance(SDL_GetTicksNS());

	if (kind == _test_latency_fire)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	else if (kind == _test_latency_move)
		pad->sThumbLY = 32000;
	/* (down and up in turn, which keeps to a menu's first items) */
	else if (kind == _test_latency_menu)
		pad->wButtons |= test_latency.pressed & 1 ? XINPUT_GAMEPAD_DPAD_DOWN : XINPUT_GAMEPAD_DPAD_UP;
}

/* debug.test_input "script:"'s "mouse=<x>,<y>": the mouse moved at that
many counts a second (x right, y down) while the script's time is within the
event's, its motion coming with the window's events as a real mouse's does,
on the real clock: as much each time as the time since the last */
static void test_script_mouse(struct platform_input_state *state)
{
	static Uint64 last_ns;
	Uint64 now = SDL_GetTicksNS();
	double t, seconds;
	int index;

	seconds = last_ns ? (double)(now - last_ns) * 1e-9 : 0.0;
	last_ns = now;
	if (!test_script_on)
		return;
	t = sim_trace_scenario_seconds() + 1e-6;
	for (index = 0; index < test_script_count; index++)
	{
		struct test_script_event const *event = &test_script[index];

		if (!event->mouse || t < event->start || t >= event->end)
			continue;
		state->mouse_dx += event->x * (float)seconds;
		state->mouse_dy += event->y * (float)seconds;
	}
}

/* (sdl_platform.c's platform_pump_events, its input lock held, after the
window's events) the scripted mouse's own, as its events would bring them:
its button held or let go, its motion */
void halo_test_input_pump(struct platform_input_state *state, unsigned char *buttons_pressed)
{
	int kind;

	test_script_mouse(state);
	if (test_latency.state < 0 || !test_latency_on())
		return;
	kind = test_latency_advance(SDL_GetTicksNS());
	if (kind == _test_latency_click)
	{
		if (test_latency.mouse_played != test_latency.pressed)
			buttons_pressed[SDL_BUTTON_LEFT] = 1;
		state->mouse_buttons[SDL_BUTTON_LEFT] = 1;
	}
	else if (test_latency.kind == _test_latency_click && state->mouse_buttons[SDL_BUTTON_LEFT])
		state->mouse_buttons[SDL_BUTTON_LEFT] = 0;
	if (test_latency.kind == _test_latency_turn && test_latency.mouse_played != test_latency.pressed)
	{
		state->mouse_dx += LATENCY_TURN_COUNTS * (float)test_latency.turn_direction;
		test_latency.turn_direction = -test_latency.turn_direction;
	}
	test_latency.mouse_played = test_latency.pressed;
}

static void test_input_gamepad(XINPUT_GAMEPAD *pad)
{
	static const WORD digital[] =
	{
		XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
	};
	static const int analog[] = { XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y };
	static int checked;
	static int seed = -1;
	static int looking;
	static int walking;
	static int pad_test;
	double t;
	int index;

	for (index = 0; index < 4; index++)
	{
		if (test_input_pressed & (1UL << (HALO_CHAT_PAD_UP + index)))
			pad->wButtons |= digital[index];
		if (test_input_pressed & (1UL << (HALO_CHAT_PAD_A + index)))
			pad->bAnalogButtons[analog[index]] = 255;
	}
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_START))
		pad->wButtons |= XINPUT_GAMEPAD_START;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_UP))
		pad->sThumbLY = 32000;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_DOWN))
		pad->sThumbLY = -32000;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_LEFT))
		pad->sThumbLX = -32000;
	if (test_input_pressed & (1UL << HALO_CHAT_PAD_STICK_RIGHT))
		pad->sThumbLX = 32000;
	if (network_chat_keyboard_open() && network_chat_controller() == 0)
		return;

	if (!checked)
	{
		const char *setting = config_string("debug.test_input");

		checked = 1;
		if (!strncmp(setting, "menu:", 5))
		{
			snprintf(test_input_menu, sizeof(test_input_menu), "%s", setting + 5);
			test_input_menu_since = test_input_menu_now();
			if (!strncmp(test_input_menu, "ready", 5) && (!test_input_menu[5] ||
				strchr(" ,", test_input_menu[5])))
			{
				/* (its seconds counted once the menus are up) */
				memmove(test_input_menu, test_input_menu + 5, strlen(test_input_menu + 5) + 1);
				test_input_menu_waiting = 1;
			}
			test_input_menu_ports_read();
		}
		else if (!strncmp(setting, "bot:", 4))
			seed = atoi(setting + 4);
		else if (!strcmp(setting, "bot"))
			seed = 0;
		else if (!strncmp(setting, "look:", 5))
		{
			seed = atoi(setting + 5);
			looking = 1;
		}
		else if (!strncmp(setting, "walk:", 5))
		{
			seed = atoi(setting + 5);
			walking = 1;
		}
		else if (!strcmp(setting, "pad"))
		{
			seed = 0;
			pad_test = 1;
		}
		else if (!strncmp(setting, "latency:", 8))
			seed = 0;
		else if (!strncmp(setting, "script:", 7))
			test_script_parse(setting + 7);
	}
	if (test_script_on)
	{
		test_script_port_gamepad(0, pad);
		return;
	}
	if (test_input_menu[0])
	{
		test_input_menu_gamepad(pad, 0);
		return;
	}
	if (seed < 0)
		return;
	if (pad_test)
	{
		test_pad_gamepad(pad);
		return;
	}
	if (test_latency_on())
	{
		test_latency_gamepad(pad);
		return;
	}
	/* (a game's player alone, never the menus': its stick and its A every
	few seconds would work the lobby, whose GAME SETTINGS opens Server Setup
	over a network test's game, which makes it private, out of the server
	browser, and whose START GAME there sets its gametype) */
	if (main_menu_is_loaded())
		return;
	if (test_input_holding_action)
	{
		/* (standing still, the button held from a second on) */
		if (test_input_seconds() - test_input_holding_action_since >= 1.0)
			pad->bAnalogButtons[XINPUT_GAMEPAD_X] = 255;
		return;
	}
	t = test_input_seconds() + seed * 1.7;
	if (looking)
	{
		pad->sThumbRX = (SHORT)(sin(t * 0.5) * 14000.0);
		pad->sThumbRY = (SHORT)(sin(t * 0.3) * 32000.0);
		return;
	}
	pad->sThumbLY = (SHORT)(sin(t * 0.9) * 32000.0);
	pad->sThumbLX = (SHORT)(cos(t * 0.6 + seed) * 20000.0);
	if (!walking)
		pad->sThumbRX = (SHORT)(sin(t * 0.4) * 14000.0);
	if (fmod(t, 3.0) < 0.3)
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
	if (fmod(t, 5.0) < 0.1)
		pad->bAnalogButtons[XINPUT_GAMEPAD_A] = 255;
	if (fmod(t, 7.0) < 0.2)
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255;
}

/* the simulation's sub-steps (port/linux/game/sim_rate.h; at 60 and 120
Hz a tick runs 2 or 4 of them, each on the frame its time comes in,
game_time.c): the first controller read again at each sub-step's start, for
the player's own movement and aim in it (input_abstraction.c). The devices
are read once a frame, at the start of the frame's first sub-step (the
frame's reading is its reading), and a later one in the same frame (a frame
slower than a sub-step) has no newer reading than the frame's; what the
scripted player plays ("script:") is on the simulation's clock, and is
played again at the sub-step's time (the script's aim, an event, is taken
there and then, as a frame takes it). The frame's reading before the
scripted player's, and with it (the touch controls' and the chat
keyboard's, after it, are the frame's alone) */
static XINPUT_GAMEPAD substep_frame_devices;
static XINPUT_GAMEPAD substep_frame_reading;

/* the first controller at the running sub-step's start (sim_rate.h), in
*pad: 1 when it differs from what the frame read, else 0 (and *pad is the
frame's) */
int halo_linux_substep_gamepad(short gamepad_index, XINPUT_GAMEPAD *pad)
{
	*pad = substep_frame_reading;
	if (gamepad_index != 0 || !test_script_on)
		return 0;
	*pad = substep_frame_devices;
	test_input_gamepad(pad);
	if (!memcmp(pad, &substep_frame_reading, sizeof(*pad)))
		return 0;
	input_trace_pad(1, pad->wButtons, pad->sThumbLX, pad->sThumbLY, pad->sThumbRX, pad->sThumbRY,
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER], pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER],
		keyboard_actions_held, 0.0f, 0.0f);
	return 1;
}

static void wheel_update(void)
{
	Uint64 now = SDL_GetTicks();

	pthread_mutex_lock(&mouse_lock);
	/* (a chat line typed: the wheel scrolls its lines, network_chat.c) */
	if (network_chat_typing())
	{
		while (mouse_wheel_accumulated >= WHEEL_NOTCH)
		{
			chat_wheel_steps++;
			mouse_wheel_accumulated -= WHEEL_NOTCH;
		}
		while (mouse_wheel_accumulated <= -WHEEL_NOTCH)
		{
			chat_wheel_steps--;
			mouse_wheel_accumulated += WHEEL_NOTCH;
		}
		wheel_scrolling = FALSE;
		pthread_mutex_unlock(&mouse_lock);
		return;
	}
	if (!wheel_scrolling)
	{
		if (fabsf(mouse_wheel_accumulated) >= WHEEL_NOTCH)
		{
			wheel_scrolling = TRUE;
			wheel_direction = mouse_wheel_accumulated > 0.0f ? 1 : -1;
			wheel_press_until_ms = now + WHEEL_PRESS_MS;
		}
	}
	else if (now >= wheel_press_until_ms && now - wheel_moved_ms >= WHEEL_SCROLL_GAP_MS)
	{
		wheel_scrolling = FALSE;
		mouse_wheel_accumulated = 0.0f;
	}
	pthread_mutex_unlock(&mouse_lock);
}

/* ---------- SDL gamepads */

/* the SDL gamepads in connection order, at most one per port */
/* the joystick IDs of the gamepads sdl_gamepads listed last, in its order */
static SDL_JoystickID sdl_gamepad_ids[PORT_COUNT];

int sdl_gamepads(SDL_Gamepad **gamepads)
{
	SDL_JoystickID *ids;
	int count = 0, index, found = 0;

	memset(gamepads, 0, sizeof(SDL_Gamepad *) * PORT_COUNT);
	memset(sdl_gamepad_ids, 0, sizeof(sdl_gamepad_ids));
	ids = SDL_GetGamepads(&count);
	if (!ids)
		return 0;
#ifdef HALO_ANDROID
	{
		/* Android can list input devices with a few gamepad buttons (the
		emulator's keyboard, some phones' key devices) as generic gamepads:
		recognised controllers take the first ports */
		int pass;

		for (pass = 0; pass < 2; pass++)
		{
			for (index = 0; index < count && found < PORT_COUNT; index++)
			{
				SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[index]);
				SDL_GamepadType type;
				BOOL recognised;

				if (!gamepad)
					continue;
				type = SDL_GetGamepadType(gamepad);
				recognised = type != SDL_GAMEPAD_TYPE_UNKNOWN && type != SDL_GAMEPAD_TYPE_STANDARD;
				if (recognised == (pass == 0))
				{
					sdl_gamepad_ids[found] = ids[index];
					gamepads[found++] = gamepad;
				}
			}
		}
	}
#else
	for (index = 0; index < count && found < PORT_COUNT; index++)
	{
		SDL_Gamepad *gamepad = SDL_GetGamepadFromID(ids[index]);

		if (gamepad)
		{
			sdl_gamepad_ids[found] = ids[index];
			gamepads[found++] = gamepad;
		}
	}
#endif
	SDL_free(ids);
	return found;
}

/* whether one gamepad is port 1's (port_gamepad) */
static BOOL lone_gamepad_split;
/* pc_menu_split_players, as the game's thread last read it (XInputGetState):
port_gamepad also runs on the input thread's rumble (XInputSetState), which
must not read the game's state while the game's thread has it */
static BOOL split_players;

/* no button of the gamepad held, its sticks and triggers at rest */
static BOOL gamepad_idle(SDL_Gamepad *gamepad)
{
	int index;

	for (index = 0; index < SDL_GAMEPAD_BUTTON_COUNT; index++)
	{
		if (SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)index))
			return FALSE;
	}
	for (index = 0; index < SDL_GAMEPAD_AXIS_COUNT; index++)
	{
		if (abs(SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)index)) > 8000)
			return FALSE;
	}
	return TRUE;
}

/* the gamepad of a port: the first shares port 0 with the keyboard, but for
two or more players with one gamepad (co-op, split screen), port 1 has it
(the keyboard's player is 1, the gamepad's 2). It changes port only at rest:
a button held across the change would be pressed again on the other port
(the B that leaves a profile screen leaving the game as player 1's) */
static SDL_Gamepad *port_gamepad(SDL_Gamepad *gamepads[PORT_COUNT], int count, int port)
{
	if (count == 1)
	{
		BOOL split = split_players;

		if (split != lone_gamepad_split && gamepad_idle(gamepads[0]))
			lone_gamepad_split = split;
		if (lone_gamepad_split)
			return port == 1 ? gamepads[0] : NULL;
	}
	return port < count ? gamepads[port] : NULL;
}





/* whether port 0 (player 1) is the keyboard's and a gamepad's */
unsigned char halo_keyboard_shares_gamepad(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count = sdl_gamepads(gamepads);

	return port_gamepad(gamepads, count, 0) != NULL;
}

int halo_gamepad_count(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];

	return sdl_gamepads(gamepads);
}

static SHORT stick(Sint16 value, BOOL flip)
{
	int result = flip ? -(int)value - 1 : value;

	if (result < -32768) result = -32768;
	if (result > 32767) result = 32767;
	return (SHORT)result;
}

static void merge_button(XINPUT_GAMEPAD *pad, int analog_index, BOOL down)
{
	if (down)
		pad->bAnalogButtons[analog_index] = 0xff;
}

/* a gamepad's buttons pressed and triggers pulled since its last reading
(platform_gamepad_presses_take): a tap let go of before the reading */
struct gamepad_taps
{
	Uint32 buttons;
	unsigned int triggers;
};

/* the taps of one of sdl_gamepads' gamepads, taken: once a reading */
static void gamepad_taps_take(SDL_Gamepad *gamepads[PORT_COUNT], int count, SDL_Gamepad *gamepad,
	struct gamepad_taps *taps)
{
	int index;

	taps->buttons = 0;
	taps->triggers = 0;
	for (index = 0; gamepad && index < count; index++)
	{
		if (gamepads[index] == gamepad)
			platform_gamepad_presses_take(sdl_gamepad_ids[index], &taps->buttons, &taps->triggers);
	}
}

/* a button held, or pressed and let go of since the last reading */
static BOOL gamepad_button_down(SDL_Gamepad *gamepad, const struct gamepad_taps *taps, SDL_GamepadButton button)
{
	return SDL_GetGamepadButton(gamepad, button) || (taps && (taps->buttons & (1u << button)) != 0);
}

/* a trigger as 0 to 255; one pulled and let go of since the last reading
fully pulled */
static int gamepad_trigger(SDL_Gamepad *gamepad, const struct gamepad_taps *taps, SDL_GamepadAxis axis,
	unsigned int bit)
{
	int value = SDL_GetGamepadAxis(gamepad, axis);

	if (taps && (taps->triggers & bit) && value < PLATFORM_GAMEPAD_TRIGGER_PRESSED)
		return 255;
	return value * 255 / 32767;
}

static void sdl_gamepad_state(SDL_Gamepad *gamepad, const struct gamepad_taps *taps, XINPUT_GAMEPAD *pad)
{
	static const struct
	{
		SDL_GamepadButton button;
		WORD mask;
	} digital[] =
	{
		{ SDL_GAMEPAD_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP },
		{ SDL_GAMEPAD_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
		{ SDL_GAMEPAD_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT },
		{ SDL_GAMEPAD_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
		{ SDL_GAMEPAD_BUTTON_START, XINPUT_GAMEPAD_START },
		{ SDL_GAMEPAD_BUTTON_BACK, XINPUT_GAMEPAD_BACK },
		{ SDL_GAMEPAD_BUTTON_LEFT_STICK, XINPUT_GAMEPAD_LEFT_THUMB },
		{ SDL_GAMEPAD_BUTTON_RIGHT_STICK, XINPUT_GAMEPAD_RIGHT_THUMB },
	};
	int index;
	int left_trigger, right_trigger;
	SHORT value;

	for (index = 0; index < (int)(sizeof(digital) / sizeof(digital[0])); index++)
	{
		if (gamepad_button_down(gamepad, taps, digital[index].button))
			pad->wButtons |= digital[index].mask;
	}
	merge_button(pad, XINPUT_GAMEPAD_A, gamepad_button_down(gamepad, taps, SDL_GAMEPAD_BUTTON_SOUTH));
	merge_button(pad, XINPUT_GAMEPAD_B, gamepad_button_down(gamepad, taps, SDL_GAMEPAD_BUTTON_EAST));
	merge_button(pad, XINPUT_GAMEPAD_X, gamepad_button_down(gamepad, taps, SDL_GAMEPAD_BUTTON_WEST));
	merge_button(pad, XINPUT_GAMEPAD_Y, gamepad_button_down(gamepad, taps, SDL_GAMEPAD_BUTTON_NORTH));
	/* the Duke's white and black buttons sit where later pads have shoulders */
	merge_button(pad, XINPUT_GAMEPAD_WHITE, gamepad_button_down(gamepad, taps, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
	merge_button(pad, XINPUT_GAMEPAD_BLACK, gamepad_button_down(gamepad, taps, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));

	left_trigger = gamepad_trigger(gamepad, taps, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1);
	right_trigger = gamepad_trigger(gamepad, taps, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 2);
	if (left_trigger > pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER])
		pad->bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = (BYTE)left_trigger;
	if (right_trigger > pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER])
		pad->bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = (BYTE)right_trigger;

	/* a stick only overrides the keyboard when it is pushed further */
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX), FALSE);
	if (abs(value) > abs(pad->sThumbLX)) pad->sThumbLX = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY), TRUE);
	if (abs(value) > abs(pad->sThumbLY)) pad->sThumbLY = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX), FALSE);
	if (abs(value) > abs(pad->sThumbRX)) pad->sThumbRX = value;
	value = stick(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY), TRUE);
	if (abs(value) > abs(pad->sThumbRY)) pad->sThumbRY = value;
}

/* the back paddles held (controller_remap_paddles' bits: the left, the
right, the second left, the second right; a Steam Deck's L4, R4, L5, R5),
which only a custom layout's actions take (Button Remapping) */
static unsigned int sdl_gamepad_paddles(SDL_Gamepad *gamepad, const struct gamepad_taps *taps)
{
	static const SDL_GamepadButton paddles[4] =
	{
		SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1,
		SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2,
	};
	unsigned int held = 0;
	int index;

	for (index = 0; gamepad && index < 4; index++)
	{
		if (gamepad_button_down(gamepad, taps, paddles[index]))
			held |= 1u << index;
	}
	return held;
}

/* ---------- gyro aiming and the flick stick (gyro_aim.h)

A controller with a gyroscope (SDL's gamepad sensors: a DualSense, a
DualShock 4, a Switch Pro Controller or Joy-Cons, a Steam Deck's own
controls) turns the player as it turns, as the mouse does: a direct turn,
added to the stick's after the aim assist's magnetism has acted on that
(player_control.c), so that the magnetism and the stick's look acceleration
never touch it. Its sensors are on only while input.gyro_aim asks for them
(or a calibration runs); their samples come with the window's events
(sdl_platform.c: halo_gyro_sensor_event), each one turned into the player's
turn there (gyro_aim_sample) and kept for the controller's port until the
game takes it (halo_linux_gyro_look), as the mouse's motion is. Under
Steam Input a game is given Steam's virtual controller, which has no
sensors: the log says so (Steam keeps the gyroscope for its own mappings).

The flick stick (input.flick_stick) is the right stick read each frame
(flick_stick_update): pushed to the rim, the player turns to face where it
points; turned round the rim, the player turns with it. Its turn is the
game's as the gyroscope's is; the stick's own look is then none
(input_abstraction.c), and only with the default stick layout, whose right
stick looks.

The scripted player's gyroscope (debug.test_input "script:", gyro=, tilt=,
gyro_button, gyro_calibrate) is a controller of its own on port 0, whose
samples go through the same code (gyro_device_sample). */

/* (the triggers, which are axes: past half way is held) */
#define GYRO_BUTTON_LEFT_TRIGGER (-2)
#define GYRO_BUTTON_RIGHT_TRIGGER (-3)

static const struct
{
	const char *name;
	int button;
} gyro_buttons[] =
{
	{ "touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD },
	{ "left_paddle", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
	{ "right_paddle", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 },
	{ "misc", SDL_GAMEPAD_BUTTON_MISC1 },
	{ "left_bumper", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
	{ "right_bumper", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
	{ "left_trigger", GYRO_BUTTON_LEFT_TRIGGER },
	{ "right_trigger", GYRO_BUTTON_RIGHT_TRIGGER },
	{ "left_stick", SDL_GAMEPAD_BUTTON_LEFT_STICK },
	{ "right_stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK },
};

/* the settings (input.gyro_*, input.flick_stick), read as they change */
static struct
{
	int mode;
	int button;
	struct gyro_aim_settings aim;
	int flick_stick;
} gyro_settings;

static void gyro_settings_read(void)
{
	static unsigned long read_at = (unsigned long)-1;
	const char *mode, *button;
	unsigned int index;

	if (read_at == config_changes())
		return;
	read_at = config_changes();
	mode = config_string("input.gyro_aim");
	gyro_settings.mode = !strcmp(mode, "on") ? GYRO_AIM_ON : !strcmp(mode, "hold") ? GYRO_AIM_HOLD :
		!strcmp(mode, "pause") ? GYRO_AIM_PAUSE : !strcmp(mode, "touch") ? GYRO_AIM_TOUCH : GYRO_AIM_OFF;
	button = config_string("input.gyro_button");
	gyro_settings.button = gyro_buttons[0].button;
	for (index = 0; index < sizeof(gyro_buttons) / sizeof(gyro_buttons[0]); index++)
	{
		if (!strcmp(button, gyro_buttons[index].name))
			gyro_settings.button = gyro_buttons[index].button;
	}
	gyro_settings.aim.sensitivity = (float)config_real("input.gyro_sensitivity");
	if (!(gyro_settings.aim.sensitivity > 0.0f))
		gyro_settings.aim.sensitivity = 2.0f;
	gyro_settings.aim.vertical_sensitivity = (float)config_real("input.gyro_vertical_sensitivity");
	if (!(gyro_settings.aim.vertical_sensitivity > 0.0f))
		gyro_settings.aim.vertical_sensitivity = 1.0f;
	gyro_settings.aim.player_space = strcmp(config_string("input.gyro_space"), "local") != 0;
	gyro_settings.aim.smoothing = config_boolean("input.gyro_smoothing");
	gyro_settings.flick_stick = config_boolean("input.flick_stick");
}

#define GYRO_DEVICES 8

/* a controller's gyroscope, by its joystick ID (the scripted player's, the
test device: its own) */
struct gyro_device
{
	SDL_JoystickID id;
	/* what it has (looked at once), its sensors on; a Steam Input virtual
	controller; the touch of its right stick (gyro_stick_touch_known); the
	Steam Deck's own controls */
	BOOL looked;
	BOOL has_gyro, has_accelerometer, steam, stick_touch, deck;
	/* (Thumb on Stick's want of a touch said in the log) */
	BOOL touch_told;
	BOOL enabled;
	/* its gyroscope's turn counts (input.gyro_aim and its button) */
	BOOL active;
	/* the last gyroscope sample's time, nanoseconds */
	Uint64 sampled_ns;
	unsigned long samples;
	struct gyro_aim aim;
	/* the turn since the game took it, and when it was used last */
	float pending[2];
	Uint64 used;
};

static pthread_mutex_t gyro_lock = PTHREAD_MUTEX_INITIALIZER;
static struct gyro_device gyro_devices[GYRO_DEVICES];
static struct gyro_device gyro_test_device;
static Uint64 gyro_uses;
/* each port's controller's, as XInputGetState read it last */
static struct gyro_device *gyro_ports[PORT_COUNT];
/* the motion the frame's input took for the port's player
(halo_linux_gyro_look), and how many readings it has gone untaken (motion
nobody takes, in the menus, is dropped as the mouse's is) */
static BOOL gyro_taken[PORT_COUNT];
static unsigned long gyro_polls_unconsumed[PORT_COUNT];

/* the device of a joystick ID (made, the least used one's place taken: the
IDs never come back), NULL for none */
static struct gyro_device *gyro_device_of(SDL_JoystickID id, BOOL make)
{
	struct gyro_device *oldest = &gyro_devices[0];
	int index;

	if (!id)
		return NULL;
	for (index = 0; index < GYRO_DEVICES; index++)
	{
		if (gyro_devices[index].id == id)
		{
			gyro_devices[index].used = ++gyro_uses;
			return &gyro_devices[index];
		}
		if (gyro_devices[index].used < oldest->used)
			oldest = &gyro_devices[index];
	}
	if (!make)
		return NULL;
	memset(oldest, 0, sizeof(*oldest));
	oldest->id = id;
	oldest->used = ++gyro_uses;
	gyro_aim_reset(&oldest->aim);
	return oldest;
}

/* a gyroscope sample over seconds since the last (gyro_lock held) */
static void gyro_device_sample(struct gyro_device *device, const float gyro[3], float seconds)
{
	float turn[2];

	gyro_aim_sample(&device->aim, gyro, seconds, &gyro_settings.aim, turn);
	device->samples++;
	if (device->active)
	{
		device->pending[0] += turn[0];
		device->pending[1] += turn[1];
	}
}

/* sdl_platform.c: a gamepad's sensor sample (SDL_EVENT_GAMEPAD_SENSOR_UPDATE),
its time the sensor's (else the event's), nanoseconds */
void halo_gyro_sensor_event(SDL_JoystickID which, int sensor, const float *data, Uint64 sensor_ns, Uint64 event_ns)
{
	struct gyro_device *device;
	Uint64 now = sensor_ns ? sensor_ns : event_ns;

	pthread_mutex_lock(&gyro_lock);
	device = gyro_device_of(which, FALSE);
	if (device && device->enabled)
	{
		if (sensor == SDL_SENSOR_ACCEL)
			gyro_aim_accelerometer(&device->aim, data);
		else if (sensor == SDL_SENSOR_GYRO)
		{
			float seconds = device->sampled_ns && now > device->sampled_ns ?
				(float)((double)(now - device->sampled_ns) * 1e-9) : 0.0f;

			device->sampled_ns = now;
			gyro_device_sample(device, data, seconds);
		}
	}
	pthread_mutex_unlock(&gyro_lock);
}

/* whether the game runs on a Steam Deck: Steam says so to the games it
starts (SteamDeck=1), or the machine's name does (DMI's product name: Valve's
Jupiter, the LCD model, or Galileo, the OLED) */
int halo_steam_deck(void)
{
	static int known = -1;

	if (known < 0)
	{
#ifdef HALO_GUEST
		known = 0;
#else
		const char *steam_deck = getenv("SteamDeck");
		char name[64];
		FILE *file;

		known = steam_deck && !strcmp(steam_deck, "1");
		if (!known && (file = fopen("/sys/devices/virtual/dmi/id/product_name", "r")) != NULL)
		{
			if (fgets(name, sizeof(name), file))
			{
				name[strcspn(name, "\r\n")] = 0;
				known = !strcmp(name, "Jupiter") || !strcmp(name, "Galileo");
			}
			fclose(file);
		}
#endif
	}
	return known;
}

/* the gyroscope button held on a controller (a Steam Deck's misc1, its
Quick Access button, is Steam's: never) */
static BOOL gyro_button_held(struct gyro_device *device, SDL_Gamepad *gamepad, int button)
{
	if (!gamepad || (device->deck && button == SDL_GAMEPAD_BUTTON_MISC1))
		return FALSE;
	if (button == GYRO_BUTTON_LEFT_TRIGGER)
		return SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16384;
	if (button == GYRO_BUTTON_RIGHT_TRIGGER)
		return SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384;
	return SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)button);
}

/* looks once at what a controller has, and says it in the log */
static void gyro_device_look(struct gyro_device *device, SDL_Gamepad *gamepad, int port)
{
	device->looked = TRUE;
	device->has_gyro = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO);
	device->has_accelerometer = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL);
	device->steam = SDL_GetGamepadSteamHandle(gamepad) != 0;
#ifndef HALO_GUEST
	device->stick_touch = gyro_stick_touch_known(SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad));
	device->deck = gyro_steam_deck(SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad));
#endif
	if (device->has_gyro)
		platform_log("gyro: controller %u (port %d): a gyroscope%s%s", (unsigned)device->id, port + 1,
			device->has_accelerometer ? " and an accelerometer" : ", no accelerometer (local space only)",
			device->stick_touch ? ", the right stick's touch" : "");
	else if (device->steam)
		platform_log("gyro: controller %u (port %d): Steam Input's virtual controller, which passes no gyroscope "
			"to the game; to aim with the gyroscope here, turn Steam Input off for OpenH1 (%s), or use Steam's "
			"own gyro mapping", (unsigned)device->id, port + 1, halo_steam_deck() ?
			"Steam > OpenH1 > the controller icon > Disable Steam Input" :
			"in Steam, OpenH1's Properties > Controller > Disable Steam Input");
	else
		platform_log("gyro: controller %u (port %d): no gyroscope", (unsigned)device->id, port + 1);
}

/* the turn the test device's samples make up to the simulation's time:
the script's gyro= and tilt= at each thousandth of a second, as a real
controller's would come (gyro_lock held) */
#define GYRO_TEST_RATE 1000
static void gyro_test_feed(void)
{
	static double fed_to = -1.0;
	double t = sim_trace_scenario_seconds() + 1e-6;
	int index;

	if (t < 0.0)
		return;
	if (fed_to < 0.0)
		fed_to = t;
	for (index = 0; index < test_script_count; index++)
	{
		struct test_script_event *event = &test_script[index];

		if (event->gyro_calibrate && !event->aimed && t >= event->start)
		{
			event->aimed = 1;
			gyro_aim_calibrate(&gyro_test_device.aim);
			platform_log("gyro: test: calibrating");
		}
	}
	while (fed_to + 1.0 / GYRO_TEST_RATE <= t)
	{
		float rate[3] = { 0.0f, 0.0f, 0.0f };
		float up[3] = { 0.0f, 1.0f, 0.0f };
		float length;

		fed_to += 1.0 / GYRO_TEST_RATE;
		for (index = 0; index < test_script_count; index++)
		{
			struct test_script_event *event = &test_script[index];

			if (fed_to < event->start || fed_to >= event->end)
				continue;
			if (event->gyro)
			{
				rate[0] += event->x * 0.0174532925f;
				rate[1] += event->y * 0.0174532925f;
				rate[2] += event->z * 0.0174532925f;
			}
			else if (event->tilt)
			{
				up[0] = event->x;
				up[1] = event->y;
				up[2] = event->z;
			}
		}
		length = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
		if (length > 0.0f)
		{
			up[0] *= 9.80665f / length;
			up[1] *= 9.80665f / length;
			up[2] *= 9.80665f / length;
			gyro_aim_accelerometer(&gyro_test_device.aim, up);
		}
		gyro_device_sample(&gyro_test_device, rate, 1.0f / GYRO_TEST_RATE);
	}
}

/* the script holds the gyroscope button (touch: has the thumb on the right
stick) */
static BOOL gyro_test_button(BOOL touch)
{
	double t = sim_trace_scenario_seconds() + 1e-6;
	int index;

	for (index = 0; index < test_script_count; index++)
	{
		if ((touch ? test_script[index].stick_touch : test_script[index].gyro_button) &&
			t >= test_script[index].start && t < test_script[index].end)
			return TRUE;
	}
	return FALSE;
}

/* a port's controller read (XInputGetState): its gyroscope's sensors on or
off as wanted, whether its turn counts, and its turn dropped when nobody
takes it */
static void gyro_port_update(int port, SDL_Gamepad *gamepad, SDL_JoystickID id)
{
	struct gyro_device *device = NULL;
	BOOL held = FALSE, want;
	/* (the right stick touched; -1, the controller cannot tell) */
	int touched = -1;

	gyro_settings_read();
	pthread_mutex_lock(&gyro_lock);
	if (port == 0 && test_script_gyro)
	{
		if (!gyro_test_device.looked)
		{
			gyro_test_device.looked = gyro_test_device.has_gyro = gyro_test_device.has_accelerometer = TRUE;
			gyro_test_device.stick_touch = TRUE;
			gyro_test_device.enabled = TRUE;
			platform_log("gyro: test: the script's gyroscope on port 1");
		}
		device = &gyro_test_device;
		held = gyro_test_button(FALSE);
		touched = gyro_test_button(TRUE);
	}
	else if (gamepad)
	{
		device = gyro_device_of(id, TRUE);
		if (!device->looked)
			gyro_device_look(device, gamepad, port);
		held = gyro_button_held(device, gamepad, gyro_settings.button);
		if (device->stick_touch)
			touched = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC4);
		else if (gyro_settings.mode == GYRO_AIM_TOUCH && device->has_gyro && !device->touch_told)
		{
			device->touch_told = TRUE;
			platform_log("gyro: controller %u (port %d): Thumb on Stick, but it does not say when its right stick "
				"is touched (a Steam Deck's own controls and the Steam Controller do): its gyroscope turns nothing",
				(unsigned)id, port + 1);
		}
		want = device->has_gyro && (gyro_settings.mode != GYRO_AIM_OFF || device->aim.calibrating);
		if (want != device->enabled)
		{
			SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, want);
			if (device->has_accelerometer)
				SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, want);
			/* (the bias measured is kept; the rest starts again) */
			if (want)
			{
				device->sampled_ns = 0;
				device->aim.up_known = device->aim.accelerometer_known = device->aim.means_known = 0;
				device->aim.still_seconds = 0.0f;
				device->aim.smoothed[0] = device->aim.smoothed[1] = 0.0f;
			}
			device->enabled = want;
			platform_log("gyro: controller %u (port %d): sensors %s", (unsigned)id, port + 1, want ? "on" : "off");
		}
	}
	if (device)
	{
		device->active = gyro_aim_active(gyro_settings.mode, held, touched);
		if (device == &gyro_test_device)
			gyro_test_feed();
	}
	if (gyro_ports[port] != device || (!gyro_taken[port] && ++gyro_polls_unconsumed[port] > 4))
	{
		if (gyro_ports[port])
			gyro_ports[port]->pending[0] = gyro_ports[port]->pending[1] = 0.0f;
		if (device)
			device->pending[0] = device->pending[1] = 0.0f;
	}
	gyro_ports[port] = device;
	gyro_taken[port] = FALSE;
	pthread_mutex_unlock(&gyro_lock);
}

/* the gyroscope's turn since the last call for the player on a port, in
radians (positive left and up, as the mouse's), before the zoom's scale
(player_control.c gives it the mouse's, view_fov.c's mouse_zoom_look_scale);
FALSE for none. The aim assist's magnetism does not act on it */
int halo_linux_gyro_look(short gamepad_index, float *yaw, float *pitch)
{
	struct gyro_device *device;

	*yaw = 0.0f;
	*pitch = 0.0f;
	if (gamepad_index < 0 || gamepad_index >= PORT_COUNT)
		return FALSE;
	pthread_mutex_lock(&gyro_lock);
	device = gyro_ports[gamepad_index];
	gyro_taken[gamepad_index] = TRUE;
	gyro_polls_unconsumed[gamepad_index] = 0;
	if (device)
	{
		*yaw = device->pending[0];
		*pitch = device->pending[1];
		device->pending[0] = device->pending[1] = 0.0f;
	}
	pthread_mutex_unlock(&gyro_lock);
	/* (none while the chat's on-screen keyboard is up for the player: what
	was turned is dropped) */
	if (network_chat_keyboard_open() && network_chat_controller() == gamepad_index)
	{
		*yaw = 0.0f;
		*pitch = 0.0f;
	}
	return *yaw != 0.0f || *pitch != 0.0f;
}

/* The drawn view's own reading of the gyroscope (player_control.c's
player_control_gyro_look_late, display.direct_camera), late in the frame:
the turn come since the frame's input took it (halo_linux_gyro_look), the
window's events taken now. Only the view drawn turns by it: it stays the
next frame's input, so the game is as it was. FALSE when the frame's input
took none (a menu) or nothing has come. */
int halo_linux_gyro_look_late(short gamepad_index, float *yaw, float *pitch)
{
	struct gyro_device *device;
	BOOL taken;

	*yaw = 0.0f;
	*pitch = 0.0f;
	if (gamepad_index < 0 || gamepad_index >= PORT_COUNT)
		return FALSE;
	pthread_mutex_lock(&gyro_lock);
	taken = gyro_taken[gamepad_index] && gyro_ports[gamepad_index];
	pthread_mutex_unlock(&gyro_lock);
	if (!taken || (network_chat_keyboard_open() && network_chat_controller() == gamepad_index))
		return FALSE;
	platform_pump_events();
	pthread_mutex_lock(&gyro_lock);
	device = gyro_ports[gamepad_index];
	if (device && gyro_taken[gamepad_index])
	{
		*yaw = device->pending[0];
		*pitch = device->pending[1];
	}
	pthread_mutex_unlock(&gyro_lock);
	return *yaw != 0.0f || *pitch != 0.0f;
}

/* for Settings' Gyro & Flick Stick page (menu_functions.c): 2 while a
connected controller has a gyroscope, 1 while the only ones are Steam
Input's virtual controllers (which pass none), else 0 */
int halo_gyro_found(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count = sdl_gamepads(gamepads), index, found = 0;

	if (test_script_gyro)
		return 2;
	for (index = 0; index < count; index++)
	{
		if (SDL_GamepadHasSensor(gamepads[index], SDL_SENSOR_GYRO))
			return 2;
		if (SDL_GetGamepadSteamHandle(gamepads[index]))
			found = 1;
	}
	return found;
}

/* ... its CALIBRATE: every connected gyroscope measured at rest (its
sensors on meanwhile: gyro_port_update); and whether one is still being
measured */
void halo_gyro_calibrate(void)
{
	int index;

	pthread_mutex_lock(&gyro_lock);
	for (index = 0; index < GYRO_DEVICES; index++)
	{
		if (gyro_devices[index].id && gyro_devices[index].has_gyro)
			gyro_aim_calibrate(&gyro_devices[index].aim);
	}
	if (test_script_gyro)
		gyro_aim_calibrate(&gyro_test_device.aim);
	pthread_mutex_unlock(&gyro_lock);
	platform_log("gyro: calibrating");
}

int halo_gyro_calibrating(void)
{
	int index, calibrating = FALSE;

	pthread_mutex_lock(&gyro_lock);
	for (index = 0; index < GYRO_DEVICES; index++)
	{
		if (gyro_devices[index].id && gyro_devices[index].aim.calibrating)
			calibrating = TRUE;
	}
	if (gyro_test_device.aim.calibrating)
		calibrating = TRUE;
	pthread_mutex_unlock(&gyro_lock);
	return calibrating;
}

/* the flick stick of each port: its state, the turn since the game took
it, when it was read last (seconds), and its readings untaken */
static struct
{
	struct flick_stick stick;
	float pending;
	double read_at;
	BOOL taken;
	unsigned long polls_unconsumed;
} flick_ports[PORT_COUNT];

/* the clock the flick stick turns by: the simulation's while a script
plays, so that it plays alike at any speed */
static double flick_clock(void)
{
	return test_script_on ? sim_trace_scenario_seconds() : (double)SDL_GetTicksNS() * 1e-9;
}

/* a port's reading's right stick, as the flick stick */
static void flick_port_update(int port, const XINPUT_GAMEPAD *pad)
{
	double now = flick_clock();
	double seconds = flick_ports[port].read_at > 0.0 ? now - flick_ports[port].read_at : 0.0;

	flick_ports[port].read_at = now;
	if (!gyro_settings.flick_stick || (!flick_ports[port].taken && ++flick_ports[port].polls_unconsumed > 4))
	{
		flick_stick_reset(&flick_ports[port].stick);
		flick_ports[port].pending = 0.0f;
		if (!gyro_settings.flick_stick)
			return;
	}
	flick_ports[port].taken = FALSE;
	if (seconds < 0.0 || seconds > 0.1)
		seconds = 0.0;
	flick_ports[port].pending += flick_stick_update(&flick_ports[port].stick, pad->sThumbRX / 32767.0f,
		pad->sThumbRY / 32767.0f, (float)seconds);
}

/* input.flick_stick (input_abstraction.c: with the default stick layout,
the right stick's own look none) */
int halo_linux_flick_stick(void)
{
	gyro_settings_read();
	return gyro_settings.flick_stick;
}

/* the flick stick's turn since the last call for the player on a port, in
radians of yaw (positive left); FALSE for none */
int halo_linux_flick_look(short gamepad_index, float *yaw)
{
	*yaw = 0.0f;
	if (gamepad_index < 0 || gamepad_index >= PORT_COUNT)
		return FALSE;
	*yaw = flick_ports[gamepad_index].pending;
	flick_ports[gamepad_index].pending = 0.0f;
	flick_ports[gamepad_index].taken = TRUE;
	flick_ports[gamepad_index].polls_unconsumed = 0;
	if (network_chat_keyboard_open() && network_chat_controller() == gamepad_index)
		*yaw = 0.0f;
	return *yaw != 0.0f;
}

/* ---------- XAPI */

VOID WINAPI XInitDevices(DWORD preallocation_type_count, PXDEVICE_PREALLOC_TYPE preallocation_types)
{
	(void)preallocation_type_count;
	(void)preallocation_types;
	platform_sdl_initialize();
}

static DWORD connected_gamepads(void)
{
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count = sdl_gamepads(gamepads);
	DWORD mask = XDEVICE_PORT0_MASK;
	int port;

#ifdef HALO_VR
	/* (in the headset, all of them: XInputGetState) */
	if (halo_vr_running())
		return mask;
#endif
	/* the first pad shares port 0 with the keyboard (port_gamepad) */
	for (port = 0; port < PORT_COUNT; port++)
		controller_device_update((short)port, port_gamepad(gamepads, count, port));
	for (port = 1; port < PORT_COUNT; port++)
	{
		/* (and the controllers the test script plays: p<n>) */
		if (port_gamepad(gamepads, count, port) || ((test_script_ports | test_input_menu_ports) & (1U << port)))
			mask |= 1UL << port;
	}
	return mask;
}

BOOL WINAPI XGetDeviceChanges(PXPP_DEVICE_TYPE device_type, PDWORD insertions, PDWORD removals)
{
	*insertions = 0;
	*removals = 0;
	if (device_type == XDEVICE_TYPE_GAMEPAD)
	{
		DWORD connected = connected_gamepads();

		*insertions = connected & ~reported_gamepads;
		*removals = reported_gamepads & ~connected;
		reported_gamepads = connected;
	}
	else if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD)
	{
		if (!reported_keyboard)
		{
			*insertions = 1;
			reported_keyboard = TRUE;
		}
	}
	return *insertions || *removals;
}

HANDLE WINAPI XInputOpen(PXPP_DEVICE_TYPE device_type, DWORD port, DWORD slot,
	PXINPUT_POLLING_PARAMETERS polling_parameters)
{
	(void)slot;
	(void)polling_parameters;
	if (device_type == XDEVICE_TYPE_GAMEPAD && port < PORT_COUNT)
	{
		memset(&controllers[port], 0, sizeof(controllers[port]));
		controllers[port].open = TRUE;
		return (HANDLE)&controllers[port];
	}
	if (device_type == XDEVICE_TYPE_DEBUG_KEYBOARD && port == 0)
	{
		keyboard_device.open = TRUE;
		return (HANDLE)&keyboard_device;
	}
	SetLastError(ERROR_DEVICE_NOT_CONNECTED);
	return NULL;
}

VOID WINAPI XInputClose(HANDLE device)
{
	struct controller *controller = (struct controller *)device;

	if (controller)
		controller->open = FALSE;
}

static int controller_port(HANDLE device)
{
	int port;

	for (port = 0; port < PORT_COUNT; port++)
	{
		if (device == (HANDLE)&controllers[port] && controllers[port].open)
			return port;
	}
	return -1;
}

/* what OpenH1's overlays read of a port's pad as it is, whatever its button
layout (input.controller_p1 to p4: controller_remap_reserve): the scoreboard's
D-pad and X while it offers its player menu, all of it while it holds the
controls (scoreboard_pad_port_hold's, not counted as a reading); a vote's
D-pad left and right, a Discord ask's up and down (the first controller's) */
static unsigned int remap_reserved(int port)
{
	struct scoreboard_pad *pad = &scoreboard_pads[port];
	struct platform_input_state input;
	unsigned int reserved = 0;
	int hold;

	platform_input_read(&input, FALSE);
	pthread_mutex_lock(&scoreboard_pad_lock);
	hold = input.menus || pad->readings > SCOREBOARD_PAD_READINGS ? HALO_SCOREBOARD_PAD_FREE : pad->hold;
	pthread_mutex_unlock(&scoreboard_pad_lock);
	if (hold == HALO_SCOREBOARD_PAD_HOLDS)
		return CONTROLLER_REMAP_RESERVE_ALL;
	if (hold == HALO_SCOREBOARD_PAD_OFFERED)
		reserved |= CONTROLLER_REMAP_RESERVE_X | CONTROLLER_REMAP_RESERVE_DPAD_UP | CONTROLLER_REMAP_RESERVE_DPAD_DOWN |
			CONTROLLER_REMAP_RESERVE_DPAD_LEFT | CONTROLLER_REMAP_RESERVE_DPAD_RIGHT;
	if (port == 0 && network_vote_pad_open())
		reserved |= CONTROLLER_REMAP_RESERVE_DPAD_LEFT | CONTROLLER_REMAP_RESERVE_DPAD_RIGHT;
	if (port == 0 && discord_ask_serial())
		reserved |= CONTROLLER_REMAP_RESERVE_DPAD_UP | CONTROLLER_REMAP_RESERVE_DPAD_DOWN;
	return reserved;
}

/* debug camera (director.c): the middle button, R and F, and the wheel's
notches since the director last asked */
static BOOL debug_cam_middle = FALSE;
static BOOL debug_cam_up = FALSE;
static BOOL debug_cam_down = FALSE;
static float debug_cam_wheel = 0.0f;

static void debug_camera_update(const struct platform_input_state *input, BOOL blocked)
{
	pthread_mutex_lock(&mouse_lock);
	debug_cam_middle = !blocked && !input->mouse_released &&
		input->mouse_buttons[SDL_BUTTON_MIDDLE] != 0;
	debug_cam_up = !blocked && input->keys[SDL_SCANCODE_R] != 0;
	debug_cam_down = !blocked && input->keys[SDL_SCANCODE_F] != 0;
	if (debug_cam_middle)
		debug_cam_wheel += input->mouse_wheel;
	else
		debug_cam_wheel = 0.0f;
	pthread_mutex_unlock(&mouse_lock);
}

/* read by director.c (halo_linux_source_fixups.h); the wheel's notches are
returned once, then cleared */
int halo_linux_debug_camera_input(int *middle, int *up, int *down, float *wheel)
{
	pthread_mutex_lock(&mouse_lock);
	*middle = debug_cam_middle;
	*up = debug_cam_up;
	*down = debug_cam_down;
	*wheel = debug_cam_wheel;
	debug_cam_wheel = 0.0f;
	pthread_mutex_unlock(&mouse_lock);
	return TRUE;
}

/* a split screen player's controller (a port past the first): physical, a
connected one's reading, or with none the test script's (p<n>), read the
same way, its presses the controller's own; and what reads it on the way,
as the first's: the scoreboard, the chat's on-screen keyboard, the D-pad's
pings and Spartan link */
static void split_screen_pad_read(int port, XINPUT_GAMEPAD *pad, const XINPUT_GAMEPAD *physical,
	unsigned int reserved)
{
	struct platform_input_state input;

	if (physical)
		*pad = *physical;
	else
	{
		test_script_port_gamepad(port, pad);
		chat_physical_pad[port] = chat_pad_bits(&test_script_pressed[port]);
	}
	/* (a split screen player's scoreboard, as port 0's) */
	platform_input_read(&input, FALSE);
	scoreboard_pad_read(port, pad, 0, scoreboard_pad_port_hold(port, input.menus));
	/* (the chat's on-screen keyboard, when this player opened it) */
	chat_pad_record((short)port, pad);
	chat_pad_hold_over((short)port, pad);
	ping_pad_read(port, pad, reserved);
	spartan_link_pad_read(port, pad, reserved);
}

/* reads a controller's state; for port 0 the keyboard, mouse, debug input
and touchscreen are merged into the first gamepad's; runs on the game's main
thread (touch_input_gamepad relies on it); returns ERROR_SUCCESS or
ERROR_DEVICE_NOT_CONNECTED for an unknown port */
DWORD WINAPI XInputGetState(HANDLE device, PXINPUT_STATE state)
{
	int port = controller_port(device);
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count;
#ifdef HALO_VR
	int back = 0;
#endif
	XINPUT_GAMEPAD physical;
	struct gamepad_taps taps;
	int captured;
	int connected;
	unsigned int reserved;

	memset(state, 0, sizeof(*state));
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	platform_pump_events();
	split_players = pc_menu_split_players() != 0;
	count = sdl_gamepads(gamepads);
	controller_device_update((short)port, port_gamepad(gamepads, count, port));
	/* Keep physical remapping separate from the keyboard and touch controls. */
	memset(&physical, 0, sizeof(physical));
	/* (what was pressed and let go of since the last reading counts once,
	through the remapping as a held button does) */
	gamepad_taps_take(gamepads, count, port_gamepad(gamepads, count, port), &taps);
	if (port_gamepad(gamepads, count, port))
		sdl_gamepad_state(port_gamepad(gamepads, count, port), &taps, &physical);
	/* Keep the opening chat press so even B can open it without immediately closing it. */
	chat_pad_record((short)port, &physical);
	chat_physical_pad[port] = chat_pad[port];
	reserved = remap_reserved(port);
	controller_remap_reserve((short)port, reserved);
	controller_remap_paddles((short)port, sdl_gamepad_paddles(port_gamepad(gamepads, count, port), &taps));
	captured = controller_remap_input((short)port, &physical, port_gamepad(gamepads, count, port) != NULL);
	/* (a split screen player's controller: in the headset every gamepad is
	the first player's) */
#ifdef HALO_VR
	connected = !halo_vr_running() && port_gamepad(gamepads, count, port) != NULL;
#else
	connected = port_gamepad(gamepads, count, port) != NULL;
#endif
	if (port == 0)
	{
		struct platform_input_state input;
		unsigned long held;
		unsigned long scoreboard_keys = 0;
		int scoreboard_hold;
		BOOL console_active;
		unsigned long discord_ask;
		short aiming_deflection;

		platform_input_read(&input, TRUE);
		mouse_poll(&input);
		wheel_update();
		pthread_mutex_lock(&mouse_lock);
		mouse_look_taken = FALSE;
		pthread_mutex_unlock(&mouse_lock);
		keyboard_actions_held = 0;
		scoreboard_hold = scoreboard_pad_port_hold(0, input.menus);
		keys_held_over_switch(&input, scoreboard_hold == HALO_SCOREBOARD_PAD_HOLDS);
		/* (a Discord friend's ask answered first, its keys taken) */
		discord_ask = discord_ask_serial();
		discord_ask_keys(&input, discord_ask);
		/* (a chat line typed takes the keyboard as the console does) */
		console_active = console_is_active() || network_chat_typing();
		debug_camera_update(&input, console_active);
		held = console_active ? 0 : keyboard_bound_actions(&input);
		keyboard_screenshot(held);
		if (!console_active)
		{
			if (input.menus)
				keyboard_gamepad(&input, &state->Gamepad);
			else
			{
				/* (the scoreboard's keys, which it takes from the game while
				it offers its player menu: the arrows and Enter; all while it
				holds the controls) */
				scoreboard_keys = scoreboard_pad_keys(input.keys, held);
				if (scoreboard_hold == HALO_SCOREBOARD_PAD_HOLDS)
					held = 0;
				else if (scoreboard_hold == HALO_SCOREBOARD_PAD_OFFERED)
				{
					input.keys[SDL_SCANCODE_UP] = input.keys[SDL_SCANCODE_DOWN] = 0;
					input.keys[SDL_SCANCODE_LEFT] = input.keys[SDL_SCANCODE_RIGHT] = 0;
					input.keys[SDL_SCANCODE_RETURN] = input.keys[SDL_SCANCODE_KP_ENTER] = 0;
					held = keyboard_bound_actions(&input);
				}
				held |= test_script_keys();
				keyboard_controls(held, &state->Gamepad);
			}
		}
		if (port_gamepad(gamepads, count, 0))
			controller_remap_merge(&physical, &state->Gamepad);
		discord_ask_pad(port_gamepad(gamepads, count, 0), discord_ask, &state->Gamepad);
#ifdef HALO_VR
		if (halo_vr_running())
		{
			/* in the headset every gamepad is the first player's: Steam
			may list gamepads of its own (Steam Input's) before the one in
			the hands */
			int other;

			for (other = 0; other < count; other++)
			{
				if (other > 0 || !port_gamepad(gamepads, count, 0))
				{
					struct gamepad_taps other_taps;

					gamepad_taps_take(gamepads, count, gamepads[other], &other_taps);
					sdl_gamepad_state(gamepads[other], &other_taps, &state->Gamepad);
				}
				back = back || SDL_GetGamepadButton(gamepads[other], SDL_GAMEPAD_BUTTON_BACK);
			}
		}
		halo_vr_gamepad(&state->Gamepad);
#endif
		substep_frame_devices = state->Gamepad;
		memset(&test_script_pressed[0], 0, sizeof(test_script_pressed[0]));
		test_input_gamepad(&state->Gamepad);
		/* (the script's presses on the first controller are a controller's
		own, as a connected one's: its D-pad's pings and Spartan link) */
		chat_physical_pad[0] |= chat_pad_bits(&test_script_pressed[0]);
		substep_frame_reading = state->Gamepad;
		touch_input_gamepad(&state->Gamepad);
		/* (debug.latency_log: when the input was read) */
		latency_log_read(state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER], state->Gamepad.sThumbLX,
			state->Gamepad.sThumbLY);
		latency_log_mouse_read(!input.menus && (held & (1UL << HALO_KEYBOARD_FIRE)) != 0,
			input.mouse_released ? 0.0f : input.mouse_dx, input.mouse_released ? 0.0f : input.mouse_dy);
		latency_log_menu_read((state->Gamepad.wButtons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
			XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT)) != 0 ||
			state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A] >= 128 || abs(state->Gamepad.sThumbLX) >= 29490 ||
			abs(state->Gamepad.sThumbLY) >= 29490, input.menus);
#ifdef HALO_TOUCH_CONTROLS
		touch_input_controls(&state->Gamepad, input.menus);
#endif
		/* (the HUD's prompts follow the device played with last) */
		prompt_device_update(held, port_gamepad(gamepads, count, 0), count,
			memcmp(&state->Gamepad, &substep_frame_reading, sizeof(state->Gamepad)) != 0);
#ifdef HALO_VR
		/* a gamepad's Back held recentres (not the keyboard's tab, the
		scores) */
		halo_vr_gamepad_back(back || test_input_back);
#endif
		/* (the scoreboard's player menu reads them, and takes what it
		uses: scoreboard_pad_read) */
		scoreboard_pad_read(0, &state->Gamepad, scoreboard_keys, scoreboard_hold);
		/* (the text chat's on-screen keyboard reads them, and takes them
		while it is up) */
		chat_pad_record(0, &state->Gamepad);
		chat_pad_hold_over(0, &state->Gamepad);
		/* (and a vote running takes the D-pad's left and right:
		network_votekick.c) */
		vote_pad_hold_over(&state->Gamepad);
		/* (and pings, the stock layouts' D-pad Right: network_markers.c) */
		ping_pad_read(0, &state->Gamepad, reserved);
		/* (and the Spartan link, the stock layouts' D-pad Down:
		spartan_link.c) */
		spartan_link_pad_read(0, &state->Gamepad, reserved);
		/* (the input trace: what the first controller reads in all) */
		input_trace_pad(0, state->Gamepad.wButtons, state->Gamepad.sThumbLX, state->Gamepad.sThumbLY,
			state->Gamepad.sThumbRX, state->Gamepad.sThumbRY,
			state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER],
			state->Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER], keyboard_actions_held,
			input.mouse_released ? 0.0f : input.mouse_dx, input.mouse_released ? 0.0f : input.mouse_dy);
		/* (a right stick dead zone set below it aims from that) */
		aiming_deflection = halo_stick_dead_range(0, 1);
		if (aiming_deflection > STICK_AIMING_DEFLECTION)
			aiming_deflection = STICK_AIMING_DEFLECTION;
		if (abs(state->Gamepad.sThumbRX) > aiming_deflection ||
			abs(state->Gamepad.sThumbRY) > aiming_deflection)
		{
			pthread_mutex_lock(&mouse_lock);
			stick_aimed_ms = SDL_GetTicks();
			pthread_mutex_unlock(&mouse_lock);
		}
	}
	else if (connected || (test_script_ports & (1U << port)))
		split_screen_pad_read(port, &state->Gamepad, connected ? &physical : NULL, reserved);
	/* (or the menu script: p<n>:) */
	else if (test_input_menu_ports & (1U << port))
		test_input_menu_gamepad(&state->Gamepad, port);
	if (captured) memset(&state->Gamepad, 0, sizeof(state->Gamepad));
	/* (the port's gyroscope, and its right stick as the flick stick) */
	{
		SDL_Gamepad *gamepad = port_gamepad(gamepads, count, port);
		SDL_JoystickID id = 0;
		int index;

		for (index = 0; index < count; index++)
		{
			if (gamepads[index] == gamepad)
				id = sdl_gamepad_ids[index];
		}
		gyro_port_update(port, gamepad, id);
		flick_port_update(port, &state->Gamepad);
	}

	if (memcmp(&state->Gamepad, &controllers[port].previous, sizeof(state->Gamepad)))
	{
		controllers[port].packet_number++;
		controllers[port].previous = state->Gamepad;
	}
	state->dwPacketNumber = controllers[port].packet_number;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputSetState(HANDLE device, PXINPUT_FEEDBACK feedback)
{
	int port = controller_port(device);
	SDL_Gamepad *gamepads[PORT_COUNT];
	int count;

	if (!feedback)
		return ERROR_INVALID_PARAMETER;
	feedback->Header.dwStatus = ERROR_SUCCESS;
	if (port < 0)
		return ERROR_DEVICE_NOT_CONNECTED;
	/* (debug.latency_log: the impulse's step to here) */
	if (port == 0)
		latency_log_rumble_written(feedback->Rumble.wLeftMotorSpeed, feedback->Rumble.wRightMotorSpeed);
#ifdef HALO_TOUCH_CONTROLS
	/* the phone vibrates for the touch controls' player */
	if (port == 0)
		touch_input_rumble(feedback->Rumble.wLeftMotorSpeed, feedback->Rumble.wRightMotorSpeed);
#endif
#ifdef HALO_VR
	if (port == 0)
		halo_vr_rumble(feedback->Rumble.wLeftMotorSpeed / 65535.0f, feedback->Rumble.wRightMotorSpeed / 65535.0f);
#endif
	count = sdl_gamepads(gamepads);
#ifdef HALO_VR
	/* (in the headset, each gamepad is port 0's) */
	if (halo_vr_running())
	{
		int index;

		for (index = 0; port == 0 && index < count; index++)
			SDL_RumbleGamepad(gamepads[index], feedback->Rumble.wLeftMotorSpeed,
				feedback->Rumble.wRightMotorSpeed, 100);
		return ERROR_SUCCESS;
	}
#endif
	if (port_gamepad(gamepads, count, port))
	{
		/* the game refreshes the motors every frame; rumble a little longer
		than that so they do not stutter */
		SDL_RumbleGamepad(port_gamepad(gamepads, count, port), feedback->Rumble.wLeftMotorSpeed,
			feedback->Rumble.wRightMotorSpeed, 100);
	}
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugInitKeyboardQueue(PXINPUT_DEBUG_KEYQUEUE_PARAMETERS parameters)
{
	(void)parameters;
	return ERROR_SUCCESS;
}

DWORD WINAPI XInputDebugGetKeystroke(PXINPUT_DEBUG_KEYSTROKE keystroke)
{
	struct platform_keystroke next;

	memset(keystroke, 0, sizeof(*keystroke));
	while (platform_next_keystroke(&next))
	{
		BOOL key_up = (next.flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP) != 0;

		/* key ups always pass, so no key is left latched down; while typing,
		escape does not (it cancels, as B: the game's own escape leaves the
		menus, main.c) */
		/* (a menu's text field's Enter, while the Enter that began the
		field is still held: not its end) */
		if (!key_up && next.virtual_key == 0x0D /* return */ && text_typing_field && !text_typing_enter_armed &&
			!console_is_active() && !network_chat_typing())
		{
			continue;
		}
		/* (and on a Settings screen of the menu engine's, whose letters start
		its search: halo_keyboard_settings_keys) */
		if (key_up || next.virtual_key == VK_OEM_3_BACKQUOTE || console_is_active() || network_chat_typing() ||
			((text_typing || halo_keyboard_settings_keys_active()) && next.virtual_key != 0x1B /* escape */))
		{
			keystroke->VirtualKey = next.virtual_key;
			keystroke->Ascii = next.ascii;
			keystroke->Flags = next.flags;
			return ERROR_SUCCESS;
		}
	}
	return ERROR_HANDLE_EOF;
}
