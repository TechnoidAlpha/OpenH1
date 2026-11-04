/*
LENS_FLARE_TESTS.C

The lens flares' visibility tests read back frames late (source/rasterizer/
rasterizer_lights.c): rasterizer_lens_flare_submit, the tests submitted
(rasterizer_lens_flares_submit_occlusion_tests) and read at the next frame's
start (rasterizer_lights_begin_for_new_frame), against a fake device whose
counts reach the game a number of frames after their tests, numbered as
port/linux/src/d3d8_device.c numbers them. A flare is a made-up light (its
identity in its position's x), in the open (all its pixels pass) or behind
a wall (none do).
*/

#include "harness.h"
#include <math.h>

typedef unsigned short word;
typedef unsigned int UINT;
#define MAXIMUM_LIGHTS_PER_MAP 4096
#define MAXIMUM_LENS_FLARES_PER_LIGHT 8
#define MAXIMUM_QUEUED_LENS_FLARES 8
#define MAXIMUM_WINDOWS 4
#define MAXIMUM_LENS_FLARE_MARKERS_PER_STRUCTURE 65536
#define MAXIMUM_LENS_FLARES_PER_FRAME 1024
#define TICKS_PER_SECOND 30
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#define error(level, ...) ((void)0)
#define vassert(condition, message) CHECK(condition, "%s", message)
#define rasterizer_profile_begin(section) ((void)0)
#define rasterizer_profile_end(section) ((void)0)
#define _rasterizer_widget_z_enable_bit 0
#define FAKE_FLARES 8192

struct lens_flare_definition
{
	real far_fade_distance;
	real occlusion_radius;
	short occlusion_offset_direction;
};

static struct
{
	boolean draw_lens_flares;
	boolean lens_flare_occlusion_enabled;
	short statistics_mode;
} rasterizer_debug_options = { TRUE, TRUE, 0 };
static struct { long lens_flare_count; } rasterizer_frame_statistics;
static struct
{
	short window_index;
	short rasterizer_target;
	struct { real_point3d position; real_vector3d forward, up; } camera;
	long frustum;
} global_window_parameters;
short global_screenshot_count = 0;
static short global_screenshot_size = 0;

static boolean render_frustum_sphere_visible(long const *frustum, real_point3d const *center, real radius)
{
	return TRUE;
}
static boolean viewmodel_draws_geometry(boolean first_person) { return TRUE; }
static void viewmodel_projection_begin(void) {}
static void viewmodel_projection_end(void) {}
static void rasterizer_widget_begin(short type, long flags) {}
static void rasterizer_widget_end(void) {}
static real main_get_seconds_elapsed(void) { return 1.0f / 30.0f; }
static void vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *out)
{
	out->i = b->x - a->x; out->j = b->y - a->y; out->k = b->z - a->z;
}
static real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}
static void point_from_line3d(real_point3d const *p, real_vector3d const *v, real scale, real_point3d *out)
{
	out->x = p->x + v->i * scale; out->y = p->y + v->j * scale; out->z = p->z + v->k * scale;
}
static real_vector3d uncompress_int32_to_real_vector3d(unsigned long compressed)
{
	real_vector3d zero = { 0.0f, 0.0f, 0.0f };
	return zero;
}

/* ---------- the fake device: a test's count reaches the game latency frames
after the frame it was made in; a slot's count is its latest test's */

static struct { long count; unsigned long test; } published[4096];
static struct { long frame; long slot; unsigned long test; long count; } pending[1 << 16];
static long pending_count;
static unsigned long device_number;
static long device_frame;
static long latency = 3;
/* the flares' truths and their tests' pixels, by identity */
static boolean flare_in_open[FAKE_FLARES];
static long flare_pixels[FAKE_FLARES];

static long rasterizer_widget_submit_occlusion_test(real_point3d const *point, real radius, long index)
{
	long flare = (long)point->x;

	CHECK(index >= 0 && index < 4095, "a slot of the flares': %ld", index);
	if (!rasterizer_debug_options.lens_flare_occlusion_enabled)
		return 1;
	device_number = (device_number + 1) & 0xffffffffUL;
	if (!device_number)
		device_number = 1;
	CHECK(pending_count < (1 << 16), "pending tests");
	pending[pending_count].frame = device_frame;
	pending[pending_count].slot = index;
	pending[pending_count].test = device_number;
	pending[pending_count].count = flare_in_open[flare] ? flare_pixels[flare] : 0;
	pending_count++;
	return flare_pixels[flare];
}

static unsigned long d3d8_visibility_test_number(void)
{
	return device_number;
}

