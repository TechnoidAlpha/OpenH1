/*
XGPU_MSL.H

Metal Shading Language from the shader intermediate form (xgpu_shader.h),
for the Metal backend (the native macOS app): xgpu_msl.c. The programs are
the SPIR-V emitter's (xgpu_spirv.c), spelled for Metal and its conventions:

- clip space is Direct3D's (z 0 to 1, the first row at the top): the
  position is the one the game's programs mean, nothing flipped;
- vertex attributes come through a vertex descriptor ([[stage_in]]): the
  attributes a program reads, at [[attribute(n)]] for v<n>. A D3DCOLOR is
  MTLVertexFormatUChar4Normalized_BGRA (the shader reads it as RGBA);
  NORMPACKED3 is MTLVertexFormatUInt, unpacked here; the unnormalized shorts
  (D3DVSDT_SHORT1-4) are MTLVertexFormatShort1-4, read as integers and
  converted here (Metal converts no integer format to float); everything
  else is read as float4, the missing components 0, 0, 0, 1. An attribute
  the program reads that the declaration lacks is the device's
  (D3DDevice_SetVertexData4f, 0, 0, 0, 1 at first): a buffer with
  MTLVertexStepFunctionConstant;
- the vertex constants (192 float4) are buffer XGPU_MSL_BUFFER_CONSTANTS of
  the vertex stage (setVertexBytes: 3 KB); the draw's other values, struct
  xgpu_msl_uniforms, buffer XGPU_MSL_BUFFER_UNIFORMS of both stages
  (setVertexBytes and setFragmentBytes);
- a texture and a sampler for each stage, [[texture(n)]] and [[sampler(n)]];
  Metal samplers have no LOD bias, so D3DTSS_MIPMAPLODBIAS is the lookup's
  (texture_lod_bias);
- border colors, which Metal's samplers hold three of (transparent black,
  opaque black and opaque white; the motion tracker's is 0x46000000): for
  every one alike, so that the pipeline does not depend on the color
  (xgpu_metal_border_axes), the sampler clamps to the edge and the shader
  mixes the stage's color in where the coordinates leave the texture
  (xgpu_msl_options border);
- the VR mode's views are not (the Mac app is flat): a key's multiview is
  drawn as none, a layered render target as its first layer.
*/

#ifndef __HALO_LINUX_XGPU_MSL_H
#define __HALO_LINUX_XGPU_MSL_H

/* the buffers' indices (the vertex streams are 0 to 15, the vertex
descriptor's): the enhanced dynamic lights' block (struct
xgpu_dynamic_lights_block) and their grid's cells (four words a cell, x
fastest, then y, then z: d3d8_device.c's mask), in buffers of their own (a
draw reads the frame's, wherever its ring put them) */
#define XGPU_MSL_BUFFER_CONSTANTS 16
#define XGPU_MSL_BUFFER_UNIFORMS 17
#define XGPU_MSL_BUFFER_DYNAMIC_LIGHTS 18
#define XGPU_MSL_BUFFER_SAMPLES 19
#define XGPU_MSL_BUFFER_DYNAMIC_LIGHT_CELLS 22
/* the enhanced dynamic lights' world (xgpu.h: the structure's BSP, texel t
the buffer's t), a buffer of its own: the cells' clusters' masks are the
cells' (their texture's slice past them, in its order) */
#define XGPU_MSL_BUFFER_DYNAMIC_LIGHT_WORLD 24
/* display.ambient_occlusion "world" (xgpu_stream.h): the structure's
distance field, its atlas of bricks (texture3d<float>, read through a
constexpr linear sampler clamped to its edge) and its table of cells
(texture3d<float>, RGBA8, read), and the window's block */
#define XGPU_MSL_TEXTURE_WORLD_SDF 5
#define XGPU_MSL_TEXTURE_WORLD_SDF_TABLE 6
/* (the static scenery's pages of the level's occlusion, a 2D array) */
#define XGPU_MSL_TEXTURE_WORLD_AO_SCENERY 7
/* (the vehicles' occlusion fields' atlas: a 3D texture of four halves;
past the combiner's places, 8 to 19) */
#define XGPU_MSL_TEXTURE_WORLD_AO_OBJECTS 20
/* (the distance-field scene's cascades: xgpu.h's XGPU_WORLD_CASCADE*) */
#define XGPU_MSL_TEXTURE_WORLD_CASCADES 22
#define XGPU_MSL_BUFFER_WORLD_AO 23
/* (display.ambient_occlusion "world" found once a pixel: its texture, a
float a pixel, which the draws with XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED
read; past the combiner's places and the objects' atlas) */
#define XGPU_MSL_TEXTURE_WORLD_AO_PIXELS 21

/* the draw's values both stages read, laid out alike in C and in Metal
(float4s, and floats in fours): d3d8_gl.c's draw uniforms, the per-pixel
lighting's model lights, the blend color the source factor takes in the
shader, and each stage's border color */
struct xgpu_msl_uniforms
{
	float viewport_scale[4];
	float viewport_offset[4];
	/* (depth_bias: the draw's constant depth bias, in the reversed depth's
	own terms, a fraction of the range: xgpu_backend.h, "Reversed depth") */
	float point_size, screen_offset, alpha_reference, depth_bias;
	float ps_c0[8][4];
	float ps_c1[8][4];
	float ps_final_c0[4];
	float ps_final_c1[4];
	float fog_color[4];
	float fog_parameters[4];
	float bump_matrix[4][4];
	float bump_luminance[4][4];
	float texture_scale[4][4];
	/* each stage's (x stage 0 ... w stage 3) */
	float texture_lod_bias[4];
	float blend_constant[4];
	/* each stage's border color (RGBA, 0 to 1), and whether its sampler
	filters linearly (1) or takes the nearest texel (0), for the stages
	whose border the shader draws (xgpu_msl_options border) */
	float texture_border[4][4];
	float texture_border_linear[4];
	float model_lights[XGPU_MODEL_LIGHT_COUNT][4];
	/* display.ambient_occlusion "world" (xgpu.h): the draw's object, its
	owner slot among the window's proxies (-1 none) and its reference, then
	two spare; declared in the pixel shaders that read it alone, past the
	others, so that their text is as it was */
	float world_ao[4];
	/* display.dynamic_lights "enhanced": the lights' tint in the
	structure's lightmap pass, ps_final_c0's color in linear light (to the
	2.2), which the backend works out; declared (past world_ao) in those
	pixel shaders alone */
	float dynamic_light_tint[4];
};

