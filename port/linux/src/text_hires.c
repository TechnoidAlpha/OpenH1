/*
TEXT_HIRES.C

The game's text drawn with fonts at the display's resolution (text_hires.h).

Each font tag the fonts draw (port/assets/fonts/fonts.json) gets its font
sized so that its capitals are as tall as the tag's, and its fallback, the
font that draws a character the first has not (Overpass, for the few that
Cyborg Interface Gothic lacks), sized and fitted the same way: the game's layout,
from the tag's character widths, is kept, and the fonts were chosen to fit
the Xbox maps' widths. A tag whose letters are narrower (the fonts of a
Custom Edition loc.map) gets its font narrowed until its letters are, on
the whole, as wide as the tag's, so that they do not run into each other;
the Xbox maps' fonts are narrowed by a few hundredths.

The glyphs are drawn from multi-channel signed distance fields
(port/third_party/msdfgen/msdf_gen.h): the shipped fonts' characters baked
into an atlas with the build (tools/msdf_atlas.py, embedded), any other made
on the thread that draws the text as the first string that has it is drawn
(rasterizer_text.c makes a string's glyphs before it draws any of them:
text_hires_rasterize; a millisecond or two, once) into the room below them.
A character neither font has is drawn as the font's missing glyph; a string
is never drawn from its font tag's bitmaps for want of a glyph. The fields
are for every scale: the display's size changes nothing. The shader takes
the median of the three channels and smooths its edge over a pixel of the
screen (xgpu_backend.h's XGPU_ADDRESS_MSDF). Each glyph's quad, advance and
ink are the font's box for it rasterized at the display's pixels
(stbtt_GetGlyphBitmapBox), so that the text is laid out and cut as it was
when it was rasterized (tools/text_msdf/layout.c). stb_truetype reads the
fonts: their metrics, and the outlines of the glyphs made at run time.

The atlas is an image (xgpu_images.h) whose pixels the host reads a frame
or two after the game draws with it: glyphs are only ever added where
nothing drawn is, and when the glyphs made at run time are let go (the
atlas full) it is a new image, the old one kept until the frames that drew
it are done (xgpu_image_delete).
*/

#include "text_hires.h"
#include "embedded_assets.h"
#include "platform.h"
#include "port_config.h"
/* (the renderer's backend is on the host: the pictures are images) */
#include "xgpu_images.h"
#include "xgpu_shader.h"

#include <math.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#include "../../third_party/msdfgen/msdf_gen.h"

/* (the Xbox maps' tags and a Custom Edition loc.map's, which share names,
each at its oversamples) */
#define MAXIMUM_FONTS 16

/* a font file as it draws a tag (none where scale is 0) */
struct font_face
{
	stbtt_fontinfo info;
	/* font units to units of the 480 lines */
	float scale;
	/* the glyphs' widths, against their heights, to fit the tag's letters */
	float narrowing;
	/* the font file's, among the atlas's */
	const char *file;
	int msdf_file;
};

/* the font, and the fallback that draws the characters it has not */
#define FONT_FACES 2

static struct
{
	char tag[64];
	/* the tag's measures it was made for */
	float cap_height;
	long letters_width;
	struct font_face faces[FONT_FACES];
	/* how many times the display's pixels its glyphs are laid out at */
	float oversample;
} fonts[MAXIMUM_FONTS];
static long font_count;

static unsigned long placeholder_data, placeholder_width, placeholder_height;

/* tag names compared as the game does, ignoring case (and without the POSIX
strcasecmp, which the Windows build has not) */
static int names_differ(const char *a, const char *b)
{
	while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return tolower((unsigned char)*a) != tolower((unsigned char)*b);
}

int text_hires_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.high_res_text");
	}
	return enabled;
}

static void (*reset_hook)(void);

void text_hires_set_reset_hook(void (*hook)(void))
{
	reset_hook = hook;
}

/* the face of a font that draws a character, and the character's glyph in
it: the font's own, else its fallback's; else the font's missing glyph */
static struct font_face *font_glyph(long font, unsigned long code, int *index)
{
	int face;

