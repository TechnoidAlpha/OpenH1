/* The cache files of decompressed maps (source/cache/cache_files_windows.c,
port/linux/src/map_cache.c): the budget of the disk, the cache file a map
is copied to, the maps given up for room (the least useful first, never the
open map's nor the one being copied), a copy ahead that never gives one up,
and an unfinished copy that is never taken for a map. The cache files are
made up: a size each, a header, the time their map was played. */
#include "harness.h"
#include <strings.h>

typedef void *HANDLE;
typedef struct { unsigned long dwLowDateTime, dwHighDateTime; } FILETIME;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define INVALID_SET_FILE_POINTER ((unsigned long)-1)
#define FILE_BEGIN 0
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define _stricmp strcasecmp
#define match_vassert(file, line, condition, message) ((void)0)
#define error(priority, ...) ((void)0)
enum { _error_silent };
enum { _scenario_type_solo, _scenario_type_multiplayer, _scenario_type_main_menu };
#include "constants.inc"

#define MEGABYTE (1024L * 1024L)

struct cache_file_header { long file_length; char name[32]; short scenario_type; };
struct cached_map_file { HANDLE file; FILETIME last_modification_date; struct cache_file_header header; };
static struct
{
	struct cached_map_file cached_map_files[NUMBER_OF_CACHED_MAP_FILES];
	boolean copy_in_progress;
	short copying_to_map_file_index;
	short open_map_file_index;
} cache_file_globals;
static unsigned long long cached_map_files_budget;
/* (the slots a worker is unpacking into: none here) */
static boolean cached_map_file_unpacking[NUMBER_OF_CACHED_MAP_FILES];

/* the made-up cache files: their sizes, when their maps were played, and
the next size set (SetFilePointer, then SetEndOfFile) */
static long file_sizes[NUMBER_OF_CACHED_MAP_FILES];
static unsigned long played_times[NUMBER_OF_CACHED_MAP_FILES];
static long pending_size;

static struct cached_map_file *cached_map_file_get(short index) { return &cache_file_globals.cached_map_files[index]; }
static HANDLE cached_map_file_get_handle(short index) { return cached_map_file_get(index)->file; }
static short handle_index(HANDLE file) { return (short)((long)file - 1); }
static unsigned long SetFilePointer(HANDLE file, long size, long *high, int method)
{ (void)file; (void)high; (void)method; pending_size = size; return (unsigned long)size; }
static boolean SetEndOfFile(HANDLE file) { file_sizes[handle_index(file)] = pending_size; return TRUE; }
static long cached_map_file_get_size(short index)
{ return cached_map_file_get(index)->file == INVALID_HANDLE_VALUE ? 0 : file_sizes[index]; }
static unsigned long cache_slot_source_played_time(short slot) { return played_times[slot]; }
static void cache_slot_source_forget(short slot) { played_times[slot] = 0; }

/* the maps folder's maps: name, size, type */
struct folder_map { const char *name; long size; short type; };
static const struct folder_map folder[] =
{
	{ "ui", 32 * MEGABYTE, _scenario_type_main_menu },
	{ "a10", 262 * MEGABYTE, _scenario_type_solo }, { "a30", 203 * MEGABYTE, _scenario_type_solo },
	{ "a50", 238 * MEGABYTE, _scenario_type_solo }, { "b30", 214 * MEGABYTE, _scenario_type_solo },
	{ "b40", 266 * MEGABYTE, _scenario_type_solo }, { "c10", 240 * MEGABYTE, _scenario_type_solo },
	{ "c20", 135 * MEGABYTE, _scenario_type_solo }, { "c40", 244 * MEGABYTE, _scenario_type_solo },
	{ "d20", 195 * MEGABYTE, _scenario_type_solo }, { "d40", 261 * MEGABYTE, _scenario_type_solo },
	{ "bloodgulch", 42 * MEGABYTE, _scenario_type_multiplayer }, { "hangemhigh", 41 * MEGABYTE, _scenario_type_multiplayer },
	{ "chillout", 40 * MEGABYTE, _scenario_type_multiplayer }, { "prisoner", 41 * MEGABYTE, _scenario_type_multiplayer },
	{ "damnation", 45 * MEGABYTE, _scenario_type_multiplayer }, { "sidewinder", 45 * MEGABYTE, _scenario_type_multiplayer },
	{ "ratrace", 45 * MEGABYTE, _scenario_type_multiplayer }, { "beavercreek", 41 * MEGABYTE, _scenario_type_multiplayer },
	{ "longest", 40 * MEGABYTE, _scenario_type_multiplayer }, { "wizard", 38 * MEGABYTE, _scenario_type_multiplayer },
	{ "carousel", 40 * MEGABYTE, _scenario_type_multiplayer }, { "boardingaction", 43 * MEGABYTE, _scenario_type_multiplayer },
	{ "putput", 38 * MEGABYTE, _scenario_type_multiplayer },
	/* a modder's multiplayer map, too large for its type */
	{ "hugeass", 120 * MEGABYTE, _scenario_type_multiplayer },
};

