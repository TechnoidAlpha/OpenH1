/*
VEHICLE_FOV.C

display.vehicle_fov: the view and its reticle's scale (view_fov.c's
render_fov_vertical, render_fov_reticle_scale) in a vehicle's seat. A seat's
view, first person (the Shade's turret) or third (the following camera), and
the moments of entering and leaving it take display.vehicle_fov: "default"
the game's own view (the reticle at its stock size), "match" display.fov's,
a number its angle. Getting in or out, the view eases between the two, and
the reticle drawn over each frame is scaled from that frame's view, so it
never shows another frame's size. Death, a perspective the director holds
(neutral) and a scope's full zoom keep the game's view.
*/

#include "harness.h"
#include <math.h>
#include <strings.h>

#include "config.inc"

#define MAXIMUM_LOCAL_PLAYERS 4
#define _pi 3.14159265358979323846f
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define tangent tanf
#define DEGREES(d) ((d) * _pi / 180.f)
#define _stricmp strcasecmp

typedef short director_perspective;

struct unit_datum
{
	struct
	{
		long parent_object_index;
		unsigned long damage_flags;
	} object;
	long definition_index;
	struct
	{
		short current_weapon_index;
	} unit;
};

struct unit_definition
{
	struct
	{
		real camera_field_of_view;
	} unit;
};

static struct unit_datum fake_unit;
static struct unit_definition fake_definition;
static director_perspective fake_perspective;
static boolean fake_scripted;
static boolean *director_camera_scripted = &fake_scripted;
static double fake_fov;
static char const *fake_vehicle_fov = "default";
static unsigned long fake_changes = 1;
static unsigned long fake_now = 100000;
static real fake_scoped_angle;

static unsigned long config_changes(void) { return fake_changes; }
static double config_real(const char *name) { (void)name; return fake_fov; }
static char const *config_string(const char *name) { (void)name; return fake_vehicle_fov; }
static unsigned long system_milliseconds(void) { return fake_now; }
static boolean cinematic_in_progress(void) { return FALSE; }
static director_perspective director_get_perspective(short local_player_index)
{
	(void)local_player_index;
	return fake_perspective;
}
static boolean director_inhibited_facing(short local_player_index) { (void)local_player_index; return FALSE; }
static long player_control_get_unit_index(short local_player_index) { (void)local_player_index; return 7; }
static struct unit_datum *unit_try_and_get(long unit_index) { return unit_index == NONE ? NULL : &fake_unit; }
static struct unit_definition *unit_definition_get(long index) { (void)index; return &fake_definition; }
static long unit_inventory_get_weapon(long unit_index, short index) { (void)unit_index; return index == NONE ? NONE : 3; }
static real weapon_get_field_of_view(long weapon_index, real unzoomed, short zoom_level)
{
	(void)weapon_index; (void)unzoomed; (void)zoom_level;
	return fake_scoped_angle;
}

#include "under_test.inc"

/* the observer's vertical angle for a camera's (main.c's
set_window_camera_values) */
static real native_of(real camera_angle)
{
	return 2.f * atanf(0.75f * render_camera_get_adjusted_field_of_view_tangent(camera_angle));
}

/* the settings: display.fov and display.vehicle_fov */
static void settings(double fov, char const *vehicle_fov)
{
	fake_fov = fov;
	fake_vehicle_fov = vehicle_fov;
	fake_changes++;
}

/* a frame of a living player, with a weapon with a 2x scope, in this seat
and perspective, at this time: the drawn angle, and the reticle's scale */
static real frame(long parent, director_perspective perspective, real camera_angle, real *scale)
{
	real drawn;

	fake_unit.object.parent_object_index = parent;
	fake_unit.object.damage_flags = 0;
	fake_unit.unit.current_weapon_index = 0;
	fake_definition.unit.camera_field_of_view = DEGREES(70.f);
	fake_perspective = perspective;
	fake_scripted = FALSE;
	fake_scoped_angle = DEGREES(35.f);
	drawn = render_fov_vertical(0, native_of(camera_angle));
	*scale = render_fov_reticle_scale(0);
	return drawn;
}

/* the view on foot (and its reticle's scale) at display.fov's degrees, a
way between the fields of view ended first */
static real on_foot(double degrees, real *scale)
{
	settings(degrees, "match");
	fake_unit.object.damage_flags = FLAG(_object_dead_bit);
	render_fov_vertical(0, native_of(DEGREES(70.f)));
	return frame(NONE, _director_perspective_first_person, DEGREES(70.f), scale);
}

