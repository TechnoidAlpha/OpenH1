/*
VR_GRENADE_THROW.C (test)

The VR mode's grenade thrown by the hand at 60 and 120 Hz
(port/linux/game/vr_grenade.c's vr_grenade_substep in the fast phase's VR
slot, and source/items/projectiles.c's projectile_substep_launch, on the
clock of port/linux/game/sim_rate.c): a fake hand lets go in the frame of
each sub-step j of a tick, and the grenade leaves at that sub-step, from the
hand (the eye that sub-step left), along its throw as hard as it threw it;
the throw animation goes on as at 30; and n ticks after the hand let go it
is as far from where it left as a grenade let go of at 30 is (n * v * T30,
on the same arc: its head start, then its hold). At 30 the throw is the
tick's alone. The projectiles' fake world is projectile_substeps.c's.
test_vr_grenade_throw.py takes the declarations (decls.inc) and the code
(under_test.inc) from the sources.
*/

#include "harness.h"

#include <math.h>

#define TICKS_PER_SECOND 30
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#define UNSIGNED_SHORT_MAX 0xFFFF
#define DATUM_INDEX_TO_IDENTIFIER(index) ((unsigned long)(index) >> 16)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define PIN(x, low, high) ((x) < (low) ? (low) : ((x) > (high) ? (high) : (x)))
#define SET_FLAG(flags, bit, value) \
	((value) ? ((flags) |= (unsigned)FLAG(bit)) : ((flags) &= ~(unsigned)FLAG(bit)))
#define _pi 3.14159265f

#include "decls.inc"

/* ---------- the fake world */

enum
{
	_game_connection_local = 0,
	_game_connection_network_client,
	_game_connection_network_server,
	_game_connection_film_playback,
};

enum
{
	_object_type_biped = 0,
	_object_type_projectile = 5,
	_object_mask_unit = 1 << _object_type_biped,
	_object_mask_projectile = 1 << _object_type_projectile,
};

enum
{
	_object_header_active_bit = 0,
	_object_header_visible_bit,
	_object_header_being_created_bit,
	_object_header_being_deleted_bit,
	_object_header_do_not_update_bit,
};

enum
{
	_object_wholly_under_media_bit = 0,
	_object_at_rest_bit,
	_object_do_not_recompute_node_matrices_bit,
};

enum
{
	_collision_user_projectiles = 1,
	MAXIMUM_COLLISION_USER_STACK_DEPTH = 8,
	MAXIMUM_LOCAL_PLAYERS = 1,
	_ai_spatial_effect_weapon_impact = 0,
	_game_difficulty_value_guidance_vs_player = 0,
	_periodic_function_wander = 0,
	MAXIMUM_PROJECTILES = 16,
};

typedef struct { real yaw, pitch; } real_euler_angles2d;
struct location { long cluster_index; };
struct plane3d { real_vector3d n; real d; };
struct collision_result { real t; real_point3d point; struct plane3d plane; struct location location; };
struct sound_location { real_point3d position; real_vector3d forward, translational_velocity; struct location game_location; };

struct _object_datum
{
	unsigned long flags;
	short type;
	long parent_object_index;
	real_point3d position;
	real_point3d bounding_sphere_center;
	real_vector3d forward;
	real_vector3d up;
	real_vector3d translational_velocity;
	real_vector3d angular_velocity;
	long attachment_indices[4];
	struct location location;
};

struct object_datum
{
	long definition_index;
	struct _object_datum object;
};

struct projectile_datum
{
	long definition_index;
	struct _object_datum object;
	struct _projectile_datum projectile;
};

struct projectile_definition
{
	struct
	{
		unsigned long flags;
		short detonation_timer_starts;
		real guided_angular_velocity;
		real final_velocity;
		real maximum_range;
		real timer_upper_bound;
		real minimum_velocity;
		real air_gravity_scale;
		real water_gravity_scale;
		real impact_noise;
		struct { long index; } flyby_sound;
	} projectile;
};

struct object_header_datum
{
	short identifier;
	unsigned short flags;
	short type;
	void *datum;
};

struct object_iterator
{
	unsigned long type_flags;
	byte flags;
	short absolute_index;
	long index;
};

