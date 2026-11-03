/*
VEHICLE_FOV.C

display.fov's view and its reticle's scale (view_fov.c's render_fov_vertical,
render_fov_reticle_scale) in a vehicle's seat. A seat's view, first person
(the Shade's turret) or third (the following camera), and the moments of
entering and leaving it are widened as the view on foot is, so the reticle
drawn over it is scaled as on foot and keeps its size as the player gets in
and out. Death, a perspective the director holds (neutral) and a scope's
full zoom keep the game's view.
*/

#include "harness.h"
#include <math.h>

#include "config.inc"

#define MAXIMUM_LOCAL_PLAYERS 4
#define _pi 3.14159265358979323846f
#define PIN(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define tangent tanf
#define DEGREES(d) ((d) * _pi / 180.f)

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
static real fake_scoped_angle;

static unsigned long config_changes(void) { return 1; }
static double config_real(const char *name) { (void)name; return fake_fov; }
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

/* a living player, display.fov 110, a weapon with a 2x scope, in this seat
and perspective: the drawn angle, and the reticle's scale */
static real view(long parent, director_perspective perspective, real camera_angle, real *scale)
{
	real drawn;

	memset(&fake_unit, 0, sizeof(fake_unit));
	fake_unit.object.parent_object_index = parent;
	fake_unit.unit.current_weapon_index = 0;
	fake_definition.unit.camera_field_of_view = DEGREES(70.f);
	fake_perspective = perspective;
	fake_scripted = FALSE;
	fake_fov = 110.0;
	fake_scoped_angle = DEGREES(35.f);
	drawn = render_fov_vertical(0, native_of(camera_angle));
	*scale = render_fov_reticle_scale(0);
	return drawn;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real foot_scale, scale;
	real foot = view(NONE, _director_perspective_first_person, DEGREES(70.f), &foot_scale);

	/* (on foot the view is wider than the game's, its reticle smaller) */
	CHECK(foot > native_of(DEGREES(70.f)) && foot_scale < 0.9f, "on foot: %f, scale %f", foot, foot_scale);

	CASE("turret")
	{
		/* the Shade's turret: first person in the seat */
		real drawn = view(12, _director_perspective_first_person, DEGREES(70.f), &scale);

		CHECK(fabsf(drawn - foot) < 1e-6f && fabsf(scale - foot_scale) < 1e-6f,
			"the turret's view %f (scale %f), on foot %f (scale %f)", drawn, scale, foot, foot_scale);
	}
	CASE("third_person_seat")
	{
		/* a driver's or gunner's following camera, its 70 degrees */
		real drawn = view(12, _director_perspective_third_person, DEGREES(70.f), &scale);

		CHECK(fabsf(drawn - foot) < 1e-6f && fabsf(scale - foot_scale) < 1e-6f,
			"the seat's view %f (scale %f), on foot %f (scale %f)", drawn, scale, foot, foot_scale);
	}
	CASE("leaving")
	{
		/* out of the seat, while the director still names the third
		person it leaves */
		real drawn = view(NONE, _director_perspective_third_person, DEGREES(70.f), &scale);

		CHECK(fabsf(drawn - foot) < 1e-6f && fabsf(scale - foot_scale) < 1e-6f,
			"leaving: %f (scale %f), on foot %f (scale %f)", drawn, scale, foot, foot_scale);
	}
	CASE("scoped_in_seat")
	{
		/* a scope's full zoom keeps its stock view, seated too */
		real drawn = view(12, _director_perspective_first_person, DEGREES(35.f), &scale);

		CHECK(drawn == native_of(DEGREES(35.f)) && scale == 1.f, "scoped: %f (scale %f)", drawn, scale);
	}
	CASE("dead")
	{
		real drawn;

		view(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		fake_unit.object.damage_flags = FLAG(_object_dead_bit);
		drawn = render_fov_vertical(0, native_of(DEGREES(70.f)));
		CHECK(drawn == native_of(DEGREES(70.f)) && render_fov_reticle_scale(0) == 1.f,
			"dead: %f (scale %f)", drawn, render_fov_reticle_scale(0));
	}
	CASE("neutral")
	{
		real drawn = view(NONE, _director_perspective_neutral, DEGREES(70.f), &scale);

		CHECK(drawn == native_of(DEGREES(70.f)) && scale == 1.f, "neutral: %f (scale %f)", drawn, scale);
	}
	CASE("scripted")
	{
		real drawn;

		view(12, _director_perspective_first_person, DEGREES(70.f), &scale);
		fake_scripted = TRUE;
		drawn = render_fov_vertical(0, native_of(DEGREES(70.f)));
		CHECK(drawn == native_of(DEGREES(70.f)) && render_fov_reticle_scale(0) == 1.f,
			"scripted: %f (scale %f)", drawn, render_fov_reticle_scale(0));
	}
	return 0;
}
