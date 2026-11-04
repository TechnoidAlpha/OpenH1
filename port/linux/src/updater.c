/*
UPDATER.C

The desktop ports' self-updater (Linux and Windows; the Android app updates
itself in Java, port/android).

A build of the main branch made by GitHub Actions knows its build number
(HALO_BUILD_NUMBER, the workflow's run number, which names its release:
build-<number>); other builds have none and never look for updates. When
update.auto in config.toml is true (the default), the game asks GitHub for
the latest release when it starts, on a thread of its own: the game starts
meanwhile, and nothing happens if the release is not newer or cannot be
reached. If it is newer, the game asks whether to update:

- Yes: the release's build for this platform and configuration
  (halo-<platform>-<release|debug>.zip) is downloaded into the game's folder
  (update.partial/) and unpacked, its files put in place of the running
  game's (which become <name>.old, listed in update-old.txt and deleted at
  the next start), and the new game started; this one quits.
- No: nothing, until the next start.
- Do not ask again: after the player confirms it, update.auto = false is
  written to config.toml.

Under gamescope (the Steam Deck's Game Mode, where Steam starts the game
that install.sh installed, ~/Applications/OpenH1/bin/openh1), a system
dialog stops the game, so the Linux tree's game asks in its own menus, as
the Steam Frame's does (below): the release's zip is downloaded in the
background while the game plays (update.partial/), and then, at the main
menu, the game asks:

- Update: the zip is unpacked and its files put in place as above, and the
  new game started in this process (update_restart: the same process, of
  the same parent, so that Steam, which waits for it, and gamescope, which
  shows its window, see a game that goes on, never one that quit or
  crashed).
- Not now: nothing, until the next start (which deletes the download).

The system side (the HTTPS download, the files, starting the new game) is
update.h's: posix_update.c on Linux, win32_update.c on Windows.

Every download is checked before it is unpacked (updater_verify): against
the release's SHA256SUMS and that file's signature by OpenH1's release key,
SHA256SUMS.sig (release_signature.h says what is refused: a signature that
is not the key's always; a release with none once
RELEASE_SIGNATURE_REQUIRED is 1).

On Linux the release is a tree (bin/openh1, lib/openh1, share/...:
install_layout.c, tools/linux_package.py), OpenH1-Linux[-debug].zip or
OpenH1-Linux-ARM64[-debug].zip (tools/release_names.py), and the game's folder
is its root. A game a package manager installed, or one in a folder the
player cannot write to, never looks for updates: its package manager
updates it; nor does a game outside a tree (a build's folder).

The Steam Frame's VR build (UPDATER_PACKAGE) is a package of its own in
~/apps/halo-vr (tools/frame_package.py), which Steam starts through its
launcher, halo-vr.sh. Started by it (HALO_FRAME_LAUNCHER), the game looks for
the latest release's OpenH1-Steam-Frame.zip as the others do, then
downloads and unpacks it in the background (update.partial/halo-vr-frame),
while the game plays. Once it is there, the game asks in its own menus, at
the main menu (a system dialog would show nowhere in the headset, and
stops the game under gamescope, where the Frame runs it):

- Update: update-ready is written beside the game, and the game quits; the
  launcher sees it, installs the package with the package's own install.sh
  (tools/linux_install.sh: its SHA256SUMS checked, each file put in place
  by a rename, the player's files untouched), and starts the new game.
- Not now: nothing, until the next start (the launcher deletes the
  download).

Settings > Network Setup > Check for Updates (update.auto) stops it
looking.

A dedicated server (halo --server) updates itself as OpenCE's does,
through updater_server_* (server.auto_update: port/linux/game/
server_director.c, which decides when): the latest release looked for on a
thread of its own, and its zip downloaded, unpacked and put in place of the
running server's files on another (as the desktop game's update does, but
with no window and nothing started: the server stops with exit code 5, for
what runs it to start the new build). The release's notes give its network
versions ("Network version: <n>", "Standard network version: <n>": the
release job of .github/workflows/build.yml writes them, tools/ci_build.py
notes), which say whether players of this build can join the new one. Only
the builds that update themselves do: the Linux release's tree, and the
Windows build's folder; not the Steam Frame's package (its launcher
updates it), nor an installed game, nor a build without a number (but for a
test's release server, debug.update_url).
*/

#include "platform.h"
#include "install_layout.h"
#include "port_config.h"
#include "update.h"

/* the release's build that this one updates to (UPDATE_NAME[-debug].zip,
as tools/release_names.py names the release's files; on Linux, the release's
tree); a build without one never looks for updates */
#if defined(HALO_ANDROID) || defined(HALO_MACOS)
/* (none: the Android app updates itself, the site through its service
worker; the macOS app through Sparkle, in its host: updater_poll below) */
#elif defined(HALO_WINDOWS)
/* (the Windows build: the guest of port/windows/host) */
#define UPDATE_PLATFORM "windows"
#define UPDATE_NAME "OpenH1-Windows"
#elif defined(HALO_VR) && defined(__x86_64__)
/* the 64-bit x86 VR build, which releases have beside the other one
(tools/ci_build.py --vr): it updates to itself, never to the other build */
#define UPDATE_PLATFORM "linux-x86_64-vr"
#define UPDATE_NAME "OpenH1-Linux-VR"
#elif defined(HALO_VR)
/* the 64-bit ARM VR build, the Steam Frame's: a package of its own
(tools/frame_package.py: OpenH1-Steam-Frame.zip, the game beside its
launcher and checksums, not a tree), which it updates to as the package's
install.sh installs it (UPDATER_PACKAGE, below), never to the desktop
build */
#define UPDATE_PLATFORM "steam-frame-vr"
#define UPDATE_NAME "OpenH1-Steam-Frame"
#define UPDATER_PACKAGE "halo-vr-frame"
#elif defined(__aarch64__)
/* the 64-bit ARM build (port/linux/host) */
#define UPDATE_PLATFORM "linux-arm64"
#define UPDATE_NAME "OpenH1-Linux-ARM64"
#elif defined(__x86_64__)
/* the 64-bit x86 build (port/linux/host, as the 64-bit ARM one) */
#define UPDATE_PLATFORM "linux-x86_64"
#define UPDATE_NAME "OpenH1-Linux"
#endif

#ifdef UPDATE_PLATFORM

#include "p2p_internal.h"
#include "release_signature.h"
#include "zlib_prefixed.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (given for this file by the build: tools/android_build.py's
generate_guest_image, windows_build.py) */
#ifndef HALO_BUILD_NUMBER
#define HALO_BUILD_NUMBER 0
#endif
#ifndef HALO_BUILD_FLAVOR
#define HALO_BUILD_FLAVOR "release"
#endif

#define UPDATE_REPOSITORY "OpenH1/OpenH1"
#ifdef HALO_WINDOWS
#define PATH_SEPARATOR "\\"
#else
#define PATH_SEPARATOR "/"
#endif
/* the release's file (a debug build's has -debug after the name) */
#define UPDATE_ASSET_RELEASE UPDATE_NAME ".zip"
#define UPDATE_ASSET_DEBUG UPDATE_NAME "-debug.zip"
#define UPDATE_ASSET (strcmp(HALO_BUILD_FLAVOR, "release") ? UPDATE_ASSET_DEBUG : UPDATE_ASSET_RELEASE)
#define UPDATE_DIRECTORY "update.partial"
/* GitHub's addresses of the latest release and a release's files; a test's
release server (debug.update_url) has them under its own address */
#define UPDATE_API "https://api.github.com"
#define UPDATE_DOWNLOADS "https://github.com"
#define UPDATE_LATEST_PATH "/repos/" UPDATE_REPOSITORY "/releases/latest"
#define UPDATE_RELEASE_PATH "/" UPDATE_REPOSITORY "/releases/download"
/* the files an update replaced (<name>.old), one on each line, which the new
version deletes */
#define UPDATE_OLD_LIST "update-old.txt"
#define MAXIMUM_UPDATE_FILES 64
/* the most a download may be: GitHub's answer about the latest release, a
release's zip (about 30 MB) */
#define MAXIMUM_CHECK_SIZE (1024ULL * 1024)
#define MAXIMUM_UPDATE_SIZE (256ULL * 1024 * 1024)

enum
{
	_updater_idle,
	_updater_checking,
	_updater_available,
	/* (the question asked in the game's menus: updater_update_now's) */
	_updater_asked,
	_updater_handled,
};

