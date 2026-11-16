/*
XGPU_VULKAN.H

The Vulkan backend's core (xgpu_vulkan.c): the device, its memory, the
images, samplers and pipelines the command stream's draws need, and the
commands that draw them, in the stream's order. It runs in the host, on the
render thread that plays each frame's records (one thread: none of these
functions may be called from two at once, but where a comment says so); the
command stream's table is xgpu_vulkan_stream.c's, the guest's textures
xgpu_vulkan_textures.c's, the window host_vulkan.c's.

The core:
- loads the Vulkan loader itself (libvulkan.so.1) and its functions
  through vkGetInstanceProcAddr, so that the host links with no Vulkan
  library;
- draws with dynamic rendering (Vulkan 1.3): a pass begins at the first
  draw or clear after the targets change, loading what they hold unless
  the first operation clears the whole attachment, and ends
  where something must be done outside one (a copy, an upload ordered
  between draws, a readback, the end of a submission); every image is in
  the GENERAL layout, and the end of a pass is a barrier for what comes
  after; a draw that samples the depth attachment it is tested against
  (the VR mode's effects) draws in a pass that writes and stores no depth
  or stencil, which a draw that writes them ends;
- caches pipelines by their whole description (shaders, vertex layout,
  fixed-function state, the targets' formats), made through a
  VkPipelineCache that is saved to a file (at exit, after a map's
  preparation, as an Android app goes to the background) and read at the
  next start; and samplers by theirs;
- writes what a draw reads that the game rewrites (streamed vertices and
  indices, the vertex constants, the uniforms, uploads' texels) into an
  upload ring, a host-visible buffer reused once the GPU is done with the
  submissions that read it;
- counts visibility tests' samples with occlusion queries, read once the
  frame's submissions are done;
- carries out display.hdr's half of the contract (docs/hdr.md): draws that
  blend in their shaders read the pixel they draw over as an input
  attachment where it is drawn (VK_KHR_dynamic_rendering_local_read), in
  rasterization order where the device has it
  (VK_EXT_rasterization_order_attachment_access) or after a
  framebuffer-local barrier before each such draw, their own triangles
  over the same pixel ordered by the fragment shader interlock where the
  device has it (VK_EXT_fragment_shader_interlock: the target read and
  written as a storage image); and the frame is
  presented through an HDR swapchain (VK_EXT_swapchain_colorspace: scRGB
  or HDR10's PQ) where the frames are HDR and the window's display shows
  HDR, encoded by the options' encode, with VK_EXT_hdr_metadata's metadata
  where the device has it;
- keeps depth reversed (xgpu_backend.h, "Reversed depth"), in a float
  format where the device has one (D32_SFLOAT_S8_UINT): the pipelines'
  depth functions mirrored, the clears and the depth range as 1 - them,
  the constant depth bias the shaders' and the slope's turned; in the VR
  build (HALO_VR) too, whose eyes' passes, effects, cockpit, scope window,
  rim and compositor's depth read and write it reversed
  (host_vulkan_vr.c, xgpu_spirv.h's reversed_depth).

Submissions are numbered by a timeline semaphore: xgpu_vulkan_fence ends the
work recorded so far and returns its number, xgpu_vulkan_wait waits for it.
*/

#ifndef __HALO_LINUX_XGPU_VULKAN_H
#define __HALO_LINUX_XGPU_VULKAN_H

#include <stddef.h>
#include <stdint.h>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>

/* depth kept reversed (above) */
#define XGPU_VULKAN_REVERSED_DEPTH 1

/* ---------- the core's life */

struct xgpu_hdr_frame;
struct xgpu_vulkan_image;

struct xgpu_vulkan_options
{
	/* the instance extensions the window's surface needs (SDL's), count of
	them; and the surface made for the instance (NULL: no window, the
	frames are not shown: SDL's offscreen driver, the tests). It is made
	again, on the render thread, when the surface is lost
	(VK_ERROR_SURFACE_LOST_KHR) or the app comes back from the background
	(xgpu_vulkan_window_suspend): VK_NULL_HANDLE while it cannot be (an
	Android app before its window is back), tried again at later frames */
	const char *const *instance_extensions;
	uint32_t instance_extension_count;
	VkSurfaceKHR (*surface_create)(VkInstance instance, void *context);
	void *surface_context;
	/* the swap interval at the start (1 or more waits for the display, 0
	does not: xgpu_vulkan_window_interval) */
	int interval;
	/* display.low_latency at the start: xgpu_vulkan_window_low_latency */
	int low_latency;
	/* debug.vulkan_present_mode: a VkPresentModeKHR plus one, which the
swapchain takes where the surface has it (0: the swap interval's choice) */
	int present_mode;
	/* The present step's turn for a surface the display turns (a phone held
	sideways: xgpu_vulkan_post.c's): rotation_prepare makes its pipeline at
	the start, where the surface can be turned (1 if it can); rotate draws
	an image turned by quarter turns clockwise into an image of its own.
	NULL: the swapchain is the window's way up, and the compositor turns
	it. */
	int (*rotation_prepare)(void);
	struct xgpu_vulkan_image *(*rotate)(struct xgpu_vulkan_image *image, int quarter_turns);
	/* the pipeline cache's file (NULL: none) */
	const char *cache_path;
	/* the validation layers (VK_LAYER_KHRONOS_validation) and their
	messages logged */
	int validation;
	/* debug.gpu_stats: display.low_latency's statistics logged too
	(xgpu_vulkan_pacing.c) */
	int statistics;
	/* OpenXR owns device selection and augments device creation. NULL for
	the desktop path. These callbacks run before the render thread starts. */
	VkResult (*create_instance)(PFN_vkGetInstanceProcAddr, const VkInstanceCreateInfo *,
		const VkAllocationCallbacks *, VkInstance *);
	VkPhysicalDevice (*choose_physical)(VkInstance);
	VkResult (*create_device)(PFN_vkGetInstanceProcAddr, VkPhysicalDevice, const VkDeviceCreateInfo *,
		const VkAllocationCallbacks *, VkDevice *);
	void (*device_ready)(VkInstance, VkPhysicalDevice, VkDevice, uint32_t, uint32_t);
	/* display.hdr: what the backend tells of its display (the render
	thread, as it changes): whether it shows HDR (the window's display
	does, and the surface has an HDR colour space), its headroom over
	SDR white, that white in nits and whether the system gave it
	(host_render_stream_display_hdr). And the encoding of a
	frame for an HDR swapchain (xgpu_vulkan_post.c's): encode_prepare
	makes its pipelines at the start (1 if it can), encode draws the frame
	into an image of its own of the swapchain's format
	(XGPU_VULKAN_FORMAT_*) and returns it, NULL if it cannot. NULL: no HDR
	swapchain. */
	void (*display_hdr)(int hdr, float headroom, float white_nits, int white_reported);
	int (*encode_prepare)(void);
	struct xgpu_vulkan_image *(*encode)(struct xgpu_vulkan_image *image, const struct xgpu_hdr_frame *frame,
		uint8_t format);
	/* display.gamma: its curve over an SDR frame before the swapchain
	(xgpu_vulkan_post.c's): gamma_prepare makes its pipeline at the start
	(1 if it can), gamma draws the frame curved (each channel raised to a
	power) into an image of its own and returns it, NULL if it cannot.
	NULL: no curve (the VR build's). */
	int (*gamma_prepare)(void);
	struct xgpu_vulkan_image *(*gamma)(struct xgpu_vulkan_image *image, float power);
};

