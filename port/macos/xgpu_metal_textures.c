/*
XGPU_METAL_TEXTURES.C

The Metal backend's cache of the Xbox's textures and the high-res
replacements' images (xgpu_metal_textures.h), as the Vulkan backend's
(port/linux/vulkan/xgpu_vulkan_textures.c) has them, with the same
decoding (xbox_texture_decode.c, built into the host beside a stand-in for
the platform header: tools/macos_build.py).

An Xbox texture is found by its Data, Format and Size words (and a
palettized one's palette, and the channels a Custom Edition map's texels
are sampled from); its texels are read from the guest's memory where they
are. DXT textures are uploaded as they are (BC1 to BC3), a volume's
decoded; on a GPU without BC's formats (an iPhone's:
xgpu_metal_block_compression) they are ASTC (below), or decoded; every
other format is decoded to BGRA. The pages it was read from are
write-protected (the write tracking, host_memory_watch.c), and the
texture is read again once any of them has been written: the check is a
comparison of the tracking's serial while no page has been, and a look at
the texture's pages' generations otherwise. The host reads a frame after
the game drew it: a texture whose memory the game has let go of since (a
map's, as the next loads) is not read (its pages are no longer the
guest's).

An image (a high-res replacement: port/linux/src/xgpu_images.h) is found by
its number and made again when its serial changes; the backend makes its
levels from the first.

A texture no stage has drawn for half a minute of time is let go.

ASTC (display.astc_textures, xgpu_astc_chosen: on a GPU that samples ASTC
and not BC, an iPhone's): a 2D or cube DXT texture's blocks are transcoded
to ASTC 4x4 blocks as it is read (port/linux/astc/xgpu_astc.h), a quarter of
the bytes its decoded texels were. A texture with blocks transcoding cannot
make exactly (DXT3 and DXT5 alpha that varies inside a block, mostly) is
drawn decoded while a worker encodes those (or finds them on disk: the
app's caches folder, ASTC_CACHE_FOLDER), and made again as ASTC once it
has them (xgpu_metal_textures_frame_end), unless the worker finds it better
left decoded.
*/

#include "xgpu.h"
#include "xgpu_stream.h"
#include "host.h"
#include "host_render_record.h"
#include "xgpu_metal.h"
#include "xgpu_metal_textures.h"
#include "xgpu_astc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BUCKET_COUNT 4096
/* the textures are looked over every SWEEP_SECONDS, and those no stage
has drawn since the look SWEEP_COUNT before are let go: 30 to 40 seconds
after they were last drawn. Counted
in time, not frames: 1800 frames, half a minute at 60 frames a second, was
15 seconds on a 120 Hz display */
#define SWEEP_SECONDS 10
#define IDLE_SECONDS 30
#define SWEEP_COUNT (IDLE_SECONDS / SWEEP_SECONDS)
/* the copies of one texture for its palettes (the game cycles some
through a few) */
#define PALETTE_VARIANTS 8
/* D3DFMT_P8 */
#define FORMAT_PALETTIZED 0x0b

/* the Xbox's contiguous memory, where the textures are: physical address
P is guest address PLATFORM_CONTIGUOUS_BASE + P (platform.h) */
#define CONTIGUOUS_BASE HALO_GUEST_WINDOW_BASE
#define CONTIGUOUS_SIZE HALO_GUEST_WINDOW_SIZE
/* the ASTC blocks encoded, kept in the app's caches folder (under $HOME:
the app's container on iOS; HALO_ASTC_CACHE another, or "" none), to this
many bytes (all the stock maps' are some 60 MB) */
#define ASTC_CACHE_FOLDER "Library/Caches/OpenH1/astc"
#define ASTC_CACHE_BYTES (256u << 20)

enum
{
	_entry_xbox,
	_entry_image,
};

/* a texture's bytes, by what they are */
enum
{
	_resident_other,
	_resident_astc,
	_resident_decoded,
	_resident_bc,
	_resident_kinds
};

