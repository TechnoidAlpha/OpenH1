/*
FAR_CLIP.C

display.draw_distance: how far the local players' views draw the world, the
far clip plane (world units; the game's own is 1024, rasterizer.c's
rasterizer_global_defaults). The game culls and clips the world at it:
the projection's far plane (main.c's set_window_camera_values), the
portals and clusters the structure's visibility walks (structure_
visibility.c, from the render camera's far distance) and the objects and
the surfaces culled against the frustum (render_cameras.c's frustum,
structure_culling.c). It is drawing alone: the simulation, the AI's sight
and everything a tick reads never see the render camera.

What else follows the far distance stays as it was:
- fog: a fog that reaches full density nearer than the far plane (render.c)
  still brings the far plane in to where everything is fogged; a map with no
  atmospheric fog has it start at the far plane (rasterizer_xbox.c), where
  nothing is drawn, at any distance;
- the teammates' marks over their heads fade with their distance as a
  share of the game's own far distance, not the drawn one (hud.c);
- the depth buffer: Vulkan and Metal keep depth reversed in 32-bit floats
  (port/linux/vulkan/xgpu_vulkan.h's XGPU_VULKAN_REVERSED_DEPTH), whose
  precision is a share of the distance at any distance: 10000 units away
  a step is well under a centimetre of the world, so the far plane moved
  out costs the near world nothing. (A Vulkan device without that depth
  format falls back to 24 bits, where a step at a distance d is about
  d * d / 2^20 units: 1 at 1024, as the game's own far plane has it.)
- the sky is drawn behind the world at any distance, and its own models
  are the sky's (not clipped by this).

A script or the console may set the game's far distance
(rasterizer_far_clip_distance): nearer than 1024, as a cinematic might,
it is kept; otherwise the farther of it and this setting.
*/

#include "cseries.h"
#include "far_clip.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_globals_internal.h"
#include "player_settings.h"

/* display.draw_distance's range (world units): the game's own, to
ChupathingyCE's farthest (its Far Clip) */
#define DRAW_DISTANCE_LEAST 1024.0
#define DRAW_DISTANCE_MOST 10000.0

real render_far_clip_distance(short local_player_index)
{
	static struct
	{
		boolean initialized;
		unsigned long read_at;
		real distance;
	} players[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	real game_distance = rasterizer_globals.far_clip_distance;
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		double setting = config_player_real(local_player_index, "display.draw_distance");

		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
		players[slot].distance = setting > DRAW_DISTANCE_LEAST && setting <= DRAW_DISTANCE_MOST ? (real)setting : 0.0f;
	}
	if (!(game_distance >= rasterizer_global_defaults.far_clip_distance) || !(players[slot].distance > game_distance))
		return game_distance;
	return players[slot].distance;
}
