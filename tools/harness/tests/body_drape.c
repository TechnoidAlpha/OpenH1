#include "harness.h"
#include <math.h>
#include <string.h>

#include "config.inc"

/* A dead body's animated pose fitted to what is around it
(biped_limp_noodle_drape_dying). The world: ground at z 0 for x below 1,
rising at a slope of SLOPE past it; a wall, solid for y below WALL_Y; the
biped's origin on the ground at (0, 0, 0). */
#define BODY 0x12340007L
#define SLOPE 0.5f
#define WALL_Y (-1.0f)
#define NODES 4

typedef struct { real scale; real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;
struct tag_block_header { long size; };
struct _object_datum
{
	real_point3d position;
	unsigned long damage_flags;
	long parent_object_index;
	struct tag_block_header node_matrices;
};
struct _biped_datum { unsigned long flags; };
struct biped_datum { struct _object_datum object; struct _biped_datum biped; };
#define object_datum biped_datum
struct collision_plane3d { real_vector3d n; real d; };
struct collision_result { real_point3d point; struct collision_plane3d plane; };

static struct biped_datum body;
static real_matrix4x3 nodes[NODES];
static int wall_present = 1;

static struct biped_datum *object_get(long index) { CHECK(index == BODY, "not the body"); return &body; }
#define biped_get(index) object_get(index)
#define MIN(a,b) ((a)>(b)?(b):(a))
#define MAX(a,b) ((a)>(b)?(a):(b))
static void set_real_vector3d(real_vector3d *v, real i, real j, real k) { v->i = i; v->j = j; v->k = k; }
static void set_real_point3d(real_point3d *p, real x, real y, real z) { p->x = x; p->y = y; p->z = z; }
static real magnitude3d(real_vector3d const *v) { return sqrtf(v->i * v->i + v->j * v->j + v->k * v->k); }
static real normalize3d(real_vector3d *v)
{
	real length = magnitude3d(v);

	if (length > 0.f)
	{
		v->i /= length;
		v->j /= length;
		v->k /= length;
	}
	return length;
}
static real_vector3d *scale_vector3d(real_vector3d const *v, real s, real_vector3d *result)
{
	result->i = v->i * s;
	result->j = v->j * s;
	result->k = v->k * s;
	return result;
}

static real ground_height(real x) { return x > 1.f ? (x - 1.f) * SLOPE : 0.f; }
static boolean inside(real_point3d const *p)
{
	return p->z < ground_height(p->x) || (wall_present && p->y < WALL_Y);
}
#define collision_test_point(flags, point, ignore) inside(point)
/* (the first point inside, along the segment, found in small steps) */
static boolean collision_test_vector(unsigned long flags, real_point3d const *start, real_vector3d const *vector,
	long ignore, struct collision_result *result)
{
	int step;

	for (step = 1; step <= 4000; step++)
	{
		real t = step / 4000.f;
		real_point3d p = { start->x + vector->i * t, start->y + vector->j * t, start->z + vector->k * t };

		if (inside(&p))
		{
			result->point = p;
			if (wall_present && p.y < WALL_Y)
				set_real_vector3d(&result->plane.n, 0.f, 1.f, 0.f);
			else if (p.x > 1.f)
			{
				set_real_vector3d(&result->plane.n, -SLOPE, 0.f, 1.f);
				normalize3d(&result->plane.n);
			}
			else
				set_real_vector3d(&result->plane.n, 0.f, 0.f, 1.f);
			return TRUE;
		}
	}
	return FALSE;
}

#include "under_test.inc"

/* a body lying dead, its nodes at these places */
static void lying(real_point3d const *places)
{
	int i;

	memset(&body, 0, sizeof(body));
	memset(nodes, 0, sizeof(nodes));
	body.object.parent_object_index = NONE;
	body.object.node_matrices.size = sizeof(nodes);
	SET_FLAG(body.object.damage_flags, _object_dead_bit, TRUE);
	for (i = 0; i < NODES; i++)
		nodes[i].position = places[i];
}

static real near(real a, real b) { return fabsf(a - b) < 0.005f; }

int main(int argc, char **argv)
{
	const char *case_name = argv[1];

	CASE("flat")
	{
		/* open, flat ground (no wall near): the animation's pose as it is */
		real_point3d places[NODES] = { {0.f, 0.f, 0.07f}, {-0.3f, 0.1f, 0.05f}, {0.4f, -0.2f, 0.03f}, {0.6f, 0.3f, 0.2f} };
		int i;

		lying(places);
		biped_limp_noodle_drape_dying(BODY, nodes);
		for (i = 0; i < NODES; i++)
			CHECK(nodes[i].position.x == places[i].x && nodes[i].position.y == places[i].y &&
				nodes[i].position.z == places[i].z, "node %d moved on flat ground", i);
		return 0;
	}
	CASE("slope")
	{
		/* the body lying across the foot of the slope: what is uphill raised
		as far as the ground rises under it, and what is not left */
		real_point3d places[NODES] = { {0.f, 0.f, 0.07f}, {0.8f, 0.f, 0.05f}, {1.2f, 0.f, 0.04f}, {1.3f, 0.2f, 0.3f} };

		lying(places);
		biped_limp_noodle_drape_dying(BODY, nodes);
		CHECK(near(nodes[0].position.z, 0.07f) && near(nodes[1].position.z, 0.05f), "a node on the flat moved");
		CHECK(near(nodes[2].position.z, 0.04f + 0.2f * SLOPE - BIPED_DRAPE_DEAD_ZONE), "uphill node at %f",
			nodes[2].position.z);
		CHECK(nodes[2].position.z > ground_height(nodes[2].position.x), "uphill node still under the ground");
		CHECK(near(nodes[3].position.z, 0.3f + 0.3f * SLOPE - BIPED_DRAPE_DEAD_ZONE), "high node at %f",
			nodes[3].position.z);
		return 0;
	}
	CASE("steep")
	{
		/* up the slope, a little way in the ground: raised no more than
		BIPED_DRAPE_MAXIMUM; deep in it (in something else): left */
		real_point3d places[NODES] = { {0.f, 0.f, 0.07f}, {0.5f, 0.f, 0.05f}, {2.0f, 0.f, 0.45f}, {2.6f, 0.f, 0.04f} };

		lying(places);
		biped_limp_noodle_drape_dying(BODY, nodes);
		CHECK(near(nodes[2].position.z, 0.45f + BIPED_DRAPE_MAXIMUM), "far node at %f", nodes[2].position.z);
		CHECK(nodes[3].position.z == places[3].z, "a node deep in the ground raised to %f", nodes[3].position.z);
		/* (the slope is ground, not a wall: nothing pushed back from it) */
		CHECK(nodes[2].position.x == places[2].x && nodes[0].position.x == places[0].x, "pushed back from the slope");
		return 0;
	}
	CASE("wall")
	{
		/* a leg through the wall: the whole body moved out of it, clear of it
		by BIPED_DRAPE_CLEARANCE, its pose kept */
		real_point3d places[NODES] = { {0.f, -0.7f, 0.07f}, {0.f, -0.9f, 0.05f}, {0.1f, -1.1f, 0.04f}, {-0.2f, -0.5f, 0.1f} };
		int i;

		lying(places);
		body.object.position.y = -0.7f;
		biped_limp_noodle_drape_dying(BODY, nodes);
		for (i = 0; i < NODES; i++)
		{
			CHECK(nodes[i].position.y >= WALL_Y + BIPED_DRAPE_CLEARANCE - 0.005f, "node %d in the wall at %f", i,
				nodes[i].position.y);
			CHECK(near(nodes[i].position.y - places[i].y, 0.1f + BIPED_DRAPE_CLEARANCE), "node %d moved by %f", i,
				nodes[i].position.y - places[i].y);
			CHECK(nodes[i].position.x == places[i].x && near(nodes[i].position.z, places[i].z), "node %d moved across",
				i);
		}
		return 0;
	}
	CASE("limp")
	{
		/* a body lying limp: its nodes are the relaxation's, left alone */
		real_point3d places[NODES] = { {0.f, 0.f, 0.07f}, {0.8f, 0.f, 0.05f}, {1.2f, 0.f, 0.04f}, {1.3f, 0.2f, 0.3f} };

		lying(places);
		SET_FLAG(body.biped.flags, _biped_limp_body_physics_active_bit, TRUE);
		biped_limp_noodle_drape_dying(BODY, nodes);
		CHECK(nodes[2].position.z == places[2].z, "a limp body's node moved");
		return 0;
	}
	CASE("living")
	{
		/* a living biped: its animation's pose */
		real_point3d places[NODES] = { {0.f, 0.f, 0.07f}, {0.8f, 0.f, 0.05f}, {1.2f, 0.f, 0.04f}, {1.3f, 0.2f, 0.3f} };

		lying(places);
		SET_FLAG(body.object.damage_flags, _object_dead_bit, FALSE);
		biped_limp_noodle_drape_dying(BODY, nodes);
		CHECK(nodes[2].position.z == places[2].z, "a living biped's node moved");
		return 0;
	}
	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
