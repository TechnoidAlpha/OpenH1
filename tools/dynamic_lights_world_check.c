/*
DYNAMIC_LIGHTS_WORLD_CHECK.C

tools/test_dynamic_lights_world.py's checks of the enhanced dynamic lights'
world as the hosts make it (port/linux/src/xgpu_dynamic_lights_world.c):

- every point ends its walk of the GPU's tree in the cluster (or none) its
  walk of the game's BSP ended in, as the shaders walked it before
  (xgpu.h's layout, the depth's limit, the bad nodes), on made-up BSPs
  (random planes and clusters, solid leaves, nodes past the count, chains
  past the depth's limit, nodes reached twice) and on worlds given as files
  (a world's texels as the game sends them);
- a light the frame's walk mask leaves out is in the mask of the cluster of
  every point its sphere's pixels walk from, or of none;
- the mask leaves out what it should: a light within one box-shaped
  cluster, and one reaching every cluster its sphere meets.

  dynamic_lights_world_check [world files...]
*/

#include "xgpu_dynamic_lights_world.h"
#include "xgpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALL 0xffffffffu

static unsigned long long random_state = 0x9e3779b97f4a7c15ull;
static int failures;

static uint32_t random_next(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (uint32_t)(random_state >> 11);
}

static float random_unit(void)
{
	return (float)(random_next() & 0xffffff) / (float)0x1000000;
}

static float random_range(float lower, float upper)
{
	return lower + (upper - lower) * random_unit();
}

static int plane_front(const uint32_t texel[4], const float q[3])
{
	float plane[4];

	memcpy(plane, texel, sizeof(plane));
	return plane[0] * q[0] + plane[1] * q[1] + plane[2] * q[2] - plane[3] >= 0.0f;
}

/* the game's BSP walked as the shaders walked it (xgpu.h): the point's
cluster, or ALL */
static uint32_t game_walk(const uint32_t (*world)[4], const float q[3])
{
	const uint32_t *header = world[0];
	uint32_t node = 0;
	int depth;

	if (!header[0])
		return ALL;
	for (depth = 0; depth < XGPU_DYNAMIC_LIGHTS_WORLD_DEPTH; depth++)
	{
		const uint32_t *entry;
		uint32_t child;

		if (node >= header[0])
			break;
		entry = world[1 + node];
		if (entry[0] >= header[2] - header[1])
			break;
		child = plane_front(world[header[1] + entry[0]], q) ? entry[2] : entry[1];
		if (child & 0x80000000u)
		{
			uint32_t leaf = child & 0x7fffffffu, cluster;

			if (child == 0xffffffffu || leaf >= header[3])
				break;
			cluster = world[header[2] + leaf / 4][leaf % 4];
			return cluster < XGPU_DYNAMIC_LIGHTS_CLUSTERS ? cluster : ALL;
		}
		node = child;
	}
	return ALL;
}

/* the GPU's tree walked as the shaders walk it now */
static uint32_t gpu_walk(const struct xgpu_dynamic_lights_world *world, const float q[3])
{
	uint32_t node = world->texels[0][1];
	int depth;

	if (!world->texels[0][0])
		return ALL;
	for (depth = 0; depth < XGPU_DYNAMIC_LIGHTS_WORLD_DEPTH; depth++)
	{
		uint32_t child = plane_front(world->texels[1 + 2 * node], q) ? world->texels[2 + 2 * node][1] :
			world->texels[2 + 2 * node][0];

		if (child & XGPU_DYNAMIC_LIGHTS_WORLD_CLUSTER)
			return child == XGPU_DYNAMIC_LIGHTS_WORLD_NONE ? ALL : child & 0x7fffffffu;
		if (child >= world->texels[0][0])
		{
			printf("FAIL: a node's child %u past the %u nodes\n", child, world->texels[0][0]);
			failures++;
			return ALL;
		}
		node = child;
	}
	printf("FAIL: a walk deeper than the limit\n");
	failures++;
	return ALL;
}

