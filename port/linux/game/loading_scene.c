/*
LOADING_SCENE.C

The main menu's scene for the loading screen (port/linux/src/loading_scene.h):
as a map change leaves the main menu, the meshes of the ui map's sky and
objects (the ring, scenery\halo\halo, in the stock map) are read as the
game draws them (the vertex and index buffers of each model's most detailed
geometry, as object_mesh.c reads them), with the textures their shaders
draw with as the texture cache holds them (the Xbox's texels: the presenter
decodes them), the sky's light and the menu's camera, and handed to the
presenter in one block (xgpu_backend_loading_scene). The presenter draws
the scene, slowly turning, under the loading screen's sweep while the next
map loads; the block is kept for every later load until the menu is left
again. Nothing is read from disk here, and nothing of the map's is kept in
the port: a scene the map does not have (another ui map's, none loaded)
leaves the bundled picture.

Called by halo_loading_begin (port/linux/src/sdl_platform.c) before the old
map is disposed of.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "models/model_definitions.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "objects/object_types.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_geometry.h"
#include "rasterizer/rasterizer_model_types.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "shaders/shader_definitions.h"
#include "shaders/shaders.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "cache/texture_cache.h"
#include "camera/observer.h"
#include "cseries/cseries_windows.h"

#include "../src/loading_scene.h"

#include <xtl.h>
#include <stdlib.h>
#include <string.h>

/* (port/linux/src/xgpu_stream_encode.c) */
void xgpu_backend_loading_scene(void const *data, unsigned long bytes);

/* ---------- constants */

enum
{
	LOADING_SCENE_DETAIL_LEVELS = 5, /* (models.c's NUMBER_OF_DETAIL_LEVELS_PER_MODEL) */
	LOADING_SCENE_OBJECTS = 16,
	/* VR: how long the level's first pictures take to come up, milliseconds */
	LOADING_SCENE_FADE_IN = 600,
	_loading_scene_part_stripped_bit = 0,
	/* shader types (shaders.c's) */
	_loading_scene_shader_model = 4,
	_loading_scene_shader_generic,
	_loading_scene_shader_chicago,
};

/* ---------- structures */

/* this build's model geometry and part (models.c's, object_mesh.c's) */
struct loading_scene_model_geometry
{
	byte reserved[0x24];
	struct tag_block parts;
};

struct loading_scene_model_geometry_part
{
	unsigned long flags;
	short shader_index;
	char previous_part_index;
	char next_part_index;
	short centroid_primary_node_index;
	short centroid_secondary_node_index;
	real centroid_primary_node_weight;
	real centroid_secondary_node_weight;
	real_point3d centroid;
	struct tag_block uncompressed_vertices;
	struct tag_block compressed_vertices;
	struct tag_block triangles;
	struct triangle_buffer triangle_buffer;
	struct vertex_buffer vertex_buffer;
};

struct loading_scene_model_shader_reference
{
	struct tag_reference shader;
	short permutation_index;
	word pad;
	long unused[3];
};

/* the fields read of a model shader (tag_schema_render.c's
shader_model_definition) */
struct loading_scene_shader_model
{
	byte shader[0x28];
	word flags;
	short type;
	byte reserved2C[0x70];
	real map_u_scale;
	real map_v_scale;
	struct tag_reference base_map;
};

/* of a generic and a chicago shader (tag_schema_render.c's shader_transparent) */
struct loading_scene_shader_transparent
{
	byte shader[0x28];
	byte numeric_counter_limit;
	byte flags;
	short first_map_type;
	short framebuffer_blend_function;
	short framebuffer_fade_mode;
	short framebuffer_fade_source;
	short pad32;
	real lens_flare_spacing;
	struct tag_reference lens_flare;
	struct tag_block extra_layers;
	struct tag_block maps;
};

/* a generic shader's map */
struct loading_scene_generic_map
{
	word flags;
	word pad02;
	real map_u_scale;
	real map_v_scale;
	real map_u_offset;
	real map_v_offset;
	real map_rotation;
	real mipmap_bias;
	struct tag_reference map;
	byte map_animation[0x38];
};

