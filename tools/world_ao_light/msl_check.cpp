/*
MSL_CHECK.CPP

The Metal emitter's display.ambient_occlusion "world" functions
(port/linux/src/xgpu_msl.c's world_ao_functions: their very text, in
world_msl.inc, which tools/test_world_ao_light.py writes from the emitter's
shader) run on the CPU (metal_shim.h) and compared with their reference,
world_proxies.c's world_proxy_occlusion and world_proxy_reach and
world_ao_shading.c's, over the made-up scene's points (scene.c): each
shape's occlusion and reach, the proxies' product (walked by a pixel
alone, and as one lane of a SIMD-group: metal_shim.h's simd_or) and the
field's cone.

  msl_check <count> <tolerance>   prints the largest difference of each;
                                  exits 1 if one is past the tolerance
*/

#include "metal_shim.h"

extern "C"
{
#include "world_ao.h"
#include "world_proxies.h"
#include "world_sdf.h"

void scene_make(void);
void scene_point(float point[3], float normal[3], float *owner, float *reference);
extern struct world_sdf_volume scene_volume;
extern struct world_proxy scene_proxies[];
extern long scene_proxy_count;
extern float scene_parameters[2][4];
extern float scene_grid[2][4];
extern unsigned int scene_cells[WORLD_AO_GRID_CELLS / 2][4];
extern float *scene_atlas_values;
extern unsigned long scene_atlas_size[3];
}

#include <cstdio>
#include <cstdlib>

#include "world_msl.inc"

static float4 vector_of(float const v[4])
{
	return float4(v[0], v[1], v[2], v[3]);
}