/* menu_functions.c's: the update question opened at the main menu, once it
has been seen; 1 when it was, or cannot be */
int pc_menu_update_question_open(void);
/* sdl_platform.c's */
void platform_request_quit(void);
void updater_update_now(void);

static SDL_AtomicInt updater_state;
static long updater_latest_build;
/* the network versions the latest release's notes give ("Network version:
<n>", "Standard network version: <n>"), 0 for one they do not (a release
made before they did) */
static long updater_latest_network_version;
static long updater_latest_standard_version;
static char updater_directory[1024];
static char updater_executable[1024];
/* the update's folder in the game's folder, and the check's file: a
dedicated server's have its port in their names (updater_server_ready), as
a computer's servers may run from one tree */
static char updater_partial[64] = UPDATE_DIRECTORY;
static char updater_check_name[64] = "update-check.json";
#ifndef UPDATER_PACKAGE
/* (the Linux tree's, under gamescope: asking in the game's menus, the
release's zip downloaded in the background) */
static int updater_in_game;
static int updater_downloaded;
#endif

/* ---------- paths */

/* (a name's parts are separated by '/', as in the zip) */
static void updater_local_separators(char *path, size_t from)
{
	for (; path[from]; from++)
	{
		if (path[from] == '/')
			path[from] = PATH_SEPARATOR[0];
	}
}

static void updater_path(char *path, size_t size, const char *name)
{
	snprintf(path, size, "%s" PATH_SEPARATOR "%s", updater_directory, name);
	updater_local_separators(path, strlen(updater_directory));
}

static void updater_partial_path(char *path, size_t size, const char *name)
{
	snprintf(path, size, "%s" PATH_SEPARATOR "%s" PATH_SEPARATOR "%s", updater_directory, updater_partial, name);
	updater_local_separators(path, strlen(updater_directory));
}

/* the folders a file at path is in, from the character at from on (those
before are there already); 1 when they all are */
static int updater_make_folders(const char *path, size_t from)
{
	char folder[1200];
	size_t index;

	snprintf(folder, sizeof(folder), "%s", path);
	for (index = from; folder[index]; index++)
	{
		if (folder[index] == PATH_SEPARATOR[0] && index > from)
		{
			folder[index] = 0;
			if (!update_make_directory(folder))
				return 0;
			folder[index] = PATH_SEPARATOR[0];
		}
	}
	return 1;
}

/* whether a name in the zip is a file inside the game's folder: relative,
its parts separated by '/', none of them empty, "." or ".." (nor a drive's
or a backslash) */
static int updater_name_inside(const char *name)
{
	const char *part = name;

	if (!*name || strchr(name, '\\') || strchr(name, ':'))
		return 0;
	for (;;)
	{
		size_t length = strcspn(part, "/");

		if (!length || (length == 1 && part[0] == '.') || (length == 2 && part[0] == '.' && part[1] == '.'))
			return 0;
		if (!part[length])
			return 1;
		part += length + 1;
	}
}

/* ---------- the zip file (the release's), stored or deflated entries */

static unsigned long zip_word(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8;
}

static unsigned long zip_long(const unsigned char *bytes)
{
	return zip_word(bytes) | zip_word(bytes + 2) << 16;
}

/* an entry's data, unpacked, to the file at path; why it could not be is
written to reason */
static int zip_extract_entry(SDL_IOStream *zip, unsigned long local_offset, int method,
	unsigned long packed_size, unsigned long size, unsigned long crc, const char *path,
	char *reason, size_t reason_size)
{
	unsigned char header[30];
	unsigned char input[16384], output[16384];
	SDL_IOStream *file;
	z_stream stream;
	unsigned long remaining = packed_size, written = 0, checksum = crc32(0L, Z_NULL, 0);
	int succeeded = 0, ended = 0;

	if (SDL_SeekIO(zip, (Sint64)local_offset, SDL_IO_SEEK_SET) < 0 ||
		SDL_ReadIO(zip, header, sizeof(header)) != sizeof(header) || zip_long(header) != 0x04034b50 ||
		SDL_SeekIO(zip, (Sint64)(zip_word(header + 26) + zip_word(header + 28)), SDL_IO_SEEK_CUR) < 0)
	{
		snprintf(reason, reason_size, "its header at %lu could not be read (%s)", local_offset, SDL_GetError());
		return 0;
	}
	file = SDL_IOFromFile(path, "wb");
	if (!file)
	{
		snprintf(reason, reason_size, "could not create %s (%s)", path, SDL_GetError());
		return 0;
	}
	memset(&stream, 0, sizeof(stream));
	if (method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK)
	{
		snprintf(reason, reason_size, "zlib would not start (%s)", stream.msg ? stream.msg : "no message");
		SDL_CloseIO(file);
		return 0;
	}
	for (;;)
	{
		size_t count = remaining < sizeof(input) ? remaining : sizeof(input);

		if (count && SDL_ReadIO(zip, input, count) != count)
		{
			snprintf(reason, reason_size, "the download ended %lu bytes early (%s)", remaining, SDL_GetError());
			break;
		}
		remaining -= (unsigned long)count;
		if (method == 0)
		{
			/* (no more than the entry says it holds) */
			if (count > size - written)
			{
				snprintf(reason, reason_size, "it unpacks to more than %lu bytes", size);
				break;
			}
			if (SDL_WriteIO(file, input, count) != count)
			{
				snprintf(reason, reason_size, "could not write %s after %lu bytes (%s)", path, written, SDL_GetError());
				break;
			}
			checksum = crc32(checksum, input, (uInt)count);
			written += (unsigned long)count;
		}
		else
		{
			int result = Z_OK;

			stream.next_in = input;
			stream.avail_in = (uInt)count;
			do
			{
				size_t produced;

				stream.next_out = output;
				stream.avail_out = sizeof(output);
				result = inflate(&stream, Z_NO_FLUSH);
				if (result != Z_OK && result != Z_STREAM_END)
					break;
				produced = sizeof(output) - stream.avail_out;
				/* (no more than the entry says, before it is on the disk) */
				if (written + (unsigned long)produced > size)
				{
					snprintf(reason, reason_size, "it unpacks to more than %lu bytes", size);
					result = Z_ERRNO;
					break;
				}
				if (SDL_WriteIO(file, output, produced) != produced)
				{
					snprintf(reason, reason_size, "could not write %s after %lu bytes (%s)", path, written, SDL_GetError());
					result = Z_ERRNO;
					break;
				}
				checksum = crc32(checksum, output, (uInt)produced);
				written += (unsigned long)produced;
			} while (stream.avail_out == 0 && result != Z_STREAM_END);
			if (result == Z_STREAM_END)
				ended = 1;
			else if (result != Z_OK && result != Z_BUF_ERROR)
			{
				if (result != Z_ERRNO)
				{
					snprintf(reason, reason_size, "zlib stopped with %d after %lu of %lu bytes (%s)", result, written, size,
						stream.msg ? stream.msg : "no message");
				}
				break;
			}
		}
		/* (all of the input unpacked is enough, as the size and the CRC are
		checked: the game's zlib 1.1, used here before, could want a byte past
		the end of a raw stream before it said the stream had ended) */
		if (ended || !remaining)
		{
			if (written != size)
				snprintf(reason, reason_size, "it unpacked to %lu bytes, not %lu", written, size);
			else if (checksum != crc)
				snprintf(reason, reason_size, "its CRC is %08lx, not %08lx", checksum, crc);
			else
				succeeded = 1;
			break;
		}
	}
	if (method == 8)
		inflateEnd(&stream);
	if (!SDL_CloseIO(file) && succeeded)
	{
		snprintf(reason, reason_size, "could not finish writing %s (%s)", path, SDL_GetError());
		succeeded = 0;
	}
	return succeeded;
}

