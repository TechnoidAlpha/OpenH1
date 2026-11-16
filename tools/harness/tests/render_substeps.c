#include "harness.h"
#include <math.h>

/* A fake world of four objects: a player's biped (three nodes, its eye 0.6 above it), the weapon it carries, a
projectile and a grenade in the biped's hand; the clock's sub-steps (k a tick) run one at a time, each moving the
objects along a curve and keeping the poses (the fast phase's render_interpolation slot), the tick's last ending it
(render_interpolation_tick); frames drawn between them as the clock has it (game_time_get_tick_fraction: (completed
sub-steps + the part of one run since) / k). */

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
	real_vector3d forward;
	real_vector3d translational_velocity;
	long parent_object_index;
	real outgoing_function_values[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	real_rgb_color outgoing_change_colors[NUMBER_OF_OBJECT_CHANGE_COLORS];
	short type;
};
struct object_datum { long definition_index; struct _object object; };
struct unit_datum { long definition_index; struct _object object; struct { real active_camouflage, active_camouflage_super_amount; } unit; };

#define MAXIMUM_NODES 3
#define BIPED 0x00010001L
#define WEAPON 0x00020002L
#define PROJECTILE 0x00030003L
#define GRENADE 0x00040004L
#define EYE 0.6f

static struct unit_datum objects[MAXIMUM_OBJECTS_PER_MAP];
static boolean exists[MAXIMUM_OBJECTS_PER_MAP];
static long identifiers[MAXIMUM_OBJECTS_PER_MAP];
static real_matrix4x3 world_nodes[MAXIMUM_OBJECTS_PER_MAP][MAXIMUM_NODES];
/* each object's nodes from its place (its pose): set by the world */
static real_vector3d node_offsets[MAXIMUM_OBJECTS_PER_MAP][MAXIMUM_NODES];

static short fake_substep_count = 1;
static real fake_fraction = 1.0f;
static sim_substep_procedure slot_procedure;

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
	return object_get(object_get_ultimate_parent(index))->object.type == _object_type_vehicle ?
		_object_lane_macro : _object_lane_micro;
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
/* (the clock's time since its newest step, in ticks: these frames' part of a sub-step past it) */
static real game_time_get_step_age(void)
{
	real steps = fake_fraction * (real)fake_substep_count;

	return (steps - floorf(steps)) / (real)fake_substep_count;
}
static short fake_perspective = _director_perspective_first_person;
static short director_get_perspective(short local_player_index) { return fake_perspective; }
static long scripted_camera_object_relative_to(void) { return NONE; }
static boolean scripted_camera_object_is_first_person_camera(long index) { return FALSE; }
static boolean cinematic_in_progress(void) { return FALSE; }
/* the player's unit (NONE while dead) */
static long fake_player_unit = BIPED;
static long player_control_get_unit_index(short local_player_index) { return fake_player_unit; }
static void unit_get_camera_position(long index, real_point3d *position) { *position = object_get(index)->object.position; }
struct interpolation_ghost { boolean in_use; long tick; long step; short substep_count; };
static struct interpolation_ghost ghosts[64];
static void render_interpolation_ghosts_tick(void) {}

#include "under_test.inc"

/* ---------- the world */

static real seconds_per_tick(void) { return 1.0f / 30.0f; }

static void object_add(long index, short type, long parent, short node_count)
{
	long at = absolute(index);
	short node_index;

	exists[at] = TRUE;
	identifiers[at] = index;
	memset(&objects[at], 0, sizeof(objects[at]));
	objects[at].object.type = type;
	objects[at].object.parent_object_index = parent;
	objects[at].object.node_matrices.size = (short)(node_count * sizeof(real_matrix4x3));
	for (node_index = 0; node_index < node_count; node_index++)
	{
		node_offsets[at][node_index].i = 0.0f;
		node_offsets[at][node_index].j = 0.0f;
		node_offsets[at][node_index].k = 0.1f * node_index;
	}
}

/* the nodes where the object is: an identity basis about its place, its pose's offsets */
static void object_pose(long index)
{
	long at = absolute(index);
	short node_index;
	short node_count = (short)(objects[at].object.node_matrices.size / sizeof(real_matrix4x3));

	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_matrix4x3 *m = &world_nodes[at][node_index];

		memset(m, 0, sizeof(*m));
		m->forward.i = 1.0f;
		m->left.j = 1.0f;
		m->up.k = 1.0f;
		m->scale = 1.0f;
		m->position.x = objects[at].object.position.x + node_offsets[at][node_index].i;
		m->position.y = objects[at].object.position.y + node_offsets[at][node_index].j;
		m->position.z = objects[at].object.position.z + node_offsets[at][node_index].k;
	}
}

