/*
XGPU_STREAM.H

The renderer's command stream: a frame's backend calls (xgpu_backend.h)
written by the game as plain data into a ring in its own memory, and
played back by a backend on the 64-bit host, on a thread of its own, while
the game goes on to the next frame. On the Linux arm64 build, Android and
macOS the game is ILP32 guest code: one host call a frame hands the frame
over instead of a crossing for each GL call, the backend's driver work
leaves the game's thread, and in the VR mode the host can take the newest
head pose just before it submits.

Both sides include this file, so it holds only fixed-width integers and
floats, laid out alike by the guest's ILP32 compiler and the host's LP64
one: every field is 4 bytes or smaller and aligned to its size, every
record a multiple of 4 bytes (the sizes are checked at the end). The
interface's own structures cannot cross as they are: unsigned long and
DWORD are 4 bytes in the guest and 8 on the host.

The guest writes (xgpu_stream_encode.c), the host reads
(port/android/host/host_render_stream.c, which calls the backend's
functions: host_render_stream.h). Guest data is a guest address: the
host's view of it is host_address(address) (port/android/host/host.h). Data
the game rewrites while the frame waits (streamed vertices, indices,
immediate mode, constants, uniforms, mirror writes, the lights) is copied
into the ring after its record ("inline": its address is in the ring);
what is too large for the ring (more than an eighth of it) is copied into
a buffer of its own in guest memory, which the guest frees once the host
has released the record. Textures and palettes are read where they are, as
the Xbox's GPU reads them; their changes are the write tracking's
(memory_watch.c). Refer to "The renderer's command stream" in
port/linux/README.md.
*/

#ifndef __HALO_XGPU_STREAM_H
#define __HALO_XGPU_STREAM_H

#include <stdint.h>

#define XGPU_STREAM_MAGIC 0x53504758u /* "XGPS" */
#define XGPU_STREAM_VERSION 23
#define XGPU_STREAM_LOADING_ART_BYTES (640u * 480u * 4u)

/* ---------- the ring

The guest's: a header and the ring's bytes, both in guest memory, the bytes
aligned to XGPU_STREAM_RING_ALIGNMENT and a power of two in size (so that a
Metal backend can wrap them in one buffer without copying:
newBufferWithBytesNoCopy). Positions are running counts of bytes written
since the stream began, modulo 2^32: byte position p is at buffer + (p &
(size - 1)). The guest writes records from committed on, at most to
released + size; it publishes them with host_render_stream_submit, which
also counts the frames; the host releases bytes once the GPU is done with
the work that read them (the backend's fence). */

#define XGPU_STREAM_RING_ALIGNMENT 0x4000u
/* records and inline data: records start at 4 bytes, inline data at 16
(vertex data's alignment in every API) */
#define XGPU_STREAM_RECORD_ALIGNMENT 4u
#define XGPU_STREAM_DATA_ALIGNMENT 16u

struct xgpu_stream_ring
{
	/* XGPU_STREAM_MAGIC, XGPU_STREAM_VERSION, sizeof(struct xgpu_stream_ring) */
	uint32_t magic, version, header_size;
	/* the ring's bytes: guest address and size (a power of two) */
	uint32_t buffer, size;
	/* the most frames submitted and not yet retired (the GPU done with
	them): 2, the frame the host draws and the one the game writes */
	uint32_t frames_in_flight;
	/* the host's answers, in guest memory the guest allocates:
	visibility: XGPU_STREAM_VISIBILITY_SLOTS counts (uint32_t), the latest
	known for each slot, each at most 0x7fffffff (the game reads them as
	longs and halts on a negative one: XGPU_VISIBILITY_COUNT_MOST), then
	XGPU_STREAM_VISIBILITY_SLOTS test numbers (uint32_t), each the number
	of the test whose count the slot's is (the visibility end record's
	test; 0: none yet), written after its count with a release store, so
	that a guest reading the number first (an acquire load) reads a count
	of that test or a later one; pipelines: XGPU_STREAM_PIPELINES states (uint8_t,
	XGPU_STREAM_PIPELINE_*) of the pipeline ids */
	uint32_t visibility, pipelines;

	/* the guest's (written before each host_render_stream_submit, read by
	the host in it) */
	uint32_t committed;
	uint32_t frames_submitted;

	/* the host's (read by the guest with acquire loads) */
	uint32_t released;
	uint32_t frames_retired;
	/* positions decoded; nonzero after a malformed record (then the host
	decodes no more: XGPU_STREAM_ERROR_*) and where it was */
	uint32_t decoded;
	uint32_t error, error_position;

	/* statistics: the guest's (bytes of the last frame, the most a frame
	has had, VERTICES records sent again for a draw in a later submit)
	and the host's (the times and nanoseconds the guest waited for room, a
	frame to retire or a synchronous record; nanoseconds the last frame
	took to play, the backend's calls included) */
	uint32_t frame_bytes, frame_bytes_most;
	uint32_t vertices_again;
	uint32_t waits, wait_nanoseconds;
	uint32_t decode_nanoseconds;
	/* the host's display (display.hdr: written by the host whenever it
	looks, read by the guest between frames): whether it shows HDR (its
	potential headroom past XGPU_HDR_DISPLAY_HEADROOM, or an HDR swapchain),
	and its headroom now (a float's bits, 1.0 for none); its SDR white in
	nits (a float's bits: the system's, or the one the encoding takes where
	it gives none) and whether the system gave it */
	uint32_t display_hdr;
	uint32_t display_headroom;
	uint32_t display_white;
	uint32_t display_white_reported;
	/* the guest's, with each frame: microseconds more than its frames take
	that the next frame will (a load zone's switch in it): the host starts
	that frame so much earlier (display_wait) */
	uint32_t frame_extra_microseconds;
	/* the host's, running counts of microseconds modulo 2^32 (read by the
	guest with acquire loads at each frame presented: a report's,
	port/linux/src/report.c): the GPU's time on the frames, at each frame's
	end (host_render_stream_gpu_time: Metal's, the time the GPU was busy;
	Vulkan's, from each frame's first work to its last), and nonzero once
	the backend measures it; the render thread's time playing records (the
	backend's calls included); the guest's waits for the display's pacing
	(display_wait, a part of wait_nanoseconds) */
	uint32_t gpu_microseconds;
	uint32_t gpu_measured;
	uint32_t render_microseconds;
	uint32_t paced_microseconds;
};

enum
{
	XGPU_STREAM_ERROR_NONE,
	XGPU_STREAM_ERROR_RECORD,   /* an unknown type, or a size too small for it */
	XGPU_STREAM_ERROR_BOUNDS,   /* a record past committed, or inline data outside its record */
	XGPU_STREAM_ERROR_ID,       /* a pipeline or layout id out of range or never defined */
};

/* host_render_stream_submit's flags */
enum
{
	/* the records end a frame (after XGPU_STREAM_FRAME_END): returns once
	no more than frames_in_flight frames are unretired */
	XGPU_STREAM_SUBMIT_FRAME = 1,
	/* returns once the host has played the records and the GPU has done
	their work: a synchronous record's answer is written, data it named in
	place may change again */
	XGPU_STREAM_SUBMIT_SYNC = 2,
};

#define XGPU_STREAM_VISIBILITY_SLOTS 4096
#define XGPU_STREAM_PIPELINES 16384
#define XGPU_STREAM_LAYOUTS 4096

/* a pipeline id's state, written by the host when the backend first makes
it (the front end's lighting fallback reads it from the next frame) */
enum
{
	XGPU_STREAM_PIPELINE_UNKNOWN,
	XGPU_STREAM_PIPELINE_MADE,
	XGPU_STREAM_PIPELINE_FAILED,
};

/* ---------- records

Each starts with a word: its type in the low 8 bits, its size in bytes
(header and inline data included, a multiple of 4) in the high 24. A
record's inline data follows its fixed part, at the next 16-byte aligned
ring position, and lies inside the record. A record never wraps: one that
would not fit before the ring's end is preceded by XGPU_STREAM_SKIP of size
0, which skips to the end (any other SKIP skips its size).

Records named "synchronous" are submitted with XGPU_STREAM_SUBMIT_SYNC and
carry the guest address of their answer (reply). */

