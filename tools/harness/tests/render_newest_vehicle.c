#include "harness.h"
#include <math.h>

/* A fake world of a local player's vehicle (three nodes: its root, a wheel, a turret that aims once a tick), the
player's unit in its seat (two nodes) and the turret's gun carried on it, moving steadily: its velocity and its spin
(about an axis through its root, tilted) constant, so that where it is at any time is known. The clock's sub-steps
(k a tick) run one at a time: of the micro lane each moves it its share of a tick, of the macro lane the first moves
it a whole tick (game.c's game_tick_substep); the tick's last ends it (render_interpolation_tick). Frames are drawn
between them as the clock has it (game_time_get_tick_fraction: (completed sub-steps + the part of one run since) /
k), and take the player's view (render_interpolation_camera) as main.c does: from the observer as the frame's steps
left it, at the seat's camera point (the first person), or swung about it (following). */

typedef struct { real red, green, blue; } real_rgb_color;
typedef struct { real_vector3d forward, left, up; real_point3d position; real scale; } real_matrix4x3;
typedef struct { real yaw, pitch; } real_euler_angles2d;
struct observer_result { real_point3d position; real_vector3d forward, up; real field_of_view; };

#define NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS 4
#define NUMBER_OF_OBJECT_CHANGE_COLORS 4
#define MAXIMUM_OBJECTS_PER_MAP 16
#define MAXIMUM_LOCAL_PLAYERS 1
#define PIN(x, low, high) ((x) < (low) ? (low) : (x) > (high) ? (high) : (x))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#include "decls.inc"

static real_vector3d const zero3d;
#define global_zero_vector3d (&zero3d)
static real_vector3d const right3d = { 0.0f, -1.0f, 0.0f };
static real_vector3d const down3d = { 0.0f, 0.0f, -1.0f };
static real_vector3d const *global_right3d = &right3d;
static real_vector3d const *global_down3d = &down3d;

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
	real_vector3d forward, up;
	real_vector3d translational_velocity;
	real_vector3d angular_velocity;
	long parent_object_index;
	long first_child_object_index;
	long next_object_index;
	real outgoing_function_values[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	real_rgb_color outgoing_change_colors[NUMBER_OF_OBJECT_CHANGE_COLORS];
	short type;
};
struct object_datum { long definition_index; struct _object object; };
struct unit_datum { long definition_index; struct _object object; struct { real active_camouflage, active_camouflage_super_amount; } unit; };
struct player_control { short zoom_level; real pitch_minimum, pitch_maximum; };

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
/* display.direct_camera */
static int direct_camera = TRUE;
/* the seat: its view the gun's, its aim the world's */
static boolean seat_gun_view = FALSE;
static boolean seat_aim_in_world = FALSE;
/* the aim's motion come since the frame's input took it (the mouse's), and its readings */
static real late_yaw, late_pitch;
static long late_reads;
static real_euler_angles2d facing = { 0.7f, -0.2f };
static struct player_control control = { NONE, -1.4f, 1.4f };

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
/* the open tick's next sub-step (the world's, below) */
static short sim_substep_next(void);
static real game_time_get_tick_fraction(void) { return fake_fraction; }
static short director_get_perspective(short local_player_index) { return perspective; }
static boolean director_inhibited_facing(short local_player_index) { return FALSE; }
static long scripted_camera_object_relative_to(void) { return NONE; }
static boolean scripted_camera_object_is_first_person_camera(long index) { return FALSE; }
static boolean cinematic_in_progress(void) { return FALSE; }
static int config_boolean(char const *name) { return direct_camera; }
static unsigned long config_changes_count;
static unsigned long config_changes(void) { return config_changes_count; }
/* the player's unit (NONE while dead) */
static long fake_player_unit = RIDER;
static long player_control_get_unit_index(short local_player_index) { return fake_player_unit; }
static long local_player_get_player_index(short local_player_index) { return 0; }
static struct player_control *player_control_get(short local_player_index) { return &control; }
static real_euler_angles2d const *player_control_get_facing_angles(short local_player_index) { return &facing; }
static real mouse_zoom_look_scale(short local_player_index, long unit_index, short zoom_level) { return 1.0f; }
static int halo_linux_mouse_look_late(short gamepad_index, float *yaw, float *pitch)
{
	late_reads++;
	*yaw = late_yaw;
	*pitch = late_pitch;
	return late_yaw != 0.0f || late_pitch != 0.0f;
}
static boolean player_control_touch_look_late(short local_player_index, real *yaw, real *pitch)
{
	*yaw = 0.0f;
	*pitch = 0.0f;
	return FALSE;
}
static boolean passenger_camera_enabled(short local_player_index) { return FALSE; }
static real biped_eye_rise_ahead(long unit_index, real since) { return 0.0f; }
static void unit_get_camera_position(long index, real_point3d *position);
static real_vector3d *vector3d_from_euler_angles2d(real_vector3d *vector, real_euler_angles2d const *angles)
{
	vector->i = (real)(cos(angles->pitch) * cos(angles->yaw));
	vector->j = (real)(cos(angles->pitch) * sin(angles->yaw));
	vector->k = (real)sin(angles->pitch);
	return vector;
}
static void player_aiming_vector_from_facing(long player_index, real_vector3d *vector, real_euler_angles2d const *angles)
{
	vector3d_from_euler_angles2d(vector, angles);
}
static real_vector3d *cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *c)
{
	real_vector3d result;

	result.i = a->j * b->k - a->k * b->j;
	result.j = a->k * b->i - a->i * b->k;
	result.k = a->i * b->j - a->j * b->i;
	*c = result;
	return c;
}
static real normalize3d(real_vector3d *v)
{
	real length = (real)sqrt(v->i * v->i + v->j * v->j + v->k * v->k);

	if (length != 0.0f)
	{
		v->i /= length;
		v->j /= length;
		v->k /= length;
	}
	return length;
}
/* observer.c's */
static void observer_up_from_forward(real_vector3d const *forward, real_vector3d *up)
{
	real_vector3d right;

	right.i = forward->j;
	right.j = -forward->i;
	right.k = 0.0f;
	if (normalize3d(&right) == 0.0f)
	{
		right.i = 1.0f;
		right.j = right.k = 0.0f;
	}
	cross_product3d(&right, forward, up);
}
/* the seat (its tags) */
static void render_interpolation_seat(long unit_index, boolean *gun_view, boolean *aim_in_world)
{
	*gun_view = seat_gun_view;
	*aim_in_world = seat_aim_in_world;
}
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

