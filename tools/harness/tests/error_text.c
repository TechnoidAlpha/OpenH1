/*
ERROR_TEXT.C (test)

The port's own error messages (ui_widget.c's display_error_text_deferred):
their line breaks, written for one dialog's lines, made spaces before the
dialog wraps them to its own width (ui_widget_port_text_unbreak), a blank
line kept. test_error_text.py takes the code.
*/

#include "harness.h"

#include <wchar.h>

#include "under_test.inc"

static void unbroken(wchar_t const *text, wchar_t const *expected)
{
	wchar_t copy[256];

	wcsncpy(copy, text, 255);
	copy[255] = 0;
	ui_widget_port_text_unbreak(copy);
	CHECK(!wcscmp(copy, expected), "\"%ls\" made \"%ls\", not \"%ls\"", text, copy, expected);
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
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
