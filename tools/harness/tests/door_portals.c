/*
DOOR_PORTALS.C

Closed doors shut their portals to the lights (port/linux/game/door_portals.c):
a door paired with the portals its polygons cover, at rest, and the lights'
flood through the portals stopped at the shut ones. The world is a corridor
along x of four clusters, 0 to 3, each a unit long, with a square portal
(y and z from -1 to 1) between each two at x = 1, 2 and 3, and a side room,
cluster 4, joining clusters 0 and 2 (for a way around). A sphere meets a
portal when it reaches the portal's bounding sphere (the stock test is the
game's own, structures.c). A door is made-up polygons.
*/

#include "harness.h"
#include <math.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define square_root(x) sqrtf(x)
typedef struct { real_vector3d n; real d; } real_plane3d;
struct tag_block { long count; void *address; void *definition; };
#define TAG_BLOCK_GET_ELEMENT(block, index, type) (&((type *)(block)->address)[index])
#define MAXIMUM_OBJECTS_PER_MAP 64
#define MAXIMUM_LIGHTS_PER_WINDOW 128
#define MAXIMUM_CLUSTERS_PER_STRUCTURE 512
#define MAXIMUM_VERTICES_PER_CLUSTER_PORTAL 128
#define MAXIMUM_VERTICES_PER_COLLISION_SURFACE 8
#define _object_mask_machine 1

struct structure_bsp { struct tag_block clusters, cluster_portals; };
struct bsp3d { struct tag_block planes; };
struct location { long leaf_index; short cluster_index; };
struct object_iterator { long index; long next; };
struct machine_datum
{
	long definition_index;
	struct { real_point3d position, bounding_sphere_center; real bounding_sphere_radius; } object;
	struct { real position; } device;
};
struct machine_definition { struct { short type; } machine; };
#define object_datum machine_datum

static int pass_light;
static int config_boolean(char const *name) { return !strcmp(name, "debug.doors_pass_light") && pass_light; }
/* (moves as the setting changes, as config.toml's count does) */
static unsigned long config_changes(void) { return (unsigned long)pass_light; }
static void platform_log(char const *format, ...) { (void)format; }
static char const *tag_get_name(long index) { (void)index; return "door"; }

static struct structure_bsp world;
static struct bsp3d world_bsp;
static struct structure_bsp *global_structure_bsp_get(void) { return &world; }
static struct bsp3d *global_bsp3d_get(void) { return &world_bsp; }

/* the doors: machines, each object's definition its kind (1: a door, 2: a
platform) */
static struct machine_datum machines[4];
static long machine_count;
static struct machine_definition kinds[3] = { { { 0 } }, { { 0 } }, { { 2 } } };
#define machine_definition_get(index) (&kinds[index])
#define object_get(index) (&machines[DATUM_INDEX_TO_ABSOLUTE_INDEX(index)])
static void object_iterator_new(struct object_iterator *iterator, long mask, int flags)
{
	(void)mask;
	(void)flags;
	iterator->next = 0;
}
static void *object_iterator_next(struct object_iterator *iterator)
{
	if (iterator->next >= machine_count)
		return NULL;
	iterator->index = 0x12340000 | iterator->next;
	return &machines[iterator->next++];
}

/* the cluster a point is in: by x, the side room above z 5 */
static void scenario_location_from_point(struct location *location, real_point3d const *point)
{
	location->cluster_index = point->z > 5.0f ? 4 : point->x < 0.0f || point->x >= 4.0f ? NONE : (short)point->x;
}

static int door_portals_flood_open(short, real_point3d const *, real, short, short *);
static short door_portals_polygons(long object_index);
static boolean sphere_intersects_cluster_portal(struct structure_bsp *structure, short portal_index,
	real_point3d const *point, real radius);
#define structure_clusters_in_sphere(cluster, position, radius, maximum, clusters) \
	door_portals_flood_open(cluster, position, radius, maximum, clusters)

/* (a door's polygons: door_polygons, put where door_portals_polygons puts
them) */
static real_point3d door_points[64];
static short door_point_counts[16];
static short door_polygon_count;

#include "under_test.inc"

static short door_portals_polygons(long object_index)
{
	(void)object_index;
	memcpy(door_portals.points, door_points, sizeof(door_points));
	memcpy(door_portals.point_counts, door_point_counts, sizeof(door_point_counts));
	return door_polygon_count;
}

static boolean sphere_intersects_cluster_portal(struct structure_bsp *structure, short portal_index,
	real_point3d const *point, real radius)
{
	struct door_portals_cluster_portal const *portal =
		TAG_BLOCK_GET_ELEMENT(&structure->cluster_portals, portal_index, struct door_portals_cluster_portal);
	real_plane3d const *plane = TAG_BLOCK_GET_ELEMENT(&world_bsp.planes, portal->plane_index, real_plane3d);
	real reach = radius + portal->bounding_radius;

	return fabsf(dot_product3d((real_vector3d const *)point, &plane->n) - plane->d) < radius &&
		distance_squared3d(point, &portal->centroid) < reach * reach;
}