/* the zip's files (the Linux release's tree, or the Windows release's folder) into
update.partial/, their names in names */
static int zip_extract(SDL_IOStream *zip, char names[][256], int *name_count, char *error, size_t error_size)
{
	unsigned char tail[65536 + 22];
	Sint64 size;
	size_t tail_size, index;
	unsigned long entries = 0, directory_offset = 0, entry;
	int found = 0, succeeded = 0;

	*name_count = 0;
	/* the end of the central directory, in the last 64 KB */
	size = SDL_GetIOSize(zip);
	tail_size = size < (Sint64)sizeof(tail) ? (size_t)size : sizeof(tail);
	if (size < 22 || SDL_SeekIO(zip, size - (Sint64)tail_size, SDL_IO_SEEK_SET) < 0 ||
		SDL_ReadIO(zip, tail, tail_size) != tail_size)
	{
		goto done;
	}
	for (index = tail_size - 22 + 1; index-- > 0;)
	{
		if (zip_long(tail + index) == 0x06054b50)
		{
			entries = zip_word(tail + index + 10);
			directory_offset = zip_long(tail + index + 16);
			found = 1;
			break;
		}
	}
	if (!found || SDL_SeekIO(zip, (Sint64)directory_offset, SDL_IO_SEEK_SET) < 0)
		goto done;
	for (entry = 0; entry < entries; entry++)
	{
		unsigned char header[46];
		char name[256];
		unsigned long name_length, extra_length, comment_length;
		Sint64 next;
		char path[1200];
		char reason[512] = "";

		if (SDL_ReadIO(zip, header, sizeof(header)) != sizeof(header) || zip_long(header) != 0x02014b50)
			goto done;
		name_length = zip_word(header + 28);
		extra_length = zip_word(header + 30);
		comment_length = zip_word(header + 32);
		if (name_length >= sizeof(name) || SDL_ReadIO(zip, name, name_length) != name_length)
			goto done;
		name[name_length] = 0;
		next = SDL_TellIO(zip) + (Sint64)(extra_length + comment_length);
		/* (a folder, a link, or a name that would leave the game's folder,
		is left out; a name with a NUL in it is no zip of a release's) */
		if (memchr(name, 0, name_length))
			goto done;
		if (name_length && name[name_length - 1] != '/' && updater_name_inside(name) &&
			!(header[5] == 3 && ((zip_long(header + 38) >> 16) & 0170000) == 0120000))
		{
			int method = (int)zip_word(header + 10);

			if (*name_count >= MAXIMUM_UPDATE_FILES)
			{
				snprintf(error, error_size, "the download has more than %d files", MAXIMUM_UPDATE_FILES);
				goto done;
			}
			if (method != 0 && method != 8)
			{
				snprintf(error, error_size, "the download packs %s in a way this build cannot read", name);
				goto done;
			}
			updater_partial_path(path, sizeof(path), name);
			if (!updater_make_folders(path, strlen(updater_directory) + 1) ||
				!zip_extract_entry(zip, zip_long(header + 42), method, zip_long(header + 20), zip_long(header + 24),
				zip_long(header + 16), path, reason, sizeof(reason)))
			{
				snprintf(error, error_size, "could not unpack %s: %s", name, reason[0] ? reason : "no folder for it");
				goto done;
			}
			/* (executable on Unix, as the zip made there says: bin/openh1) */
			if (header[5] == 3 && ((zip_long(header + 38) >> 16) & 0111) && !update_make_executable(path))
			{
				snprintf(error, error_size, "could not make %s executable", name);
				goto done;
			}
			snprintf(names[*name_count], 256, "%s", name);
			(*name_count)++;
		}
		if (SDL_SeekIO(zip, next, SDL_IO_SEEK_SET) < 0)
			goto done;
	}
	succeeded = *name_count > 0;

done:
	if (!succeeded && !error[0])
		snprintf(error, error_size, "the download is not a zip file this build can read");
	return succeeded;
}

/* ---------- checking */

/* a release's file, the asset of build */
static void updater_asset_url(char *url, size_t size, long build, const char *asset)
{
	const char *test = config_string("debug.update_url");

	snprintf(url, size, "%s" UPDATE_RELEASE_PATH "/build-%ld/%s", test[0] ? test : UPDATE_DOWNLOADS, build, asset);
}

/* url's file (GitHub's, over HTTPS; a test's release server's, debug.update_url,
plain http:// on this machine, which update_https_request alone fetches)
at path, no larger than maximum; 1 when it is there, else 0 and why */
static int updater_fetch(const char *url, const char *path, unsigned long long maximum, update_progress_proc progress,
	void *context, char *error, size_t error_size)
{
	struct update_response response;
	SDL_IOStream *file = NULL;
	int succeeded = 0;

	if (strncmp(url, "http://", 7))
		return update_download(url, path, maximum, progress, context, error, (int)error_size);
	if (!update_https_request("GET", url, NULL, NULL, 0, 16UL * 1024 * 1024, &response, error, (int)error_size))
		return 0;
	if (response.status != 200)
	{
		snprintf(error, error_size, "the server answered %d", response.status);
	}
	else if ((unsigned long long)response.body_size > maximum || !(file = SDL_IOFromFile(path, "wb")))
	{
		snprintf(error, error_size, "could not write %s", path);
	}
	else
	{
		succeeded = SDL_WriteIO(file, response.body, response.body_size) == response.body_size;
		if (!SDL_CloseIO(file))
			succeeded = 0;
		if (!succeeded)
			snprintf(error, error_size, "could not write %s", path);
	}
	free(response.body);
	return succeeded;
}

/* the number after the last key in text (a release's notes in GitHub's
answer), 0 if there is none */
static long updater_notes_number(const char *text, const char *key)
{
	const char *found = NULL, *next = text;

	while ((next = strstr(next, key)) != NULL)
		found = next++;
	return found ? strtol(found + strlen(key), NULL, 10) : 0;
}

/* the network versions the notes of the release updater_latest_release
found give, for updater_check_thread to keep with its build */
static long updater_release_network_version;
static long updater_release_standard_version;

/* the build number of GitHub's latest release, 0 if there is none */
static long updater_latest_release(void)
{
	char path[1200], url[512];
	char error[512] = "";
	size_t size = 0;
	char *text;
	const char *tag;
	const char *test = config_string("debug.update_url");
	long build = 0;

	updater_path(path, sizeof(path), updater_check_name);
	snprintf(url, sizeof(url), "%s" UPDATE_LATEST_PATH, test[0] ? test : UPDATE_API);
	if (!updater_fetch(url, path, MAXIMUM_CHECK_SIZE, NULL, NULL, error, sizeof(error)))
	{
		platform_log("update: could not check for a new version: %s", error);
		return 0;
	}
	text = SDL_LoadFile(path, &size);
	update_delete_file(path);
	if (!text)
		return 0;
	/* "tag_name": "build-<number>" */
	tag = strstr(text, "\"tag_name\"");
	if (tag)
	{
		tag = strchr(tag + 10, '"');
		if (tag && !strncmp(tag, "\"build-", 7))
			build = strtol(tag + 7, NULL, 10);
	}
	/* (the notes' lines, which the release job writes: the last of each,
	after the commit's subject) */
	updater_release_network_version = updater_notes_number(text, "Network version: ");
	updater_release_standard_version = updater_notes_number(text, "Standard network version: ");
	SDL_free(text);
	return build;
}

/* ---------- the release's signature (release_signature.c) */

/* the latest release's file (SHA256SUMS or SHA256SUMS.sig) into memory
(SDL_free'd by the caller): 1 when it is there, 0 when the release has none
(GitHub answers 404), -1 when it could not be had (why in error) */
static int updater_release_file(const char *name, int maximum, char **text, int *size, char *error,
	size_t error_size)
{
	struct update_response response;
	char url[512], path[1200];
	size_t loaded = 0;

	*text = NULL;
	*size = 0;
	updater_asset_url(url, sizeof(url), updater_latest_build, name);
	/* (whether there is one: GitHub redirects a release's file to its
	storage, which update_https_request does not follow) */
	memset(&response, 0, sizeof(response));
	if (!update_https_request("GET", url, NULL, NULL, 0, (unsigned long)maximum, &response, error, (int)error_size))
		return -1;
	free(response.body);
	if (response.status == 404)
		return 0;
	if (response.status != 200 && (response.status < 300 || response.status > 399))
	{
		snprintf(error, error_size, "GitHub answered %d for %s", response.status, name);
		return -1;
	}
	/* (fetched as the release's zip is: a test's release server,
	debug.update_url, answers with the file itself) */
	updater_partial_path(path, sizeof(path), name);
	if (!updater_fetch(url, path, (unsigned long long)maximum, NULL, NULL, error, error_size))
	{
		update_delete_file(path);
		return -1;
	}
	*text = SDL_LoadFile(path, &loaded);
	update_delete_file(path);
	if (!*text || loaded > (size_t)maximum)
	{
		SDL_free(*text);
		*text = NULL;
		snprintf(error, error_size, "could not read %s", name);
		return -1;
	}
	*size = (int)loaded;
	return 1;
}

