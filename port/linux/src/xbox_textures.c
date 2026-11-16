/*
XBOX_TEXTURES.C

The high-res replacements drawn in place of the Xbox's textures, and the
Custom Edition channel orders; the host caches the textures themselves
(port/linux/vulkan/xgpu_vulkan_textures.c, port/macos/xgpu_metal_textures.c),
decoded by xbox_texture_decode.c.

An Xbox texture is a Direct3D header - Common, Data (physical address),
Lock, Format and Size - over texels in guest memory. Power-of-two textures
are swizzled (Morton order, one level after another); textures with a Size
field are linear, with a pitch, and are addressed with texel coordinates.
DXT textures are stored as plain 4x4 blocks. Everything except DXT is
converted to 32-bit BGRA on upload.

A cached texture stays valid until any page it was read from is written;
memory_watch.c detects that by write-protecting the pages.

Here are the replacements, as images (xgpu_images.h), and the Custom
Edition channel orders, which the command stream hands the host.
*/

#include "xgpu_images.h"
#include "port_config.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "menu_pictures.h"
#include "text_hires.h"
#include "../game/cache_file_formats.h"

#include <SDL3/SDL.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ---------- Custom Edition channel orders

Halo PC keeps what some textures hold in other channels than the game reads
it from (enum custom_edition_channel_order): a model shader's multipurpose
masks, and a HUD meter's shape and fill order. The Custom Edition map
loading says which texels hold which order as they arrive
(port/linux/game/custom_edition_bitmaps.c), and textures made of them are
sampled with each channel taken from where Halo PC keeps it, which leaves
them as compressed as they were. Addresses stay listed until other texels
arrive there, which the loading also says, or the map goes; the game and the
renderer share a thread. */

/* for each order, the channel (red, green, blue, alpha) of the texels each
channel is sampled from */
static const unsigned char custom_edition_channel_sources[NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS][4] =
{
	{ 0, 1, 2, 3 },
	/* specular, self-illumination, color change and the auxiliary mask */
	{ 2, 1, 3, 0 },
	/* the fill order in color, the shape in alpha */
	{ 3, 3, 3, 0 },
	/* a meter's, in the Xbox's order as its map has it (drawn as it is, by
	meters too) */
	{ 0, 1, 2, 3 },
};

struct custom_edition_texels
{
	unsigned long address;
	unsigned char channel_order;
};

static struct custom_edition_texels *custom_edition_texels;
static unsigned long custom_edition_texel_count;
static unsigned long custom_edition_texel_capacity;

/* whether a HUD meter of a Custom Edition map is being drawn (hud_draw.c):
a sheet that is a meter's and other pictures' too keeps the order it has for
the pictures, and its texels in the Xbox's order are sampled in the meters'
for the meter's draw alone (its first stage) */
static int custom_edition_hud_meter_active;

void halo_custom_edition_hud_meter(int active)
{
	custom_edition_hud_meter_active = active;
}

/* the order of the texels at address */
static unsigned char custom_edition_texels_order(unsigned long address)
{
	unsigned long index;

	for (index = 0; index < custom_edition_texel_count; index++)
	{
		if (custom_edition_texels[index].address == address)
			return custom_edition_texels[index].channel_order;
	}
	return _custom_edition_channels_xbox;
}

void halo_custom_edition_texels_channels(const void *texels, unsigned char channel_order)
{
	unsigned long address = (unsigned long)texels;
	unsigned long index;

	if (channel_order >= NUMBER_OF_CUSTOM_EDITION_CHANNEL_ORDERS)
		channel_order = _custom_edition_channels_xbox;
	for (index = 0; index < custom_edition_texel_count && custom_edition_texels[index].address != address; index++)
	{
	}
	if (index < custom_edition_texel_count)
	{
		if (channel_order == _custom_edition_channels_xbox)
			custom_edition_texels[index] = custom_edition_texels[--custom_edition_texel_count];
		else
			custom_edition_texels[index].channel_order = channel_order;
	}
	else if (channel_order != _custom_edition_channels_xbox)
	{
		if (custom_edition_texel_count == custom_edition_texel_capacity)
		{
			unsigned long capacity = custom_edition_texel_capacity ? custom_edition_texel_capacity * 2 : 64;
			struct custom_edition_texels *grown = realloc(custom_edition_texels, capacity * sizeof(*grown));

			if (!grown)
			{
				platform_log("no memory to list the texels at %08lx: they are sampled in Halo PC's channel order",
					address);
				return;
			}
			custom_edition_texels = grown;
			custom_edition_texel_capacity = capacity;
		}
		custom_edition_texels[custom_edition_texel_count].address = address;
		custom_edition_texels[custom_edition_texel_count].channel_order = channel_order;
		custom_edition_texel_count++;
	}
}

