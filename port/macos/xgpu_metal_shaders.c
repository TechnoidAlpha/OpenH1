/*
XGPU_METAL_SHADERS.C

The command stream's pipelines' shaders, written by the MSL emitter
(port/linux/src/xgpu_msl.c, built into the host) and compiled by the Metal
backend: the provider xgpu_metal_stream.m asks (xgpu_metal_stream.h). And
the uniforms the emitter's shaders read (struct xgpu_msl_uniforms), made
of the front end's and the rest of the draw's state.

The stream sends the vertex programs and pixel programs as the guest's
structures' bytes (xgpu_stream.h): they become the host's structures
(xgpu_shader.h, whose unsigned longs are 8 bytes here) field by field.

The emitter's sources are compiled beside a stand-in for the platform
header (xgpu_metal_platform.h; tools/macos_build.py copies them away from
port/linux/src, whose platform.h is the guest's).

HALO_METAL_SHADERS=<folder> writes each shader's MSL there as it is made
(vs<hash>_<variant>.metal, ps_<hash>.metal). The pixel shaders' debugging
settings (debug.gpu_debug_expression, _texture0, _flat) are read from their
environment variables (HALO_GPU_DEBUG_EXPR, _T0, _FLAT): the host does not
read the game's settings file.
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_shader.h"
#include "xgpu_msl.h"
#include "xgpu_stream.h"
#include "xgpu_metal.h"
#include "xgpu_metal_stream.h"
#include "xgpu_metal_combiner.h"

#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(XGPU_METAL_BORDER_REGION == XGPU_BORDER_REGION, "a region's border bit");
_Static_assert(XGPU_METAL_BORDER_MSDF == XGPU_BORDER_MSDF, "the distance fields' border bit");
_Static_assert(XGPU_METAL_ADDRESS_MSDF == XGPU_ADDRESS_MSDF, "XGPU_ADDRESS_MSDF's number");

/* the stream's copies of the intermediate form's pieces are their bytes */
_Static_assert(sizeof(struct xgpu_vertex_instruction) == XGPU_STREAM_VERTEX_INSTRUCTION_SIZE, "vertex instruction");
_Static_assert(sizeof(struct xgpu_texture_stage) == XGPU_STREAM_TEXTURE_STAGE_SIZE, "texture stage");
_Static_assert(sizeof(struct xgpu_combiner_stage) == XGPU_STREAM_COMBINER_STAGE_SIZE, "combiner stage");
_Static_assert(sizeof(struct xgpu_final_combiner) == XGPU_STREAM_FINAL_COMBINER_SIZE, "final combiner");
_Static_assert(XGPU_VERTEX_PROGRAM_MAXIMUM == XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM, "vertex program");
/* the emitter's bindings are the backend's */
_Static_assert(XGPU_MSL_BUFFER_CONSTANTS == XGPU_METAL_BUFFER_VERTEX_CONSTANTS, "constants' buffer");
_Static_assert(XGPU_MSL_BUFFER_UNIFORMS == XGPU_METAL_BUFFER_DRAW_UNIFORMS, "uniforms' buffer");
_Static_assert(XGPU_METAL_BUFFER_STREAM(XGPU_METAL_STREAM_COUNT - 1) < XGPU_MSL_BUFFER_CONSTANTS, "streams' buffers");
_Static_assert(XGPU_MSL_BUFFER_DYNAMIC_LIGHTS == XGPU_METAL_BUFFER_DYNAMIC_LIGHTS &&
	XGPU_MSL_BUFFER_DYNAMIC_LIGHT_CELLS == XGPU_METAL_BUFFER_DYNAMIC_LIGHT_CELLS &&
	XGPU_MSL_BUFFER_DYNAMIC_LIGHT_WORLD == XGPU_METAL_BUFFER_DYNAMIC_LIGHT_WORLD, "dynamic lights' buffers");
_Static_assert(XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES != XGPU_MSL_BUFFER_DYNAMIC_LIGHTS &&
	XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES != XGPU_MSL_BUFFER_SAMPLES &&
	XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES != XGPU_MSL_BUFFER_DYNAMIC_LIGHT_CELLS &&
	XGPU_METAL_BUFFER_VIEW_UNIFORMS != XGPU_MSL_BUFFER_DYNAMIC_LIGHT_CELLS &&
	XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES > XGPU_MSL_BUFFER_UNIFORMS, "constant inputs' buffer");
