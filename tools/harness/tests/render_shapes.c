#include "harness.h"
#include <math.h>

/* A fake world for what is drawn from the simulation between the ticks other than an object's nodes: a widget's
shape (render_interpolation.c's shapes: a cloth flag stepped in the tick by flags.c's flags_tick, drawn between
the ticks by flag_render, hung from its marker as drawn), and a point worked out from an object's state
(render_interpolation_unit_camera_position, render_interpolation_object_origin). A flag hangs from a pole whose
marker the tick has at one place and turning, and a frame draws at another; a unit stands, or sits in a vehicle,
whose nodes a frame draws a fixed offset from where the tick left them. */

typedef struct { real_vector3d forward, left, up; real_point3d position; real scale; } real_matrix4x3;
struct object_marker { short node_index; real_matrix4x3 node_matrix; real_matrix4x3 matrix; };
struct tag_reference { long group_tag; long index; };
struct tag_block { long count; void *address; void *definition; };
struct render_lighting;
struct render_animation;

static real_vector3d const forward3d = { 1.0f, 0.0f, 0.0f };
static real_vector3d const left3d = { 0.0f, 1.0f, 0.0f };
static real_vector3d const up3d = { 0.0f, 0.0f, 1.0f };
#define global_forward3d (&forward3d)
#define global_left3d (&left3d)
#define global_up3d (&up3d)
#define TAG_BLOCK_GET_ELEMENT(block, element, type) (&((type *)(block)->address)[(element)])
#define csmemset memset

/* the frames' clock (render_interpolation.c's) */
static long interpolation_tick;
static boolean interpolation_rendering;
static real interpolation_fraction = 1.0f;

#include "decls.inc"

static struct interpolated_shape interpolated_shapes[MAXIMUM_INTERPOLATED_SHAPES];

/* ---------- the world */

/* the flag's pole: its marker as the tick has it, and as the frame draws it */
static real_matrix4x3 tick_marker, drawn_marker;
/* a unit (on foot, or seated in a vehicle) and its vehicle: where the tick left each's root and where a frame
draws it, the eye 0.6 above a unit on foot's origin, a seat's camera marker 1.0 above the vehicle's */
#define UNIT 0x00010001L
#define VEHICLE 0x00020002L
#define EYE 0.6f
#define SEAT 1.0f
static boolean seated;
static real_point3d unit_tick, unit_drawn, vehicle_tick, vehicle_drawn;

static real_point3d point(real x, real y, real z)
{
	real_point3d p = { x, y, z };

	return p;
}

static real_matrix4x3 matrix_at(real_point3d position, real yaw)
{
	real_matrix4x3 m;

	m.forward.i = cosf(yaw); m.forward.j = sinf(yaw); m.forward.k = 0.0f;
	m.left.i = -sinf(yaw); m.left.j = cosf(yaw); m.left.k = 0.0f;
	m.up = up3d;
	m.position = position;
	m.scale = 1.0f;
	return m;
}

static real distance(real_point3d const *a, real_point3d const *b)
{
	return sqrtf((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y) + (a->z - b->z) * (a->z - b->z));
}

/* the point seen from a marker (its place and axes) */
static real_point3d seen_from(real_matrix4x3 const *m, real_point3d const *p)
{
	real_vector3d d = { p->x - m->position.x, p->y - m->position.y, p->z - m->position.z };

	return point(d.i * m->forward.i + d.j * m->forward.j + d.k * m->forward.k,
		d.i * m->left.i + d.j * m->left.j + d.k * m->left.k,
		d.i * m->up.i + d.j * m->up.j + d.k * m->up.k);
}

static real_point3d placed_by(real_matrix4x3 const *m, real_point3d const *p)
{
	return point(m->position.x + m->forward.i * p->x + m->left.i * p->y + m->up.i * p->z,
		m->position.y + m->forward.j * p->x + m->left.j * p->y + m->up.j * p->z,
		m->position.z + m->forward.k * p->x + m->left.k * p->y + m->up.k * p->z);
}

/* as objects.c's: the nodes and markers a frame draws, between the ticks; the tick's otherwise */
static void object_get_marker_by_name(long object_index, char const *name, struct object_marker *marker, short count)
{
	(void)object_index; (void)name; (void)count;
	memset(marker, 0, sizeof(*marker));
	marker->matrix = interpolation_rendering ? drawn_marker : tick_marker;
}

/* the unit's camera as units.c has it: on foot its origin, the tick's (object.position), and its eye above it;
seated its seat's marker on the vehicle, as drawn in a frame (object_get_node_matrices) */
static void unit_get_camera_position(long unit_index, real_point3d *position)
{
	CHECK(unit_index == UNIT, "no unit %lx", unit_index);
	if (seated)
	{
		*position = interpolation_rendering ? vehicle_drawn : vehicle_tick;
		position->z += SEAT;
	}
	else
	{
		*position = unit_tick;
		position->z += EYE;
	}
}

