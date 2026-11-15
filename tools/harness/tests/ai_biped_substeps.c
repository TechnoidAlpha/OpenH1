/*
AI_BIPED_SUBSTEPS.C

The AI's bipeds in the fast phase (bipeds.c, objects.c): which bipeds are of
the micro lane (object_lane_micro_candidate), and the velocity a biped's
first sub-step of a tick starts from (biped_substep_start_velocity). An AI's
biped walks by its animation's root motion, which biped_physics_prepare sets
on the object as the velocity the tick wants; at 30 the tick's one step
starts from the velocity kept and overwrites the object's unread. The tick
here is biped_update_physics's on flat ground with no collision: each step
of h of a tick changes the velocity by h of the tick's change (a, worked out
from the tick's starting velocity) and moves v' h + a h (1 - h) / 2, so the
steps of a tick end where the tick at 30 does, if the first starts from the
velocity kept.
*/

#include "harness.h"
#include <math.h>

#include "config.inc"

struct unit_datum
{
	struct
	{
		long player_index;
		long actor_index;
		long swarm_actor_index;
	} unit;
};

static struct unit_datum fake_unit;
/* the player (its index) of an OpenCE machine in a mixed game
(network_standard.c), NONE for none */
static long opence_player = NONE;

static boolean network_standard_player_is_opence(long player_index)
{
	return player_index != NONE && player_index == opence_player;
}

/* the biped (its index) of a rider in a vehicle's window
(vehicle_riding_bipeds.c), NONE for none */
static long rider_biped = NONE;

static boolean biped_riding_holds_lane(long biped_index)
{
	return biped_index != NONE && biped_index == rider_biped;
}

/* the fake object (a projectile's owner) */
struct object_datum
{
	struct
	{
		long owner_player_index;
	} object;
};

static struct object_datum fake_object;

static struct object_datum *object_get(long index)
{
	(void)index;
	return &fake_object;
}

static struct unit_datum *unit_get(long index)
{
	(void)index;
	return &fake_unit;
}

/* (vehicles.c's: a vehicle a player drives is of the micro lane; here, the
fake unit's driver is the fake unit) */
static boolean vehicle_takes_substeps(long index)
{
	(void)index;
	return fake_unit.unit.player_index != NONE;
}

#include "under_test.inc"