_Static_assert(XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE == XGPU_DYNAMIC_LIGHTS_CELLS_BYTES, "dynamic lights' cells");
_Static_assert(XGPU_METAL_STREAM_DYNAMIC_LIGHT_WORLD_TEXELS == XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS, "dynamic lights' world");
_Static_assert(sizeof(struct xgpu_metal_halo_uniforms) == sizeof(struct xgpu_msl_halo_uniforms) &&
	offsetof(struct xgpu_metal_halo_uniforms, depth_parameters) == offsetof(struct xgpu_msl_halo_uniforms, depth_parameters) &&
	offsetof(struct xgpu_metal_halo_uniforms, screen_offset) == offsetof(struct xgpu_msl_halo_uniforms, screen_offset),
	"the glows' uniforms");
_Static_assert(XGPU_MSL_HALO_MIDDLE == 0 && XGPU_MSL_HALO_COLOR == 1 && XGPU_MSL_HALO_CORNER == 2 &&
	XGPU_MSL_HALO_MIDDLE < 16 && XGPU_MSL_HALO_COLOR < 16 && XGPU_MSL_HALO_CORNER < 16, "glows' attributes");
_Static_assert(XGPU_METAL_TEXTURE_STAGE(3) == 3 && XGPU_METAL_SAMPLER_STAGE(3) == 3, "stages' textures and samplers");
_Static_assert(XGPU_METAL_VERTEX_CONSTANT_COUNT == XGPU_VERTEX_CONSTANT_COUNT, "vertex constants");
_Static_assert(XGPU_MSL_TEXTURE_WORLD_AO_PIXELS == XGPU_METAL_TEXTURE_WORLD_AO_PIXELS &&
	XGPU_MSL_TEXTURE_WORLD_AO_OBJECTS == XGPU_METAL_TEXTURE_WORLD_AO_OBJECTS &&
	XGPU_MSL_BUFFER_WORLD_AO == XGPU_METAL_BUFFER_WORLD_AO && XGPU_MSL_WORLD_AO_PASS_HISTORY == XGPU_METAL_WORLD_AO_PASS_HISTORY &&
	XGPU_MSL_WORLD_AO_PASS_CASCADES == XGPU_METAL_WORLD_AO_PASS_CASCADES &&
	XGPU_MSL_TEXTURE_WORLD_SDF == XGPU_METAL_TEXTURE_WORLD_SDF &&
	XGPU_MSL_TEXTURE_WORLD_SDF_TABLE == XGPU_METAL_TEXTURE_WORLD_SDF_TABLE, "the world's occlusion's bindings");
/* (setVertexBytes' limit) */
_Static_assert(sizeof(struct xgpu_msl_uniforms) <= 4096 && sizeof(struct xgpu_msl_halo_uniforms) <= 4096,
	"uniforms' size");

/* ---------- the stream's records as the host's structures */

static void vertex_program_convert(const struct xgpu_stream_vertex_program *record, struct xgpu_vertex_program *program)
{
	uint32_t count = record->instruction_count;

	memset(program, 0, sizeof(*program));
	if (count > XGPU_VERTEX_PROGRAM_MAXIMUM)
		count = XGPU_VERTEX_PROGRAM_MAXIMUM;
	program->instruction_count = count;
	memcpy(program->instructions, record->instructions, (size_t)count * sizeof(program->instructions[0]));
	program->hash = record->hash[0] | ((unsigned long long)record->hash[1] << 32);
	program->projects = (unsigned char)(record->projects != 0);
}

static void vertex_lighting_convert(const struct xgpu_stream_vertex_lighting *record,
	struct nv2a_vertex_lighting *lighting)
{
	memset(lighting, 0, sizeof(*lighting));
	if (!record)
		return;
	lighting->lights = record->lights;
	lighting->normal_instruction = record->normal_instruction;
	lighting->normal_register = record->normal_register;
	lighting->position_instruction = record->position_instruction;
	lighting->position_register = record->position_register;
	lighting->world_found = record->world_found != 0;
	lighting->world_instruction = record->world_instruction;
	lighting->world_register = record->world_register;
}

