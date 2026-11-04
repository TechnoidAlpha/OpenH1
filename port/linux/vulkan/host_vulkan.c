/*
HOST_VULKAN.C

The game's window drawn with Vulkan (the Linux desktop's Vulkan build,
HALO_VULKAN): the backend (xgpu_vulkan.c) made for the window, and the
command stream given it.

The guest makes the window with SDL_WINDOW_VULKAN (sdl_platform.c) and hands
it over (host_vulkan_window_open, a host import, before the renderer's
device is made): the backend's instance has the extensions SDL's surface
needs, the surface is SDL's, and the frames are shown through a swapchain.
A window SDL cannot draw with Vulkan (its offscreen driver, where the
device has no headless surfaces) is drawn without one: the frames are
drawn and read back (screenshots) but not shown.

The surface is made again when it is lost, and when the app comes back
from the background: Android takes the app's window (its ANativeWindow)
away when the app stops and gives it a new one when it returns, and SDL
makes again only EGL's surfaces. As the app goes (SDL's
WILL_ENTER_BACKGROUND and DID_ENTER_BACKGROUND, sent on the thread that
pumps SDL's events), the render thread lets go of the swapchain and the
surface (xgpu_vulkan_window_suspend) and saves the pipeline cache; the
frames drawn in the background are not shown. As it comes back
(DID_ENTER_FOREGROUND) the surface is made again, or, while the window is
not back yet (Android resumes the app before it makes the window's
surface), at a later frame.

The swap interval is display.vsync's (host_vulkan_window_open, asked again
when the setting changes); while battery saver is on (display.battery_saver,
battery_saver.c), each frame is shown for the whole number of the display's
refreshes that brings the frames a second to 30 to 60 (frame_pacing.h's
frame_pacing_divisor, at the window's display's rate as SDL has it when it
is asked). With OpenXR (HALO_VR), the headset's runtime
chooses the device; the window keeps its surface, so that the game can play
flat in it if VR cannot start, and a device the runtime refuses is made
again without it (flat).

The game's debugging settings for the graphics come with the window (the
host does not read the settings file): debug.gpu_validation (or its old
name, debug.gl_debug) turns on the validation layers, as
HALO_VULKAN_VALIDATION does; debug.gpu_stats logs the backend's statistics,
as HALO_VULKAN_STATS does; debug.gpu_dump_shaders names the folder each
shader module is written into, as HALO_VULKAN_SHADERS does (which wins);
debug.texture_dump_directory the folder each texture is written into
(xgpu_vulkan_textures.c).

display.hdr (docs/hdr.md): the window's display is read as SDL has it
(whether HDR is on, its SDR white and its headroom), at the start and as
SDL reports the window's HDR state or display changed, and handed to the
backend, which tells the front end the display shows HDR where its surface
also has an HDR colour space (host_render_stream_display_hdr) and makes an
HDR swapchain while the frames are HDR. HALO_VULKAN_HDR_HEADROOM stands in
for SDL's (HDR on, that headroom, BT.2408's 203-nit SDR white);
HALO_VULKAN_HDR_OUTPUT ("scrgb", "pq") chooses the swapchain's encoding
where the surface has both. Not on Android, nor in the VR mode.

HALO_VULKAN_VALIDATION=1 turns on the validation layers and logs their
messages (the layers must be installed, or found through VK_ADD_LAYER_PATH).
The pipeline cache is $XDG_CACHE_HOME/openh1/vulkan_pipelines.bin (or
~/.cache's; on Windows %LOCALAPPDATA%\OpenH1's); HALO_VULKAN_PIPELINE_CACHE
names another file, or none (empty).
*/

#include "host.h"
#include "host_render_stream.h"
#include "xgpu_vulkan.h"
#include "xgpu_vulkan_loading.h"
#include "xgpu_vulkan_post.h"
#include "xgpu_vulkan_stream.h"
#include "xgpu_vulkan_textures.h"
#include "frame_pacing.h"
#ifdef HALO_VR
#include "host_vr_vulkan.h"
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static SDL_Window *vulkan_window;
/* the last surface could not be made (logged once until one is) */
static int surface_failing;

uint64_t host_vulkan_display_period(void)
{
	const SDL_DisplayMode *mode = vulkan_window ?
		SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(vulkan_window)) : NULL;

	return mode && mode->refresh_rate > 0.0f ?
		(uint64_t)(1000000000.0 / mode->refresh_rate) : 16666667;
}

