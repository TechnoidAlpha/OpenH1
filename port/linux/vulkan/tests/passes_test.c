/*
PASSES_TEST.C

The real Vulkan core over a recording-only Vulkan: no driver or GPU.
Checks attachment load actions, clears inside a pass, multiview layer
selection, sampling an array's first layer as a 2D image, and capability
requirements on devices with optional depth formats or query precision.
Also checks the shader modules found again (by their words, or a fixed
program's by their address), packed vertex bindings, each draw's state set
only where it changed, and transfer/submission dependencies,
the texture sets a submission reuses, the order of the enhanced dynamic
lights' uploads among the draws, and anti-aliasing's passes: their order,
attachments, barriers, samples and uniforms (xgpu_vulkan_post.c); and the
clears of some of the colour's channels: drawn through their write mask,
over each rectangle, in each view, while every channel's are cleared. And
the window: a lost surface made again, the app in the background, the
present mode of the swap interval, frames turned for the display; the
pipeline cache written only when pipelines were made; and the VR effects'
passes that sample their depth: begun after the depth's writes, storing no
depth or stencil, ended by a draw or clear that writes them. And
HALO_VULKAN_DISCARDS: the passes recorded into secondary command buffers
and begun at the submission, their stores chosen from what came after them,
the frame-end discards learned over DISCARD_FRAMES frames and their count
started again, the resolves made in the pass, the transient multisampled
images, and what is done where the device lacks a part. And the passes put
aside before the pass they interrupt, whose pieces are one rendering, what
keeps the order drawn, and the buffers renamed rather than written in
order (but not the lights' block, which the draws' set names, and not the
lights' cells' upload put aside before a draw that reads them).
*/

#include "../xgpu_vulkan.c"
#include "../xgpu_vulkan_post.c"
#include "../xgpu_vulkan_post_shaders.c"
#include "../xgpu_vulkan_inspect_shader.c"
#include "../xgpu_vulkan_world_ao.c"
#include "../xgpu_vulkan_rt.c"
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>

/* display.low_latency's pacing (xgpu_vulkan_pacing.c), with the command
stream's and the host's calls it makes: never started here (the fake
device has neither present wait nor display timing) */
#include "../xgpu_vulkan_pacing.c"

void host_render_stream_display_paced(int paced)
{
	(void)paced;
}

void host_render_stream_frames_ahead(int frames)
{
	(void)frames;
}

void host_render_stream_frames_taken_reset(void)
{
}

void host_render_stream_frame_displayed(uint64_t input, uint64_t finished, uint64_t latched, uint64_t shown,
	uint64_t period)
{
	(void)input;
	(void)finished;
	(void)latched;
	(void)shown;
	(void)period;
}

void host_render_stream_frame_finished(uint64_t input, uint64_t finished)
{
	(void)input;
	(void)finished;
}

void host_render_stream_pacing_statistics(struct host_render_pacing_statistics *statistics)
{
	memset(statistics, 0, sizeof(*statistics));
}

uint64_t host_render_stream_frame_input(void)
{
	return 0;
}

uint64_t host_vulkan_display_period(void)
{
	return 16666667;
}

static struct xgpu_vulkan_image color_target, depth_target, stand_in;
static struct slot recording;
static unsigned int begins, clears, created_views;
static VkRenderingInfo began;
static VkRenderingAttachmentInfo began_color, began_depth, began_stencil;
static VkClearAttachment cleared[2];
static VkClearRect cleared_rect;
static VkImageViewCreateInfo last_view;
static VkDescriptorImageInfo written_images[4];
static VkFormatFeatureFlags depth24_features, depth32_features;
static unsigned int capability_errors;
static unsigned int ended_queries;
static unsigned int barriers, copies, bound_vertices;
/* the vertex buffers each binding has, as the command buffer has them, and
the calls that set state (a draw's state is set only where it changed) */
static VkBuffer vertex_state[3];
static VkDeviceSize vertex_state_offsets[3];
static unsigned int state_calls;
static unsigned int allocated_sets, set_updates, pool_resets, pools_made;
/* the pool each set was allocated from, by its handle (50 on) */
static VkDescriptorPool allocated_from[64];
static VkMemoryBarrier last_memory;
static VkPipelineStageFlags last_source, last_destination;

static void VKAPI_CALL fake_end_query(VkCommandBuffer command, VkQueryPool pool, uint32_t query)
{
	(void)command; (void)pool; (void)query;
	ended_queries++;
}

/* (debug.pass_times: never) */
int host_pass_times;

void host_logf(int priority, const char *format, ...)
{
	(void)format;
	if (priority == HOST_LOG_ERROR)
		capability_errors++;
}

void host_fatal(const char *format, ...)
{
	(void)format;
	abort();
}

static void VKAPI_CALL fake_begin(VkCommandBuffer command, const VkRenderingInfo *info)
{
	(void)command;
	begins++;
	began = *info;
	if (info->pColorAttachments)
		began_color = *info->pColorAttachments;
	if (info->pDepthAttachment)
		began_depth = *info->pDepthAttachment;
	if (info->pStencilAttachment)
		began_stencil = *info->pStencilAttachment;
}

static void VKAPI_CALL fake_end(VkCommandBuffer command)
{
	(void)command;
}

static void VKAPI_CALL fake_barrier(VkCommandBuffer command, VkPipelineStageFlags source, VkPipelineStageFlags destination,
	VkDependencyFlags dependency, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	(void)command; (void)source; (void)destination; (void)dependency;
	(void)memory_count; (void)memory; (void)buffer_count; (void)buffers; (void)image_count; (void)images;
	barriers++;
	if (memory_count)
		last_memory = memory[0];
	last_source = source;
	last_destination = destination;
}

static void VKAPI_CALL fake_clear(VkCommandBuffer command, uint32_t attachment_count, const VkClearAttachment *attachments,
	uint32_t rect_count, const VkClearRect *rects)
{
	(void)command;
	assert(attachment_count <= 2 && rect_count > 0);
	clears++;
	memcpy(cleared, attachments, attachment_count * sizeof(*attachments));
	cleared_rect = rects[0];
}

