/*
TEXT_SPACING.C (test)

Where the high-res text draws each glyph (source/rasterizer/rasterizer_text.c:
hires_text_glyph_offset and hires_text_run_lay_out; the run of the line a
character is in, source/text/draw_string.c: draw_string_port_run): a run's
letters with the font's own spacing, as much closer or further apart each,
the run's ink where the font tag's is (issues 744 and 754). The fonts are
fake: the tag's characters have whole-unit widths that are not the font's
advances, and bitmaps whose ink starts a unit before the pen (as the Xbox
maps' large_ui and interstate do), each edge column part covered; the
font's glyphs have advances, ink and kerning of their own, and lack '~'.
test_text_spacing.py takes the code (under_test.inc).
*/

#include "harness.h"

#include <math.h>
#include <wchar.h>

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define csmemcpy memcpy
typedef unsigned short word;

struct font_header
{
	short ascending_height;
	struct
	{
		void *address;
	} pixels;
};

struct font_character
{
	word character;
	short character_width;
	short bitmap_width;
	short bitmap_height;
	short bitmap_origin_x;
	short bitmap_origin_y;
	long pixels_offset;
};

struct parse_string_state
{
	struct font_header *font_header;
	byte *string;
	short string_index;
};

struct text_hires_glyph
{
	float left, top, right, bottom;
	float u0, v0, u1, v1;
	float advance;
	float ink_left, ink_right;
};

/* the fake tag: ' ' to '~', each character's width, and a bitmap 3 rows
tall of its ink: from the column before the pen (origin 1) for ink columns,
its first and last columns 60% covered */
#define FIRST_CODE 32
#define CODES 95
static struct font_character characters[CODES];
static byte pixels[CODES * 64];
static struct font_header tag = { 17, { pixels } };

/* the fake font's advance and ink for a character: '~' it has not */
static real font_advance(word code)
{
	return code == ' ' ? 4.6f : 6.0f + (real)((code * 7) % 9) * 0.55f;
}

static real font_ink_left(word code)
{
	return 0.4f + (real)((code * 3) % 5) * 0.3f;
}

static real font_ink_right(word code)
{
	return font_advance(code) - 0.3f - (real)((code * 5) % 4) * 0.35f;
}

static void tag_make(void)
{
	long index;
	long offset = 0;

	for (index = 0; index < CODES; index++)
	{
		struct font_character *character = &characters[index];
		word code = (word)(FIRST_CODE + index);
		short columns = code == ' ' ? 0 : (short)(4 + (code * 5) % 6);
		short row;

		character->character = code;
		/* (whole units, a unit or so off the font's advance either way) */
		character->character_width = (short)(font_advance(code) + (real)((code * 11) % 5) * 0.5f - 0.5f + 0.5f);
		if (code == ' ')
			character->character_width = 4;
		character->bitmap_width = columns;
		character->bitmap_height = columns ? 3 : 0;
		character->bitmap_origin_x = 1;
		character->bitmap_origin_y = 3;
		character->pixels_offset = offset;
		for (row = 0; row < character->bitmap_height; row++)
		{
			short column;

			for (column = 0; column < columns; column++)
				pixels[offset + row * columns + column] = column == 0 || column == columns - 1 ? 153 : 255;
		}
		offset += columns * character->bitmap_height;
	}
}

static struct font_character *font_get_character_by_ascii_code(struct font_header *font, word code)
{
	(void)font;
	return code >= FIRST_CODE && code < FIRST_CODE + CODES ? &characters[code - FIRST_CODE] : NULL;
}

static float text_hires_advance(long font, unsigned long code)
{
	(void)font;
	return code == '~' ? -1.0f : font_advance((word)code);
}

/* the fake font's kerning: "To" and "AV" closer */
static float text_hires_kerning(long font, unsigned long first, unsigned long second)
{
	(void)font;
	return first == 'T' && second == 'o' ? -0.8f : first == 'A' && second == 'V' ? -0.6f : 0.0f;
}

static int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph)
{
	(void)font;
	if (code == '~' || code == ' ')
		return 0;
	memset(glyph, 0, sizeof(*glyph));
	glyph->advance = font_advance((word)code);
	glyph->left = floorf(font_ink_left((word)code));
	glyph->right = ceilf(font_ink_right((word)code));
	glyph->ink_left = font_ink_left((word)code);
	glyph->ink_right = font_ink_right((word)code);
	return 1;
}

/* draw_string's characters of byte strings (international_strings.c's, one byte each here) */
static word get_next_character(byte *string, short *index)
{
	return string[(*index)++];
}

#include "under_test.inc"

/* a line drawn as draw_unicode_string_partial (or draw_string_partial) draws
it: each character's ink where the high-res text puts it, from its pen */
#define MAXIMUM 128
static real drawn_left[MAXIMUM], drawn_right[MAXIMUM], tag_left[MAXIMUM], tag_right[MAXIMUM];
static boolean inked[MAXIMUM];

