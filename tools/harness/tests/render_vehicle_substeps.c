#include "harness.h"
#include <math.h>

/* A fake world of a vehicle a player drives (three nodes: its root, a wheel, a turret that aims once a tick), the
player's unit in its seat (two nodes) and the turret's gun carried on it; the clock's sub-steps (k a tick) run one at
a time, each moving and turning the vehicle along a curve (swerving, pitching, rolling) and posing what it carries
where it is (vehicles.c's vehicle_substep_pose), keeping the poses (the fast phase's render_interpolation slot), the
tick's last ending it (render_interpolation_tick); frames drawn between them as the clock has it
(game_time_get_tick_fraction: (completed sub-steps + the part of one run since) / k). The player's view hangs on the
vehicle at the unit's camera position, in the first person or following it (a constant swing about that point). */

typedef struct { real red, green, blue; } real_rgb_color;
typedef struct { real_vector3d forward, left, up; real_point3d position; real scale; } real_matrix4x3;
struct observer_result { real_point3d position; real_vector3d forward, up; real field_of_view; };

#define NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS 4
#define NUMBER_OF_OBJECT_CHANGE_COLORS 4
#define MAXIMUM_OBJECTS_PER_MAP 16
#define MAXIMUM_LOCAL_PLAYERS 1
#include "decls.inc"

static real_vector3d const zero3d;
#define global_zero_vector3d (&zero3d)

enum { _object_type_biped, _object_type_vehicle, _object_type_weapon, _object_type_projectile };
#define _object_mask_biped FLAG(_object_type_biped)
#define _object_mask_vehicle FLAG(_object_type_vehicle)
#define _object_mask_unit (FLAG(_object_type_biped) | FLAG(_object_type_vehicle))
#define _object_mask_item FLAG(_object_type_weapon)
#define _object_mask_projectile FLAG(_object_type_projectile)
#define _object_mask_all 0xFF
enum { _object_lane_macro, _object_lane_micro };
enum { _director_perspective_first_person, _director_perspective_third_person, _director_perspective_scripted };

