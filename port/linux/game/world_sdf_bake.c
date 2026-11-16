/*
WORLD_SDF_BAKE.C

The distance field's pure arithmetic (world_sdf.h): its layout of cells
over a BSP's bounds, the cells a surface may be within range of, the bake
of one cell's brick, the field made from a whole grid of voxels, its
encoding, its GPU atlas, and its trilinear sample as the shaders take it.
No game state: the bake's threads run it on a copy of the geometry. Tested
asset-free (tools/harness/tests/test_world_sdf.py). Workstream B.

The field is sparse (docs/world-ao.md, "Geometry, bounds and
resolution"): a grid of cells WORLD_SDF_BRICK voxels' steps a side, each
either a brick of voxels or one kind, all outside (+range) or all inside
(-range). A cell holds a brick where a triangle may be within range of one
of its voxels; its memory follows the surfaces' area, so the voxel stays
fine where a whole grid's would have to grow with the bounds' volume.
Bricks share their faces' voxels with their neighbours, so a sample
filtered within one brick is the whole grid's, exactly, everywhere.

The bake (docs/world-ao.md, "The bake"): each voxel's distance is the least
exact distance from its centre to a triangle, found only for the triangles
within range of it (the narrow band: the triangles binned to the cells they
may reach; each visits the voxels of its cell's rows that pass within range
of its plane and of its bounding sphere, and only works out the exact
distance where its plane is nearer than the best so far); past range a
voxel keeps range, which the encoding clamps to anyway. The sign is the
collision BSP's own point test (inside), not the triangles' facing: Halo's
BSPs are not perfectly closed.
*/

#include "world_sdf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the threads */

long world_sdf_bake_threads(long processors, long forced)
{
	long threads = forced > 0 ? forced : processors - WORLD_SDF_RESERVED_PROCESSORS;

	if (threads < 1)
		threads = 1;
	if (threads > WORLD_SDF_MAXIMUM_THREADS)
		threads = WORLD_SDF_MAXIMUM_THREADS;
	return threads;
}

void world_sdf_pace_begin(struct world_sdf_pace *pace)
{
	memset(pace, 0, sizeof(*pace));
	pace->next_rest = WORLD_SDF_PACE_LEAST_REST;
}

/* a rest begun, of so many frames, for frames this late (0: to learn) */
static void sdf_pace_rest(struct world_sdf_pace *pace, long frames, float late)
{
	pace->rest = frames;
	pace->late = late;
	pace->calm = 0;
}

/* the last frames' median (a frame now and then slower, for whatever
reason, moves it not at all: a bake's threads slow every frame) */
static float sdf_pace_median(struct world_sdf_pace const *pace)
{
	float sorted[WORLD_SDF_PACE_WINDOW];
	long count = pace->count < WORLD_SDF_PACE_WINDOW ? pace->count : WORLD_SDF_PACE_WINDOW, i, j;

	for (i = 0; i < count; i++)
	{
		float value = pace->frames[i];

		for (j = i; j > 0 && sorted[j - 1] > value; j--)
			sorted[j] = sorted[j - 1];
		sorted[j] = value;
	}
	return count ? sorted[count / 2] : 0.0f;
}

/* the most frames' median that keeps to a normal */
static float sdf_pace_kept(float normal)
{
	return normal * WORLD_SDF_PACE_LATE + WORLD_SDF_PACE_SLACK_MS;
}

int world_sdf_pace_frame(struct world_sdf_pace *pace, float milliseconds, int playing)
{
	float median;

	if (!playing)
	{
		world_sdf_pace_begin(pace);
		return 1;
	}
	if (pace->next_rest < WORLD_SDF_PACE_LEAST_REST)
		pace->next_rest = WORLD_SDF_PACE_LEAST_REST;
	if (pace->rest <= 0 && pace->normal <= 0.0f)
		sdf_pace_rest(pace, WORLD_SDF_PACE_LEARN, 0.0f);
	pace->frames[pace->count++ % WORLD_SDF_PACE_WINDOW] = milliseconds;
	median = sdf_pace_median(pace);
	if (pace->rest > 0)
	{
		if (--pace->rest > 0)
			return 0;
		/* (the rest's last frames, the bake's threads at rest: the normal) */
		pace->normal = median;
		pace->count = 0;
		/* (as slow while it rested: the frames ran late for something
		else, a heavier scene, not for the bake; no longer rests for it) */
		if (pace->late > 0.0f && pace->late <= sdf_pace_kept(pace->normal))
			pace->next_rest = WORLD_SDF_PACE_LEAST_REST;
		return 1;
	}
	if (pace->count >= WORLD_SDF_PACE_WINDOW && median > sdf_pace_kept(pace->normal))
	{
		/* (late: a rest, each longer than the last while resting helps) */
		sdf_pace_rest(pace, pace->next_rest, median);
		pace->next_rest = pace->next_rest * 2 < WORLD_SDF_PACE_MOST_REST ? pace->next_rest * 2 : WORLD_SDF_PACE_MOST_REST;
		return 0;
	}
	if (++pace->calm >= WORLD_SDF_PACE_CALM)
	{
		pace->calm = 0;
		pace->next_rest = pace->next_rest / 2 > WORLD_SDF_PACE_LEAST_REST ? pace->next_rest / 2 :
			WORLD_SDF_PACE_LEAST_REST;
	}
	return 1;
}

/* ---------- the layout and the encoding */

void world_sdf_layout(float const bounds[6], float voxel, struct world_sdf_header *header)
{
	float step = WORLD_SDF_BRICK * voxel;
	int axis;
	unsigned long count[3];

	for (axis = 0; axis < 3; axis++)
	{
		float size = bounds[3 + axis] - bounds[axis] + 2.0f * WORLD_SDF_PADDING_VOXELS * voxel;
		/* (centred over the bounds: the first voxel's centre) */
		float middle = 0.5f * (bounds[axis] + bounds[3 + axis]);

		count[axis] = size > step ? (unsigned long)ceilf(size / step) : 1;
		header->origin[axis] = middle - 0.5f * (float)count[axis] * step;
	}
	header->width = count[0];
	header->height = count[1];
	header->depth = count[2];
	header->voxel = voxel;
	header->range = WORLD_SDF_RANGE_VOXELS * voxel;
	header->brick_count = 0;
}

unsigned long world_sdf_bytes(struct world_sdf_header const *header, unsigned long bricks)
{
	unsigned long cells = header->width * header->height * header->depth;

	return cells * WORLD_SDF_CELL_BYTES +
		(bricks + WORLD_SDF_ATLAS_LAYER - 1) / WORLD_SDF_ATLAS_LAYER * WORLD_SDF_ATLAS_LAYER_BYTES;
}

signed char world_sdf_encode(float distance, float range)
{
	float value = distance / range * 127.0f;

	if (value > 127.0f)
		value = 127.0f;
	if (value < -127.0f)
		value = -127.0f;
	return (signed char)(value < 0.0f ? value - 0.5f : value + 0.5f);
}

float world_sdf_decode(signed char value, float range)
{
	return (float)value / 127.0f * range;
}

/* ---------- distances */

