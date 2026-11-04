/*
HOST_VULKAN_VR.C

Vulkan's VR eye passes and composition. Poses, the late latch and OpenXR's
frame loop belong to host_vr_stream.h. The fixed programs are generated
from their GLSL (xgpu_vulkan_vr_glsl.inc), and compiled at build time.

The game's images keep their encoded colour values in UNORM targets. The
runtime's sRGB images receive the same bytes, without a second encoding.
All Vulkan images have their first row at the top.
*/

#include "host.h"
#include "host_render_stream.h"
#include "host_vr_stream.h"
#include "host_vr_render.h"
#include "host_vr_vulkan.h"
#include "host_vulkan_vr.h"
#include "xgpu_vulkan.h"
#include "xgpu_vulkan_stream.h"
#include "xgpu_vulkan_textures.h"
#include "xgpu_vulkan_vr_shaders.h"
#include "xgpu_shader.h"
#include "xgpu_spirv.h"
#include "vr.h"
#include "vr_visor.h"
#include "port_config.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FOVEA_BLEND 0.15f
#define VR_LIGHT_WIDTH 64
#define VR_LIGHT_HEIGHT 52
#define SCOPE_TEXT_LENGTH 28
#define COCKPIT_TEXT_LENGTH 44

static struct
{
	int ready, pass, sky, foveated;
	int eye_width, eye_height, hud_width, hud_height, scope_size;
	int fovea_width, fovea_height, corners[2][2], fovea_show;
	struct xgpu_vulkan_image *eyes, *fovea, *eye_copy, *eye_depth;
	struct xgpu_vulkan_image *hud, *scope, *light;
	struct xgpu_vulkan_image *panels[VR_PANEL_COUNT];
	struct xgpu_vulkan_image *groups[HALO_VR_HUD_GROUPS];
	unsigned int groups_drawn;
	uint32_t eye_target;
	unsigned int eye_token, hud_token, scope_token, panel_tokens[VR_PANEL_COUNT];
	int eyes_drawn, hud_drawn, scope_drawn, hud_panels, light_filled;
	float transforms[2][16], corrections[2][4];
	float tint[3], feed[8];
	int feed_set;
	int statistics, gpu_time;
	uint64_t marks[4];
	double spans[3], gpu_milliseconds, gpu_worst;
	unsigned int timed, gpu_frames, feed_seed, cockpit_seed;
} vr;

static uint64_t nanoseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static int color_format(int swapchain)
{
	VkFormat format = host_vr_vulkan_format(swapchain);

	return format == VK_FORMAT_R8G8B8A8_SRGB ?
		XGPU_VULKAN_FORMAT_RGBA8 : XGPU_VULKAN_FORMAT_BGRA8;
}

static struct xgpu_vulkan_image *image_make(int width, int height, int layers, int format)
{
	struct xgpu_vulkan_image_description description;

	memset(&description, 0, sizeof(description));
	description.type = layers > 1 ? XGPU_VULKAN_IMAGE_2D_ARRAY : XGPU_VULKAN_IMAGE_2D;
	description.format = format;
	description.usage = XGPU_VULKAN_USAGE_SAMPLED | XGPU_VULKAN_USAGE_TARGET;
	description.samples = 1;
	description.width = width;
	description.height = height;
	description.depth_or_layers = layers;
	description.levels = 1;
	return xgpu_vulkan_image_create(&description);
}

static struct xgpu_vulkan_image *image_keep(struct xgpu_vulkan_image **image, int width, int height,
	int layers, int format)
{
	const struct xgpu_vulkan_image_description *description = *image ? xgpu_vulkan_image_description(*image) : NULL;

	if (description && (description->width != (uint32_t)width || description->height != (uint32_t)height ||
		description->depth_or_layers != (uint32_t)layers || description->format != format))
	{
		xgpu_vulkan_image_destroy(*image);
		*image = NULL;
	}
	if (!*image && width > 0 && height > 0)
		*image = image_make(width, height, layers, format);
	return *image;
}

static void copy_eyes(void)
{
	int eye;

	if (!image_keep(&vr.eye_copy, vr.eye_width, vr.eye_height, 2, color_format(VR_SWAPCHAIN_EYES)))
		return;
	for (eye = 0; eye < 2; eye++)
		xgpu_vulkan_image_copy(vr.eyes, 0, eye, vr.eye_copy, 0, eye);
}

/* ---------- fixed composition programs */

struct fixed_draw
{
	const struct xgpu_vulkan_vr_program *program;
	struct xgpu_vulkan_pipeline_description pipeline;
	struct xgpu_vulkan_dynamic_state state;
	struct xgpu_vulkan_draw draw;
	unsigned char uniforms[2048];
};

static void uniform(struct fixed_draw *draw, const char *name, const void *values, int components, int count)
{
	const struct xgpu_vulkan_vr_program *program = draw->program;
	uint32_t index;

	for (index = 0; index < program->uniform_count; index++)
	{
		const struct xgpu_vulkan_vr_uniform *item = &program->uniforms[index];
		int element;

		if (strcmp(item->name, name))
			continue;
		if (components > item->components || count > item->count ||
			item->offset + (count - 1) * item->stride + components * 4 > sizeof(draw->uniforms))
		{
			host_logf(HOST_LOG_ERROR, "vulkan: invalid VR uniform %s", name);
			abort();
		}
		for (element = 0; element < count; element++)
			memcpy(draw->uniforms + item->offset + element * item->stride,
				(const unsigned char *)values + element * components * 4, components * 4);
		return;
	}
	host_logf(HOST_LOG_ERROR, "vulkan: missing VR uniform %s", name);
	abort();
}

static void uniform1f(struct fixed_draw *draw, const char *name, float value)
{
	uniform(draw, name, &value, 1, 1);
}

static void uniform1i(struct fixed_draw *draw, const char *name, int value)
{
	uniform(draw, name, &value, 1, 1);
}

static void uniform2f(struct fixed_draw *draw, const char *name, float x, float y)
{
	float value[2] = { x, y };

	uniform(draw, name, value, 2, 1);
}

static void uniform3(struct fixed_draw *draw, const char *name, const float value[3])
{
	float padded[4] = { value[0], value[1], value[2], 0.0f };

	uniform(draw, name, padded, 4, 1);
}

static int fixed_begin(struct fixed_draw *draw, int program, struct xgpu_vulkan_image *color,
	struct xgpu_vulkan_image *depth, uint32_t views)
{
	const struct xgpu_vulkan_image_description *target = color || depth ?
		xgpu_vulkan_image_description(color ? color : depth) : NULL;

