/*
VIEW_FOV.C

The view's field of view (display.fov on foot and in a seat seen in first
person, a passenger's; display.vehicle_fov in a seat seen in third person,
a driver's or gunner's), the first-person weapon's (display.viewmodel_fov) and
whether the weapon is drawn (display.viewmodel_visible) and where
(display.viewmodel_offset_x, _y and _z, display.viewmodel_pitch, _yaw and
_roll, and each weapon's own: display.viewmodel_weapon), the local render
view only: the
observer, its transitions and what the network sends keep the game's own
camera. Each angle is horizontal at 16:9 (the vertical
angle is kept, so other shapes of window widen or narrow as the stock view
does), and 0 keeps the stock view. Called from the game's camera
(main.c's set_window_camera_values), its HUD (hud_draw.c) and the passes
that draw the first-person weapon (rasterizer and render sources).
*/

#include "cseries.h"
#include "view_fov.h"
#include "panini.h"
#include "camera/director.h"
#include "game/players.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "tag_files/tag_files.h"
#include "tag_files/tag_groups.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_internal.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "render/render_cameras_internal.h"
#include "units/unit_definitions.h"
#include "units/units.h"

#include "mouse_sensitivity.h"
#include "near_clip.h"
#include "panini_projection.h"
#include "passenger_camera.h"
#include "zoom_fov.h"
#include "network_standard.h"
#include "player_settings.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* (the window's rasterizer target, as the rasterizer's sources number it) */
enum
{
	_rasterizer_target_render_primary = 0
};

unsigned long system_milliseconds(void);
void platform_log(char const *format, ...);
/* each weapon's own offsets, as a local player sees them (port/linux/src/port_config.h) */
#define CONFIG_WEAPON_VALUES 6
int config_player_viewmodel_weapon(int local_player, const char *path, double values[CONFIG_WEAPON_VALUES]);

/* ---------- the world's field of view */

/* how long the view takes to go from one field of view to the other as the
player gets into a third-person seat or out of it, or changes between one
and a first-person seat, when display.fov and display.vehicle_fov differ:
about the time the director takes to fly the camera between the head and
the seat's following camera */
#define VEHICLE_FOV_TRANSITION_MILLISECONDS 500

static real reticle_scales[MAXIMUM_LOCAL_PLAYERS];
static real reticle_sizes[MAXIMUM_LOCAL_PLAYERS];
/* how much wider the unzoomed view is than the one drawn (1 unzoomed) */
static real reticle_widenings[MAXIMUM_LOCAL_PLAYERS];
static real authored_vertical[MAXIMUM_LOCAL_PLAYERS];
/* whether the view is drawn with display.scale_scoped_fov on (its scope's
pieces keep to the scope's picture: render_fov_scope_piece_scale) */
static boolean scaled_scopes[MAXIMUM_LOCAL_PLAYERS];

/* each local player's way between display.fov and display.vehicle_fov: the
weight of the vehicle's (0 on foot or in a first-person seat, 1 in a
third-person one) the view is drawn at, the weight it set out from, when,
and toward which */
static struct
{
	boolean valid;
	boolean third_person;
	real from, weight;
	unsigned long start;
} vehicle_blends[MAXIMUM_LOCAL_PLAYERS];

/* the widest field of view a Pro 2v2 game draws (network_standard_pro_game),
in display.fov's degrees across at 16:9: its vertical angle, the one every
shape of window keeps, so a 4:3 view is about 94 degrees across, 16:10's 104
and 21:9's 124, as at display.fov 110 outside one. The setting stays as it is */
#define PRO_FIELD_OF_VIEW 110.0

/* an angle in degrees across at 16:9 as the tangent of half the vertical
angle, or 0 (the stock view) outside 20 to 160; above 150 only while the
local player's display.panini is on, 150 otherwise (panini_projection.h's
panini_field_of_view: why 160) */
static real fov_tangent(short local_player_index, double degrees)
{
	degrees = panini_field_of_view(degrees, panini_strength(local_player_index));
	return degrees ? (real)(tan(degrees * _pi / 360.0) * (9.0 / 16.0)) : 0.0f;
}

/* a field of view's tangent at most PRO_FIELD_OF_VIEW's */
static real fov_pro_capped(real tangent)
{
	real cap = (real)(tan(PRO_FIELD_OF_VIEW * _pi / 360.0) * (9.0 / 16.0));

	return tangent > cap ? cap : tangent;
}

