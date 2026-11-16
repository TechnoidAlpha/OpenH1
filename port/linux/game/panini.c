/*
PANINI.C

display.panini (its strength, 0 off) and display.panini_vertical, each local
player's own (their profile's: player_settings.c): each player's window's 3D
view drawn again through the Panini projection
(port/linux/include/panini_projection.h has its arithmetic and why the view
is drawn as ever first), once it is whole and before the HUD is drawn over
it, by the renderers' pass (halo_screen_panini: port/linux/src/d3d8_device.c,
then xgpu_vulkan_post.c and xgpu_metal_post.c). The HUD, the menus and
what is drawn over the view are not curved.

What the HUD places over the world takes the same projection: the
teammates' marks and the players' names (hud.c), the waypoints (hud_nav_
points.c), the passenger's reticle (passenger_camera.c) and the debug
text, through render_camera_view_to_screen; the reticle at the middle is
scaled by the middle's magnification, so that it covers the angle it did
(hud_draw.c), and the first-person weapon's projection is widened by as
much, so that it is drawn its own size there (view_fov.c). The middle
itself does not move: the shots go where the reticle is. What is drawn into
the 3D view (the lens flares, the effects, the ambient occlusion from the
depth) is in the view's own projection, and the pass carries it with the
rest.

A window's view is any of its rectilinear views about an axis inside it:
split screen's windows each have their own, a little off their middles (the
safe frame), and the projection is about that axis, its scale from the
farther side. Not the VR build's (the headset's eyes need the true
perspective; its mirror is not the player's view), nor a mirror's view or
any other target than the screen.
*/

#include "cseries.h"
#include "panini.h"
#include "render/render.h"
#include "render/render_cameras.h"

#include "panini_projection.h"
#include "player_settings.h"
#include "render_enumerate.h"

#include <math.h>

/* (the window's rasterizer target, as the rasterizer's sources number it) */
enum
{
	_rasterizer_target_render_primary = 0
};

/* the local player's settings (their profile's: player_settings.c): the
strength (d; 0 off) and the vertical compression (s); TRUE while on */
static boolean panini_settings(short local_player_index, real *d, real *s)
{
	static struct
	{
		boolean initialized;
		unsigned long read_at;
		real strength, vertical;
	} players[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
		players[slot].strength = (real)config_player_real(local_player_index, "display.panini");
		players[slot].vertical = (real)config_player_real(local_player_index, "display.panini_vertical");
	}
	*d = players[slot].strength;
	*s = players[slot].vertical;
#ifdef HALO_VR
	/* (not the VR build's: the headset's eyes need the true perspective,
	and its mirror on the screen is not the player's view) */
	return FALSE;
#else
	return players[slot].strength > 0.0f;
#endif
}

/* the window drawn now, while its view is drawn through the projection: its
viewport (the game's units of the screen), its projection's terms (x and y
scales, x and y offsets: render_cameras.c's), the rectilinear view's
tangents at the viewport's left, right, top and bottom, and the projection */
static struct
{
	/* (its local player's settings) */
	short local_player_index;
	boolean active;
	/* (off, the window a map's preparation draws: its pass journaled
	alone, for its record: panini_window_draw) */
	boolean journaled;
	rectangle2d viewport;
	real projection[4];
	real left, right, top, bottom;
	struct panini_projection panini;
} window;

void panini_window_begin(short local_player_index, short rasterizer_target, struct render_camera const *camera,
	struct render_frustum const *frustum)
{
	real d, s, x_scale, y_scale, x_offset, y_offset;

	window.active = window.journaled = FALSE;
	window.local_player_index = local_player_index;
	if (local_player_index < 0 || rasterizer_target != _rasterizer_target_render_primary || camera->mirrored ||
		!frustum->projection_valid)
	{
		return;
	}
	if (!panini_settings(local_player_index, &d, &s))
	{
		window.viewport = camera->viewport_bounds;
		window.journaled = render_enumerating;
		return;
	}
	x_scale = frustum->projection_matrix[0][0];
	y_scale = frustum->projection_matrix[1][1];
	x_offset = frustum->projection_matrix[2][0];
	y_offset = frustum->projection_matrix[2][1];
	if (!(x_scale > 0.0f && y_scale > 0.0f) ||
		camera->viewport_bounds.x1 <= camera->viewport_bounds.x0 ||
		camera->viewport_bounds.y1 <= camera->viewport_bounds.y0)
	{
		return;
	}
	/* (render_camera_view_to_screen's: a point at tangents x and y is at
	x_scale x - x_offset across the viewport, y_offset - y_scale y down it,
	each -1 to 1) */
	window.left = (x_offset - 1.0f) / x_scale;
	window.right = (x_offset + 1.0f) / x_scale;
	window.top = (y_offset + 1.0f) / y_scale;
	window.bottom = (y_offset - 1.0f) / y_scale;
	/* (an axis inside the view; the scale from its farther sides) */
	if (!(window.left < 0.0f && window.right > 0.0f && window.bottom < 0.0f && window.top > 0.0f) ||
		!panini_projection_fit(&window.panini, d, s, MAX(-window.left, window.right),
			MAX(-window.bottom, window.top)))
	{
		return;
	}
	window.projection[0] = x_scale;
	window.projection[1] = y_scale;
	window.projection[2] = x_offset;
	window.projection[3] = y_offset;
	window.viewport = camera->viewport_bounds;
	window.active = TRUE;
}

