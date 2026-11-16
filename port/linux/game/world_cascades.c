/*
WORLD_CASCADES.C

display.ambient_occlusion "world"'s distance-field scene (world_cascades.h,
docs/world-ao.md "The distance-field scene"): the cascades about the camera,
their updates as the camera and the objects move, and the ray step through
them. Pure arithmetic: the game hands it the camera, the structure's field
(a sampler) and the objects' shapes (world_ao.c).
*/

#include "world_cascades.h"
#include "world_proxies.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* a distance where there is none (past every cascade's range) */
#define CASCADES_FAR 1.0e9f

/* ---------- small arithmetic */

static long floor_long(float value)
{
	return (long)floorf(value);
}

/* a world index's place in its cascade (modulo the side, never negative) */
static long cascade_place(long index)
{
	long place = index % WORLD_CASCADE_SIDE;

	return place < 0 ? place + WORLD_CASCADE_SIDE : place;
}

static float minf(float a, float b)
{
	return a < b ? a : b;
}

static float maxf(float a, float b)
{
	return a > b ? a : b;
}

signed char world_cascades_encode(float distance, float range)
{
	float value = distance / range * 127.0f;

	if (value > 127.0f)
		value = 127.0f;
	if (value < -127.0f)
		value = -127.0f;
	return (signed char)lrintf(value);
}

float world_cascades_decode(signed char value, float range)
{
	/* (a snorm byte as a GPU reads it: -128 is -1 too) */
	float unit = (float)value / 127.0f;

	return (unit < -1.0f ? -1.0f : unit) * range;
}

/* ---------- the shapes' distances */

float world_cascades_shape_distance(struct world_proxy const *shape, float const point[3])
{
	int kind = (int)shape->v[0][3];
	float offset[3];
	int axis;

	for (axis = 0; axis < 3; axis++)
		offset[axis] = point[axis] - shape->v[0][axis];
	if (kind == _world_proxy_sphere)
		return sqrtf(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]) - shape->v[2][0];
	if (kind == _world_proxy_capsule)
	{
		float segment[3], along = 0.0f, length_squared = 0.0f, h, d[3];

		for (axis = 0; axis < 3; axis++)
		{
			segment[axis] = shape->v[1][axis] - shape->v[0][axis];
			along += offset[axis] * segment[axis];
			length_squared += segment[axis] * segment[axis];
		}
		h = length_squared > 0.0f ? along / length_squared : 0.0f;
		h = h < 0.0f ? 0.0f : h > 1.0f ? 1.0f : h;
		for (axis = 0; axis < 3; axis++)
			d[axis] = offset[axis] - segment[axis] * h;
		return sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - shape->v[2][0];
	}
	if (kind == _world_proxy_box)
	{
		/* (its half axes x and y as vectors, z's half extent along their
		cross product's direction: world_proxies.h's layout) */
		float x[3], y[3], z[3], extents[3], local[3], outside = 0.0f, inside = -CASCADES_FAR;

		for (axis = 0; axis < 3; axis++)
		{
			x[axis] = shape->v[1][axis];
			y[axis] = shape->v[2][axis];
		}
		extents[0] = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
		extents[1] = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
		extents[2] = shape->v[2][3];
		if (extents[0] <= 0.0f || extents[1] <= 0.0f)
			return CASCADES_FAR;
		for (axis = 0; axis < 3; axis++)
		{
			x[axis] /= extents[0];
			y[axis] /= extents[1];
		}
		z[0] = x[1] * y[2] - x[2] * y[1];
		z[1] = x[2] * y[0] - x[0] * y[2];
		z[2] = x[0] * y[1] - x[1] * y[0];
		local[0] = offset[0] * x[0] + offset[1] * x[1] + offset[2] * x[2];
		local[1] = offset[0] * y[0] + offset[1] * y[1] + offset[2] * y[2];
		local[2] = offset[0] * z[0] + offset[1] * z[1] + offset[2] * z[2];
		for (axis = 0; axis < 3; axis++)
		{
			float q = fabsf(local[axis]) - extents[axis];

			if (q > 0.0f)
				outside += q * q;
			inside = maxf(inside, q);
		}
		return sqrtf(outside) + minf(inside, 0.0f);
	}
	return CASCADES_FAR;
}

