/*
GAME_DATA_SCAN_TEST.C

Tests of the first start's search for game data (port/linux/src/game_data_scan.c):
which disc images and maps folders it offers from a made-up home folder, SD
card, game folder and an earlier version's maps folder, in what order and
with what places, drives without a table of mounts (macOS's /Volumes), the
copy of a maps folder, and a folder the player picks. Built and run by tools/test_host_units.py with a scratch folder
as its argument; exits nonzero on a failure.
*/

#include "game_data_scan.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL line %d: ", __LINE__); \
			printf(__VA_ARGS__); \
			printf("\n"); \
			failures++; \
		} \
	} while (0)

static char root[900];

static void make_folder(const char *relative)
{
	char path[1024], partial[1024];
	size_t index;

	snprintf(path, sizeof(path), "%s/%s", root, relative);
	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if (partial[index] == '/')
		{
			partial[index] = 0;
			mkdir(partial, 0755);
			partial[index] = '/';
		}
	}
	mkdir(partial, 0755);
}

/* a file of size bytes (sparse), starting with bytes */
static void make_file(const char *relative, const void *bytes, size_t length, long long size)
{
	char path[1024];
	int descriptor;

	snprintf(path, sizeof(path), "%s/%s", root, relative);
	descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (descriptor < 0)
	{
		printf("FAIL: cannot create %s\n", path);
		exit(1);
	}
	if (length && write(descriptor, bytes, length) != (ssize_t)length)
		failures++;
	if (size > (long long)length && ftruncate(descriptor, (off_t)size) != 0)
		failures++;
	close(descriptor);
}

/* a ui.map header: "head" and the version */
static void make_ui_map(const char *relative, unsigned version, long long size)
{
	unsigned char header[8] = { 'd', 'a', 'e', 'h', 0, 0, 0, 0 };

	header[4] = (unsigned char)version;
	header[5] = (unsigned char)(version >> 8);
	make_file(relative, header, sizeof(header), size);
}

/* the stand-in for xiso.c's check: an image is Halo's if it starts with
"HALO" */
static int image_check(const char *path, void *context)
{
	char start[4] = { 0 };
	FILE *file = fopen(path, "rb");

	(*(int *)context)++;
	if (!file)
		return 0;
	if (fread(start, 1, 4, file) != 4)
		start[0] = 0;
	fclose(file);
	return !memcmp(start, "HALO", 4);
}

static void path_of(const char *relative, char *path, size_t size)
{
	snprintf(path, size, "%s/%s", root, relative);
}