static void pixel_program_convert(const struct xgpu_stream_pixel_program *wire, struct xgpu_pixel_program *program)
{
	memset(program, 0, sizeof(*program));
	memcpy(program->stages, wire->stages, sizeof(program->stages));
	program->combiner_count = wire->combiner_count <= 8 ? wire->combiner_count : 8;
	memcpy(program->combiners, wire->combiners, sizeof(program->combiners));
	program->mux_msb = wire->mux_msb;
	memcpy(&program->final, wire->final, sizeof(program->final));
	program->fog = wire->fog;
	program->fog_mode = wire->fog_mode;
	program->alpha_test = wire->alpha_test;
	program->alpha_test_samples = wire->alpha_test_samples;
	program->count_samples = wire->count_samples;
	program->coverage_alpha = wire->coverage_alpha;
	program->point_threshold = wire->point_threshold;
	program->per_pixel_lighting = wire->per_pixel_lighting;
	program->dynamic_lights = wire->dynamic_lights;
	program->multiview = wire->multiview;
	program->source_blend = wire->source_blend;
	program->ambient_occlusion = wire->ambient_occlusion;
	program->hdr = wire->hdr;
}

/* ---------- the shaders */

static const char *dump_folder(void)
{
	static int read;
	static const char *folder;

	if (!read)
	{
		folder = getenv("HALO_METAL_SHADERS");
		if (folder && !*folder)
			folder = NULL;
		read = 1;
	}
	return folder;
}

static void dump(const char *name, const char *source)
{
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s.metal", dump_folder(), name);
	if ((file = fopen(path, "w")) != NULL)
	{
		fputs(source, file);
		fclose(file);
	}
}

/* a word hash of bytes (the dumps' names) */
static uint32_t bytes_hash(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	uint32_t hash = 2166136261u;
	size_t index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 16777619u;
	return hash;
}

/* the pixel shaders' debugging settings */
static void debug_options(struct xgpu_msl_options *options)
{
	static int read;
	static const char *expression;
	static BOOL texture0, flat;

	if (!read)
	{
		expression = getenv("HALO_GPU_DEBUG_EXPR");
		texture0 = getenv("HALO_GPU_DEBUG_T0") != NULL;
		flat = getenv("HALO_GPU_DEBUG_FLAT") != NULL;
		read = 1;
	}
	options->debug_expression = expression;
	options->debug_texture0 = texture0;
	options->debug_flat = flat;
}

/* a source's hash (its function's name) */
static unsigned long long text_hash(const char *text, size_t size)
{
	unsigned long long hash = 14695981039346656037ULL;
	size_t index;

	for (index = 0; index < size; index++)
		hash = (hash ^ (unsigned char)text[index]) * 1099511628211ULL;
	return hash;
}

/* the emitter's function entry, named after its source's hash (64 bits:
the name is the source's identity in the app's library of thousands,
xgpu_metal_shader_create): the pipeline archive tells functions apart by
their names, and a pipeline whose two functions have another pipeline's
names (all the emitter's have one of two) is written without one of its
stages, which spoils the archive */
static uint32_t compile(char *source, const char *entry, const char *name, const uint32_t *constants)
{
	struct xgpu_text text = { 0 };
	char call[64], renamed[64];
	const char *at;
	uint32_t shader = 0;

	if (!source)
		return 0;
	snprintf(call, sizeof(call), " %s(", entry);
	snprintf(renamed, sizeof(renamed), "%s_%016llx", entry, text_hash(source, strlen(source)));
	at = strstr(source, call);
	if (at)
	{
		xgpu_text_append(&text, "%.*s %s(%s", (int)(at - source), source, renamed, at + strlen(call));
		if (text.buffer)
		{
			if (dump_folder())
				dump(name, text.buffer);
			shader = xgpu_metal_pixel_shader_create(text.buffer, text.length, renamed, constants);
		}
		free(text.buffer);
	}
	free(source);
	return shader;
}