static real_point3d placed(real_matrix4x3 const *frame, real_point3d local)
{
	return at(frame->position.x + frame->forward.i * local.x + frame->left.i * local.y + frame->up.i * local.z,
		frame->position.y + frame->forward.j * local.x + frame->left.j * local.y + frame->up.j * local.z,
		frame->position.z + frame->forward.k * local.x + frame->left.k * local.y + frame->up.k * local.z);
}

static real_matrix4x3 composed(real_matrix4x3 const *frame, quat frame_rotation, quat rotation, real_point3d local)
{
	return q_frame(q_mul(frame_rotation, rotation), placed(frame, local));
}

static real dot(real_vector3d a, real_vector3d b) { return a.i * b.i + a.j * b.j + a.k * b.k; }

static real_vector3d along(real_matrix4x3 const *frame, real_vector3d local)
{
	real_vector3d v;

	v.i = frame->forward.i * local.i + frame->left.i * local.j + frame->up.i * local.k;
	v.j = frame->forward.j * local.i + frame->left.j * local.j + frame->up.j * local.k;
	v.k = frame->forward.k * local.i + frame->left.k * local.j + frame->up.k * local.k;
	return v;
}

static real distance(real_point3d const *a, real_point3d const *b)
{
	real x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;

	return (real)sqrt(x * x + y * y + z * z);
}

static real vector_distance(real_vector3d a, real_vector3d b)
{
	return (real)sqrt((a.i - b.i) * (a.i - b.i) + (a.j - b.j) * (a.j - b.j) + (a.k - b.k) * (a.k - b.k));
}

/* ---------- the world */

/* the vehicle's steady motion, by the simulation's time in ticks: a velocity (world units a tick) and a spin about
a tilted axis through its root (radians a tick), from a tilted, rolled start */
static real_vector3d const velocity = { 0.45f, 0.2f, 0.05f };
static real const spin_rate = 0.08f;
/* ... speeding up, and turning faster (world units a tick a tick, radians a tick a tick): none while steady */
static real_vector3d acceleration;
static real spin_acceleration;
/* ... standing until this tick (0: from the start), and from this tick on climbing this much faster (world units a
tick: a ramp's lip, a bounce), which no motion carried on foresees */
static real start_tick;
static real kink_tick = 1e9f;
static real kink_climb;
static real_vector3d spin_axis(void)
{
	real_vector3d axis = { 0.3f, 0.2f, 0.93f };

	normalize3d(&axis);
	return axis;
}
/* a correction's jump of the path (the netcode's), from when it came */
static real_vector3d path_jump;

static real_point3d vehicle_place_at(real t)
{
	real climbed = t > kink_tick ? kink_climb * (t - kink_tick) : 0.0f;

	t -= start_tick > 0.0f ? start_tick : 0.0f;
	if (t < 0.0f)
		t = 0.0f;
	return at(2.0f + velocity.i * t + 0.5f * acceleration.i * t * t + path_jump.i,
		-1.0f + velocity.j * t + 0.5f * acceleration.j * t * t + path_jump.j,
		0.5f + velocity.k * t + 0.5f * acceleration.k * t * t + path_jump.k + climbed);
}

static quat vehicle_rotation_at(real t)
{
	real_vector3d axis = spin_axis();
	quat start = q_mul(q_axis(0, 0, 1, 0.4f), q_mul(q_axis(0, 1, 0, 0.15f), q_axis(1, 0, 0, 0.2f)));

	t -= start_tick > 0.0f ? start_tick : 0.0f;
	if (t < 0.0f)
		t = 0.0f;

	return q_mul(q_axis(axis.i, axis.j, axis.k, spin_rate * t + 0.5f * spin_acceleration * t * t), start);
}

