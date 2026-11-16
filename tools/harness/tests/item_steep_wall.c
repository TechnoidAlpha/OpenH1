#include "harness.h"
#include <math.h>

/* A loose item against a wall steeper than 45 degrees (items.c's
item_move_substep, #974): the original left an item where a bounce struck,
on the surface, so on such a wall (where nothing comes to rest) gravity's
push into it struck it there again every tick, at the line's start: it never
moved, its velocity grew, it sounded each tick. A strike where the item
already was now lifts it 0.05 off along the normal, and it falls on down the
wall to rest on the floor; but in a Legacy game (network_standard.c's
network_standard_simulation_fixes), whose OpenCE machines play the
original's. Nothing else an item does changes.

The world: a floor (z = 0) and a wall leaning back at 63 degrees from the
horizontal, as b40's slanted Forerunner walls do (its normal's k 0.447),
meeting at x = 0; the open space is in front of the wall and above the
floor. Collision tests take a point on a surface as on it: a line starting
there and going into it strikes at its start (t 0), as the structure's
test does. */

typedef struct { real_vector3d n; real d; } real_plane3d;
typedef struct { real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;
struct location { long cluster_index; };
#include "config.inc"

struct collision_result
{
	short type;
	struct location location;
	real t;
	real_point3d point;
	real_plane3d plane;
	short material_type;
	long object_index;
	long surface_index;
};

struct item_datum
{
	struct
	{
		real_point3d position;
		real_vector3d translational_velocity;
		real_vector3d angular_velocity;
		unsigned long flags;
		long owner_player_index;
		struct location location;
	} object;
	struct
	{
		long ignore_object_index;
		unsigned long flags;
		real_vector3d rotation_axis;
		short bsp_index;
		long item_on_rest_object_index;
		real_point3d item_rest_object_offset;
	} item;
};

struct item_definition
{
	struct
	{
		unsigned long flags;
		struct { long index; } material_effects, collision_sound;
	} item;
};

enum { _object_at_rest_bit };
enum { _item_on_structure_bit, _item_on_object_bit };
enum { _item_definition_antigravity_bit };
enum { ITEM_UPDATE_COLLISION_TEST_FLAGS = 0x1FF3E9 };
#define _object_mask_scenery 0x1
#define _object_mask_device 0x2
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))

/* (gravity a tick, in world units: the game's 4.17 a second squared, in
30ths, as the real tick's velocity.k changes) */
static real global_gravity = 0.0035584f;
static real const item_maximum_impact_velocity = 0.5f;
static real_vector3d const zero_vector = { 0.f, 0.f, 0.f };
static real_vector3d const *global_zero_vector3d = &zero_vector;

static struct item_datum the_item;
static boolean fixes;
static long impacts;
/* the strikes at a line's start */
static long start_strikes;

static struct item_datum *item_get(long index) { return &the_item; }
static real sim_substep_tick_fraction(void) { return 1.f; }
static short sim_substep_count(void) { return 1; }
static real sim_per_substep(real value) { return value; }
static boolean network_standard_simulation_fixes(void) { return fixes; }

static real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}
static real magnitude3d(real_vector3d const *v) { return sqrtf(dot_product3d(v, v)); }
static void scale_vector3d(real_vector3d const *v, real scale, real_vector3d *out)
{
	out->i = v->i * scale; out->j = v->j * scale; out->k = v->k * scale;
}
static void point_from_line3d(real_point3d const *point, real_vector3d const *vector, real scale, real_point3d *out)
{
	out->x = point->x + vector->i * scale; out->y = point->y + vector->j * scale; out->z = point->z + vector->k * scale;
}

/* the world's two surfaces */
static real_vector3d const floor_normal = { 0.f, 0.f, 1.f };
static real_vector3d const wall_normal = { 0.894427f, 0.f, 0.447214f };
#define ON_SURFACE 1e-4f

