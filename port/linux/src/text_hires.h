/*
TEXT_HIRES.H

The game's text drawn with fonts at the display's resolution, in place of
the bitmap fonts of the maps (port/assets/fonts, embedded by
tools/embed_assets.py). The game lays its text out as before, from its font
tags' character widths, and each run of it is set with the font's own
advances where the tag's is (rasterizer_text.c's hires_text_glyph_offset);
each character is drawn from a glyph of the font
that stands for its tag (fonts.json), laid out at as many pixels as the
display gives the 480 lines, from its multi-channel signed distance field in
an atlas that the game binds as a placeholder bitmap
(source/rasterizer/rasterizer_text.c).
*/

#ifndef TEXT_HIRES_H
#define TEXT_HIRES_H

/* an embedded font, and the font tag it draws (by name); and the font that
draws a character it has not (none: 0) */
struct text_hires_embedded
{
	const char *tag;
	const char *file;
	/* (its bytes: an offset in the assets' file, embedded_assets.h) */
	unsigned int data_offset;
	unsigned int size;
	const char *fallback_file;
	unsigned int fallback_data_offset;
	unsigned int fallback_size;
};

extern const struct text_hires_embedded text_hires_embedded[];
extern const unsigned int text_hires_embedded_count;
/* the fonts' glyphs' MSDF atlas, zlib-compressed (tools/msdf_atlas.py; an
offset in the assets' file, size 0: none) */
extern const unsigned int text_hires_msdf_offset;
extern const unsigned int text_hires_msdf_size;

/* a glyph: its quad, in units of the 480 lines from the pen on the baseline,
its texels in the atlas, in texels of the atlas's placeholder bitmap, the
font's advance for it, in units, and its ink's left and right from the pen,
in units: its outline's, which its quad (whole pixels of the atlas) is out
to */
struct text_hires_glyph
{
	float left, top, right, bottom;
	float u0, v0, u1, v1;
	float advance;
	float ink_left, ink_right;
};

/* the letters a font is fitted to its tag by: A to Z, then a to z */
#define TEXT_HIRES_FIT_LETTERS 52

/* The committed setting; checked before looking up cached font choices. */
int text_hires_enabled(void);

/* the font standing for a font tag, sized so that its capitals are
cap_height units tall and narrowed where its letters are wider than the
tag's letter_widths (TEXT_HIRES_FIT_LETTERS of them, 0 for a letter the tag
has not), its glyphs laid out at oversample times the display's pixels
(for text drawn scaled up); -1 if none, display.high_res_text is off, or
the atlas cannot be read */
long text_hires_font(char const *tag_name, float cap_height, short const *letter_widths, float oversample);
/* the character's field made into the atlas now, if it is not there yet:
each of a string's before any is drawn, so that the atlas does not start
over in the middle of it */
void text_hires_rasterize(long font, unsigned long code);
/* the glyph of a character (its field made into the atlas if it is not yet; the
font's missing glyph for one neither the font nor its fallback has); 0 if
it has no ink */
int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph);
/* a character's advance in units, as its glyph has it (whether or not it is
made yet, a space's too; the missing glyph's for one the fonts have
not); negative if there is no font */
float text_hires_advance(long font, unsigned long code);
/* the font's kerning between two characters, in units (0 unless one face
has both, with kerning) */
float text_hires_kerning(long font, unsigned long first, unsigned long second);
/* the atlas's placeholder: the D3D texture of the bitmap the game binds,
and its size; NULL forgets it */
void text_hires_register_atlas(const unsigned long *texture, unsigned long width, unsigned long height);

/* the image of the atlas when data (a D3D texture's Data) is its
placeholder's, its glyphs' fields uploaded, to be sampled as
XGPU_ADDRESS_MSDF; 0 otherwise */
unsigned int text_hires_atlas_texture(unsigned long data);
/* whether a texture's Data is the atlas's placeholder's (its image is made
and changed as the text is drawn: chosen at every bind, xbox_textures.c) */
int text_hires_atlas_placeholder(unsigned long data);

/* a function called before the atlas is emptied, which draws what has been
batched from it (rasterizer_xbox_text.c's rasterizer_text_flush) */
void text_hires_set_reset_hook(void (*hook)(void));
/* d3d8_device.c: the display's pixels for each of the 480 lines */
float halo_screen_pixel_scale(void);

/* A line of text set in the font that draws `tag_name` (fonts.json), its
capitals `cap_pixels` tall: its coverage, one byte a pixel, width by height
(malloc'd; NULL if the font or the memory is not there). Whatever
display.high_res_text says: for the loading screen's caption. Its digits
are set in equal cells, each the widest digit's advance, the digit in the
middle of its own, and each number as many cells wide as the line's longest
(its digits at the right of them): a count that changes ("4 of 23", "11 of
23") changes neither the line's width nor any other letter's place. */
unsigned char *text_hires_line(char const *tag_name, char const *text, float cap_pixels,
	int *width, int *height);

#endif
