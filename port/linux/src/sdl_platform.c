/*
SDL_PLATFORM.C

The SDL3 window and event loop behind the ports.

The window is created with the Direct3D device (d3d8_device.c) on the game's
main thread, which is also the only thread that pumps events. Keyboard and
mouse state gathered here feeds the controller emulation in xinput_sdl.c
and the debug keyboard that the game's console reads.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "embedded_assets.h"
#include "battery_saver.h"
#include "frame_pacing.h"
#include "xgpu_backend.h"
#include "load_trace.h"
#include "loading_presentation.h"
#include "loading_art.h"
#include "loading_screen.h"
#include "text_hires.h"
#include "p2p.h"
#include "port_mods.h"
#include "halo_menus.h"
#include "server_mode.h"
#include "server_master.h"
#include "player_link.h"
#include "report.h"
#include "input_trace.h"
#include "xiso.h"
#include "map_unpack.h"
#include "map_import.h"
#ifdef HALO_VR
#include "vr.h"
#include "vr_host.h"
#endif
#include "touch_input.h"
#include "../game/text_filter.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zlib.h>
#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID)
#include <signal.h>
#endif

static SDL_Window *platform_window;
static SDL_ThreadID platform_event_thread;
static BOOL platform_sdl_started = FALSE;
/* the game's window was asked for (platform_video_initialize): without one
after that, the game runs without a window (platform_pump_events) */
static BOOL platform_video_tried = FALSE;
static int loading_presentation_active;

extern const unsigned int platform_loading_picture[];
extern const unsigned long platform_loading_picture_size;

void halo_loading_art(void)
{
	unsigned char *pixels;
	uLongf size = LOADING_ART_BYTES;

	if (!platform_loading_picture_size)
		return;
	pixels = malloc(LOADING_ART_BYTES);
	if (!pixels)
		return;
	if (uncompress(pixels, &size, (const unsigned char *)platform_loading_picture,
		platform_loading_picture_size) == Z_OK && size == LOADING_ART_BYTES)
	{
		xgpu_backend_loading_art(pixels);
		platform_log("loading: using the bundled loading picture");
	}
	else
		platform_log("loading: could not decode the bundled loading picture");
	free(pixels);
}

/* (port/linux/game/loading_scene.c) */
void halo_loading_scene(void);
void halo_loading_fade_begin(void);

/* (port/linux/game/menu_pictures_render.c) */
int menu_pictures_preparing(void);

void halo_loading_begin(void)
{
	/* the menus' pictures' maps: the loading screen of their whole pass
	stays (halo_loading_screen_begin), the old draws retired all the same */
	if (menu_pictures_preparing())
	{
		if (platform_window)
			xgpu_backend_consume();
		return;
	}
	if (!loading_presentation_active && platform_window)
	{
		/* the main menu's scene, read before its map is disposed of, for
		the presenter to draw while the next loads */
		halo_loading_scene();
		loading_presentation_active = 1;
		halo_load_trace("loading.request", "begin");
		xgpu_backend_loading_mode(0);
		xgpu_backend_loading(1);
		/* (the monks' chant under it, swelling with the load's progress) */
		platform_loading_chant(1, 0.0f);
	}
}

void halo_loading_progress(float progress)
{
	if (loading_presentation_active == 1)
	{
		xgpu_backend_loading_progress(progress);
		platform_loading_chant(1, progress);
	}
}

/* ---------- the loading screen for the port's own waits (loading_screen.h):
loading_presentation_active 3 while it is shown */

static char loading_screen_caption[256];

void halo_loading_screen_begin(int follow_progress)
{
	if (loading_presentation_active || !platform_window)
		return;
	halo_loading_scene();
	loading_presentation_active = 3;
	loading_screen_caption[0] = 0;
	halo_load_trace("loading.request", follow_progress ? "screen follow=1" : "screen follow=0");
	xgpu_backend_loading_mode(follow_progress ? 1 : 0);
	xgpu_backend_loading_caption(NULL, 0, 0);
	xgpu_backend_loading_progress(0.0f);
	xgpu_backend_loading(1);
	platform_loading_chant(1, 0.0f);
}

void halo_loading_screen_progress(float progress, char const *caption)
{
	int changed = 0;

	if (loading_presentation_active != 3)
		return;
	if (!(progress == progress))
		progress = 0.0f;
	progress = progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress;
	xgpu_backend_loading_progress(progress);
	platform_loading_chant(1, progress);
	if (!caption)
		caption = "";
	if (strncmp(caption, loading_screen_caption, sizeof(loading_screen_caption) - 1))
	{
		int width = 0, height = 0;
		char capitals[sizeof(loading_screen_caption)];
		unsigned char *coverage;
		size_t index;

		/* (lettered as the words over it are, "LOADING...": in capitals, in
		the menus' titles' face, OpenCE, the words' own (the menus' heading
		font, pc\heading), its capitals 15 of the 480 lines tall, theirs 22,
		set at LOADING_PRESENTATION_CAPTION_SCALE times their size: the
		presenter draws it at the screen's pixels, lit as the words are) */
		snprintf(capitals, sizeof(capitals), "%s", caption);
		for (index = 0; capitals[index]; index++)
			if (capitals[index] >= 'a' && capitals[index] <= 'z')
				capitals[index] = (char)(capitals[index] - 'a' + 'A');
		coverage = capitals[0] ? text_hires_line("pc\\heading", capitals,
			15.0f * LOADING_PRESENTATION_CAPTION_SCALE, &width, &height) : NULL;

		snprintf(loading_screen_caption, sizeof(loading_screen_caption), "%s", caption);
		xgpu_backend_loading_caption(coverage, (unsigned long)width, (unsigned long)height);
		free(coverage);
		/* (a new caption goes out at once) */
		changed = 1;
	}
	/* (sent now: the game may draw nothing meanwhile, whose frames would
	carry them; at most 30 times a second) */
	{
		static unsigned long long sent;
		unsigned long long now = SDL_GetTicksNS();

		if (changed || now - sent >= 33000000ULL)
		{
			sent = now;
			xgpu_backend_flush();
		}
	}
}

void halo_loading_screen_end(void)
{
	if (loading_presentation_active != 3)
		return;
	platform_loading_chant(0, 0.0f);
	xgpu_backend_loading_caption(NULL, 0, 0);
	xgpu_backend_loading(0);
	xgpu_backend_loading_mode(0);
	loading_presentation_active = 0;
	loading_screen_caption[0] = 0;
	halo_load_trace("loading.request", "screen end");
}

int halo_loading_owned(void)
{
	return loading_presentation_active == 1 && xgpu_backend_loading_owned();
}

void halo_loading_retire(void)
{
	/* Loading may already be active while progress frames borrow map data. */
	xgpu_backend_consume();
}

void halo_loading_end(void)
{
	/* (a map load's: the port's own waits end themselves) */
	if (loading_presentation_active && loading_presentation_active != 3)
	{
		platform_loading_chant(0, 0.0f);
		xgpu_backend_loading(0);
		loading_presentation_active = 0;
	}
}

void halo_loading_ready(void)
{
	if (loading_presentation_active == 1)
	{
		/* (the chant fades as the level comes up) */
		platform_loading_chant(0, 0.0f);
		/* the level's first pictures come up out of the dark (VR's eyes) */
		halo_loading_fade_begin();
		xgpu_backend_loading(2);
		loading_presentation_active = 2;
	}
}

static struct platform_input_state input_state;
/* keys pressed since the last read, so a press and release between two
reads still counts as a press (input injected on Android, or a slow frame) */
static unsigned char keys_pressed[SDL_SCANCODE_COUNT];
/* F3 to F7, and F9, pressed since the menu studio last asked, a bit each
(platform_menu_studio_keys) */
static int menu_studio_keys;
/* the menu studio's note being typed: the keys go to it, not the game
(platform_menu_studio_typing), as characters, \b, \n and \033 */
static BOOL menu_studio_typing;
static char menu_studio_typed[256];
static int menu_studio_typed_count;
/* The bound Screenshot action requests capture at the next presentation. */
static BOOL screenshot_requested;
/* likewise the mouse buttons pressed since the last read, so that a click
quicker than a frame still counts */
static unsigned char mouse_buttons_pressed[PLATFORM_MOUSE_BUTTON_COUNT];
#ifndef HALO_ANDROID
/* the menus' pointer (platform_ui_pointer_set_active), under input_lock */
static struct platform_ui_pointer ui_pointer;
static float ui_pointer_wheel;
/* the mouse's Back button pressed in the menus since the controller's last
reading (input_state.menu_back), so that a click quicker than a frame still
backs out */
static BOOL menu_back_pressed;
#endif
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
/* the presses that skip a Bink movie (platform_movie_skip_pressed), under
input_lock: a key down (not F11's or F12's, which work the window), a mouse
button or a finger down; and how many were seen */
static unsigned long movie_skip_presses, movie_skip_presses_seen;
/* a gamepad's stick or trigger past these is in use (the menus' cursor) */
#define INPUT_AXIS_PRESSED 16000
#define INPUT_TRIGGER_PRESSED PLATFORM_GAMEPAD_TRIGGER_PRESSED
/* port/linux/src/xinput_sdl.c: the scripted player's mouse, as its events
would bring it (debug.test_input "latency:") */
void halo_test_input_pump(struct platform_input_state *state, unsigned char *buttons_pressed);

/* (under input_lock) each gamepad's buttons pressed and triggers pulled since
the controller's last reading of it (platform_gamepad_presses_take), as
keys_pressed is the keyboard's: a tap pressed and let go of between two
readings (a slow frame) still counts as a press. By the gamepad's joystick
ID (never 0); triggers' bit 0 is the left, bit 1 the right; triggers_down,
which are past INPUT_TRIGGER_PRESSED now, so that a pull counts once */
#define GAMEPAD_PRESSES_COUNT 8
typedef char gamepad_presses_buttons_fit[SDL_GAMEPAD_BUTTON_COUNT <= 32 ? 1 : -1];
static struct gamepad_presses
{
	Uint32 id;
	Uint32 buttons;
	unsigned int triggers;
	unsigned int triggers_down;
} gamepad_presses[GAMEPAD_PRESSES_COUNT];

/* a gamepad's presses, given a place first if it has none and add is set */
static struct gamepad_presses *gamepad_presses_find(Uint32 id, BOOL add)
{
	struct gamepad_presses *empty = NULL;
	int index;

	if (!id)
		return NULL;
	for (index = 0; index < GAMEPAD_PRESSES_COUNT; index++)
	{
		if (gamepad_presses[index].id == id)
			return &gamepad_presses[index];
		if (!gamepad_presses[index].id && !empty)
			empty = &gamepad_presses[index];
	}
	if (!add || !empty)
		return NULL;
	memset(empty, 0, sizeof(*empty));
	empty->id = id;
	return empty;
}

static void gamepad_button_pressed(Uint32 id, int button)
{
	struct gamepad_presses *presses = gamepad_presses_find(id, TRUE);

	if (presses && button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT)
		presses->buttons |= 1u << button;
}

static void gamepad_trigger_moved(Uint32 id, int axis, int value)
{
	struct gamepad_presses *presses;
	unsigned int bit;

	if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
		bit = 1;
	else if (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
		bit = 2;
	else
		return;
	presses = gamepad_presses_find(id, value >= INPUT_TRIGGER_PRESSED);
	if (!presses)
		return;
	if (value < INPUT_TRIGGER_PRESSED)
		presses->triggers_down &= ~bit;
	else if (!(presses->triggers_down & bit))
	{
		presses->triggers_down |= bit;
		presses->triggers |= bit;
	}
}

static void gamepad_presses_forget(Uint32 id)
{
	struct gamepad_presses *presses = gamepad_presses_find(id, FALSE);

	if (presses)
		memset(presses, 0, sizeof(*presses));
}

/* (the window's focus gone: what was pressed is dropped, as the keys are) */
static void gamepad_presses_clear(void)
{
	int index;

	for (index = 0; index < GAMEPAD_PRESSES_COUNT; index++)
	{
		gamepad_presses[index].buttons = 0;
		gamepad_presses[index].triggers = 0;
	}
}

static void gamepad_presses_take(Uint32 id, Uint32 *buttons, unsigned int *triggers)
{
	struct gamepad_presses *presses = gamepad_presses_find(id, FALSE);

	*buttons = 0;
	*triggers = 0;
	if (presses)
	{
		*buttons = presses->buttons;
		*triggers = presses->triggers;
		presses->buttons = 0;
		presses->triggers = 0;
	}
}

void platform_gamepad_presses_take(Uint32 id, Uint32 *buttons, unsigned int *triggers)
{
	pthread_mutex_lock(&input_lock);
	gamepad_presses_take(id, buttons, triggers);
	pthread_mutex_unlock(&input_lock);
}
/* xinput_sdl.c: a gamepad's gyroscope or accelerometer sample */
void halo_gyro_sensor_event(SDL_JoystickID which, int sensor, const float *data, Uint64 sensor_ns, Uint64 event_ns);
/* rebinding (platform_binding_capture_begin), under input_lock: waiting for
an input, and the one taken; then the keyboard and mouse settle (their keys
and buttons held then are let go) before they drive anything again */
enum
{
	_binding_capture_idle,
	_binding_capture_waiting,
	_binding_capture_taken,
};
static int binding_capture;
static int binding_capture_result;
static int binding_captured_input;
static BOOL binding_settling;
static Uint64 binding_taken_ms;
/* when the menus last asked (a capture they stop asking about, their screen
gone, ends: else the keyboard stays held from the game) */
static Uint64 binding_polled_ms;
#define BINDING_ABANDONED_MS 500
static unsigned mouse_buttons_down;
/* (an input taken that nothing asks for is let go after this) */
#define BINDING_UNCLAIMED_MS 2000
/* the multiplayer scoreboard is open (platform_scoreboard_scroll): the wheel
and Page Up/Down scroll it, and the wheel switches no weapon; how far they
have moved it since the game last asked (notches down, pages down). Open
until the game stops saying so for SCOREBOARD_OPEN_MS (a game that ends with
it open never says it closed). */
#define SCOREBOARD_OPEN_MS 250
static Uint64 scoreboard_open_until_ms;
static float scoreboard_wheel;
static long scoreboard_notches;
static long scoreboard_pages;
#ifndef HALO_ANDROID
/* the scoreboard's pointer (platform_scoreboard_pointer): while the game
offers it (a network game's scoreboard is open), a right click frees the
mouse, whose pointer then picks a player; its motion and clicks go to it,
not to the aim and the triggers. Another right click, or the scoreboard
closing, takes the mouse back for the aim. */
static BOOL scoreboard_pointer_offered;
static struct platform_ui_pointer scoreboard_pointer;
#endif
static BOOL scoreboard_pointer_active;

/* debug keyboard queue */
#define KEYSTROKE_QUEUE_SIZE 64
static struct platform_keystroke keystroke_queue[KEYSTROKE_QUEUE_SIZE];
static unsigned long keystroke_head, keystroke_count;

static void screen_keyboard_update(void);
#ifndef HALO_ANDROID
/* dsound_sdl.c's: the output device followed */
void dsound_sdl_output_device_check(void);
/* updater.c's: the desktop self-updater */
void updater_start(void);
void updater_poll(SDL_Window *window);
/* the windows' icon, a PNG (tools/embed_assets.py, from port/assets/icon) */
extern const unsigned int platform_window_icon[];
extern const unsigned long platform_window_icon_size;
/* the menus' pointer, a PNG (tools/embed_assets.py, from port/assets/cursor) */
extern const unsigned int platform_menu_cursor[];
extern const unsigned long platform_menu_cursor_size;
#endif

/* the menus' own cursor (below): the desktop's guests (not iOS's, which has
no pointer to draw), whose host makes it */
#if defined(HALO_GUEST) && !defined(HALO_ANDROID) && !defined(HALO_IOS)
#define PLATFORM_MENU_CURSOR
#endif

#ifndef HALO_ANDROID
/* the maps folder checked to hold the Xbox maps (platform_maps_folder_check).
When it holds Halo PC's instead, or no ui.map, the player is told where each
kind goes and the game quits, rather than failing to load Halo PC's ui.map
as the Xbox's (game_load). Halo PC maps in maps/ itself, which play only from
custom_maps, are warned of */
static void data_check_maps(void)
{
	struct platform_maps_folder maps;
	char message[2048];
	char where[1200];
	BOOL quiet = config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		config_string("debug.network_test")[0];

	platform_maps_folder_check(&maps);
	snprintf(where, sizeof(where),
		"Put the Xbox maps from your Halo: Combat Evolved disc image in %s/maps. Halo Custom Edition "
		"maps go in a custom_maps folder beside it.",
		maps.root);
	switch (maps.state)
	{
	case _maps_folder_xbox:
		if (!maps.stray_pc_maps)
			return;
		snprintf(message, sizeof(message),
			"These maps in the maps folder are Halo PC maps, which play only from custom_maps: %s%s.\n\n%s",
			maps.stray_names, maps.stray_pc_maps > PLATFORM_MAPS_FOLDER_NAMED ? " and more" : "", where);
		platform_log("maps folder: %ld Halo PC maps in %s/maps (%s)", maps.stray_pc_maps, maps.root,
			maps.stray_names);
		if (!quiet)
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Halo", message, NULL);
		return;
	case _maps_folder_halo_pc:
		snprintf(message, sizeof(message),
			"The maps folder holds Halo PC maps, not the Xbox maps: its ui.map is from %s.\n\n%s\n\n"
			"To copy the Xbox maps out of your disc image again, move this maps folder aside and "
			"start the game.",
			maps.ui_version == 609 ? "Halo PC (Custom Edition)" : "Halo PC", where);
		break;
	default:
		snprintf(message, sizeof(message),
			"Halo's Xbox maps were not found: %s/maps has no ui.map.\n\n%s\n\n"
			"To copy the Xbox maps out of your disc image, move the maps folder aside and start the game.",
			maps.root, where);
		break;
	}
	platform_log("maps folder: %s/maps is not the Xbox maps (ui.map version %ld): quitting", maps.root,
		maps.ui_version);
	platform_log("%s", message);
	if (!quiet)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", message, NULL);
	exit(EXIT_FAILURE);
}
#endif

/* the game's own assets' file, embedded_assets.h: missing, or another
build's, the game quits with a message naming it, as it does on missing
maps (data_check_maps), rather than showing menus without their files */
static void assets_check(void)
{
	char message[2048];
	const char *problem = embedded_assets_check();
	BOOL quiet = config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		config_string("debug.network_test")[0];

	if (!problem)
		return;
	snprintf(message, sizeof(message),
		"The game's own files are incomplete: %s.\n\nIt holds the menus, the high-res HUD and the text. "
		"Reinstall or update GOAT H1 so that the " EMBEDDED_ASSETS_FILE " that came with this version is "
		"beside it.", problem);
	platform_log("assets: %s: quitting", problem);
	if (!quiet)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", message, NULL);
	exit(EXIT_FAILURE);
}

BOOL platform_sdl_initialize(void)
{
	SDL_InitFlags flags = SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS;

	if (platform_sdl_started)
		return TRUE;
#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID)
	/* a write to a connection the other end closed fails instead of ending
	the game (the game's sockets and Discord's pass MSG_NOSIGNAL, but UPnP's
	miniupnpc does not, nor does a write to a closed pipe's standard error) */
	signal(SIGPIPE, SIG_IGN);
#endif
	/* a copy of the game started to open an invite link hands it to the
	one already running, and goes */
	if (p2p_hand_off_invite())
		exit(EXIT_SUCCESS);
	SDL_SetHint(SDL_HINT_APP_NAME, "Halo");
#ifdef HALO_ANDROID
	/* landscape (either way up) or, display.orientation, portrait or
	either (SDL asks the activity for it as it makes the window; then
	orientation_apply, as the setting changes); the back key arrives as a
	key event (xinput_sdl.c) instead of closing the activity */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, !strcmp(config_string("display.orientation"), "portrait") ?
		"Portrait PortraitUpsideDown" : !strcmp(config_string("display.orientation"), "auto") ?
		"LandscapeLeft LandscapeRight Portrait PortraitUpsideDown" : "LandscapeLeft LandscapeRight");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
	/* touching the screen must not aim or fire (the mouse drives the
	controller emulation in xinput_sdl.c) */
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif
#if defined(__linux__) && !defined(HALO_ANDROID)
	/* input.playstation_kernel_driver: a PlayStation pad through the
	kernel's driver of it (hid-playstation's, hid-sony's: its buttons,
	sticks, triggers and rumble) where the kernel has one, not SDL's own:
	SDL's reads the pad's hidraw node, which only udev rules (Steam's) open
	to the player, and a pad it took and could not open was not found at
	all. SDL's own has the lightbar and the motion sensors */
	if (config_boolean("input.playstation_kernel_driver"))
	{
		if (SDL_GetPathInfo("/sys/bus/hid/drivers/playstation", NULL))
			SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5, "0");
		if (SDL_GetPathInfo("/sys/bus/hid/drivers/sony", NULL))
			SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4, "0");
	}