static float sdf_dot(float const a[3], float const b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* the squared distance from p to the segment a to a + ab */
static float sdf_segment_squared(float const p[3], float const a[3], float const ab[3])
{
	float ap[3], length = sdf_dot(ab, ab), t = 0.0f, d[3];
	int axis;

	for (axis = 0; axis < 3; axis++)
		ap[axis] = p[axis] - a[axis];
	if (length > 0.0f)
	{
		t = sdf_dot(ap, ab) / length;
		t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	}
	for (axis = 0; axis < 3; axis++)
		d[axis] = ap[axis] - t * ab[axis];
	return sdf_dot(d, d);
}

/* the squared distance from p to the triangle abc: the closest point found
by the Voronoi regions of its corners, edges and face (Ericson, Real-Time
Collision Detection, 5.1.5); a triangle with no area as its three edges */
static float sdf_triangle_squared(float const p[3], float const a[3], float const b[3], float const c[3],
	float const ab[3], float const ac[3], int flat)
{
	float ap[3], bp[3], cp[3], closest[3], d[3];
	float d1, d2, d3, d4, d5, d6, va, vb, vc, v, w, denominator;
	int axis;

	if (flat)
	{
		float bc[3], best, other;

		for (axis = 0; axis < 3; axis++)
			bc[axis] = c[axis] - b[axis];
		best = sdf_segment_squared(p, a, ab);
		other = sdf_segment_squared(p, a, ac);
		if (other < best)
			best = other;
		other = sdf_segment_squared(p, b, bc);
		return other < best ? other : best;
	}
	for (axis = 0; axis < 3; axis++)
		ap[axis] = p[axis] - a[axis];
	d1 = sdf_dot(ab, ap);
	d2 = sdf_dot(ac, ap);
	if (d1 <= 0.0f && d2 <= 0.0f)
		return sdf_dot(ap, ap);
	for (axis = 0; axis < 3; axis++)
		bp[axis] = p[axis] - b[axis];
	d3 = sdf_dot(ab, bp);
	d4 = sdf_dot(ac, bp);
	if (d3 >= 0.0f && d4 <= d3)
		return sdf_dot(bp, bp);
	vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
	{
		v = d1 / (d1 - d3);
		for (axis = 0; axis < 3; axis++)
			d[axis] = ap[axis] - v * ab[axis];
		return sdf_dot(d, d);
	}
	for (axis = 0; axis < 3; axis++)
		cp[axis] = p[axis] - c[axis];
	d5 = sdf_dot(ab, cp);
	d6 = sdf_dot(ac, cp);
	if (d6 >= 0.0f && d5 <= d6)
		return sdf_dot(cp, cp);
	vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
	{
		w = d2 / (d2 - d6);
		for (axis = 0; axis < 3; axis++)
			d[axis] = ap[axis] - w * ac[axis];
		return sdf_dot(d, d);
	}
	va = d3 * d6 - d5 * d4;
	if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f)
	{
		w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		for (axis = 0; axis < 3; axis++)
			d[axis] = bp[axis] - w * (c[axis] - b[axis]);
		return sdf_dot(d, d);
	}
	denominator = 1.0f / (va + vb + vc);
	v = vb * denominator;
	w = vc * denominator;
	for (axis = 0; axis < 3; axis++)
	{
		closest[axis] = ab[axis] * v + ac[axis] * w;
		d[axis] = ap[axis] - closest[axis];
	}
	return sdf_dot(d, d);
}

/* a triangle's edges, unit normal, plane offset and bounding sphere (about
its centroid), and whether it has no area worth the name (its edges alone,
and no plane) */
struct sdf_triangle
{
	float const *a, *b, *c;
	float ab[3], ac[3], normal[3], offset, centre[3], radius, low[3], high[3];
	int flat;
};

static void sdf_triangle_make(float const *corners, struct sdf_triangle *triangle)
{
	float const *a = corners, *b = corners + 3, *c = corners + 6, *each[3];
	float length;
	int axis, corner;

	triangle->a = a;
	triangle->b = b;
	triangle->c = c;
	for (axis = 0; axis < 3; axis++)
	{
		triangle->ab[axis] = b[axis] - a[axis];
		triangle->ac[axis] = c[axis] - a[axis];
		triangle->centre[axis] = (a[axis] + b[axis] + c[axis]) / 3.0f;
		triangle->low[axis] = fminf(a[axis], fminf(b[axis], c[axis]));
		triangle->high[axis] = fmaxf(a[axis], fmaxf(b[axis], c[axis]));
	}
	triangle->normal[0] = triangle->ab[1] * triangle->ac[2] - triangle->ab[2] * triangle->ac[1];
	triangle->normal[1] = triangle->ab[2] * triangle->ac[0] - triangle->ab[0] * triangle->ac[2];
	triangle->normal[2] = triangle->ab[0] * triangle->ac[1] - triangle->ab[1] * triangle->ac[0];
	length = sqrtf(sdf_dot(triangle->normal, triangle->normal));
	triangle->flat = !(length > 1.0e-6f * (sdf_dot(triangle->ab, triangle->ab) + sdf_dot(triangle->ac, triangle->ac)));
	if (!triangle->flat)
	{
		for (axis = 0; axis < 3; axis++)
			triangle->normal[axis] /= length;
	}
	triangle->offset = triangle->flat ? 0.0f : sdf_dot(triangle->normal, a);
	each[0] = a;
	each[1] = b;
	each[2] = c;
	triangle->radius = 0.0f;
	for (corner = 0; corner < 3; corner++)
	{
		float d[3], squared;

		for (axis = 0; axis < 3; axis++)
			d[axis] = each[corner][axis] - triangle->centre[axis];
		squared = sdf_dot(d, d);
		if (squared > triangle->radius)
			triangle->radius = squared;
	}
	triangle->radius = sqrtf(triangle->radius);
}

static float sdf_triangle_distance_squared(struct sdf_triangle const *triangle, float const point[3])
{
	return sdf_triangle_squared(point, triangle->a, triangle->b, triangle->c, triangle->ab, triangle->ac,
		triangle->flat);
}

/* the indices along an axis of points origin + step * i (i 0 to count - 1)
from low to high (world units), first to last inclusive; FALSE if none */
static int sdf_span(float low, float high, float origin, float step, long count, long *first, long *last)
{
	float from = ceilf((low - origin) / step), to = floorf((high - origin) / step);

	if (from < 0.0f)
		from = 0.0f;
	if (to > (float)(count - 1))
		to = (float)(count - 1);
	if (from > to)
		return 0;
	*first = (long)from;
	*last = (long)to;
	return 1;
}

/* along a row of points (y and z fixed) within reach of the triangle's
plane and of its bounding sphere grown by reach: the x span (world units)
low to high narrowed; FALSE if none of the row is */
static int sdf_row(struct sdf_triangle const *triangle, float y, float z, float reach, float *low, float *high)
{
	float dy = y - triangle->centre[1], dz = z - triangle->centre[2];
	float sphere = triangle->radius + reach;
	float across = sphere * sphere - dy * dy - dz * dz, plane_row;

	if (across <= 0.0f)
		return 0;
	across = sqrtf(across);
	if (triangle->centre[0] - across > *low)
		*low = triangle->centre[0] - across;
	if (triangle->centre[0] + across < *high)
		*high = triangle->centre[0] + across;
	if (triangle->flat)
		return *low <= *high;
	plane_row = triangle->normal[1] * y + triangle->normal[2] * z - triangle->offset;
	if (fabsf(triangle->normal[0]) > 1.0e-6f)
	{
		float one = (-reach - plane_row) / triangle->normal[0], other = (reach - plane_row) / triangle->normal[0];

		if (one > other)
		{
			float swap = one;

			one = other;
			other = swap;
		}
		if (one > *low)
			*low = one;
		if (other < *high)
			*high = other;
	}
	else if (fabsf(plane_row) >= reach)
	{
		return 0;
	}
	return *low <= *high;
}

/* ---------- the cells a surface may be within range of */

enum
{
	_sdf_cells_mark,
	_sdf_cells_count,
	_sdf_cells_fill,
};

/* the cells of slices z_first to z_end - 1 within range and half a cell's
diagonal of a triangle (from their middles): marked, counted for it
(firsts[cell + 1]++, a marked one), or given it (indices[firsts[cell]++]);
how many */
static long sdf_cells_near(struct world_sdf_header const *header, struct sdf_triangle const *triangle,
	long triangle_index, int what, unsigned char *marks, long *firsts, long *indices, long z_first, long z_end)
{
	float step = WORLD_SDF_BRICK * header->voxel;
	float reach = header->range + 0.5f * step * 1.7320508f;
	long count[3], first[3], last[3], x, y, z, found = 0;
	float middle[3];
	int axis;

	count[0] = (long)header->width;
	count[1] = (long)header->height;
	count[2] = (long)header->depth;
	for (axis = 0; axis < 3; axis++)
	{
		middle[axis] = header->origin[axis] + 0.5f * step;
		if (!sdf_span(triangle->low[axis] - reach, triangle->high[axis] + reach, middle[axis], step, count[axis],
			&first[axis], &last[axis]))
		{
			return 0;
		}
	}
	if (first[2] < z_first)
		first[2] = z_first;
	if (last[2] > z_end - 1)
		last[2] = z_end - 1;
	for (z = first[2]; z <= last[2]; z++)
	{
		float point_z = middle[2] + (float)z * step;

		for (y = first[1]; y <= last[1]; y++)
		{
			float point_y = middle[1] + (float)y * step;
			float low = middle[0] + (float)first[0] * step, high = middle[0] + (float)last[0] * step;
			long row_first, row_last;

			if (!sdf_row(triangle, point_y, point_z, reach, &low, &high) ||
				!sdf_span(low, high, middle[0], step, count[0], &row_first, &row_last))
			{
				continue;
			}
			for (x = row_first; x <= row_last; x++)
			{
				unsigned long cell = ((unsigned long)z * (unsigned long)count[1] + (unsigned long)y) *
					(unsigned long)count[0] + (unsigned long)x;
				float point[3];

				if (what != _sdf_cells_mark && !marks[cell])
					continue;
				point[0] = middle[0] + (float)x * step;
				point[1] = point_y;
				point[2] = point_z;
				if (sdf_triangle_distance_squared(triangle, point) >= reach * reach)
					continue;
				found++;
				if (what == _sdf_cells_mark)
					marks[cell] = 1;
				else if (what == _sdf_cells_count)
					firsts[cell + 1]++;
				else
					indices[firsts[cell]++] = triangle_index;
			}
		}
	}
	return found;
}

