/*
STRUCTURE_BSP_CACHE.C

The map's structure bsps, each kept in memory as it was read from the map
and checked (tag_validate_structure_bsp), so that a switch to one (a load
zone, switch_bsp) copies it back where it loads instead of reading the file
and checking it again: the Xbox read the bsp from its disc or hard disk
there, in the frame, and the check is the port's. Every bsp of a map is
read and checked as the map loads, under its loading screen
(scenario_structure_bsps_read_ahead, cache_files.c), so that no switch reads
or checks anything; the bytes put back are the ones checked, so what the
game reads of a bsp is the same either way.

The copies are let go of with the map's scenario (scenario_unload), and are
kept only for the map file they were read from (its checksum). They take at
most STRUCTURE_BSP_CACHE_BYTES (a campaign level's bsps are some tens of
megabytes); a bsp without room, or memory, is read and checked as it loads,
as before.
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "scenario/scenario_definitions.h"
#include "structures/structure_bsp_definitions.h"
#include "structure_bsp_cache.h"
#include "structures/structure_visibility.h"
#include "structure_culling.h"

#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);
/* (memory_watch.c's, or the host's) */
void memory_watch_prepare_write(void *address, unsigned long size);

#define STRUCTURE_BSP_CACHE_BYTES (128UL * 1024 * 1024)
#define MAXIMUM_STRUCTURE_BSP_CACHE_ENTRIES 32

struct structure_bsp_cache_entry
{
	long file_offset;
	long file_size;
	void *base_address;
	void *bytes;
};

static struct
{
	unsigned long checksum;
	unsigned long bytes;
	short count;
	struct structure_bsp_cache_entry entries[MAXIMUM_STRUCTURE_BSP_CACHE_ENTRIES];
	/* the bytes of the region bsps load in that a bsp has been read or put
	in since the region was last filled (0xCD), when they are known: only
	those are filled again */
	boolean written_known;
	unsigned long written_first, written_end;
} structure_bsp_cache;

static void structure_bsp_cache_fill(
	unsigned long first,
	unsigned long end)
{
	if (first < end)
		memset((void *)first, 0xCD, end - first);
}

static struct structure_bsp_cache_entry *structure_bsp_cache_find(
	struct scenario_structure_bsp_reference const *reference)
{
	short index;

	if (structure_bsp_cache.count && structure_bsp_cache.checksum != cache_files_get_checksum())
		structure_bsp_cache_dispose();
	for (index = 0; index < structure_bsp_cache.count; index++)
	{
		struct structure_bsp_cache_entry *entry = &structure_bsp_cache.entries[index];

		if (entry->file_offset == reference->file_offset && entry->file_size == reference->file_size &&
			entry->base_address == reference->base_address)
		{
			return entry;
		}
	}
	return NULL;
}

boolean structure_bsp_cache_restore(
	struct scenario_structure_bsp_reference const *reference,
	void *region,
	unsigned long region_size)
{
	struct structure_bsp_cache_entry *entry = structure_bsp_cache_find(reference);
	unsigned long first = (unsigned long)reference->base_address;
	unsigned long end = first + (unsigned long)reference->file_size;
	unsigned long region_first = (unsigned long)region;
	unsigned long region_end = region_first + region_size;

	if (!entry || (region && (first < region_first || end > region_end)))
	{
		/* (the caller fills the region and reads the bsp into it) */
		structure_bsp_cache.written_known = region != NULL;
		structure_bsp_cache.written_first = first;
		structure_bsp_cache.written_end = end;
		return FALSE;
	}
	if (region)
	{
		/* (the region outside the bsp as the read leaves it: filled where
		another bsp was) */
		if (structure_bsp_cache.written_known)
		{
			region_first = MAX(region_first, structure_bsp_cache.written_first);
			region_end = MIN(region_end, structure_bsp_cache.written_end);
		}
		/* (the renderer's watched pages there let go of at once, not each
		as the copy first writes it, a fault each) */
		memory_watch_prepare_write((void *)MIN(region_first, first), MAX(region_end, end) - MIN(region_first, first));
		structure_bsp_cache_fill(region_first, MIN(first, region_end));
		structure_bsp_cache_fill(MAX(end, region_first), region_end);
		structure_bsp_cache.written_known = TRUE;
		structure_bsp_cache.written_first = first;
		structure_bsp_cache.written_end = end;
	}
	else
	{
		memory_watch_prepare_write(reference->base_address, (unsigned long)entry->file_size);
	}
	memcpy(reference->base_address, entry->bytes, (size_t)entry->file_size);
	return TRUE;
}

boolean structure_bsp_cache_room(
	void)
{
	return structure_bsp_cache.count < MAXIMUM_STRUCTURE_BSP_CACHE_ENTRIES &&
		structure_bsp_cache.bytes < STRUCTURE_BSP_CACHE_BYTES;
}

void structure_bsp_cache_keep(
	struct scenario_structure_bsp_reference const *reference)
{
	struct structure_bsp_cache_entry *entry;
	void *bytes;