#define XGPU_STREAM_HEADER(type, size) ((uint32_t)(type) | ((uint32_t)(size) << 8))
#define XGPU_STREAM_HEADER_TYPE(header) ((header) & 0xffu)
#define XGPU_STREAM_HEADER_SIZE(header) ((header) >> 8)
#define XGPU_STREAM_RECORD_MAXIMUM 0xfffffcu

enum
{
	XGPU_STREAM_SKIP,
	/* the backend's life */
	XGPU_STREAM_INITIALIZE,            /* synchronous: xgpu_stream_capabilities */
	XGPU_STREAM_INVALIDATE,
	XGPU_STREAM_FLUSH,
	XGPU_STREAM_ANTI_ALIASING_PREPARE,
	XGPU_STREAM_ANTI_ALIAS,
	/* render targets */
	XGPU_STREAM_TARGET_CREATE,
	XGPU_STREAM_TARGET_DELETE,
	XGPU_STREAM_TARGET_MULTISAMPLE,
	XGPU_STREAM_TARGET_RESOLVE,
	XGPU_STREAM_SET_TARGETS,
	XGPU_STREAM_CLEAR,
	XGPU_STREAM_CLEAR_TARGET,
	XGPU_STREAM_READ_TARGET,           /* synchronous */
	/* textures */
	XGPU_STREAM_SET_TEXTURES,
	/* pipelines */
	XGPU_STREAM_CONSTANTS,
	XGPU_STREAM_VERTEX_PROGRAM,
	XGPU_STREAM_VERTEX_LIGHTING,
	XGPU_STREAM_PIPELINE_DEFINE,
	XGPU_STREAM_PIPELINE,
	XGPU_STREAM_DRAW_STATE,
	/* vertices and draws */
	XGPU_STREAM_MIRROR_WRITE,
	XGPU_STREAM_LAYOUT_DEFINE,
	XGPU_STREAM_VERTICES,
	XGPU_STREAM_DRAW,
	XGPU_STREAM_DRAW_INDICES,
	XGPU_STREAM_DRAW_INDEXED,
	XGPU_STREAM_DRAW_IMMEDIATE,
	/* visibility tests */
	XGPU_STREAM_VISIBILITY_BEGIN,
	XGPU_STREAM_VISIBILITY_END,
	XGPU_STREAM_VISIBILITY_FRAME,
	/* frames */
	XGPU_STREAM_PRESENT,
	XGPU_STREAM_FRAME_END,
	/* the enhanced dynamic lights */
	XGPU_STREAM_DYNAMIC_LIGHTS_PREPARE,
	XGPU_STREAM_DYNAMIC_LIGHTS_UPLOAD,
	XGPU_STREAM_DYNAMIC_LIGHTS_HALOS,
	/* the VR mode */
	XGPU_STREAM_VR_SUPPORTED,          /* synchronous: uint32_t */
	XGPU_STREAM_VR_INITIALIZE,         /* synchronous: xgpu_stream_vr_info, in and out */
	XGPU_STREAM_VR_EYES,
	XGPU_STREAM_VR_PASS,
	XGPU_STREAM_VR_GROUPS_RESET,
	XGPU_STREAM_VR_TIMING,
	XGPU_STREAM_VR_COMPOSE,
	XGPU_STREAM_VR_SUBMIT,
	/* the shaders and pipelines made */
	XGPU_STREAM_PREPARED,              /* synchronous: uint32_t, the milliseconds waited */
	XGPU_STREAM_PREPARE,
	/* the VR mode's state its composition reads, and what vr.c sets in the
	backend as the frame is drawn (vr.h) */
	XGPU_STREAM_VR_STATE,
	XGPU_STREAM_VR_EYE_TRANSFORMS,
	XGPU_STREAM_VR_SCREEN_CORRECTIONS,
	XGPU_STREAM_VR_SKY,
	XGPU_STREAM_VR_EYE_FEED,
	XGPU_STREAM_VR_EYE_TINT,
	XGPU_STREAM_LOADING,
	XGPU_STREAM_LOADING_OWNED,          /* synchronous: current presenter ownership */
	XGPU_STREAM_LOADING_ART,            /* value: byte count, followed by owned 640x480 RGBA pixels */
	XGPU_STREAM_LOADING_SCENE,          /* value: byte count, followed by the menu's scene (loading_scene.h) */
	XGPU_STREAM_LOADING_PROGRESS,       /* value: the load's progress, a float's bits (0 to 1) */
	XGPU_STREAM_LOADING_MODE,           /* value: 1 the sweep follows the progress (the Xbox's), 0 it loops */
	XGPU_STREAM_LOADING_CAPTION,        /* value: byte count, followed by a width and a height (uint32_t)
	                                       and that many bytes of a line's coverage; 0: none */
	/* display.hdr (xgpu_hdr.h) */
	XGPU_STREAM_HDR,
	XGPU_STREAM_TONE_MAP,
	/* the screenshot key's picture (controls.screenshot) */
	XGPU_STREAM_SCREENSHOT,            /* synchronous */
	/* display.picture_position: where a picture of another shape than the
	window sits in it (value: 0 centered, n + 1 n pixels from the top, no
	lower than the bottom) */
	XGPU_STREAM_PICTURE_POSITION,
	/* display.gamma: an SDR frame's curve as it is presented (value: the
	setting, a float's bits; 1 none) */
	XGPU_STREAM_GAMMA,
	/* display.panini (xgpu_backend_panini) */
	XGPU_STREAM_PANINI,
	/* display.ambient_occlusion "world" (xgpu_backend_world_ao_window,
	_field; docs/world-ao.md) */
	XGPU_STREAM_WORLD_AO_WINDOW,
	XGPU_STREAM_WORLD_AO_FIELD,
	/* the pipeline bound's stand-in (xgpu_backend_pipeline_stand_in) */
	XGPU_STREAM_PIPELINE_STAND_IN,
	/* the game's renderer's category of the draws from now on, for the
	backends' timing alone (xgpu_backend_category; value: XGPU_STREAM_CATEGORY_*) */
	XGPU_STREAM_CATEGORY,
	/* the enhanced dynamic lights' world (xgpu_backend_dynamic_lights_world) */
	XGPU_STREAM_DYNAMIC_LIGHTS_WORLD,
	/* display.ambient_occlusion "world" found once a pixel
	(xgpu_backend_world_ao_prepass) */
	XGPU_STREAM_WORLD_AO_PREPASS,
	/* display.ambient_occlusion "world"'s distance-field scene: a box of
	its cascades (xgpu_backend_world_ao_cascades) */
	XGPU_STREAM_WORLD_AO_CASCADES,
	/* display.ambient_occlusion "ray_traced": a mesh of the scene the GPU's
	ray queries trace, and the frame's instances of the meshes
	(xgpu_backend_world_ao_mesh, _instances) */
	XGPU_STREAM_WORLD_AO_MESH,
	XGPU_STREAM_WORLD_AO_INSTANCES,
	/* debug.render_marks: what the draws from now on belong to, for the
	inspector's replay (port/macos/tools/halo_replay.m); the backends play
	nothing for them (xgpu_stream_mark_*) */
	XGPU_STREAM_MARK_STRING,
	XGPU_STREAM_MARK_VIEW,
	XGPU_STREAM_MARK_OWNER,
	/* the enhanced dynamic lights' cells that changed
	(xgpu_backend_dynamic_lights_upload; DYNAMIC_LIGHTS_UPLOAD's records,
	of the whole, are the stream's version 22's) */
	XGPU_STREAM_DYNAMIC_LIGHTS_CHANGE,
	XGPU_STREAM_RECORD_TYPES,
};

/* a record of no more than its header: SKIP, INVALIDATE, FLUSH,
VISIBILITY_BEGIN, VISIBILITY_FRAME, DYNAMIC_LIGHTS_PREPARE,
VR_GROUPS_RESET, PREPARE */
struct xgpu_stream_record
{
	uint32_t header;
};

/* FRAME_END: when the game read the frame's input, nanoseconds of its
monotonic clock (0 unknown; a recording's older FRAME_END is its header
alone), for the time from it to the frame on screen (frame_pacing.h) */
struct xgpu_stream_frame_end
{
	uint32_t header;
	uint32_t input_low, input_high;
};