struct _object
{
	struct { short size; } node_matrices;
	real_point3d position;
	real_vector3d translational_velocity;
	long parent_object_index;
	real outgoing_function_values[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	real_rgb_color outgoing_change_colors[NUMBER_OF_OBJECT_CHANGE_COLORS];
	short type;
};
struct object_datum { long definition_index; struct _object object; };
struct unit_datum { long definition_index; struct _object object; struct { real active_camouflage, active_camouflage_super_amount; } unit; };

#define MAXIMUM_NODES 3
#define VEHICLE 0x00010001L
#define RIDER 0x00020002L
#define GUN 0x00030003L

static struct unit_datum objects[MAXIMUM_OBJECTS_PER_MAP];
static boolean exists[MAXIMUM_OBJECTS_PER_MAP];
static long identifiers[MAXIMUM_OBJECTS_PER_MAP];
static real_matrix4x3 world_nodes[MAXIMUM_OBJECTS_PER_MAP][MAXIMUM_NODES];

static short fake_substep_count = 1;
static real fake_fraction = 1.0f;
static sim_substep_procedure slot_procedure;
static boolean vehicle_micro = TRUE;
static short perspective = _director_perspective_first_person;

static long absolute(long index) { return DATUM_INDEX_TO_ABSOLUTE_INDEX(index); }
static boolean valid(long index)
{
	return index != NONE && absolute(index) < MAXIMUM_OBJECTS_PER_MAP && exists[absolute(index)] &&
		identifiers[absolute(index)] == index;
}
static struct object_datum *object_get(long index)
{
	CHECK(valid(index), "no object %lx", index);
	return (struct object_datum *)&objects[absolute(index)];
}
static struct object_datum *object_try_and_get(long index) { return valid(index) ? object_get(index) : NULL; }
static void *object_try_and_get_and_verify_type(long index, unsigned long mask)
{
	return valid(index) && TEST_FLAG(mask, object_get(index)->object.type) ? object_get(index) : NULL;
}
static real_matrix4x3 *object_get_node_matrices(long index) { return world_nodes[absolute(index)]; }
#define object_header_block_get(index, block) ((void *)world_nodes[absolute(index)])
static long object_get_ultimate_parent(long index)
{
	while (object_get(index)->object.parent_object_index != NONE)
		index = object_get(index)->object.parent_object_index;
	return index;
}
static short object_get_lane(long index)
{
	(void)index;
	return vehicle_micro ? _object_lane_micro : _object_lane_macro;
}
struct object_iterator { long index; long next; unsigned long mask; };
static void object_iterator_new(struct object_iterator *iterator, unsigned long mask, unsigned long flags)
{
	iterator->next = 0;
	iterator->mask = mask;
	iterator->index = NONE;
}
static void *object_iterator_next(struct object_iterator *iterator)
{
	while (iterator->next < MAXIMUM_OBJECTS_PER_MAP)
	{
		long at = iterator->next++;

		if (exists[at] && TEST_FLAG(iterator->mask, objects[at].object.type))
		{
			iterator->index = identifiers[at];
			return &objects[at];
		}
	}
	return NULL;
}
static void matrix4x3_inverse_transform_point(real_matrix4x3 const *m, real_point3d const *p, real_point3d *out)
{
	real x = p->x - m->position.x, y = p->y - m->position.y, z = p->z - m->position.z;

	out->x = (x * m->forward.i + y * m->forward.j + z * m->forward.k) / m->scale;
	out->y = (x * m->left.i + y * m->left.j + z * m->left.k) / m->scale;
	out->z = (x * m->up.i + y * m->up.j + z * m->up.k) / m->scale;
}
static int halo_interpolation_enabled(void) { return 1; }
static void sim_substep_register(short slot, sim_substep_procedure procedure)
{
	if (slot == _sim_substep_render_interpolation)
		slot_procedure = procedure;
}
static short sim_substep_count(void) { return fake_substep_count; }
static real game_time_get_tick_fraction(void) { return fake_fraction; }
static short director_get_perspective(short local_player_index) { return perspective; }
static long scripted_camera_object_relative_to(void) { return NONE; }
static boolean scripted_camera_object_is_first_person_camera(long index) { return FALSE; }
static boolean cinematic_in_progress(void) { return FALSE; }
/* the player's unit (NONE while dead) */
static long fake_player_unit = RIDER;
static long player_control_get_unit_index(short local_player_index) { return fake_player_unit; }
static void unit_get_camera_position(long index, real_point3d *position);
struct interpolation_ghost { boolean in_use; long tick; long step; short substep_count; };
static struct interpolation_ghost ghosts[64];
static void render_interpolation_ghosts_tick(void) {}

#include "under_test.inc"

/* ---------- the test's own rotations */

typedef struct { real w, i, j, k; } quat;

static quat q_axis(real x, real y, real z, real angle)
{
	quat q;
	real s = (real)sin(angle * 0.5);

	q.w = (real)cos(angle * 0.5);
	q.i = x * s;
	q.j = y * s;
	q.k = z * s;
	return q;
}

static quat q_mul(quat a, quat b)
{
	quat q;

	q.w = a.w * b.w - a.i * b.i - a.j * b.j - a.k * b.k;
	q.i = a.w * b.i + a.i * b.w + a.j * b.k - a.k * b.j;
	q.j = a.w * b.j - a.i * b.k + a.j * b.w + a.k * b.i;
	q.k = a.w * b.k + a.i * b.j - a.j * b.i + a.k * b.w;
	return q;
}

/* the shorter way round, normalised */
static quat q_nlerp(quat a, quat b, real t)
{
	quat q;
	real length;

	if (a.w * b.w + a.i * b.i + a.j * b.j + a.k * b.k < 0.0f)
	{
		b.w = -b.w;
		b.i = -b.i;
		b.j = -b.j;
		b.k = -b.k;
	}
	q.w = a.w + (b.w - a.w) * t;
	q.i = a.i + (b.i - a.i) * t;
	q.j = a.j + (b.j - a.j) * t;
	q.k = a.k + (b.k - a.k) * t;
	length = (real)sqrt(q.w * q.w + q.i * q.i + q.j * q.j + q.k * q.k);
	q.w /= length;
	q.i /= length;
	q.j /= length;
	q.k /= length;
	return q;
}

/* the quaternion's axes, as a matrix's columns (forward, left, up), at a place */
static real_matrix4x3 q_frame(quat q, real_point3d position)
{
	real_matrix4x3 m;

	m.forward.i = 1.0f - 2.0f * (q.j * q.j + q.k * q.k);
	m.forward.j = 2.0f * (q.i * q.j + q.w * q.k);
	m.forward.k = 2.0f * (q.i * q.k - q.w * q.j);
	m.left.i = 2.0f * (q.i * q.j - q.w * q.k);
	m.left.j = 1.0f - 2.0f * (q.i * q.i + q.k * q.k);
	m.left.k = 2.0f * (q.j * q.k + q.w * q.i);
	m.up.i = 2.0f * (q.i * q.k + q.w * q.j);
	m.up.j = 2.0f * (q.j * q.k - q.w * q.i);
	m.up.k = 1.0f - 2.0f * (q.i * q.i + q.j * q.j);
	m.position = position;
	m.scale = 1.0f;
	return m;
}

static real_point3d at(real x, real y, real z)
{
	real_point3d p;

	p.x = x;
	p.y = y;
	p.z = z;
	return p;
}

/* a point of the frame, in the world */
static real_point3d placed(real_matrix4x3 const *frame, real_point3d local)
{
	return at(frame->position.x + frame->forward.i * local.x + frame->left.i * local.y + frame->up.i * local.z,
		frame->position.y + frame->forward.j * local.x + frame->left.j * local.y + frame->up.j * local.z,
		frame->position.z + frame->forward.k * local.x + frame->left.k * local.y + frame->up.k * local.z);
}

/* a node (its rotation and place in the frame) in the world */
static real_matrix4x3 composed(real_matrix4x3 const *frame, quat frame_rotation, quat rotation, real_point3d local)
{
	return q_frame(q_mul(frame_rotation, rotation), placed(frame, local));
}

/* ---------- the world */

static real seconds_per_tick(void) { return 1.0f / 30.0f; }

/* the vehicle's curve, by the simulation's time: driving, swerving hard (about 0.1 a tick), pitching and rolling */
static real_point3d vehicle_place_at(real t)
{
	return at(5.0f * t, 2.0f * (real)sin(1.5 * t), 0.3f * (real)fabs(sin(2.0 * t)));
}

static quat vehicle_rotation_at(real t)
{
	real yaw = 2.5f * t + 0.8f * (real)sin(3.0 * t);
	real pitch = 0.2f * (real)sin(4.0 * t);
	real roll = 0.1f * (real)sin(5.0 * t);

	return q_mul(q_mul(q_axis(0, 0, 1, yaw), q_axis(0, 1, 0, pitch)), q_axis(1, 0, 0, roll));
}

/* what rides it, in its frame: a wheel (turned and spun, as posed), the turret (aimed once a tick, in the tick's
own work), the seat's unit (its root and head), and the anchor of the view (the unit's camera position) */
#define WHEEL_AT at(1.2f, 0.6f, -0.3f)
#define TURRET_AT at(-0.4f, 0.0f, 0.8f)
#define SEAT_AT at(0.2f, 0.35f, 0.5f)
#define HEAD_AT at(0.2f, 0.35f, 1.1f)
#define ANCHOR_AT at(0.1f, 0.35f, 1.0f)
static quat wheel_rotation(void) { return q_mul(q_axis(0, 0, 1, 0.3f), q_axis(0, 1, 0, 0.7f)); }
static quat turret_rotation(long tick) { return q_axis(0, 0, 1, 0.25f * (real)tick); }
static quat identity(void) { return q_axis(1, 0, 0, 0.0f); }
/* the view's swing about the anchor when following (a constant: the aim is the player's, not the vehicle's) */
static real_vector3d const swing = { -3.0f, 0.5f, 1.2f };

static void object_add(long index, short type, long parent, short node_count)
{
	long at_index = absolute(index);

	exists[at_index] = TRUE;
	identifiers[at_index] = index;
	memset(&objects[at_index], 0, sizeof(objects[at_index]));
	objects[at_index].object.type = type;
	objects[at_index].object.parent_object_index = parent;
	objects[at_index].object.node_matrices.size = (short)(node_count * sizeof(real_matrix4x3));
}

/* the world's state: the ticks ended, the open tick's sub-steps run */
static long ticks_ended;
static short substeps_run;
/* the vehicle's frame after each sub-step of each tick, the test's own record */
#define MAXIMUM_TICKS 128
static real_point3d frame_places[MAXIMUM_TICKS][MAXIMUM_SIM_SUBSTEPS];
static quat frame_rotations[MAXIMUM_TICKS][MAXIMUM_SIM_SUBSTEPS];

/* everything posed where the vehicle is (vehicle_substep_pose), the turret aimed as the tick has it */
static void world_pose(long tick, real t)
{
	real_point3d p = vehicle_place_at(t);
	quat q = vehicle_rotation_at(t);
	real_matrix4x3 frame = q_frame(q, p);
	real_matrix4x3 *vehicle = world_nodes[absolute(VEHICLE)];

	object_get(VEHICLE)->object.position = p;
	vehicle[0] = frame;
	vehicle[1] = composed(&frame, q, wheel_rotation(), WHEEL_AT);
	vehicle[2] = composed(&frame, q, turret_rotation(tick), TURRET_AT);
	world_nodes[absolute(RIDER)][0] = composed(&frame, q, identity(), SEAT_AT);
	world_nodes[absolute(RIDER)][1] = composed(&frame, q, identity(), HEAD_AT);
	object_get(RIDER)->object.position = world_nodes[absolute(RIDER)][0].position;
	world_nodes[absolute(GUN)][0] = vehicle[2];
	object_get(GUN)->object.position = vehicle[2].position;
}

/* units.c's: the seat's camera point on the vehicle, as it is now */
static void unit_get_camera_position(long index, real_point3d *position)
{
	CHECK(index == RIDER, "the camera position of another unit");
	*position = placed(&world_nodes[absolute(VEHICLE)][0], ANCHOR_AT);
}

/* a sub-step of the open tick */
static void world_substep(void)
{
	long tick = ticks_ended + 1;
	real t = ((real)tick - 1.0f + (real)(substeps_run + 1) / (real)fake_substep_count) * seconds_per_tick();

	CHECK(tick < MAXIMUM_TICKS, "too many ticks");
	world_pose(tick, t);
	frame_places[tick][substeps_run] = vehicle_place_at(t);
	frame_rotations[tick][substeps_run] = vehicle_rotation_at(t);
	render_interpolation_register();
	slot_procedure(substeps_run, fake_substep_count, seconds_per_tick() / fake_substep_count);
	if (++substeps_run == fake_substep_count)
	{
		substeps_run = 0;
		ticks_ended++;
		render_interpolation_tick();
	}
}

static void world_new(short substep_count)
{
	memset(exists, 0, sizeof(exists));
	fake_substep_count = substep_count;
	ticks_ended = 0;
	substeps_run = 0;
	vehicle_micro = TRUE;
	fake_player_unit = RIDER;
	render_interpolation_register();
	object_add(VEHICLE, _object_type_vehicle, NONE, 3);
	object_add(RIDER, _object_type_biped, VEHICLE, 2);
	object_add(GUN, _object_type_weapon, VEHICLE, 1);
	world_pose(0, 0.0f);
	frame_places[0][substep_count - 1] = vehicle_place_at(0.0f);
	frame_rotations[0][substep_count - 1] = vehicle_rotation_at(0.0f);
}

/* a frame drawn now, a part (0 to 1) of a sub-step after the last that ran */
static void frame_begin(real part)
{
	fake_fraction = ((real)substeps_run + part) / (real)fake_substep_count;
	render_interpolation_frame_begin();
}

static real distance(real_point3d const *a, real_point3d const *b)
{
	real x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;

	return (real)sqrt(x * x + y * y + z * z);
}

/* how far a drawn node is from the one expected: its place, and its axes' ends a world unit out */
static real node_error(real_matrix4x3 const *drawn, real_matrix4x3 const *expected)
{
	real_vector3d const *a[3] = { &drawn->forward, &drawn->left, &drawn->up };
	real_vector3d const *b[3] = { &expected->forward, &expected->left, &expected->up };
	real error = distance(&drawn->position, &expected->position);
	short axis;

	for (axis = 0; axis < 3; axis++)
	{
		real x = a[axis]->i - b[axis]->i, y = a[axis]->j - b[axis]->j, z = a[axis]->k - b[axis]->k;
		real e = (real)sqrt(x * x + y * y + z * z);

		if (e > error)
			error = e;
	}
	return error;
}

static real_point3d point_between(real_point3d const *a, real_point3d const *b, real t)
{
	return at(a->x + (b->x - a->x) * t, a->y + (b->y - a->y) * t, a->z + (b->z - a->z) * t);
}

/* the vehicle's frame on its sub-steps' path, as a frame of the tick drawn should have it, and its rotation */
static real_matrix4x3 frame_on_path(quat *rotation)
{
	long tick = ticks_ended;
	short j;
	real fs;
	real_point3d before_place;
	quat before_rotation;

	sim_substep_blend(fake_fraction, &j, &fs);
	before_place = j > 0 ? frame_places[tick][j - 1] : frame_places[tick - 1][fake_substep_count - 1];
	before_rotation = j > 0 ? frame_rotations[tick][j - 1] : frame_rotations[tick - 1][fake_substep_count - 1];
	*rotation = q_nlerp(before_rotation, frame_rotations[tick][j], fs);
	return q_frame(*rotation, point_between(&before_place, &frame_places[tick][j], fs));
}

/* ... and on the line between the drawn tick's snapshots (the frames as before) */
static real_matrix4x3 frame_on_line(quat *rotation)
{
	long tick = ticks_ended;
	short last = fake_substep_count - 1;

	*rotation = q_nlerp(frame_rotations[tick - 1][last], frame_rotations[tick][last], fake_fraction);
	return q_frame(*rotation, point_between(&frame_places[tick - 1][last], &frame_places[tick][last], fake_fraction));
}

static real random_part(void)
{
	return (real)(rand() % 1000) / 1000.0f;
}

/* the observer as the main loop has it after the frame's sub-steps: at the seat's camera point now (the first
person), or swung about it (following) */
static struct observer_result observer_now(void)
{
	struct observer_result observer;

