/*
XGPU_METAL.H

The Metal backend (xgpu_metal.m): what the renderer's backend table calls on
macOS. It runs in the host, on the render thread that replays each frame's
commands (one thread: none of these functions may be called from two at
once, except where a comment says so).

The renderer's front end describes each draw as plain data (a pipeline key,
dynamic state, bindings; port/linux/src/xgpu_backend.h). These functions
take those descriptions with fixed-width fields, so that a guest's
descriptions (32-bit longs) and the host's agree, and Metal objects never
leave this backend: textures and shaders are named by 32-bit handles.

The backend:
- caches render pipeline states by the part of a pipeline that Metal bakes
  (shaders, vertex layout, blend, write mask, formats, samples) and
  depth-stencil states by the depth and stencil part; the rest (cull,
  winding, fill, depth bias, depth clip) is encoder state. New pipelines are
  added to a binary archive, saved to a file, which the next run's pipelines
  are made from;
- caches samplers by their description;
- makes textures in the Xbox formats Metal has (BGRA8, the 16-bit packed
  ones, L8, A8, A8L8 by swizzles, DXT1-5 as BC1-3, the depth formats), and
  uploads into them in the order of the frame's commands;
- reads guest data in place: a range of the guest's memory is wrapped in a
  buffer (newBufferWithBytesNoCopy, on whole host pages) and drawn from
  without a copy; what the game rewrites while the GPU reads it is copied
  into the frame's upload ring instead (xgpu_metal_stream);
- begins a render pass at the first draw or clear after set_targets
  (load actions "load"; "clear" where the first thing done is a clear of a
  whole attachment), and draws other clears as quads;
- counts samples for visibility tests in a visibility result buffer;
- keeps at most two frames in flight, each with its fence.
*/

#ifndef __HALO_MACOS_XGPU_METAL_H
#define __HALO_MACOS_XGPU_METAL_H

#include <stddef.h>
#include <stdint.h>

#include "xgpu_metal_bindings.h"

/* ---------- the interface's values (port/linux/src/xgpu_backend.h)

The pipeline description's comparison, blend, stencil and fill fields hold
the interface's XGPU_* values. Until that header lands they are repeated
here, as the renderer split's snapshot has them. */

#ifndef __HALO_LINUX_XGPU_BACKEND_H
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

enum
{
	XGPU_WRITE_RED = 1, XGPU_WRITE_GREEN = 2, XGPU_WRITE_BLUE = 4, XGPU_WRITE_ALPHA = 8,
};
struct loading_presentation_frame;
uint64_t host_metal_loading(void *context, const struct loading_presentation_frame *frame);
int host_metal_loading_owned(void *context);
void host_metal_loading_art(void *context, const uint8_t *rgba);
void host_metal_loading_scene(void *context, const void *data, uint32_t bytes);
void host_metal_loading_progress(void *context, float progress);
void host_metal_loading_mode(void *context, int follow_progress);
void host_metal_loading_caption(void *context, const unsigned char *coverage, uint32_t width, uint32_t height);

#endif

/* ---------- formats

The interface has XGPU_FORMAT_NONE, _BGRA8 and _D24S8 for targets; the
textures' formats are still to come. These are the backend's, a superset. */

