/*
XGPU_SPIRV.H

SPIR-V from the shader intermediate form (xgpu_shader.h), for the Vulkan
backend (port/linux/vulkan): xgpu_spirv.c. The programs are OpenCE's GLSL
emitter's, written as SPIR-V words directly rather than as GLSL for a
compiler: the shaders are made from pipeline keys
as the game first draws with them, and a GLSL compiler in the host would
cost a library of megabytes and milliseconds a shader there, where these
programs (no control flow but discarding, a few dozen kinds of operation)
are a walk over the form. spirv-val checks the words
(tools/spirv_corpus).

The conventions (SPIR-V 1.3, Vulkan 1.1):

- clip space is Vulkan's (z 0 to 1, the first row at the top of the
  framebuffer, y down): the game's position (Direct3D's, y up) has its y
  negated, so that the targets' first row is the Xbox's top one;
- vertex attributes are at location n for v<n>. A D3DCOLOR is
  VK_FORMAT_B8G8R8A8_UNORM (read as RGBA); NORMPACKED3
  is VK_FORMAT_R32_UINT, unpacked here; the unnormalized shorts
  (integer_attribute_mask) are VK_FORMAT_R16*_SINT, read as integers and
  converted here; everything else is read as a vec4, the missing components
  0, 0, 0, 1;
- the draw's values are uniform buffers in descriptor set
  XGPU_SPIRV_SET_DRAW: the vertex constants (192 vec4) at
  XGPU_SPIRV_BINDING_CONSTANTS, struct xgpu_spirv_uniforms at
  XGPU_SPIRV_BINDING_UNIFORMS (both stages), the enhanced dynamic lights'
  block and cells (struct xgpu_dynamic_lights_block, a usampler3D) and the
  visibility tests' counters (a storage buffer of uints) at theirs;
- each stage's texture is a combined image sampler at binding n of
  XGPU_SPIRV_SET_TEXTURES; the samplers have D3DTSS_MIPMAPLODBIAS
  (mipLodBias) and the border colors (VK_EXT_custom_border_color), or the
  shader draws the border as the Metal emitter's does
  (xgpu_spirv_options border);
- the stages hand on, at locations XGPU_SPIRV_LOCATION_*: the colors, the
  texture coordinates, fog, and with lighting the world normal, position,
  binormal and tangent;
- the VR mode uses Vulkan multiview, with ViewIndex relative to the
  attachment view's first layer. A draw of one eye uses that eye's layer
  (its two layers when foveated) as the attachment view. The eyes and all
  other targets have their first row at the top: neither the eye positions
  nor layered texture lookups flip rows.
*/

#ifndef __HALO_LINUX_XGPU_SPIRV_H
#define __HALO_LINUX_XGPU_SPIRV_H

#define XGPU_SPIRV_SET_DRAW 0
#define XGPU_SPIRV_BINDING_CONSTANTS 0
#define XGPU_SPIRV_BINDING_UNIFORMS 1
#define XGPU_SPIRV_BINDING_DYNAMIC_LIGHTS 2
#define XGPU_SPIRV_BINDING_DYNAMIC_LIGHT_CELLS 3
#define XGPU_SPIRV_BINDING_SAMPLES 4
#define XGPU_SPIRV_SET_TEXTURES 1
/* display.hdr: the pixel a draw blends over in its shader (its tag's
XGPU_HDR_SHADER_BLENDS, xgpu_hdr.h), the colour attachment read as input
attachment 0 at binding 0 (VK_KHR_dynamic_rendering_local_read) */
#define XGPU_SPIRV_SET_INPUT 2

/* the locations of what the vertex shader hands the pixel shader */
enum
{
	XGPU_SPIRV_LOCATION_D0, XGPU_SPIRV_LOCATION_D1,
	XGPU_SPIRV_LOCATION_T0, XGPU_SPIRV_LOCATION_T1, XGPU_SPIRV_LOCATION_T2, XGPU_SPIRV_LOCATION_T3,
	XGPU_SPIRV_LOCATION_FOG,
	XGPU_SPIRV_LOCATION_WORLD_NORMAL, XGPU_SPIRV_LOCATION_WORLD_POSITION,
	XGPU_SPIRV_LOCATION_WORLD_BINORMAL, XGPU_SPIRV_LOCATION_WORLD_TANGENT,
};

