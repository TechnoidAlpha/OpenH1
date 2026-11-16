/*
NEAR_CLIP.C

The near clip plane of the local players' views (port/linux/game/near_clip.c's
render_near_clip_distance), from the game's frustum bounds
(render_cameras.c's render_camera_build_frustum_bounds, taken as it is):
the game's own distance, exactly, at the stock view and any whose corners
are within the reach; at a wider one, the distance at which the view's
farthest corner is the reach from the eye, never farther than the game's.
Checked against a pinhole of its own: the window's middle is the view's
axis and its half height the vertical angle's tangent, the viewport any
part of the window (split screen's views are off their windows' middles),
each corner's distance from the eye on the near plane.
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

/* the game's near distance and the reach (near_clip.c's NEAR_CLIP_REACH) */
#define GAME_NEAR 0.0625f
#define REACH 0.2f

/* a degrees-across-16:9 setting as the vertical angle (view_fov.c's
fov_tangent); 0: the stock view (the cyborg's 70 degrees across 4:3, as
render_camera_get_adjusted_field_of_view_tangent widens it) */
static real vertical_angle(double degrees)
{
	double tangent = degrees ? tan(degrees * _pi / 360.0) * (9.0 / 16.0) : 0.75 * 0.85 * tan(35.0 * _pi / 180.0);

	return (real)(2.0 * atan(tangent));
}

static struct render_camera camera(double degrees, short width, short height, rectangle2d viewport)
{
	struct render_camera result;

	memset(&result, 0, sizeof(result));
	result.vertical_field_of_view = vertical_angle(degrees);
	result.window_bounds.x0 = 0;
	result.window_bounds.y0 = 0;
	result.window_bounds.x1 = width;
	result.window_bounds.y1 = height;
	result.viewport_bounds = viewport;
	result.z_near = GAME_NEAR;
	result.z_far = 1024.0f;
	return result;
}

static rectangle2d whole(short width, short height)
{
	rectangle2d bounds = { 0, 0, width, height };

	return bounds;
}

/* the near plane's farthest point from the eye, its corners, by the
pinhole: the window's middle the axis, its half height the vertical
tangent */
static double farthest_corner(struct render_camera const *view, real z_near)
{
	double tangent = tan(view->vertical_field_of_view * 0.5);
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

static real near_of(struct render_camera const *view)
{
	real_rectangle2d bounds;

	render_camera_build_frustum_bounds(view, &bounds);
	return render_near_clip_distance(view, &bounds);
}

/* the game's own distance, the same float */
static void check_kept(struct render_camera view, char const *what)
{
	real z_near = near_of(&view);

	CHECK(z_near == GAME_NEAR, "%s: %.9f, not the game's", what, z_near);
	CHECK(farthest_corner(&view, z_near) <= REACH, "%s: its corner %.6f from the eye", what,
		farthest_corner(&view, z_near));
}

/* nearer: the farthest corner at the reach */
static void check_reaches(struct render_camera view, char const *what)
{
	real z_near = near_of(&view);
	double corner = farthest_corner(&view, z_near);

	CHECK(z_near < GAME_NEAR && z_near > 0.0f, "%s: %.9f", what, z_near);
	CHECK(fabs(corner - REACH) < 1e-5, "%s: the farthest corner %.7f from the eye, not %.3f", what, corner, REACH);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("stock")
	{
		/* the stock view and the wide ones whose corners are within the
		reach: 4:3, 16:9, 21:9, 32:9 */
		check_kept(camera(0.0, 640, 480, whole(640, 480)), "stock 4:3");
		check_kept(camera(0.0, 1920, 1080, whole(1920, 1080)), "stock 16:9");
		check_kept(camera(0.0, 2560, 1080, whole(2560, 1080)), "stock 21:9");
		check_kept(camera(0.0, 3840, 1080, whole(3840, 1080)), "stock 32:9");
		check_kept(camera(90.0, 1920, 1080, whole(1920, 1080)), "90");
		check_kept(camera(120.0, 1920, 1080, whole(1920, 1080)), "120");
		check_kept(camera(130.0, 1920, 1080, whole(1920, 1080)), "130");
	}
	CASE("wide")
	{
		/* the game's plane would cut a wall beside the player: its corner
		farther than the reach */
		struct render_camera view = camera(160.0, 1920, 1080, whole(1920, 1080));

		CHECK(farthest_corner(&view, GAME_NEAR) > REACH, "160: the game's own corner %.4f",
			farthest_corner(&view, GAME_NEAR));
		check_reaches(camera(145.0, 1920, 1080, whole(1920, 1080)), "145");
		check_reaches(camera(150.0, 1920, 1080, whole(1920, 1080)), "150");
		check_reaches(view, "160");
		check_reaches(camera(160.0, 1280, 720, whole(1280, 720)), "160 at 1280x720");
		check_reaches(camera(120.0, 3840, 1080, whole(3840, 1080)), "120 at 32:9");
		check_reaches(camera(160.0, 1440, 1080, whole(1440, 1080)), "160 at 4:3");
	}
	CASE("tall")
	{
		/* a portrait screen: its top and bottom farther out than its sides */
		check_reaches(camera(160.0, 1080, 1920, whole(1080, 1920)), "160 portrait");
	}
	CASE("split_screen")
	{
		/* a view off its window's middle (split screen's): its corners on
		the far side farther out than the near side's */
		rectangle2d top = { 0, 0, 1920, 540 };
		rectangle2d left = { 0, 0, 960, 1080 };
		struct render_camera view = camera(150.0, 1920, 1080, top);

		view.window_bounds.y1 = 600;
		check_reaches(view, "150, the top half, off its middle");
		view = camera(160.0, 1920, 1080, left);
		view.window_bounds.x1 = 1100;
		check_reaches(view, "160, the left half, off its middle");
		view = camera(0.0, 1920, 1080, top);
		view.window_bounds.y1 = 600;
		check_kept(view, "stock, the top half");
	}
	CASE("nearer_kept")
	{
		/* the game's (a script's) distance nearer than the reach's: kept;
		a mirror's 0 too */
		struct render_camera view = camera(160.0, 1920, 1080, whole(1920, 1080));

		view.z_near = 0.001f;
		CHECK(near_of(&view) == 0.001f, "a nearer one: %.9f", near_of(&view));
		view.z_near = 0.0f;
		CHECK(near_of(&view) == 0.0f, "a mirror's: %.9f", near_of(&view));
	}
	return 0;
}
