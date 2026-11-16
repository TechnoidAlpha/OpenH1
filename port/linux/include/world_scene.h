/*
WORLD_SCENE.H

display.ambient_occlusion "world" (docs/world-ao.md, #578): the world as a
renderer that works in world space sees it, beside the game's own state and
never part of it. Built once per map (the structure BSPs' static geometry)
and updated each frame (the objects, as drawn: render_interpolation.c's
pose), each thing under an id that stays the same for as long as it exists,
so that a bake (the distance field, world_sdf.h), the occlusion's proxies
(world_proxies.h) and a later ray tracer (a BLAS for each mesh, a TLAS of
the instances) all name it alike. Render-only: nothing here is read by the
simulation, sent between machines or written to a saved game.

Workstream A owns this interface (port/linux/game/world_scene.c); B and C
read it.
*/

#ifndef WORLD_SCENE_H
#define WORLD_SCENE_H

/* ---------- constants */

enum
{
	/* a static mesh's id: its BSP's index in the scenario (and this bit) */
	WORLD_SCENE_ID_STATIC_BIT = 0x40000000,
	/* none */
	WORLD_SCENE_ID_NONE = -1,
	/* the most objects a frame's scene holds (the game's own object
	table's size, as halo_port_capacity.h raises it, is the cap) */
	WORLD_SCENE_MAXIMUM_INSTANCES = 2048,
	/* the most lights in its light list (the dynamic lights the game
	submits, as display.dynamic_lights ranks them, plus the structure's
	light fixtures) */
	WORLD_SCENE_MAXIMUM_LIGHTS = 512,
};

/* what an instance is */
enum world_scene_instance_kind
{
	_world_scene_instance_static, /* a structure BSP (an identity transform) */
	_world_scene_instance_object, /* an object: biped, vehicle, weapon, scenery, ... */
	_world_scene_instance_first_person, /* the first-person weapon and hands (no occluder) */
	NUMBER_OF_WORLD_SCENE_INSTANCE_KINDS
};

/* ---------- structures */

/* a world-space triangle soup, owned by whoever asked for it (a copy: a
background bake reads it after the map's tags are gone) */
struct world_scene_triangles
{
	long count;
	/* count * 3 vertices, x y z each (world units) */
	float *positions;
	/* count material ids (the collision surface's material index, -1 none) */
	short *materials;
	/* the bounds of every vertex: x0 y0 z0, x1 y1 z1 */
	float bounds[6];
};

/* an instance as drawn this frame */
struct world_scene_instance
{
	/* stable: an object's datum handle (its index and salt), a BSP's
	WORLD_SCENE_ID_STATIC_BIT | index */
	long id;
	short kind; /* world_scene_instance_kind */
	short object_type; /* the game's _object_type_*, -1 for static */
	/* its definition (an object's tag index, -1 none): the proxies' and
	the meshes' key */
	long definition;
	/* the object's root node as drawn this frame and the frame before (a
	row-major 3x4 whose columns are the node's forward, left and up,
	scaled, then its position: transform * (x y z 1) is a point of the
	node's own space in the world; for motion vectors) */
	float transform[3][4];
	float previous_transform[3][4];
	/* its bounding sphere as drawn: centre and radius */
	float sphere[4];
	/* where its ambient light was sampled (render_lighting's point): the
	occlusion's reference (world_ao.h) */
	float lighting_point[3];
	/* the frame index it was last updated in */
	unsigned long frame;
};

/* a light of the world light list (world space, linear colour) */
struct world_scene_light
{
	long id; /* stable for the light's life (its datum handle, or a fixture's index) */
	short type; /* 0 point, 1 spot, 2 directional */
	short flags;
	float position[3], radius;
	float direction[3], cone_cosine;
	float color[3], intensity;
};

/* ---------- prototypes (port/linux/game/world_scene.c) */

/* the map's structure loaded (scenario_load) and unloaded (scenario_unload);
a BSP switched to (scenario_switch_structure_bsp) */
void world_scene_map_loaded(void);
void world_scene_map_unloaded(void);
void world_scene_bsp_switched(short bsp_index);

/* a copy of a structure BSP's collision geometry, world space; FALSE if the
map has no such BSP. The caller frees it (world_scene_triangles_free). Safe
only on the game's thread while the map is loaded */
int world_scene_static_triangles(short bsp_index, struct world_scene_triangles *triangles);
void world_scene_triangles_free(struct world_scene_triangles *triangles);
/* any collision BSP's surfaces (a struct collision_bsp: an object's, the
static scenery's) fanned into triangles in its own space, as the structure's
are (9 floats each into positions; NULL for the count alone): how many */
long world_scene_collision_fan(void const *collision_bsp, float *positions);

/* each frame, once the objects stand where they are drawn (render_frame,
inside render_interpolation_frame_begin/_end): the objects found again */
void world_scene_frame_begin(void);

/* the frame's instances and lights (valid until the next
world_scene_frame_begin) */
long world_scene_instance_count(void);
struct world_scene_instance const *world_scene_instances(void);
struct world_scene_instance const *world_scene_instance_find(long id);
long world_scene_light_count(void);
struct world_scene_light const *world_scene_lights(void);

#endif
