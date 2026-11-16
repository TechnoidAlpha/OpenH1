/*
XGPU_METAL_WORLD_AO.C

display.ambient_occlusion "world"'s resources in the Metal backend
(docs/world-ao.md, "GPU binding layout"), as the Vulkan backend has them
(port/linux/vulkan/xgpu_vulkan_world_ao.c): the structure's distance field,
sparse (world_sdf.h): its atlas of bricks, a 3D texture of signed bytes
(MTLPixelFormatR8Snorm) that the shaders read through a constexpr linear
sampler clamped to its edge, at XGPU_METAL_TEXTURE_WORLD_SDF, and its
table of cells, a 3D RGBA8 texture they read by texel, at
XGPU_METAL_TEXTURE_WORLD_SDF_TABLE; and the window's block (struct
xgpu_world_ao_block) at XGPU_METAL_BUFFER_WORLD_AO (xgpu_metal_set_world_ao);
and the distance-field scene's cascades (world_cascades.h), one 3D texture
of two signed bytes a texel (MTLPixelFormatRG8Snorm), written a box at a
time (WORLD_AO_CASCADES records).

The field arrives a few slices at a time (the command stream's
WORLD_AO_FIELD records), its table first, then its atlas: each texture is
made as its first slices come, and both are bound once both have had their
last; until then, and with none, 1x1x1 stand-ins (a table cell far outside:
no occlusion). The block arrives with each window (WORLD_AO_WINDOW) and is
copied into the frame's upload ring, its parameters[1].z set where the GPU
has the SIMD-groups' reductions (the shaders' walk of the shapes is then a
SIMD-group's: xgpu_msl.c's world_proxies_occlusion); with none, a block of
zeros of the backend's own.
*/

#include "xgpu_metal.h"
#include "xgpu_metal_bindings.h"
#include "xgpu_stream.h"

#include <stdlib.h>
#include <string.h>

/* a part of the field: its texture (0 none), its size, and whether its last
slices have come */
struct world_ao_part
{
	uint32_t texture;
	uint32_t width, height, depth;
	int whole;
};

static struct
{
	/* the table, the atlas and the static scenery's pages
	(XGPU_STREAM_WORLD_AO_TABLE, _ATLAS, _SCENERY: a 2D array of bytes),
	their stand-ins, and the block of zeros */
	struct world_ao_part parts[4];
	uint32_t stand_ins[4];
	uint32_t zeros;
	struct xgpu_metal_source block;
	int made;
	/* the occlusion found once a pixel the draws read (0: its stand-in, an
	occlusion of 1) */
	uint32_t pixels, pixels_stand_in;
	/* (whether the GPU has the SIMD-groups' reductions: the block's
	parameters[1].z) */
	int simd;
	/* the distance-field scene's cascades (made as their first box comes;
	0 none), and their stand-in (nothing anywhere) */
	uint32_t cascades, cascades_stand_in;
} world_ao;

/* the distance-field scene's cascades: two signed bytes a texel */
static uint32_t world_ao_cascades_make(uint32_t width, uint32_t height, uint32_t depth)
{
	struct xgpu_metal_texture_description description;

	memset(&description, 0, sizeof(description));
	description.type = XGPU_METAL_TEXTURE_3D;
	description.format = XGPU_METAL_FORMAT_RG8_SNORM;
	description.usage = XGPU_METAL_USAGE_SAMPLED;
	description.width = (uint16_t)width;
	description.height = (uint16_t)height;
	description.depth_or_layers = (uint16_t)depth;
	description.levels = 1;
	return xgpu_metal_texture_create(&description);
}

