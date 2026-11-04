/*
SHOW_FRAME_RATE.C (test)

Settings > Interface's SHOW FRAME RATE (display.show_frame_rate): main.c's
main_show_frame_rate_setting, which main_framerate_render draws the frame
rate by as the console's display_framerate does, follows the setting as it
changes. test_show_frame_rate.py takes the code.
*/

#include "harness.h"

/* the setting, and config.toml's changes (port_config.c) */
static int show_frame_rate;
static unsigned long changes = 1;

static int config_boolean(const char *name)
{
	CHECK(!strcmp(name, "display.show_frame_rate"), "read %s", name);
	return show_frame_rate;
}

static unsigned long config_changes(void)
{
	return changes;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";

	if (!strcmp(test, "follows"))
	{
		CHECK(!main_show_frame_rate_setting(), "on by default");
		show_frame_rate = 1;
		changes++;
		CHECK(main_show_frame_rate_setting(), "not on once turned on");
		show_frame_rate = 0;
		changes++;
		CHECK(!main_show_frame_rate_setting(), "not off once turned off");
		return 0;
	}
	fprintf(stderr, "no case %s\n", test);
	return 2;
}