enum
{
	XGPU_METAL_FORMAT_NONE,
	/* D3DFMT_A8R8G8B8 and X8R8G8B8 (alpha read as 1) */
	XGPU_METAL_FORMAT_BGRA8,
	XGPU_METAL_FORMAT_BGRX8,
	/* the 16-bit packed formats: R5G6B5, A1R5G5B5, X1R5G5B5, A4R4G4B4 */
	XGPU_METAL_FORMAT_R5G6B5,
	XGPU_METAL_FORMAT_A1R5G5B5,
	XGPU_METAL_FORMAT_X1R5G5B5,
	XGPU_METAL_FORMAT_A4R4G4B4,
	/* L8 (read as l, l, l, 1), A8 (0, 0, 0, a), A8L8 (l, l, l, a) */
	XGPU_METAL_FORMAT_L8,
	XGPU_METAL_FORMAT_A8,
	XGPU_METAL_FORMAT_A8L8,
	/* DXT1 (BC1), DXT2 and DXT3 (BC2), DXT4 and DXT5 (BC3) */
	XGPU_METAL_FORMAT_DXT1,
	XGPU_METAL_FORMAT_DXT3,
	XGPU_METAL_FORMAT_DXT5,
	/* depth and stencil: D24S8 is 32-bit float depth and 8-bit stencil
	here (Apple's GPUs have no 24-bit depth); D16 */
	XGPU_METAL_FORMAT_D24S8,
	XGPU_METAL_FORMAT_D16,
	/* the high-res replacements' pictures: bytes red, green, blue, alpha;
	and a byte each, white's coverage (read as 1, 1, 1, the byte) */
	XGPU_METAL_FORMAT_RGBA8,
	XGPU_METAL_FORMAT_COVERAGE,
	/* half floats: the back buffer with display.hdr (xgpu_hdr.h) */
	XGPU_METAL_FORMAT_RGBA16F,
	/* ASTC 4x4 (LDR): DXT textures transcoded on a GPU without BC
	(port/linux/astc/xgpu_astc.h) */
	XGPU_METAL_FORMAT_ASTC,
	/* a signed byte (display.ambient_occlusion "world"'s distance field) */
	XGPU_METAL_FORMAT_R8_SNORM,
	/* the backend's own targets: a byte (red), two (red, green) */
	XGPU_METAL_FORMAT_R8,
	XGPU_METAL_FORMAT_RG8,
	/* (display.ambient_occlusion "world" found once a pixel: the pre-pass's
	inputs, four 32-bit unsigned integers; the occlusion and what the
	surfaces upsample it by, four floats) */
	XGPU_METAL_FORMAT_RGBA32UI,
	XGPU_METAL_FORMAT_RGBA32F,
	/* two signed bytes (display.ambient_occlusion "world"'s distance-field
	scene's cascades) */
	XGPU_METAL_FORMAT_RG8_SNORM,
	XGPU_METAL_FORMAT_COUNT
};

enum
{
	XGPU_METAL_TEXTURE_2D, XGPU_METAL_TEXTURE_3D, XGPU_METAL_TEXTURE_CUBE, XGPU_METAL_TEXTURE_2D_ARRAY,
};

/* a texture's usage (bits) */
enum
{
	XGPU_METAL_USAGE_SAMPLED = 1,
	/* drawn into */
	XGPU_METAL_USAGE_TARGET = 2,
};

struct xgpu_metal_texture_description
{
	/* XGPU_METAL_TEXTURE_*, XGPU_METAL_FORMAT_*, XGPU_METAL_USAGE_* */
	uint8_t type, format, usage;
	/* a target's samples a pixel (0 or 1: not multisampled): it is drawn
	into a multisampled attachment the backend keeps, resolved into the
	texture at the end of each pass */
	uint8_t samples;
	uint16_t width, height;
	/* a 3D texture's depth, an array's layers (a cube has 6 faces) */
	uint16_t depth_or_layers;
	uint16_t levels;
	/* the channels sampled: 0 the format's, else 0x100 and two bits for
	each of red, green, blue and alpha (red's lowest), the format's channel
	each is taken from (a Custom Edition map's: xgpu_stream.h) */
	uint16_t channels;
};

/* ---------- samplers */

enum
{
	XGPU_METAL_FILTER_POINT, XGPU_METAL_FILTER_LINEAR,
};

/* the mip filter: none (level 0 alone), nearest level, between levels */
enum
{
	XGPU_METAL_MIP_NONE, XGPU_METAL_MIP_POINT, XGPU_METAL_MIP_LINEAR,
};

enum
{
	XGPU_METAL_ADDRESS_WRAP, XGPU_METAL_ADDRESS_MIRROR, XGPU_METAL_ADDRESS_CLAMP, XGPU_METAL_ADDRESS_BORDER,
	XGPU_METAL_ADDRESS_MIRROR_ONCE,
	/* XGPU_ADDRESS_REGION: the shader's (xgpu_metal_border_axes), clamped
	to the edge here */
	XGPU_METAL_ADDRESS_REGION,
	/* XGPU_ADDRESS_MSDF (on w): the shader's (xgpu_metal_border_axes),
	repeated here */
	XGPU_METAL_ADDRESS_MSDF = 6,
};