/* 1 on success: a device that has what the backend needs (logged if not) */
int xgpu_vulkan_initialize(const struct xgpu_vulkan_options *options);
/* saves the pipeline cache and lets go of everything */
void xgpu_vulkan_shutdown(void);
/* After xgpu_vulkan_initialize failed: lets go of what it made, so that
it can be tried again (with other options: VR's runtime refused the
device, and the game plays flat). */
void xgpu_vulkan_initialize_undo(void);
/* the pipeline cache written to its file (atomically: a temporary file
renamed over it) if pipelines were made since it was last written: after a
map's preparation, and as the app goes to the background, since Android
seldom lets an app exit cleanly. Render thread. */
void xgpu_vulkan_pipeline_cache_save(void);

/* what the front end's descriptions can ask of the device */
struct xgpu_vulkan_capabilities
{
	uint32_t largest_target;
	/* the most samples a target may have (a power of 2) */
	uint32_t samples;
	uint32_t anisotropy;
	/* border colours of any colour (VK_EXT_custom_border_color): else the
	shader draws them (xgpu_spirv_options border) */
	int custom_border_colors;
	/* the vertex formats of 16-bit integers converted to float
	(R16*_SSCALED): else the shader converts them */
	int scaled_shorts;
	/* DXT textures uploaded as they are (BC1 to BC3): else decoded, or ASTC
	(port/linux/astc) where the device samples ASTC 4x4 */
	int block_compression, astc;
	/* pixel shaders can write storage buffers (the visibility tests' counts
	in the shader) */
	int fragment_stores;
	uint32_t multiview_views;
	int depth_clamp;
	/* Timestamp queries on the selected graphics queue. */
	int gpu_timestamps;
	/* display.hdr: a draw can read the pixel of the colour attachment it
	draws over (VK_KHR_dynamic_rendering_local_read, and sample shading for
	a multisampled target's), and the half floats are drawn into and
	blended; and those reads are in rasterization order
	(VK_EXT_rasterization_order_attachment_access), else each such draw
	is ordered after the ones before it by a barrier (two of its own
	triangles over the same pixel, unordered) */
	int color_read, raster_order;
	/* display.hdr, where the reads are not in rasterization order: the
	device's fragment shader interlock (VK_EXT_fragment_shader_interlock)
	orders a draw's own triangles over the same pixel, the pixel's
	(pixel_interlock) or each sample's (sample_interlock), each reading and
	writing the target as a storage image (xgpu_spirv_options interlock;
	the half floats as storage images, multisampled ones too) */
	int pixel_interlock, sample_interlock;
	/* pixel shaders have the subgroups' bitwise operations (Vulkan 1.1's
	subgroup arithmetic in the fragment stage): xgpu_spirv_options
	subgroups */
	int subgroups;
	/* display.ambient_occlusion "ray_traced": the device's ray queries and
	acceleration structures (VK_KHR_ray_query, VK_KHR_acceleration_structure:
	xgpu_vulkan_rt.c), unless HALO_VULKAN_NO_RAY_QUERIES */
	int ray_queries;
	/* the device, its Vulkan version and driver, in a few words (a
	report's: port/linux/src/report.c) */
	char device[96];
};
void xgpu_vulkan_capabilities(struct xgpu_vulkan_capabilities *capabilities);

/* ---------- images

The game's render targets and textures. A target can be multisampled: it
is then two images, its texture and its multisampled attachment, resolved
into the texture (xgpu_vulkan_image_resolve) before anything reads it. */

enum
{
	XGPU_VULKAN_IMAGE_2D, XGPU_VULKAN_IMAGE_3D, XGPU_VULKAN_IMAGE_CUBE, XGPU_VULKAN_IMAGE_2D_ARRAY,
};

enum
{
	XGPU_VULKAN_FORMAT_NONE,
	/* 32-bit BGRA: the targets, and the textures decoded (D3DCOLOR texels) */
	XGPU_VULKAN_FORMAT_BGRA8,
	/* depth and stencil (D24S8, else D32S8 where the device has no D24S8) */
	XGPU_VULKAN_FORMAT_D24S8,
	/* DXT1 (BC1, its 1-bit alpha), DXT2 and 3 (BC2), DXT4 and 5 (BC3) */
	XGPU_VULKAN_FORMAT_DXT1,
	XGPU_VULKAN_FORMAT_DXT3,
	XGPU_VULKAN_FORMAT_DXT5,
	/* the high-res replacements' pictures: bytes red, green, blue, alpha;
	and a byte each, white's coverage (read as 1, 1, 1, the byte) */
	XGPU_VULKAN_FORMAT_RGBA8,
	XGPU_VULKAN_FORMAT_COVERAGE,
	/* four 32-bit unsigned integers (the enhanced dynamic lights' cells) */
	XGPU_VULKAN_FORMAT_RGBA32_UINT,
	/* four half floats (display.hdr's back buffer and scRGB's swapchain,
	the VR mode's light) */
	XGPU_VULKAN_FORMAT_RGBA16F,
	/* 10-bit red, green and blue, 2-bit alpha (A2B10G10R10: HDR10's
	swapchain) */
	XGPU_VULKAN_FORMAT_RGB10A2,
	/* ASTC 4x4 (LDR): DXT textures transcoded on a device without BC
	(port/linux/astc/xgpu_astc.h) */
	XGPU_VULKAN_FORMAT_ASTC,
	/* a signed byte (display.ambient_occlusion "world"'s distance field) */
	XGPU_VULKAN_FORMAT_R8_SNORM,
	/* (display.ambient_occlusion "world" found once a pixel: the tiles'
	masks, two 32-bit unsigned integers; the occlusion and what the surfaces
	upsample it by, four floats) */
	XGPU_VULKAN_FORMAT_RG32_UINT,
	XGPU_VULKAN_FORMAT_RGBA32F,
	/* two signed bytes (its distance-field scene's cascades) */
	XGPU_VULKAN_FORMAT_RG8_SNORM,
	XGPU_VULKAN_FORMAT_COUNT
};

