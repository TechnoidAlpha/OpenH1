/*
FIRST_PERSON_LEGS_VIEW.C

The first-person legs moved to the view drawn (render_interpolation.c's
render_interpolation_view_body): turned about the vertical from the body's
heading as it is drawn (the blend of its facing at the ticks drawn between)
to the view drawn's, and moved from the view between the ticks' place, which
goes with the body, to the view drawn's.

- strafing (#697): drawn where the body is, the legs trailed the view drawn
  from the newest step by a tick's motion, more or less as each step and
  each frame's aim came, and stepped against it.
- turning (#826): the body faces the aim only once the aim is far enough
  from it, then turns after it a tick at a time; the view drawn turns every
  frame. Turned by the view's own turn alone, the legs swung away from the
  view and came back with a jerk each tick.
*/

#include "harness.h"
#include <math.h>

#define MAXIMUM_LOCAL_PLAYERS 4

typedef struct
{
	real scale;
	real_vector3d forward, left, up;
	real_point3d position;
} real_matrix4x3;

typedef short director_perspective;
enum
{
	_director_perspective_first_person,
	_director_perspective_third_person,
	_director_perspective_scripted
};

struct observer_result
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
};

struct object_datum
{
	struct
	{
		long parent_object_index;
		real_vector3d forward;
	} object;
};

#define MAXIMUM_INTERPOLATED_OBJECTS 8

struct interpolated_object
{
	long object_index;
	long tick;
	boolean has_previous;
	unsigned char latest;
	real_vector3d facing[2];
};

static struct
{
	short local_player_index;
} render;
static long fake_unit = 7;
static director_perspective fake_perspective;
static boolean fake_inhibited, fake_cinematic;
static struct object_datum fake_object = { { NONE, { 1.0f, 0.0f, 0.0f } } };
static struct interpolated_object fake_records[MAXIMUM_INTERPOLATED_OBJECTS];
static struct interpolated_object *interpolated_objects = fake_records;
static boolean interpolation_rendering;
static long interpolation_tick;
static real interpolation_fraction;

static long player_control_get_unit_index(short local_player_index) { (void)local_player_index; return fake_unit; }
static director_perspective director_get_perspective(short local_player_index)
{
	(void)local_player_index;
	return fake_perspective;
}
static boolean director_inhibited_facing(short local_player_index) { (void)local_player_index; return fake_inhibited; }
static boolean cinematic_in_progress(void) { return fake_cinematic; }
static struct object_datum *object_get(long object_index) { (void)object_index; return &fake_object; }
static struct object_datum *object_try_and_get(long object_index) { (void)object_index; return &fake_object; }
static real lerp(real a, real b, real t) { return a + (b - a) * t; }

static struct observer_result blended_views[MAXIMUM_LOCAL_PLAYERS];
static struct observer_result drawn_views[MAXIMUM_LOCAL_PLAYERS];
static boolean drawn_views_valid[MAXIMUM_LOCAL_PLAYERS];

#include "under_test.inc"

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

/* a view at a place, turned by yaw about the vertical and pitched (radians,
up positive), its up square to its forward */
static struct observer_result view(real x, real y, real z, real yaw, real pitch)
{
	struct observer_result result;

	result.position.x = x;
	result.position.y = y;
	result.position.z = z;
	result.forward.i = cosf(yaw) * cosf(pitch);
	result.forward.j = sinf(yaw) * cosf(pitch);
	result.forward.k = sinf(pitch);
	result.up.i = -cosf(yaw) * sinf(pitch);
	result.up.j = -sinf(yaw) * sinf(pitch);
	result.up.k = cosf(pitch);
	return result;
}

/* the point as the move takes it (matrix4x3_transform_point's, scale 1) */
static real_point3d moved(real_matrix4x3 const *m, real_point3d p)
{
	real_point3d result;

	result.x = m->forward.i * p.x + m->left.i * p.y + m->up.i * p.z + m->position.x;
	result.y = m->forward.j * p.x + m->left.j * p.y + m->up.j * p.z + m->position.y;
	result.z = m->forward.k * p.x + m->left.k * p.y + m->up.k * p.z + m->position.z;
	return result;
}