	for (face = 0; face < FONT_FACES; face++)
	{
		if (fonts[font].faces[face].scale > 0.0f &&
			(*index = stbtt_FindGlyphIndex(&fonts[font].faces[face].info, (int)code)) != 0)
		{
			return &fonts[font].faces[face];
		}
	}
	*index = 0;
	return &fonts[font].faces[0];
}

/* a font file made a face that draws a tag: sized so that its capitals are
cap_height units tall, narrowed where its letters are wider than the tag's
(letter_widths); FALSE (and no face) if it cannot be read */
static int face_make(struct font_face *face, const unsigned char *data, float cap_height, short const *letter_widths)
{
	float tag_width = 0.0f;
	float font_width = 0.0f;
	int x0, y0, x1, y1;
	int letter;

	face->scale = 0.0f;
	face->narrowing = 1.0f;
	face->msdf_file = -1;
	if (!data || !stbtt_InitFont(&face->info, data, stbtt_GetFontOffsetForIndex(data, 0)) ||
		!stbtt_GetCodepointBox(&face->info, 'H', &x0, &y0, &x1, &y1) || y1 <= y0)
	{
		return 0;
	}
	face->scale = cap_height / (float)(y1 - y0);
	for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
	{
		int glyph = stbtt_FindGlyphIndex(&face->info, letter < 26 ? 'A' + letter : 'a' + letter - 26);
		int advance = 0, bearing = 0;

		if (letter_widths[letter] <= 0 || !glyph)
			continue;
		stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &bearing);
		tag_width += letter_widths[letter];
		font_width += advance * face->scale;
	}
	if (tag_width > 0.0f && font_width > tag_width)
		face->narrowing = tag_width / font_width;
	return 1;
}

/* ---------- the distance fields */

#define MSDF_ATLAS_WIDTH 1024
#define MSDF_ATLAS_HEIGHT 1024
#define MSDF_FILES 16
#define MSDF_GLYPHS 4096

/* a glyph's field in the atlas, and its edges in font units (no field: the
glyph has no ink) */
struct msdf_glyph
{
	short file;
	unsigned long code;
	short x, y, width, height;
	float left, top;
};

static struct
{
	/* tried to read the baked atlas; read it */
	int read, ready;
	unsigned char *pixels;
	float range, em;
	struct
	{
		char name[48];
		float scale;
	} files[MSDF_FILES];
	int file_count;
	/* the baked glyphs, and those made since (from baked_count) */
	struct msdf_glyph glyphs[MSDF_GLYPHS];
	int baked_count, count;
	/* (glyph numbers + 1, by file and code) */
	short table[MSDF_GLYPHS * 2];
	/* where the next made glyph goes, below the baked ones */
	int baked_height, pack_x, pack_y, pack_row_height;
	unsigned int image;
	int changed;
} msdf;

static unsigned long msdf_hash(int file, unsigned long code)
{
	return ((unsigned long)file * 2654435761UL ^ code * 40503UL) & (MSDF_GLYPHS * 2 - 1);
}

static void msdf_table_add(int glyph)
{
	unsigned long slot = msdf_hash(msdf.glyphs[glyph].file, msdf.glyphs[glyph].code);

	while (msdf.table[slot])
		slot = (slot + 1) & (MSDF_GLYPHS * 2 - 1);
	msdf.table[slot] = (short)(glyph + 1);
}

static struct msdf_glyph *msdf_table_find(int file, unsigned long code)
{
	unsigned long slot = msdf_hash(file, code);

	while (msdf.table[slot])
	{
		struct msdf_glyph *glyph = &msdf.glyphs[msdf.table[slot] - 1];

		if (glyph->file == file && glyph->code == code)
			return glyph;
		slot = (slot + 1) & (MSDF_GLYPHS * 2 - 1);
	}
	return NULL;
}