/* the SHA-256 of the file at path; 1 when it was read whole */
static int updater_file_digest(const char *path, unsigned char *digest)
{
	SDL_IOStream *stream = SDL_IOFromFile(path, "rb");
	Sint64 size = stream ? SDL_GetIOSize(stream) : -1;
	Sint64 total = 0;
	struct p2p_sha256 context;
	unsigned char buffer[8192];
	size_t count;

	if (!stream)
		return 0;
	p2p_sha256_begin(&context);
	while ((count = SDL_ReadIO(stream, buffer, sizeof(buffer))) > 0)
	{
		p2p_sha256_add(&context, buffer, (int)count);
		total += (Sint64)count;
	}
	/* (all of it read: a guest's SDL bridges no SDL_GetIOStatus) */
	if (size < 0 || total != size)
	{
		SDL_CloseIO(stream);
		return 0;
	}
	SDL_CloseIO(stream);
	p2p_sha256_end(&context, digest);
	return 1;
}

/* the release's download (zip_path, UPDATE_ASSET) checked against its
SHA256SUMS and their signature by the release key (release_signature.h:
what is refused, and RELEASE_SIGNATURE_REQUIRED); 1 when it may be
installed, else 0, the download deleted and why in error */
static int updater_verify(const char *zip_path, char *error, size_t error_size)
{
	unsigned char digest[RELEASE_DIGEST_SIZE];
	char *sums = NULL, *signature = NULL;
	int sums_size = 0, signature_size = 0, has_sums, has_signature;
	char reason[300] = "";
	enum release_verdict verdict = _release_refused;

	has_sums = updater_release_file("SHA256SUMS", RELEASE_SUMS_MAXIMUM_SIZE, &sums, &sums_size, error, error_size);
	has_signature = has_sums < 0 ? -1 :
		updater_release_file("SHA256SUMS.sig", RELEASE_SIGNATURE_MAXIMUM_SIZE, &signature, &signature_size, error,
			error_size);
	if (has_sums < 0 || has_signature < 0)
	{
		/* (one that is there but could not be had is never taken for one
		that is not) */
		snprintf(reason, sizeof(reason), "%s", error);
		snprintf(error, error_size, "could not check build %ld's signature: %s", updater_latest_build, reason);
	}
	else if (!updater_file_digest(zip_path, digest))
	{
		snprintf(error, error_size, "could not read the download to check it");
	}
	else
	{
		/* (a test's key, debug.update_key, is trusted only from a test's
		release server, debug.update_url: never for GitHub's releases) */
		const char *test_key = config_string("debug.update_url")[0] ? config_string("debug.update_key") : NULL;

		verdict = release_check(has_sums ? sums : NULL, sums_size, has_signature ? signature : NULL, signature_size,
			UPDATE_ASSET, digest, RELEASE_SIGNATURE_REQUIRED, test_key, reason, sizeof(reason));
		if (verdict == _release_refused)
			snprintf(error, error_size, "build %ld was refused: %s", updater_latest_build, reason);
		else if (verdict == _release_unsigned)
			platform_log("update: build %ld: %s", updater_latest_build, reason);
		else
			platform_log("update: build %ld is signed by OpenH1's release key, and its download checks",
				updater_latest_build);
	}
	SDL_free(sums);
	SDL_free(signature);
	if (verdict == _release_refused)
		update_delete_file(zip_path);
	return verdict != _release_refused;
}

#ifndef UPDATER_PACKAGE
/* whether the game runs under gamescope (the Steam Deck's Game Mode), where
a system dialog such as SDL's message box crashes the game. gamescope sets
GAMESCOPE_WAYLAND_DISPLAY for the programs it runs and Game Mode's session
XDG_CURRENT_DESKTOP; Steam sets SteamDeck and SteamGamepadUI there. Wine
passes them on to the Windows build under Proton. */
static int updater_gamescope(void)
{
	const char *wayland_display = getenv("GAMESCOPE_WAYLAND_DISPLAY");
	const char *desktop = getenv("XDG_CURRENT_DESKTOP");
	const char *steam_deck = getenv("SteamDeck");
	const char *gamepad_ui = getenv("SteamGamepadUI");

	return (wayland_display && wayland_display[0]) || (desktop && strstr(desktop, "gamescope")) ||
		(steam_deck && !strcmp(steam_deck, "1") && gamepad_ui && !strcmp(gamepad_ui, "1"));
}

/* under gamescope, the release's zip downloaded in the background, while
the game plays (with no window of its own, as updater_download_zip's would
stand over the game there), into update.partial/, where updater_update
unpacks it once the player chose to update; 1 when it is there */
static int updater_download_background(void)
{
	char partial[1200], zip_path[1200], url[512], error[512] = "";

	updater_path(partial, sizeof(partial), updater_partial);
	if (!update_make_directory(partial))
	{
		platform_log("update: could not make %s (is the game's folder read-only?)", partial);
		return 0;
	}
	updater_partial_path(zip_path, sizeof(zip_path), UPDATE_ASSET);
	updater_asset_url(url, sizeof(url), updater_latest_build, UPDATE_ASSET);
	platform_log("update: downloading build %ld (%s) in the background", updater_latest_build, UPDATE_ASSET);
	if (!updater_fetch(url, zip_path, MAXIMUM_UPDATE_SIZE, NULL, NULL, error, sizeof(error)))
	{
		platform_log("update: could not download build %ld: %s", updater_latest_build, error);
		update_delete_file(zip_path);
		update_delete_file(partial);
		return 0;
	}
	if (!updater_verify(zip_path, error, sizeof(error)))
	{
		platform_log("update: %s", error);
		update_delete_file(partial);
		return 0;
	}
	platform_log("update: build %ld is downloaded", updater_latest_build);
	return 1;
}
#else
static int updater_unpack(const char *zip_path, int downloaded, char names[][256], int *name_count, char *error,
	size_t error_size);

/* the Frame's: the release's package downloaded (or debug.update_from's zip)
and unpacked into update.partial/, where the launcher's install takes it
(update.partial/halo-vr-frame, with its install.sh, SHA256SUMS and
launcher); 1 when it is there */
static int updater_prepare_package(void)
{
	static char names[MAXIMUM_UPDATE_FILES][256];
	static const char *const needed[] = { "install.sh", "SHA256SUMS", "halo-vr.sh", "halo" };
	char partial[1200], zip_path[1200], url[512], error[512] = "";
	const char *local_zip = config_string("debug.update_from");
	int name_count = 0;
	size_t index;

	updater_path(partial, sizeof(partial), updater_partial);
	if (!update_make_directory(partial))
	{
		platform_log("update: could not make %s (is the game's folder read-only?)", partial);
		return 0;
	}
	if (local_zip[0])
	{
		snprintf(zip_path, sizeof(zip_path), "%s", local_zip);
	}
	else
	{
		updater_partial_path(zip_path, sizeof(zip_path), UPDATE_ASSET);
		updater_asset_url(url, sizeof(url), updater_latest_build, UPDATE_ASSET);
		platform_log("update: downloading build %ld (%s)", updater_latest_build, UPDATE_ASSET);
		if (!updater_fetch(url, zip_path, MAXIMUM_UPDATE_SIZE, NULL, NULL, error, sizeof(error)))
		{
			platform_log("update: could not download build %ld: %s", updater_latest_build, error);
			update_delete_file(zip_path);
			return 0;
		}
		if (!updater_verify(zip_path, error, sizeof(error)))
		{
			platform_log("update: %s", error);
			return 0;
		}
	}
	if (!updater_unpack(zip_path, !local_zip[0], names, &name_count, error, sizeof(error)))
	{
		platform_log("update: could not unpack build %ld: %s", updater_latest_build, error);
		return 0;
	}
	for (index = 0; index < sizeof(needed) / sizeof(needed[0]); index++)
	{
		char path[1200];

		updater_partial_path(path, sizeof(path), UPDATER_PACKAGE "/");
		snprintf(path + strlen(path), sizeof(path) - strlen(path), "%s", needed[index]);
		if (!SDL_GetPathInfo(path, NULL))
		{
			platform_log("update: build %ld's download is not the Steam Frame's package (no " UPDATER_PACKAGE "/%s)",
				updater_latest_build, needed[index]);
			return 0;
		}
	}
	platform_log("update: build %ld is ready to install (%d files in " UPDATE_DIRECTORY "/" UPDATER_PACKAGE ")",
		updater_latest_build, name_count);
	return 1;
}
#endif

