/*
XGPU_VULKAN_RT.C

display.ambient_occlusion "ray_traced" in the Vulkan backend (docs/world-ao.md,
"Hardware ray queries"; xgpu_stream.h's WORLD_AO_MESH and _INSTANCES): the
ray-traced scene's acceleration structures, which the occlusion's pass reads
with ray queries (VK_KHR_ray_query, VK_KHR_acceleration_structure) where the
device has them (the core asks for them as it makes the device and starts
this file: xgpu_vulkan_rt_create).

- Each mesh is a bottom-level structure, built once (fast to trace): its
  triangles kept as they come and built at the next frame's scene with any
  others that came, in one call, then let go of. They live in large buffers
  of their own (a map's few hundred, never one allocation each), let go of
  together with the meshes (id 0, a map's end).
- The frame's instances are a top-level structure, built again (fast to
  build) before the pass that first reads them: three in turn, each written
  only once the GPU is done with the work that read it. An instance whose
  mesh never came, or whose owner, kind or transform is out of bounds, is
  left out. Its flags: the structure's triangles opaque and either way
  facing; an object's not opaque (so that the pass leaves a pixel's own
  out) and facing front where wound counterclockwise seen from outside
  (HALO_VULKAN_RT_FLIP=1 turns that, to check it).
- The builds go in the early commands (xgpu_vulkan_early_commands), before
  the submission's draws, which the main commands' first barrier orders
  after them; they never end a pass.
- Until a scene is built, the draws' occlusion set names an empty one.

Render thread alone, as the core.
*/

#include "xgpu_stream.h"
#include "xgpu_vulkan.h"
#include "host.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

enum
{
	/* the scenes in turn */
	RT_SCENES = 3,
	/* the meshes' storage's buffers, each at least so large */
	RT_CHUNKS = 64,
};

#define RT_CHUNK_BYTES ((VkDeviceSize)16 << 20)
/* an acceleration structure's offset in its buffer (the specification's) */
#define RT_STRUCTURE_ALIGNMENT 256u

/* ---------- structures */

/* a buffer of its own memory, with its device address (mapped where it is
host-visible) */
struct rt_buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceAddress address;
	VkDeviceSize size;
	void *mapped;
};

struct rt_mesh
{
	VkAccelerationStructureKHR structure;
	VkDeviceAddress address;
	uint32_t count;
	/* its triangles until it is built (NULL after) */
	float *triangles;
	int built;
};

struct rt_scene
{
	VkAccelerationStructureKHR structure;
	struct rt_buffer storage, instances;
	uint32_t capacity;
	/* the last submission that reads it */
	uint64_t used;
	int built;
};

struct rt_garbage
{
	uint64_t serial;
	struct rt_buffer buffer;
	VkAccelerationStructureKHR structure;
};

/* ---------- globals */

static struct
{
	int ready, flip;
	VkDevice device;
	VkPhysicalDeviceMemoryProperties memory;
	uint32_t scratch_alignment;
	PFN_vkCreateAccelerationStructureKHR create;
	PFN_vkDestroyAccelerationStructureKHR destroy;
	PFN_vkGetAccelerationStructureBuildSizesKHR sizes;
	PFN_vkCmdBuildAccelerationStructuresKHR build;
	PFN_vkGetAccelerationStructureDeviceAddressKHR structure_address;
	PFN_vkGetBufferDeviceAddress buffer_address;
	PFN_vkCreateBuffer create_buffer;
	PFN_vkDestroyBuffer destroy_buffer;
	PFN_vkGetBufferMemoryRequirements requirements;
	PFN_vkAllocateMemory allocate;
	PFN_vkFreeMemory free_memory;
	PFN_vkBindBufferMemory bind;
	PFN_vkMapMemory map;
	PFN_vkCmdPipelineBarrier barrier;
	/* the meshes (by id) and those not yet built */
	struct rt_mesh *meshes[XGPU_STREAM_WORLD_AO_MESHES];
	uint32_t pending;
	/* the meshes' storage: its buffers, and what of the last is taken */
	struct rt_buffer chunks[RT_CHUNKS];
	uint32_t chunk_count;
	VkDeviceSize chunk_used;
	/* the instances given (the frame's), and whether they were built since */
	struct xgpu_world_ao_instance *given;
	uint32_t given_count, given_capacity;
	int given_new;
	/* the scenes in turn (current: the latest built, -1 none) and the empty
	one */
	struct rt_scene scenes[RT_SCENES], empty;
	int current;
	struct rt_garbage *garbage;
	uint32_t garbage_count, garbage_capacity;
	int failed_said;
} rt;

