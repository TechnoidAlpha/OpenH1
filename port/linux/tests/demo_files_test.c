/*
DEMO_FILES_TEST.C

The demos' files (port/linux/src/demo_files.c, demo_unpack.c) without the
game: a demo written by the writer's thread and read back block by block,
its blocks unpacked as written and a damaged one refused, the names that
are no demo's refused, and the retention (the oldest deleted past the
count and the megabytes, never a pinned one nor the one being written).
tools/test_demo_files.py builds and runs it:

    demo_files_test <empty folder> [write]
*/

#include "demo_files.h"
#include "posix.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char const *data_root;
static int failures;

const char *platform_data_root(void)
{
	return data_root;
}

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	failures++; } } while (0)

static unsigned long read_u32(unsigned char const *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 | (unsigned long)bytes[2] << 16 |
		(unsigned long)bytes[3] << 24;
}

/* a record of the demos' format: its type, length and bytes */
static unsigned long record(unsigned char *out, char type, void const *bytes, unsigned long size)
{
	out[0] = (unsigned char)type;
	out[1] = (unsigned char)size;
	out[2] = (unsigned char)(size >> 8);
	out[3] = (unsigned char)(size >> 16);
	out[4] = (unsigned char)(size >> 24);
	memcpy(out + 5, bytes, size);
	return 5 + size;
}

static void test_write_and_read(void)
{
	enum { BLOCKS = 4, BIG = 6 << 20 };
	unsigned char header[512];
	unsigned char *blocks[BLOCKS];
	unsigned long sizes[BLOCKS];
	char folder[512], path[768];
	struct demo_writer *writer;
	struct demo_reader *reader;
	unsigned long file_size = 0, offset = sizeof(header);
	unsigned long index, byte_index;
	int read_blocks = 0;

	CHECK(demo_files_folder(folder, sizeof(folder), 0));
	snprintf(path, sizeof(path), "%s/20261108-010000-test.demo", folder);
	memset(header, 0, sizeof(header));
	memcpy(header, "OH1DEMO", 8);
	header[8] = 1;
	snprintf((char *)header + 128, 128, "levels\\test\\demo\\demo");
	writer = demo_writer_open(path, header, sizeof(header));
	CHECK(writer != NULL);
	if (!writer)
		return;
	/* (records of ticks, then a snapshot's worth that packs well, then one
	that does not) */
	for (index = 0; index < BLOCKS; index++)
	{
		unsigned long size = index == 2 ? BIG : index == 3 ? (1 << 20) + 1 : 3000;
		unsigned long position = 0;

		blocks[index] = demo_files_alloc(size + 64);
		if (index < 2)
		{
			unsigned char tick[8] = { (unsigned char)index, 0, 0, 0, 60, 0, 0, 0 };

			while (position + 13 + 5 <= size)
				position += record(blocks[index] + position, 'T', tick, sizeof(tick));
			size = position;
		}
		else
		{
			unsigned long seed = 12345 + index;

			for (byte_index = 0; byte_index < size; byte_index++)
			{
				seed = seed * 1103515245UL + 12345UL;
				blocks[index][byte_index] = index == 2 ? (unsigned char)(byte_index / 4096) : (unsigned char)(seed >> 16);
			}
		}
		sizes[index] = size;
		demo_writer_block(writer, index == 2 ? 'K' : index == 3 ? 'k' : 'R', (unsigned long)index * 30, blocks[index], size, 0);
	}
	/* (nothing of no size) */
	demo_writer_block(writer, 'R', 0, blocks[0], 0, 0);
	demo_writer_close(writer);

	reader = demo_reader_open(path, &file_size);
	CHECK(reader != NULL);
	if (!reader)
		return;
	{
		unsigned char read_header[512];

		CHECK(demo_reader_read(reader, 0, read_header, sizeof(read_header)));
		CHECK(!memcmp(read_header, header, sizeof(header)));
	}
	while (offset + DEMO_BLOCK_HEADER_SIZE <= file_size)
	{
		unsigned char block_header[DEMO_BLOCK_HEADER_SIZE];
		unsigned long kind, first_tick, size, packed_size;
		unsigned char *packed, *data;

		CHECK(demo_reader_read(reader, offset, block_header, sizeof(block_header)));
		CHECK(read_u32(block_header) == DEMO_BLOCK_MAGIC);
		kind = read_u32(block_header + 4);
		first_tick = read_u32(block_header + 8);
		size = read_u32(block_header + 12);
		packed_size = read_u32(block_header + 16);
		CHECK(read_blocks < BLOCKS);
		if (read_blocks >= BLOCKS)
			break;
		CHECK(kind == (unsigned long)(read_blocks == 2 ? 'K' : read_blocks == 3 ? 'k' : 'R'));
		CHECK(first_tick == (unsigned long)read_blocks * 30);
		CHECK(size == sizes[read_blocks]);
		packed = demo_files_alloc(packed_size);
		data = demo_files_alloc(size);
		CHECK(demo_reader_read(reader, offset + DEMO_BLOCK_HEADER_SIZE, packed, packed_size));
		CHECK(demo_files_unpack(packed, packed_size, data, size));
		CHECK(!memcmp(data, blocks[read_blocks], size));
		/* (one byte too few asked for, and a damaged stream, refused) */
		CHECK(!demo_files_unpack(packed, packed_size, data, size - 1));
		packed[packed_size / 2] ^= 0x5A;
		packed[packed_size / 2 + 1] ^= 0xA5;
		CHECK(!demo_files_unpack(packed, packed_size, data, size) || read_blocks == 3);
		/* (the snapshot packed well) */
		if (read_blocks == 2)
			CHECK(packed_size < size / 20);
		demo_files_free(packed);
		demo_files_free(data);
		offset += DEMO_BLOCK_HEADER_SIZE + packed_size;
		read_blocks++;
	}
	CHECK(read_blocks == BLOCKS);
	CHECK(offset == file_size);
	CHECK(!demo_reader_read(reader, file_size - 4, header, 8));
	demo_reader_close(reader);
	for (index = 0; index < BLOCKS; index++)
		demo_files_free(blocks[index]);
	printf("%s\n", path);
}

