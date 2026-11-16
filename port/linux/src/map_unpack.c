/*
MAP_UNPACK.C

A stock map unpacked into a cache slot (z:\cacheNNN.map, source/cache/
cache_files_windows.c) off the game's thread, on a few workers of its own,
so that the first start's menus' pictures' pass (port/linux/game/
menu_pictures_render.c) finds each map whole as it reaches it, instead of
waiting on the game's one decompressor (cache_files_decompress_windows.c,
the Xbox's, one map at a time) for each.

An Xbox map is its header (struct cache_file_header, 0x800 bytes) and one
zlib stream of the rest: the map's file_length less the header, inflated.
The slot written is the precache's, byte for byte: the header at 0, the
inflated map after it, and zeros to the slot's size (the map's length
rounded up to 64 KB); its pages put on the disk, then its header written
and put on the disk, and only then the slot's source file (xbox_files.c's
cache_slot_source_record, version 3), the mark of a whole copy. A slot cut
short anywhere before has no mark and is copied again, never played.

Each unpack has its own zlib stream and buffers: any number run at once,
each into its own slot. The game's thread chooses the slot and holds it
while a worker fills it (cache_files_unpack_begin), and takes it back
through the same check the slots get as the game starts
(cache_files_unpack_update).

Before the game starts, once the maps are copied out of a disc image
(xiso_extract_maps' callers: first_start.c, sdl_platform.c, the iOS app's
port/ios/data_import.m), map_unpack_folder fills the slots the same way,
on the same number of workers, with slots chosen by the cache's rules
without the game's tables (map_unpack_assign: 32 slots, the most a map of
each type may be, the budget of map_cache_choose_budget), and writes
z:\last_language.dat, so that the game's start adopts them
(cache_files_open_cache_files). One cut short is copied again then. The
iOS app's host builds this part alone (no HALO_LINUX_PLATFORM_LAYER), on
the system's zlib.
*/

#ifdef HALO_LINUX_PLATFORM_LAYER
#include "platform.h"
#include "loading_screen.h"
#include "../../third_party/zlib/zlib_prefixed.h"
#else
#include <zlib.h>
#define HALO_PORT_CACHED_MAP_FILES 32
#endif
#include "posix.h"
#include "map_unpack.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

/* (xbox_files.c's cache slots' sources) */
void cache_slot_source_record(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length);
int cache_slot_source_matches(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length);

/* ---------- constants */

enum
{
	MAP_UNPACK_HEADER_SIZE = 0x800,
	MAP_UNPACK_FILE_LENGTH_OFFSET = 0x8,
	MAP_UNPACK_NAME_OFFSET = 0x20,
	MAP_UNPACK_NAME_SIZE = 0x20,
	MAP_UNPACK_CHECKSUM_OFFSET = 0x64,
	MAP_UNPACK_READ_SIZE = 0x40000,
	MAP_UNPACK_WRITE_SIZE = 0x400000,
	/* (cache_files_windows.c's NUMBER_OF_CACHED_MAP_FILES) */
	MAP_UNPACK_SLOTS = HALO_PORT_CACHED_MAP_FILES,
	MAP_UNPACK_MOST_WORKERS = 4,
	MAP_UNPACK_TYPE_OFFSET = 0x60,
	MAP_UNPACK_VERSION = 5,
	/* (cache_files_windows.c's: the most a map of each type may be) */
	MAP_UNPACK_SOLO_MAXIMUM_SIZE = 0x11600000,
	MAP_UNPACK_MULTIPLAYER_MAXIMUM_SIZE = 0x02F00000,
	MAP_UNPACK_MAIN_MENU_MAXIMUM_SIZE = 0x02300000,
	MAP_UNPACK_SOURCE_VERSION = 3,
};