/* a shape's thickness: its radius, a box's least half extent (one thinner
than WORLD_CASCADE_THINNEST of a cascade's voxel is not in it: its
filtered distance would hardly show it, and it would cost as its reach
there, which grows as the voxel) */
static float shape_thickness(struct world_proxy const *shape)
{
	int kind = (int)shape->v[0][3];

	if (kind == _world_proxy_box)
	{
		float x = sqrtf(shape->v[1][0] * shape->v[1][0] + shape->v[1][1] * shape->v[1][1] + shape->v[1][2] * shape->v[1][2]);
		float y = sqrtf(shape->v[2][0] * shape->v[2][0] + shape->v[2][1] * shape->v[2][1] + shape->v[2][2] * shape->v[2][2]);

		return minf(minf(x, y), shape->v[2][3]);
	}
	return shape->v[2][0];
}

/* a shape's bounds in the world (x0 y0 z0 x1 y1 z1); FALSE for a kind with
no distance */
static int shape_bounds(struct world_proxy const *shape, float bounds[6])
{
	int kind = (int)shape->v[0][3], axis;

	for (axis = 0; axis < 3; axis++)
	{
		float a = shape->v[0][axis], reach;

		if (kind == _world_proxy_sphere)
		{
			reach = shape->v[2][0];
			bounds[axis] = a - reach;
			bounds[3 + axis] = a + reach;
		}
		else if (kind == _world_proxy_capsule)
		{
			float b = shape->v[1][axis];

			reach = shape->v[2][0];
			bounds[axis] = minf(a, b) - reach;
			bounds[3 + axis] = maxf(a, b) + reach;
		}
		else if (kind == _world_proxy_box)
		{
			/* (its half axes' extents along the world's axis, z's at most its
			half extent: a box's corners are within these) */
			reach = fabsf(shape->v[1][axis]) + fabsf(shape->v[2][axis]) + shape->v[2][3];
			bounds[axis] = a - reach;
			bounds[3 + axis] = a + reach;
		}
		else
		{
			return 0;
		}
	}
	return 1;
}

static void bounds_add(float bounds[6], float const more[6], int *any)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (!*any || more[axis] < bounds[axis])
			bounds[axis] = more[axis];
		if (!*any || more[3 + axis] > bounds[3 + axis])
			bounds[3 + axis] = more[3 + axis];
	}
	*any = 1;
}

/* ---------- the texture */

static signed char *texel_at(struct world_cascades *cascades, int cascade, long x, long y, long z)
{
	return cascades->texels + (((size_t)(cascade * WORLD_CASCADE_SPAN + z) * WORLD_CASCADE_SPAN + (size_t)y) *
		WORLD_CASCADE_SPAN + (size_t)x) * WORLD_CASCADE_TEXEL;
}

/* a voxel's two channels written at its place, and at its guard's where
its place is first along an axis (structure_too FALSE: the objects' alone) */
static void texel_store(struct world_cascades *cascades, int cascade, long const place[3], int structure_too,
	signed char structure, signed char objects)
{
	int copy;

	for (copy = 0; copy < 8; copy++)
	{
		long at[3];
		int axis, good = 1;
		signed char *texel;

		for (axis = 0; axis < 3 && good; axis++)
		{
			at[axis] = place[axis];
			if (copy & (1 << axis))
			{
				good = place[axis] == 0;
				at[axis] = WORLD_CASCADE_SIDE;
			}
		}
		if (!good)
			continue;
		texel = texel_at(cascades, cascade, at[0], at[1], at[2]);
		if (structure_too)
			texel[0] = structure;
		texel[1] = objects;
		/* (no guard to write: the first alone) */
		if (!copy && place[0] && place[1] && place[2])
			break;
	}
}

