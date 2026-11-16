/*
GAME_DATA_SCAN.C

The first start's search for the player's game data (game_data_scan.h), and
the copy of a maps folder it finds. On a Steam Deck in Game Mode (gamescope)
or in a VR headset no system file dialog can be shown, so the game finds the
disc image itself, in the places a player puts a downloaded or copied file,
and first_start.c offers what it finds in the game's own window. The macOS
app does the same (its window works in fullscreen, in any Space and with a
controller), and looks first in the maps folders its earlier versions used.

The search is shallow and bounded: each place a few folders deep, hidden
folders and the large folders of games (steamapps) skipped, and the whole
within a time limit and a number of entries. An image is offered only once
the caller's check (xiso.c) has found Halo's maps in it, or found a program
named halo...exe Custom Edition's installer (ce_installer.c); a maps folder
only when its ui.map is the Xbox game's (not Halo PC's, version 7, or
Custom Edition's, 609). (Files are reached through posix.h, as the rest of the
platform layer's: the game's ABI cannot use the C library's struct stat.)

On macOS the folders the system guards (Documents, Pictures, Music, Movies,
the Library) are not looked in: each would ask the player to let the game
in. Downloads and the Desktop are, and asking for them is the price of
finding the image there; the time the player takes to answer is not counted
against the scan's.
*/

#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID) && !defined(HALO_IOS)

#include "game_data_scan.h"
#include "posix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* an image is a few GB; anything under this is not one */
#define SCAN_MINIMUM_IMAGE_SIZE (64ULL * 1024 * 1024)
#define SCAN_MAXIMUM_VISITS 20000
#define SCAN_MAXIMUM_SEEN 64
#define SCAN_MAXIMUM_MOUNTS 16
#define COPY_CHUNK (1024 * 1024)

/* ui.map's header: its signature ("head") and the versions of Halo PC's maps */
#define MAP_HEADER_SIGNATURE 0x68656164UL
#define HALO_PC_MAP_VERSION 7
#define CUSTOM_EDITION_MAP_VERSION 609

struct scan_root
{
	const char *path;
	/* where it is, for the player ("" for a media root: each drive's own) */
	const char *label;
	/* how many folders deep below it the search goes */
	int depth;
};

struct scan_mount
{
	char point[GAME_DATA_PATH_SIZE];
	int sd_card;
};

struct scan
{
	const struct game_data_places *places;
	game_data_image_check check;
	void *context;
	struct game_data_candidate *candidates;
	int maximum;
	int count;
	/* the files found, by name, size and time of modification (posix_stat
	has no inode), so that a file linked to (the Steam Frame's installer
	links the image next to the game, under its name) or reached twice is
	offered once */
	struct
	{
		char name[64];
		unsigned long long size;
		unsigned long seconds, nanoseconds;
	} seen[SCAN_MAXIMUM_SEEN];
	int seen_count;
	struct scan_mount mounts[SCAN_MAXIMUM_MOUNTS];
	int mount_count;
	/* the earlier maps folders' places, for the player */
	char earlier_labels[GAME_DATA_EARLIER][GAME_DATA_PATH_SIZE];
	struct timespec started;
	long visits;
	int stopped;
};

/* ---------- names */

/* strcasecmp, in ASCII */
static int name_compare(const char *a, const char *b)
{
	for (;; a++, b++)
	{
		int x = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
		int y = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;

		if (x != y || !x)
			return x - y;
	}
}

static int name_ends_with(const char *name, const char *ending)
{
	size_t length = strlen(name);
	size_t ending_length = strlen(ending);

	return length > ending_length && !name_compare(name + length - ending_length, ending);
}

/* a disc image, or Custom Edition's installer (halocesetup_en_1.00.exe, or
another program whose name starts with halo), which the caller's check
tells apart by its contents (first_start.c) */
static int name_is_image(const char *name)
{
	char start[5];

	snprintf(start, sizeof(start), "%s", name);
	return name_ends_with(name, ".iso") || name_ends_with(name, ".xiso") ||
		(name_ends_with(name, ".exe") && !name_compare(start, "halo"));
}