void halo_custom_edition_texels_forget(void)
{
	custom_edition_hud_meter_active = 0;
	free(custom_edition_texels);
	custom_edition_texels = NULL;
	custom_edition_texel_count = 0;
	custom_edition_texel_capacity = 0;
}

unsigned long xgpu_texture_channels(unsigned long address, int stage)
{
	unsigned char order = custom_edition_texels_order(address);
	const unsigned char *sources;

	if (order == _custom_edition_channels_xbox && stage == 0 && custom_edition_hud_meter_active)
		order = _custom_edition_channels_hud_meter;
	if (order == _custom_edition_channels_xbox || order == _custom_edition_channels_hud_meter_xbox)
		return 0;
	sources = custom_edition_channel_sources[order];
	return 0x100 | sources[0] | (sources[1] << 2) | (sources[2] << 4) | ((unsigned long)sources[3] << 6);
}

/* ---------- the replacements, where the renderer is on the host

What the host's cache draws in a texture's place: the high-res text's
atlas, a menu's art, a high-res HUD texture for a placeholder bitmap, or
for the bitmap whose pixels these are. Which HUD texture stands for a
bitmap's pixels is found again whenever they change (the write tracking),
for each texture by its Data, Format and Size. */

#define REPLACEMENT_BUCKET_COUNT 1024
#define REPLACEMENT_IDLE_FRAMES 1800

/* A menu's picture of ui.map's drawn larger than it is (kind 5: a mission's
picture, a difficulty's badge) is upscaled on a thread of its own, the
original drawn until it is: upscaling one (256x128 to 1024x512) took a few
milliseconds of the frame that first drew it, four of them as Choose
Mission opened. Where the frames' time is fixed (debug.frame_time:
port/linux/game/frames_clock.c) the frame that asks for it waits for it, so
that two runs draw the same frames (tools/menu_parity.py). */
struct upscale_job
{
	struct upscale_job *next;
	unsigned char *bgra, *pixels;
	unsigned long width, height;
	long kind;
	int done;
	/* its entry gone: freed by the thread when done */
	int abandoned;
};

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t work;
	/* (a job done: upscale_take's wait) */
	pthread_cond_t finished;
	int running;
	struct upscale_job *queue;
} upscaler = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

/* frames_clock.c */
int frames_clock(unsigned long *milliseconds);

static void *upscaler_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&upscaler.lock);
	for (;;)
	{
		struct upscale_job *job;
		unsigned char *pixels;

		while (!upscaler.queue)
			pthread_cond_wait(&upscaler.work, &upscaler.lock);
		job = upscaler.queue;
		upscaler.queue = job->next;
		pthread_mutex_unlock(&upscaler.lock);
		pixels = job->abandoned ? NULL :
			hud_hires_upscale_kind_pixels(job->bgra, job->width, job->height, job->kind);
		pthread_mutex_lock(&upscaler.lock);
		free(job->bgra);
		job->bgra = NULL;
		if (job->abandoned)
		{
			free(pixels);
			free(job);
			continue;
		}
		job->pixels = pixels;
		job->done = 1;
		pthread_cond_broadcast(&upscaler.finished);
	}
	return NULL;
}

/* the texels (BGRA, the job's now) upscaled on the thread: the job, or NULL
if it cannot be */
static struct upscale_job *upscale_begin(unsigned char *bgra, unsigned long width, unsigned long height, long kind)
{
	struct upscale_job *job = calloc(1, sizeof(*job)), **end;

	if (!job)
	{
		free(bgra);
		return NULL;
	}
	job->bgra = bgra;
	job->width = width;
	job->height = height;
	job->kind = kind;
	pthread_mutex_lock(&upscaler.lock);
	if (!upscaler.running)
	{
		pthread_t thread;

		if (pthread_create(&thread, NULL, upscaler_thread, NULL) != 0)
		{
			pthread_mutex_unlock(&upscaler.lock);
			free(bgra);
			free(job);
			return NULL;
		}
		pthread_detach(thread);
		upscaler.running = 1;
	}
	for (end = &upscaler.queue; *end; end = &(*end)->next)
		;
	*end = job;
	pthread_cond_signal(&upscaler.work);
	pthread_mutex_unlock(&upscaler.lock);
	return job;
}

