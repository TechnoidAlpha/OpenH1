/*
DOOR_PORTALS.C

Closed doors shut their portals to the dynamic lights. The game lights the
structure with a dynamic light only in the clusters its sphere floods into
through the structure's portals (structure_clusters_in_sphere), and an
object only by the lights of its clusters. A door is an object: it never
closes the portal it stands in, so a light just behind a closed door lit the
near side's walls and the objects there, through the door, as an enemy
walked up behind it with a plasma pistol charged.

So each door is paired with the portals it covers, from geometry alone: the
door's collision polygons, where its nodes are, against each portal near it.
A portal is the door's when the door's polygons, met along the portal's
normal within DOOR_PORTALS_DEPTH of it, cover DOOR_PORTALS_COVERED of the
portal's polygon (door_portals_coverage). The pairing is made the first time
the door is seen at rest, at either end of its travel (its device position 0
or 1), for that end: a door's closed end is the one whose polygons cover its
doorway, whichever end that is, and an open door covers nothing. While a door
rests where it covers its portals they are shut; moving, it is open (its gap
lets the light through as soon as it starts to open, as it would).

What the shut portals change, each frame (lights_preprocess_scene):
- the enhanced dynamic lights' clusters (port/linux/game/dynamic_lights.c)
  are flooded without crossing a shut portal (door_portals_clusters_in_sphere);
- for the game's own lights (classic, and the models' lights in either), each
  light's clusters behind a shut door (door_portals_light: reached through
  the portals as the light partition's sphere reaches them, but not with the
  shut ones closed) are left out of its structure passes and of the lights
  the objects there take.

Rendering only: the door, the lights and the clusters are read, nothing the
game simulates is written (the units' illumination, which the AI sees by,
keeps the game's own clusters), and nothing goes between machines.
debug.doors_pass_light leaves the portals open, as the game did.
debug.dynamic_lights_log logs each door's pairing.
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "objects/object_types.h"
#include "devices/device_machines.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_model_definitions.h"
#include "physics/collision_models.h"
#include "rasterizer/rasterizer.h"
#include "scenario/scenario.h"
#include "structures/structures.h"
#include "structures/structure_bsp_definitions.h"
#include "tag_files/tag_groups.h"

#include <math.h>
#include <string.h>

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
unsigned long config_changes(void);
/* port/linux/src (halo.log) */
void platform_log(const char *format, ...);
/* structures.c */
boolean sphere_intersects_cluster_portal(struct structure_bsp *structure, short portal_index,
	real_point3d const *point, real radius);

enum
{
	/* (device_machines.c's _machine_type_door) */
	DOOR_PORTALS_MACHINE_TYPE_DOOR = 0,
	/* the portals a door shuts at most, at each end */
	DOOR_PORTALS_PER_DOOR = 4,
	/* the portals that can be shut (more is no stock map's: past them,
	none is) */
	DOOR_PORTALS_MAXIMUM_PORTALS = 4096,
	/* a door's polygons and their points looked at (past them, the rest
	of a door covers nothing) */
	DOOR_PORTALS_MAXIMUM_POLYGONS = 2048,
	DOOR_PORTALS_MAXIMUM_POINTS = DOOR_PORTALS_MAXIMUM_POLYGONS * MAXIMUM_VERTICES_PER_COLLISION_SURFACE,
	/* each of a portal's triangles (about its middle) is cut into this
	many squared, a point at each one's middle */
	DOOR_PORTALS_SUBDIVISIONS = 6,
};

/* how far from a portal's plane a door's polygon may meet a line along the
portal's normal, in world units (a doorway's depth: a door stands in the
frame, the portal is drawn at one face of it or in the middle) */
#define DOOR_PORTALS_DEPTH 0.5f
/* how far outside a polygon's edges a point still meets it (the seam
between a door's two halves) */
#define DOOR_PORTALS_SLACK 0.01f
/* the share of a portal a door covers to shut it (a portal is drawn about
its doorway, not cut to it: the stock maps' closed doors cover 75% to 100%
of theirs, a burnt door half of its, an open one little) */
#define DOOR_PORTALS_COVERED 0.75f
/* a polygon more nearly edge-on to the portal than this (its normal's
cosine with the portal's) covers nothing */
#define DOOR_PORTALS_EDGE_ON 0.1f

