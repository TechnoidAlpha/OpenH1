/*
DEMO_FILES.C

The demos' files (demo_files.h; port/linux/game/demo.c writes and reads
what is in them, docs/demos.md). They are kept in paths.data's demos/
folder, the pinned ones in demos/kept/, which retention never touches: of
the others, the oldest go once there are more than demo.keep_count of them
or they take more than demo.keep_megabytes.

The writer packs each block with the game's deflate (level 1: a snapshot of
the game state is several megabytes, packed as the game plays) and appends
it on a thread of its own, so that a frame never waits on the disk. Its
queue holds at most WRITER_QUEUE_BYTES: past half of it, a block that may
be dropped (a seek's snapshot) is, and past all of it, the game waits for
room rather than lose the input of its ticks.
*/

#include "platform.h"
#include "posix.h"
#include "demo_files.h"
#include "memory/zlib/zlib.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	WRITER_QUEUE_BYTES = 96 << 20,
};

struct demo_writer_block
{
	struct demo_writer_block *next;
	unsigned long kind;
	unsigned long first_tick;
	unsigned long size;
	unsigned char data[1];
};

struct demo_writer
{
	FILE *file;
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t changed;
	struct demo_writer_block *first;
	struct demo_writer_block *last;
	unsigned long queued_bytes;
	/* (64 bits: a long demo passes 4 GB, which an ILP32 guest's long wraps at) */
	unsigned long long written_bytes;
	int closing;
	int failed;
};

/* ---------- the folders */

int demo_files_folder(char *path, unsigned long size, int pinned)
{
	char const *root = platform_data_root();
	int length;

	length = snprintf(path, size, "%s/demos", root && *root ? root : ".");
	if (length < 0 || (unsigned long)length >= size)
		return 0;
	posix_make_directory(path);
	if (pinned)
	{
		length = snprintf(path, size, "%s/demos/kept", root && *root ? root : ".");
		if (length < 0 || (unsigned long)length >= size)
			return 0;
		posix_make_directory(path);
	}
	return 1;
}

/* a name of a demo's file: no folders, no hidden files */
static int demo_files_name_valid(char const *name)
{
	size_t length = name ? strlen(name) : 0;

	if (!length || length >= DEMO_FILE_NAME_SIZE || name[0] == '.' || strchr(name, '/') || strchr(name, '\\'))
		return 0;
	return length > 5 && !strcmp(name + length - 5, ".demo");
}

static int demo_files_join(char *path, unsigned long size, int pinned, char const *name)
{
	char folder[512];
	int length;

	if (!demo_files_name_valid(name) || !demo_files_folder(folder, sizeof(folder), pinned))
		return 0;
	length = snprintf(path, size, "%s/%s", folder, name);
	return length > 0 && (unsigned long)length < size;
}

int demo_files_path(char const *name, char *path, unsigned long size)
{
	struct posix_file_information information;
	int pinned;

	for (pinned = 0; pinned < 2; pinned++)
	{
		if (demo_files_join(path, size, pinned, name) && !posix_stat(path, &information) &&
			!(information.flags & _posix_file_is_directory))
		{
			return 1;
		}
	}
	return 0;
}

/* ---------- the list */

static int demo_files_list_folder(int pinned, struct demo_file_entry *entries, int count, int maximum)
{
	char folder[512];
	char name[256];
	void *directory;

	if (!demo_files_folder(folder, sizeof(folder), pinned))
		return count;
	directory = posix_directory_open(folder);
	if (!directory)
		return count;
	while (count < maximum && posix_directory_next(directory, name, sizeof(name)))
	{
		struct posix_file_information information;
		char path[768];

		if (!demo_files_name_valid(name))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (posix_stat(path, &information) || (information.flags & _posix_file_is_directory))
			continue;
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", name);
		entries[count].size = information.size_high ? 0xFFFFFFFFUL : information.size_low;
		entries[count].modified = information.modification_seconds;
		entries[count].pinned = pinned;
		count++;
	}
	posix_directory_close(directory);
	return count;
}

static int demo_files_newer_first(void const *a, void const *b)
{
	struct demo_file_entry const *first = a;
	struct demo_file_entry const *second = b;

	if (first->modified != second->modified)
		return first->modified > second->modified ? -1 : 1;
	return strcmp(second->name, first->name);
}

int demo_files_list(struct demo_file_entry *entries, int maximum)
{
	int count = demo_files_list_folder(0, entries, 0, maximum);

	count = demo_files_list_folder(1, entries, count, maximum);
	qsort(entries, (size_t)count, sizeof(*entries), demo_files_newer_first);
	return count;
}

