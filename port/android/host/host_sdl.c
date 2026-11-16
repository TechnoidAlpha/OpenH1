/*
HOST_SDL.C

SDL3 on behalf of the guest (guest/runtime/guest_sdl.c). SDL objects are
64-bit pointers, which the guest cannot hold; it gets small handles into the
table here instead.

The guest calls these on its own threads, whose stacks (in guest memory) are
also the ones ART knows, so SDL may call into Java from them
(host_thread.c). SDL's audio thread is the exception: it has no guest stack,
so the audio callback is handed to a thread that has one.
*/

#include "host.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
/* port/ios/host_app.m: returns once the app is in the foreground; the
fraction of the screen's pixels the game draws at */
void host_app_background_wait(void);
float host_app_render_scale(void);
/* the audio session's output latency now (microseconds; -1 not known) */
int host_app_audio_output_latency(void);
#endif
#ifdef __ANDROID__
/* host_main.c: the fraction of the screen's pixels the game draws at */
float host_app_render_scale(void);
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
/* the system's own state of the keys and mouse buttons
(host_sdl_input_held) */
#define HOST_SYSTEM_KEYS 1
/* CoreGraphics's (CGEventSource.h, whose headers host.h's names collide
with): the session's state of a key, by its key code, and of a mouse
button */
typedef uint16_t CGKeyCode;
typedef uint32_t CGMouseButton;
typedef int32_t CGEventSourceStateID;
#define kCGEventSourceStateCombinedSessionState 0
#define kCGMouseButtonLeft 0
#define kCGMouseButtonRight 1
#define kCGMouseButtonCenter 2
extern bool CGEventSourceKeyState(CGEventSourceStateID state, CGKeyCode key);
extern bool CGEventSourceButtonState(CGEventSourceStateID state, CGMouseButton button);
/* each scancode's key code (its event's raw code), as last seen down */
static uint16_t system_key_codes[SDL_SCANCODE_COUNT];
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
/* port/macos/host_app.m: the updater's (weak: the Metal tests and the
replay have no host_app.m) */
void host_app_update_tick(void);
#pragma weak host_app_update_tick
#endif
#ifdef __linux__
#include <sys/resource.h>
#include <time.h>
#endif
#ifdef __APPLE__
/* port/macos/host_links.c: the links the app is opened with (openh1://...),
which SDL sends as drop events, kept for the guest */
void host_links_watch(void);
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <CoreAudio/CoreAudio.h>
/* port/macos/host_audio_output.c: the sound played through CoreAudio's
output unit rather than SDL's AudioQueue, with less delay */
#define HOST_AUDIO_OUTPUT 1
SDL_AudioStream *host_audio_output_open(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
	SDL_AudioStreamCallback callback, void *userdata);
int host_audio_output_resume(SDL_AudioStream *stream);
int host_audio_output_close(SDL_AudioStream *stream);
int host_audio_output_timing(SDL_AudioStream *stream, int *timing);
void host_audio_output_thread(SDL_AudioStream *stream, int *state, void **output);
void host_audio_output_usage(uint64_t *values);
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
/* port/macos/host_raw_mouse.m: the mouse's unaccelerated counts */
#define HOST_RAW_MOUSE 1
void host_raw_mouse_start(void);
int host_raw_mouse_take(float *x, float *y);
void host_raw_mouse_cursor(void);
#endif

#define HANDLE_COUNT 256

struct handle
{
	int type;
	void *object;
	/* an audio stream with a callback: its binding (below) */
	void *binding;
};

static struct handle handles[HANDLE_COUNT];
static pthread_mutex_t handle_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t host_sdl_handle_new(int type, void *object)
{
	uint32_t index;

	if (!object)
		return 0;
	pthread_mutex_lock(&handle_lock);
	/* an object that already has a handle keeps it */
	for (index = 1; index < HANDLE_COUNT; index++)
	{
		if (handles[index].type == type && handles[index].object == object)
		{
			pthread_mutex_unlock(&handle_lock);
			return index;
		}
	}
	for (index = 1; index < HANDLE_COUNT; index++)
	{
		if (handles[index].type == _handle_free)
		{
			handles[index].type = type;
			handles[index].object = object;
			handles[index].binding = NULL;
			pthread_mutex_unlock(&handle_lock);
			return index;
		}
	}
	pthread_mutex_unlock(&handle_lock);
	host_logf(HOST_LOG_ERROR, "out of SDL handles");
	return 0;
}

void *host_sdl_handle_get(uint32_t handle, int type)
{
	void *object = NULL;

	if (handle == 0 || handle >= HANDLE_COUNT)
		return NULL;
	pthread_mutex_lock(&handle_lock);
	if (handles[handle].type == type)
		object = handles[handle].object;
	pthread_mutex_unlock(&handle_lock);
	return object;
}

void host_sdl_handle_free(uint32_t handle)
{
	if (handle == 0 || handle >= HANDLE_COUNT)
		return;
	pthread_mutex_lock(&handle_lock);
	handles[handle].type = _handle_free;
	handles[handle].object = NULL;
	handles[handle].binding = NULL;
	pthread_mutex_unlock(&handle_lock);
}

