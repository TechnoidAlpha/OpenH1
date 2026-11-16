/*
XGPU_IMAGES.H

The high-res replacements' pictures (the high-res HUD's and titles'
textures, the menus' art, the high-res text's atlas: hud_hires.c,
menu_files.c, text_hires.c), for the renderer's backend on the host
(xgpu_stream_encode.c): pixels in the game's memory, which the host reads
where they are and makes a texture of (xgpu_stream.h's
XGPU_STREAM_SOURCE_IMAGE). xgpu_images.c.
*/

#ifndef __HALO_LINUX_XGPU_IMAGES_H
#define __HALO_LINUX_XGPU_IMAGES_H

#include "xgpu.h"

/* an image's pixels: 4 bytes each, red, green, blue and alpha (the PNGs'
order) */
enum
{
	XGPU_IMAGE_RGBA,
};

struct xgpu_image
{
	void *pixels;
	unsigned long width, height;
	/* the mip levels drawn from it (the host makes those past the first) */
	unsigned long levels;
	int format;
	/* changes whenever its pixels do (unique among all images') */
	unsigned long serial;
	/* the pixels are the image's, freed with it */
	BOOL owned;
};

/* an image of the first level's pixels (XGPU_IMAGE_*), owned or not: its
number (from 1), or 0 if there is no room */
unsigned int xgpu_image_create(void *pixels, unsigned long width, unsigned long height, unsigned long levels,
	int format, BOOL owned);
/* its pixels have changed */
void xgpu_image_changed(unsigned int image);
/* it is drawn no more (its pixels are freed once the frames that drew it
are done) */
void xgpu_image_delete(unsigned int image);
/* the image of a number (NULL: none) */
const struct xgpu_image *xgpu_image_get(unsigned int image);
/* a frame is submitted (xgpu_stream_encode.c) */
void xgpu_images_frame_end(void);

/* (xbox_textures.c) refresh replacement choices after HUD settings change,
and retire unused generated HUD images */
void xgpu_texture_cache_begin_frame(void);

/* (xbox_textures.c) the image drawn in place of the Xbox texture whose
header is at resource, 0 if none, and its description as the replacement
changes it (its levels, hires and what goes with it); and the channels the
texels at address are sampled from at the stage (a Custom Edition map's:
xgpu_stream.h's texture stage) */
unsigned int xgpu_texture_replacement(DWORD resource, struct xgpu_texture_description *description);
/* (xbox_textures.c) what a texture's replacement is chosen from has changed
(a placeholder or a menu's art registered or let go), on any thread: each
texture's choice is made again at its next bind */
void xgpu_texture_replacements_changed(void);
unsigned long xgpu_texture_channels(unsigned long address, int stage);

#endif
