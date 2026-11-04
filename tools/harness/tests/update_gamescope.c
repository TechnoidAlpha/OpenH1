/*
UPDATE_GAMESCOPE.C (test)

The self-updater (port/linux/src/updater.c, the whole file) in a Linux
release's tree, its new version a local fake release: the question under
gamescope (the Steam Deck's Game Mode) asked in the game's menus at the main
menu, after a background download, never with a system dialog; UPDATE puts
the files in place and starts the new bin/openh1 in this process
(posix_update.c's update_restart); NOT NOW changes nothing. The desktop's
question stays a dialog. test_update_gamescope.py takes the code
(under_test.inc: posix_update.c's files and processes, and updater.c), and
runs the cases natively, each in a process of its own (FAKE_* in the
environment), as the restart replaces it.

Compiled for this computer, not the game's 32-bit ABI: the host's side
(posix_update.c) runs here, and SDL is a fake of the parts the updater uses,
over stdio and pthreads.
*/

#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* (the real one: the system side's functions, posix_update.c's but the
download, in under_test.inc) */
#include "update.h"
/* (the real one, and p2p_internal.h's SHA-256: faked below) */
#include "release_signature.h"
#include "p2p_internal.h"

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
static void SDL_Delay(Uint32 milliseconds) { usleep(milliseconds * 1000); }
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

/* ---------- the game's */

#define INSTALL_NAME "openh1"

static void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
	putchar('\n');
	/* (before the restart, which drops what stdio holds) */
	fflush(stdout);
}

static const char *environment(const char *name)
{
	const char *value = getenv(name);

	return value ? value : "";
}

static const char *config_string(const char *name)
{
	if (!strcmp(name, "debug.update_from"))
		return environment("FAKE_UPDATE_FROM");
	if (!strcmp(name, "debug.update_answer"))
		return environment("FAKE_UPDATE_ANSWER");
	return "";
}

static int config_boolean(const char *name)
{
	return !strcmp(name, "update.auto") && strcmp(environment("FAKE_UPDATE_AUTO"), "false");
}

static double config_real(const char *name)
{
	(void)name;
	return 0.0;
}

/* (a dedicated server's: its network.port, which names its update folder) */
static long config_integer(const char *name)
{
	(void)name;
	return 0;
}

static int config_write_boolean(const char *name, int value)
{
	printf("test: config.toml: %s = %s\n", name, value ? "true" : "false");
	return 1;
}

static int install_is_system(void) { return 0; }
static const char *install_system_reason(void) { return ""; }
static int install_is_tree(void) { return 1; }
static const char *install_folder(void) { return environment("FAKE_TREE"); }

/* GitHub: the latest release, build FAKE_LATEST, whose zip is FAKE_RELEASE */
int update_download(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size)
{
	FILE *output = fopen(path, "wb");
	const char *latest = "/releases/latest";
	size_t length = strlen(url);

	(void)maximum, (void)progress, (void)context;
	printf("test: download %s\n", url);
	fflush(stdout);
	if (!output)
	{
		snprintf(error, (size_t)error_size, "could not write %s", path);
		return 0;
	}
	if (length > strlen(latest) && !strcmp(url + length - strlen(latest), latest))
	{
		fprintf(output, "{\"url\": \"...\", \"tag_name\": \"build-%s\", \"name\": \"...\"}", environment("FAKE_LATEST"));
	}
	else
	{
		FILE *input = fopen(environment("FAKE_RELEASE"), "rb");
		char buffer[16384];
		size_t count;

		if (!input)
		{
			fclose(output);
			snprintf(error, (size_t)error_size, "the server answered 404");
			return 0;
		}
		while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0)
			fwrite(buffer, 1, count, output);
		fclose(input);
	}
	fclose(output);
	return 1;
}

/* the main menu (menu_functions.c's): drawn from frame MAIN_MENU_FRAME on,
a game played before it */
#define MAIN_MENU_FRAME 40
static int main_menu_drawn, question_opened, quit_requested;