/* the object of a handle, which the handle no longer refers to; NULL when
there is none. *binding gets what host_sdl_handle_new's caller stored (audio) */
static void *handle_release(uint32_t handle, int type, void **binding)
{
	void *object = NULL;

	if (binding)
		*binding = NULL;
	if (handle == 0 || handle >= HANDLE_COUNT)
		return NULL;
	pthread_mutex_lock(&handle_lock);
	if (handles[handle].type == type)
	{
		object = handles[handle].object;
		if (binding)
			*binding = handles[handle].binding;
		handles[handle].type = _handle_free;
		handles[handle].object = NULL;
		handles[handle].binding = NULL;
	}
	pthread_mutex_unlock(&handle_lock);
	return object;
}

/* ---------- general */

int host_sdl_init(uint32_t flags)
{
#ifdef __APPLE__
	/* (before SDL starts: the link a launch was for may come as it does) */
	host_links_watch();
#endif
	return SDL_Init((SDL_InitFlags)flags);
}

int host_sdl_set_hint(const char *name, const char *value)
{
	return SDL_SetHint(name, value);
}

void host_sdl_get_error(char *buffer, uint32_t size)
{
	SDL_strlcpy(buffer, SDL_GetError(), size);
}

void host_sdl_scancode_name(int32_t scancode, char *buffer, uint32_t size)
{
	SDL_strlcpy(buffer, SDL_GetScancodeName((SDL_Scancode)scancode), size);
}

int32_t host_sdl_scancode_from_name(const char *name)
{
	return (int32_t)SDL_GetScancodeFromName(name);
}

int32_t host_sdl_key_from_scancode(int32_t scancode, int32_t modifiers, int32_t key_event)
{
	if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
		return (int32_t)SDLK_UNKNOWN;
	return (int32_t)SDL_GetKeyFromScancode((SDL_Scancode)scancode, (SDL_Keymod)modifiers, key_event != 0);
}

int64_t host_sdl_ticks(void)
{
	return (int64_t)SDL_GetTicks();
}

int64_t host_sdl_ticks_ns(void)
{
	return (int64_t)SDL_GetTicksNS();
}

int64_t host_sdl_thread_id(void)
{
	return (int64_t)SDL_GetCurrentThreadID();
}

/* (the guest's poll thread, battery_saver.c: SDL asks the system, Java's
battery broadcast on Android) */
int host_sdl_power_info(int *seconds, int *percent)
{
	return (int)SDL_GetPowerInfo(seconds, percent);
}

/* ---------- video */

uint32_t host_sdl_create_window(const char *title, int width, int height, int64_t flags)
{
	return host_sdl_handle_new(_handle_window, SDL_CreateWindow(title, width, height, (SDL_WindowFlags)flags));
}

void host_sdl_window_size_in_pixels(uint32_t window, int *width, int *height)
{
	SDL_Window *object = host_sdl_handle_get(window, _handle_window);

	*width = 0;
	*height = 0;
	if (object)
		SDL_GetWindowSizeInPixels(object, width, height);
#if (defined(__APPLE__) && TARGET_OS_IPHONE) || defined(__ANDROID__)
	/* (display.render_scale: the game draws smaller, host_metal.m's
	drawables and Android's swapchain are that size, and the display scales
	them up) */
	*width = (int)(*width * host_app_render_scale() + 0.5f) & ~1;
	*height = (int)(*height * host_app_render_scale() + 0.5f) & ~1;
#endif
}

int host_sdl_set_relative_mouse(uint32_t window, int enabled)
{
	SDL_Window *object = host_sdl_handle_get(window, _handle_window);

#ifdef HOST_RAW_MOUSE
	if (enabled)
		host_raw_mouse_start();
#endif
	return object ? SDL_SetWindowRelativeMouseMode(object, enabled != 0) : 0;
}

/* ---------- events */

int host_sdl_poll_event(void *event)
{
	SDL_Event host_event;

#if defined(__APPLE__) && TARGET_OS_IPHONE
	/* (the game waits here while the app is in the background) */
	host_app_background_wait();
#elif defined(__APPLE__)
	/* (the macOS app's updater: port/macos/host_app.m; not in the Metal
	tests, which link no host_app.m) */
	if (host_app_update_tick)
		host_app_update_tick();
#endif
#ifdef HOST_RAW_MOUSE
	host_raw_mouse_cursor();
#endif
	if (!SDL_PollEvent(&host_event))
	{
#ifdef HOST_RAW_MOUSE
		/* (the counts that came since the last motion, as one more, read as
		late as the game reads its events: host_raw_mouse.m) */
		SDL_Window *focus = SDL_GetMouseFocus();
		float x, y;

		if (!focus || !SDL_GetWindowRelativeMouseMode(focus) || !host_raw_mouse_take(&x, &y) ||
			(x == 0.0f && y == 0.0f))
			return 0;
		SDL_zero(host_event);
		host_event.type = SDL_EVENT_MOUSE_MOTION;
		host_event.motion.timestamp = SDL_GetTicksNS();
		host_event.motion.windowID = SDL_GetWindowID(focus);
		host_event.motion.which = 1;
		host_event.motion.state = SDL_GetMouseState(&host_event.motion.x, &host_event.motion.y);
		host_event.motion.xrel = x;
		host_event.motion.yrel = y;
		memcpy(event, &host_event, sizeof(host_event));
		return 1;
#else
		return 0;
#endif
	}
#ifdef HOST_RAW_MOUSE
	/* (captured, the mouse's motion is its own counts, not the system's
	accelerated pointer's: host_raw_mouse.m) */
	if (host_event.type == SDL_EVENT_MOUSE_MOTION && host_event.motion.which != SDL_TOUCH_MOUSEID)
	{
		SDL_Window *window = SDL_GetWindowFromID(host_event.motion.windowID);
		float x, y;

		if (window && SDL_GetWindowRelativeMouseMode(window) && host_raw_mouse_take(&x, &y))
		{
			host_event.motion.xrel = x;
			host_event.motion.yrel = y;
		}
	}
#endif
#ifdef HOST_SYSTEM_KEYS
	/* (each key's place on the keyboard, as the system's state names it:
	host_sdl_input_held) */
	if (host_event.type == SDL_EVENT_KEY_DOWN && host_event.key.scancode < SDL_SCANCODE_COUNT)
		system_key_codes[host_event.key.scancode] = host_event.key.raw;
#endif
	/* the layouts agree except for the pointers of text, drop and user
	events, which the guest does not read */
	memcpy(event, &host_event, sizeof(host_event));
	return 1;
}