static long rasterizer_widget_port_occlusion_test_count(long index, unsigned long *test)
{
	*test = published[index].test;
	return published[index].count;
}

static long rasterizer_widget_get_occlusion_test_result(long index)
{
	if (!rasterizer_debug_options.lens_flare_occlusion_enabled)
		return 1;
	return published[index].count;
}

/* the frame's end: the tests made latency frames before are known */
static void device_frame_end(void)
{
	long index, kept = 0;

	for (index = 0; index < pending_count; index++)
	{
		if (pending[index].frame <= device_frame - latency + 1)
		{
			published[pending[index].slot].count = pending[index].count;
			published[pending[index].slot].test = pending[index].test;
		}
		else
		{
			pending[kept++] = pending[index];
		}
	}
	pending_count = kept;
	device_frame++;
}

#include "under_test.inc"

/* ---------- the game's frames */

static struct lens_flare_definition definition = { 0.0f, 0.1f, _lens_flare_occlusion_offset_direction_none };
static byte *flare_result[FAKE_FLARES];
/* the frame each flare was last listed in, and the first of its run of frames */
static long flare_listed[FAKE_FLARES];
static boolean flare_shown[FAKE_FLARES];

/* a light's flare (identifier not NONE) or a structure's (NONE: light_index
the marker's index's high bits, lens_flare_index its low), into the frame's
list */
static void submit(long flare, short light_index, short identifier, short lens_flare_index, short window)
{
	struct rasterizer_lens_flare_submit_parameters parameters;
	long before = local_lens_flare_count;

	memset(&parameters, 0, sizeof(parameters));
	parameters.definition = &definition;
	parameters.position.x = (real)flare;
	parameters.compressed_light_color = 0xffffffffUL;
	parameters.compressed_light_scale = 0xff;
	parameters.light_index = light_index;
	parameters.light_identifier = identifier;
	parameters.lens_flare_index = lens_flare_index;
	parameters.compressed_window_index = (byte)window;
	global_window_parameters.window_index = window;
	rasterizer_lens_flare_submit(&parameters);
	CHECK(local_lens_flare_count == before + 1, "flare %ld listed", flare);
	flare_result[flare] = lens_flare_occlusion_test_results_get(&local_lens_flare_parameters[before]);
	flare_listed[flare] = device_frame;
}

/* the frame's start: last frame's tests read, then each flare listed last
frame checked: one behind a wall never shows; one in the open, once shown,
never goes out */
static void frame_begin(void)
{
	long index;

	rasterizer_lights_begin_for_new_frame();
	for (index = 0; index < FAKE_FLARES; index++)
	{
		if (!flare_result[index] || flare_listed[index] != device_frame - 1)
			continue;
		if (!flare_in_open[index])
			CHECK(*flare_result[index] == 0, "flare %ld behind a wall shown at frame %ld: %d", index, device_frame,
				*flare_result[index]);
		else if (flare_shown[index])
			CHECK(*flare_result[index] > 0, "flare %ld in the open gone out at frame %ld", index, device_frame);
		if (*flare_result[index])
			flare_shown[index] = TRUE;
	}
}

static void frame_end(short windows)
{
	short window;

	for (window = 0; window < windows; window++)
	{
		global_window_parameters.window_index = window;
		rasterizer_lens_flares_submit_occlusion_tests();
	}
	device_frame_end();
}

static unsigned long random_state = 12345;

static unsigned long random_next(void)
{
	random_state = random_state * 1103515245UL + 12345UL;
	return (random_state >> 16) & 0x7fff;
}

/* 40 lights' flares and 20 structure markers', half in the open, listed in
a new order each frame, some coming into view and leaving it; each in the
open shown at the end */
static void shuffled(void)
{
	long order[60];
	long frame, index;

	for (index = 0; index < 60; index++)
	{
		flare_in_open[index] = (index % 2) == 0;
		flare_pixels[index] = 100;
	}
	for (frame = 0; frame < 200; frame++)
	{
		frame_begin();
		for (index = 0; index < 60; index++)
			order[index] = index;
		for (index = 59; index > 0; index--)
		{
			long other = (long)(random_next() % (unsigned long)(index + 1));
			long swap = order[index];

			order[index] = order[other];
			order[other] = swap;
		}
		for (index = 0; index < 60; index++)
		{
			long flare = order[index];

			/* (a few out of view a while, until the last 40 frames) */
			if (frame < 160 && (random_next() % 5) == 0)
				continue;
			if (flare < 40)
				submit(flare, (short)(100 + flare), (short)(0x1000 + flare), 0, 0);
			else
				submit(flare, 0, NONE, (short)(flare * 3), 0);
		}
		frame_end(1);
	}
	frame_begin();
	for (index = 0; index < 60; index++)
	{
		if (flare_in_open[index])
			CHECK(*flare_result[index] == 255, "flare %ld in the open shown wholly at the end: %d", index,
				*flare_result[index]);
	}
}