/* ---------- memory */

static int rt_memory_type(uint32_t bits, VkMemoryPropertyFlags wanted)
{
	uint32_t index;

	for (index = 0; index < rt.memory.memoryTypeCount; index++)
	{
		if ((bits & (1u << index)) && (rt.memory.memoryTypes[index].propertyFlags & wanted) == wanted)
			return (int)index;
	}
	return -1;
}

/* a buffer of size bytes for usage (and its device address), in memory of
its own, host-visible or the device's; 0 if it cannot be */
static int rt_buffer_make(VkDeviceSize size, VkBufferUsageFlags usage, int host, struct rt_buffer *buffer)
{
	VkBufferCreateInfo info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateFlagsInfo flags;
	VkMemoryAllocateInfo allocate;
	VkBufferDeviceAddressInfo address;
	int type;

	memset(buffer, 0, sizeof(*buffer));
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (rt.create_buffer(rt.device, &info, NULL, &buffer->buffer) != VK_SUCCESS)
		return 0;
	rt.requirements(rt.device, buffer->buffer, &requirements);
	type = rt_memory_type(requirements.memoryTypeBits, host ?
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	if (type < 0 && !host)
		type = rt_memory_type(requirements.memoryTypeBits, 0);
	memset(&flags, 0, sizeof(flags));
	flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	memset(&allocate, 0, sizeof(allocate));
	allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocate.pNext = &flags;
	allocate.allocationSize = requirements.size;
	allocate.memoryTypeIndex = (uint32_t)type;
	if (type < 0 || rt.allocate(rt.device, &allocate, NULL, &buffer->memory) != VK_SUCCESS)
	{
		rt.destroy_buffer(rt.device, buffer->buffer, NULL);
		memset(buffer, 0, sizeof(*buffer));
		return 0;
	}
	if (rt.bind(rt.device, buffer->buffer, buffer->memory, 0) != VK_SUCCESS ||
		(host && rt.map(rt.device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped) != VK_SUCCESS))
	{
		rt.destroy_buffer(rt.device, buffer->buffer, NULL);
		rt.free_memory(rt.device, buffer->memory, NULL);
		memset(buffer, 0, sizeof(*buffer));
		return 0;
	}
	memset(&address, 0, sizeof(address));
	address.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	address.buffer = buffer->buffer;
	buffer->address = rt.buffer_address(rt.device, &address);
	buffer->size = size;
	return 1;
}

static void rt_buffer_free(struct rt_buffer *buffer)
{
	if (buffer->buffer)
		rt.destroy_buffer(rt.device, buffer->buffer, NULL);
	if (buffer->memory)
		rt.free_memory(rt.device, buffer->memory, NULL);
	memset(buffer, 0, sizeof(*buffer));
}

/* ---------- garbage: let go of once the GPU is past the work recorded now */

static void rt_garbage_collect(void)
{
	uint64_t done = xgpu_vulkan_completed();
	uint32_t index = 0;

	while (index < rt.garbage_count)
	{
		struct rt_garbage *item = &rt.garbage[index];

		if (item->serial > done)
		{
			index++;
			continue;
		}
		if (item->structure)
			rt.destroy(rt.device, item->structure, NULL);
		rt_buffer_free(&item->buffer);
		rt.garbage[index] = rt.garbage[--rt.garbage_count];
	}
}

static void rt_garbage_add(const struct rt_buffer *buffer, VkAccelerationStructureKHR structure)
{
	struct rt_garbage *item;

	if (rt.garbage_count == rt.garbage_capacity)
	{
		uint32_t capacity = rt.garbage_capacity ? rt.garbage_capacity * 2 : 64;
		struct rt_garbage *grown = realloc(rt.garbage, capacity * sizeof(*grown));

		/* (none: waited for, then let go of now) */
		if (!grown)
		{
			xgpu_vulkan_wait(xgpu_vulkan_fence());
			if (structure)
				rt.destroy(rt.device, structure, NULL);
			if (buffer)
			{
				struct rt_buffer copy = *buffer;

				rt_buffer_free(&copy);
			}
			return;
		}
		rt.garbage = grown;
		rt.garbage_capacity = capacity;
	}
	item = &rt.garbage[rt.garbage_count++];
	memset(item, 0, sizeof(*item));
	item->serial = xgpu_vulkan_serial();
	if (buffer)
		item->buffer = *buffer;
	item->structure = structure;
}

/* ---------- the meshes */

/* room for a mesh's structure in the meshes' storage: its buffer and
offset; 0 if none can be had */
static int rt_storage_take(VkDeviceSize size, VkBuffer *buffer, VkDeviceSize *offset)
{
	VkDeviceSize at = (rt.chunk_used + RT_STRUCTURE_ALIGNMENT - 1) & ~(VkDeviceSize)(RT_STRUCTURE_ALIGNMENT - 1);

	if (!rt.chunk_count || at + size > rt.chunks[rt.chunk_count - 1].size)
	{
		if (rt.chunk_count == RT_CHUNKS || !rt_buffer_make(size > RT_CHUNK_BYTES ? size : RT_CHUNK_BYTES,
			VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, 0, &rt.chunks[rt.chunk_count]))
			return 0;
		rt.chunk_count++;
		at = 0;
	}
	*buffer = rt.chunks[rt.chunk_count - 1].buffer;
	*offset = at;
	rt.chunk_used = at + size;
	return 1;
}

static void rt_meshes_release(void)
{
	uint32_t id;

	for (id = 0; id < XGPU_STREAM_WORLD_AO_MESHES; id++)
	{
		struct rt_mesh *mesh = rt.meshes[id];

		if (!mesh)
			continue;
		if (mesh->structure)
			rt_garbage_add(NULL, mesh->structure);
		free(mesh->triangles);
		free(mesh);
		rt.meshes[id] = NULL;
	}
	for (id = 0; id < rt.chunk_count; id++)
		rt_garbage_add(&rt.chunks[id], VK_NULL_HANDLE);
	memset(rt.chunks, 0, sizeof(rt.chunks));
	rt.chunk_count = 0;
	rt.chunk_used = 0;
	rt.pending = 0;
}

void xgpu_vulkan_rt_mesh(uint32_t id, uint32_t count, const float *triangles)
{
	struct rt_mesh *mesh;

	if (!rt.ready)
		return;
	rt_garbage_collect();
	if (!id)
	{
		rt_meshes_release();
		/* (the instances named them) */
		rt.given_count = 0;
		rt.given_new = 1;
		return;
	}
	if (id >= XGPU_STREAM_WORLD_AO_MESHES || !count || count > XGPU_STREAM_WORLD_AO_MESH_TRIANGLES || !triangles ||
		rt.meshes[id])
		return;
	mesh = calloc(1, sizeof(*mesh));
	if (!mesh)
		return;
	mesh->triangles = malloc((size_t)count * 36u);
	if (!mesh->triangles)
	{
		free(mesh);
		return;
	}
	memcpy(mesh->triangles, triangles, (size_t)count * 36u);
	mesh->count = count;
	rt.meshes[id] = mesh;
	rt.pending++;
}

/* the meshes not yet built, built in one call into the early commands (a
mesh that cannot be is left out from then on) */
static void rt_meshes_build(VkCommandBuffer commands)
{
	VkAccelerationStructureBuildGeometryInfoKHR *infos;
	VkAccelerationStructureGeometryKHR *geometries;
	VkAccelerationStructureBuildRangeInfoKHR *ranges;
	const VkAccelerationStructureBuildRangeInfoKHR **range_pointers;
	struct rt_buffer vertices, scratch;
	VkDeviceSize vertex_bytes = 0, scratch_bytes = 0, *scratch_offsets;
	uint32_t id, count = 0, index;
	VkMemoryBarrier barrier;

	if (!rt.pending)
		return;
	infos = calloc(rt.pending, sizeof(*infos));
	geometries = calloc(rt.pending, sizeof(*geometries));
	ranges = calloc(rt.pending, sizeof(*ranges));
	range_pointers = calloc(rt.pending, sizeof(*range_pointers));
	scratch_offsets = calloc(rt.pending, sizeof(*scratch_offsets));
	memset(&vertices, 0, sizeof(vertices));
	memset(&scratch, 0, sizeof(scratch));
	if (!infos || !geometries || !ranges || !range_pointers || !scratch_offsets)
		goto done;
	for (id = 1; id < XGPU_STREAM_WORLD_AO_MESHES; id++)
	{
		if (rt.meshes[id] && !rt.meshes[id]->built)
			vertex_bytes += ((VkDeviceSize)rt.meshes[id]->count * 36u + 15u) & ~(VkDeviceSize)15u;
	}
	if (!rt_buffer_make(vertex_bytes, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 1, &vertices))
		goto done;
	vertex_bytes = 0;
	for (id = 1; id < XGPU_STREAM_WORLD_AO_MESHES && count < rt.pending; id++)
	{
		struct rt_mesh *mesh = rt.meshes[id];
		VkAccelerationStructureBuildSizesInfoKHR sizes;
		VkAccelerationStructureCreateInfoKHR create;
		VkAccelerationStructureDeviceAddressInfoKHR address;
		VkAccelerationStructureGeometryKHR *geometry = &geometries[count];
		VkAccelerationStructureBuildGeometryInfoKHR *info = &infos[count];

		if (!mesh || mesh->built)
			continue;
		/* (left out from now on, whether it is built or not) */
		mesh->built = 1;
		memcpy((unsigned char *)vertices.mapped + vertex_bytes, mesh->triangles, (size_t)mesh->count * 36u);
		geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry->geometry.triangles.vertexData.deviceAddress = vertices.address + vertex_bytes;
		geometry->geometry.triangles.vertexStride = 12;
		geometry->geometry.triangles.maxVertex = mesh->count * 3 - 1;
		geometry->geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
		geometry->flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
		vertex_bytes += ((VkDeviceSize)mesh->count * 36u + 15u) & ~(VkDeviceSize)15u;
		info->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		info->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		info->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		info->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		info->geometryCount = 1;
		info->pGeometries = geometry;
		memset(&sizes, 0, sizeof(sizes));
		sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		rt.sizes(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, info, &mesh->count, &sizes);
		memset(&create, 0, sizeof(create));
		create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		create.size = sizes.accelerationStructureSize;
		create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		if (!rt_storage_take(sizes.accelerationStructureSize, &create.buffer, &create.offset) ||
			rt.create(rt.device, &create, NULL, &mesh->structure) != VK_SUCCESS)
		{
			mesh->structure = VK_NULL_HANDLE;
			continue;
		}
		memset(&address, 0, sizeof(address));
		address.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		address.accelerationStructure = mesh->structure;
		mesh->address = rt.structure_address(rt.device, &address);
		info->dstAccelerationStructure = mesh->structure;
		scratch_offsets[count] = scratch_bytes;
		scratch_bytes += (sizes.buildScratchSize + rt.scratch_alignment - 1) & ~(VkDeviceSize)(rt.scratch_alignment - 1);
		ranges[count].primitiveCount = mesh->count;
		range_pointers[count] = &ranges[count];
		count++;
	}
	if (!count)
		goto done;
	if (!rt_buffer_make(scratch_bytes + rt.scratch_alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, &scratch))
	{
		/* (none built: their structures let go of, never named) */
		for (index = 0; index < count; index++)
		{
			for (id = 1; id < XGPU_STREAM_WORLD_AO_MESHES; id++)
			{
				if (rt.meshes[id] && rt.meshes[id]->structure == infos[index].dstAccelerationStructure)
				{
					rt_garbage_add(NULL, rt.meshes[id]->structure);
					rt.meshes[id]->structure = VK_NULL_HANDLE;
				}
			}
		}
		goto done;
	}
	for (index = 0; index < count; index++)
	{
		VkDeviceAddress at = (scratch.address + rt.scratch_alignment - 1) & ~(VkDeviceAddress)(rt.scratch_alignment - 1);

		infos[index].scratchData.deviceAddress = at + scratch_offsets[index];
	}
	rt.build(commands, count, infos, range_pointers);
	/* (the meshes built before the scene that names them) */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	rt.barrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL);
done:
	/* (every mesh tried: their triangles let go of) */
	for (id = 1; id < XGPU_STREAM_WORLD_AO_MESHES; id++)
	{
		if (rt.meshes[id] && rt.meshes[id]->built && rt.meshes[id]->triangles)
		{
			free(rt.meshes[id]->triangles);
			rt.meshes[id]->triangles = NULL;
		}
	}
	rt.pending = 0;
	if (vertices.buffer)
		rt_garbage_add(&vertices, VK_NULL_HANDLE);
	if (scratch.buffer)
		rt_garbage_add(&scratch, VK_NULL_HANDLE);
	free(infos);
	free(geometries);
	free(ranges);
	free(range_pointers);
	free(scratch_offsets);
}