/* ---------- the keys and buttons held */

#ifdef HOST_SYSTEM_KEYS
/* the system's own state of a key, by its place on the keyboard (the
key's raw code in SDL's events, a modifier's from the table: SDL's events
of the modifiers carry none); -1 where it is not known (an ISO keyboard's
two keys whose codes SDL exchanges, a key not seen down) */
static int system_key_held(int scancode)
{
	int code;

	switch (scancode)
	{
	case SDL_SCANCODE_LSHIFT: code = 56; break;
	case SDL_SCANCODE_RSHIFT: code = 60; break;
	case SDL_SCANCODE_LCTRL: code = 59; break;
	case SDL_SCANCODE_RCTRL: code = 62; break;
	case SDL_SCANCODE_LALT: code = 58; break;
	case SDL_SCANCODE_RALT: code = 61; break;
	case SDL_SCANCODE_LGUI: code = 55; break;
	case SDL_SCANCODE_RGUI: code = 54; break;
	default:
		code = system_key_codes[scancode];
		if (!code || code == 10 || code == 50 || code > 127)
			return -1;
		break;
	}
	return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, (CGKeyCode)code) ? 1 : 0;
}

/* the system's own state of a mouse button (SDL's numbering), -1 where it
is not known */
static int system_button_held(int button)
{
	static const int buttons[] = { -1, kCGMouseButtonLeft, kCGMouseButtonCenter, kCGMouseButtonRight, 3, 4 };

	if (button <= 0 || button >= (int)(sizeof(buttons) / sizeof(buttons[0])))
		return -1;
	return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, (CGMouseButton)buttons[button]) ? 1 : 0;
}

/* how long SDL may hold what the system has let go of before it counts as
let go: its event can be that long on the way on a loaded machine (it then
lets go as it comes, and nothing is lost) */
#define SYSTEM_RELEASE_NS 250000000ull
#endif

/* what SDL holds now (SDL_GetKeyboardState, SDL_GetMouseState), for the
game's held keys and buttons, which its events alone could leave held when
an event is lost (sdl_platform.c: platform_pump_events): keys[scancode],
for count scancodes, bit 0 when SDL holds the key, bit 1 when the system
has had it up for SYSTEM_RELEASE_NS though SDL holds it (macOS, whose key
up can fail to come); the mouse buttons SDL holds (SDL_BUTTON_MASK) in the
low 16 bits returned, those the system has had up so long in the high 16.
The system's state of a key (or a button) counts only once it has been
seen to agree with SDL's that it is held: where the app may not read it, it
says every key is up, and nothing is let go of. */
uint32_t host_sdl_input_held(uint8_t *keys, int count)
{
	int numkeys = 0, scancode;
	const bool *state = SDL_GetKeyboardState(&numkeys);
	uint32_t buttons = (uint32_t)SDL_GetMouseState(NULL, NULL) & 0xffffu;
#ifdef HOST_SYSTEM_KEYS
	static uint64_t key_up_since[SDL_SCANCODE_COUNT], button_up_since[16];
	static uint8_t key_readable[SDL_SCANCODE_COUNT], button_readable[16];
	uint64_t now = SDL_GetTicksNS();
	int button;
#endif

	if (count > SDL_SCANCODE_COUNT)
		count = SDL_SCANCODE_COUNT;
	for (scancode = 0; scancode < count; scancode++)
	{
		keys[scancode] = scancode < numkeys && state && state[scancode] ? 1 : 0;
#ifdef HOST_SYSTEM_KEYS
		if (keys[scancode])
		{
			int system = system_key_held(scancode);

			if (system == 1)
				key_readable[scancode] = 1;
			if (system != 0)
				key_up_since[scancode] = 0;
			else if (!key_up_since[scancode])
				key_up_since[scancode] = now;
			else if (key_readable[scancode] && now - key_up_since[scancode] >= SYSTEM_RELEASE_NS)
				keys[scancode] |= 2;
		}
		else
			key_up_since[scancode] = 0;
#endif
	}
#ifdef HOST_SYSTEM_KEYS
	for (button = 1; button < 16; button++)
	{
		if (buttons & SDL_BUTTON_MASK(button))
		{
			int system = system_button_held(button);

			if (system == 1)
				button_readable[button] = 1;
			if (system != 0)
				button_up_since[button] = 0;
			else if (!button_up_since[button])
				button_up_since[button] = now;
			else if (button_readable[button] && now - button_up_since[button] >= SYSTEM_RELEASE_NS)
				buttons |= (uint32_t)SDL_BUTTON_MASK(button) << 16;
		}
		else
			button_up_since[button] = 0;
	}
#endif
	return buttons;
}