	if (!target)
		return 0;
	memset(draw, 0, sizeof(*draw));
	draw->program = &xgpu_vulkan_vr_programs[program];
	if (!draw->program)
		return 0;
	draw->pipeline.vertex_shader = xgpu_vulkan_shader_constant(draw->program->vertex_words,
		draw->program->vertex_bytes / 4);
	draw->pipeline.pixel_shader = xgpu_vulkan_shader_constant(draw->program->fragment_words,
		draw->program->fragment_bytes / 4);
	draw->pipeline.primitive_class = XGPU_VULKAN_CLASS_TRIANGLES;
	draw->pipeline.color_format = color ? xgpu_vulkan_image_description(color)->format : XGPU_VULKAN_FORMAT_NONE;
	draw->pipeline.depth_format = depth ? xgpu_vulkan_image_description(depth)->format : XGPU_VULKAN_FORMAT_NONE;
	draw->pipeline.samples = 1;
	draw->pipeline.view_mask = views > 1 ? (1u << views) - 1u : 0;
	draw->pipeline.color_mask = 15;
	draw->state.viewport[2] = draw->state.scissor[2] = target->width;
	draw->state.viewport[3] = draw->state.scissor[3] = target->height;
	draw->state.depth_range[1] = 1.0f;
	draw->draw.dynamic = &draw->state;
	draw->draw.topology = XGPU_VULKAN_TOPOLOGY_TRIANGLES;
	draw->draw.count = 3;
	uniform1f(draw, "framebuffer_height", (float)target->height);
	xgpu_vulkan_set_target_views(color, depth, 0, views);
	return 1;
}

static void texture(struct fixed_draw *draw, int stage, struct xgpu_vulkan_image *image, int kind,
	int filter, int address)
{
	struct xgpu_vulkan_sampler_description sampler;

	memset(&sampler, 0, sizeof(sampler));
	sampler.minify = sampler.magnify = filter;
	sampler.address[0] = sampler.address[1] = sampler.address[2] = address;
	sampler.anisotropy = 1;
	if (kind == XGPU_VULKAN_IMAGE_3D)
		sampler.mip = XGPU_VULKAN_MIP_LINEAR;
	draw->draw.textures[stage].image = image;
	draw->draw.textures[stage].kind = kind;
	draw->draw.textures[stage].sampler = xgpu_vulkan_sampler(&sampler);
}

static void blend(struct fixed_draw *draw, int source, int destination)
{
	draw->pipeline.blend = 1;
	draw->pipeline.blend_source = source;
	draw->pipeline.blend_destination = destination;
	draw->pipeline.blend_operation = XGPU_BLEND_ADD;
}

static void fixed_end(struct fixed_draw *draw)
{
	void *constants, *uniforms;

	draw->draw.pipeline = xgpu_vulkan_pipeline(&draw->pipeline);
	constants = xgpu_vulkan_upload(3072, 256, &draw->draw.constants);
	uniforms = xgpu_vulkan_upload(sizeof(draw->uniforms), 256, &draw->draw.uniforms);
	if (!constants || !uniforms || !draw->draw.pipeline)
		return;
	memset(constants, 0, 3072);
	memcpy(uniforms, draw->uniforms, sizeof(draw->uniforms));
	xgpu_vulkan_draw(&draw->draw);
}

static void fovea_place(void)
{
	float centres[2][2];
	int eye, axis;
	int full[2] = { vr.eye_width, vr.eye_height };
	int inset[2] = { vr.fovea_width, vr.fovea_height };

	if (!halo_vr_foveation(centres))
		centres[0][0] = centres[0][1] = centres[1][0] = centres[1][1] = 0.5f;
	for (eye = 0; eye < 2; eye++)
	{
		for (axis = 0; axis < 2; axis++)
		{
			int corner = (int)floorf(centres[eye][axis] * full[axis] - inset[axis] * 0.5f + 0.5f);

			vr.corners[eye][axis] = corner < 0 ? 0 : corner > full[axis] - inset[axis] ?
				full[axis] - inset[axis] : corner;
		}
	}
}

static void fovea_compose(void)
{
	struct fixed_draw draw;
	float insets[2][4], open[2][4], image[4];
	int eye;

	if (!vr.foveated || !vr.eyes_drawn || !vr.eye_token ||
		!fixed_begin(&draw, XGPU_VULKAN_VR_FOVEA, vr.eyes, NULL, 2))
		return;
	for (eye = 0; eye < 2; eye++)
	{
		const int *corner = vr.corners[eye];

		insets[eye][0] = (float)corner[0];
		insets[eye][1] = (float)corner[1];
		insets[eye][2] = (float)vr.fovea_width;
		insets[eye][3] = (float)vr.fovea_height;
		open[eye][0] = corner[0] <= 0 ? 1.0f : 0.0f;
		open[eye][1] = corner[1] <= 0 ? 1.0f : 0.0f;
		open[eye][2] = corner[0] + vr.fovea_width >= vr.eye_width ? 1.0f : 0.0f;
		open[eye][3] = corner[1] + vr.fovea_height >= vr.eye_height ? 1.0f : 0.0f;
	}
	image[0] = (float)vr.eye_width;
	image[1] = (float)vr.eye_height;
	image[2] = FOVEA_BLEND * (float)(vr.fovea_width < vr.fovea_height ? vr.fovea_width : vr.fovea_height);
	image[3] = 0.0f;
	uniform(&draw, "inset", insets, 4, 2);
	uniform(&draw, "open", open, 4, 2);
	uniform(&draw, "image", image, 4, 1);
	uniform1f(&draw, "show", vr.fovea_show ? 1.0f : 0.0f);
	texture(&draw, 0, vr.fovea, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_POINT, XGPU_VULKAN_ADDRESS_CLAMP);
	fixed_end(&draw);
}

static struct xgpu_vulkan_image *light_map(const struct vr_visor_hud_image *look)
{
	struct fixed_draw draw;

	if (look->react <= 0.0f || !look->curve)
		return NULL;
	if (!vr.eyes_drawn)
		return vr.light_filled ? vr.light : NULL;
	if (!image_keep(&vr.light, VR_LIGHT_WIDTH, VR_LIGHT_HEIGHT, 1, XGPU_VULKAN_FORMAT_RGBA16F) ||
		!fixed_begin(&draw, XGPU_VULKAN_VR_LIGHT, vr.light, NULL, 1))
		return NULL;
	uniform2f(&draw, "size", VR_LIGHT_WIDTH, VR_LIGHT_HEIGHT);
	uniform(&draw, "rows", look->eye, 4, 6);
	uniform(&draw, "eye_fov", look->eye_fov, 4, 2);
	uniform(&draw, "reach", look->light_reach, 4, 1);
	uniform1f(&draw, "blend", vr.light_filled ? look->light_blend : 1.0f);
	texture(&draw, 0, vr.eyes, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_CLAMP);
	if (vr.light_filled)
		blend(&draw, XGPU_BLEND_SOURCE_ALPHA, XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA);
	fixed_end(&draw);
	vr.light_filled = 1;
	return vr.light;
}

