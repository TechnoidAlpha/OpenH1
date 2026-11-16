/*
MAP_CACHE_PREFILL.C

The stock maps decompressed into the cache files ahead, in the background
(port/linux/src/map_cache.c), so that no load waits for one to be
decompressed (most of a campaign level's second on a fast computer, more
on a slow one). The menus' pictures' pass at the first start copies every
map it draws, and the cache keeps them; this copies what is left: a map
whose picture the pass did not draw, a cache of a build before (whose six
cache files held six maps), a map given up for room since, a map file
replaced. The order: the main menu's map, the campaign level after the
one played (or the one the campaign continues at, z:\last_solo.txt, and
its next), then every other stock map, the campaign's in order, then the
multiplayer maps, the most played first (cache_files_windows.c's
cached_map_usefulness). A copy made ahead never gives up a map for its
room: it waits for room in the budget.

A copy is made as the game's precache makes one (cache_files_precache_map_begin
with no loading screen, its thread at normal priority, with buffers of its
own: the texture cache keeps its memory), one at a time, and only when the
game is not loading, not preparing the menus' pictures, not copying a map
of its own, and its map has played for a few seconds. The game's own copy
of another map ends it (game_precache_new_map, cache_files_give_time_to_precache)
or takes it over if it is the map wanted (the copy goes on, at the
loading's priority); a copy ended unfinished is not used (the cache file's
header is written last). A map whose copy fails is not tried again in the
run. Once nothing is left to copy, it looks again when another map loads.

It copies only while game.map_cache_megabytes is 0 (Settings' MAP STORAGE:
INSTANT LOADING, every stock map kept). With a budget set (DISK SPACE
SAVER, the Xbox's 730 MB) the player has chosen the disk over the loads: a
map is copied as it is played, as on the Xbox, and none ahead. As the
setting changes, the cache's budget is read again: a smaller one ends the
copy ahead and gives up maps now, the least useful first, never the open
map (cache_files_budget_update); back at INSTANT LOADING, every stock map
given up is copied again, as above.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "game/game.h"
#include "tag_files/tag_files.h"

#include <stdio.h>
#include <string.h>

/* (port/linux/src/port_config.c's) */
long config_integer(const char *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* (frames) a map plays this long before a copy is begun */
	PREFILL_SETTLE_FRAMES = 120,
	NUMBER_OF_CAMPAIGN_LEVELS = 10,
};

/* the stock maps: the main menu's, the campaign's in its order, then the
multiplayer maps, the most played first */
static char const *const prefill_maps[] =
{
	"levels\\ui\\ui",
	"levels\\a10\\a10", "levels\\a30\\a30", "levels\\a50\\a50", "levels\\b30\\b30", "levels\\b40\\b40",
	"levels\\c10\\c10", "levels\\c20\\c20", "levels\\c40\\c40", "levels\\d20\\d20", "levels\\d40\\d40",
	"levels\\test\\bloodgulch\\bloodgulch", "levels\\test\\hangemhigh\\hangemhigh",
	"levels\\test\\chillout\\chillout", "levels\\test\\prisoner\\prisoner", "levels\\test\\damnation\\damnation",
	"levels\\test\\sidewinder\\sidewinder", "levels\\test\\ratrace\\ratrace",
	"levels\\test\\beavercreek\\beavercreek", "levels\\test\\longest\\longest", "levels\\test\\wizard\\wizard",
	"levels\\test\\carousel\\carousel", "levels\\test\\boardingaction\\boardingaction",
	"levels\\test\\putput\\putput",
};

#define FIRST_CAMPAIGN_MAP 1
#define NUMBER_OF_PREFILL_MAPS ((long)(sizeof(prefill_maps) / sizeof(prefill_maps[0])))

/* ---------- globals */

static struct
{
	/* the map being copied ahead (of prefill_maps), or NONE */
	long copying;
	/* the maps whose copies failed */
	boolean failed[NUMBER_OF_PREFILL_MAPS];
	/* the map played, the frames since it was loaded */
	char loaded[64];
	long settled_frames;
	/* nothing was left to copy: looked for again once another map loads */
	boolean idle;
	/* game.map_cache_megabytes as last read (0: every stock map kept,
	copied ahead), at config_changes' count, once read */
	boolean setting_read;
	unsigned long setting_read_at;
	long megabytes;
} map_cache_prefill = { NONE };

/* ---------- private code */

/* (debug.load_trace) what, the map, and the cache's bytes, budget and maps */
static void prefill_trace(
	char const *phase,
	char const *map_name)
{
	unsigned long long bytes, budget;
	long maps;
	char detail[192];

	if (!halo_load_trace_enabled())
		return;
	cache_files_cache_usage(&bytes, &budget, &maps);
	snprintf(detail, sizeof(detail), "map=%s bytes=%llu budget=%llu maps=%ld", map_name, bytes, budget, maps);
	halo_load_trace(phase, detail);
}

/* the index in prefill_maps of a campaign level's name (a10), else NONE */
static long prefill_campaign_level(
	char const *name)
{
	long index;

	for (index = FIRST_CAMPAIGN_MAP; index < FIRST_CAMPAIGN_MAP + NUMBER_OF_CAMPAIGN_LEVELS; index++)
	{
		if (!_stricmp(tag_name_strip_path(prefill_maps[index]), tag_name_strip_path(name)))
			return index;
	}
	return NONE;
}

/* the level the campaign continues at (main_load_last_solo_map's), else NONE */
static long prefill_last_solo_level(
	void)
{
	char name[256];
	long count;
	FILE *file = fopen("z:\\last_solo.txt", "r");

	if (!file)
		return NONE;
	count = (long)fread(name, 1, sizeof(name) - 1, file);
	fclose(file);
	name[count > 0 ? count : 0] = 0;
	return prefill_campaign_level(name);
}

