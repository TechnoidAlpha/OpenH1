/*
MOUSE_TARGETS_FREED.C (test)

The menus' mouse targets (ui_widget.c): the items drawn are noted with
their widgets for the next frame's processing, which resolves the pointer's
clicks against them. A widget freed before that (a map changed at the
frame's start: Save and Quit's main menu) must take its targets with it
(ui_mouse_forget_widget), else the click resolves against freed memory,
another screen's widget by then; and what the pointer did on the screen
that went is let go until the menus are drawn again
(ui_mouse_let_go_if_stale), not taken by the next screen. A widget freed
while the menus draw does not let the frame's clicks go.
test_mouse_targets_freed.py takes the game's code.
*/

#include "harness.h"

typedef struct { short x0, y0, x1, y1; } rectangle2d;
struct widget_instance { int unused; };
struct halo_ui_pointer { signed char wheel_steps; };

#include "config.inc"

static struct ui_mouse_target ui_mouse_targets[UI_MOUSE_MAXIMUM_TARGETS];
static long ui_mouse_target_count;
static boolean ui_mouse_targets_stale;
static boolean ui_mouse_rendering;
static short ui_mouse_presses[UI_MOUSE_MAXIMUM_PRESSES];
static long ui_mouse_press_count;
static boolean ui_mouse_hover_pending;
static boolean ui_mouse_click_pending;

#include "under_test.inc"

/* the pause menu's five rows, as its drawing notes them */
static struct widget_instance pause_rows[5];

static void draw_pause_menu(void)
{
	long index;

	ui_mouse_rendering = TRUE;
	ui_mouse_target_count = 0;
	for (index = 0; index < 5; index++)
	{
		struct ui_mouse_target *target = &ui_mouse_targets[ui_mouse_target_count++];

		memset(target, 0, sizeof(*target));
		target->widget = &pause_rows[index];
		target->bounds.x0 = 72;
		target->bounds.x1 = 274;
		target->bounds.y0 = (short)(171 + 30 * index);
		target->bounds.y1 = (short)(198 + 30 * index);
	}
	ui_mouse_targets_stale = FALSE;
	ui_mouse_rendering = FALSE;
}

/* every screen closed, as a map change does (ui_widgets_close_all) */
static void close_all(void)
{
	long index;

	for (index = 0; index < 5; index++)
		ui_mouse_forget_widget(&pause_rows[index]);
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";
	/* Save and Quit's row */
	short x = 172, y = 303;

	if (!strcmp(name, "freed"))
	{
		draw_pause_menu();
		CHECK(ui_mouse_target_at(x, y) && ui_mouse_target_at(x, y)->widget == &pause_rows[4], "the row is a target");
		ui_mouse_forget_widget(&pause_rows[4]);
		CHECK(!ui_mouse_target_at(x, y), "a freed widget's target is still resolved");
		CHECK(ui_mouse_target_count == 4, "the other rows' targets went with it: %ld left", ui_mouse_target_count);
		CHECK(ui_mouse_target_at(x, 180) && ui_mouse_target_at(x, 180)->widget == &pause_rows[0],
			"the first row's target is not kept");
		close_all();
		CHECK(ui_mouse_target_count == 0, "%ld targets of freed widgets left", ui_mouse_target_count);
		return 0;
	}
	if (!strcmp(name, "let-go"))
	{
		struct halo_ui_pointer pointer = { 3 };

		draw_pause_menu();
		close_all();
		/* the click made while the main menu loaded, and what it queued */
		ui_mouse_click_pending = TRUE;
		ui_mouse_hover_pending = TRUE;
		ui_mouse_presses[ui_mouse_press_count++] = 0;
		ui_mouse_let_go_if_stale(&pointer);
		CHECK(!ui_mouse_click_pending, "the click on the screen that went is taken by the next");
		CHECK(!ui_mouse_hover_pending, "the move on the screen that went is taken by the next");
		CHECK(ui_mouse_press_count == 0, "the presses queued for the screen that went are kept");
		CHECK(pointer.wheel_steps == 0, "the wheel's steps are kept");
		return 0;
	}
	if (!strcmp(name, "drawn"))
	{
		struct halo_ui_pointer pointer = { 2 };

		/* a click on the screen drawn, its widgets all there: resolved */
		draw_pause_menu();
		ui_mouse_click_pending = TRUE;
		ui_mouse_let_go_if_stale(&pointer);
		CHECK(ui_mouse_click_pending && pointer.wheel_steps == 2, "a click on the screen drawn is let go");
		/* a widget freed while the menus draw (they go on to note the rest) */
		ui_mouse_rendering = TRUE;
		ui_mouse_forget_widget(&pause_rows[2]);
		ui_mouse_rendering = FALSE;
		ui_mouse_let_go_if_stale(&pointer);
		CHECK(ui_mouse_click_pending, "a widget freed while the menus draw lets the click go");
		CHECK(!ui_mouse_target_at(x, 240), "the widget freed while drawing keeps its target");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", name);
	return 2;
}
