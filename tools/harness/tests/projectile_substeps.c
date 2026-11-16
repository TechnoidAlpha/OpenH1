/*
PROJECTILE_SUBSTEPS.C (test)

The projectiles in the fast phase (source/items/projectiles.c's
projectile_update, projectiles_substep and projectile_step, on the clock of
port/linux/game/sim_rate.c): a projectile's head start, a whole 30 Hz tick
at the moment it is made, its hold of a tick of time, then its sub-steps.
A shot fired at any sub-step of any rate is, a whole number of 30ths of a
second after it was fired, where a shot fired at 30 is that long after it
was: n * v * T30 out, on the same arc, striking the same floor at the same
point and time, its timers going off on the same tick.
test_projectile_substeps.py takes the declarations (decls.inc) and the code
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
static struct { struct { long player_index; } unit; } fake_unit = { { NONE } };
static struct { long unit_index; } fake_player = { NONE };

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
#define object_delete_immediately(index) ((void)0) /* (left flagged: the trace sees it gone) */
#define ai_handle_spatial_effect(index, point, type, noise, count) ((void)0)
#define game_difficulty_get_value(value) 1.0f
#define unit_get(index) (&fake_unit)
#define unit_get_center_of_mass(index, point) (*(point) = projectile_get(index)->object.position)
#define periodic_function_evaluate(function, x) 0.0f
#define set_real_euler_angles2d(angles, y, p) ((angles)->yaw = (y), (angles)->pitch = (p))
#define vector3d_from_euler_angles2d(v, angles) \
	((v)->i = cosine((angles)->yaw) * cosine((angles)->pitch), (v)->j = sine((angles)->yaw) * cosine((angles)->pitch), \
	 (v)->k = sine((angles)->pitch))
#define local_player_get_player_index(index) NONE
#define player_get(index) (&fake_player)
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

#include "under_test.inc"

/* ---------- the game's tick, and a shot */

static real_point3d const muzzle = { 0.0f, 0.0f, 1.0f };

/* the shot to fire: its tick and sub-step, its velocity (world units a tick),
its fuse in ticks (0 none); and the projectile made */
static struct
{
	long tick;
	short substep;
	real_vector3d velocity;
	real fuse_ticks;
	long made;
} shot;

/* each step's end: the projectile's place, in 1/4 ticks from the start */
enum { MAXIMUM_STEPS = 64 * 4 };
static real_point3d places[MAXIMUM_STEPS];
static boolean placed[MAXIMUM_STEPS];

static long fire(void)
{
	short absolute_index;

	for (absolute_index = 0; absolute_index < MAXIMUM_PROJECTILES; absolute_index++)
	{
		if (!headers[absolute_index].identifier)
		{
			struct projectile_datum *projectile = &projectiles[absolute_index];

			memset(projectile, 0, sizeof(*projectile));
			headers[absolute_index].identifier = (short)(0x1000 + absolute_index);
			headers[absolute_index].type = _object_type_projectile;
			headers[absolute_index].flags = FLAG(_object_header_active_bit) | FLAG(_object_header_being_created_bit);
			headers[absolute_index].datum = projectile;
			projectile->object.type = _object_type_projectile;
			projectile->object.parent_object_index = NONE;
			projectile->object.position = muzzle;
			projectile->object.forward.i = 1.0f;
			projectile->object.up.k = 1.0f;
			projectile->object.translational_velocity = shot.velocity;
			projectile->projectile.target_object_index = NONE;
			projectile->projectile.tracer_attachment_index = NONE;
			projectile->projectile.ignore_object_index = NONE;
			projectile->projectile.rotation_cosine = 1.0f;
			/* a deceleration timer that runs out on its second tick, as the
			scorecard's bullet's does; its fuse */
			projectile->projectile.deceleration_timer_delta = 0.6f;
			if (shot.fuse_ticks >= 1.0f)
				projectile->projectile.detonation_timer_delta = 1.0f / shot.fuse_ticks;
			mark_made(datum_of(absolute_index), projectile);
			shot.made = datum_of(absolute_index);
			return shot.made;
		}
	}
	return NONE;
}

/* (the weapons' slot: a shot pulled at a sub-step after the tick's update) */
static void weapons_substep(short substep_index, short substep_count, real seconds)
{
	if (game_time == shot.tick && substep_index == shot.substep && substep_index != 0)
		fire();
}