/* an image's usage (bits) */
enum
{
	XGPU_VULKAN_USAGE_SAMPLED = 1,
	/* drawn into */
	XGPU_VULKAN_USAGE_TARGET = 2,
	/* and read where it is drawn (an input attachment: display.hdr's back
	buffer) */
	XGPU_VULKAN_USAGE_INPUT = 4,
};

struct xgpu_vulkan_image_description
{
	/* XGPU_VULKAN_IMAGE_*, XGPU_VULKAN_FORMAT_*, XGPU_VULKAN_USAGE_*, its
	samples a pixel (0 or 1: one) */
	uint8_t type, format, usage, samples;
	uint32_t width, height;
	/* a 3D image's depth, an array's layers (a cube's are 6) */
	uint32_t depth_or_layers;
	uint32_t levels;
	/* the channels sampled: 0 the format's, else 0x100 and two bits for each
	of red, green, blue and alpha (red's lowest), the format's channel each
	is taken from (a Custom Edition map's: xgpu_stream.h) */
	uint32_t channels;
};

struct xgpu_vulkan_image;

/* NULL if it cannot be made (logged) */
struct xgpu_vulkan_image *xgpu_vulkan_image_create(const struct xgpu_vulkan_image_description *description);
/* (after the work recorded so far that uses it) */
void xgpu_vulkan_image_destroy(struct xgpu_vulkan_image *image);
const struct xgpu_vulkan_image_description *xgpu_vulkan_image_description(const struct xgpu_vulkan_image *image);
/* writes a region of a level and layer (a cube's face, a 3D image's slices
z to z + depth); bytes_per_row is a compressed format's row of blocks. In
the stream's order: draws recorded before it see the old texels, draws after
it the new ones. */
void xgpu_vulkan_image_upload(struct xgpu_vulkan_image *image, uint32_t level, uint32_t layer, uint32_t x, uint32_t y,
	uint32_t z, uint32_t width, uint32_t height, uint32_t depth, const void *data, uint32_t bytes_per_row,
	uint32_t bytes_per_image);
/* its levels past from from level from - 1, each from the one before
(linear), in the stream's order */
void xgpu_vulkan_image_make_levels(struct xgpu_vulkan_image *image, uint32_t from);
/* copies a level and layer of one image into a level and layer of another
of the same format (as much as both have), in the stream's order */
void xgpu_vulkan_image_copy(struct xgpu_vulkan_image *source, uint32_t source_level, uint32_t source_layer,
	struct xgpu_vulkan_image *destination, uint32_t destination_level, uint32_t destination_layer);
/* copies a rectangle (x0, y0, x1, y1) of a colour image's layer into the
same place of another's first layer, of the same format, in the stream's
order */
void xgpu_vulkan_image_copy_rectangle(struct xgpu_vulkan_image *source, uint32_t source_layer,
	struct xgpu_vulkan_image *destination, const int32_t rectangle[4]);
/* a multisampled image's samples resolved into an image of one (colour:
averaged; depth and stencil: the first sample), in the stream's order */
void xgpu_vulkan_image_resolve(struct xgpu_vulkan_image *multisampled, struct xgpu_vulkan_image *image);
/* reads a layer's first level as rows of 4-byte pixels (BGRA8 images:
BGRA; display.hdr's RGBA16F ones as BGRA too, clamped to [0, 1] as an SDR
display shows them, or with tone_map tone mapped into it: xgpu_hdr.h's
xgpu_hdr_sdr_pixel) from the first, after the work recorded so far: waits
for the GPU (screenshots, tests); 1 on success */
int xgpu_vulkan_read_pixels(struct xgpu_vulkan_image *image, uint32_t layer, void *pixels, uint32_t bytes_per_row,
	int tone_map);

/* ---------- samplers */

enum
{
	XGPU_VULKAN_FILTER_POINT, XGPU_VULKAN_FILTER_LINEAR,
};

/* the mip filter: none (the first level alone), nearest level, between
levels */
enum
{
	XGPU_VULKAN_MIP_NONE, XGPU_VULKAN_MIP_POINT, XGPU_VULKAN_MIP_LINEAR,
};

enum
{
	XGPU_VULKAN_ADDRESS_WRAP, XGPU_VULKAN_ADDRESS_MIRROR, XGPU_VULKAN_ADDRESS_CLAMP, XGPU_VULKAN_ADDRESS_BORDER,
	/* XGPU_ADDRESS_REGION: the shader's (xgpu_vulkan_stream.c's
	border_axes), clamped to the edge here */
	XGPU_VULKAN_ADDRESS_REGION = 5,
	/* XGPU_ADDRESS_MSDF (on w): the shader's (xgpu_vulkan_stream.c's
	border_axes), repeated here */
	XGPU_VULKAN_ADDRESS_MSDF = 6,
};

/* zeroed before it is filled (it is compared whole) */
struct xgpu_vulkan_sampler_description
{
	uint8_t minify, magnify, mip;
	uint8_t address[3];
	/* 1 for none */
	uint8_t anisotropy;
	uint8_t pad;
	float lod_bias;
	/* the most detailed level sampled */
	float min_lod;
	/* ARGB, 8 bits each (address border) */
	uint32_t border_color;
};

/* the sampler of a description, made the first time; VK_NULL_HANDLE if it
cannot be */
VkSampler xgpu_vulkan_sampler(const struct xgpu_vulkan_sampler_description *description);

/* ---------- shaders and pipelines */