/* a chicago shader's map */
struct loading_scene_chicago_map
{
	word flags;
	byte reserved02[0x2A];
	short color_function;
	short alpha_function;
	byte reserved30[0x24];
	real map_u_scale;
	real map_v_scale;
	real map_u_offset;
	real map_v_offset;
	real map_rotation;
	real mipmap_bias;
	struct tag_reference map;
	byte reserved7C[0x60];
};

/* the fields read of a sky (render_sky.c's) */
struct loading_scene_sky
{
	struct tag_reference model;
	struct tag_reference animation_graph;
	byte pad20[0x8C];
	struct tag_block render_model_regions;
	struct tag_block animations;
	struct tag_block lights;
};

struct loading_scene_sky_light
{
	struct tag_reference lens_flare;
	char marker_name[TAG_STRING_LENGTH + 1];
	byte pad31[0x37];
	real_euler_angles2d direction;
	byte pad70[4];
};

typedef char verify_loading_scene_model_geometry_part_size[
	sizeof(struct loading_scene_model_geometry_part) == 0x68 ? 1 : -1];
typedef char verify_loading_scene_shader_model_base_map_offset[
	offsetof(struct loading_scene_shader_model, base_map) == 0xA4 ? 1 : -1];
typedef char verify_loading_scene_shader_transparent_maps_offset[
	offsetof(struct loading_scene_shader_transparent, maps) == 0x54 ? 1 : -1];
typedef char verify_loading_scene_generic_map_size[
	sizeof(struct loading_scene_generic_map) == 0x64 ? 1 : -1];
typedef char verify_loading_scene_chicago_map_function_offset[
	offsetof(struct loading_scene_chicago_map, color_function) == 0x2C ? 1 : -1];
typedef char verify_loading_scene_chicago_map_map_offset[
	offsetof(struct loading_scene_chicago_map, map) == 0x6C ? 1 : -1];
typedef char verify_loading_scene_chicago_map_size[
	sizeof(struct loading_scene_chicago_map) == 0xDC ? 1 : -1];
typedef char verify_loading_scene_sky_lights_offset[
	offsetof(struct loading_scene_sky, lights) == 0xC4 ? 1 : -1];
typedef char verify_loading_scene_sky_light_size[
	sizeof(struct loading_scene_sky_light) == 0x74 ? 1 : -1];

/* the block as it is built */
struct loading_scene_builder
{
	unsigned char *data;
	unsigned long bytes, capacity;
	boolean failed;
	struct loading_scene_part parts[LOADING_SCENE_MAXIMUM_PARTS];
	unsigned long part_count;
	struct loading_scene_texture textures[LOADING_SCENE_MAXIMUM_TEXTURES];
	long texture_bitmaps[LOADING_SCENE_MAXIMUM_TEXTURES];
	unsigned long texture_count;
	unsigned long vertex_total;
	real_vector3d light_direction;
};

/* ---------- private code */

/* room for `size` more bytes at an `alignment`, returning their offset (or
the builder failed) */
static unsigned long loading_scene_reserve(
	struct loading_scene_builder *builder,
	unsigned long size,
	unsigned long alignment)
{
	unsigned long offset = (builder->bytes + alignment - 1) & ~(alignment - 1);

	if (builder->failed || offset + size > LOADING_SCENE_MAXIMUM_BYTES)
	{
		builder->failed = TRUE;
		return 0;
	}
	if (offset + size > builder->capacity)
	{
		unsigned long capacity = builder->capacity ? builder->capacity : 256 * 1024;
		unsigned char *data;

		while (capacity < offset + size)
			capacity *= 2;
		data = realloc(builder->data, capacity);
		if (!data)
		{
			builder->failed = TRUE;
			return 0;
		}
		builder->data = data;
		builder->capacity = capacity;
	}
	memset(builder->data + builder->bytes, 0, offset + size - builder->bytes);
	builder->bytes = offset + size;
	return offset;
}