/* a point seen from a view, level: how far along its heading, to its left
and above it */
static real_point3d seen(struct observer_result const *v, real_point3d p)
{
	real length = sqrtf(v->forward.i * v->forward.i + v->forward.j * v->forward.j);
	real hi = v->forward.i / length, hj = v->forward.j / length;
	real dx = p.x - v->position.x, dy = p.y - v->position.y;
	real_point3d result;

	result.x = dx * hi + dy * hj;
	result.y = -dx * hj + dy * hi;
	result.z = p.z - v->position.z;
	return result;
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";
	real_matrix4x3 move;
	real_vector3d heading;
	boolean carried;

	CASE("strafing")
	{
		/* frames at 144 a second over ticks at 30, the player strafing left
		and right, a step's speed changing every tick (#697's case). The body
		and its view are drawn between the last two ticks; the view drawn
		from the newest tick, carried on by its velocity, and its aim swaying
		in each frame. Seen from the view drawn, the legs stay where the body
		has them from its own eye: they do not step against the view */
		real positions[64], velocities[64];
		long tick, frame_index;
		real previous[3] = { 0.0f, 0.0f, 0.0f };

		positions[0] = 0.0f;
		for (tick = 1; tick < 64; tick++)
		{
			/* left and right, a few ticks each, the speed ramping */
			real target = ((tick / 6) & 1) ? -0.075f : 0.075f;

			velocities[tick] = tick > 1 ? velocities[tick - 1] + (target - velocities[tick - 1]) * 0.5f : 0.0f;
			positions[tick] = positions[tick - 1] + velocities[tick];
		}
		for (frame_index = 0; frame_index < 60 * 144 / 30 - 10; frame_index++)
		{
			real t = 2.0f + frame_index / (144.0f / 30.0f);
			long newest = (long)t;
			real fraction = t - (real)newest;
			/* (a tick behind: between the tick before the newest and it) */
			real body_y = positions[newest - 1] + (positions[newest] - positions[newest - 1]) * fraction;
			real view_y = positions[newest] + velocities[newest] * fraction;
			real aim = 0.02f * sinf((real)frame_index * 1.7f);
			real_point3d body_foot = { 10.3f, 4.0f + body_y - 0.1f, -0.05f };
			real_point3d at;

			render.local_player_index = 0;
			fake_perspective = _director_perspective_first_person;
			fake_object.object.parent_object_index = NONE;
			drawn_views_valid[0] = TRUE;
			blended_views[0] = view(10.0f, 4.0f + body_y, 0.6f, 0.0f, -0.8f);
			drawn_views[0] = view(10.0f, 4.0f + view_y, 0.6f, aim, -0.8f);
			carried = render_interpolation_view_body(7, &move, &heading);
			CHECK(carried, "not moved");
			at = seen(&drawn_views[0], moved(&move, body_foot));
			if (frame_index > 0)
			{
				CHECK(NEAR(at.x, previous[0]) && NEAR(at.y, previous[1]) && NEAR(at.z, previous[2]),
					"frame %ld: the foot stepped by %f %f %f against the view", frame_index, at.x - previous[0],
					at.y - previous[1], at.z - previous[2]);
			}
			previous[0] = at.x;
			previous[1] = at.y;
			previous[2] = at.z;
		}
	}
	else CASE("turning")
	{
		/* frames at 144 a second over ticks at 30, the player standing,
		looking down and turning at 3 radians a second. The body faces the
		aim only once it is 0.5 radians from it, then turns 0.2244 radians a
		tick after it (the cyborg's, as logged from the game); it is drawn
		between the last two ticks, its feet turned with it about its place
		under the eye, and so is the view between the ticks. The view drawn
		turns every frame. Seen from the view drawn, the feet stay put */
		real facings[64], aims[64];
		long tick, frame_index;
		real previous[3] = { 0.0f, 0.0f, 0.0f };

		facings[0] = aims[0] = 0.0f;
		for (tick = 1; tick < 64; tick++)
		{
			aims[tick] = aims[tick - 1] + 0.1f;
			facings[tick] = facings[tick - 1];
			if (aims[tick] - facings[tick] > 0.5f || (aims[tick - 1] - facings[tick - 1] > 0.2244f &&
				facings[tick - 1] != (tick > 1 ? facings[tick - 2] : 0.0f)))
			{
				facings[tick] += 0.2244f;
			}
		}
		for (frame_index = 0; frame_index < 60 * 144 / 30 - 10; frame_index++)
		{
			real t = 2.0f + frame_index / (144.0f / 30.0f);
			long newest = (long)t;
			real fraction = t - (real)newest;
			real facing = facings[newest - 1] + (facings[newest] - facings[newest - 1]) * fraction;
			real blended_aim = aims[newest - 1] + (aims[newest] - aims[newest - 1]) * fraction;
			real drawn_aim = aims[newest] + 0.1f * fraction + 0.01f * sinf((real)frame_index * 1.7f);
			/* (the foot ahead and to the left of the body's place, turned
			with the body) */
			real_point3d body_foot;
			real_point3d at;

			body_foot.x = 10.0f + cosf(facing) * 0.3f - sinf(facing) * 0.1f;
			body_foot.y = 4.0f + sinf(facing) * 0.3f + cosf(facing) * 0.1f;
			body_foot.z = -0.05f;
			render.local_player_index = 0;
			fake_perspective = _director_perspective_first_person;
			fake_object.object.parent_object_index = NONE;
			/* (the game's facing is the newest tick's; the record has the
			two drawn between) */
			fake_object.object.forward.i = cosf(facings[newest]);
			fake_object.object.forward.j = sinf(facings[newest]);
			fake_object.object.forward.k = 0.0f;
			interpolation_rendering = TRUE;
			interpolation_tick = newest;
			interpolation_fraction = fraction;
			fake_records[7].object_index = 7;
			fake_records[7].tick = newest;
			fake_records[7].has_previous = TRUE;
			fake_records[7].latest = (unsigned char)(newest & 1);
			fake_records[7].facing[newest & 1].i = cosf(facings[newest]);
			fake_records[7].facing[newest & 1].j = sinf(facings[newest]);
			fake_records[7].facing[newest & 1].k = 0.0f;
			fake_records[7].facing[(newest & 1) ^ 1].i = cosf(facings[newest - 1]);
			fake_records[7].facing[(newest & 1) ^ 1].j = sinf(facings[newest - 1]);
			fake_records[7].facing[(newest & 1) ^ 1].k = 0.0f;
			drawn_views_valid[0] = TRUE;
			blended_views[0] = view(10.0f, 4.0f, 0.6f, blended_aim, -1.3f);
			drawn_views[0] = view(10.0f, 4.0f, 0.6f, drawn_aim, -1.3f);
			carried = render_interpolation_view_body(7, &move, &heading);
			CHECK(carried, "not moved");
			CHECK(NEAR(heading.i, cosf(drawn_aim)) && NEAR(heading.j, sinf(drawn_aim)) && heading.k == 0.0f,
				"frame %ld: the heading %f %f %f, not the view's", frame_index, heading.i, heading.j, heading.k);
			at = seen(&drawn_views[0], moved(&move, body_foot));
			if (frame_index > 0)
			{
				CHECK(NEAR(at.x, previous[0]) && NEAR(at.y, previous[1]) && NEAR(at.z, previous[2]),
					"frame %ld: the foot moved by %f %f %f against the view", frame_index, at.x - previous[0],
					at.y - previous[1], at.z - previous[2]);
			}
			previous[0] = at.x;
			previous[1] = at.y;
			previous[2] = at.z;
		}
	}
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	return 0;
}
