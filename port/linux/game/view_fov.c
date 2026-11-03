/*
VIEW_FOV.C

The view's field of view (display.fov on foot, display.vehicle_fov in a
vehicle's seat), the first-person weapon's (display.viewmodel_fov) and
whether the weapon is drawn (display.viewmodel_visible) and where
(display.viewmodel_offset_x, _y and _z), the local render view only: the
observer, its transitions and what the network sends keep the game's own
camera. Each angle is horizontal at 16:9 (the vertical
angle is kept, so other shapes of window widen or narrow as the stock view
does), and 0 keeps the stock view. Called from the game's camera
(main.c's set_window_camera_values), its HUD (hud_draw.c) and the passes
that draw the first-person weapon (rasterizer and render sources).
*/

#include "cseries.h"
#include "view_fov.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "game/players.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_internal.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "render/render_cameras_internal.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include "mouse_sensitivity.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* (the window's rasterizer target, as the rasterizer's sources number it) */
enum
{
	_rasterizer_target_render_primary = 0
};

double config_real(const char *name);
int config_boolean(const char *name);
unsigned long config_changes(void);
char const *config_string(char const *name);
unsigned long system_milliseconds(void);

/* ---------- the world's field of view */

/* how long the view takes to go from one field of view to the other as the
player gets into a vehicle's seat or out of it, when display.fov and
display.vehicle_fov differ: about the time the director takes to fly the
camera between the head and the seat's following camera */
#define VEHICLE_FOV_TRANSITION_MILLISECONDS 500

static real reticle_scales[MAXIMUM_LOCAL_PLAYERS];
static real authored_vertical[MAXIMUM_LOCAL_PLAYERS];

/* each local player's way between display.fov and display.vehicle_fov: the
weight of the vehicle's (0 on foot, 1 seated) the view is drawn at, the
weight it set out from, when, and toward which */
static struct
{
	boolean valid;
	boolean in_vehicle;
	real from, weight;
	unsigned long start;
} vehicle_blends[MAXIMUM_LOCAL_PLAYERS];

/* an angle in degrees across at 16:9 as the tangent of half the vertical
angle, or 0 (the stock view) outside 20 to 150 */
static real fov_tangent(double degrees)
{
	return degrees >= 20.0 && degrees <= 150.0 ?
		(real)(tan(degrees * _pi / 360.0) * (9.0 / 16.0)) : 0.0f;
}

/* display.fov's and display.vehicle_fov's tangents (0: the stock view):
the vehicle's "default" is the stock view, "match" display.fov's, and a
number an angle as display.fov's is */
static void fov_requested_tangents(real *on_foot, real *seated)
{
	static boolean initialized;
	static unsigned long read_at;
	static real foot_tangent, vehicle_tangent;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		char const *vehicle = config_string("display.vehicle_fov");
		char *end;
		double degrees = strtod(vehicle, &end);

		initialized = TRUE;
		read_at = changes;
		foot_tangent = fov_tangent(config_real("display.fov"));
		while (end != vehicle && (*end == ' ' || *end == '\t'))
			end++;
		if (!_stricmp(vehicle, "match"))
			vehicle_tangent = foot_tangent;
		else if (end != vehicle && !*end)
			vehicle_tangent = fov_tangent(degrees);
		else
			vehicle_tangent = 0.0f;
	}
	*on_foot = foot_tangent;
	*seated = vehicle_tangent;
}

/* the local player's unit, when its view is one the fields of view widen:
not dead, in a cinematic, a scripted camera or a view the director holds
(and its index); else NULL */
static struct unit_datum *render_fov_unit(short local_player_index, long *unit_index)
{
	struct unit_datum *unit;
	director_perspective perspective;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		cinematic_in_progress() || (director_camera_scripted && *director_camera_scripted))
	{
		return NULL;
	}
	/* (the third person is a vehicle's seat, its entering and exiting, and
	the moment after the director leaves it) */
	perspective = director_get_perspective(local_player_index);
	if ((perspective != _director_perspective_first_person &&
		perspective != _director_perspective_third_person) ||
		director_inhibited_facing(local_player_index))
	{
		return NULL;
	}
	*unit_index = player_control_get_unit_index(local_player_index);
	if (*unit_index == NONE)
		return NULL;
	unit = unit_try_and_get(*unit_index);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return NULL;
	return unit;
}

