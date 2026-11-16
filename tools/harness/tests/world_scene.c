/* display.ambient_occlusion "world"'s scene (port/linux/game/world_scene.c,
test_world_scene.py): the whole file compiled against a small fake game (its
game headers left out). The empty case; the static triangles' copy of a
hand-built collision BSP; ids stable over frames and never another live
object's; an object's instance following its node as drawn between the
ticks; the light list. */

#include "harness.h"

#include <math.h>
#include <stdarg.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

enum
{
	MAXIMUM_OBJECTS_PER_MAP = 64,
	MAXIMUM_LIGHTS_PER_WINDOW = 8,
	MAXIMUM_LIGHTS_PER_MAP = 32,
};

typedef struct { real red, green, blue; } real_rgb_color;
typedef struct { real yaw, pitch, roll; } real_euler_angles3d;
typedef struct real_matrix4x3
{
	real scale;
	union
	{
		real n[4][3];
		struct
		{
			real_vector3d forward;
			real_vector3d left;
			real_vector3d up;
			real_point3d position;
		};
	};
} real_matrix4x3;

struct tag_block
{
	long count;
	void *address;
	void *definition;
};

#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])

/* (the collision BSP's elements and limits, and the objects' flags, from
the game's headers) */
#include "game.inc"

/* only what world_scene.c reads of them */
struct collision_bsp
{
	struct tag_block surfaces, edges, vertices;
};

struct scenario
{
	struct tag_block structure_bsp_references;
	struct tag_block light_fixtures;
};

struct scenario_light_fixture_datum
{
	struct
	{
		real_point3d position;
		real_euler_angles3d rotation;
		unsigned short on_bsp_flags;
	} object;
	real_rgb_color color;
	real intensity;
	real falloff_angle;
	real cutoff_angle;
};

struct scenario *fake_scenario;
struct collision_bsp *global_collision_bsp;
short global_structure_bsp_index = NONE;
long global_scenario_index = NONE;

struct scenario *global_scenario_try_and_get(void)
{
	return fake_scenario;
}

long global_structure_bsp_tag_index_get(void)
{
	return 0x7001;
}

char *tag_get_name(long tag_index)
{
	(void)tag_index;
	return "levels\\test\\fake\\fake";
}