static uint32_t world_ao_texture(uint32_t part, uint32_t width, uint32_t height, uint32_t depth)
{
	struct xgpu_metal_texture_description description;

	memset(&description, 0, sizeof(description));
	description.type = part == XGPU_STREAM_WORLD_AO_SCENERY ? XGPU_METAL_TEXTURE_2D_ARRAY : XGPU_METAL_TEXTURE_3D;
	/* (the pages' bytes: R8 unorm, the coverage's format; the vehicles'
	fields' four halves: RGBA16F) */
	description.format = part == XGPU_STREAM_WORLD_AO_TABLE ? XGPU_METAL_FORMAT_RGBA8 :
		part == XGPU_STREAM_WORLD_AO_SCENERY ? XGPU_METAL_FORMAT_COVERAGE :
		part == XGPU_STREAM_WORLD_AO_OBJECTS ? XGPU_METAL_FORMAT_RGBA16F : XGPU_METAL_FORMAT_R8_SNORM;
	description.usage = XGPU_METAL_USAGE_SAMPLED;
	description.width = (uint16_t)width;
	description.height = (uint16_t)height;
	description.depth_or_layers = (uint16_t)depth;
	description.levels = 1;
	return xgpu_metal_texture_create(&description);
}

/* the stand-ins and the zeros, made the first time */
static void world_ao_make(void)
{
	static const unsigned char outside_cell[4] = { 0, 0, 0, 0 };
	static const signed char far_outside = 127;
	static const unsigned char none = 255;
	static const unsigned short no_occlusion[4] = { 0, 0, 0, 0 };

	if (world_ao.made)
		return;
	world_ao.made = 1;
	world_ao.simd = xgpu_metal_simd_reductions();
	world_ao.stand_ins[XGPU_STREAM_WORLD_AO_OBJECTS] = world_ao_texture(XGPU_STREAM_WORLD_AO_OBJECTS, 1, 1, 1);
	if (world_ao.stand_ins[XGPU_STREAM_WORLD_AO_OBJECTS])
		xgpu_metal_texture_upload(world_ao.stand_ins[XGPU_STREAM_WORLD_AO_OBJECTS], 0, 0, 0, 0, 0, 1, 1, 1, no_occlusion,
			8, 8);
	world_ao.stand_ins[XGPU_STREAM_WORLD_AO_SCENERY] = world_ao_texture(XGPU_STREAM_WORLD_AO_SCENERY, 1, 1, 1);
	if (world_ao.stand_ins[XGPU_STREAM_WORLD_AO_SCENERY])
		xgpu_metal_texture_upload(world_ao.stand_ins[XGPU_STREAM_WORLD_AO_SCENERY], 0, 0, 0, 0, 0, 1, 1, 1, &none, 1, 1);
	world_ao.stand_ins[XGPU_STREAM_WORLD_AO_TABLE] = world_ao_texture(XGPU_STREAM_WORLD_AO_TABLE, 1, 1, 1);
	if (world_ao.stand_ins[XGPU_STREAM_WORLD_AO_TABLE])
		xgpu_metal_texture_upload(world_ao.stand_ins[XGPU_STREAM_WORLD_AO_TABLE], 0, 0, 0, 0, 0, 1, 1, 1, outside_cell,
			4, 4);
	world_ao.stand_ins[XGPU_STREAM_WORLD_AO_ATLAS] = world_ao_texture(XGPU_STREAM_WORLD_AO_ATLAS, 1, 1, 1);
	if (world_ao.stand_ins[XGPU_STREAM_WORLD_AO_ATLAS])
		xgpu_metal_texture_upload(world_ao.stand_ins[XGPU_STREAM_WORLD_AO_ATLAS], 0, 0, 0, 0, 0, 1, 1, 1, &far_outside,
			1, 1);
	{
		struct xgpu_metal_texture_description description;
		static const float one[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

		memset(&description, 0, sizeof(description));
		description.type = XGPU_METAL_TEXTURE_2D;
		description.format = XGPU_METAL_FORMAT_RGBA32F;
		description.usage = XGPU_METAL_USAGE_SAMPLED;
		description.width = description.height = description.depth_or_layers = description.levels = 1;
		world_ao.pixels_stand_in = xgpu_metal_texture_create(&description);
		if (world_ao.pixels_stand_in)
			xgpu_metal_texture_upload(world_ao.pixels_stand_in, 0, 0, 0, 0, 0, 1, 1, 1, one, 16, 16);
		xgpu_metal_set_world_ao_pixels(world_ao.pixels_stand_in);
	}
	world_ao.cascades_stand_in = world_ao_cascades_make(1, 1, 1);
	if (world_ao.cascades_stand_in)
	{
		static const signed char nothing[2] = { 127, 127 };

		xgpu_metal_texture_upload(world_ao.cascades_stand_in, 0, 0, 0, 0, 0, 1, 1, 1, nothing, 2, 2);
	}
	world_ao.zeros = xgpu_metal_buffer_create(sizeof(struct xgpu_world_ao_block));
	if (world_ao.zeros)
	{
		static const struct xgpu_world_ao_block none;

		xgpu_metal_buffer_write(world_ao.zeros, 0, &none, sizeof(none), 1);
	}
	world_ao.block.kind = XGPU_METAL_SOURCE_BUFFER;
	world_ao.block.handle = world_ao.zeros;
	world_ao.block.offset = 0;
	world_ao.block.size = world_ao.zeros ? sizeof(struct xgpu_world_ao_block) : 0;
	if (!world_ao.zeros)
		world_ao.block.kind = XGPU_METAL_SOURCE_NONE;
}

static void world_ao_bind(void)
{
	int whole = world_ao.parts[0].texture && world_ao.parts[0].whole && world_ao.parts[1].texture &&
		world_ao.parts[1].whole;
	struct world_ao_part const *scenery = &world_ao.parts[XGPU_STREAM_WORLD_AO_SCENERY];
	struct world_ao_part const *objects = &world_ao.parts[XGPU_STREAM_WORLD_AO_OBJECTS];

	xgpu_metal_set_world_ao(whole ? world_ao.parts[XGPU_STREAM_WORLD_AO_ATLAS].texture :
		world_ao.stand_ins[XGPU_STREAM_WORLD_AO_ATLAS], whole ? world_ao.parts[XGPU_STREAM_WORLD_AO_TABLE].texture :
		world_ao.stand_ins[XGPU_STREAM_WORLD_AO_TABLE], scenery->texture && scenery->whole ? scenery->texture :
		world_ao.stand_ins[XGPU_STREAM_WORLD_AO_SCENERY], objects->texture && objects->whole ? objects->texture :
		world_ao.stand_ins[XGPU_STREAM_WORLD_AO_OBJECTS], world_ao.block);
}

static void world_ao_part_release(struct world_ao_part *part)
{
	if (part->texture)
		xgpu_metal_texture_destroy(part->texture);
	memset(part, 0, sizeof(*part));
}

void xgpu_metal_world_ao_field(uint32_t part_index, uint32_t width, uint32_t height, uint32_t depth, uint32_t z_first,
	uint32_t z_count, const void *data)
{
	struct world_ao_part *part;
	uint32_t texel = XGPU_STREAM_WORLD_AO_TEXEL(part_index);

	world_ao_make();
	/* (none: every part let go of) */
	if (!width || !height || !depth || !data || part_index > XGPU_STREAM_WORLD_AO_OBJECTS)
	{
		world_ao_part_release(&world_ao.parts[0]);
		world_ao_part_release(&world_ao.parts[1]);
		world_ao_part_release(&world_ao.parts[2]);
		world_ao_part_release(&world_ao.parts[3]);
		world_ao_bind();
		return;
	}
	part = &world_ao.parts[part_index];
	/* (a new field's table comes first: the atlas before it is another's) */
	if (part_index == XGPU_STREAM_WORLD_AO_TABLE && z_first == 0)
		world_ao_part_release(&world_ao.parts[XGPU_STREAM_WORLD_AO_ATLAS]);
	/* (a new field's part: its texture made as its first slices come) */
	if (!part->texture || part->width != width || part->height != height || part->depth != depth || z_first == 0)
	{
		world_ao_part_release(part);
		part->texture = world_ao_texture(part_index, width, height, depth);
		part->width = width;
		part->height = height;
		part->depth = depth;
		world_ao_bind();
		if (!part->texture)
			return;
	}
	/* (the pages a layer at a time) */
	if (part_index == XGPU_STREAM_WORLD_AO_SCENERY)
	{
		uint32_t layer;

		for (layer = 0; layer < z_count; layer++)
			xgpu_metal_texture_upload(part->texture, 0, z_first + layer, 0, 0, 0, width, height, 1,
				(const unsigned char *)data + (size_t)layer * width * height, width, width * height);
	}
	else
	{
		xgpu_metal_texture_upload(part->texture, 0, 0, 0, 0, z_first, width, height, z_count, data, width * texel,
			width * height * texel);
	}
	if (z_first + z_count >= depth && !part->whole)
	{
		part->whole = 1;
		world_ao_bind();
	}
}

void xgpu_metal_world_ao_cascades(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth,
	const void *data)
{
	world_ao_make();
	if (!width || !data)
	{
		if (world_ao.cascades)
			xgpu_metal_texture_destroy(world_ao.cascades);
		world_ao.cascades = 0;
		return;
	}
	/* (made as the first box comes, nothing anywhere until each part of it
	is written: the game writes every cascade whole before it names it) */
	if (!world_ao.cascades)
	{
		size_t bytes = (size_t)XGPU_STREAM_WORLD_AO_CASCADES_SIDE * XGPU_STREAM_WORLD_AO_CASCADES_SIDE *
			XGPU_STREAM_WORLD_AO_CASCADES_DEPTH * 2u;
		signed char *nothing = malloc(bytes);

		world_ao.cascades = world_ao_cascades_make(XGPU_STREAM_WORLD_AO_CASCADES_SIDE, XGPU_STREAM_WORLD_AO_CASCADES_SIDE,
			XGPU_STREAM_WORLD_AO_CASCADES_DEPTH);
		if (!world_ao.cascades || !nothing)
		{
			free(nothing);
			return;
		}
		memset(nothing, 127, bytes);
		xgpu_metal_texture_upload(world_ao.cascades, 0, 0, 0, 0, 0, XGPU_STREAM_WORLD_AO_CASCADES_SIDE,
			XGPU_STREAM_WORLD_AO_CASCADES_SIDE, XGPU_STREAM_WORLD_AO_CASCADES_DEPTH, nothing,
			XGPU_STREAM_WORLD_AO_CASCADES_SIDE * 2u, XGPU_STREAM_WORLD_AO_CASCADES_SIDE * XGPU_STREAM_WORLD_AO_CASCADES_SIDE * 2u);
		free(nothing);
	}
	xgpu_metal_texture_upload(world_ao.cascades, 0, 0, x, y, z, width, height, depth, data, width * 2u, width * height * 2u);
}

uint32_t xgpu_metal_world_ao_cascades_texture(void)
{
	world_ao_make();
	return world_ao.cascades ? world_ao.cascades : world_ao.cascades_stand_in;
}

void xgpu_metal_world_ao_window(const struct xgpu_world_ao_block *block)
{
	world_ao_make();
	if (block)
	{
		/* (its parameters[1].z, the backend's: whether the walk of the shapes
		is a SIMD-group's, xgpu_msl.c's world_proxies_occlusion) */
		static struct xgpu_world_ao_block copy;

		memcpy(&copy, block, sizeof(copy));
		copy.parameters[1][2] = world_ao.simd ? 1.0f : 0.0f;
		world_ao.block = xgpu_metal_stream(&copy, sizeof(copy));
	}
	else
	{
		world_ao.block.kind = world_ao.zeros ? XGPU_METAL_SOURCE_BUFFER : XGPU_METAL_SOURCE_NONE;
		world_ao.block.handle = world_ao.zeros;
		world_ao.block.offset = 0;
		world_ao.block.size = world_ao.zeros ? sizeof(*block) : 0;
	}
	world_ao_bind();
}

void xgpu_metal_world_ao_set_pixels(uint32_t texture)
{
	world_ao_make();
	world_ao.pixels = texture;
	xgpu_metal_set_world_ao_pixels(texture ? texture : world_ao.pixels_stand_in);
}
