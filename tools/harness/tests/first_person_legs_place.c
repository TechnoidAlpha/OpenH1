/*
FIRST_PERSON_LEGS_PLACE.C

The first-person body on the flat screen (port/linux/game/first_person_legs.c):
its pose (first_person_legs_node_matrices: the body placed for the eye by
first_person_legs_place, its chest and all above it folded into the chest's
joint) and its shadow's (first_person_legs_shadow_node_matrices), made for a
body of the test's own, a skeleton of the game's node names placed as the
cyborg's are about its eye (standing, crouching, a crouch jump's), in views
of the test's own:

- planted (#724, #772): the view tilted from level to looking straight down,
  and turned: the legs' pose is the same in every frame, the body moved along
  its own heading so that the eye is over the front of its hips and belly at
  rest (the model's mesh's), its feet where the game has them less that; and
  seen from the view drawn (render_interpolation_view_body), turned to the
  lower body's own heading about the eye (first_person_legs_lower_get), the
  same however the view turns;
- turned: the lower body's heading turned, the pose turned as much about the
  eye's vertical, nothing else changed;
- stateless (#772): drawn again in the same view after other views, the pose
  is the same, whatever came before (no lean stepped frame by frame);
- clear (#772): standing and crouching on the ground, nothing drawn is
  within the game's near distance of the eye nor above it, the chest and all
  above it folded into the chest's joint, the belly flattened onto its joint's
  height, and nothing lowered;
- crouch_jump (#692): the eye down at the hips: the body lowered as far as
  keeps its head below the eye as the ground's poses have it, nothing drawn
  near the eye;
- rigid (#772): what is drawn is the game's pose moved whole: every two
  nodes drawn as far apart as the game has them (nothing stretched);
- shadow: the shadow's pose is placed as the legs are, nothing folded nor
  flattened.
*/

#include "harness.h"
#include <math.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define _pi 3.14159265358979323846
#define TAG_STRING_LENGTH 31

#include "types.inc"

/* the fake world: the player's unit, its model's nodes, the view */
struct model_node
{
	char name[TAG_STRING_LENGTH + 1];
	short parent_node_index;
	real_point3d default_translation;
};
struct tag_block
{
	long count;
	void *elements;
};
struct model
{
	struct tag_block nodes;
};
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->elements)[index])
struct object_definition
{
	struct { struct { long index; } model; } object;
};
struct object_datum
{
	long definition_index;
	struct
	{
		long parent_object_index;
		real_vector3d forward;
		struct { long size; } node_matrices;
	} object;
};
static struct
{
	struct { real_point3d position; real_vector3d forward; } camera;
} global_window_parameters;
static real_point3d const test_origin3d = { 0.f, 0.f, 0.f };
static real_vector3d const test_forward3d = { 1.f, 0.f, 0.f }, test_up3d = { 0.f, 0.f, 1.f };
#define global_origin3d (&test_origin3d)
#define global_forward3d (&test_forward3d)
#define global_up3d (&test_up3d)

/* the model's surface (object_mesh.h's), a few points of it */
struct object_mesh_vertex
{
	real_point3d position;
	byte nodes[2];
	short weight;
};
struct object_mesh
{
	long vertex_count;
	struct object_mesh_vertex *vertices;
};
static struct object_mesh test_mesh;
#define object_mesh_get_model(index) (&test_mesh)

/* the unit as a biped (first_person_legs_place's walk of its own, never
drawn here) */
struct biped_definition
{
	struct { real standing_camera_height, crouching_camera_height; } biped;
};
struct biped_datum
{
	long definition_index;
	struct { real crouch; } biped;
};
static struct biped_definition test_biped_definition;
static struct biped_datum test_biped;
#define biped_try_and_get(index) (&test_biped)
#define biped_definition_get(index) (&test_biped_definition)

