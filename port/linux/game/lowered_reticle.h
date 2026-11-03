/*
LOWERED_RETICLE.H

The reticle below the middle of the view (display.lowered_reticle,
lowered_reticle.c): the view looks above the aim, and the aim lands on the
reticle.
*/

#ifndef LOWERED_RETICLE_H
#define LOWERED_RETICLE_H

union real_vector3d;
union real_point2d;
union point2d;
struct render_camera;
struct render_frustum;

/* (each frame, as the local render view is set from its observer) turns the
view up from the aim by the angle that puts the aim the setting's fraction
of the view's height below its middle, at the vertical angle the view is
drawn at (after display.fov); the game's own field of view tells a scope's
zoom, which brings the reticle back to the middle. Leaves forward and up
alone when the setting is off or the view is not the player's aim */
void lowered_reticle_view(short local_player_index, real game_field_of_view, real vertical_field_of_view,
	union real_vector3d *forward, union real_vector3d *up);

/* how far the local player's view looks above its aim (radians), this frame;
0 for none */
real lowered_reticle_lift(short local_player_index);

/* where the aim lands on the screen: its offset from the middle of the view
(window pixels, y down) through this camera, the frame's own (its
projection, display.fov's, any other). FALSE when the reticle is not
lowered: the middle. The one place the HUD asks */
boolean lowered_reticle_screen_offset(short local_player_index, struct render_camera const *camera,
	struct render_frustum const *frustum, union real_point2d *offset);

/* a reticle's point moved there (render.local_player_index's, render.camera's),
given at the HUD's size: the HUD scales the reticle about the middle by
reticle_scale (view_fov.c), offset and all */
void lowered_reticle_offset_point(union point2d *point, real reticle_scale);

/* the camera the first-person weapon is posed at: the view turned back down
to the aim (the same camera when the reticle is not lowered), so the weapon
is held along the aim and points at the reticle. Its right, up and forward
are the aim's, which display.viewmodel_offset_* move the weapon along */
struct render_camera const *lowered_reticle_aim_camera(short local_player_index,
	struct render_camera const *camera);

#endif