/* ---------- the scenes */

void xgpu_vulkan_rt_instances(const struct xgpu_world_ao_instance *instances, uint32_t count)
{
	if (!rt.ready)
		return;
	if (count > rt.given_capacity)
	{
		struct xgpu_world_ao_instance *grown = realloc(rt.given, count * sizeof(*grown));

		if (!grown)
			return;
		rt.given = grown;
		rt.given_capacity = count;
	}
	if (count)
		memcpy(rt.given, instances, count * sizeof(*instances));
	rt.given_count = count;
	rt.given_new = 1;
}

/* a scene's structure, with room for count instances (made again where it
has not: the old let go of once its work is done); 0 if it cannot be */
static int rt_scene_room(struct rt_scene *scene, uint32_t count)
{
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR info;
	VkAccelerationStructureBuildSizesInfoKHR sizes;
	VkAccelerationStructureCreateInfoKHR create;
	uint32_t capacity;

	if (scene->structure && count <= scene->capacity)
		return 1;
	if (scene->structure)
		rt_garbage_add(&scene->storage, scene->structure);
	if (scene->instances.buffer)
		rt_garbage_add(&scene->instances, VK_NULL_HANDLE);
	memset(&scene->storage, 0, sizeof(scene->storage));
	memset(&scene->instances, 0, sizeof(scene->instances));
	scene->structure = VK_NULL_HANDLE;
	scene->built = 0;
	for (capacity = 64; capacity < count; capacity *= 2)
		;
	memset(&geometry, 0, sizeof(geometry));
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info.geometryCount = 1;
	info.pGeometries = &geometry;
	memset(&sizes, 0, sizeof(sizes));
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	rt.sizes(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &capacity, &sizes);
	/* (its scratch after its structure, in the same buffer) */
	if (!rt_buffer_make(((sizes.accelerationStructureSize + rt.scratch_alignment - 1) &
		~(VkDeviceSize)(rt.scratch_alignment - 1)) + sizes.buildScratchSize + rt.scratch_alignment,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, &scene->storage))
		return 0;
	if (!rt_buffer_make((VkDeviceSize)capacity * sizeof(VkAccelerationStructureInstanceKHR),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 1, &scene->instances))
	{
		rt_buffer_free(&scene->storage);
		return 0;
	}
	memset(&create, 0, sizeof(create));
	create.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	create.buffer = scene->storage.buffer;
	create.size = sizes.accelerationStructureSize;
	create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	if (rt.create(rt.device, &create, NULL, &scene->structure) != VK_SUCCESS)
	{
		rt_buffer_free(&scene->storage);
		rt_buffer_free(&scene->instances);
		scene->structure = VK_NULL_HANDLE;
		return 0;
	}
	scene->capacity = capacity;
	return 1;
}