int config_boolean(const char *name)
{
	(void)name;
	return 0;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

void platform_log(const char *format, ...)
{
	(void)format;
}

/* the objects: each its handle, whether it's alive, its datum, its node
as the tick left it and as the frame draws it */
struct object_datum
{
	long definition_index;
	struct
	{
		unsigned long flags;
		short type;
		long parent_object_index;
		real_point3d bounding_sphere_center;
		real bounding_sphere_radius;
	} object;
};

struct object_iterator
{
	long index;
	short absolute_index;
};

enum { FAKE_OBJECTS = 8 };

static struct
{
	long handle;
	boolean alive;
	struct object_datum datum;
	real_matrix4x3 tick_node;
	real_matrix4x3 drawn_node;
} fake_objects[FAKE_OBJECTS];
static unsigned short fake_salt = 0x8000;
static long fake_raycasts;

static long fake_object_new(short absolute_index, short type, real x, real y, real z)
{
	long handle = (long)(((unsigned long)++fake_salt << 16) | (unsigned long)absolute_index);

	memset(&fake_objects[absolute_index], 0, sizeof(fake_objects[absolute_index]));
	fake_objects[absolute_index].handle = handle;
	fake_objects[absolute_index].alive = TRUE;
	fake_objects[absolute_index].datum.definition_index = 0x100 + type;
	fake_objects[absolute_index].datum.object.flags = FLAG(_object_connected_to_map_bit);
	fake_objects[absolute_index].datum.object.type = type;
	fake_objects[absolute_index].datum.object.parent_object_index = NONE;
	fake_objects[absolute_index].datum.object.bounding_sphere_center.x = x;
	fake_objects[absolute_index].datum.object.bounding_sphere_center.y = y;
	fake_objects[absolute_index].datum.object.bounding_sphere_center.z = z + 0.5f;
	fake_objects[absolute_index].datum.object.bounding_sphere_radius = 0.7f;
	fake_objects[absolute_index].tick_node.scale = 1.0f;
	fake_objects[absolute_index].tick_node.forward.i = 1.0f;
	fake_objects[absolute_index].tick_node.left.j = 1.0f;
	fake_objects[absolute_index].tick_node.up.k = 1.0f;
	fake_objects[absolute_index].tick_node.position.x = x;
	fake_objects[absolute_index].tick_node.position.y = y;
	fake_objects[absolute_index].tick_node.position.z = z;
	fake_objects[absolute_index].drawn_node = fake_objects[absolute_index].tick_node;
	return handle;
}

static void object_iterator_new(struct object_iterator *iterator, unsigned long type_flags, byte flags)
{
	(void)type_flags;
	(void)flags;
	iterator->absolute_index = 0;
	iterator->index = NONE;
}

static void *object_iterator_next(struct object_iterator *iterator)
{
	while (iterator->absolute_index < FAKE_OBJECTS)
	{
		short absolute_index = iterator->absolute_index++;

		if (fake_objects[absolute_index].alive)
		{
			iterator->index = fake_objects[absolute_index].handle;
			return &fake_objects[absolute_index].datum;
		}
	}
	return NULL;
}

static struct object_datum *object_get(long object_index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	CHECK(absolute_index < FAKE_OBJECTS && fake_objects[absolute_index].alive &&
		fake_objects[absolute_index].handle == object_index, "object_get of %lx", (unsigned long)object_index);
	return &fake_objects[absolute_index].datum;
}

/* (as the frame draws it) */
static real_matrix4x3 *object_get_node_matrices(long object_index)
{
	object_get(object_index);
	return &fake_objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index)].drawn_node;
}

static void object_get_render_bounding_sphere(long object_index, real_point3d *center, real *radius)
{
	struct object_datum *object = object_get(object_index);

	*center = object->object.bounding_sphere_center;
	*radius = object->object.bounding_sphere_radius * 2.0f;
}

/* (a parent the case deleted from under its child, as the game never does:
the child's own index) */
static long object_get_ultimate_parent(long object_index)
{
	for (;;)
	{
		long parent = object_get(object_index)->object.parent_object_index;
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(parent);

		if (parent == NONE || absolute_index >= FAKE_OBJECTS || !fake_objects[absolute_index].alive ||
			fake_objects[absolute_index].handle != parent)
		{
			return object_index;
		}
		object_index = parent;
	}
}

/* (the menus' Spartan, drawn in a view of its own: menu_spartan.c) */
static long fake_menu_spartan = NONE;

boolean menu_spartan_object(long object_index)
{
	return object_index != NONE && object_index == fake_menu_spartan;
}

/* (moved with the root node from where the tick left it to where it's drawn) */
static void render_interpolation_object_point(long object_index, real_point3d *point)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);

	point->x += fake_objects[absolute_index].drawn_node.position.x - fake_objects[absolute_index].tick_node.position.x;
	point->y += fake_objects[absolute_index].drawn_node.position.y - fake_objects[absolute_index].tick_node.position.y;
	point->z += fake_objects[absolute_index].drawn_node.position.z - fake_objects[absolute_index].tick_node.position.z;
}

/* (the level's floor at z 0, a vector going down meeting it if it reaches) */
static boolean structure_test_vector(real_point3d const *point, real_vector3d const *vector,
	real_point3d *collision_point, short *lightmap_index, short *material_index, long *surface_index, real *s, real *t)
{
	(void)lightmap_index;
	(void)material_index;
	(void)surface_index;
	(void)s;
	(void)t;
	fake_raycasts++;
	if (!(vector->k < 0.0f) || point->z + vector->k > 0.0f)
		return FALSE;
	collision_point->x = point->x;
	collision_point->y = point->y;
	collision_point->z = 0.0f;
	return TRUE;
}

