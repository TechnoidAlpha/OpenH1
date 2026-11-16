/*
WORLD_SCENE.C

display.ambient_occlusion "world"'s scene (world_scene.h, docs/world-ao.md):
the structure's static geometry and the frame's objects under stable ids,
and the world light list. Workstream A.

The static meshes: the loaded structure BSP's collision geometry (its
surfaces, each a ring of edges, fanned into triangles from the ring's first
corner), in world space, as the BSP's collision surfaces are, with each
triangle's collision material. world_scene_static_triangles hands out a
copy, which a bake on another thread reads after the map's tags are gone.
Only the BSP that is loaded has its tags in place; another BSP of the map
is found when the game switches to it (world_scene_bsp_switched).

Each frame (world_scene_frame_begin, while the objects stand where the frame
draws them): the loaded BSP as a static instance, then every object that
may be drawn under its datum handle, which no other object takes while the
map is loaded (a handle's salt), with its root node as drawn this frame and
the frame before, its bounding sphere as drawn, and where its ambient light
was sampled. Then the world light list: the dynamic lights the rasterizer
took the frame before, under their datum handles, and the scenario's light
fixtures in the loaded BSP, under WORLD_SCENE_ID_STATIC_BIT and their index.

debug.world_scene_dump writes each BSP's triangles to the data folder as it
loads, world_scene_<map>_bsp<n>.obj, to look at in any mesh viewer; debug.world_scene_log
logs the counts every 72 frames, and where the first object is drawn.

Nothing here runs while the setting is not "world" (world_ao.c), and
nothing here is read by the simulation.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "memory/data.h"
#include "objects/objects.h"
#include "physics/collision_bsp_definitions.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_lights.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "devices/devices.h"
#include "devices/device_light_fixtures.h"
#include "structures/structure_vector_tests.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "world_scene.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
const char *config_string(const char *name);
/* port/linux/src (halo.log) */
void platform_log(const char *format, ...);
/* port/linux/game/dynamic_lights.c: the light last drawn at an absolute
index, its handle, where and how far it reached (FALSE: none) */
boolean dynamic_lights_drawn_get(long index, long *light, real_point3d *position, real *radius);
/* object_lights.c's lights */
extern struct data_array *light_data;

/* ---------- the static meshes */

static struct
{
	/* the BSPs whose triangles debug.world_scene_dump wrote, a bit each */
	unsigned long dumped;
} world_scene_static;

/* a collision BSP's surfaces fanned into triangles (positions: 9 floats
each, materials: one each; NULL for the count alone): each surface's ring
of edges walked as the game walks it (an edge's right side is the
surface's when its second surface is), its corners fanned from the first.
A surface whose ring leaves the BSP's edges or vertices, or doesn't close
within MAXIMUM_EDGES_PER_COLLISION_SURFACE, is left out. The count */
static long world_scene_fan(
	struct collision_surface const *surfaces, long surface_count,
	struct collision_edge const *edges, long edge_count,
	struct collision_vertex const *vertices, long vertex_count,
	float *positions, short *materials)
{
	long surface_index, count = 0;

	for (surface_index = 0; surface_index < surface_count; surface_index++)
	{
		struct collision_surface const *surface = &surfaces[surface_index];
		long ring[MAXIMUM_EDGES_PER_COLLISION_SURFACE];
		long edge_index = surface->first_edge_index;
		short corners = 0, corner;

		do
		{
			struct collision_edge const *edge;
			short right;

			if (edge_index < 0 || edge_index >= edge_count || corners >= MAXIMUM_EDGES_PER_COLLISION_SURFACE)
			{
				corners = 0;
				break;
			}
			edge = &edges[edge_index];
			right = edge->surface_indices[1] == surface_index;
			if ((!right && edge->surface_indices[0] != surface_index) ||
				edge->vertex_indices[right] < 0 || edge->vertex_indices[right] >= vertex_count)
			{
				corners = 0;
				break;
			}
			ring[corners++] = edge->vertex_indices[right];
			edge_index = edge->edge_indices[right];
		}
		while (edge_index != surface->first_edge_index);
		for (corner = 1; corner + 1 < corners; corner++)
		{
			if (positions)
			{
				long const fan[3] = { ring[0], ring[corner], ring[corner + 1] };
				short vertex;

				for (vertex = 0; vertex < 3; vertex++)
				{
					real_point3d const *point = &vertices[fan[vertex]].point;

					positions[count * 9 + vertex * 3 + 0] = point->x;
					positions[count * 9 + vertex * 3 + 1] = point->y;
					positions[count * 9 + vertex * 3 + 2] = point->z;
				}
				if (materials)
					materials[count] = surface->material_index;
			}
			count++;
		}
	}
	return count;
}

