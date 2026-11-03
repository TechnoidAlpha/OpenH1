/*
RASTERIZER_MEMORY_POOL.C
*/

/* ---------- headers */

#include "cseries.h"
#include "errors.h"
#include "real_math.h"
#include "rasterizer.h"
/* Empty reconstructed header removed; no declarations to include. */

/* ---------- globals */

/* swapped? */
static void *global_memory_pool;
static long global_memory_index;
/* port: the start of what is kept for the whole window, allocated from the
pool's end down (rasterizer_memory_alloc_const_for_window): the rest, the
transparent geometry's, is released when its queue is drawn mid-window */
static long global_memory_window_start = RASTERIZER_MEMORY_POOL_SIZE;
/* port: whether the pool ran out, logged once (a frame that runs out
usually means every frame does) */
static boolean pool_exceeded_logged;

/* ---------- public code */

boolean rasterizer_memory_pool_initialize(
	void)
{
	boolean success = TRUE;
	global_memory_pool = match_malloc("c:\\halo\\SOURCE\\rasterizer\\rasterizer_memory_pool.c", 19, RASTERIZER_MEMORY_POOL_SIZE);

	if (!global_memory_pool)
	{
		error(_error_silent, "### ERROR rasterizer failed to allocate global memory pool");
		success = FALSE;
	}

	return success;
}

void rasterizer_memory_pool_begin(
	void)
{
	global_memory_index = 0;
	global_memory_window_start = RASTERIZER_MEMORY_POOL_SIZE;

	return;
}

void *rasterizer_memory_alloc(
	const void *src,
	unsigned long size)
{
	void *pointer = NULL;

	/* port: below what is kept for the window */
	if (size <= (unsigned long)(global_memory_window_start-global_memory_index))
	{
		pointer = (byte *)global_memory_pool+global_memory_index;
		global_memory_index+= size;

		if (src)
		{
			memcpy(pointer, src, size);
		}
	}
	else if (!pool_exceeded_logged)
	{
		pool_exceeded_logged = TRUE;
		error(_error_silent, "### ERROR rasterizer memory pool exceeded (not logged again)");
	}

	return pointer;
}

const void *rasterizer_memory_alloc_const(
	const void *src,
	unsigned long size)
{
	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_memory_pool.c", 66, src);
	return rasterizer_memory_alloc(src, size);
}

/* port: memory kept until the window ends, through rasterizer_memory_pool_
release_queued: the environment fog screen's opaque models are drawn at the
window's end, after a map's first frame (port/linux/game/render_enumerate.c)
has drawn and emptied the transparent geometry's queue many times */
const void *rasterizer_memory_alloc_const_for_window(
	const void *src,
	unsigned long size)
{
	void *pointer = NULL;

	if (size <= (unsigned long)(global_memory_window_start-global_memory_index))
	{
		global_memory_window_start-= size;
		pointer = (byte *)global_memory_pool+global_memory_window_start;
		memcpy(pointer, src, size);
	}
	else if (!pool_exceeded_logged)
	{
		pool_exceeded_logged = TRUE;
		error(_error_silent, "### ERROR rasterizer memory pool exceeded (not logged again)");
	}

	return pointer;
}

/* port: whether half the pool is spent: the transparent geometry's queue is
drawn and emptied then (rasterizer_transparent_geometry_flush) */
boolean rasterizer_memory_pool_half_spent(
	void)
{
	return global_memory_index+(RASTERIZER_MEMORY_POOL_SIZE-global_memory_window_start) >
		RASTERIZER_MEMORY_POOL_SIZE/2;
}

/* port: the memory of the transparent geometry's queue released, once it is
drawn and emptied (rasterizer_transparent_geometry_flush): what is kept for
the window stays */
void rasterizer_memory_pool_release_queued(
	void)
{
	global_memory_index = 0;

	return;
}

void rasterizer_memory_pool_end(
	void)
{
	return;
}

void rasterizer_memory_pool_dispose(
	void)
{
	if (global_memory_pool!=NULL)
	{
		match_free("c:\\halo\\SOURCE\\rasterizer\\rasterizer_memory_pool.c", 80, global_memory_pool);
	}

	global_memory_pool = NULL;
	global_memory_index = 0;
	global_memory_window_start = RASTERIZER_MEMORY_POOL_SIZE;

	return;
}
