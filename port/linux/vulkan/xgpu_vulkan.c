/*
XGPU_VULKAN.C

The Vulkan backend's core (xgpu_vulkan.h): the device and its memory, the
resources the command stream's draws need, and the commands that draw them.

Recording: the work of a submission is two command buffers, submitted
together. The second (main) has the draws, the clears and what is ordered
between them; the first (early) has the uploads into images and buffers that
no work recorded so far in this submission has used, which can then be done
before all of it, without ending a pass for them. A barrier begins the early
work (after everything submitted before it) and ends it (before everything
after it).

Every image is made in the GENERAL layout and stays in it, sampled, drawn
into and copied; the barrier at the end of each pass and after each copy
makes what it wrote visible to everything after it.

Submissions are numbered by a timeline semaphore. What a submission uses
(its command buffers and descriptor pool, its bytes of the upload ring, the
images and buffers let go of while it was recorded) is reused or freed once
the GPU has passed its number. A frame's visibility tests are read from its
query pool once its last submission is done.

display.hdr (docs/hdr.md). A draw that blends in its shader (its pipeline's
color_read) reads the colour attachment where it draws, as input
attachment 0 of a third descriptor set (VK_KHR_dynamic_rendering_local_read:
the attachment stays in the GENERAL layout, which local reads allow, and is
the input of the same index by default). Its reads see the draws before it
through a framebuffer-local barrier before it, or, where the device orders
attachment reads in rasterization order
(VK_EXT_rasterization_order_attachment_access), with no barrier, its own
overlapping triangles in order too. Where the device has the fragment
shader interlock instead (VK_EXT_fragment_shader_interlock), a draw that
writes no depth (its pipeline's color_store) reads and writes the
attachment as a storage image of the same set, its own triangles ordered by
the interlock, a run of them after a barrier and with one after it, its
pipeline writing no colour; the targets it writes are uncompressed where
the device can say so (VK_EXT_image_compression_control). Where the
frames are HDR and the window's display shows HDR, the swapchain is HDR
(VK_EXT_swapchain_colorspace: scRGB's half floats in extended linear
sRGB, or HDR10's PQ in BT.2020's primaries) and the frame is encoded for it
(the options' encode) before it is letterboxed into it as an SDR frame is;
the swapchain is made again as that changes. An SDR swapchain's frame is curved by display.gamma (the
options' gamma) first, where the setting is not 1.
*/

#include "xgpu_vulkan.h"
#include "xgpu_vulkan_loading.h"
#include "xgpu_vulkan_pacing.h"
#include "xgpu_vulkan_post_shaders.h"
#include "host.h"
#include "host_load_trace.h"
#include "../src/loading_presentation.h"
#include "../src/loading_art.h"
/* (the reference's xgpu_hdr_sdr_pixel: the screenshot key's pictures) */
#define XGPU_HDR_REFERENCE
#include "../src/xgpu_hdr.h"
#include "../src/xgpu_stream.h"

#ifdef _WIN32
/* (the 64-bit Windows host: Windows's loader, vulkan-1.dll) */
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __ANDROID__
/* port/android/host/host_main.c: display.render_scale */
float host_app_render_scale(void);
#endif

/* ---------- the functions (loaded at run time) */

#define GLOBAL_FUNCTIONS(F) \
	F(vkCreateInstance) F(vkEnumerateInstanceExtensionProperties) F(vkEnumerateInstanceLayerProperties) \
	F(vkEnumerateInstanceVersion)

#define INSTANCE_FUNCTIONS(F) \
	F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) \
	F(vkGetPhysicalDeviceFeatures2) F(vkGetPhysicalDeviceQueueFamilyProperties) \
	F(vkGetPhysicalDeviceProperties2) F(vkGetPhysicalDeviceMemoryProperties) F(vkGetPhysicalDeviceFormatProperties) F(vkCreateDevice) \
	F(vkGetDeviceProcAddr) F(vkEnumerateDeviceExtensionProperties) F(vkDestroyInstance)

#define SURFACE_FUNCTIONS(F) \
	F(vkGetPhysicalDeviceSurfaceSupportKHR) F(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	F(vkGetPhysicalDeviceSurfaceFormatsKHR) F(vkGetPhysicalDeviceSurfacePresentModesKHR) F(vkDestroySurfaceKHR)

#define DEBUG_FUNCTIONS(F) \
	F(vkCreateDebugUtilsMessengerEXT) F(vkDestroyDebugUtilsMessengerEXT)

#define DEVICE_FUNCTIONS(F) \
	F(vkGetDeviceQueue) F(vkDeviceWaitIdle) F(vkQueueSubmit) F(vkDestroyDevice) \
	F(vkCreateCommandPool) F(vkResetCommandPool) F(vkAllocateCommandBuffers) \
	F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkAllocateMemory) F(vkFreeMemory) F(vkMapMemory) \
	F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) \
	F(vkCreateImage) F(vkDestroyImage) F(vkGetImageMemoryRequirements) F(vkBindImageMemory) \
	F(vkCreateImageView) F(vkDestroyImageView) F(vkCreateSampler) \
	F(vkCreateShaderModule) F(vkCreatePipelineCache) \
	F(vkGetPipelineCacheData) F(vkCreateGraphicsPipelines) F(vkCreatePipelineLayout) \
	F(vkCreateDescriptorSetLayout) \
	F(vkCreateDescriptorPool) F(vkResetDescriptorPool) F(vkAllocateDescriptorSets) \
	F(vkUpdateDescriptorSets) F(vkCreateSemaphore) F(vkDestroySemaphore) F(vkWaitSemaphores) \
	F(vkGetSemaphoreCounterValue) F(vkCreateQueryPool) F(vkResetQueryPool) \
	F(vkGetQueryPoolResults) \
	F(vkCmdPipelineBarrier) F(vkCmdBeginRendering) F(vkCmdEndRendering) F(vkCmdBindPipeline) F(vkCmdSetViewport) \
	F(vkCmdSetScissor) F(vkCmdSetDepthBias) F(vkCmdSetBlendConstants) F(vkCmdSetStencilReference) \
	F(vkCmdSetPrimitiveTopology) F(vkCmdBindVertexBuffers) F(vkCmdBindIndexBuffer) F(vkCmdBindDescriptorSets) \
	F(vkCmdDraw) F(vkCmdDrawIndexed) F(vkCmdClearAttachments) F(vkCmdCopyBuffer) F(vkCmdCopyBufferToImage) \
	F(vkCmdCopyImageToBuffer) F(vkCmdCopyImage) F(vkCmdBlitImage) F(vkCmdResolveImage) F(vkCmdClearColorImage) \
	F(vkCmdClearDepthStencilImage) F(vkCmdBeginQuery) F(vkCmdEndQuery) F(vkCmdWriteTimestamp) \
	F(vkCmdExecuteCommands) F(vkCreateComputePipelines) F(vkCmdDispatch) F(vkCmdPushConstants)

#define SWAPCHAIN_FUNCTIONS(F) \
	F(vkCreateSwapchainKHR) F(vkDestroySwapchainKHR) F(vkGetSwapchainImagesKHR) F(vkAcquireNextImageKHR) \
	F(vkQueuePresentKHR)

#define DECLARE(name) static PFN_##name name;
GLOBAL_FUNCTIONS(DECLARE)
INSTANCE_FUNCTIONS(DECLARE)
SURFACE_FUNCTIONS(DECLARE)
DEBUG_FUNCTIONS(DECLARE)
DEVICE_FUNCTIONS(DECLARE)
SWAPCHAIN_FUNCTIONS(DECLARE)
#undef DECLARE
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;

/* ---------- the driver's calls watched (xgpu_vulkan_calls_watch: the
replay's cost, port/tools/replay): while a thread watches, each call it
makes of these (by the pointers above) is counted and timed, by its kind,
into the watcher's counters; the pointers are the watched functions' only
while some thread watches, and each passes a call of another thread on
untimed */

static uint64_t *calls_ticks, *calls_counts;
static uint64_t (*calls_clock)(void);
static _Thread_local int calls_watching;
static int calls_installed;

const char *const xgpu_vulkan_call_kind_names[XGPU_VULKAN_CALL_KINDS] =
{
	"bind_pipeline", "bind_descriptors", "update_descriptors", "allocate_descriptors", "push_constants",
	"bind_buffers", "dynamic_state", "draw", "clear", "begin_commands", "end_commands", "execute_commands",
	"rendering", "barrier", "copy", "query", "submit", "wait", "create",
};

/* (the watched call's time and count added, the total's in the last) */
#define CALL_COUNTED(kind, start) \
	do \
	{ \
		uint64_t elapsed = calls_clock() - (start); \
		calls_ticks[kind] += elapsed; \
		calls_ticks[XGPU_VULKAN_CALL_KINDS] += elapsed; \
		calls_counts[kind]++; \
		calls_counts[XGPU_VULKAN_CALL_KINDS]++; \
	} while (0)

#define WATCHED_VOID(kind, name, parameters, arguments) \
	static PFN_##name name##_unwatched; \
	static VKAPI_ATTR void VKAPI_CALL name##_watched parameters \
	{ \
		uint64_t start; \
		if (!calls_watching) \
		{ \
			name##_unwatched arguments; \
			return; \
		} \
		start = calls_clock(); \
		name##_unwatched arguments; \
		CALL_COUNTED(XGPU_VULKAN_CALL_##kind, start); \
	}
#define WATCHED_RESULT(kind, name, parameters, arguments) \
	static PFN_##name name##_unwatched; \
	static VKAPI_ATTR VkResult VKAPI_CALL name##_watched parameters \
	{ \
		uint64_t start; \
		VkResult result; \
		if (!calls_watching) \
			return name##_unwatched arguments; \
		start = calls_clock(); \
		result = name##_unwatched arguments; \
		CALL_COUNTED(XGPU_VULKAN_CALL_##kind, start); \
		return result; \
	}

WATCHED_VOID(BIND_PIPELINE, vkCmdBindPipeline, (VkCommandBuffer a, VkPipelineBindPoint b, VkPipeline c),
	(a, b, c))
WATCHED_VOID(BIND_DESCRIPTORS, vkCmdBindDescriptorSets, (VkCommandBuffer a, VkPipelineBindPoint b, VkPipelineLayout c, uint32_t d, uint32_t e, const VkDescriptorSet *f, uint32_t g, const uint32_t *h),
	(a, b, c, d, e, f, g, h))
WATCHED_VOID(UPDATE_DESCRIPTORS, vkUpdateDescriptorSets, (VkDevice a, uint32_t b, const VkWriteDescriptorSet *c, uint32_t d, const VkCopyDescriptorSet *e),
	(a, b, c, d, e))
WATCHED_RESULT(ALLOCATE_DESCRIPTORS, vkAllocateDescriptorSets, (VkDevice a, const VkDescriptorSetAllocateInfo *b, VkDescriptorSet *c),
	(a, b, c))
WATCHED_RESULT(ALLOCATE_DESCRIPTORS, vkResetDescriptorPool, (VkDevice a, VkDescriptorPool b, VkDescriptorPoolResetFlags c),
	(a, b, c))
WATCHED_VOID(PUSH_CONSTANTS, vkCmdPushConstants, (VkCommandBuffer a, VkPipelineLayout b, VkShaderStageFlags c, uint32_t d, uint32_t e, const void *f),
	(a, b, c, d, e, f))
WATCHED_VOID(BIND_BUFFERS, vkCmdBindVertexBuffers, (VkCommandBuffer a, uint32_t b, uint32_t c, const VkBuffer *d, const VkDeviceSize *e),
	(a, b, c, d, e))
WATCHED_VOID(BIND_BUFFERS, vkCmdBindIndexBuffer, (VkCommandBuffer a, VkBuffer b, VkDeviceSize c, VkIndexType d),
	(a, b, c, d))
WATCHED_VOID(DYNAMIC_STATE, vkCmdSetViewport, (VkCommandBuffer a, uint32_t b, uint32_t c, const VkViewport *d),
	(a, b, c, d))
WATCHED_VOID(DYNAMIC_STATE, vkCmdSetScissor, (VkCommandBuffer a, uint32_t b, uint32_t c, const VkRect2D *d),
	(a, b, c, d))
WATCHED_VOID(DYNAMIC_STATE, vkCmdSetDepthBias, (VkCommandBuffer a, float b, float c, float d),
	(a, b, c, d))
WATCHED_VOID(DYNAMIC_STATE, vkCmdSetBlendConstants, (VkCommandBuffer a, const float b[4]),
	(a, b))
WATCHED_VOID(DYNAMIC_STATE, vkCmdSetStencilReference, (VkCommandBuffer a, VkStencilFaceFlags b, uint32_t c),
	(a, b, c))
WATCHED_VOID(DYNAMIC_STATE, vkCmdSetPrimitiveTopology, (VkCommandBuffer a, VkPrimitiveTopology b),
	(a, b))
WATCHED_VOID(DRAW, vkCmdDraw, (VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, uint32_t e),
	(a, b, c, d, e))
WATCHED_VOID(DRAW, vkCmdDrawIndexed, (VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d, int32_t e, uint32_t f),
	(a, b, c, d, e, f))
WATCHED_VOID(DRAW, vkCmdDispatch, (VkCommandBuffer a, uint32_t b, uint32_t c, uint32_t d),
	(a, b, c, d))
WATCHED_VOID(CLEAR, vkCmdClearAttachments, (VkCommandBuffer a, uint32_t b, const VkClearAttachment *c, uint32_t d, const VkClearRect *e),
	(a, b, c, d, e))
WATCHED_VOID(CLEAR, vkCmdClearColorImage, (VkCommandBuffer a, VkImage b, VkImageLayout c, const VkClearColorValue *d, uint32_t e, const VkImageSubresourceRange *f),
	(a, b, c, d, e, f))
WATCHED_VOID(CLEAR, vkCmdClearDepthStencilImage, (VkCommandBuffer a, VkImage b, VkImageLayout c, const VkClearDepthStencilValue *d, uint32_t e, const VkImageSubresourceRange *f),
	(a, b, c, d, e, f))
WATCHED_RESULT(BEGIN_COMMANDS, vkBeginCommandBuffer, (VkCommandBuffer a, const VkCommandBufferBeginInfo *b),
	(a, b))
WATCHED_RESULT(END_COMMANDS, vkEndCommandBuffer, (VkCommandBuffer a),
	(a))
WATCHED_VOID(EXECUTE_COMMANDS, vkCmdExecuteCommands, (VkCommandBuffer a, uint32_t b, const VkCommandBuffer *c),
	(a, b, c))
WATCHED_VOID(RENDERING, vkCmdBeginRendering, (VkCommandBuffer a, const VkRenderingInfo *b),
	(a, b))
WATCHED_VOID(RENDERING, vkCmdEndRendering, (VkCommandBuffer a),
	(a))
WATCHED_VOID(BARRIER, vkCmdPipelineBarrier, (VkCommandBuffer a, VkPipelineStageFlags b, VkPipelineStageFlags c, VkDependencyFlags d, uint32_t e, const VkMemoryBarrier *f, uint32_t g, const VkBufferMemoryBarrier *h, uint32_t i, const VkImageMemoryBarrier *j),
	(a, b, c, d, e, f, g, h, i, j))
WATCHED_VOID(COPY, vkCmdCopyBuffer, (VkCommandBuffer a, VkBuffer b, VkBuffer c, uint32_t d, const VkBufferCopy *e),
	(a, b, c, d, e))
WATCHED_VOID(COPY, vkCmdCopyBufferToImage, (VkCommandBuffer a, VkBuffer b, VkImage c, VkImageLayout d, uint32_t e, const VkBufferImageCopy *f),
	(a, b, c, d, e, f))
WATCHED_VOID(COPY, vkCmdCopyImageToBuffer, (VkCommandBuffer a, VkImage b, VkImageLayout c, VkBuffer d, uint32_t e, const VkBufferImageCopy *f),
	(a, b, c, d, e, f))
WATCHED_VOID(COPY, vkCmdCopyImage, (VkCommandBuffer a, VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageCopy *g),
	(a, b, c, d, e, f, g))
WATCHED_VOID(COPY, vkCmdBlitImage, (VkCommandBuffer a, VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageBlit *g, VkFilter h),
	(a, b, c, d, e, f, g, h))
WATCHED_VOID(COPY, vkCmdResolveImage, (VkCommandBuffer a, VkImage b, VkImageLayout c, VkImage d, VkImageLayout e, uint32_t f, const VkImageResolve *g),
	(a, b, c, d, e, f, g))
WATCHED_VOID(QUERY, vkCmdBeginQuery, (VkCommandBuffer a, VkQueryPool b, uint32_t c, VkQueryControlFlags d),
	(a, b, c, d))
WATCHED_VOID(QUERY, vkCmdEndQuery, (VkCommandBuffer a, VkQueryPool b, uint32_t c),
	(a, b, c))
WATCHED_VOID(QUERY, vkCmdWriteTimestamp, (VkCommandBuffer a, VkPipelineStageFlagBits b, VkQueryPool c, uint32_t d),
	(a, b, c, d))
WATCHED_VOID(QUERY, vkResetQueryPool, (VkDevice a, VkQueryPool b, uint32_t c, uint32_t d),
	(a, b, c, d))
WATCHED_RESULT(QUERY, vkGetQueryPoolResults, (VkDevice a, VkQueryPool b, uint32_t c, uint32_t d, size_t e, void *f, VkDeviceSize g, VkQueryResultFlags h),
	(a, b, c, d, e, f, g, h))
WATCHED_RESULT(SUBMIT, vkQueueSubmit, (VkQueue a, uint32_t b, const VkSubmitInfo *c, VkFence d),
	(a, b, c, d))
WATCHED_RESULT(WAIT, vkWaitSemaphores, (VkDevice a, const VkSemaphoreWaitInfo *b, uint64_t c),
	(a, b, c))
WATCHED_RESULT(WAIT, vkGetSemaphoreCounterValue, (VkDevice a, VkSemaphore b, uint64_t *c),
	(a, b, c))
WATCHED_RESULT(WAIT, vkDeviceWaitIdle, (VkDevice a),
	(a))
WATCHED_RESULT(CREATE, vkCreateSampler, (VkDevice a, const VkSamplerCreateInfo *b, const VkAllocationCallbacks *c, VkSampler *d),
	(a, b, c, d))
WATCHED_RESULT(CREATE, vkCreateImageView, (VkDevice a, const VkImageViewCreateInfo *b, const VkAllocationCallbacks *c, VkImageView *d),
	(a, b, c, d))
WATCHED_RESULT(CREATE, vkCreateImage, (VkDevice a, const VkImageCreateInfo *b, const VkAllocationCallbacks *c, VkImage *d),
	(a, b, c, d))
WATCHED_RESULT(CREATE, vkCreateBuffer, (VkDevice a, const VkBufferCreateInfo *b, const VkAllocationCallbacks *c, VkBuffer *d),
	(a, b, c, d))
WATCHED_RESULT(CREATE, vkAllocateMemory, (VkDevice a, const VkMemoryAllocateInfo *b, const VkAllocationCallbacks *c, VkDeviceMemory *d),
	(a, b, c, d))
WATCHED_RESULT(CREATE, vkAllocateCommandBuffers, (VkDevice a, const VkCommandBufferAllocateInfo *b, VkCommandBuffer *c),
	(a, b, c))
WATCHED_RESULT(CREATE, vkResetCommandPool, (VkDevice a, VkCommandPool b, VkCommandPoolResetFlags c),
	(a, b, c))
WATCHED_RESULT(CREATE, vkCreateGraphicsPipelines, (VkDevice a, VkPipelineCache b, uint32_t c, const VkGraphicsPipelineCreateInfo *d, const VkAllocationCallbacks *e, VkPipeline *f),
	(a, b, c, d, e, f))
#undef WATCHED_VOID
#undef WATCHED_RESULT

#define WATCHED_FUNCTIONS(F) \
	F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkUpdateDescriptorSets) F(vkAllocateDescriptorSets) \
	F(vkResetDescriptorPool) F(vkCmdPushConstants) F(vkCmdBindVertexBuffers) F(vkCmdBindIndexBuffer) \
	F(vkCmdSetViewport) F(vkCmdSetScissor) F(vkCmdSetDepthBias) F(vkCmdSetBlendConstants) \
	F(vkCmdSetStencilReference) F(vkCmdSetPrimitiveTopology) F(vkCmdDraw) F(vkCmdDrawIndexed) \
	F(vkCmdDispatch) F(vkCmdClearAttachments) F(vkCmdClearColorImage) F(vkCmdClearDepthStencilImage) \
	F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkCmdExecuteCommands) F(vkCmdBeginRendering) \
	F(vkCmdEndRendering) F(vkCmdPipelineBarrier) F(vkCmdCopyBuffer) F(vkCmdCopyBufferToImage) \
	F(vkCmdCopyImageToBuffer) F(vkCmdCopyImage) F(vkCmdBlitImage) F(vkCmdResolveImage) \
	F(vkCmdBeginQuery) F(vkCmdEndQuery) F(vkCmdWriteTimestamp) F(vkResetQueryPool) \
	F(vkGetQueryPoolResults) F(vkQueueSubmit) F(vkWaitSemaphores) F(vkGetSemaphoreCounterValue) \
	F(vkDeviceWaitIdle) F(vkCreateSampler) F(vkCreateImageView) F(vkCreateImage) \
	F(vkCreateBuffer) F(vkAllocateMemory) F(vkAllocateCommandBuffers) F(vkResetCommandPool) \
	F(vkCreateGraphicsPipelines)

void xgpu_vulkan_calls_watch(uint64_t *ticks, uint64_t *counts, uint64_t (*clock)(void))
{
	if (ticks && counts && clock)
	{
		calls_ticks = ticks;
		calls_counts = counts;
		calls_clock = clock;
		if (!calls_installed)
		{
#define INSTALL(name) name##_unwatched = name; name = name##_watched;
			WATCHED_FUNCTIONS(INSTALL)
#undef INSTALL
			calls_installed = 1;
		}
		calls_watching = 1;
		return;
	}
	calls_watching = 0;
	if (calls_installed)
	{
#define UNINSTALL(name) name = name##_unwatched;
		WATCHED_FUNCTIONS(UNINSTALL)
#undef UNINSTALL
		calls_installed = 0;
	}
}

/* (VK_EXT_hdr_metadata's, where the device has it) */
static PFN_vkSetHdrMetadataEXT vkSetHdrMetadataEXT;

/* display.hdr's half (docs/hdr.md) is the desktop's: Android's displays
and the VR mode's headsets draw SDR, and their devices are asked for
nothing more */
#if !defined(__ANDROID__) && !defined(HALO_VR)
#define HDR_POSSIBLE 1
#else
#define HDR_POSSIBLE 0
#endif

/* ---------- sizes */

/* submissions whose resources are kept until the GPU is done with them */
#define SLOTS 8
/* vkAcquireNextImageKHR's wait, in parts; the wait after which the stall is
noted */
#define ACQUIRE_PART_NS 100000000ull
#define ACQUIRE_STALL_NS 2000000000ull
/* the upload ring */
#define RING_SIZE (128u * 1024 * 1024)
/* images' memory, in blocks of this (larger images have their own) */
#define BLOCK_SIZE (64u * 1024 * 1024)
#define SAMPLER_CACHE 1024
#define PIPELINE_BUCKETS 4096
#define SHADER_BUCKETS 1024
/* the fixed programs' modules, found by their words' address
(xgpu_vulkan_shader_constant) */
#define CONSTANT_SHADERS 128
/* frames whose visibility tests may be in flight, and the queries a frame
has (a test can be several, one for each pass it spans) */
#define QUERY_FRAMES 4
/* Timestamp intervals may span several submissions; never reuse an
in-flight pair, and skip samples rather than wait if all pairs are busy. */
#define GPU_TIME_FRAMES 32
#define QUERIES_A_FRAME 8192
/* texture descriptor sets a submission's pool has, before another pool */
#define SETS_A_POOL 1024
/* the texture sets kept from one submission to the next, and their buckets,
by what they have */
#define TEXTURE_SETS_KEPT 4096
#define TEXTURE_SET_BUCKETS 4096

/* ---------- memory */

struct free_range
{
	VkDeviceSize offset, size;
};

struct block
{
	VkDeviceMemory memory;
	VkDeviceSize size;
	uint32_t type;
	struct free_range *free;
	uint32_t free_count, free_capacity;
	struct block *next;
};

struct allocation
{
	struct block *block;
	VkDeviceMemory memory;
	VkDeviceSize offset, size;
	/* a dedicated allocation (block NULL): its memory is its own */
};

/* ---------- resources */

struct xgpu_vulkan_image
{
	struct xgpu_vulkan_image_description description;
	VkImage image;
	VkFormat format;
	VkImageAspectFlags aspect;
	struct allocation memory;
	/* sampled (depth alone for a depth/stencil image), an array's first
	layer sampled as 2D, and attachment views (made on their first use) */
	VkImageView sampled, sampled_2d;
	VkImageView *attachments;
	uint32_t attachment_count;
	/* the submission whose recording last used it (draws, copies) */
	uint64_t used;
	/* its number, never given to another image: a texture set is cached
	by its images' addresses and numbers, so an image made where a
	destroyed one was is never given the destroyed one's sets */
	uint64_t number;
	/* HALO_VULKAN_DISCARDS (the passes' stores chosen at the submission):
	the last pass of the submission being recorded (pass_serial) that has
	it as an attachment (its segment, plus one: 0 none), and which of the
	pass's attachments it is (0 colour, 1 depth and stencil); the number
	of segments when the submission last used it otherwise (its
	use_serial); the frame of its first use, plus one; the frames in a
	row whose first use cleared it whole, by aspect (colour or depth,
	stencil), and whether what it held at the last frame's end was
	discarded; and whether it is a transient attachment (its samples in
	lazily allocated memory, where the device has it) */
	uint64_t pass_serial, use_serial, first_use_frame;
	uint32_t pass_segment, use_mark;
	/* with discards: a count of what wrote it (passes with it as an
	attachment, copies, uploads, resolves into it); a multisampled
	image's last resolve, the number of the image it went into and both
	images' counts after it (xgpu_vulkan_image_resolve) */
	uint32_t writes, resolved_writes, resolved_into_writes;
	uint64_t resolved_into;
	/* with aside passes: the chain of the passes put aside from whose
	pieces sampled it (its chain_number) */
	uint64_t aside_read;
	uint8_t pass_slot, clears[2], discarded[2], transient;
};

struct xgpu_vulkan_buffer
{
	VkBuffer buffer;
	struct allocation memory;
	void *mapped;
	uint32_t size;
	uint64_t used;
	/* with aside passes: the submission whose early commands copy its
	bytes from the buffer it took the place of (buffer_rename), and the
	last whose main commands wrote into it (which an early copy would
	come before: not renamed in it) */
	uint64_t renamed, ordered;
	/* a descriptor set made once names it (the draws' set: the enhanced
	dynamic lights' block, the visibility counts): never renamed, which
	would leave the set naming the buffer it took the place of, a spare
	that is reused or let go of while draws still read it */
	uint8_t named_by_set;
};

/* things let go of, freed once the GPU has passed their submission */
enum
{
	_garbage_image, _garbage_buffer, _garbage_view,
};

struct garbage
{
	int kind;
	uint64_t serial;
	VkImage image;
	VkImageView view;
	VkBuffer buffer;
	struct allocation memory;
};

/* what a texture set has: each stage's view and sampler as they are
written, and the number of the image the view is of (a destroyed image's
sets are never found, even by a view made with its handle; samplers are
never destroyed) */
struct texture_set_key
{
	VkImageView views[4];
	VkSampler samplers[4];
	uint64_t numbers[4];
};

struct texture_set_entry
{
	struct texture_set_key key;
	VkDescriptorSet set;
	/* the last submission that drew with it */
	uint64_t used;
	/* the next in its bucket (an index plus one; 0 ends it), and its
	bucket */
	uint32_t next, bucket;
};

/* a submission's own */
struct slot
{
	uint64_t serial;
	VkCommandPool pool;
	VkCommandBuffer main, early;
	VkDescriptorPool *descriptor_pools;
	uint32_t descriptor_pool_count, descriptor_pool_used;
	/* the pools allocated from since they were reset */
	uint32_t descriptor_pool_dirty;
	/* the upload ring's position its uploads end at */
	uint64_t ring_end;
	/* HALO_VULKAN_DISCARDS: the secondary command buffers allocated from
	its pool, and those used since it was reset */
	VkCommandBuffer *secondaries;
	uint32_t secondary_count, secondary_used;
};

/* HALO_VULKAN_DISCARDS: a piece of the main command buffer's work, in its
order, recorded into a secondary command buffer and put into the main one
at the submission: work outside a pass, or a pass's draws, whose
vkCmdBeginRendering is made only then, when what the later pieces do
with its attachments is known (segments_play) */
struct pass_attachment
{
	/* NULL: none (or the image destroyed since) */
	struct xgpu_vulkan_image *image;
	/* (the depth's info is the stencil's but for its load) */
	VkRenderingAttachmentInfo info;
	VkAttachmentLoadOp stencil_load;
	/* by aspect (colour or depth, stencil): a later use needs what the
	pass stores; a later pass clears all of it first */
	uint8_t keep[2], killed[2];
	/* the view of the image resolved into as the pass ends
	(xgpu_vulkan_image_resolve) */
	VkImageView resolve_view;
};

struct segment
{
	VkCommandBuffer commands;
	int pass;
	/* (debug.pass_times) the function that ended the pass */
	const char *ended_by;
	/* with aside passes: a pass that continues the chain's pass before it
	(piece), and a chain's first pass, the last of its pieces (its index
	plus one; segments_play) */
	int piece;
	uint32_t chain_last;
	/* a pass's */
	VkRect2D area;
	uint32_t view_mask;
	/* the targets' first layer and views */
	uint32_t layer, views;
	struct pass_attachment color, depth;
};

/* a frame's visibility tests: its queries, and the slot and area of each */
struct query_frame
{
	VkQueryPool pool;
	uint32_t count;
	uint16_t slots[QUERIES_A_FRAME];
	float areas[QUERIES_A_FRAME];
	/* the test each query is of (a test of several passes: several), and
	the game's number for it */
	uint32_t tests[QUERIES_A_FRAME];
	uint32_t numbers[QUERIES_A_FRAME];
	/* the slots of tests that drew in no pass (a count of 0), and their
	numbers */
	uint16_t empty[QUERIES_A_FRAME];
	uint32_t empty_numbers[QUERIES_A_FRAME];
	uint32_t empty_count;
	uint16_t counter_slots[XGPU_VULKAN_VISIBILITY_SLOTS];
	float counter_areas[XGPU_VULKAN_VISIBILITY_SLOTS];
	uint32_t counter_numbers[XGPU_VULKAN_VISIBILITY_SLOTS];
	uint32_t counter_count;
	/* the submission that ends the frame (0: the frame is being recorded),
	and the frame's number */
	uint64_t serial;
	uint64_t frame;
	int in_use;
};

struct pipeline_entry
{
	struct pipeline_entry *next;
	uint32_t hash;
	struct xgpu_vulkan_pipeline_description description;
	VkPipeline pipeline;
	int pending;
	/* (made apart from the draws, xgpu_vulkan_pipeline_ready's: not yet
	handed to the compilers, their queue full) */
	int waiting;
};

struct shader_entry
{
	struct shader_entry *next;
	uint64_t hash;
	size_t count;
	uint32_t *words;
	VkShaderModule module;
};

static struct
{
	void *library;
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;
	VkPhysicalDevice physical;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceMemoryProperties memory_properties;
	VkDevice device;
	VkQueue queue;
	uint32_t queue_family, timestamp_bits;
	int validation;
	uint32_t validation_messages;

	struct xgpu_vulkan_capabilities capabilities;
	VkFormat depth_format;
	/* NVIDIA's own driver (its anisotropic filtering: anisotropic_bias) */
	int nvidia;
	/* cube maps sampled face by face (VK_EXT_non_seamless_cube_map) */
	int non_seamless;
	/* display.hdr's interlocked draws: the targets they write as storage
	images made uncompressed (VK_EXT_image_compression_control); and one
	drawn whose writes the pass's other commands have not yet been made to
	see (color_stores_seen) */
	int compression_control, color_stored;

	/* memory blocks for images, by memory type */
	struct block *blocks;
	/* the last number given to an image */
	uint64_t image_number;

	/* the timeline: the next submission's number, the last done */
	VkSemaphore timeline;
	uint64_t serial;
	uint64_t completed;
	struct slot slots[SLOTS];
	struct slot *slot;
	int recording, early_recording;

	/* the upload ring: its buffer, mapped, and the running count of bytes
	given out (its position: modulo its size) */
	VkBuffer ring;
	struct allocation ring_memory;
	unsigned char *ring_mapped;
	uint64_t ring_head, ring_free_from;

	struct garbage *garbage;
	uint32_t garbage_count, garbage_capacity;

	/* the readback buffer (read_pixels) */
	VkBuffer readback;
	struct allocation readback_memory;
	unsigned char *readback_mapped;
	VkDeviceSize readback_size;

	VkDescriptorSetLayout draw_layout, texture_layout;
	VkPipelineLayout pipeline_layout;
	VkDescriptorPool draw_pool;
	VkDescriptorSet draw_set;
	/* the draw set's other resources: the enhanced dynamic lights' block
	and cells, and the visibility counters (unused by the flat game) */
	struct xgpu_vulkan_buffer *lights_block, *samples;
	struct xgpu_vulkan_image *lights_cells;
	/* the cells uploaded since lights_cells was made (else they are all
	uploaded, whatever changed) */
	int lights_cells_uploaded;
	VkSampler stand_in_sampler;
	struct xgpu_vulkan_image *stand_ins[4];
	/* Presentation owns this image across loading frames, independently
	of the guest's render targets and map allocations. */
	struct xgpu_vulkan_image *loading_image;
	struct xgpu_vulkan_image *loading_art_image;
	/* the caption over it, at the loading image's pixels
	(loading_art_caption_shown) */
	struct xgpu_vulkan_image *loading_caption_image;
	struct loading_art loading_art;
	/* the last loading image held VR's stereo pair (loading_presentation.h) */
	int loading_stereo_drawn;

	VkPipelineCache pipeline_cache;
	uint64_t pipeline_count;
	/* the pipelines made (their compilation finished), and their count
	when the cache was last written */
	uint64_t pipelines_made, pipelines_saved;
	char cache_path[1024];
	struct pipeline_entry *pipelines[PIPELINE_BUCKETS];
	struct shader_entry *shaders[SHADER_BUCKETS];
	struct
	{
		const uint32_t *words;
		size_t count;
		VkShaderModule module;
	} constant_shaders[CONSTANT_SHADERS];
	uint32_t constant_shader_count;
	/* the shaders of the clears of some of the colour's channels */
	VkShaderModule clear_vertex, clear_pixel;
	struct
	{
		struct xgpu_vulkan_sampler_description description;
		VkSampler sampler;
	} samplers[SAMPLER_CACHE];
	uint32_t sampler_count;

	/* the targets bound, and the pass drawing into them */
	struct xgpu_vulkan_image *color, *depth;
	uint32_t layer, views;
	int pass_active;
	uint32_t pass_width, pass_height;
	/* the pass's draws sample its depth attachment (xgpu_vulkan_draw's
	reads_depth): it writes neither depth nor stencil, nor stores them */
	int pass_reads_depth;
	/* what the main command buffer has bound in this pass */
	VkPipeline bound_pipeline;
	/* and the rest of the state a draw sets, as the main command buffer
	has it (every pipeline has the same dynamic state and layout, so a
	pipeline bound or a pass begun keeps it): set again only where it
	changes, as the driver's CPU time is much of a frame's on a phone's or
	a headset's GPU (xgpu_vulkan_draw); all of it unknown as a recording
	begins (dynamic set) */
	struct
	{
		int dynamic;
		VkViewport viewport;
		VkRect2D scissor;
		float depth_bias[2];
		float blend[4];
		uint32_t stencil_reference;
		VkPrimitiveTopology topology;
		VkDescriptorSet sets[2];
		uint32_t offsets[2];
		/* (display.hdr's input set; display.ambient_occlusion "world"'s set) */
		VkDescriptorSet input, occlusion;
		VkBuffer vertices[XGPU_VULKAN_BINDINGS];
		VkDeviceSize vertex_offsets[XGPU_VULKAN_BINDINGS];
		VkBuffer indices;
		VkDeviceSize index_offset;
	} bound;
	VkDescriptorSet bound_textures;
	struct xgpu_vulkan_texture last_textures[4];
	VkDescriptorSet last_texture_set;
	int last_texture_valid;
	/* the texture sets kept (texture_set), from a pool of their own, never
	reset: each bucket's first (an index plus one), and where the next one
	written again is looked for */
	VkDescriptorPool texture_set_pool;
	struct texture_set_entry *texture_sets;
	uint32_t texture_set_count, texture_set_hand;
	uint32_t texture_set_buckets[TEXTURE_SET_BUCKETS];

	/* visibility tests; the frames ended */
	volatile uint32_t *visibility_output;
	uint64_t frame;
	struct query_frame query_frames[QUERY_FRAMES];
	struct query_frame *queries;
	int test_active, sample_counting;
	/* the current test's queries (indices in queries), and the one begun */
	uint32_t test_queries[64];
	uint32_t test_query_count;
	uint8_t test_query_widths[64];
	int test_query_open;
	uint32_t test_number;

	VkQueryPool gpu_time_pool;
	struct
	{
		uint64_t serial;
		int used;
	} gpu_times[GPU_TIME_FRAMES];
	uint32_t gpu_time_current;
	int gpu_time_active, gpu_time_armed, gpu_time_failed;

	/* the window: its surface, and what makes it again (NULL: no window) */
	VkSurfaceKHR surface;
	VkSurfaceKHR (*surface_create)(VkInstance instance, void *context);
	void *surface_context;
	/* the app in the background (no surface), and a surface that could
	not be made again (logged once until it is) */
	int window_suspended, surface_failing;
	VkSwapchainKHR swapchain;
	VkFormat swapchain_format;
	VkExtent2D swapchain_extent;
	/* the surface's extent and transform as the swapchain was made for
	them (a SUBOPTIMAL present makes it again only if they changed), and
	the presents found SUBOPTIMAL since (the surface is asked again only
	every SUBOPTIMAL_CHECKS of them) */
	VkExtent2D surface_extent;
	VkSurfaceTransformFlagBitsKHR surface_transform;
	uint32_t suboptimal_presents;
	/* the presents left, after the window's size changed, at which the
	surface is asked again (RESIZE_CHECKS) */
	uint32_t resize_checks;
	/* quarter turns clockwise the frames are drawn turned by, for a
	swapchain made for the display's own way up (preTransform: Android's
	pre-rotation); and what turns them (NULL: none, the compositor turns
	them) */
	int swapchain_turns;
	struct xgpu_vulkan_image *(*rotate)(struct xgpu_vulkan_image *image, int quarter_turns);
	/* display.picture_position: the picture's top in a taller window, in
	its pixels, plus 1; 0 centered (xgpu_vulkan_picture_top; any thread) */
	int picture_top;
	VkImage swapchain_images[8];
	uint32_t swapchain_image_count;
	VkSemaphore acquired[SLOTS], presentable[8];
	int swapchain_stale;
	VkPresentModeKHR present_mode;
	/* debug.vulkan_present_mode's, plus one (0: none asked, the interval's
	choice) */
	int present_mode_asked;
	/* (any thread, atomic) the window's size in pixels, width in the upper
	half, and whether it changed since the render thread last looked; the
	swap interval asked for, plus one (0: none asked) */
	uint64_t window_size;
	uint32_t window_resized, interval_asked, window_hidden;
	/* the swap interval: 0 not waiting for the display, else the refreshes
	each frame is shown for (FIFO; the swapchain's present mode is made for
	0 or more) */
	int interval;
	/* display.low_latency (any thread, atomic: 0 auto, 1 on, 2 off), and
	whether the game is paced by the display now (xgpu_vulkan_pacing.c);
	the device has VK_KHR_present_id and VK_KHR_present_wait, and
	VK_GOOGLE_display_timing */
	uint32_t low_latency;
	int paced, present_wait, display_timing;
	/* the frames ahead of the GPU the stream is told, and one
	(frames_ahead_update; 0: none told) */
	int frames_ahead;

	/* display.hdr: the input attachments' set layout (the pipeline
	layout's third set, where draws can read their colour attachment);
	the input set of the attachment view bound, written for the
	submission of that number (the slot's pools have it) */
	VkDescriptorSetLayout input_layout;
	VkDescriptorSet input_set;
	VkImageView input_view;
	uint64_t input_serial;
	/* display.ambient_occlusion "world": its set's layout (the pipeline
	layout's fourth set, XGPU_VULKAN_SET_OCCLUSION; the third an empty one
	where draws cannot read their colour attachment), and its set, written
	for the submission of that number with the field and block of
	xgpu_vulkan_world_ao.c's number */
	VkDescriptorSetLayout occlusion_layout, empty_layout;
	VkDescriptorSet occlusion_set;
	uint64_t occlusion_serial, occlusion_world_number;
	/* the window's display (any thread, atomic: HDR on, its SDR white's
	and its headroom's bits, and whether they changed since the render
	thread looked); the front end's state; the HDR outputs the surface
	has (bits 1 << XGPU_HDR_OUTPUT_*), the swapchain's output, and what
	was last told of the display (-1: nothing yet). The swapchain is HDR
	where the instance has VK_EXT_swapchain_colorspace and the options an
	encode. */
	uint32_t window_hdr, window_sdr_white, window_headroom, window_hdr_changed;
	int hdr_active, hdr_mode, hdr_paper_white_nits, hdr_paper_white, hdr_ui_nits;
	unsigned int surface_hdr;
	int hdr_output;
	int display_hdr_told, display_window_told;
	unsigned int display_surface_told;
	float display_headroom_told, display_white_told;
	int colorspaces, hdr_metadata;
	void (*display_hdr)(int hdr, float headroom, float white_nits, int white_reported);
	struct xgpu_vulkan_image *(*encode)(struct xgpu_vulkan_image *image, const struct xgpu_hdr_frame *frame,
		uint8_t format);
	/* display.gamma: the options' curve (NULL: none, or it could not be
	made), and the setting (a float's bits; 0 or 1: none) */
	struct xgpu_vulkan_image *(*gamma)(struct xgpu_vulkan_image *image, float power);
	uint32_t gamma_setting;

	/* HALO_VULKAN_DISCARDS: the passes recorded into secondary command
	buffers, their stores chosen at the submission (deferred); the
	depth's and stencil's SAMPLE_ZERO resolve in a pass; multisampled
	images transient (1: in lazily allocated memory; 2: forced, in
	ordinary memory, to try the path on a desktop's GPU); the submission
	being made is the frame's last (frame_ending); the submission's
	segments, the last one's commands still being recorded outside a pass
	(segment_open); where the pass's commands go (the main command buffer
	without discards); a discarded attachment read, logged once */
	int deferred, depth_resolve, transient, frame_ending;
	/* (not the VR mode's, which times its own frames) each frame's GPU
	time and the time between frames' ends counted into the statistics
	(debug.gpu_stats logs them), and the GPU's time so far (nanoseconds:
	xgpu_vulkan_gpu_nanoseconds, a report's); the last frame's end */
	int frame_timing;
	double frame_ended;
	uint64_t gpu_nanoseconds;
	struct segment *segments;
	uint32_t segment_count, segment_capacity;
	int segment_open, discard_logged;
	VkCommandBuffer pass_commands;
	/* without discards: the pass being recorded (into the main command
	buffer) as a segment, for its timestamps' labels, and whether work
	outside passes was recorded since the last timestamp */
	struct segment direct_pass;
	int direct_outside;
	/* HALO_VULKAN_ASIDE (on with discards): passes put aside, before the
	pass they interrupt, which goes on in one rendering ("aside passes",
	below): the chain's first and last pieces (segment indices plus one;
	0: no chain), its number (aside_read), its targets; the outside work
	being recorded is a copy or an upload whose images are checked
	(main_commands); renamed buffers' spares */
	int aside, aside_allowed;
	uint32_t chain_head, chain_last;
	uint64_t chain_number;
	struct xgpu_vulkan_image *chain_color, *chain_depth;
	struct buffer_spare *spares;
	uint32_t spare_count, spare_capacity;

	struct xgpu_vulkan_statistics statistics;
} vk;

/* ---------- logging */

static const char *result_name(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS: return "VK_SUCCESS";
	case VK_NOT_READY: return "VK_NOT_READY";
	case VK_TIMEOUT: return "VK_TIMEOUT";
	case VK_INCOMPLETE: return "VK_INCOMPLETE";
	case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
	case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
	default: return "VkResult";
	}
}

/* a result checked: 1 if it succeeded, else logged (what failed) */
static int check(VkResult result, const char *what)
{
	if (result >= 0)
		return 1;
	host_logf(HOST_LOG_ERROR, "vulkan: %s: %s (%d)", what, result_name(result), (int)result);
	if (result == VK_ERROR_DEVICE_LOST)
		host_fatal("vulkan: the device was lost");
	return 0;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT *data, void *context)
{
	(void)types;
	(void)context;
	/* (any thread: counted roughly) */
	if (vk.validation_messages++ < 1000)
		host_logf(severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? HOST_LOG_ERROR : HOST_LOG_WARN,
			"vulkan validation: %s", data->pMessage ? data->pMessage : "(no message)");
	return VK_FALSE;
}

static double seconds_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

/* ---------- memory */

static int memory_type(uint32_t bits, VkMemoryPropertyFlags wanted, VkMemoryPropertyFlags avoided)
{
	uint32_t index;

	for (index = 0; index < vk.memory_properties.memoryTypeCount; index++)
	{
		VkMemoryPropertyFlags flags = vk.memory_properties.memoryTypes[index].propertyFlags;

		if ((bits & (1u << index)) && (flags & wanted) == wanted && !(flags & avoided))
			return (int)index;
	}
	for (index = 0; index < vk.memory_properties.memoryTypeCount; index++)
	{
		VkMemoryPropertyFlags flags = vk.memory_properties.memoryTypes[index].propertyFlags;

		if ((bits & (1u << index)) && (flags & wanted) == wanted)
			return (int)index;
	}
	return -1;
}

static int memory_dedicated(VkDeviceSize size, uint32_t type, struct allocation *allocation)
{
	VkMemoryAllocateInfo info;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	info.allocationSize = size;
	info.memoryTypeIndex = type;
	memset(allocation, 0, sizeof(*allocation));
	if (!check(vkAllocateMemory(vk.device, &info, NULL, &allocation->memory), "vkAllocateMemory"))
		return 0;
	allocation->size = size;
	return 1;
}

static void block_free_insert(struct block *block, VkDeviceSize offset, VkDeviceSize size)
{
	uint32_t at = 0, index;

	while (at < block->free_count && block->free[at].offset < offset)
		at++;
	/* (merged with its neighbours where they touch) */
	if (at > 0 && block->free[at - 1].offset + block->free[at - 1].size == offset)
	{
		block->free[at - 1].size += size;
		if (at < block->free_count && block->free[at - 1].offset + block->free[at - 1].size == block->free[at].offset)
		{
			block->free[at - 1].size += block->free[at].size;
			memmove(&block->free[at], &block->free[at + 1], (block->free_count - at - 1) * sizeof(block->free[0]));
			block->free_count--;
		}
		return;
	}
	if (at < block->free_count && offset + size == block->free[at].offset)
	{
		block->free[at].offset = offset;
		block->free[at].size += size;
		return;
	}
	if (block->free_count == block->free_capacity)
	{
		uint32_t capacity = block->free_capacity ? block->free_capacity * 2 : 64;
		struct free_range *grown = realloc(block->free, capacity * sizeof(*grown));

		if (!grown)
			return;
		block->free = grown;
		block->free_capacity = capacity;
	}
	for (index = block->free_count; index > at; index--)
		block->free[index] = block->free[index - 1];
	block->free[at].offset = offset;
	block->free[at].size = size;
	block->free_count++;
}

/* memory for an image's requirements, of device-local memory */
static int memory_for(const VkMemoryRequirements *requirements, struct allocation *allocation)
{
	int type = memory_type(requirements->memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	struct block *block;

	if (type < 0)
		return 0;
	if (requirements->size > BLOCK_SIZE / 4)
		return memory_dedicated(requirements->size, (uint32_t)type, allocation);
	for (;;)
	{
		for (block = vk.blocks; block; block = block->next)
		{
			uint32_t index;

			if (block->type != (uint32_t)type)
				continue;
			for (index = 0; index < block->free_count; index++)
			{
				struct free_range *range = &block->free[index];
				VkDeviceSize start = (range->offset + requirements->alignment - 1) & ~(requirements->alignment - 1);

				if (start + requirements->size > range->offset + range->size)
					continue;
				allocation->block = block;
				allocation->memory = block->memory;
				allocation->offset = start;
				allocation->size = requirements->size;
				{
					VkDeviceSize before = start - range->offset;
					VkDeviceSize after = range->offset + range->size - (start + requirements->size);
					VkDeviceSize after_offset = start + requirements->size;

					/* (the range becomes what is left before and after it) */
					memmove(&block->free[index], &block->free[index + 1],
						(block->free_count - index - 1) * sizeof(block->free[0]));
					block->free_count--;
					if (before)
						block_free_insert(block, start - before, before);
					if (after)
						block_free_insert(block, after_offset, after);
				}
				return 1;
			}
		}
		block = calloc(1, sizeof(*block));
		if (!block)
			return 0;
		{
			struct allocation whole;

			if (!memory_dedicated(BLOCK_SIZE, (uint32_t)type, &whole))
			{
				free(block);
				return 0;
			}
			block->memory = whole.memory;
		}
		block->size = BLOCK_SIZE;
		block->type = (uint32_t)type;
		block_free_insert(block, 0, BLOCK_SIZE);
		block->next = vk.blocks;
		vk.blocks = block;
	}
}

static void memory_release(struct allocation *allocation)
{
	if (allocation->block)
		block_free_insert(allocation->block, allocation->offset, allocation->size);
	else if (allocation->memory)
		vkFreeMemory(vk.device, allocation->memory, NULL);
	memset(allocation, 0, sizeof(*allocation));
}

/* a buffer of its own memory, host-visible (mapped) or device-local */
/* (buffer_make's host: 0 the device's memory, BUFFER_HOST written by the
CPU and read by the GPU, BUFFER_READBACK written by the GPU and read by the
CPU) */
enum
{
	BUFFER_DEVICE,
	BUFFER_HOST,
	BUFFER_READBACK,
};

static int buffer_make(VkDeviceSize size, VkBufferUsageFlags usage, int host, VkBuffer *buffer,
	struct allocation *memory, void **mapped)
{
	VkBufferCreateInfo info;
	VkMemoryRequirements requirements;
	int type;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = usage;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (!check(vkCreateBuffer(vk.device, &info, NULL, buffer), "vkCreateBuffer"))
		return 0;
	vkGetBufferMemoryRequirements(vk.device, *buffer, &requirements);
	/* (memory the CPU reads from is the host's cached memory where the
	device has it: uncached, write-combined memory, which the CPU writes
	quickly, it reads a few hundred MB a second at most, and a 4K frame of
	the menus' pictures took 140 ms to read on an RTX 4090, the game waiting) */
	type = host == BUFFER_READBACK ? memory_type(requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
		0) : -1;
	if (type < 0)
		type = host ? memory_type(requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0) :
			memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	if (type < 0 || !memory_dedicated(requirements.size, (uint32_t)type, memory))
	{
		vkDestroyBuffer(vk.device, *buffer, NULL);
		*buffer = VK_NULL_HANDLE;
		return 0;
	}
	if (!check(vkBindBufferMemory(vk.device, *buffer, memory->memory, 0), "vkBindBufferMemory") ||
		(host && !check(vkMapMemory(vk.device, memory->memory, 0, VK_WHOLE_SIZE, 0, mapped), "vkMapMemory")))
	{
		vkDestroyBuffer(vk.device, *buffer, NULL);
		memory_release(memory);
		*buffer = VK_NULL_HANDLE;
		return 0;
	}
	return 1;
}

/* ---------- the timeline */

static uint64_t completed_serial(void)
{
	uint64_t value = vk.completed;

	if (vkGetSemaphoreCounterValue(vk.device, vk.timeline, &value) == VK_SUCCESS && value > vk.completed)
		vk.completed = value;
	return vk.completed;
}

static void garbage_collect(void);
static void ring_reclaim(void);

/* waits until the GPU has passed serial */
static void serial_wait(uint64_t serial)
{
	VkSemaphoreWaitInfo info;
	double started;

	if (serial <= completed_serial())
		return;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	info.semaphoreCount = 1;
	info.pSemaphores = &vk.timeline;
	info.pValues = &serial;
	started = host_load_trace_enabled() ? host_load_trace_clock(CLOCK_MONOTONIC) : -1.0;
	check(vkWaitSemaphores(vk.device, &info, UINT64_MAX), "vkWaitSemaphores");
	if (started >= 0.0)
	{
		double finished = host_load_trace_clock(CLOCK_MONOTONIC);

		if (finished >= started)
		{
			vk.statistics.waits++;
			vk.statistics.wait_seconds += finished - started;
		}
	}
	completed_serial();
	garbage_collect();
	ring_reclaim();
}

/* ---------- garbage */

static void garbage_add(const struct garbage *item)
{
	if (vk.garbage_count == vk.garbage_capacity)
	{
		uint32_t capacity = vk.garbage_capacity ? vk.garbage_capacity * 2 : 256;
		struct garbage *grown = realloc(vk.garbage, capacity * sizeof(*grown));

		if (!grown)
			return;
		vk.garbage = grown;
		vk.garbage_capacity = capacity;
	}
	vk.garbage[vk.garbage_count++] = *item;
}

static void garbage_free(struct garbage *item)
{
	switch (item->kind)
	{
	case _garbage_image:
		vkDestroyImage(vk.device, item->image, NULL);
		memory_release(&item->memory);
		break;
	case _garbage_buffer:
		vkDestroyBuffer(vk.device, item->buffer, NULL);
		memory_release(&item->memory);
		break;
	default:
		vkDestroyImageView(vk.device, item->view, NULL);
		break;
	}
}

static void garbage_collect(void)
{
	uint64_t done = vk.completed;
	uint32_t index = 0;

	while (index < vk.garbage_count)
	{
		if (vk.garbage[index].serial <= done)
		{
			garbage_free(&vk.garbage[index]);
			vk.garbage[index] = vk.garbage[--vk.garbage_count];
		}
		else
		{
			index++;
		}
	}
}

/* ---------- recording */

static void barrier(VkCommandBuffer commands, VkPipelineStageFlags source_stages, VkAccessFlags source_access,
	VkPipelineStageFlags destination_stages, VkAccessFlags destination_access)
{
	VkMemoryBarrier memory;

	memset(&memory, 0, sizeof(memory));
	memory.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	memory.srcAccessMask = source_access;
	memory.dstAccessMask = destination_access;
	vkCmdPipelineBarrier(commands, source_stages, destination_stages, 0, 1, &memory, 0, NULL, 0, NULL);
}

/* everything a draw or a copy after it can do with what was written before */
#define AFTER_STAGES (VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | \
	VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | \
	VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT)
#define AFTER_ACCESS (VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | \
	VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | \
	VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | \
	VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)

/* the current submission's slot, its command buffers begun: its earlier use
done first */
static void gpu_time_start(void);

static void pass_times_mark(VkCommandBuffer main, const char *label);
static const char *pass_times_label(const struct segment *segment, int aside);
static void timing_mark(VkCommandBuffer main, const struct segment *segment, int aside, int outside);

static void recording_begin(void)
{
	struct slot *slot;
	VkCommandBufferBeginInfo begin;

	if (vk.recording)
	{
		gpu_time_start();
		return;
	}
	slot = &vk.slots[vk.serial % SLOTS];
	if (slot->serial)
		serial_wait(slot->serial);
	vk.slot = slot;
	slot->serial = vk.serial;
	slot->ring_end = 0;
	vkResetCommandPool(vk.device, slot->pool, 0);
	/* (only the pools sets were allocated from: the others are empty) */
	if (slot->descriptor_pool_dirty)
	{
		uint32_t index;

		for (index = 0; index < slot->descriptor_pool_dirty; index++)
			vkResetDescriptorPool(vk.device, slot->descriptor_pools[index], 0);
	}
	slot->descriptor_pool_used = 0;
	slot->descriptor_pool_dirty = 0;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	check(vkBeginCommandBuffer(slot->main, &begin), "vkBeginCommandBuffer");
	/* A submission can have no early uploads. Its draws still need to
	wait for prior reads, notably the previous frame's presentation blit
	from the same color target. The timeline tracks completion for the
	CPU; signaling it alone does not order the next submission's work. */
	barrier(slot->main, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
	vk.recording = 1;
	vk.early_recording = 0;
	slot->secondary_used = 0;
	vk.segment_count = 0;
	vk.segment_open = 0;
	vk.bound_pipeline = VK_NULL_HANDLE;
	memset(&vk.bound, 0, sizeof(vk.bound));
	vk.last_texture_valid = 0;
	/* (without discards the main command buffer is recorded as drawn: its
	timestamps written as it is, segments_play's otherwise) */
	vk.direct_outside = 0;
	if (!vk.deferred)
	{
		pass_times_mark(slot->main, NULL);
		timing_mark(slot->main, NULL, 0, 0);
	}
	gpu_time_start();
}

/* the early command buffer, begun, after everything submitted before */
static VkCommandBuffer early_commands(void)
{
	recording_begin();
	if (!vk.early_recording)
	{
		VkCommandBufferBeginInfo begin;

		memset(&begin, 0, sizeof(begin));
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		check(vkBeginCommandBuffer(vk.slot->early, &begin), "vkBeginCommandBuffer");
		barrier(vk.slot->early, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
		vk.early_recording = 1;
	}
	return vk.slot->early;
}

static void pass_end_from(const char *caller);
/* (each pass's ender named, for debug.pass_times) */
#define pass_end() pass_end_from(__func__)
static VkCommandBuffer main_commands_from(const char *caller);
#define main_commands() main_commands_from(__func__)

VkCommandBuffer xgpu_vulkan_early_commands(void)
{
	return early_commands();
}

uint64_t xgpu_vulkan_completed(void)
{
	return completed_serial();
}

/* ---------- HALO_VULKAN_DISCARDS: the main command buffer in segments

A pass's stores are chosen as its vkCmdBeginRendering is made, before its
draws, but what the later work of the frame does with its attachments is
known only after them. With discards on, the main command buffer's work is
recorded in segments, each into a secondary command buffer: the work
outside passes (copies, uploads, barriers, the present's blit), and each
pass's draws (begun with VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
and the pass's formats inherited). At the submission (segments_play) each
pass's vkCmdBeginRendering, vkCmdExecuteCommands and vkCmdEndRendering are
made in the main command buffer, in order with the rest, with stores chosen
from what came after it:

- an attachment that a later pass of the submission clears whole (each
  layer, all of the area) before anything else uses it is not stored
  (DONT_CARE): the clear writes over all of it;
- an attachment that anything else uses after the pass (a later pass that
  loads it or draws over part of it, a draw that samples it, a copy, a
  resolve outside the pass, a readback, the present) is stored, as before;
- the frame's last pass with a depth/stencil image or a multisampled one
  (whose samples are the frame's to draw) does not store what the next
  frame clears first: an image whose first use in each of the last
  DISCARD_FRAMES frames cleared that aspect whole. Any other first use
  starts the count again, and, if the frame before discarded it, is logged
  once (its pixels were undefined for that frame).

A multisampled image's resolve right after the pass that drew it is made
in that pass (resolveMode AVERAGE for colour, SAMPLE_ZERO for depth and
stencil: VK_KHR_depth_stencil_resolve, Vulkan 1.2's), so a tiler writes
the resolved pixels from its tile memory, not the samples, which are then
stored only where something later loads them; a resolve of samples not
drawn since they were last resolved into the same image, unchanged since,
is not made again (the stream resolves a target it bound and did not draw
into, which would read samples a frame's end discarded). Where the device has lazily
allocated memory, multisampled images are transient attachments in it
(attachments alone: their clear as they are made and their resolves outside
a pass are passes too), whose memory a tiler need never commit. A transient
image that is loaded is still correct: its lazily allocated memory is then
committed (Vulkan's lazily allocated memory keeps what is stored in it,
unlike Metal's memoryless textures).

Without discards (HALO_VULKAN_DISCARDS=0), passes are recorded into the main command
buffer as they are drawn, with every attachment stored. */

/* frames whose first use of an image cleared it whole before its end is
discarded (#131's) */
#define DISCARD_FRAMES 30

/* a secondary command buffer of the slot's pool begun: for a pass's draws
(the pass's formats inherited) or for work outside passes (NULL) */
static VkCommandBuffer secondary_begin(const VkCommandBufferInheritanceRenderingInfo *rendering)
{
	struct slot *slot = vk.slot;
	VkCommandBufferInheritanceInfo inheritance;
	VkCommandBufferBeginInfo begin;
	VkCommandBuffer commands;

	if (slot->secondary_used == slot->secondary_count)
	{
		VkCommandBufferAllocateInfo allocate;
		uint32_t count = slot->secondary_count ? slot->secondary_count : 16;
		VkCommandBuffer *grown = realloc(slot->secondaries, (slot->secondary_count + count) * sizeof(*grown));

		if (!grown)
			host_fatal("vulkan: no memory for secondary command buffers");
		slot->secondaries = grown;
		memset(&allocate, 0, sizeof(allocate));
		allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocate.commandPool = slot->pool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		allocate.commandBufferCount = count;
		if (!check(vkAllocateCommandBuffers(vk.device, &allocate, slot->secondaries + slot->secondary_count),
			"vkAllocateCommandBuffers (secondary)"))
			host_fatal("vulkan: no secondary command buffers");
		slot->secondary_count += count;
	}
	commands = slot->secondaries[slot->secondary_used++];
	memset(&inheritance, 0, sizeof(inheritance));
	inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
	inheritance.pNext = rendering;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
		(rendering ? VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT : 0);
	begin.pInheritanceInfo = &inheritance;
	check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer (secondary)");
	return commands;
}

/* the segment of work outside passes being recorded, ended */
static void segment_close(void)
{
	if (!vk.segment_open)
		return;
	check(vkEndCommandBuffer(vk.segments[vk.segment_count - 1].commands), "vkEndCommandBuffer (secondary)");
	vk.segment_open = 0;
}

/* a new segment of the submission (its index: segment_count - 1) */
static struct segment *segment_add(void)
{
	struct segment *segment;

	segment_close();
	if (vk.segment_count == vk.segment_capacity)
	{
		uint32_t capacity = vk.segment_capacity ? vk.segment_capacity * 2 : 64;
		struct segment *grown = realloc(vk.segments, capacity * sizeof(*grown));

		if (!grown)
			host_fatal("vulkan: no memory for the passes' segments");
		vk.segments = grown;
		vk.segment_capacity = capacity;
	}
	segment = &vk.segments[vk.segment_count++];
	memset(segment, 0, sizeof(*segment));
	return segment;
}

/* the depth/stencil images and the multisampled ones: what the frame's
last pass may discard */
static int image_discardable(const struct xgpu_vulkan_image *image)
{
	return (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || image->description.samples > 1;
}

/* the first use in a frame of an image whose end-of-frame contents may be
discarded: whether it cleared each aspect (bits 1 colour or depth, 2
stencil) whole */
static void image_first_use(struct xgpu_vulkan_image *image, int cleared, const char *use)
{
	int aspect;

	if (image->first_use_frame == vk.frame + 1 || !image_discardable(image))
		return;
	image->first_use_frame = vk.frame + 1;
	for (aspect = 0; aspect < 2; aspect++)
	{
		if (cleared & (1 << aspect))
		{
			if (image->clears[aspect] < 255)
				image->clears[aspect]++;
		}
		else
		{
			if (image->discarded[aspect] && !vk.discard_logged)
			{
				host_logf(HOST_LOG_WARN, "vulkan: a %ux%u %s's contents were discarded at a frame's end and then "
					"read (frame %llu, by %s): undefined for a frame (logged once)", image->description.width,
					image->description.height, image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? (aspect ? "stencil" : "depth") :
					"multisampled target", (unsigned long long)vk.frame, use);
				vk.discard_logged = 1;
			}
			image->clears[aspect] = 0;
		}
		image->discarded[aspect] = 0;
	}
}

static struct pass_attachment *segment_attachment(uint32_t segment, int slot)
{
	return slot ? &vk.segments[segment].depth : &vk.segments[segment].color;
}

static void aside_image_read(struct xgpu_vulkan_image *image);
static void aside_image_written(struct xgpu_vulkan_image *image);

/* An image used other than as a pass's attachment (sampled, copied,
resolved outside a pass, read back, presented, uploaded into): the last
pass that drew into it in this submission stores it. */
static void image_use(struct xgpu_vulkan_image *image)
{
	if (!vk.deferred || !image)
		return;
	aside_image_read(image);
	image_first_use(image, 0, "a use outside a pass");
	if (image->pass_serial == vk.serial && image->pass_segment)
	{
		struct pass_attachment *before = segment_attachment(image->pass_segment - 1, image->pass_slot);

		before->keep[0] = before->keep[1] = 1;
	}
	image->use_serial = vk.serial;
	image->use_mark = vk.segment_count;
}

/* with discards: an image written outside a pass (a copy's, an upload's
destination) */
static void image_written(struct xgpu_vulkan_image *image)
{
	if (vk.deferred)
		image->writes++;
	aside_image_written(image);
}

/* An image an attachment of the pass of this segment (slot 0 colour, 1
depth and stencil), with the aspects (bits 1 colour or depth, 2 stencil)
it has and those it clears whole as the pass begins: the pass before with
the image stores what this one loads, and need not store what this one
clears. */
static void attachment_use(uint32_t segment, int slot, struct xgpu_vulkan_image *image, int aspects, int cleared)
{
	int aspect;

	image_first_use(image, cleared, "a pass that loads it");
	image->writes++;
	if (image->pass_serial == vk.serial && image->pass_segment)
	{
		struct pass_attachment *before = segment_attachment(image->pass_segment - 1, image->pass_slot);

		for (aspect = 0; aspect < 2; aspect++)
		{
			if (!(aspects & (1 << aspect)))
				continue;
			if (cleared & (1 << aspect))
				before->killed[aspect] = 1;
			else
				before->keep[aspect] = 1;
		}
	}
	image->pass_serial = vk.serial;
	image->pass_segment = segment + 1;
	image->pass_slot = (uint8_t)slot;
}

/* the store of an aspect of a pass's attachment (segments_play) */
static VkAttachmentStoreOp store_choose(uint32_t segment, struct pass_attachment *attachment, int aspect)
{
	struct xgpu_vulkan_image *image = attachment->image;

	if (attachment->keep[aspect] || attachment->info.storeOp != VK_ATTACHMENT_STORE_OP_STORE)
		return attachment->info.storeOp;
	if (attachment->killed[aspect])
	{
		vk.statistics.stores_discarded++;
		return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	/* (the frame's last pass with it) */
	if (image && vk.frame_ending && image->pass_serial == vk.serial && image->pass_segment == segment + 1 &&
		image_discardable(image) && image->clears[aspect] >= DISCARD_FRAMES)
	{
		image->discarded[aspect] = 1;
		/* (its next use checked as a first use, even in this frame) */
		image->first_use_frame = 0;
		vk.statistics.stores_discarded++;
		vk.statistics.frame_end_discards++;
		return VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	return attachment->info.storeOp;
}

/* ---------- aside passes

The game draws into other targets in the middle of its 3D view (the
shadows, the motion sensor, the effects' targets), each in a pass of its
own, which ends the view's pass; the view's next pass then loads what the
one before stored: on a tiler, the whole view written out of tile memory
and read back each time (#335). With aside passes (on with discards;
HALO_VULKAN_ASIDE=0: none), as the Metal backend puts passes aside
(port/macos/xgpu_metal.m):

- a pass that draws colour alone, not multisampled, into other targets
  than the pass recorded before it, and the copies and image uploads
  about it, are put aside: played before that pass, the chain's first;
- the chain's next pass of the same targets, area, layer and views, which
  loads every attachment and stores as the one before it, continues it:
  its pieces are played as one rendering, each piece's secondary command
  buffer executed in it, begun with the first's loads and ended with the
  last's stores and resolves.

What is put aside then comes before pieces drawn before it, which keeps
the frame's pixels where neither reads what the other writes: what is put
aside may not read the chain's targets (sample, copy, resolve them) or
write an image a piece sampled (aside_read). That, a pass with depth or
multisampled, or any other work outside passes (a buffer written in order,
a readback, the lights' upload, the present) ends the chain: from there
everything is played in the order drawn, and the next pass may begin
another chain. A piece resolved in its pass is the chain's last. A buffer the
submission reads that is written again is renamed (buffer_rename), not
written in order, so that it ends no chain. */

/* the chain ended: no more of its pieces, nothing more put aside */
static void aside_break(void)
{
	vk.chain_head = vk.chain_last = 0;
	vk.chain_color = vk.chain_depth = NULL;
}

/* the pass being recorded is the chain's last piece */
static int aside_in_piece(void)
{
	return vk.chain_head && vk.pass_active && vk.chain_last == vk.segment_count;
}

/* an image read (sampled, copied from or into, resolved, read back): by a
piece, marked; by anything else, not one of the chain's targets */
static void aside_image_read(struct xgpu_vulkan_image *image)
{
	if (!vk.chain_head || !image)
		return;
	if (aside_in_piece())
		image->aside_read = vk.chain_number;
	else if (image == vk.chain_color || image == vk.chain_depth)
		aside_break();
}

/* an image written outside a pass (a copy's, an upload's, a resolve's): not
one of the chain's targets, nor one a piece sampled */
static void aside_image_written(struct xgpu_vulkan_image *image)
{
	if (vk.chain_head && image && (image == vk.chain_color || image == vk.chain_depth ||
		image->aside_read == vk.chain_number))
		aside_break();
}

/* whether the pass of this segment continues the chain's last piece: its
targets, area, layer and views, each attachment loaded and stored as the
piece's, and the piece resolves nothing */
static int aside_continues(const struct segment *segment)
{
	const struct segment *last = &vk.segments[vk.chain_last - 1];

	if (segment->color.image != vk.chain_color || segment->depth.image != vk.chain_depth ||
		segment->color.info.imageView != last->color.info.imageView ||
		segment->depth.info.imageView != last->depth.info.imageView ||
		memcmp(&segment->area, &last->area, sizeof(segment->area)) || segment->view_mask != last->view_mask ||
		segment->layer != last->layer || segment->views != last->views ||
		last->color.resolve_view || last->depth.resolve_view)
		return 0;
	if (segment->color.info.imageView && (segment->color.info.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD ||
		segment->color.info.storeOp != last->color.info.storeOp))
		return 0;
	if (segment->depth.info.imageView && (segment->depth.info.loadOp != VK_ATTACHMENT_LOAD_OP_LOAD ||
		segment->depth.stencil_load != VK_ATTACHMENT_LOAD_OP_LOAD ||
		segment->depth.info.storeOp != last->depth.info.storeOp))
		return 0;
	return 1;
}

/* the pass of this segment (its attachments known) begun: a piece of the
chain, put aside, or another chain's first */
static void aside_pass(uint32_t index, struct segment *segment)
{
	if (!vk.aside)
		return;
	if (vk.chain_head)
	{
		if (aside_continues(segment))
		{
			segment->piece = 1;
			vk.chain_last = index + 1;
			vk.segments[vk.chain_head - 1].chain_last = vk.chain_last;
			vk.statistics.passes_continued++;
			return;
		}
		if (segment->color.image && !segment->depth.image && segment->color.image->description.samples <= 1 &&
			segment->color.image != vk.chain_color && segment->color.image->aside_read != vk.chain_number)
			return;
	}
	vk.chain_number++;
	vk.chain_head = vk.chain_last = index + 1;
	vk.chain_color = segment->color.image;
	vk.chain_depth = segment->depth.image;
	segment->chain_last = index + 1;
}

/* the barrier after a pass: what it wrote (and resolved) before what comes
after it */
static void pass_barrier(VkCommandBuffer commands)
{
	barrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, AFTER_STAGES, AFTER_ACCESS);
}

/* a pass played: the first piece's to the last's (the same segment where
it has no other), begun with the first's loads, ended with the last's stores
and resolves, the pieces between executed in it */
static void pass_play(VkCommandBuffer main, uint32_t first, uint32_t last)
{
	struct segment *segment = &vk.segments[first], *end = &vk.segments[last];
	VkRenderingAttachmentInfo color, depth, stencil;
	VkRenderingInfo rendering;
	uint32_t index;

	memset(&rendering, 0, sizeof(rendering));
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
	rendering.renderArea = segment->area;
	rendering.layerCount = 1;
	rendering.viewMask = segment->view_mask;
	if (segment->color.info.imageView)
	{
		color = segment->color.info;
		color.storeOp = store_choose(last, &end->color, 0);
		if (end->color.resolve_view)
		{
			color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			color.resolveImageView = end->color.resolve_view;
			color.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &color;
	}
	if (segment->depth.info.imageView)
	{
		depth = segment->depth.info;
		stencil = segment->depth.info;
		depth.storeOp = store_choose(last, &end->depth, 0);
		stencil.storeOp = store_choose(last, &end->depth, 1);
		stencil.loadOp = segment->depth.stencil_load;
		if (end->depth.resolve_view)
		{
			depth.resolveMode = stencil.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			depth.resolveImageView = stencil.resolveImageView = end->depth.resolve_view;
			depth.resolveImageLayout = stencil.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}
		rendering.pDepthAttachment = &depth;
		rendering.pStencilAttachment = &stencil;
	}
	vkCmdBeginRendering(main, &rendering);
	for (index = first; index <= last; index++)
	{
		if (index == first || vk.segments[index].piece)
			vkCmdExecuteCommands(main, 1, &vk.segments[index].commands);
	}
	vkCmdEndRendering(main);
	pass_barrier(main);
}

/* the submission's segments into the main command buffer, in order but for
what was put aside (before the chain it was put aside from, whose pieces
are then one pass): each pass begun with the stores chosen now */
static void segments_play(void)
{
	VkCommandBuffer main = vk.slot->main;
	uint32_t index, between;

	segment_close();
	pass_times_mark(main, NULL);
	timing_mark(main, NULL, 0, 0);
	for (index = 0; index < vk.segment_count; index++)
	{
		struct segment *segment = &vk.segments[index];

		if (!segment->pass)
		{
			vkCmdExecuteCommands(main, 1, &segment->commands);
			pass_times_mark(main, "outside passes");
			timing_mark(main, NULL, 0, 1);
			continue;
		}
		if (segment->chain_last > index + 1)
		{
			uint32_t last = segment->chain_last - 1;

			for (between = index + 1; between < last; between++)
			{
				struct segment *aside = &vk.segments[between];

				if (aside->piece)
					continue;
				if (aside->pass)
				{
					pass_play(main, between, between);
					vk.statistics.passes_aside++;
					pass_times_mark(main, pass_times_label(aside, 1));
					timing_mark(main, aside, 1, 0);
				}
				else
				{
					vkCmdExecuteCommands(main, 1, &aside->commands);
					pass_times_mark(main, "outside passes");
					timing_mark(main, NULL, 0, 1);
				}
			}
			pass_play(main, index, last);
			pass_times_mark(main, pass_times_label(segment, 0));
			timing_mark(main, segment, 0, 0);
			index = last;
			continue;
		}
		pass_play(main, index, index);
		pass_times_mark(main, pass_times_label(segment, 0));
		timing_mark(main, segment, 0, 0);
	}
	vk.segment_count = 0;
	aside_break();
}

/* the commands of work outside passes: the main command buffer's, or with
discards the segment of them being recorded */
static VkCommandBuffer outside_commands(void)
{
	struct segment *segment;

	if (!vk.deferred)
	{
		vk.direct_outside = 1;
		return vk.slot->main;
	}
	if (vk.segment_open)
		return vk.segments[vk.segment_count - 1].commands;
	segment = segment_add();
	segment->commands = secondary_begin(NULL);
	vk.segment_open = 1;
	return segment->commands;
}

/* without discards: the work outside passes recorded since the last
timestamp, timed (before a pass, and as the submission is made) */
static void direct_outside_mark(void)
{
	if (vk.deferred || !vk.direct_outside)
		return;
	vk.direct_outside = 0;
	pass_times_mark(vk.slot->main, "outside passes");
	timing_mark(vk.slot->main, NULL, 0, 1);
}

/* the main command buffer, outside a pass */
static VkCommandBuffer main_commands_from(const char *caller)
{
	recording_begin();
	pass_end_from(caller);
	/* (put aside only where it is a copy or an image's upload, whose images
	are checked: other work ends the chain) */
	if (!vk.aside_allowed)
		aside_break();
	vk.aside_allowed = 0;
	return outside_commands();
}

/* A timestamp interval begins in the early buffer, before the frame's
uploads, and ends after the main buffer's rendering and composition. The
pair belongs to its final submission until that submission completes. */
void xgpu_vulkan_gpu_time_begin(void)
{
	if (vk.capabilities.gpu_timestamps && !vk.gpu_time_failed && !vk.gpu_time_active)
		vk.gpu_time_armed = 1;
}

static void gpu_time_start(void)
{
	uint32_t index;
	VkCommandBuffer commands;

	if (!vk.gpu_time_armed)
		return;
	/* Consume before early_commands reenters recording_begin. No query is
	issued during the runtime's frame wait, before actual render work. */
	vk.gpu_time_armed = 0;
	if (!vk.gpu_time_pool)
	{
		VkQueryPoolCreateInfo create;

		memset(&create, 0, sizeof(create));
		create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		create.queryType = VK_QUERY_TYPE_TIMESTAMP;
		create.queryCount = GPU_TIME_FRAMES * 2;
		if (!check(vkCreateQueryPool(vk.device, &create, NULL, &vk.gpu_time_pool), "vkCreateQueryPool (GPU time)"))
		{
			vk.gpu_time_failed = 1;
			return;
		}
	}
	for (index = 0; index < GPU_TIME_FRAMES && vk.gpu_times[index].used; index++)
		;
	if (index == GPU_TIME_FRAMES)
		return;
	commands = early_commands();
	vkResetQueryPool(vk.device, vk.gpu_time_pool, index * 2, 2);
	vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, vk.gpu_time_pool, index * 2);
	vk.gpu_times[index].used = 1;
	vk.gpu_times[index].serial = 0;
	vk.gpu_time_current = index;
	vk.gpu_time_active = 1;
}

void xgpu_vulkan_gpu_time_end(void)
{
	VkCommandBuffer commands;
	uint32_t index;

	vk.gpu_time_armed = 0;
	if (!vk.gpu_time_active)
		return;
	commands = main_commands();
	index = vk.gpu_time_current;
	/* Outside the multiview rendering scope: one query, not one per view. */
	vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, vk.gpu_time_pool, index * 2 + 1);
	vk.gpu_times[index].serial = vk.serial;
	vk.gpu_time_active = 0;
}

int xgpu_vulkan_gpu_time_read(double *milliseconds)
{
	uint64_t done, oldest = UINT64_MAX, results[2][2], mask, elapsed;
	uint32_t index, found = GPU_TIME_FRAMES;
	VkResult result;

	if (!milliseconds || !vk.gpu_time_pool)
		return 0;
	done = completed_serial();
	for (index = 0; index < GPU_TIME_FRAMES; index++)
	{
		uint64_t serial = vk.gpu_times[index].serial;

		if (vk.gpu_times[index].used && serial && serial <= done && serial < oldest)
		{
			oldest = serial;
			found = index;
		}
	}
	if (found == GPU_TIME_FRAMES)
		return 0;
	result = vkGetQueryPoolResults(vk.device, vk.gpu_time_pool, found * 2, 2, sizeof(results), results,
		sizeof(results[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	if (result == VK_NOT_READY)
		return 0;
	if (!check(result, "vkGetQueryPoolResults (GPU time)"))
	{
		vk.gpu_times[found].used = 0;
		return 0;
	}
	if (!results[0][1] || !results[1][1])
		return 0;
	mask = vk.timestamp_bits >= 64 ? UINT64_MAX : (UINT64_C(1) << vk.timestamp_bits) - 1;
	elapsed = (results[1][0] - results[0][0]) & mask;
	*milliseconds = (double)elapsed * (double)vk.properties.limits.timestampPeriod * 1e-6;
	vk.gpu_times[found].used = 0;
	return 1;
}

/* ---------- HALO_VULKAN_DRAW_TIMES (measuring)

Every Nth frame (the setting's number), a timestamp at the bottom of the
pipe after each draw: sorted by time, each gap is the draw's that ended it
(its kind, xgpu_vulkan_draw's kind), and from the last draw to the frame's
end the frame's other work's. Summed by the draws' display.ambient_occlusion
"world" kind and logged every 20 timed frames; with HALO_VULKAN_DRAW_TIMES_FILE,
each timed frame's draws written there one a line, to compare two runs'
draws one for one. A timestamp after every draw serializes them, so the
times are each draw's own, not its share of a frame's overlapped work. */

#define DRAW_TIMES_MAXIMUM 16384

static struct
{
	int every, pending;
	VkQueryPool pool;
	FILE *file;
	uint32_t used;
	uint64_t serial, frame;
	struct
	{
		uint8_t kind[4];
		uint32_t count;
	} draws[DRAW_TIMES_MAXIMUM];
	double kind_milliseconds[256], kind_draws[256], other_milliseconds, frames;
} draw_times;

static void draw_times_create(void)
{
	const char *every = getenv("HALO_VULKAN_DRAW_TIMES"), *file = getenv("HALO_VULKAN_DRAW_TIMES_FILE");
	VkQueryPoolCreateInfo create;

	draw_times.every = every ? atoi(every) : 0;
	if (draw_times.every <= 0 || !vk.capabilities.gpu_timestamps)
	{
		draw_times.every = 0;
		return;
	}
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	create.queryType = VK_QUERY_TYPE_TIMESTAMP;
	create.queryCount = DRAW_TIMES_MAXIMUM;
	if (!check(vkCreateQueryPool(vk.device, &create, NULL, &draw_times.pool), "vkCreateQueryPool (draw times)"))
	{
		draw_times.every = 0;
		return;
	}
	if (file && *file)
		draw_times.file = fopen(file, "w");
}

/* after a draw recorded into commands: its timestamp (the frame's first
draw: a timestamp before it, too) */
static void draw_times_draw(VkCommandBuffer commands, const struct xgpu_vulkan_draw *draw, int after)
{
	if (!draw_times.every || draw_times.pending)
		return;
	if (!after)
	{
		if (draw_times.used || vk.frame % (uint64_t)draw_times.every)
			return;
		vkResetQueryPool(vk.device, draw_times.pool, 0, DRAW_TIMES_MAXIMUM);
		vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, draw_times.pool, 0);
		memset(&draw_times.draws[0], 0, sizeof(draw_times.draws[0]));
		draw_times.used = 1;
		draw_times.frame = vk.frame;
		return;
	}
	if (!draw_times.used || draw_times.frame != vk.frame || draw_times.used >= DRAW_TIMES_MAXIMUM - 1)
		return;
	vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, draw_times.pool, draw_times.used);
	memcpy(draw_times.draws[draw_times.used].kind, draw->kind, 4);
	draw_times.draws[draw_times.used].count = draw->count;
	draw_times.used++;
}

/* the timed frame's end, in its last submission's commands */
static void draw_times_frame_end(void)
{
	if (!draw_times.used || draw_times.pending || draw_times.frame != vk.frame)
		return;
	vkCmdWriteTimestamp(main_commands(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, draw_times.pool, draw_times.used);
	memset(&draw_times.draws[draw_times.used], 0, sizeof(draw_times.draws[0]));
	draw_times.used++;
	draw_times.pending = 1;
}

static int draw_times_order(const void *a, const void *b)
{
	const uint64_t *x = a, *y = b;

	return x[0] < y[0] ? -1 : x[0] > y[0];
}

/* the timed frame's times, once its submission is done */
static void draw_times_collect(void)
{
	static uint64_t times[DRAW_TIMES_MAXIMUM][2], sorted[DRAW_TIMES_MAXIMUM][2];
	uint32_t index, last;
	VkResult result;

	if (!draw_times.pending || !draw_times.serial || draw_times.serial > completed_serial())
		return;
	result = vkGetQueryPoolResults(vk.device, draw_times.pool, 0, draw_times.used, sizeof(times[0]) * draw_times.used,
		times, sizeof(times[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	if (result == VK_NOT_READY)
		return;
	last = draw_times.used - 1;
	for (index = 0; index < draw_times.used; index++)
	{
		sorted[index][0] = times[index][0];
		sorted[index][1] = index;
	}
	qsort(sorted, draw_times.used, sizeof(sorted[0]), draw_times_order);
	if (draw_times.file)
		fprintf(draw_times.file, "frame %llu draws %u\n", (unsigned long long)draw_times.frame, draw_times.used - 2);
	for (index = 1; result == VK_SUCCESS && index < draw_times.used; index++)
	{
		uint32_t which = (uint32_t)sorted[index][1];
		double milliseconds = (double)(sorted[index][0] - sorted[index - 1][0]) *
			(double)vk.properties.limits.timestampPeriod * 1e-6;

		if (which == last)
		{
			draw_times.other_milliseconds += milliseconds;
			continue;
		}
		draw_times.kind_milliseconds[draw_times.draws[which].kind[0]] += milliseconds;
		draw_times.kind_draws[draw_times.draws[which].kind[0]]++;
		if (draw_times.file)
			fprintf(draw_times.file, "%u %.4f %02x %02x %02x %02x %u\n", which, milliseconds * 1000.0,
				draw_times.draws[which].kind[0], draw_times.draws[which].kind[1], draw_times.draws[which].kind[2],
				draw_times.draws[which].kind[3], draw_times.draws[which].count);
	}
	draw_times.frames++;
	draw_times.used = 0;
	draw_times.pending = 0;
	draw_times.serial = 0;
	if (draw_times.frames >= 20)
	{
		char line[2048];
		int length = 0;

		for (index = 0; index < 256; index++)
		{
			if (draw_times.kind_draws[index] && length < (int)sizeof(line) - 64)
				length += snprintf(line + length, sizeof(line) - (size_t)length, " ao%02x %.3f ms (%.0f)", index,
					draw_times.kind_milliseconds[index] / draw_times.frames,
					draw_times.kind_draws[index] / draw_times.frames);
		}
		host_logf(HOST_LOG_INFO, "vulkan: draw times a frame:%s; other %.3f ms", line,
			draw_times.other_milliseconds / draw_times.frames);
		if (draw_times.file)
			fflush(draw_times.file);
		memset(draw_times.kind_milliseconds, 0, sizeof(draw_times.kind_milliseconds));
		memset(draw_times.kind_draws, 0, sizeof(draw_times.kind_draws));
		draw_times.other_milliseconds = draw_times.frames = 0.0;
	}
}

/* ---------- each pass's GPU time (debug.pass_times)

Every host_pass_times frames, a timestamp at the start of each of the
frame's submissions and after each pass and each piece of work outside
passes, played into the main command buffer (segments_play); once the
frame is done, the time between each and the one before is its pass's,
summed by what the pass draws into (its size, samples and attachments).
On a tiler a pass's time is the time it took from its first draw to its
stores, the cost to find. Every 30 timed frames the sums are logged as
milliseconds a frame and shares of the timed frames' GPU time: the shares
hold whatever clock the GPU ran at, which a phone's governor changes. */

#define PASS_TIMES_MAXIMUM 512
#define PASS_TIMES_LABELS 48
#define PASS_TIMES_LABEL 96

static struct
{
	VkQueryPool pool;
	int pending;
	uint32_t used;
	uint64_t serial, frame;
	/* each timestamp's: 0 a submission's start, else the work before it */
	char labels[PASS_TIMES_MAXIMUM][PASS_TIMES_LABEL];
	struct
	{
		char label[PASS_TIMES_LABEL];
		double milliseconds, count;
	} sums[PASS_TIMES_LABELS];
	uint32_t sum_count;
	double frames;
} pass_times;

static void pass_times_create(void)
{
	VkQueryPoolCreateInfo create;

	if (host_pass_times <= 0 || !vk.capabilities.gpu_timestamps)
	{
		host_pass_times = 0;
		return;
	}
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	create.queryType = VK_QUERY_TYPE_TIMESTAMP;
	create.queryCount = PASS_TIMES_MAXIMUM;
	if (!check(vkCreateQueryPool(vk.device, &create, NULL, &pass_times.pool), "vkCreateQueryPool (pass times)"))
		host_pass_times = 0;
	else
		host_logf(HOST_LOG_INFO, "vulkan: each pass timed every %d frames (debug.pass_times)", host_pass_times);
}

/* a timestamp in the main command buffer, after the work label names (NULL:
a submission's start, which begins the frame's timing on a timed frame) */
static void pass_times_mark(VkCommandBuffer main, const char *label)
{
	if (!host_pass_times || pass_times.pending)
		return;
	if (!label && !pass_times.used)
	{
		if (vk.frame % (uint64_t)host_pass_times)
			return;
		vkResetQueryPool(vk.device, pass_times.pool, 0, PASS_TIMES_MAXIMUM);
		pass_times.frame = vk.frame;
	}
	if (!pass_times.used && label)
		return;
	if (pass_times.frame != vk.frame || pass_times.used >= PASS_TIMES_MAXIMUM)
		return;
	vkCmdWriteTimestamp(main, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pass_times.pool, pass_times.used);
	snprintf(pass_times.labels[pass_times.used], PASS_TIMES_LABEL, "%s", label ? label : "");
	pass_times.used++;
}

/* a pass's label: what it draws into */
static const char *pass_times_label(const struct segment *segment, int aside)
{
	static char label[PASS_TIMES_LABEL];
	const struct xgpu_vulkan_image *image = segment->color.image ? segment->color.image : segment->depth.image;

	snprintf(label, sizeof(label), "%s%ux%u%s%s%s, ended by %s", aside ? "aside " : "", segment->area.extent.width,
		segment->area.extent.height, segment->color.image ? " colour" : "", segment->depth.image ? " depth" : "",
		image && image->description.samples > 1 ? " msaa" : "", segment->ended_by ? segment->ended_by : "?");
	return label;
}

static void pass_times_frame_end(void)
{
	if (pass_times.used && !pass_times.pending && pass_times.frame == vk.frame)
		pass_times.pending = 1;
}

static void pass_times_collect(void)
{
	static uint64_t times[PASS_TIMES_MAXIMUM][2];
	uint64_t mask = vk.timestamp_bits >= 64 ? UINT64_MAX : (UINT64_C(1) << vk.timestamp_bits) - 1;
	uint32_t index, sum;
	VkResult result;

	if (!pass_times.pending || !pass_times.serial || pass_times.serial > completed_serial())
		return;
	result = vkGetQueryPoolResults(vk.device, pass_times.pool, 0, pass_times.used, sizeof(times[0]) * pass_times.used,
		times, sizeof(times[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
	if (result == VK_NOT_READY)
		return;
	for (index = 1; result == VK_SUCCESS && index < pass_times.used; index++)
	{
		double milliseconds;

		if (!pass_times.labels[index][0] || !times[index][1] || !times[index - 1][1])
			continue;
		milliseconds = (double)((times[index][0] - times[index - 1][0]) & mask) *
			(double)vk.properties.limits.timestampPeriod * 1e-6;
		for (sum = 0; sum < pass_times.sum_count; sum++)
		{
			if (!strcmp(pass_times.sums[sum].label, pass_times.labels[index]))
				break;
		}
		if (sum == pass_times.sum_count)
		{
			if (sum == PASS_TIMES_LABELS)
				continue;
			snprintf(pass_times.sums[sum].label, PASS_TIMES_LABEL, "%s", pass_times.labels[index]);
			pass_times.sums[sum].milliseconds = pass_times.sums[sum].count = 0.0;
			pass_times.sum_count++;
		}
		pass_times.sums[sum].milliseconds += milliseconds;
		pass_times.sums[sum].count++;
	}
	pass_times.frames++;
	pass_times.used = 0;
	pass_times.pending = 0;
	pass_times.serial = 0;
	if (pass_times.frames >= 30)
	{
		char line[2048];
		int length = 0;
		double total = 0.0;

		for (sum = 0; sum < pass_times.sum_count; sum++)
			total += pass_times.sums[sum].milliseconds;
		/* (the largest first) */
		for (index = 0; index < pass_times.sum_count && length < (int)sizeof(line) - 96; index++)
		{
			uint32_t largest = index;

			for (sum = index + 1; sum < pass_times.sum_count; sum++)
			{
				if (pass_times.sums[sum].milliseconds > pass_times.sums[largest].milliseconds)
					largest = sum;
			}
			if (largest != index)
			{
				char swap[sizeof(pass_times.sums[0])];

				memcpy(swap, &pass_times.sums[index], sizeof(swap));
				memcpy(&pass_times.sums[index], &pass_times.sums[largest], sizeof(swap));
				memcpy(&pass_times.sums[largest], swap, sizeof(swap));
			}
			length += snprintf(line + length, sizeof(line) - (size_t)length, "; %s %.3f ms (%.0f%%, %.1f a frame)",
				pass_times.sums[index].label, pass_times.sums[index].milliseconds / pass_times.frames,
				total > 0.0 ? 100.0 * pass_times.sums[index].milliseconds / total : 0.0,
				pass_times.sums[index].count / pass_times.frames);
		}
		host_logf(HOST_LOG_INFO, "vulkan: pass times, %.3f ms a frame of %.0f frames%s", total / pass_times.frames,
			pass_times.frames, line);
		pass_times.sum_count = 0;
		pass_times.frames = 0.0;
	}
}

/* ---------- a frame's GPU time for a caller (xgpu_vulkan_timing_*)

The replay's cost (port/tools/replay/host_render_replay.h): the frame
begun after a request is timed with timestamps at the bottom of the
pipeline before and after each of its draws (in the segments' commands)
and, as segments_play plays them, at each submission's start and after
each pass and each piece of work outside passes (in the main command
buffer). A pass's time is from the timestamp before it to its own, so that
the passes' times sum to the frame's (the waits between its submissions
left out); a draw's from its timestamp before to its timestamp after, and
it belongs to the pass whose span holds its end. A timestamp at the bottom
of the pipeline waits for the work before it: around each draw, the draws
that would overlap on an immediate GPU are drawn one after the other, so
a draw's time is its own, without the overlap, and the frame drawn so is
slower than it is untimed (its passes' times are those of the frame drawn
so); a tiler's draws are binned and shaded at the pass's end, so their
timestamps say little. HALO_VULKAN_TIMING_DRAWS=0 times the passes alone
(the frame drawn as untimed; the replay's subtraction needs no more). */

#define TIMING_QUERIES 16384
/* (room kept for the passes' timestamps) */
#define TIMING_PASS_ROOM 1024
#define TIMING_LABEL 64

enum { TIMING_START, TIMING_PASS, TIMING_DRAW_BEFORE, TIMING_DRAW_AFTER };

static struct
{
	VkQueryPool pool;
	/* the frame being timed (render thread), its timestamps written to
	used; the draw being handed (the caller's number), done: the frame
	ended, its timestamps all written to (used) */
	int active, done, draws_off;
	uint32_t used, draw;
	struct
	{
		uint8_t what;
		/* a draw's number; a pass's place in passes */
		uint32_t index;
	} marks[TIMING_QUERIES];
	struct
	{
		char label[TIMING_LABEL];
		uint32_t width, height;
	} passes[TIMING_PASS_ROOM];
	uint32_t pass_count;
	/* the results */
	struct xgpu_vulkan_pass_time results[TIMING_PASS_ROOM];
	uint32_t *draw_pass;
	double *draw_ms;
	uint32_t draw_count, draw_capacity;
	char note[512];
} timing;

static int timing_write(VkCommandBuffer commands, uint8_t what, uint32_t index);

int xgpu_vulkan_timing_begin(void)
{
	VkQueryPoolCreateInfo create;

	if (!vk.capabilities.gpu_timestamps)
		return 0;
	if (!timing.pool)
	{
		memset(&create, 0, sizeof(create));
		create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		create.queryType = VK_QUERY_TYPE_TIMESTAMP;
		create.queryCount = TIMING_QUERIES;
		if (!check(vkCreateQueryPool(vk.device, &create, NULL, &timing.pool), "vkCreateQueryPool (timing)"))
		{
			timing.pool = VK_NULL_HANDLE;
			return 0;
		}
	}
	vkResetQueryPool(vk.device, timing.pool, 0, TIMING_QUERIES);
	timing.used = 0;
	timing.pass_count = 0;
	timing.draw = UINT32_MAX;
	timing.done = 0;
	timing.draws_off = getenv("HALO_VULKAN_TIMING_DRAWS") && !atoi(getenv("HALO_VULKAN_TIMING_DRAWS"));
	timing.active = 1;
	/* (without discards a submission's timestamps are written as it is
	recorded: one being recorded starts its timing now) */
	if (vk.recording && !vk.deferred)
		timing_write(vk.slot->main, TIMING_START, 0);
	return 1;
}

void xgpu_vulkan_timing_draw(uint32_t index)
{
	timing.draw = index;
}

/* a timestamp of the timed frame's into commands (0 if there is no room) */
static int timing_write(VkCommandBuffer commands, uint8_t what, uint32_t index)
{
	if (!timing.active || timing.used >= TIMING_QUERIES ||
		((what == TIMING_DRAW_BEFORE || what == TIMING_DRAW_AFTER) && timing.used >= TIMING_QUERIES - TIMING_PASS_ROOM))
		return 0;
	vkCmdWriteTimestamp(commands, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, timing.pool, timing.used);
	timing.marks[timing.used].what = what;
	timing.marks[timing.used].index = index;
	timing.used++;
	return 1;
}

/* (about a draw recorded into commands) */
static void timing_draw_mark(VkCommandBuffer commands, int after)
{
	if (!timing.active || timing.draw == UINT32_MAX || timing.draws_off)
		return;
	/* (a pair, or none: the one after is written where the one before was) */
	if (!after)
		timing_write(commands, TIMING_DRAW_BEFORE, timing.draw);
	else if (timing.used && timing.marks[timing.used - 1].what == TIMING_DRAW_BEFORE)
		timing_write(commands, TIMING_DRAW_AFTER, timing.draw);
}

/* (segments_play) a submission's start (segment NULL, outside 0), or the
end of a pass (segment) or of work outside passes (outside) */
static void timing_mark(VkCommandBuffer main, const struct segment *segment, int aside, int outside)
{
	const struct xgpu_vulkan_image *image;

	if (!timing.active)
		return;
	if (!segment && !outside)
	{
		timing_write(main, TIMING_START, 0);
		return;
	}
	if (timing.pass_count >= TIMING_PASS_ROOM || !timing_write(main, TIMING_PASS, timing.pass_count))
		return;
	if (outside)
	{
		snprintf(timing.passes[timing.pass_count].label, TIMING_LABEL, "outside passes");
		timing.passes[timing.pass_count].width = timing.passes[timing.pass_count].height = 0;
	}
	else
	{
		image = segment->color.image ? segment->color.image : segment->depth.image;
		snprintf(timing.passes[timing.pass_count].label, TIMING_LABEL, "%s%ux%u%s%s%s", aside ? "aside " : "pass ",
			segment->area.extent.width, segment->area.extent.height, segment->color.image ? " colour" : "",
			segment->depth.image ? " depth" : "", image && image->description.samples > 1 ? " msaa" : "");
		timing.passes[timing.pass_count].width = segment->area.extent.width;
		timing.passes[timing.pass_count].height = segment->area.extent.height;
	}
	timing.pass_count++;
}

/* (the frame's end, its last submission made) */
static void timing_frame_end(void)
{
	if (!timing.active)
		return;
	timing.active = 0;
	timing.done = 1;
}

int xgpu_vulkan_timing_wait(double seconds, struct xgpu_vulkan_timing *result)
{
	static uint64_t times[TIMING_QUERIES][2];
	uint64_t mask = vk.timestamp_bits >= 64 ? UINT64_MAX : (UINT64_C(1) << vk.timestamp_bits) - 1, first = 0, previous = 0;
	double period = (double)vk.properties.limits.timestampPeriod * 1e-6, frame = 0.0, waited = 0.0;
	uint32_t index, pass, draws = 0;
	int have_first = 0, have_previous = 0;
	VkResult status;

	memset(result, 0, sizeof(*result));
	if (!timing.done || !timing.used)
		return 0;
	/* (the timestamps of the main command buffer all written: the frame is
	done; a draw's segment that was not played has none) */
	for (;;)
	{
		int ready = 1;

		status = vkGetQueryPoolResults(vk.device, timing.pool, 0, timing.used, sizeof(times[0]) * timing.used, times,
			sizeof(times[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
		if (status != VK_SUCCESS && status != VK_NOT_READY)
			return 0;
		for (index = 0; index < timing.used && ready; index++)
		{
			if ((timing.marks[index].what == TIMING_START || timing.marks[index].what == TIMING_PASS) && !times[index][1])
				ready = 0;
		}
		if (ready)
			break;
		if (waited >= seconds)
			return 0;
		{
			struct timespec pause = { 0, 200000 };

			nanosleep(&pause, NULL);
			waited += 0.0002;
		}
	}
	/* the passes: each from the timestamp before it, in its submission */
	for (index = 0; index < timing.used; index++)
	{
		if (timing.marks[index].what == TIMING_START)
		{
			if (!have_first)
			{
				first = times[index][0];
				have_first = 1;
			}
			previous = times[index][0];
			have_previous = 1;
			continue;
		}
		if (timing.marks[index].what != TIMING_PASS || !have_previous)
			continue;
		pass = timing.marks[index].index;
		timing.results[pass].kind = timing.passes[pass].label;
		timing.results[pass].width = timing.passes[pass].width;
		timing.results[pass].height = timing.passes[pass].height;
		timing.results[pass].draws = 0;
		timing.results[pass].start_ms = (double)((previous - first) & mask) * period;
		timing.results[pass].end_ms = (double)((times[index][0] - first) & mask) * period;
		timing.results[pass].ms = (double)((times[index][0] - previous) & mask) * period;
		frame += timing.results[pass].ms;
		previous = times[index][0];
	}
	/* the draws: each its own time, in the pass whose span holds its end */
	for (index = 0; index + 1 < timing.used; index++)
	{
		if (timing.marks[index].what == TIMING_DRAW_BEFORE && timing.marks[index + 1].what == TIMING_DRAW_AFTER &&
			timing.marks[index + 1].index + 1 > draws)
			draws = timing.marks[index + 1].index + 1;
	}
	if (draws > timing.draw_capacity)
	{
		uint32_t *grown_pass = realloc(timing.draw_pass, draws * sizeof(*grown_pass));
		double *grown_ms;

		if (grown_pass)
			timing.draw_pass = grown_pass;
		grown_ms = realloc(timing.draw_ms, draws * sizeof(*grown_ms));
		if (grown_ms)
			timing.draw_ms = grown_ms;
		if (!grown_pass || !grown_ms)
			return 0;
		timing.draw_capacity = draws;
	}
	for (index = 0; index < draws; index++)
	{
		timing.draw_pass[index] = UINT32_MAX;
		timing.draw_ms[index] = 0.0;
	}
	for (index = 0; index + 1 < timing.used; index++)
	{
		uint32_t draw = timing.marks[index].index;
		double end;

		if (timing.marks[index].what != TIMING_DRAW_BEFORE || timing.marks[index + 1].what != TIMING_DRAW_AFTER ||
			!times[index][1] || !times[index + 1][1] || !have_first)
			continue;
		/* (a draw handed on in pieces: their times summed) */
		timing.draw_ms[draw] += (double)((times[index + 1][0] - times[index][0]) & mask) * period;
		end = (double)((times[index + 1][0] - first) & mask) * period;
		for (pass = 0; pass < timing.pass_count; pass++)
		{
			if (end > timing.results[pass].start_ms && end <= timing.results[pass].end_ms)
			{
				if (timing.draw_pass[draw] == UINT32_MAX)
					timing.results[pass].draws++;
				timing.draw_pass[draw] = pass;
				break;
			}
		}
	}
	snprintf(timing.note, sizeof(timing.note), "Vulkan timestamps at the bottom of the pipeline about each draw "
		"(period %.3f ns, %u valid bits): overlapping draws are drawn one after another, so a draw's ms is its own "
		"and the timed frame is slower than untimed; a tiler's draws are shaded at the pass's end",
		(double)vk.properties.limits.timestampPeriod, vk.timestamp_bits);
	result->frame_ms = frame;
	result->passes = timing.results;
	result->pass_count = timing.pass_count;
	result->draw_pass = timing.draw_pass;
	result->draw_ms = timing.draw_ms;
	result->draw_count = draws;
	result->note = timing.note;
	return frame > 0.0;
}

/* ---------- the upload ring */

/* the start of size bytes at the ring's head, aligned, wrapped to the
ring's beginning rather than across its end */
static uint64_t ring_start(uint32_t size, uint32_t alignment)
{
	uint64_t start = (vk.ring_head + alignment - 1) & ~(uint64_t)(alignment - 1);

	if (start / RING_SIZE != (start + size - 1) / RING_SIZE)
		start = (start + size - 1) / RING_SIZE * RING_SIZE;
	return start;
}

void *xgpu_vulkan_upload(uint32_t size, uint32_t alignment, struct xgpu_vulkan_range *range)
{
	uint64_t start;

	if (!size || size > RING_SIZE / 4)
		return NULL;
	recording_begin();
	start = ring_start(size, alignment);
	/* the bytes the GPU may still read: wait for the submissions that read
	them, or (all of them are this recording's) submit it first */
	while (start + size - vk.ring_free_from > RING_SIZE)
	{
		uint32_t index;
		uint64_t oldest = 0;

		for (index = 0; index < SLOTS; index++)
		{
			struct slot *slot = &vk.slots[index];

			if (slot->serial && slot->serial < vk.serial && slot->serial > vk.completed &&
				(!oldest || slot->serial < oldest))
				oldest = slot->serial;
		}
		if (oldest)
		{
			serial_wait(oldest);
			continue;
		}
		xgpu_vulkan_wait(xgpu_vulkan_fence());
		recording_begin();
		start = ring_start(size, alignment);
		if (start + size - vk.ring_free_from > RING_SIZE)
			return NULL;
	}
	vk.ring_head = start + size;
	vk.statistics.ring_bytes += size;
	range->buffer = vk.ring;
	range->offset = start % RING_SIZE;
	return vk.ring_mapped + range->offset;
}

/* ---------- queries */

/* The visibility tests' counts reach the game as OpenCE's OpenGL backend's
did: the counts of a frame's tests copied at the next frame's end (by the
GPU, after that frame's work) and read at the end of the frame after it, so
that the game, drawing frame k + 1, has frame k - 2's. Here they are written into the
game's slots at the end of frame k (the frame's queries waited for, which
they have long been): with the guest waiting for each frame's GPU work (as
a test of the same frames does), the game has the same counts in the same
frames as it had with OpenGL. */
static void queries_publish(uint64_t last)
{
	/* the latest test of each slot, and its count so far */
	static uint64_t sums[XGPU_VULKAN_VISIBILITY_SLOTS];
	static float areas[XGPU_VULKAN_VISIBILITY_SLOTS];
	static uint32_t tests[XGPU_VULKAN_VISIBILITY_SLOTS];
	static uint8_t seen[XGPU_VULKAN_VISIBILITY_SLOTS];
	/* the game's number for the test each slot's count is of */
	static uint32_t numbers[XGPU_VULKAN_VISIBILITY_SLOTS];

	for (;;)
	{
		struct query_frame *frame = NULL;
		uint64_t *results = NULL;
		uint32_t query;
		int index;

		/* (the oldest frame's first: a slot's latest count wins) */
		for (index = 0; index < QUERY_FRAMES; index++)
		{
			struct query_frame *candidate = &vk.query_frames[index];

			if (candidate->in_use && candidate->serial && candidate->frame <= last &&
				(!frame || candidate->frame < frame->frame))
				frame = candidate;
		}
		if (!frame)
			return;
		serial_wait(frame->serial);
		memset(seen, 0, sizeof(seen));
		/* (in the order the tests were made: a slot tested twice in a frame
		has the later's count, a test of several queries their sum) */
		for (query = 0; query < frame->empty_count; query++)
		{
			uint16_t slot = frame->empty[query];

			if (slot < XGPU_VULKAN_VISIBILITY_SLOTS)
			{
				seen[slot] = 1;
				sums[slot] = 0;
				areas[slot] = 1.0f;
				tests[slot] = 0;
				numbers[slot] = frame->empty_numbers[query];
			}
		}
		if (frame->count)
		{
			results = malloc(frame->count * sizeof(*results));
			if (results && vkGetQueryPoolResults(vk.device, frame->pool, 0, frame->count, frame->count * sizeof(*results),
				results, sizeof(*results), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
			{
				for (query = 0; query < frame->count; query++)
				{
					uint16_t slot = frame->slots[query];

					if (slot >= XGPU_VULKAN_VISIBILITY_SLOTS)
						continue;
					if (!seen[slot] || tests[slot] != frame->tests[query])
					{
						sums[slot] = 0;
						tests[slot] = frame->tests[query];
						seen[slot] = 1;
					}
					sums[slot] += results[query];
					areas[slot] = frame->areas[query];
					numbers[slot] = frame->numbers[query];
				}
			}
			free(results);
			vkResetQueryPool(vk.device, frame->pool, 0, frame->count);
		}
		/* The shader counters are in test order: the last test of a slot
		wins, including a test whose count is zero. */
		for (query = 0; query < frame->counter_count; query++)
		{
			uint32_t slot = frame->counter_slots[query];
			uint32_t base = (uint32_t)(frame - vk.query_frames) * (XGPU_VULKAN_VISIBILITY_SLOTS + 1);

			if (slot >= XGPU_VULKAN_VISIBILITY_SLOTS)
				continue;
			seen[slot] = 1;
			sums[slot] = ((volatile uint32_t *)vk.samples->mapped)[base + query];
			areas[slot] = frame->counter_areas[query];
			numbers[slot] = frame->counter_numbers[query];
		}
		if (vk.visibility_output)
		{
			for (query = 0; query < XGPU_VULKAN_VISIBILITY_SLOTS; query++)
			{
				uint64_t count;

				if (!seen[query])
					continue;
				/* the count in the game's pixels (as OpenCE's OpenGL backend
				counted it) */
				count = areas[query] > 1.0f ? (uint64_t)((float)sums[query] / areas[query] + 0.5f) : sums[query];
				vk.visibility_output[query] = count < XGPU_VULKAN_VISIBILITY_MOST ? (uint32_t)count :
					XGPU_VULKAN_VISIBILITY_MOST;
				/* (the test's number after its count: xgpu_stream.h) */
				__atomic_store_n(&vk.visibility_output[XGPU_VULKAN_VISIBILITY_SLOTS + query], numbers[query],
					__ATOMIC_RELEASE);
			}
		}
		frame->count = 0;
		frame->empty_count = 0;
		frame->serial = 0;
		frame->in_use = 0;
	}
}

/* the frame being recorded's queries */
static struct query_frame *queries_current(void)
{
	int index;

	if (vk.queries)
		return vk.queries;
	for (;;)
	{
		for (index = 0; index < QUERY_FRAMES; index++)
		{
			if (!vk.query_frames[index].in_use)
			{
				vk.queries = &vk.query_frames[index];
				vk.queries->in_use = 1;
				vk.queries->serial = 0;
				vk.queries->frame = vk.frame;
				vk.queries->count = 0;
				vk.queries->empty_count = 0;
				vk.queries->counter_count = 0;
				memset((uint32_t *)vk.samples->mapped + index * (XGPU_VULKAN_VISIBILITY_SLOTS + 1), 0,
					(XGPU_VULKAN_VISIBILITY_SLOTS + 1) * sizeof(uint32_t));
				return vk.queries;
			}
		}
		/* (every pool in use: the oldest frame's counts written now) */
		{
			uint64_t oldest = 0;
			int found = 0;

			for (index = 0; index < QUERY_FRAMES; index++)
			{
				if (vk.query_frames[index].serial && (!found || vk.query_frames[index].frame < oldest))
				{
					oldest = vk.query_frames[index].frame;
					found = 1;
				}
			}
			if (!found)
				return NULL;
			queries_publish(oldest);
		}
	}
}

/* a query of the current test begun (in a pass) */
static void test_query_begin(void)
{
	struct query_frame *frame;
	uint32_t views = vk.views ? vk.views : 1;
	uint32_t index;

	if (vk.sample_counting)
		return;
	frame = queries_current();
	if (!frame || frame->count + views > QUERIES_A_FRAME || vk.test_query_count >= 64)
		return;
	for (index = 0; index < views; index++)
	{
		frame->slots[frame->count + index] = 0xffff;
		frame->areas[frame->count + index] = 1.0f;
		frame->tests[frame->count + index] = vk.test_number;
	}
	vk.test_query_widths[vk.test_query_count] = (uint8_t)views;
	vk.test_queries[vk.test_query_count++] = frame->count;
	vkCmdBeginQuery(vk.pass_commands, frame->pool, frame->count, VK_QUERY_CONTROL_PRECISE_BIT);
	frame->count += views;
	vk.test_query_open = 1;
}

static void test_query_end(void)
{
	if (!vk.test_query_open)
		return;
	vkCmdEndQuery(vk.pass_commands, vk.queries->pool, vk.test_queries[vk.test_query_count - 1]);
	vk.test_query_open = 0;
}

void xgpu_vulkan_visibility_count_samples(int enabled)
{
	vk.sample_counting = enabled && vk.capabilities.fragment_stores;
}

uint32_t xgpu_vulkan_visibility_counter(void)
{
	struct query_frame *frame = vk.queries;

	if (!frame)
		return 0;
	return (uint32_t)(frame - vk.query_frames) * (XGPU_VULKAN_VISIBILITY_SLOTS + 1) + frame->counter_count;
}

void xgpu_vulkan_visibility_begin(void)
{
	vk.test_active = 1;
	vk.test_number++;
	vk.test_query_count = 0;
	vk.test_query_open = 0;
	if (vk.sample_counting)
		queries_current();
	else if (vk.pass_active)
		test_query_begin();
}

void xgpu_vulkan_visibility_end(uint32_t slot, float area, uint32_t number)
{
	uint32_t index;

	if (!vk.test_active)
		return;
	if (vk.sample_counting)
	{
		struct query_frame *frame = vk.queries;

		if (frame && frame->counter_count < XGPU_VULKAN_VISIBILITY_SLOTS)
		{
			frame->counter_slots[frame->counter_count] = (uint16_t)slot;
			frame->counter_numbers[frame->counter_count] = number;
			frame->counter_areas[frame->counter_count++] = area;
		}
		vk.test_active = 0;
		return;
	}
	test_query_end();
	/* (a test that drew in no pass: its slot's count is 0, as a query
	that counted nothing) */
	if (!vk.test_query_count)
	{
		struct query_frame *frame = queries_current();

		if (frame && frame->empty_count < QUERIES_A_FRAME)
		{
			frame->empty_numbers[frame->empty_count] = number;
			frame->empty[frame->empty_count++] = (uint16_t)slot;
		}
		vk.test_active = 0;
		return;
	}
	for (index = 0; index < vk.test_query_count; index++)
	{
		uint32_t view;

		for (view = 0; view < vk.test_query_widths[index]; view++)
		{
			vk.queries->slots[vk.test_queries[index] + view] = (uint16_t)slot;
			vk.queries->areas[vk.test_queries[index] + view] = area;
			vk.queries->numbers[vk.test_queries[index] + view] = number;
		}
	}
	vk.test_active = 0;
	vk.test_query_count = 0;
}

void xgpu_vulkan_set_visibility_output(volatile uint32_t *counts)
{
	vk.visibility_output = counts;
}

/* ---------- images */

static VkFormat vulkan_format(uint8_t format)
{
	switch (format)
	{
	case XGPU_VULKAN_FORMAT_BGRA8: return VK_FORMAT_B8G8R8A8_UNORM;
	case XGPU_VULKAN_FORMAT_D24S8: return vk.depth_format;
	case XGPU_VULKAN_FORMAT_DXT1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case XGPU_VULKAN_FORMAT_DXT3: return VK_FORMAT_BC2_UNORM_BLOCK;
	case XGPU_VULKAN_FORMAT_DXT5: return VK_FORMAT_BC3_UNORM_BLOCK;
	case XGPU_VULKAN_FORMAT_RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
	case XGPU_VULKAN_FORMAT_COVERAGE: return VK_FORMAT_R8_UNORM;
	case XGPU_VULKAN_FORMAT_RGBA32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
	case XGPU_VULKAN_FORMAT_RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case XGPU_VULKAN_FORMAT_RGB10A2: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	case XGPU_VULKAN_FORMAT_ASTC: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
	case XGPU_VULKAN_FORMAT_R8_SNORM: return VK_FORMAT_R8_SNORM;
	case XGPU_VULKAN_FORMAT_RG32_UINT: return VK_FORMAT_R32G32_UINT;
	case XGPU_VULKAN_FORMAT_RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case XGPU_VULKAN_FORMAT_RG8_SNORM: return VK_FORMAT_R8G8_SNORM;
	default: return VK_FORMAT_UNDEFINED;
	}
}

static int format_blocks(uint8_t format)
{
	return format == XGPU_VULKAN_FORMAT_DXT1 || format == XGPU_VULKAN_FORMAT_DXT3 || format == XGPU_VULKAN_FORMAT_DXT5 ||
		format == XGPU_VULKAN_FORMAT_ASTC;
}

static VkComponentSwizzle swizzle_of(uint32_t source)
{
	static const VkComponentSwizzle components[4] =
	{
		VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A,
	};

	return components[source & 3];
}

static VkImageView view_make(const struct xgpu_vulkan_image *image, VkImageViewType type, uint32_t first_layer,
	uint32_t layers, int sampled)
{
	VkImageViewCreateInfo info;
	VkImageView view = VK_NULL_HANDLE;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = image->image;
	info.viewType = type;
	info.format = image->format;
	info.subresourceRange.aspectMask = sampled && (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ?
		VK_IMAGE_ASPECT_DEPTH_BIT : image->aspect;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = sampled ? image->description.levels : 1;
	info.subresourceRange.baseArrayLayer = first_layer;
	info.subresourceRange.layerCount = layers;
	if (sampled && image->description.format == XGPU_VULKAN_FORMAT_COVERAGE)
	{
		info.components.r = info.components.g = info.components.b = VK_COMPONENT_SWIZZLE_ONE;
		info.components.a = VK_COMPONENT_SWIZZLE_R;
	}
	else if (sampled && (image->description.channels & 0x100))
	{
		uint32_t channels = image->description.channels;

		info.components.r = swizzle_of(channels);
		info.components.g = swizzle_of(channels >> 2);
		info.components.b = swizzle_of(channels >> 4);
		info.components.a = swizzle_of(channels >> 6);
	}
	check(vkCreateImageView(vk.device, &info, NULL, &view), "vkCreateImageView");
	return view;
}

/* With discards: memory for a transient image, lazily allocated and its
own where the device has such memory (vk.transient 1), else as any image's */
static int memory_transient(const VkMemoryRequirements *requirements, struct allocation *allocation)
{
	int type = vk.transient == 1 ?
		memory_type(requirements->memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, 0) : -1;

	if (type >= 0 && memory_dedicated(requirements->size, (uint32_t)type, allocation))
		return 1;
	return memory_for(requirements, allocation);
}

/* A transient image into the GENERAL layout, which it keeps, cleared as
any image is (zero; depth 1, stencil 0), by a pass: it may be used only as
an attachment. With the early work, before the work recorded so far. */
static void transient_clear(struct xgpu_vulkan_image *image, uint32_t layers)
{
	VkCommandBuffer commands = early_commands();
	VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	VkAccessFlags writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	int depth = (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
	VkImageMemoryBarrier transition;
	VkRenderingAttachmentInfo attachment;
	VkRenderingInfo rendering;
	VkImageView view = view_make(image, layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D, 0, layers, 0);
	struct garbage item;

	memset(&transition, 0, sizeof(transition));
	transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	transition.dstAccessMask = writes;
	transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	transition.srcQueueFamilyIndex = transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	transition.image = image->image;
	transition.subresourceRange.aspectMask = image->aspect;
	transition.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	transition.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, stages, 0, 0, NULL, 0, NULL, 1, &transition);
	if (!view)
		return;
	memset(&attachment, 0, sizeof(attachment));
	attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	attachment.imageView = view;
	attachment.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	if (depth)
		attachment.clearValue.depthStencil.depth = 1.0f;
	memset(&rendering, 0, sizeof(rendering));
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent.width = image->description.width;
	rendering.renderArea.extent.height = image->description.height;
	rendering.layerCount = layers;
	if (depth)
	{
		rendering.pDepthAttachment = &attachment;
		rendering.pStencilAttachment = &attachment;
	}
	else
	{
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &attachment;
	}
	vkCmdBeginRendering(commands, &rendering);
	vkCmdEndRendering(commands);
	/* (before everything after it: the early work's end orders only its
	transfers) */
	barrier(commands, stages, writes, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
	memset(&item, 0, sizeof(item));
	item.kind = _garbage_view;
	item.serial = vk.serial;
	item.view = view;
	garbage_add(&item);
}

struct xgpu_vulkan_image *xgpu_vulkan_image_create(const struct xgpu_vulkan_image_description *description)
{
	struct xgpu_vulkan_image *image = calloc(1, sizeof(*image));
	VkImageCreateInfo info;
	VkImageCompressionControlEXT uncompressed;
	VkMemoryRequirements requirements;
	uint32_t layers = 1;

	if (!image)
		return NULL;
	image->number = ++vk.image_number;
	image->description = *description;
	if (!image->description.levels)
		image->description.levels = 1;
	image->format = vulkan_format(description->format);
	image->aspect = description->format == XGPU_VULKAN_FORMAT_D24S8 ?
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = description->type == XGPU_VULKAN_IMAGE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
	info.format = image->format;
	info.extent.width = description->width ? description->width : 1;
	info.extent.height = description->height ? description->height : 1;
	info.extent.depth = description->type == XGPU_VULKAN_IMAGE_3D ? description->depth_or_layers : 1;
	if (description->type == XGPU_VULKAN_IMAGE_CUBE)
	{
		layers = 6;
		info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}
	else if (description->type == XGPU_VULKAN_IMAGE_2D_ARRAY)
	{
		layers = description->depth_or_layers ? description->depth_or_layers : 1;
	}
	info.mipLevels = image->description.levels;
	info.arrayLayers = layers;
	info.samples = description->samples > 1 ? (VkSampleCountFlagBits)description->samples : VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (description->samples <= 1)
		info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (description->usage & XGPU_VULKAN_USAGE_TARGET)
		info.usage |= image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	if ((description->usage & XGPU_VULKAN_USAGE_INPUT) && vk.capabilities.color_read)
		info.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	/* (and written by the interlocked draws as a storage image) */
	if ((description->usage & XGPU_VULKAN_USAGE_INPUT) && vk.capabilities.pixel_interlock &&
		info.format == VK_FORMAT_R16G16B16A16_SFLOAT)
		info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
	/* (with discards, where the device has lazily allocated memory: a
	multisampled target is an attachment alone, transient) */
	image->transient = vk.transient && description->samples > 1 && (description->usage & XGPU_VULKAN_USAGE_TARGET) &&
		!(info.usage & VK_IMAGE_USAGE_STORAGE_BIT);
	if (image->transient)
		info.usage = (info.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)) | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	/* (a target the interlocked draws write as a storage image while the
	pass draws into it, uncompressed where the device can say so: RADV's
	DCC left specks of other colours where it was compressed, on an RDNA
	3.5 GPU, Mesa 26.2) */
	memset(&uncompressed, 0, sizeof(uncompressed));
	uncompressed.sType = VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT;
	uncompressed.flags = VK_IMAGE_COMPRESSION_DISABLED_EXT;
	if ((info.usage & VK_IMAGE_USAGE_STORAGE_BIT) && vk.compression_control)
		info.pNext = &uncompressed;
	if (image->format == VK_FORMAT_UNDEFINED || !check(vkCreateImage(vk.device, &info, NULL, &image->image), "vkCreateImage"))
	{
		host_logf(HOST_LOG_ERROR, "vulkan: cannot make a %ux%u image of format %u", description->width, description->height,
			(unsigned)description->format);
		free(image);
		return NULL;
	}
	vkGetImageMemoryRequirements(vk.device, image->image, &requirements);
	if (!(image->transient ? memory_transient(&requirements, &image->memory) : memory_for(&requirements, &image->memory)) ||
		!check(vkBindImageMemory(vk.device, image->image, image->memory.memory, image->memory.offset), "vkBindImageMemory"))
	{
		vkDestroyImage(vk.device, image->image, NULL);
		memory_release(&image->memory);
		free(image);
		return NULL;
	}
	if (description->samples <= 1)
	{
		VkImageViewType type = description->type == XGPU_VULKAN_IMAGE_3D ? VK_IMAGE_VIEW_TYPE_3D :
			description->type == XGPU_VULKAN_IMAGE_CUBE ? VK_IMAGE_VIEW_TYPE_CUBE :
			description->type == XGPU_VULKAN_IMAGE_2D_ARRAY ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;

		image->sampled = view_make(image, type, 0, layers, 1);
	}
	if (image->transient)
	{
		transient_clear(image, layers);
		return image;
	}
	/* (into the GENERAL layout, which it keeps, its texels zero: before the
	work recorded so far, or with the early work) */
	{
		VkCommandBuffer commands = early_commands();
		VkImageMemoryBarrier transition;

		memset(&transition, 0, sizeof(transition));
		transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		transition.srcAccessMask = 0;
		transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		transition.srcQueueFamilyIndex = transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		transition.image = image->image;
		transition.subresourceRange.aspectMask = image->aspect;
		transition.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		transition.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
		vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
			1, &transition);
		if (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
		{
			VkClearDepthStencilValue clear = { 1.0f, 0 };

			vkCmdClearDepthStencilImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1,
				&transition.subresourceRange);
		}
		else if (!format_blocks(description->format))
		{
			VkClearColorValue clear;

			memset(&clear, 0, sizeof(clear));
			vkCmdClearColorImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &transition.subresourceRange);
		}
		/* (before the uploads after it) */
		barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
	}
	return image;
}

void xgpu_vulkan_image_destroy(struct xgpu_vulkan_image *image)
{
	struct garbage item;
	uint32_t index;

	if (!image)
		return;
	if (vk.color == image || vk.depth == image)
	{
		pass_end();
		if (vk.color == image)
			vk.color = NULL;
		if (vk.depth == image)
			vk.depth = NULL;
	}
	if (image == vk.chain_color || image == vk.chain_depth)
		aside_break();
	/* (with discards: the submission's passes keep their views, which are
	freed after it, and store what they drew as before) */
	for (index = 0; index < vk.segment_count; index++)
	{
		struct segment *segment = &vk.segments[index];

		if (segment->color.image == image)
			segment->color.image = NULL;
		if (segment->depth.image == image)
			segment->depth.image = NULL;
	}
	if (vk.last_texture_valid)
	{
		for (index = 0; index < 4; index++)
		{
			if (vk.last_textures[index].image == image)
				vk.last_texture_valid = 0;
		}
	}
	memset(&item, 0, sizeof(item));
	/* (after the submission being recorded, which may use it) */
	item.serial = vk.serial;
	item.kind = _garbage_view;
	if (image->sampled)
	{
		item.view = image->sampled;
		garbage_add(&item);
	}
	if (image->sampled_2d)
	{
		item.view = image->sampled_2d;
		garbage_add(&item);
	}
	for (index = 0; index < image->attachment_count; index++)
	{
		if (image->attachments[index])
		{
			item.view = image->attachments[index];
			garbage_add(&item);
		}
	}
	item.kind = _garbage_image;
	item.image = image->image;
	item.memory = image->memory;
	garbage_add(&item);
	free(image->attachments);
	free(image);
}

const struct xgpu_vulkan_image_description *xgpu_vulkan_image_description(const struct xgpu_vulkan_image *image)
{
	return &image->description;
}

/* the commands an upload into an image goes in: the early ones where no
work recorded so far in this submission uses it */
static VkCommandBuffer upload_commands(uint64_t used, int image)
{
	if (vk.recording && used == vk.serial)
	{
		vk.statistics.uploads_ordered++;
		/* (an image's: put aside where the chain has not sampled it) */
		vk.aside_allowed = image;
		return main_commands();
	}
	vk.statistics.uploads_early++;
	return early_commands();
}

/* Main transfers must be visible to draws. Early transfers also need an
order among themselves: a watched buffer page can be uploaded more than
once before any draw uses it. */
static void transfer_done(VkCommandBuffer commands)
{
	if (commands != vk.slot->early)
		barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
			AFTER_STAGES, AFTER_ACCESS);
	else
		barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
}

void xgpu_vulkan_image_upload(struct xgpu_vulkan_image *image, uint32_t level, uint32_t layer, uint32_t x, uint32_t y,
	uint32_t z, uint32_t width, uint32_t height, uint32_t depth, const void *data, uint32_t bytes_per_row,
	uint32_t bytes_per_image)
{
	const struct xgpu_vulkan_image_description *description = &image->description;
	int blocks = format_blocks(description->format);
	uint32_t texel = description->format == XGPU_VULKAN_FORMAT_COVERAGE ||
		description->format == XGPU_VULKAN_FORMAT_R8_SNORM ? 1 :
		description->format == XGPU_VULKAN_FORMAT_RG8_SNORM ? 2 :
		description->format == XGPU_VULKAN_FORMAT_RGBA32_UINT || description->format == XGPU_VULKAN_FORMAT_RGBA32F ? 16 :
		description->format == XGPU_VULKAN_FORMAT_RGBA16F || description->format == XGPU_VULKAN_FORMAT_RG32_UINT ? 8 : 4;
	uint32_t rows = blocks ? (height + 3) / 4 : height;
	uint32_t row_bytes = blocks ? (description->format == XGPU_VULKAN_FORMAT_DXT1 ? 8 : 16) * ((width + 3) / 4) :
		width * texel;
	uint32_t size, slice, row;
	struct xgpu_vulkan_range range;
	unsigned char *staging;
	VkBufferImageCopy copy;
	VkCommandBuffer commands;

	if (!depth)
		depth = 1;
	if (!bytes_per_row)
		bytes_per_row = row_bytes;
	if (!bytes_per_image)
		bytes_per_image = bytes_per_row * rows;
	size = row_bytes * rows * depth;
	staging = xgpu_vulkan_upload(size, 16, &range);
	if (!staging)
		return;
	/* (the commands chosen first: the upload may have submitted the
	recording) */
	commands = upload_commands(image->used, 1);
	/* (an early upload is played before every pass of the submission: not
	a use by the pass being recorded, which would mark it read by the
	chain's piece and then the write after that read, ending the chain;
	a draw of the submission samples it after) */
	if (vk.deferred && commands == vk.slot->early)
	{
		image_first_use(image, 0, "an early upload");
		image->writes++;
	}
	else
	{
		image_use(image);
		image_written(image);
	}
	for (slice = 0; slice < depth; slice++)
	{
		for (row = 0; row < rows; row++)
		{
			memcpy(staging + (slice * rows + row) * row_bytes,
				(const unsigned char *)data + slice * bytes_per_image + row * bytes_per_row, row_bytes);
		}
	}
	memset(&copy, 0, sizeof(copy));
	copy.bufferOffset = range.offset;
	copy.imageSubresource.aspectMask = image->aspect;
	copy.imageSubresource.mipLevel = level;
	copy.imageSubresource.baseArrayLayer = description->type == XGPU_VULKAN_IMAGE_3D ? 0 : layer;
	copy.imageSubresource.layerCount = 1;
	copy.imageOffset.x = (int32_t)x;
	copy.imageOffset.y = (int32_t)y;
	copy.imageOffset.z = (int32_t)z;
	copy.imageExtent.width = width;
	copy.imageExtent.height = height;
	copy.imageExtent.depth = depth;
	vkCmdCopyBufferToImage(commands, range.buffer, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	transfer_done(commands);
}

void xgpu_vulkan_image_make_levels(struct xgpu_vulkan_image *image, uint32_t from)
{
	const struct xgpu_vulkan_image_description *description = &image->description;
	VkCommandBuffer commands = upload_commands(image->used, 1);
	uint32_t level, layers = description->type == XGPU_VULKAN_IMAGE_CUBE ? 6 :
		description->type == XGPU_VULKAN_IMAGE_2D_ARRAY ? description->depth_or_layers : 1;

	image_use(image);
	image_written(image);
	if (!from)
		from = 1;
	for (level = from; level < description->levels; level++)
	{
		VkImageBlit blit;
		int32_t width = (int32_t)(description->width >> (level - 1)), height = (int32_t)(description->height >> (level - 1));
		int32_t depth = description->type == XGPU_VULKAN_IMAGE_3D ? (int32_t)(description->depth_or_layers >> (level - 1)) : 1;

		barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_ACCESS_TRANSFER_READ_BIT);
		memset(&blit, 0, sizeof(blit));
		blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = image->aspect;
		blit.srcSubresource.mipLevel = level - 1;
		blit.dstSubresource.mipLevel = level;
		blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = layers;
		blit.srcOffsets[1].x = width > 0 ? width : 1;
		blit.srcOffsets[1].y = height > 0 ? height : 1;
		blit.srcOffsets[1].z = depth > 0 ? depth : 1;
		blit.dstOffsets[1].x = width > 1 ? width / 2 : 1;
		blit.dstOffsets[1].y = height > 1 ? height / 2 : 1;
		blit.dstOffsets[1].z = depth > 1 ? depth / 2 : 1;
		vkCmdBlitImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit,
			VK_FILTER_LINEAR);
	}
	transfer_done(commands);
}

void xgpu_vulkan_image_copy(struct xgpu_vulkan_image *source, uint32_t source_level, uint32_t source_layer,
	struct xgpu_vulkan_image *destination, uint32_t destination_level, uint32_t destination_layer)
{
	VkCommandBuffer commands;
	VkImageCopy copy;
	uint32_t width = source->description.width >> source_level, height = source->description.height >> source_level;
	uint32_t destination_width = destination->description.width >> destination_level;
	uint32_t destination_height = destination->description.height >> destination_level;

	/* (put aside where the chain neither drew nor sampled its images) */
	vk.aside_allowed = 1;
	commands = main_commands();
	memset(&copy, 0, sizeof(copy));
	copy.srcSubresource.aspectMask = source->aspect;
	copy.srcSubresource.mipLevel = source_level;
	copy.srcSubresource.baseArrayLayer = source_layer;
	copy.srcSubresource.layerCount = 1;
	copy.dstSubresource.aspectMask = destination->aspect;
	copy.dstSubresource.mipLevel = destination_level;
	copy.dstSubresource.baseArrayLayer = destination_layer;
	copy.dstSubresource.layerCount = 1;
	copy.extent.width = (width < destination_width ? width : destination_width);
	copy.extent.height = (height < destination_height ? height : destination_height);
	copy.extent.depth = 1;
	/* (a transient image is an attachment alone: the stream copies no
	multisampled image) */
	if (!copy.extent.width || !copy.extent.height || source->transient || destination->transient)
		return;
	source->used = destination->used = vk.serial;
	image_use(source);
	image_use(destination);
	image_written(destination);
	if (source->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		vkCmdCopyImage(commands, source->image, VK_IMAGE_LAYOUT_GENERAL, destination->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
		copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	vkCmdCopyImage(commands, source->image, VK_IMAGE_LAYOUT_GENERAL, destination->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	transfer_done(commands);
}

void xgpu_vulkan_image_copy_rectangle(struct xgpu_vulkan_image *source, uint32_t source_layer,
	struct xgpu_vulkan_image *destination, const int32_t rectangle[4])
{
	VkCommandBuffer commands;
	VkImageCopy copy;
	int32_t right = rectangle[2], bottom = rectangle[3];

	/* (inside both) */
	if (right > (int32_t)source->description.width)
		right = (int32_t)source->description.width;
	if (right > (int32_t)destination->description.width)
		right = (int32_t)destination->description.width;
	if (bottom > (int32_t)source->description.height)
		bottom = (int32_t)source->description.height;
	if (bottom > (int32_t)destination->description.height)
		bottom = (int32_t)destination->description.height;
	if (rectangle[0] < 0 || rectangle[1] < 0 || right <= rectangle[0] || bottom <= rectangle[1] ||
		(source->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) || source->transient || destination->transient)
		return;
	vk.aside_allowed = 1;
	commands = main_commands();
	memset(&copy, 0, sizeof(copy));
	copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.srcSubresource.baseArrayLayer = source_layer;
	copy.srcSubresource.layerCount = copy.dstSubresource.layerCount = 1;
	copy.srcOffset.x = copy.dstOffset.x = rectangle[0];
	copy.srcOffset.y = copy.dstOffset.y = rectangle[1];
	copy.extent.width = (uint32_t)(right - rectangle[0]);
	copy.extent.height = (uint32_t)(bottom - rectangle[1]);
	copy.extent.depth = 1;
	source->used = destination->used = vk.serial;
	image_use(source);
	image_use(destination);
	image_written(destination);
	vkCmdCopyImage(commands, source->image, VK_IMAGE_LAYOUT_GENERAL, destination->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	transfer_done(commands);
}

/* OpenXR guarantees this layout after xrWaitSwapchainImage and requires
it again at release. The copy preserves encoded colour bytes, as GL does
with FRAMEBUFFER_SRGB disabled, including for the runtime's sRGB images. */
void xgpu_vulkan_image_to_runtime(struct xgpu_vulkan_image *source, VkImage destination, uint32_t layers)
{
	VkCommandBuffer commands;
	VkImageMemoryBarrier transition;
	VkImageCopy copy;
	int depth;
	VkImageLayout acquired_layout;
	VkPipelineStageFlags attachment_stages;
	VkAccessFlags attachment_access;

	if (!source || !destination || !layers || source->transient)
		return;
	depth = (source->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
	acquired_layout = depth ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment_stages = depth ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT :
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	attachment_access = depth ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT :
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	commands = main_commands();
	image_use(source);
	memset(&transition, 0, sizeof(transition));
	transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	transition.srcAccessMask = attachment_access;
	transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transition.oldLayout = acquired_layout;
	transition.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transition.srcQueueFamilyIndex = transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	transition.image = destination;
	transition.subresourceRange.aspectMask = source->aspect;
	transition.subresourceRange.levelCount = 1;
	transition.subresourceRange.layerCount = layers;
	vkCmdPipelineBarrier(commands, attachment_stages, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, NULL, 0, NULL, 1, &transition);
	memset(&copy, 0, sizeof(copy));
	copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	copy.srcSubresource.layerCount = copy.dstSubresource.layerCount = layers;
	copy.extent.width = source->description.width;
	copy.extent.height = source->description.height;
	copy.extent.depth = 1;
	vkCmdCopyImage(commands, source->image, VK_IMAGE_LAYOUT_GENERAL, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &copy);
	if (depth)
	{
		copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		vkCmdCopyImage(commands, source->image, VK_IMAGE_LAYOUT_GENERAL, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &copy);
	}
	transition.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transition.dstAccessMask = attachment_access;
	transition.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transition.newLayout = acquired_layout;
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, attachment_stages,
		0, 0, NULL, 0, NULL, 1, &transition);
	source->used = vk.serial;
}

VkFormat xgpu_vulkan_depth_format(void)
{
	return vk.depth_format;
}

static VkImageView attachment_view(struct xgpu_vulkan_image *image, uint32_t layer, uint32_t views);

/* With discards: a multisampled image's resolve made in the pass that has
just drawn it (the submission's last segment), as that pass ends, where
that is the resolve outside it: the pass is of the whole image's first
layer, the image resolved into has its size and format, and nothing used
that image since the pass began. 1 if it is. */
static int resolve_in_pass(struct xgpu_vulkan_image *multisampled, struct xgpu_vulkan_image *image)
{
	int depth = (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
	struct segment *segment;
	struct pass_attachment *attachment;
	VkImageView view;

	if (!vk.deferred || !vk.recording || (depth && !vk.depth_resolve))
		return 0;
	pass_end();
	if (vk.segment_open || !vk.segment_count)
		return 0;
	segment = &vk.segments[vk.segment_count - 1];
	attachment = depth ? &segment->depth : &segment->color;
	if (!segment->pass || attachment->image != multisampled || attachment->resolve_view || segment->layer ||
		segment->views != 1 || segment->area.offset.x || segment->area.offset.y ||
		segment->area.extent.width != image->description.width ||
		segment->area.extent.height != image->description.height ||
		multisampled->description.width != image->description.width ||
		multisampled->description.height != image->description.height || multisampled->format != image->format ||
		image->description.samples > 1 || !(image->description.usage & XGPU_VULKAN_USAGE_TARGET) ||
		(image->use_serial == vk.serial && image->use_mark >= vk.segment_count) ||
		(image->pass_serial == vk.serial && image->pass_segment == vk.segment_count))
		return 0;
	view = attachment_view(image, 0, 1);
	if (!view)
		return 0;
	attachment->resolve_view = view;
	multisampled->used = image->used = vk.serial;
	image_use(image);
	/* (a pass put aside resolving: what it writes checked; the chain's
	last piece resolving is its last, aside_continues) */
	aside_image_written(image);
	vk.statistics.resolves_in_pass++;
	return 1;
}

void xgpu_vulkan_image_resolve(struct xgpu_vulkan_image *multisampled, struct xgpu_vulkan_image *image)
{
	VkCommandBuffer commands;

	if (vk.deferred)
	{
		/* (samples not drawn since they were last resolved into this
		image, which nothing has written since: the same pixels again,
		not resolved, as the samples may since have been discarded at a
		frame's end; the stream resolves a target bound but not drawn
		into) */
		if (multisampled->resolved_into == image->number && multisampled->resolved_writes == multisampled->writes &&
			multisampled->resolved_into_writes == image->writes)
		{
			vk.statistics.resolves_skipped++;
			return;
		}
		image->writes++;
		multisampled->resolved_into = image->number;
		multisampled->resolved_writes = multisampled->writes;
		multisampled->resolved_into_writes = image->writes;
	}
	if (resolve_in_pass(multisampled, image))
		return;
	commands = main_commands();
	multisampled->used = image->used = vk.serial;
	image_use(multisampled);
	image_use(image);
	if (multisampled->transient && !(image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT))
	{
		/* (a transient image's samples, which only a pass may read:
		resolved by a pass that loads them and stores nothing) */
		VkRenderingAttachmentInfo color;
		VkRenderingInfo rendering;
		VkImageView source = attachment_view(multisampled, 0, 1), destination = attachment_view(image, 0, 1);

		if (!source || !destination)
			return;
		memset(&color, 0, sizeof(color));
		color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color.imageView = source;
		color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		color.resolveImageView = destination;
		color.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
		color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		color.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
		memset(&rendering, 0, sizeof(rendering));
		rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering.renderArea.extent.width = image->description.width;
		rendering.renderArea.extent.height = image->description.height;
		rendering.layerCount = 1;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &color;
		vkCmdBeginRendering(commands, &rendering);
		vkCmdEndRendering(commands);
		pass_barrier(commands);
		return;
	}
	if (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		/* (depth: the first sample, drawn by a pass that resolves it) */
		VkRenderingAttachmentInfo depth;
		VkRenderingInfo rendering;
		VkImageView source = VK_NULL_HANDLE, destination = VK_NULL_HANDLE;

		source = view_make(multisampled, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0);
		destination = view_make(image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 0);
		if (source && destination)
		{
			memset(&depth, 0, sizeof(depth));
			depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth.imageView = source;
			depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
			depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
			depth.resolveImageView = destination;
			depth.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
			depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			memset(&rendering, 0, sizeof(rendering));
			rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
			rendering.renderArea.extent.width = image->description.width;
			rendering.renderArea.extent.height = image->description.height;
			rendering.layerCount = 1;
			rendering.pDepthAttachment = &depth;
			rendering.pStencilAttachment = &depth;
			vkCmdBeginRendering(commands, &rendering);
			vkCmdEndRendering(commands);
			barrier(commands, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, AFTER_STAGES, AFTER_ACCESS);
		}
		{
			struct garbage item;

			memset(&item, 0, sizeof(item));
			item.kind = _garbage_view;
			item.serial = vk.serial;
			if (source)
			{
				item.view = source;
				garbage_add(&item);
			}
			if (destination)
			{
				item.view = destination;
				garbage_add(&item);
			}
		}
		return;
	}
	{
		VkImageResolve resolve;

		memset(&resolve, 0, sizeof(resolve));
		resolve.srcSubresource.aspectMask = resolve.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		resolve.srcSubresource.layerCount = resolve.dstSubresource.layerCount = 1;
		resolve.extent.width = image->description.width;
		resolve.extent.height = image->description.height;
		resolve.extent.depth = 1;
		vkCmdResolveImage(commands, multisampled->image, VK_IMAGE_LAYOUT_GENERAL, image->image, VK_IMAGE_LAYOUT_GENERAL, 1,
			&resolve);
		transfer_done(commands);
	}
}

/* a half float's value (IEEE 754's binary16) */
static float half_value(uint16_t half)
{
	int exponent = (half >> 10) & 31;
	float magnitude = exponent == 0 ? ldexpf((float)(half & 1023), -24) : exponent == 31 ? INFINITY :
		ldexpf((float)((half & 1023) | 1024), exponent - 25);

	return half & 0x8000 ? -magnitude : magnitude;
}

/* a channel of the frame's values as 8 bits, as an SDR display shows it
(clamped to [0, 1]) */
static unsigned char half_byte(uint16_t half)
{
	float value = half_value(half);

	return (unsigned char)(value <= 0.0f ? 0 : value >= 1.0f ? 255 : (int)(value * 255.0f + 0.5f));
}

int xgpu_vulkan_read_pixels(struct xgpu_vulkan_image *image, uint32_t layer, void *pixels, uint32_t bytes_per_row,
	int tone_map)
{
	uint32_t width = image->description.width, height = image->description.height, row;
	/* (display.hdr's half floats: 8 bytes a pixel, made BGRA8's) */
	int halves = image->description.format == XGPU_VULKAN_FORMAT_RGBA16F;
	VkDeviceSize size = (VkDeviceSize)width * height * (halves ? 8 : 4);
	VkCommandBuffer commands;
	VkBufferImageCopy copy;

	if (image->description.samples > 1 || (image->aspect & VK_IMAGE_ASPECT_DEPTH_BIT))
		return 0;
	if (size > vk.readback_size)
	{
		if (vk.readback)
		{
			vkDestroyBuffer(vk.device, vk.readback, NULL);
			memory_release(&vk.readback_memory);
			vk.readback = VK_NULL_HANDLE;
		}
		if (!buffer_make(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BUFFER_READBACK, &vk.readback, &vk.readback_memory,
			(void **)&vk.readback_mapped))
		{
			vk.readback_size = 0;
			return 0;
		}
		vk.readback_size = size;
	}
	commands = main_commands();
	image_use(image);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.baseArrayLayer = layer;
	copy.imageSubresource.layerCount = 1;
	copy.imageExtent.width = width;
	copy.imageExtent.height = height;
	copy.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, vk.readback, 1, &copy);
	barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_READ_BIT);
	xgpu_vulkan_wait(xgpu_vulkan_fence());
	for (row = 0; row < height; row++)
	{
		unsigned char *to = (unsigned char *)pixels + (size_t)row * bytes_per_row;

		if (halves)
		{
			const uint16_t *from = (const uint16_t *)(vk.readback_mapped + (size_t)row * width * 8);
			uint32_t pixel;

			/* (RGBA as BGRA) */
			for (pixel = 0; pixel < width; pixel++)
			{
				if (tone_map)
				{
					float rgba[4] = { half_value(from[pixel * 4]), half_value(from[pixel * 4 + 1]),
						half_value(from[pixel * 4 + 2]), half_value(from[pixel * 4 + 3]) };

					xgpu_hdr_sdr_pixel(rgba, to + pixel * 4);
					continue;
				}
				to[pixel * 4] = half_byte(from[pixel * 4 + 2]);
				to[pixel * 4 + 1] = half_byte(from[pixel * 4 + 1]);
				to[pixel * 4 + 2] = half_byte(from[pixel * 4]);
				to[pixel * 4 + 3] = half_byte(from[pixel * 4 + 3]);
			}
		}
		else
			memcpy(to, vk.readback_mapped + (size_t)row * width * 4, (size_t)width * 4);
	}
	return 1;
}

/* ---------- buffers */

#define BUFFER_USAGE (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | \
	VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | \
	VK_BUFFER_USAGE_TRANSFER_SRC_BIT)

struct xgpu_vulkan_buffer *xgpu_vulkan_buffer_create(uint32_t size)
{
	struct xgpu_vulkan_buffer *buffer = calloc(1, sizeof(*buffer));

	if (!buffer)
		return NULL;
	if (!buffer_make(size, BUFFER_USAGE, 1, &buffer->buffer, &buffer->memory, &buffer->mapped))
	{
		free(buffer);
		return NULL;
	}
	buffer->size = size;
	memset(buffer->mapped, 0, size);
	return buffer;
}

VkBuffer xgpu_vulkan_buffer_handle(struct xgpu_vulkan_buffer *buffer)
{
	if (!buffer)
		return VK_NULL_HANDLE;
	/* (read by the work being recorded: writes into it are ordered after) */
	buffer->used = vk.serial;
	return buffer->buffer;
}

const void *xgpu_vulkan_buffer_contents(const struct xgpu_vulkan_buffer *buffer)
{
	if (!buffer)
		return NULL;
	/* (a renamed buffer's bytes are the GPU's copy, made as the submission
	that renamed it begins: waited for) */
	if (buffer->renamed > completed_serial())
		xgpu_vulkan_wait(vk.recording && buffer->renamed == vk.serial ? xgpu_vulkan_fence() : buffer->renamed);
	return buffer->mapped;
}

/* With aside passes: a buffer the submission being recorded reads, written
again, is renamed rather than written in the main command buffer's order
(which ends the pass, and the chain: "aside passes", above), as the Metal
backend renames its mirror's segments: a spare buffer of its size, which no
submission still reads (or a new one), takes its place, its bytes copied
from the old one by the early commands, before this submission's work and
after the earlier early writes into the old one; the write that follows
goes in the early commands too, after the copy. What was recorded
before keeps reading the old buffer, which is a spare from this
submission on. 1 if it was renamed. */
#define BUFFER_SPARES 16

struct buffer_spare
{
	VkBuffer buffer;
	struct allocation memory;
	void *mapped;
	uint32_t size;
	/* the last submission that may read it */
	uint64_t serial;
};

static int buffer_rename(struct xgpu_vulkan_buffer *buffer)
{
	struct buffer_spare old, fresh;
	uint64_t done = completed_serial();
	uint32_t index;
	VkBufferCopy copy;
	VkCommandBuffer commands;

	memset(&fresh, 0, sizeof(fresh));
	for (index = 0; index < vk.spare_count; index++)
	{
		if (vk.spares[index].size == buffer->size && vk.spares[index].serial <= done)
		{
			fresh = vk.spares[index];
			vk.spares[index] = vk.spares[--vk.spare_count];
			break;
		}
	}
	if (!fresh.buffer && !buffer_make(buffer->size, BUFFER_USAGE, 1, &fresh.buffer, &fresh.memory, &fresh.mapped))
		return 0;
	if (vk.spare_count == vk.spare_capacity)
	{
		uint32_t capacity = vk.spare_capacity ? vk.spare_capacity * 2 : 8;
		struct buffer_spare *grown = realloc(vk.spares, capacity * sizeof(*grown));

		if (!grown)
		{
			struct garbage item;

			memset(&item, 0, sizeof(item));
			item.kind = _garbage_buffer;
			item.serial = vk.serial;
			item.buffer = fresh.buffer;
			item.memory = fresh.memory;
			garbage_add(&item);
			return 0;
		}
		vk.spares = grown;
		vk.spare_capacity = capacity;
	}
	old.buffer = buffer->buffer;
	old.memory = buffer->memory;
	old.mapped = buffer->mapped;
	old.size = buffer->size;
	old.serial = vk.serial;
	commands = early_commands();
	memset(&copy, 0, sizeof(copy));
	copy.size = buffer->size;
	vkCmdCopyBuffer(commands, old.buffer, fresh.buffer, 1, &copy);
	transfer_done(commands);
	buffer->buffer = fresh.buffer;
	buffer->memory = fresh.memory;
	buffer->mapped = fresh.mapped;
	buffer->used = 0;
	buffer->renamed = vk.serial;
	/* (the spares beyond BUFFER_SPARES let go of, the oldest first, once
	the GPU has passed them) */
	if (vk.spare_count >= BUFFER_SPARES)
	{
		struct garbage item;

		memset(&item, 0, sizeof(item));
		item.kind = _garbage_buffer;
		item.serial = vk.spares[0].serial;
		item.buffer = vk.spares[0].buffer;
		item.memory = vk.spares[0].memory;
		garbage_add(&item);
		memmove(vk.spares, vk.spares + 1, (vk.spare_count - 1) * sizeof(*vk.spares));
		vk.spare_count--;
	}
	vk.spares[vk.spare_count++] = old;
	vk.statistics.buffers_renamed++;
	return 1;
}

void xgpu_vulkan_buffer_write(struct xgpu_vulkan_buffer *buffer, uint32_t offset, const void *data, uint32_t size,
	int unused)
{
	struct xgpu_vulkan_range range;
	VkBufferCopy copy;
	VkCommandBuffer commands;
	void *staging;

	if (!buffer || !size || (uint64_t)offset + size > buffer->size)
		return;
	/* (bytes no work reads, written as they are, but into a renamed buffer
	whose copy is still to be made, after which they go: also after it) */
	if (unused)
	{
		memcpy((unsigned char *)buffer->mapped + offset, data, size);
		if (buffer->renamed <= completed_serial())
			return;
	}
	staging = xgpu_vulkan_upload(size, 16, &range);
	if (!staging)
		return;
	memcpy(staging, data, size);
	if (unused)
		commands = early_commands();
	else if (vk.aside && vk.recording && buffer->used == vk.serial && buffer->ordered != vk.serial &&
		!buffer->named_by_set && buffer_rename(buffer))
		commands = early_commands();
	else
	{
		commands = upload_commands(buffer->used, 0);
		if (commands != vk.slot->early)
			buffer->ordered = vk.serial;
	}
	memset(&copy, 0, sizeof(copy));
	copy.srcOffset = range.offset;
	copy.dstOffset = offset;
	copy.size = size;
	vkCmdCopyBuffer(commands, range.buffer, buffer->buffer, 1, &copy);
	transfer_done(commands);
}

/* ---------- samplers */

/* NVIDIA's drivers blend between mip levels with anisotropic filtering as
OpenGL and Vulkan alike (a narrowed blend, "brilinear"), but OpenGL's blend
is centred between the levels and Vulkan's begins at the lower: OpenGL's
LOD is Vulkan's less a constant that depends on the most anisotropy, which
this bias takes off, so that this backend's textures are OpenCE's OpenGL
backend's pixel for pixel. Measured with random textured triangles drawn by
both on an RTX 4090: exact for 2x to 16x, both mip filters, every
anisotropy of the footprint; none without mip levels (OpenGL's GL_LINEAR
minification), and none on other drivers (not measured there). */
static float anisotropic_bias(float anisotropy)
{
	if (!vk.nvidia)
		return 0.0f;
	if (anisotropy >= 8.0f)
		return -64.0f / 256.0f;
	if (anisotropy >= 4.0f)
		return -54.0f / 256.0f;
	return -42.0f / 256.0f;
}

/* ---------- the caches' keys */

/* a hash of bytes for the caches' buckets, eight at a time (a byte at a
time was a frame's noticeable part on a phone's or a headset's CPU, for
each draw's pipeline and texture set); only where the entries are looked
for depends on it */
static uint64_t bytes_hash64(const void *data, size_t size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	uint64_t hash = 0x9e3779b97f4a7c15ull ^ size, word;
	size_t index;

	for (index = 0; index + 8 <= size; index += 8)
	{
		memcpy(&word, bytes + index, 8);
		hash = (hash ^ word) * 0xff51afd7ed558ccdull;
		hash ^= hash >> 31;
	}
	for (; index < size; index++)
		hash = (hash ^ bytes[index]) * 0x100000001b3ull;
	hash ^= hash >> 33;
	hash *= 0xc4ceb9fe1a85ec53ull;
	hash ^= hash >> 29;
	return hash;
}

static VkSamplerAddressMode address_mode(uint8_t mode)
{
	switch (mode)
	{
	case XGPU_VULKAN_ADDRESS_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case XGPU_VULKAN_ADDRESS_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case XGPU_VULKAN_ADDRESS_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

VkSampler xgpu_vulkan_sampler(const struct xgpu_vulkan_sampler_description *description)
{
	VkSamplerCreateInfo info;
	VkSamplerCustomBorderColorCreateInfoEXT custom;
	uint32_t hash = (uint32_t)bytes_hash64(description, sizeof(*description)), index, probe;
	VkSampler sampler = VK_NULL_HANDLE;

	for (probe = 0; probe < SAMPLER_CACHE; probe++)
	{
		index = (hash + probe) % SAMPLER_CACHE;
		if (!vk.samplers[index].sampler)
			break;
		if (!memcmp(&vk.samplers[index].description, description, sizeof(*description)))
			return vk.samplers[index].sampler;
	}
	if (probe == SAMPLER_CACHE || vk.sampler_count >= SAMPLER_CACHE * 3 / 4)
	{
		/* (never seen: the game uses a few dozen) */
		return vk.stand_in_sampler;
	}
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	/* (a cube map's faces filtered each alone, as OpenGL's are without
	GL_TEXTURE_CUBE_MAP_SEAMLESS, which OpenCE's OpenGL backend left off) */
	if (vk.non_seamless)
		info.flags = VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT;
	info.magFilter = description->magnify == XGPU_VULKAN_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	info.minFilter = description->minify == XGPU_VULKAN_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	info.mipmapMode = description->mip == XGPU_VULKAN_MIP_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR :
		VK_SAMPLER_MIPMAP_MODE_NEAREST;
	info.addressModeU = address_mode(description->address[0]);
	info.addressModeV = address_mode(description->address[1]);
	info.addressModeW = address_mode(description->address[2]);
	info.mipLodBias = description->lod_bias;
	if (description->anisotropy > 1 && vk.capabilities.anisotropy > 1)
	{
		info.anisotropyEnable = VK_TRUE;
		info.maxAnisotropy = (float)(description->anisotropy < vk.capabilities.anisotropy ? description->anisotropy :
			vk.capabilities.anisotropy);
		if (description->mip != XGPU_VULKAN_MIP_NONE)
			info.mipLodBias += anisotropic_bias(info.maxAnisotropy);
	}
	/* (no mip filter: the first level alone, as OpenGL's minification
	without mipmaps samples its base level whatever its least LOD) */
	if (description->mip == XGPU_VULKAN_MIP_NONE)
	{
		info.minLod = 0.0f;
		info.maxLod = 0.25f;
	}
	else
	{
		info.minLod = description->min_lod;
		info.maxLod = VK_LOD_CLAMP_NONE;
	}
	info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	if (description->border_color == 0xff000000u)
		info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	else if (description->border_color == 0xffffffffu)
		info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	else if (description->border_color && vk.capabilities.custom_border_colors &&
		(description->address[0] == XGPU_VULKAN_ADDRESS_BORDER || description->address[1] == XGPU_VULKAN_ADDRESS_BORDER ||
		description->address[2] == XGPU_VULKAN_ADDRESS_BORDER))
	{
		memset(&custom, 0, sizeof(custom));
		custom.sType = VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT;
		custom.customBorderColor.float32[0] = (float)((description->border_color >> 16) & 0xff) / 255.0f;
		custom.customBorderColor.float32[1] = (float)((description->border_color >> 8) & 0xff) / 255.0f;
		custom.customBorderColor.float32[2] = (float)(description->border_color & 0xff) / 255.0f;
		custom.customBorderColor.float32[3] = (float)(description->border_color >> 24) / 255.0f;
		custom.format = VK_FORMAT_UNDEFINED;
		info.pNext = &custom;
		info.borderColor = VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
	}
	if (!check(vkCreateSampler(vk.device, &info, NULL, &sampler), "vkCreateSampler"))
		return vk.stand_in_sampler;
	vk.samplers[index].description = *description;
	vk.samplers[index].sampler = sampler;
	vk.sampler_count++;
	vk.statistics.samplers_made++;
	return sampler;
}

/* ---------- shaders */

VkShaderModule xgpu_vulkan_shader(const uint32_t *words, size_t count)
{
	uint64_t hash = bytes_hash64(words, count * 4);
	struct shader_entry *entry, **bucket;
	VkShaderModuleCreateInfo info;

	bucket = &vk.shaders[hash % SHADER_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && entry->count == count && !memcmp(entry->words, words, count * 4))
			return entry->module;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return VK_NULL_HANDLE;
	entry->words = malloc(count * 4);
	if (!entry->words)
	{
		free(entry);
		return VK_NULL_HANDLE;
	}
	memcpy(entry->words, words, count * 4);
	entry->count = count;
	entry->hash = hash;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = count * 4;
	info.pCode = words;
	check(vkCreateShaderModule(vk.device, &info, NULL, &entry->module), "vkCreateShaderModule");
	entry->next = *bucket;
	*bucket = entry;
	return entry->module;
}

VkShaderModule xgpu_vulkan_shader_constant(const uint32_t *words, size_t count)
{
	VkShaderModule module;
	uint32_t index;

	for (index = 0; index < vk.constant_shader_count; index++)
	{
		if (vk.constant_shaders[index].words == words && vk.constant_shaders[index].count == count)
			return vk.constant_shaders[index].module;
	}
	module = xgpu_vulkan_shader(words, count);
	/* (more than the table holds are hashed each time, as before) */
	if (module && vk.constant_shader_count < CONSTANT_SHADERS)
	{
		vk.constant_shaders[vk.constant_shader_count].words = words;
		vk.constant_shaders[vk.constant_shader_count].count = count;
		vk.constant_shaders[vk.constant_shader_count].module = module;
		vk.constant_shader_count++;
	}
	return module;
}

/* ---------- pipelines */

static VkCompareOp compare_op(uint8_t function)
{
	static const VkCompareOp operations[8] =
	{
		VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL,
		VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS,
	};

	return operations[function & 7];
}

static VkBlendFactor blend_factor(uint8_t factor)
{
	static const VkBlendFactor factors[] =
	{
		VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
		VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_ALPHA,
		VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
		VK_BLEND_FACTOR_SRC_ALPHA_SATURATE, VK_BLEND_FACTOR_CONSTANT_COLOR, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
		VK_BLEND_FACTOR_CONSTANT_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
	};

	return factor < sizeof(factors) / sizeof(factors[0]) ? factors[factor] : VK_BLEND_FACTOR_ONE;
}

static VkBlendOp blend_op(uint8_t operation)
{
	switch (operation)
	{
	case 1: return VK_BLEND_OP_SUBTRACT;
	case 2: return VK_BLEND_OP_REVERSE_SUBTRACT;
	case 3: return VK_BLEND_OP_MIN;
	case 4: return VK_BLEND_OP_MAX;
	default: return VK_BLEND_OP_ADD;
	}
}

static VkStencilOp stencil_op(uint8_t operation)
{
	static const VkStencilOp operations[8] =
	{
		VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
		VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INCREMENT_AND_WRAP,
		VK_STENCIL_OP_DECREMENT_AND_WRAP,
	};

	return operations[operation & 7];
}

static uint32_t description_hash(const struct xgpu_vulkan_pipeline_description *description)
{
	return (uint32_t)bytes_hash64(description, sizeof(*description));
}

/* Preparation queues owned native descriptions. Shader modules, the device
and pipeline layout live until shutdown; no job borrows guest/map memory.
Only the render owner publishes handles and statistics. A full queue, a
requested handle and PREPARED wait with loading presentation still serviced. */
/* Enough lookahead for a map's usual variants without blocking the producer
behind each short burst. Jobs contain descriptions, never vertex/texture data. */
#define PIPELINE_JOBS 512
/* Compilers, each the driver's on one processor: one left the Steam Frame's
eight cores idle while it made a map's 442 pipelines in 2 s, the game
waiting 0.9 s of them. The game's and the render thread, and the VR
runtime's, keep theirs. (A pipeline cache needs no lock of ours: Vulkan
synchronizes one not made externally synchronized.) */
#define PIPELINE_WORKERS_MOST 4
static VkPipeline pipeline_build(const struct xgpu_vulkan_pipeline_description *description,
	VkResult *result, double *elapsed);
static struct
{
	pthread_t threads[PIPELINE_WORKERS_MOST];
	pthread_mutex_t mutex;
	pthread_cond_t changed;
	int started, stop;
	uint64_t submitted, claimed, published;
	struct
	{
		struct xgpu_vulkan_pipeline_description description;
		struct pipeline_entry *entry;
		VkPipeline pipeline;
		VkResult result;
		double elapsed;
		/* (made: the workers finish out of order, published in order) */
		int done;
	} jobs[PIPELINE_JOBS];
	void (*service)(void);
} pipeline_compiler = { .mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER };

static void *pipeline_compile_worker(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&pipeline_compiler.mutex);
	for (;;)
	{
		uint32_t index;

		while (pipeline_compiler.claimed == pipeline_compiler.submitted && !pipeline_compiler.stop)
			pthread_cond_wait(&pipeline_compiler.changed, &pipeline_compiler.mutex);
		if (pipeline_compiler.stop)
			break;
		index = (uint32_t)(pipeline_compiler.claimed++ % PIPELINE_JOBS);
		pthread_mutex_unlock(&pipeline_compiler.mutex);
		pipeline_compiler.jobs[index].pipeline = pipeline_build(&pipeline_compiler.jobs[index].description,
			&pipeline_compiler.jobs[index].result, &pipeline_compiler.jobs[index].elapsed);
		pthread_mutex_lock(&pipeline_compiler.mutex);
		pipeline_compiler.jobs[index].done = 1;
		pthread_cond_broadcast(&pipeline_compiler.changed);
	}
	pthread_mutex_unlock(&pipeline_compiler.mutex);
	return NULL;
}

static void pipeline_publish(struct pipeline_entry *entry, VkPipeline pipeline, VkResult result, double elapsed)
{
	entry->pipeline = check(result, "vkCreateGraphicsPipelines") ? pipeline : VK_NULL_HANDLE;
	entry->pending = 0;
	vk.pipelines_made++;
	vk.statistics.pipelines_made++;
	/* Aggregate driver call wall time, not preparation latency: compilation
	overlaps enumeration, so adding it to enumeration time double counts. */
	vk.statistics.pipeline_seconds += elapsed;
	host_load_trace("vulkan.pipeline.published", "hash=%08x driver_seconds=%.6f", entry->hash, elapsed);
}

/* Called with the mutex held, only by the render owner. */
static void pipeline_publish_completed(void)
{
	while (pipeline_compiler.published < pipeline_compiler.submitted &&
		pipeline_compiler.jobs[pipeline_compiler.published % PIPELINE_JOBS].done)
	{
		uint32_t index = (uint32_t)(pipeline_compiler.published++ % PIPELINE_JOBS);

		pipeline_compiler.jobs[index].done = 0;
		pipeline_publish(pipeline_compiler.jobs[index].entry, pipeline_compiler.jobs[index].pipeline,
			pipeline_compiler.jobs[index].result, pipeline_compiler.jobs[index].elapsed);
	}
}

static void pipeline_wait(struct pipeline_entry *entry, int room)
{
	pthread_mutex_lock(&pipeline_compiler.mutex);
	for (;;)
	{
		struct timespec deadline;

		pipeline_publish_completed();
		if (entry ? !entry->pending : room ?
			pipeline_compiler.submitted - pipeline_compiler.published < PIPELINE_JOBS :
			pipeline_compiler.submitted == pipeline_compiler.published)
			break;
		if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
		{
			pthread_cond_wait(&pipeline_compiler.changed, &pipeline_compiler.mutex);
			continue;
		}
		deadline.tv_nsec += 1000000;
		if (deadline.tv_nsec >= 1000000000)
		{
			deadline.tv_sec++;
			deadline.tv_nsec -= 1000000000;
		}
		pthread_cond_timedwait(&pipeline_compiler.changed, &pipeline_compiler.mutex, &deadline);
		pipeline_publish_completed();
		if (pipeline_compiler.service && (entry ? entry->pending : room ?
			pipeline_compiler.submitted - pipeline_compiler.published >= PIPELINE_JOBS :
			pipeline_compiler.submitted != pipeline_compiler.published))
		{
			pthread_mutex_unlock(&pipeline_compiler.mutex);
			pipeline_compiler.service();
			pthread_mutex_lock(&pipeline_compiler.mutex);
		}
	}
	pthread_mutex_unlock(&pipeline_compiler.mutex);
}

void xgpu_vulkan_preparation_service(void (*service)(void))
{
	if (!service)
		pipeline_wait(NULL, 0);
	pipeline_compiler.service = service;
}

/* the compilers started (where they are not yet) */
static void pipeline_compilers_start(void)
{
	if (!pipeline_compiler.started)
	{
		long processors = sysconf(_SC_NPROCESSORS_ONLN);
		int workers = processors > 4 ? (int)processors - 4 : 1;

		if (workers > PIPELINE_WORKERS_MOST)
			workers = PIPELINE_WORKERS_MOST;
		pipeline_compiler.stop = 0;
		while (pipeline_compiler.started < workers &&
			pthread_create(&pipeline_compiler.threads[pipeline_compiler.started], NULL, pipeline_compile_worker,
				NULL) == 0)
			pipeline_compiler.started++;
		host_logf(HOST_LOG_INFO, "vulkan: %d pipeline compilers", pipeline_compiler.started);
	}
}

static void pipeline_request(struct pipeline_entry *entry)
{
	VkResult result;
	VkPipeline pipeline;
	double elapsed;
	uint32_t index;

	vk.pipeline_count++;
	host_load_trace("vulkan.pipeline.request", "count=%llu hash=%08x",
		(unsigned long long)vk.pipeline_count, entry->hash);
	if (pipeline_compiler.service)
		pipeline_compilers_start();
	if (!pipeline_compiler.service || !pipeline_compiler.started)
	{
		pipeline = pipeline_build(&entry->description, &result, &elapsed);
		pipeline_publish(entry, pipeline, result, elapsed);
		return;
	}
	pipeline_wait(NULL, 1);
	pthread_mutex_lock(&pipeline_compiler.mutex);
	index = (uint32_t)(pipeline_compiler.submitted % PIPELINE_JOBS);
	pipeline_compiler.jobs[index].description = entry->description;
	pipeline_compiler.jobs[index].entry = entry;
	entry->pending = 1;
	pipeline_compiler.submitted++;
	pthread_cond_broadcast(&pipeline_compiler.changed);
	pthread_mutex_unlock(&pipeline_compiler.mutex);
}

/* a pipeline handed to the compilers without waiting, where there is room
(else it waits for the next look); FALSE if there are none */
static int pipeline_request_apart(struct pipeline_entry *entry)
{
	uint32_t index;

	pipeline_compilers_start();
	if (!pipeline_compiler.started)
		return 0;
	pthread_mutex_lock(&pipeline_compiler.mutex);
	pipeline_publish_completed();
	entry->waiting = pipeline_compiler.submitted - pipeline_compiler.published >= PIPELINE_JOBS;
	if (!entry->waiting)
	{
		index = (uint32_t)(pipeline_compiler.submitted % PIPELINE_JOBS);
		pipeline_compiler.jobs[index].description = entry->description;
		pipeline_compiler.jobs[index].entry = entry;
		entry->pending = 1;
		pipeline_compiler.submitted++;
		pthread_cond_broadcast(&pipeline_compiler.changed);
	}
	pthread_mutex_unlock(&pipeline_compiler.mutex);
	return 1;
}

static void pipeline_compiler_stop(void)
{
	if (!pipeline_compiler.started)
		return;
	pipeline_wait(NULL, 0);
	pthread_mutex_lock(&pipeline_compiler.mutex);
	pipeline_compiler.stop = 1;
	pthread_cond_broadcast(&pipeline_compiler.changed);
	pthread_mutex_unlock(&pipeline_compiler.mutex);
	while (pipeline_compiler.started)
		pthread_join(pipeline_compiler.threads[--pipeline_compiler.started], NULL);
}

/* The stream has 16 vertex sources plus constant attributes, but at most
16 attributes use them. Pack active sources into Vulkan's 16 guaranteed
bindings; the logical constant source (16) is not a valid device binding
on GPUs exposing that minimum. Pipelines and draws use the same mapping. */
static uint32_t vertex_binding(uint32_t used, uint32_t source)
{
	uint32_t binding = 0;

	used &= (1u << source) - 1;
	while (used)
	{
		used &= used - 1;
		binding++;
	}
	return binding;
}

static VkPipeline pipeline_build(const struct xgpu_vulkan_pipeline_description *description,
	VkResult *result, double *elapsed)
{
	VkPipelineShaderStageCreateInfo stages[2];
	VkVertexInputBindingDescription bindings[XGPU_VULKAN_BINDINGS];
	VkVertexInputAttributeDescription attributes[16];
	VkPipelineVertexInputStateCreateInfo vertex_input;
	VkPipelineInputAssemblyStateCreateInfo assembly;
	VkPipelineViewportStateCreateInfo viewport;
	VkPipelineRasterizationStateCreateInfo raster;
	VkPipelineMultisampleStateCreateInfo multisample;
	VkPipelineDepthStencilStateCreateInfo depth_stencil;
	VkPipelineColorBlendAttachmentState blend_attachment;
	VkPipelineColorBlendStateCreateInfo blend;
	VkPipelineDynamicStateCreateInfo dynamic;
	VkPipelineRenderingCreateInfo rendering;
	VkGraphicsPipelineCreateInfo info;
	VkFormat color_format = vulkan_format(description->color_format);
	VkFormat depth_format = description->depth_format ? vk.depth_format : VK_FORMAT_UNDEFINED;
	static const VkDynamicState dynamic_states[] =
	{
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS, VK_DYNAMIC_STATE_BLEND_CONSTANTS,
		VK_DYNAMIC_STATE_STENCIL_REFERENCE, VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
	};
	uint32_t binding_count = 0, attribute_count = 0, index, used = 0;
	VkPipeline pipeline = VK_NULL_HANDLE;
	double started;

	memset(stages, 0, sizeof(stages));
	stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = description->vertex_shader;
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = description->pixel_shader;
	stages[1].pName = "main";

	for (index = 0; index < 16; index++)
	{
		const struct xgpu_vulkan_attribute *attribute = &description->attributes[index];

		if (attribute->format == VK_FORMAT_UNDEFINED)
			continue;
		attributes[attribute_count].location = index;
		attributes[attribute_count].binding = attribute->binding;
		attributes[attribute_count].format = (VkFormat)attribute->format;
		attributes[attribute_count].offset = attribute->offset;
		attribute_count++;
		used |= 1u << attribute->binding;
	}
	for (index = 0; index < XGPU_VULKAN_BINDINGS; index++)
	{
		if (!(used & (1u << index)))
			continue;
		bindings[binding_count].binding = vertex_binding(used, index);
		bindings[binding_count].stride = description->strides[index];
		bindings[binding_count].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		binding_count++;
	}
	for (index = 0; index < attribute_count; index++)
		attributes[index].binding = vertex_binding(used, attributes[index].binding);
	memset(&vertex_input, 0, sizeof(vertex_input));
	vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input.vertexBindingDescriptionCount = binding_count;
	vertex_input.pVertexBindingDescriptions = bindings;
	vertex_input.vertexAttributeDescriptionCount = attribute_count;
	vertex_input.pVertexAttributeDescriptions = attributes;

	memset(&assembly, 0, sizeof(assembly));
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = description->primitive_class == XGPU_VULKAN_CLASS_POINTS ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST :
		description->primitive_class == XGPU_VULKAN_CLASS_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST :
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	memset(&viewport, 0, sizeof(viewport));
	viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport.viewportCount = 1;
	viewport.scissorCount = 1;

	memset(&raster, 0, sizeof(raster));
	raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	raster.depthClampEnable = description->depth_clamp && vk.capabilities.depth_clamp;
	raster.polygonMode = description->fill == 1 ? VK_POLYGON_MODE_LINE : description->fill == 2 ? VK_POLYGON_MODE_POINT :
		VK_POLYGON_MODE_FILL;
	raster.cullMode = !description->cull ? VK_CULL_MODE_NONE : description->cull_front ? VK_CULL_MODE_FRONT_BIT :
		VK_CULL_MODE_BACK_BIT;
	raster.frontFace = description->front_counter_clockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
	raster.depthBiasEnable = description->depth_bias ? VK_TRUE : VK_FALSE;
	raster.lineWidth = 1.0f;

	memset(&multisample, 0, sizeof(multisample));
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = description->samples > 1 ? (VkSampleCountFlagBits)description->samples :
		VK_SAMPLE_COUNT_1_BIT;

	memset(&depth_stencil, 0, sizeof(depth_stencil));
	depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	if (depth_format != VK_FORMAT_UNDEFINED)
	{
		/* (depth is written only where it is tested, as OpenGL has it) */
		depth_stencil.depthTestEnable = description->depth_test ? VK_TRUE : VK_FALSE;
		depth_stencil.depthWriteEnable = description->depth_test && description->depth_write ? VK_TRUE : VK_FALSE;
		depth_stencil.depthCompareOp = compare_op(XGPU_VULKAN_REVERSED_DEPTH ?
			xgpu_reversed_depth_function(description->depth_function) : description->depth_function);
		depth_stencil.stencilTestEnable = description->stencil_test ? VK_TRUE : VK_FALSE;
		depth_stencil.front.failOp = stencil_op(description->stencil_fail);
		depth_stencil.front.passOp = stencil_op(description->stencil_pass);
		depth_stencil.front.depthFailOp = stencil_op(description->stencil_depth_fail);
		depth_stencil.front.compareOp = compare_op(description->stencil_function);
		depth_stencil.front.compareMask = description->stencil_read_mask;
		depth_stencil.front.writeMask = description->stencil_write_mask;
		depth_stencil.back = depth_stencil.front;
	}
	depth_stencil.minDepthBounds = 0.0f;
	depth_stencil.maxDepthBounds = 1.0f;

	memset(&blend_attachment, 0, sizeof(blend_attachment));
	/* (display.hdr: a pixel shader that reads the pixel it draws over
	blends with it itself) */
	blend_attachment.blendEnable = description->blend && !description->color_read ? VK_TRUE : VK_FALSE;
	blend_attachment.srcColorBlendFactor = blend_attachment.srcAlphaBlendFactor = blend_factor(description->blend_source);
	blend_attachment.dstColorBlendFactor = blend_attachment.dstAlphaBlendFactor = blend_factor(description->blend_destination);
	blend_attachment.colorBlendOp = blend_attachment.alphaBlendOp = blend_op(description->blend_operation);
	blend_attachment.colorWriteMask = description->color_store ? 0 : description->color_mask & 15;
	memset(&blend, 0, sizeof(blend));
	blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blend.attachmentCount = color_format != VK_FORMAT_UNDEFINED ? 1 : 0;
	blend.pAttachments = &blend_attachment;
	if (description->color_read && vk.capabilities.raster_order)
		blend.flags = VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT;

	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]);
	dynamic.pDynamicStates = dynamic_states;

	memset(&rendering, 0, sizeof(rendering));
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.viewMask = description->view_mask;
	rendering.colorAttachmentCount = color_format != VK_FORMAT_UNDEFINED ? 1 : 0;
	rendering.pColorAttachmentFormats = &color_format;
	rendering.depthAttachmentFormat = depth_format;
	rendering.stencilAttachmentFormat = depth_format;

	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.pNext = &rendering;
	info.stageCount = 2;
	info.pStages = stages;
	info.pVertexInputState = &vertex_input;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewport;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depth_stencil;
	info.pColorBlendState = &blend;
	info.pDynamicState = &dynamic;
	info.layout = vk.pipeline_layout;
	if (description->vertex_shader && description->pixel_shader)
	{
		host_load_trace("vulkan.pipeline.driver.begin", "hash=%08x", description_hash(description));
		started = seconds_now();
		*result = vkCreateGraphicsPipelines(vk.device, vk.pipeline_cache, 1, &info, NULL, &pipeline);
		*elapsed = seconds_now() - started;
		host_load_trace("vulkan.pipeline.driver.end", "hash=%08x result=%d", description_hash(description), *result);
	}
	else
	{
		*result = VK_ERROR_INITIALIZATION_FAILED;
		*elapsed = 0.0;
	}
	return pipeline;
}

static struct pipeline_entry *pipeline_find_apart(const struct xgpu_vulkan_pipeline_description *description,
	int apart)
{
	static struct pipeline_entry *last;
	uint32_t hash;
	struct pipeline_entry **bucket, *entry;

	if (last && !memcmp(&last->description, description, sizeof(*description)))
		return last;
	hash = description_hash(description);
	bucket = &vk.pipelines[hash % PIPELINE_BUCKETS];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->hash == hash && !memcmp(&entry->description, description, sizeof(*description)))
		{
			last = entry;
			return entry;
		}
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return NULL;
	entry->hash = hash;
	entry->description = *description;
	entry->next = *bucket;
	*bucket = entry;
	last = entry;
	if (!apart || !pipeline_request_apart(entry))
		pipeline_request(entry);
	else
		/* (counted as pipeline_request counts it: a miss in play, the
		stream's pipeline_missed) */
		vk.pipeline_count++;
	return entry;
}

static struct pipeline_entry *pipeline_find(const struct xgpu_vulkan_pipeline_description *description)
{
	return pipeline_find_apart(description, 0);
}

int xgpu_vulkan_pipeline_prepare(const struct xgpu_vulkan_pipeline_description *description)
{
	struct pipeline_entry *entry = pipeline_find(description);

	return entry && (entry->pending || entry->pipeline != VK_NULL_HANDLE);
}

VkPipeline xgpu_vulkan_pipeline(const struct xgpu_vulkan_pipeline_description *description)
{
	struct pipeline_entry *entry = pipeline_find(description);

	if (!entry)
		return VK_NULL_HANDLE;
	if (entry->pending)
		pipeline_wait(entry, 0);
	return entry->pipeline;
}

VkPipeline xgpu_vulkan_pipeline_ready(const struct xgpu_vulkan_pipeline_description *description)
{
	struct pipeline_entry *entry = pipeline_find_apart(description, 1);

	if (!entry)
		return VK_NULL_HANDLE;
	if (entry->waiting && !pipeline_request_apart(entry))
		pipeline_request(entry);
	if (entry->pending)
	{
		pthread_mutex_lock(&pipeline_compiler.mutex);
		pipeline_publish_completed();
		pthread_mutex_unlock(&pipeline_compiler.mutex);
	}
	return entry->pending || entry->waiting ? VK_NULL_HANDLE : entry->pipeline;
}

uint64_t xgpu_vulkan_pipeline_count(void)
{
	return vk.pipeline_count;
}

/* ---------- passes */

static VkImageView attachment_view(struct xgpu_vulkan_image *image, uint32_t layer, uint32_t views)
{
	uint32_t layers = image->description.type == XGPU_VULKAN_IMAGE_2D_ARRAY ? image->description.depth_or_layers : 1;
	uint32_t index, count = layers * 4;

	if (!views || views > 4 || layer >= layers || views > layers - layer)
		return VK_NULL_HANDLE;
	index = layer * 4 + views - 1;
	if (count > image->attachment_count)
	{
		VkImageView *grown = realloc(image->attachments, count * sizeof(*grown));

		if (!grown)
			return VK_NULL_HANDLE;
		memset(grown + image->attachment_count, 0, (count - image->attachment_count) * sizeof(*grown));
		image->attachments = grown;
		image->attachment_count = count;
	}
	if (!image->attachments[index])
		image->attachments[index] = view_make(image, views > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D,
			layer, views, 0);
	return image->attachments[index];
}

/* an attachment of a pass over this area is the whole of its image: all of
it, each of its layers */
static int attachment_whole(const struct xgpu_vulkan_image *image, const VkRect2D *area)
{
	uint32_t layers = image->description.type == XGPU_VULKAN_IMAGE_2D_ARRAY && image->description.depth_or_layers ?
		image->description.depth_or_layers : 1;

	return !area->offset.x && !area->offset.y && area->extent.width == image->description.width &&
		area->extent.height == image->description.height && !vk.layer && vk.views >= layers;
}

/* With discards: the pass begun as a segment of its own, its draws
recorded into a secondary command buffer that inherits its formats, its
rendering made with its stores at the submission (segments_play) */
static void pass_segment_begin(const VkRenderingInfo *rendering)
{
	struct segment *segment = segment_add();
	uint32_t index = vk.segment_count - 1;
	VkCommandBufferInheritanceRenderingInfo inheritance;
	VkFormat color_format = VK_FORMAT_UNDEFINED;
	struct xgpu_vulkan_image *sized = vk.color ? vk.color : vk.depth;

	segment->pass = 1;
	segment->area = rendering->renderArea;
	segment->view_mask = rendering->viewMask;
	segment->layer = vk.layer;
	segment->views = vk.views;
	memset(&inheritance, 0, sizeof(inheritance));
	inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
	inheritance.viewMask = rendering->viewMask;
	inheritance.rasterizationSamples = sized->description.samples > 1 ?
		(VkSampleCountFlagBits)sized->description.samples : VK_SAMPLE_COUNT_1_BIT;
	if (rendering->colorAttachmentCount)
	{
		const VkRenderingAttachmentInfo *color = &rendering->pColorAttachments[0];

		segment->color.image = vk.color;
		segment->color.info = *color;
		color_format = vk.color->format;
		inheritance.colorAttachmentCount = 1;
		inheritance.pColorAttachmentFormats = &color_format;
		attachment_use(index, 0, vk.color, 1,
			color->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && attachment_whole(vk.color, &rendering->renderArea));
	}
	if (rendering->pDepthAttachment)
	{
		int whole = attachment_whole(vk.depth, &rendering->renderArea);

		segment->depth.image = vk.depth;
		segment->depth.info = *rendering->pDepthAttachment;
		segment->depth.stencil_load = rendering->pStencilAttachment->loadOp;
		inheritance.depthAttachmentFormat = inheritance.stencilAttachmentFormat = vk.depth->format;
		attachment_use(index, 1, vk.depth, 3, !whole ? 0 :
			(rendering->pDepthAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) |
			(rendering->pStencilAttachment->loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) << 1);
	}
	aside_pass(index, segment);
	segment->commands = secondary_begin(&inheritance);
	vk.pass_commands = segment->commands;
	/* (a secondary command buffer begins with no state bound) */
	vk.bound_pipeline = VK_NULL_HANDLE;
	memset(&vk.bound, 0, sizeof(vk.bound));
}

/* the pass drawing into the bound targets, begun */
static int pass_begin(uint32_t clear_parts, const float clear_color[4], float clear_depth, uint32_t clear_stencil)
{
	VkRenderingAttachmentInfo color, depth, stencil;
	VkRenderingInfo rendering;
	struct xgpu_vulkan_image *size_of = vk.color ? vk.color : vk.depth;

	if (vk.pass_active)
		return 1;
	if (!size_of)
		return 0;
	recording_begin();
	memset(&rendering, 0, sizeof(rendering));
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea.extent.width = size_of->description.width;
	rendering.renderArea.extent.height = size_of->description.height;
	rendering.layerCount = 1;
	rendering.viewMask = vk.views > 1 ? (1u << vk.views) - 1 : 0;
	if (vk.color)
	{
		memset(&color, 0, sizeof(color));
		color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		color.imageView = attachment_view(vk.color, vk.layer, vk.views);
		color.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (!color.imageView)
			return 0;
		color.loadOp = (clear_parts & XGPU_VULKAN_CLEAR_COLOR) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		if (clear_parts & XGPU_VULKAN_CLEAR_COLOR)
			memcpy(color.clearValue.color.float32, clear_color, sizeof(color.clearValue.color.float32));
		color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		rendering.colorAttachmentCount = 1;
		rendering.pColorAttachments = &color;
		vk.color->used = vk.serial;
		if (vk.depth && (vk.depth->description.width < rendering.renderArea.extent.width ||
			vk.depth->description.height < rendering.renderArea.extent.height))
		{
			rendering.renderArea.extent.width = vk.depth->description.width < rendering.renderArea.extent.width ?
				vk.depth->description.width : rendering.renderArea.extent.width;
			rendering.renderArea.extent.height = vk.depth->description.height < rendering.renderArea.extent.height ?
				vk.depth->description.height : rendering.renderArea.extent.height;
		}
	}
	if (vk.depth)
	{
		memset(&depth, 0, sizeof(depth));
		depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth.imageView = attachment_view(vk.depth, vk.layer, vk.views);
		depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		if (!depth.imageView)
			return 0;
		depth.loadOp = (clear_parts & XGPU_VULKAN_CLEAR_DEPTH) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		depth.clearValue.depthStencil.depth = clear_depth;
		depth.clearValue.depthStencil.stencil = clear_stencil & 0xff;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		/* (a pass that samples its depth writes none of it: nothing to
		store, which a tiler would write back to memory again) */
		if (vk.pass_reads_depth)
			depth.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
		stencil = depth;
		stencil.loadOp = (clear_parts & XGPU_VULKAN_CLEAR_STENCIL) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		rendering.pDepthAttachment = &depth;
		rendering.pStencilAttachment = &stencil;
		vk.depth->used = vk.serial;
	}
	if (vk.color)
	{
		vk.statistics.attachment_clears += color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
		vk.statistics.attachment_loads += color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	if (vk.depth)
	{
		vk.statistics.attachment_clears += (depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) +
			(stencil.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		vk.statistics.attachment_loads += (depth.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD) +
			(stencil.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	}
	if (vk.deferred)
		pass_segment_begin(&rendering);
	else
	{
		/* (the pass's state as a segment's, for its timestamp's label; the
		state bound before it stays bound in the main command buffer) */
		direct_outside_mark();
		memset(&vk.direct_pass, 0, sizeof(vk.direct_pass));
		vk.direct_pass.pass = 1;
		vk.direct_pass.area = rendering.renderArea;
		vk.direct_pass.color.image = rendering.colorAttachmentCount ? vk.color : NULL;
		vk.direct_pass.depth.image = rendering.pDepthAttachment ? vk.depth : NULL;
		vkCmdBeginRendering(vk.slot->main, &rendering);
		vk.pass_commands = vk.slot->main;
	}
	vk.pass_active = 1;
	vk.pass_width = rendering.renderArea.extent.width;
	vk.pass_height = rendering.renderArea.extent.height;
	vk.statistics.passes++;
	if (vk.test_active)
		test_query_begin();
	return 1;
}

/* display.hdr: the writes of the interlocked draws drawn since the last
barrier (their storage image's) seen by what comes after them in the pass,
as the colour attachment's writes, and by what follows the pass as its
writes are: one barrier after a run of interlocked draws, and none between
them (draw_body) */
static void color_stores_seen(VkCommandBuffer commands)
{
	VkMemoryBarrier stored;

	if (!vk.color_stored)
		return;
	memset(&stored, 0, sizeof(stored));
	stored.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	stored.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	stored.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1, &stored, 0, NULL, 0, NULL);
	vk.color_stored = 0;
}

static void pass_end_body(void)
{
	if (!vk.pass_active)
		return;
	color_stores_seen(vk.pass_commands);
	test_query_end();
	/* (with discards, the pass's draws ended: its rendering, and the
	barrier after it, are made at the submission, segments_play) */
	if (vk.deferred)
		check(vkEndCommandBuffer(vk.pass_commands), "vkEndCommandBuffer (pass)");
	else
	{
		vkCmdEndRendering(vk.slot->main);
		pass_barrier(vk.slot->main);
		pass_times_mark(vk.slot->main, pass_times_label(&vk.direct_pass, 0));
		timing_mark(vk.slot->main, &vk.direct_pass, 0, 0);
	}
	vk.pass_active = 0;
	vk.pass_reads_depth = 0;
}

static void pass_end_from(const char *caller)
{
	if (vk.pass_active && !vk.deferred)
		vk.direct_pass.ended_by = caller;
	else if (vk.pass_active && vk.segment_count && vk.segments[vk.segment_count - 1].pass)
		vk.segments[vk.segment_count - 1].ended_by = caller;
	pass_end_body();
}

void xgpu_vulkan_set_target_views(struct xgpu_vulkan_image *color, struct xgpu_vulkan_image *depth,
	uint32_t layer, uint32_t views)
{
	if (views != 2 && views != 4)
		views = 1;
	if (color == vk.color && depth == vk.depth && layer == vk.layer && views == vk.views)
		return;
	pass_end();
	vk.color = color;
	vk.depth = depth;
	vk.layer = layer;
	vk.views = views;
}

void xgpu_vulkan_set_targets(struct xgpu_vulkan_image *color, struct xgpu_vulkan_image *depth, uint32_t layer)
{
	xgpu_vulkan_set_target_views(color, depth, layer, 1);
}

/* the draw set's constants' range (draw_set_make), which the uniforms' fits
in: one range of the ring for both, as the anti-aliasing passes have it */
#define CLEAR_UNIFORMS_RANGE (192 * 16)

/* the pipeline of a clear of some of the bound colour target's channels
(XGPU_VULKAN_CLEAR_RED to _ALPHA), which vkCmdClearAttachments cannot do (it
writes every channel): a triangle over the viewport (the anti-aliasing
passes' vertex shader) writing the uniforms' colour through the channels'
write mask, as OpenGL's glClear through glColorMask, with no depth or stencil
test or write, for the bound targets' formats, samples and views; 0 if
there is none */
static int clear_description(uint32_t channels, struct xgpu_vulkan_pipeline_description *description)
{
	if (!vk.color || !(channels & XGPU_VULKAN_CLEAR_CHANNELS))
		return 0;
	if (!vk.clear_vertex)
		vk.clear_vertex = xgpu_vulkan_shader(xgpu_vulkan_post_vertex.words, xgpu_vulkan_post_vertex.count);
	if (!vk.clear_pixel)
		vk.clear_pixel = xgpu_vulkan_shader(xgpu_vulkan_post_clear.words, xgpu_vulkan_post_clear.count);
	if (!vk.clear_vertex || !vk.clear_pixel)
		return 0;
	memset(description, 0, sizeof(*description));
	description->vertex_shader = vk.clear_vertex;
	description->pixel_shader = vk.clear_pixel;
	description->primitive_class = XGPU_VULKAN_CLASS_TRIANGLES;
	description->color_mask = (uint8_t)(channels & XGPU_VULKAN_CLEAR_CHANNELS);
	description->color_format = vk.color->description.format;
	description->depth_format = vk.depth ? XGPU_VULKAN_FORMAT_D24S8 : XGPU_VULKAN_FORMAT_NONE;
	description->samples = vk.color->description.samples > 1 ? vk.color->description.samples : 1;
	description->view_mask = vk.views > 1 ? (uint8_t)((1u << vk.views) - 1) : 0;
	return 1;
}

int xgpu_vulkan_clear_prepare(uint32_t parts)
{
	struct xgpu_vulkan_pipeline_description description;

	if (!(parts & XGPU_VULKAN_CLEAR_COLOR) || (parts & XGPU_VULKAN_CLEAR_CHANNELS) == XGPU_VULKAN_CLEAR_CHANNELS ||
		!clear_description(parts, &description))
		return 1;
	return xgpu_vulkan_pipeline_prepare(&description);
}

void xgpu_vulkan_clear(const int32_t (*rectangles)[4], uint32_t count, uint32_t parts, const float color[4], float depth,
	uint32_t stencil)
{
	VkClearAttachment attachments[2];
	VkClearRect *rects;
	uint32_t attachment_count = 0, rect_count = 0, index, clear_parts = 0;
	struct xgpu_vulkan_pipeline_description description;
	struct xgpu_vulkan_draw draw;

	if (!count || !rectangles || (!vk.color && !vk.depth))
		return;
	if (XGPU_VULKAN_REVERSED_DEPTH)
		depth = 1.0f - depth;
	/* (depth or stencil cleared: not in a pass that samples the depth) */
	if (vk.pass_reads_depth && (parts & (XGPU_VULKAN_CLEAR_DEPTH | XGPU_VULKAN_CLEAR_STENCIL)))
		pass_end();
	/* Some of the colour's channels alone are drawn: the pipeline and its
	uniforms first, before the pass, as uploading may submit the recording
	(and end a pass). */
	memset(&draw, 0, sizeof(draw));
	if ((parts & XGPU_VULKAN_CLEAR_COLOR) && (parts & XGPU_VULKAN_CLEAR_CHANNELS) != XGPU_VULKAN_CLEAR_CHANNELS &&
		clear_description(parts, &description))
	{
		float *uniforms;

		draw.pipeline = xgpu_vulkan_pipeline(&description);
		uniforms = draw.pipeline ? xgpu_vulkan_upload(CLEAR_UNIFORMS_RANGE, 256, &draw.uniforms) : NULL;
		if (!uniforms)
			draw.pipeline = VK_NULL_HANDLE;
		else
			memcpy(uniforms, color, 4 * sizeof(float));
		draw.constants = draw.uniforms;
	}
	/* A full-target clear before a pass needs none of the overwritten
	attachment's pixels. Keep other aspects, and all stores: later passes
	may sample the depth or continue drawing over the colour. */
	if (!vk.pass_active)
	{
		struct xgpu_vulkan_image *target = vk.color ? vk.color : vk.depth;
		uint32_t width = target->description.width, height = target->description.height;

		if (vk.depth && vk.depth->description.width < width)
			width = vk.depth->description.width;
		if (vk.depth && vk.depth->description.height < height)
			height = vk.depth->description.height;
		for (index = 0; index < count; index++)
		{
			int64_t right = (int64_t)rectangles[index][0] + rectangles[index][2];
			int64_t bottom = (int64_t)rectangles[index][1] + rectangles[index][3];

			if (rectangles[index][0] <= 0 && rectangles[index][1] <= 0 && right >= width && bottom >= height)
			{
				if (vk.color && (parts & XGPU_VULKAN_CLEAR_COLOR) &&
					(parts & XGPU_VULKAN_CLEAR_CHANNELS) == XGPU_VULKAN_CLEAR_CHANNELS)
					clear_parts |= XGPU_VULKAN_CLEAR_COLOR;
				if (vk.depth)
					clear_parts |= parts & (XGPU_VULKAN_CLEAR_DEPTH | XGPU_VULKAN_CLEAR_STENCIL);
				break;
			}
		}
	}
	if (!pass_begin(clear_parts, color, depth, stencil))
		return;
	vk.statistics.clears++;
	parts &= ~clear_parts;
	if ((parts & XGPU_VULKAN_CLEAR_COLOR) && vk.color)
	{
		/* (all of them: cleared, below; some: drawn, after) */
		if ((parts & XGPU_VULKAN_CLEAR_CHANNELS) == XGPU_VULKAN_CLEAR_CHANNELS)
		{
			attachments[attachment_count].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			attachments[attachment_count].colorAttachment = 0;
			memcpy(attachments[attachment_count].clearValue.color.float32, color, 4 * sizeof(float));
			attachment_count++;
		}
	}
	if ((parts & (XGPU_VULKAN_CLEAR_DEPTH | XGPU_VULKAN_CLEAR_STENCIL)) && vk.depth)
	{
		attachments[attachment_count].aspectMask = ((parts & XGPU_VULKAN_CLEAR_DEPTH) ? VK_IMAGE_ASPECT_DEPTH_BIT : 0) |
			((parts & XGPU_VULKAN_CLEAR_STENCIL) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0);
		attachments[attachment_count].colorAttachment = 0;
		attachments[attachment_count].clearValue.depthStencil.depth = depth;
		attachments[attachment_count].clearValue.depthStencil.stencil = stencil & 0xff;
		attachment_count++;
	}
	if (!attachment_count && !draw.pipeline)
		return;
	rects = malloc(count * sizeof(*rects));
	if (!rects)
		return;
	for (index = 0; index < count; index++)
	{
		int32_t left = rectangles[index][0], top = rectangles[index][1];
		int64_t right = (int64_t)left + rectangles[index][2], bottom = (int64_t)top + rectangles[index][3];

		/* (inside the targets) */
		if (left < 0)
			left = 0;
		if (top < 0)
			top = 0;
		if (right > (int32_t)vk.pass_width)
			right = (int32_t)vk.pass_width;
		if (bottom > (int32_t)vk.pass_height)
			bottom = (int32_t)vk.pass_height;
		if (right <= left || bottom <= top)
			continue;
		rects[rect_count].rect.offset.x = left;
		rects[rect_count].rect.offset.y = top;
		rects[rect_count].rect.extent.width = (uint32_t)(right - left);
		rects[rect_count].rect.extent.height = (uint32_t)(bottom - top);
		rects[rect_count].baseArrayLayer = 0;
		rects[rect_count].layerCount = 1;
		rect_count++;
	}
	if (rect_count && attachment_count)
	{
		color_stores_seen(vk.pass_commands);
		vkCmdClearAttachments(vk.pass_commands, attachment_count, attachments, rect_count, rects);
	}
	if (rect_count && draw.pipeline)
	{
		struct xgpu_vulkan_dynamic_state dynamic;
		int counting = vk.test_query_open;

		/* the channels' triangle over each rectangle (into each view, as
		the clear's), counted by no visibility test, as a clear is not */
		test_query_end();
		memset(&dynamic, 0, sizeof(dynamic));
		dynamic.depth_range[1] = 1.0f;
		draw.dynamic = &dynamic;
		draw.topology = XGPU_VULKAN_TOPOLOGY_TRIANGLES;
		draw.count = 3;
		for (index = 0; index < rect_count; index++)
		{
			dynamic.viewport[0] = dynamic.scissor[0] = rects[index].rect.offset.x;
			dynamic.viewport[1] = dynamic.scissor[1] = rects[index].rect.offset.y;
			dynamic.viewport[2] = dynamic.scissor[2] = (int32_t)rects[index].rect.extent.width;
			dynamic.viewport[3] = dynamic.scissor[3] = (int32_t)rects[index].rect.extent.height;
			xgpu_vulkan_draw(&draw);
		}
		vk.statistics.clears_as_draws++;
		if (counting && vk.pass_active)
			test_query_begin();
	}
	free(rects);
}

/* ---------- draws */

static VkDescriptorPool descriptor_pool_get(void)
{
	struct slot *slot = vk.slot;

	if (slot->descriptor_pool_used >= slot->descriptor_pool_count)
	{
		VkDescriptorPoolSize sizes[5];
		VkDescriptorPoolCreateInfo info;
		VkDescriptorPool *grown = realloc(slot->descriptor_pools, (slot->descriptor_pool_count + 1) * sizeof(*grown));

		if (!grown)
			return VK_NULL_HANDLE;
		slot->descriptor_pools = grown;
		/* (the most a set has: the occlusion's, the field's atlas and
		table, the static scenery's pages, the vehicles' fields' atlas and
		the occlusion found once a pixel) */
		sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sizes[0].descriptorCount = SETS_A_POOL * 5;
		/* (display.hdr's input sets, where draws can read their colour
		attachment) */
		sizes[2].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		sizes[2].descriptorCount = SETS_A_POOL;
		/* (and, interlocked, as a storage image) */
		sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		sizes[3].descriptorCount = SETS_A_POOL;
		/* (display.ambient_occlusion "world"'s block, in the occlusion's
		sets) */
		sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		sizes[1].descriptorCount = SETS_A_POOL;
		memset(&info, 0, sizeof(info));
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		info.maxSets = SETS_A_POOL;
		info.poolSizeCount = vk.capabilities.color_read ? (vk.capabilities.pixel_interlock ? 4 : 3) : 2;
		/* (display.ambient_occlusion "ray_traced"'s scene, in the occlusion's
		sets, where the device has ray queries) */
		if (vk.capabilities.ray_queries)
		{
			sizes[info.poolSizeCount].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
			sizes[info.poolSizeCount].descriptorCount = SETS_A_POOL;
			info.poolSizeCount++;
		}
		info.pPoolSizes = sizes;
		if (!check(vkCreateDescriptorPool(vk.device, &info, NULL, &slot->descriptor_pools[slot->descriptor_pool_count]),
			"vkCreateDescriptorPool"))
			return VK_NULL_HANDLE;
		slot->descriptor_pool_count++;
	}
	return slot->descriptor_pools[slot->descriptor_pool_used];
}

/* a set of a layout from the slot's pools (good until they are reset, as
the slot is used again), or VK_NULL_HANDLE */
static VkDescriptorSet set_allocate(VkDescriptorSetLayout layout)
{
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorSet set = VK_NULL_HANDLE;

	for (;;)
	{
		VkDescriptorPool pool = descriptor_pool_get();
		VkResult result;

		if (!pool)
			return VK_NULL_HANDLE;
		memset(&allocate, 0, sizeof(allocate));
		allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocate.descriptorPool = pool;
		allocate.descriptorSetCount = 1;
		allocate.pSetLayouts = &layout;
		result = vkAllocateDescriptorSets(vk.device, &allocate, &set);
		if (result == VK_SUCCESS)
		{
			if (vk.slot->descriptor_pool_dirty <= vk.slot->descriptor_pool_used)
				vk.slot->descriptor_pool_dirty = vk.slot->descriptor_pool_used + 1;
			return set;
		}
		if (result != VK_ERROR_OUT_OF_POOL_MEMORY && result != VK_ERROR_FRAGMENTED_POOL)
		{
			check(result, "vkAllocateDescriptorSets");
			return VK_NULL_HANDLE;
		}
		vk.slot->descriptor_pool_used++;
	}
}

/* display.hdr: the input set of the colour attachment bound (the pass
begun), as a draw that reads it binds it: the last one, where it is of the
same view and submission, else one written now */
static VkDescriptorSet input_set(void)
{
	VkDescriptorImageInfo image;
	VkWriteDescriptorSet write;
	VkImageView view = vk.color ? attachment_view(vk.color, vk.layer, vk.views) : VK_NULL_HANDLE;

	if (!view || !vk.input_layout)
		return VK_NULL_HANDLE;
	if (vk.input_set && vk.input_view == view && vk.input_serial == vk.serial)
		return vk.input_set;
	vk.input_set = set_allocate(vk.input_layout);
	if (!vk.input_set)
		return VK_NULL_HANDLE;
	vk.input_view = view;
	vk.input_serial = vk.serial;
	memset(&image, 0, sizeof(image));
	image.imageView = view;
	image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = vk.input_set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	write.pImageInfo = &image;
	vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
	/* (the interlocked draws': the same view as a storage image,
	XGPU_SPIRV_BINDING_STORE) */
	if (vk.capabilities.pixel_interlock)
	{
		write.dstBinding = XGPU_VULKAN_BINDING_STORE;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		vkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
	}
	return vk.input_set;
}

/* display.ambient_occlusion "world"'s set, as a draw that lights by the
occlusion binds it: the last one, where it is of the same field and
submission, else one written now */
static VkDescriptorSet occlusion_set(void)
{
	VkDescriptorImageInfo field_image, table_image, scenery_image, objects_image, pixels_image;
	VkDescriptorBufferInfo block;
	VkWriteDescriptorSet writes[7];
	VkWriteDescriptorSetAccelerationStructureKHR scene_write;
	VkAccelerationStructureKHR scene = VK_NULL_HANDLE;
	struct xgpu_vulkan_image *field = xgpu_vulkan_world_ao_field_image();
	struct xgpu_vulkan_image *table = xgpu_vulkan_world_ao_table_image();
	struct xgpu_vulkan_image *scenery = xgpu_vulkan_world_ao_scenery_image();
	struct xgpu_vulkan_image *objects = xgpu_vulkan_world_ao_objects_image();
	struct xgpu_vulkan_image *pixels = xgpu_vulkan_world_ao_pixels_image();
	uint64_t serial = vk.serial;
	int index;

	if (!field || !field->sampled || !table || !table->sampled || !scenery || !scenery->sampled ||
		!objects || !objects->sampled || !pixels || !pixels->sampled || !vk.occlusion_layout)
		return VK_NULL_HANDLE;
	/* (display.ambient_occlusion "world"'s block in this submission's ring) */
	if (!xgpu_vulkan_world_ao_block(serial, &block))
		return VK_NULL_HANDLE;
	/* (display.ambient_occlusion "ray_traced"'s scene, where the device has
	ray queries: the latest built, or an empty one) */
	if (vk.capabilities.ray_queries)
	{
		scene = xgpu_vulkan_rt_scene();
		if (!scene)
			return VK_NULL_HANDLE;
	}
	field->used = vk.serial;
	table->used = vk.serial;
	scenery->used = vk.serial;
	objects->used = vk.serial;
	pixels->used = vk.serial;
	if (vk.occlusion_set && vk.occlusion_serial == vk.serial && vk.occlusion_world_number == xgpu_vulkan_world_ao_number())
		return vk.occlusion_set;
	vk.occlusion_set = set_allocate(vk.occlusion_layout);
	if (!vk.occlusion_set)
		return VK_NULL_HANDLE;
	vk.occlusion_serial = vk.serial;
	vk.occlusion_world_number = xgpu_vulkan_world_ao_number();
	memset(&field_image, 0, sizeof(field_image));
	field_image.sampler = xgpu_vulkan_world_ao_sampler();
	field_image.imageView = field->sampled;
	field_image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	table_image = field_image;
	table_image.imageView = table->sampled;
	scenery_image = field_image;
	scenery_image.imageView = scenery->sampled;
	objects_image = field_image;
	objects_image.imageView = objects->sampled;
	pixels_image = field_image;
	pixels_image.imageView = pixels->sampled;
	memset(writes, 0, sizeof(writes));
	for (index = 0; index < 6; index++)
	{
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = vk.occlusion_set;
		writes[index].descriptorCount = 1;
		writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	}
	writes[0].dstBinding = XGPU_VULKAN_BINDING_WORLD_SDF;
	writes[0].pImageInfo = &field_image;
	writes[1].dstBinding = XGPU_VULKAN_BINDING_WORLD_AO;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[1].pBufferInfo = &block;
	writes[2].dstBinding = XGPU_VULKAN_BINDING_WORLD_SDF_TABLE;
	writes[2].pImageInfo = &table_image;
	writes[3].dstBinding = XGPU_VULKAN_BINDING_WORLD_AO_SCENERY;
	writes[3].pImageInfo = &scenery_image;
	writes[4].dstBinding = XGPU_VULKAN_BINDING_WORLD_AO_OBJECTS;
	writes[4].pImageInfo = &objects_image;
	writes[5].dstBinding = XGPU_VULKAN_BINDING_WORLD_AO_PIXELS;
	writes[5].pImageInfo = &pixels_image;
	if (scene)
	{
		memset(&scene_write, 0, sizeof(scene_write));
		scene_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
		scene_write.accelerationStructureCount = 1;
		scene_write.pAccelerationStructures = &scene;
		writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[6].pNext = &scene_write;
		writes[6].dstSet = vk.occlusion_set;
		writes[6].dstBinding = XGPU_VULKAN_BINDING_WORLD_AO_SCENE;
		writes[6].descriptorCount = 1;
		writes[6].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	}
	vkUpdateDescriptorSets(vk.device, scene ? 7 : 6, writes, 0, NULL);
	return vk.occlusion_set;
}

/* a kept set with these textures, or NULL */
static struct texture_set_entry *texture_set_find(const struct texture_set_key *key, uint32_t bucket)
{
	uint32_t index;

	for (index = vk.texture_set_buckets[bucket]; index; index = vk.texture_sets[index - 1].next)
	{
		if (!memcmp(&vk.texture_sets[index - 1].key, key, sizeof(*key)))
			return &vk.texture_sets[index - 1];
	}
	return NULL;
}

/* the kept set pool, made the first time */
static int texture_set_pool(void)
{
	VkDescriptorPoolSize size;
	VkDescriptorPoolCreateInfo info;

	if (vk.texture_set_pool)
		return 1;
	vk.texture_sets = calloc(TEXTURE_SETS_KEPT, sizeof(*vk.texture_sets));
	if (!vk.texture_sets)
		return 0;
	size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	size.descriptorCount = TEXTURE_SETS_KEPT * 4;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	info.maxSets = TEXTURE_SETS_KEPT;
	info.poolSizeCount = 1;
	info.pPoolSizes = &size;
	if (!check(vkCreateDescriptorPool(vk.device, &info, NULL, &vk.texture_set_pool), "vkCreateDescriptorPool"))
	{
		free(vk.texture_sets);
		vk.texture_sets = NULL;
		vk.texture_set_pool = VK_NULL_HANDLE;
		return 0;
	}
	return 1;
}

/* a kept set to write these textures to, in their bucket: a new one while
there are fewer than TEXTURE_SETS_KEPT, else the one the GPU finished with
longest ago that is found first after the last taken; NULL if none can be
(every one is read by work the GPU has not done) */
static struct texture_set_entry *texture_set_take(const struct texture_set_key *key, uint32_t bucket)
{
	struct texture_set_entry *entry = NULL;
	uint32_t looked, *link;

	if (!texture_set_pool())
		return NULL;
	if (vk.texture_set_count < TEXTURE_SETS_KEPT)
	{
		VkDescriptorSetAllocateInfo allocate;
		VkDescriptorSet set = VK_NULL_HANDLE;

		memset(&allocate, 0, sizeof(allocate));
		allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocate.descriptorPool = vk.texture_set_pool;
		allocate.descriptorSetCount = 1;
		allocate.pSetLayouts = &vk.texture_layout;
		if (!check(vkAllocateDescriptorSets(vk.device, &allocate, &set), "vkAllocateDescriptorSets"))
			return NULL;
		entry = &vk.texture_sets[vk.texture_set_count++];
		entry->set = set;
	}
	else
	{
		/* (the work done is asked again once, if the first look finds
		none) */
		uint64_t completed = vk.completed;

		for (looked = 0; looked < 2 * TEXTURE_SETS_KEPT && !entry; looked++)
		{
			struct texture_set_entry *candidate = &vk.texture_sets[vk.texture_set_hand];

			vk.texture_set_hand = (vk.texture_set_hand + 1) % TEXTURE_SETS_KEPT;
			if (looked == TEXTURE_SETS_KEPT)
				completed = completed_serial();
			if (candidate->used <= completed && candidate->used < vk.serial)
				entry = candidate;
		}
		if (!entry)
			return NULL;
		/* (out of its bucket) */
		for (link = &vk.texture_set_buckets[entry->bucket]; *link; link = &vk.texture_sets[*link - 1].next)
		{
			if (&vk.texture_sets[*link - 1] == entry)
			{
				*link = entry->next;
				break;
			}
		}
	}
	/* (copied as bytes, its padding too: texture_set_find compares them
	all; a structure's assignment need not copy padding) */
	memcpy(&entry->key, key, sizeof(entry->key));
	entry->bucket = bucket;
	entry->next = vk.texture_set_buckets[bucket];
	vk.texture_set_buckets[bucket] = (uint32_t)(entry - vk.texture_sets) + 1;
	return entry;
}

/* the descriptor set of the stages' textures: the last draw's, one kept
with the same views and samplers (the game's draws come back to the same
few, frame after frame), or one written now */
static VkDescriptorSet texture_set(const struct xgpu_vulkan_texture textures[4])
{
	VkDescriptorImageInfo images[4];
	VkWriteDescriptorSet writes[4];
	struct xgpu_vulkan_image *sampled[4];
	VkDescriptorSet set = VK_NULL_HANDLE;
	struct texture_set_key key;
	struct texture_set_entry *entry;
	uint32_t bucket;
	int stage;

	if (vk.last_texture_valid && !memcmp(vk.last_textures, textures, sizeof(vk.last_textures)))
		return vk.last_texture_set;
	memset(&key, 0, sizeof(key));
	for (stage = 0; stage < 4; stage++)
	{
		const struct xgpu_vulkan_texture *texture = &textures[stage];
		struct xgpu_vulkan_image *image = texture->image && texture->image->sampled ? texture->image :
			vk.stand_ins[texture->kind & 3];

		images[stage].sampler = texture->sampler ? texture->sampler : vk.stand_in_sampler;
		images[stage].imageView = image->sampled;
		if (texture->kind == XGPU_VULKAN_IMAGE_2D && image->description.type == XGPU_VULKAN_IMAGE_2D_ARRAY)
		{
			if (!image->sampled_2d)
				image->sampled_2d = view_make(image, VK_IMAGE_VIEW_TYPE_2D, 0, 1, 1);
			if (!image->sampled_2d)
				return VK_NULL_HANDLE;
			images[stage].imageView = image->sampled_2d;
		}
		images[stage].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		sampled[stage] = image;
		key.views[stage] = images[stage].imageView;
		key.samplers[stage] = images[stage].sampler;
		key.numbers[stage] = image->number;
	}
	/* (its images used by this submission: destroyed after it) */
	for (stage = 0; stage < 4; stage++)
		sampled[stage]->used = vk.serial;
	bucket = (uint32_t)(bytes_hash64(&key, sizeof(key)) % TEXTURE_SET_BUCKETS);
	entry = texture_set_find(&key, bucket);
	if (entry)
	{
		entry->used = vk.serial;
		memcpy(vk.last_textures, textures, sizeof(vk.last_textures));
		vk.last_texture_set = entry->set;
		vk.last_texture_valid = 1;
		vk.statistics.texture_sets_reused++;
		return entry->set;
	}
	/* (a kept one, else one of the slot's pools, good for this submission
	alone) */
	entry = texture_set_take(&key, bucket);
	if (entry)
	{
		entry->used = vk.serial;
		set = entry->set;
	}
	else
	{
		set = set_allocate(vk.texture_layout);
	}
	if (!set)
		return VK_NULL_HANDLE;
	memset(writes, 0, sizeof(writes));
	/* One write per binding: PowerVR's Android driver does not reliably
	advance across bindings when one write contains all four descriptors. */
	for (stage = 0; stage < 4; stage++)
	{
		writes[stage].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[stage].dstSet = set;
		writes[stage].dstBinding = stage;
		writes[stage].descriptorCount = 1;
		writes[stage].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[stage].pImageInfo = &images[stage];
	}
	vkUpdateDescriptorSets(vk.device, 4, writes, 0, NULL);
	memcpy(vk.last_textures, textures, sizeof(vk.last_textures));
	vk.last_texture_set = set;
	vk.last_texture_valid = 1;
	vk.statistics.texture_sets_written++;
	return set;
}

static VkPrimitiveTopology topology_of(uint8_t topology)
{
	switch (topology)
	{
	case XGPU_VULKAN_TOPOLOGY_POINTS: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	case XGPU_VULKAN_TOPOLOGY_LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case XGPU_VULKAN_TOPOLOGY_LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	case XGPU_VULKAN_TOPOLOGY_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	case XGPU_VULKAN_TOPOLOGY_TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
	default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
}

/* ---------- the replay's ID buffer (xgpu_vulkan_inspect_*)

Each draw of a frame's into the target it presents (its image, or its
multisampled image) between the stream's handing it on and its end
(xgpu_vulkan_inspect_next) is drawn in a pass of its own: before it the
target is copied (a multisampled one resolved), after it the copy is
compared with the target (resolved again) by xgpu_vulkan_inspect_shader.c's
pass, which puts the draw's number on top of the stacks of the pixels it
changed. The ID image is a 2D array of R32_UINT, a layer for each place
of the stacks. The work is the main command buffer's, in order with the
draws (outside their passes: the pass breaks here are the replay's alone,
and the stores between them are kept). */

extern const uint32_t xgpu_vulkan_inspect_shader[];
extern const size_t xgpu_vulkan_inspect_shader_size;

#define INSPECT_SETS 64

static struct
{
	/* a frame's (begin to end); the next draw's number (armed: the
	stream's draw being handed on), and its target's images */
	int active, armed, failed, cleared;
	uint32_t depth, number;
	struct xgpu_vulkan_image *image, *multisampled;
	/* the pass: made once */
	VkDescriptorSetLayout set_layout;
	VkPipelineLayout layout;
	VkPipeline pipeline;
	VkDescriptorPool pool;
	VkSampler sampler;
	/* the target's copies (before, and after: a multisampled one's
	resolve) and the ID image, of its size and format */
	struct xgpu_vulkan_image *before, *after;
	VkImage ids;
	struct allocation ids_memory;
	VkImageView ids_view;
	uint32_t width, height, layers;
	uint8_t format;
	/* the sets written, by the image compared after (its number) */
	struct
	{
		uint64_t number;
		VkDescriptorSet set;
	} sets[INSPECT_SETS];
	uint32_t set_count;
} inspect;

static int inspect_pass_make(void)
{
	VkDescriptorSetLayoutBinding bindings[3];
	VkDescriptorSetLayoutCreateInfo set_info;
	VkPushConstantRange push;
	VkPipelineLayoutCreateInfo layout_info;
	VkShaderModuleCreateInfo module_info;
	VkShaderModule module;
	VkComputePipelineCreateInfo pipeline_info;
	VkDescriptorPoolSize sizes[2];
	VkDescriptorPoolCreateInfo pool_info;
	struct xgpu_vulkan_sampler_description sampler;
	int index;

	if (inspect.pipeline)
		return 1;
	memset(bindings, 0, sizeof(bindings));
	for (index = 0; index < 3; index++)
	{
		bindings[index].binding = (uint32_t)index;
		bindings[index].descriptorType = index < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		bindings[index].descriptorCount = 1;
		bindings[index].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_info.bindingCount = 3;
	set_info.pBindings = bindings;
	if (!check(vkCreateDescriptorSetLayout(vk.device, &set_info, NULL, &inspect.set_layout),
		"vkCreateDescriptorSetLayout (inspect)"))
		return 0;
	memset(&push, 0, sizeof(push));
	push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push.size = 24;
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &inspect.set_layout;
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &push;
	if (!check(vkCreatePipelineLayout(vk.device, &layout_info, NULL, &inspect.layout), "vkCreatePipelineLayout (inspect)"))
		return 0;
	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = xgpu_vulkan_inspect_shader_size;
	module_info.pCode = xgpu_vulkan_inspect_shader;
	if (!check(vkCreateShaderModule(vk.device, &module_info, NULL, &module), "vkCreateShaderModule (inspect)"))
		return 0;
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = inspect.layout;
	/* (the module is kept: a pass made once) */
	if (!check(vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &inspect.pipeline),
		"vkCreateComputePipelines (inspect)"))
		return 0;
	sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[0].descriptorCount = INSPECT_SETS * 2;
	sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	sizes[1].descriptorCount = INSPECT_SETS;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = INSPECT_SETS;
	pool_info.poolSizeCount = 2;
	pool_info.pPoolSizes = sizes;
	if (!check(vkCreateDescriptorPool(vk.device, &pool_info, NULL, &inspect.pool), "vkCreateDescriptorPool (inspect)"))
		return 0;
	memset(&sampler, 0, sizeof(sampler));
	sampler.minify = sampler.magnify = XGPU_VULKAN_FILTER_POINT;
	sampler.mip = XGPU_VULKAN_MIP_NONE;
	sampler.anisotropy = 1;
	inspect.sampler = xgpu_vulkan_sampler(&sampler);
	return inspect.sampler != VK_NULL_HANDLE;
}

/* the copies and the ID image, of the image's size and format (made again
for another: the GPU waited for first) */
static int inspect_images_make(const struct xgpu_vulkan_image *image)
{
	struct xgpu_vulkan_image_description description;
	VkImageCreateInfo info;
	VkImageViewCreateInfo view;
	VkMemoryRequirements requirements;

	if (inspect.before && inspect.width == image->description.width && inspect.height == image->description.height &&
		inspect.format == image->description.format && inspect.layers == inspect.depth)
		return 1;
	xgpu_vulkan_wait(xgpu_vulkan_fence());
	if (inspect.before)
		xgpu_vulkan_image_destroy(inspect.before);
	if (inspect.after)
		xgpu_vulkan_image_destroy(inspect.after);
	inspect.before = inspect.after = NULL;
	if (inspect.ids_view)
		vkDestroyImageView(vk.device, inspect.ids_view, NULL);
	if (inspect.ids)
	{
		vkDestroyImage(vk.device, inspect.ids, NULL);
		memory_release(&inspect.ids_memory);
	}
	inspect.ids = VK_NULL_HANDLE;
	inspect.ids_view = VK_NULL_HANDLE;
	vkResetDescriptorPool(vk.device, inspect.pool, 0);
	inspect.set_count = 0;
	memset(&description, 0, sizeof(description));
	description.type = XGPU_VULKAN_IMAGE_2D;
	description.format = image->description.format;
	description.usage = XGPU_VULKAN_USAGE_SAMPLED;
	description.width = image->description.width;
	description.height = image->description.height;
	description.depth_or_layers = 1;
	description.levels = 1;
	inspect.before = xgpu_vulkan_image_create(&description);
	inspect.after = xgpu_vulkan_image_create(&description);
	if (!inspect.before || !inspect.after)
		return 0;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = VK_FORMAT_R32_UINT;
	info.extent.width = description.width;
	info.extent.height = description.height;
	info.extent.depth = 1;
	info.mipLevels = 1;
	info.arrayLayers = inspect.depth;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if (!check(vkCreateImage(vk.device, &info, NULL, &inspect.ids), "vkCreateImage (inspect)"))
	{
		inspect.ids = VK_NULL_HANDLE;
		return 0;
	}
	vkGetImageMemoryRequirements(vk.device, inspect.ids, &requirements);
	if (!memory_for(&requirements, &inspect.ids_memory) ||
		!check(vkBindImageMemory(vk.device, inspect.ids, inspect.ids_memory.memory, inspect.ids_memory.offset),
			"vkBindImageMemory (inspect)"))
		return 0;
	memset(&view, 0, sizeof(view));
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = inspect.ids;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	view.format = VK_FORMAT_R32_UINT;
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.levelCount = 1;
	view.subresourceRange.layerCount = inspect.depth;
	if (!check(vkCreateImageView(vk.device, &view, NULL, &inspect.ids_view), "vkCreateImageView (inspect)"))
		return 0;
	{
		VkCommandBuffer commands = early_commands();
		VkImageMemoryBarrier transition;

		memset(&transition, 0, sizeof(transition));
		transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		transition.srcQueueFamilyIndex = transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		transition.image = inspect.ids;
		transition.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		transition.subresourceRange.levelCount = 1;
		transition.subresourceRange.layerCount = inspect.depth;
		vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
			NULL, 1, &transition);
	}
	inspect.width = description.width;
	inspect.height = description.height;
	inspect.format = description.format;
	inspect.layers = inspect.depth;
	return 1;
}

/* the set comparing the copy before with compared (the target, or the
copy after) */
static VkDescriptorSet inspect_set(struct xgpu_vulkan_image *compared)
{
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorImageInfo images[3];
	VkWriteDescriptorSet writes[3];
	VkDescriptorSet set;
	uint32_t index;

	for (index = 0; index < inspect.set_count; index++)
	{
		if (inspect.sets[index].number == compared->number)
			return inspect.sets[index].set;
	}
	if (inspect.set_count == INSPECT_SETS)
		return VK_NULL_HANDLE;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = inspect.pool;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &inspect.set_layout;
	if (!check(vkAllocateDescriptorSets(vk.device, &allocate, &set), "vkAllocateDescriptorSets (inspect)"))
		return VK_NULL_HANDLE;
	memset(images, 0, sizeof(images));
	images[0].sampler = images[1].sampler = inspect.sampler;
	images[0].imageView = inspect.before->sampled;
	images[1].imageView = compared->sampled;
	images[2].imageView = inspect.ids_view;
	images[0].imageLayout = images[1].imageLayout = images[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	memset(writes, 0, sizeof(writes));
	for (index = 0; index < 3; index++)
	{
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = set;
		writes[index].dstBinding = index;
		writes[index].descriptorCount = 1;
		writes[index].descriptorType = index < 2 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[index].pImageInfo = &images[index];
	}
	vkUpdateDescriptorSets(vk.device, 3, writes, 0, NULL);
	inspect.sets[inspect.set_count].number = compared->number;
	inspect.sets[inspect.set_count].set = set;
	inspect.set_count++;
	return set;
}

/* the draw's target, where it is the one looked at (a 2D image's first
layer): the target bound, else NULL */
static struct xgpu_vulkan_image *inspect_looked(void)
{
	struct xgpu_vulkan_image *color = vk.color;

	if (!inspect.active || !inspect.armed || inspect.failed || !color || vk.layer || vk.views > 1 ||
		(color != inspect.image && color != inspect.multisampled) || color->transient)
		return NULL;
	return color;
}

/* everything before it done with, before the transfer and the pass after */
#define INSPECT_STAGES (VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
#define INSPECT_ACCESS (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)

/* the image resolved into, or copied into, to */
static void inspect_copy(VkCommandBuffer commands, struct xgpu_vulkan_image *image, struct xgpu_vulkan_image *to)
{
	if (image->description.samples > 1)
	{
		VkImageResolve resolve;

		memset(&resolve, 0, sizeof(resolve));
		resolve.srcSubresource.aspectMask = resolve.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		resolve.srcSubresource.layerCount = resolve.dstSubresource.layerCount = 1;
		resolve.extent.width = inspect.width;
		resolve.extent.height = inspect.height;
		resolve.extent.depth = 1;
		vkCmdResolveImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, to->image, VK_IMAGE_LAYOUT_GENERAL, 1,
			&resolve);
	}
	else
	{
		VkImageCopy copy;

		memset(&copy, 0, sizeof(copy));
		copy.srcSubresource.aspectMask = copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.srcSubresource.layerCount = copy.dstSubresource.layerCount = 1;
		copy.extent.width = inspect.width;
		copy.extent.height = inspect.height;
		copy.extent.depth = 1;
		vkCmdCopyImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, to->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
	}
}

static void inspect_before(struct xgpu_vulkan_image *image)
{
	VkCommandBuffer commands;

	/* (the pass ended first: an image made again waits for the GPU, the
	work so far submitted) */
	main_commands();
	if (!inspect_pass_make() || !inspect_images_make(image))
	{
		inspect.failed = 1;
		host_logf(HOST_LOG_WARN, "vulkan: the replay's ID buffer cannot be made");
		return;
	}
	commands = main_commands();
	image_use(image);
	image_use(inspect.before);
	barrier(commands, INSPECT_STAGES, INSPECT_ACCESS, VK_PIPELINE_STAGE_TRANSFER_BIT, INSPECT_ACCESS);
	if (!inspect.cleared)
	{
		VkClearColorValue zero;
		VkImageSubresourceRange range;

		memset(&zero, 0, sizeof(zero));
		memset(&range, 0, sizeof(range));
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.levelCount = 1;
		range.layerCount = inspect.depth;
		vkCmdClearColorImage(commands, inspect.ids, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
		inspect.cleared = 1;
	}
	inspect_copy(commands, image, inspect.before);
	barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, INSPECT_ACCESS, INSPECT_STAGES, INSPECT_ACCESS);
}

static void inspect_after(struct xgpu_vulkan_image *image)
{
	VkCommandBuffer commands;
	struct xgpu_vulkan_image *compared = image->description.samples > 1 ? inspect.after : image;
	VkDescriptorSet set;
	uint32_t push[6];

	if (inspect.failed)
		return;
	commands = main_commands();
	image_use(image);
	barrier(commands, INSPECT_STAGES, INSPECT_ACCESS, VK_PIPELINE_STAGE_TRANSFER_BIT |
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, INSPECT_ACCESS);
	if (compared != image)
	{
		inspect_copy(commands, image, compared);
		barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, INSPECT_ACCESS, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			INSPECT_ACCESS);
	}
	set = inspect_set(compared);
	if (!set)
	{
		inspect.failed = 1;
		return;
	}
	vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, inspect.pipeline);
	vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, inspect.layout, 0, 1, &set, 0, NULL);
	push[0] = push[1] = 0;
	push[2] = inspect.width;
	push[3] = inspect.height;
	push[4] = inspect.number + 1;
	push[5] = inspect.depth;
	vkCmdPushConstants(commands, inspect.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
	vkCmdDispatch(commands, (inspect.width + 7) / 8, (inspect.height + 7) / 8, 1);
	barrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, INSPECT_ACCESS, INSPECT_STAGES, INSPECT_ACCESS);
	/* (the draws after it bind their state again: the compute pipeline
	took the bind point's descriptor sets' place in the command buffer) */
	vk.bound_pipeline = VK_NULL_HANDLE;
	memset(&vk.bound, 0, sizeof(vk.bound));
}

int xgpu_vulkan_inspect_begin(uint32_t depth)
{
	inspect.image = inspect.multisampled = NULL;
	inspect.depth = depth ? depth : 1;
	inspect.active = 1;
	inspect.armed = inspect.failed = inspect.cleared = 0;
	return 1;
}

void xgpu_vulkan_inspect_next(uint32_t number, struct xgpu_vulkan_image *image, struct xgpu_vulkan_image *multisampled)
{
	inspect.armed = number != UINT32_MAX;
	inspect.number = number;
	inspect.image = image;
	inspect.multisampled = multisampled;
}

uint32_t *xgpu_vulkan_inspect_read(struct xgpu_vulkan_image *image, uint32_t *width, uint32_t *height)
{
	VkDeviceSize layer_size, size;
	VkCommandBuffer commands;
	VkBufferImageCopy copy;
	uint32_t *ids, layer;
	size_t pixel, pixels;

	if (!inspect.active || inspect.failed || !image)
		return NULL;
	*width = image->description.width;
	*height = image->description.height;
	pixels = (size_t)*width * *height;
	ids = calloc(pixels * inspect.depth, sizeof(*ids));
	if (!ids)
		return NULL;
	/* (no draw was looked at: none drew into the target) */
	if (!inspect.before || !inspect.cleared || inspect.width != *width || inspect.height != *height)
		return ids;
	layer_size = (VkDeviceSize)pixels * 4;
	size = layer_size * inspect.depth;
	if (size > vk.readback_size)
	{
		if (vk.readback)
		{
			vkDestroyBuffer(vk.device, vk.readback, NULL);
			memory_release(&vk.readback_memory);
			vk.readback = VK_NULL_HANDLE;
		}
		if (!buffer_make(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, BUFFER_READBACK, &vk.readback, &vk.readback_memory,
			(void **)&vk.readback_mapped))
		{
			vk.readback_size = 0;
			free(ids);
			return NULL;
		}
		vk.readback_size = size;
	}
	commands = main_commands();
	barrier(commands, INSPECT_STAGES, INSPECT_ACCESS, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
	memset(&copy, 0, sizeof(copy));
	copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copy.imageSubresource.layerCount = inspect.depth;
	copy.imageExtent.width = *width;
	copy.imageExtent.height = *height;
	copy.imageExtent.depth = 1;
	vkCmdCopyImageToBuffer(commands, inspect.ids, VK_IMAGE_LAYOUT_GENERAL, vk.readback, 1, &copy);
	barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
		VK_ACCESS_HOST_READ_BIT);
	xgpu_vulkan_wait(xgpu_vulkan_fence());
	/* (layer by layer, into each pixel's stack) */
	for (layer = 0; layer < inspect.depth; layer++)
	{
		const uint32_t *from = (const uint32_t *)(vk.readback_mapped + layer * layer_size);

		for (pixel = 0; pixel < pixels; pixel++)
			ids[pixel * inspect.depth + layer] = from[pixel];
	}
	return ids;
}

void xgpu_vulkan_inspect_end(void)
{
	inspect.active = inspect.armed = 0;
	inspect.image = inspect.multisampled = NULL;
}

static void draw_body(const struct xgpu_vulkan_draw *draw);

void xgpu_vulkan_draw(const struct xgpu_vulkan_draw *draw)
{
	struct xgpu_vulkan_image *looked = inspect_looked();

	if (!looked)
	{
		draw_body(draw);
		return;
	}
	inspect_before(looked);
	draw_body(draw);
	/* (its pass ended: the comparison is outside it) */
	inspect_after(looked);
}

static void draw_body(const struct xgpu_vulkan_draw *draw)
{
	const struct xgpu_vulkan_dynamic_state *dynamic = draw->dynamic;
	VkCommandBuffer commands;
	VkViewport viewport;
	VkRect2D scissor;
	VkDescriptorSet sets[2];
	uint32_t offsets[2], index;
	int32_t left, top, right, bottom;

	if (!draw->pipeline || !draw->count || dynamic->viewport[2] <= 0 || dynamic->viewport[3] <= 0)
		return;
	/* (the texture set first: it may begin the recording) */
	recording_begin();
	sets[1] = texture_set(draw->textures);
	if (!sets[1])
		return;
	/* A draw that samples the depth attachment it is tested against (the
	VR mode's effects) is drawn in a pass of its own that writes no depth
	or stencil, which draws that read none of them may share; one that
	writes them, in a pass that samples none. Vulkan allows an attachment to
	be read through a descriptor in a pass only where the pass does not
	write it (the images stay in the GENERAL layout), and the pass before
	has stored the depth sampled (the barrier at its end). */
	if (vk.pass_active && ((draw->reads_depth && !vk.pass_reads_depth) || (draw->writes_depth && vk.pass_reads_depth)))
		pass_end();
	if (!vk.pass_active)
		vk.pass_reads_depth = draw->reads_depth && vk.depth;
	if (!pass_begin(0, NULL, 0.0f, 0))
	{
		vk.pass_reads_depth = 0;
		return;
	}
	/* (with discards: what the draw samples, used in this pass) */
	if (vk.deferred)
	{
		for (index = 0; index < 4; index++)
			image_use(draw->textures[index].image);
		/* (and the enhanced dynamic lights' cells, which the draws' set
		names: no upload of them put aside before this pass) */
		if (draw->dynamic_lights)
			image_use(vk.lights_cells);
		/* (and display.ambient_occlusion "world"'s) */
		if (draw->ambient_occlusion)
		{
			image_use(xgpu_vulkan_world_ao_field_image());
			image_use(xgpu_vulkan_world_ao_table_image());
			image_use(xgpu_vulkan_world_ao_scenery_image());
			image_use(xgpu_vulkan_world_ao_objects_image());
		}
	}
	commands = vk.pass_commands;
	if (vk.bound_pipeline != draw->pipeline)
	{
		vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, draw->pipeline);
		vk.bound_pipeline = draw->pipeline;
	}
	viewport.x = (float)dynamic->viewport[0];
	viewport.y = (float)dynamic->viewport[1];
	viewport.width = (float)dynamic->viewport[2];
	viewport.height = (float)dynamic->viewport[3];
	viewport.minDepth = XGPU_VULKAN_REVERSED_DEPTH ? 1.0f - dynamic->depth_range[1] : dynamic->depth_range[0];
	viewport.maxDepth = XGPU_VULKAN_REVERSED_DEPTH ? 1.0f - dynamic->depth_range[0] : dynamic->depth_range[1];
	/* (the scissor rectangle where it is one, else the whole target, as
	OpenGL's scissor test is enabled; inside the targets) */
	if (dynamic->scissor[2] > 0 && dynamic->scissor[3] > 0)
	{
		left = dynamic->scissor[0];
		top = dynamic->scissor[1];
		right = left + dynamic->scissor[2];
		bottom = top + dynamic->scissor[3];
	}
	else
	{
		left = top = 0;
		right = (int32_t)vk.pass_width;
		bottom = (int32_t)vk.pass_height;
	}
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (right > (int32_t)vk.pass_width)
		right = (int32_t)vk.pass_width;
	if (bottom > (int32_t)vk.pass_height)
		bottom = (int32_t)vk.pass_height;
	if (right <= left || bottom <= top)
		return;
	scissor.offset.x = left;
	scissor.offset.y = top;
	scissor.extent.width = (uint32_t)(right - left);
	scissor.extent.height = (uint32_t)(bottom - top);
	/* (each only where it differs from what the command buffer has: the
	game's draws mostly repeat their neighbours' state) */
	if (!vk.bound.dynamic || memcmp(&vk.bound.viewport, &viewport, sizeof(viewport)))
	{
		vkCmdSetViewport(commands, 0, 1, &viewport);
		vk.bound.viewport = viewport;
	}
	if (!vk.bound.dynamic || memcmp(&vk.bound.scissor, &scissor, sizeof(scissor)))
	{
		vkCmdSetScissor(commands, 0, 1, &scissor);
		vk.bound.scissor = scissor;
	}
	if (!vk.bound.dynamic || memcmp(vk.bound.depth_bias, dynamic->depth_bias, sizeof(vk.bound.depth_bias)))
	{
		/* (reversed: the constant bias the shaders', the slope's turned) */
		if (XGPU_VULKAN_REVERSED_DEPTH)
			vkCmdSetDepthBias(commands, 0.0f, 0.0f, -dynamic->depth_bias[0]);
		else
			vkCmdSetDepthBias(commands, dynamic->depth_bias[1], 0.0f, dynamic->depth_bias[0]);
		memcpy(vk.bound.depth_bias, dynamic->depth_bias, sizeof(vk.bound.depth_bias));
	}
	if (!vk.bound.dynamic || memcmp(vk.bound.blend, dynamic->blend_color, sizeof(vk.bound.blend)))
	{
		vkCmdSetBlendConstants(commands, dynamic->blend_color);
		memcpy(vk.bound.blend, dynamic->blend_color, sizeof(vk.bound.blend));
	}
	if (!vk.bound.dynamic || vk.bound.stencil_reference != (dynamic->stencil_reference & 0xff))
	{
		vkCmdSetStencilReference(commands, VK_STENCIL_FACE_FRONT_AND_BACK, dynamic->stencil_reference & 0xff);
		vk.bound.stencil_reference = dynamic->stencil_reference & 0xff;
	}
	if (!vk.bound.dynamic || vk.bound.topology != topology_of(draw->topology))
	{
		vkCmdSetPrimitiveTopology(commands, topology_of(draw->topology));
		vk.bound.topology = topology_of(draw->topology);
	}
	vk.bound.dynamic = 1;
	/* display.hdr: a draw that reads its colour attachment binds it as its
	input, and, unless the device orders the reads in rasterization order,
	sees the pass's draws before it through a framebuffer-local barrier
	(local reads' dependency: a memory barrier between framebuffer-space
	stages, by region); an interlocked one, which reads and writes it as a
	storage image, the same, its shader's reads and writes (and the draws
	after it see its writes: below) */
	if (!draw->stores_color)
		color_stores_seen(commands);
	if (draw->reads_color)
	{
		VkDescriptorSet input = input_set();

		if (!input)
			return;
		/* (an interlocked draw right after another, none: the interlock
		orders the two draws' triangles over the same pixel as it orders one
		draw's, in primitive order, which runs on from one draw to the next
		in a pass, and their coherent reads and writes of the storage image
		inside it see each other's) */
		if (!vk.capabilities.raster_order && !(draw->stores_color && vk.color_stored))
		{
			VkMemoryBarrier local;

			memset(&local, 0, sizeof(local));
			local.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			local.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			local.dstAccessMask = draw->stores_color ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT :
				VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
			vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1, &local, 0, NULL, 0, NULL);
			vk.statistics.local_reads++;
		}
		if (vk.bound.input != input)
		{
			vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 2, 1, &input, 0, NULL);
			vk.bound.input = input;
		}
	}
	/* (display.ambient_occlusion "world": the field and block the draw
	lights by) */
	if (draw->ambient_occlusion)
	{
		VkDescriptorSet occlusion = occlusion_set();

		if (!occlusion)
			return;
		if (vk.bound.occlusion != occlusion)
		{
			vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout,
				XGPU_VULKAN_SET_OCCLUSION, 1, &occlusion, 0, NULL);
			vk.bound.occlusion = occlusion;
		}
	}
	/* (the lights written after this are written after it: in order) */
	if (draw->dynamic_lights)
		vk.lights_block->used = vk.lights_cells->used = vk.serial;
	sets[0] = vk.draw_set;
	offsets[0] = (uint32_t)draw->constants.offset;
	offsets[1] = (uint32_t)draw->uniforms.offset;
	if (memcmp(vk.bound.sets, sets, sizeof(sets)) || memcmp(vk.bound.offsets, offsets, sizeof(offsets)))
	{
		vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline_layout, 0, 2, sets, 2, offsets);
		memcpy(vk.bound.sets, sets, sizeof(sets));
		memcpy(vk.bound.offsets, offsets, sizeof(offsets));
	}
	for (index = 0; index < XGPU_VULKAN_BINDINGS; index++)
	{
		if ((draw->bindings & (1u << index)) && draw->vertices[index].buffer)
		{
			uint32_t binding = vertex_binding(draw->bindings, index);

			if (vk.bound.vertices[binding] == draw->vertices[index].buffer &&
				vk.bound.vertex_offsets[binding] == draw->vertices[index].offset)
				continue;
			vkCmdBindVertexBuffers(commands, binding, 1, &draw->vertices[index].buffer, &draw->vertices[index].offset);
			vk.bound.vertices[binding] = draw->vertices[index].buffer;
			vk.bound.vertex_offsets[binding] = draw->vertices[index].offset;
		}
	}
	if (draw->indices.buffer)
	{
		if (vk.bound.indices != draw->indices.buffer || vk.bound.index_offset != draw->indices.offset)
		{
			vkCmdBindIndexBuffer(commands, draw->indices.buffer, draw->indices.offset, VK_INDEX_TYPE_UINT16);
			vk.bound.indices = draw->indices.buffer;
			vk.bound.index_offset = draw->indices.offset;
		}
		draw_times_draw(commands, draw, 0);
		timing_draw_mark(commands, 0);
		if (draw->reads_color && getenv("HALO_VULKAN_HDR_SERIAL") && draw->topology == XGPU_VULKAN_TOPOLOGY_TRIANGLES)
		{
			uint32_t triangle;

			for (triangle = 0; triangle + 3 <= draw->count; triangle += 3)
			{
				VkMemoryBarrier local;

				memset(&local, 0, sizeof(local));
				local.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
				local.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				local.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
				vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_DEPENDENCY_BY_REGION_BIT, 1, &local, 0, NULL, 0, NULL);
				vkCmdDrawIndexed(commands, 3, 1, draw->first_index + triangle, draw->vertex_offset, 0);
			}
		}
		else
			vkCmdDrawIndexed(commands, draw->count, 1, draw->first_index, draw->vertex_offset, 0);
	}
	else
	{
		draw_times_draw(commands, draw, 0);
		timing_draw_mark(commands, 0);
		vkCmdDraw(commands, draw->count, 1, 0, 0);
	}
	draw_times_draw(commands, draw, 1);
	timing_draw_mark(commands, 1);
	/* (an interlocked draw's writes: seen by what comes after its run,
	color_stores_seen) */
	if (draw->stores_color)
	{
		vk.color_stored = 1;
		vk.statistics.color_stores++;
	}
	vk.statistics.draws++;
}

/* ---------- submissions */

/* the recording submitted, with the semaphores of a present (VK_NULL_HANDLE:
none); returns its number */
static uint64_t submit(VkSemaphore wait_semaphore, VkSemaphore signal_semaphore)
{
	VkSubmitInfo info;
	VkTimelineSemaphoreSubmitInfo timeline;
	VkCommandBuffer buffers[2];
	VkSemaphore signals[2];
	uint64_t values[2] = { 0, 0 }, wait_values[1] = { 0 };
	VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	uint32_t count = 0;
	uint64_t serial;

	if (!vk.recording)
		return vk.serial - 1;
	pass_end();
	if (vk.deferred)
		segments_play();
	else
		direct_outside_mark();
	if (vk.early_recording)
	{
		barrier(vk.slot->early, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
		check(vkEndCommandBuffer(vk.slot->early), "vkEndCommandBuffer");
		buffers[count++] = vk.slot->early;
	}
	check(vkEndCommandBuffer(vk.slot->main), "vkEndCommandBuffer");
	buffers[count++] = vk.slot->main;
	serial = vk.serial;
	signals[0] = vk.timeline;
	values[0] = serial;
	signals[1] = signal_semaphore;
	memset(&timeline, 0, sizeof(timeline));
	timeline.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
	timeline.signalSemaphoreValueCount = signal_semaphore ? 2 : 1;
	timeline.pSignalSemaphoreValues = values;
	timeline.waitSemaphoreValueCount = wait_semaphore ? 1 : 0;
	timeline.pWaitSemaphoreValues = wait_values;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	info.pNext = &timeline;
	info.waitSemaphoreCount = wait_semaphore ? 1 : 0;
	info.pWaitSemaphores = &wait_semaphore;
	info.pWaitDstStageMask = &wait_stage;
	info.commandBufferCount = count;
	info.pCommandBuffers = buffers;
	info.signalSemaphoreCount = signal_semaphore ? 2 : 1;
	info.pSignalSemaphores = signals;
	check(vkQueueSubmit(vk.queue, 1, &info, VK_NULL_HANDLE), "vkQueueSubmit");
	vk.slot->ring_end = vk.ring_head;
	vk.recording = 0;
	vk.early_recording = 0;
	vk.serial++;
	vk.statistics.submissions++;
	return serial;
}

uint64_t xgpu_vulkan_serial(void)
{
	return vk.serial;
}

uint64_t xgpu_vulkan_fence(void)
{
	return submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
}

/* the upload ring's bytes the done submissions read, free again */
static void ring_reclaim(void)
{
	uint32_t index;

	for (index = 0; index < SLOTS; index++)
	{
		struct slot *slot = &vk.slots[index];

		if (slot->serial && slot->serial <= vk.completed && slot->ring_end > vk.ring_free_from)
			vk.ring_free_from = slot->ring_end;
	}
}

void xgpu_vulkan_wait(uint64_t fence)
{
	serial_wait(fence);
	ring_reclaim();
}

/* ---------- the window */

/* presents found SUBOPTIMAL between the surface's questions (about a second
of frames): a SUBOPTIMAL that making the swapchain again cannot mend is
not asked about every frame */
#define SUBOPTIMAL_CHECKS 60

/* presents after the window's size changes in which the surface is asked
again (about two seconds of frames), at every RESIZE_CHECK_EVERY of them,
until it has the window's new size: Android can tell the new size a little
after the event (split screen on a folding phone), and a swapchain made for
the old one is scaled into the window, squashed or stretched, with no
SUBOPTIMAL or OUT_OF_DATE to say so */
#define RESIZE_CHECKS 120
#define RESIZE_CHECK_EVERY 4

static void swapchain_forget(void)
{
	uint32_t index;

	if (!vk.swapchain)
		return;
	vkDeviceWaitIdle(vk.device);
	for (index = 0; index < vk.swapchain_image_count; index++)
	{
		if (vk.presentable[index])
			vkDestroySemaphore(vk.device, vk.presentable[index], NULL);
		vk.presentable[index] = VK_NULL_HANDLE;
	}
	xgpu_vulkan_pacing_swapchain(VK_NULL_HANDLE);
	vkDestroySwapchainKHR(vk.device, vk.swapchain, NULL);
	vk.swapchain = VK_NULL_HANDLE;
	vk.swapchain_image_count = 0;
}

/* the surface let go of, its swapchain first (lost, or the app in the
background); made again by surface_remake */
static void surface_forget(void)
{
	swapchain_forget();
	if (vk.surface && vkDestroySurfaceKHR)
		vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
	vk.surface = VK_NULL_HANDLE;
}

/* VK_ERROR_SURFACE_LOST_KHR from what: the surface let go of, and made
again at the next frame's present */
static void surface_lost(const char *what)
{
	host_logf(HOST_LOG_WARN, "vulkan: %s: the window's surface was lost: it is made again", what);
	surface_forget();
}

/* the window's surface made again (none, or the app back from the
background): 1 if there is one. Tried at each frame while the window has
none (Android gives the app its window back after it resumes). */
static int surface_remake(void)
{
	VkBool32 supported = VK_FALSE;

	if (vk.surface)
		return 1;
	if (!vk.surface_create || vk.window_suspended || !vkCreateSwapchainKHR)
		return 0;
	vk.surface = vk.surface_create(vk.instance, vk.surface_context);
	if (!vk.surface)
	{
		vk.surface_failing = 1;
		return 0;
	}
	vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.queue_family, vk.surface, &supported);
	if (!supported)
	{
		if (!vk.surface_failing)
			host_logf(HOST_LOG_WARN, "vulkan: the device's queue cannot present to the window's new surface");
		vk.surface_failing = 1;
		vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
		vk.surface = VK_NULL_HANDLE;
		return 0;
	}
	if (vk.surface_failing)
		host_logf(HOST_LOG_INFO, "vulkan: the window's surface is made again");
	vk.surface_failing = 0;
	vk.swapchain_stale = 1;
	return 1;
}

static const char *present_mode_name(VkPresentModeKHR mode)
{
	return mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "immediate" : mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" :
		mode == VK_PRESENT_MODE_FIFO_KHR ? "fifo" : mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR ? "fifo_relaxed" :
		"other";
}

/* the present mode of a swap interval: FIFO (which every surface has)
waits for the display; without waiting, MAILBOX (no tearing) where the
surface has it, else IMMEDIATE, else FIFO. debug.vulkan_present_mode's,
where the surface has it, before all (to measure each: PresentMon); the
surface's modes, and what was asked of them, are logged once */
static VkPresentModeKHR present_mode_choose(const VkPresentModeKHR *modes, uint32_t count, int interval)
{
	static int logged;
	VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t index;

	if (!logged)
	{
		char names[160];
		size_t used = 0;

		names[0] = 0;
		for (index = 0; index < count && used < sizeof(names); index++)
			used += (size_t)snprintf(names + used, sizeof(names) - used, "%s%s", index ? ", " : "",
				present_mode_name(modes[index]));
		host_logf(HOST_LOG_INFO, "vulkan: the surface's present modes: %s", count ? names : "(none)");
	}
	if (vk.present_mode_asked)
	{
		VkPresentModeKHR asked = (VkPresentModeKHR)(vk.present_mode_asked - 1);

		for (index = 0; index < count; index++)
		{
			if (modes[index] == asked)
				break;
		}
		if (!logged)
		{
			host_logf(HOST_LOG_INFO, index < count ? "vulkan: debug.vulkan_present_mode %s" :
				"vulkan: debug.vulkan_present_mode %s: not the surface's, the game's choice instead",
				present_mode_name(asked));
		}
		logged = 1;
		if (index < count)
			return asked;
	}
	logged = 1;
	if (interval > 0)
		return mode;
	for (index = 0; index < count; index++)
	{
		if (modes[index] == VK_PRESENT_MODE_MAILBOX_KHR)
			return modes[index];
		if (modes[index] == VK_PRESENT_MODE_IMMEDIATE_KHR)
			mode = modes[index];
	}
	return mode;
}

/* the quarter turns clockwise of a surface's transform (0: none, or a
mirroring one, which the frames are not drawn for) */
static int transform_turns(VkSurfaceTransformFlagBitsKHR transform)
{
	return transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? 1 :
		transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ? 2 :
		transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR ? 3 : 0;
}

/* (a float's bits: display.hdr's display, below) */
static float float_of(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* the window's SDR white in nits: the system's, else BT.2408's reference
white (where the system says nothing: 1.0, 80 nits) */
static float sdr_white_nits(void)
{
	float sdr_white = float_of(__atomic_load_n(&vk.window_sdr_white, __ATOMIC_ACQUIRE));

	return sdr_white > 1.0f ? sdr_white * 80.0f : XGPU_HDR_REFERENCE_WHITE_NITS;
}

/* the window's display shows HDR, and the swapchain can (the surface has
an HDR colour space) */
static int display_shows_hdr(void)
{
	return __atomic_load_n(&vk.window_hdr, __ATOMIC_ACQUIRE) && vk.surface_hdr;
}

/* the swapchain's output for the frames as they are: an HDR one where they
are HDR and the display shows it (scRGB on Windows, whose compositor is
scRGB's, else HDR10's PQ, the Linux compositors' own; HALO_VULKAN_HDR_OUTPUT
"scrgb" or "pq" chooses), else SDR */
static int hdr_output_choose(void)
{
	const char *chosen = getenv("HALO_VULKAN_HDR_OUTPUT");
	int first, second;

	if (!vk.hdr_active || !display_shows_hdr())
		return XGPU_HDR_OUTPUT_SDR;
#ifdef _WIN32
	first = XGPU_HDR_OUTPUT_SCRGB;
#else
	first = XGPU_HDR_OUTPUT_PQ;
#endif
	if (chosen && !strcmp(chosen, "scrgb"))
		first = XGPU_HDR_OUTPUT_SCRGB;
	else if (chosen && !strcmp(chosen, "pq"))
		first = XGPU_HDR_OUTPUT_PQ;
	second = first == XGPU_HDR_OUTPUT_PQ ? XGPU_HDR_OUTPUT_SCRGB : XGPU_HDR_OUTPUT_PQ;
	if (vk.surface_hdr & (1u << first))
		return first;
	return (vk.surface_hdr & (1u << second)) ? second : XGPU_HDR_OUTPUT_SDR;
}

/* what the host is told of the display, where it changed (the front end's
display.hdr "auto" follows it), and logged: the window system's word (SDL's:
HDR on, its headroom) and the surface's HDR outputs (the driver's), which
must both say HDR. The window system says nothing of HDR under X11 or
XWayland, nor on a Wayland compositor without its colour management; the
surface's outputs need the driver to have it as well. */
static void display_hdr_tell(void)
{
	int hdr = display_shows_hdr();
	int window = __atomic_load_n(&vk.window_hdr, __ATOMIC_ACQUIRE) != 0;
	float headroom = float_of(__atomic_load_n(&vk.window_headroom, __ATOMIC_ACQUIRE));
	int told;
	char outputs[32];

	if (!(headroom >= 1.0f))
		headroom = 1.0f;
	/* (the host is told where HDR or the headroom changed; the log is also
	written where only the reason for it did) */
	told = hdr != vk.display_hdr_told || headroom != vk.display_headroom_told ||
		sdr_white_nits() != vk.display_white_told;
	if (!told && window == vk.display_window_told && vk.surface_hdr == vk.display_surface_told)
		return;
	vk.display_hdr_told = hdr;
	vk.display_headroom_told = headroom;
	vk.display_white_told = sdr_white_nits();
	vk.display_window_told = window;
	vk.display_surface_told = vk.surface_hdr;
	snprintf(outputs, sizeof(outputs), "%s%s%s",
		(vk.surface_hdr & (1u << XGPU_HDR_OUTPUT_SCRGB)) ? "scRGB" : "",
		(vk.surface_hdr & (1u << XGPU_HDR_OUTPUT_SCRGB)) && (vk.surface_hdr & (1u << XGPU_HDR_OUTPUT_PQ)) ? ", " : "",
		(vk.surface_hdr & (1u << XGPU_HDR_OUTPUT_PQ)) ? "HDR10" : "");
	if (hdr)
		host_logf(HOST_LOG_INFO, "vulkan: the display shows HDR (headroom %.2f over an SDR white of %.0f nits%s, "
			"a peak of %.0f nits; swapchain outputs: %s)", (double)headroom, (double)sdr_white_nits(),
			float_of(__atomic_load_n(&vk.window_sdr_white, __ATOMIC_ACQUIRE)) > 1.0f ? "" :
			" (none given: BT.2408's)", (double)(headroom * sdr_white_nits()), outputs);
	else if (window)
		host_logf(HOST_LOG_INFO, "vulkan: the display shows HDR (headroom %.2f), but the surface has no HDR "
			"output (the driver's colour management): SDR", (double)headroom);
	else if (vk.surface_hdr)
		host_logf(HOST_LOG_INFO, "vulkan: the surface has HDR outputs (%s), but the window system says the "
			"display is SDR (or has no HDR to say: X11 and XWayland have none, nor a Wayland compositor without "
			"its colour management; HALO_VULKAN_HDR_HEADROOM gives a headroom): SDR", outputs);
	else
		host_logf(HOST_LOG_INFO, "vulkan: the display does not show HDR (no HDR state from the window system, "
			"no HDR output on the surface)");
	if (told && vk.display_hdr)
		vk.display_hdr(hdr, headroom, sdr_white_nits(),
			float_of(__atomic_load_n(&vk.window_sdr_white, __ATOMIC_ACQUIRE)) > 1.0f);
}

void xgpu_vulkan_window_hdr(int enabled, float sdr_white, float headroom)
{
	uint32_t white_bits, headroom_bits;

	if (!(sdr_white > 0.0f))
		sdr_white = 1.0f;
	if (!(headroom >= 1.0f))
		headroom = 1.0f;
	memcpy(&white_bits, &sdr_white, sizeof(white_bits));
	memcpy(&headroom_bits, &headroom, sizeof(headroom_bits));
	__atomic_store_n(&vk.window_sdr_white, white_bits, __ATOMIC_RELEASE);
	__atomic_store_n(&vk.window_headroom, headroom_bits, __ATOMIC_RELEASE);
	__atomic_store_n(&vk.window_hdr, enabled ? 1u : 0u, __ATOMIC_RELEASE);
	__atomic_store_n(&vk.window_hdr_changed, 1u, __ATOMIC_RELEASE);
}

void xgpu_vulkan_picture_top(int top)
{
	__atomic_store_n(&vk.picture_top, top < 0 ? 0 : top + 1, __ATOMIC_RELEASE);
}

void xgpu_vulkan_gamma(float gamma)
{
	uint32_t bits;

	memcpy(&bits, &gamma, sizeof(bits));
	__atomic_store_n(&vk.gamma_setting, bits, __ATOMIC_RELEASE);
}

/* a rectangle in the window, its way up, as it lies in a swapchain turned
quarter_turns clockwise from it (the frame's turn: present_turned) */
static void rectangle_turned(int quarter_turns, uint32_t window_width, uint32_t window_height, uint32_t *x,
	uint32_t *y, uint32_t *width, uint32_t *height)
{
	uint32_t left = *x, top = *y, across = *width, down = *height;

	switch (quarter_turns & 3)
	{
	case 1:
		*x = window_height - (top + down);
		*y = left;
		*width = down;
		*height = across;
		break;
	case 2:
		*x = window_width - (left + across);
		*y = window_height - (top + down);
		break;
	case 3:
		*x = top;
		*y = window_width - (left + across);
		*width = down;
		*height = across;
		break;
	}
}

void xgpu_vulkan_hdr_state(int active, int mode, int paper_white_nits, int paper_white, int ui_nits)
{
	vk.hdr_active = active != 0;
	vk.hdr_mode = mode == XGPU_HDR_MODE_PUNCHY ? XGPU_HDR_MODE_PUNCHY : XGPU_HDR_MODE_FAITHFUL;
	vk.hdr_paper_white_nits = paper_white_nits > 0 ? paper_white_nits : 0;
	vk.hdr_paper_white = paper_white > 0 ? paper_white : 100;
	vk.hdr_ui_nits = ui_nits > 0 ? ui_nits : 0;
	/* (the swapchain made again where its output changes) */
	if (vk.swapchain && hdr_output_choose() != vk.hdr_output)
		vk.swapchain_stale = 1;
}

/* paper white in nits, as the front end's state says */
static float paper_white_nits(void)
{
	return xgpu_hdr_paper_white_nits(vk.hdr_paper_white_nits, vk.hdr_paper_white, sdr_white_nits());
}

/* the nits the swapchain's encoding gives the system's SDR white. Windows'
compositor shows both encodings as they are, absolute: the SDR white's own
nits. A Wayland compositor maps the surface's reference white onto its own
(KWin's SDR brightness, which SDL gives as the SDR white: the patched
SDL's reference luminance), and the Linux drivers say none, so it is the
protocol's for the encoding: 203 nits for PQ (BT.2408's), scRGB's 1.0 (80
nits) for linear sRGB (wp_color_manager_v1's ext_linear, as Mesa makes
it). The highlights are the same ratio over it in either case. */
static float encoding_white_nits(void)
{
#ifdef _WIN32
	return sdr_white_nits();
#else
	return vk.hdr_output == XGPU_HDR_OUTPUT_SCRGB ? 80.0f : XGPU_HDR_REFERENCE_WHITE_NITS;
#endif
}

/* paper white over the system's SDR white (the setting's nits over the
SDR white's), no brighter than the display can show */
static float paper_white_setting(float headroom)
{
	float setting = paper_white_nits() / sdr_white_nits();

	if (!(setting > 0.0f))
		setting = 1.0f;
	return setting > headroom ? headroom : setting;
}

/* the window's headroom, at least 1 */
static float window_headroom(void)
{
	float headroom = float_of(__atomic_load_n(&vk.window_headroom, __ATOMIC_ACQUIRE));

	return headroom >= 1.0f ? headroom : 1.0f;
}

void xgpu_vulkan_hdr_frame(struct xgpu_hdr_frame *frame)
{
	float headroom = window_headroom(), setting, nits;

	memset(frame, 0, sizeof(*frame));
	frame->mode = vk.hdr_mode;
	frame->output = XGPU_HDR_OUTPUT_SDR;
	frame->paper_white = 1.0f;
	frame->peak = 1.0f;
	frame->ui = 1.0f;
	if (vk.hdr_output == XGPU_HDR_OUTPUT_SDR)
		return;
	setting = paper_white_setting(headroom);
	/* (in the encoding's nits) */
	nits = encoding_white_nits() * setting;
	frame->output = vk.hdr_output;
	frame->paper_white = vk.hdr_output == XGPU_HDR_OUTPUT_SCRGB ? nits / 80.0f : nits / 10000.0f;
	frame->peak = headroom / setting;
	/* (the UI's white over the system's SDR white as paper white's is: on
	Linux relative to the compositor's, no brighter than the display) */
	frame->ui = xgpu_hdr_ui_white((float)vk.hdr_ui_nits / sdr_white_nits(), setting, headroom);
}

/* the swapchain's HDR metadata (VK_EXT_hdr_metadata, where the device has
it), in the encoding's nits: the content's gamut sRGB's (the frame's
primaries), its light at most the display's peak, on average about paper
white */
static void hdr_metadata_set(void)
{
	VkHdrMetadataEXT metadata;
	float headroom = window_headroom();
	float peak = encoding_white_nits() * headroom;

	if (!vkSetHdrMetadataEXT || vk.hdr_output == XGPU_HDR_OUTPUT_SDR)
		return;
	memset(&metadata, 0, sizeof(metadata));
	metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
	metadata.displayPrimaryRed.x = 0.640f;
	metadata.displayPrimaryRed.y = 0.330f;
	metadata.displayPrimaryGreen.x = 0.300f;
	metadata.displayPrimaryGreen.y = 0.600f;
	metadata.displayPrimaryBlue.x = 0.150f;
	metadata.displayPrimaryBlue.y = 0.060f;
	metadata.whitePoint.x = 0.3127f;
	metadata.whitePoint.y = 0.3290f;
	metadata.maxLuminance = peak;
	metadata.minLuminance = 0.0f;
	metadata.maxContentLightLevel = peak;
	metadata.maxFrameAverageLightLevel = encoding_white_nits() * paper_white_setting(headroom);
	vkSetHdrMetadataEXT(vk.device, 1, &vk.swapchain, &metadata);
}

/* what changed from any thread (the window's size, the swap interval, the
display's HDR) taken up by the render thread: the swapchain made again for
it */
static void window_changes(void)
{
	uint32_t asked = __atomic_load_n(&vk.interval_asked, __ATOMIC_ACQUIRE);

	if (__atomic_exchange_n(&vk.window_resized, 0, __ATOMIC_ACQ_REL))
	{
		vk.swapchain_stale = 1;
		vk.resize_checks = RESIZE_CHECKS;
	}
	if (asked && (int)(asked - 1) != vk.interval)
	{
		/* (made again for its present mode: not for the refreshes a
		frame is shown for, xgpu_vulkan_frame_end's) */
		if (((int)(asked - 1) > 0) != (vk.interval > 0))
			vk.swapchain_stale = 1;
		vk.interval = (int)(asked - 1);
	}
	/* (display.hdr: the display told of again, and the swapchain made
	again where its output changes) */
	if (__atomic_exchange_n(&vk.window_hdr_changed, 0, __ATOMIC_ACQ_REL) && vk.swapchain)
	{
		display_hdr_tell();
		if (hdr_output_choose() != vk.hdr_output)
			vk.swapchain_stale = 1;
	}
}

/* the surface's extent the size the window was last told (either way
round: Android's is the display's way up), or none, the swapchain's then */
static int resize_told(VkExtent2D extent)
{
	uint64_t size = __atomic_load_n(&vk.window_size, __ATOMIC_ACQUIRE);
	uint32_t width = (uint32_t)(size >> 32), height = (uint32_t)size;

	return extent.width == 0xffffffffu || (extent.width == width && extent.height == height) ||
		(extent.width == height && extent.height == width);
}

static int swapchain_make(void)
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkSurfaceFormatKHR formats[64];
	VkPresentModeKHR modes[16];
	VkSwapchainCreateInfoKHR info;
	VkSemaphoreCreateInfo semaphore;
	uint32_t format_count = 64, mode_count = 16, index;
	VkSwapchainKHR old = vk.swapchain;
	VkColorSpaceKHR color_space;
	VkResult result;
	uint64_t size;
	int turns;

	window_changes();
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities);
	if (result == VK_ERROR_SURFACE_LOST_KHR)
	{
		surface_lost("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
		return 0;
	}
	if (!check(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
		return 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical, vk.surface, &format_count, formats);
	vkGetPhysicalDeviceSurfacePresentModesKHR(vk.physical, vk.surface, &mode_count, modes);
	if (!format_count)
		return 0;
	/* display.hdr: the HDR outputs the surface has (with the instance's
	colour spaces), told the host; the output the frames want */
	vk.surface_hdr = 0;
	for (index = 0; index < format_count && vk.colorspaces && vk.encode; index++)
	{
		if (formats[index].format == VK_FORMAT_R16G16B16A16_SFLOAT &&
			formats[index].colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
			vk.surface_hdr |= 1u << XGPU_HDR_OUTPUT_SCRGB;
		if (formats[index].format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
			formats[index].colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT)
			vk.surface_hdr |= 1u << XGPU_HDR_OUTPUT_PQ;
	}
	__atomic_store_n(&vk.window_hdr_changed, 0u, __ATOMIC_RELEASE);
	display_hdr_tell();
	vk.hdr_output = hdr_output_choose();
	color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	if (vk.hdr_output == XGPU_HDR_OUTPUT_SCRGB)
	{
		vk.swapchain_format = VK_FORMAT_R16G16B16A16_SFLOAT;
		color_space = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
	}
	else if (vk.hdr_output == XGPU_HDR_OUTPUT_PQ)
	{
		vk.swapchain_format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		color_space = VK_COLOR_SPACE_HDR10_ST2084_EXT;
	}
	else
	{
		/* (8 bits, sRGB's colour space: the first the surface lists, or
		of any format) */
		vk.swapchain_format = formats[0].format;
		color_space = formats[0].colorSpace;
		for (index = 0; index < format_count; index++)
		{
			if (formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				vk.swapchain_format = formats[index].format;
				color_space = formats[index].colorSpace;
				break;
			}
		}
		for (index = 0; index < format_count; index++)
		{
			if ((formats[index].format == VK_FORMAT_B8G8R8A8_UNORM || formats[index].format == VK_FORMAT_R8G8B8A8_UNORM) &&
				formats[index].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				vk.swapchain_format = formats[index].format;
				color_space = formats[index].colorSpace;
				break;
			}
		}
	}
	vk.present_mode = present_mode_choose(modes, mode_count, vk.interval);
	/* Turned (Android's landscape on a phone whose own way up is portrait):
	the swapchain is the display's way up, its width and height the
	surface's swapped for a quarter turn, and the frames are drawn turned
	into it (present_turned), so that the compositor has nothing to turn
	and no SUBOPTIMAL to report. Without a way to turn them, the swapchain
	is the window's way up and the compositor turns it. */
	turns = vk.rotate && (capabilities.supportedTransforms & capabilities.currentTransform) ?
		transform_turns(capabilities.currentTransform) : 0;
	vk.swapchain_extent = capabilities.currentExtent;
	if (vk.swapchain_extent.width == 0xffffffffu)
	{
		size = __atomic_load_n(&vk.window_size, __ATOMIC_ACQUIRE);
		vk.swapchain_extent.width = (uint32_t)(size >> 32) ? (uint32_t)(size >> 32) : 640;
		vk.swapchain_extent.height = (uint32_t)size ? (uint32_t)size : 480;
	}
	else if (turns & 1)
	{
		vk.swapchain_extent.width = capabilities.currentExtent.height;
		vk.swapchain_extent.height = capabilities.currentExtent.width;
	}
#ifdef __ANDROID__
	/* display.render_scale: the swapchain smaller than the surface, which
	Android's compositor scales up to it, as the game's frames are its size
	(host_sdl.c's window size) */
	if (capabilities.currentExtent.width != 0xffffffffu && host_app_render_scale() < 1.0f)
	{
		vk.swapchain_extent.width = (uint32_t)(vk.swapchain_extent.width * host_app_render_scale() + 0.5f) & ~1u;
		vk.swapchain_extent.height = (uint32_t)(vk.swapchain_extent.height * host_app_render_scale() + 0.5f) & ~1u;
		if (vk.swapchain_extent.width < capabilities.minImageExtent.width)
			vk.swapchain_extent.width = capabilities.minImageExtent.width;
		if (vk.swapchain_extent.height < capabilities.minImageExtent.height)
			vk.swapchain_extent.height = capabilities.minImageExtent.height;
	}
#endif
	if (!vk.swapchain_extent.width || !vk.swapchain_extent.height)
		return 0;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = vk.surface;
	/* (paced or bounded by the display, at most one frame waits for it
	while the next is drawn: three images, one on screen, one waiting, one
	drawn into. FIFO not paced by it (a display that says nothing of its
	presents): the least, two at least, so that the swapchain itself bounds
	the frames queued ahead of the display, beside the GPU's bound
	(xgpu_vulkan_pacing_frames_ahead). Else one more than the least, for
	the frames queued ahead) */
	info.minImageCount = capabilities.minImageCount + 1;
	if (vk.paced && vk.interval <= 1)
		info.minImageCount = capabilities.minImageCount > 3 ? capabilities.minImageCount : 3;
	else if (!vk.paced && vk.present_mode == VK_PRESENT_MODE_FIFO_KHR)
		info.minImageCount = capabilities.minImageCount > 2 ? capabilities.minImageCount : 2;
	if (capabilities.maxImageCount && info.minImageCount > capabilities.maxImageCount)
		info.minImageCount = capabilities.maxImageCount;
	if (info.minImageCount > 8)
		info.minImageCount = 8;
	info.imageFormat = vk.swapchain_format;
	info.imageColorSpace = color_space;
	info.imageExtent = vk.swapchain_extent;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = turns ? capabilities.currentTransform :
		(capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ?
		VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : capabilities.currentTransform;
	info.compositeAlpha = (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : (VkCompositeAlphaFlagBitsKHR)(capabilities.supportedCompositeAlpha &
		-capabilities.supportedCompositeAlpha);
	info.presentMode = vk.present_mode;
	info.clipped = VK_TRUE;
	info.oldSwapchain = old;
	if (old)
	{
		/* (display.low_latency's thread let go of the one before first: a
		swapchain made from it must have it to itself, and the thread's
		calls on it, display timing's, crashed gamescope's layer on the Steam
		Deck as the new one was made) */
		xgpu_vulkan_pacing_swapchain(VK_NULL_HANDLE);
		vkDeviceWaitIdle(vk.device);
		for (index = 0; index < vk.swapchain_image_count; index++)
		{
			if (vk.presentable[index])
				vkDestroySemaphore(vk.device, vk.presentable[index], NULL);
			vk.presentable[index] = VK_NULL_HANDLE;
		}
	}
	result = vkCreateSwapchainKHR(vk.device, &info, NULL, &vk.swapchain);
	if (result < 0)
	{
		vk.swapchain = VK_NULL_HANDLE;
		vk.swapchain_image_count = 0;
		xgpu_vulkan_pacing_swapchain(VK_NULL_HANDLE);
		if (old)
			vkDestroySwapchainKHR(vk.device, old, NULL);
		if (result == VK_ERROR_SURFACE_LOST_KHR)
			surface_lost("vkCreateSwapchainKHR");
		else
			check(result, "vkCreateSwapchainKHR");
		return 0;
	}
	/* (the presents to the one before no longer waited for) */
	xgpu_vulkan_pacing_swapchain(vk.swapchain);
	if (old)
		vkDestroySwapchainKHR(vk.device, old, NULL);
	vk.swapchain_image_count = 8;
	vkGetSwapchainImagesKHR(vk.device, vk.swapchain, &vk.swapchain_image_count, vk.swapchain_images);
	memset(&semaphore, 0, sizeof(semaphore));
	semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (index = 0; index < vk.swapchain_image_count; index++)
		vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.presentable[index]);
	vk.swapchain_stale = 0;
	vk.swapchain_turns = turns;
	vk.surface_extent = capabilities.currentExtent;
	/* (the window's new size come: RESIZE_CHECKS done) */
	if (resize_told(capabilities.currentExtent))
		vk.resize_checks = 0;
	vk.surface_transform = capabilities.currentTransform;
	vk.suboptimal_presents = 0;
	hdr_metadata_set();
	host_logf(HOST_LOG_INFO, "vulkan: swapchain %ux%u, %u images, present mode %s, turned %d%s", vk.swapchain_extent.width,
		vk.swapchain_extent.height, vk.swapchain_image_count, present_mode_name(vk.present_mode), turns * 90,
		vk.hdr_output == XGPU_HDR_OUTPUT_SCRGB ? ", HDR (scRGB)" : vk.hdr_output == XGPU_HDR_OUTPUT_PQ ? ", HDR (HDR10)" : "");
	return 1;
}

void xgpu_vulkan_window_resized(uint32_t width, uint32_t height)
{
	__atomic_store_n(&vk.window_size, (uint64_t)width << 32 | height, __ATOMIC_RELEASE);
	__atomic_store_n(&vk.window_resized, 1, __ATOMIC_RELEASE);
}

void xgpu_vulkan_window_hidden(int hidden)
{
	__atomic_store_n(&vk.window_hidden, hidden ? 1 : 0, __ATOMIC_RELEASE);
}

void xgpu_vulkan_window_low_latency(int mode)
{
	__atomic_store_n(&vk.low_latency, (uint32_t)(mode >= 0 && mode <= 2 ? mode : 0), __ATOMIC_RELEASE);
}

void xgpu_vulkan_window_interval(int interval)
{
	__atomic_store_n(&vk.interval_asked, (uint32_t)(interval > 0 ? (interval < 8 ? interval : 8) : 0) + 1,
		__ATOMIC_RELEASE);
}

/* 1 if the surface's extent or transform changed from the swapchain's
(which making it again mends) or the surface is lost */
static int surface_changed(void)
{
	VkSurfaceCapabilitiesKHR capabilities;
	VkResult result;

	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities);
	if (result == VK_ERROR_SURFACE_LOST_KHR)
	{
		surface_lost("vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
		return 1;
	}
	if (!check(result, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
		return 1;
	return capabilities.currentTransform != vk.surface_transform ||
		(capabilities.currentExtent.width != 0xffffffffu &&
		(capabilities.currentExtent.width != vk.surface_extent.width ||
		capabilities.currentExtent.height != vk.surface_extent.height));
}

/* a SUBOPTIMAL present: surface_changed, else the present is fine as it is
(a compositor that turns or scales it), and the surface is asked again
only after SUBOPTIMAL_CHECKS more */
static int swapchain_changed(void)
{
	if (vk.suboptimal_presents++ % SUBOPTIMAL_CHECKS)
		return 0;
	return surface_changed();
}

void xgpu_vulkan_window_suspend(int suspended)
{
	if (!vk.device)
		return;
	if (suspended)
	{
		if (!vk.window_suspended && vk.surface_create)
			host_logf(HOST_LOG_INFO, "vulkan: the app is in the background: the window's surface is let go of");
		vk.window_suspended = 1;
		vkDeviceWaitIdle(vk.device);
		surface_forget();
		xgpu_vulkan_pipeline_cache_save();
		return;
	}
	if (vk.window_suspended)
		host_logf(HOST_LOG_INFO, "vulkan: the app is back: the window's surface is made again");
	vk.window_suspended = 0;
	vk.surface_failing = 1;
	surface_remake();
}

/* the frame turned for a swapchain made for the display's way up: the
turned image (the frame's own if it cannot be turned, and no later
swapchain is turned); the targets and visibility test as they were */
static struct xgpu_vulkan_image *present_turned(struct xgpu_vulkan_image *image)
{
	struct xgpu_vulkan_image *color = vk.color, *depth = vk.depth, *turned;
	uint32_t layer = vk.layer, views = vk.views;
	int test_active = vk.test_active;

	pass_end();
	vk.test_active = 0;
	turned = vk.rotate(image, vk.swapchain_turns);
	pass_end();
	vk.test_active = test_active;
	xgpu_vulkan_set_target_views(color, depth, layer, views);
	if (turned)
		return turned;
	host_logf(HOST_LOG_WARN, "vulkan: the frame cannot be turned for the display: the compositor turns it");
	vk.rotate = NULL;
	vk.swapchain_stale = 1;
	return image;
}

/* display.hdr: the frame encoded for an HDR swapchain (the options'
encode, into an image of its own of the swapchain's format); the targets
and visibility test as they were. NULL if it cannot be (logged; the
swapchain is SDR from then on, and the display told it shows no HDR). */
static struct xgpu_vulkan_image *present_encoded(struct xgpu_vulkan_image *image)
{
	struct xgpu_vulkan_image *color = vk.color, *depth = vk.depth, *encoded;
	uint32_t layer = vk.layer, views = vk.views;
	int test_active = vk.test_active;
	struct xgpu_hdr_frame frame;

	xgpu_vulkan_hdr_frame(&frame);
	pass_end();
	vk.test_active = 0;
	encoded = vk.encode(image, &frame, vk.hdr_output == XGPU_HDR_OUTPUT_SCRGB ? XGPU_VULKAN_FORMAT_RGBA16F :
		XGPU_VULKAN_FORMAT_RGB10A2);
	pass_end();
	vk.test_active = test_active;
	xgpu_vulkan_set_target_views(color, depth, layer, views);
	if (encoded)
		return encoded;
	host_logf(HOST_LOG_WARN, "vulkan: the frame cannot be encoded for the HDR swapchain: the window is SDR");
	vk.encode = NULL;
	vk.swapchain_stale = 1;
	return NULL;
}

/* display.gamma: an SDR frame curved by one over the setting (the
options' gamma, into an image of its own); the frame itself where the
setting is 1 (none: nothing drawn, so the frame is the swapchain's exactly)
or the curve cannot be drawn; the targets and visibility test as they
were */
static struct xgpu_vulkan_image *present_curved(struct xgpu_vulkan_image *image)
{
	struct xgpu_vulkan_image *color = vk.color, *depth = vk.depth, *curved;
	uint32_t layer = vk.layer, views = vk.views;
	int test_active = vk.test_active;
	uint32_t bits = __atomic_load_n(&vk.gamma_setting, __ATOMIC_ACQUIRE);
	float gamma;

	memcpy(&gamma, &bits, sizeof(gamma));
	if (!(gamma > 0.0f) || gamma == 1.0f)
		return image;
	pass_end();
	vk.test_active = 0;
	curved = vk.gamma(image, 1.0f / gamma);
	pass_end();
	vk.test_active = test_active;
	xgpu_vulkan_set_target_views(color, depth, layer, views);
	return curved ? curved : image;
}

/* the image shown in the window: letterboxed into a swapchain image
(cleared black), then presented. 0 if nothing is presented: no surface (the
app in the background, the window not back yet), a swapchain that cannot
be made; the caller then submits the frame's work itself. */
static uint64_t present(struct xgpu_vulkan_image *image, int frame)
{
	int trace = host_load_trace_detail_enabled();
	VkSemaphore acquired;
	VkCommandBuffer commands;
	VkImageMemoryBarrier transition;
	VkClearColorValue black;
	VkImageSubresourceRange whole;
	VkImageBlit blit;
	VkPresentInfoKHR info;
	VkResult result;
	uint32_t index, width, height, x, y, window_width, window_height, image_width, image_height;
	int turns = 0, top;

	uint64_t serial;

	if (__atomic_load_n(&vk.window_hidden, __ATOMIC_ACQUIRE) || (!vk.surface && !surface_remake()))
		return 0;
	window_changes();
	/* (a resize's new size, told late: RESIZE_CHECKS) */
	if (vk.resize_checks && vk.swapchain && !vk.swapchain_stale && !(--vk.resize_checks % RESIZE_CHECK_EVERY))
	{
		if (surface_changed())
			vk.swapchain_stale = 1;
		if (!vk.surface)
			return 0;
	}
	if (vk.swapchain_stale || !vk.swapchain)
	{
		if (!swapchain_make())
			return 0;
	}
	if (image && vk.swapchain_turns && vk.rotate)
	{
		image = present_turned(image);
		/* (none if it could not be turned: vk.rotate is let go of) */
		turns = vk.rotate ? vk.swapchain_turns : 0;
	}
	if (image && vk.hdr_output != XGPU_HDR_OUTPUT_SDR && vk.encode)
	{
		image = present_encoded(image);
		if (!image)
			return 0;
	}
	else if (image && vk.hdr_output == XGPU_HDR_OUTPUT_SDR && vk.gamma)
		image = present_curved(image);
	/* (the slot's semaphore free: the recording begun, which waits for the
	slot's last submission) */
	if (trace)
		host_load_trace("vulkan.present.slot.begin", "");
	commands = main_commands();
	if (trace)
		host_load_trace("vulkan.present.slot.end", "");
	acquired = vk.acquired[vk.serial % SLOTS];
	if (trace)
		host_load_trace("vulkan.acquire.begin", "");
	/* (in parts, never for good: a swapchain that gives no image for
	seconds is noted, and display.low_latency's thread stops waiting for its
	presents, which might be what holds it, xgpu_vulkan_pacing_silence) */
	{
		uint64_t waited = 0;

		for (;;)
		{
			result = xgpu_vulkan_pacing_acquire(vkAcquireNextImageKHR, vk.device, vk.swapchain, ACQUIRE_PART_NS,
				acquired, &index);
			if (result != VK_TIMEOUT && result != VK_NOT_READY)
				break;
			waited += ACQUIRE_PART_NS;
			if (waited == ACQUIRE_STALL_NS)
			{
				host_logf(HOST_LOG_WARN, "vulkan: the swapchain has given no image for %llu ms",
					(unsigned long long)(waited / 1000000ull));
				xgpu_vulkan_pacing_silence();
			}
		}
	}
	if (trace)
		host_load_trace("vulkan.acquire.end", "result=%d", result);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		vk.swapchain_stale = 1;
		return 0;
	}
	if (result == VK_ERROR_SURFACE_LOST_KHR)
	{
		surface_lost("vkAcquireNextImageKHR");
		return 0;
	}
	if (!check(result, "vkAcquireNextImageKHR"))
		return 0;
	memset(&whole, 0, sizeof(whole));
	whole.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	whole.levelCount = 1;
	whole.layerCount = 1;
	memset(&transition, 0, sizeof(transition));
	transition.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	transition.srcAccessMask = 0;
	transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	transition.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transition.srcQueueFamilyIndex = transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	transition.image = vk.swapchain_images[index];
	transition.subresourceRange = whole;
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
		&transition);
	memset(&black, 0, sizeof(black));
	black.float32[3] = 1.0f;
	vkCmdClearColorImage(commands, vk.swapchain_images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole);
	barrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT);
	if (image)
	{
		/* letterboxed to the image's shape, in the window as it is seen
		(the swapchain and the image turned back): centered, or at the top
		of a taller window, as the game says (d3d8_device.c's
		picture_top_in: display.picture_position, a portrait window); then
		turned with the frame */
		window_width = turns & 1 ? vk.swapchain_extent.height : vk.swapchain_extent.width;
		window_height = turns & 1 ? vk.swapchain_extent.width : vk.swapchain_extent.height;
		image_width = turns & 1 ? image->description.height : image->description.width;
		image_height = turns & 1 ? image->description.width : image->description.height;
		width = window_width;
		height = (uint32_t)((uint64_t)width * image_height / image_width);
		if (height > window_height)
		{
			height = window_height;
			width = (uint32_t)((uint64_t)height * image_width / image_height);
		}
		x = (window_width - width) / 2;
		top = __atomic_load_n(&vk.picture_top, __ATOMIC_ACQUIRE);
		y = !top ? (window_height - height) / 2 : (uint32_t)(top - 1) < window_height - height ?
			(uint32_t)(top - 1) : window_height - height;
		rectangle_turned(turns, window_width, window_height, &x, &y, &width, &height);
		memset(&blit, 0, sizeof(blit));
		blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
		blit.srcOffsets[1].x = (int32_t)image->description.width;
		blit.srcOffsets[1].y = (int32_t)image->description.height;
		blit.srcOffsets[1].z = 1;
		blit.dstOffsets[0].x = (int32_t)x;
		blit.dstOffsets[0].y = (int32_t)y;
		blit.dstOffsets[1].x = (int32_t)(x + width);
		blit.dstOffsets[1].y = (int32_t)(y + height);
		blit.dstOffsets[1].z = 1;
		image->used = vk.serial;
		image_use(image);
		vkCmdBlitImage(commands, image->image, VK_IMAGE_LAYOUT_GENERAL, vk.swapchain_images[index],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
	}
	transition.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	transition.dstAccessMask = 0;
	transition.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	transition.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
		1, &transition);
	if (trace)
		host_load_trace("vulkan.present.submit.begin", "");
	serial = submit(acquired, vk.presentable[index]);
	if (trace)
		host_load_trace("vulkan.present.submit.end", "");
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	info.waitSemaphoreCount = 1;
	info.pWaitSemaphores = &vk.presentable[index];
	info.swapchainCount = 1;
	info.pSwapchains = &vk.swapchain;
	info.pImageIndices = &index;
	if (trace)
		host_load_trace("vulkan.queue_present.begin", "");
	result = xgpu_vulkan_pacing_present(vkQueuePresentKHR, vk.queue, &info, frame, serial);
	if (trace)
		host_load_trace("vulkan.queue_present.end", "result=%d", result);
	host_load_trace("vulkan.present.return", "result=%d image=%d", result, image != NULL);
	if (result == VK_ERROR_SURFACE_LOST_KHR)
		surface_lost("vkQueuePresentKHR");
	else if (result == VK_ERROR_OUT_OF_DATE_KHR || (result == VK_SUBOPTIMAL_KHR && swapchain_changed()))
		vk.swapchain_stale = 1;
	else
		check(result, "vkQueuePresentKHR");
	return serial;
}

int xgpu_vulkan_loading_resources(int window)
{
	return vk.loading_image && (!window || (vk.surface && vk.swapchain));
}

void xgpu_vulkan_loading_art(void *context, const uint8_t *rgba)
{
	(void)context;
	loading_art_set(&vk.loading_art, rgba);
}

void xgpu_vulkan_loading_scene(void *context, const void *data, uint32_t bytes)
{
	(void)context;
	loading_art_scene(&vk.loading_art, bytes ? data : NULL, bytes);
}

void xgpu_vulkan_loading_progress(void *context, float progress)
{
	(void)context;
	loading_art_progress(&vk.loading_art, progress);
}

void xgpu_vulkan_loading_mode(void *context, int follow_progress)
{
	(void)context;
	loading_art_follow(&vk.loading_art, follow_progress ? 1u : 0u);
}

void xgpu_vulkan_loading_caption(void *context, const unsigned char *coverage, uint32_t width, uint32_t height)
{
	(void)context;
	loading_art_caption(&vk.loading_art, coverage, width, height);
}

void xgpu_vulkan_loading_stereo(int stereo)
{
	loading_art_stereo(&vk.loading_art, stereo ? 1u : 0u);
}

int xgpu_vulkan_loading_stereo_drawn(void)
{
	return vk.loading_stereo_drawn;
}

void xgpu_vulkan_loading_eyes(const float left[3], const float right[3])
{
	loading_art_stereo_eyes(&vk.loading_art, left, right);
}

float xgpu_vulkan_loading_stereo_tangent(void)
{
	return loading_art_stereo_tangent(&vk.loading_art);
}

/* the loading frame's width for the window (its swapchain as it is shown:
turned for a turned screen), loading_art_width's */
uint32_t xgpu_vulkan_loading_frame_width(void)
{
	uint32_t width = vk.swapchain_extent.width, height = vk.swapchain_extent.height;

	if (vk.swapchain_turns && vk.rotate)
	{
		width = vk.swapchain_extent.height;
		height = vk.swapchain_extent.width;
	}
	loading_art_width(&vk.loading_art, width, height);
	return loading_art_frame_width(&vk.loading_art);
}

/* and its height: the screen's rows, where the loading screen draws its
sharp frame (loading_art_hd), else 480 */
uint32_t xgpu_vulkan_loading_frame_height(void)
{
	return loading_art_frame_height(&vk.loading_art);
}

/* the window's size as it is shown (turned for a turned screen), where the
loading screen has a caption, which is drawn at its pixels (the frame
scaled up to them under it); else 0 */
int xgpu_vulkan_loading_caption_size(uint32_t *width, uint32_t *height)
{
	*width = vk.swapchain_extent.width;
	*height = vk.swapchain_extent.height;
	if (vk.swapchain_turns && vk.rotate)
	{
		*width = vk.swapchain_extent.height;
		*height = vk.swapchain_extent.width;
	}
	return vk.loading_art.caption && *width && *height && *width <= 16384 && *height <= 16384;
}

/* The loading screen's caption drawn over the frame `pixels` (the last
render's) where it was scaled into the loading image, `width` by `height`
at x, y: at the image's pixels (loading_art_caption_shown). */
static void loading_caption_draw(const uint8_t *pixels, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	struct xgpu_vulkan_image_description description;
	unsigned int left, top, tile_width, tile_height;
	const uint8_t *tile = loading_art_caption_shown(&vk.loading_art, pixels, width, height, &left, &top,
		&tile_width, &tile_height);
	VkCommandBuffer commands;
	VkImageBlit blit;

	if (!tile)
		return;
	if (vk.loading_caption_image && (vk.loading_caption_image->description.width < tile_width ||
		vk.loading_caption_image->description.height < tile_height))
	{
		xgpu_vulkan_image_destroy(vk.loading_caption_image);
		vk.loading_caption_image = NULL;
	}
	if (!vk.loading_caption_image)
	{
		memset(&description, 0, sizeof(description));
		description.type = XGPU_VULKAN_IMAGE_2D;
		description.format = XGPU_VULKAN_FORMAT_RGBA8;
		description.usage = XGPU_VULKAN_USAGE_SAMPLED;
		description.width = tile_width;
		description.height = tile_height;
		description.depth_or_layers = description.levels = 1;
		vk.loading_caption_image = xgpu_vulkan_image_create(&description);
		if (!vk.loading_caption_image)
			return;
	}
	pass_end();
	xgpu_vulkan_image_upload(vk.loading_caption_image, 0, 0, 0, 0, 0, tile_width, tile_height, 1, tile,
		tile_width * 4, 0);
	commands = main_commands();
	/* (a blit of the same size: pixel for pixel, the image's channels'
	order made the loading image's) */
	memset(&blit, 0, sizeof(blit));
	blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
	blit.srcOffsets[1].x = (int32_t)tile_width;
	blit.srcOffsets[1].y = (int32_t)tile_height;
	blit.srcOffsets[1].z = 1;
	blit.dstOffsets[0].x = (int32_t)(x + left);
	blit.dstOffsets[0].y = (int32_t)(y + top);
	blit.dstOffsets[1].x = (int32_t)(x + left + tile_width);
	blit.dstOffsets[1].y = (int32_t)(y + top + tile_height);
	blit.dstOffsets[1].z = 1;
	vk.loading_caption_image->used = vk.loading_image->used = vk.serial;
	image_use(vk.loading_caption_image);
	image_use(vk.loading_image);
	image_written(vk.loading_image);
	vkCmdBlitImage(commands, vk.loading_caption_image->image, VK_IMAGE_LAYOUT_GENERAL, vk.loading_image->image,
		VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
	transfer_done(commands);
}

struct xgpu_vulkan_image *xgpu_vulkan_loading_draw(const struct loading_presentation_frame *frame,
	uint32_t width, uint32_t height, uint32_t format)
{
	static const float background[4] = { LOADING_PRESENTATION_RED, LOADING_PRESENTATION_GREEN,
		LOADING_PRESENTATION_BLUE, 1.0f };
	static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	struct xgpu_vulkan_image_description description;
	struct xgpu_vulkan_image *color = vk.color, *depth = vk.depth;
	uint32_t layer = vk.layer, views = vk.views, index;
	int test_active = vk.test_active;
	int32_t rectangle[1][4];
	const uint8_t *pixels;
	uint32_t art_width, art_height;
	int stereo;

	vk.loading_stereo_drawn = 0;
	if (!frame || !frame->active)
	{
		xgpu_vulkan_image_destroy(vk.loading_image);
		vk.loading_image = NULL;
		return NULL;
	}
	if (!width || !height || (format != XGPU_VULKAN_FORMAT_BGRA8 && format != XGPU_VULKAN_FORMAT_RGBA8))
		return NULL;
	if (vk.loading_image && (vk.loading_image->description.width != width ||
		vk.loading_image->description.height != height || vk.loading_image->description.format != format))
	{
		xgpu_vulkan_image_destroy(vk.loading_image);
		vk.loading_image = NULL;
	}
	if (!vk.loading_image)
	{
		memset(&description, 0, sizeof(description));
		description.type = XGPU_VULKAN_IMAGE_2D;
		description.format = (uint8_t)format;
		description.usage = XGPU_VULKAN_USAGE_TARGET | XGPU_VULKAN_USAGE_SAMPLED;
		description.width = width;
		description.height = height;
		description.depth_or_layers = description.levels = 1;
		vk.loading_image = xgpu_vulkan_image_create(&description);
		if (!vk.loading_image)
			return NULL;
	}
	pixels = loading_art_render(&vk.loading_art, frame);
	/* (the picture's size: the sharp frame's, or the 480 rows') */
	art_width = loading_art_output_width(&vk.loading_art);
	art_height = loading_art_output_height(&vk.loading_art);
	/* (VR's stereo pair side by side, where the image holds them) */
	stereo = pixels && loading_art_stereo_drawn(&vk.loading_art) &&
		width >= 2 * LOADING_PRESENTATION_VR_EYE_WIDTH && height >= LOADING_PRESENTATION_VR_EYE_HEIGHT;
	if (pixels && vk.loading_art_image && (vk.loading_art_image->description.width < art_width ||
		vk.loading_art_image->description.height < art_height))
	{
		xgpu_vulkan_image_destroy(vk.loading_art_image);
		vk.loading_art_image = NULL;
	}
	if (pixels && !vk.loading_art_image)
	{
		memset(&description, 0, sizeof(description));
		description.type = XGPU_VULKAN_IMAGE_2D;
		description.format = XGPU_VULKAN_FORMAT_RGBA8;
		description.usage = XGPU_VULKAN_USAGE_SAMPLED;
		description.width = 2 * LOADING_ART_FRAME_WIDTH > LOADING_ART_FRAME_MAXIMUM_WIDTH ? 2 * LOADING_ART_FRAME_WIDTH :
			LOADING_ART_FRAME_MAXIMUM_WIDTH;
		description.height = LOADING_ART_FRAME_HEIGHT;
		if (description.width < art_width)
			description.width = art_width;
		if (description.height < art_height)
			description.height = art_height;
		description.depth_or_layers = description.levels = 1;
		vk.loading_art_image = xgpu_vulkan_image_create(&description);
	}
	if (!vk.loading_art_image)
		pixels = NULL;
	/* A loading pass must not contribute samples or advance the game's
	visibility frame. Ending and restoring the target also restores the
	next draw's render-pass state; no pipeline or guest buffer is needed. */
	pass_end();
	vk.test_active = 0;
	xgpu_vulkan_set_targets(vk.loading_image, NULL, 0);
	rectangle[0][0] = rectangle[0][1] = 0;
	rectangle[0][2] = (int32_t)width;
	rectangle[0][3] = (int32_t)height;
	xgpu_vulkan_clear(rectangle, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS,
		pixels ? black : background, 0.0f, 0);
	if (pixels)
	{
		VkCommandBuffer commands;
		VkImageBlit blit;
		uint32_t fit_width = width;
		uint32_t fit_height = (uint32_t)((uint64_t)width * art_height / art_width);
		uint32_t x, y;

		if (fit_height > height)
		{
			fit_height = height;
			fit_width = (uint32_t)((uint64_t)height * art_width / art_height);
		}
		if (!fit_width)
			fit_width = 1;
		if (!fit_height)
			fit_height = 1;
		x = (width - fit_width) / 2;
		y = (height - fit_height) / 2;
		if (stereo)
		{
			/* each eye's picture as it is, at the image's top left */
			x = y = 0;
			fit_width = 2 * LOADING_ART_FRAME_WIDTH;
			fit_height = LOADING_ART_FRAME_HEIGHT;
		}
		pass_end();
		xgpu_vulkan_image_upload(vk.loading_art_image, 0, 0, 0, 0, 0,
			art_width, art_height, 1, pixels, art_width * 4, 0);
		if (stereo)
			xgpu_vulkan_image_upload(vk.loading_art_image, 0, 0, LOADING_ART_FRAME_WIDTH, 0, 0,
				LOADING_ART_FRAME_WIDTH, LOADING_ART_FRAME_HEIGHT, 1, vk.loading_art.output_right,
				LOADING_ART_FRAME_WIDTH * 4, 0);
		commands = main_commands();
		memset(&blit, 0, sizeof(blit));
		blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
		blit.srcOffsets[1].x = stereo ? 2 * LOADING_ART_FRAME_WIDTH : (int32_t)art_width;
		blit.srcOffsets[1].y = stereo ? LOADING_ART_FRAME_HEIGHT : (int32_t)art_height;
		blit.srcOffsets[1].z = 1;
		blit.dstOffsets[0].x = (int32_t)x;
		blit.dstOffsets[0].y = (int32_t)y;
		blit.dstOffsets[1].x = (int32_t)(x + fit_width);
		blit.dstOffsets[1].y = (int32_t)(y + fit_height);
		blit.dstOffsets[1].z = 1;
		vk.loading_art_image->used = vk.loading_image->used = vk.serial;
		image_use(vk.loading_art_image);
		image_use(vk.loading_image);
		image_written(vk.loading_image);
		vkCmdBlitImage(commands, vk.loading_art_image->image, VK_IMAGE_LAYOUT_GENERAL,
			vk.loading_image->image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
		transfer_done(commands);
		vk.loading_stereo_drawn = stereo;
		/* (the caption over it, at the image's pixels: the frame under it
		as the blit scaled it) */
		if (!stereo)
			loading_caption_draw(pixels, x, y, fit_width, fit_height);
	}
	else
	{
		for (index = 0; index < frame->count && index < LOADING_PRESENTATION_RECTS; index++)
		{
			const struct loading_presentation_rect *source = &frame->rectangles[index];

			rectangle[0][0] = (int32_t)((int64_t)source->x * width / LOADING_PRESENTATION_WIDTH);
			rectangle[0][1] = (int32_t)((int64_t)source->y * height / LOADING_PRESENTATION_HEIGHT);
			rectangle[0][2] = (int32_t)((int64_t)source->width * width / LOADING_PRESENTATION_WIDTH);
			rectangle[0][3] = (int32_t)((int64_t)source->height * height / LOADING_PRESENTATION_HEIGHT);
			xgpu_vulkan_clear(rectangle, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS, source->color, 0.0f, 0);
		}
	}
	pass_end();
	vk.test_active = test_active;
	xgpu_vulkan_set_target_views(color, depth, layer, views);
	return vk.loading_image;
}

void xgpu_vulkan_loading_present(struct xgpu_vulkan_image *image)
{
	pass_end();
	if (!present(image, 0))
		xgpu_vulkan_fence();
	completed_serial();
	garbage_collect();
}

/* display.low_latency: the game paced by the display, or not, from this
frame on (xgpu_vulkan_pacing.c); the swapchain made again for its images */
static void frame_pacing_update(void)
{
	int fifo, paced;

	window_changes();
	/* (FIFO: vsync's, or the only mode a surface has) */
	fifo = vk.swapchain ? vk.present_mode == VK_PRESENT_MODE_FIFO_KHR : vk.interval > 0;
	paced = xgpu_vulkan_pacing_mode((int)__atomic_load_n(&vk.low_latency, __ATOMIC_ACQUIRE), vk.interval, fifo,
		vk.frame % 120 ? 0 : host_vulkan_display_period());
	if (!paced != !vk.paced && vk.swapchain && vk.interval <= 1)
		vk.swapchain_stale = 1;
	vk.paced = paced;
}

/* the frames ahead of the GPU bounded (xgpu_vulkan_pacing_frames_ahead):
where the window's presents queue for the display (FIFO), the game starts a
frame only once the frame before is done on the GPU, whatever the display
says of them (issue #274: NVIDIA's Windows driver queued five refreshes of
them); the guest's own bound else (mailbox and immediate replace or tear,
and the headset paces the VR mode). With a display that says nothing of its
presents, on gamescope's headless 60 Hz, the press reached the screen in
57 ms, against 96 ms unbounded (68 ms with one frame more allowed) */
static void frames_ahead_update(int present_window)
{
	int ahead = present_window && vk.swapchain && vk.present_mode == VK_PRESENT_MODE_FIFO_KHR ? 0 : -1;

	if (ahead + 2 == vk.frames_ahead)
		return;
	vk.frames_ahead = ahead + 2;
	xgpu_vulkan_pacing_frames_ahead(ahead);
	host_logf(HOST_LOG_INFO, "vulkan: the game's frames %s", !ahead ?
		"start once the frame before is done on the GPU (FIFO)" : "ahead of the GPU as the stream allows");
}

void xgpu_vulkan_frame_end(struct xgpu_vulkan_image *image, int present_window)
{
	uint64_t serial;
	int gpu_time_armed = vk.gpu_time_armed, repeat;

	/* VR arms its next interval before this frame's visibility maintenance.
	Keep that arm for real rendering after the next runtime frame wait. */
	vk.gpu_time_armed = 0;
	if (vk.frame_timing)
		xgpu_vulkan_gpu_time_end();
	draw_times_frame_end();
	if (vk.queries && vk.queries->counter_count)
		barrier(main_commands(), VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
	frame_pacing_update();
	frames_ahead_update(present_window);
	/* (the frame's last work, its tests' queries with it: the present's
	submission, or one of its own; with discards, its last passes' stores
	chosen as the frame's) */
	vk.frame_ending = vk.deferred;
	serial = present_window ? present(image, 1) : 0;
	if (!serial)
		xgpu_vulkan_pacing_unshown();
	/* (a swap interval of more than one, battery saver's: the frame
	presented again for each refresh after its first, FIFO showing one a
	refresh, so that it stays on screen as long as every other; the render
	thread, and so the game, waits for the display meanwhile) */
	for (repeat = 1; serial && repeat < vk.interval && vk.present_mode == VK_PRESENT_MODE_FIFO_KHR; repeat++)
	{
		uint64_t again = present(image, 0);

		if (!again)
			break;
		serial = again;
	}
	if (!serial)
	{
		recording_begin();
		serial = submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
	}
	vk.frame_ending = 0;
	if (draw_times.pending && !draw_times.serial)
		draw_times.serial = serial;
	/* (after the present: its submission plays the frame's last passes) */
	pass_times_frame_end();
	timing_frame_end();
	if (pass_times.pending && !pass_times.serial)
		pass_times.serial = serial;
	if (vk.queries)
	{
		vk.queries->serial = serial;
		vk.queries = NULL;
	}
	/* (frame k - 2's counts, at frame k's end) */
	if (vk.frame >= 2)
		queries_publish(vk.frame - 2);
	vk.frame++;
	vk.statistics.frames++;
	completed_serial();
	draw_times_collect();
	pass_times_collect();
	garbage_collect();
	vk.gpu_time_armed = gpu_time_armed;
	if (vk.frame_timing)
	{
		double milliseconds, now = seconds_now();

		while (xgpu_vulkan_gpu_time_read(&milliseconds))
		{
			vk.statistics.gpu_milliseconds += milliseconds;
			vk.statistics.gpu_frames++;
			vk.gpu_nanoseconds += (uint64_t)(milliseconds * 1e6);
		}
		if (vk.frame_ended > 0.0)
		{
			vk.statistics.frame_seconds += now - vk.frame_ended;
			vk.statistics.frames_timed++;
		}
		vk.frame_ended = now;
		/* (from the next frame's first work) */
		xgpu_vulkan_gpu_time_begin();
	}
}

uint64_t xgpu_vulkan_gpu_nanoseconds(int *measured)
{
	*measured = vk.frame_timing && vk.capabilities.gpu_timestamps && !vk.gpu_time_failed;
	return vk.gpu_nanoseconds;
}

void xgpu_vulkan_statistics_peek(struct xgpu_vulkan_statistics *statistics)
{
	*statistics = vk.statistics;
}

void xgpu_vulkan_statistics(struct xgpu_vulkan_statistics *statistics)
{
	*statistics = vk.statistics;
	memset(&vk.statistics, 0, sizeof(vk.statistics));
}

void xgpu_vulkan_capabilities(struct xgpu_vulkan_capabilities *capabilities)
{
	*capabilities = vk.capabilities;
}

/* ---------- initialization */

static int load_functions(void)
{
#ifdef _WIN32
	if (!vk.library)
		vk.library = LoadLibraryExW(L"vulkan-1.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!vk.library)
	{
		host_logf(HOST_LOG_ERROR, "vulkan: no Vulkan loader (vulkan-1.dll, error %lu)", (unsigned long)GetLastError());
		return 0;
	}
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(vk.library, "vkGetInstanceProcAddr");
#else
	const char *names[] = { "libvulkan.so.1", "libvulkan.so" };
	size_t index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]) && !vk.library; index++)
		vk.library = dlopen(names[index], RTLD_NOW | RTLD_LOCAL);
	if (!vk.library)
	{
		host_logf(HOST_LOG_ERROR, "vulkan: no Vulkan loader (libvulkan.so.1): %s", dlerror());
		return 0;
	}
	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(vk.library, "vkGetInstanceProcAddr");
#endif
	if (!vkGetInstanceProcAddr)
		return 0;
#define LOAD(name) name = (PFN_##name)vkGetInstanceProcAddr(NULL, #name);
	GLOBAL_FUNCTIONS(LOAD)
#undef LOAD
	return vkCreateInstance != NULL;
}

static int has_layer(const char *name)
{
	VkLayerProperties layers[64];
	uint32_t count = 64, index;

	if (vkEnumerateInstanceLayerProperties(&count, layers) < 0)
		return 0;
	for (index = 0; index < count; index++)
	{
		if (!strcmp(layers[index].layerName, name))
			return 1;
	}
	return 0;
}

static int has_instance_extension(const char *name)
{
	VkExtensionProperties extensions[256];
	uint32_t count = 256, index;

	if (vkEnumerateInstanceExtensionProperties(NULL, &count, extensions) < 0)
		return 0;
	for (index = 0; index < count; index++)
	{
		if (!strcmp(extensions[index].extensionName, name))
			return 1;
	}
	return 0;
}

static int instance_make(const struct xgpu_vulkan_options *options)
{
	VkApplicationInfo application;
	VkInstanceCreateInfo info;
	const char *extensions[32];
	const char *layers[1];
	uint32_t extension_count = 0, index, version = VK_API_VERSION_1_0;
	int debug = 0;

	if (vkEnumerateInstanceVersion)
		vkEnumerateInstanceVersion(&version);
	if (version < VK_API_VERSION_1_3)
	{
		host_logf(HOST_LOG_ERROR, "vulkan: the loader has Vulkan %u.%u; the backend needs 1.3", VK_API_VERSION_MAJOR(version),
			VK_API_VERSION_MINOR(version));
		return 0;
	}
	for (index = 0; index < options->instance_extension_count && extension_count < 30; index++)
		extensions[extension_count++] = options->instance_extensions[index];
	vk.validation = options->validation && has_layer("VK_LAYER_KHRONOS_validation");
	if (options->validation && !vk.validation)
		host_logf(HOST_LOG_WARN, "vulkan: no validation layers (VK_LAYER_KHRONOS_validation)");
	if (vk.validation && has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
	{
		extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
		debug = 1;
	}
	/* (display.hdr: a window's swapchain can be HDR) */
	if (HDR_POSSIBLE && options->surface_create && options->encode && !options->create_instance &&
		has_instance_extension(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME))
	{
		extensions[extension_count++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
		vk.colorspaces = 1;
	}
	memset(&application, 0, sizeof(application));
	application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application.pApplicationName = "OpenH1";
	application.pEngineName = "OpenH1";
	application.apiVersion = VK_API_VERSION_1_3;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo = &application;
	info.enabledExtensionCount = extension_count;
	info.ppEnabledExtensionNames = extensions;
	layers[0] = "VK_LAYER_KHRONOS_validation";
	info.enabledLayerCount = vk.validation ? 1 : 0;
	info.ppEnabledLayerNames = layers;
	if (!check(options->create_instance ?
		options->create_instance(vkGetInstanceProcAddr, &info, NULL, &vk.instance) :
		vkCreateInstance(&info, NULL, &vk.instance), "vkCreateInstance"))
		return 0;
#define LOAD(name) name = (PFN_##name)vkGetInstanceProcAddr(vk.instance, #name);
	INSTANCE_FUNCTIONS(LOAD)
	SURFACE_FUNCTIONS(LOAD)
	DEBUG_FUNCTIONS(LOAD)
#undef LOAD
	if (debug && vkCreateDebugUtilsMessengerEXT)
	{
		VkDebugUtilsMessengerCreateInfoEXT messenger;

		memset(&messenger, 0, sizeof(messenger));
		messenger.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		messenger.pfnUserCallback = validation_message;
		vkCreateDebugUtilsMessengerEXT(vk.instance, &messenger, NULL, &vk.messenger);
	}
	return 1;
}

static int device_has_extension(const char *name)
{
	VkExtensionProperties *extensions;
	uint32_t count = 0, index;
	int found = 0;

	vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &count, NULL);
	extensions = malloc(count * sizeof(*extensions));
	if (!extensions)
		return 0;
	vkEnumerateDeviceExtensionProperties(vk.physical, NULL, &count, extensions);
	for (index = 0; index < count && !found; index++)
		found = !strcmp(extensions[index].extensionName, name);
	free(extensions);
	return found;
}

/* the driver's (VK_DRIVER_ID_*) */
static VkDriverId driver_id(void)
{
	VkPhysicalDeviceVulkan12Properties properties12;
	VkPhysicalDeviceProperties2 properties;

	memset(&properties12, 0, sizeof(properties12));
	memset(&properties, 0, sizeof(properties));
	properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &properties12;
	vkGetPhysicalDeviceProperties2(vk.physical, &properties);
	return properties12.driverID;
}

static int format_has(VkFormat format, int buffer, VkFormatFeatureFlags features)
{
	VkFormatProperties properties;

	vkGetPhysicalDeviceFormatProperties(vk.physical, format, &properties);
	return ((buffer ? properties.bufferFeatures : properties.optimalTilingFeatures) & features) == features;
}

/* Depth is also read by the scope, cockpit and feedback paths. Attachment
support alone does not guarantee sampling of a combined depth/stencil format.
Reversed depth is kept in a float where the device has one: an integer
depth steps as finely near 0 as near 1, which reversed depth does not gain
from (xgpu_vulkan.h). */
static int depth_format_choose(void)
{
	VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	if (XGPU_VULKAN_REVERSED_DEPTH && format_has(VK_FORMAT_D32_SFLOAT_S8_UINT, 0, features))
		vk.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	else if (format_has(VK_FORMAT_D24_UNORM_S8_UINT, 0, features))
		vk.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	else if (format_has(VK_FORMAT_D32_SFLOAT_S8_UINT, 0, features))
		vk.depth_format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	else
	{
		vk.depth_format = VK_FORMAT_UNDEFINED;
		host_logf(HOST_LOG_ERROR, "vulkan: %s has no combined depth/stencil format supporting attachments and sampling",
			vk.properties.deviceName);
		return 0;
	}
	return 1;
}

static int required_features(const VkPhysicalDeviceFeatures2 *features,
	const VkPhysicalDeviceVulkan12Features *features12, const VkPhysicalDeviceVulkan13Features *features13)
{
	if (!features13->dynamicRendering || !features12->timelineSemaphore || !features12->hostQueryReset)
	{
		host_logf(HOST_LOG_ERROR, "vulkan: %s has no dynamic rendering, timeline semaphores or host query reset",
			vk.properties.deviceName);
		return 0;
	}
	/* Flat rendering and VR's fallback use the actual number of visible
	samples. Boolean occlusion queries cannot provide those counts. */
	if (!features->features.occlusionQueryPrecise)
	{
		host_logf(HOST_LOG_ERROR, "vulkan: %s has no precise occlusion queries", vk.properties.deviceName);
		return 0;
	}
	return 1;
}

/* a physical device of Vulkan 1.3 with a graphics queue (that can present to
the surface, with one; VR's runtime's device may have none that can, and
then draws only the headset's frames) */
static int physical_choose(const struct xgpu_vulkan_options *options)
{
	VkPhysicalDevice devices[16];
	uint32_t count = 0, index, best_score = 0;
	int chosen = -1, graphics;

	if (options->choose_physical)
	{
		devices[0] = options->choose_physical(vk.instance);
		if (!devices[0])
			return 0;
		count = 1;
	}
	else
	{
		/* Query the count before fetching handles. Some Android drivers
		leave the supplied count unchanged when the array is larger than
		the device list, so the loader then dereferences unwritten handles. */
		if (!check(vkEnumeratePhysicalDevices(vk.instance, &count, NULL), "vkEnumeratePhysicalDevices"))
			return 0;
		if (count > sizeof(devices) / sizeof(devices[0]))
			count = sizeof(devices) / sizeof(devices[0]);
		if (count && !check(vkEnumeratePhysicalDevices(vk.instance, &count, devices), "vkEnumeratePhysicalDevices"))
			return 0;
	}
	for (index = 0; index < count; index++)
	{
		VkPhysicalDeviceProperties properties;
		VkQueueFamilyProperties families[16];
		uint32_t family_count = 16, family, score;

		vkGetPhysicalDeviceProperties(devices[index], &properties);
		if (properties.apiVersion < VK_API_VERSION_1_3)
			continue;
		vkGetPhysicalDeviceQueueFamilyProperties(devices[index], &family_count, families);
		graphics = -1;
		for (family = 0; family < family_count; family++)
		{
			VkBool32 present = VK_TRUE;

			if (!(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
				continue;
			if (vk.surface)
				vkGetPhysicalDeviceSurfaceSupportKHR(devices[index], family, vk.surface, &present);
			if (graphics < 0 && options->choose_physical)
				graphics = (int)family;
			if (!present)
				continue;
			graphics = (int)family;
			break;
		}
		if (graphics < 0)
			continue;
		family = (uint32_t)graphics;
		score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
			properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : 1;
		if (score > best_score)
		{
			best_score = score;
			chosen = (int)index;
			vk.queue_family = family;
			vk.timestamp_bits = families[family].timestampValidBits;
		}
	}
	if (chosen < 0)
	{
		host_logf(HOST_LOG_ERROR, "vulkan: no device of Vulkan 1.3 with a graphics queue%s", vk.surface ?
			" that presents to the window" : "");
		return 0;
	}
	vk.physical = devices[chosen];
	vkGetPhysicalDeviceProperties(vk.physical, &vk.properties);
	vkGetPhysicalDeviceMemoryProperties(vk.physical, &vk.memory_properties);
	vk.capabilities.gpu_timestamps = vk.timestamp_bits > 0 && vk.properties.limits.timestampPeriod > 0.0f;
	return 1;
}

static int device_make(const struct xgpu_vulkan_options *options)
{
	VkPhysicalDeviceFeatures2 available;
	VkPhysicalDeviceVulkan11Features available11;
	VkPhysicalDeviceVulkan12Features available12;
	VkPhysicalDeviceVulkan13Features available13;
	VkPhysicalDeviceCustomBorderColorFeaturesEXT available_border;
	VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT available_cube, cube;
	VkPhysicalDeviceFeatures2 features;
	VkPhysicalDeviceMultiviewFeatures multiview;
	VkPhysicalDeviceTimelineSemaphoreFeatures timeline;
	VkPhysicalDeviceHostQueryResetFeatures query_reset;
	VkPhysicalDeviceDynamicRenderingFeatures rendering;
	VkPhysicalDeviceCustomBorderColorFeaturesEXT border;
	VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR available_local_read, local_read;
	VkPhysicalDeviceRasterizationOrderAttachmentAccessFeaturesEXT available_order, order;
	VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT available_interlock, interlock;
	VkPhysicalDeviceImageCompressionControlFeaturesEXT available_compression, compression;
	VkDriverId driver = driver_id();
	VkDeviceQueueCreateInfo queue;
	VkDeviceCreateInfo info;
	const char *extensions[20];
	uint32_t extension_count = 0;
	float priority = 1.0f;
	/* (HALO_VULKAN_NO_CUSTOM_BORDER_COLORS: as a device without them, whose
	shaders draw the border colours, Metal's way: to test that path) */
	int custom_border = device_has_extension(VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME) &&
		!getenv("HALO_VULKAN_NO_CUSTOM_BORDER_COLORS");
	int non_seamless = device_has_extension(VK_EXT_NON_SEAMLESS_CUBE_MAP_EXTENSION_NAME);
	/* (display.hdr's: the colour attachment read where it is drawn, in
	rasterization order where the device can; the swapchain's metadata) */
	int local_reads = HDR_POSSIBLE && device_has_extension(VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME);
	int ordered = local_reads && device_has_extension(VK_EXT_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME);
	/* (else the fragment shader interlock, where the targets the interlocked
	draws write as storage images can be made uncompressed, or the driver is
	NVIDIA's, whose compression they were right with (an RTX 4090, 615.78);
	HALO_VULKAN_NO_INTERLOCK: as a device without it, whose draws' own
	triangles are unordered) */
	int interlocks = local_reads && device_has_extension(VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME) &&
		!getenv("HALO_VULKAN_NO_INTERLOCK");
	int compression_control = interlocks && device_has_extension(VK_EXT_IMAGE_COMPRESSION_CONTROL_EXTENSION_NAME);
	int metadata = HDR_POSSIBLE && vk.colorspaces && device_has_extension(VK_EXT_HDR_METADATA_EXTENSION_NAME);
	/* (display.low_latency: when each present reaches the screen) */
	int present_ids = device_has_extension(VK_KHR_PRESENT_ID_EXTENSION_NAME) &&
		device_has_extension(VK_KHR_PRESENT_WAIT_EXTENSION_NAME);
	int display_timing = device_has_extension(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
	VkPhysicalDevicePresentIdFeaturesKHR available_id, present_id;
	VkPhysicalDevicePresentWaitFeaturesKHR available_wait, present_wait;
	/* (display.ambient_occlusion "ray_traced": ray queries through
	acceleration structures, where the device has them, xgpu_vulkan_rt.c;
	HALO_VULKAN_NO_RAY_QUERIES as a device without them. Not a processor's
	(llvmpipe's): its ray tracing is no faster than the distance field's
	rays, and Mesa 25's crashed in its own code as the scene was built
	again each frame, on the Spark) */
	int ray_queries = device_has_extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
		device_has_extension(VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
		device_has_extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
		vk.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU && !getenv("HALO_VULKAN_NO_RAY_QUERIES");
	VkPhysicalDeviceAccelerationStructureFeaturesKHR available_structures, structures;
	VkPhysicalDeviceRayQueryFeaturesKHR available_queries, queries;
	VkPhysicalDeviceBufferDeviceAddressFeatures addresses;
	void **chain;

	memset(&available, 0, sizeof(available));
	memset(&available11, 0, sizeof(available11));
	memset(&available12, 0, sizeof(available12));
	memset(&available13, 0, sizeof(available13));
	memset(&available_border, 0, sizeof(available_border));
	available.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	available11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	available12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	available13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	available_border.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
	memset(&available_cube, 0, sizeof(available_cube));
	available_cube.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT;
	available.pNext = &available11;
	available11.pNext = &available12;
	available12.pNext = &available13;
	chain = &available13.pNext;
	if (custom_border)
	{
		*chain = &available_border;
		chain = &available_border.pNext;
	}
	if (non_seamless)
	{
		*chain = &available_cube;
		chain = &available_cube.pNext;
	}
	memset(&available_local_read, 0, sizeof(available_local_read));
	available_local_read.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;
	memset(&available_order, 0, sizeof(available_order));
	available_order.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT;
	if (local_reads)
	{
		*chain = &available_local_read;
		chain = &available_local_read.pNext;
	}
	if (ordered)
	{
		*chain = &available_order;
		chain = &available_order.pNext;
	}
	memset(&available_interlock, 0, sizeof(available_interlock));
	available_interlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
	if (interlocks)
	{
		*chain = &available_interlock;
		chain = &available_interlock.pNext;
	}
	memset(&available_compression, 0, sizeof(available_compression));
	available_compression.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT;
	if (compression_control)
	{
		*chain = &available_compression;
		chain = &available_compression.pNext;
	}
	memset(&available_structures, 0, sizeof(available_structures));
	available_structures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	memset(&available_queries, 0, sizeof(available_queries));
	available_queries.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	if (ray_queries)
	{
		*chain = &available_structures;
		available_structures.pNext = &available_queries;
		chain = &available_queries.pNext;
	}
	memset(&available_id, 0, sizeof(available_id));
	available_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
	memset(&available_wait, 0, sizeof(available_wait));
	available_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
	if (present_ids)
	{
		*chain = &available_id;
		available_id.pNext = &available_wait;
	}
	vkGetPhysicalDeviceFeatures2(vk.physical, &available);
	if (!required_features(&available, &available12, &available13) || !depth_format_choose())
		return 0;

	memset(&features, 0, sizeof(features));
	memset(&multiview, 0, sizeof(multiview));
	memset(&timeline, 0, sizeof(timeline));
	memset(&query_reset, 0, sizeof(query_reset));
	memset(&rendering, 0, sizeof(rendering));
	memset(&border, 0, sizeof(border));
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	multiview.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES;
	timeline.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
	query_reset.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
	rendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
	border.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CUSTOM_BORDER_COLOR_FEATURES_EXT;
	/* OpenXR runtimes may add their own required features. The individual
	structures let them find ours without mixing a promoted feature with
	its aggregate Vulkan 1.2 structure (as Monado does for timelines). */
	features.pNext = &multiview;
	multiview.pNext = &timeline;
	timeline.pNext = &query_reset;
	query_reset.pNext = &rendering;
	multiview.multiview = available11.multiview;
	features.features.depthClamp = available.features.depthClamp;
	vk.capabilities.depth_clamp = available.features.depthClamp;
	features.features.fillModeNonSolid = available.features.fillModeNonSolid;
	features.features.samplerAnisotropy = available.features.samplerAnisotropy;
	features.features.largePoints = available.features.largePoints;
	features.features.occlusionQueryPrecise = available.features.occlusionQueryPrecise;
	features.features.textureCompressionBC = available.features.textureCompressionBC;
	features.features.textureCompressionASTC_LDR = available.features.textureCompressionASTC_LDR;
	features.features.fragmentStoresAndAtomics = available.features.fragmentStoresAndAtomics;
	features.features.shaderImageGatherExtended = VK_FALSE;
	timeline.timelineSemaphore = VK_TRUE;
	query_reset.hostQueryReset = VK_TRUE;
	rendering.dynamicRendering = VK_TRUE;
	/* (a window that has no surface now may have one later: the swapchain
	where the device has it) */
	if (vk.surface || (vk.surface_create && device_has_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME)))
		extensions[extension_count++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
	chain = &rendering.pNext;
	if (custom_border && available_border.customBorderColors && available_border.customBorderColorWithoutFormat)
	{
		extensions[extension_count++] = VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME;
		border.customBorderColors = VK_TRUE;
		border.customBorderColorWithoutFormat = VK_TRUE;
		*chain = &border;
		chain = &border.pNext;
		vk.capabilities.custom_border_colors = 1;
	}
	if (non_seamless && available_cube.nonSeamlessCubeMap)
	{
		memset(&cube, 0, sizeof(cube));
		cube.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_NON_SEAMLESS_CUBE_MAP_FEATURES_EXT;
		cube.nonSeamlessCubeMap = VK_TRUE;
		extensions[extension_count++] = VK_EXT_NON_SEAMLESS_CUBE_MAP_EXTENSION_NAME;
		*chain = &cube;
		chain = &cube.pNext;
		vk.non_seamless = 1;
	}
	/* display.hdr: draws that read the pixel they draw over (and each
	sample's own, of a multisampled target: sample shading), into half
	floats they blend */
	if (local_reads && available_local_read.dynamicRenderingLocalRead && available.features.sampleRateShading &&
		format_has(VK_FORMAT_R16G16B16A16_SFLOAT, 0, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
	{
		memset(&local_read, 0, sizeof(local_read));
		local_read.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR;
		local_read.dynamicRenderingLocalRead = VK_TRUE;
		extensions[extension_count++] = VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME;
		*chain = &local_read;
		chain = &local_read.pNext;
		features.features.sampleRateShading = VK_TRUE;
		vk.capabilities.color_read = 1;
		if (ordered && available_order.rasterizationOrderColorAttachmentAccess)
		{
			memset(&order, 0, sizeof(order));
			order.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_FEATURES_EXT;
			order.rasterizationOrderColorAttachmentAccess = VK_TRUE;
			extensions[extension_count++] = VK_EXT_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_EXTENSION_NAME;
			*chain = &order;
			chain = &order.pNext;
			vk.capabilities.raster_order = 1;
		}
		else if (interlocks && available_interlock.fragmentShaderPixelInterlock &&
			available.features.shaderStorageImageMultisample &&
			format_has(VK_FORMAT_R16G16B16A16_SFLOAT, 0, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) &&
			(available_compression.imageCompressionControl || driver == VK_DRIVER_ID_NVIDIA_PROPRIETARY))
		{
			memset(&interlock, 0, sizeof(interlock));
			interlock.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT;
			interlock.fragmentShaderPixelInterlock = VK_TRUE;
			interlock.fragmentShaderSampleInterlock = available_interlock.fragmentShaderSampleInterlock;
			extensions[extension_count++] = VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME;
			*chain = &interlock;
			chain = &interlock.pNext;
			features.features.shaderStorageImageMultisample = VK_TRUE;
			vk.capabilities.pixel_interlock = 1;
			if (available_compression.imageCompressionControl)
			{
				memset(&compression, 0, sizeof(compression));
				compression.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_COMPRESSION_CONTROL_FEATURES_EXT;
				compression.imageCompressionControl = VK_TRUE;
				extensions[extension_count++] = VK_EXT_IMAGE_COMPRESSION_CONTROL_EXTENSION_NAME;
				*chain = &compression;
				chain = &compression.pNext;
				vk.compression_control = 1;
			}
			vk.capabilities.sample_interlock = available_interlock.fragmentShaderSampleInterlock != 0;
		}
	}
	if (metadata && (vk.surface || vk.surface_create))
	{
		extensions[extension_count++] = VK_EXT_HDR_METADATA_EXTENSION_NAME;
		vk.hdr_metadata = 1;
	}
	vk.present_wait = vk.display_timing = 0;
	if ((vk.surface || vk.surface_create) && present_ids && available_id.presentId && available_wait.presentWait)
	{
		memset(&present_id, 0, sizeof(present_id));
		present_id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR;
		present_id.presentId = VK_TRUE;
		memset(&present_wait, 0, sizeof(present_wait));
		present_wait.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR;
		present_wait.presentWait = VK_TRUE;
		present_id.pNext = &present_wait;
		*chain = &present_id;
		chain = &present_wait.pNext;
		extensions[extension_count++] = VK_KHR_PRESENT_ID_EXTENSION_NAME;
		extensions[extension_count++] = VK_KHR_PRESENT_WAIT_EXTENSION_NAME;
		vk.present_wait = 1;
	}
	/* (display timing only where present wait is not, Android's, unless
	asked for: gamescope's layer on the Steam Deck answered its past times
	VK_INCOMPLETE without end, and crashed as a swapchain was made from one
	whose times had been asked for; present wait alone paces the frames
	there, its return gamescope's latch, HALO_VULKAN_DISPLAY_TIMING=1 asks
	for both) */
	if ((vk.surface || vk.surface_create) && display_timing && !getenv("HALO_VULKAN_NO_DISPLAY_TIMING") &&
		(!vk.present_wait || getenv("HALO_VULKAN_NO_PRESENT_WAIT") || getenv("HALO_VULKAN_DISPLAY_TIMING")))
	{
		extensions[extension_count++] = VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME;
		vk.display_timing = 1;
	}
	vk.capabilities.ray_queries = 0;
	if (ray_queries && available_structures.accelerationStructure && available_queries.rayQuery &&
		available12.bufferDeviceAddress)
	{
		memset(&structures, 0, sizeof(structures));
		structures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		structures.accelerationStructure = VK_TRUE;
		memset(&queries, 0, sizeof(queries));
		queries.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
		queries.rayQuery = VK_TRUE;
		memset(&addresses, 0, sizeof(addresses));
		addresses.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		addresses.bufferDeviceAddress = VK_TRUE;
		structures.pNext = &queries;
		queries.pNext = &addresses;
		*chain = &structures;
		chain = &addresses.pNext;
		extensions[extension_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
		extensions[extension_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
		extensions[extension_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
		vk.capabilities.ray_queries = 1;
	}

	memset(&queue, 0, sizeof(queue));
	queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue.queueFamilyIndex = vk.queue_family;
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	info.pNext = &features;
	info.queueCreateInfoCount = 1;
	info.pQueueCreateInfos = &queue;
	info.enabledExtensionCount = extension_count;
	info.ppEnabledExtensionNames = extensions;
	if (!check(options->create_device ?
		options->create_device(vkGetInstanceProcAddr, vk.physical, &info, NULL, &vk.device) :
		vkCreateDevice(vk.physical, &info, NULL, &vk.device), "vkCreateDevice"))
		return 0;
#define LOAD(name) name = (PFN_##name)vkGetDeviceProcAddr(vk.device, #name);
	DEVICE_FUNCTIONS(LOAD)
	if (vk.surface || vk.surface_create)
	{
		SWAPCHAIN_FUNCTIONS(LOAD)
	}
	if (vk.hdr_metadata)
		LOAD(vkSetHdrMetadataEXT)
#undef LOAD
	vkGetDeviceQueue(vk.device, vk.queue_family, 0, &vk.queue);
	if (vk.capabilities.ray_queries)
	{
		VkPhysicalDeviceAccelerationStructurePropertiesKHR structure_properties;
		VkPhysicalDeviceProperties2 properties;
		struct xgpu_vulkan_rt_device device;

		memset(&structure_properties, 0, sizeof(structure_properties));
		structure_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
		memset(&properties, 0, sizeof(properties));
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &structure_properties;
		vkGetPhysicalDeviceProperties2(vk.physical, &properties);
		memset(&device, 0, sizeof(device));
		device.device = vk.device;
		device.get_device_proc_addr = vkGetDeviceProcAddr;
		device.memory = vk.memory_properties;
		device.scratch_alignment = structure_properties.minAccelerationStructureScratchOffsetAlignment;
		vk.capabilities.ray_queries = xgpu_vulkan_rt_create(&device);
	}

	vk.capabilities.largest_target = vk.properties.limits.maxImageDimension2D;
	{
		VkSampleCountFlags counts = vk.properties.limits.framebufferColorSampleCounts &
			vk.properties.limits.framebufferDepthSampleCounts;

		vk.capabilities.samples = 1;
		while (vk.capabilities.samples < 16 && (counts & (vk.capabilities.samples * 2)))
			vk.capabilities.samples *= 2;
	}
	vk.capabilities.anisotropy = available.features.samplerAnisotropy ?
		(uint32_t)vk.properties.limits.maxSamplerAnisotropy : 1;
	vk.capabilities.block_compression = available.features.textureCompressionBC;
	vk.capabilities.astc = available.features.textureCompressionASTC_LDR && format_has(VK_FORMAT_ASTC_4x4_UNORM_BLOCK, 0,
		VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
		VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
	vk.capabilities.fragment_stores = available.features.fragmentStoresAndAtomics;
	vk.capabilities.scaled_shorts = format_has(VK_FORMAT_R16G16_SSCALED, 1, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) &&
		format_has(VK_FORMAT_R16G16B16_SSCALED, 1, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) &&
		format_has(VK_FORMAT_R16G16B16A16_SSCALED, 1, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) &&
		format_has(VK_FORMAT_R16_SSCALED, 1, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
	{
		VkPhysicalDeviceVulkan11Properties properties11;
		VkPhysicalDeviceVulkan12Properties properties12;
		VkPhysicalDeviceProperties2 properties;

		memset(&properties11, 0, sizeof(properties11));
		memset(&properties12, 0, sizeof(properties12));
		memset(&properties, 0, sizeof(properties));
		properties11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES;
		properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
		properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
		properties.pNext = &properties11;
		properties11.pNext = &properties12;
		vkGetPhysicalDeviceProperties2(vk.physical, &properties);
		vk.capabilities.multiview_views = available11.multiview ? properties11.maxMultiviewViewCount : 0;
		vk.capabilities.subgroups = (properties11.subgroupSupportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
			(properties11.subgroupSupportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) &&
			!getenv("HALO_VULKAN_NO_SUBGROUPS");
		vk.nvidia = properties12.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY;
		snprintf(vk.capabilities.device, sizeof(vk.capabilities.device), "%s (Vulkan %u.%u.%u, %s%s%s)",
			vk.properties.deviceName, VK_API_VERSION_MAJOR(vk.properties.apiVersion),
			VK_API_VERSION_MINOR(vk.properties.apiVersion), VK_API_VERSION_PATCH(vk.properties.apiVersion),
			properties12.driverName[0] ? properties12.driverName : "driver", properties12.driverInfo[0] ? " " : "",
			properties12.driverInfo);
	}
	host_logf(HOST_LOG_INFO, "vulkan: %s (Vulkan %u.%u.%u, driver %08x)%s%s%s%s%s%s%s", vk.properties.deviceName,
		VK_API_VERSION_MAJOR(vk.properties.apiVersion), VK_API_VERSION_MINOR(vk.properties.apiVersion),
		VK_API_VERSION_PATCH(vk.properties.apiVersion), vk.properties.driverVersion,
		vk.capabilities.custom_border_colors ? ", custom border colours" : "",
		vk.capabilities.block_compression ? ", BC" : "", vk.capabilities.astc ? ", ASTC" : "",
		vk.capabilities.color_read ? (vk.capabilities.raster_order ? ", local reads (ordered)" :
		vk.capabilities.pixel_interlock ? ", local reads (interlocked)" : ", local reads") : "",
		vk.colorspaces ? ", swapchain colour spaces" : "", vk.capabilities.ray_queries ? ", ray queries" : "",
		vk.validation ? ", validated" : "");
	return 1;
}

static int layouts_make(void)
{
	VkDescriptorSetLayoutBinding draw[5], textures[7];
	VkDescriptorSetLayoutCreateInfo info;
	VkPipelineLayoutCreateInfo layout;
	VkDescriptorSetLayout sets[4];
	int index;

	memset(draw, 0, sizeof(draw));
	draw[0].binding = 0;
	draw[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	draw[0].descriptorCount = 1;
	draw[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	draw[1].binding = 1;
	draw[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	draw[1].descriptorCount = 1;
	draw[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	draw[2].binding = 2;
	draw[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	draw[2].descriptorCount = 1;
	draw[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	draw[3].binding = 3;
	draw[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	draw[3].descriptorCount = 1;
	draw[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	draw[4].binding = 4;
	draw[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	draw[4].descriptorCount = 1;
	draw[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.bindingCount = 5;
	info.pBindings = draw;
	if (!check(vkCreateDescriptorSetLayout(vk.device, &info, NULL, &vk.draw_layout), "vkCreateDescriptorSetLayout"))
		return 0;
	memset(textures, 0, sizeof(textures));
	for (index = 0; index < 4; index++)
	{
		textures[index].binding = (uint32_t)index;
		textures[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		textures[index].descriptorCount = 1;
		textures[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	info.bindingCount = 4;
	info.pBindings = textures;
	if (!check(vkCreateDescriptorSetLayout(vk.device, &info, NULL, &vk.texture_layout), "vkCreateDescriptorSetLayout"))
		return 0;
	sets[0] = vk.draw_layout;
	sets[1] = vk.texture_layout;
	memset(&layout, 0, sizeof(layout));
	layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout.setLayoutCount = 2;
	layout.pSetLayouts = sets;
	/* (display.hdr: the colour attachment read where it is drawn, where
	the device can: XGPU_SPIRV_SET_INPUT) */
	if (vk.capabilities.color_read)
	{
		textures[0].binding = 0;
		textures[0].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		textures[0].descriptorCount = 1;
		textures[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textures[1] = textures[0];
		textures[1].binding = XGPU_VULKAN_BINDING_STORE;
		textures[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		info.bindingCount = vk.capabilities.pixel_interlock ? 2 : 1;
		info.pBindings = textures;
		if (!check(vkCreateDescriptorSetLayout(vk.device, &info, NULL, &vk.input_layout), "vkCreateDescriptorSetLayout"))
			return 0;
		sets[2] = vk.input_layout;
	}
	else
	{
		info.bindingCount = 0;
		info.pBindings = NULL;
		if (!check(vkCreateDescriptorSetLayout(vk.device, &info, NULL, &vk.empty_layout), "vkCreateDescriptorSetLayout"))
			return 0;
		sets[2] = vk.empty_layout;
	}
	/* (display.ambient_occlusion "world"'s: XGPU_VULKAN_SET_OCCLUSION, the
	field and block, xgpu_vulkan_world_ao.c) */
	textures[0].binding = XGPU_VULKAN_BINDING_WORLD_SDF;
	textures[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textures[0].descriptorCount = 1;
	textures[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	textures[1].binding = XGPU_VULKAN_BINDING_WORLD_AO;
	textures[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	textures[1].descriptorCount = 1;
	textures[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	textures[2].binding = XGPU_VULKAN_BINDING_WORLD_SDF_TABLE;
	textures[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textures[2].descriptorCount = 1;
	textures[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	textures[3].binding = XGPU_VULKAN_BINDING_WORLD_AO_SCENERY;
	textures[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textures[3].descriptorCount = 1;
	textures[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	textures[4].binding = XGPU_VULKAN_BINDING_WORLD_AO_OBJECTS;
	textures[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textures[4].descriptorCount = 1;
	textures[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	textures[5].binding = XGPU_VULKAN_BINDING_WORLD_AO_PIXELS;
	textures[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textures[5].descriptorCount = 1;
	textures[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	info.bindingCount = 6;
	/* (and display.ambient_occlusion "ray_traced"'s scene, where the device
	has ray queries) */
	if (vk.capabilities.ray_queries)
	{
		textures[6].binding = XGPU_VULKAN_BINDING_WORLD_AO_SCENE;
		textures[6].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		textures[6].descriptorCount = 1;
		textures[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		textures[6].pImmutableSamplers = NULL;
		info.bindingCount = 7;
	}
	info.pBindings = textures;
	if (!check(vkCreateDescriptorSetLayout(vk.device, &info, NULL, &vk.occlusion_layout), "vkCreateDescriptorSetLayout"))
		return 0;
	sets[XGPU_VULKAN_SET_OCCLUSION] = vk.occlusion_layout;
	layout.setLayoutCount = 4;
	return check(vkCreatePipelineLayout(vk.device, &layout, NULL, &vk.pipeline_layout), "vkCreatePipelineLayout");
}

/* the draw set: the ring's ranges (dynamic offsets) of the constants and
uniforms, the lights' block and cells, the visibility counters */
static int draw_set_make(void)
{
	VkDescriptorPoolSize sizes[4];
	VkDescriptorPoolCreateInfo pool;
	VkDescriptorSetAllocateInfo allocate;
	VkDescriptorBufferInfo buffers[4];
	VkDescriptorImageInfo cells;
	VkWriteDescriptorSet writes[5];
	struct xgpu_vulkan_image_description description;
	int index;

	sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	sizes[0].descriptorCount = 2;
	sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	sizes[1].descriptorCount = 1;
	sizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[2].descriptorCount = 1;
	sizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	sizes[3].descriptorCount = 1;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool.maxSets = 1;
	pool.poolSizeCount = 4;
	pool.pPoolSizes = sizes;
	if (!check(vkCreateDescriptorPool(vk.device, &pool, NULL, &vk.draw_pool), "vkCreateDescriptorPool"))
		return 0;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate.descriptorPool = vk.draw_pool;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &vk.draw_layout;
	if (!check(vkAllocateDescriptorSets(vk.device, &allocate, &vk.draw_set), "vkAllocateDescriptorSets"))
		return 0;
	vk.lights_block = xgpu_vulkan_buffer_create(6192);
	vk.samples = xgpu_vulkan_buffer_create(4 * (XGPU_VULKAN_VISIBILITY_SLOTS + 1) * QUERY_FRAMES);
	memset(&description, 0, sizeof(description));
	description.type = XGPU_VULKAN_IMAGE_3D;
	description.format = XGPU_VULKAN_FORMAT_RGBA32_UINT;
	description.usage = XGPU_VULKAN_USAGE_SAMPLED;
	description.width = 32;
	description.height = 32;
	/* (the cells, the clusters' masks and the world: xgpu.h) */
	description.depth_or_layers = XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES + XGPU_STREAM_DYNAMIC_LIGHTS_WORLD_SLICES;
	description.levels = 1;
	vk.lights_cells = xgpu_vulkan_image_create(&description);
	vk.lights_cells_uploaded = 0;
	if (!vk.lights_block || !vk.samples || !vk.lights_cells)
		return 0;
	/* (no world yet: its header's node count 0) */
	{
		static const uint32_t none[4];

		xgpu_vulkan_image_upload(vk.lights_cells, 0, 0, 0, 0, XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES, 1, 1, 1, none, 16,
			16);
	}
	/* (named by the set below, made once: xgpu_vulkan_buffer_write) */
	vk.lights_block->named_by_set = vk.samples->named_by_set = 1;
	memset(buffers, 0, sizeof(buffers));
	buffers[0].buffer = vk.ring;
	buffers[0].range = 192 * 16;
	buffers[1].buffer = vk.ring;
	/* (the uniforms' size: xgpu_spirv.h's struct xgpu_spirv_uniforms, at
	most) */
	buffers[1].range = 2048;
	buffers[2].buffer = vk.lights_block->buffer;
	buffers[2].range = 6192;
	buffers[3].buffer = vk.samples->buffer;
	buffers[3].range = 4 * (XGPU_VULKAN_VISIBILITY_SLOTS + 1) * QUERY_FRAMES;
	cells.sampler = vk.stand_in_sampler;
	cells.imageView = vk.lights_cells->sampled;
	cells.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	memset(writes, 0, sizeof(writes));
	for (index = 0; index < 5; index++)
	{
		writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[index].dstSet = vk.draw_set;
		writes[index].dstBinding = (uint32_t)index;
		writes[index].descriptorCount = 1;
	}
	writes[0].descriptorType = writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writes[0].pBufferInfo = &buffers[0];
	writes[1].pBufferInfo = &buffers[1];
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[2].pBufferInfo = &buffers[2];
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[3].pImageInfo = &cells;
	writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[4].pBufferInfo = &buffers[3];
	vkUpdateDescriptorSets(vk.device, 5, writes, 0, NULL);
	return 1;
}

void xgpu_vulkan_dynamic_lights_upload(const void *block, uint32_t size, const void *cells, uint32_t first,
	uint32_t end)
{
	const uint32_t row = 32 * 16, slice = 32 * row;
	uint32_t z, z_end, y, y_end;

	if (!vk.lights_block || !vk.lights_cells)
		return;
	/* (early, before the submission's draws, unless a draw recorded in it
	read the lights: xgpu_vulkan_draw) */
	xgpu_vulkan_buffer_write(vk.lights_block, 0, block, size < vk.lights_block->size ? size : vk.lights_block->size, 0);
	if (!vk.lights_cells_uploaded)
	{
		first = 0;
		end = XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES * slice;
		vk.lights_cells_uploaded = 1;
	}
	if (first >= end || end > XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES * slice)
		return;
	/* (the rows changed, in a slice; the slices, in more than one) */
	z = first / slice;
	z_end = (end - 1) / slice + 1;
	y = z_end - z == 1 ? first % slice / row : 0;
	y_end = z_end - z == 1 ? ((end - 1) % slice) / row + 1 : 32;
	xgpu_vulkan_image_upload(vk.lights_cells, 0, 0, 0, y, z, 32, y_end - y, z_end - z,
		(const unsigned char *)cells + z * slice + y * row, row, slice);
}

void xgpu_vulkan_dynamic_lights_world(const void *texels, uint32_t count)
{
	uint32_t slices = (count + 1023) / 1024;
	unsigned char *whole;

	if (!vk.lights_cells || !count || count > XGPU_STREAM_DYNAMIC_LIGHTS_WORLD_TEXELS)
		return;
	/* (whole slices: the last's rest zeros) */
	whole = calloc(slices, 1024 * 16);
	if (!whole)
		return;
	memcpy(whole, texels, (size_t)count * 16);
	xgpu_vulkan_image_upload(vk.lights_cells, 0, 0, 0, 0, XGPU_STREAM_DYNAMIC_LIGHTS_CELL_SLICES, 32, 32, slices, whole,
		0, 0);
	free(whole);
}

/* the stand-ins sampled by a stage with no image: 1 by 1, (0, 0, 0, 1), as
OpenGL's incomplete textures read */
static int stand_ins_make(void)
{
	static const unsigned char texel[4] = { 0, 0, 0, 255 };
	struct xgpu_vulkan_sampler_description sampler;
	int kind, layer;

	memset(&sampler, 0, sizeof(sampler));
	sampler.anisotropy = 1;
	{
		VkSamplerCreateInfo info;

		memset(&info, 0, sizeof(info));
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.magFilter = info.minFilter = VK_FILTER_NEAREST;
		info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		info.maxLod = 0.25f;
		if (!check(vkCreateSampler(vk.device, &info, NULL, &vk.stand_in_sampler), "vkCreateSampler"))
			return 0;
	}
	for (kind = 0; kind < 4; kind++)
	{
		struct xgpu_vulkan_image_description description;

		memset(&description, 0, sizeof(description));
		description.type = (uint8_t)kind;
		description.format = XGPU_VULKAN_FORMAT_BGRA8;
		description.usage = XGPU_VULKAN_USAGE_SAMPLED;
		description.width = description.height = 1;
		description.depth_or_layers = 1;
		description.levels = 1;
		vk.stand_ins[kind] = xgpu_vulkan_image_create(&description);
		if (!vk.stand_ins[kind])
			return 0;
		for (layer = 0; layer < (kind == XGPU_VULKAN_IMAGE_CUBE ? 6 : 1); layer++)
			xgpu_vulkan_image_upload(vk.stand_ins[kind], 0, (uint32_t)layer, 0, 0, 0, 1, 1, 1, texel, 4, 4);
	}
	/* (display.ambient_occlusion "world"'s far-outside field) */
	if (!xgpu_vulkan_world_ao_create())
		return 0;
	(void)sampler;
	return 1;
}

static void pipeline_cache_make(const char *path)
{
	VkPipelineCacheCreateInfo info;
	void *data = NULL;
	size_t size = 0;
	FILE *file;

	if (path && *path)
	{
		snprintf(vk.cache_path, sizeof(vk.cache_path), "%s", path);
		file = fopen(path, "rb");
		if (file)
		{
			if (!fseek(file, 0, SEEK_END))
			{
				long length = ftell(file);

				if (length > 0 && length < 256L * 1024 * 1024 && !fseek(file, 0, SEEK_SET) && (data = malloc((size_t)length)))
				{
					if (fread(data, 1, (size_t)length, file) == (size_t)length)
						size = (size_t)length;
				}
			}
			fclose(file);
		}
	}
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	info.initialDataSize = size;
	info.pInitialData = size ? data : NULL;
	/* (one the driver does not take, another device's: an empty one) */
	if (vkCreatePipelineCache(vk.device, &info, NULL, &vk.pipeline_cache) != VK_SUCCESS)
	{
		info.initialDataSize = 0;
		info.pInitialData = NULL;
		vkCreatePipelineCache(vk.device, &info, NULL, &vk.pipeline_cache);
	}
	else if (size)
	{
		host_logf(HOST_LOG_INFO, "vulkan: the pipeline cache's %lu bytes read from %s", (unsigned long)size, path);
	}
	free(data);
}

void xgpu_vulkan_pipeline_cache_save(void)
{
	size_t size = 0;
	void *data;
	char temporary[1100];
	FILE *file;
	int saved = 0;

	if (!vk.pipeline_cache || !vk.cache_path[0])
		return;
	/* (with the compilers' pipelines in it: those a draw in play asked for) */
	pipeline_wait(NULL, 0);
	if (vk.pipelines_saved == vk.pipelines_made)
		return;
	if (vkGetPipelineCacheData(vk.device, vk.pipeline_cache, &size, NULL) != VK_SUCCESS || !size)
		return;
	data = malloc(size);
	if (!data)
		return;
	if (vkGetPipelineCacheData(vk.device, vk.pipeline_cache, &size, data) == VK_SUCCESS)
	{
		/* (written whole under another name, then renamed over the old
		one: a process killed while it writes leaves the old file) */
		snprintf(temporary, sizeof(temporary), "%s.tmp", vk.cache_path);
		file = fopen(temporary, "wb");
		if (file)
		{
			int written = fwrite(data, 1, size, file) == size;

			if (!fclose(file) && written && !rename(temporary, vk.cache_path))
				saved = 1;
			else
				remove(temporary);
		}
	}
	free(data);
	if (saved)
	{
		vk.pipelines_saved = vk.pipelines_made;
		host_logf(HOST_LOG_INFO, "vulkan: the pipeline cache's %lu bytes written to %s", (unsigned long)size,
			vk.cache_path);
	}
}

/* HALO_VULKAN_DISCARDS=1: the passes in segments, their stores chosen at
the submission ("the main command buffer in segments", above), where the
device has what each part needs: dynamic rendering's secondary command
buffers (Vulkan 1.3's, which every device the backend runs on has), the
depth's and stencil's SAMPLE_ZERO resolve in a pass (Vulkan 1.2's
VK_KHR_depth_stencil_resolve: else the depth is resolved by a pass of its
own, as without discards), and lazily allocated memory for the transient
multisampled images (else they are ordinary images: a desktop's GPU has
none). HALO_VULKAN_TRANSIENT=0: no transient images; =force: transient
images in ordinary memory, to try their path on a desktop's GPU. On by
default on a tiler (measured on the Steam Frame's Adreno, #335), off on an
immediate GPU (a desktop's, a laptop's or the Steam Deck's), which has no
tile memory for them to save and on which the segments cost the render
thread a secondary command buffer begun, ended and executed each pass and
each piece of work between passes, and the state bound again in each:
there the passes are recorded into the main command buffer as they are
drawn, the state bound in it kept from pass to pass.
HALO_VULKAN_DISCARDS=1: on, =0: off, whatever the GPU. */

/* a tiler: a GPU whose passes draw in tile memory, which their loads and
stores fill and write out (ARM's Mali, Qualcomm's Adreno, Imagination's
PowerVR, Apple's, Broadcom's VideoCore), or any with lazily allocated
memory, which only a tiler's attachments can leave uncommitted */
static int device_tiles(int lazily)
{
	switch (vk.properties.vendorID)
	{
	case 0x13b5: /* ARM */
	case 0x5143: /* Qualcomm */
	case 0x1010: /* Imagination */
	case 0x106b: /* Apple */
	case 0x14e4: /* Broadcom */
		return 1;
	default:
		return lazily;
	}
}

static void discards_choose(void)
{
	const char *chosen = getenv("HALO_VULKAN_DISCARDS"), *transient = getenv("HALO_VULKAN_TRANSIENT");
	const char *aside = getenv("HALO_VULKAN_ASIDE");
	VkPhysicalDeviceDepthStencilResolveProperties resolve;
	VkPhysicalDeviceProperties2 properties;
	uint32_t index;
	int lazily = 0;

	for (index = 0; index < vk.memory_properties.memoryTypeCount; index++)
		lazily |= (vk.memory_properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0;
	vk.deferred = chosen && *chosen ? atoi(chosen) != 0 : device_tiles(lazily);
	vk.depth_resolve = vk.transient = vk.aside = 0;
	if (!vk.deferred)
	{
		host_logf(HOST_LOG_INFO, "vulkan: discards off: passes recorded into the main command buffer as drawn (%s)",
			chosen && *chosen ? "HALO_VULKAN_DISCARDS=0" : "an immediate GPU; HALO_VULKAN_DISCARDS=1: on");
		return;
	}
	vk.aside = !aside || !*aside || atoi(aside) != 0;
	memset(&resolve, 0, sizeof(resolve));
	resolve.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_STENCIL_RESOLVE_PROPERTIES;
	memset(&properties, 0, sizeof(properties));
	properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	properties.pNext = &resolve;
	vkGetPhysicalDeviceProperties2(vk.physical, &properties);
	vk.depth_resolve = (resolve.supportedDepthResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) &&
		(resolve.supportedStencilResolveModes & VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
	if (transient && !strcmp(transient, "force"))
		vk.transient = 2;
	else if (lazily && !(transient && *transient && !atoi(transient)))
		vk.transient = 1;
	host_logf(HOST_LOG_INFO, "vulkan: discards on: stores chosen at the submission; passes put aside: %s; "
		"depth resolved in passes: %s; transient multisampled targets: %s",
		vk.aside ? "yes" : "no (HALO_VULKAN_ASIDE=0)", vk.depth_resolve ? "yes" : "no (no SAMPLE_ZERO resolve)",
		vk.transient == 1 ? "lazily allocated" : vk.transient == 2 ? "forced, in ordinary memory" :
		lazily ? "no (HALO_VULKAN_TRANSIENT=0)" : "no (no lazily allocated memory)");
}

int xgpu_vulkan_initialize(const struct xgpu_vulkan_options *options)
{
	VkSemaphoreTypeCreateInfo type;
	VkSemaphoreCreateInfo semaphore;
	VkCommandPoolCreateInfo pool;
	VkCommandBufferAllocateInfo allocate;
	VkQueryPoolCreateInfo queries;
	int index;

	if (vk.device)
		return 1;
	if (!load_functions() || !instance_make(options))
		return 0;
	vk.surface_create = options->surface_create;
	vk.surface_context = options->surface_context;
	vk.display_hdr = options->display_hdr;
	vk.encode = options->encode;
	vk.gamma = options->gamma;
	vk.display_hdr_told = -1;
	vk.hdr_paper_white = 100;
	if (!vk.window_headroom)
	{
		float one = 1.0f;

		memcpy(&vk.window_sdr_white, &one, sizeof(one));
		memcpy(&vk.window_headroom, &one, sizeof(one));
	}
	vk.interval = getenv("HALO_NO_VSYNC") || options->interval < 0 ? 0 : options->interval;
	vk.present_mode_asked = options->present_mode;
	if (vk.surface_create)
	{
		vk.surface = vk.surface_create(vk.instance, vk.surface_context);
		if (!vk.surface)
		{
			host_logf(HOST_LOG_WARN, "vulkan: no surface for the window: its frames are not shown until there is one");
			vk.surface_failing = 1;
		}
	}
	if (!physical_choose(options))
		return 0;
	if (vk.surface)
	{
		VkBool32 supported = VK_FALSE;

		/* (VR's runtime's device, which may not reach the window: the
		headset's frames alone) */
		vkGetPhysicalDeviceSurfaceSupportKHR(vk.physical, vk.queue_family, vk.surface, &supported);
		if (!supported)
		{
			host_logf(HOST_LOG_WARN, "vulkan: the device's queue cannot present to the window: its frames are not shown");
			vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
			vk.surface = VK_NULL_HANDLE;
			vk.surface_create = NULL;
		}
	}
	if (!device_make(options))
		return 0;
	discards_choose();
	memset(&type, 0, sizeof(type));
	type.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	type.initialValue = 0;
	memset(&semaphore, 0, sizeof(semaphore));
	semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphore.pNext = &type;
	if (!check(vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.timeline), "vkCreateSemaphore"))
		return 0;
	semaphore.pNext = NULL;
	vk.low_latency = (uint32_t)(options->low_latency >= 0 && options->low_latency <= 2 ? options->low_latency : 0);
	if (vk.present_wait || vk.display_timing)
	{
		struct xgpu_vulkan_pacing_device pacing;

		memset(&pacing, 0, sizeof(pacing));
		pacing.device = vk.device;
		pacing.get_device_proc_addr = vkGetDeviceProcAddr;
		pacing.timeline = vk.timeline;
		pacing.present_wait = vk.present_wait && !getenv("HALO_VULKAN_NO_PRESENT_WAIT");
		pacing.display_timing = vk.display_timing;
		pacing.statistics = options->statistics;
		xgpu_vulkan_pacing_start(&pacing);
	}
#ifndef HALO_VR
	/* (always: two timestamps a frame, for a report's GPU time) */
	vk.frame_timing = 1;
	draw_times_create();
	pass_times_create();
#endif
	vk.serial = 1;
	memset(&pool, 0, sizeof(pool));
	pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	pool.queueFamilyIndex = vk.queue_family;
	for (index = 0; index < SLOTS; index++)
	{
		VkCommandBuffer buffers[2];

		if (!check(vkCreateCommandPool(vk.device, &pool, NULL, &vk.slots[index].pool), "vkCreateCommandPool"))
			return 0;
		memset(&allocate, 0, sizeof(allocate));
		allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocate.commandPool = vk.slots[index].pool;
		allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocate.commandBufferCount = 2;
		if (!check(vkAllocateCommandBuffers(vk.device, &allocate, buffers), "vkAllocateCommandBuffers"))
			return 0;
		vk.slots[index].main = buffers[0];
		vk.slots[index].early = buffers[1];
		if (vk.surface_create)
			vkCreateSemaphore(vk.device, &semaphore, NULL, &vk.acquired[index]);
	}
	if (!buffer_make(RING_SIZE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 1, &vk.ring, &vk.ring_memory,
		(void **)&vk.ring_mapped))
		return 0;
	memset(&queries, 0, sizeof(queries));
	queries.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queries.queryType = VK_QUERY_TYPE_OCCLUSION;
	queries.queryCount = QUERIES_A_FRAME;
	for (index = 0; index < QUERY_FRAMES; index++)
	{
		if (!check(vkCreateQueryPool(vk.device, &queries, NULL, &vk.query_frames[index].pool), "vkCreateQueryPool"))
			return 0;
		vkResetQueryPool(vk.device, vk.query_frames[index].pool, 0, QUERIES_A_FRAME);
	}
	if (!layouts_make() || !stand_ins_make() || !draw_set_make())
		return 0;
	pipeline_cache_make(options->cache_path);
	/* (display.hdr: the encoding's pipelines made now, as the loading
	screen's presentation may encode while a map is prepared) */
	if (vk.encode && (!vk.colorspaces || !options->encode_prepare || !options->encode_prepare()))
		vk.encode = NULL;
	/* (display.gamma's too) */
	if (vk.gamma && (!options->gamma_prepare || !options->gamma_prepare()))
		vk.gamma = NULL;
	if (vk.surface && options->rotation_prepare && options->rotate)
	{
		VkSurfaceCapabilitiesKHR capabilities;

		/* (made now, not when the phone is first turned: while a map is
		prepared, the loading screen presents, and may not ask for a
		pipeline) */
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.physical, vk.surface, &capabilities) == VK_SUCCESS &&
			(capabilities.supportedTransforms & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR |
			VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)) &&
			options->rotation_prepare())
			vk.rotate = options->rotate;
	}
	if (options->device_ready)
		options->device_ready(vk.instance, vk.physical, vk.device, vk.queue_family, 0);
	return 1;
}

void xgpu_vulkan_shutdown(void)
{
	pipeline_compiler_stop();
	if (!vk.device)
		return;
	xgpu_vulkan_image_destroy(vk.loading_image);
	vk.loading_image = NULL;
	xgpu_vulkan_image_destroy(vk.loading_art_image);
	vk.loading_art_image = NULL;
	xgpu_vulkan_image_destroy(vk.loading_caption_image);
	vk.loading_caption_image = NULL;
	/* (the loading screen's sharp frame and its threads) */
	loading_art_hd_free(&vk.loading_art);
	if (vk.recording)
		xgpu_vulkan_fence();
	vkDeviceWaitIdle(vk.device);
	xgpu_vulkan_pacing_stop();
	xgpu_vulkan_pipeline_cache_save();
	if (vk.validation)
		host_logf(HOST_LOG_INFO, "vulkan: %u validation messages", vk.validation_messages);
}

void xgpu_vulkan_initialize_undo(void)
{
	void *library = vk.library;

	/* (what a failed start makes before it fails: the instance, the
	surface, at most the device; anything made with the device goes with
	it) */
	if (vk.device)
	{
		vkDeviceWaitIdle(vk.device);
		xgpu_vulkan_pacing_stop();
		xgpu_vulkan_rt_destroy();
		if (vkDestroyDevice)
			vkDestroyDevice(vk.device, NULL);
	}
	if (vk.instance)
	{
		if (vk.surface && vkDestroySurfaceKHR)
			vkDestroySurfaceKHR(vk.instance, vk.surface, NULL);
		if (vk.messenger && vkDestroyDebugUtilsMessengerEXT)
			vkDestroyDebugUtilsMessengerEXT(vk.instance, vk.messenger, NULL);
		if (vkDestroyInstance)
			vkDestroyInstance(vk.instance, NULL);
	}
	memset(&vk, 0, sizeof(vk));
	vk.library = library;
}