/* the local player's display.fov's and display.vehicle_fov's tangents (0:
the stock view): the vehicle's "default" is the stock view, "match"
display.fov's, and a number an angle as display.fov's is; and
display.scale_scoped_fov; each local player's own (their profile's:
player_settings.c), read again as the settings change */
static void fov_requested_tangents(short local_player_index, real *on_foot, real *seated, boolean *scale_scoped)
{
	static struct
	{
		boolean initialized, scaled;
		unsigned long read_at;
		real foot_tangent, vehicle_tangent;
		double foot_degrees;
	} players[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		char const *vehicle = config_player_string(local_player_index, "display.vehicle_fov");
		char *end;
		double degrees = strtod(vehicle, &end);
		double foot = config_player_real(local_player_index, "display.fov");
		boolean first = !players[slot].initialized;

		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
		/* (a local player's own, in the log as it changes) */
		if (slot < PLAYER_SETTINGS_SLOTS - 1 && (first ? foot != 0.0 : foot != players[slot].foot_degrees))
			platform_log("view: player %d's field of view %g, in a vehicle %s", slot + 1, foot, vehicle);
		players[slot].foot_degrees = foot;
		players[slot].foot_tangent = fov_tangent(local_player_index, foot);
		players[slot].scaled = config_player_boolean(local_player_index, "display.scale_scoped_fov") ? TRUE : FALSE;
		while (end != vehicle && (*end == ' ' || *end == '\t'))
			end++;
		if (!_stricmp(vehicle, "match"))
			players[slot].vehicle_tangent = players[slot].foot_tangent;
		else if (end != vehicle && !*end)
			players[slot].vehicle_tangent = fov_tangent(local_player_index, degrees);
		else
			players[slot].vehicle_tangent = 0.0f;
	}
	*on_foot = players[slot].foot_tangent;
	*seated = players[slot].vehicle_tangent;
	*scale_scoped = players[slot].scaled;
}

/* the local player's unit, when its view is one the fields of view widen:
not dead, a scripted camera or a view the director holds (and its index);
else NULL (the lowered reticle's too: lowered_reticle.c). A cinematic seen
from the player's own eyes (a50's opening, before the player has control)
is widened as the play after it is, so that a setting changed there shows
at once and the view does not jump when play begins (#784); a cinematic's
own camera is a scripted one */
struct unit_datum *render_fov_unit(short local_player_index, long *unit_index)
{
	struct unit_datum *unit;
	director_perspective perspective;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		(director_camera_scripted && *director_camera_scripted))
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

/* whether the local player's seat is one seen in third person: a seat with
the following camera (a driver's, a gunner's), getting in and out of it
too, or a passenger's with display.passenger_third_person; not on foot, nor
in a seat seen from the head (a passenger's, the Shade's turret), whose
view is display.fov's as on foot (#775), the third-person shot of getting
into it too */
static boolean vehicle_fov_third_person(short local_player_index, struct unit_datum const *unit)
{
	struct unit_datum *vehicle;
	struct unit_definition *definition;
	short seat_index = unit->unit.parent_seat_index;

	if (unit->object.parent_object_index == NONE)
		return FALSE;
	if (passenger_camera_enabled(local_player_index))
		return TRUE;
	vehicle = unit_try_and_get(unit->object.parent_object_index);
	if (!vehicle)
		return FALSE;
	definition = unit_definition_get(vehicle->definition_index);
	if (seat_index < 0 || seat_index >= definition->unit.seats.count)
		return FALSE;
	return TEST_FLAG(TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, seat_index, struct unit_seat)->flags,
		_unit_seat_third_person_camera_bit) ? TRUE : FALSE;
}