/* the structure's cluster portal and cluster, as structures.c has them */
struct door_portals_cluster_portal
{
	short cluster_indices[2];
	long plane_index;
	real_point3d centroid;
	real bounding_radius;
	unsigned long flags;
	long unused[6];
	struct tag_block vertices;
};

struct door_portals_cluster
{
	byte unused[0x5C];
	struct tag_block portal_indices;
};

typedef char door_portals_cluster_portal_size[sizeof(struct door_portals_cluster_portal) == 0x40 ? 1 : -1];
typedef char door_portals_cluster_size[sizeof(struct door_portals_cluster) == 0x68 ? 1 : -1];

/* a door as paired: the object, where it was and in which structure, and at
each end of its travel whether it was paired there and the portals it covers */
struct door_portals_door
{
	long object_index;
	long definition_index;
	real_point3d position;
	void const *structure;
	boolean paired[2];
	short portal_count[2];
	short portals[2][DOOR_PORTALS_PER_DOOR];
};

static struct
{
	/* each object's (by its absolute index) */
	struct door_portals_door doors[MAXIMUM_OBJECTS_PER_MAP];
	/* the portals shut this frame */
	unsigned long shut[DOOR_PORTALS_MAXIMUM_PORTALS / 32];
	short shut_count;
	/* for each rasterizer light this frame, the clusters it reaches only
	through a shut portal (door_portals_light), if any */
	boolean light_blocks[MAXIMUM_LIGHTS_PER_WINDOW];
	unsigned long light_blocked[MAXIMUM_LIGHTS_PER_WINDOW][MAXIMUM_CLUSTERS_PER_STRUCTURE / 32];
	/* a door's polygons, as it is paired */
	real_point3d points[DOOR_PORTALS_MAXIMUM_POINTS];
	short point_counts[DOOR_PORTALS_MAXIMUM_POLYGONS];
} door_portals;

/* ---------- the pairing */