/* (each frame) the weight of display.vehicle_fov in the view, from 0 on
foot to 1 in a seat. Seated is the unit in its seat: getting in and out
too, while it is attached; once out, the view eases to display.fov while
the director flies the camera back to the head. It eases the same way in
(smoothly, VEHICLE_FOV_TRANSITION_MILLISECONDS), turning back from where
it is if the player gets in or out again on the way. A view the fields of
view leave alone (death, a cinematic) ends the way: the next view is
wherever the player then is */
static real vehicle_fov_weight(short local_player_index, struct unit_datum const *unit)
{
	unsigned long now = system_milliseconds();
	boolean in_vehicle;
	real progress;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return 0.0f;
	if (!unit)
	{
		vehicle_blends[local_player_index].valid = FALSE;
		return 0.0f;
	}
	in_vehicle = unit->object.parent_object_index != NONE;
	if (!vehicle_blends[local_player_index].valid)
	{
		vehicle_blends[local_player_index].valid = TRUE;
		vehicle_blends[local_player_index].in_vehicle = in_vehicle;
		vehicle_blends[local_player_index].weight = in_vehicle ? 1.0f : 0.0f;
		vehicle_blends[local_player_index].from = vehicle_blends[local_player_index].weight;
		vehicle_blends[local_player_index].start = now;
		return vehicle_blends[local_player_index].weight;
	}
	if (in_vehicle != vehicle_blends[local_player_index].in_vehicle)
	{
		vehicle_blends[local_player_index].in_vehicle = in_vehicle;
		vehicle_blends[local_player_index].from = vehicle_blends[local_player_index].weight;
		vehicle_blends[local_player_index].start = now;
	}
	progress = (real)(now - vehicle_blends[local_player_index].start) / VEHICLE_FOV_TRANSITION_MILLISECONDS;
	if (!(progress < 1.0f))
	{
		vehicle_blends[local_player_index].weight = in_vehicle ? 1.0f : 0.0f;
	}
	else
	{
		real eased = progress * progress * (3.0f - 2.0f * progress);
		real from = vehicle_blends[local_player_index].from;

		vehicle_blends[local_player_index].weight = from + ((in_vehicle ? 1.0f : 0.0f) - from) * eased;
	}
	return vehicle_blends[local_player_index].weight;
}

/* the view's vertical angle for display.fov on foot and display.vehicle_fov
seated, the weight of the vehicle's between them: the game's own dead, in a
cinematic, a scripted camera or a view the director holds, and at a scope's
full zoom; between, the zoom's own transition. A vehicle's seat, first or
third person, and its entering and exiting take the vehicle's, so the view
and its reticle keep one size in the seat ("default": the stock view, its
reticle at the stock size) */
static real render_fov_adjust(short local_player_index, real native_vertical_field_of_view, real vehicle_weight)
{
	long unit_index, weapon_index;
	struct unit_datum *unit;
	real foot_tangent, vehicle_tangent, requested_tangent;
	real unzoomed_angle, unzoomed_tangent, native_tangent, adjusted_tangent;

	fov_requested_tangents(&foot_tangent, &vehicle_tangent);
	if (!foot_tangent && !vehicle_tangent)
		return native_vertical_field_of_view;
	unit = render_fov_unit(local_player_index, &unit_index);
	if (!unit)
		return native_vertical_field_of_view;

	unzoomed_angle = unit_definition_get(unit->definition_index)->unit.camera_field_of_view;
	if (!(unzoomed_angle >= 0.001f && unzoomed_angle <= _pi / 2.0f) ||
		!(native_vertical_field_of_view > 0.0f && native_vertical_field_of_view < _pi))
	{
		return native_vertical_field_of_view;
	}
	unzoomed_tangent = 0.75f * render_camera_get_adjusted_field_of_view_tangent(unzoomed_angle);
	/* (the stock view's is the unit's own, unzoomed) */
	if (!foot_tangent)
		foot_tangent = unzoomed_tangent;
	if (!vehicle_tangent)
		vehicle_tangent = unzoomed_tangent;
	if (!(vehicle_weight > 0.0f))
		requested_tangent = foot_tangent;
	else if (!(vehicle_weight < 1.0f))
		requested_tangent = vehicle_tangent;
	else
		requested_tangent = foot_tangent + (vehicle_tangent - foot_tangent) * vehicle_weight;
	if (requested_tangent == unzoomed_tangent)
		return native_vertical_field_of_view;

	native_tangent = tanf(native_vertical_field_of_view * 0.5f);
	if (requested_tangent < unzoomed_tangent)
	{
		/* (a view narrower than the stock one: the zoom narrows it further) */
		adjusted_tangent = native_tangent * (requested_tangent / unzoomed_tangent);
	}
	else
	{
		real blend = 1.0f;

		weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);
		if (weapon_index != NONE)
		{
			real scoped_angle = weapon_get_field_of_view(weapon_index, unzoomed_angle, 0);

			if (scoped_angle > 0.0f && scoped_angle < unzoomed_angle)
			{
				real scoped_tangent = 0.75f * render_camera_get_adjusted_field_of_view_tangent(scoped_angle);
				/* (a scope's full zoom keeps its stock angle exactly: the
				observer's arctangent and tangent can land just above it) */
				if (native_tangent <= scoped_tangent * 1.000001f)
					return native_vertical_field_of_view;

				/* (the extra width fades out along the scope's own
				transition, so every zoom level reached is the stock view) */
				blend = PIN((native_tangent - scoped_tangent) /
					(unzoomed_tangent - scoped_tangent), 0.0f, 1.0f);
			}
		}
		if (blend == 0.0f)
			return native_vertical_field_of_view;
		adjusted_tangent = native_tangent + (requested_tangent - unzoomed_tangent) * blend;
	}
	return 2.0f * atanf(adjusted_tangent);
}

