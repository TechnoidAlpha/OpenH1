/*
XGPU_METAL_STREAM.M

The Metal backend as the command stream's backend table (struct
host_render_backend, port/android/host/host_render_stream.h): each record's
entry, in terms of xgpu_metal.h's functions. The render thread calls them,
in the records' order.

What the records name, this file keeps: the game's render targets (by its
numbers) as the backend's textures; the mirror's segments as buffers; the
pipelines' definitions, the vertex programs, the shaders made for each
pipeline; the vertex layout of the last set_vertices. A draw's pipeline is
the pipeline's fixed-function part with that layout's attributes.

A pipeline's shaders come from a provider (xgpu_metal_stream_set_shaders:
the MSL emitter's, xgpu_metal_shaders.c; tests give their own), made for
the pipeline's key and the draw's variant (the inputs read as integers, the
border colours the shader draws), as are the uniforms both stages read. The
stages' textures are the cache's (xgpu_metal_textures.c), looked at again
at a draw once a stage has changed or a watched page has been written; a
stage that samples the target the draw draws into samples a copy of it.

The enhanced dynamic lights' block and cells are copied into the frame's
upload ring as the stream sends them, and bound for the draws after them
(in the stream's order, with no pass broken); their glows are a pass of
their own. Anti-aliasing's passes are xgpu_metal_post.c's, over the
target's pixels as the record names them.

Not here: the VR mode (no entries: the stream skips them), and a render
target's mip levels drawn one by one (its first level is sampled).

The interface's values (xgpu_backend.h: the primitives, the clear's parts,
the samplers' filters and addresses, the vertex elements' formats) are read
by the functions under "the interface's values".
*/

#if __has_include("host_render_stream.h")

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#undef host_page_size
#include "host_render_stream.h"
#include "xgpu_metal_internal.h"
#include "xgpu_metal_pipeline.h"
#include "xgpu_metal_post.h"
#include "xgpu_metal_stream.h"
#include "xgpu_metal_textures.h"
#define XGPU_HDR_REFERENCE
#include "xgpu_dynamic_lights_world.h"
#include "xgpu_hdr.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* the mirror's segments: 4 MB of the contiguous window each (d3d8_gl.c's
MIRROR_SEGMENT_SIZE) */
#define MIRROR_SEGMENT_SIZE 0x400000u
#define MIRROR_SEGMENT_MAXIMUM 64

_Static_assert(sizeof(struct xgpu_stream_dynamic_state) == sizeof(struct xgpu_metal_dynamic_state),
	"the dynamic state's layouts");

struct stream_target
{
	uint32_t texture;
	struct xgpu_metal_texture_description description;
	/* counts the draws and clears into it */
	uint32_t written;
};

struct stream_pipeline
{
	/* XGPU_STREAM_PIPELINE_* */
	uint8_t state;
	/* the shaders, made for this variant of the draw's */
	uint32_t vertex_shader, pixel_shader;
	struct xgpu_metal_stream_variant variant;
};

static struct
{
	unsigned char *ring;
	uint32_t ring_size;
	NSMutableDictionary<NSNumber *, NSValue *> *targets;
	uint32_t segments[MIRROR_SEGMENT_MAXIMUM];
	NSMutableDictionary<NSNumber *, NSData *> *vertex_programs, *vertex_lightings;
	struct stream_pipeline pipelines[XGPU_STREAM_PIPELINES];
	struct xgpu_metal_stream_shaders shaders;
	/* the draws' pipeline (without its attributes and shaders), its number
	and definition, and its layout; and the layout of immediate mode's
	draws */
	struct xgpu_metal_pipeline pipeline;
	uint32_t pipeline_id;
	const struct xgpu_stream_pipeline_define *definition;
	/* the inputs the layout feeds as integers; the stages' axes whose
	border colour the shader draws (xgpu_metal_stream_variant) */
	uint16_t integer_attributes;
	uint8_t border[4];
	/* what the uniforms are made of (the provider's uniforms), and whether
	it changed since they were made */
	struct xgpu_stream_draw_uniforms uniforms;
	float constants[XGPU_METAL_VERTEX_CONSTANT_COUNT][4];
	float blend_color[4];
	struct xgpu_stream_sampler samplers[4];
	/* (the constant depth bias the uniforms were made with) */
	float depth_bias;
	BOOL uniforms_dirty;
	/* the stages as set; set since they were bound, and the write
	tracking's serial then; a target's copy bound (checked at each draw) */
	struct xgpu_stream_texture_stage stages[4];
	BOOL textures_dirty, feedback_bound;
	uint32_t textures_serial;
	/* the copy of a target drawn into that a draw samples: of which
	texture, as of which of its draws */
	uint32_t feedback, feedback_target, feedback_written;
	struct xgpu_metal_texture_description feedback_description;
	struct xgpu_metal_vertex_attribute attributes[16];
	uint16_t strides[XGPU_METAL_STREAM_COUNT];
	struct xgpu_metal_source streams[XGPU_METAL_STREAM_COUNT];
	float constant_attributes[16][4];
	uint32_t color, depth, samples;
	/* the colour target's layer drawn into */
	uint32_t color_layer;
	uint32_t back_buffer;
	uint32_t uniforms_serial;
	BOOL uniforms_set, preparing;
	/* a map's preparation made (a draw's pipeline not made after it is
	missed: pipeline_missed), and the draws missed so */
	BOOL prepared;
	uint32_t pipeline_misses;
	/* the dynamic state as last set (the passes of the stream's own set it
	again after them) */
	struct xgpu_metal_dynamic_state dynamic;
	BOOL dynamic_set;
	/* the enhanced dynamic lights' glows' functions (the provider's); their
	zeroed block and cells, bound where the frame has sent none */
	uint32_t halo_vertex, halo_pixel;
	BOOL halo_failed;
	uint32_t lights_none[2];
	/* their world (none: lights_world_none's header of no nodes), as the
	GPU has it (xgpu_dynamic_lights_world.h), and the cells as the stream's
	changes leave them, with the lights the world is walked for (made each
	frame) */
	uint32_t lights_world, lights_world_none;
	struct xgpu_dynamic_lights_world lights_world_made;
	unsigned char *lights_cells;
} stream;

/* display.ambient_occlusion "world" found once a pixel (xgpu_stream.h's
WORLD_AO_PREPASS): the pre-pass is drawing (into inputs and depth, its own
targets of the window's target's size); each pipeline's twin, made of its
definition (xgpu_stream_world_ao_twin), its functions and its pipeline when
first drawn (never waited for: its pixels have no inputs, an occlusion of
1, until they are), or as a map is prepared (twins: its draws are the
pre-pass's in play, XGPU_WORLD_AO_PREPASS_TWINS); the occlusion, and the
pass's functions */
static struct
{
	BOOL active, failed, twins;
	uint32_t width, height;
	uint32_t inputs, depth, pixels;
	uint32_t pixels_width, pixels_height;
	/* the previous pass's occlusion (the history its next reprojects; 0
	none), and its size */
	uint32_t history;
	uint32_t history_width, history_height;
	struct xgpu_stream_pipeline_define *definitions;
	struct stream_pipeline *entries;
	uint32_t vertex, pixel;
} prepass;

static void prepass_targets_bind(void)
{
	xgpu_metal_set_targets(prepass.inputs, 0, 0, prepass.depth, 0);
}

void xgpu_metal_stream_set_shaders(const struct xgpu_metal_stream_shaders *shaders)
{
	uint32_t index;

	stream.shaders = *shaders;
	stream.halo_vertex = stream.halo_pixel = 0;
	stream.halo_failed = NO;
	/* (the pipelines are made again by these) */
	for (index = 0; index < XGPU_STREAM_PIPELINES; index++)
		stream.pipelines[index].state = XGPU_STREAM_PIPELINE_UNKNOWN;
	stream.uniforms_dirty = YES;
}

/* ---------- the interface's values */

/* XGPU_POINTS ... XGPU_TRIANGLE_FAN */
enum
{
	_primitive_points, _primitive_lines, _primitive_line_loop, _primitive_line_strip, _primitive_triangles,
	_primitive_triangle_strip, _primitive_triangle_fan,
};

/* XGPU_CLEAR_*: XGPU_METAL_CLEAR_* are the same */
static uint32_t clear_parts(uint32_t flags)
{
	return flags & (XGPU_METAL_CLEAR_CHANNELS | XGPU_METAL_CLEAR_COLOR | XGPU_METAL_CLEAR_DEPTH | XGPU_METAL_CLEAR_STENCIL);
}

/* XGPU_FILTER_NONE, _POINT, _LINEAR, _ANISOTROPIC; XGPU_ADDRESS_WRAP,
_MIRROR, _CLAMP, _BORDER (XGPU_METAL_ADDRESS_*'s first four); the border
colour as ARGB */
enum
{
	_filter_none, _filter_point, _filter_linear, _filter_anisotropic,
};

/* the sampler; axes: those whose border the shader draws, clamped to the
edge here */
static void sampler_convert(const struct xgpu_stream_sampler *from, uint8_t axes, struct xgpu_metal_sampler *to)
{
	memset(to, 0, sizeof(*to));
	to->min_filter = from->minify >= _filter_linear ? XGPU_METAL_FILTER_LINEAR : XGPU_METAL_FILTER_POINT;
	to->mag_filter = from->magnify >= _filter_linear ? XGPU_METAL_FILTER_LINEAR : XGPU_METAL_FILTER_POINT;
	to->mip_filter = from->mip == _filter_none ? XGPU_METAL_MIP_NONE :
		from->mip == _filter_point ? XGPU_METAL_MIP_POINT : XGPU_METAL_MIP_LINEAR;
	to->address_u = (axes & 1) ? XGPU_METAL_ADDRESS_CLAMP : from->address[0];
	to->address_v = (axes & 2) ? XGPU_METAL_ADDRESS_CLAMP : from->address[1];
	to->address_w = (axes & 4) ? XGPU_METAL_ADDRESS_CLAMP : from->address[2];
	/* (the others are the shader's to blend in, where it can: a cube's or a
	layered target's are transparent black) */
	to->border = from->border_color == 0xff000000u ? XGPU_METAL_BORDER_OPAQUE_BLACK :
		from->border_color == 0xffffffffu ? XGPU_METAL_BORDER_OPAQUE_WHITE : XGPU_METAL_BORDER_TRANSPARENT_BLACK;
	to->max_anisotropy = (uint8_t)(from->minify == _filter_anisotropic && from->anisotropy > 1 ?
		(from->anisotropy > 16 ? 16 : from->anisotropy) : 1);
	to->min_lod = (float)from->first_level;
}

