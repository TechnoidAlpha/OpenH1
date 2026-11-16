/*
XGPU_VULKAN_TEXTURES.C

The Vulkan backend's cache of the Xbox's textures and the high-res
replacements' images (xgpu_vulkan_textures.h), as the Metal backend's
(port/macos/xgpu_metal_textures.c) has them, with the same decoding
(xbox_texture_decode.c, built into the host beside a stand-in for the
platform header: xgpu_vulkan_platform.h).

An Xbox texture is found by its Data, Format and Size words (and a
palettized one's palette, and the channels a Custom Edition map's texels are
sampled from); its texels are read from the guest's memory where they are.
DXT textures are uploaded as they are (BC1 to BC3), where
the device has them, a volume's decoded; where it has not (most Android
phones' GPUs), ASTC where it has that (below), else decoded; every other
format is decoded to BGRA. The pages it was read from are write-protected (the write tracking,
host_memory_watch.c), and the texture is read again once any of them has
been written: the check is a comparison of the tracking's serial while no
page has been, and a look at the texture's pages' generations otherwise. The
host reads a frame after the game drew it: a texture whose memory the game
has let go of since (a map's, as the next loads) is not read (its pages are
no longer the guest's).

An image (a high-res replacement: port/linux/src/xgpu_images.h) is found by
its number and made again when its serial changes; its levels are made from
its first.

debug.texture_dump_directory writes each 2D Xbox texture's first level as it
is read, decoded to BGRA (texture_dump).

A texture no stage has drawn for half a minute of time is let go.

ASTC (HALO_ASTC, the Android app's build; display.astc_textures,
xgpu_astc_chosen: on a device that samples ASTC 4x4 and not BC): a 2D or
cube DXT texture's blocks are transcoded to ASTC 4x4 blocks as it is read
(port/linux/astc/xgpu_astc.h), a quarter of the bytes its decoded texels
were. A texture with blocks transcoding cannot make exactly (DXT3 and DXT5
alpha that varies inside a block, mostly) is drawn decoded while a worker
encodes those (or finds them in memory or on disk: the app's cache folder,
xgpu_vulkan_textures_astc_folder), and made again as ASTC once it has them
(xgpu_vulkan_textures_frame_end), unless the worker finds it better left
decoded. The Metal backend's (port/macos/xgpu_metal_textures.c) does the
same.

The VR mode's effects draw an effect's bitmap with its thickness
(xgpu_vulkan_effects.h), baked from its first level the first time an
effect is drawn with it (as the map is prepared: its enumeration draws
every effect's bitmaps) and kept with it, made again with its texels. A
texture with a thickness is not let go while the guest keeps its texels.
*/

#include "xgpu.h"
#include "xgpu_backend.h"
#include "xgpu_stream.h"
#include "host.h"
#include "host_render_record.h"
#include "xgpu_vulkan.h"
#include "xgpu_vulkan_effects.h"
#include "xgpu_vulkan_textures.h"
#ifdef HALO_ASTC
#include "xgpu_astc.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#define BUCKET_COUNT 4096
/* the textures are looked over every SWEEP_SECONDS, and those no stage
has drawn since the look SWEEP_COUNT before are let go: 30 to 40 seconds
after they were last drawn. Counted
in time, not frames: 1800 frames, half a minute at 60 frames a second, was
15 seconds on the Steam Frame at 120 */
#define SWEEP_SECONDS 10
#define IDLE_SECONDS 30
#define SWEEP_COUNT (IDLE_SECONDS / SWEEP_SECONDS)
/* the copies of one texture for its palettes (the game cycles some through
a few) */
#define PALETTE_VARIANTS 8
/* D3DFMT_P8 */
#define FORMAT_PALETTIZED 0x0b

/* the Xbox's contiguous memory, where the textures are */
#define CONTIGUOUS_BASE HALO_GUEST_WINDOW_BASE
#define CONTIGUOUS_SIZE HALO_GUEST_WINDOW_SIZE
/* the ASTC blocks encoded, kept in the cache folder to this many bytes (all
the stock maps' are some 60 MB) */
#define ASTC_CACHE_BYTES (256u << 20)

enum
{
	_entry_xbox,
	_entry_image,
};

