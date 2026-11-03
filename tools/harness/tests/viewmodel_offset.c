/*
VIEWMODEL_OFFSET.C

display.viewmodel_offset_x, _y and _z (view_fov.c's viewmodel_offset_apply):
the first-person weapon's and hands' node matrices, built at the camera,
moved along the view's right, up and forward by centimeters (a world unit is
ten feet), every node alike, so that on the screen the weapon goes right,
up or back at any field of view; 0 leaves them as the game built them, and
a value past 20 centimeters is held at 20. And the settings' slider, whose
track rounds a negative value to its nearest step (menu_functions.c's
settings_slider_round).
*/

#include "harness.h"
#include <math.h>

#include "types.inc"

struct render_camera
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	real vertical_field_of_view;
};

static double fake_x, fake_y, fake_z;
static unsigned long fake_changes = 1;

static unsigned long config_changes(void) { return fake_changes; }
static double config_real(const char *name)
{
	return !strcmp(name, "display.viewmodel_offset_x") ? fake_x :
		!strcmp(name, "display.viewmodel_offset_y") ? fake_y :
		!strcmp(name, "display.viewmodel_offset_z") ? fake_z : -1.0e9;
}

#include "under_test.inc"

#define NODES 3
#define UNITS(centimeters) ((real)((centimeters) / 304.8))
#define NEAR(a, b) (fabsf((a) - (b)) <= 1e-6f)

static void settings(double x, double y, double z)
{
	fake_x = x;
	fake_y = y;
	fake_z = z;
	fake_changes++;
}

static struct render_camera camera_of(real_vector3d forward, real_vector3d up)
{
	struct render_camera camera;

	memset(&camera, 0, sizeof(camera));
	camera.position.x = 10.f;
	camera.position.y = -4.f;
	camera.position.z = 2.f;
	camera.forward = forward;
	camera.up = up;
	return camera;
}

/* a pose in front of the camera: each node a little ahead, below and to the
right of the eye, as a gun's are */
static void pose(struct render_camera const *camera, struct real_matrix4x3 *nodes)
{
	real_vector3d right;
	short node;

	cross_product3d(&camera->forward, &camera->up, &right);
	memset(nodes, 0, sizeof(*nodes) * NODES);
	for (node = 0; node < NODES; node++)
	{
		real ahead = 0.08f + 0.02f * node, down = -0.03f, aside = 0.02f + 0.01f * node;

		nodes[node].scale = 1.f;
		nodes[node].forward = camera->forward;
		nodes[node].up = camera->up;
		nodes[node].position.x = camera->position.x + camera->forward.i * ahead + camera->up.i * down + right.i * aside;
		nodes[node].position.y = camera->position.y + camera->forward.j * ahead + camera->up.j * down + right.j * aside;
		nodes[node].position.z = camera->position.z + camera->forward.k * ahead + camera->up.k * down + right.k * aside;
	}
}

/* a point where the screen shows it at this vertical field of view
(radians) and a 16:9 window: x right, y up, from -1 to 1 across */
static void screen_of(struct render_camera const *camera, real_point3d const *point, real angle, real *x, real *y)
{
	real_vector3d right, to;
	real depth, half = tanf(angle / 2.f);

	cross_product3d(&camera->forward, &camera->up, &right);
	to.i = point->x - camera->position.x;
	to.j = point->y - camera->position.y;
	to.k = point->z - camera->position.z;
	depth = to.i * camera->forward.i + to.j * camera->forward.j + to.k * camera->forward.k;
	*x = (to.i * right.i + to.j * right.j + to.k * right.k) / (depth * half * 16.f / 9.f);
	*y = (to.i * camera->up.i + to.j * camera->up.j + to.k * camera->up.k) / (depth * half);
}

/* each node moved by right, up and forward world units along the camera */
static void check_moved(struct render_camera const *camera, struct real_matrix4x3 const *before,
	struct real_matrix4x3 const *after, real right_units, real up_units, real forward_units)
{
	real_vector3d right;
	short node;

	cross_product3d(&camera->forward, &camera->up, &right);
	for (node = 0; node < NODES; node++)
	{
		real_point3d expected;

		expected.x = before[node].position.x + right.i * right_units + camera->up.i * up_units + camera->forward.i * forward_units;
		expected.y = before[node].position.y + right.j * right_units + camera->up.j * up_units + camera->forward.j * forward_units;
		expected.z = before[node].position.z + right.k * right_units + camera->up.k * up_units + camera->forward.k * forward_units;
		CHECK(NEAR(after[node].position.x, expected.x) && NEAR(after[node].position.y, expected.y) &&
			NEAR(after[node].position.z, expected.z), "node %d at %f %f %f, not %f %f %f", node,
			after[node].position.x, after[node].position.y, after[node].position.z, expected.x, expected.y,
			expected.z);
		CHECK(!memcmp(&after[node].forward, &before[node].forward, sizeof(real_vector3d) * 3) &&
			after[node].scale == before[node].scale, "node %d turned or scaled", node);
	}
}