/* the texture a bitmap group's first bitmap is drawn with, if the texture
cache holds it now (none is loaded: a map change is no time to read one) */
static unsigned long loading_scene_texture(
	struct loading_scene_builder *builder,
	long bitmap_group_index)
{
	struct bitmap_data *bitmap;
	DWORD const *texture;
	D3DLOCKED_RECT locked;
	unsigned long index;
	unsigned long offset;

	if (bitmap_group_index == NONE)
		return LOADING_SCENE_NO_TEXTURE;
	for (index = 0; index < builder->texture_count; index++)
	{
		if (builder->texture_bitmaps[index] == bitmap_group_index)
			return index;
	}
	if (builder->texture_count >= LOADING_SCENE_MAXIMUM_TEXTURES)
		return LOADING_SCENE_NO_TEXTURE;
	bitmap = bitmap_group_try_and_get_bitmap(bitmap_group_index, 0);
	/* (2D textures only: no cube, volume or palette) */
	if (!bitmap || bitmap->type != 0 || bitmap->pixels_size <= 0 || bitmap->format == 17)
		return LOADING_SCENE_NO_TEXTURE;
	texture = _texture_cache_bitmap_get_hardware_format(bitmap, FALSE, FALSE);
	if (!texture || !texture[1] || !texture[3])
		return LOADING_SCENE_NO_TEXTURE;
	memset(&locked, 0, sizeof(locked));
	IDirect3DTexture8_LockRect((D3DTexture *)texture, 0, &locked, NULL, D3DLOCK_READONLY);
	if (!locked.pBits)
		return LOADING_SCENE_NO_TEXTURE;
	offset = loading_scene_reserve(builder, (unsigned long)bitmap->pixels_size, 4);
	if (builder->failed)
		return LOADING_SCENE_NO_TEXTURE;
	memcpy(builder->data + offset, locked.pBits, (size_t)bitmap->pixels_size);
	index = builder->texture_count++;
	builder->texture_bitmaps[index] = bitmap_group_index;
	builder->textures[index].format = (uint32_t)texture[3];
	builder->textures[index].size = (uint32_t)texture[4];
	builder->textures[index].texel_offset = (uint32_t)offset;
	builder->textures[index].texel_bytes = (uint32_t)bitmap->pixels_size;
	return index;
}

/* a map of a part: its bitmap's texture (if the texture cache holds it),
how it meets the next map, its scale (the model's base map scale's times
the shader's; none is one) and offset */
static void loading_scene_map(
	struct loading_scene_builder *builder,
	struct model const *model,
	struct loading_scene_part *part,
	long bitmap,
	short function,
	real u_scale,
	real v_scale,
	real u_offset,
	real v_offset)
{
	struct loading_scene_map *map = &part->maps[part->map_count++];

	map->texture = loading_scene_texture(builder, bitmap);
	map->function = function >= 0 && function < LOADING_SCENE_MAP_FUNCTIONS ?
		(uint32_t)function : LOADING_SCENE_MAP_CURRENT;
	map->scale[0] = (model->base_map_scale.i != 0.f ? model->base_map_scale.i : 1.f) * (u_scale != 0.f ? u_scale : 1.f);
	map->scale[1] = (model->base_map_scale.j != 0.f ? model->base_map_scale.j : 1.f) * (v_scale != 0.f ? v_scale : 1.f);
	map->offset[0] = u_offset;
	map->offset[1] = v_offset;
}

/* how a part of this shader is drawn; FALSE: not at all */
static boolean loading_scene_shader(
	struct loading_scene_builder *builder,
	struct model const *model,
	long shader_index,
	struct loading_scene_part *part)
{
	struct shader *shader = shader_index != NONE ? shader_definition_get(shader_index) : NULL;