#endif
#ifndef HALO_ANDROID
	/* a dedicated server (server_mode.c) runs where there is no display,
	sound or controller (a service, a container): SDL's drivers that need
	none, and its events alone */
	if (halo_server_mode())
	{
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
		SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
		flags = SDL_INIT_EVENTS;
	}
#endif
#ifdef HALO_MACOS
	/* a run nobody watches (debug.hidden_window): a background process,
	with no Dock icon, that never takes the focus from the person at the
	computer as it starts */
	if (config_boolean("debug.hidden_window"))
		SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
#endif
	if (!SDL_Init(flags))
	{
		/* (tried again by each caller that needs SDL, some of them every
		frame: a failure is logged once, and again only when its reason
		changes, not tens of thousands of times without a display) */
		static char last_failure[256];
		const char *error = SDL_GetError();

		if (strncmp(error, last_failure, sizeof(last_failure) - 1))
		{
			snprintf(last_failure, sizeof(last_failure), "%s", error);
			platform_log("SDL_Init failed: %s", error);
		}
		return FALSE;
	}
	platform_sdl_started = TRUE;
	/* the game's own files' bytes (the menus', the high-res HUD's and text's:
	embedded_assets.h), beside it as a build or a package puts them; a
	dedicated server draws nothing, and starts without them */
	if (!halo_server_mode())
		assets_check();
#ifndef HALO_ANDROID
	/* found (or offered to the player, platform_offer_game_data) before the
	game's window opens, and checked to be the Xbox maps (a dedicated
	server's were, by server_mode.c) */
	platform_data_root();
	if (!halo_server_mode())
		data_check_maps();
	/* (a new version looked for meanwhile, updater_poll asking about it; a
	dedicated server's is server.auto_update's) */
	if (!halo_server_mode())
	{
		updater_start();
		/* (a player's public game on the master servers too, with a
		publisher key: server_master.c) */
		server_master_start_host();
	}
#endif
	/* (a Discord account linked at a master server: its passes asked for
	again before they run out, player_link.c) */
	if (!halo_server_mode())
		player_link_start();
	/* (the last run's crash found, to be offered at the main menu, and
this run's marked as begun: report.c) */
	if (!halo_server_mode())
		report_session_start();
	/* (the mods, and whether to play those that say they cannot be played
	together, before the window opens: port_mods.c) */
	port_mods_startup();
	/* (the PC menus' files are read ahead once the renderer is ready, which
	their rows for it need: d3d8_device.c's device_backend_ready) */
	return TRUE;
}

#if !defined(HALO_ANDROID) && (defined(HALO_WINDOWS) || defined(HALO_IOS))
/* ---------- first start without game data (xbox_files.c), on Windows: the
system's dialogs. (The Linux builds' and the macOS app's is first_start.c,
in the game's own window; the iOS app asks before the game starts,
port/ios/host_app.m)

Besides the Xbox disc image the game needs, the player may add Custom
Edition's maps from its installer (halocesetup_en_1.00.exe) into
custom_maps (map_import.c), as ChupathingyCE's first start does (their
pull request #313, whose choices this follows); a file picked is told apart
by its contents. */

struct data_extraction
{
	pthread_mutex_t lock;
	/* Custom Edition's installer, into custom_maps (destination) */
	BOOL installer;
	unsigned long long written;
	char image[1024];
	char destination[1024];
	char file[256];
	unsigned long long done;
	unsigned long long total;
	BOOL finished;
	BOOL succeeded;
	char error[512];
};

static void data_extraction_progress(void *context, const char *file, unsigned long long done,
	unsigned long long total)
{
	struct data_extraction *extraction = context;

	pthread_mutex_lock(&extraction->lock);
	snprintf(extraction->file, sizeof(extraction->file), "%s", file);
	extraction->done = done;
	extraction->total = total;
	pthread_mutex_unlock(&extraction->lock);
}

static void *data_extraction_thread(void *context)
{
	struct data_extraction *extraction = context;
	unsigned long long written = 0;
	BOOL succeeded;

	if (extraction->installer)
	{
		succeeded = map_import_custom_edition(extraction->image, extraction->destination, data_extraction_progress,
			extraction, &written, extraction->error, sizeof(extraction->error)) != 0;
	}
	else
	{
		succeeded = xiso_extract_maps(extraction->image, extraction->destination, data_extraction_progress,
			extraction, extraction->error, sizeof(extraction->error)) != 0;
		/* (and the stock maps unpacked into the cache's slots, on the
		same bar: map_unpack.c) */
		if (succeeded)
			map_unpack_after_extraction(extraction->destination, data_extraction_progress, extraction);
	}
	pthread_mutex_lock(&extraction->lock);
	extraction->written = written;
	extraction->succeeded = succeeded;
	extraction->finished = TRUE;
	pthread_mutex_unlock(&extraction->lock);
	return NULL;
}

/* copies the maps (with installer, Custom Edition's installer's into the
custom_maps folder destination, the bytes written in *written), showing how
far it has got; closing the window quits */
static BOOL data_extract(const char *image, const char *destination, BOOL installer, unsigned long long *written,
	char *error, int error_size)
{
	static struct data_extraction extraction;
	SDL_Window *window;
	SDL_Renderer *renderer = NULL;
	pthread_t thread;
	BOOL finished = FALSE;

	memset(&extraction, 0, sizeof(extraction));
	pthread_mutex_init(&extraction.lock, NULL);
	extraction.installer = installer;
	snprintf(extraction.image, sizeof(extraction.image), "%s", image);
	snprintf(extraction.destination, sizeof(extraction.destination), "%s", destination);
	if (pthread_create(&thread, NULL, data_extraction_thread, &extraction) != 0)
	{
		snprintf(error, (size_t)error_size, "Could not start the extraction.");
		return FALSE;
	}
	/* (waited for through extraction.finished; the Windows port's threads
	cannot be joined) */
	pthread_detach(thread);
	window = SDL_CreateWindow("GOAT H1", 640, 150, 0);
	if (window)
	{
		renderer = SDL_CreateRenderer(window, NULL);
		if (renderer)
			SDL_SetRenderVSync(renderer, 1);
	}
	while (!finished)
	{
		SDL_Event event;
		char file[256];
		unsigned long long done, total;

		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				platform_log("extraction cancelled");
				exit(EXIT_SUCCESS);
			}
		}
		pthread_mutex_lock(&extraction.lock);
		finished = extraction.finished;
		snprintf(file, sizeof(file), "%s", extraction.file);
		done = extraction.done;
		total = extraction.total;
		pthread_mutex_unlock(&extraction.lock);
		if (renderer)
		{
			char line[320];
			SDL_FRect bar = { 20.0f, 100.0f, 600.0f, 24.0f };
			float fraction = total ? (float)((double)done / (double)total) : 0.0f;

			SDL_SetRenderDrawColor(renderer, 12, 16, 20, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
			SDL_SetRenderScale(renderer, 2.0f, 2.0f);
			SDL_RenderDebugText(renderer, 10.0f, 10.0f,
				installer ? "Adding Custom Edition's maps..." : "Extracting the maps folder...");
			SDL_SetRenderScale(renderer, 1.0f, 1.0f);
			snprintf(line, sizeof(line), "%s  (%llu of %llu MB)", file, done >> 20, total >> 20);
			SDL_RenderDebugText(renderer, 20.0f, 70.0f, line);
			SDL_SetRenderDrawColor(renderer, 60, 66, 72, 255);
			SDL_RenderFillRect(renderer, &bar);
			bar.w *= fraction;
			SDL_SetRenderDrawColor(renderer, 90, 160, 90, 255);
			SDL_RenderFillRect(renderer, &bar);
			SDL_RenderPresent(renderer);
		}
		SDL_Delay(16);
	}
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	if (written)
		*written = extraction.written;
	if (!extraction.succeeded)
		snprintf(error, (size_t)error_size, "%s", extraction.error);
	return extraction.succeeded;
}

struct data_image_choice
{
	SDL_AtomicInt done;
	BOOL failed;
	char path[1024];
	char error[256];
};

static void SDLCALL data_image_chosen(void *userdata, const char * const *files, int filter)
{
	struct data_image_choice *choice = userdata;

	(void)filter;
	if (!files)
	{
		/* no dialog could be shown (SDL needs the desktop portal through
		libdbus, or zenity) */
		choice->failed = TRUE;
		snprintf(choice->error, sizeof(choice->error), "%s",
			*SDL_GetError() ? SDL_GetError() : "no desktop portal or zenity");
	}
	else if (files[0])
	{
		snprintf(choice->path, sizeof(choice->path), "%s", files[0]);
	}
	SDL_SetAtomicInt(&choice->done, 1);
}

/* a disc image in the folder the maps are extracted to (next to the
executable), for systems where no file dialog can be shown */
static BOOL data_find_image(const char *directory, char *path, int size)
{
	static const char *const patterns[] = { "*.iso", "*.xiso" };
	int pattern;

	for (pattern = 0; pattern < (int)(sizeof(patterns) / sizeof(patterns[0])); pattern++)
	{
		int count = 0;
		char **files = SDL_GlobDirectory(directory, patterns[pattern], SDL_GLOB_CASEINSENSITIVE, &count);

		if (files && count > 0)
		{
			snprintf(path, (size_t)size, "%s/%s", directory, files[0]);
			SDL_free(files);
			return TRUE;
		}
		SDL_free(files);
	}
	return FALSE;
}

/* the disc image (with installer, Custom Edition's installer) the player
picks; FALSE if they pick none, with *failed set (and error written) when no
dialog could be shown */
static BOOL data_choose_image(char *path, int size, BOOL installer, BOOL *failed, char *error, int error_size)
{
	static const SDL_DialogFileFilter image_filters[] =
	{
		{ "Xbox disc images", "iso;xiso" },
		{ "All files", "*" },
	};
	static const SDL_DialogFileFilter installer_filters[] =
	{
		{ "Custom Edition's installer", "exe" },
		{ "All files", "*" },
	};
	static struct data_image_choice choice;

	memset(&choice, 0, sizeof(choice));
	SDL_ShowOpenFileDialog(data_image_chosen, &choice, NULL, installer ? installer_filters : image_filters, 2, NULL,
		false);
	/* the dialog answers through events (and on some systems another
	thread) */
	while (!SDL_GetAtomicInt(&choice.done))
	{
		SDL_PumpEvents();
		SDL_Delay(50);
	}
	*failed = choice.failed;
	if (choice.failed)
		snprintf(error, (size_t)error_size, "%s", choice.error);
	if (!choice.path[0])
		return FALSE;
	snprintf(path, (size_t)size, "%s", choice.path);
	return TRUE;
}

/* adds Custom Edition's maps from the installer the player picks, saying
what became of it */
static void data_import_custom_edition(const char *destination)
{
	char installer[1024];
	char folder[1024];
	char error[512];
	char message[1600];
	unsigned long long written = 0;
	BOOL failed = FALSE;
	int kind;

	if (!data_choose_image(installer, sizeof(installer), TRUE, &failed, error, sizeof(error)))
	{
		if (failed)
		{
			platform_log("no file dialog: %s", error);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", error, NULL);
		}
		return;
	}
	kind = map_import_kind(installer);
	if (kind != _map_import_custom_edition_installer)
	{
		platform_log("%s is not Custom Edition's installer", installer);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", kind == _map_import_cd_image ? map_import_cd_image_text :
			kind == _map_import_xbox_disc ? "That is an Xbox disc image: choose Xbox disc image for it." :
			"That file is not Custom Edition's installer (halocesetup_en_1.00.exe).", NULL);
		return;
	}
	map_import_custom_maps_folder(destination, folder, sizeof(folder));
	if (!data_extract(installer, folder, TRUE, &written, error, sizeof(error)))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", error, NULL);
		return;
	}
	snprintf(message, sizeof(message), "%s %s. They show in the map lists' CUSTOM MULTIPLAYER.\n\n"
		"Halo still needs the maps of your Xbox disc to start: choose Xbox disc image next.",
		written ? "Custom Edition's maps are in" : "Custom Edition's maps were already in", folder);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Halo", message, NULL);
}

BOOL platform_offer_game_data(const char *destination, const char *missing)
{
	enum
	{
		_choice_quit,
		_choice_xbox,
		_choice_custom_edition,
	};
	static const SDL_MessageBoxButtonData buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, _choice_xbox, "Xbox disc image" },
		{ 0, _choice_custom_edition, "Custom Edition maps" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, _choice_quit, "Quit" },
	};
	char message[2600];
	char found[1024];
	BOOL image_found;

	/* not for runs nobody is watching */
	if (config_boolean("debug.hidden_window") || config_real("debug.exit_after") > 0.0 ||
		!SDL_Init(SDL_INIT_VIDEO))
	{
		return FALSE;
	}
	if (missing && *missing)
		platform_log("paths.data (%s) has no maps folder", missing);
	image_found = data_find_image(destination, found, sizeof(found));
	if (image_found)
	{
		snprintf(message, sizeof(message),
			"Halo's game data (its maps folder) was not found.\n\n"
			"Xbox disc image: extract the maps folder from the Xbox disc image %s. "
			"It is copied to %s/maps (about 2 GB).\n\n"
			"Custom Edition maps (optional): add the multiplayer maps of Custom Edition's installer "
			"(halocesetup_en_1.00.exe) to custom_maps.\n\n"
			"(Or put the maps folder there yourself, or set paths.data in config.toml.)",
			found, destination);
	}
	else
	{
		snprintf(message, sizeof(message),
			"Halo's game data (its maps folder) was not found.\n\n"
			"Xbox disc image: extract the maps folder from an Xbox disc image (.iso) of Halo: Combat Evolved. "
			"It is copied to %s/maps (about 2 GB).\n\n"
			"Custom Edition maps (optional): add the multiplayer maps of Custom Edition's installer "
			"(halocesetup_en_1.00.exe) to custom_maps.\n\n"
			"(Or put the maps folder there yourself, or set paths.data in config.toml.)",
			destination);
	}
	for (;;)
	{
		SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, NULL, "Halo", message, 3, buttons, NULL };
		char image[1024];
		char error[512];
		int answer = _choice_quit;
		BOOL failed = FALSE;

		if (!SDL_ShowMessageBox(&question, &answer) || answer == _choice_quit)
		{
			platform_log("no game data: quitting");
			exit(EXIT_SUCCESS);
		}
		if (answer == _choice_custom_edition)
		{
			data_import_custom_edition(destination);
			continue;
		}
		if (image_found)
		{
			snprintf(image, sizeof(image), "%s", found);
		}
		else if (!data_choose_image(image, sizeof(image), FALSE, &failed, error, sizeof(error)))
		{
			char explanation[1400];

			/* no image picked: ask again */
			if (!failed)
				continue;
			/* no dialog could be shown */
			platform_log("no file dialog: %s", error);
			snprintf(explanation, sizeof(explanation),
				"No file dialog could be shown (%s).\n\n"
				"Put the disc image (.iso) in %s and start Halo again, "
				"or install zenity.",
				error, destination);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", explanation, NULL);
			exit(EXIT_SUCCESS);
		}
		if (map_import_kind(image) == _map_import_cd_image)
		{
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", map_import_cd_image_text, NULL);
			continue;
		}
		platform_log("extracting the maps folder from %s to %s", image, destination);
		if (data_extract(image, destination, FALSE, NULL, error, sizeof(error)))
		{
			platform_log("extracted the maps folder");
			return TRUE;
		}
		platform_log("extraction failed: %s", error);
		image_found = FALSE;
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", error, NULL);
	}
}
#endif

int halo_interpolation_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled;
}

#ifndef HALO_ANDROID
/* the display mode (display.mode, else display.fullscreen's: borderless or
the window) */
enum
{
	_display_mode_windowed = 0,
	_display_mode_borderless,
	_display_mode_fullscreen
};

static int platform_display_mode(void)
{
	const char *mode = config_string("display.mode");

	if (!strcmp(mode, "fullscreen"))
		return _display_mode_fullscreen;
	if (!strcmp(mode, "borderless"))
		return _display_mode_borderless;
	if (!strcmp(mode, "windowed"))
		return _display_mode_windowed;
	return config_boolean("display.fullscreen") ? _display_mode_borderless : _display_mode_windowed;
}

/* whether the window opens fullscreen (either kind), never when it is
hidden */
static BOOL platform_fullscreen_setting(void)
{
	return !config_boolean("debug.hidden_window") && platform_display_mode() != _display_mode_windowed;
}

/* a size as a setting has it, "<width>x<height>": whether it is one, and
the Xbox's 640x480 or more */
static BOOL platform_size_parse(const char *text, long *width, long *height)
{
	char *end;

	*width = strtol(text, &end, 10);
	*height = *end == 'x' || *end == 'X' ? strtol(end + 1, &end, 10) : 0;
	return !*end && *width >= 640 && *height >= 480;
}

/* display.resolution in pixels, or 0x0 for the display's own ("native"),
as for one the game cannot draw at */
static void platform_resolution_setting(long *width, long *height)
{
	/* (read again as the settings change: config_changes; the screen's
	mode asks every frame) */
	static unsigned long read_at = (unsigned long)-1;
	static long setting_width, setting_height;

	if (read_at != config_changes())
	{
		unsigned long changes = config_changes();

		if (!platform_size_parse(config_string("display.resolution"), &setting_width, &setting_height))
			setting_width = setting_height = 0;
		read_at = changes;
	}
	*width = setting_width;
	*height = setting_height;
}

/* the window's size (display.window_size), else the Xbox's 640x480 times
display.window_scale, as older versions set it */
static void platform_window_size_setting(long *width, long *height)
{
	long scale = config_integer("display.window_scale");

	if (!platform_size_parse(config_string("display.window_size"), width, height))
	{
		*width = 640 * (scale < 1 ? 1 : scale);
		*height = 480 * (scale < 1 ? 1 : scale);
	}
}

/* a display's own size in pixels (its desktop mode) */
static BOOL platform_display_size(SDL_DisplayID display, long *width, long *height)
{
	const SDL_DisplayMode *mode = display ? SDL_GetDesktopDisplayMode(display) : NULL;

	if (!mode)
		return FALSE;
	*width = (long)(mode->w * mode->pixel_density + 0.5f);
	*height = (long)(mode->h * mode->pixel_density + 0.5f);
	return TRUE;
}

