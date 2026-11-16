/*
SAVED_GAME_CHECK.C

The check of the campaign's saved game (saved_game_check.h) made on a
thread of its own: its header read and the whole of its file (20 MB)
summed and compared with the checksum the header keeps, as
game_state_xbox.c's game_state_read_header_from_persistent_storage does.
The menus ask for it as their screens open (the Campaign menu, Choose
Mission, Load Game), which read it on the game's thread, a frame of tens of
milliseconds (34 on a Ryzen AI Max+ 395). The main menu now asks for it
ahead, once a second (saved_game_check_ahead: a look at the file's size
and time of change, a check begun only when they have changed), and the
screens find it made; only one asked for before its check is done waits for
it, which the check begun ahead has mostly done.

The file is read with the C library on the host's path, and summed with
the game's CRC (crc.c: the same sum the game wrote).
*/

#include "saved_game_check.h"
#include "platform.h"
#include "posix.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (the game's: source/memory/crc.c) */
void crc_new(unsigned long *crc_reference);
void crc_checksum_buffer(unsigned long *crc_reference, void const *buffer, long buffer_size);

/* the longest header (game_state.h's is some hundreds of bytes) */
#define SAVED_GAME_HEADER_MAXIMUM 4096
/* (milliseconds) how often the main menu looks at the file */
#define SAVED_GAME_LOOK_MILLISECONDS 1000
#define SAVED_GAME_READ_SIZE (128 * 1024)

/* the file as a check found it: its path, size and time of change */
struct saved_game_key
{
	char path[1024];
	unsigned long long size;
	unsigned long seconds, nanoseconds;
	long header_size, checksum_offset, buffer_size;
};

static pthread_mutex_t check_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t check_done = PTHREAD_COND_INITIALIZER;
/* (check_lock's) */
static struct
{
	/* the check, running (checking) or made (made) of the file at key */
	struct saved_game_key key;
	int checking, made;
	int valid, corrupted;
	unsigned char header[SAVED_GAME_HEADER_MAXIMUM];
	unsigned long long looked_at;
} check;

static int key_same(const struct saved_game_key *a, const struct saved_game_key *b)
{
	return !strcmp(a->path, b->path) && a->size == b->size && a->seconds == b->seconds &&
		a->nanoseconds == b->nanoseconds && a->header_size == b->header_size &&
		a->checksum_offset == b->checksum_offset && a->buffer_size == b->buffer_size;
}

/* the file's key, or 0 if there is no file */
static int key_of(const char *xbox_path, long header_size, long checksum_offset, long buffer_size,
	struct saved_game_key *key)
{
	struct posix_file_information information;

	memset(key, 0, sizeof(*key));
	platform_translate_path(xbox_path, key->path, sizeof(key->path));
	key->header_size = header_size;
	key->checksum_offset = checksum_offset;
	key->buffer_size = buffer_size;
	if (posix_stat(key->path, &information) != 0)
		return 0;
	key->size = ((unsigned long long)information.size_high << 32) | information.size_low;
	key->seconds = information.modification_seconds;
	key->nanoseconds = information.modification_nanoseconds;
	return 1;
}

/* the check: the header, then the rest of buffer_size bytes, summed (the
header's checksum taken as 0); 1 if the sum is the header's */
static int file_check(const struct saved_game_key *key, unsigned char *header, int *corrupted)
{
	FILE *file = fopen(key->path, "rb");
	unsigned char *buffer;
	unsigned long sum;
	/* (the file's: 32 bits) */
	uint32_t stored = 0;
	long remaining;
	int valid = 0;

	*corrupted = 0;
	memset(header, 0, (size_t)key->header_size);
	if (!file)
		return 0;
	buffer = malloc(SAVED_GAME_READ_SIZE);
	if (buffer && fread(header, 1, (size_t)key->header_size, file) == (size_t)key->header_size)
	{
		memcpy(&stored, header + key->checksum_offset, sizeof(stored));
		memset(header + key->checksum_offset, 0, sizeof(stored));
		crc_new(&sum);
		crc_checksum_buffer(&sum, header, key->header_size);
		/* (a part that cannot be read is not summed, as the game's check
		does: the sum is then not the header's) */
		for (remaining = key->buffer_size - key->header_size; remaining > 0; remaining -= SAVED_GAME_READ_SIZE)
		{
			long size = remaining < SAVED_GAME_READ_SIZE ? remaining : SAVED_GAME_READ_SIZE;

			if (fread(buffer, 1, (size_t)size, file) == (size_t)size)
				crc_checksum_buffer(&sum, buffer, size);
		}
		memcpy(header + key->checksum_offset, &stored, sizeof(stored));
		valid = (uint32_t)sum == stored;
		*corrupted = !valid && stored != 0;
		if (!valid)
			platform_log("saved game: the checksum of %s failed", key->path);
	}
	free(buffer);
	fclose(file);
	return valid;
}

