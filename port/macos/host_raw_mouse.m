/*
HOST_RAW_MOUSE.M

The mouse's own motion on macOS, unaccelerated. SDL's relative motion on
macOS is the NSEvent's delta, which the system's pointer acceleration has
already scaled, so the game's mouse aim (input.mouse_sensitivity, on
Counter-Strike 2's scale: port/linux/include/mouse_sensitivity.h) would not
turn 0.022 degrees a count. The GameController framework's GCMouse reports
the device's counts as they are: its handler runs on a queue of its own,
off the main thread, and adds them up here, and host_sdl_poll_event
(port/android/host/host_sdl.c) gives them to the game in place of SDL's
while the mouse is captured, as each poll finds them: SDL's motion events
take what has come since the last, and a poll that finds no event left
hands over what has come since as one more, so that a count that arrived
before the game reads its input this frame turns this frame's view.

A pointer GCMouse does not report (a trackpad, which is no GCMouse) keeps
SDL's motion: while no count has come for RAW_MOUSE_IDLE_NS, SDL's
deltas pass as they are. Not on iOS, whose SDL reads GCMouse itself.

The cursor while the mouse is captured in fullscreen
(host_raw_mouse_cursor): a hidden cursor ([NSCursor hide], as SDL's
relative mode hides it, or an invisible cursor image, SDL_HideCursor's) in a
fullscreen Space has the window server composite the window again, where
the game's frames otherwise go to the display directly, a refresh sooner
(on an M2 at 60 Hz, input to screen 40 ms instead of 24; a fullscreen
window outside a Space, or a visible cursor, kept the direct path). So the
cursor stays shown there, as one black point, parked in the window's
bottom right corner, where the relative mode holds it
(CGAssociateMouseAndMouseCursorPosition).
*/

#include <TargetConditionals.h>

#if !TARGET_OS_IPHONE

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#include <SDL3/SDL.h>

#include <math.h>
#include <pthread.h>
#include <string.h>
#include <time.h>

/* how long after the last count the mouse is taken to be still, and SDL's
motion (a trackpad's) to be the one moving */
#define RAW_MOUSE_IDLE_NS 100000000ull

static pthread_mutex_t raw_lock = PTHREAD_MUTEX_INITIALIZER;
static float raw_x, raw_y;
static unsigned long long raw_moved_ns;
static dispatch_queue_t raw_queue;
static id raw_observer;

static unsigned long long raw_now_ns(void)
{
	return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static void raw_mouse_watch(GCMouse *mouse)
{
	mouse.handlerQueue = raw_queue;
	mouse.mouseInput.mouseMovedHandler = ^(GCMouseInput *input, float delta_x, float delta_y) {
		(void)input;
		pthread_mutex_lock(&raw_lock);
		/* (GCMouse's y is up; SDL's, and the game's, down) */
		raw_x += delta_x;
		raw_y -= delta_y;
		raw_moved_ns = raw_now_ns();
		pthread_mutex_unlock(&raw_lock);
	};
}

void host_raw_mouse_start(void)
{
	if (raw_queue)
		return;
	raw_queue = dispatch_queue_create("com.openh1.raw-mouse", DISPATCH_QUEUE_SERIAL);
	for (GCMouse *mouse in [GCMouse mice])
		raw_mouse_watch(mouse);
	raw_observer = [[NSNotificationCenter defaultCenter] addObserverForName:GCMouseDidConnectNotification object:nil
		queue:nil usingBlock:^(NSNotification *note) {
			raw_mouse_watch(note.object);
		}];
}

/* how often the captured cursor is looked at (host_raw_mouse_cursor), and
how far inside the window's corner it is parked, in points (not in it: a
hot corner there would act) */
#define RAW_CURSOR_CHECK_NS 100000000ull
#define RAW_CURSOR_INSET 4.0f

/* whether the cursor is parked (host_raw_mouse_cursor), and the one shown
otherwise: the menus' (host_raw_mouse_menu_cursor), or NULL for the
system's */
static int parked;
static SDL_Cursor *raw_menu_cursor;

/* (the main thread: port/linux/host/host_desktop.c's host_sdl_menu_cursor)
the cursor shown while the mouse is not parked, set now unless it is */
void host_raw_mouse_menu_cursor(SDL_Cursor *cursor)
{
	raw_menu_cursor = cursor;
	if (!parked)
		SDL_SetCursor(cursor ? cursor : SDL_GetDefaultCursor());
}

/* (the main thread, at each poll of the events) the cursor while the mouse
is captured in fullscreen: shown, one black point, parked (above); let go,
the menus' again */
void host_raw_mouse_cursor(void)
{
	static SDL_Cursor *point;
	static unsigned long long checked_ns;
	SDL_Window *focus = SDL_GetKeyboardFocus();
	int wanted = focus && SDL_GetWindowRelativeMouseMode(focus) &&
		(SDL_GetWindowFlags(focus) & SDL_WINDOW_FULLSCREEN);
	unsigned long long now;
	int x = 0, y = 0, width = 0, height = 0, shows = 0;
	float at_x, at_y;

	if (wanted != parked)
	{
		parked = wanted;
		checked_ns = 0;
		SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE, wanted ? "1" : "0");
		if (wanted && !point)
		{
			SDL_Surface *surface = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);

			if (surface)
			{
				/* (opaque: an invisible image hides the cursor too) */
				memset(surface->pixels, 0, 3);
				((Uint8 *)surface->pixels)[3] = 255;
				point = SDL_CreateColorCursor(surface, 0, 0);
				SDL_DestroySurface(surface);
			}
		}
		SDL_SetCursor(wanted && point ? point : raw_menu_cursor ? raw_menu_cursor : SDL_GetDefaultCursor());
		/* (let go: back in the window's middle, where SDL's relative mode
		leaves it, not in the corner) */
		if (!wanted && focus)
		{
			SDL_GetWindowSize(focus, &width, &height);
			SDL_WarpMouseInWindow(focus, width / 2.0f, height / 2.0f);
		}
	}
	if (!parked)
		return;
	now = raw_now_ns();
	if (checked_ns && now - checked_ns < RAW_CURSOR_CHECK_NS)
		return;
	checked_ns = now;
	/* (SDL's relative mode hides it as it turns on, each time) */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
	while (!CGCursorIsVisible() && shows++ < 8)
		[NSCursor unhide];
#pragma clang diagnostic pop
	SDL_GetWindowPosition(focus, &x, &y);
	SDL_GetWindowSize(focus, &width, &height);
	SDL_GetGlobalMouseState(&at_x, &at_y);
	if (width > 2 * RAW_CURSOR_INSET && height > 2 * RAW_CURSOR_INSET &&
		(fabsf(at_x - (x + width - RAW_CURSOR_INSET)) > 1.0f || fabsf(at_y - (y + height - RAW_CURSOR_INSET)) > 1.0f))
		CGWarpMouseCursorPosition(CGPointMake(x + width - RAW_CURSOR_INSET, y + height - RAW_CURSOR_INSET));
}

/* the counts since the last call, if the mouse has moved lately (else 0:
SDL's motion stands) */
int host_raw_mouse_take(float *x, float *y)
{
	int active;

	pthread_mutex_lock(&raw_lock);
	active = raw_moved_ns && raw_now_ns() - raw_moved_ns < RAW_MOUSE_IDLE_NS;
	*x = raw_x;
	*y = raw_y;
	raw_x = 0.0f;
	raw_y = 0.0f;
	pthread_mutex_unlock(&raw_lock);
	return active;
}

#endif
