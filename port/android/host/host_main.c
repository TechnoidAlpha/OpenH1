/*
HOST_MAIN.C

Entry point of the Android port (SDL_main, called by SDLActivity on its
own thread).

It loads the guest image (the game, built as ILP32 code) from the APK's
assets, gives it an environment describing where the game data and saves
live, and runs its main() on a thread of its own with its stack in guest
memory (host_thread.c), on which everything here after startup runs; the
SDL thread waits for it.

Storage (see port/android/README.md): the game data (the directory holding
maps/) is the app's external files directory,
/sdcard/Android/data/<package>/files, where the launcher activity copies it
on first run; saves go to its save/ subdirectory. The settings,
config.toml, live there too (port/linux/src/port_config.c, which the game
reads); the host reads only its debugging settings (host_common.c). The log
is logcat's, tag "halo".
*/

#include "host.h"
#include "host_mcc_memory.h"
#include "tomlc17.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <android/log.h>
#include <jni.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------- the log: logcat */

static void log_to_logcat(int priority, const char *line)
{
	__android_log_write(priority, "halo", line);
}

/* ---------- paths */

static char data_root[512];
static char save_root[512];
/* display.render_scale, as it is used: set once at the start (game_main) */
static float render_scale = 1.0f;

void host_android_path(int which, char *buffer, uint32_t size)
{
	snprintf(buffer, size, "%s", which ? save_root : data_root);
}

static int directory_has_maps(const char *root)
{
	char path[600];
	struct stat information;

	snprintf(path, sizeof(path), "%s/maps/ui.map", root);
	return stat(path, &information) == 0;
}

/* Directories the app creates in its external storage are private to it
(mode 0770 under the app's own group), so the shell user (adb) cannot list
them. Open the save tree for reading, with set-group-ID directories as
posix_make_directory creates them (port/linux/src/posix_files.c). */
static int share_entry(const char *path, const struct stat *information, int type, struct FTW *walk)
{
	(void)information;
	(void)walk;
	if (type == FTW_D || type == FTW_DP)
		chmod(path, 02775);
	else if (type == FTW_F)
		chmod(path, 0664);
	return 0;
}

static void share_save_tree(const char *root)
{
	nftw(root, share_entry, 16, FTW_PHYS);
}

/* ---------- the system's gesture edges */

/* asks the activity for Android's system gesture insets via JNI
(HaloActivity.getSystemGestureInsetsPixels); the guest calls it at every
finger down because the insets change when the phone rotates; the method is
found on the activity's own class because the game's native thread has the
system's class loader, which does not know the app's classes; a change is
logged so the log shows the value the game works with; all 0 on any failure
(the touch controls then use the whole screen) */
void host_gesture_insets(int *insets)
{
	static int logged[4] = { -1, -1, -1, -1 };
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	jint values[4] = { 0, 0, 0, 0 };
	int index;

	if (env && activity)
	{
		jclass activity_class = (*env)->GetObjectClass(env, activity);
		jmethodID method = activity_class ? (*env)->GetMethodID(env, activity_class, "getSystemGestureInsetsPixels", "()[I") : NULL;
		jintArray array = method ? (jintArray)(*env)->CallObjectMethod(env, activity, method) : NULL;

		if (array && !(*env)->ExceptionCheck(env) && (*env)->GetArrayLength(env, array) == 4)
			(*env)->GetIntArrayRegion(env, array, 0, 4, values);
		/* a Java exception left pending would break the next JNI call of the
		thread, and a failed read must leave zeros */
		if ((*env)->ExceptionCheck(env))
		{
			(*env)->ExceptionClear(env);
			for (index = 0; index < 4; index++)
				values[index] = 0;
		}
		if (array)
			(*env)->DeleteLocalRef(env, array);
		if (activity_class)
			(*env)->DeleteLocalRef(env, activity_class);
		(*env)->DeleteLocalRef(env, activity);
	}
	/* (in the game's pixels: display.render_scale's) */
	for (index = 0; index < 4; index++)
		insets[index] = (int)(values[index] * render_scale + 0.5f);
	if (memcmp(logged, insets, sizeof(logged)))
	{
		memcpy(logged, insets, sizeof(logged));
		host_logf(HOST_LOG_INFO, "system gesture insets %d,%d,%d,%d", insets[0], insets[1], insets[2], insets[3]);
	}
}