/* whether fullscreen can set the display's refresh rate: on Windows, and
on an X server's display of its own; not on Wayland or XWayland (whose
modes are pretended, the display's own kept), nor under gamescope, nor on
macOS, iOS or Android, whose displays stay at their own rate */
int platform_refresh_rates_settable(void)
{
#ifdef HALO_MACOS
	return 0;
#else
	const char *driver;
	const char *wayland = getenv("WAYLAND_DISPLAY");
	const char *gamescope = getenv("GAMESCOPE_WAYLAND_DISPLAY");

	if (!platform_sdl_initialize() || !(driver = SDL_GetCurrentVideoDriver()))
		return 0;
	if (!strcmp(driver, "windows"))
		return 1;
	return !strcmp(driver, "x11") && !(wayland && wayland[0]) && !(gamescope && gamescope[0]);
#endif
}

/* display.refresh_rate where fullscreen can set it, else 0: the display's
own */
static float platform_refresh_rate_setting(void)
{
	double rate = config_real("display.refresh_rate");

	return rate > 0.0 && rate < 1000.0 && platform_refresh_rates_settable() ? (float)rate : 0.0f;
}

/* the window's fullscreen kind (display.mode): borderless, a window over
the whole desktop (SDL's fullscreen without a mode), or fullscreen, the
display taken at display.resolution's mode (the one nearest it), else at its
desktop one, at display.refresh_rate's rate (the nearest the display has at
that size), else at the desktop's. F11 switches to the kind set. */
static void platform_fullscreen_kind_apply(void)
{
	static int applied = -1;
	static long applied_width, applied_height;
	static float applied_rate;
	int exclusive = platform_display_mode() == _display_mode_fullscreen ? 1 : 0;
	long width = 0, height = 0;
	float rate = 0.0f;
	SDL_DisplayID display;
	SDL_DisplayMode closest;
	const SDL_DisplayMode *mode = NULL, *desktop;

	if (!platform_window)
		return;
	if (exclusive)
	{
		platform_resolution_setting(&width, &height);
		rate = platform_refresh_rate_setting();
	}
	if (exclusive == applied && width == applied_width && height == applied_height && rate == applied_rate)
		return;
	applied = exclusive;
	applied_width = width;
	applied_height = height;
	applied_rate = rate;
	display = SDL_GetDisplayForWindow(platform_window);
	if (exclusive && display)
	{
		desktop = SDL_GetDesktopDisplayMode(display);
		if (width && SDL_GetClosestFullscreenDisplayMode(display, (int)width, (int)height, rate, false, &closest))
			mode = &closest;
		else if (!width && rate > 0.0f && desktop &&
			SDL_GetClosestFullscreenDisplayMode(display, desktop->w, desktop->h, rate, true, &closest))
		{
			mode = &closest;
		}
		else
			mode = desktop;
		if (mode && rate > 0.0f)
			platform_log("display: fullscreen at %dx%d, %.2f Hz (display.refresh_rate %.2f)", mode->w, mode->h,
				mode->refresh_rate, rate);
	}
	SDL_SetWindowFullscreenMode(platform_window, mode);
}

/* whether the game last asked for the window to be fullscreen */
static BOOL platform_fullscreen_requested = FALSE;

static void platform_window_set_fullscreen(BOOL fullscreen)
{
	platform_fullscreen_requested = fullscreen;
	SDL_SetWindowFullscreen(platform_window, fullscreen ? true : false);
}

/* whether the window is fullscreen. SDL sets its flag when the window
manager confirms the request, and some never do: gamescope (the Steam
Deck's Game Mode) makes the window the size of the display but leaves the
flag unset, so the game drew 640x480 and gamescope stretched it. A window
that was asked to be fullscreen and covers its display counts too. */
static BOOL platform_window_fullscreen(void)
{
	SDL_DisplayID display;
	const SDL_DisplayMode *mode;
	int width, height;

	if (SDL_GetWindowFlags(platform_window) & SDL_WINDOW_FULLSCREEN)
		return TRUE;
	if (!platform_fullscreen_requested)
		return FALSE;
	display = SDL_GetDisplayForWindow(platform_window);
	mode = display ? SDL_GetDesktopDisplayMode(display) : NULL;
	if (!mode || !SDL_GetWindowSizeInPixels(platform_window, &width, &height))
		return FALSE;
	return width >= (int)(mode->w * mode->pixel_density + 0.5f) &&
		height >= (int)(mode->h * mode->pixel_density + 0.5f);
}

/* the size in pixels the game draws its picture at (d3d8_device.c): the
window's, the display's while fullscreen, or display.resolution's while
fullscreen (either kind) where the display has room for it; before the
window opens, what it will be. FALSE where display.resolution_scaling is
"original": the Xbox's 640x480, scaled to the window. */
BOOL platform_screen_mode(long *width, long *height)
{
	/* (the size last given, for a window that has none: minimized) */
	static long last_width, last_height;
	/* (display.resolution_scaling, read again as the settings change:
	config_changes) */
	static unsigned long scaling_read_at = (unsigned long)-1;
	static BOOL original;
	long resolution_width, resolution_height;
	BOOL fullscreen;

	if (scaling_read_at != config_changes())
	{
		unsigned long changes = config_changes();

		original = !strcmp(config_string("display.resolution_scaling"), "original");
		scaling_read_at = changes;
	}
	if (original)
		return FALSE;
	if (platform_window)
	{
		int pixel_width = 0, pixel_height = 0;

		SDL_GetWindowSizeInPixels(platform_window, &pixel_width, &pixel_height);
		if (pixel_width <= 0 || pixel_height <= 0)
		{
			*width = last_width;
			*height = last_height;
			return last_width > 0;
		}
		*width = pixel_width;
		*height = pixel_height;
		fullscreen = platform_window_fullscreen();
	}
	else
	{
		if (!platform_sdl_initialize())
			return FALSE;
		fullscreen = platform_fullscreen_setting();
		platform_window_size_setting(width, height);
		if (fullscreen && !platform_display_size(SDL_GetPrimaryDisplay(), width, height))
			return FALSE;
	}
	/* (fullscreen's display is at the resolution already, where it has that
	mode: platform_fullscreen_kind_apply; borderless's is scaled to) */
	platform_resolution_setting(&resolution_width, &resolution_height);
	if (fullscreen && resolution_width && resolution_width <= *width && resolution_height <= *height)
	{
		*width = resolution_width;
		*height = resolution_height;
	}
	last_width = *width;
	last_height = *height;
	return TRUE;
}

/* the size added to the list unless it has it already; the count */
static int platform_resolution_add(long *widths, long *heights, int count, int maximum, long width, long height)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (widths[index] == width && heights[index] == height)
			return count;
	}
	if (count < maximum)
	{
		widths[count] = width;
		heights[count] = height;
		count++;
	}
	return count;
}

int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	SDL_DisplayID display;
	SDL_DisplayMode **modes;
	long display_width, display_height, width, height;
	int mode_count = 0, count = 0, index;

	if (maximum < 1 || !platform_sdl_initialize())
		return 0;
	display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
	if (!display)
		display = SDL_GetPrimaryDisplay();
	if (!platform_display_size(display, &display_width, &display_height))
		return 0;
	modes = SDL_GetFullscreenDisplayModes(display, &mode_count);
	for (index = 0; modes && index < mode_count; index++)
	{
		width = (long)(modes[index]->w * modes[index]->pixel_density + 0.5f);
		height = (long)(modes[index]->h * modes[index]->pixel_density + 0.5f);
		if (width >= 640 && height >= 480 && width <= display_width && height <= display_height &&
			(width != display_width || height != display_height))
		{
			count = platform_resolution_add(widths, heights, count, maximum, width, height);
		}
	}
	SDL_free(modes);
	/* (the one set, though this display has no such mode, so that Video
	Setup shows it) */
	platform_resolution_setting(&width, &height);
	if (width && (width != display_width || height != display_height))
		count = platform_resolution_add(widths, heights, count, maximum, width, height);
	/* largest first */
	for (index = 1; index < count; index++)
	{
		int place;

		width = widths[index];
		height = heights[index];
		for (place = index; place > 0 && (widths[place - 1] < width ||
			(widths[place - 1] == width && heights[place - 1] < height)); place--)
		{
			widths[place] = widths[place - 1];
			heights[place] = heights[place - 1];
		}
		widths[place] = width;
		heights[place] = height;
	}
	return count;
}

int platform_display_refresh_rates(long *rates, int maximum)
{
	SDL_DisplayID display;
	SDL_DisplayMode **modes;
	int mode_count = 0, count = 0, index, place;
	long rate;

	if (maximum < 1 || !platform_refresh_rates_settable())
		return 0;
	display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
	if (!display)
		display = SDL_GetPrimaryDisplay();
	modes = display ? SDL_GetFullscreenDisplayModes(display, &mode_count) : NULL;
	for (index = 0; modes && index <= mode_count; index++)
	{
		/* (then the one set, though this display has no such rate, so that
		Video Setup shows it) */
		rate = index < mode_count ? (long)(modes[index]->refresh_rate + 0.5f) :
			(long)(config_real("display.refresh_rate") + 0.5);
		if (rate < 24 || rate >= 1000)
			continue;
		for (place = 0; place < count && rates[place] != rate; place++)
		{
		}
		if (place == count && count < maximum)
			rates[count++] = rate;
	}
	SDL_free(modes);
	/* highest first */
	for (index = 1; index < count; index++)
	{
		rate = rates[index];
		for (place = index; place > 0 && rates[place - 1] < rate; place--)
			rates[place] = rates[place - 1];
		rates[place] = rate;
	}
	return count;
}

/* Video Setup's window sizes, by shape (4:3, 16:10, 16:9, 21:9), each from
the smallest */
static const short platform_window_sizes_offered[][2] =
{
	{ 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 960 }, { 1600, 1200 }, { 1920, 1440 }, { 2560, 1920 },
	{ 1280, 800 }, { 1440, 900 }, { 1680, 1050 }, { 1920, 1200 }, { 2560, 1600 },
	{ 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 },
	{ 2560, 1080 }, { 3440, 1440 }, { 3840, 1600 }, { 5120, 2160 },
};

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	SDL_DisplayID display;
	SDL_Rect usable;
	long width, height;
	int count = 0, index;

	if (maximum < 1 || !platform_sdl_initialize())
		return 0;
	display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
	if (!display)
		display = SDL_GetPrimaryDisplay();
	if (!display || !SDL_GetDisplayUsableBounds(display, &usable))
		usable.w = usable.h = 0;
	for (index = 0; index < (int)(sizeof(platform_window_sizes_offered) / sizeof(*platform_window_sizes_offered));
		index++)
	{
		width = platform_window_sizes_offered[index][0];
		height = platform_window_sizes_offered[index][1];
		/* (the Xbox's own whatever the desktop's size) */
		if (!index || (width <= usable.w && height <= usable.h))
			count = platform_resolution_add(widths, heights, count, maximum, width, height);
	}
	/* (the one set, though it is none of them or too big for this desktop,
	so that Video Setup shows it) */
	platform_window_size_setting(&width, &height);
	return platform_resolution_add(widths, heights, count, maximum, width, height);
}

#else
/* Android renders at the surface's pixel size. Before the window opens,
the host supplies the display's landscape pixel dimensions in the boot
environment. Keep the last valid size while a surface is unavailable. */
BOOL platform_screen_mode(long *width, long *height)
{
	static long last_width, last_height;

	if (platform_window)
	{
		int pixel_width = 0, pixel_height = 0;

		SDL_GetWindowSizeInPixels(platform_window, &pixel_width, &pixel_height);
		if (pixel_width > 0 && pixel_height > 0)
		{
			last_width = pixel_width;
			last_height = pixel_height;
		}
	}
	else
	{
		const char *display_width = getenv("HALO_DISPLAY_WIDTH");
		const char *display_height = getenv("HALO_DISPLAY_HEIGHT");
		long pixel_width = display_width ? atol(display_width) : 0;
		long pixel_height = display_height ? atol(display_height) : 0;

		if (pixel_width > 0 && pixel_height > 0)
		{
			last_width = pixel_width;
			last_height = pixel_height;
		}
	}
	*width = last_width;
	*height = last_height;
	return last_width > 0 && last_height > 0;
}

int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

/* (Android's display keeps its own rate: no Refresh Rate row) */
int platform_refresh_rates_settable(void)
{
	return 0;
}

int platform_display_refresh_rates(long *rates, int maximum)
{
	(void)rates;
	(void)maximum;
	return 0;
}

#endif

/* ---------- audio devices (Settings > Audio: audio.output_device,
audio.input_device) */

#ifndef HALO_ANDROID
int platform_audio_devices(int recording, char (*names)[PLATFORM_AUDIO_DEVICE_NAME_SIZE], int maximum)
{
	SDL_AudioDeviceID *devices;
	int device_count = 0, count = 0, index;

	if (maximum < 1 || !platform_sdl_initialize())
		return 0;
	/* (debug.audio_device_name's, first: a long name's menus tested) */
	{
		const char *fake = config_string("debug.audio_device_name");

		if (fake && fake[0] && !strchr(fake, '|') && strlen(fake) < PLATFORM_AUDIO_DEVICE_NAME_SIZE)
			snprintf(names[count++], PLATFORM_AUDIO_DEVICE_NAME_SIZE, "%s", fake);
	}
	devices = recording ? SDL_GetAudioRecordingDevices(&device_count) : SDL_GetAudioPlaybackDevices(&device_count);
	for (index = 0; devices && index < device_count && count < maximum; index++)
	{
		const char *name = SDL_GetAudioDeviceName(devices[index]);

		/* (a name a setting can hold, and a menu show: no "|", which
		separates a spinner's values) */
		if (!name || !name[0] || strchr(name, '|') || strlen(name) >= PLATFORM_AUDIO_DEVICE_NAME_SIZE)
			continue;
		snprintf(names[count++], PLATFORM_AUDIO_DEVICE_NAME_SIZE, "%s", name);
	}
	SDL_free(devices);
	return count;
}

SDL_AudioDeviceID platform_audio_device(int recording, const char *name)
{
	SDL_AudioDeviceID *devices;
	SDL_AudioDeviceID found = recording ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
	int device_count = 0, index;

	if (!name || !name[0] || !strcmp(name, "default"))
		return found;
	devices = recording ? SDL_GetAudioRecordingDevices(&device_count) : SDL_GetAudioPlaybackDevices(&device_count);
	for (index = 0; devices && index < device_count; index++)
	{
		const char *device_name = SDL_GetAudioDeviceName(devices[index]);

		if (device_name && !strcmp(device_name, name))
		{
			found = devices[index];
			break;
		}
	}
	SDL_free(devices);
	if (found == (recording ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK))
		platform_log("audio: no %s device named \"%s\": the system's default", recording ? "input" : "output", name);
	return found;
}
#else
int platform_audio_devices(int recording, char (*names)[PLATFORM_AUDIO_DEVICE_NAME_SIZE], int maximum)
{
	(void)recording;
	(void)names;
	(void)maximum;
	return 0;
}

SDL_AudioDeviceID platform_audio_device(int recording, const char *name)
{
	(void)name;
	return recording ? SDL_AUDIO_DEVICE_DEFAULT_RECORDING : SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
}
#endif

#ifndef HALO_ANDROID
/* the window's size (platform_window_size_setting), as the window was made
or last resized: platform_display_apply */
static long platform_window_width = -1, platform_window_height = -1;
/* the last configured mode; F11 can change the window independently */
static int platform_window_mode = _display_mode_windowed;

BOOL platform_video_hdr(float *sdr_white, float *headroom)
{
#ifdef HALO_GUEST
	/* (a guest's SDL bridges no window properties, guest_sdl.c; and no
	renderer of OpenH1's presents HDR yet) */
	*sdr_white = 1.0f;
	*headroom = 1.0f;
	return FALSE;
#else
	SDL_PropertiesID properties = platform_window ? SDL_GetWindowProperties(platform_window) : 0;
	BOOL enabled = properties && SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);

	*sdr_white = properties ? SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f) : 1.0f;
	*headroom = properties ? SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f) : 1.0f;
	if (!(*sdr_white > 0.0f))
		*sdr_white = 1.0f;
	if (!(*headroom >= 1.0f))
		*headroom = 1.0f;
	return enabled;
#endif
}
#endif

#ifdef HALO_WINDOWS
/* port/windows/host/host_windows.c: sleeps until a time of CLOCK_MONOTONIC,
on time (the frames' limit) */
void host_windows_sleep_until(unsigned long long when);
#endif

#ifdef HALO_METAL
/* port/macos/host_metal.m: the window drawn with Metal, from the main
thread; nonzero on success */
int host_metal_window_open(unsigned int window);
/* port/macos/host_metal.m: battery saver on or off (battery_saver.c), the
display link at a whole fraction of the display's rate (frame_pacing.h) */
void host_metal_battery_saver(int on);
/* port/macos/host_metal.m: display.vsync, the game paced by the display or
drawn as fast as display.max_fps lets it (frame_interval_ns) */
void host_metal_vsync(int on);
/* port/macos/host_metal.m: with vsync off, a start time for the game's
frames on CLOCK_MONOTONIC (0: none), and the display's refreshes' period
in nanoseconds (platform_frame_slot) */
unsigned long long host_metal_frame_phase(void);
unsigned int host_metal_frame_period(void);
unsigned int host_metal_frame_budget(void);
/* port/macos/host_metal.m: sleeps until a time of CLOCK_MONOTONIC, on time */
void host_metal_sleep_until(unsigned long long when);
/* the window's kind of drawing */
#define PLATFORM_WINDOW_DRAWING SDL_WINDOW_METAL
#elif defined(HALO_VULKAN)
/* port/linux/vulkan/host_vulkan.c: the window drawn with Vulkan by the
host, with the graphics' debugging settings (the host does not read the
settings file): debug, HOST_VULKAN_DEBUG_*; the folders debug.gpu_dump_shaders
and debug.texture_dump_directory name (empty: none); at a swap interval (vsync 1,
or 0), each frame shown for a whole number of refreshes while battery saver
is on (battery_saver.c, frame_pacing.h's frame_pacing_divisor at the host's
display's rate): asked again, either changed; nonzero on success */
int host_vulkan_window_open(unsigned int window, unsigned int debug, const char *shader_folder,
	const char *texture_folder, int interval, int battery_saver);
/* (host_vulkan.c's) debug.gpu_stats, debug.gpu_validation (or debug.gl_debug) */
#define HOST_VULKAN_DEBUG_STATISTICS 1
#define HOST_VULKAN_DEBUG_VALIDATION 2
/* display.low_latency, with the debugging settings: on, off (neither: auto);
and display.vrr's VRR on (platform_vrr: the frames bounded by the display,
not started for a refresh) */
#define HOST_VULKAN_LOW_LATENCY_ON 4
#define HOST_VULKAN_LOW_LATENCY_OFF 8
#define HOST_VULKAN_VRR 16
/* debug.vulkan_present_mode's, of these bits: immediate, mailbox, fifo,
fifo_relaxed (none: the swap interval's choice) */
#define HOST_VULKAN_PRESENT_MODE_SHIFT 5
#define HOST_VULKAN_PRESENT_MODE_MASK (7u << HOST_VULKAN_PRESENT_MODE_SHIFT)
/* port/linux/vulkan/host_vulkan.c: whether the system says VRR is active
(display.vrr's auto): HOST_VULKAN_VRR_NONE where it says nothing of it;
gamescope's word (the Steam Deck's, the Steam Frame's), or a Linux
display's DRM (a CRTC on with VRR_ENABLED), yes or no */
#define HOST_VULKAN_VRR_NONE 0
#define HOST_VULKAN_VRR_GAMESCOPE 1
#define HOST_VULKAN_VRR_DRM 2
#define HOST_VULKAN_VRR_GAMESCOPE_OFF 3
#define HOST_VULKAN_VRR_DRM_OFF 4
#ifndef HALO_ANDROID
int host_vulkan_vrr(void);
#endif
static BOOL platform_vrr(void);

