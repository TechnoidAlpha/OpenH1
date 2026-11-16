/*
LAYOUT.C (test)

The high-res text's glyphs laid out where the fonts put them at the
display's pixels (port/linux/src/text_hires.c): each character's quad,
advance and ink, from text_hires_glyph, against stb_truetype's own box for
it rasterized at the display's pixels (the reference: the font sized and
narrowed to its tag as text_hires.c's face_make does, its glyph from the
font or its fallback), at the display's scales of 720p, 1080p and 4K and
drawn twice as large, for each of the shipped fonts' tags: the positions
the rasterized text had, before the distance fields drew every letter. The
baked characters, and some the atlas has not (made at run time) and the
missing glyph's. tools/test_text_msdf.py builds it with text_hires.c and
stand-ins for the platform (generated.c: the embedded fonts and atlas, in a
blob it reads).
*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "text_hires.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"

/* ---------- stand-ins */

static unsigned char *blob;
static float screen_scale = 2.25f;

const void *embedded_assets_data(unsigned int offset, unsigned int size)
{
	(void)size;
	return blob + offset;
}

unsigned long config_changes(void)
{
	return 1;
}

int config_boolean(const char *name)
{
	(void)name;
	return 1;
}

const char *config_string(const char *name)
{
	(void)name;
	return "";
}

float halo_screen_pixel_scale(void)
{
	return screen_scale;
}

void platform_log(const char *format, ...)
{
	(void)format;
}

unsigned int xgpu_image_create(void *pixels, unsigned long width, unsigned long height, unsigned long levels,
	int format, int owned)
{
	(void)pixels; (void)width; (void)height; (void)levels; (void)format; (void)owned;
	return 1;
}

void xgpu_image_changed(unsigned int image)
{
	(void)image;
}

void xgpu_image_delete(unsigned int image)
{
	(void)image;
}

/* ---------- the reference: the fonts' own boxes at the display's pixels */

struct face
{
	stbtt_fontinfo info;
	float scale, narrowing;
};

static int names_differ(const char *a, const char *b)
{
	while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
	{
		a++;
		b++;
	}
	return tolower((unsigned char)*a) != tolower((unsigned char)*b);
}

/* a font sized so that its capitals are cap units tall, narrowed where its
letters are wider than the tag's */
static int face_make(struct face *face, unsigned int offset, float cap, short const *widths)
{
	const unsigned char *data = blob + offset;
	float tag_width = 0.0f, font_width = 0.0f;
	int x0, y0, x1, y1, letter;

	face->scale = 0.0f;
	face->narrowing = 1.0f;
	if (!stbtt_InitFont(&face->info, data, stbtt_GetFontOffsetForIndex(data, 0)) ||
		!stbtt_GetCodepointBox(&face->info, 'H', &x0, &y0, &x1, &y1) || y1 <= y0)
	{
		return 0;
	}
	face->scale = cap / (float)(y1 - y0);
	for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
	{
		int glyph = stbtt_FindGlyphIndex(&face->info, letter < 26 ? 'A' + letter : 'a' + letter - 26);
		int advance = 0, bearing = 0;

		if (widths[letter] <= 0 || !glyph)
			continue;
		stbtt_GetGlyphHMetrics(&face->info, glyph, &advance, &bearing);
		tag_width += widths[letter];
		font_width += advance * face->scale;
	}
	if (tag_width > 0.0f && font_width > tag_width)
		face->narrowing = tag_width / font_width;
	return 1;
}

/* the tag's font and its fallback: how many there are (0: none) */
static int faces_make(const char *tag, float cap, short const *widths, struct face faces[2])
{
	unsigned int index;

	for (index = 0; index < text_hires_embedded_count; index++)
	{
		const struct text_hires_embedded *embedded = &text_hires_embedded[index];

		if (!embedded->tag || names_differ(embedded->tag, tag))
			continue;
		if (!face_make(&faces[0], embedded->data_offset, cap, widths))
			return 0;
		return embedded->fallback_file && face_make(&faces[1], embedded->fallback_data_offset, cap, widths) ? 2 : 1;
	}
	return 0;
}

/* the character's glyph rasterized at the display's pixels (the font's, else
its fallback's, else the font's missing glyph); 0 if it has no ink */
static int reference_glyph(struct face *faces, int count, float oversample, unsigned long code,
	struct text_hires_glyph *glyph)
{
	struct face *face = &faces[0];
	int index = 0, which, x0 = 0, y0 = 0, x1 = 0, y1 = 0, advance = 0, bearing = 0, ink_x0 = 0, ink_x1 = 0;
	float pixels, pixels_x, pixel_scale, ink_left = 0.0f, ink_right = 0.0f;