static void object_place(long index, real x, real y, real z)
{
	struct object_datum *object = object_get(index);

	object->object.position.x = x;
	object->object.position.y = y;
	object->object.position.z = z;
	object_pose(index);
}

/* the biped's curve, by the simulation's time: running, turning, in the air (world units) */
static real_point3d biped_at(real t)
{
	real_point3d p;

	p.x = 3.0f * t;
	p.y = 0.6f * (real)sin(4.0 * t);
	p.z = 0.5f * (real)fabs(sin(3.0 * t));
	return p;
}

/* the world's state: the time, the ticks ended, the open tick's sub-steps run */
static long ticks_ended;
static short substeps_run;
/* the biped's place after each sub-step of each tick, the test's own record */
#define MAXIMUM_TICKS 128
static real_point3d biped_positions[MAXIMUM_TICKS][MAXIMUM_SIM_SUBSTEPS];
/* the projectile and grenade, moved by the case's procedure (NULL: none) */
static void (*case_substep)(long tick, short substep_index);

/* the carried weapon in the biped's hand */
static void carried_follow(void)
{
	if (valid(WEAPON))
	{
		struct object_datum *biped = object_get(BIPED);

		object_place(WEAPON, biped->object.position.x + 0.2f, biped->object.position.y, biped->object.position.z + 0.5f);
	}
	if (valid(GRENADE) && object_get(GRENADE)->object.parent_object_index == BIPED)
	{
		struct object_datum *biped = object_get(BIPED);

		object_place(GRENADE, biped->object.position.x - 0.1f, biped->object.position.y + 0.3f,
			biped->object.position.z + 0.4f);
	}
}