static VkSurfaceKHR surface_create(VkInstance instance, void *context)
{
	VkSurfaceKHR surface = VK_NULL_HANDLE;

	(void)context;
	/* (also on the render thread, made again: tried at each frame while
	Android has not given the window back) */
	if (!SDL_Vulkan_CreateSurface(vulkan_window, instance, NULL, &surface))
	{
		if (!surface_failing)
			host_logf(HOST_LOG_WARN, "vulkan: SDL makes no surface for the window: %s", SDL_GetError());
		surface_failing = 1;
		return VK_NULL_HANDLE;
	}
	surface_failing = 0;
	return surface;
}

static void suspend_call(void *argument)
{
	xgpu_vulkan_window_suspend(argument != NULL);
}

/* the Wayland compositor's reference white in nits, a window property of
the SDL the Linux builds bring */
#define WAYLAND_REFERENCE_LUMINANCE "openh1.wayland.reference_luminance"

/* display.hdr: what the window's display says (SDL's), told the backend
(any thread) */
static void window_hdr_read(void)
{
	SDL_PropertiesID properties = vulkan_window ? SDL_GetWindowProperties(vulkan_window) : 0;
	const char *chosen = getenv("HALO_VULKAN_HDR_HEADROOM");
	float sdr_white, headroom;
	int enabled;

	if (chosen && atof(chosen) >= 1.0)
	{
		xgpu_vulkan_window_hdr(1, 203.0f / 80.0f, (float)atof(chosen));
		return;
	}
	enabled = properties && SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false);
	sdr_white = properties ? SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 1.0f) : 1.0f;
	/* (on Wayland SDL's SDR white is 1.0, the compositor's reference white,
	in no nits: its nits are the patched SDL's property,
	port/android/patches/sdl-wayland-reference-luminance.patch) */
	if (properties && SDL_GetFloatProperty(properties, WAYLAND_REFERENCE_LUMINANCE, 0.0f) > 0.0f)
		sdr_white = SDL_GetFloatProperty(properties, WAYLAND_REFERENCE_LUMINANCE, 0.0f) / 80.0f;
	headroom = properties ? SDL_GetFloatProperty(properties, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f) : 1.0f;
	xgpu_vulkan_window_hdr(enabled, sdr_white, headroom);
}

/* (the thread SDL reports the window's events on) */
static bool SDLCALL window_event(void *userdata, SDL_Event *event)
{
	(void)userdata;
	switch (event->type)
	{
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		if (SDL_GetWindowFromID(event->window.windowID) == vulkan_window)
			xgpu_vulkan_window_resized((uint32_t)event->window.data1, (uint32_t)event->window.data2);
		break;
	case SDL_EVENT_WILL_ENTER_BACKGROUND:
	case SDL_EVENT_DID_ENTER_BACKGROUND:
		/* (after the frames submitted so far; Android blocks SDL's events'
		thread once these are sent, until the app comes back) */
		host_render_stream_call(suspend_call, (void *)1);
		break;
	case SDL_EVENT_DID_ENTER_FOREGROUND:
		host_render_stream_call(suspend_call, NULL);
		break;
	case SDL_EVENT_WINDOW_SHOWN:
		/* (VR's window, shown to play flat in: presented to from now on) */
		if (SDL_GetWindowFromID(event->window.windowID) == vulkan_window)
			xgpu_vulkan_window_hidden(0);
		break;
	case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		if (SDL_GetWindowFromID(event->window.windowID) == vulkan_window)
			window_hdr_read();
		break;
	default:
		break;
	}
	return true;
}

/* the caches' folder (made), into folder; NULL if there is none */
static const char *cache_folder(char *folder, size_t size)
{
	const char *cache = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");

#ifdef _WIN32
	/* (the 64-bit Windows host: the user's local application data) */
	cache = getenv("LOCALAPPDATA");
	if (!cache || !*cache)
		return NULL;
	snprintf(folder, size, "%s\\OpenH1", cache);
	if (mkdir(folder, 0755) && errno != EEXIST)
		return NULL;
	(void)home;
	return folder;
#endif
#ifdef __ANDROID__
	/* The app has no HOME; keep the driver cache in its private cache. */
	if (!cache || !*cache)
		cache = SDL_GetAndroidCachePath();
#endif
	if (cache && *cache)
		snprintf(folder, size, "%s/openh1", cache);
	else if (home && *home)
		snprintf(folder, size, "%s/.cache/openh1", home);
	else
		return NULL;
	{
		char *slash;

		/* (each folder of it made, as mkdir -p) */
		for (slash = strchr(folder + 1, '/'); slash; slash = strchr(slash + 1, '/'))
		{
			*slash = 0;
			mkdir(folder, 0755);
			*slash = '/';
		}
		if (mkdir(folder, 0755) && errno != EEXIST)
			return NULL;
	}
	return folder;
}