/* (each frame) the weight of display.vehicle_fov in the view, from 0 on
foot or in a first-person seat to 1 in a third-person seat. A third-person
seat is the unit in it: getting in and out too, while it is attached; once
out, the view eases to display.fov while the director flies the camera back
to the head. It eases the same way in and from one seat to another
(smoothly, VEHICLE_FOV_TRANSITION_MILLISECONDS), turning back from where it
is if the player changes again on the way. A view the fields of view leave
alone (death, a scripted camera) ends the way: the next view is wherever the
player then is */
static real vehicle_fov_weight(short local_player_index, struct unit_datum const *unit)
{
	unsigned long now = system_milliseconds();
	boolean third_person;
	real progress;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return 0.0f;
	if (!unit)
	{
		vehicle_blends[local_player_index].valid = FALSE;
		return 0.0f;
	}
	third_person = vehicle_fov_third_person(local_player_index, unit);
	if (!vehicle_blends[local_player_index].valid)
	{
		vehicle_blends[local_player_index].valid = TRUE;
		vehicle_blends[local_player_index].third_person = third_person;
		vehicle_blends[local_player_index].weight = third_person ? 1.0f : 0.0f;
		vehicle_blends[local_player_index].from = vehicle_blends[local_player_index].weight;
		vehicle_blends[local_player_index].start = now;
		return vehicle_blends[local_player_index].weight;
	}
	if (third_person != vehicle_blends[local_player_index].third_person)
	{
		vehicle_blends[local_player_index].third_person = third_person;
		vehicle_blends[local_player_index].from = vehicle_blends[local_player_index].weight;
		vehicle_blends[local_player_index].start = now;
	}
	progress = (real)(now - vehicle_blends[local_player_index].start) / VEHICLE_FOV_TRANSITION_MILLISECONDS;
	if (!(progress < 1.0f))
	{
		vehicle_blends[local_player_index].weight = third_person ? 1.0f : 0.0f;
	}
	else
	{
		real eased = progress * progress * (3.0f - 2.0f * progress);
		real from = vehicle_blends[local_player_index].from;

		vehicle_blends[local_player_index].weight = from + ((third_person ? 1.0f : 0.0f) - from) * eased;
	}
	return vehicle_blends[local_player_index].weight;
}

/* the view's vertical angle for display.fov on foot and display.vehicle_fov
in a third-person seat, the weight of the vehicle's between them: the game's
own dead, a scripted camera or a view the director holds,
and at a scope's full zoom (display.scale_scoped_fov off, the Xbox's:
zoom_fov.h); between, the zoom's own transition. With it on, the zoom's
views are the player's zoomed by the same magnification. A third-person
seat and its entering and exiting take the vehicle's, so the view and its
reticle keep one size in the seat ("default": the stock view, its reticle
at the stock size). The reticle's size, the unzoomed view's scale
of it, is set too, and that view's tangent: 1 and 0 where the view is the
game's own */
static real render_fov_adjust(short local_player_index, real native_vertical_field_of_view, real vehicle_weight,
	real *reticle_size, real *unzoomed_drawn_tangent)
{
	long unit_index, weapon_index;
	struct unit_datum *unit;
	real foot_tangent, vehicle_tangent, requested_tangent;
	real unzoomed_angle, unzoomed_tangent, native_tangent, scoped_tangent, adjusted_tangent;
	boolean scale_scoped;

	*reticle_size = 1.0f;
	*unzoomed_drawn_tangent = 0.0f;
	fov_requested_tangents(local_player_index, &foot_tangent, &vehicle_tangent, &scale_scoped);
	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS)
		scaled_scopes[local_player_index] = scale_scoped && (foot_tangent || vehicle_tangent);
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
	/* (a Pro 2v2 game's at most PRO_FIELD_OF_VIEW, on foot and seated) */
	if (network_standard_pro_game())
		requested_tangent = fov_pro_capped(requested_tangent);
	/* (the unzoomed view's scale of the reticle: a zoom leaves it as it is,
	as the stock game draws its reticle one size at every zoom) */
	if (requested_tangent > 0.0f)
	{
		*reticle_size = PIN(unzoomed_tangent / requested_tangent, 0.001f, 1000.0f);
		*unzoomed_drawn_tangent = requested_tangent;
	}
	if (requested_tangent == unzoomed_tangent)
		return native_vertical_field_of_view;

	/* (the zoom: the Xbox's levels, the player's width faded out along its
	transition, or display.scale_scoped_fov's, the player's view zoomed:
	zoom_fov.h) */
	native_tangent = tanf(native_vertical_field_of_view * 0.5f);
	scoped_tangent = 0.0f;
	weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);
	if (weapon_index != NONE)
	{
		real scoped_angle = weapon_get_field_of_view(weapon_index, unzoomed_angle, 0);

		if (scoped_angle > 0.0f && scoped_angle < unzoomed_angle)
			scoped_tangent = 0.75f * render_camera_get_adjusted_field_of_view_tangent(scoped_angle);
	}
	adjusted_tangent = zoom_fov_tangent(native_tangent, unzoomed_tangent, requested_tangent, scoped_tangent,
		scale_scoped);
	if (adjusted_tangent == native_tangent)
		return native_vertical_field_of_view;
	return 2.0f * atanf(adjusted_tangent);
}