/* a shader module of SPIR-V words (count of them): the same for the same
words; VK_NULL_HANDLE if it cannot be made (logged) */
VkShaderModule xgpu_vulkan_shader(const uint32_t *words, size_t count);
/* the same, for words that never change while the game runs (the fixed
programs built into it, xgpu_vulkan_vr_shaders.c): found again by their
address, without reading the words again, as a composition draw each frame
asks for them */
VkShaderModule xgpu_vulkan_shader_constant(const uint32_t *words, size_t count);

/* the vertex inputs' bindings: the 16 streams', and the one of the inputs
no stream feeds (a vec4 each, every vertex the same) */
#define XGPU_VULKAN_BINDING_CONSTANT 16
#define XGPU_VULKAN_BINDINGS 17

struct xgpu_vulkan_attribute
{
	/* VkFormat (VK_FORMAT_UNDEFINED: not read), its binding and offset */
	uint32_t format;
	uint8_t binding, pad;
	uint16_t offset;
};

/* the primitives' kinds the pipeline draws (its topology's class: the
topology itself is the draw's) */
enum
{
	XGPU_VULKAN_CLASS_POINTS, XGPU_VULKAN_CLASS_LINES, XGPU_VULKAN_CLASS_TRIANGLES,
};

/* What a pipeline bakes in: the shaders, the vertex layout and the
pipeline key's fixed-function fields (xgpu_backend.h's XGPU_* values,
named as struct xgpu_pipeline_key names them), and the targets' formats
(XGPU_VULKAN_FORMAT_*) and samples. Zeroed before it is filled (it is
hashed and compared whole). */
struct xgpu_vulkan_pipeline_description
{
	VkShaderModule vertex_shader, pixel_shader;
	struct xgpu_vulkan_attribute attributes[16];
	uint16_t strides[XGPU_VULKAN_BINDINGS];
	uint8_t primitive_class;
	uint8_t blend, blend_source, blend_destination, blend_operation;
	uint8_t color_mask;
	uint8_t depth_test, depth_function, depth_write;
	uint8_t stencil_test, stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
	uint8_t stencil_read_mask, stencil_write_mask;
	/* raster: the Xbox screen's front winding (rows from the top, as
	Vulkan's framebuffers have them) */
	uint8_t cull, front_counter_clockwise, cull_front;
	uint8_t fill, depth_bias;
	uint8_t color_format, depth_format, samples;
	/* the pixel shader writes the sample mask, or counts samples (early
	fragment tests): nothing to the pipeline but its key */
	uint8_t view_mask, depth_clamp;
	/* the pixel shader reads the pixel it draws over and blends with it
	itself (display.hdr: capabilities color_read): the blend's fields are
	not the pipeline's */
	uint8_t color_read;
	/* and, interlocked (capabilities pixel_interlock), reads and writes it
	as a storage image: the pipeline writes no colour */
	uint8_t color_store;
};

/* the pipeline of a description, made the first time; VK_NULL_HANDLE if it
cannot be */
VkPipeline xgpu_vulkan_pipeline(const struct xgpu_vulkan_pipeline_description *description);
/* the pipeline where it is made; else none (VK_NULL_HANDLE), and it is made
apart from the draws, on the compilers, never waited for (display.ambient_
occlusion "world"'s, while their stand-ins draw) */
VkPipeline xgpu_vulkan_pipeline_ready(const struct xgpu_vulkan_pipeline_description *description);
/* Queue preparation without requiring the handle yet. Nonzero means the
request was accepted or already ready; PREPARED drains every accepted job. */
int xgpu_vulkan_pipeline_prepare(const struct xgpu_vulkan_pipeline_description *description);
/* cumulative distinct native pipeline requests, including failed ones */
uint64_t xgpu_vulkan_pipeline_count(void);
/* Render-owner callback while waiting for preparation queue space or results.
It must not request another pipeline or access the pipeline cache. */
void xgpu_vulkan_preparation_service(void (*service)(void));

/* ---------- data the GPU reads */

/* a buffer range */
struct xgpu_vulkan_range
{
	VkBuffer buffer;
	VkDeviceSize offset;
};

/* size bytes of the upload ring for the work recorded from now on, aligned
to alignment (a power of 2): where to write them, and their range; NULL if
the ring cannot have them */
void *xgpu_vulkan_upload(uint32_t size, uint32_t alignment, struct xgpu_vulkan_range *range);

/* a buffer of the backend's that draws read (the mirror's segments): NULL
if it cannot be made */
struct xgpu_vulkan_buffer;
struct xgpu_vulkan_buffer *xgpu_vulkan_buffer_create(uint32_t size);
/* its handle, for a draw recorded now (which then reads it) */
VkBuffer xgpu_vulkan_buffer_handle(struct xgpu_vulkan_buffer *buffer);
/* its bytes, as the CPU last wrote them */
const void *xgpu_vulkan_buffer_contents(const struct xgpu_vulkan_buffer *buffer);
/* writes into it: in the stream's order (draws recorded before it read the
old bytes), or, unused (no work the GPU still has reads those bytes), at
once */
void xgpu_vulkan_buffer_write(struct xgpu_vulkan_buffer *buffer, uint32_t offset, const void *data, uint32_t size,
	int unused);

/* ---------- drawing */

/* draws go to a colour image and a depth image (NULL for none) from now
on; their layer (a layered target's) */
void xgpu_vulkan_set_targets(struct xgpu_vulkan_image *color, struct xgpu_vulkan_image *depth, uint32_t layer);
/* views is 1, 2 or 4, relative to the image view's first layer. */
void xgpu_vulkan_set_target_views(struct xgpu_vulkan_image *color, struct xgpu_vulkan_image *depth,
	uint32_t layer, uint32_t views);
/* Copy to a runtime image of the matching channel order or exact depth
format. It enters and leaves in COLOR_ATTACHMENT_OPTIMAL (colour) or
DEPTH_STENCIL_ATTACHMENT_OPTIMAL (depth); OpenXR owns its lifetime. */
void xgpu_vulkan_image_to_runtime(struct xgpu_vulkan_image *source, VkImage destination,
	uint32_t layers);

