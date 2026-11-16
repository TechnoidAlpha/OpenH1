/* display.ambient_occlusion "ray_traced"'s scene (port/linux/game/world_rt.c;
test_world_rt.py): a mesh's triangles turned to face out, the box, the
instances' transforms; and a frame of a fake world's objects: the meshes
made once a map as they are first needed (the structure's, a definition's
nodes', the box for one with none), each frame's instances of them on the
objects' nodes, the static scenery left out, all let go of at the map's
end. */

#include "harness.h"
#include "world_ao.h"
#include "world_proxies_source.h"
#include "world_rt.h"

#include <math.h>

void platform_log(const char *format, ...)
{
	(void)format;
}

/* ---------- the fake world

The loaded BSP (2 triangles); definition 7: two nodes, the first with a
tetrahedron wound inward, the second with none; definition 9: one node and
none (its box); objects 0x10003 (of 7, its nodes at x 10 and 20), 0x20005
(of 9, an item, at y 5, scaled 2), 0x30006 (of 7, static scenery baked)
and 0x40008 (of 11, neither an item nor any collision geometry: none). */

static short bsp_index = 0;
static int structure_asked, definitions_asked;

short world_rt_source_bsp(void)
{
	return bsp_index;
}

long world_rt_source_structure(short index, float *triangles, long maximum)
{
	static const float floor[2][9] = { { 0, 0, 0, 1, 0, 0, 1, 1, 0 }, { 0, 0, 0, 1, 1, 0, 0, 1, 0 } };

	CHECK(index == bsp_index, "another BSP's: %d", index);
	structure_asked++;
	if (triangles)
		memcpy(triangles, floor, sizeof(float) * 9 * (maximum < 2 ? maximum : 2));
	return 2;
}

short world_rt_source_definition(long definition, float sphere[4], int *item)
{
	definitions_asked++;
	/* (9 an item; 11 not, with no collision geometry either) */
	*item = definition == 9;
	sphere[0] = 0.0f;
	sphere[1] = 0.0f;
	sphere[2] = 0.5f;
	sphere[3] = 1.0f;
	return definition == 7 ? 2 : definition == 9 || definition == 11 ? 1 : 0;
}

long world_rt_source_node(long definition, short node_index, float *triangles, long maximum)
{
	/* (a tetrahedron, each face wound inward) */
	static const float inward[4][9] =
	{
		{ 0, 0, 0, 1, 0, 0, 0, 1, 0 },
		{ 0, 0, 0, 0, 0, 1, 1, 0, 0 },
		{ 0, 0, 0, 0, 1, 0, 0, 0, 1 },
		{ 1, 0, 0, 0, 0, 1, 0, 1, 0 },
	};

	if (definition != 7 || node_index != 0)
		return 0;
	if (triangles)
	{
		CHECK(maximum >= 4, "room for %ld", maximum);
		memcpy(triangles, inward, sizeof(inward));
	}
	return 4;
}

char const *world_rt_source_name(long definition)
{
	(void)definition;
	return "a definition";
}

long world_proxies_source_instances(struct world_proxies_source_instance *instances, long maximum)
{
	static const long ids[4] = { 0x10003, 0x20005, 0x30006, 0x40008 }, definitions[4] = { 7, 9, 7, 11 };
	long index;

	for (index = 0; index < 4 && index < maximum; index++)
	{
		memset(&instances[index], 0, sizeof(instances[index]));
		instances[index].id = ids[index];
		instances[index].definition = definitions[index];
		instances[index].baked = index == 2;
	}
	return index;
}

short world_proxies_source_nodes(long id, struct world_proxies_frame *frames, short maximum)
{
	short node, count = id == 0x20005 ? 1 : 2;

	CHECK(maximum >= count, "room for %d", maximum);
	for (node = 0; node < count; node++)
	{
		float scale = id == 0x20005 ? 2.0f : 1.0f;

		memset(&frames[node], 0, sizeof(frames[node]));
		frames[node].axes[0][0] = frames[node].axes[1][1] = frames[node].axes[2][2] = scale;
		frames[node].position[0] = id == 0x20005 ? 0.0f : 10.0f * (float)(node + 1);
		frames[node].position[1] = id == 0x20005 ? 5.0f : 0.0f;
	}
	return count;
}