/* the bit of a stage's border axes for a region (xgpu_shader.h's
XGPU_BORDER_REGION) */
#define XGPU_METAL_BORDER_REGION 8
/* the bit of a stage's border axes for the high-res text's distance fields
(xgpu_shader.h's XGPU_BORDER_MSDF) */
#define XGPU_METAL_BORDER_MSDF 16

/* the border colours Metal has; another is the emitter's to blend in (the
design notes, 7) */
enum
{
	XGPU_METAL_BORDER_TRANSPARENT_BLACK, XGPU_METAL_BORDER_OPAQUE_BLACK, XGPU_METAL_BORDER_OPAQUE_WHITE,
};

/* Zeroed before it is filled (it is compared whole). Metal samplers have no
LOD bias: the shader applies it (the emitter's texture_lod_bias, as ES). */
struct xgpu_metal_sampler
{
	uint8_t min_filter, mag_filter, mip_filter;
	uint8_t address_u, address_v, address_w;
	uint8_t border;
	/* 1 for none */
	uint8_t max_anisotropy;
	/* depth compared (XGPU_COMPARE_*, for a depth texture), or 0: none */
	uint8_t compare, compare_function;
	uint8_t pad[2];
	float min_lod, max_lod;
};

/* ---------- pipelines */

/* vertex attributes' formats: the interface's XGPU_VERTEX_* (the Xbox's
D3DVSDT_*), in its order */
enum
{
	XGPU_METAL_ATTRIBUTE_NONE,
	XGPU_METAL_ATTRIBUTE_FLOAT1, XGPU_METAL_ATTRIBUTE_FLOAT2, XGPU_METAL_ATTRIBUTE_FLOAT3, XGPU_METAL_ATTRIBUTE_FLOAT4,
	/* D3DCOLOR: bytes b, g, r, a, read as r, g, b, a (UChar4Normalized_BGRA) */
	XGPU_METAL_ATTRIBUTE_D3DCOLOR,
	XGPU_METAL_ATTRIBUTE_SHORT1, XGPU_METAL_ATTRIBUTE_SHORT2, XGPU_METAL_ATTRIBUTE_SHORT3, XGPU_METAL_ATTRIBUTE_SHORT4,
	XGPU_METAL_ATTRIBUTE_NORMSHORT1, XGPU_METAL_ATTRIBUTE_NORMSHORT2, XGPU_METAL_ATTRIBUTE_NORMSHORT3,
	XGPU_METAL_ATTRIBUTE_NORMSHORT4,
	/* unsigned bytes, normalized (D3DVSDT_PBYTE*) */
	XGPU_METAL_ATTRIBUTE_PBYTE1, XGPU_METAL_ATTRIBUTE_PBYTE2, XGPU_METAL_ATTRIBUTE_PBYTE3, XGPU_METAL_ATTRIBUTE_PBYTE4,
	/* NORMPACKED3, read as a 32-bit integer the shader unpacks */
	XGPU_METAL_ATTRIBUTE_PACKED,
	/* FLOAT2H: three floats */
	XGPU_METAL_ATTRIBUTE_FLOAT2H,
	XGPU_METAL_ATTRIBUTE_COUNT
};

/* an attribute no stream feeds: read from the constant inputs
(xgpu_metal_set_constant_attributes), its register's vec4 */
#define XGPU_METAL_STREAM_CONSTANT 0xff

struct xgpu_metal_vertex_attribute
{
	/* XGPU_METAL_ATTRIBUTE_* (NONE: not read), the stream it is read
	from and its offset in each vertex */
	uint8_t format, stream;
	uint16_t offset;
};

/* What draws need of a pipeline: the shaders (xgpu_metal_shader_create's
handles; the MSL emitter's functions for the front end's vertex program
and variant, and its pixel program), the vertex layout (attribute n is
[[attribute(n)]]) and the pipeline key's fixed-function fields, named as
struct xgpu_pipeline_key names them. Zeroed before it is filled. */
struct xgpu_metal_pipeline
{
	uint32_t vertex_shader, pixel_shader;
	struct xgpu_metal_vertex_attribute attributes[16];
	uint16_t strides[XGPU_METAL_STREAM_COUNT];
	/* blend (XGPU_BLEND_*), the channels written (XGPU_WRITE_*) */
	uint8_t blend, blend_source, blend_destination, blend_operation;
	uint8_t color_mask;
	/* depth (XGPU_COMPARE_*) */
	uint8_t depth_test, depth_function, depth_write;
	/* stencil (XGPU_COMPARE_*, XGPU_STENCIL_*) */
	uint8_t stencil_test, stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
	uint8_t stencil_read_mask, stencil_write_mask;
	/* raster: the Xbox screen's front winding (rows from the top, which
	Metal's are too), XGPU_FILL_*, depth biased (the values are dynamic) */
	uint8_t cull, front_counter_clockwise, cull_front;
	uint8_t fill, depth_bias;
	/* depth clamped instead of clipped */
	uint8_t depth_clamp;
	/* the targets' formats (XGPU_METAL_FORMAT_*) and samples */
	uint8_t color_format, depth_format, samples;
	uint8_t pad[3];
};