struct entry
{
	struct entry *next;
	uint8_t kind;
	/* an Xbox texture's words, its palette's hash and its channels; an
	image's number (data) and serial (format_word) */
	uint32_t data, format_word, size_word, palette_hash, channels;
	uint32_t texture;
	struct xgpu_texture_description description;
	/* (an Xbox texture's) the range read, and its pages' newest generation
	as of the tracking's serial watched_serial */
	uint32_t address, size;
	uint32_t generation, watched_serial;
	uint64_t last_used;
	/* (a DXT texture's, with ASTC) its texture is ASTC; the job encoding the
	blocks it is waiting for, drawn decoded meanwhile */
	uint8_t astc;
	struct xgpu_astc_job *astc_job;
	/* its texture's bytes (all its levels and faces), and what they are
	counted as (_resident_*) */
	uint32_t bytes;
	uint8_t resident;
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
	struct xgpu_metal_texture_statistics statistics;
	/* ASTC: to be used (-1 not decided yet); the DXT blocks of the largest
	texture so far, its ASTC blocks and which are to be encoded */
	int astc;
	uint8_t *dxt_blocks, *astc_blocks, *encode;
	size_t blocks_size;
	/* the textures' bytes now: all, and by what they are (_resident_*:
	DXT as ASTC, decoded, as BC) */
	uint64_t resident, resident_kinds[_resident_kinds];
	uint32_t astc_waiting;
} cache = { .astc = -1 };

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

static uint16_t levels_most(unsigned long width, unsigned long height, unsigned long depth)
{
	unsigned long largest = width > height ? width : height;
	uint16_t levels = 1;

	if (depth > largest)
		largest = depth;
	while (largest > 1)
	{
		largest >>= 1;
		levels++;
	}
	return levels;
}

/* ---------- Xbox textures */

/* the texture's storage, made for its description */
static uint32_t xbox_texture_make(const struct entry *entry)
{
	const struct xgpu_texture_description *description = &entry->description;
	struct xgpu_metal_texture_description made;
	uint16_t most = levels_most(description->width, description->height, description->depth);

	memset(&made, 0, sizeof(made));
	made.type = description->cube_map ? XGPU_METAL_TEXTURE_CUBE : description->depth > 1 ? XGPU_METAL_TEXTURE_3D :
		XGPU_METAL_TEXTURE_2D;
	/* (DXT as it is, but for a volume's, which is decoded, and on a GPU
	without BC's formats: ASTC, or decoded) */
	if (description->compressed && description->depth == 1 && xgpu_metal_block_compression() && cache.astc != 1)
		made.format = description->format == 0x0c ? XGPU_METAL_FORMAT_DXT1 : description->format == 0x0e ?
			XGPU_METAL_FORMAT_DXT3 : XGPU_METAL_FORMAT_DXT5;
	else if (entry->astc)
		made.format = XGPU_METAL_FORMAT_ASTC;
	else
		made.format = XGPU_METAL_FORMAT_BGRA8;
	made.usage = XGPU_METAL_USAGE_SAMPLED;
	made.width = (uint16_t)description->width;
	made.height = (uint16_t)description->height;
	made.depth_or_layers = (uint16_t)(description->depth > 1 ? description->depth : 1);
	made.levels = (uint16_t)(description->levels < most ? description->levels : most);
	made.channels = (uint16_t)entry->channels;
	return xgpu_metal_texture_create(&made);
}

/* the bytes of a texture of the entry's description in a format of bytes a
texel (block: bytes a 4x4 block) */
static uint32_t texture_bytes(const struct entry *entry, unsigned bytes, int block)
{
	const struct xgpu_texture_description *description = &entry->description;
	unsigned long levels = levels_most(description->width, description->height, description->depth), level;
	uint64_t total = 0;

	if (description->levels < levels)
		levels = description->levels;
	for (level = 0; level < levels; level++)
	{
		uint64_t width = xgpu_texture_level_dimension(description->width, level);
		uint64_t height = xgpu_texture_level_dimension(description->height, level);
		uint64_t depth = xgpu_texture_level_dimension(description->depth, level);

		total += block ? (width + 3) / 4 * ((height + 3) / 4) * depth * bytes : width * height * depth * bytes;
	}
	total *= description->cube_map ? 6 : 1;
	return total > 0xffffffffu ? 0xffffffffu : (uint32_t)total;
}