/* the triangles' bounds (zero for none) */
static void world_scene_bounds(long count, float const *positions, float bounds[6])
{
	long index;

	memset(bounds, 0, 6 * sizeof(float));
	for (index = 0; index < count * 3; index++)
	{
		short axis;

		for (axis = 0; axis < 3; axis++)
		{
			float value = positions[index * 3 + axis];

			if (!index || value < bounds[axis])
				bounds[axis] = value;
			if (!index || value > bounds[3 + axis])
				bounds[3 + axis] = value;
		}
	}
}

/* the loaded BSP's collision geometry, if it is bsp_index's (NULL) */
static struct collision_bsp const *world_scene_collision_bsp(short bsp_index)
{
	struct scenario *scenario = global_scenario_try_and_get();

	if (!scenario || bsp_index < 0 || bsp_index >= scenario->structure_bsp_references.count ||
		bsp_index != global_structure_bsp_index || !global_collision_bsp)
	{
		return NULL;
	}
	return global_collision_bsp;
}

int world_scene_static_triangles(short bsp_index, struct world_scene_triangles *triangles)
{
	struct collision_bsp const *bsp = world_scene_collision_bsp(bsp_index);
	struct collision_surface const *surfaces;
	struct collision_edge const *edges;
	struct collision_vertex const *vertices;
	long count;

	memset(triangles, 0, sizeof(*triangles));
	if (!bsp)
		return FALSE;
	surfaces = (struct collision_surface const *)bsp->surfaces.address;
	edges = (struct collision_edge const *)bsp->edges.address;
	vertices = (struct collision_vertex const *)bsp->vertices.address;
	count = world_scene_fan(surfaces, bsp->surfaces.count, edges, bsp->edges.count, vertices, bsp->vertices.count,
		NULL, NULL);
	if (count > 0)
	{
		triangles->positions = malloc((size_t)count * 9 * sizeof(float));
		triangles->materials = malloc((size_t)count * sizeof(short));
		if (!triangles->positions || !triangles->materials)
		{
			world_scene_triangles_free(triangles);
			return FALSE;
		}
		world_scene_fan(surfaces, bsp->surfaces.count, edges, bsp->edges.count, vertices, bsp->vertices.count,
			triangles->positions, triangles->materials);
	}
	triangles->count = count;
	world_scene_bounds(count, triangles->positions, triangles->bounds);
	return TRUE;
}

long world_scene_collision_fan(void const *collision_bsp, float *positions)
{
	struct collision_bsp const *bsp = collision_bsp;

	if (!bsp || !bsp->surfaces.address || !bsp->edges.address || !bsp->vertices.address)
		return 0;
	return world_scene_fan((struct collision_surface const *)bsp->surfaces.address, bsp->surfaces.count,
		(struct collision_edge const *)bsp->edges.address, bsp->edges.count,
		(struct collision_vertex const *)bsp->vertices.address, bsp->vertices.count, positions, NULL);
}

void world_scene_triangles_free(struct world_scene_triangles *triangles)
{
	free(triangles->positions);
	free(triangles->materials);
	triangles->positions = NULL;
	triangles->materials = NULL;
	triangles->count = 0;
}