static real_vector3d vector(real i, real j, real k)
{
	real_vector3d v;

	v.i = i;
	v.j = j;
	v.k = k;
	return v;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct render_camera camera = camera_of(vector(1.f, 0.f, 0.f), vector(0.f, 0.f, 1.f));
	struct real_matrix4x3 before[NODES], after[NODES];
	real_vector3d offset;

	pose(&camera, before);
	memcpy(after, before, sizeof(after));

	CASE("stock")
	{
		/* 0, 0, 0: the game's pose, untouched */
		settings(0.0, 0.0, 0.0);
		CHECK(!viewmodel_offset(5, &offset), "an offset of none");
		viewmodel_offset_apply(5, after, NODES, &camera);
		CHECK(!memcmp(after, before, sizeof(after)), "the pose moved");
	}
	CASE("right")
	{
		/* along x, z up, the view's right is -y */
		settings(10.0, 0.0, 0.0);
		CHECK(viewmodel_offset(5, &offset), "no offset");
		viewmodel_offset_apply(5, after, NODES, &camera);
		check_moved(&camera, before, after, UNITS(10.0), 0.f, 0.f);
		CHECK(NEAR(after[0].position.y - before[0].position.y, -UNITS(10.0)), "moved %f along y",
			after[0].position.y - before[0].position.y);
	}
	CASE("up_and_forward")
	{
		settings(0.0, -4.5, 6.0);
		viewmodel_offset_apply(5, after, NODES, &camera);
		check_moved(&camera, before, after, 0.f, -UNITS(4.5), UNITS(6.0));
		CHECK(NEAR(after[1].position.z - before[1].position.z, -UNITS(4.5)), "moved %f along z",
			after[1].position.z - before[1].position.z);
	}
	CASE("turned_view")
	{
		/* a view turned and pitched: the move is the view's, not the world's */
		real c = cosf(0.7f), s = sinf(0.7f), p = cosf(-0.4f), q = sinf(-0.4f);

		camera = camera_of(vector(c * p, s * p, q), vector(-c * q, -s * q, p));
		pose(&camera, before);
		memcpy(after, before, sizeof(after));
		settings(3.0, -5.0, 2.0);
		viewmodel_offset_apply(5, after, NODES, &camera);
		check_moved(&camera, before, after, UNITS(3.0), -UNITS(5.0), UNITS(2.0));
	}
	CASE("on_the_screen")
	{
		/* lowered and pushed right: on the screen every node goes right and
		down, at the stock view's angle, a wide weapon field of view and a
		narrow one */
		static real const angles[] = { 0.9f, 1.2f, 1.9f, 0.4f };
		short a, node;

		settings(4.0, -3.0, 0.0);
		viewmodel_offset_apply(5, after, NODES, &camera);
		for (a = 0; a < (short)(sizeof(angles) / sizeof(angles[0])); a++)
		{
			for (node = 0; node < NODES; node++)
			{
				real x0, y0, x1, y1;

				screen_of(&camera, &before[node].position, angles[a], &x0, &y0);
				screen_of(&camera, &after[node].position, angles[a], &x1, &y1);
				CHECK(x1 > x0 && y1 < y0, "angle %f node %d: %f %f to %f %f", angles[a], node, x0, y0, x1, y1);
			}
		}
		/* and pushed away, it is nearer the screen's middle (smaller) */
		memcpy(after, before, sizeof(after));
		settings(0.0, 0.0, 5.0);
		viewmodel_offset_apply(5, after, NODES, &camera);
		for (node = 0; node < NODES; node++)
		{
			real x0, y0, x1, y1;

			screen_of(&camera, &before[node].position, angles[0], &x0, &y0);
			screen_of(&camera, &after[node].position, angles[0], &x1, &y1);
			CHECK(fabsf(x1) < fabsf(x0) && fabsf(y1) < fabsf(y0), "node %d: %f %f to %f %f", node, x0, y0, x1, y1);
		}
	}
	CASE("held_at_the_range")
	{
		settings(50.0, -80.0, 20.0);
		viewmodel_offset(5, &offset);
		CHECK(NEAR(offset.i, UNITS(20.0)) && NEAR(offset.j, -UNITS(20.0)) && NEAR(offset.k, UNITS(20.0)),
			"%f %f %f", offset.i, offset.j, offset.k);
	}
	CASE("changed")
	{
		/* a change of the settings is read at once */
		settings(2.0, 0.0, 0.0);
		viewmodel_offset(5, &offset);
		settings(-7.0, 0.0, 0.0);
		viewmodel_offset(5, &offset);
		CHECK(NEAR(offset.i, -UNITS(7.0)), "%f", offset.i);
	}
	CASE("slider_steps")
	{
		/* a track's step either side of 0: -1.2 is nearest -1.0, -1.3 -1.5,
		and left from -1.0 goes to -1.5 */
		CHECK(settings_slider_round(-1.2, 0.5) == -1.0, "%f", settings_slider_round(-1.2, 0.5));
		CHECK(settings_slider_round(-1.3, 0.5) == -1.5, "%f", settings_slider_round(-1.3, 0.5));
		CHECK(settings_slider_round(-1.0, 0.5) - 0.5 == -1.5, "%f", settings_slider_round(-1.0, 0.5) - 0.5);
		CHECK(settings_slider_round(1.3, 0.5) == 1.5 && settings_slider_round(0.2, 0.5) == 0.0, "the positives");
		CHECK(settings_slider_round(117.0, 5.0) == 115.0, "%f", settings_slider_round(117.0, 5.0));
	}
	return 0;
}