/* folders no disc image is put in, too large to look through */
static int name_is_skipped(const char *name)
{
	static const char *const skipped[] =
	{
		"steamapps", "compatdata", "shadercache", "node_modules", "lost+found", "System Volume Information",
		"maps.partial", "proc", "sys", "dev",
#ifdef HALO_MACOS
		"Library", "Documents", "Pictures", "Music", "Movies", "Applications",
#endif
	};
	size_t index;

	if (name[0] == '.' || name[0] == '$')
		return 1;
	for (index = 0; index < sizeof(skipped) / sizeof(skipped[0]); index++)
	{
		if (!name_compare(name, skipped[index]))
			return 1;
	}
	return 0;
}

static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash && slash[1] ? slash + 1 : path;
}

static unsigned long long file_size(const struct posix_file_information *information)
{
	return (unsigned long long)information->size_high << 32 | information->size_low;
}

static int is_folder(const struct posix_file_information *information)
{
	return (information->flags & _posix_file_is_directory) != 0;
}

/* ---------- the scan's limits */

static long milliseconds_since(const struct timespec *start)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (long)(now.tv_sec - start->tv_sec) * 1000 + (now.tv_nsec - start->tv_nsec) / 1000000;
}

static int scan_should_stop(struct scan *scan)
{
	if (!scan->stopped && (++scan->visits > SCAN_MAXIMUM_VISITS || scan->count >= scan->maximum ||
		((scan->visits & 31) == 0 && milliseconds_since(&scan->started) > scan->places->time_limit)))
	{
		scan->stopped = 1;
	}
	return scan->stopped;
}

/* whether a file was found before; remembers it if not */
static int scan_seen(struct scan *scan, const char *name, const struct posix_file_information *information)
{
	unsigned long long size = file_size(information);
	int index;

	for (index = 0; index < scan->seen_count; index++)
	{
		if (scan->seen[index].size == size && scan->seen[index].seconds == information->modification_seconds &&
			scan->seen[index].nanoseconds == information->modification_nanoseconds &&
			!strncmp(scan->seen[index].name, name, sizeof(scan->seen[index].name) - 1))
		{
			return 1;
		}
	}
	if (scan->seen_count < SCAN_MAXIMUM_SEEN)
	{
		snprintf(scan->seen[scan->seen_count].name, sizeof(scan->seen[scan->seen_count].name), "%s", name);
		scan->seen[scan->seen_count].size = size;
		scan->seen[scan->seen_count].seconds = information->modification_seconds;
		scan->seen[scan->seen_count].nanoseconds = information->modification_nanoseconds;
		scan->seen_count++;
	}
	return 0;
}

/* ---------- the drives */

/* a mount point as the mounts table has it: its spaces and tabs as octal
escapes (\040) */
static void unescape_mount_point(char *text)
{
	char *read = text, *write = text;

	while (*read)
	{
		if (read[0] == '\\' && read[1] >= '0' && read[1] <= '3' && read[2] >= '0' && read[2] <= '7' &&
			read[3] >= '0' && read[3] <= '7')
		{
			*write++ = (char)((read[1] - '0') * 64 + (read[2] - '0') * 8 + (read[3] - '0'));
			read += 4;
		}
		else
		{
			*write++ = *read++;
		}
	}
	*write = 0;
}

static int path_is_below(const char *path, const char *folder)
{
	size_t length = strlen(folder);

	return length && !strncmp(path, folder, length) && (path[length] == '/' || !path[length]);
}