/* the frame's walk masks (the models', then the structure's), from the
clusters' masks put in the cells' slice of them (xgpu.h) */
static void walked_both(const struct xgpu_dynamic_lights_world *world, const float (*position)[4], uint32_t count,
	const uint32_t (*masks)[4], uint32_t walk[8])
{
	static uint32_t cells[XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * (XGPU_DYNAMIC_LIGHTS_GRID_Z + 1)][4];
	static float block[3 + XGPU_DYNAMIC_LIGHTS_MAXIMUM][4];
	uint32_t light;

	memcpy(cells[XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * XGPU_DYNAMIC_LIGHTS_GRID_Z], masks,
		XGPU_DYNAMIC_LIGHTS_CLUSTERS * sizeof(cells[0]));
	/* (the block: its grid's count of lights, then their positions; past
	them an earlier frame's, which are not walked for) */
	for (light = 0; light < XGPU_DYNAMIC_LIGHTS_MAXIMUM; light++)
	{
		block[3 + light][0] = block[3 + light][1] = block[3 + light][2] = 0.0f;
		block[3 + light][3] = 1.0f;
	}
	block[0][3] = (float)count;
	memcpy(block[3], position, count * sizeof(block[0]));
	xgpu_dynamic_lights_world_walked(world, (const float (*)[4])block, XGPU_DYNAMIC_LIGHTS_MAXIMUM, cells, walk);
	for (light = count; light < XGPU_DYNAMIC_LIGHTS_MAXIMUM; light++)
	{
		if ((walk[light >> 5] | walk[4 + (light >> 5)]) & (1u << (light & 31)))
		{
			printf("FAIL: light %u past the frame's %u walked for\n", light, count);
			failures++;
		}
	}
	/* (the structure's: the models' but the lights for the models alone) */
	for (light = 0; light < count; light++)
	{
		uint32_t bit = 1u << (light & 31);
		int model = (walk[light >> 5] & bit) != 0, structure = (walk[4 + (light >> 5)] & bit) != 0;

		if (structure != (model && !(position[light][3] < 0.0f)))
		{
			printf("FAIL: light %u walked for by the models %d, the structure %d\n", light, model, structure);
			failures++;
		}
	}
}

static void walked(const struct xgpu_dynamic_lights_world *world, const float (*position)[4], uint32_t count,
	const uint32_t (*masks)[4], uint32_t walk[4])
{
	uint32_t both[8];

	walked_both(world, position, count, masks, both);
	memcpy(walk, both, 4 * sizeof(walk[0]));
}

struct world_input
{
	uint32_t (*texels)[4];
	uint32_t count;
	float lower[3], upper[3];
};

/* a made-up BSP: nodes in a random tree (some reached twice, some past
the count, some planes past the planes, a chain past the depth's limit),
random planes through the box, leaves in a few clusters or none */
static void made_up_world(struct world_input *input, uint32_t nodes, uint32_t clusters, int flavour)
{
	uint32_t planes = nodes / 2 + 1, leaves = nodes + 1, plane_base = 1 + nodes, leaf_base = plane_base + planes;
	uint32_t node, next_leaf = 0, index;

	input->count = leaf_base + (leaves + 3) / 4;
	input->texels = calloc(input->count, sizeof(*input->texels));
	for (index = 0; index < 3; index++)
	{
		input->lower[index] = -100.0f;
		input->upper[index] = 100.0f;
	}
	input->texels[0][0] = nodes;
	input->texels[0][1] = plane_base;
	input->texels[0][2] = leaf_base;
	input->texels[0][3] = leaves;
	for (index = 0; index < planes; index++)
	{
		float plane[4], length;
		int axis = (int)(random_next() % 4);

		plane[0] = random_range(-1.0f, 1.0f);
		plane[1] = random_range(-1.0f, 1.0f);
		plane[2] = random_range(-1.0f, 1.0f);
		/* (some along an axis, as many of a map's are) */
		if (axis < 3)
		{
			plane[0] = plane[1] = plane[2] = 0.0f;
			plane[axis] = random_next() & 1 ? 1.0f : -1.0f;
		}
		length = sqrtf(plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2]);
		plane[0] /= length;
		plane[1] /= length;
		plane[2] /= length;
		plane[3] = random_range(-80.0f, 80.0f);
		memcpy(input->texels[plane_base + index], plane, sizeof(plane));
	}
	for (index = 0; index < leaves; index++)
	{
		uint32_t roll = random_next() % 8;

		input->texels[leaf_base + index / 4][index % 4] = roll < 3 ? 0xffffffffu : random_next() % clusters;
	}
	for (node = 0; node < nodes; node++)
	{
		int side;

		input->texels[1 + node][0] = random_next() % planes;
		if (flavour == 1 && random_next() % 64 == 0)
			input->texels[1 + node][0] = planes + random_next() % 4;
		for (side = 0; side < 2; side++)
		{
			uint32_t child;
			uint32_t roll = random_next() % 16;

			/* a tree: each node's children later nodes, or leaves */
			if (flavour == 2)
			{
				/* (a chain: node n's in front is n + 1, deeper than the limit) */
				child = side ? node + 1 : 0x80000000u | (next_leaf++ % leaves);
				if (node + 1 >= nodes)
					child = 0x80000000u | (next_leaf++ % leaves);
			}
			else if (roll < 7 || node + 1 >= nodes)
				child = 0x80000000u | (next_leaf++ % leaves);
			else
				child = node + 1 + random_next() % ((nodes - node - 1) < 6 ? (nodes - node - 1) : 6);
			if (flavour == 1 && roll == 15)
				child = random_next() & 1 ? 0xffffffffu : nodes + random_next() % 4;
			if (flavour == 1 && roll == 14)
				child = 0x80000000u | (leaves + random_next() % 4);
			input->texels[1 + node][1 + side] = child;
		}
	}
}