static void make_demo(char const *name, unsigned long size, unsigned long modified, int pinned)
{
	char folder[512], path[768];
	FILE *file;
	unsigned long written;

	demo_files_folder(folder, sizeof(folder), pinned);
	snprintf(path, sizeof(path), "%s/%s", folder, name);
	file = fopen(path, "wb");
	CHECK(file != NULL);
	if (!file)
		return;
	for (written = 0; written < size; written++)
		fputc((int)(written & 0xFF), file);
	fclose(file);
	CHECK(posix_set_file_times(path, modified, 0, modified, 0) == 0);
}

static int listed(char const *name, int *pinned)
{
	struct demo_file_entry entries[DEMO_FILES_MAXIMUM_LISTED];
	int count = demo_files_list(entries, DEMO_FILES_MAXIMUM_LISTED);
	int index;

	for (index = 0; index < count; index++)
	{
		if (!strcmp(entries[index].name, name))
		{
			if (pinned)
				*pinned = entries[index].pinned;
			return 1;
		}
	}
	return 0;
}

static void test_retention(void)
{
	struct demo_file_entry entries[DEMO_FILES_MAXIMUM_LISTED];
	char path[768];
	int pinned = 0;
	int count;

	/* (one MB each, a minute apart; the oldest pinned) */
	make_demo("a-1.demo", 1 << 20, 1700000000, 1);
	make_demo("b-2.demo", 1 << 20, 1700000060, 0);
	make_demo("c-3.demo", 1 << 20, 1700000120, 0);
	make_demo("d-4.demo", 1 << 20, 1700000180, 0);
	make_demo("e-5.demo", 1 << 20, 1700000240, 0);
	make_demo("f-6.demo", 1 << 20, 1700000300, 0);
	make_demo("not-a-demo.txt", 10, 1700000000, 0);

	count = demo_files_list(entries, DEMO_FILES_MAXIMUM_LISTED);
	/* (and the one written above) */
	CHECK(count == 7);
	CHECK(!strcmp(entries[0].name, "20261108-010000-test.demo"));
	CHECK(!strcmp(entries[1].name, "f-6.demo"));
	CHECK(!strcmp(entries[count - 1].name, "a-1.demo") && entries[count - 1].pinned);
	CHECK(entries[1].size == 1 << 20);

	/* (the newest four kept by count, the one written being one of them) */
	demo_files_retain(4, 100, NULL);
	CHECK(listed("20261108-010000-test.demo", NULL));
	CHECK(listed("f-6.demo", NULL) && listed("e-5.demo", NULL) && listed("d-4.demo", NULL));
	CHECK(!listed("c-3.demo", NULL) && !listed("b-2.demo", NULL));
	CHECK(listed("a-1.demo", &pinned) && pinned);

	/* (by the megabytes: two of one MB fit in two, the one being written
	kept whatever it takes) */
	demo_files_retain(20, 2, "d-4.demo");
	CHECK(listed("d-4.demo", NULL));
	CHECK(listed("20261108-010000-test.demo", NULL));
	CHECK(!listed("f-6.demo", NULL) && !listed("e-5.demo", NULL));
	CHECK(listed("a-1.demo", NULL));

	/* (pinned and unpinned; a name with a folder in it is no demo's) */
	CHECK(demo_files_pin("d-4.demo", 1));
	CHECK(listed("d-4.demo", &pinned) && pinned);
	CHECK(!demo_files_pin("d-4.demo", 1));
	CHECK(demo_files_pin("a-1.demo", 0));
	CHECK(listed("a-1.demo", &pinned) && !pinned);
	CHECK(!demo_files_path("../a-1.demo", path, sizeof(path)));
	CHECK(!demo_files_delete("../../a-1.demo"));
	CHECK(!demo_files_delete("kept/d-4.demo"));
	CHECK(demo_files_path("d-4.demo", path, sizeof(path)) && strstr(path, "/demos/kept/d-4.demo"));
	demo_files_retain(0, 0, NULL);
	CHECK(listed("d-4.demo", NULL));
	CHECK(!listed("a-1.demo", NULL) && !listed("20261108-010000-test.demo", NULL));
	CHECK(demo_files_delete("d-4.demo"));
	CHECK(demo_files_list(entries, DEMO_FILES_MAXIMUM_LISTED) == 0);
}

