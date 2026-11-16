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

#include "update_sdl.h"

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

/* GitHub: the latest release, build FAKE_LATEST, whose zip is FAKE_RELEASE;
its SHA256SUMS and signature: none (GitHub's 404) */
int update_download(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, int error_size)
{
	FILE *output;
	const char *latest = "/releases/latest";
	const char *name = strrchr(url, '/');
	size_t length = strlen(url);

	(void)maximum, (void)progress, (void)context;
	name = name ? name + 1 : url;
	if (!strcmp(name, "SHA256SUMS") || !strcmp(name, "SHA256SUMS.sig"))
	{
		printf("test: asked for %s\n", name);
		fflush(stdout);
		snprintf(error, (size_t)error_size, "the server answered 404");
		return 0;
	}
	printf("test: download %s\n", url);
	fflush(stdout);
	output = fopen(path, "wb");
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

/* report.c's: this session ended before the new build starts, or (0) taken
up again; said for the test to see, before the restart */
static void report_session_handover(int leaving)
{
	printf("test: session %s\n", leaving ? "handed over" : "taken up again");
	fflush(stdout);
}

static void platform_request_quit(void)
{
	quit_requested = 1;
	printf("test: quit\n");
}

/* (GitHub's API, which the updater asks only for a file its release lists:
none is listed here) and the check of the download (release_signature.c's,
test_release_signature.py's) as FAKE_VERDICT says: "refused", else taken
as an unsigned release */
int update_https_request(const char *method, const char *url, const char *headers, const void *body,
	unsigned long body_size, unsigned long maximum, struct update_response *response, char *error, int error_size)
{
	(void)method, (void)headers, (void)body, (void)body_size, (void)maximum, (void)error, (void)error_size;
	printf("test: asked GitHub's API for %s\n", url);
	memset(response, 0, sizeof(*response));
	response->status = 404;
	response->body = calloc(1, 1);
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
