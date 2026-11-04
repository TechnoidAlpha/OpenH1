/*
WHEEL_SIDEWAYS.C (test)

The mouse's wheel on a row its own left and right step through (ui_widget.c's
ui_mouse_wheel_sideways_steps: Choose Mission's cards, the row's port flag
wheel_sideways): a card a notch, a wheel turned a notch at a time stepping
the row a card each notch, but no sooner than
UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS after its last step, the notches between
dropped, so that a trackpad's or a high-resolution wheel's fast scroll moves
the row a card at a time rather than on to its end, and steps nothing once
it stops. test_wheel_sideways.py takes the game's code.
*/

#include "harness.h"

#include "config.inc"
#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("notch-a-card")
	{
		/* a wheel turned a notch at a time, even quickly (a notch each
		UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS), steps a card each notch, the
		first ever at the clock's start */
		long last = NONE;
		unsigned long now;
		long steps = 0;

		for (now = 0; now < 10 * UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS; now += UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS)
			steps += ui_mouse_wheel_sideways_steps(1, now, &last);
		CHECK(steps == 10, "ten notches stepped %ld cards", steps);
		/* (and at a calmer pace, a notch a quarter second) */
		for (steps = 0; now < 20000; now += 250)
			steps += ui_mouse_wheel_sideways_steps(1, now, &last);
		CHECK(steps == (20000 - 10 * UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS + 249) / 250,
			"a notch a quarter second stepped %ld cards", steps);
		return 0;
	}
	CASE("many-at-once")
	{
		/* many notches at once (a flick, gathered in one frame) step one card */
		long last = NONE;

		CHECK(ui_mouse_wheel_sideways_steps(8, 1000, &last) == 1, "eight notches at once not one card");
		CHECK(ui_mouse_wheel_sideways_steps(3, 1001, &last) == 0, "notches just after a step stepped again");
		CHECK(ui_mouse_wheel_sideways_steps(1, 1000 + UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS - 1, &last) == 0,
			"a notch before the interval stepped");
		CHECK(ui_mouse_wheel_sideways_steps(1, 1000 + UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS, &last) == 1,
			"a notch after the interval did not step");
		return 0;
	}
	CASE("trackpad-flick")
	{
		/* a trackpad's fast scroll, a notch every frame of 60 a second for
		half a second (300 pixels: SDL's tenth of a notch a pixel on macOS),
		steps a few cards, not the 30 notches, and nothing after it stops */
		long last = NONE;
		long steps = 0;
		long frame;

		for (frame = 0; frame < 30; frame++)
			steps += ui_mouse_wheel_sideways_steps(1, 5000 + (unsigned long)(frame * 1000 / 60), &last);
		CHECK(steps >= 1 && steps <= 500 / UI_MOUSE_WHEEL_SIDEWAYS_MILLISECONDS + 1,
			"30 notches in half a second stepped %ld cards", steps);
		CHECK(ui_mouse_wheel_sideways_steps(0, 9000, &last) == 0, "no notch stepped");
		return 0;
	}
	CASE("no-notch")
	{
		/* no notch steps nothing, and keeps no time: the next notch steps */
		long last = NONE;

		CHECK(ui_mouse_wheel_sideways_steps(0, 100, &last) == 0 && last == NONE, "no notch stepped or kept a time");
		CHECK(ui_mouse_wheel_sideways_steps(1, 101, &last) == 1, "the first notch did not step");
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 1;
}
