/*
SCENE.C

display.ambient_occlusion "world"'s made-up scene for the shading's checks
(tools/test_world_ao_light.py): a field of a floor and a wall, a sphere, a
capsule, a box and a second sphere of the first's owner, and points about
them with their normals, owners and references, the same every run. Built
with the shading's CPU reference (port/linux/game/world_ao_shading.c) and
the parts it reads (world_proxies.c's arithmetic, world_sdf_bake.c's
sample), whose game side is stood in for here.

  reference <count>   the scene and each point's occlusion, as JSON
*/

#include "world_ao.h"
#include "world_proxies.h"
#include "world_proxies_source.h"
#include "world_sdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (world_proxies.c's game side: none) */
int world_proxies_source_definition(long definition, struct world_proxies_source *source)
{
	(void)definition;
	(void)source;
	return 0;
}

long world_proxies_source_instances(struct world_proxies_source_instance *instances, long maximum)
{
	(void)instances;
	(void)maximum;
	return 0;
}

short world_proxies_source_nodes(long id, struct world_proxies_frame *frames, short maximum)
{
	(void)id;
	(void)frames;
	(void)maximum;
	return 0;
}

int world_proxies_source_field(long definition, struct world_proxies_source_field *field)
{
	(void)definition;
	(void)field;
	return 0;
}

int world_proxies_source_frustum(float planes[4][4])
{
	(void)planes;
	return 0;
}

int world_proxies_source_view(void)
{
	return 0;
}

void world_proxies_source_line(float const a[3], float const b[3], unsigned long color)
{
	(void)a;
	(void)b;
	(void)color;
}

enum
{
	/* (the field's cells; its whole grid of voxels, the cells' steps and one
	more, made sparse: world_sdf_pack) */
	SCENE_WIDTH = 5,
	SCENE_HEIGHT = 5,
	SCENE_DEPTH = 3,
	SCENE_ROW = WORLD_SDF_BRICK * SCENE_WIDTH + 1,
	SCENE_ROWS = WORLD_SDF_BRICK * SCENE_HEIGHT + 1,
	SCENE_SLICES = WORLD_SDF_BRICK * SCENE_DEPTH + 1,
	SCENE_PROXIES = 8,
	SCENE_DYNAMIC = 6,
	/* (a vehicle's occlusion field's place in the objects' atlas: along z
	past two slices of none) */
	SCENE_FIELD_PLACE = 2,
};

/* the static scenery's pages: each layer one value all through (so that a
lightmap draw's page is the same at any of its coordinates) */
float const scene_pages[2] = { 153.0f / 255.0f, 77.0f / 255.0f };

struct world_sdf_volume scene_volume;
static signed char scene_voxels[SCENE_ROW * SCENE_ROWS * SCENE_SLICES];
struct world_proxy scene_proxies[SCENE_PROXIES];
long scene_proxy_count = SCENE_PROXIES;
float scene_parameters[2][4];
/* the proxies' grid about a camera among them (world_ao_grid) */
float scene_grid[2][4];
unsigned int scene_cells[WORLD_AO_GRID_CELLS / 2][4];
/* a vehicle's occlusion field (a box's, baked), and the objects' atlas it
is in: its halves (the reference's) and their values (the GPU's) */
struct world_sdf_object scene_object;
unsigned short *scene_atlas;
float *scene_atlas_values;
unsigned long scene_atlas_size[3];

static unsigned long scene_seed = 12345;

/* a number 0 to 1 */
float scene_random(void)
{
	scene_seed = scene_seed * 1103515245UL + 12345UL;
	return (float)((scene_seed >> 8) & 0xffffff) / 16777216.0f;
}

/* the box the field is baked of, in its own space */
static float const scene_object_low[3] = { -0.6f, -0.35f, 0.0f }, scene_object_high[3] = { 0.6f, 0.35f, 0.5f };

static int scene_object_inside(void *context, float const point[3])
{
	int axis;

	(void)context;
	for (axis = 0; axis < 3; axis++)
	{
		if (point[axis] <= scene_object_low[axis] || point[axis] >= scene_object_high[axis])
			return 0;
	}
	return 1;
}

