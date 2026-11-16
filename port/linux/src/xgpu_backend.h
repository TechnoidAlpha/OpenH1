/*
XGPU_BACKEND.H

The renderer's two halves and what passes between them. The front end
(d3d8_device.c) is the Xbox Direct3D 8 device: it keeps the game's states
and knows what they mean, and describes each draw in terms of no API: a
pipeline key (everything a pipeline object bakes in: shaders, vertex
layout, blend, depth and stencil, raster state, the targets' formats), the
dynamic state no API bakes in (viewport, scissor, depth range, stencil
reference, blend color, depth bias), the resources bound and the draw's
range. A backend (the host's, Vulkan's or Metal's, through the command
stream: xgpu_stream_encode.c) makes the GPU do it. One backend is built into
an executable; the functions here are its.

Nothing here holds an Xbox value or a GL one: the front end translates the
game's D3D states into these, each backend into its API's. And every call
is plain data, so that a frame's calls can be written into a command
stream and played back elsewhere (the 64-bit host, a thread of its own):
resources are small integers (render targets numbered by the front end),
guest data is a guest address and a size (vertices, indices, textures are
read from guest memory by the backend), structures are passed by value in
effect (arrays and structures are the call's: the backend keeps no pointer
into the front end's), and nothing is
returned that the front end needs within the frame, but for the few calls
marked "answered now" (each says how it could wait). Refer to "The
renderer" in port/linux/README.md.
*/

#ifndef __HALO_LINUX_XGPU_BACKEND_H
#define __HALO_LINUX_XGPU_BACKEND_H

#include "xgpu.h"

/* (xgpu_stream.h) */
struct xgpu_world_ao_block;
struct xgpu_world_ao_pass;

struct xgpu_vertex_program;

/* a render target, numbered by the front end from 1 (0: none) */
typedef unsigned int xgpu_target;

/* ---------- what the backend can do (xgpu_backend_initialize) */

struct xgpu_backend_capabilities
{
	/* the largest target each way (texture and renderbuffer), the most
	samples a pixel, the most anisotropy (0 until the backend is ready) */
	long largest_target;
	int most_samples;
	long most_anisotropy;
	/* visibility tests counted by the pixel shaders (atomic counters)
	rather than by queries */
	BOOL sample_counting;
	/* vertex streams' D3DCOLOR elements read as the Xbox has them (BGRA);
	else streamed with their bytes swapped, never from the
	mirror */
	BOOL bgra_vertex_colors;
	/* indexed draws from the mirror (a base vertex for them) */
	BOOL base_vertex;
	/* an alpha test covering a multisampled pixel's samples in proportion
	to alpha (a sample mask) */
	BOOL sample_mask;
	/* a blend mixing a constant color factor with a constant alpha one
	(else the pixel shader applies the source factor) */
	BOOL mixed_constant_blend;
	/* the enhanced dynamic lights (display.dynamic_lights) */
	BOOL dynamic_lights;
	/* a draw can sample the target it draws into, as it was (else a copy
	of it: xgpu_texture_source's drawn) */
	BOOL sample_drawn_target;
	/* display.hdr: an RGBA16F back buffer, the draws' tags carried out
	(xgpu_hdr.h), the tone map and an HDR display's encoding */
	BOOL hdr;
	/* display.ambient_occlusion "ray_traced": the occlusion's rays traced
	by the GPU's ray queries (xgpu_backend_world_ao_mesh, _instances) */
	BOOL ray_queries;
};

extern struct xgpu_backend_capabilities xgpu_backend_capabilities;
/* the GPU, its API and driver, as the backend's host names them ("" if
unknown: a report's, port/linux/src/report.c) */
const char *xgpu_backend_device(void);
/* a report's times (report_frame_presented), running counts of
microseconds modulo 2^32: the GPU's on the frames, as the backend's host
measures it; the renderer's thread's, playing the frames' records; the
game's waits for the renderer and the display; and of those waits, the
display's pacing and display.max_fps's. The function's value: whether the
GPU's is measured (0: the backend measures none, gpu not written) */
struct xgpu_backend_frame_times
{
	unsigned long gpu, render, waited, paced;
};

int xgpu_backend_frame_times(struct xgpu_backend_frame_times *times);

/* ---------- the backend's life

xgpu_backend_initialize: the window's context exists (answered as the
device is made: the capabilities). xgpu_backend_invalidate: something drew behind the
backend's back (the state it remembers setting is unknown). */

void xgpu_backend_initialize(void);
void xgpu_backend_invalidate(void);
/* the commands so far to the GPU (D3DDevice_KickPushBuffer) */
void xgpu_backend_flush(void);
/* All earlier commands have consumed their CPU resource data. This does
not advance a game frame or wait for GPU completion. */
void xgpu_backend_consume(void);
/* display.anti_aliasing's FXAA or SMAA passes, made now (logged if they do
not build); and the passes run over a target's corners (x0, y0, x1, y1 in
its pixels from row 0) */
void xgpu_backend_anti_aliasing_prepare(BOOL smaa);
void xgpu_backend_anti_alias(xgpu_target target, BOOL smaa, const long corners[4]);
/* display.panini: a target's corners (as anti-aliasing's) drawn again
through the Panini projection (port/linux/include/panini_projection.h),
projection its d, s and magnification at the middle, 0, then the
rectilinear view's tangents at the corners' left, right, top and bottom */
void xgpu_backend_panini(xgpu_target target, const long corners[4], const float projection[8]);