static struct model test_model;
static struct object_definition test_definition;
static struct object_datum test_unit;
#define object_try_and_get(index) (&test_unit)
#define object_definition_get(index) (&test_definition)
#define model_definition_get(index) (&test_model)
/* the view drawn (render_interpolation_view_body): none (the body's own
place, the third person's), or the body turned about the eye from its heading
to the view's, level */
static boolean test_view_drawn;
static boolean test_view_body(real_matrix4x3 *move, real_vector3d *heading);
#define render_interpolation_view_body(unit_index, move, heading) test_view_body((move), (heading))

#include "lower.inc"

/* the lower body's own heading (first_person_legs_lower_get): the test's */
static struct first_person_legs_lower test_lower;
#define first_person_legs_lower_get(unit_index, model, count, aim) (&test_lower)

#include "under_test.inc"

static boolean test_view_body(real_matrix4x3 *move, real_vector3d *heading)
{
	real_vector3d view = global_window_parameters.camera.forward;
	real from = atan2f(test_unit.object.forward.j, test_unit.object.forward.i), to, c, s;
	real_point3d const *eye = &global_window_parameters.camera.position;

	memset(move, 0, sizeof(*move));
	move->scale = 1.f;
	move->forward.i = 1.f;
	move->left.j = 1.f;
	move->up.k = 1.f;
	*heading = test_unit.object.forward;
	if (!test_view_drawn)
		return FALSE;
	view.k = 0.f;
	to = atan2f(view.j, view.i);
	c = cosf(to - from);
	s = sinf(to - from);
	move->forward.i = c;
	move->forward.j = s;
	move->left.i = -s;
	move->left.j = c;
	move->position.x = eye->x - (c * eye->x - s * eye->y);
	move->position.y = eye->y - (s * eye->x + c * eye->y);
	heading->i = cosf(to);
	heading->j = sinf(to);
	heading->k = 0.f;
	return TRUE;
}

/* the game's near distance (rasterizer.c's rasterizer_global_defaults) */
#define GAME_NEAR 0.0625f