/* the entry's texture let go, and its bytes counted out */
static void texture_release(struct entry *entry)
{
	if (entry->texture)
		xgpu_metal_texture_destroy(entry->texture);
	entry->texture = 0;
	cache.resident -= entry->bytes;
	cache.resident_kinds[entry->resident] -= entry->bytes;
	entry->bytes = 0;
}

/* the entry's texture made (its format as entry->astc and the GPU have
it), and its bytes counted */
static uint32_t texture_make(struct entry *entry)
{
	const struct xgpu_texture_description *description = &entry->description;

	entry->texture = xbox_texture_make(entry);
	if (!entry->texture)
		return 0;
	if (entry->astc)
	{
		entry->bytes = texture_bytes(entry, XGPU_ASTC_BLOCK_BYTES, 1);
		entry->resident = _resident_astc;
	}
	else if (description->compressed && description->depth == 1 && xgpu_metal_block_compression() && cache.astc != 1)
	{
		entry->bytes = texture_bytes(entry, description->format == 0x0c ? 8 : 16, 1);
		entry->resident = _resident_bc;
	}
	else
	{
		entry->bytes = texture_bytes(entry, 4, 0);
		entry->resident = description->compressed ? _resident_decoded : _resident_other;
	}
	cache.resident += entry->bytes;
	cache.resident_kinds[entry->resident] += entry->bytes;
	return entry->texture;
}

/* ---------- ASTC */

const char *xgpu_metal_textures_astc_folder(char *path, size_t size)
{
	const char *folder = getenv("HALO_ASTC_CACHE"), *home = getenv("HOME");

	if (folder)
		return folder;
	if (!home || !*home)
		return NULL;
	snprintf(path, size, "%s/" ASTC_CACHE_FOLDER, home);
	return path;
}

/* whether DXT textures are ASTC here (decided at the first, once the
device is there): display.astc_textures, and the GPU's formats */
static int astc_wanted(void)
{
	if (cache.astc < 0)
	{
		char path[1024];

		cache.astc = xgpu_astc_chosen(host_astc_textures, xgpu_metal_block_compression(), xgpu_metal_astc());
		if (cache.astc)
			xgpu_astc_cache_initialize(xgpu_metal_textures_astc_folder(path, sizeof(path)), ASTC_CACHE_BYTES);
		host_logf(HOST_LOG_INFO, "metal: DXT textures %s (display.astc_textures \"%s\"; the GPU samples BC %s, ASTC %s)",
			cache.astc ? "as ASTC 4x4" : xgpu_metal_block_compression() ? "as BC" : "decoded", host_astc_textures,
			xgpu_metal_block_compression() ? "yes" : "no", xgpu_metal_astc() ? "yes" : "no");
	}
	return cache.astc;
}

/* XGPU_ASTC_DXT* of a DXT format */
static int astc_kind(DWORD format)
{
	return format == 0x0c ? XGPU_ASTC_DXT1 : format == 0x0e ? XGPU_ASTC_DXT3 : XGPU_ASTC_DXT5;
}

