/*
HALO_DYNAMIC_LIGHTS.H

The enhanced dynamic lights (display.dynamic_lights): what the game
(port/linux/game/dynamic_lights.c, source/objects/object_lights.c) hands the
renderer (port/linux/src/d3d8_device.c).
*/

#ifndef HALO_DYNAMIC_LIGHTS_H
#define HALO_DYNAMIC_LIGHTS_H

/* display.dynamic_lights "enhanced" (port/linux/src/d3d8_device.c, the game's
side port/linux/game/dynamic_lights.c): a frame's dynamic light, where it
is drawn, its radius, its color (the game's, 0 to 1) and its cone (the
model lighting's: an axis, scale and offset; omni: 0, 0 and 1) */
struct halo_dynamic_light
{
	float position[3];
	float radius;
	float color[3];
	float cone_offset;
	float axis[3];
	float cone_scale;
	long flags;
};
enum
{
	/* the structure is lit by the game's own pass (a spotlight's gel) */
	HALO_DYNAMIC_LIGHT_MODELS_ONLY = 1,
	/* a glow is drawn about it (a bolt's, a needle's: a projectile with no model) */
	HALO_DYNAMIC_LIGHT_HALO = 2,
	/* a projectile's (debug.dynamic_lights_log) */
	HALO_DYNAMIC_LIGHT_PROJECTILE = 4,
};
/* the vertex constant (as the game numbers them) a model's draw has the
rasterizer lights it is not lit by in (rasterizer_set_model_lighting: its
object's own lights that say so, render.h's RENDER_LIGHTING_OWN_LIGHT), as
four indices, -1 for none; none of the game's vertex programs reads it.
The per-pixel model lighting has it as its model_lights[12] (xgpu.h) */
#define HALO_DYNAMIC_LIGHTS_OWN_LIGHTS_REGISTER (-57)
/* whether the lights are enhanced, the frame's lights (the camera's
position and its right and up), the structure's lightmap pass drawn, and
the glows drawn into the 3D view */
int halo_dynamic_lights_enhanced(void);
void halo_dynamic_lights_set(const struct halo_dynamic_light *lights, int count, const float camera[3],
	const float right[3], const float up[3]);
void halo_dynamic_lights_environment(int drawing);
void halo_dynamic_lights_halos_draw(void);

#endif
