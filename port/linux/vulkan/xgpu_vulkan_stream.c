/*
XGPU_VULKAN_STREAM.C

The Vulkan backend as the command stream's backend table (struct
host_render_backend, port/android/host/host_render_stream.h): each record's
entry, in terms of xgpu_vulkan.h's functions, called by the render thread
in the records' order. As the Metal backend's (port/macos/xgpu_metal_stream.m),
with OpenCE's OpenGL backend's ways where the two differ: the backend these
were made to draw as.

What the records name, this file keeps: the game's render targets (by its
numbers) as images, a multisampled one's attachment beside its texture,
resolved into it (before anything reads the texture); the
mirror's segments as buffers; the vertex programs, the pipelines'
definitions and the shaders made for each (SPIR-V, port/linux/src/xgpu_spirv.c);
the vertex input of the last set_vertices. A draw's pipeline is the
pipeline's fixed-function part with the attributes its vertex shader reads,
from the streams that feed them or from the inputs' values.

The shaders depend on the draw besides the pipeline's key where the device
lacks what it needs: the unnormalized shorts read as integers (no SSCALED
vertex formats) and the border colours drawn by the shader (no custom border
colours). The stages' textures are the cache's (xgpu_vulkan_textures.c);
a stage that samples the target the draw draws into samples a copy of it; a
render target sampled with its mip levels (the game's levels drawn one by
one) samples a composite of them, the levels past them made from the last.

The VR host supplies the eyes' targets and transforms, and consumes their
images for the OpenXR layers. The enhanced dynamic lights' block and cells
are the draw set's, and their glows a pass of their own; in the VR mode's
eyes, the light each bolt scatters toward each eye. The VR mode's
effects (a pipeline whose pixel key has vr_effect: xgpu_spirv.h) sample the
scene's depth (texture 2, in a pass that writes none: xgpu_vulkan_draw's
reads_depth) and their bitmap's thickness (texture 3, baked by the texture
cache), and reconstruct the world from each view's unprojection
(xgpu_vulkan_effects.h) in their uniforms.
Anti-aliasing's passes are xgpu_vulkan_post.c's, over the target's pixels as
the record names them, in its first layer.

display.hdr (docs/hdr.md): where the core's draws can read their colour
attachment (its capabilities' color_read: the desktop's), the stream says it
draws HDR. The front end's back buffer is then RGBA16F, read where it is
drawn; a draw whose tag blends in its shader has its pipeline's blending
off and reads the pixel it draws over (xgpu_spirv.c); the tone map is a
draw in the view's pass (xgpu_vulkan_post.c), the encoding the core's, for
its swapchain.
*/

#include "host.h"
#include "host_load_trace.h"
#include "host_render_stream.h"
#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_hdr.h"
#include "xgpu_shader.h"
#include "xgpu_spirv.h"
#include "xgpu_vulkan.h"
#include "xgpu_vulkan_effects.h"
#include "xgpu_vulkan_loading.h"
#include "xgpu_vulkan_post.h"
#include "xgpu_vulkan_stream.h"
#include "xgpu_vulkan_textures.h"
#ifdef HALO_VR
#include "host_vulkan_vr.h"
#endif

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the mirror's segments: 4 MB of the contiguous window each */
#define MIRROR_SEGMENT_SIZE 0x400000u
#define MIRROR_SEGMENT_MAXIMUM 64
#define PROGRAM_BUCKETS 256
/* the uniforms' and constants' sizes in the ring: the draw set's ranges
(xgpu_vulkan.c draw_set_make) */
#define UNIFORMS_RANGE 2048
#define CONSTANTS_RANGE (192 * 16)

_Static_assert(sizeof(struct xgpu_spirv_uniforms) <= UNIFORMS_RANGE, "the uniforms' range");
_Static_assert(sizeof(struct xgpu_stream_dynamic_state) == sizeof(struct xgpu_vulkan_dynamic_state),
	"the dynamic state's layouts");
/* the stream's copies of the intermediate form's pieces are their bytes */
_Static_assert(sizeof(struct xgpu_vertex_instruction) == XGPU_STREAM_VERTEX_INSTRUCTION_SIZE, "vertex instruction");
_Static_assert(sizeof(struct xgpu_texture_stage) == XGPU_STREAM_TEXTURE_STAGE_SIZE, "texture stage");
_Static_assert(sizeof(struct xgpu_combiner_stage) == XGPU_STREAM_COMBINER_STAGE_SIZE, "combiner stage");
_Static_assert(sizeof(struct xgpu_final_combiner) == XGPU_STREAM_FINAL_COMBINER_SIZE, "final combiner");
_Static_assert(XGPU_VERTEX_PROGRAM_MAXIMUM == XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM, "vertex program");

/* ---------- the interface's values (xgpu_backend.h) */

enum
{
	_primitive_points, _primitive_lines, _primitive_line_loop, _primitive_line_strip, _primitive_triangles,
	_primitive_triangle_strip, _primitive_triangle_fan,
};

enum
{
	_filter_none, _filter_point, _filter_linear, _filter_anisotropic,
};

enum
{
	_source_none, _source_texture, _source_target,
};

enum
{
	_vertex_none, _vertex_float1, _vertex_float2, _vertex_float3, _vertex_float4, _vertex_color,
	_vertex_short1, _vertex_short2, _vertex_short3, _vertex_short4,
	_vertex_normshort1, _vertex_normshort2, _vertex_normshort3, _vertex_normshort4,
	_vertex_byte1, _vertex_byte2, _vertex_byte3, _vertex_byte4, _vertex_packed3, _vertex_float2h,
};

enum
{
	_lighting_none, _lighting_model, _lighting_model_point, _lighting_environment,
};

/* ---------- state */

/* every stage's texture looked at again (stream.textures_dirty) */
#define TEXTURES_ALL 15u

struct stream_target
{
	struct xgpu_vulkan_image *image;
	/* its multisampled attachment (samples > 1), drawn into since it was
	last resolved into image */
	struct xgpu_vulkan_image *multisampled;
	int samples, unresolved;
	uint32_t width, height, layers;
	int depth, borrowed;
	/* its colour's XGPU_VULKAN_FORMAT_* (display.hdr's back buffer:
	RGBA16F, read where it is drawn) */
	uint8_t format;
	/* counts the draws and clears into it */
	uint32_t written;
};

/* what a pipeline's shaders are made for besides its key */
struct variant
{
	uint16_t integer_attributes;
	uint8_t border[4];
	uint8_t foveated, pad;
};

struct stream_pipeline
{
	/* XGPU_STREAM_PIPELINE_* */
	uint8_t state;
	VkShaderModule vertex_shader, pixel_shader;
	/* the inputs its vertex shader reads */
	uint16_t inputs;
	struct variant variant;
	/* its pixel shader is a VR effect's (XGPU_SPIRV_EFFECT_*: it samples
	the scene's depth and the bitmap's thickness) */
	uint8_t effect;
	/* and a z-sprite's (xgpu_shader.h, xgpu_pixel_program_depth_stage),
	which tests the depth itself, by its fade: xgpu_spirv.c */
	uint8_t effect_tests_depth;
};

struct program_entry
{
	struct program_entry *next;
	uint64_t hash;
	void *record;
};

/* where a stream's vertices are: the mirror's segment at offset, or data
(host memory, bytes of it), copied into the ring once a submission (the
range it was copied to, for the submission uploaded) */
struct vertex_source
{
	struct xgpu_vulkan_buffer *segment;
	uint32_t offset;
	const void *data;
	uint32_t bytes;
	struct xgpu_vulkan_range range;
	uint64_t uploaded;
};

/* a render target's mip levels in one image (mip_composite_get) */
struct composite
{
	struct composite *next;
	uint32_t data, width, height, levels, layers;
	uint8_t format;
	struct xgpu_vulkan_image *image;
	uint32_t rendered_levels;
	uint32_t level_targets[XGPU_STREAM_MIP_LEVELS], level_written[XGPU_STREAM_MIP_LEVELS];
};

static struct
{
	struct xgpu_vulkan_capabilities capabilities;
	unsigned char *ring;
	uint32_t ring_size;
	struct stream_target **targets;
	uint32_t target_count;
	struct xgpu_vulkan_buffer *segments[MIRROR_SEGMENT_MAXIMUM];
	struct program_entry *programs[PROGRAM_BUCKETS], *lightings[PROGRAM_BUCKETS];
	struct stream_pipeline pipelines[XGPU_STREAM_PIPELINES];

	/* the draws' pipeline: its number and definition, its fixed-function
	part */
	uint32_t pipeline_id;
	const struct xgpu_stream_pipeline_define *definition;
	struct xgpu_vulkan_pipeline_description pipeline;

	/* the uniforms' inputs, and their upload (made again where an input
	changed or another submission reads them) */
	struct xgpu_stream_draw_uniforms uniforms;
	uint32_t uniforms_serial;
	int uniforms_set, uniforms_dirty;
	/* (the constant depth bias the uniforms were made with) */
	float depth_bias;
	/* the uniforms uploaded have the VR effects' views (effect_views), for
	the projection as it is; and the views, made for the projection, the
	screen conversion, the viewport, the depth range and the eyes as they
	are (effect_valid), and the conversion's uniforms they were made for */
	int uniforms_effect, effect_valid;
	float effect_unproject[4][4][4], effect_eye[4][4];
	float effect_scale[4], effect_offset[4], effect_screen_offset;
	float constants[192][4];
	int constants_dirty;
	struct xgpu_vulkan_dynamic_state dynamic;
	struct xgpu_vulkan_range uniforms_range, constants_range;
	uint64_t uniforms_uploaded, constants_uploaded;

	/* the stages as set, their samplers' border axes the shader draws, and
	what is bound for them (looked at again where a stage changed, a
	watched page was written, or a target drawn into is sampled): the
	stages to look at again (bits; TEXTURES_ALL) */
	struct host_render_texture_stage stages[4];
	uint8_t border[4];
	unsigned int textures_dirty;
	int feedback_bound;
	uint32_t textures_serial;
	struct xgpu_vulkan_texture textures[4];
	/* the copy of a target drawn into that a draw samples: of which
	target, as of which of its draws */
	struct xgpu_vulkan_image *feedback;
	uint32_t feedback_target, feedback_written;
	/* texture 0's thickness, for the VR effects (effect_textures), and the
	stage, the watched pages' serial and the texture cache's thickness epoch
	it was looked up for (it is looked up again once any changes: the
	texture another, or read again, or the image destroyed: its texture let
	go of, which may come back for the same stage and serial) */
	struct xgpu_vulkan_image *thickness;
	int thickness_known;
	struct xgpu_stream_texture_stage thickness_stage;
	uint32_t thickness_serial, thickness_epoch;
	struct composite *composites;

	/* the vertex input: each input's attribute, the streams, the inputs'
	values; streamed data's guest addresses, copied into the ring for a
	submission at its first draw */
	struct xgpu_vulkan_attribute attributes[16];
	uint16_t strides[16];
	uint16_t integer_attributes;
	uint32_t present;
	struct vertex_source streams[16];
	float constant_attributes[16][4];

	uint32_t color, depth, samples;
	uint32_t views, base;
	int hud_group;
	uint32_t back_buffer;
	/* display.hdr: the frames are HDR (stream_hdr) */
	int hdr_active;
	int preparing, prepared;
	uint64_t prepare_pipelines;
	/* the dynamic lights' glows' shaders for 1, 2 and 4 layers
	(halo_shaders) */
	VkShaderModule halo_vertex[3], halo_pixel[3];
	int halo_failed[3];
	int trace_preparing, trace_pipeline_descriptions;
	struct xgpu_vulkan_statistics trace_statistics;
	struct xgpu_vulkan_texture_statistics trace_textures;
	double trace_seconds[4];
	uint64_t trace_counts[4];
	uint32_t pipeline_misses;
	/* the debugging settings (xgpu_vulkan_stream_debug): the statistics
	logged, the folder the shader modules are written into (NULL: none) */
	int statistics;
	const char *shader_folder;
} stream;

static double preparation_start(void)
{
	return stream.trace_preparing ? host_load_trace_clock(CLOCK_MONOTONIC) : -1.0;
}

static void preparation_end(int stage, double started)
{
	double finished;

	if (started < 0.0)
		return;
	finished = host_load_trace_clock(CLOCK_MONOTONIC);
	if (finished >= started)
	{
		stream.trace_seconds[stage] += finished - started;
		stream.trace_counts[stage]++;
	}
}

/* ---------- vertex programs and lightings, by their hash */

static struct program_entry *program_find(struct program_entry **buckets, uint64_t hash)
{
	struct program_entry *entry;

	for (entry = buckets[hash % PROGRAM_BUCKETS]; entry; entry = entry->next)
	{
		if (entry->hash == hash)
			return entry;
	}
	return NULL;
}

static void program_keep(struct program_entry **buckets, uint64_t hash, const void *record, size_t size)
{
	struct program_entry *entry = program_find(buckets, hash);
	void *copy = malloc(size);

	if (!copy)
		return;
	memcpy(copy, record, size);
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
		{
			free(copy);
			return;
		}
		entry->hash = hash;
		entry->next = buckets[hash % PROGRAM_BUCKETS];
		buckets[hash % PROGRAM_BUCKETS] = entry;
	}
	free(entry->record);
	entry->record = copy;
}

/* ---------- targets */

static struct stream_target *target_get(uint32_t number)
{
	return number && number < stream.target_count ? stream.targets[number] : NULL;
}

static struct xgpu_vulkan_image *target_image(uint32_t number)
{
	struct stream_target *target = target_get(number);

	return target ? target->image : NULL;
}

/* a multisampled target's pixels drawn since, into its texture */
static void target_resolve(struct stream_target *target)
{
	if (!target || !target->unresolved || !target->multisampled)
		return;
	target->unresolved = 0;
	xgpu_vulkan_image_resolve(target->multisampled, target->image);
}

static void composites_forget(uint32_t number)
{
	struct composite *composite;
	uint32_t level;

	for (composite = stream.composites; composite; composite = composite->next)
	{
		for (level = 0; level < composite->rendered_levels && level < XGPU_STREAM_MIP_LEVELS; level++)
		{
			if (composite->level_targets[level] == number)
			{
				composite->rendered_levels = ~0u;
				break;
			}
		}
	}
}

static void target_forget(uint32_t number)
{
	struct stream_target *target = target_get(number);

	if (!target)
		return;
	if (!target->borrowed)
		xgpu_vulkan_image_destroy(target->image);
	xgpu_vulkan_image_destroy(target->multisampled);
	free(target);
	stream.targets[number] = NULL;
	composites_forget(number);
	if (stream.feedback_target == number)
		stream.feedback_target = 0;
	stream.textures_dirty = TEXTURES_ALL;
}

static struct xgpu_vulkan_image *target_image_make(const struct stream_target *target, int samples)
{
	struct xgpu_vulkan_image_description description;

