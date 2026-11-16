/*
XGPU_METAL_PIPELINE.H

What a draw's render pipeline state is made of, and the Metal descriptor
made from it (xgpu_metal_pipeline.m): shared by the backend (xgpu_metal.m,
xgpu_metal_stream.m), which makes the states as draws need them, and the
tool that makes them ahead of time from the renderer's manifest when the
app is built (xgpu_metal_prebuild.m), so that both describe a pipeline the
same way: a pipeline the tool put in the app's archive is one the backend
finds there. What uses Metal's types is Objective-C's alone; the rest C's
too.
*/

#ifndef __HALO_MACOS_XGPU_METAL_PIPELINE_H
#define __HALO_MACOS_XGPU_METAL_PIPELINE_H

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

#include "xgpu_metal.h"

#include <stdint.h>

struct xgpu_stream_pipeline_key;
struct xgpu_stream_sampler;
struct xgpu_stream_vertex_element;

#ifdef __OBJC__

/* ---------- formats */

struct xgpu_metal_format_information
{
	MTLPixelFormat pixel_format;
	/* bytes a pixel, or a block's (compressed: 4x4 pixels) */
	uint8_t bytes;
	uint8_t compressed;
	uint8_t depth, stencil;
	MTLTextureSwizzleChannels swizzle;
	uint8_t swizzled;
};

/* an XGPU_METAL_FORMAT_*'s (NONE's for one there is not) */
const struct xgpu_metal_format_information *xgpu_metal_format_information(uint32_t format);

/* ---------- the interface's values in Metal's */

MTLCompareFunction xgpu_metal_compare_function(uint32_t value);
MTLBlendFactor xgpu_metal_blend_factor(uint32_t value);
MTLBlendOperation xgpu_metal_blend_operation(uint32_t value);
MTLStencilOperation xgpu_metal_stencil_operation(uint32_t value);
MTLVertexFormat xgpu_metal_vertex_format(uint32_t format);

#endif

/* ---------- render pipeline states */

/* what a render pipeline state is made from: the pipeline's shaders,
layout, blend and targets (the rest is depth-stencil or encoder state).
Zeroed, then filled. */
struct xgpu_metal_pipeline_state_key
{
	uint32_t vertex_shader, pixel_shader;
	struct xgpu_metal_vertex_attribute attributes[16];
	uint16_t strides[XGPU_METAL_STREAM_COUNT];
	uint8_t blend, blend_source, blend_destination, blend_operation, color_mask;
	uint8_t color_format, depth_format, samples;
};

void xgpu_metal_pipeline_state_key_make(const struct xgpu_metal_pipeline *pipeline,
	struct xgpu_metal_pipeline_state_key *key);
#ifdef __OBJC__
/* the descriptor of a key's state, but for its functions (the vertex and
fragment functions of its shaders') */
MTLRenderPipelineDescriptor *xgpu_metal_pipeline_descriptor(const struct xgpu_metal_pipeline_state_key *key);
#endif
/* a state a binary archive can hold: one that writes a colour (Metal makes
one that does not without its fragment stage, which the archiver does not
write) */
int xgpu_metal_pipeline_archivable(const struct xgpu_metal_pipeline_state_key *key);

/* a clear's pipeline (xgpu_metal.m's clears drawn as quads): the targets'
formats, their samples, and the colour's channels written
(XGPU_METAL_CLEAR_RED...: 0 none) */
struct xgpu_metal_clear_key
{
	uint8_t color_format, depth_format, samples, parts;
};

#ifdef __OBJC__
MTLRenderPipelineDescriptor *xgpu_metal_clear_descriptor(const struct xgpu_metal_clear_key *key);
/* the presentation's (host_metal.m) */
MTLRenderPipelineDescriptor *xgpu_metal_present_descriptor(void);

/* the functions of the clears' and the presentation's pipelines (built
into the app's library, and compiled from this source where there is none,
as the tests run) */
extern NSString *const xgpu_metal_fixed_source;
#endif

/* ---------- the command stream's pipelines (xgpu_metal_stream.m) */

/* the fixed-function part of a pipeline key's pipeline (zeroed, then
filled: no attributes and no shaders) */
void xgpu_metal_pipeline_from_key(const struct xgpu_stream_pipeline_key *key, struct xgpu_metal_pipeline *pipeline);
/* a vertex layout's attributes (each input no element feeds read from its
register's value); returns the inputs read as integers (bit n, input n) */
uint16_t xgpu_metal_layout_attributes(const struct xgpu_stream_vertex_element *elements, uint32_t count,
	struct xgpu_metal_vertex_attribute attributes[16]);
/* immediate mode's layout for the registers written (a bit each): each a
vec4 in stream 0, in order, the rest from their values; returns how many
were written */
uint32_t xgpu_metal_layout_immediate(uint32_t written, struct xgpu_metal_vertex_attribute attributes[16],
	uint16_t strides[XGPU_METAL_STREAM_COUNT]);
/* a texture stage's axes (bit 0 u, 1 v, 2 w) whose border colour the
pixel shader draws (every axis addressed by the border, of a 2D or 3D
texture: whatever the colour, which the uniforms hold): the stage's source
(XGPU_SOURCE_*), the shader's sampler type (_xgpu_sampler_*) and the
sampler */
uint8_t xgpu_metal_border_axes(uint8_t source, uint8_t sampler_type, const struct xgpu_stream_sampler *sampler);

#endif