#define MAP_CACHE_MEGABYTE (1024ULL * 1024ULL)
/* the disk's room the cache never takes: a twentieth of the disk, but at
least 2 GB (a small disk's: a phone's, a 64 GB Steam Deck's) and at most
5 GB. The cache is the game's own to give back (a load gives a map up for
room), the system is not: what it needs free is a fixed amount, for its swap,
its temporary files and an update's download, not a share of the disk, and 5 GB
holds those with the game's own writes (its saves, its shader caches, a few
hundred MB). A twentieth alone kept 23.5 GB of a 471 GB disk free, more than
many have left, and so most of every stock map out of the cache. */
#define MAP_CACHE_FREE_MARGIN (2048ULL * MAP_CACHE_MEGABYTE)
#define MAP_CACHE_FREE_MARGIN_MOST (5120ULL * MAP_CACHE_MEGABYTE)
#define MAP_CACHE_FREE_MARGIN_SHARE 20
/* (cache_files_initialize's least: the Xbox's six cache files) */
#define MAP_CACHE_LEAST (0x11600000ULL * 2 + 0x02300000ULL + 0x02F00000ULL * 3)

enum map_unpack_state
{
	_map_unpack_free,
	_map_unpack_queued,
	_map_unpack_running,
	_map_unpack_done,
};

/* ---------- structures */

struct map_unpack_job
{
	enum map_unpack_state state;
	int succeeded;
	/* the game's own copy (the precache's: taken only by map_unpack_take),
	the copy asked to stop, how far it has come (0 to 1) */
	int precache;
	/* (its place in the queue: the workers take the jobs in the order they
	were queued, the order the pass reaches the maps in) */
	unsigned long order;
	volatile int stop;
	volatile float progress;
	long copy_size;
	char map_path[1024], slot_path[1024];
	/* (the map's path as the slot's source names it: cache_slot_source_record) */
	char map_xbox_path[256];
};

/* ---------- globals */

#ifdef HALO_LINUX_PLATFORM_LAYER

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake, finished;
	int workers;
	unsigned long queued;
	struct map_unpack_job jobs[MAP_UNPACK_SLOTS];
} map_unpack = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };
#endif

/* ---------- private code */