static unsigned int platform_present_mode_flags(void)
{
	static const char *const names[] = { "immediate", "mailbox", "fifo", "fifo_relaxed" };
	const char *mode = config_string("debug.vulkan_present_mode");
	unsigned int index;

	if (!mode || !*mode)
		return 0;
	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		if (!strcmp(mode, names[index]))
			return (index + 1) << HOST_VULKAN_PRESENT_MODE_SHIFT;
	}
	platform_log("debug.vulkan_present_mode \"%s\" is none of immediate, mailbox, fifo, fifo_relaxed: "
		"the game's choice", mode);
	return 0;
}

static unsigned int platform_low_latency_flags(void)
{
	const char *mode = config_string("display.low_latency");

	if (platform_vrr())
		return HOST_VULKAN_VRR;
	if (mode && !strcmp(mode, "on"))
		return HOST_VULKAN_LOW_LATENCY_ON;
	if (mode && !strcmp(mode, "off"))
		return HOST_VULKAN_LOW_LATENCY_OFF;
	return 0;
}
#define PLATFORM_WINDOW_DRAWING SDL_WINDOW_VULKAN
#endif
/* (below) display.vsync's swap interval, with display.max_fps's in step;
the one the window was given (-1: none yet), which the frames' limit goes
by (frame_interval_ns) */
static int platform_swap_interval(void);
static int platform_interval_take(void);
static int platform_interval = -1;

#ifdef PLATFORM_MENU_CURSOR
/* ---------- the menus' cursor

The blue arrow the menus show for the mouse, in place of the system's. Halo
PC drew its cursor in the menus' 640 x 480, so it took the same part of the
screen at any resolution; this one is made again whenever the window's size
in pixels or display.menu_cursor changes (menu_cursor_fit, from
platform_pump_events). The guest reaches no SDL surfaces: the host makes the
cursor from the embedded picture (host_sdl_menu_cursor). */

/* the arrow's tip in the picture, past its soft edge: the click point */
#define MENU_CURSOR_TIP 2

/* port/linux/host/host_desktop.c: the window's cursor made from the PNG,
height pixels tall (where the desktop counts in points, that size in points
with the pixels' image as its alternate), its click point at (tip, tip) of
the picture's pixels; the system's for a height of 0. Nonzero on success */
int host_sdl_menu_cursor(unsigned int window, const void *png, unsigned int size, int height, int tip);

/* the cursor's height made last, in pixels (0: the system's; -1: none made
for this window yet) */
static int menu_cursor_height = -1;

/* display.menu_cursor: the arrow's height in the menus' 480 lines (medium:
54 pixels at 1080p), or 0 for the system's pointer; read again when a
setting changes */
static int menu_cursor_size(void)
{
	static const struct { const char *name; int lines; } sizes[] =
	{
		{ "off", 0 }, { "small", 18 }, { "medium", 24 }, { "large", 32 }, { "extra_large", 40 }
	};
	static unsigned long read_at = (unsigned long)-1;
	static int lines = 24;

	if (read_at != config_changes())
	{
		const char *name = config_string("display.menu_cursor");
		size_t index;

		read_at = config_changes();
		lines = 24;
		for (index = 0; index < sizeof(sizes) / sizeof(sizes[0]); index++)
			if (!strcmp(name, sizes[index].name))
				lines = sizes[index].lines;
	}
	return lines;
}

/* the cursor made for the window's size and display.menu_cursor, when one
has changed: its height the part of the window the menus' 480 lines take
(their 4:3 fits a window of another shape), in pixels. Off, the system's
pointer again */
static void menu_cursor_fit(void)
{
	int width = 0, height = 0, lines, cursor_height = 0, size = menu_cursor_size();

	if (!platform_menu_cursor_size || !SDL_GetWindowSizeInPixels(platform_window, &width, &height) ||
		width <= 0 || height <= 0)
		return;
	if (size)
	{
		lines = height < width * 3 / 4 ? height : width * 3 / 4;
		cursor_height = lines * size / 480;
		if (cursor_height < 16)
			cursor_height = 16;
	}
	if (cursor_height == menu_cursor_height)
		return;
	menu_cursor_height = cursor_height;
	if (!host_sdl_menu_cursor((unsigned int)(uintptr_t)platform_window, platform_menu_cursor,
		(unsigned int)platform_menu_cursor_size, cursor_height, MENU_CURSOR_TIP))
		platform_log("cannot set the menus' cursor");
}

/* A controller's buttons or sticks, used while a menu is up, hide the
cursor, and the mouse moved or clicked shows it again, so a player on a
controller has no arrow left over the menus; a menu opened while the
controller was used last opens without it, and play shows it again as it
takes the mouse (platform_ui_pointer_set_active), where the relative mode
hides it (or macOS parks it in fullscreen: port/macos/host_raw_mouse.m).
Only these hide it, so its state is kept here, not asked of the host at
each event. */
static BOOL menu_cursor_controller;
static BOOL menu_cursor_hidden;

static void menu_cursor_show(BOOL show)
{
	if (show == !menu_cursor_hidden)
		return;
	menu_cursor_hidden = !show;
	if (show)
		SDL_ShowCursor();
	else
		SDL_HideCursor();
}

static void menu_cursor_controller_used(BOOL in_menus)
{
	menu_cursor_controller = TRUE;
	if (in_menus)
		menu_cursor_show(FALSE);
}

static void menu_cursor_mouse_used(BOOL in_menus)
{
	menu_cursor_controller = FALSE;
	if (in_menus)
		menu_cursor_show(TRUE);
}
#endif

BOOL platform_video_initialize(unsigned long width, unsigned long height)
{
	if (platform_window)
		return TRUE;
	platform_video_tried = TRUE;
	if (!platform_sdl_initialize())
		return FALSE;
#ifdef HALO_ANDROID
	{
		long pixel_width = 0, pixel_height = 0;

		(void)width;
		(void)height;
		platform_screen_mode(&pixel_width, &pixel_height);
		platform_window = SDL_CreateWindow("GOAT H1", (int)pixel_width, (int)pixel_height,
			PLATFORM_WINDOW_DRAWING | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	}
#else
	/* fullscreen (either kind) unless display.mode is the window, which F11
	switches to and from: display.window_size, whatever shape the fullscreen
	picture has. The game draws at the size platform_screen_mode gives
	(d3d8_device.c). */
	(void)width;
	(void)height;
	platform_window_size_setting(&platform_window_width, &platform_window_height);
#ifdef HALO_VR
	/* with an OpenXR runtime that takes the host's Vulkan device
	(host_vr_probe, logging why where there is none: the game then plays
	flat, in its window), a window that stays hidden: the headset shows the
	game (vr.c) */
	if (halo_vr_enabled() && host_vr_probe() != VR_BINDING_NONE)
	{
		platform_window = SDL_CreateWindow("GOAT H1", (int)platform_window_width, (int)platform_window_height,
			PLATFORM_WINDOW_DRAWING | SDL_WINDOW_HIDDEN);
	}
	else
#endif
	platform_window = SDL_CreateWindow("GOAT H1", (int)platform_window_width, (int)platform_window_height,
		PLATFORM_WINDOW_DRAWING | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
		(config_boolean("debug.hidden_window") ? SDL_WINDOW_HIDDEN : 0) |
		(platform_fullscreen_setting() ? SDL_WINDOW_FULLSCREEN : 0));
#ifdef HALO_VULKAN
	/* (a video driver that draws no window with Vulkan, as SDL's offscreen
	one where the device has no headless surfaces: a window the host draws
	without showing) */
	if (!platform_window)
	{
		platform_log("SDL_CreateWindow with Vulkan failed (%s): the frames are drawn and not shown", SDL_GetError());
		platform_window = SDL_CreateWindow("GOAT H1", (int)platform_window_width, (int)platform_window_height,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
			(config_boolean("debug.hidden_window") ? SDL_WINDOW_HIDDEN : 0) |
			(platform_fullscreen_setting() ? SDL_WINDOW_FULLSCREEN : 0));
	}
#endif
#endif
	if (!platform_window)
	{
		platform_log("SDL_CreateWindow failed: %s", SDL_GetError());
		return FALSE;
	}
#ifndef HALO_ANDROID
	/* the game's icon, which the desktop shows for the window (on Windows
	also halo.exe's own, port/windows/host/halo.rc; not a guest's, which
	reaches no SDL surfaces) */
#ifndef HALO_GUEST
	if (platform_window_icon_size)
	{
		SDL_Surface *icon = SDL_LoadPNG_IO(SDL_IOFromConstMem(platform_window_icon, platform_window_icon_size), true);

		if (!icon || !SDL_SetWindowIcon(platform_window, icon))
			platform_log("cannot set the window's icon: %s", SDL_GetError());
		SDL_DestroySurface(icon);
	}
#endif
#ifdef PLATFORM_MENU_CURSOR
	/* the pointer the menus show (platform_ui_pointer_set_active), in place
	of the system's, at the window's size */
	menu_cursor_height = -1;
	menu_cursor_fit();
#endif
	platform_fullscreen_requested = platform_fullscreen_setting();
	platform_window_mode = platform_display_mode();
	platform_fullscreen_kind_apply();
#endif
#ifdef HALO_METAL
	if (!host_metal_window_open((unsigned int)(uintptr_t)platform_window))
	{
		platform_log("cannot draw the window with Metal");
		return FALSE;
	}
	/* (display.vsync and battery saver from the first frame: the window
	opens paced by the display, and the Vulkan host takes them as it opens) */
	platform_frame_rate_apply();
#else
	if (!host_vulkan_window_open((unsigned int)(uintptr_t)platform_window,
		(config_boolean("debug.gpu_stats") ? HOST_VULKAN_DEBUG_STATISTICS : 0) |
		(config_boolean("debug.gpu_validation") || config_boolean("debug.gl_debug") ? HOST_VULKAN_DEBUG_VALIDATION : 0) |
		platform_low_latency_flags() | platform_present_mode_flags(), config_string("debug.gpu_dump_shaders"),
		config_string("debug.texture_dump_directory"),
		platform_interval_take(), battery_saver_active()))
	{
		platform_log("cannot draw the window with Vulkan");
		/* (said where it can be seen, not only in the log: else the game
		runs on with nothing on the screen; a box cannot show above a
		fullscreen window) */
#ifndef HALO_ANDROID
		if (platform_window_fullscreen())
			platform_window_set_fullscreen(FALSE);
#endif
		if (!config_boolean("debug.hidden_window"))
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo",
				"The game cannot draw with Vulkan on this device. It needs a GPU and driver with Vulkan 1.3 "
#ifdef HALO_ANDROID
				"(most phones from about 2021 on, with their system updates)."
#else
				"(dynamic rendering, timeline semaphores): updating the graphics driver may give it."
#endif
				, NULL);
		return FALSE;
	}
#endif
	/* (the events are read on this thread, the main one: on macOS also
	the window's own, without which it never comes on screen) */
	platform_event_thread = SDL_GetCurrentThreadID();
	platform_mouse_capture(TRUE);
	return TRUE;
}

#ifdef HALO_VR
/* the VR build's window, made hidden for the headset (platform_video_
initialize), shown as a build without VR shows it when there is no VR
session after all (vr_gl_initialize): the game then plays flat in it */
void platform_video_vr_unavailable(void)
{
	if (!platform_window || !(SDL_GetWindowFlags(platform_window) & SDL_WINDOW_HIDDEN) ||
		config_boolean("debug.hidden_window"))
	{
		return;
	}
	SDL_SetWindowResizable(platform_window, true);
	SDL_ShowWindow(platform_window);
	platform_display_apply();
}
#endif

/* display.mode, display.resolution (fullscreen's display mode),
display.window_size (when it changes: the window can be resized) and
display.vsync, as Settings has written them; display.resolution_scaling
and borderless's resolution are taken up between frames
(halo_screen_commit) */
void platform_display_apply(void)
{
#ifndef HALO_ANDROID
	int mode = platform_display_mode();
	long width, height;

	if (!platform_window)
		return;
	platform_fullscreen_kind_apply();
	/* Saving unrelated settings must preserve the window mode chosen by F11. */
	if (mode != platform_window_mode)
	{
		platform_window_mode = mode;
		platform_window_set_fullscreen(platform_fullscreen_setting());
	}
	platform_window_size_setting(&width, &height);
	if (width != platform_window_width || height != platform_window_height)
	{
		platform_window_width = width;
		platform_window_height = height;
		SDL_SetWindowSize(platform_window, (int)width, (int)height);
	}
#else
	if (!platform_window)
		return;
#endif
	platform_frame_rate_apply();
}

/* the window's display's refresh rate (frames a second), 0 if unknown (on
Android, whose host knows it: frame_pacing_divisor takes it as 60) */
static float platform_refresh_rate(void)
{
#ifdef HALO_ANDROID
	return 0.0f;
#else
	SDL_DisplayID display = platform_window ? SDL_GetDisplayForWindow(platform_window) : 0;
	const SDL_DisplayMode *mode = display ? SDL_GetCurrentDisplayMode(display) : NULL;

	return mode && mode->refresh_rate > 0.0f ? mode->refresh_rate : 0.0f;
#endif
}

/* ---------- display.vrr

A display of variable refresh (G-SYNC, FreeSync, VRR) shows each frame as
it comes, while the frames come within its range. The game with VRR on
presents with vsync (FIFO, which waits for the display while the frames
come faster than its range allows, and never tears) and holds its frames to
display.vrr_cap_margin a second under the refresh rate (by default 3: 117
at 120 Hz, 141 at 144),
or display.max_fps where that is lower: so they stay inside the range and
the display refreshes when each is ready, without vsync's queue. The frames
are bounded by the display (at most one waits for it), not started just in
time for a refresh, which a variable display does not have
(HOST_VULKAN_VRR).

"on" and "off" are the player's; "auto" turns it on only where the system
says VRR is active (host_vulkan_vrr: gamescope's word, or a Linux display's
DRM), asked again every VRR_ASK_MS, so that a display whose compositor
turns VRR on once the game is fullscreen is followed. Windows says nothing
reliable of it: there auto is off, and the player turns it on in Settings >
Video. Not while battery saver is on, whose frames are a whole number of
refreshes each (battery_saver.c). The choice and its reason are logged as
they change. The Mac (Metal) and Android have none of it. */

#define VRR_ASK_MS 2000u

/* display.vrr_cap_margin: the frames a second under the refresh rate VRR
holds the game to, 1 to 10 */
static float platform_vrr_margin(void)
{
	long margin = config_integer("display.vrr_cap_margin");

	return (float)(margin < 1 ? 1 : margin > 10 ? 10 : margin);
}

/* (platform_frame_rate_apply's: the VRR the window was given, -1 none) */
static int platform_vrr_applied = -1;

static BOOL platform_vrr(void)
{
#if defined(HALO_VULKAN) && !defined(HALO_ANDROID)
	static unsigned long read_at = (unsigned long)-1;
	static Uint64 asked_at;
	static int on = -1;
	static char reason[160];
	static float logged_margin;
	/* (display.vrr is "auto": asked again every VRR_ASK_MS) */
	static int automatic;
	char why[160];
	const char *mode;
	int now_on, detected;
	Uint64 now = SDL_GetTicks();

	if (on >= 0 && read_at == config_changes() && (!automatic || now - asked_at < VRR_ASK_MS))
		return on;
	mode = config_string("display.vrr");
	automatic = !strcmp(mode, "auto");
	read_at = config_changes();
	asked_at = now;
	if (!strcmp(mode, "on"))
	{
		now_on = 1;
		snprintf(why, sizeof(why), "display.vrr is on");
	}
	else if (!strcmp(mode, "off"))
	{
		now_on = 0;
		snprintf(why, sizeof(why), "display.vrr is off");
	}
	else
	{
		detected = host_vulkan_vrr();
		now_on = detected == HOST_VULKAN_VRR_GAMESCOPE || detected == HOST_VULKAN_VRR_DRM;
		snprintf(why, sizeof(why), "auto: %s", detected == HOST_VULKAN_VRR_GAMESCOPE ? "gamescope says VRR is active" :
			detected == HOST_VULKAN_VRR_DRM ? "the display's DRM output has VRR enabled" :
			detected == HOST_VULKAN_VRR_GAMESCOPE_OFF ? "gamescope says VRR is not active" :
			detected == HOST_VULKAN_VRR_DRM_OFF ? "no display's DRM output has VRR enabled" :
			"the system says nothing of VRR (display.vrr = \"on\" for a G-SYNC or FreeSync display)");
	}
	if (now_on && battery_saver_active())
	{
		now_on = 0;
		snprintf(why, sizeof(why), "battery saver is on");
	}
	if (now_on != on || strcmp(why, reason) || (now_on && platform_vrr_margin() != logged_margin))
	{
		float refresh = platform_refresh_rate();

		if (now_on && refresh > platform_vrr_margin())
			platform_log("display: VRR on (%s): vsync on, at most %.2f frames a second, display.vrr_cap_margin %.0f "
				"under the display's %.2f Hz", why, (double)(refresh - platform_vrr_margin()),
				(double)platform_vrr_margin(), (double)refresh);
		else if (now_on)
			platform_log("display: VRR on (%s): vsync on, the display's refresh rate not known yet", why);
		else
			platform_log("display: VRR off (%s)", why);
		snprintf(reason, sizeof(reason), "%s", why);
		logged_margin = platform_vrr_margin();
	}
	on = now_on;
	return on;
#else
	return FALSE;
#endif
}

int platform_vrr_detected(void)
{
#if defined(HALO_METAL)
	return PLATFORM_VRR_SYSTEM;
#elif defined(HALO_ANDROID) || !defined(HALO_VULKAN)
	return PLATFORM_VRR_NOT_HERE;
#elif defined(HALO_WINDOWS)
	return PLATFORM_VRR_UNKNOWN;
#else
	/* (asked at most every VRR_ASK_MS: the menus ask each frame) */
	static Uint64 asked_at;
	static int found = -1;
	Uint64 now = SDL_GetTicks();
	int detected;

	if (found < 0 || now - asked_at >= VRR_ASK_MS)
	{
		detected = host_vulkan_vrr();
		found = detected == HOST_VULKAN_VRR_GAMESCOPE || detected == HOST_VULKAN_VRR_DRM;
		asked_at = now;
	}
	return found ? PLATFORM_VRR_FOUND : PLATFORM_VRR_NONE;
#endif
}