	part->tint[0] = part->tint[1] = part->tint[2] = part->tint[3] = 1.f;
	if (!shader)
		return FALSE;
	switch (shader->base.type)
	{
	case _loading_scene_shader_model:
	{
		struct loading_scene_shader_model const *definition = (struct loading_scene_shader_model const *)shader;

		part->blend = LOADING_SCENE_BLEND_OPAQUE;
		part->flags = LOADING_SCENE_PART_LIT | LOADING_SCENE_PART_OPAQUE_ALPHA;
		loading_scene_map(builder, model, part, definition->base_map.index, LOADING_SCENE_MAP_CURRENT,
			definition->map_u_scale, definition->map_v_scale, 0.f, 0.f);
		break;
	}
	case _loading_scene_shader_generic:
	case _loading_scene_shader_chicago:
	{
		struct loading_scene_shader_transparent const *definition =
			(struct loading_scene_shader_transparent const *)shader;
		long index;

		if (definition->maps.count <= 0 || !definition->maps.address)
			return FALSE;
		part->blend = definition->framebuffer_blend_function >= 0 &&
			definition->framebuffer_blend_function < LOADING_SCENE_BLENDS - 1 ?
			(uint32_t)definition->framebuffer_blend_function + 1 : LOADING_SCENE_BLEND_ADD;
		part->flags = LOADING_SCENE_PART_TWO_SIDED;
		if (shader->base.type == _loading_scene_shader_generic)
		{
			/* (its first map: its stages' combiners are not drawn) */
			struct loading_scene_generic_map const *map = definition->maps.address;

			loading_scene_map(builder, model, part, map->map.index, LOADING_SCENE_MAP_CURRENT,
				map->map_u_scale, map->map_v_scale, map->map_u_offset, map->map_v_offset);
		}
		else
		{
			for (index = 0; index < MIN(definition->maps.count, (long)LOADING_SCENE_PART_MAPS); index++)
			{
				struct loading_scene_chicago_map const *map = TAG_BLOCK_GET_ELEMENT(&definition->maps, index,
					struct loading_scene_chicago_map);

				loading_scene_map(builder, model, part, map->map.index, map->color_function,
					map->map_u_scale, map->map_v_scale, map->map_u_offset, map->map_v_offset);
			}
		}
		break;
	}
	default:
		return FALSE;
	}
	return TRUE;
}

