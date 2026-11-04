/*
ERROR_TEXT.C (test)

The port's own error messages (ui_widget.c's display_error_text_deferred):
their line breaks, written for one dialog's lines, made spaces before the
dialog wraps them to its own width (ui_widget_port_text_unbreak), a blank
line kept; and the wrap (ui_widget_port_text_wrap), which measures each
line's last word before a break too. test_error_text.py takes the code.
*/

#include "harness.h"

#include <wchar.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define csmemcpy memcpy
typedef struct { short y0, x0, y1, x1; } rectangle2d;

/* every character 10 units wide (rasterizer_text.c's measure, in a font of
one width): the cursor after the text */
static void draw_unicode_string_compute_bounds(rectangle2d const *bounds, wchar_t const *text,
	rectangle2d *text_bounds, rectangle2d *cursor)
{
	*text_bounds = *bounds;
	*cursor = *bounds;
	cursor->x0 = (short)(bounds->x0 + 10 * (short)wcslen(text));
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
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