static void box_add(struct world_cascades *cascades, int cascade, long const first[3], long const last[3])
{
	long pieces[3][2][2], counts[3];
	int axis, i, j, k;

	/* (each axis's range, at most the side, as its places: one piece, or two
	where it wraps; a piece from place 0 takes the guard too) */
	for (axis = 0; axis < 3; axis++)
	{
		long length = last[axis] - first[axis] + 1, start = cascade_place(first[axis]);

		if (start + length <= WORLD_CASCADE_SIDE)
		{
			pieces[axis][0][0] = start;
			pieces[axis][0][1] = start + length - 1;
			counts[axis] = 1;
		}
		else
		{
			pieces[axis][0][0] = start;
			pieces[axis][0][1] = WORLD_CASCADE_SIDE - 1;
			pieces[axis][1][0] = 0;
			pieces[axis][1][1] = length - (WORLD_CASCADE_SIDE - start) - 1;
			counts[axis] = 2;
		}
		for (i = 0; i < counts[axis]; i++)
		{
			if (pieces[axis][i][0] == 0)
				pieces[axis][i][1] = WORLD_CASCADE_SIDE;
		}
	}
	for (i = 0; i < counts[0]; i++)
	{
		for (j = 0; j < counts[1]; j++)
		{
			for (k = 0; k < counts[2]; k++)
			{
				struct world_cascades_box *box;

				if (cascades->box_count >= WORLD_CASCADES_MAXIMUM_BOXES)
				{
					cascades->whole = 1;
					return;
				}
				box = &cascades->boxes[cascades->box_count++];
				box->place[0] = pieces[0][i][0];
				box->place[1] = pieces[1][j][0];
				box->place[2] = pieces[2][k][0] + (long)cascade * WORLD_CASCADE_SPAN;
				box->size[0] = pieces[0][i][1] - pieces[0][i][0] + 1;
				box->size[1] = pieces[1][j][1] - pieces[1][j][0] + 1;
				box->size[2] = pieces[2][k][1] - pieces[2][k][0] + 1;
			}
		}
	}
}

/* a region of a cascade's window (world indices first to last, each way,
at most its side) found again: the structure's channel and/or the
objects'. Each shape within range of it stamps its distance on the voxels
within range of it (a frame's work goes as the shapes' reach, not as the
region's voxels times the shapes) */
static void region_fill(struct world_cascades *cascades, int cascade, long const first[3], long const last[3],
	int structure_too, world_cascades_structure structure, void *context)
{
	static float nearest[WORLD_CASCADE_SIDE * WORLD_CASCADE_SIDE * WORLD_CASCADE_SIDE];
	struct world_cascade const *c = &cascades->cascades[cascade];
	float voxel = c->voxel, range = (float)WORLD_CASCADE_RANGE_VOXELS * voxel;
	long size[3], count, index, x, y, z;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (last[axis] < first[axis])
			return;
		size[axis] = last[axis] - first[axis] + 1;
	}
	count = size[0] * size[1] * size[2];
	for (index = 0; index < count; index++)
		nearest[index] = CASCADES_FAR;
	for (index = 0; index < cascades->shape_count; index++)
	{
		struct world_proxy const *shape = &cascades->shapes[index];
		float bounds[6];
		long low[3], high[3];

		if (!shape_bounds(shape, bounds) || shape_thickness(shape) < WORLD_CASCADE_THINNEST * voxel)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			/* (the voxels whose centres are within range of its bounds) */
			low[axis] = (long)ceilf((bounds[axis] - range) / voxel - 0.5f);
			high[axis] = floor_long((bounds[3 + axis] + range) / voxel - 0.5f);
			if (low[axis] < first[axis])
				low[axis] = first[axis];
			if (high[axis] > last[axis])
				high[axis] = last[axis];
			if (high[axis] < low[axis])
				break;
		}
		if (axis < 3)
			continue;
		for (z = low[2]; z <= high[2]; z++)
		{
			for (y = low[1]; y <= high[1]; y++)
			{
				float *row = nearest + ((z - first[2]) * size[1] + (y - first[1])) * size[0] - first[0];
				float point[3];

				point[1] = ((float)y + 0.5f) * voxel;
				point[2] = ((float)z + 0.5f) * voxel;
				for (x = low[0]; x <= high[0]; x++)
				{
					float d;

					point[0] = ((float)x + 0.5f) * voxel;
					d = world_cascades_shape_distance(shape, point);
					row[x] = d < row[x] ? d : row[x];
				}
			}
		}
	}
	for (z = first[2]; z <= last[2]; z++)
	{
		for (y = first[1]; y <= last[1]; y++)
		{
			float const *row = nearest + ((z - first[2]) * size[1] + (y - first[1])) * size[0] - first[0];
			long place[3];
			float point[3];

			place[1] = cascade_place(y);
			place[2] = cascade_place(z);
			point[1] = ((float)y + 0.5f) * voxel;
			point[2] = ((float)z + 0.5f) * voxel;
			for (x = first[0]; x <= last[0]; x++)
			{
				signed char structure_value = 0;

				place[0] = cascade_place(x);
				if (structure_too)
				{
					point[0] = ((float)x + 0.5f) * voxel;
					structure_value = world_cascades_encode(structure ? structure(context, point) : CASCADES_FAR, range);
				}
				texel_store(cascades, cascade, place, structure_too, structure_value,
					world_cascades_encode(row[x], range));
			}
		}
	}
	if (structure_too)
		cascades->structure_voxels += count;
	cascades->object_voxels += count;
	box_add(cascades, cascade, first, last);
}