/* the glyphs made at run time let go (their room taken again), the baked
ones kept: a new image (the old one's pixels kept for the frames that drew
them, the host reading them a frame or two late: xgpu_image_delete) */
static void msdf_made_reset(void)
{
	int glyph;

	if (reset_hook)
		reset_hook();
	memset(msdf.table, 0, sizeof(msdf.table));
	msdf.count = msdf.baked_count;
	for (glyph = 0; glyph < msdf.count; glyph++)
		msdf_table_add(glyph);
	if (msdf.image && msdf.pixels)
	{
		unsigned char *pixels = calloc((size_t)MSDF_ATLAS_WIDTH * MSDF_ATLAS_HEIGHT, 4);

		if (pixels)
			memcpy(pixels, msdf.pixels, (size_t)msdf.baked_height * MSDF_ATLAS_WIDTH * 4);
		xgpu_image_delete(msdf.image);
		msdf.image = 0;
		msdf.pixels = pixels;
	}
	else if (msdf.pixels)
	{
		memset(msdf.pixels + (size_t)msdf.baked_height * MSDF_ATLAS_WIDTH * 4, 0,
			(size_t)(MSDF_ATLAS_HEIGHT - msdf.baked_height) * MSDF_ATLAS_WIDTH * 4);
	}
	msdf.pack_x = msdf.pack_row_height = 0;
	msdf.pack_y = msdf.baked_height;
	msdf.changed = 1;
}

static unsigned int read16(const unsigned char *at)
{
	return (unsigned int)at[0] | (unsigned int)at[1] << 8;
}

static unsigned long read32(const unsigned char *at)
{
	return (unsigned long)read16(at) | (unsigned long)read16(at + 2) << 16;
}

