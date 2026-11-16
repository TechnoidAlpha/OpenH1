/* The stock maps copied into the cache ahead (port/linux/game/map_cache_prefill.c),
as Settings' MAP STORAGE (game.map_cache_megabytes) changes: every stock map
copied at 0 (INSTANT LOADING), none ahead with a budget set (DISK SPACE
SAVER), the copy ahead ended and the budget read again (maps given up) as
it changes, and every map copied again on the way back. The cache and the
config are made up: a map is held or not, a copy ends whole at once. */
#include "harness.h"
#include <stdarg.h>
#include <strings.h>

#define _stricmp strcasecmp
#define csstrncpy strncpy
#define csmemset memset
enum { _cached_map_file_in_progress, _cached_map_file_success, _cached_map_file_failed };

#define STOCK_MAPS 24

/* the made-up config: game.map_cache_megabytes, and config_changes' count */
static long setting_megabytes;
static unsigned long setting_changes;
static long config_integer(const char *name) { (void)name; return setting_megabytes; }
static unsigned long config_changes(void) { return setting_changes; }
static void platform_log(char const *format, ...) { (void)format; }
static void set_megabytes(long megabytes) { setting_megabytes = megabytes; setting_changes++; }

/* the made-up cache: the stock maps held (by name), the copy going on, the
budget's reads, the copies begun and ended unfinished, a map whose next copy fails */
static char held[STOCK_MAPS][64];
static char copying[64];
static boolean in_progress, copy_stopped;
static int budget_updates, copies_begun, copies_ended_unfinished;
static char failing[64];
/* what the budget read again gives up: every map but the main menu's */
static boolean budget_gives_up = TRUE;

static char const *tag_name_strip_path(char const *name)
{
	const char *slash = strrchr(name, '\\');

	return slash ? slash + 1 : name;
}

/* (by its name, a10, or its path, levels\a10\a10) */
static int held_index(const char *name)
{
	int index;

	for (index = 0; index < STOCK_MAPS; index++)
	{
		if (!strcmp(held[index], tag_name_strip_path(name)))
			return index;
	}
	return NONE;
}
static int held_count(void)
{
	int index, count = 0;

	for (index = 0; index < STOCK_MAPS; index++)
		count += held[index][0] != 0;
	return count;
}
static void hold(const char *name)
{
	int index;

	if (held_index(name) != NONE)
		return;
	for (index = 0; index < STOCK_MAPS; index++)
	{
		if (!held[index][0])
		{
			strcpy(held[index], name);
			return;
		}
	}
}

static int halo_load_trace_enabled(void) { return 0; }
static void halo_load_trace(char const *phase, char const *detail) { (void)phase; (void)detail; }
static void cache_files_cache_usage(unsigned long long *bytes, unsigned long long *budget, long *maps)
{ *bytes = 0; *budget = 0; *maps = held_count(); }
static void cache_files_unpack_update(void) {}
static boolean cache_slot_folder_keeps_cache(void) { return TRUE; }
static char const *cache_file_loaded_map_name(void) { return "levels\\ui\\ui"; }
static int menu_pictures_preparing(void) { return 0; }
static boolean game_map_loading_in_progress(real *progress) { (void)progress; return FALSE; }
static boolean cache_files_map_unpacking(char const *name) { (void)name; return FALSE; }
static long cache_files_map_prefill_size(char const *name)
{ return held_index(name) == NONE && !(in_progress && !strcmp(copying, name)) ? 0x10000 : 0; }
static boolean cache_files_precache_in_progress(void) { return in_progress; }
static boolean cache_files_precache_is_copying_map(char const *name) { return in_progress && !strcmp(copying, name); }
static boolean cache_files_precache_map_begin(char *name, boolean copy_map)
{
	(void)copy_map;
	copies_begun++;
	if (!strcmp(failing, name))
	{
		failing[0] = 0;
		return FALSE;
	}
	strcpy(copying, name);
	in_progress = TRUE;
	copy_stopped = FALSE;
	return TRUE;
}
/* (a copy runs a frame; one is left going, as at a switch, while hold_copy is set) */
static boolean hold_copy;
static short cache_files_precache_map_status(real *progress)
{
	*progress = 1.0f;
	return hold_copy ? _cached_map_file_in_progress : _cached_map_file_success;
}
static void cache_files_precache_map_queue_end(void) { copy_stopped = TRUE; }
static void cache_files_precache_map_end(void)
{
	if (copy_stopped || hold_copy)
		copies_ended_unfinished++;
	else
		hold(tag_name_strip_path(copying));
	in_progress = FALSE;
	copying[0] = 0;
}
static unsigned long long cache_files_budget_update(void)
{
	int index;
	unsigned long long given_up = 0;

	budget_updates++;
	if (!budget_gives_up)
		return 0;
	for (index = 0; index < STOCK_MAPS; index++)
	{
		if (held[index][0] && strcmp(held[index], "ui"))
		{
			held[index][0] = 0;
			given_up += 0x10000;
		}
	}
	return given_up;
}

#include "under_test.inc"

static void frames(int count)
{
	while (count--)
		map_cache_prefill_update();
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";

	CASE("instant_copies_every_map")
	{
		setting_megabytes = 0;
		frames(400);
		CHECK(held_count() == STOCK_MAPS, "%d of the %d stock maps copied", held_count(), STOCK_MAPS);
		CHECK(budget_updates == 0, "the budget read again with no change");
	}
	else CASE("saver_copies_none_ahead")
	{
		setting_megabytes = 730;
		frames(400);
		CHECK(copies_begun == 0, "%d copies begun ahead with a budget set", copies_begun);
	}
	else CASE("setting_unchanged")
	{
		/* another setting written: the budget is not read again */
		setting_megabytes = 0;
		frames(400);
		setting_changes++;
		frames(10);
		CHECK(budget_updates == 0, "the budget read again for another setting");
	}
	else CASE("switch_to_saver")
	{
		/* in the middle of a copy ahead: it is ended, maps are given up now
		(cache_files_budget_update), and none is copied ahead after */
		setting_megabytes = 0;
		frames(130);
		CHECK(held_count() > 0 && held_count() < STOCK_MAPS, "%d copied by frame 130", held_count());
		hold_copy = TRUE;
		frames(1);
		CHECK(in_progress, "no copy going on");
		hold_copy = FALSE;
		set_megabytes(730);
		frames(1);
		CHECK(budget_updates == 1, "the budget read %d times", budget_updates);
		CHECK(!in_progress && copies_ended_unfinished == 1, "the copy ahead not ended");
		CHECK(held_count() == 1, "%d maps held, not given up", held_count());
		copies_begun = 0;
		frames(400);
		CHECK(copies_begun == 0, "%d copies begun ahead after Disk Space Saver", copies_begun);
	}
	else CASE("switch_back_rebuilds")
	{
		/* every map copied (one failing once, so marked failed), Disk Space
		Saver given up all but the main menu's, then back at Instant Load
		Times: every one copied again, the failed one too */
		setting_megabytes = 0;
		strcpy(failing, "levels\\b30\\b30");
		frames(400);
		CHECK(held_count() == STOCK_MAPS - 1 && held_index("b30") == NONE, "b30's failed copy: %d held",
			held_count());
		set_megabytes(730);
		frames(10);
		CHECK(held_count() == 1, "%d maps held after Disk Space Saver", held_count());
		set_megabytes(0);
		frames(400);
		CHECK(budget_updates == 2, "the budget read %d times", budget_updates);
		CHECK(held_count() == STOCK_MAPS, "%d of the %d stock maps copied again", held_count(), STOCK_MAPS);
	}
	else return 2;
	return 0;
}