/* ---------- making and updating */

int world_cascades_create(struct world_cascades *cascades)
{
	memset(cascades, 0, sizeof(*cascades));
	cascades->texels = malloc(WORLD_CASCADES_BYTES);
	cascades->shapes = malloc(sizeof(struct world_proxy) * WORLD_CASCADES_MAXIMUM_SHAPES);
	if (!cascades->texels || !cascades->shapes)
	{
		world_cascades_free(cascades);
		return 0;
	}
	/* (nothing anywhere: every distance its range) */
	memset(cascades->texels, 127, WORLD_CASCADES_BYTES);
	return 1;
}

void world_cascades_free(struct world_cascades *cascades)
{
	free(cascades->texels);
	free(cascades->shapes);
	memset(cascades, 0, sizeof(*cascades));
}

/* whether two objects' shapes are alike (none moved by more than
WORLD_CASCADE_MOVED) */
static int shapes_alike(struct world_proxy const *a, struct world_proxy const *b, long count)
{
	long index;
	int row, column;

	for (index = 0; index < count; index++)
	{
		for (row = 0; row < 3; row++)
		{
			for (column = 0; column < 4; column++)
			{
				if (fabsf(a[index].v[row][column] - b[index].v[row][column]) > WORLD_CASCADE_MOVED)
					return 0;
			}
		}
	}
	return 1;
}

/* shapes' bounds (bounds[0..5]) and their greatest thickness (bounds[6]),
added to any already there */
static int shapes_bounds(struct world_proxy const *shapes, long count, float bounds[7], int any)
{
	long index;

	if (!any)
		bounds[6] = 0.0f;
	for (index = 0; index < count; index++)
	{
		float more[6];

		if (shape_bounds(&shapes[index], more))
		{
			bounds_add(bounds, more, &any);
			bounds[6] = maxf(bounds[6], shape_thickness(&shapes[index]));
		}
	}
	return any;
}

/* the world regions whose objects changed (x0 y0 z0 x1 y1 z1) */
struct cascades_dirty
{
	float bounds[WORLD_CASCADES_MAXIMUM_OBJECTS * 2][7];
	long count;
};

static void dirty_add(struct cascades_dirty *dirty, float const bounds[7])
{
	if (dirty->count < WORLD_CASCADES_MAXIMUM_OBJECTS * 2)
		memcpy(dirty->bounds[dirty->count++], bounds, sizeof(float) * 7);
}

/* the objects compared with those stamped, and taken in their place: the
regions of each that moved, came or went */
static void objects_take(struct world_cascades *cascades, struct world_cascades_object const *objects, long object_count,
	struct world_proxy const *shapes, struct cascades_dirty *dirty)
{
	static unsigned char matched[WORLD_CASCADES_MAXIMUM_OBJECTS];
	static struct world_cascades_object taken[WORLD_CASCADES_MAXIMUM_OBJECTS];
	static struct world_proxy taken_shapes[WORLD_CASCADES_MAXIMUM_SHAPES];
	long index, old, taken_count = 0, taken_shape_count = 0;