/* ---------- targets */

static struct stream_target *target_get(uint32_t target)
{
	NSValue *value = stream.targets[@(target)];

	return value ? (struct stream_target *)value.pointerValue : NULL;
}

static uint32_t target_texture(uint32_t target)
{
	struct stream_target *entry = target ? target_get(target) : NULL;

	return entry ? entry->texture : 0;
}

static void target_forget(uint32_t target)
{
	struct stream_target *entry = target_get(target);

	if (!entry)
		return;
	xgpu_metal_texture_destroy(entry->texture);
	free(entry);
	[stream.targets removeObjectForKey:@(target)];
	stream.textures_dirty = YES;
}

static void target_make(uint32_t target, const struct xgpu_metal_texture_description *description)
{
	struct stream_target *entry = calloc(1, sizeof(*entry));

	target_forget(target);
	if (!entry)
		return;
	entry->description = *description;
	entry->texture = xgpu_metal_texture_create(description);
	stream.targets[@(target)] = [NSValue valueWithPointer:entry];
	stream.textures_dirty = YES;
}

static void stream_target_create(void *context, const struct xgpu_stream_target_create *record)
{
	struct xgpu_metal_texture_description description;

	(void)context;
	memset(&description, 0, sizeof(description));
	description.type = record->layers > 1 ? XGPU_METAL_TEXTURE_2D_ARRAY : XGPU_METAL_TEXTURE_2D;
	description.format = record->depth ? XGPU_METAL_FORMAT_D24S8 :
		record->format == 3 /* XGPU_FORMAT_RGBA16F */ ? XGPU_METAL_FORMAT_RGBA16F : XGPU_METAL_FORMAT_BGRA8;
	description.usage = XGPU_METAL_USAGE_SAMPLED | XGPU_METAL_USAGE_TARGET;
	description.width = (uint16_t)record->width;
	description.height = (uint16_t)record->height;
	description.depth_or_layers = record->layers > 1 ? record->layers : 1;
	description.levels = 1;
	target_make(record->target, &description);
}

static void stream_target_delete(void *context, uint32_t target)
{
	(void)context;
	target_forget(target);
}

static void stream_target_multisample(void *context, const struct xgpu_stream_target_multisample *record)
{
	struct stream_target *entry = target_get(record->target);
	struct xgpu_metal_texture_description description;

	(void)context;
	if (!entry || entry->description.samples == (record->samples > 1 ? record->samples : 0))
		return;
	description = entry->description;
	description.samples = (uint8_t)(record->samples > 1 ? record->samples : 0);
	/* (its pixels go: the front end makes a target multisampled before it
	draws into it) */
	target_make(record->target, &description);
}

static void stream_set_targets(void *context, const struct xgpu_stream_set_targets *record)
{
	(void)context;
	stream.color = record->color;
	stream.depth = record->depth;
	/* (whether a stage samples the target drawn into) */
	stream.textures_dirty = YES;
	stream.samples = record->samples;
	/* (the macOS app draws no VR views: views and hud_group are the VR
	mode's) */
	stream.color_layer = record->views < 0 ? (uint32_t)(-record->views - 1) + record->base : record->base;
	/* (the pre-pass draws into its own) */
	if (prepass.active)
	{
		prepass_targets_bind();
		return;
	}
	xgpu_metal_set_targets(target_texture(record->color), 0, stream.color_layer, target_texture(record->depth),
		record->samples != 0);
}

/* a target's texture (the tests') */
uint32_t xgpu_metal_stream_target_texture(uint32_t target)
{
	return target_texture(target);
}

/* the stream's targets, dynamic state, textures and uniforms again, after
a pass of the stream's own (the glows, anti-aliasing's) drew with its own:
the next draw sets its pipeline and streams */
static void stream_state_restore(void)
{
	if (prepass.active)
		prepass_targets_bind();
	else
		xgpu_metal_set_targets(target_texture(stream.color), 0, stream.color_layer, target_texture(stream.depth),
			stream.samples != 0);
	if (stream.dynamic_set)
		xgpu_metal_set_dynamic_state(&stream.dynamic);
	stream.textures_dirty = YES;
	stream.uniforms_dirty = YES;
}

static void stream_clear(void *context, const struct xgpu_stream_clear *record)
{
	(void)context;
	/* (none in the pre-pass) */
	if (prepass.active)
		return;
	if (target_get(stream.color))
		target_get(stream.color)->written++;
	xgpu_metal_clear((const int32_t (*)[4])record->rectangles, record->count, clear_parts(record->flags), record->color,
		record->depth, record->stencil);
}

static void stream_clear_target(void *context, const struct xgpu_stream_clear_target *record)
{
	(void)context;
	if (prepass.active)
		return;
	if (target_get(record->target))
		target_get(record->target)->written++;
	xgpu_metal_set_targets(target_texture(record->target), 0, 0, 0, 0);
	xgpu_metal_clear(NULL, 0, XGPU_METAL_CLEAR_COLOR | XGPU_METAL_CLEAR_CHANNELS, record->color, 1.0f, 0);
	xgpu_metal_set_targets(target_texture(stream.color), 0, 0, target_texture(stream.depth), stream.samples != 0);
}

/* an RGBA16F target's pixels (display.hdr's back buffer) as BGRA8, as an
SDR display shows them (clamped to [0, 1]), or tone mapped into [0, 1]
(xgpu_hdr_sdr_pixel: the screenshot key's); 0 if they cannot be read */
static int read_half_floats(struct stream_target *entry, uint32_t layer, unsigned char *bgra, int tone_map)
{
	uint32_t width = entry->description.width, height = entry->description.height;
	__fp16 *halves = malloc((size_t)width * height * 8);
	size_t pixel;
	int read;

	if (!halves)
		return 0;
	read = xgpu_metal_read_pixels(entry->texture, 0, layer, 0, 0, width, height, halves, width * 8);
	for (pixel = 0; read && pixel < (size_t)width * height; pixel++)
	{
		int channel;

		if (tone_map)
		{
			float rgba[4] = { (float)halves[pixel * 4], (float)halves[pixel * 4 + 1], (float)halves[pixel * 4 + 2],
				(float)halves[pixel * 4 + 3] };

			xgpu_hdr_sdr_pixel(rgba, bgra + pixel * 4);
			continue;
		}
		for (channel = 0; channel < 4; channel++)
		{
			float value = (float)halves[pixel * 4 + channel];

			/* (BGRA: blue, green, red, alpha) */
			bgra[pixel * 4 + (channel == 3 ? 3 : 2 - channel)] =
				(unsigned char)(value <= 0.0f ? 0 : value >= 1.0f ? 255 : (int)(value * 255.0f + 0.5f));
		}
	}
	free(halves);
	return read;
}

static int stream_read_target(void *context, const struct xgpu_stream_read_target *record)
{
	struct stream_target *entry = target_get(record->target);
	uint32_t width, height;

	(void)context;
	if (!entry || !record->pixels)
		return 0;
	width = entry->description.width;
	height = entry->description.height;
	if ((uint64_t)width * height * 4 > record->size)
		return 0;
	if (entry->description.format == XGPU_METAL_FORMAT_RGBA16F)
		return read_half_floats(entry, record->layer > 0 ? (uint32_t)record->layer : 0, host_pointer(record->pixels),
			record->tone_map != 0);
	return xgpu_metal_read_pixels(entry->texture, 0, record->layer > 0 ? (uint32_t)record->layer : 0, 0, 0, width, height,
		host_pointer(record->pixels), width * 4);
}

/* display.hdr's shots: the frame's values as they are (an RGBA16F target's
half floats), beside its PNG: <path>.rgba16f, a header of the magic "RGBA16F\0",
the width and the height (32 bits each, little-endian), then the rows from
the top, four half floats a pixel; and the frame encoded for the display the
presentation had then (<path>.encoded.rgba16f: linear light, 1.0 the
system's SDR white, as host_metal.m's encoding writes it: xgpu_hdr.h's
reference), with the encoding's paper white, peak and output in a line of
<path>.encoded.txt */
static void shot_half_floats(struct stream_target *entry, const char *path)
{
	uint32_t width = entry->description.width, height = entry->description.height, header[4] = { 0, 0, 0, 0 };
	__fp16 *halves = malloc((size_t)width * height * 8);
	struct xgpu_hdr_frame frame;
	char name[1100];
	FILE *file;
	size_t pixel;

	if (!halves || !xgpu_metal_read_pixels(entry->texture, 0, 0, 0, 0, width, height, halves, width * 8))
	{
		free(halves);
		return;
	}
	memcpy(header, "RGBA16F", 8);
	header[2] = width;
	header[3] = height;
	snprintf(name, sizeof(name), "%s.rgba16f", path);
	if ((file = fopen(name, "wb")) != NULL)
	{
		fwrite(header, sizeof(header), 1, file);
		fwrite(halves, 8, (size_t)width * height, file);
		fclose(file);
	}
	host_metal_hdr_frame(&frame);
	for (pixel = 0; pixel < (size_t)width * height; pixel++)
	{
		float rgb[3] = { (float)halves[pixel * 4], (float)halves[pixel * 4 + 1], (float)halves[pixel * 4 + 2] };

		xgpu_hdr_encode(rgb, &frame);
		halves[pixel * 4] = (__fp16)rgb[0];
		halves[pixel * 4 + 1] = (__fp16)rgb[1];
		halves[pixel * 4 + 2] = (__fp16)rgb[2];
		halves[pixel * 4 + 3] = (__fp16)1.0f;
	}
	snprintf(name, sizeof(name), "%s.encoded.rgba16f", path);
	if ((file = fopen(name, "wb")) != NULL)
	{
		fwrite(header, sizeof(header), 1, file);
		fwrite(halves, 8, (size_t)width * height, file);
		fclose(file);
	}
	snprintf(name, sizeof(name), "%s.encoded.txt", path);
	if ((file = fopen(name, "w")) != NULL)
	{
		fprintf(file, "paper_white %.6f peak %.6f output %d mode %d ui %.6f\n", (double)frame.paper_white,
			(double)frame.peak, frame.output, frame.mode, (double)frame.ui);
		fclose(file);
	}
	free(halves);
}