/* the texture's DXT blocks, its faces' levels one after another, into
cache.dxt_blocks (with room for as many ASTC blocks in cache.astc_blocks);
their count, 0 if there is no room */
static uint32_t astc_blocks_read(const struct entry *entry)
{
	const struct xgpu_texture_description *description = &entry->description;
	const unsigned char *base = host_pointer(entry->address);
	unsigned long faces = description->cube_map ? 6 : 1, face_size = xgpu_texture_face_size(description);
	unsigned long levels = levels_most(description->width, description->height, 1), face, level;
	unsigned block_bytes = xgpu_astc_dxt_block_bytes(astc_kind(description->format));
	uint32_t count = texture_bytes(entry, XGPU_ASTC_BLOCK_BYTES, 1) / XGPU_ASTC_BLOCK_BYTES, at = 0;

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
	if (description->levels < levels)
		levels = description->levels;
	for (face = 0; face < faces; face++)
	{
		for (level = 0; level < levels; level++)
		{
			uint32_t blocks = (uint32_t)((xgpu_texture_level_dimension(description->width, level) + 3) / 4 *
				((xgpu_texture_level_dimension(description->height, level) + 3) / 4));

			if (at + blocks > count)
				return 0;
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
	unsigned long levels = levels_most(description->width, description->height, 1), face, level;

	if (description->levels < levels)
		levels = description->levels;
	for (face = 0; face < faces; face++)
	{
		for (level = 0; level < levels; level++)
		{
			uint32_t width = (uint32_t)xgpu_texture_level_dimension(description->width, level);
			uint32_t height = (uint32_t)xgpu_texture_level_dimension(description->height, level);
			uint32_t row = (width + 3) / 4 * XGPU_ASTC_BLOCK_BYTES;

			xgpu_metal_texture_upload(entry->texture, (uint32_t)level, (uint32_t)face, 0, 0, 0, width, height, 1, blocks,
				row, 0);
			blocks += (size_t)row * ((height + 3) / 4);
		}
	}
}

/* the entry's texture made again as ASTC (astc) or decoded, if it is not
so; zero if it cannot be */
static int astc_format(struct entry *entry, int astc)
{
	if (entry->texture && entry->astc == astc)
		return 1;
	texture_release(entry);
	entry->astc = (uint8_t)astc;
	return texture_make(entry) != 0;
}

/* a DXT texture read as ASTC (the guest's memory readable over it): its
blocks transcoded, and uploaded if they all were, or if the workers have
the rest in memory (the same texels read before at another address); else
it is made decoded (xbox_texture_upload goes on: zero) and its blocks go
to a worker, unless it is known to be better decoded. Nonzero if it was
uploaded, or has no texture to upload into. */
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
		if (left)
			cache.statistics.astc_encoded++;
		else
			cache.statistics.astc_transcoded++;
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

/* (HALO_METAL_STATS, each look over the textures) the textures' bytes, and
the ASTC encoder's work */
static void resident_log(void)
{
	static int wanted = -1;
	struct xgpu_astc_statistics astc;

	if (wanted < 0)
		wanted = getenv("HALO_METAL_STATS") != NULL;
	if (!wanted)
		return;
	xgpu_astc_statistics(&astc);
	host_logf(HOST_LOG_INFO, "metal: textures %.1f MB (DXT as ASTC %.1f MB, decoded %.1f MB, as BC %.1f MB), %u "
		"waiting for the ASTC encoder; the encoder: %u jobs, %u from the disk cache, %u found in memory, %llu blocks "
		"in %.2f s, %.1f MB on disk", cache.resident / 1048576.0, cache.resident_kinds[_resident_astc] / 1048576.0,
		cache.resident_kinds[_resident_decoded] / 1048576.0, cache.resident_kinds[_resident_bc] / 1048576.0,
		cache.astc_waiting, astc.jobs, astc.cache_hits, astc.memory_hits, (unsigned long long)astc.blocks_encoded,
		astc.encode_seconds, astc.cache_bytes / 1048576.0);
}

/* the workers' finished jobs: their textures made again as ASTC (the
texels the same: a job is cancelled when its texture is read again) */
static void astc_finished(void)
{
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
		{
			astc_upload(entry, blocks);
			cache.statistics.astc_encoded++;
		}
		else if (!blocks)
		{
			cache.statistics.astc_left_decoded++;
		}
		xgpu_astc_job_free(job);
	}
}

/* reads the texture's texels into it */
static void xbox_texture_upload(struct entry *entry, const D3DCOLOR *palette)
{
	const struct xgpu_texture_description *description = &entry->description;
	const unsigned char *base = host_pointer(entry->address);
	unsigned long faces = description->cube_map ? 6 : 1, face_size = xgpu_texture_face_size(description);
	unsigned long levels = levels_most(description->width, description->height, description->depth), face, level;
	BOOL blocks = description->compressed && description->depth == 1 && xgpu_metal_block_compression() && cache.astc != 1;

	if (description->levels < levels)
		levels = description->levels;
	if (description->compressed && description->depth == 1 && cache.astc == 1 && astc_read(entry))
	{
		cache.statistics.bytes += entry->size;
		return;
	}
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

				xgpu_metal_texture_upload(entry->texture, (uint32_t)level, (uint32_t)face, 0, 0, 0, width, height, 1, source,
					row, 0);
				continue;
			}
			converted = converted_get((size_t)width * height * depth);
			if (!converted)
				return;
			if (description->compressed)
				xgpu_texture_decode_dxt(description, level, source, converted);
			else if (!xgpu_texture_decode_level(description, level, source, palette, converted))
				return;
			xgpu_metal_texture_upload(entry->texture, (uint32_t)level, (uint32_t)face, 0, 0, 0, width, height, depth,
				converted, width * 4, width * height * 4);
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
	/* (one larger than the device takes is never drawn, nor one outside the
	contiguous memory) */
	if (xgpu_texture_size_supported(&entry->description))
	{
		uint64_t size = (uint64_t)xgpu_texture_face_size(&entry->description) * (entry->description.cube_map ? 6 : 1);

		if (size && entry->address >= CONTIGUOUS_BASE && (uint64_t)entry->address + size <=
			(uint64_t)CONTIGUOUS_BASE + CONTIGUOUS_SIZE)
			entry->size = (uint32_t)size;
	}
	/* (made ASTC from the start where it most likely will be: most DXT1
	textures transcode exactly, most DXT3 and DXT5 ones wait for a worker) */
	entry->astc = (uint8_t)(entry->description.compressed && entry->description.depth == 1 && astc_wanted() &&
		entry->description.format == 0x0c);
	if (entry->size)
		texture_make(entry);
	if (!entry->size || !entry->texture)
		host_logf(HOST_LOG_WARN, "metal: a %lux%lux%lu texture of format %02x at %08x is not drawn",
			entry->description.width, entry->description.height, entry->description.depth,
			(unsigned)entry->description.format, data);
	entry->next = *bucket;
	*bucket = entry;
	cache.statistics.made++;
	return entry;
}