/* the draw's values both stages read, laid out alike in C and in the
shaders' uniform block (std140: vec4s, and floats in fours): the backend's
draw uniforms, the per-pixel lighting's model lights, the blend color the
source factor takes in the shader, and each stage's border color */
struct xgpu_spirv_uniforms
{
	float viewport_scale[4];
	float viewport_offset[4];
	float point_size, screen_offset, alpha_reference, screen_points;
	float ps_c0[8][4];
	float ps_c1[8][4];
	float ps_final_c0[4];
	float ps_final_c1[4];
	float fog_color[4];
	float fog_parameters[4];
	float bump_matrix[4][4];
	float bump_luminance[4][4];
	float texture_scale[4][4];
	float blend_constant[4];
	/* each stage's border color (RGBA, 0 to 1), and whether its sampler
	filters linearly (1) or takes the nearest texel (0), for the stages
	whose border the shader draws (xgpu_spirv_options border) */
	float texture_border[4][4];
	float texture_border_linear[4];
	float model_lights[XGPU_MODEL_LIGHT_COUNT][4];
	/* the clip and screen corrections, as the stream delivers them;
	the matrices are column-major (four consecutive vec4 columns) */
	float eye_correction[2][16];
	float screen_correction[2][4];
	/* each foveated layer's scale and offset: fovea_view in Vulkan clip
	space (x/y down from the top left after the y negation), fovea_sample
	in texture space (its offset in texels). The whole eyes' are identity. */
	float fovea_view[4][4];
	float fovea_sample[4][4];
	/* this draw's counter in the visibility tests' uint storage buffer;
	its constant depth bias, where depth is reversed (xgpu_spirv_options
	reversed_depth: xgpu_reversed_depth_bias) */
	float visibility_counter, depth_bias, vr_unused[2];
	/* the VR mode's effects (xgpu_spirv_options effect): for each view, the
	world point (homogeneous) at a pixel and depth, as columns (the point is
	unproject[0] x + unproject[1] y + unproject[2] depth + unproject[3],
	gl_FragCoord's x and y), and its eye in the world (port/linux/vulkan/
	xgpu_vulkan_effects.h) */
	float effect_unproject[4][4][4];
	float effect_eye[4][4];
};

/* what the SPIR-V is written for (xgpu_spirv.c):
- count_samples: the pixel shaders of a visibility test count their samples
  themselves, an atomic counter at XGPU_SPIRV_BINDING_SAMPLES (else the
  backend counts with occlusion queries, and the shaders do nothing);
- border: for each stage, the axes (bit 0 u, 1 v, 2 w) whose border color
  the shader draws (its sampler clamping to the edge) where the sampler
  cannot (no VK_EXT_custom_border_color): part of the pipeline, as the
  pixel key is;
- multiview: the vertex shader's XGPU_MULTIVIEW_* mode (the pixel
  program carries its own); foveated: four layers, periphery and inset
  for each eye, rather than the two eyes alone;
- the pixel shaders' debugging settings, as the GLSL's;
- effect: the VR mode's effects (XGPU_SPIRV_EFFECT_*, below), and
  effect_fade how their fade is drawn under the pipeline's blend
  (XGPU_SPIRV_FADE_*: the backend's choice); for the glows' shaders, any effect draws them
  as the light the bolt scatters toward each eye;
- input_samples: the samples of the colour target a display.hdr draw that
  blends in its shader reads (XGPU_SPIRV_SET_INPUT): more than 1, each
  sample its own (sample shading);
- reversed_depth: depth reversed (xgpu_backend.h, "Reversed depth"): the
  vertex shaders write w - z (from the projection's rows where the program
  has them: xgpu_vertex_program_depth_rows; in an eye's view, with its
  correction's rows) and add the uniforms' depth_bias, a z-sprite writes
  its depth reversed, the glows and the VR mode's effects read the scene's
  depth and their own reversed (their unprojections stay the game's). */
struct xgpu_spirv_options
{
	BOOL count_samples;
	unsigned char border[4];
	BOOL debug_texture0, debug_flat;
	int multiview;
	BOOL foveated;
	int effect, effect_fade;
	int input_samples;
	BOOL reversed_depth;
};

