/* Riding at game.sim_rate 30, 60 and 120 (test_riding_rates.py). The riding
code's own carry, seed and record (vehicle_riding.c) and the riders' lanes
(vehicle_riding_bipeds.c) run through a tick's order of updates, as
objects_update_passes and the fast phase run them: the lanes taken as the
objects' update begins, the vehicle's update (a deck held to the tick moves
there), a rider in a vehicle's window after it (vehicle_riding_bipeds.c's
deferred pass: its riding made ready, then its whole tick's step when it is
held, or its sub-steps later), then the sub-steps of what moves in them, the
vehicle's before the biped's, the biped's riding recorded at its last
(bipeds.c's biped_physics_riding_finish).

The vehicle drives on a path known at every instant, so its pose at a tick's
end is the same whatever its sub-steps: what differs between the rates is the
riders' handling alone. The biped is a pill on a flat deck (2 by 1 each way of
the vehicle's origin, 1 up), with ground friction that takes its support's
motion at once and no air control; the cabin, contacts and air frame of
vehicle_riding_bipeds.c are left out (none apply to an open deck). */
#include "harness.h"
#include <math.h>
#include <stddef.h>

typedef unsigned short word;
typedef struct { real i, j; } real_vector2d;
#define MAXIMUM_LOCAL_PLAYERS 4
#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 4
#define HALO_PORT_MAXIMUM_OBJECTS_PER_MAP 2048
#define csmemset memset
#define csmemcpy memcpy
#include "types.inc"

enum { VEHICLE = 0x10080, BIPED = 1000, TICKS = 200, RATES = 3 };
#define DECK_TOP 1.0f
#define DECK_LENGTH 2.0f
#define DECK_WIDTH 1.0f
#define GRAVITY 0.0036f
#define JUMP_VELOCITY 0.11f
#define WALK_SPEED 0.08f
#define PILL_HEIGHT 0.7f
#define PILL_WIDTH 0.2f
/* (the biped's surface limit: steeper is no floor, physics.c's minimum_normal_k) */
#define MINIMUM_NORMAL_K 0.5f

/* ---------- the lanes' world (riding_lanes.c's) */

struct player_datum { long unit_index; };
struct biped_datum { struct { long riding_object_index; char riding_ticks; } biped; };
struct data_iterator { short index; long datum_index; };

static struct player_datum players[1] = { { BIPED } };
static struct biped_datum fake_biped = { { NONE, 0 } };
static void *player_data = players;
static short substeps = 1;
static boolean riding_enabled = TRUE;
static long now;

static short sim_substep_count(void) { return substeps; }
boolean vehicle_riding_enabled(void) { return riding_enabled; }
static long game_time_get(void) { return now; }
static void data_iterator_new(struct data_iterator *iterator, void *data) { (void)data; iterator->index = 0; }
static void *data_iterator_next(struct data_iterator *iterator)
{
	if (iterator->index > 0)
		return NULL;
	iterator->datum_index = iterator->index++;
	return &players[0];
}
static struct biped_datum *biped_try_and_get(long biped_index) { return biped_index == BIPED ? &fake_biped : NULL; }
/* (vehicle_riding_bipeds.c's: in a window, and riding applies) */
static boolean biped_riding_deferred(long biped_index)
{
	return biped_index == BIPED && fake_biped.biped.riding_ticks > 0 && riding_enabled;
}

#include "under_test.inc"

/* ---------- the vehicle: its path */

static struct
{
	real speed; /* world units a tick along its forward */
	real yaw_rate; /* radians a tick */
	long roll_start; /* the tick it starts to roll onto its side (NONE: never) */
} path;

static struct
{
	real_point3d position;
	real_vector3d forward;
	real_vector3d up;
	long updates;
} deck;