/* the drives mounted under the media roots, and which are SD cards (the
Steam Deck's is /dev/mmcblk0p1, mounted under /run/media) */
static void scan_read_mounts(struct scan *scan)
{
	FILE *file = scan->places->mounts[0] ? fopen(scan->places->mounts, "r") : NULL;
	char line[2048];

	if (!file)
		return;
	while (scan->mount_count < SCAN_MAXIMUM_MOUNTS && fgets(line, sizeof(line), file))
	{
		char device[512], point[GAME_DATA_PATH_SIZE];
		int root;

		if (sscanf(line, "%511s %1023s", device, point) != 2)
			continue;
		unescape_mount_point(point);
		for (root = 0; root < GAME_DATA_MEDIA_ROOTS; root++)
		{
			const char *media = scan->places->media[root];

			if (media[0] && path_is_below(point, media) && strcmp(point, media))
			{
				struct scan_mount *mount = &scan->mounts[scan->mount_count++];

				snprintf(mount->point, sizeof(mount->point), "%s", point);
				mount->sd_card = !strncmp(device, "/dev/mmcblk", 11);
				break;
			}
		}
	}
	fclose(file);
}

/* where a folder is, for the player: its root's label, or below a media
root the drive's ("SD card", "Drive NAME"), then the folders below that */
static void scan_place(struct scan *scan, const struct scan_root *root, const char *folder, char *place, size_t size)
{
	const char *below = folder + strlen(root->path);
	char label[300];

	if (root->label[0])
	{
		snprintf(label, sizeof(label), "%s", root->label);
	}
	else
	{
		const struct scan_mount *drive = NULL;
		int index;

		for (index = 0; index < scan->mount_count; index++)
		{
			if (path_is_below(folder, scan->mounts[index].point) &&
				(!drive || strlen(scan->mounts[index].point) > strlen(drive->point)))
			{
				drive = &scan->mounts[index];
			}
		}
		if (drive)
		{
			if (drive->sd_card)
				snprintf(label, sizeof(label), "SD card");
			else
				snprintf(label, sizeof(label), "Drive %.250s", base_name(drive->point));
			below = folder + strlen(drive->point);
		}
		else if (!scan->places->mounts[0] && *below == '/')
		{
			/* (no table of mounts: the drive is the first folder below,
			/Volumes/NAME) */
			size_t length = strcspn(below + 1, "/");

			snprintf(label, sizeof(label), "Drive %.*s", (int)(length < 250 ? length : 250), below + 1);
			below += 1 + length;
		}
		else
		{
			snprintf(label, sizeof(label), "Drive");
		}
	}
	while (*below == '/')
		below++;
	snprintf(place, size, "%s%s%s", label, *below ? "/" : "", below);
}

/* ---------- the candidates */

static void scan_add(struct scan *scan, int kind, const char *path, const char *name, const char *place,
	unsigned long long size)
{
	struct game_data_candidate *candidate;

	if (scan->count >= scan->maximum)
		return;
	candidate = &scan->candidates[scan->count++];
	memset(candidate, 0, sizeof(*candidate));
	candidate->kind = kind;
	snprintf(candidate->path, sizeof(candidate->path), "%s", path);
	snprintf(candidate->name, sizeof(candidate->name), "%s", name);
	snprintf(candidate->place, sizeof(candidate->place), "%s", place);
	candidate->size = size;
}