#define WHEEL_AT at(1.2f, 0.6f, -0.3f)
#define TURRET_AT at(-0.4f, 0.0f, 0.8f)
#define SEAT_AT at(0.2f, 0.35f, 0.5f)
#define HEAD_AT at(0.2f, 0.35f, 1.1f)
#define ANCHOR_AT at(0.1f, 0.35f, 1.0f)
static quat wheel_rotation(void) { return q_mul(q_axis(0, 0, 1, 0.3f), q_axis(0, 1, 0, 0.7f)); }
static quat turret_rotation(long tick) { return q_axis(0, 0, 1, 0.25f * (real)tick); }
static quat identity(void) { return q_axis(1, 0, 0, 0.0f); }
static real_vector3d const swing = { -3.0f, 0.5f, 1.2f };
/* the following camera's pull in, as the observer finds it frame by frame (at what it would pass through) */
static real swing_scale = 1.0f;

static void object_add(long index, short type, long parent, short node_count)
{
	long at_index = absolute(index);

	exists[at_index] = TRUE;
	identifiers[at_index] = index;
	memset(&objects[at_index], 0, sizeof(objects[at_index]));
	objects[at_index].object.type = type;
	objects[at_index].object.parent_object_index = parent;
	objects[at_index].object.first_child_object_index = NONE;
	objects[at_index].object.next_object_index = NONE;
	objects[at_index].object.node_matrices.size = (short)(node_count * sizeof(real_matrix4x3));
}

static long ticks_ended;
static short substeps_run;
/* the physics' velocity and spin kept apart from the motion (as a tick keeps them half a sub-step's acceleration
apart from its sub-steps', and half a step behind): a factor of the motion's */
static real physics_apart = 1.0f;

static short sim_substep_next(void) { return substeps_run; }
/* the simulation's time (ticks) the world was last posed at */
static real posed_at;

static void world_pose(long tick, real t)
{
	real_point3d p = vehicle_place_at(t);
	quat q = vehicle_rotation_at(t);
	real_matrix4x3 frame = q_frame(q, p);
	real_matrix4x3 *vehicle = world_nodes[absolute(VEHICLE)];
	real_vector3d axis = spin_axis();

	object_get(VEHICLE)->object.position = p;
	object_get(VEHICLE)->object.forward = frame.forward;
	object_get(VEHICLE)->object.up = frame.up;
	if (t < start_tick)
	{
		axis.i = axis.j = axis.k = 0.0f;
	}
	object_get(VEHICLE)->object.translational_velocity.i = velocity.i * physics_apart;
	object_get(VEHICLE)->object.translational_velocity.j = velocity.j * physics_apart;
	object_get(VEHICLE)->object.translational_velocity.k = velocity.k * physics_apart;
	object_get(VEHICLE)->object.angular_velocity.i = axis.i * spin_rate * physics_apart;
	object_get(VEHICLE)->object.angular_velocity.j = axis.j * spin_rate * physics_apart;
	object_get(VEHICLE)->object.angular_velocity.k = axis.k * spin_rate * physics_apart;
	vehicle[0] = frame;
	vehicle[1] = composed(&frame, q, wheel_rotation(), WHEEL_AT);
	vehicle[2] = composed(&frame, q, turret_rotation(tick), TURRET_AT);
	world_nodes[absolute(RIDER)][0] = composed(&frame, q, identity(), SEAT_AT);
	world_nodes[absolute(RIDER)][1] = composed(&frame, q, identity(), HEAD_AT);
	world_nodes[absolute(GUN)][0] = vehicle[2];
	posed_at = t;
}

/* units.c's: the seat's camera point on the vehicle, as it is now */
static void unit_get_camera_position(long index, real_point3d *position)
{
	CHECK(index == RIDER, "the camera position of another unit");
	*position = placed(&world_nodes[absolute(VEHICLE)][0], ANCHOR_AT);
}

/* a sub-step of the open tick: of the micro lane its share of the tick, of the macro lane the tick's whole motion
in its first */
static void world_substep(void)
{
	long tick = ticks_ended + 1;

	if (vehicle_micro)
		world_pose(tick, (real)ticks_ended + (real)(substeps_run + 1) / (real)fake_substep_count);
	else if (substeps_run == 0)
		world_pose(tick, (real)tick);
	render_interpolation_register();
	slot_procedure(substeps_run, fake_substep_count, 1.0f / 30.0f / fake_substep_count);
	if (++substeps_run == fake_substep_count)
	{
		substeps_run = 0;
		ticks_ended++;
		render_interpolation_tick();
	}
}

static void world_new(short substep_count, boolean micro, short view)
{
	memset(exists, 0, sizeof(exists));
	memset(&path_jump, 0, sizeof(path_jump));
	fake_substep_count = substep_count;
	ticks_ended = 0;
	substeps_run = 0;
	vehicle_micro = micro;
	perspective = view;
	fake_player_unit = RIDER;
	object_add(VEHICLE, _object_type_vehicle, NONE, 3);
	object_add(RIDER, _object_type_biped, VEHICLE, 2);
	object_add(GUN, _object_type_weapon, VEHICLE, 1);
	object_get(VEHICLE)->object.first_child_object_index = RIDER;
	object_get(RIDER)->object.next_object_index = GUN;
	world_pose(0, 0.0f);
}

/* the frame's time in the simulation's (ticks): as far into the open tick as the clock has run */
static real frame_now;

