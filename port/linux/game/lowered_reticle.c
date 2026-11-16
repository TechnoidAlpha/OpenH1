/*
LOWERED_RETICLE.C

display.lowered_reticle: the reticle below the middle of the view, as Halo
2's and Halo 3's are, display.lowered_reticle_amount hundredths of the
view's height below it. The aim is the game's: the player's facing, the
unit's aim, the shots, aim assist and its magnetism, the flashlight and
what the network sends are as they were. Only the local render view turns:
it looks above the aim by the angle that puts the aim on the reticle
(lowered_reticle_view, from main.c's set_window_camera_values), so the
world under the reticle is what the shots hit. The HUD draws the reticle
where the aim lands on the screen (lowered_reticle_screen_offset, the one
place that says where), and the first-person weapon is posed along the aim
(lowered_reticle_aim_camera), so it is lower and points at the reticle.

The view turns for the player's own aim, on foot and in a seat, first or
third person (the views display.fov widens: view_fov.c's render_fov_unit);
not dead, a scripted camera or a view the director holds (a cinematic
seen from the player's eyes turns it too, as play will: #784), nor in the
VR mode, whose view is the head's. A scope's zoom brings the
reticle back to the middle along the zoom's own transition, as Halo 2 and 3
do, for the scope's picture is drawn about the middle.
*/

#include "cseries.h"
#include "lowered_reticle.h"
#include "view_fov.h"
#include "panini.h"
#include "game/players.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "render/render_cameras_internal.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include "panini_projection.h"
#include "player_settings.h"

#include <math.h>

#ifdef HALO_VR
/* port/linux/src/vr.c */
int halo_vr_running(void);
#endif

/* the farthest below the middle the reticle goes, in hundredths of the
view's height (the menu's slider goes to 25) */
#define LOWERED_RETICLE_MAXIMUM_PERCENT 30.0

/* how far each local player's view looks above its aim, this frame */
static real lift_angles[MAXIMUM_LOCAL_PLAYERS];
/* the cameras the first-person weapons are posed at */
static struct render_camera aim_cameras[MAXIMUM_LOCAL_PLAYERS];

/* ---------- the angles */

/* the local player's display.lowered_reticle's fraction of the view's
height below its middle (their profile's: player_settings.c): 0 off */
static real lowered_reticle_fraction(short local_player_index)
{
	static struct
	{
		boolean initialized;
		unsigned long read_at;
		real fraction;
	} players[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		double percent = config_player_real(local_player_index, "display.lowered_reticle_amount");

		/* (beyond the range, its end; not a number, none) */
		if (percent > LOWERED_RETICLE_MAXIMUM_PERCENT)
			percent = LOWERED_RETICLE_MAXIMUM_PERCENT;
		else if (!(percent > 0.0))
			percent = 0.0;
		players[slot].fraction = config_player_boolean(local_player_index, "display.lowered_reticle") ?
			(real)(percent / 100.0) : 0.0f;
		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
	}
	return players[slot].fraction;
}

/* the angle above the aim a view looks for the aim to land that fraction of
the screen's height (its viewport's) below the middle. The view's vertical
angle spans its window, the safe frame, and the viewport reaches past it
(render_camera_build_frustum_bounds), so in the view's depth the window's
edge is tan(angle / 2) from the middle, the viewport's that times the
viewport's height over the window's. The reticle's point on the screen is
that edge's tangent times twice the fraction; drawn as it is (rectilinear)
the aim's tangent below the middle, tan(lift), is the same. Drawn through
display.panini's projection (panini.c), the direction the screen shows
there is the projection's inverse of it */
static real lowered_reticle_lift_angle(short local_player_index, real fraction, struct render_camera const *camera)
{
	real vertical_field_of_view = camera->vertical_field_of_view;
	real viewport_width = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0);
	real viewport_height = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	real window_height = (real)(camera->window_bounds.y1 - camera->window_bounds.y0);
	real tangent_y, tangent_x, screen_tangent, aim_tangent;
	struct panini_projection projection;

	if (!(fraction > 0.0f) || !(vertical_field_of_view > 0.0f && vertical_field_of_view < _pi) ||
		!(viewport_width > 0.0f && viewport_height > 0.0f && window_height > 0.0f))
	{
		return 0.0f;
	}
	/* (the viewport's half tangents, up and across: its pixels square) */
	tangent_y = tanf(vertical_field_of_view * 0.5f) * viewport_height / window_height;
	tangent_x = tangent_y * viewport_width / viewport_height;
	screen_tangent = 2.0f * fraction * tangent_y;
	aim_tangent = screen_tangent;
	if (panini_projection_of_view(local_player_index, tangent_x, tangent_y, &projection))
	{
		real aim_across;

		panini_projection_inverse(&projection, 0.0f, screen_tangent, &aim_across, &aim_tangent);
	}
	return atanf(aim_tangent);
}

/* how much of the lowering a zoom keeps, from the view's tangent over the
unit's unzoomed one (1 unzoomed) and the first zoom level's (the scope's,
or a 2x zoom's for a view narrowed without one): all of it unzoomed, none
from the first zoom level in, and between them along the zoom's transition */
static real lowered_reticle_zoom_weight(real tangent_ratio, real zoomed_ratio)
{
	if (!(zoomed_ratio > 0.0f && zoomed_ratio < 1.0f))
		zoomed_ratio = 0.5f;
	return PIN((tangent_ratio - zoomed_ratio) / (1.0f - zoomed_ratio), 0.0f, 1.0f);
}

