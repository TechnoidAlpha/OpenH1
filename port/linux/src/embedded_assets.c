/*
EMBEDDED_ASSETS.C

The assets' file read and checked, once (embedded_assets.h).
*/

#include "embedded_assets.h"
#include "install_layout.h"
#include "platform.h"
#include "port_config.h"

#include "zlib_prefixed.h"

#ifndef HALO_ANDROID
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EMBEDDED_ASSETS_MAGIC 0x4131484fu /* "OH1A" */
#define EMBEDDED_ASSETS_FORM 1
#define EMBEDDED_ASSETS_HEADER_SIZE 16

static const unsigned char *assets;
static size_t assets_size;

static unsigned int assets_word(const unsigned char *bytes)
{
	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

/* why the file is not used, for embedded_assets_check: "" when it is */
static char assets_problem[1400];
static int assets_stale;

/* a candidate file: kept if it is this build's, else its problem noted */
static int assets_try(const char *path)
{
	size_t size = 0;
	char *file = config_file_read(path, &size);

	if (!file)
	{
		/* (where the game's own goes, the last place looked: unless a file
		of another build was found, which is the problem to tell of) */
		if (!assets_stale)
			snprintf(assets_problem, sizeof(assets_problem), "%s is missing", path);
		return 0;
	}
	if (size != embedded_assets_size || size < EMBEDDED_ASSETS_HEADER_SIZE ||
		assets_word((const unsigned char *)file) != EMBEDDED_ASSETS_MAGIC ||
		assets_word((const unsigned char *)file + 4) != EMBEDDED_ASSETS_FORM ||
		crc32(0L, (const Bytef *)file, (uInt)size) != embedded_assets_crc)
	{
		/* (another build's, or damaged: that is the problem to tell of) */
		snprintf(assets_problem, sizeof(assets_problem), "%s is not this build's (%lu bytes, not %u, or damaged)",
			path, (unsigned long)size, embedded_assets_size);
		assets_stale = 1;
		free(file);
		return 0;
	}
	assets = (const unsigned char *)file;
	assets_size = size;
	assets_problem[0] = 0;
	return 1;
}

/* beside config.toml, else the Linux release's (share/openh1,
install_layout.c), as brokers.txt is found (p2p_signal.c) */
static void assets_read(void)
{
	char path[1024];
	size_t length;

	if (embedded_assets_size <= EMBEDDED_ASSETS_HEADER_SIZE)
		return;
	config_folder(path, sizeof(path));
	length = strlen(path);
	snprintf(path + length, sizeof(path) - length, "%s", EMBEDDED_ASSETS_FILE);
	if (assets_try(path))
		return;
#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID) && !defined(HALO_MACOS)
	install_shared_file(EMBEDDED_ASSETS_FILE, path, sizeof(path));
	if (assets_try(path))
		return;
#endif
	platform_log("assets: %s: no high-res HUD, text or menus", assets_problem);
}

#ifndef HALO_ANDROID
static pthread_mutex_t assets_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
static int assets_tried;

static void assets_ready(void)
{
	/* (the high-res HUD's decoders ask from their threads too: the guest's C
	library has no pthread_once) */
#ifndef HALO_ANDROID
	pthread_mutex_lock(&assets_lock);
#endif
	if (!assets_tried)
	{
		assets_tried = 1;
		assets_read();
	}
#ifndef HALO_ANDROID
	pthread_mutex_unlock(&assets_lock);
#endif
}

const void *embedded_assets_data(unsigned int offset, unsigned int size)
{
	assets_ready();
	if (!assets || offset < EMBEDDED_ASSETS_HEADER_SIZE || offset > assets_size || size > assets_size - offset)
		return NULL;
	return assets + offset;
}

const char *embedded_assets_check(void)
{
	assets_ready();
	return assets_problem[0] ? assets_problem : NULL;
}