/* a sub-step of the open tick (the tick's own work runs before its first: case_substep's -1) */
static void world_substep(void)
{
	long tick = ticks_ended + 1;
	real t = ((real)tick - 1.0f + (real)(substeps_run + 1) / (real)fake_substep_count) * seconds_per_tick();
	real_point3d p = biped_at(t);

	CHECK(tick < MAXIMUM_TICKS, "too many ticks");
	if (substeps_run == 0 && case_substep)
		case_substep(tick, -1);
	object_place(BIPED, p.x, p.y, p.z);
	carried_follow();
	if (case_substep)
		case_substep(tick, substeps_run);
	biped_positions[tick][substeps_run] = p;
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
	case_substep = NULL;
	fake_player_unit = BIPED;
	fake_perspective = _director_perspective_first_person;
	render_interpolation_register();
	object_add(BIPED, _object_type_biped, NONE, 3);
	object_place(BIPED, 0.0f, 0.0f, 0.0f);
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

static real_point3d point_between(real_point3d const *a, real_point3d const *b, real t)
{
	real_point3d p;

	p.x = a->x + (b->x - a->x) * t;
	p.y = a->y + (b->y - a->y) * t;
	p.z = a->z + (b->z - a->z) * t;
	return p;
}

/* the biped's place on its sub-steps' path, as a frame of the tick drawn should have it */
static real_point3d biped_on_path(void)
{
	long tick = ticks_ended;
	short j;
	real fs;
	real_point3d const *before;

	sim_substep_blend(fake_fraction, &j, &fs);
	before = j > 0 ? &biped_positions[tick][j - 1] : &biped_positions[tick - 1][fake_substep_count - 1];
	return point_between(before, &biped_positions[tick][j], fs);
}

/* the biped's place on the line between the drawn tick's snapshots */
static real_point3d biped_on_line(void)
{
	long tick = ticks_ended;

	return point_between(&biped_positions[tick - 1][fake_substep_count - 1],
		&biped_positions[tick][fake_substep_count - 1], fake_fraction);
}

/* the frames' times drawn by the test's own generator (xorshift32), not the C library's rand(), whose numbers
differ from system to system: a case draws the same frames everywhere, from the seed it is run with */
static unsigned long random_state = 7;

static void random_seed(unsigned long seed)
{
	random_state = (seed & 0xFFFFFFFFUL) ? (seed & 0xFFFFFFFFUL) : 7;
}

static real random_part(void)
{
	random_state ^= (random_state << 13) & 0xFFFFFFFFUL;
	random_state ^= random_state >> 17;
	random_state ^= (random_state << 5) & 0xFFFFFFFFUL;
	return (real)(random_state % 1000) / 1000.0f;
}

/* the observer as the main loop has it after the frame's sub-steps: the biped's eye, now */
static struct observer_result observer_now(void)
{
	struct observer_result observer;

	memset(&observer, 0, sizeof(observer));
	observer.position = object_get(BIPED)->object.position;
	observer.position.z += EYE;
	observer.forward.i = 1.0f;
	observer.up.k = 1.0f;
	observer.field_of_view = 1.2f;
	return observer;
}

/* ---------- cases */

/* At 30 a tick is one sub-step: the frames draw the line between the ticks' snapshots, as they always did */
static void case_classic(void)
{
	long frame;

	world_new(1);
	for (frame = 0; frame < 200; frame++)
	{
		real_matrix4x3 const *drawn;
		real_point3d line;

		if (frame % 3 == 0)
			world_substep();
		if (ticks_ended < 3)
			continue;
		frame_begin(random_part());
		drawn = render_interpolation_object_node_matrices(BIPED);
		CHECK(drawn, "frame %ld: not drawn between the ticks", frame);
		line = biped_on_line();
		CHECK(distance(&drawn[0].position, &line) < 1e-5f, "frame %ld: off the line between the ticks by %f", frame,
			distance(&drawn[0].position, &line));
		render_interpolation_frame_end();
	}
}

/* At 60 or 120, a frame after each sub-step (part of one since): the biped and its view are drawn on its sub-steps'
path, which strays from the line between the ticks */
static void substeps_case(short substep_count, real part_scale)
{
	long frame;
	real largest_stray = 0.0f;

	world_new(substep_count);
	for (frame = 0; frame < 160; frame++)
	{
		real_matrix4x3 const *drawn;
		real_point3d path, line, eye;
		struct observer_result observer;
		struct observer_result const *camera;

		world_substep();
		if (ticks_ended < 1)
			continue;
		frame_begin(random_part() * part_scale);
		observer = observer_now();
		camera = render_interpolation_blended_camera(0, &observer);
		if (ticks_ended < 3)
		{
			/* (the view's snapshots begun) */
			render_interpolation_frame_end();
			continue;
		}
		drawn = render_interpolation_object_node_matrices(BIPED);
		CHECK(drawn, "frame %ld: not drawn between the ticks", frame);
		path = biped_on_path();
		line = biped_on_line();
		CHECK(distance(&drawn[0].position, &path) < 1e-5f, "frame %ld: the biped off its sub-steps' path by %f "
			"(%f from the line between the ticks)", frame, distance(&drawn[0].position, &path),
			distance(&drawn[0].position, &line));
		if (distance(&path, &line) > largest_stray)
			largest_stray = distance(&path, &line);
		eye = path;
		eye.z += EYE;
		CHECK(distance(&camera->position, &eye) < 1e-5f, "frame %ld: the view off the biped's sub-steps' path by %f",
			frame, distance(&camera->position, &eye));
		render_interpolation_frame_end();
	}
	CHECK(largest_stray > 1e-3f, "the path never left the line (%f): the case shows nothing", largest_stray);
}

static void case_carried(void)
{
	long frame;

	world_new(4);
	object_add(WEAPON, _object_type_weapon, BIPED, 1);
	carried_follow();
	for (frame = 0; frame < 120; frame++)
	{
		real_matrix4x3 const *drawn;
		real_point3d expected;

		world_substep();
		if (ticks_ended < 3)
			continue;
		frame_begin(random_part());
		drawn = render_interpolation_object_node_matrices(WEAPON);
		CHECK(drawn, "frame %ld: the weapon not drawn between the ticks", frame);
		expected = biped_on_path();
		expected.x += 0.2f;
		expected.z += 0.5f;
		CHECK(distance(&drawn[0].position, &expected) < 1e-5f, "frame %ld: the carried weapon off its carrier's path "
			"by %f", frame, distance(&drawn[0].position, &expected));
		render_interpolation_frame_end();
	}
}

/* a frame slower than a sub-step: two (or three) a frame, the frame after a tick's end with the next tick's first
in it, which the observer has (the view drawn from snapshots moved back to the tick's end) */
static void case_camera_slow(void)
{
	long frame;

	world_new(4);
	for (frame = 0; frame < 80; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
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
		eye = biped_on_path();
		eye.z += EYE;
		CHECK(distance(&camera->position, &eye) < 1e-5f, "frame %ld: the view off the biped's sub-steps' path by %f",
			frame, distance(&camera->position, &eye));
		render_interpolation_frame_end();
	}
}

/* A respawn: dead (no unit), the game's camera the dead camera, 1.5 units behind and above where the player will be,
looking down at 37 degrees (nearer, and turned less, than a cut is taken for); then the player's unit, the game's
camera its eye at once (player_spawn: observer_obsolete_position). Every frame from the respawn's tick on is drawn
at the eye, never on the way there from the dead camera (at 30, and at 120 the micro lane's view on its sub-steps) */
static void respawn_case(short substep_count)
{
	long frame;
	long respawn_tick = 6;
	boolean back_seen = FALSE;

	world_new(substep_count);
	for (frame = 0; frame < 30 * substep_count; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		boolean back;

		world_substep();
		if (ticks_ended < 1)
			continue;
		back = ticks_ended >= respawn_tick;
		fake_player_unit = back ? BIPED : NONE;
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

			/* (the eye moves 0.1 a tick: within a tick's of it, not 1.5 off on the way in) */
			CHECK(distance(&camera->position, &now.position) < 0.2f, "frame %ld (tick %ld, the respawn's %ld): the "
				"view %f from the new eye, on its way in from the dead camera", frame, ticks_ended, respawn_tick,
				distance(&camera->position, &now.position));
			CHECK(camera->forward.k > -0.01f, "frame %ld: the view still turning down from the dead camera's (%f)",
				frame, camera->forward.k);
			back_seen = TRUE;
		}
		render_interpolation_frame_end();
	}
	CHECK(back_seen, "no frame after the respawn");
}