/* the dynamic state (struct xgpu_dynamic_state), in the targets' pixels */
struct xgpu_metal_dynamic_state
{
	int32_t viewport[4];
	int32_t scissor[4];
	float depth_range[2];
	uint32_t stencil_reference;
	float blend_color[4];
	/* the slope factor and the offset in the depth's units */
	float depth_bias[2];
};

/* ---------- data the GPU reads */

enum
{
	/* nothing */
	XGPU_METAL_SOURCE_NONE,
	/* copied into this frame's upload ring (xgpu_metal_stream) */
	XGPU_METAL_SOURCE_RING,
	/* the guest's memory, read in place (offset: a guest address); the
	front end keeps it unchanged until the frame's fence */
	XGPU_METAL_SOURCE_GUEST,
	/* the command stream's ring (xgpu_metal_set_stream_ring; offset in
	it): inline data, read in place */
	XGPU_METAL_SOURCE_STREAM_RING,
	/* a buffer of the backend's (handle: xgpu_metal_buffer_create) */
	XGPU_METAL_SOURCE_BUFFER,
};

struct xgpu_metal_source
{
	uint32_t kind;
	uint32_t offset;
	uint32_t size;
	uint32_t handle;
};

enum
{
	XGPU_METAL_PRIMITIVE_POINTS, XGPU_METAL_PRIMITIVE_LINES, XGPU_METAL_PRIMITIVE_LINE_STRIP,
	XGPU_METAL_PRIMITIVE_TRIANGLES, XGPU_METAL_PRIMITIVE_TRIANGLE_STRIP,
	/* Metal has neither: the front end converts them (the capabilities
	say so), and the backend refuses them */
	XGPU_METAL_PRIMITIVE_TRIANGLE_FAN, XGPU_METAL_PRIMITIVE_LINE_LOOP,
};

/* the clears' parts: the interface's XGPU_CLEAR_* (the colour's channels
are cleared where their bits are set too) */
enum
{
	XGPU_METAL_CLEAR_RED = 1, XGPU_METAL_CLEAR_GREEN = 2, XGPU_METAL_CLEAR_BLUE = 4, XGPU_METAL_CLEAR_ALPHA = 8,
	XGPU_METAL_CLEAR_COLOR = 16, XGPU_METAL_CLEAR_DEPTH = 32, XGPU_METAL_CLEAR_STENCIL = 64,
	XGPU_METAL_CLEAR_CHANNELS = 15,
};

/* ---------- the backend */

struct xgpu_metal_options
{
	/* the binary archive's file (NULL: none) */
	const char *archive_path;
	/* shaders and pipelines made before the call that wants them returns
	(the tests): else on the backend's queues */
	int synchronous;
	/* the library of the shaders built ahead of time (NULL: none), which
	xgpu_metal_shader_create looks a function up in before it compiles */
	const char *library_path;
	/* the host's page size, which wrapped guest ranges are aligned to
	(0: the system's) */
	uint32_t page_size;
	/* the extent of readable memory around a guest range: sets *start and
	*end (guest addresses, whole host pages, inside [limit_start,
	limit_end)) and returns 1, or returns 0 if the range is not all
	readable. NULL: guest data is always copied. Called on the render
	thread. */
	int (*guest_extent)(uint32_t address, uint32_t size, uint32_t limit_start, uint32_t limit_end,
		uint32_t *start, uint32_t *end);
	/* the large pass parked for the frame's first aside passes goes on
	(0) rather than ending there (1: the frames identical, to the pixel, to
	those drawn without aside passes; xgpu_metal.m, "aside passes"): one
	pass for the whole 3D view, and with MSAA its samples never leave tile
	memory, but a few pixels of some frames differ. HALO_METAL_ASIDE_BREAK
	chooses either. */
	int aside_continue;
};

