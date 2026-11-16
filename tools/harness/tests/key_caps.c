/*
KEY_CAPS.C (test)

A key's cap in the prompts and the menus' footers (port/linux/game/
hud_key_prompts.c: key_cap_layout, key_cap_draw, hud_key_prompt_cap_advance):
its name's letters in the middle of the box, as much room either side, the
text after it a gap past it, and the advance the menus' legends lay their
rows out with (ui_widget.c) the one it is drawn with. The font is a fake
one: each letter's glyph a box of its own width, starting LEAD past its pen
(the Xbox maps' small_ui and large_ui start theirs 3 past it) and ending
TRAIL short of the next letter's pen, and its ink as the high-res text
draws it INK_LEFT further in at the left and INK_RIGHT at the right (its
glyphs centred on the tag's advances, not on its bitmaps' blank columns),
its outline a fraction of a unit further in still (ink_fraction_left and
ink_fraction_right: whole units are the rounding of it); its capitals
caps_rows rows tall; the cap's drawing is noted, not drawn.
test_key_caps.py takes the code (under_test.inc).
*/

#include "harness.h"

#include <math.h>
#include <wchar.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SHORT_MAX 32767
#define SHORT_MIN (-32768)

typedef unsigned long pixel32;
typedef struct { short y0, x0, y1, x1; } rectangle2d;

static short players = 1;
static short local_player_count(void) { return players; }

/* the fake font: a letter's advance, and its glyph's box from its pen */
static short lead = 3, trail = 0, line_end = SHORT_MAX;
static short advance_of(wchar_t letter) { return letter == L'i' || letter == L'1' ? 4 : letter == L'W' ? 12 : 7; }

static short indent;
static void draw_string_set_indents(short first, short rest) { indent = first; (void)rest; }

/* the text from the bounds' left and the indent: its glyphs' span (x0 to
x1), the cursor moved to the pen after it; a line too short for it wraps
it whole to the next line */
static void draw_unicode_string_compute_bounds(rectangle2d const *bounds, wchar_t const *text, rectangle2d *text_bounds,
	rectangle2d *cursor)
{
	short pen = (short)(bounds->x0 + indent), first = SHORT_MAX, last = SHORT_MIN;
	long index;

	for (index = 0; text[index]; index++)
	{
		first = MIN(first, (short)(pen + lead));
		last = MAX(last, (short)(pen + advance_of(text[index]) - trail));
		pen = (short)(pen + advance_of(text[index]));
	}
	text_bounds->x0 = first;
	text_bounds->x1 = last;
	text_bounds->y0 = bounds->y0;
	text_bounds->y1 = (short)(bounds->y0 + 20);
	if (pen > line_end)
	{
		cursor->y0 = (short)(cursor->y0 + 20);
		cursor->x0 = (short)(bounds->x0 + (pen - bounds->x0 - indent));
		return;
	}
	cursor->x0 = pen;
}

/* the high-res text's ink, past the tag's bitmaps' left (ink_left) and
short of their right (ink_right), and its outline in fractions of a unit
further in (draw_unicode_string_drawn_span) */
static short ink_left, ink_right;
static real ink_fraction_left, ink_fraction_right;
static real drawn_left, drawn_right;
static short caps_rows = 10;

/* as draw_unicode_string_compute_bounds, the letters' ink where it is drawn */
static void draw_unicode_string_compute_drawn_bounds(rectangle2d const *bounds, wchar_t const *text,
	rectangle2d *text_bounds, rectangle2d *cursor)
{
	draw_unicode_string_compute_bounds(bounds, text, text_bounds, cursor);
	/* (a glyph narrower than both: as its bitmap) */
	if (text_bounds->x1 - text_bounds->x0 > ink_left + ink_right)
	{
		text_bounds->x0 = (short)(text_bounds->x0 + ink_left);
		text_bounds->x1 = (short)(text_bounds->x1 - ink_right);
	}
	drawn_left = (real)text_bounds->x0 + ink_fraction_left;
	drawn_right = (real)text_bounds->x1 - ink_fraction_right;
}

static boolean draw_unicode_string_drawn_span(real *left, real *right)
{
	*left = drawn_left;
	*right = drawn_right;
	return drawn_left <= drawn_right;
}

static short draw_string_capital_height(void)
{
	return caps_rows;
}

static short draw_unicode_string_capital_middle(rectangle2d const *bounds, wchar_t const *text)
{
	(void)text;
	return (short)(bounds->y0 + 10);
}

/* what was drawn: the name's pen (and an "='s"), and the cap's box */
typedef struct { real y0, x0, y1, x1; } real_rectangle2d;
static short drawn_pen, equals_pen = SHORT_MIN;
static real_rectangle2d drawn_box;
static int caps_drawn;