/* A projectile made (in the tick's own work, or in sub-step 2 by the weapons' slot) at M with velocity v a tick:
its head start puts it at M + v at once, and it holds for a tick of sub-steps, then moves v/k each (projectiles.c).
It is drawn from its newest sub-steps (render_interpolation_projectile_lead), not a tick behind: from the frame after
the sub-step it was made in (kept first after), from half a tick's motion back (a whole tick back is M, for a bolt the
eye), reaching M + v as its hold ends, over the tick it holds: never darting out and waiting; then between its two
newest sub-steps, a sub-step behind the newest, never past it. */
static long projectile_made_tick;
static short projectile_made_substep;
static short projectile_held;
#define PROJECTILE_SPEED 0.9f
static real_point3d projectile_positions[MAXIMUM_TICKS][MAXIMUM_SIM_SUBSTEPS];
/* (projectile_turning) how far it turns about the up axis each sub-step it moves (radians), and has turned */
static real projectile_turn;
static real projectile_heading;

/* the projectile's nodes turned to its heading, about where it is */
static void projectile_turned(void)
{
	real_matrix4x3 *m = &object_get_node_matrices(PROJECTILE)[0];

	m->forward.i = (real)cos(projectile_heading);
	m->forward.j = (real)sin(projectile_heading);
	m->forward.k = 0.0f;
	m->left.i = -(real)sin(projectile_heading);
	m->left.j = (real)cos(projectile_heading);
	m->left.k = 0.0f;
}

static void projectile_substep(long tick, short substep_index)
{
	struct object_datum *projectile;

	if (tick == projectile_made_tick && substep_index == projectile_made_substep)
	{
		object_add(PROJECTILE, _object_type_projectile, NONE, 1);
		projectile = object_get(PROJECTILE);
		projectile->object.translational_velocity.i = PROJECTILE_SPEED;
		/* (its head start) */
		object_place(PROJECTILE, 1.0f + PROJECTILE_SPEED, 5.0f, 1.0f);
		projectile_held = (short)(substep_index < 0 ? fake_substep_count : fake_substep_count - 1);
		projectile_heading = 0.0f;
		projectile_turned();
	}
	else if (valid(PROJECTILE) && substep_index >= 0)
	{
		projectile = object_get(PROJECTILE);
		if (projectile_held > 0)
			projectile_held--;
		else
		{
			object_place(PROJECTILE, projectile->object.position.x + PROJECTILE_SPEED / fake_substep_count, 5.0f, 1.0f);
			projectile_heading += projectile_turn;
		}
		projectile_turned();
	}
	if (valid(PROJECTILE) && substep_index >= 0)
		projectile_positions[tick][substep_index] = object_get(PROJECTILE)->object.position;
}