static void check_points(const char *name, const struct world_input *input, const struct xgpu_dynamic_lights_world *world,
	int points)
{
	int index, wrong = 0;

	for (index = 0; index < points; index++)
	{
		float q[3];
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			float span = input->upper[axis] - input->lower[axis];

			q[axis] = random_range(input->lower[axis] - 0.1f * span, input->upper[axis] + 0.1f * span);
			/* (some on a plane's grid, as a map's points along its axes) */
			if (random_next() % 4 == 0)
				q[axis] = roundf(q[axis]);
		}
		if (game_walk((const uint32_t (*)[4])input->texels, q) != gpu_walk(world, q))
		{
			if (wrong++ < 5)
				printf("FAIL: %s: the point %g %g %g in %u by the game's BSP, %u by the GPU's\n", name, (double)q[0],
					(double)q[1], (double)q[2], game_walk((const uint32_t (*)[4])input->texels, q), gpu_walk(world, q));
		}
	}
	failures += wrong;
}

/* random lights and the clusters they reach; for points in each light's
sphere, the walk's point off it: a light the mask leaves out is kept by
every such point's walk */
static void check_walk_mask(const char *name, const struct world_input *input,
	const struct xgpu_dynamic_lights_world *world, uint32_t clusters, int frames, unsigned long *left_out,
	unsigned long *lights_seen)
{
	static float position[XGPU_DYNAMIC_LIGHTS_MAXIMUM][4];
	static uint32_t masks[XGPU_DYNAMIC_LIGHTS_CLUSTERS][4];
	int frame, wrong = 0;

	for (frame = 0; frame < frames; frame++)
	{
		uint32_t walk[4], light, count = 1 + random_next() % XGPU_DYNAMIC_LIGHTS_MAXIMUM, cluster;

		memset(masks, 0, sizeof(masks));
		for (light = 0; light < count; light++)
		{
			float radius = random_unit() < 0.5f ? random_range(0.2f, 3.0f) : random_range(3.0f, 30.0f);
			int axis;
			uint32_t reach = random_next() % 4;

			for (axis = 0; axis < 3; axis++)
				position[light][axis] = random_range(input->lower[axis], input->upper[axis]);
			position[light][3] = (random_next() & 1 ? 1.0f : -1.0f) / radius;
			/* (the clusters it reaches: none, a few, every one, or the one
			it is in and those the sampled points of its sphere are in) */
			if (reach == 0)
				continue;
			for (cluster = 0; cluster < clusters; cluster++)
			{
				if (reach == 2 || (reach == 1 && random_next() % 3 == 0))
					masks[cluster][light >> 5] |= 1u << (light & 31);
			}
			if (reach == 3)
			{
				int sample;

				for (sample = 0; sample < 64; sample++)
				{
					float q[3];
					uint32_t found;

					for (axis = 0; axis < 3; axis++)
						q[axis] = position[light][axis] + random_range(-radius, radius) * 0.7f;
					found = game_walk((const uint32_t (*)[4])input->texels, q);
					if (found != ALL)
						masks[found][light >> 5] |= 1u << (light & 31);
				}
			}
		}
		walked(world, (const float (*)[4])position, count, (const uint32_t (*)[4])masks, walk);
		for (light = 0; light < count; light++)
		{
			float radius = 1.0f / fabsf(position[light][3]);
			int sample;

			(*lights_seen)++;
			if (walk[light >> 5] & (1u << (light & 31)))
				continue;
			(*left_out)++;
			for (sample = 0; sample < 400; sample++)
			{
				float p[3], n[3], length, q[3], distance;
				uint32_t found;
				int axis;

				/* a pixel within the sphere (many near its edge), and its
				walk's point along a normal */
				do
				{
					for (axis = 0; axis < 3; axis++)
						p[axis] = random_range(-1.0f, 1.0f);
					distance = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
				} while (distance > 1.0f || distance < 1.0e-3f);
				if (sample & 1)
					distance = 1.0f / distance * 0.9999f;
				else
					distance = 1.0f;
				for (axis = 0; axis < 3; axis++)
					p[axis] = position[light][axis] + p[axis] * distance * radius;
				do
				{
					for (axis = 0; axis < 3; axis++)
						n[axis] = random_range(-1.0f, 1.0f);
					length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				} while (length > 1.0f || length < 1.0e-3f);
				for (axis = 0; axis < 3; axis++)
					q[axis] = p[axis] + n[axis] / length * XGPU_DYNAMIC_LIGHTS_CLUSTER_OFFSET;
				found = game_walk((const uint32_t (*)[4])input->texels, q);
				if (found != ALL && !(masks[found][light >> 5] & (1u << (light & 31))))
				{
					if (wrong++ < 5)
						printf("FAIL: %s: light %u (at %g %g %g, radius %g) left out of the walk, but the point "
							"%g %g %g of its sphere is in cluster %u, which it does not reach\n", name, light,
							(double)position[light][0], (double)position[light][1], (double)position[light][2],
							(double)radius, (double)q[0], (double)q[1], (double)q[2], found);
				}
			}
		}
	}
	failures += wrong;
}