/* ---------- the VR mode's effects

The game's effects (particles, contrails, weather: its effect shaders),
drawn into both eyes at once (the pixel key's vr_effect: d3d8_device.c,
halo_vr_effect_depth):

- XGPU_SPIRV_EFFECT_SOFT: soft. Each pixel finds the world point it draws
  and the scene's behind it (the depth buffer, texture
  XGPU_SPIRV_EFFECT_DEPTH_BINDING, an array of the views' layers sampled
  with texelFetch; the uniforms' effect_unproject) and fades the effect out
  as the two meet, over a distance scaled to the effect's sprite (its
  radius, the thickness map's green, in the world by the texels' size
  there), so that it meets walls and floors without a hard line.
- XGPU_SPIRV_EFFECT_THICK: soft and thick. The sprite's thickness (the map
  at XGPU_SPIRV_EFFECT_THICKNESS_BINDING, baked from its bitmap's alpha:
  xgpu_vulkan_effects.h) is a bulge toward the eyes: each eye's ray meets
  the bulge's front where the sprite's texel there is drawn (one parallax
  step: the bulge is smooth and the eyes' rays near the sprite's normal),
  so each eye samples other texels and the effect has depth inside it; the
  fade is measured from the bulge's front, through its thickness.

The pixel program must sample texture 0 in 2D (the effect's bitmap) and
not textures 2 and 3, whose bindings the depth and thickness take
(xgpu_spirv_effect_possible), drawn into both eyes. The fade is drawn as
the blend's own nothing (XGPU_SPIRV_FADE_*): alpha to 0 over an alpha
blend, colour to 0 when added, to white when multiplied, to grey when
multiplied twice. */
enum
{
	XGPU_SPIRV_EFFECT_NONE, XGPU_SPIRV_EFFECT_SOFT, XGPU_SPIRV_EFFECT_THICK,
};
enum
{
	XGPU_SPIRV_FADE_NONE, XGPU_SPIRV_FADE_ALPHA, XGPU_SPIRV_FADE_COLOR, XGPU_SPIRV_FADE_COLOR_ALPHA,
	XGPU_SPIRV_FADE_TO_WHITE, XGPU_SPIRV_FADE_TO_GREY,
};
#define XGPU_SPIRV_EFFECT_DEPTH_BINDING 2
#define XGPU_SPIRV_EFFECT_THICKNESS_BINDING 3
/* the bulge's height at a sprite's middle, of its radius (a sphere's is
1). Smoke is denser in its middle than at its silhouette, so the front of
what is seen of it lies nearer its middle than a ball's surface would: a
sphere's depth made puffs look like balloons, half of it keeps them round
without a hard surface. */
#define XGPU_SPIRV_EFFECT_THICKNESS 0.5f
/* the fade's distance: of the sprite's radius, and the least (world units,
6 cm, and of the distance from the eye: 24-bit depth's steps there, with the
game's near plane, are smaller), and the most an eye's parallax moves a
texel (of the radius) */
#define XGPU_SPIRV_EFFECT_SOFTNESS 0.5f
#define XGPU_SPIRV_EFFECT_SOFTNESS_LEAST 0.02f
#define XGPU_SPIRV_EFFECT_SOFTNESS_DISTANCE 0.002f
#define XGPU_SPIRV_EFFECT_PARALLAX_MOST 0.5f

struct xgpu_pixel_program;

/* the program can be drawn as an effect (above) */
BOOL xgpu_spirv_effect_possible(const struct xgpu_pixel_program *program);

/* a module's words (SPIR-V's own: 32 bits each), malloc'd, count of them */
struct xgpu_spirv_module
{
	unsigned int *words;
	unsigned long count;
};

/* the vertex program (xgpu_shader.h) as a vertex shader, entry point
"main": packed_attribute_mask's attributes are NORMPACKED3,
integer_attribute_mask's unnormalized shorts; with lighting (else NULL), the
normal and world position go to the pixel shader too, as the GLSL's.
FALSE if out of memory. */
BOOL xgpu_spirv_vertex_shader(const struct xgpu_vertex_program *program, unsigned long packed_attribute_mask,
	unsigned long integer_attribute_mask, const struct nv2a_vertex_lighting *lighting,
	const struct xgpu_spirv_options *options, struct xgpu_spirv_module *module);