static void projectile_case(short made_substep, long checked_ticks, real turn)
{
	long frame;
	short count = 4;
	long made_step;
	real head_start = 1.0f + PROJECTILE_SPEED;
	boolean seen = FALSE;

	world_new(count);
	projectile_made_tick = 5;
	projectile_made_substep = made_substep;
	projectile_turn = turn;
	/* (the sub-step it is first kept after, counted from the clock's first: the open tick's are ticks_ended + 1's) */
	made_step = projectile_made_tick * count + (made_substep < 0 ? 0 : made_substep);
	case_substep = projectile_substep;
	for (frame = 0; frame < count * 12; frame++)
	{
		real_matrix4x3 const *drawn;
		real part = random_part();
		/* the frame's time on the projectile's path, in sub-steps from the clock's first: a sub-step behind the
		newest run, the frame's part of a sub-step after the one before it */
		real time;
		real expected;

		world_substep();
		if (!valid(PROJECTILE) || ticks_ended >= projectile_made_tick + checked_ticks)
			continue;
		frame_begin(part);
		drawn = render_interpolation_object_node_matrices(PROJECTILE);
		time = (real)((ticks_ended + 1) * count + substeps_run - 2 - made_step) + part;
		/* (from half a tick back over the tick it holds, then on at v a tick) */
		if (time <= (real)(count - 1))
		{
			expected = head_start - 0.5f * PROJECTILE_SPEED +
				0.5f * PROJECTILE_SPEED * (time + 1.0f > 0.0f ? time + 1.0f : 0.0f) / (real)count;
		}
		else
		{
			expected = head_start + PROJECTILE_SPEED * (time - (real)(count - 1)) / (real)count;
		}
		CHECK(drawn && fabs(drawn[0].position.x - expected) < 1e-5f && fabs(drawn[0].position.y - 5.0f) < 1e-5f,
			"frame %ld (sub-step %ld, made at %ld:%d): drawn at %f, not %f", frame, ticks_ended * count + substeps_run,
			projectile_made_tick, made_substep, drawn ? drawn[0].position.x : -1.0f, expected);
		CHECK(drawn[0].position.x <= object_get(PROJECTILE)->object.position.x + 1e-5f,
			"frame %ld: drawn past where the newest sub-step left it (%f, %f)", frame, drawn[0].position.x,
			object_get(PROJECTILE)->object.position.x);
		/* turning as it moves: headed as far round as it is along (the sub-steps' headings blended as the
		quaternions are, near enough over a sub-step's small turn), not as the newest sub-step left it */
		if (turn != 0.0f)
		{
			real moved = time <= (real)(count - 1) ? 0.0f : time - (real)(count - 1);
			real heading = (real)atan2(drawn[0].forward.j, drawn[0].forward.i);

			CHECK(fabs(heading - turn * moved) < 1e-3f && fabs(drawn[0].forward.k) < 1e-5f &&
				fabs(drawn[0].up.k - 1.0f) < 1e-5f,
				"frame %ld: headed %f radians round, not %f (turning %f a sub-step)", frame, heading, turn * moved,
				turn);
		}
		seen = TRUE;
		render_interpolation_frame_end();
	}
	CHECK(seen, "the projectile never drawn");
}

/* The VR mode's grenade, in the biped's hand (the body's, not drawn in first person), let go of in sub-step 1 by
the VR slot at the hand H (render_interpolation_object_from), its head start and hold begun (vr_grenade.c): drawn
from its newest sub-steps, from H, where the hand last drew it, from the frame after its release. No frame jumps
further than its motion allows. */
static real_point3d hand = { 2.0f, -1.0f, 1.5f };
static long release_tick;

static void vr_substep(long tick, short substep_index)
{
	if (tick == release_tick && substep_index == 1)
	{
		struct object_datum *grenade = object_get(GRENADE);

		grenade->object.parent_object_index = NONE;
		/* (slower than its head start went: the head start's drag) */
		grenade->object.translational_velocity.i = 0.5f;
		object_place(GRENADE, hand.x, hand.y, hand.z);
		render_interpolation_object_from(GRENADE, &hand);
		/* (its head start) */
		object_place(GRENADE, hand.x + 0.6f, hand.y, hand.z);
	}
	else if (tick == release_tick + 1 && substep_index >= 1)
	{
		struct object_datum *grenade = object_get(GRENADE);

		object_place(GRENADE, grenade->object.position.x + 0.15f, hand.y, hand.z);
	}
}

static void case_vr_release(void)
{
	long frame;
	boolean pending_seen = FALSE, released_seen = FALSE;
	real_point3d last;
	boolean has_last = FALSE;

	world_new(4);
	object_add(GRENADE, _object_type_projectile, BIPED, 1);
	carried_follow();
	release_tick = 6;
	case_substep = vr_substep;
	for (frame = 0; frame < 4 * 9; frame++)
	{
		real_matrix4x3 const *drawn;
		real part = random_part();

		world_substep();
		if (ticks_ended < release_tick - 1)
			continue;
		frame_begin(part);
		drawn = render_interpolation_object_node_matrices(GRENADE);
		if (object_get(GRENADE)->object.parent_object_index == NONE)
		{
			CHECK(drawn, "frame %ld: the grenade let go of not drawn", frame);
			if (!pending_seen)
			{
				/* (its first frame: the frame's part of a sub-step's share of the way from the hand to where its head
				start put it) */
				CHECK(fabs(drawn[0].position.x - (hand.x + 0.6f * part / 4.0f)) < 1e-5f, "frame %ld: the grenade let "
					"go of first drawn %f from the hand, not %f", frame, distance(&drawn[0].position, &hand),
					0.6f * part / 4.0f);
				pending_seen = TRUE;
			}
			else
			{
				released_seen = TRUE;
			}
			/* (no pop: no frame further from the last than a frame of its flight) */
			if (has_last)
			{
				CHECK(distance(&drawn[0].position, &last) < 0.35f, "frame %ld: the grenade jumped %f", frame,
					distance(&drawn[0].position, &last));
			}
			last = drawn[0].position;
			has_last = TRUE;
		}
		render_interpolation_frame_end();
	}
	CHECK(pending_seen && released_seen, "the release's frames not seen (%d %d)", pending_seen, released_seen);
	CHECK(distance(&last, &hand) > 0.5f, "the grenade never left the hand");
}

