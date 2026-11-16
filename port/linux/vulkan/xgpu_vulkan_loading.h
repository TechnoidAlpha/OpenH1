/* The loading scene is independent of the game's targets and pipelines. */
#ifndef XGPU_VULKAN_LOADING_H
#define XGPU_VULKAN_LOADING_H

#include <stdint.h>
struct loading_presentation_frame;
uint64_t xgpu_vulkan_loading(void *context, const struct loading_presentation_frame *frame);
int xgpu_vulkan_loading_owned(void *context);
void xgpu_vulkan_loading_art(void *context, const uint8_t *rgba);
void xgpu_vulkan_loading_scene(void *context, const void *data, uint32_t bytes);
/* VR: the loading image is the stereo pair when the scene allows; whether
the last one drawn was */
void xgpu_vulkan_loading_progress(void *context, float progress);
void xgpu_vulkan_loading_mode(void *context, int follow_progress);
void xgpu_vulkan_loading_caption(void *context, const unsigned char *coverage, uint32_t width, uint32_t height);
void xgpu_vulkan_loading_stereo(int stereo);
int xgpu_vulkan_loading_stereo_drawn(void);
/* where VR's eyes are (loading_art_stereo_eyes), and how wide the window is
(loading_art_stereo_tangent) */
void xgpu_vulkan_loading_eyes(const float left[3], const float right[3]);
float xgpu_vulkan_loading_stereo_tangent(void);
/* the loading frame's width for the window (loading_art_width) */
uint32_t xgpu_vulkan_loading_frame_width(void);
uint32_t xgpu_vulkan_loading_frame_height(void);
/* the window's size as it is shown, where the loading screen has a caption
(drawn at its pixels); else 0 */
int xgpu_vulkan_loading_caption_size(uint32_t *width, uint32_t *height);
uint64_t host_vulkan_display_period(void);

#endif