static int copy_hud_image(struct xgpu_vulkan_image *source, struct xgpu_vulkan_image *destination,
	const struct vr_visor_hud_image *look, const struct vr_visor_panel *panel, struct xgpu_vulkan_image *light)
{
	struct fixed_draw draw;
	const struct xgpu_vulkan_image_description *size = xgpu_vulkan_image_description(destination);
	static const float none[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

	if (!fixed_begin(&draw, XGPU_VULKAN_VR_HUD, destination, NULL, 1))
		return 0;
	uniform2f(&draw, "size", (float)size->width, (float)size->height);
	uniform2f(&draw, "half_angles", look->half_angles[0], look->half_angles[1]);
	uniform1f(&draw, "opacity", look->opacity);
	uniform1f(&draw, "glow", look->curve ? look->glow : 0.0f);
	uniform1f(&draw, "glass", look->curve ? look->glass : 0.0f);
	uniform1f(&draw, "react", look->curve && light ? look->react : 0.0f);
	uniform(&draw, "reach", look->light_reach, 4, 1);
	uniform2f(&draw, "hit", look->curve ? look->hit : 0.0f, look->hit_pattern);
	uniform1f(&draw, "warning", look->curve ? look->warning : 0.0f);
	if (panel)
	{
		uniform1f(&draw, "curve", 1.0f);
		uniform2f(&draw, "extent", panel->half_size[0], panel->half_size[1]);
		uniform3(&draw, "centre", panel->centre);
		uniform3(&draw, "right_axis", panel->right);
		uniform3(&draw, "up_axis", panel->up);
		uniform(&draw, "region", panel->region, 4, 1);
		uniform3(&draw, "wrap", panel->wrap);
	}
	else
	{
		static const float ahead[3] = { 0.0f, 0.0f, -1.0f }, across[3] = { 1.0f, 0.0f, 0.0f };
		static const float up[3] = { 0.0f, 1.0f, 0.0f }, straight[3] = { 0.0f, 0.0f, 0.0f };

		uniform1f(&draw, "curve", (float)look->curve);
		uniform2f(&draw, "extent", look->extent[0], look->extent[1]);
		uniform3(&draw, "centre", ahead);
		uniform3(&draw, "right_axis", across);
		uniform3(&draw, "up_axis", up);
		uniform(&draw, "region", none, 4, 1);
		uniform3(&draw, "wrap", straight);
	}
	texture(&draw, 0, source, XGPU_VULKAN_IMAGE_2D, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_BORDER);
	texture(&draw, 1, light, XGPU_VULKAN_IMAGE_2D, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_CLAMP);
	fixed_end(&draw);
	return 1;
}

static void hud_compose(struct xgpu_vulkan_image *hud)
{
	struct vr_visor_hud_image look;
	struct vr_visor_panel panels[VR_PANEL_COUNT];
	struct xgpu_vulkan_image *light;
	int panel;

	vr_visor_hud_image(&look);
	light = light_map(&look);
	vr.hud_token = halo_vr_image(VR_SWAPCHAIN_HUD);
	if (!vr.hud_token || !image_keep(&vr.hud, vr.hud_width, vr.hud_height, 1, color_format(VR_SWAPCHAIN_HUD)) ||
		!copy_hud_image(hud, vr.hud, &look, NULL, light))
		vr.hud_token = 0;
	if (vr.hud_panels && vr_visor_panels(panels))
	{
		for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		{
			int group = panels[panel].group, width, height;
			unsigned int token;

			halo_vr_panel_size(panel, &width, &height);
			if (group < 0 || group >= HALO_VR_HUD_GROUPS || !(vr.groups_drawn & (1u << group)) ||
				!vr.groups[group] || width <= 0)
				continue;
			/* Acquisition marks a panel for submission in the shared stream.
			Do it only once this frame's pixels have been made successfully. */
			if (image_keep(&vr.panels[panel], width, height, 1, color_format(VR_SWAPCHAIN_PANEL + panel)) &&
				copy_hud_image(vr.groups[group], vr.panels[panel], &look, &panels[panel], light) &&
				(token = halo_vr_image(VR_SWAPCHAIN_PANEL + panel)))
				vr.panel_tokens[panel] = token;
		}
	}
}

static void text_glyphs(const char *line, int *text, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		static const char letters[] = "XRANGEMO/.- ";
		char letter = index < (int)strlen(line) ? line[index] : 0;
		const char *found = letter ? strchr(letters, letter) : NULL;

		text[index] = letter >= '0' && letter <= '9' ? letter - '0' : found ? 10 + (int)(found - letters) : -1;
	}
}

static int copy_scope(struct xgpu_vulkan_image *source)
{
	struct fixed_draw draw;
	float zoom_height, reticle[2], range, animation[4] = { 0.0f }, corner, dim, feather;
	int zoom = (int)(halo_vr_scope_zoom(&zoom_height) + 0.5f);
	int smartlink = halo_vr_scope_smartlink(reticle), rounds_loaded, rounds_carried;
	struct halo_vr_scope_window window;
	int text[SCOPE_TEXT_LENGTH];
	char line[SCOPE_TEXT_LENGTH + 1], range_text[16], rounds_text[16];

	if (!image_keep(&vr.scope, vr.scope_size, vr.scope_size, 1, color_format(VR_SWAPCHAIN_SCOPE)) ||
		!fixed_begin(&draw, XGPU_VULKAN_VR_SCOPE, vr.scope, NULL, 1))
		return 0;
	smartlink = smartlink && halo_vr_scope_window(&window);
	halo_vr_scope_readout(&range, &rounds_loaded, &rounds_carried);
	if (range > 0.0f && range < 999.9f)
		snprintf(range_text, sizeof(range_text), "%05.1f", range);
	else
		snprintf(range_text, sizeof(range_text), "---.-");
	rounds_text[0] = 0;
	if (rounds_loaded >= 0)
		snprintf(rounds_text, sizeof(rounds_text), "   AMMO %d/%d", rounds_loaded, rounds_carried < 0 ? 0 : rounds_carried);
	snprintf(line, sizeof(line), "%dX   RANGE %s%s", zoom < 0 ? 0 : zoom > 99 ? 99 : zoom, range_text, rounds_text);
	text_glyphs(line, text, SCOPE_TEXT_LENGTH);
	uniform1f(&draw, "size", (float)vr.scope_size);
	uniform1i(&draw, "zoom", zoom < 0 ? 0 : zoom > 99 ? 99 : zoom);
	uniform1f(&draw, "zoom_cell", fmaxf(floorf((float)vr.scope_size * zoom_height / 5.0f), 1.0f));
	uniform1i(&draw, "smartlink", smartlink);
	uniform2f(&draw, "window", smartlink ? window.window[0] : 0.0f,
		smartlink ? window.window[1] * window.half[1] / window.half[0] : 0.0f);
	uniform1f(&draw, "height", smartlink ? window.half[1] / window.half[0] : 1.0f);
	uniform(&draw, "text", text, 1, SCOPE_TEXT_LENGTH);
	halo_vr_scope_animation(&animation[0], &animation[1], &animation[2]);
	uniform(&draw, "animation", animation, 4, 1);
	halo_vr_scope_shape(&corner, &dim, &feather);
	uniform1f(&draw, "corner", smartlink ? corner : -1.0f);
	uniform1f(&draw, "feather", feather);
	uniform1f(&draw, "text_cell", smartlink ?
		fmaxf(floorf((float)vr.scope_size * window.window[0] * 0.0036f + 0.5f), 1.0f) : 1.0f);
	texture(&draw, 0, source, XGPU_VULKAN_IMAGE_2D, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_CLAMP);
	fixed_end(&draw);
	return 1;
}