static void test_scan(void)
{
	struct game_data_places places;
	struct game_data_candidate candidates[GAME_DATA_CANDIDATES];
	char mounts[1200], expected[1024], target[1024], link_path[1024];
	int count, checks = 0, index;
	FILE *file;

	make_folder("home/Downloads/Halo");
	make_folder("home/Desktop");
	make_folder("home/Halo/maps");
	make_folder("home/pc/maps");
	make_folder("home/.cache/deep");
	make_folder("home/a/b/c");
	make_folder("media/deck/SDCARD/Games");
	make_folder("media/deck/USB STICK");
	make_folder("media/deck/SDCARD/steamapps/common");
	make_folder("game");
	/* offered: Halo's images (any case of .iso, .xiso), big enough */
	make_file("home/Downloads/Halo.ISO", "HALO", 4, 100LL << 20);
	make_file("media/deck/SDCARD/Games/halo ce.xiso", "HALO", 4, 80LL << 20);
	/* not: too small, not Halo's, hidden, in a game library, too deep */
	make_file("home/Desktop/small.iso", "HALO", 4, 1 << 20);
	make_file("home/Downloads/Halo/other.iso", "NOPE", 4, 100LL << 20);
	make_file("home/.cache/deep/hidden.iso", "HALO", 4, 100LL << 20);
	make_file("media/deck/SDCARD/steamapps/common/x.iso", "HALO", 4, 100LL << 20);
	make_file("home/a/b/c/deep.iso", "HALO", 4, 100LL << 20);
	make_file("home/Downloads/readme.txt", "HALO", 4, 100LL << 20);
	/* a maps folder of the Xbox game's (ui.map version 5); not Halo PC's */
	make_ui_map("home/Halo/maps/ui.map", 5, 1 << 20);
	make_file("home/Halo/maps/a10.map", "x", 1, 3 << 20);
	make_file("home/Halo/maps/notes.txt", "x", 1, 0);
	make_ui_map("home/pc/maps/ui.map", 609, 1 << 20);
	/* the image linked next to the game (the Steam Frame's installer does)
	is the one in Downloads */
	path_of("home/Downloads/Halo.ISO", target, sizeof(target));
	path_of("game/Halo.ISO", link_path, sizeof(link_path));
	if (symlink(target, link_path) != 0)
		failures++;
	/* the SD card, and a USB drive by its name */
	path_of("mounts", mounts, sizeof(mounts));
	file = fopen(mounts, "w");
	if (!file)
		exit(1);
	fprintf(file, "/dev/nvme0n1p8 / ext4 rw 0 0\n");
	fprintf(file, "/dev/mmcblk0p1 %s/media/deck/SDCARD ext4 rw 0 0\n", root);
	fprintf(file, "/dev/sda1 %s/media/deck/USB\\040STICK vfat rw 0 0\n", root);
	fclose(file);

	memset(&places, 0, sizeof(places));
	path_of("game", places.destination, sizeof(places.destination));
	path_of("home", places.home, sizeof(places.home));
	path_of("home/Downloads", places.downloads, sizeof(places.downloads));
	path_of("home/Desktop", places.desktop, sizeof(places.desktop));
	path_of("media", places.media[0], sizeof(places.media[0]));
	snprintf(places.mounts, sizeof(places.mounts), "%s", mounts);
	places.time_limit = 10000;

	count = game_data_scan(&places, image_check, &checks, candidates, GAME_DATA_CANDIDATES);
	for (index = 0; index < count; index++)
		printf("found %d %s (%s, %llu)\n", candidates[index].kind, candidates[index].path, candidates[index].place,
			candidates[index].size);
	CHECK(count == 3, "%d candidates, not 3", count);
	if (count < 3)
		return;
	path_of("home/Downloads/Halo.ISO", expected, sizeof(expected));
	CHECK(candidates[0].kind == _game_data_image && !strcmp(candidates[0].path, expected), "first: %s",
		candidates[0].path);
	CHECK(!strcmp(candidates[0].name, "Halo.ISO"), "name %s", candidates[0].name);
	CHECK(!strcmp(candidates[0].place, "Downloads"), "place %s", candidates[0].place);
	CHECK(candidates[0].size == 100ULL << 20, "size %llu", candidates[0].size);
	CHECK(candidates[1].kind == _game_data_image && !strcmp(candidates[1].name, "halo ce.xiso"), "second: %s",
		candidates[1].path);
	CHECK(!strcmp(candidates[1].place, "SD card/Games"), "place %s", candidates[1].place);
	path_of("home/Halo/maps", expected, sizeof(expected));
	CHECK(candidates[2].kind == _game_data_maps_folder && !strcmp(candidates[2].path, expected), "third: %s",
		candidates[2].path);
	CHECK(!strcmp(candidates[2].place, "Home folder/Halo"), "place %s", candidates[2].place);
	CHECK(candidates[2].size == (1ULL << 20) + (3ULL << 20), "maps size %llu", candidates[2].size);
	/* (only images big enough and named so are read) */
	CHECK(checks == 3, "%d images checked, not 3", checks);

	/* a USB drive's place is its name */
	make_file("media/deck/USB STICK/halo.iso", "HALO", 4, 100LL << 20);
	count = game_data_scan(&places, image_check, &checks, candidates, GAME_DATA_CANDIDATES);
	CHECK(count == 4, "%d candidates with the USB drive, not 4", count);
	for (index = 0; index < count && strcmp(candidates[index].name, "halo.iso"); index++)
		;
	CHECK(index < 3 && !strcmp(candidates[index].place, "Drive USB STICK"), "USB drive: %s",
		index < count ? candidates[index].place : "not found");

	/* Custom Edition's installer, a program named halo...exe that the check
	takes; no other program is checked */
	make_file("home/Downloads/halocesetup_en_1.00.exe", "HALO", 4, 100LL << 20);
	make_file("home/Downloads/setup.exe", "HALO", 4, 100LL << 20);
	count = game_data_scan(&places, image_check, &checks, candidates, GAME_DATA_CANDIDATES);
	CHECK(count == 5, "%d candidates with the installer, not 5", count);
	for (index = 0; index < count && strcmp(candidates[index].name, "halocesetup_en_1.00.exe"); index++)
		;
	CHECK(index < count && candidates[index].kind == _game_data_image &&
		!strcmp(candidates[index].place, "Downloads"), "installer: %s",
		index < count ? candidates[index].place : "not found");

	/* the most there may be */
	count = game_data_scan(&places, image_check, &checks, candidates, 1);
	CHECK(count == 1, "%d candidates with room for 1", count);

	/* nothing at all */
	memset(&places, 0, sizeof(places));
	path_of("game/missing", places.destination, sizeof(places.destination));
	places.time_limit = 1000;
	count = game_data_scan(&places, image_check, &checks, candidates, GAME_DATA_CANDIDATES);
	CHECK(count == 0, "%d candidates in no places", count);
}

