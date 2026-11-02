/* The objects' memory pool (source/memory/memory_pool.c), as a release
build checks it: blocks allocated, grown, freed and compacted keep their
contents and references with no assertion failed; a block whose neighbours
do not name it is caught; and the checks of an allocation or a lookup cost
the same however many blocks the pool has (a load zone placing hundreds of
objects into a pool of thousands). */
#include "harness.h"
#undef match_assert
#define HALO_RELEASE
typedef unsigned short word;
#define csmemset memset
#define csmemmove memmove
#define csmemcpy memcpy
#define temporary ""
#define csprintf(buffer, ...) ""
/* each assertion evaluated is counted; a failed one too */
static long assertions_evaluated, assertions_failed;
#define match_assert(file, line, expr) { assertions_evaluated++; if (!(expr)) assertions_failed++; }
#define match_vassert(file, line, expr, string) match_assert(file, line, expr)
#define POOL_SIGNATURE 'pool'
#define BLOCK_HEADER_SIGNATURE 'head'
#define BLOCK_TRAILER_SIGNATURE 'tail'
#include "types.inc"
#include "under_test.inc"

#define BLOCKS 2000
static void *references[BLOCKS];

static struct memory_pool *pool_new(long size)
{
	struct memory_pool *pool = calloc(1, sizeof(struct memory_pool) + size);

	pool->signature = POOL_SIGNATURE;
	pool->base_address = pool + 1;
	pool->size = pool->free_size = size;
	return pool;
}

static void fill(long index, long size)
{
	memset(references[index], (int)(index & 0xff), size);
}

static boolean filled(long index, long size)
{
	long byte_index;

	for (byte_index = 0; byte_index < size; byte_index++)
	{
		if (((unsigned char *)references[index])[byte_index] != (unsigned char)(index & 0xff))
			return FALSE;
	}
	return TRUE;
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";
	struct memory_pool *pool = pool_new(BLOCKS * 256);
	long index;

	for (index = 0; index < BLOCKS; index++)
	{
		CHECK(memory_pool_block_allocate(pool, &references[index], 32), "block %ld allocated", index);
		fill(index, 32);
	}
	if (!strcmp(name, "lifetime"))
	{
		for (index = 0; index < BLOCKS; index += 3)
		{
			CHECK(memory_pool_block_reallocate(pool, &references[index], 64), "block %ld grown", index);
			CHECK(filled(index, 32), "block %ld kept its bytes as it grew", index);
			fill(index, 64);
		}
		for (index = 1; index < BLOCKS; index += 3)
		{
			memory_pool_block_free(pool, &references[index]);
			references[index] = NULL;
		}
		memory_pool_compact(pool);
		for (index = 0; index < BLOCKS; index++)
		{
			if (references[index])
				CHECK(filled(index, index % 3 ? 32 : 64), "block %ld kept its bytes through the compaction", index);
		}
		CHECK(!assertions_failed, "%ld assertions failed of a pool used as the game uses it", assertions_failed);
		return 0;
	}
	if (!strcmp(name, "unlinked"))
	{
		struct memory_pool_block *block = (struct memory_pool_block *)references[BLOCKS / 2] - 1;

		block->previous_block->next_block = block->next_block;
		memory_pool_block_get(pool, &references[BLOCKS / 2]);
		CHECK(assertions_failed, "a block its neighbours do not name is caught");
		return 0;
	}
	if (!strcmp(name, "constant"))
	{
		long evaluated;

		assertions_evaluated = 0;
		memory_pool_block_get(pool, &references[BLOCKS / 2]);
		CHECK(memory_pool_block_allocate(pool, &references[0], 16), "another block allocated");
		evaluated = assertions_evaluated;
		CHECK(evaluated < 40, "a lookup and an allocation checked %ld times in a pool of %d blocks", evaluated, BLOCKS);
		CHECK(!assertions_failed, "%ld assertions failed", assertions_failed);
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
