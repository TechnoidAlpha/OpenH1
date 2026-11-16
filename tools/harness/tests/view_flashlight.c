/*
VIEW_FLASHLIGHT.C

The local player's own flashlight in the first person on foot, where no
first-person weapon carries it (render_interpolation.c's
render_interpolation_view_flashlight, #589): it shines along the view drawn,
from where the body has it seen from the view between the ticks, so it
neither trails the view drawn from the newest step nor swings with the
head's look. Any other unit, view or perspective is left as the game has it.
*/

#include "harness.h"
#include <math.h>

#define MAXIMUM_LOCAL_PLAYERS 4

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
	} object;
};

static struct
{
	short local_player_index;
} render;
static long fake_unit = 7;
static director_perspective fake_perspective;
static boolean fake_inhibited, fake_cinematic;
static struct object_datum fake_object = { { NONE } };

static long player_control_get_unit_index(short local_player_index) { (void)local_player_index; return fake_unit; }
static director_perspective director_get_perspective(short local_player_index)
{
	(void)local_player_index;
	return fake_perspective;
}
static boolean director_inhibited_facing(short local_player_index) { (void)local_player_index; return fake_inhibited; }
static boolean cinematic_in_progress(void) { return fake_cinematic; }
static struct object_datum *object_get(long object_index) { (void)object_index; return &fake_object; }
static void cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *result)
{
	result->i = a->j * b->k - a->k * b->j;
	result->j = a->k * b->i - a->i * b->k;
	result->k = a->i * b->j - a->j * b->i;
}
static real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

static struct observer_result blended_views[MAXIMUM_LOCAL_PLAYERS];
static struct observer_result drawn_views[MAXIMUM_LOCAL_PLAYERS];
static boolean drawn_views_valid[MAXIMUM_LOCAL_PLAYERS];

#include "under_test.inc"

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-5f)

/* a view at a place, turned by yaw (radians) about the vertical, level */
static struct observer_result view(real x, real y, real z, real yaw)
{
	struct observer_result result;

	result.position.x = x;
	result.position.y = y;
	result.position.z = z;
	result.forward.i = cosf(yaw);
	result.forward.j = sinf(yaw);
	result.forward.k = 0.0f;
	result.up.i = 0.0f;
	result.up.j = 0.0f;
	result.up.k = 1.0f;
	return result;
}

/* the frame: the body's view between the ticks a tick behind (at the
origin, facing +x), the view drawn on (moved and turned 0.3 rad left) */
static void frame(void)
{
	render.local_player_index = 0;
	fake_unit = 7;
	fake_perspective = _director_perspective_first_person;
	fake_inhibited = FALSE;
	fake_cinematic = FALSE;
	fake_object.object.parent_object_index = NONE;
	blended_views[0] = view(0.0f, 0.0f, 0.6f, 0.0f);
	drawn_views[0] = view(0.1f, 0.05f, 0.6f, 0.3f);
	drawn_views_valid[0] = TRUE;
}

int main(int argc, char **argv)
{
	char const *case_name = argc == 2 ? argv[1] : "";
	/* the body's flashlight: 0.2 ahead of the eye, 0.1 to the right, 0.05
	down, turned 0.4 rad right of the view with the head's look */
	real_point3d position = { 0.2f, -0.1f, 0.55f };
	real_vector3d forward = { 0.921061f, -0.389418f, 0.0f }, up = { 0.0f, 0.0f, 1.0f };
	real_point3d moved = position;
	real_vector3d turned = forward, turned_up = up;
	boolean carried;

	frame();
	CASE("follows_the_view")
	{
		/* along the view drawn, from as far ahead, across and down of it as
		the body has it of the view between the ticks */
		real c = cosf(0.3f), s = sinf(0.3f);

		carried = render_interpolation_view_flashlight(7, &moved, &turned, &turned_up);
		CHECK(carried, "not carried");
		CHECK(NEAR(turned.i, c) && NEAR(turned.j, s) && NEAR(turned.k, 0.0f), "axis %f %f %f, not the view's",
			turned.i, turned.j, turned.k);
		CHECK(NEAR(turned_up.k, 1.0f), "up %f", turned_up.k);
		CHECK(NEAR(moved.x, 0.1f + 0.2f * c + 0.1f * s) && NEAR(moved.y, 0.05f + 0.2f * s - 0.1f * c) &&
			NEAR(moved.z, 0.55f), "origin %f %f %f", moved.x, moved.y, moved.z);
	}
	else CASE("still")
	{
		/* standing still the views agree: the origin stays, the axis is the view's */
		drawn_views[0] = blended_views[0];
		carried = render_interpolation_view_flashlight(7, &moved, &turned, &turned_up);
		CHECK(carried && NEAR(moved.x, position.x) && NEAR(moved.y, position.y) && NEAR(moved.z, position.z),
			"moved standing still: %f %f %f", moved.x, moved.y, moved.z);
		CHECK(NEAR(turned.i, 1.0f) && NEAR(turned.j, 0.0f), "axis %f %f", turned.i, turned.j);
	}
	else CASE("left_alone")
	{
		/* another unit's, another perspective's, a seat's, a cinematic's, a
		held view's, a window with no view: as the game has it */
		int which;

		for (which = 0; which < 7; which++)
		{
			frame();
			moved = position;
			turned = forward;
			if (which == 0)
				fake_unit = 8;
			else if (which == 1)
				fake_perspective = _director_perspective_third_person;
			else if (which == 2)
				fake_object.object.parent_object_index = 12;
			else if (which == 3)
				fake_cinematic = TRUE;
			else if (which == 4)
				fake_inhibited = TRUE;
			else if (which == 5)
				drawn_views_valid[0] = FALSE;
			else
				render.local_player_index = NONE;
			carried = render_interpolation_view_flashlight(7, &moved, &turned, &turned_up);
			CHECK(!carried && moved.x == position.x && moved.y == position.y && turned.i == forward.i &&
				turned.j == forward.j, "case %d moved", which);
		}
	}
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	return 0;
}