/* a world of box-shaped clusters, split along the axes at 0 (x) and 10
(y): cluster 0 below both, 1 past x, 2 past y, 3 past both */
static void check_boxes(void)
{
	static uint32_t texels[16][4];
	static float position[XGPU_DYNAMIC_LIGHTS_MAXIMUM][4];
	static uint32_t masks[XGPU_DYNAMIC_LIGHTS_CLUSTERS][4];
	struct xgpu_dynamic_lights_world world;
	const float x_plane[4] = { 1.0f, 0.0f, 0.0f, 0.0f }, y_plane[4] = { 0.0f, 1.0f, 0.0f, 10.0f };
	uint32_t walk[4];

	memset(&world, 0, sizeof(world));
	memset(texels, 0, sizeof(texels));
	/* (three nodes, two planes, four leaves) */
	texels[0][0] = 3;
	texels[0][1] = 4;
	texels[0][2] = 6;
	texels[0][3] = 4;
	texels[1][0] = 0; texels[1][1] = 1; texels[1][2] = 2;
	texels[2][0] = 1; texels[2][1] = 0x80000000u; texels[2][2] = 0x80000002u;
	texels[3][0] = 1; texels[3][1] = 0x80000001u; texels[3][2] = 0x80000003u;
	memcpy(texels[4], x_plane, 16);
	memcpy(texels[5], y_plane, 16);
	texels[6][0] = 0; texels[6][1] = 1; texels[6][2] = 2; texels[6][3] = 3;
	if (!xgpu_dynamic_lights_world_convert(&world, (const uint32_t (*)[4])texels, 7) || !world.bounded)
	{
		printf("FAIL: the boxes' world was not made, or not bounded\n");
		failures++;
		return;
	}
	/* light 0 in cluster 0, well within it: left out though it reaches no
	other; light 1 near the x split, reaching cluster 0 alone: walked; light
	2 across the x split, reaching both clusters it meets: left out; light 3
	reaching no cluster, within cluster 3: walked */
	memset(masks, 0, sizeof(masks));
	position[0][0] = -20.0f; position[0][1] = -20.0f; position[0][2] = 0.0f; position[0][3] = 1.0f / 5.0f;
	masks[0][0] |= 1u;
	position[1][0] = -2.0f; position[1][1] = -20.0f; position[1][2] = 0.0f; position[1][3] = 1.0f / 5.0f;
	masks[0][0] |= 2u;
	position[2][0] = 1.0f; position[2][1] = -20.0f; position[2][2] = 0.0f; position[2][3] = -1.0f / 5.0f;
	masks[0][0] |= 4u;
	masks[1][0] |= 4u;
	position[3][0] = 20.0f; position[3][1] = 20.0f; position[3][2] = 0.0f; position[3][3] = 1.0f / 2.0f;
	walked(&world, (const float (*)[4])position, 4, (const uint32_t (*)[4])masks, walk);
	if (walk[0] != 0xau)
	{
		printf("FAIL: the boxes' walk mask %08x, not 0000000a\n", walk[0]);
		failures++;
	}
	/* (a light past the world's reach: walked) */
	position[0][0] = 1.0e6f;
	walked(&world, (const float (*)[4])position, 1, (const uint32_t (*)[4])masks, walk);
	if (walk[0] != 1u)
	{
		printf("FAIL: a light past the reach left out of the walk\n");
		failures++;
	}
	printf("boxes: %u nodes, depth %u, walk mask as expected\n", world.nodes, world.depth);
}

