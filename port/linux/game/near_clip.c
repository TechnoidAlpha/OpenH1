/*
NEAR_CLIP.C

The near clip plane of the local players' views at a wide field of view
(display.fov, display.vehicle_fov). The game draws the world clipped at a
plane 0.0625 world units ahead of the eye (rasterizer_near_clip_distance,
rasterizer.c's rasterizer_global_defaults), a plane across the view, not a
sphere about the eye: a point at an angle a off the view's axis is clipped
nearer than 0.0625 / cos a. At the stock view the near plane's corners are
0.085 units from the eye (16:9), and nothing the player stands against is
cut. At a wide one the sides are nearly beside the eye: a wall the player
stands against is 0.2 units (the cyborg's collision radius) from it, and
the plane cuts it wherever it is more than atan(0.2 / 0.0625), 72.6
degrees, off the axis, so from about 145 degrees across 16:9 a wall
beside the player is drawn cut along a line near the screen's side, the
world behind it showing (#604). The Panini projection (panini.c) redraws
the rectilinear view as it was drawn, cut walls and all.

So the plane is brought nearer as the view widens, to where its farthest
point, a corner of the view, is NEAR_CLIP_REACH from the eye: then nothing
NEAR_CLIP_REACH or more from the eye is clipped, at any angle the view
draws. The corners are the view's as the game's frustum takes them
(render_camera_build_frustum): its vertical field of view, its viewport's
shape and its bounds (split screen's windows are off their middles, so a
corner of one is farther out than the other side's), which the culling
(render_cameras.c's frustum, structure_culling.c) and the projection share.
The lowered reticle's view is turned up, not skewed (lowered_reticle.c),
and the Panini pass's supersampling draws the same frustum at more pixels.
The plane is never moved farther than the game's: the stock view, and any
whose corners are within NEAR_CLIP_REACH, are drawn exactly as before.

The depth buffer (Vulkan's and Metal's reversed 32-bit floats: far_clip.c)
keeps its precision as a share of the distance whatever the near distance
is. It is drawing alone: nothing a tick reads sees the render camera.

The first-person weapon has a near plane of its own (rasterizer_first_
person_weapon_near_clip_distance, 0.0117) and a projection of its own
(view_fov.c's viewmodel_projection_begin): the unit's stock angle, widened
by display.viewmodel_fov, and by display.panini's magnification of the
middle so that the weapon is its own size there. At the stock angle, the
weapon's near plane cuts nothing of it in view: what the plane would cut
(its rear, beside the eye) is off the screen. A widened projection brings
that rear onto the screen at its sides, the near plane's corners reaching
farther from the eye, and the rear is drawn cut along the plane, the cut
moving as the weapon bobs and sways with the player's steps (#604: at 160
degrees with display.panini, the rear of the plasma rifle and the
assault rifle at the bottom right). So, as for the world, the weapon's
plane is brought nearer as its projection widens, to where its corners
are as far from the eye as the stock angle's corners are in the same
viewport: nothing of the weapon farther than that from the eye is cut,
at any width, as at the stock angle. display.viewmodel_offset_z moves the
weapon nearer the eye (by up to 20 centimeters): the plane is brought as
much nearer, so that it cuts the weapon where it cut it unmoved, down to
FIRST_PERSON_NEAR_CLIP_LEAST of the game's distance (what was nearer than
that is now at or behind the eye). The stock angle unmoved keeps the
game's distance exactly.
*/

#include "cseries.h"
#include "render/render_cameras.h"
#include "near_clip.h"

#include <math.h>

/* how far from the eye the near plane may reach at the view's corners
(world units): the cyborg's collision radius, as near as the world comes
to the eye beside it (a wall the player stands against) */
#define NEAR_CLIP_REACH 0.2f

/* the nearest the first-person weapon's near plane comes, a part of the
game's distance: 1/64 of it, half a millimeter (the reversed 32-bit float
depth keeps its precision as a share of the distance, far_clip.c) */
#define FIRST_PERSON_NEAR_CLIP_LEAST (1.0f / 64.0f)

/* how far a corner of the view is from the eye at depth 1, the view's
vertical half angle's tangent this: the farther side's and the farther top
or bottom's tangents, render_camera_build_frustum's planes, its bounds as
across the viewport's height */
static real near_clip_corner(struct render_camera const *camera, real_rectangle2d const *frustum_bounds,
	real tangent)
{
	real viewport_width = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	real viewport_height = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real across = MAX(fabsf(frustum_bounds->x0), fabsf(frustum_bounds->x1)) * viewport_width / viewport_height * tangent;
	real up = MAX(fabsf(frustum_bounds->y0), fabsf(frustum_bounds->y1)) * tangent;

	return (real)sqrt(1.0 + (double)across * across + (double)up * up);
}

real render_near_clip_distance(struct render_camera const *camera, real_rectangle2d const *frustum_bounds)
{
	real tangent = (real)tan(camera->vertical_field_of_view * 0.5f);
	real reached = NEAR_CLIP_REACH / near_clip_corner(camera, frustum_bounds, tangent);

	return reached < camera->z_near ? reached : camera->z_near;
}

real render_first_person_near_clip_distance(struct render_camera const *camera,
	real_rectangle2d const *frustum_bounds, real stock_vertical_field_of_view, real z_near, real forward_offset)
{
	real drawn = (real)tan(camera->vertical_field_of_view * 0.5f);
	real stock = (real)tan(stock_vertical_field_of_view * 0.5f);
	real result = z_near;

	if (!(z_near > 0.0f))
		return z_near;
	/* (a projection wider than the stock angle: its corners as far from the
	eye as the stock angle's, in this viewport; a narrower one keeps the
	game's) */
	if (stock > 0.0f && drawn > stock && drawn < 1.0e6f)
	{
		result = z_near * near_clip_corner(camera, frustum_bounds, stock) /
			near_clip_corner(camera, frustum_bounds, drawn);
	}
	/* (the weapon moved nearer the eye: the plane with it) */
	if (forward_offset < 0.0f)
	{
		real moved = MAX(z_near + forward_offset, z_near * FIRST_PERSON_NEAR_CLIP_LEAST);

		if (moved < result)
			result = moved;
	}
	return result;
}
