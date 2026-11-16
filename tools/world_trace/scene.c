/*
SCENE.C

The distance-field scene's made-up world for tools/test_world_trace.py: a
floor and a wall (the structure), a sphere, a capsule and a turned box (the
objects), its cascades about a camera away from the world's origin (so that
the toroidal places wrap), and rays from about the shapes, some long enough
to pass into the coarser cascades. world_trace_ray
(port/linux/game/world_cascades.c) is the reference the emitters' traces
are held to.

  scene <directory>   writes cascades.bin (the texture's bytes) and
                      rays.json (the block, each ray and its reference)

Built with -DSCENE_LIBRARY, scene_make and scene_ray alone, for
trace_check.cpp.
*/

#include "world_cascades.h"
#include "world_proxies.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHAPES 3

static float structure_distance(void *context, float const point[3])
{
	float floor_distance = point[2] - 1.5f, wall = 17.0f - point[0];

	(void)context;
	return floor_distance < wall ? floor_distance : wall;
}

void scene_make(struct world_cascades *cascades, float block[WORLD_CASCADES][4])
{
	static struct world_proxy shapes[SHAPES];
	static struct world_cascades_object objects[SHAPES];
	float camera[3] = { 13.3f, -7.2f, 3.1f };
	long index;

	memset(shapes, 0, sizeof(shapes));
	/* (a sphere, a capsule, a box turned about z) */
	shapes[0].v[0][0] = 12.5f;
	shapes[0].v[0][1] = -8.0f;
	shapes[0].v[0][2] = 2.1f;
	shapes[0].v[0][3] = (float)_world_proxy_sphere;
	shapes[0].v[2][0] = 0.5f;
	shapes[1].v[0][0] = 11.5f;
	shapes[1].v[0][1] = -6.5f;
	shapes[1].v[0][2] = 1.8f;
	shapes[1].v[1][0] = 12.5f;
	shapes[1].v[1][1] = -5.5f;
	shapes[1].v[1][2] = 2.4f;
	shapes[1].v[0][3] = (float)_world_proxy_capsule;
	shapes[1].v[2][0] = 0.2f;
	shapes[2].v[0][0] = 14.0f;
	shapes[2].v[0][1] = -6.0f;
	shapes[2].v[0][2] = 1.9f;
	shapes[2].v[0][3] = (float)_world_proxy_box;
	shapes[2].v[1][0] = cosf(0.6f) * 0.5f;
	shapes[2].v[1][1] = sinf(0.6f) * 0.5f;
	shapes[2].v[2][0] = -sinf(0.6f) * 0.3f;
	shapes[2].v[2][1] = cosf(0.6f) * 0.3f;
	shapes[2].v[2][3] = 0.4f;
	for (index = 0; index < SHAPES; index++)
	{
		objects[index].id = 0x10000 + index;
		objects[index].first = index;
		objects[index].count = 1;
	}
	world_cascades_create(cascades);
	/* (found whole a few slices a frame) */
	world_cascades_update(cascades, camera, 1, structure_distance, NULL, objects, SHAPES, shapes);
	for (index = 0; index < 64; index++)
		world_cascades_update(cascades, camera, 0, structure_distance, NULL, objects, SHAPES, shapes);
	world_cascades_block(cascades, block);
}

/* ray index's origin, direction, reach and mask (the same each run) */
void scene_ray(long index, float origin[3], float direction[3], float *t_max, int *mask)
{
	unsigned int state = 2654435761u * (unsigned int)(index + 1);
	float length = 0.0f;
	int axis;

	for (axis = 0; axis < 6; axis++)
	{
		float unit;

		state = state * 1664525u + 1013904223u;
		unit = (float)(state >> 8) / 16777216.0f;
		if (axis < 3)
			origin[axis] = (axis == 0 ? 10.5f : axis == 1 ? -9.5f : 1.55f) + unit * (axis == 2 ? 1.5f : 5.0f);
		else
			direction[axis - 3] = unit * 2.0f - 1.0f;
	}
	for (axis = 0; axis < 3; axis++)
		length += direction[axis] * direction[axis];
	length = sqrtf(length);
	for (axis = 0; axis < 3; axis++)
		direction[axis] = length > 1.0e-3f ? direction[axis] / length : (axis == 2 ? 1.0f : 0.0f);
	*t_max = index % 4 == 3 ? 12.0f : 2.5f;
	*mask = index % 3 == 0 ? WORLD_TRACE_STRUCTURE | WORLD_TRACE_OBJECTS : index % 3 == 1 ? WORLD_TRACE_OBJECTS :
		WORLD_TRACE_STRUCTURE;
}

#ifndef SCENE_LIBRARY
int main(int argc, char **argv)
{
	static struct world_cascades cascades;
	float block[WORLD_CASCADES][4];
	char path[1024];
	FILE *file;
	long count = argc > 2 ? atol(argv[2]) : 200, index;
	int cascade;

	if (argc < 2)
		return 2;
	scene_make(&cascades, block);
	snprintf(path, sizeof(path), "%s/cascades.bin", argv[1]);
	if (!(file = fopen(path, "wb")))
		return 1;
	fwrite(cascades.texels, 1, WORLD_CASCADES_BYTES, file);
	fclose(file);
	snprintf(path, sizeof(path), "%s/rays.json", argv[1]);
	if (!(file = fopen(path, "w")))
		return 1;
	fprintf(file, "{\"block\": [");
	for (cascade = 0; cascade < WORLD_CASCADES; cascade++)
		fprintf(file, "%s[%.9g, %.9g, %.9g, %.9g]", cascade ? ", " : "", block[cascade][0], block[cascade][1],
			block[cascade][2], block[cascade][3]);
	fprintf(file, "], \"rays\": [\n");
	for (index = 0; index < count; index++)
	{
		struct world_trace_hit hit;
		float origin[3], direction[3], t_max;
		int mask;

		scene_ray(index, origin, direction, &t_max, &mask);
		world_trace_ray(&cascades, block, origin, direction, t_max, mask, &hit);
		fprintf(file, "%s{\"origin\": [%.9g, %.9g, %.9g], \"direction\": [%.9g, %.9g, %.9g], \"t_max\": %.9g, "
			"\"mask\": %d, \"hit\": %d, \"t\": %.9g, \"kind\": %d}", index ? ",\n" : "", origin[0], origin[1], origin[2],
			direction[0], direction[1], direction[2], t_max, mask, hit.hit, hit.t, hit.kind);
	}
	fprintf(file, "]}\n");
	fclose(file);
	return 0;
}
#endif
