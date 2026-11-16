/*
GAME_CACHES.C

What the game keeps only to start and to load faster, cleared at the
player's asking: Settings' CLEAR CACHES (menu_functions.c, issue 774). Each is
made again as the game plays, so that clearing them shows the game as a
first start makes it (a check that nothing is held over from a version
before), at the cost of that start's time:

- the maps unpacked: z:\cacheNNN.map and .source in each copy of the game's
  cache folder (z:\, z:\cache-2 and on: xbox_files.c's cache_slot_folder),
  which the first start unpacks again (map_unpack.c, map_cache_prefill.c);
- the maps' prepared draws: z:\prepared (render_prepared.c);
- the menus' pictures: the data folder's menu_pictures (menu_pictures.c);
- the world's volumes for the ambient occlusion: the data folder's
  world_sdf (world_sdf.c);
- the host's: the GPU's pipelines and the ASTC textures (host_caches,
  port/linux/vulkan/host_vulkan.c and port/macos/host_metal.m).

Nothing is deleted while the game runs: the open map's cache file, the
pipelines and the pictures are in use. The request is a file in the
settings' folder (GAME_CACHES_REQUEST), and the host's its own (host_caches);
the next start clears the game's before anything opens them
(game_caches_start: shell_xbox.c's main), and the host's before its
renderer reads them. Another copy of the game running (z:\cache.lock held)
keeps the request for a start that has the caches to itself. Nothing but the
caches' own files is touched: the saves, the checkpoint and the settings
beside them in z:\ stay.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "game_caches.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>

/* the host's caches (guest_host.h) */
unsigned int host_caches(int what);
int halo_server_mode(void);

/* ---------- constants */

#define GAME_CACHES_REQUEST "clear_caches"
/* (xbox_files.c's MAXIMUM_CACHE_SLOT_FOLDERS: z:\ and z:\cache-2 to 8) */
#define GAME_CACHES_SLOT_FOLDERS 8
#define GAME_CACHES_PATH 1024

/* ---------- private code */

/* whether a file of a cache folder is an unpacked map's: cacheNNN.map, or
its note, cacheNNN.source (not cache.lock) */
static int game_caches_slot_file(const char *name)
{
	return strlen(name) > 8 && !strncmp(name, "cache", 5) && name[5] >= '0' && name[5] <= '9' &&
		(strstr(name, ".map") || strstr(name, ".source"));
}

/* the bytes of a folder's files (those slot_files takes, else all), each
deleted with clear; not its folders */
static unsigned long long game_caches_folder(const char *folder, int slot_files, int clear)
{
	void *directory = posix_directory_open(folder);
	char name[256];
	unsigned long long total = 0;

	if (!directory)
		return 0;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[GAME_CACHES_PATH + 256];
		struct posix_file_information information;

		if (slot_files && !game_caches_slot_file(name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (posix_stat(path, &information) || (information.flags & _posix_file_is_directory))
			continue;
		total += ((unsigned long long)information.size_high << 32) | information.size_low;
		if (clear)
			remove(path);
	}
	posix_directory_close(directory);
	return total;
}

/* a cache folder's (number 1: z:\, else z:\cache-number) path, into path */
static void game_caches_slot_folder(int number, char *path, int size)
{
	char xbox_path[32];

	if (number == 1)
		snprintf(xbox_path, sizeof(xbox_path), "z:\\");
	else
		snprintf(xbox_path, sizeof(xbox_path), "z:\\cache-%d", number);
	platform_translate_path(xbox_path, path, (unsigned long)size);
}

/* whether no copy of the game holds a cache folder (its cache.lock, as
xbox_files.c's cache_slot_folder takes it): the lock taken and let go */
static int game_caches_slot_free(const char *folder)
{
	char path[GAME_CACHES_PATH + 16];
	int descriptor, unheld;

	snprintf(path, sizeof(path), "%s/cache.lock", folder);
	descriptor = open(path, O_RDWR | O_CLOEXEC);
	if (descriptor < 0)
		return 1;
	unheld = syscall(SYS_flock, descriptor, LOCK_EX | LOCK_NB) == 0 || errno == ENOSYS;
	close(descriptor);
	return unheld;
}

/* the game's own caches' bytes (not the host's), each file deleted with
clear: the cache folders' only where no copy holds them */
static unsigned long long game_caches_own(int clear)
{
	char folder[GAME_CACHES_PATH];
	unsigned long long total = 0;
	int number;

	for (number = 1; number <= GAME_CACHES_SLOT_FOLDERS; number++)
	{
		game_caches_slot_folder(number, folder, sizeof(folder));
		if (folder[0] && (!clear || game_caches_slot_free(folder)))
			total += game_caches_folder(folder, 1, clear);
	}
	platform_translate_path("z:\\prepared", folder, sizeof(folder));
	if (folder[0])
		total += game_caches_folder(folder, 0, clear);
	snprintf(folder, sizeof(folder), "%s/menu_pictures", platform_data_root());
	total += game_caches_folder(folder, 0, clear);
	snprintf(folder, sizeof(folder), "%s/world_sdf", platform_data_root());
	total += game_caches_folder(folder, 0, clear);
	return total;
}

/* the request's file, in the settings' folder, into path */
static void game_caches_request_path(char *path, int size)
{
	char folder[GAME_CACHES_PATH];

	config_folder(folder, sizeof(folder));
	snprintf(path, size, "%s" GAME_CACHES_REQUEST, folder);
}

/* ---------- public code */

unsigned long long game_caches_bytes(void)
{
	return game_caches_own(0) + (unsigned long long)host_caches(0) * 1024ULL;
}

int game_caches_requested(void)
{
	char path[GAME_CACHES_PATH + 32];
	struct posix_file_information information;

	game_caches_request_path(path, sizeof(path));
	return posix_stat(path, &information) == 0;
}

/* the game's request made, or taken back (not the host's) */
static void game_caches_request_file(int request)
{
	char path[GAME_CACHES_PATH + 32];

	game_caches_request_path(path, sizeof(path));
	if (request)
	{
		FILE *file = fopen(path, "wb");

		if (file)
			fclose(file);
	}
	else
	{
		remove(path);
	}
}

void game_caches_request(int request)
{
	game_caches_request_file(request);
	host_caches(request ? 1 : 2);
	platform_log("caches: %s at the next start", request ? "to be cleared" : "no longer to be cleared");
}

void game_caches_start(void)
{
	char folder[GAME_CACHES_PATH];

	if (halo_server_mode() || !game_caches_requested())
		return;
	game_caches_slot_folder(1, folder, sizeof(folder));
	if (folder[0] && !game_caches_slot_free(folder))
	{
		platform_log("caches: another copy of the game is running; they are cleared at a later start");
		return;
	}
	platform_log("caches: cleared, as Settings asked: %llu MB", game_caches_own(1) / (1024ULL * 1024ULL));
	/* (the host's request is its own: its renderer clears its caches as it
	starts, before or after this) */
	game_caches_request_file(0);
}
