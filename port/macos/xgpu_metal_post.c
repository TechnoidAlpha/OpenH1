/*
XGPU_METAL_POST.C

The post-process passes of display.anti_aliasing (xgpu_metal_post.h): FXAA
and SMAA, run on a render target in place, over the rectangle of a window's
3D view, as the Vulkan backend runs them (port/linux/vulkan/xgpu_vulkan_post.c),
with the same shaders written as MSL (xgpu_metal_post_shaders.c, by
tools/metal_post_shaders.py). Each pass draws one triangle covering the
rectangle (its corners from the vertex's index, with no vertex data) and
finds its texels from the fragment's position; the target's rows are the
picture's from the top, as the passes' own textures' are, so no coordinate
is turned over. The target's pixels are copied first, so that no pass reads
what it writes, and only colour is written back: the game keeps values of
its own in destination alpha.

It draws through the backend's own functions (xgpu_metal.h): each pass is a
pass of the backend's, which ends the one before it (so that what one draws
is there for the next to sample). SMAA's edges and weights are emptied
whole as their passes begin (a clear of the whole attachment, which loads
none of it), so that no edge of another window's pass is found: they write
only where there is an edge. The intermediate textures are the target's
size (the shaders' coordinates are the target's).

The functions, pipelines and SMAA's lookup textures are made as the setting
is chosen (xgpu_metal_post_prepare), or asked for as a map is prepared, not
in the middle of a frame. The lookup textures are RGBA8 textures of SMAA's
one and two channels (the rest zero), sampled as an R8 and an RG8 texture
would be.
*/

#include "xgpu_metal.h"
#include "xgpu_metal_post.h"
#include "xgpu_metal_post_shaders.h"
#include "xgpu_stream.h"
#include "host.h"

#include <stdlib.h>
#include <string.h>

static struct
{
	uint32_t functions[XGPU_METAL_POST_FUNCTION_COUNT];
	/* FXAA's, and SMAA's, functions or textures could not be made */
	int failed[2];
	/* the size of the textures below, and color's format (the target's) */
	uint32_t width, height;
	uint8_t color_format;
	/* the target's pixels before the pass, and SMAA's edges and blending
	weights */
	uint32_t color, edges, weights;
	/* SMAA's lookup textures */
	uint32_t area, search;
	/* display.hdr's tone map's functions could not be made */
	int hdr_failed;
} post;

/* display.ambient_occlusion's (below): its functions could not be made
(logged once); the distances and the mask, of the target's size, and the
occlusion and its blur along x, of that over the preset's scale; the
occlusion last laid over a target, for its view alone, and its uniforms */
static struct
{
	int failed;
	uint32_t distances, mask, occlusion, blurred;
	uint32_t width, height, scale;
	int laid;
	struct xgpu_metal_post_uniforms laid_uniforms;
} occlusion;

/* ---------- textures */

static uint32_t texture_make(uint8_t format, uint8_t usage, uint32_t width, uint32_t height)
{
	struct xgpu_metal_texture_description description;

	memset(&description, 0, sizeof(description));
	description.type = XGPU_METAL_TEXTURE_2D;
	description.format = format;
	description.usage = usage;
	description.width = (uint16_t)width;
	description.height = (uint16_t)height;
	description.depth_or_layers = 1;
	description.levels = 1;
	return xgpu_metal_texture_create(&description);
}

/* a lookup texture of SMAA's, its channels' bytes (four to a word) each
made a texel of four */
static uint32_t lookup_texture(const uint32_t *words, uint32_t width, uint32_t height, uint32_t channels)
{
	uint32_t texture = texture_make(XGPU_METAL_FORMAT_RGBA8, XGPU_METAL_USAGE_SAMPLED, width, height);
	unsigned char *texels = malloc((size_t)width * height * 4);
	uint32_t texel, channel, byte;

	if (!texture || !texels)
	{
		free(texels);
		xgpu_metal_texture_destroy(texture);
		return 0;
	}
	for (texel = 0; texel < width * height; texel++)
	{
		for (channel = 0; channel < 4; channel++)
		{
			byte = texel * channels + channel;
			texels[texel * 4 + channel] = channel < channels ? (unsigned char)(words[byte / 4] >> (byte % 4 * 8)) : 0;
		}
	}
	xgpu_metal_texture_upload(texture, 0, 0, 0, 0, 0, width, height, 1, texels, width * 4, width * height * 4);
	free(texels);
	return texture;
}

