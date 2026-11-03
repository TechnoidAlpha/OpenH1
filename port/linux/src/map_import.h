/*
MAP_IMPORT.H

Custom Edition's maps from the player's own copy of its installer, into the
custom_maps folder (map_import.c), and what a file the player picks is.
*/

#ifndef __HALO_LINUX_MAP_IMPORT_H
#define __HALO_LINUX_MAP_IMPORT_H

/* what a file holds, told by its contents, not its name */
enum
{
	_map_import_unknown,
	/* an Xbox disc image of Halo (xiso.c): the game's own maps */
	_map_import_xbox_disc,
	/* Custom Edition's installer (ce_installer.c) */
	_map_import_custom_edition_installer,
	/* a CD image that is not an Xbox disc of Halo: Halo PC's, whose own maps
	OpenH1 does not play (pc_disc.c) */
	_map_import_cd_image,
};

/* called as the import goes: the map being written, and the bytes written
of all the maps the folder lacked */
typedef void (*map_import_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* what the file at path is (an Xbox image of a whole disc has a CD's file
system too, so it is looked for first) */
int map_import_kind(const char *path);

/* the custom_maps folder of a game whose data root is destination:
paths.custom_maps when it is set, else <destination>/custom_maps */
void map_import_custom_maps_folder(const char *destination, char *folder, int size);

/* writes the maps of Custom Edition's installer that folder lacks (each as
<name>.partial until complete); returns nonzero on success, with the bytes
written in *written (0 when the folder had them all), or 0 with the reason
(for the player) in error. Called from any thread */
int map_import_custom_edition(const char *installer, const char *folder, map_import_progress_proc progress,
	void *context, unsigned long long *written, char *error, int error_size);

/* what the player is told about a CD image that is not an Xbox disc */
extern const char map_import_cd_image_text[];

/* halo --import-maps <file>: the import into the data root's custom_maps,
said on the standard output, then the process ends (server_mode.c) */
void map_import_command_line(const char *path);

#endif