static int pc_menu_update_question_open(void)
{
	int shown = main_menu_drawn;

	main_menu_drawn = 0;
	if (!shown)
		return 0;
	question_opened++;
	printf("test: the update question opened over the main menu\n");
	return 1;
}

static void platform_request_quit(void)
{
	quit_requested = 1;
	printf("test: quit\n");
}

/* the release's SHA256SUMS and signature: none (GitHub's 404), and the check
of the download (release_signature.c's, test_release_signature.py's) as
FAKE_VERDICT says: "refused", else taken as an unsigned release */
int update_https_request(const char *method, const char *url, const char *headers, const void *body,
	unsigned long body_size, unsigned long maximum, struct update_response *response, char *error, int error_size)
{
	const char *name = strrchr(url, '/');

	(void)method, (void)headers, (void)body, (void)body_size, (void)maximum, (void)error, (void)error_size;
	printf("test: asked for %s\n", name ? name + 1 : url);
	memset(response, 0, sizeof(*response));
	response->status = 404;
	return 1;
}

void p2p_sha256_begin(struct p2p_sha256 *context)
{
	memset(context, 0, sizeof(*context));
}

void p2p_sha256_add(struct p2p_sha256 *context, const void *data, int size)
{
	(void)data;
	context->length += (unsigned long long)size;
}

void p2p_sha256_end(struct p2p_sha256 *context, unsigned char *digest)
{
	memset(digest, 0, 32);
	memcpy(digest, &context->length, sizeof(context->length));
}

enum release_verdict release_check(const char *sums, int sums_size, const char *signature, int signature_size,
	const char *asset, const unsigned char *digest, int required, const char *test_key, char *error, int error_size)
{
	int refused = !strcmp(environment("FAKE_VERDICT"), "refused");

	(void)sums_size, (void)signature_size, (void)digest, (void)test_key;
	printf("test: release check of %s: sums %d, signature %d, required %d: %s\n", asset, sums != NULL,
		signature != NULL, required, refused ? "refused" : "unsigned");
	snprintf(error, (size_t)error_size, "%s", refused ? "a test's refusal" : "a test's unsigned release");
	return refused ? _release_refused : _release_unsigned;
}

#include "under_test.inc"

/* FAKE_PLAYER: what the player does with the question: "update" (UPDATE),
"not now" (NOT NOW or B), or nothing */
int main(int argc, char **argv)
{
	SDL_Window window = { 0 };
	const char *player = environment("FAKE_PLAYER");
	char held[1200];
	int frame, held_file;

	(void)argc, (void)argv;
	setvbuf(stdout, NULL, _IOLBF, 0);
	/* (a file the game holds, as its sockets: not the new version's) */
	snprintf(held, sizeof(held), "%s/held-by-the-old-game", install_folder());
	held_file = open(held, O_CREAT | O_WRONLY, 0644);
	CHECK(held_file >= 0, "could not open %s", held);
	printf("test: started, process %d\n", (int)getpid());
	updater_start();
	for (frame = 0; frame < 5000 && !quit_requested; frame++)
	{
		int state = SDL_GetAtomicInt(&updater_state);

		/* (until the updater is done: not looking, answered, or asked and
		the player's choice made) */
		if (state == _updater_idle || state == _updater_handled || (state == _updater_asked && question_opened > 1))
			break;
		main_menu_drawn = frame >= MAIN_MENU_FRAME;
		updater_poll(&window);
		if (question_opened == 1 && !strcmp(player, "update"))
		{
			printf("test: UPDATE\n");
			question_opened++;
			updater_update_now();
			/* (once more, as a second press would) */
			updater_update_now();
		}
		else if (question_opened == 1 && !strcmp(player, "not now"))
		{
			printf("test: NOT NOW\n");
			question_opened++;
		}
		usleep(2000);
	}
	printf("test: the game goes on: %d question(s), %d dialog(s), %d window(s)\n", question_opened ? 1 : 0,
		dialogs_shown, windows_made);
	close(held_file);
	return 0;
}