/* the vertex inputs (bit n v<n>) the vertex shader of a program and its
lighting reads: the pipeline's vertex layout has these alone */
unsigned long xgpu_spirv_vertex_inputs(const struct xgpu_vertex_program *program,
	const struct nv2a_vertex_lighting *lighting);

/* the pixel program (xgpu_shader.h) as a fragment shader, entry point
"main". FALSE if out of memory. */
BOOL xgpu_spirv_pixel_shader(const struct xgpu_pixel_program *program, const struct xgpu_spirv_options *options,
	struct xgpu_spirv_module *module);

/* the enhanced dynamic lights' glows (xgpu_backend.h's
xgpu_backend_dynamic_lights_halos):
a camera-facing quad about each light, six vertices of a triangle list,
added to the 3D view and faded against the depth the scene was drawn with.
The vertices: v<XGPU_SPIRV_HALO_MIDDLE> the light's middle and the glow's
size, v<XGPU_SPIRV_HALO_COLOR> its color, v<XGPU_SPIRV_HALO_CORNER> the
corner (-1 or 1 each way). The vertex constants: the world's projection
and the screen conversion (c[-96] to c[-93], c[-38], c[-37]) at
XGPU_SPIRV_HALO_PROJECTION, the camera's right and up at
XGPU_SPIRV_HALO_CAMERA_AXES, and the depth for a distance and the softness
at XGPU_SPIRV_HALO_DEPTH (xgpu_vulkan_stream.c's halo_depth_parameters).
The uniforms are struct xgpu_spirv_uniforms (the viewport's, the screen
offset, the eyes' and insets' corrections); the scene's depth is texture 0,
sampled with texelFetch (an array of the views' layers with multiview).
options: multiview (XGPU_MULTIVIEW_EYES or none), foveated and effect alone.

With effect (the VR mode's eyes), each glow is the light its bolt
scatters toward each eye from a haze about it, worked out for each pixel
along that eye's ray: the haze's density falls off from the bolt as the
card's glow does across it (a wide Gaussian, less its value at the glow's
edge, and a narrow hot core), and its integral along the ray is closed
(each Gaussian's is an error function's difference), from the eye to the
scene's surface behind the pixel. Unhidden, a ray passing the bolt at a
fraction r of the glow's size gets exactly the card's glow at r; a wall
through the glow hides the part behind it, in each eye as it should. The
quad drawn covers the haze's outline from the head (a quad about the bolt
facing the head's centre, or, with the head in the haze, one across the
view). Its vertex constants, past the card's: each view's clip transform
(rows: XGPU_SPIRV_GLOW_CLIP + 4 view), its unprojection (columns:
XGPU_SPIRV_GLOW_UNPROJECT + 4 view) and eye (XGPU_SPIRV_GLOW_EYE + view),
the head's centre and the near distance the quad across the view is drawn
at (XGPU_SPIRV_GLOW_HEAD), and the head's right, up and forward
(XGPU_SPIRV_GLOW_AXES); port/linux/vulkan/xgpu_vulkan_effects.h. */
enum
{
	XGPU_SPIRV_HALO_MIDDLE, XGPU_SPIRV_HALO_COLOR, XGPU_SPIRV_HALO_CORNER,
};
#define XGPU_SPIRV_HALO_PROJECTION 0
#define XGPU_SPIRV_HALO_CAMERA_AXES 6
#define XGPU_SPIRV_HALO_DEPTH 8
#define XGPU_SPIRV_GLOW_CLIP 16
#define XGPU_SPIRV_GLOW_UNPROJECT 32
#define XGPU_SPIRV_GLOW_EYE 48
#define XGPU_SPIRV_GLOW_HEAD 52
#define XGPU_SPIRV_GLOW_AXES 53

BOOL xgpu_spirv_halo_vertex_shader(const struct xgpu_spirv_options *options, struct xgpu_spirv_module *module);
BOOL xgpu_spirv_halo_pixel_shader(const struct xgpu_spirv_options *options, struct xgpu_spirv_module *module);

#endif