real render_fov_vertical(short local_player_index, real native_vertical_field_of_view)
{
	/* (the way between the fields of view moves on once a frame, here; the
	view and its reticle's scale below are of the same weight, so the
	reticle is never a frame apart from the view it is drawn over) */
	long unit_index;
	real weight = vehicle_fov_weight(local_player_index, render_fov_unit(local_player_index, &unit_index));
	real adjusted = render_fov_adjust(local_player_index, native_vertical_field_of_view, weight);

	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS)
	{
		real scale = 1.0f;

		if (adjusted != native_vertical_field_of_view &&
			native_vertical_field_of_view > 0.0f && native_vertical_field_of_view < _pi &&
			adjusted > 0.0f && adjusted < _pi)
		{
			scale = tanf(native_vertical_field_of_view * 0.5f) / tanf(adjusted * 0.5f);
			if (!(scale > 0.0f && scale < 1000.0f))
				scale = 1.0f;
		}
		/* (the view's projection, the zoom's transition too, for the HUD:
		the weapon's own projection, viewmodel_projection_begin, is not it) */
		reticle_scales[local_player_index] = scale;
		/* (a widened view's own angle, which the weapon keeps by default) */
		authored_vertical[local_player_index] =
			adjusted != native_vertical_field_of_view ? native_vertical_field_of_view : 0.0f;
	}
	return adjusted;
}

real render_fov_authored_vertical(short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return 0.0f;
	return authored_vertical[local_player_index];
}

real render_fov_reticle_scale(short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		reticle_scales[local_player_index] == 0.0f)
	{
		return 1.0f;
	}
	return reticle_scales[local_player_index];
}

/* ---------- the mouse's turn zoomed (input.zoom_sensitivity) */

/* a vertical angle as the horizontal one across 4:3, as CS2 gives its
fields of view (and the game its units' cameras) */
static real fov_horizontal_4_3(real vertical)
{
	return 2.0f * atanf(tanf(vertical * 0.5f) / 0.75f);
}