static unsigned long map_unpack_word(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

static int map_unpack_write_all(int descriptor, const void *buffer, size_t size, off_t offset)
{
	const unsigned char *bytes = buffer;

	while (size)
	{
		ssize_t written = pwrite(descriptor, bytes, size, offset);

		if (written <= 0)
			return 0;
		bytes += written;
		size -= (size_t)written;
		offset += written;
	}
	return 1;
}

/* ---------- public code */

int map_unpack_slot(const char *map_path, const char *slot_path, long copy_size, char *name, unsigned long *checksum,
	unsigned long *file_length, volatile int *stop, volatile float *progress)
{
	unsigned char header[MAP_UNPACK_HEADER_SIZE];
	unsigned char *in = NULL, *out = NULL;
	z_stream stream;
	int source = -1, slot = -1, inflating = 0, whole = 0;
	unsigned long length, body, done = 0;
	off_t read_offset = MAP_UNPACK_HEADER_SIZE;

	source = open(map_path, O_RDONLY | O_CLOEXEC);
	if (source < 0 || pread(source, header, sizeof(header), 0) != (ssize_t)sizeof(header))
		goto finished;
	length = map_unpack_word(header + MAP_UNPACK_FILE_LENGTH_OFFSET);
	if (length < MAP_UNPACK_HEADER_SIZE || (long)length > copy_size || !memchr(header + MAP_UNPACK_NAME_OFFSET, 0,
			MAP_UNPACK_NAME_SIZE))
	{
		goto finished;
	}
	body = length - MAP_UNPACK_HEADER_SIZE;
	in = malloc(MAP_UNPACK_READ_SIZE);
	out = malloc(MAP_UNPACK_WRITE_SIZE);
	memset(&stream, 0, sizeof(stream));
	if (!in || !out || inflateInit(&stream) != Z_OK)
		goto finished;
	inflating = 1;
	/* (emptied, then its size: zeros past the map, as the precache's
	cached_map_file_resize leaves them, and no header until the end) */
	slot = open(slot_path, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (slot < 0 || ftruncate(slot, 0) != 0 || ftruncate(slot, (off_t)copy_size) != 0)
		goto finished;
	while (done < body)
	{
		int result;

		if (stop && *stop)
			goto finished;
		if (progress)
			*progress = (float)done / (float)body;

		if (!stream.avail_in)
		{
			ssize_t count = pread(source, in, MAP_UNPACK_READ_SIZE, read_offset);

			if (count <= 0)
				goto finished;
			read_offset += count;
			stream.next_in = in;
			stream.avail_in = (uInt)count;
		}
		stream.next_out = out;
		stream.avail_out = (uInt)(body - done < MAP_UNPACK_WRITE_SIZE ? body - done : MAP_UNPACK_WRITE_SIZE);
		result = inflate(&stream, Z_NO_FLUSH);
		if (result != Z_OK && result != Z_STREAM_END)
			goto finished;
		{
			unsigned long produced = (unsigned long)(stream.next_out - out);

			if (produced && !map_unpack_write_all(slot, out, produced, (off_t)(MAP_UNPACK_HEADER_SIZE + done)))
				goto finished;
			done += produced;
		}
		/* (a stream that ends before the map's length is a bad file, as the
		precache has it; one that goes on past it is read no further) */
		if (result == Z_STREAM_END && done != body)
			goto finished;
	}
	/* (its pages on the disk, then its header, itself on the disk) */
	if (fsync(slot) != 0 || !map_unpack_write_all(slot, header, sizeof(header), 0) || fsync(slot) != 0)
		goto finished;
	if (name)
		memcpy(name, header + MAP_UNPACK_NAME_OFFSET, MAP_UNPACK_NAME_SIZE);
	if (checksum)
		*checksum = map_unpack_word(header + MAP_UNPACK_CHECKSUM_OFFSET);
	if (file_length)
		*file_length = length;
	whole = 1;

finished:
	if (inflating)
		inflateEnd(&stream);
	free(in);
	free(out);
	if (slot >= 0 && close(slot) != 0)
		whole = 0;
	if (source >= 0)
		close(source);
	return whole;
}

#ifdef HALO_LINUX_PLATFORM_LAYER
/* the job's slot unpacked, and only if it is whole, its source written:
the mark, last */
static int map_unpack_job_run(short slot, struct map_unpack_job *job)
{
	char name[MAP_UNPACK_NAME_SIZE];
	unsigned long checksum = 0, file_length = 0;

	if (!map_unpack_slot(job->map_path, job->slot_path, job->copy_size, name, &checksum, &file_length, &job->stop,
			&job->progress))
		return 0;
	cache_slot_source_record(slot, job->map_xbox_path, name, checksum, file_length);
	return cache_slot_source_matches(slot, job->map_xbox_path, name, checksum, file_length);
}

static void *map_unpack_worker(void *context)
{
	(void)context;
	pthread_mutex_lock(&map_unpack.lock);
	for (;;)
	{
		short slot = MAP_UNPACK_SLOTS, other;

		/* (the game's own copy first, as a load waits for it; else the job
		queued first) */
		for (other = 0; other < MAP_UNPACK_SLOTS; other++)
		{
			struct map_unpack_job const *job = &map_unpack.jobs[other];

			if (job->state != _map_unpack_queued)
				continue;
			if (slot == MAP_UNPACK_SLOTS || (job->precache && !map_unpack.jobs[slot].precache) ||
				(job->precache == map_unpack.jobs[slot].precache && job->order < map_unpack.jobs[slot].order))
				slot = other;
		}
		if (slot >= MAP_UNPACK_SLOTS)
		{
			pthread_cond_wait(&map_unpack.wake, &map_unpack.lock);
			continue;
		}
		map_unpack.jobs[slot].state = _map_unpack_running;
		pthread_mutex_unlock(&map_unpack.lock);
		{
			struct timespec began, ended;
			int succeeded;

			clock_gettime(CLOCK_MONOTONIC, &began);
			succeeded = map_unpack_job_run(slot, &map_unpack.jobs[slot]);
			clock_gettime(CLOCK_MONOTONIC, &ended);
#ifdef HALO_LINUX_PLATFORM_LAYER
			platform_log("map cache: %s unpacked%s into slot %d in %.0f ms (%ld MB)", map_unpack.jobs[slot].map_xbox_path,
				succeeded ? "" : " (not whole)", slot, (ended.tv_sec - began.tv_sec) * 1e3 +
				(ended.tv_nsec - began.tv_nsec) / 1e6, map_unpack.jobs[slot].copy_size >> 20);
#endif

			pthread_mutex_lock(&map_unpack.lock);
			map_unpack.jobs[slot].succeeded = succeeded;
		}
		map_unpack.jobs[slot].state = _map_unpack_done;
		pthread_cond_broadcast(&map_unpack.finished);
	}
	return NULL;
}

#endif

int map_unpack_worker_count(void)
{
	long cores = sysconf(_SC_NPROCESSORS_ONLN);
	long count = cores / 2;

	if (count < 1)
		count = 1;
	if (count > MAP_UNPACK_MOST_WORKERS)
		count = MAP_UNPACK_MOST_WORKERS;
	return (int)count;
}

#ifdef HALO_LINUX_PLATFORM_LAYER
/* (the lock held) the pool started, once */
static int map_unpack_start(void)
{
	int wanted = map_unpack_worker_count();

	while (map_unpack.workers < wanted)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, map_unpack_worker, NULL) != 0)
			break;
		pthread_detach(thread);
		map_unpack.workers++;
	}
	return map_unpack.workers > 0;
}

