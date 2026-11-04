/*
GUEST_SDL.C

The SDL3 functions the platform layer calls (sdl_platform.c, xinput_sdl.c,
dsound_sdl.c), for the guest. SDL itself runs in the host; objects it
returns (windows, contexts, gamepads, audio streams) are 64-bit pointers
there, so the guest only ever sees small integer handles that the host maps
back (host_sdl.c). Events are written by SDL straight into the guest's
buffer: SDL_Event has the same 128-byte layout in both ABIs for every event
type that carries no pointer, which covers all the platform layer reads.
*/

/* before SDL: musl's alloca.h must come first */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <SDL3/SDL.h>

#include "guest_host.h"
#include "posix.h"

bool SDL_Init(SDL_InitFlags flags)
{
	return host_sdl_init(flags) != 0;
}

bool SDL_SetHint(const char *name, const char *value)
{
	return host_sdl_set_hint(name, value) != 0;
}

const char *SDL_GetError(void)
{
	static __thread char buffer[256];

	host_sdl_get_error(buffer, sizeof(buffer));
	return buffer;
}

const char *SDL_GetScancodeName(SDL_Scancode scancode)
{
	static __thread char buffer[64];

	host_sdl_scancode_name((int)scancode, buffer, sizeof(buffer));
	return buffer;
}

SDL_Scancode SDL_GetScancodeFromName(const char *name)
{
	return (SDL_Scancode)host_sdl_scancode_from_name(name);
}

Uint64 SDL_GetTicks(void)
{
	return (Uint64)host_sdl_ticks();
}

Uint64 SDL_GetTicksNS(void)
{
	return (Uint64)host_sdl_ticks_ns();
}

SDL_ThreadID SDL_GetCurrentThreadID(void)
{
	return (SDL_ThreadID)host_sdl_thread_id();
}

void SDL_free(void *memory)
{
	free(memory);
}

/* ---------- the clipboard (internet play's invite links, sdl_platform.c) */

bool SDL_SetClipboardText(const char *text)
{
	return host_sdl_set_clipboard_text(text) != 0;
}

char *SDL_GetClipboardText(void)
{
	char buffer[1024];

	host_sdl_get_clipboard_text(buffer, sizeof(buffer));
	return strdup(buffer);
}

/* ---------- the browser (a Discord link's page: player_link.c) */

bool SDL_OpenURL(const char *url)
{
	return host_sdl_open_url(url) != 0;
}

bool SDL_ShowAndroidToast(const char *message, int duration, int gravity, int xoffset, int yoffset)
{
	return host_sdl_show_toast(message, duration, gravity, xoffset, yoffset) != 0;
}

/* ---------- a message for the player (sdl_platform.c): the host's own window */

bool SDL_ShowSimpleMessageBox(SDL_MessageBoxFlags flags, const char *title, const char *message, SDL_Window *window)
{
	(void)window;
	return host_sdl_show_simple_message_box((unsigned int)flags, title, message) != 0;
}

#ifdef HALO_ANDROID
/* (up to three buttons, their ids those of the guest's; the desktop builds
use guest_desktop.c's message box bridge) */
bool SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *buttonid)
{
	char labels[1024];
	size_t used = 0;
	int default_index = -1;
	int index;
	int chosen;

	if (!data || !data->buttons || data->numbuttons < 1 || data->numbuttons > 3)
		return false;
	for (index = 0; index < data->numbuttons; index++)
	{
		const char *label = data->buttons[index].text ? data->buttons[index].text : "";
		size_t length = strlen(label) + 1;

		if (length > sizeof(labels) - used)
			return false;
		memcpy(labels + used, label, length);
		used += length;
		if (data->buttons[index].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT)
			default_index = index;
	}
	chosen = host_sdl_ask((unsigned int)data->flags, data->title, data->message, default_index,
		labels, (unsigned int)used);
	if (chosen < 0 || chosen >= data->numbuttons)
		return false;
	if (buttonid)
		*buttonid = data->buttons[chosen].buttonID;
	return true;
}

#endif

/* (display.battery_saver's "auto": battery_saver.c) */
SDL_PowerState SDL_GetPowerInfo(int *seconds, int *percent)
{
	int left = -1, charge = -1;
	SDL_PowerState state = (SDL_PowerState)host_sdl_power_info(&left, &charge);

	if (seconds)
		*seconds = left;
	if (percent)
		*percent = charge;
	return state;
}

void SDL_Delay(Uint32 milliseconds)
{
	struct timespec duration;

	duration.tv_sec = milliseconds / 1000;
	duration.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
	nanosleep(&duration, NULL);
}

/* ---------- video */