/* whether a triangle may reach slices z_first to z_end - 1 (its corners' z
against theirs grown by reach), before it is made */
static int sdf_triangle_in_slices(struct world_sdf_header const *header, float const *corners, long z_first,
	long z_end)
{
	float step = WORLD_SDF_BRICK * header->voxel, reach = header->range + step;
	float low = fminf(corners[2], fminf(corners[5], corners[8])), high = fmaxf(corners[2], fmaxf(corners[5], corners[8]));

	return high + reach >= header->origin[2] + (float)z_first * step &&
		low - reach <= header->origin[2] + (float)z_end * step;
}

long world_sdf_mark_slices(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char *marks, long z_first, long z_end)
{
	unsigned long slice = header->width * header->height, cell;
	long triangle, count = 0;

	memset(marks + (unsigned long)z_first * slice, 0, (unsigned long)(z_end - z_first) * slice);
	for (triangle = 0; triangle < triangle_count; triangle++)
	{
		struct sdf_triangle made;

		if (!sdf_triangle_in_slices(header, positions + triangle * 9, z_first, z_end))
			continue;
		sdf_triangle_make(positions + triangle * 9, &made);
		sdf_cells_near(header, &made, triangle, _sdf_cells_mark, marks, NULL, NULL, z_first, z_end);
	}
	for (cell = (unsigned long)z_first * slice; cell < (unsigned long)z_end * slice; cell++)
		count += marks[cell];
	return count;
}

long world_sdf_mark(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char *marks)
{
	return world_sdf_mark_slices(header, positions, triangle_count, marks, 0, (long)header->depth);
}

void world_sdf_bin_slices(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char const *marks, long *firsts, long *indices, long z_first, long z_end)
{
	long triangle;

	for (triangle = 0; triangle < triangle_count; triangle++)
	{
		struct sdf_triangle made;

		if (!sdf_triangle_in_slices(header, positions + triangle * 9, z_first, z_end))
			continue;
		sdf_triangle_make(positions + triangle * 9, &made);
		sdf_cells_near(header, &made, triangle, indices ? _sdf_cells_fill : _sdf_cells_count, (unsigned char *)marks,
			firsts, indices, z_first, z_end);
	}
}

long *world_sdf_bin(struct world_sdf_header const *header, float const *positions, long triangle_count,
	unsigned char const *marks, long **firsts_out)
{
	unsigned long cells = header->width * header->height * header->depth;
	long *firsts = calloc(cells + 1, sizeof(*firsts)), *indices;

	*firsts_out = NULL;
	if (!firsts)
		return NULL;
	world_sdf_bin_slices(header, positions, triangle_count, marks, firsts, NULL, 0, (long)header->depth);
	indices = world_sdf_bin_place(header, firsts);
	if (!indices)
	{
		free(firsts);
		return NULL;
	}
	world_sdf_bin_slices(header, positions, triangle_count, marks, firsts, indices, 0, (long)header->depth);
	world_sdf_bin_placed(header, firsts);
	*firsts_out = firsts;
	return indices;
}

long *world_sdf_bin_place(struct world_sdf_header const *header, long *firsts)
{
	unsigned long cells = header->width * header->height * header->depth, cell;

	for (cell = 0; cell < cells; cell++)
		firsts[cell + 1] += firsts[cell];
	return malloc(((size_t)firsts[cells] + 1) * sizeof(long));
}

void world_sdf_bin_placed(struct world_sdf_header const *header, long *firsts)
{
	unsigned long cell;

	/* (each cell's start, moved up as its triangles were put: put back) */
	for (cell = header->width * header->height * header->depth; cell > 0; cell--)
		firsts[cell] = firsts[cell - 1];
	firsts[0] = 0;
}

static long sdf_mark_all(void *context, struct world_sdf_header const *header, float const *positions,
	long triangle_count, unsigned char *marks)
{
	(void)context;
	return world_sdf_mark(header, positions, triangle_count, marks);
}

unsigned char *world_sdf_plan(float const bounds[6], float const *positions, long triangle_count,
	struct world_sdf_header *header, world_sdf_marker mark, void *context)
{
	float voxel = (float)WORLD_SDF_FINEST_VOXEL_MILLI / 1000.0f;
	unsigned char *marks = NULL;

	if (!mark)
		mark = sdf_mark_all;
	for (;;)
	{
		unsigned long cells, bytes;
		unsigned char *grown;
		long marked;
		int steps;

		world_sdf_layout(bounds, voxel, header);
		cells = header->width * header->height * header->depth;
		if (header->width > WORLD_SDF_MAXIMUM_DIMENSION || header->height > WORLD_SDF_MAXIMUM_DIMENSION ||
			header->depth > WORLD_SDF_MAXIMUM_DIMENSION || world_sdf_bytes(header, 0) > WORLD_SDF_MAXIMUM_BYTES)
		{
			voxel *= 1.125f;
			continue;
		}
		grown = realloc(marks, cells);
		if (!grown)
		{
			free(marks);
			return NULL;
		}
		marks = grown;
		marked = mark(context, header, positions, triangle_count, marks);
		bytes = world_sdf_bytes(header, (unsigned long)marked);
		if (bytes <= WORLD_SDF_MAXIMUM_BYTES)
			return marks;
		/* (the bricks' memory falls at least as the voxel's square grows: a
		step short of as many steps as that says, so that none finer that
		fits is passed over) */
		{
			double predicted = (double)bytes;

			for (steps = 0; predicted > (double)WORLD_SDF_MAXIMUM_BYTES && steps < 8; steps++)
				predicted /= 1.125 * 1.125;
		}
		for (steps = steps > 1 ? steps - 1 : 1; steps > 0; steps--)
			voxel *= 1.125f;
	}
}

/* ---------- a cell's brick */

static void sdf_cell_place(struct world_sdf_header const *header, unsigned long cell, long base[3])
{
	base[0] = (long)(cell % header->width) * WORLD_SDF_BRICK;
	base[1] = (long)(cell / header->width % header->height) * WORLD_SDF_BRICK;
	base[2] = (long)(cell / (header->width * header->height)) * WORLD_SDF_BRICK;
}

/* a voxel's centre (its whole grid's indices) */
static void sdf_voxel_centre(struct world_sdf_header const *header, long x, long y, long z, float point[3])
{
	point[0] = header->origin[0] + (float)x * header->voxel;
	point[1] = header->origin[1] + (float)y * header->voxel;
	point[2] = header->origin[2] + (float)z * header->voxel;
}