struct entry
{
	struct entry *next;
	uint8_t kind;
	/* an Xbox texture's words, its palette's hash and its channels; an
	image's number (data) and serial (format_word) */
	uint32_t data, format_word, size_word, palette_hash, channels;
	struct xgpu_vulkan_image *image;
	struct xgpu_texture_description description;
	/* (an Xbox texture's) the range read, and its pages' newest generation
	as of the tracking's serial watched_serial */
	uint32_t address, size;
	uint32_t generation, watched_serial;
	uint64_t last_used;
	/* its DXT texels decoded (the device has no BC formats); ASTC (HALO_ASTC),
	and the job encoding the blocks it is waiting for, drawn decoded
	meanwhile */
	int decoded, astc;
	struct xgpu_astc_job *astc_job;
	/* (an Xbox texture's) its thickness for the VR mode's effects, baked
	from its texels of that generation, or not to be (failed: the
	generation it failed at) */
	struct xgpu_vulkan_image *thickness;
	uint32_t thickness_generation, thickness_failed;
};

static struct
{
	struct entry *buckets[BUCKET_COUNT];
	uint64_t frame;
	/* the frames of the last looks over the textures, a look apart, the
	looks made, and the time (seconds) the next is due */
	uint64_t sweep_frames[SWEEP_COUNT];
	unsigned long sweeps;
	double next_sweep;
	/* the decoded texels of the largest level so far */
	DWORD *converted;
	size_t converted_size;
	struct xgpu_vulkan_texture_statistics statistics;
	/* debug.texture_dump_directory (NULL: none), and the textures written
	into it */
	const char *dump_folder;
	unsigned long dumped;
	/* counts the thickness images destroyed (made again, their texture let
	go of or read with another palette): an image looked up before is still
	the one to draw with while it is the same
	(xgpu_vulkan_textures_thickness_epoch) */
	uint32_t thickness_epoch;
	/* ASTC: to be used (-1 not decided yet), the cache's folder; the DXT
	blocks of the largest texture so far, its ASTC blocks and which are to
	be encoded; the textures waiting for the encoder */
	int astc;
	char astc_folder[1024];
	uint8_t *dxt_blocks, *astc_blocks, *encode;
	size_t blocks_size;
	uint32_t astc_waiting;
} cache = { .astc = -1 };

/* the entry's thickness destroyed, and not to be taken as failed */
static void thickness_forget(struct entry *entry)
{
	if (entry->thickness)
	{
		xgpu_vulkan_image_destroy(entry->thickness);
		entry->thickness = NULL;
		cache.thickness_epoch++;
	}
	entry->thickness_generation = entry->thickness_failed = 0;
}

static uint32_t bucket_index(uint32_t data, uint32_t format_word, uint32_t size_word)
{
	uint32_t hash = data * 2654435761u ^ format_word * 2246822519u ^ size_word * 3266489917u;

	return (hash >> 20) % BUCKET_COUNT;
}

static uint32_t palette_hash(const uint32_t *palette)
{
	uint32_t hash = 2166136261u;
	int index;

	if (!palette)
		return 0;
	for (index = 0; index < 256; index++)
		hash = (hash ^ palette[index]) * 16777619u;
	return hash ? hash : 1;
}

static DWORD *converted_get(size_t texels)
{
	if (texels > cache.converted_size)
	{
		DWORD *grown = realloc(cache.converted, texels * sizeof(DWORD));

		if (!grown)
			return NULL;
		cache.converted = grown;
		cache.converted_size = texels;
	}
	return cache.converted;
}

static uint32_t levels_most(unsigned long width, unsigned long height, unsigned long depth)
{
	unsigned long largest = width > height ? width : height;
	uint32_t levels = 1;

	if (depth > largest)
		largest = depth;
	while (largest > 1)
	{
		largest >>= 1;
		levels++;
	}
	return levels;
}

/* ---------- the textures written out (debug.texture_dump_directory) */

/* a 2D texture's first level as it is read (texels: its BGRA, decoded):
tex<number>_fmt<format>_<width>x<height>.tga, 32-bit, rows from the top,
numbered in the order they are read; texels as stored (a Custom Edition
map's channel orders are the sampler's) */
static void texture_dump(const struct xgpu_texture_description *description, const DWORD *texels)
{
	unsigned long width = description->width, height = description->height;
	unsigned char header[18];
	char path[1024];
	FILE *file;

	snprintf(path, sizeof(path), "%s/tex%05lu_fmt%02x_%lux%lu.tga", cache.dump_folder, cache.dumped++,
		(unsigned)description->format, width, height);
	file = fopen(path, "wb");
	if (!file)
		return;
	memset(header, 0, sizeof(header));
	header[2] = 2;
	header[12] = (unsigned char)width;
	header[13] = (unsigned char)(width >> 8);
	header[14] = (unsigned char)height;
	header[15] = (unsigned char)(height >> 8);
	header[16] = 32;
	header[17] = 0x28;
	fwrite(header, 1, sizeof(header), file);
	fwrite(texels, 4, width * height, file);
	fclose(file);
}

void xgpu_vulkan_textures_dump(const char *folder)
{
	cache.dump_folder = folder && *folder ? folder : NULL;
}

