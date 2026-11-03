/*
XGPU_VULKAN_POST.H

display.anti_aliasing's passes for the Vulkan backend (xgpu_vulkan_post.c):
FXAA or SMAA antialias a window's 3D view in place before the HUD and menus
are drawn over it (their GLSL: tools/vulkan_post_shaders.py).
Supersampling and multisampling are the front end's and the core's. And
display.hdr's tone map and encoding (port/linux/src/xgpu_hdr.c's shaders).
*/

#ifndef __HALO_LINUX_XGPU_VULKAN_POST_H
#define __HALO_LINUX_XGPU_VULKAN_POST_H

#include <stdint.h>

struct xgpu_vulkan_image;

struct xgpu_hdr_frame;

/* the pipelines of FXAA, or of SMAA's passes, for a target of a format
(XGPU_VULKAN_FORMAT_BGRA8, or display.hdr's RGBA16F), and SMAA's lookup
textures, made now; queued for the pipelines' compiler instead where
preparing (a map's preparation, which waits for them); 0 if they cannot be
made (logged; once 0, it stays so) */
int xgpu_vulkan_post_prepare(int smaa, uint8_t format, int preparing);

/* FXAA, or SMAA, over the corners x0, y0 to x1, y1 (pixels from row 0) of a
layer of a BGRA8 or RGBA16F target, in the stream's order; the targets
bound before are not bound after. 0 if its pipelines cannot be made */
int xgpu_vulkan_post_anti_alias(int smaa, struct xgpu_vulkan_image *target, uint32_t layer, const int32_t corners[4]);

/* display.hdr (docs/hdr.md). The tone map's pipeline for an RGBA16F target
of a number of samples drawn with a depth attachment (XGPU_VULKAN_FORMAT_
D24S8) or none, made now or queued where preparing; 0 if it cannot be.
The tone map over the corners of a target (its first layer), in the pass
drawing into it and its depth (bound after as they are, so that the pass
goes on): a draw reading each pixel where it is drawn (the core's local
reads), its colour alone written; hdr the mode and the peak. */
int xgpu_vulkan_post_prepare_hdr(uint8_t depth_format, uint32_t samples, int preparing);
void xgpu_vulkan_post_tone_map(struct xgpu_vulkan_image *target, struct xgpu_vulkan_image *depth,
	const int32_t corners[4], const float hdr[4]);
/* The encoding of a frame for an HDR swapchain (the core's options'
encode): encode_prepare makes its pipelines at the start, for the
swapchains' formats (XGPU_VULKAN_FORMAT_RGBA16F, scRGB's;
XGPU_VULKAN_FORMAT_RGB10A2, HDR10's), as the loading screen's presentation
may encode while a map is prepared: 0 if they cannot be made (logged).
encode draws the frame (BGRA8 or RGBA16F, the frame's values) encoded
(xgpu_hdr.c's hdr_encode: the frame's paper white, peak and output) into an
image of its own of the format and its size, in the stream's order, and
returns it; the targets bound before are not bound after. NULL if it
cannot. */
int xgpu_vulkan_post_encode_prepare(void);
struct xgpu_vulkan_image *xgpu_vulkan_post_encode(struct xgpu_vulkan_image *image, const struct xgpu_hdr_frame *frame,
	uint8_t format);
/* display.gamma's curve over an SDR frame (the core's options' gamma), its
last pass before the swapchain: gamma_prepare makes its pipeline at the
start (as the encoding's are), 0 if it cannot be (logged). gamma draws the
frame with each channel raised to power (one over the setting) into a BGRA8
image of its own of the frame's size, in the stream's order, and returns
it; the targets bound before are not bound after. NULL if it cannot. */
int xgpu_vulkan_post_gamma_prepare(void);
struct xgpu_vulkan_image *xgpu_vulkan_post_gamma(struct xgpu_vulkan_image *image, float power);

/* display.ambient_occlusion (xgpu_backend_ambient_occlusion). Its
pipelines for a target of a format and number of samples drawn with a
depth target (XGPU_VULKAN_FORMAT_D24S8), made now or queued where
preparing; 0 if they cannot be (logged; once 0, it stays so). The occlusion
over the corners of a target's 3D view (its first layer), found from a
depth target of one sample (depth_texture: depth's, or its first sample's
resolved) and the stencil of depth (its attachment, drawn with the target:
XGPU_AMBIENT_OCCLUSION_OBJECT and _KEEP), laid over the target's colour:
quality XGPU_AMBIENT_OCCLUSION_SUBTLE or _STRONG, camera its near and far
planes and the projection's x and y scales and offsets. In the stream's
order; the targets bound before are not bound after. show draws the
occlusion last laid in place of the view (white where it was not laid),
once. */
int xgpu_vulkan_post_ambient_occlusion_prepare(uint8_t format, uint32_t samples, int preparing);
void xgpu_vulkan_post_ambient_occlusion(struct xgpu_vulkan_image *target, struct xgpu_vulkan_image *depth,
	struct xgpu_vulkan_image *depth_texture, const int32_t corners[4], int quality, const float camera[6]);
void xgpu_vulkan_post_ambient_occlusion_show(struct xgpu_vulkan_image *target, const int32_t corners[4]);

/* The present step's turn, for a swapchain made for the display's own
orientation while the phone is held another way (Android's pre-rotation:
the compositor then has nothing to turn). prepare makes its pipeline, at the
start (not while a map is prepared, since rotate may be called by the
loading screen's presentation then): 0 if it cannot be made (logged).
rotate draws an image turned by quarter turns clockwise (1 to 3) into an
image of its own (its width and height swapped for an odd count), in the
stream's order, and returns it; the targets bound before are not bound
after. NULL if it cannot. */
int xgpu_vulkan_post_rotate_prepare(void);
struct xgpu_vulkan_image *xgpu_vulkan_post_rotate(struct xgpu_vulkan_image *image, int quarter_turns);

#endif