int map_unpack_queue(short slot, const char *map_xbox_path, const char *slot_xbox_path, long copy_size, int precache)
{
	struct map_unpack_job *job;
	int queued = 0;

	if (slot < 0 || slot >= MAP_UNPACK_SLOTS)
		return 0;
	pthread_mutex_lock(&map_unpack.lock);
	job = &map_unpack.jobs[slot];
	if (job->state == _map_unpack_free && map_unpack_start())
	{
		platform_translate_path(map_xbox_path, job->map_path, sizeof(job->map_path));
		platform_translate_path(slot_xbox_path, job->slot_path, sizeof(job->slot_path));
		snprintf(job->map_xbox_path, sizeof(job->map_xbox_path), "%s", map_xbox_path);
		job->copy_size = copy_size;
		job->succeeded = 0;
		job->precache = precache;
		job->order = map_unpack.queued++;
		job->stop = 0;
		job->progress = 0.0f;
		if (job->map_path[0] && job->slot_path[0])
		{
			job->state = _map_unpack_queued;
			pthread_cond_signal(&map_unpack.wake);
			queued = 1;
		}
	}
	pthread_mutex_unlock(&map_unpack.lock);
	return queued;
}

int map_unpack_take_finished(short *slot, int *succeeded)
{
	short index;
	int found = 0;

	pthread_mutex_lock(&map_unpack.lock);
	for (index = 0; index < MAP_UNPACK_SLOTS; index++)
	{
		if (map_unpack.jobs[index].state == _map_unpack_done && !map_unpack.jobs[index].precache)
		{
			*slot = index;
			*succeeded = map_unpack.jobs[index].succeeded;
			map_unpack.jobs[index].state = _map_unpack_free;
			found = 1;
			break;
		}
	}
	pthread_mutex_unlock(&map_unpack.lock);
	return found;
}