static int SDLCALL updater_check_thread(void *context)
{
	/* (a test's zip, debug.update_from, is always newer) */
	long latest = config_string("debug.update_from")[0] ? HALO_BUILD_NUMBER + 1 : updater_latest_release();

	(void)context;
	if (latest > HALO_BUILD_NUMBER)
	{
		platform_log("update: build %ld is available (this is build %d)", latest, HALO_BUILD_NUMBER);
		updater_latest_build = latest;
		updater_latest_network_version = updater_release_network_version;
		updater_latest_standard_version = updater_release_standard_version;
#ifdef UPDATER_PACKAGE
		/* (the Frame's: asked about once it is ready to install) */
		if (!updater_prepare_package())
		{
			SDL_SetAtomicInt(&updater_state, _updater_handled);
			return 0;
		}
#else
		/* (under gamescope: asked about once it is downloaded) */
		if (updater_in_game && !config_string("debug.update_from")[0])
		{
			if (!updater_download_background())
			{
				SDL_SetAtomicInt(&updater_state, _updater_handled);
				return 0;
			}
			updater_downloaded = 1;
		}
#endif
		SDL_SetAtomicInt(&updater_state, _updater_available);
	}
	else
	{
		if (latest)
			platform_log("update: this is the latest build (%d)", HALO_BUILD_NUMBER);
		SDL_SetAtomicInt(&updater_state, _updater_handled);
	}
	return 0;
}

/* ---------- updating */

/* (the release's files put in place by the game, but the Frame's, which its
launcher installs) */
#ifndef UPDATER_PACKAGE
struct updater_download
{
	SDL_Mutex *lock;
	char url[512];
	char zip_path[1200];
	unsigned long long received, total;
	int finished, succeeded;
	char error[512];
};

static void updater_download_progress(void *context, unsigned long long received, unsigned long long total)
{
	struct updater_download *download = context;

	SDL_LockMutex(download->lock);
	download->received = received;
	download->total = total;
	SDL_UnlockMutex(download->lock);
}

static int SDLCALL updater_download_thread(void *context)
{
	struct updater_download *download = context;
	char error[512] = "";
	int succeeded = updater_fetch(download->url, download->zip_path, MAXIMUM_UPDATE_SIZE, updater_download_progress,
		download, error, sizeof(error));

	SDL_LockMutex(download->lock);
	download->succeeded = succeeded;
	snprintf(download->error, sizeof(download->error), "%s", error);
	download->finished = 1;
	SDL_UnlockMutex(download->lock);
	return 0;
}

/* downloads the release's zip, showing how far it has got in a window of its
own (drawn in software, clear of the game's renderer); 1 when it is there */
static int updater_download_zip(const char *zip_path, char *error, size_t error_size)
{
	static struct updater_download download;
	SDL_Window *window;
	SDL_Renderer *renderer = NULL;
	SDL_Thread *thread;
	int finished = 0;

	memset(&download, 0, sizeof(download));
	download.lock = SDL_CreateMutex();
	updater_asset_url(download.url, sizeof(download.url), updater_latest_build, UPDATE_ASSET);
	snprintf(download.zip_path, sizeof(download.zip_path), "%s", zip_path);
	thread = SDL_CreateThread(updater_download_thread, "update download", &download);
	if (!thread)
	{
		SDL_DestroyMutex(download.lock);
		snprintf(error, error_size, "could not start the download");
		return 0;
	}
	window = SDL_CreateWindow("Halo", 640, 150, 0);
	if (window)
		renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
	while (!finished)
	{
		SDL_Event event;
		unsigned long long received, total;

		/* (the game's own events wait: its window is not drawn meanwhile) */
		while (SDL_PollEvent(&event))
		{
		}
		SDL_LockMutex(download.lock);
		finished = download.finished;
		received = download.received;
		total = download.total;
		SDL_UnlockMutex(download.lock);
		if (renderer)
		{
			char line[160];
			SDL_FRect bar = { 20.0f, 100.0f, 600.0f, 24.0f };
			float fraction = total ? (float)((double)received / (double)total) : 0.0f;

			SDL_SetRenderDrawColor(renderer, 12, 16, 20, 255);
			SDL_RenderClear(renderer);
			SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
			SDL_SetRenderScale(renderer, 2.0f, 2.0f);
			SDL_RenderDebugText(renderer, 10.0f, 10.0f, "Downloading the new version...");
			SDL_SetRenderScale(renderer, 1.0f, 1.0f);
			snprintf(line, sizeof(line), "build %ld  (%llu of %llu MB)", updater_latest_build, received >> 20,
				total >> 20);
			SDL_RenderDebugText(renderer, 20.0f, 70.0f, line);
			SDL_SetRenderDrawColor(renderer, 60, 66, 72, 255);
			SDL_RenderFillRect(renderer, &bar);
			bar.w *= fraction;
			SDL_SetRenderDrawColor(renderer, 90, 160, 90, 255);
			SDL_RenderFillRect(renderer, &bar);
			SDL_RenderPresent(renderer);
		}
		SDL_Delay(16);
	}
	SDL_WaitThread(thread, NULL);
	if (renderer)
		SDL_DestroyRenderer(renderer);
	if (window)
		SDL_DestroyWindow(window);
	SDL_DestroyMutex(download.lock);
	if (!download.succeeded)
		snprintf(error, error_size, "%s", download.error);
	return download.succeeded;
}

/* a file replaced, <name>.old, into update-old.txt */
static void updater_list_old(SDL_IOStream *old_list, const char *name)
{
	char line[300];
	int length = snprintf(line, sizeof(line), "%s.old\n", name);

	if (old_list && length > 0 && length < (int)sizeof(line))
		SDL_WriteIO(old_list, line, (size_t)length);
}

/* update.partial/, once its files are in place: its folders, the deepest
first */
static void updater_delete_partial(const char *partial, char names[][256], int name_count)
{
	int index;

	for (index = 0; index < name_count; index++)
	{
		char name[256];
		char *slash;

		snprintf(name, sizeof(name), "%s", names[index]);
		while ((slash = strrchr(name, '/')) != NULL)
		{
			char path[1200];

			*slash = 0;
			updater_partial_path(path, sizeof(path), name);
			update_delete_file(path);
		}
	}
	update_delete_file(partial);
}

#endif

/* the zip, no larger than a release's can be, unpacked; a download is
deleted either way (a test's zip, debug.update_from, is kept) */
static int updater_unpack(const char *zip_path, int downloaded, char names[][256], int *name_count, char *error,
	size_t error_size)
{
	SDL_IOStream *stream = SDL_IOFromFile(zip_path, "rb");
	Sint64 size = stream ? SDL_GetIOSize(stream) : -1;
	int succeeded = 0;

	/* (read from the file, not from a copy in memory: a guest's SDL bridges
	no SDL_IOFromConstMem, guest_sdl.c) */
	if (size < 0 || (unsigned long long)size > MAXIMUM_UPDATE_SIZE)
		snprintf(error, error_size, "could not read the download");
	else
		succeeded = zip_extract(stream, names, name_count, error, error_size);
	if (stream)
		SDL_CloseIO(stream);
	if (downloaded)
		update_delete_file(zip_path);
	return succeeded;
}

#ifndef UPDATER_PACKAGE
/* the first count of the new files, already in place, taken back out (to
the update folder) and the old ones (<name>.old) put back, the last first:
the install as it was before the update. A new file that had no old one is
deleted */
static void updater_put_back(char names[][256], int count)
{
	int index, failures = 0;

	for (index = count - 1; index >= 0; index--)
	{
		char path[1200], new_path[1200], old_path[1300];

		updater_path(path, sizeof(path), names[index]);
		updater_partial_path(new_path, sizeof(new_path), names[index]);
		snprintf(old_path, sizeof(old_path), "%s.old", path);
		if (SDL_GetPathInfo(old_path, NULL))
		{
			if (!update_replace_file(path, old_path, new_path))
			{
				platform_log("update: could not put back %s", names[index]);
				failures++;
			}
		}
		else
		{
			update_delete_file(path);
		}
	}
	if (failures)
		platform_log("update: %d of %d replaced files could not be put back", failures, count);
	else
		platform_log("update: the %d replaced files put back: the install is as it was", count);
}