/* the world: the projectiles, a floor, gravity, the clock */
static struct projectile_datum projectiles[MAXIMUM_PROJECTILES];
static struct object_header_datum headers[MAXIMUM_PROJECTILES];
static struct projectile_definition definition;
static real global_gravity = 0.0035651792f;
static real floor_z = -1.0e9f;
/* (0: a strike stops it; else it bounces off the floor, its upward speed
this share of its downward) */
static real restitution = 0.0f;
static long game_time;
static long setting = 30;
static unsigned long changes = 1;
static short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];
static short global_current_collision_user_depth = 1;
static real_vector3d const zero_vector3d = { 0.0f, 0.0f, 0.0f };
static real_vector3d const *global_zero_vector3d = &zero_vector3d;
/* the VR player's unit (not in the projectiles' array), its definition and
its player */
#define UNIT_INDEX 0x70000000L
typedef struct { real scale; real_vector3d forward, left, up; real_point3d position; } real_matrix4x3;
struct unit_definition { struct { real grenade_velocity; } unit; };
struct unit_datum
{
	long definition_index;
	struct
	{
		long parent_object_index;
		real_vector3d translational_velocity;
		struct { struct { short frame_index; } state; } animation;
	} object;
	struct
	{
		long player_index;
		char grenade_throw_state;
		long grenade_object_index;
		real_vector3d aiming_vector;
		struct { short state; } animation;
	} unit;
};
struct player_datum { long unit_index; };
static struct unit_datum fake_unit;
static struct unit_definition fake_unit_definition = { { 7.5f } };
static struct player_datum fake_player = { UNIT_INDEX };

/* what happened: each projectile's strikes (their times in ticks, their
points), its detonation (the tick it was in) */
enum { MAXIMUM_STRIKES = 4 };
static struct
{
	boolean struck;
	double strike_ticks;
	real_point3d strike_point;
	short strike_count;
	double strikes_ticks[MAXIMUM_STRIKES];
	real_point3d strikes[MAXIMUM_STRIKES];
	boolean detonated;
	long detonation_tick;
	short detonation_substep;
} events[MAXIMUM_PROJECTILES];

long config_integer(char const *name)
{
	return strcmp(name, "game.sim_rate") ? 0 : setting;
}

unsigned long config_changes(void)
{
	return changes;
}

static short game_connection(void)
{
	return _game_connection_local;
}

boolean network_game_is_splitscreen_local(void)
{
	return FALSE;
}

long network_standard_agreed_rate(void)
{
	return 0;
}

/* (port/linux/game/demo.c: no demo plays) */
long demo_playback_sim_rate(void)
{
	return 0;
}

static real square_root(real x)
{
	return (real)sqrt(x);
}

static long game_time_get(void)
{
	return game_time;
}

static boolean game_players_are_double_speed(void)
{
	return FALSE;
}

static long datum_of(long absolute_index)
{
	return ((long)(unsigned short)headers[absolute_index].identifier << 16) | absolute_index;
}

static struct object_header_datum *object_header_get(long index)
{
	return &headers[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)];
}

static struct projectile_datum *projectile_get(long index)
{
	return &projectiles[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)];
}

static struct object_datum *object_get(long index)
{
	return (struct object_datum *)projectile_get(index);
}

static struct projectile_definition const *projectile_definition_get(long index)
{
	return &definition;
}

static void object_iterator_new(struct object_iterator *iterator, unsigned long type_flags, byte flags)
{
	iterator->type_flags = type_flags;
	iterator->flags = flags;
	iterator->absolute_index = 0;
	iterator->index = NONE;
}

static void *object_iterator_next(struct object_iterator *iterator)
{
	while (iterator->absolute_index < MAXIMUM_PROJECTILES)
	{
		short index = iterator->absolute_index++;
		struct object_header_datum *header = &headers[index];

		if (header->identifier && (iterator->flags & header->flags) == iterator->flags &&
			TEST_FLAG(iterator->type_flags, header->type))
		{
			iterator->index = datum_of(index);
			return header->datum;
		}
	}
	return NULL;
}

/* vectors (real_math.h's) */
static real dot_product3d(real_vector3d const *a, real_vector3d const *b)
{
	return a->i * b->i + a->j * b->j + a->k * b->k;
}

static real magnitude_squared3d(real_vector3d const *v)
{
	return dot_product3d(v, v);
}

static real magnitude3d(real_vector3d const *v)
{
	return (real)sqrt(magnitude_squared3d(v));
}

static real_vector3d *scale_vector3d(real_vector3d const *v, real s, real_vector3d *out)
{
	out->i = v->i * s;
	out->j = v->j * s;
	out->k = v->k * s;
	return out;
}

static real normalize3d(real_vector3d *v)
{
	real m = magnitude3d(v);

	if (m != 0.0f)
		scale_vector3d(v, 1.0f / m, v);
	return m;
}