/* the clears' parts: the interface's XGPU_CLEAR_* (the colour's channels
are cleared where their bits are set) */
enum
{
	XGPU_VULKAN_CLEAR_RED = 1, XGPU_VULKAN_CLEAR_GREEN = 2, XGPU_VULKAN_CLEAR_BLUE = 4, XGPU_VULKAN_CLEAR_ALPHA = 8,
	XGPU_VULKAN_CLEAR_COLOR = 16, XGPU_VULKAN_CLEAR_DEPTH = 32, XGPU_VULKAN_CLEAR_STENCIL = 64,
	XGPU_VULKAN_CLEAR_CHANNELS = 15,
};

/* rectangles (left, top, width, height in the targets' pixels; none:
nothing, as OpenGL's) of the parts XGPU_VULKAN_CLEAR_* of the bound
targets. Every channel of the colour, depth and stencil are cleared (a
whole attachment's as its pass begins); some of the colour's channels alone
are a triangle over each rectangle, drawn through their write mask, as
OpenGL's glClear writes through glColorMask. */
void xgpu_vulkan_clear(const int32_t (*rectangles)[4], uint32_t count, uint32_t parts, const float color[4], float depth,
	uint32_t stencil);
/* the pipeline xgpu_vulkan_clear draws a clear of parts with (some of the
colour's channels) into the bound targets, queued for the pipelines'
compiler (a map's preparation); nonzero if it was accepted, or none is
needed */
int xgpu_vulkan_clear_prepare(uint32_t parts);

/* the dynamic state (struct xgpu_dynamic_state), in the targets' pixels */
struct xgpu_vulkan_dynamic_state
{
	int32_t viewport[4];
	int32_t scissor[4];
	float depth_range[2];
	uint32_t stencil_reference;
	float blend_color[4];
	/* the slope factor and the offset in the depth's units (the offset
	the shaders' where depth is reversed: xgpu_reversed_depth_bias) */
	float depth_bias[2];
};

/* a stage's texture: its image (NULL: none, a stand-in of its kind is
sampled) and how it is sampled (XGPU_VULKAN_IMAGE_*, the shader's), and its
sampler */
struct xgpu_vulkan_texture
{
	struct xgpu_vulkan_image *image;
	uint8_t kind;
	VkSampler sampler;
};

enum
{
	XGPU_VULKAN_TOPOLOGY_POINTS, XGPU_VULKAN_TOPOLOGY_LINES, XGPU_VULKAN_TOPOLOGY_LINE_STRIP,
	XGPU_VULKAN_TOPOLOGY_TRIANGLES, XGPU_VULKAN_TOPOLOGY_TRIANGLE_STRIP, XGPU_VULKAN_TOPOLOGY_TRIANGLE_FAN,
};

/* a draw: its pipeline, topology and dynamic state; its vertices' bindings
(those of bindings' bits) and indices (16-bit; buffer VK_NULL_HANDLE: none);
the vertex constants and uniforms (ranges of the upload ring, or another
buffer's, at most 64 KB each); the stages' textures; and the count of
vertices or indices, the first index and the offset added to each index.
dynamic_lights: its pixel shader reads the enhanced dynamic lights' block
and cells (xgpu_vulkan_dynamic_lights_upload); reads_depth: it samples
the bound depth attachment (the VR mode's effects), and is drawn in a pass
that writes neither depth nor stencil; writes_depth: its pipeline writes
depth or stencil, and is not drawn in such a pass */
struct xgpu_vulkan_draw
{
	VkPipeline pipeline;
	uint8_t topology, dynamic_lights, reads_depth, writes_depth;
	/* its pixel shader reads display.ambient_occlusion "world"'s field and
	block (XGPU_VULKAN_SET_OCCLUSION) */
	uint8_t ambient_occlusion;
	/* its pipeline reads the colour attachment (color_read): the
	attachment bound as its input, the draw ordered after the pass's
	draws before it */
	uint8_t reads_color;
	/* its pipeline writes it as a storage image (color_store): the draw
	ordered after the pass's draws before it and before those after it */
	uint8_t stores_color;
	/* HALO_VULKAN_DRAW_TIMES (measuring): the game's draw's kind, its
	pixel key's ambient_occlusion, its pipeline key's lighting and its
	per_pixel_lighting, and XGPU_VULKAN_DRAW_GAME; zeros the backend's own */
	uint8_t kind[4];
	const struct xgpu_vulkan_dynamic_state *dynamic;
	uint32_t bindings;
	struct xgpu_vulkan_range vertices[XGPU_VULKAN_BINDINGS];
	struct xgpu_vulkan_range indices;
	struct xgpu_vulkan_range constants, uniforms;
	struct xgpu_vulkan_texture textures[4];
	uint32_t count, first_index;
	int32_t vertex_offset;
};

#define XGPU_VULKAN_DRAW_GAME 0x80
void xgpu_vulkan_draw(const struct xgpu_vulkan_draw *draw);

/* display.ambient_occlusion "world" (xgpu_vulkan_world_ao.c, xgpu_stream.h's
struct xgpu_world_ao_block), which the draws lighting by it read
(xgpu_vulkan_draw's ambient_occlusion) in the pipeline layout's set
XGPU_VULKAN_SET_OCCLUSION (xgpu_spirv.h's XGPU_SPIRV_SET_OCCLUSION; its
binding 0 unused): the structure's distance
field, its atlas of bricks (a 3D image of signed bytes, linear, clamped to
its edge) and its table of cells (a 3D RGBA8 image, read by texel), each a
1x1x1 far-outside stand-in where the field is not whole, and the window's
block (a uniform buffer) */
/* display.hdr's input set's binding of the colour target as a storage
image, for the interlocked draws (xgpu_spirv.h's XGPU_SPIRV_BINDING_STORE;
the input attachment is binding 0) */
#define XGPU_VULKAN_BINDING_STORE 1

