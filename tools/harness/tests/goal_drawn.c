#include "harness.h"
#include <math.h>

/* A fake world for a game engine goal's waypoint (game_engine.c's game_engine_set_goal_object and
game_engine_get_goal_drawn_position: a flag, the ball, a target), drawn over its object between the ticks. At 60 and
120 the goal is set in a tick's work, at its first sub-step; the object's sub-steps then move it on, and a frame draws
it between its snapshots. The object's root node: where the tick's work saw it, where the sub-steps have moved it
since, and where the frame draws it (turned, too). */

typedef struct { real_vector3d forward, left, up; real_point3d position; real scale; } real_matrix4x3;
#define NUMBEROF(array) (sizeof(array) / sizeof(array[0]))
#define VALID_INDEX(index, count) (index>=0 && index<count)
#define MULTIPLAYER_MAXIMUM_PLAYERS 16
#define MAX(a, b) ((a) > (b) ? (a) : (b))
enum { _item_attached_to_unit_bit };
struct item_datum { struct { long owner_player_index; } object; struct { unsigned long flags; } item; };
struct player_datum { long unit_index; };

#include "decls.inc"

#define FLAG_OBJECT 0x00050005L
static struct item_datum the_flag;
static real_matrix4x3 root_now; /* the object's root node as the game has it (moved by the sub-steps run) */
static real_matrix4x3 root_drawn; /* ... and as a frame draws it */
static boolean rendering;

static real_matrix4x3 matrix_at(real x, real y, real z, real yaw)
{
	real_matrix4x3 m;

	m.forward.i = cosf(yaw); m.forward.j = sinf(yaw); m.forward.k = 0.0f;
	m.left.i = -sinf(yaw); m.left.j = cosf(yaw); m.left.k = 0.0f;
	m.up.i = 0.0f; m.up.j = 0.0f; m.up.k = 1.0f;
	m.position.x = x; m.position.y = y; m.position.z = z;
	m.scale = 1.0f;
	return m;
}

static struct item_datum *item_try_and_get(long object_index) { return object_index == FLAG_OBJECT ? &the_flag : NULL; }
static void *object_try_and_get(long object_index) { return object_index == FLAG_OBJECT ? &the_flag : NULL; }
static struct player_datum *player_get(long player_index) { static struct player_datum player; (void)player_index; return &player; }
static real_matrix4x3 *render_interpolation_object_node_matrices(long object_index)
{
	return rendering && object_index == FLAG_OBJECT ? &root_drawn : NULL;
}
/* (objects.c's: as a frame draws it, in a frame) */
static real_matrix4x3 *object_get_node_matrix(long object_index, short node_index)
{
	CHECK(object_index == FLAG_OBJECT && node_index == 0, "no node");
	return rendering ? &root_drawn : &root_now;
}
/* (the old way: the point moved with the object from its newest nodes to where it is drawn) */
static void render_interpolation_object_point(long object_index, real_point3d *point)
{
	(void)object_index;
	if (!rendering)
		return;
	point->x += root_drawn.position.x - root_now.position.x;
	point->y += root_drawn.position.y - root_now.position.y;
	point->z += root_drawn.position.z - root_now.position.z;
}
static void matrix4x3_inverse_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *result)
{
	real_vector3d d = { p->x - m->position.x, p->y - m->position.y, p->z - m->position.z };
	real_point3d r;

	r.x = (d.i * m->forward.i + d.j * m->forward.j + d.k * m->forward.k) / m->scale;
	r.y = (d.i * m->left.i + d.j * m->left.j + d.k * m->left.k) / m->scale;
	r.z = (d.i * m->up.i + d.j * m->up.j + d.k * m->up.k) / m->scale;
	*result = r;
}
static void matrix4x3_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *result)
{
	real_point3d r;

	r.x = m->position.x + (m->forward.i * p->x + m->left.i * p->y + m->up.i * p->z) * m->scale;
	r.y = m->position.y + (m->forward.j * p->x + m->left.j * p->y + m->up.j * p->z) * m->scale;
	r.z = m->position.z + (m->forward.k * p->x + m->left.k * p->y + m->up.k * p->z) * m->scale;
	*result = r;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real_point3d drawn;

	/* the goal set in the tick's work over the flag (0.63 above its root), the flag then moved on by the sub-steps
	(0.5 along x), and a frame drawing the flag between the ticks (0.2 along x, turned a quarter): the waypoint is
	over the flag where the frame draws it */
	CASE("held_to_its_object")
	{
		the_flag.object.owner_player_index = NONE;
		root_now = matrix_at(10.0f, 0.0f, 1.0f, 0.0f);
		global_goal[1].in_use = TRUE;
		global_goal[1].position.x = 10.0f;
		global_goal[1].position.y = 0.0f;
		global_goal[1].position.z = 1.63f;
		game_engine_set_goal_object(1, FLAG_OBJECT);
		root_now = matrix_at(10.5f, 0.0f, 1.0f, 0.0f);
		root_drawn = matrix_at(10.2f, 0.0f, 1.0f, 1.5707964f);
		rendering = TRUE;
		game_engine_get_goal_drawn_position(&drawn, 1);
		CHECK(fabsf(drawn.x - 10.2f) < 1e-4f && fabsf(drawn.y) < 1e-4f && fabsf(drawn.z - 1.63f) < 1e-4f,
			"drawn at (%f %f %f), not over the flag as drawn (10.2 0 1.63)", drawn.x, drawn.y, drawn.z);
		/* outside a frame, where the goal is */
		rendering = FALSE;
		game_engine_get_goal_drawn_position(&drawn, 1);
		CHECK(fabsf(drawn.x - 10.0f) < 1e-4f, "outside a frame at %f", drawn.x);
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