/* Another machine's player in a network game (render_interpolation_lead_wanted): drawn between its two newest
sub-steps, the frame's part of a sub-step after the newer, not between the tick drawn's a tick behind: never more
than a sub-step behind the newest the clock ran, and never past it. The carried weapon goes with it. With glide, the
lead is let go of at tick 12 (a lift starting under it) and taken again at tick 20: the frames glide from one to the
other over 4 ticks of their time, never a jump */
static real_point3d biped_step(long tick_drawn, short index)
{
	return index < fake_substep_count ? biped_positions[tick_drawn][index] :
		biped_positions[tick_drawn + 1][index - fake_substep_count];
}

static void lead_case(short substep_count, boolean glide)
{
	long frame;
	real weight = 1.0f;
	real last_time = 0.0f;
	real largest_lead = 0.0f;
	boolean released_seen = FALSE, taken_seen = FALSE;

	world_new(substep_count);
	object_add(WEAPON, _object_type_weapon, BIPED, 1);
	carried_follow();
	lead_wanted_fake = TRUE;
	for (frame = 0; frame < 40 * substep_count; frame++)
	{
		real_matrix4x3 const *drawn, *weapon;
		real_point3d path, lead, expected, held;
		short newest;
		real part, time;

		world_substep();
		if (frame % 5 == 0)
			world_substep();
		if (glide)
			lead_wanted_fake = ticks_ended < 12 || ticks_ended >= 20;
		part = random_part();
		frame_begin(part);
		if (ticks_ended < 3)
		{
			render_interpolation_frame_end();
			continue;
		}
		/* (the frames' time, as the glide counts it) */
		time = (real)ticks_ended + fake_fraction;
		if (ticks_ended > 3)
		{
			real step = (time - last_time) / 4.0f;

			weight = lead_wanted_fake ? (weight + step < 1.0f ? weight + step : 1.0f) :
				(weight - step > 0.0f ? weight - step : 0.0f);
		}
		last_time = time;
		drawn = render_interpolation_object_node_matrices(BIPED);
		CHECK(drawn, "frame %ld: not drawn", frame);
		path = biped_on_path();
		newest = (short)(substeps_run + substep_count - 1);
		{
			real_point3d from = biped_step(ticks_ended, (short)(newest - 1));
			real_point3d to = biped_step(ticks_ended, newest);

			lead = point_between(&from, &to, part);
		}
		expected = point_between(&path, &lead, weight);
		CHECK(distance(&drawn[0].position, &expected) < 1e-4f, "frame %ld (tick %ld, sub-step %d, weight %f): drawn "
			"%f from its newest sub-steps' (%f from the tick drawn's path, %f from its lead)", frame, ticks_ended,
			substeps_run, weight, distance(&drawn[0].position, &expected), distance(&drawn[0].position, &path),
			distance(&drawn[0].position, &lead));
		if (weight == 1.0f && distance(&lead, &path) > largest_lead)
			largest_lead = distance(&lead, &path);
		if (weight == 0.0f)
			released_seen = TRUE;
		if (released_seen && weight == 1.0f)
			taken_seen = TRUE;
		weapon = render_interpolation_object_node_matrices(WEAPON);
		held = expected;
		held.x += 0.2f;
		held.z += 0.5f;
		CHECK(weapon && distance(&weapon[0].position, &held) < 1e-4f, "frame %ld: the carried weapon %f off its "
			"carrier", frame, weapon ? distance(&weapon[0].position, &held) : -1.0f);
		render_interpolation_frame_end();
	}
	CHECK(largest_lead > 0.05f, "the lead never left the tick drawn's path (%f): the case shows nothing", largest_lead);
	if (glide)
		CHECK(released_seen && taken_seen, "the glide not seen both ways (%d %d)", released_seen, taken_seen);
}