static void test_copy(void)
{
	char maps[1024], destination[1024], path[1200], error[512] = "";
	struct stat information;
	int result;

	path_of("home/Halo/maps", maps, sizeof(maps));
	path_of("copy/data", destination, sizeof(destination));
	/* (what an interrupted copy left) */
	make_folder("copy/data/maps.partial");
	make_file("copy/data/maps.partial/stale.map", "x", 1, 0);
	result = game_data_copy_maps(maps, destination, NULL, NULL, error, sizeof(error));
	CHECK(result, "copy failed: %s", error);
	snprintf(path, sizeof(path), "%s/maps/ui.map", destination);
	CHECK(stat(path, &information) == 0 && information.st_size == 1 << 20, "ui.map not copied whole");
	snprintf(path, sizeof(path), "%s/maps/a10.map", destination);
	CHECK(stat(path, &information) == 0 && information.st_size == 3 << 20, "a10.map not copied whole");
	snprintf(path, sizeof(path), "%s/maps/notes.txt", destination);
	CHECK(stat(path, &information) != 0, "notes.txt copied");
	snprintf(path, sizeof(path), "%s/maps/stale.map", destination);
	CHECK(stat(path, &information) != 0, "an interrupted copy's file kept");
	snprintf(path, sizeof(path), "%s/maps.partial", destination);
	CHECK(stat(path, &information) != 0, "maps.partial left");

	/* a folder without maps */
	path_of("home/Desktop", maps, sizeof(maps));
	path_of("copy/other", destination, sizeof(destination));
	result = game_data_copy_maps(maps, destination, NULL, NULL, error, sizeof(error));
	CHECK(!result && strstr(error, "has no maps"), "a folder without maps: %d, %s", result, error);
}

/* an earlier version's maps folder (HALO_MAPS_USED_BEFORE) first, shown
from ~; and drives named by their folders where there is no table of mounts
(macOS's /Volumes), whose links (to the startup disk) are not entered */
static void test_earlier_and_volumes(void)
{
	struct game_data_places places;
	struct game_data_candidate candidates[GAME_DATA_CANDIDATES];
	char earlier[1100], expected[1100], target[1024], link_path[1024];
	int count, checks = 0;

	make_folder("home2/Library/Old/maps");
	make_ui_map("home2/Library/Old/maps/ui.map", 5, 1 << 20);
	make_folder("Volumes/My Drive/Games");
	make_file("Volumes/My Drive/Games/halo.xiso", "HALO", 4, 100LL << 20);
	make_folder("startup/Users");
	make_file("startup/Users/elsewhere.iso", "HALO", 4, 100LL << 20);
	path_of("startup", target, sizeof(target));
	path_of("Volumes/Startup Disk", link_path, sizeof(link_path));
	if (symlink(target, link_path) != 0)
		failures++;
	path_of("home2", earlier, sizeof(earlier));
	setenv("HOME", earlier, 1);
	path_of("home2/Library/Old/maps", earlier, sizeof(earlier));
	/* (the path a line: one that is not a whole path is passed over) */
	strcat(earlier, "\nrelative/maps");
	setenv("HALO_MAPS_USED_BEFORE", earlier, 1);
	path_of("game2", expected, sizeof(expected));
	game_data_default_places(&places, expected);
	unsetenv("HALO_MAPS_USED_BEFORE");
	path_of("home2/Library/Old/maps", expected, sizeof(expected));
	CHECK(!strcmp(places.earlier[0], expected) && !places.earlier[1][0], "earlier %s, %s", places.earlier[0],
		places.earlier[1]);
	path_of("Volumes", places.media[0], sizeof(places.media[0]));
	places.media[1][0] = 0;
	places.mounts[0] = 0;
	places.time_limit = 10000;
	count = game_data_scan(&places, image_check, &checks, candidates, GAME_DATA_CANDIDATES);
	CHECK(count == 2, "%d candidates, not 2", count);
	if (count < 2)
		return;
	CHECK(candidates[0].kind == _game_data_image && !strcmp(candidates[0].place, "Drive My Drive/Games"),
		"the drive's image: %s (%s)", candidates[0].path, candidates[0].place);
	CHECK(candidates[1].kind == _game_data_maps_folder && !strcmp(candidates[1].path, expected) &&
		!strcmp(candidates[1].place, "~/Library/Old"), "the earlier maps: %s (%s)", candidates[1].path,
		candidates[1].place);
}

