/*
MAP_CACHE.C

The budget of the cache files of decompressed maps (z:\cacheNNN.map,
source/cache/cache_files_windows.c): how much of the disk they may take.

The game copies a map from the maps folder into a cache file, decompressed,
before it plays it, as the Xbox copied it from the disc to its hard disk:
on a fast computer most of a second for a campaign level (zlib's inflate),
the largest part of its load that is not the renderer's. The Xbox had six
cache files of fixed sizes; the port keeps up to 32, each its map's size,
so that every stock map is decompressed once (the menus' pictures' pass at
the first start copies them all) and never again: the 24 stock maps take
3.0 GB (the campaign's ten 2.4 GB, the thirteen multiplayer maps 0.57 GB,
the main menu's 34 MB). Maps played after them (Custom Edition's are read
in place, not copied) take their room in the same cache, the least
recently played map given up first, and before any played map those never
played, the least useful first (cache_files_windows.c).

The budget is game.map_cache_megabytes where set, else the disk's room:
what the cache takes now and the disk's free space, less a margin the
cache never takes (a twentieth of the disk, at least 2 GB and at most 5 GB), so that
the cache never fills the disk. Either way it is at least what the Xbox's
six cache files took (about 730 MB), as the builds before kept: what the
open map and a copy need.
*/

#include "platform.h"
#include "port_config.h"

#include <stdio.h>
#include <sys/mman.h>

#define MAP_CACHE_MEGABYTE (1024ULL * 1024ULL)

/* (map_unpack.c's, which the first start's unpack before the game shares) */
unsigned long long map_cache_choose_budget(long megabytes, unsigned long long cache, unsigned long long free,
	unsigned long long total, unsigned long long least);

unsigned long long map_cache_budget(unsigned long long cache_bytes, unsigned long long least)
{
	ULARGE_INTEGER free_bytes, total_bytes;
	unsigned long long free = 0, total = 0, budget;

	if (GetDiskFreeSpaceExA("z:\\", &free_bytes, &total_bytes, NULL))
	{
		free = ((unsigned long long)free_bytes.HighPart << 32) | free_bytes.LowPart;
		total = ((unsigned long long)total_bytes.HighPart << 32) | total_bytes.LowPart;
	}
	budget = map_cache_choose_budget(config_integer("game.map_cache_megabytes"), cache_bytes, free, total, least);
	platform_log("map cache: %llu MB of the disk (%llu MB taken, %llu MB free of %llu MB)",
		budget / MAP_CACHE_MEGABYTE, cache_bytes / MAP_CACHE_MEGABYTE, free / MAP_CACHE_MEGABYTE,
		total / MAP_CACHE_MEGABYTE);
	return budget;
}

/* the buffers a map's copy decompresses through (cache_files_decompress_windows.c's),
the game's own, kept, not taken from the texture cache's memory as the
Xbox's were, so that a copy made in a game (port/linux/game/map_cache_prefill.c)
leaves the textures alone: whole pages, for XPhysicalProtect */
void *map_cache_copy_buffer(long size)
{
	static void *buffer;
	static long buffer_size;

	if (size <= 0)
		return NULL;
	if (buffer && size > buffer_size)
	{
		munmap(buffer, (size_t)buffer_size);
		buffer = NULL;
	}
	if (!buffer)
	{
		long rounded = (size + 0xFFFF) & ~0xFFFFL;
		void *mapped = mmap(NULL, (size_t)rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (mapped == MAP_FAILED)
			return NULL;
		buffer = mapped;
		buffer_size = rounded;
	}
	return buffer;
}
