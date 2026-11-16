/* A stock map unpacked into a cache slot off the game's thread
(port/linux/src/map_unpack.c): the slot is the precache's, byte for byte
(cache_files_decompress_windows.c: the map's header at 0, its zlib stream
inflated after it, zeros to the map's length rounded up to 64 KB), its
source marked only once the whole slot is written, a map whose stream is
cut short or ends early leaves no mark, and two unpacks run at once, each
into its own slot, by hand and on the workers. The map is made up (the
test's map.inc, from zlib's deflate); the files are real, in a folder of
their own. */
#include "harness.h"
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#define Z_PREFIX
#include "zlib_all.inc"

#define HALO_PORT_CACHED_MAP_FILES 32
#define HALO_LINUX_PLATFORM_LAYER 1
#include "map.inc"

typedef unsigned int posix_ulong;
struct posix_file_information { posix_ulong size_low, size_high, modification_seconds, modification_nanoseconds; };
static int posix_stat(const char *path, struct posix_file_information *information)
{
	struct stat status;

	if (stat(path, &status) != 0)
		return -1;
	information->size_low = (posix_ulong)status.st_size;
	information->size_high = 0;
#ifdef __APPLE__
	information->modification_seconds = (posix_ulong)status.st_mtimespec.tv_sec;
	information->modification_nanoseconds = (posix_ulong)status.st_mtimespec.tv_nsec;
#else
	information->modification_seconds = (posix_ulong)status.st_mtim.tv_sec;
	information->modification_nanoseconds = (posix_ulong)status.st_mtim.tv_nsec;
#endif
	return 0;
}

static char folder[256];
static void platform_log(const char *format, ...) { (void)format; }
/* (the loading screen over a wait for a copy: none here) */
static void halo_loading_screen_begin(int follow_progress) { (void)follow_progress; }
static void halo_loading_screen_progress(float progress, const char *caption) { (void)progress; (void)caption; }
static void halo_loading_screen_end(void) {}
static unsigned long XGetLanguage(void) { return 1; }
static int posix_make_directory(const char *path) { return mkdir(path, 0755); }
unsigned long long map_cache_budget(unsigned long long cache_bytes, unsigned long long least)
{
	return cache_bytes + least;
}
static pthread_mutex_t marks_lock = PTHREAD_MUTEX_INITIALIZER;
static int marks, marks_whole;
static char marked_name[32];
static unsigned long marked_checksum, marked_length;

static void platform_translate_path(const char *xbox_path, char *path, unsigned long size)
{
	snprintf(path, size, "%s/%s", folder, xbox_path + 3);
}

static unsigned char *slot_bytes(const char *path, long *size)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bytes;

	*size = 0;
	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	*size = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc(*size ? (size_t)*size : 1);
	if (fread(bytes, 1, (size_t)*size, file) != (size_t)*size)
		*size = -1;
	fclose(file);
	return bytes;
}

/* what the precache writes: the header, the map inflated, zeros to its size */
static int slot_is_precache_s(const char *path)
{
	long size, index;
	unsigned char *bytes = slot_bytes(path, &size);
	int same = bytes && size == MAP_COPY_SIZE && !memcmp(bytes, map_file, 0x800) &&
		!memcmp(bytes + 0x800, map_body, sizeof(map_body));

	for (index = 0x800 + (long)sizeof(map_body); same && index < size; index++)
		same = bytes[index] == 0;
	free(bytes);
	return same;
}

/* (xbox_files.c's) the mark: the slot must be whole when it is made */
void cache_slot_source_record(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length)
{
	char xbox_path[64], path[512];

	(void)map_path;
	snprintf(xbox_path, sizeof(xbox_path), "z:\\cache%03d.map", slot);
	platform_translate_path(xbox_path, path, sizeof(path));
	pthread_mutex_lock(&marks_lock);
	marks++;
	marks_whole += slot_is_precache_s(path);
	snprintf(marked_name, sizeof(marked_name), "%s", name);
	marked_checksum = checksum;
	marked_length = file_length;
	pthread_mutex_unlock(&marks_lock);
}