void map_unpack_wait(short slot)
{
	if (slot < 0 || slot >= MAP_UNPACK_SLOTS)
		return;
	pthread_mutex_lock(&map_unpack.lock);
	while (map_unpack.jobs[slot].state == _map_unpack_queued || map_unpack.jobs[slot].state == _map_unpack_running)
		pthread_cond_wait(&map_unpack.finished, &map_unpack.lock);
	pthread_mutex_unlock(&map_unpack.lock);
}

int map_unpack_status(short slot, float *progress)
{
	int status;

	if (slot < 0 || slot >= MAP_UNPACK_SLOTS)
		return -1;
	pthread_mutex_lock(&map_unpack.lock);
	if (progress)
		*progress = map_unpack.jobs[slot].progress;
	status = map_unpack.jobs[slot].state == _map_unpack_done ? (map_unpack.jobs[slot].succeeded ? 1 : -1) :
		map_unpack.jobs[slot].state == _map_unpack_free ? -1 : 0;
	pthread_mutex_unlock(&map_unpack.lock);
	return status;
}

void map_unpack_stop(short slot)
{
	if (slot >= 0 && slot < MAP_UNPACK_SLOTS)
		map_unpack.jobs[slot].stop = 1;
}

/* (the time a copy is waited for before the loading screen covers the wait:
a game started before its map's copy was done stopped with nothing on the
screen for 0.7 s, on an M1 Max, the stall detector's) */
#define MAP_UNPACK_SCREEN_AFTER_NS 100000000ll

int map_unpack_take(short slot)
{
	int succeeded = 0;
#ifdef HALO_LINUX_PLATFORM_LAYER
	struct timespec started, now;
	int shown = 0;
#endif

	if (slot < 0 || slot >= MAP_UNPACK_SLOTS)
		return 0;
#ifdef HALO_LINUX_PLATFORM_LAYER
	/* a copy not yet done: waited for under the loading screen, its
	progress the copy's, once the wait has gone on long enough to be seen */
	clock_gettime(CLOCK_MONOTONIC, &started);
	pthread_mutex_lock(&map_unpack.lock);
	while (map_unpack.jobs[slot].state == _map_unpack_queued || map_unpack.jobs[slot].state == _map_unpack_running)
	{
		struct timespec until;
		float progress = map_unpack.jobs[slot].progress;

		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((now.tv_sec - started.tv_sec) * 1000000000ll + (now.tv_nsec - started.tv_nsec) >= MAP_UNPACK_SCREEN_AFTER_NS)
		{
			pthread_mutex_unlock(&map_unpack.lock);
			if (!shown)
				halo_loading_screen_begin(TRUE);
			shown = 1;
			halo_loading_screen_progress(progress, NULL);
			pthread_mutex_lock(&map_unpack.lock);
		}
		/* (the condition's clock is the real time's) */
		clock_gettime(CLOCK_REALTIME, &until);
		until.tv_nsec += 30000000l;
		if (until.tv_nsec >= 1000000000l)
		{
			until.tv_sec++;
			until.tv_nsec -= 1000000000l;
		}
		pthread_cond_timedwait(&map_unpack.finished, &map_unpack.lock, &until);
	}
	pthread_mutex_unlock(&map_unpack.lock);
	if (shown)
	{
		platform_log("map cache: the game waited %.1f s for the copy of the map it starts",
			(double)((now.tv_sec - started.tv_sec) * 1000000000ll + (now.tv_nsec - started.tv_nsec)) / 1e9);
		halo_loading_screen_progress(1.0f, NULL);
		halo_loading_screen_end();
	}
#else
	map_unpack_wait(slot);
#endif
	pthread_mutex_lock(&map_unpack.lock);
	if (map_unpack.jobs[slot].state == _map_unpack_done)
	{
		succeeded = map_unpack.jobs[slot].succeeded;
		map_unpack.jobs[slot].state = _map_unpack_free;
	}
	pthread_mutex_unlock(&map_unpack.lock);
	return succeeded;
}
#endif