/* the map at index copied ahead, if it is not held, has room and has not
failed: whether its copy was begun */
static boolean prefill_try(
	long index)
{
	/* (nor one a worker is unpacking: cache_files_unpack_begin) */
	if (index == NONE || index >= NUMBER_OF_PREFILL_MAPS || map_cache_prefill.failed[index] ||
		cache_files_map_unpacking(prefill_maps[index]) || cache_files_map_prefill_size(prefill_maps[index]) <= 0)
	{
		return FALSE;
	}
	prefill_trace("map_cache.prefill.begin", prefill_maps[index]);
	if (!cache_files_precache_map_begin((char *)prefill_maps[index], FALSE))
	{
		map_cache_prefill.failed[index] = TRUE;
		return FALSE;
	}
	if (!cache_files_precache_in_progress())
		return FALSE;
	map_cache_prefill.copying = index;
	return TRUE;
}

/* the next map to copy ahead begun: FALSE if none is left */
static boolean prefill_next(
	void)
{
	long played = prefill_campaign_level(map_cache_prefill.loaded);
	long continued = prefill_last_solo_level();
	long last_level = FIRST_CAMPAIGN_MAP + NUMBER_OF_CAMPAIGN_LEVELS - 1;
	long index;

	if (prefill_try(0) ||
		(played != NONE && played < last_level && prefill_try(played + 1)) ||
		prefill_try(continued) ||
		(continued != NONE && continued < last_level && prefill_try(continued + 1)))
	{
		return TRUE;
	}
	for (index = 0; index < NUMBER_OF_PREFILL_MAPS; index++)
	{
		if (prefill_try(index))
			return TRUE;
	}
	return FALSE;
}

/* game.map_cache_megabytes followed (Settings' MAP STORAGE): as it changes,
the copy ahead ended (its cache file, unfinished, is emptied), the cache's
budget read again and maps given up now to fit it (cache_files_budget_update),
and every map looked for again, those that failed too */
static void prefill_follow_setting(
	void)
{
	unsigned long changes = config_changes();
	long megabytes;
	unsigned long long given_up;

	if (map_cache_prefill.setting_read && map_cache_prefill.setting_read_at == changes)
		return;
	megabytes = config_integer("game.map_cache_megabytes");
	map_cache_prefill.setting_read_at = changes;
	if (!map_cache_prefill.setting_read || megabytes == map_cache_prefill.megabytes)
	{
		map_cache_prefill.setting_read = TRUE;
		map_cache_prefill.megabytes = megabytes;
		return;
	}
	map_cache_prefill.megabytes = megabytes;
	if (map_cache_prefill.copying != NONE)
	{
		if (cache_files_precache_in_progress() &&
			cache_files_precache_is_copying_map(prefill_maps[map_cache_prefill.copying]))
		{
			cache_files_precache_map_queue_end();
			cache_files_precache_map_end();
		}
		map_cache_prefill.copying = NONE;
	}
	given_up = cache_files_budget_update();
	platform_log("map cache: game.map_cache_megabytes %ld: %llu MB given up", megabytes, given_up / (1024ULL * 1024ULL));
	prefill_trace("map_cache.budget_changed", map_cache_prefill.loaded);
	map_cache_prefill.idle = FALSE;
	csmemset(map_cache_prefill.failed, 0, sizeof(map_cache_prefill.failed));
}

/* ---------- public code */

void map_cache_prefill_update(
	void)
{
	char const *loaded = cache_file_loaded_map_name();

	/* (the slots the unpacking workers are done with taken back: port/linux/src/map_unpack.c) */
	cache_files_unpack_update();
	/* (none in a copy of the game running beside the one that keeps the
	cache: cache_slot_folder) */
	if (!cache_slot_folder_keeps_cache())
		return;
	prefill_follow_setting();
	/* (none ahead with a budget set: DISK SPACE SAVER) */
	if (map_cache_prefill.megabytes > 0)
		return;
	/* the copy made ahead, finished or failed, ended (or ended by the game,
	or taken over for its load) */
	if (map_cache_prefill.copying != NONE)
	{
		char const *map_name = prefill_maps[map_cache_prefill.copying];
		real progress;
		short status;

		if (!cache_files_precache_in_progress() || !cache_files_precache_is_copying_map(map_name))
		{
			map_cache_prefill.copying = NONE;
			return;
		}
		status = cache_files_precache_map_status(&progress);
		if (status == _cached_map_file_in_progress)
			return;
		if (status == _cached_map_file_failed)
			map_cache_prefill.failed[map_cache_prefill.copying] = TRUE;
		cache_files_precache_map_end();
		prefill_trace(status == _cached_map_file_success ? "map_cache.prefill.end" : "map_cache.prefill.failed",
			map_name);
		map_cache_prefill.copying = NONE;
		return;
	}
	if (strcmp(loaded ? loaded : "", map_cache_prefill.loaded))
	{
		csstrncpy(map_cache_prefill.loaded, loaded ? loaded : "", sizeof(map_cache_prefill.loaded) - 1);
		map_cache_prefill.settled_frames = 0;
		map_cache_prefill.idle = FALSE;
	}
	if (map_cache_prefill.settled_frames < PREFILL_SETTLE_FRAMES)
	{
		map_cache_prefill.settled_frames++;
		return;
	}
	if (map_cache_prefill.idle || cache_files_precache_in_progress() || menu_pictures_preparing() ||
		game_map_loading_in_progress(NULL))
	{
		return;
	}
	if (!prefill_next())
	{
		map_cache_prefill.idle = TRUE;
		prefill_trace("map_cache.prefill.idle", map_cache_prefill.loaded);
	}
}