/* a model part onto the block, its vertices moved by `matrices` (one for
each of the model's nodes; NULL: as they are) */
static void loading_scene_add_part(
	struct loading_scene_builder *builder,
	struct model const *model,
	struct loading_scene_model_geometry_part const *source,
	uint32_t layer,
	real_matrix4x3 const *matrices,
	long node_count)
{
	struct vertex_buffer const *vertex_buffer = &source->vertex_buffer;
	struct triangle_buffer const *triangle_buffer = &source->triangle_buffer;
	struct loading_scene_part part;
	byte *vertices = NULL;
	BYTE *indices = NULL;
	long index_count;
	long index;
	long stride;
	unsigned long vertex_offset;
	unsigned long triangle_offset;
	unsigned long triangle_count = 0;
	struct loading_scene_model_shader_reference const *reference;

	if (builder->part_count >= LOADING_SCENE_MAXIMUM_PARTS ||
		TEST_FLAG(source->flags, _loading_scene_part_stripped_bit) ||
		!VALID_INDEX(source->shader_index, model->shaders.count) ||
		!vertex_buffer->hardware_format || vertex_buffer->count <= 0 ||
		!triangle_buffer->hardware_format || triangle_buffer->count <= 0)
	{
		return;
	}
	if (vertex_buffer->type == _rasterizer_vertex_type_model_compressed)
		stride = sizeof(struct model_vertex_compressed);
	else if (vertex_buffer->type == _rasterizer_vertex_type_model_uncompressed)
		stride = sizeof(struct model_vertex_uncompressed);
	else
		return;
	if (triangle_buffer->type == 1)
		index_count = triangle_buffer->count + 2; /* a strip */
	else if (triangle_buffer->type == 0)
		index_count = triangle_buffer->count * 3; /* a list */
	else
		return;
	if (vertex_buffer->count > (long)LOADING_SCENE_MAXIMUM_VERTICES ||
		builder->vertex_total + vertex_buffer->count > 4 * LOADING_SCENE_MAXIMUM_VERTICES)
	{
		return;
	}
	memset(&part, 0, sizeof(part));
	part.layer = layer;
	reference = TAG_BLOCK_GET_ELEMENT(&model->shaders, source->shader_index,
		struct loading_scene_model_shader_reference);
	if (!loading_scene_shader(builder, model, reference->shader.index, &part))
		return;
	part.matrix[0][0] = part.matrix[1][1] = part.matrix[2][2] = 1.f;

	IDirect3DVertexBuffer8_Lock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format, 0, 0, &vertices, D3DLOCK_READONLY);
	IDirect3DIndexBuffer8_Lock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format, 0, 0, &indices, D3DLOCK_READONLY);
	if (vertices && indices)
	{
		struct loading_scene_vertex *out;
		unsigned short const *list = (unsigned short const *)indices;

		vertex_offset = loading_scene_reserve(builder, (unsigned long)vertex_buffer->count * sizeof(*out), 4);
		out = builder->failed ? NULL : (struct loading_scene_vertex *)(builder->data + vertex_offset);
		for (index = 0; out && index < vertex_buffer->count; index++, out++)
		{
			real_point3d position;
			real_vector3d normal;
			real_point2d texcoord;
			long node;

			if (vertex_buffer->type == _rasterizer_vertex_type_model_compressed)
			{
				struct model_vertex_compressed const *in = (struct model_vertex_compressed const *)(vertices + index * stride);

				position = in->position;
				normal = uncompress_int32_to_real_vector3d(in->normal);
				texcoord.x = ((real)in->texcoord.x * 2.f + 1.f) * (1.f / 65535.f);
				texcoord.y = ((real)in->texcoord.y * 2.f + 1.f) * (1.f / 65535.f);
				node = in->nodes[0] / 3;
			}
			else
			{
				struct model_vertex_uncompressed const *in = (struct model_vertex_uncompressed const *)(vertices + index * stride);

				position = in->position;
				normal = in->normal;
				texcoord = in->texcoord;
				node = in->nodes[0];
			}
			if (matrices && node_count > 0)
			{
				real_matrix4x3 const *matrix = &matrices[VALID_INDEX(node, node_count) ? node : 0];

				matrix4x3_transform_point(matrix, &position, &position);
				matrix4x3_transform_vector(matrix, &normal, &normal);
			}
			out->position[0] = position.x;
			out->position[1] = position.y;
			out->position[2] = position.z;
			out->normal[0] = normal.i;
			out->normal[1] = normal.j;
			out->normal[2] = normal.k;
			out->texcoord[0] = texcoord.x;
			out->texcoord[1] = texcoord.y;
		}

		/* its triangles: a strip's every one (turning, its degenerate ones
		left out), or a list's */
		triangle_offset = loading_scene_reserve(builder, (unsigned long)index_count * 6, 4);
		for (index = 0; !builder->failed && index + 2 < index_count && triangle_count < LOADING_SCENE_MAXIMUM_TRIANGLES;
			index += (triangle_buffer->type == 1) ? 1 : 3)
		{
			unsigned short a = list[index];
			unsigned short b = list[index + 1];
			unsigned short c = list[index + 2];
			uint16_t *triangle = (uint16_t *)(builder->data + triangle_offset) + triangle_count * 3;

			if (a == b || b == c || a == c ||
				a >= vertex_buffer->count || b >= vertex_buffer->count || c >= vertex_buffer->count)
			{
				continue;
			}
			if (triangle_buffer->type == 1 && (index & 1))
			{
				unsigned short swap = b;

				b = c;
				c = swap;
			}
			triangle[0] = a;
			triangle[1] = b;
			triangle[2] = c;
			triangle_count++;
		}
		if (!builder->failed && triangle_count)
		{
			/* (the triangles' room left unused is given back) */
			builder->bytes = triangle_offset + triangle_count * 6;
			part.vertex_offset = (uint32_t)vertex_offset;
			part.vertex_count = (uint32_t)vertex_buffer->count;
			part.triangle_offset = (uint32_t)triangle_offset;
			part.triangle_count = (uint32_t)triangle_count;
			builder->parts[builder->part_count++] = part;
			builder->vertex_total += vertex_buffer->count;
		}
	}
	if (vertices)
		IDirect3DVertexBuffer8_Unlock((IDirect3DVertexBuffer8 *)vertex_buffer->hardware_format);
	if (indices)
		IDirect3DIndexBuffer8_Unlock((IDirect3DIndexBuffer8 *)triangle_buffer->hardware_format);
}

