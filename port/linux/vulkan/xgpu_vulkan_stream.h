/*
XGPU_VULKAN_STREAM.H

The Vulkan backend as the command stream's backend table
(xgpu_vulkan_stream.c; port/android/host/host_render_stream.h).
*/

#ifndef __HALO_LINUX_XGPU_VULKAN_STREAM_H
#define __HALO_LINUX_XGPU_VULKAN_STREAM_H

#include <stdint.h>

struct host_render_backend;
struct xgpu_vulkan_image;

/* the table (made once; xgpu_vulkan_initialize first) */
const struct host_render_backend *xgpu_vulkan_stream_backend(void);

/* the debugging settings (debug.gpu_stats, debug.gpu_dump_shaders; before
the stream is played): the backend's statistics logged every 600 frames,
and the folder each shader module is written into as it is made, as
<name>.spv (NULL or empty: none; the string is kept) */
void xgpu_vulkan_stream_debug(int statistics, const char *shader_folder);

/* Host VR composition uses the same numbered targets as the stream. A
replacement image remains owned by the composer, including at target deletion. */
struct xgpu_vulkan_image *xgpu_vulkan_stream_target(uint32_t number);
void xgpu_vulkan_stream_set_vr_target(uint32_t number, struct xgpu_vulkan_image *image);
void xgpu_vulkan_stream_vr_changed(void);

/* (the render thread) a colour target's pixels as an SDR display shows
them (display.hdr's tone mapped): BGRA rows from the top, malloc'd; NULL if
it has none. The shot: them as a PNG at path, nonzero if written
(host_render_record.h's shooter). */
unsigned char *xgpu_vulkan_stream_read(uint32_t number, uint32_t *width, uint32_t *height);
int xgpu_vulkan_stream_shot(uint32_t number, const char *path);

/* (the render thread) the replay's ID buffer (port/tools/replay/
host_render_replay.h): of the target a frame presents, its draws numbered
as they come (xgpu_vulkan_inspect_*) */
int xgpu_vulkan_stream_ids_begin(uint32_t number, uint32_t depth);
void xgpu_vulkan_stream_ids_draw(uint32_t index);
uint32_t *xgpu_vulkan_stream_ids_read(uint32_t number, uint32_t *width, uint32_t *height);
void xgpu_vulkan_stream_ids_end(void);

#endif