/* ---------- Xbox textures */

/* the texture's image, made for its description */
static struct xgpu_vulkan_image *xbox_texture_make(struct entry *entry)
{
	const struct xgpu_texture_description *description = &entry->description;
	struct xgpu_vulkan_image_description made;
	struct xgpu_vulkan_capabilities capabilities;
	uint32_t most = levels_most(description->width, description->height, description->depth);

	xgpu_vulkan_capabilities(&capabilities);
	memset(&made, 0, sizeof(made));
	made.type = description->cube_map ? XGPU_VULKAN_IMAGE_CUBE : description->depth > 1 ? XGPU_VULKAN_IMAGE_3D :
		XGPU_VULKAN_IMAGE_2D;
	/* (DXT as it is, but for a volume's, which is decoded, and on a device
	without BC formats: ASTC, or decoded) */
	entry->decoded = description->compressed && (description->depth > 1 || !capabilities.block_compression ||
		cache.astc == 1);
	if (description->compressed && !entry->decoded)
		made.format = description->format == 0x0c ? XGPU_VULKAN_FORMAT_DXT1 : description->format == 0x0e ?
			XGPU_VULKAN_FORMAT_DXT3 : XGPU_VULKAN_FORMAT_DXT5;
	else if (entry->astc)
		made.format = XGPU_VULKAN_FORMAT_ASTC;
	else
		made.format = XGPU_VULKAN_FORMAT_BGRA8;
	made.usage = XGPU_VULKAN_USAGE_SAMPLED;
	made.width = (uint32_t)description->width;
	made.height = (uint32_t)description->height;
	made.depth_or_layers = (uint32_t)(description->depth > 1 ? description->depth : 1);
	made.levels = (uint32_t)(description->levels < most ? description->levels : most);
	if (!made.levels)
		made.levels = 1;
	made.channels = entry->channels;
	return xgpu_vulkan_image_create(&made);
}

/* ---------- ASTC */

void xgpu_vulkan_textures_astc_folder(const char *folder)
{
	snprintf(cache.astc_folder, sizeof(cache.astc_folder), "%s", folder ? folder : "");
}

#ifdef HALO_ASTC

/* whether DXT textures are ASTC here (decided at the first, the device
there): display.astc_textures, and the device's formats */
static int astc_wanted(void)
{
	if (cache.astc < 0)
	{
		struct xgpu_vulkan_capabilities capabilities;

		xgpu_vulkan_capabilities(&capabilities);
		cache.astc = xgpu_astc_chosen(host_astc_textures, capabilities.block_compression, capabilities.astc);
		if (cache.astc)
			xgpu_astc_cache_initialize(*cache.astc_folder ? cache.astc_folder : NULL, ASTC_CACHE_BYTES);
		host_logf(HOST_LOG_INFO, "vulkan: DXT textures %s (display.astc_textures \"%s\"; the device samples BC %s, "
			"ASTC %s)", cache.astc ? "as ASTC 4x4" : capabilities.block_compression ? "as BC" : "decoded",
			host_astc_textures, capabilities.block_compression ? "yes" : "no", capabilities.astc ? "yes" : "no");
	}
	return cache.astc;
}

/* XGPU_ASTC_DXT* of a DXT format */
static int astc_kind(DWORD format)
{
	return format == 0x0c ? XGPU_ASTC_DXT1 : format == 0x0e ? XGPU_ASTC_DXT3 : XGPU_ASTC_DXT5;
}

/* the texture's DXT blocks, its faces' levels one after another, into
cache.dxt_blocks (with room for as many ASTC blocks in cache.astc_blocks,
and their flags); their count, 0 if there is no room */
static uint32_t astc_blocks_read(const struct entry *entry)
{
	const struct xgpu_texture_description *description = &entry->description;
	const unsigned char *base = host_pointer(entry->address);
	unsigned long faces = description->cube_map ? 6 : 1, face_size = xgpu_texture_face_size(description);
	unsigned long levels = xgpu_vulkan_image_description(entry->image)->levels, face, level;
	unsigned block_bytes = xgpu_astc_dxt_block_bytes(astc_kind(description->format));
	uint32_t count = 0, at = 0;

	for (level = 0; level < levels; level++)
		count += (uint32_t)((xgpu_texture_level_dimension(description->width, level) + 3) / 4 *
			((xgpu_texture_level_dimension(description->height, level) + 3) / 4));
	count *= (uint32_t)faces;
	if (!count || (uint64_t)count * block_bytes > entry->size)
		return 0;
	if ((size_t)count * XGPU_ASTC_BLOCK_BYTES > cache.blocks_size)
	{
		uint8_t *dxt = realloc(cache.dxt_blocks, (size_t)count * XGPU_ASTC_BLOCK_BYTES);
		uint8_t *astc = dxt ? realloc(cache.astc_blocks, (size_t)count * XGPU_ASTC_BLOCK_BYTES) : NULL;
		uint8_t *encode = astc ? realloc(cache.encode, count) : NULL;

		if (dxt)
			cache.dxt_blocks = dxt;
		if (astc)
			cache.astc_blocks = astc;
		if (!encode)
			return 0;
		cache.encode = encode;
		cache.blocks_size = (size_t)count * XGPU_ASTC_BLOCK_BYTES;
	}
	for (face = 0; face < faces; face++)
	{
		for (level = 0; level < levels; level++)
		{
			uint32_t blocks = (uint32_t)((xgpu_texture_level_dimension(description->width, level) + 3) / 4 *
				((xgpu_texture_level_dimension(description->height, level) + 3) / 4));

			memcpy(cache.dxt_blocks + (size_t)at * block_bytes,
				base + face * face_size + xgpu_texture_level_offset(description, level), (size_t)blocks * block_bytes);
			at += blocks;
		}
	}
	return at;
}

