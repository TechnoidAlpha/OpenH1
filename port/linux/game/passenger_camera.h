/* The local Warthog and Scorpion passenger view (passenger_camera.c). */
#ifndef PASSENGER_CAMERA_H
#define PASSENGER_CAMERA_H

#include "math/real_math.h"

struct player_control_unit_camera_info;

boolean passenger_camera_enabled(short local_player_index);
boolean passenger_camera_info(short local_player_index, struct player_control_unit_camera_info *info);
void passenger_camera_clamp_facing(short local_player_index, real_euler_angles2d *facing);

/* Scoped to crosshairs_draw: one ray query for all of a reticle's pieces.
The offset says whether it moved the point to the shot (which, projected
through the drawn view, needs no lowered reticle's offset besides): the
point to the whole unit short of it, and rest how far past that, in the
screen's units as drawn (hud_draw.c's reticle scaled), that the reticle
keeps to the shot, not a unit short of it in steps. */
boolean passenger_camera_reticle_begin(short local_player_index, long weapon_index);
boolean passenger_camera_reticle_offset(point2d *point, real reticle_scale, real_vector2d *rest);
void passenger_camera_reticle_end(void);

#endif