static void *check_thread(void *context)
{
	struct saved_game_key key;
	unsigned char *header = malloc(SAVED_GAME_HEADER_MAXIMUM);
	int valid = 0, corrupted = 0;

	(void)context;
	pthread_mutex_lock(&check_lock);
	key = check.key;
	pthread_mutex_unlock(&check_lock);
	if (header)
		valid = file_check(&key, header, &corrupted);
	pthread_mutex_lock(&check_lock);
	/* (the file asked for since is another's: its check begins again) */
	if (key_same(&key, &check.key) && header)
	{
		memcpy(check.header, header, (size_t)key.header_size);
		check.valid = valid;
		check.corrupted = corrupted;
		check.made = 1;
	}
	check.checking = 0;
	pthread_cond_broadcast(&check_done);
	pthread_mutex_unlock(&check_lock);
	free(header);
	return NULL;
}

/* (the lock held) the check of the file at key begun, unless it is made or
running */
static void check_begin(const struct saved_game_key *key)
{
	pthread_t thread;
	pthread_attr_t attributes;

	if (key_same(key, &check.key) && (check.made || check.checking))
		return;
	/* (one at a time: a check of another file running, this one waits for
	it to end) */
	while (check.checking)
		pthread_cond_wait(&check_done, &check_lock);
	if (key_same(key, &check.key) && check.made)
		return;
	check.key = *key;
	check.made = 0;
	check.checking = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&thread, &attributes, check_thread, NULL) != 0)
	{
		/* (no thread: checked here) */
		int corrupted = 0;

		check.valid = file_check(key, check.header, &corrupted);
		check.corrupted = corrupted;
		check.made = 1;
		check.checking = 0;
	}
	pthread_attr_destroy(&attributes);
}

int saved_game_check(const char *xbox_path, void *header, long header_size, long checksum_offset,
	long buffer_size, int *corrupted)
{
	struct saved_game_key key;
	int valid;

	*corrupted = 0;
	if (header_size <= 0 || header_size > SAVED_GAME_HEADER_MAXIMUM || checksum_offset < 0 ||
		checksum_offset + (long)sizeof(uint32_t) > header_size || buffer_size < header_size)
	{
		return 0;
	}
	if (!key_of(xbox_path, header_size, checksum_offset, buffer_size, &key))
	{
		memset(header, 0, (size_t)header_size);
		return 0;
	}
	pthread_mutex_lock(&check_lock);
	check_begin(&key);
	while (check.checking && key_same(&key, &check.key))
		pthread_cond_wait(&check_done, &check_lock);
	memcpy(header, check.header, (size_t)header_size);
	valid = check.valid;
	*corrupted = check.corrupted;
	pthread_mutex_unlock(&check_lock);
	return valid;
}

void saved_game_check_ahead(const char *xbox_path, long header_size, long checksum_offset, long buffer_size)
{
	struct saved_game_key key;
	unsigned long long now = SDL_GetTicks();

	if (check.looked_at && now - check.looked_at < SAVED_GAME_LOOK_MILLISECONDS)
		return;
	check.looked_at = now;
	if (header_size <= 0 || header_size > SAVED_GAME_HEADER_MAXIMUM ||
		!key_of(xbox_path, header_size, checksum_offset, buffer_size, &key))
	{
		return;
	}
	pthread_mutex_lock(&check_lock);
	/* (never waited for here: a check running goes on) */
	if (!check.checking)
		check_begin(&key);
	pthread_mutex_unlock(&check_lock);
}