boolean panini_projection_of_view(short local_player_index, real tangent_x, real tangent_y,
	struct panini_projection *projection)
{
	real d, s;

	return panini_settings(local_player_index, &d, &s) &&
		panini_projection_fit(projection, d, s, tangent_x, tangent_y);
}

void panini_window_draw(void)
{
	float parameters[8];

	if (!window.active)
	{
		if (window.journaled)
			halo_screen_panini(window.viewport.x0, window.viewport.y0, window.viewport.x1, window.viewport.y1, NULL);
		return;
	}
	parameters[0] = window.panini.d;
	parameters[1] = window.panini.s;
	parameters[2] = window.panini.scale;
	parameters[3] = 0.0f;
	parameters[4] = window.left;
	parameters[5] = window.right;
	parameters[6] = window.top;
	parameters[7] = window.bottom;
	halo_screen_panini(window.viewport.x0, window.viewport.y0, window.viewport.x1, window.viewport.y1, parameters);
}

void panini_window_end(void)
{
	window.active = window.journaled = FALSE;
}

boolean panini_replay_projection(float projection[8])
{
	real d, s;
	struct panini_projection fitted;

	/* (the first player's, whose view a map's preparation draws) */
	if (!panini_settings(0, &d, &s))
		return FALSE;
	/* (a record made with it off: a projection of a 90-degree view, the
	pass's own being the same whatever its terms) */
	if (projection[0] <= 0.0f && panini_projection_fit(&fitted, d, s, 1.0f, 1.0f))
	{
		projection[0] = fitted.d;
		projection[1] = fitted.s;
		projection[2] = fitted.scale;
		projection[3] = 0.0f;
		projection[4] = -1.0f;
		projection[5] = 1.0f;
		projection[6] = 1.0f;
		projection[7] = -1.0f;
	}
	return TRUE;
}

real panini_center_scale(void)
{
	return window.active ? window.panini.scale : 1.0f;
}

real panini_center_scale_widened(real widening)
{
	struct panini_projection widened;
	real d, s;

	if (!window.active)
		return 1.0f;
	if (widening == 1.0f || !(widening > 0.0f) || !panini_settings(window.local_player_index, &d, &s) ||
		!panini_projection_fit(&widened, d, s, MAX(-window.left, window.right) * widening,
			MAX(-window.bottom, window.top) * widening))
	{
		return window.panini.scale;
	}
	return widened.scale;
}

real panini_strength(short local_player_index)
{
	real d, s;

	return panini_settings(local_player_index, &d, &s) ? d : 0.0f;
}

/* a term of a projection as the window's (the game's camera's frustum and
the rasterizer's are built alike) */
static boolean panini_same_term(real term, real window_term)
{
	return fabsf(term - window_term) <= 1.0e-4f * (1.0f + fabsf(window_term));
}

/* the window's projection, the one a point is projected with (not another
camera's, nor the first-person weapon's) */
static boolean panini_window_projects(struct render_camera const *camera, struct render_frustum const *frustum)
{
	return window.active && frustum->projection_valid &&
		camera->viewport_bounds.x0 == window.viewport.x0 && camera->viewport_bounds.x1 == window.viewport.x1 &&
		camera->viewport_bounds.y0 == window.viewport.y0 && camera->viewport_bounds.y1 == window.viewport.y1 &&
		panini_same_term(frustum->projection_matrix[0][0], window.projection[0]) &&
		panini_same_term(frustum->projection_matrix[1][1], window.projection[1]) &&
		panini_same_term(frustum->projection_matrix[2][0], window.projection[2]) &&
		panini_same_term(frustum->projection_matrix[2][1], window.projection[3]);
}

boolean panini_view_to_screen(struct render_camera const *camera, struct render_frustum const *frustum,
	real_point3d const *view_point, real_point2d *screen_point, boolean *visible)
{
	real x, y;

	if (!panini_window_projects(camera, frustum))
		return FALSE;
	*visible = FALSE;
	if (view_point->z < 0.0f)
	{
		panini_projection_forward(&window.panini, view_point->x / -view_point->z, view_point->y / -view_point->z,
			&x, &y);
		if (x >= window.left && x <= window.right && y >= window.bottom && y <= window.top)
		{
			screen_point->x = window.viewport.x0 +
				(x - window.left) / (window.right - window.left) * (real)(window.viewport.x1 - window.viewport.x0);
			screen_point->y = window.viewport.y0 +
				(window.top - y) / (window.top - window.bottom) * (real)(window.viewport.y1 - window.viewport.y0);
			*visible = TRUE;
		}
	}
	return TRUE;
}

void panini_view_direction(real_point3d const *view_point, real *x, real *y)
{
	real across, up;

	if (!window.active || !(view_point->z < 0.0f))
		return;
	panini_projection_forward(&window.panini, view_point->x / -view_point->z, view_point->y / -view_point->z,
		&across, &up);
	*x = across;
	*y = -up;
}