	memset(&description, 0, sizeof(description));
	description.type = target->layers > 1 ? XGPU_VULKAN_IMAGE_2D_ARRAY : XGPU_VULKAN_IMAGE_2D;
	description.format = target->depth ? XGPU_VULKAN_FORMAT_D24S8 :
		target->format == XGPU_VULKAN_FORMAT_RGBA16F ? XGPU_VULKAN_FORMAT_RGBA16F : XGPU_VULKAN_FORMAT_BGRA8;
	description.usage = XGPU_VULKAN_USAGE_SAMPLED | XGPU_VULKAN_USAGE_TARGET;
	if (description.format == XGPU_VULKAN_FORMAT_RGBA16F)
		description.usage |= XGPU_VULKAN_USAGE_INPUT;
	description.samples = (uint8_t)(samples > 1 ? samples : 0);
	description.width = target->width;
	description.height = target->height;
	description.depth_or_layers = target->layers > 1 ? target->layers : 1;
	description.levels = 1;
	return xgpu_vulkan_image_create(&description);
}

static void stream_target_create(void *context, const struct xgpu_stream_target_create *record)
{
	struct stream_target *target;

	(void)context;
	if (!record->target || record->target > 0xffff)
		return;
	target_forget(record->target);
	if (record->target >= stream.target_count)
	{
		uint32_t count = stream.target_count ? stream.target_count : 64;
		struct stream_target **grown;

		while (count <= record->target)
			count *= 2;
		grown = realloc(stream.targets, count * sizeof(*grown));
		if (!grown)
			return;
		memset(grown + stream.target_count, 0, (count - stream.target_count) * sizeof(*grown));
		stream.targets = grown;
		stream.target_count = count;
	}
	target = calloc(1, sizeof(*target));
	if (!target)
		return;
	target->width = record->width;
	target->height = record->height;
	target->layers = record->layers ? record->layers : 1;
	target->depth = record->depth != 0;
	target->format = record->format == XGPU_FORMAT_RGBA16F && stream.capabilities.color_read && !target->depth ?
		XGPU_VULKAN_FORMAT_RGBA16F : XGPU_VULKAN_FORMAT_BGRA8;
	target->image = target_image_make(target, 0);
	if (!target->image)
	{
		free(target);
		return;
	}
	stream.targets[record->target] = target;
	stream.textures_dirty = TEXTURES_ALL;
}

struct xgpu_vulkan_image *xgpu_vulkan_stream_target(uint32_t number)
{
	struct stream_target *target = target_get(number);

	target_resolve(target);
	return target ? target->image : NULL;
}

void xgpu_vulkan_stream_set_vr_target(uint32_t number, struct xgpu_vulkan_image *image)
{
	const struct xgpu_vulkan_image_description *description;
	struct stream_target *target;

	if (!image || !number)
		return;
	description = xgpu_vulkan_image_description(image);
	target = target_get(number);
	if (!target)
	{
		struct xgpu_stream_target_create record;

		memset(&record, 0, sizeof(record));
		record.target = number;
		record.width = description->width;
		record.height = description->height;
		record.layers = description->depth_or_layers;
		stream_target_create(NULL, &record);
		target = target_get(number);
	}
	if (!target)
		return;
	if (target->image != image)
	{
		if (!target->borrowed)
			xgpu_vulkan_image_destroy(target->image);
		target->image = image;
	}
	target->borrowed = 1;
	target->width = description->width;
	target->height = description->height;
	target->layers = description->type == XGPU_VULKAN_IMAGE_2D_ARRAY ? description->depth_or_layers : 1;
	target->written++;
	stream.textures_dirty = TEXTURES_ALL;
	stream.uniforms_dirty = 1;
	stream.uniforms_effect = stream.effect_valid = 0;
}

void xgpu_vulkan_stream_vr_changed(void)
{
	stream.uniforms_dirty = 1;
	stream.textures_dirty = TEXTURES_ALL;
	stream.uniforms_effect = stream.effect_valid = 0;
}

static void stream_target_delete(void *context, uint32_t target)
{
	(void)context;
	target_forget(target);
}

static void stream_target_multisample(void *context, const struct xgpu_stream_target_multisample *record)
{
	struct stream_target *target = target_get(record->target);
	int samples = record->samples > 1 ? record->samples : 0;

	(void)context;
	if (!target || target->samples == samples)
		return;
	/* (its pixels resolved first; the new attachment's are cleared: the
	front end changes it between frames, which clear the screen's targets
	before they draw) */
	target_resolve(target);
	xgpu_vulkan_image_destroy(target->multisampled);
	target->multisampled = samples ? target_image_make(target, samples) : NULL;
	target->samples = target->multisampled ? samples : 0;
	target->unresolved = 0;
}

static void stream_target_resolve(void *context, uint32_t number)
{
	(void)context;
	target_resolve(target_get(number));
}

/* the images draws go to: the targets' multisampled attachments where the
record says so */
static void targets_bind(void)
{
	struct stream_target *color = target_get(stream.color), *depth = target_get(stream.depth);
	struct xgpu_vulkan_image *color_image = color ? color->image : NULL, *depth_image = depth ? depth->image : NULL;

	if (stream.samples)
	{
		if (color && color->multisampled)
		{
			color_image = color->multisampled;
			color->unresolved = 1;
		}
		if (depth && depth->multisampled)
		{
			depth_image = depth->multisampled;
			depth->unresolved = 1;
		}
	}
#ifdef HALO_VR
	if (stream.hud_group >= 0)
	{
		struct xgpu_vulkan_image *group = host_vulkan_vr_hud_group(stream.color, stream.hud_group);

		if (group)
			color_image = group;
	}
#endif
	xgpu_vulkan_set_target_views(color_image, depth_image, stream.base, stream.views);
}

static void stream_set_targets(void *context, const struct xgpu_stream_set_targets *record)
{
	(void)context;
	stream.color = record->color;
	stream.depth = record->depth;
	stream.samples = record->samples;
	stream.views = record->views > 1 ? (uint32_t)record->views : 1;
	stream.base = record->views == -2 ? 1 : record->base;
	stream.hud_group = record->hud_group;
	/* (whether a stage samples the target drawn into) */
	stream.textures_dirty = TEXTURES_ALL;
	targets_bind();
	/* The game's clears of some of the colour's channels are of alpha alone
	(a fog screen's, and a split screen's window's end, which the map's
	enumeration may not reach): their pipeline is made for each of the
	targets the map's draws go to, as the map loads. */
	if (stream.preparing)
		(void)xgpu_vulkan_clear_prepare(XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA);
}

static void stream_clear(void *context, const struct xgpu_stream_clear *record)
{
	uint32_t parts = record->flags & 0x7f;
	uint64_t before;

	(void)context;
	if (target_get(stream.color))
		target_get(stream.color)->written++;
	targets_bind();
	/* Preparation clears nothing: the enumeration's frame is not seen, and
	its draws are not drawn, so its clears were its only GPU work, each a
	pass of the whole target that a tiler loads and stores (1585 passes for
	Blood Gulch, 0.4 s of the Steam Frame's preparation). Some of the
	colour's channels are drawn, through a pipeline made now. */
	if (stream.preparing)
	{
		if ((parts & XGPU_VULKAN_CLEAR_COLOR) && (parts & XGPU_VULKAN_CLEAR_CHANNELS) != XGPU_VULKAN_CLEAR_CHANNELS)
			(void)xgpu_vulkan_clear_prepare(parts);
		return;
	}
	before = xgpu_vulkan_pipeline_count();
	xgpu_vulkan_clear((const int32_t (*)[4])record->rectangles, record->count, parts, record->color, record->depth,
		record->stencil);
	if (!stream.preparing && stream.prepared && xgpu_vulkan_pipeline_count() != before)
	{
		stream.pipeline_misses++;
		host_logf(HOST_LOG_WARN, "vulkan: pipeline missed map preparation: a clear of channels %x, views %u, "
			"samples %u, total %u", (unsigned)(parts & XGPU_VULKAN_CLEAR_CHANNELS), (unsigned)stream.views,
			(unsigned)stream.samples, stream.pipeline_misses);
	}
}

static void stream_clear_target(void *context, const struct xgpu_stream_clear_target *record)
{
	struct stream_target *target = target_get(record->target);
	int32_t whole[1][4];

	(void)context;
	if (!target)
		return;
	target->written++;
	/* (not while preparing, as stream_clear) */
	if (stream.preparing)
		return;
	whole[0][0] = whole[0][1] = 0;
	whole[0][2] = (int32_t)target->width;
	whole[0][3] = (int32_t)target->height;
	xgpu_vulkan_set_targets(target->image, NULL, 0);
	xgpu_vulkan_clear((const int32_t (*)[4])whole, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS, record->color,
		1.0f, 0);
	targets_bind();
}

static int stream_read_target(void *context, const struct xgpu_stream_read_target *record)
{
	struct stream_target *target = target_get(record->target);

	(void)context;
	if (!target || target->depth || !record->pixels || (uint64_t)target->width * target->height * 4 > record->size)
		return 0;
	target_resolve(target);
	return xgpu_vulkan_read_pixels(target->image, record->layer > 0 ? (uint32_t)record->layer : 0,
		host_pointer(record->pixels), target->width * 4, record->tone_map != 0);
}

/* ---------- textures */

/* the axes (bit 0 u, 1 v, 2 w) whose border colour the shader draws: none
where the device's samplers have any colour */
static uint8_t border_axes(const struct xgpu_stream_sampler *sampler)
{
	uint8_t axes = 0;
	int axis;

	if (stream.capabilities.custom_border_colors || sampler->border_color == 0 || sampler->border_color == 0xff000000u ||
		sampler->border_color == 0xffffffffu)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		if (sampler->address[axis] == XGPU_VULKAN_ADDRESS_BORDER)
			axes |= (uint8_t)(1u << axis);
	}
	return axes;
}

/* the sampler the record describes; axes: those whose border the shader
draws, clamped to the edge here */
static VkSampler sampler_get(const struct xgpu_stream_sampler *from, uint8_t axes)
{
	struct xgpu_vulkan_sampler_description to;
	int axis;

	memset(&to, 0, sizeof(to));
	to.minify = from->minify == _filter_point ? XGPU_VULKAN_FILTER_POINT : XGPU_VULKAN_FILTER_LINEAR;
	to.magnify = from->magnify == _filter_point ? XGPU_VULKAN_FILTER_POINT : XGPU_VULKAN_FILTER_LINEAR;
	to.mip = from->mip == _filter_none ? XGPU_VULKAN_MIP_NONE : from->mip == _filter_point ? XGPU_VULKAN_MIP_POINT :
		XGPU_VULKAN_MIP_LINEAR;
	for (axis = 0; axis < 3; axis++)
		to.address[axis] = (axes & (1u << axis)) ? XGPU_VULKAN_ADDRESS_CLAMP : from->address[axis];
	to.anisotropy = (uint8_t)(from->minify == _filter_anisotropic && from->anisotropy > 1 ?
		(from->anisotropy > 16 ? 16 : from->anisotropy) : 1);
	to.lod_bias = from->lod_bias;
	to.min_lod = (float)from->first_level;
	to.border_color = from->border_color;
	return xgpu_vulkan_sampler(&to);
}