/* a model's most detailed geometry, in each region's permutation
(`permutations`, NULL: each region's first) */
static void loading_scene_add_model(
	struct loading_scene_builder *builder,
	long model_index,
	byte const *permutations,
	uint32_t layer,
	real_matrix4x3 const *matrices,
	long node_count)
{
	struct model *model = model_index != NONE ? model_definition_get(model_index) : NULL;
	short region_index;

	if (!model || !model->regions.address || model->geometries.count <= 0 || !model->geometries.address ||
		!model->shaders.address)
	{
		return;
	}
	for (region_index = 0; region_index < MIN(model->regions.count, MAXIMUM_REGIONS_PER_OBJECT); region_index++)
	{
		struct model_region const *region = TAG_BLOCK_GET_ELEMENT(&model->regions, region_index, struct model_region);
		short permutation_index = permutations ? permutations[region_index] : 0;
		struct model_region_permutation const *permutation;
		short geometry_index = NONE;
		short level;

		if (!VALID_INDEX(permutation_index, region->permutations.count) || !region->permutations.address)
			continue;
		permutation = TAG_BLOCK_GET_ELEMENT(&region->permutations, permutation_index, struct model_region_permutation);
		for (level = LOADING_SCENE_DETAIL_LEVELS - 1; level >= 0 && !VALID_INDEX(geometry_index, model->geometries.count); level--)
			geometry_index = permutation->geometry_indices[level];
		if (!VALID_INDEX(geometry_index, model->geometries.count))
			continue;
		{
			struct loading_scene_model_geometry const *geometry = TAG_BLOCK_GET_ELEMENT(
				&model->geometries, geometry_index, struct loading_scene_model_geometry);
			long part_index;

			if (geometry->parts.count <= 0 || geometry->parts.count > MAXIMUM_PARTS_PER_MODEL_GEOMETRY ||
				!geometry->parts.address)
			{
				continue;
			}
			for (part_index = 0; part_index < geometry->parts.count; part_index++)
			{
				loading_scene_add_part(builder, model,
					TAG_BLOCK_GET_ELEMENT(&geometry->parts, part_index, struct loading_scene_model_geometry_part),
					layer, matrices, node_count);
			}
		}
	}
}

/* the scenario's first sky's model (the presenter draws its planets alone
behind the ring, as the loading picture has one, not its stars or galaxy:
loading_ring_set), and toward its first light (its sun, which lights the
ring) */
static void loading_scene_add_light(
	struct loading_scene_builder *builder,
	struct scenario *scenario)
{
	struct loading_scene_sky const *sky;

	if (!scenario->sky_references.count || !scenario->sky_references.address)
		return;
	sky = (struct loading_scene_sky const *)scenario_get_sky(0);
	if (!sky)
		return;
	loading_scene_add_model(builder, sky->model.index, NULL, LOADING_SCENE_LAYER_SKY, NULL, 0);
	if (sky->lights.count > 0 && sky->lights.address)
	{
		struct loading_scene_sky_light const *light = sky->lights.address;
		real_vector3d direction;

		vector3d_from_euler_angles2d(&direction, &light->direction);
		if (valid_real(direction.i) && valid_real(direction.j) && valid_real(direction.k))
			builder->light_direction = direction;
	}
}

/* the scenario's objects (its scenery: the ring), where the game has them */
static void loading_scene_add_objects(
	struct loading_scene_builder *builder)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long objects = 0;

	object_iterator_new(&iterator, _object_mask_scenery, 0);
	while ((object = object_iterator_next(&iterator)) != NULL && objects < LOADING_SCENE_OBJECTS)
	{
		struct object_definition *definition = object_definition_get(object->definition_index);
		long model_index = definition ? definition->object.model.index : NONE;
		struct model *model = model_index != NONE ? model_definition_get(model_index) : NULL;
		real_matrix4x3 matrices[MAXIMUM_NODES_PER_MODEL];
		real_matrix4x3 const *nodes;
		long node_count;
		long node;

		if (!model || !model->nodes.address || model->nodes.count <= 0)
			continue;
		nodes = object_get_node_matrices(iterator.index);
		if (!nodes)
			continue;
		node_count = MIN(model->nodes.count, MAXIMUM_NODES_PER_MODEL);
		/* (a vertex is in the model's space: each node's matrix after its
		default inverse) */
		for (node = 0; node < node_count; node++)
		{
			struct model_node const *model_node = TAG_BLOCK_GET_ELEMENT(&model->nodes, node, struct model_node);

			matrix4x3_multiply(&nodes[node], &model_node->runtime_default_inverse_matrix, &matrices[node]);
		}
		loading_scene_add_model(builder, model_index, object->object.region_permutations,
			LOADING_SCENE_LAYER_WORLD, matrices, node_count);
		objects++;
	}
}

/* ---------- globals */

