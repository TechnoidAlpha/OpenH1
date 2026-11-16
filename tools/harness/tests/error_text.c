/*
ERROR_TEXT.C (test)

The port's own error messages (ui_widget.c's display_error_text_deferred):
their line breaks, written for one dialog's lines, made spaces before the
dialog wraps them to its own width (ui_widget_port_text_unbreak), a blank
line kept; the wrap (ui_widget_port_text_wrap), which measures each line's
last word before a break too; and a dialog's text fitted to its room
(ui_widget_port_text_fit: issue 689's two reasons, issue 696's long
mission name), wrapped, in the smaller font, then drawn smaller, to end
inside it. test_error_text.py takes the code.
*/

#include "harness.h"

#include <wchar.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define csmemcpy memcpy
#define ustrncpy wcsncpy
#define ustrlen wcslen
typedef struct { short y0, x0, y1, x1; } rectangle2d;
typedef struct { real alpha, red, green, blue; } real_argb_color;
enum { _text_justification_left, _text_justification_right, _text_justification_center };

/* the fonts: the dialog's (large_ui's), each character 10 units wide, lines
20 apart; the smaller (small_ui), 7 wide, lines 15 apart */
#define LARGE_FONT 1
#define SMALL_FONT 2
static long font_drawn = LARGE_FONT;
#define tag_loaded(group, name) SMALL_FONT
static void draw_string_set_draw_mode(long font, long style, short justification, long flags,
	real_argb_color *color)
{
	(void)style; (void)justification; (void)flags; (void)color;
	font_drawn = font;
}

/* the text's lines in the draw mode's font (rasterizer_text.c's measure, in
a font of one width): the cursor after the text, its bottom the last
line's */
static void draw_unicode_string_compute_bounds(rectangle2d const *bounds, wchar_t const *text,
	rectangle2d *text_bounds, rectangle2d *cursor)
{
	short advance = font_drawn == SMALL_FONT ? 7 : 10, line = font_drawn == SMALL_FONT ? 15 : 20, lines = 1;
	long index, last = 0;

	for (index = 0; text[index]; index++)
	{
		if (text[index] == L'\r' || text[index] == L'\n')
		{
			if (!(text[index] == L'\n' && index > 0 && text[index - 1] == L'\r'))
				lines++;
			last = index + 1;
		}
	}
	*text_bounds = *bounds;
	*cursor = *bounds;
	cursor->x0 = (short)(bounds->x0 + advance * (short)(index - last));
	cursor->y1 = (short)(bounds->y0 + lines * line);
}

static real scale_drawn = 1.0f;
static void rasterizer_text_set_scale(real scale, real origin_x, real origin_y)
{
	(void)origin_x; (void)origin_y;
	scale_drawn = scale;
}

#include "under_test.inc"

static void unbroken(wchar_t const *text, wchar_t const *expected)
{
	wchar_t copy[256];

	wcsncpy(copy, text, 255);
	copy[255] = 0;
	ui_widget_port_text_unbreak(copy);
	CHECK(!wcscmp(copy, expected), "\"%ls\" made \"%ls\", not \"%ls\"", text, copy, expected);
}