/* the guest's, display.orientation (sdl_platform.c's orientation_apply): 0
landscape and 1 portrait, either way up (turned over with the device
whatever its rotation lock, as the manifest's sensorLandscape), 2
whichever way the device is held, as its own rotation lock allows;
Activity.setRequestedOrientation, which turns the window in place
(HaloActivity's configChanges); SDL asks only as it makes the window.
Logged, so the log shows each request. */
void host_orientation(int which)
{
	/* ActivityInfo's SCREEN_ORIENTATION_SENSOR_LANDSCAPE, _SENSOR_PORTRAIT
	and _FULL_USER */
	static const jint requests[] = { 6, 7, 13 };
	static const char *const names[] = { "landscape", "portrait", "auto" };
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();

	if (which < 0 || which > 2)
		which = 0;
	if (env && activity)
	{
		jclass activity_class = (*env)->GetObjectClass(env, activity);
		jmethodID method = activity_class ? (*env)->GetMethodID(env, activity_class, "setRequestedOrientation", "(I)V") : NULL;

		if (method)
			(*env)->CallVoidMethod(env, activity, method, requests[which]);
		/* (a Java exception left pending would break the thread's next JNI
		call) */
		if ((*env)->ExceptionCheck(env))
			(*env)->ExceptionClear(env);
		if (activity_class)
			(*env)->DeleteLocalRef(env, activity_class);
		(*env)->DeleteLocalRef(env, activity);
	}
	host_logf(HOST_LOG_INFO, "orientation %s", names[which]);
}

/* the system's keyboard for a menu's text field (sdl_platform.c's
screen_keyboard_update): SDL's text input on the game's window, which the
activity's keyboard shows, one line (Enter ends the field), a password's
hidden; what it types arrives as keys */
static SDL_Window *text_input_window(void)
{
	int count = 0;
	SDL_Window **windows = SDL_GetWindows(&count);
	SDL_Window *window = windows && count > 0 ? windows[0] : NULL;

	SDL_free(windows);
	return window;
}