/* the pipeline cache's file: made folders, or NULL */
static const char *cache_path(char *path, size_t size)
{
	const char *chosen = getenv("HALO_VULKAN_PIPELINE_CACHE");
	char folder[1024];

	if (chosen)
		return *chosen ? chosen : NULL;
	if (!cache_folder(folder, sizeof(folder)))
		return NULL;
	snprintf(path, size, "%s/vulkan_pipelines.bin", folder);
	return path;
}

static void vulkan_shutdown(void)
{
	static int stopped;

	if (stopped)
		return;
	stopped = 1;
	/* host_exit uses _exit, so the backend's exiting hook must also save
	the cache. Finish the committed stream before touching its device. */
	host_render_stream_close();
#ifdef HALO_VR
	xgpu_vulkan_wait(xgpu_vulkan_fence());
	host_vr_vulkan_shutdown();
#endif
	xgpu_vulkan_shutdown();
}

static void vulkan_exiting(void *context)
{
	(void)context;
	vulkan_shutdown();
}

/* sdl_platform.c's HOST_VULKAN_DEBUG_*: debug.gpu_stats, and
debug.gpu_validation (or debug.gl_debug) */
#define DEBUG_STATISTICS 1
#define DEBUG_VALIDATION 2
/* and display.low_latency's: on, off (neither: auto) */
#define LOW_LATENCY_ON 4
#define LOW_LATENCY_OFF 8

/* a folder the guest names (its address), or the environment variable's
(set, it wins: NULL, none), copied; NULL for none */
static const char *debug_folder(uint32_t address, const char *variable, char *folder, size_t size)
{
	const char *chosen = variable ? getenv(variable) : NULL;

	if (!chosen)
		chosen = host_pointer(address);
	if (!chosen || !*chosen)
		return NULL;
	snprintf(folder, size, "%s", chosen);
	return folder;
}

/* (a host import: the guest's sdl_platform.c, on its main thread; the
folders are guest addresses of strings; again with the same window when
display.vsync or battery saver changes, for its swap interval and
display.low_latency, debug's LOW_LATENCY_* then alone) */
int host_vulkan_window_open(uint32_t window, uint32_t debug, uint32_t shader_folder, uint32_t texture_folder,
	int interval, int battery_saver)
{
	static int opened;
	static char shaders[1024], textures[1024];
	SDL_Window *object = host_sdl_handle_get(window, _handle_window);
	struct xgpu_vulkan_options options;
	static struct host_render_backend backend;
	char path[1100];
	int width = 0, height = 0;
#ifdef HALO_VR
	int vr = 0;
#endif

	if (!object)
		return 0;
	if (interval > 0 && battery_saver)
	{
		const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(object));

		interval = (int)frame_pacing_divisor(mode ? mode->refresh_rate : 0.0f, BATTERY_SAVER_LEAST,
			BATTERY_SAVER_MOST);
		host_logf(HOST_LOG_INFO, "vulkan: battery saver: each frame shown for %d refreshes", interval);
	}
	if (opened)
	{
		if (object != vulkan_window)
			return 0;
		xgpu_vulkan_window_interval(interval);
		xgpu_vulkan_window_low_latency(debug & LOW_LATENCY_ON ? 1 : debug & LOW_LATENCY_OFF ? 2 : 0);
		return 1;
	}
	if (getenv("HALO_VULKAN_STATS"))
		debug |= DEBUG_STATISTICS;
	if (getenv("HALO_VULKAN_VALIDATION") && atoi(getenv("HALO_VULKAN_VALIDATION")))
		debug |= DEBUG_VALIDATION;
	xgpu_vulkan_stream_debug((debug & DEBUG_STATISTICS) != 0,
		debug_folder(shader_folder, "HALO_VULKAN_SHADERS", shaders, sizeof(shaders)));
	xgpu_vulkan_textures_dump(debug_folder(texture_folder, NULL, textures, sizeof(textures)));
	{
		/* (the DXT textures' ASTC blocks encoded, where they are ASTC:
		HALO_ASTC_CACHE, or the caches' folder's astc) */
		char folder[1024];
		const char *chosen = getenv("HALO_ASTC_CACHE");

		if (!chosen && cache_folder(folder, sizeof(folder)))
		{
			strncat(folder, "/astc", sizeof(folder) - strlen(folder) - 1);
			chosen = folder;
		}
		xgpu_vulkan_textures_astc_folder(chosen);
	}
	vulkan_window = object;
	/* (which of SDL's window systems the window is in: on a Linux desktop
	Wayland's where the session is Wayland's, which alone gives HDR, X11's
	under XWayland and gamescope) */
	host_logf(HOST_LOG_INFO, "vulkan: the window is SDL's %s", SDL_GetCurrentVideoDriver() ?
		SDL_GetCurrentVideoDriver() : "(no video driver)");
	memset(&options, 0, sizeof(options));
	if (SDL_GetWindowFlags(object) & SDL_WINDOW_VULKAN)
	{
		Uint32 count = 0;
		const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&count);

		if (extensions)
		{
			options.instance_extensions = extensions;
			options.instance_extension_count = count;
			options.surface_create = surface_create;
			options.rotation_prepare = xgpu_vulkan_post_rotate_prepare;
			options.rotate = xgpu_vulkan_post_rotate;
#if !defined(__ANDROID__) && !defined(HALO_VR)
			/* (display.hdr: an HDR swapchain, and the display told the front
			end) */
			options.display_hdr = host_render_stream_display_hdr;
			options.encode_prepare = xgpu_vulkan_post_encode_prepare;
			options.encode = xgpu_vulkan_post_encode;
#endif
#ifndef HALO_VR
			/* (display.gamma's curve over an SDR frame: not the VR build's) */
			options.gamma_prepare = xgpu_vulkan_post_gamma_prepare;
			options.gamma = xgpu_vulkan_post_gamma;
#endif
		}
	}
	else
	{
		host_logf(HOST_LOG_INFO, "vulkan: the window is not one SDL draws with Vulkan: its frames are not shown");
	}
	options.interval = interval;
	options.low_latency = debug & LOW_LATENCY_ON ? 1 : debug & LOW_LATENCY_OFF ? 2 : 0;