void demo_files_retain(long count, long megabytes, char const *skip)
{
	static struct demo_file_entry entries[DEMO_FILES_MAXIMUM_LISTED];
	unsigned long long total = 0;
	unsigned long long budget = (unsigned long long)(megabytes > 0 ? megabytes : 0) << 20;
	long kept = 0;
	int listed = demo_files_list_folder(0, entries, 0, DEMO_FILES_MAXIMUM_LISTED);
	int index;

	qsort(entries, (size_t)listed, sizeof(*entries), demo_files_newer_first);
	/* (newest first: each kept while it fits, the rest deleted) */
	for (index = 0; index < listed; index++)
	{
		int is_skip = skip && !strcmp(entries[index].name, skip);

		if (is_skip || (kept < count && total + entries[index].size <= budget))
		{
			kept++;
			total += entries[index].size;
			continue;
		}
		if (demo_files_delete(entries[index].name))
			platform_log("demos: %s deleted (demo.keep_count %ld, demo.keep_megabytes %ld)", entries[index].name,
				count, megabytes);
	}
}

int demo_files_pin(char const *name, int pinned)
{
	char from[768], to[768];

	if (!demo_files_join(from, sizeof(from), !pinned, name) || !demo_files_join(to, sizeof(to), pinned, name))
		return 0;
	return rename(from, to) == 0;
}

int demo_files_delete(char const *name)
{
	char path[768];
	int pinned;

	for (pinned = 0; pinned < 2; pinned++)
	{
		if (demo_files_join(path, sizeof(path), pinned, name) && remove(path) == 0)
			return 1;
	}
	return 0;
}

/* ---------- the writer */

/* the game's deflate, with the C library's allocator (thread-safe: the
game's own is for the game's thread) */
static voidpf demo_writer_zalloc(voidpf opaque, uInt items, uInt size)
{
	(void)opaque;
	return calloc(items, size);
}

static void demo_writer_zfree(voidpf opaque, voidpf address)
{
	(void)opaque;
	free(address);
}

static void demo_put_u32(unsigned char *out, unsigned long value)
{
	out[0] = (unsigned char)value;
	out[1] = (unsigned char)(value >> 8);
	out[2] = (unsigned char)(value >> 16);
	out[3] = (unsigned char)(value >> 24);
}

static int demo_writer_pack_write(struct demo_writer *writer, struct demo_writer_block const *block)
{
	unsigned long capacity = block->size + block->size / 1000 + 64;
	unsigned char *packed = malloc(DEMO_BLOCK_HEADER_SIZE + capacity);
	unsigned char *header = packed;
	z_stream stream;
	int result;
	int written = 0;

	if (!packed)
		return 0;
	memset(&stream, 0, sizeof(stream));
	stream.zalloc = demo_writer_zalloc;
	stream.zfree = demo_writer_zfree;
	if (deflateInit(&stream, 1) == Z_OK)
	{
		stream.next_in = (Bytef *)block->data;
		stream.avail_in = (uInt)block->size;
		stream.next_out = packed + DEMO_BLOCK_HEADER_SIZE;
		stream.avail_out = (uInt)capacity;
		result = deflate(&stream, Z_FINISH);
		if (result == Z_STREAM_END)
		{
			demo_put_u32(header, DEMO_BLOCK_MAGIC);
			demo_put_u32(header + 4, block->kind);
			demo_put_u32(header + 8, block->first_tick);
			demo_put_u32(header + 12, block->size);
			demo_put_u32(header + 16, stream.total_out);
			written = fwrite(packed, 1, DEMO_BLOCK_HEADER_SIZE + stream.total_out, writer->file) ==
				DEMO_BLOCK_HEADER_SIZE + stream.total_out;
			/* (a block whole on the disk as it is written: a crash leaves the
			blocks before it readable) */
			if (written)
				fflush(writer->file);
			pthread_mutex_lock(&writer->lock);
			writer->written_bytes += DEMO_BLOCK_HEADER_SIZE + stream.total_out;
			pthread_mutex_unlock(&writer->lock);
		}
		deflateEnd(&stream);
	}
	free(packed);
	return written;
}

static void *demo_writer_thread(void *context)
{
	struct demo_writer *writer = context;

	for (;;)
	{
		struct demo_writer_block *block;

		pthread_mutex_lock(&writer->lock);
		while (!writer->first && !writer->closing)
			pthread_cond_wait(&writer->changed, &writer->lock);
		block = writer->first;
		if (!block)
		{
			pthread_mutex_unlock(&writer->lock);
			break;
		}
		writer->first = block->next;
		if (!writer->first)
			writer->last = NULL;
		pthread_mutex_unlock(&writer->lock);

		if (!writer->failed && !demo_writer_pack_write(writer, block))
		{
			writer->failed = 1;
			platform_log("demos: cannot write a block (the disk is full?); the demo ends there");
		}
		pthread_mutex_lock(&writer->lock);
		writer->queued_bytes -= block->size;
		pthread_cond_broadcast(&writer->changed);
		pthread_mutex_unlock(&writer->lock);
		free(block);
	}
	return NULL;
}

