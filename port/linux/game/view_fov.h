/*
VIEW_FOV.H

The first-person view's field of view and weapon (view_fov.c).
*/

#ifndef VIEW_FOV_H
#define VIEW_FOV_H

/* the vertical angle the local player's world view is drawn at (display.fov,
after the observer and before the projection and the visibility frustum
are built), from the game's own */
real render_fov_vertical(short local_player_index, real native_vertical_field_of_view);
/* how much the reticle shrinks or grows to stay on its aim at that view (the
last local view's own projection against the game's): 1 for the stock view */
real render_fov_reticle_scale(short local_player_index);
/* the view's own vertical angle when display.fov widened it, else 0 */
real render_fov_authored_vertical(short local_player_index);

/* what the unit's zoom multiplies the mouse's turn by (input.zoom_sensitivity,
port/linux/include/mouse_sensitivity.h): "cs2", the zoomed field of view
over the unzoomed one, each as the view is drawn; "halo", the game's one
over the magnification. 1 unzoomed. The mouse's alone: the sticks, the
touch controls and VR keep the game's scaling */
real mouse_zoom_look_scale(short local_player_index, long unit_index, short zoom_level);

/* the first-person weapon's projection (display.viewmodel_fov) around its
passes: balanced and nestable, the current local render view only */
void viewmodel_projection_begin(void);
void viewmodel_projection_end(void);

/* where the first-person weapon and hands are drawn (display.viewmodel_offset_x,
_y and _z): this weapon's offset along the view's right, up and forward, in
world units (every weapon's, for now); FALSE when it has none */
union real_vector3d;
boolean viewmodel_offset(long weapon_index, union real_vector3d *offset);
/* the weapon's and hands' node matrices, built at the camera, moved by that
offset: what is drawn and the markers the muzzle's effects, the shells and
the lights are placed at, together */
struct real_matrix4x3;
struct render_camera;
void viewmodel_offset_apply(long weapon_index, struct real_matrix4x3 *node_matrices, short node_count,
	struct render_camera const *camera);

/* whether the first-person weapon, hands and attached visuals are drawn
(display.viewmodel_visible); its firing, sounds and lights go on either way */
boolean viewmodel_is_visible(void);
/* display.viewmodel_shield: the unit's energy shield flare drawn on the
first-person arms too, as on its body */
boolean viewmodel_shield_is_visible(void);
/* whether a pass draws: not one of the first-person weapon's that is hidden */
boolean viewmodel_draws_geometry(boolean first_person);

#endif
