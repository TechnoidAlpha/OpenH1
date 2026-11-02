/*
XISO.H

The maps and movies folders out of an Xbox disc image (xiso.c).
*/

#ifndef __HALO_LINUX_XISO_H
#define __HALO_LINUX_XISO_H

/* called as the copy goes: the file being copied, and the bytes copied of
all the files' */
typedef void (*xiso_progress_proc)(void *context, const char *file, unsigned long long done,
	unsigned long long total);

/* the file in the data root that says where the disc image the maps were
copied from is: its path and a newline (bink_import.c) */
#define XISO_IMAGE_FILE "disc_image.txt"

/* copies the image's maps folder to <destination>/maps, and its movies (its
bink folder's .bik files) to <destination>/bink when it has them and the
destination has none; returns nonzero on success, or 0 with the reason (for
the player) in error. The movies' failure is logged, not an error. Called
from any thread */
int xiso_extract_maps(const char *image_path, const char *destination, xiso_progress_proc progress, void *context,
	char *error, int error_size);

/* copies the image's movies to <destination>/bink, unless it has movies
already; returns how many the image has (0 for none), or -1 with the reason
in error. Called from any thread */
int xiso_extract_movies(const char *image_path, const char *destination, xiso_progress_proc progress,
	void *context, char *error, int error_size);

/* whether a file is an Xbox disc image with Halo's maps folder (its ui.map),
reading only its volume descriptor and two directory tables: cheap enough to
check every image a search finds (game_data_scan.c) */
int xiso_is_halo_image(const char *image_path);

#endif