static void scope_window(struct xgpu_vulkan_image *depth, int depth_layers)
{
	struct fixed_draw draw;
	struct halo_vr_scope_window window;
	float centre[2][4] = { { 0 } }, right[2][4] = { { 0 } }, up[2][4] = { { 0 } }, corner, dim, feather;
	int gun = halo_vr_scope_gun(), eye;

	if (!vr.eyes_drawn || !vr.scope || !halo_vr_scope_window(&window) ||
		!fixed_begin(&draw, XGPU_VULKAN_VR_SCOPE_WINDOW, vr.eyes, NULL, 2))
		return;
	for (eye = 0; eye < 2; eye++)
	{
		memcpy(centre[eye], window.centre[eye], sizeof(window.centre[eye]));
		memcpy(right[eye], window.right[eye], sizeof(window.right[eye]));
		memcpy(up[eye], window.up[eye], sizeof(window.up[eye]));
	}
	uniform(&draw, "centre", centre, 4, 2);
	uniform(&draw, "right", right, 4, 2);
	uniform(&draw, "up", up, 4, 2);
	uniform(&draw, "fov", window.fov, 4, 2);
	uniform2f(&draw, "half_size", window.half[0], window.half[1]);
	uniform1f(&draw, "height", window.half[1] / window.half[0]);
	uniform2f(&draw, "image", (float)vr.eye_width, (float)vr.eye_height);
	uniform1i(&draw, "gun", depth ? gun : 0);
	uniform1f(&draw, "gun_depth", window.far_z > window.near_z && 1.2f > window.near_z ?
		window.far_z / (window.far_z - window.near_z) * (1.0f - window.near_z / 1.2f) : 0.0f);
	uniform1i(&draw, "layers", depth_layers);
	halo_vr_scope_shape(&corner, &dim, &feather);
	uniform1f(&draw, "dim", dim);
	texture(&draw, 0, vr.scope, XGPU_VULKAN_IMAGE_2D, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_CLAMP);
	texture(&draw, 1, depth, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_POINT, XGPU_VULKAN_ADDRESS_CLAMP);
	blend(&draw, XGPU_BLEND_ONE, XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA);
	draw.pipeline.color_mask = 7;
	fixed_end(&draw);
}

static void visor(void)
{
	struct fixed_draw draw;
	struct vr_visor_image look;
	struct xgpu_vulkan_image *noise_textures[2] = { NULL, NULL };
	float rim[4], plasma[4] = { 0.0f }, noise[2][4], scale[4] = { 0.0f };
	int map;

	if (!vr.eyes_drawn)
		return;
	vr_visor_image(&look);
	if (!look.shown)
		return;
	copy_eyes();
	if (!vr.eye_copy || !fixed_begin(&draw, XGPU_VULKAN_VR_VISOR, vr.eyes, NULL, 2))
		return;
	if (look.plasma)
	{
		for (map = 0; map < 2; map++)
		{
			struct xgpu_vulkan_image *image = xgpu_vulkan_textures_header((uint32_t)(uintptr_t)look.noise_texture[map]);

			if (image && xgpu_vulkan_image_description(image)->type == XGPU_VULKAN_IMAGE_3D)
				noise_textures[map] = image;
		}
		if (!noise_textures[0] || !noise_textures[1])
			look.plasma = 0;
	}
	uniform(&draw, "rows", look.eye, 4, 6);
	uniform(&draw, "eye_fov", look.eye_fov, 4, 2);
	uniform1f(&draw, "distance", VR_GLASS_DISTANCE);
	uniform(&draw, "extent", look.extent, 4, 1);
	uniform(&draw, "fov", look.fov, 4, 1);
	uniform(&draw, "clear", look.clear, 4, 1);
	rim[0] = look.glow[0];
	rim[1] = look.glow[1];
	rim[2] = look.glow[2];
	rim[3] = look.frame;
	if (look.plasma)
	{
		plasma[0] = look.plasma_glow;
		plasma[1] = look.charge;
		plasma[2] = look.full;
		plasma[3] = look.energy;
	}
	for (map = 0; map < 2; map++)
	{
		float cycles = look.noise_period[map] > 0.0f ? look.time / look.noise_period[map] : 0.0f;

		memcpy(noise[map], look.noise_direction[map], 3 * sizeof(float));
		noise[map][3] = cycles - floorf(cycles);
		scale[map] = look.noise_scale[map];
		texture(&draw, map, noise_textures[map], XGPU_VULKAN_IMAGE_3D,
			XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_WRAP);
	}
	uniform(&draw, "rim", rim, 4, 1);
	uniform(&draw, "plasma", plasma, 4, 1);
	uniform1f(&draw, "level", look.charge_level);
	uniform3(&draw, "perpendicular", look.perpendicular);
	uniform3(&draw, "parallel", look.parallel);
	uniform(&draw, "noise", noise, 4, 2);
	uniform(&draw, "noise_scale", scale, 4, 1);
	/* The visor alone blends linear light into the encoded eyes. Its
	shader decodes the copy, blends, then encodes exactly once. */
	texture(&draw, 2, vr.eye_copy, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_POINT, XGPU_VULKAN_ADDRESS_CLAMP);
	draw.pipeline.color_mask = 7;
	fixed_end(&draw);
}

static void rim(void)
{
	struct fixed_draw draw;
	struct vr_visor_rim look;
	struct xgpu_vulkan_image *depth = vr.foveated ? NULL : vr.eye_depth;

	if (!vr.eyes_drawn || !vr_visor_rim(&look))
		return;
	if (depth && xgpu_vulkan_image_description(depth)->depth_or_layers != 2)
		depth = NULL;
	if (!fixed_begin(&draw, XGPU_VULKAN_VR_RIM, vr.eyes, depth, 2))
		return;
	uniform(&draw, "rows", look.eye, 4, 6);
	uniform(&draw, "fov", look.fov, 4, 2);
	uniform(&draw, "edges", look.edges, 4, 1);
	uniform1f(&draw, "strength", look.strength);
	uniform1f(&draw, "distance", look.distance);
	uniform1f(&draw, "reach", look.reach);
	uniform1f(&draw, "depth_pass", 0.0f);
	blend(&draw, XGPU_BLEND_ONE, XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA);
	draw.pipeline.color_mask = 7;
	draw.draw.count = (4 - 1) * 128 * 6;
	fixed_end(&draw);
	if (depth)
	{
		uniform1f(&draw, "depth_pass", 1.0f);
		draw.pipeline.blend = 0;
		draw.pipeline.color_mask = 0;
		draw.pipeline.depth_test = draw.pipeline.depth_write = 1;
		draw.pipeline.depth_function = XGPU_COMPARE_ALWAYS;
		/* (the nearest depth, the game's 0: the range its alone, which the
		core turns with the depth it keeps reversed, xgpu_vulkan.h) */
		draw.state.depth_range[1] = 0.0f;
		fixed_end(&draw);
	}
}

static void feed(void)
{
	struct fixed_draw draw;

	if (!vr.feed_set)
		return;
	vr.feed_set = 0;
	if (!vr.eyes_drawn)
		return;
	copy_eyes();
	if (!vr.eye_copy || !fixed_begin(&draw, XGPU_VULKAN_VR_FEED, vr.eyes, NULL, 2))
		return;
	vr.feed[7] = (float)(++vr.feed_seed % 97);
	uniform(&draw, "crt", vr.feed, 4, 1);
	uniform(&draw, "look", vr.feed + 4, 4, 1);
	uniform2f(&draw, "image", (float)vr.eye_width, (float)vr.eye_height);
	texture(&draw, 0, vr.eye_copy, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_CLAMP);
	draw.pipeline.color_mask = 7;
	fixed_end(&draw);
}

