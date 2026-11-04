/*
CABINET_TEST.C

The importers of Custom Edition's installer and of Halo PC's CD
(ce_installer.c, pc_disc.c, cabinet.c: ChupathingyCE's) and OpenH1's glue
to them (map_import.c), on the build machine, for tools/test_cabinet.py,
which writes the cabinets, installers and CD images:

	ce <installer> <folder>		ce_installer_extract_maps into folder
	pc <image> <destination>	pc_disc_extract_maps into destination/maps_pc
	import <installer> <folder>	map_import_custom_edition into folder

each printing 1 or 0, the error, and (import) the bytes written; and

	kind <file>			map_import_kind
	folder <destination>		map_import_custom_maps_folder (CUSTOM_MAPS
					in the environment standing for
					paths.custom_maps)
*/

#include "platform.h"
#include "port_config.h"
#include "ce_installer.h"
#include "pc_disc.h"
#include "map_import.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fputs("log: ", stderr);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

const char *config_string(const char *name)
{
	const char *value = getenv("CUSTOM_MAPS");

	return !strcmp(name, "paths.custom_maps") && value ? value : "";
}

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	snprintf(host_path, host_path_size, "%s", xbox_path);
}

int main(int argc, char **argv)
{
	char error[512] = "";

	if (argc == 4 && !strcmp(argv[1], "ce"))
	{
		int result = ce_installer_extract_maps(argv[2], argv[3], NULL, NULL, error, sizeof(error));

		printf("%d %s\n", result, error);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "pc"))
	{
		int result = pc_disc_extract_maps(argv[2], argv[3], NULL, NULL, error, sizeof(error));

		printf("%d %s\n", result, error);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "import"))
	{
		unsigned long long written = 0;
		int result = map_import_custom_edition(argv[2], argv[3], NULL, NULL, &written, error, sizeof(error));

		printf("%d %s\n%llu\n", result, error, written);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "kind"))
	{
		printf("%d\n", map_import_kind(argv[2]));
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "folder"))
	{
		char folder[1024];

		map_import_custom_maps_folder(argv[2], folder, sizeof(folder));
		printf("%s\n", folder);
		return 0;
	}
	fprintf(stderr, "usage: cabinet_test ce|pc|import <file> <folder> | kind <file> | folder <destination>\n");
	return 2;
}