/* a frame begun now, a part (0 to 1) of a sub-step after the last that ran */
static void frame_begin(real part)
{
	fake_fraction = ((real)substeps_run + part) / (real)fake_substep_count;
	frame_now = (real)ticks_ended + fake_fraction;
	render_interpolation_frame_begin();
}

static real random_part(void)
{
	return (real)(rand() % 1000) / 1000.0f;
}

/* the observer as the main loop has it after the frame's steps: at the seat's camera point (the first person, its
aim the seat's own, which the view takes again), or swung about it, aimed by the player (following) */
static struct observer_result observer_now(void)
{
	struct observer_result observer;

	memset(&observer, 0, sizeof(observer));
	unit_get_camera_position(RIDER, &observer.position);
	vector3d_from_euler_angles2d(&observer.forward, &facing);
	if (perspective == _director_perspective_third_person)
	{
		observer.position.x += swing.i * swing_scale;
		observer.position.y += swing.j * swing_scale;
		observer.position.z += swing.k * swing_scale;
	}
	observer_up_from_forward(&observer.forward, &observer.up);
	observer.field_of_view = 1.2f;
	return observer;
}

/* each drawn node of the vehicle and of what it carries against the frame given (rotation q), the pose blended
between the ticks in it (the turret's aim): how far the worst is */
static real tree_error(real_matrix4x3 const *frame, quat q)
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
	CHECK(drawn[0] && drawn[3] && drawn[5], "not drawn between the ticks");
	drawn[1] = drawn[0] + 1;
	drawn[2] = drawn[0] + 2;
	drawn[4] = drawn[3] + 1;
	for (index = 0; index < 6; index++)
	{
		real_vector3d const *a[3] = { &drawn[index]->forward, &drawn[index]->left, &drawn[index]->up };
		real_vector3d const *b[3] = { &expected[index].forward, &expected[index].left, &expected[index].up };
		real error = distance(&drawn[index]->position, &expected[index].position);
		short axis;

		for (axis = 0; axis < 3; axis++)
		{
			if (vector_distance(*a[axis], *b[axis]) > error)
				error = vector_distance(*a[axis], *b[axis]);
		}
		if (error > worst)
			worst = error;
	}
	return worst;
}

/* where the player aims, as the first-person camera poses its view in the seat of the vehicle as drawn (frame):
the facing (turned by the late motion), turned from the world's up to the vehicle's in a seat whose aim is the
vehicle's (players.c), its up square to it in the vehicle's frame (first_person_camera.c) */
static void seat_view(real_matrix4x3 const *frame, real_vector3d *forward, real_vector3d *up)
{
	real_euler_angles2d angles = facing;
	real_vector3d local, local_up, aim;

	angles.yaw += late_yaw;
	angles.pitch = PIN(angles.pitch + late_pitch, control.pitch_minimum, control.pitch_maximum);
	vector3d_from_euler_angles2d(&aim, &angles);
	if (!seat_aim_in_world)
	{
		real_matrix4x3 turn;

		turn.up = frame->up;
		cross_product3d(&frame->up, &right3d, &turn.forward);
		normalize3d(&turn.forward);
		cross_product3d(&turn.up, &turn.forward, &turn.left);
		aim = along(&turn, aim);
	}
	*forward = aim;
	local.i = dot(aim, frame->forward);
	local.j = dot(aim, frame->left);
	local.k = dot(aim, frame->up);
	observer_up_from_forward(&local, &local_up);
	*up = along(frame, local_up);
}

/* ---------- cases */

#define CLOSE 1e-4f
/* the tick from which the vehicle drawn from its newest step has glided from where it was drawn before, the first
frames' (a correction's glide, to none) */
#define SETTLED 20

/* Steady motion, at 30, 60 or 120, of the micro lane or the macro: the vehicle, its wheel and turret, its rider and
its gun are drawn where the vehicle is at the frame's time (its newest step's place carried on by its velocity, its
rotation by its spin, for the time since: less than none past the macro lane's first sub-step), the pose blended
between the ticks in its frame; the view, in the first person, at the seat's camera point there, aimed where the
player aims in the seat of the vehicle as drawn; following it, swung about that point as the observer has it */
static void steady_case(short substep_count, boolean micro, short view)
{
	long frame;

	world_new(substep_count, micro, view);
	for (frame = 0; frame < 50 * substep_count; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real_matrix4x3 now;
		quat q;
		real_point3d eye;
		real error;

		world_substep();
		if (frame % 3 == 0 && substeps_run != 0)
			world_substep();
		if (ticks_ended < 2)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		camera = render_interpolation_camera(0, &observer);
		if (ticks_ended < SETTLED)
		{
			render_interpolation_frame_end();
			continue;
		}
		q = vehicle_rotation_at(frame_now);
		now = q_frame(q, vehicle_place_at(frame_now));
		error = tree_error(&now, q);
		CHECK(error < CLOSE, "frame %ld (%.3f ticks, %.3f since its newest step's): the vehicle or what it carries "
			"%f from where it is at the frame's time", frame, frame_now, frame_now - posed_at, error);
		CHECK(render_interpolation_view_newest(0), "frame %ld: the view not drawn from the newest step", frame);
		eye = placed(&now, ANCHOR_AT);
		if (view == _director_perspective_third_person)
		{
			eye.x += swing.i;
			eye.y += swing.j;
			eye.z += swing.k;
			CHECK(vector_distance(camera->forward, observer.forward) < CLOSE, "frame %ld: the following view turned "
				"from the player's aim", frame);
		}
		else
		{
			real_vector3d forward, up;

			seat_view(&now, &forward, &up);
			CHECK(vector_distance(camera->forward, forward) < CLOSE && vector_distance(camera->up, up) < CLOSE,
				"frame %ld: the view in the seat aimed (%f %f %f) up (%f %f %f), not (%f %f %f) up (%f %f %f)", frame,
				camera->forward.i, camera->forward.j, camera->forward.k, camera->up.i, camera->up.j, camera->up.k,
				forward.i, forward.j, forward.k, up.i, up.j, up.k);
		}
		CHECK(distance(&camera->position, &eye) < CLOSE, "frame %ld: the view %f from the seat's camera point at the "
			"frame's time", frame, distance(&camera->position, &eye));
		render_interpolation_frame_end();
	}
}

