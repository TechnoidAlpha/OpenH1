/*
RASTERIZER_MEMORY_POOL.C (test)

The real rasterizer memory pool (rasterizer_memory_pool.c) and the real
rasterizer_transparent_geometry_flush (rasterizer_transparent_geometry.c). A
map's first frame (port/linux/game/render_enumerate.c) draws every model in
one window: the transparent geometry's queue is drawn and emptied many times,
and the pool must be released with it, or the window spends it (c10's
characters did, and a camouflaged model was then drawn from a NULL
animation). What the fog screen keeps for the window's end must survive the
release. test_rasterizer_memory_pool.py takes the code (under_test.inc); the
pool is small here, its logic the same.
*/

#include "harness.h"

#define RASTERIZER_MEMORY_POOL_SIZE 4096
#define RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS 64
#define RASTERIZER_MAXIMUM_TRANSPARENT_GEOMETRY_GROUPS2 16

enum { _error_silent };

static long errors_logged;
static long draws;

#define error(priority, ...) ((void)(priority), errors_logged++)
#define match_malloc(file, line, size) malloc(size)
#define match_free(file, line, pointer) free(pointer)

static long transparent_geometry_group_count;
static long transparent_geometry_group_count2;

static void rasterizer_transparent_geometry_draw(boolean first) { (void)first; draws++; }
static void rasterizer_transparent_geometry_begin(void) { transparent_geometry_group_count = transparent_geometry_group_count2 = 0; }

boolean rasterizer_memory_pool_half_spent(void);
void rasterizer_memory_pool_release_queued(void);

#include "under_test.inc"

static int overlaps(const void *a, unsigned long a_size, const void *b, unsigned long b_size)
{
	return (const byte *)a < (const byte *)b + b_size && (const byte *)b < (const byte *)a + a_size;
}

static int inside(const void *pointer, unsigned long size)
{
	return (const byte *)pointer >= (const byte *)global_memory_pool &&
		(const byte *)pointer + size <= (const byte *)global_memory_pool + RASTERIZER_MEMORY_POOL_SIZE;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	byte pattern[256];
	byte zeros[256];
	long index;

	for (index = 0; index < (long)sizeof(pattern); index++)
		pattern[index] = (byte)(index * 7 + 1);
	memset(zeros, 0, sizeof(zeros));
	CHECK(rasterizer_memory_pool_initialize(), "the pool was not allocated");
	rasterizer_memory_pool_begin();

	/* the fog screen's memory, kept for the window, survives the queue's
	release; the queue's is used again */
	CASE("window-memory-survives-release")
	{
		const void *queued = rasterizer_memory_alloc_const(zeros, 200);
		const void *kept = rasterizer_memory_alloc_const_for_window(pattern, sizeof(pattern));
		const void *again;

		CHECK(queued && kept, "an allocation failed in an empty pool");
		rasterizer_memory_pool_release_queued();
		again = rasterizer_memory_alloc_const(zeros, 256);
		CHECK(again == queued, "the released queue's memory was not used again");
		CHECK(!overlaps(again, 256, kept, sizeof(pattern)), "the queue's memory is the window's");
		CHECK(!memcmp(kept, pattern, sizeof(pattern)), "the window's memory changed");
		return 0;
	}
	/* allocated from both ends until it is spent: inside the pool, never
	the same memory, and then refused with one error */
	CASE("never-overlap")
	{
		const void *pointers[128];
		unsigned long sizes[128];
		long count = 0, other, attempt;

		/* (each kind tried until neither fits) */
		for (attempt = 0; attempt < 1000; attempt++)
		{
			unsigned long size = 52 * (unsigned long)(attempt % 5 + 1);
			const void *pointer = attempt & 1 ?
				rasterizer_memory_alloc_const_for_window(pattern, size) :
				rasterizer_memory_alloc_const(pattern, size);

			if (!pointer)
				continue;
			CHECK(count < 128, "the pool held more than it can");
			CHECK(inside(pointer, size), "allocation %ld is outside the pool", count);
			for (other = 0; other < count; other++)
				CHECK(!overlaps(pointer, size, pointers[other], sizes[other]),
					"allocations %ld and %ld overlap", other, count);
			pointers[count] = pointer;
			sizes[count++] = size;
		}
		CHECK(count > RASTERIZER_MEMORY_POOL_SIZE / (52 * 5), "the pool held only %ld allocations", count);
		CHECK(!rasterizer_memory_alloc_const_for_window(pattern, 52) && !rasterizer_memory_alloc_const(pattern, 52),
			"the spent pool gave more");
		CHECK(errors_logged == 1, "the spent pool was logged %ld times", errors_logged);
		return 0;
	}
	/* a queue that has spent half the pool is drawn, and its memory
	released, though it has room for groups */
	CASE("flush-when-half-spent")
	{
		rasterizer_memory_alloc_const_for_window(pattern, 256);
		transparent_geometry_group_count = 1;
		while (!rasterizer_memory_pool_half_spent())
			CHECK(rasterizer_memory_alloc_const(zeros, 208), "the pool was spent before half of it");
		rasterizer_transparent_geometry_flush(8);
		CHECK(draws == 2, "the queue was drawn %ld times", draws);
		CHECK(transparent_geometry_group_count == 0, "the queue was not emptied");
		CHECK(rasterizer_memory_alloc_const(zeros, 208) == global_memory_pool, "the queue's memory was not released");
		CHECK(rasterizer_memory_alloc_const(zeros, RASTERIZER_MEMORY_POOL_SIZE - 256 - 208),
			"the released pool does not hold what the window does not keep");
		return 0;
	}
	/* a queue with room in the pool and for groups is left as it is */
	CASE("no-flush-with-room")
	{
		const void *first = rasterizer_memory_alloc_const(zeros, 208);

		transparent_geometry_group_count = 1;
		rasterizer_transparent_geometry_flush(8);
		CHECK(draws == 0, "a queue with room was drawn");
		CHECK(rasterizer_memory_alloc_const(zeros, 52) == (const byte *)first + 208, "the queue's memory was released");
		return 0;
	}
	/* the next window has the whole pool again */
	CASE("window-begin-resets")
	{
		rasterizer_memory_alloc_const_for_window(pattern, RASTERIZER_MEMORY_POOL_SIZE / 2);
		rasterizer_memory_alloc_const(zeros, RASTERIZER_MEMORY_POOL_SIZE / 2);
		CHECK(!rasterizer_memory_alloc_const(zeros, 4), "a spent pool gave more");
		rasterizer_memory_pool_begin();
		CHECK(rasterizer_memory_alloc_const(zeros, RASTERIZER_MEMORY_POOL_SIZE) == global_memory_pool,
			"a new window does not have the whole pool");
		return 0;
	}
	fprintf(stderr, "unknown case '%s'\n", case_name);
	return 2;
}