static real_vector3d *cross_product3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *out)
{
	real_vector3d r;

	r.i = a->j * b->k - a->k * b->j;
	r.j = a->k * b->i - a->i * b->k;
	r.k = a->i * b->j - a->j * b->i;
	*out = r;
	return out;
}

static real_vector3d *perpendicular3d(real_vector3d const *v, real_vector3d *out)
{
	out->i = -v->j;
	out->j = v->i;
	out->k = 0.0f;
	return out;
}

static real_point3d *point_from_line3d(real_point3d const *p, real_vector3d const *v, real t, real_point3d *out)
{
	out->x = p->x + v->i * t;
	out->y = p->y + v->j * t;
	out->z = p->z + v->k * t;
	return out;
}

static real_vector3d *vector_from_points3d(real_point3d const *a, real_point3d const *b, real_vector3d *out)
{
	out->i = b->x - a->x;
	out->j = b->y - a->y;
	out->k = b->z - a->z;
	return out;
}

static real distance3d(real_point3d const *a, real_point3d const *b)
{
	real_vector3d v;

	return magnitude3d(vector_from_points3d(a, b, &v));
}

static void rotate_vector_about_axis(real_vector3d *v, real_vector3d const *axis, real sine, real cosine)
{
	real_vector3d cross;
	real dot = dot_product3d(axis, v);

	cross_product3d(axis, v, &cross);
	v->i = v->i * cosine + cross.i * sine + axis->i * dot * (1.0f - cosine);
	v->j = v->j * cosine + cross.j * sine + axis->j * dot * (1.0f - cosine);
	v->k = v->k * cosine + cross.k * sine + axis->k * dot * (1.0f - cosine);
}

static void component_vectors_from_direction3d(real_vector3d const *v, real_vector3d const *direction,
	real_vector3d *parallel, real_vector3d *perpendicular)
{
	real_vector3d unit = *direction;

	normalize3d(&unit);
	scale_vector3d(&unit, dot_product3d(v, &unit), parallel);
	perpendicular->i = v->i - parallel->i;
	perpendicular->j = v->j - parallel->j;
	perpendicular->k = v->k - parallel->k;
}

#define sine(x) ((real)sin(x))
#define cosine(x) ((real)cos(x))
#define profile_enter(section) ((void)0)
#define profile_exit(section) ((void)0)
#define match_vassert(file, line, condition, message) ((void)0)
#define match_assert_valid_real_vector3d(file, line, v) ((void)0)
#define match_assert_valid_real_point3d(file, line, p) ((void)0)
#define match_assert_valid_real_vector3d_axes2(file, line, a, b) ((void)0)
#define contrail_delete(index) ((void)0)
#define contrail_owner_collision(index, a, seconds) ((void)0)
#define projectile_export_function_values(index) ((void)0)
#define object_compute_node_matrices(index) ((void)0)
#define object_delete_immediately(index) ((void)0)
#define ai_handle_spatial_effect(index, point, type, noise, count) ((void)0)
#define game_difficulty_get_value(value) 1.0f
#define unit_get_center_of_mass(index, point) (*(point) = projectile_get(index)->object.position)
#define periodic_function_evaluate(function, x) 0.0f
#define set_real_euler_angles2d(angles, y, p) ((angles)->yaw = (y), (angles)->pitch = (p))
#define vector3d_from_euler_angles2d(v, angles) \
	((v)->i = cosine((angles)->yaw) * cosine((angles)->pitch), (v)->j = sine((angles)->yaw) * cosine((angles)->pitch), \
	 (v)->k = sine((angles)->pitch))
#define sound_definition_get_maximum_distance(index) 0.0f
#define unattached_impulse_sound_new(index, location, scale) ((void)0)

static void object_translate(long index, real_point3d const *position, struct location const *location)
{
	projectile_get(index)->object.position = *position;
}

static void object_delete(long index)
{
	SET_FLAG(object_header_get(index)->flags, _object_header_being_deleted_bit, TRUE);
}

/* the floor, z = floor_z: a strike, where the projectile stops */
static boolean projectile_collision_test_line(long index, real_point3d const *new_position, struct collision_result *collision)
{
	real_point3d const *from = &projectile_get(index)->object.position;

	if (from->z > floor_z && new_position->z <= floor_z)
	{
		collision->t = (from->z - floor_z) / (from->z - new_position->z);
		collision->point.x = from->x + (new_position->x - from->x) * collision->t;
		collision->point.y = from->y + (new_position->y - from->y) * collision->t;
		collision->point.z = floor_z;
		collision->plane.n.i = 0.0f;
		collision->plane.n.j = 0.0f;
		collision->plane.n.k = 1.0f;
		collision->location.cluster_index = 0;
		return TRUE;
	}
	return FALSE;
}