/* The aim's motion come since the frame's input took it (the mouse's late reading): the view takes it, read once a
frame; in the first person, aimed by it in the seat; following, it and its swing about the seat's camera point
turned by it, as the observer would be at the facing turned by it */
static void late_case(short view)
{
	long frame;

	world_new(2, TRUE, view);
	late_yaw = 0.03f;
	late_pitch = -0.02f;
	for (frame = 0; frame < 100; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real_matrix4x3 now;
		long reads;

		world_substep();
		if (ticks_ended < 2)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		reads = late_reads;
		camera = render_interpolation_camera(0, &observer);
		CHECK(late_reads == reads + 1, "frame %ld: the late motion read %ld times", frame, late_reads - reads);
		if (ticks_ended < SETTLED)
		{
			render_interpolation_frame_end();
			continue;
		}
		now = q_frame(vehicle_rotation_at(frame_now), vehicle_place_at(frame_now));
		if (view == _director_perspective_first_person)
		{
			real_vector3d forward, up;

			seat_view(&now, &forward, &up);
			CHECK(vector_distance(camera->forward, forward) < CLOSE && vector_distance(camera->up, up) < CLOSE,
				"frame %ld: the view in the seat not aimed by the late motion", frame);
		}
		else
		{
			real_euler_angles2d turned = facing;
			real_vector3d forward;
			real_point3d anchor = placed(&now, ANCHOR_AT);
			real_point3d expected;
			real_vector3d offset;

			turned.yaw += late_yaw;
			turned.pitch += late_pitch;
			vector3d_from_euler_angles2d(&forward, &turned);
			CHECK(vector_distance(camera->forward, forward) < CLOSE, "frame %ld: the following view not turned by the "
				"late motion", frame);
			/* (the swing turned with the aim: as far from the point, at the same angle to the aim) */
			offset.i = camera->position.x - anchor.x;
			offset.j = camera->position.y - anchor.y;
			offset.k = camera->position.z - anchor.z;
			CHECK(fabs(sqrt(dot(offset, offset)) - sqrt(dot(swing, swing))) < CLOSE, "frame %ld: the swing's length "
				"changed", frame);
			{
				real_vector3d before;

				vector3d_from_euler_angles2d(&before, &facing);
				CHECK(fabs(dot(offset, forward) - dot(swing, before)) < 1e-3f, "frame %ld: the swing not turned with "
					"the aim (%f, %f along it)", frame, dot(offset, forward), dot(swing, before));
			}
			expected = anchor;
			(void)expected;
		}
		render_interpolation_frame_end();
	}
	late_yaw = 0.0f;
	late_pitch = 0.0f;
}

/* Following the vehicle, the observer's swing pulled in and out frame by frame (at what the camera would pass
through, as the observer tests it each frame): the view swings as the frames between the ticks swing it, between
the swings the ticks' first frames found, as before (not the frame's own, which steps), about the seat's point on
the vehicle drawn from its newest step */
static void case_following_pulled(void)
{
	long frame;
	real sampled[512];
	long sampled_tick = NONE;

	world_new(4, TRUE, _director_perspective_third_person);
	for (frame = 0; frame < 400; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real_matrix4x3 now;
		real_point3d eye;
		real scale;

		world_substep();
		if (ticks_ended < 2)
			continue;
		swing_scale = 0.7f + 0.3f * (real)sin(2.3 * frame);
		frame_begin(random_part());
		observer = observer_now();
		camera = render_interpolation_camera(0, &observer);
		/* (the ticks' snapshots: the first frame after each) */
		if (sampled_tick != ticks_ended)
		{
			CHECK(ticks_ended < 512, "too many ticks");
			sampled[ticks_ended] = swing_scale;
			sampled_tick = ticks_ended;
		}
		if (ticks_ended < SETTLED)
		{
			render_interpolation_frame_end();
			continue;
		}
		now = q_frame(vehicle_rotation_at(frame_now), vehicle_place_at(frame_now));
		eye = placed(&now, ANCHOR_AT);
		scale = sampled[ticks_ended - 1] + (sampled[ticks_ended] - sampled[ticks_ended - 1]) * fake_fraction;
		eye.x += swing.i * scale;
		eye.y += swing.j * scale;
		eye.z += swing.k * scale;
		CHECK(distance(&camera->position, &eye) < CLOSE, "frame %ld: the view %f from the swing between the ticks' "
			"(the frame's own pulled in %f, between the ticks' %f)", frame, distance(&camera->position, &eye),
			swing_scale, scale);
		render_interpolation_frame_end();
	}
	swing_scale = 1.0f;
}

