/*
WORLD_CASCADES.H

display.ambient_occlusion "world" (docs/world-ao.md, "The distance-field
scene"): a global signed distance field about the camera, in nested
cascades (a clipmap: the finest nearest the camera, each next twice as
coarse and twice as wide), and the ray step through it that ray tracing's
effects share (#602).

Each cascade is WORLD_CASCADE_SIDE voxels a side, two distances a voxel:
- the structure (the level's own field, world_sdf.h's, which holds the static
  scenery too): written when the cascade is filled, and where the camera's
  movement brings new voxels into it;
- the objects (their shapes, world_proxies.h's: bipeds' capsules, vehicles'
  boxes and wheels, items): an object is stamped once, and stamped again only
  when it has moved (its shapes changed by more than WORLD_CASCADE_MOVED: the
  motion decides, not the object's kind), and then only the region its old
  and new shapes reach is found again. A frame's work goes as what moved.

The cascades move with the camera in whole steps of WORLD_CASCADE_SNAP
voxels, and are addressed toroidally (a voxel's place in its cascade is its
world index modulo the side), so that a step finds only the slab it
brings in. On the GPU they are one 3D texture of signed bytes, two a texel
(RG8 snorm), the cascades one above the other along z, each with one more
texel along each axis than its side (the guard: a copy of the first, so that
a linear filter at the toroidal seam reads the right neighbour); see
world_cascades_texture.

The arithmetic here is pure (no game state): port/linux/game/world_cascades.c,
tools/harness/tests/world_cascades.c. The emitters' trace (xgpu_spirv.c's and
xgpu_msl.c's world_trace) is checked against world_trace_ray
(tools/test_world_trace.py).
*/

#ifndef WORLD_CASCADES_H
#define WORLD_CASCADES_H

struct world_proxy;

/* ---------- constants (xgpu.h's XGPU_WORLD_CASCADE* are these) */

enum
{
	WORLD_CASCADES = 4,
	WORLD_CASCADE_SIDE = 64,
	/* (a texel more a side on the GPU: the guard) */
	WORLD_CASCADE_SPAN = WORLD_CASCADE_SIDE + 1,
	/* the distance each way a voxel holds, in its cascade's voxels (past
	it, clamped: a sphere trace's longest step) */
	WORLD_CASCADE_RANGE_VOXELS = 4,
	/* the cascades move in steps of this many of their voxels */
	WORLD_CASCADE_SNAP = 4,
	/* bytes a texel: the structure's distance, the objects' */
	WORLD_CASCADE_TEXEL = 2,
	WORLD_CASCADES_BYTES = WORLD_CASCADE_SPAN * WORLD_CASCADE_SPAN * WORLD_CASCADE_SPAN * WORLD_CASCADES *
		WORLD_CASCADE_TEXEL,
	/* the most objects and shapes stamped, and regions found again a frame
	(past them: the cascade found whole) */
	WORLD_CASCADES_MAXIMUM_OBJECTS = 512,
	WORLD_CASCADES_MAXIMUM_SHAPES = 2048,
	WORLD_CASCADES_MAXIMUM_BOXES = 128,
	/* the slices along z a frame fills of cascades found whole (at the
start, after a field comes or the camera jumps), the coarsest first: a
cascade being filled is not named ready, and the coarser serve */
	WORLD_CASCADES_FILL_SLICES = 16,
	/* a ray's most steps */
	WORLD_TRACE_STEPS = 24,
	/* what a ray may hit (a mask), and what it hit */
	WORLD_TRACE_STRUCTURE = 1,
	WORLD_TRACE_OBJECTS = 2,
};

/* the finest cascade's voxel (world units; each next twice it) */
#define WORLD_CASCADE_FINEST 0.25f
/* a shape is in the cascades whose voxel is at most its thickness (a
radius, a box's least half extent) over this: thinner, its filtered
distance would hardly show it (a biped's limbs are in the finest two) */
#define WORLD_CASCADE_THINNEST 0.25f
/* a shape moved: any of its numbers changed by more than this (world units) */
#define WORLD_CASCADE_MOVED 0.005f
/* a ray hits where the distance is under this share of its cascade's voxel,
and steps at least this share of it */
#define WORLD_TRACE_HIT 0.25f
#define WORLD_TRACE_LEAST_STEP 0.5f

/* ---------- structures */