static VkResult VKAPI_CALL fake_view(VkDevice device, const VkImageViewCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkImageView *view)
{
	(void)device; (void)allocator;
	last_view = *info;
	*view = (VkImageView)(uintptr_t)(++created_views + 100);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_allocate(VkDevice device, const VkDescriptorSetAllocateInfo *info, VkDescriptorSet *set)
{
	(void)device;
	if (allocated_sets < 64)
		allocated_from[allocated_sets] = info->descriptorPool;
	*set = (VkDescriptorSet)(uintptr_t)(50 + allocated_sets++);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL fake_make_pool(VkDevice device, const VkDescriptorPoolCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkDescriptorPool *pool)
{
	(void)device; (void)info; (void)allocator;
	*pool = (VkDescriptorPool)(uintptr_t)(90 + pools_made++);
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_update(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
	uint32_t copies, const VkCopyDescriptorSet *copy)
{
	uint32_t index;

	(void)device; (void)copies; (void)copy;
	assert(count == 4);
	set_updates++;
	for (index = 0; index < count; index++)
	{
		assert(writes[index].dstBinding == index && writes[index].descriptorCount == 1);
		assert(writes[index].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		written_images[index] = *writes[index].pImageInfo;
	}
}

/* a clear's depth as the core clears it: reversed (xgpu_vulkan.h) */
#define CLEARED(depth) (XGPU_VULKAN_REVERSED_DEPTH ? 1.0f - (depth) : (depth))

static void VKAPI_CALL fake_format_properties(VkPhysicalDevice physical, VkFormat format, VkFormatProperties *properties)
{
	(void)physical;
	memset(properties, 0, sizeof(*properties));
	if (format == VK_FORMAT_D24_UNORM_S8_UINT)
		properties->optimalTilingFeatures = depth24_features;
	else if (format == VK_FORMAT_D32_SFLOAT_S8_UINT)
		properties->optimalTilingFeatures = depth32_features;
	else
		assert(0);
}

static void capabilities_test(void)
{
	VkPhysicalDeviceFeatures2 features = { 0 };
	VkPhysicalDeviceVulkan12Features features12 = { 0 };
	VkPhysicalDeviceVulkan13Features features13 = { 0 };
	VkFormatFeatureFlags attachment = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VkFormatFeatureFlags sampled = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	vkGetPhysicalDeviceFormatProperties = fake_format_properties;
	depth24_features = depth32_features = attachment | sampled;
	/* (reversed depth in a float where the device has both: xgpu_vulkan.h) */
	assert(depth_format_choose() && vk.depth_format ==
		(XGPU_VULKAN_REVERSED_DEPTH ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D24_UNORM_S8_UINT));
	/* D24 may draw correctly but not support the scope's sampled reads. */
	depth24_features = attachment;
	assert(depth_format_choose() && vk.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT);
	depth24_features = 0;
	assert(depth_format_choose() && vk.depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT);
	depth24_features = attachment | sampled;
	depth32_features = 0;
	assert(depth_format_choose() && vk.depth_format == VK_FORMAT_D24_UNORM_S8_UINT);
	capability_errors = 0;
	depth24_features = attachment;
	depth32_features = sampled;
	assert(!depth_format_choose() && vk.depth_format == VK_FORMAT_UNDEFINED);
	assert(capability_errors == 1);
	depth24_features = depth32_features = 0;
	assert(!depth_format_choose() && capability_errors == 2);

	features.features.occlusionQueryPrecise = VK_TRUE;
	features12.timelineSemaphore = features12.hostQueryReset = VK_TRUE;
	features13.dynamicRendering = VK_TRUE;
	assert(required_features(&features, &features12, &features13));
	features.features.occlusionQueryPrecise = VK_FALSE;
	assert(!required_features(&features, &features12, &features13));
	features.features.occlusionQueryPrecise = VK_TRUE;
	features12.timelineSemaphore = VK_FALSE;
	assert(!required_features(&features, &features12, &features13));
	features12.timelineSemaphore = VK_TRUE;
	features12.hostQueryReset = VK_FALSE;
	assert(!required_features(&features, &features12, &features13));
	features12.hostQueryReset = VK_TRUE;
	features13.dynamicRendering = VK_FALSE;
	assert(!required_features(&features, &features12, &features13));
	assert(capability_errors == 6);
}

static void reset(void)
{
	int index;

	free(color_target.attachments);
	free(depth_target.attachments);
	free(vk.garbage);
	(void)index;
	free(vk.texture_sets);
	memset(&vk, 0, sizeof(vk));
	memset(&color_target, 0, sizeof(color_target));
	memset(&depth_target, 0, sizeof(depth_target));
	memset(&recording, 0, sizeof(recording));
	memset(&began, 0, sizeof(began));
	memset(&began_color, 0, sizeof(began_color));
	memset(&began_depth, 0, sizeof(began_depth));
	memset(&began_stencil, 0, sizeof(began_stencil));
	begins = clears = created_views = 0;
	color_target.description.width = depth_target.description.width = 64;
	color_target.description.height = depth_target.description.height = 48;
	color_target.description.type = depth_target.description.type = XGPU_VULKAN_IMAGE_2D_ARRAY;
	color_target.description.depth_or_layers = depth_target.description.depth_or_layers = 4;
	color_target.description.levels = depth_target.description.levels = 1;
	color_target.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	depth_target.aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	vk.recording = 1;
	vk.serial = 11;
	vk.slot = &recording;
	vk.color = &color_target;
	vk.depth = &depth_target;
	vk.views = 1;
	stand_in.sampled = (VkImageView)(uintptr_t)20;
	for (index = 0; index < 4; index++)
		vk.stand_ins[index] = &stand_in;
	vkCmdBeginRendering = fake_begin;
	vkCmdEndRendering = fake_end;
	vkCmdEndQuery = fake_end_query;
	vkCmdClearAttachments = fake_clear;
	vkCmdPipelineBarrier = fake_barrier;
	vkCreateImageView = fake_view;
	vkAllocateDescriptorSets = fake_allocate;
	vkUpdateDescriptorSets = fake_update;
	vkCreateDescriptorPool = fake_make_pool;
}

static VkResult VKAPI_CALL fake_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
	const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *pipeline)
{
	const VkPipelineVertexInputStateCreateInfo *input = info->pVertexInputState;
	const uint32_t expected[4] = { 1, 0, 2, 2 };
	uint32_t index;

	(void)device; (void)cache; (void)allocator;
	assert(count == 1 && input->vertexBindingDescriptionCount == 3 && input->vertexAttributeDescriptionCount == 4);
	for (index = 0; index < 3; index++)
		assert(input->pVertexBindingDescriptions[index].binding == index);
	assert(input->pVertexBindingDescriptions[0].stride == 12);
	assert(input->pVertexBindingDescriptions[1].stride == 32);
	assert(input->pVertexBindingDescriptions[2].stride == 0);
	for (index = 0; index < 4; index++)
	{
		assert(input->pVertexAttributeDescriptions[index].location == index);
		assert(input->pVertexAttributeDescriptions[index].binding == expected[index]);
		assert(input->pVertexAttributeDescriptions[index].offset == index * 16);
	}
	*pipeline = (VkPipeline)(uintptr_t)10;
	return VK_SUCCESS;
}

static void VKAPI_CALL fake_bind_vertices(VkCommandBuffer command, uint32_t first, uint32_t count,
	const VkBuffer *buffers, const VkDeviceSize *offsets)
{
	const uint32_t sources[3] = { 3, 15, XGPU_VULKAN_BINDING_CONSTANT };

	(void)command;
	assert(count == 1 && first < 3);
	assert(buffers[0] == (VkBuffer)(uintptr_t)(100 + sources[first]));
	assert(offsets[0] == sources[first] * 16);
	vertex_state[first] = buffers[0];
	vertex_state_offsets[first] = offsets[0];
	bound_vertices++;
	state_calls++;
}

static void VKAPI_CALL counted_bind_vertices(VkCommandBuffer command, uint32_t first, uint32_t count,
	const VkBuffer *buffers, const VkDeviceSize *offsets)
{
	(void)command;
	assert(count == 1 && first < 3);
	vertex_state[first] = buffers[0];
	vertex_state_offsets[first] = offsets[0];
	bound_vertices++;
	state_calls++;
}

static void VKAPI_CALL fake_viewport(VkCommandBuffer c, uint32_t first, uint32_t count, const VkViewport *v)
{ (void)c; (void)first; (void)count; (void)v; state_calls++; }
static void VKAPI_CALL fake_scissor(VkCommandBuffer c, uint32_t first, uint32_t count, const VkRect2D *r)
{ (void)c; (void)first; (void)count; (void)r; state_calls++; }
static float bias_state[2];
static void VKAPI_CALL fake_bias(VkCommandBuffer c, float constant, float clamp, float slope)
{ (void)c; (void)clamp; bias_state[0] = slope; bias_state[1] = constant; state_calls++; }
static void VKAPI_CALL fake_blend(VkCommandBuffer c, const float color[4])
{ (void)c; (void)color; state_calls++; }
static void VKAPI_CALL fake_stencil(VkCommandBuffer c, VkStencilFaceFlags face, uint32_t reference)
{ (void)c; (void)face; (void)reference; state_calls++; }
static void VKAPI_CALL fake_topology(VkCommandBuffer c, VkPrimitiveTopology topology)
{ (void)c; (void)topology; state_calls++; }
static void VKAPI_CALL fake_bind_sets(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
	uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic_count, const uint32_t *offsets)
{ (void)c; (void)point; (void)layout; (void)first; (void)count; (void)sets; (void)dynamic_count; (void)offsets; state_calls++; }
static void VKAPI_CALL plain_draw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t instance)
{ (void)c; (void)vertices; (void)instances; (void)first; (void)instance; }
/* (each binding the pipeline reads has its stream's buffer, set by this
draw or kept from the one before) */
static void VKAPI_CALL fake_draw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t instance)
{
	const uint32_t sources[3] = { 3, 15, XGPU_VULKAN_BINDING_CONSTANT };
	int index;

	(void)c; (void)vertices; (void)instances; (void)first; (void)instance;
	for (index = 0; index < 3; index++)
	{
		assert(vertex_state[index] == (VkBuffer)(uintptr_t)(100 + sources[index]));
		assert(vertex_state_offsets[index] == sources[index] * 16);
	}
}

static void vertex_input_test(void)
{
	struct xgpu_vulkan_pipeline_description description = { 0 };
	struct xgpu_vulkan_draw draw = { 0 };
	struct xgpu_vulkan_dynamic_state dynamic = { 0 };
	VkDescriptorPool pool = (VkDescriptorPool)(uintptr_t)1;
	const uint32_t sources[4] = { 15, 3, XGPU_VULKAN_BINDING_CONSTANT, XGPU_VULKAN_BINDING_CONSTANT };
	VkResult result;
	double elapsed;
	uint32_t index;

	reset();
	description.vertex_shader = description.pixel_shader = (VkShaderModule)(uintptr_t)1;
	for (index = 0; index < 4; index++)
	{
		description.attributes[index].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		description.attributes[index].binding = sources[index];
		description.attributes[index].offset = index * 16;
		draw.bindings |= 1u << sources[index];
		draw.vertices[sources[index]].buffer = (VkBuffer)(uintptr_t)(100 + sources[index]);
		draw.vertices[sources[index]].offset = sources[index] * 16;
	}
	description.strides[3] = 12;
	description.strides[15] = 32;
	vkCreateGraphicsPipelines = fake_pipeline;
	draw.pipeline = pipeline_build(&description, &result, &elapsed);
	assert(draw.pipeline && result == VK_SUCCESS);
	vk.bound_pipeline = draw.pipeline;
	dynamic.viewport[2] = 64;
	dynamic.viewport[3] = 48;
	draw.dynamic = &dynamic;
	draw.count = 3;
	recording.descriptor_pools = &pool;
	recording.descriptor_pool_count = 1;
	vkCmdBindVertexBuffers = fake_bind_vertices;
	vkCmdSetViewport = fake_viewport;
	vkCmdSetScissor = fake_scissor;
	vkCmdSetDepthBias = fake_bias;
	vkCmdSetBlendConstants = fake_blend;
	vkCmdSetStencilReference = fake_stencil;
	vkCmdSetPrimitiveTopology = fake_topology;
	vkCmdBindDescriptorSets = fake_bind_sets;
	vkCmdDraw = fake_draw;
	bound_vertices = 0;
	memset(vertex_state, 0, sizeof(vertex_state));
	xgpu_vulkan_draw(&draw);
	assert(bound_vertices == 3);
	/* The same draw again sets nothing again: the command buffer keeps the
	state, the buffers and the sets; a change sets only what changed. */
	bound_vertices = state_calls = 0;
	xgpu_vulkan_draw(&draw);
	assert(bound_vertices == 0 && state_calls == 0);
	draw.vertices[15].offset = 7 * 16;
	draw.vertices[15].buffer = (VkBuffer)(uintptr_t)(100 + 7);
	vkCmdBindVertexBuffers = counted_bind_vertices;
	vkCmdDraw = plain_draw;
	xgpu_vulkan_draw(&draw);
	assert(bound_vertices == 1 && state_calls == 1 && vertex_state[1] == (VkBuffer)(uintptr_t)107);
	draw.vertices[15].offset = 15 * 16;
	draw.vertices[15].buffer = (VkBuffer)(uintptr_t)(100 + 15);
	xgpu_vulkan_draw(&draw);
	vkCmdBindVertexBuffers = fake_bind_vertices;
	vkCmdDraw = fake_draw;
	bound_vertices = state_calls = 0;
	dynamic.depth_bias[0] = 2.0f;
	dynamic.depth_bias[1] = 1.0f;
	xgpu_vulkan_draw(&draw);
	/* (on reversed depth: the slope's turned, the constant the shaders') */
	if (XGPU_VULKAN_REVERSED_DEPTH)
		assert(state_calls == 1 && bias_state[0] == -2.0f && bias_state[1] == 0.0f);
	else
		assert(state_calls == 1 && bias_state[0] == 2.0f && bias_state[1] == 1.0f);
	draw.uniforms.offset = 256;
	state_calls = 0;
	xgpu_vulkan_draw(&draw);
	assert(state_calls == 1);
	/* A recording begun knows nothing of the last one's state. */
	memset(&vk.bound, 0, sizeof(vk.bound));
	state_calls = bound_vertices = 0;
	xgpu_vulkan_draw(&draw);
	assert(bound_vertices == 3 && state_calls == 10);
	/* A draw that reads the dynamic lights orders their next upload after
	it; others leave it free to go before the submission's draws. */
	{
		static struct xgpu_vulkan_buffer block;
		static struct xgpu_vulkan_image cells;

		vk.lights_block = &block;
		vk.lights_cells = &cells;
		xgpu_vulkan_draw(&draw);
		assert(block.used == 0 && cells.used == 0);
		draw.dynamic_lights = 1;
		xgpu_vulkan_draw(&draw);
		assert(block.used == vk.serial && cells.used == vk.serial);
	}
	/* The VR mode's effects sample the depth attachment they are tested
	against: in a pass of their own that writes neither depth nor stencil
	(and stores neither), begun after the depth's writes, which draws that
	write neither share; a draw that writes depth ends it, as a clear of
	depth does. */
	{
		const float black[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		unsigned int before;

		draw.dynamic_lights = 0;
		draw.writes_depth = 1;
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		before = begins;
		assert(vk.pass_active && !vk.pass_reads_depth);
		draw.writes_depth = 0;
		draw.reads_depth = 1;
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		assert(begins == before + 1 && vk.pass_reads_depth);
		/* (without discards the pass is the main command buffer's, which
		keeps the state bound before it) */
		assert(!vk.deferred && bound_vertices == 0);
		assert(began_depth.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_depth.storeOp == VK_ATTACHMENT_STORE_OP_NONE);
		assert(began_stencil.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_stencil.storeOp == VK_ATTACHMENT_STORE_OP_NONE);
		assert(began_color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		draw.reads_depth = 0;
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		assert(begins == before + 1 && vk.pass_reads_depth);
		draw.writes_depth = 1;
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		assert(begins == before + 2 && !vk.pass_reads_depth && began_depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
		draw.writes_depth = 0;
		draw.reads_depth = 1;
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		assert(begins == before + 3 && vk.pass_reads_depth);
		xgpu_vulkan_clear((const int32_t[][4]){ { 0, 0, 8, 8 } }, 1, XGPU_VULKAN_CLEAR_DEPTH, black, 1.0f, 0);
		assert(begins == before + 4 && !vk.pass_reads_depth && began_depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
		/* (targets changed: the next pass is an ordinary one) */
		bound_vertices = 0;
		xgpu_vulkan_draw(&draw);
		xgpu_vulkan_set_target_views(&color_target, NULL, 0, 1);
		assert(!vk.pass_active && !vk.pass_reads_depth);
		draw.reads_depth = 0;
	}
}

static unsigned int light_copies, light_image_copies;
static VkCommandBuffer light_commands;

static void VKAPI_CALL fake_copy_lights(VkCommandBuffer command, VkBuffer source, VkBuffer destination,
	uint32_t count, const VkBufferCopy *regions)
{
	(void)source; (void)destination;
	assert(count == 1 && regions[0].dstOffset == 0 && regions[0].size == 6192);
	light_commands = command;
	light_copies++;
}

/* (the cells and the clusters' masks, 17 slices from 0; or the world, its
whole slices from 17) */
static uint32_t light_image_z, light_image_depth, light_image_y = 0, light_image_height = 32;

static void VKAPI_CALL fake_copy_cells(VkCommandBuffer command, VkBuffer source, VkImage destination,
	VkImageLayout layout, uint32_t count, const VkBufferImageCopy *regions)
{
	(void)source; (void)destination;
	assert(layout == VK_IMAGE_LAYOUT_GENERAL && count == 1);
	assert(regions[0].imageExtent.width == 32 && regions[0].imageExtent.height == light_image_height &&
		regions[0].imageOffset.y == (int32_t)light_image_y && regions[0].imageOffset.z == (int32_t)light_image_z &&
		regions[0].imageExtent.depth == light_image_depth);
	assert(regions[0].imageSubresource.baseArrayLayer == 0 && regions[0].imageSubresource.layerCount == 1);
	assert(command == light_commands);
	light_image_copies++;
}

/* The enhanced dynamic lights' block and cells: written before the
submission's draws where none of them read the lights, in their order
where one did. */
static void dynamic_lights_test(void)
{
	static struct xgpu_vulkan_buffer block;
	static struct xgpu_vulkan_image cells;
	static unsigned char ring[6192 + 32 * 32 * 17 * 16 + 64], data[6192], grid[32 * 32 * 17 * 16];
	static uint32_t world[1500][4];

	reset();
	vk.slot->main = (VkCommandBuffer)(uintptr_t)1;
	vk.slot->early = (VkCommandBuffer)(uintptr_t)2;
	vk.early_recording = 1;
	vk.ring_mapped = ring;
	block.size = sizeof(data);
	block.buffer = (VkBuffer)(uintptr_t)3;
	cells.description.type = XGPU_VULKAN_IMAGE_3D;
	cells.description.format = XGPU_VULKAN_FORMAT_RGBA32_UINT;
	cells.description.width = cells.description.height = 32;
	cells.description.depth_or_layers = 17 + 240;
	cells.description.levels = 1;
	cells.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	vk.lights_block = &block;
	vk.lights_cells = &cells;
	light_image_z = 0;
	light_image_depth = 17;
	data[0] = 7;
	grid[sizeof(grid) - 1] = 9;
	vkCmdCopyBuffer = fake_copy_lights;
	vkCmdCopyBufferToImage = fake_copy_cells;
	light_copies = light_image_copies = 0;
	/* (the first upload: all of the cells, whatever changed) */
	vk.lights_cells_uploaded = 0;
	xgpu_vulkan_dynamic_lights_upload(data, sizeof(data), grid, 0, 0);
	assert(light_copies == 1 && light_image_copies == 1 && light_commands == vk.slot->early);
	assert(vk.statistics.uploads_early == 2 && vk.statistics.uploads_ordered == 0);
	assert(ring[0] == 7 && ring[6192 + sizeof(grid) - 1] == 9);
	block.used = cells.used = vk.serial;
	vk.ring_head = 0;
	xgpu_vulkan_dynamic_lights_upload(data, sizeof(data), grid, 0, sizeof(grid));
	assert(light_copies == 2 && light_image_copies == 2 && light_commands == vk.slot->main);
	assert(vk.statistics.uploads_ordered == 2);
	/* (none changed: the block alone; some of a slice's rows: those rows;
	more than one slice: those slices) */
	vk.ring_head = 0;
	xgpu_vulkan_dynamic_lights_upload(data, sizeof(data), grid, 4096, 4096);
	assert(light_copies == 3 && light_image_copies == 2);
	vk.ring_head = 0;
	grid[3 * 16384 + 5 * 512 + 16] = 21;
	light_image_z = 3;
	light_image_depth = 1;
	light_image_y = 5;
	light_image_height = 3;
	xgpu_vulkan_dynamic_lights_upload(data, sizeof(data), grid, 3 * 16384 + 5 * 512 + 16, 3 * 16384 + 7 * 512 + 32);
	assert(light_copies == 4 && light_image_copies == 3 && ring[6192 + 16] == 21);
	vk.ring_head = 0;
	light_image_z = 2;
	light_image_depth = 2;
	light_image_y = 0;
	light_image_height = 32;
	xgpu_vulkan_dynamic_lights_upload(data, sizeof(data), grid, 2 * 16384 + 31 * 512, 3 * 16384 + 16);
	assert(light_copies == 5 && light_image_copies == 4 && ring[6192 + 16384 + 5 * 512 + 16] == 21);
	/* the world: its 1500 texels in two whole slices past the clusters' */
	world[1499][3] = 11;
	light_image_z = 17;
	light_image_depth = 2;
	vk.ring_head = 0;
	xgpu_vulkan_dynamic_lights_world(world, 1500);
	assert(light_image_copies == 5 && ring[1499 * 16 + 12] == 11 && ring[1500 * 16] == 0);
}

static void VKAPI_CALL fake_copy_buffer(VkCommandBuffer command, VkBuffer source, VkBuffer destination,
	uint32_t count, const VkBufferCopy *regions)
{
	(void)command; (void)source; (void)destination;
	assert(count == 1 && regions[0].dstOffset == 0 && regions[0].size == 16);
	/* A second write to the same bytes must follow the first's barrier. */
	assert(barriers == copies);
	copies++;
}

static void uploads_test(void)
{
	struct xgpu_vulkan_buffer buffer = { 0 };
	unsigned char ring[64], data[16] = { 1 };

	reset();
	vk.slot->main = (VkCommandBuffer)(uintptr_t)1;
	vk.slot->early = (VkCommandBuffer)(uintptr_t)2;
	vk.early_recording = 1;
	vk.ring_mapped = ring;
	buffer.size = sizeof(ring);
	buffer.buffer = (VkBuffer)(uintptr_t)3;
	vkCmdCopyBuffer = fake_copy_buffer;
	barriers = copies = 0;
	xgpu_vulkan_buffer_write(&buffer, 0, data, sizeof(data), 0);
	xgpu_vulkan_buffer_write(&buffer, 0, data, sizeof(data), 0);
	assert(copies == 2 && barriers == 2);
	assert(last_source == VK_PIPELINE_STAGE_TRANSFER_BIT && last_destination == VK_PIPELINE_STAGE_TRANSFER_BIT);
	assert(last_memory.srcAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT);
	assert(last_memory.dstAccessMask & VK_ACCESS_TRANSFER_WRITE_BIT);
	assert(!memcmp(ring, data, sizeof(data)) && !memcmp(ring + 16, data, sizeof(data)));
	buffer.used = vk.serial;
	xgpu_vulkan_buffer_write(&buffer, 0, data, sizeof(data), 0);
	assert(copies == 3 && barriers == 3 && last_destination == AFTER_STAGES);
	assert(last_memory.dstAccessMask == AFTER_ACCESS);
}

static VkResult VKAPI_CALL fake_reset_pool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags)
{ (void)device; (void)pool; (void)flags; return VK_SUCCESS; }

static VkResult VKAPI_CALL fake_begin_commands(VkCommandBuffer command, const VkCommandBufferBeginInfo *info)
{ (void)command; (void)info; return VK_SUCCESS; }

static void submissions_test(void)
{
	reset();
	vk.recording = 0;
	vkResetCommandPool = fake_reset_pool;
	vkBeginCommandBuffer = fake_begin_commands;
	barriers = 0;
	recording_begin();
	/* Even without an early command buffer, the next frame cannot write
	its target while the previous presentation blit still reads it. */
	assert(barriers == 1 && vk.recording && !vk.early_recording);
	assert(last_source == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	assert(last_destination == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	assert(last_memory.srcAccessMask == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
	assert(last_memory.dstAccessMask == (VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT));
	recording_begin();
	assert(barriers == 1);
}

static VkResult VKAPI_CALL fake_reset_descriptors(VkDevice device, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags)
{ (void)device; (void)pool; (void)flags; pool_resets++; return VK_SUCCESS; }

static VkResult VKAPI_CALL fake_counter(VkDevice device, VkSemaphore semaphore, uint64_t *value)
{ (void)device; (void)semaphore; *value = vk.serial; return VK_SUCCESS; }

/* Draws that come back to earlier textures reuse the sets kept for them,
in this submission and the ones after. Another sampler or an image made
where a destroyed one was never gives back a set written before. Once as
many are kept as there can be, the one the GPU finished with that is found
first is written again; while every one is read by work not done, a set of
the slot's pools is written, kept for nothing. */
static void texture_sets_test(void)
{
	static struct xgpu_vulkan_image first, second;
	struct xgpu_vulkan_texture a[4], b[4];
	VkDescriptorPool pools[2] = { (VkDescriptorPool)(uintptr_t)7, (VkDescriptorPool)(uintptr_t)8 };
	VkDescriptorSet set_a, set_b, set;
	struct slot *slot;
	uint32_t index;

	reset();
	vk.recording = 0;
	vkResetCommandPool = fake_reset_pool;
	vkBeginCommandBuffer = fake_begin_commands;
	vkResetDescriptorPool = fake_reset_descriptors;
	vkGetSemaphoreCounterValue = fake_counter;
	slot = &vk.slots[vk.serial % SLOTS];
	slot->descriptor_pools = pools;
	slot->descriptor_pool_count = 2;
	pool_resets = pools_made = 0;
	recording_begin();
	assert(vk.slot == slot && !pool_resets);
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	first.sampled = (VkImageView)(uintptr_t)31;
	first.number = 1;
	second.sampled = (VkImageView)(uintptr_t)32;
	second.number = 2;
	memset(a, 0, sizeof(a));
	memset(b, 0, sizeof(b));
	a[0].image = &first;
	b[0].image = &second;
	a[0].kind = b[0].kind = XGPU_VULKAN_IMAGE_2D;
	a[0].sampler = b[0].sampler = (VkSampler)(uintptr_t)5;
	allocated_sets = set_updates = 0;
	set_a = texture_set(a);
	set_b = texture_set(b);
	assert(set_a && set_b && set_a != set_b && set_updates == 2);
	assert(pools_made == 1 && allocated_from[0] == vk.texture_set_pool && allocated_from[1] == vk.texture_set_pool);
	assert(texture_set(a) == set_a && texture_set(b) == set_b);
	assert(texture_set(a) == set_a && set_updates == 2 && allocated_sets == 2);
	assert(vk.statistics.texture_sets_written == 2 && vk.statistics.texture_sets_reused == 3);
	assert(written_images[0].imageView == second.sampled);
	assert(first.used == vk.serial && second.used == vk.serial);

	a[0].sampler = (VkSampler)(uintptr_t)6;
	set = texture_set(a);
	assert(set != set_a && set != set_b && set_updates == 3);
	assert(written_images[0].sampler == (VkSampler)(uintptr_t)6);
	a[0].sampler = (VkSampler)(uintptr_t)5;
	assert(texture_set(a) == set_a && set_updates == 3);

	/* (first destroyed and another image made at its address, as
	xgpu_vulkan_image_create numbers it; the last set is b's, as
	xgpu_vulkan_image_destroy forgets the last set when it had first) */
	assert(texture_set(b) == set_b);
	first.number = 3;
	first.sampled = (VkImageView)(uintptr_t)33;
	set = texture_set(a);
	assert(set != set_a && set != set_b && set_updates == 4);
	assert(written_images[0].imageView == (VkImageView)(uintptr_t)33);
	assert(texture_set(b) == set_b && texture_set(a) == set && set_updates == 4);

	/* The slot's next submission: the kept sets are found again (none
	was allocated from the slot's pools, which are not reset), their images
	used by it. */
	vk.recording = 0;
	vk.serial += SLOTS;
	recording_begin();
	assert(vk.slot == slot && !pool_resets && !slot->descriptor_pool_dirty);
	assert(texture_set(b) == set_b && texture_set(a) == set && set_updates == 4 && allocated_sets == 4);
	assert(second.used == vk.serial && first.used == vk.serial);

	/* As many kept as there can be: each one used by this submission, a
	set of the slot's pools; one the GPU has done with, written again */
	vk.texture_set_count = TEXTURE_SETS_KEPT;
	for (index = 0; index < TEXTURE_SETS_KEPT; index++)
	{
		if (!vk.texture_sets[index].set)
			vk.texture_sets[index].set = (VkDescriptorSet)(uintptr_t)(1000 + index);
		vk.texture_sets[index].used = vk.serial;
	}
	b[0].sampler = (VkSampler)(uintptr_t)7;
	set = texture_set(b);
	assert(set && set_updates == 5 && allocated_sets == 5 && allocated_from[4] == pools[0]);
	b[0].sampler = (VkSampler)(uintptr_t)8;
	vk.texture_sets[TEXTURE_SETS_KEPT - 2].used = vk.serial - 1;
	set = texture_set(b);
	assert(set == vk.texture_sets[TEXTURE_SETS_KEPT - 2].set && set_updates == 6 && allocated_sets == 5);
	assert(texture_set(a) && texture_set(b) == set && set_updates == 6);

	/* An image destroyed and another made at its address whose view the
	driver gives the destroyed view's handle: its number is new
	(xgpu_vulkan_image_create's count, never reused), so a kept set of
	the old view is never bound for it. */
	set = texture_set(a);
	assert(set_updates == 6);
	first.number = 4;
	/* (the last set b's, as xgpu_vulkan_image_destroy leaves it) */
	assert(texture_set(b));
	assert(texture_set(a) != set && set_updates == 7);
	assert(written_images[0].imageView == (VkImageView)(uintptr_t)33);
	slot->descriptor_pools = NULL;
	slot->descriptor_pool_count = 0;
	reset();
}

/* ---------- anti-aliasing's passes */

/* what the passes record, in order: C a copy, X a barrier, B a pass begun
(c its colour cleared, l loaded), D a draw, E a pass ended */
static char post_events[64];
static VkImageCopy post_copied;
static VkImageView post_attachments[4];
static VkRenderingAttachmentInfo post_began[4];
static uint32_t post_begins, post_draws, post_updates, post_masks[4], post_pipelines;
static VkSampleCountFlagBits post_pipeline_samples[4];
static VkDescriptorImageInfo post_sampled[4][4];
static VkViewport post_viewports[4];
static VkRect2D post_scissors[4];
static uint32_t post_offsets[4][2];
/* the state the command buffer has (each draw's, as it draws: a draw sets
only what changed) */
static VkViewport post_viewport_state;
static VkRect2D post_scissor_state;
static uint32_t post_offset_state[2];
static VkPipelineStageFlags post_barrier_sources[8], post_barrier_destinations[8];
static VkAccessFlags post_barrier_access[8];
static uint32_t post_barriers;
static unsigned char post_ring[65536];
static VkDescriptorPool post_pool = (VkDescriptorPool)(uintptr_t)7;

static void post_event(char event)
{
	size_t length = strlen(post_events);

	assert(length + 1 < sizeof(post_events));
	post_events[length] = event;
}

static void VKAPI_CALL post_copy(VkCommandBuffer command, VkImage source, VkImageLayout source_layout,
	VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageCopy *regions)
{
	(void)command;
	assert(source == (VkImage)(uintptr_t)1 && destination == (VkImage)(uintptr_t)2);
	assert(source_layout == VK_IMAGE_LAYOUT_GENERAL && destination_layout == VK_IMAGE_LAYOUT_GENERAL && count == 1);
	post_copied = regions[0];
	post_event('C');
}

static void VKAPI_CALL post_barrier(VkCommandBuffer command, VkPipelineStageFlags source, VkPipelineStageFlags destination,
	VkDependencyFlags dependency, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	(void)command; (void)dependency; (void)buffer_count; (void)buffers; (void)image_count; (void)images;
	assert(memory_count == 1 && post_barriers < 8);
	post_barrier_sources[post_barriers] = source;
	post_barrier_destinations[post_barriers] = destination;
	post_barrier_access[post_barriers] = memory[0].dstAccessMask;
	post_barriers++;
	post_event('X');
}

static void VKAPI_CALL post_begin(VkCommandBuffer command, const VkRenderingInfo *info)
{
	(void)command;
	assert(post_begins < 4 && info->colorAttachmentCount == 1 && !info->pDepthAttachment && !info->viewMask);
	post_began[post_begins] = info->pColorAttachments[0];
	post_attachments[post_begins] = info->pColorAttachments[0].imageView;
	post_begins++;
	post_event('B');
	post_event(info->pColorAttachments[0].loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ? 'c' : 'l');
}

static void VKAPI_CALL post_end(VkCommandBuffer command)
{
	(void)command;
	post_event('E');
}

static VkResult VKAPI_CALL post_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
	const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *pipeline)
{
	const VkPipelineRenderingCreateInfo *rendering = info->pNext;

	(void)device; (void)cache; (void)allocator;
	/* one triangle from the vertex's index, into BGRA8 alone, nothing
	culled, tested or blended */
	assert(count == 1 && post_pipelines < 4);
	assert(!info->pVertexInputState->vertexBindingDescriptionCount &&
		!info->pVertexInputState->vertexAttributeDescriptionCount);
	assert(info->pInputAssemblyState->topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	assert(info->pRasterizationState->cullMode == VK_CULL_MODE_NONE);
	assert(!info->pDepthStencilState->depthTestEnable && !info->pDepthStencilState->stencilTestEnable);
	assert(!info->pColorBlendState->pAttachments[0].blendEnable);
	assert(rendering->colorAttachmentCount == 1 && rendering->pColorAttachmentFormats[0] == VK_FORMAT_B8G8R8A8_UNORM);
	assert(rendering->depthAttachmentFormat == VK_FORMAT_UNDEFINED && !rendering->viewMask);
	post_pipeline_samples[post_pipelines] = info->pMultisampleState->rasterizationSamples;
	post_masks[post_pipelines] = info->pColorBlendState->pAttachments[0].colorWriteMask;
	*pipeline = (VkPipeline)(uintptr_t)(200 + post_pipelines++);
	return VK_SUCCESS;
}

static void VKAPI_CALL post_bind_pipeline(VkCommandBuffer command, VkPipelineBindPoint point, VkPipeline pipeline)
{
	(void)command; (void)point; (void)pipeline;
}

static void VKAPI_CALL post_update(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
	uint32_t copies, const VkCopyDescriptorSet *copy)
{
	uint32_t index;

	(void)device; (void)copies; (void)copy;
	assert(count == 4 && post_updates < 4);
	for (index = 0; index < count; index++)
		post_sampled[post_updates][index] = *writes[index].pImageInfo;
	post_updates++;
}

static void VKAPI_CALL post_viewport(VkCommandBuffer c, uint32_t first, uint32_t count, const VkViewport *v)
{
	(void)c;
	assert(first == 0 && count == 1);
	post_viewport_state = v[0];
}

static void VKAPI_CALL post_scissor(VkCommandBuffer c, uint32_t first, uint32_t count, const VkRect2D *r)
{
	(void)c;
	assert(first == 0 && count == 1);
	post_scissor_state = r[0];
}

static void VKAPI_CALL post_bind_sets(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
	uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic_count, const uint32_t *offsets)
{
	(void)c; (void)point; (void)layout; (void)sets;
	assert(first == 0 && count == 2 && dynamic_count == 2);
	post_offset_state[0] = offsets[0];
	post_offset_state[1] = offsets[1];
}

static void VKAPI_CALL post_draw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t instance)
{
	(void)c;
	assert(vertices == 3 && instances == 1 && first == 0 && instance == 0 && post_draws < 4);
	post_viewports[post_draws] = post_viewport_state;
	post_scissors[post_draws] = post_scissor_state;
	post_offsets[post_draws][0] = post_offset_state[0];
	post_offsets[post_draws][1] = post_offset_state[1];
	post_draws++;
	post_event('D');
}

static void post_image(struct xgpu_vulkan_image *image, uintptr_t number, uint8_t format, uint32_t width, uint32_t height)
{
	free(image->attachments);
	memset(image, 0, sizeof(*image));
	image->image = (VkImage)number;
	image->sampled = (VkImageView)(number + 40);
	image->number = (uint32_t)number;
	image->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	image->description.type = XGPU_VULKAN_IMAGE_2D;
	image->description.format = format;
	image->description.width = width;
	image->description.height = height;
	image->description.depth_or_layers = 1;
	image->description.levels = 1;
}

static void post_reset(void)
{
	reset();
	memset(post_events, 0, sizeof(post_events));
	post_begins = post_draws = post_updates = post_barriers = 0;
	vk.color = vk.depth = NULL;
	vk.ring_mapped = post_ring;
	vk.ring = (VkBuffer)(uintptr_t)9;
	recording.main = (VkCommandBuffer)(uintptr_t)1;
	recording.descriptor_pools = &post_pool;
	recording.descriptor_pool_count = 1;
	vkCmdCopyImage = post_copy;
	vkCmdPipelineBarrier = post_barrier;
	vkCmdBeginRendering = post_begin;
	vkCmdEndRendering = post_end;
	vkUpdateDescriptorSets = post_update;
	vkCmdSetViewport = post_viewport;
	vkCmdSetScissor = post_scissor;
	vkCmdBindDescriptorSets = post_bind_sets;
	vkCmdDraw = post_draw;
	vkCreateGraphicsPipelines = post_pipeline;
	vkCmdBindPipeline = post_bind_pipeline;
}

/* SMAA's three passes and FXAA's one over a window's rectangle: the
target's pixels copied first (the copy's barrier before the first pass
samples them), each pass ended by the barrier the next one's samples
need, SMAA's intermediates cleared whole as their passes begin, the target
drawn last, loaded, its alpha kept; each pass's viewport and scissor the
rectangle, its uniforms the target's size and FXAA's bounds the
rectangle's outermost texel centres */
static void post_test(void)
{
	static struct xgpu_vulkan_image target, color, edges, weights, area, search;
	static const int32_t corners[4] = { 8, 4, 40, 44 };
	static const int32_t beyond[4] = { -4, 4, 70, 44 }, empty[4] = { 70, 4, 80, 44 };
	struct xgpu_vulkan_image *images[6];
	const struct xgpu_vulkan_post_uniforms *uniforms;
	uint32_t index;

	post_reset();
	post_image(&target, 1, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&color, 2, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&edges, 3, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&weights, 4, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&area, 5, XGPU_VULKAN_FORMAT_RGBA8, 160, 560);
	post_image(&search, 6, XGPU_VULKAN_FORMAT_RGBA8, 64, 16);
	memset(&post, 0, sizeof(post));
	post.vertex = (VkShaderModule)(uintptr_t)1;
	for (index = 0; index < XGPU_VULKAN_POST_PROGRAM_COUNT; index++)
		post.pixel[index] = (VkShaderModule)(uintptr_t)(2 + index);
	post.sampler = (VkSampler)(uintptr_t)5;
	post.area = &area;
	post.search = &search;
	post.color = &color;
	post.edges = &edges;
	post.weights = &weights;
	post.width = 64;
	post.height = 48;
	post.format = XGPU_VULKAN_FORMAT_BGRA8;
	post_pipelines = 0;

	assert(xgpu_vulkan_post_anti_alias(1, &target, 0, corners));
	assert(!strcmp(post_events, "CXBcDEXBcDEXBlD"));
	assert(post_pipelines == 3 && post_masks[0] == 15 && post_masks[1] == 15 && post_masks[2] == 7);
	for (index = 0; index < 3; index++)
		assert(post_pipeline_samples[index] == VK_SAMPLE_COUNT_1_BIT);
	assert(post_copied.srcSubresource.baseArrayLayer == 0 && post_copied.dstSubresource.baseArrayLayer == 0);
	assert(post_copied.srcOffset.x == 8 && post_copied.srcOffset.y == 4 && post_copied.dstOffset.x == 8);
	assert(post_copied.dstOffset.y == 4 && post_copied.extent.width == 32 && post_copied.extent.height == 40);
	/* (the copy, and each pass, made visible to the samples after) */
	assert(post_barriers == 3 && post_barrier_sources[0] == VK_PIPELINE_STAGE_TRANSFER_BIT);
	for (index = 0; index < 3; index++)
	{
		assert(post_barrier_destinations[index] & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		assert(post_barrier_access[index] & VK_ACCESS_SHADER_READ_BIT);
	}
	assert(post_barrier_sources[1] & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	assert(post_barrier_sources[2] & VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	for (index = 0; index < 2; index++)
	{
		assert(post_began[index].clearValue.color.float32[0] == 0.0f);
		assert(post_began[index].clearValue.color.float32[3] == 0.0f);
		assert(post_began[index].storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	}
	assert(post_attachments[0] == edges.attachments[0] && post_attachments[1] == weights.attachments[0]);
	assert(post_attachments[2] == target.attachments[0]);
	/* edges from the copy; weights from the edges, the area and search
	textures; the blend from the copy and the weights */
	assert(post_updates == 3);
	assert(post_sampled[0][0].imageView == color.sampled && post_sampled[0][0].sampler == post.sampler);
	assert(post_sampled[1][0].imageView == edges.sampled && post_sampled[1][1].imageView == area.sampled);
	assert(post_sampled[1][2].imageView == search.sampled);
	assert(post_sampled[2][0].imageView == color.sampled && post_sampled[2][1].imageView == weights.sampled);
	for (index = 0; index < 3; index++)
	{
		assert(post_viewports[index].x == 8.0f && post_viewports[index].y == 4.0f);
		assert(post_viewports[index].width == 32.0f && post_viewports[index].height == 40.0f);
		assert(post_scissors[index].offset.x == 8 && post_scissors[index].offset.y == 4);
		assert(post_scissors[index].extent.width == 32 && post_scissors[index].extent.height == 40);
		assert(post_offsets[index][0] == post_offsets[index][1] && post_offsets[index][1] % 256 == 0);
		uniforms = (const struct xgpu_vulkan_post_uniforms *)(post_ring + post_offsets[index][1]);
		assert(uniforms->metrics[0] == 1.0f / 64.0f && uniforms->metrics[1] == 1.0f / 48.0f);
		assert(uniforms->metrics[2] == 64.0f && uniforms->metrics[3] == 48.0f);
	}
	assert(vk.color == &target);

	/* FXAA in a layered target's second layer, beyond its corners */
	post_reset();
	free(target.attachments);
	target.attachments = NULL;
	target.attachment_count = 0;
	target.description.type = XGPU_VULKAN_IMAGE_2D_ARRAY;
	target.description.depth_or_layers = 2;
	assert(xgpu_vulkan_post_anti_alias(0, &target, 1, beyond));
	assert(!strcmp(post_events, "CXBlD"));
	assert(post_pipelines == 4 && post_masks[3] == 7);
	assert(post_copied.srcSubresource.baseArrayLayer == 1 && post_copied.dstSubresource.baseArrayLayer == 0);
	assert(post_copied.srcOffset.x == 0 && post_copied.extent.width == 64 && post_copied.extent.height == 40);
	assert(post_attachments[0] == target.attachments[4] && last_view.subresourceRange.baseArrayLayer == 1);
	assert(post_sampled[0][0].imageView == color.sampled);
	uniforms = (const struct xgpu_vulkan_post_uniforms *)(post_ring + post_offsets[0][1]);
	assert(uniforms->bounds[0] == 0.5f / 64.0f && uniforms->bounds[1] == 4.5f / 48.0f);
	assert(uniforms->bounds[2] == 63.5f / 64.0f && uniforms->bounds[3] == 43.5f / 48.0f);
	assert(post_viewports[0].x == 0.0f && post_viewports[0].width == 64.0f);

	/* nothing of an empty rectangle, nor of a multisampled target */
	post_reset();
	assert(xgpu_vulkan_post_anti_alias(0, &target, 0, empty) && !post_events[0]);
	target.description.samples = 4;
	assert(!xgpu_vulkan_post_anti_alias(0, &target, 0, corners) && !post_events[0]);

	images[0] = &target;
	images[1] = &color;
	images[2] = &edges;
	images[3] = &weights;
	images[4] = &area;
	images[5] = &search;
	for (index = 0; index < 6; index++)
	{
		free(images[index]->attachments);
		images[index]->attachments = NULL;
	}
	memset(&post, 0, sizeof(post));
	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	reset();
}

/* display.panini over a multisampled target: its resolved pixels copied,
and the pass drawn into its samples, by a pipeline of as many samples (drawn
into the resolved pixels alone, it would be lost to the frame's resolve of
the samples, which the HUD is drawn into after); over a target of one
sample, into the target; never from samples */
static void panini_post_test(void)
{
	static struct xgpu_vulkan_image target, color, samples;
	static const int32_t corners[4] = { 0, 0, 64, 48 };
	static const float projection[8] = { 0.5f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.75f, -0.75f };
	uint32_t index;

	post_reset();
	post_image(&target, 1, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&color, 2, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&samples, 3, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	samples.description.samples = 4;
	memset(&post, 0, sizeof(post));
	post.vertex = (VkShaderModule)(uintptr_t)1;
	for (index = 0; index < XGPU_VULKAN_POST_PROGRAM_COUNT; index++)
		post.pixel[index] = (VkShaderModule)(uintptr_t)(2 + index);
	post.sampler = (VkSampler)(uintptr_t)5;
	post.color = &color;
	post.width = 64;
	post.height = 48;
	post.format = XGPU_VULKAN_FORMAT_BGRA8;
	post_pipelines = 0;

	assert(xgpu_vulkan_post_panini(&target, &samples, 0, corners, projection));
	assert(!strcmp(post_events, "CXBlD"));
	assert(post_copied.extent.width == 64 && post_copied.extent.height == 48);
	assert(post_pipelines == 1 && post_pipeline_samples[0] == VK_SAMPLE_COUNT_4_BIT && post_masks[0] == 15);
	assert(post_attachments[0] == samples.attachments[0] && vk.color == &samples);
	assert(post_sampled[0][0].imageView == color.sampled);

	post_reset();
	assert(xgpu_vulkan_post_panini(&target, &target, 0, corners, projection));
	assert(!strcmp(post_events, "CXBlD"));
	assert(post_pipelines == 2 && post_pipeline_samples[1] == VK_SAMPLE_COUNT_1_BIT);
	assert(post_attachments[0] == target.attachments[0]);

	post_reset();
	assert(!xgpu_vulkan_post_panini(&samples, &samples, 0, corners, projection) && !post_events[0]);

	free(target.attachments);
	free(color.attachments);
	free(samples.attachments);
	target.attachments = color.attachments = samples.attachments = NULL;
	memset(&post, 0, sizeof(post));
	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	reset();
}

/* ---------- clears of some of the colour's channels */

/* what the clears record, in order: B a pass begun, A attachments cleared,
D a draw, Q a query begun, q ended */
static char masked_events[64];
static unsigned char masked_ring[65536];
static uint32_t masked_pipelines, masked_draws;
static struct
{
	VkColorComponentFlags mask;
	VkBool32 blend, depth_test, depth_write, stencil_test;
	VkFormat color_format, depth_format;
	VkSampleCountFlagBits samples;
	uint32_t view_mask, bindings, attributes;
	VkPrimitiveTopology topology;
	VkCullModeFlags cull;
} masked_made;
static VkViewport masked_viewports[4];
static VkRect2D masked_scissors[4];
static uint32_t masked_offsets[4];
static VkViewport masked_viewport_state;
static VkRect2D masked_scissor_state;
static uint32_t masked_offset_state;
static VkClearAttachment masked_cleared[2];
static uint32_t masked_cleared_count;

static void masked_event(char event)
{
	size_t length = strlen(masked_events);

	assert(length + 1 < sizeof(masked_events));
	masked_events[length] = event;
}

static void VKAPI_CALL masked_begin(VkCommandBuffer command, const VkRenderingInfo *info)
{
	fake_begin(command, info);
	masked_event('B');
}

static void VKAPI_CALL masked_clear(VkCommandBuffer command, uint32_t attachment_count,
	const VkClearAttachment *attachments, uint32_t rect_count, const VkClearRect *rects)
{
	fake_clear(command, attachment_count, attachments, rect_count, rects);
	memcpy(masked_cleared, attachments, attachment_count * sizeof(*attachments));
	masked_cleared_count = attachment_count;
	masked_event('A');
}

static VkResult VKAPI_CALL masked_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
	const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *pipeline)
{
	const VkPipelineRenderingCreateInfo *rendering = info->pNext;

	(void)device; (void)cache; (void)allocator;
	assert(count == 1 && info->pColorBlendState->attachmentCount == 1 && rendering->colorAttachmentCount == 1);
	masked_made.mask = info->pColorBlendState->pAttachments[0].colorWriteMask;
	masked_made.blend = info->pColorBlendState->pAttachments[0].blendEnable;
	masked_made.depth_test = info->pDepthStencilState->depthTestEnable;
	masked_made.depth_write = info->pDepthStencilState->depthWriteEnable;
	masked_made.stencil_test = info->pDepthStencilState->stencilTestEnable;
	masked_made.color_format = rendering->pColorAttachmentFormats[0];
	masked_made.depth_format = rendering->depthAttachmentFormat;
	masked_made.samples = info->pMultisampleState->rasterizationSamples;
	masked_made.view_mask = rendering->viewMask;
	masked_made.bindings = info->pVertexInputState->vertexBindingDescriptionCount;
	masked_made.attributes = info->pVertexInputState->vertexAttributeDescriptionCount;
	masked_made.topology = info->pInputAssemblyState->topology;
	masked_made.cull = info->pRasterizationState->cullMode;
	*pipeline = (VkPipeline)(uintptr_t)(300 + masked_pipelines++);
	return VK_SUCCESS;
}

static void VKAPI_CALL masked_viewport(VkCommandBuffer c, uint32_t first, uint32_t count, const VkViewport *v)
{
	(void)c;
	assert(first == 0 && count == 1 && masked_draws < 4);
	masked_viewport_state = v[0];
}

static void VKAPI_CALL masked_scissor(VkCommandBuffer c, uint32_t first, uint32_t count, const VkRect2D *r)
{
	(void)c;
	assert(first == 0 && count == 1 && masked_draws < 4);
	masked_scissor_state = r[0];
}

static void VKAPI_CALL masked_bind_sets(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
	uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic_count, const uint32_t *offsets)
{
	(void)c; (void)point; (void)layout; (void)sets;
	assert(first == 0 && count == 2 && dynamic_count == 2 && offsets[0] == offsets[1]);
	masked_offset_state = offsets[1];
}

static void VKAPI_CALL masked_draw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first,
	uint32_t instance)
{
	(void)c;
	assert(vertices == 3 && instances == 1 && first == 0 && instance == 0 && masked_draws < 4);
	masked_viewports[masked_draws] = masked_viewport_state;
	masked_scissors[masked_draws] = masked_scissor_state;
	masked_offsets[masked_draws] = masked_offset_state;
	masked_draws++;
	masked_event('D');
}

static void VKAPI_CALL masked_begin_query(VkCommandBuffer c, VkQueryPool pool, uint32_t query, VkQueryControlFlags flags)
{
	(void)c; (void)pool; (void)query; (void)flags;
	masked_event('Q');
}

static void VKAPI_CALL masked_end_query(VkCommandBuffer c, VkQueryPool pool, uint32_t query)
{
	(void)c; (void)pool; (void)query;
	masked_event('q');
}

static void masked_reset(void)
{
	reset();
	memset(masked_events, 0, sizeof(masked_events));
	memset(&masked_made, 0, sizeof(masked_made));
	masked_draws = masked_cleared_count = 0;
	vk.ring_mapped = masked_ring;
	vk.ring = (VkBuffer)(uintptr_t)9;
	vk.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	color_target.description.format = XGPU_VULKAN_FORMAT_BGRA8;
	depth_target.description.format = XGPU_VULKAN_FORMAT_D24S8;
	/* (the clears' shaders, made already) */
	vk.clear_vertex = (VkShaderModule)(uintptr_t)61;
	vk.clear_pixel = (VkShaderModule)(uintptr_t)62;
	recording.main = (VkCommandBuffer)(uintptr_t)1;
	recording.descriptor_pools = &post_pool;
	recording.descriptor_pool_count = 1;
	vkCmdBeginRendering = masked_begin;
	vkCmdClearAttachments = masked_clear;
	vkCreateGraphicsPipelines = masked_pipeline;
	vkCmdBindPipeline = post_bind_pipeline;
	vkCmdSetViewport = masked_viewport;
	vkCmdSetScissor = masked_scissor;
	vkCmdSetDepthBias = fake_bias;
	vkCmdSetBlendConstants = fake_blend;
	vkCmdSetStencilReference = fake_stencil;
	vkCmdSetPrimitiveTopology = fake_topology;
	vkCmdBindDescriptorSets = masked_bind_sets;
	vkCmdDraw = masked_draw;
	vkCmdBeginQuery = masked_begin_query;
	vkCmdEndQuery = masked_end_query;
}

/* A clear of some of the colour's channels (the game's are of alpha alone)
is a triangle over each rectangle, through a pipeline whose write mask is
those channels, testing and writing no depth or stencil, of the targets'
formats, samples and views; depth and stencil are cleared as before (a
whole attachment's as the pass begins). A clear of every channel draws
nothing. */
static void masked_clear_test(void)
{
	static const int32_t full[1][4] = { { 0, 0, 64, 48 } };
	static const int32_t two[2][4] = { { 4, 5, 10, 12 }, { -20, -20, 100, 100 } };
	static const int32_t outside[1][4] = { { 70, 0, 8, 8 } };
	static const float color[4] = { 0.25f, 0.5f, 0.75f, 0.125f };
	struct query_frame queries;
	const float *uniforms;
	uint64_t count;

	/* a fog screen's: alpha alone and depth, the whole target, before a
	pass: depth cleared as the pass begins, the colour loaded and drawn */
	masked_reset();
	masked_pipelines = 0;
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA | XGPU_VULKAN_CLEAR_DEPTH, color,
		1.0f, 0);
	assert(!strcmp(masked_events, "BD"));
	assert(began_color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(began_stencil.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_depth.clearValue.depthStencil.depth == CLEARED(1.0f));
	assert(vk.statistics.attachment_clears == 1 && vk.statistics.clears_as_draws == 1);
	assert(masked_pipelines == 1 && masked_made.mask == VK_COLOR_COMPONENT_A_BIT && !masked_made.blend);
	assert(!masked_made.depth_test && !masked_made.depth_write && !masked_made.stencil_test);
	assert(masked_made.color_format == VK_FORMAT_B8G8R8A8_UNORM);
	assert(masked_made.depth_format == VK_FORMAT_D24_UNORM_S8_UINT);
	assert(masked_made.samples == VK_SAMPLE_COUNT_1_BIT && !masked_made.view_mask);
	assert(!masked_made.bindings && !masked_made.attributes && masked_made.cull == VK_CULL_MODE_NONE);
	assert(masked_made.topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	assert(masked_viewports[0].x == 0.0f && masked_viewports[0].y == 0.0f);
	assert(masked_viewports[0].width == 64.0f && masked_viewports[0].height == 48.0f);
	assert(masked_viewports[0].minDepth == 0.0f && masked_viewports[0].maxDepth == 1.0f);
	assert(masked_scissors[0].extent.width == 64 && masked_scissors[0].extent.height == 48);
	assert(masked_offsets[0] % 256 == 0);
	uniforms = (const float *)(masked_ring + masked_offsets[0]);
	assert(uniforms[0] == 0.25f && uniforms[1] == 0.5f && uniforms[2] == 0.75f && uniforms[3] == 0.125f);
	/* (the same targets again: the same pipeline) */
	count = xgpu_vulkan_pipeline_count();
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BDD") && xgpu_vulkan_pipeline_count() == count && masked_pipelines == 1);

	/* red, green and blue and the stencil, in a pass, over two rectangles
	(one beyond the target): the stencil cleared, the colour drawn over
	each, inside the target */
	masked_reset();
	xgpu_vulkan_clear(two, 1, XGPU_VULKAN_CLEAR_DEPTH, color, 0.5f, 0);
	xgpu_vulkan_clear(two, 2, XGPU_VULKAN_CLEAR_COLOR | 7 | XGPU_VULKAN_CLEAR_STENCIL, color, 0.5f, 9);
	assert(!strcmp(masked_events, "BAADD"));
	assert(masked_cleared_count == 1 && masked_cleared[0].aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT);
	assert(masked_cleared[0].clearValue.depthStencil.stencil == 9);
	assert(masked_made.mask == (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT));
	assert(masked_viewports[0].x == 4.0f && masked_viewports[0].y == 5.0f);
	assert(masked_viewports[0].width == 10.0f && masked_viewports[0].height == 12.0f);
	assert(masked_scissors[0].offset.x == 4 && masked_scissors[0].offset.y == 5);
	assert(masked_scissors[0].extent.width == 10 && masked_scissors[0].extent.height == 12);
	assert(masked_viewports[1].x == 0.0f && masked_viewports[1].y == 0.0f);
	assert(masked_viewports[1].width == 64.0f && masked_viewports[1].height == 48.0f);
	assert(masked_scissors[1].extent.width == 64 && masked_scissors[1].extent.height == 48);

	/* every channel: cleared, never drawn (the whole target as the pass
	begins, a rectangle in it with vkCmdClearAttachments) */
	masked_reset();
	count = xgpu_vulkan_pipeline_count();
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS, color, 1.0f, 0);
	xgpu_vulkan_clear(two, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BA") && began_color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(masked_cleared_count == 1 && masked_cleared[0].aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(xgpu_vulkan_pipeline_count() == count && !vk.statistics.clears_as_draws && !masked_draws);
	/* no channel, or a rectangle outside the target: nothing drawn */
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR, color, 1.0f, 0);
	xgpu_vulkan_clear(outside, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BA") && !masked_draws);

	/* a layered target's two views (multiview), and a multisampled one */
	masked_reset();
	xgpu_vulkan_set_target_views(&color_target, &depth_target, 2, 2);
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BD") && began.viewMask == 3 && masked_made.view_mask == 3);
	assert(last_view.subresourceRange.baseArrayLayer == 2 && last_view.subresourceRange.layerCount == 2);
	masked_reset();
	color_target.description.samples = depth_target.description.samples = 4;
	vk.depth = NULL;
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BD") && masked_made.samples == VK_SAMPLE_COUNT_4_BIT);
	assert(masked_made.depth_format == VK_FORMAT_UNDEFINED && !masked_made.view_mask);

	/* in a visibility test: its query ended over the triangle, which counts
	no samples, and begun again after it */
	masked_reset();
	memset(&queries, 0, sizeof(queries));
	vk.queries = &queries;
	xgpu_vulkan_visibility_begin();
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_ALPHA, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BQqDQ") && vk.test_query_open && vk.test_query_count == 2);
	vk.queries = NULL;

	/* queued as a map is prepared: the targets' pipeline, the one the clear
	draws with after (none for every channel) */
	masked_reset();
	masked_pipelines = 0;
	count = xgpu_vulkan_pipeline_count();
	assert(xgpu_vulkan_clear_prepare(XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS));
	assert(xgpu_vulkan_pipeline_count() == count);
	assert(xgpu_vulkan_clear_prepare(XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_RED | XGPU_VULKAN_CLEAR_BLUE));
	assert(xgpu_vulkan_pipeline_count() == count + 1 && masked_pipelines == 1);
	assert(masked_made.mask == (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_B_BIT));
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_RED | XGPU_VULKAN_CLEAR_BLUE, color, 1.0f, 0);
	assert(!strcmp(masked_events, "BD") && xgpu_vulkan_pipeline_count() == count + 1 && masked_pipelines == 1);

	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	vkCmdEndQuery = fake_end_query;
	reset();
}

/* ---------- the window: its surface lost and made again, the app in the
background, the present mode of the swap interval, the frames turned for
the display, and the pipeline cache written when there is something new */

static VkSurfaceCapabilitiesKHR window_capabilities;
static VkResult window_capabilities_result, window_acquire_result, window_present_result;
static VkPresentModeKHR window_modes[4];
static uint32_t window_mode_count;
static VkSwapchainCreateInfoKHR window_made;
static unsigned int window_swapchains, window_surfaces_made, window_surfaces_destroyed, window_surface_calls;
static unsigned int window_presents, window_capability_queries, window_turns, window_rotations;
static int window_gone;
static VkImage window_blit_source;
static VkImageBlit window_blit_region;
static struct xgpu_vulkan_image window_frame, window_turned;

static VkResult VKAPI_CALL window_get_capabilities(VkPhysicalDevice physical, VkSurfaceKHR surface,
	VkSurfaceCapabilitiesKHR *capabilities)
{
	(void)physical;
	assert(surface);
	window_capability_queries++;
	*capabilities = window_capabilities;
	return window_capabilities_result;
}

static VkResult VKAPI_CALL window_get_formats(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count,
	VkSurfaceFormatKHR *formats)
{
	(void)physical; (void)surface;
	assert(*count >= 1);
	*count = 1;
	formats[0].format = VK_FORMAT_B8G8R8A8_UNORM;
	formats[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL window_get_modes(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count,
	VkPresentModeKHR *modes)
{
	(void)physical; (void)surface;
	assert(*count >= window_mode_count);
	*count = window_mode_count;
	memcpy(modes, window_modes, window_mode_count * sizeof(*modes));
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL window_get_support(VkPhysicalDevice physical, uint32_t family, VkSurfaceKHR surface,
	VkBool32 *supported)
{
	(void)physical; (void)family;
	assert(surface);
	*supported = VK_TRUE;
	return VK_SUCCESS;
}

static VkSurfaceKHR window_surface_create(VkInstance instance, void *context)
{
	(void)instance;
	assert(context == &window_gone);
	window_surface_calls++;
	if (window_gone)
		return VK_NULL_HANDLE;
	return (VkSurfaceKHR)(uintptr_t)(300 + ++window_surfaces_made);
}

static void VKAPI_CALL window_destroy_surface(VkInstance instance, VkSurfaceKHR surface,
	const VkAllocationCallbacks *allocator)
{
	(void)instance; (void)allocator;
	assert(surface);
	window_surfaces_destroyed++;
}

static VkResult VKAPI_CALL window_create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR *info,
	const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain)
{
	(void)device; (void)allocator;
	assert(info->surface == vk.surface);
	window_made = *info;
	*swapchain = (VkSwapchainKHR)(uintptr_t)(400 + ++window_swapchains);
	return VK_SUCCESS;
}

static void VKAPI_CALL window_destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain,
	const VkAllocationCallbacks *allocator)
{
	(void)device; (void)swapchain; (void)allocator;
}

static VkResult VKAPI_CALL window_get_images(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count, VkImage *images)
{
	(void)device; (void)swapchain;
	assert(*count >= 2);
	*count = 2;
	images[0] = (VkImage)(uintptr_t)501;
	images[1] = (VkImage)(uintptr_t)502;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL window_create_semaphore(VkDevice device, const VkSemaphoreCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkSemaphore *semaphore)
{
	(void)device; (void)info; (void)allocator;
	*semaphore = (VkSemaphore)(uintptr_t)600;
	return VK_SUCCESS;
}

static void VKAPI_CALL window_destroy_semaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks *allocator)
{
	(void)device; (void)semaphore; (void)allocator;
}

static VkResult VKAPI_CALL window_wait_idle(VkDevice device)
{
	(void)device;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL window_acquire(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
	VkSemaphore semaphore, VkFence fence, uint32_t *index)
{
	(void)device; (void)timeout; (void)semaphore; (void)fence;
	assert(swapchain == vk.swapchain);
	*index = 1;
	return window_acquire_result;
}

static VkResult VKAPI_CALL window_present(VkQueue queue, const VkPresentInfoKHR *info)
{
	(void)queue;
	assert(info->swapchainCount == 1 && info->pImageIndices[0] == 1);
	window_presents++;
	return window_present_result;
}

static VkResult VKAPI_CALL window_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence)
{
	(void)queue; (void)count; (void)info; (void)fence;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL window_end_commands(VkCommandBuffer commands)
{
	(void)commands;
	return VK_SUCCESS;
}

static void VKAPI_CALL window_clear_image(VkCommandBuffer commands, VkImage image, VkImageLayout layout,
	const VkClearColorValue *color, uint32_t count, const VkImageSubresourceRange *ranges)
{
	(void)commands; (void)image; (void)layout; (void)color; (void)count; (void)ranges;
}

static void VKAPI_CALL window_blit(VkCommandBuffer commands, VkImage source, VkImageLayout source_layout,
	VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageBlit *regions,
	VkFilter filter)
{
	(void)commands; (void)source_layout; (void)destination; (void)destination_layout; (void)count; (void)filter;
	window_blit_source = source;
	window_blit_region = regions[0];
}

static struct xgpu_vulkan_image *window_rotate(struct xgpu_vulkan_image *image, int quarter_turns)
{
	assert(image == &window_frame);
	window_rotations++;
	window_turns = (unsigned int)quarter_turns;
	return &window_turned;
}

/* a present of the frame: its serial (0: nothing presented) */
static uint64_t window_present_frame(void)
{
	window_blit_source = VK_NULL_HANDLE;
	return present(&window_frame, 1);
}

static void window_reset(void)
{
	reset();
	vk.device = (VkDevice)(uintptr_t)1;
	vk.instance = (VkInstance)(uintptr_t)2;
	vk.physical = (VkPhysicalDevice)(uintptr_t)3;
	vk.surface_create = window_surface_create;
	vk.surface_context = &window_gone;
	vk.interval = 1;
	vk.color = vk.depth = NULL;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR = window_get_capabilities;
	vkGetPhysicalDeviceSurfaceFormatsKHR = window_get_formats;
	vkGetPhysicalDeviceSurfacePresentModesKHR = window_get_modes;
	vkGetPhysicalDeviceSurfaceSupportKHR = window_get_support;
	vkDestroySurfaceKHR = window_destroy_surface;
	vkCreateSwapchainKHR = window_create_swapchain;
	vkDestroySwapchainKHR = window_destroy_swapchain;
	vkGetSwapchainImagesKHR = window_get_images;
	vkCreateSemaphore = window_create_semaphore;
	vkDestroySemaphore = window_destroy_semaphore;
	vkDeviceWaitIdle = window_wait_idle;
	vkAcquireNextImageKHR = window_acquire;
	vkQueuePresentKHR = window_present;
	vkQueueSubmit = window_submit;
	vkEndCommandBuffer = window_end_commands;
	vkResetCommandPool = fake_reset_pool;
	vkBeginCommandBuffer = fake_begin_commands;
	vkCmdClearColorImage = window_clear_image;
	vkCmdBlitImage = window_blit;
	memset(&window_capabilities, 0, sizeof(window_capabilities));
	window_capabilities.minImageCount = 2;
	window_capabilities.currentExtent.width = 1280;
	window_capabilities.currentExtent.height = 720;
	window_capabilities.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	window_capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	window_capabilities.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	window_capabilities_result = window_acquire_result = window_present_result = VK_SUCCESS;
	window_modes[0] = VK_PRESENT_MODE_FIFO_KHR;
	window_modes[1] = VK_PRESENT_MODE_IMMEDIATE_KHR;
	window_modes[2] = VK_PRESENT_MODE_MAILBOX_KHR;
	window_mode_count = 3;
	window_swapchains = window_surfaces_made = window_surfaces_destroyed = window_surface_calls = 0;
	window_presents = window_capability_queries = window_turns = window_rotations = 0;
	window_gone = 0;
	memset(&window_frame, 0, sizeof(window_frame));
	window_frame.image = (VkImage)(uintptr_t)700;
	window_frame.description.width = 640;
	window_frame.description.height = 480;
	memset(&window_turned, 0, sizeof(window_turned));
	window_turned.image = (VkImage)(uintptr_t)701;
	window_turned.description.width = 480;
	window_turned.description.height = 640;
}

static void window_test(void)
{
	static const VkPresentModeKHR fifo_only[1] = { VK_PRESENT_MODE_FIFO_KHR };
	static const VkPresentModeKHR no_mailbox[2] = { VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR };
	static const VkPresentModeKHR all[3] = { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR,
		VK_PRESENT_MODE_FIFO_KHR };
	unsigned int index, queries, swapchains;

	/* the swap interval's present mode: FIFO waits; else MAILBOX, else
	IMMEDIATE, else FIFO */
	assert(present_mode_choose(all, 3, 1) == VK_PRESENT_MODE_FIFO_KHR);
	assert(present_mode_choose(all, 3, 0) == VK_PRESENT_MODE_MAILBOX_KHR);
	assert(present_mode_choose(no_mailbox, 2, 0) == VK_PRESENT_MODE_IMMEDIATE_KHR);
	assert(present_mode_choose(fifo_only, 1, 0) == VK_PRESENT_MODE_FIFO_KHR);
	/* debug.vulkan_present_mode's, where the surface has it, over the
	interval's; else the interval's */
	vk.present_mode_asked = VK_PRESENT_MODE_IMMEDIATE_KHR + 1;
	assert(present_mode_choose(all, 3, 1) == VK_PRESENT_MODE_IMMEDIATE_KHR);
	vk.present_mode_asked = VK_PRESENT_MODE_FIFO_KHR + 1;
	assert(present_mode_choose(all, 3, 0) == VK_PRESENT_MODE_FIFO_KHR);
	vk.present_mode_asked = VK_PRESENT_MODE_MAILBOX_KHR + 1;
	assert(present_mode_choose(no_mailbox, 2, 0) == VK_PRESENT_MODE_IMMEDIATE_KHR);
	assert(present_mode_choose(no_mailbox, 2, 1) == VK_PRESENT_MODE_FIFO_KHR);
	vk.present_mode_asked = 0;

	/* the first present makes the surface and the swapchain (vsync: FIFO),
	and blits the frame itself */
	window_reset();
	assert(window_present_frame() && window_surfaces_made == 1 && window_swapchains == 1 && window_presents == 1);
	assert(window_made.presentMode == VK_PRESENT_MODE_FIFO_KHR && window_blit_source == window_frame.image);
	assert(window_made.preTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
	assert(window_made.imageExtent.width == 1280 && window_made.imageExtent.height == 720);
	assert(window_present_frame() && window_swapchains == 1);

	/* display.vsync off: the swapchain made again, MAILBOX; on: FIFO */
	xgpu_vulkan_window_interval(0);
	assert(window_present_frame() && window_swapchains == 2 && window_made.presentMode == VK_PRESENT_MODE_MAILBOX_KHR);
	assert(window_made.oldSwapchain == (VkSwapchainKHR)(uintptr_t)401);
	assert(window_present_frame() && window_swapchains == 2);
	xgpu_vulkan_window_interval(1);
	assert(window_present_frame() && window_swapchains == 3 && window_made.presentMode == VK_PRESENT_MODE_FIFO_KHR);

	/* a resize (from another thread) makes it again, once */
	xgpu_vulkan_window_resized(800, 600);
	assert(window_present_frame() && window_swapchains == 4);
	assert(window_present_frame() && window_swapchains == 4);

	/* VK_ERROR_SURFACE_LOST_KHR from the acquire: nothing presented, the
	surface let go of; the next frame makes a new one and presents */
	window_acquire_result = VK_ERROR_SURFACE_LOST_KHR;
	index = window_presents;
	assert(!window_present_frame() && window_presents == index);
	assert(!vk.surface && !vk.swapchain && window_surfaces_destroyed == 1);
	window_acquire_result = VK_SUCCESS;
	assert(window_present_frame() && window_surfaces_made == 2 && window_swapchains == 5);
	assert(window_made.surface == (VkSurfaceKHR)(uintptr_t)302 && !window_made.oldSwapchain);

	/* ... from the present */
	window_present_result = VK_ERROR_SURFACE_LOST_KHR;
	assert(window_present_frame() && !vk.surface && window_surfaces_destroyed == 2);
	window_present_result = VK_SUCCESS;
	assert(window_present_frame() && window_surfaces_made == 3 && vk.surface);

	/* ... from the surface's capabilities, as the swapchain is made */
	xgpu_vulkan_window_resized(800, 600);
	window_capabilities_result = VK_ERROR_SURFACE_LOST_KHR;
	assert(!window_present_frame() && !vk.surface && window_surfaces_destroyed == 3);
	window_capabilities_result = VK_SUCCESS;
	assert(window_present_frame() && window_surfaces_made == 4);

	/* the app in the background: the surface let go of and not made
	again, the frames not presented (nor waited for) */
	xgpu_vulkan_window_suspend(1);
	assert(!vk.surface && !vk.swapchain && window_surfaces_destroyed == 4 && vk.window_suspended);
	window_gone = 1;
	index = window_surface_calls;
	assert(!window_present_frame() && !window_present_frame() && window_surface_calls == index);
	/* back, before Android gives the window back: tried at each frame */
	xgpu_vulkan_window_suspend(0);
	assert(!vk.window_suspended && !vk.surface && window_surface_calls == index + 1);
	assert(!window_present_frame() && window_surface_calls == index + 2);
	window_gone = 0;
	assert(window_present_frame() && vk.surface && window_surfaces_made == 5 && window_presents);

	/* a hidden window (VR's, while the headset shows the game): nothing
	presented, nothing waited for; shown, presented again */
	xgpu_vulkan_window_hidden(1);
	index = window_presents;
	assert(!window_present_frame() && window_presents == index && vk.surface);
	xgpu_vulkan_window_hidden(0);
	assert(window_present_frame() && window_presents == index + 1);

	/* held sideways (a quarter turn), with a way to turn the frames: the
	swapchain is the display's way up (the extent swapped, preTransform
	the surface's), and the frame is drawn turned into it */
	window_reset();
	vk.rotate = window_rotate;
	window_capabilities.currentExtent.width = 2400;
	window_capabilities.currentExtent.height = 1080;
	window_capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
	window_capabilities.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR |
		VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR |
		VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
	assert(window_present_frame() && window_made.preTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR);
	assert(window_made.imageExtent.width == 1080 && window_made.imageExtent.height == 2400);
	assert(window_rotations == 1 && window_turns == 1 && window_blit_source == window_turned.image);
	/* turned the other way: a SUBOPTIMAL present finds the transform
	changed, and the swapchain is made again for it */
	window_capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
	window_present_result = VK_SUBOPTIMAL_KHR;
	assert(window_present_frame() && vk.swapchain_stale);
	window_present_result = VK_SUCCESS;
	assert(window_present_frame() && window_made.preTransform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
	assert(window_turns == 3 && window_made.imageExtent.width == 1080);
	/* upside down: no swap */
	window_capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR;
	window_capabilities.currentExtent.width = 1080;
	window_capabilities.currentExtent.height = 2400;
	xgpu_vulkan_window_resized(1080, 2400);
	assert(window_present_frame() && window_turns == 2 && window_made.imageExtent.width == 1080 &&
		window_made.imageExtent.height == 2400);

	/* a SUBOPTIMAL that making the swapchain again cannot mend (the
	surface unchanged) neither makes it again nor asks the surface at
	each frame */
	queries = window_capability_queries;
	index = window_swapchains;
	window_present_result = VK_SUBOPTIMAL_KHR;
	for (index = 0; index < 120; index++)
		assert(window_present_frame() && !vk.swapchain_stale);
	assert(window_capability_queries - queries == 2);

	/* a resize told before the surface has the new size (split screen on
	a folding phone): the surface asked again at every RESIZE_CHECK_EVERY
	presents, not at each, until it has it; then the swapchain made again
	for it, and the surface not asked again */
	window_present_result = VK_SUCCESS;
	swapchains = window_swapchains;
	xgpu_vulkan_window_resized(1080, 1200);
	assert(window_present_frame() && window_swapchains == swapchains + 1 && window_made.imageExtent.height == 2400);
	queries = window_capability_queries;
	for (index = 0; index < 8; index++)
		assert(window_present_frame());
	assert(window_capability_queries - queries == 2 && window_swapchains == swapchains + 1);
	window_capabilities.currentExtent.height = 1200;
	for (index = 0; index < 4; index++)
		assert(window_present_frame());
	assert(window_swapchains == swapchains + 2 && window_made.imageExtent.height == 1200);
	queries = window_capability_queries;
	for (index = 0; index < 120; index++)
		assert(window_present_frame());
	assert(window_capability_queries == queries && window_swapchains == swapchains + 2);

	/* without a way to turn the frames: the window's way up, which the
	compositor turns */
	window_reset();
	window_capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
	window_capabilities.supportedTransforms |= VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
	window_capabilities.currentExtent.width = 2400;
	window_capabilities.currentExtent.height = 1080;
	assert(window_present_frame() && window_made.preTransform == VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR);
	assert(window_made.imageExtent.width == 2400 && !window_rotations && window_blit_source == window_frame.image);

	/* display.picture_position: the 4:3 frame in a taller window (a
	folding phone's inner screen) centered, at the top with all the black
	below it, or at the bottom (d3d8_device.c sends the window's height,
	brought up to the lowest place) with all the black above it */
	window_reset();
	window_capabilities.currentExtent.width = 1000;
	window_capabilities.currentExtent.height = 1200;
	assert(window_present_frame() && window_blit_region.dstOffsets[0].x == 0 &&
		window_blit_region.dstOffsets[0].y == 225 && window_blit_region.dstOffsets[1].y == 975);
	xgpu_vulkan_picture_top(0);
	assert(window_present_frame() && window_blit_region.dstOffsets[0].x == 0 &&
		window_blit_region.dstOffsets[0].y == 0 && window_blit_region.dstOffsets[1].y == 750);
	xgpu_vulkan_picture_top(1200);
	assert(window_present_frame() && window_blit_region.dstOffsets[0].x == 0 &&
		window_blit_region.dstOffsets[0].y == 450 && window_blit_region.dstOffsets[1].y == 1200);
	/* ... turned with the frame, held sideways: the top of the window is
	the swapchain's right edge */
	window_reset();
	vk.rotate = window_rotate;
	window_capabilities.currentExtent.width = 1200;
	window_capabilities.currentExtent.height = 1000;
	window_capabilities.currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
	window_capabilities.supportedTransforms |= VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
	assert(window_present_frame() && window_blit_source == window_turned.image);
	assert(window_blit_region.dstOffsets[0].x == 50 && window_blit_region.dstOffsets[1].x == 950);
	xgpu_vulkan_picture_top(0);
	assert(window_present_frame() && window_blit_region.dstOffsets[0].x == 100 &&
		window_blit_region.dstOffsets[1].x == 1000 && window_blit_region.dstOffsets[0].y == 0 &&
		window_blit_region.dstOffsets[1].y == 1200);
	/* (the bottom of the window: the swapchain's left edge) */
	xgpu_vulkan_picture_top(1200);
	assert(window_present_frame() && window_blit_region.dstOffsets[0].x == 0 &&
		window_blit_region.dstOffsets[1].x == 900 && window_blit_region.dstOffsets[0].y == 0 &&
		window_blit_region.dstOffsets[1].y == 1200);
	reset();
}

/* the present step's turn (xgpu_vulkan_post_rotate): one triangle over
the whole turned image (the frame's width and height swapped for a
quarter turn), sampling the frame, its uniforms the turned image's size
and the turn's matrix */
static void rotate_test(void)
{
	static struct xgpu_vulkan_image frame, turned;
	const struct xgpu_vulkan_post_uniforms *uniforms;

	post_reset();
	post_image(&frame, 1, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	post_image(&turned, 2, XGPU_VULKAN_FORMAT_BGRA8, 48, 64);
	memset(&post, 0, sizeof(post));
	post.sampler = (VkSampler)(uintptr_t)5;
	assert(!xgpu_vulkan_post_rotate(&frame, 1));
	post.rotate = (VkPipeline)(uintptr_t)210;
	post.turned = &turned;
	assert(!xgpu_vulkan_post_rotate(&frame, 0) && !xgpu_vulkan_post_rotate(&frame, 4));
	assert(xgpu_vulkan_post_rotate(&frame, 1) == &turned);
	assert(!strcmp(post_events, "BlD") && post_attachments[0] == turned.attachments[0]);
	assert(post_sampled[0][0].imageView == frame.sampled && post_sampled[0][0].sampler == post.sampler);
	assert(post_viewports[0].x == 0.0f && post_viewports[0].width == 48.0f && post_viewports[0].height == 64.0f);
	assert(post_scissors[0].extent.width == 48 && post_scissors[0].extent.height == 64);
	uniforms = (const struct xgpu_vulkan_post_uniforms *)(post_ring + post_offsets[0][1]);
	assert(uniforms->metrics[0] == 1.0f / 48.0f && uniforms->metrics[1] == 1.0f / 64.0f);
	/* (a quarter turn clockwise: the turned image's x is the frame's y,
	its y the frame's -x, about the centre) */
	assert(uniforms->bounds[0] == 0.0f && uniforms->bounds[1] == 1.0f);
	assert(uniforms->bounds[2] == -1.0f && uniforms->bounds[3] == 0.0f);
	post_reset();
	assert(xgpu_vulkan_post_rotate(&frame, 3) == &turned);
	uniforms = (const struct xgpu_vulkan_post_uniforms *)(post_ring + post_offsets[0][1]);
	assert(uniforms->bounds[1] == -1.0f && uniforms->bounds[2] == 1.0f);
	free(frame.attachments);
	free(turned.attachments);
	memset(&post, 0, sizeof(post));
	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	reset();
}

static unsigned int cache_reads;

static VkResult VKAPI_CALL cache_data(VkDevice device, VkPipelineCache cache, size_t *size, void *data)
{
	(void)device;
	assert(cache);
	cache_reads++;
	if (data)
		memcpy(data, "cache", 5);
	*size = 5;
	return VK_SUCCESS;
}

static void pipeline_cache_test(void)
{
	char path[] = "/tmp/openh1_passes_cacheXXXXXX", temporary[64];
	FILE *file;
	char read[8] = { 0 };
	int descriptor = mkstemp(path);

	assert(descriptor >= 0);
	close(descriptor);
	remove(path);
	reset();
	vk.pipeline_cache = (VkPipelineCache)(uintptr_t)5;
	snprintf(vk.cache_path, sizeof(vk.cache_path), "%s", path);
	vkGetPipelineCacheData = cache_data;
	/* nothing made since the cache was read: not written */
	xgpu_vulkan_pipeline_cache_save();
	assert(!cache_reads && access(path, F_OK));
	/* a pipeline made: written whole (through a temporary file, gone) */
	pipeline_publish(&(struct pipeline_entry){ 0 }, (VkPipeline)(uintptr_t)9, VK_SUCCESS, 0.0);
	xgpu_vulkan_pipeline_cache_save();
	assert(cache_reads == 2);
	file = fopen(path, "rb");
	assert(file && fread(read, 1, sizeof(read), file) == 5 && !memcmp(read, "cache", 5));
	fclose(file);
	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	assert(access(temporary, F_OK));
	/* and not again until another is made */
	xgpu_vulkan_pipeline_cache_save();
	assert(cache_reads == 2);
	remove(path);
	reset();
}

static unsigned int modules_made;

static VkResult VKAPI_CALL fake_shader_module(VkDevice device, const VkShaderModuleCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkShaderModule *module)
{
	(void)device; (void)allocator;
	assert(info->codeSize % 4 == 0 && info->pCode);
	*module = (VkShaderModule)(uintptr_t)(500 + modules_made++);
	return VK_SUCCESS;
}

/* A module is the same for the same words, made once (by its words'
hash, eight bytes at a time); the fixed programs' (the VR composition's,
asked for at each of its draws) are found by their address, not read
again. */
static void shaders_test(void)
{
	static uint32_t fixed[5] = { 0x07230203, 1, 2, 3, 4 };
	uint32_t copy[5], other[3] = { 0x07230203, 9, 9 };
	VkShaderModule module;

	reset();
	modules_made = 0;
	vkCreateShaderModule = fake_shader_module;
	memcpy(copy, fixed, sizeof(copy));
	module = xgpu_vulkan_shader_constant(fixed, 5);
	assert(module && modules_made == 1);
	assert(xgpu_vulkan_shader_constant(fixed, 5) == module && vk.constant_shader_count == 1);
	/* (the same words elsewhere: the same module, by their hash) */
	assert(xgpu_vulkan_shader(copy, 5) == module && modules_made == 1);
	assert(xgpu_vulkan_shader(other, 3) != module && modules_made == 2);
	/* (found by the address: the words are not hashed or compared again) */
	fixed[4] = 5;
	assert(xgpu_vulkan_shader_constant(fixed, 5) == module && modules_made == 2);
	/* (another count at the address is another module) */
	assert(xgpu_vulkan_shader_constant(fixed, 4) != module && modules_made == 3);
	assert(bytes_hash64(copy, sizeof(copy)) != bytes_hash64(fixed, sizeof(fixed)));
	assert(bytes_hash64(copy, 19) != bytes_hash64(copy, 20));
	reset();
}

/* ---------- display.hdr (docs/hdr.md)

What the core and the passes record for display.hdr's half of the contract:
a draw that blends in its shader reads its colour attachment as set 2's
input attachment, after a framebuffer-local barrier (by region, attachment
writes before input reads) unless the device orders the reads itself, the
set written once for the attachment and the submission; its pipeline's
blending off, in rasterization order where the device has it; the swapchain
HDR (HDR10's PQ, or scRGB's half floats) only while the frames are HDR and
the display shows HDR and the surface has the colour space, the frame
encoded for it, the display told the front end as it changes; the tone map
a draw in the view's pass reading its pixels. */

static char hdr_events[64];
static VkDependencyFlags hdr_dependency;
static VkPipelineStageFlags hdr_stages[2];
static VkMemoryBarrier hdr_memory;
static VkDescriptorSet hdr_input;
static VkWriteDescriptorSet hdr_write;
static VkDescriptorImageInfo hdr_write_image;
static VkPipelineColorBlendStateCreateInfo hdr_blend;
static VkPipelineColorBlendAttachmentState hdr_blend_attachment;
static VkFormat hdr_color_format, hdr_depth_format;
static VkSampleCountFlagBits hdr_samples;
static int hdr_told[8][2];
static float hdr_told_headroom[8];
static unsigned int hdr_tells, hdr_encodes;
static uint8_t hdr_encoded_format;
static struct xgpu_hdr_frame hdr_encoded_frame;
static struct xgpu_vulkan_image hdr_encoded;
static int hdr_encode_fails;

static void hdr_event(char event)
{
	size_t length = strlen(hdr_events);

	assert(length + 1 < sizeof(hdr_events));
	hdr_events[length] = event;
}

static void VKAPI_CALL hdr_barrier(VkCommandBuffer command, VkPipelineStageFlags source, VkPipelineStageFlags destination,
	VkDependencyFlags dependency, uint32_t memory_count, const VkMemoryBarrier *memory, uint32_t buffer_count,
	const VkBufferMemoryBarrier *buffers, uint32_t image_count, const VkImageMemoryBarrier *images)
{
	(void)command; (void)buffers; (void)images;
	/* (local reads' dependency, inside a pass: a memory barrier alone, by
	region; the others, a pass's end's and the copies', not counted) */
	if (dependency & VK_DEPENDENCY_BY_REGION_BIT)
	{
		assert(vk.pass_active);
		assert(memory_count == 1 && !buffer_count && !image_count);
		hdr_dependency = dependency;
		hdr_stages[0] = source;
		hdr_stages[1] = destination;
		hdr_memory = memory[0];
		hdr_event('R');
	}
}

static void VKAPI_CALL hdr_begin(VkCommandBuffer command, const VkRenderingInfo *info)
{
	(void)command; (void)info;
	hdr_event('B');
}

static void VKAPI_CALL hdr_end(VkCommandBuffer command)
{
	(void)command;
	hdr_event('E');
}

static void VKAPI_CALL hdr_update(VkDevice device, uint32_t count, const VkWriteDescriptorSet *writes,
	uint32_t copies, const VkCopyDescriptorSet *copy)
{
	(void)device; (void)copies; (void)copy;
	if (writes[0].descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
	{
		assert(count == 1);
		hdr_write = writes[0];
		hdr_write_image = writes[0].pImageInfo[0];
		hdr_event('U');
		return;
	}
	assert(count == 4);
	hdr_event('T');
}

static void VKAPI_CALL hdr_bind_sets(VkCommandBuffer c, VkPipelineBindPoint point, VkPipelineLayout layout,
	uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t dynamic_count, const uint32_t *offsets)
{
	(void)c; (void)point; (void)layout; (void)offsets;
	if (first == 2)
	{
		assert(count == 1 && !dynamic_count);
		hdr_input = sets[0];
		hdr_event('I');
		return;
	}
	assert(first == 0 && count == 2);
	hdr_event('S');
}

static void VKAPI_CALL hdr_draw(VkCommandBuffer c, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t instance)
{
	(void)c; (void)vertices; (void)instances; (void)first; (void)instance;
	hdr_event('D');
}

static VkResult VKAPI_CALL hdr_pipeline(VkDevice device, VkPipelineCache cache, uint32_t count,
	const VkGraphicsPipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *pipeline)
{
	const VkPipelineRenderingCreateInfo *rendering = info->pNext;

	(void)device; (void)cache; (void)allocator;
	assert(count == 1);
	hdr_blend = *info->pColorBlendState;
	hdr_blend_attachment = info->pColorBlendState->pAttachments[0];
	hdr_color_format = rendering->pColorAttachmentFormats[0];
	hdr_depth_format = rendering->depthAttachmentFormat;
	hdr_samples = info->pMultisampleState->rasterizationSamples;
	*pipeline = (VkPipeline)(uintptr_t)90;
	return VK_SUCCESS;
}

static void hdr_reset(void)
{
	reset();
	memset(hdr_events, 0, sizeof(hdr_events));
	hdr_input = VK_NULL_HANDLE;
	memset(&hdr_write, 0, sizeof(hdr_write));
	vk.color = &color_target;
	vk.depth = NULL;
	vk.layer = 0;
	vk.views = 1;
	vk.capabilities.color_read = 1;
	vk.input_layout = (VkDescriptorSetLayout)(uintptr_t)77;
	vk.ring_mapped = post_ring;
	vk.ring = (VkBuffer)(uintptr_t)9;
	recording.main = (VkCommandBuffer)(uintptr_t)1;
	recording.descriptor_pools = &post_pool;
	recording.descriptor_pool_count = 1;
	vkCmdPipelineBarrier = hdr_barrier;
	vkCmdBeginRendering = hdr_begin;
	vkCmdEndRendering = hdr_end;
	vkUpdateDescriptorSets = hdr_update;
	vkCmdBindDescriptorSets = hdr_bind_sets;
	vkCmdDraw = hdr_draw;
	vkCmdBindPipeline = post_bind_pipeline;
	vkCmdSetViewport = fake_viewport;
	vkCmdSetScissor = fake_scissor;
	vkCmdSetDepthBias = fake_bias;
	vkCmdSetBlendConstants = fake_blend;
	vkCmdSetStencilReference = fake_stencil;
	vkCmdSetPrimitiveTopology = fake_topology;
	vkCreateGraphicsPipelines = hdr_pipeline;
}

/* a triangle, its pipeline reading its colour attachment or not */
static void hdr_triangle(int reads_color)
{
	static struct xgpu_vulkan_dynamic_state dynamic;
	struct xgpu_vulkan_draw draw;
	int stage;

	memset(&dynamic, 0, sizeof(dynamic));
	dynamic.viewport[2] = 64;
	dynamic.viewport[3] = 48;
	dynamic.depth_range[1] = 1.0f;
	memset(&draw, 0, sizeof(draw));
	draw.pipeline = (VkPipeline)(uintptr_t)(reads_color ? 91 : 92);
	draw.dynamic = &dynamic;
	draw.count = 3;
	draw.topology = XGPU_VULKAN_TOPOLOGY_TRIANGLES;
	draw.reads_color = (uint8_t)reads_color;
	for (stage = 0; stage < 4; stage++)
		draw.textures[stage].kind = XGPU_VULKAN_IMAGE_2D;
	xgpu_vulkan_draw(&draw);
}

static void local_read_test(void)
{
	VkDescriptorSet first;

	/* two draws that read the colour attachment, and one that does not:
	the input set written once and bound once at set 2, a barrier before
	each reading draw, none before the other */
	hdr_reset();
	hdr_triangle(1);
	hdr_triangle(1);
	hdr_triangle(0);
	assert(!strcmp(hdr_events, "TBURISDRDD"));
	assert(hdr_dependency == VK_DEPENDENCY_BY_REGION_BIT);
	assert(hdr_stages[0] == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT &&
		hdr_stages[1] == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	assert(hdr_memory.srcAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT &&
		hdr_memory.dstAccessMask == VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);
	assert(hdr_write.dstBinding == 0 && hdr_write.descriptorCount == 1 && hdr_write.dstSet == hdr_input);
	assert(hdr_write_image.imageView == color_target.attachments[0]);
	assert(hdr_write_image.imageLayout == VK_IMAGE_LAYOUT_GENERAL && !hdr_write_image.sampler);
	assert(vk.statistics.local_reads == 2);
	first = hdr_input;

	/* the next submission writes its own set (the slot's pools are reset
	as it is used again) */
	vk.serial++;
	memset(hdr_events, 0, sizeof(hdr_events));
	vk.bound.input = VK_NULL_HANDLE;
	hdr_triangle(1);
	assert(!strcmp(hdr_events, "URID") && hdr_input != first);

	/* reads in rasterization order: no barrier at all */
	hdr_reset();
	vk.capabilities.raster_order = 1;
	hdr_triangle(1);
	hdr_triangle(1);
	hdr_triangle(0);
	assert(!strcmp(hdr_events, "TBUISDDD") && !vk.statistics.local_reads);

	/* a device that cannot read its attachments draws no such draw */
	hdr_reset();
	vk.capabilities.color_read = 0;
	vk.input_layout = VK_NULL_HANDLE;
	hdr_triangle(1);
	assert(!strchr(hdr_events, 'D') && !strchr(hdr_events, 'I'));
	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	reset();
}

/* a pipeline that reads its colour attachment blends in its shader: the
fixed-function blending off, in rasterization order where the device
orders the reads */
static void hdr_pipeline_test(void)
{
	struct xgpu_vulkan_pipeline_description description;
	VkResult result;
	double elapsed;

	hdr_reset();
	vk.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	memset(&description, 0, sizeof(description));
	description.vertex_shader = (VkShaderModule)(uintptr_t)1;
	description.pixel_shader = (VkShaderModule)(uintptr_t)2;
	description.primitive_class = XGPU_VULKAN_CLASS_TRIANGLES;
	description.blend = 1;
	/* (an add: xgpu_backend.h's XGPU_BLEND_ONE) */
	description.blend_source = description.blend_destination = 1;
	description.color_mask = 15;
	description.color_format = XGPU_VULKAN_FORMAT_RGBA16F;
	description.samples = 1;
	assert(pipeline_build(&description, &result, &elapsed) && result == VK_SUCCESS);
	assert(hdr_blend_attachment.blendEnable && !hdr_blend.flags);
	assert(hdr_color_format == VK_FORMAT_R16G16B16A16_SFLOAT);
	description.color_read = 1;
	assert(pipeline_build(&description, &result, &elapsed));
	assert(!hdr_blend_attachment.blendEnable && !hdr_blend.flags && hdr_blend_attachment.colorWriteMask == 15);
	vk.capabilities.raster_order = 1;
	assert(pipeline_build(&description, &result, &elapsed));
	assert(!hdr_blend_attachment.blendEnable &&
		hdr_blend.flags == VK_PIPELINE_COLOR_BLEND_STATE_CREATE_RASTERIZATION_ORDER_ATTACHMENT_ACCESS_BIT_EXT);
	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	reset();
}

/* the surface's formats: the HDR ones first, then 8 bits in HDR10's
colour space, then in sRGB's (which an SDR swapchain must take) */
static VkResult VKAPI_CALL hdr_formats(VkPhysicalDevice physical, VkSurfaceKHR surface, uint32_t *count,
	VkSurfaceFormatKHR *formats)
{
	(void)physical; (void)surface;
	assert(*count >= 4);
	*count = 4;
	formats[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
	formats[0].colorSpace = VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
	formats[1].format = VK_FORMAT_B8G8R8A8_UNORM;
	formats[1].colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
	formats[2].format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	formats[2].colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
	formats[3].format = VK_FORMAT_B8G8R8A8_UNORM;
	formats[3].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	return VK_SUCCESS;
}

static void hdr_display(int hdr, float headroom, float white_nits, int white_reported)
{
	(void)white_nits;
	(void)white_reported;
	assert(hdr_tells < 8);
	hdr_told[hdr_tells][0] = hdr;
	hdr_told_headroom[hdr_tells] = headroom;
	hdr_tells++;
}

static struct xgpu_vulkan_image *hdr_encode(struct xgpu_vulkan_image *image, const struct xgpu_hdr_frame *frame,
	uint8_t format)
{
	assert(image == &window_frame);
	hdr_encodes++;
	hdr_encoded_format = format;
	hdr_encoded_frame = *frame;
	if (hdr_encode_fails)
		return NULL;
	hdr_encoded.description.format = format;
	return &hdr_encoded;
}

static void hdr_window_reset(void)
{
	window_reset();
	vkGetPhysicalDeviceSurfaceFormatsKHR = hdr_formats;
	vk.colorspaces = 1;
	vk.encode = hdr_encode;
	vk.display_hdr = hdr_display;
	vk.display_hdr_told = -1;
	vk.hdr_paper_white = 100;
	xgpu_vulkan_window_hdr(0, 1.0f, 1.0f);
	hdr_tells = hdr_encodes = 0;
	hdr_encode_fails = 0;
	memset(&hdr_encoded, 0, sizeof(hdr_encoded));
	hdr_encoded.image = (VkImage)(uintptr_t)800;
	hdr_encoded.description.width = 640;
	hdr_encoded.description.height = 480;
}

static void hdr_window_test(void)
{
	struct xgpu_hdr_frame frame;

	/* an SDR display: an 8-bit swapchain in sRGB's colour space (not the
	first 8-bit format, in HDR10's), the frame blitted as it is; the
	display told it shows no HDR, once */
	hdr_window_reset();
	assert(window_present_frame() && window_swapchains == 1 && window_blit_source == window_frame.image);
	assert(window_made.imageFormat == VK_FORMAT_B8G8R8A8_UNORM);
	assert(window_made.imageColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
	assert(hdr_tells == 1 && !hdr_told[0][0] && hdr_told_headroom[0] == 1.0f && !hdr_encodes);
	assert(window_present_frame() && hdr_tells == 1 && window_swapchains == 1);
	xgpu_vulkan_hdr_frame(&frame);
	assert(frame.output == XGPU_HDR_OUTPUT_SDR && frame.paper_white == 1.0f && frame.peak == 1.0f);

	/* HDR on (SDL's, from another thread): told, with its headroom; the
	frames still SDR, so the swapchain stays */
	xgpu_vulkan_window_hdr(1, 2.5f, 4.0f);
	assert(window_present_frame() && hdr_tells == 2 && hdr_told[1][0] && hdr_told_headroom[1] == 4.0f);
	assert(window_swapchains == 1 && !hdr_encodes && window_blit_source == window_frame.image);

	/* the frames HDR: HDR10 (A2B10G10R10, PQ) where the surface has both
	(not Windows), the frame encoded for it and that blitted; paper white
	the system's SDR white (SDL's 2.5 times 80, 200 nits), which a Wayland
	compositor shows PQ's reference white at: BT.2408's 203 nits in PQ's
	units; the peak the headroom */
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_PUNCHY, 0, 100, 0);
	assert(window_present_frame() && window_swapchains == 2);
	assert(window_made.imageFormat == VK_FORMAT_A2B10G10R10_UNORM_PACK32);
	assert(window_made.imageColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT);
	assert(hdr_encodes == 1 && hdr_encoded_format == XGPU_VULKAN_FORMAT_RGB10A2 && window_blit_source == hdr_encoded.image);
	assert(hdr_encoded_frame.output == XGPU_HDR_OUTPUT_PQ && hdr_encoded_frame.mode == XGPU_HDR_MODE_PUNCHY);
	assert(fabsf(hdr_encoded_frame.paper_white - 0.0203f) < 1e-6f && hdr_encoded_frame.peak == 4.0f);
	assert(hdr_tells == 2);

	/* paper white twice the system's: the peak in paper whites halved;
	past the headroom, no brighter than it */
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 0, 200, 0);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.paper_white - 0.0406f) < 1e-6f && frame.peak == 2.0f && frame.mode == XGPU_HDR_MODE_FAITHFUL);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 0, 500, 0);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.paper_white - 0.0812f) < 1e-6f && frame.peak == 1.0f);
	/* paper white in nits, whatever the system's white (the percent not
	read: 300 nits is 1.5 of its 200, PQ's 304.5); past the headroom, no
	brighter than it */
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 300, 50, 0);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.paper_white - 0.03045f) < 1e-6f && fabsf(frame.peak - 800.0f / 300.0f) < 1e-5f);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 1000, 100, 0);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.paper_white - 0.0812f) < 1e-6f && frame.peak == 1.0f);
	/* the UI's white: paper white's (1) by default; in nits over paper
	white's (400 over 200: twice), no brighter than the peak (800 nits:
	4 times), on Linux relative to the system's white as paper white is */
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 0, 100, 0);
	xgpu_vulkan_hdr_frame(&frame);
	assert(frame.ui == 1.0f);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 0, 100, 400);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.ui - 2.0f) < 1e-6f && frame.peak == 4.0f && fabsf(frame.paper_white - 0.0203f) < 1e-6f);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 400, 100, 100);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.ui - 0.25f) < 1e-6f);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 100, 100, 1000);
	xgpu_vulkan_hdr_frame(&frame);
	assert(fabsf(frame.ui - 8.0f) < 1e-5f && fabsf(frame.peak - 8.0f) < 1e-5f);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 1000, 100, 0);
	assert(window_present_frame() && window_swapchains == 2);

	/* scRGB asked for: half floats in extended linear sRGB, paper white its
	1.0, the reference white a Wayland compositor maps onto its own */
	setenv("HALO_VULKAN_HDR_OUTPUT", "scrgb", 1);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 0, 100, 0);
	assert(window_present_frame() && window_swapchains == 3);
	assert(window_made.imageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);
	assert(window_made.imageColorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
	assert(hdr_encoded_format == XGPU_VULKAN_FORMAT_RGBA16F && hdr_encoded_frame.output == XGPU_HDR_OUTPUT_SCRGB);
	assert(fabsf(hdr_encoded_frame.paper_white - 1.0f) < 1e-6f && hdr_encoded_frame.peak == 4.0f);
	unsetenv("HALO_VULKAN_HDR_OUTPUT");

	/* the display's HDR off: told, and the swapchain SDR again, the frame
	(still the half floats) blitted as it is, clamped */
	xgpu_vulkan_window_hdr(0, 1.0f, 1.0f);
	hdr_encodes = 0;
	assert(window_present_frame() && hdr_tells == 3 && !hdr_told[2][0] && window_swapchains == 4);
	assert(window_made.imageFormat == VK_FORMAT_B8G8R8A8_UNORM && !hdr_encodes);
	assert(window_blit_source == window_frame.image);

	/* an encoding that fails: nothing presented that frame, the window
	SDR from then on, and the display told it shows no HDR */
	xgpu_vulkan_window_hdr(1, 2.5f, 4.0f);
	hdr_encode_fails = 1;
	assert(!window_present_frame() && hdr_encodes == 1 && !vk.encode && vk.swapchain_stale);
	assert(window_present_frame() && window_made.imageFormat == VK_FORMAT_B8G8R8A8_UNORM);
	assert(!hdr_told[hdr_tells - 1][0]);

	/* without the instance's colour spaces: no HDR, whatever SDL says */
	hdr_window_reset();
	vk.colorspaces = 0;
	xgpu_vulkan_window_hdr(1, 2.5f, 4.0f);
	xgpu_vulkan_hdr_state(1, XGPU_HDR_MODE_FAITHFUL, 0, 100, 0);
	assert(window_present_frame() && window_made.imageFormat == VK_FORMAT_B8G8R8A8_UNORM && !hdr_encodes);
	assert(hdr_tells == 1 && !hdr_told[0][0]);
	reset();
}