/* forward and up turned up about the view's right by an angle (down for a
negative one): the frame stays square */
static void lowered_reticle_turn(real_vector3d *forward, real_vector3d *up, real angle)
{
	real cosine_angle = cosf(angle), sine_angle = sinf(angle);
	real_vector3d turned_forward, turned_up;

	turned_forward.i = forward->i * cosine_angle + up->i * sine_angle;
	turned_forward.j = forward->j * cosine_angle + up->j * sine_angle;
	turned_forward.k = forward->k * cosine_angle + up->k * sine_angle;
	turned_up.i = up->i * cosine_angle - forward->i * sine_angle;
	turned_up.j = up->j * cosine_angle - forward->j * sine_angle;
	turned_up.k = up->k * cosine_angle - forward->k * sine_angle;
	*forward = turned_forward;
	*up = turned_up;
}

/* ---------- the view */

/* the lowering's weight at the unit's zoom: the game's field of view (the
observer's, before display.fov) against the unit's unzoomed one and its
weapon's first zoom level's (as view_fov.c's render_fov_adjust finds them) */
static real lowered_reticle_zoom(struct unit_datum const *unit, long unit_index, real game_field_of_view)
{
	real unzoomed_angle = unit_definition_get(unit->definition_index)->unit.camera_field_of_view;
	real zoomed_ratio = 0.0f;
	long weapon_index;

	if (!(unzoomed_angle >= 0.001f && unzoomed_angle <= _pi / 2.0f) ||
		!(game_field_of_view > 0.0f && game_field_of_view < _pi))
	{
		return 1.0f;
	}
	weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);
	if (weapon_index != NONE)
	{
		real scoped_angle = weapon_get_field_of_view(weapon_index, unzoomed_angle, 0);

		if (scoped_angle > 0.0f && scoped_angle < unzoomed_angle)
			zoomed_ratio = tanf(scoped_angle * 0.5f) / tanf(unzoomed_angle * 0.5f);
	}
	return lowered_reticle_zoom_weight(tanf(game_field_of_view * 0.5f) / tanf(unzoomed_angle * 0.5f),
		zoomed_ratio);
}

void lowered_reticle_view(short local_player_index, real game_field_of_view, struct render_camera *camera)
{
	struct unit_datum *unit;
	long unit_index;
	real fraction, angle = 0.0f;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return;
	fraction = lowered_reticle_fraction(local_player_index);
#ifdef HALO_VR
	if (halo_vr_running())
		fraction = 0.0f;
#endif
	if (fraction > 0.0f && (unit = render_fov_unit(local_player_index, &unit_index)) != NULL)
	{
		angle = lowered_reticle_lift_angle(local_player_index,
			fraction * lowered_reticle_zoom(unit, unit_index, game_field_of_view), camera);
		if (angle != 0.0f)
			lowered_reticle_turn(&camera->forward, &camera->up, angle);
	}
	lift_angles[local_player_index] = angle;
}

real lowered_reticle_lift(short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return 0.0f;
	return lift_angles[local_player_index];
}

/* ---------- where the aim lands */

boolean lowered_reticle_screen_offset(short local_player_index, struct render_camera const *camera,
	struct render_frustum const *frustum, real_point2d *offset)
{
	real angle = lowered_reticle_lift(local_player_index);
	real_vector3d aim, up;
	real_point3d point;
	real_point2d screen;

	if (angle == 0.0f || !camera || !frustum || !frustum->projection_valid)
		return FALSE;
	/* (the view's own forward turned back down by its lift: the view as
	drawn, its shake too, so the reticle keeps its place on the screen as
	the stock one keeps the middle) */
	aim = camera->forward;
	up = camera->up;
	lowered_reticle_turn(&aim, &up, -angle);
	point.x = camera->position.x + aim.i;
	point.y = camera->position.y + aim.j;
	point.z = camera->position.z + aim.k;
	if (!render_camera_world_to_screen(camera, frustum, &point, &screen))
		return FALSE;
	offset->x = screen.x - (camera->window_bounds.x0 + camera->window_bounds.x1) * 0.5f;
	offset->y = screen.y - (camera->window_bounds.y0 + camera->window_bounds.y1) * 0.5f;
	return TRUE;
}

void lowered_reticle_offset_point(point2d *point, real reticle_scale)
{
	real_point2d offset;

	if (!lowered_reticle_screen_offset(render.local_player_index, &render.camera, &render.frustum, &offset))
		return;
	if (!(reticle_scale > 0.0f))
		reticle_scale = 1.0f;
	point->x = (short)(point->x + floorf(offset.x / reticle_scale + 0.5f));
	point->y = (short)(point->y + floorf(offset.y / reticle_scale + 0.5f));
}

/* ---------- the first-person weapon */

struct render_camera const *lowered_reticle_aim_camera(short local_player_index, struct render_camera const *camera)
{
	real angle = lowered_reticle_lift(local_player_index);
	struct render_camera *aim_camera;

	if (angle == 0.0f)
		return camera;
	aim_camera = &aim_cameras[local_player_index];
	*aim_camera = *camera;
	lowered_reticle_turn(&aim_camera->forward, &aim_camera->up, -angle);
	return aim_camera;
}