int world_sdf_bake_cell(struct world_sdf_header const *header, unsigned long cell, float const *positions,
	long const *triangles, long triangle_count, int (*inside)(void *context, float const point[3]),
	void *context, signed char *brick)
{
	float best[WORLD_SDF_BRICK_VOXELS], voxel = header->voxel, range = header->range;
	long base[3], index, x, y, z;
	int outside_all = 1, inside_all = 1;

	sdf_cell_place(header, cell, base);
	for (index = 0; index < WORLD_SDF_BRICK_VOXELS; index++)
		best[index] = range;
	for (index = 0; index < triangle_count; index++)
	{
		struct sdf_triangle triangle;
		long first[3], last[3];
		int axis;

		sdf_triangle_make(positions + triangles[index] * 9, &triangle);
		for (axis = 0; axis < 3; axis++)
		{
			float origin = header->origin[axis] + (float)base[axis] * voxel;

			if (!sdf_span(triangle.low[axis] - range, triangle.high[axis] + range, origin, voxel,
				WORLD_SDF_BRICK_SPAN, &first[axis], &last[axis]))
			{
				break;
			}
		}
		if (axis < 3)
			continue;
		for (z = first[2]; z <= last[2]; z++)
		{
			float point_z = header->origin[2] + (float)(base[2] + z) * voxel;

			for (y = first[1]; y <= last[1]; y++)
			{
				float point_y = header->origin[1] + (float)(base[1] + y) * voxel;
				float low = header->origin[0] + (float)(base[0] + first[0]) * voxel;
				float high = header->origin[0] + (float)(base[0] + last[0]) * voxel;
				float plane_row = triangle.normal[1] * point_y + triangle.normal[2] * point_z - triangle.offset;
				float *row_best = best + (z * WORLD_SDF_BRICK_SPAN + y) * WORLD_SDF_BRICK_SPAN;
				long row_first, row_last;

				if (!sdf_row(&triangle, point_y, point_z, range, &low, &high) ||
					!sdf_span(low, high, header->origin[0] + (float)base[0] * voxel, voxel, WORLD_SDF_BRICK_SPAN,
					&row_first, &row_last))
				{
					continue;
				}
				for (x = row_first; x <= row_last; x++)
				{
					float point[3], distance;

					point[0] = header->origin[0] + (float)(base[0] + x) * voxel;
					/* (never nearer than its plane) */
					if (!triangle.flat && fabsf(plane_row + triangle.normal[0] * point[0]) >= row_best[x])
						continue;
					point[1] = point_y;
					point[2] = point_z;
					distance = sdf_triangle_distance_squared(&triangle, point);
					if (distance < row_best[x] * row_best[x])
						row_best[x] = sqrtf(distance);
				}
			}
		}
	}
	/* the sign, and the encoding */
	for (z = 0, index = 0; z < WORLD_SDF_BRICK_SPAN; z++)
	{
		for (y = 0; y < WORLD_SDF_BRICK_SPAN; y++)
		{
			for (x = 0; x < WORLD_SDF_BRICK_SPAN; x++, index++)
			{
				float point[3], distance = best[index];

				sdf_voxel_centre(header, base[0] + x, base[1] + y, base[2] + z, point);
				if (inside && inside(context, point))
					distance = -distance;
				brick[index] = world_sdf_encode(distance, range);
				outside_all = outside_all && brick[index] == 127;
				inside_all = inside_all && brick[index] == -127;
			}
		}
	}
	return outside_all ? WORLD_SDF_CELL_OUTSIDE : inside_all ? WORLD_SDF_CELL_INSIDE : WORLD_SDF_CELL_BRICK;
}

int world_sdf_cell_kind(struct world_sdf_header const *header, unsigned long cell,
	int (*inside)(void *context, float const point[3]), void *context)
{
	long base[3];
	float point[3];

	sdf_cell_place(header, cell, base);
	sdf_voxel_centre(header, base[0] + WORLD_SDF_BRICK / 2, base[1] + WORLD_SDF_BRICK / 2,
		base[2] + WORLD_SDF_BRICK / 2, point);
	return inside && inside(context, point) ? WORLD_SDF_CELL_INSIDE : WORLD_SDF_CELL_OUTSIDE;
}

/* ---------- a field of a whole grid */

int world_sdf_pack(struct world_sdf_volume *volume, signed char const *voxels)
{
	struct world_sdf_header *header = &volume->header;
	unsigned long cells = header->width * header->height * header->depth, cell;
	unsigned long row = WORLD_SDF_BRICK * header->width + 1, slice = row * (WORLD_SDF_BRICK * header->height + 1);
	signed char brick[WORLD_SDF_BRICK_VOXELS];
	int pass;

	volume->cells = calloc(cells, sizeof(*volume->cells));
	volume->bricks = NULL;
	memset(&volume->scenery, 0, sizeof(volume->scenery));
	if (!volume->cells)
		return 0;
	/* (counted, then copied) */
	for (pass = 0; pass < 2; pass++)
	{
		header->brick_count = 0;
		for (cell = 0; cell < cells; cell++)
		{
			long base[3], x, y, z, index = 0;
			int outside_all = 1, inside_all = 1;

			sdf_cell_place(header, cell, base);
			for (z = 0; z < WORLD_SDF_BRICK_SPAN; z++)
			{
				for (y = 0; y < WORLD_SDF_BRICK_SPAN; y++)
				{
					for (x = 0; x < WORLD_SDF_BRICK_SPAN; x++, index++)
					{
						brick[index] = voxels[(unsigned long)(base[2] + z) * slice + (unsigned long)(base[1] + y) * row +
							(unsigned long)(base[0] + x)];
						outside_all = outside_all && brick[index] == 127;
						inside_all = inside_all && brick[index] == -127;
					}
				}
			}
			if (outside_all || inside_all)
			{
				volume->cells[cell][3] = outside_all ? WORLD_SDF_CELL_OUTSIDE : WORLD_SDF_CELL_INSIDE;
				continue;
			}
			if (pass)
			{
				unsigned long n = header->brick_count;

				volume->cells[cell][0] = (unsigned char)(n % WORLD_SDF_ATLAS);
				volume->cells[cell][1] = (unsigned char)(n / WORLD_SDF_ATLAS % WORLD_SDF_ATLAS);
				volume->cells[cell][2] = (unsigned char)(n / WORLD_SDF_ATLAS_LAYER);
				volume->cells[cell][3] = WORLD_SDF_CELL_BRICK;
				memcpy(volume->bricks + n * WORLD_SDF_BRICK_VOXELS, brick, sizeof(brick));
			}
			header->brick_count++;
		}
		if (!pass)
		{
			/* (the atlas's layers are at most 256: the table's z is a byte) */
			if (header->brick_count > 256UL * WORLD_SDF_ATLAS_LAYER ||
				!(volume->bricks = malloc(header->brick_count * WORLD_SDF_BRICK_VOXELS + 1)))
			{
				free(volume->cells);
				volume->cells = NULL;
				return 0;
			}
		}
	}
	return 1;
}

/* ---------- the sample, and the GPU's atlas */

float world_sdf_sample(struct world_sdf_volume const *volume, float const point[3])
{
	struct world_sdf_header const *header = &volume->header;
	unsigned long size[3];
	long cell[3], base[3];
	float local[3], fraction[3], result = 0.0f;
	unsigned char const *entry;
	signed char const *brick;
	int axis, corner;

	size[0] = header->width;
	size[1] = header->height;
	size[2] = header->depth;
	for (axis = 0; axis < 3; axis++)
	{
		/* (in voxels from the first voxel's centre, clamped to the edge,
		as a GPU's linear filter with clamp-to-edge reads the voxels; the
		cell, and where in it) */
		float coordinate = (point[axis] - header->origin[axis]) / header->voxel;
		float last = (float)(WORLD_SDF_BRICK * size[axis]);

		if (coordinate < 0.0f)
			coordinate = 0.0f;
		if (coordinate > last)
			coordinate = last;
		cell[axis] = (long)floorf(coordinate / (float)WORLD_SDF_BRICK);
		if (cell[axis] > (long)size[axis] - 1)
			cell[axis] = (long)size[axis] - 1;
		local[axis] = coordinate - (float)(cell[axis] * WORLD_SDF_BRICK);
		base[axis] = (long)floorf(local[axis]);
		if (base[axis] > WORLD_SDF_BRICK - 1)
			base[axis] = WORLD_SDF_BRICK - 1;
		fraction[axis] = local[axis] - (float)base[axis];
	}
	entry = volume->cells[((unsigned long)cell[2] * size[1] + (unsigned long)cell[1]) * size[0] +
		(unsigned long)cell[0]];
	if (entry[3] != WORLD_SDF_CELL_BRICK)
		return entry[3] == WORLD_SDF_CELL_INSIDE ? -header->range : header->range;
	brick = volume->bricks + ((unsigned long)entry[0] + (unsigned long)entry[1] * WORLD_SDF_ATLAS +
		(unsigned long)entry[2] * WORLD_SDF_ATLAS_LAYER) * WORLD_SDF_BRICK_VOXELS;
	for (corner = 0; corner < 8; corner++)
	{
		long x = base[0] + (corner & 1), y = base[1] + ((corner >> 1) & 1), z = base[2] + ((corner >> 2) & 1);
		float weight = ((corner & 1) ? fraction[0] : 1.0f - fraction[0]) *
			(((corner >> 1) & 1) ? fraction[1] : 1.0f - fraction[1]) *
			(((corner >> 2) & 1) ? fraction[2] : 1.0f - fraction[2]);

		if (weight == 0.0f)
			continue;
		result += weight * world_sdf_decode(brick[(z * WORLD_SDF_BRICK_SPAN + y) * WORLD_SDF_BRICK_SPAN + x],
			header->range);
	}
	return result;
}