/* a record of one value: ANTI_ALIASING_PREPARE (smaa), TARGET_DELETE,
TARGET_RESOLVE, PRESENT (the back buffer's target), PICTURE_POSITION, GAMMA, VR_PASS, VR_TIMING
(mark), PIPELINE (the id, XGPU_STREAM_PIPELINE_DEFINE's), VR_SKY (sky) */
struct xgpu_stream_value
{
	uint32_t header;
	uint32_t value;
};

/* CATEGORY's value: the category (rasterizer.h's _rasterizer_profile_*,
XGPU_STREAM_CATEGORY_NONE outside them) */
#define XGPU_STREAM_CATEGORY_NONE 255u

/* ---------- debug.render_marks (port/linux/game/render_marks.c)

Written only with the setting on. A string a mark names is a number: the
first mark that names it is preceded by MARK_STRING, its number and its
bytes (inline, NUL-terminated); 0 names none. Numbers count from 1 since
the stream began and are never given again. A VIEW mark says where the
camera of the window drawn from now on is; an OWNER mark says what the
draws from now on, until the next OWNER mark, belong to. */

/* an owner's kind (the replay's names: halo_replay.m's mark_kind_names) */
enum
{
	XGPU_STREAM_MARK_OTHER,
	XGPU_STREAM_MARK_WORLD_OBJECT,
	XGPU_STREAM_MARK_FIRST_PERSON,
	XGPU_STREAM_MARK_BSP,
	XGPU_STREAM_MARK_SKY,
	XGPU_STREAM_MARK_DECAL,
	XGPU_STREAM_MARK_EFFECT,
	XGPU_STREAM_MARK_PARTICLE,
	XGPU_STREAM_MARK_CONTRAIL,
	XGPU_STREAM_MARK_LIGHTNING,
	XGPU_STREAM_MARK_WEATHER,
	XGPU_STREAM_MARK_WATER,
	XGPU_STREAM_MARK_HUD,
	XGPU_STREAM_MARK_TEXT,
	XGPU_STREAM_MARK_MENU_WIDGET,
	XGPU_STREAM_MARK_CINEMATIC_LETTERBOX,
	XGPU_STREAM_MARK_DEBUG,
	XGPU_STREAM_MARK_DETAIL_OBJECT,
	XGPU_STREAM_MARK_LENS_FLARE,
	XGPU_STREAM_MARK_FOG,
	XGPU_STREAM_MARK_SCREEN_EFFECT,
	XGPU_STREAM_MARK_LIGHT_VOLUME,
	XGPU_STREAM_MARK_SHADOW,
	XGPU_STREAM_MARK_CLEAR,
	XGPU_STREAM_MARK_KINDS,
};

#define XGPU_STREAM_MARK_NONE 0xffffffffu

/* MARK_STRING: then length bytes and a NUL (inline: at data) */
struct xgpu_stream_mark_string
{
	uint32_t header;
	uint32_t number, length, data;
};

/* MARK_VIEW: the map, the game's tick, the window (XGPU_STREAM_MARK_NONE:
the frame's start, no camera) and its camera: position, forward and up
(the world's units), the horizontal field of view (radians) and the
viewport in the back buffer's pixels (x, y, width, height) */
struct xgpu_stream_mark_view
{
	uint32_t header;
	uint32_t map, tick, window;
	float position[3], forward[3], up[3];
	float field_of_view;
	int32_t viewport[4];
};

/* MARK_OWNER: the kind (XGPU_STREAM_MARK_*), an id (an object's or a
widget's handle, a tag's index; XGPU_STREAM_MARK_NONE), and strings: a
label, the tags that matter (the object's definition, its model, the
shader, a bitmap), a menu widget's tag and the generator source that wrote
it (tools/menu_sources.py's) */
struct xgpu_stream_mark_owner
{
	uint32_t header;
	uint32_t kind, id;
	uint32_t label;
	uint32_t object, model, shader, bitmap;
	uint32_t widget, source;
};

/* ---------- the backend's life */

/* INITIALIZE's answer: xgpu_backend_capabilities, and the backend's own
(the enhanced dynamic lights can be made: xgpu_backend_dynamic_lights_prepare) */
struct xgpu_stream_capabilities
{
	int32_t largest_target;
	int32_t most_samples;
	int32_t most_anisotropy;
	uint8_t sample_counting, bgra_vertex_colors, base_vertex, sample_mask;
	uint8_t mixed_constant_blend, dynamic_lights, dynamic_lights_preparable, hdr;
	/* display.ambient_occlusion "ray_traced": the backend traces the
	occlusion's rays by the GPU's ray queries (xgpu_backend_world_ao_mesh,
	_instances: Vulkan's VK_KHR_ray_query) */
	uint8_t ray_queries, pad[3];
	/* the GPU, its API and driver, in a few words (a report's:
	port/linux/src/report.c), NUL terminated */
	char device[96];
};

struct xgpu_stream_reply_record
{
	uint32_t header;
	/* where the answer goes (guest address) */
	uint32_t reply;
};

struct xgpu_stream_anti_alias
{
	uint32_t header;
	uint32_t target;
	uint32_t smaa;
	int32_t corners[4];
};

/* PANINI: a window's 3D view drawn again through the Panini projection
(xgpu_backend_panini): its target, corners, and the projection (d, s, the
magnification at the middle, 0, and the rectilinear view's tangents at the
corners' left, right, top and bottom) */
struct xgpu_stream_panini
{
	uint32_t header;
	uint32_t target;
	int32_t corners[4];
	float projection[8];
};

/* HDR: display.hdr's state (xgpu_backend_hdr): drawn in HDR, the mode
(XGPU_HDR_MODE_*), paper white (in nits; 0: the system's SDR white, in
percent of it: xgpu_hdr_paper_white_nits), the UI's white (in nits; 0:
paper white's) */
struct xgpu_stream_hdr
{
	uint32_t header;
	uint32_t active, mode, paper_white_nits, paper_white, ui_nits;
};

/* TONE_MAP: a window's 3D view tone mapped (xgpu_backend_tone_map) */
struct xgpu_stream_tone_map
{
	uint32_t header;
	uint32_t target;
	int32_t corners[4];
};

/* ---------- display.ambient_occlusion "world" (docs/world-ao.md, #578)

As the GPU sees it, alike in the guest's front end and the hosts' backends.

- The field: one 3D texture of the current structure BSP's signed distance
  (world_sdf.h), R8 signed normalized (VK_FORMAT_R8_SNORM,
  MTLPixelFormatR8Snorm), linear filtering, clamped to its edge; a texel's
  value times the block's range is the distance in world units at the
  voxel's centre.
- The block: the field's place and the window's proxies (world_proxies.h's
  struct world_proxy), a uniform buffer (std140: vec4s only).

Bindings (the shaders: workstream D; the backends' descriptors: B):
- SPIR-V: XGPU_SPIRV_SET_OCCLUSION (a set of its own, so that a draw
  still binds at most four sets), bindings XGPU_SPIRV_BINDING_WORLD_SDF and
  XGPU_SPIRV_BINDING_WORLD_AO (xgpu_spirv.h);
- MSL: [[texture(XGPU_MSL_TEXTURE_WORLD_SDF)]] with a constexpr sampler in
  the shader, [[buffer(XGPU_MSL_BUFFER_WORLD_AO)]] (xgpu_msl.h).
*/

#define XGPU_WORLD_AO_PROXIES_MAXIMUM 64
/* a proxy's kind (its [0].w) */
#define XGPU_WORLD_AO_PROXY_SPHERE 1
#define XGPU_WORLD_AO_PROXY_CAPSULE 2
#define XGPU_WORLD_AO_PROXY_BOX 3
/* (a vehicle's occlusion field, over this proxy and the next two: [0]
the reach's centre, kind; [1] row 0 of the world to its voxels' place,
owner; [2] the three rows' translations, its reach (world units); then [0]
row 1, 0; [1] row 2, owner; [2] its last voxel's place (its sides less
one), 0; then [0] its place in the objects' atlas plus a half texel, 0;
[1] one over the atlas's size, owner; [2] its geometry's box's half
extents (voxels, about the grid's middle), 0: the box's bound
(world_proxy_box_fade's U) bounds the field's occlusion) */
#define XGPU_WORLD_AO_PROXY_FIELD 4

