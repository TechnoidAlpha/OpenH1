/*
XGPU.H

What the renderer's parts share, in no GPU API's terms: the vertex
programs' and pixel shaders' keys and lighting (decoded by nv2a_vsh.c and
nv2a_psh.c), the enhanced dynamic lights' layout, the Xbox's texture
formats (xbox_textures.c) and generated source text. The front end
(d3d8_device.c) and every backend include it; the interface between front
end and backend is xgpu_backend.h.
*/

#ifndef __HALO_LINUX_XGPU_H
#define __HALO_LINUX_XGPU_H

#include "platform.h"

/* ---------- generated source text */

struct xgpu_text
{
	char *buffer;
	unsigned long length;
	unsigned long capacity;
};

void xgpu_text_append(struct xgpu_text *text, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* ---------- vertex shaders */

#define XGPU_VERTEX_ATTRIBUTE_COUNT 16
#define XGPU_VERTEX_CONSTANT_COUNT 192
/* D3D constant register -96 is hardware register 0 */
#define XGPU_VERTEX_CONSTANT_BIAS 96

/* where one of the game's model lighting programs (d3d8_device.c
halo_vertex_shader_lighting) has the normal, and the world position, that it
lights the diffuse color by: the temporary register that holds each before
the instruction given */
struct nv2a_vertex_lighting
{
	/* 1 by the ambient and distant lights, 2 by the point lights too */
	int lights;
	unsigned long normal_instruction, normal_register;
	/* (with the point lights only) */
	unsigned long position_instruction, position_register;
	/* the world position the program projects to the screen (the
	temporary its clip position is computed from, before the instruction
	given), for the enhanced dynamic lights (display.dynamic_lights): with
	the point lights, the same as theirs; world_found FALSE if none */
	BOOL world_found;
	unsigned long world_instruction, world_register;
};

/* the lights value of a structure's (the environment's) lightmap pass lit
by the enhanced dynamic lights: its vertices' world position, normal,
binormal and tangent (v0 to v3) handed on as they are */
#define XGPU_ENVIRONMENT_LIGHTING 3

struct xgpu_vertex_program;

/* finds them in a model lighting program (decoded: xgpu_shader.h), checking
that it lights its diffuse color as xgpu_spirv.c does for each pixel; FALSE if
it does not */
BOOL nv2a_vertex_shader_lighting(const struct xgpu_vertex_program *program, struct nv2a_vertex_lighting *lighting);

/* the views a draw goes to (a vertex program's multiview variant): with
XGPU_MULTIVIEW_EYES, both of the VR mode's eyes at once (multiview): the
clip position goes through each eye's correction (xgpu_spirv.c,
halo_vr_set_eye_transforms); with XGPU_MULTIVIEW_LEFT_EYE or
XGPU_MULTIVIEW_RIGHT_EYE, that eye's layer alone */
enum
{
	XGPU_MULTIVIEW_NONE = 0,
	XGPU_MULTIVIEW_EYES,
	/* the eye pass's visibility tests: queries in a multiview pass would
	count in as many query slots as views (Vulkan), which Zink does not
	allow for, and the GPU writes past them */
	XGPU_MULTIVIEW_LEFT_EYE,
	/* a draw into each eye's layer in turn, each with its own screen
	correction (the crosshairs: halo_vr_draw_eye) */
	XGPU_MULTIVIEW_RIGHT_EYE,
};
/* The VR mode's eye pass is foveated (vr.foveation, host_vulkan_vr.c): its targets
have four layers, two for each eye (layer 2 eye + 0, the whole eye at the
periphery's resolution; 2 eye + 1, the inset about the gaze at full
resolution, as large in pixels). XGPU_MULTIVIEW_EYES then draws all four
(eye_correction[VIEW >> 1], then fovea_view[VIEW] scales and moves the
inset's views), XGPU_MULTIVIEW_LEFT_EYE and XGPU_MULTIVIEW_RIGHT_EYE that
eye's two (its visibility tests are counted by the pixel shaders instead),
and a layered target is sampled in an inset's layer through
fovea_sample[layer] (its pixels from the whole eye's), from the whole eye's
layer where the inset does not have the point. Set before the first VR
shader is made. */
extern int xgpu_foveated;

/* ---------- pixel shaders */

enum
{
	_xgpu_sampler_none = 0,
	_xgpu_sampler_2d,
	_xgpu_sampler_3d,
	_xgpu_sampler_cube,
	/* a 2-layer render target (the VR mode's eye pass; 4 foveated,
	xgpu_foveated): the view's layer */
	_xgpu_sampler_2d_layered,
};

/* everything a translated pixel shader depends on; the GLSL program cache
is keyed by these bytes */
struct nv2a_pixel_shader_key
{
	DWORD combiner_state[D3DRS_PS_MAX];
	/* D3DRS_PSTEXTUREMODES lies past D3DRS_PS_MAX */
	DWORD texture_modes;
	unsigned char sampler_type[4];
	unsigned char alpha_kill[4];
	/* D3DTSS_COLORSIGN: channels (bit 0 alpha ... bit 3 blue, as
	D3DTSIGN_*) that hold signed data in an unsigned texture format */
	unsigned char color_sign[4];
	/* D3DCMP_* function for the alpha test, or 0 when disabled */
	unsigned long alpha_test_function;
	unsigned char fog_enable;
	unsigned char fog_table_mode;
	/* inside a visibility test: count the samples that pass (Android) */
	unsigned char count_samples;
	/* a high-res HUD meter (hud_hires.h) drawn with the meter's blend (the
	destination kept by the source's alpha): that alpha is eased to 1 by the
	coverage texture 0's green holds, so that the meter darkens what is
	behind it only where it covers it (the Xbox's point-sampled meters stop
	at their texels' edges; filtered ones have a fringe of faint texels) */
	unsigned char coverage_alpha;
	/* Discrete meter thresholds in texture 0's red are read at level zero,
	without filtering. Coverage/brightness still use the filtered lookup. */
	unsigned char point_threshold;
	/* a model lighting program's draw lit for each pixel
	(display.per_pixel_lighting): nv2a_vertex_lighting's lights, or 0 for
	the diffuse color the vertex shader computed */
	unsigned char per_pixel_lighting;
	/* drawn into a multisampled target (display.anti_aliasing's
	multisampling), its samples a pixel: the alpha test covers samples in
	proportion to how far alpha is past the reference, not all of the pixel
	or none of it, so that cut-out edges (foliage, grates) are smoothed too */
	unsigned char alpha_test_samples;
	/* drawn into both eyes at once (the VR mode, d3d8_device.c, xgpu_vulkan.c):
	layered render targets are sampled at the view's layer
	(XGPU_MULTIVIEW_*) */
	unsigned char multiview;
	/* the source's blend factor, applied here instead (1 the blend color, 2
	one less it, 3 its alpha, 4 one less that; 0 none), for a blend of a
	constant color factor with a constant alpha one, as the meters'
	(shader_transparent_meter) */
	unsigned char source_blend;
	/* the enhanced dynamic lights (display.dynamic_lights, d3d8_device.c,
	xgpu_spirv.c) added for each pixel: XGPU_DYNAMIC_LIGHTS_ENVIRONMENT to a
	structure's lightmap pass (into the light the textures are then
	multiplied by, the bump map in texture 0), XGPU_DYNAMIC_LIGHTS_MODEL to a
	model's lighting lit for each pixel in place of its two point lights; 0
	none */
	unsigned char dynamic_lights;
	/* the VR mode's effects (d3d8_device.c halo_vr_effect_depth, the
	backend's xgpu_spirv.h XGPU_SPIRV_EFFECT_*): an effect drawn into both
	eyes soft against the scene's depth (1), and thick (2); 0 none */
	unsigned char vr_effect;
	/* display.hdr: the draw's tag into the HDR back buffer (xgpu_hdr.h:
	XGPU_HDR_*); 0 into any other target, and without HDR */
	unsigned short hdr;
};

/* the vertex constants the per-pixel model lighting reads, in a uniform of
their own (the vertex shader's 192 would pass the least fragment uniform
space a GPU must have): [0] c[-82] (the translucency in z), [1] to [11] c[-79] to
c[-69] (rasterizer_set_model_lighting's two point lights, two distant
lights and the ambient light), [12] c[-57] (with the enhanced dynamic
lights, the lights the model is not lit by: its object's own that say so,
as four light indices, -1 none: halo_dynamic_lights.h) */
#define XGPU_MODEL_LIGHT_COUNT 13
#define XGPU_MODEL_LIGHT_OWN_LIGHTS 12

/* ---------- enhanced dynamic lights

display.dynamic_lights "enhanced" (d3d8_device.c, halo_dynamic_lights_set):
the frame's dynamic lights (up to the game's 128 a window) light the
structure's lightmap pass and the models lit for each pixel, each pixel by
the lights near it alone. A grid about the camera, its cells boxes in the
world (eye-independent: the VR mode's eyes share it), holds for each cell a
128-bit mask of the lights whose spheres reach it (an integer 3D texture at
XGPU_DYNAMIC_LIGHTS_UNIT); the lights are a uniform block at
XGPU_DYNAMIC_LIGHTS_BINDING. The cells grow away from the camera: a cell's
index along each axis is log2(1 + |offset| / XGPU_DYNAMIC_LIGHTS_CELL) times
that axis's scale, from the middle, so that the grid reaches
XGPU_DYNAMIC_LIGHTS_REACH world units each way with small cells near the
camera, where lights are seen large. */
#define XGPU_DYNAMIC_LIGHTS_MAXIMUM 128
#define XGPU_DYNAMIC_LIGHTS_GRID_X 32
#define XGPU_DYNAMIC_LIGHTS_GRID_Y 32
#define XGPU_DYNAMIC_LIGHTS_GRID_Z 16
#define XGPU_DYNAMIC_LIGHTS_CELL 0.5f
#define XGPU_DYNAMIC_LIGHTS_REACH 128.0f
/* the largest soft core of a light's falloff, world units (xgpu_spirv.c) */
#define XGPU_DYNAMIC_LIGHTS_CORE 0.6
#define XGPU_DYNAMIC_LIGHTS_UNIT 12
#define XGPU_DYNAMIC_LIGHTS_BINDING 1
enum
{
	XGPU_DYNAMIC_LIGHTS_ENVIRONMENT = 1,
	XGPU_DYNAMIC_LIGHTS_MODEL,
};

/* the uniform block, as std140 lays it out: [0] the grid's middle (the
camera) and the light count, [1] each axis's cells a doubling of the offset
and 1 / XGPU_DYNAMIC_LIGHTS_CELL, [2] the look (xgpu_spirv.c); then each
light's position and 1 / radius (less than 0: models only, the structure
lit by the game's own pass: a spotlight's gel), its color (linear, the
look's brightness in it) and its cone's offset, its cone's axis and scale
(omni: 0, 0 and an offset of 1) */
struct xgpu_dynamic_lights_block
{
	float grid[3][4];
	float position[XGPU_DYNAMIC_LIGHTS_MAXIMUM][4];
	float color[XGPU_DYNAMIC_LIGHTS_MAXIMUM][4];
	float cone[XGPU_DYNAMIC_LIGHTS_MAXIMUM][4];
};

/* writes BGRA pixels as a BMP named <name><frame>.bmp in the screenshot
folder (debug.screenshot_directory): rows from the top, or from the bottom
(d3d8_device.c) */
void xgpu_write_bmp(const char *name, const unsigned char *pixels, unsigned long width, unsigned long height,
	BOOL bottom_up);
/* the <frame> of this frame's screenshots (where the host writes the VR
mode's, it is told it: xgpu_stream_encode.c) */
unsigned long xgpu_screenshot_number(void);

/* ---------- textures */

struct xgpu_texture_description
{
	DWORD format;       /* D3DFMT_* */
	unsigned long width, height, depth, levels;
	BOOL cube_map;
	BOOL linear;        /* not swizzled; addressed with texel coordinates */
	BOOL compressed;
	unsigned long pitch; /* linear textures */
	BOOL hires;         /* a high-res HUD texture drawn in the texture's place (hud_hires.h) */
	BOOL hires_coverage; /* ... whose green is its coverage (a meter's) */
	BOOL hires_point_threshold; /* ... whose red holds discrete segment data */
};

/* (xbox_texture_decode.c) */
void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description);
/* bytes of one face, mip levels included (cube faces are padded) */
unsigned long xgpu_texture_face_size(const struct xgpu_texture_description *description);
unsigned long xgpu_texture_level_offset(const struct xgpu_texture_description *description, unsigned long level);
unsigned long xgpu_texture_level_pitch(const struct xgpu_texture_description *description, unsigned long level);
/* bytes of a level (all of a volume's slices), and a level's width, height
or depth from the first level's */
unsigned long xgpu_texture_level_size(const struct xgpu_texture_description *description, unsigned long level);
unsigned long xgpu_texture_level_dimension(unsigned long base, unsigned long level);
/* the size is one D3DDevice_GetDeviceCaps allows (d3d8_device.c) */
BOOL xgpu_texture_size_supported(const struct xgpu_texture_description *description);
/* a level of a texture that is not compressed (a volume's slices one after
another), from its texels at source, as 32-bit BGRA (a DWORD each: ARGB),
rows from the first; FALSE when out of memory. palette: a P8 texture's 256
D3DCOLORs (NULL: grey) */
BOOL xgpu_texture_decode_level(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, const D3DCOLOR *palette, DWORD *destination);
/* a level of a DXT texture, as xgpu_texture_decode_level */
void xgpu_texture_decode_dxt(const struct xgpu_texture_description *description, unsigned long level,
	const unsigned char *source, DWORD *destination);

#endif