	memset(matched, 0, sizeof(matched));
	if (object_count > WORLD_CASCADES_MAXIMUM_OBJECTS)
		object_count = WORLD_CASCADES_MAXIMUM_OBJECTS;
	for (index = 0; index < object_count; index++)
	{
		struct world_cascades_object const *object = &objects[index];
		struct world_proxy const *new_shapes = shapes + object->first;
		long count = object->count;
		float bounds[7];
		int any;

		if (taken_shape_count + count > WORLD_CASCADES_MAXIMUM_SHAPES)
			count = WORLD_CASCADES_MAXIMUM_SHAPES - taken_shape_count;
		if (count <= 0)
			continue;
		/* (its old self: most often at the same index) */
		old = index < cascades->object_count && cascades->objects[index].id == object->id ? index : -1;
		if (old < 0)
		{
			for (old = 0; old < cascades->object_count && cascades->objects[old].id != object->id; old++)
				;
			if (old >= cascades->object_count)
				old = -1;
		}
		if (old >= 0 && !matched[old])
		{
			struct world_cascades_object const *before = &cascades->objects[old];

			matched[old] = 1;
			if (before->count != count || !shapes_alike(cascades->shapes + before->first, new_shapes, count))
			{
				any = shapes_bounds(cascades->shapes + before->first, before->count, bounds, 0);
				if (shapes_bounds(new_shapes, count, bounds, any))
					dirty_add(dirty, bounds);
				cascades->moved++;
			}
		}
		else
		{
			if (shapes_bounds(new_shapes, count, bounds, 0))
				dirty_add(dirty, bounds);
			cascades->moved++;
		}
		taken[taken_count].id = object->id;
		taken[taken_count].first = taken_shape_count;
		taken[taken_count].count = count;
		memcpy(taken_shapes + taken_shape_count, new_shapes, sizeof(*new_shapes) * (size_t)count);
		taken_count++;
		taken_shape_count += count;
	}
	/* (those gone) */
	for (old = 0; old < cascades->object_count; old++)
	{
		float bounds[7];

		if (!matched[old] && shapes_bounds(cascades->shapes + cascades->objects[old].first, cascades->objects[old].count,
			bounds, 0))
		{
			dirty_add(dirty, bounds);
			cascades->moved++;
		}
	}
	memcpy(cascades->objects, taken, sizeof(*taken) * (size_t)taken_count);
	memcpy(cascades->shapes, taken_shapes, sizeof(*taken_shapes) * (size_t)taken_shape_count);
	cascades->object_count = taken_count;
	cascades->shape_count = taken_shape_count;
}

/* a world region as a cascade's voxels (with its range and a voxel more for
the filter), within its window; FALSE if none of it is */
static int region_voxels(struct world_cascade const *c, float const bounds[6], long first[3], long last[3])
{
	float grow = ((float)WORLD_CASCADE_RANGE_VOXELS + 1.0f) * c->voxel;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		first[axis] = floor_long((bounds[axis] - grow) / c->voxel - 0.5f);
		last[axis] = floor_long((bounds[3 + axis] + grow) / c->voxel - 0.5f) + 1;
		if (first[axis] < c->first[axis])
			first[axis] = c->first[axis];
		if (last[axis] > c->first[axis] + WORLD_CASCADE_SIDE - 1)
			last[axis] = c->first[axis] + WORLD_CASCADE_SIDE - 1;
		if (last[axis] < first[axis])
			return 0;
	}
	return 1;
}

void world_cascades_update(struct world_cascades *cascades, float const camera[3], int structure_changed,
	world_cascades_structure structure, void *context, struct world_cascades_object const *objects, long object_count,
	struct world_proxy const *shapes)
{
	static struct cascades_dirty dirty;
	long budget = WORLD_CASCADES_FILL_SLICES;
	int cascade, axis;