/* display.hdr (xgpu_hdr.h): the frame drawn in HDR (the back buffer
RGBA16F) or not from now on, the mode (XGPU_HDR_MODE_*) and paper white
(in nits; 0: the system's SDR white, in percent of it:
xgpu_hdr_paper_white_nits) and the UI's white (in nits; 0: paper
white's); a window's 3D view in the back buffer tone mapped over its
corners (as xgpu_backend_anti_alias's); and the display, as the backend
last looked (answered now, from what the backend writes where the front end
reads it: the stream's ring): whether it shows HDR, and its headroom; its
SDR white in nits, and whether the system reported it (else it is the one
the encoding takes) */
void xgpu_backend_hdr(BOOL active, int mode, int paper_white_nits, int paper_white, int ui_nits);
/* display.picture_position: the presented picture letterboxed this many
pixels from the top of a window taller than it (no lower than its bottom),
or centered (-1), from the next present on */
void xgpu_backend_picture_top(int top);
/* display.gamma: an SDR frame curved by one over this as it is presented
(each channel raised to that power: past 1 brighter), from the next present
on; 1 none, which costs nothing. An HDR swapchain's frame never is. */
void xgpu_backend_gamma(float gamma);
/* the game's renderer's category of the draws from now on (rasterizer.h's
_rasterizer_profile_*, -1 none): what the backends' GPU timing sums by
(HALO_METAL_PASS_TIMES) */
void xgpu_backend_category(int category);
void xgpu_backend_tone_map(xgpu_target target, const long corners[4]);
/* debug.render_marks (xgpu_stream.h's MARK records, from
port/linux/game/render_marks.c): what the draws from now on belong to, for
the inspector's replay; the backends draw nothing for them. A view: the
map, the game's tick, the window (-1: the frame's start) and its camera
(position, forward, up, the horizontal field of view in radians, the
viewport in the back buffer's pixels); an owner: its kind
(XGPU_STREAM_MARK_*), an id (-1 none), a label, tag paths (NULL none), a
menu widget's tag and its generator source */
void xgpu_backend_mark_view(const char *map, unsigned long tick, long window, const float position[3],
	const float forward[3], const float up[3], float field_of_view, const long viewport[4]);
void xgpu_backend_mark_owner(int kind, long id, const char *label, const char *object, const char *model,
	const char *shader, const char *bitmap, const char *widget, const char *source);
/* display.ambient_occlusion "world" (xgpu_stream.h, docs/world-ao.md):
the block the opaque surfaces' ambient light reads from here on (the
field's place, the window's proxies; NULL none), and slices of the field
uploaded (width 0: released) */
void xgpu_backend_world_ao_window(const struct xgpu_world_ao_block *block);
void xgpu_backend_world_ao_field(int part, unsigned long width, unsigned long height, unsigned long depth,
	unsigned long z_first, unsigned long z_count, const void *data);
/* ... found once a pixel (xgpu_stream.h's WORLD_AO_PREPASS): the pre-pass
begun (XGPU_WORLD_AO_PREPASS_BEGIN, its targets target's size), ended and
the occlusion found from it over the window (_END, pass), or none (_NONE) */
void xgpu_backend_world_ao_prepass(int phase, xgpu_target target, const struct xgpu_world_ao_pass *pass);
/* a box of the distance-field scene's cascades' texture (xgpu.h's
XGPU_WORLD_CASCADE*): its first texel, its size, two signed bytes a texel,
x fastest (width 0: released) */
void xgpu_backend_world_ao_cascades(unsigned long x, unsigned long y, unsigned long z, unsigned long width,
	unsigned long height, unsigned long depth, const void *data);
/* display.ambient_occlusion "ray_traced" (xgpu_stream.h's WORLD_AO_MESH,
_INSTANCES): a mesh of the scene, count triangles of nine floats (id 0:
every mesh let go of), and the frame's instances of them */
struct xgpu_world_ao_instance;
void xgpu_backend_world_ao_mesh(unsigned long id, unsigned long count, const float *triangles);
void xgpu_backend_world_ao_instances(const struct xgpu_world_ao_instance *instances, unsigned long count);
BOOL xgpu_backend_display_hdr(float *headroom);
BOOL xgpu_backend_display_white(float *nits);

/* ---------- render targets

The front end finds a target by the Xbox surface it is (its address, its
size, its kind: the screen's, a shadow map, the VR mode's layered eye
targets) and numbers it; the backend makes its storage from its
description, and frees it. A target can be multisampled: drawn into a
multisampled attachment, resolved into its texture before anything reads
it. */