/* a cascade: its window's first voxel (its world index: the voxel's centre
is at (first + 0.5) * voxel), its voxel, whether it is filled, and while it
is not, how many of its slices along z are */
struct world_cascade
{
	long first[3];
	float voxel;
	int ready;
	long filled;
};

/* an object stamped: its id (a datum handle) and its shapes (shapes[first]
to shapes[first + count - 1], in the world: world_proxies.h's struct
world_proxy) */
struct world_cascades_object
{
	long id;
	long first, count;
};

/* a region of the texture to upload (its texels, world_cascades_texture's
places: x, y, z and the size along each) */
struct world_cascades_box
{
	long place[3];
	long size[3];
};

/* the structure's distance at a point (world units; a large positive one
where there is none): world_sdf.h's world_sdf_sample */
typedef float (*world_cascades_structure)(void *context, float const point[3]);

struct world_cascades
{
	struct world_cascade cascades[WORLD_CASCADES];
	/* the texture as the GPU has it (WORLD_CASCADES_BYTES: x fastest, then
	y, then z; the cascades one above the other along z) */
	signed char *texels;
	/* the objects stamped and their shapes */
	struct world_cascades_object objects[WORLD_CASCADES_MAXIMUM_OBJECTS];
	struct world_proxy *shapes;
	long object_count, shape_count;
	/* this update's regions to upload, and whether one was missed (the
	whole texture to upload then) */
	struct world_cascades_box boxes[WORLD_CASCADES_MAXIMUM_BOXES];
	long box_count;
	int whole;
	/* this update's work: voxels whose structure was found, whose objects
	were, and the objects that moved (counted, measured) */
	long structure_voxels, object_voxels, moved;
};

/* the result of a ray (world_trace_ray): whether it hit, how far along it
(t_max where it did not), and what (WORLD_TRACE_*; 0 none) */
struct world_trace_hit
{
	int hit;
	float t;
	int kind;
};

/* ---------- prototypes (port/linux/game/world_cascades.c) */

/* made empty (no cascade ready), and let go of; FALSE if no memory */
int world_cascades_create(struct world_cascades *cascades);
void world_cascades_free(struct world_cascades *cascades);

/* a frame's update about the camera: each cascade moved with it (the slabs
it brings in found; one moved past its side found whole again,
WORLD_CASCADES_FILL_SLICES slices a frame); the structure found again
everywhere where it changed (a field come, a BSP switched to); and the
objects' shapes (objects[i]'s, of
shapes) compared with those stamped: each that moved, came or went found
again where its old and new shapes reach. The regions written are in
boxes (whole: all of it) */
void world_cascades_update(struct world_cascades *cascades, float const camera[3], int structure_changed,
	world_cascades_structure structure, void *context, struct world_cascades_object const *objects, long object_count,
	struct world_proxy const *shapes);

/* the cascades as the GPU's block has them: each [k] the first voxel's
centre (xyz) and the voxel (w; 0 where not ready) */
void world_cascades_block(struct world_cascades const *cascades, float block[WORLD_CASCADES][4]);

/* a shape's signed distance at a point (world units; a large positive one
for a kind that has none: an occlusion field) */
float world_cascades_shape_distance(struct world_proxy const *shape, float const point[3]);

/* the cascade a point is in (the finest ready whose window holds it with
its linear filter's neighbours; -1 none), and the two distances there
(world units), as the GPU finds them */
int world_cascades_find(float const block[WORLD_CASCADES][4], float const point[3]);
void world_cascades_sample(struct world_cascades const *cascades, float const block[WORLD_CASCADES][4],
	int cascade, float const point[3], float distances[2]);

/* the shared ray step (sphere tracing through the cascades): from origin
along the unit direction to at most t_max, hitting what mask allows
(WORLD_TRACE_*). A distance counts only once the ray is outside it: a ray
that starts inside solid (an object's own shapes about its surface) hits
nothing of it until it has left it, as front faces alone would. The
emitters' world_trace finds the same. A hardware backend (ray queries,
#602) implements the same: docs/world-ao.md */
void world_trace_ray(struct world_cascades const *cascades, float const block[WORLD_CASCADES][4],
	float const origin[3], float const direction[3], float t_max, int mask, struct world_trace_hit *hit);

/* a distance encoded (a voxel's byte, over range), and decoded as the
GPU's snorm is */
signed char world_cascades_encode(float distance, float range);
float world_cascades_decode(signed char value, float range);

#endif
