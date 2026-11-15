#include "harness.h"

#include <math.h>
#include <wchar.h>

/* A menu's line too long for its box (issue 899; port/linux/game/menu_functions.c's
pc_menu_text_line, menu_text_box_fit_to, menu_text_scroll_offset and the
settings panel's settings_panel_wrap): while its row has the focus the whole
line is drawn, still at its start for a pause, moved left at a steady speed
to its end, still there for a pause, then from its start again; while not,
its form cut short with "..."; a line cut short by a fit keeps its whole
text for it; the panel wraps a long value onto more lines, whole. The same
line drawn or fitted again each frame is not measured again. */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))

/* a text box: its text, font and width (ui_widget.c's) */
struct widget_instance
{
	wchar_t text[512];
	long font;
	short width;
};

/* how many times a text was measured */
static long measures;

/* the fake font: each character 6 units wide, "." 2; a line's the widest
of its lines */
static short settings_text_width(long font, wchar_t const *text)
{
	short width = 0, widest = 0;

	(void)font;
	measures++;
	for (; *text; text++)
	{
		if (*text == '\r' || *text == '\n')
		{
			width = 0;
			continue;
		}
		width = (short)(width + (*text == '.' ? 2 : 6));
		widest = MAX(widest, width);
	}
	return widest;
}

static long ui_widget_port_text_box_font(struct widget_instance *box, short *width)
{
	*width = box ? box->width : 0;
	return box ? box->font : NONE;
}

static void text_set_length(struct widget_instance *box, wchar_t const *text, short length)
{
	long index;

	for (index = 0; index < length - 1 && index < NUMBEROF(box->text) - 1 && text[index]; index++)
		box->text[index] = text[index];
	box->text[index] = 0;
}

static void text_set(struct widget_instance *box, wchar_t const *text)
{
	text_set_length(box, text, 64);
}

/* the fake font's line */
static short settings_font_line(long font)
{
	(void)font;
	return 20;
}

#include "under_test.inc"

#define LONG_NAME L"SPEAKERS (HIGH DEFINITION AUDIO DEVICE)"

/* a line drawn each frame (60 a second) from one time to another, its row
focused or not: the last frame's */
static wchar_t const *frames(struct widget_instance *widget, wchar_t const *text, short width, boolean row,
	boolean focused, unsigned long from, unsigned long to, real *scroll, short *room)
{
	wchar_t const *line = NULL;
	unsigned long now;
	short overflow;

	for (now = from; now < to; now += 16)
		pc_menu_text_line(widget, 1, text, width, row, focused, now, scroll, room, &overflow);
	line = pc_menu_text_line(widget, 1, text, width, row, focused, to, scroll, room, &overflow);
	return line;
}

static boolean ends_with_dots(wchar_t const *text)
{
	size_t length = wcslen(text);

	return length > 3 && !wcscmp(text + length - 3, L"...");
}

/* the offset over time: still, moved smoothly at the speed, still at the
end, from the start again */
static void case_scroll(void)
{
	short overflow = 60;
	unsigned long travel = (unsigned long)overflow * 1000UL / MENU_TEXT_SCROLL_SPEED;
	unsigned long cycle = 2UL * MENU_TEXT_SCROLL_PAUSE + travel, at;
	real last = 0.0f;

	CHECK(menu_text_scroll_offset(0, 2000) == 0.0f, "a line that fits moved");
	CHECK(menu_text_scroll_offset(overflow, 0) == 0.0f, "moved as the row took the focus");
	CHECK(menu_text_scroll_offset(overflow, MENU_TEXT_SCROLL_PAUSE - 1) == 0.0f, "moved in its first pause");
	CHECK(fabsf(menu_text_scroll_offset(overflow, MENU_TEXT_SCROLL_PAUSE + 1000) - MENU_TEXT_SCROLL_SPEED) < 0.01f,
		"a second into its move it is at %g, not %d", menu_text_scroll_offset(overflow, MENU_TEXT_SCROLL_PAUSE + 1000),
		MENU_TEXT_SCROLL_SPEED);
	/* (each millisecond a little further, never back, never past its end) */
	for (at = MENU_TEXT_SCROLL_PAUSE; at < MENU_TEXT_SCROLL_PAUSE + travel; at++)
	{
		real offset = menu_text_scroll_offset(overflow, at);

		CHECK(offset >= last && offset - last < 0.05f && offset <= overflow, "at %lu ms it jumped from %g to %g", at,
			last, offset);
		last = offset;
	}
	CHECK(menu_text_scroll_offset(overflow, MENU_TEXT_SCROLL_PAUSE + travel) == overflow, "not at its end");
	CHECK(menu_text_scroll_offset(overflow, cycle - 1) == overflow, "left its end before the pause");
	CHECK(menu_text_scroll_offset(overflow, cycle) == 0.0f, "not back at its start");
	CHECK(menu_text_scroll_offset(overflow, cycle + MENU_TEXT_SCROLL_PAUSE + 500) ==
		menu_text_scroll_offset(overflow, MENU_TEXT_SCROLL_PAUSE + 500), "not the same the second time");
}

