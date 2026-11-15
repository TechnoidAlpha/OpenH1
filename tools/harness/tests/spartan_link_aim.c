/*
SPARTAN_LINK_AIM.C

The Spartan link's reticle on a teammate's feed (port/linux/game/
spartan_link.c): where their aim lands on the feed through its camera (the
middle along the aim; below the middle from a camera behind and above
them; nothing behind the camera or off the feed's picture), whether it is
red (a living enemy biped within the weapon's autoaim reach, or what one
holds; not an ally, the dead, the camouflaged, one autoaim ignores, a
vehicle, out of reach), and the line along the aim, which passes their own
unit and what it rides and stops at a wall. Against a world of its own:
spheres on the line and a wall across it.
*/

#include "harness.h"
#include <math.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct { real x, y; } real_point2d;
typedef struct { real x0, x1, y0, y1; } real_rectangle2d;

struct render_camera
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	real vertical_field_of_view;
};

/* ---------- the world */

enum { _object_type_biped = 0, _object_type_vehicle, _object_type_weapon, _object_type_scenery = 6 };
#define _object_mask_biped FLAG(_object_type_biped)
#define _object_mask_unit (FLAG(_object_type_biped) | FLAG(_object_type_vehicle))
enum { _object_invisible_bit = 0 };
enum { _object_dead_bit = 2 };
enum { _unit_ignored_by_autoaiming_bit = 4 };

struct object_datum
{
	long definition_index;
	struct
	{
		short type;
		unsigned long flags;
		unsigned long damage_flags;
		long parent_object_index;
		short owner_team_index;
		/* (the test's: where it is, and how big, on the line) */
		real_point3d position;
		real radius;
	} object;
	struct { real active_camouflage; } unit;
};
struct unit_datum { struct { real active_camouflage; } unit; };
struct biped_definition { struct { unsigned long flags; } unit; };

#define OBJECTS 8
static struct object_datum objects[OBJECTS];
static boolean exists[OBJECTS];
static struct biped_definition definitions[2];

static struct object_datum *object_try_and_get(long index)
{
	return index >= 0 && index < OBJECTS && exists[index] ? &objects[index] : NULL;
}
static struct unit_datum *unit_get(long index) { return (struct unit_datum *)&objects[index].unit; }
static struct biped_definition *biped_definition_get(long index) { return &definitions[index]; }
static boolean game_team_is_enemy(short team0, short team1) { return team0 != team1; }

/* ---------- the collision test */

enum
{
	_collision_test_front_facing_surfaces_bit = 0,
	_collision_test_ignore_invisible_surfaces_bit = 3,
	_collision_test_structure_bit = 5,
	_collision_test_objects_bit = 7,
	_collision_test_objects_bipeds_bit,
	_collision_test_objects_vehicles_bit,
	_collision_test_objects_scenery_bit = 14,
	_collision_test_objects_machines_bit,
};
enum { _collision_result_structure = 2, _collision_result_object };
enum { _collision_user_ui = 9 };
#define MAXIMUM_COLLISION_USER_STACK_DEPTH 32
static short global_current_collision_user_depth;
static short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];

struct collision_result
{
	short type;
	real t;
	real_point3d point;
	long object_index;
};

/* a wall across the line at x = wall_x (none: 0) */
static real wall_x;
static int tests;
static unsigned long tested_flags;

/* the first sphere or the wall the segment from point along vector meets,
but the ignored object */
static boolean collision_test_vector(unsigned long flags, real_point3d const *point, real_vector3d const *vector,
	long ignore_object_index, struct collision_result *collision)
{
	real length = sqrtf(vector->i * vector->i + vector->j * vector->j + vector->k * vector->k);
	real best = 2.0f;
	long index;

	tests++;
	tested_flags = flags;
	CHECK(global_current_collision_user_depth > 0, "a test with no collision user");
	if (wall_x != 0.0f && vector->i > 0.0f)
	{
		real t = (wall_x - point->x) / vector->i;

		if (t >= 0.0f && t <= 1.0f)
		{
			best = t;
			collision->type = _collision_result_structure;
			collision->object_index = NONE;
		}
	}
	for (index = 0; index < OBJECTS; index++)
	{
		struct object_datum *object = object_try_and_get(index);
		real along, across, t;
		real_vector3d to;

		if (!object || index == ignore_object_index)
			continue;
		to.i = object->object.position.x - point->x;
		to.j = object->object.position.y - point->y;
		to.k = object->object.position.z - point->z;
		along = (to.i * vector->i + to.j * vector->j + to.k * vector->k) / length;
		across = sqrtf(MAX(to.i * to.i + to.j * to.j + to.k * to.k - along * along, 0.0f));
		if (across > object->object.radius)
			continue;
		t = (along - sqrtf(object->object.radius * object->object.radius - across * across)) / length;
		/* (from inside it, its far side) */
		if (t < 0.0f)
			t = (along + sqrtf(object->object.radius * object->object.radius - across * across)) / length;
		if (t >= 0.0f && t <= 1.0f && t < best)
		{
			best = t;
			collision->type = _collision_result_object;
			collision->object_index = index;
		}
	}
	if (best > 1.0f)
		return FALSE;
	collision->t = best;
	collision->point.x = point->x + vector->i * best;
	collision->point.y = point->y + vector->j * best;
	collision->point.z = point->z + vector->k * best;
	return TRUE;
}