	cascades->box_count = 0;
	cascades->whole = 0;
	cascades->structure_voxels = cascades->object_voxels = cascades->moved = 0;
	if (!cascades->texels)
		return;
	dirty.count = 0;
	objects_take(cascades, objects, object_count, shapes, &dirty);
	/* (the coarsest first: a cascade being filled takes the frame's budget
	of slices, and the coarser are the sooner of use) */
	for (cascade = WORLD_CASCADES - 1; cascade >= 0; cascade--)
	{
		struct world_cascade *c = &cascades->cascades[cascade];
		float voxel = WORLD_CASCADE_FINEST * (float)(1 << cascade);
		long first[3], last[3], index;
		int restart = structure_changed || c->voxel != voxel;

		/* (its window: the camera's voxel in its middle, in steps) */
		for (axis = 0; axis < 3; axis++)
		{
			long middle = floor_long(camera[axis] / voxel);
			long step = (long)floorf((float)(middle - WORLD_CASCADE_SIDE / 2) / (float)WORLD_CASCADE_SNAP + 0.5f);

			first[axis] = step * WORLD_CASCADE_SNAP;
			/* (moved past its side; or moved while it is filled, which begins
			again where it is) */
			if (labs(first[axis] - c->first[axis]) >= WORLD_CASCADE_SIDE || (!c->ready && first[axis] != c->first[axis]))
				restart = 1;
		}
		if (restart)
		{
			c->voxel = voxel;
			c->ready = 0;
			c->filled = 0;
			memcpy(c->first, first, sizeof(first));
		}
		if (!c->ready)
		{
			/* (filled a few slices a frame, then named ready: until then the
			coarser serve where it would) */
			long slices = WORLD_CASCADE_SIDE - c->filled < budget ? WORLD_CASCADE_SIDE - c->filled : budget;

			if (slices > 0)
			{
				for (axis = 0; axis < 3; axis++)
				{
					first[axis] = c->first[axis];
					last[axis] = c->first[axis] + WORLD_CASCADE_SIDE - 1;
				}
				first[2] = c->first[2] + c->filled;
				last[2] = first[2] + slices - 1;
				region_fill(cascades, cascade, first, last, 1, structure, context);
				c->filled += slices;
				budget -= slices;
			}
			c->ready = c->filled >= WORLD_CASCADE_SIDE;
			/* (the objects that moved within what was filled before) */
			for (index = 0; index < dirty.count; index++)
			{
				if (dirty.bounds[index][6] >= WORLD_CASCADE_THINNEST * voxel && region_voxels(c, dirty.bounds[index], first,
					last))
				{
					if (last[2] > c->first[2] + c->filled - slices - 1)
						last[2] = c->first[2] + c->filled - slices - 1;
					region_fill(cascades, cascade, first, last, 0, structure, context);
				}
			}
			continue;
		}
		/* (moved: the slabs it brought in, each along one axis, across the
		new window along the others) */
		for (axis = 0; axis < 3; axis++)
		{
			long delta = first[axis] - c->first[axis], slab_first[3], slab_last[3];
			int other;

			if (!delta)
				continue;
			for (other = 0; other < 3; other++)
			{
				slab_first[other] = first[other];
				slab_last[other] = first[other] + WORLD_CASCADE_SIDE - 1;
			}
			if (delta > 0)
				slab_first[axis] = first[axis] + WORLD_CASCADE_SIDE - delta;
			else
				slab_last[axis] = first[axis] - delta - 1;
			c->first[axis] = first[axis];
			region_fill(cascades, cascade, slab_first, slab_last, 1, structure, context);
		}
		memcpy(c->first, first, sizeof(first));
		/* (the objects that moved, came or went: where they reach) */
		for (index = 0; index < dirty.count; index++)
		{
			if (dirty.bounds[index][6] >= WORLD_CASCADE_THINNEST * voxel && region_voxels(c, dirty.bounds[index], first,
				last))
			{
				region_fill(cascades, cascade, first, last, 0, structure, context);
			}
		}
	}
}

void world_cascades_block(struct world_cascades const *cascades, float block[WORLD_CASCADES][4])
{
	int cascade, axis;

	for (cascade = 0; cascade < WORLD_CASCADES; cascade++)
	{
		struct world_cascade const *c = &cascades->cascades[cascade];

		for (axis = 0; axis < 3; axis++)
			block[cascade][axis] = ((float)c->first[axis] + 0.5f) * c->voxel;
		block[cascade][3] = c->ready && cascades->texels ? c->voxel : 0.0f;
	}
}

/* ---------- reading, and the ray step (the emitters' world_trace: operation
for operation) */

