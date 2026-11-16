/*
VIEWMODEL_OFFSET.C

display.viewmodel_offset_x, _y and _z (view_fov.c's viewmodel_offset_apply):
the first-person weapon's and hands' node matrices, built at the camera,
moved along the view's right, up and forward by centimeters (a world unit is
ten feet), every node alike, so that on the screen the weapon goes right,
up or back at any field of view; 0 leaves them as the game built them, and
a value past 20 centimeters is held at 20. With display.viewmodel_fov the
moves right and up are scaled by the tangents' ratio of the weapon's drawn
angle to its stock one, so that they go as far across the screen as at the
stock angle; the move away is not, as it shrinks the weapon by the same part
at any angle. And the settings' slider, whose track rounds a negative value
to its nearest step (menu_functions.c's settings_slider_round).
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

static double fake_x, fake_y, fake_z, fake_pitch, fake_yaw, fake_roll;
static unsigned long fake_changes = 1;
/* display.viewmodel_fov's angle and the stock one (view_fov.c's
viewmodel_angles), for local player 1; 0: the stock angle */
static real fake_drawn, fake_stock;

static boolean viewmodel_angles(short local_player_index, real *drawn, real *stock)
{
	if (local_player_index != 1 || !fake_drawn)
		return FALSE;
	*drawn = fake_drawn;
	*stock = fake_stock;
	return TRUE;
}

static unsigned long config_changes(void) { return fake_changes; }
static double config_real(const char *name)
{
	return !strcmp(name, "display.viewmodel_offset_x") ? fake_x :
		!strcmp(name, "display.viewmodel_offset_y") ? fake_y :
		!strcmp(name, "display.viewmodel_offset_z") ? fake_z :
		!strcmp(name, "display.viewmodel_pitch") ? fake_pitch :
		!strcmp(name, "display.viewmodel_yaw") ? fake_yaw :
		!strcmp(name, "display.viewmodel_roll") ? fake_roll : -1.0e9;
}
/* each local player's own (their profile's): player 2's move right set
apart (fake_player2_x: none while 0), config.toml's for the rest */
static double fake_player2_x;
static double config_player_real(int local_player, const char *name)
{
	if (local_player == 1 && fake_player2_x != 0.0 && !strcmp(name, "display.viewmodel_offset_x"))
		return fake_player2_x;
	return config_real(name);
}

/* the weapons: object 5 a pistol, 6 a modded map's rifle, 7 none; their
own offsets by path (port_config.c's config_viewmodel_weapon), and how many
times they were looked up */
#define CONFIG_WEAPON_VALUES 6
static double fake_pistol[CONFIG_WEAPON_VALUES], fake_rifle[CONFIG_WEAPON_VALUES];
static int fake_lookups;

static long viewmodel_weapon_definition(long weapon_index)
{
	return weapon_index == 5 ? (long)0xE1000005 : weapon_index == 6 ? (long)0xE2000006 : NONE;
}

static char const *viewmodel_weapon_path(long definition_index)
{
	return definition_index == (long)0xE1000005 ? "weapons\\pistol\\pistol" :
		definition_index == (long)0xE2000006 ? "mod\\weapons\\rifle\\rifle" : NULL;
}

/* (player 2's profile's pistol: fake_player2_pistol, while set) */
static double fake_player2_pistol[CONFIG_WEAPON_VALUES];
static int fake_player2_pistol_set;

static int config_player_viewmodel_weapon(int local_player, const char *path, double values[CONFIG_WEAPON_VALUES])
{
	fake_lookups++;
	if (local_player == 1 && fake_player2_pistol_set && path && !strcmp(path, "weapons\\pistol\\pistol"))
		memcpy(values, fake_player2_pistol, sizeof(fake_player2_pistol));
	else if (path && !strcmp(path, "weapons\\pistol\\pistol"))
		memcpy(values, fake_pistol, sizeof(fake_pistol));
	else if (path && !strcmp(path, "mod\\weapons\\rifle\\rifle"))
		memcpy(values, fake_rifle, sizeof(fake_rifle));
	else
	{
		memset(values, 0, sizeof(double) * CONFIG_WEAPON_VALUES);
		return 0;
	}
	return 1;
}