/* debug.world_scene_dump: the loaded BSP's triangles written to the data
folder as world_scene_<map>_bsp<n>.obj (once while it's loaded), each
collision material a group */
static void world_scene_dump(short bsp_index)
{
	char const *scenario_name, *map_name;
	struct world_scene_triangles triangles;
	char path[1024];
	FILE *file;
	long index;
	short material = -2;

	if (!config_boolean("debug.world_scene_dump") || bsp_index < 0 || bsp_index >= 32 ||
		(world_scene_static.dumped & (1UL << bsp_index)) ||
		!world_scene_static_triangles(bsp_index, &triangles))
	{
		return;
	}
	world_scene_static.dumped |= 1UL << bsp_index;
	scenario_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	map_name = scenario_name ? scenario_name : "map";
	if (strrchr(map_name, '\\'))
		map_name = strrchr(map_name, '\\') + 1;
	/* (a path of the game's: in the data folder) */
	snprintf(path, sizeof(path), "world_scene_%s_bsp%d.obj", map_name, (int)bsp_index);
	file = fopen(path, "w");
	if (file)
	{
		fprintf(file, "# %s bsp %d: %ld collision triangles, world units\n", map_name, (int)bsp_index,
			triangles.count);
		for (index = 0; index < triangles.count * 3; index++)
		{
			fprintf(file, "v %.4f %.4f %.4f\n", (double)triangles.positions[index * 3],
				(double)triangles.positions[index * 3 + 1], (double)triangles.positions[index * 3 + 2]);
		}
		for (index = 0; index < triangles.count; index++)
		{
			if (triangles.materials[index] != material)
			{
				material = triangles.materials[index];
				fprintf(file, "g material_%d\n", (int)material);
			}
			fprintf(file, "f %ld %ld %ld\n", index * 3 + 1, index * 3 + 2, index * 3 + 3);
		}
		fclose(file);
	}
	platform_log("world scene: %s bsp %d: %ld triangles, bounds %.1f %.1f %.1f to %.1f %.1f %.1f%s%s",
		map_name, (int)bsp_index, triangles.count, (double)triangles.bounds[0], (double)triangles.bounds[1],
		(double)triangles.bounds[2], (double)triangles.bounds[3], (double)triangles.bounds[4],
		(double)triangles.bounds[5], file ? ", written to " : ", not written to ", path);
	world_scene_triangles_free(&triangles);
}


/* ---------- the scene */

/* the start of a light's definition (object_lights.c's, and
tag_schema_render.c's light_definition), which the rasterizer's lights
point to: its cone */
struct world_scene_light_definition
{
	unsigned long flags;
	real radius;
	real radius_modifier_lower_bound;
	real radius_modifier_upper_bound;
	real falloff_angle;
	real cutoff_angle;
	real lens_flare_radius;
	real runtime_cosine_falloff_angle;
	real runtime_cosine_cutoff_angle;
};

/* a light's flags (world_scene_light's) */
enum
{
	/* a light fixture of the scenario's, placed in the structure (its light
	is in the lightmaps; no reach is known: radius 0) */
	WORLD_SCENE_LIGHT_FIXTURE = 1 << 0,
	/* a dynamic light, as the rasterizer took it the frame before (the last
	window's) */
	WORLD_SCENE_LIGHT_DYNAMIC = 1 << 1,
};

/* what's kept of an object from frame to frame, by its absolute index */
struct world_scene_history
{
	long id; /* (0: none) */
	unsigned long frame; /* the frame it was last listed in */
	float transform[3][4];
	/* its lighting point, and the bounding sphere's centre it was found
	from (the game's, at the tick: where its ambient is sampled) */
	boolean lit;
	real_point3d lit_from;
	float lighting_point[3];
};

static struct
{
	unsigned long frame;
	/* the loaded BSP (if found): its index, structure_bsp tag, bounding
	sphere and triangles */
	boolean bsp_found;
	short bsp_index;
	long bsp_definition;
	float bsp_sphere[4];
	long bsp_triangles;
	long instance_count;
	struct world_scene_instance instances[WORLD_SCENE_MAXIMUM_INSTANCES];
	long light_count;
	struct world_scene_light lights[WORLD_SCENE_MAXIMUM_LIGHTS];
	/* by an object's absolute index: its instance (valid if that
	instance's id is the object's) and what's kept of it */
	short slots[MAXIMUM_OBJECTS_PER_MAP];
	struct world_scene_history history[MAXIMUM_OBJECTS_PER_MAP];
} world_scene;

/* a node's matrix as a row-major 3x4 (columns: the node's forward, left and
up, scaled, then its position), so that transform * (x y z 1) is a point
of the node's own space (x forward, y left, z up) in the world */
static void world_scene_transform(real_matrix4x3 const *matrix, float transform[3][4])
{
	short row, column;

	for (row = 0; row < 3; row++)
	{
		for (column = 0; column < 3; column++)
			transform[row][column] = matrix->n[column][row] * matrix->scale;
		transform[row][3] = matrix->n[3][row];
	}
}