	memset(&observer, 0, sizeof(observer));
	unit_get_camera_position(RIDER, &observer.position);
	if (perspective == _director_perspective_third_person)
	{
		observer.position.x += swing.i;
		observer.position.y += swing.j;
		observer.position.z += swing.k;
	}
	observer.forward.i = 1.0f;
	observer.up.k = 1.0f;
	observer.field_of_view = 1.2f;
	return observer;
}

/* each drawn node of the vehicle and of what it carries, against the frame given (its rotation q) and the pose
blended between the ticks in it (the turret's aim): how far the worst is */
static real tree_error(real_matrix4x3 const *frame, quat q, long frame_number)
{
	long tick = ticks_ended;
	quat turret = q_nlerp(turret_rotation(tick - 1), turret_rotation(tick), fake_fraction);
	real_matrix4x3 expected[6];
	real_matrix4x3 const *drawn[6];
	real worst = 0.0f;
	short index;

	expected[0] = *frame;
	expected[1] = composed(frame, q, wheel_rotation(), WHEEL_AT);
	expected[2] = composed(frame, q, turret, TURRET_AT);
	expected[3] = composed(frame, q, identity(), SEAT_AT);
	expected[4] = composed(frame, q, identity(), HEAD_AT);
	expected[5] = expected[2];
	drawn[0] = render_interpolation_object_node_matrices(VEHICLE);
	drawn[3] = render_interpolation_object_node_matrices(RIDER);
	drawn[5] = render_interpolation_object_node_matrices(GUN);
	CHECK(drawn[0] && drawn[3] && drawn[5], "frame %ld: not drawn between the ticks", frame_number);
	drawn[1] = drawn[0] + 1;
	drawn[2] = drawn[0] + 2;
	drawn[4] = drawn[3] + 1;
	for (index = 0; index < 6; index++)
	{
		real error = node_error(drawn[index], &expected[index]);

		if (error > worst)
			worst = error;
	}
	return worst;
}

/* ---------- cases */

#define CLOSE 2e-5f

/* At 30 a tick is one sub-step: nothing is kept, and the frames draw every node on the line between the ticks'
snapshots (each node's own place lerped and rotation nlerped), as they always did */
static void case_classic(void)
{
	long frame;

	world_new(1);
	for (frame = 0; frame < 150; frame++)
	{
		real_matrix4x3 const *drawn;
		real_point3d line;

		if (frame % 3 == 0)
			world_substep();
		if (ticks_ended < 3)
			continue;
		frame_begin(random_part());
		drawn = render_interpolation_object_node_matrices(VEHICLE);
		CHECK(drawn, "frame %ld: not drawn between the ticks", frame);
		/* (the wheel: its own place, lerped) */
		{
			real_matrix4x3 before, after;
			quat rb, ra;

			before = q_frame(rb = frame_rotations[ticks_ended - 1][0], frame_places[ticks_ended - 1][0]);
			after = q_frame(ra = frame_rotations[ticks_ended][0], frame_places[ticks_ended][0]);
			before = composed(&before, rb, wheel_rotation(), WHEEL_AT);
			after = composed(&after, ra, wheel_rotation(), WHEEL_AT);
			line = point_between(&before.position, &after.position, fake_fraction);
		}
		CHECK(distance(&drawn[1].position, &line) < CLOSE, "frame %ld: the wheel off the line between the ticks by %f",
			frame, distance(&drawn[1].position, &line));
		render_interpolation_frame_end();
	}
}

/* At 60 or 120, a frame after each sub-step (part of one since): the vehicle, its wheel and turret, its rider and
its gun are drawn on its sub-steps' path, turning as it did, which strays from the line between the ticks; the
view from its seat with it */
static void substeps_case(short substep_count, short view)
{
	long frame;
	real largest_stray = 0.0f;

	world_new(substep_count);
	perspective = view;
	for (frame = 0; frame < 40 * substep_count; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real_matrix4x3 path, line;
		quat path_rotation, line_rotation;
		real_point3d eye;
		real error, stray;

		world_substep();
		if (ticks_ended < 1)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		camera = render_interpolation_blended_camera(0, &observer);
		if (ticks_ended < 3)
		{
			/* (the view's snapshots begun) */
			render_interpolation_frame_end();
			continue;
		}
		path = frame_on_path(&path_rotation);
		line = frame_on_line(&line_rotation);
		error = tree_error(&path, path_rotation, frame);
		stray = tree_error(&line, line_rotation, frame);
		CHECK(error < CLOSE, "frame %ld: the vehicle or what it carries off its sub-steps' path by %f (%f from the "
			"line between the ticks)", frame, error, stray);
		if (stray > largest_stray)
			largest_stray = stray;
		eye = placed(&path, ANCHOR_AT);
		if (view == _director_perspective_third_person)
		{
			eye.x += swing.i;
			eye.y += swing.j;
			eye.z += swing.k;
		}
		CHECK(distance(&camera->position, &eye) < CLOSE, "frame %ld: the view off the vehicle's sub-steps' path by %f",
			frame, distance(&camera->position, &eye));
		render_interpolation_frame_end();
	}
	CHECK(largest_stray > 1e-3f, "the path never left the line (%f): the case shows nothing", largest_stray);
}

/* a frame slower than a sub-step: two (or three) a frame, the frame after a tick's end with the next tick's first
in it, which the observer has, the vehicle moved and turned (the view drawn from snapshots taken back to where the
tick left its anchor on the vehicle) */
static void camera_slow_case(short view)
{
	long frame;

	world_new(4);
	perspective = view;
	for (frame = 0; frame < 80; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real_matrix4x3 path;
		quat path_rotation;
		real_point3d eye;

		world_substep();
		world_substep();
		if (frame % 3 == 0)
			world_substep();
		if (ticks_ended < 1)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		camera = render_interpolation_blended_camera(0, &observer);
		if (ticks_ended < 4)
		{
			render_interpolation_frame_end();
			continue;
		}
		path = frame_on_path(&path_rotation);
		CHECK(tree_error(&path, path_rotation, frame) < CLOSE, "frame %ld: the vehicle off its sub-steps' path by %f",
			frame, tree_error(&path, path_rotation, frame));
		eye = placed(&path, ANCHOR_AT);
		if (view == _director_perspective_third_person)
		{
			eye.x += swing.i;
			eye.y += swing.j;
			eye.z += swing.k;
		}
		CHECK(distance(&camera->position, &eye) < CLOSE, "frame %ld: the view off the vehicle's sub-steps' path by %f",
			frame, distance(&camera->position, &eye));
		render_interpolation_frame_end();
	}
}

/* A respawn into the seat (or a revert to one): dead (no unit), the game's camera the dead camera, 1.5 units behind
and above the seat's camera point, looking down at 37 degrees (nearer, and turned less, than a cut is taken for);
then the rider the player's unit, the game's camera at its seat at once. Every frame from the respawn's tick on is
drawn at the seat (within the vehicle's tick of motion of it, as the view hangs on the vehicle's path), never on the
way there from the dead camera, in the first person or following */
static void respawn_case(short view)
{
	long frame;
	long respawn_tick = 6;
	boolean back_seen = FALSE;

	world_new(4);
	perspective = view;
	for (frame = 0; frame < 30 * 4; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		boolean back;

		world_substep();
		if (ticks_ended < 1)
			continue;
		back = ticks_ended >= respawn_tick;
		fake_player_unit = back ? RIDER : NONE;
		frame_begin(random_part());
		observer = observer_now();
		if (!back)
		{
			observer.position.x -= 1.2f;
			observer.position.z += 0.9f;
			observer.forward.i = 0.8f;
			observer.forward.k = -0.6f;
			observer.up.i = 0.6f;
			observer.up.k = 0.8f;
		}
		camera = render_interpolation_blended_camera(0, &observer);
		if (back)
		{
			struct observer_result now = observer_now();

			CHECK(distance(&camera->position, &now.position) < 0.4f, "frame %ld (tick %ld, the respawn's %ld): the "
				"view %f from the seat's, on its way in from the dead camera", frame, ticks_ended, respawn_tick,
				distance(&camera->position, &now.position));
			CHECK(camera->forward.k > -0.01f, "frame %ld: the view still turning down from the dead camera's (%f)",
				frame, camera->forward.k);
			back_seen = TRUE;
		}
		render_interpolation_frame_end();
	}
	CHECK(back_seen, "no frame after the respawn");
}

/* A vehicle of the macro lane (the AI drives it, or nobody does) at 120: nothing of it is kept, and it is drawn on
the line between the ticks as before (each node's place lerped), and the view in its seat from its snapshots */
static void case_macro(void)
{
	long frame;

	world_new(4);
	vehicle_micro = FALSE;
	perspective = _director_perspective_first_person;
	for (frame = 0; frame < 120; frame++)
	{
		real_matrix4x3 const *drawn;
		struct observer_result observer;
		struct observer_result const *camera;
		real_point3d line, before, after;

		world_substep();
		if (ticks_ended < 1)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		camera = render_interpolation_blended_camera(0, &observer);
		if (ticks_ended < 3)
		{
			render_interpolation_frame_end();
			continue;
		}
		drawn = render_interpolation_object_node_matrices(RIDER);
		CHECK(drawn, "frame %ld: not drawn between the ticks", frame);
		{
			real_matrix4x3 b = q_frame(frame_rotations[ticks_ended - 1][3], frame_places[ticks_ended - 1][3]);
			real_matrix4x3 a = q_frame(frame_rotations[ticks_ended][3], frame_places[ticks_ended][3]);

			before = placed(&b, SEAT_AT);
			after = placed(&a, SEAT_AT);
		}
		line = point_between(&before, &after, fake_fraction);
		CHECK(distance(&drawn[0].position, &line) < CLOSE, "frame %ld: the rider off the line between the ticks by %f",
			frame, distance(&drawn[0].position, &line));
		(void)camera;
		render_interpolation_frame_end();
	}
}

int main(int argc, char **argv)
{
	CHECK(argc == 2, "usage: %s <case>", argv[0]);
	srand(11);
	if (!strcmp(argv[1], "classic"))
		case_classic();
	else if (!strcmp(argv[1], "substeps"))
		substeps_case(4, _director_perspective_first_person);
	else if (!strcmp(argv[1], "substeps_60"))
		substeps_case(2, _director_perspective_first_person);
	else if (!strcmp(argv[1], "following"))
		substeps_case(4, _director_perspective_third_person);
	else if (!strcmp(argv[1], "camera_slow"))
		camera_slow_case(_director_perspective_first_person);
	else if (!strcmp(argv[1], "following_slow"))
		camera_slow_case(_director_perspective_third_person);
	else if (!strcmp(argv[1], "respawn"))
		respawn_case(_director_perspective_first_person);
	else if (!strcmp(argv[1], "respawn_following"))
		respawn_case(_director_perspective_third_person);
	else if (!strcmp(argv[1], "macro"))
		case_macro();
	else
		CHECK(0, "no case %s", argv[1]);
	return 0;
}