/* the job's image, once it is done (the job freed); 0 until then, or if it
could not be made */
static unsigned int upscale_take(struct upscale_job **job, unsigned long *levels)
{
	unsigned char *pixels;
	unsigned long width, height;

	pthread_mutex_lock(&upscaler.lock);
	/* (waited for where the frames' time is fixed) */
	while (!(*job)->done && frames_clock(NULL))
		pthread_cond_wait(&upscaler.finished, &upscaler.lock);
	if (!(*job)->done)
	{
		pthread_mutex_unlock(&upscaler.lock);
		return 0;
	}
	pthread_mutex_unlock(&upscaler.lock);
	pixels = (*job)->pixels;
	width = (*job)->width;
	height = (*job)->height;
	free(*job);
	*job = NULL;
	return hud_hires_upscaled_texture(pixels, width, height, levels);
}

/* the job let go of (freed now if done, else by the thread) */
static void upscale_drop(struct upscale_job **job)
{
	if (!*job)
		return;
	pthread_mutex_lock(&upscaler.lock);
	if ((*job)->done)
	{
		free((*job)->pixels);
		free(*job);
	}
	else
	{
		(*job)->abandoned = 1;
	}
	pthread_mutex_unlock(&upscaler.lock);
	*job = NULL;
}

struct replacement_entry
{
	struct replacement_entry *next;
	DWORD data, format_word, size_word;
	/* the high-res HUD texture (hud_hires.h), or -1 */
	long override;
	unsigned int hud_upscale;
	unsigned long hud_upscale_levels, last_used_frame;
	BOOL hud_upscale_checked;
	/* its upscaling on the thread (a menu's picture's), until it is done */
	struct upscale_job *upscale_job;
	/* the menu picture's pose whose original this is (menu_pictures.h), -1
none, -2 not yet looked for */
	long picture;
	/* its pages' newest generation when it was found, and the memory
	watch serial as of which that is known (as texture_entry's) */
	unsigned long generation, watched_serial;
};

static struct replacement_entry *replacement_buckets[REPLACEMENT_BUCKET_COUNT];
static unsigned long replacement_frame;

/* Each texture's choice, kept for its next binds (a frame binds a texture
many times, and each texture of the frame about 1,400 times in all): what
was drawn in its place, for its Data, Format and Size, while nothing it was
chosen from has changed since. That is the replacements' epoch, moved as a
replacement may change (a map's HUD placeholders and text atlas registered,
the menus' art registered or let go, an entry found again or freed); the
memory watch's serial, as a texture's pixels are written; and the settings
(config_changes: display.high_res_hud and display.high_res_text). The text
atlas, a menu's picture and a picture being upscaled are chosen at every
bind, as their images change as they are drawn. */
#define REPLACEMENT_CHOICE_COUNT 2048

struct replacement_choice
{
	DWORD data, format_word, size_word;
	unsigned long epoch, watched_serial, settings;
	/* its entry (NULL: one the replacements do not keep), whose last use
	is moved at its first bind of each frame (used_frame) */
	struct replacement_entry *entry;
	unsigned long used_frame;
	unsigned int image;
	unsigned long levels;
	unsigned char kept;
	/* a high-res HUD texture's (its coverage and point threshold) */
	unsigned char override, coverage, point_threshold;
};

static struct replacement_choice replacement_choices[REPLACEMENT_CHOICE_COUNT];
static unsigned long replacement_epoch;

void xgpu_texture_replacements_changed(void)
{
	__atomic_fetch_add(&replacement_epoch, 1, __ATOMIC_RELEASE);
}

