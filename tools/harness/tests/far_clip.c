/*
FAR_CLIP.C

display.draw_distance (port/linux/game/far_clip.c's render_far_clip_distance):
the far clip plane the local views are drawn to. By default it is the game's
own, exactly; a farther setting replaces the game's 1024, and the farther
of the two is kept when a script sets a farther one, but a script's nearer
far clip is kept whatever the setting; values out of 1024 to 10000 (or
not a number) are the game's.
*/

#include "harness.h"

struct
{
	real far_clip_distance;
} rasterizer_globals = { 1024.f };

static struct
{
	real near_clip_distance;
	real far_clip_distance;
} const rasterizer_global_defaults = { 0.0625f, 1024.f };

static double fake_setting = 1024.0;
static unsigned long fake_changes = 1;

static double config_real(const char *name) { (void)name; return fake_setting; }
static unsigned long config_changes(void) { return fake_changes; }

#include "under_test.inc"

/* the setting, as a change of config.toml */
static void setting(double distance)
{
	fake_setting = distance;
	fake_changes++;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real drawn;

	CASE("default")
	{
		/* the game's own, the same float */
		setting(1024.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == rasterizer_globals.far_clip_distance, "default: %f", drawn);
	}
	CASE("farther")
	{
		setting(4096.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 4096.f, "4096: %f", drawn);
		setting(10000.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 10000.f, "10000: %f", drawn);
		/* (read again when the settings change) */
		setting(1024.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 1024.f, "back to the default: %f", drawn);
	}
	CASE("out_of_range")
	{
		setting(20000.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 1024.f, "20000: %f", drawn);
		setting(500.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 1024.f, "500: %f", drawn);
		setting(0.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 1024.f, "0 (not a number): %f", drawn);
	}
	CASE("script_nearer")
	{
		/* a script's nearer far clip (rasterizer_far_clip_distance) is kept */
		setting(10000.0);
		rasterizer_globals.far_clip_distance = 200.f;
		drawn = render_far_clip_distance();
		CHECK(drawn == 200.f, "a script's 200: %f", drawn);
	}
	CASE("script_farther")
	{
		/* a script's farther one: the farther of it and the setting */
		rasterizer_globals.far_clip_distance = 3000.f;
		setting(2048.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 3000.f, "a script's 3000, the setting 2048: %f", drawn);
		setting(8192.0);
		drawn = render_far_clip_distance();
		CHECK(drawn == 8192.f, "a script's 3000, the setting 8192: %f", drawn);
	}
	return 0;
}