/* what the front end's descriptions depend on */
struct xgpu_metal_capabilities
{
	uint32_t largest_target;
	/* the most samples a target may have (a power of 2) */
	uint32_t samples;
	uint32_t anisotropy;
	/* border colours: transparent black, opaque black and opaque white
	alone */
	uint8_t border_colors;
	uint8_t base_vertex;
	uint8_t fans, line_loops;
	uint8_t fill_modes;
	/* the GPU and the system, in a few words (a report's:
	port/linux/src/report.c) */
	char device[96];
};

/* 1 on success (a Metal device); host_metal.m calls it */
int xgpu_metal_initialize(const struct xgpu_metal_options *options);
/* saves the binary archive and lets go of everything */
void xgpu_metal_shutdown(void);
/* the process is ending: the binary archive saved (any thread; waits a
second at most, then leaves it as it was) */
void xgpu_metal_exiting(void);
void xgpu_metal_capabilities(struct xgpu_metal_capabilities *capabilities);

/* an MSL function (the source's function named entry, a name only that
source has): the app's library's; else made on the backend's queues (never
on the calling thread): a pixel program's with its configuration as the
combiner's function's constants (xgpu_metal_combiner.h; NULL: one the
combiner does not draw) is the combiner's specialised for it at once, and
its own compiled in the background takes its place. Returns its handle,
the same for the same entry, or 0 */
uint32_t xgpu_metal_shader_create(const char *source, size_t size, const char *entry);
uint32_t xgpu_metal_pixel_shader_create(const char *source, size_t size, const char *entry,
	const uint32_t *constants);

/* a pipeline made on the backend's queues for the draws to come (a map's,
while it loads); waits until every pipeline prepared so is made, and
returns the seconds waited */
void xgpu_metal_prepare(const struct xgpu_metal_pipeline *pipeline);
/* the draws until xgpu_metal_prepared are made for their pipelines alone
(not drawn: a map's first frame's, unseen) */
void xgpu_metal_prepare_begin(void);
double xgpu_metal_prepared(void);
/* whether a pipeline is made or being made (a draw's: as the map's
preparation made it, or an earlier draw asked for it) */
int xgpu_metal_pipeline_known(const struct xgpu_metal_pipeline *pipeline);
/* whether a pipeline is made (a draw of it is drawn, not skipped) */
int xgpu_metal_pipeline_made(const struct xgpu_metal_pipeline *pipeline);

/* textures: a handle, or 0 (logged) */
uint32_t xgpu_metal_texture_create(const struct xgpu_metal_texture_description *description);
/* whether the GPU samples DXT's blocks (BC1 to BC3): Apple silicon Macs and
iPads with an M chip do; iPhones and the iOS simulator do not, and their
DXT textures are ASTC (xgpu_metal_textures.c, port/linux/astc/xgpu_astc.h)
or decoded */
int xgpu_metal_block_compression(void);
/* whether the GPU samples ASTC's LDR blocks: every Apple GPU (the Apple2
family and later: every iPhone the app runs on, Apple silicon Macs), not an
Intel Mac's */
int xgpu_metal_astc(void);
/* whether the GPU has the SIMD-groups' reductions in fragment functions
(simd_or): the Apple7 family (A14, M1) and later and Apple silicon Macs; not
the A12 or A13 an iPhone of the oldest the app runs on may have */
int xgpu_metal_simd_reductions(void);
/* (after the frames in flight that use it) */
void xgpu_metal_texture_destroy(uint32_t texture);
/* makes its levels past the first from the first, in the frame's order */
void xgpu_metal_texture_make_levels(uint32_t texture);
/* copies a texture's first level and layer into another's (as much of it
as both have; the same format), in the frame's order: the pass drawing
into it ends first, so that a draw can sample what was drawn before it
(Metal cannot sample a texture its pass draws into) */
void xgpu_metal_texture_copy(uint32_t source, uint32_t destination);
/* a multisampled target's attachment resolved into its texture now, in the
frame's order (the pass drawing into it ends first): a depth target's,
which the passes store and do not resolve, for a draw that reads the depth
(the enhanced dynamic lights' glows) */
void xgpu_metal_texture_resolve(uint32_t texture);
/* writes a region of a level and layer (a cube's face, a 3D texture's
slices z to z + depth); bytes_per_row is a compressed format's row of
blocks. Ordered with the frame's draws: draws before it see the old texels,
draws after it the new ones. */
void xgpu_metal_texture_upload(uint32_t texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t z,
	uint32_t width, uint32_t height, uint32_t depth, const void *data, uint32_t bytes_per_row,
	uint32_t bytes_per_image);

