/* display.ambient_occlusion "world"'s distance-field scene (port/linux/game/world_cascades.c,
test_world_cascades.py): the ray step through the cascades against exact intersections with a made-up scene (a
floor and a wall, a sphere, a capsule and a turned box), and the updates: an object that moves is found again only
where it reaches (the result alike, byte for byte, to cascades found whole with it there; every texel changed in an
upload box), one that stays is not found at all, and the camera's step finds the slabs it brings in alone. */

#include "harness.h"

#include <math.h>

#include "under_test.inc"

/* ---------- the made-up scene */

/* (the structure: solid below z = 0 and past x = 3) */
static float structure_distance(void *context, float const point[3])
{
	float floor_distance = point[2], wall = 3.0f - point[0];

	(void)context;
	return floor_distance < wall ? floor_distance : wall;
}

static void sphere(struct world_proxy *shape, float x, float y, float z, float radius)
{
	memset(shape, 0, sizeof(*shape));
	shape->v[0][0] = x;
	shape->v[0][1] = y;
	shape->v[0][2] = z;
	shape->v[0][3] = (float)_world_proxy_sphere;
	shape->v[2][0] = radius;
}

static void capsule(struct world_proxy *shape, float const a[3], float const b[3], float radius)
{
	memset(shape, 0, sizeof(*shape));
	memcpy(shape->v[0], a, sizeof(float) * 3);
	memcpy(shape->v[1], b, sizeof(float) * 3);
	shape->v[0][3] = (float)_world_proxy_capsule;
	shape->v[2][0] = radius;
}

/* (a box turned by angle about z: its half axes x and y, its z half extent) */
static void box(struct world_proxy *shape, float x, float y, float z, float angle, float ex, float ey, float ez)
{
	memset(shape, 0, sizeof(*shape));
	shape->v[0][0] = x;
	shape->v[0][1] = y;
	shape->v[0][2] = z;
	shape->v[0][3] = (float)_world_proxy_box;
	shape->v[1][0] = cosf(angle) * ex;
	shape->v[1][1] = sinf(angle) * ex;
	shape->v[2][0] = -sinf(angle) * ey;
	shape->v[2][1] = cosf(angle) * ey;
	shape->v[2][3] = ez;
}

#define SHAPES 3
static struct world_proxy shapes[SHAPES];
static struct world_cascades_object objects[SHAPES];

static void scene(float sphere_x)
{
	static const float a[3] = { -1.5f, 0.5f, 0.3f }, b[3] = { -0.5f, 1.5f, 0.9f };
	long index;

	sphere(&shapes[0], sphere_x, -1.0f, 0.6f, 0.5f);
	capsule(&shapes[1], a, b, 0.2f);
	box(&shapes[2], 1.0f, 1.5f, 0.4f, 0.6f, 0.5f, 0.3f, 0.4f);
	for (index = 0; index < SHAPES; index++)
	{
		objects[index].id = 0x10000 + index;
		objects[index].first = index;
		objects[index].count = 1;
	}
}

/* the exact distance of what a mask allows */
static float exact(float const point[3], int mask)
{
	float nearest = 1.0e9f;
	long index;

	if (mask & WORLD_TRACE_STRUCTURE)
		nearest = structure_distance(NULL, point);
	if (mask & WORLD_TRACE_OBJECTS)
	{
		for (index = 0; index < SHAPES; index++)
		{
			float d = world_cascades_shape_distance(&shapes[index], point);

			nearest = d < nearest ? d : nearest;
		}
	}
	return nearest;
}

/* an exact ray: where it first meets a surface (fine sphere tracing of the
exact distance; t_max none), the least clearance along the way, and how deep
it then goes into what it meets (the least distance in the next 0.5 wu) */
static float exact_ray(float const origin[3], float const direction[3], float t_max, int mask, float *clearance,
	float *depth)
{
	float t = 0.0f;
	int step;

	*clearance = 1.0e9f;
	*depth = 0.0f;
	for (step = 0; step < 100000 && t < t_max; step++)
	{
		float point[3], d;

		point[0] = origin[0] + direction[0] * t;
		point[1] = origin[1] + direction[1] * t;
		point[2] = origin[2] + direction[2] * t;
		d = exact(point, mask);
		if (d < 1.0e-4f)
		{
			float in;

			for (in = 0.0f; in < 0.5f; in += 0.005f)
			{
				float deeper[3] = { point[0] + direction[0] * in, point[1] + direction[1] * in, point[2] + direction[2] * in };
				float e = exact(deeper, mask);

				*depth = e < *depth ? e : *depth;
			}
			return t;
		}
		*clearance = d < *clearance ? d : *clearance;
		t += d > 1.0e-3f ? d : 1.0e-3f;
	}
	return t_max;
}

static unsigned int random_state = 12345u;

static float random_unit(void)
{
	random_state = random_state * 1664525u + 1013904223u;
	return (float)(random_state >> 8) / 16777216.0f;
}