static const struct folder_map *folder_find(const char *name)
{
	long index;

	for (index = 0; index < NUMBEROF(folder); index++)
	{
		if (!strcasecmp(folder[index].name, name))
			return &folder[index];
	}
	return NULL;
}

/* (cache_files_map_prefill_size's) */
static const char *tag_name_strip_path(const char *name)
{
	const char *slash = strrchr(name, '\\');

	return slash ? slash + 1 : name;
}
static boolean custom_edition_level_name(const char *name) { (void)name; return FALSE; }
static boolean cache_files_precache_map_loaded(const char *name)
{
	long index;

	for (index = 0; index < NUMBER_OF_CACHED_MAP_FILES; index++)
	{
		if (!strcasecmp(cached_map_file_get(index)->header.name, tag_name_strip_path(name)))
			return TRUE;
	}
	return FALSE;
}
static boolean cache_file_read_header_from_dvd(const char *name, struct cache_file_header *header)
{
	const struct folder_map *map = folder_find(name);

	if (!map)
		return FALSE;
	memset(header, 0, sizeof(*header));
	header->file_length = map->size;
	header->scenario_type = map->type;
	strcpy(header->name, map->name);
	return TRUE;
}

#include "under_test.inc"

static void reset(unsigned long long budget)
{
	short index;

	memset(&cache_file_globals, 0, sizeof(cache_file_globals));
	memset(file_sizes, 0, sizeof(file_sizes));
	memset(played_times, 0, sizeof(played_times));
	for (index = 0; index < NUMBER_OF_CACHED_MAP_FILES; index++)
		cache_file_globals.cached_map_files[index].file = (HANDLE)(long)(index + 1);
	cache_file_globals.open_map_file_index = NONE;
	cache_file_globals.copying_to_map_file_index = NONE;
	cached_map_files_budget = budget;
}

/* a map copied as cache_files_precache_map_begin and _end copy it: the
cache file chosen, room made, the file its map's size, the header written
last; the cache file */
static short copy(const char *name)
{
	const struct folder_map *map = folder_find(name);
	long copy_size = (map->size + 0xFFFF) & ~0xFFFF;
	short index = cached_map_files_find_free_map(map->size, map->type);

	CHECK(index != NONE, "no cache file for %s", name);
	CHECK(index != cache_file_globals.open_map_file_index, "%s copied over the open map", name);
	memset(&cached_map_file_get(index)->header, 0, sizeof(struct cache_file_header));
	cached_map_files_make_room(index, copy_size);
	CHECK(cached_map_file_resize(index, 0) && cached_map_file_resize(index, copy_size), "resize");
	cached_map_file_get(index)->header.file_length = map->size;
	cached_map_file_get(index)->header.scenario_type = map->type;
	strcpy(cached_map_file_get(index)->header.name, map->name);
	return index;
}

static short held(const char *name)
{
	short index;

	for (index = 0; index < NUMBER_OF_CACHED_MAP_FILES; index++)
	{
		if (!strcasecmp(cached_map_file_get(index)->header.name, name))
			return index;
	}
	return NONE;
}