/* the lights: the rasterizer's, those drawn (by absolute index) and which
are alive */
struct rasterizer_light_submit_parameters
{
	void *definition;
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	real_rgb_color color;
	real radius;
};

struct
{
	long light_count;
	struct rasterizer_light_submit_parameters lights[MAXIMUM_LIGHTS_PER_WINDOW];
} rasterizer_lights;

static struct
{
	long light;
	real_point3d position;
	real radius;
} fake_lights_drawn[MAXIMUM_LIGHTS_PER_MAP];
static long fake_lights_alive[MAXIMUM_LIGHTS_PER_MAP];
static struct data_array fake_light_data;
struct data_array *light_data = &fake_light_data;

boolean dynamic_lights_drawn_get(long index, long *light, real_point3d *position, real *radius)
{
	if (index < 0 || index >= MAXIMUM_LIGHTS_PER_MAP || !fake_lights_drawn[index].light)
		return FALSE;
	*light = fake_lights_drawn[index].light;
	*position = fake_lights_drawn[index].position;
	*radius = fake_lights_drawn[index].radius;
	return TRUE;
}

void *datum_try_and_get(struct data_array *data, long index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);

	CHECK(data == &fake_light_data, "the lights' data");
	return absolute_index < MAXIMUM_LIGHTS_PER_MAP && fake_lights_alive[absolute_index] == index ?
		&fake_lights_alive[absolute_index] : NULL;
}

static void matrix4x3_rotation_from_angles(real_matrix4x3 *matrix, real yaw, real pitch, real roll)
{
	(void)roll;
	memset(matrix, 0, sizeof(*matrix));
	matrix->scale = 1.0f;
	matrix->forward.i = cosf(yaw) * cosf(pitch);
	matrix->forward.j = sinf(yaw) * cosf(pitch);
	matrix->forward.k = sinf(pitch);
}

#include "under_test.inc"

static void empty_case(void)
{
	struct world_scene_triangles triangles;

	world_scene_map_loaded();
	world_scene_frame_begin();
	CHECK(world_scene_instance_count() == 0, "instances");
	CHECK(world_scene_instance_find(WORLD_SCENE_ID_STATIC_BIT) == NULL, "found");
	CHECK(world_scene_light_count() == 0, "lights");
	CHECK(!world_scene_static_triangles(0, &triangles) && triangles.count == 0, "triangles");
	world_scene_triangles_free(&triangles);
	world_scene_map_unloaded();
}

/* A hand-built collision BSP: a square ABCD (surface 0, material 3) and a
triangle DCE above it sharing the edge CD (surface 1, material 5), each a
ring of edges as the game stores them; and a surface whose ring leaves the
BSP's edges (left out). */
static struct collision_vertex fake_vertices[5];
static struct collision_edge fake_edges[6];
static struct collision_surface fake_surfaces[3];
static struct collision_bsp fake_bsp;
static struct scenario fake_scenario_storage;

static void fake_edge(long index, long v0, long v1, long next0, long next1, long s0, long s1)
{
	struct collision_edge *edge = &fake_edges[index];

	edge->vertex_indices[0] = v0;
	edge->vertex_indices[1] = v1;
	edge->edge_indices[0] = next0;
	edge->edge_indices[1] = next1;
	edge->surface_indices[0] = s0;
	edge->surface_indices[1] = s1;
}