struct xgpu_target_description
{
	/* its size in pixels; a depth and stencil target (else color); its
	layers (1; 2 for the VR mode's eyes, a layer each, drawn into with
	multiview; 4 foveated: xgpu_foveated) */
	unsigned long width, height;
	BOOL depth;
	int layers;
	/* a colour target's format: XGPU_FORMAT_BGRA8, or XGPU_FORMAT_RGBA16F
	(the back buffer with display.hdr) */
	int format;
};

void xgpu_backend_target_create(xgpu_target target, const struct xgpu_target_description *description);
void xgpu_backend_target_delete(xgpu_target target);
/* the target drawn into with these samples a pixel (0: none, its own
texture); and its multisampled pixels into its texture */
void xgpu_backend_target_multisample(xgpu_target target, int samples);
void xgpu_backend_target_resolve(xgpu_target target);

/* draws go to color and depth from now on (0 for none), color's VR HUD
group's target instead where hud_group is one (vr.h's HALO_VR_HUD_GROUP_*,
else -1); views: the layers drawn at once from layer base (1, or 2 or 4 with
multiview), or -1 or -2 the first or second layer alone; samples: into
their multisampled attachments (0: their textures) */
void xgpu_backend_set_targets(xgpu_target color, int hud_group, xgpu_target depth, int views, int base, int samples);

/* clears the bound targets' rectangles (left, top from the first row,
width, height, in their pixels; count of them): XGPU_CLEAR_* */
enum
{
	XGPU_CLEAR_RED = 1, XGPU_CLEAR_GREEN = 2, XGPU_CLEAR_BLUE = 4, XGPU_CLEAR_ALPHA = 8,
	XGPU_CLEAR_COLOR = 16, XGPU_CLEAR_DEPTH = 32, XGPU_CLEAR_STENCIL = 64,
};
void xgpu_backend_clear(unsigned long flags, const float color[4], float depth, unsigned long stencil,
	const long (*rectangles)[4], unsigned long count);
/* a target cleared whole to color, outside any draw */
void xgpu_backend_clear_target(xgpu_target target, const float color[4]);

/* a target's pixels (a layer of a layered one, or -1) as BGRA rows from
its first, malloc'd: display.hdr's back buffer clamped to [0, 1], or with
tone_map tone mapped into it (xgpu_hdr.h's xgpu_hdr_sdr_pixel). Answered
now; a screenshot can wait for the frame's end. */
unsigned char *xgpu_backend_read_target(xgpu_target target, int layer, BOOL tone_map);
/* the screenshot key's picture (controls.screenshot): the target, tone
mapped as xgpu_backend_read_target's, written by the host as a PNG file in
directory (made if it is missing), named for the local date and time; its
name (or why it failed) into name. Answered now. */
BOOL xgpu_backend_screenshot(xgpu_target target, const char *directory, char *name, unsigned long name_size);

/* ---------- textures */

/* how a texture is sampled */
enum
{
	XGPU_TEXTURE_KIND_2D, XGPU_TEXTURE_KIND_3D, XGPU_TEXTURE_KIND_CUBE, XGPU_TEXTURE_KIND_2D_ARRAY,
};

/* an Xbox texture's description as its cache has it (its levels, and
whether a high-res replacement is drawn in its place: text_hires.c,
menu_files.c, hud_hires.c), the texture made or refreshed from guest memory
now (xbox_textures.c). Answered now: the replacements are found by the
texture's address, which the front end could do itself once the
replacements' bookkeeping leaves their GL textures (a later step). */
void xgpu_backend_texture_describe(DWORD resource, DWORD palette, struct xgpu_texture_description *description,
	int *kind);

enum
{
	XGPU_FILTER_NONE, XGPU_FILTER_POINT, XGPU_FILTER_LINEAR, XGPU_FILTER_ANISOTROPIC,
};

enum
{
	XGPU_ADDRESS_WRAP, XGPU_ADDRESS_MIRROR, XGPU_ADDRESS_CLAMP, XGPU_ADDRESS_BORDER,
	/* (u and v) a region of the texture, sampled as a texture of its own
	whose border is beyond it (d3d8_texture_region): the draw's
	texture_scale for the stage its width and height, then its left and
	top, in the texture's coordinates; the shader draws the border */
	XGPU_ADDRESS_REGION = 5,
	/* (w, of a 2D texture) its texels are the high-res text's distance
	fields (text_hires.c, xgpu_shader.h's XGPU_MSDF_RANGE): the shader samples the median
	of red, green and blue as white of the coverage of the field's edge,
	smoothed over a pixel of the screen (xgpu_shader.h's
	XGPU_BORDER_MSDF); repeated on w, which a 2D texture has not */
	XGPU_ADDRESS_MSDF = 6,
};