int world_cascades_find(float const block[WORLD_CASCADES][4], float const point[3])
{
	int cascade, axis;

	for (cascade = 0; cascade < WORLD_CASCADES; cascade++)
	{
		float voxel = block[cascade][3];

		if (voxel <= 0.0f)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			float q = (point[axis] - block[cascade][axis]) / voxel;

			if (!(q >= 0.0f && q <= (float)(WORLD_CASCADE_SIDE - 1)))
				break;
		}
		if (axis == 3)
			return cascade;
	}
	return -1;
}

void world_cascades_sample(struct world_cascades const *cascades, float const block[WORLD_CASCADES][4],
	int cascade, float const point[3], float distances[2])
{
	float voxel = block[cascade][3], range = (float)WORLD_CASCADE_RANGE_VOXELS * voxel, fraction[3], sums[2] = { 0.0f, 0.0f };
	long base[3];
	int axis, corner, channel;

	for (axis = 0; axis < 3; axis++)
	{
		/* (its place: the voxel's centres' coordinate, modulo the side) */
		float at = point[axis] / voxel - 0.5f;
		float place = at - (float)WORLD_CASCADE_SIDE * floorf(at / (float)WORLD_CASCADE_SIDE);

		base[axis] = (long)floorf(place);
		if (base[axis] > WORLD_CASCADE_SIDE - 1)
			base[axis] = WORLD_CASCADE_SIDE - 1;
		if (base[axis] < 0)
			base[axis] = 0;
		fraction[axis] = place - (float)base[axis];
	}
	for (corner = 0; corner < 8; corner++)
	{
		float weight = ((corner & 1) ? fraction[0] : 1.0f - fraction[0]) *
			(((corner >> 1) & 1) ? fraction[1] : 1.0f - fraction[1]) *
			(((corner >> 2) & 1) ? fraction[2] : 1.0f - fraction[2]);
		signed char const *texel = texel_at((struct world_cascades *)cascades, cascade, base[0] + (corner & 1),
			base[1] + ((corner >> 1) & 1), base[2] + ((corner >> 2) & 1));

		for (channel = 0; channel < 2; channel++)
			sums[channel] += weight * world_cascades_decode(texel[channel], 1.0f);
	}
	distances[0] = sums[0] * range;
	distances[1] = sums[1] * range;
}

void world_trace_ray(struct world_cascades const *cascades, float const block[WORLD_CASCADES][4],
	float const origin[3], float const direction[3], float t_max, int mask, struct world_trace_hit *hit)
{
	int active[2] = { 0, 0 }, step;
	float t = 0.0f;

	hit->hit = 0;
	hit->t = t_max;
	hit->kind = 0;
	for (step = 0; step < WORLD_TRACE_STEPS; step++)
	{
		float point[3], distances[2], voxel, nearest, inside, advance;
		int cascade, channel;

		point[0] = origin[0] + direction[0] * t;
		point[1] = origin[1] + direction[1] * t;
		point[2] = origin[2] + direction[2] * t;
		cascade = world_cascades_find(block, point);
		if (cascade < 0)
			return;
		voxel = block[cascade][3];
		world_cascades_sample(cascades, block, cascade, point, distances);
		/* (each channel the mask allows, once the ray is outside it; inside
		one yet, how far it is to its way out) */
		nearest = inside = (float)WORLD_CASCADE_RANGE_VOXELS * voxel;
		for (channel = 0; channel < 2; channel++)
		{
			if (!(mask & (1 << channel)))
				continue;
			if (distances[channel] >= 0.0f)
				active[channel] = 1;
			if (active[channel])
				nearest = minf(nearest, distances[channel]);
			else
				inside = minf(inside, -distances[channel]);
		}
		if (nearest < WORLD_TRACE_HIT * voxel)
		{
			hit->hit = 1;
			hit->t = t;
			hit->kind = (mask & WORLD_TRACE_STRUCTURE) && active[0] && distances[0] <= nearest ? WORLD_TRACE_STRUCTURE :
				WORLD_TRACE_OBJECTS;
			return;
		}
		advance = maxf(minf(nearest, inside), WORLD_TRACE_LEAST_STEP * voxel);
		t += advance;
		if (t >= t_max)
			return;
	}
}