enum
{
	PELVIS, SPINE, SPINE1, NECK, HEAD, LEFT_CLAVICLE, LEFT_ARM, RIGHT_CLAVICLE, RIGHT_ARM,
	LEFT_THIGH, LEFT_CALF, LEFT_FOOT, RIGHT_THIGH, RIGHT_CALF, RIGHT_FOOT,
	NODES
};
static struct model_node nodes_named[NODES] =
{
	{ "bip01 pelvis", NONE, { -0.008f, 0.002f, 0.394f } }, { "bip01 spine", PELVIS }, { "bip01 spine1", SPINE }, { "bip01 neck", SPINE1 },
	{ "bip01 head", NECK }, { "bip01 l clavicle", SPINE1 }, { "bip01 l upperarm", LEFT_CLAVICLE },
	{ "bip01 r clavicle", SPINE1 }, { "bip01 r upperarm", RIGHT_CLAVICLE },
	{ "bip01 l thigh", PELVIS }, { "bip01 l calf", LEFT_THIGH }, { "bip01 l foot", LEFT_CALF },
	{ "bip01 r thigh", PELVIS }, { "bip01 r calf", RIGHT_THIGH }, { "bip01 r foot", RIGHT_CALF },
};
/* each node's place from the eye, in the body's frame (forward, left, up,
world units): the cyborg's as the game draws it, standing (its eye 0.62 over
the ground), crouching (0.35) and in a crouch jump's airborne pose (the eye
at its hips) */
static real const standing[NODES][3] =
{
	{ -0.008f, 0.f, -0.250f }, { -0.008f, 0.f, -0.212f }, { -0.009f, 0.f, -0.138f }, { -0.011f, 0.f, -0.052f },
	{ -0.011f, 0.f, -0.035f }, { 0.016f, 0.055f, -0.075f }, { 0.030f, 0.082f, -0.075f },
	{ -0.037f, -0.052f, -0.075f }, { -0.051f, -0.079f, -0.075f },
	{ 0.016f, 0.035f, -0.250f }, { 0.097f, 0.028f, -0.401f }, { 0.116f, 0.064f, -0.552f },
	{ -0.032f, -0.035f, -0.250f }, { -0.066f, -0.090f, -0.409f }, { -0.116f, -0.047f, -0.551f },
};
static real const crouching[NODES][3] =
{
	{ -0.051f, 0.032f, -0.167f }, { -0.052f, 0.032f, -0.130f }, { -0.010f, -0.001f, -0.079f },
	{ 0.050f, -0.049f, -0.039f }, { 0.062f, -0.058f, -0.032f }, { 0.071f, 0.011f, -0.048f },
	{ 0.085f, 0.040f, -0.050f }, { 0.020f, -0.090f, -0.050f }, { 0.010f, -0.110f, -0.055f },
	{ -0.023f, 0.064f, -0.165f }, { 0.125f, -0.024f, -0.156f }, { 0.108f, 0.062f, -0.286f },
	{ -0.079f, 0.f, -0.169f }, { -0.072f, -0.168f, -0.204f }, { -0.115f, -0.080f, -0.288f },
};
static real const crouch_jump[NODES][3] =
{
	{ -0.002f, 0.001f, 0.024f }, { 0.011f, 0.001f, 0.059f }, { 0.042f, 0.f, 0.126f }, { 0.084f, -0.002f, 0.201f },
	{ 0.093f, -0.002f, 0.216f }, { 0.084f, 0.057f, 0.177f }, { 0.090f, 0.080f, 0.170f },
	{ 0.084f, -0.057f, 0.177f }, { 0.090f, -0.080f, 0.170f },
	{ -0.004f, 0.043f, 0.017f }, { 0.056f, 0.039f, -0.144f }, { 0.027f, 0.034f, -0.298f },
	{ -0.001f, -0.040f, 0.031f }, { 0.101f, -0.049f, -0.106f }, { 0.040f, -0.040f, -0.250f },
};

static real_point3d const test_eye = { 12.5f, -40.25f, 3.0f };
static real_matrix4x3 game_pose[NODES];

/* the model's surface at rest: the front of the hips and of the belly, and
farther forward the knee's and the chest's, which are not the hips' */
static struct object_mesh_vertex test_vertices[] =
{
	{ { 0.040f, 0.0f, 0.40f }, { PELVIS, PELVIS }, 32767 },
	{ { 0.061f, 0.01f, 0.46f }, { SPINE, PELVIS }, 30000 },
	{ { 0.030f, -0.05f, 0.30f }, { LEFT_THIGH, PELVIS }, 20000 },
	{ { 0.150f, 0.05f, 0.20f }, { LEFT_CALF, LEFT_THIGH }, 32767 },
	{ { 0.230f, 0.0f, 0.55f }, { SPINE1, SPINE }, 32767 },
};
#define TEST_FRONT 0.061f

/* the game's pose of a body facing yaw (radians), each node where table puts
it from the eye */
static void body(real const table[NODES][3], real yaw)
{
	real c = cosf(yaw), s = sinf(yaw);
	short index;

	test_model.nodes.count = NODES;
	test_model.nodes.elements = nodes_named;
	test_mesh.vertex_count = sizeof(test_vertices) / sizeof(test_vertices[0]);
	test_mesh.vertices = test_vertices;
	test_definition.object.model.index = 1;
	test_unit.object.parent_object_index = NONE;
	test_unit.object.forward.i = c;
	test_unit.object.forward.j = s;
	test_unit.object.forward.k = 0.f;
	test_unit.object.node_matrices.size = NODES * (long)sizeof(real_matrix4x3);
	for (index = 0; index < NODES; index++)
	{
		real_matrix4x3 *m = &game_pose[index];

		memset(m, 0, sizeof(*m));
		m->scale = 1.f;
		m->forward.i = c;
		m->forward.j = s;
		m->left.i = -s;
		m->left.j = c;
		m->up.k = 1.f;
		m->position.x = test_eye.x + table[index][0] * c - table[index][1] * s;
		m->position.y = test_eye.y + table[index][0] * s + table[index][1] * c;
		m->position.z = test_eye.z + table[index][2];
	}
}