/* the new build's files put in place of this one's: the release's zip
downloaded (in a window of its own, the desktop game's, unless
updater_download_background has, or a test's zip is named,
debug.update_from; with none, a dedicated server's, on its install's
thread), unpacked into the update's folder, and each file put in place
(the replaced ones, <name>.old, listed in update-old.txt for the new
version to delete); 1 when they all are, else 0 and why, the files put
back as they were */
static int updater_install(int windowed, char *error, size_t error_size)
{
	/* (not on the stack of the server's install thread) */
	static char names[MAXIMUM_UPDATE_FILES][256];
	char zip_path[1200], partial[1200], list_path[1200], url[512];
	const char *local_zip = windowed ? config_string("debug.update_from") : "";
	SDL_IOStream *old_list;
	int name_count = 0, index;

	updater_path(partial, sizeof(partial), updater_partial);
	if (local_zip[0])
		snprintf(zip_path, sizeof(zip_path), "%s", local_zip);
	else
		updater_partial_path(zip_path, sizeof(zip_path), UPDATE_ASSET);
	platform_log("update: %s build %ld (%s)", local_zip[0] ? "unpacking" : "downloading", updater_latest_build,
		local_zip[0] ? local_zip : UPDATE_ASSET);
	if (!update_make_directory(partial))
	{
		snprintf(error, error_size, "could not make %s (is the game's folder read-only?)", partial);
		return 0;
	}
	/* (a download checked against the release's signature before it is
	unpacked, updater_verify, which deletes one it refuses; the
	background's was when it came, updater_download_background, and a
	test's zip is not) */
	if (!local_zip[0] && !updater_downloaded)
	{
		if (windowed)
		{
			if (!updater_download_zip(zip_path, error, error_size))
				return 0;
		}
		else
		{
			updater_asset_url(url, sizeof(url), updater_latest_build, UPDATE_ASSET);
			if (!updater_fetch(url, zip_path, MAXIMUM_UPDATE_SIZE, NULL, NULL, error, error_size))
			{
				update_delete_file(zip_path);
				update_delete_file(partial);
				return 0;
			}
		}
		if (!updater_verify(zip_path, error, error_size))
		{
			update_delete_file(partial);
			return 0;
		}
	}
	if (!updater_unpack(zip_path, !local_zip[0], names, &name_count, error, error_size))
		return 0;
	/* (the files replaced, for the new version to delete) */
	updater_path(list_path, sizeof(list_path), UPDATE_OLD_LIST);
	old_list = SDL_IOFromFile(list_path, "ab");
	/* the new files in place of the old ones */
	for (index = 0; index < name_count; index++)
	{
		char path[1200], new_path[1200], old_path[1300];

		updater_path(path, sizeof(path), names[index]);
		updater_partial_path(new_path, sizeof(new_path), names[index]);
		snprintf(old_path, sizeof(old_path), "%s.old", path);
		if (!updater_make_folders(path, strlen(updater_directory) + 1) ||
			!update_replace_file(path, new_path, old_path))
		{
			snprintf(error, error_size, "could not replace %s", path);
			break;
		}
		updater_list_old(old_list, names[index]);
	}
	if (old_list)
		SDL_CloseIO(old_list);
	if (index == name_count)
	{
		updater_delete_partial(partial, names, name_count);
		return 1;
	}
	/* (one could not be: the ones before it put back as they were, and the
	list of them, which is of no update now, deleted) */
	if (index > 0)
		updater_put_back(names, index);
	update_delete_file(list_path);
	return 0;
}

/* downloads (unless updater_download_background has), unpacks and puts in
place the new build, and starts it; returns only if something failed */
static void updater_update(void)
{
	char error[512] = "";

	if (updater_install(1, error, sizeof(error)))
	{
		char launch[1200];

		snprintf(launch, sizeof(launch), "%s", updater_executable);
#ifndef HALO_WINDOWS
		/* (the tree's game: install_layout.c) */
		updater_path(launch, sizeof(launch), "bin/" INSTALL_NAME);
#endif
		platform_log("update: starting build %ld", updater_latest_build);
#ifndef HALO_WINDOWS
		/* (under gamescope, in this process, which Steam started and waits
		for: it never quits, and the new game's window is this process's,
		which gamescope shows as the game's. If it cannot be, the game
		quits, as its Quit does, to be started again) */
		if (updater_in_game)
		{
			update_restart(launch);
			platform_log("update: the new version is in place, but could not be started in this process: "
				"quitting");
			platform_request_quit();
			return;
		}
#endif
		if (update_launch(launch))
			exit(EXIT_SUCCESS);
		snprintf(error, sizeof(error), "the new version is in place, but could not be started: start it again");
	}
	platform_log("update: failed: %s", error);
	/* (no dialog under gamescope: the game goes on as it was) */
	if (updater_in_game)
		return;
	{
		char message[800];

		snprintf(message, sizeof(message), "The update failed:\n\n%s", error);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", message, NULL);
	}
}

/* the files a previous update left behind, which update-old.txt lists, and
the notice of extract-xiso, which releases carried until the disc image
readers were written again; 1 when the list was there (this is the start
that follows an update) */
static int updater_clean_up(void)
{
	/* (the Windows folder's; the Linux tree's, with the CC0 licence the
	releases before the AGPL had, under the names of the trees then and now:
	tools/linux_package.py) */
	static const char *const notices[] =
	{
#ifdef HALO_WINDOWS
		"extract-xiso-LICENSE.txt",
#else
		"share/licenses/opence/extract-xiso-LICENSE.txt", "share/licenses/opence/LICENSE.md",
		"share/licenses/" INSTALL_NAME "/extract-xiso-LICENSE.txt",
		"share/licenses/" INSTALL_NAME "/LICENSE.md",
#endif
	};
	size_t index;
	char path[1200];
	size_t size = 0;
	char *list;

	for (index = 0; index < sizeof(notices) / sizeof(notices[0]); index++)
	{
		updater_path(path, sizeof(path), notices[index]);
		update_delete_file(path);
	}
	updater_path(path, sizeof(path), UPDATE_OLD_LIST);
	list = SDL_LoadFile(path, &size);
	if (list)
	{
		char *line = list;

		while (*line)
		{
			size_t length = strcspn(line, "\r\n");
			char name[256];

			if (length && length < sizeof(name))
			{
				memcpy(name, line, length);
				name[length] = 0;
				/* (only <name>.old inside the game's folder) */
				if (length > 4 && !strcmp(name + length - 4, ".old") && updater_name_inside(name))
				{
					char old_path[1200];

					updater_path(old_path, sizeof(old_path), name);
					update_delete_file(old_path);
				}
			}
			line += length;
			line += strspn(line, "\r\n");
		}
		SDL_free(list);
		update_delete_file(path);
	}
	updater_partial_path(path, sizeof(path), UPDATE_ASSET);
	update_delete_file(path);
	updater_path(path, sizeof(path), updater_partial);
	update_delete_file(path);
	return list != NULL;
}
#endif

/* ---------- the game's */

/* at start-up (sdl_platform.c): looks for a new version, in the background */
void updater_start(void)
{
	char *slash;
	int updated;

	if (!update_executable_path(updater_executable, sizeof(updater_executable)))
		return;
#ifdef HALO_WINDOWS
	snprintf(updater_directory, sizeof(updater_directory), "%s", updater_executable);
	slash = strrchr(updater_directory, PATH_SEPARATOR[0]);
	if (!slash)
		return;
	*slash = 0;
#else
	/* (the tree's root; not a game a package manager installed, which it
	updates, nor one outside a tree, a build's: install_layout.c) */
	(void)slash;
	if (install_is_system())
	{
		if (HALO_BUILD_NUMBER > 0)
			platform_log("update: not looking for updates: the game is installed (%s)", install_system_reason());
		return;
	}
#ifdef UPDATER_PACKAGE
	/* (the Frame's package's folder, whose launcher, which says it started
	the game, installs an update once the game has quit; HALO_ as the host
	passes only those to the game, port/linux/host/host_main.c) */
	snprintf(updater_directory, sizeof(updater_directory), "%s", install_folder());
	if (!updater_directory[0])
		return;
	{
		char launcher[1200], sums[1200];
		const char *started_by = getenv("HALO_FRAME_LAUNCHER");

		updater_path(launcher, sizeof(launcher), "halo-vr.sh");
		updater_path(sums, sizeof(sums), "SHA256SUMS");
		if (!started_by || !started_by[0] || !SDL_GetPathInfo(launcher, NULL) || !SDL_GetPathInfo(sums, NULL))
		{
			if (HALO_BUILD_NUMBER > 0)
				platform_log("update: not looking for updates: the game was not started by the Steam Frame "
					"package's halo-vr.sh");
			return;
		}
	}
	/* (not again just after the launcher installed one, or tried to) */
	{
		const char *just_updated = getenv("HALO_FRAME_UPDATED");

		updated = just_updated && just_updated[0];
		if (updated)
		{
			platform_log("update: not looked for: halo-vr.sh has just installed an update (or tried to)");
			return;
		}
	}
#else
	if (!install_is_tree())
	{
		if (HALO_BUILD_NUMBER > 0)
			platform_log("update: not looking for updates: the game is not in a release's tree (bin/" INSTALL_NAME ")");
		return;
	}
	snprintf(updater_directory, sizeof(updater_directory), "%s", install_folder());
	if (!updater_directory[0])
		return;
	/* (asked in the game's menus under gamescope) */
	updater_in_game = updater_gamescope();
#endif
#endif
#ifndef UPDATER_PACKAGE
	updated = updater_clean_up();
#endif
	/* (not for builds without a number, unless a test names a zip (not
	again in the version it started), the player's no, or runs nobody is
	watching, but for a test with its answer) */
	if (config_string("debug.update_from")[0] && updated)
		return;
	if (HALO_BUILD_NUMBER <= 0 && !config_string("debug.update_from")[0])
	{
		platform_log("update: not looked for: only the builds CI numbers update themselves (this one has no "
			"number)");
		return;
	}
	if (!config_boolean("update.auto"))
	{
		platform_log("update: not looked for: update.auto is false");
		return;
	}
	if (!config_string("debug.update_answer")[0] && (config_boolean("debug.hidden_window") ||
		config_real("debug.exit_after") > 0.0 || config_string("debug.network_test")[0]))
	{
		return;
	}
	SDL_SetAtomicInt(&updater_state, _updater_checking);
	{
		SDL_Thread *thread = SDL_CreateThread(updater_check_thread, "update check", NULL);

		if (thread)
			SDL_DetachThread(thread);
		else
			SDL_SetAtomicInt(&updater_state, _updater_handled);
	}
}

