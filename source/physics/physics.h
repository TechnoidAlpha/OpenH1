/*
PHYSICS.H

header included in hcex build.
*/

#ifndef __PHYSICS_H
#define __PHYSICS_H
#pragma once

/* ---------- headers */

#include "math/real_math.h"

/* ---------- constants */

enum
{
	/* port: the mass points a sub-step keeps contacts for (physics_substep):
	physics_definitions.h's MAXIMUM_MASS_POINTS_PER_PHYSICS, which physics.c
	checks */
	MAXIMUM_PHYSICS_SUBSTEP_CONTACTS = 32,
};

/* ---------- macros */

/* ---------- structures */

struct collision_feature_list;
struct mass_point_datum;
struct physics_definition;
struct powered_mass_point_datum;

struct physics_instance
{
	long object_index;
	const struct physics_definition *physics;
	real_matrix4x3 world_matrix;
};

struct physics_test_vector_result
{
	real t;
	real_plane3d plane;
};

/* port: (physics.c's) a vehicle's mass point where its tick began, and what
it met there: the ground under it, the water round it, the ground its
antigrav lifts it from (physics_compute_substep) */
struct physics_substep_contact
{
	real_point3d position;
	real_plane3d ground_plane;
	real ground_depth;
	short ground_material_type;
	boolean on_volatile_surface;
	boolean antigrav;
	real water_depth;
	real_vector3d antigrav_normal;
	real antigrav_height;
};

/* port: one of a tick's sub-steps of a vehicle's physics, in the fast phase
(port/linux/game/sim_rate.h, vehicles.c): physics_substep_set */
struct physics_substep
{
	/* which (0 up to count - 1), of how many */
	short index;
	short count;
	/* its share of a tick (1 / count) */
	real fraction;
	/* (physics.c's) whether the vehicle's physics has run in an earlier
	sub-step of the tick (FALSE as the tick begins: vehicles.c) */
	boolean begun;
	/* (physics.c's, from the tick's first sub-step its physics runs in) the collision force and
	torque the vehicle had kept for this tick, given each sub-step its share;
	the vehicle as the tick began, and its mass points' contacts */
	real_vector3d collision_force;
	real_vector3d collision_torque;
	real_matrix4x3 world_matrix;
	real_point3d tick_position;
	struct physics_substep_contact contacts[MAXIMUM_PHYSICS_SUBSTEP_CONTACTS];
};

/* ---------- prototypes/PHYSICS.C */

boolean physics_instance_new(struct physics_instance *instance, long object_index);
real pin_fraction(real value, real begin, real end);
boolean physics_get_features_in_sphere(
	struct physics_instance const *instance,
	real_point3d const *center,
	real radius,
	real height,
	real width,
	struct collision_feature_list *features);
boolean physics_test_point(
	struct physics_instance const *instance,
	real_point3d const *point);
boolean physics_test_vector(
	struct physics_instance const *instance,
	real_point3d const *point,
	real_vector3d const *vector,
	struct physics_test_vector_result *result);
void physics_compute_new(
	struct physics_instance const *instance,
	struct powered_mass_point_datum const *powered_mass_points,
	struct mass_point_datum *mass_points,
	real_vector3d *total_force,
	real_vector3d *total_torque);
void physics_update(
	long object_index,
	struct powered_mass_point_datum *powered_mass_points,
	struct mass_point_datum *mass_points,
	real_vector3d const *magic_force,
	real_vector3d const *magic_torque);

void render_debug_physics(struct physics_instance *instance);
/* port: the push a vehicle gives a biped it collides with, and the
original vehicle collision damage it deals along it: the vehicle's own
update, and a rider's tick when a vehicle's touch pins it
(biped_riding_contacts in vehicle_riding_bipeds.c) */
void physics_biped_vehicle_collision_push(long vehicle_index, long biped_index, real_vector3d *acceleration);
void physics_biped_vehicle_collision_damage(long vehicle_index, long biped_index,
	real_vector3d const *acceleration);

/* port: physics_update moves a vehicle by a sub-step of a tick, as the
substep says, from now until it is set NULL again (a tick's, as it always
did): vehicles.c, around a vehicle's update in a sub-step of the fast phase
(physics.c, "sub-steps") */
void physics_substep_set(struct physics_substep *substep);


/* ---------- globals */

extern real global_gravity;
extern real global_water_density;
extern real global_air_density;
extern real global_physics_collision_depth;
extern real_plane3d depths_of_hell;

/* ---------- public code */

#endif // __PHYSICS_H