/* the uniform block: [0] the field's origin (its first voxel's centre) and
one over the voxel's size; [1] its width, height, depth (in voxels) and
range (world units); [2] the strength on objects, the strength on the level,
the field's fade (0 none yet: the proxies alone), the proxies' count; [3]
the reference's weight (how much of the occlusion at the object's lighting
point its ambient sample already holds) and three spare; then the
proxies, three vec4s each (world_proxies.h's struct world_proxy); then
their grid about the camera (world_ao.h's world_ao_grid): [0] the camera,
[1] each axis's scale and one over the cell, and the cells' 64-bit masks of
the proxies that can reach them, two cells a uvec4 */
#define XGPU_WORLD_AO_GRID_CELLS_MAXIMUM (16 * 16 * 8)
struct xgpu_world_ao_block
{
	float field[2][4];
	float parameters[2][4];
	float proxies[XGPU_WORLD_AO_PROXIES_MAXIMUM][3][4];
	float grid[2][4];
	uint32_t cells[XGPU_WORLD_AO_GRID_CELLS_MAXIMUM / 2][4];
};

/* WORLD_AO_WINDOW: display.ambient_occlusion "world"'s block for the
window's opaque surfaces from here on (xgpu_backend_world_ao_window):
active 0 none (the block's contents unused) */
struct xgpu_stream_world_ao_window
{
	uint32_t header;
	uint32_t active;
	uint32_t pad[2];
	struct xgpu_world_ao_block block;
};

/* ---------- display.ambient_occlusion "world" found once a pixel

The window's opaque surfaces are drawn twice (source/render/render.c): first
their inputs to the occlusion alone (the pre-pass: each draw with the
world's light, XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED, drawn by the backend
with its twin, the same pipeline but for its pixel key's
XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS and its target: xgpu_world_ao_twin),
into a target of the inputs (XGPU_FORMAT_WORLD_AO_INPUTS: four 32-bit
words a pixel, xgpu.h's) and a depth target of its own; then the
occlusion found once from them, at each 2x2 block's nearest pixel (a
quarter of the pixels: the pass's camera[3].w 2; 1 each pixel's), into a
texture of four floats a texel (the occlusion, the pixel's distance from
the eye, its normal octahedral); then the surfaces drawn, each reading its
pixel's (XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED), upsampled by distance and
normal. Nothing else is drawn in the pre-pass: the draws without the
world's light are skipped, its clears too.

WORLD_AO_PREPASS: phase XGPU_WORLD_AO_PREPASS_BEGIN (the draws from here
on are the pre-pass's, into targets of target's size), _END (the pre-pass
done: the occlusion found over the window's corners (left, top, right,
bottom, in target's pixels) as the camera saw it (struct
xgpu_world_ao_pass)), or _NONE (the occlusion found no more: the surfaces
read 1 from here on). While a map is prepared (the others then ignored):
_TWINS (the draws from here on would be drawn in the pre-pass in play:
each one's twin made with it, xgpu_stream_world_ao_twin) and _NONE (no
more) */
enum
{
	XGPU_WORLD_AO_PREPASS_NONE,
	XGPU_WORLD_AO_PREPASS_BEGIN,
	XGPU_WORLD_AO_PREPASS_END,
	XGPU_WORLD_AO_PREPASS_TWINS,
};
/* the camera, as the occlusion's pass finds a pixel's world position
(world_ao.h's world_ao_pass_position): [0] its position and its near
plane; [1] the direction (one unit along the view's axis) at the screen's
middle, and its far plane; [2] and [3] the direction's change a unit of
the screen's x (-1 its left edge, 1 its right) and y (-1 its top, 1 its
bottom), [2].w 0, [3].w the pixels a side of the blocks the occlusion is
found once for (1 or 2); the window's corners in its target's pixels
(left, top, right, bottom). A pixel's position: the eye (the block's
grid[0]) + its distance (from the pre-pass) along normalize([1] + x [2] +
y [3]). Then the distance-field scene's cascades, which the dynamic
objects' rays step through (world_cascades.h: each the first voxel's
centre and the voxel, 0 not ready); the window's previous frame's camera
and corners as these, where its occlusion is reprojected from; and history:
[0] 1 where the previous frame's occlusion is this window's (one window a
frame, its corners alike: the backend's previous texture of it), else 0;
[1] the frame's number (the rays' turn); [2] the share of the new frame's
occlusion blended into what is reprojected (1: none of the history); [3] 1
where the dynamic objects' occlusion is traced, 0 where their shapes are
walked instead (HALO_WORLD_AO_NO_TRACE, for measuring against), 2 where
it is traced by the GPU's ray queries ("ray_traced": WORLD_AO_MESH, below;
a backend without them, or before its scene is built, traces it as 1) */
struct xgpu_world_ao_pass
{
	float camera[4][4];
	int32_t corners[4];
	float cascades[4][4];
	float previous[4][4];
	int32_t previous_corners[4];
	float history[4];
};
struct xgpu_stream_world_ao_prepass
{
	uint32_t header;
	uint32_t phase;
	uint32_t target;
	uint32_t pad;
	struct xgpu_world_ao_pass pass;
};

/* PIPELINE_STAND_IN: pipeline id (one with the world's light) has a stand-in
(an id, 0 none), the pipeline the draw would have without the world: the
backend may draw with it until id's is made, and never makes id's while a
map is prepared (xgpu_backend_pipeline_stand_in) */
struct xgpu_stream_pipeline_stand_in
{
	uint32_t header;
	uint32_t id, stand_in;
};

/* WORLD_AO_FIELD: slices z_first to z_first + z_count - 1 of a part of
the field (a width * height * depth volume, x fastest:
xgpu_backend_world_ao_field): its table of cells (part 0, four bytes a
texel: an atlas brick's x, y, z and the cell's kind, read as RGBA8) or its
atlas of bricks (part 1, signed bytes: R8 snorm), or the static
scenery's pages of the level's occlusion (part 2, bytes, R8 unorm: a 2D
array, depth its layers, sent before the table), or the vehicles'
occlusion fields' atlas (part 3, four half floats a texel: RGBA16F, a 3D
volume, sent before the table), its bytes at data (size:
width * height * z_count * the texel's); width 0 releases the field, every
part */
#define XGPU_STREAM_WORLD_AO_TABLE 0
#define XGPU_STREAM_WORLD_AO_ATLAS 1
#define XGPU_STREAM_WORLD_AO_SCENERY 2
#define XGPU_STREAM_WORLD_AO_OBJECTS 3
#define XGPU_STREAM_WORLD_AO_TEXEL(part) ((part) == XGPU_STREAM_WORLD_AO_TABLE ? 4u : \
	(part) == XGPU_STREAM_WORLD_AO_OBJECTS ? 8u : 1u)
struct xgpu_stream_world_ao_field
{
	uint32_t header;
	uint32_t part;
	uint32_t width, height, depth;
	uint32_t z_first, z_count;
	uint32_t data, size;
	uint32_t pad;
};

/* WORLD_AO_CASCADES: a box of the distance-field scene's cascades'
texture (xgpu.h's XGPU_WORLD_CASCADE*: XGPU_WORLD_CASCADE_SPAN texels
square, XGPU_WORLD_CASCADE_SPAN * XGPU_WORLD_CASCADES deep, two signed bytes
a texel, RG8 snorm; xgpu_backend_world_ao_cascades): its first texel x, y,
z and its size, its bytes at data (size: width * height * depth * 2, x
fastest); width 0 releases the texture */
#define XGPU_STREAM_WORLD_AO_CASCADES_SIDE 65u
#define XGPU_STREAM_WORLD_AO_CASCADES_DEPTH (65u * 4u)
struct xgpu_stream_world_ao_cascades
{
	uint32_t header;
	uint32_t x, y, z;
	uint32_t width, height, depth;
	uint32_t data, size;
	uint32_t pad;
};

/* ---------- render targets */

struct xgpu_stream_target_create
{
	uint32_t header;
	uint32_t target;
	uint32_t width, height;
	/* (format: XGPU_FORMAT_* of a colour target: BGRA8, or RGBA16F the
	back buffer with display.hdr) */
	uint8_t depth, layers, format, pad;
};

struct xgpu_stream_target_multisample
{
	uint32_t header;
	uint32_t target;
	int32_t samples;
};