	if (reference->file_size <= 0 || structure_bsp_cache_find(reference) || !structure_bsp_cache_room() ||
		(unsigned long)reference->file_size > STRUCTURE_BSP_CACHE_BYTES - structure_bsp_cache.bytes)
	{
		return;
	}
	bytes = malloc((size_t)reference->file_size);
	if (!bytes)
		return;
	memcpy(bytes, reference->base_address, (size_t)reference->file_size);
	if (!structure_bsp_cache.count)
		structure_bsp_cache.checksum = cache_files_get_checksum();
	entry = &structure_bsp_cache.entries[structure_bsp_cache.count++];
	entry->file_offset = reference->file_offset;
	entry->file_size = reference->file_size;
	entry->base_address = reference->base_address;
	entry->bytes = bytes;
	structure_bsp_cache.bytes += (unsigned long)reference->file_size;
}

long structure_bsp_cache_copy_offset(
	struct scenario_structure_bsp_reference const *reference)
{
	struct structure_bsp_cache_entry const *entry = structure_bsp_cache_find(reference);

	return entry ? (long)((byte *)entry->bytes - (byte *)entry->base_address) : 0;
}

/* the copy's bytes standing for count elements of size bytes at address,
where the bsp loads; NULL for any not in the copy */
static void const *structure_bsp_cache_at(
	struct structure_bsp_cache_entry const *entry,
	void const *address,
	long count,
	unsigned long size)
{
	unsigned long offset = (unsigned long)address - (unsigned long)entry->base_address;

	if (count < 0 || (unsigned long)address < (unsigned long)entry->base_address ||
		offset > (unsigned long)entry->file_size ||
		(count && size > ((unsigned long)entry->file_size - offset) / (unsigned long)count))
	{
		return NULL;
	}
	return (byte const *)entry->bytes + offset;
}

boolean structure_bsp_cache_has_mirrors(
	void)
{
	unsigned long size = structure_visibility_port_cluster_size();
	short index;

	for (index = 0; index < structure_bsp_cache.count; index++)
	{
		struct structure_bsp_cache_entry const *entry = &structure_bsp_cache.entries[index];
		void *const *header = structure_bsp_cache_at(entry, entry->base_address, 1, sizeof(void *));
		struct structure_bsp const *structure_bsp =
			header ? structure_bsp_cache_at(entry, *header, 1, sizeof(struct structure_bsp)) : NULL;
		byte const *clusters = structure_bsp ? structure_bsp_cache_at(entry, structure_bsp->clusters.address,
			structure_bsp->clusters.count, size) : NULL;
		long cluster_index;

		for (cluster_index = 0; clusters && cluster_index < structure_bsp->clusters.count; cluster_index++)
		{
			if (structure_visibility_port_cluster_mirror_count(clusters + cluster_index * size) > 0)
				return TRUE;
		}
	}
	return FALSE;
}

void structure_bsp_cache_materials(
	void (*visit)(void *context, struct structure_lightmap const *lightmap, struct structure_material const *material),
	void *context)
{
	short index;

	for (index = 0; index < structure_bsp_cache.count; index++)
	{
		struct structure_bsp_cache_entry const *entry = &structure_bsp_cache.entries[index];
		/* (the bsp's header: where its structure is, first) */
		void *const *header = structure_bsp_cache_at(entry, entry->base_address, 1, sizeof(void *));
		struct structure_bsp const *structure_bsp =
			header ? structure_bsp_cache_at(entry, *header, 1, sizeof(struct structure_bsp)) : NULL;
		struct structure_lightmap const *lightmaps;
		long lightmap_index;

		if (!structure_bsp)
			continue;
		lightmaps = structure_bsp_cache_at(entry, structure_bsp->lightmaps.address, structure_bsp->lightmaps.count,
			sizeof(struct structure_lightmap));
		for (lightmap_index = 0; lightmaps && lightmap_index < structure_bsp->lightmaps.count; lightmap_index++)
		{
			struct structure_lightmap const *lightmap = &lightmaps[lightmap_index];
			struct structure_material const *materials = structure_bsp_cache_at(entry, lightmap->materials.address,
				lightmap->materials.count, sizeof(struct structure_material));
			long material_index;

			for (material_index = 0; materials && material_index < lightmap->materials.count; material_index++)
				visit(context, lightmap, &materials[material_index]);
		}
	}
}

void structure_bsp_cache_dispose(
	void)
{
	short index;

	/* (the buckets being built from the copies: structure_culling.c) */
	structure_culling_wait();
	if (structure_bsp_cache.count)
	{
		platform_log("structure bsps: %d kept, %lu bytes, let go of", structure_bsp_cache.count,
			structure_bsp_cache.bytes);
	}
	for (index = 0; index < structure_bsp_cache.count; index++)
		free(structure_bsp_cache.entries[index].bytes);
	memset(&structure_bsp_cache, 0, sizeof(structure_bsp_cache));
}
