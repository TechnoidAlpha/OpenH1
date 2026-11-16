/*
CONFIG_CHECK.C

port/linux/src/port_config.c on its own, for tools/test_linux_port.py's
tests of config.toml: built with stand-ins for SDL, the platform layer
(the test writes them) and install_layout.c, the file in the folder given.

    config_check <folder> get <setting>          prints its value
    config_check <folder> write <setting> <value>
    config_check <folder> saving <setting>       prints the value the game reads
                                                 with battery saver on, then the
                                                 one Settings shows
    config_check <folder> player <n> <profile folder> <setting>
                                                 prints local player n's value, their
                                                 profile's folder that one ("": none)
    config_check <folder> profile-write <profile folder> <setting> <value>
    config_check <folder> profile-get <profile folder> <setting>
                                                 a profile's own value (1: none)
*/

#include "install_layout.h"
#include "port_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char folder[1024];

/* (the platform layer's command line: none here, so no --config, --server
or --version; and a dedicated server's private file, which none is) */
int posix_command_line_argument(int index, char *buffer, unsigned long size)
{
	(void)index;
	(void)buffer;
	(void)size;
	return 0;
}

int posix_server_private_file(const char *path)
{
	(void)path;
	return 1;
}

/* (install_layout.c's: the folder of config.toml) */
const char *install_config_folder(void)
{
	return folder;
}

#ifdef HALO_MACOS
/* (the macOS app's: SDL's base path, its settings folder) */
const char *SDL_GetBasePath(void)
{
	return folder;
}
#endif

/* (port_config.c's look for a config.toml it does not read: none here) */
typedef struct SDL_PathInfo SDL_PathInfo;
_Bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	(void)path;
	(void)info;
	return 0;
}

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	fprintf(stderr, "log: ");
	vfprintf(stderr, format, arguments);
	fprintf(stderr, "\n");
	va_end(arguments);
}

int main(int argc, char **argv)
{
	char text[512];

	if (argc < 4)
		return 2;
	snprintf(folder, sizeof(folder), "%s/", argv[1]);
	if (!strcmp(argv[2], "get") && config_text(argv[3], text, sizeof(text)))
	{
		printf("%s\n", text);
		return 0;
	}
	if (!strcmp(argv[2], "write") && argc == 5)
		return config_write(argv[3], argv[4]) ? 0 : 1;
	/* (as the game reads it with battery saver on, by the type its text
	says, then config_text's, which Settings shows) */
	if (!strcmp(argv[2], "saving") && config_text(argv[3], text, sizeof(text)))
	{
		config_battery_saver_set(1);
		if (!strcmp(text, "true") || !strcmp(text, "false"))
			printf("%s", config_boolean(argv[3]) ? "true" : "false");
		else if (text[0] >= '0' && text[0] <= '9')
			printf("%ld", config_integer(argv[3]));
		else
			printf("%s", config_string(argv[3]));
		config_text(argv[3], text, sizeof(text));
		printf(" %s\n", text);
		return 0;
	}
	/* (a local player's value, their profile's folder given ("" none), by
the setting's type) */
	if (!strcmp(argv[2], "player") && argc == 6)
	{
		struct config_setting_info info;
		int index;

		config_player_profile(atoi(argv[3]), argv[4]);
		for (index = 0; config_setting_info(index, &info) && strcmp(info.name, argv[5]); index++)
		{
		}
		if (!info.name || strcmp(info.name, argv[5]))
			return 2;
		if (!strcmp(info.type, "boolean"))
			printf("%s\n", config_player_boolean(atoi(argv[3]), argv[5]) ? "true" : "false");
		else if (!strcmp(info.type, "integer"))
			printf("%ld\n", config_player_integer(atoi(argv[3]), argv[5]));
		else if (!strcmp(info.type, "real"))
			printf("%g\n", config_player_real(atoi(argv[3]), argv[5]));
		else
			printf("%s\n", config_player_string(atoi(argv[3]), argv[5]));
		return 0;
	}
	/* (a weapon's own offsets: a local player's, their profile's folder
given; and set in a profile) */
	if (!strcmp(argv[2], "player-weapon") && argc == 6)
	{
		double values[CONFIG_WEAPON_VALUES];
		int index;

		config_player_profile(atoi(argv[3]), argv[4]);
		config_player_viewmodel_weapon(atoi(argv[3]), argv[5], values);
		for (index = 0; index < CONFIG_WEAPON_VALUES; index++)
			printf("%s%g", index ? " " : "", values[index]);
		printf("\n");
		return 0;
	}
	if (!strcmp(argv[2], "profile-weapon-write") && argc == 5 + CONFIG_WEAPON_VALUES)
	{
		double values[CONFIG_WEAPON_VALUES];
		int index;

		for (index = 0; index < CONFIG_WEAPON_VALUES; index++)
			values[index] = atof(argv[5 + index]);
		return config_profile_viewmodel_weapon_write(argv[3], argv[4], values) ? 0 : 1;
	}
	if (!strcmp(argv[2], "profile-write") && argc == 6)
		return config_profile_write(argv[3], argv[4], argv[5]) ? 0 : 1;
	if (!strcmp(argv[2], "profile-get") && argc == 5)
	{
		if (!config_profile_text(argv[3], argv[4], text, sizeof(text)))
			return 1;
		printf("%s\n", text);
		return 0;
	}
	return 2;
}