static void projectile_set_action(long projectile_index, short action);

static void projectile_collision(long index, struct collision_result *collision, real_point3d *new_position,
	real_vector3d *new_velocity, real time_left)
{
	*new_position = collision->point;
	if (restitution > 0.0f)
	{
		new_position->z += 1e-4f;
		new_velocity->k = -new_velocity->k * restitution;
		return;
	}
	*new_velocity = zero_vector3d;
	projectile_set_action(index, _projectile_action_detonate);
}

static void projectile_detonate(long index, boolean first_collision, real time_left)
{
	events[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)].detonated = TRUE;
}

/* the trace's hooks: the strike's time as sim_trace.c takes it (in its
update, a sub-step's or the tick's, by the fraction found) */
short sim_substep_index(void);
short sim_substep_count(void);
#define SIM_TRACE(call) call

/* (objects.h's lanes: a projectile loose in the world is of the micro lane) */
static boolean object_lane_moves_in_substeps(long index)
{
	return sim_substep_count() > 1;
}
static void sim_trace_projectile_collision(long index, void const *point, void const *normal, float time_remaining,
	float fraction)
{
	double into_update = (1.0 - time_remaining) + fraction * time_remaining;
	short substep_index = sim_substep_index();
	double ticks = substep_index == NONE ? into_update : (substep_index + into_update) / sim_substep_count();
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);

	if (!events[absolute_index].struck)
	{
		events[absolute_index].struck = TRUE;
		events[absolute_index].strike_ticks = (double)game_time + ticks;
		events[absolute_index].strike_point = *(real_point3d const *)point;
	}
	if (events[absolute_index].strike_count < MAXIMUM_STRIKES)
	{
		events[absolute_index].strikes_ticks[events[absolute_index].strike_count] = (double)game_time + ticks;
		events[absolute_index].strikes[events[absolute_index].strike_count] = *(real_point3d const *)point;
		events[absolute_index].strike_count++;
	}
}

static void sim_trace_projectile_detonate(long index)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);

	events[absolute_index].detonation_tick = game_time;
	events[absolute_index].detonation_substep = sim_substep_index();
}


/* the units (units.c's, the unit's and its definition's), the players'
(players.c's): the one VR player */
static struct unit_datum *unit_get(long index)
{
	return &fake_unit;
}

static struct unit_definition *unit_definition_get(long index)
{
	return &fake_unit_definition;
}

static long local_player_get_player_index(short index)
{
	return 0;
}

static struct player_datum *player_get(long index)
{
	return &fake_player;
}

/* vectors (real_math.h's) */
static real_vector3d *add_vectors3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *out)
{
	out->i = a->i + b->i;
	out->j = a->j + b->j;
	out->k = a->k + b->k;
	return out;
}

static real_vector3d *subtract_vectors3d(real_vector3d const *a, real_vector3d const *b, real_vector3d *out)
{
	out->i = a->i - b->i;
	out->j = a->j - b->j;
	out->k = a->k - b->k;
	return out;
}

/* (the spin a throw gives a grenade: none, which its travel does not see) */
static void random_direction3d(real_vector3d *v)
{
	v->i = 0.0f;
	v->j = 0.0f;
	v->k = 1.0f;
}
#define real_random() 0.0f
static void projectile_adjust_for_angular_velocity_change(long index)
{
}

/* the frames' VR runtime (port/linux/src/vr.c's): the left hand's throw as
the frame's input left it, swinging then let go of, its release's pose */
static struct halo_vr_throw hand;

static boolean halo_vr_running(void)
{
	return TRUE;
}

static int halo_vr_throw(struct halo_vr_throw *throw_state)
{
	*throw_state = hand;
	return hand.phase;
}

static void halo_vr_throw_done(void)
{
	hand.phase = HALO_VR_THROW_NONE;
	hand.held = FALSE;
}

static void platform_log(char const *format, ...)
{
}

/* the unit's eye (units.c's unit_get_camera_position): walking along x a
little each tick, where the bipeds' slot left it (a sub-step's end), or
where the tick before left it (in the tick's own work) */
enum { EYE_TICKS = 3 };
static real const walk_per_tick = 0.03f;
static real const eye_height = 0.62f;

static double eye_time(void)
{
	short substep_index = sim_substep_index();

	return substep_index == NONE ? (double)game_time :
		(double)game_time + (double)(substep_index + 1) / sim_substep_count();
}

