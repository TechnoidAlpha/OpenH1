/*
INPUT_TRACE.H

The input trace (input_trace.c): what the keyboard, mouse and gamepads sent
and what the game read of them, over the last INPUT_TRACE_SECONDS, for the
problem report (F8, report.c: input.txt) and the log.
*/

#ifndef __HALO_LINUX_INPUT_TRACE_H
#define __HALO_LINUX_INPUT_TRACE_H

#include <stddef.h>

#define INPUT_TRACE_SECONDS 30

/* the window's events traced (input_trace_window) */
enum
{
	INPUT_TRACE_FOCUS_GAINED = 1,
	INPUT_TRACE_FOCUS_LOST,
	INPUT_TRACE_SHOWN,
	INPUT_TRACE_HIDDEN,
	INPUT_TRACE_EXPOSED,
	INPUT_TRACE_OCCLUDED,
	INPUT_TRACE_MINIMIZED,
	INPUT_TRACE_RESTORED,
	INPUT_TRACE_FULLSCREEN,
	INPUT_TRACE_WINDOWED,
	INPUT_TRACE_MOUSE_ENTERED,
	INPUT_TRACE_MOUSE_LEFT,
	INPUT_TRACE_KEYBOARD_REMOVED,
	NUMBER_OF_INPUT_TRACE_WINDOW_EVENTS
};

/* who let go of a held key or button its events left held
(input_trace_release) */
enum
{
	INPUT_TRACE_RELEASED_BY_SDL = 1,
	INPUT_TRACE_RELEASED_BY_SYSTEM
};

/* a reading's flags (input_trace_reading) */
#define INPUT_TRACE_READING_MENUS 1
#define INPUT_TRACE_READING_FOCUSED 2
#define INPUT_TRACE_READING_POINTER 4
#define INPUT_TRACE_READING_RELEASED 8

/* the events, as the window's thread takes them (event_ns: the event's own
time, SDL's clock) */
void input_trace_key(unsigned long long event_ns, int scancode, int down, int repeat, int raw);
void input_trace_mouse_button(unsigned long long event_ns, int button, int down, int pointer);
void input_trace_gamepad_button(unsigned long long event_ns, unsigned int id, int button, int down);
/* (only as an axis crosses a quarter of its range, either way) */
void input_trace_gamepad_axis(unsigned long long event_ns, unsigned int id, int axis, int value);
void input_trace_gamepad_device(unsigned long long event_ns, unsigned int id, int added);
void input_trace_window(unsigned long long event_ns, int what);
/* a key (a scancode) or a mouse button (INPUT_MOUSE + its number) its
events left held, let go of because SDL, or the system, says it is up */
void input_trace_release(int input, int by);

/* the controller's reading of the keyboard and mouse (xinput_sdl.c's
XInputGetState), as the game takes it: the keys and buttons held, those
pressed since the last reading and counted in it (latched), the flags; then
what the first controller reads in all (its pad, the keyboard's actions
held, the mouse's motion); then a sub-step's reading of it, where it
differs from the frame's. Each written only where it differs from the last
(the motion where there is some). */
void input_trace_reading(const unsigned char *keys, int key_count, const unsigned char *buttons, int button_count,
	const unsigned char *latched_keys, const unsigned char *latched_buttons, int flags);
void input_trace_pad(int substep, unsigned int buttons, int left_x, int left_y, int right_x, int right_y,
	int left_trigger, int right_trigger, unsigned long actions, float mouse_dx, float mouse_dy);
/* the player's move and look as the game read them (input_abstraction.c),
for a frame or a sub-step */
void input_trace_player(int controller, int substep, float forward, float strafe, float yaw, float pitch);
/* the look stick's acceleration as the game took it a tick (player_control.c): the aim style, whether the turn
accelerates, and the look_acceleration_time it has reached */
void input_trace_aim(int controller, int modern, int accelerating, float acceleration_seconds);

/* the trace as text, of the last seconds (malloc()'s, with its size; NULL if
it could not be) */
char *input_trace_text(int seconds, size_t *size);
/* written to the log, headed by why */
void input_trace_log(const char *why, int seconds);

/* (what the trace asks of the rest: sdl_platform.c's clock, xinput_sdl.c's
names) */
unsigned long long input_trace_now_ns(void);
void halo_input_name(int input, char *name, size_t size);
const char *halo_keyboard_action_setting(int action);

#endif