static void draw_line(void const *string, boolean unicode, short first, short length)
{
	struct parse_string_state state;
	short index;
	short pen = 0;

	state.font_header = &tag;
	state.string = (byte *)string;
	draw_string_port_line_begin(string, unicode, first, length);
	for (index = first; index < length; index++)
	{
		word code = unicode ? (word)((wchar_t const *)string)[index] : ((byte const *)string)[index];
		struct font_character *character = font_get_character_by_ascii_code(&tag, code);
		struct text_hires_glyph glyph;
		real ink_left, ink_right;

		state.string_index = (short)(index + 1);
		inked[index] = FALSE;
		if (!character)
			continue;
		if (code != '\t' && text_hires_glyph(0, code, &glyph) &&
			font_character_ink_span(&tag, character, &ink_left, &ink_right))
		{
			real offset = hires_text_glyph_offset(&state, &tag, character, 0, glyph.advance);

			inked[index] = TRUE;
			drawn_left[index] = pen + offset + glyph.ink_left;
			drawn_right[index] = pen + offset + glyph.ink_right;
			tag_left[index] = pen + ink_left;
			tag_right[index] = pen + ink_right;
		}
		pen = (short)(pen + character->character_width);
	}
}

/* each two neighbouring inked characters of first to last (a run): how much
closer or further apart than the font spaces them, for each advance between
them, the same for all; and the run's ink from the tag's ink's left to its
right */
static void check_run(wchar_t const *string, short first, short last, boolean placed)
{
	short index, previous = NONE;
	real tracking = 0.0f;
	boolean tracked = FALSE;
	short left = NONE, right = NONE;

	for (index = first; index < last; index++)
	{
		if (!inked[index])
			continue;
		if (left == NONE)
			left = index;
		right = index;
		if (previous != NONE)
		{
			real own = -font_ink_right(string[previous]) + font_ink_left(string[index]);
			real gap = drawn_left[index] - drawn_right[previous];
			short between;

			for (between = previous; between < index; between++)
				own += font_advance(string[between]) + text_hires_kerning(0, string[between], string[between + 1]);
			if (!tracked)
			{
				tracking = (gap - own) / (index - previous);
				tracked = TRUE;
			}
			CHECK(fabsf((gap - own) / (index - previous) - tracking) < 0.01f,
				"'%lc%lc' (%d) is %.2f units from the font's spacing, the run's others %.2f", (wint_t)string[previous],
				(wint_t)string[index], index, (gap - own) / (index - previous), tracking);
		}
		previous = index;
	}
	CHECK(left != NONE, "no ink");
	if (placed)
	{
		CHECK(fabsf(drawn_left[left] - tag_left[left]) < 0.01f && fabsf(drawn_right[right] - tag_right[right]) < 0.01f,
			"the run's ink is %.2f to %.2f, the tag's %.2f to %.2f", drawn_left[left], drawn_right[right], tag_left[left],
			tag_right[right]);
	}
}

static wchar_t const *lines[] = {
	L"How far the view turns as the mouse",
	L"INTERFACE PROFILES QUALITY",
	L"Puts this tab's settings back to their",
	L"1920 x 1080",
	L"Hold",
	L"to swap for",
	L"To fly the Banshee: AVOID",
};

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";
	long line;

	tag_make();
	/* a run's letters with the font's spacing, each pair as much closer or further apart */
	if (!strcmp(test, "even") || !strcmp(test, "placed"))
	{
		for (line = 0; line < NUMBEROF(lines); line++)
		{
			short length = (short)wcslen(lines[line]);

			draw_line(lines[line], TRUE, 0, length);
			check_run(lines[line], 0, length, !strcmp(test, "placed"));
		}
		return 0;
	}
	/* the game's byte strings (rasterizer_draw_string) as its unicode ones: a
	line of one, from its 18th character (the line before it wrapped) */
	if (!strcmp(test, "bytes"))
	{
		char const *bytes = "Searching for the available games";
		wchar_t wide[64];
		short length = (short)strlen(bytes);
		short index;

		for (index = 0; index <= length; index++)
			wide[index] = (wchar_t)(byte)bytes[index];
		draw_line(bytes, FALSE, 18, length);
		check_run(wide, 18, length, TRUE);
		return 0;
	}
	/* a tab ends a run: each side laid on the tag's on its own */
	if (!strcmp(test, "tab"))
	{
		wchar_t const *string = L"Score Limit:\t25 points";
		short length = (short)wcslen(string);

		draw_line(string, TRUE, 0, length);
		check_run(string, 0, 12, TRUE);
		check_run(string, 13, length, TRUE);
		return 0;
	}
	/* a run with a character the font lacks: each glyph's advance centred on its character's */
	if (!strcmp(test, "lacking"))
	{
		wchar_t const *string = L"AB~CD";
		short length = (short)wcslen(string);
		short index;

		draw_line(string, TRUE, 0, length);
		for (index = 0; index < length; index++)
		{
			struct font_character *character = font_get_character_by_ascii_code(&tag, string[index]);
			real pen = 0.0f;
			short before;

			if (!inked[index])
				continue;
			for (before = 0; before < index; before++)
				pen += font_get_character_by_ascii_code(&tag, string[before])->character_width;
			CHECK(fabsf(drawn_left[index] - (pen + (character->character_width - font_advance(string[index])) * 0.5f +
				font_ink_left(string[index]))) < 0.01f, "'%lc' not centred on its character", (wint_t)string[index]);
		}
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