/* a sampler (zeroed, then filled: compared whole) */
struct xgpu_sampler
{
	/* XGPU_FILTER_*: minification, between mip levels (none: no mip
	levels), magnification */
	unsigned char minify, mip, magnify;
	/* XGPU_ADDRESS_* */
	unsigned char address[3];
	unsigned char pad[2];
	float lod_bias;
	/* the most detailed level sampled */
	unsigned long first_level;
	unsigned long anisotropy;
	/* ARGB, 8 bits each */
	unsigned long border_color;
};

/* what a stage samples */
enum
{
	XGPU_SOURCE_NONE,
	/* an Xbox texture: its header's guest address, its palette's (0 none) */
	XGPU_SOURCE_TEXTURE,
	/* a render target; with mip levels (levels more than 0), its levels'
	targets (each the written serial it has: copied again when it
	changes), the levels past them made from the last; drawn: the target
	the draw draws into (a backend that cannot sample it samples a copy) */
	XGPU_SOURCE_TARGET,
};

#define XGPU_MIP_LEVELS 16

struct xgpu_texture_source
{
	unsigned char source;
	/* how its shader samples it (_xgpu_sampler_*: xgpu.h) */
	unsigned char sampler_type;
	/* bound for a stage that samples nothing (the pixel shader's dot
	product stage) */
	unsigned char sampled;
	unsigned char pad;
	DWORD resource, palette;
	/* (and the target's written serial) */
	xgpu_target target, drawn;
	unsigned long written;
	unsigned long data, width, height, levels, rendered_levels;
	xgpu_target level_targets[XGPU_MIP_LEVELS];
	unsigned long level_written[XGPU_MIP_LEVELS];
	struct xgpu_sampler sampler;
};

void xgpu_backend_set_textures(const struct xgpu_texture_source stages[4]);

/* ---------- pipelines */

/* the vertex constants c[first .. first + count) (hardware registers: c[-96]
is 0) set to values, as the game sets them; the backend keeps them, and
uploads to each program only what changed since it last had them */
void xgpu_backend_set_constants(unsigned long first, const float (*values)[4], unsigned long count);

/* the vertex constant register of each of the per-pixel lighting's
(XGPU_MODEL_LIGHT_COUNT): c[-82], then c[-79] to c[-69], then c[-57]
(halo_dynamic_lights.h's HALO_DYNAMIC_LIGHTS_OWN_LIGHTS_REGISTER) */
#define XGPU_MODEL_LIGHT_REGISTER(light) ((unsigned long)(XGPU_VERTEX_CONSTANT_BIAS + \
	((light) == XGPU_MODEL_LIGHT_OWN_LIGHTS ? -57 : (light) ? -80 + (light) : -82)))

/* the uniforms a draw sets besides the vertex constants, converted from
the render and texture stage states */
struct xgpu_draw_uniforms
{
	float viewport_scale[4];
	float viewport_offset[4];
	float point_size;
	float ps_c0[8][4];
	float ps_c1[8][4];
	float ps_final_c0[4];
	float ps_final_c1[4];
	float fog_color[4];
	float fog_parameters[4];
	float alpha_reference;
	float bump_matrix[4][4];
	float bump_luminance[4][4];
	float texture_scale[4][4];
	float screen_offset;
	float texture_lod_bias[4];
	/* display.ambient_occlusion "world": the draw's object's owner slot
	(-1 none) and reference (xgpu.h; d3d8_device.c's halo_world_ao_object),
	and with "ray_traced" its owner among the ray-traced scene's instances
	(its datum index plus one, 0 none: xgpu_stream.h's
	xgpu_world_ao_instance); 1 where the draw is a two-sided model shader's
	cards (halo_world_ao_cards: their occlusion about their most open
	direction) */
	float world_ao[4];
};

/* ---------- the pipeline key's values */

enum
{
	XGPU_COMPARE_NEVER, XGPU_COMPARE_LESS, XGPU_COMPARE_EQUAL, XGPU_COMPARE_LESS_EQUAL, XGPU_COMPARE_GREATER,
	XGPU_COMPARE_NOT_EQUAL, XGPU_COMPARE_GREATER_EQUAL, XGPU_COMPARE_ALWAYS,
};

enum
{
	XGPU_BLEND_ZERO, XGPU_BLEND_ONE, XGPU_BLEND_SOURCE_COLOR, XGPU_BLEND_ONE_MINUS_SOURCE_COLOR,
	XGPU_BLEND_SOURCE_ALPHA, XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA, XGPU_BLEND_DESTINATION_ALPHA,
	XGPU_BLEND_ONE_MINUS_DESTINATION_ALPHA, XGPU_BLEND_DESTINATION_COLOR, XGPU_BLEND_ONE_MINUS_DESTINATION_COLOR,
	XGPU_BLEND_SOURCE_ALPHA_SATURATE, XGPU_BLEND_CONSTANT_COLOR, XGPU_BLEND_ONE_MINUS_CONSTANT_COLOR,
	XGPU_BLEND_CONSTANT_ALPHA, XGPU_BLEND_ONE_MINUS_CONSTANT_ALPHA,
};