/* an instance as the device takes it (0: left out) */
static int rt_instance_of(const struct xgpu_world_ao_instance *given, VkAccelerationStructureInstanceKHR *instance)
{
	const struct rt_mesh *mesh = given->mesh < XGPU_STREAM_WORLD_AO_MESHES ? rt.meshes[given->mesh] : NULL;
	int row, column;

	if (!mesh || !mesh->structure || given->owner >= XGPU_WORLD_AO_INSTANCE_OWNERS ||
		(given->kind != XGPU_WORLD_AO_INSTANCE_STRUCTURE && given->kind != XGPU_WORLD_AO_INSTANCE_OBJECT))
		return 0;
	memset(instance, 0, sizeof(*instance));
	for (row = 0; row < 3; row++)
	{
		for (column = 0; column < 4; column++)
		{
			float value = given->transform[row][column];

			if (!isfinite(value) || fabsf(value) > 1.0e6f)
				return 0;
			instance->transform.matrix[row][column] = value;
		}
	}
	instance->instanceCustomIndex = given->owner;
	instance->mask = given->kind;
	instance->instanceShaderBindingTableRecordOffset = 0;
	instance->flags = given->kind == XGPU_WORLD_AO_INSTANCE_STRUCTURE ?
		VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR | VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR :
		VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR | (rt.flip ? VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR : 0);
	instance->accelerationStructureReference = mesh->address;
	return 1;
}

