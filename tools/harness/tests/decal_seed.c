#include "harness.h"

/* decal_new (effects/decals.c) on the local seeds of math/random_math.c: what
a decal takes of the seed is the frames', in a tick too, however much of it
the decal takes (decal_new_from_collision takes less of it once the vertex
cache has no room, as the frames leave it) */
#include "config.inc"
struct decal_editor_geometry;
struct random_math_globals { unsigned long global_random_seed, global_local_random_seed; };
static struct random_math_globals random_math_globals;
static unsigned long frame_local_random_seed;
static boolean random_math_port_ticking;
static boolean determinism_log_seeds;
static void determinism_log_seed_taken(boolean local, boolean frames, void const *site) { }
unsigned long *get_global_local_random_seed_address(void);
boolean random_math_port_begin_cosmetic(void);
void random_math_port_end_cosmetic(boolean ticking);

boolean decals_enabled = TRUE;
enum { MAXIMUM_COLLISION_USER_STACK_DEPTH = 8, _collision_user_decals = 3 };
static short global_current_collision_user_depth = 1;
static short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];
struct collision_result { short type; };
struct decal_definition { unsigned short flags; };
static struct decal_definition definition;
static struct decal_definition *decal_definition_get(long index) { return &definition; }

/* the decals' state the frames leave: how many of the seed the decal takes */
static int vertex_cache_room = 3;
static unsigned long seed_seen;
static unsigned long *seed_address_seen;
static int decals_made;

static boolean collision_test_vector(unsigned long flags, real_point3d const *point, real_vector3d const *vector,
	long ignore, struct collision_result *collision)
{
	collision->type = _collision_result_structure;
	return TRUE;
}

static void decal_new_from_media_collision(long index, struct collision_result const *collision,
	real_vector3d const *velocity, real radius_modifier, boolean permanent, short forced_sequence_index,
	struct decal_editor_geometry *editor_geometry) { }

static void decal_new_from_collision(long index, struct collision_result const *collision,
	real_vector3d const *velocity, real radius_modifier, boolean permanent, short forced_sequence_index,
	struct decal_editor_geometry *editor_geometry)
{
	int take;

	seed_address_seen = get_global_local_random_seed_address();
	seed_seen = *seed_address_seen;
	for (take = 0; take < vertex_cache_room; take++)
	{
		unsigned long *seed = get_global_local_random_seed_address();

		*seed = *seed * 1664525 + 1013904223;
	}
	decals_made++;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *case_name = argv[1];
	real_point3d origin = { 1.0f, 2.0f, 3.0f };
	real_vector3d velocity = { 0.0f, 0.0f, -1.0f };

	random_math_port_set_local_seeds(0x1234);
	CASE("tick")
	{
		/* in a tick: the game's local seed is as it was whatever the decals
		took, and the tick still takes the game's */
		unsigned long game = random_math_globals.global_local_random_seed;
		unsigned long frames = frame_local_random_seed;

		random_math_port_set_ticking(TRUE);
		decal_new(7, &origin, &velocity, 1.0f, FALSE, NONE, NULL);
		vertex_cache_room = 0;
		decal_new(7, &origin, &velocity, 1.0f, FALSE, NONE, NULL);
		CHECK(decals_made == 2, "%d decals made", decals_made);
		CHECK(seed_address_seen == &frame_local_random_seed, "the decal took the game's local seed");
		CHECK(random_math_globals.global_local_random_seed == game,
			"the game's local seed moved: %08lx, was %08lx", random_math_globals.global_local_random_seed, game);
		CHECK(frame_local_random_seed != frames, "the frames' seed did not move");
		CHECK(get_global_local_random_seed_address() == &random_math_globals.global_local_random_seed,
			"the tick no longer takes the game's local seed");
	}
	CASE("frame")
	{
		/* between the ticks: the frames' seed, as before */
		unsigned long game = random_math_globals.global_local_random_seed;
		unsigned long frames = frame_local_random_seed;

		random_math_port_set_ticking(FALSE);
		decal_new(7, &origin, &velocity, 1.0f, FALSE, NONE, NULL);
		CHECK(random_math_globals.global_local_random_seed == game, "the game's local seed moved");
		CHECK(frame_local_random_seed != frames, "the frames' seed did not move");
		CHECK(get_global_local_random_seed_address() == &frame_local_random_seed, "the frames take the game's seed");
	}
	CASE("permanent")
	{
		/* a permanent decal (the map's) picks by its place, and leaves both
		seeds as they were */
		unsigned long const *bits = (unsigned long const *)&origin;
		unsigned long game = random_math_globals.global_local_random_seed;
		unsigned long frames = frame_local_random_seed;

		random_math_port_set_ticking(TRUE);
		decal_new(7, &origin, &velocity, 1.0f, TRUE, NONE, NULL);
		CHECK(seed_seen == (bits[2] ^ bits[1] ^ bits[0] ^ 0xdeadc0de), "the permanent decal's seed is not its place's");
		CHECK(random_math_globals.global_local_random_seed == game, "the game's local seed moved");
		CHECK(frame_local_random_seed == frames, "the frames' seed was not put back");
	}
	return 0;
}