real render_fov_vertical(short local_player_index, real native_vertical_field_of_view)
{
	/* (the way between the fields of view moves on once a frame, here; the
	view and its reticle's scale below are of the same weight, so the
	reticle is never a frame apart from the view it is drawn over) */
	long unit_index;
	struct unit_datum *unit = render_fov_unit(local_player_index, &unit_index);
	real weight = vehicle_fov_weight(local_player_index, unit);
	real reticle_size, unzoomed_drawn_tangent;
	real adjusted = render_fov_adjust(local_player_index, native_vertical_field_of_view, weight, &reticle_size,
		&unzoomed_drawn_tangent);

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
		reticle_sizes[local_player_index] = reticle_size;
		reticle_widenings[local_player_index] = 1.0f;
		if (unzoomed_drawn_tangent > 0.0f && adjusted > 0.0f && adjusted < _pi)
		{
			real widening = unzoomed_drawn_tangent / tanf(adjusted * 0.5f);

			if (widening > 1.0f && widening < 1000.0f)
				reticle_widenings[local_player_index] = widening;
		}
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

/* a scope's own pieces keep to its picture, which the HUD draws at its own
size: at the Xbox's full zoom the view is the stock zoomed one, its scale 1,
but with display.scale_scoped_fov the zoomed view stays as much wider than
the stock one as the player's own, and the pieces drawn at the view's scale
shrank to the middle of the scope's frame, away from its edges and ticks
(#731) */
real render_fov_scope_piece_scale(short local_player_index)
{
	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&
		scaled_scopes[local_player_index])
	{
		return 1.0f;
	}
	return render_fov_reticle_scale(local_player_index);
}

/* whether a zoomed reticle takes its scope's scale: with the Xbox's zoom
(display.scale_scoped_fov off) the zoomed view is the stock one, and the
reticle meets its scope's marks at its stock size, as on the Xbox; with
display.scale_scoped_fov's, which keeps the player's wider view zoomed, it
keeps the size the field of view gives it unzoomed (#738, #594) */
boolean render_fov_zoomed_reticle_with_scope(short local_player_index)
{
	return !(local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&
		scaled_scopes[local_player_index]);
}

boolean render_fov_zoom_easing(short local_player_index)
{
	/* (the zoom's view narrower than the unzoomed one: the transition in
	or out, or the full zoom; a hair over 1 is the unzoomed view's own
	arctangent and tangent) */
	return local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&
		!scaled_scopes[local_player_index] && reticle_widenings[local_player_index] > 1.0001f;
}

real render_fov_reticle_size(short local_player_index)
{
	/* (display.panini's magnification of the middle too, the unzoomed
	view's: a zoom narrows the view the projection fits) */
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!(reticle_sizes[local_player_index] > 0.0f))
	{
		return panini_center_scale();
	}
	return reticle_sizes[local_player_index] *
		panini_center_scale_widened(reticle_widenings[local_player_index] > 0.0f ?
			reticle_widenings[local_player_index] : 1.0f);
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
	real reticle_size, unzoomed_drawn_tangent; /* (not wanted here) */

	if (unit_index == NONE || zoom_level == NONE)
		return 1.0f;
	if (read_at != config_changes())
	{
		read_at = config_changes();
		/* (the mouse's player's, player 1's: their profile's) */
		mode = strcmp(config_player_string(0, "input.zoom_sensitivity"), "halo") ? _mouse_zoom_cs2 : _mouse_zoom_halo;
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
		2.0f * atanf(0.75f * render_camera_get_adjusted_field_of_view_tangent(unzoomed_angle)), weight,
		&reticle_size, &unzoomed_drawn_tangent);
	zoomed_drawn = render_fov_adjust(local_player_index,
		2.0f * atanf(0.75f * render_camera_get_adjusted_field_of_view_tangent(zoomed_angle)), weight,
		&reticle_size, &unzoomed_drawn_tangent);
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
/* the stock angle the applied projection widens (the view's own, before it) */
static real projection_stock_vertical;
/* how far each local player's weapon is moved along the view's forward this
frame (display.viewmodel_offset_z and the weapon's own; world units) */
static real applied_forwards[MAXIMUM_LOCAL_PLAYERS];