/* the guest's pages of a range are mapped and readable (looked at on whole
host pages, the extent's limits: no further) */
static BOOL readable(uint32_t address, uint32_t size)
{
	uint64_t page = host_page_size();
	uint64_t first = address & ~(page - 1), last = ((uint64_t)address + size + page - 1) & ~(page - 1);
	uint32_t start, end;

	if (!address || last > 0xffffffffu)
		return FALSE;
	return host_memory_readable_extent(address, size, (uint32_t)first, (uint32_t)last, &start, &end) != 0;
}

static uint32_t xbox_texture_get(const struct xgpu_stream_texture_stage *stage)
{
	const D3DCOLOR *palette = readable(stage->palette, 256 * sizeof(D3DCOLOR)) ? host_pointer(stage->palette) : NULL;
	struct entry *entry;
	uint32_t serial, generation;

	if (!readable(stage->resource, 5 * sizeof(uint32_t)))
		return 0;
	/* (what is read of the guest's memory, for a recording) */
	if (host_render_record_active)
	{
		host_render_record_note(stage->resource, 5 * sizeof(uint32_t));
		if (palette && ((stage->resource_format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT) == FORMAT_PALETTIZED)
			host_render_record_note(stage->palette, 256 * sizeof(D3DCOLOR));
	}
	entry = xbox_entry_find(stage, palette);

	if (!entry || !entry->texture)
		return 0;
	entry->last_used = cache.frame;
	serial = host_memory_watch_serial();
	if (entry->generation && entry->watched_serial == serial)
		return entry->texture;
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
	return entry->texture;
}

/* ---------- images */

/* (XGPU_IMAGE_RGBA, xgpu_images.h: 4 bytes a pixel) */
#define IMAGE_TEXEL 4

static uint32_t image_get(const struct xgpu_stream_texture_stage *stage)
{
	struct entry **bucket = &cache.buckets[bucket_index(stage->resource, 0, 1)];
	struct entry *entry;
	const void *pixels = host_pointer(stage->data);
	uint32_t width = stage->width, height = stage->height;

	if (!pixels || !width || !height || width > 16384 || height > 16384 ||
		!readable(stage->data, width * height * IMAGE_TEXEL))
		return 0;
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->kind == _entry_image && entry->data == stage->resource)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return 0;
		entry->kind = _entry_image;
		entry->data = stage->resource;
		entry->next = *bucket;
		*bucket = entry;
	}
	entry->last_used = cache.frame;
	if (entry->texture && entry->format_word == stage->resource_size)
		return entry->texture;
	/* (its pixels changed, or it is another image of the number's) */
	if (!entry->texture || entry->description.width != width || entry->description.height != height ||
		entry->description.levels != stage->levels || entry->size_word != stage->resource_format)
	{
		struct xgpu_metal_texture_description made;
		uint16_t most = levels_most(width, height, 1);

		texture_release(entry);
		memset(&made, 0, sizeof(made));
		made.type = XGPU_METAL_TEXTURE_2D;
		made.format = XGPU_METAL_FORMAT_RGBA8;
		made.usage = XGPU_METAL_USAGE_SAMPLED;
		made.width = (uint16_t)width;
		made.height = (uint16_t)height;
		made.depth_or_layers = 1;
		made.levels = (uint16_t)(stage->levels > 1 ? (stage->levels < most ? stage->levels : most) : 1);
		entry->texture = xgpu_metal_texture_create(&made);
		entry->description.width = width;
		entry->description.height = height;
		entry->description.levels = stage->levels;
		entry->size_word = stage->resource_format;
		if (!entry->texture)
			return 0;
		entry->bytes = width * height * IMAGE_TEXEL *
			(made.levels > 1 ? 4 : 3) / 3;
		entry->resident = _resident_other;
		cache.resident += entry->bytes;
		cache.resident_kinds[_resident_other] += entry->bytes;
	}
	entry->format_word = stage->resource_size;
	if (host_render_record_active)
		host_render_record_note(stage->data, width * height * IMAGE_TEXEL);
	xgpu_metal_texture_upload(entry->texture, 0, 0, 0, 0, 0, width, height, 1, pixels,
		width * IMAGE_TEXEL, 0);
	if (stage->levels > 1)
		xgpu_metal_texture_make_levels(entry->texture);
	cache.statistics.images++;
	cache.statistics.bytes += (uint64_t)width * height * IMAGE_TEXEL;
	return entry->texture;
}