static void unit_get_camera_position(long unit_index, real_point3d *eye)
{
	eye->x = (real)(walk_per_tick * eye_time());
	eye->y = 0.0f;
	eye->z = eye_height;
}

/* what the release did: when (tick, sub-step, the eye's time), where from
(render_interpolation_object_from's place), the velocity as the release
left it (at object_force_inside_bsp, after projectile_accelerate) */
static struct
{
	boolean released;
	long tick;
	short substep;
	double eye_time;
	real_point3d from;
	real_vector3d velocity;
} release;

static void render_interpolation_object_from(long object_index, real_point3d const *position)
{
	release.from = *position;
}

static boolean object_force_inside_bsp(long object_index, real_point3d const *known_good_point)
{
	release.released = TRUE;
	release.tick = game_time;
	release.substep = sim_substep_index();
	release.eye_time = eye_time();
	release.velocity = projectile_get(object_index)->object.translational_velocity;
	return TRUE;
}

/* objects.c's: let go of by the unit, it moves with what carried it */
static void object_detach(long index)
{
	struct projectile_datum *projectile = projectile_get(index);

	projectile->object.parent_object_index = NONE;
	projectile->object.translational_velocity = fake_unit.object.translational_velocity;
}

#include "under_test.inc"

/* ---------- the game's tick, and a throw */

/* the throw: the tick its grenade comes to the hand (its animation's frame
then, and its key frame, the release), the hand's release's tick and
sub-step (the frame it let go in), and whether the tick may release it
(units.c's release, at its animation's key frame) */
enum { KEY_FRAME = 8, GRENADE_FRAME = 2 };
static struct
{
	long in_hand_tick;
	long release_tick;
	short release_substep;
	boolean tick_releases;
	long grenade;
} throw_plan;

/* the hand as it lets go: 0.3 m ahead, 0.25 m to the left and 0.1 m below
the head, along a throw to the forward left and up, at 0.8 of the game's
throw, the body turned 30 degrees; 1 / 3.048 world units a metre */
static void hand_let_go(void)
{
	hand.phase = HALO_VR_THROW_RELEASED;
	hand.held = TRUE;
	hand.position[0] = 0.3f;
	hand.position[1] = 0.25f;
	hand.position[2] = -0.1f;
	hand.direction[0] = 0.8f;
	hand.direction[1] = 0.3f;
	hand.direction[2] = 0.52f;
	hand.power = 0.8f;
	hand.body_yaw = 0.5235988f;
	hand.world_units_per_metre = 1.0f / 3.048f;
	hand.pitch = 0.5f;
}

/* where the release must be from, and its velocity: the hand from the eye
(eye_at), turned by the body; along its throw, the game's throw's speed by
its power */
static void expected_release(double eye_at, real_point3d *from, real_vector3d *velocity)
{
	real c = (real)cos(hand.body_yaw), s = (real)sin(hand.body_yaw);
	real units = hand.world_units_per_metre;
	real_vector3d direction;

	from->x = (real)(walk_per_tick * eye_at) + (hand.position[0] * c - hand.position[1] * s) * units;
	from->y = (hand.position[0] * s + hand.position[1] * c) * units;
	from->z = eye_height + hand.position[2] * units;
	direction.i = hand.direction[0] * c - hand.direction[1] * s;
	direction.j = hand.direction[0] * s + hand.direction[1] * c;
	direction.k = hand.direction[2];
	normalize3d(&direction);
	scale_vector3d(&direction, fake_unit_definition.unit.grenade_velocity / TICKS_PER_SECOND * hand.power, velocity);
}

/* the frame's input, before the clock runs a sub-step: the hand lets go in
the frame of the release's sub-step */
static void frame_input(short substep)
{
	if (game_time == throw_plan.release_tick && substep == throw_plan.release_substep)
		hand_let_go();
}

/* units.c's unit_throw_grenade_release for a player's grenade (in the tick,
at the throw animation's key frame) */
static void unit_throw_grenade_release(void)
{
	long grenade_index = fake_unit.unit.grenade_object_index;
	real_vector3d initial_velocity;
	real_point3d eye;

	if (fake_unit.unit.grenade_throw_state != _unit_grenade_throw_in_hand || grenade_index == NONE)
	{
		fake_unit.unit.grenade_throw_state = _unit_grenade_throw_ending;
		return;
	}
	object_detach(grenade_index);
	unit_get_camera_position(UNIT_INDEX, &eye);
	object_translate(grenade_index, &eye, NULL);
	scale_vector3d(&fake_unit.unit.aiming_vector, fake_unit_definition.unit.grenade_velocity / TICKS_PER_SECOND,
		&initial_velocity);
	vr_grenade_release(UNIT_INDEX, grenade_index, &initial_velocity);
	subtract_vectors3d(&initial_velocity, &projectile_get(grenade_index)->object.translational_velocity,
		&initial_velocity);
	projectile_accelerate(grenade_index, &initial_velocity);
	fake_unit.unit.grenade_throw_state = _unit_grenade_throw_ending;
	fake_unit.unit.grenade_object_index = NONE;
	object_force_inside_bsp(grenade_index, &eye);
}