static int shaders_make(void *context, const struct xgpu_stream_pipeline_define *definition,
	const struct xgpu_stream_vertex_program *record, const struct xgpu_stream_vertex_lighting *lighting_record,
	const struct xgpu_metal_stream_variant *variant, uint32_t *vertex_shader, uint32_t *pixel_shader)
{
	static const struct nv2a_vertex_lighting environment = { XGPU_ENVIRONMENT_LIGHTING };
	const struct xgpu_stream_pipeline_key *key = &definition->key;
	struct xgpu_vertex_program *program;
	struct xgpu_pixel_program pixel;
	struct nv2a_vertex_lighting model;
	const struct nv2a_vertex_lighting *lighting;
	struct xgpu_msl_options options;
	unsigned long packed = key->immediate ? 0 : key->packed_attributes;
	unsigned long integers = key->immediate ? 0 : variant->integer_attributes;
	char name[96];
	struct xgpu_metal_combiner combiner;
	uint32_t constants[XGPU_METAL_COMBINER_CONSTANTS];
	int have_constants;

	(void)context;
	*vertex_shader = *pixel_shader = 0;
	if (!record)
		return 0;
	/* (as the GL backend's: a model lighting program's normal and position,
	or a structure's lightmap pass's own) */
	vertex_lighting_convert(lighting_record, &model);
	/* (display.ambient_occlusion: lit for each vertex, the ambient light
	handed on apart) */
	model.split = key->lighting == XGPU_LIGHTING_MODEL_SPLIT;
	lighting = key->lighting == XGPU_LIGHTING_NONE ? NULL : key->lighting == XGPU_LIGHTING_ENVIRONMENT ? &environment :
		&model;
	memset(&options, 0, sizeof(options));
	memcpy(options.border, variant->border, sizeof(options.border));
	debug_options(&options);
	program = malloc(sizeof(*program));
	if (!program)
		return 0;
	vertex_program_convert(record, program);
	snprintf(name, sizeof(name), "vs%08x%08x_%lx_%lx_%u", record->hash[1], record->hash[0], packed, integers,
		key->lighting);
	*vertex_shader = compile(xgpu_msl_vertex_shader(program, packed, integers, lighting, &options), "xgpu_vertex", name,
		NULL);
	free(program);
	pixel_program_convert(&definition->pixel_program, &pixel);
	snprintf(name, sizeof(name), "ps_%08x_%02x%02x%02x%02x", bytes_hash(&definition->pixel_program,
		sizeof(definition->pixel_program)), options.border[0], options.border[1], options.border[2], options.border[3]);
	/* (one the app's library lacks drawn at once by the combiner's function,
	specialised for it, where it can: its configuration) */
	have_constants = xgpu_metal_combiner_draws(&pixel) && !options.debug_expression && !options.debug_texture0 &&
		!options.debug_flat;
	if (have_constants)
	{
		xgpu_metal_combiner_configure(&pixel, variant->border, &combiner);
		xgpu_metal_combiner_constants(&combiner, constants);
	}
	*pixel_shader = compile(xgpu_msl_pixel_shader(&pixel, &options), "xgpu_fragment", name,
		have_constants ? constants : NULL);
	return *vertex_shader && *pixel_shader;
}

/* ---------- the uniforms */

static void color_from_argb(uint32_t argb, float color[4])
{
	color[0] = (float)((argb >> 16) & 0xff) / 255.0f;
	color[1] = (float)((argb >> 8) & 0xff) / 255.0f;
	color[2] = (float)(argb & 0xff) / 255.0f;
	color[3] = (float)(argb >> 24) / 255.0f;
}