/* the passes' textures, of the target's size (SMAA's only for SMAA); the
copy of the target's pixels in its format (display.hdr's half floats) */
static int textures_fit(int smaa, uint8_t format, uint32_t width, uint32_t height)
{
	const uint8_t usage = XGPU_METAL_USAGE_SAMPLED | XGPU_METAL_USAGE_TARGET;

	if (post.width != width || post.height != height || !post.color || post.color_format != format)
	{
		xgpu_metal_texture_destroy(post.color);
		xgpu_metal_texture_destroy(post.edges);
		xgpu_metal_texture_destroy(post.weights);
		post.color = post.edges = post.weights = 0;
		post.width = width;
		post.height = height;
		post.color_format = format;
		post.color = texture_make(format, usage, width, height);
	}
	if (smaa && !post.edges)
	{
		post.edges = texture_make(XGPU_METAL_FORMAT_BGRA8, usage, width, height);
		post.weights = texture_make(XGPU_METAL_FORMAT_BGRA8, usage, width, height);
	}
	return post.color && (!smaa || (post.edges && post.weights));
}

/* ---------- pipelines */

/* a pass's pipeline: into the target its colour alone; into an
intermediate, all of it; the target's format (display.hdr's half floats) */
static void pipeline_describe(int function, uint8_t format, struct xgpu_metal_pipeline *pipeline)
{
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->vertex_shader = post.functions[XGPU_METAL_POST_VERTEX];
	pipeline->pixel_shader = post.functions[function];
	pipeline->color_mask = function == XGPU_METAL_POST_FXAA || function == XGPU_METAL_POST_SMAA_BLEND ?
		XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE :
		XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE | XGPU_WRITE_ALPHA;
	pipeline->color_format = function == XGPU_METAL_POST_FXAA || function == XGPU_METAL_POST_SMAA_BLEND ? format :
		XGPU_METAL_FORMAT_BGRA8;
	pipeline->samples = 1;
}

static int prepare_failed(int smaa, const char *what)
{
	host_logf(HOST_LOG_WARN, "metal: %s's %s cannot be made, so the 3D view is not antialiased",
		smaa ? "SMAA" : "FXAA", what);
	post.failed[smaa] = 1;
	return 0;
}

/* a function, the app's library's or compiled on the backend's queues */
static int function_make(int function)
{
	const struct xgpu_metal_post_function *source = &xgpu_metal_post_functions[function];

	if (!post.functions[function])
		post.functions[function] = xgpu_metal_shader_create(source->source, source->size, source->entry);
	return post.functions[function] != 0;
}

int xgpu_metal_post_prepare(int smaa)
{
	int first = smaa ? XGPU_METAL_POST_SMAA_EDGES : XGPU_METAL_POST_FXAA;
	int last = smaa ? XGPU_METAL_POST_SMAA_BLEND : XGPU_METAL_POST_FXAA;
	int function;

	smaa = smaa != 0;
	if (post.failed[smaa])
		return 0;
	if (!function_make(XGPU_METAL_POST_VERTEX))
		return prepare_failed(smaa, "functions");
	for (function = first; function <= last; function++)
	{
		struct xgpu_metal_pipeline pipeline;

		if (!function_make(function))
			return prepare_failed(smaa, "functions");
		/* (made on the backend's queues: a draw waits for its own, a map's
		preparation for all of them) */
		pipeline_describe(function, XGPU_METAL_FORMAT_BGRA8, &pipeline);
		xgpu_metal_prepare(&pipeline);
	}
	if (smaa && !post.area)
	{
		post.area = lookup_texture(xgpu_metal_post_area_texture, XGPU_METAL_POST_AREA_WIDTH, XGPU_METAL_POST_AREA_HEIGHT, 2);
		post.search = lookup_texture(xgpu_metal_post_search_texture, XGPU_METAL_POST_SEARCH_WIDTH,
			XGPU_METAL_POST_SEARCH_HEIGHT, 1);
		if (!post.area || !post.search)
			return prepare_failed(smaa, "lookup textures");
	}
	return 1;
}