/* ---------- before the game starts */

/* the budget: megabytes game.map_cache_megabytes' (0: the disk's), cache
the bytes the cache takes now, free and total the disk's (0 unknown),
least the least it may be */
unsigned long long map_cache_choose_budget(long megabytes, unsigned long long cache, unsigned long long free,
	unsigned long long total, unsigned long long least)
{
	unsigned long long margin = total / MAP_CACHE_FREE_MARGIN_SHARE;
	unsigned long long budget;

	if (margin < MAP_CACHE_FREE_MARGIN)
		margin = MAP_CACHE_FREE_MARGIN;
	if (margin > MAP_CACHE_FREE_MARGIN_MOST)
		margin = MAP_CACHE_FREE_MARGIN_MOST;
	if (megabytes > 0)
		budget = (unsigned long long)megabytes * MAP_CACHE_MEGABYTE;
	else
		budget = cache + free > margin ? cache + free - margin : 0;
	return budget > least ? budget : least;
}

unsigned long long map_unpack_least_budget(void)
{
	return MAP_CACHE_LEAST;
}

/* the slots of maps (their lengths and types, in the order they are
wanted), as the cache would give them with every slot empty: the next slot
to each map a slot can hold (shorter than the most its type may be) while
the budget has room for it (its length rounded up to 64 KB), else none
(-1); how many have one */
int map_unpack_assign(const long *file_lengths, const short *types, int count, unsigned long long budget,
	short *slots)
{
	unsigned long long taken = 0;
	short next = 0;
	int index, assigned = 0;

	for (index = 0; index < count; index++)
	{
		long most = types[index] == 0 ? MAP_UNPACK_SOLO_MAXIMUM_SIZE :
			types[index] == 1 ? MAP_UNPACK_MULTIPLAYER_MAXIMUM_SIZE :
			types[index] == 2 ? MAP_UNPACK_MAIN_MENU_MAXIMUM_SIZE : 0;
		unsigned long long copy_size = ((unsigned long long)file_lengths[index] + 0xFFFF) & ~0xFFFFULL;

		slots[index] = -1;
		if (next >= MAP_UNPACK_SLOTS || file_lengths[index] <= MAP_UNPACK_HEADER_SIZE || file_lengths[index] >= most ||
			taken + copy_size > budget)
		{
			continue;
		}
		slots[index] = next++;
		taken += copy_size;
		assigned++;
	}
	return assigned;
}

/* the stock maps, the cache's most useful first (cache_files_windows.c's
cached_map_usefulness) */
static const char *const map_unpack_stock_maps[] =
{
	"ui", "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
	"bloodgulch", "hangemhigh", "chillout", "prisoner", "damnation", "sidewinder", "ratrace",
	"beavercreek", "longest", "wizard", "carousel", "boardingaction", "putput",
};

#define MAP_UNPACK_STOCK_MAPS ((int)(sizeof(map_unpack_stock_maps) / sizeof(map_unpack_stock_maps[0])))

struct map_unpack_folder_job
{
	char map_path[1024], slot_path[1024], source_path[1024];
	long copy_size, file_length;
	volatile int done, whole;
	volatile float progress;
};

static struct
{
	pthread_mutex_t lock;
	struct map_unpack_folder_job *jobs;
	int count, next;
} map_unpack_folder_pool = { PTHREAD_MUTEX_INITIALIZER };