#define XGPU_VULKAN_SET_OCCLUSION 3
#define XGPU_VULKAN_BINDING_WORLD_SDF 1
#define XGPU_VULKAN_BINDING_WORLD_AO 2
#define XGPU_VULKAN_BINDING_WORLD_SDF_TABLE 3
/* (and the static scenery's pages of the level's occlusion: a 2D array of
bytes, linear, clamped, a 1x1 page of none where they have not all come) */
#define XGPU_VULKAN_BINDING_WORLD_AO_SCENERY 4
/* (and the vehicles' occlusion fields' atlas: a 3D image of four halves,
linear, clamped, a texel of none where it has not all come) */
#define XGPU_VULKAN_BINDING_WORLD_AO_OBJECTS 5
/* (and the occlusion found once a pixel, which the draws that read it
read: a 2D image of four floats, read by texel, a 1x1 of 1 where none is
found) */
#define XGPU_VULKAN_BINDING_WORLD_AO_PIXELS 6
/* (and, where the device has ray queries, the ray-traced scene's top-level
acceleration structure: xgpu_vulkan_rt_scene) */
#define XGPU_VULKAN_BINDING_WORLD_AO_SCENE 8
struct xgpu_world_ao_block;
/* the stand-ins and the sampler made (the core's start): 0 if they cannot be */
int xgpu_vulkan_world_ao_create(void);
/* slices z_first to z_first + z_count - 1 of a width x height x depth part
of the field (XGPU_STREAM_WORLD_AO_TABLE or _ATLAS), in the stream's order
(width 0: both let go of), and the window's block from now on (NULL: none,
zeros) */
void xgpu_vulkan_world_ao_field(uint32_t part, uint32_t width, uint32_t height, uint32_t depth, uint32_t z_first,
	uint32_t z_count, const void *data);
void xgpu_vulkan_world_ao_window(const struct xgpu_world_ao_block *block);
/* a box of the distance-field scene's cascades (xgpu_stream.h's
WORLD_AO_CASCADES; width 0 released), and their image (a stand-in of
nothing where there is none) */
void xgpu_vulkan_world_ao_cascades(uint32_t x, uint32_t y, uint32_t z, uint32_t width, uint32_t height, uint32_t depth,
	const void *data);
struct xgpu_vulkan_image *xgpu_vulkan_world_ao_cascades_image(void);
/* (the core's occlusion set) the field's atlas and table or the stand-ins,
the sampler, the block's range in the ring for the submission of that
serial (0: none), and a number that changes whenever any of them does */
struct xgpu_vulkan_image *xgpu_vulkan_world_ao_field_image(void);
struct xgpu_vulkan_image *xgpu_vulkan_world_ao_table_image(void);
struct xgpu_vulkan_image *xgpu_vulkan_world_ao_scenery_image(void);
struct xgpu_vulkan_image *xgpu_vulkan_world_ao_objects_image(void);
VkSampler xgpu_vulkan_world_ao_sampler(void);
/* the occlusion found once a pixel the draws read from now on (NULL: its
stand-in, 1), and what they read */
void xgpu_vulkan_world_ao_set_pixels(struct xgpu_vulkan_image *image);
struct xgpu_vulkan_image *xgpu_vulkan_world_ao_pixels_image(void);
int xgpu_vulkan_world_ao_block(uint64_t serial, VkDescriptorBufferInfo *info);
uint64_t xgpu_vulkan_world_ao_number(void);
/* (the number changed: what the set names did, xgpu_vulkan_rt.c's scene) */
void xgpu_vulkan_world_ao_touch(void);

/* display.ambient_occlusion "ray_traced" (xgpu_vulkan_rt.c; xgpu_stream.h's
WORLD_AO_MESH, _INSTANCES), where the device has ray queries
(capabilities' ray_queries; else these do nothing): a mesh (id 0: every
mesh let go of) and the frame's instances, in the stream's order; the
frame's scene built for the pass about to read it (its meshes not yet
built, then its instances', in the early commands: 1 if there is one to
trace, 0 if not, the pass then the distance-field scene's); and the scene
the occlusion set names (the latest built, else an empty one; VK_NULL_HANDLE
without ray queries), read by the work recorded now */
struct xgpu_world_ao_instance;
void xgpu_vulkan_rt_mesh(uint32_t id, uint32_t count, const float *triangles);
void xgpu_vulkan_rt_instances(const struct xgpu_world_ao_instance *instances, uint32_t count);
int xgpu_vulkan_rt_build(void);
VkAccelerationStructureKHR xgpu_vulkan_rt_scene(void);
int xgpu_vulkan_rt_ready(void);
/* (the core's: at the device's making and end) */
struct xgpu_vulkan_rt_device
{
	VkDevice device;
	PFN_vkGetDeviceProcAddr get_device_proc_addr;
	VkPhysicalDeviceMemoryProperties memory;
	uint32_t scratch_alignment;
};
int xgpu_vulkan_rt_create(const struct xgpu_vulkan_rt_device *device);
void xgpu_vulkan_rt_destroy(void);
/* (the core's, for xgpu_vulkan_rt.c) the early commands, begun: before the
draws of the submission being recorded, which are ordered after them; and
the latest submission the GPU is done with */
VkCommandBuffer xgpu_vulkan_early_commands(void);
uint64_t xgpu_vulkan_completed(void);

/* the enhanced dynamic lights' block (struct xgpu_dynamic_lights_block,
size bytes) and their grid's cells (32 x 32 x 16, four uints each: xgpu.h)
with the clusters' masks past them (a slice more), which the draw set holds
(xgpu_spirv.h), in the stream's order: draws recorded before it read the old
ones, draws after it these. cells is all of them, of which only the bytes
from first to end changed since the last upload (multiples of 16; first ==
end: none), which alone are uploaded (all of them the first time) */
void xgpu_vulkan_dynamic_lights_upload(const void *block, uint32_t size, const void *cells, uint32_t first,
	uint32_t end);
/* the world (xgpu.h): count texels, into the cells' texture past the
clusters' masks, in the stream's order as the cells are */
void xgpu_vulkan_dynamic_lights_world(const void *texels, uint32_t count);

/* ---------- visibility tests */

/* the draws between begin and end count their samples (each sample that
passes the tests) for the test's slot (0 to 4095), which end names; the
count is known once the frame's work is done */
void xgpu_vulkan_visibility_begin(void);
/* VR counts only peripheral views in its pixel shaders. Slots are local
 to a frame until its submission completes; no draw waits for a readback. */