int cache_slot_source_matches(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length)
{
	(void)slot;
	(void)map_path;
	return !strcmp(name, marked_name) && checksum == marked_checksum && file_length == marked_length;
}

#include "under_test.inc"

static void write_map(const char *name, const unsigned char *bytes, long size)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	fwrite(bytes, 1, (size_t)size, file);
	fclose(file);
}

static const char *host(const char *name)
{
	static char paths[4][512];
	static int next;
	char *path = paths[next++ & 3];

	snprintf(path, 512, "%s/%s", folder, name);
	return path;
}

struct unpack_thread
{
	const char *map, *slot;
	int whole;
};

static void *unpack_thread(void *context)
{
	struct unpack_thread *unpack = context;

	unpack->whole = map_unpack_slot(unpack->map, unpack->slot, MAP_COPY_SIZE, NULL, NULL, NULL, NULL, NULL);
	return NULL;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	char template[] = "/tmp/map-unpack-XXXXXX";

	snprintf(folder, sizeof(folder), "%s", mkdtemp(template));
	write_map("a10.map", map_file, sizeof(map_file));
	CASE("precache_slot")
	{
		char name[32];
		unsigned long checksum = 0, length = 0;

		CHECK(map_unpack_slot(host("a10.map"), host("cache000.map"), MAP_COPY_SIZE, name, &checksum, &length, NULL, NULL),
			"a whole map not unpacked");
		CHECK(slot_is_precache_s(host("cache000.map")), "the slot is not the precache's");
		CHECK(!strcmp(name, "a10") && checksum == MAP_CHECKSUM && length == MAP_LENGTH,
			"the header's name, checksum or length not given (%s %lx %lu)", name, checksum, length);
	}
	else CASE("marker_last")
	{
		struct map_unpack_job job;

		memset(&job, 0, sizeof(job));
		snprintf(job.map_path, sizeof(job.map_path), "%s", host("a10.map"));
		snprintf(job.slot_path, sizeof(job.slot_path), "%s", host("cache003.map"));
		snprintf(job.map_xbox_path, sizeof(job.map_xbox_path), "z:\\a10.map");
		job.copy_size = MAP_COPY_SIZE;
		CHECK(map_unpack_job_run(3, &job), "a whole map not unpacked");
		CHECK(marks == 1, "the slot marked %d times", marks);
		CHECK(marks_whole == 1, "the slot marked before it was whole");
		CHECK(!strcmp(marked_name, "a10") && marked_checksum == MAP_CHECKSUM && marked_length == MAP_LENGTH,
			"the mark names another map");
	}
	else CASE("torn_stream")
	{
		struct map_unpack_job job;

		/* the map file cut in its stream (a copy from the disc cut short) */
		write_map("cut.map", map_file, (long)sizeof(map_file) / 2);
		memset(&job, 0, sizeof(job));
		snprintf(job.map_path, sizeof(job.map_path), "%s", host("cut.map"));
		snprintf(job.slot_path, sizeof(job.slot_path), "%s", host("cache001.map"));
		snprintf(job.map_xbox_path, sizeof(job.map_xbox_path), "z:\\cut.map");
		job.copy_size = MAP_COPY_SIZE;
		CHECK(!map_unpack_job_run(1, &job), "a cut map unpacked");
		CHECK(marks == 0, "a cut map's slot marked");
	}
	else CASE("short_stream")
	{
		unsigned char *longer = malloc(sizeof(map_file));
		unsigned long length = MAP_LENGTH + 0x1000;

		/* a header that gives the map more than its stream holds */
		memcpy(longer, map_file, sizeof(map_file));
		memcpy(longer + 8, &length, 4);
		write_map("short.map", longer, sizeof(map_file));
		CHECK(!map_unpack_slot(host("short.map"), host("cache002.map"), MAP_COPY_SIZE, NULL, NULL, NULL, NULL, NULL),
			"a map whose stream ends early unpacked");
		free(longer);
	}
	else CASE("two_threads")
	{
		struct unpack_thread unpacks[2] = { { host("a10.map"), host("cache004.map") },
			{ host("a10.map"), host("cache005.map") } };
		pthread_t threads[2];
		int index;

		for (index = 0; index < 2; index++)
			pthread_create(&threads[index], NULL, unpack_thread, &unpacks[index]);
		for (index = 0; index < 2; index++)
			pthread_join(threads[index], NULL);
		CHECK(unpacks[0].whole && unpacks[1].whole, "two at once not both unpacked");
		CHECK(slot_is_precache_s(host("cache004.map")) && slot_is_precache_s(host("cache005.map")),
			"two at once: a slot is not the precache's");
	}
	else CASE("workers")
	{
		short slot;
		int succeeded, done = 0, whole = 0;

		CHECK(map_unpack_worker_count() >= 1 && map_unpack_worker_count() <= 4, "%d workers",
			map_unpack_worker_count());
		CHECK(map_unpack_queue(6, "z:\\a10.map", "z:\\cache006.map", MAP_COPY_SIZE, 0) &&
			map_unpack_queue(7, "z:\\a10.map", "z:\\cache007.map", MAP_COPY_SIZE, 0), "not queued");
		CHECK(!map_unpack_queue(6, "z:\\a10.map", "z:\\cache006.map", MAP_COPY_SIZE, 0), "a busy slot queued again");
		map_unpack_wait(6);
		map_unpack_wait(7);
		while (map_unpack_take_finished(&slot, &succeeded))
		{
			done++;
			whole += succeeded && slot_is_precache_s(host(slot == 6 ? "cache006.map" : "cache007.map"));
		}
		CHECK(done == 2 && whole == 2, "%d finished, %d whole", done, whole);
		CHECK(marks == 2 && marks_whole == 2, "%d marks, %d of whole slots", marks, marks_whole);
	}
	else CASE("assign")
	{
		/* too big a multiplayer map, a campaign level, the main menu, a
		multiplayer map, then one past the budget; and 40 small maps for 32
		slots */
		long lengths[5] = { 0x03000000, 0x10000000, 0x01000000, 0x00800000, 0x00800000 };
		short types[5] = { 1, 0, 2, 1, 1 }, slots[40];
		long many[40];
		short many_types[40];
		int index;

		CHECK(map_unpack_assign(lengths, types, 5, 0x11800000ULL, slots) == 3, "not 3 assigned");
		CHECK(slots[0] == -1 && slots[1] == 0 && slots[2] == 1 && slots[3] == 2 && slots[4] == -1,
			"slots %d %d %d %d %d", slots[0], slots[1], slots[2], slots[3], slots[4]);
		for (index = 0; index < 40; index++)
		{
			many[index] = 0x10000;
			many_types[index] = 1;
		}
		CHECK(map_unpack_assign(many, many_types, 40, ~0ULL, slots) == 32 && slots[31] == 31 && slots[32] == -1,
			"more than 32 slots");
	}
	else CASE("folder")
	{
		char path[512], line[256];
		unsigned char language[4] = { 0 };
		FILE *file;

		snprintf(path, sizeof(path), "%s/maps", folder);
		mkdir(path, 0755);
		write_map("maps/a10.map", map_file, sizeof(map_file));
		CHECK(map_unpack_folder(host("maps"), host("z"), 1, ~0ULL, NULL, NULL) == 1, "the folder not unpacked");
		CHECK(slot_is_precache_s(host("z/cache000.map")), "the folder's slot is not the precache's");
		file = fopen(host("z/cache000.source"), "r");
		CHECK(file && fgets(line, sizeof(line), file), "no mark");
		fclose(file);
		CHECK(!strncmp(line, "openh1-cache-source 3 ", 22) && strstr(line, " 0 1592594996 526336 a10\n"),
			"the mark is not version 3's: %s", line);
		file = fopen(host("z/last_language.dat"), "rb");
		CHECK(file && fread(language, 1, 4, file) == 4 && language[0] == 1 && !language[1], "no language");
		fclose(file);
	}
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	return 0;
}
