/*
SDL_PLATFORM.H

Window and input state shared by the renderer and the
controller emulation (see sdl_platform.c).
*/

#ifndef __HALO_LINUX_SDL_PLATFORM_H
#define __HALO_LINUX_SDL_PLATFORM_H

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_audio.h>
#include <stddef.h>

#define PLATFORM_MOUSE_BUTTON_COUNT 8

struct platform_input_state
{
	unsigned char keys[SDL_SCANCODE_COUNT];
	unsigned char mouse_buttons[PLATFORM_MOUSE_BUTTON_COUNT]; /* SDL_BUTTON_* */
	float mouse_dx, mouse_dy;
	float mouse_wheel;
	BOOL focused;
	BOOL mouse_released;
	/* the mouse drives the menus' pointer (platform_ui_pointer_set_active)
	instead of the controller */
	BOOL ui_pointer;
	/* a menu is up (platform_menus_set_active): the keys drive the first
	controller, to move about it, instead of the player's actions */
	BOOL menus;
	/* the mouse's Back button (platform_mouse_button_backs) is held in the
	menus, or was pressed there since the controller's last reading: their
	B, as Escape is (xinput_sdl.c's keyboard_gamepad); a binding of it is
	the game's alone, never the menus' */
	BOOL menu_back;
};

/* whether a mouse button (SDL's numbering) is the one a web browser goes
back with: SDL's X1 (Windows' XBUTTON1, macOS's fourth button, Linux's
BTN_SIDE), or a Linux mouse's BTN_BACK, which SDL numbers 7 (Wayland's
SDL_BUTTON_X1 + BTN_BACK - BTN_SIDE; X11's button 11, less 4). Forward (X2,
BTN_EXTRA; BTN_FORWARD, 6) has no meaning in the menus. */
#define PLATFORM_MOUSE_BUTTON_LINUX_BACK 7
#define platform_mouse_button_backs(button) \
	((button) == SDL_BUTTON_X1 || (button) == PLATFORM_MOUSE_BUTTON_LINUX_BACK)

/* an input of the keyboard and mouse's controls (xinput_sdl.c): a scancode,
a mouse button (INPUT_MOUSE + SDL_BUTTON_*), or the wheel */
#define INPUT_MOUSE SDL_SCANCODE_COUNT
#define INPUT_WHEEL (INPUT_MOUSE + PLATFORM_MOUSE_BUTTON_COUNT)
#define INPUT_WHEEL_UP (INPUT_WHEEL + 1)
#define INPUT_WHEEL_DOWN (INPUT_WHEEL + 2)
/* an input by its name in config.toml ("W", "Mouse Left"), -1 if none */
int halo_input_from_name(const char *name);
void halo_input_name(int input, char *name, size_t size);
/* a binding's name ("Z") as the keyboard's layout labels its key ("Y"),
for the menus */
void halo_input_shown_name(const char *binding, char *shown, size_t size);

/* whether a key, a mouse button or a finger went down since it was last
asked: what skips a Bink movie besides a controller's buttons (also declared
for the game in halo_linux_source_fixups.h) */
int platform_movie_skip_pressed(void);
/* whether a menu is up (halo_ui_pointer_update, every frame) */
void platform_menus_set_active(BOOL active);
/* rebinding a control (Settings > Controls Setup): from now the next key,
mouse button or wheel turn is taken, and none reaches the game or the
menus; nor does the keyboard after, until every key and button is up */
void platform_binding_capture_begin(void);
/* Stop a capture, retaining its release guard for already held keys. */
void platform_binding_capture_cancel(void);
/* 0 while waiting; else the capture ends: 1 with the input, 2 for none
(Delete), 3 if cancelled (Escape) */
int platform_binding_capture_poll(int *input);

struct platform_keystroke
{
	BYTE virtual_key;
	CHAR ascii;
	BYTE flags;
};

