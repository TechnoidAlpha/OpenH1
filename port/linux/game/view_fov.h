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
last local view's own projection against the game's): 1 for the stock view.
A scope's own pieces, drawn zoomed over its picture, keep to it */
real render_fov_reticle_scale(short local_player_index);
/* a scope's own pieces' scale: the view's (render_fov_reticle_scale), but
the picture's own (1) with display.scale_scoped_fov, whose zoomed view is
wider than the picture was drawn for (#731) */
real render_fov_scope_piece_scale(short local_player_index);
/* whether a zoomed reticle takes its scope's scale (the Xbox's zoom) rather
than keeping its unzoomed size (display.scale_scoped_fov's: #738, #594) */
boolean render_fov_zoomed_reticle_with_scope(short local_player_index);
/* whether the view is still zoomed by the Xbox's zoom (display.scale_scoped_fov
off): at a scope's zoom or along its transition, in or out. The zoom level
is gone as a scope is let go, but the view eases back to the unzoomed one,
and the reticle slides back with it as it slid in (#747) */
boolean render_fov_zoom_easing(short local_player_index);
/* the weapon's reticle's size: its scale at the unzoomed view (display.panini's
magnification of the middle with it), kept through the zoom (the stock game
draws its reticle one size at every zoom: display.fov sizes it, the zoom
does not) */
real render_fov_reticle_size(short local_player_index);
/* the view's own vertical angle when display.fov widened it, else 0 */
real render_fov_authored_vertical(short local_player_index);
/* the local player's unit when its view is the player's own aim, which the
fields of view widen and the lowered reticle turns: on foot or seated,
first or third person, not dead, in a cinematic, a scripted camera or a
view the director holds (and its index); else NULL */
struct unit_datum;
struct unit_datum *render_fov_unit(short local_player_index, long *unit_index);

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
/* the first-person weapon's near clip distance for the projection drawn now
(near_clip.c's render_first_person_near_clip_distance): the game's
(rasterizer_first_person_weapon_near_clip_distance) at the weapon's stock
angle and place, nearer where its projection is widened or it is moved
toward the eye; the game's in the VR mode */
real viewmodel_near_clip_distance(void);

/* where the first-person weapon and hands are drawn: moved along the view's
right, up and forward, in world units (display.viewmodel_offset_x, _y and
_z), and turned, in radians, muzzle up (display.viewmodel_pitch), right
(_yaw) and top to the right (_roll), the local player's own (their profile's:
player_settings.c); each weapon's own (display.viewmodel_weapon, by its tag's
path) added. FALSE when it has none */
struct viewmodel_offset
{
	real right, up, forward;
	real pitch, yaw, roll;
};
boolean viewmodel_offset(short local_player_index, long weapon_index, struct viewmodel_offset *offset);
/* the local player's weapon's and hands' node matrices, built at the camera,
turned about their middle and moved by that offset: what is drawn and the
markers the muzzle's effects, the shells and the lights are placed at,
together. With display.viewmodel_fov the moves right and up are scaled to
go as far on the screen as at the weapon's stock angle */
struct real_matrix4x3;
struct render_camera;
void viewmodel_offset_apply(short local_player_index, long weapon_index, struct real_matrix4x3 *node_matrices,
	short node_count, struct render_camera const *camera);
/* the tag path of the weapon the local player holds now (Settings' THIS
WEAPON rows); FALSE with none */
boolean viewmodel_held_weapon(short local_player_index, char *path, unsigned int size);

/* whether the first-person weapon, hands and attached visuals are drawn
(display.viewmodel_visible) in the view drawn now (render.local_player_index's
own); its firing, sounds and lights go on either way */
boolean viewmodel_is_visible(void);
/* display.viewmodel_shield: the unit's energy shield flare drawn on the
first-person arms too, as on its body */
boolean viewmodel_shield_is_visible(void);
/* whether a pass draws: not one of the first-person weapon's that is hidden */
boolean viewmodel_draws_geometry(boolean first_person);

#endif