/* a scene's instances built into its structure, in the commands */
static void rt_scene_build(VkCommandBuffer commands, struct rt_scene *scene, uint32_t count)
{
	VkAccelerationStructureGeometryKHR geometry;
	VkAccelerationStructureBuildGeometryInfoKHR info;
	VkAccelerationStructureBuildRangeInfoKHR range;
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	VkDeviceSize structure_bytes;
	VkMemoryBarrier barrier;
	VkAccelerationStructureBuildSizesInfoKHR sizes;

	memset(&geometry, 0, sizeof(geometry));
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.data.deviceAddress = scene->instances.address;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
	info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	info.geometryCount = 1;
	info.pGeometries = &geometry;
	info.dstAccelerationStructure = scene->structure;
	memset(&sizes, 0, sizeof(sizes));
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	rt.sizes(rt.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &scene->capacity, &sizes);
	structure_bytes = (sizes.accelerationStructureSize + rt.scratch_alignment - 1) &
		~(VkDeviceSize)(rt.scratch_alignment - 1);
	info.scratchData.deviceAddress = ((scene->storage.address + rt.scratch_alignment - 1) &
		~(VkDeviceAddress)(rt.scratch_alignment - 1)) + structure_bytes;
	memset(&range, 0, sizeof(range));
	range.primitiveCount = count;
	rt.build(commands, 1, &info, &ranges);
	/* (built before the pass's ray queries read it) */
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	rt.barrier(commands, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0,
		NULL, 0, NULL);
	scene->built = 1;
}

