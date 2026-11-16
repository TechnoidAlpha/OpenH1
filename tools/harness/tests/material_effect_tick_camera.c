/*
MATERIAL_EFFECT_TICK_CAMERA.C

material_effect_visible (source/effects/material_effects.c): a footstep, an
item's bounce or a particle's strike makes its effect and sound only within
10 world units of a local player's camera. A tick at game.sim_rate 30 runs
whole between two frames, so it sees the camera the frames left before it
began. At 60 and 120 the frames run between a tick's sub-steps and move the
camera on; the fast phase sees the camera as the tick began
(material_effects_tick_begin), and the frames the camera as it is.

The case is a player's own spawn: the tick that makes the player begins
with the camera far off (at the origin, or the dead player's view), the
frames between its sub-steps bring the camera to the new player, and the
tick's last sub-step makes the new biped's landing footsteps beside it.
*/

#include "harness.h"

enum
{
	MAXIMUM_NUMBER_OF_LOCAL_PLAYERS = 4
};

struct observer_result
{
	real_point3d position;
};

/* the fake world: one local player, its camera, and the sub-step running */
static struct observer_result cameras[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
static long local_player_indices[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS] = { 0, NONE, NONE, NONE };
static short substep_index = NONE;

static short local_player_count(void)
{
	return 1;
}

static long local_player_get_player_index(short local_player_index)
{
	return local_player_indices[local_player_index];
}

static struct observer_result const *observer_get_camera(short local_player_index)
{
	return &cameras[local_player_index];
}

static short sim_substep_index(void)
{
	return substep_index;
}

static real distance_squared3d(real_point3d const *a, real_point3d const *b)
{
	real x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;

	return x * x + y * y + z * z;
}

#include "under_test.inc"

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	real_point3d const spawn = { 61.3f, -65.6f, 1.7f };
	real_point3d const far_off = { 0.0f, 0.0f, 0.0f };
	short substep;

	CASE("spawn")
	{
		short const counts[] = { 1, 2, 4 };
		int rate;

		for (rate = 0; rate < 3; rate++)
		{
			short const count = counts[rate];
			boolean heard = FALSE;

			/* the tick begins with the camera far off: its own work makes the player */
			cameras[0].position = far_off;
			material_effects_tick_begin();
			for (substep = 0; substep < count; substep++)
			{
				/* the frames before each sub-step past the first bring the camera to the new player */
				if (substep > 0)
				{
					cameras[0].position = spawn;
					substep_index = NONE;
					/* (a frame's own effects, a particle's strike: the camera as it is) */
					CHECK(material_effect_visible(&spawn), "a frame at %d sub-steps a tick sees its own camera", count);
				}
				substep_index = substep;
				/* the tick's last sub-step: the new biped's landing footsteps */
				if (substep == count - 1)
					heard = material_effect_visible(&spawn);
			}
			substep_index = NONE;
			CHECK(!heard, "at %d sub-steps a tick the spawn's landing is made, which a tick at 30 never makes", count);

			/* the next tick begins with the camera at the player: its footsteps are made */
			cameras[0].position = spawn;
			material_effects_tick_begin();
			substep_index = (short)(count - 1);
			CHECK(material_effect_visible(&spawn), "at %d sub-steps a tick the next tick's footsteps are not made", count);
			substep_index = NONE;
		}
		printf("ok\n");
	}

	return 0;
}