int main(int argc, char **argv)
{
	char const *case_name;
	long frame, index;

	CHECK(argc == 2, "one named case");
	case_name = argv[1];
	memset(flare_listed, 0xff, sizeof(flare_listed));
	global_window_parameters.camera.forward.i = 1.0f;
	rasterizer_lights_reset_for_new_map();
	if (!strcmp(case_name, "shuffled") || !strcmp(case_name, "shuffled-latency-1") ||
		!strcmp(case_name, "shuffled-latency-6"))
	{
		latency = !strcmp(case_name, "shuffled") ? 3 : !strcmp(case_name, "shuffled-latency-1") ? 1 : 6;
		shuffled();
		return 0;
	}
	if (!strcmp(case_name, "turnover"))
	{
		/* sets of 900 lights, each set's for 35 frames, then another's (new
		lights): the slots go round, and a slot is given to a flare after
		another's, in the open or not */
		long round;

		for (round = 0; round < 12; round++)
		{
			for (index = 0; index < 900; index++)
			{
				long flare = round % 8 * 900 + index;

				flare_in_open[flare] = (random_next() % 2) == 0;
				flare_pixels[flare] = 100;
				flare_shown[flare] = FALSE;
			}
			for (frame = 0; frame < 35; frame++)
			{
				frame_begin();
				for (index = 0; index < 900; index++)
				{
					long flare = round % 8 * 900 + index;

					submit(flare, (short)((round * 900 + index) % MAXIMUM_LIGHTS_PER_MAP), (short)(0x1000 + round), 0, 0);
				}
				frame_end(1);
			}
		}
		frame_begin();
		return 0;
	}
	if (!strcmp(case_name, "identifier-reused"))
	{
		/* a light in the open deleted, and its datum's index taken at once
		by a light behind a wall: the new light's flare never shows */
		flare_in_open[1] = TRUE;
		flare_pixels[1] = 100;
		flare_pixels[2] = 100;
		for (frame = 0; frame < 60; frame++)
		{
			frame_begin();
			submit(frame < 30 ? 1 : 2, 5, (short)(frame < 30 ? 0x1001 : 0x1002), 0, 0);
			frame_end(1);
		}
		frame_begin();
		CHECK(*flare_result[2] == 0, "the new light's flare: %d", *flare_result[2]);
		return 0;
	}
	if (!strcmp(case_name, "windows"))
	{
		/* one light seen in two windows (split screen): in the open in the
		first, behind a wall in the second */
		flare_in_open[1] = TRUE;
		flare_pixels[1] = flare_pixels[2] = 100;
		for (frame = 0; frame < 60; frame++)
		{
			frame_begin();
			submit(1, 5, 0x1001, 0, 0);
			submit(2, 5, 0x1001, 0, 1);
			frame_end(2);
		}
		frame_begin();
		CHECK(*flare_result[1] == 255 && *flare_result[2] == 0, "the first window's %d, the second's %d",
			*flare_result[1], *flare_result[2]);
		return 0;
	}
	if (!strcmp(case_name, "growing"))
	{
		/* a flare in the open coming nearer: its test's pixels more each
		frame; its count is divided by its own test's, shown wholly */
		flare_in_open[1] = TRUE;
		for (frame = 0; frame < 60; frame++)
		{
			frame_begin();
			flare_pixels[1] = 100 + 20 * frame;
			submit(1, 5, 0x1001, 0, 0);
			frame_end(1);
		}
		frame_begin();
		CHECK(*flare_result[1] == 255, "shown at %d", *flare_result[1]);
		return 0;
	}
	if (!strcmp(case_name, "tests-off"))
	{
		/* the tests turned off (no test made: the device's count is 1):
		every flare shown wholly, as the game had it */
		rasterizer_debug_options.lens_flare_occlusion_enabled = FALSE;
		flare_pixels[1] = flare_pixels[2] = 100;
		flare_in_open[1] = flare_in_open[2] = TRUE;
		for (frame = 0; frame < 60; frame++)
		{
			frame_begin();
			submit(1, 5, 0x1001, 0, 0);
			submit(2, 6, 0x1002, 0, 0);
			frame_end(1);
		}
		frame_begin();
		CHECK(*flare_result[1] == 255 && *flare_result[2] == 255, "shown at %d and %d", *flare_result[1],
			*flare_result[2]);
		return 0;
	}
	CHECK(FALSE, "unknown case %s", case_name);
	return 1;
}