/* the tone map: one triangle over the view's rectangle in the pass that
drew it (its colour and depth attachments, no pass ended or begun), its
pipeline reading the colour attachment and writing its colour alone, its
uniforms the mode and the peak; the encoding: one triangle over all of an
image of the swapchain's format, sampling the frame, its uniforms paper
white, the peak and the output */
static void hdr_post_test(void)
{
	static struct xgpu_vulkan_image target, depth, encoded, frame;
	static const int32_t corners[4] = { 8, 4, 40, 44 };
	static const float tone[4] = { 1.0f, 4.0f, 0.0f, 0.0f };
	const struct xgpu_vulkan_post_uniforms *uniforms;
	struct xgpu_hdr_frame encoding;

	hdr_reset();
	vk.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
	post_image(&target, 1, XGPU_VULKAN_FORMAT_RGBA16F, 64, 48);
	post_image(&depth, 3, XGPU_VULKAN_FORMAT_D24S8, 64, 48);
	depth.aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	memset(&post, 0, sizeof(post));
	post.vertex = (VkShaderModule)(uintptr_t)1;
	post.pixel[XGPU_VULKAN_POST_HDR_TONE_MAP] = (VkShaderModule)(uintptr_t)2;
	post.pixel[XGPU_VULKAN_POST_HDR_TONE_MAP_MS] = (VkShaderModule)(uintptr_t)3;
	post.pixel[XGPU_VULKAN_POST_HDR_ENCODE] = (VkShaderModule)(uintptr_t)4;
	post.sampler = (VkSampler)(uintptr_t)5;
	xgpu_vulkan_set_target_views(&target, &depth, 0, 1);
	hdr_triangle(0);
	memset(hdr_events, 0, sizeof(hdr_events));
	xgpu_vulkan_post_tone_map(&target, &depth, corners, tone);
	assert(strspn(hdr_events, "URISD") == strlen(hdr_events) && strstr(hdr_events, "URI") && strchr(hdr_events, 'D'));
	assert(vk.color == &target && vk.depth == &depth && vk.pass_active);
	assert(!hdr_blend_attachment.blendEnable && hdr_blend_attachment.colorWriteMask == 7);
	assert(hdr_color_format == VK_FORMAT_R16G16B16A16_SFLOAT && hdr_depth_format == VK_FORMAT_D24_UNORM_S8_UINT);
	assert(hdr_samples == VK_SAMPLE_COUNT_1_BIT && hdr_write_image.imageView == target.attachments[0]);
	uniforms = (const struct xgpu_vulkan_post_uniforms *)(post_ring + vk.bound.offsets[1]);
	assert(uniforms->hdr[0] == 1.0f && uniforms->hdr[1] == 4.0f);
	assert(vk.bound.viewport.x == 8.0f && vk.bound.viewport.y == 4.0f && vk.bound.viewport.width == 32.0f &&
		vk.bound.viewport.height == 40.0f);
	/* (an 8-bit target is no HDR frame: nothing) */
	post_image(&frame, 6, XGPU_VULKAN_FORMAT_BGRA8, 64, 48);
	memset(hdr_events, 0, sizeof(hdr_events));
	xgpu_vulkan_post_tone_map(&frame, NULL, corners, tone);
	assert(!hdr_events[0]);

	/* the encoding into an image of its own, of the swapchain's format
	(the pipelines made at the start) */
	post.encode[1] = (VkPipeline)(uintptr_t)93;
	post_image(&encoded, 7, XGPU_VULKAN_FORMAT_RGB10A2, 64, 48);
	post.encoded = &encoded;
	memset(&encoding, 0, sizeof(encoding));
	encoding.output = XGPU_HDR_OUTPUT_PQ;
	encoding.paper_white = 0.02f;
	encoding.peak = 4.0f;
	assert(!xgpu_vulkan_post_encode(&frame, &encoding, XGPU_VULKAN_FORMAT_RGBA16F));
	memset(hdr_events, 0, sizeof(hdr_events));
	assert(xgpu_vulkan_post_encode(&frame, &encoding, XGPU_VULKAN_FORMAT_RGB10A2) == &encoded);
	assert(vk.color == &encoded && !vk.depth && strchr(hdr_events, 'D') && !strchr(hdr_events, 'I'));
	uniforms = (const struct xgpu_vulkan_post_uniforms *)(post_ring + vk.bound.offsets[1]);
	assert(uniforms->hdr[0] == 0.02f && uniforms->hdr[1] == 4.0f && uniforms->hdr[2] == (float)XGPU_HDR_OUTPUT_PQ);
	assert(uniforms->metrics[0] == 0.0f && uniforms->metrics[2] == 1.0f / 64.0f && uniforms->metrics[3] == 1.0f / 48.0f);
	assert(vk.bound.viewport.width == 64.0f && vk.bound.viewport.height == 48.0f);

	free(target.attachments);
	free(depth.attachments);
	free(encoded.attachments);
	free(frame.attachments);
	memset(&post, 0, sizeof(post));
	recording.descriptor_pools = NULL;
	recording.descriptor_pool_count = 0;
	reset();
}