/* ---------- gamepads */

int host_sdl_get_gamepads(uint32_t *ids, int capacity)
{
	int count = 0, index;
	SDL_JoystickID *list = SDL_GetGamepads(&count);

	if (!list)
		return 0;
	if (count > capacity)
		count = capacity;
	for (index = 0; index < count; index++)
		ids[index] = list[index];
	SDL_free(list);
	return count;
}

uint32_t host_sdl_open_gamepad(uint32_t id)
{
	SDL_Gamepad *gamepad = SDL_OpenGamepad((SDL_JoystickID)id);

	if (gamepad)
		host_logf(HOST_LOG_INFO, "gamepad %u: %s (type %d, %04x:%04x)", (unsigned)id, SDL_GetGamepadName(gamepad),
			(int)SDL_GetGamepadType(gamepad), SDL_GetGamepadVendor(gamepad), SDL_GetGamepadProduct(gamepad));
	return host_sdl_handle_new(_handle_gamepad, gamepad);
}

uint32_t host_sdl_gamepad_from_id(uint32_t id)
{
	return host_sdl_handle_new(_handle_gamepad, SDL_GetGamepadFromID((SDL_JoystickID)id));
}

/* (the event thread, when the controller goes: sdl_platform.c) */
void host_sdl_close_gamepad(uint32_t gamepad)
{
	SDL_Gamepad *object = handle_release(gamepad, _handle_gamepad, NULL);

	if (object)
		SDL_CloseGamepad(object);
}

int host_sdl_gamepad_axis(uint32_t gamepad, int axis)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GetGamepadAxis(object, (SDL_GamepadAxis)axis) : 0;
}

int host_sdl_gamepad_button(uint32_t gamepad, int button)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GetGamepadButton(object, (SDL_GamepadButton)button) : 0;
}

int host_sdl_gamepad_type(uint32_t gamepad)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GetGamepadType(object) : SDL_GAMEPAD_TYPE_UNKNOWN;
}

int host_sdl_rumble_gamepad(uint32_t gamepad, uint32_t low, uint32_t high, uint32_t milliseconds)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);

	return object ? SDL_RumbleGamepad(object, (Uint16)low, (Uint16)high, milliseconds) : 0;
}

/* (gyro aiming: port/linux/src/xinput_sdl.c) */
int host_sdl_gamepad_has_sensor(uint32_t gamepad, int type)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);

	return object ? SDL_GamepadHasSensor(object, (SDL_SensorType)type) : 0;
}

int host_sdl_set_gamepad_sensor_enabled(uint32_t gamepad, int type, int enabled)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);
	int done;

	if (!object)
		return 0;
	done = SDL_SetGamepadSensorEnabled(object, (SDL_SensorType)type, enabled != 0);
	if (enabled)
		host_logf(HOST_LOG_INFO, "gamepad %s: sensor %d on%s (%.0f samples a second)", SDL_GetGamepadName(object),
			type, done ? "" : " failed", SDL_GetGamepadSensorDataRate(object, (SDL_SensorType)type));
	return done;
}

int host_sdl_gamepad_steam(uint32_t gamepad)
{
	SDL_Gamepad *object = host_sdl_handle_get(gamepad, _handle_gamepad);

	return object && SDL_GetGamepadSteamHandle(object) != 0;
}

/* ---------- audio */

/* SDL calls audio_callback on its own audio thread, which cannot run guest
code; it passes each request to the stream's thread (audio_thread), which
can, and waits for it to be done. SDL holds the stream's lock throughout, so
the audio the guest puts into the stream meanwhile (from audio_thread) is
kept in the binding's buffer instead, and put in by audio_callback once the
guest is done: audio_thread putting it in itself would wait for the lock
forever */
struct audio_binding
{
	uint32_t handle;
	uint32_t callback;
	uint32_t userdata;
	pthread_mutex_t lock;
	pthread_cond_t requested;
	pthread_cond_t done;
	int pending;
	/* the stream is destroyed: the thread frees the binding and ends */
	int quit;
	int additional;
	int total;
	/* when the request was passed to the thread (HALO_AUDIO_MIX_TIMES) */
	uint64_t requested_ns;
	unsigned char *buffer;
	int buffer_length;
	int buffer_size;
#ifdef HOST_AUDIO_OUTPUT
	/* the stream, the output unit's set-up the thread was last made ready
	for, and its output, found once (host_audio_output_thread) */
	SDL_AudioStream *stream;
	int output_setups;
	void *output;
#endif
};

/* the binding whose callback this thread is running, if any */
static __thread struct audio_binding *calling_back;

/* (HALO_AUDIO_MIX_TIMES) the longest the device's callback took, from its
call to its return, since audio_mix_timed last logged */
static _Atomic uint64_t audio_callback_most_ns;