void host_android_text_input(int show, int password)
{
	SDL_Window *window = text_input_window();
	SDL_PropertiesID properties;

	if (!window)
		return;
	if (!show)
	{
		SDL_StopTextInput(window);
		return;
	}
	properties = SDL_CreateProperties();
	SDL_SetBooleanProperty(properties, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
	SDL_SetNumberProperty(properties, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
		password ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN : SDL_TEXTINPUT_TYPE_TEXT);
	if (!SDL_StartTextInputWithProperties(window, properties))
		host_logf(HOST_LOG_WARN, "text input: %s", SDL_GetError());
	SDL_DestroyProperties(properties);
}

int host_android_text_input_active(void)
{
	SDL_Window *window = text_input_window();

	return window && SDL_TextInputActive(window);
}

/* Whether the app runs through an ARM translator. The x86 emulator runs the
app's ARM code through one, which ro.dalvik.vm.native.bridge names; it
cannot deliver the page faults of the write tracking to the app
(host_memory.c). */
static int native_bridge_active(void)
{
	char value[PROP_VALUE_MAX] = "";

	__system_property_get("ro.dalvik.vm.native.bridge", value);
	return value[0] && strcmp(value, "0") != 0;
}

/* ---------- main */

#define MAIN_STACK_SIZE (16 * 1024 * 1024)

/* display.render_scale (HALO_RENDER_SCALE): the fraction of the screen's
pixels the game draws each way, which the display scales up (the swapchain
is that size, xgpu_vulkan.c's swapchain_make); 0, the device's: 0.8, or
less where that would still draw more than 1080 of the screen's shorter
side (1440p's phones and the tablets draw about 1080): a phone's pixels are
small enough that 0.8 stays sharp, and the GPU's time saved keeps its frames
steady at 120 Hz. Read from
config.toml here, before the guest starts, as the window's size and the
display's density it is given depend on it. */
static float render_scale_choose(const char *config, int shorter)
{
	const char *value = getenv("HALO_RENDER_SCALE");
	double scale = 0.0;

	if (value && *value)
	{
		scale = atof(value);
	}
	else
	{
		toml_result_t result = toml_parse_file_ex(config);

		if (result.ok)
		{
			toml_datum_t setting = toml_seek(result.toptab, "display.render_scale");

			scale = setting.type == TOML_FP64 ? setting.u.fp64 :
				setting.type == TOML_INT64 ? (double)setting.u.int64 : 0.0;
			toml_free(result);
		}
	}
	if (scale == 0.0)
		scale = shorter * 0.8 > 1080.0 ? 1080.0 / shorter : 0.8;
	if (!(scale >= 0.25 && scale <= 1.0))
		scale = 1.0;
	return (float)scale;
}

/* the host's (host_sdl.c's window size, xgpu_vulkan.c's swapchain) */
float host_app_render_scale(void)
{
	return render_scale;
}

/* a size in the screen's pixels in the game's (display.render_scale's),
even as the swapchain's is */
static int render_scaled(int pixels)
{
	return (int)(pixels * render_scale + 0.5f) & ~1;
}

/* the thread that runs the game: passes the display and its density, the
time zone and the data and save folders to the guest through its
environment, chooses the write tracking (page protection, or page hashes when
translated or when debug.memory_watch is false), and runs the guest's main;
never returns normally */
static void *game_main(void *unused)
{
	struct host_environment environment = { { 0 }, 0 };
	const char *external;
	char zone[64];
	char path[600];
	size_t image_size = 0;
	void *image;
	uint32_t boot;

	(void)unused;
	external = SDL_GetAndroidExternalStoragePath();
	if (!external)
		host_fatal("Android storage is unavailable: %s", SDL_GetError());
	snprintf(data_root, sizeof(data_root), "%s", external);
	snprintf(save_root, sizeof(save_root), "%s/save", external);
	/* readable by adb (the shell user), for managing saves */
	mkdir(save_root, 0775);
	share_save_tree(save_root);
	if (!directory_has_maps(data_root))
	{
		host_fatal("The Halo game data was not found.\n\nCopy the PAL game data (build 01.01.14.2342), "
			"the folder that contains maps, into\n%s\nor import it from the launcher screen.", data_root);
	}

	host_environment_set(&environment, "HOME", save_root);
	host_environment_set(&environment, "HALO_DATA_ROOT", data_root);
	host_environment_set(&environment, "HALO_SAVE_ROOT", save_root);
	{
		/* The guest needs the landscape display's pixel dimensions before
		its window opens; afterwards it queries the drawable itself. */
		const SDL_DisplayMode *mode;
		char width[16], height[16], refresh[16];

		SDL_InitSubSystem(SDL_INIT_VIDEO);
		mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
		if (mode && mode->w > 0 && mode->h > 0)
		{
			int pixel_width = (int)(mode->w * mode->pixel_density + 0.5f);
			int pixel_height = (int)(mode->h * mode->pixel_density + 0.5f);
			int longer = pixel_width > pixel_height ? pixel_width : pixel_height;
			int shorter = pixel_width > pixel_height ? pixel_height : pixel_width;

			snprintf(path, sizeof(path), "%s/config.toml", data_root);
			render_scale = render_scale_choose(path, shorter);
			snprintf(width, sizeof(width), "%d", render_scaled(longer));
			snprintf(height, sizeof(height), "%d", render_scaled(shorter));
			host_environment_set(&environment, "HALO_DISPLAY_WIDTH", width);
			host_environment_set(&environment, "HALO_DISPLAY_HEIGHT", height);
			host_logf(HOST_LOG_INFO, "display %dx%d: rendering %sx%s (render scale %.2f)", mode->w, mode->h, width,
				height, render_scale);
		}
		if (mode)
		{
			/* display.max_fps's "twice the refresh rate" (sdl_platform.c):
			the guest cannot ask SDL for the display mode */
			snprintf(refresh, sizeof(refresh), "%g", mode->refresh_rate > 0.0f ? mode->refresh_rate : 60.0f);
			host_environment_set(&environment, "HALO_DISPLAY_REFRESH", refresh);
		}
		{
			/* dp for the touch controls (port/linux/src/touch_input.c): SDL
			gives Android's densityDpi / 160, here in the game's pixels */
			float density = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()) * render_scale;
			char text[16];

			if (density > 0.0f)
			{
				snprintf(text, sizeof(text), "%g", density);
				host_environment_set(&environment, "HALO_DISPLAY_DENSITY", text);
				host_logf(HOST_LOG_INFO, "display density %s", text);
			}
		}
	}
	host_time_zone(zone, sizeof(zone));
	host_environment_set(&environment, "TZ", zone);
	/* internet play's MQTT brokers (network.brokers_file): the APK's list,
	written beside config.toml at each start, as a desktop update replaces
	the file beside its game */
	{
		size_t brokers_size = 0;
		void *brokers = SDL_LoadFile("brokers.txt", &brokers_size);

		snprintf(path, sizeof(path), "%s/brokers.txt", data_root);
		if (!brokers || !SDL_SaveFile(path, brokers, brokers_size))
			host_logf(HOST_LOG_ERROR, "cannot write %s: %s", path, SDL_GetError());
		SDL_free(brokers);
	}
	/* ...and the stock maps' preparations' records (port/linux/src/
	prepared_shipped.h), the same way */
	{
		size_t records_size = 0;
		void *records = SDL_LoadFile("prepared.records", &records_size);

		snprintf(path, sizeof(path), "%s/prepared.records", data_root);
		if (!records || !SDL_SaveFile(path, records, records_size))
			host_logf(HOST_LOG_ERROR, "cannot write %s: %s", path, SDL_GetError());
		SDL_free(records);
	}
	/* ...and the textures', fonts' and menus' bytes (port/linux/src/
	embedded_assets.h), the same way */
	{
		size_t assets_size = 0;
		void *assets = SDL_LoadFile("openh1.assets", &assets_size);

		snprintf(path, sizeof(path), "%s/openh1.assets", data_root);
		if (!assets || !SDL_SaveFile(path, assets, assets_size))
			host_logf(HOST_LOG_ERROR, "cannot write %s: %s", path, SDL_GetError());
		SDL_free(assets);
	}
	snprintf(path, sizeof(path), "%s/config.toml", data_root);

	image = SDL_LoadFile("halo_guest.elf", &image_size);
	if (!image)
		host_fatal("cannot read the game image from the APK: %s", SDL_GetError());
	if (host_load_image(image, image_size) != 0)
	{
		FILE *report;

		if (!host_memory_fixed_unavailable())
			host_fatal("cannot load the game image; see logcat (tag \"halo\") for details");

		snprintf(path, sizeof(path), "%s/memory_map.txt", data_root);
		report = fopen(path, "w");
		host_memory_report_low_mappings(report);
		if (report)
			fclose(report);
		host_fatal("The game cannot start: the memory it needs at 0x80000000 is taken by Android's "
			"Java runtime on this device.\n\nRestarting the device may help. Please report it with "
			"memory_map.txt from\n%s\n(or adb logcat -s halo).", data_root);
	}
	SDL_free(image);

	host_apply_debug_settings(path, native_bridge_active() ? "ARM translation" : NULL);
	boot = host_make_boot(&environment, "halo");
	host_logf(HOST_LOG_INFO, "data %s, saves %s", data_root, save_root);
	host_run_guest_main(boot);
}

/* Called when HaloApplication loads this library at the start of the
game's own process (":game"), before its Java side has allocated anything
large: ART's large object space, which on some devices covers the Xbox
window, is then idle there and can be reclaimed (host_memory.c). */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
	(void)vm;
	(void)reserved;
	/* (before main: what the reservation finds goes to logcat too) */
	host_log_set_sink(log_to_logcat);
	host_memory_reserve_early();
	/* MCC must reserve before the guest and renderer make allocations. */
	host_mcc_memory_initialize();
	return JNI_VERSION_1_6;
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	host_log_set_sink(log_to_logcat);
	host_logf(HOST_LOG_INFO, "Halo for Android starting");
	host_install_signal_handlers();
	host_signal_stack_install();
	if (host_native_thread_create(game_main, NULL, MAIN_STACK_SIZE) != 0)
		host_fatal("cannot start the game thread");
	/* the game ends the process itself (host_exit) */
	for (;;)
		pause();
}