SDL_Window *SDL_CreateWindow(const char *title, int width, int height, SDL_WindowFlags flags)
{
	return (SDL_Window *)host_sdl_create_window(title, width, height, (long long)flags);
}

bool SDL_GetWindowSizeInPixels(SDL_Window *window, int *width, int *height)
{
	int w = 0, h = 0;

	host_sdl_window_size_in_pixels((unsigned int)window, &w, &h);
	if (width)
		*width = w;
	if (height)
		*height = h;
	return true;
}

bool SDL_SetWindowRelativeMouseMode(SDL_Window *window, bool enabled)
{
	return host_sdl_set_relative_mouse((unsigned int)window, enabled) != 0;
}

/* ---------- events */

bool SDL_PollEvent(SDL_Event *event)
{
	SDL_Event scratch;

	return host_sdl_poll_event(event ? event : &scratch) != 0;
}

/* ---------- gamepads */

SDL_JoystickID *SDL_GetGamepads(int *count)
{
	unsigned int ids[16];
	int found = host_sdl_get_gamepads(ids, 16);
	SDL_JoystickID *result = malloc((found + 1) * sizeof(SDL_JoystickID));
	int index;

	if (!result)
		return NULL;
	for (index = 0; index < found; index++)
		result[index] = ids[index];
	result[found] = 0;
	if (count)
		*count = found;
	return result;
}

SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id)
{
	return (SDL_Gamepad *)host_sdl_open_gamepad(id);
}

SDL_Gamepad *SDL_GetGamepadFromID(SDL_JoystickID id)
{
	return (SDL_Gamepad *)host_sdl_gamepad_from_id(id);
}

Sint16 SDL_GetGamepadAxis(SDL_Gamepad *gamepad, SDL_GamepadAxis axis)
{
	return (Sint16)host_sdl_gamepad_axis((unsigned int)gamepad, (int)axis);
}

bool SDL_GetGamepadButton(SDL_Gamepad *gamepad, SDL_GamepadButton button)
{
	return host_sdl_gamepad_button((unsigned int)gamepad, (int)button) != 0;
}

SDL_GamepadType SDL_GetGamepadType(SDL_Gamepad *gamepad)
{
	return (SDL_GamepadType)host_sdl_gamepad_type((unsigned int)gamepad);
}

bool SDL_RumbleGamepad(SDL_Gamepad *gamepad, Uint16 low, Uint16 high, Uint32 milliseconds)
{
	return host_sdl_rumble_gamepad((unsigned int)gamepad, low, high, milliseconds) != 0;
}

void SDL_CloseGamepad(SDL_Gamepad *gamepad)
{
	host_sdl_close_gamepad((unsigned int)gamepad);
}

/* ---------- audio */

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID device, const SDL_AudioSpec *spec,
	SDL_AudioStreamCallback callback, void *userdata)
{
	return (SDL_AudioStream *)host_sdl_open_audio_stream(device, spec, (unsigned int)callback,
		(unsigned int)userdata);
}

bool SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *data, int length)
{
	return host_sdl_put_audio_stream_data((unsigned int)stream, data, length) != 0;
}

bool SDL_ResumeAudioStreamDevice(SDL_AudioStream *stream)
{
	return host_sdl_resume_audio_stream_device((unsigned int)stream) != 0;
}

int SDL_GetAudioStreamData(SDL_AudioStream *stream, void *data, int length)
{
	return host_sdl_get_audio_stream_data((unsigned int)stream, data, length);
}

int SDL_GetAudioStreamAvailable(SDL_AudioStream *stream)
{
	return host_sdl_get_audio_stream_available((unsigned int)stream);
}

void SDL_DestroyAudioStream(SDL_AudioStream *stream)
{
	host_sdl_destroy_audio_stream((unsigned int)stream);
}

/* (from the host's stat, port/linux/src/posix.h: whether a path is there, and
what it is; the game asks for the files and folders it looks for) */
bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct posix_file_information information;

	if (!path || posix_stat(path, &information) != 0)
	{
		if (info)
			memset(info, 0, sizeof(*info));
		return false;
	}
	if (info)
	{
		info->type = (information.flags & _posix_file_is_directory) ? SDL_PATHTYPE_DIRECTORY : SDL_PATHTYPE_FILE;
		info->size = ((Uint64)information.size_high << 32) | (Uint32)information.size_low;
		info->modify_time = (SDL_Time)information.modification_seconds * 1000000000 +
			(SDL_Time)information.modification_nanoseconds;
		info->access_time = (SDL_Time)information.access_seconds * 1000000000 +
			(SDL_Time)information.access_nanoseconds;
		info->create_time = (SDL_Time)information.creation_seconds * 1000000000 +
			(SDL_Time)information.creation_nanoseconds;
	}
	return true;
}