int main(int argc, char **argv)
{
	long count = argc > 2 ? atol(argv[1]) : 0, index, shape;
	float tolerance = argc > 2 ? (float)atof(argv[2]) : 0.0f;
	float worst_shape = 0.0f, worst_reach = 0.0f, worst_product = 0.0f, worst_field = 0.0f, worst_field_shape = 0.0f;
	long shaded = 0;
	static xgpu_world_ao block;
	texture3d<float> field, table, objects;
	static signed char atlas[16 * WORLD_SDF_ATLAS_LAYER_BYTES];

	if (count <= 0)
	{
		fprintf(stderr, "msl_check <count> <tolerance>\n");
		return 2;
	}
	scene_make();
	for (index = 0; index < 2; index++)
	{
		block.field[index] = float4(0, 0, 0, 0);
		block.parameters[index] = vector_of(scene_parameters[index]);
	}
	block.field[0] = float4(scene_volume.header.origin[0], scene_volume.header.origin[1],
		scene_volume.header.origin[2], 1.0f / scene_volume.header.voxel);
	block.field[1] = float4((float)scene_volume.header.width, (float)scene_volume.header.height,
		(float)scene_volume.header.depth, (float)world_sdf_atlas_layers(&scene_volume.header));
	for (shape = 0; shape < scene_proxy_count; shape++)
	{
		for (int part = 0; part < 3; part++)
			block.proxies[shape * 3 + part] = vector_of(scene_proxies[shape].v[part]);
	}
	for (index = 0; index < 2; index++)
		block.grid[index] = vector_of(scene_grid[index]);
	for (index = 0; index < WORLD_AO_GRID_CELLS / 2; index++)
		block.cells[index] = uint4{ scene_cells[index][0], scene_cells[index][1], scene_cells[index][2], scene_cells[index][3] };
	/* (the field as the GPU has it: its atlas's layers, and its table) */
	for (index = 0; index < (long)world_sdf_atlas_layers(&scene_volume.header) && index < 16; index++)
		world_sdf_atlas_layer(&scene_volume, (unsigned long)index, atlas + index * WORLD_SDF_ATLAS_LAYER_BYTES);
	field.voxels = atlas;
	field.width = field.height = WORLD_SDF_ATLAS * WORLD_SDF_BRICK_SPAN;
	field.depth = (int)world_sdf_atlas_layers(&scene_volume.header) * WORLD_SDF_BRICK_SPAN;
	table.texels = &scene_volume.cells[0][0];
	table.width = (int)scene_volume.header.width;
	table.height = (int)scene_volume.header.height;
	table.depth = (int)scene_volume.header.depth;
	objects.values = scene_atlas_values;
	objects.width = (int)scene_atlas_size[0];
	objects.height = (int)scene_atlas_size[1];
	objects.depth = (int)scene_atlas_size[2];
	for (shape = 0; shape < scene_proxy_count; shape++)
	{
		float4 v0 = block.proxies[shape * 3], v1 = block.proxies[shape * 3 + 1], v2 = block.proxies[shape * 3 + 2];

		/* (a vehicle's field, and its slots: its own, below) */
		if (v0.w < 1.0f || v0.w > 3.0f)
			continue;
		float found = v0.w == 1.0f ? world_proxy_sphere_reach(v2) : v0.w == 2.0f ? world_proxy_capsule_reach(v0, v1, v2) :
			world_proxy_box_reach(v1, v2);
		float difference = std::fabs(found - world_proxy_reach(&scene_proxies[shape]));

		worst_reach = difference > worst_reach ? difference : worst_reach;
	}
	for (index = 0; index < count; index++)
	{
		float point[3], normal[3], owner, reference, difference;
		float3 p, n;

		scene_point(point, normal, &owner, &reference);
		p = float3(point[0], point[1], point[2]);
		n = float3(normal[0], normal[1], normal[2]);
		for (shape = 0; shape < scene_proxy_count; shape++)
		{
			float4 v0 = block.proxies[shape * 3], v1 = block.proxies[shape * 3 + 1], v2 = block.proxies[shape * 3 + 2];

			if (v0.w == 4.0f)
			{
				/* (a vehicle's field: its occlusion by its fades, against the
				reference's) */
				struct world_proxy const *proxy = &scene_proxies[shape];
				float reach = proxy->v[2][3], away = 0.0f, fade, expected;

				for (int axis = 0; axis < 3; axis++)
					away += (point[axis] - proxy->v[0][axis]) * (point[axis] - proxy->v[0][axis]);
				away = std::sqrt(away);
				fade = away >= reach ? 0.0f : std::fmin(std::fmax((reach - away) / (reach * WORLD_AO_REACH_FADE), 0.0f), 1.0f) *
					world_ao_field_proxy_fade(proxy, point, normal);
				expected = fade > 0.0f ? world_ao_field_proxy_occlusion(proxy, point, normal) * fade : 0.0f;
				difference = std::fabs(world_proxy_field(block, objects, (int)shape, p, n, v0, v1, v2) - expected);
				worst_field_shape = difference > worst_field_shape ? difference : worst_field_shape;
				shaded += expected > 1.0f / 255.0f;
				continue;
			}
			if (v0.w < 1.0f || v0.w > 3.0f)
				continue;
			float found = v0.w == 1.0f ? world_proxy_sphere(p, n, v0.xyz, v2.x) :
				v0.w == 2.0f ? world_proxy_capsule(p, n, v0.xyz, v1.xyz, v2.x) :
				world_proxy_box(p, n, v0.xyz, v1.xyz, v2.xyz, v2.w);

			difference = std::fabs(found - world_proxy_occlusion(&scene_proxies[shape], point, normal));
			if (difference > worst_shape)
			{
				worst_shape = difference;
				if (difference > tolerance)
					printf("shape %ld (kind %g) at %g %g %g, normal %g %g %g: %g, reference %g\n", shape, v0.w,
						point[0], point[1], point[2], normal[0], normal[1], normal[2], found,
						world_proxy_occlusion(&scene_proxies[shape], point, normal));
			}
		}
		/* (every shape, or the dynamic objects' alone: the first three; a
		third of the points walked as one lane of a SIMD-group whose other
		lanes' cells hold other shapes, which it must leave out) */
		int limit = index % 2 ? 64 : 3;

		block.parameters[1].z = index % 3 == 1 ? 1.0f : 0.0f;
		simd_lanes_others = index % 3 == 1 ? (unsigned int)(index * 2654435761u) : 0u;
		difference = std::fabs(world_proxies_occlusion(block, objects, p, n, owner, limit, 0) -
			world_ao_proxies_occlusion(scene_proxies, limit < scene_proxy_count ? limit : scene_proxy_count, point, normal,
			owner));
		worst_product = difference > worst_product ? difference : worst_product;
		/* (from the pixel's own depth too, as a static placement's own draws) */
		float own = index % 3 ? 0.0f : -0.4f;

		difference = std::fabs(world_sdf_occlusion(block, field, table, p, n, own) -
			world_ao_field_occlusion_from(&scene_volume, point, normal, own));
		worst_field = difference > worst_field ? difference : worst_field;
		/* (and as a two-sided shader's cards: their most open direction, and
		which it is) */
		{
			float3 found = n;
			float direction[3] = { normal[0], normal[1], normal[2] };

			difference = std::fabs(world_sdf_cards_occlusion(block, field, table, p, found, own, true) -
				world_ao_cards_occlusion(&scene_volume, point, direction, own));
			difference = std::fmax(difference, std::fabs(found.x - direction[0]) + std::fabs(found.y - direction[1]) +
				std::fabs(found.z - direction[2]));
			worst_field = difference > worst_field ? difference : worst_field;
		}
	}
	printf("shapes %.3g reach %.3g proxies %.3g field %.3g; a vehicle's field %.3g at %ld points it shades\n", worst_shape,
		worst_reach, worst_product, worst_field, worst_field_shape, shaded);
	return worst_shape > tolerance || worst_reach > tolerance || worst_product > tolerance || worst_field > tolerance ||
		worst_field_shape > tolerance || shaded < count / 20;
}