/* the texture's ASTC blocks (as astc_blocks_read lays them) into it */
static void astc_upload(struct entry *entry, const uint8_t *blocks)
{
	const struct xgpu_texture_description *description = &entry->description;
	unsigned long faces = description->cube_map ? 6 : 1;
	unsigned long levels = xgpu_vulkan_image_description(entry->image)->levels, face, level;

	for (face = 0; face < faces; face++)
	{
		for (level = 0; level < levels; level++)
		{
			uint32_t width = (uint32_t)xgpu_texture_level_dimension(description->width, level);
			uint32_t height = (uint32_t)xgpu_texture_level_dimension(description->height, level);
			uint32_t row = (width + 3) / 4 * XGPU_ASTC_BLOCK_BYTES;

			xgpu_vulkan_image_upload(entry->image, (uint32_t)level, (uint32_t)face, 0, 0, 0, width, height, 1, blocks, row,
				0);
			blocks += (size_t)row * ((height + 3) / 4);
		}
	}
}

/* the entry's image made again as ASTC (astc) or decoded, if it is not so;
zero if it cannot be */
static int astc_format(struct entry *entry, int astc)
{
	if (entry->image && entry->astc == astc)
		return 1;
	xgpu_vulkan_image_destroy(entry->image);
	entry->astc = astc;
	entry->image = xbox_texture_make(entry);
	return entry->image != NULL;
}

/* a DXT texture read as ASTC: its blocks transcoded, and uploaded if they
all were, or if the workers have the rest in memory (the same texels read
before at another address); else it is made decoded (xbox_texture_upload
goes on: zero) and its blocks go to a worker, unless it is known to be
better decoded. Nonzero if it was uploaded, or has no image to upload
into. */
static int astc_read(struct entry *entry)
{
	int kind = astc_kind(entry->description.format), remembered = 0;
	uint32_t count, left = 0;

	if (entry->astc_job)
	{
		xgpu_astc_cancel(entry->astc_job);
		entry->astc_job = NULL;
		cache.astc_waiting--;
	}
	count = astc_blocks_read(entry);
	if (count)
	{
		left = xgpu_astc_transcode(kind, cache.dxt_blocks, count, cache.astc_blocks, cache.encode);
		if (left)
			remembered = xgpu_astc_remembered(kind, cache.dxt_blocks, count, cache.astc_blocks, cache.encode, left);
	}
	if (count && (!left || remembered > 0))
	{
		if (!astc_format(entry, 1))
			return 1;
		astc_upload(entry, cache.astc_blocks);
		return 1;
	}
	if (count && !remembered)
	{
		entry->astc_job = xgpu_astc_submit(kind, cache.dxt_blocks, count, entry);
		if (entry->astc_job)
			cache.astc_waiting++;
	}
	return !astc_format(entry, 0);
}

#endif

/* the workers' finished jobs: their textures made again as ASTC (the
texels the same: a job is cancelled when its texture is read again) */
static void astc_finished(void)
{
#ifdef HALO_ASTC
	struct xgpu_astc_job *job;

	if (!cache.astc_waiting)
		return;
	while ((job = xgpu_astc_finished()) != NULL)
	{
		struct entry *entry = xgpu_astc_job_owner(job);
		const uint8_t *blocks = xgpu_astc_job_blocks(job);

		entry->astc_job = NULL;
		cache.astc_waiting--;
		if (blocks && astc_format(entry, 1))
			astc_upload(entry, blocks);
		xgpu_astc_job_free(job);
	}
#endif
}