static void deck_pose_at(double tick)
{
	double yaw = path.yaw_rate*tick;
	double roll = 0.0;
	double x, y;

	if (path.yaw_rate != 0.f)
	{
		x = path.speed/path.yaw_rate*sin(yaw);
		y = path.speed/path.yaw_rate*(1.0 - cos(yaw));
	}
	else
	{
		x = path.speed*tick;
		y = 0.0;
	}
	/* (onto its side over 6 ticks, 100 degrees about its forward) */
	if (path.roll_start != NONE && tick > path.roll_start)
		roll = (tick - path.roll_start >= 6.0 ? 1.0 : (tick - path.roll_start)/6.0)*1.745;
	deck.position.x = (real)x;
	deck.position.y = (real)y;
	deck.position.z = 0.f;
	deck.forward.i = (real)cos(yaw);
	deck.forward.j = (real)sin(yaw);
	deck.forward.k = 0.f;
	/* (up turned toward the left, -sin yaw, cos yaw, 0) */
	deck.up.i = (real)(-sin(yaw)*sin(roll));
	deck.up.j = (real)(cos(yaw)*sin(roll));
	deck.up.k = (real)cos(roll);
}

/* the vehicle as vehicle_riding_bipeds.c's biped_riding_vehicle gathers it */
static void deck_vehicle(struct vehicle_riding_vehicle *vehicle)
{
	memset(vehicle, 0, sizeof(*vehicle));
	vehicle->object_index = VEHICLE;
	vehicle->node_index = NONE;
	vehicle->position = deck.position;
	vehicle->forward = deck.forward;
	vehicle->up = deck.up;
	vehicle->origin = deck.position;
	vehicle->body_forward = deck.forward;
	vehicle->body_up = deck.up;
	vehicle->updates = deck.updates;
	scale_vector3d(&deck.forward, path.speed, &vehicle->translational_velocity);
	vehicle->angular_velocity.k = path.yaw_rate;
	vehicle->bounding_sphere_center = deck.position;
	vehicle->bounding_sphere_radius = 3.f;
	vehicle->has_player = TRUE;
	vehicle->role = _vehicle_riding_role_simulated;
	vehicle->time = now;
	vehicle_riding_correction_identity(&vehicle->correction, VEHICLE);
}

static void deck_left(real_vector3d *left)
{
	cross_product3d(&deck.up, &deck.forward, left);
}

/* ---------- the biped */

static struct
{
	real_point3d position;
	real_vector3d velocity; /* the world's, the object's translational velocity */
	boolean airborne;
	boolean walking;
	boolean jump;
	real facing;
	real kept_dyaw;
	struct vehicle_riding_state riding;
	/* the tick's movement (biped_physics_prepare's) */
	real_vector3d physics_velocity;
	real_vector3d frame_velocity;
	struct vehicle_riding_step step;
	boolean step_valid;
	boolean carried;
	boolean supported;
	boolean on_deck;
} rider;

static void rider_fields(void)
{
	fake_biped.biped.riding_object_index = rider.riding.object_index;
	fake_biped.biped.riding_ticks = rider.riding.ticks;
}

/* its riding made ready, once a tick in its update (vehicle_riding_bipeds.c's
biped_riding_prepare: the carry, the view's turn, the velocity in the deck's
frame turned with it) */
static void rider_prepare(void)
{
	rider.step_valid = FALSE;
	rider.carried = FALSE;
	rider.kept_dyaw = 0.f;
	rider.frame_velocity.i = rider.frame_velocity.j = rider.frame_velocity.k = 0.f;
	if (!riding_enabled)
	{
		rider.physics_velocity = rider.velocity;
	}
	else
	{
		if (rider.riding.ticks > 0)
		{
			struct vehicle_riding_vehicle vehicle;

			deck_vehicle(&vehicle);
			rider.step_valid = vehicle_riding_snapshot_step(&rider.riding, &vehicle, &rider.position, PILL_HEIGHT,
				PILL_WIDTH, FALSE, 1000.f, &rider.step);
		}
		rider.carried = rider.step_valid && vehicle_riding_is_riding(&rider.riding);
		if (rider.carried)
		{
			add_vectors3d((real_vector3d *)&rider.position, &rider.step.displacement, (real_vector3d *)&rider.position);
			rider.kept_dyaw = rider.step.dyaw;
			rider.facing += rider.kept_dyaw;
		}
		vehicle_riding_seed(&rider.riding, &rider.velocity, &rider.physics_velocity, &rider.frame_velocity);
		vehicle_riding_turn_relative(&rider.riding, rider.step_valid ? &rider.step : NULL, &rider.physics_velocity);
	}
	if (rider.jump && !rider.airborne)
	{
		rider.physics_velocity.k += JUMP_VELOCITY;
		rider.airborne = TRUE;
		rider.jump = FALSE;
	}
}