unsigned long world_sdf_atlas_layers(struct world_sdf_header const *header)
{
	return (header->brick_count + WORLD_SDF_ATLAS_LAYER - 1) / WORLD_SDF_ATLAS_LAYER;
}

void world_sdf_atlas_layer(struct world_sdf_volume const *volume, unsigned long layer, signed char *voxels)
{
	unsigned long row = WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN, slice = row * row, index;

	memset(voxels, 0, WORLD_SDF_ATLAS_LAYER_BYTES);
	for (index = 0; index < WORLD_SDF_ATLAS_LAYER; index++)
	{
		unsigned long n = layer * WORLD_SDF_ATLAS_LAYER + index;
		unsigned long at = index % WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN +
			index / WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN * row;
		signed char const *brick;
		int y, z;

		if (n >= volume->header.brick_count)
			break;
		brick = volume->bricks + n * WORLD_SDF_BRICK_VOXELS;
		for (z = 0; z < WORLD_SDF_BRICK_SPAN; z++)
		{
			for (y = 0; y < WORLD_SDF_BRICK_SPAN; y++)
			{
				memcpy(voxels + z * slice + at + y * row, brick + (z * WORLD_SDF_BRICK_SPAN + y) * WORLD_SDF_BRICK_SPAN,
					WORLD_SDF_BRICK_SPAN);
			}
		}
	}
}

/* ---------- the objects' occlusion fields (docs/world-ao.md, "The objects' occlusion fields")

A vehicle's or a piece of scenery's shape does not change, only where it
is: its occlusion of what is about it is baked once, in its own space. At
each voxel of a grid about it, the share of the cosine-weighted hemisphere
its geometry blocks, for every normal, is fitted as sat(t + n . v): four
numbers, which the shaders read with one filtered fetch. Each voxel looks
along WORLD_SDF_OBJECT_RAYS fixed directions (a Fibonacci sphere's) and
finds how much of each the triangles block: all of it for an opaque
triangle, its opacity for one partly so (alpha-tested leaves, whose texture
lets the rest through), each triangle crossed letting through its share of
what is left. The occlusion at each of WORLD_SDF_OBJECT_NORMALS normals is
the directions' cosines' sum, each by its share blocked, and t, v are its
clamped least squares fit. A voxel in solid takes its neighbours' outside, so that
a point just outside the geometry is never filtered with the inside's
darkness. Pure, and each voxel its own: the same on any number of threads. */

/* n points of a Fibonacci sphere (the golden angle about z, z evenly
spaced), offset by half a step */
static void sdf_object_sphere(long count, float (*points)[3])
{
	long index;

	for (index = 0; index < count; index++)
	{
		float z = 1.0f - (2.0f * (float)index + 1.0f) / (float)count, r = sqrtf(1.0f - z * z);
		float angle = (float)index * 2.39996323f;

		points[index][0] = r * cosf(angle);
		points[index][1] = r * sinf(angle);
		points[index][2] = z;
	}
}

static void sdf_cross(float const a[3], float const b[3], float out[3])
{
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

void world_sdf_object_layout(float const bounds[6], unsigned long most_voxels, struct world_sdf_object *object)
{
	float longest = 0.0f, margin, voxel = WORLD_SDF_OBJECT_LEAST_VOXEL, low[3], size[3];
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float side = bounds[3 + axis] - bounds[axis];

		longest = side > longest ? side : longest;
	}
	margin = WORLD_SDF_OBJECT_MARGIN * longest;
	margin = margin < WORLD_SDF_OBJECT_LEAST_MARGIN ? WORLD_SDF_OBJECT_LEAST_MARGIN :
		margin > WORLD_SDF_OBJECT_MOST_MARGIN ? WORLD_SDF_OBJECT_MOST_MARGIN : margin;
	for (axis = 0; axis < 3; axis++)
	{
		low[axis] = bounds[axis] - margin;
		size[axis] = bounds[3 + axis] - bounds[axis] + 2.0f * margin;
	}
	for (;;)
	{
		unsigned long count[3];

		for (axis = 0; axis < 3; axis++)
			count[axis] = (unsigned long)ceilf(size[axis] / voxel) + 1;
		if ((count[0] <= WORLD_SDF_OBJECT_MOST_SIDE && count[1] <= WORLD_SDF_OBJECT_MOST_SIDE &&
			count[2] <= WORLD_SDF_OBJECT_MOST_SIDE && count[0] * count[1] * count[2] <= most_voxels) ||
			voxel > 1000.0f)
		{
			object->width = count[0];
			object->height = count[1];
			object->depth = count[2];
			break;
		}
		voxel *= WORLD_SDF_OBJECT_GROWTH;
	}
	/* (centred over the grown bounds) */
	for (axis = 0; axis < 3; axis++)
	{
		unsigned long count = axis == 0 ? object->width : axis == 1 ? object->height : object->depth;

		object->origin[axis] = low[axis] + 0.5f * size[axis] - 0.5f * (float)(count - 1) * voxel;
	}
	object->voxel = voxel;
	object->reach = 0.0f;
	memcpy(object->bounds, bounds, sizeof(object->bounds));
	object->texels = NULL;
	object->atlas[0] = object->atlas[1] = object->atlas[2] = 0;
}

/* the least weight of a normal in the fit (each normal's is this and its
occlusion) */
#define WORLD_SDF_OBJECT_FIT_WEIGHT 0.02

/* the fit of t, v to occlusions at normals, sat(t + n . v): least squares
over the normals whose fit is not clamped where it should be, a few steps
from the plain least squares (Gauss and Newton's, the clamp's active set) */
static void sdf_object_fit(float const (*normals)[3], float const *occlusions, float fit[4])
{
	double x[4] = { 0.0, 0.0, 0.0, 0.0 };
	int step, k, a, b, c;

	unsigned char active[WORLD_SDF_OBJECT_NORMALS], last[WORLD_SDF_OBJECT_NORMALS];

	for (step = 0; step < 12; step++)
	{
		double m[4][5];

		memset(m, 0, sizeof(m));
		memcpy(last, active, sizeof(last));
		for (k = 0; k < WORLD_SDF_OBJECT_NORMALS; k++)
		{
			double f[4], value, weight;

			f[0] = 1.0;
			f[1] = normals[k][0];
			f[2] = normals[k][1];
			f[3] = normals[k][2];
			value = x[0] + x[1] * f[1] + x[2] * f[2] + x[3] * f[3];
			/* (a normal whose fit is clamped to the side it should be on
			leaves the fit alone) */
			active[k] = !step || !((value <= 0.0 && occlusions[k] <= 0.0f) || (value >= 1.0 && occlusions[k] >= 1.0f));
			if (!active[k])
				continue;
			/* (weighted to the normals that see the most of it: a surface
			under it faces it, and its contact darkening is what shows) */
			weight = WORLD_SDF_OBJECT_FIT_WEIGHT + occlusions[k];
			for (a = 0; a < 4; a++)
			{
				for (b = 0; b < 4; b++)
					m[a][b] += weight * f[a] * f[b];
				m[a][4] += weight * f[a] * occlusions[k];
			}
		}
		/* (the same normals as the step before: the same fit) */
		if (step > 1 && !memcmp(active, last, sizeof(active)))
			break;
		for (a = 0; a < 4; a++)
			m[a][a] += 1.0e-6;
		for (a = 0; a < 4; a++)
		{
			for (b = a + 1; b < 4; b++)
			{
				double scale = m[b][a] / m[a][a];

				for (c = a; c < 5; c++)
					m[b][c] -= scale * m[a][c];
			}
		}
		for (a = 3; a >= 0; a--)
		{
			double sum = m[a][4];

			for (b = a + 1; b < 4; b++)
				sum -= m[a][b] * x[b];
			x[a] = sum / m[a][a];
		}
	}
	for (a = 0; a < 4; a++)
		fit[a] = (float)x[a];
}

