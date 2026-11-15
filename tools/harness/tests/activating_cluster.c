/*
ACTIVATING_CLUSTER.C

The objects' activating cluster (objects.c): a co-op host's (objects_port_set_
activating_cluster) names a cluster of the bsp loaded when it came, and a
structure bsp switch lets it go (objects_port_structure_bsp_switched); an index
past the loaded bsp's clusters activates none rather than reading past its pvs
(objects_get_activating_cluster_index). A script's camera point is kept across
a switch, as the original keeps it.
*/

#include "harness.h"

enum { _pvs_activation_normal, _pvs_activation_object, _pvs_activation_cluster };
struct structure_bsp { struct { long count; } clusters; };
static struct structure_bsp bsps[3] = { { { 40 } }, { { 64 } }, { { 12 } } };
static struct structure_bsp *loaded = &bsps[0];
static struct structure_bsp *global_structure_bsp_get(void) { return loaded; }
struct object_header_datum { short identifier; byte flags, type; void *datum; };
static struct object_header_datum *object_header_try_and_get(long index) { (void)index; return NULL; }
enum { _object_connected_to_map_bit = 3 };
#define object_get(index) ((struct object_datum *)NULL)
#define object_get_ultimate_parent(index) (index)
struct object_datum { struct { unsigned long flags; struct { short cluster_index; } location; } object; };
static struct
{
	short pvs_activation_type;
	union { long object_index; short cluster_index; } pvs_activation;
} globals, *object_globals = &globals;

/* (the original's FLAG(header->type) && ... in the object case) */
#pragma clang diagnostic ignored "-Wint-in-bool-context"
#include "under_test.inc"

static void camera_point(short cluster_index)
{
	/* (object_pvs_set_camera_point's found cluster) */
	object_globals->pvs_activation_type = _pvs_activation_cluster;
	object_globals->pvs_activation.cluster_index = cluster_index;
	objects_port_activation_from_host = FALSE;
}

int main(int argc, char **argv)
{
	char const *test = argc > 1 ? argv[1] : "";

	if (!strcmp(test, "host-switch"))
	{
		/* (the host's cluster 30 of a10's first bsp; then the intro's switch to
		a bsp of 64, where 30 is a cluster, but another one) */
		objects_port_set_activating_cluster(30);
		CHECK(objects_get_activating_cluster_index() == 30, "the host's cluster not taken");
		loaded = &bsps[1];
		objects_port_structure_bsp_switched();
		CHECK(objects_get_activating_cluster_index() == NONE, "the old bsp's cluster kept across the switch");
		CHECK(object_globals->pvs_activation_type == _pvs_activation_normal, "not back to normal activation");
		objects_port_set_activating_cluster(5);
		CHECK(objects_get_activating_cluster_index() == 5, "the new bsp's cluster not taken");
	}
	else if (!strcmp(test, "stale-index"))
	{
		/* (an index left from a bigger bsp with no switch call between) */
		objects_port_set_activating_cluster(30);
		loaded = &bsps[2];
		CHECK(objects_get_activating_cluster_index() == NONE, "an index past the bsp's clusters returned");
		CHECK(object_globals->pvs_activation_type == _pvs_activation_normal, "the stale index kept");
	}
	else if (!strcmp(test, "script-kept"))
	{
		camera_point(7);
		loaded = &bsps[1];
		objects_port_structure_bsp_switched();
		CHECK(objects_get_activating_cluster_index() == 7, "a script's camera point let go at a switch");
	}
	else
	{
		return 2;
	}
	return 0;
}