/* a map file's header: its length and type, if it is an Xbox map whose
header names it */
static int map_unpack_read_header(const char *path, const char *name, long *file_length, short *type)
{
	unsigned char header[MAP_UNPACK_HEADER_SIZE];
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	int read_whole = descriptor >= 0 && pread(descriptor, header, sizeof(header), 0) == (ssize_t)sizeof(header);

	if (descriptor >= 0)
		close(descriptor);
	if (!read_whole || map_unpack_word(header) != 0x68656164UL || map_unpack_word(header + 0x7FC) != 0x666F6F74UL ||
		map_unpack_word(header + 4) != MAP_UNPACK_VERSION || !memchr(header + MAP_UNPACK_NAME_OFFSET, 0,
			MAP_UNPACK_NAME_SIZE) || strcasecmp((const char *)header + MAP_UNPACK_NAME_OFFSET, name) != 0)
	{
		return 0;
	}
	*file_length = (long)map_unpack_word(header + MAP_UNPACK_FILE_LENGTH_OFFSET);
	*type = (short)(header[MAP_UNPACK_TYPE_OFFSET] | header[MAP_UNPACK_TYPE_OFFSET + 1] << 8);
	return 1;
}

/* a whole slot's source (xbox_files.c's cache_slot_source_write, version
3: never played), on the disk */
static int map_unpack_source_write(const char *source_path, const char *map_path, const char *name,
	unsigned long checksum, unsigned long file_length)
{
	struct posix_file_information information;
	FILE *file;
	int written;

	if (posix_stat(map_path, &information) != 0)
		return 0;
	file = fopen(source_path, "wb");
	if (!file)
		return 0;
	written = fprintf(file, "openh1-cache-source %d %lu %lu %lu %lu %lu %lu %lu %s\n", MAP_UNPACK_SOURCE_VERSION,
		(unsigned long)information.size_low, (unsigned long)information.size_high,
		(unsigned long)information.modification_seconds, (unsigned long)information.modification_nanoseconds, 0UL,
		checksum, file_length, name) >= 0;
	if (fflush(file) != 0 || fsync(fileno(file)) != 0)
		written = 0;
	if (fclose(file) != 0)
		written = 0;
	if (!written)
		remove(source_path);
	return written;
}

static void *map_unpack_folder_worker(void *context)
{
	(void)context;
	for (;;)
	{
		struct map_unpack_folder_job *job;
		char name[MAP_UNPACK_NAME_SIZE];
		unsigned long checksum = 0, file_length = 0;

		pthread_mutex_lock(&map_unpack_folder_pool.lock);
		job = map_unpack_folder_pool.next < map_unpack_folder_pool.count ?
			&map_unpack_folder_pool.jobs[map_unpack_folder_pool.next++] : NULL;
		pthread_mutex_unlock(&map_unpack_folder_pool.lock);
		if (!job)
			return NULL;
		job->whole = map_unpack_slot(job->map_path, job->slot_path, job->copy_size, name, &checksum, &file_length,
				NULL, &job->progress) &&
			map_unpack_source_write(job->source_path, job->map_path, name, checksum, file_length);
		job->progress = 1.0f;
		job->done = 1;
	}
}