/* HALO_AUDIO_MIX_TIMES=<seconds>: the guest's mixes timed (the audio
thread's, each request's) and logged that often: their count, mean, 99th
percentile and most, and the bytes asked for (for measuring: a mix longer
than its buffer's time is a buffer the device ran dry for) */
static void audio_mix_timed(uint64_t nanoseconds, int bytes)
{
	static int wanted = -1;
	static uint64_t period, began, most, total, times[4096];
	static unsigned long count, asked;

	if (wanted < 0)
	{
		const char *seconds = getenv("HALO_AUDIO_MIX_TIMES");

		wanted = seconds && atof(seconds) > 0.0;
		period = wanted ? (uint64_t)(atof(seconds) * 1e9) : 0;
	}
	if (!wanted)
		return;
	if (!began)
		began = SDL_GetTicksNS();
	if (count < sizeof(times) / sizeof(times[0]))
		times[count] = nanoseconds;
	count++;
	total += nanoseconds;
	asked += bytes > 0 ? (unsigned long)bytes : 0;
	most = nanoseconds > most ? nanoseconds : most;
	if (SDL_GetTicksNS() - began >= period)
	{
		unsigned long kept = count < sizeof(times) / sizeof(times[0]) ? count : sizeof(times) / sizeof(times[0]);
		unsigned long index, above = 0;
		uint64_t high;

		/* (the 99th percentile: the least of the top 1%, by counting) */
		for (high = most; high > 0; high -= high / 64 + 1)
		{
			for (index = 0, above = 0; index < kept; index++)
				above += times[index] >= high;
			if (above * 100 >= kept)
				break;
		}
		host_logf(HOST_LOG_INFO, "sound: %lu mixes in %.1f s, %.3f ms each on average, %.3f at the 99th percentile, "
			"%.3f at most; %lu bytes asked for each; the device's callback %.3f ms at most", count,
			(double)(SDL_GetTicksNS() - began) / 1e9, (double)total / count / 1e6, (double)high / 1e6,
			(double)most / 1e6, asked / count,
			(double)atomic_exchange(&audio_callback_most_ns, 0) / 1e6);
		began = SDL_GetTicksNS();
		count = asked = 0;
		total = most = 0;
	}
}

/* what the thread running the mix did meanwhile (HALO_AUDIO_MIX_TIMES): its
processor time, the page faults (the process's on a Mac, the thread's on
Linux) and pages read in, the times it was switched out (Linux: its
involuntary switches), and its priority (a Mac's: 97 while real-time) */
struct audio_mix_usage
{
	uint64_t cpu_ns;
	uint64_t faults, pageins, switches;
	int priority;
};

static void audio_mix_usage(struct audio_mix_usage *usage)
{
	memset(usage, 0, sizeof(*usage));
#ifdef HOST_AUDIO_OUTPUT
	{
		uint64_t values[5];

		host_audio_output_usage(values);
		usage->cpu_ns = values[0];
		usage->faults = values[1];
		usage->pageins = values[2];
		usage->switches = values[3];
		usage->priority = (int)values[4];
	}
#elif defined(__linux__)
	{
		struct timespec now;
		struct rusage rusage;

		if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0)
			usage->cpu_ns = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
		if (getrusage(RUSAGE_THREAD, &rusage) == 0)
		{
			usage->faults = (uint64_t)rusage.ru_minflt + (uint64_t)rusage.ru_majflt;
			usage->pageins = (uint64_t)rusage.ru_majflt;
			usage->switches = (uint64_t)rusage.ru_nivcsw;
		}
	}
#endif
}

/* HALO_AUDIO_MIX_TIMES: a mix over HALO_AUDIO_MIX_SLOW_MS (2 by default)
logged by its request's number (the guest's mixer logs its parts by the same
number): its time, the thread's processor time in it, the faults, pages read
in and switches meanwhile, its priority, and how long the thread took to start
on the request */
static int audio_mix_watching(uint64_t *slow_ns)
{
	static int wanted = -1;
	static uint64_t slow;

	if (wanted < 0)
	{
		const char *seconds = getenv("HALO_AUDIO_MIX_TIMES");
		const char *milliseconds = getenv("HALO_AUDIO_MIX_SLOW_MS");

		slow = (uint64_t)((milliseconds && atof(milliseconds) > 0.0 ? atof(milliseconds) : 2.0) * 1e6);
		wanted = seconds && atof(seconds) > 0.0;
	}
	*slow_ns = slow;
	return wanted;
}

#ifdef _WIN32
/* port/windows/host/host_thread_win.c: the calling thread made MMCSS's "Pro
Audio" task (its handle; NULL, it could not be), and made itself again */
void *host_thread_pro_audio_begin(void);
void host_thread_pro_audio_end(void *task);
#endif

