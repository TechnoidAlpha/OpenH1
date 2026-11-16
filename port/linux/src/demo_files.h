/*
DEMO_FILES.H

The demos' files (port/linux/game/demo.c, docs/demos.md): the folder they
are kept in, the writer that packs and appends their blocks on a thread of
its own, the reader's unpacking, and their retention.
*/

#ifndef __HALO_LINUX_DEMO_FILES_H
#define __HALO_LINUX_DEMO_FILES_H

enum
{
	DEMO_FILE_NAME_SIZE = 96,
	DEMO_FILES_MAXIMUM_LISTED = 256,
};

struct demo_file_entry
{
	char name[DEMO_FILE_NAME_SIZE];
	unsigned long size;
	unsigned long modified;
	int pinned;
};

/* the folder of the demos (paths.data's demos/), made if it is not there,
and the folder of the pinned ones (demos/kept/): 0 if it cannot be */
int demo_files_folder(char *path, unsigned long size, int pinned);
/* a demo's file by its name, in either folder: 0 if neither has it */
int demo_files_path(char const *name, char *path, unsigned long size);

/* the oldest demos deleted, but the pinned ones and skip (the one being
written), until no more than count are kept and they take no more than
megabytes */
void demo_files_retain(long count, long megabytes, char const *skip);

/* every demo, the newest first; the count listed */
int demo_files_list(struct demo_file_entry *entries, int maximum);
/* a demo moved to the pinned folder or back: whether it was */
int demo_files_pin(char const *name, int pinned);
/* a demo deleted: whether it was */
int demo_files_delete(char const *name);

/* the writer: the file made with its header; each block handed on is packed
(zlib) and appended in order, off the caller's thread; closed once every
block is written. A block too many for the writer's queue is dropped if
droppable (a seek's snapshot), and the caller waits for room otherwise. */
struct demo_writer;
struct demo_writer *demo_writer_open(char const *path, void const *header, unsigned long header_size);
void demo_writer_block(struct demo_writer *writer, unsigned long kind, unsigned long first_tick,
	void const *data, unsigned long size, int droppable);
/* the bytes written so far (the blocks packed); and once closed, all */
unsigned long long demo_writer_bytes(struct demo_writer *writer);
unsigned long long demo_writer_close(struct demo_writer *writer);

/* a block's packed bytes unpacked (with the port's zlib: the file is
anyone's): whether they made exactly size bytes */
int demo_files_unpack(void const *packed, unsigned long packed_size, void *data, unsigned long size);

/* memory for the demos' buffers (the C library's: the game's own allocator is
for the game's thread, its blocks checked and counted) */
void *demo_files_alloc(unsigned long size);
void *demo_files_realloc(void *address, unsigned long size);
void demo_files_free(void *address);

/* a demo read: opened (NULL if it cannot be), its size, a read of size
bytes at offset (whether they were all read), closed */
struct demo_reader;
struct demo_reader *demo_reader_open(char const *path, unsigned long *size);
int demo_reader_read(struct demo_reader *reader, unsigned long offset, void *data, unsigned long size);
void demo_reader_close(struct demo_reader *reader);

/* the file's block header: its magic, the kind of block, the tick it
begins at, its size unpacked and packed, then the packed bytes */
#define DEMO_BLOCK_MAGIC 0x4B4C4244UL /* "DBLK" */
enum
{
	DEMO_BLOCK_HEADER_SIZE = 20,
	/* the largest block unpacked: a snapshot of the whole game state, and
	a second of records */
	DEMO_BLOCK_MAXIMUM_SIZE = 64 << 20,
};

#endif