/* the half floats read back as an SDR display shows them */
static void half_test(void)
{
	assert(half_value(0x3c00) == 1.0f && half_value(0x3800) == 0.5f && half_value(0xbc00) == -1.0f);
	assert(half_value(0x4000) == 2.0f && half_value(0x0001) == ldexpf(1.0f, -24));
	assert(half_byte(0x3c00) == 255 && half_byte(0x4400) == 255 && half_byte(0xbc00) == 0);
	assert(half_byte(0x3800) == 128 && half_byte(0x0000) == 0);
}

/* ---------- HALO_VULKAN_DISCARDS: the passes in segments

Each pass's draws go into a secondary command buffer that inherits its
formats; its rendering is made at the submission, with the stores chosen
from what came after it: not stored where a later pass clears it whole,
stored where anything later loads or reads it, and the depth and samples
of a frame's last pass not stored once the next frames have cleared them
first for DISCARD_FRAMES frames. A resolve right after the pass that drew
the samples is made in that pass. */

#define DISCARDS_MAIN ((VkCommandBuffer)(uintptr_t)900)
#define DISCARDS_EARLY ((VkCommandBuffer)(uintptr_t)901)

struct discards_rendering
{
	VkCommandBuffer commands;
	VkRenderingInfo info;
	VkRenderingAttachmentInfo color, depth, stencil;
	/* the secondary command buffer it executed */
	VkCommandBuffer executed;
};