/* ---------- the passes */

/* a function's triangle drawn into a texture over the rectangle, sampling
textures (0: none) */
static void pass_draw(int function, uint32_t into, uint8_t format, const int32_t rectangle[4], uint32_t texture0,
	uint32_t texture1, uint32_t texture2, const float bounds[4])
{
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_dynamic_state dynamic;
	struct xgpu_metal_post_uniforms uniforms;
	struct xgpu_metal_sampler sampler;
	const uint32_t textures[4] = { texture0, texture1, texture2, 0 };
	uint32_t stage;

	xgpu_metal_set_targets(into, 0, 0, 0, 0);
	pipeline_describe(function, format, &pipeline);
	xgpu_metal_set_pipeline(&pipeline);
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.viewport[0] = dynamic.scissor[0] = rectangle[0];
	dynamic.viewport[1] = dynamic.scissor[1] = rectangle[1];
	dynamic.viewport[2] = dynamic.scissor[2] = rectangle[2] - rectangle[0];
	dynamic.viewport[3] = dynamic.scissor[3] = rectangle[3] - rectangle[1];
	dynamic.depth_range[1] = 1.0f;
	xgpu_metal_set_dynamic_state(&dynamic);
	/* (linear and clamped, as FXAA and SMAA sample) */
	memset(&sampler, 0, sizeof(sampler));
	sampler.min_filter = sampler.mag_filter = XGPU_METAL_FILTER_LINEAR;
	sampler.mip_filter = XGPU_METAL_MIP_NONE;
	sampler.address_u = sampler.address_v = sampler.address_w = XGPU_METAL_ADDRESS_CLAMP;
	sampler.max_anisotropy = 1;
	for (stage = 0; stage < 4; stage++)
		xgpu_metal_set_texture(stage, textures[stage], &sampler);
	memset(&uniforms, 0, sizeof(uniforms));
	uniforms.metrics[0] = 1.0f / (float)post.width;
	uniforms.metrics[1] = 1.0f / (float)post.height;
	uniforms.metrics[2] = (float)post.width;
	uniforms.metrics[3] = (float)post.height;
	if (bounds)
		memcpy(uniforms.bounds, bounds, sizeof(uniforms.bounds));
	xgpu_metal_set_draw_uniforms(&uniforms, sizeof(uniforms));
	xgpu_metal_draw(XGPU_METAL_PRIMITIVE_TRIANGLES, 3, 0, 0, (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 });
}

/* an intermediate emptied, all of it, as its pass begins, so that no edge
of another window's pass is found: SMAA's passes write only where there
is an edge */
static void intermediate_clear(uint32_t texture)
{
	static const float transparent[4];

	xgpu_metal_set_targets(texture, 0, 0, 0, 0);
	xgpu_metal_clear(NULL, 0, XGPU_METAL_CLEAR_COLOR | XGPU_METAL_CLEAR_CHANNELS, transparent, 1.0f, 0);
}

