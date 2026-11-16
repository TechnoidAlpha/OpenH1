/*
XGPU_METAL_POST.H

display.anti_aliasing's passes for the Metal backend (xgpu_metal_post.c):
FXAA and SMAA over the rectangle of a window's 3D view, in the target,
before the HUD and the menus are drawn over it, as the Vulkan backend runs
them.
*/

#ifndef __HALO_MACOS_XGPU_METAL_POST_H
#define __HALO_MACOS_XGPU_METAL_POST_H

#include <stdint.h>

/* SMAA's (smaa nonzero), or FXAA's, functions, pipelines and lookup
textures, made as the setting is chosen or a map is prepared: the
functions and pipelines on the backend's queues (which a map's preparation
waits for); 0 (logged once) if they cannot be */
int xgpu_metal_post_prepare(int smaa);

/* FXAA or SMAA over the corners (x0, y0, x1, y1, from the top left) of a
target's texture (BGRA8, or display.hdr's RGBA16F: format) (xgpu_metal_texture_create's, not multisampled), of
its size; nonzero if it was antialiased. The backend's targets, pipeline,
dynamic state, textures and draw uniforms are the passes' afterwards: the
caller sets its own again. */
int xgpu_metal_post_anti_alias(int smaa, uint32_t target, uint8_t format, uint32_t width, uint32_t height,
	const int32_t corners[4]);

/* display.panini (port/linux/game/panini.c): its function and pipelines,
made on the backend's queues (0, logged once, if the function cannot be);
and the corners of a target's texture (BGRA8 or RGBA16F: format; its size
and samples: a multisampled target's pixels are resolved for the pass to
read, and the pass drawn into its samples) drawn again through the Panini
projection
(port/linux/include/panini_projection.h), projection its d, s,
magnification at the middle, 0, and the rectilinear view's tangents at
the corners' left, right, top and bottom, as the Vulkan backend draws it.
Nonzero if it was drawn; the backend's state is the pass's afterwards, as
anti-aliasing's is. */
int xgpu_metal_post_panini_prepare(uint32_t samples);
int xgpu_metal_post_panini(uint32_t target, uint8_t format, uint32_t width, uint32_t height, uint32_t samples,
	const int32_t corners[4], const float projection[8]);

/* display.hdr (port/linux/src/xgpu_hdr.h): the tone map's function and the
pipeline for a pass with that depth attachment (its XGPU_METAL_FORMAT_*, or
NONE) and samples, made (as anti-aliasing's are); and the tone map of the
corners of an RGBA16F target (as anti-aliasing's), drawn in the pass that
drew it, with its depth attachment (0: none) and samples, its pixels read
where they are (programmable blending): hdr the mode and the peak
(xgpu_hdr.h). Nonzero if it was drawn; the backend's state is the pass's
afterwards, as anti-aliasing's is. */
int xgpu_metal_post_prepare_hdr(uint8_t depth_format, uint32_t samples);
int xgpu_metal_post_tone_map(uint32_t target, uint32_t width, uint32_t height, uint32_t depth, uint8_t depth_format,
	uint32_t samples, const int32_t corners[4], const float hdr[4]);

#endif