/* the empty scene, made and built once (0 if it cannot be) */
static int rt_empty_scene(void)
{
	if (rt.empty.built)
		return 1;
	if (!rt.empty.structure && !rt_scene_room(&rt.empty, 0))
		return 0;
	rt_scene_build(xgpu_vulkan_early_commands(), &rt.empty, 0);
	return 1;
}

int xgpu_vulkan_rt_build(void)
{
	uint64_t serial = xgpu_vulkan_serial(), done;
	VkCommandBuffer commands;
	struct rt_scene *scene = NULL;
	VkAccelerationStructureInstanceKHR *written;
	uint32_t index, count = 0;
	int which;

	if (!rt.ready)
		return 0;
	if (!rt.given_new)
		return rt.current >= 0;
	rt_garbage_collect();
	commands = xgpu_vulkan_early_commands();
	rt_meshes_build(commands);
	rt.given_new = 0;
	rt.current = -1;
	xgpu_vulkan_world_ao_touch();
	if (!rt.given_count)
		return 0;
	/* (a scene no work recorded so far reads, the GPU done with it; the
	oldest waited for where none is) */
	done = xgpu_vulkan_completed();
	for (which = 0; which < RT_SCENES && !scene; which++)
	{
		if (rt.scenes[which].used < serial && rt.scenes[which].used <= done)
			scene = &rt.scenes[which];
	}
	if (!scene)
	{
		struct rt_scene *oldest = &rt.scenes[0];

		for (which = 1; which < RT_SCENES; which++)
		{
			if (rt.scenes[which].used < oldest->used)
				oldest = &rt.scenes[which];
		}
		if (oldest->used >= serial)
			return 0;
		xgpu_vulkan_wait(oldest->used);
		scene = oldest;
	}
	if (!rt_scene_room(scene, rt.given_count))
	{
		if (!rt.failed_said)
			host_logf(HOST_LOG_WARN, "vulkan: display.ambient_occlusion \"ray_traced\"'s scene cannot be made: drawn as \"world\"");
		rt.failed_said = 1;
		return 0;
	}
	written = scene->instances.mapped;
	for (index = 0; index < rt.given_count; index++)
		count += (uint32_t)rt_instance_of(&rt.given[index], &written[count]);
	rt_scene_build(commands, scene, count);
	scene->used = serial;
	rt.current = (int)(scene - rt.scenes);
	return 1;
}