/* whether a point meets a polygon along a direction (unit), within depth of
it and slack of its edges */
static boolean door_portals_meets(
	real_point3d const *point,
	real_vector3d const *direction,
	real_point3d const *polygon,
	short count)
{
	real_vector3d normal = { 0.0f, 0.0f, 0.0f };
	real_vector3d edge, offset, cross;
	real_point3d met;
	real length, along, distance;
	short index;

	if (count < 3)
		return FALSE;
	/* (its normal, Newell's: any convex polygon's, of any winding) */
	for (index = 0; index < count; index++)
	{
		real_point3d const *a = &polygon[index];
		real_point3d const *b = &polygon[(index + 1) % count];

		normal.i += (a->y - b->y) * (a->z + b->z);
		normal.j += (a->z - b->z) * (a->x + b->x);
		normal.k += (a->x - b->x) * (a->y + b->y);
	}
	length = magnitude3d(&normal);
	if (!(length > 0.0f))
		return FALSE;
	along = dot_product3d(&normal, direction);
	if (!(fabsf(along) > DOOR_PORTALS_EDGE_ON * length))
		return FALSE;
	distance = dot_product3d(&normal, vector_from_points3d(point, &polygon[0], &offset)) / along;
	if (!(fabsf(distance) <= DOOR_PORTALS_DEPTH))
		return FALSE;
	point_from_line3d(point, direction, distance, &met);
	for (index = 0; index < count; index++)
	{
		real edge_length;

		vector_from_points3d(&polygon[index], &polygon[(index + 1) % count], &edge);
		vector_from_points3d(&polygon[index], &met, &offset);
		edge_length = magnitude3d(&edge);
		if (!(edge_length > 0.0f))
			continue;
		/* (its distance inside this edge, in the polygon's plane) */
		if (dot_product3d(cross_product3d(&edge, &offset, &cross), &normal) / (length * edge_length) <
			-DOOR_PORTALS_SLACK)
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* the share of a portal's polygon (count points, its plane's normal) that a
door's polygons (polygon_count of them, each point_counts[] points of
points[] in turn) cover: of points spread evenly over it, the part whose
line along the normal meets one of them (door_portals_meets) */
static real door_portals_coverage(
	real_point3d const *portal,
	short count,
	real_vector3d const *normal,
	real_point3d const *points,
	short const *point_counts,
	short polygon_count)
{
	real_point3d middle = { 0.0f, 0.0f, 0.0f };
	real_vector3d unit = *normal;
	real total = 0.0f, covered = 0.0f;
	real length = magnitude3d(normal);
	short index;

	if (count < 3 || !(length > 0.0f))
		return 0.0f;
	scale_vector3d(&unit, 1.0f / length, &unit);
	for (index = 0; index < count; index++)
	{
		middle.x += portal[index].x / count;
		middle.y += portal[index].y / count;
		middle.z += portal[index].z / count;
	}
	/* (each triangle of the middle and an edge, cut in N squared, a point
	at each one's middle, weighed by the triangle's area) */
	for (index = 0; index < count; index++)
	{
		real_vector3d a, b, cross;
		real area;
		short i, j, half, met = 0;

		vector_from_points3d(&middle, &portal[index], &a);
		vector_from_points3d(&middle, &portal[(index + 1) % count], &b);
		area = magnitude3d(cross_product3d(&a, &b, &cross));
		for (i = 0; i < DOOR_PORTALS_SUBDIVISIONS; i++)
		{
			for (j = 0; i + j < DOOR_PORTALS_SUBDIVISIONS; j++)
			{
				for (half = 0; half < 2 && (half == 0 || i + j + 1 < DOOR_PORTALS_SUBDIVISIONS); half++)
				{
					real s = ((real)i + (half ? 2.0f : 1.0f) / 3.0f) / DOOR_PORTALS_SUBDIVISIONS;
					real t = ((real)j + (half ? 2.0f : 1.0f) / 3.0f) / DOOR_PORTALS_SUBDIVISIONS;
					real_point3d sample;
					short polygon, first = 0;

					sample.x = middle.x + a.i * s + b.i * t;
					sample.y = middle.y + a.j * s + b.j * t;
					sample.z = middle.z + a.k * s + b.k * t;
					for (polygon = 0; polygon < polygon_count; first += point_counts[polygon], polygon++)
					{
						if (door_portals_meets(&sample, &unit, &points[first], point_counts[polygon]))
						{
							met++;
							break;
						}
					}
				}
			}
		}
		total += area;
		covered += area * (real)met / (real)(DOOR_PORTALS_SUBDIVISIONS * DOOR_PORTALS_SUBDIVISIONS);
	}
	return total > 0.0f ? covered / total : 0.0f;
}

/* a door's collision polygons where its nodes are now, into door_portals'
points; how many */
static short door_portals_polygons(
	long object_index)
{
	struct collision_model_instance instance;
	short polygon_count = 0;
	long point_count = 0;
	short node_index;

	if (!collision_model_instance_new(&instance, object_index))
		return 0;
	for (node_index = 0; node_index < instance.node_count; node_index++)
	{
		struct collision_node const *node =
			TAG_BLOCK_GET_ELEMENT(&instance.model->nodes, node_index, struct collision_node);
		struct collision_bsp const *bsp;
		long surface_index;
		short permutation;

		if (node->region_index < 0 || node->region_index >= MAXIMUM_REGIONS_PER_OBJECT || node->bsps.count <= 0)
			continue;
		/* (as the collision tests take it) */
		permutation = (short)PIN((short)instance.region_permutation_indices[node->region_index], 0,
			node->bsps.count - 1);
		bsp = TAG_BLOCK_GET_ELEMENT(&node->bsps, permutation, struct collision_bsp);
		for (surface_index = 0; surface_index < bsp->surfaces.count; surface_index++)
		{
			real_point3d local[MAXIMUM_VERTICES_PER_COLLISION_SURFACE];
			short count, index;

			if (polygon_count >= DOOR_PORTALS_MAXIMUM_POLYGONS ||
				point_count + MAXIMUM_VERTICES_PER_COLLISION_SURFACE > DOOR_PORTALS_MAXIMUM_POINTS)
			{
				return polygon_count;
			}
			count = collision_surface_polygon(bsp, surface_index, local);
			if (count < 3)
				continue;
			for (index = 0; index < count; index++)
				matrix4x3_transform_point(&instance.matrices[node_index], &local[index],
					&door_portals.points[point_count + index]);
			door_portals.point_counts[polygon_count++] = count;
			point_count += count;
		}
	}
	return polygon_count;
}