/* one step of its physics, a share of the tick: against the deck as it is now */
static void rider_step(real share)
{
	struct vehicle_riding_vehicle vehicle;
	real_point3d local;

	rider.physics_velocity.k -= GRAVITY*share;
	rider.position.x += rider.physics_velocity.i*share;
	rider.position.y += rider.physics_velocity.j*share;
	rider.position.z += rider.physics_velocity.k*share;
	rider.supported = rider.on_deck = FALSE;
	deck_vehicle(&vehicle);
	vehicle_riding_point_to_local(&vehicle.position, &vehicle.forward, &vehicle.up, &rider.position, &local);
	if (fabsf(local.x) <= DECK_LENGTH && fabsf(local.y) <= DECK_WIDTH && local.z <= DECK_TOP &&
		local.z >= DECK_TOP - 0.5f && vehicle.up.k >= MINIMUM_NORMAL_K)
	{
		real_vector3d ground = { 0.f, 0.f, 0.f };

		local.z = DECK_TOP;
		vehicle_riding_point_to_world(&vehicle.position, &vehicle.forward, &vehicle.up, &local, &rider.position);
		rider.supported = rider.on_deck = TRUE;
		/* (its frame is the deck's while carried, else the world's: the deck's motion there) */
		if (!rider.carried)
			vehicle_riding_deck_point_velocity(&vehicle, &rider.position, FALSE, &ground);
		rider.physics_velocity = ground;
		if (rider.walking)
		{
			real_vector3d left;

			deck_left(&left);
			rider.physics_velocity.i -= left.i*WALK_SPEED;
			rider.physics_velocity.j -= left.j*WALK_SPEED;
		}
	}
	else if (rider.position.z <= 0.f)
	{
		rider.position.z = 0.f;
		rider.supported = TRUE;
		rider.walking = FALSE;
		rider.physics_velocity.i = rider.physics_velocity.j = rider.physics_velocity.k = 0.f;
	}
	rider.airborne = !rider.supported;
}

/* its riding recorded where its tick's step, or its last sub-step, leaves it
(vehicle_riding_bipeds.c's biped_riding_finish) */
static void rider_finish(void)
{
	struct vehicle_riding_vehicle support;
	real_vector3d support_velocity = { 0.f, 0.f, 0.f };
	real_vector3d normal = { 0.f, 0.f, 1.f };

	if (!riding_enabled)
	{
		rider.velocity = rider.physics_velocity;
		return;
	}
	deck_vehicle(&support);
	if (rider.on_deck)
	{
		/* (vehicle_riding_support_velocity's choice) */
		if (rider.carried)
			support_velocity = rider.frame_velocity;
		else if (rider.step_valid)
			support_velocity = rider.step.tracked_deck_velocity;
		else
			vehicle_riding_deck_point_velocity(&support, &rider.position, FALSE, &support_velocity);
		normal = support.up;
	}
	rider.velocity = rider.physics_velocity;
	vehicle_riding_record(&rider.riding, rider.step_valid ? &rider.step : NULL, rider.on_deck ? &support : NULL,
		rider.supported, &support_velocity, &normal, &rider.position, FALSE, &rider.velocity);
	rider_fields();
}

