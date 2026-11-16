/*
PREPARED_SHIPPED.C

The shipped records' file read, and its records found and inflated
(prepared_shipped.h).
*/

#include "prepared_shipped.h"
#include "install_layout.h"
#include "platform.h"
#include "port_config.h"

#include "zlib_prefixed.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREPARED_SHIPPED_MAGIC 0x5331484fu /* "OH1S" */
#define PREPARED_SHIPPED_FORM 1
#define PREPARED_SHIPPED_HEADER_WORDS 3
#define PREPARED_SHIPPED_ENTRY_WORDS 6
/* the largest record inflated (render_prepared.c's own limit is larger: a
shipped record is a stock map's, under 1 MB) */
#define PREPARED_SHIPPED_MAXIMUM_SIZE (64U << 20)

static struct
{
	int read;
	unsigned char *file;
	size_t size;
	unsigned int count;
} shipped;

static unsigned int shipped_word(size_t offset)
{
	const unsigned char *bytes = shipped.file + offset;

	return (unsigned int)bytes[0] | (unsigned int)bytes[1] << 8 | (unsigned int)bytes[2] << 16 |
		(unsigned int)bytes[3] << 24;
}

/* the file read, once: beside config.toml, else the Linux release's
(share/openh1, install_layout.c), as brokers.txt is found (p2p_signal.c);
none with HALO_SHIPPED_RECORDS=0 (tools/prepared_records.py, which makes
them again: each map walked and recorded) */
static void shipped_read(void)
{
	const char *setting = getenv("HALO_SHIPPED_RECORDS");
	char path[1024];
	size_t length;

	if (shipped.read)
		return;
	shipped.read = 1;
	if (setting && !strcmp(setting, "0"))
	{
		platform_log("render: the shipped records left out (HALO_SHIPPED_RECORDS=0): each map's first load is walked");
		return;
	}
	config_folder(path, sizeof(path));
	length = strlen(path);
	snprintf(path + length, sizeof(path) - length, "%s", PREPARED_SHIPPED_FILE);
	shipped.file = (unsigned char *)config_file_read(path, &shipped.size);
#if !defined(HALO_WINDOWS) && !defined(HALO_ANDROID) && !defined(HALO_MACOS)
	if (!shipped.file)
	{
		install_shared_file(PREPARED_SHIPPED_FILE, path, sizeof(path));
		shipped.file = (unsigned char *)config_file_read(path, &shipped.size);
	}
#endif
	if (!shipped.file)
	{
		platform_log("render: no shipped records (%s): each map's first load is walked", path);
		return;
	}
	if (shipped.size >= PREPARED_SHIPPED_HEADER_WORDS * 4 && shipped_word(0) == PREPARED_SHIPPED_MAGIC &&
		shipped_word(4) == PREPARED_SHIPPED_FORM && shipped_word(8) <= (shipped.size / 4 -
		PREPARED_SHIPPED_HEADER_WORDS) / PREPARED_SHIPPED_ENTRY_WORDS)
	{
		shipped.count = shipped_word(8);
		platform_log("render: %u shipped records (%s)", shipped.count, path);
		return;
	}
	platform_log("render: the shipped records (%s) are not of this form: passed over", path);
	free(shipped.file);
	shipped.file = NULL;
}

/* the record's entry's offset in the file, or 0 */
static size_t shipped_find(unsigned int checksum, unsigned int key_high, unsigned int key_low)
{
	unsigned int index;

	shipped_read();
	for (index = 0; index < shipped.count; index++)
	{
		size_t entry = (PREPARED_SHIPPED_HEADER_WORDS + (size_t)index * PREPARED_SHIPPED_ENTRY_WORDS) * 4;
		size_t offset = shipped_word(entry + 12), compressed = shipped_word(entry + 16);

		if (shipped_word(entry) == checksum && shipped_word(entry + 4) == key_high &&
			shipped_word(entry + 8) == key_low && offset <= shipped.size && compressed <= shipped.size - offset &&
			shipped_word(entry + 20) && shipped_word(entry + 20) <= PREPARED_SHIPPED_MAXIMUM_SIZE)
			return entry;
	}
	return 0;
}

unsigned int prepared_shipped_size(unsigned int checksum, unsigned int key_high, unsigned int key_low)
{
	size_t entry = shipped_find(checksum, key_high, key_low);

	return entry ? shipped_word(entry + 20) : 0;
}

int prepared_shipped_inflate(unsigned int checksum, unsigned int key_high, unsigned int key_low, void *into,
	unsigned int size)
{
	size_t entry = shipped_find(checksum, key_high, key_low);
	uLongf inflated = size;

	return entry && size == shipped_word(entry + 20) && uncompress(into, &inflated, shipped.file +
		shipped_word(entry + 12), shipped_word(entry + 16)) == Z_OK && inflated == size;
}