/* a row's own line too long for its box: cut short while its row has no
focus, whole and scrolled while it has; measured once */
static void case_row(void)
{
	static struct widget_instance spinner;
	wchar_t const *line;
	real scroll;
	short room, overflow;
	long frame;

	line = pc_menu_text_line(&spinner, 1, LONG_NAME, 90, TRUE, FALSE, 1000, &scroll, &room, &overflow);
	CHECK(line && ends_with_dots(line) && settings_text_width(1, line) <= 90 && scroll == 0.0f,
		"not cut short to its box with no focus: %ls", line ? line : L"(as it is)");
	CHECK(!wcsncmp(line, LONG_NAME, wcslen(line) - 3), "its cut form is not its start: %ls", line);
	line = pc_menu_text_line(&spinner, 1, LONG_NAME, 90, TRUE, TRUE, 2000, &scroll, &room, &overflow);
	CHECK(line && !wcscmp(line, LONG_NAME) && scroll == 0.0f && room == 90, "not whole and still as focused");
	line = frames(&spinner, LONG_NAME, 90, TRUE, TRUE, 2000, 2000 + MENU_TEXT_SCROLL_PAUSE + 1000, &scroll, &room);
	CHECK(line && fabsf(scroll - MENU_TEXT_SCROLL_SPEED) < 0.01f, "a second into its move it is at %g", scroll);
	/* (drawn each frame for ten seconds: measured no more) */
	measures = 0;
	for (frame = 0; frame < 600; frame++)
	{
		pc_menu_text_line(&spinner, 1, LONG_NAME, 90, TRUE, frame < 300, 10000 + (unsigned long)frame * 16,
			&scroll, &room, &overflow);
	}
	CHECK(measures == 0, "measured %ld times drawn again", measures);
	/* (its end shown, and no further) */
	frames(&spinner, LONG_NAME, 90, TRUE, FALSE, 29000, 29900, &scroll, &room);
	line = frames(&spinner, LONG_NAME, 90, TRUE, TRUE, 30000, 30000 + MENU_TEXT_SCROLL_PAUSE +
		(unsigned long)(settings_text_width(1, LONG_NAME) - 90) * 1000UL / MENU_TEXT_SCROLL_SPEED + 500, &scroll, &room);
	CHECK(line && !wcscmp(line, LONG_NAME), "not whole at its end");
	CHECK(fabsf(scroll - (settings_text_width(1, LONG_NAME) - 90)) < 0.01f, "at its end it is at %g, not %d", scroll,
		settings_text_width(1, LONG_NAME) - 90);
	pc_menu_text_line(&spinner, 1, LONG_NAME, 90, TRUE, TRUE, 40000, &scroll, &room, &overflow);
	CHECK(overflow == settings_text_width(1, LONG_NAME) - 90, "its overflow %d", overflow);
}

/* a line that fits, a line of more lines, and a long one of no row's that
no fit cut: drawn as they are */
static void case_as_it_is(void)
{
	static struct widget_instance label, title, lines;
	real scroll;
	short room, overflow;

	CHECK(!pc_menu_text_line(&label, 1, L"OUTPUT", 90, TRUE, TRUE, 1000, &scroll, &room, &overflow) && scroll == 0.0f,
		"a line that fits drawn otherwise");
	CHECK(!pc_menu_text_line(&title, 1, LONG_NAME, 90, FALSE, TRUE, 1000, &scroll, &room, &overflow),
		"a screen's own text cut or scrolled");
	CHECK(!pc_menu_text_line(&lines, 1, L"SPEAKERS\r\nHIGH DEFINITION AUDIO DEVICE", 90, TRUE, TRUE, 1000, &scroll,
		&room, &overflow), "a text of more lines scrolled");
}