int xgpu_metal_post_anti_alias(int smaa, uint32_t target, uint8_t format, uint32_t width, uint32_t height,
	const int32_t corners[4])
{
	int32_t rectangle[4];
	int index;

	smaa = smaa != 0;
	/* (made already, as the setting was chosen) */
	if (!target || !width || !height || !xgpu_metal_post_prepare(smaa))
		return 0;
	/* (inside the target) */
	for (index = 0; index < 4; index++)
	{
		int32_t most = (int32_t)(index & 1 ? height : width);

		rectangle[index] = corners[index] < 0 ? 0 : corners[index] > most ? most : corners[index];
	}
	if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
		return 1;
	if (!textures_fit(smaa, format, width, height))
		return 0;

	/* the target's pixels, read while the target is drawn into */
	xgpu_metal_texture_copy(target, post.color);
	if (!smaa)
	{
		float bounds[4];

		/* (split screen: no texel of the next window is read) */
		bounds[0] = ((float)rectangle[0] + 0.5f) / (float)post.width;
		bounds[1] = ((float)rectangle[1] + 0.5f) / (float)post.height;
		bounds[2] = ((float)rectangle[2] - 0.5f) / (float)post.width;
		bounds[3] = ((float)rectangle[3] - 0.5f) / (float)post.height;
		pass_draw(XGPU_METAL_POST_FXAA, target, format, rectangle, post.color, 0, 0, bounds);
	}
	else
	{
		intermediate_clear(post.edges);
		pass_draw(XGPU_METAL_POST_SMAA_EDGES, post.edges, XGPU_METAL_FORMAT_BGRA8, rectangle, post.color, 0, 0, NULL);

		intermediate_clear(post.weights);
		pass_draw(XGPU_METAL_POST_SMAA_WEIGHTS, post.weights, XGPU_METAL_FORMAT_BGRA8, rectangle, post.edges, post.area,
			post.search, NULL);

		pass_draw(XGPU_METAL_POST_SMAA_BLEND, target, format, rectangle, post.color, post.weights, 0, NULL);
	}
	return 1;
}

/* ---------- display.hdr's tone map */

/* the tone map's pipeline: into the target's colour (its format, its
samples), with its depth attachment (the pass's, which it neither tests nor
writes) */
static void tone_map_pipeline(uint8_t depth_format, uint32_t samples, struct xgpu_metal_pipeline *pipeline)
{
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->vertex_shader = post.functions[XGPU_METAL_POST_VERTEX];
	pipeline->pixel_shader = post.functions[XGPU_METAL_POST_HDR_TONE_MAP];
	pipeline->color_mask = XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE;
	pipeline->color_format = XGPU_METAL_FORMAT_RGBA16F;
	pipeline->depth_format = depth_format;
	pipeline->samples = (uint8_t)(samples > 1 ? samples : 1);
}

int xgpu_metal_post_prepare_hdr(uint8_t depth_format, uint32_t samples)
{
	struct xgpu_metal_pipeline pipeline;

	if (post.hdr_failed)
		return 0;
	if (!function_make(XGPU_METAL_POST_VERTEX) || !function_make(XGPU_METAL_POST_HDR_TONE_MAP))
	{
		host_logf(HOST_LOG_WARN, "metal: the HDR tone map's functions cannot be made, so the 3D view is not tone mapped");
		post.hdr_failed = 1;
		return 0;
	}
	tone_map_pipeline(depth_format, samples, &pipeline);
	xgpu_metal_prepare(&pipeline);
	return 1;
}

