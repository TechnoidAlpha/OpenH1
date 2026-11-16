/*
WORLD_PROXIES_SOURCE.H

What the object proxies (world_proxies.c) read of the game, as plain
numbers: a definition's skeleton and collision vertices, the window's
objects and their nodes as drawn, the camera's frustum, and the debug
view's lines. port/linux/game/world_proxies_game.c reads them from the tags
and objects; tools/harness/tests/world_proxies.c gives a fake world instead.
Workstream C's own (docs/world-ao.md).

The objects come from world_scene.h's instances (workstream A).
*/

#ifndef WORLD_PROXIES_SOURCE_H
#define WORLD_PROXIES_SOURCE_H

/* ---------- constants */

enum
{
	/* the most nodes read of a definition (the game's MAXIMUM_NODES_PER_MODEL) */
	WORLD_PROXIES_SOURCE_MAXIMUM_NODES = 64,
};

/* what shapes a definition gets (docs/world-ao.md, "Proxies") */
enum world_proxies_source_class
{
	_world_proxies_source_none, /* projectiles, sound scenery, no model */
	_world_proxies_source_biped, /* capsules on the limbs, the torso; spheres */
	_world_proxies_source_vehicle, /* up to 4 boxes and the wheels' spheres */
	_world_proxies_source_box, /* scenery, items, devices: one box (of its collision
		geometry, else of what it draws opaque) */
	_world_proxies_source_sphere, /* anything else: its bounding sphere */
	NUMBER_OF_WORLD_PROXIES_SOURCE_CLASSES
};

/* ---------- structures */

/* a node's frame: where its x, y and z axes point (with its scale) and its
origin, in its parent space (the world, or a model's space) */
struct world_proxies_frame
{
	float axes[3][3];
	float position[3];
};

struct world_proxies_source_node
{
	short parent; /* -1 the root */
	short wheel; /* its name says it is a wheel (or tire) */
	/* the node in the model's space, in the default pose */
	struct world_proxies_frame default_frame;
	/* its collision geometry's vertices, in the node's space (the first
	permutation's), x y z each vertex_stride bytes; none: 0 */
	long vertex_count;
	long vertex_stride;
	float const *vertices;
};

/* a definition as the shapes are made from it */
struct world_proxies_source
{
	short shape_class; /* world_proxies_source_class */
	short node_count;
	/* the bounding sphere, in the model's space */
	float bounding_center[3];
	float bounding_radius;
	/* a box definition with no collision geometry: the bounds of what its
	model draws opaque (its most detailed geometry, its regions' first
	permutations), in node 0's space; drawn 0 when it draws nothing
	opaque (glass, a glowing marker, an effect) or can't be read */
	short drawn;
	float drawn_bounds[2][3];
	struct world_proxies_source_node nodes[WORLD_PROXIES_SOURCE_MAXIMUM_NODES];
};

/* an object's occlusion field drawn now (world_sdf.h's struct
world_sdf_object, docs/world-ao.md "The objects' occlusion fields"), as the window places
it on node 0: its first voxel's centre and the voxel, in node 0's space; its
voxels along x, y, z; its reach from the grid's middle, in voxels; its
place in the GPU's atlas of them and the atlas's size, in texels; its
geometry's bounds in node 0's space (x0 y0 z0 x1 y1 z1) */
struct world_proxies_source_field
{
	float origin[3];
	float voxel;
	float reach;
	float bounds[6];
	unsigned long size[3];
	unsigned long place[3];
	unsigned long atlas[3];
};

/* an object of the window, as drawn */
struct world_proxies_source_instance
{
	long id; /* its datum handle (world_scene.h's id) */
	long definition; /* its tag index */
	/* a static scenery placement that the drawn field and its pages hold
	(world_sdf_scenery_baked): its shapes after the dynamic objects' */
	int baked;
	/* a vehicle whose occlusion field is on its way with the BSP's field
	(world_sdf_object_awaited): its boxes left out until the field is
	drawn, its wheels' spheres kept */
	int field_awaited;
};

/* ---------- prototypes (port/linux/game/world_proxies_game.c) */

/* a definition's skeleton and collision vertices; FALSE: none (its shape_class is
then _world_proxies_source_none). The vertices stay valid while the map is
loaded */
int world_proxies_source_definition(long definition, struct world_proxies_source *source);
/* the frame's objects that may occlude (drawn, not a held weapon), at most
maximum; returns how many */
long world_proxies_source_instances(struct world_proxies_source_instance *instances, long maximum);
/* a definition's occlusion field, if one is drawn (FALSE: none; its
shapes' boxes shade, as before) */
int world_proxies_source_field(long definition, struct world_proxies_source_field *field);
/* an object's nodes as drawn this frame (world space), at most maximum;
returns how many (0: not drawn) */
short world_proxies_source_nodes(long id, struct world_proxies_frame *frames, short maximum);
/* the window's frustum: its 4 side planes, x y z d each (a point p is
outside a plane where x*p.x + y*p.y + z*p.z - d > 0); FALSE: none known */
int world_proxies_source_frustum(float planes[4][4]);
/* debug.world_proxies_view, and its lines (world space, a colour 0xRRGGBB) */
int world_proxies_source_view(void);
void world_proxies_source_line(float const a[3], float const b[3], unsigned long color);

#endif