/* what was handed to the renderer */
static struct
{
	unsigned long id, count;
	float first[9];
} meshes[16];
static int mesh_count, releases;
static struct world_ao_instance instances[16];
static unsigned long instance_count, instance_handings;

void halo_world_ao_mesh(unsigned long id, unsigned long count, float const *triangles)
{
	if (!id)
	{
		releases++;
		return;
	}
	CHECK(mesh_count < 16, "too many meshes");
	meshes[mesh_count].id = id;
	meshes[mesh_count].count = count;
	memcpy(meshes[mesh_count].first, triangles, sizeof(meshes[mesh_count].first));
	mesh_count++;
}

void halo_world_ao_instances(struct world_ao_instance const *given, unsigned long count)
{
	CHECK(count <= 16, "%lu instances", count);
	memcpy(instances, given, count * sizeof(*given));
	instance_count = count;
	instance_handings++;
}

#include "under_test.inc"

/* ---------- the cases */

static float signed_volume(float const *triangles, long count)
{
	double volume = 0.0;
	long index;

	for (index = 0; index < count; index++)
	{
		float const *a = triangles + index * 9, *b = a + 3, *c = a + 6;

		volume += a[0] * (b[1] * c[2] - b[2] * c[1]) + a[1] * (b[2] * c[0] - b[0] * c[2]) +
			a[2] * (b[0] * c[1] - b[1] * c[0]);
	}
	return (float)(volume / 6.0);
}

static void arithmetic_case(void)
{
	float box[12 * 9], turned[12 * 9], flat[9] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 }, kept[9];
	struct world_proxies_frame frame;
	float transform[3][4];
	static const float sphere[4] = { 0.0f, 0.0f, 1.0f, 2.0f };
	long index;
	int k;

	/* the box faces out (its volume 8), and is kept so */
	world_rt_box(box);
	CHECK(fabsf(signed_volume(box, 12) - 8.0f) < 1.0e-5f, "the box's volume %g", signed_volume(box, 12));
	memcpy(turned, box, sizeof(box));
	CHECK(fabsf(world_rt_orient(turned, 12, 1.0e-6f) - 8.0f) < 1.0e-5f && !memcmp(turned, box, sizeof(box)),
		"the box turned");
	/* wound inward, turned to face out: each triangle's second and third
	corners swapped */
	for (index = 0; index < 12; index++)
	{
		for (k = 0; k < 3; k++)
		{
			turned[index * 9 + 3 + k] = box[index * 9 + 6 + k];
			turned[index * 9 + 6 + k] = box[index * 9 + 3 + k];
		}
	}
	CHECK(signed_volume(turned, 12) < 0.0f, "wound inward");
	CHECK(fabsf(world_rt_orient(turned, 12, 1.0e-6f) - 8.0f) < 1.0e-5f && !memcmp(turned, box, sizeof(box)),
		"the inward box turned to face out");
	/* a flat mesh as it is */
	memcpy(kept, flat, sizeof(flat));
	world_rt_orient(kept, 1, 1.0e-6f);
	CHECK(!memcmp(kept, flat, sizeof(flat)), "a flat mesh turned");
	/* a frame's transform: its axes as columns, its position last */
	memset(&frame, 0, sizeof(frame));
	frame.axes[0][1] = 1.0f;
	frame.axes[1][0] = -1.0f;
	frame.axes[2][2] = 1.0f;
	frame.position[0] = 3.0f;
	frame.position[1] = 4.0f;
	frame.position[2] = 5.0f;
	world_rt_transform(&frame, transform);
	CHECK(transform[1][0] == 1.0f && transform[0][1] == -1.0f && transform[2][2] == 1.0f && transform[0][3] == 3.0f &&
		transform[1][3] == 4.0f && transform[2][3] == 5.0f && transform[0][0] == 0.0f, "the frame's transform");
	/* the box's about a sphere in the node's space */
	world_rt_box_transform(&frame, sphere, transform);
	CHECK(fabsf(transform[0][1] + 2.0f * WORLD_RT_BOX_SHARE) < 1.0e-6f &&
		fabsf(transform[2][2] - 2.0f * WORLD_RT_BOX_SHARE) < 1.0e-6f && transform[2][3] == 6.0f &&
		transform[0][3] == 3.0f, "the box's transform");
	CHECK(world_rt_owner(-1) == 0 && world_rt_owner(0x10003) == 4 && world_rt_owner(0x7fff0000) == 1,
		"the owners");
}

