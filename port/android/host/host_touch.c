/*
HOST_TOUCH.C

The on-screen touch controls between the overlay and the game. The overlay
(app/.../TouchControls.java) is an Android view over SDL's surface: it
hands its stick, buttons, view swipes and gyroscope turns over JNI on the UI
thread, and the game reads them through host imports when it reads port 0's
controller (port/linux/src/xinput_sdl.c). The game hands back what the
overlay needs to know: whether it is in a game with no menu or cinematic up
and the input.touch_controls setting (host_touch_scene), the profile's
button mapping, which names the buttons (host_touch_bindings), and how hard
port 0 rumbles; and the overlay tells the game where it wants the picture
in a portrait window (host_touch_picture_top). The overlay also says
whether the device has a touchscreen and a controller (host_touch_devices),
for the game's own touchscreen buttons.

The iOS app's overlay (port/ios/touch_controls.m, a UIKit view over SDL's)
hands its state over through the same functions, host_touch.h's, without
JNI.
*/

#include "host_touch.h"

#ifdef __ANDROID__
#include <jni.h>
#endif
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef __ANDROID__
/* host_main.c: the fraction of the screen's pixels the game draws at */
float host_app_render_scale(void);
#endif

/* a press shorter than this still reaches the game: it reads the
controller once a frame and acts at 30 ticks a second, and a quick tap can
go down and up between two reads */
#define TOUCH_TAP_NS 60000000LL
/* view motion and rumble older than this are dropped: left over from the
menus, a cinematic or the app in the background */
#define TOUCH_STALE_NS 150000000LL

/* SDL's 15 gamepad buttons, then the left and right triggers */
#define TOUCH_INPUTS 17

static pthread_mutex_t touch_lock = PTHREAD_MUTEX_INITIALIZER;
/* the SDL axes (left x, y, right x, y, left trigger, right trigger), then
the SDL button bits */
static int32_t touch_state[7];
static int64_t touch_pressed_ns[TOUCH_INPUTS];
static float look_delta[2];
static int64_t look_ns;
/* the gyroscope's turn, apart: the profile's invert is the swipe's only */
static float gyro_delta[2];
static int64_t gyro_ns;
static int rumble_amplitude;
static int64_t rumble_ns;
static volatile int touch_scene;
/* where the overlay wants the picture's top in a portrait window, in the
window's pixels, plus 1 (0: it has not said) */
static volatile int touch_picture_top;
#ifdef __ANDROID__
/* the overlay's devices (host_touch_devices): a touchscreen, a game
controller (a stick or a hat, not a phone's few gamepad keys) */
#define HOST_TOUCH_DEVICE_TOUCHSCREEN 1
#define HOST_TOUCH_DEVICE_CONTROLLER 2
static volatile int touch_devices;
#endif
/* the game control on each controller button (-1: none), and a count of
its changes, which the overlay polls */
#define TOUCH_BUTTONS 16
static int32_t touch_bindings[TOUCH_BUTTONS];
static int touch_bindings_serial;

static int64_t now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/* the inputs of a state as bits: buttons, then the triggers */
static uint32_t touch_inputs(const int32_t *state)
{
	return ((uint32_t)state[6] & 0x7fff) | (state[4] ? 1u << 15 : 0) | (state[5] ? 1u << 16 : 0);
}

void host_touch_set_state(const int32_t *next)
{
	uint32_t pressed;
	int64_t now = now_ns();
	int input;

	pthread_mutex_lock(&touch_lock);
	pressed = touch_inputs(next) & ~touch_inputs(touch_state);
	for (input = 0; input < TOUCH_INPUTS; input++)
	{
		if (pressed & (1u << input))
			touch_pressed_ns[input] = now;
	}
	memcpy(touch_state, next, sizeof(touch_state));
	pthread_mutex_unlock(&touch_lock);
}

/* the guest's: the overlay's controller state, with each press held for
at least TOUCH_TAP_NS */
void host_touch_read(int32_t *state)
{
	int64_t now = now_ns();
	int input;

	pthread_mutex_lock(&touch_lock);
	memcpy(state, touch_state, sizeof(touch_state));
	for (input = 0; input < TOUCH_INPUTS; input++)
	{
		if (touch_pressed_ns[input] && now - touch_pressed_ns[input] < TOUCH_TAP_NS)
		{
			if (input < 15)
				state[6] |= 1 << input;
			else if (!state[4 + input - 15])
				state[4 + input - 15] = 32767;
		}
	}
	pthread_mutex_unlock(&touch_lock);
}

void host_touch_add_look(float dx, float dy)
{
	pthread_mutex_lock(&touch_lock);
	look_delta[0] += dx;
	look_delta[1] += dy;
	look_ns = now_ns();
	pthread_mutex_unlock(&touch_lock);
}

void host_touch_add_gyro(float dx, float dy)
{
	pthread_mutex_lock(&touch_lock);
	gyro_delta[0] += dx;
	gyro_delta[1] += dy;
	gyro_ns = now_ns();
	pthread_mutex_unlock(&touch_lock);
}

void host_touch_look_reset(void)
{
	pthread_mutex_lock(&touch_lock);
	look_delta[0] = look_delta[1] = 0.0f;
	gyro_delta[0] = gyro_delta[1] = 0.0f;
	pthread_mutex_unlock(&touch_lock);
}

