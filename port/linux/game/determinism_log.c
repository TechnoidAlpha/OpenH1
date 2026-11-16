/*
DETERMINISM_LOG.C

The determinism check (debug.determinism_log): a line a tick in a file of
its own, with a hash of the game state as the tick left it, the two random
seeds, and every taker of a seed since the line before, by its call site.
Two runs of the same input, compared line by line
(tools/determinism_compare.py), first differ at the tick where something
outside the input reached the game: the line names what took a seed that
the other run did not, and which of the game state's allocations differ.

Lines:

    alloc <index> <offset> <size> <name>    each game state allocation, once
    rate <rate>                             the simulation's rate (game.sim_rate,
                                            sim_rate.c), before the first tick
                                            and when it changes
    asserts <failed> <warnings>             the run's failed assertions and
                                            warnings so far (assert_report.c),
                                            before the first tick and when
                                            they change
    t <tick> g <seed> l <seed> h <hash>     after each tick
    a <hash> <hash> ...                     the allocations' hashes, in order
    G <count>: <site>...                    the global seed's takers in the tick
    L <count>: <site>...                    the local seed's
    g, l <count>: <site>...                 ... between the ticks (the local
                                            seed's there are the frames' seed's,
                                            random_math.c)
    o <count>: <g|l><site>...               ... the other threads' (either seed)
    C <count>: <site>...                    the clock's readers in the tick, each
                                            site once
    f <tick> <frame> <hash>                 each frame drawn: its pixels' hash,
                                            the frame-th since the tick

A site is the return address in the taker (the guest's link map, or the
executable's symbols, name it); a run of one site is <site>*<count>.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "memory/memory_pool.h"
#include "objects/objects.h"
#include "tag_files/tag_groups.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

const char *config_string(char const *name);
long config_integer(char const *name);
void platform_log(const char *format, ...);
void random_math_port_seeds(unsigned long *global_seed, unsigned long *local_seed);
/* port/linux/game/sim_rate.c */
long sim_rate_get(void);

enum
{
	MAXIMUM_DETERMINISM_ALLOCATIONS = 512,
	MAXIMUM_DETERMINISM_SITES = 512,
	MAXIMUM_DETERMINISM_OTHER_SITES = 64
};

enum
{
	_determinism_takers_global_tick,
	_determinism_takers_local_tick,
	_determinism_takers_global_frame,
	_determinism_takers_local_frame,
	_determinism_takers_clock_tick,
	NUMBER_OF_DETERMINISM_TAKERS
};

struct determinism_takers
{
	unsigned long count;
	unsigned long sites[MAXIMUM_DETERMINISM_SITES];
};

struct determinism_allocation
{
	char name[32];
	void const *address;
	long size;
};

static struct
{
	boolean checked;
	boolean enabled;
	boolean in_tick;
	FILE *file;
	char path[256];
	long dump_tick;
	unsigned long base;
	long tick;
	long rate;
	long ticks_logged;
	unsigned long asserts_failed;
	unsigned long asserts_warned;
	long frames_since_tick;
	long allocation_count;
	long allocations_written;
	struct determinism_allocation allocations[MAXIMUM_DETERMINISM_ALLOCATIONS];
	struct determinism_takers takers[NUMBER_OF_DETERMINISM_TAKERS];
	volatile unsigned long other_count;
	unsigned long other_sites[MAXIMUM_DETERMINISM_OTHER_SITES];
	boolean other_local[MAXIMUM_DETERMINISM_OTHER_SITES];
} determinism_log;

/* whether random_math.c's getters log their takers */
boolean determinism_log_seeds;

/* the thread that runs the ticks (the first to end one) */
static __thread boolean determinism_main_thread;
static boolean determinism_main_thread_known;

static boolean determinism_log_enabled(
	void)
{
	if (!determinism_log.checked)
	{
		char const *path = config_string("debug.determinism_log");

		determinism_log.checked = TRUE;
		if (path && *path)
		{
			snprintf(determinism_log.path, sizeof(determinism_log.path), "%s", path);
			determinism_log.dump_tick = config_integer("debug.determinism_dump");
			determinism_log.file = fopen(path, "w");
			if (determinism_log.file)
			{
				determinism_log.enabled = TRUE;
				determinism_log_seeds = TRUE;
			}
			else
				platform_log("determinism log: cannot write %s", path);
		}
	}

	return determinism_log.enabled;
}

