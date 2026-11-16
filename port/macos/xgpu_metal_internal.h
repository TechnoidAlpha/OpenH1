/*
XGPU_METAL_INTERNAL.H

What the Metal backend (xgpu_metal.m) and the window's presentation
(host_metal.m) share. Objective-C.
*/

#ifndef __HALO_MACOS_XGPU_METAL_INTERNAL_H
#define __HALO_MACOS_XGPU_METAL_INTERNAL_H

#import <Metal/Metal.h>

#include "xgpu_metal.h"

/* ---------- xgpu_metal.m */

/* the device and the game renderer's producer queue (nil before
xgpu_metal_initialize) */
id<MTLDevice> xgpu_metal_device(void);
id<MTLCommandQueue> xgpu_metal_queue(void);
/* the library of the clears' and the presentation's functions
(xgpu_metal_fixed_source's) */
id<MTLLibrary> xgpu_metal_fixed_library(void);
/* the app's library (nil without one) */
id<MTLLibrary> xgpu_metal_library(void);
/* a command buffer's GPU start and end, counted in the GPU's busy time */
void xgpu_metal_gpu_interval(double start, double end);
/* the GPU's busy time so far (its intervals' union), in nanoseconds */
uint64_t xgpu_metal_gpu_busy_nanoseconds(void);
/* the render thread waits for the GPU to finish a frame (xgpu_metal_frame_wait) */
int xgpu_metal_frame_waiting(void);
/* a texture's Metal texture (nil for none) */
id<MTLTexture> xgpu_metal_texture(uint32_t texture);

/* the replay's inspector (port/macos/tools/halo_replay_serve.m): called on
the render thread before and after each draw (after: 1) with the draw's pass
ended (its targets stored, what it drew in them) and the colour target's
texture handle and level, so that it can encode work of its own into the
command buffer (xgpu_metal_command_buffer) between draws; NULL: none (the
draws as ever) */
typedef void (*xgpu_metal_draw_inspector)(int after, uint32_t color, uint32_t level);
void xgpu_metal_set_draw_inspector(xgpu_metal_draw_inspector inspector);
/* the command buffer being recorded (begun if there is none); and it
committed (the open pass ended first) and waited for, the frame going on
in another */
id<MTLCommandBuffer> xgpu_metal_command_buffer(void);
void xgpu_metal_command_buffer_finish(void);

/* ---------- host_metal.m */

/* a window shows the frames presented */
int host_metal_window_active(void);
/* takes a frame's picture (a texture the frame drew) as one of the
presentation's images, in the place of one neither queued nor on screen:
waits while there is none, so that the game runs no faster than the display
shows its frames. Returns the image it replaces, for the next frame to draw
into, where that is of the picture's size and format (else nil) */
id<MTLTexture> host_metal_present_exchange(id<MTLTexture> picture);
/* Set up the presentation queue and event once, after the device exists
and before presentation starts. Does not open a window. */
int host_metal_present_initialize(void);
/* On the presentation thread: zero is owned procedural content, otherwise
wait for the published image's producer frame on the independent queue. */
id<MTLCommandBuffer> host_metal_present_buffer(uint64_t producer_frame);
/* Signal the image's monotonic frame value after its last encoded work,
then commit the producer buffer before publishing the image below. */
void host_metal_present_signal(id<MTLCommandBuffer> buffer, uint64_t frame);
/* The independent presentation queue waits for this image's producer value,
not later game work. Called once per frame, nil when there is no picture
(its display's pacing, host_render_stream_frame_taken). */
void host_metal_present_queue(id<MTLTexture> image, uint64_t frame);

struct xgpu_hdr_frame;
/* display.hdr (port/linux/src/xgpu_hdr.h): the front end's state (drawn in
HDR, the mode, paper white in nits, or 0 and its percent of the system's
SDR white: xgpu_hdr_paper_white_nits; the UI's white in nits, or 0 for
paper white's; the render thread, as it plays the stream); and what the
tone map and the encoding are given now: the mode, the layer's output (EDR's
extended linear sRGB, or SDR), paper white, the peak in paper whites and the
UI's white over paper white (any thread) */
void host_metal_hdr_state(int active, int mode, int paper_white_nits, int paper_white, int ui_nits);
/* the display's headroom stood in for (HALO_METAL_EDR_HEADROOM's: the tests,
the replay) */
void host_metal_hdr_override(float headroom);
void host_metal_hdr_frame(struct xgpu_hdr_frame *frame);
/* display.gamma (the render thread, as it plays the stream): the setting,
1 for none; the presentation curves an SDR frame by one over it, an EDR
layer's never */
void host_metal_gamma(float gamma);

#endif