/* as objects.c's: the origin of one carried is its parent's node's, drawn in a frame */
static real_point3d *object_get_origin(long object_index, real_point3d *origin)
{
	CHECK(object_index == UNIT, "no object %lx", object_index);
	if (seated)
		*origin = interpolation_rendering ? vehicle_drawn : vehicle_tick;
	else
		*origin = unit_tick;
	return origin;
}

/* the window's player, and its own view drawn from the newest step on foot, carried on from where that step left
it by so much (render_interpolation.c's) */
#define MAXIMUM_LOCAL_PLAYERS 4
static struct { short local_player_index; } render;
static boolean newest_carried_valid[MAXIMUM_LOCAL_PLAYERS];
static real_vector3d newest_carried[MAXIMUM_LOCAL_PLAYERS];
static long player_control_get_unit_index(short local_player_index) { return local_player_index == 0 ? UNIT : NONE; }

/* the point moved with the object's root from where the tick left it to where it is drawn */
static void render_interpolation_object_point(long object_index, real_point3d *p)
{
	real_point3d from = seated ? vehicle_tick : unit_tick;
	real_point3d to = seated ? vehicle_drawn : unit_drawn;

	CHECK(object_index == UNIT, "no object %lx", object_index);
	if (!interpolation_rendering)
		return;
	p->x += to.x - from.x;
	p->y += to.y - from.y;
	p->z += to.z - from.z;
}

/* ---------- the flag (flags.c's own, but for its cloth's step and its drawing) */

static struct data_array *flag_data;
static long flag_steps;
static real_point3d drawn_corner;
static boolean drawn_at_all;
static real_point3d tick_shape_corner; /* the corner, seen from the marker, the next step leaves */

#define flag_get(index) ((struct flag_datum_prefix *)datum_get(flag_data, (index)))
static struct flag_definition the_definition;
static struct flag_attachment_point the_points[2];
#define flag_definition_get(index) ((void)(index), &the_definition)

static long data_next_index(struct data_array *array, long index)
{
	long absolute_index = index == NONE ? 0 : DATUM_INDEX_TO_ABSOLUTE_INDEX(index) + 1;

	for (; absolute_index < array->maximum_count; absolute_index++)
	{
		struct datum_header *header = (struct datum_header *)(array->data + absolute_index * array->size);

		if (header->identifier)
			return ((long)(unsigned short)header->identifier << 16) | absolute_index;
	}
	return NONE;
}

static void object_get(long object_index) { CHECK(object_index != NONE, "no object"); }

/* a step of the cloth: every vertex hangs from the pole's marker as the tick has it, its corner where the step
puts it */
static void flag_update(struct flag_datum_prefix *flag, struct flag_definition *definition, real time)
{
	short count = (short)(definition->width * definition->height);
	short index;

	(void)time;
	flag_steps++;
	for (index = 0; index < count; index++)
	{
		real_point3d local = point(0.1f * index, 0.0f, -0.05f * index);

		if (index == count - 1)
			local = tick_shape_corner;
		flag->vertices[index].position = placed_by(&tick_marker, &local);
	}
}

static void flag_render_proper(struct flag_datum_prefix *flag, struct flag_definition *definition,
	struct render_lighting const *lighting, struct render_animation const *animation)
{
	(void)lighting; (void)animation;
	drawn_corner = flag->vertices[definition->width * definition->height - 1].position;
	drawn_at_all = TRUE;
}

/* (no feed of the HUD's Spartan link is drawn: spartan_link.c) */
static boolean spartan_link_drawing(void)
{
	return FALSE;
}

#include "under_test.inc"