int xgpu_metal_post_tone_map(uint32_t target, uint32_t width, uint32_t height, uint32_t depth, uint8_t depth_format,
	uint32_t samples, const int32_t corners[4], const float hdr[4])
{
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_dynamic_state dynamic;
	struct xgpu_metal_post_uniforms uniforms;
	int32_t rectangle[4];
	int index;

	if (!target || !width || !height || !xgpu_metal_post_prepare_hdr(depth_format, samples))
		return 0;
	for (index = 0; index < 4; index++)
	{
		int32_t most = (int32_t)(index & 1 ? height : width);

		rectangle[index] = corners[index] < 0 ? 0 : corners[index] > most ? most : corners[index];
	}
	if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
		return 1;
	/* (the pass drawing the 3D view goes on: the pixels are read where
	they are, in tile memory) */
	xgpu_metal_set_targets(target, 0, 0, depth, samples > 1);
	tone_map_pipeline(depth_format, samples, &pipeline);
	xgpu_metal_set_pipeline(&pipeline);
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.viewport[0] = dynamic.scissor[0] = rectangle[0];
	dynamic.viewport[1] = dynamic.scissor[1] = rectangle[1];
	dynamic.viewport[2] = dynamic.scissor[2] = rectangle[2] - rectangle[0];
	dynamic.viewport[3] = dynamic.scissor[3] = rectangle[3] - rectangle[1];
	dynamic.depth_range[1] = 1.0f;
	xgpu_metal_set_dynamic_state(&dynamic);
	memset(&uniforms, 0, sizeof(uniforms));
	memcpy(uniforms.hdr, hdr, sizeof(uniforms.hdr));
	xgpu_metal_set_draw_uniforms(&uniforms, sizeof(uniforms));
	xgpu_metal_draw(XGPU_METAL_PRIMITIVE_TRIANGLES, 3, 0, 0, (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 });
	return 1;
}

/* ---------- display.ambient_occlusion

The Vulkan backend's passes (xgpu_vulkan_post.c, which says what each
does), with its functions written as MSL: the distances, the mask (the
stencil's bits made a texture by draws tested against them), the occlusion
at half the resolution, its blur along x and then y, and its laying over
the target's colour. Each is a pass of the backend's: the target's pass
ends and begins again after them. A multisampled target has none. */

/* each quality's: the target's pixels to the occlusion's, the slices, the
steps each side, the radius in world units, the strength and the power (the
Vulkan backend's) */
static const struct
{
	uint32_t scale;
	float slices, steps, radius, strength, power;
} occlusion_presets[2] =
{
	{ 2, 2.0f, 4.0f, 0.5f, 0.5f, 1.0f },
	{ 2, 2.0f, 4.0f, 0.5f, 1.0f, 1.5f },
};

enum
{
	/* the occlusion laid over the light (a multiply), drawn alone, and the
	white drawn first under it alone; the mask's emptying and its objects'
	and kept pixels' channels */
	OCCLUSION_LAY,
	OCCLUSION_SHOW,
	OCCLUSION_WHITE,
	OCCLUSION_MASK_EMPTY,
	OCCLUSION_MASK_OBJECTS,
	OCCLUSION_MASK_KEPT,
	/* an intermediate's: function the pass's */
	OCCLUSION_INTERMEDIATE
};

/* a pass's pipeline: an intermediate's (RGBA16F, function its), over the
target (its format: OCCLUSION_LAY, _SHOW, _WHITE), or over the mask with the
depth target (its format), whose stencil is tested (OCCLUSION_MASK_*) */
static void occlusion_pipeline(int kind, int function, uint8_t format, uint8_t depth_format,
	struct xgpu_metal_pipeline *pipeline)
{
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->vertex_shader = post.functions[XGPU_METAL_POST_VERTEX];
	pipeline->pixel_shader = post.functions[kind == OCCLUSION_INTERMEDIATE ? function :
		kind == OCCLUSION_LAY || kind == OCCLUSION_SHOW ? XGPU_METAL_POST_OCCLUSION_APPLY : XGPU_METAL_POST_OCCLUSION_FILL];
	pipeline->color_mask = XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE;
	pipeline->color_format = format;
	pipeline->samples = 1;
	if (kind == OCCLUSION_INTERMEDIATE || kind >= OCCLUSION_MASK_EMPTY)
	{
		pipeline->color_format = XGPU_METAL_FORMAT_RGBA16F;
		pipeline->color_mask = kind == OCCLUSION_MASK_OBJECTS ? XGPU_WRITE_RED : kind == OCCLUSION_MASK_KEPT ?
			XGPU_WRITE_GREEN : XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE | XGPU_WRITE_ALPHA;
	}
	if (kind >= OCCLUSION_MASK_EMPTY && kind != OCCLUSION_INTERMEDIATE)
		pipeline->depth_format = depth_format;
	if (kind == OCCLUSION_MASK_OBJECTS || kind == OCCLUSION_MASK_KEPT)
	{
		pipeline->stencil_test = 1;
		pipeline->stencil_function = XGPU_COMPARE_NOT_EQUAL;
		pipeline->stencil_fail = pipeline->stencil_depth_fail = pipeline->stencil_pass = XGPU_STENCIL_KEEP;
		pipeline->stencil_read_mask = kind == OCCLUSION_MASK_OBJECTS ? XGPU_AMBIENT_OCCLUSION_OBJECT :
			XGPU_AMBIENT_OCCLUSION_KEEP;
	}
	if (kind == OCCLUSION_LAY)
	{
		pipeline->blend = 1;
		pipeline->blend_source = XGPU_BLEND_ZERO;
		pipeline->blend_destination = XGPU_BLEND_SOURCE_COLOR;
		pipeline->blend_operation = XGPU_BLEND_ADD;
	}
}