/* a seat's steady view (got in long ago) */
static real seated(director_perspective perspective, real *scale)
{
	fake_unit.object.damage_flags = FLAG(_object_dead_bit);
	render_fov_vertical(0, native_of(DEGREES(70.f)));
	frame(12, perspective, DEGREES(70.f), scale);
	fake_now += 10000;
	return frame(12, perspective, DEGREES(70.f), scale);
}

/* the reticle's scale as the drawn view gives it (render_fov_vertical's) */
static real scale_of(real drawn)
{
	return drawn == native_of(DEGREES(70.f)) ? 1.f : tanf(native_of(DEGREES(70.f)) * 0.5f) / tanf(drawn * 0.5f);
}

#define SAME(a, b) (fabsf((a) - (b)) < 1e-6f)

/* out of the seat at fake_now, frames of 16 ms after: each frame's reticle
is its own view's, the view moves from the seat's to the foot's without a
jump (no frame's step more than a fifth of the way) and in one direction,
and it is the foot's once the way is done */
static void check_exit(director_perspective leaving_perspective, real seat_drawn, real foot_drawn, real foot_scale)
{
	real drawn = seat_drawn, scale = 1.f, last = seat_drawn;
	int step;

	for (step = 0; step <= 40; step++)
	{
		drawn = frame(NONE, step < 20 ? leaving_perspective : _director_perspective_first_person, DEGREES(70.f),
			&scale);
		CHECK(SAME(scale, scale_of(drawn)), "frame %d: the reticle's scale %f is not its view's (%f)", step, scale,
			scale_of(drawn));
		CHECK(fabsf(drawn - last) <= fabsf(foot_drawn - seat_drawn) * 0.2f + 1e-6f,
			"frame %d: the view jumped from %f to %f (seat %f, on foot %f)", step, last, drawn, seat_drawn, foot_drawn);
		CHECK((foot_drawn - seat_drawn) * (drawn - last) >= -1e-6f, "frame %d: the view went back, %f to %f", step,
			last, drawn);
		if (step == 0)
			CHECK(SAME(drawn, seat_drawn), "the first frame out was %f, not the seat's %f", drawn, seat_drawn);
		last = drawn;
		fake_now += 16;
	}
	CHECK(SAME(drawn, foot_drawn) && SAME(scale, foot_scale), "after the way: %f (scale %f), on foot %f (scale %f)",
		drawn, scale, foot_drawn, foot_scale);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real foot_scale, scale, drawn;
	real native = native_of(DEGREES(70.f));
	real foot = on_foot(110.0, &foot_scale);

	/* (on foot the view is wider than the game's, its reticle smaller) */
	CHECK(foot > native && foot_scale < 0.9f, "on foot: %f, scale %f", foot, foot_scale);

	CASE("turret_default")
	{
		/* the Shade's turret, first person in the seat: the game's view,
		the reticle at its stock size, as before display.fov reached
		vehicles */
		settings(110.0, "default");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(drawn == native && scale == 1.f, "the turret's view %f (scale %f), the game's %f", drawn, scale, native);
	}
	CASE("turret_match")
	{
		settings(110.0, "match");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "the turret's view %f (scale %f), on foot %f (scale %f)",
			drawn, scale, foot, foot_scale);
		/* (written in capitals too) */
		settings(110.0, "MATCH");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot), "MATCH: %f, on foot %f", drawn, foot);
	}
	CASE("turret_number")
	{
		/* 90 in the seat, 110 on foot: the seat's view is display.fov's at 90 */
		real foot_90_scale, foot_90 = on_foot(90.0, &foot_90_scale);

		settings(110.0, "90");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot_90) && SAME(scale, foot_90_scale) && drawn < foot && drawn > native,
			"the turret at 90: %f (scale %f), on foot at 90 %f (scale %f)", drawn, scale, foot_90, foot_90_scale);
		/* (with the stock view on foot too) */
		settings(0.0, "90");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot_90), "the turret at 90, stock on foot: %f, 90 %f", drawn, foot_90);
		/* (as display.fov reads its number: outside 20 to 150, or not a
		number, the stock view) */
		settings(110.0, "10");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(drawn == native && scale == 1.f, "10: %f", drawn);
		settings(110.0, "wide");
		drawn = seated(_director_perspective_first_person, &scale);
		CHECK(drawn == native && scale == 1.f, "wide: %f", drawn);
	}
	CASE("third_person_seat")
	{
		/* a driver's or gunner's following camera, its 70 degrees */
		settings(110.0, "default");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(drawn == native && scale == 1.f, "default: the seat's view %f (scale %f)", drawn, scale);
		settings(110.0, "match");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "match: the seat's view %f (scale %f), on foot %f", drawn,
			scale, foot);
	}
	CASE("turret_exit")
	{
		/* #441: out of the Shade's turret, display.fov 110, the turret's
		view the game's: the view and its reticle go from the seat's to the
		foot's over the way, never a frame at another's size */
		settings(110.0, "default");
		drawn = seated(_director_perspective_first_person, &scale);
		check_exit(_director_perspective_first_person, drawn, foot, foot_scale);
	}
	CASE("leaving_third_person")
	{
		/* a Warthog's seat at 130, left: the director flies the camera back
		(the third person while out of the seat), then the head */
		real wide_scale, wide = on_foot(130.0, &wide_scale);

		settings(110.0, "130");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, wide), "the seat at 130: %f, 130 %f", drawn, wide);
		check_exit(_director_perspective_third_person, drawn, foot, foot_scale);
	}
	CASE("match_exit")
	{
		/* the same angle in and out: nothing moves */
		settings(110.0, "match");
		seated(_director_perspective_first_person, &scale);
		drawn = frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "out: %f, on foot %f", drawn, foot);
	}
	CASE("entering")
	{
		/* in, from on foot: the first frame seated is the foot's view, then
		it eases to the seat's (the game's), the reticle growing with it */
		real last = foot;
		int step;

		settings(110.0, "default");
		frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		for (step = 0; step <= 40; step++)
		{
			drawn = frame(12, _director_perspective_third_person, DEGREES(70.f), &scale);
			CHECK(SAME(scale, scale_of(drawn)), "frame %d: the reticle's scale %f, its view's %f", step, scale,
				scale_of(drawn));
			CHECK(drawn <= last + 1e-6f && last - drawn <= (foot - native) * 0.2f + 1e-6f,
				"frame %d: %f after %f", step, drawn, last);
			if (step == 0)
				CHECK(SAME(drawn, foot), "the first frame seated was %f, on foot %f", drawn, foot);
			last = drawn;
			fake_now += 16;
		}
		CHECK(drawn == native && scale == 1.f, "seated after the way: %f (scale %f)", drawn, scale);
	}
	CASE("turned_back")
	{
		/* out and straight back in: the way turns back from where it is,
		without a jump */
		real last;
		int step;

		settings(110.0, "default");
		last = seated(_director_perspective_first_person, &scale);
		for (step = 0; step < 50; step++)
		{
			drawn = frame(step < 10 ? NONE : 12, _director_perspective_first_person, DEGREES(70.f), &scale);
			CHECK(fabsf(drawn - last) <= (foot - native) * 0.2f + 1e-6f, "frame %d: %f after %f", step, drawn, last);
			CHECK(SAME(scale, scale_of(drawn)), "frame %d: the reticle's scale %f, its view's %f", step, scale,
				scale_of(drawn));
			last = drawn;
			fake_now += 16;
		}
		CHECK(drawn == native, "back in the seat: %f", drawn);
	}
	CASE("respawn")
	{
		/* killed in a seat and back on foot: no way from the seat's view,
		the foot's at once */
		settings(110.0, "default");
		seated(_director_perspective_first_person, &scale);
		fake_unit.object.damage_flags = FLAG(_object_dead_bit);
		drawn = render_fov_vertical(0, native);
		CHECK(drawn == native && render_fov_reticle_scale(0) == 1.f, "dead: %f", drawn);
		drawn = frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "respawned: %f (scale %f), on foot %f", drawn, scale, foot);
	}
	CASE("scoped_in_seat")
	{
		/* a scope's full zoom keeps its stock view, seated too */
		settings(110.0, "130");
		seated(_director_perspective_first_person, &scale);
		drawn = frame(12, _director_perspective_first_person, DEGREES(35.f), &scale);
		CHECK(drawn == native_of(DEGREES(35.f)) && scale == 1.f, "scoped: %f (scale %f)", drawn, scale);
	}
	CASE("dead")
	{
		settings(110.0, "match");
		frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		fake_unit.object.damage_flags = FLAG(_object_dead_bit);
		drawn = render_fov_vertical(0, native);
		CHECK(drawn == native && render_fov_reticle_scale(0) == 1.f, "dead: %f (scale %f)", drawn,
			render_fov_reticle_scale(0));
	}
	CASE("neutral")
	{
		settings(110.0, "match");
		drawn = frame(NONE, _director_perspective_neutral, DEGREES(70.f), &scale);
		CHECK(drawn == native && scale == 1.f, "neutral: %f (scale %f)", drawn, scale);
	}
	CASE("scripted")
	{
		settings(110.0, "match");
		seated(_director_perspective_first_person, &scale);
		fake_scripted = TRUE;
		drawn = render_fov_vertical(0, native);
		CHECK(drawn == native && render_fov_reticle_scale(0) == 1.f, "scripted: %f (scale %f)", drawn,
			render_fov_reticle_scale(0));
	}
	return 0;
}
