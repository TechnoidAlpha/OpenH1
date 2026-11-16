/* GPU timestamps against a fake Vulkan queue: a query pair stays owned
until its last submission completes, without a GPU wait or an early reset. */
#include "../vulkan/xgpu_vulkan.c"
/* (the masked clears' shaders, which the core draws with) */
#include "../vulkan/xgpu_vulkan_post_shaders.c"
#include <assert.h>

/* display.low_latency's pacing (xgpu_vulkan_pacing.c), with the command
stream's and the host's calls it makes: never started here (the fake
device has neither present wait nor display timing) */
#include "../vulkan/xgpu_vulkan_pacing.c"

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

static uint64_t completed, start_value, end_value;
static unsigned creates, resets, writes, reads;
static int available = 1;
static VkResult create_result = VK_SUCCESS;
static VkResult read_result = VK_SUCCESS;

/* (debug.pass_times: never) */
int host_pass_times;

void host_logf(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

void host_fatal(const char *format, ...)
{
	(void)format;
	abort();
}

static VKAPI_ATTR VkResult VKAPI_CALL create_queries(VkDevice device, const VkQueryPoolCreateInfo *info,
	const VkAllocationCallbacks *allocator, VkQueryPool *pool)
{
	(void)device;
	(void)allocator;
	assert(info->queryType == VK_QUERY_TYPE_TIMESTAMP && info->queryCount == GPU_TIME_FRAMES * 2);
	creates++;
	if (create_result == VK_SUCCESS)
		*pool = (VkQueryPool)(uintptr_t)1;
	return create_result;
}

static VKAPI_ATTR void VKAPI_CALL reset_queries(VkDevice device, VkQueryPool pool, uint32_t first, uint32_t count)
{
	(void)device;
	assert(pool == vk.gpu_time_pool && count == 2 && !(first & 1));
	assert(!vk.gpu_times[first / 2].used);
	resets++;
}

static VKAPI_ATTR void VKAPI_CALL timestamp(VkCommandBuffer commands, VkPipelineStageFlagBits stage,
	VkQueryPool pool, uint32_t query)
{
	assert(pool == vk.gpu_time_pool);
	assert(stage == ((query & 1) ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT));
	assert(commands == ((query & 1) ? vk.slot->main : vk.slot->early));
	assert(!vk.pass_active);
	writes++;
}

static VKAPI_ATTR VkResult VKAPI_CALL counter(VkDevice device, VkSemaphore semaphore, uint64_t *value)
{
	(void)device;
	(void)semaphore;
	*value = completed;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL query_results(VkDevice device, VkQueryPool pool, uint32_t first,
	uint32_t count, size_t size, void *data, VkDeviceSize stride, VkQueryResultFlags flags)
{
	uint64_t (*values)[2] = data;

	(void)device;
	assert(pool == vk.gpu_time_pool && count == 2 && !(first & 1));
	assert(size == 4 * sizeof(uint64_t) && stride == 2 * sizeof(uint64_t));
	assert(flags == (VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
	assert(vk.gpu_times[first / 2].serial <= completed);
	reads++;
	values[0][0] = start_value;
	values[1][0] = end_value;
	values[0][1] = values[1][1] = available;
	return read_result;
}

static VKAPI_ATTR void VKAPI_CALL pipeline_barrier(VkCommandBuffer commands, VkPipelineStageFlags source,
	VkPipelineStageFlags destination, VkDependencyFlags dependencies, uint32_t memory_count,
	const VkMemoryBarrier *memory, uint32_t buffer_count, const VkBufferMemoryBarrier *buffers,
	uint32_t image_count, const VkImageMemoryBarrier *images)
{
	(void)commands;
	(void)source;
	(void)destination;
	(void)dependencies;
	(void)memory_count;
	(void)memory;
	(void)buffer_count;
	(void)buffers;
	(void)image_count;
	(void)images;
}

static VKAPI_ATTR VkResult VKAPI_CALL reset_commands(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
	(void)device;
	(void)pool;
	(void)flags;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL begin_commands(VkCommandBuffer commands, const VkCommandBufferBeginInfo *info)
{
	(void)commands;
	assert(info->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL end_commands(VkCommandBuffer commands)
{
	(void)commands;
	return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL queue_submit(VkQueue queue, uint32_t count, const VkSubmitInfo *info, VkFence fence)
{
	(void)queue;
	(void)fence;
	assert(count == 1 && info->commandBufferCount >= 1 && info->commandBufferCount <= 2);
	return VK_SUCCESS;
}

static void initialize(void)
{
	memset(&vk, 0, sizeof(vk));
	vk.capabilities.gpu_timestamps = 1;
	vk.timestamp_bits = 64;
	vk.properties.limits.timestampPeriod = 2.0f;
	vk.recording = vk.early_recording = 1;
	vk.serial = 1;
	vk.slot = &vk.slots[0];
	vk.slot->early = (VkCommandBuffer)(uintptr_t)2;
	vk.slot->main = (VkCommandBuffer)(uintptr_t)3;
	vkCreateQueryPool = create_queries;
	vkResetQueryPool = reset_queries;
	vkCmdWriteTimestamp = timestamp;
	vkGetSemaphoreCounterValue = counter;
	vkGetQueryPoolResults = query_results;
	vkCmdPipelineBarrier = pipeline_barrier;
	vkResetCommandPool = reset_commands;
	vkBeginCommandBuffer = begin_commands;
	vkEndCommandBuffer = end_commands;
	vkQueueSubmit = queue_submit;
	completed = creates = resets = writes = reads = 0;
	available = 1;
	create_result = read_result = VK_SUCCESS;
	start_value = 100;
	end_value = 500100;
}

static void begin_work(void)
{
	xgpu_vulkan_gpu_time_begin();
	early_commands();
}

int main(void)
{
	double milliseconds = -1.0;
	unsigned index;

	/* VR arms the next sample before frame_end retires this frame. Neither
	an empty submission nor visibility maintenance starts the next interval. */
	for (index = 0; index < 2; index++)
	{
		initialize();
		if (index)
		{
			vk.queries = &vk.query_frames[0];
			vk.queries->counter_count = 1;
		}
		xgpu_vulkan_gpu_time_begin();
		xgpu_vulkan_frame_end(NULL, 1);
		assert(!creates && !writes && vk.gpu_time_armed && !vk.gpu_time_active);
		early_commands();
		assert(creates == 1 && writes == 1 && !vk.gpu_time_armed && vk.gpu_time_active);
	}
	/* An interval already started may span a frame-end submission. */
	initialize();
	begin_work();
	xgpu_vulkan_frame_end(NULL, 1);
	assert(writes == 1 && vk.gpu_time_active && !vk.gpu_time_armed);

	initialize();
	vk.capabilities.gpu_timestamps = 0;
	begin_work();
	xgpu_vulkan_gpu_time_end();
	assert(!xgpu_vulkan_gpu_time_read(&milliseconds) && !creates && !writes);

	initialize();
	xgpu_vulkan_gpu_time_begin();
	assert(!creates && !writes && vk.gpu_time_armed);
	begin_work();
	begin_work();
	assert(creates == 1 && resets == 1 && writes == 1);
	/* The frame can span submissions: only the final serial releases it. */
	vk.serial = 3;
	xgpu_vulkan_gpu_time_end();
	completed = 2;
	assert(!xgpu_vulkan_gpu_time_read(&milliseconds) && !reads && resets == 1);
	begin_work();
	assert(vk.gpu_time_current == 1 && resets == 2);
	xgpu_vulkan_gpu_time_end();
	completed = 3;
	read_result = VK_NOT_READY;
	assert(!xgpu_vulkan_gpu_time_read(&milliseconds) && vk.gpu_times[0].used);
	read_result = VK_SUCCESS;
	available = 0;
	assert(!xgpu_vulkan_gpu_time_read(&milliseconds) && vk.gpu_times[0].used);
	available = 1;
	assert(xgpu_vulkan_gpu_time_read(&milliseconds) && milliseconds == 1.0);
	assert(!vk.gpu_times[0].used && vk.gpu_times[1].used);
	assert(xgpu_vulkan_gpu_time_read(&milliseconds));
	assert(!xgpu_vulkan_gpu_time_read(&milliseconds));

	/* Counter rollover uses the queue family's valid timestamp bits. */
	begin_work();
	xgpu_vulkan_gpu_time_end();
	vk.timestamp_bits = 8;
	start_value = 250;
	end_value = 10;
	assert(xgpu_vulkan_gpu_time_read(&milliseconds));
	assert(milliseconds > 0.0000319 && milliseconds < 0.0000321);
	begin_work();
	xgpu_vulkan_gpu_time_end();
	vk.timestamp_bits = 64;
	start_value = UINT64_MAX - 15;
	end_value = 0;
	assert(xgpu_vulkan_gpu_time_read(&milliseconds));
	assert(milliseconds > 0.0000319 && milliseconds < 0.0000321);

	initialize();
	for (index = 0; index < GPU_TIME_FRAMES; index++)
	{
		begin_work();
		xgpu_vulkan_gpu_time_end();
		vk.serial++;
	}
	begin_work();
	xgpu_vulkan_gpu_time_end();
	assert(resets == GPU_TIME_FRAMES && writes == GPU_TIME_FRAMES * 2 && !vk.gpu_time_active);
	assert(!xgpu_vulkan_gpu_time_read(&milliseconds) && !reads);
	completed = 1;
	assert(xgpu_vulkan_gpu_time_read(&milliseconds));
	begin_work();
	assert(resets == GPU_TIME_FRAMES + 1 && vk.gpu_time_current == 0);

	initialize();
	create_result = VK_ERROR_OUT_OF_HOST_MEMORY;
	begin_work();
	begin_work();
	assert(creates == 1 && !resets && !writes && vk.gpu_time_failed);
	puts("ok");
	return 0;
}
