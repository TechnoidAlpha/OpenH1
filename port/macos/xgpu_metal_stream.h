/*
XGPU_METAL_STREAM.H

The Metal backend as the command stream's backend table
(xgpu_metal_stream.m; port/android/host/host_render_stream.h).
*/

#ifndef __HALO_MACOS_XGPU_METAL_STREAM_H
#define __HALO_MACOS_XGPU_METAL_STREAM_H

#include <stdint.h>

struct host_render_backend;
struct xgpu_stream_draw_uniforms;
struct xgpu_stream_pipeline_define;
struct xgpu_stream_sampler;
struct xgpu_stream_vertex_lighting;
struct xgpu_stream_vertex_program;

/* the enhanced dynamic lights' cells' bytes (xgpu.h's grid, four words a
cell, and a slice of the clusters' masks past them), and the most texels of
their world (xgpu_metal_shaders.c checks them) */
#define XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE (32 * 32 * 17 * 16)
#define XGPU_METAL_STREAM_DYNAMIC_LIGHT_WORLD_TEXELS (240 * 1024)

/* the glows' uniforms (the MSL emitter's struct xgpu_msl_halo_uniforms,
xgpu_msl.h: xgpu_metal_shaders.c checks it) */
struct xgpu_metal_halo_uniforms
{
	float projection[6][4];
	float camera_axes[2][4];
	float viewport_scale[4];
	float viewport_offset[4];
	float depth_parameters[4];
	float screen_offset, unused[3];
};

/* the table (made once; xgpu_metal_initialize first) */
const struct host_render_backend *xgpu_metal_stream_backend(void);

/* what a pipeline's shaders depend on besides its definition, known only
at a draw: the inputs its vertex layout feeds as integers (unnormalized
shorts: bit n, input n; none in immediate mode), and for each texture
stage the axes (bit 0 u, 1 v, 2 w) whose border colour the pixel shader
draws (any axis addressed by the border: xgpu_metal_border_axes) */
struct xgpu_metal_stream_variant
{
	uint16_t integer_attributes;
	uint8_t border[4];
	uint8_t pad[2];
};

/* the draw's values the uniforms are made of: the front end's uniforms,
the blend colour, the 192 vertex constants (the per-pixel lighting's lights
are some of them), the four stages' samplers (their border colours) and the
constant depth bias the vertex shader adds to reversed depth
(xgpu_reversed_depth_bias) */
struct xgpu_metal_stream_uniform_inputs
{
	const struct xgpu_stream_draw_uniforms *uniforms;
	const float *blend_color;
	const float (*constants)[4];
	const struct xgpu_stream_sampler *samplers;
	float depth_bias;
};

/* Where a pipeline's shaders come from: make sets the MSL functions
(xgpu_metal_shader_create's handles) for the pipeline's vertex program in
its key's variant and the draw's, and its pixel program in the draw's, and
returns nonzero; program is the vertex program's record (NULL if none was
sent), lighting a model lighting program's (NULL if none). uniforms writes
the bytes both stages read as their uniforms (at most size) and returns
how many; NULL: the front end's uniforms as they are. halo sets the functions
of the enhanced dynamic lights' glows (the MSL emitter's
xgpu_msl_halo_shader, whose uniforms are struct xgpu_msl_halo_uniforms) and
returns nonzero; NULL: no glows. The MSL emitter's (xgpu_metal_shaders.c);
tests give their own. */
struct xgpu_metal_stream_shaders
{
	void *context;
	int (*make)(void *context, const struct xgpu_stream_pipeline_define *definition,
		const struct xgpu_stream_vertex_program *program, const struct xgpu_stream_vertex_lighting *lighting,
		const struct xgpu_metal_stream_variant *variant, uint32_t *vertex_shader, uint32_t *pixel_shader);
	uint32_t (*uniforms)(void *context, const struct xgpu_metal_stream_uniform_inputs *inputs, void *bytes,
		uint32_t size);
	int (*halo)(void *context, uint32_t *vertex_shader, uint32_t *pixel_shader);
	/* display.ambient_occlusion "world" found once a pixel: its pass's
	functions (xgpu_msl.h's xgpu_msl_world_ao_pass) */
	int (*world_ao_pass)(void *context, uint32_t *vertex_shader, uint32_t *pixel_shader);
};

void xgpu_metal_stream_set_shaders(const struct xgpu_metal_stream_shaders *shaders);

/* a target's picture written to path as a PNG; nonzero if written (the
render thread: host_render_record.h's shooter) */
int xgpu_metal_stream_shot(uint32_t target, const char *path);
/* (the render thread) a colour target's pixels as xgpu_metal_stream_shot
reads them: BGRA rows from the top, malloc'd; NULL if it has none */
unsigned char *xgpu_metal_stream_read(uint32_t target, uint32_t *width, uint32_t *height);
/* a target's texture handle (0: none: the tests', the replay's ID buffer;
the render thread) */
uint32_t xgpu_metal_stream_target_texture(uint32_t target);

/* the MSL emitter's provider (xgpu_metal_shaders.c) */
const struct xgpu_metal_stream_shaders *xgpu_metal_shaders(void);

#endif