static float read_float(const unsigned char *at)
{
	unsigned int bits = (unsigned int)read32(at);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* the baked atlas read (tools/msdf_atlas/bake.c's form), once; FALSE if it
cannot be */
static int msdf_read(void)
{
	unsigned char *raw = NULL;
	uLongf size;
	unsigned long glyph_count, index, offset, width, height;

	if (msdf.read)
		return msdf.ready;
	msdf.read = 1;
	if (!text_hires_msdf_size)
		return 0;
	/* (no more than the most it can hold) */
	size = 26 + MSDF_FILES * 52 + MSDF_GLYPHS * 24 + (uLongf)MSDF_ATLAS_WIDTH * MSDF_ATLAS_HEIGHT * 4;
	raw = malloc(size);
	if (!raw || uncompress(raw, &size, (const unsigned char *)embedded_assets_data(text_hires_msdf_offset,
		text_hires_msdf_size), text_hires_msdf_size) != Z_OK || size < 26 || memcmp(raw, "OHMS", 4) ||
		read32(raw + 4) != 1)
	{
		platform_log("high-res text: the MSDF atlas cannot be read");
		free(raw);
		return 0;
	}
	width = read16(raw + 8);
	height = read16(raw + 10);
	msdf.range = read_float(raw + 12);
	msdf.em = read_float(raw + 16);
	msdf.file_count = (int)read16(raw + 20);
	offset = 22;
	if (width != MSDF_ATLAS_WIDTH || height > MSDF_ATLAS_HEIGHT || msdf.range != XGPU_MSDF_RANGE ||
		msdf.file_count > MSDF_FILES || offset + (unsigned long)msdf.file_count * 52 + 4 > size)
	{
		free(raw);
		return 0;
	}
	for (index = 0; index < (unsigned long)msdf.file_count; index++, offset += 52)
	{
		memcpy(msdf.files[index].name, raw + offset, sizeof(msdf.files[index].name));
		msdf.files[index].name[sizeof(msdf.files[index].name) - 1] = 0;
		msdf.files[index].scale = read_float(raw + offset + 48);
	}
	glyph_count = read32(raw + offset);
	offset += 4;
	if (glyph_count > MSDF_GLYPHS || offset + glyph_count * 24 + width * height * 4 != size)
	{
		free(raw);
		return 0;
	}
	for (index = 0; index < glyph_count; index++, offset += 24)
	{
		struct msdf_glyph *glyph = &msdf.glyphs[index];

		glyph->file = (short)read16(raw + offset);
		glyph->code = read32(raw + offset + 4);
		glyph->x = (short)read16(raw + offset + 8);
		glyph->y = (short)read16(raw + offset + 10);
		glyph->width = (short)read16(raw + offset + 12);
		glyph->height = (short)read16(raw + offset + 14);
		glyph->left = read_float(raw + offset + 16);
		glyph->top = read_float(raw + offset + 20);
		if (glyph->file >= msdf.file_count || glyph->x + glyph->width > (long)width ||
			glyph->y + glyph->height > (long)height)
		{
			free(raw);
			return 0;
		}
	}
	msdf.pixels = calloc((size_t)MSDF_ATLAS_WIDTH * MSDF_ATLAS_HEIGHT, 4);
	if (!msdf.pixels)
	{
		free(raw);
		return 0;
	}
	memcpy(msdf.pixels, raw + offset, (size_t)width * height * 4);
	free(raw);
	msdf.baked_count = (int)glyph_count;
	/* (a texel's row between the baked glyphs and the made ones) */
	msdf.baked_height = (int)height + 1;
	msdf_made_reset();
	msdf.ready = 1;
	platform_log("high-res text: MSDF atlas of %lu glyphs, %lux%lu", glyph_count, width, height);
	return 1;
}

/* the atlas's number for a face's font file (one the atlas has not added,
its fields made at run time) */
static int msdf_face_file(struct font_face *face)
{
	int file;

	if (face->msdf_file >= 0)
		return face->msdf_file;
	for (file = 0; file < msdf.file_count; file++)
	{
		if (!names_differ(msdf.files[file].name, face->file))
			return face->msdf_file = file;
	}
	if (msdf.file_count >= MSDF_FILES)
		return -1;
	snprintf(msdf.files[file].name, sizeof(msdf.files[file].name), "%s", face->file);
	msdf.files[file].scale = stbtt_ScaleForMappingEmToPixels(&face->info, msdf.em);
	msdf.file_count++;
	return face->msdf_file = file;
}

/* a glyph's field made into the atlas, on the game's thread: NULL if there
is no room even once the made ones are let go */
static struct msdf_glyph *msdf_make(struct font_face *face, int file, unsigned long code, int index)
{
	struct msdf_glyph *glyph;
	stbtt_vertex *vertices = NULL;
	struct msdf_vertex *shape;
	unsigned char *field;
	float scale = msdf.files[file].scale;
	int x0, y0, x1, y1, count, vertex, row, attempt;
	int pad = (int)ceilf(msdf.range * 0.5f) + 1, width, height, made;

	if (!msdf.pixels)
		return NULL;
	if (msdf.count >= MSDF_GLYPHS)
		msdf_made_reset();
	if (msdf.count >= MSDF_GLYPHS)
		return NULL;
	glyph = &msdf.glyphs[msdf.count];
	memset(glyph, 0, sizeof(*glyph));
	glyph->file = (short)file;
	glyph->code = code;
	if (!stbtt_GetGlyphBox(&face->info, index, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0)
	{
		/* (no ink) */
		msdf_table_add(msdf.count++);
		return glyph;
	}
	width = (int)ceilf((x1 - x0) * scale) + 2 * pad;
	height = (int)ceilf((y1 - y0) * scale) + 2 * pad;
	if (width + 1 > MSDF_ATLAS_WIDTH || height + 1 > MSDF_ATLAS_HEIGHT - msdf.baked_height)
		return NULL;
	for (attempt = 0; attempt < 2; attempt++)
	{
		if (msdf.pack_x + width > MSDF_ATLAS_WIDTH)
		{
			msdf.pack_x = 0;
			msdf.pack_y += msdf.pack_row_height + 1;
			msdf.pack_row_height = 0;
		}
		if (msdf.pack_y + height <= MSDF_ATLAS_HEIGHT)
			break;
		msdf_made_reset();
		glyph = &msdf.glyphs[msdf.count];
		memset(glyph, 0, sizeof(*glyph));
		glyph->file = (short)file;
		glyph->code = code;
	}
	glyph->x = (short)msdf.pack_x;
	glyph->y = (short)msdf.pack_y;
	glyph->width = (short)width;
	glyph->height = (short)height;
	glyph->left = (float)x0 - pad / scale;
	glyph->top = (float)y1 + pad / scale;
	count = stbtt_GetGlyphShape(&face->info, index, &vertices);
	shape = malloc(sizeof(*shape) * (size_t)(count > 0 ? count : 1));
	field = malloc((size_t)width * height * 4);
	made = 0;
	if (shape && field)
	{
		for (vertex = 0; vertex < count; vertex++)
		{
			shape[vertex].type = vertices[vertex].type;
			shape[vertex].x = vertices[vertex].x;
			shape[vertex].y = vertices[vertex].y;
			shape[vertex].cx = vertices[vertex].cx;
			shape[vertex].cy = vertices[vertex].cy;
			shape[vertex].cx1 = vertices[vertex].cx1;
			shape[vertex].cy1 = vertices[vertex].cy1;
		}
		made = msdf_generate(shape, count, scale, glyph->left, glyph->top, msdf.range, width, height, field);
	}
	if (made)
	{
		for (row = 0; row < height; row++)
			memcpy(msdf.pixels + ((size_t)(glyph->y + row) * MSDF_ATLAS_WIDTH + glyph->x) * 4,
				field + (size_t)row * width * 4, (size_t)width * 4);
		msdf.pack_x += width + 1;
		if (height > msdf.pack_row_height)
			msdf.pack_row_height = height;
		msdf_table_add(msdf.count++);
		msdf.changed = 1;
	}
	stbtt_FreeShape(&face->info, vertices);
	free(shape);
	free(field);
	return made ? glyph : NULL;
}

/* a character's field: baked, made already, or made now (the font's missing
glyph's, under code 0, for one neither font has); NULL if none can be */
static struct msdf_glyph *msdf_glyph(long font, unsigned long code, struct font_face **face_used, int *index)
{
	struct font_face *face = font_glyph(font, code, index);
	struct msdf_glyph *glyph;
	unsigned long key = *index ? code : 0;
	int file;

	if (!msdf_read())
		return NULL;
	file = msdf_face_file(face);
	if (file < 0)
		return NULL;
	*face_used = face;
	glyph = msdf_table_find(file, key);
	return glyph ? glyph : msdf_make(face, file, key, *index);
}

/* a question mark's side bearings, in font units, cut to an exclamation
mark's where its font draws it in the middle of a whole em (Cyborg Interface
Gothic's, a fifth of an em of room either side), which set it apart from the
word it ends ("ARE YOU SURE ?", #901): how much less room is left of its ink
(*left) and in all (the return), 0 for a glyph left as it is */
static int glyph_trim(stbtt_fontinfo const *info, unsigned long code, int index, int advance, int bearing, int *left)
{
	int mark = stbtt_FindGlyphIndex(info, '!');
	int ink_x0 = 0, ink_x1 = 0, mark_x0 = 0, mark_x1 = 0, mark_advance = 0, mark_bearing = 0, right;
	int em = info->numGlyphs > 0 ? (int)(1.0f / stbtt_ScaleForMappingEmToPixels(info, 1.0f) + 0.5f) : 0;

	*left = 0;
	if (code != '?' || !index || !mark || em <= 0 || bearing <= em / 8 ||
		!stbtt_GetGlyphBox(info, index, &ink_x0, NULL, &ink_x1, NULL) ||
		!stbtt_GetGlyphBox(info, mark, &mark_x0, NULL, &mark_x1, NULL))
	{
		return 0;
	}
	stbtt_GetGlyphHMetrics(info, mark, &mark_advance, &mark_bearing);
	right = advance - ink_x1 - (mark_advance - mark_x1);
	*left = bearing - mark_bearing > 0 ? bearing - mark_bearing : 0;
	return *left + (right > 0 ? right : 0);
}

/* text_hires_glyph */
static int msdf_text_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	struct font_face *face = NULL;
	int index = 0, x0, y0, x1, y1, advance = 0, bearing = 0, ink_x0 = 0, ink_x1 = 0, cut, cut_left;
	float shift;
	struct msdf_glyph *field = msdf_glyph(font, code, &face, &index);
	float screen = halo_screen_pixel_scale(), pixels, pixels_x, pixel_scale, scale, u_scale, v_scale;

	if (!field || !field->width || screen <= 0.0f || !placeholder_data)
		return 0;
	/* (the quad the glyph has rasterized at the display's pixels) */
	pixel_scale = screen * fonts[font].oversample;
	pixels = face->scale * screen * fonts[font].oversample;
	pixels_x = pixels * face->narrowing;
	stbtt_GetGlyphBitmapBox(&face->info, index, pixels_x, pixels, &x0, &y0, &x1, &y1);
	if (x1 <= x0 || y1 <= y0)
		return 0;
	stbtt_GetGlyphHMetrics(&face->info, index, &advance, &bearing);
	glyph->left = x0 / pixel_scale;
	glyph->top = y0 / pixel_scale;
	glyph->right = x1 / pixel_scale;
	glyph->bottom = y1 / pixel_scale;
	glyph->advance = advance * face->scale * face->narrowing;
	if (stbtt_GetGlyphBox(&face->info, index, &ink_x0, NULL, &ink_x1, NULL) && ink_x1 > ink_x0)
	{
		glyph->ink_left = (float)ink_x0 * pixels_x / pixel_scale;
		glyph->ink_right = (float)ink_x1 * pixels_x / pixel_scale;
	}
	else
	{
		glyph->ink_left = glyph->left;
		glyph->ink_right = glyph->right;
	}
	/* (a question mark's room either side cut: glyph_trim) */
	cut = glyph_trim(&face->info, code, index, advance, bearing, &cut_left);
	if (cut)
	{
		shift = cut_left * face->scale * face->narrowing;
		glyph->left -= shift;
		glyph->right -= shift;
		glyph->ink_left -= shift;
		glyph->ink_right -= shift;
		glyph->advance -= cut * face->scale * face->narrowing;
	}
	/* (the quad's corners in the field: font units, y up, from the pixels'
	box, y down) */
	scale = msdf.files[field->file].scale;
	u_scale = (float)placeholder_width / MSDF_ATLAS_WIDTH;
	v_scale = (float)placeholder_height / MSDF_ATLAS_HEIGHT;
	glyph->u0 = (field->x + (x0 / pixels_x - field->left) * scale) * u_scale;
	glyph->u1 = (field->x + (x1 / pixels_x - field->left) * scale) * u_scale;
	glyph->v0 = (field->y + (field->top + y0 / pixels) * scale) * v_scale;
	glyph->v1 = (field->y + (field->top + y1 / pixels) * scale) * v_scale;
	return 1;
}

long text_hires_font(char const *tag_name, float cap_height, short const *letter_widths, float oversample)
{
	unsigned int index;
	long font;
	long letters_width = 0;
	int letter;

	/* (without the atlas, the maps' bitmap fonts) */
	if (!text_hires_enabled() || !tag_name || cap_height <= 0.0f || !letter_widths || !msdf_read())
		return -1;
	/* (text is never drawn with fewer pixels than the display's) */
	if (oversample < 1.0f)
		oversample = 1.0f;
	for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
		letters_width += letter_widths[letter] > 0 ? letter_widths[letter] : 0;
	for (font = 0; font < font_count; font++)
	{
		if (!names_differ(fonts[font].tag, tag_name) && fonts[font].cap_height == cap_height &&
			fonts[font].letters_width == letters_width && fonts[font].oversample == oversample)
		{
			return fonts[font].faces[0].scale > 0.0f ? font : -1;
		}
	}
	if (font_count >= MAXIMUM_FONTS)
		return -1;
	font = font_count++;
	snprintf(fonts[font].tag, sizeof(fonts[font].tag), "%s", tag_name);
	fonts[font].cap_height = cap_height;
	fonts[font].letters_width = letters_width;
	memset(fonts[font].faces, 0, sizeof(fonts[font].faces));
	fonts[font].oversample = oversample;
	for (index = 0; index < text_hires_embedded_count; index++)
	{
		const struct text_hires_embedded *embedded = &text_hires_embedded[index];

		if (!embedded->tag || names_differ(embedded->tag, tag_name))
			continue;
		fonts[font].faces[0].file = embedded->file;
		fonts[font].faces[1].file = embedded->fallback_file;
		if (!face_make(&fonts[font].faces[0],
			(const unsigned char *)embedded_assets_data(embedded->data_offset, embedded->size), cap_height,
			letter_widths))
		{
			platform_log("high-res text: could not read %s for %s", embedded->file, tag_name);
			break;
		}
		/* (its fallback: one that cannot be read draws nothing, a
		character the font lacks drawn as its missing glyph, as without one) */
		if (embedded->fallback_file && !face_make(&fonts[font].faces[1],
			(const unsigned char *)embedded_assets_data(embedded->fallback_data_offset, embedded->fallback_size),
			cap_height, letter_widths))
		{
			platform_log("high-res text: could not read %s for %s", embedded->fallback_file, tag_name);
		}
		platform_log("high-res text: %s (capitals %.0f units) drawn with %s, %.0f%% wide%s%s",
			tag_name, cap_height, embedded->file, fonts[font].faces[0].narrowing * 100.0f,
			fonts[font].faces[1].scale > 0.0f ? ", what it lacks with " : "",
			fonts[font].faces[1].scale > 0.0f ? embedded->fallback_file : "");
		break;
	}
	if (fonts[font].faces[0].scale <= 0.0f)
		return -1;
	return font;
}

void text_hires_rasterize(long font, unsigned long code)
{
	struct font_face *face;
	int index;

	if (font < 0 || font >= font_count || fonts[font].faces[0].scale <= 0.0f || code <= ' ')
		return;
	msdf_glyph(font, code, &face, &index);
}

int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	if (font < 0 || font >= font_count || fonts[font].faces[0].scale <= 0.0f || code <= ' ')
		return 0;
	return msdf_text_glyph(font, code, glyph);
}

float text_hires_advance(long font, unsigned long code)
{
	struct font_face *face;
	int index, advance = 0, bearing = 0, cut_left;

	if (font < 0 || font >= font_count || fonts[font].faces[0].scale <= 0.0f)
		return -1.0f;
	face = font_glyph(font, code, &index);
	stbtt_GetGlyphHMetrics(&face->info, index, &advance, &bearing);
	return (advance - glyph_trim(&face->info, code, index, advance, bearing, &cut_left)) * face->scale *
		face->narrowing;
}

float text_hires_kerning(long font, unsigned long first, unsigned long second)
{
	struct font_face *face;
	int first_index, second_index;

	if (font < 0 || font >= font_count || fonts[font].faces[0].scale <= 0.0f)
		return 0.0f;
	face = font_glyph(font, first, &first_index);
	if (!first_index || face != font_glyph(font, second, &second_index) || !second_index)
		return 0.0f;
	if (!face->info.kern && !face->info.gpos)
		return 0.0f;
	return stbtt_GetGlyphKernAdvance(&face->info, first_index, second_index) * face->scale * face->narrowing;
}

void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height)
{
	placeholder_data = texture ? texture[1] : 0;
	placeholder_width = width;
	placeholder_height = height;
	xgpu_texture_replacements_changed();
}

