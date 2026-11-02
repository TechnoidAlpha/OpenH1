/*
XISO_TEST.C

The disc image extractor (port/linux/src/xiso.c) on the build machine, for
tools/test_xiso.py, which writes the images: "maps <image> <destination>"
runs xiso_extract_maps and prints 1 or 0 and the error, "movies <image>
<destination>" runs xiso_extract_movies and prints its result and the error,
"halo <image>" prints xiso_is_halo_image's answer.
*/

#include "platform.h"
#include "xiso.h"

#include <stdarg.h>
#include <stdio.h>
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

int main(int argc, char **argv)
{
	char error[512] = "";

	if (argc == 4 && !strcmp(argv[1], "maps"))
	{
		int result = xiso_extract_maps(argv[2], argv[3], NULL, NULL, error, sizeof(error));

		printf("%d %s\n", result, error);
		return 0;
	}
	if (argc == 4 && !strcmp(argv[1], "movies"))
	{
		int result = xiso_extract_movies(argv[2], argv[3], NULL, NULL, error, sizeof(error));

		printf("%d %s\n", result, error);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "halo"))
	{
		printf("%d\n", xiso_is_halo_image(argv[2]));
		return 0;
	}
	fprintf(stderr, "usage: xiso_test maps|movies <image> <destination> | halo <image>\n");
	return 2;
}