/* each game state allocation, as game_state.c makes it */
void determinism_log_game_state_allocation(
	char const *name,
	void const *address,
	long size)
{
	struct determinism_allocation *allocation;

	if (determinism_log.allocation_count >= MAXIMUM_DETERMINISM_ALLOCATIONS)
		return;
	allocation = &determinism_log.allocations[determinism_log.allocation_count++];
	/* (a copy: some names are made in a buffer) */
	snprintf(allocation->name, sizeof(allocation->name), "%s", name ? name : "?");
	allocation->address = address;
	allocation->size = size;
	if (!determinism_log.base || (unsigned long)address < determinism_log.base)
		determinism_log.base = (unsigned long)address;
}

/* a taker of a seed (random_math.c): its call site */
void determinism_log_seed_taken(
	boolean local,
	boolean frames,
	void const *site)
{
	struct determinism_takers *takers;

	if (!determinism_log.enabled)
		return;
	if (determinism_main_thread_known && !determinism_main_thread)
	{
		unsigned long index = __sync_fetch_and_add(&determinism_log.other_count, 1);

		if (index < MAXIMUM_DETERMINISM_OTHER_SITES)
		{
			determinism_log.other_sites[index] = (unsigned long)site;
			determinism_log.other_local[index] = local;
		}
		return;
	}
	/* (frames: the frames' local seed, taken in a tick too by what is only
	seen: random_math_port_begin_cosmetic) */
	takers = &determinism_log.takers[(local ? 1 : 0) + (determinism_log.in_tick && !frames ? 0 : 2)];
	if (takers->count < MAXIMUM_DETERMINISM_SITES)
		takers->sites[takers->count] = (unsigned long)site;
	takers->count++;
}

/* a read of the clock (cseries_windows.c's system_milliseconds and
system_seconds): its call site, if in a tick (the ticks should read only the
game's time) */
void determinism_log_clock_read(
	void const *site)
{
	struct determinism_takers *takers = &determinism_log.takers[_determinism_takers_clock_tick];
	unsigned long known;
	unsigned long index;

	if (!determinism_log.enabled || !determinism_log.in_tick ||
		(determinism_main_thread_known && !determinism_main_thread))
	{
		return;
	}
	/* (each site once: the readers are compared by their sites, not how
	often they read, and a load that waits for the disk reads in a loop, as
	often as the disk takes, which filled the list and left the sites after
	it out) */
	known = takers->count < MAXIMUM_DETERMINISM_SITES ? takers->count : MAXIMUM_DETERMINISM_SITES;
	for (index = 0; index < known; index++)
	{
		if (takers->sites[index] == (unsigned long)site)
			return;
	}
	if (takers->count < MAXIMUM_DETERMINISM_SITES)
		takers->sites[takers->count] = (unsigned long)site;
	takers->count++;
}

/* game_tick's start and end (game_time.c) */
void determinism_log_tick_begin(
	void)
{
	if (!determinism_log_enabled())
		return;
	if (!determinism_main_thread_known)
	{
		determinism_main_thread = TRUE;
		determinism_main_thread_known = TRUE;
	}
	determinism_log.in_tick = TRUE;
}

/* a tick left open between two of its sub-steps (game.sim_rate's spread
over the frames: game_time.c), the frames meanwhile not the tick's; the
next sub-step begins it again (determinism_log_tick_begin) and the last
writes its line */
void determinism_log_tick_pause(
	void)
{
	determinism_log.in_tick = FALSE;
}

static unsigned long determinism_hash(
	void const *address,
	long size,
	unsigned long hash)
{
	unsigned char const *bytes = address;
	unsigned long const *words = address;
	long word_count = size / (long)sizeof(unsigned long);
	long index;

	/* (FNV-1a over words, then the bytes left) */
	for (index = 0; index < word_count; index++)
		hash = (hash ^ words[index]) * 16777619UL;
	for (index = word_count * (long)sizeof(unsigned long); index < size; index++)
		hash = (hash ^ bytes[index]) * 16777619UL;

	return hash;
}