struct demo_writer *demo_writer_open(char const *path, void const *header, unsigned long header_size)
{
	struct demo_writer *writer = calloc(1, sizeof(*writer));

	if (!writer)
		return NULL;
	writer->file = fopen(path, "wb");
	if (!writer->file)
	{
		platform_log("demos: cannot write %s", path);
		free(writer);
		return NULL;
	}
	if (fwrite(header, 1, header_size, writer->file) != header_size)
	{
		fclose(writer->file);
		remove(path);
		free(writer);
		return NULL;
	}
	fflush(writer->file);
	writer->written_bytes = header_size;
	pthread_mutex_init(&writer->lock, NULL);
	pthread_cond_init(&writer->changed, NULL);
	if (pthread_create(&writer->thread, NULL, demo_writer_thread, writer) != 0)
	{
		fclose(writer->file);
		free(writer);
		return NULL;
	}
	return writer;
}

void demo_writer_block(struct demo_writer *writer, unsigned long kind, unsigned long first_tick,
	void const *data, unsigned long size, int droppable)
{
	struct demo_writer_block *block;

	if (!writer || !size || size > DEMO_BLOCK_MAXIMUM_SIZE)
		return;
	pthread_mutex_lock(&writer->lock);
	if (droppable && writer->queued_bytes + size > WRITER_QUEUE_BYTES / 2)
	{
		pthread_mutex_unlock(&writer->lock);
		return;
	}
	while (writer->queued_bytes && writer->queued_bytes + size > WRITER_QUEUE_BYTES)
		pthread_cond_wait(&writer->changed, &writer->lock);
	pthread_mutex_unlock(&writer->lock);

	block = malloc(sizeof(*block) + size);
	if (!block)
		return;
	block->next = NULL;
	block->kind = kind;
	block->first_tick = first_tick;
	block->size = size;
	memcpy(block->data, data, size);
	pthread_mutex_lock(&writer->lock);
	if (writer->last)
		writer->last->next = block;
	else
		writer->first = block;
	writer->last = block;
	writer->queued_bytes += size;
	pthread_cond_broadcast(&writer->changed);
	pthread_mutex_unlock(&writer->lock);
}

unsigned long long demo_writer_bytes(struct demo_writer *writer)
{
	unsigned long long bytes;

	if (!writer)
		return 0;
	pthread_mutex_lock(&writer->lock);
	bytes = writer->written_bytes;
	pthread_mutex_unlock(&writer->lock);
	return bytes;
}

unsigned long long demo_writer_close(struct demo_writer *writer)
{
	unsigned long long bytes;

	if (!writer)
		return 0;
	pthread_mutex_lock(&writer->lock);
	writer->closing = 1;
	pthread_cond_broadcast(&writer->changed);
	pthread_mutex_unlock(&writer->lock);
	pthread_join(writer->thread, NULL);
	fclose(writer->file);
	bytes = writer->written_bytes;
	pthread_mutex_destroy(&writer->lock);
	pthread_cond_destroy(&writer->changed);
	free(writer);
	return bytes;
}

/* ---------- memory and the reader */

void *demo_files_alloc(unsigned long size)
{
	return malloc(size ? size : 1);
}

void *demo_files_realloc(void *address, unsigned long size)
{
	return realloc(address, size ? size : 1);
}

void demo_files_free(void *address)
{
	free(address);
}

struct demo_reader
{
	FILE *file;
};

struct demo_reader *demo_reader_open(char const *path, unsigned long *size)
{
	struct demo_reader *reader;
	struct posix_file_information information;
	FILE *file;

	if (posix_stat(path, &information) || (information.flags & _posix_file_is_directory) || information.size_high)
		return NULL;
	file = fopen(path, "rb");
	if (!file)
		return NULL;
	reader = calloc(1, sizeof(*reader));
	if (!reader)
	{
		fclose(file);
		return NULL;
	}
	reader->file = file;
	*size = information.size_low;
	return reader;
}

int demo_reader_read(struct demo_reader *reader, unsigned long offset, void *data, unsigned long size)
{
	if (!reader || fseek(reader->file, (long)offset, SEEK_SET))
		return 0;
	return fread(data, 1, size, reader->file) == size;
}

void demo_reader_close(struct demo_reader *reader)
{
	if (!reader)
		return;
	fclose(reader->file);
	free(reader);
}
