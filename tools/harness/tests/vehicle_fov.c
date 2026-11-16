/*
VEHICLE_FOV.C

display.vehicle_fov: the view and its reticle's scale (view_fov.c's
render_fov_vertical, render_fov_reticle_scale) in a vehicle's seat. A
third-person seat (the director's following camera: a driver's, a
gunner's, display.passenger_third_person's passenger), and the moments of
entering and leaving it, take display.vehicle_fov: "default" the game's own
view (the reticle at its stock size), "match" display.fov's, a number its
angle. A first-person seat (a passenger's, the Shade's turret) takes
display.fov, as on foot, getting into it too (#775). Getting in or out,
or from one kind of seat to the other, the view eases between the two, and
the reticle drawn over each frame is scaled from that frame's view, so it
never shows another frame's size. Each local player has their own. Death, a perspective the
director holds (neutral) and a scope's full zoom keep the game's view.
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
		short parent_seat_index;
	} unit;
};

struct unit_seat
{
	unsigned long flags;
};

struct unit_definition
{
	struct
	{
		real camera_field_of_view;
		struct
		{
			long count;
			struct unit_seat *elements;
		} seats;
	} unit;
};

#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&(block)->elements[index])

/* each local player's unit (player_control_get_unit_index: 7 on) */
static struct unit_datum fake_units[MAXIMUM_LOCAL_PLAYERS];
#define fake_unit (fake_units[0])
/* the seats, as their parent objects: a vehicle whose seat 0 has the
following camera (a driver's), and one whose seat 0 is seen from the head
(a passenger's, its getting in seen in third person) */
#define THIRD_PERSON_SEAT 12
#define FIRST_PERSON_SEAT 14
static struct unit_seat fake_third_person_seat = { FLAG(_unit_seat_third_person_camera_bit) };
static struct unit_seat fake_first_person_seat = { FLAG(_unit_seat_driver_bit) };
static struct unit_definition fake_vehicle_definitions[2] = {
	{ { 0.0f, { 1, &fake_third_person_seat } } },
	{ { 0.0f, { 1, &fake_first_person_seat } } },
};
static struct unit_datum fake_vehicles[2] = {
	{ { NONE, 0 }, 1, { NONE, NONE } },
	{ { NONE, 0 }, 2, { NONE, NONE } },
};
/* display.passenger_third_person's view, for each local player */
static boolean fake_passenger_camera[MAXIMUM_LOCAL_PLAYERS];
static struct unit_definition fake_definition;
static director_perspective fake_perspective;
static boolean fake_scripted;
static boolean *director_camera_scripted = &fake_scripted;
static double fake_fov;
static char const *fake_vehicle_fov = "default";
static unsigned long fake_changes = 1;
static unsigned long fake_now = 100000;
static real fake_scoped_angle;
/* display.panini's strength (panini.c's panini_strength: 0 off) */
static real fake_panini;

static unsigned long config_changes(void) { return fake_changes; }
/* each local player's display.fov (their profile's), 0 for none: config.toml's */
static double fake_player_fov[MAXIMUM_LOCAL_PLAYERS];
static double config_player_real(int local_player, const char *name)
{
	(void)name;
	return local_player >= 0 && local_player < MAXIMUM_LOCAL_PLAYERS && fake_player_fov[local_player] ?
		fake_player_fov[local_player] : fake_fov;
}
static char const *config_player_string(int local_player, const char *name)
{
	(void)local_player;
	(void)name;
	return fake_vehicle_fov;
}
/* (display.scale_scoped_fov: off, the Xbox's zoom levels) */
static int config_player_boolean(int local_player, const char *name) { (void)local_player; (void)name; return 0; }
static void platform_log(char const *format, ...) { (void)format; }
static unsigned long system_milliseconds(void) { return fake_now; }
static boolean cinematic_in_progress(void) { return FALSE; }
/* (no Pro 2v2 game: its cap is test_pro_rules.py's) */
static boolean network_standard_pro_game(void) { return FALSE; }
static director_perspective director_get_perspective(short local_player_index)
{
	(void)local_player_index;
	return fake_perspective;
}
static boolean director_inhibited_facing(short local_player_index) { (void)local_player_index; return FALSE; }
static long player_control_get_unit_index(short local_player_index) { return 7 + local_player_index; }
static struct unit_datum *unit_try_and_get(long unit_index)
{
	if (unit_index == THIRD_PERSON_SEAT)
		return &fake_vehicles[0];
	if (unit_index == FIRST_PERSON_SEAT)
		return &fake_vehicles[1];
	return unit_index < 7 || unit_index >= 7 + MAXIMUM_LOCAL_PLAYERS ? NULL : &fake_units[unit_index - 7];
}
static boolean passenger_camera_enabled(short local_player_index) { return fake_passenger_camera[local_player_index]; }
static struct unit_definition *unit_definition_get(long index)
{
	return index == 1 || index == 2 ? &fake_vehicle_definitions[index - 1] : &fake_definition;
}
static long unit_inventory_get_weapon(long unit_index, short index) { (void)unit_index; return index == NONE ? NONE : 3; }
static real panini_strength(short local_player_index) { (void)local_player_index; return fake_panini; }
/* (the projection's magnification: none here, test_panini.py has it) */
static real panini_center_scale(void) { return 1.f; }
static real fake_widening;
static real panini_center_scale_widened(real widening) { fake_widening = widening; return 1.f; }
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
static real seated_in(long seat, director_perspective perspective, real *scale)
{
	fake_unit.object.damage_flags = FLAG(_object_dead_bit);
	render_fov_vertical(0, native_of(DEGREES(70.f)));
	frame(seat, perspective, DEGREES(70.f), scale);
	fake_now += 10000;
	return frame(seat, perspective, DEGREES(70.f), scale);
}