/* ("killed": a recorder that never ends its demo, the test's to kill: blocks
of tick records written on and on, its path said first) */
static void test_killed(void)
{
	enum { SIZE = 256 << 10 };
	unsigned char header[512];
	char folder[512], path[768];
	struct demo_writer *writer;
	unsigned char *block = demo_files_alloc(SIZE);
	unsigned long index;

	if (!block || !demo_files_folder(folder, sizeof(folder), 0))
		exit(2);
	snprintf(path, sizeof(path), "%s/20261108-020000-killed.demo", folder);
	memset(header, 0, sizeof(header));
	memcpy(header, "OH1DEMO", 8);
	header[8] = 2;
	snprintf((char *)header + 128, 128, "levels\\test\\demo\\demo");
	writer = demo_writer_open(path, header, sizeof(header));
	if (!writer)
		exit(2);
	printf("%s\n", path);
	fflush(stdout);
	for (index = 0;; index++)
	{
		unsigned long position = 0;
		unsigned long seed = 777 + index;

		/* (a second's ticks, each with bytes that pack poorly) */
		while (position + 5 + 8 + 5 + 4096 <= SIZE)
		{
			unsigned char tick[8] = { (unsigned char)index, (unsigned char)(index >> 8), 0, 0, 120, 0, 0, 0 };
			unsigned char noise[4096];
			unsigned long byte_index;

			for (byte_index = 0; byte_index < sizeof(noise); byte_index++)
			{
				seed = seed * 1103515245UL + 12345UL;
				noise[byte_index] = (unsigned char)(seed >> 16);
			}
			position += record(block + position, 'T', tick, sizeof(tick));
			position += record(block + position, 'P', noise, sizeof(noise));
		}
		demo_writer_block(writer, 'R', index * 30, block, position, 0);
	}
}

int main(int argc, char **argv)
{
	if (argc < 2)
		return 2;
	data_root = argv[1];
	if (argc >= 3 && !strcmp(argv[2], "killed"))
		test_killed();
	test_write_and_read();
	/* ("write": the demo left for tools/demo_info.py) */
	if (argc < 3 || strcmp(argv[2], "write"))
		test_retention();
	return failures ? 1 : 0;
}