#ifdef UPDATER_PACKAGE
/* every frame, on the game's thread (sdl_platform.c): the Frame's asks in
the game's menus once the new version is ready to install */
void updater_poll(SDL_Window *window)
{
	const char *test_answer = config_string("debug.update_answer");

	(void)window;
	if (SDL_GetAtomicInt(&updater_state) != _updater_available)
		return;
	/* (an automated test's answer: debug.update_answer) */
	if (test_answer[0])
	{
		SDL_SetAtomicInt(&updater_state, _updater_handled);
		platform_log("update: answering %s (debug.update_answer)", test_answer);
		if (!strcmp(test_answer, "yes"))
			updater_update_now();
		else if (!strcmp(test_answer, "never"))
			config_write_boolean("update.auto", 0);
		return;
	}
	if (!pc_menu_update_question_open())
		return;
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	platform_log("update: asking about build %ld at the main menu", updater_latest_build);
}

/* "port update now" (the update question's UPDATE: menu_functions.c): the
game quits, and its launcher installs the update (update-ready says so)
and starts the new game */
void updater_update_now(void)
{
	char path[1200], line[64];
	SDL_IOStream *ready;
	int length;

	updater_path(path, sizeof(path), "update-ready");
	ready = SDL_IOFromFile(path, "wb");
	length = snprintf(line, sizeof(line), "build %ld\n", updater_latest_build);
	if (!ready || SDL_WriteIO(ready, line, (size_t)length) != (size_t)length)
	{
		platform_log("update: could not write %s (%s)", path, SDL_GetError());
		if (ready)
			SDL_CloseIO(ready);
		return;
	}
	SDL_CloseIO(ready);
	platform_log("update: quitting, for halo-vr.sh to install build %ld", updater_latest_build);
	platform_request_quit();
}
#else
/* every frame, on the game's thread (sdl_platform.c): asks the player once a
new version is found */
void updater_poll(SDL_Window *window)
{
	static const SDL_MessageBoxButtonData question_buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
		{ 0, 2, "Do not ask again" },
	};
	static const SDL_MessageBoxButtonData confirm_buttons[] =
	{
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Yes" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "No" },
	};
	char message[400];
	int answer = 0;
	int fullscreen;
	const char *test_answer = config_string("debug.update_answer");

	if (SDL_GetAtomicInt(&updater_state) != _updater_available)
		return;
	/* (an automated test's answer: debug.update_answer) */
	if (test_answer[0])
	{
		SDL_SetAtomicInt(&updater_state, _updater_handled);
		platform_log("update: answering %s (debug.update_answer)", test_answer);
		if (!strcmp(test_answer, "yes"))
			updater_update();
		else if (!strcmp(test_answer, "never"))
			config_write_boolean("update.auto", 0);
		return;
	}
	/* (the Linux tree's under gamescope: in the game's menus, at the main
	menu, whose UPDATE is updater_update_now) */
	if (updater_in_game)
	{
		if (!pc_menu_update_question_open())
			return;
		SDL_SetAtomicInt(&updater_state, _updater_asked);
		platform_log("update: asking about build %ld at the main menu", updater_latest_build);
		return;
	}
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	/* (no question for the Windows build under gamescope, Proton's: the
	player updates from the desktop, or with debug.update_answer) */
	if (updater_gamescope())
	{
		platform_log("update: build %ld is out (this is build %d); not asking under gamescope (Steam Deck Game Mode)",
			updater_latest_build, HALO_BUILD_NUMBER);
		return;
	}
	/* (a dialog cannot show above a fullscreen game) */
	fullscreen = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN);
	if (fullscreen)
		SDL_SetWindowFullscreen(window, false);
	snprintf(message, sizeof(message),
		"A new version of Halo was detected (build %ld; this is build %d).\n\n"
		"Do you want to update? The game will close and start the new version.",
		updater_latest_build, HALO_BUILD_NUMBER);
	{
		SDL_MessageBoxData question = { SDL_MESSAGEBOX_INFORMATION, window, "Halo: new version", message,
			3, question_buttons, NULL };

		if (!SDL_ShowMessageBox(&question, &answer))
			answer = 0;
	}
	if (answer == 2)
	{
		SDL_MessageBoxData confirm = { SDL_MESSAGEBOX_WARNING, window, "Halo: new version",
			"Stop asking about new versions?\n\n"
			"To ask again, set auto = true in the [update] section of config.toml.",
			2, confirm_buttons, NULL };
		int confirmed = 0;

		if (SDL_ShowMessageBox(&confirm, &confirmed) && confirmed == 1)
		{
			if (config_write_boolean("update.auto", 0))
				platform_log("update: update.auto = false written to config.toml");
			else
				platform_log("update: could not write update.auto to config.toml");
		}
	}
	else if (answer == 1)
	{
		updater_update();
	}
	if (fullscreen)
		SDL_SetWindowFullscreen(window, true);
}

/* "port update now" (the update question's UPDATE: menu_functions.c),
under gamescope: the new version put in place and started in this
process; the game goes on if it could not be */
void updater_update_now(void)
{
	/* (once, for the question asked) */
	if (SDL_GetAtomicInt(&updater_state) != _updater_asked)
		return;
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	platform_log("update: updating to build %ld (UPDATE)", updater_latest_build);
	updater_update();
}
#endif

/* ---------- a dedicated server's (server.auto_update: server_director.c) */

#ifndef UPDATER_PACKAGE
/* why this build does not update itself as a server (updater_server_check's
0), empty while it does */
static char updater_server_reason[200];

/* the game's folder found, once, and what an update before this start left
deleted (update-old.txt's files): 1 when the server updates itself, else 0
and why in updater_server_reason */
static int updater_server_ready(void)
{
	static int ready = -1;
	long port = config_integer("network.port");

	if (ready >= 0)
		return ready;
	ready = 0;
	/* (a test's release server, debug.update_url, is asked by a build
	without a number too) */
	if (HALO_BUILD_NUMBER <= 0 && !config_string("debug.update_url")[0])
	{
		snprintf(updater_server_reason, sizeof(updater_server_reason), "it has no build number (only the builds "
			"CI numbers update themselves)");
		return 0;
	}
	if (!update_executable_path(updater_executable, sizeof(updater_executable)))
	{
		snprintf(updater_server_reason, sizeof(updater_server_reason), "its executable's path is not known");
		return 0;
	}
#ifdef HALO_WINDOWS
	{
		char *slash;

		snprintf(updater_directory, sizeof(updater_directory), "%s", updater_executable);
		slash = strrchr(updater_directory, PATH_SEPARATOR[0]);
		if (!slash)
		{
			updater_directory[0] = 0;
			snprintf(updater_server_reason, sizeof(updater_server_reason), "its folder is not known");
			return 0;
		}
		*slash = 0;
	}
#else
	/* (the tree's root; not a game a package manager installed, which it
	updates, nor one outside a tree, a build's: install_layout.c) */
	if (install_is_system())
	{
		snprintf(updater_server_reason, sizeof(updater_server_reason), "it is installed (%s)",
			install_system_reason());
		return 0;
	}
	if (!install_is_tree() || !install_folder()[0])
	{
		snprintf(updater_server_reason, sizeof(updater_server_reason), "it is not in a release's tree (bin/"
			INSTALL_NAME ")");
		return 0;
	}
	snprintf(updater_directory, sizeof(updater_directory), "%s", install_folder());
#endif
	/* (a computer's servers, each on a port of its own, may run from one
	tree: each has its own update folder and check) */
	snprintf(updater_partial, sizeof(updater_partial), UPDATE_DIRECTORY "-%ld", port);
	snprintf(updater_check_name, sizeof(updater_check_name), "update-check-%ld.json", port);
	updater_clean_up();
	ready = 1;
	return 1;
}

