/*
NEAR_CLIP.H

The near clip plane of the local players' views at a wide field of view
(near_clip.c).
*/

#ifndef NEAR_CLIP_H
#define NEAR_CLIP_H

struct render_camera;

/* the near clip distance a local player's view is drawn at (world units),
from the camera (its field of view, viewport and near distance, the game's)
and its frustum's bounds (render_camera_build_frustum_bounds'): the game's
own, or nearer where the view is so wide that the game's near plane would
reach farther than NEAR_CLIP_REACH from the eye at its corners */
real render_near_clip_distance(struct render_camera const *camera, real_rectangle2d const *frustum_bounds);

/* the first-person weapon's near clip distance (world units), its own
(z_near, rasterizer_first_person_weapon_near_clip_distance), from its
projection (the camera's field of view and viewport, the frustum's bounds),
the stock angle that projection widens, and how far the weapon is moved
along the view's forward (display.viewmodel_offset_z, world units): the
game's own at the stock angle unmoved; nearer where the projection is
wider, so that its corners are no farther from the eye than the stock
angle's, and where the weapon is moved nearer the eye, by as much (down to
a 64th of it) */
real render_first_person_near_clip_distance(struct render_camera const *camera,
	real_rectangle2d const *frustum_bounds, real stock_vertical_field_of_view, real z_near, real forward_offset);

#endif