static void *audio_thread(void *context)
{
	unsigned long requests = 0;
	struct audio_binding *binding = context;

#ifdef _WIN32
	void *pro_audio;
#endif

#ifndef __ANDROID__
	/* (SDL's audio thread waits on this one while the guest mixes: at its
	priority, so that a busy computer, a build running, keeps the mix in
	time, as SDL keeps its own; a small audio.buffer_frames needs it) */
	SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
#endif
#ifdef _WIN32
	/* (and on Windows, MMCSS's "Pro Audio" task, above that priority, as
	Windows's own audio engine's threads are, where it gives one) */
	pro_audio = host_thread_pro_audio_begin();
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
#endif
	pthread_mutex_lock(&binding->lock);
	for (;;)
	{
		int additional, total;

		while (!binding->pending && !binding->quit)
			pthread_cond_wait(&binding->requested, &binding->lock);
		if (binding->quit)
			break;
		additional = binding->additional;
		total = binding->total;
		pthread_mutex_unlock(&binding->lock);
#ifdef HOST_AUDIO_OUTPUT
		host_audio_output_thread(binding->stream, &binding->output_setups, &binding->output);
#endif
		calling_back = binding;
		requests++;
		{
			uint64_t began = SDL_GetTicksNS(), took, slow_ns;
			int watching = audio_mix_watching(&slow_ns);
			struct audio_mix_usage before, after;

			if (watching)
				audio_mix_usage(&before);
			host_call_guest(binding->callback, binding->userdata, binding->handle, (uint32_t)additional,
				(uint32_t)total);
			took = SDL_GetTicksNS() - began;
			audio_mix_timed(took, additional);
			calling_back = NULL;
			if (watching)
				audio_mix_usage(&after);
			pthread_mutex_lock(&binding->lock);
			binding->pending = 0;
			pthread_cond_signal(&binding->done);
			/* (logged after the buffer is handed back) */
			if (watching && took >= slow_ns)
			{
				pthread_mutex_unlock(&binding->lock);
				host_logf(HOST_LOG_INFO, "sound: slow mix (request %lu): %.3f ms, %.3f ms of it on the processor; "
					"started %.3f ms after the request; %llu faults, %llu pages read in, %llu switches meanwhile; "
					"priority %d (%d before)", requests, (double)took / 1e6,
					(double)(after.cpu_ns - before.cpu_ns) / 1e6, (double)(began - binding->requested_ns) / 1e6,
					(unsigned long long)(after.faults - before.faults),
					(unsigned long long)(after.pageins - before.pageins),
					(unsigned long long)(after.switches - before.switches), after.priority, before.priority);
				pthread_mutex_lock(&binding->lock);
			}
		}
	}
	pthread_mutex_unlock(&binding->lock);
#ifdef _WIN32
	host_thread_pro_audio_end(pro_audio);
#endif
	pthread_cond_destroy(&binding->done);
	pthread_cond_destroy(&binding->requested);
	pthread_mutex_destroy(&binding->lock);
	SDL_free(binding->buffer);
	SDL_free(binding);
	return NULL;
}

static void SDLCALL audio_callback(void *userdata, SDL_AudioStream *stream, int additional, int total)
{
	struct audio_binding *binding = userdata;
	uint64_t slow_ns, took;
	int watching = audio_mix_watching(&slow_ns);
	uint64_t called = watching ? SDL_GetTicksNS() : 0;

	pthread_mutex_lock(&binding->lock);
	binding->additional = additional;
	binding->total = total;
	binding->requested_ns = SDL_GetTicksNS();
	binding->pending = 1;
	pthread_cond_signal(&binding->requested);
	while (binding->pending)
		pthread_cond_wait(&binding->done, &binding->lock);
	pthread_mutex_unlock(&binding->lock);
	if (binding->buffer_length)
	{
		SDL_PutAudioStreamData(stream, binding->buffer, binding->buffer_length);
		binding->buffer_length = 0;
	}
	if (!watching)
		return;
	took = SDL_GetTicksNS() - called;
	if (took > atomic_load_explicit(&audio_callback_most_ns, memory_order_relaxed))
		atomic_store_explicit(&audio_callback_most_ns, took, memory_order_relaxed);
}

/* audio the guest puts into its stream during the stream's callback, for
audio_callback to put in; 1 on success */
static int audio_keep(struct audio_binding *binding, const void *data, int length)
{
	if (length < 0)
		return 0;
	if (binding->buffer_length + length > binding->buffer_size)
	{
		int size = (binding->buffer_length + length) * 2;
		unsigned char *buffer = SDL_realloc(binding->buffer, (size_t)size);

		if (!buffer)
			return 0;
		binding->buffer = buffer;
		binding->buffer_size = size;
	}
	memcpy(binding->buffer + binding->buffer_length, data, (size_t)length);
	binding->buffer_length += length;
	return 1;
}

/* (returns once a callback that is running has finished) */
static void audio_stream_destroy(SDL_AudioStream *stream)
{
#ifdef HOST_AUDIO_OUTPUT
	if (host_audio_output_close(stream))
		return;
#endif
	SDL_DestroyAudioStream(stream);
}