static void check_within_budget(void)
{
	CHECK(cached_map_files_bytes(NONE) <= cached_map_files_budget, "%llu bytes over the budget's %llu",
		cached_map_files_bytes(NONE), cached_map_files_budget);
}

#define GIGABYTE (1024ULL * MEGABYTE)
#define LEAST (2ULL * SOLO_CACHE_FILE_MAXIMUM_SIZE + MAIN_MENU_CACHE_FILE_MAXIMUM_SIZE + \
	3ULL * MULTIPLAYER_CACHE_FILE_MAXIMUM_SIZE)

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";
	long index;

	CASE("budget")
	{
		/* the disk's room less a twentieth of the disk, at least 2 GB and at
		most 5 GB */
		CHECK(map_cache_choose_budget(0, 0, 100 * GIGABYTE, 1000 * GIGABYTE, LEAST) == 95 * GIGABYTE, "large disk");
		CHECK(map_cache_choose_budget(0, 0, 20 * GIGABYTE, 64 * GIGABYTE, LEAST) == 20 * GIGABYTE - 64 * GIGABYTE / 20,
			"a twentieth, between 2 and 5 GB");
		CHECK(map_cache_choose_budget(0, 3 * GIGABYTE, 10 * GIGABYTE, 40 * GIGABYTE, LEAST) == 11 * GIGABYTE,
			"the cache's own bytes count as room");
		CHECK(map_cache_choose_budget(0, 0, 5 * GIGABYTE, 20 * GIGABYTE, LEAST) == 3 * GIGABYTE, "small disk");
		CHECK(map_cache_choose_budget(0, 0, GIGABYTE, 20 * GIGABYTE, LEAST) == LEAST, "a full disk keeps the Xbox's");
		CHECK(map_cache_choose_budget(0, 0, 0, 0, LEAST) == LEAST, "unknown");
		CHECK(map_cache_choose_budget(4000, 0, 100 * GIGABYTE, 1000 * GIGABYTE, LEAST) == 4000ULL * MEGABYTE, "set");
		CHECK(map_cache_choose_budget(100, 0, 100 * GIGABYTE, 1000 * GIGABYTE, LEAST) == LEAST, "set too low");
	}
	else CASE("keeps_every_stock_map")
	{
		/* the menus' pictures' pass copies every stock map: all kept, in
		cache files of their maps' sizes */
		reset(3200ULL * MEGABYTE);
		for (index = 0; index < NUMBEROF(folder) - 1; index++)
			copy(folder[index].name);
		for (index = 0; index < NUMBEROF(folder) - 1; index++)
			CHECK(held(folder[index].name) != NONE, "%s given up", folder[index].name);
		check_within_budget();
		CHECK(cached_map_files_bytes(NONE) < 3100ULL * MEGABYTE, "the files are their maps' sizes");
	}
	else CASE("least_useful_first")
	{
		/* room for three campaign levels: those never played go first, the
		least useful of them (the campaign's later levels) first */
		reset(800ULL * MEGABYTE);
		copy("a10");
		copy("a30");
		copy("a50");
		check_within_budget();
		copy("b30");
		CHECK(held("a50") == NONE && held("a10") != NONE && held("a30") != NONE, "a50 should have gone");
		check_within_budget();
		played_times[held("a10")] = 100;
		played_times[held("b30")] = 200;
		copy("b40");
		CHECK(held("a30") == NONE && held("a10") != NONE && held("b30") != NONE, "a30 (never played) should have gone");
		check_within_budget();
		played_times[held("b40")] = 300;
		copy("c10");
		CHECK(held("a10") == NONE && held("b30") != NONE && held("b40") != NONE, "a10 (played longest ago) should have gone");
		check_within_budget();
	}
	else CASE("room_made")
	{
		/* a campaign level copied over the multiplayer maps: as many given
		up as its room needs, the least useful first */
		reset(600ULL * MEGABYTE);
		for (index = 11; index < 24; index++)
			copy(folder[index].name);
		check_within_budget();
		copy("b40");
		check_within_budget();
		CHECK(held("bloodgulch") != NONE && held("putput") == NONE, "the least useful given up first");
	}
	else CASE("open_map_kept")
	{
		/* the open map (never played, least useful) is not given up */
		reset(560ULL * MEGABYTE);
		cache_file_globals.open_map_file_index = copy("d40");
		copy("a10");
		CHECK(held("d40") != NONE, "the open map was given up");
		CHECK(cached_map_files_least_useful(NONE) == held("a10"), "the open map chosen to give up");
		cache_file_globals.open_map_file_index = held("a10");
		CHECK(cached_map_files_least_useful(NONE) == held("d40"), "d40 not chosen once it is not open");
	}
	else CASE("copy_in_progress_kept")
	{
		/* the cache file a copy is going to is neither chosen nor given up */
		reset(3200ULL * MEGABYTE);
		copy("ui");
		cache_file_globals.copy_in_progress = TRUE;
		cache_file_globals.copying_to_map_file_index = copy("a10");
		/* (as cache_files_precache_map_begin leaves it: its header written last) */
		memset(&cached_map_file_get(cache_file_globals.copying_to_map_file_index)->header, 0,
			sizeof(struct cache_file_header));
		for (index = 0; index < 4; index++)
		{
			short chosen = cached_map_files_find_free_map(40 * MEGABYTE, _scenario_type_multiplayer);

			CHECK(chosen != cache_file_globals.copying_to_map_file_index, "the copy's cache file chosen");
			CHECK(cached_map_files_least_useful(NONE) != cache_file_globals.copying_to_map_file_index,
				"the copy's cache file given up");
		}
	}
	else CASE("unfinished_copy")
	{
		/* a copy ended before its header was written is no map: its cache
		file is taken again, never given up as one, and never found */
		short unfinished;

		reset(3200ULL * MEGABYTE);
		copy("a10");
		unfinished = copy("b30");
		memset(&cached_map_file_get(unfinished)->header, 0, sizeof(struct cache_file_header));
		CHECK(cached_map_files_least_useful(NONE) == held("a10"), "an unfinished copy chosen as a map to give up");
		CHECK(!cache_files_precache_map_loaded("levels\\b30\\b30"), "an unfinished copy taken for b30");
		CHECK(copy("c10") == unfinished, "the unfinished copy's cache file not taken again");
	}
	else CASE("ahead_never_gives_up")
	{
		/* a copy ahead only where the budget has room for it */
		reset(560ULL * MEGABYTE);
		CHECK(cache_files_map_prefill_size("levels\\a10\\a10") > 0, "room for a10");
		copy("a10");
		CHECK(cache_files_map_prefill_size("levels\\a10\\a10") == 0, "a10 copied twice");
		CHECK(cache_files_map_prefill_size("levels\\d40\\d40") > 0, "room for d40");
		copy("d40");
		CHECK(cache_files_map_prefill_size("levels\\b40\\b40") == 0, "b40 ahead would give up a map");
		CHECK(cache_files_map_prefill_size("levels\\test\\bloodgulch\\bloodgulch") == 0,
			"bloodgulch ahead would pass the budget");
	}
	else CASE("storage_values")
	{
		/* Settings' MAP STORAGE (tools/port_settings.py's MAP_STORAGE):
		Instant Loading the disk's room, which holds every stock map;
		Disk Space Saver the Xbox's six cache files' room, however much the
		disk has */
		CHECK(map_cache_choose_budget(INSTANT_MEGABYTES, 3 * GIGABYTE, 100 * GIGABYTE, 1000 * GIGABYTE, LEAST) ==
			98 * GIGABYTE, "instant: the disk's room");
		/* (a 471 GB disk with 20.1 GB free and the 2.8 GB cache: the 5 GB
		margin leaves every stock map room, where a twentieth, 23.5 GB, left
		only the Xbox's) */
		CHECK(map_cache_choose_budget(INSTANT_MEGABYTES, 2841ULL * MEGABYTE, 20602ULL * MEGABYTE, 471482ULL * MEGABYTE,
			LEAST) == (2841ULL + 20602 - 5120) * MEGABYTE, "instant on a large disk with 20 GB free");
		CHECK(map_cache_choose_budget(INSTANT_MEGABYTES, 0, 10 * GIGABYTE, 64 * GIGABYTE, LEAST) >= 3100ULL * MEGABYTE,
			"instant: every stock map on a disk with 10 GB free");
		CHECK(map_cache_choose_budget(SAVER_MEGABYTES, 3 * GIGABYTE, 100 * GIGABYTE, 1000 * GIGABYTE, LEAST) == LEAST,
			"saver: the Xbox's room");
		CHECK(map_cache_choose_budget(SAVER_MEGABYTES, 0, 0, 0, LEAST) == LEAST, "saver, the disk unknown");
		CHECK(LEAST < 740ULL * MEGABYTE, "the Xbox's room is about 730 MB");
	}
	else CASE("shrink_on_switch")
	{
		/* every stock map kept (Instant Loading), four campaign levels
		played, d40 longest ago and open; then Disk Space Saver: maps given
		up now to fit, those never played first, then those played longest
		ago, never the open map; the bytes given up reported */
		unsigned long long before, given_up;

		reset(3200ULL * MEGABYTE);
		for (index = 0; index < NUMBEROF(folder) - 1; index++)
			copy(folder[index].name);
		played_times[held("d40")] = 1;
		played_times[held("a10")] = 5;
		played_times[held("b40")] = 6;
		played_times[held("c40")] = 7;
		cache_file_globals.open_map_file_index = held("d40");
		before = cached_map_files_bytes(NONE);
		cached_map_files_budget = LEAST;
		given_up = cached_map_files_fit_budget();
		check_within_budget();
		CHECK(given_up == before - cached_map_files_bytes(NONE), "%llu bytes reported, %llu given up", given_up,
			before - cached_map_files_bytes(NONE));
		CHECK(given_up > 2 * GIGABYTE, "only %llu bytes given up", given_up);
		CHECK(held("d40") != NONE, "the open map given up");
		CHECK(held("c40") != NONE, "the map played last given up");
		CHECK(held("a10") == NONE && held("ui") == NONE, "a map never played, or played longer ago, kept");
		/* (already within it: nothing more) */
		CHECK(cached_map_files_fit_budget() == 0, "maps given up within the budget");
	}
	else CASE("rebuild_on_switch_back")
	{
		/* after Disk Space Saver, Instant Loading: each map given up has
		room to be copied again ahead (cache_files_map_prefill_size), and all
		of them fit with the maps kept */
		reset(3200ULL * MEGABYTE);
		for (index = 0; index < NUMBEROF(folder) - 1; index++)
			copy(folder[index].name);
		played_times[held("b30")] = 9;
		cached_map_files_budget = LEAST;
		cached_map_files_fit_budget();
		CHECK(held("d40") == NONE && cache_files_map_prefill_size("levels\\d40\\d40") == 0,
			"d40 ahead past Disk Space Saver's room");
		cached_map_files_budget = 3200ULL * MEGABYTE;
		for (index = 0; index < NUMBEROF(folder) - 1; index++)
		{
			char path[64];

			snprintf(path, sizeof(path), "levels\\%s\\%s", folder[index].name, folder[index].name);
			if (held(folder[index].name) == NONE)
			{
				CHECK(cache_files_map_prefill_size(path) > 0, "no room to copy %s again", folder[index].name);
				copy(folder[index].name);
			}
		}
		for (index = 0; index < NUMBEROF(folder) - 1; index++)
			CHECK(held(folder[index].name) != NONE, "%s not copied again", folder[index].name);
		check_within_budget();
	}
	else CASE("type_limits")
	{
		reset(3200ULL * MEGABYTE);
		CHECK(cached_map_files_find_free_map(120 * MEGABYTE, _scenario_type_multiplayer) == NONE,
			"a multiplayer map past the Xbox's multiplayer cache file");
		CHECK(cached_map_files_find_free_map(266 * MEGABYTE, _scenario_type_solo) != NONE, "b40");
		CHECK(cached_map_files_find_free_map(32 * MEGABYTE, 7) == NONE, "a type of no cache file");
	}
	else return 2;
	return 0;
}