enum
{
	XGPU_BLEND_ADD, XGPU_BLEND_SUBTRACT, XGPU_BLEND_REVERSE_SUBTRACT, XGPU_BLEND_MIN, XGPU_BLEND_MAX,
};

enum
{
	XGPU_STENCIL_KEEP, XGPU_STENCIL_ZERO, XGPU_STENCIL_REPLACE, XGPU_STENCIL_INCREMENT_CLAMP,
	XGPU_STENCIL_DECREMENT_CLAMP, XGPU_STENCIL_INVERT, XGPU_STENCIL_INCREMENT_WRAP, XGPU_STENCIL_DECREMENT_WRAP,
};

enum
{
	XGPU_FILL_SOLID, XGPU_FILL_LINES, XGPU_FILL_POINTS,
};

/* the color write mask's bits */
enum
{
	XGPU_WRITE_RED = 1, XGPU_WRITE_GREEN = 2, XGPU_WRITE_BLUE = 4, XGPU_WRITE_ALPHA = 8,
};

/* the targets' formats */
enum
{
	XGPU_FORMAT_NONE, XGPU_FORMAT_BGRA8, XGPU_FORMAT_D24S8,
	/* half floats: the back buffer with display.hdr (xgpu_hdr.h) */
	XGPU_FORMAT_RGBA16F,
	/* (display.ambient_occlusion "world"'s pre-pass's inputs: four 32-bit
	unsigned words, xgpu.h's XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS; the
	backends' own target, never the game's) */
	XGPU_FORMAT_WORLD_AO_INPUTS,
};

/* the vertex shader's lighting variant: none, a model lighting program's
for each pixel (its lights: 1 the ambient and distant lights, 2 the point
lights too), a structure's lightmap pass's for the enhanced dynamic lights */
enum
{
	XGPU_LIGHTING_NONE, XGPU_LIGHTING_MODEL, XGPU_LIGHTING_MODEL_POINT, XGPU_LIGHTING_ENVIRONMENT,
	/* (a model lighting program lit for each vertex, its ambient light
	handed on apart: xgpu.h's XGPU_AMBIENT_OCCLUSION_LIGHT_VERTEX) */
	XGPU_LIGHTING_MODEL_SPLIT,
};

/* ---------- the pipeline key

Zeroed before it is filled (keys are hashed and compared whole). */

struct xgpu_pipeline_key
{
	/* the vertex program (its microcode's hash: xgpu_shader.h), drawn from
	immediate mode's registers (all floats) or from streams with these
	attributes packed (NORMPACKED3), lit as XGPU_LIGHTING_*, into these
	views (XGPU_MULTIVIEW_*: xgpu.h) */
	unsigned long long vertex_program;
	unsigned long packed_attributes;
	/* (the stencil masks tested and written: below) */
	unsigned long stencil_read_mask, stencil_write_mask;
	unsigned char immediate, lighting, views;
	/* blend: enabled, the source and destination factors (XGPU_BLEND_*) and
	the operation, and the channels written (XGPU_WRITE_*) */
	unsigned char blend, blend_source, blend_destination, blend_operation;
	unsigned char color_mask;
	/* depth: tested (XGPU_COMPARE_*), written */
	unsigned char depth_test, depth_function, depth_write;
	/* stencil: tested, the operations where the stencil test fails, where
	the depth test does, and where both pass (XGPU_STENCIL_*), and the masks
	(above) */
	unsigned char stencil_test, stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
	/* raster: culled; the front faces' winding where the Xbox's screen has
	them (rows from the top), counter-clockwise or not, and the front faces
	culled (else the back); filled (XGPU_FILL_*); depth biased (the bias is
	dynamic) */
	unsigned char cull, front_counter_clockwise, cull_front;
	unsigned char fill, depth_bias;
	/* the targets: their formats (XGPU_FORMAT_*) and samples a pixel */
	unsigned char color_format, depth_format, samples;
	unsigned char pad[2];
	/* the pixel shader */
	struct nv2a_pixel_shader_key pixel;
};

/* ---------- dynamic state */

struct xgpu_dynamic_state
{
	/* the viewport and the scissor rectangle in the targets' pixels (left,
	top from the first row, width, height), the depth range */
	long viewport[4];
	long scissor[4];
	float depth_range[2];
	/* the stencil test's reference; the blend color (RGBA, 0 to 1); the
	depth bias, its slope factor and its offset in the depth's units */
	unsigned long stencil_reference;
	float blend_color[4];
	float depth_bias[2];
};