/* a third-person seat's (a driver's) */
static real seated(director_perspective perspective, real *scale)
{
	return seated_in(THIRD_PERSON_SEAT, perspective, scale);
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

	CASE("seat_default")
	{
		/* a driver's seat, the director's following camera: the game's
		view, the reticle at its stock size, as before display.fov reached
		vehicles */
		settings(110.0, "default");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(drawn == native && scale == 1.f, "the seat's view %f (scale %f), the game's %f", drawn, scale, native);
	}
	CASE("seat_match")
	{
		settings(110.0, "match");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "the seat's view %f (scale %f), on foot %f (scale %f)",
			drawn, scale, foot, foot_scale);
		/* (written in capitals too) */
		settings(110.0, "MATCH");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot), "MATCH: %f, on foot %f", drawn, foot);
	}
	CASE("seat_number")
	{
		/* 90 in the seat, 110 on foot: the seat's view is display.fov's at 90 */
		real foot_90_scale, foot_90 = on_foot(90.0, &foot_90_scale);

		settings(110.0, "90");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot_90) && SAME(scale, foot_90_scale) && drawn < foot && drawn > native,
			"the seat at 90: %f (scale %f), on foot at 90 %f (scale %f)", drawn, scale, foot_90, foot_90_scale);
		/* (with the stock view on foot too) */
		settings(0.0, "90");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot_90), "the seat at 90, stock on foot: %f, 90 %f", drawn, foot_90);
		/* (as display.fov reads its number: outside 20 to 150, or not a
		number, the stock view) */
		settings(110.0, "10");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(drawn == native && scale == 1.f, "10: %f", drawn);
		settings(110.0, "wide");
		drawn = seated(_director_perspective_third_person, &scale);
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
	CASE("first_person_seat")
	{
		/* (#775) a passenger's seat, or the Shade's turret, seen from the
		head: display.fov's view whatever display.vehicle_fov is, as on foot */
		real stock_scale;

		settings(110.0, "default");
		drawn = seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "default: the seat's view %f (scale %f), on foot %f",
			drawn, scale, foot);
		settings(110.0, "70");
		drawn = seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot), "70: the seat's view %f, on foot %f", drawn, foot);
		settings(110.0, "match");
		drawn = seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot), "match: the seat's view %f, on foot %f", drawn, foot);
		/* (the stock view on foot: the stock view in the seat, a number for
		the vehicle's or not) */
		settings(0.0, "130");
		drawn = seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &stock_scale);
		CHECK(drawn == native && stock_scale == 1.f, "stock on foot, 130 in vehicles: the seat's view %f", drawn);
	}
	CASE("turret_exit")
	{
		/* #441: out of the Shade's turret, display.fov 110: the view stays
		display.fov's, never a frame at another size */
		settings(110.0, "default");
		drawn = seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		CHECK(SAME(drawn, foot), "in the turret: %f, on foot %f", drawn, foot);
		check_exit(_director_perspective_first_person, drawn, foot, foot_scale);
	}
	CASE("passenger_climbing_in")
	{
		/* (#775) getting into a passenger's seat, which the director shows
		in third person: display.fov's view all the way, no dip to the
		vehicle's and back */
		int step;

		settings(110.0, "default");
		frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		for (step = 0; step <= 60; step++)
		{
			drawn = frame(FIRST_PERSON_SEAT, step < 30 ? _director_perspective_third_person :
				_director_perspective_first_person, DEGREES(70.f), &scale);
			CHECK(SAME(drawn, foot) && SAME(scale, foot_scale), "frame %d: %f (scale %f), on foot %f", step, drawn,
				scale, foot);
			fake_now += 16;
		}
	}
	CASE("passenger_camera")
	{
		/* display.passenger_third_person's passenger, followed in third
		person: display.vehicle_fov's view */
		settings(110.0, "default");
		fake_passenger_camera[0] = TRUE;
		drawn = seated_in(FIRST_PERSON_SEAT, _director_perspective_third_person, &scale);
		fake_passenger_camera[0] = FALSE;
		CHECK(drawn == native && scale == 1.f, "the passenger followed: %f (scale %f), the game's %f", drawn, scale,
			native);
	}
	CASE("seat_change")
	{
		/* from a passenger's seat to the driver's and back (the director's
		view from the head to the following camera): the view eases from
		display.fov's to the vehicle's and back, without a jump */
		real last;
		int step;

		settings(110.0, "default");
		last = seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		CHECK(SAME(last, foot), "the passenger's view %f, on foot %f", last, foot);
		for (step = 0; step <= 80; step++)
		{
			long seat = step < 40 ? THIRD_PERSON_SEAT : FIRST_PERSON_SEAT;

			drawn = frame(seat, seat == THIRD_PERSON_SEAT ? _director_perspective_third_person :
				_director_perspective_first_person, DEGREES(70.f), &scale);
			CHECK(SAME(scale, scale_of(drawn)), "frame %d: the reticle's scale %f, its view's %f", step, scale,
				scale_of(drawn));
			CHECK(fabsf(drawn - last) <= (foot - native) * 0.2f + 1e-6f, "frame %d: %f after %f", step, drawn, last);
			if (step == 0)
				CHECK(SAME(drawn, foot), "the first frame in the driver's seat was %f, on foot %f", drawn, foot);
			if (step == 39)
				CHECK(drawn == native, "in the driver's seat after the way: %f, the game's %f", drawn, native);
			last = drawn;
			fake_now += 16;
		}
		CHECK(SAME(drawn, foot), "back in the passenger's seat: %f, on foot %f", drawn, foot);
	}
	CASE("split_screen")
	{
		/* two local players, one a passenger and one the driver: each view
		its own */
		real second;

		settings(110.0, "default");
		seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		fake_units[1] = fake_units[0];
		fake_units[1].object.parent_object_index = THIRD_PERSON_SEAT;
		fake_perspective = _director_perspective_first_person;
		render_fov_vertical(1, native);
		fake_now += 10000;
		drawn = render_fov_vertical(0, native);
		second = render_fov_vertical(1, native);
		CHECK(SAME(drawn, foot) && SAME(render_fov_reticle_scale(0), foot_scale),
			"the passenger: %f (scale %f), on foot %f", drawn, render_fov_reticle_scale(0), foot);
		CHECK(second == native && render_fov_reticle_scale(1) == 1.f, "the driver: %f (scale %f), the game's %f",
			second, render_fov_reticle_scale(1), native);
	}
	CASE("players")
	{
		/* two local players on foot, player 2 with a field of view of their
		profile's own, player 1 with config.toml's: each view its own */
		real narrow_scale, narrow = on_foot(60.0, &narrow_scale);
		real second;

		foot = on_foot(110.0, &foot_scale);
		fake_units[1] = fake_units[0];
		fake_player_fov[1] = 60.0;
		fake_changes++;
		render_fov_vertical(1, native);
		drawn = render_fov_vertical(0, native);
		second = render_fov_vertical(1, native);
		CHECK(SAME(drawn, foot) && SAME(render_fov_reticle_scale(0), foot_scale), "player 1: %f, config.toml's %f",
			drawn, foot);
		CHECK(SAME(second, narrow) && SAME(render_fov_reticle_scale(1), narrow_scale), "player 2: %f, their own %f",
			second, narrow);
		fake_player_fov[1] = 0.0;
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
		seated(_director_perspective_third_person, &scale);
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
			drawn = frame(THIRD_PERSON_SEAT, _director_perspective_third_person, DEGREES(70.f), &scale);
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
		last = seated(_director_perspective_third_person, &scale);
		for (step = 0; step < 50; step++)
		{
			drawn = frame(step < 10 ? NONE : THIRD_PERSON_SEAT, _director_perspective_third_person, DEGREES(70.f), &scale);
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
		seated(_director_perspective_third_person, &scale);
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
		seated(_director_perspective_third_person, &scale);
		drawn = frame(THIRD_PERSON_SEAT, _director_perspective_third_person, DEGREES(35.f), &scale);
		CHECK(drawn == native_of(DEGREES(35.f)) && scale == 1.f, "scoped: %f (scale %f)", drawn, scale);
		/* (a passenger's too, and its zoom's way in from display.fov's
		view, as on foot: 50 degrees, on the way to 35, is the same view
		seated and on foot) */
		seated_in(FIRST_PERSON_SEAT, _director_perspective_first_person, &scale);
		drawn = frame(FIRST_PERSON_SEAT, _director_perspective_first_person, DEGREES(35.f), &scale);
		CHECK(drawn == native_of(DEGREES(35.f)) && scale == 1.f, "a passenger scoped: %f (scale %f)", drawn, scale);
		drawn = frame(FIRST_PERSON_SEAT, _director_perspective_first_person, DEGREES(50.f), &scale);
		{
			real seat_scale = scale, seat_drawn = drawn, foot_zooming;

			on_foot(110.0, &scale);
			foot_zooming = frame(NONE, _director_perspective_first_person, DEGREES(50.f), &scale);
			CHECK(SAME(seat_drawn, foot_zooming) && SAME(seat_scale, scale) && seat_drawn > native_of(DEGREES(50.f)),
				"a passenger zooming: %f (scale %f), on foot %f (scale %f)", seat_drawn, seat_scale, foot_zooming,
				scale);
		}
	}
	CASE("zoomed_reticle_size")
	{
		/* (#594) the weapon's reticle keeps its unzoomed size through a
		zoom's transition and at its full zoom, on foot and seated, where the
		view's own scale goes back to the stock 1 (a scope's pieces take it) */
		real foot_scale, seat_scale, angle, on_foot_drawn;

		on_foot_drawn = on_foot(110.0, &foot_scale);
		render_fov_reticle_size(0);
		CHECK(fake_widening == 1.f, "unzoomed: widened by %f", fake_widening);
		CHECK(foot_scale < 0.9f && SAME(render_fov_reticle_size(0), foot_scale),
			"unzoomed: scale %f size %f", foot_scale, render_fov_reticle_size(0));
		for (angle = DEGREES(66.f); angle > DEGREES(35.f); angle -= DEGREES(4.f))
		{
			frame(NONE, _director_perspective_first_person, angle, &scale);
			CHECK(SAME(render_fov_reticle_size(0), foot_scale) && scale > foot_scale,
				"zooming at %f: size %f, view's %f", angle, render_fov_reticle_size(0), scale);
		}
		frame(NONE, _director_perspective_first_person, DEGREES(35.f), &scale);
		CHECK(scale == 1.f && SAME(render_fov_reticle_size(0), foot_scale),
			"full zoom: size %f, view's %f", render_fov_reticle_size(0), scale);
		/* (display.panini's magnification asked of the unzoomed view: the
		zoomed one widened by the unzoomed tangent over its own) */
		CHECK(fabsf(fake_widening - tanf(on_foot_drawn * 0.5f) / tanf(native_of(DEGREES(35.f)) * 0.5f)) < 1e-4f,
			"full zoom: widened by %f", fake_widening);
		settings(110.0, "130");
		seated(_director_perspective_third_person, &seat_scale);
		frame(THIRD_PERSON_SEAT, _director_perspective_third_person, DEGREES(35.f), &scale);
		CHECK(scale == 1.f && seat_scale < foot_scale && SAME(render_fov_reticle_size(0), seat_scale),
			"seated full zoom: size %f, seat's %f", render_fov_reticle_size(0), seat_scale);
		/* the stock view's reticle is the stock size, zoomed or not */
		on_foot(0.0, &foot_scale);
		frame(NONE, _director_perspective_first_person, DEGREES(35.f), &scale);
		CHECK(foot_scale == 1.f && render_fov_reticle_size(0) == 1.f, "stock: size %f",
			render_fov_reticle_size(0));
	}
	CASE("unzooming")
	{
		/* (#747) let go, the zoom level is gone at once but the view eases
		back out, and the reticle is drawn at its scale (as a scope's piece,
		as zoomed) until the view is the unzoomed one: it slides back from
		the stock size to display.fov's as it slid in, not snapping */
		real foot_scale, angle, last;

		on_foot(110.0, &foot_scale);
		CHECK(!render_fov_zoom_easing(0), "unzoomed: easing");
		frame(NONE, _director_perspective_first_person, DEGREES(35.f), &scale);
		CHECK(render_fov_zoom_easing(0) && scale == 1.f, "full zoom: easing %d, scale %f",
			render_fov_zoom_easing(0), scale);
		last = scale;
		for (angle = DEGREES(39.f); angle < DEGREES(69.f); angle += DEGREES(4.f))
		{
			frame(NONE, _director_perspective_first_person, angle, &scale);
			CHECK(render_fov_zoom_easing(0) && scale < last && scale > foot_scale,
				"easing out at %f: easing %d, scale %f (last %f, unzoomed %f)", angle,
				render_fov_zoom_easing(0), scale, last, foot_scale);
			last = scale;
		}
		frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		CHECK(!render_fov_zoom_easing(0) && SAME(scale, foot_scale) && SAME(render_fov_reticle_size(0), foot_scale),
			"unzoomed again: easing %d, scale %f", render_fov_zoom_easing(0), scale);
		/* the stock view never eases its reticle (one size at every zoom) */
		on_foot(0.0, &foot_scale);
		frame(NONE, _director_perspective_first_person, DEGREES(50.f), &scale);
		CHECK(!render_fov_zoom_easing(0), "stock: easing");
	}
	CASE("dead")
	{
		settings(110.0, "match");
		frame(NONE, _director_perspective_first_person, DEGREES(70.f), &scale);
		fake_unit.object.damage_flags = FLAG(_object_dead_bit);
		drawn = render_fov_vertical(0, native);
		CHECK(drawn == native && render_fov_reticle_scale(0) == 1.f && render_fov_reticle_size(0) == 1.f,
			"dead: %f (scale %f, size %f)", drawn, render_fov_reticle_scale(0), render_fov_reticle_size(0));
	}
	CASE("neutral")
	{
		settings(110.0, "match");
		drawn = frame(NONE, _director_perspective_neutral, DEGREES(70.f), &scale);
		CHECK(drawn == native && scale == 1.f, "neutral: %f (scale %f)", drawn, scale);
	}
	CASE("wider_with_panini")
	{
		/* above 150 only with display.panini on: 160 wider than 150, the
		vehicle's number too; without it, 150 (panini_projection.h) */
		real foot_150_scale, foot_150 = on_foot(150.0, &foot_150_scale);
		real foot_160_scale, foot_160;

		fake_panini = 1.f;
		settings(150.0, "match");
		foot_160 = on_foot(160.0, &foot_160_scale);
		CHECK(foot_160 > foot_150 && foot_160_scale < foot_150_scale && SAME(foot_160, 2.f * atanf(tanf(DEGREES(80.f)) *
			9.f / 16.f)), "160 with the projection: %f, 150 %f", foot_160, foot_150);
		settings(150.0, "160");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot_160), "the seat at 160 with the projection: %f, on foot at 160 %f", drawn, foot_160);
		fake_panini = 0.f;
		drawn = on_foot(160.0, &scale);
		CHECK(SAME(drawn, foot_150) && SAME(scale, foot_150_scale), "160 without the projection: %f, 150 %f", drawn,
			foot_150);
		settings(150.0, "160");
		drawn = seated(_director_perspective_third_person, &scale);
		CHECK(SAME(drawn, foot_150), "the seat at 160 without the projection: %f, 150 %f", drawn, foot_150);
	}
	CASE("past_the_widest")
	{
		/* past 160, with or without the projection, the stock view (as any
		number out of range) */
		fake_panini = 1.f;
		drawn = on_foot(161.0, &scale);
		CHECK(drawn == native && scale == 1.f, "161 with the projection: %f", drawn);
		fake_panini = 0.f;
		drawn = on_foot(170.0, &scale);
		CHECK(drawn == native && scale == 1.f, "170: %f", drawn);
	}
	CASE("scripted")
	{
		settings(110.0, "match");
		seated(_director_perspective_third_person, &scale);
		fake_scripted = TRUE;
		drawn = render_fov_vertical(0, native);
		CHECK(drawn == native && render_fov_reticle_scale(0) == 1.f, "scripted: %f (scale %f)", drawn,
			render_fov_reticle_scale(0));
	}
	return 0;
}