/* the refreshes each frame is shown for, 0 for none (vsync off):
display.vsync's 1; and on the Linux builds, with vsync off, a display.max_fps
of the display's refresh rate or a whole fraction of it (60 at 60 Hz, 30 or
45 at 90), within a frame a second, so many: frames spaced by the clock at
the display's own rate drift against its refreshes, and now and then one is
shown twice and the next not at all (judder, while the frame rate counter
says 60). In step with the display, display.low_latency starts each in time
for its refresh, for less input lag than the clock's. (Not Windows', not
measured there in step with its display, nor the Mac's, whose frames
with vsync off are taken at the display link's refreshes already.) */
static int platform_swap_interval(void)
{
#if defined(HALO_VULKAN) && !defined(HALO_ANDROID) && !defined(HALO_WINDOWS)
	long maximum;
	float refresh;
	int interval;
#endif

	if (config_boolean("display.vsync") || platform_vrr())
		return 1;
#if defined(HALO_VULKAN) && !defined(HALO_ANDROID) && !defined(HALO_WINDOWS)
	maximum = config_integer("display.max_fps");
	refresh = platform_refresh_rate();
	if (maximum > 0 && refresh > 0.0f)
	{
		for (interval = 1; interval <= 4; interval++)
		{
			float apart = refresh / (float)interval - (float)maximum;

			if (apart >= -1.0f && apart <= 1.0f)
				return interval;
		}
	}
#endif
	return 0;
}

/* platform_swap_interval's, which the window is given (platform_interval),
noted in the log as display.max_fps comes in step with the display */
static int platform_interval_take(void)
{
	int interval = platform_swap_interval();

	if (interval != platform_interval && interval > 0 && !config_boolean("display.vsync"))
	{
		platform_log("frame rate: display.max_fps %ld shown in step with the display's %.2f Hz, each frame for %d "
			"refresh%s", config_integer("display.max_fps"), platform_refresh_rate(), interval, interval == 1 ? "" : "es");
	}
	platform_interval = interval;
	return interval;
}

/* whether the display paces the frames: display.vsync, and on the Mac
battery saver too, whose display link then runs at a whole fraction of the
display's rate (frame_pacing.h). Frames spaced by the clock alone come at
the link's rate but not in step with it: on a 60 Hz M2, 15% of the
refreshes showed no new frame. */
static BOOL platform_vsync(void)
{
#ifdef HALO_METAL
	if (battery_saver_active())
		return TRUE;
#endif
	return (platform_interval >= 0 ? platform_interval : platform_swap_interval()) > 0;
}

/* display.vsync's swap interval, with battery saver's frames a second
(battery_saver.c): the Vulkan host shows each frame for a whole number of
refreshes, and the Metal host's display link runs at that rate */
void platform_frame_rate_apply(void)
{
	int saving = battery_saver_active();
	int interval;

	if (!platform_window)
		return;
	interval = platform_interval_take();
	platform_vrr_applied = platform_vrr();
#if defined(HALO_METAL)
	host_metal_vsync(platform_vsync());
	host_metal_battery_saver(saving);
#elif defined(HALO_VULKAN)
	/* (the window open already: only its swap interval and
	display.low_latency are taken) */
	host_vulkan_window_open((unsigned int)(uintptr_t)platform_window, platform_low_latency_flags(), "", "",
		interval, saving);
#endif
}

void platform_video_drawable_size(int *width, int *height)
{
	SDL_GetWindowSizeInPixels(platform_window, width, height);
}

/* with vsync off, the time between frames display.max_fps asks for (0:
twice the display's refresh rate), or 0 for no limit. A GPU never left idle
can hang (Intel's Raptor Lake graphics, whose reset then takes the desktop
with it); the limit gives it a rest every frame. And battery saver's
(battery_saver.c), where the display does not space its frames: without
vsync. (Android has no
display.max_fps.) */
static Uint64 frame_interval_ns(void)
{
	static int vsync;
	static long maximum;
	static unsigned long read_at = (unsigned long)-1;
	float rate = 0.0f;

	/* (battery saver turning on or off moves config_changes; the swap
	interval the window has, platform_frame_rate_apply's) */
	if (read_at != config_changes())
	{
		read_at = config_changes();
#ifdef HALO_ANDROID
		maximum = -1;
#else
		maximum = config_integer("display.max_fps");
#endif
	}
	/* (a display.max_fps in step with the display's refresh rate, or no
	longer: the rate known only once the window is shown (gamescope's), the
	window on another display, the display's mode changed) */
	if (maximum > 0 && platform_interval >= 0 && !config_boolean("display.vsync") &&
		platform_swap_interval() != platform_interval)
	{
		platform_frame_rate_apply();
	}
	/* (VRR turned on or off: auto's system, or battery saver) */
	if (platform_interval >= 0 && platform_vrr() != platform_vrr_applied)
		platform_frame_rate_apply();
	vsync = platform_vsync();
	if (platform_vrr())
	{
		/* (VRR's ceiling, or display.max_fps under it) */
		float refresh = platform_refresh_rate();

		rate = refresh > platform_vrr_margin() ? refresh - platform_vrr_margin() : 0.0f;
		if (maximum > 0 && (!rate || (float)maximum < rate))
			rate = (float)maximum;
	}
	else if (!vsync && maximum >= 0)
		rate = maximum ? (float)maximum : 2.0f * (platform_refresh_rate() > 0.0f ? platform_refresh_rate() : 60.0f);
	if (battery_saver_active() && !vsync)
	{
		float refresh = platform_refresh_rate() > 0.0f ? platform_refresh_rate() : 60.0f;
		float saving = refresh / (float)frame_pacing_divisor(refresh, BATTERY_SAVER_LEAST, BATTERY_SAVER_MOST);

		if (!rate || saving < rate)
			rate = saving;
	}
	return rate > 0.0f ? (Uint64)(1e9f / rate) : 0;
}

#ifdef HALO_METAL
/* (vsync off on the Mac) the next frame's start, as SDL_GetTicksNS counts,
on the display's grid (host_metal_frame_phase): as many times a refresh as
the interval allows, the refresh's shown one its budget before the
refresh's take, the others a whole step after it, but none before the take
(a frame started there could be ready for the take and shown instead: it
starts at the take). The first time half a step after the last frame's
start (last); one already passed is skipped but for a shown one, which
starts at once (else the refresh would show the frame before it, a step
older), and after a frame over a refresh late the grid is joined again
from now. 0 where the host gives no grid. */
static Uint64 platform_frame_slot(Uint64 interval, Uint64 now, Uint64 last)
{
	unsigned long long phase = host_metal_frame_phase();
	Uint64 period = host_metal_frame_period(), budget = host_metal_frame_budget(), count, index, step;
	Sint64 anchor, earliest, refresh, slot;
	struct timespec monotonic;

	if (!phase || !period || !interval)
		return 0;
	/* (a period a little under two intervals is two) */
	count = (period + interval / 16u) / interval;
	if (!count)
		count = 1;
	step = period / count;
	clock_gettime(CLOCK_MONOTONIC, &monotonic);
	anchor = (Sint64)phase - ((Sint64)monotonic.tv_sec * 1000000000 + monotonic.tv_nsec) + (Sint64)now;
	earliest = last ? (Sint64)(last + step / 2u) : (Sint64)now - (Sint64)(step / 4u);
	if (earliest < (Sint64)now - (Sint64)period)
		earliest = (Sint64)now - (Sint64)(step / 4u);
	/* (the refresh whose times may hold the first: one before earliest's) */
	refresh = (earliest - anchor) / (Sint64)period - 1;
	if (earliest < anchor)
		refresh--;
	for (;; refresh++)
	{
		for (index = 0; index < count; index++)
		{
			/* (none but the shown one where the budget leaves no room after
			the take) */
			if (index && budget + step / 2u > period)
				break;
			slot = anchor + refresh * (Sint64)period + (Sint64)(index * step);
			if (index && slot < anchor + refresh * (Sint64)period + (Sint64)budget)
				slot = anchor + refresh * (Sint64)period + (Sint64)budget;
			if (slot < earliest)
				continue;
			if (slot >= (Sint64)now || index == 0)
				return (Uint64)slot;
		}
	}
}
#endif

/* The frames' limit (display.max_fps, battery saver's without vsync): no
frame starts before its time, whatever comes meanwhile. The wait is a plain
sleep, not one for the window's events: the keys, the mouse, the window's
own events and the controllers' polling neither end it nor add a frame, so
the frame rate holds at the limit while the player plays (a press that
began the next frame at once had added a frame each interval, up to twice
the limit while keys were pressed, #543). The input is read as late as the
limit lets it be: the wait comes after a frame has gone to the renderer and
before the next one reads anything (D3DDevice_Present pumps the window's
events once it returns, and the main loop reads them at once:
frame_pacing.h), so a press during the wait is in the very next frame, read
at its start, and the view reads the mouse again after the simulation
(xinput_sdl.c's mouse look). */
void platform_video_swap(void)
{
	Uint64 swap_start = SDL_GetTicks(), swap_time;
	static Uint64 next_frame;
#ifdef HALO_METAL
	/* the last frame's start on the display's grid (platform_frame_slot) */
	static Uint64 slot_last;
	Uint64 slot;
#endif
	Uint64 interval, now;
	/* (the host shows the frame, on its render thread: here it is paced) */
	/* (a frame the graphics driver held for seconds: the game froze, and
	online, a host stops hearing a machine after a few; the log says where) */
	swap_time = SDL_GetTicks() - swap_start;
	if (swap_time >= 2000)
		platform_log("display: showing a frame took %.1f s in the graphics driver", (double)swap_time / 1000.0);
	interval = frame_interval_ns();
	if (!interval)
		return;
	now = SDL_GetTicksNS();
#ifdef HALO_METAL
	slot = platform_frame_slot(interval, now, slot_last);
	if (slot)
	{
		/* (the host's sleep, on time: the guest's own timed waits ended up
		to 8 ms late) */
		if (slot > now)
		{
			struct timespec monotonic;

			clock_gettime(CLOCK_MONOTONIC, &monotonic);
			host_metal_sleep_until((unsigned long long)monotonic.tv_sec * 1000000000ull +
				(unsigned long long)monotonic.tv_nsec + (slot - now));
		}
		slot_last = slot;
		return;
	}
#endif
	if (next_frame > now)
	{
#ifdef HALO_ANDROID
		/* (the Android guest's SDL waits in milliseconds) */
		SDL_Delay((Uint32)((next_frame - now) / 1000000u));
#elif defined(HALO_WINDOWS)
		/* (the host's wait, on time: Windows's timer wakes up to a
		millisecond late; guest_windows.c) */
		struct timespec monotonic;

		clock_gettime(CLOCK_MONOTONIC, &monotonic);
		host_windows_sleep_until((unsigned long long)monotonic.tv_sec * 1000000000ull +
			(unsigned long long)monotonic.tv_nsec + (next_frame - now));
#else
		SDL_DelayPrecise(next_frame - now);
#endif
		now = next_frame;
	}
	/* (a frame more than an interval late starts the count again) */
	next_frame = now - next_frame > interval ? now + interval : next_frame + interval;
}

#if !defined(HALO_ANDROID) && !defined(HALO_GUEST)
#define PLATFORM_MOUSE_WARP
#endif
#ifdef PLATFORM_MOUSE_WARP
/* when SDL_SetWindowRelativeMouseMode has no XInput2 (32-bit libSDL3 on X11
without 32-bit libXi.so.6), the mouse is held with a hidden cursor, a window
grab and a warp back to the middle after each move */
static BOOL mouse_warp_capture;
static int mouse_warp_x;
static int mouse_warp_y;
#endif

void platform_mouse_capture(BOOL capture)
{
	if (!platform_window)
		return;
#ifndef PLATFORM_MOUSE_WARP
	/* (and a 64-bit guest's, whose SDL is the system's 64-bit one, with
	XInput2: guest_sdl.c bridges no grab, cursor or focus calls) */
	SDL_SetWindowRelativeMouseMode(platform_window, capture ? true : false);
#else
	/* (without the focus, the mouse is taken when the focus comes back:
	SDL_EVENT_WINDOW_FOCUS_GAINED. SDL_SetWindowRelativeMouseMode would only
	set SDL_WINDOW_MOUSE_RELATIVE_MODE then, without testing XInput2, and
	later calls would find it set and return at once) */
	if (!capture || SDL_GetKeyboardFocus() != platform_window)
	{
		mouse_warp_capture = FALSE;
		SDL_SetWindowRelativeMouseMode(platform_window, false);
		SDL_SetWindowMouseGrab(platform_window, false);
		SDL_ShowCursor();
		return;
	}
	if (SDL_SetWindowRelativeMouseMode(platform_window, true))
	{
		if (mouse_warp_capture)
		{
			mouse_warp_capture = FALSE;
			SDL_SetWindowMouseGrab(platform_window, false);
			SDL_ShowCursor();
		}
		return;
	}
	mouse_warp_capture = TRUE;
	SDL_HideCursor();
	SDL_SetWindowMouseGrab(platform_window, true);
	{
		int width = 0, height = 0;

		SDL_GetWindowSize(platform_window, &width, &height);
		if (width > 0 && height > 0)
		{
			mouse_warp_x = width / 2;
			mouse_warp_y = height / 2;
			SDL_WarpMouseInWindow(platform_window, (float)mouse_warp_x, (float)mouse_warp_y);
		}
	}
#endif
}

/* ---------- keyboard translation */

/* Windows virtual key code for an SDL scancode (the Xbox debug keyboard
reports virtual keys) */
static BYTE virtual_key_from_scancode(SDL_Scancode scancode)
{
	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
		return (BYTE)('A' + (scancode - SDL_SCANCODE_A));
	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
		return (BYTE)('1' + (scancode - SDL_SCANCODE_1));
	if (scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12)
		return (BYTE)(0x70 + (scancode - SDL_SCANCODE_F1));
	if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_9)
		return (BYTE)(0x61 + (scancode - SDL_SCANCODE_KP_1));
	switch (scancode)
	{
	case SDL_SCANCODE_0: return '0';
	case SDL_SCANCODE_KP_0: return 0x60;
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
	case SDL_SCANCODE_ESCAPE: return 0x1b;
	case SDL_SCANCODE_BACKSPACE: return 0x08;
	case SDL_SCANCODE_TAB: return 0x09;
	case SDL_SCANCODE_SPACE: return 0x20;
	case SDL_SCANCODE_MINUS: return 0xbd;
	case SDL_SCANCODE_EQUALS: return 0xbb;
	case SDL_SCANCODE_LEFTBRACKET: return 0xdb;
	case SDL_SCANCODE_RIGHTBRACKET: return 0xdd;
	case SDL_SCANCODE_BACKSLASH: return 0xdc;
	case SDL_SCANCODE_SEMICOLON: return 0xba;
	case SDL_SCANCODE_APOSTROPHE: return 0xde;
	case SDL_SCANCODE_GRAVE: return 0xc0;
	case SDL_SCANCODE_COMMA: return 0xbc;
	case SDL_SCANCODE_PERIOD: return 0xbe;
	case SDL_SCANCODE_SLASH: return 0xbf;
	case SDL_SCANCODE_CAPSLOCK: return 0x14;
	case SDL_SCANCODE_PRINTSCREEN: return 0x2c;
	case SDL_SCANCODE_SCROLLLOCK: return 0x91;
	case SDL_SCANCODE_PAUSE: return 0x13;
	case SDL_SCANCODE_INSERT: return 0x2d;
	case SDL_SCANCODE_HOME: return 0x24;
	case SDL_SCANCODE_PAGEUP: return 0x21;
	case SDL_SCANCODE_DELETE: return 0x2e;
	case SDL_SCANCODE_END: return 0x23;
	case SDL_SCANCODE_PAGEDOWN: return 0x22;
	case SDL_SCANCODE_RIGHT: return 0x27;
	case SDL_SCANCODE_LEFT: return 0x25;
	case SDL_SCANCODE_DOWN: return 0x28;
	case SDL_SCANCODE_UP: return 0x26;
	case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
	case SDL_SCANCODE_KP_DIVIDE: return 0x6f;
	case SDL_SCANCODE_KP_MULTIPLY: return 0x6a;
	case SDL_SCANCODE_KP_MINUS: return 0x6d;
	case SDL_SCANCODE_KP_PLUS: return 0x6b;
	case SDL_SCANCODE_KP_PERIOD: return 0x6e;
	case SDL_SCANCODE_LCTRL: return 0xa2;
	case SDL_SCANCODE_RCTRL: return 0xa3;
	case SDL_SCANCODE_LSHIFT: return 0xa0;
	case SDL_SCANCODE_RSHIFT: return 0xa1;
	case SDL_SCANCODE_LALT: return 0xa4;
	case SDL_SCANCODE_RALT: return 0xa5;
	default: return 0;
	}
}

/* a key's character as the keyboard's layout types it (typed: its key with
the modifiers, SDL_GetKeyFromScancode's), where that is ASCII: Shift and
the + key give ? on a Nordic layout, AltGr and 2 give @ (#726). Else, and
for the keys that type no character (Return, Backspace, Tab, Escape), the
key event's own, shifted as a US keyboard's */
static CHAR ascii_from_key(SDL_Keycode key, SDL_Keycode typed, SDL_Keymod modifiers)
{
	BOOL shift = (modifiers & SDL_KMOD_SHIFT) != 0;
	static const char shifted_digits[] = ")!@#$%^&*(";

	if (typed >= ' ' && typed < 0x7f)
		return (CHAR)typed;
	if (key >= 'a' && key <= 'z')
		return (CHAR)((shift ^ ((modifiers & SDL_KMOD_CAPS) != 0)) ? key - 32 : key);
	if (key >= '0' && key <= '9')
		return (CHAR)(shift ? shifted_digits[key - '0'] : key);
	if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
		return '\r';
	if (key == SDLK_BACKSPACE)
		return '\b';
	if (key == SDLK_TAB)
		return '\t';
	if (key == SDLK_ESCAPE)
		return 0x1b;
	if (key >= 32 && key < 127)
	{
		if (!shift)
			return (CHAR)key;
		switch (key)
		{
		case '-': return '_';
		case '=': return '+';
		case '[': return '{';
		case ']': return '}';
		case '\\': return '|';
		case ';': return ':';
		case '\'': return '"';
		case ',': return '<';
		case '.': return '>';
		case '/': return '?';
		case '`': return '~';
		default: return (CHAR)key;
		}
	}
	return 0;
}

static void queue_keystroke(const SDL_KeyboardEvent *event)
{
	struct platform_keystroke *keystroke;
	BYTE flags = 0;

	if (keystroke_count == KEYSTROKE_QUEUE_SIZE)
	{
		keystroke_head = (keystroke_head + 1) % KEYSTROKE_QUEUE_SIZE;
		keystroke_count--;
	}
	keystroke = &keystroke_queue[(keystroke_head + keystroke_count) % KEYSTROKE_QUEUE_SIZE];
	if (event->mod & SDL_KMOD_CTRL) flags |= 0x01;
	if (event->mod & SDL_KMOD_SHIFT) flags |= 0x02;
	if (event->mod & SDL_KMOD_ALT) flags |= 0x04;
	if (event->mod & SDL_KMOD_CAPS) flags |= 0x08;
	if (event->mod & SDL_KMOD_NUM) flags |= 0x10;
	if (!event->down) flags |= 0x40;
	if (event->repeat) flags |= 0x80;
	keystroke->virtual_key = virtual_key_from_scancode(event->scancode);
	keystroke->ascii = event->down ? ascii_from_key(event->key,
		SDL_GetKeyFromScancode(event->scancode, event->mod, false), event->mod) : 0;
	keystroke->flags = flags;
	keystroke_count++;
}

/* (input_lock) a key pressed and let go of, as its events queue it */
static void queue_test_keystroke(BYTE virtual_key, CHAR ascii)
{
	int up;

	for (up = 0; up < 2; up++)
	{
		struct platform_keystroke *keystroke;

		if (keystroke_count == KEYSTROKE_QUEUE_SIZE)
			return;
		keystroke = &keystroke_queue[(keystroke_head + keystroke_count) % KEYSTROKE_QUEUE_SIZE];
		keystroke->virtual_key = virtual_key;
		keystroke->ascii = up ? 0 : ascii;
		keystroke->flags = up ? 0x40 : 0;
		keystroke_count++;
	}
}

