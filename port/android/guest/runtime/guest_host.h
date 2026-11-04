/*
GUEST_HOST.H

The host services the guest imports (the guest's view; the host defines
them in port/android/host). Every name here must also be listed in
port/android/host_imports.list, or for the Android app's own (the last two
sections) in port/android/android_imports.list; the lists generate the
import stubs, and tools/check_host_imports.py checks them against this
file.

Parameter types follow the rules in halo_android_abi.h: 32-bit values are
int or unsigned int, 64-bit values long long, and pointers are passed as
they are.
*/

#ifndef __GUEST_HOST_H
#define __GUEST_HOST_H

/* ---------- process */

/* performs a Linux system call on the guest's behalf, converting the
structures whose layout differs; returns the raw result (-errno on failure) */
long long host_syscall(long long number, long long a, long long b, long long c,
	long long d, long long e, long long f);

/* Android log priorities (android/log.h) */
void host_log(int priority, const char *text);
void host_abort(const char *reason) __attribute__((noreturn));
void host_exit(int code) __attribute__((noreturn));
/* the host's errno on this thread, after a call to a host function */
int host_errno(void);

/* ---------- threads

The guest's thread pointer (its struct pthread) is kept by the host for
each thread. */

unsigned int host_get_tp(void);
void host_set_tp(unsigned int thread);
/* starts a host thread with a stack in guest memory that calls the image's
__guest_thread_start(thread); returns 0 or an errno value */
int host_thread_create(unsigned int thread, unsigned int stack_size);

/* ---------- memory write tracking (port/linux/src/memory_watch.c) */

void host_memory_watch_initialize(void);
void host_memory_watch_protect(unsigned int address, unsigned int size);
unsigned int host_memory_watch_generation(unsigned int address, unsigned int size);
unsigned int host_memory_watch_serial(void);
void host_memory_watch_prepare_write(unsigned int address, unsigned int size);
void host_memory_watch_forget(unsigned int address, unsigned int size);
/* memory_watch_begin_frame (port/linux/src/platform.h) */
void host_memory_watch_begin_frame(void);

/* ---------- SDL (guest/runtime/guest_sdl.c)

Window, context, gamepad and audio stream objects are small integer
handles on this side. */

int host_sdl_init(unsigned int flags);
int host_sdl_set_hint(const char *name, const char *value);
void host_sdl_get_error(char *buffer, unsigned int size);
long long host_sdl_ticks(void);
long long host_sdl_ticks_ns(void);
long long host_sdl_thread_id(void);
unsigned int host_sdl_create_window(const char *title, int width, int height, long long flags);
void host_sdl_window_size_in_pixels(unsigned int window, int *width, int *height);
int host_sdl_set_relative_mouse(unsigned int window, int enabled);
int host_sdl_poll_event(void *event);
int host_sdl_set_clipboard_text(const char *text);
void host_sdl_get_clipboard_text(char *buffer, unsigned int size);
int host_sdl_open_url(const char *url);
/* the keyboard's keys by name (the controls' bindings, xinput_sdl.c) */
void host_sdl_scancode_name(int scancode, char *buffer, unsigned int size);
int host_sdl_scancode_from_name(const char *name);
int host_sdl_show_toast(const char *message, int duration, int gravity, int x, int y);
/* SDL_GetPowerInfo: the SDL_PowerState, and the seconds and percent of
battery left (-1 unknown) */
int host_sdl_power_info(int *seconds, int *percent);
int host_sdl_show_simple_message_box(unsigned int flags, const char *title, const char *message);
/* SDL_ShowMessageBox of up to three buttons (SDL_MessageBoxData has not the
guest's layout): labels are consecutive NUL-terminated strings, size bytes
in all (at most 1024); the index of the one chosen, else -1 */
int host_sdl_ask(unsigned int flags, const char *title, const char *message, int default_index,
	const char *labels, unsigned int size);