/* the next instance (NULL past the most): its id and what it is; its
transform this frame and (an object's listed the frame before) last
frame's, else this frame's again */
static struct world_scene_instance *world_scene_instance_add(long id, short kind, short object_type,
	long definition, float const transform[3][4])
{
	struct world_scene_instance *instance;

	if (world_scene.instance_count >= WORLD_SCENE_MAXIMUM_INSTANCES)
		return NULL;
	if (kind == _world_scene_instance_object && DATUM_INDEX_TO_ABSOLUTE_INDEX(id) >= MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	instance = &world_scene.instances[world_scene.instance_count];
	memset(instance, 0, sizeof(*instance));
	instance->id = id;
	instance->kind = kind;
	instance->object_type = object_type;
	instance->definition = definition;
	instance->frame = world_scene.frame;
	memcpy(instance->transform, transform, sizeof(instance->transform));
	memcpy(instance->previous_transform, transform, sizeof(instance->previous_transform));
	if (kind == _world_scene_instance_object)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(id);
		struct world_scene_history *history = &world_scene.history[absolute_index];

		if (history->id != id)
		{
			/* (another object in the slot: nothing of the last one's) */
			memset(history, 0, sizeof(*history));
			history->id = id;
		}
		else if (history->frame + 1 == world_scene.frame)
		{
			memcpy(instance->previous_transform, history->transform, sizeof(instance->previous_transform));
		}
		history->frame = world_scene.frame;
		memcpy(history->transform, transform, sizeof(history->transform));
		world_scene.slots[absolute_index] = (short)world_scene.instance_count;
	}
	world_scene.instance_count++;
	return instance;
}

/* where the object's ambient light is sampled, as
lights_prepare_for_object_static samples it: the lightmap below its
bounding sphere's centre (the game's, at the tick), 10 world units at most
(the centre if there's none); found again only when the centre moves */
static void world_scene_lighting_point(struct world_scene_history *history, real_point3d const *center,
	float lighting_point[3])
{
	if (!history->lit || history->lit_from.x != center->x || history->lit_from.y != center->y ||
		history->lit_from.z != center->z)
	{
		real_vector3d down = { 0.0f, 0.0f, -10.0f };
		real_point3d point;
		short lightmap_index, material_index;
		long surface_index;
		real s, t;

		if (!structure_test_vector(center, &down, &point, &lightmap_index, &material_index, &surface_index, &s, &t))
			point = *center;
		history->lit = TRUE;
		history->lit_from = *center;
		history->lighting_point[0] = point.x;
		history->lighting_point[1] = point.y;
		history->lighting_point[2] = point.z;
	}
	memcpy(lighting_point, history->lighting_point, sizeof(history->lighting_point));
}

/* the loaded BSP as a static instance (an identity transform), first */
static void world_scene_static_instance(void)
{
	static float const identity[3][4] = { { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f } };
	struct world_scene_instance *instance;

	if (!world_scene.bsp_found || !world_scene_collision_bsp(world_scene.bsp_index))
		return;
	instance = world_scene_instance_add(WORLD_SCENE_ID_STATIC_BIT | world_scene.bsp_index,
		_world_scene_instance_static, NONE, world_scene.bsp_definition, identity);
	if (!instance)
		return;
	memcpy(instance->sphere, world_scene.bsp_sphere, sizeof(instance->sphere));
	memcpy(instance->lighting_point, world_scene.bsp_sphere, sizeof(instance->lighting_point));
}

/* every object that may be drawn (not invisible, and in the map or held by
one), as it's drawn: its root node in render_interpolation.c's pose (the
frame is drawn between the ticks), its bounding sphere moved with it */
boolean menu_spartan_object(long object_index); /* (menu_spartan.c) */

static void world_scene_objects(void)
{
	struct object_iterator iterator;

	object_iterator_new(&iterator, _object_mask_all, 0);
	while (object_iterator_next(&iterator))
	{
		long object_index = iterator.index;
		struct object_datum *object = object_get(object_index);
		real_matrix4x3 const *nodes;
		struct world_scene_instance *instance;
		float transform[3][4];
		real_point3d center;
		real radius;

		/* (nor the menus' Spartans, drawn in a view of their own:
		menu_spartan.c) */
		if (TEST_FLAG(object->object.flags, _object_invisible_bit) ||
			(!TEST_FLAG(object->object.flags, _object_connected_to_map_bit) &&
				object->object.parent_object_index == NONE) ||
			menu_spartan_object(object_get_ultimate_parent(object_index)))
		{
			continue;
		}
		nodes = object_get_node_matrices(object_index);
		if (!nodes)
			continue;
		world_scene_transform(&nodes[0], transform);
		instance = world_scene_instance_add(object_index, _world_scene_instance_object, object->object.type,
			object->definition_index, transform);
		if (!instance)
			continue;
		object_get_render_bounding_sphere(object_index, &center, &radius);
		world_scene_lighting_point(&world_scene.history[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)], &center,
			instance->lighting_point);
		render_interpolation_object_point(object_index, &center);
		instance->sphere[0] = center.x;
		instance->sphere[1] = center.y;
		instance->sphere[2] = center.z;
		instance->sphere[3] = radius;
	}
}