static void fake_map(void)
{
	static float const points[5][3] = {
		{ 10.0f, 20.0f, 1.0f }, /* A */
		{ 11.0f, 20.0f, 1.0f }, /* B */
		{ 11.0f, 21.0f, 1.0f }, /* C */
		{ 10.0f, 21.0f, 1.0f }, /* D */
		{ 10.5f, 22.0f, 1.5f }, /* E */
	};
	long index;

	for (index = 0; index < 5; index++)
	{
		fake_vertices[index].point.x = points[index][0];
		fake_vertices[index].point.y = points[index][1];
		fake_vertices[index].point.z = points[index][2];
		fake_vertices[index].first_edge_index = 0;
	}
	/* the square: A B C D (each edge's left side, 0) */
	fake_edge(0, 0, 1, 1, NONE, 0, NONE); /* AB */
	fake_edge(1, 1, 2, 2, NONE, 0, NONE); /* BC */
	fake_edge(2, 2, 3, 3, 4, 0, 1); /* CD: the square's left, the triangle's right (from D) */
	fake_edge(3, 3, 0, 0, NONE, 0, NONE); /* DA */
	/* the triangle: D (CD's right side) C E */
	fake_edge(4, 2, 4, 5, NONE, 1, NONE); /* from C */
	fake_edge(5, 4, 3, 2, NONE, 1, NONE); /* from E */
	memset(fake_surfaces, 0, sizeof(fake_surfaces));
	fake_surfaces[0].first_edge_index = 0;
	fake_surfaces[0].material_index = 3;
	fake_surfaces[1].first_edge_index = 2;
	fake_surfaces[1].material_index = 5;
	fake_surfaces[2].first_edge_index = 77;
	fake_surfaces[2].material_index = 9;
	fake_bsp.surfaces.count = 3;
	fake_bsp.surfaces.address = fake_surfaces;
	fake_bsp.edges.count = 6;
	fake_bsp.edges.address = fake_edges;
	fake_bsp.vertices.count = 5;
	fake_bsp.vertices.address = fake_vertices;
	fake_scenario_storage.structure_bsp_references.count = 2;
	fake_scenario = &fake_scenario_storage;
	global_collision_bsp = &fake_bsp;
	global_structure_bsp_index = 0;
	global_scenario_index = 1;
}

static int same_point(float const *position, long vertex)
{
	return position[0] == fake_vertices[vertex].point.x && position[1] == fake_vertices[vertex].point.y &&
		position[2] == fake_vertices[vertex].point.z;
}

static void triangles_case(void)
{
	static long const expected[3][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 3, 2, 4 } };
	static short const materials[3] = { 3, 3, 5 };
	struct world_scene_triangles triangles;
	long index;

	fake_map();
	world_scene_map_loaded();
	CHECK(world_scene_static_triangles(0, &triangles), "the loaded bsp");
	CHECK(triangles.count == 3, "%ld triangles", triangles.count);
	for (index = 0; index < 3; index++)
	{
		short corner;

		for (corner = 0; corner < 3; corner++)
		{
			CHECK(same_point(&triangles.positions[index * 9 + corner * 3], expected[index][corner]),
				"triangle %ld corner %d: %g %g %g", index, corner, triangles.positions[index * 9 + corner * 3],
				triangles.positions[index * 9 + corner * 3 + 1], triangles.positions[index * 9 + corner * 3 + 2]);
		}
		CHECK(triangles.materials[index] == materials[index], "triangle %ld material %d", index,
			triangles.materials[index]);
	}
	CHECK(triangles.bounds[0] == 10.0f && triangles.bounds[1] == 20.0f && triangles.bounds[2] == 1.0f &&
		triangles.bounds[3] == 11.0f && triangles.bounds[4] == 22.0f && triangles.bounds[5] == 1.5f,
		"bounds %g %g %g %g %g %g", triangles.bounds[0], triangles.bounds[1], triangles.bounds[2],
		triangles.bounds[3], triangles.bounds[4], triangles.bounds[5]);
	/* a copy: the tags changed (or gone) leave it as it was */
	fake_vertices[0].point.x = 99.0f;
	CHECK(triangles.positions[0] == 10.0f, "a copy");
	world_scene_triangles_free(&triangles);
	CHECK(!triangles.positions && !triangles.materials && !triangles.count, "freed");
	/* another bsp than the one loaded, one the map doesn't have, none */
	CHECK(!world_scene_static_triangles(1, &triangles) && !triangles.count && !triangles.positions, "bsp 1");
	CHECK(!world_scene_static_triangles(2, &triangles) && !triangles.count, "bsp 2");
	CHECK(!world_scene_static_triangles(-1, &triangles) && !triangles.count, "bsp -1");
	world_scene_map_unloaded();
}