/* (the trace's slot: each step's end) */
static void trace_substep(short substep_index, short substep_count, real seconds)
{
	long step = (game_time * 4) + (substep_index + 1) * (4 / substep_count);

	if (shot.made != NONE && step < MAXIMUM_STEPS && !TEST_FLAG(object_header_get(shot.made)->flags, _object_header_being_deleted_bit))
	{
		places[step] = projectile_get(shot.made)->object.position;
		placed[step] = TRUE;
	}
}

static void game_tick(void)
{
	short absolute_index;

	sim_rate_tick_begin();
	/* the tick's update: a shot pulled at its start fires in it (a unit's
	weapon's update), then each object's update, then those made in it */
	if (game_time == shot.tick && shot.substep == 0)
		fire();
	for (absolute_index = 0; absolute_index < MAXIMUM_PROJECTILES; absolute_index++)
	{
		struct object_header_datum *header = &headers[absolute_index];

		if (header->identifier && !TEST_FLAG(header->flags, _object_header_being_created_bit) &&
			!TEST_FLAG(header->flags, _object_header_being_deleted_bit))
		{
			projectile_update(datum_of(absolute_index));
		}
	}
	for (absolute_index = 0; absolute_index < MAXIMUM_PROJECTILES; absolute_index++)
	{
		struct object_header_datum *header = &headers[absolute_index];

		if (header->identifier && TEST_FLAG(header->flags, _object_header_being_created_bit))
		{
			SET_FLAG(header->flags, _object_header_being_created_bit, FALSE);
			projectile_update(datum_of(absolute_index));
		}
	}
	/* (the tick's sub-steps, one at a time as the clock runs them) */
	{
		short substep;

		for (substep = 0; substep < sim_substep_count(); substep++)
			sim_substep_run(substep);
	}
	game_time++;
}

/* a game at a rate: a shot at a tick's sub-step, played some ticks */
static void play(long rate, long tick, short substep, real_vector3d velocity, real fuse_ticks, long ticks)
{
	long index;

	memset(projectiles, 0, sizeof(projectiles));
	memset(headers, 0, sizeof(headers));
	memset(events, 0, sizeof(events));
	memset(placed, 0, sizeof(placed));
	game_time = 0;
	setting = rate;
	changes++;
	shot.tick = tick;
	shot.substep = substep;
	shot.velocity = velocity;
	shot.fuse_ticks = fuse_ticks;
	shot.made = NONE;
	sim_substep_register(_sim_substep_weapons, weapons_substep);
	sim_substep_register(_sim_substep_projectiles, projectiles_substep);
	sim_substep_register(_sim_substep_trace, trace_substep);
	for (index = 0; index < ticks; index++)
		game_tick();
}

/* the shot's place n ticks after it was fired (its tick and sub-step's start) */
static boolean place_after(long rate, short substep, long n, real_point3d *place)
{
	long step = shot.tick * 4 + substep * (4 / (rate / 30)) + n * 4;

	if (step >= MAXIMUM_STEPS || !placed[step])
		return FALSE;
	*place = places[step];
	return TRUE;
}

static real distance(real_point3d const *a, real_point3d const *b)
{
	return distance3d(a, b);
}