/* the portals a door covers, resting at an end of its travel */
static void door_portals_pair(
	long object_index,
	struct door_portals_door *door,
	short end)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	struct bsp3d *bsp = global_bsp3d_get();
	struct object_datum *object = object_get(object_index);
	short polygon_count = door_portals_polygons(object_index);
	long portal_index;

	door->portal_count[end] = 0;
	if (!polygon_count || !structure || !bsp)
		return;
	for (portal_index = 0; portal_index < MIN(structure->cluster_portals.count, DOOR_PORTALS_MAXIMUM_PORTALS);
		portal_index++)
	{
		struct door_portals_cluster_portal const *portal = TAG_BLOCK_GET_ELEMENT(&structure->cluster_portals,
			portal_index, struct door_portals_cluster_portal);
		real reach = object->object.bounding_sphere_radius + portal->bounding_radius;
		real coverage;

		if (portal->plane_index < 0 || portal->plane_index >= bsp->planes.count ||
			portal->vertices.count < 3 || portal->vertices.count > MAXIMUM_VERTICES_PER_CLUSTER_PORTAL ||
			!(distance_squared3d(&object->object.bounding_sphere_center, &portal->centroid) <= reach * reach))
		{
			continue;
		}
		coverage = door_portals_coverage(TAG_BLOCK_GET_ELEMENT(&portal->vertices, 0, real_point3d),
			(short)portal->vertices.count, &TAG_BLOCK_GET_ELEMENT(&bsp->planes, portal->plane_index, real_plane3d)->n,
			door_portals.points, door_portals.point_counts, polygon_count);
		if (coverage > 0.0f && config_boolean("debug.dynamic_lights_log"))
		{
			platform_log("door portals: %s at (%.2f %.2f %.2f), position %d: portal %ld (clusters %d %d) %.0f%% covered",
				tag_get_name(object->definition_index), object->object.position.x, object->object.position.y,
				object->object.position.z, end, portal_index, portal->cluster_indices[0], portal->cluster_indices[1],
				coverage * 100.0f);
		}
		if (coverage >= DOOR_PORTALS_COVERED && door->portal_count[end] < DOOR_PORTALS_PER_DOOR)
			door->portals[end][door->portal_count[end]++] = (short)portal_index;
	}
}

/* ---------- each frame */