static void cockpit(struct xgpu_vulkan_image *depth, int depth_layers)
{
	struct fixed_draw draw;
	struct halo_vr_cockpit_window window;
	struct halo_vr_cockpit_display display;
	float centre[2][4] = { { 0 } }, up[2][4] = { { 0 } };
	float forward[2][4] = { { 0 } }, right[2][4] = { { 0 } }, shape[4];
	int text[COCKPIT_TEXT_LENGTH], eye;
	char line[COCKPIT_TEXT_LENGTH + 1], range_text[16], rounds_text[16], second_text[16];

	if (!vr.eyes_drawn || !halo_vr_cockpit_window(&window, &display))
		return;
	copy_eyes();
	if (!vr.eye_copy || !fixed_begin(&draw, XGPU_VULKAN_VR_COCKPIT, vr.eyes, NULL, 2))
		return;
	if (display.range > 0.0f && display.range < 999.9f)
		snprintf(range_text, sizeof(range_text), "%05.1f", display.range);
	else
		snprintf(range_text, sizeof(range_text), "---.-");
	rounds_text[0] = second_text[0] = 0;
	if (display.rounds_loaded >= 0)
		snprintf(rounds_text, sizeof(rounds_text), "   AMMO %d/%d", display.rounds_loaded,
			display.rounds_carried < 0 ? 0 : display.rounds_carried);
	if (display.second_loaded >= 0)
		snprintf(second_text, sizeof(second_text), "   MG %d/%d", display.second_loaded,
			display.second_carried < 0 ? 0 : display.second_carried);
	snprintf(line, sizeof(line), "1X   RANGE %s%s%s", range_text, rounds_text, second_text);
	text_glyphs(line, text, COCKPIT_TEXT_LENGTH);
	for (eye = 0; eye < 2; eye++)
	{
		memcpy(centre[eye], window.centre[eye], sizeof(window.centre[eye]));
		memcpy(up[eye], window.up[eye], sizeof(window.up[eye]));
		memcpy(forward[eye], window.forward[eye], sizeof(window.forward[eye]));
		memcpy(right[eye], window.right[eye], sizeof(window.right[eye]));
	}
	uniform(&draw, "centre", centre, 4, 2);
	uniform(&draw, "up", up, 4, 2);
	uniform(&draw, "forward", forward, 4, 2);
	uniform(&draw, "right", right, 4, 2);
	uniform(&draw, "fov", window.fov, 4, 2);
	shape[0] = display.radius;
	shape[1] = display.half_angle;
	shape[2] = display.half_height;
	shape[3] = display.off;
	uniform(&draw, "shape", shape, 4, 1);
	uniform2f(&draw, "image", (float)vr.eye_width, (float)vr.eye_height);
	uniform2f(&draw, "wash", display.wash, (float)(++vr.cockpit_seed % 97));
	uniform(&draw, "text", text, 1, COCKPIT_TEXT_LENGTH);
	uniform2f(&draw, "depth_map", depth && window.far_z > window.near_z ?
		window.far_z / (window.far_z - window.near_z) : 0.0f, window.near_z);
	uniform1i(&draw, "layers", depth_layers);
	texture(&draw, 0, vr.eye_copy, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_LINEAR, XGPU_VULKAN_ADDRESS_CLAMP);
	texture(&draw, 1, depth, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_FILTER_POINT, XGPU_VULKAN_ADDRESS_CLAMP);
	draw.pipeline.color_mask = 7;
	fixed_end(&draw);
}

static void tint(void)
{
	struct fixed_draw draw;
	float value[4] = { vr.tint[0], vr.tint[1], vr.tint[2], 1.0f };
	int channel;

	vr.tint[0] = vr.tint[1] = vr.tint[2] = 1.0f;
	if (!vr.eyes_drawn || (value[0] >= 1.0f && value[1] >= 1.0f && value[2] >= 1.0f) ||
		!fixed_begin(&draw, XGPU_VULKAN_VR_TINT, vr.eyes, NULL, 2))
		return;
	for (channel = 0; channel < 3; channel++)
		value[channel] = value[channel] > 0.0f ? value[channel] : 0.0f;
	uniform(&draw, "tint", value, 4, 1);
	blend(&draw, XGPU_BLEND_ZERO, XGPU_BLEND_SOURCE_COLOR);
	draw.pipeline.color_mask = 7;
	fixed_end(&draw);
}

static void vignette(void)
{
	struct fixed_draw draw;
	float fov[2][4], strength = halo_vr_vignette(fov);
	float radians = 3.14159265f / 180.0f;

	if (strength <= 0.0f || !vr.eyes_drawn || !fixed_begin(&draw, XGPU_VULKAN_VR_VIGNETTE, vr.eyes, NULL, 2))
		return;
	uniform(&draw, "fov", fov, 4, 2);
	uniform1f(&draw, "clear", (50.0f - 20.0f * strength) * radians);
	uniform1f(&draw, "fade", 22.0f * radians);
	uniform1f(&draw, "dimming", 0.65f * strength);
	blend(&draw, XGPU_BLEND_ZERO, XGPU_BLEND_SOURCE_COLOR);
	draw.pipeline.color_mask = 7;
	fixed_end(&draw);
}

static int runtime_copy(struct xgpu_vulkan_image *source, int swapchain, unsigned int token, int layers)
{
	VkImage image = token ? host_vr_vulkan_image(swapchain, (int)token - 1) : VK_NULL_HANDLE;

	if (!source || !image)
		return 0;
	xgpu_vulkan_image_to_runtime(source, image, layers);
	return 1;
}

static void compose(const struct xgpu_vr_frame *frame)
{
	struct xgpu_vulkan_image *hud = xgpu_vulkan_stream_target(frame->hud);
	struct xgpu_vulkan_image *scope = xgpu_vulkan_stream_target(frame->scope);
	int panel;

	vr.eyes_drawn = frame->eyes_drawn;
	vr.hud_drawn = frame->hud_drawn;
	vr.scope_drawn = frame->scope_drawn;
	vr.hud_panels = frame->hud_panels;
	vr.eye_depth = xgpu_vulkan_stream_target(frame->eye_depth);
	vr.hud_token = vr.scope_token = 0;
	memset(vr.panel_tokens, 0, sizeof(vr.panel_tokens));
	/* The eye pass's work submitted now, so that the GPU draws it while the
	composition is recorded, rather than idle until the frame's one
	submission at its end. No runtime image has been written yet: those
	copies come below, and are submitted before their release (submit). */
	if (vr.eyes_drawn)
		xgpu_vulkan_fence();
	fovea_compose();
	xgpu_vulkan_stream_set_vr_target(vr.eye_target, vr.eyes);
	cockpit(vr.eye_depth, frame->eye_depth_layers);
	if (vr.scope_drawn && vr.eyes_drawn && scope && copy_scope(scope))
	{
		struct halo_vr_scope_window window;

		if (halo_vr_scope_window(&window))
			scope_window(vr.eye_depth, frame->eye_depth_layers);
		else
			vr.scope_token = halo_vr_image(VR_SWAPCHAIN_SCOPE);
	}
	feed();
	tint();
	vignette();
	rim();
	visor();
	if (vr.eyes_drawn && !vr.foveated && halo_vr_depth_wanted() && vr.eye_depth &&
		xgpu_vulkan_image_description(vr.eye_depth)->depth_or_layers == 2 &&
		runtime_copy(vr.eye_depth, VR_SWAPCHAIN_DEPTH, halo_vr_image(VR_SWAPCHAIN_DEPTH), 2))
		halo_vr_depth_written();
	if (hud)
		hud_compose(hud);
	if (vr.eyes_drawn)
		runtime_copy(vr.eyes, VR_SWAPCHAIN_EYES, vr.eye_token, 2);
	runtime_copy(vr.hud, VR_SWAPCHAIN_HUD, vr.hud_token, 1);
	runtime_copy(vr.scope, VR_SWAPCHAIN_SCOPE, vr.scope_token, 1);
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
		runtime_copy(vr.panels[panel], VR_SWAPCHAIN_PANEL + panel, vr.panel_tokens[panel], 1);
	vr.pass = HALO_VR_PASS_NONE;
	vr.sky = 0;
	xgpu_vulkan_stream_vr_changed();
}

