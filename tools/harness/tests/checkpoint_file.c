/*
CHECKPOINT_FILE.C (test)

A checkpoint's file (saved games/game_state_xbox.c, written by
port/linux/src/saved_game_writer.c): the image is taken at the checkpoint and
written on the writer's thread while the game goes on; a revert's read and
the next checkpoint wait for the write; the file is always a whole image,
the last or the one before it, even if the game stops in the middle of a
write; a write that fails leaves the one before it. test_checkpoint_file.py
takes the code from the sources; the game state and the Xbox's files are
the test's.
*/

#include "harness.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------- the writes: slowed, failed, or the game stopped in the middle */

static size_t test_write_chunk; /* at most this many bytes a write (0: all) */
static useconds_t test_write_delay; /* before each write */
static long test_write_stop_after = -1; /* bytes written before the game stops (-1: never) */
static long test_write_fail_after = -1; /* bytes written before the disk is full (-1: never) */
static long test_written;

static ssize_t test_write(int descriptor, const void *buffer, size_t size)
{
	ssize_t result;

	if (test_write_chunk && size > test_write_chunk)
		size = test_write_chunk;
	if (test_write_stop_after >= 0 && test_written + (long)size > test_write_stop_after)
	{
		if (write(descriptor, buffer, (size_t)(test_write_stop_after - test_written)) < 0)
			_exit(3);
		_exit(0);
	}
	if (test_write_fail_after >= 0 && test_written + (long)size > test_write_fail_after)
	{
		errno = ENOSPC;
		return -1;
	}
	if (test_write_delay)
		usleep(test_write_delay);
	result = write(descriptor, buffer, size);
	if (result > 0)
		test_written += result;
	return result;
}
#define write test_write

/* ---------- the platform */

static char test_directory[512];

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

static void load_trace_log(const char *format, ...)
{
	(void)format;
}

/* z:\<name> is <test directory>/<name> */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	snprintf(host_path, host_path_size, "%s/%s", test_directory, xbox_path + 3);
}

/* ---------- the Xbox's files */

typedef void *HANDLE;
typedef int BOOL;
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define FILE_FLAG_NO_BUFFERING 0x20000000UL
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000UL

static HANDLE CreateFileA(const char *name, unsigned long access, unsigned long share, void *security,
	unsigned long disposition, unsigned long flags, HANDLE template_file)
{
	char path[1024];
	int descriptor;

	(void)share, (void)security, (void)flags, (void)template_file;
	platform_translate_path(name, path, sizeof(path));
	descriptor = open(path, ((access & GENERIC_WRITE) ? O_RDWR : O_RDONLY) | (disposition == OPEN_ALWAYS ? O_CREAT : 0),
		0644);
	return descriptor < 0 ? INVALID_HANDLE_VALUE : (HANDLE)(long)descriptor;
}

static BOOL ReadFile(HANDLE file, void *buffer, unsigned long size, unsigned long *bytes_read, void *overlapped)
{
	ssize_t result = read((int)(long)file, buffer, size);

	(void)overlapped;
	*bytes_read = result > 0 ? (unsigned long)result : 0;
	return result >= 0;
}

static unsigned long GetFileSize(HANDLE file, unsigned long *high)
{
	struct stat information;

	(void)high;
	return fstat((int)(long)file, &information) == 0 ? (unsigned long)information.st_size : 0xFFFFFFFFUL;
}

static BOOL CloseHandle(HANDLE file)
{
	return close((int)(long)file) == 0;
}

/* (an int: the game prints it with %d) */
static int GetLastError(void)
{
	return errno;
}

/* GAME_STATE_FILE_SIZE, the Xbox side's globals, the checkpoint's image */
#include "types.inc"

/* ---------- the game */

static int test_vasserts;
static char temporary[256];
#define match_vassert(file, line, condition, message) ((condition) ? (void)0 : ((void)(message), (void)test_vasserts++))
#define csprintf(buffer, ...) (snprintf(buffer, sizeof(buffer), __VA_ARGS__), buffer)

boolean recover_saved_games_hack;

/* the game state: the part of it in use (game_state_image_take's) */
#define TEST_STATE_SIZE (1024L * 1024L + 12L)
static unsigned char test_state[TEST_STATE_SIZE];
static int test_accepted_size;

static long game_state_image_size(void)
{
	return TEST_STATE_SIZE;
}

static void game_state_image_take(void *image)
{
	memcpy(image, test_state, TEST_STATE_SIZE);
}