struct xgpu_stream_set_targets
{
	uint32_t header;
	uint32_t color, depth;
	/* vr.h's HALO_VR_HUD_GROUP_* or -1; the layers drawn at once (1, 2, 4,
	or -1, -2 one layer alone) from base; their multisampled attachments' samples */
	int8_t hud_group, views;
	uint8_t base, samples;
};

/* the rectangles (left, top, width, height) follow, count of them */
struct xgpu_stream_clear
{
	uint32_t header;
	uint32_t flags;
	float color[4];
	float depth;
	uint32_t stencil;
	uint32_t count;
	int32_t rectangles[][4];
};

struct xgpu_stream_clear_target
{
	uint32_t header;
	uint32_t target;
	float color[4];
};

/* synchronous: the host writes the target's (or its layer's, -1 none)
pixels as BGRA rows from its first into pixels (guest memory of
width * height * 4 bytes, as the target was made), then the reply.
display.hdr's RGBA16F back buffer is clamped to [0, 1], as an SDR display
shows it, or with tone_map, tone mapped into it (xgpu_hdr_sdr_pixel) */
struct xgpu_stream_read_target
{
	uint32_t header;
	uint32_t target;
	int32_t layer;
	uint32_t tone_map;
	uint32_t pixels, size;
	/* (guest address of a uint32_t: 1 written, 0 failed) */
	uint32_t reply;
};

/* synchronous: the screenshot key's picture. The host reads the target as
READ_TARGET does, tone mapped, into pixels (guest memory of width * height
* 4 bytes), and writes it as a PNG file in the folder the record ends with
(a UTF-8 path and its NUL, made if it is missing), named for the local date
and time; then the file's name (without its folder, NUL-terminated) into
name (XGPU_STREAM_SCREENSHOT_NAME bytes of guest memory), and the reply (a
uint32_t: 1 written, 0 failed) */
#define XGPU_STREAM_SCREENSHOT_NAME 64u
struct xgpu_stream_screenshot
{
	uint32_t header;
	uint32_t target;
	uint32_t width, height;
	uint32_t pixels, size;
	uint32_t name;
	uint32_t reply;
	char directory[];
};

/* ---------- textures */

/* xgpu_backend.h's struct xgpu_sampler, which is these fields */
struct xgpu_stream_sampler
{
	uint8_t minify, mip, magnify;
	uint8_t address[3];
	uint8_t pad[2];
	float lod_bias;
	uint32_t first_level;
	uint32_t anisotropy;
	uint32_t border_color;
};

#define XGPU_STREAM_MIP_LEVELS 16

/* a stage's xgpu_texture_source; resource_format and resource_size are
the texture header's Format and Size words as they were when the call was
made (the header is at resource). For an Xbox texture (XGPU_SOURCE_TEXTURE)
target is the channels its texels are sampled from: 0 as they are, else
0x100 and two bits for each of red, green, blue and alpha (red's lowest),
the channel of the texels it is taken from (a Custom Edition map's model
masks and meters: xbox_textures.c).

A stage may instead draw an image (XGPU_STREAM_SOURCE_IMAGE: a high-res
replacement for the Xbox texture, xgpu_images.h): resource is its number,
resource_format its pixels' kind (XGPU_IMAGE_*: RGBA, 4 bytes each),
resource_size a serial that
changes with its pixels (no two images have the same), data its first
level's pixels, width by height, rows from the first; the host makes its
levels (levels of them) from the first. */
#define XGPU_STREAM_SOURCE_IMAGE 3

struct xgpu_stream_texture_stage
{
	uint8_t source, sampler_type, sampled;
	/* the level_targets and level_written that follow it in the record
	(rendered_levels of them, at most XGPU_STREAM_MIP_LEVELS) */
	uint8_t level_count;
	uint32_t resource, palette;
	uint32_t resource_format, resource_size;
	uint32_t target, drawn;
	uint32_t data, width, height, levels, rendered_levels;
	struct xgpu_stream_sampler sampler;
};

struct xgpu_stream_texture_level
{
	uint32_t target, written;
};

/* the stages whose bits are set in changed follow, in order, each an
xgpu_stream_texture_stage and its level_count levels; the others are as
they were */
struct xgpu_stream_set_textures
{
	uint32_t header;
	uint32_t changed;
};

/* ---------- pipelines */

/* the vertex constants first .. first + count, count vec4s following */
struct xgpu_stream_constants
{
	uint32_t header;
	uint32_t first, count;
	float values[][4];
};

/* the bytes of struct xgpu_vertex_instruction (xgpu_shader.h, all
unsigned char: alike on both sides), count of them, following */
#define XGPU_STREAM_VERTEX_INSTRUCTION_SIZE 36
#define XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM 136

struct xgpu_stream_vertex_program
{
	uint32_t header;
	/* the vertex shader's number (for the logs); its microcode's hash
	(low word first), the key's vertex_program */
	uint32_t id;
	uint32_t hash[2];
	uint32_t instruction_count;
	uint32_t projects;
	uint8_t instructions[][XGPU_STREAM_VERTEX_INSTRUCTION_SIZE];
};

/* xgpu.h's struct nv2a_vertex_lighting */
struct xgpu_stream_vertex_lighting
{
	uint32_t header;
	uint32_t hash[2];
	int32_t lights;
	uint32_t normal_instruction, normal_register;
	uint32_t position_instruction, position_register;
	uint32_t world_found, world_instruction, world_register;
};

/* xgpu.h's struct nv2a_pixel_shader_key */
#define XGPU_STREAM_COMBINER_STATES 57

struct xgpu_stream_pixel_key
{
	uint32_t combiner_state[XGPU_STREAM_COMBINER_STATES];
	uint32_t texture_modes;
	uint8_t sampler_type[4];
	uint8_t alpha_kill[4];
	uint8_t color_sign[4];
	uint32_t alpha_test_function;
	uint8_t fog_enable, fog_table_mode, count_samples, coverage_alpha;
	uint8_t point_threshold, per_pixel_lighting, alpha_test_samples, multiview;
	uint8_t source_blend, dynamic_lights, vr_effect, ambient_occlusion;
	/* display.hdr's tag (xgpu_hdr.h) */
	uint16_t hdr;
};

/* xgpu_backend.h's struct xgpu_pipeline_key */
struct xgpu_stream_pipeline_key
{
	uint32_t vertex_program[2];
	uint32_t packed_attributes;
	uint32_t stencil_read_mask, stencil_write_mask;
	uint8_t immediate, lighting, views;
	uint8_t blend, blend_source, blend_destination, blend_operation;
	uint8_t color_mask;
	uint8_t depth_test, depth_function, depth_write;
	uint8_t stencil_test, stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
	uint8_t cull, front_counter_clockwise, cull_front;
	uint8_t fill, depth_bias;
	uint8_t color_format, depth_format, samples;
	struct xgpu_stream_pixel_key pixel;
};

/* xgpu_shader.h's struct xgpu_pixel_program: its parts of unsigned chars
alone (alike on both sides) as their bytes, its unsigned longs as 32 bits */
#define XGPU_STREAM_TEXTURE_STAGE_SIZE 9
#define XGPU_STREAM_COMBINER_STAGE_SIZE 44
#define XGPU_STREAM_FINAL_COMBINER_SIZE 25

struct xgpu_stream_pixel_program
{
	uint8_t stages[4][XGPU_STREAM_TEXTURE_STAGE_SIZE];
	uint8_t mux_msb;
	uint8_t final[XGPU_STREAM_FINAL_COMBINER_SIZE];
	uint8_t fog, fog_mode;
	uint32_t combiner_count;
	uint8_t combiners[8][XGPU_STREAM_COMBINER_STAGE_SIZE];
	uint32_t alpha_test;
	uint8_t alpha_test_samples;
	uint8_t count_samples, coverage_alpha, point_threshold, per_pixel_lighting, dynamic_lights, multiview;
	uint8_t source_blend;
	uint8_t ambient_occlusion;
	/* display.hdr's tag (xgpu_hdr.h) */
	uint16_t hdr;
};