	for (which = 0; which < count; which++)
	{
		if ((index = stbtt_FindGlyphIndex(&faces[which].info, (int)code)) != 0)
		{
			face = &faces[which];
			break;
		}
	}
	if (which == count)
		index = 0;
	pixels = face->scale * screen_scale * oversample;
	pixels_x = pixels * face->narrowing;
	pixel_scale = screen_scale * oversample;
	stbtt_GetGlyphBitmapBox(&face->info, index, pixels_x, pixels, &x0, &y0, &x1, &y1);
	stbtt_GetGlyphHMetrics(&face->info, index, &advance, &bearing);
	if (stbtt_GetGlyphBox(&face->info, index, &ink_x0, NULL, &ink_x1, NULL))
	{
		ink_left = (float)ink_x0 * pixels_x;
		ink_right = (float)ink_x1 * pixels_x;
	}
	if (x1 <= x0 || y1 <= y0)
		return 0;
	glyph->left = x0 / pixel_scale;
	glyph->top = y0 / pixel_scale;
	glyph->right = x1 / pixel_scale;
	glyph->bottom = y1 / pixel_scale;
	glyph->advance = advance * face->scale * face->narrowing;
	glyph->ink_left = ink_right > ink_left ? ink_left / pixel_scale : glyph->left;
	glyph->ink_right = ink_right > ink_left ? ink_right / pixel_scale : glyph->right;
	return 1;
}

/* ---------- the test */

/* (some the atlas has not, and some no font has: the missing glyph) */
static const unsigned long extra[] = { 0x2026, 0x20ac, 0x2122, 0x100, 0x101, 0x141, 0x3a9, 0x4e2d, 0x1f600 };

static int same(float a, float b)
{
	return a == b;
}

int main(int argc, char **argv)
{
	static const struct
	{
		const char *tag;
		float cap;
		short width;
	} tags[] = {
		{ "ui\\large_ui", 13.0f, 9 }, { "ui\\small_ui", 10.0f, 7 }, { "old tags\\internal system plain", 9.0f, 6 },
		{ "pc\\heading", 13.0f, 9 },
	};
	static const float scales[] = { 1.5f, 2.25f, 4.5f };
	static const float oversamples[] = { 1.0f, 2.0f };
	FILE *file;
	long size;
	int tag, scale, oversample, checked = 0, made = 0, failed = 0;
	unsigned long texture[2] = { 0, 0x1000 };

	if (argc != 2 || !(file = fopen(argv[1], "rb")))
		return 2;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	blob = malloc((size_t)size);
	if (fread(blob, 1, (size_t)size, file) != (size_t)size)
		return 2;
	fclose(file);
	text_hires_register_atlas(texture, 256, 256);
	for (scale = 0; scale < 3; scale++)
	{
		screen_scale = scales[scale];
		for (oversample = 0; oversample < 2; oversample++)
		{
			for (tag = 0; tag < 4; tag++)
			{
				short widths[TEXT_HIRES_FIT_LETTERS];
				struct face faces[2];
				long font;
				unsigned long code;
				int letter, face_count;

				for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
					widths[letter] = (short)(tags[tag].width - (letter >= 26));
				font = text_hires_font(tags[tag].tag, tags[tag].cap, widths, oversamples[oversample]);
				face_count = faces_make(tags[tag].tag, tags[tag].cap, widths, faces);
				if (font < 0 || !face_count)
				{
					printf("FAIL %s: no font\n", tags[tag].tag);
					return 1;
				}
				for (code = 0x21; code < 0x100 + sizeof(extra) / sizeof(extra[0]); code++)
				{
					unsigned long character = code < 0x100 ? code : extra[code - 0x100];
					struct text_hires_glyph reference, drawn;
					int had_reference, had_drawn;

					if (character >= 0x7f && character < 0xa1)
						continue;
					memset(&reference, 0, sizeof(reference));
					memset(&drawn, 0, sizeof(drawn));
					had_reference = reference_glyph(faces, face_count, oversamples[oversample], character, &reference);
					text_hires_rasterize(font, character);
					had_drawn = text_hires_glyph(font, character, &drawn);
					if (had_reference != had_drawn || (had_reference == 1 && (!same(reference.left, drawn.left) ||
						!same(reference.top, drawn.top) || !same(reference.right, drawn.right) ||
						!same(reference.bottom, drawn.bottom) || !same(reference.advance, drawn.advance) ||
						!same(reference.ink_left, drawn.ink_left) || !same(reference.ink_right, drawn.ink_right))))
					{
						printf("FAIL %s U+%04lX at %g x%g: the font's %d (%g %g %g %g, %g, ink %g %g), "
							"drawn %d (%g %g %g %g, %g, ink %g %g)\n", tags[tag].tag, character, scales[scale],
							oversamples[oversample], had_reference, reference.left, reference.top, reference.right,
							reference.bottom, reference.advance, reference.ink_left, reference.ink_right, had_drawn,
							drawn.left, drawn.top, drawn.right, drawn.bottom, drawn.advance, drawn.ink_left,
							drawn.ink_right);
						failed++;
					}
					/* (its quad within the atlas, the right way round) */
					if (had_drawn == 1 && !(drawn.u0 < drawn.u1 && drawn.v0 < drawn.v1 && drawn.u0 >= 0.0f &&
						drawn.v0 >= 0.0f && drawn.u1 <= 256.0f && drawn.v1 <= 256.0f))
					{
						printf("FAIL %s U+%04lX: its field's quad %g %g %g %g\n", tags[tag].tag, character, drawn.u0,
							drawn.v0, drawn.u1, drawn.v1);
						failed++;
					}
					checked += had_drawn == 1;
					made += had_drawn == 1 && code >= 0x100;
				}
			}
		}
	}
	printf("%d glyphs checked, %d of them made at run time, %d failed\n", checked, made, failed);
	return failed != 0;
}