/* the local player's display.viewmodel_fov as a vertical angle, 0 for none */
static real viewmodel_vertical(short local_player_index)
{
	static struct
	{
		boolean initialized;
		unsigned long read_at;
		real angle;
	} players[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		double degrees = config_player_real(local_player_index, "display.viewmodel_fov");

		players[slot].angle = degrees >= 20.0 && degrees <= 150.0 ?
			(real)(2.0 * atan(tan(degrees * _pi / 360.0) * (9.0 / 16.0))) : 0.0f;
		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
	}
	return players[slot].angle;
}

/* whether the local player's view draws the weapon with its own projection:
the player's first-person aim (a cinematic seen from it too: #784), not a
scripted camera or a view the director holds */
static boolean viewmodel_projection_view(short local_player_index)
{
	return local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&
		!(director_camera_scripted && *director_camera_scripted) &&
		director_get_perspective(local_player_index) == _director_perspective_first_person &&
		!director_inhibited_facing(local_player_index);
}

/* the vertical angle display.viewmodel_fov draws the local player's weapon
at, and the stock one it replaces: the unit's own, unzoomed, which the
weapon keeps by default (the view's, before display.fov widens it). FALSE
when the weapon is drawn at its stock angle: no display.viewmodel_fov, or a
view viewmodel_projection_begin leaves alone. (display.panini widens the
weapon's projection by the center zoom it draws the middle at, so the
weapon is drawn this angle's size: the angle is the one seen) */
static boolean viewmodel_angles(short local_player_index, real *drawn, real *stock)
{
	long unit_index;
	struct unit_datum *unit;
	real unzoomed;

	*drawn = viewmodel_vertical(local_player_index);
	if (!*drawn || !viewmodel_projection_view(local_player_index))
		return FALSE;
	unit_index = player_control_get_unit_index(local_player_index);
	unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;
	if (!unit)
		return FALSE;
	unzoomed = unit_definition_get(unit->definition_index)->unit.camera_field_of_view;
	if (!(unzoomed >= 0.001f && unzoomed <= _pi / 2.0f))
		return FALSE;
	*stock = 2.0f * atanf(0.75f * render_camera_get_adjusted_field_of_view_tangent(unzoomed));
	return TRUE;
}

void viewmodel_projection_begin(void)
{
	real angle, panini_scale;
	real_rectangle2d render_bounds, rasterizer_bounds;

	if (projection_depth++ != 0)
		return;
	projection_applied = FALSE;
	/* (display.viewmodel_fov's angle; by default the view's own, from before
	display.fov widened it: arms and a gun right against the camera stretch
	at a wide angle) */
	angle = viewmodel_vertical(render.local_player_index);
	if (!angle)
		angle = render_fov_authored_vertical(render.local_player_index);
	/* (display.panini draws the view's middle larger: the weapon's view as
	much wider, so that it is drawn its own size there, panini.c) */
	panini_scale = panini_center_scale();
	if (!angle && panini_scale != 1.0f)
		angle = render.camera.vertical_field_of_view;
	if (panini_scale != 1.0f && angle > 0.0f && angle < _pi)
		angle = 2.0f * atanf(tanf(angle * 0.5f) * panini_scale);
	if (!angle || global_window_parameters.rasterizer_target != _rasterizer_target_render_primary ||
		!viewmodel_projection_view(render.local_player_index))
	{
		return;
	}

	/* (the stock angle: the unit's own, unzoomed or zoomed, as the view
	had it before display.fov widened it) */
	projection_stock_vertical = render_fov_authored_vertical(render.local_player_index);
	if (!projection_stock_vertical)
		projection_stock_vertical = global_window_parameters.camera.vertical_field_of_view;
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

real viewmodel_near_clip_distance(void)
{
	real z_near = rasterizer_globals.first_person_weapon_near_clip_distance;
#ifdef HALO_VR
	/* (the VR mode's eyes keep the game's: vr_render_projection_set knows
	the weapon's passes by it) */
	return z_near;
#else
	short local_player_index = render.local_player_index;
	real forward = 0.0f;

	if (global_window_parameters.rasterizer_target == _rasterizer_target_render_primary &&
		local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS)
	{
		forward = applied_forwards[local_player_index];
	}
	/* (the projection drawn now, the weapon's when it has its own; the
	stock angle the view's own, before the weapon's widened it) */
	return render_first_person_near_clip_distance(&global_window_parameters.camera,
		&global_window_parameters.frustum.frustum_bounds,
		projection_applied ? projection_stock_vertical : global_window_parameters.camera.vertical_field_of_view,
		z_near, forward);
#endif
}

/* ---------- the weapon a first-person offset is for */

/* the weapon's tag (its definition), or NONE */
static long viewmodel_weapon_definition(long weapon_index)
{
	struct object_datum *weapon = weapon_index != NONE ? object_try_and_get(weapon_index) : NULL;

	return weapon ? weapon->definition_index : NONE;
}

/* the tag's path ("weapons\\pistol\\pistol"), by which its own offsets are
kept (config_viewmodel_weapon) */
static char const *viewmodel_weapon_path(long definition_index)
{
	return definition_index != NONE ? tag_get_name(definition_index) : NULL;
}

boolean viewmodel_held_weapon(short local_player_index, char *path, unsigned int size)
{
	long unit_index;
	struct unit_datum *unit;
	short current;
	char const *name;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS || !size)
		return FALSE;
	unit_index = player_control_get_unit_index(local_player_index);
	unit = unit_index != NONE ? unit_try_and_get(unit_index) : NULL;
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return FALSE;
	current = unit->unit.current_weapon_index;
	if (current < 0 || current >= MAXIMUM_WEAPONS_PER_UNIT)
		return FALSE;
	name = viewmodel_weapon_path(viewmodel_weapon_definition(unit->unit.weapon_object_indices[current]));
	if (!name || !name[0] || strlen(name) >= size)
		return FALSE;
	strcpy(path, name);
	return TRUE;
}