static struct world_ao_instance const *instance_of(unsigned long owner, unsigned int kind)
{
	unsigned long index;

	for (index = 0; index < instance_count; index++)
	{
		if (instances[index].owner == owner && instances[index].kind == kind)
			return &instances[index];
	}
	return NULL;
}

static void frame_case(void)
{
	struct world_ao_instance const *structure, *seven, *nine;
	unsigned long index;

	world_rt_frame(1);
	/* the meshes: the structure's, definition 7's first node's (turned to
	face out), the box for 9 */
	CHECK(mesh_count == 3, "%d meshes", mesh_count);
	CHECK(meshes[0].id == 1 && meshes[0].count == 2, "the structure's: %lu %lu", meshes[0].id, meshes[0].count);
	CHECK(meshes[1].id == 2 && meshes[1].count == 4 && meshes[1].first[3] == 0.0f && meshes[1].first[4] == 1.0f,
		"definition 7's node, turned: %lu %lu", meshes[1].id, meshes[1].count);
	CHECK(meshes[2].id == 3 && meshes[2].count == 12, "the box: %lu %lu", meshes[2].id, meshes[2].count);
	/* the instances: the structure, 7's one node with a mesh, 9's box; not
	the baked scenery */
	CHECK(instance_count == 3, "%lu instances", instance_count);
	structure = instance_of(0, WORLD_RT_STRUCTURE);
	seven = instance_of(4, WORLD_RT_OBJECT);
	nine = instance_of(6, WORLD_RT_OBJECT);
	CHECK(structure && structure->mesh == 1 && structure->transform[0][0] == 1.0f && structure->transform[0][3] == 0.0f,
		"the structure's");
	CHECK(seven && seven->mesh == 2 && seven->transform[0][3] == 10.0f && seven->transform[1][1] == 1.0f,
		"definition 7's node");
	CHECK(nine && nine->mesh == 3 && fabsf(nine->transform[0][0] - 2.0f * WORLD_RT_BOX_SHARE) < 1.0e-6f &&
		nine->transform[1][3] == 5.0f && nine->transform[2][3] == 1.0f, "9's box");
	for (index = 0; index < instance_count; index++)
		CHECK(instances[index].owner != 7 && instances[index].owner != 9, "the baked scenery's or 11's");
	/* the same frame again: nothing more; the next, the same meshes */
	world_rt_frame(1);
	CHECK(instance_handings == 1, "handed twice in a frame");
	world_rt_frame(2);
	CHECK(instance_handings == 2 && instance_count == 3 && mesh_count == 3, "the next frame: %lu %d", instance_count,
		mesh_count);
	CHECK(structure_asked == 2 && definitions_asked == 3, "asked again: %d %d", structure_asked, definitions_asked);
	/* another BSP: its structure's mesh */
	bsp_index = 1;
	world_rt_frame(3);
	CHECK(mesh_count == 4 && meshes[3].id == 4 && instance_of(0, WORLD_RT_STRUCTURE)->mesh == 4,
		"another BSP's structure");
	/* the map's end: every mesh let go of, made again from 1 */
	world_rt_map_unloaded();
	CHECK(releases == 1, "%d releases", releases);
	world_rt_frame(4);
	CHECK(mesh_count == 7 && meshes[4].id == 1, "made again: %d %lu", mesh_count, meshes[4].id);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "arithmetic"))
		arithmetic_case();
	else if (!strcmp(name, "frame"))
		frame_case();
	else
		return 2;
	return 0;
}