static struct world_scene_instance const *found(long id)
{
	struct world_scene_instance const *instance = world_scene_instance_find(id);

	CHECK(instance && instance->id == id, "%lx not found", (unsigned long)id);
	return instance;
}

static int same_transform(float const a[3][4], float const b[3][4])
{
	return !memcmp(a, b, 12 * sizeof(float));
}

static void ids_case(void)
{
	long first, second, third, hidden, held, outside, rifle;
	struct world_scene_instance const *instance;
	float second_transform[3][4];

	fake_map();
	world_scene_map_loaded();
	first = fake_object_new(0, 0, 1.0f, 2.0f, 3.0f);
	second = fake_object_new(1, 1, 5.0f, 5.0f, 1.0f);
	hidden = fake_object_new(2, 2, 0.0f, 0.0f, 1.0f);
	fake_objects[2].datum.object.flags |= FLAG(_object_invisible_bit);
	held = fake_object_new(3, 2, 1.0f, 2.0f, 3.5f);
	fake_objects[3].datum.object.flags = 0;
	fake_objects[3].datum.object.parent_object_index = first;
	outside = fake_object_new(4, 6, 0.0f, 0.0f, -50.0f);
	fake_objects[4].datum.object.flags = 0;
	/* (the menus' Spartan and the rifle it holds: not the scene's) */
	fake_menu_spartan = fake_object_new(5, 0, 0.0f, 3.0f, 1.0f);
	rifle = fake_object_new(6, 2, 0.0f, 3.0f, 1.5f);
	fake_objects[6].datum.object.flags = 0;
	fake_objects[6].datum.object.parent_object_index = fake_menu_spartan;
	world_scene_frame_begin();
	/* the BSP first, then the objects that may be drawn */
	CHECK(world_scene_instance_count() == 4, "%ld instances", world_scene_instance_count());
	instance = found(WORLD_SCENE_ID_STATIC_BIT | 0);
	CHECK(instance == &world_scene_instances()[0] && instance->kind == _world_scene_instance_static &&
		instance->definition == 0x7001 && instance->object_type == NONE, "the bsp");
	CHECK(instance->transform[0][0] == 1.0f && instance->transform[1][1] == 1.0f &&
		instance->transform[2][2] == 1.0f && instance->transform[0][3] == 0.0f, "identity");
	CHECK(fabsf(instance->sphere[0] - 10.5f) < 1e-5f && fabsf(instance->sphere[1] - 21.0f) < 1e-5f &&
		instance->sphere[3] > 1.0f, "the bsp's sphere");
	instance = found(first);
	CHECK(instance->kind == _world_scene_instance_object && instance->object_type == 0 &&
		instance->definition == 0x100, "the first");
	found(second);
	found(held);
	CHECK(!world_scene_instance_find(hidden) && !world_scene_instance_find(outside), "not drawn");
	CHECK(!world_scene_instance_find(fake_menu_spartan) && !world_scene_instance_find(rifle), "the menus' Spartan");
	memcpy(second_transform, found(second)->transform, sizeof(second_transform));

	/* the first deleted, another made in its slot: a new id; the second's
	the same */
	fake_objects[0].alive = FALSE;
	third = fake_object_new(0, 0, 40.0f, 40.0f, 0.0f);
	CHECK(third != first && DATUM_INDEX_TO_ABSOLUTE_INDEX(third) == DATUM_INDEX_TO_ABSOLUTE_INDEX(first), "the slot");
	world_scene_frame_begin();
	CHECK(!world_scene_instance_find(first), "the first's id found again");
	instance = found(third);
	CHECK(same_transform(instance->previous_transform, instance->transform), "the first's transform carried over");
	CHECK(found(second)->frame == world_scene.frame && same_transform(found(second)->transform, second_transform),
		"the second");
	CHECK(world_scene_instance_count() == 4, "%ld instances", world_scene_instance_count());
	/* none past the map */
	world_scene_map_unloaded();
	CHECK(!world_scene_instance_count() && !world_scene_instance_find(second), "unloaded");
	fake_menu_spartan = NONE;
}