/* ---------- where the first-person weapon is drawn */

/* a world unit is ten feet */
#define CENTIMETERS_PER_WORLD_UNIT 304.8
/* the farthest display.viewmodel_offset_* move the weapon along each axis,
in centimeters, and display.viewmodel_pitch, _yaw and _roll turn it, in
degrees: a weapon's own added to the global, the sum held there too */
#define VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS 20.0
#define VIEWMODEL_ROTATION_MAXIMUM_DEGREES 15.0

/* a value held at its range (not a number: none) */
static double viewmodel_offset_held(double value, double maximum)
{
	if (value > maximum)
		return maximum;
	if (value < -maximum)
		return -maximum;
	if (!(value >= -maximum))
		return 0.0;
	return value;
}

static real viewmodel_offset_axis(short local_player_index, char const *name)
{
	return (real)(viewmodel_offset_held(config_player_real(local_player_index, name),
		VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS) / CENTIMETERS_PER_WORLD_UNIT);
}

static real viewmodel_rotation_axis(short local_player_index, char const *name)
{
	return (real)(viewmodel_offset_held(config_player_real(local_player_index, name),
		VIEWMODEL_ROTATION_MAXIMUM_DEGREES) * (_pi / 180.0));
}

static boolean viewmodel_offset_any(struct viewmodel_offset const *offset)
{
	return offset->right != 0.0f || offset->up != 0.0f || offset->forward != 0.0f ||
		offset->pitch != 0.0f || offset->yaw != 0.0f || offset->roll != 0.0f;
}

/* the local player's offset of every weapon (display.viewmodel_offset_x, _y
and _z, along the view's right, up and forward, in world units;
display.viewmodel_pitch, _yaw and _roll, in radians) */
static void viewmodel_offset_global(short local_player_index, struct viewmodel_offset *offset)
{
	static struct
	{
		boolean initialized;
		unsigned long read_at;
		struct viewmodel_offset global;
	} players[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		players[slot].global.right = viewmodel_offset_axis(local_player_index, "display.viewmodel_offset_x");
		players[slot].global.up = viewmodel_offset_axis(local_player_index, "display.viewmodel_offset_y");
		players[slot].global.forward = viewmodel_offset_axis(local_player_index, "display.viewmodel_offset_z");
		players[slot].global.pitch = viewmodel_rotation_axis(local_player_index, "display.viewmodel_pitch");
		players[slot].global.yaw = viewmodel_rotation_axis(local_player_index, "display.viewmodel_yaw");
		players[slot].global.roll = viewmodel_rotation_axis(local_player_index, "display.viewmodel_roll");
		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
	}
	*offset = players[slot].global;
}