/* the portals shut this frame: each door's, resting where it covers them
(the lights' clusters behind them forgotten) */
void door_portals_update(
	void)
{
	/* (debug.doors_pass_light, read again as the settings change:
	config_changes) */
	static boolean pass_light_read;
	static unsigned long pass_light_read_at;
	static boolean pass_light;
	struct structure_bsp *structure = global_structure_bsp_get();
	struct object_iterator iterator;
	struct machine_datum *machine;

	memset(door_portals.shut, 0, sizeof(door_portals.shut));
	memset(door_portals.light_blocks, 0, sizeof(door_portals.light_blocks));
	door_portals.shut_count = 0;
	if (!pass_light_read || pass_light_read_at != config_changes())
	{
		pass_light_read_at = config_changes();
		pass_light_read = TRUE;
		pass_light = config_boolean("debug.doors_pass_light") != 0;
	}
	if (!structure || pass_light)
		return;
	object_iterator_new(&iterator, _object_mask_machine, 0);
	while ((machine = (struct machine_datum *)object_iterator_next(&iterator)) != NULL)
	{
		struct door_portals_door *door = &door_portals.doors[DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index)];
		short end, index;

		if (machine_definition_get(machine->definition_index)->machine.type != DOOR_PORTALS_MACHINE_TYPE_DOOR)
			continue;
		end = machine->device.position == 0.0f ? 0 : machine->device.position == 1.0f ? 1 : NONE;
		if (end == NONE)
			continue;
		/* (another object, or this one moved or in another structure, is
		paired again) */
		if (door->object_index != iterator.index || door->definition_index != machine->definition_index ||
			door->structure != structure || memcmp(&door->position, &machine->object.position, sizeof(door->position)))
		{
			memset(door, 0, sizeof(*door));
			door->object_index = iterator.index;
			door->definition_index = machine->definition_index;
			door->position = machine->object.position;
			door->structure = structure;
		}
		if (!door->paired[end])
		{
			door_portals_pair(iterator.index, door, end);
			door->paired[end] = TRUE;
		}
		for (index = 0; index < door->portal_count[end]; index++)
		{
			short portal_index = door->portals[end][index];

			if (!(door_portals.shut[portal_index >> 5] & (1ul << (portal_index & 31))))
			{
				door_portals.shut[portal_index >> 5] |= 1ul << (portal_index & 31);
				door_portals.shut_count++;
			}
		}
	}
}

static boolean door_portals_shut(
	short portal_index)
{
	return portal_index >= 0 && portal_index < DOOR_PORTALS_MAXIMUM_PORTALS &&
		(door_portals.shut[portal_index >> 5] & (1ul << (portal_index & 31))) != 0;
}