static void rasterizer_draw_unicode_string(rectangle2d const *bounds, void const *clip, void const *style, long flags,
	wchar_t const *text)
{
	(void)clip; (void)style; (void)flags;
	if (text[0] == L'=' && !text[1])
		equals_pen = (short)(bounds->x0 + indent);
	else
		drawn_pen = (short)(bounds->x0 + indent);
}

static void key_cap_shape(real x0, real y0, real x1, real y1, pixel32 fill, pixel32 color)
{
	(void)fill; (void)color;
	drawn_box.x0 = x0;
	drawn_box.y0 = y0;
	drawn_box.x1 = x1;
	drawn_box.y1 = y1;
	caps_drawn++;
}

#include "under_test.inc"

/* a glyph's box drawn from a pen in the fake font */
static short glyph_left(short pen) { return (short)(pen + lead); }
static short glyph_right(short pen, wchar_t letter) { return (short)(pen + advance_of(letter) - trail); }

/* a legend's words after a cap drawn at x ("=CANCEL"): the "=" and the
word drawn as draw_string_and_hack_in_icons draws them (the "=", then the
rest from the cursor) */
struct equals_drawn
{
	long taken;
	real box_x1;
	short equals_left, equals_right, word_left, word_right;
};

static struct equals_drawn draw_equals(wchar_t const *name, short x, wchar_t const *words)
{
	rectangle2d bounds = { 0, 0, 480, 640 }, cursor = { 0, 0, 20, 0 }, text_bounds;
	struct equals_drawn result;

	cursor.x0 = x;
	equals_pen = SHORT_MIN;
	CHECK(key_cap_draw(name, &bounds, &cursor, 0xFF2896FFUL), "drawn");
	result.box_x1 = drawn_box.x1;
	result.taken = hud_key_prompt_draw_equals(&bounds, &cursor, words);
	result.equals_left = glyph_left(equals_pen);
	result.equals_right = glyph_right(equals_pen, L'=');
	indent = (short)(cursor.x0 - bounds.x0);
	draw_unicode_string_compute_bounds(&bounds, words + result.taken, &text_bounds, &(rectangle2d){ 0, 0, 20, 0 });
	indent = 0;
	result.word_left = text_bounds.x0;
	result.word_right = text_bounds.x1;
	return result;
}

/* the name drawn at the cursor at x: its glyphs' span as drawn, the cap's
box, and where the text after it starts (the cursor) */
struct drawn
{
	short left, right, after;
	real box_x0, box_x1, box_y0, box_y1;
};

static struct drawn draw(wchar_t const *name, short x)
{
	rectangle2d bounds = { 0, 0, 480, 640 }, cursor = { 0, 0, 20, 0 }, text_bounds;
	struct drawn result;
	short saved = indent;

	cursor.x0 = x;
	caps_drawn = 0;
	CHECK(key_cap_draw(name, &bounds, &cursor, 0xFF2896FFUL), "drawn");
	/* (the name's glyphs from the pen it was drawn from) */
	indent = (short)(drawn_pen - bounds.x0);
	draw_unicode_string_compute_bounds(&bounds, name, &text_bounds, &(rectangle2d){ 0, 0, 20, 0 });
	indent = saved;
	result.left = text_bounds.x0;
	result.right = text_bounds.x1;
	result.box_x0 = drawn_box.x0;
	result.box_x1 = drawn_box.x1;
	result.box_y0 = drawn_box.y0;
	result.box_y1 = drawn_box.y1;
	result.after = cursor.x0;
	return result;
}