void xgpu_vulkan_visibility_count_samples(int enabled);
uint32_t xgpu_vulkan_visibility_counter(void);
VkFormat xgpu_vulkan_depth_format(void);
/* area: the samples a pixel of the game's is, which the count written for
the game is divided by; number: the game's number for the test, written
with its count */
void xgpu_vulkan_visibility_end(uint32_t slot, float area, uint32_t number);
/* where each slot's latest count is written, divided by its area (a count
of the game's pixels, as the game reads it; any thread), then, after
XGPU_VULKAN_VISIBILITY_SLOTS counts, the number of the test each count is
of, written after the count (a release store: xgpu_stream.h) */
void xgpu_vulkan_set_visibility_output(volatile uint32_t *counts);
#define XGPU_VULKAN_VISIBILITY_SLOTS 4096
/* the largest count written (the game halts on a negative one:
xgpu_backend.h, XGPU_VISIBILITY_COUNT_MOST) */
#define XGPU_VULKAN_VISIBILITY_MOST 0x7fffffffU

/* ---------- submissions and frames */

/* the number of the submission the work recorded from now on goes in:
what was uploaded for an earlier one is not read by this one (the ring's
bytes are reused once that one is done) */
uint64_t xgpu_vulkan_serial(void);

/* ends the work recorded so far and submits it; returns its number, which
grows */
/* A frame's elapsed render GPU timeline, including uploads/composition.
Begin arms the first GPU command, excluding the runtime's frame wait.
Read returns one completed interval in milliseconds, without waiting; an
unsupported queue or a full query ring omits the sample. Begin/end must
run on the renderer's thread, with end before the frame's fence. */
void xgpu_vulkan_gpu_time_begin(void);
void xgpu_vulkan_gpu_time_end(void);
int xgpu_vulkan_gpu_time_read(double *milliseconds);
/* the frames' GPU time so far (nanoseconds, a running total), and whether
it is measured (the frames timed: not the VR mode's, which reads them) */
uint64_t xgpu_vulkan_gpu_nanoseconds(int *measured);

uint64_t xgpu_vulkan_fence(void);
/* returns once the GPU has done the submission of that number and those
before (the visibility tests' counts of the frames they end are written) */
void xgpu_vulkan_wait(uint64_t fence);

/* the window's size in pixels changed (any thread): the swapchain is made
again */
void xgpu_vulkan_window_resized(uint32_t width, uint32_t height);
/* the swap interval (display.vsync; any thread): 1 or more waits for the
display (FIFO), 0 does not (MAILBOX where the surface has it, else
IMMEDIATE); the swapchain is made again when that changes. More than 1
(battery saver's, at most 8) shows each frame for that many refreshes: it
is presented that many times (xgpu_vulkan_frame_end) */
void xgpu_vulkan_window_interval(int interval);
/* display.low_latency (any thread): 0 auto, 1 on, 2 off. On, with vsync
(the swap interval 1 or more, FIFO) and a device that says when each
present reaches the screen, the game starts each frame just in time for
the refresh it is shown at, with at most one frame waiting for the display
(xgpu_vulkan_pacing.c); auto is on where that was measured (the Linux
desktop's and the Steam Deck's drivers); from the next frame */
void xgpu_vulkan_window_low_latency(int mode);
/* The app goes to the background (1) or comes back (0), on the render
thread. Going: the GPU waited for, the swapchain and the surface let go of
(Android takes the window away), the pipeline cache saved; frames are then
drawn but not shown, never waited for. Coming back: the surface made again,
now or, while the window is not back yet, at a later frame. */
void xgpu_vulkan_window_suspend(int suspended);
/* the window hidden (1: nothing is presented to it, which could wait for a
display that never takes a frame: VR's window, until the game plays flat
in it) or shown (0), any thread */
void xgpu_vulkan_window_hidden(int hidden);

/* display.hdr. What the window's display says (any thread, as it
changes: SDL's): whether HDR is on, its SDR white in scRGB's units (1.0:
80 nits; 1.0 also where the system does not say) and its headroom over SDR
white; the swapchain is made again where its output changes. And the front
end's state (the stream's record, render thread): whether the frames are
HDR (a half-float back buffer), the mode, paper white (in nits; 0: the
system's SDR white, in percent of it: xgpu_hdr_paper_white_nits), the
UI's white (in nits; 0: paper white's). */
void xgpu_vulkan_window_hdr(int enabled, float sdr_white, float headroom);
void xgpu_vulkan_hdr_state(int active, int mode, int paper_white_nits, int paper_white, int ui_nits);
/* display.picture_position: a picture of another shape than the window
this many pixels from its top (no lower than its bottom) or centered (-1)
(present), from the next present on */
void xgpu_vulkan_picture_top(int top);
/* display.gamma (the stream's record, render thread): the setting, 1 for
none, from the next present on; an SDR frame is curved by one over it
(present), an HDR swapchain's never */
void xgpu_vulkan_gamma(float gamma);
/* the tone map's and the encoding's parameters now (xgpu_hdr.h): the
swapchain's output, paper white in its units, the peak in paper whites, the
UI's white over paper white (1 and SDR where the swapchain is SDR) */
void xgpu_vulkan_hdr_frame(struct xgpu_hdr_frame *frame);

/* the frame's end: its last work submitted, and (with a window) an image
shown, letterboxed into the window (NULL: none) */
/* A held guest frame still retires its GPU work, but must not replace the
independent loading image with a blank swapchain presentation. */
void xgpu_vulkan_frame_end(struct xgpu_vulkan_image *present, int present_window);

/* Loading uses its own image and never advances the game's visibility
frame. The returned image stays owned by the backend until active becomes
zero; dimensions/format match the window panel or runtime HUD image. */
struct loading_presentation_frame;
struct xgpu_vulkan_image *xgpu_vulkan_loading_draw(const struct loading_presentation_frame *frame,
	uint32_t width, uint32_t height, uint32_t format);
void xgpu_vulkan_loading_present(struct xgpu_vulkan_image *image);
int xgpu_vulkan_loading_resources(int window);