/* frames: begin waits while two frames are in flight */
void xgpu_metal_frame_begin(void);
/* ends the frame (presenting a texture, 0 none: with a window, a 2D
target's texture itself is given to the presentation, and the target has
another, which has its pictures where they are used before a clear of the
whole of it) and returns its fence */
uint64_t xgpu_metal_frame_end(uint32_t present);
/* the last frame the GPU has finished (any thread) */
uint64_t xgpu_metal_frame_completed(void);
void xgpu_metal_frame_wait(uint64_t fence);

/* copies data into this frame's upload ring */
struct xgpu_metal_source xgpu_metal_stream(const void *data, uint32_t size);
/* a guest range to read in place */
struct xgpu_metal_source xgpu_metal_guest(uint32_t address, uint32_t size);
/* the guest's pages from address to address + size were mapped again or
unmapped: buffers made over them no longer see them (any thread) */
void xgpu_metal_guest_remapped(uint32_t address, uint32_t size);

/* the command stream's ring (host memory, whole host pages), wrapped in a
buffer that draws read inline data from in place; 1 on success */
int xgpu_metal_set_stream_ring(void *ring, uint32_t size);

/* a GPU buffer of the backend's (the mirror's segments): a handle, or 0 */
uint32_t xgpu_metal_buffer_create(uint32_t size);
/* its bytes, as the CPU last wrote them (a write in the frame's order
lands later) */
const void *xgpu_metal_buffer_contents(uint32_t buffer);
/* writes into it: in the frame's order (draws before it read the old
bytes), or, unused (no work the GPU still has reads those bytes), at once */
void xgpu_metal_buffer_write(uint32_t buffer, uint32_t offset, const void *data, uint32_t size, int unused);

/* begins drawing into a colour target's level and layer and a depth
target (0: none): into their multisampled attachments (resolved into them
at the pass's end) or, multisampled 0, into them */
void xgpu_metal_set_targets(uint32_t color, uint32_t color_level, uint32_t color_layer, uint32_t depth,
	int multisampled);
/* rectangles (left, top, width, height in the targets' pixels; count 0:
the whole target) of the parts XGPU_METAL_CLEAR_* */
void xgpu_metal_clear(const int32_t (*rectangles)[4], uint32_t count, uint32_t parts, const float color[4],
	float depth, uint32_t stencil);
void xgpu_metal_set_pipeline(const struct xgpu_metal_pipeline *pipeline);
void xgpu_metal_set_dynamic_state(const struct xgpu_metal_dynamic_state *state);
/* a stage's texture (0: none) and sampler */
void xgpu_metal_set_texture(uint32_t stage, uint32_t texture, const struct xgpu_metal_sampler *sampler);
/* registers first to first + count of the 192 */
void xgpu_metal_set_vertex_constants(uint32_t first, uint32_t count, const float *values);
void xgpu_metal_set_draw_uniforms(const void *data, uint32_t size);
void xgpu_metal_set_stream(uint32_t stream, struct xgpu_metal_source source);
/* the enhanced dynamic lights' block and their grid's cells, which the
pixel shaders that light with them read (XGPU_METAL_BUFFER_DYNAMIC_LIGHTS,
_DYNAMIC_LIGHT_CELLS): until set again (XGPU_METAL_SOURCE_NONE: none bound;
a ring's source is the frame's alone) */
void xgpu_metal_set_dynamic_lights(struct xgpu_metal_source block, struct xgpu_metal_source cells);
/* their world (xgpu.h), which the same shaders read
(XGPU_METAL_BUFFER_DYNAMIC_LIGHT_WORLD): until set again */
void xgpu_metal_set_dynamic_light_world(struct xgpu_metal_source world);
/* display.ambient_occlusion "world"'s field, its atlas and its table (3D
textures: 0 none), the static scenery's pages (a 2D array, at
XGPU_METAL_TEXTURE_WORLD_AO_SCENERY, the fragments'), the vehicles' occlusion fields' atlas (a 3D texture,
at XGPU_METAL_TEXTURE_WORLD_AO_OBJECTS, the fragments'), and the window's block (struct xgpu_world_ao_block),
which the shaders that read them take at XGPU_METAL_TEXTURE_WORLD_SDF (with
a constexpr sampler of their own), _WORLD_SDF_TABLE and
XGPU_METAL_BUFFER_WORLD_AO, both stages: until set again (a ring's source
is the frame's alone) */
void xgpu_metal_set_world_ao(uint32_t field, uint32_t table, uint32_t scenery, uint32_t objects,
	struct xgpu_metal_source block);