/* whether an object's attachment of this type is a datum of the frames'
allocations (tools/determinism_compare.py's FRAME_ALLOCATIONS): a light, a
looping sound, a contrail or a particle system. The tick makes it with the
object (objects.c's attachments_new), but in a slot of an array the frames
free slots of too (a contrail whose points have run out, a light's fade, a
sound the mixer let go), so the index the object keeps is the frames' as
much as the slot is: two runs of one game whose lobbies differed in length
kept a projectile's tracer, a contrail, at index 0 and at 1. Nothing of the
ticks but the frames' own work goes by the index (an effect's, the
simulation's, is compared) */
static boolean determinism_frames_attachment(
	char type)
{
	return type == _object_attachment_type_light ||
		type == _object_attachment_type_looping_sound ||
		type == _object_attachment_type_contrail ||
		type == _object_attachment_type_particle_system;
}

/* the objects' memory pool: its header and its blocks, but for each
object's magic number, the collision tests' mark (objects.c's
object_mark_function), which the tests of the frames (the sounds'
obstruction, the camera's) leave as well as the ticks', and which means
nothing between two tests, the indices of its attachments that are the
frames' (above), and its cached render state's index, which the renderer
gives it as it draws it; and not the free space after the last block,
which holds what deleted objects left there */
static unsigned long determinism_hash_objects(
	struct memory_pool const *pool,
	long size,
	unsigned long hash)
{
	byte const *start = (byte const *)pool;
	byte const *end = start + size;
	byte const *last_end = start + sizeof(*pool);
	struct memory_pool_block const *block = pool->first_block;
	long blocks = 0;

	while (block && (byte const *)block >= start && (byte const *)(block + 1) <= end && blocks++ < size)
	{
		struct object_datum const *object = (struct object_datum const *)(block + 1);
		byte const *block_end = (byte const *)block + block->size;
		/* (the fields left out, in their order in the object) */
		byte const *skipped[2 + MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT];
		short skipped_count = 0;
		short index;

		if ((byte const *)(&object->object.magic_number + 1) > end || block->size < (long)sizeof(*block) ||
			block_end > end)
		{
			break;
		}
		skipped[skipped_count++] = (byte const *)&object->object.magic_number;
		if ((byte const *)(&object->object.attachment_indices[MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT]) <= block_end)
		{
			/* (the attachments attachments_new made, the definition's count:
			the slots past it are as the object was made) */
			short attachment_count = (short)MIN(
				object_definition_get(object->definition_index)->object.attachments.count,
				MAXIMUM_NUMBER_OF_ATTACHMENTS_PER_OBJECT);

			for (index = 0; index < attachment_count; index++)
			{
				if (determinism_frames_attachment(object->object.attachment_types[index]))
					skipped[skipped_count++] = (byte const *)&object->object.attachment_indices[index];
			}
		}
		if ((byte const *)(&object->object.cached_render_state_index + 1) <= block_end)
			skipped[skipped_count++] = (byte const *)&object->object.cached_render_state_index;
		for (index = 0; index < skipped_count; index++)
		{
			/* (each a long) */
			hash = determinism_hash(start, (long)(skipped[index] - start), hash);
			start = skipped[index] + sizeof(long);
		}
		last_end = block_end;
		block = block->next_block;
	}

	return determinism_hash(start, (long)(last_end > start ? last_end - start : 0), hash);
}

/* a data array: its header and its datums in use, not its free slots,
which hold the fill (or what a deleted datum left), so that a zero and a
pattern run differ only where something read the fill (debug.fresh_memory_
fill), and a demo's playback does not differ from its recording where its
frames, not the ticks, made a datum and deleted it again (each tick's u
line) */
static unsigned long determinism_hash_data_array(
	struct data_array const *data,
	long size,
	unsigned long hash)
{
	long index;

	hash = determinism_hash(data, sizeof(*data), hash);
	for (index = 0; index < data->maximum_count; index++)
	{
		byte const *datum = (byte const *)data->data + (long)data->size * index;

		if (datum + data->size > (byte const *)data + size)
			break;
		if (((struct datum_header const *)datum)->identifier)
			hash = determinism_hash(datum, data->size, hash);
	}
	return hash;
}