/* the tick's pose and the next, drawn a fraction of the way */
static void fake_draw(long absolute_index, real_point3d const *from, real_point3d const *to, real fraction)
{
	fake_objects[absolute_index].drawn_node.position.x = from->x + (to->x - from->x) * fraction;
	fake_objects[absolute_index].drawn_node.position.y = from->y + (to->y - from->y) * fraction;
	fake_objects[absolute_index].drawn_node.position.z = from->z + (to->z - from->z) * fraction;
}

static void follows_case(void)
{
	real_point3d const from = { 2.0f, 0.0f, 1.0f }, to = { 3.0f, 0.5f, 1.0f };
	float last[3][4];
	long object;
	short frame;

	fake_map();
	world_scene_map_loaded();
	object = fake_object_new(0, 0, from.x, from.y, from.z);
	/* turned a quarter left and twice the size: x forward goes to +y */
	fake_objects[0].drawn_node.scale = 2.0f;
	fake_objects[0].drawn_node.forward.i = 0.0f;
	fake_objects[0].drawn_node.forward.j = 1.0f;
	fake_objects[0].drawn_node.left.i = -1.0f;
	fake_objects[0].drawn_node.left.j = 0.0f;
	fake_raycasts = 0;
	for (frame = 0; frame < 4; frame++)
	{
		struct world_scene_instance const *instance;
		real fraction = 0.25f * frame;

		fake_draw(0, &from, &to, fraction);
		world_scene_frame_begin();
		instance = found(object);
		CHECK(instance->transform[0][3] == fake_objects[0].drawn_node.position.x &&
			instance->transform[1][3] == fake_objects[0].drawn_node.position.y &&
			instance->transform[2][3] == fake_objects[0].drawn_node.position.z, "frame %d: drawn at %g %g %g",
			frame, instance->transform[0][3], instance->transform[1][3], instance->transform[2][3]);
		/* (a point 1 forward of the node: 2 along +y) */
		CHECK(instance->transform[0][0] == 0.0f && instance->transform[1][0] == 2.0f &&
			instance->transform[0][1] == -2.0f && instance->transform[2][2] == 2.0f, "frame %d: the axes", frame);
		CHECK(same_transform(instance->previous_transform, frame ? last : instance->transform),
			"frame %d: last frame's %g", frame, instance->previous_transform[0][3]);
		/* the bounding sphere moved with it, from the tick's */
		CHECK(fabsf(instance->sphere[0] - (from.x + (to.x - from.x) * fraction)) < 1e-6f &&
			fabsf(instance->sphere[2] - (from.z + 0.5f)) < 1e-6f && instance->sphere[3] == 1.4f,
			"frame %d: sphere %g %g %g %g", frame, instance->sphere[0], instance->sphere[1], instance->sphere[2],
			instance->sphere[3]);
		/* lit from the floor below the tick's centre, found once */
		CHECK(instance->lighting_point[0] == from.x && instance->lighting_point[1] == from.y &&
			instance->lighting_point[2] == 0.0f, "frame %d: lit from %g %g %g", frame, instance->lighting_point[0],
			instance->lighting_point[1], instance->lighting_point[2]);
		memcpy(last, instance->transform, sizeof(last));
	}
	CHECK(fake_raycasts == 1, "%ld raycasts", fake_raycasts);
	/* the next tick: lit from below its new centre */
	fake_objects[0].datum.object.bounding_sphere_center.x = to.x;
	fake_objects[0].tick_node.position = to;
	world_scene_frame_begin();
	CHECK(fake_raycasts == 2 && found(object)->lighting_point[0] == to.x, "the next tick's");
	/* not listed for a frame: no last frame's of a frame long ago */
	fake_objects[0].datum.object.flags |= FLAG(_object_invisible_bit);
	world_scene_frame_begin();
	fake_objects[0].datum.object.flags &= ~FLAG(_object_invisible_bit);
	fake_objects[0].drawn_node.position.x += 1.0f;
	world_scene_frame_begin();
	CHECK(same_transform(found(object)->previous_transform, found(object)->transform), "a frame missed");
	world_scene_map_unloaded();
}