/* a pipeline key the guest numbered (from 1, below XGPU_STREAM_PIPELINES),
sent once; XGPU_STREAM_PIPELINE names it from then on. With its pixel
shader decoded (nv2a_pixel_program_decode), so that the host needs no NV2A
decoding. */
struct xgpu_stream_pipeline_define
{
	uint32_t header;
	uint32_t id;
	struct xgpu_stream_pipeline_key key;
	struct xgpu_stream_pixel_program pixel_program;
};

/* a pipeline's twin in the pre-pass, made of its definition in place
(nonzero; 0 none, the draw not drawn in the pre-pass):
- of one with the world's light read from its pixel
  (XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED), its pixel key's occlusion
  XGPU_AMBIENT_OCCLUSION_WORLD_INPUTS (its light kept): it writes its
  pixel's inputs;
- of another opaque one that writes the depth where it is nearer or as
  near (an occluder: the first-person weapon, a surface lighting itself),
  _INPUTS alone, its test made strictly nearer (so that it never takes the
  pixel of a surface it draws over at its own depth): it writes none, so
  that no surface it hides has its occlusion found;
into the inputs' target (xgpu_backend.h's XGPU_FORMAT_WORLD_AO_INPUTS:
RGBA32Uint), one sample, nothing blended, every channel written, no HDR tag,
no enhanced dynamic lights, no samples counted; its depth, stencil, raster,
vertex program and alpha test as they are. The backends, and the Mac's
library's build, make it alike */
#define XGPU_STREAM_WORLD_AO_SAMPLED 0x50u
#define XGPU_STREAM_WORLD_AO_INPUTS 0x90u
#define XGPU_STREAM_WORLD_AO_OCCLUDER 0x80u
#define XGPU_STREAM_FORMAT_WORLD_AO_INPUTS 4u
/* (xgpu_backend.h's XGPU_COMPARE_LESS and _LESS_EQUAL) */
#define XGPU_STREAM_COMPARE_LESS 1u
#define XGPU_STREAM_COMPARE_LESS_EQUAL 3u
static inline int xgpu_stream_world_ao_twin(struct xgpu_stream_pipeline_define *definition)
{
	uint8_t occlusion = definition->pixel_program.ambient_occlusion;

	if (occlusion & 0x80u)
		return 0;
	if ((occlusion & XGPU_STREAM_WORLD_AO_SAMPLED) == XGPU_STREAM_WORLD_AO_SAMPLED)
	{
		occlusion = (uint8_t)((occlusion & 0x0fu) | XGPU_STREAM_WORLD_AO_INPUTS);
	}
	else
	{
		if (occlusion || !definition->key.depth_test || !definition->key.depth_write || definition->key.blend ||
			!definition->key.color_mask || definition->key.pixel.source_blend ||
			(definition->key.depth_function != XGPU_STREAM_COMPARE_LESS &&
			definition->key.depth_function != XGPU_STREAM_COMPARE_LESS_EQUAL))
			return 0;
		occlusion = (uint8_t)XGPU_STREAM_WORLD_AO_OCCLUDER;
		definition->key.depth_function = (uint8_t)XGPU_STREAM_COMPARE_LESS;
	}
	definition->key.pixel.ambient_occlusion = definition->pixel_program.ambient_occlusion = occlusion;
	definition->key.blend = definition->key.blend_source = definition->key.blend_destination = 0;
	definition->key.blend_operation = 0;
	definition->key.color_mask = 0x0f;
	definition->key.color_format = (uint8_t)XGPU_STREAM_FORMAT_WORLD_AO_INPUTS;
	definition->key.samples = 0;
	definition->key.pixel.alpha_test_samples = definition->pixel_program.alpha_test_samples = 0;
	definition->key.pixel.hdr = definition->pixel_program.hdr = 0;
	definition->key.pixel.source_blend = definition->pixel_program.source_blend = 0;
	definition->key.pixel.count_samples = definition->pixel_program.count_samples = 0;
	definition->key.pixel.dynamic_lights = definition->pixel_program.dynamic_lights = 0;
	return 1;
}


/* xgpu_backend.h's struct xgpu_dynamic_state */
struct xgpu_stream_dynamic_state
{
	int32_t viewport[4];
	int32_t scissor[4];
	float depth_range[2];
	uint32_t stencil_reference;
	float blend_color[4];
	float depth_bias[2];
};

/* reversed depth (xgpu_backend.h, "Reversed depth"): the depth test's
function that passes on reversed depth what the key's (xgpu_backend.h's
XGPU_COMPARE_*: never, less, equal, less or equal, greater, not equal,
greater or equal, always) passes on the game's */
static __inline uint8_t xgpu_reversed_depth_function(uint8_t function)
{
	static const uint8_t reversed[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

	return function < 8 ? reversed[function] : function;
}

/* a draw's constant depth bias (its dynamic state's depth_bias[1], in the
24-bit depth buffer's units; the pipeline's depth_bias set) as the shaders
add it to reversed depth: toward the camera, positive */
static __inline float xgpu_reversed_depth_bias(int biased, float offset)
{
	return biased ? -offset / 16777215.0f : 0.0f;
}

/* xgpu_backend.h's struct xgpu_draw_uniforms (floats alone: alike on both
sides) */
struct xgpu_stream_draw_uniforms
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
	float world_ao[4];
};

/* xgpu_backend_draw_state: what changed since the last follows, the
dynamic state (XGPU_STREAM_DRAW_DYNAMIC), then the uniforms' pieces that
changed (XGPU_STREAM_DRAW_UNIFORMS: the uniforms as 16-byte pieces, the
last of 4, those whose bits are set in pieces, low word first, in order);
a call that changes neither sends nothing */
enum
{
	XGPU_STREAM_DRAW_DYNAMIC = 1,
	XGPU_STREAM_DRAW_UNIFORMS = 2,
};

#define XGPU_STREAM_UNIFORM_PIECES ((sizeof(struct xgpu_stream_draw_uniforms) + 15) / 16)

struct xgpu_stream_draw_state
{
	uint32_t header;
	uint32_t parts;
	uint32_t uniforms_serial;
	uint32_t pieces[2];
};

/* ---------- vertices and draws */

/* inline: data is in the ring */
struct xgpu_stream_mirror_write
{
	uint32_t header;
	uint32_t segment, offset, size;
	uint32_t unused;
	uint32_t data;
};

/* xgpu_backend.h's struct xgpu_vertex_element */
struct xgpu_stream_vertex_element
{
	uint8_t reg, stream, format, bytes;
	uint16_t offset;
};

/* a vertex layout the guest numbered (from 1, below XGPU_STREAM_LAYOUTS):
xgpu_vertex_input's declaration, elements and packed mask, sent once */
struct xgpu_stream_layout_define
{
	uint32_t header;
	uint32_t id;
	uint32_t declaration;
	uint32_t element_count;
	uint32_t packed_mask;
	struct xgpu_stream_vertex_element elements[];
};

struct xgpu_stream_vertex_stream
{
	/* the mirror's segment and the offset in it, or -1 and the vertices
	(inline) at data, bytes of them */
	int32_t segment;
	uint32_t offset;
	uint32_t data;
	uint32_t bytes;
	uint32_t stride;
};

/* xgpu_backend_set_vertices: the layout, then an xgpu_stream_vertex_stream
for each stream present (bits of streams, in order), then the values of
the inputs that changed of those no stream feeds (bits of attributes, a
vec4 each, in order); then the streamed vertices' data, inline. A draw is
in the same submit as the VERTICES record whose data it reads (the guest
sends the record again before a draw in a later submit), so the data is
the backend's until the fence after that draw. */
struct xgpu_stream_vertices
{
	uint32_t header;
	uint32_t layout;
	uint16_t streams, attributes;
};

struct xgpu_stream_draw
{
	uint32_t header;
	uint32_t primitive;
	uint32_t count;
};

/* the indices (inline) at indices, count of them (16 bits each) */
struct xgpu_stream_draw_indices
{
	uint32_t header;
	uint32_t primitive;
	uint32_t count;
	uint32_t indices;
};

/* indices from the mirror's segment at offset, or (segment -1) inline at
indices, temporary if the front end made them for this draw alone (a
backend that keeps index arrays must not keep them) */
struct xgpu_stream_draw_indexed
{
	uint32_t header;
	uint32_t primitive;
	uint32_t count;
	uint32_t minimum;
	int32_t segment;
	uint32_t offset;
	uint32_t indices;
	uint32_t temporary;
};