/* ---------- screenshots and frame timing */

static unsigned char *read_image(struct xgpu_vulkan_image *image, int layer)
{
	const struct xgpu_vulkan_image_description *description = image ? xgpu_vulkan_image_description(image) : NULL;
	unsigned char *pixels;
	uint32_t index;

	if (!description || !(pixels = malloc((size_t)description->width * description->height * 4)))
		return NULL;
	if (!xgpu_vulkan_read_pixels(image, layer, pixels, description->width * 4, 0))
	{
		free(pixels);
		return NULL;
	}
	if (description->format == XGPU_VULKAN_FORMAT_RGBA8)
	{
		for (index = 0; index < description->width * description->height; index++)
		{
			unsigned char red = pixels[index * 4];

			pixels[index * 4] = pixels[index * 4 + 2];
			pixels[index * 4 + 2] = red;
		}
	}
	return pixels;
}

static void screenshot_layer(const char *name, struct xgpu_vulkan_image *image)
{
	const struct xgpu_vulkan_image_description *description = image ? xgpu_vulkan_image_description(image) : NULL;
	unsigned char *pixels = read_image(image, 0);
	uint32_t index;

	if (!pixels || !description)
		return;
	for (index = 0; index < description->width * description->height; index++)
	{
		unsigned char *pixel = pixels + index * 4;
		int channel;

		for (channel = 0; channel < 3; channel++)
		{
			int value = pixel[channel] + (255 - pixel[3]) * 96 / 255;

			pixel[channel] = (unsigned char)(value > 255 ? 255 : value);
		}
		pixel[3] = 255;
	}
	xgpu_write_bmp(name, pixels, description->width, description->height, FALSE);
	free(pixels);
}

static void screenshots(const struct xgpu_vr_frame *frame)
{
	unsigned char *left, *right, *both;
	int row, panel;
	size_t index;

	if (!*config_string("debug.screenshot_directory"))
		return;
	if (vr.hud_token)
		screenshot_layer("hud-layer", vr.hud);
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
	{
		char name[32];

		if (!vr.panel_tokens[panel])
			continue;
		snprintf(name, sizeof(name), "panel%d", panel);
		screenshot_layer(name, vr.panels[panel]);
	}
	if (vr.eyes_drawn)
	{
		left = read_image(vr.eyes, 0);
		right = read_image(vr.eyes, 1);
		both = malloc((size_t)vr.eye_width * vr.eye_height * 8);
		if (left && right && both)
		{
			for (row = 0; row < vr.eye_height; row++)
			{
				memcpy(both + (size_t)row * vr.eye_width * 8, left + (size_t)row * vr.eye_width * 4, vr.eye_width * 4);
				memcpy(both + (size_t)row * vr.eye_width * 8 + vr.eye_width * 4,
					right + (size_t)row * vr.eye_width * 4, vr.eye_width * 4);
			}
			for (index = 0; index < (size_t)vr.eye_width * vr.eye_height * 2; index++)
				both[index * 4 + 3] = 255;
			xgpu_write_bmp("eyes", both, vr.eye_width * 2, vr.eye_height, FALSE);
		}
		free(left);
		free(right);
		free(both);
	}
	if (vr.hud_drawn && frame->hud)
		screenshot_layer("hud", xgpu_vulkan_stream_target(frame->hud));
	if (vr.scope_token)
		screenshot_layer("scope", vr.scope);
	/* (the smart-link's window: its view as the game drew it, all its
	640x480 screen, opaque) */
	else if (vr.scope_drawn && frame->scope && halo_vr_scope_smartlink((float[2]){ 0.0f, 0.0f }))
	{
		struct xgpu_vulkan_image *view = xgpu_vulkan_stream_target(frame->scope);
		const struct xgpu_vulkan_image_description *description = view ? xgpu_vulkan_image_description(view) : NULL;
		unsigned char *pixels = description ? read_image(view, 0) : NULL;
		uint32_t index;

		if (pixels)
		{
			for (index = 0; index < description->width * description->height; index++)
				pixels[index * 4 + 3] = 255;
			xgpu_write_bmp("scope-view", pixels, description->width, description->height, FALSE);
			free(pixels);
		}
	}
}

static void submit(const struct xgpu_vr_frame *frame, BOOL screenshot)
{
	if (vr.gpu_time)
		xgpu_vulkan_gpu_time_end();
	xgpu_vulkan_fence();
	if (vr.gpu_time)
	{
		double milliseconds;

		while (xgpu_vulkan_gpu_time_read(&milliseconds))
		{
			vr.gpu_milliseconds += milliseconds;
			if (milliseconds > vr.gpu_worst)
				vr.gpu_worst = milliseconds;
			vr.gpu_frames++;
		}
	}
	/* Every write to runtime images has been submitted on the same queue
	OpenXR was given. Release follows submission, without a CPU GPU wait. */
	halo_vr_present(vr.eyes_drawn, vr.hud_token != 0, vr.scope_token != 0);
	if (screenshot)
		screenshots(frame);
	vr.eyes_drawn = vr.hud_drawn = vr.scope_drawn = 0;
	vr.eye_depth = NULL;
}

static void timing(int mark)
{
	if (!vr.statistics || mark < 0 || mark > 3)
		return;
	vr.marks[mark] = nanoseconds();
	if (mark == 0 && vr.gpu_time)
		xgpu_vulkan_gpu_time_begin();
	if (mark == 3 && vr.marks[0] && vr.marks[1] > vr.marks[0] && vr.marks[2] > vr.marks[1])
	{
		vr.spans[0] += (double)(vr.marks[1] - vr.marks[0]) * 1e-6;
		vr.spans[1] += (double)(vr.marks[2] - vr.marks[1]) * 1e-6;
		vr.spans[2] += (double)(vr.marks[3] - vr.marks[2]) * 1e-6;
		if (++vr.timed == 300)
		{
			host_logf(HOST_LOG_INFO, "vr: a frame's draws: %.2f ms before the eyes, %.2f ms the eyes, "
				"%.2f ms the HUD and present", vr.spans[0] / vr.timed, vr.spans[1] / vr.timed, vr.spans[2] / vr.timed);
			if (vr.gpu_frames)
				host_logf(HOST_LOG_INFO, "vr: elapsed render GPU timeline per frame: %.2f ms, worst %.2f ms (%u samples, %dx%d eyes%s)",
					vr.gpu_milliseconds / vr.gpu_frames, vr.gpu_worst, vr.gpu_frames, vr.eye_width, vr.eye_height,
					vr.foveated ? ", foveated" : "");
			vr.gpu_milliseconds = vr.gpu_worst = 0.0;
			vr.gpu_frames = vr.timed = 0;
			memset(vr.spans, 0, sizeof(vr.spans));
		}
	}
}