static void determinism_write_takers(
	char const *label,
	unsigned long count,
	unsigned long const *sites,
	unsigned long site_capacity)
{
	unsigned long index;
	unsigned long known = count < site_capacity ? count : site_capacity;

	fprintf(determinism_log.file, "%s %lu:", label, count);
	for (index = 0; index < known;)
	{
		unsigned long run = 1;

		while (index + run < known && sites[index + run] == sites[index])
			run++;
		if (run > 1)
			fprintf(determinism_log.file, " %lx*%lu", sites[index], run);
		else
			fprintf(determinism_log.file, " %lx", sites[index]);
		index += run;
	}
	fputc('\n', determinism_log.file);
}

void determinism_log_tick_end(
	long tick)
{
	static char const *const labels[NUMBER_OF_DETERMINISM_TAKERS] = { "G", "L", "g", "l", "C" };
	FILE *file = determinism_log.file;
	unsigned long hash = 2166136261UL;
	unsigned long global_seed, local_seed;
	unsigned long asserts_failed, asserts_warned;
	unsigned long other_count;
	long index;

	if (!determinism_log.enabled)
		return;
	determinism_log.in_tick = FALSE;
	determinism_log.tick = tick;
	determinism_log.frames_since_tick = 0;
	for (; determinism_log.allocations_written < determinism_log.allocation_count; determinism_log.allocations_written++)
	{
		struct determinism_allocation const *allocation = &determinism_log.allocations[determinism_log.allocations_written];

		fprintf(file, "alloc %ld %lx %ld %s\n", determinism_log.allocations_written,
			(unsigned long)allocation->address - determinism_log.base, allocation->size, allocation->name);
	}
	fputc('a', file);
	for (index = 0; index < determinism_log.allocation_count; index++)
	{
		struct determinism_allocation const *allocation = &determinism_log.allocations[index];
		unsigned long allocation_hash;

		if (allocation->size >= (long)sizeof(struct memory_pool) && !strcmp(allocation->name, "objects") &&
			((struct memory_pool const *)allocation->address)->signature == 'pool')
		{
			allocation_hash = determinism_hash_objects(allocation->address, allocation->size, 2166136261UL);
		}
		else if (halo_fresh_memory_fill() >= 0 && allocation->size >= (long)sizeof(struct data_array) &&
			((struct data_array const *)allocation->address)->signature == 'd@t@' &&
			((struct data_array const *)allocation->address)->data == (struct data_array const *)allocation->address + 1)
		{
			allocation_hash = determinism_hash_data_array(allocation->address, allocation->size, 2166136261UL);
		}
		else
			allocation_hash = determinism_hash(allocation->address, allocation->size, 2166136261UL);

		fprintf(file, " %08lx", allocation_hash);
		hash = (hash ^ allocation_hash) * 16777619UL;
	}
	fputc('\n', file);
	/* (each allocation's hash again, its data arrays' of their datums in use
	only: what the ticks can read of them, determinism_compare.py's
	--simulation) */
	fputc('u', file);
	for (index = 0; index < determinism_log.allocation_count; index++)
	{
		struct determinism_allocation const *allocation = &determinism_log.allocations[index];

		if (allocation->size >= (long)sizeof(struct data_array) &&
			((struct data_array const *)allocation->address)->signature == 'd@t@' &&
			((struct data_array const *)allocation->address)->data == (struct data_array const *)allocation->address + 1)
		{
			fprintf(file, " %08lx", determinism_hash_data_array(allocation->address, allocation->size, 2166136261UL));
		}
		else
			fputs(" =", file);
	}
	fputc('\n', file);
	if (determinism_log.rate != sim_rate_get())
	{
		determinism_log.rate = sim_rate_get();
		fprintf(file, "rate %ld\n", determinism_log.rate);
	}
	assert_report_counts(&asserts_failed, &asserts_warned);
	if (!determinism_log.ticks_logged || asserts_failed != determinism_log.asserts_failed ||
		asserts_warned != determinism_log.asserts_warned)
	{
		determinism_log.asserts_failed = asserts_failed;
		determinism_log.asserts_warned = asserts_warned;
		fprintf(file, "asserts %lu %lu\n", asserts_failed, asserts_warned);
	}
	random_math_port_seeds(&global_seed, &local_seed);
	fprintf(file, "t %ld g %08lx l %08lx h %08lx\n", tick, global_seed, local_seed, hash);
	for (index = 0; index < NUMBER_OF_DETERMINISM_TAKERS; index++)
	{
		struct determinism_takers *takers = &determinism_log.takers[index];

		if (takers->count)
			determinism_write_takers(labels[index], takers->count, takers->sites, MAXIMUM_DETERMINISM_SITES);
		takers->count = 0;
	}
	other_count = __sync_lock_test_and_set(&determinism_log.other_count, 0);
	if (other_count)
	{
		unsigned long known = other_count < MAXIMUM_DETERMINISM_OTHER_SITES ? other_count : MAXIMUM_DETERMINISM_OTHER_SITES;
		unsigned long site_index;

		fprintf(file, "o %lu:", other_count);
		for (site_index = 0; site_index < known; site_index++)
			fprintf(file, " %c%lx", determinism_log.other_local[site_index] ? 'l' : 'g', determinism_log.other_sites[site_index]);
		fputc('\n', file);
	}
	fflush(file);
	/* (counted as logged: a tick's number comes again after a checkpoint is
	loaded, as when a cinematic is skipped) */
	if (++determinism_log.ticks_logged == determinism_log.dump_tick)
	{
		char dump_path[300];
		FILE *dump;

		/* the allocations one after another, as the alloc lines list them */
		snprintf(dump_path, sizeof(dump_path), "%s.%ld.bin", determinism_log.path, determinism_log.ticks_logged);
		dump = fopen(dump_path, "wb");
		if (dump)
		{
			for (index = 0; index < determinism_log.allocation_count; index++)
				fwrite(determinism_log.allocations[index].address, 1, determinism_log.allocations[index].size, dump);
			fclose(dump);
		}
	}
}