int text_hires_atlas_placeholder(unsigned long data)
{
	return data && data == placeholder_data;
}

unsigned int text_hires_atlas_texture(unsigned long data)
{
	if (!data || data != placeholder_data)
		return 0;
	/* (the fields' atlas, which its image owns once it is made: a new one
	each time the glyphs made at run time are let go, msdf_made_reset) */
	if (!msdf.image && msdf.pixels)
	{
		msdf.image = xgpu_image_create(msdf.pixels, MSDF_ATLAS_WIDTH, MSDF_ATLAS_HEIGHT, 1, XGPU_IMAGE_RGBA, TRUE);
		msdf.changed = 0;
	}
	if (msdf.changed && msdf.image)
	{
		xgpu_image_changed(msdf.image);
		msdf.changed = 0;
	}
	return msdf.image;
}

unsigned char *text_hires_line(char const *tag_name, char const *text, float cap_pixels,
	int *width, int *height)
{
	stbtt_fontinfo info;
	const unsigned char *data = NULL;
	unsigned char *pixels;
	unsigned int index;
	int x0, y0, x1, y1, pass, w = 0, h, pen, baseline, digits = 0, run = 0, digit;
	float scale, cell = 0.0f;
	const char *c;

	*width = *height = 0;
	if (!tag_name || !text || cap_pixels <= 0.0f || cap_pixels > 512.0f)
		return NULL;
	for (index = 0; index < text_hires_embedded_count && !data; index++)
		if (text_hires_embedded[index].tag && !names_differ(text_hires_embedded[index].tag, tag_name))
			data = (const unsigned char *)embedded_assets_data(text_hires_embedded[index].data_offset,
				text_hires_embedded[index].size);
	if (!data || !stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0)) ||
		!stbtt_GetCodepointBox(&info, 'H', &x0, &y0, &x1, &y1) || y1 <= y0)
		return NULL;
	scale = cap_pixels / (float)(y1 - y0);
	/* (a number's cells: the widest digit's advance, as many as the line's
	longest number has digits) */
	for (digit = '0'; digit <= '9'; digit++)
	{
		int advance, bearing;

		stbtt_GetCodepointHMetrics(&info, digit, &advance, &bearing);
		cell = (float)advance * scale > cell ? (float)advance * scale : cell;
	}
	for (c = text; *c; c++)
	{
		run = *c >= '0' && *c <= '9' ? run + 1 : 0;
		digits = run > digits ? run : digits;
	}
	/* (room above the capitals for accents, below the baseline for
	descenders: the font's own ascent and descent are not to be trusted
	for it, OpenCE's being smaller than its capitals) */
	baseline = (int)ceilf(cap_pixels * 1.3f);
	h = baseline + (int)ceilf(cap_pixels * 0.4f) + 1;
	/* (twice: the width, then the glyphs) */
	pixels = NULL;
	for (pass = 0; pass < 2; pass++)
	{
		const unsigned char *p = (const unsigned char *)text;
		float x = 1.0f;
		int previous = 0;

		while (*p)
		{
			int code = *p++, advance, bearing, number = code >= '0' && code <= '9';
			float place;

			/* (UTF-8's two- and three-byte letters; others as Latin-1) */
			if ((code & 0xe0) == 0xc0 && (p[0] & 0xc0) == 0x80)
				code = ((code & 0x1f) << 6) | (*p++ & 0x3f);
			else if ((code & 0xf0) == 0xe0 && (p[0] & 0xc0) == 0x80 && (p[1] & 0xc0) == 0x80)
			{
				code = ((code & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
				p += 2;
			}
			/* (a number's first digit: the cells its number has not, before
			it; no kerning into a digit or out of one) */
			if (number && !(previous >= '0' && previous <= '9'))
			{
				const unsigned char *q = p;

				for (run = 1; *q >= '0' && *q <= '9'; q++)
					run++;
				x += (float)(digits - run) * cell;
			}
			if (previous && !number && !(previous >= '0' && previous <= '9'))
				x += (float)stbtt_GetCodepointKernAdvance(&info, previous, code) * scale;
			stbtt_GetCodepointHMetrics(&info, code, &advance, &bearing);
			place = number ? x + (cell - (float)advance * scale) * 0.5f : x;
			if (pass)
			{
				int left, top, right, bottom;
				float shift = place - floorf(place);

				unsigned char *glyph;

				stbtt_GetCodepointBitmapBoxSubpixel(&info, code, scale, scale, shift, 0.0f, &left, &top, &right, &bottom);
				pen = (int)floorf(place) + left;
				glyph = right > left && bottom > top ? calloc((size_t)(right - left) * (bottom - top), 1) : NULL;
				if (glyph)
				{
					int gx, gy;

					stbtt_MakeCodepointBitmapSubpixel(&info, glyph, right - left, bottom - top, right - left,
						scale, scale, shift, 0.0f, code);
					/* (each pixel the most of the glyphs over it, the line's
					edges clipping them) */
					for (gy = 0; gy < bottom - top; gy++)
						for (gx = 0; gx < right - left; gx++)
						{
							int px = pen + gx, py = baseline + top + gy;
							unsigned char value = glyph[gy * (right - left) + gx];

							if (px >= 0 && px < w && py >= 0 && py < h && value > pixels[py * w + px])
								pixels[py * w + px] = value;
						}
					free(glyph);
				}
			}
			x += number ? cell : (float)advance * scale;
			previous = code;
		}
		if (!pass)
		{
			w = (int)ceilf(x) + 2;
			if (w <= 2 || w > 8192)
				return NULL;
			pixels = calloc((size_t)w * (size_t)h, 1);
			if (!pixels)
				return NULL;
		}
	}
	*width = w;
	*height = h;
	return pixels;
}
