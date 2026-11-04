/*
BINK_IMPORT.C

The Xbox's movies (display.movies: bink_movie.c plays the data root's
bink folder) for a data root that has none. The first start copies them out
of the disc image with the maps (xiso.c), and writes where the image was
beside them (XISO_IMAGE_FILE). A data root whose maps came from elsewhere, or
from a version before the movies were copied, has no movies: once the
setting is on (at start-up, or when it is turned on in Settings), the image
named there, if it is still where it was, has its bink folder copied in a
thread of its own while the game goes on. xiso.c writes it to bink.partial
and renames it bink once whole, so the game finds all the movies or none.
Without an image, the log and STARTUP MOVIES' help (tools/port_settings.py)
say where to put the disc's bink folder.

Tried once a run. Not on Android, whose app extracts the image itself
(XisoExtractor.java), nor in a dedicated server.
*/

#include "platform.h"
#include "port_config.h"
#include "server_mode.h"
#include "bink_import.h"

#ifndef HALO_ANDROID

#include "posix.h"
#include "xiso.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static struct
{
	unsigned long read_at;
	int tried;
	char image[1024];
	char destination[1024];
} bink_import = { (unsigned long)-1 };

/* whether a name ends in .bik, ignoring case */
static int bink_import_movie_name(const char *name)
{
	size_t length = strlen(name);

	return length > 4 && name[length - 4] == '.' && (name[length - 3] | 0x20) == 'b' &&
		(name[length - 2] | 0x20) == 'i' && (name[length - 1] | 0x20) == 'k';
}

/* whether a folder holds a movie */
static int bink_import_has_movies(const char *folder)
{
	void *directory = posix_directory_open(folder);
	char name[260];
	int found = 0;

	if (!directory)
		return 0;
	while (!found && posix_directory_next(directory, name, sizeof(name)))
		found = bink_import_movie_name(name);
	posix_directory_close(directory);
	return found;
}

static void *bink_import_thread(void *argument)
{
	char error[512] = "";
	int count = xiso_extract_movies(bink_import.image, bink_import.destination, NULL, NULL, error,
		sizeof(error));

	(void)argument;
	if (count < 0)
		platform_log("movies: not copied from %s: %s", bink_import.image, error);
	else if (!count)
		platform_log("movies: %s has none (no bink folder of .bik files)", bink_import.image);
	else
		platform_log("movies: copied from %s to %s/bink (the intro plays from the next start)", bink_import.image,
			bink_import.destination);
	return NULL;
}

void bink_import_update(void)
{
	char folder[1024], path[1100];
	struct posix_file_information information;
	pthread_t thread;
	FILE *file;

	if (bink_import.read_at == config_changes())
		return;
	bink_import.read_at = config_changes();
	if (bink_import.tried || !config_boolean("display.movies") || halo_server_mode())
		return;
	bink_import.tried = 1;
	platform_translate_path("d:\\bink", folder, sizeof(folder));
	if (bink_import_has_movies(folder))
		return;
	snprintf(bink_import.destination, sizeof(bink_import.destination), "%s", platform_data_root());
	snprintf(path, sizeof(path), "%s/" XISO_IMAGE_FILE, bink_import.destination);
	bink_import.image[0] = 0;
	file = fopen(path, "r");
	if (file)
	{
		if (!fgets(bink_import.image, sizeof(bink_import.image), file))
			bink_import.image[0] = 0;
		fclose(file);
		bink_import.image[strcspn(bink_import.image, "\r\n")] = 0;
	}
	if (!bink_import.image[0] || posix_stat(bink_import.image, &information) != 0)
	{
		platform_log("movies: %s has none, and the disc image the maps came from is not known or not there: put "
			"the Xbox disc's bink folder (intro.bik and the rest) there", folder);
		return;
	}
	platform_log("movies: %s has none; copying them from %s", folder, bink_import.image);
	if (pthread_create(&thread, NULL, bink_import_thread, NULL) == 0)
		pthread_detach(thread);
	else
		platform_log("movies: the copy could not be started");
}

#else

void bink_import_update(void)
{
}

#endif
