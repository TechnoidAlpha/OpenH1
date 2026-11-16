/*
WORLD_AO_SHADING.C

display.ambient_occlusion "world"'s shading on the CPU (world_ao.h, "the
arithmetic"; docs/world-ao.md, "Shading"): the occlusion a pixel's ambient
light is multiplied by, as the shaders find it (port/linux/src/xgpu.h's
XGPU_AMBIENT_OCCLUSION_WORLD; the emitters' world_ao_*, operation for
operation), and an object's reference, which world_ao.c hands the shaders
with its draws. Pure: the field's sample is world_sdf_sample's
(world_sdf_bake.c), a proxy's occlusion and reach world_proxies.c's.
Tested against both emitters' shaders (tools/test_world_ao_light.py).
Workstream D.
*/

#include "world_ao.h"
#include "world_proxies.h"
#include "world_sdf.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* (world_proxies.c's WORLD_PROXIES_LEAST and pi: the box's bound) */
#define WORLD_AO_BOX_LEAST 510.0f
#define WORLD_AO_PI 3.14159265358979f

static float saturate(float x)
{
	return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
}

static float mix(float a, float b, float t)
{
	return a + (b - a) * t;
}

float world_ao_field_occlusion(struct world_sdf_volume const *volume, float const point[3], float const normal[3])
{
	return world_ao_field_occlusion_from(volume, point, normal, 0.0f);
}

float world_ao_field_occlusion_from(struct world_sdf_volume const *volume, float const point[3], float const normal[3],
	float own)
{
	float first, occlusion = 0.0f;
	int tap, axis;

	if (!volume)
		return 1.0f;
	first = volume->header.voxel > WORLD_AO_STEP ? volume->header.voxel : WORLD_AO_STEP;
	for (tap = 0; tap < WORLD_AO_TAPS; tap++)
	{
		float h = first * (float)(1 << tap), at[3];

		for (axis = 0; axis < 3; axis++)
			at[axis] = point[axis] + normal[axis] * h;
		occlusion += 1.0f / (float)(2 << tap) * saturate((h - (world_sdf_sample(volume, at) - own)) / h);
	}
	return 1.0f - saturate(WORLD_AO_K * occlusion);
}

float world_ao_cards_occlusion(struct world_sdf_volume const *volume, float const point[3], float normal[3],
	float own)
{
	float directions[3][3], best;
	int index, axis, found;

	for (axis = 0; axis < 3; axis++)
	{
		directions[0][axis] = axis == 2 ? 1.0f : 0.0f;
		directions[1][axis] = normal[axis];
		directions[2][axis] = -normal[axis];
	}
	best = world_ao_field_occlusion_from(volume, point, directions[0], own);
	found = 0;
	for (index = 1; index < 3; index++)
	{
		float occlusion = world_ao_field_occlusion_from(volume, point, directions[index], own);

		if (occlusion > best)
		{
			best = occlusion;
			found = index;
		}
	}
	for (axis = 0; axis < 3; axis++)
		normal[axis] = directions[found][axis];
	return best;
}

float world_ao_reference(struct world_sdf_volume const *volume, float const lighting_point[3])
{
	static const float up[3] = { 0.0f, 0.0f, 1.0f };
	float above[3];

	if (!volume)
		return 1.0f;
	above[0] = lighting_point[0];
	above[1] = lighting_point[1];
	above[2] = lighting_point[2] + (volume->header.voxel > WORLD_AO_STEP ? volume->header.voxel : WORLD_AO_STEP);
	return world_ao_field_occlusion(volume, above, up);
}

/* the radius of a sphere about a shape's centre that holds it */
static float world_ao_proxy_bound(struct world_proxy const *proxy)
{
	float axis[3];
	int k;

	for (k = 0; k < 3; k++)
		axis[k] = proxy->v[1][k] - proxy->v[0][k];
	switch ((int)proxy->v[0][3])
	{
	case _world_proxy_capsule:
		return 0.5f * sqrtf(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]) + proxy->v[2][0];
	case _world_proxy_box:
		return sqrtf(proxy->v[1][0] * proxy->v[1][0] + proxy->v[1][1] * proxy->v[1][1] + proxy->v[1][2] * proxy->v[1][2] +
			proxy->v[2][0] * proxy->v[2][0] + proxy->v[2][1] * proxy->v[2][1] + proxy->v[2][2] * proxy->v[2][2] +
			proxy->v[2][3] * proxy->v[2][3]);
	default:
		return proxy->v[2][0];
	}
}

/* ---------- a vehicle's occlusion field (world_proxies.h's _world_proxy_field) */

/* the objects' atlas the reference reads (the GPU's: width x height x
depth texels of four halves, t and v.xyz) */
static struct
{
	unsigned short const *texels;
	unsigned long size[3];
} world_ao_objects;

void world_ao_objects_atlas(unsigned short const *texels, unsigned long width, unsigned long height,
	unsigned long depth)
{
	world_ao_objects.texels = texels;
	world_ao_objects.size[0] = width;
	world_ao_objects.size[1] = height;
	world_ao_objects.size[2] = depth;
}