/* ---------- the cache */

uint32_t xgpu_metal_textures_get(const struct xgpu_stream_texture_stage *stage)
{
	if (stage->source == XGPU_STREAM_SOURCE_IMAGE)
		return image_get(stage);
	return stage->resource ? xbox_texture_get(stage) : 0;
}

static double seconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

void xgpu_metal_textures_frame_end(void)
{
	unsigned long index;
	uint64_t idle_since;
	double now = seconds();

	cache.frame++;
	astc_finished();
	if (now < cache.next_sweep)
		return;
	cache.next_sweep = now + SWEEP_SECONDS;
	resident_log();
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

			if (entry->last_used < idle_since)
			{
				*link = entry->next;
				texture_release(entry);
				if (entry->astc_job)
				{
					xgpu_astc_cancel(entry->astc_job);
					cache.astc_waiting--;
				}
				free(entry);
			}
			else
			{
				link = &entry->next;
			}
		}
	}
}

void xgpu_metal_textures_statistics(struct xgpu_metal_texture_statistics *statistics)
{
	*statistics = cache.statistics;
	memset(&cache.statistics, 0, sizeof(cache.statistics));
	statistics->resident = cache.resident;
	statistics->resident_astc = cache.resident_kinds[_resident_astc];
	statistics->resident_decoded = cache.resident_kinds[_resident_decoded];
	statistics->astc_waiting = cache.astc_waiting;
}