/* the guest's: the view swipe, then the gyroscope's turn, since the last
read, into delta[4], in the overlay's logical pixels with its sensitivity
applied */
void host_touch_look_read(float *delta)
{
	int64_t now = now_ns();

	pthread_mutex_lock(&touch_lock);
	memcpy(delta, look_delta, sizeof(look_delta));
	memcpy(delta + 2, gyro_delta, sizeof(gyro_delta));
	if (now - look_ns > TOUCH_STALE_NS)
		delta[0] = delta[1] = 0.0f;
	if (now - gyro_ns > TOUCH_STALE_NS)
		delta[2] = delta[3] = 0.0f;
	look_delta[0] = look_delta[1] = 0.0f;
	gyro_delta[0] = gyro_delta[1] = 0.0f;
	pthread_mutex_unlock(&touch_lock);
}

/* the guest's: port 0's motors (XInputSetState), each 0..65535; the game
sends them every frame, and zero while paused or with the profile's
vibration off */
void host_touch_rumble(unsigned int low, unsigned int high)
{
	unsigned int strength = low > high ? low : high;

	pthread_mutex_lock(&touch_lock);
	rumble_amplitude = strength ? 64 + (int)(strength * 191u / 65535u) : 0;
	rumble_ns = now_ns();
	pthread_mutex_unlock(&touch_lock);
}

int host_touch_rumble_strength(void)
{
	int amplitude;

	pthread_mutex_lock(&touch_lock);
	amplitude = now_ns() - rumble_ns > TOUCH_STALE_NS ? 0 : rumble_amplitude;
	pthread_mutex_unlock(&touch_lock);
	return amplitude;
}

/* the guest's, at every read of port 0: port/linux/src/touch_input.c's
_touch_scene_* bits */
void host_touch_scene(int scene)
{
	touch_scene = scene;
}

/* the guest's, when the profile's mapping changes: the game control on
each of the 16 controller buttons (port/linux/game/touch_game.c) */
void host_touch_bindings(const int32_t *controls)
{
	pthread_mutex_lock(&touch_lock);
	memcpy(touch_bindings, controls, sizeof(touch_bindings));
	touch_bindings_serial++;
	pthread_mutex_unlock(&touch_lock);
}

int host_touch_bindings_read(int32_t *controls)
{
	int serial;

	pthread_mutex_lock(&touch_lock);
	memcpy(controls, touch_bindings, sizeof(touch_bindings));
	serial = touch_bindings_serial;
	pthread_mutex_unlock(&touch_lock);
	return serial;
}

int host_touch_scene_read(void)
{
	return touch_scene;
}

void host_touch_set_picture_top(int top)
{
	touch_picture_top = top < 0 ? 0 : top + 1;
}

/* the guest's, every frame (port/linux/src/d3d8_device.c's picture_top_in):
where the overlay wants the picture's top in a portrait window, in the
game's pixels, or -1 if it has not said */
int host_touch_picture_top(void)
{
#ifdef __ANDROID__
	/* (in the game's pixels: display.render_scale's, host_main.c) */
	int top = touch_picture_top - 1;

	return top < 0 ? top : (int)(top * host_app_render_scale() + 0.5f);
#else
	return touch_picture_top - 1;
#endif
}

#ifdef __ANDROID__
/* ---------- the Android overlay's (TouchControls.java's natives) */

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeState(
	JNIEnv *env, jclass cls, jint lx, jint ly, jint rx, jint ry,
	jint lt, jint rt, jint buttons)
{
	int32_t next[] = { lx, ly, rx, ry, lt, rt, buttons };

	(void)env;
	(void)cls;
	host_touch_set_state(next);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeLook(
	JNIEnv *env, jclass cls, jfloat dx, jfloat dy)
{
	(void)env;
	(void)cls;
	host_touch_add_look(dx, dy);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeGyro(
	JNIEnv *env, jclass cls, jfloat dx, jfloat dy)
{
	(void)env;
	(void)cls;
	host_touch_add_gyro(dx, dy);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeLookReset(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	host_touch_look_reset();
}

/* the phone's vibration strength, 0 or 64..255 */
JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeRumble(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	return host_touch_rumble_strength();
}

/* the mapping into controls[16]; returns its count of changes (0 before the
game has sent it) */
JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeBindings(JNIEnv *env, jclass cls,
	jintArray controls)
{
	int32_t copy[TOUCH_BUTTONS];
	int serial = host_touch_bindings_read(copy);

	(void)cls;
	if (controls && (*env)->GetArrayLength(env, controls) >= TOUCH_BUTTONS)
		(*env)->SetIntArrayRegion(env, controls, 0, TOUCH_BUTTONS, (const jint *)copy);
	return serial;
}

/* the overlay's devices, as it last found them (TouchControls'
refreshInputDevices) */
JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeDevices(JNIEnv *env, jclass cls,
	jboolean touchscreen, jboolean controller)
{
	(void)env;
	(void)cls;
	touch_devices = (touchscreen ? HOST_TOUCH_DEVICE_TOUCHSCREEN : 0) |
		(controller ? HOST_TOUCH_DEVICE_CONTROLLER : 0);
}

/* the guest's: the overlay's devices as HOST_TOUCH_DEVICE_* bits; 0 until it
has looked */
int host_touch_devices(void)
{
	return touch_devices;
}

/* 0 until the game has read its controller once */
JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeScene(JNIEnv *env, jclass cls)
{
	(void)env;
	(void)cls;
	return host_touch_scene_read();
}

/* the picture's top in a portrait window, in its pixels (-1: none) */
JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativePictureTop(JNIEnv *env, jclass cls, jint top)
{
	(void)env;
	(void)cls;
	host_touch_set_picture_top(top);
}
#endif