/* a target's picture as a PNG (host_render_record.h's shots: a frame's back
buffer as it is presented); its pixels' alpha is not written */
unsigned char *xgpu_metal_stream_read(uint32_t target, uint32_t *width, uint32_t *height)
{
	struct stream_target *entry = target_get(target);
	unsigned char *pixels;

	if (!entry || (entry->description.format != XGPU_METAL_FORMAT_BGRA8 &&
		entry->description.format != XGPU_METAL_FORMAT_RGBA16F))
		return NULL;
	*width = entry->description.width;
	*height = entry->description.height;
	pixels = malloc((size_t)*width * *height * 4);
	if (pixels && !(entry->description.format == XGPU_METAL_FORMAT_RGBA16F ? read_half_floats(entry, 0, pixels, 0) :
		xgpu_metal_read_pixels(entry->texture, 0, 0, 0, 0, *width, *height, pixels, *width * 4)))
	{
		free(pixels);
		pixels = NULL;
	}
	return pixels;
}

int xgpu_metal_stream_shot(uint32_t target, const char *path)
{
	struct stream_target *entry = target_get(target);
	uint32_t width, height;
	unsigned char *pixels;
	int written = 0;

	if (entry && entry->description.format == XGPU_METAL_FORMAT_RGBA16F)
		shot_half_floats(entry, path);
	pixels = xgpu_metal_stream_read(target, &width, &height);
	if (pixels)
	{
		CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, pixels, (size_t)width * height * 4, NULL);
		CGImageRef image = CGImageCreate(width, height, 8, 32, width * 4, space,
			kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst, provider, NULL, false,
			kCGRenderingIntentDefault);
		NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
		CGImageDestinationRef destination = CGImageDestinationCreateWithURL((__bridge CFURLRef)url, CFSTR("public.png"), 1,
			NULL);

		if (destination && image)
		{
			CGImageDestinationAddImage(destination, image, NULL);
			written = CGImageDestinationFinalize(destination) ? 1 : 0;
		}
		if (destination)
			CFRelease(destination);
		CGImageRelease(image);
		CGDataProviderRelease(provider);
		CGColorSpaceRelease(space);
	}
	free(pixels);
	return written;
}

/* ---------- textures */

/* XGPU_SOURCE_* */
enum
{
	_source_none, _source_texture, _source_target,
};

/* xgpu.h's _xgpu_sampler_* */
enum
{
	_xgpu_sampler_none, _xgpu_sampler_2d, _xgpu_sampler_3d, _xgpu_sampler_cube, _xgpu_sampler_2d_layered,
};

static void stream_set_textures(void *context, const struct host_render_texture_stage stages[4])
{
	int index;

	(void)context;
	for (index = 0; index < 4; index++)
	{
		const struct xgpu_stream_texture_stage *stage = &stages[index].stage;

		stream.stages[index] = *stage;
		stream.border[index] = xgpu_metal_border_axes(stage->source, stage->sampler_type, &stage->sampler);
		if (memcmp(&stream.samplers[index], &stage->sampler, sizeof(stage->sampler)))
		{
			stream.samplers[index] = stage->sampler;
			stream.uniforms_dirty = YES;
		}
	}
	stream.textures_dirty = YES;
}

/* a copy of a target the draw draws into, which it samples (Metal cannot
sample an attachment of the pass): copied again once the target has been
drawn into since */
static uint32_t feedback_copy(struct stream_target *target)
{
	if (target->description.format != XGPU_METAL_FORMAT_BGRA8 && target->description.format != XGPU_METAL_FORMAT_RGBA16F)
		return target->texture;
	if (!stream.feedback || stream.feedback_description.width != target->description.width ||
		stream.feedback_description.height != target->description.height ||
		stream.feedback_description.format != target->description.format)
	{
		struct xgpu_metal_texture_description description = target->description;

		if (stream.feedback)
			xgpu_metal_texture_destroy(stream.feedback);
		description.type = XGPU_METAL_TEXTURE_2D;
		description.usage = XGPU_METAL_USAGE_SAMPLED | XGPU_METAL_USAGE_TARGET;
		description.samples = 0;
		description.depth_or_layers = 1;
		description.levels = 1;
		stream.feedback = xgpu_metal_texture_create(&description);
		stream.feedback_description = description;
		stream.feedback_target = 0;
	}
	if (stream.feedback && (stream.feedback_target != target->texture || stream.feedback_written != target->written))
	{
		xgpu_metal_texture_copy(target->texture, stream.feedback);
		stream.feedback_target = target->texture;
		stream.feedback_written = target->written;
	}
	return stream.feedback ? stream.feedback : target->texture;
}

/* the stages' textures and samplers for a draw: the textures are looked up
again where a stage changed or a watched page has been written since (the
textures' cache makes them again from pages written since), and a target the
draw draws into is copied */
static void textures_bind(void)
{
	uint32_t serial = host_memory_watch_serial(), index;

	if (!stream.textures_dirty && serial == stream.textures_serial && !stream.feedback_bound)
		return;
	stream.textures_dirty = NO;
	stream.textures_serial = serial;
	stream.feedback_bound = NO;
	for (index = 0; index < 4; index++)
	{
		const struct xgpu_stream_texture_stage *stage = &stream.stages[index];
		struct xgpu_metal_sampler sampler;
		uint32_t texture = 0;

		sampler_convert(&stage->sampler, stream.border[index], &sampler);
		if (stage->source == _source_target)
		{
			struct stream_target *target = stage->target ? target_get(stage->target) : NULL;

			if (target)
			{
				texture = target->texture;
				/* (drawn into by this draw: its copy, checked at each draw) */
				if (stage->drawn == stage->target && stage->target == stream.color)
				{
					texture = feedback_copy(target);
					stream.feedback_bound = YES;
				}
			}
		}
		else if (stage->source == _source_texture || stage->source == XGPU_STREAM_SOURCE_IMAGE)
		{
			texture = xgpu_metal_textures_get(stage);
		}
		xgpu_metal_set_texture(index, texture, &sampler);
	}
}

/* ---------- pipelines */

/* the per-pixel lighting's lights' registers (xgpu_backend.h's
XGPU_MODEL_LIGHT_REGISTER: c[-82], c[-79] to c[-69], c[-57]) */
#define MODEL_LIGHT_FIRST (96 - 82)
#define MODEL_LIGHT_LAST (96 - 69)
#define MODEL_LIGHT_OWN_LIGHTS (96 - 57)

static void stream_set_constants(void *context, const struct xgpu_stream_constants *record)
{
	uint32_t count = record->count;

	(void)context;
	if (record->first >= XGPU_METAL_VERTEX_CONSTANT_COUNT)
		return;
	if (count > XGPU_METAL_VERTEX_CONSTANT_COUNT - record->first)
		count = XGPU_METAL_VERTEX_CONSTANT_COUNT - record->first;
	memcpy(stream.constants[record->first], record->values, (size_t)count * sizeof(stream.constants[0]));
	if ((record->first <= MODEL_LIGHT_LAST && record->first + count > MODEL_LIGHT_FIRST) ||
		(record->first <= MODEL_LIGHT_OWN_LIGHTS && record->first + count > MODEL_LIGHT_OWN_LIGHTS))
		stream.uniforms_dirty = YES;
	xgpu_metal_set_vertex_constants(record->first, count, &record->values[0][0]);
}

static void stream_vertex_program(void *context, const struct xgpu_stream_vertex_program *record)
{
	uint64_t hash = record->hash[0] | ((uint64_t)record->hash[1] << 32);

	(void)context;
	stream.vertex_programs[@(hash)] = [NSData dataWithBytes:record length:XGPU_STREAM_HEADER_SIZE(record->header)];
}

