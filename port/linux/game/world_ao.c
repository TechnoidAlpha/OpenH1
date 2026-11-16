/*
WORLD_AO.C

display.ambient_occlusion "world" (world_ao.h, docs/world-ao.md): the
renderer's side of each frame and window. The scene found again
(world_scene.c, workstream A), the field's upload continued (world_sdf.c,
B), the window's proxies chosen (world_proxies.c, C) and handed with the
field's place to the front end (halo_world_ao_*, d3d8_device.c), which the
shaders read in the ambient slot (D: the arithmetic is world_ao_shading.c,
the shaders' world_ao_* in xgpu_spirv.c and xgpu_msl.c). Each object drawn
is named to the front end with its owner slot among the window's proxies'
owners (its own do not shade it) and its reference (the field's occlusion
where its ambient light was sampled, which that light already holds).

With "ray_traced", the same, and the dynamic objects' occlusion traced by
the GPU's ray queries through the ray-traced scene (world_rt.c: its meshes
and each frame's instances) where the backend has them; "world" where it has
not (said once, with why).

Nothing here runs unless the setting is "world" or "ray_traced": every
other value (and the Classic preset's "off") draws exactly as before.
*/

#include "world_ao.h"
#include "world_cascades.h"
#include "world_proxies.h"
#include "world_rt.h"
#include "world_scene.h"
#include "world_sdf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(const char *name);
int config_boolean(const char *name);
unsigned long halo_gpu_settings_taken(void);
unsigned long long SDL_GetTicksNS(void);
void platform_log(const char *format, ...);
/* (render_enumerate.c's: the window drawn is a map's first frame's, unseen,
which prepares its pipelines) */
extern unsigned char render_enumerating;