static struct discards_rendering discards_renderings[256];
static unsigned int discards_rendering_count, discards_executions, discards_resolves, discards_buffers;
static unsigned int discards_depth_image_clears;
static VkCommandBufferInheritanceRenderingInfo discards_inherited;
static VkFormat discards_inherited_color;
static VkCommandBufferUsageFlags discards_begin_flags;
static VkImageCreateInfo discards_image_info;
static uint32_t discards_memory_type;
static VkPhysicalDeviceDepthStencilResolveProperties discards_resolve_properties;
static struct xgpu_vulkan_image discards_ms, discards_ms_depth, discards_resolved, discards_resolved_depth;

static VkResult VKAPI_CALL discards_allocate(VkDevice device, const VkCommandBufferAllocateInfo *info,
	VkCommandBuffer *buffers)
{
	uint32_t index;

	(void)device;
	assert(info->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
	for (index = 0; index < info->commandBufferCount; index++)
		buffers[index] = (VkCommandBuffer)(uintptr_t)(2000 + discards_buffers++);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL discards_begin_commands(VkCommandBuffer commands, const VkCommandBufferBeginInfo *info)
{
	(void)commands;
	discards_begin_flags = info->flags;
	if (info->pInheritanceInfo && info->pInheritanceInfo->pNext)
	{
		discards_inherited = *(const VkCommandBufferInheritanceRenderingInfo *)info->pInheritanceInfo->pNext;
		discards_inherited_color = discards_inherited.colorAttachmentCount ?
			discards_inherited.pColorAttachmentFormats[0] : VK_FORMAT_UNDEFINED;
	}
	return VK_SUCCESS;
}

static void VKAPI_CALL discards_begin(VkCommandBuffer commands, const VkRenderingInfo *info)
{
	struct discards_rendering *rendering = &discards_renderings[discards_rendering_count++];

	assert(discards_rendering_count <= 256);
	memset(rendering, 0, sizeof(*rendering));
	rendering->commands = commands;
	rendering->info = *info;
	if (info->colorAttachmentCount)
		rendering->color = info->pColorAttachments[0];
	if (info->pDepthAttachment)
		rendering->depth = *info->pDepthAttachment;
	if (info->pStencilAttachment)
		rendering->stencil = *info->pStencilAttachment;
}

static void VKAPI_CALL discards_execute(VkCommandBuffer commands, uint32_t count, const VkCommandBuffer *buffers)
{
	assert(commands == DISCARDS_MAIN && count == 1);
	discards_executions++;
	if (discards_rendering_count && discards_renderings[discards_rendering_count - 1].commands == DISCARDS_MAIN &&
		!discards_renderings[discards_rendering_count - 1].executed)
		discards_renderings[discards_rendering_count - 1].executed = buffers[0];
}

static void VKAPI_CALL discards_resolve(VkCommandBuffer commands, VkImage source, VkImageLayout source_layout,
	VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageResolve *regions)
{
	(void)commands; (void)source; (void)source_layout; (void)destination; (void)destination_layout;
	(void)count; (void)regions;
	discards_resolves++;
}

static void VKAPI_CALL discards_clear_depth_image(VkCommandBuffer commands, VkImage image, VkImageLayout layout,
	const VkClearDepthStencilValue *value, uint32_t count, const VkImageSubresourceRange *ranges)
{
	(void)commands; (void)image; (void)layout; (void)value; (void)count; (void)ranges;
	discards_depth_image_clears++;
}

static VkResult VKAPI_CALL discards_create_image(VkDevice device, const VkImageCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkImage *image)
{
	(void)device; (void)allocator;
	discards_image_info = *info;
	*image = (VkImage)(uintptr_t)800;
	return VK_SUCCESS;
}

static void VKAPI_CALL discards_requirements(VkDevice device, VkImage image, VkMemoryRequirements *requirements)
{
	(void)device; (void)image;
	requirements->size = 64u * 1024 * 1024;
	requirements->alignment = 256;
	requirements->memoryTypeBits = 3;
}

static VkResult VKAPI_CALL discards_allocate_memory(VkDevice device, const VkMemoryAllocateInfo *info,
	const VkAllocationCallbacks *allocator, VkDeviceMemory *memory)
{
	(void)device; (void)allocator;
	discards_memory_type = info->memoryTypeIndex;
	*memory = (VkDeviceMemory)(uintptr_t)(810 + info->memoryTypeIndex);
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL discards_bind(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize offset)
{
	(void)device; (void)image; (void)memory; (void)offset;
	return VK_SUCCESS;
}

static void VKAPI_CALL discards_properties(VkPhysicalDevice physical, VkPhysicalDeviceProperties2 *properties)
{
	(void)physical;
	assert(properties->pNext);
	*(VkPhysicalDeviceDepthStencilResolveProperties *)properties->pNext = discards_resolve_properties;
}

static void discards_image(struct xgpu_vulkan_image *image, uintptr_t number, int depth, int samples)
{
	free(image->attachments);
	memset(image, 0, sizeof(*image));
	image->image = (VkImage)number;
	image->number = number;
	image->description.type = XGPU_VULKAN_IMAGE_2D;
	image->description.format = depth ? XGPU_VULKAN_FORMAT_D24S8 : XGPU_VULKAN_FORMAT_BGRA8;
	image->description.usage = XGPU_VULKAN_USAGE_SAMPLED | XGPU_VULKAN_USAGE_TARGET;
	image->description.samples = (uint8_t)samples;
	image->description.width = 64;
	image->description.height = 48;
	image->description.depth_or_layers = 1;
	image->description.levels = 1;
	image->format = depth ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_B8G8R8A8_UNORM;
	image->aspect = depth ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

static void discards_reset(int deferred)
{
	int index;

	for (index = 0; index < SLOTS; index++)
		free(vk.slots[index].secondaries);
	free(vk.segments);
	reset();
	discards_image(&color_target, 60, 0, 0);
	discards_image(&depth_target, 61, 1, 0);
	discards_image(&discards_ms, 62, 0, 4);
	discards_image(&discards_ms_depth, 63, 1, 4);
	discards_image(&discards_resolved, 64, 0, 0);
	discards_image(&discards_resolved_depth, 65, 1, 0);
	vk.deferred = deferred;
	vk.depth_resolve = 1;
	vk.device = (VkDevice)(uintptr_t)1;
	vk.recording = 0;
	vk.slot = NULL;
	for (index = 0; index < SLOTS; index++)
	{
		vk.slots[index].main = DISCARDS_MAIN;
		vk.slots[index].early = DISCARDS_EARLY;
		vk.slots[index].pool = (VkCommandPool)(uintptr_t)902;
	}
	vkResetCommandPool = fake_reset_pool;
	vkBeginCommandBuffer = discards_begin_commands;
	vkEndCommandBuffer = window_end_commands;
	vkAllocateCommandBuffers = discards_allocate;
	vkQueueSubmit = window_submit;
	vkGetSemaphoreCounterValue = fake_counter;
	vkCmdBeginRendering = discards_begin;
	vkCmdExecuteCommands = discards_execute;
	vkCmdResolveImage = discards_resolve;
	vkCmdClearDepthStencilImage = discards_clear_depth_image;
	discards_rendering_count = discards_executions = discards_resolves = 0;
	memset(&discards_inherited, 0, sizeof(discards_inherited));
	discards_begin_flags = 0;
}

/* the whole of the bound targets cleared, as a frame's first draws do,
in a pass of its own */
static void discards_clear(uint32_t parts)
{
	static const int32_t whole[1][4] = { { 0, 0, 64, 48 } };
	static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	struct xgpu_vulkan_image *color = vk.color, *depth = vk.depth;

	xgpu_vulkan_set_targets(NULL, NULL, 0);
	xgpu_vulkan_set_targets(color, depth, 0);
	xgpu_vulkan_clear(whole, 1, parts, black, 1.0f, 0);
}

/* drawn over part of the targets (a pass that loads them), in a pass of its
own */
static void discards_draw_over(void)
{
	static const int32_t part[1][4] = { { 4, 4, 8, 8 } };
	static const float grey[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
	struct xgpu_vulkan_image *color = vk.color, *depth = vk.depth;

	xgpu_vulkan_set_targets(NULL, NULL, 0);
	xgpu_vulkan_set_targets(color, depth, 0);
	xgpu_vulkan_clear(part, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS | XGPU_VULKAN_CLEAR_DEPTH |
		XGPU_VULKAN_CLEAR_STENCIL, grey, 0.5f, 1);
}

#define DISCARDS_ALL (XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS | XGPU_VULKAN_CLEAR_DEPTH | \
	XGPU_VULKAN_CLEAR_STENCIL)

/* the renderings made in the main command buffer (the passes) */
static unsigned int discards_main_renderings(void)
{
	unsigned int index, count = 0;

	for (index = 0; index < discards_rendering_count; index++)
		count += discards_renderings[index].commands == DISCARDS_MAIN;
	return count;
}

/* a frame of the bound targets: cleared whole first (or drawn over), then
drawn over; its last pass's rendering returned */
static struct discards_rendering *discards_frame(int cleared)
{
	discards_rendering_count = 0;
	if (cleared)
		discards_clear(DISCARDS_ALL);
	else
		discards_draw_over();
	discards_draw_over();
	xgpu_vulkan_frame_end(NULL, 0);
	assert(discards_main_renderings() == 2 && discards_renderings[1].commands == DISCARDS_MAIN);
	return &discards_renderings[1];
}

static void discards_test(void)
{
	struct discards_rendering *first, *second;
	unsigned int index;

	/* without discards: each pass begun in the main command buffer as it is
	drawn, every attachment stored, no secondary command buffers */
	discards_reset(0);
	vk.color = &color_target;
	vk.depth = &depth_target;
	discards_clear(DISCARDS_ALL);
	assert(discards_rendering_count == 1 && discards_renderings[0].commands == DISCARDS_MAIN);
	assert(discards_renderings[0].info.flags == 0);
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_rendering_count == 2 && !discards_executions && !discards_buffers);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* with them: nothing begun until the submission; then each pass's
	rendering of secondary command buffers, executing the one its draws
	went into, which inherited its formats */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	discards_clear(DISCARDS_ALL);
	assert(!discards_rendering_count && vk.pass_commands != DISCARDS_MAIN);
	assert(discards_begin_flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT);
	assert(discards_inherited.colorAttachmentCount == 1 && discards_inherited_color == VK_FORMAT_B8G8R8A8_UNORM);
	assert(discards_inherited.depthAttachmentFormat == VK_FORMAT_D24_UNORM_S8_UINT);
	assert(discards_inherited.stencilAttachmentFormat == VK_FORMAT_D24_UNORM_S8_UINT);
	assert(discards_inherited.rasterizationSamples == VK_SAMPLE_COUNT_1_BIT);
	/* a later pass that clears all of it whole: the first pass stores
	nothing (the clear writes over all of it) */
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_rendering_count == 2 && discards_executions == 2);
	first = &discards_renderings[0];
	second = &discards_renderings[1];
	assert(first->commands == DISCARDS_MAIN && first->info.flags == VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT);
	assert(first->executed && second->executed && first->executed != second->executed);
	assert(first->color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(first->color.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	assert(first->depth.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	assert(first->stencil.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	/* (the last: stored, as the next submission may use them) */
	assert(second->color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(second->depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(vk.statistics.stores_discarded == 3);

	/* a later piece of the same targets that loads them (the pass ended
	by something outside it, as an upload or a copy does): stored */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	discards_clear(DISCARDS_ALL);
	(void)main_commands();
	discards_draw_over();
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_main_renderings() == 3);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].stencil.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[1].color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(discards_renderings[1].color.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	/* (the outside work between them executed between them) */
	assert(discards_executions == 4);

	/* depth cleared whole, stencil loaded: depth alone not stored */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	discards_clear(DISCARDS_ALL);
	discards_clear(XGPU_VULKAN_CLEAR_DEPTH);
	xgpu_vulkan_fence();
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	assert(discards_renderings[0].stencil.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* sampled, copied or read after the pass, before the clear: stored */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	discards_clear(DISCARDS_ALL);
	pass_end();
	image_use(&color_target);
	image_use(&depth_target);
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].stencil.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* a "whole" clear of a pass smaller than its depth (a depth larger than
	the colour), or of one layer of several: not all of it, stored */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	depth_target.description.width = 128;
	discards_clear(DISCARDS_ALL);
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	discards_reset(1);
	color_target.description.type = XGPU_VULKAN_IMAGE_2D_ARRAY;
	color_target.description.depth_or_layers = 2;
	vk.color = &color_target;
	vk.depth = &depth_target;
	discards_clear(DISCARDS_ALL);
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);

	/* an image destroyed before the submission: its pass stores as before */
	discards_reset(1);
	{
		struct xgpu_vulkan_image *gone = calloc(1, sizeof(*gone));

		assert(gone);
		*gone = depth_target;
		gone->attachments = NULL;
		vk.color = &color_target;
		vk.depth = gone;
		discards_clear(DISCARDS_ALL);
		xgpu_vulkan_image_destroy(gone);
		assert(!vk.segments[0].depth.image);
		xgpu_vulkan_fence();
		assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	}

	/* the frame's last pass: depth and stencil stored until the frames
	have cleared them first for DISCARD_FRAMES frames, then not; the
	colour (not multisampled: the frame's picture) stored always */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	for (index = 1; index <= DISCARD_FRAMES; index++)
	{
		struct discards_rendering *last = discards_frame(1);
		VkAttachmentStoreOp expected = index < DISCARD_FRAMES ? VK_ATTACHMENT_STORE_OP_STORE :
			VK_ATTACHMENT_STORE_OP_DONT_CARE;

		assert(last->depth.storeOp == expected && last->stencil.storeOp == expected);
		assert(last->color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
		/* (the frame's first pass: its stores read by its second) */
		assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	}
	assert(vk.statistics.frame_end_discards == 2 && depth_target.discarded[0] && depth_target.discarded[1]);
	second = discards_frame(1);
	assert(second->depth.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	/* any other first use (a load): logged once, and counted again from
	nothing, so stored for DISCARD_FRAMES frames more */
	vk.discard_logged = 0;
	second = discards_frame(0);
	assert(vk.discard_logged == 1 && !depth_target.clears[0] && !depth_target.clears[1]);
	assert(second->depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	for (index = 1; index <= DISCARD_FRAMES; index++)
	{
		struct discards_rendering *last = discards_frame(1);

		assert(last->depth.storeOp == (index < DISCARD_FRAMES ? VK_ATTACHMENT_STORE_OP_STORE :
			VK_ATTACHMENT_STORE_OP_DONT_CARE));
	}
	/* a use in the same frame after the discard (as a later submission of
	it would make): checked as a first use, so counted again */
	assert(depth_target.discarded[0] && !depth_target.first_use_frame);
	image_use(&depth_target);
	assert(!depth_target.clears[0]);
	/* frames whose first use is a read, never discarded: the negative
	control of the learning */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	for (index = 0; index < 2 * DISCARD_FRAMES; index++)
		assert(discards_frame(0)->depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	/* stencil not cleared whole each frame: depth alone discarded */
	discards_reset(1);
	vk.color = &color_target;
	vk.depth = &depth_target;
	for (index = 1; index <= DISCARD_FRAMES; index++)
	{
		discards_rendering_count = 0;
		discards_clear(XGPU_VULKAN_CLEAR_DEPTH | XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS);
		xgpu_vulkan_frame_end(NULL, 0);
	}
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	assert(discards_renderings[0].stencil.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	/* not the frame's last submission (a readback's, a fence's): stored */
	discards_rendering_count = 0;
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* MSAA: the resolve right after the pass that drew the samples, in that
	pass (colour's average, depth's and stencil's first sample), and no
	vkCmdResolveImage; the samples then discarded at the frame's end once
	learned */
	discards_reset(1);
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	assert(discards_inherited.rasterizationSamples == VK_SAMPLE_COUNT_4_BIT);
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	xgpu_vulkan_image_resolve(&discards_ms_depth, &discards_resolved_depth);
	assert(vk.statistics.resolves_in_pass == 2 && !discards_resolves);
	xgpu_vulkan_fence();
	assert(discards_rendering_count == 1 && !discards_resolves);
	assert(discards_renderings[0].color.resolveMode == VK_RESOLVE_MODE_AVERAGE_BIT);
	assert(discards_renderings[0].color.resolveImageView == discards_resolved.attachments[0]);
	assert(discards_renderings[0].depth.resolveMode == VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
	assert(discards_renderings[0].stencil.resolveMode == VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
	assert(discards_renderings[0].depth.resolveImageView == discards_resolved_depth.attachments[0]);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	for (index = 1; index <= DISCARD_FRAMES; index++)
	{
		discards_rendering_count = 0;
		discards_clear(DISCARDS_ALL);
		xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
		xgpu_vulkan_frame_end(NULL, 0);
		assert(discards_rendering_count == 1 && discards_renderings[0].color.resolveImageView);
		assert(discards_renderings[0].color.storeOp == (index < DISCARD_FRAMES ? VK_ATTACHMENT_STORE_OP_STORE :
			VK_ATTACHMENT_STORE_OP_DONT_CARE));
	}
	/* resolved again with nothing drawn since (the stream resolves a target
	it bound and did not draw into): not made again, as the samples were
	discarded at the frame's end; made where the image resolved into was
	written since, or the samples drawn */
	{
		uint32_t skipped = vk.statistics.resolves_skipped;

		discards_resolves = 0;
		discards_rendering_count = 0;
		xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
		assert(vk.statistics.resolves_skipped == skipped + 1 && !discards_resolves);
		image_written(&discards_resolved);
		xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
		assert(vk.statistics.resolves_skipped == skipped + 1 && discards_resolves == 1);
		xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
		assert(vk.statistics.resolves_skipped == skipped + 2 && discards_resolves == 1);
		discards_draw_over();
		xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
		assert(vk.statistics.resolves_skipped == skipped + 2 && vk.statistics.resolves_in_pass);
		xgpu_vulkan_fence();
	}
	/* (the image resolved into: never discarded) */
	assert(!discards_resolved.clears[0] && !image_discardable(&discards_resolved));
	/* drawing over the samples again after the resolve (the HUD over the
	3D view): the pass with the resolve stores them */
	discards_rendering_count = 0;
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	discards_draw_over();
	xgpu_vulkan_frame_end(NULL, 0);
	assert(discards_renderings[0].color.resolveImageView);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* not right after the pass (work outside it between them), or the
	image resolved into sampled in the pass, or a pass of part of the
	image: resolved outside it as before, its samples stored */
	discards_reset(1);
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	(void)main_commands();
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	assert(!vk.statistics.resolves_in_pass && discards_resolves == 1);
	xgpu_vulkan_fence();
	assert(!discards_renderings[0].color.resolveImageView);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	discards_reset(1);
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	image_use(&discards_resolved);
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	assert(!vk.statistics.resolves_in_pass && discards_resolves == 1);
	discards_reset(1);
	discards_resolved.description.width = discards_ms.description.width = 128;
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	assert(!vk.statistics.resolves_in_pass && discards_resolves == 1);
	/* no SAMPLE_ZERO resolve of depth and stencil: the depth's resolve pass
	of its own, as without discards */
	discards_reset(1);
	vk.depth_resolve = 0;
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_image_resolve(&discards_ms_depth, &discards_resolved_depth);
	assert(!vk.statistics.resolves_in_pass);
	xgpu_vulkan_fence();
	/* (the resolve pass, inline in the outside work's secondary buffer,
	recorded as it was asked for; the pass that drew, at the submission) */
	assert(discards_rendering_count == 2 && discards_renderings[0].commands != DISCARDS_MAIN);
	assert(discards_renderings[0].depth.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(discards_renderings[0].depth.resolveMode == VK_RESOLVE_MODE_SAMPLE_ZERO_BIT);
	assert(discards_renderings[1].commands == DISCARDS_MAIN && !discards_renderings[1].depth.resolveImageView);
	assert(discards_renderings[1].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* a transient image's samples resolved outside a pass: by a pass that
	loads them, not vkCmdResolveImage (which a transient image cannot be) */
	discards_reset(1);
	discards_ms.transient = 1;
	vk.color = &discards_ms;
	vk.depth = NULL;
	discards_clear(XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS);
	(void)main_commands();
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	assert(!discards_resolves && discards_rendering_count == 1);
	assert(discards_renderings[0].color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_NONE);
	assert(discards_renderings[0].color.resolveMode == VK_RESOLVE_MODE_AVERAGE_BIT);
	xgpu_vulkan_fence();

	/* what the device has: discards on unless turned off; depth resolved in
	passes where it has SAMPLE_ZERO for both; transient images where it
	has lazily allocated memory (HALO_VULKAN_TRANSIENT=0 none, =force
	without it) */
	discards_reset(0);
	vkGetPhysicalDeviceProperties2 = discards_properties;
	vk.memory_properties.memoryTypeCount = 2;
	vk.memory_properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	vk.memory_properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
		VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
	discards_resolve_properties.supportedDepthResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
	discards_resolve_properties.supportedStencilResolveModes = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
	unsetenv("HALO_VULKAN_DISCARDS");
	unsetenv("HALO_VULKAN_TRANSIENT");
	discards_choose();
	assert(vk.deferred && vk.depth_resolve && vk.transient == 1);
	setenv("HALO_VULKAN_DISCARDS", "0", 1);
	discards_choose();
	assert(!vk.deferred && !vk.transient && !vk.depth_resolve);
	setenv("HALO_VULKAN_DISCARDS", "1", 1);
	discards_choose();
	assert(vk.deferred && vk.depth_resolve && vk.transient == 1);
	discards_resolve_properties.supportedStencilResolveModes = 0;
	discards_choose();
	assert(vk.deferred && !vk.depth_resolve);
	setenv("HALO_VULKAN_TRANSIENT", "0", 1);
	discards_choose();
	assert(!vk.transient);
	vk.memory_properties.memoryTypeCount = 1;
	unsetenv("HALO_VULKAN_TRANSIENT");
	discards_choose();
	assert(!vk.transient);
	setenv("HALO_VULKAN_TRANSIENT", "force", 1);
	discards_choose();
	assert(vk.transient == 2);
	unsetenv("HALO_VULKAN_TRANSIENT");
	unsetenv("HALO_VULKAN_DISCARDS");
	/* (by default on a tiler alone: one with lazily allocated memory, or of
	a tiler's vendor; an immediate GPU records into the main command
	buffer, unless HALO_VULKAN_DISCARDS=1) */
	vk.properties.vendorID = 0x10de;
	discards_choose();
	assert(!vk.deferred && !vk.aside && !vk.transient);
	vk.properties.vendorID = 0x5143;
	discards_choose();
	assert(vk.deferred && vk.aside);
	vk.properties.vendorID = 0x1002;
	vk.memory_properties.memoryTypeCount = 2;
	discards_choose();
	assert(vk.deferred && vk.transient == 1);
	vk.memory_properties.memoryTypeCount = 1;
	discards_choose();
	assert(!vk.deferred);
	setenv("HALO_VULKAN_DISCARDS", "1", 1);
	discards_choose();
	assert(vk.deferred);
	unsetenv("HALO_VULKAN_DISCARDS");
	vk.properties.vendorID = 0;

	/* a transient multisampled target: an attachment alone, in lazily
	allocated memory, cleared by a pass of the early work; an ordinary
	one (no transient, or one sample) as before */
	discards_reset(1);
	vk.transient = 1;
	vk.memory_properties.memoryTypeCount = 2;
	vk.memory_properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	vk.memory_properties.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
	vkCreateImage = discards_create_image;
	vkGetImageMemoryRequirements = discards_requirements;
	vkAllocateMemory = discards_allocate_memory;
	vkBindImageMemory = discards_bind;
	{
		struct xgpu_vulkan_image_description description;
		struct xgpu_vulkan_image *made;

		memset(&description, 0, sizeof(description));
		description.type = XGPU_VULKAN_IMAGE_2D;
		description.format = XGPU_VULKAN_FORMAT_D24S8;
		description.usage = XGPU_VULKAN_USAGE_SAMPLED | XGPU_VULKAN_USAGE_TARGET;
		description.samples = 4;
		description.width = 64;
		description.height = 48;
		vk.depth_format = VK_FORMAT_D24_UNORM_S8_UINT;
		discards_depth_image_clears = 0;
		made = xgpu_vulkan_image_create(&description);
		assert(made && made->transient);
		assert(discards_image_info.usage == (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT));
		assert(discards_memory_type == 1 && !discards_depth_image_clears);
		assert(discards_rendering_count == 1 && discards_renderings[0].commands == DISCARDS_EARLY);
		assert(discards_renderings[0].depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
		assert(discards_renderings[0].depth.clearValue.depthStencil.depth == 1.0f);
		xgpu_vulkan_image_destroy(made);
		vk.transient = 0;
		discards_rendering_count = 0;
		made = xgpu_vulkan_image_create(&description);
		assert(made && !made->transient && discards_depth_image_clears == 1 && !discards_rendering_count);
		assert(discards_image_info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
		xgpu_vulkan_image_destroy(made);
		vk.transient = 1;
		description.samples = 0;
		made = xgpu_vulkan_image_create(&description);
		assert(made && !made->transient);
		xgpu_vulkan_image_destroy(made);
	}

	for (index = 0; index < SLOTS; index++)
	{
		free(vk.slots[index].secondaries);
		vk.slots[index].secondaries = NULL;
	}
	free(vk.segments);
	vk.segments = NULL;
	free(discards_ms.attachments);
	free(discards_ms_depth.attachments);
	free(discards_resolved.attachments);
	free(discards_resolved_depth.attachments);
	memset(&discards_ms, 0, sizeof(discards_ms));
	memset(&discards_ms_depth, 0, sizeof(discards_ms_depth));
	memset(&discards_resolved, 0, sizeof(discards_resolved));
	memset(&discards_resolved_depth, 0, sizeof(discards_resolved_depth));
}

/* Aside passes (with discards): a pass of other targets that draws colour
alone, and the copies and uploads about it, played before the pass it
interrupted, whose pieces are one rendering; each thing that must keep the
order drawn (a piece sampled what is put aside writes, what is put aside
reads the chain's targets, other work outside passes, a pass with depth, a
resolve in the last piece, a piece that clears) keeps it; and a buffer the
submission reads, written again, renamed rather than written in order. */

static char aside_log[64];
static unsigned int aside_log_count, aside_copies, aside_buffer_copies;
static VkCommandBuffer aside_executed[64];
static unsigned int aside_executed_count;
static VkCommandBuffer aside_copy_commands;
static VkBuffer aside_copy_source, aside_copy_destination;
static struct xgpu_vulkan_image aside_target, aside_other, aside_other_depth;
static unsigned int aside_buffers_made;
static unsigned char aside_memory[4][256];
static int aside_hold;

/* the GPU's progress: held before the submission being recorded, else
past it */
static VkResult VKAPI_CALL aside_counter(VkDevice device, VkSemaphore semaphore, uint64_t *value)
{
	(void)device; (void)semaphore;
	*value = aside_hold ? 0 : vk.serial;
	return VK_SUCCESS;
}

static void VKAPI_CALL aside_begin(VkCommandBuffer commands, const VkRenderingInfo *info)
{
	discards_begin(commands, info);
	assert(aside_log_count < sizeof(aside_log) - 1);
	aside_log[aside_log_count++] = 'B';
}

static void VKAPI_CALL aside_execute(VkCommandBuffer commands, uint32_t count, const VkCommandBuffer *buffers)
{
	assert(commands == DISCARDS_MAIN && count == 1);
	assert(aside_log_count < sizeof(aside_log) - 1 && aside_executed_count < 64);
	aside_log[aside_log_count++] = 'X';
	aside_executed[aside_executed_count++] = buffers[0];
}

static void VKAPI_CALL aside_end(VkCommandBuffer commands)
{
	assert(commands == DISCARDS_MAIN);
	assert(aside_log_count < sizeof(aside_log) - 1);
	aside_log[aside_log_count++] = 'E';
}

static void VKAPI_CALL aside_copy_image(VkCommandBuffer commands, VkImage source, VkImageLayout source_layout,
	VkImage destination, VkImageLayout destination_layout, uint32_t count, const VkImageCopy *regions)
{
	(void)commands; (void)source; (void)source_layout; (void)destination; (void)destination_layout;
	(void)count; (void)regions;
	aside_copies++;
}

static void VKAPI_CALL aside_copy_buffer(VkCommandBuffer commands, VkBuffer source, VkBuffer destination,
	uint32_t count, const VkBufferCopy *regions)
{
	(void)regions;
	assert(count == 1);
	aside_copy_commands = commands;
	aside_copy_source = source;
	aside_copy_destination = destination;
	aside_buffer_copies++;
}

static unsigned int aside_cells_copies;

static void VKAPI_CALL aside_copy_cells(VkCommandBuffer commands, VkBuffer source, VkImage destination,
	VkImageLayout layout, uint32_t count, const VkBufferImageCopy *regions)
{
	(void)commands; (void)source; (void)destination; (void)layout; (void)regions;
	assert(count == 1);
	aside_cells_copies++;
}

static void VKAPI_CALL aside_bind_pipeline(VkCommandBuffer commands, VkPipelineBindPoint point, VkPipeline pipeline)
{
	(void)commands; (void)point; (void)pipeline;
}

static VkResult VKAPI_CALL aside_create_buffer(VkDevice device, const VkBufferCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkBuffer *buffer)
{
	(void)device; (void)allocator;
	assert(info->size == 256);
	*buffer = (VkBuffer)(uintptr_t)(700 + aside_buffers_made++);
	return VK_SUCCESS;
}

static void VKAPI_CALL aside_buffer_requirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements *requirements)
{
	(void)device; (void)buffer;
	requirements->size = 256;
	requirements->alignment = 16;
	requirements->memoryTypeBits = 1;
}

static VkResult VKAPI_CALL aside_bind_buffer(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
	VkDeviceSize offset)
{
	(void)device; (void)buffer; (void)memory; (void)offset;
	return VK_SUCCESS;
}

static VkResult VKAPI_CALL aside_map(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
	VkMemoryMapFlags flags, void **mapped)
{
	(void)device; (void)memory; (void)offset; (void)size; (void)flags;
	assert(aside_buffers_made <= 4);
	*mapped = aside_memory[aside_buffers_made - 1];
	return VK_SUCCESS;
}

static void aside_reset(int aside)
{
	discards_reset(1);
	vk.aside = aside;
	discards_image(&aside_target, 66, 0, 0);
	discards_image(&aside_other, 67, 0, 0);
	discards_image(&aside_other_depth, 68, 1, 0);
	vkCmdBeginRendering = aside_begin;
	vkCmdExecuteCommands = aside_execute;
	vkCmdEndRendering = aside_end;
	vkCmdCopyImage = aside_copy_image;
	memset(aside_log, 0, sizeof(aside_log));
	aside_log_count = aside_executed_count = aside_copies = 0;
	vk.color = &color_target;
	vk.depth = &depth_target;
}

/* drawn over part of these targets (a pass that loads them) */
static void aside_draw(struct xgpu_vulkan_image *color, struct xgpu_vulkan_image *depth)
{
	static const int32_t part[1][4] = { { 4, 4, 8, 8 } };
	static const float grey[4] = { 0.5f, 0.5f, 0.5f, 1.0f };

	xgpu_vulkan_set_targets(color, depth, 0);
	xgpu_vulkan_clear(part, 1, XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS |
		(depth ? XGPU_VULKAN_CLEAR_DEPTH | XGPU_VULKAN_CLEAR_STENCIL : 0), grey, 0.5f, 1);
}

/* the view cleared, a pass of the aside target, the view drawn again (with
work outside passes between, if asked for): the view's three secondary
command buffers */
static void aside_frame(VkCommandBuffer commands[3])
{
	discards_clear(DISCARDS_ALL);
	commands[0] = vk.pass_commands;
	aside_draw(&aside_target, NULL);
	commands[1] = vk.pass_commands;
}

static void aside_test(void)
{
	VkCommandBuffer commands[6];

	/* the aside target's pass played first, the view's two pieces one
	rendering: begun with the first's clears, ended with the last's
	stores */
	aside_reset(1);
	aside_frame(commands);
	aside_draw(&color_target, &depth_target);
	commands[2] = vk.pass_commands;
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXXE"));
	assert(aside_executed[0] == commands[1] && aside_executed[1] == commands[0] && aside_executed[2] == commands[2]);
	assert(discards_rendering_count == 2);
	assert(discards_renderings[0].color.imageView == aside_target.attachments[0] && !discards_renderings[0].info.pDepthAttachment);
	assert(discards_renderings[1].color.imageView == color_target.attachments[0]);
	assert(discards_renderings[1].color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(discards_renderings[1].depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(discards_renderings[1].stencil.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(discards_renderings[1].color.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(discards_renderings[1].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(vk.statistics.passes_continued == 1 && vk.statistics.passes_aside == 1);
	assert(!vk.chain_head && !vk.segment_count);

	/* the negative control: without aside passes, in the order drawn */
	aside_reset(0);
	aside_frame(commands);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE"));
	assert(!vk.statistics.passes_continued && !vk.statistics.passes_aside);

	/* two passes put aside and a copy between them (of the aside target
	into another image): all before the view's three pieces */
	aside_reset(1);
	aside_frame(commands);
	xgpu_vulkan_image_copy(&aside_target, 0, 0, &aside_other, 0, 0);
	assert(aside_copies == 1);
	aside_draw(&color_target, &depth_target);
	commands[2] = vk.pass_commands;
	aside_draw(&aside_other, NULL);
	commands[3] = vk.pass_commands;
	aside_draw(&color_target, &depth_target);
	commands[4] = vk.pass_commands;
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEXBXEBXXXE"));
	assert(aside_executed[0] == commands[1] && aside_executed[2] == commands[3]);
	assert(aside_executed[3] == commands[0] && aside_executed[4] == commands[2] && aside_executed[5] == commands[4]);
	assert(vk.statistics.passes_continued == 2 && vk.statistics.passes_aside == 2);

	/* a piece sampled the image the pass put aside would draw into: in
	the order drawn (the aside target's pass then begins a chain of its
	own, which the view's pass, with depth, ends) */
	aside_reset(1);
	discards_clear(DISCARDS_ALL);
	image_use(&aside_target);
	aside_draw(&aside_target, NULL);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);
	/* or that a copy put aside would write into */
	aside_reset(1);
	discards_clear(DISCARDS_ALL);
	image_use(&aside_other);
	aside_draw(&aside_target, NULL);
	xgpu_vulkan_image_copy(&aside_target, 0, 0, &aside_other, 0, 0);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEXBXE") && !vk.statistics.passes_continued);

	/* what is put aside reads the view (samples it, copies it): in order */
	aside_reset(1);
	aside_frame(commands);
	image_use(&color_target);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);
	aside_reset(1);
	aside_frame(commands);
	xgpu_vulkan_image_copy(&color_target, 0, 0, &aside_other, 0, 0);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEXBXE") && !vk.statistics.passes_continued);

	/* other work outside passes between: in order */
	aside_reset(1);
	aside_frame(commands);
	(void)main_commands();
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEXBXE") && !vk.statistics.passes_continued);

	/* a pass with depth, or multisampled, between: in order */
	aside_reset(1);
	discards_clear(DISCARDS_ALL);
	aside_draw(&aside_other, &aside_other_depth);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);
	aside_reset(1);
	discards_clear(DISCARDS_ALL);
	aside_draw(&discards_ms, NULL);
	aside_draw(&color_target, &depth_target);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);

	/* the view's next pass clears it whole: not a piece, and the pass
	before it stores nothing */
	aside_reset(1);
	aside_frame(commands);
	xgpu_vulkan_set_targets(&color_target, &depth_target, 0);
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	/* (its colour alone) */
	aside_reset(1);
	aside_frame(commands);
	xgpu_vulkan_set_targets(&color_target, &depth_target, 0);
	discards_clear(XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);
	assert(discards_renderings[0].color.storeOp == VK_ATTACHMENT_STORE_OP_DONT_CARE);
	assert(discards_renderings[0].depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);

	/* the last piece resolved in its pass: the chain's last */
	aside_reset(1);
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	assert(vk.statistics.resolves_in_pass == 1);
	aside_draw(&aside_target, NULL);
	aside_draw(&discards_ms, &discards_ms_depth);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXEBXE") && !vk.statistics.passes_continued);
	assert(discards_renderings[0].color.resolveImageView);

	/* the pieces' resolve, after the last: in the one rendering */
	aside_reset(1);
	vk.color = &discards_ms;
	vk.depth = &discards_ms_depth;
	discards_clear(DISCARDS_ALL);
	aside_draw(&aside_target, NULL);
	aside_draw(&discards_ms, &discards_ms_depth);
	xgpu_vulkan_image_resolve(&discards_ms, &discards_resolved);
	xgpu_vulkan_fence();
	assert(!strcmp(aside_log, "BXEBXXE") && vk.statistics.passes_continued == 1);
	assert(discards_renderings[1].color.resolveImageView && discards_renderings[1].color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);

	/* the view destroyed meanwhile: in order */
	aside_reset(1);
	{
		struct xgpu_vulkan_image *gone = calloc(1, sizeof(*gone));

		assert(gone);
		*gone = aside_other;
		gone->attachments = NULL;
		vk.color = gone;
		vk.depth = NULL;
		discards_clear(XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS);
		aside_draw(&aside_target, NULL);
		assert(vk.chain_head == 1);
		xgpu_vulkan_image_destroy(gone);
		assert(!vk.chain_head);
		xgpu_vulkan_fence();
	}

	/* a buffer the submission reads, written again: renamed, its old bytes
	copied into the new one by the early commands and the write after
	them, nothing in the main commands (which would end the chain); what
	was recorded keeps the old buffer, a spare once the GPU passes it,
	which the next rename takes */
	aside_reset(1);
	{
		struct xgpu_vulkan_buffer buffer;
		static unsigned char ring[4096];
		unsigned char data[16] = { 5 };
		VkBuffer first;

		vk.ring_mapped = ring;
		vk.memory_properties.memoryTypeCount = 1;
		vk.memory_properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		vkCreateBuffer = aside_create_buffer;
		vkGetBufferMemoryRequirements = aside_buffer_requirements;
		vkAllocateMemory = discards_allocate_memory;
		vkBindBufferMemory = aside_bind_buffer;
		vkMapMemory = aside_map;
		vkCmdCopyBuffer = aside_copy_buffer;
		aside_buffers_made = aside_buffer_copies = 0;
		vkGetSemaphoreCounterValue = aside_counter;
		aside_hold = 1;
		memset(&buffer, 0, sizeof(buffer));
		buffer.size = 256;
		buffer.buffer = first = (VkBuffer)(uintptr_t)600;
		buffer.mapped = aside_memory[3];
		discards_clear(DISCARDS_ALL);
		assert(xgpu_vulkan_buffer_handle(&buffer) == first);
		aside_draw(&aside_target, NULL);
		xgpu_vulkan_buffer_write(&buffer, 16, data, sizeof(data), 0);
		assert(vk.statistics.buffers_renamed == 1 && aside_buffers_made == 1);
		assert(buffer.buffer != first && buffer.renamed == vk.serial && !buffer.used);
		assert(aside_buffer_copies == 2 && aside_copy_commands == DISCARDS_EARLY);
		assert(aside_copy_destination == buffer.buffer && aside_copy_source != aside_copy_destination);
		assert(vk.spare_count == 1 && vk.spares[0].buffer == first);
		assert(!vk.statistics.uploads_ordered && vk.chain_head);
		/* (not read since: written early, not renamed again) */
		xgpu_vulkan_buffer_write(&buffer, 32, data, sizeof(data), 0);
		assert(vk.statistics.buffers_renamed == 1 && aside_copy_commands == DISCARDS_EARLY);
		/* (bytes no work reads, while the copy is still to be made: also
		written after it) */
		xgpu_vulkan_buffer_write(&buffer, 48, data, sizeof(data), 1);
		assert(aside_memory[0][48] == 5 && aside_buffer_copies == 4 && aside_copy_commands == DISCARDS_EARLY);
		aside_draw(&color_target, &depth_target);
		xgpu_vulkan_fence();
		assert(!strcmp(aside_log, "BXEBXXE"));
		/* the next rename, once the GPU has passed the first: the spare */
		aside_hold = 0;
		recording_begin();
		(void)xgpu_vulkan_buffer_handle(&buffer);
		xgpu_vulkan_buffer_write(&buffer, 0, data, sizeof(data), 0);
		assert(vk.statistics.buffers_renamed == 2 && buffer.buffer == first && aside_buffers_made == 1);
		/* without aside passes: written in order, as before */
		vk.aside = 0;
		(void)xgpu_vulkan_buffer_handle(&buffer);
		xgpu_vulkan_buffer_write(&buffer, 0, data, sizeof(data), 0);
		assert(vk.statistics.buffers_renamed == 2 && vk.statistics.uploads_ordered == 1);
		assert(aside_copy_commands != DISCARDS_EARLY);
		xgpu_vulkan_fence();
		free(vk.spares);
		vk.spares = NULL;
		vk.spare_count = vk.spare_capacity = 0;
	}

	/* the lights' block, bound into the draw set, which would go on
	reading the buffer it was bound with (a set may not be written again
	while a recording has it bound): read by a draw and written again,
	never renamed but written in order, which ends the chain; and the
	lights' cells, used by the draw in its pass, so that an upload into
	them is not put aside before it */
	aside_reset(1);
	{
		static struct xgpu_vulkan_buffer block;
		static struct xgpu_vulkan_image cells;
		static struct xgpu_vulkan_dynamic_state dynamic;
		static unsigned char ring[4096 + 32 * 32 * 16 * 16], grid[32 * 32 * 16 * 16];
		struct xgpu_vulkan_draw draw;
		unsigned char data[256] = { 5 };
		VkBuffer bound = (VkBuffer)(uintptr_t)610;
		int stage;

		vk.ring_mapped = ring;
		/* (a rename would have its buffer) */
		vk.memory_properties.memoryTypeCount = 1;
		vk.memory_properties.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
			VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		vkCreateBuffer = aside_create_buffer;
		vkGetBufferMemoryRequirements = aside_buffer_requirements;
		vkAllocateMemory = discards_allocate_memory;
		vkBindBufferMemory = aside_bind_buffer;
		vkMapMemory = aside_map;
		vkCmdCopyBuffer = aside_copy_buffer;
		vkCmdCopyBufferToImage = aside_copy_cells;
		vkCmdBindPipeline = aside_bind_pipeline;
		vkCmdBindDescriptorSets = fake_bind_sets;
		vkCmdSetViewport = fake_viewport;
		vkCmdSetScissor = fake_scissor;
		vkCmdSetDepthBias = fake_bias;
		vkCmdSetBlendConstants = fake_blend;
		vkCmdSetStencilReference = fake_stencil;
		vkCmdSetPrimitiveTopology = fake_topology;
		vkCmdDraw = plain_draw;
		aside_buffers_made = aside_buffer_copies = aside_cells_copies = 0;
		memset(&block, 0, sizeof(block));
		block.size = sizeof(data);
		block.buffer = bound;
		block.mapped = aside_memory[3];
		block.named_by_set = 1;
		memset(&cells, 0, sizeof(cells));
		cells.description.type = XGPU_VULKAN_IMAGE_3D;
		cells.description.format = XGPU_VULKAN_FORMAT_RGBA32_UINT;
		cells.description.width = cells.description.height = 32;
		cells.description.depth_or_layers = 16;
		cells.description.levels = 1;
		cells.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		vk.lights_block = &block;
		vk.lights_cells = &cells;
		memset(&dynamic, 0, sizeof(dynamic));
		dynamic.viewport[2] = 64;
		dynamic.viewport[3] = 48;
		dynamic.depth_range[1] = 1.0f;
		memset(&draw, 0, sizeof(draw));
		draw.pipeline = (VkPipeline)(uintptr_t)93;
		draw.dynamic = &dynamic;
		draw.count = 3;
		draw.topology = XGPU_VULKAN_TOPOLOGY_TRIANGLES;
		draw.dynamic_lights = 1;
		for (stage = 0; stage < 4; stage++)
			draw.textures[stage].kind = XGPU_VULKAN_IMAGE_2D;
		/* (the view's pass, the chain's head, lit) */
		discards_clear(DISCARDS_ALL);
		vk.slot->descriptor_pools = &post_pool;
		vk.slot->descriptor_pool_count = 1;
		xgpu_vulkan_draw(&draw);
		assert(vk.chain_head && block.used == vk.serial && cells.used == vk.serial);
		assert(cells.aside_read == vk.chain_number);
		/* (the cells written again: not put aside before the draw) */
		xgpu_vulkan_image_upload(&cells, 0, 0, 0, 0, 0, 32, 32, 16, grid, 0, 0);
		assert(aside_cells_copies == 1 && !vk.chain_head);
		/* (the block written again while a chain is open: in order, which
		ends it, not renamed) */
		vk.ring_head = 0;
		xgpu_vulkan_set_targets(&color_target, &depth_target, 0);
		xgpu_vulkan_draw(&draw);
		aside_draw(&aside_target, NULL);
		assert(vk.chain_head);
		xgpu_vulkan_buffer_write(&block, 0, data, sizeof(data), 0);
		assert(!vk.statistics.buffers_renamed && !aside_buffers_made && !vk.spare_count);
		assert(block.buffer == bound && block.ordered == vk.serial);
		assert(aside_copy_destination == bound && aside_copy_commands != DISCARDS_EARLY);
		assert(!vk.chain_head);
		xgpu_vulkan_fence();
		vk.lights_block = NULL;
		vk.lights_cells = NULL;
		vk.slot = NULL;
	}

	/* on with discards, unless HALO_VULKAN_ASIDE=0 */
	discards_reset(0);
	vkGetPhysicalDeviceProperties2 = discards_properties;
	setenv("HALO_VULKAN_DISCARDS", "1", 1);
	discards_choose();
	assert(vk.aside);
	setenv("HALO_VULKAN_ASIDE", "0", 1);
	discards_choose();
	assert(vk.deferred && !vk.aside);
	unsetenv("HALO_VULKAN_ASIDE");
	setenv("HALO_VULKAN_DISCARDS", "0", 1);
	discards_choose();
	assert(!vk.aside);
	unsetenv("HALO_VULKAN_DISCARDS");
	free(aside_target.attachments);
	free(aside_other.attachments);
	free(aside_other_depth.attachments);
	memset(&aside_target, 0, sizeof(aside_target));
	memset(&aside_other, 0, sizeof(aside_other));
	memset(&aside_other_depth, 0, sizeof(aside_other_depth));
}

int main(void)
{
	static const int32_t full[1][4] = { { 0, 0, 64, 48 } };
	static const int32_t partial[1][4] = { { 4, 5, 10, 12 } };
	static const int32_t beyond[2][4] = { { 70, 0, 1, 1 }, { -20, -20, 100, 100 } };
	static const float color[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
	uint32_t all = XGPU_VULKAN_CLEAR_COLOR | XGPU_VULKAN_CLEAR_CHANNELS |
		XGPU_VULKAN_CLEAR_DEPTH | XGPU_VULKAN_CLEAR_STENCIL;

	capabilities_test();
	shaders_test();
	vertex_input_test();
	uploads_test();
	dynamic_lights_test();
	submissions_test();
	texture_sets_test();
	post_test();
	panini_post_test();
	masked_clear_test();
	window_test();
	rotate_test();
	pipeline_cache_test();
	local_read_test();
	hdr_pipeline_test();
	hdr_window_test();
	hdr_post_test();
	half_test();
	discards_test();
	aside_test();
	reset();
	xgpu_vulkan_clear(full, 0, all, color, 0.75f, 0x123);
	assert(!begins && !clears);

	reset();
	xgpu_vulkan_clear(full, 1, all, color, 0.75f, 0x123);
	assert(begins == 1 && !clears);
	assert(began_color.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && began_depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(began_stencil.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(began_color.clearValue.color.float32[0] == 0.25f);
	assert(began_depth.clearValue.depthStencil.depth == CLEARED(0.75f));
	assert(began_stencil.clearValue.depthStencil.stencil == 0x23);
	assert(began_color.storeOp == VK_ATTACHMENT_STORE_OP_STORE && began_depth.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(began_stencil.storeOp == VK_ATTACHMENT_STORE_OP_STORE);
	assert(vk.statistics.attachment_clears == 3 && vk.statistics.attachment_loads == 0);
	/* A later clear must remain in the pass: it cannot overwrite an
	earlier draw's initial attachment values retroactively. */
	xgpu_vulkan_clear(full, 1, all, color, 0.5f, 7);
	assert(begins == 1 && clears == 1);
	assert(cleared[1].clearValue.depthStencil.depth == CLEARED(0.5f));

	reset();
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_DEPTH, color, 0.75f, 0);
	assert(begins == 1 && !clears);
	assert(began_depth.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR);
	assert(began_color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_stencil.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(vk.statistics.attachment_clears == 1 && vk.statistics.attachment_loads == 2);

	reset();
	xgpu_vulkan_clear(full, 1, XGPU_VULKAN_CLEAR_STENCIL, color, 0.75f, 19);
	assert(began_stencil.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR && began_depth.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(began_stencil.clearValue.depthStencil.stencil == 19 && !clears);

	reset();
	xgpu_vulkan_clear(partial, 1, all, color, 0.75f, 3);
	assert(begins == 1 && clears == 1);
	assert(began_color.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD && began_depth.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(began_stencil.loadOp == VK_ATTACHMENT_LOAD_OP_LOAD);
	assert(cleared_rect.rect.offset.x == 4 && cleared_rect.rect.extent.height == 12);
	assert(vk.statistics.attachment_clears == 0 && vk.statistics.attachment_loads == 3);

	reset();
	xgpu_vulkan_clear(beyond, 2, all, color, 0.75f, 3);
	assert(begins == 1 && !clears && vk.statistics.attachment_clears == 3);

	reset();
	xgpu_vulkan_set_target_views(&color_target, &depth_target, 2, 2);
	xgpu_vulkan_clear(full, 1, all, color, 0.75f, 3);
	assert(began.viewMask == 3 && began.layerCount == 1);
	assert(last_view.viewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY);
	assert(last_view.subresourceRange.baseArrayLayer == 2 && last_view.subresourceRange.layerCount == 2);

	reset();
	depth_target.description.width = 32;
	depth_target.description.height = 24;
	xgpu_vulkan_clear((const int32_t[][4]){ { 0, 0, 32, 24 } }, 1, all, color, 0.75f, 3);
	assert(began.renderArea.extent.width == 32 && began.renderArea.extent.height == 24);
	assert(vk.statistics.attachment_clears == 3 && !clears);

	reset();
	xgpu_vulkan_set_target_views(&color_target, &depth_target, 3, 2);
	xgpu_vulkan_clear(full, 1, all, color, 0.75f, 3);
	assert(!begins && !clears);

	reset();
	{
		struct xgpu_vulkan_image *array = calloc(1, sizeof(*array));
		struct xgpu_vulkan_texture textures[4] = { 0 };
		VkDescriptorPool pool = (VkDescriptorPool)(uintptr_t)7;
		VkImageView first_layer;
		unsigned int index, views_freed = 0;

		assert(array);
		array->description = color_target.description;
		array->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		array->sampled = (VkImageView)(uintptr_t)21;
		textures[0].image = array;
		textures[0].kind = XGPU_VULKAN_IMAGE_2D;
		recording.descriptor_pools = &pool;
		recording.descriptor_pool_count = 1;
		assert(texture_set(textures));
		first_layer = written_images[0].imageView;
		assert(created_views == 1 && first_layer == array->sampled_2d);
		assert(first_layer != array->sampled && last_view.viewType == VK_IMAGE_VIEW_TYPE_2D);
		assert(last_view.subresourceRange.baseArrayLayer == 0 && last_view.subresourceRange.layerCount == 1);
		assert(texture_set(textures) && created_views == 1);
		textures[0].kind = XGPU_VULKAN_IMAGE_2D_ARRAY;
		assert(texture_set(textures) && written_images[0].imageView == array->sampled && created_views == 1);
		xgpu_vulkan_image_destroy(array);
		for (index = 0; index < vk.garbage_count; index++)
		{
			if (vk.garbage[index].kind == _garbage_view)
			{
				views_freed++;
				assert(vk.garbage[index].serial == 11);
			}
		}
		assert(views_freed == 2);
	}
	reset();
	{
		struct xgpu_vulkan_image *loading_image = calloc(1, sizeof(*loading_image));
		struct loading_presentation_frame frame = { 0 };
		struct query_frame queries = { 0 };
		unsigned int index;
		float first_color;

		assert(loading_image);
		loading_image->description = color_target.description;
		loading_image->description.type = XGPU_VULKAN_IMAGE_2D;
		loading_image->description.format = XGPU_VULKAN_FORMAT_BGRA8;
		loading_image->description.depth_or_layers = 1;
		loading_image->aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		vk.loading_image = loading_image;
		xgpu_vulkan_set_target_views(&color_target, &depth_target, 2, 2);
		xgpu_vulkan_clear(full, 1, all, color, 0.75f, 3);
		/* Simulate a game query spanning a preparation wait. Loading
		passes close it once, never allocate more query segments, and
		leave the game's target, test and frame number intact. */
		vk.test_active = vk.test_query_open = 1;
		vk.test_query_count = 1;
		vk.test_number = 37;
		vk.queries = &queries;
		ended_queries = 0;
		frame.active = 1;
		loading_presentation_build(&frame);
		first_color = frame.rectangles[0].color[0];
		for (index = 0; index < 128; index++)
		{
			frame.elapsed_ns = (uint64_t)index * 16666667;
			loading_presentation_build(&frame);
			assert(xgpu_vulkan_loading_draw(&frame, 64, 48, XGPU_VULKAN_FORMAT_BGRA8) == loading_image);
			assert(vk.color == &color_target && vk.depth == &depth_target && vk.layer == 2 && vk.views == 2);
			assert(vk.test_active && !vk.test_query_open && vk.test_query_count == 1 && vk.test_number == 37);
			assert(!vk.statistics.frames && !vk.pass_active && queries.count == 0 && ended_queries == 1);
		}
		assert(first_color != frame.rectangles[0].color[0]);
		frame.active = 0;
		assert(!xgpu_vulkan_loading_draw(&frame, 0, 0, 0) && !vk.loading_image);
		vk.queries = NULL;
	}
	reset();
	puts("Vulkan passes, image views, vertex bindings, synchronization, anti-aliasing, the window and display.hdr passed");
	return 0;
}
