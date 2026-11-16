/*
NAME_KEYBOARD.C (test)

The name keyboard's screen in the port's menus' look (source/interface/
virtual_keyboard.c, issue 667): its footer's prompts on the screen, in the
footer's band, apart, at the wide layout's right; a click on a prompt where
it is drawn takes it as the Xbox's legend (BACK cancels, ENTER is Done), and
the touch targets' list names those boxes; and how each key looks (chosen,
pressed, held on). test_name_keyboard.py takes the code (under_test.inc).
*/

#include "harness.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef unsigned short word;
typedef struct { short y0, x0, y1, x1; } rectangle2d;

#include "config.inc"

static struct
{
	boolean active, shift_active, caps_active, symbols_active;
	short row, column;
	short last_event, last_key;
	unsigned long time_of_last_event;
} virtual_keyboard_globals;

/* what the keyboard did: cancelled, and the key a select pressed */
static int cancels, selects;
static long selected_key = NONE;

static boolean virtual_keyboard_cancel(void)
{
	cancels++;
	return TRUE;
}

static boolean virtual_keyboard_select(void)
{
	selects++;
	selected_key = virtual_keyboard_layout_table[virtual_keyboard_globals.row][virtual_keyboard_globals.column];
	return TRUE;
}

#define system_milliseconds() 0UL

#include "under_test.inc"

static boolean inside(rectangle2d const *box, short x0, short y0, short x1, short y1)
{
	return box->x0 >= x0 && box->x1 <= x1 && box->y0 >= y0 && box->y1 <= y1;
}

static boolean apart(rectangle2d const *a, rectangle2d const *b)
{
	return a->x1 <= b->x0 || b->x1 <= a->x0 || a->y1 <= b->y0 || b->y1 <= a->y0;
}

/* the screen's sides past the menus' 640: 4:3, 16:10, 16:9 */
static short const sides[] = { 0, 64, 106 };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	unsigned long side;

	CASE("prompts")
	{
		/* on the screen and the margin from its right, in the footer's
		band, apart, BACK at the right, a key cap and its word room */
		for (side = 0; side < sizeof(sides) / sizeof(sides[0]); side++)
		{
			rectangle2d back, enter;
			short right = (short)(640 + sides[side]);

			virtual_keyboard_port_prompt_boxes(right, &back, &enter);
			CHECK(inside(&back, (short)-sides[side], VIRTUAL_KEYBOARD_PORT_FOOTER_LINE + 1, (short)(right - 24),
				VIRTUAL_KEYBOARD_PORT_FOOTER_BOTTOM), "BACK [%d %d %d %d] off the band", back.x0, back.y0, back.x1,
				back.y1);
			CHECK(inside(&enter, (short)-sides[side], VIRTUAL_KEYBOARD_PORT_FOOTER_LINE + 1, (short)(right - 24),
				VIRTUAL_KEYBOARD_PORT_FOOTER_BOTTOM), "ENTER [%d %d %d %d] off the band", enter.x0, enter.y0,
				enter.x1, enter.y1);
			CHECK(apart(&back, &enter) && enter.x1 <= back.x0, "the prompts over each other");
			CHECK(back.x1 == right - 24, "BACK %d from the right", right - back.x1);
			/* (clear of the keys' panel at 4:3, as the footer is) */
			CHECK(back.y0 >= VIRTUAL_KEYBOARD_PORT_KEYS_PANEL_BOTTOM + 8, "the prompts reach the keys");
			CHECK(back.x1 - back.x0 >= 80 && enter.x1 - enter.x0 >= 100, "a prompt too narrow for its key and word");
		}
	}
	CASE("click")
	{
		/* the prompts' boxes, where drawn, are where a click finds them */
		for (side = 0; side < sizeof(sides) / sizeof(sides[0]); side++)
		{
			rectangle2d targets[NUMBER_OF_VIRTUAL_KEYS + 2];
			long hit, count;

			virtual_keyboard_port_prompt_boxes((short)(640 + sides[side]), &keyboard_back_legend_rect,
				&keyboard_enter_legend_rect);
			virtual_keyboard_globals.active = TRUE;
			virtual_keyboard_globals.row = 2;
			virtual_keyboard_globals.column = 3;
			cancels = selects = 0;
			CHECK(virtual_keyboard_click((short)(keyboard_back_legend_rect.x0 + 4),
				(short)((keyboard_back_legend_rect.y0 + keyboard_back_legend_rect.y1) / 2), &hit), "BACK missed");
			CHECK(cancels == 1 && selects == 0 && hit == NUMBER_OF_VIRTUAL_KEYS, "BACK did not cancel");
			CHECK(virtual_keyboard_click((short)(keyboard_enter_legend_rect.x1 - 4),
				(short)((keyboard_enter_legend_rect.y0 + keyboard_enter_legend_rect.y1) / 2), &hit), "ENTER missed");
			CHECK(selects == 1 && selected_key == _vkey_done && hit == NUMBER_OF_VIRTUAL_KEYS + 1,
				"ENTER pressed %ld, not Done", selected_key);
			count = virtual_keyboard_target_rectangles(targets, NUMBER_OF_VIRTUAL_KEYS + 2);
			CHECK(count == NUMBER_OF_VIRTUAL_KEYS + 2, "%ld targets", count);
			CHECK(!memcmp(&targets[count - 2], &keyboard_back_legend_rect, sizeof(rectangle2d)) &&
				!memcmp(&targets[count - 1], &keyboard_enter_legend_rect, sizeof(rectangle2d)),
				"the targets are not the prompts' boxes");
		}
	}
	CASE("look")
	{
		long key;

		virtual_keyboard_globals.row = 0;
		virtual_keyboard_globals.column = 4;
		virtual_keyboard_globals.last_event = _event_tab_right;
		virtual_keyboard_globals.shift_active = TRUE;
		virtual_keyboard_globals.caps_active = FALSE;
		virtual_keyboard_globals.symbols_active = TRUE;
		for (key = 0; key < NUMBER_OF_VIRTUAL_KEYS; key++)
		{
			short look = virtual_keyboard_port_key_look(key);
			short expected = key == _vkey_4 ? _virtual_keyboard_port_key_chosen :
				key == _vkey_shift || key == _vkey_symbols ? _virtual_keyboard_port_key_held :
				_virtual_keyboard_port_key_plain;

			CHECK(look == expected, "key %ld looks %d, not %d", key, look, expected);
		}
		virtual_keyboard_globals.last_event = _event_key_select;
		CHECK(virtual_keyboard_port_key_look(_vkey_4) == _virtual_keyboard_port_key_pressed, "a pressed key not lit");
		/* (a held key chosen looks chosen) */
		virtual_keyboard_globals.row = 1;
		virtual_keyboard_globals.column = 0;
		virtual_keyboard_globals.last_event = _event_tab_down;
		CHECK(virtual_keyboard_port_key_look(_vkey_shift) == _virtual_keyboard_port_key_chosen, "Shift chosen");
	}
	return 0;
}