boolean viewmodel_offset(short local_player_index, long weapon_index, struct viewmodel_offset *offset)
{
	/* (each local player's last weapon's, until the weapon or a setting
	changes: a lookup by its tag's path, their profile's) */
	static struct
	{
		boolean valid;
		long definition;
		unsigned long at;
		double own[CONFIG_WEAPON_VALUES];
	} cached[PLAYER_SETTINGS_SLOTS];
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	double const *own = cached[slot].own;
	long definition_index = viewmodel_weapon_definition(weapon_index);
	unsigned long changes = config_changes();
	double total[CONFIG_WEAPON_VALUES];
	short value;

	viewmodel_offset_global(local_player_index, offset);
	if (definition_index == NONE)
		return viewmodel_offset_any(offset);
	if (!cached[slot].valid || definition_index != cached[slot].definition || cached[slot].at != changes)
	{
		cached[slot].valid = TRUE;
		config_player_viewmodel_weapon(local_player_index, viewmodel_weapon_path(definition_index), cached[slot].own);
		cached[slot].definition = definition_index;
		cached[slot].at = changes;
	}
	/* (the weapon's own added to the global, the sum held at the range) */
	total[0] = offset->right * CENTIMETERS_PER_WORLD_UNIT + own[0];
	total[1] = offset->up * CENTIMETERS_PER_WORLD_UNIT + own[1];
	total[2] = offset->forward * CENTIMETERS_PER_WORLD_UNIT + own[2];
	total[3] = offset->pitch * (180.0 / _pi) + own[3];
	total[4] = offset->yaw * (180.0 / _pi) + own[4];
	total[5] = offset->roll * (180.0 / _pi) + own[5];
	for (value = 0; value < CONFIG_WEAPON_VALUES; value++)
	{
		total[value] = viewmodel_offset_held(total[value],
			value < 3 ? VIEWMODEL_OFFSET_MAXIMUM_CENTIMETERS : VIEWMODEL_ROTATION_MAXIMUM_DEGREES);
	}
	offset->right = (real)(total[0] / CENTIMETERS_PER_WORLD_UNIT);
	offset->up = (real)(total[1] / CENTIMETERS_PER_WORLD_UNIT);
	offset->forward = (real)(total[2] / CENTIMETERS_PER_WORLD_UNIT);
	offset->pitch = (real)(total[3] * (_pi / 180.0));
	offset->yaw = (real)(total[4] * (_pi / 180.0));
	offset->roll = (real)(total[5] * (_pi / 180.0));
	return viewmodel_offset_any(offset);
}

/* what the weapon's moves right and up are multiplied by at the angle it is
drawn at, against its stock one: the ratio of their tangents. A wider angle
draws the weapon as the stock one draws it, shrunk toward the screen's
middle by that ratio; a move across the view is shrunk with it, so it is
made that much longer, and goes as far across the screen as at the stock
angle. A move away needs nothing: it shrinks the weapon by the same part
at any angle (a point's place on the screen is across over away, times
the angle's own factor, and the move away changes the away alone). A turn
needs nothing either: it is an angle, the same at any field of view */
static real viewmodel_offset_screen_scale(real drawn, real stock)
{
	if (!(drawn > 0.0f && drawn < _pi && stock > 0.0f && stock < _pi))
		return 1.0f;
	return tanf(drawn * 0.5f) / tanf(stock * 0.5f);
}

/* a vector given along the view's right, up and forward (x, y, z) turned
by the offset's pitch (muzzle up: forward toward up, about the right), then
yaw (muzzle right: forward toward the right, about the up) after its roll
(top to the right: up toward the right, about the forward) */
static void viewmodel_rotation_turn(struct viewmodel_offset const *offset, real *x, real *y, real *z)
{
	real cosine, sine, a, b;

	/* roll */
	cosine = cosf(offset->roll);
	sine = sinf(offset->roll);
	a = *x * cosine + *y * sine;
	b = -*x * sine + *y * cosine;
	*x = a;
	*y = b;
	/* pitch */
	cosine = cosf(offset->pitch);
	sine = sinf(offset->pitch);
	a = *y * cosine + *z * sine;
	b = -*y * sine + *z * cosine;
	*y = a;
	*z = b;
	/* yaw */
	cosine = cosf(offset->yaw);
	sine = sinf(offset->yaw);
	a = *x * cosine + *z * sine;
	b = -*x * sine + *z * cosine;
	*x = a;
	*z = b;
}

/* a world vector turned in the view's frame (right, up and forward) */
static void viewmodel_rotation_vector(struct viewmodel_offset const *offset, real_vector3d const *right,
	struct render_camera const *camera, real_vector3d *vector)
{
	real x = dot_product3d(vector, right);
	real y = dot_product3d(vector, &camera->up);
	real z = dot_product3d(vector, &camera->forward);

	viewmodel_rotation_turn(offset, &x, &y, &z);
	vector->i = right->i * x + camera->up.i * y + camera->forward.i * z;
	vector->j = right->j * x + camera->up.j * y + camera->forward.j * z;
	vector->k = right->k * x + camera->up.k * y + camera->forward.k * z;
}