static void check_world(const char *name, struct world_input *input, uint32_t clusters, int points, int frames)
{
	struct xgpu_dynamic_lights_world world;
	unsigned long left_out = 0, lights_seen = 0;

	memset(&world, 0, sizeof(world));
	if (!xgpu_dynamic_lights_world_convert(&world, (const uint32_t (*)[4])input->texels, input->count))
	{
		printf("FAIL: %s: not made\n", name);
		failures++;
		return;
	}
	check_points(name, input, &world, points);
	if (frames)
		check_walk_mask(name, input, &world, clusters, frames, &left_out, &lights_seen);
	printf("%s: %u nodes (depth %u) from %u (%u texels from %u), bounded %d; walk left out %lu of %lu lights\n", name,
		world.nodes, world.depth, world.game_nodes, world.count, input->count, world.bounded, left_out, lights_seen);
	free(world.texels);
	free(world.boxes);
	free(world.first);
	free(world.box);
	free(world.planes);
}

static int world_file(const char *path, struct world_input *input)
{
	FILE *file = fopen(path, "rb");
	long size;
	uint32_t index, leaf;
	int axis;

	if (!file)
		return 0;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	input->count = (uint32_t)(size / 16);
	input->texels = malloc((size_t)size);
	if (!input->texels || fread(input->texels, 16, input->count, file) != input->count)
	{
		fclose(file);
		return 0;
	}
	fclose(file);
	/* (the points sampled within the planes' reach) */
	for (axis = 0; axis < 3; axis++)
	{
		input->lower[axis] = 1.0e30f;
		input->upper[axis] = -1.0e30f;
	}
	for (index = 0; index < input->texels[0][0]; index++)
	{
		float plane[4];
		uint32_t plane_index = input->texels[1 + index][0];

		if (input->texels[0][1] + plane_index >= input->count)
			continue;
		memcpy(plane, input->texels[input->texels[0][1] + plane_index], 16);
		for (axis = 0; axis < 3; axis++)
		{
			if (fabsf(plane[axis]) > 0.999f)
			{
				float at = plane[3] / plane[axis];

				input->lower[axis] = fminf(input->lower[axis], at);
				input->upper[axis] = fmaxf(input->upper[axis], at);
			}
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		if (input->lower[axis] > input->upper[axis])
		{
			input->lower[axis] = -500.0f;
			input->upper[axis] = 500.0f;
		}
	}
	(void)leaf;
	return 1;
}

int main(int argc, char **argv)
{
	int index, flavour;

	check_boxes();
	for (flavour = 0; flavour < 3; flavour++)
	{
		for (index = 0; index < 12; index++)
		{
			struct world_input input;
			char name[64];
			uint32_t nodes = 1 + random_next() % (index < 6 ? 40 : 3000);

			if (flavour == 2)
				nodes = 150 + random_next() % 100;
			made_up_world(&input, nodes, 1 + random_next() % 12, flavour);
			snprintf(name, sizeof(name), "made-up %d.%d", flavour, index);
			check_world(name, &input, XGPU_DYNAMIC_LIGHTS_CLUSTERS, 20000, index < 4 ? 20 : 4);
			free(input.texels);
		}
	}
	for (index = 1; index < argc; index++)
	{
		struct world_input input;

		if (!world_file(argv[index], &input))
		{
			printf("FAIL: %s cannot be read\n", argv[index]);
			failures++;
			continue;
		}
		check_world(argv[index], &input, XGPU_DYNAMIC_LIGHTS_CLUSTERS, 400000, 30);
		free(input.texels);
	}
	printf(failures ? "%d failures\n" : "ok\n", failures);
	return failures != 0;
}
