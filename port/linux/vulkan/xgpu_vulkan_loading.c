/* Draw the shared loading scene with attachment clears. It can run while
   a worker compiles a game pipeline, without requesting another pipeline. */
#include "xgpu_vulkan.h"
#include "xgpu_vulkan_loading.h"
#include "host_load_trace.h"
#include "host_loading_capture.h"
#include "../src/loading_presentation.h"
#include "../src/loading_art.h"
#ifdef HALO_VR
#include "host_vr_render.h"
#include "host_vr_vulkan.h"
#endif

static void loading_capture(const struct loading_presentation_frame *frame, struct xgpu_vulkan_image *image)
{
	const struct xgpu_vulkan_image_description *description;
	unsigned char *pixels;

	if (!image || !host_loading_capture_due(frame))
		return;
	description = xgpu_vulkan_image_description(image);
	if (!description->width || !description->height || description->width > 16384 || description->height > 16384)
		return;
	pixels = malloc((size_t)description->width * description->height * 4);
	if (!pixels)
		return;
	if (xgpu_vulkan_read_pixels(image, 0, pixels, description->width * 4, 0))
		host_loading_capture_write(frame, pixels, description->width, description->height, 0,
			description->format == XGPU_VULKAN_FORMAT_BGRA8);
	free(pixels);
}

int xgpu_vulkan_loading_owned(void *context)
{
	(void)context;
#ifdef HALO_VR
	if (host_vr_info())
		return host_vr_loading_owned() && xgpu_vulkan_loading_resources(0);
#endif
	return xgpu_vulkan_loading_resources(1);
}

uint64_t xgpu_vulkan_loading(void *context, const struct loading_presentation_frame *frame)
{
	struct xgpu_vulkan_image *image;
	uint64_t period = host_vulkan_display_period();

	(void)context;
	if (frame->ready)
		return period;
	if (!frame->active)
	{
		xgpu_vulkan_loading_draw(frame, 0, 0, 0);
		return period;
	}
#ifdef HALO_VR
	if (host_vr_info())
	{
		const struct vr_host_info *info = host_vr_info();
		struct vr_host_views views = { 0 };
		int index = host_vr_loading_begin(frame, &views);
		VkFormat format = host_vr_vulkan_format(VR_SWAPCHAIN_HUD);

		if (views.display_period > 0.0f)
			period = (uint64_t)((double)views.display_period * 1000000000.0);
		if (index >= 0)
		{
			float left[3], right[3];

			if (host_vr_loading_eyes(left, right))
				xgpu_vulkan_loading_eyes(left, right);
			xgpu_vulkan_loading_stereo(1);
			image = xgpu_vulkan_loading_draw(frame, (uint32_t)info->hud_width, (uint32_t)info->hud_height,
				format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ?
				XGPU_VULKAN_FORMAT_RGBA8 : XGPU_VULKAN_FORMAT_BGRA8);
			if (image)
			{
				loading_capture(frame, image);
				xgpu_vulkan_image_to_runtime(image, host_vr_vulkan_image(VR_SWAPCHAIN_HUD, index), 1);
			}
			/* Queue the copy before releasing the image to OpenXR. Its
			graphics binding uses this same queue. */
			xgpu_vulkan_fence();
			host_vr_loading_stereo(image && xgpu_vulkan_loading_stereo_drawn(), xgpu_vulkan_loading_stereo_tangent());
			host_vr_loading_end(frame);
		}
		return 1000000;
	}
#endif
	xgpu_vulkan_loading_stereo(0);
	/* (the frame at the window's shape, which it then fills: at the
	window's rows where the loading screen draws them sharp; with a
	caption, at the window's pixels, the caption drawn at them) */
	{
		uint32_t frame_width = xgpu_vulkan_loading_frame_width(), frame_height = xgpu_vulkan_loading_frame_height();
		uint32_t window_width, window_height;

		if (xgpu_vulkan_loading_caption_size(&window_width, &window_height))
		{
			frame_width = window_width;
			frame_height = window_height;
		}
		image = xgpu_vulkan_loading_draw(frame, frame_width, frame_height, XGPU_VULKAN_FORMAT_BGRA8);
	}
	if (image)
	{
		loading_capture(frame, image);
		xgpu_vulkan_loading_present(image);
		host_load_trace("loading.submitted", "epoch=%llu serial=%llu elapsed_ns=%llu",
			(unsigned long long)frame->epoch, (unsigned long long)frame->serial,
			(unsigned long long)frame->elapsed_ns);
	}
	return period;
}