/* statistics since the last call */
struct xgpu_vulkan_statistics
{
	/* (clears_as_draws: of some of the colour's channels, drawn; their
	triangles are among the draws) */
	uint32_t frames, submissions, passes, draws, clears, clears_as_draws;
	uint32_t pipelines_made, samplers_made, uploads_ordered, uploads_early;
	/* load actions for colour, depth and stencil (each aspect counted) */
	uint32_t attachment_loads, attachment_clears;
	/* draws' texture sets written, and found already written in their
	submission (the last draw's, which most draws reuse, not counted) */
	uint32_t texture_sets_written, texture_sets_reused;
	/* display.hdr's draws that read their colour attachment after a
	barrier (none where the device orders the reads itself) */
	uint32_t local_reads;
	/* (of them, the interlocked draws': each one's writes seen after it
	through a second barrier) */
	uint32_t color_stores;
	/* HALO_VULKAN_DISCARDS (xgpu_vulkan.c): aspects of attachments not
	stored (those at frames' ends among them), multisampled images
	resolved in the pass that drew them, and resolves of samples resolved
	already (not made again) */
	uint32_t stores_discarded, frame_end_discards, resolves_in_pass, resolves_skipped;
	/* aside passes (xgpu_vulkan.c): passes that continued the one before
	them in one rendering, passes put aside before it, and buffers renamed
	rather than written in order */
	uint32_t passes_continued, passes_aside, buffers_renamed;
	/* debug.gpu_stats (not the VR mode's): the frames' GPU time (from each
	frame's first work to its last, waits between its submissions
	included) and the frames it is of; the time between frames' ends and
	the frames it is of */
	double gpu_milliseconds, frame_seconds;
	uint32_t gpu_frames, frames_timed;
	uint64_t ring_bytes;
	double pipeline_seconds;
	uint64_t waits;
	double wait_seconds;
};
void xgpu_vulkan_statistics(struct xgpu_vulkan_statistics *statistics);

/* The replay's ID buffer (port/tools/replay/host_render_replay.h; render
thread): begin, for a frame, each pixel's stack of depth draws; next, the
draws handed to the backend from now on (to the next call; UINT32_MAX:
none) are the frame's number'th, and they are looked at where they draw
into image or multisampled (the target the frame presents); read, at the
present, the stacks of image's pixels, rows from the top, depth numbers a
pixel, topmost first, each a draw's number plus one (0 none), malloc'd;
end, the frame's done with. */
int xgpu_vulkan_inspect_begin(uint32_t depth);
void xgpu_vulkan_inspect_next(uint32_t number, struct xgpu_vulkan_image *image, struct xgpu_vulkan_image *multisampled);
uint32_t *xgpu_vulkan_inspect_read(struct xgpu_vulkan_image *image, uint32_t *width, uint32_t *height);
void xgpu_vulkan_inspect_end(void);
void xgpu_vulkan_statistics_peek(struct xgpu_vulkan_statistics *statistics);

/* A frame's GPU time for a caller (the replay's cost: port/tools/replay/
host_render_replay.h), from timestamps at the bottom of the pipeline about
each draw and each pass (xgpu_vulkan.c, "a frame's GPU time").
xgpu_vulkan_timing_begin: (the render thread, before the frame's first
work) the frame is timed; 0 if the device has no timestamps.
xgpu_vulkan_timing_draw: (the render thread) the draws recorded from now
are the caller's index'th (UINT32_MAX: none). xgpu_vulkan_timing_wait:
(the frame ended, the render thread idle) waits up to seconds for its
GPU work, then gives its time (its passes', which sum to it), its passes
(each from the timestamp before it, ms from the frame's first; kind its
label), each draw's own time and its pass (UINT32_MAX: none), by the
caller's numbers; they are the backend's until the next begin. 0 if it
was not timed. */
struct xgpu_vulkan_pass_time
{
	const char *kind;
	uint32_t width, height, draws;
	double start_ms, end_ms, ms;
};
struct xgpu_vulkan_timing
{
	double frame_ms;
	const struct xgpu_vulkan_pass_time *passes;
	uint32_t pass_count;
	const uint32_t *draw_pass;
	const double *draw_ms;
	uint32_t draw_count;
	const char *note;
};
int xgpu_vulkan_timing_begin(void);
void xgpu_vulkan_timing_draw(uint32_t index);
int xgpu_vulkan_timing_wait(double seconds, struct xgpu_vulkan_timing *result);

/* The driver's calls a thread makes, counted and timed for a caller (the
replay's cost: its CPU time, port/tools/replay/host_render_replay.h), by
kind. xgpu_vulkan_calls_watch: (the thread watched, the render thread)
from now each Vulkan call of the backend's that it makes adds its time (in
clock's ticks, the clock read before and after it) and one to its kind's
entry of ticks and counts, and to their last (XGPU_VULKAN_CALL_KINDS: the
total); NULL: the watch ends (the calls go straight to the driver again). */
enum
{
	XGPU_VULKAN_CALL_BIND_PIPELINE,
	XGPU_VULKAN_CALL_BIND_DESCRIPTORS,
	XGPU_VULKAN_CALL_UPDATE_DESCRIPTORS,
	/* (and the pools' resets) */
	XGPU_VULKAN_CALL_ALLOCATE_DESCRIPTORS,
	XGPU_VULKAN_CALL_PUSH_CONSTANTS,
	/* (the vertex and index buffers) */
	XGPU_VULKAN_CALL_BIND_BUFFERS,
	/* (viewport, scissor, depth bias, blend constants, stencil reference,
	topology) */
	XGPU_VULKAN_CALL_DYNAMIC_STATE,
	/* (and the dispatches) */
	XGPU_VULKAN_CALL_DRAW,
	XGPU_VULKAN_CALL_CLEAR,
	/* (a command buffer begun, ended: the secondary command buffers'
	and the submissions') */
	XGPU_VULKAN_CALL_BEGIN_COMMANDS,
	XGPU_VULKAN_CALL_END_COMMANDS,
	XGPU_VULKAN_CALL_EXECUTE_COMMANDS,
	/* (vkCmdBeginRendering, vkCmdEndRendering) */
	XGPU_VULKAN_CALL_RENDERING,
	XGPU_VULKAN_CALL_BARRIER,
	/* (copies, blits, resolves) */
	XGPU_VULKAN_CALL_COPY,
	/* (queries and timestamps) */
	XGPU_VULKAN_CALL_QUERY,
	XGPU_VULKAN_CALL_SUBMIT,
	/* (the semaphores waited for and read, the device idle) */
	XGPU_VULKAN_CALL_WAIT,
	/* (samplers, views, images, buffers, memory, command buffers made) */
	XGPU_VULKAN_CALL_CREATE,
	XGPU_VULKAN_CALL_KINDS
};
extern const char *const xgpu_vulkan_call_kind_names[XGPU_VULKAN_CALL_KINDS];
void xgpu_vulkan_calls_watch(uint64_t *ticks, uint64_t *counts, uint64_t (*clock)(void));

#endif