/* the entry's image let go (and its job, if it waits for one) */
static void image_release(struct entry *entry)
{
	if (entry->image)
		xgpu_vulkan_image_destroy(entry->image);
	entry->image = NULL;
#ifdef HALO_ASTC
	if (entry->astc_job)
	{
		xgpu_astc_cancel(entry->astc_job);
		entry->astc_job = NULL;
		cache.astc_waiting--;
	}
#endif
}

/* reads the texture's texels into it */
static void xbox_texture_upload(struct entry *entry, const D3DCOLOR *palette)
{
	const struct xgpu_texture_description *description = &entry->description;
	const unsigned char *base = host_pointer(entry->address);
	unsigned long faces = description->cube_map ? 6 : 1, face_size = xgpu_texture_face_size(description);
	unsigned long levels = xgpu_vulkan_image_description(entry->image)->levels, face, level;
	int blocks = description->compressed && !entry->decoded;
	/* (a 2D texture's first level written out) */
	int dump = cache.dump_folder && !description->cube_map && description->depth <= 1;

#ifdef HALO_ASTC
	if (description->compressed && description->depth <= 1 && cache.astc == 1 && astc_read(entry))
	{
		DWORD *converted;

		if (dump && (converted = converted_get(description->width * description->height)) != NULL)
		{
			xgpu_texture_decode_dxt(description, 0, base, converted);
			texture_dump(description, converted);
		}
		cache.statistics.bytes += entry->size;
		return;
	}
	/* (made again as it is drawn, decoded) */
	levels = xgpu_vulkan_image_description(entry->image)->levels;
	blocks = description->compressed && !entry->decoded;
#endif
	for (face = 0; face < faces; face++)
	{
		for (level = 0; level < levels; level++)
		{
			const unsigned char *source = base + face * face_size + xgpu_texture_level_offset(description, level);
			uint32_t width = (uint32_t)xgpu_texture_level_dimension(description->width, level);
			uint32_t height = (uint32_t)xgpu_texture_level_dimension(description->height, level);
			uint32_t depth = (uint32_t)xgpu_texture_level_dimension(description->depth, level);
			DWORD *converted;

			if (blocks)
			{
				uint32_t row = (uint32_t)xgpu_texture_level_pitch(description, level);

				xgpu_vulkan_image_upload(entry->image, (uint32_t)level, (uint32_t)face, 0, 0, 0, width, height, 1, source, row,
					0);
				/* (decoded to be written out) */
				if (dump && !level && (converted = converted_get((size_t)width * height)) != NULL)
				{
					xgpu_texture_decode_dxt(description, level, source, converted);
					texture_dump(description, converted);
				}
				continue;
			}
			converted = converted_get((size_t)width * height * depth);
			if (!converted)
				return;
			if (description->compressed)
				xgpu_texture_decode_dxt(description, level, source, converted);
			else if (!xgpu_texture_decode_level(description, level, source, palette, converted))
				return;
			xgpu_vulkan_image_upload(entry->image, (uint32_t)level, (uint32_t)face, 0, 0, 0, width, height, depth, converted,
				width * 4, width * height * 4);
			if (dump && !level)
				texture_dump(description, converted);
		}
	}
	cache.statistics.bytes += entry->size;
}

static struct entry *xbox_entry_find(const struct xgpu_stream_texture_stage *stage, const D3DCOLOR *palette)
{
	const uint32_t *header = host_pointer(stage->resource);
	uint32_t data, format_word = stage->resource_format, size_word = stage->resource_size;
	BOOL palettized = ((format_word & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == FORMAT_PALETTIZED;
	uint32_t hash = palettized ? palette_hash(palette) : 0;
	struct entry **bucket, *entry, *oldest = NULL;
	unsigned long variants = 0;

	if (!header || !header[1])
		return NULL;
	data = header[1];
	bucket = &cache.buckets[bucket_index(data, format_word, size_word)];
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->kind != _entry_xbox || entry->data != data || entry->format_word != format_word ||
			entry->size_word != size_word || entry->channels != stage->target)
			continue;
		if (entry->palette_hash == hash)
			return entry;
		variants++;
		if (!oldest || entry->last_used < oldest->last_used)
			oldest = entry;
	}
	if (variants >= PALETTE_VARIANTS)
	{
		/* (a palette that keeps changing: the stalest copy is read again) */
		oldest->palette_hash = hash;
		oldest->generation = 0;
		/* (its thickness was the other palette's: the generation read again
		may be the one it was baked at) */
		thickness_forget(oldest);
		return oldest;
	}
	entry = calloc(1, sizeof(*entry));
	if (!entry)
		return NULL;
	entry->kind = _entry_xbox;
	entry->data = data;
	entry->format_word = format_word;
	entry->size_word = size_word;
	entry->palette_hash = hash;
	entry->channels = stage->target;
	xgpu_texture_describe(format_word, size_word, &entry->description);
	entry->address = CONTIGUOUS_BASE | data;
#ifdef HALO_ASTC
	/* (made ASTC from the start where it most likely will be: most DXT1
	textures transcode exactly, most DXT3 and DXT5 ones wait for a worker) */
	entry->astc = entry->description.compressed && entry->description.depth <= 1 && astc_wanted() &&
		entry->description.format == 0x0c;
#endif
	/* (one larger than the device takes is never drawn, nor one outside the
	contiguous memory) */
	if (xgpu_texture_size_supported(&entry->description))
	{
		uint64_t size = (uint64_t)xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);