/* the next light (NULL past the most): a point light of intensity 1 until
it's said otherwise */
static struct world_scene_light *world_scene_light_add(long id, short flags)
{
	struct world_scene_light *light;

	if (world_scene.light_count >= WORLD_SCENE_MAXIMUM_LIGHTS)
		return NULL;
	light = &world_scene.lights[world_scene.light_count++];
	memset(light, 0, sizeof(*light));
	light->id = id;
	light->flags = flags;
	light->cone_cosine = -1.0f;
	light->intensity = 1.0f;
	return light;
}

/* a key of where a light is drawn and how far it reaches */
static unsigned long world_scene_light_key(real_point3d const *position, real radius)
{
	unsigned long bits[4], key = 2166136261UL;
	short index;

	memcpy(&bits[0], &position->x, sizeof(bits[0]));
	memcpy(&bits[1], &position->y, sizeof(bits[1]));
	memcpy(&bits[2], &position->z, sizeof(bits[2]));
	memcpy(&bits[3], &radius, sizeof(bits[3]));
	for (index = 0; index < 4; index++)
		key = (key ^ (bits[index] & 0xFFFFFFFFUL)) * 16777619UL;
	return key & 0xFFFFFFFFUL;
}

/* the dynamic lights the rasterizer took the frame before (the last
window's, as dynamic_lights_rank ranked them past its room), each under
its datum handle: found by where it was drawn (dynamic_lights_drawn, by its
absolute index), and only one still alive */
static void world_scene_dynamic_lights(void)
{
	enum { HASH_SIZE = 4 * MAXIMUM_LIGHTS_PER_WINDOW };
	short hash[HASH_SIZE];
	long handles[MAXIMUM_LIGHTS_PER_WINDOW];
	long count = rasterizer_lights.light_count, index;

	if (count <= 0)
		return;
	count = MIN(count, MAXIMUM_LIGHTS_PER_WINDOW);
	memset(hash, 0xFF, sizeof(hash));
	for (index = 0; index < count; index++)
	{
		struct rasterizer_light_submit_parameters const *drawn = &rasterizer_lights.lights[index];
		unsigned long key = world_scene_light_key(&drawn->position, drawn->radius) % HASH_SIZE;

		while (hash[key] != NONE)
			key = (key + 1) % HASH_SIZE;
		hash[key] = (short)index;
		handles[index] = NONE;
	}
	for (index = 0; index < MAXIMUM_LIGHTS_PER_MAP; index++)
	{
		real_point3d position;
		real radius;
		long light;
		unsigned long key;

		if (!dynamic_lights_drawn_get(index, &light, &position, &radius) || !datum_try_and_get(light_data, light))
			continue;
		for (key = world_scene_light_key(&position, radius) % HASH_SIZE; hash[key] != NONE; key = (key + 1) % HASH_SIZE)
		{
			struct rasterizer_light_submit_parameters const *drawn = &rasterizer_lights.lights[hash[key]];

			if (drawn->position.x == position.x && drawn->position.y == position.y &&
				drawn->position.z == position.z && drawn->radius == radius)
			{
				handles[hash[key]] = light;
				break;
			}
		}
	}
	for (index = 0; index < count; index++)
	{
		struct rasterizer_light_submit_parameters const *drawn = &rasterizer_lights.lights[index];
		struct world_scene_light_definition const *definition =
			(struct world_scene_light_definition const *)drawn->definition;
		struct world_scene_light *light;

		if (handles[index] == NONE)
			continue;
		light = world_scene_light_add(handles[index], WORLD_SCENE_LIGHT_DYNAMIC);
		if (!light)
			return;
		light->position[0] = drawn->position.x;
		light->position[1] = drawn->position.y;
		light->position[2] = drawn->position.z;
		light->radius = drawn->radius;
		light->direction[0] = drawn->forward.i;
		light->direction[1] = drawn->forward.j;
		light->direction[2] = drawn->forward.k;
		light->color[0] = drawn->color.red;
		light->color[1] = drawn->color.green;
		light->color[2] = drawn->color.blue;
		/* (a cone where object_lights.c finds one: a falloff angle given) */
		if (definition && definition->runtime_cosine_falloff_angle != -1.0f)
		{
			light->type = 1;
			light->cone_cosine = definition->runtime_cosine_cutoff_angle;
		}
	}
}