real mouse_zoom_look_scale(short local_player_index, long unit_index, short zoom_level)
{
	static int mode = _mouse_zoom_cs2;
	static unsigned long read_at = (unsigned long)-1;
	struct unit_datum *unit;
	long weapon_index;
	real magnification, unzoomed_angle, zoomed_angle, unzoomed_drawn, zoomed_drawn, weight;

	if (unit_index == NONE || zoom_level == NONE)
		return 1.0f;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		mode = strcmp(config_string("input.zoom_sensitivity"), "halo") ? _mouse_zoom_cs2 : _mouse_zoom_halo;
	}
	magnification = unit_get_zoom_magnification(unit_index, zoom_level);
	unit = unit_try_and_get(unit_index);
	weapon_index = unit ? unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index) : NONE;
	if (mode != _mouse_zoom_cs2 || weapon_index == NONE)
		return mouse_zoom_scale(_mouse_zoom_halo, magnification, 0.0f, 0.0f);
	/* the angles the view is drawn at, unzoomed and at this zoom level (the
	game's, then display.fov's: render_fov_adjust), across 4:3 */
	unzoomed_angle = unit_definition_get(unit->definition_index)->unit.camera_field_of_view;
	if (!(unzoomed_angle >= 0.001f && unzoomed_angle <= _pi / 2.0f))
		return mouse_zoom_scale(_mouse_zoom_halo, magnification, 0.0f, 0.0f);
	zoomed_angle = weapon_get_field_of_view(weapon_index, unzoomed_angle, zoom_level);
	/* (the last frame's way between display.fov and display.vehicle_fov) */
	weight = local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&
		vehicle_blends[local_player_index].valid ? vehicle_blends[local_player_index].weight : 0.0f;
	unzoomed_drawn = render_fov_adjust(local_player_index,
		2.0f * atanf(0.75f * render_camera_get_adjusted_field_of_view_tangent(unzoomed_angle)), weight);
	zoomed_drawn = render_fov_adjust(local_player_index,
		2.0f * atanf(0.75f * render_camera_get_adjusted_field_of_view_tangent(zoomed_angle)), weight);
	return mouse_zoom_scale(_mouse_zoom_cs2, magnification, fov_horizontal_4_3(zoomed_drawn),
		fov_horizontal_4_3(unzoomed_drawn));
}

/* ---------- the first-person weapon's */

/* how deep the weapon's passes nest, and whether the outermost changed the
view (the cameras and frustums it saved) */
static unsigned long projection_depth;
static boolean projection_applied;
static struct render_camera saved_render_camera, saved_rasterizer_camera;
static struct render_frustum saved_render_frustum, saved_rasterizer_frustum;

static real viewmodel_vertical(void)
{
	static boolean initialized;
	static unsigned long read_at;
	static real angle;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		double degrees = config_real("display.viewmodel_fov");

		angle = degrees >= 20.0 && degrees <= 150.0 ?
			(real)(2.0 * atan(tan(degrees * _pi / 360.0) * (9.0 / 16.0))) : 0.0f;
		initialized = TRUE;
		read_at = changes;
	}
	return angle;
}

void viewmodel_projection_begin(void)
{
	real angle;
	real_rectangle2d render_bounds, rasterizer_bounds;

	if (projection_depth++ != 0)
		return;
	projection_applied = FALSE;
	/* (display.viewmodel_fov's angle; by default the view's own, from before
	display.fov widened it: arms and a gun right against the camera stretch
	at a wide angle) */
	angle = viewmodel_vertical();
	if (!angle)
		angle = render_fov_authored_vertical(render.local_player_index);
	if (!angle || render.local_player_index < 0 || render.local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		global_window_parameters.rasterizer_target != _rasterizer_target_render_primary ||
		cinematic_in_progress() || (director_camera_scripted && *director_camera_scripted) ||
		director_get_perspective(render.local_player_index) != _director_perspective_first_person ||
		director_inhibited_facing(render.local_player_index))
	{
		return;
	}

	saved_render_camera = render.camera;
	saved_render_frustum = render.frustum;
	saved_rasterizer_camera = global_window_parameters.camera;
	saved_rasterizer_frustum = global_window_parameters.frustum;
	/* (the frustums' own bounds, the view's crop of a split screen: the
	visibility and the projection change together, the weapon's pose and
	markers do not; get_projection_bounds' slopes are not bounds) */
	render_bounds = saved_render_frustum.frustum_bounds;
	rasterizer_bounds = saved_rasterizer_frustum.frustum_bounds;
	render.camera.vertical_field_of_view = angle;
	global_window_parameters.camera.vertical_field_of_view = angle;
	render_camera_build_frustum(&render.camera, &render_bounds, &render.frustum, TRUE);
	render_camera_build_frustum(&global_window_parameters.camera, &rasterizer_bounds,
		&global_window_parameters.frustum, TRUE);
	projection_applied = TRUE;
	rasterizer_set_frustum_z(0.0f, 0.0f);
}