/* (input_lock) Return held by test_menu_keys, and the mouse buttons held by
the scripted player's mouse (halo_test_input_pump, a bit each), which no
events hold (input_held_reconcile leaves them) */
static int test_enter_held;
static unsigned int test_buttons_held;

/* (input_lock) debug.test_input "menu:...": its "type:" and "enter"
(platform_test_menu_keys), as the keyboard would type them: the letters'
and digits' keystrokes, and Return held and its keystroke */
static void test_menu_keys(void)
{
	char typed[64];
	int enter, index;

	if (platform_test_menu_keys(typed, (int)sizeof(typed), &enter))
	{
		for (index = 0; typed[index]; index++)
		{
			char character = typed[index];

			if (character >= 'a' && character <= 'z')
				queue_test_keystroke((BYTE)(character - 'a' + 'A'), character);
			else if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'))
				queue_test_keystroke((BYTE)character, character);
		}
		if (enter)
			queue_test_keystroke(0x0d, '\r');
	}
	if (enter != test_enter_held)
	{
		test_enter_held = enter;
		input_state.keys[SDL_SCANCODE_RETURN] = (unsigned char)enter;
	}
}

#ifndef HALO_ANDROID
/* the scripted run's mouse (test_menu_mouse): its events' mouse */
#define TEST_MENU_MOUSE_ID ((SDL_MouseID)0x7e57)

/* (the event thread, before the window's events are read) debug.test_input
"menu:...": its "mouse:<button>" and "wheel:<notches>[*<events>]"
(platform_test_menu_mouse) come as a mouse's events, which take the mouse's
own way to the menus (platform_pump_events) */
static void test_menu_mouse(void)
{
	int button, events, index;
	float wheel;
	SDL_Event event;

	if (!platform_test_menu_mouse(&button, &wheel, &events))
		return;
	memset(&event, 0, sizeof(event));
	if (button)
	{
		event.button.which = TEST_MENU_MOUSE_ID;
		event.button.button = (Uint8)button;
		event.button.clicks = 1;
		event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
		event.button.down = true;
		SDL_PushEvent(&event);
		event.type = SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.down = false;
		SDL_PushEvent(&event);
		platform_log("test input: mouse button %d clicked", button);
		return;
	}
	event.type = SDL_EVENT_MOUSE_WHEEL;
	event.wheel.which = TEST_MENU_MOUSE_ID;
	event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
	event.wheel.y = wheel / (float)events;
	for (index = 0; index < events; index++)
		SDL_PushEvent(&event);
	platform_log("test input: wheel turned %g in %d events", wheel, events);
}

/* whether this is a scripted run of the menus (debug.test_input
"menu:..."), read again as the settings change (config_changes): the
pointer is read every frame */
static BOOL test_menu_run(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static BOOL menu;

	if (read_at != config_changes())
	{
		unsigned long changes = config_changes();

		menu = !strncmp(config_string("debug.test_input"), "menu:", 5);
		read_at = changes;
	}
	return menu;
}

/* whether a mouse's event is left out of a scripted run of the menus,
which takes its presses alone: any mouse's but its own (test_menu_mouse) */
static BOOL test_menu_mouse_ignored(SDL_MouseID which)
{
	return which != TEST_MENU_MOUSE_ID && test_menu_run();
}
#endif

BOOL platform_next_keystroke(struct platform_keystroke *keystroke)
{
	BOOL result = FALSE;

	pthread_mutex_lock(&input_lock);
	if (keystroke_count)
	{
		*keystroke = keystroke_queue[keystroke_head];
		keystroke_head = (keystroke_head + 1) % KEYSTROKE_QUEUE_SIZE;
		keystroke_count--;
		result = TRUE;
	}
	pthread_mutex_unlock(&input_lock);
	return result;
}

/* ---------- internet play's invite links (p2p.c) */

#ifdef HALO_ANDROID
/* SDL declares it for Android builds only, which the guest is not
(guest/runtime/guest_sdl.c passes it to the host) */
bool SDL_ShowAndroidToast(const char *message, int duration, int gravity, int xoffset, int yoffset);
#endif

/* whether the text has an invite link in it (its prefix, in any case), a
party's link (p2p_party.c), an older build's of either (halo://), or the
join page's address of either (COPY
INVITE's message: p2p_invite_message) */
static BOOL platform_text_has_invite_link(const char *text)
{
	static const char *const prefixes[] = { P2P_JOIN_LINK_PREFIX, P2P_PARTY_LINK_PREFIX, P2P_OLD_JOIN_LINK_PREFIX,
		P2P_OLD_PARTY_LINK_PREFIX, "openh1.com/join/#" };

	for (; *text; text++)
	{
		size_t prefix;

		for (prefix = 0; prefix < sizeof(prefixes) / sizeof(*prefixes); prefix++)
		{
			size_t length = strlen(prefixes[prefix]);
			size_t index;

			for (index = 0; index < length && text[index] &&
				(text[index] | 0x20) == prefixes[prefix][index]; index++)
			{
			}
			if (index == length)
				return TRUE;
		}
	}
	return FALSE;
}

/* the clipboard's text (the menus' text fields' Ctrl+V), and text put on it
(the server settings' invite link); 0 if there is none. The main thread's */
int platform_clipboard_get(char *text, int size)
{
	char *clipboard = SDL_GetClipboardText();
	int got = clipboard && *clipboard;

	snprintf(text, (size_t)size, "%s", got ? clipboard : "");
	SDL_free(clipboard);
	return got;
}

/* whether the clipboard is left alone: in a run of the tests and tools
(debug.no_clipboard, or a setting only they set: drawing nothing, scripted
input, a tick a frame, a network test, quitting by itself), which play on
the machine of someone at work on it, the game puts no invite on it and
joins none found there */
static BOOL platform_clipboard_left_alone(void)
{
	/* (read again as the settings change: config_changes; the events'
	pump asks every frame) */
	static unsigned long read_at;
	static int alone = -1;

	if (alone < 0 || read_at != config_changes())
	{
		read_at = config_changes();
		alone = config_boolean("debug.no_clipboard") || config_boolean("debug.null_renderer") ||
			config_string("debug.test_input")[0] || config_boolean("debug.fixed_step") ||
			config_string("debug.network_test")[0] || config_real("debug.exit_after") > 0.0;
	}
	return alone;
}

/* whether the clipboard has an invite link on it (the multiplayer menu's
JOIN BY LINK says so), unless it is left alone */
int platform_clipboard_has_invite(void)
{
	char *clipboard;
	int found;

	if (platform_clipboard_left_alone())
		return 0;
	clipboard = SDL_GetClipboardText();
	found = clipboard && platform_text_has_invite_link(clipboard);
	SDL_free(clipboard);
	return found;
}

void platform_clipboard_set(const char *text)
{
	if (!platform_clipboard_left_alone())
		SDL_SetClipboardText(text);
}

int platform_open_url(const char *url)
{
#ifdef HALO_WEB
	web_js_post(5, url);
	return 1;
#else
	/* (the address logged too, for a machine whose browser does not open) */
	if (!SDL_OpenURL(url))
	{
		platform_log("could not open the browser (%s): open %s", SDL_GetError(), url);
		return 0;
	}
	return 1;
#endif
}

/* puts a new invite on the clipboard, and joins one found there when the
game comes to the front */
static void platform_invite_clipboard(BOOL look)
{
	/* the last clipboard text looked at, so each invite is joined once */
	static char seen[256];
	const char *invite = p2p_take_clipboard_text();

	if (platform_clipboard_left_alone())
		return;
	if (invite)
	{
		SDL_SetClipboardText(invite);
		snprintf(seen, sizeof(seen), "%s", invite);
		platform_log("Internet play: the invite link is on the clipboard");
#ifdef HALO_ANDROID
		SDL_ShowAndroidToast("Hosting: the invite link is on the clipboard", 1, -1, 0, 0);
#endif
	}
	if (look && config_boolean("network.join_from_clipboard"))
	{
		char *text = SDL_GetClipboardText();

		if (text && strcmp(text, seen) && strlen(text) < sizeof(seen))
		{
			snprintf(seen, sizeof(seen), "%s", text);
			/* (a link, not a bare code: 64 hex digits alone are as often a
			checksum copied for something else) */
			if (platform_text_has_invite_link(text) && p2p_join_invite(text))
			{
#ifdef HALO_ANDROID
				SDL_ShowAndroidToast("Joining the invite on the clipboard", 1, -1, 0, 0);
#endif
			}
		}
		SDL_free(text);
	}
}

/* ---------- messages for the player */

/* a message waiting for the event pump to show it (on the window's thread,
between frames) */
static pthread_mutex_t platform_message_lock = PTHREAD_MUTEX_INITIALIZER;
static char platform_message_title[80];
static char platform_message_text[600];
static BOOL platform_message_pending;

/* shows the player a message in a box of its own (the network code's: a
host of another version), and logs it */
void platform_show_message(const char *title, const char *message)
{
	platform_log("%s: %s", title, message);
	/* (a run nobody watches: the log only) */
	if (config_boolean("debug.hidden_window") || config_boolean("debug.null_renderer"))
		return;
	pthread_mutex_lock(&platform_message_lock);
	snprintf(platform_message_title, sizeof(platform_message_title), "%s", title);
	snprintf(platform_message_text, sizeof(platform_message_text), "%s", message);
	/* (streamer mode's words hidden here too: a map's or a mod's name) */
	text_filter_utf8(platform_message_title);
	text_filter_utf8(platform_message_text);
	platform_message_pending = TRUE;
	pthread_mutex_unlock(&platform_message_lock);
}

int platform_ask(const char *title, const char *text, const char *const *labels, int count, int default_index)
{
	/* (streamer mode's words hidden here too: mods' names) */
	static char shown_title[128];
	static char shown_text[4096];
	static char shown_labels[3][128];
	const char *shown[3];
	int index;
	SDL_MessageBoxButtonData buttons[3];
	SDL_MessageBoxData question;
	int answer = -1;

	if (count > 3)
		count = 3;
	snprintf(shown_title, sizeof(shown_title), "%s", title);
	snprintf(shown_text, sizeof(shown_text), "%s", text);
	text_filter_utf8(shown_title);
	text_filter_utf8(shown_text);
	for (index = 0; index < count; index++)
	{
		snprintf(shown_labels[index], sizeof(shown_labels[index]), "%s", labels[index]);
		text_filter_utf8(shown_labels[index]);
		shown[index] = shown_labels[index];
	}
	title = shown_title;
	text = shown_text;
	labels = shown;
	for (index = 0; index < count; index++)
	{
		buttons[index].flags = index == default_index ?
			SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0;
		buttons[index].buttonID = index;
		buttons[index].text = labels[index];
	}
	question.flags = SDL_MESSAGEBOX_WARNING;
	question.window = NULL;
	question.title = title;
	question.message = text;
	question.numbuttons = count;
	question.buttons = buttons;
	question.colorScheme = NULL;
	if (!SDL_ShowMessageBox(&question, &answer))
		return -1;
	return answer;
}

static void platform_show_pending_message(void)
{
	char title[sizeof(platform_message_title)];
	char text[sizeof(platform_message_text)];
	BOOL pending;

	pthread_mutex_lock(&platform_message_lock);
	pending = platform_message_pending;
	platform_message_pending = FALSE;
	memcpy(title, platform_message_title, sizeof(title));
	memcpy(text, platform_message_text, sizeof(text));
	pthread_mutex_unlock(&platform_message_lock);
	if (!pending)
		return;
#ifdef HALO_ANDROID
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, NULL);
#else
	{
		/* (a box cannot show above a fullscreen game) */
		BOOL fullscreen = platform_window_fullscreen();

		if (fullscreen)
			platform_window_set_fullscreen(FALSE);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, title, text, platform_window);
		if (fullscreen)
			platform_window_set_fullscreen(TRUE);
	}
#endif
}

/* ---------- events */

/* quits as closing the window does, when the events are next read (the
menus' Quit: port/linux/game/menu_functions.c); on Android at once */
void platform_request_quit(void)
{
#ifdef HALO_ANDROID
	/* (the guest has no SDL_PushEvent: exit ends the process, host_exit) */
	exit(EXIT_SUCCESS);
#else
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&event);
#endif
}

#ifndef HALO_ANDROID
/* (under input_lock, on the event thread) the scoreboard's pointer on: the
mouse freed, at the window's middle, and nothing held for the triggers */
static void scoreboard_pointer_start(void)
{
	int width, height;

	scoreboard_pointer_active = TRUE;
	memset(&scoreboard_pointer, 0, sizeof(scoreboard_pointer));
	memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
	memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
	input_state.mouse_dx = input_state.mouse_dy = 0.0f;
	platform_mouse_capture(FALSE);
	SDL_GetWindowSize(platform_window, &width, &height);
	SDL_WarpMouseInWindow(platform_window, width * 0.5f, height * 0.5f);
	scoreboard_pointer.x = width * 0.5f;
	scoreboard_pointer.y = height * 0.5f;
}

/* ... off: the mouse the aim's again (unless freed: F12, or the menus) */
static void scoreboard_pointer_stop(void)
{
	if (!scoreboard_pointer_active)
		return;
	scoreboard_pointer_active = FALSE;
	memset(&scoreboard_pointer, 0, sizeof(scoreboard_pointer));
	platform_mouse_capture(!input_state.mouse_released && !input_state.ui_pointer);
}

BOOL platform_scoreboard_pointer(BOOL offered, struct platform_ui_pointer *pointer)
{
	BOOL active;

	pthread_mutex_lock(&input_lock);
	scoreboard_pointer_offered = offered;
	active = scoreboard_pointer_active && offered;
	*pointer = scoreboard_pointer;
	scoreboard_pointer.moved = FALSE;
	scoreboard_pointer.left_clicks = 0;
	scoreboard_pointer.right_clicks = 0;
	scoreboard_pointer.wheel_steps = 0;
	pthread_mutex_unlock(&input_lock);
	return active;
}
#endif

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	Uint64 now = SDL_GetTicks();

	pthread_mutex_lock(&input_lock);
	if (!open || now >= scoreboard_open_until_ms)
	{
		scoreboard_wheel = 0.0f;
		scoreboard_notches = 0;
		scoreboard_pages = 0;
	}
	scoreboard_open_until_ms = open ? now + SCOREBOARD_OPEN_MS : 0;
#ifndef HALO_ANDROID
	if (!open)
		scoreboard_pointer_offered = FALSE;
#endif
	if (notches)
		*notches = scoreboard_notches;
	if (pages)
		*pages = scoreboard_pages;
	scoreboard_notches = 0;
	scoreboard_pages = 0;
	pthread_mutex_unlock(&input_lock);
}

void platform_screenshot_request(void)
{
	pthread_mutex_lock(&input_lock);
	screenshot_requested = TRUE;
	pthread_mutex_unlock(&input_lock);
}

BOOL platform_screenshot_take_request(void)
{
	BOOL requested;

	pthread_mutex_lock(&input_lock);
	requested = screenshot_requested;
	screenshot_requested = FALSE;
	pthread_mutex_unlock(&input_lock);
	return requested;
}

void platform_menu_studio_typing(BOOL typing)
{
	pthread_mutex_lock(&input_lock);
	menu_studio_typing = typing;
	menu_studio_typed_count = 0;
	pthread_mutex_unlock(&input_lock);
}

int platform_menu_studio_typed(char *typed, int size)
{
	int count;

	pthread_mutex_lock(&input_lock);
	count = menu_studio_typed_count < size ? menu_studio_typed_count : size;
	memcpy(typed, menu_studio_typed, (size_t)count);
	menu_studio_typed_count = 0;
	pthread_mutex_unlock(&input_lock);
	return count;
}

int platform_menu_studio_keys(void)
{
	int keys;

	pthread_mutex_lock(&input_lock);
	keys = menu_studio_keys;
	menu_studio_keys = 0;
	pthread_mutex_unlock(&input_lock);
	return keys;
}

/* debug.crash_after (seconds) crashes the game that long after the window
opens (without one, after the first frame), by a write to address 0 on the
game's thread: the crash report and the next start's question to test
(report.c, the hosts' crash records) */
static void crash_after_check(void)
{
	static Uint64 crash_ticks = (Uint64)-1;

	if (crash_ticks == (Uint64)-1)
	{
		double seconds = config_real("debug.crash_after");

		crash_ticks = seconds > 0.0 ? SDL_GetTicks() + (Uint64)(seconds * 1000.0) : 0;
	}
	if (crash_ticks && SDL_GetTicks() >= crash_ticks)
	{
		platform_log("crashing after debug.crash_after");
		*(volatile int *)0 = 0;
	}
}

/* debug.exit_after (seconds) ends the game that long after the window
opens (without one, after the first frame), as closing it does
(tools/pgo_train.py, net_compare.py) */
static void exit_after_check(void)
{
	static Uint64 exit_ticks = (Uint64)-1;

	if (exit_ticks == (Uint64)-1)
	{
		double seconds = config_real("debug.exit_after");

		exit_ticks = seconds > 0.0 ? SDL_GetTicks() + (Uint64)(seconds * 1000.0) : 0;
	}
	if (exit_ticks && SDL_GetTicks() >= exit_ticks)
	{
		const char *summary = getenv("HALO_FRAME_SUMMARY");

		if (summary && atof(summary) > 0.0)
			report_frames_log(atof(summary));
		platform_log("exiting after debug.exit_after");
		exit(EXIT_SUCCESS);
	}
	crash_after_check();
}

/* the events without a window (debug.null_renderer, or no display): on the
thread that first asks (the game's, as its frames end), debug.exit_after, a
request to quit (SIGINT and SIGTERM, which SDL makes quit events: else the
game would not stop for them), and the updater's question if
debug.update_answer answers it (a dialog with no game window to stand
over is never shown); the other events are of no window, and dropped */
static void platform_pump_events_headless(void)
{
	static SDL_ThreadID thread;
	SDL_Event event;

	if (!thread)
		thread = SDL_GetCurrentThreadID();
	if (SDL_GetCurrentThreadID() != thread)
		return;
	/* (a dedicated server stops itself: server_director.c) */
	if (!halo_server_mode())
		exit_after_check();
#ifndef HALO_ANDROID
	if (config_string("debug.update_answer")[0])
		updater_poll(NULL);
#endif
	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_EVENT_QUIT)
		{
			platform_log("asked to quit (no window)");
			exit(EXIT_SUCCESS);
		}
	}
}

#ifdef HALO_ANDROID
/* port/android/guest/runtime/guest_host.h */
void host_orientation(int which);

/* (on the window's thread: platform_pump_events) display.orientation, as
Settings or config.toml has it now, asked of the activity once the window
is made and whenever it changes: SDL asks only as it makes the window, and
not again for a full-screen one; the window turns in place, and the
swapchain and the touch controls follow its shape */
static void orientation_apply(void)
{
	static unsigned long read_at;
	static int asked = -1;
	const char *value;
	int which;

	if (asked >= 0 && read_at == config_changes())
		return;
	read_at = config_changes();
	value = config_string("display.orientation");
	which = !strcmp(value, "portrait") ? 1 : !strcmp(value, "auto") ? 2 : 0;
	if (which != asked)
	{
		asked = which;
		host_orientation(which);
	}
}
#endif

#ifdef HALO_GUEST
/* port/android/guest/runtime/guest_host.h: the keys and mouse buttons SDL
holds, and those the system has had up a while (host_sdl.c) */
unsigned int host_sdl_input_held(unsigned char *keys, int count);
#endif