/* their resources (xgpu_metal_world_ao.c): slices z_first to z_first +
z_count - 1 of a width x height x depth part of the field
(XGPU_STREAM_WORLD_AO_TABLE or _ATLAS), in the frame's order (width 0: both
let go of; until both have their last slices, 1x1x1 far-outside stand-ins
are bound), and the window's block from now on (NULL: none, zeros) */
struct xgpu_world_ao_block;
void xgpu_metal_world_ao_field(uint32_t part, uint32_t width, uint32_t height, uint32_t depth, uint32_t z_first,
	uint32_t z_count, const void *data);
void xgpu_metal_world_ao_window(const struct xgpu_world_ao_block *block);
/* ... found once a pixel: the texture of it the draws that read it read
from now on (XGPU_METAL_TEXTURE_WORLD_AO_PIXELS; 0: its stand-in, 1); and
the backend's, which the fragments read */
void xgpu_metal_world_ao_set_pixels(uint32_t texture);
void xgpu_metal_set_world_ao_pixels(uint32_t texture);
/* a box of the distance-field scene's cascades (xgpu_stream.h's
WORLD_AO_CASCADES; width 0 released), and their texture (a stand-in of
nothing where there is none) */
void xgpu_metal_world_ao_cascades(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth,
	const void *data);
uint32_t xgpu_metal_world_ao_cascades_texture(void);
/* the values of the 16 inputs no stream feeds */
void xgpu_metal_set_constant_attributes(const float (*values)[4]);
/* indices: 16-bit, or XGPU_METAL_SOURCE_NONE */
void xgpu_metal_draw(uint32_t primitive, uint32_t count, uint32_t first_vertex, int32_t base_vertex,
	struct xgpu_metal_source indices);

/* the draws between begin and end count their samples (each sample that
passes the tests) for the test's slot (0 to 4095), which end names; the
count is known when the frame is finished */
void xgpu_metal_visibility_begin(void);
/* area: the samples a pixel of the game's is (the target's scale, its
samples, its views), which the count written for the game is divided by;
number: the game's number for the test, written with its count */
void xgpu_metal_visibility_end(uint32_t slot, float area, uint32_t number);
/* a slot's latest count of samples and the frame it is from (0: none yet;
any thread) */
uint64_t xgpu_metal_visibility_result(uint32_t slot, uint32_t *samples);
/* where each slot's latest count is also written, divided by its area: a
count of the game's pixels, as the game reads it (any thread); then, after
XGPU_METAL_VISIBILITY_SLOTS counts, the number of the test each count is of,
written after the count (a release store: xgpu_stream.h) */
void xgpu_metal_set_visibility_output(volatile uint32_t *counts);
#define XGPU_METAL_VISIBILITY_SLOTS 4096
/* the largest count (the game halts on a negative one: xgpu_backend.h,
XGPU_VISIBILITY_COUNT_MOST) */
#define XGPU_METAL_VISIBILITY_MOST 0x7fffffffU

/* commits the work so far (the frame goes on in a new command buffer);
returns its fence */
uint64_t xgpu_metal_fence(void);

/* reads a level's pixels (BGRA8 textures: 4 bytes a pixel; depth: 4-byte
floats, as the game has it, not reversed) after the work done so far; waits for the GPU (screenshots,
tests). Returns 1 on success. */
int xgpu_metal_read_pixels(uint32_t texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y,
	uint32_t width, uint32_t height, void *pixels, uint32_t bytes_per_row);