void viewmodel_projection_end(void)
{
	if (!projection_depth || --projection_depth || !projection_applied)
		return;
	render.camera = saved_render_camera;
	render.frustum = saved_render_frustum;
	global_window_parameters.camera = saved_rasterizer_camera;
	global_window_parameters.frustum = saved_rasterizer_frustum;
	projection_applied = FALSE;
	rasterizer_set_frustum_z(0.0f, 0.0f);
}

/* ---------- where the first-person weapon is drawn */

/* a world unit is ten feet */
#define CENTIMETERS_PER_WORLD_UNIT 304.8
/* the farthest display.viewmodel_offset_* move the weapon along each axis,
in centimeters (the menu's sliders go half as far) */
#define VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS 20.0

static real viewmodel_offset_axis(char const *name)
{
	double centimeters = config_real(name);

	/* (beyond the range, its end; not a number, none) */
	if (centimeters > VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS)
		centimeters = VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS;
	else if (centimeters < -VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS)
		centimeters = -VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS;
	else if (!(centimeters >= -VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS))
		centimeters = 0.0;
	return (real)(centimeters / CENTIMETERS_PER_WORLD_UNIT);
}

/* every weapon's offset (display.viewmodel_offset_x, _y and _z): along the
view's right, up and forward, in world units */
static boolean viewmodel_offset_global(real_vector3d *offset)
{
	static boolean initialized;
	static unsigned long read_at;
	static real_vector3d global;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		global.i = viewmodel_offset_axis("display.viewmodel_offset_x");
		global.j = viewmodel_offset_axis("display.viewmodel_offset_y");
		global.k = viewmodel_offset_axis("display.viewmodel_offset_z");
		initialized = TRUE;
		read_at = changes;
	}
	*offset = global;
	return global.i != 0.0f || global.j != 0.0f || global.k != 0.0f;
}

boolean viewmodel_offset(long weapon_index, real_vector3d *offset)
{
	/* (one offset for every weapon: a weapon's own would be found here) */
	(void)weapon_index;
	return viewmodel_offset_global(offset);
}

void viewmodel_offset_apply(long weapon_index, struct real_matrix4x3 *node_matrices, short node_count,
	struct render_camera const *camera)
{
	real_vector3d offset, right, move;
	short node_index;

	if (!node_matrices || node_count <= 0 || !viewmodel_offset(weapon_index, &offset))
		return;
	/* (the view's right: forward across up, as the world has z up and y
	left) */
	cross_product3d(&camera->forward, &camera->up, &right);
	move.i = right.i * offset.i + camera->up.i * offset.j + camera->forward.i * offset.k;
	move.j = right.j * offset.i + camera->up.j * offset.j + camera->forward.j * offset.k;
	move.k = right.k * offset.i + camera->up.k * offset.j + camera->forward.k * offset.k;
	for (node_index = 0; node_index < node_count; node_index++)
	{
		node_matrices[node_index].position.x += move.i;
		node_matrices[node_index].position.y += move.j;
		node_matrices[node_index].position.z += move.k;
	}
}

/* ---------- whether the first-person weapon is drawn */

boolean viewmodel_is_visible(void)
{
	static boolean initialized, visible;
	static unsigned long read_at;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		visible = config_boolean("display.viewmodel_visible") != 0;
		initialized = TRUE;
		read_at = changes;
	}
	return visible;
}

/* ---------- whether the energy shield's flare is drawn on the arms */

boolean viewmodel_shield_is_visible(void)
{
	static boolean initialized, visible;
	static unsigned long read_at;
	unsigned long changes = config_changes();

	if (!initialized || read_at != changes)
	{
		visible = config_boolean("display.viewmodel_shield") != 0;
		initialized = TRUE;
		read_at = changes;
	}
	return visible;
}

boolean viewmodel_draws_geometry(boolean first_person)
{
	return !first_person || viewmodel_is_visible();
}