void xgpu_texture_cache_begin_frame(void)
{
	unsigned long changed = hud_hires_settings_changed();
	unsigned long index;

	replacement_frame++;
	if (!changed && replacement_frame % 600)
		return;
	for (index = 0; index < REPLACEMENT_BUCKET_COUNT; index++)
	{
		struct replacement_entry **link = &replacement_buckets[index];

		while (*link)
		{
			struct replacement_entry *entry = *link;
			struct xgpu_texture_description description;
			unsigned long address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(entry->data);

			if (replacement_frame - entry->last_used_frame > REPLACEMENT_IDLE_FRAMES)
			{
				/* (no choice kept points at it after) */
				xgpu_texture_replacements_changed();
				*link = entry->next;
				xgpu_image_delete(entry->hud_upscale);
				upscale_drop(&entry->upscale_job);
				free(entry);
				continue;
			}
			if (changed)
			{
				xgpu_texture_describe(entry->format_word, entry->size_word, &description);
				if (hud_hires_bitmap_affected(address, (long)description.width, (long)description.height, changed))
				{
					/* Find the replacement again even when its pixels have not
					changed. Keep the images used by frames still in flight. */
					entry->generation = 0;
					entry->watched_serial = 0;
					xgpu_texture_replacements_changed();
				}
			}
			link = &entry->next;
		}
	}
}

/* the hash of a texture's Data, Format and Size */
static unsigned long replacement_hash(DWORD data, DWORD format_word, DWORD size_word)
{
	return ((unsigned long)data * 2654435761UL ^ (unsigned long)format_word * 2246822519UL ^
		(unsigned long)size_word * 3266489917UL) >> 20;
}

/* whether the replacements keep an entry for a texture of this kind */
static BOOL replacement_supported(const struct xgpu_texture_description *description)
{
	return !(description->format == 0x0b || description->cube_map || description->depth > 1 ||
		!xgpu_texture_size_supported(description));
}

static struct replacement_entry *replacement_get(const DWORD *resource, const struct xgpu_texture_description *description)
{
	DWORD data = resource[1], format_word = resource[3], size_word = resource[4];
	struct replacement_entry **bucket = &replacement_buckets[replacement_hash(data, format_word, size_word) %
		REPLACEMENT_BUCKET_COUNT];
	struct replacement_entry *entry;
	unsigned long address, size, generation, watch_serial = memory_watch_serial();

	if (!replacement_supported(description))
		return NULL;
	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->data == data && entry->format_word == format_word && entry->size_word == size_word)
			break;
	}
	if (!entry)
	{
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return NULL;
		entry->data = data;
		entry->format_word = format_word;
		entry->size_word = size_word;
		entry->override = -1;
		entry->picture = -2;
		entry->next = *bucket;
		*bucket = entry;
	}
	entry->last_used_frame = replacement_frame;
	if (entry->watched_serial && entry->watched_serial == watch_serial)
		return entry;
	address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(data);
	size = xgpu_texture_face_size(description);
	generation = memory_watch_generation(address, size);
	if (!entry->generation || generation > entry->generation)
	{
		unsigned long levels;

		/* protect first, so a write racing with the search is noticed */
		memory_watch_protect(address, size);
		entry->generation = memory_watch_generation(address, size);
		if (!entry->generation)
			entry->generation = 1;
		xgpu_image_delete(entry->hud_upscale);
		entry->hud_upscale = 0;
		upscale_drop(&entry->upscale_job);
		entry->hud_upscale_checked = FALSE;
		entry->picture = -2;
		entry->override = hud_hires_override_find(address, description->width, description->height,
			description->levels > 1 ? xgpu_texture_level_offset(description, 1) : size);
		if (entry->override >= 0 && !hud_hires_override_texture(entry->override, &levels))
			entry->override = -1;
	}
	entry->watched_serial = watch_serial;
	return entry;
}

/* the image drawn in the texture's place (xgpu_texture_replacement), its
entry (*kept_entry), whether it is a high-res HUD texture (*overridden), and
whether the choice may be kept for its next binds (*keep) */
static unsigned int replacement_choose(DWORD resource, struct xgpu_texture_description *description,
	struct replacement_entry **kept_entry, BOOL *overridden, BOOL *keep)
{
	const DWORD *header = (const DWORD *)resource;
	unsigned long levels;
	unsigned int image;
	long override;
	struct replacement_entry *entry;