/* the rays in buckets about a cube's faces' cells (each its cone: its
middle's direction and the cosine of its widest ray from it), so that a
triangle seen within a narrow cone tests only the buckets that cone meets */
enum
{
	SDF_OBJECT_CELLS = 3,
	SDF_OBJECT_BUCKETS = 6 * SDF_OBJECT_CELLS * SDF_OBJECT_CELLS,
};

struct sdf_object_buckets
{
	float axes[SDF_OBJECT_BUCKETS][3];
	float sines[SDF_OBJECT_BUCKETS], cosines[SDF_OBJECT_BUCKETS];
	short firsts[SDF_OBJECT_BUCKETS + 1];
	short rays[WORLD_SDF_OBJECT_RAYS];
	/* the rays in the buckets' order, by axis (a bucket's run of them
	tested together: world_sdf_object_slice), and each ray's place in it */
	float ordered[3][WORLD_SDF_OBJECT_RAYS];
	short places[WORLD_SDF_OBJECT_RAYS];
	/* the buckets' axes by axis (their cones tested together) */
	float axes_by_axis[3][SDF_OBJECT_BUCKETS];
};

static int sdf_object_bucket(float const ray[3])
{
	float ax = fabsf(ray[0]), ay = fabsf(ray[1]), az = fabsf(ray[2]), u, v;
	int face, cu, cv;

	if (ax >= ay && ax >= az)
	{
		face = ray[0] > 0.0f ? 0 : 1;
		u = ray[1] / ax;
		v = ray[2] / ax;
	}
	else if (ay >= az)
	{
		face = ray[1] > 0.0f ? 2 : 3;
		u = ray[0] / ay;
		v = ray[2] / ay;
	}
	else
	{
		face = ray[2] > 0.0f ? 4 : 5;
		u = ray[0] / az;
		v = ray[1] / az;
	}
	cu = (int)((u + 1.0f) * 0.5f * SDF_OBJECT_CELLS);
	cv = (int)((v + 1.0f) * 0.5f * SDF_OBJECT_CELLS);
	cu = cu < 0 ? 0 : cu >= SDF_OBJECT_CELLS ? SDF_OBJECT_CELLS - 1 : cu;
	cv = cv < 0 ? 0 : cv >= SDF_OBJECT_CELLS ? SDF_OBJECT_CELLS - 1 : cv;
	return (face * SDF_OBJECT_CELLS + cv) * SDF_OBJECT_CELLS + cu;
}

static void sdf_object_buckets_make(float const (*rays)[3], struct sdf_object_buckets *buckets)
{
	short counts[SDF_OBJECT_BUCKETS];
	int bucket, j, k;

	memset(counts, 0, sizeof(counts));
	memset(buckets->axes, 0, sizeof(buckets->axes));
	for (j = 0; j < WORLD_SDF_OBJECT_RAYS; j++)
	{
		bucket = sdf_object_bucket(rays[j]);
		counts[bucket]++;
		for (k = 0; k < 3; k++)
			buckets->axes[bucket][k] += rays[j][k];
	}
	buckets->firsts[0] = 0;
	for (bucket = 0; bucket < SDF_OBJECT_BUCKETS; bucket++)
	{
		float length = sqrtf(sdf_dot(buckets->axes[bucket], buckets->axes[bucket]));

		buckets->firsts[bucket + 1] = (short)(buckets->firsts[bucket] + counts[bucket]);
		for (k = 0; k < 3; k++)
			buckets->axes[bucket][k] = length > 0.0f ? buckets->axes[bucket][k] / length : 0.0f;
		buckets->cosines[bucket] = 1.0f;
		counts[bucket] = 0;
	}
	for (j = 0; j < WORLD_SDF_OBJECT_RAYS; j++)
	{
		float cosine;

		bucket = sdf_object_bucket(rays[j]);
		buckets->places[j] = (short)(buckets->firsts[bucket] + counts[bucket]);
		buckets->rays[buckets->firsts[bucket] + counts[bucket]++] = (short)j;
		for (k = 0; k < 3; k++)
			buckets->ordered[k][buckets->places[j]] = rays[j][k];
		cosine = sdf_dot(buckets->axes[bucket], rays[j]);
		/* (a little wider: the cones' test is never the reason a ray is missed) */
		cosine -= 1.0e-3f;
		buckets->cosines[bucket] = cosine < buckets->cosines[bucket] ? cosine : buckets->cosines[bucket];
	}
	for (bucket = 0; bucket < SDF_OBJECT_BUCKETS; bucket++)
	{
		float c = buckets->cosines[bucket];

		buckets->sines[bucket] = c < 1.0f ? sqrtf(1.0f - c * c) : 0.0f;
		for (k = 0; k < 3; k++)
			buckets->axes_by_axis[k][bucket] = buckets->axes[bucket][k];
	}
}

/* the directions looked along and the normals fitted, their buckets, and
each normal's cosine to each ray by the ray's share of the sphere over pi
(the cosine-weighted hemisphere's occlusion is their sum over the blocked
rays): the same for every voxel and every object */
struct world_sdf_object_tables
{
	float rays[WORLD_SDF_OBJECT_RAYS][3];
	float normals[WORLD_SDF_OBJECT_NORMALS][3];
	float weights[WORLD_SDF_OBJECT_RAYS * WORLD_SDF_OBJECT_NORMALS];
	struct sdf_object_buckets buckets;
};

struct world_sdf_object_tables *world_sdf_object_tables_make(void)
{
	struct world_sdf_object_tables *tables = malloc(sizeof(*tables));
	long k, j;

	if (!tables)
		return NULL;
	sdf_object_sphere(WORLD_SDF_OBJECT_RAYS, tables->rays);
	sdf_object_sphere(WORLD_SDF_OBJECT_NORMALS, tables->normals);
	sdf_object_buckets_make((float const (*)[3])tables->rays, &tables->buckets);
	for (k = 0; k < WORLD_SDF_OBJECT_NORMALS; k++)
	{
		for (j = 0; j < WORLD_SDF_OBJECT_RAYS; j++)
		{
			float cosine = sdf_dot(tables->normals[k], tables->rays[j]);

			tables->weights[j * WORLD_SDF_OBJECT_NORMALS + k] = cosine > 0.0f ?
				cosine * 4.0f / (float)WORLD_SDF_OBJECT_RAYS : 0.0f;
		}
	}
	return tables;
}

void world_sdf_object_tables_free(struct world_sdf_object_tables *tables)
{
	free(tables);
}