/* ---------- a tick */

static boolean rider_held_this_tick;

static void tick(void)
{
	boolean deck_in_tick;
	boolean rider_in_tick;
	short index;

	/* (objects_update_passes: the lanes for the tick) */
	biped_riding_lanes_latch();
	deck_in_tick = substeps == 1 || vehicle_riding_holds_lane(VEHICLE);
	rider_in_tick = substeps == 1 || biped_riding_holds_lane(BIPED);
	rider_held_this_tick = substeps > 1 && rider_in_tick;
	/* (the main loop: the vehicle, which counts an update; a biped in a window
	after it, and one in none moves nothing of the vehicle's) */
	deck.updates++;
	if (deck_in_tick)
		deck_pose_at((double)(now + 1));
	rider_prepare();
	if (rider_in_tick)
	{
		rider_step(1.f);
		rider_finish();
	}
	/* (the fast phase: the vehicle's sub-step, then the biped's) */
	if (substeps > 1)
	{
		for (index = 0; index < substeps; index++)
		{
			if (!deck_in_tick)
				deck_pose_at((double)now + (double)(index + 1)/(double)substeps);
			if (!rider_in_tick)
			{
				rider_step(1.f/(real)substeps);
				if (index == substeps - 1)
					rider_finish();
			}
		}
	}
	now++;
}

/* ---------- the runs */

struct run
{
	real_point3d positions[TICKS];
	real_point3d locals[TICKS];
	real_vector3d velocities[TICKS];
	real facings[TICKS];
	real kept_dyaws[TICKS];
	real deck_yaws[TICKS];
	char windows[TICKS];
	boolean airborne[TICKS];
	boolean held[TICKS];
};

static struct run runs[RATES];
static short const rate_substeps[RATES] = { 1, 2, 4 };

static void start(short count, boolean on_deck, real_point3d const *local, real_vector3d const *offset)
{
	struct vehicle_riding_vehicle vehicle;
	real_vector3d normal;

	substeps = count;
	now = 0;
	memset(&rider, 0, sizeof(rider));
	vehicle_riding_clear(&rider.riding);
	deck.updates = 0;
	deck_pose_at(0.0);
	deck_vehicle(&vehicle);
	vehicle_riding_point_to_world(&vehicle.position, &vehicle.forward, &vehicle.up, local, &rider.position);
	add_vectors3d((real_vector3d *)&rider.position, offset, (real_vector3d *)&rider.position);
	vehicle_riding_deck_point_velocity(&vehicle, &rider.position, FALSE, &rider.velocity);
	rider.airborne = !on_deck;
	/* (standing on it at the start, as its last tick recorded) */
	if (on_deck && riding_enabled)
	{
		normal = vehicle.up;
		vehicle_riding_record(&rider.riding, NULL, &vehicle, TRUE, &rider.velocity, &normal, &rider.position, FALSE,
			&rider.velocity);
	}
	rider_fields();
	biped_riding_lanes.time = NONE;
}

static void play(struct run *run, long ticks, long jump_tick, long walk_tick)
{
	long index;

	for (index = 0; index < ticks; index++)
	{
		if (index == jump_tick)
			rider.jump = TRUE;
		if (index == walk_tick)
			rider.walking = TRUE;
		tick();
		vehicle_riding_point_to_local(&deck.position, &deck.forward, &deck.up, &rider.position, &run->locals[index]);
		run->positions[index] = rider.position;
		run->velocities[index] = rider.velocity;
		run->facings[index] = rider.facing;
		run->kept_dyaws[index] = rider.kept_dyaw;
		run->deck_yaws[index] = path.yaw_rate*(real)now;
		run->windows[index] = rider.riding.ticks;
		run->airborne[index] = rider.airborne;
		run->held[index] = rider_held_this_tick;
	}
}