uint32_t host_sdl_open_audio_stream(uint32_t device, const void *spec, uint32_t callback, uint32_t userdata)
{
	struct audio_binding *binding;
	SDL_AudioStream *stream;

	/* (one without a callback, the microphone's, which the guest reads:
	nothing to bind) */
	if (!callback)
	{
		stream = SDL_OpenAudioDeviceStream((SDL_AudioDeviceID)device, spec, NULL, NULL);
		return stream ? host_sdl_handle_new(_handle_audio, stream) : 0;
	}
	binding = SDL_calloc(1, sizeof(*binding));
	if (!binding)
		return 0;
	binding->callback = callback;
	binding->userdata = userdata;
	pthread_mutex_init(&binding->lock, NULL);
	pthread_cond_init(&binding->requested, NULL);
	pthread_cond_init(&binding->done, NULL);
	stream = NULL;
#ifdef HOST_AUDIO_OUTPUT
	stream = host_audio_output_open((SDL_AudioDeviceID)device, spec, audio_callback, binding);
#endif
	if (!stream)
		stream = SDL_OpenAudioDeviceStream((SDL_AudioDeviceID)device, spec, audio_callback, binding);
	if (!stream)
	{
		SDL_free(binding);
		return 0;
	}
#ifdef HOST_AUDIO_OUTPUT
	binding->stream = stream;
#endif
	/* the device starts paused, so no callback can run before this */
	binding->handle = host_sdl_handle_new(_handle_audio, stream);
	if (!binding->handle)
	{
		audio_stream_destroy(stream);
		pthread_cond_destroy(&binding->done);
		pthread_cond_destroy(&binding->requested);
		pthread_mutex_destroy(&binding->lock);
		SDL_free(binding);
		return 0;
	}
	pthread_mutex_lock(&handle_lock);
	handles[binding->handle].binding = binding;
	pthread_mutex_unlock(&handle_lock);
	if (host_native_thread_create(audio_thread, binding, 256 * 1024) != 0)
		host_fatal("cannot start the audio thread");
	return binding->handle;
}

int host_sdl_put_audio_stream_data(uint32_t stream, const void *data, int length)
{
	SDL_AudioStream *object;

	/* (the mix, during the device's callback: kept without the handles'
	lock, which the game's threads take, so that the device's thread never
	waits on one the scheduler has set aside holding it) */
	if (calling_back && calling_back->handle == stream)
		return audio_keep(calling_back, data, length);
	object = host_sdl_handle_get(stream, _handle_audio);
	if (!object)
		return 0;
	return SDL_PutAudioStreamData(object, data, length);
}

int host_sdl_resume_audio_stream_device(uint32_t stream)
{
	SDL_AudioStream *object = host_sdl_handle_get(stream, _handle_audio);

#ifdef HOST_AUDIO_OUTPUT
	int resumed = host_audio_output_resume(object);

	if (resumed >= 0)
		return resumed;
#endif
	return object ? SDL_ResumeAudioStreamDevice(object) : 0;
}

/* (voice chat's microphone: port/linux/src/voice_audio.c) */
int host_sdl_get_audio_stream_data(uint32_t stream, void *data, int length)
{
	SDL_AudioStream *object = host_sdl_handle_get(stream, _handle_audio);

	return object ? SDL_GetAudioStreamData(object, data, length) : -1;
}

int host_sdl_get_audio_stream_available(uint32_t stream)
{
	SDL_AudioStream *object = host_sdl_handle_get(stream, _handle_audio);

	return object ? SDL_GetAudioStreamAvailable(object) : -1;
}

/* (a stream with a callback has a thread the guest's callbacks run on:
once no callback can run any more, it is told to end, and frees the
binding and its stack as it goes) */
void host_sdl_destroy_audio_stream(uint32_t stream)
{
	void *context;
	SDL_AudioStream *object = handle_release(stream, _handle_audio, &context);
	struct audio_binding *binding = context;

	if (!object)
		return;
	/* (returns once a callback that is running has finished) */
	audio_stream_destroy(object);
	if (binding)
	{
		pthread_mutex_lock(&binding->lock);
		binding->quit = 1;
		pthread_cond_signal(&binding->requested);
		pthread_mutex_unlock(&binding->lock);
	}
}

#if defined(__APPLE__) && !TARGET_OS_IPHONE
/* the latency CoreAudio reports for the default output device (microseconds;
-1 not known): the device's own and its stream's, the safety offset, and
its I/O buffer, which is filled a cycle ahead of the hardware */
static int system_output_latency(void)
{
	AudioObjectPropertyAddress address = { kAudioHardwarePropertyDefaultOutputDevice,
		kAudioObjectPropertyScopeGlobal, 0 };
	AudioDeviceID device = kAudioObjectUnknown;
	AudioStreamID streams[8];
	UInt32 size = sizeof(device), value, frames = 0;
	Float64 rate = 0.0;

	if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, NULL, &size, &device) != noErr ||
		device == kAudioObjectUnknown)
		return -1;
	address.mScope = kAudioObjectPropertyScopeOutput;
	address.mSelector = kAudioDevicePropertyNominalSampleRate;
	size = sizeof(rate);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &rate) != noErr || rate <= 0.0)
		return -1;
	address.mSelector = kAudioDevicePropertyLatency;
	size = sizeof(value);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &value) == noErr)
		frames += value;
	address.mSelector = kAudioDevicePropertySafetyOffset;
	size = sizeof(value);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &value) == noErr)
		frames += value;
	address.mSelector = kAudioDevicePropertyBufferFrameSize;
	size = sizeof(value);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &value) == noErr)
		frames += value;
	address.mSelector = kAudioDevicePropertyStreams;
	size = sizeof(streams);
	if (AudioObjectGetPropertyData(device, &address, 0, NULL, &size, streams) == noErr && size >= sizeof(streams[0]))
	{
		address.mSelector = kAudioStreamPropertyLatency;
		address.mScope = kAudioObjectPropertyScopeGlobal;
		size = sizeof(value);
		if (AudioObjectGetPropertyData(streams[0], &address, 0, NULL, &size, &value) == noErr)
			frames += value;
	}
	return (int)((double)frames * 1.0e6 / rate);
}
#elif defined(__APPLE__) && TARGET_OS_IPHONE
/* iOS's: the audio session's output latency and I/O buffer
(port/ios/host_app.m) */
static int system_output_latency(void)
{
	return host_app_audio_output_latency();
}
#else
static int system_output_latency(void)
{
	return -1;
}
#endif