BOOL platform_sdl_initialize(void);
/* creates the window, which the host's renderer draws */
BOOL platform_video_initialize(unsigned long width, unsigned long height);
/* Loading owns presentation until phase 2 hands the next game picture back.
Phase 0 waits until its presenter no longer touches the default framebuffer. */
void platform_loading_set(int phase);
void platform_loading_art(const unsigned char *rgba);
void platform_loading_scene(const void *data, unsigned long bytes);
void platform_loading_progress(float progress);
/* The loading screen's chant (dsound_sdl.c): playing (its four voices
swelling with progress, 0 to 1), or fading out (playing 0). */
void platform_loading_chant(int playing, float progress);
int platform_loading_active(void);
BOOL platform_screen_mode(long *width, long *height);
/* Video Setup's resolutions (port/linux/game/menu_tags.c), in pixels: the
display's modes the game can draw at, largest first, each once, but the
display's own (Native), and display.resolution's where it is none of them;
how many. None on Android. */
int platform_display_resolutions(long *widths, long *heights, int maximum);
/* whether fullscreen can set the display's refresh rate (display.refresh_rate):
Windows, and an X server's own display; not Wayland, XWayland, gamescope,
macOS, iOS or Android */
int platform_refresh_rates_settable(void);
/* Video Setup's refresh rates: those of the display's modes, whole, highest
first, each once, and display.refresh_rate's where it is none of them; how
many. None where they cannot be set (platform_refresh_rates_settable). */
int platform_display_refresh_rates(long *rates, int maximum);
/* Video Setup's window sizes: those of each shape (4:3, 16:10, 16:9, 21:9)
that fit the desktop, and display.window_size's; how many. None on
Android. */
int platform_window_sizes(long *widths, long *heights, int maximum);
/* Frame Rate & Sync's Variable Refresh (menu_tags.c, menu_functions.c):
PLATFORM_VRR_FOUND or _NONE where the system is asked (the Linux builds:
gamescope's word, a display's DRM), PLATFORM_VRR_UNKNOWN where nothing
reliable says (Windows), PLATFORM_VRR_SYSTEM where the system sets it (the
Mac: Metal and ProMotion), PLATFORM_VRR_NOT_HERE where it has no meaning
(Android) */
#define PLATFORM_VRR_NONE 0
#define PLATFORM_VRR_FOUND 1
#define PLATFORM_VRR_UNKNOWN 2
#define PLATFORM_VRR_SYSTEM 3
#define PLATFORM_VRR_NOT_HERE 4
int platform_vrr_detected(void);
/* Settings > Audio's devices (port/linux/game/menu_tags.c): the names of
the output (or, recording, input) devices SDL finds; how many. None on
Android, which plays and records with the system's. */
#define PLATFORM_AUDIO_DEVICE_NAME_SIZE 128
int platform_audio_devices(int recording, char (*names)[PLATFORM_AUDIO_DEVICE_NAME_SIZE], int maximum);
/* ... the device of a name (audio.output_device, audio.input_device):
the system's default for "default", or one not found */
SDL_AudioDeviceID platform_audio_device(int recording, const char *name);
void platform_video_drawable_size(int *width, int *height);
#ifndef HALO_ANDROID
/* whether the window's display shows HDR, with the desktop's white (in
scRGB's units, 1.0 being 80 nits) and how many times brighter than it the
display goes (1.0 without HDR) */
BOOL platform_video_hdr(float *sdr_white, float *headroom);
#endif
/* the window's mode and size and V-Sync, from config.toml as Settings has
just written it (the main thread's) */
void platform_display_apply(void);
/* the frames a second battery saver asks for (battery_saver.c), given the
window's renderer: the main thread's, as it turns on or off */
void platform_frame_rate_apply(void);
#ifdef HALO_VR
/* the VR build's hidden window shown, to play flat in: no VR session */
void platform_video_vr_unavailable(void);
#endif
void platform_video_swap(void);
/* frames between the 30 Hz ticks at the display's refresh rate, unless
display.interpolation is false (port/linux/game/render_interpolation.c) */
int halo_interpolation_enabled(void);
void platform_mouse_capture(BOOL capture);