/* (the input trace's clock: input_trace.c) */
unsigned long long input_trace_now_ns(void)
{
	return (unsigned long long)SDL_GetTicksNS();
}

/* an event as the input trace keeps it (input_trace.c) */
static void input_trace_event(const SDL_Event *event)
{
	static const struct
	{
		Uint32 type;
		int what;
	} window_events[] =
	{
		{ SDL_EVENT_WINDOW_FOCUS_GAINED, INPUT_TRACE_FOCUS_GAINED },
		{ SDL_EVENT_WINDOW_FOCUS_LOST, INPUT_TRACE_FOCUS_LOST },
		{ SDL_EVENT_WINDOW_SHOWN, INPUT_TRACE_SHOWN },
		{ SDL_EVENT_WINDOW_HIDDEN, INPUT_TRACE_HIDDEN },
		{ SDL_EVENT_WINDOW_EXPOSED, INPUT_TRACE_EXPOSED },
		{ SDL_EVENT_WINDOW_OCCLUDED, INPUT_TRACE_OCCLUDED },
		{ SDL_EVENT_WINDOW_MINIMIZED, INPUT_TRACE_MINIMIZED },
		{ SDL_EVENT_WINDOW_RESTORED, INPUT_TRACE_RESTORED },
		{ SDL_EVENT_WINDOW_ENTER_FULLSCREEN, INPUT_TRACE_FULLSCREEN },
		{ SDL_EVENT_WINDOW_LEAVE_FULLSCREEN, INPUT_TRACE_WINDOWED },
		{ SDL_EVENT_WINDOW_MOUSE_ENTER, INPUT_TRACE_MOUSE_ENTERED },
		{ SDL_EVENT_WINDOW_MOUSE_LEAVE, INPUT_TRACE_MOUSE_LEFT },
		{ SDL_EVENT_KEYBOARD_REMOVED, INPUT_TRACE_KEYBOARD_REMOVED },
	};
	size_t index;

	switch (event->type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		input_trace_key(event->key.timestamp, event->key.scancode, event->key.down, event->key.repeat, event->key.raw);
		return;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		input_trace_mouse_button(event->button.timestamp, event->button.button, event->button.down,
			input_state.ui_pointer);
		return;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		input_trace_gamepad_button(event->gbutton.timestamp, event->gbutton.which, event->gbutton.button,
			event->gbutton.down);
		return;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		input_trace_gamepad_axis(event->gaxis.timestamp, event->gaxis.which, event->gaxis.axis, event->gaxis.value);
		return;
	case SDL_EVENT_GAMEPAD_ADDED:
	case SDL_EVENT_GAMEPAD_REMOVED:
		input_trace_gamepad_device(event->gdevice.timestamp, event->gdevice.which,
			event->type == SDL_EVENT_GAMEPAD_ADDED);
		return;
	default:
		break;
	}
	for (index = 0; index < sizeof(window_events) / sizeof(window_events[0]); index++)
	{
		if (window_events[index].type == event->type)
			input_trace_window(event->common.timestamp, window_events[index].what);
	}
}

/* (input_lock, once a window's events are taken) The keys and mouse
buttons held are what the events left held, and an event can be lost on
its way (SDL's queue full, which drops its events but keeps its state; a
loop that takes the events and drops them, as a download's window does:
updater.c), or never come (macOS lets a key's release go astray: the
system's state has the key up, SDL's still down). A key or button held
counts only while SDL (host_sdl_input_held: SDL_GetKeyboardState,
SDL_GetMouseState) holds it too, and, on macOS, while the system has not
had it up for a while; what is let go of here is traced, logged, and the
last seconds of the trace logged with it (once a minute). Only what is held
is let go of: a key's press still counts once, latched (keys_pressed), and
nothing is held that its events did not hold. What a scripted run holds
(test_enter_held, test_buttons_held) stays. */
static void input_held_reconcile(void)
{
	static unsigned char held[SDL_SCANCODE_COUNT];
	static Uint64 logged_ms;
	unsigned int buttons;
	int scancode, button, released = 0;

#ifdef HALO_GUEST
	buttons = host_sdl_input_held(held, SDL_SCANCODE_COUNT);
#else
	{
		int count = 0;
		const bool *state = SDL_GetKeyboardState(&count);

		for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
			held[scancode] = scancode < count && state && state[scancode] ? 1 : 0;
		buttons = (unsigned int)SDL_GetMouseState(NULL, NULL) & 0xffffu;
	}
#endif
	for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
	{
		int by;

		if (!input_state.keys[scancode] || ((held[scancode] & 1) && !(held[scancode] & 2)))
			continue;
		if (scancode == SDL_SCANCODE_RETURN && test_enter_held)
			continue;
		by = held[scancode] & 1 ? INPUT_TRACE_RELEASED_BY_SYSTEM : INPUT_TRACE_RELEASED_BY_SDL;
		input_state.keys[scancode] = 0;
		input_trace_release(scancode, by);
		platform_log("input: %s held by its events, but %s; let go of", SDL_GetScancodeName((SDL_Scancode)scancode),
			by == INPUT_TRACE_RELEASED_BY_SYSTEM ? "the system has it up" : "SDL has it up");
		released++;
	}
	for (button = 1; button < PLATFORM_MOUSE_BUTTON_COUNT; button++)
	{
		unsigned int mask = SDL_BUTTON_MASK(button);
		int by;

		if (!input_state.mouse_buttons[button])
			test_buttons_held &= ~(1u << button);
		if (!input_state.mouse_buttons[button] || ((buttons & mask) && !(buttons & (mask << 16))) ||
			(test_buttons_held & (1u << button)))
		{
			continue;
		}
		by = buttons & mask ? INPUT_TRACE_RELEASED_BY_SYSTEM : INPUT_TRACE_RELEASED_BY_SDL;
		input_state.mouse_buttons[button] = 0;
		input_trace_release(INPUT_MOUSE + button, by);
		platform_log("input: mouse button %d held by its events, but %s; let go of", button,
			by == INPUT_TRACE_RELEASED_BY_SYSTEM ? "the system has it up" : "SDL has it up");
		released++;
	}
	if (released && (!logged_ms || SDL_GetTicks() - logged_ms >= 60000))
	{
		logged_ms = SDL_GetTicks();
		input_trace_log("a key or button its events left held was let go of", 10);
	}
}

/* (atexit, debug.input_trace) the trace as the game ends */
static void input_trace_at_exit(void)
{
	input_trace_log("the game ends", INPUT_TRACE_SECONDS);
}

void platform_pump_events(void)
{
	SDL_Event event;
	static BOOL looked_at_clipboard;
	BOOL look_at_clipboard = !looked_at_clipboard;
#ifdef PLATFORM_MOUSE_WARP
	BOOL mouse_needs_warp = FALSE;
#endif
	static int null_renderer = -1;

	{
		static BOOL trace_at_exit;

		/* (debug.input_trace: and as the game ends, a game without a window
		too) */
		if (!trace_at_exit)
		{
			trace_at_exit = TRUE;
			if (config_boolean("debug.input_trace"))
				atexit(input_trace_at_exit);
		}
	}
	if (!platform_window)
	{
		if (null_renderer < 0)
			null_renderer = config_boolean("debug.null_renderer") ? 1 : 0;
		if (platform_sdl_started && (null_renderer || platform_video_tried))
			platform_pump_events_headless();
		return;
	}
	if (SDL_GetCurrentThreadID() != platform_event_thread)
		return;
	exit_after_check();
	platform_show_pending_message();
#ifdef PLATFORM_MENU_CURSOR
	menu_cursor_fit();
#endif
#ifndef HALO_ANDROID
	updater_poll(platform_window);
	/* (Settings > Audio's output device, as it changes: dsound_sdl.c) */
	dsound_sdl_output_device_check();
#else
	/* (display.orientation, as it changes) */
	orientation_apply();
#endif
	screen_keyboard_update();
#ifndef HALO_ANDROID
	test_menu_mouse();
#endif
	pthread_mutex_lock(&input_lock);
#ifndef HALO_ANDROID
	/* (the scoreboard closed, or no longer offering it: the pointer goes) */
	if (scoreboard_pointer_active && (SDL_GetTicks() >= scoreboard_open_until_ms || !scoreboard_pointer_offered ||
		input_state.ui_pointer))
	{
		scoreboard_pointer_stop();
	}
#endif
	while (SDL_PollEvent(&event))
	{
		input_trace_event(&event);
		switch (event.type)
		{
		case SDL_EVENT_QUIT:
			pthread_mutex_unlock(&input_lock);
			platform_log("window closed");
			exit(EXIT_SUCCESS);
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			if (event.key.down && !event.key.repeat && event.key.scancode != SDL_SCANCODE_F11 &&
				event.key.scancode != SDL_SCANCODE_F12)
				movie_skip_presses++;
			if (event.key.scancode < SDL_SCANCODE_COUNT)
			{
				input_state.keys[event.key.scancode] = event.key.down;
				if (event.key.down)
					keys_pressed[event.key.scancode] = 1;
			}
			if (binding_capture == _binding_capture_waiting && event.key.down && !event.key.repeat &&
				event.key.scancode != SDL_SCANCODE_F11 && event.key.scancode != SDL_SCANCODE_F12)
			{
				binding_capture = _binding_capture_taken;
				binding_taken_ms = SDL_GetTicks();
				binding_capture_result = event.key.scancode == SDL_SCANCODE_ESCAPE ? 3 :
					event.key.scancode == SDL_SCANCODE_DELETE ? 2 : 1;
				binding_captured_input = event.key.scancode;
				break;
			}
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F9)
				menu_studio_keys |= 1 << 6;
			/* (a note typed: its keys the studio's alone) */
			if (menu_studio_typing && event.key.scancode != SDL_SCANCODE_F9)
			{
				if (event.key.down && menu_studio_typed_count < (int)sizeof(menu_studio_typed))
				{
					char typed = event.key.scancode == SDL_SCANCODE_BACKSPACE ? '\b' :
						event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER ? '\n' :
						event.key.scancode == SDL_SCANCODE_ESCAPE ? '\033' :
						(char)ascii_from_key(event.key.key, SDL_GetKeyFromScancode(event.key.scancode, event.key.mod, false),
							event.key.mod);

					if (typed)
						menu_studio_typed[menu_studio_typed_count++] = typed;
				}
				break;
			}
			queue_keystroke(&event.key);
			if (SDL_GetTicks() < scoreboard_open_until_ms && event.key.down &&
				(event.key.scancode == SDL_SCANCODE_PAGEUP || event.key.scancode == SDL_SCANCODE_PAGEDOWN))
			{
				scoreboard_pages += event.key.scancode == SDL_SCANCODE_PAGEDOWN ? 1 : -1;
			}
			/* (the menu studio's keys: port/linux/game/menu_studio.c) */
			if (event.key.down && !event.key.repeat && event.key.scancode >= SDL_SCANCODE_F3 &&
				event.key.scancode <= SDL_SCANCODE_F7)
			{
				menu_studio_keys |= 1 << (event.key.scancode - SDL_SCANCODE_F3);
			}

			/* F12 releases or recaptures the mouse */
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12)
			{
				input_state.mouse_released = !input_state.mouse_released;
				platform_mouse_capture(!input_state.mouse_released && !input_state.ui_pointer &&
					!scoreboard_pointer_active);
			}
#ifndef HALO_ANDROID
			/* F11 switches between fullscreen and the window (SDL keeps the
			window's size and place while fullscreen) */
			if (event.key.down && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F11)
			{
				platform_window_set_fullscreen(!platform_window_fullscreen());
			}
#endif
			break;
		case SDL_EVENT_MOUSE_MOTION:
#ifndef HALO_ANDROID
#ifdef PLATFORM_MENU_CURSOR
			/* (not the warp to the middle as a menu opens) */
			if (!input_state.ui_pointer || event.motion.x != ui_pointer.x || event.motion.y != ui_pointer.y)
				menu_cursor_mouse_used(input_state.ui_pointer);
#endif
			if (scoreboard_pointer_active)
			{
				scoreboard_pointer.x = event.motion.x;
				scoreboard_pointer.y = event.motion.y;
				scoreboard_pointer.moved = TRUE;
				break;
			}
			/* in the menus the mouse moves the pointer, not the view */
			if (input_state.ui_pointer)
			{
				ui_pointer.x = event.motion.x;
				ui_pointer.y = event.motion.y;
				ui_pointer.moved = TRUE;
				/* (iOS's taps come as the mouse's, SDL's for the touchscreen:
				port/ios/touch_controls.m. The menus size and use their targets
				for a finger: ui_widget.c's ui_mouse_pointer_is_touch) */
				ui_pointer.touch = event.motion.which == SDL_TOUCH_MOUSEID;
				break;
			}
#ifdef PLATFORM_MOUSE_WARP
			if (mouse_warp_capture)
			{
				if ((int)event.motion.x == mouse_warp_x && (int)event.motion.y == mouse_warp_y)
					break;
				mouse_needs_warp = TRUE;
			}
#endif
#endif
			input_state.mouse_dx += event.motion.xrel;
			input_state.mouse_dy += event.motion.yrel;
			break;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
#ifndef HALO_ANDROID
			if (test_menu_mouse_ignored(event.button.which))
				break;
#endif
			if (event.button.down)
				movie_skip_presses++;
#ifdef PLATFORM_MENU_CURSOR
			if (event.button.down)
				menu_cursor_mouse_used(input_state.ui_pointer);
#endif
			if (event.button.button < 32)
			{
				if (event.button.down)
					mouse_buttons_down |= 1u << event.button.button;
				else
					mouse_buttons_down &= ~(1u << event.button.button);
			}
			if (binding_capture == _binding_capture_waiting && event.button.down &&
				event.button.button < PLATFORM_MOUSE_BUTTON_COUNT)
			{
				binding_capture = _binding_capture_taken;
				binding_taken_ms = SDL_GetTicks();
				binding_capture_result = 1;
				binding_captured_input = INPUT_MOUSE + event.button.button;
				break;
			}
#ifndef HALO_ANDROID
			/* the open scoreboard's pointer: a right click frees it (and
			fires nothing), and another takes it back; its clicks pick */
			if (!input_state.ui_pointer && SDL_GetTicks() < scoreboard_open_until_ms && scoreboard_pointer_offered &&
				(scoreboard_pointer_active || (event.button.down && event.button.button == SDL_BUTTON_RIGHT)))
			{
				if (event.button.down && event.button.button == SDL_BUTTON_RIGHT)
				{
					if (scoreboard_pointer_active)
						scoreboard_pointer_stop();
					else
						scoreboard_pointer_start();
				}
				else if (event.button.down && event.button.button == SDL_BUTTON_LEFT)
				{
					scoreboard_pointer.left_clicks++;
					scoreboard_pointer.click_x = event.button.x;
					scoreboard_pointer.click_y = event.button.y;
				}
				break;
			}
			/* clicks in the menus go to the pointer; a button held down
			when the menu closes stays up until pressed again, so the click
			that resumes the game does not also fire */
			if (input_state.ui_pointer)
			{
				if (event.button.down && event.button.button == SDL_BUTTON_LEFT)
				{
					ui_pointer.left_clicks++;
					ui_pointer.click_x = event.button.x;
					ui_pointer.click_y = event.button.y;
					/* (a tap's: as the motion's) */
					ui_pointer.touch = event.button.which == SDL_TOUCH_MOUSEID;
				}
				else if (event.button.down && event.button.button == SDL_BUTTON_RIGHT)
				{
					ui_pointer.right_clicks++;
				}
				/* (the Back button: the menus' B, held as long as it is, as
				Escape's; let go of, or the menus closing, it lets B go) */
				else if (platform_mouse_button_backs(event.button.button))
				{
					input_state.menu_back = event.button.down;
					if (event.button.down)
						menu_back_pressed = TRUE;
				}
				break;
			}
#endif
			if (event.button.button < PLATFORM_MOUSE_BUTTON_COUNT)
			{
				input_state.mouse_buttons[event.button.button] = event.button.down;
				if (event.button.down)
					mouse_buttons_pressed[event.button.button] = 1;
			}
			break;
		case SDL_EVENT_MOUSE_WHEEL:
#ifndef HALO_ANDROID
			if (test_menu_mouse_ignored(event.wheel.which))
				break;
#endif
			if (binding_capture == _binding_capture_waiting && event.wheel.y != 0.0f)
			{
				binding_capture = _binding_capture_taken;
				binding_taken_ms = SDL_GetTicks();
				binding_capture_result = 1;
				binding_captured_input = event.wheel.y > 0.0f ? INPUT_WHEEL_UP : INPUT_WHEEL_DOWN;
				break;
			}
			if (SDL_GetTicks() < scoreboard_open_until_ms)
			{
				/* whole notches, up (away) scrolling up */
				scoreboard_wheel -= event.wheel.y;
				while (scoreboard_wheel >= 1.0f)
				{
					scoreboard_notches++;
					scoreboard_wheel -= 1.0f;
				}
				while (scoreboard_wheel <= -1.0f)
				{
					scoreboard_notches--;
					scoreboard_wheel += 1.0f;
				}
				break;
			}
#ifndef HALO_ANDROID
			if (input_state.ui_pointer)
			{
				/* whole notches: smooth-scrolling wheels send fractions */
				ui_pointer_wheel += event.wheel.y;
				while (ui_pointer_wheel >= 1.0f)
				{
					ui_pointer.wheel_steps++;
					ui_pointer_wheel -= 1.0f;
				}
				while (ui_pointer_wheel <= -1.0f)
				{
					ui_pointer.wheel_steps--;
					ui_pointer_wheel += 1.0f;
				}
				break;
			}
#endif
			input_state.mouse_wheel += event.wheel.y;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			/* (debug.input_trace: the trace, as the window lets the keys go) */
			if (config_boolean("debug.input_trace"))
				input_trace_log("the window lost the focus", INPUT_TRACE_SECONDS);
			/* (and what was pressed before it went, which would otherwise
			count when it comes back) */
			memset(input_state.keys, 0, sizeof(input_state.keys));
			memset(keys_pressed, 0, sizeof(keys_pressed));
			memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
			memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
			gamepad_presses_clear();
			input_state.menu_back = FALSE;
#ifndef HALO_ANDROID
			menu_back_pressed = FALSE;
#endif
			input_state.focused = FALSE;
			/* (the scoreboard's pointer goes; the mouse is taken back for
			the aim as the window has the focus again) */
			scoreboard_pointer_active = FALSE;
#ifdef PLATFORM_MOUSE_WARP
			if (mouse_warp_capture)
			{
				mouse_warp_capture = FALSE;
				SDL_SetWindowMouseGrab(platform_window, false);
				SDL_ShowCursor();
			}
#endif
#if defined(HALO_ANDROID) || defined(HALO_IOS)
			touch_input_cancel();
#endif
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			input_state.focused = TRUE;
			look_at_clipboard = TRUE;
			/* (Android drops the pointer capture when the app leaves the
			foreground; take it back, as the desktop builds do) */
			if (!input_state.mouse_released && !input_state.ui_pointer && !scoreboard_pointer_active)
				platform_mouse_capture(TRUE);
			break;
#if defined(HALO_ANDROID) || defined(HALO_IOS)
		/* (on iOS the fingers the touch controls' overlay does not take: a
		cinematic's skip, the text chat's keys; the menus take them as the
		mouse, port/ios/touch_controls.m) */
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED:
			if (event.type == SDL_EVENT_FINGER_DOWN)
				movie_skip_presses++;
			touch_input_event(event.type, &event.tfinger);
			break;
#endif
		case SDL_EVENT_GAMEPAD_ADDED:
#ifdef HALO_GUEST
			/* (the guest reaches SDL only through host_imports.list, which
			has no SDL_GetGamepadName) */
			SDL_OpenGamepad(event.gdevice.which);
#else
			{
				SDL_Gamepad *gamepad = SDL_OpenGamepad(event.gdevice.which);

				/* (which pads the game drives: under Steam Input, Steam's
				virtual ones, named for the controllers behind them; its
				kind, and the device SDL reads it through: on Linux, a
				hidraw node is SDL's own driver of the pad, an event node
				the kernel's) */
				if (gamepad)
				{
					const char *name = SDL_GetGamepadName(gamepad);
					const char *type = SDL_GetGamepadStringForType(SDL_GetGamepadType(gamepad));
					const char *path = SDL_GetGamepadPath(gamepad);

					platform_log("gamepad: %s (%s%s%s)", name ? name : "(unnamed)", type ? type : "unknown",
						path ? ", " : "", path ? path : "");
				}
			}
#endif
			break;
		case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		case SDL_EVENT_GAMEPAD_BUTTON_UP:
			if (event.gbutton.down)
				gamepad_button_pressed(event.gbutton.which, event.gbutton.button);
#ifdef PLATFORM_MENU_CURSOR
			if (event.gbutton.down)
				menu_cursor_controller_used(input_state.ui_pointer);
#endif
			break;
		case SDL_EVENT_GAMEPAD_AXIS_MOTION:
			gamepad_trigger_moved(event.gaxis.which, event.gaxis.axis, event.gaxis.value);
#ifdef PLATFORM_MENU_CURSOR
			/* (a stick or trigger past its rest) */
			if (abs(event.gaxis.value) >= (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
				event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? INPUT_TRIGGER_PRESSED : INPUT_AXIS_PRESSED))
			{
				menu_cursor_controller_used(input_state.ui_pointer);
			}
#endif
			break;
		case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
			halo_gyro_sensor_event(event.gsensor.which, event.gsensor.sensor, event.gsensor.data,
				event.gsensor.sensor_timestamp, event.gsensor.timestamp);
			break;
		case SDL_EVENT_GAMEPAD_REMOVED:
			/* (SDL keeps a gamepad open until it is closed, even once the
			controller has gone) */
			{
				SDL_Gamepad *gamepad = SDL_GetGamepadFromID(event.gdevice.which);

				gamepad_presses_forget(event.gdevice.which);
				if (gamepad)
					SDL_CloseGamepad(gamepad);
			}
			break;
		default:
			break;
		}
	}