		if (size && entry->address >= CONTIGUOUS_BASE && (uint64_t)entry->address + size <=
			(uint64_t)CONTIGUOUS_BASE + CONTIGUOUS_SIZE)
			entry->size = (uint32_t)size;
	}
	if (entry->size)
		entry->image = xbox_texture_make(entry);
	if (!entry->size || !entry->image)
		host_logf(HOST_LOG_WARN, "vulkan: a %lux%lux%lu texture of format %02x at %08x is not drawn",
			entry->description.width, entry->description.height, entry->description.depth,
			(unsigned)entry->description.format, data);
	entry->next = *bucket;
	*bucket = entry;
	cache.statistics.made++;
	return entry;
}

static struct entry *xbox_entry_current(const struct xgpu_stream_texture_stage *stage, const D3DCOLOR **palette_out);

/* the guest's pages of a range are mapped and readable (looked at on whole
host pages, the extent's limits: no further). Without the base register
(the low 4 GB: the Steam Frame's and x86-64's guests) the memory layer keeps
no such account: the Xbox's contiguous window is mapped whole, and other
ranges are mapped if mincore finds their pages. The memory the guest's
heaps and image come from (host_memory.c's pools, its image's range, and
the window) is mapped from the start to the end, reserved where it is not
in use, so mincore would find every page of a range inside one of them
(host_low_owns, which takes no lock and makes no system call): each stage
of each draw asked the kernel, which was a noticeable part of the render
thread's time on the Steam Frame. */
static BOOL readable(uint32_t address, uint32_t size)
{
	uint64_t page = host_page_size();
	uint64_t first = address & ~(page - 1), last = ((uint64_t)address + size + page - 1) & ~(page - 1);

	if (!address || last > 0xffffffffu)
		return FALSE;
#ifdef HALO_GUEST_BASE
	{
		uint32_t start, end;

		return host_memory_readable_extent(address, size, (uint32_t)first, (uint32_t)last, &start, &end) != 0;
	}
#else
	if ((uint64_t)address >= CONTIGUOUS_BASE && (uint64_t)address + size <= (uint64_t)CONTIGUOUS_BASE + CONTIGUOUS_SIZE)
		return TRUE;
	if (host_low_owns(host_address((uint32_t)first), (size_t)(last - first)))
		return TRUE;
	{
		unsigned char residency[64];
		uint64_t at;

		/* (ENOMEM for a page that is not mapped) */
		for (at = first; at < last; at += page * sizeof(residency))
		{
			uint64_t length = last - at < page * sizeof(residency) ? last - at : page * sizeof(residency);

			if (mincore(host_pointer((uint32_t)at), (size_t)length, residency) != 0)
				return FALSE;
		}
		return TRUE;
	}
#endif
}

static struct xgpu_vulkan_image *xbox_texture_get(const struct xgpu_stream_texture_stage *stage)
{
	struct entry *entry = xbox_entry_current(stage, NULL);

	return entry ? entry->image : NULL;
}

/* the stage's texture's entry, its texels as the guest's memory has them now
(palette: its palette, NULL none) */
static struct entry *xbox_entry_current(const struct xgpu_stream_texture_stage *stage, const D3DCOLOR **palette_out)
{
	const D3DCOLOR *palette = readable(stage->palette, 256 * sizeof(D3DCOLOR)) ? host_pointer(stage->palette) : NULL;
	struct entry *entry;
	uint32_t serial, generation;