int xgpu_metal_post_ambient_occlusion_prepare(uint8_t format, uint8_t depth_format)
{
	struct xgpu_metal_pipeline pipeline;
	int function, kind;

	if (occlusion.failed)
		return 0;
	for (function = XGPU_METAL_POST_OCCLUSION_DEPTH; function <= XGPU_METAL_POST_OCCLUSION_FILL; function++)
	{
		if (!function_make(function) || !function_make(XGPU_METAL_POST_VERTEX))
		{
			host_logf(HOST_LOG_WARN, "metal: display.ambient_occlusion's functions cannot be made: the view has none");
			occlusion.failed = 1;
			return 0;
		}
	}
	for (function = XGPU_METAL_POST_OCCLUSION_DEPTH; function < XGPU_METAL_POST_OCCLUSION_APPLY; function++)
	{
		occlusion_pipeline(OCCLUSION_INTERMEDIATE, function, 0, 0, &pipeline);
		xgpu_metal_prepare(&pipeline);
	}
	for (kind = OCCLUSION_LAY; kind <= OCCLUSION_MASK_KEPT; kind++)
	{
		occlusion_pipeline(kind, 0, format, depth_format, &pipeline);
		xgpu_metal_prepare(&pipeline);
	}
	return 1;
}

/* the occlusion's textures for a target of a size, at a scale: 1 if they
are */
static int occlusion_textures_fit(uint32_t width, uint32_t height, uint32_t scale)
{
	const uint8_t usage = XGPU_METAL_USAGE_SAMPLED | XGPU_METAL_USAGE_TARGET;

	if (occlusion.width != width || occlusion.height != height || occlusion.scale != scale || !occlusion.distances)
	{
		xgpu_metal_texture_destroy(occlusion.distances);
		xgpu_metal_texture_destroy(occlusion.mask);
		xgpu_metal_texture_destroy(occlusion.occlusion);
		xgpu_metal_texture_destroy(occlusion.blurred);
		occlusion.width = width;
		occlusion.height = height;
		occlusion.scale = scale;
		occlusion.laid = 0;
		occlusion.distances = texture_make(XGPU_METAL_FORMAT_RGBA16F, usage, width, height);
		occlusion.mask = texture_make(XGPU_METAL_FORMAT_RGBA16F, usage, width, height);
		occlusion.occlusion = texture_make(XGPU_METAL_FORMAT_RGBA16F, usage, (width + scale - 1) / scale,
			(height + scale - 1) / scale);
		occlusion.blurred = texture_make(XGPU_METAL_FORMAT_RGBA16F, usage, (width + scale - 1) / scale,
			(height + scale - 1) / scale);
	}
	return occlusion.distances && occlusion.mask && occlusion.occlusion && occlusion.blurred;
}

