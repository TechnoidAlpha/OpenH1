/*
LEGEND_ROW.C (test)

A row of legends under a pause menu (the Xbox's "(B) =CANCEL (A) =SELECT",
source/interface/ui_widget.c's ui_widget_port_legend_row) laid out as each
key's cap and its words while the keyboard plays: how much wider than the
Xbox's pictures and words it is at their gaps (the room the pause box is
made wider by: port/linux/game/menu_tags.c's pause_box_widen, issue 596),
the row given that room laid out at the Xbox's gaps, centred where the
Xbox's was; without it, its gaps narrowed to fit. test_legend_row.py takes
the code.
*/

#include "harness.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define SHORT_MAX 32767
#define SHORT_MIN (-32768)

#include "under_test.inc"

/* the pause menu's legends as the Xbox laid them out (ui\shell\main_menu\
button_key_sm: B's picture at 0, "=CANCEL" from 16; A's at 90, "=SELECT"
from 107), and the keyboard's caps and words as they are drawn */
static struct ui_widget_port_legend const pause_legends[] = { { 0, 63, 85 }, { 90, 167, 95 } };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	short starts[UI_WIDGET_PORT_LEGENDS];

	CASE("growth")
	{
		/* as much wider as the caps and words are than the pictures and
		words, at the Xbox's gap (27); none for a row no wider */
		static struct ui_widget_port_legend const narrow[] = { { 0, 63, 60 }, { 90, 167, 70 } };
		short growth = ui_widget_port_legend_row(pause_legends, 2, 0, NULL);

		CHECK(growth == 27 + 85 + 95 - 167, "%d wider, not %d", growth, 27 + 85 + 95 - 167);
		CHECK(ui_widget_port_legend_row(narrow, 2, 0, NULL) == 0, "a narrower row %d wider",
			ui_widget_port_legend_row(narrow, 2, 0, NULL));
	}
	CASE("room")
	{
		/* given the room the box was made wider by: the Xbox's gap, the
		row centred where the Xbox's was (as much past either end of it) */
		short growth = ui_widget_port_legend_row(pause_legends, 2, 0, NULL);
		short end;

		ui_widget_port_legend_row(pause_legends, 2, growth, starts);
		end = (short)(starts[1] + pause_legends[1].width);
		CHECK(starts[1] - (starts[0] + pause_legends[0].width) == 27, "the gap %d, not the Xbox's 27",
			starts[1] - (starts[0] + pause_legends[0].width));
		CHECK(-starts[0] == end - 167 || -starts[0] == end - 167 - 1, "%d past the Xbox's left, %d past its right",
			-starts[0], end - 167);
	}
	CASE("narrowed")
	{
		/* without the room (a box not made wider: a custom map's), its
		gaps narrowed to fit, to no narrower than the least */
		static struct ui_widget_port_legend const wide[] = { { 0, 63, 130 }, { 90, 167, 130 } };

		ui_widget_port_legend_row(pause_legends, 2, 0, starts);
		CHECK(starts[1] - (starts[0] + pause_legends[0].width) == MAX(27 - (27 + 85 + 95 - 167), 4),
			"the gap %d, not %d", starts[1] - (starts[0] + pause_legends[0].width),
			MAX(27 - (27 + 85 + 95 - 167), 4));
		ui_widget_port_legend_row(wide, 2, 0, starts);
		CHECK(starts[1] - (starts[0] + wide[0].width) == UI_WIDGET_PORT_LEGEND_LEAST_GAP, "the gap %d, not %d",
			starts[1] - (starts[0] + wide[0].width), UI_WIDGET_PORT_LEGEND_LEAST_GAP);
	}
	return 0;
}