static struct
{
	int initialized;
	unsigned long read_at;
	int mode;
	/* "ray_traced" asked for, and whether why it is drawn as "world" was
	said */
	int rays, rays_missing_said;
	/* a window's occlusion handed to the front end (world_ao_window_end
	takes it away): its proxies and their objects (datum handles), the
	field's volume (for the objects' references; NULL none) and its fade */
	int window;
	/* the window's span (WORLD_AO_SPAN_OPEN, _FOUND, or _NONE outside one),
	whatever the setting: the first-person weapon's drawn in it */
	int span;
	struct world_proxy proxies[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	long owners[WORLD_PROXY_MAXIMUM_PER_WINDOW];
	/* (the dynamic objects' first, dynamic_count of them; the static
	scenery's the field and its pages hold after) */
	long count, dynamic_count;
	struct world_sdf_volume const *volume;
	float fade;
	/* the proxies' grid (world_ao_grid; its cells allocated once) */
	float grid[2][4];
	unsigned int (*cells)[4];
	/* the distance-field scene's cascades as the window has them (each the
	first voxel's centre and the voxel, 0 not ready) */
	float cascades[WORLD_CASCADES][4];
} world_ao;

/* (read again as the front end takes the settings up between frames,
d3d8_device.c's halo_gpu_settings_taken, not as they are written: the map
is prepared again under the change at the same frame, render_enumerate.c's
enumerated_derivation_check, so that no frame draws the world's pipelines
before they are made) */
int world_ao_mode(void)
{
	unsigned long changes = halo_gpu_settings_taken();

	if (!world_ao.initialized || world_ao.read_at != changes)
	{
		const char *setting = config_string("display.ambient_occlusion");

		world_ao.initialized = 1;
		world_ao.read_at = changes;
		world_ao.rays = setting && !strcmp(setting, "ray_traced");
		world_ao.mode = setting && (!strcmp(setting, "world") || world_ao.rays) ? WORLD_AO_PROXIES | WORLD_AO_FIELD : 0;
	}
	/* (the backend's ray queries, once it has said: until then, and
	without them, "world") */
	if (world_ao.rays && halo_world_ao_ray_queries())
		return world_ao.mode | WORLD_AO_RAYS;
	if (world_ao.rays && !world_ao.rays_missing_said && halo_world_ao_ray_queries_missing())
	{
		platform_log("world ao: display.ambient_occlusion \"ray_traced\" is drawn as \"world\" here: %s",
			halo_world_ao_ray_queries_missing());
		world_ao.rays_missing_said = 1;
	}
	return world_ao.mode;
}

void world_ao_map_loaded(void)
{
	if (!world_ao_mode())
		return;
	world_scene_map_loaded();
	world_sdf_map_loaded();
}

void world_ao_bsp_switched(short bsp_index)
{
	if (!world_ao_mode())
		return;
	world_scene_bsp_switched(bsp_index);
	world_sdf_bsp_switched(bsp_index);
}

static void world_ao_scene_release(void);

void world_ao_map_unloaded(void)
{
	world_rt_map_unloaded();
	world_ao_scene_release();
	world_sdf_map_unloaded();
	world_proxies_map_unloaded();
	world_scene_map_unloaded();
}

/* ---------- the distance-field scene (world_cascades.h, docs/world-ao.md
"The distance-field scene")

Its cascades about the first window's camera, updated once a frame: the
structure's field sampled where they move or it changes, the objects'
shapes (world_proxies_scene) stamped where they moved, and the boxes
written handed to the renderer. HALO_WORLD_AO_COUNTS logs, every 600
frames, its mean time and voxels a frame. */

enum
{
	/* (the farthest a shape is taken from the camera: the outermost
	cascade's half side and its range) */
	WORLD_AO_SCENE_SHAPES = WORLD_CASCADES_MAXIMUM_SHAPES,
};

static struct
{
	struct world_cascades cascades;
	int made, failed;
	/* the frame (world_ao_frame_begin's count) it was last updated in, and
	the structure's field it holds (NULL none) */
	unsigned long frame, updated;
	struct world_sdf_volume const *structure;
	/* the frame's shapes and their objects */
	struct world_proxy shapes[WORLD_AO_SCENE_SHAPES];
	long owners[WORLD_AO_SCENE_SHAPES];
	struct world_cascades_object objects[WORLD_CASCADES_MAXIMUM_OBJECTS];
	/* a box's bytes on their way */
	signed char *box;
	/* the windows drawn this frame (windows_frame) and the frame before
	(last_windows, last_frame) */
	unsigned long windows_frame, last_frame;
	long windows, last_windows;
	/* (measuring) */
	double seconds, most_seconds, shapes_seconds, cascades_seconds;
	long frames, voxels, moved, boxes;
	unsigned long bytes;
} world_ao_scene;

static float world_ao_scene_structure(void *context, float const point[3])
{
	return world_sdf_sample((struct world_sdf_volume const *)context, point);
}

static void world_ao_scene_release(void)
{
	if (world_ao_scene.made)
	{
		world_cascades_free(&world_ao_scene.cascades);
		halo_world_ao_cascades(0, 0, 0, 0, 0, 0, NULL);
	}
	free(world_ao_scene.box);
	world_ao_scene.box = NULL;
	world_ao_scene.made = world_ao_scene.failed = 0;
	world_ao_scene.structure = NULL;
}

/* the cascades about the camera, once a frame; their block (each [k] the
first voxel's centre and the voxel, 0 not ready) */
static void world_ao_scene_update(float const camera[3], float block[WORLD_CASCADES][4])
{
	struct world_sdf_volume const *structure = (struct world_sdf_volume const *)world_sdf_current();
	unsigned long long start = SDL_GetTicksNS();
	float reach = WORLD_CASCADE_FINEST * (float)(1 << (WORLD_CASCADES - 1)) *
		(float)(WORLD_CASCADE_SIDE / 2 + WORLD_CASCADE_RANGE_VOXELS + 1);
	long count, index, object_count = 0, box_index;

	memset(block, 0, sizeof(float) * 4 * WORLD_CASCADES);
	if (world_ao_scene.failed)
		return;
	if (!world_ao_scene.made)
	{
		world_ao_scene.box = malloc(WORLD_CASCADES_BYTES);
		if (!world_ao_scene.box || !world_cascades_create(&world_ao_scene.cascades))
		{
			free(world_ao_scene.box);
			world_ao_scene.box = NULL;
			world_ao_scene.failed = 1;
			return;
		}
		world_ao_scene.made = 1;
		world_ao_scene.structure = NULL;
		world_ao_scene.updated = world_ao_scene.frame - 1;
	}
	if (world_ao_scene.updated != world_ao_scene.frame)
	{
		struct world_cascades *cascades = &world_ao_scene.cascades;

		world_ao_scene.updated = world_ao_scene.frame;
		count = world_proxies_scene(camera, reach, world_ao_scene.shapes, world_ao_scene.owners, WORLD_AO_SCENE_SHAPES);
		for (index = 0; index < count; index++)
		{
			if (index && world_ao_scene.owners[index] == world_ao_scene.owners[index - 1])
			{
				world_ao_scene.objects[object_count - 1].count++;
				continue;
			}
			if (object_count >= WORLD_CASCADES_MAXIMUM_OBJECTS)
				break;
			world_ao_scene.objects[object_count].id = world_ao_scene.owners[index];
			world_ao_scene.objects[object_count].first = index;
			world_ao_scene.objects[object_count].count = 1;
			object_count++;
		}
		world_ao_scene.shapes_seconds += (double)(SDL_GetTicksNS() - start) * 1.0e-9;
		world_cascades_update(cascades, camera, structure != world_ao_scene.structure,
			structure ? world_ao_scene_structure : NULL, (void *)structure, world_ao_scene.objects, object_count,
			world_ao_scene.shapes);
		world_ao_scene.structure = structure;
		world_ao_scene.cascades_seconds += (double)(SDL_GetTicksNS() - start) * 1.0e-9;
		/* (the boxes written: each its texels, x fastest; or the whole) */
		if (cascades->whole)
		{
			halo_world_ao_cascades(0, 0, 0, WORLD_CASCADE_SPAN, WORLD_CASCADE_SPAN, WORLD_CASCADE_SPAN * WORLD_CASCADES,
				cascades->texels);
			world_ao_scene.bytes += WORLD_CASCADES_BYTES;
		}
		for (box_index = 0; !cascades->whole && box_index < cascades->box_count; box_index++)
		{
			struct world_cascades_box const *box = &cascades->boxes[box_index];
			signed char *at = world_ao_scene.box;
			long y, z;

			for (z = 0; z < box->size[2]; z++)
			{
				for (y = 0; y < box->size[1]; y++)
				{
					size_t row = (((size_t)(box->place[2] + z) * WORLD_CASCADE_SPAN + (size_t)(box->place[1] + y)) *
						WORLD_CASCADE_SPAN + (size_t)box->place[0]) * WORLD_CASCADE_TEXEL;

					memcpy(at, cascades->texels + row, (size_t)box->size[0] * WORLD_CASCADE_TEXEL);
					at += box->size[0] * WORLD_CASCADE_TEXEL;
				}
			}
			halo_world_ao_cascades((unsigned long)box->place[0], (unsigned long)box->place[1],
				(unsigned long)box->place[2], (unsigned long)box->size[0], (unsigned long)box->size[1],
				(unsigned long)box->size[2], world_ao_scene.box);
			world_ao_scene.bytes += (unsigned long)(at - world_ao_scene.box);
		}
		/* (measuring) */
		{
			double seconds = (double)(SDL_GetTicksNS() - start) * 1.0e-9;

			world_ao_scene.seconds += seconds;
			world_ao_scene.most_seconds = seconds > world_ao_scene.most_seconds ? seconds : world_ao_scene.most_seconds;
			world_ao_scene.voxels += cascades->structure_voxels + cascades->object_voxels;
			world_ao_scene.moved += cascades->moved;
			world_ao_scene.boxes += cascades->box_count;
			if (++world_ao_scene.frames >= 600)
			{
				if (getenv("HALO_WORLD_AO_COUNTS"))
				{
					printf("world ao: the distance-field scene's update %.3f ms a frame (at most %.3f; its shapes found "
						"%.3f, the cascades %.3f), %.0f voxels, %.1f objects moved, %.1f boxes, %.1f KB uploaded a frame; "
						"%ld objects, %ld shapes\n",
						world_ao_scene.seconds * 1000.0 / 600.0, world_ao_scene.most_seconds * 1000.0,
						world_ao_scene.shapes_seconds * 1000.0 / 600.0,
						(world_ao_scene.cascades_seconds - world_ao_scene.shapes_seconds) * 1000.0 / 600.0,
						(double)world_ao_scene.voxels / 600.0, (double)world_ao_scene.moved / 600.0,
						(double)world_ao_scene.boxes / 600.0, (double)world_ao_scene.bytes / 600.0 / 1024.0,
						cascades->object_count, cascades->shape_count);
					fflush(stdout);
				}
				world_ao_scene.seconds = world_ao_scene.most_seconds = 0.0;
				world_ao_scene.shapes_seconds = world_ao_scene.cascades_seconds = 0.0;
				world_ao_scene.frames = world_ao_scene.voxels = world_ao_scene.moved = world_ao_scene.boxes = 0;
				world_ao_scene.bytes = 0;
			}
		}
	}
	world_cascades_block(&world_ao_scene.cascades, block);
}

void world_ao_frame_begin(void)
{
	const char *test;

	world_ao_scene.frame++;
	if (!world_ao_mode())
		return;
	world_scene_frame_begin();
	/* (not with debug.world_ao_test's made-up field, which the level's
	would overwrite in the backend's one texture) */
	test = config_string("debug.world_ao_test");
	if (!test || !*test)
		world_sdf_frame();
}

/* ---------- debug.world_ao_test

A made-up window, for the shading's checks without the scene, the field's
bake or the proxies (docs/world-ao.md, "D. Shading"): "plane nx ny nz d" (a
field of the plane n.x = d, solid behind it, about the camera where it was
first asked for), then "; sphere x y z r", "; capsule ax ay az bx by bz r"
or "; box x y z ex ey ez" (along the world's axes), at most
WORLD_AO_TEST_SHAPES, each its own owner's (none of the objects': they
shade every one). */

enum
{
	WORLD_AO_TEST_SHAPES = 8,
	/* (its cells; a whole grid of voxels the cells' steps and one more) */
	WORLD_AO_TEST_WIDTH = 12,
	WORLD_AO_TEST_HEIGHT = 12,
	WORLD_AO_TEST_DEPTH = 6,
	WORLD_AO_TEST_VOXELS = (WORLD_SDF_BRICK * WORLD_AO_TEST_WIDTH + 1) * (WORLD_SDF_BRICK * WORLD_AO_TEST_HEIGHT + 1) *
		(WORLD_SDF_BRICK * WORLD_AO_TEST_DEPTH + 1),
	WORLD_AO_TEST_OWNER = 200,
};

static struct
{
	char text[256];
	int parsed, uploaded;
	float plane[4];
	struct world_proxy proxies[WORLD_AO_TEST_SHAPES];
	long count;
	struct world_sdf_volume volume;
	/* (allocated when first asked for: the guest's image holds no more) */
	signed char *voxels;
} world_ao_test;

static void world_ao_test_parse(const char *setting)
{
	const char *at;

	signed char *voxels = world_ao_test.voxels;

	free(world_ao_test.volume.cells);
	free(world_ao_test.volume.bricks);
	memset(&world_ao_test, 0, sizeof(world_ao_test));
	world_ao_test.voxels = voxels;
	snprintf(world_ao_test.text, sizeof(world_ao_test.text), "%s", setting);
	world_ao_test.parsed = 1;
	for (at = setting; at && *at; at = strchr(at, ';') ? strchr(at, ';') + 1 : NULL)
	{
		struct world_proxy *proxy = &world_ao_test.proxies[world_ao_test.count];
		float v[7];

		while (*at == ' ')
			at++;
		if (sscanf(at, "plane %f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4)
		{
			memcpy(world_ao_test.plane, v, sizeof(world_ao_test.plane));
			continue;
		}
		if (world_ao_test.count == WORLD_AO_TEST_SHAPES)
			break;
		memset(proxy, 0, sizeof(*proxy));
		if (sscanf(at, "sphere %f %f %f %f", &v[0], &v[1], &v[2], &v[3]) == 4)
		{
			memcpy(proxy->v[0], v, 3 * sizeof(float));
			proxy->v[0][3] = _world_proxy_sphere;
			proxy->v[2][0] = v[3];
		}
		else if (sscanf(at, "capsule %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) == 7)
		{
			memcpy(proxy->v[0], v, 3 * sizeof(float));
			memcpy(proxy->v[1], v + 3, 3 * sizeof(float));
			proxy->v[0][3] = _world_proxy_capsule;
			proxy->v[2][0] = v[6];
		}
		else if (sscanf(at, "box %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6)
		{
			memcpy(proxy->v[0], v, 3 * sizeof(float));
			proxy->v[0][3] = _world_proxy_box;
			proxy->v[1][0] = v[3];
			proxy->v[2][1] = v[4];
			proxy->v[2][3] = v[5];
		}
		else
		{
			continue;
		}
		proxy->v[1][3] = (float)(WORLD_AO_TEST_OWNER + world_ao_test.count);
		world_ao_test.count++;
	}
}

/* the plane's field about the camera, as a bake encodes one */
static void world_ao_test_field(float const camera_position[3])
{
	struct world_sdf_header *header = &world_ao_test.volume.header;
	float const *plane = world_ao_test.plane;
	float length = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
	unsigned long x, y, z, row = WORLD_SDF_BRICK * WORLD_AO_TEST_WIDTH + 1, rows = WORLD_SDF_BRICK * WORLD_AO_TEST_HEIGHT + 1;

	if (!world_ao_test.voxels)
		world_ao_test.voxels = malloc(WORLD_AO_TEST_VOXELS);
	if (!world_ao_test.voxels)
		return;
	memset(header, 0, sizeof(*header));
	header->width = WORLD_AO_TEST_WIDTH;
	header->height = WORLD_AO_TEST_HEIGHT;
	header->depth = WORLD_AO_TEST_DEPTH;
	header->voxel = (float)WORLD_SDF_FINEST_VOXEL_MILLI / 1000.0f;
	header->range = WORLD_SDF_RANGE_VOXELS * header->voxel;
	header->origin[0] = camera_position[0] - 0.5f * (float)(WORLD_SDF_BRICK * WORLD_AO_TEST_WIDTH) * header->voxel;
	header->origin[1] = camera_position[1] - 0.5f * (float)(WORLD_SDF_BRICK * WORLD_AO_TEST_HEIGHT) * header->voxel;
	header->origin[2] = camera_position[2] - 0.5f * (float)(WORLD_SDF_BRICK * WORLD_AO_TEST_DEPTH) * header->voxel;
	for (z = 0; z < WORLD_SDF_BRICK * WORLD_AO_TEST_DEPTH + 1; z++)
	{
		for (y = 0; y < rows; y++)
		{
			for (x = 0; x < row; x++)
			{
				float point[3], distance;

				point[0] = header->origin[0] + (float)x * header->voxel;
				point[1] = header->origin[1] + (float)y * header->voxel;
				point[2] = header->origin[2] + (float)z * header->voxel;
				distance = length > 0.0f ? (plane[0] * point[0] + plane[1] * point[1] + plane[2] * point[2] -
					plane[3]) / length : header->range;
				world_ao_test.voxels[(z * rows + y) * row + x] = world_sdf_encode(distance, header->range);
			}
		}
	}
	free(world_ao_test.volume.cells);
	free(world_ao_test.volume.bricks);
	if (!world_sdf_pack(&world_ao_test.volume, world_ao_test.voxels))
		return;
	world_sdf_upload(&world_ao_test.volume);
	world_ao_test.uploaded = 1;
}

/* the made-up window's (FALSE: none asked for) */
static int world_ao_test_window(float const camera_position[3])
{
	const char *setting = config_string("debug.world_ao_test");

	if (!setting || !*setting)
		return 0;
	if (!world_ao_test.parsed || strcmp(setting, world_ao_test.text))
		world_ao_test_parse(setting);
	if (!world_ao_test.uploaded)
		world_ao_test_field(camera_position);
	if (!world_ao_test.uploaded)
		return 0;
	memcpy(world_ao.proxies, world_ao_test.proxies, sizeof(world_ao_test.proxies));
	memset(world_ao.owners, 0xff, sizeof(world_ao.owners));
	world_ao.count = world_ao_test.count;
	world_ao.volume = &world_ao_test.volume;
	world_ao.fade = 1.0f;
	return 1;
}

/* HALO_WORLD_AO_COUNTS (measuring): every 600 windows, the windows'
shapes and the dynamic objects' of them, and the lightmap pages drawn with
a layer and without, a window's mean */
static struct
{
	int wanted;
	long windows, shapes, dynamic, paged, unpaged, kinds[NUMBER_OF_WORLD_PROXY_KINDS];
} world_ao_counts = { -1 };

static void world_ao_counts_window(void)
{
	if (world_ao_counts.wanted < 0)
		world_ao_counts.wanted = getenv("HALO_WORLD_AO_COUNTS") != NULL;
	if (!world_ao_counts.wanted)
		return;
	world_ao_counts.windows++;
	world_ao_counts.shapes += world_ao.count;
	world_ao_counts.dynamic += world_ao.dynamic_count;
	{
		long index;

		/* (the dynamic objects' kinds) */
		for (index = 0; index < world_ao.dynamic_count; index++)
		{
			int kind = (int)world_ao.proxies[index].v[0][3];

			if (kind > 0 && kind < NUMBER_OF_WORLD_PROXY_KINDS)
				world_ao_counts.kinds[kind]++;
		}
	}
	if (world_ao_counts.windows >= 600)
	{
		printf("world ao: a window's %.1f shapes, %.1f of them dynamic (%.1f spheres, %.1f capsules, %.1f boxes, %.1f "
			"occlusion fields); %.1f lightmap pages with a layer, %.1f without\n", (double)world_ao_counts.shapes / 600.0,
			(double)world_ao_counts.dynamic / 600.0, (double)world_ao_counts.kinds[_world_proxy_sphere] / 600.0,
			(double)world_ao_counts.kinds[_world_proxy_capsule] / 600.0, (double)world_ao_counts.kinds[_world_proxy_box] / 600.0,
			(double)world_ao_counts.kinds[_world_proxy_field] / 600.0,
			(double)world_ao_counts.paged / 600.0, (double)world_ao_counts.unpaged / 600.0);
		fflush(stdout);
		/* (the counts zeroed, whatever the padding after wanted) */
		memset(world_ao_counts.kinds, 0, sizeof(world_ao_counts.kinds));
		world_ao_counts.windows = world_ao_counts.shapes = world_ao_counts.dynamic = 0;
		world_ao_counts.paged = world_ao_counts.unpaged = 0;
	}
}

/* ---------- a window */

/* the block's field: the volume's place (without one, a stand-in's) */
static void world_ao_field_place(struct world_sdf_header const *header, float field[2][4])
{
	static const float none[2][4] = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };

	if (!header)
	{
		memcpy(field, none, sizeof(none));
		return;
	}
	field[0][0] = header->origin[0];
	field[0][1] = header->origin[1];
	field[0][2] = header->origin[2];
	field[0][3] = 1.0f / header->voxel;
	field[1][0] = (float)header->width;
	field[1][1] = (float)header->height;
	field[1][2] = (float)header->depth;
	/* (the atlas's layers, at least one: as world_sdf.c uploads it) */
	field[1][3] = (float)((header->brick_count + WORLD_SDF_ATLAS_LAYER - 1) / WORLD_SDF_ATLAS_LAYER);
	if (field[1][3] < 1.0f)
		field[1][3] = 1.0f;
}

void world_ao_window_begin(short local_player_index, short rasterizer_target, float const camera_position[3])
{
	float field[2][4], parameters[2][4];
	int mode = world_ao_mode();

	(void)local_player_index;
	(void)rasterizer_target;
	/* (its span, whatever the setting: halo_world_ao_span) */
	world_ao.span = WORLD_AO_SPAN_OPEN;
	halo_world_ao_span(WORLD_AO_SPAN_OPEN);
	/* (none while a map's pipelines are prepared, nothing being seen then:
	the front end gives its draws the keys they have in play from their span
	alone, and makes the pre-pass's twins of them, d3d8_device.c's
	world_ao_key) */
	if (!mode)
		return;
	if (render_enumerating)
	{
		world_sdf_frame_prepared();
		return;
	}
	world_ao.count = world_ao.dynamic_count = 0;
	world_ao.volume = NULL;
	world_ao.fade = 0.0f;
	if (!world_ao_test_window(camera_position))
	{
		if (mode & WORLD_AO_PROXIES)
			world_ao.count = world_proxies_window(camera_position, world_ao.proxies, world_ao.owners,
				WORLD_PROXY_MAXIMUM_PER_WINDOW, &world_ao.dynamic_count);
		/* (world_sdf_current's header is its volume's first member: the
		volume itself, for the objects' references) */
		if (mode & WORLD_AO_FIELD)
			world_ao.volume = (struct world_sdf_volume const *)world_sdf_current();
		world_ao.fade = world_ao.volume ? world_sdf_fade() : 0.0f;
		/* (the distance-field scene about the camera, once a frame; the
		ray-traced scene's instances too, with "ray_traced") */
		world_ao_scene_update(camera_position, world_ao.cascades);
		if (mode & WORLD_AO_RAYS)
			world_rt_frame(world_ao_scene.frame);
		if (world_ao_scene.windows_frame != world_ao_scene.frame)
		{
			world_ao_scene.last_windows = world_ao_scene.windows;
			world_ao_scene.last_frame = world_ao_scene.windows_frame;
			world_ao_scene.windows_frame = world_ao_scene.frame;
			world_ao_scene.windows = 0;
		}
		world_ao_scene.windows++;
	}
	if (world_ao.count < 0)
		world_ao.count = 0;
	/* (debug.world_ao_test's shapes: every one an object's) */
	if (world_ao.dynamic_count <= 0 || world_ao.dynamic_count > world_ao.count)
		world_ao.dynamic_count = world_ao.count;
	world_ao_field_place(world_ao.volume ? &world_ao.volume->header : NULL, field);
	memset(parameters, 0, sizeof(parameters));
	parameters[0][0] = WORLD_AO_STRENGTH_OBJECTS;
	parameters[0][1] = WORLD_AO_STRENGTH_LEVEL;
	parameters[0][2] = world_ao.fade;
	parameters[0][3] = (float)world_ao.count;
	parameters[1][0] = WORLD_AO_REFERENCE_WEIGHT;
	parameters[1][1] = (float)world_ao.dynamic_count;
	/* (the proxies' grid about the camera: each pixel computes those that
	can reach its cell alone. Its 16 KB of cells on the heap, made once) */
	if (!world_ao.cells)
		world_ao.cells = malloc(sizeof(unsigned int) * 4 * (WORLD_AO_GRID_CELLS / 2));
	if (!world_ao.cells)
		return;
	world_ao_grid(world_ao.proxies, world_ao.count, camera_position, world_ao.grid, world_ao.cells);
	halo_world_ao_window((float const (*)[4])field, (float const (*)[4])parameters,
		(float const (*)[3][4])world_ao.proxies, (int)world_ao.count, (float const (*)[4])world_ao.grid,
		(unsigned int const (*)[4])world_ao.cells);
	world_ao.window = 1;
	world_ao_counts_window();
}

int world_ao_prepass_begin(void)
{
	/* (its span, whatever the setting and while a map's pipelines are
	prepared: the surfaces found once a pixel where it is on, which a map's
	preparation makes the pipelines and twins of: d3d8_device.c's
	world_ao_key) */
	if (halo_world_ao_prepass_found())
	{
		world_ao.span = WORLD_AO_SPAN_FOUND;
		halo_world_ao_span(WORLD_AO_SPAN_FOUND);
	}
	if (!world_ao.window || config_boolean("debug.world_ao_inline"))
		return 0;
	return halo_world_ao_prepass_begin();
}

void world_ao_prepass_end(short x0, short y0, short x1, short y1, float const camera[4][4])
{
	/* (the previous frame's occlusion reprojected only where one window
	draws a frame, this one and the last: split screen's share the
	backend's texture) */
	halo_world_ao_prepass_end(x0, y0, x1, y1, camera, (float const (*)[4])world_ao.cascades, world_ao_scene.frame,
		world_ao_scene.windows == 1 && world_ao_scene.last_windows == 1 &&
		world_ao_scene.windows_frame == world_ao_scene.last_frame + 1);
}

void world_ao_window_end(void)
{
	world_ao.span = WORLD_AO_SPAN_NONE;
	halo_world_ao_span(WORLD_AO_SPAN_NONE);
	if (!world_ao.window)
		return;
	world_ao.window = 0;
	halo_world_ao_window(NULL, NULL, NULL, -1, NULL, NULL);
}

void world_ao_object_begin(long object_index)
{
	struct world_scene_instance const *instance;
	int owner = WORLD_AO_OWNER_NONE, baked;
	float reference = 1.0f;
	unsigned long rays_owner = 0;
	long index;

	if (!world_ao.window)
		return;
	if (object_index != -1)
	{
		/* its slot (its proxies' [1].w, world_proxies_window's: owners[i]
		is the datum handle of proxies[i]'s object) */
		for (index = 0; index < world_ao.count; index++)
		{
			if (world_ao.owners[index] == object_index)
			{
				owner = (int)world_ao.proxies[index].v[1][3];
				break;
			}
		}
		/* (a static scenery placement the field holds: its own geometry is in
		the field, which a negative reference says: the shaders take its taps
		from the pixel's own depth in it. One without collision geometry (a
		shrub, a fern) is not in the field: its reference is taken as a moving
		object's, since the lightmap its ambient light is sampled from already
		holds what the field finds about it (#781: a shrub by a cliff went
		black)) */
		baked = world_ao.volume && world_ao.fade > 0.0f && world_proxies_source_baked(object_index) &&
			world_proxies_source_solid(object_index);
		instance = world_ao.volume && world_ao.fade > 0.0f && !baked ? world_scene_instance_find(object_index) : NULL;
		if (instance)
			reference = world_ao_reference(world_ao.volume, instance->lighting_point);
		if (baked)
			reference = WORLD_AO_REFERENCE_NO_FIELD;
		/* (its own instances, which its pixels' rays leave out) */
		if (world_ao_mode() & WORLD_AO_RAYS)
			rays_owner = world_rt_owner(object_index);
	}
	halo_world_ao_object(owner, reference, rays_owner);
}

void world_ao_cards(int cards)
{
	if (!world_ao.window)
		return;
	halo_world_ao_cards(cards);
}

void world_ao_lightmap_begin(void const *lightmap_bitmap)
{
	int layer;

	/* (the structure's: none of the objects' shapes its own; its page's
	layer where the lightmap draws take their reference, or none) */
	if (!world_ao.window)
		return;
	layer = world_sdf_scenery_layer(lightmap_bitmap);
	if (layer >= 0)
		world_ao_counts.paged++;
	else
		world_ao_counts.unpaged++;
	halo_world_ao_object(WORLD_AO_OWNER_NONE, (float)layer, 0);
}

void world_ao_first_person(int drawing)
{
	/* (the window's own span again after it) */
	halo_world_ao_span(!drawing ? world_ao.span : world_ao.span == WORLD_AO_SPAN_FOUND ?
		WORLD_AO_SPAN_FOUND_FIRST_PERSON : WORLD_AO_SPAN_FIRST_PERSON);
	if (!world_ao.window)
		return;
	halo_world_ao_object(drawing ? WORLD_AO_OWNER_UNLIT : WORLD_AO_OWNER_NONE, 1.0f, 0);
}