/* ---------- the shared VR stream's renderer operations */

static BOOL supported(void)
{
	struct xgpu_vulkan_capabilities capabilities;

	xgpu_vulkan_capabilities(&capabilities);
	if (capabilities.multiview_views < 2)
	{
		host_logf(HOST_LOG_WARN, "vr: Vulkan device has no stereo multiview; playing flat");
		return FALSE;
	}
	if (!capabilities.depth_clamp)
	{
		host_logf(HOST_LOG_WARN, "vr: Vulkan device has no depth clamping for the sky; playing flat");
		return FALSE;
	}
	return TRUE;
}

static BOOL sample_counting(void)
{
	struct xgpu_vulkan_capabilities capabilities;

	xgpu_vulkan_capabilities(&capabilities);
	return capabilities.fragment_stores != 0;
}

/* Make every fixed compositor variant before gameplay; scene pipelines
are prepared by the shared per-map enumeration. */
static void prepare_fixed(void)
{
	int id, variant;

	for (id = 0; id < XGPU_VULKAN_VR_PROGRAM_COUNT; id++)
	{
		const struct xgpu_vulkan_vr_program *program = &xgpu_vulkan_vr_programs[id];
		struct xgpu_vulkan_pipeline_description pipeline;

		memset(&pipeline, 0, sizeof(pipeline));
		pipeline.vertex_shader = xgpu_vulkan_shader_constant(program->vertex_words, program->vertex_bytes / 4);
		pipeline.pixel_shader = xgpu_vulkan_shader_constant(program->fragment_words, program->fragment_bytes / 4);
		pipeline.primitive_class = XGPU_VULKAN_CLASS_TRIANGLES;
		pipeline.samples = 1;
		pipeline.view_mask = program->views == 2 ? 3 : 0;
		pipeline.color_format = id == XGPU_VULKAN_VR_LIGHT ? XGPU_VULKAN_FORMAT_RGBA16F :
			id == XGPU_VULKAN_VR_HUD ? color_format(VR_SWAPCHAIN_HUD) :
			id == XGPU_VULKAN_VR_SCOPE ? color_format(VR_SWAPCHAIN_SCOPE) : color_format(VR_SWAPCHAIN_EYES);
		pipeline.color_mask = 15;
		if (id == XGPU_VULKAN_VR_VISOR || id == XGPU_VULKAN_VR_RIM || id == XGPU_VULKAN_VR_FEED ||
			id == XGPU_VULKAN_VR_COCKPIT || id == XGPU_VULKAN_VR_VIGNETTE || id == XGPU_VULKAN_VR_TINT ||
			id == XGPU_VULKAN_VR_SCOPE_WINDOW)
			pipeline.color_mask = 7;
		if (id == XGPU_VULKAN_VR_RIM || id == XGPU_VULKAN_VR_SCOPE_WINDOW ||
			id == XGPU_VULKAN_VR_VIGNETTE || id == XGPU_VULKAN_VR_TINT)
		{
			pipeline.blend = 1;
			pipeline.blend_source = id == XGPU_VULKAN_VR_VIGNETTE || id == XGPU_VULKAN_VR_TINT ?
				XGPU_BLEND_ZERO : XGPU_BLEND_ONE;
			pipeline.blend_destination = pipeline.blend_source == XGPU_BLEND_ZERO ?
				XGPU_BLEND_SOURCE_COLOR : XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA;
			pipeline.blend_operation = XGPU_BLEND_ADD;
		}
		if (id == XGPU_VULKAN_VR_DEPTH)
			continue; /* matching native depth is copied */
		if (!xgpu_vulkan_pipeline(&pipeline))
			abort();
		if (id == XGPU_VULKAN_VR_LIGHT)
		{
			pipeline.blend = 1;
			pipeline.blend_source = XGPU_BLEND_SOURCE_ALPHA;
			pipeline.blend_destination = XGPU_BLEND_ONE_MINUS_SOURCE_ALPHA;
			pipeline.blend_operation = XGPU_BLEND_ADD;
			if (!xgpu_vulkan_pipeline(&pipeline))
				abort();
		}
		if (id == XGPU_VULKAN_VR_RIM)
		{
			pipeline.depth_format = XGPU_VULKAN_FORMAT_D24S8;
			for (variant = 0; variant < 2; variant++)
			{
				if (variant)
				{
					pipeline.blend = 0;
					pipeline.color_mask = 0;
					pipeline.depth_test = pipeline.depth_write = 1;
					pipeline.depth_function = XGPU_COMPARE_ALWAYS;
				}
				if (!xgpu_vulkan_pipeline(&pipeline))
					abort();
			}
		}
	}
}

static void initialize(struct xgpu_vr_info *info)
{
	struct xgpu_vulkan_capabilities capabilities;
	float inset = halo_vr_foveation_inset();
	int eye, diagonal, panel;

	xgpu_vulkan_capabilities(&capabilities);
	vr.eye_width = info->eye_width;
	vr.eye_height = info->eye_height;
	vr.hud_width = info->hud_width;
	vr.hud_height = info->hud_height;
	vr.scope_size = info->scope_size;
	vr.statistics = info->statistics;
	/* (the settings file's, which the guest reads, or HALO_VR_GPU_TIME) */
	vr.gpu_time = vr.statistics && (info->gpu_time || config_boolean("debug.vr_gpu_time"));
	if (vr.gpu_time && !capabilities.gpu_timestamps)
	{
		host_logf(HOST_LOG_INFO, "vr: this Vulkan queue has no GPU timestamps; GPU duration unavailable");
		vr.gpu_time = 0;
	}
	vr.tint[0] = vr.tint[1] = vr.tint[2] = 1.0f;
	xgpu_vulkan_visibility_count_samples(capabilities.fragment_stores);
	vr.foveated = inset > 0.0f && capabilities.multiview_views >= 4 && capabilities.fragment_stores;
	if (vr.foveated)
	{
		vr.fovea_width = (int)((float)vr.eye_width * inset + 0.5f) & ~1;
		vr.fovea_height = (int)((float)vr.eye_height * inset + 0.5f) & ~1;
		if (vr.fovea_width < 2 || vr.fovea_height < 2)
			vr.foveated = 0;
		else
			vr.fovea = image_make(vr.fovea_width, vr.fovea_height, 4, color_format(VR_SWAPCHAIN_EYES));
		vr.fovea_show = config_boolean("debug.vr_foveation_show");
	}
	vr.eyes = image_make(vr.eye_width, vr.eye_height, 2, color_format(VR_SWAPCHAIN_EYES));
	if (!vr.eyes || (vr.foveated && !vr.fovea))
	{
		host_logf(HOST_LOG_ERROR, "vr: cannot allocate Vulkan eye images");
		abort();
	}
	for (eye = 0; eye < 2; eye++)
	{
		for (diagonal = 0; diagonal < 4; diagonal++)
			vr.transforms[eye][diagonal * 5] = 1.0f;
		vr.corrections[eye][0] = vr.corrections[eye][1] = 1.0f;
	}
	/* The composition's other images made now, not the first time each is
	drawn (a hitch in play): each is still made then if it cannot be now */
	image_keep(&vr.eye_copy, vr.eye_width, vr.eye_height, 2, color_format(VR_SWAPCHAIN_EYES));
	image_keep(&vr.light, VR_LIGHT_WIDTH, VR_LIGHT_HEIGHT, 1, XGPU_VULKAN_FORMAT_RGBA16F);
	image_keep(&vr.hud, vr.hud_width, vr.hud_height, 1, color_format(VR_SWAPCHAIN_HUD));
	image_keep(&vr.scope, vr.scope_size, vr.scope_size, 1, color_format(VR_SWAPCHAIN_SCOPE));
	for (panel = 0; panel < VR_PANEL_COUNT; panel++)
	{
		int width = 0, height = 0;

		halo_vr_panel_size(panel, &width, &height);
		image_keep(&vr.panels[panel], width, height, 1, color_format(VR_SWAPCHAIN_PANEL + panel));
	}
	prepare_fixed();
	vr.ready = 1;
	info->foveated = vr.foveated;
	info->fovea_width = vr.fovea_width;
	info->fovea_height = vr.fovea_height;
	host_logf(HOST_LOG_INFO, "vr: Vulkan %dx%d eyes, %s%s", vr.eye_width, vr.eye_height,
		vr.foveated ? "four-view foveation by " : "stereo multiview", vr.foveated ? halo_vr_foveation_name() : "");
	if (vr.foveated)
		host_logf(HOST_LOG_INFO, "vr: foveated views %dx%d, %.0f%% of the eyes' pixels", vr.fovea_width,
			vr.fovea_height, 200.0f * vr.fovea_width * vr.fovea_height / ((float)vr.eye_width * vr.eye_height));
}