/* (debug.latency_log, dsound_sdl.c) the device a stream plays on: its
driver's name into driver (size bytes), and into timing: [0] its buffer
(sample frames), [1] its rate, [2] the time SDL's driver keeps queued
ahead of the buffer it asks for (microseconds), [3] the system's own
latency after that, as it reports it (microseconds; -1 not known), [4] the
times the Mac's output unit was set up for a device (0 otherwise). The
queue is SDL 3.4's: CoreAudio's AudioQueue keeps three buffers, or as many
pairs as make 15 ms (all but the one asked for ahead); PipeWire's stream
(not on its real-time thread) a quantum, the buffer; PulseAudio's target
length is the buffer, as are WASAPI's and ALSA's periods. On the Mac the
stream plays through CoreAudio's output unit instead (driver "coreaudio
unit", port/macos/host_audio_output.c), which queues nothing. 1 on success */
int host_sdl_audio_stream_timing(uint32_t stream, char *driver, int driver_size, int *timing)
{
	SDL_AudioStream *object = host_sdl_handle_get(stream, _handle_audio);
	const char *name = SDL_GetCurrentAudioDriver();
	SDL_AudioSpec spec;
	int frames = 0;
	double queued = 0.0;

#ifdef HOST_AUDIO_OUTPUT
	if (object && timing && host_audio_output_timing(object, timing))
	{
		if (driver && driver_size > 0)
			SDL_strlcpy(driver, "coreaudio unit", (size_t)driver_size);
		return 1;
	}
#endif
	if (!object || !timing || !SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(object), &spec, &frames) ||
		spec.freq <= 0 || frames <= 0)
		return 0;
	if (driver && driver_size > 0)
		SDL_strlcpy(driver, name ? name : "", (size_t)driver_size);
	if (name && !SDL_strcmp(name, "coreaudio"))
	{
		double milliseconds = (double)frames * 1000.0 / spec.freq;
		int buffers = milliseconds < 15.0 ? (int)SDL_ceil(15.0 / milliseconds) * 2 : 3;

		queued = (double)(buffers - 1) * frames;
	}
	else if (name && (!SDL_strcmp(name, "pipewire") || !SDL_strcmp(name, "pulseaudio") ||
		!SDL_strcmp(name, "wasapi") || !SDL_strcmp(name, "alsa") || !SDL_strcmp(name, "aaudio")))
	{
		queued = frames;
	}
	timing[0] = frames;
	timing[1] = spec.freq;
	timing[2] = (int)(queued * 1.0e6 / spec.freq);
	timing[3] = name && !SDL_strcmp(name, "coreaudio") ? system_output_latency() : -1;
	timing[4] = 0;
	return 1;
}

/* ---------- the clipboard (internet play's invite links) */

int host_sdl_set_clipboard_text(const char *text)
{
	return SDL_SetClipboardText(text) ? 1 : 0;
}

void host_sdl_get_clipboard_text(char *buffer, uint32_t size)
{
	char *text = SDL_GetClipboardText();

	SDL_strlcpy(buffer, text ? text : "", size);
	SDL_free(text);
}

int host_sdl_open_url(const char *url)
{
	return SDL_OpenURL(url) ? 1 : 0;
}

int host_sdl_show_toast(const char *message, int duration, int gravity, int x, int y)
{
#ifdef __ANDROID__
	return SDL_ShowAndroidToast(message, duration, gravity, x, y) ? 1 : 0;
#else
	/* (the desktop guest shows none: Linux arm64, port/linux/host) */
	(void)message;
	(void)duration;
	(void)gravity;
	(void)x;
	(void)y;
	return 0;
#endif
}

/* ---------- a message for the player (a host of another network version) */

int host_sdl_show_simple_message_box(uint32_t flags, const char *title, const char *message)
{
	return SDL_ShowSimpleMessageBox((SDL_MessageBoxFlags)flags, title, message, NULL) ? 1 : 0;
}

/* ---------- a question for the player (the mods at startup: port_mods.c) */

int host_sdl_ask(uint32_t flags, const char *title, const char *message, int default_index,
	const char *labels, uint32_t size)
{
	SDL_MessageBoxButtonData buttons[3];
	SDL_MessageBoxData question;
	uint32_t used = 0;
	int count = 0;
	int answer = -1;

	if (!labels || !size || size > 1024)
		return -1;
	while (count < 3 && used < size)
	{
		const char *end = memchr(labels + used, 0, size - used);

		if (!end)
			return -1;
		buttons[count].flags = count == default_index ?
			SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0;
		buttons[count].buttonID = count;
		buttons[count].text = labels + used;
		used = (uint32_t)(end - labels) + 1;
		count++;
	}
	if (used != size)
		return -1;
	question.flags = (SDL_MessageBoxFlags)flags;
	question.window = NULL;
	question.title = title;
	question.message = message;
	question.numbuttons = count;
	question.buttons = buttons;
	question.colorScheme = NULL;
	return SDL_ShowMessageBox(&question, &answer) ? answer : -1;
}