/* A cutscene's scripted camera on its way between two camera points, which the observer moves once a frame by the
frame's time: the observer a frame leaves is the camera at the frame's moment, a slow move (a few tenths of a unit a
tick). Frames come at any part of a tick, several in a tick or one every tick and a half; the camera is drawn where
it was a tick before the frame's moment, as the objects are, at every frame rate (#805: blended from snapshots taken
as if each were the tick's, it lurched ahead and back at each tick by the part of a tick its first frame came) */
static double scripted_speed = 0.3;
/* a cut to another camera point at this moment (0: none) */
static double scripted_cut;

static real scripted_path(double moment)
{
	return (real)(scripted_speed * moment + 0.001 * moment * moment +
		(scripted_cut > 0.0 && moment >= scripted_cut ? 100.0 : 0.0));
}

/* ... a fast one (d40's, c40's: nearly a cut's distance a tick), whose snapshots are often more than a cut's distance
apart (a frame more than a tick after the last): drawn a tick behind as the slow one, not from the latest snapshot a
tick ahead at random ticks (#805); and a cut to another point still cut, not glided through */
static void scripted_camera_case(short substep_count, double speed, double cut)
{
	double moment = 0.0;
	long frame;

	world_new(substep_count);
	fake_perspective = _director_perspective_scripted;
	scripted_speed = speed;
	scripted_cut = cut;
	for (frame = 0; frame < 300; frame++)
	{
		struct observer_result observer;
		struct observer_result const *camera;
		double drawn_moment;
		real error;

		/* (the next frame's moment: some frames close together, some more than a tick apart) */
		moment += frame % 7 == 3 ? 1.0 + 0.6 * random_part() : 0.05 + 0.4 * random_part();
		/* (as many ticks as the world keeps) */
		if (moment > (double)(MAXIMUM_TICKS - 4))
			break;
		while ((double)(ticks_ended * substep_count + substeps_run + 1) <= moment * substep_count)
			world_substep();
		frame_begin((real)(moment * substep_count - (double)(ticks_ended * substep_count + substeps_run)));
		memset(&observer, 0, sizeof(observer));
		observer.position.x = scripted_path(moment);
		observer.position.y = 5.0f;
		observer.forward.i = 1.0f;
		observer.up.k = 1.0f;
		observer.field_of_view = 1.2f;
		camera = render_interpolation_blended_camera(0, &observer);
		drawn_moment = (double)ticks_ended + (double)fake_fraction - 1.0;
		if (ticks_ended < 4)
		{
			render_interpolation_frame_end();
			continue;
		}
		if (cut > 0.0)
		{
			/* (either side of the cut, never between) */
			real before = (real)(speed * drawn_moment + 0.001 * drawn_moment * drawn_moment);

			CHECK(fabsf(camera->position.x - before) < 2.0f * (real)speed + 0.5f ||
				fabsf(camera->position.x - before - 100.0f) < 2.0f * (real)speed + 0.5f,
				"frame %ld (moment %.3f): the camera %f between the cut's two points (%f, %f)", frame, moment,
				camera->position.x, before, before + 100.0f);
			/* (from the cut's frame until a tick past the snapshot after it: drawn from either point) */
			if (moment >= cut && drawn_moment < cut + 1.7)
			{
				render_interpolation_frame_end();
				continue;
			}
		}
		error = camera->position.x - scripted_path(drawn_moment);
		CHECK(fabsf(error) < 2e-3f, "frame %ld (moment %.3f): the camera %f from where it was a tick before (%f): "
			"%.2f of a tick's move", frame, moment, camera->position.x, scripted_path(drawn_moment),
			error / (real)speed);
		CHECK(camera->field_of_view == 1.2f, "frame %ld: the field of view %f", frame, camera->field_of_view);
		render_interpolation_frame_end();
	}
}

/* An object whose part an animation carries away from its root faster each tick (a30's lifepod's body, dropping from
its root): further in a tick than a limb moves, but steadily, so blended between the ticks as any object, not drawn
stepping from tick to tick as a new pose each tick (#805); a part that jumps at once from rest is a new pose still,
drawn as the latest snapshot has it */
#define STEADY_POSED_TICK 20

static real steady_part_offset(long tick)
{
	return (real)(0.035 * tick * (tick + 1));
}

static void steady_part_substep(long tick, short substep_index)
{
	long at = absolute(BIPED);

	if (substep_index != -1)
		return;
	node_offsets[at][2].k = 0.2f + steady_part_offset(tick);
	node_offsets[at][1].i = tick >= STEADY_POSED_TICK ? 1.0f : 0.0f;
}

