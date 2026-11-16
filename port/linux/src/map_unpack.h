/*
MAP_UNPACK.H

The stock maps unpacked into the cache's slots before the game starts, once
they are copied out of a disc image (map_unpack.c).
*/

#ifndef MAP_UNPACK_H
#define MAP_UNPACK_H

/* the maps of maps_folder unpacked into the slots of z_folder (host paths)
on the workers, within budget (bytes), with the slots' marks and the
language's file (XGetLanguage's) the game's start adopts them by; progress
as xiso_progress_proc's. Returns how many slots are whole */
int map_unpack_folder(const char *maps_folder, const char *z_folder, long language, unsigned long long budget,
	void (*progress)(void *context, const char *file, unsigned long long done, unsigned long long total),
	void *context);
/* the cache's budget's rules (map_cache.c's), its least, and the slots a
list of maps gets with every slot empty (-1: none) */
unsigned long long map_cache_choose_budget(long megabytes, unsigned long long cache, unsigned long long free,
	unsigned long long total, unsigned long long least);
unsigned long long map_unpack_least_budget(void);
int map_unpack_assign(const long *file_lengths, const short *types, int count, unsigned long long budget,
	short *slots);
int map_unpack_worker_count(void);
/* (the game's platform layer) the maps xiso_extract_maps copied to
<destination>/maps unpacked into z:\ */
int map_unpack_after_extraction(const char *destination,
	void (*progress)(void *context, const char *file, unsigned long long done, unsigned long long total),
	void *context);

#endif
