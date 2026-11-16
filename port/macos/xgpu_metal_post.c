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
	/* display.panini's function could not be made */
	int panini_failed;
} post;

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
static void pipeline_describe(int function, uint8_t format, uint32_t samples, struct xgpu_metal_pipeline *pipeline)
{
	memset(pipeline, 0, sizeof(*pipeline));
	pipeline->vertex_shader = post.functions[XGPU_METAL_POST_VERTEX];
	pipeline->pixel_shader = post.functions[function];
	pipeline->color_mask = function == XGPU_METAL_POST_FXAA || function == XGPU_METAL_POST_SMAA_BLEND ?
		XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE :
		XGPU_WRITE_RED | XGPU_WRITE_GREEN | XGPU_WRITE_BLUE | XGPU_WRITE_ALPHA;
	/* (display.panini's into the target, all of it: destination alpha moves
	with its pixels) */
	pipeline->color_format = function == XGPU_METAL_POST_FXAA || function == XGPU_METAL_POST_SMAA_BLEND ||
		function == XGPU_METAL_POST_PANINI ? format : XGPU_METAL_FORMAT_BGRA8;
	/* (display.panini's into a multisampled target's samples, every sample
	of a pixel given its value) */
	pipeline->samples = (uint8_t)(samples > 1 ? samples : 1);
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
		preparation for all of them; the pass into the target in each of
		its formats, display.hdr's half floats too: pipeline_describe) */
		pipeline_describe(function, XGPU_METAL_FORMAT_BGRA8, 1, &pipeline);
		xgpu_metal_prepare(&pipeline);
		if (function == XGPU_METAL_POST_FXAA || function == XGPU_METAL_POST_SMAA_BLEND)
		{
			pipeline_describe(function, XGPU_METAL_FORMAT_RGBA16F, 1, &pipeline);
			xgpu_metal_prepare(&pipeline);
		}
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

/* a function's triangle drawn into a texture over the rectangle (into its
samples, where it has samples: a multisampled target's), sampling textures
(0: none) */
static void pass_draw_with(int function, uint32_t into, uint8_t format, uint32_t samples, const int32_t rectangle[4],
	uint32_t texture0, uint32_t texture1, uint32_t texture2, const float bounds[4], const float window[4],
	const float panini[8])
{
	struct xgpu_metal_pipeline pipeline;
	struct xgpu_metal_dynamic_state dynamic;
	struct xgpu_metal_post_uniforms uniforms;
	struct xgpu_metal_sampler sampler;
	const uint32_t textures[4] = { texture0, texture1, texture2, 0 };
	uint32_t stage;

	xgpu_metal_set_targets(into, 0, 0, 0, samples > 1);
	pipeline_describe(function, format, samples, &pipeline);
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
	if (window)
		memcpy(uniforms.window, window, sizeof(uniforms.window));
	if (panini)
	{
		memcpy(uniforms.panini, panini, sizeof(uniforms.panini));
		memcpy(uniforms.tangents, panini + 4, sizeof(uniforms.tangents));
	}
	xgpu_metal_set_draw_uniforms(&uniforms, sizeof(uniforms));
	xgpu_metal_draw(XGPU_METAL_PRIMITIVE_TRIANGLES, 3, 0, 0, (struct xgpu_metal_source){ XGPU_METAL_SOURCE_NONE, 0, 0, 0 });
}

static void pass_draw(int function, uint32_t into, uint8_t format, const int32_t rectangle[4], uint32_t texture0,
	uint32_t texture1, uint32_t texture2, const float bounds[4])
{
	pass_draw_with(function, into, format, 1, rectangle, texture0, texture1, texture2, bounds, NULL, NULL);
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

/* ---------- display.panini (port/linux/game/panini.c)

The window's 3D view drawn again through the Panini projection, as the
Vulkan backend draws it (xgpu_vulkan_post.c): the target's pixels copied,
then each pixel of the rectangle drawn from the copy where the rectilinear
view saw what the projection puts there, all four channels. A multisampled
target stays multisampled: its pixels resolved for the copy, and the pass
drawn into its samples, which the HUD is drawn into after and the frame's
end resolves (drawn into the resolved pixels alone, the pass would be lost
to that resolve). */

int xgpu_metal_post_panini_prepare(uint32_t samples)
{
	struct xgpu_metal_pipeline pipeline;

	if (post.panini_failed)
		return 0;
	if (!function_make(XGPU_METAL_POST_VERTEX) || !function_make(XGPU_METAL_POST_PANINI))
	{
		host_logf(HOST_LOG_WARN, "metal: the Panini projection's function cannot be made, so the 3D view is drawn flat");
		post.panini_failed = 1;
		return 0;
	}
	/* (made on the backend's queues: a draw waits for its own, a map's
	preparation for all of them) */
	pipeline_describe(XGPU_METAL_POST_PANINI, XGPU_METAL_FORMAT_BGRA8, samples, &pipeline);
	xgpu_metal_prepare(&pipeline);
	pipeline_describe(XGPU_METAL_POST_PANINI, XGPU_METAL_FORMAT_RGBA16F, samples, &pipeline);
	xgpu_metal_prepare(&pipeline);
	return 1;
}

int xgpu_metal_post_panini(uint32_t target, uint8_t format, uint32_t width, uint32_t height, uint32_t samples,
	const int32_t corners[4], const float projection[8])
{
	int32_t rectangle[4];
	float bounds[4], window[4];
	int index;

	if (!target || !width || !height || !xgpu_metal_post_panini_prepare(samples))
		return 0;
	/* (inside the target) */
	for (index = 0; index < 4; index++)
	{
		int32_t most = (int32_t)(index & 1 ? height : width);

		rectangle[index] = corners[index] < 0 ? 0 : corners[index] > most ? most : corners[index];
	}
	if (rectangle[2] <= rectangle[0] || rectangle[3] <= rectangle[1])
		return 1;
	/* (the copy, FXAA's) */
	if (!textures_fit(0, format, width, height))
		return 0;
	/* (a multisampled target's samples resolved into its texture, which is
	copied) */
	if (samples > 1)
		xgpu_metal_texture_resolve(target);
	xgpu_metal_texture_copy(target, post.color);
	/* (split screen: no texel of the next window is read) */
	bounds[0] = ((float)rectangle[0] + 0.5f) / (float)post.width;
	bounds[1] = ((float)rectangle[1] + 0.5f) / (float)post.height;
	bounds[2] = ((float)rectangle[2] - 0.5f) / (float)post.width;
	bounds[3] = ((float)rectangle[3] - 0.5f) / (float)post.height;
	/* (the view's own corners, which the tangents are at) */
	window[0] = (float)corners[0];
	window[1] = (float)corners[1];
	window[2] = (float)(corners[2] - corners[0]);
	window[3] = (float)(corners[3] - corners[1]);
	pass_draw_with(XGPU_METAL_POST_PANINI, target, format, samples, rectangle, post.color, 0, 0, bounds, window,
		projection);
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