/* units.c's unit_update for the throw: the grenade to the hand (made
there, attached to the unit), the animation on a frame, held or moved on by
the hand (vr_grenade_throw_advance), its key frame the release */
static void unit_update(void)
{
	short frame_index = fake_unit.object.animation.state.frame_index;

	if (game_time == throw_plan.in_hand_tick)
	{
		short absolute_index = 0;
		struct projectile_datum *projectile = &projectiles[absolute_index];

		memset(projectile, 0, sizeof(*projectile));
		headers[absolute_index].identifier = 0x1000;
		headers[absolute_index].type = _object_type_projectile;
		headers[absolute_index].flags = FLAG(_object_header_active_bit);
		headers[absolute_index].datum = projectile;
		projectile->object.type = _object_type_projectile;
		projectile->object.parent_object_index = UNIT_INDEX;
		projectile->object.forward.i = 1.0f;
		projectile->object.up.k = 1.0f;
		projectile->projectile.target_object_index = NONE;
		projectile->projectile.tracer_attachment_index = NONE;
		projectile->projectile.ignore_object_index = NONE;
		projectile->projectile.rotation_cosine = 1.0f;
		throw_plan.grenade = datum_of(absolute_index);
		fake_unit.unit.grenade_object_index = throw_plan.grenade;
		fake_unit.unit.grenade_throw_state = _unit_grenade_throw_in_hand;
		fake_unit.unit.animation.state = _unit_state_throw_grenade;
		fake_unit.object.animation.state.frame_index = GRENADE_FRAME;
		return;
	}
	if (fake_unit.unit.animation.state != _unit_state_throw_grenade)
		return;
	if (fake_unit.unit.grenade_throw_state == _unit_grenade_throw_in_hand)
	{
		if (!vr_grenade_throw_advance(UNIT_INDEX, &frame_index, KEY_FRAME, FALSE))
			return;
		fake_unit.object.animation.state.frame_index = frame_index;
	}
	if (++fake_unit.object.animation.state.frame_index == KEY_FRAME && throw_plan.tick_releases)
		unit_throw_grenade_release();
}

/* each sub-step's end: the grenade's place, in 1/4 ticks from the start,
and the unit's throw */
enum { MAXIMUM_STEPS = 64 * 4 };
static real_point3d places[MAXIMUM_STEPS];
static boolean placed[MAXIMUM_STEPS];
static boolean attached[MAXIMUM_STEPS];
static short frames[MAXIMUM_STEPS];

static void trace_substep(short substep_index, short substep_count, real seconds)
{
	long step = (game_time * 4) + (substep_index + 1) * (4 / substep_count);

	if (throw_plan.grenade != NONE && step < MAXIMUM_STEPS)
	{
		struct projectile_datum *projectile = projectile_get(throw_plan.grenade);

		places[step] = projectile->object.position;
		placed[step] = TRUE;
		attached[step] = projectile->object.parent_object_index != NONE;
		frames[step] = fake_unit.object.animation.state.frame_index;
	}
}

static void game_tick(void)
{
	short absolute_index;
	short substep;

	sim_rate_tick_begin();
	/* the tick's own work, in the frame of its first sub-step: the unit's
	update, then the projectiles' (the grenade, attached, in the unit's) */
	frame_input(0);
	unit_update();
	for (absolute_index = 0; absolute_index < MAXIMUM_PROJECTILES; absolute_index++)
	{
		if (headers[absolute_index].identifier)
			projectile_update(datum_of(absolute_index));
	}
	/* (the tick's sub-steps, each in its frame) */
	for (substep = 0; substep < sim_substep_count(); substep++)
	{
		if (substep > 0)
			frame_input(substep);
		sim_substep_run(substep);
	}
	game_time++;
}

enum { IN_HAND = 2, RELEASED = 4, TICKS = 30 };