/* ---------- reversed depth

The front end describes depth as the game has it: 0 at the near plane, 1
at the far, a function less (nearer) passing, a clear to 1, a bias toward
the camera negative, in the 24-bit depth buffer's units. The Metal and
Vulkan backends keep it reversed, 1 - depth, in a floating-point buffer:
the game's projection puts a point 100 m away at 0.9989, where a float, as
the Xbox's 24-bit integers, steps 1/16777216 at a time, about 9 mm there
(more by the sniper rifle's view than the eye can see: a light a few
millimetres off its wall, as Blood Gulch's bases' are, fights the wall); near
0, where reversed depth puts that point, a float steps a million times finer.
Their emitters write w - z (as one dot product of the projection's rows
where the program has them: xgpu_vertex_program_depth_rows), so that the
precision is not lost first to the vertex's coordinates; the backends test
with each function's mirror, clear to 1 - depth, take the depth range as
1 - its ends, read depth as 1 - it, and add the constant bias in the shader
(the same distance of depth as the Xbox's units, which a float's own units
are not) and the slope's with its sign turned (xgpu_stream.h's
xgpu_reversed_depth_function and xgpu_reversed_depth_bias). Nothing the
game sees changes. */

/* a vertex program the keys name (by its hash), as the game makes it (id:
the vertex shader's number, for the logs), and where a model lighting
program has its normal and position (nv2a_vertex_shader_lighting), for its
per-pixel lighting */
void xgpu_backend_vertex_program(unsigned long id, const struct xgpu_vertex_program *program);
void xgpu_backend_vertex_lighting(unsigned long long program, const struct nv2a_vertex_lighting *lighting);

/* makes the pipeline of a key the draws' from now on, made the first time;
FALSE if its shaders do not build. Answered now (a draw lit for each pixel
falls back to the vertex's lighting when its shaders fail); it could wait:
the backend would fall back itself and say so for the frames after. */
BOOL xgpu_backend_pipeline(const struct xgpu_pipeline_key *key);
/* the number of the key xgpu_backend_pipeline was given last: one key's
alone (with the times the numbers have begun again), so that the front
end's map preparation tells draws apart by it, not by the key's bytes */
unsigned long xgpu_backend_pipeline_number(void);
/* display.ambient_occlusion "world": the pipeline bound, one with the
world's light, has a stand-in (a number of xgpu_backend_pipeline_number's;
0 none), the pipeline the draw would have without the world, which the
backend may draw with until the world's is made (it never makes the
world's while a map is prepared) */
void xgpu_backend_pipeline_stand_in(unsigned long number);
/* the pipeline's state, the dynamic state and the uniforms (converted
anew when uniforms_serial changes) set for the draws that follow */
void xgpu_backend_draw_state(const struct xgpu_dynamic_state *dynamic, const struct xgpu_draw_uniforms *uniforms,
	unsigned long uniforms_serial);

/* ---------- vertices and draws */

/* a vertex declaration's element formats */
enum
{
	XGPU_VERTEX_NONE_FORMAT,
	XGPU_VERTEX_FLOAT1, XGPU_VERTEX_FLOAT2, XGPU_VERTEX_FLOAT3, XGPU_VERTEX_FLOAT4,
	/* 4 bytes, BGRA, 0 to 1 */
	XGPU_VERTEX_COLOR,
	XGPU_VERTEX_SHORT1, XGPU_VERTEX_SHORT2, XGPU_VERTEX_SHORT3, XGPU_VERTEX_SHORT4,
	/* signed, -1 to 1 */
	XGPU_VERTEX_NORMSHORT1, XGPU_VERTEX_NORMSHORT2, XGPU_VERTEX_NORMSHORT3, XGPU_VERTEX_NORMSHORT4,
	/* unsigned bytes, 0 to 1 */
	XGPU_VERTEX_BYTE1, XGPU_VERTEX_BYTE2, XGPU_VERTEX_BYTE3, XGPU_VERTEX_BYTE4,
	/* 11, 11 and 10 bits in a 32-bit integer, unpacked by the vertex shader */
	XGPU_VERTEX_PACKED3,
	/* three floats read as the Xbox's two and a w (FLOAT2H) */
	XGPU_VERTEX_FLOAT2H,
};

struct xgpu_vertex_element
{
	unsigned char reg;
	unsigned char stream;
	/* XGPU_VERTEX_*, and its bytes */
	unsigned char format;
	unsigned char bytes;
	unsigned short offset;
};

/* the contiguous window's mirror (d3d8_device.c): guest memory held in the
backend's buffers, a segment of XGPU_MIRROR_SEGMENT_SIZE bytes each, written
by the page from guest memory (unused: no draw yet has read the pages) */
#define XGPU_MIRROR_SEGMENT_SIZE 0x400000UL
void xgpu_backend_mirror_write(unsigned long segment, unsigned long offset, unsigned long size, const void *data,
	BOOL unused);