static real_point3d lerp_point(real_point3d a, real_point3d b, real t)
{
	return point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

/* a tick: its work (game.c: the cloth stepped, its shape left), its sub-steps, its end */
static void tick(real_point3d corner, real_matrix4x3 marker, boolean ends)
{
	tick_marker = marker;
	tick_shape_corner = corner;
	flags_tick(1.0f / 30.0f);
	if (ends)
		interpolation_tick++;
}

static void frame(real fraction)
{
	interpolation_rendering = TRUE;
	interpolation_fraction = fraction;
	drawn_at_all = FALSE;
}

static long a_flag(void)
{
	long flag_index;
	struct flag_datum_prefix *flag;

	flag_data = game_state_data_new("flag", 2, sizeof(struct flag_datum_prefix));
	the_definition.width = 4;
	the_definition.height = 3;
	the_definition.attachment_points.count = 2;
	the_definition.attachment_points.address = the_points;
	flag_index = datum_new(flag_data);
	flag = flag_get(flag_index);
	flag->definition_index = 0;
	flag->object_index = NONE;
	return flag_index;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real_matrix4x3 pole_a = matrix_at(point(10.0f, 0.0f, 1.0f), 0.0f);
	real_matrix4x3 pole_b = matrix_at(point(10.3f, 0.1f, 1.0f), 0.2f);
	real_point3d corner_a = point(0.8f, 0.0f, -0.4f);
	real_point3d corner_b = point(0.6f, 0.3f, -0.5f);
	real_point3d corner_c = point(-0.5f, -0.6f, 0.2f);

	/* the frames between the ticks don't step the cloth (game_frame's widgets_update counts the frames it goes
	undrawn); the tick does, once */
	CASE("frames_dont_step")
	{
		long flag_index = a_flag();
		short f;

		flag_get(flag_index)->object_index = 7;
		for (f = 0; f < 4; f++)
		{
			flag_get(flag_index)->updates_since_last_render = 0;
			flags_update(1.0f / 120.0f);
		}
		CHECK(flag_steps == 0, "the frames stepped the cloth %ld times", flag_steps);
		flags_tick(1.0f / 30.0f);
		CHECK(flag_steps == 1, "a tick stepped it %ld times", flag_steps);
		return 0;
	}

	/* a frame draws the cloth between the last two ticks' shapes, as seen from the pole's marker at each, hung
	from the marker as the frame draws it */
	CASE("cloth_between_ticks")
	{
		long flag_index = a_flag();
		real_matrix4x3 drawn = matrix_at(point(12.0f, 3.0f, 2.0f), 1.0f);
		real_point3d expected;

		flag_get(flag_index)->object_index = 7;
		flag_get(flag_index)->initialized = TRUE;
		tick(corner_a, pole_a, TRUE);
		tick(corner_b, pole_b, TRUE);
		frame(0.25f);
		drawn_marker = drawn;
		flag_render(7, flag_index, NULL, NULL);
		expected = lerp_point(corner_a, corner_b, 0.25f);
		expected = placed_by(&drawn, &expected);
		CHECK(drawn_at_all && distance(&drawn_corner, &expected) < 1e-4f,
			"drawn at (%f %f %f), not (%f %f %f)", drawn_corner.x, drawn_corner.y, drawn_corner.z,
			expected.x, expected.y, expected.z);
		/* (and through the whole tick: no step at its end) */
		frame(0.999f);
		flag_render(7, flag_index, NULL, NULL);
		expected = placed_by(&drawn, &corner_b);
		CHECK(distance(&drawn_corner, &expected) < 1e-2f, "the tick's end drawn %f from its shape",
			distance(&drawn_corner, &expected));
		return 0;
	}

	/* at 60 and 120 a tick's work runs at its first sub-step: the shape it leaves is the open tick's, drawn once
	the frames reach it, not before */
	CASE("open_tick_waits")
	{
		long flag_index = a_flag();
		real_point3d expected;

		flag_get(flag_index)->object_index = 7;
		flag_get(flag_index)->initialized = TRUE;
		tick(corner_a, pole_a, TRUE);
		tick(corner_b, pole_a, TRUE);
		tick(corner_c, pole_a, FALSE);
		frame(0.5f);
		drawn_marker = pole_a;
		flag_render(7, flag_index, NULL, NULL);
		expected = lerp_point(corner_a, corner_b, 0.5f);
		expected = placed_by(&pole_a, &expected);
		CHECK(distance(&drawn_corner, &expected) < 1e-4f, "the open tick's shape drawn early (%f off)",
			distance(&drawn_corner, &expected));
		/* the tick ends: the frames now draw from b to c */
		interpolation_tick++;
		frame(0.5f);
		flag_render(7, flag_index, NULL, NULL);
		expected = lerp_point(corner_b, corner_c, 0.5f);
		expected = placed_by(&pole_a, &expected);
		CHECK(distance(&drawn_corner, &expected) < 1e-4f, "the next tick drawn %f off",
			distance(&drawn_corner, &expected));
		return 0;
	}

	/* the cloth turns with its pole: what the tick left beside the pole stays beside it as the pole is drawn
	turned, not where the world's axes put it */
	CASE("turns_with_pole")
	{
		long flag_index = a_flag();
		real_matrix4x3 turned = matrix_at(pole_a.position, 1.5707964f);
		real_point3d expected;

		flag_get(flag_index)->object_index = 7;
		flag_get(flag_index)->initialized = TRUE;
		tick(corner_a, pole_a, TRUE);
		tick(corner_a, pole_a, TRUE);
		frame(0.5f);
		drawn_marker = turned;
		flag_render(7, flag_index, NULL, NULL);
		expected = placed_by(&turned, &corner_a);
		CHECK(distance(&drawn_corner, &expected) < 1e-4f, "drawn %f from the turned pole's cloth",
			distance(&drawn_corner, &expected));
		return 0;
	}

	/* after a gap (not drawn, made again) only the newest is drawn, and with none what the cloth is */
	CASE("gap")
	{
		long flag_index = a_flag();
		real_point3d expected;

		flag_get(flag_index)->object_index = 7;
		flag_get(flag_index)->initialized = TRUE;
		tick(corner_a, pole_a, TRUE);
		interpolation_tick += 3;
		tick(corner_b, pole_a, TRUE);
		frame(0.5f);
		drawn_marker = pole_b;
		flag_render(7, flag_index, NULL, NULL);
		expected = placed_by(&pole_b, &corner_b);
		CHECK(distance(&drawn_corner, &expected) < 1e-4f, "after a gap drawn %f off", distance(&drawn_corner, &expected));
		render_interpolation_shape_forget(0x666C6167, flag_index);
		flag_render(7, flag_index, NULL, NULL);
		expected = placed_by(&pole_a, &corner_b);
		CHECK(distance(&drawn_corner, &expected) < 1e-4f, "with none, not the cloth as it is (%f off)",
			distance(&drawn_corner, &expected));
		/* and outside a frame, as it is */
		interpolation_rendering = FALSE;
		CHECK(!render_interpolation_shape(0x666C6167, flag_index, &pole_b, 12, &expected), "a shape outside a frame");
		return 0;
	}

	/* a unit's camera where it is drawn, on foot (the tick's origin, moved) and seated (the seat's marker, drawn:
	not moved again) */
	CASE("camera_on_foot")
	{
		real_point3d eye;

		seated = FALSE;
		unit_tick = point(1.0f, 2.0f, 0.0f);
		unit_drawn = point(1.5f, 2.25f, 0.1f);
		interpolation_rendering = TRUE;
		render_interpolation_unit_camera_position(UNIT, &eye);
		CHECK(distance(&eye, &(real_point3d){ 1.5f, 2.25f, 0.1f + EYE }) < 1e-5f, "on foot at (%f %f %f)",
			eye.x, eye.y, eye.z);
		CHECK(interpolation_rendering, "the frame left");
		return 0;
	}
	CASE("camera_seated")
	{
		real_point3d eye;
		real_point3d origin;

		seated = TRUE;
		vehicle_tick = point(5.0f, 0.0f, 0.0f);
		vehicle_drawn = point(6.0f, 0.5f, 0.0f);
		interpolation_rendering = TRUE;
		render_interpolation_unit_camera_position(UNIT, &eye);
		CHECK(distance(&eye, &(real_point3d){ 6.0f, 0.5f, SEAT }) < 1e-5f, "seated at (%f %f %f)", eye.x, eye.y, eye.z);
		render_interpolation_object_origin(UNIT, &origin);
		CHECK(distance(&origin, &vehicle_drawn) < 1e-5f, "carried origin at (%f %f %f)", origin.x, origin.y, origin.z);
		interpolation_rendering = FALSE;
		render_interpolation_unit_camera_position(UNIT, &eye);
		CHECK(distance(&eye, &(real_point3d){ 5.0f, 0.0f, SEAT }) < 1e-5f, "in the tick, not the tick's");
		return 0;
	}

	/* the window's own player on foot, its view drawn from the newest step (display.direct_camera): the unit's
	camera where that view is, the newest step's carried on as the view is, not with the body a tick behind; any
	other window's, and outside a frame, as before */
	CASE("camera_view_newest")
	{
		real_point3d eye;

		seated = FALSE;
		unit_tick = point(1.0f, 2.0f, 0.0f);
		unit_drawn = point(0.6f, 1.8f, 0.0f);
		newest_carried_valid[0] = TRUE;
		newest_carried[0].i = 0.05f;
		newest_carried[0].j = 0.02f;
		newest_carried[0].k = 0.01f;
		interpolation_rendering = TRUE;
		render.local_player_index = 0;
		render_interpolation_unit_camera_position(UNIT, &eye);
		CHECK(distance(&eye, &(real_point3d){ 1.05f, 2.02f, EYE + 0.01f }) < 1e-5f, "at (%f %f %f), not the view's",
			eye.x, eye.y, eye.z);
		render.local_player_index = 1;
		render_interpolation_unit_camera_position(UNIT, &eye);
		CHECK(distance(&eye, &(real_point3d){ 0.6f, 1.8f, EYE }) < 1e-5f, "another window's at (%f %f %f)",
			eye.x, eye.y, eye.z);
		render.local_player_index = 0;
		interpolation_rendering = FALSE;
		render_interpolation_unit_camera_position(UNIT, &eye);
		CHECK(distance(&eye, &(real_point3d){ 1.0f, 2.0f, EYE }) < 1e-5f, "outside a frame at (%f %f %f)",
			eye.x, eye.y, eye.z);
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
