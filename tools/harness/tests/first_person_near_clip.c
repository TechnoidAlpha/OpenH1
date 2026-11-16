/*
FIRST_PERSON_NEAR_CLIP.C

The first-person weapon's near clip plane (port/linux/game/near_clip.c's
render_first_person_near_clip_distance), from the game's frustum bounds
(render_cameras.c's render_camera_build_frustum_bounds, taken as it is):
the game's own distance, exactly, at the weapon's stock angle and place in
any window; where its projection is wider than the stock angle (display.
viewmodel_fov, display.panini's magnification), the distance at which its
farthest corner is as far from the eye as the stock angle's is in the same
viewport; where the weapon is moved toward the eye (display.viewmodel_
offset_z), as much nearer, down to a 64th of the game's. Checked against a
pinhole of its own: the window's middle is the view's axis and its half
height the vertical angle's tangent, each corner's distance from the eye
on the near plane.
*/

#include "harness.h"
#include <math.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define _pi 3.14159265358979323846

typedef struct { short x0, y0, x1, y1; } rectangle2d;
typedef struct { real x0, x1, y0, y1; } real_rectangle2d;

struct render_camera
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	boolean mirrored;
	real vertical_field_of_view;
	rectangle2d viewport_bounds;
	rectangle2d window_bounds;
	real z_near;
	real z_far;
};

#include "under_test.inc"

/* the game's first-person near distance (rasterizer.c's
rasterizer_global_defaults) */
#define WEAPON_NEAR 0.01171875f
/* a world unit in centimeters (view_fov.c's CENTIMETERS_PER_WORLD_UNIT) */
#define CENTIMETERS 304.8

/* the stock angle: the cyborg's 70 degrees across 4:3, as
render_camera_get_adjusted_field_of_view_tangent widens it */
static double stock_tangent(void)
{
	return 0.75 * 0.85 * tan(35.0 * _pi / 180.0);
}

static real angle_of(double tangent)
{
	return (real)(2.0 * atan(tangent));
}

static rectangle2d whole(short width, short height)
{
	rectangle2d bounds = { 0, 0, width, height };

	return bounds;
}

/* the weapon's projection: the stock angle's tangent times widening */
static struct render_camera camera(double widening, short width, short height, rectangle2d viewport)
{
	struct render_camera result;

	memset(&result, 0, sizeof(result));
	result.vertical_field_of_view = angle_of(stock_tangent() * widening);
	result.window_bounds.x0 = 0;
	result.window_bounds.y0 = 0;
	result.window_bounds.x1 = width;
	result.window_bounds.y1 = height;
	result.viewport_bounds = viewport;
	result.z_near = 0.0625f;
	result.z_far = 1024.0f;
	return result;
}

/* the near plane's farthest point from the eye, its corners, by the
pinhole, at a vertical angle */
static double farthest_corner(struct render_camera const *view, real vertical, double z_near)
{
	double tangent = tan(vertical * 0.5);
	double middle_x = (view->window_bounds.x0 + view->window_bounds.x1) * 0.5;
	double middle_y = (view->window_bounds.y0 + view->window_bounds.y1) * 0.5;
	double half_height = (view->window_bounds.y1 - view->window_bounds.y0) * 0.5;
	short xs[2] = { view->viewport_bounds.x0, view->viewport_bounds.x1 };
	short ys[2] = { view->viewport_bounds.y0, view->viewport_bounds.y1 };
	double farthest = 0.0;
	int i, j;

	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 2; j++)
		{
			double x = (xs[i] - middle_x) / half_height * tangent;
			double y = (ys[j] - middle_y) / half_height * tangent;
			double distance = z_near * sqrt(1.0 + x * x + y * y);

			if (distance > farthest)
				farthest = distance;
		}
	}
	return farthest;
}

static real near_of(struct render_camera const *view, real stock_vertical, real z_near, double centimeters)
{
	real_rectangle2d bounds;

	render_camera_build_frustum_bounds(view, &bounds);
	return render_first_person_near_clip_distance(view, &bounds, stock_vertical, z_near,
		(real)(centimeters / CENTIMETERS));
}

/* the game's own distance, the same float */
static void check_kept(struct render_camera view, char const *what)
{
	real z_near = near_of(&view, angle_of(stock_tangent()), WEAPON_NEAR, 0.0);

	CHECK(z_near == WEAPON_NEAR, "%s: %.9f, not the game's", what, z_near);
}

