/*
UPDATE_SDL.H (test)

A fake of the parts of SDL the self-updater (port/linux/src/updater.c)
uses, over stdio and pthreads, for the tests that compile the whole file
for this computer (update_gamescope.c, update_fallback.c): files, atomics,
threads and mutexes as they are; no window is made, and a system dialog is
counted (dialogs_shown, windows_made) and printed, not shown.
*/

#ifndef UPDATE_SDL_H
#define UPDATE_SDL_H

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ---------- SDL, the parts the updater uses */

typedef int64_t Sint64;
typedef uint64_t Uint64;
typedef uint32_t Uint32;
typedef uint8_t Uint8;
#define SDLCALL
#define SDL_IO_SEEK_SET SEEK_SET
#define SDL_IO_SEEK_CUR SEEK_CUR
#define SDL_SOFTWARE_RENDERER "software"
#define SDL_WINDOW_FULLSCREEN 1
#define SDL_MESSAGEBOX_ERROR 0x10
#define SDL_MESSAGEBOX_WARNING 0x20
#define SDL_MESSAGEBOX_INFORMATION 0x40
#define SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT 1
#define SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT 2

typedef struct SDL_IOStream { FILE *file; } SDL_IOStream;
typedef struct SDL_PathInfo SDL_PathInfo;
typedef struct { int value; } SDL_AtomicInt;
typedef struct SDL_Thread { pthread_t thread; } SDL_Thread;
typedef struct SDL_Mutex { pthread_mutex_t mutex; } SDL_Mutex;
typedef struct SDL_Window { int unused; } SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct { float x, y, w, h; } SDL_FRect;
typedef union { Uint32 type; } SDL_Event;
typedef int (*SDL_ThreadFunction)(void *data);
typedef struct { Uint32 flags; int buttonID; const char *text; } SDL_MessageBoxButtonData;
typedef struct
{
	Uint32 flags;
	SDL_Window *window;
	const char *title, *message;
	int numbuttons;
	const SDL_MessageBoxButtonData *buttons;
	const void *colorScheme;
} SDL_MessageBoxData;

/* what the player saw of the updater beyond the game's own window */
static int dialogs_shown, windows_made;

static SDL_IOStream *SDL_IOFromFile(const char *path, const char *mode)
{
	FILE *file = fopen(path, mode);
	SDL_IOStream *stream;

	if (!file)
		return NULL;
	stream = malloc(sizeof(*stream));
	stream->file = file;
	return stream;
}

static size_t SDL_ReadIO(SDL_IOStream *stream, void *data, size_t size)
{
	return fread(data, 1, size, stream->file);
}

static size_t SDL_WriteIO(SDL_IOStream *stream, const void *data, size_t size)
{
	return fwrite(data, 1, size, stream->file);
}

static Sint64 SDL_SeekIO(SDL_IOStream *stream, Sint64 offset, int whence)
{
	return fseeko(stream->file, (off_t)offset, whence) ? -1 : (Sint64)ftello(stream->file);
}

static Sint64 SDL_TellIO(SDL_IOStream *stream)
{
	return (Sint64)ftello(stream->file);
}

static Sint64 SDL_GetIOSize(SDL_IOStream *stream)
{
	struct stat information;

	return fstat(fileno(stream->file), &information) ? -1 : (Sint64)information.st_size;
}

static bool SDL_CloseIO(SDL_IOStream *stream)
{
	int closed = fclose(stream->file) == 0;

	free(stream);
	return closed;
}

static void *SDL_LoadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)length + 1);
	*size = fread(data, 1, (size_t)length, file);
	data[*size] = 0;
	fclose(file);
	return data;
}

static void SDL_free(void *data)
{
	free(data);
}

static bool SDL_GetPathInfo(const char *path, SDL_PathInfo *information)
{
	struct stat status;

	(void)information;
	return stat(path, &status) == 0;
}

static const char *SDL_GetError(void)
{
	return strerror(errno);
}

static int SDL_SetAtomicInt(SDL_AtomicInt *atomic, int value)
{
	return __atomic_exchange_n(&atomic->value, value, __ATOMIC_SEQ_CST);
}

static int SDL_GetAtomicInt(SDL_AtomicInt *atomic)
{
	return __atomic_load_n(&atomic->value, __ATOMIC_SEQ_CST);
}