static void lights_case(void)
{
	static struct world_scene_light_definition spot = { 0, 1.0f, 0.0f, 0.0f, 0.5f, 0.6f, 0.0f, 0.9f, 0.8f };
	static struct world_scene_light_definition omni = { 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f };
	struct scenario_light_fixture_datum fixtures[2];
	struct world_scene_light const *lights;
	long alive = (long)0x80010003, dead = (long)0x80020005;

	fake_map();
	world_scene_map_loaded();
	/* the rasterizer's three: a live spot, a dead light's, one with no
	handle known */
	memset(&rasterizer_lights, 0, sizeof(rasterizer_lights));
	rasterizer_lights.light_count = 3;
	rasterizer_lights.lights[0].definition = &spot;
	rasterizer_lights.lights[0].position.x = 4.0f;
	rasterizer_lights.lights[0].radius = 3.0f;
	rasterizer_lights.lights[0].forward.k = -1.0f;
	rasterizer_lights.lights[0].color.red = 0.5f;
	rasterizer_lights.lights[1].definition = &omni;
	rasterizer_lights.lights[1].position.y = 7.0f;
	rasterizer_lights.lights[1].radius = 2.0f;
	rasterizer_lights.lights[2].definition = &omni;
	rasterizer_lights.lights[2].position.z = 9.0f;
	rasterizer_lights.lights[2].radius = 1.0f;
	memset(fake_lights_drawn, 0, sizeof(fake_lights_drawn));
	fake_lights_drawn[3].light = alive;
	fake_lights_drawn[3].position = rasterizer_lights.lights[0].position;
	fake_lights_drawn[3].radius = 3.0f;
	fake_lights_drawn[5].light = dead;
	fake_lights_drawn[5].position = rasterizer_lights.lights[1].position;
	fake_lights_drawn[5].radius = 2.0f;
	fake_lights_alive[3] = alive;
	/* two fixtures, the first in BSP 0 */
	memset(fixtures, 0, sizeof(fixtures));
	fixtures[0].object.on_bsp_flags = FLAG(0);
	fixtures[0].object.position.x = 1.0f;
	fixtures[0].color.green = 1.0f;
	fixtures[0].intensity = 2.0f;
	fixtures[0].cutoff_angle = 1.0f;
	fixtures[1].object.on_bsp_flags = FLAG(1);
	fake_scenario_storage.light_fixtures.count = 2;
	fake_scenario_storage.light_fixtures.address = fixtures;
	world_scene_frame_begin();
	lights = world_scene_lights();
	CHECK(world_scene_light_count() == 2, "%ld lights", world_scene_light_count());
	CHECK(lights[0].id == alive && lights[0].type == 1 && lights[0].cone_cosine == 0.8f &&
		lights[0].position[0] == 4.0f && lights[0].radius == 3.0f && lights[0].direction[2] == -1.0f &&
		lights[0].color[0] == 0.5f && (lights[0].flags & WORLD_SCENE_LIGHT_DYNAMIC), "the live light");
	CHECK(lights[1].id == (WORLD_SCENE_ID_STATIC_BIT | 0) && (lights[1].flags & WORLD_SCENE_LIGHT_FIXTURE) &&
		lights[1].position[0] == 1.0f && lights[1].color[1] == 1.0f && lights[1].intensity == 2.0f &&
		lights[1].type == 1 && fabsf(lights[1].cone_cosine - cosf(1.0f)) < 1e-6f, "the fixture");
	fake_scenario_storage.light_fixtures.count = 0;
	rasterizer_lights.light_count = 0;
	world_scene_map_unloaded();
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "empty"))
		empty_case();
	else if (!strcmp(name, "triangles"))
		triangles_case();
	else if (!strcmp(name, "ids"))
		ids_case();
	else if (!strcmp(name, "follows"))
		follows_case();
	else if (!strcmp(name, "lights"))
		lights_case();
	else
		return 2;
	return 0;
}