/* the scenario's light fixtures in the loaded BSP (placed in it, as
object_types.c finds them), each under WORLD_SCENE_ID_STATIC_BIT and its
index in the scenario: where it's placed, its colour, intensity and cone */
static void world_scene_fixtures(void)
{
	struct scenario *scenario = global_scenario_try_and_get();
	long index;

	if (!scenario || !world_scene.bsp_found)
		return;
	for (index = 0; index < scenario->light_fixtures.count; index++)
	{
		struct scenario_light_fixture_datum const *fixture =
			TAG_BLOCK_GET_ELEMENT(&scenario->light_fixtures, index, struct scenario_light_fixture_datum);
		struct world_scene_light *light;
		real_matrix4x3 matrix;

		if (!TEST_FLAG(fixture->object.on_bsp_flags, world_scene.bsp_index))
			continue;
		light = world_scene_light_add(WORLD_SCENE_ID_STATIC_BIT | index, WORLD_SCENE_LIGHT_FIXTURE);
		if (!light)
			return;
		matrix4x3_rotation_from_angles(&matrix, fixture->object.rotation.yaw, fixture->object.rotation.pitch,
			fixture->object.rotation.roll);
		light->position[0] = fixture->object.position.x;
		light->position[1] = fixture->object.position.y;
		light->position[2] = fixture->object.position.z;
		light->direction[0] = matrix.forward.i;
		light->direction[1] = matrix.forward.j;
		light->direction[2] = matrix.forward.k;
		light->color[0] = fixture->color.red;
		light->color[1] = fixture->color.green;
		light->color[2] = fixture->color.blue;
		light->intensity = fixture->intensity;
		if (fixture->cutoff_angle > 0.0f && fixture->cutoff_angle < 3.14159265f)
		{
			light->type = 1;
			light->cone_cosine = (float)cos((double)fixture->cutoff_angle);
		}
	}
}

/* debug.world_scene_log: the counts every 72 frames, and for the 8 frames
after, where the first object listed is drawn this frame and last (a step
each frame, not each tick) */
static void world_scene_log(void)
{
	long counts[NUMBER_OF_WORLD_SCENE_INSTANCE_KINDS] = { 0 };
	long fixtures = 0, index;
	struct world_scene_instance const *first = NULL;

	if (world_scene.frame % 72 >= 8 || !config_boolean("debug.world_scene_log"))
		return;
	for (index = 0; index < world_scene.instance_count; index++)
	{
		struct world_scene_instance const *instance = &world_scene.instances[index];

		counts[instance->kind]++;
		if (!first && instance->kind == _world_scene_instance_object)
			first = instance;
	}
	if (world_scene.frame % 72 == 0)
	{
		for (index = 0; index < world_scene.light_count; index++)
			fixtures += (world_scene.lights[index].flags & WORLD_SCENE_LIGHT_FIXTURE) != 0;
		platform_log("world scene: frame %lu: bsp %d (%ld triangles), %ld static, %ld objects; %ld lights "
			"(%ld dynamic of the rasterizer's %ld, %ld fixtures)", world_scene.frame, (int)world_scene.bsp_index,
			world_scene.bsp_triangles, counts[_world_scene_instance_static], counts[_world_scene_instance_object],
			world_scene.light_count, world_scene.light_count - fixtures, rasterizer_lights.light_count, fixtures);
	}
	if (first)
	{
		platform_log("world scene: frame %lu: object %08lx (type %d) drawn at %.4f %.4f %.4f, last frame %.4f %.4f "
			"%.4f; lit from %.2f %.2f %.2f", world_scene.frame, (unsigned long)first->id, (int)first->object_type,
			(double)first->transform[0][3], (double)first->transform[1][3], (double)first->transform[2][3],
			(double)first->previous_transform[0][3], (double)first->previous_transform[1][3],
			(double)first->previous_transform[2][3], (double)first->lighting_point[0],
			(double)first->lighting_point[1], (double)first->lighting_point[2]);
	}
}