/* cascades found whole: updated until every one is ready (a few slices a
frame), the camera and the shapes still */
static void fill(struct world_cascades *cascades, float const camera[3])
{
	int frames, cascade, ready = 0;

	world_cascades_update(cascades, camera, 1, structure_distance, NULL, objects, SHAPES, shapes);
	for (frames = 0; frames < 64 && !ready; frames++)
	{
		world_cascades_update(cascades, camera, 0, structure_distance, NULL, objects, SHAPES, shapes);
		for (ready = 1, cascade = 0; cascade < WORLD_CASCADES; cascade++)
			ready = ready && cascades->cascades[cascade].ready;
	}
	CHECK(ready, "not filled in %d frames", frames);
}

/* ---------- the ray step against exact intersections */

static void trace_case(void)
{
	struct world_cascades cascades;
	float block[WORLD_CASCADES][4], camera[3] = { 0.3f, -0.2f, 1.7f }, worst = 0.0f;
	long ray, hits = 0, misses = 0, checked = 0;

	scene(0.5f);
	CHECK(world_cascades_create(&cascades), "no memory");
	fill(&cascades, camera);
	world_cascades_block(&cascades, block);
	CHECK(block[0][3] == WORLD_CASCADE_FINEST && block[3][3] == WORLD_CASCADE_FINEST * 8.0f, "%g %g", block[0][3],
		block[3][3]);
	for (ray = 0; ray < 4000; ray++)
	{
		struct world_trace_hit hit;
		float origin[3], direction[3], length, t_exact, clearance, depth, t_max = 2.5f;
		int mask = ray % 3 == 0 ? WORLD_TRACE_STRUCTURE | WORLD_TRACE_OBJECTS : ray % 3 == 1 ? WORLD_TRACE_OBJECTS :
			WORLD_TRACE_STRUCTURE;

		/* (from the air above the floor, about the shapes) */
		origin[0] = -2.5f + 5.0f * random_unit();
		origin[1] = -2.5f + 5.0f * random_unit();
		origin[2] = 0.05f + 1.5f * random_unit();
		if (exact(origin, WORLD_TRACE_STRUCTURE | WORLD_TRACE_OBJECTS) < 0.1f)
			continue;
		direction[0] = random_unit() * 2.0f - 1.0f;
		direction[1] = random_unit() * 2.0f - 1.0f;
		direction[2] = random_unit() * 2.0f - 1.0f;
		length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
		if (length < 0.1f)
			continue;
		direction[0] /= length;
		direction[1] /= length;
		direction[2] /= length;
		world_trace_ray(&cascades, block, origin, direction, t_max, mask, &hit);
		t_exact = exact_ray(origin, direction, t_max, mask, &clearance, &depth);
		/* (a hit well within its reach that goes into what it meets, or a
		miss with room to spare: the voxels' sampling may go either way where
		a ray grazes a surface) */
		if (t_exact < t_max - 0.5f && depth < -0.5f * WORLD_CASCADE_FINEST)
		{
			float error = fabsf(hit.t - t_exact), at[3];

			CHECK(hit.hit, "ray %ld from %g %g %g along %g %g %g missed (exact %g)", ray, origin[0], origin[1], origin[2],
				direction[0], direction[1], direction[2], t_exact);
			/* (where, or before it, within the hit's distance of a surface it
			passes: half a voxel, a voxel's own) */
			at[0] = origin[0] + direction[0] * hit.t;
			at[1] = origin[1] + direction[1] * hit.t;
			at[2] = origin[2] + direction[2] * hit.t;
			if (hit.t < t_exact && exact(at, mask) < 0.5f * WORLD_CASCADE_FINEST)
				error = 0.0f;
			CHECK(error <= 0.5f * WORLD_CASCADE_FINEST + 0.05f, "ray %ld: %g, exact %g", ray, hit.t, t_exact);
			CHECK(hit.kind & mask, "ray %ld: kind %d of mask %d", ray, hit.kind, mask);
			worst = error > worst ? error : worst;
			hits++;
			checked++;
		}
		else if (t_exact >= t_max && clearance > 2.0f * WORLD_CASCADE_FINEST)
		{
			CHECK(!hit.hit && hit.t == t_max, "ray %ld from %g %g %g along %g %g %g hit at %g (exact: none)", ray,
				origin[0], origin[1], origin[2], direction[0], direction[1], direction[2], hit.t);
			misses++;
			checked++;
		}
	}
	CHECK(hits > 500 && misses > 300, "%ld hits, %ld misses", hits, misses);
	/* (a ray from inside the sphere, out and up into nothing: it leaves the
	solid it starts in without hitting it; down, it hits the floor) */
	{
		struct world_trace_hit hit;
		float origin[3] = { 0.5f, -1.0f, 0.6f }, up[3] = { 0.0f, 0.0f, 1.0f }, down[3] = { 0.0f, 0.0f, -1.0f };

		world_trace_ray(&cascades, block, origin, up, 2.0f, WORLD_TRACE_STRUCTURE | WORLD_TRACE_OBJECTS, &hit);
		CHECK(!hit.hit, "hit its own sphere at %g", hit.t);
		world_trace_ray(&cascades, block, origin, down, 2.0f, WORLD_TRACE_STRUCTURE | WORLD_TRACE_OBJECTS, &hit);
		CHECK(hit.hit && hit.kind == WORLD_TRACE_STRUCTURE && fabsf(hit.t - 0.6f) < 0.15f, "%d %d %g", hit.hit, hit.kind,
			hit.t);
	}
	printf("%ld rays checked (%ld hits, %ld misses), the largest error %.3f wu\n", checked, hits, misses, worst);
	world_cascades_free(&cascades);
}