/* a texel of the atlas, clamped to its edge (a sampler's) */
static float world_ao_objects_texel(long x, long y, long z, int channel)
{
	unsigned long const *size = world_ao_objects.size;

	x = x < 0 ? 0 : x >= (long)size[0] ? (long)size[0] - 1 : x;
	y = y < 0 ? 0 : y >= (long)size[1] ? (long)size[1] - 1 : y;
	z = z < 0 ? 0 : z >= (long)size[2] ? (long)size[2] - 1 : z;
	return world_sdf_half_value(world_ao_objects.texels[(((unsigned long)z * size[1] + (unsigned long)y) * size[0] +
		(unsigned long)x) * 4 + (unsigned long)channel]);
}

float world_ao_field_proxy_fade(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	struct world_proxy const *b = proxy + 1, *c = proxy + 2;
	float const *rows[3];
	float extents[3], local[3], offset[3], outside[3], length, distance_squared = 0.0f, height = 0.0f, faces, distance;
	int axis;

	rows[0] = proxy->v[1];
	rows[1] = b->v[0];
	rows[2] = b->v[1];
	for (axis = 0; axis < 3; axis++)
		extents[axis] = c->v[2][axis];
	for (axis = 0; axis < 3; axis++)
		local[axis] = rows[axis][0] * normal[0] + rows[axis][1] * normal[1] + rows[axis][2] * normal[2];
	length = sqrtf(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
	for (axis = 0; axis < 3; axis++)
	{
		local[axis] = length > 0.0f ? local[axis] / length : 0.0f;
		offset[axis] = rows[axis][0] * point[0] + rows[axis][1] * point[1] + rows[axis][2] * point[2] +
			proxy->v[2][axis] - 0.5f * b->v[2][axis];
		outside[axis] = fabsf(offset[axis]) - extents[axis];
		outside[axis] = outside[axis] > 0.0f ? outside[axis] : 0.0f;
		distance_squared += outside[axis] * outside[axis];
		height += extents[axis] * fabsf(local[axis]) - local[axis] * offset[axis];
	}
	/* (world_proxy_box_fade's, in the field's voxels: the box is its axes') */
	if (distance_squared <= 0.0f)
		return 1.0f;
	if (height <= 0.0f)
		return 0.0f;
	faces = extents[1] * extents[2] * outside[0] + extents[0] * extents[2] * outside[1] +
		extents[0] * extents[1] * outside[2];
	distance = sqrtf(distance_squared);
	return saturate(WORLD_AO_BOX_LEAST * 4.0f / WORLD_AO_PI * faces * (height < distance ? height : distance) /
		(distance_squared * distance_squared) - 1.0f);
}

float world_ao_field_proxy_occlusion(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	struct world_proxy const *b = proxy + 1, *c = proxy + 2;
	float g[3], q[3], local[3], length, outside = 0.0f, from = 0.0f, scale, at[3], value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float const *rows[3];
	long base[3];
	int axis, corner, k;

	if (!world_ao_objects.texels || !world_ao_objects.size[0])
		return 0.0f;
	rows[0] = proxy->v[1];
	rows[1] = b->v[0];
	rows[2] = b->v[1];
	for (axis = 0; axis < 3; axis++)
	{
		float middle = 0.5f * b->v[2][axis];

		g[axis] = rows[axis][0] * point[0] + rows[axis][1] * point[1] + rows[axis][2] * point[2] + proxy->v[2][axis];
		q[axis] = g[axis] < 0.0f ? 0.0f : g[axis] > b->v[2][axis] ? b->v[2][axis] : g[axis];
		outside += (g[axis] - q[axis]) * (g[axis] - q[axis]);
		from += (q[axis] - middle) * (q[axis] - middle);
		local[axis] = rows[axis][0] * normal[0] + rows[axis][1] * normal[1] + rows[axis][2] * normal[2];
		/* (the texel's place: the atlas's coordinate times its size, less a
		half, as the sampler takes it) */
		at[axis] = (q[axis] + c->v[0][axis]) * c->v[1][axis] * (float)world_ao_objects.size[axis] - 0.5f;
		base[axis] = (long)floorf(at[axis]);
	}
	outside = sqrtf(outside);
	from = sqrtf(from);
	scale = (from + 1.0e-3f) / (from + outside + 1.0e-3f);
	scale *= scale;
	length = sqrtf(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
	for (axis = 0; axis < 3; axis++)
		local[axis] = length > 0.0f ? local[axis] / length : 0.0f;
	for (corner = 0; corner < 8; corner++)
	{
		float weight = 1.0f;
		long x = base[0] + (corner & 1), y = base[1] + ((corner >> 1) & 1), z = base[2] + ((corner >> 2) & 1);

		for (axis = 0; axis < 3; axis++)
		{
			float f = at[axis] - (float)base[axis];

			weight *= (corner >> axis) & 1 ? f : 1.0f - f;
		}
		for (k = 0; k < 4; k++)
			value[k] += weight * world_ao_objects_texel(x, y, z, k);
	}
	return saturate(scale * (value[0] + local[0] * value[1] + local[1] * value[2] + local[2] * value[3]));
}

/* one proxy's factor of A_prox at a point (1 - occlusion x fade); FALSE
none (its owner's, past its reach, wholly below the tangent plane, or a box
whose bound is under 1/510: the product as it was). The brute-force loop
and the grid's call it alike, in the same order */
static int world_ao_proxy_factor(struct world_proxy const *proxy, float const point[3], float const normal[3],
	float owner, float *factor)
{
	float centre[3], offset[3], reach, away, fade;
	int axis;

	if (proxy->v[1][3] == owner || (int)proxy->v[0][3] == _world_proxy_none)
		return 0;
	/* (a vehicle's occlusion field: within its reach, faded out over the
	last of it) */
	if ((int)proxy->v[0][3] == _world_proxy_field)
	{
		reach = proxy->v[2][3];
		for (axis = 0; axis < 3; axis++)
			offset[axis] = point[axis] - proxy->v[0][axis];
		away = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
		if (away >= reach)
			return 0;
		/* (and by its geometry's box's bound, which its occlusion is under,
		as a box's) */
		fade = saturate((reach - away) / (reach * WORLD_AO_REACH_FADE)) * world_ao_field_proxy_fade(proxy, point, normal);
		if (fade <= 0.0f)
			return 0;
		*factor = 1.0f - world_ao_field_proxy_occlusion(proxy, point, normal) * fade;
		return 1;
	}
	/* (from its centre: a capsule's segment's middle, a sphere's or a
	box's a) */
	for (axis = 0; axis < 3; axis++)
	{
		centre[axis] = (int)proxy->v[0][3] == _world_proxy_capsule ?
			0.5f * (proxy->v[0][axis] + proxy->v[1][axis]) : proxy->v[0][axis];
		offset[axis] = point[axis] - centre[axis];
	}
	reach = world_proxy_reach(proxy);
	away = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
	if (away >= reach)
		return 0;
	/* (wholly below the point's tangent plane: none) */
	if (normal[0] * -offset[0] + normal[1] * -offset[1] + normal[2] * -offset[2] < -world_ao_proxy_bound(proxy))
		return 0;
	/* (a sphere and a capsule fade out over the last of their reach; a box
	by its bound (world_proxy_box_fade), which is under 1/510 well inside
	its reach on a floor beside it or past its thin sides) */
	if ((int)proxy->v[0][3] == _world_proxy_box)
	{
		fade = world_proxy_box_fade(proxy, point, normal);
		if (fade <= 0.0f)
			return 0;
	}
	else
	{
		fade = saturate((reach - away) / (reach * WORLD_AO_REACH_FADE));
	}
	*factor = 1.0f - world_proxy_occlusion(proxy, point, normal) * fade;
	return 1;
}

float world_ao_proxies_occlusion(struct world_proxy const *proxies, long count, float const point[3],
	float const normal[3], float owner)
{
	float product = 1.0f, factor;
	long index;

	for (index = 0; index < count; index++)
	{
		if (world_ao_proxy_factor(&proxies[index], point, normal, owner, &factor))
			product *= factor;
	}
	return product;
}

/* ---------- the window's proxy grid */

static const int world_ao_grid_sizes[3] = { WORLD_AO_GRID_X, WORLD_AO_GRID_Y, WORLD_AO_GRID_Z };

/* a cell's index along an axis for an offset from the camera, as the
shaders find it: floor(sign(offset) log2(1 + |offset| grid[1].w) grid[1][axis]
+ size / 2), clamped */
static int world_ao_grid_axis(float const grid[2][4], int axis, float offset)
{
	int size = world_ao_grid_sizes[axis];
	float cell = log2f(1.0f + fabsf(offset) * grid[1][3]) * grid[1][axis];
	int index = (int)floorf((offset > 0.0f ? cell : offset < 0.0f ? -cell : 0.0f) + (float)(size / 2));

	return index < 0 ? 0 : index >= size ? size - 1 : index;
}

int world_ao_grid_cell(float const grid[2][4], float const point[3])
{
	int index[3], axis;

	for (axis = 0; axis < 3; axis++)
		index[axis] = world_ao_grid_axis(grid, axis, point[axis] - grid[0][axis]);
	return (index[2] * WORLD_AO_GRID_Y + index[1]) * WORLD_AO_GRID_X + index[0];
}

/* the half extents along the world's axes of the box about a proxy's
centre (world_proxy_reach's) outside which it is never computed: its
reach's; a box's own bounds grown by its reach less its half diagonal
(where its bound falls under 1/510), where that is less */
static void world_ao_grid_extent(struct world_proxy const *proxy, float reach, float half[3])
{
	float ex, ey, ez, grow, z_axis[3];
	int k;

	for (k = 0; k < 3; k++)
		half[k] = reach;
	/* (a vehicle's field: its geometry's box (in its voxels: each row of
	the world to them is an axis over its length squared there) grown as a
	box's, from the grid's middle) */
	if ((int)proxy->v[0][3] == _world_proxy_field)
	{
		struct world_proxy const *b = proxy + 1, *c = proxy + 2;
		float const *rows[3];
		float axes[3][3], centre[3], areas, sizes[3];
		int i;

		rows[0] = proxy->v[1];
		rows[1] = b->v[0];
		rows[2] = b->v[1];
		ex = c->v[2][0];
		ey = c->v[2][1];
		ez = c->v[2][2];
		for (k = 0; k < 3; k++)
			centre[k] = 0.0f;
		for (i = 0; i < 3; i++)
		{
			float squared = rows[i][0] * rows[i][0] + rows[i][1] * rows[i][1] + rows[i][2] * rows[i][2];
			float extent = i == 0 ? ex : i == 1 ? ey : ez;

			if (squared <= 0.0f)
				return;
			for (k = 0; k < 3; k++)
			{
				axes[i][k] = extent * rows[i][k] / squared;
				centre[k] += (0.5f * b->v[2][i] - proxy->v[2][i]) * rows[i][k] / squared;
			}
			sizes[i] = extent / sqrtf(squared);
		}
		areas = sqrtf((sizes[1] * sizes[2]) * (sizes[1] * sizes[2]) + (sizes[0] * sizes[2]) * (sizes[0] * sizes[2]) +
			(sizes[0] * sizes[1]) * (sizes[0] * sizes[1]));
		grow = sqrtf(WORLD_AO_BOX_LEAST * 4.0f / WORLD_AO_PI * areas);
		for (k = 0; k < 3; k++)
		{
			float extent = fabsf(centre[k] - proxy->v[0][k]) + fabsf(axes[0][k]) + fabsf(axes[1][k]) + fabsf(axes[2][k]) +
				grow;

			half[k] = extent < reach ? extent : reach;
		}
		return;
	}
	if ((int)proxy->v[0][3] != _world_proxy_box)
		return;
	ex = sqrtf(proxy->v[1][0] * proxy->v[1][0] + proxy->v[1][1] * proxy->v[1][1] + proxy->v[1][2] * proxy->v[1][2]);
	ey = sqrtf(proxy->v[2][0] * proxy->v[2][0] + proxy->v[2][1] * proxy->v[2][1] + proxy->v[2][2] * proxy->v[2][2]);
	ez = proxy->v[2][3];
	if (ex <= 0.0f || ey <= 0.0f || ez <= 0.0f)
		return;
	grow = reach - sqrtf(ex * ex + ey * ey + ez * ez);
	z_axis[0] = (proxy->v[1][1] * proxy->v[2][2] - proxy->v[1][2] * proxy->v[2][1]) / (ex * ey);
	z_axis[1] = (proxy->v[1][2] * proxy->v[2][0] - proxy->v[1][0] * proxy->v[2][2]) / (ex * ey);
	z_axis[2] = (proxy->v[1][0] * proxy->v[2][1] - proxy->v[1][1] * proxy->v[2][0]) / (ex * ey);
	for (k = 0; k < 3; k++)
	{
		float extent = fabsf(proxy->v[1][k]) + fabsf(proxy->v[2][k]) + ez * fabsf(z_axis[k]) + grow;

		half[k] = extent < reach ? extent : reach;
	}
}

void world_ao_grid(struct world_proxy const *proxies, long count, float const camera[3], float grid[2][4],
	unsigned int cells[WORLD_AO_GRID_CELLS / 2][4])
{
	long index;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		grid[0][axis] = camera[axis];
		grid[1][axis] = (float)(world_ao_grid_sizes[axis] / 2) / log2f(1.0f + WORLD_AO_GRID_REACH / WORLD_AO_GRID_CELL);
	}
	grid[0][3] = 0.0f;
	grid[1][3] = 1.0f / WORLD_AO_GRID_CELL;
	memset(cells, 0, sizeof(unsigned int) * 4 * (WORLD_AO_GRID_CELLS / 2));
	if (count > 64)
		count = 64;
	for (index = 0; index < count; index++)
	{
		struct world_proxy const *proxy = &proxies[index];
		float centre[3], half[3], reach = world_proxy_reach(proxy);
		int first[3], last[3], x, y, z, k;

		/* (a vehicle's field's slots after its first: in no cell) */
		if ((int)proxy->v[0][3] == _world_proxy_none)
			continue;
		world_ao_grid_extent(proxy, reach, half);
		for (k = 0; k < 3; k++)
		{
			/* (a little past it, so that a pixel at its edge that the GPU's
			log2 rounds into the next cell finds it there too) */
			float pad;

			centre[k] = (int)proxy->v[0][3] == _world_proxy_capsule ?
				0.5f * (proxy->v[0][k] + proxy->v[1][k]) : proxy->v[0][k];
			pad = 1.0e-3f * (half[k] + fabsf(centre[k] - camera[k])) + 1.0e-3f;
			first[k] = world_ao_grid_axis((float const (*)[4])grid, k, centre[k] - half[k] - pad - camera[k]);
			last[k] = world_ao_grid_axis((float const (*)[4])grid, k, centre[k] + half[k] + pad - camera[k]);
		}
		for (z = first[2]; z <= last[2]; z++)
		{
			for (y = first[1]; y <= last[1]; y++)
			{
				for (x = first[0]; x <= last[0]; x++)
				{
					int cell = (z * WORLD_AO_GRID_Y + y) * WORLD_AO_GRID_X + x;

					cells[cell >> 1][((cell & 1) << 1) + (int)(index >> 5)] |= 1u << (index & 31);
				}
			}
		}
	}
}

float world_ao_proxies_occlusion_grid(struct world_proxy const *proxies, float const grid[2][4],
	unsigned int const cells[WORLD_AO_GRID_CELLS / 2][4], float const point[3], float const normal[3], float owner)
{
	int cell = world_ao_grid_cell(grid, point), word, bit;
	float product = 1.0f, factor;

	/* (lowest first, as the shaders' findLSB / ctz) */
	for (word = 0; word < 2; word++)
	{
		unsigned int bits = cells[cell >> 1][((cell & 1) << 1) + word];

		for (bit = 0; bit < 32; bit++)
		{
			if ((bits >> bit) & 1u && world_ao_proxy_factor(&proxies[word * 32 + bit], point, normal, owner, &factor))
				product *= factor;
		}
	}
	return product;
}

long world_ao_proxies_walked(float const parameters[2][4], long count, float reference, int lightmap)
{
	long dynamic = (long)parameters[1][1];

	/* (the lightmap pass where its page has no layer: the static scenery's
	shapes too, as with no pages at all) */
	if (lightmap && reference < 0.0f)
		return count;
	return dynamic < count ? dynamic : count;
}

float world_ao_occlusion(float const parameters[2][4], struct world_sdf_volume const *volume,
	struct world_proxy const *proxies, long count, float const point[3], float const normal[3], float owner,
	float reference, int lightmap, float scenery, int cards)
{
	float proxied, field, own = 0.0f, found[3];

	memcpy(found, normal, sizeof(found));
	/* (a static scenery placement's own draw, its reference negative: the
	field holds its own geometry, so taken from the pixel's own depth in it,
	where its surface is inside its collision (a canopy within its hull), and
	relative to none) */
	if (!lightmap && reference < 0.0f)
	{
		float inside = volume ? world_sdf_sample(volume, point) : 0.0f;

		own = inside < 0.0f ? inside : 0.0f;
	}
	/* (a two-sided shader's cards: about their most open direction, the
	shapes too) */
	field = lightmap ? 1.0f : cards ? world_ao_cards_occlusion(volume, point, found, own) :
		world_ao_field_occlusion_from(volume, point, normal, own);
	proxied = world_ao_proxies_occlusion(proxies, world_ao_proxies_walked(parameters, count, reference, lightmap),
		point, found, owner);
	if (lightmap)
	{
		/* (the static scenery's, from its page) */
		if (reference >= 0.0f)
			proxied *= scenery;
		return mix(1.0f, proxied > WORLD_AO_FLOOR ? proxied : WORLD_AO_FLOOR, parameters[0][1]);
	}
	if (reference < 0.0f)
		reference = 1.0f;
	field = mix(field, saturate(field / (reference > WORLD_AO_REFERENCE_LEAST ? reference : WORLD_AO_REFERENCE_LEAST)),
		parameters[1][0]);
	field = mix(1.0f, field, parameters[0][2]) * proxied;
	return mix(1.0f, field > WORLD_AO_FLOOR ? field : WORLD_AO_FLOOR, parameters[0][0]);
}

/* ---------- found once a pixel (xgpu_stream.h's WORLD_AO_PREPASS)

The window's surfaces' inputs drawn first (the pre-pass), then the
occlusion found once at each pixel (or each 2x2 block) from them, as the
backends' pass finds it (the emitters' world_ao_pass_*), and the surfaces'
upsampling of it. */

void world_ao_pass_position(float const camera[4][4], long const corners[4], float x, float y, float distance,
	float const eye[3], float point[3])
{
	float sx = 2.0f * (x - (float)corners[0]) / (float)(corners[2] - corners[0]) - 1.0f;
	float sy = 2.0f * (y - (float)corners[1]) / (float)(corners[3] - corners[1]) - 1.0f;
	float ray[3], length;
	int axis;

	for (axis = 0; axis < 3; axis++)
		ray[axis] = camera[1][axis] + sx * camera[2][axis] + sy * camera[3][axis];
	length = sqrtf(ray[0] * ray[0] + ray[1] * ray[1] + ray[2] * ray[2]);
	for (axis = 0; axis < 3; axis++)
		point[axis] = eye[axis] + distance * ray[axis] / length;
}

/* a float as a half's bits, rounded to the nearest (even), and back */
static unsigned int world_ao_half(float value)
{
	unsigned int bits, sign, exponent, mantissa, half;

	memcpy(&bits, &value, sizeof(bits));
	sign = (bits >> 16) & 0x8000u;
	exponent = (bits >> 23) & 0xffu;
	mantissa = bits & 0x7fffffu;
	if (exponent == 0xffu)
		return sign | 0x7c00u | (mantissa ? 0x200u : 0);
	if (exponent > 142)
		return sign | 0x7c00u;
	if (exponent < 113)
	{
		/* (below the halves' least normal: as a subnormal) */
		unsigned int shift;

		if (exponent < 102)
			return sign;
		mantissa |= 0x800000u;
		shift = 126 - exponent;
		half = mantissa >> shift;
		if ((mantissa >> (shift - 1)) & 1u && (half & 1u || mantissa & ((1u << (shift - 1)) - 1u)))
			half++;
		return sign | half;
	}
	half = ((exponent - 112) << 10) | (mantissa >> 13);
	if (mantissa & 0x1000u && (half & 1u || mantissa & 0xfffu))
		half++;
	return sign | half;
}

static float world_ao_half_float(unsigned int half)
{
	unsigned int sign = (half & 0x8000u) << 16, exponent = (half >> 10) & 0x1fu, mantissa = half & 0x3ffu, bits;
	float value;

	if (!exponent)
	{
		value = ldexpf((float)mantissa, -24);
		return sign ? -value : value;
	}
	bits = sign | (exponent == 31 ? 0x7f800000u | (mantissa << 13) : ((exponent + 112) << 23) | (mantissa << 13));
	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* a signed 16-bit half of a word, as the shaders' packSnorm2x16 */
static unsigned int world_ao_snorm16(float value)
{
	float clamped = value < -1.0f ? -1.0f : value > 1.0f ? 1.0f : value;

	return (unsigned int)(int)roundf(clamped * 32767.0f) & 0xffffu;
}

static float world_ao_snorm16_float(unsigned int bits)
{
	float value = (float)(short)(unsigned short)bits / 32767.0f;

	return value < -1.0f ? -1.0f : value;
}

void world_ao_inputs_pack(float const normal[3], float owner, float value, int lightmap, int all_shapes, int cards,
	float distance, unsigned int words[4])
{
	float length = fabsf(normal[0]) + fabsf(normal[1]) + fabsf(normal[2]), x, y;

	/* (octahedral: the lower half folded over its diagonals) */
	if (length <= 0.0f)
		length = 1.0f;
	x = normal[0] / length;
	y = normal[1] / length;
	if (normal[2] < 0.0f)
	{
		float fx = (1.0f - fabsf(y)) * (x >= 0.0f ? 1.0f : -1.0f), fy = (1.0f - fabsf(x)) * (y >= 0.0f ? 1.0f : -1.0f);

		x = fx;
		y = fy;
	}
	words[0] = world_ao_snorm16(x) | world_ao_snorm16(y) << 16;
	words[1] = ((unsigned int)(int)(owner + 2.0f) & 0xffu) | XGPU_WORLD_AO_INPUT_PRESENT_BIT |
		(lightmap ? XGPU_WORLD_AO_INPUT_LIGHTMAP_BIT : 0) | (all_shapes ? XGPU_WORLD_AO_INPUT_ALL_SHAPES_BIT : 0) |
		(cards ? XGPU_WORLD_AO_INPUT_CARDS_BIT : 0) | world_ao_half(value) << 16;
	memcpy(&words[2], &distance, sizeof(distance));
	words[3] = 0;
}

int world_ao_inputs_unpack(unsigned int const words[4], float normal[3], float *owner, float *value, int *lightmap,
	int *all_shapes, int *cards, float *distance)
{
	float x = world_ao_snorm16_float(words[0] & 0xffffu), y = world_ao_snorm16_float(words[0] >> 16);
	float z = 1.0f - fabsf(x) - fabsf(y), t = z < 0.0f ? -z : 0.0f, length;

	if (!(words[1] & XGPU_WORLD_AO_INPUT_PRESENT_BIT))
		return 0;
	x += x >= 0.0f ? -t : t;
	y += y >= 0.0f ? -t : t;
	length = sqrtf(x * x + y * y + z * z);
	normal[0] = x / length;
	normal[1] = y / length;
	normal[2] = z / length;
	*owner = (float)(int)(words[1] & 0xffu) - 2.0f;
	*value = world_ao_half_float(words[1] >> 16);
	*lightmap = (words[1] & XGPU_WORLD_AO_INPUT_LIGHTMAP_BIT) != 0;
	*all_shapes = (words[1] & XGPU_WORLD_AO_INPUT_ALL_SHAPES_BIT) != 0;
	*cards = (words[1] & XGPU_WORLD_AO_INPUT_CARDS_BIT) != 0;
	memcpy(distance, &words[2], sizeof(*distance));
	return 1;
}

float world_ao_pass_occlusion(float const parameters[2][4], struct world_sdf_volume const *volume,
	struct world_proxy const *proxies, long count, float const grid[2][4],
	unsigned int const cells[WORLD_AO_GRID_CELLS / 2][4], float const point[3], unsigned int const words[4])
{
	float normal[3], owner, value, proxied = 1.0f, factor, field = 1.0f, reference, distance, own = 0.0f;
	int lightmap, all_shapes, cards, cell, word, bit;
	long limit;

	if (!world_ao_inputs_unpack(words, normal, &owner, &value, &lightmap, &all_shapes, &cards, &distance))
		return 1.0f;
	/* (a model's field: from the pixel's own depth in it where its
	reference is negative, a static scenery placement's own draw; a
	two-sided shader's cards about their most open direction, the shapes
	too) */
	if (!lightmap)
	{
		if (value < 0.0f)
		{
			float inside = volume ? world_sdf_sample(volume, point) : 0.0f;

			own = inside < 0.0f ? inside : 0.0f;
		}
		field = cards ? world_ao_cards_occlusion(volume, point, normal, own) :
			world_ao_field_occlusion_from(volume, point, normal, own);
	}
	/* (the shapes walked: the dynamic objects' alone, or every one) */
	limit = all_shapes ? count : (long)parameters[1][1] < count ? (long)parameters[1][1] : count;
	cell = world_ao_grid_cell(grid, point);
	for (word = 0; word < 2; word++)
	{
		unsigned int bits = cells[cell >> 1][((cell & 1) << 1) + word];

		for (bit = 0; bit < 32; bit++)
		{
			if (word * 32 + bit < limit && (bits >> bit) & 1u &&
				world_ao_proxy_factor(&proxies[word * 32 + bit], point, normal, owner, &factor))
				proxied *= factor;
		}
	}
	/* (the static term: the lightmap pass's page's texel, a model's field
	relative to its reference; then the dynamic shapes' product) */
	if (lightmap)
	{
		proxied *= value;
		return mix(1.0f, proxied > WORLD_AO_FLOOR ? proxied : WORLD_AO_FLOOR, parameters[0][1]);
	}
	reference = value < 0.0f ? 1.0f : value;
	field = mix(field, saturate(field / (reference > WORLD_AO_REFERENCE_LEAST ? reference : WORLD_AO_REFERENCE_LEAST)),
		parameters[1][0]);
	field = mix(1.0f, field, parameters[0][2]) * proxied;
	return mix(1.0f, field > WORLD_AO_FLOOR ? field : WORLD_AO_FLOOR, parameters[0][0]);
}

/* a pixel's occlusion read from the pass's texels (struct
world_ao_pass_texel: found at a quarter of the pixels, each at its 2x2
block's nearest pixel): the four about it, each by its bilinear weight and
its closeness (world_ao_upsample_closeness); where none is close, the
closest of the 4x4 about it. At scale 1 the pixel's own */
static void world_ao_unfold(float x, float y, float normal[3])
{
	float z = 1.0f - fabsf(x) - fabsf(y), t = z < 0.0f ? -z : 0.0f, length;

	x += x >= 0.0f ? -t : t;
	y += y >= 0.0f ? -t : t;
	length = sqrtf(x * x + y * y + z * z);
	normal[0] = x / length;
	normal[1] = y / length;
	normal[2] = z / length;
}

/* a texel's closeness to the pixel: its distance's to the pixel's
(relative, by how squarely the pixel's surface faces the eye, so that a
surface seen edge on, whose distance changes fast across it, keeps its
texels), and its normal's (the cosine's eighth power) */
static float world_ao_upsample_closeness(float distance, float facing, float const normal[3],
	struct world_ao_pass_texel const *texel)
{
	float texel_normal[3], relative, along;

	if (texel->distance <= 0.0f || distance <= 0.0f)
		return 0.0f;
	world_ao_unfold(texel->normal[0], texel->normal[1], texel_normal);
	relative = fabsf(texel->distance / distance - 1.0f) * (facing > WORLD_AO_UPSAMPLE_FACING ? facing :
		WORLD_AO_UPSAMPLE_FACING) * (1.0f / WORLD_AO_UPSAMPLE_DEPTH);
	along = normal[0] * texel_normal[0] + normal[1] * texel_normal[1] + normal[2] * texel_normal[2];
	along = along > 0.0f ? along : 0.0f;
	along *= along;
	along *= along;
	along *= along;
	return along / (1.0f + relative * relative);
}

float world_ao_upsample(struct world_ao_pass_texel const *texels, long width, long height, float scale, float x,
	float y, float distance, float facing, float const normal[3])
{
	float at_x, at_y, fx, fy, sum = 0.0f, weights = 0.0f, best = -1.0f, nearest = 1.0f;
	long base_x, base_y, i, j;

	if (scale <= 1.0f)
	{
		long px = (long)x, py = (long)y;

		px = px < 0 ? 0 : px >= width ? width - 1 : px;
		py = py < 0 ? 0 : py >= height ? height - 1 : py;
		return texels[py * width + px].occlusion;
	}
	at_x = x / scale - 0.5f;
	at_y = y / scale - 0.5f;
	base_x = (long)floorf(at_x);
	base_y = (long)floorf(at_y);
	fx = at_x - (float)base_x;
	fy = at_y - (float)base_y;
	for (j = 0; j < 2; j++)
	{
		for (i = 0; i < 2; i++)
		{
			long tx = base_x + i, ty = base_y + j;
			float closeness;

			tx = tx < 0 ? 0 : tx >= width ? width - 1 : tx;
			ty = ty < 0 ? 0 : ty >= height ? height - 1 : ty;
			closeness = world_ao_upsample_closeness(distance, facing, normal, &texels[ty * width + tx]);
			sum += (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy) * closeness * texels[ty * width + tx].occlusion;
			weights += (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy) * closeness;
		}
	}
	if (weights > WORLD_AO_UPSAMPLE_LEAST)
		return sum / weights;
	/* (none close: the closest of the 4x4 about it, a thin surface's own
	texel a block or two away) */
	for (j = -1; j < 3; j++)
	{
		for (i = -1; i < 3; i++)
		{
			long tx = base_x + i, ty = base_y + j;
			float closeness;

			tx = tx < 0 ? 0 : tx >= width ? width - 1 : tx;
			ty = ty < 0 ? 0 : ty >= height ? height - 1 : ty;
			closeness = world_ao_upsample_closeness(distance, facing, normal, &texels[ty * width + tx]);
			if (closeness > best)
			{
				best = closeness;
				nearest = texels[ty * width + tx].occlusion;
			}
		}
	}
	return nearest;
}

/* ---------- the static scenery's pages (world_sdf.h's struct world_sdf_scenery) */

/* (an owner no shape has: every shape shades the level) */
#define WORLD_AO_SCENERY_OWNER -3.0f
/* (two triangles at a texel farther apart than this, world units: the
page means two places there) */
#define WORLD_AO_SCENERY_APART 0.25f

long world_ao_scenery_page(struct world_proxy const *shapes, long shape_count, float const *triangles,
	long triangle_count, unsigned long width, unsigned long height, unsigned char *texels, long *shared)
{
	unsigned long count = width * height, texel;
	/* (each texel's place once covered, and whether it is: 0 none, 1 a
	triangle's, 2 its neighbours' after) */
	float *places = malloc(count * 3 * sizeof(float));
	unsigned char *covered = calloc(count, 1);
	long triangle, total = 0;
	int pass;

	*shared = 0;
	memset(texels, 255, count);
	if (!places || !covered)
	{
		free(places);
		free(covered);
		return 0;
	}
	for (triangle = 0; triangle < triangle_count; triangle++)
	{
		float const *t = triangles + triangle * WORLD_AO_SCENERY_TRIANGLE;
		float u[3], v[3], area, low[2], high[2];
		long x0, x1, y0, y1, x, y;
		int corner;

		for (corner = 0; corner < 3; corner++)
		{
			u[corner] = t[18 + corner * 2] * (float)width;
			v[corner] = t[19 + corner * 2] * (float)height;
		}
		area = (u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0]);
		if (fabsf(area) < 1.0e-12f)
			continue;
		low[0] = fminf(u[0], fminf(u[1], u[2]));
		low[1] = fminf(v[0], fminf(v[1], v[2]));
		high[0] = fmaxf(u[0], fmaxf(u[1], u[2]));
		high[1] = fmaxf(v[0], fmaxf(v[1], v[2]));
		/* (the texels whose centres, x + 0.5, are within) */
		x0 = (long)floorf(low[0] - 0.5f) + 1;
		y0 = (long)floorf(low[1] - 0.5f) + 1;
		x1 = (long)floorf(high[0] - 0.5f);
		y1 = (long)floorf(high[1] - 0.5f);
		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 0 ? 0 : y0;
		x1 = x1 >= (long)width ? (long)width - 1 : x1;
		y1 = y1 >= (long)height ? (long)height - 1 : y1;
		for (y = y0; y <= y1; y++)
		{
			for (x = x0; x <= x1; x++)
			{
				float cu = (float)x + 0.5f, cv = (float)y + 0.5f, b[3], place[3], normal[3], length, value;
				int axis;

				b[1] = ((cu - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (cv - v[0])) / area;
				b[2] = ((u[1] - u[0]) * (cv - v[0]) - (cu - u[0]) * (v[1] - v[0])) / area;
				b[0] = 1.0f - b[1] - b[2];
				if (b[0] < 0.0f || b[1] < 0.0f || b[2] < 0.0f)
					continue;
				for (axis = 0; axis < 3; axis++)
				{
					place[axis] = b[0] * t[axis] + b[1] * t[3 + axis] + b[2] * t[6 + axis];
					normal[axis] = b[0] * t[9 + axis] + b[1] * t[12 + axis] + b[2] * t[15 + axis];
				}
				length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
				if (length < 1.0e-12f)
					continue;
				for (axis = 0; axis < 3; axis++)
					normal[axis] /= length;
				texel = (unsigned long)y * width + (unsigned long)x;
				if (covered[texel])
				{
					float apart[3];

					for (axis = 0; axis < 3; axis++)
						apart[axis] = place[axis] - places[texel * 3 + axis];
					if (apart[0] * apart[0] + apart[1] * apart[1] + apart[2] * apart[2] >
						WORLD_AO_SCENERY_APART * WORLD_AO_SCENERY_APART)
					{
						(*shared)++;
					}
					continue;
				}
				value = world_ao_proxies_occlusion(shapes, shape_count, place, normal, WORLD_AO_SCENERY_OWNER);
				texels[texel] = (unsigned char)(saturate(value) * 255.0f + 0.5f);
				memcpy(places + texel * 3, place, sizeof(place));
				covered[texel] = 1;
				total++;
			}
		}
	}
	/* (the texels about the surfaces, which a filter at their edges reads:
	their covered neighbours' mean, two steps out) */
	for (pass = 0; pass < 2; pass++)
	{
		unsigned char mark = (unsigned char)(pass + 2);

		for (texel = 0; texel < count; texel++)
		{
			long x = (long)(texel % width), y = (long)(texel / width), dx, dy, sum = 0, n = 0;

			if (covered[texel])
				continue;
			for (dy = -1; dy <= 1; dy++)
			{
				for (dx = -1; dx <= 1; dx++)
				{
					long nx = x + dx, ny = y + dy;
					unsigned long near;

					if (nx < 0 || ny < 0 || nx >= (long)width || ny >= (long)height)
						continue;
					near = (unsigned long)ny * width + (unsigned long)nx;
					if (covered[near] && covered[near] < mark)
					{
						sum += texels[near];
						n++;
					}
				}
			}
			if (n)
			{
				texels[texel] = (unsigned char)((sum + n / 2) / n);
				covered[texel] = mark;
			}
		}
	}
	free(places);
	free(covered);
	return total;
}