static void stream_vertex_lighting(void *context, const struct xgpu_stream_vertex_lighting *record)
{
	uint64_t hash = record->hash[0] | ((uint64_t)record->hash[1] << 32);

	(void)context;
	stream.vertex_lightings[@(hash)] = [NSData dataWithBytes:record length:sizeof(*record)];
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

/* a pipeline's shaders for a variant of the draw's: made by the provider
if the pipeline has none for it; 0 if they cannot be */
static int pipeline_shaders(struct stream_pipeline *entry, const struct xgpu_stream_pipeline_define *definition,
	const struct xgpu_metal_stream_variant *variant)
{
	const struct xgpu_stream_pipeline_key *key = &definition->key;
	uint64_t hash = key->vertex_program[0] | ((uint64_t)key->vertex_program[1] << 32);
	NSData *program, *lighting;

	if (entry->state == XGPU_STREAM_PIPELINE_MADE && !memcmp(&entry->variant, variant, sizeof(*variant)))
		return 1;
	if (entry->state == XGPU_STREAM_PIPELINE_FAILED || !stream.shaders.make)
		return 0;
	program = stream.vertex_programs[@(hash)];
	lighting = stream.vertex_lightings[@(hash)];
	entry->variant = *variant;
	if (stream.shaders.make(stream.shaders.context, definition, program ? program.bytes : NULL,
		lighting ? lighting.bytes : NULL, variant, &entry->vertex_shader, &entry->pixel_shader) && entry->vertex_shader)
	{
		entry->state = XGPU_STREAM_PIPELINE_MADE;
		return 1;
	}
	/* (one that fails is not tried again for every draw) */
	entry->state = XGPU_STREAM_PIPELINE_FAILED;
	return 0;
}

static int stream_pipeline(void *context, uint32_t id, const struct xgpu_stream_pipeline_define *definition)
{
	struct stream_pipeline *entry;
	const struct xgpu_stream_pipeline_key *key = &definition->key;
	struct xgpu_metal_pipeline *pipeline = &stream.pipeline;

	(void)context;
	stream.pipeline_id = 0;
	if (!id || id >= XGPU_STREAM_PIPELINES)
		return 0;
	entry = &stream.pipelines[id];
	/* (made at first in the variant the draws have now, to tell the guest
	whether it can be) */
	if (entry->state == XGPU_STREAM_PIPELINE_UNKNOWN)
	{
		struct xgpu_metal_stream_variant variant;

		memset(&variant, 0, sizeof(variant));
		variant.integer_attributes = key->immediate ? 0 : stream.integer_attributes;
		memcpy(variant.border, stream.border, sizeof(variant.border));
		pipeline_shaders(entry, definition, &variant);
	}
	if (entry->state != XGPU_STREAM_PIPELINE_MADE)
		return 0;
	stream.pipeline_id = id;
	stream.definition = definition;
	xgpu_metal_pipeline_from_key(key, pipeline);
	return 1;
}

static void stream_draw_state(void *context, const struct xgpu_stream_dynamic_state *dynamic,
	const struct xgpu_stream_draw_uniforms *uniforms, uint32_t uniforms_serial)
{
	(void)context;
	if (dynamic)
	{
		memcpy(&stream.dynamic, dynamic, sizeof(stream.dynamic));
		stream.dynamic_set = YES;
		xgpu_metal_set_dynamic_state((const struct xgpu_metal_dynamic_state *)dynamic);
		if (memcmp(stream.blend_color, dynamic->blend_color, sizeof(stream.blend_color)))
		{
			memcpy(stream.blend_color, dynamic->blend_color, sizeof(stream.blend_color));
			stream.uniforms_dirty = YES;
		}
	}
	if (uniforms && (!stream.uniforms_set || uniforms_serial != stream.uniforms_serial))
	{
		stream.uniforms = *uniforms;
		stream.uniforms_serial = uniforms_serial;
		stream.uniforms_set = YES;
		stream.uniforms_dirty = YES;
	}
}

/* the uniforms both stages read, made again where what they are made of
changed */
static void uniforms_update(void)
{
	struct xgpu_metal_stream_uniform_inputs inputs;
	unsigned char bytes[4096];
	uint32_t size;
	float depth_bias = xgpu_reversed_depth_bias(stream.pipeline.depth_bias, stream.dynamic.depth_bias[1]);

	/* (the bias is the pipeline's and the dynamic state's) */
	if (depth_bias != stream.depth_bias)
	{
		stream.depth_bias = depth_bias;
		stream.uniforms_dirty = YES;
	}
	if (!stream.uniforms_dirty || !stream.uniforms_set)
		return;
	stream.uniforms_dirty = NO;
	if (!stream.shaders.uniforms)
	{
		xgpu_metal_set_draw_uniforms(&stream.uniforms, sizeof(stream.uniforms));
		return;
	}
	inputs.uniforms = &stream.uniforms;
	inputs.blend_color = stream.blend_color;
	inputs.constants = (const float (*)[4])stream.constants;
	inputs.samplers = stream.samplers;
	inputs.depth_bias = stream.depth_bias;
	size = stream.shaders.uniforms(stream.shaders.context, &inputs, bytes, sizeof(bytes));
	if (size)
		xgpu_metal_set_draw_uniforms(bytes, size);
}

/* ---------- vertices */

static uint32_t segment_buffer(int32_t segment)
{
	if (segment < 0 || segment >= MIRROR_SEGMENT_MAXIMUM)
		return 0;
	if (!stream.segments[segment])
		stream.segments[segment] = xgpu_metal_buffer_create(MIRROR_SEGMENT_SIZE);
	return stream.segments[segment];
}

static void stream_mirror_write(void *context, const struct xgpu_stream_mirror_write *record)
{
	(void)context;
	if ((uint64_t)record->offset + record->size > MIRROR_SEGMENT_SIZE)
		return;
	xgpu_metal_buffer_write(segment_buffer((int32_t)record->segment), record->offset, host_pointer(record->data),
		record->size, record->unused != 0);
}

/* inline data at a guest address: in the ring, or (too large for it) in a
buffer of its own in guest memory, read in place as well */
static struct xgpu_metal_source ring_source(uint32_t address, uint32_t size)
{
	struct xgpu_metal_source source = { XGPU_METAL_SOURCE_STREAM_RING, 0, size, 0 };
	unsigned char *data = host_pointer(address);

	if (!data)
		return (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 };
	if (data < stream.ring || data + size > stream.ring + stream.ring_size)
		return xgpu_metal_guest(address, size);
	source.offset = (uint32_t)(data - stream.ring);
	return source;
}

static void stream_set_vertices(void *context, const struct host_render_vertex_input *input)
{
	uint32_t index;

	(void)context;
	memset(stream.strides, 0, sizeof(stream.strides));
	stream.integer_attributes = xgpu_metal_layout_attributes(input->layout ? input->layout->elements : NULL,
		input->layout ? input->layout->element_count : 0, stream.attributes);
	for (index = 0; index < XGPU_METAL_STREAM_COUNT; index++)
	{
		const struct xgpu_stream_vertex_stream *source = &input->streams[index];

		if (!(input->present & (1u << index)))
		{
			stream.streams[index] = (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 };
			continue;
		}
		stream.strides[index] = (uint16_t)source->stride;
		if (source->segment >= 0)
			stream.streams[index] = (struct xgpu_metal_source){ XGPU_METAL_SOURCE_BUFFER, source->offset,
				MIRROR_SEGMENT_SIZE - source->offset, segment_buffer(source->segment) };
		else
			stream.streams[index] = ring_source(source->data, source->bytes);
	}
	memcpy(stream.constant_attributes, input->attributes, sizeof(stream.constant_attributes));
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

/* A map's preparation should make every pipeline its draws need before
play: a draw whose pipeline is not made is logged with what tells it
apart (as Vulkan's: xgpu_vulkan_stream.c), the settings' parts of it
(xgpu_draw_derive) among them */
static void pipeline_missed(const struct xgpu_stream_pipeline_key *key)
{
	stream.pipeline_misses++;
	host_logf(HOST_LOG_WARN, "metal: pipeline missed map preparation: vertex %08x%08x, key %08x, pixel %08x "
		"(lighting %u, occlusion %u, hdr %x, per-pixel %u, lights %u, samples %u/%u, colour %u, depth %u, "
		"blend %u/%u/%u, mask %x, stencil %u, views %u), total %u", (unsigned)key->vertex_program[1],
		(unsigned)key->vertex_program[0], bytes_hash(key, sizeof(*key)), bytes_hash(&key->pixel, sizeof(key->pixel)),
		(unsigned)key->lighting, (unsigned)key->pixel.ambient_occlusion, (unsigned)key->pixel.hdr,
		(unsigned)key->pixel.per_pixel_lighting, (unsigned)key->pixel.dynamic_lights, (unsigned)key->samples,
		(unsigned)key->pixel.alpha_test_samples, (unsigned)key->color_format, (unsigned)key->depth_format,
		(unsigned)key->blend, (unsigned)key->blend_source, (unsigned)key->blend_destination,
		(unsigned)key->color_mask, (unsigned)key->stencil_test, (unsigned)key->views, stream.pipeline_misses);
}

/* the pipeline and layout for a draw: set_vertices's, or immediate mode's
(integers: the inputs read as integers); NO if it cannot be drawn */
static BOOL prepass_draw_prepare(const struct xgpu_metal_vertex_attribute *attributes, const uint16_t *strides,
	const struct xgpu_metal_source *streams, const float (*constants)[4], uint16_t integers);

static BOOL draw_prepare(const struct xgpu_metal_vertex_attribute *attributes, const uint16_t *strides,
	const struct xgpu_metal_source *streams, const float (*constants)[4], uint16_t integers)
{
	struct xgpu_metal_pipeline pipeline = stream.pipeline;
	struct xgpu_metal_stream_variant variant;
	struct stream_pipeline *entry;
	uint32_t index;

	if (!stream.pipeline_id || !stream.definition)
		return NO;
	/* (the pre-pass: the twins of the draws with the world's light, read
	from their pixels, alone) */
	if (prepass.active)
		return prepass_draw_prepare(attributes, strides, streams, constants, integers);
	entry = &stream.pipelines[stream.pipeline_id];
	memset(&variant, 0, sizeof(variant));
	variant.integer_attributes = stream.definition->key.immediate ? 0 : integers;
	memcpy(variant.border, stream.border, sizeof(variant.border));
	if (!pipeline_shaders(entry, stream.definition, &variant))
		return NO;
	pipeline.vertex_shader = entry->vertex_shader;
	pipeline.pixel_shader = entry->pixel_shader;
	memcpy(pipeline.attributes, attributes, sizeof(pipeline.attributes));
	memcpy(pipeline.strides, strides, sizeof(pipeline.strides));
	if (stream.preparing)
	{
		/* Keep resource residency and the complete vertex layout, without
		allocating transient draw data for an unseen picture. */
		textures_bind();
		xgpu_metal_prepare(&pipeline);
		/* (where its draws are the pre-pass's in play: its twin's too) */
		if (prepass.twins)
			(void)prepass_draw_prepare(attributes, strides, streams, constants, integers);
		return NO;
	}
	if (stream.prepared && !xgpu_metal_pipeline_known(&pipeline))
		pipeline_missed(&stream.definition->key);
	uniforms_update();
	textures_bind();
	if (target_get(stream.color))
		target_get(stream.color)->written++;
	memcpy(pipeline.attributes, attributes, sizeof(pipeline.attributes));
	memcpy(pipeline.strides, strides, sizeof(pipeline.strides));
	xgpu_metal_set_pipeline(&pipeline);
	for (index = 0; index < XGPU_METAL_STREAM_COUNT; index++)
		xgpu_metal_set_stream(index, streams[index]);
	xgpu_metal_set_constant_attributes(constants);
	return YES;
}

/* draws count vertices (indices: 16-bit at data, or NULL for 0 to count),
making fans and loops into lists, which Metal has */
static void draw_converted(uint32_t primitive, uint32_t count, const uint16_t *indices, struct xgpu_metal_source source,
	int32_t base_vertex)
{
	static const uint32_t metal_primitives[] = {
		[_primitive_points] = XGPU_METAL_PRIMITIVE_POINTS,
		[_primitive_lines] = XGPU_METAL_PRIMITIVE_LINES,
		[_primitive_line_strip] = XGPU_METAL_PRIMITIVE_LINE_STRIP,
		[_primitive_triangles] = XGPU_METAL_PRIMITIVE_TRIANGLES,
		[_primitive_triangle_strip] = XGPU_METAL_PRIMITIVE_TRIANGLE_STRIP,
	};
	uint16_t *list;
	uint32_t index, written = 0;

	if (primitive != _primitive_triangle_fan && primitive != _primitive_line_loop)
	{
		if (primitive < sizeof(metal_primitives) / sizeof(metal_primitives[0]))
			xgpu_metal_draw(metal_primitives[primitive], count, 0, base_vertex, source);
		return;
	}
	if (count < 2 || count > 65536)
		return;
	list = malloc(sizeof(*list) * (size_t)count * 3);
	if (!list)
		return;
	if (primitive == _primitive_triangle_fan)
	{
		for (index = 1; index + 1 < count; index++)
		{
			list[written++] = indices ? indices[0] : 0;
			list[written++] = indices ? indices[index] : (uint16_t)index;
			list[written++] = indices ? indices[index + 1] : (uint16_t)(index + 1);
		}
	}
	else
	{
		for (index = 0; index < count; index++)
		{
			list[written++] = indices ? indices[index] : (uint16_t)index;
			list[written++] = indices ? indices[(index + 1) % count] : (uint16_t)((index + 1) % count);
		}
	}
	if (written)
		xgpu_metal_draw(primitive == _primitive_triangle_fan ? XGPU_METAL_PRIMITIVE_TRIANGLES : XGPU_METAL_PRIMITIVE_LINES,
			written, 0, base_vertex, xgpu_metal_stream(list, written * (uint32_t)sizeof(*list)));
	free(list);
}

static void stream_draw(void *context, const struct xgpu_stream_draw *record)
{
	(void)context;
	if (!draw_prepare(stream.attributes, stream.strides, stream.streams, (const float (*)[4])stream.constant_attributes,
		stream.integer_attributes))
		return;
	draw_converted(record->primitive, record->count, NULL, (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 }, 0);
}

static void stream_draw_indices(void *context, const struct xgpu_stream_draw_indices *record)
{
	struct xgpu_metal_source source = ring_source(record->indices, record->count * 2);

	(void)context;
	if (source.kind == XGPU_METAL_SOURCE_NONE)
		return;
	if (!draw_prepare(stream.attributes, stream.strides, stream.streams, (const float (*)[4])stream.constant_attributes,
		stream.integer_attributes))
		return;
	draw_converted(record->primitive, record->count, host_pointer(record->indices), source, 0);
}

static void stream_draw_indexed(void *context, const struct xgpu_stream_draw_indexed *record)
{
	struct xgpu_metal_source source;
	const uint16_t *indices = NULL;

	(void)context;
	if (record->segment >= 0)
	{
		uint32_t buffer = segment_buffer(record->segment);

		if ((uint64_t)record->offset + record->count * 2 > MIRROR_SEGMENT_SIZE || !buffer)
			return;
		source = (struct xgpu_metal_source){ XGPU_METAL_SOURCE_BUFFER, record->offset, record->count * 2, buffer };
		/* (a fan's or a loop's indices, made into a list, are read from the
		mirror's copy) */
		indices = (const uint16_t *)((const unsigned char *)xgpu_metal_buffer_contents(buffer) + record->offset);
	}
	else
	{
		source = ring_source(record->indices, record->count * 2);
		indices = host_pointer(record->indices);
	}
	if (source.kind == XGPU_METAL_SOURCE_NONE)
		return;
	if (!draw_prepare(stream.attributes, stream.strides, stream.streams, (const float (*)[4])stream.constant_attributes,
		stream.integer_attributes))
		return;
	/* (the streams start at vertex minimum) */
	draw_converted(record->primitive, record->count, indices, source, -(int32_t)record->minimum);
}

static void stream_draw_immediate(void *context, const struct xgpu_stream_draw_immediate *record,
	const float (*attributes)[4])
{
	struct xgpu_metal_vertex_attribute layout[16];
	uint16_t strides[XGPU_METAL_STREAM_COUNT];
	struct xgpu_metal_source streams[XGPU_METAL_STREAM_COUNT];
	uint32_t written, size;

	(void)context;
	memset(streams, 0, sizeof(streams));
	/* the registers written, a vec4 each, in order; the rest from their
	values */
	written = xgpu_metal_layout_immediate(record->written, layout, strides);
	if (!written || !record->count)
		return;
	size = record->count * written * 16;
	streams[0] = ring_source(record->vertices, size);
	if (streams[0].kind == XGPU_METAL_SOURCE_NONE)
		return;
	if (!draw_prepare(layout, strides, streams, attributes, 0))
		return;
	if (record->indices && record->index_count)
		draw_converted(record->primitive, record->index_count, host_pointer(record->indices),
			ring_source(record->indices, record->index_count * 2), 0);
	else
		draw_converted(record->primitive, record->count, NULL, (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 }, 0);
}

/* ---------- the enhanced dynamic lights (display.dynamic_lights; xgpu.h)

The front end works out the frame's lights and the grid's cells
(d3d8_device.c); the block and the cells go into the frame's upload ring as
the stream sends them, and the draws after them read them there (the MSL
emitter's lit shaders: xgpu_msl.c), in the stream's order without a pass
broken; and the glows are drawn here, as the Vulkan backend draws them
(xgpu_vulkan_stream.c). Where the frame has sent none, a zeroed block and cells are
bound (no light reaches anything). */

_Static_assert(sizeof(struct xgpu_stream_dynamic_lights_block) == 6192, "the dynamic lights' block");

/* a glow's vertex: its light's middle and the glow's size, its color, the
corner (xgpu_msl.h's XGPU_MSL_HALO_*: attributes 0, 1 and 2) */
struct halo_vertex
{
	float middle[4];
	float color[4];
	float corner[2];
};

/* the zeroed block and cells bound (made the first time) */
static void lights_none_bind(void)
{
	static const uint32_t sizes[2] = { sizeof(struct xgpu_stream_dynamic_lights_block),
		XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE };
	struct xgpu_metal_source sources[2];
	int index;

	for (index = 0; index < 2; index++)
	{
		if (!stream.lights_none[index])
		{
			void *zeros = calloc(1, sizes[index]);

			stream.lights_none[index] = xgpu_metal_buffer_create(sizes[index]);
			if (stream.lights_none[index] && zeros)
				xgpu_metal_buffer_write(stream.lights_none[index], 0, zeros, sizes[index], 1);
			free(zeros);
		}
		sources[index] = stream.lights_none[index] ?
			(struct xgpu_metal_source){ XGPU_METAL_SOURCE_BUFFER, 0, sizes[index], stream.lights_none[index] } :
			(struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 };
	}
	xgpu_metal_set_dynamic_lights(sources[0], sources[1]);
	if (!stream.lights_world_none)
	{
		static const uint32_t none[4];

		stream.lights_world_none = xgpu_metal_buffer_create(sizeof(none));
		if (stream.lights_world_none)
			xgpu_metal_buffer_write(stream.lights_world_none, 0, none, sizeof(none), 1);
	}
	if (!stream.lights_world && stream.lights_world_none)
		xgpu_metal_set_dynamic_light_world((struct xgpu_metal_source){ XGPU_METAL_SOURCE_BUFFER, 0, 16,
			stream.lights_world_none });
}

static int stream_dynamic_lights_prepare(void *context)
{
	(void)context;
	lights_none_bind();
	return stream.lights_none[0] && stream.lights_none[1];
}

/* the frame's cells (the bytes from offset, size of them: the stream's
version 22's the whole, a change only those that changed) into the cells
kept (the stream's changes applied, with the lights the world is walked
for: xgpu.h), which the frame's draws read */
static void dynamic_lights_cells(const struct xgpu_stream_dynamic_lights_block *record_block, uint32_t count,
	uint32_t offset, uint32_t size, const unsigned char *cells)
{
	struct xgpu_metal_source block, copied;

	if (!stream.lights_cells)
		stream.lights_cells = calloc(1, XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE);
	if (stream.lights_cells)
	{
		if (size)
			memcpy(stream.lights_cells + offset, cells, size);
		xgpu_dynamic_lights_world_walked(&stream.lights_world_made, (const float (*)[4])record_block, count,
			stream.lights_cells, (uint32_t *)(stream.lights_cells + XGPU_DYNAMIC_LIGHTS_WORLD_WALK_TEXEL * 16));
		cells = stream.lights_cells;
	}
	else if (offset || size != XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE)
	{
		lights_none_bind();
		return;
	}
	block = xgpu_metal_stream(record_block, sizeof(*record_block));
	copied = xgpu_metal_stream(cells, XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE);
	if (block.kind == XGPU_METAL_SOURCE_NONE || copied.kind == XGPU_METAL_SOURCE_NONE)
		lights_none_bind();
	else
		xgpu_metal_set_dynamic_lights(block, copied);
}

static void stream_dynamic_lights_upload(void *context, const struct xgpu_stream_dynamic_lights_upload *record)
{
	const void *cells = host_pointer(record->cells);

	(void)context;
	if (!cells || record->size != XGPU_METAL_STREAM_DYNAMIC_LIGHT_CELLS_SIZE)
		return;
	dynamic_lights_cells(&record->block, record->count, 0, record->size, cells);
}

static void stream_dynamic_lights_change(void *context, const struct xgpu_stream_dynamic_lights_change *record)
{
	const void *cells = record->size ? host_pointer(record->cells) : NULL;

	(void)context;
	if (record->size && !cells)
		return;
	dynamic_lights_cells(&record->block, record->count, record->offset, record->size, cells);
}

/* the world (xgpu.h), as the GPU has it (xgpu_dynamic_lights_world.h), into
a buffer of its own made the first time with room for the most, written in
the frame's order */
static void stream_dynamic_lights_world(void *context, const struct xgpu_stream_dynamic_lights_world *record)
{
	const void *texels = host_pointer(record->texels);
	struct xgpu_dynamic_lights_world *made = &stream.lights_world_made;

	(void)context;
	if (!texels || !record->count || record->count > XGPU_METAL_STREAM_DYNAMIC_LIGHT_WORLD_TEXELS)
		return;
	if (!stream.lights_world)
		stream.lights_world = xgpu_metal_buffer_create(XGPU_METAL_STREAM_DYNAMIC_LIGHT_WORLD_TEXELS * 16);
	if (!stream.lights_world)
		return;
	if (!xgpu_dynamic_lights_world_convert(made, (const uint32_t (*)[4])texels, record->count))
		host_logf(HOST_LOG_WARN, "metal: the dynamic lights' world (%u texels) could not be made: its lights reach "
			"every cluster", record->count);
	else if (made->game_nodes)
		host_logf(HOST_LOG_INFO, "metal: the dynamic lights' world: %u nodes, %u deep (the game's %u, %u deep)",
			made->nodes, made->depth, made->game_nodes, made->game_depth);
	if (!made->count)
		return;
	xgpu_metal_buffer_write(stream.lights_world, 0, made->texels, made->count * 16, 1);
	xgpu_metal_set_dynamic_light_world((struct xgpu_metal_source){ XGPU_METAL_SOURCE_BUFFER, 0,
		XGPU_METAL_STREAM_DYNAMIC_LIGHT_WORLD_TEXELS * 16, stream.lights_world });
}

/* the glows' functions, made the first time; 0 if they cannot be */
static int halo_shaders(void)
{
	if (stream.halo_vertex && stream.halo_pixel)
		return 1;
	if (stream.halo_failed || !stream.shaders.halo)
		return 0;
	stream.halo_failed = YES;
	if (!stream.shaders.halo(stream.shaders.context, &stream.halo_vertex, &stream.halo_pixel) || !stream.halo_vertex ||
		!stream.halo_pixel)
	{
		host_logf(HOST_LOG_WARN, "metal: the dynamic lights' glows' functions cannot be made");
		stream.halo_vertex = stream.halo_pixel = 0;
		return 0;
	}
	stream.halo_failed = NO;
	return 1;
}

/* the depth buffer's value for a distance from the camera, alpha + beta /
distance (beta less than 0: nearer, less), from two points ahead; and the
softness, world units (as xgpu_vulkan_stream.c's) */
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

/* the glows of the frame's bolts' lights, into the 3D view, after its
transparent geometry: added to what is there (not its alpha), into the
target without its depth, which the shader reads (resolved, where it is
multisampled), in the target's format (display.hdr's half floats as the
SDR frame's bytes: their light goes on past white, an additive draw's, as
docs/hdr.md has it and Vulkan draws it); the macOS app draws no VR views (one
layer) */
static void stream_dynamic_lights_halos(void *context, const struct xgpu_stream_dynamic_lights_halos *record)
{
	static const float corners[6][2] =
	{
		{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f },
	};
	struct stream_target *color = target_get(record->color), *depth = target_get(record->depth);
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_sampler sampler;
	struct xgpu_metal_dynamic_state dynamic;
	struct xgpu_metal_halo_uniforms uniforms;
	struct halo_vertex *vertices;
	struct xgpu_metal_source source;
	uint32_t count, stage;
	int halo, corner, multisampled;

	(void)context;
	if (!color || !depth || (color->description.format != XGPU_METAL_FORMAT_BGRA8 &&
		color->description.format != XGPU_METAL_FORMAT_RGBA16F) || depth->description.format != XGPU_METAL_FORMAT_D24S8 || record->count <= 0 || record->layers > 1 ||
		color->description.depth_or_layers > 1 || !halo_shaders())
		return;
	multisampled = stream.samples != 0 && color->description.samples > 1;
	memset(&pipeline, 0, sizeof(pipeline));
	pipeline.vertex_shader = stream.halo_vertex;
	pipeline.pixel_shader = stream.halo_pixel;
	pipeline.attributes[0] = (struct xgpu_metal_vertex_attribute){ XGPU_METAL_ATTRIBUTE_FLOAT4, 0,
		(uint16_t)offsetof(struct halo_vertex, middle) };
	pipeline.attributes[1] = (struct xgpu_metal_vertex_attribute){ XGPU_METAL_ATTRIBUTE_FLOAT4, 0,
		(uint16_t)offsetof(struct halo_vertex, color) };
	pipeline.attributes[2] = (struct xgpu_metal_vertex_attribute){ XGPU_METAL_ATTRIBUTE_FLOAT2, 0,
		(uint16_t)offsetof(struct halo_vertex, corner) };
	pipeline.strides[0] = (uint16_t)sizeof(struct halo_vertex);
	pipeline.blend = 1;
	pipeline.blend_source = pipeline.blend_destination = XGPU_BLEND_ONE;
	pipeline.blend_operation = XGPU_BLEND_ADD;
	pipeline.color_mask = XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE;
	pipeline.color_format = color->description.format;
	pipeline.samples = (uint8_t)(multisampled ? color->description.samples : 1);
	if (stream.preparing)
	{
		/* (made as the map loads, as the game's pipelines are) */
		xgpu_metal_prepare(&pipeline);
		return;
	}
	count = (uint32_t)record->count;
	vertices = malloc(sizeof(*vertices) * count * 6);
	if (!vertices)
		return;
	for (halo = 0; halo < (int)count; halo++)
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
	source = xgpu_metal_stream(vertices, (uint32_t)sizeof(*vertices) * count * 6);
	free(vertices);
	if (source.kind == XGPU_METAL_SOURCE_NONE)
		return;
	/* the world's projection, the camera's axes, the screen conversion and
	the depth's parameters */
	memset(&uniforms, 0, sizeof(uniforms));
	memcpy(uniforms.projection, record->projection, sizeof(uniforms.projection));
	memcpy(uniforms.camera_axes[0], record->camera_right, sizeof(record->camera_right));
	memcpy(uniforms.camera_axes[1], record->camera_up, sizeof(record->camera_up));
	memcpy(uniforms.viewport_scale, record->viewport_scale, sizeof(uniforms.viewport_scale));
	memcpy(uniforms.viewport_offset, record->viewport_offset, sizeof(uniforms.viewport_offset));
	halo_depth_parameters(record, uniforms.depth_parameters);
	uniforms.screen_offset = record->screen_offset;

	/* the depth the scene was drawn with, in its texture; the colour target
	alone drawn into */
	xgpu_metal_texture_resolve(depth->texture);
	xgpu_metal_set_targets(color->texture, 0, 0, 0, multisampled);
	xgpu_metal_set_pipeline(&pipeline);
	/* (the viewport and depth range the 3D view was drawn with, and no
	scissor) */
	dynamic = stream.dynamic;
	memset(dynamic.scissor, 0, sizeof(dynamic.scissor));
	xgpu_metal_set_dynamic_state(&dynamic);
	/* (the depth read at the pixel: no filter) */
	memset(&sampler, 0, sizeof(sampler));
	sampler.mip_filter = XGPU_METAL_MIP_NONE;
	sampler.address_u = sampler.address_v = sampler.address_w = XGPU_METAL_ADDRESS_CLAMP;
	sampler.max_anisotropy = 1;
	for (stage = 0; stage < 4; stage++)
		xgpu_metal_set_texture(stage, stage ? 0 : depth->texture, &sampler);
	xgpu_metal_set_draw_uniforms(&uniforms, sizeof(uniforms));
	xgpu_metal_set_stream(0, source);
	color->written++;
	xgpu_metal_draw(XGPU_METAL_PRIMITIVE_TRIANGLES, count * 6, 0, 0,
		(struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 });
	stream_state_restore();
}

/* ---------- anti-aliasing (display.anti_aliasing; xgpu_metal_post.c) */

static void stream_anti_aliasing_prepare(void *context, uint32_t smaa)
{
	(void)context;
	/* (made as the setting is chosen: the front end chooses it between
	frames; logged if they cannot be) */
	(void)xgpu_metal_post_prepare(smaa != 0);
}

/* FXAA or SMAA over a window's 3D view, before the HUD and menus are drawn
over it: the target's texture (the front end no longer multisamples the
target, from just before the record) */
static void stream_anti_alias(void *context, const struct xgpu_stream_anti_alias *record)
{
	struct stream_target *target = target_get(record->target);
	uint32_t kind;

	(void)context;
	if (!target || (target->description.format != XGPU_METAL_FORMAT_BGRA8 &&
		target->description.format != XGPU_METAL_FORMAT_RGBA16F) || target->description.samples > 1 ||
		target->description.depth_or_layers > 1)
		return;
	if (stream.preparing)
	{
		/* (made as the map loads, as the game's pipelines are) */
		(void)xgpu_metal_post_prepare(record->smaa != 0);
		return;
	}
	target->written++;
	kind = xgpu_metal_pass_kind(XGPU_METAL_PASS_ANTI_ALIASING);
	xgpu_metal_post_anti_alias(record->smaa != 0, target->texture, target->description.format,
		target->description.width, target->description.height, record->corners);
	xgpu_metal_pass_kind(kind);
	stream_state_restore();
}

/* display.panini over a window's 3D view, after its anti-aliasing and
before the HUD: the target's texture. A multisampled target stays
multisampled, the HUD's draws going on into its samples: the pass reads its
pixels resolved and draws into its samples (the front end changes a
target's samples only between frames: made again mid-frame, it would lose
the view) */
static void stream_panini(void *context, const struct xgpu_stream_panini *record)
{
	struct stream_target *target = target_get(record->target);
	uint32_t kind;

	(void)context;
	if (!target || (target->description.format != XGPU_METAL_FORMAT_BGRA8 &&
		target->description.format != XGPU_METAL_FORMAT_RGBA16F) || target->description.depth_or_layers > 1)
		return;
	if (stream.preparing)
	{
		/* (made as the map loads, as the game's pipelines are) */
		(void)xgpu_metal_post_panini_prepare(target->description.samples);
		return;
	}
	target->written++;
	kind = xgpu_metal_pass_kind(XGPU_METAL_PASS_PANINI);
	xgpu_metal_post_panini(target->texture, target->description.format, target->description.width,
		target->description.height, target->description.samples, record->corners, record->projection);
	xgpu_metal_pass_kind(kind);
	stream_state_restore();
}

/* ---------- display.hdr (port/linux/src/xgpu_hdr.h)

The front end's state: whether the frame is drawn in HDR, the mode and
paper white, for the tone map here and the encoding (host_metal.m, which
knows the display: its headroom, the layer's encoding). */

static void stream_hdr(void *context, const struct xgpu_stream_hdr *record)
{
	(void)context;
	host_metal_hdr_state(record->active != 0, record->mode == XGPU_HDR_MODE_PUNCHY ? XGPU_HDR_MODE_PUNCHY :
		XGPU_HDR_MODE_FAITHFUL, (int)record->paper_white_nits, (int)record->paper_white, (int)record->ui_nits);
	/* (the tone map's function and its pipeline for the back buffer's pass,
	made as HDR is chosen, not in the middle of a frame) */
	if (record->active)
		(void)xgpu_metal_post_prepare_hdr(XGPU_METAL_FORMAT_D24S8, 0);
}

/* display.gamma: the presentation curves an SDR frame by it */
static void stream_gamma(void *context, uint32_t gamma)
{
	float value;

	(void)context;
	memcpy(&value, &gamma, sizeof(value));
	host_metal_gamma(value);
}

/* a window's 3D view tone mapped, in the pass that drew it, before the HUD
and the menus are drawn over it */
static void stream_tone_map(void *context, const struct xgpu_stream_tone_map *record)
{
	struct stream_target *target = target_get(record->target);
	struct stream_target *depth = stream.color == record->target ? target_get(stream.depth) : NULL;
	uint32_t samples = target ? target->description.samples : 0;
	struct xgpu_hdr_frame frame;
	float hdr[4];
	uint32_t kind;

	(void)context;
	if (!target || target->description.format != XGPU_METAL_FORMAT_RGBA16F || target->description.depth_or_layers > 1)
		return;
	if (depth && depth->description.samples != target->description.samples)
		depth = NULL;
	if (stream.preparing)
	{
		(void)xgpu_metal_post_prepare_hdr(depth ? depth->description.format : XGPU_METAL_FORMAT_NONE, samples);
		return;
	}
	host_metal_hdr_frame(&frame);
	hdr[0] = (float)frame.mode;
	hdr[1] = frame.peak;
	/* (the view's light over the UI's white) */
	hdr[2] = frame.ui;
	hdr[3] = 0.0f;
	target->written++;
	kind = xgpu_metal_pass_kind(XGPU_METAL_PASS_TONE_MAP);
	xgpu_metal_post_tone_map(target->texture, target->description.width, target->description.height,
		depth ? depth->texture : 0, depth ? depth->description.format : XGPU_METAL_FORMAT_NONE, samples,
		record->corners, hdr);
	xgpu_metal_pass_kind(kind);
	stream_state_restore();
}

/* display.ambient_occlusion "world": the structure's distance field's
slices (the decoder has checked their bounds and size) and each window's
block (xgpu_metal_world_ao.c) */
static void stream_world_ao_field(void *context, const struct xgpu_stream_world_ao_field *record)
{
	(void)context;
	xgpu_metal_world_ao_field(record->part, record->width, record->height, record->depth, record->z_first,
		record->z_count, record->width && record->size ? host_pointer(record->data) : NULL);
}

static void stream_world_ao_cascades(void *context, const struct xgpu_stream_world_ao_cascades *record)
{
	(void)context;
	xgpu_metal_world_ao_cascades(record->x, record->y, record->z, record->width, record->height, record->depth,
		record->width && record->size ? host_pointer(record->data) : NULL);
}

static void stream_world_ao_window(void *context, const struct xgpu_stream_world_ao_window *record)
{
	(void)context;
	xgpu_metal_world_ao_window(record->active ? &record->block : NULL);
}

/* ---------- display.ambient_occlusion "world" found once a pixel

xgpu_stream.h's WORLD_AO_PREPASS, as xgpu_vulkan_stream.c's: the window's
opaque surfaces drawn first as their twins into the pre-pass's inputs and
depth, the others skipped, the clears and the game's targets with them;
then the occlusion found from them at each 2x2 block's nearest pixel (or
each pixel's: the pass's camera[3].w), one draw of xgpu_msl_world_ao_pass's
functions, which the surfaces then read and upsample
(xgpu_metal_world_ao_set_pixels) until the window is done. The pass's
pipeline is made as a map's are (prepass_prepare); a twin's on the
compilers as first drawn, never waited for. */

static uint32_t prepass_texture(uint32_t texture, uint8_t format, uint32_t width, uint32_t height, uint32_t *made_width,
	uint32_t *made_height)
{
	struct xgpu_metal_texture_description description;

	if (texture && *made_width == width && *made_height == height)
		return texture;
	if (texture)
		xgpu_metal_texture_destroy(texture);
	memset(&description, 0, sizeof(description));
	description.type = XGPU_METAL_TEXTURE_2D;
	description.format = format;
	description.usage = XGPU_METAL_USAGE_SAMPLED | XGPU_METAL_USAGE_TARGET;
	description.width = (uint16_t)width;
	description.height = (uint16_t)height;
	description.depth_or_layers = 1;
	description.levels = 1;
	*made_width = width;
	*made_height = height;
	return xgpu_metal_texture_create(&description);
}

/* the pass's pipeline (0: its functions cannot be made) */
static int prepass_pipeline(struct xgpu_metal_pipeline *pipeline)
{
	if (prepass.failed)
		return 0;
	if (!prepass.vertex || !prepass.pixel)
	{
		if (!stream.shaders.world_ao_pass ||
			!stream.shaders.world_ao_pass(stream.shaders.context, &prepass.vertex, &prepass.pixel))
		{
			host_logf(HOST_LOG_WARN, "metal: display.ambient_occlusion \"world\"'s pass cannot be made: its surfaces "
				"read 1");
			prepass.failed = YES;
			return 0;
		}
	}
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->vertex_shader = prepass.vertex;
	pipeline->pixel_shader = prepass.pixel;
	pipeline->color_mask = XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE | XGPU_WRITE_ALPHA;
	pipeline->color_format = XGPU_METAL_FORMAT_RGBA32F;
	pipeline->samples = 1;
	return 1;
}

static void prepass_prepare(void)
{
	struct xgpu_metal_pipeline pipeline;

	if (prepass_pipeline(&pipeline))
		xgpu_metal_prepare(&pipeline);
}

/* the twins' definitions and entries, made once (NO: none can be) */
static BOOL prepass_tables(void)
{
	if (prepass.definitions)
		return YES;
	prepass.definitions = calloc(XGPU_STREAM_PIPELINES, sizeof(*prepass.definitions));
	prepass.entries = calloc(XGPU_STREAM_PIPELINES, sizeof(*prepass.entries));
	if (prepass.definitions && prepass.entries)
		return YES;
	free(prepass.definitions);
	free(prepass.entries);
	prepass.definitions = NULL;
	prepass.entries = NULL;
	return NO;
}

static BOOL prepass_draw_prepare(const struct xgpu_metal_vertex_attribute *attributes, const uint16_t *strides,
	const struct xgpu_metal_source *streams, const float (*constants)[4], uint16_t integers)
{
	uint32_t id = stream.pipeline_id, index;
	struct xgpu_stream_pipeline_define twin = *stream.definition;
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_stream_variant variant;
	struct stream_pipeline *entry;

	if (!prepass_tables() || !xgpu_stream_world_ao_twin(&twin))
		return NO;
	entry = &prepass.entries[id];
	if (memcmp(&twin, &prepass.definitions[id], sizeof(twin)))
	{
		prepass.definitions[id] = twin;
		memset(entry, 0, sizeof(*entry));
	}
	memset(&variant, 0, sizeof(variant));
	variant.integer_attributes = twin.key.immediate ? 0 : integers;
	memcpy(variant.border, stream.border, sizeof(variant.border));
	if (!pipeline_shaders(entry, &prepass.definitions[id], &variant))
		return NO;
	xgpu_metal_pipeline_from_key(&twin.key, &pipeline);
	pipeline.vertex_shader = entry->vertex_shader;
	pipeline.pixel_shader = entry->pixel_shader;
	memcpy(pipeline.attributes, attributes, sizeof(pipeline.attributes));
	memcpy(pipeline.strides, strides, sizeof(pipeline.strides));
	/* (made as the map is prepared; else on the compilers as first drawn,
	never waited for, and missed) */
	if (stream.preparing)
	{
		xgpu_metal_prepare(&pipeline);
		return NO;
	}
	if (!xgpu_metal_pipeline_made(&pipeline))
	{
		if (stream.prepared && !xgpu_metal_pipeline_known(&pipeline))
			pipeline_missed(&twin.key);
		xgpu_metal_prepare(&pipeline);
		return NO;
	}
	uniforms_update();
	textures_bind();
	xgpu_metal_set_pipeline(&pipeline);
	for (index = 0; index < XGPU_METAL_STREAM_COUNT; index++)
		xgpu_metal_set_stream(index, streams[index]);
	xgpu_metal_set_constant_attributes(constants);
	return YES;
}

static void stream_world_ao_prepass(void *context, const struct xgpu_stream_world_ao_prepass *record)
{
	struct stream_target *target = target_get(record->target);

	(void)context;
	/* (a map prepared: whether its draws are the pre-pass's in play, their
	twins made with them, draw_prepare) */
	if (stream.preparing)
	{
		prepass.twins = record->phase == XGPU_WORLD_AO_PREPASS_TWINS;
		return;
	}
	if (record->phase == XGPU_WORLD_AO_PREPASS_TWINS)
		return;
	if (record->phase == XGPU_WORLD_AO_PREPASS_BEGIN)
	{
		static const float zero[4];
		struct xgpu_metal_pipeline pipeline;

		/* (what the surfaces read: 1, until the pass finds it) */
		xgpu_metal_world_ao_set_pixels(0);
		if (!target || !prepass_pipeline(&pipeline) || !prepass_tables())
			return;
		{
			uint32_t width = prepass.width, height = prepass.height;

			prepass.inputs = prepass_texture(prepass.inputs, XGPU_METAL_FORMAT_RGBA32UI, target->description.width,
				target->description.height, &width, &height);
			width = prepass.width;
			height = prepass.height;
			prepass.depth = prepass_texture(prepass.depth, XGPU_METAL_FORMAT_D24S8, target->description.width,
				target->description.height, &width, &height);
			prepass.width = width;
			prepass.height = height;
		}
		if (!prepass.inputs || !prepass.depth)
			return;
		prepass.active = YES;
		prepass_targets_bind();
		xgpu_metal_clear(NULL, 0, XGPU_METAL_CLEAR_COLOR | XGPU_METAL_CLEAR_CHANNELS | XGPU_METAL_CLEAR_DEPTH |
			XGPU_METAL_CLEAR_STENCIL, zero, 1.0f, 0);
		return;
	}
	if (record->phase == XGPU_WORLD_AO_PREPASS_END && prepass.active)
	{
		const int32_t *corners = record->pass.corners;
		/* (found at a quarter of the pixels: each 2x2 block's) */
		int32_t scale = record->pass.camera[3][3] >= 2.0f ? 2 : 1, rectangle[4];
		uint32_t width = ((uint32_t)prepass.width + (uint32_t)scale - 1) / (uint32_t)scale;
		uint32_t height = ((uint32_t)prepass.height + (uint32_t)scale - 1) / (uint32_t)scale;
		struct xgpu_metal_pipeline pipeline;
		struct xgpu_metal_dynamic_state dynamic;
		struct xgpu_metal_sampler sampler;
		int axis;

		prepass.active = NO;
		for (axis = 0; axis < 2; axis++)
		{
			int32_t size = (int32_t)(axis ? prepass.height : prepass.width);
			int32_t low = corners[axis] < 0 ? 0 : corners[axis] > size ? size : corners[axis];
			int32_t high = corners[axis + 2] < low ? low : corners[axis + 2] > size ? size : corners[axis + 2];

			rectangle[axis] = low / scale;
			rectangle[axis + 2] = (high + scale - 1) / scale;
		}
		struct xgpu_world_ao_pass uniforms = record->pass;

		/* (the last pass's occlusion kept as the history, this one's drawn
		into the other: two in turn) */
		{
			uint32_t texture = prepass.history, made_width = prepass.history_width, made_height = prepass.history_height;

			prepass.history = prepass.pixels;
			prepass.history_width = prepass.pixels_width;
			prepass.history_height = prepass.pixels_height;
			prepass.pixels = texture;
			prepass.pixels_width = made_width;
			prepass.pixels_height = made_height;
		}
		prepass.pixels = prepass_texture(prepass.pixels, XGPU_METAL_FORMAT_RGBA32F, width, height, &prepass.pixels_width,
			&prepass.pixels_height);
		if (!prepass.history || prepass.history_width != width || prepass.history_height != height)
			uniforms.history[0] = 0.0f;
		if (prepass.pixels && rectangle[2] > rectangle[0] && rectangle[3] > rectangle[1] && prepass_pipeline(&pipeline))
		{
			if (!xgpu_metal_pipeline_made(&pipeline))
			{
				xgpu_metal_prepare(&pipeline);
			}
			else
			{
				uint32_t kind = xgpu_metal_pass_kind(XGPU_METAL_PASS_WORLD_AO);

				xgpu_metal_set_targets(prepass.pixels, 0, 0, 0, 0);
				xgpu_metal_set_pipeline(&pipeline);
				memset(&dynamic, 0, sizeof(dynamic));
				dynamic.viewport[0] = dynamic.scissor[0] = rectangle[0];
				dynamic.viewport[1] = dynamic.scissor[1] = rectangle[1];
				dynamic.viewport[2] = dynamic.scissor[2] = rectangle[2] - rectangle[0];
				dynamic.viewport[3] = dynamic.scissor[3] = rectangle[3] - rectangle[1];
				dynamic.depth_range[1] = 1.0f;
				xgpu_metal_set_dynamic_state(&dynamic);
				memset(&sampler, 0, sizeof(sampler));
				sampler.mip_filter = XGPU_METAL_MIP_NONE;
				sampler.address_u = sampler.address_v = sampler.address_w = XGPU_METAL_ADDRESS_CLAMP;
				sampler.max_anisotropy = 1;
				xgpu_metal_set_texture(0, prepass.inputs, &sampler);
				for (axis = 1; axis < 4; axis++)
					xgpu_metal_set_texture((uint32_t)axis, 0, &sampler);
				/* (the history, read where it is this window's; the
				distance-field scene's cascades) */
				if (uniforms.history[0] != 0.0f)
					xgpu_metal_set_texture(XGPU_METAL_WORLD_AO_PASS_HISTORY, prepass.history, &sampler);
				sampler.min_filter = sampler.mag_filter = XGPU_METAL_FILTER_LINEAR;
				xgpu_metal_set_texture(XGPU_METAL_WORLD_AO_PASS_CASCADES, xgpu_metal_world_ao_cascades_texture(), &sampler);
				xgpu_metal_set_draw_uniforms(&uniforms, sizeof(uniforms));
				xgpu_metal_draw(XGPU_METAL_PRIMITIVE_TRIANGLES, 3, 0, 0,
					(struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 });
				xgpu_metal_pass_kind(kind);
				xgpu_metal_world_ao_set_pixels(prepass.pixels);
			}
		}
		stream_state_restore();
		return;
	}
	/* (_NONE: the surfaces read 1) */
	if (prepass.active)
	{
		prepass.active = NO;
		stream_state_restore();
	}
	xgpu_metal_world_ao_set_pixels(0);
}

/* the game's renderer's category of the draws from now on
(XGPU_STREAM_CATEGORY's value: rasterizer.h's _rasterizer_profile_*), as the
kind of pass HALO_METAL_PASS_TIMES sums them under */
static void stream_category(void *context, uint32_t category)
{
	/* (_rasterizer_profile_clear to _screen_flash) */
	static const uint8_t kinds[29] =
	{
		XGPU_METAL_PASS_SKY, XGPU_METAL_PASS_SKY, XGPU_METAL_PASS_WORLD,
		/* (the environment's lightmaps to its reflections) */
		XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD,
		XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD,
		XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_WORLD,
		XGPU_METAL_PASS_WORLD,
		/* (its transparents and fog) */
		XGPU_METAL_PASS_TRANSPARENTS, XGPU_METAL_PASS_TRANSPARENTS, XGPU_METAL_PASS_TRANSPARENTS,
		/* (water and its decals, detail objects, the queued transparents) */
		XGPU_METAL_PASS_WATER, XGPU_METAL_PASS_WATER, XGPU_METAL_PASS_WORLD, XGPU_METAL_PASS_TRANSPARENTS,
		/* (the lens flares, the screen's effect; the HUD and the screen's flash) */
		XGPU_METAL_PASS_EFFECTS, XGPU_METAL_PASS_EFFECTS, XGPU_METAL_PASS_EFFECTS, XGPU_METAL_PASS_EFFECTS,
		XGPU_METAL_PASS_HUD, XGPU_METAL_PASS_HUD,
	};
	(void)context;
	xgpu_metal_pass_kind(category < sizeof(kinds) ? kinds[category] : XGPU_METAL_PASS_OTHER);
}

/* ---------- visibility tests, frames */

static void stream_visibility_begin(void *context)
{
	(void)context;
	xgpu_metal_visibility_begin();
}

static void stream_visibility_end(void *context, const struct xgpu_stream_visibility_end *record)
{
	(void)context;
	/* (the count written for the game is of its pixels: divided by the
	samples one of them is, as the GL backend's) */
	xgpu_metal_visibility_end(record->slot, record->area * (float)(record->samples > 1 ? record->samples : 1),
		record->test);
}

static void stream_present(void *context, uint32_t back_buffer)
{
	(void)context;
	stream.back_buffer = back_buffer;
}

static void prepass_prepare(void);

static void stream_prepare(void *context)
{
	(void)context;
	stream.preparing = YES;
	xgpu_metal_prepare_begin();
	/* (display.ambient_occlusion "world"'s pass's pipeline, as the map's are) */
	prepass_prepare();
}

static void stream_exiting(void *context)
{
	(void)context;
	xgpu_metal_exiting();
}

static uint32_t stream_prepared(void *context)
{
	(void)context;
	stream.preparing = NO;
	stream.prepared = YES;
	prepass.twins = NO;
	return (uint32_t)(xgpu_metal_prepared() * 1000.0 + 0.5);
}

static void stream_frame_end(void *context)
{
	(void)context;
	xgpu_metal_frame_end(target_texture(stream.back_buffer));
	stream.back_buffer = 0;
	xgpu_metal_textures_frame_end();
	/* (the GPU's time so far, for the guest: a report's) */
	host_render_stream_gpu_time(xgpu_metal_gpu_busy_nanoseconds());
	/* (each stage's texture looked for at least once a frame, so that the
	cache sees it used and keeps it: the guest sends the stages only when
	they change, and a frame of draws with the same ones sends none) */
	stream.textures_dirty = YES;
	/* (the dynamic lights the frame sent were in its ring) */
	if (stream.lights_none[0])
		lights_none_bind();
}

static void stream_open(void *context, void *ring, uint32_t ring_size, volatile uint32_t *visibility)
{
	(void)context;
	stream.ring = ring;
	stream.ring_size = ring_size;
	xgpu_metal_set_stream_ring(ring, ring_size);
	xgpu_metal_set_visibility_output(visibility);
}

static uint64_t stream_fence(void *context)
{
	(void)context;
	return xgpu_metal_fence();
}

static void stream_wait(void *context, uint64_t fence)
{
	(void)context;
	xgpu_metal_frame_wait(fence);
}

static int stream_fence_ready(void *context, uint64_t fence)
{
	(void)context;
	return xgpu_metal_frame_completed() >= fence;
}

static void stream_initialize(void *context, struct xgpu_stream_capabilities *capabilities)
{
	struct xgpu_metal_capabilities metal;

	(void)context;
	xgpu_metal_capabilities(&metal);
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->largest_target = (int32_t)metal.largest_target;
	capabilities->most_samples = (int32_t)metal.samples;
	capabilities->most_anisotropy = (int32_t)metal.anisotropy;
	/* (visibility counts from the visibility result buffer, not the pixel
	shaders) */
	capabilities->sample_counting = 0;
	capabilities->bgra_vertex_colors = 1;
	capabilities->base_vertex = 1;
	capabilities->sample_mask = 1;
	capabilities->mixed_constant_blend = 1;
	/* (the block and cells go in the frame's ring: nothing to make but
	their zeroed stand-ins) */
	capabilities->dynamic_lights = 1;
	capabilities->dynamic_lights_preparable = 1;
	/* (display.hdr: the half floats, the draws' tags blended in their
	shaders, reading the pixels they draw over, the tone map in place) */
	capabilities->hdr = 1;
	snprintf(capabilities->device, sizeof(capabilities->device), "%s", metal.device);
}

const struct host_render_backend *xgpu_metal_stream_backend(void)
{
	static struct host_render_backend backend;

	if (!backend.open)
	{
		stream.targets = [NSMutableDictionary new];
		stream.vertex_programs = [NSMutableDictionary new];
		stream.vertex_lightings = [NSMutableDictionary new];
		backend.open = stream_open;
		backend.fence = stream_fence;
		backend.wait = stream_wait;
		backend.fence_ready = stream_fence_ready;
		backend.initialize = stream_initialize;
		backend.target_create = stream_target_create;
		backend.target_delete = stream_target_delete;
		backend.target_multisample = stream_target_multisample;
		backend.set_targets = stream_set_targets;
		backend.clear = stream_clear;
		backend.clear_target = stream_clear_target;
		backend.read_target = stream_read_target;
		backend.set_textures = stream_set_textures;
		backend.set_constants = stream_set_constants;
		backend.vertex_program = stream_vertex_program;
		backend.vertex_lighting = stream_vertex_lighting;
		backend.pipeline_define = stream_pipeline_define;
		backend.prepared = stream_prepared;
		backend.exiting = stream_exiting;
		backend.prepare = stream_prepare;
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
		backend.frame_end = stream_frame_end;
		backend.loading = host_metal_loading;
		backend.loading_owned = host_metal_loading_owned;
		backend.loading_art = host_metal_loading_art;
		backend.loading_scene = host_metal_loading_scene;
		backend.loading_progress = host_metal_loading_progress;
		backend.loading_mode = host_metal_loading_mode;
		backend.loading_caption = host_metal_loading_caption;
		backend.dynamic_lights_prepare = stream_dynamic_lights_prepare;
		backend.dynamic_lights_upload = stream_dynamic_lights_upload;
		backend.dynamic_lights_change = stream_dynamic_lights_change;
		backend.dynamic_lights_halos = stream_dynamic_lights_halos;
		backend.dynamic_lights_world = stream_dynamic_lights_world;
		backend.anti_aliasing_prepare = stream_anti_aliasing_prepare;
		backend.anti_alias = stream_anti_alias;
		backend.hdr = stream_hdr;
		backend.tone_map = stream_tone_map;
		backend.gamma = stream_gamma;
		backend.category = stream_category;
		backend.panini = stream_panini;
		/* display.ambient_occlusion "world" (#578, docs/world-ao.md): the
		field's 3D texture (XGPU_METAL_TEXTURE_WORLD_SDF) and the window's
		block (XGPU_METAL_BUFFER_WORLD_AO): xgpu_metal_world_ao.c */
		backend.world_ao_field = stream_world_ao_field;
		backend.world_ao_cascades = stream_world_ao_cascades;
		backend.world_ao_window = stream_world_ao_window;
		backend.world_ao_prepass = stream_world_ao_prepass;
	}
	return &backend;
}

#endif