void world_sdf_object_slice(struct world_sdf_object_tables const *tables, struct world_sdf_object const *object,
	float const *positions, float const *opacities, long triangle_count, int (*inside)(void *context,
	float const point[3]), void *context, unsigned long z, unsigned long first_row, unsigned long end_row, float *values,
	unsigned char *solid)
{
	float const (*normals)[3] = tables->normals;
	float const *weights = tables->weights;
	struct sdf_object_buckets const *buckets = &tables->buckets;
	/* (each direction's light let through, in the buckets' order: the
	product of 1 less the opacity of each triangle it crosses; 0 once an
	opaque one blocks it) */
	float through[WORLD_SDF_OBJECT_RAYS];
	float const *xs = buckets->ordered[0], *ys = buckets->ordered[1], *zs = buckets->ordered[2];
	float occlusions[WORLD_SDF_OBJECT_NORMALS];
	unsigned long x, y;
	long k, j, triangle;

	for (y = first_row; y < end_row && y < object->height; y++)
	{
		for (x = 0; x < object->width; x++)
		{
			unsigned long at = y * object->width + x;
			float point[3], *value = values + at * 4;

			point[0] = object->origin[0] + object->voxel * (float)x;
			point[1] = object->origin[1] + object->voxel * (float)y;
			point[2] = object->origin[2] + object->voxel * (float)z;
			solid[at] = inside && inside(context, point) ? 1 : 0;
			if (solid[at])
			{
				value[0] = 1.0f;
				value[1] = value[2] = value[3] = 0.0f;
				continue;
			}
			for (j = 0; j < WORLD_SDF_OBJECT_RAYS; j++)
				through[j] = 1.0f;
			for (triangle = 0; triangle < triangle_count; triangle++)
			{
				float const *t = positions + triangle * 9;
				float a[3], b[3], c[3], ab[3], bc[3], ca[3], determinant, side;
				float passed = opacities ? 1.0f - opacities[triangle] : 0.0f;
				int axis;

				for (axis = 0; axis < 3; axis++)
				{
					a[axis] = t[axis] - point[axis];
					b[axis] = t[3 + axis] - point[axis];
					c[axis] = t[6 + axis] - point[axis];
				}
				sdf_cross(a, b, ab);
				sdf_cross(b, c, bc);
				sdf_cross(c, a, ca);
				determinant = sdf_dot(a, bc);
				/* (seen edge on, or through the point: it blocks nothing) */
				if (fabsf(determinant) < 1.0e-12f)
					continue;
				side = determinant > 0.0f ? 1.0f : -1.0f;
				/* (the cone of its corners' directions, which holds it: their
				mean's direction, the least cosine to a corner) */
				{
					float la = sqrtf(sdf_dot(a, a)), lb = sqrtf(sdf_dot(b, b)), lc = sqrtf(sdf_dot(c, c)), axis_vector[3];
					float length, cosine, sine;
					int bucket, narrow;
					unsigned char missed[SDF_OBJECT_BUCKETS];

					for (axis = 0; axis < 3; axis++)
						axis_vector[axis] = a[axis] / la + b[axis] / lb + c[axis] / lc;
					length = sqrtf(sdf_dot(axis_vector, axis_vector));
					cosine = length > 1.0e-6f ? sdf_dot(axis_vector, a) / (length * la) : -1.0f;
					cosine = length > 1.0e-6f && sdf_dot(axis_vector, b) / (length * lb) < cosine ?
						sdf_dot(axis_vector, b) / (length * lb) : cosine;
					cosine = length > 1.0e-6f && sdf_dot(axis_vector, c) / (length * lc) < cosine ?
						sdf_dot(axis_vector, c) / (length * lc) : cosine;
					cosine -= 1.0e-3f;
					sine = cosine > -1.0f && cosine < 1.0f ? sqrtf(1.0f - cosine * cosine) : 0.0f;
					/* (the two cones meet where the angle between their axes is
					at most the sum of theirs: cos(between) >= cos(sum); a cone of
					90 degrees or more meets every bucket. Every bucket's test at
					once, then the rays of those it meets) */
					narrow = cosine > 0.0f && length > 1.0e-6f;
					for (bucket = 0; narrow && bucket < SDF_OBJECT_BUCKETS; bucket++)
					{
						float between = (axis_vector[0] * buckets->axes_by_axis[0][bucket] +
							axis_vector[1] * buckets->axes_by_axis[1][bucket] +
							axis_vector[2] * buckets->axes_by_axis[2][bucket]) / length;

						missed[bucket] = between < cosine * buckets->cosines[bucket] - sine * buckets->sines[bucket];
					}
					for (bucket = 0; bucket < SDF_OBJECT_BUCKETS; bucket++)
					{
						short ray;

						if (narrow && missed[bucket])
							continue;
						/* (a direction within the triangle's corners as seen from the
						point: a positive mix of them. Without branches, so that the
						bucket's run is tested a few rays at once: a ray blocked
						already stays 0) */
						for (ray = buckets->firsts[bucket]; ray < buckets->firsts[bucket + 1]; ray++)
						{
							float to_ab = side * (xs[ray] * ab[0] + ys[ray] * ab[1] + zs[ray] * ab[2]);
							float to_bc = side * (xs[ray] * bc[0] + ys[ray] * bc[1] + zs[ray] * bc[2]);
							float to_ca = side * (xs[ray] * ca[0] + ys[ray] * ca[1] + zs[ray] * ca[2]);
							int within = (to_ab >= 0.0f) & (to_bc >= 0.0f) & (to_ca >= 0.0f);

							through[ray] = within ? through[ray] * passed : through[ray];
						}
					}
				}
			}
			{
				int any = 0;

				memset(occlusions, 0, sizeof(occlusions));
				for (j = 0; j < WORLD_SDF_OBJECT_RAYS; j++)
				{
					float const *row = weights + j * WORLD_SDF_OBJECT_NORMALS;
					float blocked = 1.0f - through[buckets->places[j]];

					if (blocked <= 0.0f)
						continue;
					any = 1;
					for (k = 0; k < WORLD_SDF_OBJECT_NORMALS; k++)
						occlusions[k] += row[k] * blocked;
				}
				/* (nothing blocked: none, for every normal) */
				if (!any)
				{
					value[0] = value[1] = value[2] = value[3] = 0.0f;
					continue;
				}
				for (k = 0; k < WORLD_SDF_OBJECT_NORMALS; k++)
					occlusions[k] = occlusions[k] > 1.0f ? 1.0f : occlusions[k];
			}
			sdf_object_fit(normals, occlusions, value);
		}
	}
}

unsigned short world_sdf_half(float value)
{
	unsigned int bits, sign, exponent, mantissa;

	memcpy(&bits, &value, sizeof(bits));
	sign = (bits >> 16) & 0x8000u;
	exponent = (bits >> 23) & 0xFFu;
	mantissa = bits & 0x7FFFFFu;
	if (exponent == 0xFFu)
		return (unsigned short)(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
	if (exponent > 142)
		return (unsigned short)(sign | 0x7C00u);
	if (exponent < 113)
	{
		/* (a subnormal half, or zero: rounded to nearest even) */
		unsigned int shift, half, rest, halfway;

		if (exponent < 102)
			return (unsigned short)sign;
		mantissa |= 0x800000u;
		shift = 126 - exponent;
		half = mantissa >> shift;
		rest = mantissa & ((1u << shift) - 1u);
		halfway = 1u << (shift - 1);
		if (rest > halfway || (rest == halfway && (half & 1u)))
			half++;
		return (unsigned short)(sign | half);
	}
	{
		unsigned int half = ((exponent - 112) << 10) | (mantissa >> 13), rest = mantissa & 0x1FFFu;

		if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))
			half++;
		return (unsigned short)(sign | half);
	}
}

float world_sdf_half_value(unsigned short half)
{
	unsigned int sign = (unsigned int)(half & 0x8000u) << 16, exponent = (half >> 10) & 0x1Fu, mantissa = half & 0x3FFu;
	unsigned int bits;
	float value;

	if (exponent == 0)
	{
		value = (float)mantissa * (1.0f / 16777216.0f);
		return sign ? -value : value;
	}
	bits = sign | (exponent == 0x1Fu ? 0x7F800000u | (mantissa << 13) : ((exponent + 112) << 23) | (mantissa << 13));
	memcpy(&value, &bits, sizeof(value));
	return value;
}