/* the drawn root's place this frame */
static real_point3d drawn_root(void)
{
	real_matrix4x3 const *drawn = render_interpolation_object_node_matrices(VEHICLE);

	CHECK(drawn, "the vehicle not drawn between the ticks");
	return drawn->position;
}

/* The vehicle climbs faster all at once (a ramp's lip, a bounce), which the motion carried on did not foresee: at 30,
the tick after it meets the vehicle a tick's misjudged climb from where it was drawn. It moves on from where it was
drawn, blended to the new path over the next tick, never jumping a tick's misjudged climb: a frame's move apart
from the frame before's by less than three quarters of that */
static void case_kinked(void)
{
	long frame;
	real worst = 0.0f;
	real_point3d last_root;
	real_vector3d last_move;
	long kept = 0;

	kink_tick = (real)SETTLED + 4.0f;
	kink_climb = 0.6f;
	world_new(1, TRUE, _director_perspective_first_person);
	for (frame = 0; frame < 3 * (SETTLED + 10); frame++)
	{
		struct observer_result observer;
		real_point3d root;
		real_vector3d move;

		if (frame % 3 == 0)
			world_substep();
		if (ticks_ended < 2)
			continue;
		frame_begin((real)(frame % 3) / 3.0f);
		observer = observer_now();
		render_interpolation_camera(0, &observer);
		root = drawn_root();
		if (ticks_ended >= SETTLED)
		{
			if (kept >= 1)
			{
				move.i = root.x - last_root.x;
				move.j = root.y - last_root.y;
				move.k = root.z - last_root.z;
				if (kept >= 2 && vector_distance(move, last_move) > worst)
					worst = vector_distance(move, last_move);
				last_move = move;
			}
			kept++;
		}
		last_root = root;
		render_interpolation_frame_end();
	}
	CHECK(worst < 0.75f * kink_climb, "a frame's move %f apart from the frame before's (a climb of %f a tick)", worst,
		kink_climb);
	kink_tick = 1e9f;
}

/* The vehicle starts from standing: the first frame after the step that moved it shows it moved (the step's latency
not given back to a blend from where it stood) */
static void case_start(void)
{
	long frame;
	boolean seen = FALSE;
	real_point3d stood;

	start_tick = (real)SETTLED + 2.0f;
	world_new(1, TRUE, _director_perspective_first_person);
	stood = vehicle_place_at(0.0f);
	for (frame = 0; frame < 3 * (SETTLED + 6) && !seen; frame++)
	{
		struct observer_result observer;
		real_point3d root;

		if (frame % 3 == 0)
			world_substep();
		if (ticks_ended < 2)
			continue;
		frame_begin((real)(frame % 3) / 3.0f + 0.1f);
		observer = observer_now();
		render_interpolation_camera(0, &observer);
		root = drawn_root();
		if ((real)ticks_ended >= start_tick + 1.0f)
		{
			real moved = distance(&root, &stood);
			real speed = (real)sqrt(dot(velocity, velocity));

			CHECK(moved > 0.5f * speed, "the first frame after the start drawn %f from where the vehicle stood (a "
				"step's motion %f)", moved, speed);
			seen = TRUE;
		}
		render_interpolation_frame_end();
	}
	CHECK(seen, "no frame after the start");
	start_tick = 0.0f;
}

/* Following the vehicle, the player's aim held at the seat's pitch limit, which the game eases a sub-step at a time
(the aim steps with it, then holds): the view's pitch moves evenly, a frame's change as much as the next's */
static void case_pitch_held(void)
{
	long frame;
	real last_pitch = 0.0f, last_change = 0.0f;
	long checked = 0;
	real saved_minimum = control.pitch_minimum, saved_pitch = facing.pitch;

	world_new(2, TRUE, _director_perspective_third_person);
	control.pitch_minimum = -0.5f;
	facing.pitch = control.pitch_minimum;
	for (frame = 0; frame < 3 * 2 * (SETTLED + 8); frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real pitch;

		if (frame % 3 == 0)
		{
			world_substep();
			/* (the limit eased on, as the vehicle tilts, the aim held at it) */
			control.pitch_minimum += 0.012f;
			facing.pitch = control.pitch_minimum;
		}
		if (ticks_ended < 2)
			continue;
		frame_begin((real)(frame % 3) / 3.0f);
		observer = observer_now();
		camera = render_interpolation_camera(0, &observer);
		pitch = (real)asin(camera->forward.k);
		if (ticks_ended >= SETTLED)
		{
			real change = pitch - last_pitch;

			if (checked > 0)
				CHECK(fabs(change - last_change) < 0.25f * 0.004f, "frame %ld: the view's pitch changed %f, %f the "
					"frame before (held at a limit easing 0.012 a sub-step)", frame, change, last_change);
			last_change = change;
			checked++;
		}
		last_pitch = pitch;
		render_interpolation_frame_end();
	}
	CHECK(checked > 20, "too few frames");
	control.pitch_minimum = saved_minimum;
	facing.pitch = saved_pitch;
}