/* a pass's triangle over a rectangle of a texture (and a depth target, for
its stencil), sampling up to three textures (single texels), with the
uniforms */
static void occlusion_draw(const struct xgpu_metal_pipeline *pipeline, uint32_t into, uint32_t depth,
	const int32_t rectangle[4], uint32_t texture0, uint32_t texture1, uint32_t texture2,
	const struct xgpu_metal_post_uniforms *uniforms)
{
	struct xgpu_metal_dynamic_state dynamic;
	struct xgpu_metal_sampler sampler;
	const uint32_t textures[4] = { texture0, texture1, texture2, 0 };
	uint32_t stage;

	xgpu_metal_set_targets(into, 0, 0, depth, 0);
	xgpu_metal_set_pipeline(pipeline);
	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.viewport[0] = dynamic.scissor[0] = rectangle[0];
	dynamic.viewport[1] = dynamic.scissor[1] = rectangle[1];
	dynamic.viewport[2] = dynamic.scissor[2] = rectangle[2] - rectangle[0];
	dynamic.viewport[3] = dynamic.scissor[3] = rectangle[3] - rectangle[1];
	dynamic.depth_range[1] = 1.0f;
	xgpu_metal_set_dynamic_state(&dynamic);
	memset(&sampler, 0, sizeof(sampler));
	sampler.min_filter = sampler.mag_filter = XGPU_METAL_FILTER_POINT;
	sampler.mip_filter = XGPU_METAL_MIP_NONE;
	sampler.address_u = sampler.address_v = sampler.address_w = XGPU_METAL_ADDRESS_CLAMP;
	sampler.max_anisotropy = 1;
	for (stage = 0; stage < 4; stage++)
		xgpu_metal_set_texture(stage, textures[stage], &sampler);
	xgpu_metal_set_draw_uniforms(uniforms, sizeof(*uniforms));
	xgpu_metal_draw(XGPU_METAL_PRIMITIVE_TRIANGLES, 3, 0, 0, (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 });
}

/* the corners inside a width and height: 0 if nothing is left */
static int occlusion_rectangle(const int32_t corners[4], uint32_t width, uint32_t height, int32_t rectangle[4])
{
	int index;

	for (index = 0; index < 4; index++)
	{
		int32_t most = (int32_t)(index & 1 ? height : width);

		rectangle[index] = corners[index] < 0 ? 0 : corners[index] > most ? most : corners[index];
	}
	return rectangle[2] > rectangle[0] && rectangle[3] > rectangle[1];
}