/* when the level's first picture was ready (system_milliseconds), and
whether its eyes still come up out of the dark (VR) */
static unsigned long loading_scene_ready_time;
static boolean loading_scene_fading = FALSE;

/* ---------- public code */

/* the level's first picture is ready, its loading screen gone dark */
void halo_loading_fade_begin(
	void)
{
	loading_scene_ready_time = system_milliseconds();
	loading_scene_fading = TRUE;
}

#ifdef HALO_VR
/* (port/linux/src/xgpu_stream_encode.c) */
void halo_vr_eye_tint(float red, float green, float blue);

/* the eye pass, after the game's screen flash (render.c): the level's
first pictures faded up out of the dark over LOADING_SCENE_FADE_IN
milliseconds, as a load ends, so that the world does not appear all at
once about the head (the HUD's layer stays lit) */
void vr_loading_fade_draw(
	void)
{
	unsigned long elapsed;
	real t;

	if (!loading_scene_fading)
		return;
	elapsed = system_milliseconds() - loading_scene_ready_time;
	if (elapsed >= LOADING_SCENE_FADE_IN)
	{
		loading_scene_fading = FALSE;
		return;
	}
	t = (real)elapsed / (real)LOADING_SCENE_FADE_IN;
	t = t * t * (3.f - 2.f * t);
	halo_vr_eye_tint(t, t, t);
}
#endif

void halo_loading_scene(
	void)
{
	struct loading_scene_builder *builder;
	struct loading_scene_header *header;
	struct scenario *scenario = global_scenario_try_and_get();
	struct observer_result const *camera;
	boolean world = FALSE;
	unsigned long index;

	if (!scenario || scenario->type != _scenario_type_main_menu)
		return;
	/* (the game's allocator: no calloc of its own) */
	builder = malloc(sizeof(*builder));
	if (!builder)
		return;
	memset(builder, 0, sizeof(*builder));
	/* (the header first; written last, as the block may move as it grows) */
	loading_scene_reserve(builder, sizeof(*header), 4);
	loading_scene_add_light(builder, scenario);
	loading_scene_add_objects(builder);
	for (index = 0; index < builder->part_count; index++)
		world |= builder->parts[index].layer == LOADING_SCENE_LAYER_WORLD;
	if (!builder->failed && world)
	{
		unsigned long parts = loading_scene_reserve(builder, builder->part_count * sizeof(struct loading_scene_part), 4);
		unsigned long textures = loading_scene_reserve(builder,
			builder->texture_count * sizeof(struct loading_scene_texture), 4);

		if (!builder->failed)
		{
			header = (struct loading_scene_header *)builder->data;
			header->magic = LOADING_SCENE_MAGIC;
			header->version = LOADING_SCENE_VERSION;
			header->bytes = (uint32_t)builder->bytes;
			header->part_count = (uint32_t)builder->part_count;
			header->texture_count = (uint32_t)builder->texture_count;
			header->part_offset = (uint32_t)parts;
			header->texture_offset = (uint32_t)textures;
			memcpy(builder->data + parts, builder->parts, builder->part_count * sizeof(struct loading_scene_part));
			memcpy(builder->data + textures, builder->textures,
				builder->texture_count * sizeof(struct loading_scene_texture));
			header->light_direction[0] = builder->light_direction.i;
			header->light_direction[1] = builder->light_direction.j;
			header->light_direction[2] = builder->light_direction.k;
			camera = observer_get_camera(0);
			if (camera)
			{
				header->camera_position[0] = camera->position.x;
				header->camera_position[1] = camera->position.y;
				header->camera_position[2] = camera->position.z;
				header->camera_forward[0] = camera->forward.i;
				header->camera_forward[1] = camera->forward.j;
				header->camera_forward[2] = camera->forward.k;
				header->camera_up[0] = camera->up.i;
				header->camera_up[1] = camera->up.j;
				header->camera_up[2] = camera->up.k;
				header->camera_field_of_view = camera->field_of_view;
				header->camera_velocity[0] = camera->velocity.i;
				header->camera_velocity[1] = camera->velocity.j;
				header->camera_velocity[2] = camera->velocity.k;
			}
			if (loading_scene_validate(builder->data, builder->bytes))
				xgpu_backend_loading_scene(builder->data, builder->bytes);
		}
	}
	free(builder->data);
	free(builder);
}
