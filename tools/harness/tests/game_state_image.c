/*
GAME_STATE_IMAGE.C (test)

The game state's image (saved games/game_state.c): a checkpoint's holds the
part of the game state in use, the CPU's allocations then the GPU's, the
bytes of the game state there; taken back, those are what the game state
had, from it or from an image of the whole game state (an older build's
file, a core, Save and Quit's), what is checked is found at the same place
in both, and an image of another size is refused. test_game_state_image.py
takes the code from the sources; the game state and its allocations are
the test's.
*/

#include "harness.h"

#include "types.inc"

#define csmemcpy memcpy
#define error(level, ...) ((void)0)

static struct
{
	void *base_address;
	long cpu_allocation_size;
	long gpu_allocation_size;
} game_state_globals;

/* the allocations' checks: a data array's is the test's, which finds the
array's signature where the image has the array */
struct game_state_allocation
{
	short kind;
	short maximum_count;
	short element_size;
	void *address;
	long size;
};

static struct game_state_allocation game_state_allocations[MAXIMUM_GAME_STATE_ALLOCATIONS];
static long game_state_allocation_count;

static void *game_state_image_pointer(byte *image, void const *address);

#define TEST_SIGNATURE 0x64407440UL

static boolean game_state_image_data_valid(byte *image, void *live, short maximum_count, short element_size)
{
	(void)maximum_count, (void)element_size;
	return *(unsigned long *)game_state_image_pointer(image, live) == TEST_SIGNATURE;
}

#define game_state_image_memory_pool_valid(image, address, size) FALSE
#define game_state_image_lruv_cache_valid(image, address, allocation) FALSE

/* no MCC map (an MCC map's image is the whole game state: mcc_checkpoint.c's
tests), and the console's state always good (hs_console_state.inl's) */
#define mcc_cache_tags_loaded() FALSE
#define mcc_checkpoint_validate(image, size, used, cpu_size, live) TRUE
#define mcc_checkpoint_restore(image, size, used, cpu_size) TRUE
#define hs_console_state_validate(image, live, size) TRUE
#define hs_console_state_restore() ((void)0)

#include "under_test.inc"

/* ---------- the cases */

#define TEST_CPU_SIZE 0x123454L
#define TEST_GPU_SIZE 0x28000L
#define TEST_UNUSED 0x5A

static byte *test_state;

static byte test_byte(long offset, byte seed)
{
	return (byte)(seed + offset * 13 + (offset >> 11));
}

/* the parts in use filled from the seed, the rest TEST_UNUSED, and a data
array's signature at its place */
static void test_state_fill(byte seed)
{
	long offset;

	for (offset = 0; offset < GAME_STATE_SIZE; offset++)
	{
		boolean used = offset < TEST_CPU_SIZE || offset >= GAME_STATE_SIZE - TEST_GPU_SIZE;

		test_state[offset] = used ? test_byte(offset, seed) : TEST_UNUSED;
	}
	*(unsigned long *)game_state_allocations[0].address = TEST_SIGNATURE;
}

static boolean test_state_is(byte seed)
{
	long offset;

	for (offset = 0; offset < GAME_STATE_SIZE; offset++)
	{
		boolean used = offset < TEST_CPU_SIZE || offset >= GAME_STATE_SIZE - TEST_GPU_SIZE;
		byte expected = used ? test_byte(offset, seed) : TEST_UNUSED;

		if (offset >= 0x1000 && offset < 0x1004)
			continue;
		if (test_state[offset] != expected)
		{
			fprintf(stderr, "the game state differs at %lx\n", offset);
			return FALSE;
		}
	}
	return *(unsigned long *)game_state_allocations[0].address == TEST_SIGNATURE;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	long size;
	byte *image;

	test_state = malloc(GAME_STATE_SIZE);
	CHECK(test_state, "no memory for the game state");
	game_state_globals.base_address = test_state;
	game_state_globals.cpu_allocation_size = TEST_CPU_SIZE;
	game_state_globals.gpu_allocation_size = TEST_GPU_SIZE;
	game_state_allocations[0].kind = _game_state_allocation_data;
	game_state_allocations[0].address = test_state + 0x1000;
	game_state_allocation_count = 1;
	test_state_fill(1);

	/* the part in use, not the whole game state */
	CASE("size")
	{
		CHECK(game_state_image_size() == TEST_CPU_SIZE + TEST_GPU_SIZE, "the image is %ld bytes",
			game_state_image_size());
		CHECK(game_state_image_size() < GAME_STATE_SIZE, "the image is not smaller than the game state");
	}

	size = game_state_image_size();
	image = malloc(GAME_STATE_SIZE);
	CHECK(image, "no memory for the image");

	/* the CPU's allocations, then the GPU's, as the game state has them */
	CASE("taken")
	{
		game_state_image_take(image);
		CHECK(!memcmp(image, test_state, TEST_CPU_SIZE), "the CPU's allocations are not the game state's");
		CHECK(!memcmp(image + TEST_CPU_SIZE, test_state + GAME_STATE_SIZE - TEST_GPU_SIZE, TEST_GPU_SIZE),
			"the GPU's allocations are not the game state's");
	}

	/* taken back: the parts in use as they were, the rest as it is */
	CASE("taken-back")
	{
		game_state_image_take(image);
		test_state_fill(2);
		CHECK(game_state_image_accept(image, size), "the image was refused");
		CHECK(test_state_is(1), "the game state is not the image's");
	}

	/* an image of the whole game state (an older build's checkpoint, a core,
	Save and Quit's) taken back as it was */
	CASE("whole")
	{
		memcpy(image, test_state, GAME_STATE_SIZE);
		test_state_fill(2);
		CHECK(game_state_image_accept(image, GAME_STATE_SIZE), "the image of the whole game state was refused");
		CHECK(test_state_is(1), "the game state is not the whole image's");
	}

	/* what is checked is found in the image of either size, and an image
	that fails is not taken */
	CASE("checked")
	{
		game_state_image_take(image);
		*(unsigned long *)(image + 0x1000) = 0;
		test_state_fill(2);
		CHECK(!game_state_image_accept(image, size), "a damaged image was taken");
		CHECK(test_state_is(2), "a damaged image changed the game state");
		test_state_fill(1);
		memcpy(image, test_state, GAME_STATE_SIZE);
		*(unsigned long *)(image + 0x1000) = 0;
		test_state_fill(2);
		CHECK(!game_state_image_accept(image, GAME_STATE_SIZE), "a damaged whole image was taken");
		CHECK(test_state_is(2), "a damaged whole image changed the game state");
	}

	/* an image of another size is refused */
	CASE("other-size")
	{
		game_state_image_take(image);
		test_state_fill(2);
		CHECK(!game_state_image_accept(image, size - 4), "an image of another size was taken");
		CHECK(!game_state_image_accept(image, size + 4), "an image of another size was taken");
		CHECK(test_state_is(2), "an image of another size changed the game state");
	}

	free(image);
	free(test_state);
	return 0;
}
