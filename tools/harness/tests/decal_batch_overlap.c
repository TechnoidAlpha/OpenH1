/* A batch of decals (rasterizer_xbox_decals.c's decal_batch) and the test of
whether a decal of some vertices overlaps one already in it. */
#include "harness.h"
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct decal_vertex
{
	real_point3d position;
	unsigned long texcoord;
};

#define DECAL_BATCH_MAXIMUM_DECALS 256
static struct
{
	struct decal_vertex const *vertices;
	short vertex_count;
	byte color[4];
	real bounds[6];
} decal_batch[DECAL_BATCH_MAXIMUM_DECALS];
static short decal_batch_count = 0;

#include "under_test.inc"

/* a decal's one quad, flat on the ground, from (x, y) size by size */
static void quad(struct decal_vertex *vertices, real x, real y, real z, real size)
{
	memset(vertices, 0, 4 * sizeof(*vertices));
	vertices[0].position.x = x;
	vertices[0].position.y = y;
	vertices[1].position.x = x + size;
	vertices[1].position.y = y;
	vertices[2].position.x = x + size;
	vertices[2].position.y = y + size;
	vertices[3].position.x = x;
	vertices[3].position.y = y + size;
	vertices[0].position.z = vertices[1].position.z = vertices[2].position.z = vertices[3].position.z = z;
}

/* the decal put in the batch, as _rasterizer_decals_draw puts it, unless it
overlaps one there; whether it overlapped */
static boolean add(struct decal_vertex const *vertices)
{
	real bounds[6];

	if (rasterizer_decal_batch_overlaps(vertices, 4, bounds))
		return TRUE;
	memcpy(decal_batch[decal_batch_count].bounds, bounds, sizeof(bounds));
	decal_batch[decal_batch_count].vertices = vertices;
	decal_batch[decal_batch_count].vertex_count = 4;
	decal_batch_count++;
	return FALSE;
}

int main(int argc, char **argv)
{
	static struct decal_vertex quads[8][4];
	const char *which = argc > 1 ? argv[1] : "";

	if (!strcmp(which, "scorches"))
	{
		/* a Scorpion's scorch over the last: not in its batch */
		quad(quads[0], 10.0f, 10.0f, 0.0f, 2.0f);
		quad(quads[1], 11.0f, 11.5f, 0.01f, 2.0f);
		CHECK(!add(quads[0]), "the first decal of an empty batch overlapped");
		CHECK(add(quads[1]), "a scorch over a scorch shared their batch");
	}
	else if (!strcmp(which, "apart"))
	{
		/* decals apart along each axis share it */
		quad(quads[0], 0.0f, 0.0f, 0.0f, 1.0f);
		quad(quads[1], 1.5f, 0.0f, 0.0f, 1.0f);
		quad(quads[2], 0.0f, 1.5f, 0.0f, 1.0f);
		quad(quads[3], 0.0f, 0.0f, 1.5f, 1.0f);
		quad(quads[4], -1.5f, -1.5f, -1.5f, 1.0f);
		CHECK(!add(quads[0]) && !add(quads[1]) && !add(quads[2]) && !add(quads[3]) && !add(quads[4]),
			"decals apart did not share a batch");
		CHECK(decal_batch_count == 5, "%d decals in the batch", decal_batch_count);
	}
	else if (!strcmp(which, "touching"))
	{
		/* a decal whose edge meets another's: their edges' pixels drawn twice */
		quad(quads[0], 0.0f, 0.0f, 0.0f, 1.0f);
		quad(quads[1], 1.0f, 0.0f, 0.0f, 1.0f);
		CHECK(!add(quads[0]), "the first decal of an empty batch overlapped");
		CHECK(add(quads[1]), "touching decals shared their batch");
	}
	else if (!strcmp(which, "inside"))
	{
		/* a small decal inside a large one, and a large one over a small one */
		quad(quads[0], 0.0f, 0.0f, 0.0f, 4.0f);
		quad(quads[1], 1.0f, 1.0f, 0.0f, 0.5f);
		CHECK(!add(quads[0]), "the first decal of an empty batch overlapped");
		CHECK(add(quads[1]), "a decal inside another shared its batch");
		decal_batch_count = 0;
		CHECK(!add(quads[1]), "the first decal of an empty batch overlapped");
		CHECK(add(quads[0]), "a decal over another shared its batch");
	}
	else if (!strcmp(which, "later"))
	{
		/* the overlap is with any decal of the batch, not the last alone */
		quad(quads[0], 0.0f, 0.0f, 0.0f, 1.0f);
		quad(quads[1], 5.0f, 0.0f, 0.0f, 1.0f);
		quad(quads[2], 10.0f, 0.0f, 0.0f, 1.0f);
		quad(quads[3], 0.5f, 0.5f, 0.0f, 1.0f);
		CHECK(!add(quads[0]) && !add(quads[1]) && !add(quads[2]), "decals apart did not share a batch");
		CHECK(add(quads[3]), "a decal over the batch's first shared it");
	}
	else
	{
		fprintf(stderr, "no case %s\n", which);
		return 2;
	}
	return 0;
}