/* The player leaves the vehicle's newest step (dies, or as they get out), or comes to it (gets in, or respawns in
it), at speed: the vehicle is drawn from one to the other gliding, as a correction is, never jumping the tick of
motion between them (a frame's move no further from the vehicle's own than the glide's share) */
static void glide_case(short substep_count, boolean leaving)
{
	long frame;
	long change_tick = 8;
	boolean changed_seen = FALSE;
	real_point3d last;
	real last_now = 0.0f;
	boolean has_last = FALSE;
	real speed = (real)sqrt(dot(velocity, velocity));

	world_new(substep_count, TRUE, _director_perspective_first_person);
	for (frame = 0; frame < 30 * substep_count; frame++)
	{
		struct observer_result observer;
		real_point3d root;
		boolean in_seat;

		world_substep();
		if (ticks_ended < 2)
			continue;
		in_seat = (ticks_ended >= change_tick) != leaving;
		fake_player_unit = in_seat ? RIDER : NONE;
		frame_begin(0.25f + 0.5f * random_part());
		if (in_seat)
		{
			observer = observer_now();
			render_interpolation_camera(0, &observer);
		}
		root = drawn_root();
		if (has_last)
		{
			real moved = distance(&root, &last);
			real own = speed * (frame_now - last_now);

			CHECK(fabs(moved - own) < 0.6f * speed * (frame_now - last_now) + 1e-3f, "frame %ld (tick %ld, the "
				"change's %ld): the vehicle moved %f, its own motion %f", frame, ticks_ended, change_tick, moved, own);
			if (ticks_ended == change_tick)
				changed_seen = TRUE;
		}
		last = root;
		last_now = frame_now;
		has_last = ticks_ended >= 3;
		render_interpolation_frame_end();
	}
	CHECK(changed_seen, "no frame at the change");
}

/* The vehicle teleported (further than a tick's motion: no glide) as the player leaves it: it is drawn where the
tick left it, at once, not gliding from the newest step's place far from there */
static void case_leaving_teleported(void)
{
	long frame;
	long change_tick = SETTLED + 2;
	boolean checked = FALSE;

	world_new(1, TRUE, _director_perspective_first_person);
	for (frame = 0; frame < 40; frame++)
	{
		struct observer_result observer;
		real_point3d root, there;

		if (ticks_ended == change_tick - 1)
		{
			path_jump.i = 15.0f;
			path_jump.j = 6.0f;
		}
		world_substep();
		if (ticks_ended < 2)
			continue;
		fake_player_unit = ticks_ended < change_tick ? RIDER : NONE;
		frame_begin(random_part());
		if (fake_player_unit != NONE)
		{
			observer = observer_now();
			render_interpolation_camera(0, &observer);
		}
		root = drawn_root();
		if (ticks_ended == change_tick)
		{
			there = vehicle_place_at((real)ticks_ended);
			CHECK(distance(&root, &there) < CLOSE, "frame %ld: the teleported vehicle drawn %f from where the tick "
				"left it", frame, distance(&root, &there));
			checked = TRUE;
		}
		render_interpolation_frame_end();
	}
	CHECK(checked, "no frame at the teleport");
}

/* The netcode moves the vehicle (a correction past the tolerance: render_interpolation_correct_object): the vehicle
glides there, and the view from its seat with it, the same, never jumping */
static void correction_case(void)
{
	long frame;
	boolean corrected = FALSE;
	real_point3d last_root, last_eye;
	real last_now = 0.0f;
	boolean has_last = FALSE;
	real speed = (real)sqrt(dot(velocity, velocity));

	world_new(2, TRUE, _director_perspective_first_person);
	for (frame = 0; frame < 80; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		real_matrix4x3 const *drawn;
		real_point3d eye;

		world_substep();
		if (ticks_ended == 6 && substeps_run == 0 && !corrected)
		{
			real_vector3d offset;

			/* (the path a world unit and a half off: where it is now, from where it was) */
			path_jump.i = 1.2f;
			path_jump.j = -0.6f;
			path_jump.k = 0.5f;
			world_pose(ticks_ended, (real)ticks_ended);
			offset.i = -path_jump.i;
			offset.j = -path_jump.j;
			offset.k = -path_jump.k;
			render_interpolation_correct_object(VEHICLE, &offset);
			corrected = TRUE;
		}
		if (ticks_ended < 2)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		camera = render_interpolation_camera(0, &observer);
		drawn = render_interpolation_object_node_matrices(VEHICLE);
		CHECK(drawn, "frame %ld: not drawn", frame);
		eye = placed(drawn, ANCHOR_AT);
		CHECK(distance(&camera->position, &eye) < CLOSE, "frame %ld: the view %f off the seat as the vehicle is "
			"drawn: it does not glide with the vehicle", frame, distance(&camera->position, &eye));
		if (has_last)
		{
			real moved = distance(&drawn->position, &last_root);
			real ticks = frame_now - last_now;

			CHECK(moved < speed * ticks + 0.6f * 1.5f * ticks + 1e-3f, "frame %ld: the vehicle jumped %f", frame,
				moved);
			CHECK(distance(&camera->position, &last_eye) < speed * ticks + 0.6f * 1.5f * ticks + 0.2f * ticks + 1e-3f,
				"frame %ld: the view jumped %f", frame, distance(&camera->position, &last_eye));
		}
		last_root = drawn->position;
		last_eye = camera->position;
		last_now = frame_now;
		has_last = TRUE;
		render_interpolation_frame_end();
	}
	CHECK(corrected, "no correction");
}

