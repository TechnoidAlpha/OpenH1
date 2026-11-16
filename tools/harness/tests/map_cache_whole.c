/* A cache slot is played only once it is a whole copy of its map
(port/linux/src/xbox_files.c's cache slots' sources,
source/cache/cache_files_windows.c's cache_files_precache_map_mark): the
slot's source file is written only after a copy that wrote every byte and
put the slot on the disk, and names the map, checksum and length its header
has. A copy that failed with its header written, a source file of the
versions before, one left by another map, a torn one, and a replaced map
file are each no map. The files are real, in a folder of their own; the
copy's end and the cache file's header are made up. */
#include "harness.h"
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

typedef int BOOL;
typedef void *HANDLE;
typedef unsigned int posix_ulong;
typedef struct { unsigned long dwLowDateTime, dwHighDateTime; } FILETIME;
#define _stricmp strcasecmp
#define error(priority, ...) ((void)0)
enum { _error_silent };
#include <strings.h>
#include "constants.inc"

struct posix_file_information { posix_ulong size_low, size_high, modification_seconds, modification_nanoseconds; };
struct cache_file_header { long file_length; unsigned long checksum; char name[32]; };
struct cached_map_file { HANDLE file; FILETIME last_modification_date; struct cache_file_header header; };

static char folder[256];
static struct cached_map_file slot;
static long slot_size;
static boolean copy_written;
static struct { char copying_to_map_file_name[32]; } cache_file_globals;

static void platform_translate_path(const char *xbox_path, char *path, unsigned long size)
{
	snprintf(path, size, "%s/%s", folder, xbox_path + 3);
}
static int posix_stat(const char *path, struct posix_file_information *information)
{
	struct stat status;

	if (stat(path, &status) != 0)
		return -1;
	information->size_low = (posix_ulong)status.st_size;
	information->size_high = 0;
	information->modification_seconds = (posix_ulong)status.st_mtime;
	information->modification_nanoseconds = 0;
	return 0;
}
static const char *cache_slot_folder(void) { return "z:\\"; }
static struct cached_map_file *cached_map_file_get(short index);
void cache_slot_source_record(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length);
int cache_slot_source_matches(short slot, const char *map_path, const char *name, unsigned long checksum,
	unsigned long file_length);
/* the worker's copy (map_unpack.c): ended whole, it marks the header's map */
static int map_unpack_take(short index)
{
	if (copy_written)
		cache_slot_source_record(index, "z:\\a10.map", slot.header.name, slot.header.checksum,
			(unsigned long)slot.header.file_length);
	return copy_written;
}
/* the start's check: the slot's source marks its header's map */
static boolean cached_map_file_adopt(short index)
{
	return cache_slot_source_matches(index, "z:\\a10.map", slot.header.name, slot.header.checksum,
		(unsigned long)slot.header.file_length);
}
static struct cached_map_file *cached_map_file_get(short index) { (void)index; return &slot; }
static long cached_map_file_get_size(short index) { (void)index; return slot_size; }
static boolean cached_map_file_resize(short index, long size) { (void)index; slot_size = size; return TRUE; }
static void cache_file_get_map_path(const char *name, char *path) { sprintf(path, "z:\\%s.map", name); }

#include "under_test.inc"

static void write_text(const char *name, const char *text)
{
	char path[512];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	fputs(text, file);
	fclose(file);
}

/* a10.map in the maps folder, its copy in slot 0 ended (written or not),
the slot's source loaded back as the next start loads it */
static void copied(const char *name, unsigned long checksum, boolean written)
{
	write_text("a10.map", "the map");
	memset(&slot, 0, sizeof(slot));
	snprintf(slot.header.name, sizeof(slot.header.name), "%s", name);
	slot.header.checksum = checksum;
	slot.header.file_length = 1000;
	slot_size = 1000;
	snprintf(cache_file_globals.copying_to_map_file_name, 32, "a10");
	copy_written = written;
	cache_files_precache_map_mark(0);
	cache_slot_source_load(0);
}

static boolean playable(void)
{
	return cache_slot_source_matches(0, "z:\\a10.map", "a10", 0x1234, 1000);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	char template[] = "/tmp/map-cache-whole-XXXXXX";

	snprintf(folder, sizeof(folder), "%s", mkdtemp(template));
	CASE("whole_copy")
	{
		copied("a10", 0x1234, TRUE);
		CHECK(playable(), "a whole copy not played");
		CHECK(slot_size == 1000 && slot.header.name[0], "a whole copy emptied");
		cache_slot_source_played(0);
		cache_slot_source_load(0);
		CHECK(playable(), "a whole copy not played once played");
	}
	else CASE("failed_copy")
	{
		/* its header written all the same (a last write that failed) */
		copied("a10", 0x1234, FALSE);
		CHECK(!playable(), "a failed copy played");
		CHECK(slot_size == 0 && !slot.header.name[0], "a failed copy kept");
	}
	else CASE("version_before")
	{
		copied("a10", 0x1234, TRUE);
		write_text("cache000.source", "openh1-cache-source 2 7 0 1 0 0\n");
		cache_slot_source_load(0);
		CHECK(!playable(), "a slot of a version before played");
	}
	else CASE("torn_source")
	{
		copied("a10", 0x1234, TRUE);
		write_text("cache000.source", "openh1-cache-source 3 7 0 1 0 0 4660 10");
		cache_slot_source_load(0);
		CHECK(!playable(), "a torn source file played");
	}
	else CASE("another_map")
	{
		/* the source of a10's whole copy left beside a slot whose header
		now names another map, or another checksum or length */
		copied("a10", 0x1234, TRUE);
		CHECK(!cache_slot_source_matches(0, "z:\\a10.map", "b30", 0x1234, 1000), "another map's slot played");
		CHECK(!cache_slot_source_matches(0, "z:\\a10.map", "a10", 0x4321, 1000), "another checksum played");
		CHECK(!cache_slot_source_matches(0, "z:\\a10.map", "a10", 0x1234, 999), "another length played");
	}
	else CASE("copy_of_another")
	{
		/* a copy that ended with another map's header */
		copied("b30", 0x1234, TRUE);
		CHECK(!cache_slot_source_matches(0, "z:\\a10.map", "b30", 0x1234, 1000), "another map's copy marked");
		CHECK(slot_size == 0, "another map's copy kept");
	}
	else CASE("map_replaced")
	{
		char path[512];
		struct stat before;
		struct timeval times[2];

		copied("a10", 0x1234, TRUE);
		snprintf(path, sizeof(path), "%s/a10.map", folder);
		stat(path, &before);
		write_text("a10.map", "another map, longer");
		/* (its time kept: the size alone tells it apart, whatever second the
		two writes fell in) */
		times[0].tv_sec = times[1].tv_sec = before.st_mtime;
		times[0].tv_usec = times[1].tv_usec = 0;
		utimes(path, times);
		CHECK(!playable(), "a replaced map file's copy played");
	}
	else return 2;
	return 0;
}
