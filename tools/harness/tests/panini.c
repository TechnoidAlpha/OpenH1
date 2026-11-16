/*
PANINI.C

display.panini's projection (port/linux/include/panini_projection.h) and the
game's use of it (port/linux/game/panini.c): the projection's forward and
inverse round trips, its fit to the screen, and that what the HUD places
over a point is where the pass draws that point.
*/

#include <math.h>
#include "harness.h"

typedef struct { short x0, y0, x1, y1; } rectangle2d;
typedef struct { real x, y; } real_point2d;
struct render_camera { boolean mirrored; rectangle2d viewport_bounds; };
struct render_frustum { boolean projection_valid; real projection_matrix[4][4]; };
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* config.toml's display.panini (the strength, 0 off) and _vertical */
static int setting_on;
static double setting_strength = 1.0, setting_vertical;
static unsigned long setting_changes;

double config_real(const char *name)
{
	return !strcmp(name, "display.panini") ? (setting_on ? setting_strength : 0.0) :
		!strcmp(name, "display.panini_vertical") ? setting_vertical : 0.0;
}

/* a local player's own (their profile's): player 2's strength set apart
(players_apart), config.toml's for the others */
static int players_apart;
static double player2_strength;

double config_player_real(int local_player, const char *name)
{
	if (players_apart && local_player == 1 && !strcmp(name, "display.panini"))
		return player2_strength;
	return config_real(name);
}

unsigned long config_changes(void)
{
	return setting_changes;
}

/* the pass, as the front end asks for it */
static int passes;
static short pass_bounds[4];
static float pass_projection[8];

void halo_screen_panini(short x0, short y0, short x1, short y1, const float projection[8])
{
	passes++;
	pass_bounds[0] = x0;
	pass_bounds[1] = y0;
	pass_bounds[2] = x1;
	pass_bounds[3] = y1;
	memcpy(pass_projection, projection, sizeof(pass_projection));
}

/* (the map's preparation's walk: not walking) */
static boolean render_enumerating;

#include "projection.inc"
#include "under_test.inc"

static void settings(int on, double strength, double vertical)
{
	setting_on = on;
	setting_strength = strength;
	setting_vertical = vertical;
	setting_changes++;
}

static int near(float a, float b, float tolerance)
{
	return fabsf(a - b) <= tolerance * (1.0f + fabsf(b));
}

/* a field of view in degrees across 16:9 at a shape (width over height): the
view's half tangents */
static void view_tangents(double degrees, double shape, float *tx, float *ty)
{
	*ty = (float)(tan(degrees * 3.14159265358979 / 360.0) * 9.0 / 16.0);
	*tx = (float)(*ty * shape);
}