/* handles the window's and input devices' events; main thread only, a no-op
elsewhere; on Android this is where finger events reach touch_input.c, which
is why that module's state is only touched from the main thread */
void platform_pump_events(void);
/* request a capture from the bound Screenshot action */
void platform_screenshot_request(void);
/* consume the request; call on the render thread with a completed frame */
BOOL platform_screenshot_take_request(void);
/* a snapshot of the input state; consume_motion resets the mouse deltas */
void platform_input_read(struct platform_input_state *state, BOOL consume_motion);
/* F3 to F7 pressed since the last call, a bit each from F3's, and F9 the
seventh bit (the menu studio's keys: port/linux/game/menu_studio.c) */
int platform_menu_studio_keys(void);
/* the menu studio's note being typed or not: while it is, the keyboard's
keys go to it alone; what was typed since the last call (characters, \b,
\n for Enter and \033 for Escape), its count */
void platform_menu_studio_typing(BOOL typing);
int platform_menu_studio_typed(char *typed, int size);
/* a gamepad's buttons pressed (bits by SDL_GamepadButton) and triggers pulled
(bit 0 the left, 1 the right) since this was last asked of it, by its
joystick ID: the controller's reading ORs them into what is held, so a tap
between two readings still counts */
/* a trigger past this (of SDL's 0 to 32767) is pulled */
#define PLATFORM_GAMEPAD_TRIGGER_PRESSED 8000
void platform_gamepad_presses_take(Uint32 id, Uint32 *buttons, unsigned int *triggers);
/* the pointer in the menus (d3d8_device.c, halo_ui_pointer_update) */
struct platform_ui_pointer
{
	/* in window coordinates, as SDL reports them (on Android the window's
	pixels: touch_input.c) */
	float x, y;
	float click_x, click_y;
	BOOL moved;
	int left_clicks, right_clicks;
	int wheel_steps;
	/* fingers down since the last read, and where the latest went down
	(the touchscreen only; the debug view of the menus' targets shows it) */
	int downs;
	float down_x, down_y;
	/* the pointer is the touchscreen (touch_input.c), not a mouse */
	BOOL touch;
	/* the mouse's left button is held (a slider dragged); never the
	touchscreen's, whose drags scroll */
	BOOL left_held;
	/* the touchscreen's: a finger is down and went sideways first, x and y
	where it is now (when no tap moved the pointer): a slider it went down
	on follows it */
	BOOL finger_held;
};
/* says whether a menu is up; the mouse is released for the menus (desktop),
or the touchscreen goes to them (Android); a change of mode drops the gesture
in progress */
void platform_ui_pointer_set_active(BOOL active);

/* pointer receives what the pointer did since the last call; returns
nonzero while a menu is up (platform_ui_pointer_set_active) */
BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer);
/* a scripted run's click on the menus, 0 to 1 of the window (debug.test_input
"menu:... click:<x>:<y> ...", xinput_sdl.c): 1, or 2 for a move without a
click ("point:<x>:<y>") */
int platform_test_menu_click(float *x, float *y);
/* its typing and Enter ("type:<letters and digits>", "enter", xinput_sdl.c) */
int platform_test_menu_keys(char *typed, int size, int *enter);
/* its mouse's ("mouse:<button>", "wheel:<notches>[*<events>]", xinput_sdl.c):
1 once in its second, with the button to click (SDL's numbering, 0 for
none), or the wheel's turn and in how many events it comes */
int platform_test_menu_mouse(int *button, float *wheel, int *wheel_events);
/* a script that starts with "ready": its seconds counted from now, the
menus' screen up (menu_tour.c's call); whether it waits for that still; the
second it is in, from 100 ms before it starts (-1: none, or waiting);
whether it is past its last press */
void platform_test_menu_start(void);
int platform_test_menu_waiting(void);
int platform_test_menu_step(void);
int platform_test_menu_done(void);

/* returns the window's size in the units that pointer positions come in,
which differ from the drawable's pixels on displays that scale */
void platform_video_window_size(int *width, int *height);
#ifndef HALO_ANDROID
/* the open scoreboard's pointer, offered (a network game's) or not: TRUE
while a right click has freed it, with what it did since the last call */
BOOL platform_scoreboard_pointer(BOOL offered, struct platform_ui_pointer *pointer);
#endif
/* a menu's text field typed into or not (a password's or not): the system's
on-screen keyboard up while it is, where there is one that text input shows
(Steam's, Android's, iOS's); each field begun brings it up again */
void platform_screen_keyboard(BOOL show, BOOL password);
BOOL platform_next_keystroke(struct platform_keystroke *keystroke);
/* the multiplayer scoreboard (game_engine.c) open or not: while it is, the
mouse wheel and Page Up/Down scroll it instead of switching weapons; how
far they moved it since the last call (notches and pages, down positive) */
void platform_scoreboard_scroll(int open, long *notches, long *pages);

#endif