	*kept_entry = NULL;
	*overridden = FALSE;
	*keep = !text_hires_atlas_placeholder(header[1]);
	/* (the high-res text's atlas, for its placeholder bitmap: text_hires.h;
	its distance fields filtered as a high-res picture's) */
	if ((image = text_hires_atlas_texture(header[1])) != 0)
	{
		description->levels = 1;
		description->hires = TRUE;
		description->hires_msdf = TRUE;
		return image;
	}
	/* (a menu's bitmap: menu_files.h) */
	if ((image = menu_art_texture(header[1], &levels)) != 0)
	{
		description->levels = levels;
		description->hires = TRUE;
		return image;
	}
	/* (a high-res texture drawn for some sprites, for the placeholder the
	game draws them from: hud_hires.h) */
	if ((image = hud_hires_placeholder_texture(header[1], &levels)) != 0)
	{
		description->levels = levels;
		description->hires = TRUE;
		return image;
	}
	entry = replacement_get(header, description);
	if (!entry)
	{
		/* (out of memory: chosen again) */
		if (replacement_supported(description))
			*keep = FALSE;
		return 0;
	}
	*kept_entry = entry;
	/* (a menu's picture of a mission or a map, drawn again from the
player's map: menu_pictures.c) */
	if (entry->picture == -2)
	{
		entry->picture = description->width == MENU_PICTURE_FRAME_WIDTH &&
			description->height == MENU_PICTURE_FRAME_HEIGHT ?
			menu_picture_at((unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(header[1]), (long)description->width,
				(long)description->height) : -1;
	}
	/* (its picture is made on a thread, and made again larger) */
	if (entry->picture >= 0)
		*keep = FALSE;
	if (entry->picture >= 0 && (image = menu_pictures_texture(entry->picture, &levels)) != 0)
	{
		description->levels = levels;
		description->hires = TRUE;
		return image;
	}
	override = entry->override;
	if (override >= 0 && (image = hud_hires_override_texture(override, &levels)) != 0)
	{
		description->levels = levels;
		description->hires = TRUE;
		description->hires_coverage = hud_hires_override_coverage(override);
		description->hires_point_threshold = hud_hires_override_point_threshold(override);
		*overridden = TRUE;
		return image;
	}
	if (hud_hires_enabled())
	{
		if (!entry->hud_upscale_checked)
		{
			unsigned long address = (unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(header[1]);
			unsigned long size = xgpu_texture_face_size(description);
			long kind = hud_hires_upscale_at(address, description->width, description->height);

			/* (a Custom Edition map's stock sheet, kind 4, may be DXT and
			up to 512 square; a menu's picture of ui.map's, kind 5, DXT) */
			unsigned long largest = kind == 4 ? 512 : 256;

			entry->hud_upscale_checked = TRUE;
			if (kind && size && (!description->compressed || kind == 4 || kind == 5) &&
				description->width <= largest && description->height <= largest &&
				custom_edition_texels_order(address) == _custom_edition_channels_xbox &&
				platform_is_contiguous((void *)address) && platform_is_contiguous((void *)(address + size - 1)))
			{
				DWORD *pixels = malloc(description->width * description->height * 4);
				BOOL decoded = FALSE;

				if (pixels && description->compressed)
				{
					xgpu_texture_decode_dxt(description, 0, (const unsigned char *)address, pixels);
					decoded = TRUE;
				}
				else if (pixels)
				{
					decoded = xgpu_texture_decode_level(description, 0, (const unsigned char *)address, NULL, pixels);
				}
				/* (a menu's picture upscaled on the thread, the original drawn
				meanwhile; the HUD's sheets, as the HUD is first drawn, here) */
				if (decoded && kind == 5)
				{
					entry->upscale_job = upscale_begin((unsigned char *)pixels, description->width,
						description->height, kind);
					pixels = NULL;
				}
				else if (decoded)
				{
					entry->hud_upscale = hud_hires_upscale_texture((const unsigned char *)pixels,
						description->width, description->height, kind, &entry->hud_upscale_levels);
				}
				free(pixels);
			}
		}
		if (entry->upscale_job)
			entry->hud_upscale = upscale_take(&entry->upscale_job, &entry->hud_upscale_levels);
		/* (until the thread has upscaled it) */
		if (entry->upscale_job)
			*keep = FALSE;
		if (entry->hud_upscale)
		{
			description->hires = TRUE;
			description->levels = entry->hud_upscale_levels;
			return entry->hud_upscale;
		}
	}
	return 0;
}

unsigned int xgpu_texture_replacement(DWORD resource, struct xgpu_texture_description *description)
{
	const DWORD *header = (const DWORD *)resource;
	DWORD data = header[1], format_word = header[3], size_word = header[4];
	struct replacement_choice *choice;
	unsigned long epoch, watched_serial, settings;
	struct replacement_entry *entry;
	unsigned int image;
	BOOL overridden, keep;

	/* (an MCC texture, drawn as an image of its own: below; every one's Data
	is the same, so none is kept among the choices) */
	if (data == XGPU_MCC_TEXTURE_DATA)
		return xgpu_mcc_texture_image(resource, 0, description);
	choice = &replacement_choices[replacement_hash(data, format_word, size_word) % REPLACEMENT_CHOICE_COUNT];
	epoch = __atomic_load_n(&replacement_epoch, __ATOMIC_ACQUIRE);
	watched_serial = memory_watch_serial();
	settings = config_changes();
	if (choice->kept && choice->data == data && choice->format_word == format_word &&
		choice->size_word == size_word && choice->epoch == epoch && choice->watched_serial == watched_serial &&
		choice->settings == settings)
	{
		if (choice->entry && choice->used_frame != replacement_frame)
		{
			choice->entry->last_used_frame = replacement_frame;
			choice->used_frame = replacement_frame;
		}
		if (choice->image)
		{
			description->levels = choice->levels;
			description->hires = TRUE;
			if (choice->override)
			{
				description->hires_coverage = choice->coverage;
				description->hires_point_threshold = choice->point_threshold;
			}
		}
		return choice->image;
	}
	image = replacement_choose(resource, description, &entry, &overridden, &keep);
	choice->kept = keep;
	if (keep)
	{
		choice->data = data;
		choice->format_word = format_word;
		choice->size_word = size_word;
		choice->epoch = epoch;
		choice->watched_serial = watched_serial;
		choice->settings = settings;
		choice->entry = entry;
		choice->used_frame = replacement_frame;
		choice->image = image;
		choice->levels = description->levels;
		choice->override = overridden != 0;
		choice->coverage = description->hires_coverage != 0;
		choice->point_threshold = description->hires_point_threshold != 0;
	}
	return image;
}

/* ---------- MCC textures (xgpu.h)

Each MCC texture's image, found by its header, made of its first level as
the texture is first bound (or as its map loads: xgpu_mcc_texture_prepare)
and kept until the texture is let go (xgpu_mcc_texture_forget); a P8
texture's is made again if it is bound with another palette. The game and
the front end share a thread. */

#define MCC_IMAGE_BUCKET_COUNT 1024

struct mcc_image
{
	struct mcc_image *next;
	const DWORD *resource;
	unsigned int image;
	/* the palette its image was decoded with (a P8 texture's) */
	unsigned long palette_hash;
	/* no image could be made: not tried again */
	BOOL failed;
};

static struct mcc_image *mcc_image_buckets[MCC_IMAGE_BUCKET_COUNT];

static unsigned long mcc_image_bucket(const DWORD *resource)
{
	return (unsigned long)((((uintptr_t)resource >> 4) * 2654435761UL) % MCC_IMAGE_BUCKET_COUNT);
}

static struct mcc_image *mcc_image_find(const DWORD *resource, BOOL create)
{
	struct mcc_image **bucket = &mcc_image_buckets[mcc_image_bucket(resource)];
	struct mcc_image *entry;

	for (entry = *bucket; entry; entry = entry->next)
	{
		if (entry->resource == resource)
			return entry;
	}
	if (!create || !(entry = calloc(1, sizeof(*entry))))
		return NULL;
	entry->resource = resource;
	entry->next = *bucket;
	*bucket = entry;
	return entry;
}

static unsigned long mcc_palette_hash(const D3DCOLOR *palette)
{
	unsigned long hash = 2166136261UL;
	int index;

	if (!palette)
		return 0;
	for (index = 0; index < 256; index++)
		hash = (hash ^ (unsigned long)palette[index]) * 16777619UL;
	return hash | 1;
}

/* the RGBA bytes of an MCC texture's first level (malloc's), NULL for one
with no image (a cube map, a volume, a P8 texture with no palette, texels
short of the level) or no memory */
static unsigned char *mcc_image_decode(const struct xgpu_texture_description *description, const void *pixels,
	unsigned long bytes, const D3DCOLOR *palette)
{
	unsigned long count = description->width * description->height, index;
	DWORD *texels;

	if (description->cube_map || description->depth > 1 || !description->width || !description->height ||
		description->width > 16384 || description->height > 16384 || !pixels ||
		xgpu_texture_level_size(description, 0) > bytes || (description->format == 0x0b && !palette))
	{
		return NULL;
	}
	texels = malloc(count * sizeof(DWORD));
	if (!texels)
		return NULL;
	if (description->compressed)
	{
		xgpu_texture_decode_dxt(description, 0, (const unsigned char *)pixels, texels);
	}
	else if (!xgpu_texture_decode_level(description, 0, (const unsigned char *)pixels, palette, texels))
	{
		free(texels);
		return NULL;
	}
	/* (ARGB words, BGRA bytes, to the images' RGBA) */
	for (index = 0; index < count; index++)
	{
		DWORD value = texels[index];

		texels[index] = (value & 0xff00ff00UL) | ((value >> 16) & 0xffUL) | ((value & 0xffUL) << 16);
	}
	return (unsigned char *)texels;
}

/* the entry's image made (or made again for another palette); FALSE if it
has none */
static BOOL mcc_image_make(struct mcc_image *entry, const struct xgpu_texture_description *description,
	const void *pixels, unsigned long bytes, const D3DCOLOR *palette)
{
	unsigned long hash = description->format == 0x0b ? mcc_palette_hash(palette) : 0;
	unsigned char *rgba;

	if (entry->image && entry->palette_hash == hash)
		return TRUE;
	if (entry->failed && entry->palette_hash == hash)
		return FALSE;
	xgpu_image_delete(entry->image);
	entry->image = 0;
	entry->palette_hash = hash;
	entry->failed = TRUE;
	rgba = mcc_image_decode(description, pixels, bytes, palette);
	if (!rgba)
	{
		platform_log("mcc textures: a %lux%lux%lu texture of format %02lx%s is not drawn", description->width,
			description->height, description->depth, (unsigned long)description->format,
			description->cube_map ? " (a cube map)" : description->depth > 1 ? " (a volume)" :
			description->format == 0x0b && !palette ? " (P8 with no palette)" : "");
		return FALSE;
	}
	/* (its levels made by the host from the first) */
	entry->image = xgpu_image_create(rgba, description->width, description->height,
		description->linear ? 1 : description->levels, XGPU_IMAGE_RGBA, TRUE);
	entry->failed = !entry->image;
	return entry->image != 0;
}

unsigned int xgpu_mcc_texture_image(DWORD resource, DWORD palette, struct xgpu_texture_description *description)
{
	const DWORD *header = (const DWORD *)resource;
	struct xgpu_texture_description mcc;
	struct mcc_image *entry;
	const void *pixels;
	unsigned long bytes, levels;
	long hud_asset;
	unsigned int image;

	if (!header || !mcc_texture_bridge_texels(header, &mcc, &pixels, &bytes, &hud_asset))
		return 0;
	*description = mcc;
	/* (a stock MCC HUD sheet's high-res texture: mcc_texture_bridge.c) */
	if (hud_asset >= 0 && hud_hires_enabled() && (image = hud_hires_override_texture(hud_asset, &levels)) != 0)
	{
		description->levels = levels;
		description->hires = TRUE;
		description->hires_coverage = hud_hires_override_coverage(hud_asset);
		description->hires_point_threshold = hud_hires_override_point_threshold(hud_asset);
		return image;
	}
	entry = mcc_image_find(header, TRUE);
	if (!entry || !mcc_image_make(entry, &mcc, pixels, bytes, (const D3DCOLOR *)palette))
		return 0;
	if (description->linear)
		description->levels = 1;
	return entry->image;
}

unsigned long xgpu_mcc_texture_prepare(const DWORD *resource, unsigned long budget)
{
	struct xgpu_texture_description description;
	struct mcc_image *entry;
	const void *pixels;
	unsigned long bytes;
	long hud_asset;

	if (!resource || !mcc_texture_bridge_texels(resource, &description, &pixels, &bytes, &hud_asset) ||
		description.format == 0x0b || (hud_asset >= 0 && hud_hires_enabled()) ||
		(unsigned long long)description.width * description.height * 4 > budget)
	{
		return 0;
	}
	entry = mcc_image_find(resource, TRUE);
	if (!entry || entry->image || entry->failed || !mcc_image_make(entry, &description, pixels, bytes, NULL))
		return 0;
	return description.width * description.height * 4;
}

void xgpu_mcc_texture_forget(const DWORD *resource)
{
	struct mcc_image **link = &mcc_image_buckets[mcc_image_bucket(resource)];

	while (*link)
	{
		struct mcc_image *entry = *link;

		if (entry->resource == resource)
		{
			*link = entry->next;
			xgpu_image_delete(entry->image);
			free(entry);
			return;
		}
		link = &entry->next;
	}
}
