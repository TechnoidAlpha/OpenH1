/*
ZOOM_FOV.H

A zoomed view's field of view at the player's own (display.fov,
display.vehicle_fov; display.scale_scoped_fov), the arithmetic alone, which
the game's view (port/linux/game/view_fov.c) and the tests share.

Each view is its half tangent up and down (the vertical angle is kept at
every shape of screen, so the ratio of two is the same across: split
screen's and any aspect's). The game zooms its unit's camera angle by the
weapon's magnification (weapon_get_field_of_view: the angle over it), from
the stock view, the Xbox's fixed one, whatever the player's is:

- off (the Xbox's): a scope's zoom levels are the game's own angles. The
  player's extra width (or narrowing) fades out along the zoom's
  transition, so every level reached is the stock zoomed view.
- on: every view, zoomed or not, is the game's times the player's tangent
  over the stock one: tan(zoomed / 2) * tan(player / 2) / tan(stock / 2).
  A zoom keeps its magnification against the player's view, and the
  projection's scale against the game's (the reticle's scale, view_fov.c's
  reticle_scales, the stock tangent over the player's) is one through the
  zoom. The scope's own pieces keep to its picture instead, which the HUD
  draws at its own size (render_fov_scope_piece_scale, #731).

With display.panini the tangents are the rectilinear view's the projection
is fitted to, the player's as display.fov reads it (to 160 degrees).
*/

#ifndef ZOOM_FOV_H
#define ZOOM_FOV_H

/* the tangent the view is drawn at: native the game's (its zoom's
transition too), unzoomed the stock view's, requested the player's,
scoped the weapon's first zoom level's (0: it has none); native where
any is not a view */
static __inline float zoom_fov_tangent(float native, float unzoomed, float requested, float scoped, int scaled)
{
	float blend;

	if (!(native > 0.0f) || !(unzoomed > 0.0f) || !(requested > 0.0f))
		return native;
	if (scaled || !(scoped > 0.0f && scoped < unzoomed))
		return native * (requested / unzoomed);
	/* (a scope's full zoom keeps its stock angle exactly: the observer's
	arctangent and tangent can land just above it) */
	if (native <= scoped * 1.000001f)
		return native;
	blend = (native - scoped) / (unzoomed - scoped);
	blend = blend < 1.0f ? blend : 1.0f;
	return native + (requested - unzoomed) * blend;
}

#endif