/* a fit's line cut short: whole for its scroll while its row has the
focus, in the fit's room; the box's own cut form while not; fitted again
each frame without measuring; another text given the box forgets it */
static void case_fitted(void)
{
	static struct widget_instance cell;
	wchar_t const *line;
	real scroll;
	short room, overflow, drawn, again;
	long frame;

	cell.font = 1;
	cell.width = 200;
	drawn = menu_text_box_fit_to(&cell, LONG_NAME, 90);
	CHECK(ends_with_dots(cell.text) && drawn <= 90 && drawn == settings_text_width(1, cell.text),
		"not cut short to 90: %ls (%d)", cell.text, drawn);
	CHECK(!pc_menu_text_line(&cell, 1, cell.text, 200, FALSE, FALSE, 1000, &scroll, &room, &overflow),
		"its cut form drawn otherwise with no focus");
	line = pc_menu_text_line(&cell, 1, cell.text, 200, FALSE, TRUE, 2000, &scroll, &room, &overflow);
	CHECK(line && !wcscmp(line, LONG_NAME) && room == 90, "not whole in its fit's room as focused (room %d)", room);
	measures = 0;
	for (frame = 0; frame < 100; frame++)
	{
		again = menu_text_box_fit_to(&cell, LONG_NAME, 90);
		pc_menu_text_line(&cell, 1, cell.text, 200, FALSE, TRUE, 2000 + (unsigned long)frame * 16, &scroll, &room, &overflow);
	}
	CHECK(measures == 0 && again == drawn, "fitted again with %ld measures (%d wide, not %d)", measures, again, drawn);
	/* (another text, which fits) */
	menu_text_box_fit_to(&cell, L"SPEAKERS", 90);
	CHECK(!wcscmp(cell.text, L"SPEAKERS"), "not whole: %ls", cell.text);
	CHECK(!pc_menu_text_line(&cell, 1, cell.text, 200, FALSE, TRUE, 3000, &scroll, &room, &overflow),
		"the line before still drawn");
	/* (the box given words of its own after a fit) */
	menu_text_box_fit_to(&cell, LONG_NAME, 90);
	text_set(&cell, L"LOADING...");
	CHECK(!pc_menu_text_line(&cell, 1, cell.text, 200, FALSE, TRUE, 4000, &scroll, &room, &overflow),
		"a fit's line drawn in place of the box's own words");
}

/* the focus gone and back, or a row hidden a while and shown: its scroll
from its start again */
static void case_refocus(void)
{
	static struct widget_instance row;
	real scroll;
	short room, overflow;
	unsigned long moving = MENU_TEXT_SCROLL_PAUSE + 1000;

	frames(&row, LONG_NAME, 90, TRUE, TRUE, 1000, 1000 + moving, &scroll, &room);
	CHECK(scroll > 0.0f, "not moving");
	pc_menu_text_line(&row, 1, LONG_NAME, 90, TRUE, FALSE, 1000 + moving + 16, &scroll, &room, &overflow);
	pc_menu_text_line(&row, 1, LONG_NAME, 90, TRUE, TRUE, 1000 + moving + 32, &scroll, &room, &overflow);
	CHECK(scroll == 0.0f, "focused again, it is at %g", scroll);
	frames(&row, LONG_NAME, 90, TRUE, TRUE, 1000 + moving + 32, 1000 + 2 * moving, &scroll, &room);
	CHECK(scroll > 0.0f, "not moving again");
	/* (not drawn for a second: hidden) */
	pc_menu_text_line(&row, 1, LONG_NAME, 90, TRUE, TRUE, 2000 + 2 * moving, &scroll, &room, &overflow);
	CHECK(scroll == 0.0f, "shown again, it is at %g", scroll);
}

/* the panel's value wrapped onto more lines, whole, each inside the box;
cut short with "..." only past its lines; wrapped again each frame
without measuring */
static void case_wrap(void)
{
	static struct settings_panel_wrapping wrapping, short_wrapping;
	static struct widget_instance value;
	wchar_t joined[512];
	short lines, index, length = 0, line_width = 0;
	long frame;

	value.font = 1;
	value.width = 100;
	lines = settings_panel_wrap(&wrapping, &value, LONG_NAME, 3);
	CHECK(lines == 3 && wrapping.line == 20, "%d lines: %ls", lines, value.text);
	for (index = 0; value.text[index]; index++)
	{
		if (value.text[index] == '\r')
			continue;
		if (value.text[index] == '\n')
		{
			joined[length++] = ' ';
			line_width = 0;
			continue;
		}
		joined[length++] = value.text[index];
		line_width = (short)(line_width + 6);
		CHECK(line_width <= 100 || value.text[index] == ' ', "a line wider than its box: %ls", value.text);
	}
	joined[length] = 0;
	CHECK(!wcscmp(joined, LONG_NAME), "not the whole value: %ls", joined);
	measures = 0;
	for (frame = 0; frame < 100; frame++)
		settings_panel_wrap(&wrapping, &value, LONG_NAME, 3);
	CHECK(measures == 0, "wrapped again with %ld measures", measures);
	/* (a short value one line; a long one past its lines cut short) */
	CHECK(settings_panel_wrap(&short_wrapping, &value, L"SPEAKERS", 3) == 1 && !wcscmp(value.text, L"SPEAKERS"),
		"a short value: %ls", value.text);
	CHECK(settings_panel_wrap(&short_wrapping, &value, LONG_NAME L" " LONG_NAME, 3) == 3 &&
		ends_with_dots(value.text), "a value past its lines: %ls", value.text);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static struct
	{
		char const *name;
		void (*run)(void);
	} const cases[] =
	{
		{ "scroll", case_scroll }, { "row", case_row }, { "as_it_is", case_as_it_is }, { "fitted", case_fitted },
		{ "refocus", case_refocus }, { "wrap", case_wrap },
	};
	long index;

	for (index = 0; index < NUMBEROF(cases); index++)
	{
		CASE(cases[index].name)
		{
			cases[index].run();
			return 0;
		}
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 1;
}