	if (palette_out)
		*palette_out = palette;
	if (!readable(stage->resource, 5 * sizeof(uint32_t)))
		return NULL;
	/* (what is read of the guest's memory, for a recording) */
	if (host_render_record_active)
	{
		host_render_record_note(stage->resource, 5 * sizeof(uint32_t));
		if (palette && ((stage->resource_format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == FORMAT_PALETTIZED)
			host_render_record_note(stage->palette, 256 * sizeof(D3DCOLOR));
	}
	entry = xbox_entry_find(stage, palette);
	if (!entry || !entry->image)
		return NULL;
	entry->last_used = cache.frame;
	serial = host_memory_watch_serial();
	if (entry->generation && entry->watched_serial == serial)
		return entry;
	generation = host_memory_watch_generation(entry->address, entry->size);
	if ((!entry->generation || generation > entry->generation) && readable(entry->address, entry->size))
	{
		if (entry->generation)
			cache.statistics.remade++;
		/* protect first, so a write racing with the reading is noticed */
		host_memory_watch_protect(entry->address, entry->size);
		entry->generation = host_memory_watch_generation(entry->address, entry->size);
		if (!entry->generation)
			entry->generation = 1;
		if (host_render_record_active)
			host_render_record_note(entry->address, entry->size);
		xbox_texture_upload(entry, palette);
	}
	entry->watched_serial = serial;
	return entry;
}

/* ---------- the effects' thickness (xgpu_vulkan_effects.h) */

static double seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

/* the entry's thickness, baked from its first level's texels (as they are
now: the entry made current first) */
static struct xgpu_vulkan_image *thickness_bake(struct entry *entry, const D3DCOLOR *palette)
{
	const struct xgpu_texture_description *description = &entry->description;
	struct xgpu_vulkan_image_description made;
	uint32_t width = (uint32_t)description->width, height = (uint32_t)description->height;
	const unsigned char *source = host_pointer(entry->address);
	DWORD *texels;
	uint8_t *thickness;
	double started = seconds();
	int baked;

	thickness_forget(entry);
	entry->thickness_failed = entry->generation;
	if (description->cube_map || description->depth > 1 || !source || width > XGPU_VULKAN_THICKNESS_LARGEST ||
		height > XGPU_VULKAN_THICKNESS_LARGEST || !(texels = converted_get((size_t)width * height)))
		return NULL;
	if (description->compressed)
		xgpu_texture_decode_dxt(description, 0, source, texels);
	else if (!xgpu_texture_decode_level(description, 0, source, palette, texels))
		return NULL;
	thickness = malloc((size_t)width * height * 4);
	if (!thickness)
		return NULL;
	baked = xgpu_vulkan_thickness_bake((const uint32_t *)texels, width, height, thickness);
	if (baked)
	{
		memset(&made, 0, sizeof(made));
		made.type = XGPU_VULKAN_IMAGE_2D;
		made.format = XGPU_VULKAN_FORMAT_RGBA8;
		made.usage = XGPU_VULKAN_USAGE_SAMPLED;
		made.width = width;
		made.height = height;
		made.depth_or_layers = 1;
		made.levels = 1;
		entry->thickness = xgpu_vulkan_image_create(&made);
		if (entry->thickness)
		{
			xgpu_vulkan_image_upload(entry->thickness, 0, 0, 0, 0, 0, width, height, 1, thickness, width * 4, 0);
			entry->thickness_generation = entry->generation;
			entry->thickness_failed = 0;
			cache.statistics.thickness++;
			cache.statistics.thickness_seconds += seconds() - started;
		}
	}
	free(thickness);
	return entry->thickness;
}

struct xgpu_vulkan_image *xgpu_vulkan_textures_thickness(const struct xgpu_stream_texture_stage *stage)
{
	const D3DCOLOR *palette;
	struct entry *entry;

	if (stage->source != XGPU_SOURCE_TEXTURE || !stage->resource || !(entry = xbox_entry_current(stage, &palette)))
		return NULL;
	/* (texels never read, their pages not readable: nothing to bake from,
	and not to be read here either) */
	if (!entry->generation)
		return NULL;
	if (entry->thickness && entry->thickness_generation == entry->generation)
		return entry->thickness;
	if (entry->thickness_failed && entry->thickness_failed == entry->generation)
		return NULL;
	return thickness_bake(entry, palette);
}

uint32_t xgpu_vulkan_textures_thickness_epoch(void)
{
	return cache.thickness_epoch;
}

/* ---------- images */

static struct xgpu_vulkan_image *image_get(const struct xgpu_stream_texture_stage *stage)
{
	struct entry **bucket = &cache.buckets[bucket_index(stage->resource, 0, 1)];
	struct entry *entry;
	const void *pixels = host_pointer(stage->data);
	uint32_t width = stage->width, height = stage->height;
	/* (XGPU_IMAGE_RGBA, xgpu_images.h: 4 bytes a pixel) */
	uint32_t texel = 4;

	if (!pixels || !width || !height || width > 16384 || height > 16384 || !readable(stage->data, width * height * texel))
		return NULL;
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->kind == _entry_image && entry->data == stage->resource)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->kind = _entry_image;
		entry->data = stage->resource;
		entry->next = *bucket;
		*bucket = entry;
	}
	entry->last_used = cache.frame;
	if (entry->image && entry->format_word == stage->resource_size)
		return entry->image;
	/* (its pixels changed, or it is another image of the number's) */
	if (!entry->image || entry->description.width != width || entry->description.height != height ||
		entry->description.levels != stage->levels || entry->size_word != stage->resource_format)
	{
		struct xgpu_vulkan_image_description made;
		uint32_t most = levels_most(width, height, 1);

		if (entry->image)
			xgpu_vulkan_image_destroy(entry->image);
		memset(&made, 0, sizeof(made));
		made.type = XGPU_VULKAN_IMAGE_2D;
		made.format = XGPU_VULKAN_FORMAT_RGBA8;
		made.usage = XGPU_VULKAN_USAGE_SAMPLED;
		made.width = width;
		made.height = height;
		made.depth_or_layers = 1;
		made.levels = stage->levels > 1 ? (stage->levels < most ? stage->levels : most) : 1;
		entry->image = xgpu_vulkan_image_create(&made);
		entry->description.width = width;
		entry->description.height = height;
		entry->description.levels = stage->levels;
		entry->size_word = stage->resource_format;
		if (!entry->image)
			return NULL;
	}
	entry->format_word = stage->resource_size;
	if (host_render_record_active)
		host_render_record_note(stage->data, width * height * texel);
	xgpu_vulkan_image_upload(entry->image, 0, 0, 0, 0, 0, width, height, 1, pixels, width * texel, 0);
	if (stage->levels > 1)
		xgpu_vulkan_image_make_levels(entry->image, 1);
	cache.statistics.images++;
	cache.statistics.bytes += (uint64_t)width * height * texel;
	return entry->image;
}