/* the clusters a sphere floods into from a cluster through the portals, as
structure_clusters_in_sphere finds them (each portal the sphere meets, to a
cluster not yet reached), but not through a shut portal where shut is TRUE:
each marked in reached (cleared by the caller), the first maximum_count
written to clusters; how many were written. *met_shut (if asked): whether a
shut portal was passed over that the sphere meets, to a cluster not reached. */
static short door_portals_flood(
	short cluster_index,
	real_point3d const *position,
	real radius,
	boolean shut,
	short maximum_count,
	short *clusters,
	unsigned long *reached,
	boolean *met_shut)
{
	struct structure_bsp *structure = global_structure_bsp_get();
	short stack[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	short next[MAXIMUM_CLUSTERS_PER_STRUCTURE];
	short depth = 0, count = 0;
	long cluster_count = structure ? MIN(structure->clusters.count, MAXIMUM_CLUSTERS_PER_STRUCTURE) : 0;

	if (met_shut)
		*met_shut = FALSE;
	if (cluster_index < 0 || cluster_index >= cluster_count)
		return 0;
	reached[cluster_index >> 5] |= 1ul << (cluster_index & 31);
	if (count < maximum_count)
		clusters[count++] = cluster_index;
	stack[depth] = cluster_index;
	next[depth++] = 0;
	while (depth > 0)
	{
		struct door_portals_cluster const *cluster = TAG_BLOCK_GET_ELEMENT(&structure->clusters, stack[depth - 1],
			struct door_portals_cluster);
		short list_index = next[depth - 1]++;
		struct door_portals_cluster_portal const *portal;
		short portal_index, adjacent;

		if (list_index >= cluster->portal_indices.count)
		{
			depth--;
			continue;
		}
		portal_index = *TAG_BLOCK_GET_ELEMENT(&cluster->portal_indices, list_index, short);
		if (portal_index < 0 || portal_index >= structure->cluster_portals.count)
			continue;
		portal = TAG_BLOCK_GET_ELEMENT(&structure->cluster_portals, portal_index, struct door_portals_cluster_portal);
		adjacent = portal->cluster_indices[0] == stack[depth - 1] ? portal->cluster_indices[1] : portal->cluster_indices[0];
		if (adjacent < 0 || adjacent >= cluster_count || (reached[adjacent >> 5] & (1ul << (adjacent & 31))) ||
			!sphere_intersects_cluster_portal(structure, portal_index, position, radius))
		{
			continue;
		}
		if (shut && door_portals_shut(portal_index))
		{
			if (met_shut)
				*met_shut = TRUE;
			continue;
		}
		reached[adjacent >> 5] |= 1ul << (adjacent & 31);
		if (count < maximum_count)
			clusters[count++] = adjacent;
		stack[depth] = adjacent;
		next[depth++] = 0;
	}
	return count;
}

/* the clusters a light's sphere reaches from its cluster, as
structure_clusters_in_sphere finds them, but through no shut portal (the
enhanced dynamic lights'); how many were written */
short door_portals_clusters_in_sphere(
	short cluster_index,
	real_point3d const *position,
	real radius,
	short maximum_count,
	short *clusters)
{
	unsigned long reached[MAXIMUM_CLUSTERS_PER_STRUCTURE / 32];

	if (!door_portals.shut_count)
		return MIN(structure_clusters_in_sphere(cluster_index, position, radius, maximum_count, clusters), maximum_count);
	memset(reached, 0, sizeof(reached));
	return door_portals_flood(cluster_index, position, radius, TRUE, maximum_count, clusters, reached, NULL);
}

/* a rasterizer light just submitted, at emitter, its light partition's
sphere (center, radius): the clusters it reaches only through a shut portal,
those the game's own passes and the objects' lights leave it out of */
void door_portals_light(
	long rasterizer_light_index,
	real_point3d const *emitter,
	real_point3d const *center,
	real radius)
{
	unsigned long shut[MAXIMUM_CLUSTERS_PER_STRUCTURE / 32];
	unsigned long *open;
	struct location location;
	boolean met_shut;
	short index;

	if (!door_portals.shut_count || rasterizer_light_index < 0 || rasterizer_light_index >= MAXIMUM_LIGHTS_PER_WINDOW)
		return;
	scenario_location_from_point(&location, emitter);
	if (location.cluster_index == NONE)
		return;
	memset(shut, 0, sizeof(shut));
	door_portals_flood(location.cluster_index, center, radius, TRUE, 0, NULL, shut, &met_shut);
	if (!met_shut)
		return;
	open = door_portals.light_blocked[rasterizer_light_index];
	memset(open, 0, sizeof(door_portals.light_blocked[0]));
	door_portals_flood(location.cluster_index, center, radius, FALSE, 0, NULL, open, NULL);
	for (index = 0; index < MAXIMUM_CLUSTERS_PER_STRUCTURE / 32; index++)
	{
		open[index] &= ~shut[index];
		if (open[index])
			door_portals.light_blocks[rasterizer_light_index] = TRUE;
	}
}

/* whether a light (its rasterizer index) is behind a shut door from a
cluster */
boolean door_portals_light_blocked(
	long rasterizer_light_index,
	short cluster_index)
{
	return rasterizer_light_index >= 0 && rasterizer_light_index < MAXIMUM_LIGHTS_PER_WINDOW &&
		door_portals.light_blocks[rasterizer_light_index] &&
		cluster_index >= 0 && cluster_index < MAXIMUM_CLUSTERS_PER_STRUCTURE &&
		(door_portals.light_blocked[rasterizer_light_index][cluster_index >> 5] & (1ul << (cluster_index & 31))) != 0;
}

/* a light's clusters with those behind a shut door left out; how many are
left */
short door_portals_light_filter(
	long rasterizer_light_index,
	short count,
	short *clusters)
{
	short kept = 0, index;

	for (index = 0; index < count; index++)
	{
		if (!door_portals_light_blocked(rasterizer_light_index, clusters[index]))
			clusters[kept++] = clusters[index];
	}
	return kept;
}