/* what the Metal Shading Language is written for (xgpu_msl.c):
- count_samples: the pixel shaders of a visibility test count their samples
  themselves, an atomic counter at buffer XGPU_MSL_BUFFER_SAMPLES (else the
  backend counts with a visibility result buffer, and the shaders do
  nothing);
- border: for each stage, the axes (bit 0 u, 1 v, 2 w) whose border color
  the shader draws (its sampler clamping to the edge): part of the
  pipeline, as the pixel key is;
- the pixel shaders' debugging settings, as the GLSL's. */
struct xgpu_msl_options
{
	BOOL count_samples;
	unsigned char border[4];
	const char *debug_expression;
	BOOL debug_texture0, debug_flat;
};

/* display.ambient_occlusion "world" found once a pixel (xgpu_stream.h's
WORLD_AO_PREPASS): its pass's functions, one source. xgpu_world_ao_vertex
covers the viewport with one triangle; xgpu_world_ao_pass draws the
occlusion into an RGBA32Float target, at each 2x2 block's nearest pixel at a
quarter of the pixels (the pass's camera[3].w 2) or at each pixel (1): the
occlusion, the pixel's distance from the eye, its normal octahedral (what
the surfaces upsample it by). Its textures: the pre-pass's inputs (0,
RGBA32Uint); the field's and the objects' atlas and the window's block where
the surfaces have them; the pass's struct xgpu_world_ao_pass at buffer
XGPU_MSL_BUFFER_UNIFORMS. The vertex function's source, or (pixel) the
pass's. Returns a malloc'd string. */
/* (the pass's history, the previous frame's occlusion of the window, and
the distance-field scene's cascades: its textures past the inputs') */
#define XGPU_MSL_WORLD_AO_PASS_HISTORY 1
#define XGPU_MSL_WORLD_AO_PASS_CASCADES 2
char *xgpu_msl_world_ao_pass(BOOL pixel);

/* the vertex program (xgpu_shader.h) as a Metal vertex function,
xgpu_vertex: packed_attribute_mask's attributes are NORMPACKED3,
integer_attribute_mask's unnormalized shorts; with lighting (else NULL), the
normal and world position go to the pixel shader too, as the GLSL's. Returns
a malloc'd string. */
char *xgpu_msl_vertex_shader(const struct xgpu_vertex_program *program, unsigned long packed_attribute_mask,
	unsigned long integer_attribute_mask, const struct nv2a_vertex_lighting *lighting,
	const struct xgpu_msl_options *options);

/* the pixel program (xgpu_shader.h) as a Metal fragment function,
xgpu_fragment. Returns a malloc'd string. */
char *xgpu_msl_pixel_shader(const struct xgpu_pixel_program *program, const struct xgpu_msl_options *options);

/* the glow about each plasma bolt's light (the enhanced dynamic lights,
xgpu_backend.h's xgpu_backend_dynamic_lights_halos), as the SPIR-V
emitter's (xgpu_spirv.h): xgpu_halo_vertex, a camera-facing quad of
six vertices about each light ([[attribute(n)]]: XGPU_MSL_HALO_MIDDLE, its
light's middle and the glow's size; _COLOR; _CORNER, -1 or 1 each way),
projected as the game's programs are; and xgpu_halo_fragment, its light
round and soft, faded against the depth the scene was drawn with (a depth
texture at texture 0, read at the pixel). Both read struct
xgpu_msl_halo_uniforms at XGPU_MSL_BUFFER_UNIFORMS. The source of one of
them (pixel: the fragment function's), a malloc'd string. */
#define XGPU_MSL_HALO_MIDDLE 0
#define XGPU_MSL_HALO_COLOR 1
#define XGPU_MSL_HALO_CORNER 2

struct xgpu_msl_halo_uniforms
{
	/* the world's projection (the game's c[0] to c[3]) and screen
	conversion (c[-38], c[-37]) */
	float projection[6][4];
	/* the camera's right and up */
	float camera_axes[2][4];
	float viewport_scale[4];
	float viewport_offset[4];
	/* the depth buffer's value for a distance from the camera, alpha +
	beta / distance; the softness against what a glow meets, world units */
	float depth_parameters[4];
	float screen_offset, unused[3];
};

char *xgpu_msl_halo_shader(BOOL pixel);

/* the distance-field scene's ray step (port/linux/include/world_cascades.h's
world_trace_ray: struct world_trace_hit and world_trace(world_cascades,
cascades, origin, direction, t_max, mask), the cascades' texture and their
four vec4s as xgpu.h's XGPU_WORLD_CASCADE* describes them) appended to a
shader's text; and a fragment function of it alone,
xgpu_world_trace_test (its ray from its inputs, the hit, t and kind
written), for tools/test_world_trace.py. A malloc'd string */
struct xgpu_text;
void xgpu_msl_world_trace(struct xgpu_text *text);
char *xgpu_msl_world_trace_test(void);

#endif