static const float strengths[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
static const float compressions[] = { 0.0f, 0.3f, 0.5f, 1.0f };
static const double fields_of_view[] = { 60.0, 90.0, 120.0, 150.0 };
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* every screen point's direction drawn back where it was, and every
direction's place on the screen back to it */
static void round_trip(void)
{
	int a, b, c, i, j;

	for (a = 0; a < COUNT(strengths); a++)
	for (b = 0; b < COUNT(compressions); b++)
	for (c = 0; c < COUNT(fields_of_view); c++)
	{
		struct panini_projection p;
		float tx, ty;

		view_tangents(fields_of_view[c], 21.0 / 9.0, &tx, &ty);
		CHECK(panini_projection_fit(&p, strengths[a], compressions[b], tx, ty), "fit");
		for (i = -10; i <= 10; i++)
		for (j = -10; j <= 10; j++)
		{
			float sx = tx * (float)i / 10.0f, sy = ty * (float)j / 10.0f;
			float x, y, back_x, back_y;

			panini_projection_inverse(&p, sx, sy, &x, &y);
			panini_projection_forward(&p, x, y, &back_x, &back_y);
			CHECK(near(back_x, sx, 2.0e-5f) && near(back_y, sy, 2.0e-5f),
				"screen (%g, %g) d %g s %g fov %g: back at (%g, %g)", sx, sy, p.d, p.s, fields_of_view[c],
				back_x, back_y);
			/* (and a direction inside the rectilinear view) */
			x = tx * (float)i / 10.0f;
			y = ty * (float)j / 10.0f;
			panini_projection_forward(&p, x, y, &sx, &sy);
			panini_projection_inverse(&p, sx, sy, &back_x, &back_y);
			CHECK(near(back_x, x, 2.0e-5f) && near(back_y, y, 2.0e-5f),
				"view (%g, %g) d %g s %g fov %g: back at (%g, %g)", x, y, p.d, p.s, fields_of_view[c], back_x, back_y);
		}
	}
}

/* d 0 and s 0: the rectilinear view itself */
static void identity(void)
{
	struct panini_projection p;
	int i, j;

	panini_projection_fit(&p, 0.0f, 0.0f, 3.0f, 1.5f);
	CHECK(p.scale == 1.0f, "scale %g", p.scale);
	for (i = -6; i <= 6; i++)
	for (j = -6; j <= 6; j++)
	{
		float x = 0.5f * (float)i, y = 0.25f * (float)j, sx, sy;

		panini_projection_forward(&p, x, y, &sx, &sy);
		CHECK(near(sx, x, 1.0e-6f) && near(sy, y, 1.0e-6f), "(%g, %g) at (%g, %g)", x, y, sx, sy);
	}
	/* (out of range: limited to 0 to 1; no view: none) */
	panini_projection_fit(&p, -1.0f, 2.0f, 1.0f, 1.0f);
	CHECK(p.d == 0.0f && p.s == 1.0f, "limits");
	CHECK(!panini_projection_fit(&p, 1.0f, 0.0f, 0.0f, 1.0f) && p.scale == 1.0f && p.d == 0.0f, "no view");
}

/* the view's sides at the screen's sides, vertical lines straight, the
middle magnified by the scale; 150 and 120 degrees at full strength */
static void edges(void)
{
	int a, b, c, j;

	for (a = 0; a < COUNT(strengths); a++)
	for (b = 0; b < COUNT(compressions); b++)
	for (c = 0; c < COUNT(fields_of_view); c++)
	{
		struct panini_projection p;
		float tx, ty, sx, sy, first;

		view_tangents(fields_of_view[c], 16.0 / 9.0, &tx, &ty);
		panini_projection_fit(&p, strengths[a], compressions[b], tx, ty);
		CHECK(p.scale >= 1.0f, "scale %g", p.scale);
		panini_projection_forward(&p, tx, 0.0f, &sx, &sy);
		CHECK(near(sx, tx, 1.0e-5f) && sy == 0.0f, "the side at %g, not %g", sx, tx);
		panini_projection_forward(&p, 1.0e-3f, 0.0f, &sx, &sy);
		CHECK(near(sx / 1.0e-3f, p.scale, 1.0e-4f), "the middle by %g, not %g", sx / 1.0e-3f, p.scale);
		panini_projection_forward(&p, 0.0f, 1.0e-3f, &sx, &sy);
		CHECK(near(sy / 1.0e-3f, p.scale, 1.0e-4f), "the middle up by %g, not %g", sy / 1.0e-3f, p.scale);
		panini_projection_forward(&p, 0.7f * tx, -ty, &first, &sy);
		for (j = -10; j <= 10; j++)
		{
			panini_projection_forward(&p, 0.7f * tx, ty * (float)j / 10.0f, &sx, &sy);
			CHECK(sx == first, "a vertical line bent: %g, %g", sx, first);
		}
	}
	CHECK(near(panini_projection_scale(1.0f, (float)tan(75.0 * 3.14159265358979 / 180.0)), 2.4319f, 1.0e-4f),
		"150 degrees: %g", panini_projection_scale(1.0f, (float)tan(75.0 * 3.14159265358979 / 180.0)));
	CHECK(near(panini_projection_scale(1.0f, (float)tan(60.0 * 3.14159265358979 / 180.0)), 1.5f, 1.0e-5f),
		"120 degrees: %g", panini_projection_scale(1.0f, (float)tan(60.0 * 3.14159265358979 / 180.0)));
}

/* the rectilinear view drawn covers the whole screen through it: each point
of the screen's border shows a direction inside the view, the corners
exactly at s 0 */
static void coverage(void)
{
	int a, b, c, i;

	for (a = 0; a < COUNT(strengths); a++)
	for (b = 0; b < COUNT(compressions); b++)
	for (c = 0; c < COUNT(fields_of_view); c++)
	{
		struct panini_projection p;
		float tx, ty, x, y;

		view_tangents(fields_of_view[c], 16.0 / 9.0, &tx, &ty);
		panini_projection_fit(&p, strengths[a], compressions[b], tx, ty);
		for (i = -20; i <= 20; i++)
		{
			panini_projection_inverse(&p, tx * (float)i / 20.0f, ty, &x, &y);
			CHECK(fabsf(x) <= tx * 1.00001f && y <= ty * 1.00001f && y > 0.0f,
				"the top at %d/20 shows (%g, %g), outside (%g, %g)", i, x, y, tx, ty);
			panini_projection_inverse(&p, tx, ty * (float)i / 20.0f, &x, &y);
			CHECK(near(x, tx, 1.0e-5f) && fabsf(y) <= ty * 1.00001f, "the side at %d/20 shows (%g, %g)", i, x, y);
		}
		panini_projection_inverse(&p, tx, ty, &x, &y);
		if (p.s == 0.0f)
			CHECK(near(x, tx, 1.0e-5f) && near(y, ty, 1.0e-5f), "the corner shows (%g, %g)", x, y);
	}
}

/* at full strength and no compression, a thing on the horizon keeps its
shape anywhere across: drawn as wide as tall (the rectilinear view stretches
it across); s 0.5 keeps lines across straight */
static void shape(void)
{
	struct panini_projection p;
	float tx, ty, phi;

	view_tangents(150.0, 16.0 / 9.0, &tx, &ty);
	panini_projection_fit(&p, 1.0f, 0.0f, tx, ty);
	for (phi = 0.1f; phi < 1.3f; phi += 0.1f)
	{
		float h = 1.0e-3f, x0, x1, y0, y1, unused, across, up;

		panini_projection_forward(&p, tanf(phi - h), 0.0f, &x0, &unused);
		panini_projection_forward(&p, tanf(phi + h), 0.0f, &x1, &unused);
		/* (up: an angle h above the horizon at phi, on its cylinder) */
		panini_projection_forward(&p, tanf(phi), 0.0f, &unused, &y0);
		panini_projection_forward(&p, tanf(phi), tanf(h) / cosf(phi), &unused, &y1);
		across = (x1 - x0) / (2.0f * h);
		up = (y1 - y0) / h;
		CHECK(near(across, up, 2.0e-3f), "at %g: %g across, %g up", phi, across, up);
	}
	panini_projection_fit(&p, 1.0f, 0.5f, tx, ty);
	for (phi = -1.3f; phi < 1.3f; phi += 0.1f)
	{
		float sx, sy, middle;

		panini_projection_forward(&p, 0.0f, 0.4f, &sx, &middle);
		panini_projection_forward(&p, tanf(phi), 0.4f, &sx, &sy);
		CHECK(near(sy, middle, 1.0e-4f), "a line across at %g: %g, not %g", phi, sy, middle);
	}
}

/* the game's window: a frustum of render_cameras.c's terms (x and y scales,
x and y offsets) over a viewport */
static struct render_camera camera;
static struct render_frustum frustum;

static void window_set(float tx, float ty, float x_offset, float y_offset, short x0, short y0, short x1, short y1)
{
	memset(&camera, 0, sizeof(camera));
	memset(&frustum, 0, sizeof(frustum));
	camera.viewport_bounds.x0 = x0;
	camera.viewport_bounds.y0 = y0;
	camera.viewport_bounds.x1 = x1;
	camera.viewport_bounds.y1 = y1;
	frustum.projection_valid = TRUE;
	frustum.projection_matrix[0][0] = 1.0f / tx;
	frustum.projection_matrix[1][1] = 1.0f / ty;
	frustum.projection_matrix[2][0] = x_offset;
	frustum.projection_matrix[2][1] = y_offset;
}

/* where the rectilinear view drew a point (render_camera_view_to_screen's
own arithmetic) */
static void rectilinear_screen(const real_point3d *point, float *x, float *y)
{
	float ndc_x = (frustum.projection_matrix[0][0] * point->x + frustum.projection_matrix[2][0] * point->z) / -point->z;
	float ndc_y = -(frustum.projection_matrix[1][1] * point->y + frustum.projection_matrix[2][1] * point->z) / -point->z;

	*x = (camera.viewport_bounds.x1 - camera.viewport_bounds.x0) * ((ndc_x + 1.0f) * 0.5f) + camera.viewport_bounds.x0;
	*y = ((ndc_y + 1.0f) * 0.5f) * (camera.viewport_bounds.y1 - camera.viewport_bounds.y0) + camera.viewport_bounds.y0;
}

/* the pass (tools/vulkan_post_shaders.py's PANINI_SOURCE): the rectilinear
view's place a place of the screen is drawn from, by the projection the
front end gave it */
static void pass_source(float x, float y, float *source_x, float *source_y)
{
	struct panini_projection p;
	float place_x = (x - pass_bounds[0]) / (float)(pass_bounds[2] - pass_bounds[0]);
	float place_y = (y - pass_bounds[1]) / (float)(pass_bounds[3] - pass_bounds[1]);
	float seen_x, seen_y;

	p.d = pass_projection[0];
	p.s = pass_projection[1];
	p.scale = pass_projection[2];
	panini_projection_inverse(&p, pass_projection[4] + (pass_projection[5] - pass_projection[4]) * place_x,
		pass_projection[6] + (pass_projection[7] - pass_projection[6]) * place_y, &seen_x, &seen_y);
	*source_x = pass_bounds[0] + (pass_bounds[2] - pass_bounds[0]) * (seen_x - pass_projection[4]) /
		(pass_projection[5] - pass_projection[4]);
	*source_y = pass_bounds[1] + (pass_bounds[3] - pass_bounds[1]) * (seen_y - pass_projection[6]) /
		(pass_projection[7] - pass_projection[6]);
}

/* what the HUD puts over a point (teammates, names, waypoints) is where the
pass draws it: the pass draws that place from where the rectilinear view
drew the point. A full screen at 150 degrees, and split screen's lower
window, its axis off its middle */
static void hud_matches_pass(void)
{
	int view, i, j, b;

	for (view = 0; view < 2; view++)
	for (b = 0; b < COUNT(compressions); b++)
	{
		float tx, ty;
		int visible_count = 0;

		view_tangents(view ? 120.0 : 150.0, view ? 852.0 / 236.0 : 852.0 / 480.0, &tx, &ty);
		if (view)
			window_set(tx, ty * 0.98f, 0.01f, -0.02f, 0, 244, 852, 480);
		else
			window_set(tx, ty, 0.0f, 0.0f, 0, 0, 852, 480);
		settings(TRUE, 1.0, compressions[b]);
		passes = 0;
		panini_window_begin(0, 0, &camera, &frustum);
		panini_window_draw();
		CHECK(passes == 1, "no pass");
		CHECK(pass_bounds[0] == camera.viewport_bounds.x0 && pass_bounds[3] == camera.viewport_bounds.y1, "bounds");
		/* (the pass draws every place of the window from the view drawn:
		its border from inside it) */
		for (i = 0; i <= 20; i++)
		{
			float along_x = pass_bounds[0] + (pass_bounds[2] - pass_bounds[0]) * (float)i / 20.0f;
			float along_y = pass_bounds[1] + (pass_bounds[3] - pass_bounds[1]) * (float)i / 20.0f;
			float edges[4][2] = { { along_x, pass_bounds[1] }, { along_x, pass_bounds[3] },
				{ pass_bounds[0], along_y }, { pass_bounds[2], along_y } };

			for (j = 0; j < 4; j++)
			{
				float source_x, source_y;

				pass_source(edges[j][0], edges[j][1], &source_x, &source_y);
				CHECK(source_x >= pass_bounds[0] - 0.01f && source_x <= pass_bounds[2] + 0.01f &&
					source_y >= pass_bounds[1] - 0.01f && source_y <= pass_bounds[3] + 0.01f,
					"view %d s %g: (%g, %g) drawn from (%g, %g), outside the view drawn", view, compressions[b],
					edges[j][0], edges[j][1], source_x, source_y);
			}
		}
		for (i = -12; i <= 12; i++)
		for (j = -12; j <= 12; j++)
		{
			real_point3d point;
			real_point2d screen;
			boolean visible;
			float source_x, source_y, drawn_x, drawn_y;

			point.x = tx * 1.2f * (float)i / 12.0f * 5.0f;
			point.y = ty * 1.2f * (float)j / 12.0f * 5.0f;
			point.z = -5.0f;
			CHECK(panini_view_to_screen(&camera, &frustum, &point, &screen, &visible), "not answered");
			if (!visible)
				continue;
			visible_count++;
			CHECK(screen.x >= camera.viewport_bounds.x0 && screen.x <= camera.viewport_bounds.x1 &&
				screen.y >= camera.viewport_bounds.y0 && screen.y <= camera.viewport_bounds.y1, "off the window");
			pass_source(screen.x, screen.y, &source_x, &source_y);
			rectilinear_screen(&point, &drawn_x, &drawn_y);
			CHECK(fabsf(source_x - drawn_x) < 0.01f && fabsf(source_y - drawn_y) < 0.01f,
				"view %d s %g (%d, %d): marked at (%g, %g), which the pass draws from (%g, %g), not (%g, %g)", view,
				compressions[b], i, j, screen.x, screen.y, source_x, source_y, drawn_x, drawn_y);
		}
		CHECK(visible_count > 80, "only %d seen", visible_count);
		/* (behind the camera: not on the screen) */
		{
			real_point3d point = { 0.0f, 0.0f, 1.0f };
			real_point2d screen;
			boolean visible = TRUE;

			CHECK(panini_view_to_screen(&camera, &frustum, &point, &screen, &visible) && !visible, "behind");
		}
		panini_window_end();
	}
}

/* the middle's magnification, for the reticle and the weapon, only while a
window is drawn through it; the middle itself does not move */
static void center(void)
{
	float tx, ty;
	real_point3d point = { 0.0f, 0.0f, -3.0f };
	real_point2d screen;
	boolean visible;

	view_tangents(120.0, 16.0 / 9.0, &tx, &ty);
	window_set(tx, ty, 0.0f, 0.0f, 0, 0, 852, 480);
	settings(TRUE, 1.0, 0.0);
	CHECK(panini_center_scale() == 1.0f, "before the window");
	panini_window_begin(0, 0, &camera, &frustum);
	CHECK(near(panini_center_scale(), 1.5f, 1.0e-4f), "120 degrees: %g", panini_center_scale());
	CHECK(panini_view_to_screen(&camera, &frustum, &point, &screen, &visible) && visible, "the middle");
	CHECK(near(screen.x, 426.0f, 1.0e-5f) && near(screen.y, 240.0f, 1.0e-5f), "the middle at (%g, %g)", screen.x,
		screen.y);
	panini_window_end();
	CHECK(panini_center_scale() == 1.0f, "after the window");
	CHECK(panini_center_scale_widened(2.0f) == 1.0f, "widened, after the window");
	/* (#594) a zoomed view's window widened back to the unzoomed view has the
	unzoomed view's magnification, which the reticle keeps through the zoom;
	at 1 the window's own */
	{
		float zx, zy;

		view_tangents(120.0, 16.0 / 9.0, &tx, &ty);
		zx = tx * 0.25f;
		zy = ty * 0.25f;
		window_set(zx, zy, 0.0f, 0.0f, 0, 0, 852, 480);
		panini_window_begin(0, 0, &camera, &frustum);
		CHECK(panini_center_scale() < 1.1f, "zoomed: %g", panini_center_scale());
		CHECK(panini_center_scale_widened(1.0f) == panini_center_scale(), "widened by 1: %g",
			panini_center_scale_widened(1.0f));
		CHECK(near(panini_center_scale_widened(4.0f), 1.5f, 1.0e-3f), "widened to 120 degrees: %g",
			panini_center_scale_widened(4.0f));
		panini_window_end();
	}
}

/* off (the default), no strength, a mirror, a target not the screen, another
camera's projection: nothing drawn, every point projected as ever */
static void off(void)
{
	float tx, ty;
	real_point3d point = { 1.0f, 0.5f, -3.0f };
	real_point2d screen;
	boolean visible;

	view_tangents(120.0, 16.0 / 9.0, &tx, &ty);
	window_set(tx, ty, 0.0f, 0.0f, 0, 0, 852, 480);
	settings(FALSE, 1.0, 0.0);
	passes = 0;
	panini_window_begin(0, 0, &camera, &frustum);
	panini_window_draw();
	CHECK(!passes && panini_center_scale() == 1.0f, "off, drawn");
	CHECK(!panini_view_to_screen(&camera, &frustum, &point, &screen, &visible), "off, answered");
	settings(TRUE, 0.0, 1.0);
	panini_window_begin(0, 0, &camera, &frustum);
	panini_window_draw();
	CHECK(!passes, "no strength, drawn");
	settings(TRUE, 1.0, 0.0);
	panini_window_begin(1, 1, &camera, &frustum);
	panini_window_draw();
	CHECK(!passes, "another target's");
	panini_window_begin(-1, 0, &camera, &frustum);
	panini_window_draw();
	CHECK(!passes, "no player's");
	camera.mirrored = TRUE;
	panini_window_begin(0, 0, &camera, &frustum);
	panini_window_draw();
	CHECK(!passes, "a mirror's");
	camera.mirrored = FALSE;
	panini_window_begin(0, 0, &camera, &frustum);
	frustum.projection_matrix[0][0] *= 2.0f;
	CHECK(!panini_view_to_screen(&camera, &frustum, &point, &screen, &visible), "another projection, answered");
	panini_window_end();
}

/* split screen's players each their own: player 2's off while player 1's
is on draws only player 1's window through it, and player 2's strength
(not player 1's) widens player 2's fields of view */
static void players(void)
{
	float tx, ty;

	view_tangents(120.0, 16.0 / 9.0, &tx, &ty);
	window_set(tx, ty, 0.0f, 0.0f, 0, 0, 852, 480);
	settings(TRUE, 1.0, 0.0);
	players_apart = 1;
	player2_strength = 0.0;
	setting_changes++;
	passes = 0;
	panini_window_begin(0, 0, &camera, &frustum);
	panini_window_draw();
	CHECK(passes == 1 && panini_center_scale() > 1.0f, "player 1's on: %d", passes);
	panini_window_begin(1, 0, &camera, &frustum);
	panini_window_draw();
	CHECK(passes == 1 && panini_center_scale() == 1.0f, "player 2's off, drawn: %d", passes);
	CHECK(panini_strength(0) == 1.0f && panini_strength(1) == 0.0f, "the strengths: %g, %g", panini_strength(0),
		panini_strength(1));
	player2_strength = 0.5;
	setting_changes++;
	CHECK(panini_strength(1) == 0.5f, "player 2's changed: %g", panini_strength(1));
	panini_window_end();
	players_apart = 0;
}

/* a waypoint's arrow off the screen points where the projection draws it:
at s 0 the rectilinear direction (the projection moves points along their
lines from the middle), at s 1 not */
static void direction(void)
{
	float tx, ty, x, y;
	real_point3d point = { 4.0f, 1.5f, -1.0f };

	view_tangents(120.0, 16.0 / 9.0, &tx, &ty);
	window_set(tx, ty, 0.0f, 0.0f, 0, 0, 852, 480);
	settings(TRUE, 1.0, 0.0);
	panini_window_begin(0, 0, &camera, &frustum);
	x = point.x;
	y = -point.y;
	panini_view_direction(&point, &x, &y);
	CHECK(near(atan2f(y, x), atan2f(-point.y, point.x), 1.0e-5f), "s 0: %g, not %g", atan2f(y, x),
		atan2f(-point.y, point.x));
	settings(TRUE, 1.0, 1.0);
	panini_window_begin(0, 0, &camera, &frustum);
	x = point.x;
	y = -point.y;
	panini_view_direction(&point, &x, &y);
	CHECK(fabsf(atan2f(y, x) - atan2f(-point.y, point.x)) > 0.01f, "s 1: unchanged");
	panini_window_end();
	x = 1.0f;
	y = 2.0f;
	panini_view_direction(&point, &x, &y);
	CHECK(x == 1.0f && y == 2.0f, "no window: changed");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "round_trip"))
		round_trip();
	else if (!strcmp(name, "identity"))
		identity();
	else if (!strcmp(name, "edges"))
		edges();
	else if (!strcmp(name, "coverage"))
		coverage();
	else if (!strcmp(name, "shape"))
		shape();
	else if (!strcmp(name, "hud_matches_pass"))
		hud_matches_pass();
	else if (!strcmp(name, "center"))
		center();
	else if (!strcmp(name, "off"))
		off();
	else if (!strcmp(name, "direction"))
		direction();
	else if (!strcmp(name, "players"))
		players();
	else
	{
		fprintf(stderr, "unknown case %s\n", name);
		return 2;
	}
	return 0;
}