static void steady_part_case(void)
{
	real_point3d snapshots[MAXIMUM_TICKS][MAXIMUM_NODES];
	long frame;
	long recorded = -1;

	world_new(1);
	case_substep = steady_part_substep;
	for (frame = 0; frame < 4 * 34; frame++)
	{
		real_matrix4x3 const *drawn;
		real_point3d expected;
		real fraction;
		short node_index;

		if (frame % 4 == 0)
			world_substep();
		if (recorded != ticks_ended)
		{
			for (node_index = 0; node_index < MAXIMUM_NODES; node_index++)
				snapshots[ticks_ended][node_index] = world_nodes[absolute(BIPED)][node_index].position;
			recorded = ticks_ended;
		}
		if (ticks_ended < 3)
			continue;
		fraction = 0.1f + 0.25f * (real)(frame % 4);
		frame_begin(fraction);
		drawn = render_interpolation_object_node_matrices(BIPED);
		CHECK(drawn, "frame %ld: not drawn between the ticks", frame);
		for (node_index = 1; node_index < MAXIMUM_NODES; node_index++)
		{
			if (ticks_ended == STEADY_POSED_TICK)
				expected = snapshots[ticks_ended][node_index];
			else
				expected = point_between(&snapshots[ticks_ended - 1][node_index], &snapshots[ticks_ended][node_index],
					fraction);
			CHECK(distance(&drawn[node_index].position, &expected) < 1e-4f,
				"tick %ld, %.2f of a tick: node %d drawn %f from %s (its part moved %f in the tick)", ticks_ended,
				fraction, node_index, distance(&drawn[node_index].position, &expected),
				ticks_ended == STEADY_POSED_TICK ? "the new pose" : "the line between the ticks",
				steady_part_offset(ticks_ended) - steady_part_offset(ticks_ended - 1));
		}
		render_interpolation_frame_end();
	}
	CHECK(steady_part_offset(33) - steady_part_offset(32) > 2.0f,
		"the part never moved far in a tick: the case shows nothing");
}

int main(int argc, char **argv)
{
	char name[64];
	char *at;

	CHECK(argc == 2, "usage: %s <case>[@<seed>]", argv[0]);
	/* (<case>@<seed>: the frames drawn from another seed) */
	snprintf(name, sizeof(name), "%s", argv[1]);
	at = strchr(name, '@');
	random_seed(7);
	if (at)
	{
		*at = 0;
		random_seed(strtoul(at + 1, NULL, 10));
	}
	argv[1] = name;
	if (!strcmp(argv[1], "classic"))
		case_classic();
	else if (!strcmp(argv[1], "substeps"))
		substeps_case(4, 1.0f);
	else if (!strcmp(argv[1], "substeps_60"))
		substeps_case(2, 1.0f);
	else if (!strcmp(argv[1], "carried"))
		case_carried();
	else if (!strcmp(argv[1], "camera_slow"))
		case_camera_slow();
	else if (!strcmp(argv[1], "projectile"))
		projectile_case(-1, 8, 0.0f);
	else if (!strcmp(argv[1], "projectile_late"))
		projectile_case(2, 8, 0.0f);
	else if (!strcmp(argv[1], "projectile_turning"))
		projectile_case(-1, 8, 0.03f);
	else if (!strcmp(argv[1], "respawn"))
		respawn_case(1);
	else if (!strcmp(argv[1], "respawn_120"))
		respawn_case(4);
	else if (!strcmp(argv[1], "scripted_camera"))
		scripted_camera_case(1, 0.3, 0.0);
	else if (!strcmp(argv[1], "scripted_camera_120"))
		scripted_camera_case(4, 0.3, 0.0);
	else if (!strcmp(argv[1], "scripted_camera_fast"))
		scripted_camera_case(1, 2.9, 0.0);
	else if (!strcmp(argv[1], "scripted_camera_fast_120"))
		scripted_camera_case(4, 2.9, 0.0);
	else if (!strcmp(argv[1], "scripted_camera_cut"))
		scripted_camera_case(1, 2.9, 61.3);
	else if (!strcmp(argv[1], "steady_part"))
		steady_part_case();
	else if (!strcmp(argv[1], "vr_release"))
		case_vr_release();
	else if (!strcmp(argv[1], "lead"))
		lead_case(4, FALSE);
	else if (!strcmp(argv[1], "lead_60"))
		lead_case(2, FALSE);
	else if (!strcmp(argv[1], "lead_glide"))
		lead_case(4, TRUE);
	else if (!strcmp(argv[1], "lead_classic"))
	{
		lead_wanted_fake = TRUE;
		case_classic();
	}
	else
		CHECK(0, "no case %s", argv[1]);
	return 0;
}