/* a tick of a biped walking by root motion, from velocity kept toward
wanted (each per tick, along one axis), in steps sub-steps: where it ends,
and its velocity */
static void walk_tick(real kept, real wanted, real bump, int steps, real *moved, real *velocity)
{
	real_vector3d kept_velocity = { kept, 0.f, 0.f };
	/* (biped_physics_prepare's: the object's velocity, the root motion's) */
	real_vector3d prepared = { wanted, 0.f, 0.f };
	real_vector3d object = { wanted + bump, 0.f, 0.f };
	real h = 1.f / (real)steps;
	real a = wanted - kept;
	int step;

	*moved = 0.f;
	for (step = 0; step < steps; step++)
	{
		real_vector3d v = object;
		real v_new;

		if (step == 0)
			biped_substep_start_velocity(&kept_velocity, &prepared, &v);
		v_new = v.i + a * h;
		*moved += v_new * h + a * h * (1.f - h) * 0.5f;
		object.i = v_new;
	}
	*velocity = object.i;
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "root-motion"))
	{
		static real const starts[] = { 0.f, 0.02f, 0.075f, -0.05f };
		static real const wants[] = { 0.075f, 0.03f, 0.f, 0.06f };
		int i;
		int steps;

		for (i = 0; i < (int)(sizeof(starts) / sizeof(starts[0])); i++)
		{
			for (steps = 2; steps <= 4; steps *= 2)
			{
				real moved;
				real velocity;

				walk_tick(starts[i], wants[i], 0.f, steps, &moved, &velocity);
				/* (the tick at 30: a whole tick at the velocity wanted) */
				CHECK(fabsf(moved - wants[i]) < 1e-6f, "from %f toward %f in %d steps: moved %f, at 30 %f",
					starts[i], wants[i], steps, moved, wants[i]);
				CHECK(fabsf(velocity - wants[i]) < 1e-6f, "from %f toward %f in %d steps: velocity %f",
					starts[i], wants[i], steps, velocity);
			}
		}
	}
	else if (!strcmp(name, "player"))
	{
		/* (a player's walk sets no velocity on the object: what the object
		has is the sub-step's start, bit for bit, a bump and all) */
		real_vector3d kept = { 0.031f, -0.017f, 0.003f };
		real_vector3d velocity = { 0.0312345f, -0.0171234f, 0.0029876f };
		real_vector3d expected = velocity;

		biped_substep_start_velocity(&kept, &kept, &velocity);
		CHECK(!memcmp(&velocity, &expected, sizeof(velocity)), "a player's velocity changed: %f %f %f",
			velocity.i, velocity.j, velocity.k);
	}
	else if (!strcmp(name, "bump"))
	{
		/* (what changed the object's velocity after biped_physics_prepare,
		a bump or a blast, is kept, on the velocity the tick started from) */
		real_vector3d kept = { 0.01f, 0.02f, 0.f };
		real_vector3d prepared = { 0.06f, -0.01f, 0.f };
		real_vector3d velocity = { 0.06f + 0.5f, -0.01f, 0.25f };

		biped_substep_start_velocity(&kept, &prepared, &velocity);
		CHECK(fabsf(velocity.i - 0.51f) < 1e-6f && fabsf(velocity.j - 0.02f) < 1e-6f && velocity.k == 0.25f,
			"start %f %f %f", velocity.i, velocity.j, velocity.k);
	}
	else if (!strcmp(name, "lane"))
	{
		struct { long player, actor, swarm; boolean micro; } const units[] = {
			{ 0, NONE, NONE, TRUE }, /* a player's */
			{ NONE, 0x10002, NONE, TRUE }, /* an actor's */
			{ NONE, NONE, 0x20003, TRUE }, /* one of a swarm's */
			{ NONE, NONE, NONE, FALSE }, /* nobody's: a body, a cinematic's */
			{ 0x30004, NONE, NONE, FALSE }, /* an OpenCE player's, on the tick as on their machine */
		};
		int i;

		opence_player = 0x30004;
		for (i = 0; i < (int)(sizeof(units) / sizeof(units[0])); i++)
		{
			fake_unit.unit.player_index = units[i].player;
			fake_unit.unit.actor_index = units[i].actor;
			fake_unit.unit.swarm_actor_index = units[i].swarm;
			CHECK(!object_lane_micro_candidate(1, _object_type_biped) == !units[i].micro,
				"biped %d: player %ld actor %lx swarm %lx", i, units[i].player, units[i].actor, units[i].swarm);
		}
		/* (a player riding on a vehicle's deck, on the tick as at 30) */
		fake_unit.unit.player_index = 0;
		fake_unit.unit.actor_index = NONE;
		fake_unit.unit.swarm_actor_index = NONE;
		rider_biped = 1;
		CHECK(!object_lane_micro_candidate(1, _object_type_biped), "a rider in the sub-steps");
		rider_biped = NONE;
		/* (a vehicle the AI drives stays on the tick) */
		fake_unit.unit.player_index = NONE;
		fake_unit.unit.actor_index = 0x10002;
		CHECK(!object_lane_micro_candidate(1, _object_type_vehicle), "a vehicle of the micro lane");
		/* (a projectile in the sub-steps, but an OpenCE player's, on the tick
		as on their machine) */
		fake_object.object.owner_player_index = NONE;
		CHECK(object_lane_micro_candidate(1, _object_type_projectile), "nobody's projectile on the tick");
		fake_object.object.owner_player_index = 0x50006;
		CHECK(object_lane_micro_candidate(1, _object_type_projectile), "an OpenH1 player's projectile on the tick");
		fake_object.object.owner_player_index = 0x30004;
		CHECK(!object_lane_micro_candidate(1, _object_type_projectile), "an OpenCE player's projectile in the sub-steps");
	}
	else
	{
		CHECK(FALSE, "no case %s", name);
	}

	return 0;
}