int world_sdf_object_finish(struct world_sdf_object *object, float const *values, unsigned char const *solid)
{
	unsigned long count = object->width * object->height * object->depth, index;
	float *filled = malloc(count * 4 * sizeof(float) + 1);
	/* (0 outside, 1 in solid and not yet filled, then the step it was) */
	unsigned short *marks = malloc(count * sizeof(unsigned short) + 1);
	float centre[3], most = 0.0f, farthest = 0.0f;
	unsigned short step;
	int changed = 1;

	if (!filled || !marks)
	{
		free(filled);
		free(marks);
		return 0;
	}
	memcpy(filled, values, count * 4 * sizeof(float));
	for (index = 0; index < count; index++)
		marks[index] = solid[index] ? 1 : 0;
	for (step = 2; changed && step < 0xFFFF; step++)
	{
		unsigned long x, y, z;

		changed = 0;
		for (z = 0; z < object->depth; z++)
		for (y = 0; y < object->height; y++)
		for (x = 0; x < object->width; x++)
		{
			static const int offsets[6][3] = { { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 } };
			unsigned long at = (z * object->height + y) * object->width + x;
			float sum[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			int near, n = 0, k;

			if (marks[at] != 1)
				continue;
			for (near = 0; near < 6; near++)
			{
				long nx = (long)x + offsets[near][0], ny = (long)y + offsets[near][1], nz = (long)z + offsets[near][2];
				unsigned long other;

				if (nx < 0 || ny < 0 || nz < 0 || nx >= (long)object->width || ny >= (long)object->height ||
					nz >= (long)object->depth)
				{
					continue;
				}
				other = ((unsigned long)nz * object->height + (unsigned long)ny) * object->width + (unsigned long)nx;
				/* (outside, or filled at an earlier step) */
				if (marks[other] == 0 || (marks[other] >= 2 && marks[other] < step))
				{
					for (k = 0; k < 4; k++)
						sum[k] += filled[other * 4 + k];
					n++;
				}
			}
			if (n)
			{
				for (k = 0; k < 4; k++)
					filled[at * 4 + k] = sum[k] / (float)n;
				marks[at] = step;
				changed = 1;
			}
		}
	}
	free(marks);
	object->texels = malloc(count * 4 * sizeof(unsigned short) + 1);
	if (!object->texels)
	{
		free(filled);
		return 0;
	}
	for (index = 0; index < count * 4; index++)
		object->texels[index] = world_sdf_half(filled[index]);
	/* (the reach: past the edge a point's occlusion is its edge point q's
	by (L / (L + e))^2, which is at most (L / r)^2 at r from the middle, and
	q's at most its edge voxels' largest, t + |v|: under 1/510 past
	sqrt(510 L^2 (t + |v|)), L within a voxel of an edge voxel's own) */
	centre[0] = 0.5f * (float)(object->width - 1);
	centre[1] = 0.5f * (float)(object->height - 1);
	centre[2] = 0.5f * (float)(object->depth - 1);
	for (index = 0; index < count; index++)
	{
		unsigned long x = index % object->width, y = index / object->width % object->height,
			z = index / (object->width * object->height);
		float d[3], length, edge;
		unsigned short const *texel = object->texels + index * 4;
		float t = world_sdf_half_value(texel[0]), vx = world_sdf_half_value(texel[1]),
			vy = world_sdf_half_value(texel[2]), vz = world_sdf_half_value(texel[3]);

		if (x && y && z && x < object->width - 1 && y < object->height - 1 && z < object->depth - 1)
			continue;
		d[0] = (float)x - centre[0];
		d[1] = (float)y - centre[1];
		d[2] = (float)z - centre[2];
		length = sqrtf(sdf_dot(d, d)) + 1.0f;
		farthest = length > farthest ? length : farthest;
		edge = t + sqrtf(vx * vx + vy * vy + vz * vz);
		edge = edge > 1.0f ? 1.0f : edge;
		edge *= 510.0f * length * length;
		most = edge > most ? edge : most;
	}
	object->reach = sqrtf(most) > farthest ? sqrtf(most) : farthest;
	free(filled);
	return 1;
}

float world_sdf_object_occlusion(struct world_sdf_object const *object, float const point[3], float const normal[3])
{
	float g[3], q[3], centre[3], fraction[3], value[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, outside = 0.0f, from = 0.0f, scale;
	unsigned long dimensions[3], base[3];
	int axis, corner, k;

	dimensions[0] = object->width;
	dimensions[1] = object->height;
	dimensions[2] = object->depth;
	for (axis = 0; axis < 3; axis++)
	{
		float last = (float)(dimensions[axis] - 1);

		g[axis] = (point[axis] - object->origin[axis]) / object->voxel;
		q[axis] = g[axis] < 0.0f ? 0.0f : g[axis] > last ? last : g[axis];
		outside += (g[axis] - q[axis]) * (g[axis] - q[axis]);
		centre[axis] = 0.5f * last;
		from += (q[axis] - centre[axis]) * (q[axis] - centre[axis]);
		base[axis] = (unsigned long)floorf(q[axis]);
		if (dimensions[axis] > 1 && base[axis] >= dimensions[axis] - 1)
			base[axis] = dimensions[axis] - 2;
		if (dimensions[axis] == 1)
			base[axis] = 0;
		fraction[axis] = q[axis] - (float)base[axis];
	}
	outside = sqrtf(outside);
	from = sqrtf(from);
	scale = (from + 1.0e-3f) / (from + outside + 1.0e-3f);
	scale *= scale;
	for (corner = 0; corner < 8; corner++)
	{
		unsigned long at[3], index;
		float weight = 1.0f;

		for (axis = 0; axis < 3; axis++)
		{
			int up = (corner >> axis) & 1;

			at[axis] = base[axis] + (dimensions[axis] > 1 ? (unsigned long)up : 0);
			weight *= up ? fraction[axis] : 1.0f - fraction[axis];
		}
		index = (at[2] * object->height + at[1]) * object->width + at[0];
		for (k = 0; k < 4; k++)
			value[k] += weight * world_sdf_half_value(object->texels[index * 4 + k]);
	}
	{
		float occlusion = scale * (value[0] + normal[0] * value[1] + normal[1] * value[2] + normal[2] * value[3]);

		return occlusion < 0.0f ? 0.0f : occlusion > 1.0f ? 1.0f : occlusion;
	}
}

/* the objects put in columns along z at most depth_cap deep, each next
column beside the last, and a row of columns at most width_cap wide, each
next row above the last, in order, until one does not fit the columns'
depth, the atlas's side or bytes (those after it: none; one larger than any
atlas is passed over); how many, and the atlas's size */
static unsigned long sdf_objects_columns(struct world_sdf_objects *objects, unsigned long depth_cap,
	unsigned long width_cap, unsigned long size[3])
{
	unsigned long index, placed = 0, column_x = 0, column_width = 0, row_y = 0, row_height = 0, z = 0;
	unsigned long width = 0, height = 0, depth = 0;

	for (index = 0; index < objects->count; index++)
		objects->objects[index].atlas[0] = ~0UL;
	for (index = 0; index < objects->count; index++)
	{
		struct world_sdf_object *object = &objects->objects[index];
		unsigned long new_width, new_height, new_depth;

		if (object->width > WORLD_SDF_OBJECT_ATLAS_SIDE || object->height > WORLD_SDF_OBJECT_ATLAS_SIDE ||
			object->depth > WORLD_SDF_OBJECT_ATLAS_SIDE)
		{
			continue;
		}
		/* (the first come first: one deeper or wider than the columns ends
		them, so that no smaller one after it takes its place) */
		if (object->depth > depth_cap || object->width > width_cap)
			break;
		/* (stacked along z in columns, the next column beside the last, the
		next row of them above the last) */
		if (z + object->depth > depth_cap)
		{
			column_x += column_width;
			column_width = 0;
			z = 0;
		}
		if (column_x + object->width > width_cap)
		{
			row_y += row_height;
			row_height = 0;
			column_x = 0;
			column_width = 0;
			z = 0;
		}
		if (row_y + object->height > WORLD_SDF_OBJECT_ATLAS_SIDE)
			break;
		new_width = column_x + object->width > width ? column_x + object->width : width;
		new_height = row_y + object->height > height ? row_y + object->height : height;
		new_depth = z + object->depth > depth ? z + object->depth : depth;
		if (new_width * new_height * new_depth * WORLD_SDF_OBJECT_TEXEL > WORLD_SDF_OBJECT_ATLAS_BYTES)
			break;
		object->atlas[0] = column_x;
		object->atlas[1] = row_y;
		object->atlas[2] = z;
		z += object->depth;
		column_width = object->width > column_width ? object->width : column_width;
		row_height = object->height > row_height ? object->height : row_height;
		width = new_width;
		height = new_height;
		depth = new_depth;
		placed++;
	}
	size[0] = width;
	size[1] = height;
	size[2] = depth;
	return placed;
}

unsigned long world_sdf_objects_place(struct world_sdf_objects *objects)
{
	/* (the columns' depth and the rows' width that place the most, in
	order, in the least bytes: columns as deep as the atlas may be are each
	that deep however few the last holds, and one row of them is as high as
	its highest object) */
	unsigned long depth_cap, width_cap, best_depth = WORLD_SDF_OBJECT_ATLAS_SIDE;
	unsigned long best_width = WORLD_SDF_OBJECT_ATLAS_SIDE, best_placed = 0, best_bytes = 0, size[3], placed;

	for (depth_cap = 8; depth_cap <= WORLD_SDF_OBJECT_ATLAS_SIDE; depth_cap += 4)
	{
		for (width_cap = 8; width_cap <= WORLD_SDF_OBJECT_ATLAS_SIDE; width_cap += 8)
		{
			unsigned long bytes;

			placed = sdf_objects_columns(objects, depth_cap, width_cap, size);
			bytes = size[0] * size[1] * size[2];
			if (placed > best_placed || (placed == best_placed && placed && bytes < best_bytes))
			{
				best_placed = placed;
				best_bytes = bytes;
				best_depth = depth_cap;
				best_width = width_cap;
			}
		}
	}
	placed = sdf_objects_columns(objects, best_depth, best_width, size);
	objects->atlas_width = size[0];
	objects->atlas_height = size[1];
	objects->atlas_depth = size[2];
	return placed;
}