static real distance_to(real_vector3d const *normal, real_point3d const *p)
{
	return normal->i * p->x + normal->j * p->y + normal->k * p->z;
}
static boolean open_space(real_point3d const *p)
{
	return distance_to(&floor_normal, p) > -ON_SURFACE && distance_to(&wall_normal, p) > -ON_SURFACE;
}

static boolean collision_test_line(unsigned long flags, real_point3d const *start, real_point3d const *end,
	long ignore_object_index, struct collision_result *result)
{
	real_vector3d const *normals[2] = { &floor_normal, &wall_normal };
	boolean hit = FALSE;
	int i;

	for (i = 0; i < 2; i++)
	{
		real d0 = distance_to(normals[i], start), d1 = distance_to(normals[i], end), t;
		real_point3d point;

		if (fabsf(d0) <= ON_SURFACE)
			d0 = 0.f;
		if (d0 < 0.f || d1 >= d0 || d1 >= 0.f)
			continue;
		t = d0 / (d0 - d1);
		point.x = start->x + (end->x - start->x) * t;
		point.y = start->y + (end->y - start->y) * t;
		point.z = start->z + (end->z - start->z) * t;
		if (!open_space(&point) || (hit && t >= result->t))
			continue;
		hit = TRUE;
		if (t == 0.f)
			start_strikes++;
		result->type = _collision_result_structure;
		result->t = t;
		result->point = point;
		result->plane.n = *normals[i];
		result->plane.d = 0.f;
		result->material_type = 0;
		result->object_index = NONE;
		result->surface_index = i;
		result->location.cluster_index = 0;
	}
	return hit;
}
static boolean collision_test_point(unsigned long flags, real_point3d const *point, long ignore_object_index)
{
	return !open_space(point);
}

#define material_effect_visible(point) FALSE
#define material_effect_new(definition, count, material, point, normal, location, scale) ((void)0)
struct sound_location
{
	real_point3d position;
	real_vector3d forward, translational_velocity;
	struct location game_location;
};
static void unattached_impulse_sound_new(long sound_index, struct sound_location const *location, real scale)
{
	impacts++;
}
static void item_align_to_normal_and_point(long item_index, real_vector3d const *normal, real_point3d const *point,
	real_point3d *new_position)
{
	*new_position = *point;
}
#define game_engine_running() TRUE
#define object_set_garbage(index, garbage) ((void)0)
#define item_set_rested_surface(item, surface) ((void)0)
#define global_structure_bsp_index_get() 0
#define object_get_type(index) 0
#define object_get_node_matrix(index, node) ((real_matrix4x3 const *)NULL)
#define matrix4x3_inverse_transform_point(matrix, point, out) ((void)(matrix))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define item_adjust_for_angular_velocity_change(index) ((void)0)
static void object_translate(long index, real_point3d const *position, struct location const *location)
{
	the_item.object.position = *position;
}

#include "under_test.inc"

static struct item_definition definition = { { 0, { NONE }, { 1 } } };

static void place(real x, real z, real vi, real vk)
{
	memset(&the_item, 0, sizeof(the_item));
	the_item.object.position.x = x;
	the_item.object.position.z = z;
	the_item.object.translational_velocity.i = vi;
	the_item.object.translational_velocity.k = vk;
	the_item.object.owner_player_index = NONE;
	the_item.item.ignore_object_index = NONE;
	impacts = 0;
	start_strikes = 0;
}

/* a tick of item_update's: the item in flight moved, one at rest left */
static boolean tick(void)
{
	if (TEST_FLAG(the_item.object.flags, _object_at_rest_bit))
		return FALSE;
	item_move_substep(0, &definition, 0);
	return TRUE;
}

#define TICKS 300