static int SDLCALL updater_server_check_thread(void *context)
{
	long latest = updater_latest_release();

	(void)context;
	if (latest > HALO_BUILD_NUMBER)
	{
		platform_log("update: build %ld is available (this is build %d; network version %ld, Standard %ld)", latest,
			HALO_BUILD_NUMBER, updater_release_network_version, updater_release_standard_version);
		updater_latest_build = latest;
		updater_latest_network_version = updater_release_network_version;
		updater_latest_standard_version = updater_release_standard_version;
		SDL_SetAtomicInt(&updater_state, _updater_available);
	}
	else
	{
		if (latest)
			platform_log("update: this is the latest build (%d)", HALO_BUILD_NUMBER);
		SDL_SetAtomicInt(&updater_state, _updater_handled);
	}
	return 0;
}

/* looks for a newer release on a thread of its own, unless a look is
under way (update.auto is not asked: server.auto_update is the operator's
setting): 1, or 0 if this build does not update itself (and why in
updater_server_unable) */
int updater_server_check(void)
{
	SDL_Thread *thread;

	if (!updater_server_ready())
		return 0;
	if (SDL_GetAtomicInt(&updater_state) == _updater_checking)
		return 1;
	SDL_SetAtomicInt(&updater_state, _updater_checking);
	thread = SDL_CreateThread(updater_server_check_thread, "update check", NULL);
	if (thread)
		SDL_DetachThread(thread);
	else
		SDL_SetAtomicInt(&updater_state, _updater_handled);
	return 1;
}

/* the newer build the last look found, 0 if none (or the look goes on) */
long updater_server_available(void)
{
	return SDL_GetAtomicInt(&updater_state) == _updater_available ? updater_latest_build : 0;
}

/* the network version of the newer build the last look found (its
release's notes'), 0 if they give none */
long updater_server_available_network_version(void)
{
	return updater_latest_network_version;
}

/* ... and its Standard games' network version, 0 if they give none */
long updater_server_available_standard_version(void)
{
	return updater_latest_standard_version;
}

enum
{
	_updater_install_none,
	_updater_install_running,
	_updater_install_installed,
	_updater_install_failed,
};

/* the server's install on a thread of its own: its state, and why it
failed (written before the state says so) */
static SDL_AtomicInt updater_install_state;
static char updater_install_error[512];

static void updater_install_failed(const char *why)
{
	snprintf(updater_install_error, sizeof(updater_install_error), "%s", why);
	SDL_SetAtomicInt(&updater_install_state, _updater_install_failed);
}

static int SDLCALL updater_install_thread(void *context)
{
	char error[512] = "";

	(void)context;
	if (updater_install(0, error, sizeof(error)))
	{
		platform_log("update: build %ld is in place of this one's files", updater_latest_build);
		SDL_SetAtomicInt(&updater_install_state, _updater_install_installed);
	}
	else
	{
		platform_log("update: build %ld could not be installed: %s", updater_latest_build, error);
		updater_install_failed(error[0] ? error : "the install failed");
	}
	return 0;
}

/* the newer build put in place of this one's files, not started (the server
stops, to be started again by what runs it), on a thread of its own: the
game goes on meanwhile, and updater_server_install_poll tells its end. 1 if
it began or goes on, else 0 (updater_server_install_poll says why) */
int updater_server_install_start(void)
{
	SDL_Thread *thread;

	if (SDL_GetAtomicInt(&updater_install_state) == _updater_install_running)
		return 1;
	if (!updater_server_ready() || SDL_GetAtomicInt(&updater_state) != _updater_available)
	{
		updater_install_failed("no newer build is known");
		return 0;
	}
	/* (the next look finds it again, should this install fail) */
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	SDL_SetAtomicInt(&updater_install_state, _updater_install_running);
	thread = SDL_CreateThread(updater_install_thread, "update install", NULL);
	if (!thread)
	{
		updater_install_failed("could not start the install's thread");
		return 0;
	}
	SDL_DetachThread(thread);
	return 1;
}

/* the newer build put in place on the calling thread, which waits for the
whole download (OpenCE's interface: the director uses
updater_server_install_start); 1, else 0 and why */
int updater_server_install(char *error, int error_size)
{
	if (!updater_server_ready() || SDL_GetAtomicInt(&updater_state) != _updater_available)
	{
		snprintf(error, (size_t)error_size, "no newer build is known");
		return 0;
	}
	SDL_SetAtomicInt(&updater_state, _updater_handled);
	return updater_install(0, error, (size_t)error_size);
}

/* the install's state: 0 while it goes on, 1 when the new build is in
place, -1 if it failed (or none was started), and why in error */
int updater_server_install_poll(char *error, int size)
{
	int state = SDL_GetAtomicInt(&updater_install_state);

	if (size > 0)
		error[0] = 0;
	switch (state)
	{
	case _updater_install_running:
		return 0;
	case _updater_install_installed:
		return 1;
	case _updater_install_failed:
		snprintf(error, (size_t)size, "%s", updater_install_error);
		return -1;
	}
	snprintf(error, (size_t)size, "no install was started");
	return -1;
}
#endif

#else

void updater_start(void)
{
}

/* (the Frame's alone: menu_functions.c's "port update now") */
void updater_update_now(void)
{
}

#if defined(HALO_MACOS)
#include <SDL3/SDL.h>

/* menu_functions.c's: 1 while a game is played, 0 at the main menu */
int pc_menu_game_playing(void);
/* port/linux/host/guest_desktop.c's import */
void host_app_playing(int playing);

/* every frame, on the game's thread (sdl_platform.c): the macOS app is
updated by Sparkle in the host (port/macos/host_app.m), which shows a new
version's window only at the main menu, never in a game: the host is told
whether a game is played as it changes */
void updater_poll(SDL_Window *window)
{
	static int told = -1;
	int playing = pc_menu_game_playing() ? 1 : 0;

	(void)window;
	if (playing == told)
		return;
	told = playing;
	host_app_playing(playing);
}
#elif !defined(HALO_ANDROID)
#include <SDL3/SDL.h>

/* (nothing to ask about: the builds without UPDATE_PLATFORM are updated
otherwise) */
void updater_poll(SDL_Window *window)
{
	(void)window;
}
#endif

#endif

/* a dedicated server of a build that does not update itself: the macOS
and iOS apps and the Android app (updated otherwise), and the Steam Frame's
package (by its launcher). updater_server_check's 0 says so: the director
logs it once and stops looking */
#if !defined(UPDATE_PLATFORM) || defined(UPDATER_PACKAGE)
int updater_server_check(void)
{
	return 0;
}

long updater_server_available(void)
{
	return 0;
}

long updater_server_available_network_version(void)
{
	return 0;
}

long updater_server_available_standard_version(void)
{
	return 0;
}

int updater_server_install(char *error, int error_size)
{
	if (error_size > 0)
		error[0] = 0;
	return 0;
}

int updater_server_install_start(void)
{
	return 0;
}

int updater_server_install_poll(char *error, int size)
{
	if (error && size > 0)
	{
		const char *why = "this build does not update itself as a server";
		int index;

		for (index = 0; why[index] && index < size - 1; index++)
			error[index] = why[index];
		error[index] = 0;
	}
	return -1;
}
#endif

/* why this build does not update itself as a server, once
updater_server_check has said it does not */
const char *updater_server_unable(void)
{
#if defined(UPDATER_PACKAGE)
	return "the Steam Frame's package is updated by its launcher, halo-vr.sh";
#elif defined(UPDATE_PLATFORM)
	return updater_server_reason[0] ? updater_server_reason : "it has not looked yet";
#else
	return "this platform's app is updated as an app, not by its server";
#endif
}