/* each local player's move along the view's forward, recorded for the
weapon's near plane (view_fov.c's viewmodel_near_clip_distance) */
#define MAXIMUM_LOCAL_PLAYERS 4
static real applied_forwards[MAXIMUM_LOCAL_PLAYERS];

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

static void turns(double pitch, double yaw, double roll)
{
	fake_pitch = pitch;
	fake_yaw = yaw;
	fake_roll = roll;
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

#define PI_ 3.14159265358979323846f
/* the unit's 70 degrees across at 4:3 as main.c draws it, vertical */
#define STOCK_VERTICAL (2.f * atanf(0.75f * 0.85f * tanf(70.f * PI_ / 360.f)))

/* an angle in degrees across at 16:9 as the vertical one, in radians, as
display.viewmodel_fov's is */
static real across_16_9(real degrees)
{
	return 2.f * atanf(tanf(degrees * PI_ / 360.f) * 9.f / 16.f);
}


static real_point3d centroid(struct real_matrix4x3 const *nodes)
{
	real_point3d middle = { 0.f, 0.f, 0.f };
	short node;

	for (node = 0; node < NODES; node++)
	{
		middle.x += nodes[node].position.x / NODES;
		middle.y += nodes[node].position.y / NODES;
		middle.z += nodes[node].position.z / NODES;
	}
	return middle;
}

static real distance_between(real_point3d const *a, real_point3d const *b)
{
	return sqrtf((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y) + (a->z - b->z) * (a->z - b->z));
}

/* the weapon turned as one: each node as far from the others, its axes
unit length and square, about the same middle moved by (right, up,
forward) world units */
static void check_rigid(struct render_camera const *camera, struct real_matrix4x3 const *before,
	struct real_matrix4x3 const *after, real right_units, real up_units, real forward_units)
{
	real_point3d middle_before = centroid(before), middle_after = centroid(after);
	real_vector3d right;
	short a, b;

	cross_product3d(&camera->forward, &camera->up, &right);
	CHECK(fabsf(middle_after.x - (middle_before.x + right.i * right_units + camera->up.i * up_units +
		camera->forward.i * forward_units)) < 1e-5f &&
		fabsf(middle_after.y - (middle_before.y + right.j * right_units + camera->up.j * up_units +
		camera->forward.j * forward_units)) < 1e-5f &&
		fabsf(middle_after.z - (middle_before.z + right.k * right_units + camera->up.k * up_units +
		camera->forward.k * forward_units)) < 1e-5f, "the middle went elsewhere");
	for (a = 0; a < NODES; a++)
	{
		CHECK(fabsf(dot_product3d(&after[a].forward, &after[a].forward) - 1.f) < 1e-5f &&
			fabsf(dot_product3d(&after[a].up, &after[a].up) - 1.f) < 1e-5f &&
			fabsf(dot_product3d(&after[a].forward, &after[a].up)) < 1e-5f, "node %d's axes bent", a);
		for (b = a + 1; b < NODES; b++)
		{
			CHECK(fabsf(distance_between(&after[a].position, &after[b].position) -
				distance_between(&before[a].position, &before[b].position)) < 1e-5f, "nodes %d and %d parted", a, b);
		}
	}
}

#define DEGREES(d) ((real)(d) * PI_ / 180.f)

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
	struct viewmodel_offset offset;

	pose(&camera, before);
	memcpy(after, before, sizeof(after));

	CASE("stock")
	{
		/* 0, 0, 0: the game's pose, untouched */
		settings(0.0, 0.0, 0.0);
		CHECK(!viewmodel_offset(0, 5, &offset), "an offset of none");
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		CHECK(!memcmp(after, before, sizeof(after)), "the pose moved");
	}
	CASE("right")
	{
		/* along x, z up, the view's right is -y */
		settings(10.0, 0.0, 0.0);
		CHECK(viewmodel_offset(0, 5, &offset), "no offset");
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_moved(&camera, before, after, UNITS(10.0), 0.f, 0.f);
		CHECK(NEAR(after[0].position.y - before[0].position.y, -UNITS(10.0)), "moved %f along y",
			after[0].position.y - before[0].position.y);
	}
	CASE("up_and_forward")
	{
		settings(0.0, -4.5, 6.0);
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_moved(&camera, before, after, 0.f, -UNITS(4.5), UNITS(6.0));
		CHECK(NEAR(after[1].position.z - before[1].position.z, -UNITS(4.5)), "moved %f along z",
			after[1].position.z - before[1].position.z);
	}
	CASE("forward_recorded")
	{
		/* the move toward the eye recorded for the player's weapon's near
		plane, the weapon's own included and held at the range; none once
		the offset is gone */
		settings(0.0, 0.0, -15.0);
		fake_rifle[2] = -15.0;
		viewmodel_offset_apply(1, 6, after, NODES, &camera);
		CHECK(NEAR(applied_forwards[1], -UNITS(20.0)), "recorded %f", applied_forwards[1]);
		CHECK(applied_forwards[0] == 0.0f, "another player's recorded %f", applied_forwards[0]);
		settings(0.0, 0.0, 0.0);
		fake_rifle[2] = 0.0;
		fake_changes++;
		viewmodel_offset_apply(1, 6, after, NODES, &camera);
		CHECK(applied_forwards[1] == 0.0f, "still recorded %f", applied_forwards[1]);
	}
	CASE("turned_view")
	{
		/* a view turned and pitched: the move is the view's, not the world's */
		real c = cosf(0.7f), s = sinf(0.7f), p = cosf(-0.4f), q = sinf(-0.4f);

		camera = camera_of(vector(c * p, s * p, q), vector(-c * q, -s * q, p));
		pose(&camera, before);
		memcpy(after, before, sizeof(after));
		settings(3.0, -5.0, 2.0);
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
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
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
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
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		for (node = 0; node < NODES; node++)
		{
			real x0, y0, x1, y1;

			screen_of(&camera, &before[node].position, angles[0], &x0, &y0);
			screen_of(&camera, &after[node].position, angles[0], &x1, &y1);
			CHECK(fabsf(x1) < fabsf(x0) && fabsf(y1) < fabsf(y0), "node %d: %f %f to %f %f", node, x0, y0, x1, y1);
		}
	}
	CASE("weapon_fov_across")
	{
		/* moved right and down at a wide weapon field of view (150 across at
		16:9), a narrow one (60) and between (110): each node goes as far
		across the screen as the same move at the stock angle, the unit's 70
		across at 4:3 as main.c draws it; with no weapon field of view, the
		move set */
		real stock = STOCK_VERTICAL;
		real const drawn[] = { across_16_9(150.f), across_16_9(60.f), across_16_9(110.f) };
		struct real_matrix4x3 stock_moved[NODES];
		short a, node;

		settings(4.0, -3.0, 0.0);
		memcpy(stock_moved, before, sizeof(stock_moved));
		viewmodel_offset_apply(1, 5, stock_moved, NODES, &camera);
		check_moved(&camera, before, stock_moved, UNITS(4.0), -UNITS(3.0), 0.f);
		for (a = 0; a < (short)(sizeof(drawn) / sizeof(drawn[0])); a++)
		{
			real scale = tanf(drawn[a] / 2.f) / tanf(stock / 2.f);

			memcpy(after, before, sizeof(after));
			fake_drawn = drawn[a];
			fake_stock = stock;
			viewmodel_offset_apply(1, 5, after, NODES, &camera);
			check_moved(&camera, before, after, UNITS(4.0) * scale, -UNITS(3.0) * scale, 0.f);
			for (node = 0; node < NODES; node++)
			{
				real x0, y0, x1, y1, sx0, sy0, sx1, sy1;

				screen_of(&camera, &before[node].position, drawn[a], &x0, &y0);
				screen_of(&camera, &after[node].position, drawn[a], &x1, &y1);
				screen_of(&camera, &before[node].position, stock, &sx0, &sy0);
				screen_of(&camera, &stock_moved[node].position, stock, &sx1, &sy1);
				CHECK(fabsf((x1 - x0) - (sx1 - sx0)) < 1e-4f && fabsf((y1 - y0) - (sy1 - sy0)) < 1e-4f,
					"angle %f node %d: moved %f %f on the screen, at the stock angle %f %f", drawn[a], node,
					x1 - x0, y1 - y0, sx1 - sx0, sy1 - sy0);
			}
		}
		/* another local player's view, or none: the move set */
		memcpy(after, before, sizeof(after));
		viewmodel_offset_apply(0, 5, after, NODES, &camera);
		check_moved(&camera, before, after, UNITS(4.0), -UNITS(3.0), 0.f);
	}
	CASE("weapon_fov_depth")
	{
		/* pushed closer at the wide weapon field of view: moved exactly the
		centimeters set, and each node is drawn larger by the part it is at
		the stock angle (its place on the screen over its place before is
		the same ratio at both angles) */
		real stock = STOCK_VERTICAL;
		short node;

		settings(0.0, 0.0, -6.0);
		fake_drawn = across_16_9(150.f);
		fake_stock = stock;
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_moved(&camera, before, after, 0.f, 0.f, -UNITS(6.0));
		for (node = 0; node < NODES; node++)
		{
			real x0, y0, x1, y1, sx0, sy0, sx1, sy1;

			screen_of(&camera, &before[node].position, fake_drawn, &x0, &y0);
			screen_of(&camera, &after[node].position, fake_drawn, &x1, &y1);
			screen_of(&camera, &before[node].position, stock, &sx0, &sy0);
			screen_of(&camera, &after[node].position, stock, &sx1, &sy1);
			CHECK(x1 / x0 > 1.05f && fabsf(x1 / x0 - sx1 / sx0) < 1e-4f && fabsf(y1 / y0 - sy1 / sy0) < 1e-4f,
				"node %d: grown %f %f, at the stock angle %f %f", node, x1 / x0, y1 / y0, sx1 / sx0, sy1 / sy0);
		}
	}
	CASE("weapon_fov_unusable")
	{
		/* angles that are none (below 0, 0, a half turn and more): the move
		set */
		static real const bad[][2] = { { -1.f, 1.f }, { 1.f, 0.f }, { 3.2f, 1.f }, { 1.f, -0.5f } };
		short b;

		settings(4.0, -3.0, 2.0);
		for (b = 0; b < (short)(sizeof(bad) / sizeof(bad[0])); b++)
		{
			memcpy(after, before, sizeof(after));
			fake_drawn = bad[b][0];
			fake_stock = bad[b][1];
			viewmodel_offset_apply(1, 5, after, NODES, &camera);
			check_moved(&camera, before, after, UNITS(4.0), -UNITS(3.0), UNITS(2.0));
		}
	}
	CASE("held_at_the_range")
	{
		settings(50.0, -80.0, 20.0);
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(20.0)) && NEAR(offset.up, -UNITS(20.0)) && NEAR(offset.forward, UNITS(20.0)),
			"%f %f %f", offset.right, offset.up, offset.forward);
		/* (and the turns at 15 degrees, for a weapon with none of its own) */
		turns(40.0, -16.0, 0.0);
		viewmodel_offset(0, 7, &offset);
		CHECK(NEAR(offset.pitch, 15.f * PI_ / 180.f) && NEAR(offset.yaw, -15.f * PI_ / 180.f), "%f %f",
			offset.pitch, offset.yaw);
	}
	CASE("changed")
	{
		/* a change of the settings is read at once */
		settings(2.0, 0.0, 0.0);
		viewmodel_offset(0, 5, &offset);
		settings(-7.0, 0.0, 0.0);
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, -UNITS(7.0)), "%f", offset.right);
	}
	CASE("turn_pitch")
	{
		/* muzzle up: each node's forward toward the view's up by the angle,
		in place */
		short node;

		turns(10.0, 0.0, 0.0);
		CHECK(viewmodel_offset(0, 5, &offset), "no offset");
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_rigid(&camera, before, after, 0.f, 0.f, 0.f);
		for (node = 0; node < NODES; node++)
		{
			CHECK(NEAR(dot_product3d(&after[node].forward, &camera.up), sinf(DEGREES(10))) &&
				NEAR(dot_product3d(&after[node].forward, &camera.forward), cosf(DEGREES(10))),
				"node %d's forward %f up", node, dot_product3d(&after[node].forward, &camera.up));
		}
		/* the muzzle (the node ahead) higher than the hand behind it */
		CHECK(dot_product3d((real_vector3d const *)&after[2].position, &camera.up) -
			dot_product3d((real_vector3d const *)&after[0].position, &camera.up) >
			dot_product3d((real_vector3d const *)&before[2].position, &camera.up) -
			dot_product3d((real_vector3d const *)&before[0].position, &camera.up), "the muzzle went down");
	}
	CASE("turn_yaw")
	{
		/* muzzle right: forward toward the view's right */
		real_vector3d right;

		cross_product3d(&camera.forward, &camera.up, &right);
		turns(0.0, 12.0, 0.0);
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_rigid(&camera, before, after, 0.f, 0.f, 0.f);
		CHECK(NEAR(dot_product3d(&after[1].forward, &right), sinf(DEGREES(12))) &&
			NEAR(dot_product3d(&after[1].up, &camera.up), 1.f), "forward %f right",
			dot_product3d(&after[1].forward, &right));
	}
	CASE("turn_roll")
	{
		/* top to the right: up toward the view's right, forward kept */
		real_vector3d right;

		cross_product3d(&camera.forward, &camera.up, &right);
		turns(0.0, 0.0, 9.0);
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_rigid(&camera, before, after, 0.f, 0.f, 0.f);
		CHECK(NEAR(dot_product3d(&after[0].up, &right), sinf(DEGREES(9))) &&
			NEAR(dot_product3d(&after[0].forward, &camera.forward), 1.f), "up %f right",
			dot_product3d(&after[0].up, &right));
	}
	CASE("turn_and_move")
	{
		/* every turn and a move, in a turned and pitched view: the view's
		axes, the weapon as one, its middle moved by the move alone */
		real c = cosf(0.7f), s = sinf(0.7f), p = cosf(-0.4f), q = sinf(-0.4f);
		real_vector3d right;

		camera = camera_of(vector(c * p, s * p, q), vector(-c * q, -s * q, p));
		cross_product3d(&camera.forward, &camera.up, &right);
		pose(&camera, before);
		memcpy(after, before, sizeof(after));
		settings(3.0, -5.0, 2.0);
		turns(-7.0, 5.0, 11.0);
		viewmodel_offset_apply(1, 5, after, NODES, &camera);
		check_rigid(&camera, before, after, UNITS(3.0), -UNITS(5.0), UNITS(2.0));
		/* (roll, then pitch, then yaw: the forward's pitch and yaw as set) */
		CHECK(NEAR(dot_product3d(&after[0].forward, &camera.up), sinf(DEGREES(-7))) &&
			NEAR(dot_product3d(&after[0].forward, &right), cosf(DEGREES(-7)) * sinf(DEGREES(5))),
			"forward %f up %f right", dot_product3d(&after[0].forward, &camera.up),
			dot_product3d(&after[0].forward, &right));
	}
	CASE("weapon_own")
	{
		/* the pistol's own added to every weapon's; the modded rifle's its
		own; a weapon without, every weapon's alone */
		settings(2.0, 0.0, 0.0);
		turns(0.0, 2.0, 0.0);
		fake_pistol[0] = 3.0;
		fake_rifle[4] = 4.0;
		fake_rifle[2] = -1.5;
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(5.0)) && NEAR(offset.yaw, DEGREES(2)), "the pistol's %f %f", offset.right,
			offset.yaw);
		viewmodel_offset(0, 6, &offset);
		CHECK(NEAR(offset.right, UNITS(2.0)) && NEAR(offset.yaw, DEGREES(6)) && NEAR(offset.forward, -UNITS(1.5)),
			"the rifle's %f %f %f", offset.right, offset.yaw, offset.forward);
		viewmodel_offset(0, 7, &offset);
		CHECK(NEAR(offset.right, UNITS(2.0)) && NEAR(offset.yaw, DEGREES(2)), "no weapon's %f %f", offset.right,
			offset.yaw);
		/* the sum held at the range */
		fake_pistol[0] = 19.0;
		fake_pistol[5] = -14.0;
		turns(0.0, 0.0, -9.0);
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(20.0)) && NEAR(offset.roll, DEGREES(-15)), "held %f %f", offset.right,
			offset.roll);
		/* the pistol's own alone, every weapon's at 0: moved */
		settings(0.0, 0.0, 0.0);
		turns(0.0, 0.0, 0.0);
		memset(fake_pistol, 0, sizeof(fake_pistol));
		fake_pistol[1] = -2.0;
		CHECK(viewmodel_offset(0, 5, &offset) && NEAR(offset.up, -UNITS(2.0)), "the pistol's own alone %f", offset.up);
		CHECK(viewmodel_offset(0, 6, &offset) && NEAR(offset.forward, -UNITS(1.5)), "the rifle's %f", offset.forward);
	}
	CASE("weapon_own_changed")
	{
		/* looked up once a weapon and change: a change written is seen,
		and the weapon switched to */
		fake_pistol[0] = 1.0;
		fake_changes++;
		viewmodel_offset(0, 5, &offset);
		viewmodel_offset(0, 5, &offset);
		CHECK(fake_lookups == 1, "looked up %d times", fake_lookups);
		fake_pistol[0] = 4.0;
		fake_changes++;
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(4.0)), "the change unseen: %f", offset.right);
		viewmodel_offset(0, 6, &offset);
		CHECK(NEAR(offset.right, 0.f), "the pistol's on the rifle: %f", offset.right);
	}
	CASE("weapon_players")
	{
		/* each player's own weapon offsets: player 2's profile's pistol,
		player 1's config.toml's, each looked up apart */
		fake_pistol[0] = 3.0;
		fake_player2_pistol[0] = -6.0;
		fake_player2_pistol_set = 1;
		fake_changes++;
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(3.0)), "player 1's pistol %f", offset.right);
		viewmodel_offset(1, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(-6.0)), "player 2's pistol %f", offset.right);
		viewmodel_offset(0, 5, &offset);
		CHECK(NEAR(offset.right, UNITS(3.0)), "player 1's pistol after player 2's %f", offset.right);
		fake_player2_pistol_set = 0;
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
	CASE("players")
	{
		/* split screen's players each their own: player 2's of their
		profile, player 1's and no player's config.toml's */
		settings(2.0, 0.0, 0.0);
		fake_player2_x = 10.0;
		fake_changes++;
		CHECK(viewmodel_offset(1, 7, &offset) && NEAR(offset.right, UNITS(10.0)), "player 2's %f", offset.right);
		CHECK(viewmodel_offset(0, 7, &offset) && NEAR(offset.right, UNITS(2.0)), "player 1's %f", offset.right);
		CHECK(viewmodel_offset(NONE, 7, &offset) && NEAR(offset.right, UNITS(2.0)), "no player's %f", offset.right);
		CHECK(viewmodel_offset(1, 7, &offset) && NEAR(offset.right, UNITS(10.0)), "player 2's again %f",
			offset.right);
		fake_player2_x = 0.0;
	}
	return 0;
}