static int door_portals_flood_open(short cluster, real_point3d const *position, real radius, short maximum,
	short *clusters)
{
	unsigned long reached[MAXIMUM_CLUSTERS_PER_STRUCTURE / 32];

	memset(reached, 0, sizeof(reached));
	return door_portals_flood(cluster, position, radius, FALSE, maximum, clusters, reached, NULL);
}

/* the corridor: portals 0, 1, 2 at x = 1, 2, 3; 3 and 4 the side room's,
from 0 and from 2 (high up, at z 6) */
static struct door_portals_cluster clusters[5];
static struct door_portals_cluster_portal portals[5];
static short portal_lists[5][3];
static real_point3d portal_points[5][4];
static real_plane3d planes[5];

static void corridor(void)
{
	static short const lists[5][3] = { { 0, 3, -1 }, { 0, 1, -1 }, { 1, 2, 4 }, { 2, -1, -1 }, { 3, 4, -1 } };
	short index, corner;

	memset(&door_portals, 0, sizeof(door_portals));
	memset(clusters, 0, sizeof(clusters));
	memset(portals, 0, sizeof(portals));
	for (index = 0; index < 5; index++)
	{
		real x = index < 3 ? (real)(index + 1) : index == 3 ? 0.5f : 2.5f;
		real z = index < 3 ? 0.0f : 6.0f;

		portals[index].cluster_indices[0] = index < 3 ? index : index == 3 ? 0 : 2;
		portals[index].cluster_indices[1] = index < 3 ? index + 1 : 4;
		portals[index].plane_index = index;
		portals[index].centroid = (real_point3d){ x, 0.0f, z };
		portals[index].bounding_radius = 1.415f;
		for (corner = 0; corner < 4; corner++)
			portal_points[index][corner] = (real_point3d){ x, corner == 1 || corner == 2 ? 1.0f : -1.0f,
				z + (corner >= 2 ? 1.0f : -1.0f) };
		portals[index].vertices = (struct tag_block){ 4, portal_points[index], NULL };
		planes[index] = (real_plane3d){ { 1.0f, 0.0f, 0.0f }, x };
		memcpy(portal_lists[index], lists[index], sizeof(portal_lists[index]));
		clusters[index].portal_indices = (struct tag_block){ lists[index][2] >= 0 ? 3 : lists[index][1] >= 0 ? 2 : 1,
			portal_lists[index], NULL };
	}
	world.clusters = (struct tag_block){ 5, clusters, NULL };
	world.cluster_portals = (struct tag_block){ 5, portals, NULL };
	world_bsp.planes = (struct tag_block){ 5, planes, NULL };
	memset(machines, 0, sizeof(machines));
	machine_count = 0;
	door_polygon_count = 0;
	pass_light = 0;
}

/* a door's polygon: the rectangle at x from y0 to y1 and z0 to z1 */
static void door_panel(real x, real y0, real y1, real z0, real z1)
{
	short first = 0, index;

	for (index = 0; index < door_polygon_count; index++)
		first += door_point_counts[index];
	door_points[first + 0] = (real_point3d){ x, y0, z0 };
	door_points[first + 1] = (real_point3d){ x, y1, z0 };
	door_points[first + 2] = (real_point3d){ x, y1, z1 };
	door_points[first + 3] = (real_point3d){ x, y0, z1 };
	door_point_counts[door_polygon_count++] = 4;
}

static real portal_coverage(short portal)
{
	return door_portals_coverage(portal_points[portal], 4, &planes[portal].n, door_points, door_point_counts,
		door_polygon_count);
}

/* a door in the doorway of portal 1, at device position */
static void door(real position)
{
	machines[0].definition_index = 1;
	machines[0].object.position = (real_point3d){ 2.0f, 0.0f, 0.0f };
	machines[0].object.bounding_sphere_center = machines[0].object.position;
	machines[0].object.bounding_sphere_radius = 1.5f;
	machines[0].device.position = position;
	machine_count = 1;
}

static boolean reaches(short const *clusters, short count, short cluster)
{
	short index;

	for (index = 0; index < count; index++)
	{
		if (clusters[index] == cluster)
			return TRUE;
	}
	return FALSE;
}