/* an image of the part in use, or of the whole game state (an older
build's file) */
static boolean game_state_image_accept(void *image, long size)
{
	if (size != TEST_STATE_SIZE && size != GAME_STATE_FILE_SIZE)
		return FALSE;
	memcpy(test_state, image, TEST_STATE_SIZE);
	test_accepted_size = (int)size;
	return TRUE;
}

static struct xbox_game_state_globals_prefix xbox_game_state_globals;

#include "under_test.inc"

/* ---------- the cases */

static void test_state_fill(unsigned char seed)
{
	long index;

	for (index = 0; index < TEST_STATE_SIZE; index++)
		test_state[index] = (unsigned char)(seed + index * 7 + (index >> 9));
}

static void test_path(char const *name, char *path, size_t size)
{
	snprintf(path, size, "%s/%s", test_directory, name);
}

static boolean test_file_exists(char const *name)
{
	char path[1024];

	test_path(name, path, sizeof(path));
	return access(path, F_OK) == 0;
}

/* whether the file holds exactly the image a game state of the seed takes */
static boolean test_file_is(char const *name, unsigned char seed)
{
	static unsigned char expected[TEST_STATE_SIZE], found[TEST_STATE_SIZE + 1];
	unsigned char saved[TEST_STATE_SIZE];
	char path[1024];
	FILE *file;
	size_t length;

	memcpy(saved, test_state, TEST_STATE_SIZE);
	test_state_fill(seed);
	memcpy(expected, test_state, TEST_STATE_SIZE);
	memcpy(test_state, saved, TEST_STATE_SIZE);
	test_path(name, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return FALSE;
	length = fread(found, 1, sizeof(found), file);
	fclose(file);
	return length == TEST_STATE_SIZE && !memcmp(found, expected, TEST_STATE_SIZE);
}

static boolean test_state_is(unsigned char seed)
{
	static unsigned char expected[TEST_STATE_SIZE];
	unsigned char saved[TEST_STATE_SIZE];

	memcpy(saved, test_state, TEST_STATE_SIZE);
	test_state_fill(seed);
	memcpy(expected, test_state, TEST_STATE_SIZE);
	memcpy(test_state, saved, TEST_STATE_SIZE);
	return !memcmp(test_state, expected, TEST_STATE_SIZE);
}

/* the writes slowed: 17 of 64 KB, 2 ms each */
static void test_writes_slow(void)
{
	test_write_chunk = 64 * 1024;
	test_write_delay = 2000;
}

static void test_writes_normal(void)
{
	test_write_chunk = 0;
	test_write_delay = 0;
}

static void test_start(void)
{
	xbox_game_state_globals.buffer_allocated = TRUE;
	game_state_create_or_open_file();
	CHECK(xbox_game_state_globals.file_open, "the file could not be made");
}

/* the test's folder removed as the test ends, passed or failed */
static void test_remove_directory(void)
{
	static char const *names[] = { "savegame.bin", "savegame.bin.new" };
	char path[1024];
	unsigned index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		test_path(names[index], path, sizeof(path));
		unlink(path);
	}
	rmdir(test_directory);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";

	snprintf(test_directory, sizeof(test_directory), "/tmp/checkpoint-file-XXXXXX");
	CHECK(mkdtemp(test_directory), "no folder for the test");
	atexit(test_remove_directory);
	test_start();

	/* the image taken at the checkpoint, written while the game goes on */
	CASE("background")
	{
		test_writes_slow();
		test_state_fill(1);
		CHECK(game_state_write_to_file(), "the checkpoint was not taken");
		CHECK(!test_file_is("savegame.bin", 1), "the checkpoint was written on the game's thread");
		test_state_fill(2);
		CHECK(game_state_write_wait(), "the write failed");
		CHECK(test_file_is("savegame.bin", 1), "the file is not the image taken at the checkpoint");
		CHECK(!test_file_exists("savegame.bin.new"), "the file written beside it is left");
		CHECK(!test_vasserts, "an assertion failed");
	}

	/* a revert reads the checkpoint the write in progress finishes */
	CASE("revert-waits")
	{
		test_writes_slow();
		test_state_fill(1);
		CHECK(game_state_write_to_file(), "the checkpoint was not taken");
		test_state_fill(2);
		CHECK(game_state_read_from_file(), "the revert read no checkpoint");
		CHECK(test_state_is(1), "the revert did not go back to the checkpoint");
		CHECK(test_accepted_size == TEST_STATE_SIZE, "the image is not of the part in use");
		CHECK(!test_vasserts, "an assertion failed");
	}

	/* the next checkpoint waits for the last one's write before it takes
	the game state's image: the last one is written whole (the game stopped
	in the middle of the next write shows what it left) */
	CASE("next-waits")
	{
		pid_t child;
		int status;

		test_writes_slow();
		child = fork();
		CHECK(child >= 0, "no fork");
		if (!child)
		{
			test_write_stop_after = TEST_STATE_SIZE + TEST_STATE_SIZE / 2;
			test_state_fill(1);
			game_state_write_to_file();
			test_state_fill(2);
			game_state_write_to_file();
			game_state_write_wait();
			_exit(4);
		}
		CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status),
			"the game did not stop in the middle of the write (%d)", status);
		CHECK(test_file_is("savegame.bin", 1), "the last checkpoint was not written whole before the next");
		test_state_fill(1);
		CHECK(game_state_write_to_file(), "the first checkpoint was not taken");
		test_state_fill(2);
		CHECK(game_state_write_to_file(), "the second checkpoint was not taken");
		test_state_fill(3);
		CHECK(game_state_write_wait(), "a write failed");
		CHECK(test_file_is("savegame.bin", 2), "the file is not the second checkpoint");
		CHECK(game_state_read_from_file() && test_state_is(2), "the revert did not go back to the second checkpoint");
	}

	/* the game stops in the middle of a write: the checkpoint before it is
	whole, and a later run reads it (recover_saved_games_hack) */
	CASE("stopped")
	{
		pid_t child;
		int status;

		test_writes_slow();
		child = fork();
		CHECK(child >= 0, "no fork");
		if (!child)
		{
			test_state_fill(1);
			game_state_write_to_file();
			game_state_write_wait();
			test_written = 0;
			test_write_stop_after = TEST_STATE_SIZE / 2;
			test_state_fill(2);
			game_state_write_to_file();
			game_state_write_wait();
			_exit(4);
		}
		CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status),
			"the game did not stop in the middle of the write (%d)", status);
		CHECK(test_file_is("savegame.bin", 1), "the game stopped in a write left a file that is not a whole image");
		test_writes_normal();
		test_state_fill(9);
		recover_saved_games_hack = TRUE;
		CHECK(game_state_read_from_file() && test_state_is(1), "the checkpoint before was not read back");
	}

	/* a write that fails (the disk full) leaves the checkpoint before it,
	and no saved game is said to be there */
	CASE("failed")
	{
		test_writes_normal();
		test_state_fill(1);
		CHECK(game_state_write_to_file() && game_state_write_wait(), "the first checkpoint was not written");
		test_written = 0;
		test_write_fail_after = TEST_STATE_SIZE / 2;
		test_state_fill(2);
		CHECK(game_state_write_to_file(), "the second checkpoint was not taken");
		CHECK(!game_state_write_wait(), "a failed write was not reported");
		CHECK(test_vasserts == 1, "the failed write was asserted %d times", test_vasserts);
		CHECK(!xbox_game_state_globals.file_valid_for_read, "a failed write left the file valid");
		CHECK(!game_state_write_wait() && test_vasserts == 1, "the failed write was reported again");
		CHECK(test_file_is("savegame.bin", 1), "a failed write damaged the checkpoint before it");
		CHECK(!test_file_exists("savegame.bin.new"), "a failed write left its file");
		test_write_fail_after = -1;
		test_state_fill(3);
		CHECK(game_state_write_to_file() && game_state_write_wait(), "the next checkpoint was not written");
		CHECK(xbox_game_state_globals.file_valid_for_read, "the next checkpoint's file is not valid");
	}

	/* an older build's file, of the whole game state, read (a later run,
	recover_saved_games_hack) */
	CASE("older-file")
	{
		static unsigned char whole[GAME_STATE_FILE_SIZE];
		char path[1024];
		FILE *file;

		test_state_fill(5);
		memcpy(whole, test_state, TEST_STATE_SIZE);
		test_path("savegame.bin", path, sizeof(path));
		file = fopen(path, "wb");
		CHECK(file && fwrite(whole, 1, sizeof(whole), file) == sizeof(whole) && !fclose(file), "no older file");
		test_state_fill(6);
		recover_saved_games_hack = TRUE;
		CHECK(game_state_read_from_file(), "the older file was not read");
		CHECK(test_accepted_size == GAME_STATE_FILE_SIZE && test_state_is(5), "the older file was not taken whole");
	}

	game_state_close_file();
	return 0;
}