static unsigned long read_u32(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

/* whether a ui.map is the Xbox game's */
static int ui_map_is_xbox(const char *path)
{
	unsigned char header[8];
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	long count;
	unsigned long version;

	if (descriptor < 0)
		return 0;
	count = (long)read(descriptor, header, sizeof(header));
	close(descriptor);
	if (count != (long)sizeof(header) || read_u32(header) != MAP_HEADER_SIGNATURE)
		return 0;
	version = read_u32(header + 4);
	return version != 0 && version != HALO_PC_MAP_VERSION && version != CUSTOM_EDITION_MAP_VERSION;
}

/* the bytes of a maps folder's maps */
static unsigned long long maps_size(const char *folder)
{
	void *directory = posix_directory_open(folder);
	char name[256];
	unsigned long long total = 0;

	if (!directory)
		return 0;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[GAME_DATA_PATH_SIZE + 256];
		struct posix_file_information information;

		if (!name_ends_with(name, ".map"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (posix_stat(path, &information) == 0 && !is_folder(&information))
			total += file_size(&information);
	}
	posix_directory_close(directory);
	return total;
}

/* a folder with ui.map in it */
static void scan_maps_folder(struct scan *scan, const struct scan_root *root, const char *folder,
	const char *ui_map, const struct posix_file_information *information)
{
	char parent[GAME_DATA_PATH_SIZE];
	char place[GAME_DATA_PATH_SIZE];
	char *slash;

	/* (known by its ui.map) */
	if (!ui_map_is_xbox(ui_map) || scan_seen(scan, base_name(folder), information))
		return;
	/* (shown as where the maps folder is) */
	snprintf(parent, sizeof(parent), "%s", folder);
	slash = strrchr(parent, '/');
	if (strlen(folder) > strlen(root->path) && slash && slash > parent)
		*slash = 0;
	scan_place(scan, root, parent, place, sizeof(place));
	scan_add(scan, _game_data_maps_folder, folder, base_name(folder), place, maps_size(folder));
}

static void scan_image(struct scan *scan, const struct scan_root *root, const char *folder, const char *path,
	const struct posix_file_information *information)
{
	char place[GAME_DATA_PATH_SIZE];

	if (file_size(information) < SCAN_MINIMUM_IMAGE_SIZE || scan_seen(scan, base_name(path), information))
		return;
	if (scan->check && !scan->check(path, scan->context))
		return;
	scan_place(scan, root, folder, place, sizeof(place));
	scan_add(scan, _game_data_image, path, base_name(path), place, file_size(information));
}

/* whether a folder is one of the other places, looked in on its own */
static int scan_is_other_root(const struct scan *scan, const char *path)
{
	const struct game_data_places *places = scan->places;
	int index;

	if (!strcmp(path, places->downloads) || !strcmp(path, places->desktop) || !strcmp(path, places->destination))
		return 1;
	for (index = 0; index < GAME_DATA_MEDIA_ROOTS; index++)
	{
		if (places->media[index][0] && !strcmp(path, places->media[index]))
			return 1;
	}
	for (index = 0; index < GAME_DATA_EARLIER; index++)
	{
		if (places->earlier[index][0] && !strcmp(path, places->earlier[index]))
			return 1;
	}
	return 0;
}

/* whether a folder is a symbolic link (/Volumes holds one to the startup
disk, /) */
static int is_link(const char *path)
{
	char target[8];

	return readlink(path, target, sizeof(target)) >= 0;
}

static void scan_folder(struct scan *scan, const struct scan_root *root, const char *folder, int depth)
{
	void *directory;
	char name[256];
	struct timespec opened;
	long waited;
	int pass;

	if (scan->stopped)
		return;
	clock_gettime(CLOCK_MONOTONIC, &opened);
	directory = posix_directory_open(folder);
	/* (the time the player took to let the game in, where the system asks:
	macOS's Downloads, Desktop and drives) */
	waited = milliseconds_since(&opened);
	if (waited > 250)
	{
		long long nanoseconds = (long long)scan->started.tv_nsec + (long long)(waited % 1000) * 1000000;

		scan->started.tv_sec += waited / 1000 + (time_t)(nanoseconds / 1000000000);
		scan->started.tv_nsec = (long)(nanoseconds % 1000000000);
	}
	/* (its files first, then its folders: what lies near the top of a
	drive or a folder is found before the time runs out in a deep one) */
	for (pass = 0; directory && pass < 2; pass++)
	{
		if (pass)
			directory = posix_directory_open(folder);
		while (directory && !scan_should_stop(scan) && posix_directory_next(directory, name, sizeof(name)))
		{
			char path[GAME_DATA_PATH_SIZE];
			struct posix_file_information information;
			int image = name_is_image(name);
			int ui_map = !name_compare(name, "ui.map");

			/* (only what may be offered or entered is looked at: most
			entries cost no system call) */
			if (!pass ? !image && !ui_map : depth >= root->depth || name_is_skipped(name))
				continue;
			if ((size_t)snprintf(path, sizeof(path), "%s/%s", folder, name) >= sizeof(path) ||
				posix_stat(path, &information) != 0)
			{
				continue;
			}
			if (!pass && !is_folder(&information))
			{
				if (ui_map)
					scan_maps_folder(scan, root, folder, path, &information);
				else
					scan_image(scan, root, folder, path, &information);
			}
			else if (pass && is_folder(&information) && !scan_is_other_root(scan, path) &&
				!(depth == 0 && !root->label[0] && !scan->places->mounts[0] && is_link(path)))
			{
				scan_folder(scan, root, path, depth + 1);
			}
		}
		if (directory)
			posix_directory_close(directory);
	}
}

/* ---------- public code */

/* an XDG user folder ("XDG_DOWNLOAD_DIR") from user-dirs.dirs, else
<home>/<fallback> */
static void user_folder(const char *home, const char *key, const char *fallback, char *folder, size_t size)
{
	const char *config_home = getenv("XDG_CONFIG_HOME");
	char path[GAME_DATA_PATH_SIZE + 64];
	char line[GAME_DATA_PATH_SIZE + 64];
	FILE *file;

	snprintf(folder, size, "%s/%s", home, fallback);
	if (config_home && config_home[0] == '/')
		snprintf(path, sizeof(path), "%s/user-dirs.dirs", config_home);
	else
		snprintf(path, sizeof(path), "%s/.config/user-dirs.dirs", home);
	file = fopen(path, "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file))
	{
		size_t key_length = strlen(key);
		char *value, *end;

		if (strncmp(line, key, key_length) || line[key_length] != '=' || line[key_length + 1] != '"')
			continue;
		value = line + key_length + 2;
		end = strchr(value, '"');
		if (!end)
			continue;
		*end = 0;
		/* ($HOME/Downloads, or a whole path) */
		if (!strncmp(value, "$HOME/", 6))
			snprintf(folder, size, "%s/%s", home, value + 6);
		else if (value[0] == '/')
			snprintf(folder, size, "%s", value);
	}
	fclose(file);
	while (strlen(folder) > 1 && folder[strlen(folder) - 1] == '/')
		folder[strlen(folder) - 1] = 0;
}

/* the earlier versions' maps folders (HALO_MAPS_USED_BEFORE: the macOS
app's host, port/macos/host_app.m, names them, a path a line) */
static void earlier_folders(struct game_data_places *places)
{
	const char *list = getenv("HALO_MAPS_USED_BEFORE");
	int count = 0;

	while (list && *list && count < GAME_DATA_EARLIER)
	{
		size_t length = strcspn(list, "\n");

		if (length && length < GAME_DATA_PATH_SIZE && list[0] == '/')
		{
			memcpy(places->earlier[count], list, length);
			places->earlier[count++][length] = 0;
		}
		list += length + (list[length] == '\n');
	}
}

void game_data_default_places(struct game_data_places *places, const char *destination)
{
	const char *home = getenv("HOME");

	memset(places, 0, sizeof(*places));
	snprintf(places->destination, sizeof(places->destination), "%s", destination);
	if (home && home[0] == '/' && strcmp(home, "/"))
	{
		snprintf(places->home, sizeof(places->home), "%s", home);
		user_folder(home, "XDG_DOWNLOAD_DIR", "Downloads", places->downloads, sizeof(places->downloads));
		user_folder(home, "XDG_DESKTOP_DIR", "Desktop", places->desktop, sizeof(places->desktop));
		/* (a user folder set to the home folder itself is the home folder's) */
		if (!strcmp(places->downloads, places->home))
			places->downloads[0] = 0;
		if (!strcmp(places->desktop, places->home))
			places->desktop[0] = 0;
	}
#ifdef HALO_MACOS
	/* (each drive a folder of /Volumes, by the name the Finder shows) */
	snprintf(places->media[0], sizeof(places->media[0]), "/Volumes");
#else
	snprintf(places->media[0], sizeof(places->media[0]), "/run/media");
	snprintf(places->media[1], sizeof(places->media[1]), "/media");
	snprintf(places->mounts, sizeof(places->mounts), "/proc/self/mounts");
#endif
	earlier_folders(places);
	places->time_limit = 3000;
}

void game_data_short_path(const struct game_data_places *places, const char *path, char *result, int size)
{
	if (places->home[0] && path_is_below(path, places->home))
		snprintf(result, (size_t)size, "~%s", path + strlen(places->home));
	else
		snprintf(result, (size_t)size, "%s", path);
}

int game_data_scan(const struct game_data_places *places, game_data_image_check check, void *context,
	struct game_data_candidate *candidates, int maximum)
{
	static struct scan scan;
	struct scan_root roots[GAME_DATA_ROOTS];
	int root_count = 0;
	int index, images;

	memset(&scan, 0, sizeof(scan));
	scan.places = places;
	scan.check = check;
	scan.context = context;
	scan.candidates = candidates;
	scan.maximum = maximum;
	clock_gettime(CLOCK_MONOTONIC, &scan.started);
	scan_read_mounts(&scan);
	/* (in the order offered: the maps an earlier version played, then where
	a player puts a file) */
	for (index = 0; index < GAME_DATA_EARLIER; index++)
	{
		char parent[GAME_DATA_PATH_SIZE];
		char *slash;

		if (!places->earlier[index][0])
			continue;
		/* (its place is the folder above it) */
		snprintf(parent, sizeof(parent), "%s", places->earlier[index]);
		slash = strrchr(parent, '/');
		if (slash && slash > parent)
			*slash = 0;
		game_data_short_path(places, parent, scan.earlier_labels[index], (int)sizeof(scan.earlier_labels[index]));
		roots[root_count].path = places->earlier[index];
		roots[root_count].label = scan.earlier_labels[index];
		/* (the maps folder itself, or a folder holding it) */
		roots[root_count++].depth = 1;
	}
	if (places->downloads[0])
	{
		roots[root_count].path = places->downloads;
		roots[root_count].label = "Downloads";
		roots[root_count++].depth = 2;
	}
	if (places->desktop[0])
	{
		roots[root_count].path = places->desktop;
		roots[root_count].label = "Desktop";
		roots[root_count++].depth = 2;
	}
	for (index = 0; index < GAME_DATA_MEDIA_ROOTS; index++)
	{
		if (!places->media[index][0])
			continue;
		/* (/run/media/<user>/<drive>/<folder>/<file>) */
		roots[root_count].path = places->media[index];
		roots[root_count].label = "";
		roots[root_count++].depth = 3;
	}
	if (places->home[0])
	{
		roots[root_count].path = places->home;
		roots[root_count].label = "Home folder";
		roots[root_count++].depth = 2;
	}
	if (places->destination[0])
	{
		roots[root_count].path = places->destination;
		roots[root_count].label = "The game's folder";
		roots[root_count++].depth = 0;
	}
	for (index = 0; index < root_count && !scan.stopped; index++)
		scan_folder(&scan, &roots[index], roots[index].path, 0);
	/* images first, each kind in the order found */
	for (images = 0, index = 0; index < scan.count; index++)
	{
		if (candidates[index].kind == _game_data_image)
		{
			struct game_data_candidate image = candidates[index];

			memmove(&candidates[images + 1], &candidates[images], (size_t)(index - images) * sizeof(*candidates));
			candidates[images++] = image;
		}
	}
	return scan.count;
}

void game_data_places_text(const struct game_data_places *places, char *text, int size)
{
	char destination[GAME_DATA_PATH_SIZE];

	game_data_short_path(places, places->destination, destination, (int)sizeof(destination));
	snprintf(text, (size_t)size, "%sDownloads, the Desktop, your home folder, %s, and %s",
		places->earlier[0][0] ? "the maps folder an earlier version used, " : "",
		places->mounts[0] ? "SD cards and USB drives" : "drives", destination);
}

/* ---------- a folder the player picked */

/* name in folder, in any case, into found (folder/name); nonzero if it is
there */
static int find_entry(const char *folder, const char *name, char *found, size_t size)
{
	void *directory = posix_directory_open(folder);
	char entry[256];
	int result = 0;

	if (!directory)
		return 0;
	while (!result && posix_directory_next(directory, entry, sizeof(entry)))
	{
		if (!name_compare(entry, name))
		{
			snprintf(found, size, "%s/%s", folder, entry);
			result = 1;
		}
	}
	posix_directory_close(directory);
	return result;
}

/* a ui.map's version, or 0 for a file that is not one of Halo's maps */
static unsigned long ui_map_version(const char *path)
{
	unsigned char header[8];
	int descriptor = open(path, O_RDONLY | O_CLOEXEC);
	long count;

	if (descriptor < 0)
		return 0;
	count = (long)read(descriptor, header, sizeof(header));
	close(descriptor);
	if (count != (long)sizeof(header) || read_u32(header) != MAP_HEADER_SIGNATURE)
		return 0;
	return read_u32(header + 4);
}

int game_data_picked_folder(const char *path, struct game_data_candidate *candidate, char *error, int error_size)
{
	struct posix_file_information information;
	char maps[GAME_DATA_PATH_SIZE], inner[GAME_DATA_PATH_SIZE], ui_map[GAME_DATA_PATH_SIZE + 256];
	char *slash;
	unsigned long version;

	if (posix_stat(path, &information) != 0 || !is_folder(&information))
		return 0;
	snprintf(maps, sizeof(maps), "%s", path);
	/* (the folder above the maps folder, the disc's copy, picked: its maps) */
	if (!find_entry(maps, "ui.map", ui_map, sizeof(ui_map)) && find_entry(path, "maps", inner, sizeof(inner)) &&
		posix_stat(inner, &information) == 0 && is_folder(&information))
	{
		snprintf(maps, sizeof(maps), "%s", inner);
	}
	if (!find_entry(maps, "ui.map", ui_map, sizeof(ui_map)))
	{
		snprintf(error, (size_t)error_size, "%s has no ui.map, so it is not Halo's maps folder. Choose the maps "
			"folder of Halo: Combat Evolved for the Xbox (it holds ui.map, bloodgulch.map and the other maps), "
			"or its disc image.", base_name(path));
		return -1;
	}
	version = ui_map_version(ui_map);
	if (version == HALO_PC_MAP_VERSION || version == CUSTOM_EDITION_MAP_VERSION)
	{
		snprintf(error, (size_t)error_size, "These are the maps of Halo for PC or Halo Custom Edition. GOAT H1 "
			"plays the maps of the Xbox game, Halo: Combat Evolved: choose its maps folder or its disc image.");
		return -1;
	}
	if (!version)
	{
		snprintf(error, (size_t)error_size, "The ui.map in %s is not one of Halo's maps.", base_name(maps));
		return -1;
	}
	memset(candidate, 0, sizeof(*candidate));
	candidate->kind = _game_data_maps_folder;
	snprintf(candidate->path, sizeof(candidate->path), "%s", maps);
	snprintf(candidate->name, sizeof(candidate->name), "%s", base_name(maps));
	snprintf(candidate->place, sizeof(candidate->place), "%s", maps);
	slash = strrchr(candidate->place, '/');
	if (slash && slash > candidate->place)
		*slash = 0;
	candidate->size = maps_size(maps);
	return 1;
}

/* ---------- the copy of a maps folder */

static void copy_error(char *error, int error_size, const char *format, const char *name)
{
	snprintf(error, (size_t)error_size, format, name);
}

/* the files of a maps folder that are copied: its maps and the loading
screen's picture */
static int copy_wanted(const char *name)
{
	return (name_ends_with(name, ".map") || !name_compare(name, "loading.tga")) && name[0] != '.';
}

/* removes the files an earlier, interrupted copy left */
static void copy_empty(const char *folder)
{
	void *directory = posix_directory_open(folder);
	char name[256];

	if (!directory)
		return;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[GAME_DATA_PATH_SIZE + 256];

		snprintf(path, sizeof(path), "%s/%s", folder, name);
		remove(path);
	}
	posix_directory_close(directory);
}

/* creates every missing folder along path */
static void copy_make_folders(const char *path)
{
	char partial[GAME_DATA_PATH_SIZE];
	size_t index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if (partial[index] == '/')
		{
			partial[index] = 0;
			posix_make_directory(partial);
			partial[index] = '/';
		}
	}
	posix_make_directory(partial);
}

static int copy_file(const char *from, const char *to, const char *name, unsigned char *buffer,
	unsigned long long *done, unsigned long long total, game_data_progress_proc progress, void *context,
	char *error, int error_size)
{
	int input = open(from, O_RDONLY | O_CLOEXEC);
	int output;
	int succeeded = 0;

	if (input < 0)
	{
		copy_error(error, error_size, "Could not read %s.", from);
		return 0;
	}
	output = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (output < 0)
	{
		close(input);
		copy_error(error, error_size, "Could not write %s.", to);
		return 0;
	}
	for (;;)
	{
		long count = (long)read(input, buffer, COPY_CHUNK);
		long written = 0;

		if (count < 0 && errno == EINTR)
			continue;
		if (count < 0)
		{
			copy_error(error, error_size, "Could not read %s.", from);
			break;
		}
		if (count == 0)
		{
			succeeded = 1;
			break;
		}
		while (written < count)
		{
			long result = (long)write(output, buffer + written, (size_t)(count - written));

			if (result < 0 && errno == EINTR)
				continue;
			if (result <= 0)
				break;
			written += result;
		}
		if (written < count)
		{
			copy_error(error, error_size, "Could not write %s (is the disk full?).", to);
			break;
		}
		*done += (unsigned long long)count;
		if (progress)
			progress(context, name, *done, total);
	}
	close(input);
	if (close(output) != 0 && succeeded)
	{
		copy_error(error, error_size, "Could not write %s (is the disk full?).", to);
		succeeded = 0;
	}
	return succeeded;
}

int game_data_copy_maps(const char *maps, const char *destination, game_data_progress_proc progress, void *context,
	char *error, int error_size)
{
	char partial[GAME_DATA_PATH_SIZE], final[GAME_DATA_PATH_SIZE];
	unsigned long long done = 0, total = 0;
	unsigned char *buffer;
	void *directory;
	char name[256];
	int succeeded = 1, copied = 0;

	if ((size_t)snprintf(partial, sizeof(partial), "%s/maps.partial", destination) >= sizeof(partial) ||
		(size_t)snprintf(final, sizeof(final), "%s/maps", destination) >= sizeof(final))
	{
		copy_error(error, error_size, "Could not create %s/maps.", destination);
		return 0;
	}
	directory = posix_directory_open(maps);
	if (!directory)
	{
		copy_error(error, error_size, "Could not open %s.", maps);
		return 0;
	}
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[GAME_DATA_PATH_SIZE + 256];
		struct posix_file_information information;

		snprintf(path, sizeof(path), "%s/%s", maps, name);
		if (copy_wanted(name) && posix_stat(path, &information) == 0 && !is_folder(&information))
			total += file_size(&information);
	}
	posix_directory_close(directory);
	buffer = malloc(COPY_CHUNK);
	directory = buffer ? posix_directory_open(maps) : NULL;
	if (!directory)
	{
		free(buffer);
		copy_error(error, error_size, "Could not open %s.", maps);
		return 0;
	}
	copy_make_folders(partial);
	copy_empty(partial);
	while (succeeded && posix_directory_next(directory, name, sizeof(name)))
	{
		char from[GAME_DATA_PATH_SIZE + 256], to[GAME_DATA_PATH_SIZE + 256];
		struct posix_file_information information;

		snprintf(from, sizeof(from), "%s/%s", maps, name);
		if (!copy_wanted(name) || posix_stat(from, &information) != 0 || is_folder(&information))
			continue;
		snprintf(to, sizeof(to), "%s/%s", partial, name);
		if (progress)
			progress(context, name, done, total);
		succeeded = copy_file(from, to, name, buffer, &done, total, progress, context, error, error_size);
		copied += succeeded;
	}
	posix_directory_close(directory);
	free(buffer);
	if (succeeded && !copied)
	{
		copy_error(error, error_size, "%s has no maps.", maps);
		succeeded = 0;
	}
	if (succeeded && rename(partial, final) != 0)
	{
		copy_error(error, error_size, "Could not create %s.", final);
		succeeded = 0;
	}
	return succeeded;
}

#endif