/* the vertices a draw reads: its declaration (a number the backend may
cache its layout by: the vertex shader's, the front end's; its elements and
the inputs fed NORMPACKED3), each stream's vertices (from the draw's first:
in the mirror's segment at offset, or segment -1 and streamed from guest
memory at data, bytes of them), and the values of the inputs no stream
feeds */
struct xgpu_vertex_input
{
	unsigned long declaration;
	unsigned long element_count;
	struct xgpu_vertex_element elements[XGPU_VERTEX_ATTRIBUTE_COUNT];
	unsigned long packed_mask;
	struct
	{
		BOOL present;
		long segment;
		unsigned long offset;
		unsigned long data;
		unsigned long bytes;
		unsigned long stride;
	} streams[16];
	float attributes[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
};

void xgpu_backend_set_vertices(const struct xgpu_vertex_input *input);

/* primitives (the Xbox's quads made triangles by the front end) */
enum
{
	XGPU_POINTS, XGPU_LINES, XGPU_LINE_LOOP, XGPU_LINE_STRIP, XGPU_TRIANGLES, XGPU_TRIANGLE_STRIP,
	XGPU_TRIANGLE_FAN,
};

/* draws count vertices; or indices, uploaded, of the vertices from the
streams' first; or an indexed draw whose indices are vertices from minimum
on (the streams' first), from the mirror (segment 0 or more) or streamed
from guest memory (temporary: the front end's, made for this draw, which a
backend keeping index arrays drawn again and again must not keep) */
void xgpu_backend_draw(int primitive, unsigned long count);
void xgpu_backend_draw_indices(int primitive, const WORD *indices, unsigned long count);
void xgpu_backend_draw_indexed(int primitive, const WORD *indices, unsigned long count, unsigned long minimum,
	long segment, unsigned long offset, BOOL temporary);
/* immediate mode: count vertices of all 16 registers (four floats each),
the registers written (a bit each), the others' values (attributes); the
quads' triangles' indices when there are some */
void xgpu_backend_draw_immediate(int primitive, const float *vertices, unsigned long count, unsigned long written,
	const float (*attributes)[4], const WORD *indices, unsigned long index_count);

/* ---------- visibility (occlusion) tests

A test counts the samples its draws pass into a slot; the result is in the
game's pixels: the front end gives the target's pixels for each of the
game's (area), the views drawn at once (the VR mode's eyes) and the samples
a pixel, which the backend divides by as its way of counting needs. */

#define XGPU_VISIBILITY_TEST_SLOTS 4096
/* the largest count: the game reads one as a long and halts on a negative
one (rasterizer_xbox_widgets.c, "(occlusion_test_result&0x80000000)==0") */
#define XGPU_VISIBILITY_COUNT_MOST 0x7fffffffU

void xgpu_backend_visibility_begin(void);
/* test: the front end's number for the test (never 0, one more each test,
modulo 2^32), which the count is published with */
void xgpu_backend_visibility_end(unsigned long slot, float area, int views, int samples, unsigned long test);
/* the latest count known for the slot (0: none yet; at most
XGPU_VISIBILITY_COUNT_MOST), and in test the number of the test it is of
(0: none yet): the count is of that test or a later one of the slot.
Answered now; it could wait: the counts are frames old already (the GPU's:
a slot's latest count may be of a test several frames before the latest
test of it), and a backend elsewhere would write them into guest memory
the front end reads) */
UINT xgpu_backend_visibility_result(unsigned long slot, unsigned long *test);

/* ---------- frames */

/* the end of a frame's commands: the flat screen's (the back buffer,
letterboxed into the window, then the window shown; the visibility tests
copied out first) */
void xgpu_backend_visibility_frame(void);
void xgpu_backend_present(xgpu_target back_buffer);
/* after either present: textures, streamed data and counters for the
next frame */
void xgpu_backend_frame_end(void);

/* ---------- enhanced dynamic lights (display.dynamic_lights)

The front end works out which lights reach which of the grid's cells
(xgpu.h); the backend holds the block and the cells, and draws the
glows. */

/* the block and cells made, the first time; FALSE if they cannot be
(answered now; it could wait: the lights would be classic from the next
frame on) */
BOOL xgpu_backend_dynamic_lights_prepare(void);
/* the block, and the cells with the clusters' masks past them
(XGPU_DYNAMIC_LIGHTS_CELLS_BYTES) */
void xgpu_backend_dynamic_lights_upload(const struct xgpu_dynamic_lights_block *block, const void *cells);
/* the world (xgpu.h): count texels of four words, when the structure
changes; the backend keeps it until the next */
void xgpu_backend_dynamic_lights_world(const void *texels, int count);

/* the glows: each one's middle and size, its color, two vec4 each; the
camera's right and up; the world's projection (c[-96] to c[-93]) and the
screen conversion (c[-38], c[-37]) and the viewport's, as the structure's
lightmap pass began; the camera's position; the glow's softness against
what it meets (world units); the menus' screen offset */
struct xgpu_dynamic_lights_halos
{
	int count;
	float halos[XGPU_DYNAMIC_LIGHTS_MAXIMUM * 2][4];
	float camera_right[3], camera_up[3];
	float projection[6][4];
	float viewport_scale[4], viewport_offset[4];
	float camera[3];
	float softness;
	float screen_offset;
};

void xgpu_backend_dynamic_lights_halos(xgpu_target color, xgpu_target depth, int layers,
	const struct xgpu_dynamic_lights_halos *halos);

/* ---------- the VR mode (HALO_VR; vr.h): the backend's part

The front end keeps the passes (vr.h's HALO_VR_PASS_*) and which views a
draw goes to; the backend holds the eyes' images and the foveated layers,
the HUD groups' targets, the eye corrections (halo_vr_set_eye_transforms),
and composes the frame for OpenXR at the present (host_vulkan_vr.c). */

/* in: the eyes', HUD's and scope's sizes, debug.gpu_stats, and with it
debug.vr_gpu_time (gpu_time: the settings file's, which a backend in the
host, whose settings are the environment's alone, could not read); out: the
eye pass foveated (vr.foveation: xgpu_foveated) and its layers' size, the
visibility tests counted in the pixel shaders */
struct xgpu_vr_info
{
	int eye_width, eye_height;
	int hud_width, hud_height;
	int scope_size;
	BOOL statistics;
	BOOL foveated;
	int fovea_width, fovea_height;
	BOOL gpu_time;
};

/* waits until the shaders and pipelines of the draws so far are made, where
the backend makes them apart from the draws (a map's, drawn through every
shader as it starts: port/linux/game/render_enumerate.c), and returns the
seconds waited; 0 where each is made at its draw */
double xgpu_backend_prepared(void);
/* the draws from now until xgpu_backend_prepared are made for their shaders
and pipelines alone (not seen: the frame's own window draws over them) */
void xgpu_backend_prepare(void);
/* The frontend's preparation key includes the VR sky's depth clamp. */
void halo_gpu_preparation_sky(int sky);

/* the backend can draw both eyes at once (FALSE: playing flat, logged).
Both answered as the device is made, before any frame. */
BOOL xgpu_backend_vr_supported(void);
void xgpu_backend_vr_initialize(struct xgpu_vr_info *info);
/* the eye pass begins: the eyes' target (eye_target, the back buffer's in
the eye pass) is drawn into this frame: the eyes' swapchain image (image,
OpenXR's), or foveated, the four layers made into it at the present */
void xgpu_backend_vr_eyes(xgpu_target eye_target, unsigned int image);
/* a pass begins (HALO_VR_PASS_*) */
void xgpu_backend_vr_pass(int pass);
/* the HUD's groups' targets (xgpu_backend_set_targets' hud_group) are
cleared as each is first drawn into in a frame: from now on */
void xgpu_backend_vr_groups_reset(void);
/* debug.gpu_stats: a frame's marks (vr_timing) */
void xgpu_backend_vr_timing(int mark);

/* the present: compose (the fovea's layers into the eyes' images, the
cockpit display, the scope's view, the feed, tint, vignette, rim and visor,
the eyes' depth, the HUD and its panels into their images), then submit the
frame to OpenXR; the targets each pass drew (NULL if it did not) */
struct xgpu_vr_frame
{
	BOOL eyes_drawn, hud_drawn, scope_drawn;
	/* the HUD has panels (vr_visor.c): its groups' targets copied into
	their images */
	BOOL hud_panels;
	xgpu_target eye_target, eye_depth, scope, hud;
	int eye_depth_layers;
};

void xgpu_backend_vr_compose(const struct xgpu_vr_frame *frame);
void xgpu_backend_vr_submit(const struct xgpu_vr_frame *frame, BOOL screenshot);

/* The time the frame after the one being written will take more than the
frames do (a load zone's switch, which it begins with), told with this
frame and each after it until 0, or XGPU_FRAME_EXTRA_FRAMES of them (a
switch put off while a tick's sub-steps run, or not made): the host starts
that frame so much earlier, so that it is on screen at its refresh all the
same. */
#define XGPU_FRAME_EXTRA_FRAMES 4
void xgpu_backend_frame_extra(unsigned long microseconds);
/* Acknowledged loading ownership change, before map storage is reused. */
void xgpu_backend_loading(int active);
int xgpu_backend_loading_owned(void);
/* Copy the 640 by 480 RGBA loading picture before returning; NULL clears. */
void xgpu_backend_loading_art(const unsigned char *rgba);
/* The main menu's scene the loading screen draws (loading_scene.h), copied
before returning; no bytes return to the picture. */
void xgpu_backend_loading_scene(const void *data, unsigned long bytes);
/* The load's progress (0 to 1), as the loading screen's sweep is drawn: the
picture fades out as it completes, as the original's did. */
void xgpu_backend_loading_progress(float progress);
/* How the sweep moves: following the progress, once across as it goes
from 0 to 1, as the Xbox's did (1), or looping on its own clock (0, a map
load's). */
void xgpu_backend_loading_mode(int follow_progress);
/* A line under the loading screen's words: its coverage, width by height
(a byte a pixel, set at four times the 480 lines' size); NULL: none. */
void xgpu_backend_loading_caption(const unsigned char *coverage, unsigned long width, unsigned long height);

#endif