/* ---------- the cache */

struct xgpu_vulkan_image *xgpu_vulkan_textures_get(const struct xgpu_stream_texture_stage *stage)
{
	if (stage->source == XGPU_STREAM_SOURCE_IMAGE)
		return image_get(stage);
	return stage->resource ? xbox_texture_get(stage) : NULL;
}

struct xgpu_vulkan_image *xgpu_vulkan_textures_header(uint32_t header)
{
	struct xgpu_stream_texture_stage stage;
	const uint32_t *words;

	if (!readable(header, 5 * sizeof(uint32_t)))
		return NULL;
	words = host_pointer(header);
	memset(&stage, 0, sizeof(stage));
	stage.source = XGPU_SOURCE_TEXTURE;
	stage.resource = header;
	stage.resource_format = words[3];
	stage.resource_size = words[4];
	return xbox_texture_get(&stage);
}

/* an idle entry kept for its thickness: one baked from its texels as the
guest still has them (none of its pages written since they were read).
The thicknesses are baked as the map is prepared so that play does not
bake them (a millisecond or so each on the Steam Frame, in the frames
with the most effects); let go with an idle texture, an effect's bitmap
not seen for half a minute (a grenade's smoke) was baked again at the
next explosion. Kept, it goes once the guest writes its pages (the next
map, or another texture read into them): its texels are no longer the
ones baked. */
static int thickness_keeps(const struct entry *entry)
{
	return entry->thickness && entry->thickness_generation == entry->generation &&
		(entry->watched_serial == host_memory_watch_serial() ||
			host_memory_watch_generation(entry->address, entry->size) <= entry->generation);
}

void xgpu_vulkan_textures_frame_end(void)
{
	unsigned long index;
	uint64_t idle_since;
	double now = seconds();

	cache.frame++;
	astc_finished();
	if (now < cache.next_sweep)
		return;
	cache.next_sweep = now + SWEEP_SECONDS;
	/* the textures not drawn since the look half a minute ago (none before
	the first looks, which find frame 0) */
	idle_since = cache.sweep_frames[cache.sweeps % SWEEP_COUNT];
	cache.sweep_frames[cache.sweeps % SWEEP_COUNT] = cache.frame;
	cache.sweeps++;
	for (index = 0; index < BUCKET_COUNT; index++)
	{
		struct entry **link = &cache.buckets[index];

		while (*link)
		{
			struct entry *entry = *link;

			if (entry->last_used < idle_since && !thickness_keeps(entry))
			{
				*link = entry->next;
				image_release(entry);
				thickness_forget(entry);
				free(entry);
			}
			else
			{
				link = &entry->next;
			}
		}
	}
}

void xgpu_vulkan_textures_statistics_peek(struct xgpu_vulkan_texture_statistics *statistics)
{
	*statistics = cache.statistics;
}

void xgpu_vulkan_textures_statistics(struct xgpu_vulkan_texture_statistics *statistics)
{
	*statistics = cache.statistics;
	memset(&cache.statistics, 0, sizeof(cache.statistics));
}
