/*
WORLD_RT.H

display.ambient_occlusion "ray_traced" (docs/world-ao.md, "Hardware ray
queries", #602): the scene the GPU's ray queries trace the occlusion's rays
through, as the backend builds its acceleration structures of it. The
meshes: the loaded structure BSP's collision geometry, and each object
definition's nodes' collision geometry (the first permutation's, in the
node's space), a mesh for each node that has some, or for an item with
none (a grenade, a weapon or equipment on the ground) a box about its
bounding sphere; made as they are first drawn
and kept until the map unloads (the backend's bottom-level structures,
built once). The instances: each frame, the structure (which stops a ray
and occludes nothing: the lightmaps and the field hold its occlusion) and
each dynamic object's meshes on its nodes as drawn (the top-level
structure, built again each frame). Not the static scenery the field and
its pages hold, nor a held weapon (world_proxies_source.h's instances).

Render-only: nothing here is read by the simulation, sent between machines
or saved. world_rt.c is the scene (its arithmetic testable alone:
tools/harness/tests/world_rt.c); what it reads of the game is
world_rt_source_* (port/linux/game/world_rt_game.c) and
world_proxies_source.h's instances and nodes.
*/

#ifndef WORLD_RT_H
#define WORLD_RT_H

struct world_proxies_frame;

/* ---------- constants */

enum
{
	/* the most meshes and instances (xgpu_stream.h's
	XGPU_STREAM_WORLD_AO_MESHES less the none, _INSTANCES_MAXIMUM) */
	WORLD_RT_MESHES = 4095,
	WORLD_RT_INSTANCES = 4096,
	/* the most nodes of a definition taken (world_proxies_source.h's) */
	WORLD_RT_NODES = 64,
	/* the most triangles of a mesh (XGPU_STREAM_WORLD_AO_MESH_TRIANGLES) */
	WORLD_RT_MESH_TRIANGLES = 1 << 20,
	/* an instance's kind (xgpu_stream.h's XGPU_WORLD_AO_INSTANCE_*) */
	WORLD_RT_STRUCTURE = 1,
	WORLD_RT_OBJECT = 2,
};

/* the half side of an item's box with no collision geometry, a share of
its bounding radius (a cube of that radius's sphere's volume would be
0.81; the shapes are smaller than their spheres), and the largest such
item's bounding radius (world units). Anything else with none has no mesh:
an emitter's, a wire's or the sky's ring's sphere is not its shape */
#define WORLD_RT_BOX_SHARE 0.6f
#define WORLD_RT_BOX_LARGEST 1.0f

/* ---------- prototypes (port/linux/game/world_rt.c) */

/* each frame, from its first window (world_ao.c, with "ray_traced"): the
meshes made that the frame's objects need first, and its instances handed
to the renderer (halo_world_ao_mesh, _instances); frame the frame's number,
once a frame */
void world_rt_frame(unsigned long frame);
/* the map unloaded (or the setting turned off): every mesh let go of */
void world_rt_map_unloaded(void);

/* an object's owner among the instances (its datum index plus one; 0 none,
-1 an object index), which a pixel of its own leaves out */
unsigned long world_rt_owner(long object_index);
/* a mesh's triangles (nine floats each) turned to face out: each wound
counterclockwise seen from outside, by the sign of the volume they hold
(none flipped where it is under least, an open or flat mesh's); returns
that volume, after */
float world_rt_orient(float *triangles, long count, float least);
/* the box from -1 to 1 along each axis, its 12 triangles facing out */
void world_rt_box(float triangles[12 * 9]);
/* a node's frame as an instance's transform (a row-major 3x4 whose
columns are its axes, scaled, then its position) */
void world_rt_transform(struct world_proxies_frame const *frame, float transform[3][4]);
/* ... and the box's about a bounding sphere in that node's space (centre,
radius): its axes the frame's times radius * WORLD_RT_BOX_SHARE */
void world_rt_box_transform(struct world_proxies_frame const *frame, float const sphere[4], float transform[3][4]);

/* ---------- what the scene reads of the game (port/linux/game/world_rt_game.c) */

/* the loaded structure BSP's index (-1 none) */
short world_rt_source_bsp(void);
/* its collision geometry's triangles, world space (nine floats each, into
triangles: NULL for the count alone, at most maximum): how many (0 none) */
long world_rt_source_structure(short bsp_index, float *triangles, long maximum);
/* a definition's nodes (at most WORLD_RT_NODES), its bounding sphere in
its model's space (centre, radius) and whether it is an item (a weapon,
equipment or garbage); 0 if it has none */
short world_rt_source_definition(long definition, float sphere[4], int *item);
/* a node's collision geometry's triangles in its own space (as above):
how many (0 none) */
long world_rt_source_node(long definition, short node_index, float *triangles, long maximum);
/* a definition's tag's name (HALO_WORLD_AO_COUNTS' log) */
char const *world_rt_source_name(long definition);

#endif
