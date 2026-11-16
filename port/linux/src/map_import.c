/*
MAP_IMPORT.C

Custom Edition's maps from the player's own copy of its installer
(halocesetup_en_1.00.exe), into the custom_maps folder, where OpenH1 plays
Custom Edition maps from (docs/custom_edition_caches.md): its multiplayer
maps (Halo PC's, Gearbox's among them) and the bitmaps.map, sounds.map and
loc.map they are read with. The reading is ChupathingyCE's
(ce_installer.c, cabinet.c); this is OpenH1's glue to it, for the first
start (first_start.c on Linux and macOS, sdl_platform.c on Windows) and
for the command line (halo --import-maps <file>, server_mode.c), which a
game that already has its Xbox maps uses.

A file the player picks is told apart by its contents: an Xbox disc image
of Halo (xiso.c), Custom Edition's installer, or another CD image. Halo
PC's own CD is one: its maps are Halo PC's (cache version 7, with Halo
PC's own bitmaps.map and sounds.map), which OpenH1 does not play, so the
player is told that the installer has the same multiplayer maps as Custom
Edition's.

Only maps the folder lacks are written, so a map the player has already
(their own bitmaps.map, a newer bloodgulch.map) is left as it is.
*/

#include "platform.h"

#ifndef HALO_ANDROID

#include "map_import.h"
#include "ce_installer.h"
#include "pc_disc.h"
#include "port_config.h"
#include "xiso.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char map_import_cd_image_text[] =
	"This is a CD image, not an Xbox disc image of Halo. If it is Halo PC's CD: GOAT H1 does not play Halo PC's "
	"own maps, but Custom Edition's installer (halocesetup_en_1.00.exe) has the same multiplayer maps in the "
	"form GOAT H1 plays. Choose the installer instead.";

int map_import_kind(const char *path)
{
	if (xiso_is_halo_image(path))
		return _map_import_xbox_disc;
	if (ce_installer_is_installer(path))
		return _map_import_custom_edition_installer;
	if (pc_disc_is_image(path))
		return _map_import_cd_image;
	return _map_import_unknown;
}

void map_import_custom_maps_folder(const char *destination, char *folder, int size)
{
	const char *setting = config_string("paths.custom_maps");

	if (setting[0])
		snprintf(folder, (size_t)size, "%s", setting);
	else
		snprintf(folder, (size_t)size, "%s/custom_maps", destination);
}

struct import_progress
{
	map_import_progress_proc progress;
	void *context;
	unsigned long long done;
};

static void import_progress(void *context, const char *file, unsigned long long done, unsigned long long total)
{
	struct import_progress *import = context;

	import->done = done;
	if (import->progress)
		import->progress(import->context, file, done, total);
}

int map_import_custom_edition(const char *installer, const char *folder, map_import_progress_proc progress,
	void *context, unsigned long long *written, char *error, int error_size)
{
	struct import_progress import;
	int result;

	import.progress = progress;
	import.context = context;
	import.done = 0;
	platform_log("importing Custom Edition's maps from %s into %s", installer, folder);
	result = ce_installer_extract_maps(installer, folder, import_progress, &import, error, error_size);
	*written = import.done;
	if (result)
		platform_log("imported %.1f MB of Custom Edition's maps", (double)import.done / 1e6);
	else
		platform_log("the import of Custom Edition's maps failed: %s", error);
	return result;
}

/* ---------- the command line */

static void command_line_progress(void *context, const char *file, unsigned long long done,
	unsigned long long total)
{
	int *shown = context;
	int percent = total ? (int)(done * 100 / total) : 100;

	/* (every 10 percent) */
	if (percent / 10 != *shown / 10)
	{
		*shown = percent;
		fprintf(stdout, "%3d%%  %s\n", percent, file);
		fflush(stdout);
	}
}

void map_import_command_line(const char *path)
{
	char folder[1024];
	char error[512] = "";
	unsigned long long written = 0;
	int shown = -10;

	switch (map_import_kind(path))
	{
	case _map_import_custom_edition_installer:
		break;
	case _map_import_xbox_disc:
		fprintf(stderr, "%s is an Xbox disc image: the game copies its maps at its first start, when it has none.\n",
			path);
		exit(EXIT_FAILURE);
	case _map_import_cd_image:
		fprintf(stderr, "%s: %s\n", path, map_import_cd_image_text);
		exit(EXIT_FAILURE);
	default:
		fprintf(stderr, "%s is not Custom Edition's installer (halocesetup_en_1.00.exe), or could not be read.\n",
			path);
		exit(EXIT_FAILURE);
	}
	/* (the data root's custom_maps, or paths.custom_maps) */
	platform_translate_path("d:\\custom_maps", folder, sizeof(folder));
	fprintf(stdout, "Importing Custom Edition's maps from %s into %s\n", path, folder);
	fflush(stdout);
	if (!map_import_custom_edition(path, folder, command_line_progress, &shown, &written, error, sizeof(error)))
	{
		fprintf(stderr, "%s\n", error);
		exit(EXIT_FAILURE);
	}
	if (written)
		fprintf(stdout, "Done: %.1f MB of maps written. They are in the map lists' CUSTOM MULTIPLAYER.\n",
			(double)written / 1e6);
	else
		fprintf(stdout, "%s already has every map the installer has: nothing written.\n", folder);
	fflush(NULL);
	exit(EXIT_SUCCESS);
}

#endif
