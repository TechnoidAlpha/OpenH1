/*
XGPU_STREAM_DESCRIBE.C

The interface's pipeline keys and pixel programs (xgpu_backend.h,
xgpu_shader.h) as the command stream's fixed-width structures
(xgpu_stream.h), and a pixel shader key back from them: the stream's
encoder sends them so (xgpu_stream_encode.c), the renderer's manifest
records and reads them so (xgpu_manifest.c), and the tools that build a
backend's shaders ahead of time decode them as the host does
(xgpu_stream_describe.h).
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_shader.h"
#include "xgpu_stream.h"
#include "xgpu_stream_describe.h"

#include <stdint.h>
#include <string.h>

void xgpu_stream_pixel_key_describe(const struct nv2a_pixel_shader_key *key, struct xgpu_stream_pixel_key *wire)
{
	int index;

	for (index = 0; index < XGPU_STREAM_COMBINER_STATES; index++)
		wire->combiner_state[index] = (uint32_t)key->combiner_state[index];
	wire->texture_modes = (uint32_t)key->texture_modes;
	memcpy(wire->sampler_type, key->sampler_type, 4);
	memcpy(wire->alpha_kill, key->alpha_kill, 4);
	memcpy(wire->color_sign, key->color_sign, 4);
	wire->alpha_test_function = (uint32_t)key->alpha_test_function;
	wire->fog_enable = key->fog_enable;
	wire->fog_table_mode = key->fog_table_mode;
	wire->count_samples = key->count_samples;
	wire->coverage_alpha = key->coverage_alpha;
	wire->point_threshold = key->point_threshold;
	wire->per_pixel_lighting = key->per_pixel_lighting;
	wire->alpha_test_samples = key->alpha_test_samples;
	wire->multiview = key->multiview;
	wire->source_blend = key->source_blend;
	wire->dynamic_lights = key->dynamic_lights;
	wire->vr_effect = key->vr_effect;
	wire->ambient_occlusion = key->ambient_occlusion;
	wire->hdr = key->hdr;
}

void xgpu_stream_pipeline_key_describe(const struct xgpu_pipeline_key *key, struct xgpu_stream_pipeline_key *wire)
{
	memset(wire, 0, sizeof(*wire));
	wire->vertex_program[0] = (uint32_t)key->vertex_program;
	wire->vertex_program[1] = (uint32_t)(key->vertex_program >> 32);
	wire->packed_attributes = (uint32_t)key->packed_attributes;
	wire->stencil_read_mask = (uint32_t)key->stencil_read_mask;
	wire->stencil_write_mask = (uint32_t)key->stencil_write_mask;
	wire->immediate = key->immediate;
	wire->lighting = key->lighting;
	wire->views = key->views;
	wire->blend = key->blend;
	wire->blend_source = key->blend_source;
	wire->blend_destination = key->blend_destination;
	wire->blend_operation = key->blend_operation;
	wire->color_mask = key->color_mask;
	wire->depth_test = key->depth_test;
	wire->depth_function = key->depth_function;
	wire->depth_write = key->depth_write;
	wire->stencil_test = key->stencil_test;
	wire->stencil_function = key->stencil_function;
	wire->stencil_fail = key->stencil_fail;
	wire->stencil_depth_fail = key->stencil_depth_fail;
	wire->stencil_pass = key->stencil_pass;
	wire->cull = key->cull;
	wire->front_counter_clockwise = key->front_counter_clockwise;
	wire->cull_front = key->cull_front;
	wire->fill = key->fill;
	wire->depth_bias = key->depth_bias;
	wire->color_format = key->color_format;
	wire->depth_format = key->depth_format;
	wire->samples = key->samples;
	xgpu_stream_pixel_key_describe(&key->pixel, &wire->pixel);
}

void xgpu_stream_pixel_program_describe(const struct xgpu_pixel_program *program, struct xgpu_stream_pixel_program *wire)
{
	memset(wire, 0, sizeof(*wire));
	memcpy(wire->stages, program->stages, sizeof(wire->stages));
	wire->mux_msb = program->mux_msb;
	memcpy(wire->final, &program->final, sizeof(wire->final));
	wire->fog = program->fog;
	wire->fog_mode = program->fog_mode;
	wire->combiner_count = (uint32_t)program->combiner_count;
	memcpy(wire->combiners, program->combiners, sizeof(wire->combiners));
	wire->alpha_test = (uint32_t)program->alpha_test;
	wire->alpha_test_samples = program->alpha_test_samples;
	wire->count_samples = program->count_samples;
	wire->coverage_alpha = program->coverage_alpha;
	wire->point_threshold = program->point_threshold;
	wire->per_pixel_lighting = program->per_pixel_lighting;
	wire->dynamic_lights = program->dynamic_lights;
	wire->multiview = program->multiview;
	wire->source_blend = program->source_blend;
	wire->ambient_occlusion = program->ambient_occlusion;
	wire->hdr = program->hdr;
}

void xgpu_stream_pixel_key_restore(const struct xgpu_stream_pixel_key *wire, struct nv2a_pixel_shader_key *key)
{
	int index;

	memset(key, 0, sizeof(*key));
	for (index = 0; index < XGPU_STREAM_COMBINER_STATES; index++)
		key->combiner_state[index] = wire->combiner_state[index];
	key->texture_modes = wire->texture_modes;
	memcpy(key->sampler_type, wire->sampler_type, 4);
	memcpy(key->alpha_kill, wire->alpha_kill, 4);
	memcpy(key->color_sign, wire->color_sign, 4);
	key->alpha_test_function = wire->alpha_test_function;
	key->fog_enable = wire->fog_enable;
	key->fog_table_mode = wire->fog_table_mode;
	key->count_samples = wire->count_samples;
	key->coverage_alpha = wire->coverage_alpha;
	key->point_threshold = wire->point_threshold;
	key->per_pixel_lighting = wire->per_pixel_lighting;
	key->alpha_test_samples = wire->alpha_test_samples;
	key->multiview = wire->multiview;
	key->source_blend = wire->source_blend;
	key->dynamic_lights = wire->dynamic_lights;
	key->vr_effect = wire->vr_effect;
	key->ambient_occlusion = wire->ambient_occlusion;
	key->hdr = wire->hdr;
}
