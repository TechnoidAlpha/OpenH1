/*
XGPU_DYNAMIC_LIGHTS_WORLD.H

The enhanced dynamic lights' world as the hosts' renderers give it to the
GPU (xgpu_dynamic_lights_world.c): the structure's 3D BSP the game sends
(xgpu.h) made smaller and quicker to walk, and the bounds of its clusters,
from which each frame's lights that a pixel's walk could take away are
found. Host code (the Vulkan and Metal backends), built beside the
renderer's other shared parts: it uses nothing but the C library.
*/

#ifndef __XGPU_DYNAMIC_LIGHTS_WORLD_H
#define __XGPU_DYNAMIC_LIGHTS_WORLD_H

#include <stdint.h>

/* the GPU's world (xgpu.h's, after the game's): texel 0 the header (the
node count, 0 none, and the root node), then each node as two texels, its
plane (its floats' bits: the normal, and its distance along it) at
1 + 2 node and its children behind and in front at 2 + 2 node: a child
with the top bit set is a cluster (its low bits) or none (all bits: every
light). A pixel's walk from the root ends at a cluster or none, as the
game's BSP's ended in the same point (xgpu_dynamic_lights_world_convert),
in at most XGPU_DYNAMIC_LIGHTS_WORLD_DEPTH nodes. */
#define XGPU_DYNAMIC_LIGHTS_WORLD_NONE 0xffffffffu
#define XGPU_DYNAMIC_LIGHTS_WORLD_CLUSTER 0x80000000u

/* a cluster's region, as a box about each of its leaves, and the planes of
the leaf's faces (outward: its points x have plane . x <= plane[3]) */
struct xgpu_dynamic_lights_world_box
{
	float lower[3], upper[3];
	uint32_t plane_first, plane_count;
};

struct xgpu_dynamic_lights_world
{
	/* the GPU's texels and their count (1: none, its header of no nodes) */
	uint32_t (*texels)[4];
	uint32_t count;
	/* (the game's nodes and what was made of them) */
	uint32_t game_nodes, game_depth, nodes, depth;
	/* the clusters' boxes: cluster c's leaves' are boxes[first[c]] to
	boxes[first[c + 1] - 1], and box[c] holds them all; their planes; FALSE:
	none were found (every light is walked) */
	int bounded;
	struct xgpu_dynamic_lights_world_box *boxes;
	float (*planes)[4];
	uint32_t *first;
	struct xgpu_dynamic_lights_world_box *box;
};

/* the game's world (count texels: xgpu.h) to the GPU's, into world (its
texels made the first time, room for XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS);
FALSE if it could not be made (world->count is then 1, no world, unless no
memory: 0). Its nodes take two texels each where the game's take one and
share their planes and leaves: a world of more than
(XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS - 1) / 2 nodes once made smaller (ten
times Blood Gulch's 12,000) cannot be made, and its lights reach every
cluster, as they do in a world the game cannot send. */
int xgpu_dynamic_lights_world_convert(struct xgpu_dynamic_lights_world *world, const uint32_t (*texels)[4],
	uint32_t count);

/* the cells' texels (four words each, of the cells' texture: xgpu.h) of the
lights the shaders walk the world for, XGPU_DYNAMIC_LIGHTS_WALK: past the
clusters' masks in their slice, the models' and then the structure's */
#define XGPU_DYNAMIC_LIGHTS_WORLD_WALK_TEXEL (32 * 32 * 16 + 512)

/* the frame's lights (the lights' block, xgpu.h's struct
xgpu_dynamic_lights_block, as its vec4s: grid[0].w the lights' count, then
each light's position, xyz and 1 / radius; count of them there) whose walk
could take them from a pixel, from the clusters' masks in the cells
(xgpu.h): those that do not reach some cluster their sphere meets. A
pixel's walk can take away only the lights not in its cluster's mask; a
light reaches no pixel farther than its radius, nor its walk's point
farther than a little more; so where a cell's lights are none of these, the
walk is not needed. Set: walk[light >> 5] bit light & 31, the models'; and
walk[4 + (light >> 5)], the structure's, which pass over a light for the
models alone (1 / radius less than 0) whatever the walk finds. */
void xgpu_dynamic_lights_world_walked(const struct xgpu_dynamic_lights_world *world, const float (*block)[4],
	uint32_t count, const void *cells, uint32_t walk[8]);

#endif