/* nearer: the farthest corner as far from the eye as the stock angle's */
static void check_reaches(struct render_camera view, char const *what)
{
	real stock = angle_of(stock_tangent());
	real z_near = near_of(&view, stock, WEAPON_NEAR, 0.0);
	double reach = farthest_corner(&view, stock, WEAPON_NEAR);
	double corner = farthest_corner(&view, view.vertical_field_of_view, z_near);

	CHECK(z_near < WEAPON_NEAR && z_near > 0.0f, "%s: %.9f", what, z_near);
	CHECK(fabs(corner - reach) < 1e-6, "%s: the farthest corner %.7f from the eye, the stock angle's %.7f", what,
		corner, reach);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("stock")
	{
		/* the stock angle, unmoved, in every shape of window and split
		screen's views off their middles */
		rectangle2d top = { 0, 0, 1920, 540 };
		struct render_camera view = camera(1.0, 1920, 1080, top);

		check_kept(camera(1.0, 640, 480, whole(640, 480)), "4:3");
		check_kept(camera(1.0, 1920, 1080, whole(1920, 1080)), "16:9");
		check_kept(camera(1.0, 2560, 1080, whole(2560, 1080)), "21:9");
		check_kept(camera(1.0, 3840, 1080, whole(3840, 1080)), "32:9");
		check_kept(camera(1.0, 1080, 1920, whole(1080, 1920)), "portrait");
		view.window_bounds.y1 = 600;
		check_kept(view, "the top half, off its middle");
	}
	CASE("narrower")
	{
		/* display.viewmodel_fov narrower than the stock angle: never
		farther than the game's */
		check_kept(camera(0.6, 1920, 1080, whole(1920, 1080)), "narrower");
		check_kept(camera(0.2, 3840, 1080, whole(3840, 1080)), "much narrower, 32:9");
	}
	CASE("widened")
	{
		/* display.panini's magnification at 160 degrees (3.38), and
		display.viewmodel_fov's 150 (4.7 times the stock tangent), with
		both; 16:9, 4:3, 32:9 */
		struct render_camera view = camera(3.38, 1920, 1080, whole(1920, 1080));

		CHECK(farthest_corner(&view, view.vertical_field_of_view, WEAPON_NEAR) >
			1.5 * farthest_corner(&view, angle_of(stock_tangent()), WEAPON_NEAR),
			"160 and Panini: the game's plane no farther out than at the stock angle");
		check_reaches(view, "Panini at 160, 16:9");
		check_reaches(camera(1.2, 1920, 1080, whole(1920, 1080)), "a little wider, 16:9");
		check_reaches(camera(4.7, 1920, 1080, whole(1920, 1080)), "viewmodel_fov 150, 16:9");
		check_reaches(camera(4.7 * 3.38, 1920, 1080, whole(1920, 1080)), "viewmodel_fov 150 and Panini");
		check_reaches(camera(3.38, 1440, 1080, whole(1440, 1080)), "Panini, 4:3");
		check_reaches(camera(3.38, 3840, 1080, whole(3840, 1080)), "Panini, 32:9");
	}
	CASE("tall")
	{
		/* a portrait screen: its top and bottom farther out than its sides */
		check_reaches(camera(3.38, 1080, 1920, whole(1080, 1920)), "Panini, portrait");
	}
	CASE("split_screen")
	{
		/* views off their windows' middles: the far side's corners */
		rectangle2d top = { 0, 0, 1920, 540 };
		rectangle2d left = { 0, 0, 960, 1080 };
		struct render_camera view = camera(3.38, 1920, 1080, top);

		view.window_bounds.y1 = 600;
		check_reaches(view, "the top half, off its middle");
		view = camera(2.5, 1920, 1080, left);
		view.window_bounds.x1 = 1100;
		check_reaches(view, "the left half, off its middle");
	}
	CASE("moved")
	{
		/* display.viewmodel_offset_z: toward the eye, the plane as much
		nearer; past the game's distance, a 64th of it; away, the game's */
		struct render_camera view = camera(1.0, 1920, 1080, whole(1920, 1080));
		real stock = angle_of(stock_tangent());
		real z_near = near_of(&view, stock, WEAPON_NEAR, -2.0);

		CHECK(fabs(z_near - (WEAPON_NEAR - 2.0 / CENTIMETERS)) < 1e-7, "2 cm nearer: %.9f", z_near);
		z_near = near_of(&view, stock, WEAPON_NEAR, -20.0);
		CHECK(z_near == WEAPON_NEAR / 64.0f, "20 cm nearer: %.9f", z_near);
		z_near = near_of(&view, stock, WEAPON_NEAR, 20.0);
		CHECK(z_near == WEAPON_NEAR, "20 cm away: %.9f", z_near);
	}
	CASE("moved_widened")
	{
		/* moved and widened: the nearer of the two */
		struct render_camera view = camera(3.38, 1920, 1080, whole(1920, 1080));
		real stock = angle_of(stock_tangent());
		real widened = near_of(&view, stock, WEAPON_NEAR, 0.0);
		real z_near = near_of(&view, stock, WEAPON_NEAR, -1.0);

		CHECK(WEAPON_NEAR - 1.0 / CENTIMETERS > widened, "1 cm nearer is not the nearer of the two");
		CHECK(z_near == widened, "1 cm nearer, widened: %.9f, not %.9f", z_near, widened);
		z_near = near_of(&view, stock, WEAPON_NEAR, -3.0);
		CHECK(fabs(z_near - (WEAPON_NEAR - 3.0 / CENTIMETERS)) < 1e-7 && z_near < widened,
			"3 cm nearer, widened: %.9f", z_near);
	}
	CASE("script")
	{
		/* a script's own distance: kept at the stock angle, scaled alike
		when widened; 0 kept */
		struct render_camera view = camera(1.0, 1920, 1080, whole(1920, 1080));
		real stock = angle_of(stock_tangent());

		CHECK(near_of(&view, stock, 0.005f, 0.0) == 0.005f, "a script's: %.9f", near_of(&view, stock, 0.005f, 0.0));
		CHECK(near_of(&view, stock, 0.0f, -20.0) == 0.0f, "0: %.9f", near_of(&view, stock, 0.0f, -20.0));
		view = camera(3.38, 1920, 1080, whole(1920, 1080));
		CHECK(fabs(near_of(&view, stock, 0.005f, 0.0) / 0.005 - near_of(&view, stock, WEAPON_NEAR, 0.0) / WEAPON_NEAR) <
			1e-5, "a script's, widened: %.9f", near_of(&view, stock, 0.005f, 0.0));
	}
	return 0;
}