int xgpu_metal_post_ambient_occlusion(uint32_t target, uint8_t format, uint32_t width, uint32_t height, uint32_t depth,
	uint8_t depth_format, const int32_t corners[4], int quality, const float camera[6])
{
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_post_uniforms values, fill;
	int32_t rectangle[4], inner[4];
	uint32_t scale;
	int preset = quality == XGPU_AMBIENT_OCCLUSION_STRONG;
	int function;

	if (!target || !depth || !xgpu_metal_post_ambient_occlusion_prepare(format, depth_format) ||
		!occlusion_rectangle(corners, width, height, rectangle))
		return 0;
	scale = occlusion_presets[preset].scale;
	if (!occlusion_textures_fit(width, height, scale))
		return 0;
	/* (the view's rectangle in the occlusion's pixels) */
	inner[0] = rectangle[0] / (int32_t)scale;
	inner[1] = rectangle[1] / (int32_t)scale;
	inner[2] = (rectangle[2] + (int32_t)scale - 1) / (int32_t)scale;
	inner[3] = (rectangle[3] + (int32_t)scale - 1) / (int32_t)scale;
	memset(&values, 0, sizeof(values));
	values.metrics[2] = (float)((width + scale - 1) / scale);
	values.metrics[3] = (float)((height + scale - 1) / scale);
	values.metrics[0] = 1.0f / values.metrics[2];
	values.metrics[1] = 1.0f / values.metrics[3];
	values.bounds[0] = (float)inner[0];
	values.bounds[1] = (float)inner[1];
	values.bounds[2] = (float)inner[2];
	values.bounds[3] = (float)inner[3];
	memcpy(values.camera, camera + 2, sizeof(values.camera));
	values.occlusion[0] = camera[0];
	values.occlusion[1] = camera[1];
	values.occlusion[2] = occlusion_presets[preset].radius;
	values.occlusion[3] = occlusion_presets[preset].strength;
	values.sampling[0] = occlusion_presets[preset].slices;
	values.sampling[1] = occlusion_presets[preset].steps;
	values.sampling[2] = occlusion_presets[preset].power;
	values.sampling[3] = (float)scale;

	occlusion_pipeline(OCCLUSION_INTERMEDIATE, XGPU_METAL_POST_OCCLUSION_DEPTH, 0, 0, &pipeline);
	occlusion_draw(&pipeline, occlusion.distances, 0, rectangle, depth, 0, 0, &values);
	/* (the mask: emptied, then each kind's channel where the stencil has
	its bits) */
	memset(&fill, 0, sizeof(fill));
	for (function = OCCLUSION_MASK_EMPTY; function <= OCCLUSION_MASK_KEPT; function++)
	{
		if (function == OCCLUSION_MASK_OBJECTS)
			fill.metrics[0] = fill.metrics[1] = fill.metrics[2] = fill.metrics[3] = 1.0f;
		occlusion_pipeline(function, 0, format, depth_format, &pipeline);
		occlusion_draw(&pipeline, occlusion.mask, depth, rectangle, 0, 0, 0, &fill);
	}
	occlusion_pipeline(OCCLUSION_INTERMEDIATE, XGPU_METAL_POST_OCCLUSION, 0, 0, &pipeline);
	occlusion_draw(&pipeline, occlusion.occlusion, 0, inner, occlusion.distances, occlusion.mask, 0, &values);
	occlusion_pipeline(OCCLUSION_INTERMEDIATE, XGPU_METAL_POST_OCCLUSION_BLUR, 0, 0, &pipeline);
	values.sampling[0] = 1.0f;
	values.sampling[1] = 0.0f;
	occlusion_draw(&pipeline, occlusion.blurred, 0, inner, occlusion.occlusion, 0, 0, &values);
	values.sampling[0] = 0.0f;
	values.sampling[1] = 1.0f;
	occlusion_draw(&pipeline, occlusion.occlusion, 0, inner, occlusion.blurred, 0, 0, &values);
	values.sampling[0] = occlusion_presets[preset].slices;
	values.sampling[1] = occlusion_presets[preset].steps;
	occlusion_pipeline(OCCLUSION_LAY, 0, format, depth_format, &pipeline);
	occlusion_draw(&pipeline, target, 0, rectangle, occlusion.occlusion, occlusion.mask, occlusion.distances, &values);
	occlusion.laid = 1;
	occlusion.laid_uniforms = values;
	return 1;
}

int xgpu_metal_post_ambient_occlusion_show(uint32_t target, uint8_t format, uint32_t width, uint32_t height,
	const int32_t corners[4])
{
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_post_uniforms white;
	int32_t rectangle[4];

	if (!target || !occlusion.laid || width != occlusion.width || height != occlusion.height ||
		!occlusion_rectangle(corners, width, height, rectangle))
		return 0;
	memset(&white, 0, sizeof(white));
	white.metrics[0] = white.metrics[1] = white.metrics[2] = white.metrics[3] = 1.0f;
	occlusion_pipeline(OCCLUSION_WHITE, 0, format, 0, &pipeline);
	occlusion_draw(&pipeline, target, 0, rectangle, 0, 0, 0, &white);
	occlusion_pipeline(OCCLUSION_SHOW, 0, format, 0, &pipeline);
	occlusion_draw(&pipeline, target, 0, rectangle, occlusion.occlusion, occlusion.mask, occlusion.distances,
		&occlusion.laid_uniforms);
	occlusion.laid = 0;
	return 1;
}
