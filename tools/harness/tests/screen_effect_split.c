/*
SCREEN_EFFECT_SPLIT.C (test)

The cinematic screen effect's parameters drawn
(source/rasterizer/rasterizer_cinematics.c's
rasterizer_screen_effect_get_cinematic_parameters), with one view and with
split screen's several. The Xbox stopped on a blur with more than one view
(its split screen was local co-op, whose scripts blur only in cutscenes,
which have one); network co-op runs the single-player scripts, and the
Pillar of Autumn's cryo tube blurs the first player's waking on Normal
outside a cutscene, with a split screen's second player up. Split screen's
views are drawn without the blur and video, keeping the filters, and the
blur comes back with one view. test_screen_effect_split.py takes the
structures (structures.inc) and the code (under_test.inc).
*/

#include "harness.h"

#include <stddef.h>

typedef unsigned short word;
typedef struct { real red, green, blue; } real_rgb_color;
#define _real_epsilon 0.0001f
#define TICKS_PER_SECOND 30
#define PIN(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#define csmemset memset
#define csmemcmp memcmp
#define csmemcpy memcpy

#include "structures.inc"

static real_rgb_color const black = { 0.0f, 0.0f, 0.0f }, green = { 0.0f, 1.0f, 0.0f };
static real_rgb_color const *global_real_rgb_black = &black, *global_real_rgb_green = &green;

static long game_time;
static short window_count = 1;

static long game_time_get(void) { return game_time; }
static short main_get_window_count(void) { return window_count; }
static void scalars_interpolate(real a, real b, real t, real *result) { *result = a + (b - a) * t; }
static void scalars_interpolate_and_clamp_0_to_1(real a, real b, real t, real *result)
{
	*result = PIN(a + (b - a) * t, 0.0f, 1.0f);
}

static struct rasterizer_cinematic_screen_effect_state state;
static struct rasterizer_cinematic_screen_effect_state *cinematic_screen_effect_globals = &state;
static boolean port_screen_effect_parameters_given = FALSE;

#include "under_test.inc"

/* a10's tutorial_setup: (cinematic_screen_effect_start true), its filter
and (cinematic_screen_effect_set_convolution 1 2 10 0.001 5) */
static void cryo_waking(void)
{
	game_time = 0;
	rasterizer_screen_effect_start(TRUE);
	rasterizer_screen_effect_set_filter(1.0f, 0.0f, 1.0f, 0.0f, TRUE, 5.0f);
	rasterizer_screen_effect_set_convolution(1, 2, 10.0f, 0.001f, 5.0f);
	game_time = TICKS_PER_SECOND;
}

static struct rasterizer_cinematic_screen_effect_parameters *drawn(short windows)
{
	static struct rasterizer_cinematic_screen_effect_parameters given;

	memset(&given, 0, sizeof(given));
	window_count = windows;
	return rasterizer_screen_effect_get_cinematic_parameters(&given);
}

int main(
	int argc,
	char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";

	if (!strcmp(test, "one-view"))
	{
		struct rasterizer_cinematic_screen_effect_parameters *parameters;

		cryo_waking();
		parameters = drawn(1);
		CHECK(parameters->convolution_type == 2 && parameters->convolution_radius > 7.0f,
			"one view drawn without the blur (type %d, radius %g)", parameters->convolution_type,
			parameters->convolution_radius);
		return 0;
	}
	if (!strcmp(test, "split"))
	{
		struct rasterizer_cinematic_screen_effect_parameters *parameters;

		cryo_waking();
		parameters = drawn(2);
		CHECK(parameters->convolution_type == 0 && parameters->convolution_radius == 0.0f &&
			parameters->convolution_extra_passes == 0,
			"split screen drawn with the blur (type %d, radius %g, passes %d)", parameters->convolution_type,
			parameters->convolution_radius, parameters->convolution_extra_passes);
		CHECK(parameters->filter_light_enhancement_intensity > 0.5f && parameters->filter_desaturation_intensity > 0.5f,
			"split screen lost the filters (%g, %g)", parameters->filter_light_enhancement_intensity,
			parameters->filter_desaturation_intensity);
		/* (the script's blur kept for one view again: a cutscene's) */
		parameters = drawn(1);
		CHECK(parameters->convolution_type == 2 && parameters->convolution_radius > 7.0f,
			"the blur did not come back with one view (type %d, radius %g)", parameters->convolution_type,
			parameters->convolution_radius);
		return 0;
	}
	if (!strcmp(test, "split-video"))
	{
		struct rasterizer_cinematic_screen_effect_parameters *parameters;

		cryo_waking();
		state.parameters.video_on = TRUE;
		parameters = drawn(2);
		CHECK(!parameters->video_on, "split screen drawn with video");
		CHECK(state.parameters.video_on, "the script's video was turned off");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", test);
	return 2;
}