#ifdef PLATFORM_MOUSE_WARP
	if (mouse_warp_capture && mouse_needs_warp && !input_state.ui_pointer &&
		!scoreboard_pointer_active && !input_state.mouse_released)
	{
		int width = 0, height = 0;

		SDL_GetWindowSize(platform_window, &width, &height);
		if (width > 0 && height > 0)
		{
			mouse_warp_x = width / 2;
			mouse_warp_y = height / 2;
			SDL_WarpMouseInWindow(platform_window, (float)mouse_warp_x, (float)mouse_warp_y);
		}
	}
#endif
	input_held_reconcile();
	test_menu_keys();
	{
		unsigned char before[PLATFORM_MOUSE_BUTTON_COUNT];
		int button;

		memcpy(before, input_state.mouse_buttons, sizeof(before));
		halo_test_input_pump(&input_state, mouse_buttons_pressed);
		for (button = 0; button < PLATFORM_MOUSE_BUTTON_COUNT; button++)
		{
			if (!before[button] && input_state.mouse_buttons[button])
				test_buttons_held |= 1u << button;
		}
	}
	pthread_mutex_unlock(&input_lock);
	looked_at_clipboard = TRUE;
	platform_invite_clipboard(look_at_clipboard);
}

int platform_movie_skip_pressed(void)
{
	int pressed;

	pthread_mutex_lock(&input_lock);
	pressed = movie_skip_presses != movie_skip_presses_seen;
	movie_skip_presses_seen = movie_skip_presses;
	pthread_mutex_unlock(&input_lock);
	return pressed;
}

void platform_menus_set_active(BOOL active)
{
	pthread_mutex_lock(&input_lock);
	input_state.menus = active;
	pthread_mutex_unlock(&input_lock);
}

void platform_binding_capture_begin(void)
{
	pthread_mutex_lock(&input_lock);
	binding_capture = _binding_capture_waiting;
	binding_settling = TRUE;
	binding_polled_ms = SDL_GetTicks();
	pthread_mutex_unlock(&input_lock);
}

void platform_binding_capture_cancel(void)
{
	pthread_mutex_lock(&input_lock);
	binding_capture = _binding_capture_idle;
	pthread_mutex_unlock(&input_lock);
}

int platform_binding_capture_poll(int *input)
{
	int result = 0;

	pthread_mutex_lock(&input_lock);
	binding_polled_ms = SDL_GetTicks();
	if (binding_capture == _binding_capture_taken)
	{
		result = binding_capture_result;
		*input = binding_captured_input;
		binding_capture = _binding_capture_idle;
	}
	pthread_mutex_unlock(&input_lock);
	return result;
}

/* ---------- the system's on-screen keyboard

A menu's text field is typed into (platform_text_field, xinput_sdl.c).
Where a system's on-screen keyboard is there to bring up, SDL's text input
runs while the field is typed into: the keyboard comes up with the field and
goes with it, and what it types arrives as keys (SDL makes a key of each
letter, digit and sign of ASCII the keyboard types, and its delete and
Enter keys theirs), which the field reads. Those keyboards are Steam's (in
Big Picture and in the Steam Deck's Game Mode, which ask for it with
SDL_ENABLE_STEAM_SCREEN_KEYBOARD) and the touchscreens' builds' own,
Android's (its activity's, by host_android_text_input) and iOS's.
Elsewhere text input stays off, as before, so that no input method takes
the keys the field reads: a Wayland touch screen's keyboard (text-input-v3)
would type text events, which the field does not read. */
static int screen_keyboard_wanted;
/* (each field begun, which brings the keyboard up again: Steam does not say
when its keyboard goes, by its own Enter or closed by hand, so SDL holds it
to be up still; after a field ended and another begun in the same frame, as
the password screen's is after a wrong password, it would not come back.
A touchscreen's keyboard closed by its own key, while the field is still
typed into, comes back at the next field begun, or the same tapped again,
and not before) */
static int screen_keyboard_requests;

void platform_screen_keyboard(BOOL show, BOOL password)
{
	__atomic_store_n(&screen_keyboard_wanted, !show ? 0 : password ? 2 : 1, __ATOMIC_SEQ_CST);
	if (show)
		__atomic_add_fetch(&screen_keyboard_requests, 1, __ATOMIC_SEQ_CST);
}

#ifdef HALO_ANDROID
/* port/android/guest/runtime/guest_host.h */
void host_android_text_input(int show, int password);
int host_android_text_input_active(void);
#endif

/* whether the system's keyboard is up (SDL's text input running) */
static BOOL screen_keyboard_active(void)
{
#ifdef HALO_ANDROID
	return host_android_text_input_active() != 0;
#else
	return SDL_TextInputActive(platform_window);
#endif
}

static void screen_keyboard_stop(void)
{
#ifdef HALO_ANDROID
	host_android_text_input(FALSE, FALSE);
#else
	SDL_StopTextInput(platform_window);
#endif
}

/* the system's keyboard brought up for one line (a password's, wanted 2,
for the keyboards that hide what is typed into one): its Enter ends the
field, and the keyboard goes with it */
static void screen_keyboard_start(int wanted)
{
	platform_log("text field: showing the on-screen keyboard");
#ifdef HALO_ANDROID
	host_android_text_input(TRUE, wanted == 2);
#else
	{
		SDL_PropertiesID properties = SDL_CreateProperties();

		SDL_SetBooleanProperty(properties, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
		SDL_SetNumberProperty(properties, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
			wanted == 2 ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN : SDL_TEXTINPUT_TYPE_TEXT);
		SDL_StartTextInputWithProperties(platform_window, properties);
		SDL_DestroyProperties(properties);
	}
#endif
}

/* (on the window's thread, as SDL asks: platform_pump_events) */
static void screen_keyboard_update(void)
{
	/* (a keyboard shown again is closed first, as SDL opens none that it
	holds to be up, and opened a moment later: Steam takes each as a URL,
	steam://close/keyboard then steam://open/keyboard, which must not
	arrive the other way round) */
	enum { REOPEN_DELAY_MS = 500 };
	static int requests_handled;
	static Uint64 open_time;
	int requests = __atomic_load_n(&screen_keyboard_requests, __ATOMIC_SEQ_CST);
	int wanted = __atomic_load_n(&screen_keyboard_wanted, __ATOMIC_SEQ_CST);

	if (!wanted)
	{
		open_time = 0;
		if (screen_keyboard_active())
			screen_keyboard_stop();
		return;
	}
	if (requests != requests_handled)
	{
		requests_handled = requests;
#if defined(HALO_ANDROID) || defined(HALO_IOS)
		/* (the touchscreens' own keyboard, which SDL knows to be up or
		closed: brought up if it is not, at once) */
		if (!screen_keyboard_active())
			screen_keyboard_start(wanted);
		return;
#else
		if (!SDL_HasScreenKeyboardSupport() ||
			!SDL_GetHintBoolean(SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD, false))
		{
			return;
		}
		open_time = SDL_GetTicks();
		if (screen_keyboard_active())
		{
			screen_keyboard_stop();
			open_time += REOPEN_DELAY_MS;
		}
#endif
	}
	if (open_time && SDL_GetTicks() >= open_time)
	{
		open_time = 0;
		screen_keyboard_start(wanted);
	}
}

#ifndef HALO_ANDROID
/* ---------- the menus' pointer */

/* While a menu is up the mouse is released, its pointer shows (centered when
the menu opens) and its motion, clicks and wheel go to the menus
(halo_ui_pointer_update, d3d8_device.c) instead of the controller and the aim. */
void platform_ui_pointer_set_active(BOOL active)
{
	if (!platform_window || (active != FALSE) == (input_state.ui_pointer != FALSE))
		return;
	pthread_mutex_lock(&input_lock);
	input_state.ui_pointer = active;
	memset(&ui_pointer, 0, sizeof(ui_pointer));
	ui_pointer_wheel = 0.0f;
	input_state.menu_back = FALSE;
	menu_back_pressed = FALSE;
	input_state.mouse_dx = 0.0f;
	input_state.mouse_dy = 0.0f;
	input_state.mouse_wheel = 0.0f;
	memset(input_state.mouse_buttons, 0, sizeof(input_state.mouse_buttons));
	memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
	pthread_mutex_unlock(&input_lock);
	platform_mouse_capture(!active && !input_state.mouse_released);
#ifdef PLATFORM_MENU_CURSOR
	menu_cursor_show(!active || !menu_cursor_controller);
#endif
	if (active)
	{
		int width, height;

		SDL_GetWindowSize(platform_window, &width, &height);
		SDL_WarpMouseInWindow(platform_window, width * 0.5f, height * 0.5f);
		pthread_mutex_lock(&input_lock);
		ui_pointer.x = width * 0.5f;
		ui_pointer.y = height * 0.5f;
		pthread_mutex_unlock(&input_lock);
	}
}

/* what the pointer did since the last call; FALSE when it is not active */
BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	BOOL active;

	pthread_mutex_lock(&input_lock);
	active = input_state.ui_pointer;
	*pointer = ui_pointer;
	pointer->left_held = (mouse_buttons_down & (1u << SDL_BUTTON_LEFT)) != 0;
	ui_pointer.moved = FALSE;
	ui_pointer.left_clicks = 0;
	ui_pointer.right_clicks = 0;
	ui_pointer.wheel_steps = 0;
	pthread_mutex_unlock(&input_lock);
	/* (a scripted run of the menus, debug.test_input = "menu:...", takes its
	presses alone: the mouse of whoever is at the computer moves no focus) */
	if (test_menu_run())
	{
		/* (but its own clicks and moves: "click:<x>:<y>" and "point:<x>:<y>",
		platform_test_menu_click;
		and its own mouse's wheel, the others' events left out:
		test_menu_mouse_ignored) */
		float x, y;
		int width, height;
		int wheel_steps = pointer->wheel_steps;
		int clicked = active ? platform_test_menu_click(&x, &y) : 0;

		if (!active || (!clicked && !wheel_steps))
			return FALSE;
		memset(pointer, 0, sizeof(*pointer));
		pointer->wheel_steps = wheel_steps;
		if (!clicked)
			return TRUE;
		SDL_GetWindowSize(platform_window, &width, &height);
		pointer->x = pointer->click_x = x * width;
		pointer->y = pointer->click_y = y * height;
		pointer->moved = TRUE;
		pointer->left_clicks = clicked == 1;
		return TRUE;
	}
	return active;
}

void platform_video_window_size(int *width, int *height)
{
	SDL_GetWindowSize(platform_window, width, height);
}

#else
/* ---------- the menus' pointer (the touchscreen)

While a menu is up, taps and drags go to the menus (touch_input.c,
halo_ui_pointer_update in d3d8_device.c). */
void platform_ui_pointer_set_active(BOOL active)
{
	BOOL changed;

	pthread_mutex_lock(&input_lock);
	changed = (active != FALSE) != (input_state.ui_pointer != FALSE);
	if (changed)
	{
		input_state.ui_pointer = active;
		touch_input_menu_set_active(active != FALSE);
	}
	pthread_mutex_unlock(&input_lock);
	/* a physical mouse: captured for the aim while playing, freed for the
	menus (its pointer shows there, as it does on the desktop) */
	if (changed)
		platform_mouse_capture(!active && !input_state.mouse_released);
}

BOOL platform_ui_pointer_read(struct platform_ui_pointer *pointer)
{
	BOOL active;

	pthread_mutex_lock(&input_lock);
	active = input_state.ui_pointer;
	touch_input_menu_read(pointer);
	pthread_mutex_unlock(&input_lock);
	return active;
}

/* the window is its pixels on Android (no display scaling): touch_input.c
scales the fingers' 0..1 by the drawable's size */
void platform_video_window_size(int *width, int *height)
{
	platform_video_drawable_size(width, height);
}

#endif
void platform_input_read(struct platform_input_state *state, BOOL consume_motion)
{
	pthread_mutex_lock(&input_lock);
	*state = input_state;
	/* (rebinding: nothing reaches the controller until the input is taken
	and every key and button is up again) */
	if (binding_capture != _binding_capture_idle || binding_settling)
	{
		int scancode;
		BOOL held = mouse_buttons_down != 0;

		for (scancode = 0; scancode < SDL_SCANCODE_COUNT && !held; scancode++)
			held = input_state.keys[scancode] != 0;
		if (binding_capture == _binding_capture_taken && SDL_GetTicks() - binding_taken_ms > BINDING_UNCLAIMED_MS)
			binding_capture = _binding_capture_idle;
		if (binding_capture == _binding_capture_waiting && SDL_GetTicks() - binding_polled_ms > BINDING_ABANDONED_MS)
			binding_capture = _binding_capture_idle;
		if (binding_capture == _binding_capture_idle && !held)
			binding_settling = FALSE;
		memset(state->keys, 0, sizeof(state->keys));
		memset(state->mouse_buttons, 0, sizeof(state->mouse_buttons));
		memset(keys_pressed, 0, sizeof(keys_pressed));
		memset(mouse_buttons_pressed, 0, sizeof(mouse_buttons_pressed));
		state->menu_back = FALSE;
#ifndef HALO_ANDROID
		menu_back_pressed = FALSE;
#endif
		state->mouse_wheel = 0.0f;
		/* (and the motion of the while, which would otherwise pile up for
		the aim) */
		state->mouse_dx = state->mouse_dy = 0.0f;
		if (consume_motion)
		{
			input_state.mouse_dx = input_state.mouse_dy = 0.0f;
			input_state.mouse_wheel = 0.0f;
		}
		pthread_mutex_unlock(&input_lock);
		return;
	}
	if (consume_motion)
	{
		int scancode;

		/* (the input trace: the reading as the controller takes it, and
		what it latched) */
		input_trace_reading(input_state.keys, SDL_SCANCODE_COUNT, input_state.mouse_buttons,
			PLATFORM_MOUSE_BUTTON_COUNT, keys_pressed, mouse_buttons_pressed,
			(input_state.menus ? INPUT_TRACE_READING_MENUS : 0) |
			(input_state.focused ? INPUT_TRACE_READING_FOCUSED : 0) |
			(input_state.ui_pointer ? INPUT_TRACE_READING_POINTER : 0) |
			(input_state.mouse_released ? INPUT_TRACE_READING_RELEASED : 0));
		for (scancode = 0; scancode < SDL_SCANCODE_COUNT; scancode++)
		{
			state->keys[scancode] |= keys_pressed[scancode];
			keys_pressed[scancode] = 0;
		}
		for (scancode = 0; scancode < PLATFORM_MOUSE_BUTTON_COUNT; scancode++)
		{
			state->mouse_buttons[scancode] |= mouse_buttons_pressed[scancode];
			mouse_buttons_pressed[scancode] = 0;
		}
#ifndef HALO_ANDROID
		state->menu_back |= menu_back_pressed;
		menu_back_pressed = FALSE;
#endif
	}
	if (consume_motion)
	{
		input_state.mouse_dx = 0;
		input_state.mouse_dy = 0;
		input_state.mouse_wheel = 0;
	}
	pthread_mutex_unlock(&input_lock);
}

/* Absolute times let a host trace use the same monotonic clock. Unsupported
CPU clocks are -1, never an apparent zero cost. No clocks read when disabled. */
int halo_load_trace_enabled(void)
{
	/* (read again as the settings change: config_changes; asked every
	frame and every file read) */
	static unsigned long read_at = (unsigned long)-1;
	static int enabled;

	if (read_at != config_changes())
	{
		unsigned long changes = config_changes();

		enabled = config_boolean("debug.load_trace");
		read_at = changes;
	}
	return enabled;
}

void halo_load_trace(char const *phase, char const *detail)
{
	struct timespec now;
	double wall = -1.0, thread = -1.0, process = -1.0;

	if (!halo_load_trace_enabled())
		return;
	if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
		wall = (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#ifdef CLOCK_THREAD_CPUTIME_ID
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
		thread = (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#endif
#ifdef CLOCK_PROCESS_CPUTIME_ID
	if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) == 0)
		process = (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#endif
	load_trace_log("LOAD phase=%s wall=%.6f thread=%.6f process=%.6f owner=guest:%llu detail=%s",
		phase, wall, thread, process, (unsigned long long)SDL_GetCurrentThreadID(), detail ? detail : "");
}

/* each pass of the game's main loop ended: with the load trace, a frame that
took longer than LONG_FRAME_SECONDS is recorded ("frame.long", its
milliseconds; "frame.longest" for each longer than any before), so that a
hitch in the menus or the game is found and measured on every platform */
#define LONG_FRAME_SECONDS 0.025
void halo_load_trace_frame(void)
{
	static double previous, longest;
	struct timespec now;
	double wall;
	char detail[32];

	if (!halo_load_trace_enabled() || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return;
	wall = (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
	if (previous > 0.0 && wall - previous > LONG_FRAME_SECONDS)
	{
		snprintf(detail, sizeof(detail), "%.1f", (wall - previous) * 1000.0);
		halo_load_trace(wall - previous > longest ? "frame.longest" : "frame.long", detail);
		if (wall - previous > longest)
			longest = wall - previous;
	}
	previous = wall;
}