/* Not drawn from the newest step, as before: display.direct_camera off, or a seat whose view is the gun's (which
aims once a tick): at 30 the vehicle on the line between the ticks' snapshots, the view not the newest's */
static void as_before_case(boolean gun_view)
{
	long frame;

	world_new(1, TRUE, _director_perspective_first_person);
	direct_camera = !gun_view ? FALSE : TRUE;
	config_changes_count++;
	seat_gun_view = gun_view;
	for (frame = 0; frame < 60; frame++)
	{
		struct observer_result observer;
		real_point3d root, line, before, after;

		world_substep();
		if (ticks_ended < 2)
			continue;
		frame_begin(random_part());
		observer = observer_now();
		render_interpolation_camera(0, &observer);
		root = drawn_root();
		before = vehicle_place_at((real)ticks_ended - 1.0f);
		after = vehicle_place_at((real)ticks_ended);
		line = at(before.x + (after.x - before.x) * fake_fraction, before.y + (after.y - before.y) * fake_fraction,
			before.z + (after.z - before.z) * fake_fraction);
		CHECK(distance(&root, &line) < CLOSE, "frame %ld: the vehicle %f off the line between the ticks", frame,
			distance(&root, &line));
		CHECK(!render_interpolation_view_newest(0), "frame %ld: the view drawn from the newest step", frame);
		render_interpolation_frame_end();
	}
	direct_camera = TRUE;
	config_changes_count++;
	seat_gun_view = FALSE;
}

int main(int argc, char **argv)
{
	CHECK(argc == 2, "usage: %s <case>", argv[0]);
	srand(7);
	if (!strcmp(argv[1], "steady_30"))
		steady_case(1, TRUE, _director_perspective_first_person);
	else if (!strcmp(argv[1], "steady_60"))
		steady_case(2, TRUE, _director_perspective_first_person);
	else if (!strcmp(argv[1], "steady_120"))
		steady_case(4, TRUE, _director_perspective_first_person);
	else if (!strcmp(argv[1], "steady_macro_60"))
		steady_case(2, FALSE, _director_perspective_first_person);
	else if (!strcmp(argv[1], "steady_macro_120"))
		steady_case(4, FALSE, _director_perspective_first_person);
	else if (!strcmp(argv[1], "following"))
		steady_case(4, TRUE, _director_perspective_third_person);
	else if (!strcmp(argv[1], "following_30"))
		steady_case(1, TRUE, _director_perspective_third_person);
	else if (!strcmp(argv[1], "physics_apart"))
	{
		physics_apart = 1.3f;
		steady_case(4, TRUE, _director_perspective_first_person);
	}
	else if (!strcmp(argv[1], "physics_apart_30"))
	{
		physics_apart = 0.6f;
		steady_case(1, TRUE, _director_perspective_first_person);
	}
	else if (!strcmp(argv[1], "accelerating") || !strcmp(argv[1], "accelerating_30") ||
		!strcmp(argv[1], "accelerating_macro"))
	{
		acceleration.i = 0.012f;
		acceleration.j = -0.008f;
		acceleration.k = 0.004f;
		spin_acceleration = 0.006f;
		steady_case(!strcmp(argv[1], "accelerating_30") ? 1 : 2, strcmp(argv[1], "accelerating_macro") != 0,
			_director_perspective_first_person);
	}
	else if (!strcmp(argv[1], "aim_in_world"))
	{
		seat_aim_in_world = TRUE;
		steady_case(2, TRUE, _director_perspective_first_person);
	}
	else if (!strcmp(argv[1], "late"))
		late_case(_director_perspective_first_person);
	else if (!strcmp(argv[1], "following_pulled"))
		case_following_pulled();
	else if (!strcmp(argv[1], "following_late"))
		late_case(_director_perspective_third_person);
	else if (!strcmp(argv[1], "leaving"))
		glide_case(4, TRUE);
	else if (!strcmp(argv[1], "leaving_30"))
		glide_case(1, TRUE);
	else if (!strcmp(argv[1], "coming"))
		glide_case(4, FALSE);
	else if (!strcmp(argv[1], "coming_30"))
		glide_case(1, FALSE);
	else if (!strcmp(argv[1], "kinked"))
		case_kinked();
	else if (!strcmp(argv[1], "start"))
		case_start();
	else if (!strcmp(argv[1], "pitch_held"))
		case_pitch_held();
	else if (!strcmp(argv[1], "leaving_teleported"))
		case_leaving_teleported();
	else if (!strcmp(argv[1], "correction"))
		correction_case();
	else if (!strcmp(argv[1], "off"))
		as_before_case(FALSE);
	else if (!strcmp(argv[1], "gun_view"))
		as_before_case(TRUE);
	else
		CHECK(0, "no case %s", argv[1]);
	return 0;
}