#include "under_test.inc"

#define NEAR(a, b, e) (fabsf((a) - (b)) <= (e))

static long add(long index, short type, short team, real x, real y, real z, real radius)
{
	memset(&objects[index], 0, sizeof(objects[index]));
	exists[index] = TRUE;
	objects[index].object.type = type;
	objects[index].object.owner_team_index = team;
	objects[index].object.parent_object_index = NONE;
	objects[index].object.position.x = x;
	objects[index].object.position.y = y;
	objects[index].object.position.z = z;
	objects[index].object.radius = radius;
	return index;
}

static void clear(void)
{
	memset(exists, 0, sizeof(exists));
	memset(definitions, 0, sizeof(definitions));
	wall_x = 0.0f;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	/* a feed of four at 1600x900's scale: 112 wide, 63 tall, up in the top line */
	real_rectangle2d feed = { 400.0f, 512.0f, 11.0f, 74.0f };
	real_point2d middle = { 456.0f, 42.5f };
	struct render_camera camera;
	real_point2d place;
	real tangent_y;

	memset(&camera, 0, sizeof(camera));
	camera.forward.i = 1.0f;
	camera.up.k = 1.0f;
	camera.vertical_field_of_view = 0.9601f;
	tangent_y = tanf(camera.vertical_field_of_view * 0.5f);
	CASE("place")
	{
		real_point3d point = { 40.0f, 0.0f, 0.0f };
		real width = feed.x1 - feed.x0, height = feed.y1 - feed.y0;

		/* along the camera's line: the middle */
		CHECK(spartan_link_aim_place(&camera, &feed, 3.0f, &point, &place), "the middle off the feed");
		CHECK(NEAR(place.x, middle.x, 0.01f) && NEAR(place.y, middle.y, 0.01f), "the middle at %f %f", place.x,
			place.y);
		/* to the right (the world's -y) and up: right of and above the middle,
		by the pinhole's amount */
		point.y = -2.0f;
		point.z = 1.5f;
		CHECK(spartan_link_aim_place(&camera, &feed, 3.0f, &point, &place), "a point off the feed");
		CHECK(NEAR(place.x, middle.x + 2.0f / 40.0f / (tangent_y * width / height) * width * 0.5f, 0.01f),
			"across at %f", place.x);
		CHECK(NEAR(place.y, middle.y - 1.5f / 40.0f / tangent_y * height * 0.5f, 0.01f), "up at %f", place.y);
		/* from behind and above a gunner (the feed's camera from behind), the
		aim 40 out from their eye along the same line lands below the middle */
		camera.position.x = -5.0f;
		camera.position.z = 1.0f;
		point.y = 0.0f;
		point.z = 0.0f;
		CHECK(spartan_link_aim_place(&camera, &feed, 3.0f, &point, &place), "the aim off the feed from behind");
		CHECK(NEAR(place.x, middle.x, 0.01f), "from behind across at %f", place.x);
		CHECK(NEAR(place.y, middle.y + 1.0f / 45.0f / tangent_y * height * 0.5f, 0.01f) && place.y > middle.y + 1.0f,
			"from behind at %f", place.y);
		/* behind the camera, and a reticle falling off the picture: none */
		point.x = -10.0f;
		CHECK(!spartan_link_aim_place(&camera, &feed, 3.0f, &point, &place), "a point behind the camera placed");
		camera.position.x = 0.0f;
		camera.position.z = 0.0f;
		point.x = 10.0f;
		point.z = tangent_y * 10.0f * 0.97f;
		CHECK(!spartan_link_aim_place(&camera, &feed, 3.0f, &point, &place), "a reticle over the feed's top edge");
		CHECK(spartan_link_aim_place(&camera, &feed, 0.0f, &point, &place), "a point inside the top edge lost");
	}
	CASE("color")
	{
		long enemy, ally, weapon, vehicle;

		clear();
		enemy = add(1, _object_type_biped, 2, 10.0f, 0.0f, 0.0f, 0.5f);
		ally = add(2, _object_type_biped, 1, 10.0f, 0.0f, 0.0f, 0.5f);
		vehicle = add(3, _object_type_vehicle, 2, 10.0f, 0.0f, 0.0f, 2.0f);
		weapon = add(4, _object_type_weapon, 2, 10.0f, 0.0f, 0.0f, 0.2f);
		objects[weapon].object.parent_object_index = enemy;
		definitions[1].unit.flags = FLAG(_unit_ignored_by_autoaiming_bit);
		CHECK(spartan_link_aim_enemy(1, enemy, 10.0f, 30.0f), "an enemy in reach blue");
		CHECK(spartan_link_aim_enemy(1, weapon, 10.0f, 30.0f), "an enemy's held weapon blue");
		CHECK(!spartan_link_aim_enemy(1, ally, 10.0f, 30.0f), "an ally red");
		CHECK(!spartan_link_aim_enemy(1, vehicle, 10.0f, 30.0f), "a vehicle red");
		CHECK(!spartan_link_aim_enemy(1, enemy, 31.0f, 30.0f), "an enemy out of reach red");
		CHECK(!spartan_link_aim_enemy(1, enemy, 10.0f, 0.0f), "red with no autoaim (no weapon)");
		CHECK(!spartan_link_aim_enemy(1, NONE, 10.0f, 30.0f), "nothing red");
		objects[enemy].unit.active_camouflage = 1.0f;
		CHECK(!spartan_link_aim_enemy(1, enemy, 10.0f, 30.0f), "a camouflaged enemy red");
		objects[enemy].unit.active_camouflage = 0.5f;
		CHECK(spartan_link_aim_enemy(1, enemy, 10.0f, 30.0f), "a shimmering enemy blue");
		objects[enemy].object.damage_flags = FLAG(_object_dead_bit);
		CHECK(!spartan_link_aim_enemy(1, enemy, 10.0f, 30.0f), "a dead enemy red");
		objects[enemy].object.damage_flags = 0;
		objects[enemy].definition_index = 1;
		CHECK(!spartan_link_aim_enemy(1, enemy, 10.0f, 30.0f), "an enemy autoaim ignores red");
	}
	CASE("ray")
	{
		real_point3d eye = { 0.0f, 0.0f, 0.0f };
		real_vector3d forward = { 1.0f, 0.0f, 0.0f };
		struct collision_result result;
		long self, ride, enemy;

		clear();
		/* the teammate (in a seat: the vehicle the root), their eye inside
		both; an enemy 20 out */
		ride = add(0, _object_type_vehicle, 1, 0.5f, 0.0f, 0.0f, 2.0f);
		self = add(5, _object_type_biped, 1, 0.0f, 0.0f, 0.0f, 0.4f);
		objects[self].object.parent_object_index = ride;
		enemy = add(1, _object_type_biped, 2, 20.0f, 0.0f, 0.0f, 0.5f);
		/* (the eye inside the teammate's own sphere: the test meets its far
		side first, the vehicle the test is told to pass) */
		eye.x = 0.1f;
		tests = 0;
		CHECK(spartan_link_aim_ray(self, &eye, &forward, 128.0f, &result), "the enemy missed");
		CHECK(result.type == _collision_result_object && result.object_index == enemy, "stopped at %ld",
			result.object_index);
		CHECK(NEAR(result.point.x, 19.5f, 0.05f), "the enemy met at %f", result.point.x);
		/* (two tests: the first met their own body's far side, the second
		went on past it to the enemy) */
		CHECK(tests == 2, "%d collision tests", tests);
		CHECK(global_current_collision_user_depth == 0, "the collision users left at %d",
			(int)global_current_collision_user_depth);
		CHECK(TEST_FLAG(tested_flags, _collision_test_objects_bipeds_bit) &&
			TEST_FLAG(tested_flags, _collision_test_structure_bit), "flags %lx", tested_flags);
		/* a wall before them: the wall */
		wall_x = 8.0f;
		CHECK(spartan_link_aim_ray(self, &eye, &forward, 128.0f, &result) &&
			result.type == _collision_result_structure && NEAR(result.point.x, 8.0f, 0.01f), "the wall missed");
		CHECK(!spartan_link_aim_enemy(1, result.object_index, 8.0f, 30.0f), "the wall red");
		/* out of reach: nothing */
		wall_x = 0.0f;
		CHECK(!spartan_link_aim_ray(self, &eye, &forward, 15.0f, &result), "met past the reach");
		CHECK(global_current_collision_user_depth == 0, "the collision users left");
	}
	return 0;
}