int map_unpack_folder(const char *maps_folder, const char *z_folder, long language, unsigned long long budget,
	void (*progress)(void *context, const char *file, unsigned long long done, unsigned long long total),
	void *context)
{
	struct map_unpack_folder_job jobs[MAP_UNPACK_STOCK_MAPS];
	long file_lengths[MAP_UNPACK_STOCK_MAPS];
	short types[MAP_UNPACK_STOCK_MAPS], slots[MAP_UNPACK_STOCK_MAPS];
	char map_paths[MAP_UNPACK_STOCK_MAPS][1024];
	pthread_t threads[MAP_UNPACK_MOST_WORKERS];
	int index, count = 0, threads_started = 0, whole = 0, finished = 0;
	unsigned long long total = 0;

	for (index = 0; index < MAP_UNPACK_STOCK_MAPS; index++)
	{
		snprintf(map_paths[index], sizeof(map_paths[index]), "%s/%s.map", maps_folder, map_unpack_stock_maps[index]);
		if (!map_unpack_read_header(map_paths[index], map_unpack_stock_maps[index], &file_lengths[index],
				&types[index]))
		{
			file_lengths[index] = 0;
			types[index] = -1;
		}
	}
	map_unpack_assign(file_lengths, types, MAP_UNPACK_STOCK_MAPS, budget, slots);
	posix_make_directory(z_folder);
	for (index = 0; index < MAP_UNPACK_STOCK_MAPS; index++)
	{
		struct map_unpack_folder_job *job = &jobs[count];

		if (slots[index] < 0)
			continue;
		memset(job, 0, sizeof(*job));
		snprintf(job->map_path, sizeof(job->map_path), "%s", map_paths[index]);
		snprintf(job->slot_path, sizeof(job->slot_path), "%s/cache%03d.map", z_folder, slots[index]);
		snprintf(job->source_path, sizeof(job->source_path), "%s/cache%03d.source", z_folder, slots[index]);
		job->file_length = file_lengths[index];
		job->copy_size = (file_lengths[index] + 0xFFFF) & ~0xFFFFL;
		/* (a slot's old mark gone first: it is no map until its new one) */
		remove(job->source_path);
		total += (unsigned long long)job->file_length;
		count++;
	}
	if (!count)
		return 0;
	map_unpack_folder_pool.jobs = jobs;
	map_unpack_folder_pool.count = count;
	map_unpack_folder_pool.next = 0;
	for (index = 0; index < map_unpack_worker_count() && index < count; index++)
	{
		if (pthread_create(&threads[threads_started], NULL, map_unpack_folder_worker, NULL) == 0)
			threads_started++;
	}
	if (!threads_started)
		map_unpack_folder_worker(NULL);
	while (finished < count)
	{
		unsigned long long done = 0;

		finished = 0;
		for (index = 0; index < count; index++)
		{
			done += (unsigned long long)((double)jobs[index].progress * (double)jobs[index].file_length);
			finished += jobs[index].done;
		}
		if (progress)
			progress(context, "Unpacking the maps", done, total);
		if (finished < count)
		{
			struct timespec wait = { 0, 100 * 1000 * 1000 };

			nanosleep(&wait, NULL);
		}
	}
	for (index = 0; index < threads_started; index++)
		pthread_join(threads[index], NULL);
	for (index = 0; index < count; index++)
		whole += jobs[index].whole;
	/* (the language the slots are of: the game's start empties every slot
	of another's, cache_files_verify_language) */
	if (whole)
	{
		char path[1024];
		int descriptor;
		unsigned char bytes[4] = { (unsigned char)language, (unsigned char)(language >> 8),
			(unsigned char)(language >> 16), (unsigned char)(language >> 24) };

		snprintf(path, sizeof(path), "%s/last_language.dat", z_folder);
		descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (descriptor < 0 || !map_unpack_write_all(descriptor, bytes, sizeof(bytes), 0) || fsync(descriptor) != 0)
			whole = 0;
		if (descriptor >= 0)
			close(descriptor);
	}
	return whole;
}

#ifdef HALO_LINUX_PLATFORM_LAYER
/* (map_cache.c's) */
unsigned long long map_cache_budget(unsigned long long cache_bytes, unsigned long long least);

int map_unpack_after_extraction(const char *destination,
	void (*progress)(void *context, const char *file, unsigned long long done, unsigned long long total),
	void *context)
{
	char maps_folder[1024], z_folder[1024];
	size_t length;
	int whole;

	snprintf(maps_folder, sizeof(maps_folder), "%s/maps", destination);
	platform_translate_path("z:\\", z_folder, sizeof(z_folder));
	length = strlen(z_folder);
	while (length > 1 && z_folder[length - 1] == '/')
		z_folder[--length] = 0;
	if (!z_folder[0])
		return 0;
	whole = map_unpack_folder(maps_folder, z_folder, (long)XGetLanguage(), map_cache_budget(0,
		map_unpack_least_budget()), progress, context);
	platform_log("map cache: %d stock maps unpacked into %s before the game starts", whole, z_folder);
	return whole;
}
#endif