static void eyes(xgpu_target target, unsigned int image)
{
	vr.eye_target = target;
	vr.eye_token = image;
	if (vr.foveated)
		fovea_place();
	xgpu_vulkan_stream_set_vr_target(target, vr.foveated ? vr.fovea : vr.eyes);
	xgpu_vulkan_stream_vr_changed();
}

static void pass(int which)
{
	vr.pass = which;
	vr.sky = 0;
	xgpu_vulkan_stream_vr_changed();
}

static void groups_reset(void)
{
	vr.groups_drawn = 0;
}

static void eye_transforms(const float transforms[2][16])
{
	memcpy(vr.transforms, transforms, sizeof(vr.transforms));
	xgpu_vulkan_stream_vr_changed();
}

static void screen_corrections(const float corrections[2][4])
{
	memcpy(vr.corrections, corrections, sizeof(vr.corrections));
	xgpu_vulkan_stream_vr_changed();
}

static void sky(int enabled)
{
	vr.sky = enabled && vr.pass == HALO_VR_PASS_EYES;
	xgpu_vulkan_stream_vr_changed();
}

static void eye_feed(float squash, float shrink, float bright, float noise, float red, float look, float interference)
{
	vr.feed_set = 1;
	vr.feed[0] = squash;
	vr.feed[1] = shrink;
	vr.feed[2] = bright;
	vr.feed[3] = noise;
	vr.feed[4] = red;
	vr.feed[5] = look;
	vr.feed[6] = interference;
}

static void eye_tint(float red, float green, float blue)
{
	vr.tint[0] *= red;
	vr.tint[1] *= green;
	vr.tint[2] *= blue;
}

static unsigned int image_token(int swapchain, int index)
{
	(void)swapchain;
	return (unsigned int)index + 1;
}

int host_vulkan_vr_active(void)
{
	return vr.ready;
}

int host_vulkan_vr_foveated(void)
{
	return vr.foveated;
}

int host_vulkan_vr_sky(void)
{
	return vr.sky;
}

void host_vulkan_vr_uniforms(struct xgpu_spirv_uniforms *uniforms, const int32_t viewport[4])
{
	int eye, row, column, view;
	static const float identity[2][4] = { { 1.0f, 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 0.0f } };

	for (eye = 0; eye < 2; eye++)
	{
		for (row = 0; row < 4; row++)
		{
			for (column = 0; column < 4; column++)
				uniforms->eye_correction[eye][column * 4 + row] = vr.transforms[eye][row * 4 + column];
		}
	}
	memcpy(uniforms->screen_correction, vr.corrections, sizeof(vr.corrections));
	uniforms->screen_points = memcmp(vr.corrections, identity, sizeof(identity)) ? 1.0f : 0.0f;
	for (view = 0; view < 4; view++)
	{
		float *transform = uniforms->fovea_view[view];
		float *sample = uniforms->fovea_sample[view];

		transform[0] = transform[1] = sample[0] = sample[1] = 1.0f;
		transform[2] = transform[3] = sample[2] = sample[3] = 0.0f;
		if (vr.foveated && (view & 1) && viewport[2] > 0 && viewport[3] > 0)
		{
			float sx = (float)vr.eye_width / vr.fovea_width;
			float sy = (float)vr.eye_height / vr.fovea_height;
			const int *corner = vr.corners[view >> 1];

			transform[0] = sample[0] = sx;
			transform[1] = sample[1] = sy;
			transform[2] = sx - 1.0f + 2.0f * ((sx - 1.0f) * viewport[0] - corner[0]) / viewport[2];
			transform[3] = sy - 1.0f + 2.0f * ((sy - 1.0f) * viewport[1] - corner[1]) / viewport[3];
			sample[2] = -(float)corner[0];
			sample[3] = -(float)corner[1];
		}
	}
}

struct xgpu_vulkan_image *host_vulkan_vr_hud_group(uint32_t hud, int group)
{
	struct xgpu_vulkan_image *target = xgpu_vulkan_stream_target(hud);
	const struct xgpu_vulkan_image_description *description = target ? xgpu_vulkan_image_description(target) : NULL;
	struct xgpu_vulkan_image *image;

	if (!vr.ready || !description || group < 0 || group >= HALO_VR_HUD_GROUPS)
		return NULL;
	image = image_keep(&vr.groups[group], description->width, description->height, 1, description->format);
	if (image && !(vr.groups_drawn & (1u << group)))
	{
		int32_t rectangle[1][4] = { { 0, 0, (int32_t)description->width, (int32_t)description->height } };
		const float transparent[4] = { 0.0f };

		xgpu_vulkan_set_targets(image, NULL, 0);
		xgpu_vulkan_clear(rectangle, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS, transparent, 1.0f, 0);
		vr.groups_drawn |= 1u << group;
	}
	return image;
}

void host_vulkan_vr_backend(struct host_render_backend *backend)
{
	static const struct host_vr_renderer renderer =
	{
		.supported = supported,
		.initialize = initialize,
		.sample_counting = sample_counting,
		.eyes = eyes,
		.pass = pass,
		.groups_reset = groups_reset,
		.timing = timing,
		.compose = compose,
		.submit = submit,
		.eye_transforms = eye_transforms,
		.screen_corrections = screen_corrections,
		.sky = sky,
		.eye_feed = eye_feed,
		.eye_tint = eye_tint,
		.image = image_token,
	};

	host_vr_stream_backend(backend, &renderer);
}
