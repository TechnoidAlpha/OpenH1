/*
PANINI.H

display.panini (panini.c): a player's window's 3D view drawn again through
the Panini projection (port/linux/include/panini_projection.h) before the
HUD, and what maps between the screen and the world through it.
*/

#ifndef PANINI_H
#define PANINI_H

#include "math/real_math.h"

struct render_camera;
struct render_frustum;

/* a window's 3D view (source/render/render.c's render_window): begun with
the projection it is drawn with, before its first draw; its pass, once the
view is whole (after display.hdr's tone map and display.anti_aliasing),
before the HUD is drawn over it; ended once the window is */
void panini_window_begin(short local_player_index, short rasterizer_target, struct render_camera const *camera,
	struct render_frustum const *frustum);
void panini_window_draw(void);
void panini_window_end(void);

/* how much larger the window's view is drawn at its middle (1: not through
the projection), by which the reticle (hud_draw.c) and the first-person
weapon's projection (view_fov.c) are scaled to keep their size there */
real panini_center_scale(void);

/* the projection a view will be drawn through, from its rectilinear half
tangents at its viewport's sides and top (across and up), before its window
is drawn (the lowered reticle's lift: lowered_reticle.c); FALSE when the
view is drawn as it is */
struct panini_projection;
boolean panini_projection_of_view(real tangent_x, real tangent_y, struct panini_projection *projection);

/* where a point of the view (the camera's coordinates, render_cameras.c's
render_camera_view_to_screen) is drawn while the window's view is drawn
through the projection: TRUE when this answered (visible: in the view, and
screen_point its place on the screen), FALSE to project it as ever */
boolean panini_view_to_screen(struct render_camera const *camera, struct render_frustum const *frustum,
	real_point3d const *view_point, real_point2d *screen_point, boolean *visible);
/* the direction from the screen's middle toward a point of the view off the
screen (the waypoints' arrows, hud_nav_points.c), as it is drawn: right and
down, at any length; unchanged unless the view is drawn through it and the
point is ahead */
void panini_view_direction(real_point3d const *view_point, real *x, real *y);

#endif