/* the field baked (the box's twelve triangles), in an atlas past two
slices of none, and placed on a frame turned 0.5 about z, scaled 1.2, at
-0.5, -0.4, 0.1: three shapes from first, as world_proxies.c's
world_proxies_field_place puts them */
static void scene_field(struct world_proxy *first)
{
	static const int faces[6][4] = { { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 },
		{ 1, 5, 7, 3 } };
	float corners[8][3], triangles[12 * 9], bounds[6], *values, axes[3][3], position[3] = { -0.5f, -0.4f, 0.1f };
	float scale = 1.2f, angle = 0.5f, over, middle[3], extents[3], areas, box_reach;
	unsigned char *solid;
	unsigned long z, slice, index;
	struct world_sdf_object_tables *tables;
	int corner, face, axis, k;

	for (corner = 0; corner < 8; corner++)
		for (axis = 0; axis < 3; axis++)
			corners[corner][axis] = (corner >> axis) & 1 ? scene_object_high[axis] : scene_object_low[axis];
	for (face = 0; face < 6; face++)
	{
		static const int halves[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
		int half;

		for (half = 0; half < 2; half++)
			for (corner = 0; corner < 3; corner++)
				memcpy(triangles + ((face * 2 + half) * 3 + corner) * 3, corners[faces[face][halves[half][corner]]],
					sizeof(float) * 3);
	}
	memcpy(bounds, scene_object_low, sizeof(scene_object_low));
	memcpy(bounds + 3, scene_object_high, sizeof(scene_object_high));
	world_sdf_object_layout(bounds, WORLD_SDF_OBJECT_MOST_VOXELS, &scene_object);
	slice = scene_object.width * scene_object.height;
	values = malloc(slice * scene_object.depth * 4 * sizeof(float));
	solid = malloc(slice * scene_object.depth);
	tables = world_sdf_object_tables_make();
	if (!tables)
		abort();
	for (z = 0; z < scene_object.depth; z++)
		world_sdf_object_slice(tables, &scene_object, triangles, NULL, 12, scene_object_inside, NULL, z, 0,
			scene_object.height, values + z * slice * 4, solid + z * slice);
	world_sdf_object_tables_free(tables);
	if (!world_sdf_object_finish(&scene_object, values, solid))
		abort();
	free(values);
	free(solid);
	scene_atlas_size[0] = scene_object.width;
	scene_atlas_size[1] = scene_object.height;
	scene_atlas_size[2] = scene_object.depth + SCENE_FIELD_PLACE;
	scene_atlas = calloc(slice * scene_atlas_size[2] * 4, sizeof(*scene_atlas));
	scene_atlas_values = calloc(slice * scene_atlas_size[2] * 4, sizeof(*scene_atlas_values));
	memcpy(scene_atlas + slice * SCENE_FIELD_PLACE * 4, scene_object.texels, slice * scene_object.depth * 4 * sizeof(short));
	for (index = 0; index < slice * scene_atlas_size[2] * 4; index++)
		scene_atlas_values[index] = world_sdf_half_value(scene_atlas[index]);
	world_ao_objects_atlas(scene_atlas, scene_atlas_size[0], scene_atlas_size[1], scene_atlas_size[2]);
	/* (its frame: turned, scaled) */
	memset(axes, 0, sizeof(axes));
	axes[0][0] = scale * cosf(angle);
	axes[0][1] = scale * sinf(angle);
	axes[1][0] = -scale * sinf(angle);
	axes[1][1] = scale * cosf(angle);
	axes[2][2] = scale;
	memset(first, 0, sizeof(*first) * 3);
	over = 1.0f / (scale * scale * scene_object.voxel);
	for (axis = 0; axis < 3; axis++)
	{
		float *row = axis == 0 ? first[0].v[1] : axis == 1 ? first[1].v[0] : first[1].v[1];
		unsigned long count = axis == 0 ? scene_object.width : axis == 1 ? scene_object.height : scene_object.depth;

		for (k = 0; k < 3; k++)
			row[k] = axes[axis][k] * over;
		first[0].v[2][axis] = -(axes[axis][0] * position[0] + axes[axis][1] * position[1] + axes[axis][2] * position[2]) *
			over - scene_object.origin[axis] / scene_object.voxel;
		middle[axis] = scene_object.origin[axis] + 0.5f * scene_object.voxel * (float)(count - 1);
		first[1].v[2][axis] = (float)(count - 1);
		first[2].v[0][axis] = (float)(axis == 2 ? SCENE_FIELD_PLACE : 0) + 0.5f;
		first[2].v[1][axis] = 1.0f / (float)scene_atlas_size[axis];
		extents[axis] = 0.5f * (scene_object.bounds[3 + axis] - scene_object.bounds[axis]) / scene_object.voxel;
		first[2].v[2][axis] = extents[axis];
	}
	for (axis = 0; axis < 3; axis++)
		first[0].v[0][axis] = position[axis] + middle[0] * axes[0][axis] + middle[1] * axes[1][axis] + middle[2] * axes[2][axis];
	first[0].v[0][3] = (float)_world_proxy_field;
	for (axis = 0; axis < 3; axis++)
		extents[axis] *= scene_object.voxel * scale;
	areas = sqrtf((extents[1] * extents[2]) * (extents[1] * extents[2]) + (extents[0] * extents[2]) *
		(extents[0] * extents[2]) + (extents[0] * extents[1]) * (extents[0] * extents[1]));
	box_reach = sqrtf(extents[0] * extents[0] + extents[1] * extents[1] + extents[2] * extents[2]) +
		sqrtf(510.0f * 4.0f / 3.14159265358979f * areas);
	first[0].v[2][3] = scene_object.reach * scene_object.voxel * scale;
	first[0].v[2][3] = box_reach < first[0].v[2][3] ? box_reach : first[0].v[2][3];
	/* (its owner, 4, in each of its shapes) */
	for (k = 0; k < 3; k++)
		first[k].v[1][3] = 4.0f;
}

void scene_make(void)
{
	struct world_sdf_header *header = &scene_volume.header;
	unsigned long x, y, z;
	float axis_x[3] = { 0.8f, 0.3f, 0.0f }, length;

	memset(&scene_volume, 0, sizeof(scene_volume));
	header->width = SCENE_WIDTH;
	header->height = SCENE_HEIGHT;
	header->depth = SCENE_DEPTH;
	header->voxel = 0.25f;
	header->range = WORLD_SDF_RANGE_VOXELS * header->voxel;
	header->origin[0] = -5.0f;
	header->origin[1] = -5.0f;
	header->origin[2] = -1.0f;
	/* a floor at z 0 and a wall at x 3: solid below and past them */
	for (z = 0; z < SCENE_SLICES; z++)
	{
		for (y = 0; y < SCENE_ROWS; y++)
		{
			for (x = 0; x < SCENE_ROW; x++)
			{
				float px = header->origin[0] + (float)x * header->voxel;
				float pz = header->origin[2] + (float)z * header->voxel;
				float floor_distance = pz, wall_distance = 3.0f - px;

				scene_voxels[(z * SCENE_ROWS + y) * SCENE_ROW + x] = world_sdf_encode(
					floor_distance < wall_distance ? floor_distance : wall_distance, header->range);
			}
		}
	}
	if (!world_sdf_pack(&scene_volume, scene_voxels))
		abort();
	memset(scene_proxies, 0, sizeof(scene_proxies));
	/* a sphere, owner 0 */
	scene_proxies[0].v[0][0] = 1.0f;
	scene_proxies[0].v[0][2] = 0.8f;
	scene_proxies[0].v[0][3] = _world_proxy_sphere;
	scene_proxies[0].v[1][3] = 0.0f;
	scene_proxies[0].v[2][0] = 0.4f;
	/* a capsule, owner 1 */
	scene_proxies[1].v[0][0] = -1.0f;
	scene_proxies[1].v[0][1] = 1.0f;
	scene_proxies[1].v[0][2] = 0.3f;
	scene_proxies[1].v[0][3] = _world_proxy_capsule;
	scene_proxies[1].v[1][0] = -0.6f;
	scene_proxies[1].v[1][1] = 1.2f;
	scene_proxies[1].v[1][2] = 1.6f;
	scene_proxies[1].v[1][3] = 1.0f;
	scene_proxies[1].v[2][0] = 0.3f;
	/* a box turned about z, owner 2 */
	length = sqrtf(axis_x[0] * axis_x[0] + axis_x[1] * axis_x[1]);
	scene_proxies[2].v[0][1] = -2.0f;
	scene_proxies[2].v[0][2] = 0.6f;
	scene_proxies[2].v[0][3] = _world_proxy_box;
	scene_proxies[2].v[1][0] = axis_x[0];
	scene_proxies[2].v[1][1] = axis_x[1];
	scene_proxies[2].v[1][3] = 2.0f;
	scene_proxies[2].v[2][0] = -axis_x[1] / length * 0.5f;
	scene_proxies[2].v[2][1] = axis_x[0] / length * 0.5f;
	scene_proxies[2].v[2][3] = 0.6f;
	/* a vehicle's occlusion field, owner 4: three shapes */
	scene_field(&scene_proxies[3]);
	/* a second sphere of owner 0 */
	scene_proxies[6] = scene_proxies[0];
	scene_proxies[6].v[0][2] = 1.6f;
	scene_proxies[6].v[2][0] = 0.3f;
	/* a box along the axes, owner 3 */
	scene_proxies[7].v[0][0] = 1.8f;
	scene_proxies[7].v[0][1] = 1.5f;
	scene_proxies[7].v[0][2] = 0.4f;
	scene_proxies[7].v[0][3] = _world_proxy_box;
	scene_proxies[7].v[1][0] = 0.6f;
	scene_proxies[7].v[1][3] = 3.0f;
	scene_proxies[7].v[2][1] = 0.4f;
	scene_proxies[7].v[2][3] = 0.4f;
	memset(scene_parameters, 0, sizeof(scene_parameters));
	scene_parameters[0][0] = 0.9f;
	scene_parameters[0][1] = 0.7f;
	scene_parameters[0][2] = 0.75f;
	scene_parameters[0][3] = (float)SCENE_PROXIES;
	scene_parameters[1][0] = 0.8f;
	/* (the first six the dynamic objects', the second sphere and the box
	along the axes the static scenery's, which the field and the pages
	hold) */
	scene_parameters[1][1] = (float)SCENE_DYNAMIC;
	{
		static const float camera[3] = { 0.4f, -0.3f, 1.5f };

		world_ao_grid(scene_proxies, scene_proxy_count, camera, scene_grid, scene_cells);
	}
}

/* a point about the shapes and the walls, a unit normal, an owner (-1 to 4)
and a reference */
void scene_point(float point[3], float normal[3], float *owner, float *reference)
{
	float length;
	int axis;

	/* (one in four farther out, where a box is found as its faces' patches) */
	float spread = scene_random() < 0.25f ? 12.0f : 3.0f;

	point[0] = -spread + 2.0f * spread * scene_random();
	point[1] = -spread + 2.0f * spread * scene_random();
	point[2] = 0.02f + 2.5f * scene_random();
	do
	{
		for (axis = 0; axis < 3; axis++)
			normal[axis] = 2.0f * scene_random() - 1.0f;
		length = sqrtf(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
	}
	while (length < 0.1f || length > 1.0f);
	for (axis = 0; axis < 3; axis++)
		normal[axis] /= length;
	*owner = (float)((int)(scene_random() * 6.0f) - 1);
	*reference = 0.1f + 0.9f * scene_random();
	/* (one in eight a static scenery placement's own draw: none of the field) */
	if (scene_random() < 0.125f)
		*reference = WORLD_AO_REFERENCE_NO_FIELD;
}

#ifndef SCENE_LIBRARY
/* ---------- cull: the grid's product against every proxy's, at random
windows of 64 shapes (boxes the most, as at b30's start) and random points
about them: equal, bit for bit. And how far a box's bound (its fade and
reach) moves the product from the 17 half diagonals' reach faded over its
last quarter that it replaced (the shapes alone, no grid) */

static float scene_between(float low, float high)
{
	return low + (high - low) * scene_random();
}

static void scene_direction(float out[3])
{
	float length;

	do
	{
		out[0] = scene_between(-1.0f, 1.0f);
		out[1] = scene_between(-1.0f, 1.0f);
		out[2] = scene_between(-1.0f, 1.0f);
		length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	}
	while (length < 0.1f || length > 1.0f);
	out[0] /= length;
	out[1] /= length;
	out[2] /= length;
}

static void scene_random_proxy(float const camera[3], struct world_proxy *proxy)
{
	float kind = scene_random(), centre[3];
	int k;

	memset(proxy, 0, sizeof(*proxy));
	centre[0] = camera[0] + scene_between(-60.0f, 60.0f);
	centre[1] = camera[1] + scene_between(-60.0f, 60.0f);
	centre[2] = camera[2] + scene_between(-8.0f, 4.0f);
	for (k = 0; k < 3; k++)
		proxy->v[0][k] = centre[k];
	proxy->v[1][3] = (float)(int)scene_between(0.0f, 20.0f);
	if (kind < 0.7f)
	{
		float x[3], y[3], along, length, ex = scene_between(0.1f, 3.0f), ey = scene_between(0.1f, 3.0f);

		proxy->v[0][3] = _world_proxy_box;
		scene_direction(x);
		scene_direction(y);
		along = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
		for (k = 0; k < 3; k++)
			y[k] -= along * x[k];
		length = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
		for (k = 0; k < 3; k++)
		{
			proxy->v[1][k] = ex * x[k];
			proxy->v[2][k] = ey * y[k] / length;
		}
		proxy->v[2][3] = scene_between(0.1f, 3.0f);
	}
	else if (kind < 0.85f)
	{
		proxy->v[0][3] = _world_proxy_sphere;
		proxy->v[2][0] = scene_between(0.1f, 1.0f);
	}
	else
	{
		float axis[3], length = scene_between(0.2f, 1.5f);

		proxy->v[0][3] = _world_proxy_capsule;
		scene_direction(axis);
		for (k = 0; k < 3; k++)
			proxy->v[1][k] = centre[k] + axis[k] * length;
		proxy->v[2][0] = scene_between(0.05f, 0.4f);
	}
}

/* the product as it was: every proxy, a box's reach 17 half diagonals and
faded over the last quarter of it */
static float scene_product_before(struct world_proxy const *proxies, long count, float const point[3],
	float const normal[3], float owner)
{
	float product = 1.0f;
	long index;

	for (index = 0; index < count; index++)
	{
		struct world_proxy const *proxy = &proxies[index];
		float centre[3], offset[3], reach, away, bound, fade;
		int axis;

		if (proxy->v[1][3] == owner)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			centre[axis] = (int)proxy->v[0][3] == _world_proxy_capsule ?
				0.5f * (proxy->v[0][axis] + proxy->v[1][axis]) : proxy->v[0][axis];
			offset[axis] = point[axis] - centre[axis];
		}
		if ((int)proxy->v[0][3] == _world_proxy_box)
		{
			bound = sqrtf(proxy->v[1][0] * proxy->v[1][0] + proxy->v[1][1] * proxy->v[1][1] +
				proxy->v[1][2] * proxy->v[1][2] + proxy->v[2][0] * proxy->v[2][0] + proxy->v[2][1] * proxy->v[2][1] +
				proxy->v[2][2] * proxy->v[2][2] + proxy->v[2][3] * proxy->v[2][3]);
			reach = 17.0f * bound;
		}
		else
		{
			reach = world_proxy_reach(proxy);
			bound = (int)proxy->v[0][3] == _world_proxy_capsule ? 0.5f * sqrtf((proxy->v[1][0] - proxy->v[0][0]) *
				(proxy->v[1][0] - proxy->v[0][0]) + (proxy->v[1][1] - proxy->v[0][1]) * (proxy->v[1][1] - proxy->v[0][1]) +
				(proxy->v[1][2] - proxy->v[0][2]) * (proxy->v[1][2] - proxy->v[0][2])) + proxy->v[2][0] : proxy->v[2][0];
		}
		away = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
		if (away >= reach)
			continue;
		if (normal[0] * -offset[0] + normal[1] * -offset[1] + normal[2] * -offset[2] < -bound)
			continue;
		fade = (reach - away) / (reach * WORLD_AO_REACH_FADE);
		fade = fade > 1.0f ? 1.0f : fade;
		product *= 1.0f - world_proxy_occlusion(proxy, point, normal) * fade;
	}
	return product;
}

static int scene_cull(long windows, long points)
{
	static struct world_proxy proxies[64];
	static float grid[2][4];
	static unsigned int cells[WORLD_AO_GRID_CELLS / 2][4];
	long window, index, unequal = 0, evaluated = 0, total = 0, boxes_kept = 0, boxes_in_reach = 0;
	double worst_before = 0.0, sum_before = 0.0;

	for (window = 0; window < windows; window++)
	{
		float camera[3];

		camera[0] = scene_between(-500.0f, 500.0f);
		camera[1] = scene_between(-500.0f, 500.0f);
		camera[2] = scene_between(-50.0f, 50.0f);
		for (index = 0; index < 64; index++)
			scene_random_proxy(camera, &proxies[index]);
		world_ao_grid(proxies, 64, camera, grid, cells);
		for (index = 0; index < points; index++)
		{
			float point[3], normal[3], owner = (float)((int)scene_between(0.0f, 21.0f) - 1), all, culled, before;
			int cell, word, shape;

			/* (near the shapes: about one of them, or anywhere about the camera) */
			if (index & 1)
			{
				struct world_proxy const *near = &proxies[(int)scene_between(0.0f, 63.99f)];
				float distance = scene_between(0.0f, 1.0f), direction[3];
				int k;

				distance = 0.05f + 30.0f * distance * distance;
				scene_direction(direction);
				for (k = 0; k < 3; k++)
					point[k] = near->v[0][k] + direction[k] * distance;
			}
			else
			{
				point[0] = camera[0] + scene_between(-140.0f, 140.0f);
				point[1] = camera[1] + scene_between(-140.0f, 140.0f);
				point[2] = camera[2] + scene_between(-12.0f, 8.0f);
			}
			scene_direction(normal);
			all = world_ao_proxies_occlusion(proxies, 64, point, normal, owner);
			culled = world_ao_proxies_occlusion_grid(proxies, (float const (*)[4])grid,
				(unsigned int const (*)[4])cells, point, normal, owner);
			if (all != culled)
			{
				if (unequal < 10)
					printf("unequal at %g %g %g: %.9g, grid %.9g\n", point[0], point[1], point[2], all, culled);
				unequal++;
			}
			before = scene_product_before(proxies, 64, point, normal, owner);
			worst_before = fabs(before - all) > worst_before ? fabs(before - all) : worst_before;
			sum_before += fabs(before - all);
			cell = world_ao_grid_cell((float const (*)[4])grid, point);
			for (word = 0; word < 2; word++)
				for (shape = 0; shape < 32; shape++)
					evaluated += (cells[cell >> 1][((cell & 1) << 1) + word] >> shape) & 1u;
			total += 64;
			for (shape = 0; shape < 64; shape++)
			{
				struct world_proxy const *proxy = &proxies[shape];
				float offset[3], away;
				int k;

				if ((int)proxy->v[0][3] != _world_proxy_box)
					continue;
				for (k = 0; k < 3; k++)
					offset[k] = point[k] - proxy->v[0][k];
				away = sqrtf(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
				if (away < 17.0f * sqrtf(proxy->v[1][0] * proxy->v[1][0] + proxy->v[1][1] * proxy->v[1][1] +
					proxy->v[1][2] * proxy->v[1][2] + proxy->v[2][0] * proxy->v[2][0] + proxy->v[2][1] * proxy->v[2][1] +
					proxy->v[2][2] * proxy->v[2][2] + proxy->v[2][3] * proxy->v[2][3]))
				{
					boxes_in_reach++;
					boxes_kept += world_proxy_box_fade(proxy, point, normal) > 0.0f;
				}
			}
		}
	}
	printf("{\"points\": %ld, \"unequal\": %ld, \"cell_share\": %.4f, \"boxes_computed_share\": %.4f, "
		"\"before_worst\": %.6f, \"before_mean\": %.7f}\n", windows * points, unequal, (double)evaluated / (double)total,
		boxes_in_reach ? (double)boxes_kept / (double)boxes_in_reach : 0.0, worst_before,
		sum_before / (double)(windows * points));
	return unequal ? 1 : 0;
}

/* ---------- pages: the static scenery's pages (world_ao_scenery_page) of a
floor 4 wu square, its lightmap's coordinates the square's, under a sphere:
each texel whose centre the floor covers is the shapes' product at its
place, as a byte; the texels about it take their neighbours'; and a page
whose second triangle's coordinates lie over the first's, at another
place, is found shared */

static void page_triangle(float *out, float const a[3], float const b[3], float const c[3], float const uv[6])
{
	static const float up[3] = { 0.0f, 0.0f, 1.0f };
	int k;

	for (k = 0; k < 3; k++)
	{
		out[k] = a[k];
		out[3 + k] = b[k];
		out[6 + k] = c[k];
		out[9 + k] = out[12 + k] = out[15 + k] = up[k];
	}
	memcpy(out + 18, uv, 6 * sizeof(float));
}

static int scene_pages_check(void)
{
	enum { SIDE = 32 };
	static const float a[3] = { -2.0f, -2.0f, 0.0f }, b[3] = { 2.0f, -2.0f, 0.0f }, c[3] = { 2.0f, 2.0f, 0.0f },
		d[3] = { -2.0f, 2.0f, 0.0f };
	/* (the square's middle half of the page: a margin of texels about it) */
	static const float first[6] = { 0.25f, 0.25f, 0.75f, 0.25f, 0.75f, 0.75f },
		second[6] = { 0.25f, 0.25f, 0.75f, 0.75f, 0.25f, 0.75f };
	static const float up[3] = { 0.0f, 0.0f, 1.0f };
	float triangles[3 * WORLD_AO_SCENERY_TRIANGLE];
	unsigned char texels[SIDE * SIDE];
	struct world_proxy sphere;
	long covered, shared, x, y, wrong = 0;

	memset(&sphere, 0, sizeof(sphere));
	sphere.v[0][2] = 0.8f;
	sphere.v[0][3] = _world_proxy_sphere;
	sphere.v[1][2] = 0.8f;
	sphere.v[1][3] = 7.0f;
	sphere.v[2][0] = 0.5f;
	page_triangle(triangles, a, b, c, first);
	page_triangle(triangles + WORLD_AO_SCENERY_TRIANGLE, a, c, d, second);
	covered = world_ao_scenery_page(&sphere, 1, triangles, 2, SIDE, SIDE, texels, &shared);
	/* (the texels whose centres are in the square: 16 a side) */
	if (covered != 16 * 16 || shared)
	{
		printf("covered %ld shared %ld\n", covered, shared);
		return 1;
	}
	for (y = 0; y < SIDE; y++)
	{
		for (x = 0; x < SIDE; x++)
		{
			float u = ((float)x + 0.5f) / SIDE, v = ((float)y + 0.5f) / SIDE, place[3];
			int inside = u > 0.25f && u < 0.75f && v > 0.25f && v < 0.75f;
			int near = !inside && u > 0.25f - 2.0f / SIDE && u < 0.75f + 2.0f / SIDE && v > 0.25f - 2.0f / SIDE &&
				v < 0.75f + 2.0f / SIDE;
			int expected;

			place[0] = -2.0f + (u - 0.25f) * 8.0f;
			place[1] = -2.0f + (v - 0.25f) * 8.0f;
			place[2] = 0.0f;
			expected = (int)(world_ao_proxies_occlusion(&sphere, 1, place, up, -3.0f) * 255.0f + 0.5f);
			/* (inside: its own; two texels about it: its neighbours', within a
			few steps of its edge's; past them, none: 255) */
			if (inside ? texels[y * SIDE + x] != expected : near ? abs(texels[y * SIDE + x] - expected) > 24 :
				texels[y * SIDE + x] != 255)
			{
				if (wrong++ < 4)
					printf("texel %ld %ld: %d, expected %d\n", x, y, texels[y * SIDE + x], expected);
			}
		}
	}
	/* (a third triangle over the first's coordinates, a metre up: shared) */
	{
		static const float raised[3][3] = { { -2.0f, -2.0f, 1.0f }, { 2.0f, -2.0f, 1.0f }, { 2.0f, 2.0f, 1.0f } };

		page_triangle(triangles + 2 * WORLD_AO_SCENERY_TRIANGLE, raised[0], raised[1], raised[2], first);
		covered = world_ao_scenery_page(&sphere, 1, triangles, 3, SIDE, SIDE, texels, &shared);
		if (covered != 16 * 16 || shared < 100)
		{
			printf("over: covered %ld shared %ld\n", covered, shared);
			return 1;
		}
	}
	printf("pages: %ld wrong\n", wrong);
	return wrong ? 1 : 0;
}

/* how many of the first count points (as the cases') the field shades
(its factor under 1 - 1/255): the check holds where it is found */
static long scene_field_shaded(long count)
{
	long index, shaded = 0;

	scene_seed = 12345;
	for (index = 0; index < count; index++)
	{
		float point[3], normal[3], owner, reference;

		scene_point(point, normal, &owner, &reference);
		shaded += world_ao_proxies_occlusion(&scene_proxies[3], 3, point, normal, -1.0f) < 1.0f - 1.0f / 255.0f;
	}
	return shaded;
}

int main(int argc, char **argv)
{
	long count = argc > 2 && !strcmp(argv[1], "reference") ? atol(argv[2]) : 0, index;
	unsigned long voxel;
	int i, j;

	if (argc > 3 && !strcmp(argv[1], "cull"))
		return scene_cull(atol(argv[2]), atol(argv[3]));
	if (argc > 1 && !strcmp(argv[1], "pages"))
		return scene_pages_check();
	if (count <= 0)
	{
		fprintf(stderr, "scene reference <count> | scene cull <windows> <points>\n");
		return 2;
	}
	scene_make();
	printf("{\"size\": [%d, %d, %d], \"origin\": [%.9g, %.9g, %.9g], \"voxel\": %.9g, \"range\": %.9g,\n",
		SCENE_WIDTH, SCENE_HEIGHT, SCENE_DEPTH, scene_volume.header.origin[0], scene_volume.header.origin[1],
		scene_volume.header.origin[2], scene_volume.header.voxel, scene_volume.header.range);
	/* (the field as the GPU has it: its table's bytes, and its atlas's layers) */
	printf("\"layers\": %lu, \"table\": [", world_sdf_atlas_layers(&scene_volume.header));
	for (voxel = 0; voxel < SCENE_WIDTH * SCENE_HEIGHT * SCENE_DEPTH * WORLD_SDF_CELL_BYTES; voxel++)
		printf("%s%d", voxel ? "," : "", scene_volume.cells[voxel / 4][voxel % 4]);
	printf("],\n\"atlas\": [");
	{
		static signed char layer[WORLD_SDF_ATLAS_LAYER_BYTES];
		unsigned long at;

		for (voxel = 0; voxel < world_sdf_atlas_layers(&scene_volume.header); voxel++)
		{
			world_sdf_atlas_layer(&scene_volume, voxel, layer);
			for (at = 0; at < WORLD_SDF_ATLAS_LAYER_BYTES; at++)
				printf("%s%d", voxel || at ? "," : "", layer[at]);
		}
	}
	printf("],\n\"parameters\": [");
	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++)
			printf("%s%.9g", i || j ? "," : "", scene_parameters[i][j]);
	printf("],\n\"proxies\": [");
	for (index = 0; index < scene_proxy_count; index++)
		for (i = 0; i < 3; i++)
			for (j = 0; j < 4; j++)
				printf("%s%.9g", index || i || j ? "," : "", scene_proxies[index].v[i][j]);
	printf("],\n\"grid\": [");
	for (i = 0; i < 2; i++)
		for (j = 0; j < 4; j++)
			printf("%s%.9g", i || j ? "," : "", scene_grid[i][j]);
	printf("],\n\"cells\": [");
	for (index = 0; index < WORLD_AO_GRID_CELLS / 2; index++)
		for (j = 0; j < 4; j++)
			printf("%s%u", index || j ? "," : "", scene_cells[index][j]);
	printf("],\n\"pages\": [%.9g, %.9g],\n", scene_pages[0], scene_pages[1]);
	/* (the objects' atlas: its size, its texels' four values each) */
	printf("\"objects\": [%lu, %lu, %lu], \"objects_texels\": [", scene_atlas_size[0], scene_atlas_size[1],
		scene_atlas_size[2]);
	for (voxel = 0; voxel < scene_atlas_size[0] * scene_atlas_size[1] * scene_atlas_size[2] * 4; voxel++)
		printf("%s%.9g", voxel ? "," : "", scene_atlas_values[voxel]);
	printf("],\n\"cases\": [\n");
	for (index = 0; index < count; index++)
	{
		float point[3], normal[3], owner, reference;
		int lightmap;

		scene_point(point, normal, &owner, &reference);
		for (lightmap = 0; lightmap < 2; lightmap++)
		{
			/* (a lightmap draw's reference is its page's layer: none, the
			first or the second) */
			float given = lightmap ? (float)(index % 3 - 1) : reference;

			/* (and a model's as a two-sided shader's cards: about their most
			open direction) */
			printf("%s{\"point\": [%.9g, %.9g, %.9g], \"normal\": [%.9g, %.9g, %.9g], \"owner\": %.9g, "
				"\"reference\": %.9g, \"lightmap\": %d, \"occlusion\": %.9g, \"cards\": %.9g}\n",
				index || lightmap ? "," : "", point[0], point[1], point[2], normal[0], normal[1], normal[2], owner, given,
				lightmap, world_ao_occlusion((float const (*)[4])scene_parameters, &scene_volume, scene_proxies,
				scene_proxy_count, point, normal, owner, given, lightmap, given >= 0.0f ? scene_pages[(int)given] : 1.0f, 0),
				world_ao_occlusion((float const (*)[4])scene_parameters, &scene_volume, scene_proxies,
				scene_proxy_count, point, normal, owner, given, lightmap, given >= 0.0f ? scene_pages[(int)given] : 1.0f,
				!lightmap));
		}
	}
	printf("], \"field_shaded\": %ld}\n", scene_field_shaded(count));
	return 0;
}
#endif