/* ---------- the updates */

static int texels_alike(struct world_cascades const *a, struct world_cascades const *b, long *first_difference)
{
	long index;

	for (index = 0; index < WORLD_CASCADES_BYTES; index++)
	{
		if (a->texels[index] != b->texels[index])
		{
			*first_difference = index;
			return 0;
		}
	}
	return 1;
}

static int in_a_box(struct world_cascades const *cascades, long index)
{
	long texel = index / WORLD_CASCADE_TEXEL, at[3], b;

	at[0] = texel % WORLD_CASCADE_SPAN;
	at[1] = texel / WORLD_CASCADE_SPAN % WORLD_CASCADE_SPAN;
	at[2] = texel / (WORLD_CASCADE_SPAN * WORLD_CASCADE_SPAN);
	for (b = 0; b < cascades->box_count; b++)
	{
		struct world_cascades_box const *box = &cascades->boxes[b];
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			if (at[axis] < box->place[axis] || at[axis] >= box->place[axis] + box->size[axis])
				break;
		}
		if (axis == 3)
			return 1;
	}
	return 0;
}

static void update_case(void)
{
	static struct world_cascades cascades, fresh;
	static signed char before[WORLD_CASCADES_BYTES];
	float camera[3] = { 0.3f, -0.2f, 1.7f };
	long difference = -1, index, changed = 0, total = (long)WORLD_CASCADE_SIDE * WORLD_CASCADE_SIDE * WORLD_CASCADE_SIDE *
		WORLD_CASCADES;

	CHECK(world_cascades_create(&cascades) && world_cascades_create(&fresh), "no memory");
	scene(0.5f);
	world_cascades_update(&cascades, camera, 1, structure_distance, NULL, objects, SHAPES, shapes);
	CHECK(cascades.moved == SHAPES, "%ld", cascades.moved);
	/* (found whole a few slices a frame: never the whole in one) */
	CHECK(cascades.structure_voxels <= (long)WORLD_CASCADES_FILL_SLICES * WORLD_CASCADE_SIDE * WORLD_CASCADE_SIDE,
		"%ld voxels in a frame", cascades.structure_voxels);
	fill(&cascades, camera);
	/* (still: nothing found again, nothing to upload) */
	world_cascades_update(&cascades, camera, 0, structure_distance, NULL, objects, SHAPES, shapes);
	CHECK(cascades.moved == 0 && cascades.box_count == 0 && cascades.object_voxels == 0 &&
		cascades.structure_voxels == 0, "%ld moved, %ld boxes, %ld voxels", cascades.moved, cascades.box_count,
		cascades.object_voxels);
	/* (the sphere moved 1.5 wu: it alone, where it was and is) */
	memcpy(before, cascades.texels, WORLD_CASCADES_BYTES);
	scene(2.0f);
	world_cascades_update(&cascades, camera, 0, structure_distance, NULL, objects, SHAPES, shapes);
	CHECK(cascades.moved == 1, "%ld moved", cascades.moved);
	CHECK(cascades.structure_voxels == 0, "%ld", cascades.structure_voxels);
	CHECK(cascades.object_voxels < total / 20, "%ld of %ld voxels found again", cascades.object_voxels, total);
	fill(&fresh, camera);
	CHECK(texels_alike(&cascades, &fresh, &difference), "the update differs from the whole at byte %ld", difference);
	for (index = 0; index < WORLD_CASCADES_BYTES; index++)
	{
		if (before[index] != cascades.texels[index])
		{
			changed++;
			CHECK(in_a_box(&cascades, index), "byte %ld changed outside the upload's boxes", index);
		}
	}
	CHECK(changed > 0 && !cascades.whole, "%ld changed", changed);
	printf("a sphere moved 1.5 wu: %ld of %ld voxels found again, %ld boxes\n", cascades.object_voxels, total,
		cascades.box_count);
	/* (the camera's step: the slabs it brings in alone) */
	camera[0] += 1.3f;
	camera[2] -= 0.6f;
	world_cascades_update(&cascades, camera, 0, structure_distance, NULL, objects, SHAPES, shapes);
	CHECK(cascades.structure_voxels > 0 && cascades.structure_voxels < total / 4, "%ld", cascades.structure_voxels);
	fill(&fresh, camera);
	CHECK(texels_alike(&cascades, &fresh, &difference), "the step differs from the whole at byte %ld", difference);
	printf("the camera stepped: %ld voxels' structure found\n", cascades.structure_voxels);
	world_cascades_free(&cascades);
	world_cascades_free(&fresh);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "trace"))
		trace_case();
	else if (!strcmp(name, "update"))
		update_case();
	else
		return 2;
	return 0;
}