/* what the passes from now on draw, which HALO_METAL_PASS_TIMES sums the
GPU's time by (xgpu_metal.m, "the passes' GPU times"): the game's
renderer's categories (xgpu_metal_stream.m), the backend's own passes */
enum
{
	XGPU_METAL_PASS_OTHER,
	XGPU_METAL_PASS_SKY,
	XGPU_METAL_PASS_WORLD,
	XGPU_METAL_PASS_TRANSPARENTS,
	XGPU_METAL_PASS_WATER,
	XGPU_METAL_PASS_EFFECTS,
	XGPU_METAL_PASS_HUD,
	XGPU_METAL_PASS_TONE_MAP,
	XGPU_METAL_PASS_ANTI_ALIASING,
	XGPU_METAL_PASS_PANINI,
	/* (display.ambient_occlusion "world" found once a pixel: its pass) */
	XGPU_METAL_PASS_WORLD_AO,
	XGPU_METAL_PASS_KINDS
};
/* sets what the passes from now on draw; returns what it was */
uint32_t xgpu_metal_pass_kind(uint32_t kind);

/* A frame's GPU time pass by pass, for a caller (the replay's cost:
port/tools/replay): the GPU's timestamps at each encoder's stages, as
HALO_METAL_PASSES's. xgpu_metal_timing_request: the next frame begun is
timed (0 if the GPU has no timestamps at its stages: nothing is);
xgpu_metal_timing_draw: (the render thread) the draws handed on from now
are the frame's index'th (UINT32_MAX: none of the caller's);
xgpu_metal_timing_wait: waits up to seconds for the timed frame's GPU work
to be done, then gives the time the GPU was busy with it, its passes (in
the order they were encoded; each one's time its fragment stage's, or its
encoder's, from its start or the end of those before it to its end, so
that they sum to the busy time) and each draw's pass (UINT32_MAX: none),
until the next request; 0 if it was not timed in that time. */
struct xgpu_metal_pass_time
{
	const char *kind, *what;
	uint32_t draws, width, height;
	/* its span, its time (the frame's part), its stages' own times (a
	blit's its encoder's, as fragment) */
	double start_ms, end_ms, ms, vertex_ms, fragment_ms;
};
void xgpu_metal_timing_request(void);
void xgpu_metal_timing_draw(uint32_t index);
int xgpu_metal_timing_wait(double seconds, double *busy_ms, const struct xgpu_metal_pass_time **passes,
	uint32_t *pass_count, const uint32_t **draw_pass, uint32_t *draws);

/* statistics since the last call */
struct xgpu_metal_statistics
{
	uint32_t frames, passes, draws, clears_as_quads, clears_as_loads;
	uint32_t pipelines_made, pipelines_from_archive, depth_stencil_states_made, samplers_made;
	uint32_t uploads_direct, uploads_ordered;
	/* copies of a target being drawn, for a draw that samples it */
	uint32_t feedback_copies;
	/* passes of small targets put aside (xgpu_metal.m: encoded before the
	large pass they would have ended), and their targets given another
	texture for it */
	uint32_t passes_aside, targets_renamed;
	/* back buffers given to the presentation whose pictures a frame used
	before clearing them (copied back) */
	uint32_t pictures_restored;
	uint32_t guest_buffers_made, guest_bytes_copied;
	uint64_t ring_bytes;
	double pipeline_seconds;
	/* the GPU's time on the command buffers completed, and their count; the
	render thread's CPU time between frames' ends */
	double gpu_seconds;
	uint32_t command_buffers;
	/* the time the GPU was busy with any command buffer, the
	presentation's included (they overlap) */
	double gpu_busy_seconds;
	double render_cpu_seconds;
	/* the shaders compiled, and the time it took (the combiner's
	specialised counted in it); those found in the library instead, those
	the combiner's function was specialised for; draws not drawn for want of
	a pipeline (made meanwhile) */
	uint32_t shaders_made, shaders_from_library, shaders_specialised, draws_waiting;
	double shader_seconds;
};
void xgpu_metal_statistics(struct xgpu_metal_statistics *statistics);

/* the shaders and pipelines made since the start, and the time it took (on
the render thread): what a late frame waited for (any thread) */
struct xgpu_metal_compile_totals
{
	uint32_t shaders, pipelines;
	double shader_seconds, pipeline_seconds;
};
void xgpu_metal_compile_totals(struct xgpu_metal_compile_totals *totals);

#endif
