/*
BIPED_LIMP_NOODLE.H

header included in hcex build.
*/

#ifndef __BIPED_LIMP_NOODLE_H
#define __BIPED_LIMP_NOODLE_H
#pragma once

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/BIPED_LIMP_NOODLE.C */

long biped_limp_noodle_get_max_relaxation_iterations(
	long biped_index);

void validate_real_vector3d_axes3(
	real_vector3d *forward,
	real_vector3d *left,
	real_vector3d *up);

boolean biped_limp_noodle_relax_nodes_onto_environment(
	long biped_index);

/* port: a dead biped's animated nodes raised onto the ground that rises
under them (biped_limp_noodle.c) */
void biped_limp_noodle_drape_dying(
	long biped_index,
	real_matrix4x3 *node_matrices);

/* ---------- globals */

/* ---------- public code */

#endif // __BIPED_LIMP_NOODLE_H