/* lying against the wall a world unit up it, as a bounce leaves an item:
on it, a hair in front */
static void place_on_wall(void)
{
	place(-0.5f + wall_normal.i * 0.002f, 1.f + wall_normal.k * 0.002f, 0.f, 0.f);
}

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("steep-wall")
	{
		long ticks = 0;
		real fastest = 0.f;

		fixes = TRUE;
		place_on_wall();
		while (ticks < TICKS && tick())
		{
			real speed = magnitude3d(&the_item.object.translational_velocity);

			fastest = speed > fastest ? speed : fastest;
			ticks++;
		}
		CHECK(TEST_FLAG(the_item.object.flags, _object_at_rest_bit),
			"never came to rest: at (%.3f, %.3f), %.3f a tick, %ld impacts", the_item.object.position.x,
			the_item.object.position.z, magnitude3d(&the_item.object.translational_velocity), impacts);
		CHECK(the_item.object.position.z < 0.01f && the_item.object.position.x > 0.f,
			"came to rest at (%.3f, %.3f), not on the floor at the wall's foot", the_item.object.position.x,
			the_item.object.position.z);
		CHECK(fastest < 0.1f && the_item.object.position.x < 1.f,
			"%.3f a tick at most, at rest %.3f from the wall: thrown off it", fastest, the_item.object.position.x);
		CHECK(ticks < 60 && impacts < 15, "%ld ticks, %ld impacts to come to rest", ticks, impacts);
	}
	CASE("legacy")
	{
		/* the original's, as a Legacy game plays it: pinned where it lay,
		its velocity growing, an impact every tick */
		real_point3d start;
		long ticks;

		fixes = FALSE;
		place_on_wall();
		start = the_item.object.position;
		for (ticks = 0; ticks < TICKS; ticks++)
			tick();
		CHECK(!TEST_FLAG(the_item.object.flags, _object_at_rest_bit), "came to rest: not the original's");
		CHECK(fabsf(the_item.object.position.x - start.x) < 0.01f && fabsf(the_item.object.position.z - start.z) < 0.01f,
			"moved to (%.3f, %.3f): not the original's", the_item.object.position.x, the_item.object.position.z);
		CHECK(impacts >= TICKS - 2 && magnitude3d(&the_item.object.translational_velocity) > 0.5f,
			"%ld impacts, %.3f a tick: not the original's", impacts,
			magnitude3d(&the_item.object.translational_velocity));
	}
	CASE("unchanged")
	{
		/* a drop on the floor, and a throw that strikes the wall from away
		from it: tick for tick as the original moves them, to rest (or, the
		throw, until it lies on the wall, struck at a line's start, where
		the original would pin it) */
		static real const throws[][4] = {
			{ 3.f, 1.f, 0.03f, 0.02f },  /* dropped onto the floor */
			{ 1.5f, 1.2f, -0.12f, 0.f }, /* thrown at the wall */
		};
		int k;

		for (k = 0; k < 2; k++)
		{
			static struct item_datum path[2][TICKS];
			long counts[2], ticks[2], same = TICKS;
			int run;

			for (run = 0; run < 2; run++)
			{
				fixes = run == 1;
				place(throws[k][0], throws[k][1], throws[k][2], throws[k][3]);
				for (ticks[run] = 0; ticks[run] < TICKS && tick(); ticks[run]++)
				{
					path[run][ticks[run]] = the_item;
					if (!fixes && start_strikes && same == TICKS)
						same = ticks[run];
				}
				counts[run] = impacts;
			}
			CHECK(k == 1 || same == TICKS, "the drop struck at a line's start");
			if (same == TICKS)
			{
				CHECK(ticks[0] < TICKS, "throw %d never came to rest", k);
				CHECK(ticks[0] == ticks[1] && counts[0] == counts[1],
					"throw %d: %ld ticks and %ld impacts, not %ld and %ld", k, ticks[1], counts[1], ticks[0], counts[0]);
				same = ticks[0];
			}
			CHECK(counts[0] > 0, "throw %d struck nothing", k);
			CHECK(!memcmp(path[0], path[1], sizeof(path[0][0]) * same), "throw %d moved otherwise", k);
		}
	}
	return 0;
}