struct thread_start { SDL_ThreadFunction function; void *data; };

static void *thread_main(void *context)
{
	struct thread_start start = *(struct thread_start *)context;

	free(context);
	start.function(start.data);
	return NULL;
}

static SDL_Thread *SDL_CreateThread(SDL_ThreadFunction function, const char *name, void *data)
{
	SDL_Thread *thread = malloc(sizeof(*thread));
	struct thread_start *start = malloc(sizeof(*start));

	(void)name;
	start->function = function;
	start->data = data;
	if (pthread_create(&thread->thread, NULL, thread_main, start))
	{
		free(start);
		free(thread);
		return NULL;
	}
	return thread;
}

static void SDL_DetachThread(SDL_Thread *thread)
{
	pthread_detach(thread->thread);
	free(thread);
}

static void SDL_WaitThread(SDL_Thread *thread, int *status)
{
	(void)status;
	pthread_join(thread->thread, NULL);
	free(thread);
}

static SDL_Mutex *SDL_CreateMutex(void)
{
	SDL_Mutex *mutex = malloc(sizeof(*mutex));

	pthread_mutex_init(&mutex->mutex, NULL);
	return mutex;
}

static void SDL_LockMutex(SDL_Mutex *mutex) { pthread_mutex_lock(&mutex->mutex); }
static void SDL_UnlockMutex(SDL_Mutex *mutex) { pthread_mutex_unlock(&mutex->mutex); }
static void SDL_DestroyMutex(SDL_Mutex *mutex) { pthread_mutex_destroy(&mutex->mutex); free(mutex); }

/* (the desktop's download window: none is made, and it draws nothing) */
static SDL_Window *SDL_CreateWindow(const char *title, int width, int height, Uint64 flags)
{
	(void)title, (void)width, (void)height, (void)flags;
	windows_made++;
	printf("test: the updater made a window\n");
	return NULL;
}

static SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, const char *name) { (void)window, (void)name; return NULL; }
static void SDL_DestroyRenderer(SDL_Renderer *renderer) { (void)renderer; }
static void SDL_DestroyWindow(SDL_Window *window) { (void)window; }
static void SDL_SetRenderDrawColor(SDL_Renderer *r, Uint8 a, Uint8 b, Uint8 c, Uint8 d) { (void)r, (void)a, (void)b, (void)c, (void)d; }
static void SDL_SetRenderScale(SDL_Renderer *renderer, float x, float y) { (void)renderer, (void)x, (void)y; }
static void SDL_RenderClear(SDL_Renderer *renderer) { (void)renderer; }
static void SDL_RenderDebugText(SDL_Renderer *r, float x, float y, const char *text) { (void)r, (void)x, (void)y, (void)text; }
static void SDL_RenderFillRect(SDL_Renderer *renderer, const SDL_FRect *rectangle) { (void)renderer, (void)rectangle; }
static void SDL_RenderPresent(SDL_Renderer *renderer) { (void)renderer; }
static bool SDL_PollEvent(SDL_Event *event) { (void)event; return false; }
/* (a test may wait in its own way: UPDATE_SDL_OWN_DELAY) */
#ifndef UPDATE_SDL_OWN_DELAY
static void SDL_Delay(Uint32 milliseconds) { usleep(milliseconds * 1000); }
#endif
static Uint64 SDL_GetWindowFlags(SDL_Window *window) { (void)window; return 0; }
static bool SDL_SetWindowFullscreen(SDL_Window *window, bool fullscreen) { (void)window, (void)fullscreen; return true; }

/* (a system dialog: under gamescope it would stop the game; the desktop's
question is answered FAKE_DIALOG_ANSWER, No by default) */
static bool SDL_ShowMessageBox(const SDL_MessageBoxData *data, int *button)
{
	const char *answer = getenv("FAKE_DIALOG_ANSWER");

	dialogs_shown++;
	printf("test: a dialog: %s\n", data->title);
	*button = answer ? atoi(answer) : 0;
	return true;
}

static bool SDL_ShowSimpleMessageBox(Uint32 flags, const char *title, const char *message, SDL_Window *window)
{
	(void)flags, (void)window;
	dialogs_shown++;
	printf("test: a dialog: %s: %s\n", title, message);
	return true;
}

#endif