void viewmodel_offset_apply(short local_player_index, long weapon_index, struct real_matrix4x3 *node_matrices,
	short node_count, struct render_camera const *camera)
{
	struct viewmodel_offset offset;
	real_vector3d right, move;
	real drawn, stock;
	short node_index;

	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS)
		applied_forwards[local_player_index] = 0.0f;
	if (!node_matrices || node_count <= 0 || !viewmodel_offset(local_player_index, weapon_index, &offset))
		return;
	/* (its move toward the eye, for the weapon's near plane:
	viewmodel_near_clip_distance) */
	if (local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS)
		applied_forwards[local_player_index] = offset.forward;
	/* (the view's right: forward across up, as the world has z up and y
	left) */
	cross_product3d(&camera->forward, &camera->up, &right);
	/* turned first, the weapon and hands as one, about the middle of their
	nodes as posed (a gun's turns about itself, wherever its animation holds
	it), each node's place and axes */
	if (offset.pitch != 0.0f || offset.yaw != 0.0f || offset.roll != 0.0f)
	{
		real_point3d pivot = { 0.0f, 0.0f, 0.0f };

		for (node_index = 0; node_index < node_count; node_index++)
		{
			pivot.x += node_matrices[node_index].position.x;
			pivot.y += node_matrices[node_index].position.y;
			pivot.z += node_matrices[node_index].position.z;
		}
		pivot.x /= node_count;
		pivot.y /= node_count;
		pivot.z /= node_count;
		for (node_index = 0; node_index < node_count; node_index++)
		{
			struct real_matrix4x3 *node = &node_matrices[node_index];
			real_vector3d away;

			away.i = node->position.x - pivot.x;
			away.j = node->position.y - pivot.y;
			away.k = node->position.z - pivot.z;
			viewmodel_rotation_vector(&offset, &right, camera, &away);
			node->position.x = pivot.x + away.i;
			node->position.y = pivot.y + away.j;
			node->position.z = pivot.z + away.k;
			viewmodel_rotation_vector(&offset, &right, camera, &node->forward);
			viewmodel_rotation_vector(&offset, &right, camera, &node->left);
			viewmodel_rotation_vector(&offset, &right, camera, &node->up);
		}
	}
	/* (display.viewmodel_fov: on the screen as at the weapon's stock angle) */
	if (viewmodel_angles(local_player_index, &drawn, &stock))
	{
		real scale = viewmodel_offset_screen_scale(drawn, stock);

		offset.right *= scale;
		offset.up *= scale;
	}
	move.i = right.i * offset.right + camera->up.i * offset.up + camera->forward.i * offset.forward;
	move.j = right.j * offset.right + camera->up.j * offset.up + camera->forward.j * offset.forward;
	move.k = right.k * offset.right + camera->up.k * offset.up + camera->forward.k * offset.forward;
	for (node_index = 0; node_index < node_count; node_index++)
	{
		node_matrices[node_index].position.x += move.i;
		node_matrices[node_index].position.y += move.j;
		node_matrices[node_index].position.z += move.k;
	}
}

/* ---------- whether the first-person weapon is drawn */

/* (the local player's of the view drawn now: render.local_player_index) */
boolean viewmodel_is_visible(void)
{
	static struct
	{
		boolean initialized, visible;
		unsigned long read_at;
	} players[PLAYER_SETTINGS_SLOTS];
	short local_player_index = render.local_player_index;
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		players[slot].visible = config_player_boolean(local_player_index, "display.viewmodel_visible") != 0;
		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
	}
	return players[slot].visible;
}

/* ---------- whether the energy shield's flare is drawn on the arms */

/* (the local player's of the view drawn now: render.local_player_index) */
boolean viewmodel_shield_is_visible(void)
{
	static struct
	{
		boolean initialized, visible;
		unsigned long read_at;
	} players[PLAYER_SETTINGS_SLOTS];
	short local_player_index = render.local_player_index;
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);
	unsigned long changes = config_changes();

	if (!players[slot].initialized || players[slot].read_at != changes)
	{
		players[slot].visible = config_player_boolean(local_player_index, "display.viewmodel_shield") != 0;
		players[slot].initialized = TRUE;
		players[slot].read_at = changes;
	}
	return players[slot].visible;
}

boolean viewmodel_draws_geometry(boolean first_person)
{
	return !first_person || viewmodel_is_visible();
}
