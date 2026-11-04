/*
STRUCTURE_CULLING.H

Conservative geometry tests for the structure's visible surface walk.
*/

#ifndef STRUCTURE_CULLING_H
#define STRUCTURE_CULLING_H

#include "cseries.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	_structure_culling_triangle_frustum_bit,
	_structure_culling_bucket_facing_bit,
	_structure_culling_triangle_facing_bit,
	_structure_culling_finer_buckets_bit,
	_structure_culling_near_first_bit,
	NUMBER_OF_STRUCTURE_CULLING_TESTS
};

enum
{
	_structure_culling_facing_away,
	_structure_culling_facing_toward,
	_structure_culling_facing_mixed
};

/* ---------- structures */

struct structure_bsp;
struct profile_section;

struct structure_culling_cone
{
	real_point3d center;
	real radius;
	real_vector3d axis;
	real cutoff;
};

struct structure_culling_split_surface
{
	long surface_index;
	real_point3d centroid;
	real_vector3d normal;
	boolean facing_eligible;
};

struct structure_culling_pairing
{
	long surface_index;
	short material_index;
	boolean testable; /* ownerless or invalid-vertex subcluster pairings stay visible without triangle tests */
};

/* ---------- globals */

extern struct profile_section render_structure_visibility_subcluster_traversal;
extern struct profile_section render_structure_visibility_surface_traversal;

/* ---------- prototypes */

boolean structure_culling_triangle_outside(
	real_plane3d const planes[6], real_point3d const *point0, real_point3d const *point1, real_point3d const *point2);
/* the triangle faces away from every point within the radius of the camera
(world units; 0, the camera alone) */
boolean structure_culling_triangle_faces_away(real_point3d const *camera, real radius,
	real_point3d const *point0, real_point3d const *point1, real_point3d const *point2);
void structure_culling_cone_build(struct structure_culling_cone *cone, long triangle_count, real_point3d const *points);
/* away and toward apply to every triangle, from every point within the
radius of the camera; mixed leaves each triangle to be tested */
short structure_culling_cone_facing(struct structure_culling_cone const *cone, real_point3d const *camera, real radius);
/* 0 is outside, 1 intersects a plane, and 2 is fully inside all six planes */
short structure_culling_box_test(real_plane3d const planes[6], real_rectangle3d const *box);
/* reserve one end slot per input surface; zero means there was not enough room */
long structure_culling_split(
	struct structure_culling_split_surface *surfaces, long surface_count, long *bucket_ends, long maximum_buckets);
/* the caller resolves group materials or first owners into the same flat table */
long structure_culling_gather_pairings(long const *list, long list_count, boolean from_surface_list,
	short group_material, long surface_count, short const *surface_materials, long const *material_vertex_counts,
	word const *surface_vertex_indices, struct structure_culling_pairing *pairings);
void structure_culling_structure_bsp_loaded(void);
/* the buckets of the map's structure_bsp_index'th bsp, just read where it
loads (another loaded or none), built and kept for the switch to it */
void structure_culling_structure_bsp_read(struct structure_bsp *structure, short structure_bsp_index);
void structure_culling_dispose(void);
void structure_culling_order_clusters(short *order, real const *distances, short count);
boolean structure_culling_traverse(struct structure_bsp *structure);
/* reads the starting tests from the settings */
void structure_culling_initialize(void);
void structure_culling_set_tests(long tests);
/* the walk's camera is every point within this radius (world units) of the
camera's position, not that point alone: in VR, the eyes beside the head's
centre, which the game culls once from (vr_render.c); 0, flat */
void structure_culling_set_camera_radius(real radius);
void structure_culling_debug(boolean enable);
void structure_culling_render_debug(void);

#endif