/* immediate mode: count vertices of the registers written (bits of
written), a vec4 each in register order (inline at vertices: no room for
those not written); the values of the registers not written that changed
(bits of attributes, a vec4 each following); the indices (inline, 0
none) */
struct xgpu_stream_draw_immediate
{
	uint32_t header;
	uint32_t primitive;
	uint32_t count;
	uint16_t written, attributes;
	uint32_t vertices;
	uint32_t indices, index_count;
};

/* ---------- visibility tests */

struct xgpu_stream_visibility_end
{
	uint32_t header;
	uint32_t slot;
	float area;
	int32_t views;
	int32_t samples;
	/* the guest's number for the test (never 0; one more each test, modulo
	2^32): published with its count, so that the guest knows which test a
	slot's count is of (a count reaches the guest frames after its test) */
	uint32_t test;
};

/* ---------- the enhanced dynamic lights */

/* xgpu.h's struct xgpu_dynamic_lights_block (floats alone) */
#define XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM 128

struct xgpu_stream_dynamic_lights_block
{
	float grid[3][4];
	float position[XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM][4];
	float color[XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM][4];
	float cone[XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM][4];
};

/* the block's lights past count are zero; the cells (inline) at cells,
size bytes */
struct xgpu_stream_dynamic_lights_upload
{
	uint32_t header;
	uint32_t count;
	uint32_t cells, size;
	struct xgpu_stream_dynamic_lights_block block;
};

/* the frame's lights: the block (its lights past count zero), and the
cells' bytes from offset that changed since the stream's last change (size
bytes, inline at cells; none: 0), the others as they were. The offset and
size are multiples of 16, inside XGPU_STREAM_DYNAMIC_LIGHTS_CELLS_BYTES;
the stream's first change is of all of them. */
struct xgpu_stream_dynamic_lights_change
{
	uint32_t header;
	uint32_t count;
	uint32_t cells, offset, size;
	struct xgpu_stream_dynamic_lights_block block;
};

/* the world's count texels (inline) at texels, count * 16 bytes; count
at most xgpu.h's XGPU_DYNAMIC_LIGHTS_WORLD_TEXELS */
#define XGPU_STREAM_DYNAMIC_LIGHTS_WORLD_TEXELS (240u * 1024u)
/* the cells' texture's slices of 32 x 32 texels (xgpu.h): the cells and the
clusters' masks, uploaded each frame, then the world's */
#define XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES 17u
/* their bytes (xgpu.h's XGPU_DYNAMIC_LIGHTS_CELLS_BYTES) */
#define XGPU_STREAM_DYNAMIC_LIGHTS_CELLS_BYTES (32u * 32u * XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES * 16u)
#define XGPU_STREAM_DYNAMIC_LIGHTS_WORLD_SLICES 240u
struct xgpu_stream_dynamic_lights_world
{
	uint32_t header;
	uint32_t count;
	uint32_t texels;
};

/* xgpu_backend.h's struct xgpu_dynamic_lights_halos but its halos, which
follow (count * 2 vec4s) */
struct xgpu_stream_dynamic_lights_halos
{
	uint32_t header;
	uint32_t color, depth;
	int32_t layers;
	int32_t count;
	float camera_right[3], camera_up[3];
	float projection[6][4];
	float viewport_scale[4], viewport_offset[4];
	float camera[3];
	float softness;
	float screen_offset;
	float halos[][4];
};

/* ---------- the VR mode

The backend composes each frame (the eyes' images, the HUD's and its
panels', the scope's) and ends it with OpenXR, on the host's render thread:
OpenXR's calls that use the device (the session made, the swapchains'
images acquired and released, the frame begun and ended) are made there
(port/linux/host/host_vr_stream.c); the game waits for the runtime's frames and
locates the views (vr.c). What the composition reads of the game's VR mode
(vr.h's and vr_visor.h's functions the guest's backend calls) comes in the
records: VR_STATE before each VR_COMPOSE, the gaze with VR_EYES, the layers
the game would submit with VR_SUBMIT; and what vr.c sets in the backend as
it draws (the eyes' transforms and corrections, the sky's depth clamping, the
feed and the tint) as records of their own, in their order. vr.h's and
vr_visor.h's structures are of 4-byte fields, alike on both sides, and are
carried as their words, but the visor image's noise textures (pointers:
guest addresses here). */

/* xgpu_backend.h's struct xgpu_vr_info (ints alone), in and out; and
vr.foveation's inset (halo_vr_foveation_inset, 0 off) and its name (for
the log), in; and out, the visibility tests counted by the pixel shaders
from now on (xgpu_backend_capabilities' sample_counting, which the VR
mode's eye pass needs) */
/* (statistics' bits: debug.gpu_stats, and with it debug.vr_gpu_time) */
#define XGPU_STREAM_VR_STATISTICS 1
#define XGPU_STREAM_VR_GPU_TIME 2
struct xgpu_stream_vr_info
{
	int32_t eye_width, eye_height;
	int32_t hud_width, hud_height;
	int32_t scope_size;
	int32_t statistics;
	int32_t foveated;
	int32_t fovea_width, fovea_height;
	float foveation_inset;
	char foveation_name[64];
	int32_t sample_counting;
};

/* vr_host.h's struct vr_host_latch, in words */
#define XGPU_STREAM_VR_LATCH_WORDS 52

/* the eye pass begins; with its insets' middles where foveated
(halo_vr_foveation: foveation 0, none this frame), and the poses the eyes
are drawn from for the late latch (halo_vr_latch's; latched 0: none) */
struct xgpu_stream_vr_eyes
{
	uint32_t header;
	uint32_t eye_target;
	uint32_t image;
	int32_t foveation;
	float centres[2][2];
	uint32_t latched;
	uint32_t latch[XGPU_STREAM_VR_LATCH_WORDS];
};

/* xgpu_backend.h's struct xgpu_vr_frame */
struct xgpu_stream_vr_frame
{
	int32_t eyes_drawn, hud_drawn, scope_drawn, hud_panels;
	uint32_t eye_target, eye_depth, scope, hud;
	int32_t eye_depth_layers;
};

/* VR_COMPOSE */
struct xgpu_stream_vr_frame_record
{
	uint32_t header;
	struct xgpu_stream_vr_frame frame;
	uint32_t screenshot;
};

/* vr_host.h's struct vr_host_layers and struct vr_host_pose, in words */
#define XGPU_STREAM_VR_LAYERS_WORDS 116
#define XGPU_STREAM_VR_POSE_WORDS 8

/* VR_SUBMIT: the frame record (screenshot: the eyes', HUD's and scope's
images written, numbered shot); the game waited for a frame of the
runtime's for it (host_vr_frame_wait: the host begins and ends one), and the
layers it would submit (halo_vr_present's, with every image taken as
acquired: the host leaves out those it did not draw) and the head they
were located with (halo_vr_submitted's) */
struct xgpu_stream_vr_submit
{
	struct xgpu_stream_vr_frame_record record;
	uint32_t waited;
	uint32_t shot;
	uint32_t layers[XGPU_STREAM_VR_LAYERS_WORDS];
	uint32_t head[XGPU_STREAM_VR_POSE_WORDS];
};

/* vr.h's and vr_visor.h's structures, in words (the visor image's up to its
noise textures, which the 64-bit host's lie 4 bytes further, aligned) */
#define XGPU_STREAM_VR_SCOPE_WINDOW_WORDS 32
#define XGPU_STREAM_VR_COCKPIT_WINDOW_WORDS 34
#define XGPU_STREAM_VR_COCKPIT_DISPLAY_WORDS 19
#define XGPU_STREAM_VR_HUD_IMAGE_WORDS 50
#define XGPU_STREAM_VR_PANEL_WORDS 20
#define XGPU_STREAM_VR_VISOR_IMAGE_WORDS 71
#define XGPU_STREAM_VR_RIM_WORDS 39