static boolean same_point(real_point3d const *a, real_point3d const *b)
{
	return memcmp(a, b, sizeof(*a)) == 0;
}

/* every rate's run, the same to the bit as 30's from tick first to last */
static void check_same_as_30(long first, long last)
{
	short rate;
	long index;

	for (rate = 1; rate < RATES; rate++)
	{
		for (index = first; index < last; index++)
		{
			CHECK(same_point(&runs[rate].positions[index], &runs[0].positions[index]),
				"at %d sub-steps, tick %ld: (%.6f %.6f %.6f), at 30 (%.6f %.6f %.6f)", rate_substeps[rate], index,
				runs[rate].positions[index].x, runs[rate].positions[index].y, runs[rate].positions[index].z,
				runs[0].positions[index].x, runs[0].positions[index].y, runs[0].positions[index].z);
			CHECK(runs[rate].facings[index] == runs[0].facings[index] && runs[rate].windows[index] == runs[0].windows[index],
				"at %d sub-steps, tick %ld: facing %.6f window %d, at 30 %.6f %d", rate_substeps[rate], index,
				runs[rate].facings[index], runs[rate].windows[index], runs[0].facings[index], runs[0].windows[index]);
		}
	}
}

static void check_held(long first, long last, boolean held)
{
	short rate;
	long index;

	for (rate = 0; rate < RATES; rate++)
	{
		for (index = first; index < last; index++)
		{
			CHECK(runs[rate].held[index] == (held && rate_substeps[rate] > 1), "at %d sub-steps, tick %ld: held %d",
				rate_substeps[rate], index, runs[rate].held[index]);
		}
	}
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";
	static real_point3d const spot = { 0.5f, 0.3f, DECK_TOP };
	static real_vector3d const none = { 0.f, 0.f, 0.f };
	short rate;
	long index;

	path.roll_start = NONE;
	if (!strcmp(name, "ride"))
	{
		/* a turning deck: the rider stays on its spot, its view turned with the deck */
		path.speed = 0.3f;
		path.yaw_rate = 0.02f;
		for (rate = 0; rate < RATES; rate++)
		{
			start(rate_substeps[rate], TRUE, &spot, &none);
			play(&runs[rate], 90, NONE, NONE);
			for (index = 0; index < 90; index++)
			{
				CHECK(runs[rate].windows[index] == RIDING_TICKS, "at %d sub-steps, tick %ld: not riding (%d)",
					rate_substeps[rate], index, runs[rate].windows[index]);
				CHECK(fabsf(runs[rate].locals[index].x - spot.x) < 0.001f && fabsf(runs[rate].locals[index].y - spot.y) < 0.001f &&
					fabsf(runs[rate].locals[index].z - spot.z) < 0.001f, "at %d sub-steps, tick %ld: off its spot (%.4f %.4f %.4f)",
					rate_substeps[rate], index, runs[rate].locals[index].x, runs[rate].locals[index].y, runs[rate].locals[index].z);
				CHECK(fabsf(runs[rate].facings[index] - runs[rate].deck_yaws[index]) < 0.001f,
					"at %d sub-steps, tick %ld: view %.5f, deck %.5f", rate_substeps[rate], index, runs[rate].facings[index],
					runs[rate].deck_yaws[index]);
			}
			/* (the frames between ticks: the view drawn with the turn kept, -(1 - fraction) of it,
			render_interpolation.c, against the deck drawn at the same fraction) */
			for (index = 1; index < 90; index++)
			{
				short frame;

				for (frame = 1; frame < 4; frame++)
				{
					real fraction = (real)frame/4.f;
					real drawn = runs[rate].facings[index] - (1.f - fraction)*runs[rate].kept_dyaws[index];
					real deck_drawn = runs[rate].deck_yaws[index - 1] + fraction*(runs[rate].deck_yaws[index] - runs[rate].deck_yaws[index - 1]);

					CHECK(fabsf(drawn - deck_drawn) < 0.001f, "at %d sub-steps, tick %ld, frame %d: view %.5f, deck %.5f",
						rate_substeps[rate], index, frame, drawn, deck_drawn);
				}
			}
		}
		check_held(0, 90, TRUE);
		check_same_as_30(0, 90);
		return 0;
	}
	if (!strcmp(name, "jump"))
	{
		/* a jump from a moving deck lands back on it, on its spot */
		path.speed = 0.3f;
		path.yaw_rate = 0.f;
		for (rate = 0; rate < RATES; rate++)
		{
			boolean flew = FALSE;

			start(rate_substeps[rate], TRUE, &spot, &none);
			play(&runs[rate], 90, 10, NONE);
			for (index = 10; index < 90; index++)
			{
				if (runs[rate].airborne[index])
				{
					flew = TRUE;
					CHECK(runs[rate].windows[index] == RIDING_TICKS - 1, "at %d sub-steps, tick %ld: window %d in the air",
						rate_substeps[rate], index, runs[rate].windows[index]);
				}
			}
			CHECK(flew, "at %d sub-steps: no jump", rate_substeps[rate]);
			CHECK(runs[rate].windows[89] == RIDING_TICKS, "at %d sub-steps: not riding again (%d)", rate_substeps[rate],
				runs[rate].windows[89]);
			CHECK(fabsf(runs[rate].locals[89].x - spot.x) < 0.05f && fabsf(runs[rate].locals[89].y - spot.y) < 0.05f,
				"at %d sub-steps: landed at (%.4f %.4f) on the deck", rate_substeps[rate], runs[rate].locals[89].x,
				runs[rate].locals[89].y);
		}
		check_held(0, 90, TRUE);
		check_same_as_30(0, 90);
		return 0;
	}
	if (!strcmp(name, "step-off"))
	{
		/* walking off a deck's side: the deck's motion kept in the air, the window run down on the
		ground, then the sub-steps again */
		long left = NONE;
		long landed = NONE;
		long ended = NONE;

		path.speed = 0.3f;
		path.yaw_rate = 0.f;
		for (rate = 0; rate < RATES; rate++)
		{
			start(rate_substeps[rate], TRUE, &spot, &none);
			play(&runs[rate], 160, NONE, 5);
			if (rate == 0)
			{
				for (index = 0; index < 160; index++)
				{
					if (left == NONE && runs[0].airborne[index])
						left = index;
					if (left != NONE && landed == NONE && !runs[0].airborne[index])
						landed = index;
					if (ended == NONE && runs[0].windows[index] == 0)
						ended = index;
				}
				CHECK(left != NONE && landed != NONE && ended != NONE, "left %ld landed %ld window ended %ld", left, landed,
					ended);
				/* (the deck's motion kept: along the deck's way as far as it drove in the fall) */
				{
					real flown = runs[0].positions[landed].x - runs[0].positions[left - 1].x;
					real driven = path.speed*(real)(landed - left + 1);

					CHECK(flown > 0.9f*driven && flown < 1.1f*driven, "flew %.3f in the fall, the deck drove %.3f", flown,
						driven);
				}
			}
			for (index = ended + 1; index < 160; index++)
			{
				CHECK(!runs[rate].held[index], "at %d sub-steps, tick %ld: held past its window", rate_substeps[rate], index);
			}
		}
		check_held(0, ended, TRUE);
		check_same_as_30(0, 160);
		return 0;
	}
	if (!strcmp(name, "flipped"))
	{
		/* a vehicle that rolls onto its side throws its rider with its motion */
		long thrown = NONE;

		path.speed = 0.2f;
		path.yaw_rate = 0.f;
		path.roll_start = 20;
		for (rate = 0; rate < RATES; rate++)
		{
			start(rate_substeps[rate], TRUE, &spot, &none);
			play(&runs[rate], 100, NONE, NONE);
			for (index = 0; index < 20; index++)
			{
				CHECK(runs[rate].windows[index] == RIDING_TICKS, "at %d sub-steps, tick %ld: not riding before the roll",
					rate_substeps[rate], index);
			}
		}
		for (index = 20; index < 100 && thrown == NONE; index++)
		{
			if (runs[0].windows[index] != RIDING_TICKS)
				thrown = index;
		}
		CHECK(thrown != NONE && thrown < 30, "not thrown (%ld)", thrown);
		CHECK(runs[0].airborne[thrown], "thrown onto the deck's side");
		CHECK(fabsf(runs[0].velocities[thrown].i - path.speed) < 0.02f, "thrown with %.4f along, the deck drove %.4f",
			runs[0].velocities[thrown].i, path.speed);
		CHECK(runs[0].positions[99].z == 0.f, "never landed (%.3f)", runs[0].positions[99].z);
		check_same_as_30(0, 100);
		return 0;
	}
	if (!strcmp(name, "drop-on"))
	{
		/* a biped on foot (in the sub-steps at 60 and 120) dropping onto a moving deck rides from its
		next tick, held to it */
		static real_point3d const above = { 0.f, 0.f, DECK_TOP };
		static real_vector3d const up = { 0.f, 0.f, 0.6f };

		path.speed = 0.3f;
		path.yaw_rate = 0.01f;
		for (rate = 0; rate < RATES; rate++)
		{
			long rode = NONE;

			start(rate_substeps[rate], FALSE, &above, &up);
			play(&runs[rate], 80, NONE, NONE);
			for (index = 0; index < 80 && rode == NONE; index++)
			{
				if (runs[rate].windows[index] == RIDING_TICKS)
					rode = index;
			}
			CHECK(rode != NONE && rode > 5, "at %d sub-steps: never landed (%ld)", rate_substeps[rate], rode);
			CHECK(!runs[rate].held[rode], "at %d sub-steps: held the tick it landed", rate_substeps[rate]);
			for (index = rode + 1; index < 80; index++)
			{
				CHECK(runs[rate].windows[index] == RIDING_TICKS && runs[rate].held[index] == (rate_substeps[rate] > 1),
					"at %d sub-steps, tick %ld: window %d held %d", rate_substeps[rate], index, runs[rate].windows[index],
					runs[rate].held[index]);
				CHECK(fabsf(runs[rate].locals[index].x - runs[rate].locals[rode].x) < 0.001f &&
					fabsf(runs[rate].locals[index].y - runs[rate].locals[rode].y) < 0.001f,
					"at %d sub-steps, tick %ld: off its spot (%.4f %.4f), landed at (%.4f %.4f)", rate_substeps[rate], index,
					runs[rate].locals[index].x, runs[rate].locals[index].y, runs[rate].locals[rode].x, runs[rate].locals[rode].y);
			}
		}
		return 0;
	}
	if (!strcmp(name, "riding-off"))
	{
		/* riding off: nothing held at any rate, no window, the view never turned by the deck */
		riding_enabled = FALSE;
		path.speed = 0.3f;
		path.yaw_rate = 0.02f;
		for (rate = 0; rate < RATES; rate++)
		{
			start(rate_substeps[rate], TRUE, &spot, &none);
			play(&runs[rate], 60, NONE, NONE);
			for (index = 0; index < 60; index++)
			{
				CHECK(!runs[rate].held[index] && runs[rate].windows[index] == 0 && runs[rate].facings[index] == 0.f,
					"at %d sub-steps, tick %ld: held %d window %d view %.4f", rate_substeps[rate], index,
					runs[rate].held[index], runs[rate].windows[index], runs[rate].facings[index]);
			}
			CHECK(!vehicle_riding_holds_lane(VEHICLE) && !biped_riding_holds_lane(BIPED), "at %d sub-steps: held",
				rate_substeps[rate]);
		}
		return 0;
	}
	CHECK(FALSE, "no case %s", name);
	return 0;
}