int main(int argc, char **argv)
{
	char const *case_name;

	CHECK(argc == 2, "one named case");
	case_name = argv[1];
	CASE("coverage")
	{
		corridor();
		/* one panel, a little behind the portal's plane */
		door_panel(2.1f, -1.0f, 1.0f, -1.0f, 1.0f);
		CHECK(portal_coverage(1) > 0.99f, "a whole door covers its portal: %f", portal_coverage(1));
		CHECK(portal_coverage(0) == 0.0f && portal_coverage(2) == 0.0f, "nor the next ones: %f %f",
			portal_coverage(0), portal_coverage(2));
		/* two halves meeting in the middle, the frame a little wider */
		corridor();
		door_panel(2.05f, -1.05f, 0.0f, -1.05f, 1.05f);
		door_panel(1.95f, 0.0f, 1.05f, -1.05f, 1.05f);
		CHECK(portal_coverage(1) > 0.99f, "two halves cover it: %f", portal_coverage(1));
		/* the halves slid apart, partly and fully */
		corridor();
		door_panel(2.05f, -1.6f, -0.4f, -1.0f, 1.0f);
		door_panel(1.95f, 0.4f, 1.6f, -1.0f, 1.0f);
		CHECK(portal_coverage(1) > 0.5f && portal_coverage(1) < DOOR_PORTALS_COVERED, "partly open: %f",
			portal_coverage(1));
		corridor();
		door_panel(2.05f, -3.0f, -1.0f, -1.0f, 1.0f);
		door_panel(1.95f, 1.0f, 3.0f, -1.0f, 1.0f);
		CHECK(portal_coverage(1) < 0.05f, "open: %f", portal_coverage(1));
		/* a panel a meter's depth away, and one edge-on (a jamb) */
		corridor();
		door_panel(2.0f + DOOR_PORTALS_DEPTH + 0.2f, -1.0f, 1.0f, -1.0f, 1.0f);
		CHECK(portal_coverage(1) == 0.0f, "a panel past the doorway's depth: %f", portal_coverage(1));
		corridor();
		door_points[0] = (real_point3d){ 1.8f, -0.01f, -1.0f };
		door_points[1] = (real_point3d){ 2.2f, 0.01f, -1.0f };
		door_points[2] = (real_point3d){ 2.2f, 0.01f, 1.0f };
		door_points[3] = (real_point3d){ 1.8f, -0.01f, 1.0f };
		door_point_counts[door_polygon_count++] = 4;
		CHECK(portal_coverage(1) == 0.0f, "a polygon nearly edge-on: %f", portal_coverage(1));
		return 0;
	}
	CASE("pairing")
	{
		short end;

		corridor();
		door(0.0f);
		/* the door covers portal 1 wholly, and a third of portal 0 (a
		part of it reaching into the last doorway) */
		door_panel(2.0f, -1.0f, 1.0f, -1.0f, 1.0f);
		door_panel(1.0f, 0.4f, 1.0f, -1.0f, 1.0f);
		door_portals_update();
		CHECK(door_portals.doors[0].paired[0] && !door_portals.doors[0].paired[1], "paired at its end: %d %d",
			door_portals.doors[0].paired[0], door_portals.doors[0].paired[1]);
		CHECK(door_portals.doors[0].portal_count[0] == 1 && door_portals.doors[0].portals[0][0] == 1,
			"paired with its own portal only: %d portals, the first %d", door_portals.doors[0].portal_count[0],
			door_portals.doors[0].portals[0][0]);
		CHECK(door_portals_shut(1) && !door_portals_shut(0) && !door_portals_shut(2) && door_portals.shut_count == 1,
			"its portal shut: %d %d %d", door_portals_shut(0), door_portals_shut(1), door_portals_shut(2));
		/* moving (its polygons as they were): open, and not paired */
		machines[0].device.position = 0.5f;
		door_portals_update();
		CHECK(door_portals.shut_count == 0, "a moving door shuts nothing: %d", door_portals.shut_count);
		CHECK(!door_portals.doors[0].paired[1], "nor is paired at an end");
		/* resting open: its halves aside cover nothing there */
		machines[0].device.position = 1.0f;
		door_polygon_count = 0;
		door_panel(2.0f, -3.0f, -1.0f, -1.0f, 1.0f);
		door_panel(2.0f, 1.0f, 3.0f, -1.0f, 1.0f);
		door_portals_update();
		CHECK(door_portals.doors[0].paired[1] && door_portals.doors[0].portal_count[1] == 0 &&
			door_portals.shut_count == 0, "open, it covers nothing: %d", door_portals.doors[0].portal_count[1]);
		/* closed again: as paired first, not paired again */
		machines[0].device.position = 0.0f;
		door_portals_update();
		CHECK(door_portals_shut(1) && door_portals.shut_count == 1, "closed again, shut again");
		/* a door whose closed end is 1 (it opens backwards) */
		corridor();
		door(1.0f);
		door_panel(2.0f, -1.0f, 1.0f, -1.0f, 1.0f);
		door_portals_update();
		CHECK(door_portals_shut(1), "a door closed at its end 1 shuts its portal");
		/* the door moved to the next doorway: paired again there */
		door_polygon_count = 0;
		door_panel(3.0f, -1.0f, 1.0f, -1.0f, 1.0f);
		machines[0].object.position.x = 3.0f;
		machines[0].object.bounding_sphere_center.x = 3.0f;
		door_portals_update();
		CHECK(door_portals_shut(2) && !door_portals_shut(1), "moved, it shuts the portal where it is: %d %d",
			door_portals_shut(1), door_portals_shut(2));
		/* a platform is no door; the setting leaves all open */
		corridor();
		door(0.0f);
		door_panel(2.0f, -1.0f, 1.0f, -1.0f, 1.0f);
		machines[0].definition_index = 2;
		door_portals_update();
		CHECK(door_portals.shut_count == 0, "a platform shuts nothing");
		machines[0].definition_index = 1;
		pass_light = 1;
		door_portals_update();
		CHECK(door_portals.shut_count == 0, "debug.doors_pass_light shuts nothing");
		pass_light = 0;
		door_portals_update();
		CHECK(door_portals.shut_count == 1, "else it shuts its portal");
		for (end = 0; end < 2; end++)
			CHECK(door_portals.doors[0].portal_count[end] <= DOOR_PORTALS_PER_DOOR, "room for its portals");
		return 0;
	}
	CASE("flood")
	{
		real_point3d light = { 0.5f, 0.0f, 0.0f };
		short clusters[8];
		short count;

		corridor();
		count = door_portals_clusters_in_sphere(0, &light, 3.0f, 8, clusters);
		CHECK(count == 4 && reaches(clusters, count, 3), "no door: the corridor's four: %d", count);
		door(0.0f);
		door_panel(2.0f, -1.0f, 1.0f, -1.0f, 1.0f);
		door_portals_update();
		count = door_portals_clusters_in_sphere(0, &light, 3.0f, 8, clusters);
		CHECK(count == 2 && reaches(clusters, count, 0) && reaches(clusters, count, 1),
			"the door shut: its near side's two: %d", count);
		/* from the far side, the near side is behind it */
		light.x = 3.5f;
		count = door_portals_clusters_in_sphere(3, &light, 3.0f, 8, clusters);
		CHECK(count == 2 && reaches(clusters, count, 2) && reaches(clusters, count, 3), "from the far side: %d", count);
		/* a sphere big enough to reach the side room goes round */
		light = (real_point3d){ 0.5f, 0.0f, 3.0f };
		count = door_portals_clusters_in_sphere(0, &light, 4.5f, 8, clusters);
		CHECK(reaches(clusters, count, 4) && reaches(clusters, count, 2), "round by the side room: %d", count);
		/* at most maximum written */
		count = door_portals_clusters_in_sphere(0, &light, 4.5f, 2, clusters);
		CHECK(count == 2, "no more than asked for: %d", count);
		return 0;
	}
	CASE("light")
	{
		real_point3d light = { 1.5f, 0.0f, 0.0f };
		short clusters[8] = { 0, 1, 2, 3 };

		corridor();
		door(0.0f);
		door_panel(2.0f, -1.0f, 1.0f, -1.0f, 1.0f);
		door_portals_update();
		door_portals_light(5, &light, &light, 2.0f);
		CHECK(!door_portals_light_blocked(5, 0) && !door_portals_light_blocked(5, 1), "its own side lit");
		CHECK(door_portals_light_blocked(5, 2) && door_portals_light_blocked(5, 3), "behind the door: %d %d",
			door_portals_light_blocked(5, 2), door_portals_light_blocked(5, 3));
		CHECK(!door_portals_light_blocked(6, 2), "another light is not");
		CHECK(door_portals_light_filter(5, 4, clusters) == 2 && clusters[0] == 0 && clusters[1] == 1,
			"its clusters with those behind the door left out: %d %d", clusters[0], clusters[1]);
		/* from the far side, the same door */
		light.x = 2.5f;
		door_portals_light(7, &light, &light, 2.0f);
		CHECK(door_portals_light_blocked(7, 1) && !door_portals_light_blocked(7, 2), "the far side's: %d %d",
			door_portals_light_blocked(7, 1), door_portals_light_blocked(7, 2));
		/* a light whose sphere reaches no shut portal: nothing behind */
		light.x = 0.5f;
		door_portals_light(8, &light, &light, 0.4f);
		CHECK(!door_portals_light_blocked(8, 2), "one not reaching the door");
		/* the next frame, the door open */
		machines[0].device.position = 0.5f;
		door_portals_update();
		light.x = 1.5f;
		door_portals_light(5, &light, &light, 2.0f);
		CHECK(!door_portals_light_blocked(5, 2) && !door_portals_light_blocked(7, 1), "the door open: none behind it");
		return 0;
	}
	fprintf(stderr, "unknown case %s\n", case_name);
	return 2;
}