/* ---------- the map */

/* the loaded BSP found: its tag, its bounding sphere (its collision
vertices'), its triangles counted, and debug.world_scene_dump's file */
static void world_scene_bsp_found(short bsp_index)
{
	struct collision_bsp const *bsp = world_scene_collision_bsp(bsp_index);
	struct collision_vertex const *vertices;
	float bounds[6] = { 0.0f };
	long index;

	world_scene.bsp_found = FALSE;
	world_scene.bsp_triangles = 0;
	if (!bsp)
		return;
	vertices = (struct collision_vertex const *)bsp->vertices.address;
	for (index = 0; index < bsp->vertices.count; index++)
	{
		float const point[3] = { vertices[index].point.x, vertices[index].point.y, vertices[index].point.z };
		short axis;

		for (axis = 0; axis < 3; axis++)
		{
			if (!index || point[axis] < bounds[axis])
				bounds[axis] = point[axis];
			if (!index || point[axis] > bounds[3 + axis])
				bounds[3 + axis] = point[axis];
		}
	}
	world_scene.bsp_found = TRUE;
	world_scene.bsp_index = bsp_index;
	world_scene.bsp_definition = global_structure_bsp_tag_index_get();
	world_scene.bsp_sphere[0] = 0.5f * (bounds[0] + bounds[3]);
	world_scene.bsp_sphere[1] = 0.5f * (bounds[1] + bounds[4]);
	world_scene.bsp_sphere[2] = 0.5f * (bounds[2] + bounds[5]);
	world_scene.bsp_sphere[3] = 0.5f * (float)sqrt((double)((bounds[3] - bounds[0]) * (bounds[3] - bounds[0]) +
		(bounds[4] - bounds[1]) * (bounds[4] - bounds[1]) + (bounds[5] - bounds[2]) * (bounds[5] - bounds[2])));
	world_scene.bsp_triangles = world_scene_fan((struct collision_surface const *)bsp->surfaces.address,
		bsp->surfaces.count, (struct collision_edge const *)bsp->edges.address, bsp->edges.count, vertices,
		bsp->vertices.count, NULL, NULL);
	world_scene_dump(bsp_index);
}

/* every instance and light forgotten, and what's kept of each object (an
id is never another object's while it might be confused with it: a datum
handle's salt) */
static void world_scene_forget(void)
{
	world_scene.instance_count = 0;
	world_scene.light_count = 0;
	world_scene.bsp_found = FALSE;
	world_scene.bsp_triangles = 0;
	memset(world_scene.history, 0, sizeof(world_scene.history));
}

void world_scene_map_loaded(void)
{
	world_scene_forget();
	world_scene_bsp_found(global_structure_bsp_index);
}

void world_scene_map_unloaded(void)
{
	world_scene_forget();
	world_scene_static.dumped = 0;
}

void world_scene_bsp_switched(short bsp_index)
{
	world_scene_bsp_found(bsp_index);
}

/* ---------- the frame */

void world_scene_frame_begin(void)
{
	world_scene.frame++;
	world_scene.instance_count = 0;
	world_scene.light_count = 0;
	if (!world_scene.bsp_found)
		return;
	world_scene_static_instance();
	world_scene_objects();
	world_scene_dynamic_lights();
	world_scene_fixtures();
	world_scene_log();
}

long world_scene_instance_count(void)
{
	return world_scene.instance_count;
}

struct world_scene_instance const *world_scene_instances(void)
{
	return world_scene.instance_count ? world_scene.instances : NULL;
}

struct world_scene_instance const *world_scene_instance_find(long id)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(id);
	short slot;

	if (id == WORLD_SCENE_ID_NONE)
		return NULL;
	/* (a static mesh's, which is listed first; an object's datum handle
	is negative: its salt's high bit) */
	if (id >= 0)
		return world_scene.instance_count && world_scene.instances[0].id == id ? &world_scene.instances[0] : NULL;
	if (absolute_index >= MAXIMUM_OBJECTS_PER_MAP)
		return NULL;
	slot = world_scene.slots[absolute_index];
	return slot >= 0 && slot < world_scene.instance_count && world_scene.instances[slot].id == id ?
		&world_scene.instances[slot] : NULL;
}

long world_scene_light_count(void)
{
	return world_scene.light_count;
}

struct world_scene_light const *world_scene_lights(void)
{
	return world_scene.light_count ? world_scene.lights : NULL;
}