/* a folder the player picks: the maps folder, the folder holding it,
Halo PC's, none, or a file */
static void test_picked_folder(void)
{
	struct game_data_candidate candidate;
	char path[1024], expected[1024], error[600];

	path_of("home/Halo/maps", path, sizeof(path));
	CHECK(game_data_picked_folder(path, &candidate, error, sizeof(error)) == 1 && !strcmp(candidate.path, path) &&
		candidate.kind == _game_data_maps_folder && candidate.size == (1ULL << 20) + (3ULL << 20),
		"the maps folder: %s", candidate.path);
	snprintf(expected, sizeof(expected), "%s", path);
	path_of("home/Halo", path, sizeof(path));
	CHECK(game_data_picked_folder(path, &candidate, error, sizeof(error)) == 1 && !strcmp(candidate.path, expected) &&
		!strcmp(candidate.place, path), "the folder holding it: %s (%s)", candidate.path, candidate.place);
	path_of("home/pc", path, sizeof(path));
	CHECK(game_data_picked_folder(path, &candidate, error, sizeof(error)) == -1 && strstr(error, "Halo for PC"),
		"Halo PC's: %s", error);
	path_of("home/Desktop", path, sizeof(path));
	CHECK(game_data_picked_folder(path, &candidate, error, sizeof(error)) == -1 && strstr(error, "no ui.map"),
		"no maps: %s", error);
	path_of("home/Downloads/Halo.ISO", path, sizeof(path));
	CHECK(game_data_picked_folder(path, &candidate, error, sizeof(error)) == 0, "a file");
}

static void test_default_places(void)
{
	struct game_data_places places;
	char home[1024], config[1024], file_path[1100], expected[1100];
	FILE *file;

	path_of("home", home, sizeof(home));
	path_of("home/.config", config, sizeof(config));
	make_folder("home/.config");
	snprintf(file_path, sizeof(file_path), "%s/user-dirs.dirs", config);
	file = fopen(file_path, "w");
	if (!file)
		exit(1);
	fprintf(file, "# written by xdg-user-dirs-update\nXDG_DESKTOP_DIR=\"$HOME/Bureau\"\n"
		"XDG_DOWNLOAD_DIR=\"$HOME/\"\n");
	fclose(file);
	setenv("HOME", home, 1);
	unsetenv("XDG_CONFIG_HOME");
	game_data_default_places(&places, "/opt/game");
	snprintf(expected, sizeof(expected), "%s/Bureau", home);
	CHECK(!strcmp(places.desktop, expected), "desktop %s", places.desktop);
	/* (Downloads set to the home folder is the home folder's) */
	CHECK(!places.downloads[0], "downloads %s", places.downloads);
	CHECK(!strcmp(places.home, home), "home %s", places.home);
	CHECK(!strcmp(places.destination, "/opt/game"), "destination %s", places.destination);
	CHECK(!strcmp(places.media[0], "/run/media") && !strcmp(places.media[1], "/media"), "media %s %s",
		places.media[0], places.media[1]);

	unlink(file_path);
	game_data_default_places(&places, "/opt/game");
	snprintf(expected, sizeof(expected), "%s/Downloads", home);
	CHECK(!strcmp(places.downloads, expected), "downloads %s", places.downloads);
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: %s <scratch folder>\n", argv[0]);
		return 2;
	}
	snprintf(root, sizeof(root), "%s", argv[1]);
	test_scan();
	test_copy();
	test_picked_folder();
	test_earlier_and_volumes();
	test_default_places();
	if (failures)
		printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
