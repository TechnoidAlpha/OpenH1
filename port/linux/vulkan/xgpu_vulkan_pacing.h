/*
XGPU_VULKAN_PACING.H

display.low_latency: the Vulkan backend's frames paced by the display with
vsync on (FIFO), by when each present reaches the screen
(xgpu_vulkan_pacing.c). The render thread presents through
xgpu_vulkan_pacing_present and says each frame's end; a thread of its own
waits for each present (VK_KHR_present_wait) or asks the display when it
showed it (VK_GOOGLE_display_timing), and tells the command stream
(host_render_stream_frame_displayed), which starts the game's frames just
in time for their refresh (frame_pacing.h).
*/

#ifndef XGPU_VULKAN_PACING_H
#define XGPU_VULKAN_PACING_H

#include "xgpu_vulkan.h"

/* what the device has, enabled with it */
struct xgpu_vulkan_pacing_device
{
	VkDevice device;
	PFN_vkGetDeviceProcAddr get_device_proc_addr;
	/* the backend's timeline semaphore: a frame's work done at its number */
	VkSemaphore timeline;
	/* VK_KHR_present_id and VK_KHR_present_wait; VK_GOOGLE_display_timing */
	int present_wait, display_timing;
	/* the statistics logged (debug.gpu_stats, HALO_VULKAN_STATS) */
	int statistics;
};

/* the device made (render thread): the thread started where it has a way
to know when a present is shown, and where watch (the frame log or the
statistics) or pacing is asked; returns whether it can pace */
int xgpu_vulkan_pacing_start(const struct xgpu_vulkan_pacing_device *device);
void xgpu_vulkan_pacing_stop(void);

/* (render thread) display.low_latency's mode: 0 auto, 1 on, 2 off; the
swap interval (the refreshes each frame is shown for: 0 as 1) and whether
the present mode is FIFO (pacing only with it: vsync's, or the only mode a
surface has); the display's refresh period by the window's mode (0: as
before), the fallback for the display's own.
Returns whether the game is paced by the display (the command stream
told). */
int xgpu_vulkan_pacing_mode(int mode, int interval, int fifo, uint64_t period);

/* (render thread) the swapchain presented to from now on (VK_NULL_HANDLE:
none), before the one before is destroyed: its presents not yet shown are
taken as shown, and the thread no longer waits for them */
void xgpu_vulkan_pacing_swapchain(VkSwapchainKHR swapchain);
/* (render thread) whether presents get their numbers: so the swapchain is
made with no more images than pacing needs (3) */
int xgpu_vulkan_pacing_active(void);

/* (render thread) vkQueuePresentKHR's call for info (one swapchain), with
the present's number where the thread waits for it; frame: the first
present of a frame (its input the command stream's frame's); serial: the
submission whose work the present shows */
VkResult xgpu_vulkan_pacing_present(PFN_vkQueuePresentKHR queue_present, VkQueue queue, VkPresentInfoKHR *info,
	int frame, uint64_t serial);
/* (render thread) vkAcquireNextImageKHR's call (no fence), one at a time
with the thread's calls on the swapchain (display timing's), which must
have it to themselves */
VkResult xgpu_vulkan_pacing_acquire(PFN_vkAcquireNextImageKHR acquire, VkDevice device, VkSwapchainKHR swapchain,
	uint64_t timeout, VkSemaphore semaphore, uint32_t *index);
/* (render thread) a frame ended with nothing presented */
void xgpu_vulkan_pacing_unshown(void);
/* (render thread) the swapchain has given no image for seconds: the thread
stops waiting for presents, and the game is no longer paced */
void xgpu_vulkan_pacing_silence(void);

#endif