static wchar_t const *const names[] = { L"Esc", L"Enter", L"E", L"Q", L"1", L"i", L"Space", L"Mouse 4", L"WWW", L"F12" };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static short const leads[][2] = { { 3, 0 }, { 1, 2 }, { 0, 0 }, { 2, 1 } };
	unsigned long name, font;

	CASE("centred")
	{
		/* the letters in the middle of the box, as much room either side,
		at least the margin, for any name, any font, either size */
		for (players = 1; players <= 2; players++)
			for (font = 0; font < sizeof(leads) / sizeof(leads[0]); font++)
				for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
				{
					struct drawn d;
					real room_left, room_right;

					lead = leads[font][0];
					trail = leads[font][1];
					d = draw(names[name], 100);
					room_left = d.left - d.box_x0;
					room_right = d.box_x1 - d.right;
					CHECK(caps_drawn == 1, "%ls: one cap", names[name]);
					CHECK(room_left == room_right, "%ls (lead %d, trail %d, %d players): %g left of the letters, %g right",
						names[name], lead, trail, players, room_left, room_right);
					CHECK(room_left >= (players > 1 ? 2 : 3), "%ls: %g units of room", names[name], room_left);
					/* (drawn where it is laid out, the text after it the gap
					past it: letters of whole units in the middle of that
					place) */
					CHECK(fabsf(d.box_x0 - (real)(100 + (players > 1 ? 3 : 4))) < 0.001f, "%ls: the cap drawn from %g, "
						"laid out from %d", names[name], d.box_x0, 100 + (players > 1 ? 3 : 4));
					CHECK(d.box_x1 - d.box_x0 >= d.box_y1 - d.box_y0, "%ls: %g wide, %g tall", names[name],
						d.box_x1 - d.box_x0, d.box_y1 - d.box_y0);
				}
	}
	CASE("drawn")
	{
		/* the cap about the letters' ink as it is drawn (a glyph of the
		high-res text further in than the tag's bitmap on one side, as
		Overpass's E and Q in their caps: issue 681), as much room either
		side of it */
		static short const inks[][2] = { { 2, 0 }, { 0, 2 }, { 3, 1 }, { 1, 1 } };
		unsigned long ink;

		for (players = 1; players <= 2; players++)
			for (ink = 0; ink < sizeof(inks) / sizeof(inks[0]); ink++)
				for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
				{
					struct drawn d;
					real room_left, room_right;

					lead = 3;
					trail = 0;
					ink_left = inks[ink][0];
					ink_right = inks[ink][1];
					d = draw(names[name], 100);
					if (d.right - d.left <= ink_left + ink_right)
						continue;
					room_left = (real)(d.left + ink_left) - d.box_x0;
					room_right = d.box_x1 - (real)(d.right - ink_right);
					CHECK(room_left == room_right, "%ls (ink %d in at the left, %d at the right): %g left of its ink, "
						"%g right", names[name], ink_left, ink_right, room_left, room_right);
				}
		ink_left = ink_right = 0;
	}
	CASE("fraction")
	{
		/* the cap about the letters' outline as it is drawn, in fractions
		of a unit (whole units are its rounding: a pixel and more off at
		4K, issue 596's "Esc" nearer the cap's right), as much room either
		side of it */
		static real const fractions[][2] = { { 0.3f, 0.0f }, { 0.0f, 0.45f }, { 0.25f, 0.4f }, { 0.1f, 0.1f } };
		unsigned long fraction;

		for (players = 1; players <= 2; players++)
			for (fraction = 0; fraction < sizeof(fractions) / sizeof(fractions[0]); fraction++)
				for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
				{
					struct drawn d;
					real room_left, room_right;

					lead = 3;
					trail = 0;
					ink_fraction_left = fractions[fraction][0];
					ink_fraction_right = fractions[fraction][1];
					d = draw(names[name], 100);
					room_left = (real)d.left + ink_fraction_left - d.box_x0;
					room_right = d.box_x1 - ((real)d.right - ink_fraction_right);
					CHECK(fabsf(room_left - room_right) < 0.001f, "%ls (outline %g in at the left, %g at the right): "
						"%g left of it, %g right", names[name], ink_fraction_left, ink_fraction_right, room_left,
						room_right);
				}
		ink_fraction_left = ink_fraction_right = 0.0f;
	}
	CASE("capitals")
	{
		/* the cap's middle the capitals' (draw_unicode_string_capital_middle's
		whole unit, half a unit lower for an odd number of rows), as much
		room above them as below: issue 596's caps a unit high */
		static short const rows[] = { 10, 13, 8, 11 };
		unsigned long row;

		for (players = 1; players <= 2; players++)
			for (row = 0; row < sizeof(rows) / sizeof(rows[0]); row++)
			{
				struct drawn d;
				real middle;

				lead = 3;
				trail = 0;
				caps_rows = rows[row];
				d = draw(L"Esc", 100);
				/* (the stub's capitals: their middle 10 under the line's top,
				a half lower for an odd number of rows) */
				middle = 10.0f + (caps_rows % 2 ? 0.5f : 0.0f);
				CHECK(fabsf((d.box_y0 + d.box_y1) * 0.5f - middle) < 0.001f, "%d rows: the cap's middle %g, the "
					"capitals' %g", caps_rows, (d.box_y0 + d.box_y1) * 0.5f, middle);
			}
		caps_rows = 10;
	}
	CASE("gap")
	{
		/* the text after the cap (its letters as far past their pen as the
		name's) starts a gap past the box, not on it; the box past the
		cursor */
		for (font = 0; font < sizeof(leads) / sizeof(leads[0]); font++)
			for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
			{
				struct drawn d;

				players = 1;
				lead = leads[font][0];
				trail = leads[font][1];
				d = draw(names[name], 50);
				CHECK(d.after + lead - d.box_x1 >= 3, "%ls: the next letters %g past the cap", names[name],
					d.after + lead - d.box_x1);
				CHECK(d.box_x0 > 50, "%ls: the cap starts at %g, before the cursor", names[name], d.box_x0);
			}
	}
	CASE("advance")
	{
		/* the advance the legends lay out with is the drawing's, from the
		name's span measured from 0 */
		for (players = 1; players <= 2; players++)
			for (font = 0; font < sizeof(leads) / sizeof(leads[0]); font++)
				for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
				{
					rectangle2d bounds = { 0, 0, 480, 640 }, text_bounds, cursor = { 0, 0, 20, 0 };
					struct drawn d;

					lead = leads[font][0];
					trail = leads[font][1];
					indent = 0;
					draw_unicode_string_compute_bounds(&bounds, names[name], &text_bounds, &cursor);
					d = draw(names[name], 30);
					CHECK(hud_key_prompt_cap_advance(text_bounds.x0, text_bounds.x1) == d.after - 30,
						"%ls: laid out %d, drawn %d", names[name],
						hud_key_prompt_cap_advance(text_bounds.x0, text_bounds.x1), d.after - 30);
				}
	}
	CASE("equals")
	{
		/* a legend's "=" (issue 596's "[Esc] =CANCEL"): as much room
		between the cap and it as between it and its word, for any name and
		font */
		for (players = 1; players <= 2; players++)
			for (font = 0; font < sizeof(leads) / sizeof(leads[0]); font++)
				for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
				{
					struct equals_drawn e;
					short before, after;

					lead = leads[font][0];
					trail = leads[font][1];
					e = draw_equals(names[name], 40, name % 2 ? L" =CANCEL" : L"=CANCEL");
					/* (by the advances, which the high-res text centres its
					glyphs on: the "='s" pen to the word's) */
					before = (short)(equals_pen - (short)e.box_x1);
					after = (short)(e.word_left - lead - (equals_pen + advance_of(L'=')));
					CHECK(e.taken == (name % 2 ? 2 : 1) && equals_pen != SHORT_MIN, "%ls: the = not drawn on its own",
						names[name]);
					CHECK(before == after, "%ls (lead %d, trail %d, %d players): the = %d past the cap, %d before its word",
						names[name], lead, trail, players, before, after);
					CHECK(before >= 1, "%ls: the = %d past the cap", names[name], before);
				}
	}
	CASE("equals-advance")
	{
		/* the legends lay a cap, its "=" and word out as they are drawn */
		for (players = 1; players <= 2; players++)
			for (font = 0; font < sizeof(leads) / sizeof(leads[0]); font++)
				for (name = 0; name < sizeof(names) / sizeof(names[0]); name++)
				{
					static wchar_t const *const words[] = { L"=CANCEL", L" =SELECT", L"BACK" };
					unsigned long word;

					lead = leads[font][0];
					trail = leads[font][1];
					for (word = 0; word < sizeof(words) / sizeof(words[0]); word++)
					{
						rectangle2d bounds = { 0, 0, 480, 640 }, text_bounds, cursor = { 0, 0, 20, 0 };
						struct equals_drawn e;
						short laid;

						indent = 0;
						draw_unicode_string_compute_bounds(&bounds, names[name], &text_bounds, &cursor);
						laid = hud_key_prompt_legend_advance(text_bounds.x0, text_bounds.x1, words[word]);
						e = draw_equals(names[name], 30, words[word]);
						CHECK(laid == e.word_right - 30, "%ls %ls: laid out to %d, drawn to %d", names[name],
							words[word], laid, e.word_right - 30);
					}
				}
	}
	CASE("equals-none")
	{
		/* no "=" or no cap before it on its line: nothing taken */
		rectangle2d bounds = { 0, 0, 480, 640 }, cursor = { 0, 600, 20, 0 };

		players = 1;
		lead = 3;
		trail = 0;
		CHECK(draw_equals(L"Esc", 40, L"CANCEL").taken == 0, "words with no = taken");
		line_end = 610;
		CHECK(key_cap_draw(L"Enter", &bounds, &cursor, 0xFF2896FFUL), "drawn");
		CHECK(hud_key_prompt_draw_equals(&bounds, &cursor, L"=SELECT") == 0, "an = after a wrapped name taken");
		line_end = SHORT_MAX;
	}
	CASE("wrapped")
	{
		/* a name past the line's end: its text where it wrapped, no cap */
		rectangle2d bounds = { 0, 0, 480, 640 }, cursor = { 0, 600, 20, 0 };

		players = 1;
		lead = 3;
		trail = 0;
		line_end = 610;
		caps_drawn = 0;
		CHECK(key_cap_draw(L"Enter", &bounds, &cursor, 0xFF2896FFUL), "drawn");
		CHECK(caps_drawn == 0 && cursor.y0 == 20, "a cap drawn for a wrapped name");
		line_end = SHORT_MAX;
	}
	return 0;
}