/* VR_STATE's answered: the functions whose result says whether there is
one (XGPU_STREAM_VR_STATE_*: the scope's smart-link, its window, a cockpit's
display, the HUD's panels, the helmet's rim, the eyes' depth wanted), and
whether a frame of the runtime's is being drawn (halo_vr_frame_waited: its
images may be acquired) */
enum
{
	XGPU_STREAM_VR_STATE_SMARTLINK = 1,
	XGPU_STREAM_VR_STATE_SCOPE_WINDOW = 2,
	XGPU_STREAM_VR_STATE_COCKPIT = 4,
	XGPU_STREAM_VR_STATE_PANELS = 8,
	XGPU_STREAM_VR_STATE_RIM = 16,
	XGPU_STREAM_VR_STATE_DEPTH = 32,
	XGPU_STREAM_VR_STATE_FRAME = 64,
};

/* what the frame's composition asks of vr.c and vr_visor.c (vr.h,
vr_visor.h), as they answered when the game composed it: the scope's
(halo_vr_scope_zoom, _smartlink, _window, _readout, _animation, _shape,
_gun), a
cockpit's display (halo_vr_cockpit_window), the vignette
(halo_vr_vignette), the visor's (vr_visor_hud_image, _panels, _image,
_rim) */
struct xgpu_stream_vr_state
{
	uint32_t header;
	uint32_t answered;
	float scope_zoom, scope_zoom_height;
	float scope_reticle[2];
	uint32_t scope_window[XGPU_STREAM_VR_SCOPE_WINDOW_WORDS];
	float scope_range;
	int32_t scope_rounds_loaded, scope_rounds_carried;
	float scope_animation[3];
	float scope_shape[3];
	int32_t scope_gun;
	uint32_t cockpit_window[XGPU_STREAM_VR_COCKPIT_WINDOW_WORDS];
	uint32_t cockpit_display[XGPU_STREAM_VR_COCKPIT_DISPLAY_WORDS];
	float vignette;
	float vignette_fov[2][4];
	uint32_t hud_image[XGPU_STREAM_VR_HUD_IMAGE_WORDS];
	uint32_t panels[4][XGPU_STREAM_VR_PANEL_WORDS];
	uint32_t visor_image[XGPU_STREAM_VR_VISOR_IMAGE_WORDS];
	uint32_t visor_noise[2];
	float visor_time;
	uint32_t rim[XGPU_STREAM_VR_RIM_WORDS];
};

/* halo_vr_eye_feed's (7) and halo_vr_eye_tint's (3) values */
struct xgpu_stream_vr_values
{
	uint32_t header;
	float values[];
};

/* The late latch. The host locates the head and the controllers again as
it begins the eye pass (VR_EYES), and moves what the frame draws by how far
they have moved since the game located them: the eyes' transforms
(VR_EYE_TRANSFORMS) for each projection the game sets, by the head's move
(the world), its turn alone (the sky, drawn about the eye), or the head's
and the weapon's controller's (the first-person weapon and the hands), and
the eyes' layers' poses, and the scope's with the weapon. */
enum
{
	XGPU_STREAM_VR_LATCH_NONE = 0,
	XGPU_STREAM_VR_LATCH_WORLD,
	XGPU_STREAM_VR_LATCH_SKY,
	XGPU_STREAM_VR_LATCH_HELD,
};

/* VR_EYE_TRANSFORMS: halo_vr_set_eye_transforms' (2 row-major 4x4s, the
centre's clip position to each eye's), what they move with in the late
latch (XGPU_STREAM_VR_LATCH_*), and each eye's clip transform from its own
space (row-major: OpenXR's axes, metres) for the projection
(halo_vr_eye_latch's) */
struct xgpu_stream_vr_eye_transforms
{
	uint32_t header;
	int32_t latch;
	float transforms[2][16];
	float clip_from_eye[2][16];
};

/* VR_SCREEN_CORRECTIONS: halo_vr_set_screen_corrections' (2 vec4s), and the
point they are for (halo_vr_screen_point's: its clip position in the
centre's view, w 0 none, and the viewport's width and height), from which
the late latch makes them again */
struct xgpu_stream_vr_screen_corrections
{
	uint32_t header;
	float corrections[2][4];
	float point[4];
	float viewport[2];
};

/* ---------- display.ambient_occlusion "ray_traced" (docs/world-ao.md,
"Hardware ray queries")

The scene the occlusion's pass traces its rays through with the GPU's ray
queries, where the backend has them (xgpu_stream_capabilities' ray_queries):
the dynamic objects' occlusion is then found by the hardware's rays against
their collision meshes, the structure's collision mesh stopping them, in
place of the distance-field scene's step (the pass's history[3] 2).

WORLD_AO_MESH: a mesh (a bottom-level acceleration structure, built once):
its id (1 to XGPU_STREAM_WORLD_AO_MESHES - 1) and count triangles, nine
floats each (each corner's x, y, z in the mesh's own space, the front faces
counterclockwise seen from outside), at data (size: count * 36); id 0 lets
go of every mesh (a map's end). An id is given once a map */
#define XGPU_STREAM_WORLD_AO_MESHES 4096u
#define XGPU_STREAM_WORLD_AO_MESH_TRIANGLES (1u << 20)
struct xgpu_stream_world_ao_mesh
{
	uint32_t header;
	uint32_t id, count;
	uint32_t data, size;
	uint32_t pad[3];
};

/* WORLD_AO_INSTANCES: the frame's instances of the meshes (a top-level
acceleration structure of them, built before the next pass), count of
them at data (size: count * sizeof(struct xgpu_world_ao_instance)): each
its transform (a row-major 3x4, its mesh's space into the world), its
mesh's id, its owner (its object's datum index plus one, the instance's
custom index: a pixel of that object's own leaves it out; 0 the structure,
which stops a ray and occludes nothing: the lightmaps and the field hold
its occlusion) and its kind (XGPU_WORLD_AO_INSTANCE_*, the instance's mask:
the structure's facing either way, an object's front faces alone) */
#define XGPU_STREAM_WORLD_AO_INSTANCES_MAXIMUM 4096u
#define XGPU_WORLD_AO_INSTANCE_STRUCTURE 1u
#define XGPU_WORLD_AO_INSTANCE_OBJECT 2u
#define XGPU_WORLD_AO_INSTANCE_OWNERS (1u << 24)
struct xgpu_world_ao_instance
{
	float transform[3][4];
	uint32_t mesh, owner, kind, pad;
};
struct xgpu_stream_world_ao_instances
{
	uint32_t header;
	uint32_t count;
	uint32_t data, size;
};

/* ---------- the layouts' sizes, alike on both sides */

#define XGPU_STREAM_ASSERT_SIZE(type, size) _Static_assert(sizeof(type) == (size), #type "'s size")

XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_ring, 120);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_frame_end, 12);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_capabilities, 120);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_hdr, 24);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_tone_map, 24);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_panini, 56);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_world_ao_window, 16 + 32 + 32 + XGPU_WORLD_AO_PROXIES_MAXIMUM * 48 + 32 +
	XGPU_WORLD_AO_GRID_CELLS_MAXIMUM * 8);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_world_ao_field, 40);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_world_ao_prepass, 96 + 64 + 64 + 16 + 16);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_world_ao_cascades, 40);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_world_ao_mesh, 32);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_world_ao_instance, 64);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_world_ao_instances, 16);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_pipeline_stand_in, 12);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_set_targets, 16);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_mark_string, 16);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_mark_view, 72);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_mark_owner, 40);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_clear, 36);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_read_target, 28);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_screenshot, 32);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_sampler, 24);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_texture_stage, 72);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_pixel_key, 264);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_pipeline_key, 308);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_pixel_program, 436);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_pipeline_define, 752);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_dynamic_state, 68);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_draw_uniforms, 588);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vertex_element, 6);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_layout_define, 20);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vertex_stream, 20);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vertices, 12);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_draw_immediate, 28);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_dynamic_lights_block, 6192);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_dynamic_lights_halos, 192);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_dynamic_lights_world, 12);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_info, 108);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_eyes, 36 + 4 * 52);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_eye_transforms, 8 + 4 * 64);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_screen_corrections, 4 + 4 * 14);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_frame_record, 44);
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_submit, 44 + 8 + 4 * (116 + 8));
XGPU_STREAM_ASSERT_SIZE(struct xgpu_stream_vr_state, 4 * (2 + 4 + 32 + 3 + 3 + 3 + 1 + 34 + 19 + 1 + 8 + 50 + 80 + 71 + 3 + 39));

#endif