/* the eye looking pitched (degrees, up positive) and turned (degrees) */
static void look(double pitch, double yaw)
{
	double p = pitch * _pi / 180.0, y = yaw * _pi / 180.0;

	global_window_parameters.camera.position = test_eye;
	global_window_parameters.camera.forward.i = (real)(cos(p) * cos(y));
	global_window_parameters.camera.forward.j = (real)(cos(p) * sin(y));
	global_window_parameters.camera.forward.k = (real)sin(p);
}

static real distance(real_point3d a, real_point3d b)
{
	return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

static boolean above_chest(short node)
{
	for (; node != NONE; node = nodes_named[node].parent_node_index)
	{
		if (node == SPINE1)
			return TRUE;
	}
	return FALSE;
}

/* the pose drawn: every node not above the chest drawn, where it is clear of
the eye; those above folded into the chest's joint; all of it the game's
moved whole (by moved, its distance from the game's); TRUE: lowered */
static void check_drawn(real_matrix4x3 const *pose, char const *what, real_vector3d *moved)
{
	short a, b;

	CHECK(pose != NULL, "%s: no pose", what);
	moved->i = pose[PELVIS].position.x - game_pose[PELVIS].position.x;
	moved->j = pose[PELVIS].position.y - game_pose[PELVIS].position.y;
	moved->k = pose[PELVIS].position.z - game_pose[PELVIS].position.z;
	for (a = 0; a < NODES; a++)
	{
		if (above_chest(a))
		{
			CHECK(pose[a].scale < 0.01f && distance(pose[a].position, pose[SPINE1].position) < 1.0e-6f,
				"%s: node %d not folded into the chest's joint", what, a);
			continue;
		}
		CHECK(pose[a].scale == 1.f, "%s: node %d scaled %g", what, a, pose[a].scale);
		/* (the belly flattened onto its joint's height, the rest as it was) */
		CHECK(fabsf(pose[a].up.k - game_pose[a].up.k * (a == SPINE ? FIRST_PERSON_LEGS_LID : 1.f)) < 1.0e-5f,
			"%s: node %d flattened %g", what, a, pose[a].up.k);
		/* (moved whole: as the hips are) */
		CHECK(fabsf(pose[a].position.x - game_pose[a].position.x - moved->i) < 1.0e-5f &&
			fabsf(pose[a].position.y - game_pose[a].position.y - moved->j) < 1.0e-5f &&
			fabsf(pose[a].position.z - game_pose[a].position.z - moved->k) < 1.0e-5f,
			"%s: node %d not moved as the hips are", what, a);
		for (b = 0; b < a; b++)
		{
			if (!above_chest(b))
			{
				CHECK(fabsf(distance(pose[a].position, pose[b].position) -
					distance(game_pose[a].position, game_pose[b].position)) < 1.0e-5f,
					"%s: nodes %d and %d stretched", what, a, b);
			}
		}
		/* (the eye clear: what is drawn is below the eye and farther than
		the near plane) */
		CHECK(distance(pose[a].position, test_eye) > GAME_NEAR && pose[a].position.z < test_eye.z - GAME_NEAR,
			"%s: node %d %.3f from the eye, %.3f below it", what, a, distance(pose[a].position, test_eye),
			test_eye.z - pose[a].position.z);
	}
	/* (the chest's joint, where the body is closed, too) */
	CHECK(test_eye.z - pose[SPINE1].position.z > GAME_NEAR, "%s: the chest's joint %.3f below the eye", what,
		test_eye.z - pose[SPINE1].position.z);
}

/* whether a pose is another turned by angle (radians) about the eye's
vertical */
static boolean same_pose(real_matrix4x3 const *pose, real_matrix4x3 const *other, real angle)
{
	real c = cosf(angle), s = sinf(angle);
	short node;

	for (node = 0; node < NODES; node++)
	{
		real x = other[node].position.x - test_eye.x, y = other[node].position.y - test_eye.y;
		real_vector3d const *a[3] = { &pose[node].forward, &pose[node].left, &pose[node].up };
		real_vector3d const *b[3] = { &other[node].forward, &other[node].left, &other[node].up };
		short axis;

		if (fabsf(pose[node].position.x - test_eye.x - (c * x - s * y)) > 1.0e-4f ||
			fabsf(pose[node].position.y - test_eye.y - (s * x + c * y)) > 1.0e-4f ||
			fabsf(pose[node].position.z - other[node].position.z) > 1.0e-4f ||
			fabsf(pose[node].scale - other[node].scale) > 1.0e-5f)
		{
			return FALSE;
		}
		for (axis = 0; axis < 3; axis++)
		{
			if (fabsf(a[axis]->i - (c * b[axis]->i - s * b[axis]->j)) > 1.0e-4f ||
				fabsf(a[axis]->j - (s * b[axis]->i + c * b[axis]->j)) > 1.0e-4f ||
				fabsf(a[axis]->k - b[axis]->k) > 1.0e-4f)
			{
				return FALSE;
			}
		}
	}
	return TRUE;
}

int main(int argc, char **argv)
{
	char const *which = argc > 1 ? argv[1] : "";
	real_matrix4x3 drawn[NODES];
	real_vector3d moved;
	int turn, pitch;

	if (!strcmp(which, "planted") || !strcmp(which, "stateless"))
	{
		/* standing still, facing 30 degrees, the view turned about and
		tilted from level to straight down: the pose the same every time,
		set back along the body's heading */
		double yaw = 30.0;

		body(standing, (real)(yaw * _pi / 180.0));
		look(0, yaw);
		memcpy(drawn, first_person_legs_node_matrices(0, game_pose), sizeof(drawn));
		check_drawn(drawn, "level", &moved);
		{
			/* (the eye over the front of the hips and belly at rest, along
			the heading: the hips' rest place, the belly's front) */
			real c = (real)cos(yaw * _pi / 180.0), s = (real)sin(yaw * _pi / 180.0);
			real y = nodes_named[PELVIS].default_translation.y;

			CHECK(fabsf(moved.i + c * TEST_FRONT - s * y) < 1.0e-5f && fabsf(moved.j + s * TEST_FRONT + c * y) < 1.0e-5f &&
				fabsf(moved.k) < 1.0e-6f, "moved %g %g %g, not to the front of the hips along the heading", moved.i,
				moved.j, moved.k);
		}
		for (turn = -60; turn <= 60; turn += 20)
		{
			for (pitch = 0; pitch >= -89; pitch -= 7)
			{
				real_matrix4x3 const *pose;

				look(pitch, yaw + turn);
				pose = first_person_legs_node_matrices(0, game_pose);
				CHECK(pose && !memcmp(pose, drawn, sizeof(drawn)), "turned %d, pitched %d: the pose moved", turn,
					pitch);
				/* (and seen from the view drawn, turned there and back to
				the lower body's heading, where it stands) */
				test_view_drawn = TRUE;
				test_lower.yaw = (real)(yaw * _pi / 180.0);
				pose = first_person_legs_node_matrices(0, game_pose);
				test_view_drawn = FALSE;
				CHECK(pose && same_pose(pose, drawn, 0.f), "seen from the view drawn turned %d, pitched %d: the pose moved",
					turn, pitch);
			}
		}
		if (!strcmp(which, "stateless"))
		{
			/* (after a crouch jump's pose and back) */
			body(crouch_jump, (real)(yaw * _pi / 180.0));
			first_person_legs_node_matrices(0, game_pose);
			body(standing, (real)(yaw * _pi / 180.0));
			look(-80, yaw);
			CHECK(!memcmp(first_person_legs_node_matrices(0, game_pose), drawn, sizeof(drawn)),
				"the pose not the same drawn again");
		}
	}
	else if (!strcmp(which, "turned"))
	{
		/* the lower body turned 40 degrees from the body's heading: the pose
		turned as much about the eye */
		double yaw = 30.0;

		body(standing, (real)(yaw * _pi / 180.0));
		look(-70, yaw);
		memcpy(drawn, first_person_legs_node_matrices(0, game_pose), sizeof(drawn));
		for (turn = -60; turn <= 60; turn += 30)
		{
			look(-70, yaw + turn);
			test_view_drawn = TRUE;
			test_lower.yaw = (real)((yaw + 40.0) * _pi / 180.0);
			CHECK(same_pose(first_person_legs_node_matrices(0, game_pose), drawn, (real)(40.0 * _pi / 180.0)),
				"the view turned %d: not the pose turned 40 degrees about the eye", turn);
			test_view_drawn = FALSE;
		}
	}
	else if (!strcmp(which, "clear"))
	{
		/* standing and crouching on the ground: never lowered */
		for (turn = 0; turn < 360; turn += 45)
		{
			body(standing, (real)(turn * _pi / 180.0));
			look(-80, turn);
			check_drawn(first_person_legs_node_matrices(0, game_pose), "standing", &moved);
			CHECK(fabsf(moved.k) < 1.0e-6f, "standing: lowered %g", moved.k);
			body(crouching, (real)(turn * _pi / 180.0));
			check_drawn(first_person_legs_node_matrices(0, game_pose), "crouching", &moved);
			CHECK(fabsf(moved.k) < 1.0e-6f, "crouching: lowered %g", moved.k);
		}
	}
	else if (!strcmp(which, "crouch_jump"))
	{
		/* the eye at the hips: lowered as far as keeps the head as far
		below the eye as the ground's poses have it */
		body(crouch_jump, 0.5f);
		look(-70, 0.5 * 180.0 / _pi);
		check_drawn(first_person_legs_node_matrices(0, game_pose), "crouch jump", &moved);
		CHECK(fabsf(game_pose[HEAD].position.z + moved.k - (test_eye.z - FIRST_PERSON_LEGS_HEAD_BELOW)) < 1.0e-5f,
			"crouch jump: the head %.3f below the eye", test_eye.z - game_pose[HEAD].position.z - moved.k);
	}
	else if (!strcmp(which, "shadow"))
	{
		/* the shadow's pose: the legs' placing, nothing folded */
		real_matrix4x3 const *shadow;
		short node;

		body(crouch_jump, 1.0f);
		look(-60, 1.0 * 180.0 / _pi);
		memcpy(drawn, first_person_legs_node_matrices(0, game_pose), sizeof(drawn));
		shadow = first_person_legs_shadow_node_matrices(0, game_pose);
		CHECK(shadow != NULL, "no shadow");
		for (node = 0; node < NODES; node++)
		{
			CHECK(shadow[node].scale == 1.f, "the shadow's node %d folded", node);
			if (!above_chest(node) && node != SPINE)
			{
				CHECK(!memcmp(&shadow[node], &drawn[node], sizeof(drawn[node])),
					"the shadow's node %d not where the legs' is", node);
			}
		}
		/* (none in a seat) */
		test_unit.object.parent_object_index = 3;
		CHECK(first_person_legs_shadow_node_matrices(0, game_pose) == NULL, "a shadow in a seat");
	}
	else
	{
		fprintf(stderr, "no case %s\n", which);
		return 2;
	}
	return 0;
}