VkAccelerationStructureKHR xgpu_vulkan_rt_scene(void)
{
	struct rt_scene *scene;

	if (!rt.ready)
		return VK_NULL_HANDLE;
	scene = rt.current >= 0 ? &rt.scenes[rt.current] : &rt.empty;
	if (scene == &rt.empty && !rt_empty_scene())
		return VK_NULL_HANDLE;
	scene->used = xgpu_vulkan_serial();
	return scene->structure;
}

int xgpu_vulkan_rt_ready(void)
{
	return rt.ready;
}

/* ---------- the start and end */

int xgpu_vulkan_rt_create(const struct xgpu_vulkan_rt_device *device)
{
	memset(&rt, 0, sizeof(rt));
	rt.device = device->device;
	rt.memory = device->memory;
	rt.scratch_alignment = device->scratch_alignment ? device->scratch_alignment : 256u;
	rt.current = -1;
	rt.flip = getenv("HALO_VULKAN_RT_FLIP") && atoi(getenv("HALO_VULKAN_RT_FLIP")) != 0;
#define LOAD(name, field) rt.field = (PFN_##name)device->get_device_proc_addr(device->device, #name);
	LOAD(vkCreateAccelerationStructureKHR, create)
	LOAD(vkDestroyAccelerationStructureKHR, destroy)
	LOAD(vkGetAccelerationStructureBuildSizesKHR, sizes)
	LOAD(vkCmdBuildAccelerationStructuresKHR, build)
	LOAD(vkGetAccelerationStructureDeviceAddressKHR, structure_address)
	LOAD(vkGetBufferDeviceAddress, buffer_address)
	LOAD(vkCreateBuffer, create_buffer)
	LOAD(vkDestroyBuffer, destroy_buffer)
	LOAD(vkGetBufferMemoryRequirements, requirements)
	LOAD(vkAllocateMemory, allocate)
	LOAD(vkFreeMemory, free_memory)
	LOAD(vkBindBufferMemory, bind)
	LOAD(vkMapMemory, map)
	LOAD(vkCmdPipelineBarrier, barrier)
#undef LOAD
	rt.ready = rt.create && rt.destroy && rt.sizes && rt.build && rt.structure_address && rt.buffer_address &&
		rt.create_buffer && rt.destroy_buffer && rt.requirements && rt.allocate && rt.free_memory && rt.bind && rt.map &&
		rt.barrier;
	return rt.ready;
}

void xgpu_vulkan_rt_destroy(void)
{
	int which;

	if (!rt.ready)
		return;
	rt_meshes_release();
	for (which = 0; which < RT_SCENES; which++)
	{
		if (rt.scenes[which].structure)
			rt.destroy(rt.device, rt.scenes[which].structure, NULL);
		rt_buffer_free(&rt.scenes[which].storage);
		rt_buffer_free(&rt.scenes[which].instances);
	}
	if (rt.empty.structure)
		rt.destroy(rt.device, rt.empty.structure, NULL);
	rt_buffer_free(&rt.empty.storage);
	rt_buffer_free(&rt.empty.instances);
	/* (the caller has waited for the device) */
	while (rt.garbage_count)
	{
		struct rt_garbage *item = &rt.garbage[--rt.garbage_count];

		if (item->structure)
			rt.destroy(rt.device, item->structure, NULL);
		rt_buffer_free(&item->buffer);
	}
	free(rt.garbage);
	free(rt.given);
	memset(&rt, 0, sizeof(rt));
}
