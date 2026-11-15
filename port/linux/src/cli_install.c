/*
CLI_INSTALL.C

`goath1`, the command players and scripts type (`goath1 bots status`),
on every desktop (cli_install.h):

- The Linux release has bin/goath1 beside bin/openh1 (port/assets/linux/goath1,
  tools/linux_package.py), a script that runs the bin/openh1 beside it.
- Windows's has goath1.cmd beside halo.exe (port/assets/windows/goath1.cmd).
- Anywhere, `halo --install-cli [folder]` writes a goath1 script into a
  folder on the PATH that runs this executable: /usr/local/bin when it may
  write there, else ~/.local/bin (made if missing). It is how the macOS app,
  whose executable is inside GOAT H1.app, gets one; on Linux it puts the
  release's on the PATH too. It says where it wrote it, and how to add the
  folder to the PATH when it is not on it. A file of that name it did not
  write is left alone. Windows: it says how to put the game's folder (and
  goath1.cmd) on the PATH.

halo and openh1 keep working as they did: goath1 runs them.
*/

#include "platform.h"
#include "cli_install.h"
#include "posix.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID) && !defined(HALO_IOS)
#include <errno.h>
#include <unistd.h>

/* (the C library's: the game's <sys/stat.h> is MSVC's) */
int mkdir(const char *path, unsigned int mode);
int chmod(const char *path, unsigned int mode);

/* the line every script this writes has: one that has it is ours to replace */
#define CLI_INSTALL_MARK "# goath1: GOAT H1's command line (halo --install-cli)"

int cli_install_write(const char *folder, const char *executable, char *path, size_t path_size, char *why,
	size_t why_size)
{
	char existing[512];
	FILE *file;
	size_t index;

	snprintf(path, path_size, "%s%sgoath1", folder, folder[0] && folder[strlen(folder) - 1] == '/' ? "" : "/");
	/* (a quote in the path would end the script's own) */
	for (index = 0; executable[index]; index++)
	{
		if (executable[index] == '"' || executable[index] == '\\' || executable[index] == '$' ||
			executable[index] == '`')
		{
			snprintf(why, why_size, "the game's path has a character the script cannot hold: %s", executable);
			return 0;
		}
	}
	file = fopen(path, "rb");
	if (file)
	{
		size_t length = fread(existing, 1, sizeof(existing) - 1, file);

		fclose(file);
		existing[length] = 0;
		if (!strstr(existing, CLI_INSTALL_MARK))
		{
			snprintf(why, why_size, "%s is there already, and not GOAT H1's: left as it is", path);
			return 0;
		}
	}
	file = fopen(path, "wb");
	if (!file)
	{
		snprintf(why, why_size, "%s cannot be written (%s)", path, strerror(errno));
		return 0;
	}
	fprintf(file, "#!/bin/sh\n" CLI_INSTALL_MARK "\nexec \"%s\" \"$@\"\n", executable);
	fclose(file);
	if (chmod(path, 0755) != 0)
	{
		snprintf(why, why_size, "%s cannot be made a program (%s)", path, strerror(errno));
		return 0;
	}
	return 1;
}

/* every missing folder along path */
static void cli_install_make_folders(const char *path)
{
	char partial[1024];
	size_t index;

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

/* this executable's path: Linux's /proc/self/exe, else the command line's
program (from the folder it started in, when it is not whole) */
static int cli_install_executable(char *path, size_t size)
{
	char program[1024], folder[1024];
	ssize_t length = readlink("/proc/self/exe", path, size - 1);

	if (length > 0)
	{
		path[length] = 0;
		return 1;
	}
	if (!posix_command_line_argument(0, program, sizeof(program)) || !program[0])
		return 0;
	if (program[0] == '/')
		snprintf(path, size, "%s", program);
	else if (getcwd(folder, sizeof(folder)))
		snprintf(path, size, "%s/%s", folder, program[0] == '.' && program[1] == '/' ? program + 2 : program);
	else
		return 0;
	return access(path, X_OK) == 0;
}

/* the folder is one of the PATH's */
static int cli_install_on_path(const char *folder)
{
	const char *list = getenv("PATH");
	size_t length = strlen(folder);

	while (list && *list)
	{
		const char *end = strchr(list, ':');
		size_t part = end ? (size_t)(end - list) : strlen(list);

		if (part == length && !strncmp(list, folder, length))
			return 1;
		if (part == length + 1 && !strncmp(list, folder, length) && list[length] == '/')
			return 1;
		list = end ? end + 1 : NULL;
	}
	return 0;
}

void cli_install_command_line(int first_argument)
{
	char folder[1024] = "", executable[1024], path[1100], why[1400];
	const char *home = getenv("HOME");

	posix_command_line_argument(first_argument, folder, sizeof(folder));
	if (folder[0] == '-')
		folder[0] = 0;
	if (!cli_install_executable(executable, sizeof(executable)))
	{
		fprintf(stderr, "install-cli: this program's own path is not known: run it by its whole path\n");
		exit(1);
	}
	if (!folder[0])
	{
		if (access("/usr/local/bin", W_OK) == 0)
			snprintf(folder, sizeof(folder), "/usr/local/bin");
		else if (home && home[0] == '/')
			snprintf(folder, sizeof(folder), "%s/.local/bin", home);
		else
		{
			fprintf(stderr, "install-cli: no folder to put goath1 in: give one (halo --install-cli <folder>)\n");
			exit(1);
		}
	}
	cli_install_make_folders(folder);
	if (!cli_install_write(folder, executable, path, sizeof(path), why, sizeof(why)))
	{
		fprintf(stderr, "install-cli: %s\n", why);
		exit(1);
	}
	fprintf(stdout, "goath1 is installed: %s runs %s\n", path, executable);
	if (!cli_install_on_path(folder))
	{
#ifdef __APPLE__
		const char *profile = "~/.zprofile";
#else
		const char *profile = "~/.profile";
#endif
		fprintf(stdout, "%s is not on your PATH yet. Add it with:\n  echo 'export PATH=\"%s:$PATH\"' >> %s\n"
			"then open a new terminal.\n", folder, folder, profile);
	}
	fprintf(stdout, "Try: goath1 bots help\n");
	exit(0);
}

#else

int cli_install_write(const char *folder, const char *executable, char *path, size_t path_size, char *why,
	size_t why_size)
{
	(void)folder;
	(void)executable;
	(void)path;
	(void)path_size;
	snprintf(why, why_size, "not on this platform");
	return 0;
}

void cli_install_command_line(int first_argument)
{
#ifdef HALO_WINDOWS
	char program[1024] = "";
	char *slash;

	(void)first_argument;
	posix_command_line_argument(0, program, sizeof(program));
	slash = strrchr(program, '\\');
	if (slash)
		*slash = 0;
	fprintf(stdout, "goath1.cmd is beside halo.exe%s%s. To type goath1 anywhere, add that folder to your PATH:\n"
		"  Settings > System > About > Advanced system settings > Environment Variables > Path > New\n"
		"then open a new terminal. Try: goath1 bots help\n", slash ? ", in " : "", slash ? program : "");
	exit(0);
#else
	(void)first_argument;
#endif
}

#endif