static void wrapped(wchar_t const *text, short width, wchar_t const *expected, boolean fits)
{
	wchar_t copy[256];
	rectangle2d bounds = { 0, 100, 50, 600 };
	boolean result;

	wcsncpy(copy, text, 255);
	copy[255] = 0;
	result = ui_widget_port_text_wrap(copy, &bounds, width);
	CHECK(!wcscmp(copy, expected) && result == fits, "\"%ls\" at %d made \"%ls\" (fits %d), not \"%ls\"", text, width,
		copy, result, expected);
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";

	if (!strcmp(test, "unbreak"))
	{
		unbroken(L"That link can't be joined.\r\nCopy the whole invite link\r\nagain, or ask the host for\r\na new one.",
			L"That link can't be joined. Copy the whole invite link again, or ask the host for a new one.");
		unbroken(L"one\ntwo", L"one two");
		unbroken(L"one \r\n two", L"one two");
		unbroken(L"first.\r\n\r\nsecond", L"first.\r\rsecond");
		unbroken(L"no breaks", L"no breaks");
		/* (a word broken with a hyphen whole again; a hyphen before a
		capital, or before a paragraph, kept: the map lists' words) */
		unbroken(L"the Cove-\nnant fear", L"the Covenant fear");
		unbroken(L"Hydro-\nProcessing Center", L"Hydro-Processing Center");
		unbroken(L"Deep-Space\nAnomaly", L"Deep-Space Anomaly");
		unbroken(L"up -\ndown", L"up - down");
		unbroken(L"", L"");
		return 0;
	}
	if (!strcmp(test, "wrap"))
	{
		/* (a line's last word before a break measured too) */
		wrapped(L"aa bbbbbbbbbb\rcc", 110, L"aa\rbbbbbbbbbb\rcc", TRUE);
		wrapped(L"aa bbbbbbbbbb\r\ncc", 110, L"aa\rbbbbbbbbbb\r\ncc", TRUE);
		wrapped(L"aa bb cc dd", 50, L"aa bb\rcc dd", TRUE);
		wrapped(L"aaaaaaa bb", 50, L"aaaaaaa\rbb", FALSE);
		wrapped(L"first\r\rsecond", 100, L"first\r\rsecond", TRUE);
		return 0;
	}
	if (!strcmp(test, "fit"))
	{
		/* a dialog's text in a room 220 wide, 87 high (the error box's): a
		short one as it is, in its font; one taller than the room in it, in
		the smaller font; one taller in that too, drawn smaller, to end
		inside the room, its lines laid out the wider for it */
		static wchar_t const *const texts[] = {
			L"The game is full.",
			L"The map intensity_v2.map can't be played here: its file is not a map this game can read.",
			L"You don't have the map intensity_v2.map. If you have it, copy it into custom_maps.\r\n\r\nThe map "
			L"couldn't be saved: custom_maps has another file of its name. Move that file, then try again.",
		};
		static long const fonts[] = { LARGE_FONT, SMALL_FONT, SMALL_FONT };
		unsigned long index;

		for (index = 0; index < NUMBEROF(texts); index++)
		{
			wchar_t text[512];
			rectangle2d bounds = { 100, 200, 300, 420 }, text_bounds, cursor;
			real_argb_color color = { 1.0f, 1.0f, 1.0f, 1.0f };
			long font = LARGE_FONT;
			real scale;

			wcsncpy(text, texts[index], 511);
			text[511] = 0;
			font_drawn = LARGE_FONT;
			scale_drawn = 1.0f;
			scale = ui_widget_port_text_fit(text, &bounds, 220, 87, &font, &color, _text_justification_left);
			CHECK(font == fonts[index], "text %lu in font %ld, not %ld", index, font, fonts[index]);
			CHECK(index < 2 ? scale == 1.0f : scale < 1.0f && scale >= 0.6f, "text %lu at %g", index, scale);
			CHECK(scale == scale_drawn, "text %lu fitted at %g, drawn at %g", index, scale, scale_drawn);
			draw_unicode_string_compute_bounds(&bounds, text, &text_bounds, &cursor);
			CHECK((real)(cursor.y1 - bounds.y0) * scale <= 87.0f, "text %lu %g high, the room 87", index,
				(real)(cursor.y1 - bounds.y0) * scale);
			/* (each line as wide as the room at most, as drawn) */
			{
				long at = 0, start = 0;

				for (;; at++)
				{
					if (!text[at] || text[at] == L'\r' || text[at] == L'\n')
					{
						short width = (short)((at - start) * (font == SMALL_FONT ? 7 : 10));

						CHECK((real)width * scale <= 220.0f, "text %lu: a line %g wide", index, (real)width * scale);
						if (!text[at])
							break;
						start = at + 1;
					}
				}
			}
		}
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