/* whether the frames are logged too (d3d8_device.c reads each back for
determinism_log_frame) */
int determinism_log_frames(
	void)
{
	return determinism_log_enabled();
}

void determinism_log_frame(
	unsigned char const *pixels,
	long width,
	long height)
{
	unsigned long hash = 2166136261UL;
	long index;

	if (!determinism_log.enabled)
		return;
	/* (the colours, not destination alpha, which the game uses as scratch) */
	for (index = 0; index < width * height; index++)
	{
		unsigned long pixel = (unsigned long)pixels[index * 4] | (unsigned long)pixels[index * 4 + 1] << 8 |
			(unsigned long)pixels[index * 4 + 2] << 16;

		hash = (hash ^ pixel) * 16777619UL;
	}
	fprintf(determinism_log.file, "f %ld %ld %08lx\n", determinism_log.tick, determinism_log.frames_since_tick, hash);
	/* (with debug.determinism_dump, the dumped tick's first frame too, as a
	BMP of 32-bit pixels, rows from the top) */
	if (determinism_log.dump_tick && determinism_log.ticks_logged == determinism_log.dump_tick &&
		!determinism_log.frames_since_tick)
	{
		char path[300];
		unsigned char header[54] = { 'B', 'M' };
		FILE *file;

		snprintf(path, sizeof(path), "%s.%ld.bmp", determinism_log.path, determinism_log.ticks_logged);
		file = fopen(path, "wb");
		if (file)
		{
			unsigned long image_size = (unsigned long)(width * height * 4);

			header[2] = (unsigned char)(54 + image_size);
			header[3] = (unsigned char)((54 + image_size) >> 8);
			header[4] = (unsigned char)((54 + image_size) >> 16);
			header[5] = (unsigned char)((54 + image_size) >> 24);
			header[10] = 54;
			header[14] = 40;
			header[18] = (unsigned char)width;
			header[19] = (unsigned char)(width >> 8);
			/* (a negative height: rows from the top) */
			header[22] = (unsigned char)-height;
			header[23] = (unsigned char)(-height >> 8);
			header[24] = 0xff;
			header[25] = 0xff;
			header[26] = 1;
			header[28] = 32;
			fwrite(header, 1, sizeof(header), file);
			fwrite(pixels, 1, image_size, file);
			fclose(file);
		}
	}
	determinism_log.frames_since_tick++;
}