/* a game at a rate: the throw, its grenade let go of by the hand in the
frame of a tick's sub-step, played some ticks */
static void play(long rate, long release_tick, short release_substep, boolean tick_releases, long ticks)
{
	long index;

	memset(projectiles, 0, sizeof(projectiles));
	memset(headers, 0, sizeof(headers));
	memset(events, 0, sizeof(events));
	memset(placed, 0, sizeof(placed));
	memset(&release, 0, sizeof(release));
	memset(&hand, 0, sizeof(hand));
	memset(&fake_unit, 0, sizeof(fake_unit));
	game_time = 0;
	setting = rate;
	changes++;
	throw_plan.in_hand_tick = IN_HAND;
	throw_plan.release_tick = release_tick;
	throw_plan.release_substep = release_substep;
	throw_plan.tick_releases = tick_releases;
	throw_plan.grenade = NONE;
	/* the unit walking along x, aiming along it; the hand swinging */
	fake_unit.object.parent_object_index = NONE;
	fake_unit.object.translational_velocity.i = walk_per_tick;
	fake_unit.unit.player_index = 0;
	fake_unit.unit.grenade_object_index = NONE;
	fake_unit.unit.aiming_vector.i = 1.0f;
	hand.phase = HALO_VR_THROW_SWING;
	hand.held = TRUE;
	vr_grenade.unit_index = NONE;
	vr_grenade.released = FALSE;
	vr_grenade.drawn_valid = FALSE;
	sim_substep_register(_sim_substep_projectiles, projectiles_substep);
	sim_substep_register(_sim_substep_vr, vr_grenade_substep);
	sim_substep_register(_sim_substep_trace, trace_substep);
	for (index = 0; index < ticks; index++)
		game_tick();
}

/* the grenade's place n ticks after the frame the hand let go in (the
start of its sub-step) */
static boolean place_after(long rate, long tick, short substep, long n, real_point3d *place)
{
	long step = tick * 4 + substep * (4 / (rate / 30)) + n * 4;

	if (step >= MAXIMUM_STEPS || !placed[step])
		return FALSE;
	*place = places[step];
	return TRUE;
}