#ifdef HALO_VR
	if (host_vr_vulkan_requested() && host_vr_vulkan_prepare())
	{
		/* The headset's runtime chooses the physical device and adds what
		it needs to the instance and device SDL's surface needs; its images
		are presented through OpenXR. The window keeps its surface, which
		the game plays flat in if VR cannot start after all. */
		options.create_instance = host_vr_vulkan_create_instance;
		options.choose_physical = host_vr_vulkan_physical_device;
		options.create_device = host_vr_vulkan_create_device;
		options.device_ready = host_vr_vulkan_bind;
		vr = 1;
	}
#endif
	options.cache_path = cache_path(path, sizeof(path));
	options.validation = (debug & DEBUG_VALIDATION) != 0;
	options.statistics = (debug & DEBUG_STATISTICS) != 0;
	if (!xgpu_vulkan_initialize(&options))
	{
#ifdef HALO_VR
		if (!vr)
			return 0;
		/* (the runtime refused the instance or device, or its device lacks
		what the backend needs: a device of the backend's own choosing, to
		play flat with) */
		host_logf(HOST_LOG_WARN, "vr: VR unavailable: the headset's runtime gives no Vulkan device the "
			"renderer can use; playing flat");
		xgpu_vulkan_initialize_undo();
		host_vr_vulkan_shutdown();
		options.create_instance = NULL;
		options.choose_physical = NULL;
		options.create_device = NULL;
		options.device_ready = NULL;
		if (!xgpu_vulkan_initialize(&options))
			return 0;
#else
#ifdef _WIN32
		/* (the 64-bit Windows build draws with Vulkan alone: say so) */
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo",
			"The game cannot draw: it needs a graphics card and driver with Vulkan 1.3.\n\n"
			"Install the latest driver for your graphics card (NVIDIA, AMD or Intel) and start the game again. "
			"A graphics card older than about 2016, or a computer without a graphics driver (a virtual machine, "
			"say), may have no Vulkan 1.3 at all. halo.log has the details.", object);
#endif
		return 0;
#endif
	}
#ifdef HALO_VR
	host_vr_vulkan_set_depth_format(xgpu_vulkan_depth_format());
	/* (VR's window, hidden while the headset shows the game, is not
	presented to until it is shown, to play flat in) */
	if (vr && (SDL_GetWindowFlags(object) & SDL_WINDOW_HIDDEN))
		xgpu_vulkan_window_hidden(1);
#endif
	SDL_GetWindowSizeInPixels(object, &width, &height);
	xgpu_vulkan_window_resized((uint32_t)width, (uint32_t)height);
	window_hdr_read();
	SDL_AddEventWatch(window_event, NULL);
	backend = *xgpu_vulkan_stream_backend();
	backend.exiting = vulkan_exiting;
	host_render_stream_set_backend(&backend);
	host_render_stream_set_screenshot_writer(host_screenshot_write);
	atexit(vulkan_shutdown);
	opened = 1;
	return 1;
}