static uint32_t uniforms_make(void *context, const struct xgpu_metal_stream_uniform_inputs *inputs, void *bytes,
	uint32_t size)
{
	const struct xgpu_stream_draw_uniforms *from = inputs->uniforms;
	struct xgpu_msl_uniforms *to = bytes;
	int stage, light;

	(void)context;
	if (size < sizeof(*to))
		return 0;
	memset(to, 0, sizeof(*to));
	if (from)
	{
		memcpy(to->viewport_scale, from->viewport_scale, sizeof(to->viewport_scale));
		memcpy(to->viewport_offset, from->viewport_offset, sizeof(to->viewport_offset));
		to->point_size = from->point_size;
		to->screen_offset = from->screen_offset;
		to->alpha_reference = from->alpha_reference;
		to->depth_bias = inputs->depth_bias;
		memcpy(to->ps_c0, from->ps_c0, sizeof(to->ps_c0));
		memcpy(to->ps_c1, from->ps_c1, sizeof(to->ps_c1));
		memcpy(to->ps_final_c0, from->ps_final_c0, sizeof(to->ps_final_c0));
		memcpy(to->ps_final_c1, from->ps_final_c1, sizeof(to->ps_final_c1));
		memcpy(to->fog_color, from->fog_color, sizeof(to->fog_color));
		memcpy(to->fog_parameters, from->fog_parameters, sizeof(to->fog_parameters));
		memcpy(to->bump_matrix, from->bump_matrix, sizeof(to->bump_matrix));
		memcpy(to->bump_luminance, from->bump_luminance, sizeof(to->bump_luminance));
		memcpy(to->texture_scale, from->texture_scale, sizeof(to->texture_scale));
		memcpy(to->texture_lod_bias, from->texture_lod_bias, sizeof(to->texture_lod_bias));
		/* (display.ambient_occlusion "world": the draw's object) */
		to->world_ao[0] = from->world_ao[0];
		to->world_ao[1] = from->world_ao[1];
		to->world_ao[3] = from->world_ao[3];
		/* (the enhanced dynamic lights' tint, in linear light) */
		for (stage = 0; stage < 3; stage++)
			to->dynamic_light_tint[stage] = powf(from->ps_final_c0[stage], 2.2f);
		to->dynamic_light_tint[3] = 1.0f;
	}
	if (inputs->blend_color)
		memcpy(to->blend_constant, inputs->blend_color, sizeof(to->blend_constant));
	if (inputs->samplers)
	{
		for (stage = 0; stage < 4; stage++)
		{
			const struct xgpu_stream_sampler *sampler = &inputs->samplers[stage];

			color_from_argb(sampler->border_color, to->texture_border[stage]);
			to->texture_border_linear[stage] = sampler->magnify >= XGPU_FILTER_LINEAR ||
				sampler->minify >= XGPU_FILTER_LINEAR ? 1.0f : 0.0f;
		}
	}
	/* the per-pixel lighting's lights: vertex constants (xgpu_backend.h) */
	if (inputs->constants)
	{
		for (light = 0; light < XGPU_MODEL_LIGHT_COUNT; light++)
			memcpy(to->model_lights[light], inputs->constants[XGPU_MODEL_LIGHT_REGISTER(light)], sizeof(to->model_lights[0]));
	}
	return sizeof(*to);
}

/* ---------- the enhanced dynamic lights' glows */

static int halo_make(void *context, uint32_t *vertex_shader, uint32_t *pixel_shader)
{
	static const char *const entries[2] = { "xgpu_halo_vertex", "xgpu_halo_fragment" };
	uint32_t *shaders[2];
	int pixel;

	(void)context;
	shaders[0] = vertex_shader;
	shaders[1] = pixel_shader;
	for (pixel = 0; pixel < 2; pixel++)
	{
		char *source = xgpu_msl_halo_shader(pixel);

		*shaders[pixel] = 0;
		if (!source)
			return 0;
		if (dump_folder())
			dump(entries[pixel], source);
		/* (the app's library's, made ahead of time: tools/xgpu_metal_prebuild.c) */
		*shaders[pixel] = xgpu_metal_pixel_shader_create(source, strlen(source), entries[pixel], NULL);
		free(source);
	}
	return *vertex_shader && *pixel_shader;
}

/* ---------- display.ambient_occlusion "world" found once a pixel */

static int world_ao_pass_make(void *context, uint32_t *vertex_shader, uint32_t *pixel_shader)
{
	static const char *const entries[2] = { "xgpu_world_ao_vertex", "xgpu_world_ao_pass" };
	int pixel;

	(void)context;
	*vertex_shader = *pixel_shader = 0;
	for (pixel = 0; pixel < 2; pixel++)
	{
		char *source = xgpu_msl_world_ao_pass(pixel);

		if (!source)
			return 0;
		if (dump_folder())
			dump(entries[pixel], source);
		/* (the app's library's, made ahead of time: tools/xgpu_metal_prebuild.c) */
		*(pixel ? pixel_shader : vertex_shader) = xgpu_metal_pixel_shader_create(source, strlen(source), entries[pixel],
			NULL);
		free(source);
	}
	return *vertex_shader && *pixel_shader;
}

const struct xgpu_metal_stream_shaders *xgpu_metal_shaders(void)
{
	static const struct xgpu_metal_stream_shaders shaders = { NULL, shaders_make, uniforms_make, halo_make,
		world_ao_pass_make };

	return &shaders;
}
