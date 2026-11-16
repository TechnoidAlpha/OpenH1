/*
ZOOM_FOV.C

display.scale_scoped_fov (port/linux/include/zoom_fov.h's
zoom_fov_tangent): a scope's zoom at the player's field of view. The game's
70 degree camera (4:3 across, its tangent times 0.85 as drawn), the sniper
rifle's 2x and 8x (the angle over the magnification), the player's 110
across 16:9. Off (the Xbox's): the player's view unzoomed, each zoom level
exactly the game's own. On: every view the game's times the player's
tangent over the stock one's, so each zoom magnifies the player's view as
much as the Xbox's magnifies the stock view, measured as tangents (the
middle of the screen), the same across any shape of screen. The stock view
is the game's in both.
*/

#include "harness.h"

#include <math.h>

#include "under_test.inc"

#define PI 3.14159265358979323846

/* a unit camera's angle (4:3 across, degrees) as the half tangent up and
down it is drawn at (main.c: 0.75 * render_camera_get_adjusted_field_of_view_tangent) */
static float game_tangent(double degrees)
{
	return (float)(0.75 * tan(degrees * PI / 360.0) * 0.85);
}

/* display.fov's angle (16:9 across, degrees) as that tangent (view_fov.c's fov_tangent) */
static float player_tangent(double degrees)
{
	return (float)(tan(degrees * PI / 360.0) * (9.0 / 16.0));
}

static int close_to(double a, double b)
{
	return fabs(a - b) <= 2e-6 * fabs(b);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	float const unzoomed = game_tangent(70.0), scoped = game_tangent(35.0);
	float const levels[] = { game_tangent(70.0), game_tangent(35.0), game_tangent(70.0 / 8.0) };
	float const requested = player_tangent(110.0);
	int level;

	CASE("zoom")
	{
		for (level = 0; level < 3; level++)
		{
			float const native = levels[level];
			float const off = zoom_fov_tangent(native, unzoomed, requested, scoped, FALSE);
			float const on = zoom_fov_tangent(native, unzoomed, requested, scoped, TRUE);
			double const aspects[] = { 4.0 / 3.0, 16.0 / 9.0, 64.0 / 27.0 };
			int aspect;

			/* the stock view: the game's own, either way */
			CHECK(zoom_fov_tangent(native, unzoomed, unzoomed, scoped, FALSE) == native, "stock off, level %d", level);
			CHECK(zoom_fov_tangent(native, unzoomed, unzoomed, scoped, TRUE) == native, "stock on, level %d", level);
			/* off: the player's view unzoomed, the game's zoom levels exactly */
			CHECK(off == (level ? native : requested), "off, level %d: %f", level, off);
			/* on: the Xbox's magnification of the player's view, as
			tangents, at every shape of screen (the angle up and down
			kept: across is the tangent times the aspect) */
			for (aspect = 0; aspect < 3; aspect++)
			{
				double const xbox = unzoomed * aspects[aspect] / (native * aspects[aspect]);
				double const scaled = requested * aspects[aspect] / (on * aspects[aspect]);

				CHECK(close_to(scaled, xbox), "on, level %d, aspect %f: %f, the Xbox's %f", level, aspects[aspect],
					scaled, xbox);
			}
			/* (and wider than the Xbox's zoom, as the player's view is) */
			CHECK(on > native, "on, level %d: %f, not wider than %f", level, on, native);
		}
		/* off, along the zoom's transition: between the player's and the zoom's */
		{
			float const halfway = 0.5f * (unzoomed + scoped);
			float const off = zoom_fov_tangent(halfway, unzoomed, requested, scoped, FALSE);

			CHECK(off > halfway && off < requested, "off, halfway: %f", off);
		}
	}
	return 0;
}
