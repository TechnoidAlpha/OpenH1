/*
TEXT_BOX_LENGTHS.C (test)

A text box's own text (menu_functions.c's text_set_length): one box is
given texts of different lengths (Choose Mission's panel line: 64
characters fitted on HALO, 256 on CUSTOM), and the game sizes a string
list's text to the string. The copy pads to the length given, so the
buffer must be at least that long each time, never only as long as it was
made first: else it is overrun, and the widgets' pool finds its block
damaged as the screen is freed (backing out of Choose Mission after
CUSTOM, #553). The pool here is the game's (stack_memory_pool.c): a block
already large enough kept, a larger one copied, a check word after each.
test_text_box_lengths.py takes the game's code.
*/

#include "harness.h"
#include <wchar.h>

#define ROW_TEXT_LENGTH 64
#define MISSION_TEXT_LENGTH 256
#define SETTINGS_PANEL_TEXT_LENGTH 512
#define CHECK_WORD 0x6E6B6863UL /* 'chkn' */

typedef unsigned short word;

struct widget_instance
{
	union
	{
		struct
		{
			wchar_t *text;
		} text_box;
	} parameters;
};

/* the pool's block: its user size before the user's bytes, the check
word after them */
struct block
{
	unsigned long size;
	unsigned long check_offset;
};

static long blocks_live;

static struct block *block_of(void *pointer)
{
	return (struct block *)((byte *)pointer - sizeof(struct block));
}

static boolean block_valid(void *pointer)
{
	struct block *block = block_of(pointer);
	unsigned long check;

	memcpy(&check, (byte *)pointer + block->size, sizeof(check));
	return check == CHECK_WORD;
}

static void *ui_widget_realloc(void *pointer, word size, char const *file, unsigned long line)
{
	struct block *block;
	unsigned long check = CHECK_WORD;

	(void)file;
	(void)line;
	if (pointer)
	{
		CHECK(block_valid(pointer), "a text's block was overrun before it was resized");
		if (block_of(pointer)->size >= size)
			return pointer;
	}
	block = malloc(sizeof(*block) + size + sizeof(check));
	block->size = size;
	memset(block + 1, 0xCA, size);
	memcpy((byte *)(block + 1) + size, &check, sizeof(check));
	if (pointer)
	{
		memcpy(block + 1, pointer, block_of(pointer)->size);
		free(block_of(pointer));
	}
	else
		blocks_live++;
	return block + 1;
}

/* the screen freed (ui_widget.c's widget disposal: the pool checks the block) */
static void text_box_free(struct widget_instance *box)
{
	if (!box->parameters.text_box.text)
		return;
	CHECK(block_valid(box->parameters.text_box.text), "a text box's block is damaged as its screen is freed");
	free(block_of(box->parameters.text_box.text));
	box->parameters.text_box.text = NULL;
	blocks_live--;
}

/* the game's own text of a string list's string, sized to it (ui_widget.c) */
static void game_text_set(struct widget_instance *box, wchar_t const *string)
{
	size_t length = wcslen(string);

	box->parameters.text_box.text = ui_widget_realloc(box->parameters.text_box.text,
		(word)((length + 1) * sizeof(wchar_t)), "", 0);
	memcpy(box->parameters.text_box.text, string, (length + 1) * sizeof(wchar_t));
}

#define ustrncpy wcsncpy

#include "under_test.inc"

static void text_set(struct widget_instance *box, wchar_t const *text)
{
	text_set_length(box, text, ROW_TEXT_LENGTH);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";
	struct widget_instance box = { { { NULL } } };

	if (!strcmp(name, "longer"))
	{
		/* Choose Mission's detail_status: a beaten mission's status on
		HALO, then CUSTOM's line, then the screen backed out of */
		text_set(&box, L"Beaten on Legendary.");
		CHECK(!wcscmp(box.parameters.text_box.text, L"Beaten on Legendary."), "the fitted line not set");
		text_set_length(&box, L"A campaign map of your custom_maps folder.", MISSION_TEXT_LENGTH);
		CHECK(block_valid(box.parameters.text_box.text), "a longer text overran the box's buffer");
		CHECK(!wcscmp(box.parameters.text_box.text, L"A campaign map of your custom_maps folder."),
			"the longer text not set");
		text_set_length(&box, L"wrapped words", SETTINGS_PANEL_TEXT_LENGTH);
		text_set(&box, L"Not yet beaten.");
		CHECK(!wcscmp(box.parameters.text_box.text, L"Not yet beaten."), "a shorter text again not set");
		text_box_free(&box);
		CHECK(blocks_live == 0, "a text's block was not given back");
		return 0;
	}
	if (!strcmp(name, "game-sized"))
	{
		/* the game's text of a string list's string first, as long as it */
		game_text_set(&box, L"BACK");
		text_set(&box, L"%b-button BACK");
		CHECK(block_valid(box.parameters.text_box.text), "a text overran the game's buffer sized to its string");
		CHECK(!wcscmp(box.parameters.text_box.text, L"%b-button BACK"), "the text not set");
		text_box_free(&box);
		return 0;
	}
	if (!strcmp(name, "same-length"))
	{
		/* (a box set each frame keeps its one block) */
		wchar_t *first;

		text_set(&box, L"MISSION 01");
		first = box.parameters.text_box.text;
		text_set(&box, L"MISSION 02");
		text_set(&box, L"");
		CHECK(box.parameters.text_box.text == first, "a box given the same length is given a new block");
		CHECK(!box.parameters.text_box.text[0], "an empty text not set");
		text_box_free(&box);
		CHECK(blocks_live == 0, "a text's block was not given back");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