/* a bullet a little slower than its final velocity, as the scorecard's is
(its speed set a little under its final velocity each tick: the case where a
tick's update changes its velocity at once), gravity on, fired up a little */
static real_vector3d const bullet = { 0.999f, 0.0f, 0.02f };

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static long const rates[] = { 60, 120 };
	enum { TICKS = 40, FIRED = 3 };
	unsigned int rate_index;

	definition.projectile.final_velocity = 1.0f;
	definition.projectile.air_gravity_scale = 1.0f;
	definition.projectile.water_gravity_scale = 1.0f;
	definition.projectile.minimum_velocity = 2.0f;
	definition.projectile.flyby_sound.index = NONE;
	definition.projectile.detonation_timer_starts = _projectile_detonation_timer_starts_immediately;

	/* at 30, the fast phase leaves the projectiles to their ticks: none of
	its bits is set, and the shot travels a tick a tick */
	CASE("classic")
	{
		real_point3d place;

		play(30, FIRED, 0, bullet, 0.0f, TICKS);
		CHECK(place_after(30, 0, 1, &place), "no place a tick after the shot");
		CHECK(fabs(place.x - bullet.i) < 1e-6f, "a tick after the shot: %g, not v * T30", place.x);
		play(30, FIRED, 0, bullet, 0.0f, FIRED + 2);
		CHECK((projectile_get(shot.made)->projectile.flags & PROJECTILE_SUBSTEP_FLAGS_MASK) == 0, "sub-step bits at 30");
		return 0;
	}

	/* a shot fired at any sub-step: n ticks after it was fired it is where a
	30 Hz shot is n ticks after it was, n * v * T30 out on the same arc, from
	its first step on (its instant range, v * T30); and a later shot is never
	ahead of an earlier one */
	CASE("head-start")
	{
		real_point3d reference[TICKS];
		boolean have[TICKS];
		long n;

		play(30, FIRED, 0, bullet, 0.0f, TICKS);
		for (n = 0; n < TICKS; n++)
			have[n] = place_after(30, 0, n, &reference[n]);
		CHECK(have[1] && fabs(distance(&reference[1], &muzzle) - 1.0f) < 1e-3f, "30 Hz instant range");
		for (rate_index = 0; rate_index < NUMBEROF(rates); rate_index++)
		{
			long rate = rates[rate_index];
			short substep;
			real previous_first = 1e9f;

			for (substep = 0; substep < rate / 30; substep++)
			{
				real_point3d first;
				/* (the end of the sub-step it was fired in: its head start's) */
				long first_step = FIRED * 4 + (substep + 1) * (4 / (rate / 30));

				play(rate, FIRED, substep, bullet, 0.0f, TICKS);
				CHECK(placed[first_step], "%ld Hz sub-step %d: no place after its first step", rate, substep);
				first = places[first_step];
				CHECK(fabs(distance(&first, &muzzle) - distance(&reference[1], &muzzle)) < 1e-5f,
					"%ld Hz sub-step %d: instant range %g, not %g", rate, substep, distance(&first, &muzzle),
					distance(&reference[1], &muzzle));
				for (n = 1; n < TICKS - FIRED - 1; n++)
				{
					real_point3d place;

					if (!have[n])
						continue;
					CHECK(place_after(rate, substep, n, &place), "%ld Hz sub-step %d: no place %ld ticks on", rate, substep, n);
					CHECK(distance(&place, &reference[n]) < 2e-5f,
						"%ld Hz sub-step %d, %ld ticks after it was fired: (%g %g %g), at 30 (%g %g %g)", rate, substep, n,
						place.x, place.y, place.z, reference[n].x, reference[n].y, reference[n].z);
				}
				/* at the end of the tick after the one it was fired in, a later
				shot is no further out */
				{
					real_point3d end = places[(FIRED + 2) * 4];
					real out = distance(&end, &muzzle);

					CHECK(out <= previous_first + 1e-6f, "%ld Hz: the shot at sub-step %d is ahead of the one before (%g, %g)",
						rate, substep, out, previous_first);
					previous_first = out;
				}
			}
		}
		return 0;
	}

	/* it strikes the floor where and when a 30 Hz shot does, a whole number
	of ticks after it was fired, at any sub-step */
	CASE("strike")
	{
		double flight;
		real_point3d point;

		floor_z = 0.0f;
		play(30, FIRED, 0, bullet, 0.0f, TICKS + 20);
		CHECK(events[0].struck, "no strike at 30");
		flight = events[0].strike_ticks - FIRED;
		point = events[0].strike_point;
		for (rate_index = 0; rate_index < NUMBEROF(rates); rate_index++)
		{
			long rate = rates[rate_index];
			short substep;

			for (substep = 0; substep < rate / 30; substep++)
			{
				double fired = FIRED + (double)substep / (rate / 30);

				play(rate, FIRED, substep, bullet, 0.0f, TICKS + 20);
				CHECK(events[0].struck, "%ld Hz sub-step %d: no strike", rate, substep);
				CHECK(distance(&events[0].strike_point, &point) < 1e-4f, "%ld Hz sub-step %d: struck (%g %g %g), at 30 (%g %g %g)",
					rate, substep, events[0].strike_point.x, events[0].strike_point.y, events[0].strike_point.z, point.x,
					point.y, point.z);
				CHECK(fabs((events[0].strike_ticks - fired) - flight) < 1e-4, "%ld Hz sub-step %d: flight %g ticks, at 30 %g",
					rate, substep, events[0].strike_ticks - fired, flight);
			}
		}
		return 0;
	}

	/* it bounces off the floor as a 30 Hz shot does: each strike after the
	first where and when that one's is (the line it travels on after a strike
	the one its tick's update at 30 travels on), a grenade's way (faster than
	its final velocity) and a slow bullet's (its speed set under its final
	velocity each tick, a strike's velocity the one set) */
	CASE("bounce")
	{
		static real_vector3d const lob = { 0.2f, 0.0f, 0.03f };
		static real const final_velocities[] = { 0.0f, 1.0f };
		unsigned int final_index;
		short count;
		short strike;
		double strikes_ticks[MAXIMUM_STRIKES];
		real_point3d strikes[MAXIMUM_STRIKES];

		floor_z = 0.0f;
		restitution = 0.6f;
		for (final_index = 0; final_index < NUMBEROF(final_velocities); final_index++)
		{
			definition.projectile.final_velocity = final_velocities[final_index];
			play(30, FIRED, 0, lob, 0.0f, TICKS + 200);
			count = events[0].strike_count;
			CHECK(count >= 3, "%d strikes at 30", count);
			memcpy(strikes_ticks, events[0].strikes_ticks, sizeof(strikes_ticks));
			memcpy(strikes, events[0].strikes, sizeof(strikes));
			for (rate_index = 0; rate_index < NUMBEROF(rates); rate_index++)
			{
				long rate = rates[rate_index];
				short substep;

				for (substep = 0; substep < rate / 30; substep++)
				{
					double fired = FIRED + (double)substep / (rate / 30);

					play(rate, FIRED, substep, lob, 0.0f, TICKS + 200);
					CHECK(events[0].strike_count >= count, "%ld Hz sub-step %d: %d strikes, at 30 %d", rate, substep,
						events[0].strike_count, count);
					for (strike = 0; strike < count; strike++)
					{
						/* (to the floats' rounding, far out: a millionth of the way) */
						CHECK(distance(&events[0].strikes[strike], &strikes[strike]) < 1e-4f + 1e-6f * distance(&strikes[strike], &muzzle),
							"%ld Hz sub-step %d: strike %d at (%.6f %.6f %.6f), at 30 (%.6f %.6f %.6f)", rate, substep, strike,
							events[0].strikes[strike].x, events[0].strikes[strike].y, events[0].strikes[strike].z,
							strikes[strike].x, strikes[strike].y, strikes[strike].z);
						CHECK(fabs((events[0].strikes_ticks[strike] - fired) - (strikes_ticks[strike] - FIRED)) < 1e-3,
							"%ld Hz sub-step %d: strike %d %g ticks after the shot, at 30 %g", rate, substep, strike,
							events[0].strikes_ticks[strike] - fired, strikes_ticks[strike] - FIRED);
					}
				}
			}
		}
		return 0;
	}

	/* its fuse (in ticks, counting the tick it was made in as the first) goes
	off a whole number of ticks after it was fired, as at 30 */
	CASE("fuse")
	{
		static real_vector3d const still = { 0.01f, 0.0f, 0.0f };
		long fuse_tick;

		global_gravity = 0.0f;
		play(30, FIRED, 0, still, 7.5f, TICKS);
		CHECK(events[0].detonated, "no detonation at 30");
		fuse_tick = events[0].detonation_tick - FIRED;
		for (rate_index = 0; rate_index < NUMBEROF(rates); rate_index++)
		{
			long rate = rates[rate_index];
			short substep;

			for (substep = 0; substep < rate / 30; substep++)
			{
				play(rate, FIRED, substep, still, 7.5f, TICKS);
				CHECK(events[0].detonated, "%ld Hz sub-step %d: no detonation", rate, substep);
				CHECK(events[0].detonation_tick - FIRED == fuse_tick && events[0].detonation_substep == substep,
					"%ld Hz sub-step %d: went off at tick +%ld sub-step %d, at 30 tick +%ld", rate, substep,
					events[0].detonation_tick - FIRED, events[0].detonation_substep, fuse_tick);
			}
		}
		return 0;
	}

	fprintf(stderr, "no case %s\n", case_name);
	return 2;
}