/* a stage of a palettized Xbox texture, whose palette's colours are read as
its texture is looked for (xgpu_vulkan_textures.c): a palette is not
watched, so such a stage is looked at again whenever the stages are set,
as every stage was */
static int stage_palettized(const struct xgpu_stream_texture_stage *stage)
{
	return stage->source == _source_texture && stage->palette &&
		((stage->resource_format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == 0x0b;
}

static void stream_set_textures(void *context, const struct host_render_texture_stage stages[4])
{
	int index;

	(void)context;
	for (index = 0; index < 4; index++)
	{
		const struct xgpu_stream_texture_stage *stage = &stages[index].stage;

		if (memcmp(&stream.stages[index].stage.sampler, &stage->sampler, sizeof(stage->sampler)))
			stream.uniforms_dirty = 1;
		/* (only the stages that changed are looked at again: the guest
		sends all four when any changes, and a draw's textures were a
		noticeable part of the render thread's time on a headset) */
		if (memcmp(&stream.stages[index], &stages[index], sizeof(stages[index])) || stage_palettized(stage))
			stream.textures_dirty |= 1u << index;
		stream.stages[index] = stages[index];
		/* (the shader draws the border of a 2D or 3D texture's sampler) */
		stream.border[index] = stage->source != _source_none && stage->sampler_type != _xgpu_sampler_cube &&
			stage->sampler_type != _xgpu_sampler_2d_layered ? border_axes(&stage->sampler) : 0;
	}
}

/* a copy of a target the draw draws into, which it samples (Vulkan cannot
sample an attachment of the pass): copied again once the target has been
drawn into since */
static struct xgpu_vulkan_image *feedback_copy(uint32_t number, struct stream_target *target)
{
	const struct xgpu_vulkan_image_description *description;
	const struct xgpu_vulkan_image_description *source = xgpu_vulkan_image_description(target->image);

	if (target->depth)
		return target->image;
	description = stream.feedback ? xgpu_vulkan_image_description(stream.feedback) : NULL;
	if (!description || description->width != target->width || description->height != target->height ||
		description->depth_or_layers != target->layers || description->format != source->format)
	{
		struct xgpu_vulkan_image_description made;

		xgpu_vulkan_image_destroy(stream.feedback);
		memset(&made, 0, sizeof(made));
		made.type = target->layers > 1 ? XGPU_VULKAN_IMAGE_2D_ARRAY : XGPU_VULKAN_IMAGE_2D;
		made.format = source->format;
		made.usage = XGPU_VULKAN_USAGE_SAMPLED;
		made.width = target->width;
		made.height = target->height;
		made.depth_or_layers = target->layers;
		made.levels = 1;
		stream.feedback = xgpu_vulkan_image_create(&made);
		stream.feedback_target = 0;
	}
	if (stream.feedback && (stream.feedback_target != number || stream.feedback_written != target->written))
	{
		uint32_t layer;

		for (layer = 0; layer < target->layers; layer++)
			xgpu_vulkan_image_copy(target->image, 0, layer, stream.feedback, 0, layer);
		stream.feedback_target = number;
		stream.feedback_written = target->written;
	}
	return stream.feedback ? stream.feedback : target->image;
}

/* a render target sampled with its mip levels: its levels' targets copied
into an image of them all, the levels past them made from the last; copied
again only once a level's target has been drawn into since */
static struct xgpu_vulkan_image *composite_get(const struct host_render_texture_stage *source)
{
	const struct xgpu_stream_texture_stage *stage = &source->stage;
	struct composite *composite;
	uint32_t level, rendered = stage->rendered_levels, levels = stage->levels;
	struct stream_target *base = target_get(stage->target);
	uint32_t layers = base ? base->layers : 1;
	uint8_t format = base ? xgpu_vulkan_image_description(base->image)->format : XGPU_VULKAN_FORMAT_BGRA8;
	int changed;

	if (levels > XGPU_STREAM_MIP_LEVELS)
		levels = XGPU_STREAM_MIP_LEVELS;
	if (rendered > levels)
		rendered = levels;
	for (composite = stream.composites; composite; composite = composite->next)
	{
		if (composite->data == stage->data && composite->width == stage->width && composite->height == stage->height &&
			composite->levels == levels && composite->layers == layers && composite->format == format)
			break;
	}
	if (!composite)
	{
		struct xgpu_vulkan_image_description made;

		composite = calloc(1, sizeof(*composite));
		if (!composite)
			return NULL;
		composite->data = stage->data;
		composite->width = stage->width;
		composite->height = stage->height;
		composite->levels = levels;
		composite->layers = layers;
		composite->format = format;
		memset(&made, 0, sizeof(made));
		made.type = layers > 1 ? XGPU_VULKAN_IMAGE_2D_ARRAY : XGPU_VULKAN_IMAGE_2D;
		made.format = format;
		made.usage = XGPU_VULKAN_USAGE_SAMPLED;
		made.width = stage->width;
		made.height = stage->height;
		made.depth_or_layers = layers;
		made.levels = levels ? levels : 1;
		composite->image = xgpu_vulkan_image_create(&made);
		composite->rendered_levels = ~0u;
		composite->next = stream.composites;
		stream.composites = composite;
	}
	if (!composite->image)
		return NULL;
	changed = rendered != composite->rendered_levels;
	for (level = 0; level < rendered && !changed; level++)
	{
		changed = source->levels[level].target != composite->level_targets[level] ||
			source->levels[level].written != composite->level_written[level];
	}
	if (!changed)
		return composite->image;
	composite->rendered_levels = rendered;
	for (level = 0; level < rendered; level++)
	{
		struct stream_target *target = target_get(source->levels[level].target);

		composite->level_targets[level] = source->levels[level].target;
		composite->level_written[level] = source->levels[level].written;
		if (!target)
			continue;
		target_resolve(target);
		{
			uint32_t layer;

			for (layer = 0; layer < target->layers && layer < layers; layer++)
				xgpu_vulkan_image_copy(target->image, 0, layer, composite->image, level, layer);
		}
	}
	/* levels the game did not render come from the ones it did */
	if (rendered < composite->levels)
		xgpu_vulkan_image_make_levels(composite->image, rendered ? rendered : 1);
	return composite->image;
}

/* the stages' images and samplers for a draw */
static void textures_bind(void)
{
	uint32_t serial = host_memory_watch_serial();
	unsigned int dirty = stream.textures_dirty;
	int index;

	/* (a watched page written: any texture may be read again; a target
	drawn into sampled: its copy is checked at each draw. Otherwise the
	stages that did not change keep what they have: each stage's image
	and sampler are its own stage's alone, and the targets' changes mark
	them all) */
	if (serial != stream.textures_serial || stream.feedback_bound)
		dirty = TEXTURES_ALL;
	if (!dirty)
		return;
	stream.textures_dirty = 0;
	stream.textures_serial = serial;
	stream.feedback_bound = 0;
	for (index = 0; index < 4; index++)
	{
		const struct host_render_texture_stage *source = &stream.stages[index];
		const struct xgpu_stream_texture_stage *stage = &source->stage;
		struct xgpu_vulkan_texture *texture = &stream.textures[index];

		if (!(dirty & (1u << index)))
			continue;
		texture->image = NULL;
		texture->kind = stage->sampler_type == _xgpu_sampler_3d ? XGPU_VULKAN_IMAGE_3D :
			stage->sampler_type == _xgpu_sampler_cube ? XGPU_VULKAN_IMAGE_CUBE :
			stage->sampler_type == _xgpu_sampler_2d_layered ? XGPU_VULKAN_IMAGE_2D_ARRAY : XGPU_VULKAN_IMAGE_2D;
		texture->sampler = stage->source != _source_none ? sampler_get(&stage->sampler, stream.border[index]) :
			VK_NULL_HANDLE;
		if (stage->source == _source_target)
		{
			struct stream_target *target = target_get(stage->target);

			if (!target)
				continue;
			target_resolve(target);
			if (stage->levels)
			{
				texture->image = composite_get(source);
			}
			else if (stage->drawn && stage->drawn == stage->target && stage->target == stream.color)
			{
				/* (drawn into by this draw: its copy, checked at each draw) */
				texture->image = feedback_copy(stage->target, target);
				stream.feedback_bound = 1;
			}
			else
			{
				texture->image = target->image;
			}
		}
		else if (stage->source == _source_texture || stage->source == XGPU_STREAM_SOURCE_IMAGE)
		{
			texture->image = xgpu_vulkan_textures_get(stage);
		}
		/* (an image of another kind than the shader samples: none) */
		if (texture->image)
		{
			const struct xgpu_vulkan_image_description *description = xgpu_vulkan_image_description(texture->image);
			uint8_t kind = description->type;

			if (kind != texture->kind && !(kind == XGPU_VULKAN_IMAGE_2D_ARRAY && texture->kind == XGPU_VULKAN_IMAGE_2D))
				texture->image = NULL;
		}
	}
	/* (the images' uploads may have ended the targets' pass) */
	targets_bind();
}

/* ---------- pipelines */

/* the per-pixel lighting's lights' registers (xgpu_backend.h's
XGPU_MODEL_LIGHT_REGISTER: c[-82], c[-79] to c[-69], c[-57]) */
#define MODEL_LIGHT_FIRST (96 - 82)
#define MODEL_LIGHT_LAST (96 - 69)
#define MODEL_LIGHT_OWN_LIGHTS (96 - 57)
/* the screen conversion's registers (c[-38], c[-37]) */
#define SCREEN_CONVERSION (96 - 38)

static void stream_set_constants(void *context, const struct xgpu_stream_constants *record)
{
	uint32_t count = record->count;

	(void)context;
	if (record->first >= 192)
		return;
	if (count > 192 - record->first)
		count = 192 - record->first;
	if (!memcmp(stream.constants[record->first], record->values, (size_t)count * sizeof(stream.constants[0])))
		return;
	memcpy(stream.constants[record->first], record->values, (size_t)count * sizeof(stream.constants[0]));
	stream.constants_dirty = 1;
	/* (the world's projection or the screen conversion: the effects' views) */
	if (record->first < 4 || (record->first <= SCREEN_CONVERSION + 1 && record->first + count > SCREEN_CONVERSION))
		stream.uniforms_effect = stream.effect_valid = 0;
	if ((record->first <= MODEL_LIGHT_LAST && record->first + count > MODEL_LIGHT_FIRST) ||
		(record->first <= MODEL_LIGHT_OWN_LIGHTS && record->first + count > MODEL_LIGHT_OWN_LIGHTS))
		stream.uniforms_dirty = 1;
}

static void stream_vertex_program(void *context, const struct xgpu_stream_vertex_program *record)
{
	uint64_t hash = record->hash[0] | ((uint64_t)record->hash[1] << 32);

	(void)context;
	program_keep(stream.programs, hash, record, XGPU_STREAM_HEADER_SIZE(record->header));
}

static void stream_vertex_lighting(void *context, const struct xgpu_stream_vertex_lighting *record)
{
	uint64_t hash = record->hash[0] | ((uint64_t)record->hash[1] << 32);

	(void)context;
	program_keep(stream.lightings, hash, record, sizeof(*record));
}

static void stream_pipeline_define(void *context, const struct xgpu_stream_pipeline_define *record)
{
	(void)context;
	/* (a number defined again is another pipeline) */
	if (record->id && record->id < XGPU_STREAM_PIPELINES)
		stream.pipelines[record->id].state = XGPU_STREAM_PIPELINE_UNKNOWN;
	if (record->id == stream.pipeline_id)
		stream.pipeline_id = 0;
}

/* ---------- the shaders (SPIR-V, xgpu_spirv.c)

The stream sends the vertex programs and pixel programs as the guest's
structures' bytes (xgpu_stream.h): they become the host's structures
(xgpu_shader.h, whose unsigned longs are 8 bytes here) field by field. */

static void vertex_program_convert(const struct xgpu_stream_vertex_program *record, struct xgpu_vertex_program *program)
{
	uint32_t count = record->instruction_count;

	memset(program, 0, sizeof(*program));
	if (count > XGPU_VERTEX_PROGRAM_MAXIMUM)
		count = XGPU_VERTEX_PROGRAM_MAXIMUM;
	program->instruction_count = count;
	memcpy(program->instructions, record->instructions, (size_t)count * sizeof(program->instructions[0]));
	program->hash = record->hash[0] | ((unsigned long long)record->hash[1] << 32);
	program->projects = (unsigned char)(record->projects != 0);
}

static void vertex_lighting_convert(const struct xgpu_stream_vertex_lighting *record,
	struct nv2a_vertex_lighting *lighting)
{
	memset(lighting, 0, sizeof(*lighting));
	if (!record)
		return;
	lighting->lights = record->lights;
	lighting->normal_instruction = record->normal_instruction;
	lighting->normal_register = record->normal_register;
	lighting->position_instruction = record->position_instruction;
	lighting->position_register = record->position_register;
	lighting->world_found = record->world_found != 0;
	lighting->world_instruction = record->world_instruction;
	lighting->world_register = record->world_register;
}

static void pixel_program_convert(const struct xgpu_stream_pixel_program *wire, struct xgpu_pixel_program *program)
{
	memset(program, 0, sizeof(*program));
	memcpy(program->stages, wire->stages, sizeof(program->stages));
	program->combiner_count = wire->combiner_count <= 8 ? wire->combiner_count : 8;
	memcpy(program->combiners, wire->combiners, sizeof(program->combiners));
	program->mux_msb = wire->mux_msb;
	memcpy(&program->final, wire->final, sizeof(program->final));
	program->fog = wire->fog;
	program->fog_mode = wire->fog_mode;
	program->alpha_test = wire->alpha_test;
	program->alpha_test_samples = wire->alpha_test_samples;
	program->count_samples = wire->count_samples;
	program->coverage_alpha = wire->coverage_alpha;
	program->point_threshold = wire->point_threshold;
	program->per_pixel_lighting = wire->per_pixel_lighting;
	program->dynamic_lights = wire->dynamic_lights;
	program->multiview = wire->multiview;
	program->source_blend = wire->source_blend;
	program->hdr = wire->hdr;
}

/* debug.gpu_dump_shaders (HALO_VULKAN_SHADERS) writes each module into its
folder as it is made */
static void shader_dump(const char *name, const struct xgpu_spirv_module *module)
{
	char path[1024];
	FILE *file;

	if (!stream.shader_folder)
		return;
	snprintf(path, sizeof(path), "%s/%s.spv", stream.shader_folder, name);
	if ((file = fopen(path, "wb")) != NULL)
	{
		fwrite(module->words, 4, module->count, file);
		fclose(file);
	}
}

static uint32_t bytes_hash(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	uint32_t hash = 2166136261u;
	size_t index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 16777619u;
	return hash;
}

/* how a VR effect fades under its pipeline's blend (XGPU_SPIRV_FADE_*): to
its blend's nothing, which leaves the target as it is. The effects' blends
(rasterizer_xbox.c's framebuffer blend functions): alpha blend, multiply,
double multiply, add, subtract, min, max, alpha-multiply add. */
static int effect_fade(const struct xgpu_stream_pipeline_key *key)
{
	if (!key->blend)
		return XGPU_SPIRV_FADE_NONE;
	switch (key->blend_operation)
	{
	case XGPU_BLEND_MIN:
		return XGPU_SPIRV_FADE_TO_WHITE;
	case XGPU_BLEND_MAX:
	case XGPU_BLEND_REVERSE_SUBTRACT:
		return XGPU_SPIRV_FADE_COLOR;
	case XGPU_BLEND_ADD:
		break;
	default:
		return XGPU_SPIRV_FADE_NONE;
	}
	if (key->blend_source == XGPU_BLEND_SOURCE_ALPHA &&
		(key->blend_destination == XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA || key->blend_destination == XGPU_BLEND_ONE))
		return XGPU_SPIRV_FADE_ALPHA;
	if (key->blend_source == XGPU_BLEND_ONE && key->blend_destination == XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA)
		return XGPU_SPIRV_FADE_COLOR_ALPHA;
	if (key->blend_destination == XGPU_BLEND_ONE)
		return XGPU_SPIRV_FADE_COLOR;
	if ((key->blend_source == XGPU_BLEND_DESTINATION_COLOR && key->blend_destination == XGPU_BLEND_ZERO) ||
		(key->blend_source == XGPU_BLEND_ZERO && key->blend_destination == XGPU_BLEND_SOURCE_COLOR))
		return XGPU_SPIRV_FADE_TO_WHITE;
	if (key->blend_source == XGPU_BLEND_DESTINATION_COLOR && key->blend_destination == XGPU_BLEND_SOURCE_COLOR)
		return XGPU_SPIRV_FADE_TO_GREY;
	return XGPU_SPIRV_FADE_NONE;
}

/* a pipeline's shaders for a variant: made if the pipeline has none for it;
0 if they cannot be */
static int pipeline_shaders(struct stream_pipeline *entry, const struct xgpu_stream_pipeline_define *definition,
	const struct variant *variant)
{
	static const struct nv2a_vertex_lighting environment = { XGPU_ENVIRONMENT_LIGHTING };
	const struct xgpu_stream_pipeline_key *key = &definition->key;
	uint64_t hash = key->vertex_program[0] | ((uint64_t)key->vertex_program[1] << 32);
	struct program_entry *program_record = program_find(stream.programs, hash);
	struct program_entry *lighting_record = program_find(stream.lightings, hash);
	struct xgpu_vertex_program *program;
	struct xgpu_pixel_program pixel;
	struct nv2a_vertex_lighting model;
	const struct nv2a_vertex_lighting *lighting;
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	unsigned long packed = key->immediate ? 0 : key->packed_attributes;
	unsigned long integers = key->immediate ? 0 : variant->integer_attributes;
	char name[96];

	if (entry->state == XGPU_STREAM_PIPELINE_MADE && !memcmp(&entry->variant, variant, sizeof(*variant)))
		return 1;
	if (entry->state == XGPU_STREAM_PIPELINE_FAILED || !program_record)
	{
		entry->state = XGPU_STREAM_PIPELINE_FAILED;
		return 0;
	}
	entry->variant = *variant;
	entry->vertex_shader = entry->pixel_shader = VK_NULL_HANDLE;
	/* (a model lighting program's normal and position, or a structure's
	lightmap pass's own) */
	vertex_lighting_convert(lighting_record ? lighting_record->record : NULL, &model);
	lighting = key->lighting == _lighting_none ? NULL : key->lighting == _lighting_environment ? &environment : &model;
	memset(&options, 0, sizeof(options));
	memcpy(options.border, variant->border, sizeof(options.border));
	options.multiview = key->views;
	options.foveated = variant->foveated;
	options.count_samples = key->pixel.count_samples != 0;
	/* (display.hdr: a draw blending in its shader reads its target's
	pixel, each sample's own) */
	options.input_samples = key->samples > 1 ? key->samples : 1;
	options.debug_texture0 = getenv("HALO_GPU_DEBUG_T0") != NULL;
	options.debug_flat = getenv("HALO_GPU_DEBUG_FLAT") != NULL;
	options.reversed_depth = XGPU_VULKAN_REVERSED_DEPTH;
	program = malloc(sizeof(*program));
	if (!program)
		return 0;
	vertex_program_convert(program_record->record, program);
	entry->inputs = (uint16_t)xgpu_spirv_vertex_inputs(program, lighting);
	if (xgpu_spirv_vertex_shader(program, packed, integers, lighting, &options, &module))
	{
		snprintf(name, sizeof(name), "vs%08x%08x_%lx_%lx_%u_view%u_fovea%u", (unsigned)(hash >> 32), (unsigned)hash,
			packed, integers, key->lighting, key->views, variant->foveated);
		shader_dump(name, &module);
		entry->vertex_shader = xgpu_vulkan_shader(module.words, module.count);
		free(module.words);
	}
	free(program);
	pixel_program_convert(&definition->pixel_program, &pixel);
	/* (a VR effect's, where its program leaves the effects' textures free) */
	entry->effect = entry->effect_tests_depth = 0;
	if (key->pixel.vr_effect && key->pixel.vr_effect <= XGPU_SPIRV_EFFECT_THICK && xgpu_spirv_effect_possible(&pixel))
	{
		entry->effect = key->pixel.vr_effect;
		entry->effect_tests_depth = xgpu_pixel_program_depth_stage(&pixel) != 0;
		options.effect = key->pixel.vr_effect;
		options.effect_fade = effect_fade(key);
	}

	if (xgpu_spirv_pixel_shader(&pixel, &options, &module))
	{
		snprintf(name, sizeof(name), "ps_%08x_%02x%02x%02x%02x_fovea%u_effect%u%u", bytes_hash(&definition->pixel_program,
			sizeof(definition->pixel_program)), options.border[0], options.border[1], options.border[2], options.border[3],
			variant->foveated, (unsigned)options.effect, (unsigned)options.effect_fade);
		shader_dump(name, &module);
		entry->pixel_shader = xgpu_vulkan_shader(module.words, module.count);
		free(module.words);
	}
	if (entry->vertex_shader && entry->pixel_shader)
	{
		entry->state = XGPU_STREAM_PIPELINE_MADE;
		return 1;
	}
	/* (one that fails is not tried again for every draw) */
	entry->state = XGPU_STREAM_PIPELINE_FAILED;
	host_logf(HOST_LOG_WARN, "vulkan: the shaders of a pipeline (vertex program %08x%08x) cannot be made",
		(unsigned)(hash >> 32), (unsigned)hash);
	return 0;
}

/* the variant the draws have now */
static void variant_current(const struct xgpu_stream_pipeline_key *key, struct variant *variant)
{
	memset(variant, 0, sizeof(*variant));
#ifdef HALO_VR
	variant->foveated = host_vulkan_vr_foveated() != 0;
#endif
	variant->integer_attributes = key->immediate ? 0 : stream.integer_attributes;
	memcpy(variant->border, stream.border, sizeof(variant->border));
}

static int stream_pipeline(void *context, uint32_t id, const struct xgpu_stream_pipeline_define *definition)
{
	struct stream_pipeline *entry;
	const struct xgpu_stream_pipeline_key *key = &definition->key;
	struct xgpu_vulkan_pipeline_description *pipeline = &stream.pipeline;

	(void)context;
	stream.pipeline_id = 0;
	if (!id || id >= XGPU_STREAM_PIPELINES)
		return 0;
	entry = &stream.pipelines[id];
	/* (made at first in the variant the draws have now, to tell the guest
	whether it can be) */
	if (entry->state == XGPU_STREAM_PIPELINE_UNKNOWN)
	{
		struct variant variant;

		variant_current(key, &variant);
		pipeline_shaders(entry, definition, &variant);
	}
	if (entry->state != XGPU_STREAM_PIPELINE_MADE)
		return 0;
	stream.pipeline_id = id;
	stream.definition = definition;
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->blend = key->blend;
	pipeline->blend_source = key->blend_source;
	pipeline->blend_destination = key->blend_destination;
	pipeline->blend_operation = key->blend_operation;
	pipeline->color_mask = key->color_mask;
	pipeline->depth_test = key->depth_test;
	pipeline->depth_function = key->depth_function;
	pipeline->depth_write = key->depth_write;
	/* (a VR z-sprite's puff is nearer than its card, which the scene's
	depth would cut where it stands on a floor: its shader tests the depth
	it computes against the scene's, sampled, and writes none) */
	if (entry->effect_tests_depth)
		pipeline->depth_test = pipeline->depth_write = 0;
	pipeline->stencil_test = key->stencil_test;
	pipeline->stencil_function = key->stencil_function;
	pipeline->stencil_fail = key->stencil_fail;
	pipeline->stencil_depth_fail = key->stencil_depth_fail;
	pipeline->stencil_pass = key->stencil_pass;
	pipeline->stencil_read_mask = (uint8_t)key->stencil_read_mask;
	pipeline->stencil_write_mask = (uint8_t)key->stencil_write_mask;
	pipeline->cull = key->cull;
	pipeline->front_counter_clockwise = key->front_counter_clockwise;
	pipeline->cull_front = key->cull_front;
	pipeline->fill = key->fill;
	pipeline->depth_bias = key->depth_bias;
	/* (the targets' formats: XGPU_FORMAT_NONE, _BGRA8, _D24S8, _RGBA16F) */
	pipeline->color_format = key->color_format == XGPU_FORMAT_BGRA8 ? XGPU_VULKAN_FORMAT_BGRA8 :
		key->color_format == XGPU_FORMAT_RGBA16F && stream.capabilities.color_read ? XGPU_VULKAN_FORMAT_RGBA16F :
		XGPU_VULKAN_FORMAT_NONE;
	/* (display.hdr: a tag that blends in the shader, which reads the pixel
	it draws over: the pipeline's blending off) */
	pipeline->color_read = pipeline->color_format == XGPU_VULKAN_FORMAT_RGBA16F &&
		XGPU_HDR_SHADER_BLENDS(key->pixel.hdr);
	pipeline->depth_format = key->depth_format == 2 ? XGPU_VULKAN_FORMAT_D24S8 : XGPU_VULKAN_FORMAT_NONE;
	pipeline->samples = key->samples > 1 ? key->samples : 1;
	return 1;
}

static void stream_draw_state(void *context, const struct xgpu_stream_dynamic_state *dynamic,
	const struct xgpu_stream_draw_uniforms *uniforms, uint32_t uniforms_serial)
{
	(void)context;
	if (dynamic)
	{
		if (memcmp(stream.dynamic.blend_color, dynamic->blend_color, sizeof(stream.dynamic.blend_color)))
			stream.uniforms_dirty = 1;
		if (memcmp(stream.dynamic.viewport, dynamic->viewport, sizeof(stream.dynamic.viewport)))
		{
			stream.uniforms_dirty = 1;
			stream.effect_valid = 0;
		}
		/* (the VR effects' views are of the depth range too) */
		if (memcmp(stream.dynamic.depth_range, dynamic->depth_range, sizeof(stream.dynamic.depth_range)))
			stream.uniforms_effect = stream.effect_valid = 0;
		memcpy(&stream.dynamic, dynamic, sizeof(stream.dynamic));
	}
	if (uniforms && (!stream.uniforms_set || uniforms_serial != stream.uniforms_serial))
	{
		stream.uniforms = *uniforms;
		stream.uniforms_serial = uniforms_serial;
		stream.uniforms_set = 1;
		stream.uniforms_dirty = 1;
	}
}

static void color_from_argb(uint32_t argb, float color[4])
{
	color[0] = (float)((argb >> 16) & 0xff) / 255.0f;
	color[1] = (float)((argb >> 8) & 0xff) / 255.0f;
	color[2] = (float)(argb & 0xff) / 255.0f;
	color[3] = (float)(argb >> 24) / 255.0f;
}

/* the VR effects' views (xgpu_vulkan_effects.h) into the uniforms: the
eyes', or foveated the eyes' peripheries and insets, as the uniforms' eye
corrections and insets have them (host_vulkan_vr_uniforms, made first), for
a projection (c[-96] to c[-93], c[-38], c[-37]) and the screen conversion,
in the viewport and depth range drawn with; their clip transforms into
clip (NULL: not wanted). The count made. */
static int effect_views(const struct xgpu_spirv_uniforms *to, const float projection[6][4], const float scale[4],
	const float offset[4], float screen_offset, float (*unproject)[4][4], float (*eye)[4], float (*clip)[4][4])
{
#ifdef HALO_VR
	struct xgpu_vulkan_effects_view made;
	int foveated = host_vulkan_vr_foveated(), view, count = 0;
	float depth_range[2];

	depth_range[0] = stream.dynamic.depth_range[0] >= 0.0f && stream.dynamic.depth_range[0] <= 1.0f ?
		stream.dynamic.depth_range[0] : 0.0f;
	depth_range[1] = stream.dynamic.depth_range[1] >= 0.0f && stream.dynamic.depth_range[1] <= 1.0f ?
		stream.dynamic.depth_range[1] : 1.0f;
	for (view = 0; view < (foveated ? 4 : 2); view++)
	{
		if (!xgpu_vulkan_effects_view(projection, scale, offset, screen_offset, to->eye_correction[foveated ? view >> 1 :
			view], foveated ? to->fovea_view[view] : NULL, stream.dynamic.viewport, depth_range, &made))
			continue;
		memcpy(unproject[view], made.unproject, sizeof(made.unproject));
		memcpy(eye[view], made.eye, sizeof(made.eye));
		if (clip)
			memcpy(clip[view], made.clip, sizeof(made.clip));
		count++;
	}
	return count;
#else
	(void)to;
	(void)projection;
	(void)scale;
	(void)offset;
	(void)screen_offset;
	(void)unproject;
	(void)eye;
	(void)clip;
	return 0;
#endif
}

/* the uniforms both stages read and the vertex constants, in the ring for
this submission (with the VR effects' views, for a VR effect's draw) */
static int uniforms_update(int effect)
{
	uint64_t serial = xgpu_vulkan_serial();
	/* (the constant depth bias, reversed depth's: the pipeline's and the
	dynamic state's) */
	float depth_bias = XGPU_VULKAN_REVERSED_DEPTH && stream.definition ?
		xgpu_reversed_depth_bias(stream.definition->key.depth_bias, stream.dynamic.depth_bias[1]) : 0.0f;

	if (effect && !stream.uniforms_effect)
		stream.uniforms_dirty = 1;
	if (depth_bias != stream.depth_bias)
	{
		stream.depth_bias = depth_bias;
		stream.uniforms_dirty = 1;
	}
	if (stream.uniforms_dirty || stream.uniforms_uploaded != serial)
	{
		const struct xgpu_stream_draw_uniforms *from = &stream.uniforms;
		struct xgpu_spirv_uniforms *to = xgpu_vulkan_upload(UNIFORMS_RANGE, 256, &stream.uniforms_range);
		int stage, light;

		if (!to)
			return 0;
		memset(to, 0, sizeof(*to));
		memcpy(to->viewport_scale, from->viewport_scale, sizeof(to->viewport_scale));
		memcpy(to->viewport_offset, from->viewport_offset, sizeof(to->viewport_offset));
		to->point_size = from->point_size;
		to->screen_offset = from->screen_offset;
		to->alpha_reference = from->alpha_reference;
#ifdef HALO_VR
		host_vulkan_vr_uniforms(to, stream.dynamic.viewport);
#endif
		to->visibility_counter = (float)xgpu_vulkan_visibility_counter();
		to->depth_bias = stream.depth_bias;
		memcpy(to->ps_c0, from->ps_c0, sizeof(to->ps_c0));
		memcpy(to->ps_c1, from->ps_c1, sizeof(to->ps_c1));
		memcpy(to->ps_final_c0, from->ps_final_c0, sizeof(to->ps_final_c0));
		memcpy(to->ps_final_c1, from->ps_final_c1, sizeof(to->ps_final_c1));
		memcpy(to->fog_color, from->fog_color, sizeof(to->fog_color));
		memcpy(to->fog_parameters, from->fog_parameters, sizeof(to->fog_parameters));
		memcpy(to->bump_matrix, from->bump_matrix, sizeof(to->bump_matrix));
		memcpy(to->bump_luminance, from->bump_luminance, sizeof(to->bump_luminance));
		memcpy(to->texture_scale, from->texture_scale, sizeof(to->texture_scale));
		memcpy(to->blend_constant, stream.dynamic.blend_color, sizeof(to->blend_constant));
		for (stage = 0; stage < 4; stage++)
		{
			const struct xgpu_stream_sampler *sampler = &stream.stages[stage].stage.sampler;

			color_from_argb(sampler->border_color, to->texture_border[stage]);
			to->texture_border_linear[stage] = sampler->magnify >= _filter_linear || sampler->minify >= _filter_linear ?
				1.0f : 0.0f;
		}
		/* the per-pixel lighting's lights: vertex constants (xgpu_backend.h) */
		for (light = 0; light < XGPU_MODEL_LIGHT_COUNT; light++)
			memcpy(to->model_lights[light], stream.constants[XGPU_MODEL_LIGHT_REGISTER(light)], sizeof(to->model_lights[0]));
		stream.uniforms_effect = 0;
		if (effect)
		{
			/* (made again only where what they are made of changed) */
			if (!stream.effect_valid || memcmp(stream.effect_scale, from->viewport_scale, sizeof(stream.effect_scale)) ||
				memcmp(stream.effect_offset, from->viewport_offset, sizeof(stream.effect_offset)) ||
				stream.effect_screen_offset != from->screen_offset)
			{
				float projection[6][4];

				memcpy(projection, stream.constants[0], 4 * sizeof(projection[0]));
				memcpy(projection[4], stream.constants[SCREEN_CONVERSION], 2 * sizeof(projection[0]));
				memset(stream.effect_unproject, 0, sizeof(stream.effect_unproject));
				memset(stream.effect_eye, 0, sizeof(stream.effect_eye));
				effect_views(to, (const float (*)[4])projection, from->viewport_scale, from->viewport_offset,
					from->screen_offset, stream.effect_unproject, stream.effect_eye, NULL);
				memcpy(stream.effect_scale, from->viewport_scale, sizeof(stream.effect_scale));
				memcpy(stream.effect_offset, from->viewport_offset, sizeof(stream.effect_offset));
				stream.effect_screen_offset = from->screen_offset;
				stream.effect_valid = 1;
			}
			memcpy(to->effect_unproject, stream.effect_unproject, sizeof(to->effect_unproject));
			memcpy(to->effect_eye, stream.effect_eye, sizeof(to->effect_eye));
			stream.uniforms_effect = 1;
		}
		stream.uniforms_dirty = 0;
		stream.uniforms_uploaded = serial;
	}
	if (stream.constants_dirty || stream.constants_uploaded != serial)
	{
		void *to = xgpu_vulkan_upload(CONSTANTS_RANGE, 256, &stream.constants_range);

		if (!to)
			return 0;
		memcpy(to, stream.constants, CONSTANTS_RANGE);
		stream.constants_dirty = 0;
		stream.constants_uploaded = serial;
	}
	/* (the constants' upload may have begun another submission) */
	return stream.uniforms_uploaded == xgpu_vulkan_serial();
}

/* ---------- vertices */

static struct xgpu_vulkan_buffer *segment_buffer(int32_t segment)
{
	if (segment < 0 || segment >= MIRROR_SEGMENT_MAXIMUM)
		return NULL;
	if (!stream.segments[segment])
		stream.segments[segment] = xgpu_vulkan_buffer_create(MIRROR_SEGMENT_SIZE);
	return stream.segments[segment];
}

static void stream_mirror_write(void *context, const struct xgpu_stream_mirror_write *record)
{
	(void)context;
	if ((uint64_t)record->offset + record->size > MIRROR_SEGMENT_SIZE)
		return;
	xgpu_vulkan_buffer_write(segment_buffer((int32_t)record->segment), record->offset, host_pointer(record->data),
		record->size, record->unused != 0);
}

/* an element's vertex format; the unnormalized shorts read as integers where
there are no SSCALED formats */
static VkFormat element_format(uint8_t format, int *integers)
{
	int scaled = stream.capabilities.scaled_shorts;

	*integers = 0;
	switch (format)
	{
	case _vertex_float1: return VK_FORMAT_R32_SFLOAT;
	case _vertex_float2: return VK_FORMAT_R32G32_SFLOAT;
	case _vertex_float3: case _vertex_float2h: return VK_FORMAT_R32G32B32_SFLOAT;
	case _vertex_float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case _vertex_color: return VK_FORMAT_B8G8R8A8_UNORM;
	case _vertex_short1: *integers = !scaled; return scaled ? VK_FORMAT_R16_SSCALED : VK_FORMAT_R16_SINT;
	case _vertex_short2: *integers = !scaled; return scaled ? VK_FORMAT_R16G16_SSCALED : VK_FORMAT_R16G16_SINT;
	case _vertex_short3: *integers = !scaled; return scaled ? VK_FORMAT_R16G16B16_SSCALED : VK_FORMAT_R16G16B16_SINT;
	case _vertex_short4: *integers = !scaled; return scaled ? VK_FORMAT_R16G16B16A16_SSCALED : VK_FORMAT_R16G16B16A16_SINT;
	case _vertex_normshort1: return VK_FORMAT_R16_SNORM;
	case _vertex_normshort2: return VK_FORMAT_R16G16_SNORM;
	case _vertex_normshort3: return VK_FORMAT_R16G16B16_SNORM;
	case _vertex_normshort4: return VK_FORMAT_R16G16B16A16_SNORM;
	case _vertex_byte1: return VK_FORMAT_R8_UNORM;
	case _vertex_byte2: return VK_FORMAT_R8G8_UNORM;
	case _vertex_byte3: return VK_FORMAT_R8G8B8_UNORM;
	case _vertex_byte4: return VK_FORMAT_R8G8B8A8_UNORM;
	case _vertex_packed3: return VK_FORMAT_R32_UINT;
	default: return VK_FORMAT_UNDEFINED;
	}
}

static void stream_set_vertices(void *context, const struct host_render_vertex_input *input)
{
	uint32_t integers = 0, index;

	(void)context;
	memset(stream.attributes, 0, sizeof(stream.attributes));
	if (input->layout)
	{
		for (index = 0; index < input->layout->element_count; index++)
		{
			const struct xgpu_stream_vertex_element *element = &input->layout->elements[index];
			int integer;

			if (element->reg >= 16 || element->stream >= 16 || !(input->present & (1u << element->stream)))
				continue;
			stream.attributes[element->reg].format = (uint32_t)element_format(element->format, &integer);
			stream.attributes[element->reg].binding = element->stream;
			stream.attributes[element->reg].offset = element->offset;
			if (integer)
				integers |= 1u << element->reg;
		}
	}
	stream.integer_attributes = (uint16_t)integers;
	stream.present = input->present;
	for (index = 0; index < 16; index++)
	{
		const struct xgpu_stream_vertex_stream *source = &input->streams[index];
		struct vertex_source *to = &stream.streams[index];

		memset(to, 0, sizeof(*to));
		if (!(input->present & (1u << index)))
			continue;
		stream.strides[index] = (uint16_t)source->stride;
		if (source->segment >= 0)
		{
			to->segment = segment_buffer(source->segment);
			to->offset = source->offset;
		}
		else
		{
			to->data = host_pointer(source->data);
			to->bytes = source->bytes;
		}
	}
	memcpy(stream.constant_attributes, input->attributes, sizeof(stream.constant_attributes));
}

/* a stream's vertices for the draw: the mirror's segment, or the data
copied into the ring (once a submission) */
static int source_range(struct vertex_source *source, struct xgpu_vulkan_range *range)
{
	if (source->segment)
	{
		range->buffer = xgpu_vulkan_buffer_handle(source->segment);
		range->offset = source->offset;
		return range->buffer != VK_NULL_HANDLE;
	}
	if (source->uploaded != xgpu_vulkan_serial())
	{
		void *to;

		if (!source->data || !source->bytes)
			return 0;
		to = xgpu_vulkan_upload(source->bytes, 16, &source->range);
		if (!to)
			return 0;
		memcpy(to, source->data, source->bytes);
		source->uploaded = xgpu_vulkan_serial();
	}
	*range = source->range;
	return 1;
}

static uint8_t primitive_class(uint32_t primitive)
{
	return primitive == _primitive_points ? XGPU_VULKAN_CLASS_POINTS : primitive <= _primitive_line_strip ?
		XGPU_VULKAN_CLASS_LINES : XGPU_VULKAN_CLASS_TRIANGLES;
}

static uint8_t primitive_topology(uint32_t primitive)
{
	switch (primitive)
	{
	case _primitive_points: return XGPU_VULKAN_TOPOLOGY_POINTS;
	case _primitive_lines: case _primitive_line_loop: return XGPU_VULKAN_TOPOLOGY_LINES;
	case _primitive_line_strip: return XGPU_VULKAN_TOPOLOGY_LINE_STRIP;
	case _primitive_triangle_strip: return XGPU_VULKAN_TOPOLOGY_TRIANGLE_STRIP;
	case _primitive_triangle_fan: return XGPU_VULKAN_TOPOLOGY_TRIANGLE_FAN;
	default: return XGPU_VULKAN_TOPOLOGY_TRIANGLES;
	}
}

/* a draw's vertices and indices: the layout's attributes and the streams'
strides, the streams (bits of present), the inputs' values; count vertices,
or indices (16-bit: in host memory, copied into the ring, or in a buffer
range already, with their bytes for a loop's) */
struct draw_input
{
	const struct xgpu_vulkan_attribute *attributes;
	const uint16_t *strides;
	uint32_t present;
	struct vertex_source *sources;
	const float (*values)[4];
	uint16_t integers;
	uint32_t primitive, count;
	const uint16_t *indices;
	struct xgpu_vulkan_range index_range;
	int32_t vertex_offset;
};

/* the draw's indices in the ring: as they are, or a line loop made a list
of lines, which Vulkan has not */
static int indices_upload(const struct draw_input *input, struct xgpu_vulkan_draw *draw)
{
	uint16_t *to;
	uint32_t index, count = input->count;

	if (input->primitive == _primitive_line_loop)
	{
		if (count < 2 || count > 65536)
			return 0;
		to = xgpu_vulkan_upload(count * 2 * sizeof(*to), 16, &draw->indices);
		if (!to)
			return 0;
		for (index = 0; index < count; index++)
		{
			to[index * 2] = input->indices ? input->indices[index] : (uint16_t)index;
			to[index * 2 + 1] = input->indices ? input->indices[(index + 1) % count] : (uint16_t)((index + 1) % count);
		}
		draw->count = count * 2;
		return 1;
	}
	if (input->index_range.buffer)
	{
		draw->indices = input->index_range;
		return 1;
	}
	if (!input->indices)
		return 1;
	to = xgpu_vulkan_upload(count * 2, 16, &draw->indices);
	if (!to)
		return 0;
	memcpy(to, input->indices, count * 2);
	return 1;
}

/* A map's enumeration should exercise every native pipeline before play.
Keep first-use misses visible, including the key and layout that need it. */
static void pipeline_missed(const struct xgpu_vulkan_pipeline_description *pipeline, uint64_t vertex_program)
{
	const struct xgpu_stream_pipeline_key *key = &stream.definition->key;

	stream.pipeline_misses++;
	host_logf(HOST_LOG_WARN, "vulkan: pipeline missed map preparation: vertex %08x%08x, "
		"views %u/%u, layout %08x/%08x, key %08x (textures %u, coverage %u, threshold %u), total %u",
		(unsigned)(vertex_program >> 32), (unsigned)vertex_program, (unsigned)key->views,
		(unsigned)pipeline->view_mask, bytes_hash(pipeline->attributes, sizeof(pipeline->attributes)),
		bytes_hash(pipeline->strides, sizeof(pipeline->strides)), bytes_hash(key, sizeof(*key)),
		(unsigned)key->pixel.texture_modes, (unsigned)key->pixel.coverage_alpha,
		(unsigned)key->pixel.point_threshold, stream.pipeline_misses);
}

/* A native key can depend on backend state absent from the manifest.
Detailed traces retain every newly compiled description for comparison
with a later miss in this process; shader handles are process-local. */
static void pipeline_description_trace(const struct xgpu_vulkan_pipeline_description *pipeline,
	uint64_t vertex_program, const struct variant *variant)
{
	static const char hex[] = "0123456789abcdef";
	const unsigned char *bytes = (const unsigned char *)pipeline;
	char description[sizeof(*pipeline) * 2 + 1];
	size_t index;

	if (!stream.trace_pipeline_descriptions)
		return;
	for (index = 0; index < sizeof(*pipeline); index++)
	{
		description[index * 2] = hex[bytes[index] >> 4];
		description[index * 2 + 1] = hex[bytes[index] & 15];
	}
	description[sizeof(*pipeline) * 2] = 0;
	host_logf(HOST_LOG_INFO, "vulkan: pipeline description: preparing=%d vertex=%08x%08x key=%08x "
		"integers=%x foveated=%u border=%u,%u,%u,%u native=%s", stream.preparing,
		(unsigned)(vertex_program >> 32), (unsigned)vertex_program,
		bytes_hash(&stream.definition->key, sizeof(stream.definition->key)),
		(unsigned)variant->integer_attributes, (unsigned)variant->foveated,
		(unsigned)variant->border[0], (unsigned)variant->border[1], (unsigned)variant->border[2],
		(unsigned)variant->border[3], description);
}

/* a VR effect's textures (xgpu_spirv.h): the stages' and past them the
scene's depth, the views' layers of the depth target drawn into (in a pass
that writes none of it: returns 1, the draw's reads_depth; or a multisampled
target's, resolved), and its bitmap's thickness (the texture cache's, baked
the first time: xgpu_vulkan_effects.h). Before the draw's uploads: the
resolve and the bake are ordered among the stream's work. */
static int effect_textures(struct xgpu_vulkan_texture textures[4])
{
	struct stream_target *depth = target_get(stream.depth);
	struct xgpu_vulkan_sampler_description sampler;
	int reads_depth = 0;

	memcpy(textures, stream.textures, sizeof(stream.textures));
	memset(&sampler, 0, sizeof(sampler));
	sampler.minify = sampler.magnify = XGPU_VULKAN_FILTER_POINT;
	sampler.mip = XGPU_VULKAN_MIP_NONE;
	sampler.address[0] = sampler.address[1] = sampler.address[2] = XGPU_VULKAN_ADDRESS_CLAMP;
	sampler.anisotropy = 1;
	textures[XGPU_SPIRV_EFFECT_DEPTH_BINDING].kind = XGPU_VULKAN_IMAGE_2D_ARRAY;
	textures[XGPU_SPIRV_EFFECT_DEPTH_BINDING].sampler = xgpu_vulkan_sampler(&sampler);
	textures[XGPU_SPIRV_EFFECT_DEPTH_BINDING].image = NULL;
	if (depth && depth->depth)
	{
		if (depth->multisampled && stream.samples)
			target_resolve(depth);
		else
			reads_depth = 1;
		textures[XGPU_SPIRV_EFFECT_DEPTH_BINDING].image = depth->image;
	}
	sampler.minify = sampler.magnify = XGPU_VULKAN_FILTER_LINEAR;
	textures[XGPU_SPIRV_EFFECT_THICKNESS_BINDING].kind = XGPU_VULKAN_IMAGE_2D;
	textures[XGPU_SPIRV_EFFECT_THICKNESS_BINDING].sampler = xgpu_vulkan_sampler(&sampler);
	if (!stream.thickness_known || stream.thickness_serial != stream.textures_serial ||
		stream.thickness_epoch != xgpu_vulkan_textures_thickness_epoch() ||
		memcmp(&stream.thickness_stage, &stream.stages[0].stage, sizeof(stream.thickness_stage)))
	{
		stream.thickness = xgpu_vulkan_textures_thickness(&stream.stages[0].stage);
		stream.thickness_stage = stream.stages[0].stage;
		stream.thickness_serial = stream.textures_serial;
		/* (after the lookup, which may bake again) */
		stream.thickness_epoch = xgpu_vulkan_textures_thickness_epoch();
		stream.thickness_known = 1;
	}
	textures[XGPU_SPIRV_EFFECT_THICKNESS_BINDING].image = stream.thickness;
	return reads_depth;
}

/* a draw of the current pipeline, made and drawn: its shaders for the
draw's variant, its pipeline for the vertex layout, its textures and
uniforms, its vertices and indices; nothing if it cannot be drawn */
static void draw_input(const struct draw_input *input)
{
	struct xgpu_vulkan_pipeline_description pipeline = stream.pipeline;
	struct xgpu_vulkan_draw draw;
	struct xgpu_vulkan_texture effect_draw_textures[4];
	struct stream_pipeline *entry;
	struct variant variant;
	uint32_t index, attempt;
	double traced;

	if (!stream.pipeline_id || !stream.definition || !input->count)
		return;
	entry = &stream.pipelines[stream.pipeline_id];
	variant_current(&stream.definition->key, &variant);
	variant.integer_attributes = stream.definition->key.immediate ? 0 : input->integers;
	if (!pipeline_shaders(entry, stream.definition, &variant))
		return;
	pipeline.vertex_shader = entry->vertex_shader;
	pipeline.pixel_shader = entry->pixel_shader;
	pipeline.primitive_class = primitive_class(input->primitive);
	memset(&draw, 0, sizeof(draw));
	for (index = 0; index < 16; index++)
	{
		const struct xgpu_vulkan_attribute *attribute = &input->attributes[index];

		if (!(entry->inputs & (1u << index)))
			continue;
		if (attribute->format != VK_FORMAT_UNDEFINED)
		{
			pipeline.attributes[index] = *attribute;
			pipeline.strides[attribute->binding] = input->strides[attribute->binding];
			draw.bindings |= 1u << attribute->binding;
		}
		else
		{
			/* (the input's value, every vertex the same: a NORMPACKED3
			input's, its value's bits) */
			pipeline.attributes[index].format = (stream.definition->key.packed_attributes & (1u << index)) &&
				!stream.definition->key.immediate ? VK_FORMAT_R32_UINT : VK_FORMAT_R32G32B32A32_SFLOAT;
			pipeline.attributes[index].binding = XGPU_VULKAN_BINDING_CONSTANT;
			pipeline.attributes[index].offset = (uint16_t)(index * 16);
			pipeline.strides[XGPU_VULKAN_BINDING_CONSTANT] = 0;
			draw.bindings |= 1u << XGPU_VULKAN_BINDING_CONSTANT;
		}
	}
	/* (a binding the pipeline reads that has no stream: not drawn) */
	for (index = 0; index < 16; index++)
	{
		if ((draw.bindings & (1u << index)) && !(input->present & (1u << index)))
			return;
	}
	pipeline.view_mask = stream.views > 1 ? (uint8_t)((1u << stream.views) - 1) : 0;
	if (target_image(stream.color))
		pipeline.color_format = xgpu_vulkan_image_description(target_image(stream.color))->format;
	/* (display.hdr: the target's half floats read where it is drawn) */
	pipeline.color_read = pipeline.color_format == XGPU_VULKAN_FORMAT_RGBA16F && stream.capabilities.color_read &&
		XGPU_HDR_SHADER_BLENDS(stream.definition->key.pixel.hdr);
#ifdef HALO_VR
	pipeline.depth_clamp = host_vulkan_vr_sky() != 0;
#endif
	{
		uint64_t before = xgpu_vulkan_pipeline_count();
		uint64_t vertex_program = stream.definition->key.vertex_program[0] |
			((uint64_t)stream.definition->key.vertex_program[1] << 32);

		if (stream.preparing)
			(void)xgpu_vulkan_pipeline_prepare(&pipeline);
		else
			draw.pipeline = xgpu_vulkan_pipeline(&pipeline);
		if (xgpu_vulkan_pipeline_count() != before)
			pipeline_description_trace(&pipeline, vertex_program, &variant);
		if (!stream.preparing && stream.prepared && xgpu_vulkan_pipeline_count() != before)
			pipeline_missed(&pipeline, vertex_program);
	}
	if (stream.preparing)
	{
		/* Preparation needs the pipeline and resident textures. Transient
		vertex and uniform uploads cannot survive until play, and drawing
		the enumeration does not make either resource more ready. */
		traced = preparation_start();
		textures_bind();
		/* (a VR effect's bitmap's thickness baked now, as the map is
		prepared, not as it is first drawn in play) */
		if (entry->effect)
			(void)xgpu_vulkan_textures_thickness(&stream.stages[0].stage);
		preparation_end(0, traced);
		return;
	}
	if (!draw.pipeline)
		return;
	draw.topology = primitive_topology(input->primitive);
	draw.count = input->count;
	draw.vertex_offset = input->vertex_offset;
	/* everything the draw reads of the ring in the same submission: an
	upload that submits the recording to make room begins them again */
	for (attempt = 0; attempt < 3; attempt++)
	{
		uint64_t serial;

		traced = preparation_start();
		textures_bind();
		if (entry->effect)
			draw.reads_depth = (uint8_t)effect_textures(effect_draw_textures);
		preparation_end(0, traced);
		serial = xgpu_vulkan_serial();
		traced = preparation_start();
		if (!uniforms_update(entry->effect))
		{
			preparation_end(1, traced);
			continue;
		}
		preparation_end(1, traced);
		traced = preparation_start();
		for (index = 0; index < 16; index++)
		{
			if ((draw.bindings & (1u << index)) && !source_range(&input->sources[index], &draw.vertices[index]))
			{
				preparation_end(2, traced);
				return;
			}
		}
		if (draw.bindings & (1u << XGPU_VULKAN_BINDING_CONSTANT))
		{
			void *to = xgpu_vulkan_upload(16 * 16, 16, &draw.vertices[XGPU_VULKAN_BINDING_CONSTANT]);

			if (!to)
			{
				preparation_end(2, traced);
				return;
			}
			memcpy(to, input->values, 16 * 16);
		}
		if (!indices_upload(input, &draw))
		{
			preparation_end(2, traced);
			return;
		}
		preparation_end(2, traced);
		if (serial == xgpu_vulkan_serial())
			break;
	}
	if (attempt == 3)
		return;
	memcpy(draw.textures, stream.textures, sizeof(draw.textures));
	draw.constants = stream.constants_range;
	draw.uniforms = stream.uniforms_range;
	draw.dynamic = &stream.dynamic;
	draw.dynamic_lights = stream.definition->pixel_program.dynamic_lights != 0;
	draw.reads_color = pipeline.color_read;
	{
		const struct xgpu_stream_pipeline_key *key = &stream.definition->key;

		draw.writes_depth = (key->depth_test && key->depth_write && !entry->effect_tests_depth) ||
			(key->stencil_test && key->stencil_write_mask &&
			(key->stencil_fail != XGPU_STENCIL_KEEP || key->stencil_depth_fail != XGPU_STENCIL_KEEP ||
			key->stencil_pass != XGPU_STENCIL_KEEP));
	}
	if (entry->effect)
		memcpy(draw.textures, effect_draw_textures, sizeof(draw.textures));
	/* (a multisampled depth target written: resolved again for the
	effects that sample it) */
	if (draw.writes_depth && stream.samples && target_get(stream.depth) && target_get(stream.depth)->multisampled)
		target_get(stream.depth)->unresolved = 1;
	if (target_get(stream.color))
		target_get(stream.color)->written++;
	traced = preparation_start();
	xgpu_vulkan_draw(&draw);
	preparation_end(3, traced);
}

/* the draws of set_vertices's input */
static void input_streams(struct draw_input *input, uint32_t primitive, uint32_t count)
{
	memset(input, 0, sizeof(*input));
	input->attributes = stream.attributes;
	input->strides = stream.strides;
	input->present = stream.present;
	input->sources = stream.streams;
	input->values = (const float (*)[4])stream.constant_attributes;
	input->integers = stream.integer_attributes;
	input->primitive = primitive;
	input->count = count;
}

static void stream_draw(void *context, const struct xgpu_stream_draw *record)
{
	struct draw_input input;

	(void)context;
	input_streams(&input, record->primitive, record->count);
	draw_input(&input);
}

static void stream_draw_indices(void *context, const struct xgpu_stream_draw_indices *record)
{
	struct draw_input input;

	(void)context;
	input_streams(&input, record->primitive, record->count);
	input.indices = host_pointer(record->indices);
	if (!input.indices)
		return;
	draw_input(&input);
}

static void stream_draw_indexed(void *context, const struct xgpu_stream_draw_indexed *record)
{
	struct draw_input input;

	(void)context;
	input_streams(&input, record->primitive, record->count);
	if (record->segment >= 0)
	{
		struct xgpu_vulkan_buffer *buffer = segment_buffer(record->segment);

		if ((uint64_t)record->offset + record->count * 2 > MIRROR_SEGMENT_SIZE || !buffer || (record->offset & 1))
			return;
		input.index_range.buffer = xgpu_vulkan_buffer_handle(buffer);
		input.index_range.offset = record->offset;
		/* (a loop's indices, made into a list, are read from the mirror's
		bytes; others are not read) */
		if (record->primitive == _primitive_line_loop)
			input.indices = (const uint16_t *)((const unsigned char *)xgpu_vulkan_buffer_contents(buffer) +
				record->offset);
	}
	else
	{
		input.indices = host_pointer(record->indices);
		if (!input.indices)
			return;
	}
	/* (the streams start at vertex minimum) */
	input.vertex_offset = -(int32_t)record->minimum;
	draw_input(&input);
}

static void stream_draw_immediate(void *context, const struct xgpu_stream_draw_immediate *record,
	const float (*attributes)[4])
{
	struct xgpu_vulkan_attribute layout[16];
	uint16_t strides[16];
	struct vertex_source sources[16];
	struct draw_input input;
	uint32_t index, written = 0;

	(void)context;
	memset(layout, 0, sizeof(layout));
	memset(strides, 0, sizeof(strides));
	memset(sources, 0, sizeof(sources));
	/* the registers written, a vec4 each, in order; the rest from their
	values */
	for (index = 0; index < 16; index++)
	{
		if (record->written & (1u << index))
		{
			layout[index].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			layout[index].binding = 0;
			layout[index].offset = (uint16_t)(written++ * 16);
		}
	}
	if (!written || !record->count)
		return;
	strides[0] = (uint16_t)(written * 16);
	sources[0].data = host_pointer(record->vertices);
	sources[0].bytes = record->count * written * 16;
	memset(&input, 0, sizeof(input));
	input.attributes = layout;
	input.strides = strides;
	input.present = 1;
	input.sources = sources;
	input.values = attributes;
	input.primitive = record->primitive;
	if (record->indices && record->index_count)
	{
		input.indices = host_pointer(record->indices);
		input.count = record->index_count;
	}
	else
	{
		input.count = record->count;
	}
	draw_input(&input);
}

/* ---------- the enhanced dynamic lights (display.dynamic_lights; xgpu.h)

The front end works out the frame's lights and the grid's cells
(d3d8_device.c); the block and the cells are the draw set's (xgpu_vulkan.c),
written in the stream's order, and the glows are drawn here. */

#define DYNAMIC_LIGHTS_CELLS_SIZE \
	(XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * XGPU_DYNAMIC_LIGHTS_GRID_Z * 16)

_Static_assert(sizeof(struct xgpu_stream_dynamic_lights_block) == sizeof(struct xgpu_dynamic_lights_block),
	"the dynamic lights' block");

/* a glow's vertex: its light's middle and the glow's size, its color, the
corner (xgpu_spirv.h) */
struct halo_vertex
{
	float middle[4];
	float color[4];
	float corner[2];
};

static int stream_dynamic_lights_prepare(void *context)
{
	(void)context;
	/* (made with the device: xgpu_vulkan.c draw_set_make) */
	return 1;
}

static void stream_dynamic_lights_upload(void *context, const struct xgpu_stream_dynamic_lights_upload *record)
{
	const void *cells = host_pointer(record->cells);

	(void)context;
	if (!cells || record->size != DYNAMIC_LIGHTS_CELLS_SIZE)
		return;
	xgpu_vulkan_dynamic_lights_upload(&record->block, sizeof(record->block), cells);
}

/* a glow's shaders for its layers' slot (0: the screen's target, 1: the VR
mode's eyes, 2: its eyes foveated: there, the light the bolt scatters toward
each eye), made the first time; 0 if they cannot be */
static int halo_shaders(int slot)
{
	static const char *const names[3] = { "halo", "glow_view1", "glow_view1_fovea1" };
	struct xgpu_spirv_options options;
	struct xgpu_spirv_module module;
	char name[32];

	if (stream.halo_vertex[slot] && stream.halo_pixel[slot])
		return 1;
	if (stream.halo_failed[slot])
		return 0;
	stream.halo_failed[slot] = 1;
	memset(&options, 0, sizeof(options));
	options.multiview = slot ? XGPU_MULTIVIEW_EYES : XGPU_MULTIVIEW_NONE;
	options.foveated = slot == 2;
	options.effect = slot ? XGPU_SPIRV_EFFECT_SOFT : XGPU_SPIRV_EFFECT_NONE;
	options.reversed_depth = XGPU_VULKAN_REVERSED_DEPTH;
	if (xgpu_spirv_halo_vertex_shader(&options, &module))
	{
		snprintf(name, sizeof(name), "%s_vertex", names[slot]);
		shader_dump(name, &module);
		stream.halo_vertex[slot] = xgpu_vulkan_shader(module.words, module.count);
		free(module.words);
	}
	if (xgpu_spirv_halo_pixel_shader(&options, &module))
	{
		snprintf(name, sizeof(name), "%s_pixel", names[slot]);
		shader_dump(name, &module);
		stream.halo_pixel[slot] = xgpu_vulkan_shader(module.words, module.count);
		free(module.words);
	}
	if (!stream.halo_vertex[slot] || !stream.halo_pixel[slot])
	{
		host_logf(HOST_LOG_WARN, "vulkan: the dynamic lights' glows' shaders cannot be made");
		return 0;
	}
	stream.halo_failed[slot] = 0;
	return 1;
}

/* the depth buffer's value for a distance from the camera, alpha + beta /
distance (beta less than 0: nearer, less), from two points ahead; and the
softness, world units */
static void halo_depth_parameters(const struct xgpu_stream_dynamic_lights_halos *record, float parameters[4])
{
	const float (*c)[4] = (const float (*)[4])record->projection;
	float forward[3], z[2], w[2], a, b, scale_z, near_z, far_z, length;
	int point, axis;

	/* (the camera's forward: the projection's w row) */
	for (axis = 0; axis < 3; axis++)
		forward[axis] = c[3][axis];
	length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
	for (point = 0; point < 2; point++)
	{
		float t = point ? 64.0f : 1.0f;
		float p[3];

		for (axis = 0; axis < 3; axis++)
			p[axis] = record->camera[axis] + forward[axis] / (length > 0.0f ? length : 1.0f) * t;
		z[point] = c[2][0] * p[0] + c[2][1] * p[1] + c[2][2] * p[2] + c[2][3];
		w[point] = c[3][0] * p[0] + c[3][1] * p[1] + c[3][2] * p[2] + c[3][3];
	}
	b = (z[0] / w[0] - z[1] / w[1]) / (1.0f / w[0] - 1.0f / w[1]);
	a = z[0] / w[0] - b / w[0];
	scale_z = record->viewport_scale[2] != 0.0f ? record->viewport_scale[2] : 1.0f;
	near_z = stream.dynamic.depth_range[0] >= 0.0f && stream.dynamic.depth_range[0] <= 1.0f ?
		stream.dynamic.depth_range[0] : 0.0f;
	far_z = stream.dynamic.depth_range[1] >= 0.0f && stream.dynamic.depth_range[1] <= 1.0f ?
		stream.dynamic.depth_range[1] : 1.0f;
	parameters[0] = near_z + (far_z - near_z) * (a * c[4][2] + c[5][2] - record->viewport_offset[2]) / scale_z;
	parameters[1] = (far_z - near_z) * b * c[4][2] / scale_z;
	parameters[2] = record->softness;
	parameters[3] = 0.0f;
}

/* the VR mode's glows' constants and uniforms (xgpu_spirv.h): each view's
clip transform, unprojection and eye; the head's centre and the distance
ahead of it of the quad drawn with the head in a glow (three times the near
plane's), its right, up and forward (the world's projection's w row). 0 if
the views cannot be made. */
static int glow_constants(const struct xgpu_stream_dynamic_lights_halos *record, struct xgpu_spirv_uniforms *uniforms,
	float (*constants)[4])
{
	float clip[4][4][4], forward[3], length, near_point[4], ahead = 0.0f;
	const float (*unproject)[4] = (const float (*)[4])uniforms->effect_unproject[0];
	int views = effect_views(uniforms, (const float (*)[4])record->projection, record->viewport_scale,
		record->viewport_offset, record->screen_offset, uniforms->effect_unproject, uniforms->effect_eye, clip), view, row,
		axis;

	if (!views)
		return 0;
	for (view = 0; view < views; view++)
		for (row = 0; row < 4; row++)
			memcpy(constants[XGPU_SPIRV_GLOW_CLIP + view * 4 + row], clip[view][row], sizeof(clip[0][0]));
	/* the near plane's distance: the middle pixel at the least depth */
	for (axis = 0; axis < 4; axis++)
		near_point[axis] = unproject[0][axis] * ((float)stream.dynamic.viewport[0] + 0.5f * stream.dynamic.viewport[2]) +
			unproject[1][axis] * ((float)stream.dynamic.viewport[1] + 0.5f * stream.dynamic.viewport[3]) +
			unproject[2][axis] * (stream.dynamic.depth_range[0] >= 0.0f && stream.dynamic.depth_range[0] <= 1.0f ?
			stream.dynamic.depth_range[0] : 0.0f) + unproject[3][axis];
	if (near_point[3] != 0.0f)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float offset = near_point[axis] / near_point[3] - uniforms->effect_eye[0][axis];

			ahead += offset * offset;
		}
		ahead = sqrtf(ahead);
	}
	ahead = 3.0f * ahead > 0.01f ? 3.0f * ahead : 0.01f;
	memcpy(constants[XGPU_SPIRV_GLOW_HEAD], record->camera, sizeof(record->camera));
	constants[XGPU_SPIRV_GLOW_HEAD][3] = ahead;
	memcpy(constants[XGPU_SPIRV_GLOW_AXES], record->camera_right, sizeof(record->camera_right));
	memcpy(constants[XGPU_SPIRV_GLOW_AXES + 1], record->camera_up, sizeof(record->camera_up));
	for (axis = 0; axis < 3; axis++)
		forward[axis] = record->projection[3][axis];
	length = sqrtf(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
	for (axis = 0; axis < 3; axis++)
		constants[XGPU_SPIRV_GLOW_AXES + 2][axis] = length > 0.0f ? forward[axis] / length : 0.0f;
	return 1;
}

/* the glows of the frame's bolts' lights, into the 3D view, after its
transparent geometry: added to what is there (not its alpha), into the
target without its depth, which the shader samples (resolved, where it is
multisampled), in each of the target's layers (the VR mode's eyes and
insets, in one multiview pass). In the VR mode's eyes the depth stays bound,
untested and unwritten, so that the glows are drawn in the pass the effects
sample the depth in (xgpu_vulkan_draw's reads_depth), where they were drawn
last, without a pass of their own (on a tiled GPU, the target's pixels
written out and read back) */
static void stream_dynamic_lights_halos(void *context, const struct xgpu_stream_dynamic_lights_halos *record)
{
	static const float corners[6][2] =
	{
		{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f },
	};
	struct stream_target *color = target_get(record->color), *depth = target_get(record->depth);
	struct xgpu_vulkan_pipeline_description pipeline;
	struct xgpu_vulkan_sampler_description sampler;
	struct xgpu_vulkan_dynamic_state dynamic;
	struct xgpu_vulkan_draw draw;
	uint32_t layers = record->layers > 1 ? (uint32_t)record->layers : 1, attempt;
	int slot = layers == 1 ? 0 : layers == 2 ? 1 : 2, halo, corner, merged;
	uint64_t before;

	(void)context;
	if (!color || !depth || color->depth || !depth->depth || record->count <= 0 ||
		(layers != 1 && layers != 2 && layers != 4) || color->layers != layers || depth->layers != layers ||
		!halo_shaders(slot))
		return;
	memset(&pipeline, 0, sizeof(pipeline));
	pipeline.vertex_shader = stream.halo_vertex[slot];
	pipeline.pixel_shader = stream.halo_pixel[slot];
	pipeline.attributes[XGPU_SPIRV_HALO_MIDDLE].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	pipeline.attributes[XGPU_SPIRV_HALO_MIDDLE].offset = (uint16_t)offsetof(struct halo_vertex, middle);
	pipeline.attributes[XGPU_SPIRV_HALO_COLOR].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	pipeline.attributes[XGPU_SPIRV_HALO_COLOR].offset = (uint16_t)offsetof(struct halo_vertex, color);
	pipeline.attributes[XGPU_SPIRV_HALO_CORNER].format = VK_FORMAT_R32G32_SFLOAT;
	pipeline.attributes[XGPU_SPIRV_HALO_CORNER].offset = (uint16_t)offsetof(struct halo_vertex, corner);
	pipeline.strides[0] = (uint16_t)sizeof(struct halo_vertex);
	pipeline.primitive_class = XGPU_VULKAN_CLASS_TRIANGLES;
	pipeline.blend = 1;
	pipeline.blend_source = pipeline.blend_destination = XGPU_BLEND_ONE;
	pipeline.blend_operation = XGPU_BLEND_ADD;
	pipeline.color_mask = 7;
	pipeline.color_format = xgpu_vulkan_image_description(color->image)->format;
	pipeline.samples = (uint8_t)(color->multisampled ? color->samples : 1);
	pipeline.view_mask = layers > 1 ? (uint8_t)((1u << layers) - 1) : 0;
	/* (the VR mode's eyes: drawn with the depth attachment bound, untested
	and unwritten, which the shader samples, as the effects draw: in their
	pass where they were the last drawn, not one of its own) */
	merged = layers > 1 && !color->multisampled && !depth->multisampled;
	if (merged)
		pipeline.depth_format = XGPU_VULKAN_FORMAT_D24S8;
	before = xgpu_vulkan_pipeline_count();
	if (stream.preparing)
	{
		/* (made as the map loads, as the game's pipelines are) */
		(void)xgpu_vulkan_pipeline_prepare(&pipeline);
		return;
	}
	memset(&draw, 0, sizeof(draw));
	draw.pipeline = xgpu_vulkan_pipeline(&pipeline);
	if (stream.prepared && xgpu_vulkan_pipeline_count() != before)
	{
		stream.pipeline_misses++;
		host_logf(HOST_LOG_WARN, "vulkan: pipeline missed map preparation: the dynamic lights' glows, views %u, "
			"samples %u, total %u", (unsigned)layers, (unsigned)pipeline.samples, stream.pipeline_misses);
	}
	if (!draw.pipeline)
		return;
	/* the depth the scene was drawn with, in its texture (its own sampler:
	no filter) */
	target_resolve(depth);
	memset(&sampler, 0, sizeof(sampler));
	sampler.minify = sampler.magnify = XGPU_VULKAN_FILTER_POINT;
	sampler.mip = XGPU_VULKAN_MIP_NONE;
	sampler.address[0] = sampler.address[1] = sampler.address[2] = XGPU_VULKAN_ADDRESS_CLAMP;
	sampler.anisotropy = 1;
	draw.textures[0].image = depth->image;
	draw.textures[0].kind = layers > 1 ? XGPU_VULKAN_IMAGE_2D_ARRAY : XGPU_VULKAN_IMAGE_2D;
	draw.textures[0].sampler = xgpu_vulkan_sampler(&sampler);
	/* (the viewport and depth range the 3D view was drawn with, and no
	scissor) */
	dynamic = stream.dynamic;
	memset(dynamic.scissor, 0, sizeof(dynamic.scissor));
	draw.dynamic = &dynamic;
	draw.topology = XGPU_VULKAN_TOPOLOGY_TRIANGLES;
	draw.count = (uint32_t)record->count * 6;
	draw.bindings = 1;
	/* the glows' corners, the world's projection, the camera's axes and the
	depth's parameters, and the uniforms, all in the same submission */
	for (attempt = 0; attempt < 3; attempt++)
	{
		uint64_t serial = xgpu_vulkan_serial();
		struct halo_vertex *vertices = xgpu_vulkan_upload(draw.count * (uint32_t)sizeof(*vertices), 16, &draw.vertices[0]);
		float (*constants)[4] = xgpu_vulkan_upload(CONSTANTS_RANGE, 256, &draw.constants);
		struct xgpu_spirv_uniforms *uniforms = xgpu_vulkan_upload(UNIFORMS_RANGE, 256, &draw.uniforms);

		if (!vertices || !constants || !uniforms)
			return;
		if (serial != xgpu_vulkan_serial())
			continue;
		for (halo = 0; halo < record->count; halo++)
		{
			for (corner = 0; corner < 6; corner++)
			{
				struct halo_vertex *vertex = &vertices[halo * 6 + corner];

				memcpy(vertex->middle, record->halos[halo * 2], sizeof(vertex->middle));
				memcpy(vertex->color, record->halos[halo * 2 + 1], sizeof(vertex->color));
				vertex->corner[0] = corners[corner][0];
				vertex->corner[1] = corners[corner][1];
			}
		}
		memset(constants, 0, CONSTANTS_RANGE);
		memcpy(constants[XGPU_SPIRV_HALO_PROJECTION], record->projection, sizeof(record->projection));
		memcpy(constants[XGPU_SPIRV_HALO_CAMERA_AXES], record->camera_right, sizeof(record->camera_right));
		memcpy(constants[XGPU_SPIRV_HALO_CAMERA_AXES + 1], record->camera_up, sizeof(record->camera_up));
		halo_depth_parameters(record, constants[XGPU_SPIRV_HALO_DEPTH]);
		memset(uniforms, 0, sizeof(*uniforms));
		memcpy(uniforms->viewport_scale, record->viewport_scale, sizeof(uniforms->viewport_scale));
		memcpy(uniforms->viewport_offset, record->viewport_offset, sizeof(uniforms->viewport_offset));
		uniforms->screen_offset = record->screen_offset;
#ifdef HALO_VR
		host_vulkan_vr_uniforms(uniforms, dynamic.viewport);
#endif
		if (layers > 1 && !glow_constants(record, uniforms, constants))
			return;
		break;
	}
	if (attempt == 3)
		return;
	if (color->multisampled)
		color->unresolved = 1;
	color->written++;
	xgpu_vulkan_set_target_views(color->multisampled ? color->multisampled : color->image, merged ? depth->image : NULL,
		0, layers);
	draw.reads_depth = (uint8_t)merged;
	xgpu_vulkan_draw(&draw);
	/* (the stream's targets again, for what follows) */
	targets_bind();
}

/* ---------- anti-aliasing (display.anti_aliasing; xgpu_vulkan_post.c) */

static void stream_anti_aliasing_prepare(void *context, uint32_t smaa)
{
	(void)context;
	/* (made as the setting is chosen: the front end chooses it between
	frames; queued as a map is prepared; logged if they cannot be; for
	display.hdr's half floats too, while the frames are HDR) */
	(void)xgpu_vulkan_post_prepare(smaa != 0, XGPU_VULKAN_FORMAT_BGRA8, stream.preparing);
	if (stream.hdr_active)
		(void)xgpu_vulkan_post_prepare(smaa != 0, XGPU_VULKAN_FORMAT_RGBA16F, stream.preparing);
}

/* FXAA or SMAA over a window's 3D view, before the HUD and menus are drawn
over it: the target's texture, its first layer (the front end no longer
multisamples the target, from just before the record: a target still
multisampled would have its pixels resolved over the passes') */
static void stream_anti_alias(void *context, const struct xgpu_stream_anti_alias *record)
{
	struct stream_target *target = target_get(record->target);

	(void)context;
	if (!target || target->depth)
		return;
	if (stream.preparing)
	{
		/* (made as the map loads, as the game's pipelines are) */
		(void)xgpu_vulkan_post_prepare(record->smaa != 0, target->format, 1);
		return;
	}
	if (target->multisampled)
		return;
	target->written++;
	xgpu_vulkan_post_anti_alias(record->smaa != 0, target->image, 0, record->corners);
	/* (the stream's targets again, for what follows) */
	targets_bind();
}

/* ---------- display.hdr (port/linux/src/xgpu_hdr.h)

The front end's state, for the tone map and the core's encoding and
swapchain; and the tone map of each window's 3D view, a draw in the pass
that drew it (its multisampled attachments where it is drawn into them)
before the anti-aliasing and the HUD. */

static void stream_picture_top(void *context, uint32_t top)
{
	(void)context;
	xgpu_vulkan_picture_top(top ? (int)(top - 1) : -1);
}

/* display.gamma: the core's present curves an SDR frame by it */
static void stream_gamma(void *context, uint32_t gamma)
{
	float value;

	(void)context;
	memcpy(&value, &gamma, sizeof(value));
	xgpu_vulkan_gamma(value);
}

static void stream_hdr(void *context, const struct xgpu_stream_hdr *record)
{
	(void)context;
	stream.hdr_active = record->active != 0 && stream.capabilities.color_read;
	xgpu_vulkan_hdr_state(stream.hdr_active, (int)record->mode, (int)record->paper_white_nits,
		(int)record->paper_white, (int)record->ui_nits);
	/* (the tone map's pipeline for the back buffer's pass, made as HDR is
	chosen, not in the middle of a frame) */
	if (stream.hdr_active)
		(void)xgpu_vulkan_post_prepare_hdr(XGPU_VULKAN_FORMAT_D24S8, 0, stream.preparing);
}

static void stream_tone_map(void *context, const struct xgpu_stream_tone_map *record)
{
	struct stream_target *target = target_get(record->target);
	struct stream_target *depth = stream.color == record->target ? target_get(stream.depth) : NULL;
	struct xgpu_vulkan_image *color_image, *depth_image;
	struct xgpu_hdr_frame frame;
	float hdr[4];

	(void)context;
	if (!target || target->depth || target->format != XGPU_VULKAN_FORMAT_RGBA16F || target->layers > 1)
		return;
	/* (the attachments the view was drawn into: multisampled where they
	are, and both of a number of samples) */
	color_image = stream.samples && target->multisampled ? target->multisampled : target->image;
	depth_image = depth ? (stream.samples && depth->multisampled ? depth->multisampled : depth->image) : NULL;
	if (depth_image && xgpu_vulkan_image_description(depth_image)->samples !=
		xgpu_vulkan_image_description(color_image)->samples)
		depth_image = NULL;
	if (stream.preparing)
	{
		(void)xgpu_vulkan_post_prepare_hdr(depth_image ? XGPU_VULKAN_FORMAT_D24S8 : XGPU_VULKAN_FORMAT_NONE,
			xgpu_vulkan_image_description(color_image)->samples, 1);
		return;
	}
	xgpu_vulkan_hdr_frame(&frame);
	hdr[0] = (float)frame.mode;
	hdr[1] = frame.peak;
	/* (the view's light over the UI's white) */
	hdr[2] = frame.ui;
	hdr[3] = 0.0f;
	target->written++;
	if (color_image == target->multisampled)
		target->unresolved = 1;
	xgpu_vulkan_post_tone_map(color_image, depth_image, record->corners, hdr);
	/* (the stream's targets again: the same, so that the pass goes on) */
	targets_bind();
}

/* display.ambient_occlusion over a window's 3D view, once its opaque
surfaces have drawn their light (xgpu_vulkan_post.c), with the depth target
drawn with it: its stencil marks the pixels left, and its depth (its first
sample's, resolved where it has more) gives the occlusion. Or the occlusion
last laid drawn in the view's place (debug.ambient_occlusion_view) */
static void stream_ambient_occlusion(void *context, const struct xgpu_stream_ambient_occlusion *record)
{
	struct stream_target *target = target_get(record->target);
	struct stream_target *depth = stream.color == record->target ? target_get(stream.depth) : NULL;
	struct xgpu_vulkan_image *color_image, *depth_image;

	(void)context;
	if (!target || target->depth || target->layers > 1 || !depth || !depth->depth ||
		record->quality < XGPU_AMBIENT_OCCLUSION_SUBTLE || record->quality > XGPU_AMBIENT_OCCLUSION_SHOW)
		return;
	/* (the attachments the view is drawn into: multisampled where they
	are, and both of a number of samples) */
	color_image = stream.samples && target->multisampled ? target->multisampled : target->image;
	depth_image = stream.samples && depth->multisampled ? depth->multisampled : depth->image;
	if (xgpu_vulkan_image_description(depth_image)->samples != xgpu_vulkan_image_description(color_image)->samples)
		return;
	if (stream.preparing)
	{
		(void)xgpu_vulkan_post_ambient_occlusion_prepare(xgpu_vulkan_image_description(color_image)->format,
			xgpu_vulkan_image_description(color_image)->samples, 1);
		return;
	}
	target->written++;
	if (color_image == target->multisampled)
		target->unresolved = 1;
	if (record->quality == XGPU_AMBIENT_OCCLUSION_SHOW)
		xgpu_vulkan_post_ambient_occlusion_show(color_image, record->corners);
	else
	{
		/* (the first sample's depth, to find the occlusion from) */
		if (depth_image == depth->multisampled)
			xgpu_vulkan_image_resolve(depth->multisampled, depth->image);
		xgpu_vulkan_post_ambient_occlusion(color_image, depth_image, depth->image, record->corners, (int)record->quality,
			record->camera);
	}
	/* (the stream's targets again: the same, so that the pass goes on) */
	targets_bind();
}

/* ---------- visibility tests, frames */

static void stream_visibility_begin(void *context)
{
	(void)context;
	xgpu_vulkan_visibility_begin();
	stream.uniforms_dirty = 1;
}

static void stream_visibility_end(void *context, const struct xgpu_stream_visibility_end *record)
{
	(void)context;
	/* (the count written for the game is of its pixels: divided by the
	samples one of them is) */
#ifdef HALO_VR
	if (host_vulkan_vr_active() && stream.capabilities.fragment_stores)
		xgpu_vulkan_visibility_end(record->slot, record->area * (float)(record->views > 1 ? record->views : 1));
	else
#endif
		xgpu_vulkan_visibility_end(record->slot, record->area * (float)(record->samples > 1 ? record->samples : 1));
	stream.uniforms_dirty = 1;
}

static void stream_present(void *context, uint32_t back_buffer)
{
	(void)context;
	stream.back_buffer = host_render_stream_loading_holds() ? 0 : back_buffer;
}

static void stream_frame_end(void *context)
{
	struct stream_target *back_buffer = target_get(stream.back_buffer);
	int present_window = !host_render_stream_loading_holds();

	(void)context;
	target_resolve(back_buffer);
#ifdef HALO_VR
	/* (the headset shows the frames: the window, hidden, is not presented
	to, which could wait for a display that never takes a frame) */
	if (host_vulkan_vr_active())
	{
		back_buffer = NULL;
		present_window = 0;
	}
#endif
	xgpu_vulkan_frame_end(back_buffer ? back_buffer->image : NULL, present_window);
	stream.back_buffer = 0;
	xgpu_vulkan_textures_frame_end();
	if (stream.statistics)
	{
		static uint32_t frames;

		if (++frames % 600 == 0)
		{
			struct xgpu_vulkan_statistics statistics;
			struct xgpu_vulkan_texture_statistics textures;

			xgpu_vulkan_statistics(&statistics);
			xgpu_vulkan_textures_statistics(&textures);
			host_logf(HOST_LOG_INFO, "vulkan: %u frames: %u submissions, %u passes, %u draws, %u clears (%u drawn), "
				"%u attachment loads, %u attachment clears, %u texture sets written, %u reused, %u pipelines made "
				"(%.1f ms), %u uploads ordered, %u early, %llu ring bytes; %u stores discarded (%u at frames' ends), "
				"%u resolves in passes, %u skipped; %u passes continued, %u put aside, %u buffers renamed; "
				"textures %u made, %u remade, %u images, %u effects' thicknesses baked (%.1f ms); "
				"GPU %.3f ms a frame, %.3f ms between frames",
				statistics.frames, statistics.submissions, statistics.passes, statistics.draws, statistics.clears,
				statistics.clears_as_draws, statistics.attachment_loads, statistics.attachment_clears,
				statistics.texture_sets_written,
				statistics.texture_sets_reused,
				statistics.pipelines_made, statistics.pipeline_seconds * 1000.0, statistics.uploads_ordered,
				statistics.uploads_early, (unsigned long long)statistics.ring_bytes, statistics.stores_discarded,
				statistics.frame_end_discards, statistics.resolves_in_pass, statistics.resolves_skipped,
				statistics.passes_continued, statistics.passes_aside, statistics.buffers_renamed, textures.made, textures.remade,
				textures.images, textures.thickness, textures.thickness_seconds * 1000.0,
				statistics.gpu_frames ? statistics.gpu_milliseconds / statistics.gpu_frames : 0.0,
				statistics.frames_timed ? statistics.frame_seconds * 1000.0 / statistics.frames_timed : 0.0);
		}
	}
	/* (each stage's texture looked for at least once a frame, so that the
	cache sees it used and keeps it: textures_bind) */
	stream.textures_dirty = TEXTURES_ALL;
}

static void stream_open(void *context, void *ring, uint32_t ring_size, volatile uint32_t *visibility)
{
	(void)context;
	stream.ring = ring;
	stream.ring_size = ring_size;
	xgpu_vulkan_set_visibility_output(visibility);
}

static uint64_t stream_fence(void *context)
{
	(void)context;
	return xgpu_vulkan_fence();
}

static void stream_wait(void *context, uint64_t fence)
{
	(void)context;
	xgpu_vulkan_wait(fence);
}

static void stream_initialize(void *context, struct xgpu_stream_capabilities *capabilities)
{
	(void)context;
	xgpu_vulkan_capabilities(&stream.capabilities);
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->largest_target = (int32_t)stream.capabilities.largest_target;
	capabilities->most_samples = (int32_t)stream.capabilities.samples;
	capabilities->most_anisotropy = (int32_t)stream.capabilities.anisotropy;
	/* (visibility counts from occlusion queries, not the pixel shaders) */
	capabilities->sample_counting = 0;
	capabilities->bgra_vertex_colors = 1;
	capabilities->base_vertex = 1;
	capabilities->sample_mask = 1;
	capabilities->mixed_constant_blend = 1;
	/* (the lights' block and cells are made with the device) */
	capabilities->dynamic_lights = 1;
	capabilities->dynamic_lights_preparable = 1;
	/* (display.hdr: the half floats, the draws' tags blended in their
	shaders, reading the pixels they draw over, the tone map in place:
	where the device reads its colour attachments, the desktop's) */
	capabilities->hdr = stream.capabilities.color_read != 0;
}

static void stream_prepare(void *context)
{
	const char *detail = getenv("HALO_LOAD_TRACE_DETAIL");

	(void)context;
	stream.trace_pipeline_descriptions = host_load_trace_enabled() && detail && *detail && strcmp(detail, "0");
	xgpu_vulkan_preparation_service(host_render_stream_loading_service);
	stream.preparing = 1;
	stream.prepare_pipelines = xgpu_vulkan_pipeline_count();
	stream.trace_preparing = host_load_trace_enabled();
	memset(stream.trace_seconds, 0, sizeof(stream.trace_seconds));
	memset(stream.trace_counts, 0, sizeof(stream.trace_counts));
	xgpu_vulkan_statistics_peek(&stream.trace_statistics);
	xgpu_vulkan_textures_statistics_peek(&stream.trace_textures);
}

static uint32_t stream_prepared(void *context)
{
	double started = host_load_trace_clock(CLOCK_MONOTONIC), finished;

	(void)context;
	xgpu_vulkan_preparation_service(NULL);
	finished = host_load_trace_clock(CLOCK_MONOTONIC);
	if (stream.trace_preparing)
	{
		struct xgpu_vulkan_statistics now;
		struct xgpu_vulkan_texture_statistics textures;
		int stage;
		static const char *names[4] = { "textures", "uniforms", "vertices_indices", "draw" };

		xgpu_vulkan_statistics_peek(&now);
		xgpu_vulkan_textures_statistics_peek(&textures);
		host_load_trace("prepare.vulkan.work", "draws=%u passes=%u submissions=%u ring_bytes=%llu waits=%llu wait_seconds=%.6f",
			now.draws - stream.trace_statistics.draws, now.passes - stream.trace_statistics.passes,
			now.submissions - stream.trace_statistics.submissions,
			(unsigned long long)(now.ring_bytes - stream.trace_statistics.ring_bytes),
			(unsigned long long)(now.waits - stream.trace_statistics.waits), now.wait_seconds - stream.trace_statistics.wait_seconds);
		host_load_trace("prepare.vulkan.textures", "made=%u remade=%u images=%u bytes=%llu thickness=%u "
			"thickness_seconds=%.6f",
			textures.made - stream.trace_textures.made, textures.remade - stream.trace_textures.remade,
			textures.images - stream.trace_textures.images, (unsigned long long)(textures.bytes - stream.trace_textures.bytes),
			textures.thickness - stream.trace_textures.thickness,
			textures.thickness_seconds - stream.trace_textures.thickness_seconds);
		for (stage = 0; stage < 4; stage++)
			host_load_trace("prepare.vulkan.stage", "stage=%s count=%llu seconds=%.6f", names[stage],
				(unsigned long long)stream.trace_counts[stage], stream.trace_seconds[stage]);
		stream.trace_preparing = 0;
	}
	/* Clearing the preparation service drains queued native descriptions.
	The reply follows compilation of every requested layout and target. */
	host_logf(HOST_LOG_INFO, "vulkan: map preparation made %llu pipelines (%llu total)",
		(unsigned long long)(xgpu_vulkan_pipeline_count() - stream.prepare_pipelines),
		(unsigned long long)xgpu_vulkan_pipeline_count());
	stream.preparing = 0;
	stream.prepared = 1;
	/* (the pipelines just made kept, where they are new: Android seldom
	lets the app exit cleanly, which also saves them) */
	if (xgpu_vulkan_pipeline_count() != stream.prepare_pipelines)
		xgpu_vulkan_pipeline_cache_save();
	return started >= 0.0 && finished >= started ? (uint32_t)((finished - started) * 1000.0) : 0;
}

void xgpu_vulkan_stream_debug(int statistics, const char *shader_folder)
{
	stream.statistics = statistics != 0;
	stream.shader_folder = shader_folder && *shader_folder ? shader_folder : NULL;
}

const struct host_render_backend *xgpu_vulkan_stream_backend(void)
{
	static struct host_render_backend backend;

	if (!backend.open)
	{
		xgpu_vulkan_capabilities(&stream.capabilities);
		backend.open = stream_open;
		backend.fence = stream_fence;
		backend.wait = stream_wait;
		backend.initialize = stream_initialize;
		backend.target_create = stream_target_create;
		backend.target_delete = stream_target_delete;
		backend.target_multisample = stream_target_multisample;
		backend.target_resolve = stream_target_resolve;
		backend.set_targets = stream_set_targets;
		backend.clear = stream_clear;
		backend.clear_target = stream_clear_target;
		backend.read_target = stream_read_target;
		backend.set_textures = stream_set_textures;
		backend.set_constants = stream_set_constants;
		backend.vertex_program = stream_vertex_program;
		backend.vertex_lighting = stream_vertex_lighting;
		backend.pipeline_define = stream_pipeline_define;
		backend.pipeline = stream_pipeline;
		backend.draw_state = stream_draw_state;
		backend.mirror_write = stream_mirror_write;
		backend.set_vertices = stream_set_vertices;
		backend.draw = stream_draw;
		backend.draw_indices = stream_draw_indices;
		backend.draw_indexed = stream_draw_indexed;
		backend.draw_immediate = stream_draw_immediate;
		backend.visibility_begin = stream_visibility_begin;
		backend.visibility_end = stream_visibility_end;
		backend.present = stream_present;
		backend.loading = xgpu_vulkan_loading;
		backend.loading_owned = xgpu_vulkan_loading_owned;
		backend.loading_art = xgpu_vulkan_loading_art;
		backend.loading_scene = xgpu_vulkan_loading_scene;
		backend.loading_progress = xgpu_vulkan_loading_progress;
		backend.frame_end = stream_frame_end;
		backend.prepare = stream_prepare;
		backend.prepared = stream_prepared;
		backend.dynamic_lights_prepare = stream_dynamic_lights_prepare;
		backend.dynamic_lights_upload = stream_dynamic_lights_upload;
		backend.dynamic_lights_halos = stream_dynamic_lights_halos;
		backend.anti_aliasing_prepare = stream_anti_aliasing_prepare;
		backend.anti_alias = stream_anti_alias;
		backend.hdr = stream_hdr;
		backend.tone_map = stream_tone_map;
		backend.ambient_occlusion = stream_ambient_occlusion;
		backend.picture_top = stream_picture_top;
		backend.gamma = stream_gamma;
#ifdef HALO_VR
		host_vulkan_vr_backend(&backend);
#endif
	}
	return &backend;
}