static real distance(real_point3d const *a, real_point3d const *b)
{
	return distance3d(a, b);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static long const rates[] = { 60, 120 };
	unsigned int rate_index;

	definition.projectile.air_gravity_scale = 1.0f;
	definition.projectile.water_gravity_scale = 1.0f;
	definition.projectile.flyby_sound.index = NONE;
	definition.projectile.detonation_timer_starts = _projectile_detonation_timer_starts_immediately;

	/* at 30, the fast phase leaves the throw to the tick: the hand let go,
	the slot does nothing with the grenade in the hand (the tick's
	animation not releasing it here); and the tick's release is as it
	always was, none of the projectiles' sub-step bits set */
	CASE("classic")
	{
		real_point3d from;
		real_vector3d velocity;

		play(30, RELEASED, 0, FALSE, RELEASED + 3);
		CHECK(!release.released && fake_unit.unit.grenade_throw_state == _unit_grenade_throw_in_hand &&
			projectile_get(throw_plan.grenade)->object.parent_object_index == UNIT_INDEX,
			"at 30 the grenade left the hand outside the tick's release");
		play(30, RELEASED, 0, TRUE, RELEASED + 3);
		CHECK(release.released && release.tick == RELEASED && release.substep == NONE, "at 30: released at tick %ld sub-step %d",
			release.tick, release.substep);
		expected_release(RELEASED, &from, &velocity);
		CHECK(distance(&release.from, &from) < 1e-6f, "at 30: from (%g %g %g), not the hand (%g %g %g)", release.from.x,
			release.from.y, release.from.z, from.x, from.y, from.z);
		CHECK((projectile_get(throw_plan.grenade)->projectile.flags & PROJECTILE_SUBSTEP_FLAGS_MASK) == 0,
			"sub-step bits at 30");
		return 0;
	}

	/* the hand letting go in the frame of any sub-step of 60 or 120: the
	grenade leaves at that sub-step (still in the hand at the sub-step
	before's end, flying at its end), from the hand, the eye where that
	sub-step put it, along the hand's throw as hard as it threw it; the
	throw animation put on the frame before its release, as the next tick
	would put it at 30; and a whole number of ticks after the hand let go
	it is as far from where it left as a grenade let go of at 30 is that
	long after (n * v * T30 on the same arc: its head start, then its hold)
	*/
	CASE("throw")
	{
		real_point3d reference[TICKS], reference_from;
		boolean have[TICKS];
		long n;

		play(30, RELEASED, 0, TRUE, TICKS);
		reference_from = release.from;
		for (n = 0; n < TICKS; n++)
			have[n] = place_after(30, RELEASED, 0, n, &reference[n]);
		CHECK(have[1], "no place a tick after the release at 30");
		for (rate_index = 0; rate_index < NUMBEROF(rates); rate_index++)
		{
			long rate = rates[rate_index];
			short k = (short)(rate / 30);
			short j;

			for (j = 0; j < k; j++)
			{
				long release_step = RELEASED * 4 + (j + 1) * (4 / k);
				real_point3d from;
				real_vector3d velocity;

				play(rate, RELEASED, j, TRUE, TICKS);
				CHECK(release.released, "%ld Hz sub-step %d: not released", rate, j);
				/* its time: at that sub-step (in the tick's own work, the first
				sub-step's frame, for the first) */
				CHECK(release.tick == RELEASED && release.substep == (j == 0 ? NONE : j),
					"%ld Hz sub-step %d: released at tick %ld sub-step %d", rate, j, release.tick, release.substep);
				CHECK(placed[release_step] && !attached[release_step] && attached[release_step - 4 / k],
					"%ld Hz sub-step %d: not in the hand until that sub-step, then flying", rate, j);
				/* from the hand, the eye of that sub-step; along the throw */
				CHECK(fabs(release.eye_time - (j == 0 ? RELEASED : RELEASED + (double)(j + 1) / k)) < 1e-9,
					"%ld Hz sub-step %d: the eye of time %g", rate, j, release.eye_time);
				expected_release(release.eye_time, &from, &velocity);
				CHECK(distance(&release.from, &from) < 1e-6f, "%ld Hz sub-step %d: from (%g %g %g), not the hand (%g %g %g)",
					rate, j, release.from.x, release.from.y, release.from.z, from.x, from.y, from.z);
				CHECK(fabs(release.velocity.i - velocity.i) < 1e-6f && fabs(release.velocity.j - velocity.j) < 1e-6f &&
					fabs(release.velocity.k - velocity.k) < 1e-6f,
					"%ld Hz sub-step %d: velocity (%g %g %g), not the hand's throw (%g %g %g)", rate, j, release.velocity.i,
					release.velocity.j, release.velocity.k, velocity.i, velocity.j, velocity.k);
				/* the animation, as at 30: at its key frame at the end of the
				tick that would have seen the release */
				CHECK(fake_unit.unit.grenade_throw_state == _unit_grenade_throw_ending &&
					fake_unit.unit.grenade_object_index == NONE, "%ld Hz sub-step %d: the throw not over", rate, j);
				CHECK(frames[(RELEASED + (j == 0 ? 1 : 2)) * 4] == KEY_FRAME,
					"%ld Hz sub-step %d: the animation at frame %d at the end of the tick that sees the release, not %d", rate,
					j, frames[(RELEASED + (j == 0 ? 1 : 2)) * 4], KEY_FRAME);
				/* n * v * T30 from where it left */
				for (n = 1; n < TICKS - RELEASED - 2; n++)
				{
					real_point3d place;
					real_vector3d out, reference_out;

					if (!have[n])
						continue;
					CHECK(place_after(rate, RELEASED, j, n, &place), "%ld Hz sub-step %d: no place %ld ticks on", rate, j, n);
					vector_from_points3d(&release.from, &place, &out);
					vector_from_points3d(&reference_from, &reference[n], &reference_out);
					CHECK(fabs(out.i - reference_out.i) < 2e-5f && fabs(out.j - reference_out.j) < 2e-5f &&
						fabs(out.k - reference_out.k) < 2e-5f,
						"%ld Hz sub-step %d, %ld ticks after the hand let go: (%g %g %g) out, at 30 (%g %g %g)", rate, j, n,
						out.i, out.j, out.k, reference_out.i, reference_out.j, reference_out.k);
				}
			}
		}
		return 0;
	}

	/* where the frames drew the grenade in the hand last, it leaves from
	there */
	CASE("drawn")
	{
		real_point3d const drawn = { 0.5f, 0.25f, 0.4f };

		/* (a tick played, then the frames' drawing of it in the hand) */
		play(120, RELEASED, 2, TRUE, 1);
		vr_grenade.drawn_valid = TRUE;
		vr_grenade.drawn.position = drawn;
		while (game_time < RELEASED + 2)
			game_tick();
		CHECK(release.released && release.substep == 2, "not released at sub-step 2");
		CHECK(distance(&release.from, &drawn) < 1e-6f, "from (%g %g %g), not where it was drawn", release.from.x,
			release.from.y, release.from.z);
		CHECK(!vr_grenade.drawn_valid, "the drawn place kept after the throw");
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