int host_sdl_get_gamepads(unsigned int *ids, int capacity);
unsigned int host_sdl_open_gamepad(unsigned int id);
unsigned int host_sdl_gamepad_from_id(unsigned int id);
int host_sdl_gamepad_axis(unsigned int gamepad, int axis);
int host_sdl_gamepad_button(unsigned int gamepad, int button);
int host_sdl_gamepad_type(unsigned int gamepad);
int host_sdl_rumble_gamepad(unsigned int gamepad, unsigned int low, unsigned int high, unsigned int milliseconds);
void host_sdl_close_gamepad(unsigned int gamepad);
/* callback: void (*)(void *userdata, unsigned int stream, int additional, int total),
called on the audio thread */
unsigned int host_sdl_open_audio_stream(unsigned int device, const void *spec, unsigned int callback, unsigned int userdata);
int host_sdl_put_audio_stream_data(unsigned int stream, const void *data, int length);
int host_sdl_resume_audio_stream_device(unsigned int stream);
/* (voice chat's microphone, a stream without a callback) */
int host_sdl_get_audio_stream_data(unsigned int stream, void *data, int length);
int host_sdl_get_audio_stream_available(unsigned int stream);
void host_sdl_destroy_audio_stream(unsigned int stream);
/* (debug.latency_log) the device a stream plays on: its driver's name, its
buffer (frames), rate, the time SDL queues ahead of it and the system's
latency after (microseconds; -1 not known), and the times the Mac's output
unit was set up for a device (0 for SDL's drivers) into timing[5]; 1 on
success */
int host_sdl_audio_stream_timing(unsigned int stream, char *driver, int driver_size, int *timing);

/* ---------- the sound's output (host_audio_route.c)

What the system's sound plays on, for audio.spatial (dsound_sdl.c): 1
headphones, 0 speakers, -1 not known; kept current by a thread of the
host's, so asking costs nothing */
int host_audio_headphones(void);

/* ---------- the renderer's command stream (port/linux/src/xgpu_stream.h,
port/android/host/host_render_stream.h): the ring at the guest address
ring opened (0, or -1: the host has no renderer for it); its records to
the header's committed handed over (XGPU_STREAM_SUBMIT_*); a wait until
the host has released up to position; the stream ended */
int host_render_stream_open(unsigned int ring);
void host_render_stream_submit(unsigned int flags);
void host_render_stream_wait(unsigned int position);
void host_render_stream_close(void);

/* ---------- Android (android_imports.list) */

/* the storage directories the port uses, copied into buffer */
void host_android_path(int which, char *buffer, unsigned int size);

/* the edges where Android keeps its gestures, as left, top, right, bottom
in pixels of the current orientation, into insets[4]; all 0 when unknown */
void host_gesture_insets(int *insets);
/* display.orientation: asks the activity for the way up, 0 landscape, 1
portrait (either way up), 2 whichever way the device is held */
void host_orientation(int which);

/* ---------- the on-screen touch controls (host_touch.c, android_imports.list) */

/* the overlay's controller: the SDL axes (left x, y, right x, y, left
trigger, right trigger) and the SDL button bits, into state[7] */
void host_touch_read(int *state);
/* the overlay's view swipe, then its gyroscope turn, since the last read,
into delta[4] */
void host_touch_look_read(float *delta);
/* port 0's motors, for the phone's vibration */
void host_touch_rumble(unsigned int low, unsigned int high);
/* tells the overlay when to show: touch_input.c's _touch_scene_* */
void host_touch_scene(int scene);
/* where the overlay wants the picture's top in a portrait window, in the
window's pixels (under its top strip), or -1 if it has not said */
int host_touch_picture_top(void);
/* the game control on each of the 16 controller buttons (touch_game.c),
for the overlay's labels */
void host_touch_bindings(const int *controls);

/* ---------- HTTPS (host_https.c): the server browser's master servers, a
dedicated server's publishing to them */

/* the headers' room at the start of host_https_request's response */
#define HOST_HTTPS_HEADERS_SIZE 4096
/* a request (port/linux/src/update.h's update_https_request, which
guest_https.c makes of it): "METHOD\nURL\n" and its header lines, and its
body; into response the answer's headers (HOST_HTTPS_HEADERS_SIZE bytes,
ended) and then its body (up to response_size - HOST_HTTPS_HEADERS_SIZE - 1
bytes, ended), and into results its status and the body's size. 0 if no
answer came, why in response */
int host_https_request(const char *request, const void *body, int body_size, char *response, int response_size,
	int *results);

#endif
