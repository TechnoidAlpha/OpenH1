/*
VR_RENDER.C

The VR mode's view (HALO_VR; port/linux/src/vr.h, port/linux/README.md
"VR"), for render.c and the rasterizer.

The game draws its 3D view once a frame, as on the flat screen, from the
centre of the head: the player's eye (where the game puts it, interpolated
between ticks) moved by the head's position, turned by the head's
orientation, its field of view the union of both eyes' (culling and LOD
cover both). The renderer draws each draw into both eyes at once
(multiview, port/linux/vulkan/xgpu_vulkan.c), moving each vertex's clip
position from the centre view's to its eye's: the eyes look the way the
centre does, offset sideways, so the move is a fixed 4x4 per eye for the
projection the game has set, clip_eye = P_eye V_eye inverse(P_centre
V_centre) clip_centre, made here each time the game sets one
(rasterizer_set_frustum_z: the world's, the first-person weapon's and the
sky's differ in depth range only).

The HUD is drawn afterwards, from a camera looking where the head does with
the HUD layer's field of view, so that the reticle and waypoints fall where
they belong on the layer before the eyes. On the visor (vr.hud = "visor",
port/linux/src/vr_visor.c) the layer is curved about the eyes and laid out
in angles, and the camera looks where the HUD does (a moment behind the
head): what the HUD projects from the world goes through the same curve
(vr_render_hud_to_screen).

The game lowers the player's eye as the player crouches. With the crouch
the head's (vr.physical_crouch, port/linux/src/vr.c, which presses the
game's crouch while the head is low) that drop is taken back out of the
eye: the head has already come down, and the view follows it alone.

A cinematic seen from the player's own head (the game's first-person
camera, not a movie camera) has the hands the controllers' as in play,
though they aim nothing (cinematic_head).
*/

#ifdef HALO_VR

#include "cseries.h"
#include "math/real_math.h"
#include "render/render_cameras.h"
#include "render/render_cameras_internal.h"
#include "cutscene/cinematics.h"
#include "camera/director.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "game/game.h"
#include "tag_files/tag_files.h"
#include "interface/hud.h"
#include "interface/hud_unit.h"
#include "game/game_engine.h"
#include "physics/collision_usage.h"
#include "physics/collisions.h"
#include "render/render.h"
#include "rasterizer/rasterizer.h"
#include "objects/object_definitions.h"
#include "shaders/shader_definitions.h"
#include "bitmaps/bitmap_group_lookup.h"
#include "cache/texture_cache.h"

#include "../src/vr.h"
#include "../src/xgpu_stream.h"
#include "vr_hands.h"
#include "vr_body.h"
#include "structure_culling.h"
#include "cache/cache_files.h"
#include "tag_files/tag_groups.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "scenario/scenario.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the union of the eyes' fields of view, a little wider (the eyes are
apart, and the culling's planes start at the centre) */
#define UNION_MARGIN 1.03f
/* and wider by this angle on each side with the late latch (vr.late_latch),
where the eyes may turn past it before they are drawn (degrees) */
#define LATCH_MARGIN_DEGREES 2.0f
/* the waypoints drawn in the eyes in a frame (vr_render_world_waypoint), at
most */
#define WORLD_WAYPOINTS_MAXIMUM 64
/* the crosshairs in the eyes (vr_render_crosshairs): where the aim meets
the world, no farther than this (world units) nor nearer than this, the
distance's changes smoothed over this time constant (seconds) */
#define CROSSHAIR_FARTHEST 100.0f
#define CROSSHAIR_NEAREST 0.15f
#define CROSSHAIR_SMOOTHING 0.06f
/* the structure's culling (structure_culling.c) culls once from the head's
centre for both eyes: its camera is a sphere about it, as wide as the
farthest eye is from the centre and wider by how far the head may move
before the frame is drawn, with the late latch (metres: 2 m/s, a fast duck
or lean, over the 15 ms or so from where the game locates the head to where
the renderer's thread locates it again), else by the rounding alone. Some
6 cm with the usual 64 mm between the eyes. */
#define CULLING_LATCH_ALLOWANCE 0.03f
#define CULLING_ALLOWANCE 0.005f
/* the top of the neck behind and below the eyes, with the head level
(metres: src/vr.c's NECK_BACK and NECK_DOWN), where the body drawn in a seat
hangs from with the head as it was recentred (vr_render_body_seat) */
#define BODY_NECK_BACK 0.10f
#define BODY_NECK_DOWN 0.10f

static struct
{
	boolean active;
	struct halo_vr_view view;
	/* the centre camera's frustum bounds (render_camera_build_frustum's
	units) */
	real_rectangle2d bounds;
	/* each eye's frustum, of the eye's own camera */
	struct render_frustum eyes[2];
	/* the player's eye as the game has it, and the heading the head's and
	controllers' poses are turned by */
	real_point3d base_position;
	real base_yaw;
	/* the player's unit, seated in a vehicle and viewed from its head (not
	drawn: render_objects.c), else NONE */
	long seated_unit_index;
	/* each eye's frustum moved to the centre (the sky's: vr_render_sky) */
	struct render_frustum eyes_at_centre[2];
	boolean infinite;
	/* an eye's pixels for each of the centre view's pixels, at the middle
	of the view (vr_render_level_of_detail_scale) */
	real level_of_detail_scale;
	/* the projection set last (vr_render_projection_set): its clip
	transform, its depth row, and the eyes' transforms made from it */
	boolean projection_valid;
	real centre[4][4];
	real projection_depth[4];
	float transforms[2][16];
	/* and it is the first-person weapon's (rasterizer_set_frustum_z) */
	boolean first_person;
	/* the camera the first-person weapon was posed from this frame
	(vr_render_weapon_camera), and whether it is in the right controller */
	boolean weapon_camera_set;
	boolean weapon_camera_valid;
	struct render_camera weapon_camera;
	/* and it is the right controller's (the weapon and the hands move with
	it: the late latch) */
	boolean weapon_in_hand;
	/* and whether the weapon and the hands are drawn mirrored about it, a
	left-hander's (vr_render_weapon_mirrored) */
	boolean weapon_mirrored;
	/* the eyes' middle, and each eye, in the world */
	real_point3d head_position;
	real_point3d eye_positions[2];
	/* the scope's pass is drawing (vr_scope.c): from the scope, with no eye
	pass's moves to the eyes (active is FALSE meanwhile) */
	boolean scope_pass;
	/* where the gamepad aims (vr.aim = "gamepad"): the player's facing, and
	up from it */
	real_vector3d aim_forward;
	real_vector3d aim_up;
	/* the crosshairs were drawn in the eyes this frame (not in the HUD's
	layer), and the inverse of their distance, smoothed */
	boolean crosshairs_drawn;
	boolean crosshair_distance_valid;
	real crosshair_inverse_distance;
	/* the marks the HUD puts over the world (vr_render_world_marks) are
	being drawn in the eyes, and were this frame (not in the HUD's layer) */
	boolean marks_drawing;
	boolean marks_drawn;
	/* the points of the waypoints drawn in the eyes this frame (the HUD's
	pass leaves them out) */
	real_point3d waypoints[WORLD_WAYPOINTS_MAXIMUM];
	short waypoint_count;
	/* the physical crouch (crouch_lift): the unit whose crouch is
	followed, the tick it was last read at, its crouch at the last two ticks
	(blended as the camera is), whether the crouch is the head's, and this
	frame's: the game's drop taken back out of the eye (world units) and how
	far the player is crouched (0 to 1); what it last logged (with
	debug.vr_test_hands) */
	boolean crouch_valid;
	long crouch_unit_index;
	long crouch_tick;
	real crouch_previous;
	real crouch_latest;
	boolean crouch_physical;
	real crouch_lift;
	real crouch_blended;
	real crouch_head_drop;
	real crouch_logged_head;
	/* a cinematic seen from the player's head (cinematic_head): this
	frame's, and the view logged last (enum cinematic_view) */
	boolean cinematic_head;
	short cinematic_logged;
	/* the player's eye as the last frame was drawn (base_position), while
	the VR mode aimed: the player's unit then and the game's tick, for the
	aim (vr_render_aim_hit) before the next frame is drawn */
	boolean aim_base_valid;
	long aim_base_unit_index;
	long aim_base_tick;
	/* the sounds' listener (vr_render_listener), from the last frame drawn:
	how far the view's eye was moved from the game's (the crouch, a
	vehicle's seat, the death's held eye; world units), the heading and
	pitch the head was turned by (the heading the camera's own, as it is
	when the listener is placed: a cinematic's), and the game's tick then */
	boolean listener_valid;
	real_vector3d listener_eye_offset;
	boolean listener_camera_yaw;
	real listener_yaw;
	real listener_pitch;
	long listener_tick;
	/* debug.vr_audio_log: the listener placed for this frame, and the
	frames logged; debug.vr_test_sound: where its sound plays, and when it
	played last (the game's ticks) */
	boolean listener_placed;
	real_point3d listener_position;
	real_vector3d listener_forward;
	real_vector3d listener_up;
	unsigned long listener_frames;
	boolean test_sound_placed;
	real_point3d test_sound_eye;
	real_point3d test_sound_position;
	long test_sound_tick;
	/* the first-person weapon's camera is placed for a scope's view
	(vr_render_scope_view): without the shots' kick */
	boolean unkicked;
} vr_render;

/* what the first player's view is, as logged (cinematic_log) */
enum cinematic_view
{
	_cinematic_view_none = 0,
	_cinematic_view_head,
	_cinematic_view_camera,
};

/* where the crosshairs are drawn from the HUD's middle, in its 640x480
(hud_draw.c): where the right controller points */
short vr_render_crosshair_offset[2];

void vr_render_screen_point_end(void);
/* vr_scope.c's */
boolean vr_scope_raised(void);
/* vr_feel.c's */
void vr_feel_frame(real seconds);
void vr_feel_kick(real *rise, real *back);
/* vr_seat.c's */
void vr_seat_eye(long seated_unit_index, boolean aiming, real seconds, real_point3d *position);
void vr_seat_log_camera(real_point3d const *position, real_vector3d const *forward, real_vector3d const *up,
	real seconds);
/* vr_vehicle.c's */
boolean vr_vehicle_view(long seated_unit_index, real seconds, real_point3d *eye, real units, real *base_yaw);
/* vr_death.c's */
boolean vr_death_eye(boolean playing, real seconds, real head_yaw, real body_yaw, real_vector3d const *camera_forward,
	real_point3d *position, real *yaw);
boolean vr_death_turn(real *turn);
boolean vr_death_viewing(void);
/* director.c's: the first player's camera, if the dead's */
struct dead_camera *director_vr_dead_camera(void);
void vr_death_camera(real_point3d const *position, real_vector3d const *forward, real_vector3d const *up,
	real half_across, real half_up);
/* vr_headwall.c's */
void vr_headwall_update(boolean playing, real_point3d const *eye, real_point3d const *head,
	real_vector3d const *forward, real_vector3d const *up, real near, real units, real seconds);
real render_interpolation_fraction(void);
void platform_log(char const *format, ...);
char const *config_string(char const *name);
int config_boolean(char const *name);
static void listener_log(struct render_camera const *camera, real_point3d const *game_eye,
	real_vector3d const *game_forward);

/* ---------- maths */

static void yaw_rotate(real yaw, const float local[3], real_vector3d *world)
{
	real c = (real)cos(yaw), s = (real)sin(yaw);

	world->i = local[0] * c - local[1] * s;
	world->j = local[0] * s + local[1] * c;
	world->k = local[2];
}

/* a vector of the head (game axes from the recentred head) in the world:
pitched up about the body's left, then turned by the yaw (the facing's,
with vr.gamepad_view = "camera": the head looks about from the aim) */
static void view_rotate(real yaw, real pitch, const float local[3], real_vector3d *world)
{
	real c = (real)cos(pitch), s = (real)sin(pitch);
	float pitched[3];

	pitched[0] = (float)(local[0] * c - local[2] * s);
	pitched[1] = local[1];
	pitched[2] = (float)(local[0] * s + local[2] * c);
	yaw_rotate(yaw, pitched, world);
}

/* the view's pitch before the head's: the facing's while the gamepad aims
with vr.gamepad_view = "camera", else none (level with the horizon) */
static real view_pitch(void)
{
	if (!vr_render.view.gamepad_pitch || !halo_vr_aiming() || cinematic_in_progress() ||
		local_player_get_player_index(0) == NONE)
	{
		return 0.0f;
	}
	return player_control_get_facing_angles(0)->pitch;
}

/* the clip transform the game uploads (rasterizer_set_frustum_z): clip[i]
= sum over j of m[i][j] * (x, y, z, 1)[j] (in double: see eye_transforms_set) */
static void frustum_clip_transform(const struct render_frustum *frustum, double m[4][4])
{
	short column, row;

	for (column = 0; column < 4; column++)
	{
		for (row = 0; row < 4; row++)
		{
			const real *n = frustum->world_to_view.n[row];

			m[column][row] = (double)n[0] * frustum->projection_matrix[0][column] +
				(double)n[1] * frustum->projection_matrix[1][column] +
				(double)n[2] * frustum->projection_matrix[2][column];
		}
		m[column][3] += frustum->projection_matrix[3][column];
	}
}

static boolean matrix4_inverse(real m[4][4], double out[4][4])
{
	double a[4][8];
	short i, j, k;

	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			a[i][j] = m[i][j];
			a[i][j + 4] = i == j ? 1.0 : 0.0;
		}
	}
	for (i = 0; i < 4; i++)
	{
		short pivot = i;
		double scale;

		for (k = i + 1; k < 4; k++)
		{
			if (fabs(a[k][i]) > fabs(a[pivot][i]))
				pivot = k;
		}
		if (fabs(a[pivot][i]) < 1e-12)
			return FALSE;
		if (pivot != i)
		{
			for (j = 0; j < 8; j++)
			{
				double t = a[i][j];

				a[i][j] = a[pivot][j];
				a[pivot][j] = t;
			}
		}
		scale = 1.0 / a[i][i];
		for (j = 0; j < 8; j++)
			a[i][j] *= scale;
		for (k = 0; k < 4; k++)
		{
			if (k != i && a[k][i] != 0.0)
			{
				double factor = a[k][i];

				for (j = 0; j < 8; j++)
					a[k][j] -= factor * a[i][j];
			}
		}
	}
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
			out[i][j] = a[i][j + 4];
	}
	return TRUE;
}

/* ---------- the view */

/* the first player's unit, seated in a vehicle and not in a cinematic or
under a scripted camera, else NONE */
static long seated_unit(void)
{
	long player_index = local_player_get_player_index(0);
	long unit_index;
	struct object_datum *object;

	if (player_index == NONE || cinematic_in_progress() ||
		director_get_perspective(0) == _director_perspective_scripted)
	{
		return NONE;
	}
	unit_index = player_get(player_index)->unit_index;
	if (unit_index == NONE)
		return NONE;
	object = object_get(unit_index);
	if (object->object.type != _object_type_biped || object->object.parent_object_index == NONE ||
		!TEST_FLAG(_object_mask_unit, object_get(object->object.parent_object_index)->object.type))
	{
		return NONE;
	}
	return unit_index;
}

/* a cinematic seen from the first player's own head: the game's
first-person camera, not a movie camera (a script's camera_control), as on
the Pelicans' rides in (b30's insertion, where the player sits in the bay and
looks out). The head looks about there as ever, and the hands are the
controllers' (vr.hands) rather than the game's arms posed before the eyes;
they aim nothing (the aim is the game's in a cinematic: player_control.c),
and the player's input is off (cinematic_start), so they cannot fire. A
movie camera elsewhere keeps the game's view, with no hands floating in it. */
static boolean cinematic_head(void)
{
	return cinematic_in_progress() && local_player_get_player_index(0) != NONE &&
		director_get_perspective(0) == _director_perspective_first_person;
}

/* the log of each cinematic: from the player's head (with the unit's seat,
and whether the hands are the controllers'), from a movie camera, and its
end, with the tick of each */
static void cinematic_log(boolean head, boolean hands)
{
	short view = head ? _cinematic_view_head : cinematic_in_progress() ? _cinematic_view_camera : _cinematic_view_none;
	long player_index = local_player_get_player_index(0);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	struct object_datum *unit, *parent = NULL;
	char const *vehicle = NULL;

	if (view == vr_render.cinematic_logged)
		return;
	/* (looked up again here, and only if still there: a frame drawn as a map
	ends has the old map's handles) */
	unit = unit_index != NONE ? object_try_and_get(unit_index) : NULL;
	if (unit && unit->object.parent_object_index != NONE)
		parent = object_try_and_get(unit->object.parent_object_index);
	if (parent)
		vehicle = tag_get_name(parent->definition_index);
	if (view == _cinematic_view_head)
	{
		platform_log("vr cinematic: tick %ld: from the player's head%s%s, the hands %s", game_time_get(),
			vehicle ? ", seated in " : "", vehicle ? vehicle : "", hands ? "the controllers'" : "the game's");
	}
	else if (view == _cinematic_view_camera)
	{
		platform_log("vr cinematic: tick %ld: from a movie camera", game_time_get());
	}
	else
	{
		platform_log("vr cinematic: tick %ld: over, the player's view", game_time_get());
	}
	vr_render.cinematic_logged = view;
}

/* the hands are posed where the controllers are (vr_render_weapon_camera):
while the right controller aims, and in a cinematic from the player's head
(posed, aiming nothing) */
static boolean hands_posed(void)
{
	return vr_render.view.controller_aim && (halo_vr_aiming() || vr_render.cinematic_head);
}

/* how far the game's eye is lowered by its crouch now (world units) that
the view takes back out, the crouch being the head's (vr.physical_crouch):
the head has already come down as far as it did, and the game's drop to its
crouching camera height (biped_get_sight_position) would lower the view
again, and further (the cyborg's, 0.27 world units: 0.82 m). The crouch the
eye has is blended between the last two ticks, as the camera is. Once the
head's, the crouch stays it until the game has stood the player up again
(both ticks the eye blends), so that standing up does not drop the view by
the game's crouch still left; a hand's crouch (the left stick) without the
head's keeps the game's drop. Only on foot and alive, in the first-person
view. */
static real crouch_lift(void)
{
	long player_index = local_player_get_player_index(0);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	struct biped_datum *biped = unit_index != NONE ? biped_try_and_get(unit_index) : NULL;
	struct biped_definition *definition;
	long tick = game_time_get();
	int crouch = halo_vr_crouch();

	vr_render.crouch_lift = 0.0f;
	vr_render.crouch_blended = 0.0f;
	if (!biped || biped->object.parent_object_index != NONE || TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
		cinematic_in_progress() || director_get_perspective(0) != _director_perspective_first_person)
	{
		vr_render.crouch_valid = FALSE;
		vr_render.crouch_physical = FALSE;
		return 0.0f;
	}
	if (!vr_render.crouch_valid || unit_index != vr_render.crouch_unit_index)
	{
		vr_render.crouch_valid = TRUE;
		vr_render.crouch_unit_index = unit_index;
		vr_render.crouch_previous = vr_render.crouch_latest = biped->biped.crouch;
		vr_render.crouch_tick = tick;
		vr_render.crouch_physical = FALSE;
	}
	else if (tick != vr_render.crouch_tick)
	{
		/* (several ticks since the last frame: the crouch a tick ago, as
		nearly as a steady change since tells, as the camera's previous is,
		render_interpolation.c) */
		real ticks = (real)(tick - vr_render.crouch_tick);

		vr_render.crouch_previous = vr_render.crouch_latest;
		vr_render.crouch_latest = biped->biped.crouch;
		if (ticks > 1.0f)
			vr_render.crouch_previous += (vr_render.crouch_latest - vr_render.crouch_previous) * (ticks - 1.0f) / ticks;
		vr_render.crouch_tick = tick;
	}
	if (crouch & HALO_VR_CROUCH_PHYSICAL)
		vr_render.crouch_physical = TRUE;
	else if ((crouch & HALO_VR_CROUCH_BUTTON) || (vr_render.crouch_latest <= 0.0f && vr_render.crouch_previous <= 0.0f))
		vr_render.crouch_physical = FALSE;
	if (!vr_render.crouch_physical)
		return 0.0f;
	definition = biped_definition_get(biped->definition_index);
	vr_render.crouch_blended = vr_render.crouch_previous +
		(vr_render.crouch_latest - vr_render.crouch_previous) * render_interpolation_fraction();
	vr_render.crouch_lift = vr_render.crouch_blended *
		(definition->biped.standing_camera_height - definition->biped.crouching_camera_height);
	return vr_render.crouch_lift;
}

/* debug.vr_test_hands (which can lower the head): the view's height above
the player's feet as the crouch and the head move, once a tick while
either does: with the crouch the head's, the game's eye standing plus the
head's drop, however far the game has crouched */
static void crouch_log(real lift)
{
	long player_index = local_player_get_player_index(0);
	struct halo_vr_view *view = &vr_render.view;
	struct biped_datum *biped;
	real units = view->world_units_per_metre;
	static long logged_tick = -1;
	static real logged_crouch = -1.0f;
	static boolean logged_physical;

	if (!halo_vr_test_hands() || !vr_render.crouch_valid || units <= 0.0f || player_index == NONE ||
		vr_render.crouch_tick == logged_tick || !(biped = biped_try_and_get(player_get(player_index)->unit_index)))
	{
		return;
	}
	if (vr_render.crouch_latest == logged_crouch && vr_render.crouch_physical == logged_physical &&
		fabsf(view->head_position[2] - vr_render.crouch_logged_head) < 0.02f)
	{
		return;
	}
	logged_tick = vr_render.crouch_tick;
	logged_crouch = vr_render.crouch_latest;
	logged_physical = vr_render.crouch_physical;
	vr_render.crouch_logged_head = view->head_position[2];
	platform_log("vr crouch: tick %ld, the game's crouch %.2f (%s), the view %.3f m above the feet: the game's eye "
		"%.3f, taken back out %.3f, the head %+.3f", vr_render.crouch_tick, vr_render.crouch_latest,
		vr_render.crouch_physical ? "the head's" : (halo_vr_crouch() & HALO_VR_CROUCH_BUTTON) ? "a hand's" : "none",
		(vr_render.head_position.z - biped->object.position.z) / units,
		(vr_render.base_position.z - lift - biped->object.position.z) / units, lift / units, view->head_position[2]);
}

/* turns the window's camera into the VR frame's centre camera (both its
render and rasterizer cameras); FALSE without a VR frame to draw */
/* The projection and culling frusta depend only on the current camera/pose. */
static void camera_frusta(struct render_camera *camera, struct render_camera *rasterizer_camera)
{
	struct halo_vr_view *view = &vr_render.view;
	real left = 0.0f, right = 0.0f, up = 0.0f, down = 0.0f, tangent;
	real aspect;
	short eye;

	for (eye = 0; eye < 2; eye++)
	{
		left = eye ? MIN(left, view->fov[eye][0]) : view->fov[eye][0];
		right = eye ? MAX(right, view->fov[eye][1]) : view->fov[eye][1];
		up = eye ? MAX(up, view->fov[eye][2]) : view->fov[eye][2];
		down = eye ? MIN(down, view->fov[eye][3]) : view->fov[eye][3];
	}
	if (halo_vr_late_latching())
	{
		/* (the eyes drawn turned a little from where the game culls for:
		halo_vr_latch) */
		const real margin = LATCH_MARGIN_DEGREES * (real)M_PI / 180.0f, most = 85.0f * (real)M_PI / 180.0f;

		left = (real)tan(MAX((real)atan(left) - margin, -most));
		right = (real)tan(MIN((real)atan(right) + margin, most));
		up = (real)tan(MIN((real)atan(up) + margin, most));
		down = (real)tan(MAX((real)atan(down) - margin, -most));
	}
	left *= UNION_MARGIN;
	right *= UNION_MARGIN;
	up *= UNION_MARGIN;
	down *= UNION_MARGIN;
	tangent = MAX(up, -down);
	camera->vertical_field_of_view = 2.0f * (real)atan(tangent);
	aspect = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0) /
		(real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);
	vr_render.bounds.x0 = left / (aspect * tangent);
	vr_render.bounds.x1 = right / (aspect * tangent);
	vr_render.bounds.y0 = down / tangent;
	vr_render.bounds.y1 = up / tangent;
	camera->mirrored = FALSE;
	rasterizer_camera->position = camera->position;
	rasterizer_camera->forward = camera->forward;
	rasterizer_camera->up = camera->up;
	rasterizer_camera->vertical_field_of_view = camera->vertical_field_of_view;
	rasterizer_camera->mirrored = FALSE;

	/* each eye's camera: the centre's, moved along the head's axes across
	the view (not along it: an eye's depth is then the centre's, which
	eye_transforms_set relies on; the eyes are side by side, a millimetre or
	so at most from the plane through the centre across the view) */
	for (eye = 0; eye < 2; eye++)
	{
		struct render_camera eye_camera = *camera;
		real_rectangle2d eye_bounds;
		real_vector3d left_axis;
		real units = view->world_units_per_metre;
		const float *o = view->eye_offset[eye];

		cross_product3d(&camera->up, &camera->forward, &left_axis);
		normalize3d(&left_axis);
		eye_camera.position.x += (left_axis.i * o[1] + camera->up.i * o[2]) * units;
		eye_camera.position.y += (left_axis.j * o[1] + camera->up.j * o[2]) * units;
		eye_camera.position.z += (left_axis.k * o[1] + camera->up.k * o[2]) * units;
		eye_bounds.x0 = view->fov[eye][0] / (aspect * tangent);
		eye_bounds.x1 = view->fov[eye][1] / (aspect * tangent);
		eye_bounds.y0 = view->fov[eye][3] / tangent;
		eye_bounds.y1 = view->fov[eye][2] / tangent;
		render_camera_build_frustum(&eye_camera, &eye_bounds, &vr_render.eyes[eye], TRUE);
		vr_render.eye_positions[eye] = eye_camera.position;
		render_camera_build_frustum(camera, &eye_bounds, &vr_render.eyes_at_centre[eye], TRUE);
	}
	{
		/* the centre view has the game's viewport (480 lines) over the
		union's height (up - down, in tangents); an eye has its image's
		height over its own */
		int eye_width = 0, eye_height = 0;
		real eye_span = MAX(view->fov[0][2] - view->fov[0][3], view->fov[1][2] - view->fov[1][3]);
		real viewport_height = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0);

		halo_vr_eye_size(&eye_width, &eye_height);
		vr_render.level_of_detail_scale = eye_height > 0 && eye_span > 0.0f && viewport_height > 0.0f ?
			((real)eye_height / eye_span) / (viewport_height / (up - down)) : 1.0f;
	}
	{
		real farthest = 0.0f;

		for (eye = 0; eye < 2; eye++)
		{
			const float *o = view->eye_offset[eye];

			farthest = MAX(farthest, (real)sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]));
		}
		structure_culling_set_camera_radius((farthest + (halo_vr_late_latching() ? CULLING_LATCH_ALLOWANCE :
			CULLING_ALLOWANCE)) * view->world_units_per_metre);
	}
	vr_render.infinite = FALSE;
	vr_render.projection_valid = FALSE;
	vr_render.active = TRUE;
}

static struct {
	boolean pending, playing;
	real_point3d eye;
	struct render_camera camera;
} preparation_headwall;

boolean vr_render_camera(struct render_camera *camera, struct render_camera *rasterizer_camera, boolean preparing)
{
	struct halo_vr_view *view = &vr_render.view;
	real_point3d game_eye;
	real base_yaw;
	real pitch;
	real_vector3d offset;
	real lift;
	boolean dead;
	real_vector3d game_forward = camera->forward;
	boolean camera_yaw;

	preparation_headwall.pending = FALSE;
	vr_render.active = FALSE;
	vr_render.seated_unit_index = NONE;
	vr_render.weapon_camera_set = FALSE;
	vr_render.weapon_camera_valid = FALSE;
	vr_render.weapon_in_hand = FALSE;
	vr_render.cinematic_head = FALSE;
	if (!halo_vr_view(view))
		return FALSE;
	game_eye = camera->position;
	vr_feel_frame(view->seconds);
	vr_render.cinematic_head = cinematic_head();
	cinematic_log(vr_render.cinematic_head, hands_posed());
	/* the turn of the stick (and of the aim) while the aim follows the head;
	else (a cinematic, the main menu's scene) the camera's own heading */
	camera_yaw = cinematic_in_progress() || !halo_vr_aiming();
	base_yaw = camera_yaw ? (real)atan2(camera->forward.j, camera->forward.i) : view->body_yaw;
	/* in a vehicle's seat (vr.vehicle_view): the eye is the seat's (where
	the seated player's head is, as the frame poses it between ticks, or
	held still in the vehicle: vr.seat_motion), and the view is level and
	turned as on foot; not the game's camera following the vehicle, which
	swings about it with the aim. The eye glides between the two, riding the
	body's climb in and out (or blinks: vr_seat.c). */
	if (view->vehicle_first_person && halo_vr_aiming())
		vr_render.seated_unit_index = seated_unit();
	/* (on foot, crouched by the head: the game's drop taken back out of
	the player's eye, before the seat's blinks hold it) */
	if (vr_render.seated_unit_index == NONE)
	{
		lift = crouch_lift();
	}
	else
	{
		lift = vr_render.crouch_lift = vr_render.crouch_blended = 0.0f;
		vr_render.crouch_valid = vr_render.crouch_physical = FALSE;
	}
	game_eye = camera->position;
	camera->position.z += lift;
	/* dead (vr.death_view = "face", "still"): the eye held where the player
	died, and the view turned as the player turns it, not the game's dead
	camera pulling away from the body and turning to it; watching another,
	held where the game's camera was and put again in a blink, the view then
	turned to look where it does (vr_death.c). With "game", the game's
	camera, blinked into and out of (vr_seat.c). */
	dead = vr_death_eye(!cinematic_in_progress(), view->seconds,
		(real)atan2(view->head_forward[1], view->head_forward[0]), view->body_yaw, &camera->forward,
		&camera->position, &base_yaw);
	vr_seat_eye(vr_render.seated_unit_index,
		(halo_vr_aiming() || dead || director_vr_dead_camera()) && !cinematic_in_progress(), view->seconds,
		&camera->position);
	/* (on a vehicle's handlebars, the view turns with the vehicle:
	vr_vehicle.c) */
	vr_vehicle_view(vr_render.seated_unit_index, view->seconds, &camera->position, view->world_units_per_metre,
		&base_yaw);
	/* (the eye moved by the hands climbing a vehicle: vr_climb.c) */
	{
		extern void vr_climb_eye(long seated_unit_index, boolean aiming, real base_yaw, real units, real seconds,
			real_point3d *eye);

		vr_climb_eye(vr_render.seated_unit_index, halo_vr_aiming() && !cinematic_in_progress() && !dead &&
			view->controller_aim, base_yaw, view->world_units_per_metre, view->seconds, &camera->position);
	}
	vr_render.base_position = camera->position;
	vr_render.base_yaw = base_yaw;
	vector_from_points3d(&game_eye, &camera->position, &vr_render.listener_eye_offset);
	vr_render.listener_camera_yaw = camera_yaw && base_yaw == (real)atan2(game_forward.j, game_forward.i);
	vr_render.listener_yaw = base_yaw;
	vr_render.listener_tick = game_time_get();
	vr_render.aim_base_valid = halo_vr_aiming() && !cinematic_in_progress() && !dead &&
		local_player_get_player_index(0) != NONE;
	if (vr_render.aim_base_valid)
	{
		vr_render.aim_base_unit_index = player_get(local_player_get_player_index(0))->unit_index;
		vr_render.aim_base_tick = game_time_get();
	}
	vr_render.aim_forward = camera->forward;
	vr_render.aim_up = camera->up;
	if (view->gamepad_aim && halo_vr_aiming() && local_player_get_player_index(0) != NONE)
	{
		/* the player's facing, not the camera: the camera shakes (firing,
		explosions: player_effect_get_camera_effect_matrix), which the
		weapon and the reticle, before a still head, need not */
		real along;

		player_control_get_facing_direction(0, &vr_render.aim_forward);
		vr_render.aim_up = *global_up3d;
		along = dot_product3d(&vr_render.aim_up, &vr_render.aim_forward);
		vr_render.aim_up.i -= vr_render.aim_forward.i * along;
		vr_render.aim_up.j -= vr_render.aim_forward.j * along;
		vr_render.aim_up.k -= vr_render.aim_forward.k * along;
		if (normalize3d(&vr_render.aim_up) <= 0.0f)
			vr_render.aim_up = camera->up;
	}
	pitch = view_pitch();
	vr_render.listener_pitch = pitch;
	vr_render.listener_valid = TRUE;
	view_rotate(base_yaw, pitch, view->head_forward, &camera->forward);
	view_rotate(base_yaw, pitch, view->head_up, &camera->up);
	view_rotate(base_yaw, pitch, view->head_position, &offset);
	camera->position.x += offset.i * view->world_units_per_metre;
	camera->position.y += offset.j * view->world_units_per_metre;
	camera->position.z += offset.k * view->world_units_per_metre;
	vr_render.head_position = camera->position;
	/* the head inside a wall, or through it, fades the view: tested from
	the game's own eye, where the game knows the player can be (on foot
	alone: vr_headwall.c) */
	preparation_headwall.pending = preparing;
	preparation_headwall.playing = halo_vr_aiming() && !cinematic_in_progress() && !dead;
	preparation_headwall.eye = game_eye;
	preparation_headwall.camera = *camera;
	if (!preparing)
		vr_headwall_update(preparation_headwall.playing, &game_eye, &camera->position,
			&camera->forward, &camera->up, camera->z_near, view->world_units_per_metre, view->seconds);
	vr_seat_log_camera(&camera->position, &camera->forward, &camera->up, view->seconds);
	vr_death_camera(&camera->position, &camera->forward, &camera->up,
		(real)atan(MIN(-MIN(view->fov[0][0], view->fov[1][0]), MAX(view->fov[0][1], view->fov[1][1]))),
		(real)atan(MIN(MAX(view->fov[0][2], view->fov[1][2]), -MIN(view->fov[0][3], view->fov[1][3]))));
	crouch_log(lift);
	/* (the gamepad's weapon, posed from the eye whichever way the head
	leans, comes down with the head's crouch as far as the game's drop is
	taken out) */
	vr_render.crouch_head_drop = vr_render.crouch_blended * offset.k * view->world_units_per_metre;

	camera_frusta(camera, rasterizer_camera);
	listener_log(camera, &game_eye, &game_forward);
	vr_render.listener_placed = FALSE;
	return TRUE;
}

/* Keep the one visual camera update, but rebase its head pose after loading.
The camera's seat/death offsets and the game's original eye remain unchanged. */
void vr_render_camera_refresh(struct render_camera *camera, struct render_camera *rasterizer_camera)
{
	struct halo_vr_view before = vr_render.view;
	struct render_camera old_camera = *camera;
	boolean refreshed;
	real_vector3d old_forward, old_up, old_left, new_forward, new_up, new_left;
	real_vector3d old_position, new_position, directions[2];
	real yaw = vr_render.base_yaw, pitch = vr_render.listener_pitch;
	short direction;

	refreshed = halo_vr_refresh_tracking(&vr_render.view);
	if (!refreshed)
		goto headwall;
	/* An unchanged pose needs no extra floating-point round trip. */
	if (!memcmp(before.head_forward, vr_render.view.head_forward, sizeof(before.head_forward)) &&
		!memcmp(before.head_up, vr_render.view.head_up, sizeof(before.head_up)) &&
		!memcmp(before.head_position, vr_render.view.head_position, sizeof(before.head_position)) &&
		!memcmp(before.eye_offset, vr_render.view.eye_offset, sizeof(before.eye_offset)) &&
		!memcmp(before.fov, vr_render.view.fov, sizeof(before.fov)))
		goto headwall;
	view_rotate(yaw, pitch, before.head_forward, &old_forward);
	view_rotate(yaw, pitch, before.head_up, &old_up);
	view_rotate(yaw, pitch, vr_render.view.head_forward, &new_forward);
	view_rotate(yaw, pitch, vr_render.view.head_up, &new_up);
	cross_product3d(&old_up, &old_forward, &old_left);
	cross_product3d(&new_up, &new_forward, &new_left);
	directions[0] = camera->forward;
	directions[1] = camera->up;
	for (direction = 0; direction < 2; direction++)
	{
		real along = dot_product3d(&directions[direction], &old_forward);
		real left = dot_product3d(&directions[direction], &old_left);
		real up = dot_product3d(&directions[direction], &old_up);
		directions[direction].i = along * new_forward.i + left * new_left.i + up * new_up.i;
		directions[direction].j = along * new_forward.j + left * new_left.j + up * new_up.j;
		directions[direction].k = along * new_forward.k + left * new_left.k + up * new_up.k;
	}
	camera->forward = directions[0];
	camera->up = directions[1];
	view_rotate(yaw, pitch, before.head_position, &old_position);
	view_rotate(yaw, pitch, vr_render.view.head_position, &new_position);
	camera->position.x += (new_position.i - old_position.i) * before.world_units_per_metre;
	camera->position.y += (new_position.j - old_position.j) * before.world_units_per_metre;
	camera->position.z += (new_position.k - old_position.k) * before.world_units_per_metre;
	vr_render.head_position.x += (new_position.i - old_position.i) * before.world_units_per_metre;
	vr_render.head_position.y += (new_position.j - old_position.j) * before.world_units_per_metre;
	vr_render.head_position.z += (new_position.k - old_position.k) * before.world_units_per_metre;
	vr_render.crouch_head_drop = vr_render.crouch_blended * new_position.k * before.world_units_per_metre;
	preparation_headwall.camera.position = vr_render.head_position;
	preparation_headwall.camera.forward = new_forward;
	preparation_headwall.camera.up = new_up;
	camera_frusta(camera, rasterizer_camera);
headwall:
	if (halo_load_trace_enabled())
	{
		char detail[768];

		snprintf(detail, sizeof(detail),
			"valid=%d old_position=%.9g,%.9g,%.9g new_position=%.9g,%.9g,%.9g old_forward=%.9g,%.9g,%.9g new_forward=%.9g,%.9g,%.9g",
			refreshed, old_camera.position.x, old_camera.position.y, old_camera.position.z,
			camera->position.x, camera->position.y, camera->position.z,
			old_camera.forward.i, old_camera.forward.j, old_camera.forward.k,
			camera->forward.i, camera->forward.j, camera->forward.k);
		halo_load_trace("loading.camera.refresh", detail);
	}
	/* One collision/fade evaluation, after the first picture's pose is fresh. */
	if (preparation_headwall.pending)
	{
		preparation_headwall.pending = FALSE;
		vr_headwall_update(preparation_headwall.playing, &preparation_headwall.eye, &preparation_headwall.camera.position,
			&preparation_headwall.camera.forward, &preparation_headwall.camera.up,
			preparation_headwall.camera.z_near, before.world_units_per_metre, before.seconds);
	}
}


/* a view's yaw (left), pitch (up) and roll (right) in degrees, for the logs */
static void view_angles(real_vector3d const *forward, real_vector3d const *up, real angles[3])
{
	real_vector3d level_up, right;
	real along = forward->k;

	level_up.i = -forward->i * along;
	level_up.j = -forward->j * along;
	level_up.k = 1.0f - forward->k * along;
	if (normalize3d(&level_up) <= 0.0f)
		level_up = *up;
	cross_product3d(forward, &level_up, &right);
	angles[0] = (real)atan2(forward->j, forward->i) * 180.0f / (real)M_PI;
	angles[1] = (real)asin(PIN(forward->k, -1.0f, 1.0f)) * 180.0f / (real)M_PI;
	angles[2] = (real)atan2(dot_product3d(up, &right), dot_product3d(up, &level_up)) * 180.0f / (real)M_PI;
}

/* debug.vr_test_sound, "<text> <seconds>[ <distance> <degrees>]": the
map's first sound whose tag name has the text in it, played again every so
many seconds at a point held in the world, distance world units (default 2)
from where the game's eye was at the first (again if it is carried a tenth
of a unit away from there: a level's ride in, the landing), turned degrees
left of the heading the view had (default 0: ahead), for automated tests of
the listener (port/linux/README.md, "VR") */
static void listener_test_sound(real_point3d const *eye)
{
	struct tag_iterator iterator;
	struct sound_location location;
	char text[64];
	real seconds, distance = 2.0f, degrees = 0.0f, yaw;
	long tag_index, tick = game_time_get();

	if (sscanf(config_string("debug.vr_test_sound"), "%63s %f %f %f", text, &seconds, &distance, &degrees) < 2 ||
		seconds <= 0.0f)
	{
		return;
	}
	/* (placed again if the player is carried away from it) */
	if (!vr_render.test_sound_placed || tick < vr_render.test_sound_tick ||
		distance_squared3d(eye, &vr_render.test_sound_eye) > 0.1f * 0.1f)
	{
		vr_render.test_sound_eye = *eye;
		yaw = vr_render.listener_yaw + degrees * (real)M_PI / 180.0f;
		vr_render.test_sound_position.x = eye->x + distance * (real)cos(yaw);
		vr_render.test_sound_position.y = eye->y + distance * (real)sin(yaw);
		vr_render.test_sound_position.z = eye->z;
		vr_render.test_sound_placed = TRUE;
		vr_render.test_sound_tick = tick - (long)(seconds * TICKS_PER_SECOND);
	}
	if (tick - vr_render.test_sound_tick < (long)(seconds * TICKS_PER_SECOND))
		return;
	vr_render.test_sound_tick = tick;
	tag_iterator_new(&iterator, SOUND_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *name = tag_get_name(tag_index);

		if (name && strstr(name, text))
			break;
	}
	if (tag_index == NONE)
	{
		platform_log("vr test sound: no sound in this map named with \"%s\"", text);
		return;
	}
	memset(&location, 0, sizeof(location));
	location.position = vr_render.test_sound_position;
	location.forward = *global_forward3d;
	scenario_location_from_point(&location.game_location, &location.position);
	unattached_impulse_sound_new(tag_index, &location, 1.0f);
	platform_log("vr test sound: tick %ld: %s at %.3f %.3f %.3f", tick, tag_get_name(tag_index),
		location.position.x, location.position.y, location.position.z);
}

/* game_sound.c (compute_sound_obstruction), with debug.vr_audio_log: the
obstruction and occlusion of debug.vr_test_sound's sound, once a tick while
it plays, and whether it was judged from the head or the game's eye */
void vr_render_sound_obstruction(real_point3d const *position, boolean head, real obstruction, real occlusion)
{
	if (!vr_render.test_sound_placed || !config_boolean("debug.vr_audio_log") ||
		distance_squared3d(position, &vr_render.test_sound_position) > 0.01f * 0.01f)
	{
		return;
	}
	platform_log("vr audio: tick %ld: test sound obstruction %.2f occlusion %.2f, from the %s", game_time_get(),
		obstruction, occlusion, head ? "head" : "game's eye");
}

/* debug.vr_audio_log: each frame, the listener placed before it was
drawn against the head it was drawn from, and the game's camera (the
body's heading), which the listener is not */
static void listener_log(struct render_camera const *camera, real_point3d const *game_eye,
	real_vector3d const *game_forward)
{
	real head[3], listener[3];

	if (!config_boolean("debug.vr_audio_log"))
		return;
	vr_render.listener_frames++;
	view_angles(&camera->forward, &camera->up, head);
	if (!vr_render.listener_placed)
	{
		platform_log("vr audio: frame %lu tick %ld: head yaw %.2f pitch %.2f roll %.2f at %.3f %.3f %.3f; "
			"no listener placed (the game's camera)", vr_render.listener_frames, game_time_get(), head[0], head[1],
			head[2], camera->position.x, camera->position.y, camera->position.z);
		return;
	}
	view_angles(&vr_render.listener_forward, &vr_render.listener_up, listener);
	platform_log("vr audio: frame %lu tick %ld: head yaw %.2f pitch %.2f roll %.2f at %.3f %.3f %.3f; "
		"listener yaw %.2f pitch %.2f roll %.2f at %.3f %.3f %.3f (%.1f mm from the head); game camera yaw %.2f "
		"at %.3f %.3f %.3f", vr_render.listener_frames, game_time_get(), head[0], head[1], head[2],
		camera->position.x, camera->position.y, camera->position.z, listener[0], listener[1], listener[2],
		vr_render.listener_position.x, vr_render.listener_position.y, vr_render.listener_position.z,
		(real)sqrt(distance_squared3d(&vr_render.listener_position, &camera->position)) * 1000.0f /
			vr_render.view.world_units_per_metre,
		(real)atan2(game_forward->j, game_forward->i) * 180.0f / (real)M_PI, game_eye->x, game_eye->y, game_eye->z);
}

/* sound_manager.c (refresh_listener): the first player's sounds are heard
from the head, as it is seen from. Placed: from the game's eye (eye: its
camera at the tick, which the flat screen hears from, the sounds being where
their objects are at the tick: from the view's eye between ticks, a sound
carried along, the player's own, would sway against the head), moved as the
view's eye is moved from the game's (the crouch, a vehicle's seat, the
death's held eye: as the last frame drawn moved it), and by the head's
place in the room. Turned as the head is (its yaw, pitch and roll), turned
by the heading the view is: the stick's turn, or where the view has the
camera's heading (a cinematic, the main menu's scene), the camera's as it
is now (eye_forward: a cut turns it at once). The head is this frame's, as
located when it began (halo_vr_head), not the last frame's: the listener is
placed before the frame is drawn. FALSE without a VR frame, or before one is
drawn on this map: the game's camera, as on the flat screen. */
boolean vr_render_listener(real_point3d const *eye, real_vector3d const *eye_forward, real_point3d *position,
	real_vector3d *forward, real_vector3d *up)
{
	float head_forward[3], head_up[3], head_position[3];
	real_vector3d offset;
	real units = vr_render.view.world_units_per_metre;
	long tick = game_time_get();
	real yaw = vr_render.listener_yaw;

	if (!vr_render.listener_valid || tick < vr_render.listener_tick ||
		tick - vr_render.listener_tick > TICKS_PER_SECOND ||
		!halo_vr_head(head_forward, head_up, head_position))
	{
		return FALSE;
	}
	if (vr_render.listener_camera_yaw)
		yaw = (real)atan2(eye_forward->j, eye_forward->i);
	view_rotate(yaw, vr_render.listener_pitch, head_forward, forward);
	view_rotate(yaw, vr_render.listener_pitch, head_up, up);
	view_rotate(yaw, vr_render.listener_pitch, head_position, &offset);
	position->x = eye->x + vr_render.listener_eye_offset.i + offset.i * units;
	position->y = eye->y + vr_render.listener_eye_offset.j + offset.j * units;
	position->z = eye->z + vr_render.listener_eye_offset.k + offset.k * units;
	vr_render.listener_placed = TRUE;
	vr_render.listener_position = *position;
	vr_render.listener_forward = *forward;
	vr_render.listener_up = *up;
	listener_test_sound(eye);
	return TRUE;
}

/* render_objects.c: what the size of an object in the centre view's
pixels (the game's viewport, 480 lines over the union of the eyes' fields
of view) is in an eye's (its image's lines over its field of view), for the
models' detail levels, the lighting's refresh and the shadows: the game
judged them by the centre view, about 3.6 times as small as the headset
shows them (1728 lines an eye). 1 outside the eye pass. */
real vr_render_level_of_detail_scale(void)
{
	return vr_render.active ? vr_render.level_of_detail_scale : 1.0f;
}

/* the centre camera's frustum bounds (render_player_frame) */
void vr_render_frustum_bounds(real_rectangle2d *bounds)
{
	if (vr_render.active)
		*bounds = vr_render.bounds;
}

/* a hand's place (game axes from the recentred head, metres) in the world */
static void hand_point(const float place[3], real_point3d *point)
{
	real_vector3d offset;
	real units = vr_render.view.world_units_per_metre;

	yaw_rotate(vr_render.base_yaw, place, &offset);
	point->x = vr_render.base_position.x + offset.i * units;
	point->y = vr_render.base_position.y + offset.j * units;
	point->z = vr_render.base_position.z + offset.k * units;
}

/* the first-person weapon's camera kicked by the shots (vr.recoil,
vr_feel_kick): turned up about the hand, as a recoil turns a gun about the
hand holding it, and pushed back along where it pointed */
static void weapon_kick(struct render_camera *camera, real_point3d const *hand)
{
	real_vector3d forward = camera->forward, up = camera->up, offset;
	real units = vr_render.view.world_units_per_metre;
	real rise, back, c, s, along, above;

	vr_feel_kick(&rise, &back);
	if (rise == 0.0f && back == 0.0f)
		return;
	c = (real)cos(rise * _pi / 180.0f);
	s = (real)sin(rise * _pi / 180.0f);
	camera->forward.i = forward.i * c + up.i * s;
	camera->forward.j = forward.j * c + up.j * s;
	camera->forward.k = forward.k * c + up.k * s;
	camera->up.i = up.i * c - forward.i * s;
	camera->up.j = up.j * c - forward.j * s;
	camera->up.k = up.k * c - forward.k * s;
	/* (the camera's place from the hand turned the same) */
	vector_from_points3d(hand, &camera->position, &offset);
	along = dot_product3d(&offset, &forward);
	above = dot_product3d(&offset, &up);
	camera->position.x = hand->x + offset.i + along * (camera->forward.i - forward.i) +
		above * (camera->up.i - up.i) - forward.i * back * units;
	camera->position.y = hand->y + offset.j + along * (camera->forward.j - forward.j) +
		above * (camera->up.j - up.j) - forward.j * back * units;
	camera->position.z = hand->z + offset.k + along * (camera->forward.k - forward.k) +
		above * (camera->up.k - up.k) - forward.k * back * units;
}

/* the camera the first-person weapon is posed from (render.c): with the
controller aiming (or the hands posed in a cinematic from the player's
head: hands_posed), one placed so that the weapon is in the right controller,
pointing where it points: with the hands floating (vr.hands), the right
hand's middle (as the weapon's idle pose has it) at the controller's grip,
else the weapon's grip vr.weapon_offset from the camera; FALSE leaves the
head's. A left-hander's weapon (halo_vr_left_handed: the right controller
here is the left one) is drawn mirrored about the plane of the camera's
forward and up, along the weapon (vr_render_weapon_mirrored), so it is
placed with its hand on the other side of that plane, where the mirror
brings it back */
boolean vr_render_weapon_camera(struct render_camera *camera)
{
	struct halo_vr_view *view = &vr_render.view;
	real_vector3d forward, up, right, left;
	real_point3d hand, centre;
	real units = view->world_units_per_metre;
	const float *o = view->weapon_offset;
	real across;

	if (!vr_render.active)
		return FALSE;
	/* (the head's, kept for the hands when the controller is not tracked) */
	vr_render.weapon_camera = *camera;
	vr_render.weapon_camera_set = halo_vr_aiming() || hands_posed();
	vr_render.weapon_mirrored = FALSE;
	if (view->gamepad_aim && halo_vr_aiming())
	{
		/* the gamepad aims (vr.aim = "gamepad"): the weapon is posed as on
		the flat screen, from the player's eye looking where the player
		faces, there whichever way the head looks or leans */
		camera->position = vr_render.base_position;
		camera->position.z += vr_render.crouch_head_drop;
		camera->forward = vr_render.aim_forward;
		camera->up = vr_render.aim_up;
		vr_render.weapon_camera = *camera;
		vr_render.weapon_camera_valid = TRUE;
		return TRUE;
	}
	if (!hands_posed())
		return FALSE;
	vr_render.weapon_mirrored = halo_vr_left_handed();
	across = vr_render.weapon_mirrored ? -1.0f : 1.0f;
	yaw_rotate(vr_render.base_yaw, view->hand_forward, &forward);
	yaw_rotate(vr_render.base_yaw, view->hand_up, &up);
	cross_product3d(&forward, &up, &right);
	normalize3d(&right);
	camera->forward = forward;
	camera->up = up;
	if (view->floating_hands && vr_hands_right_centre(&centre))
	{
		hand_point(view->right_grip_position, &hand);
		left.i = -right.i;
		left.j = -right.j;
		left.k = -right.k;
		centre.y *= across;
		camera->position.x = hand.x - (forward.i * centre.x + left.i * centre.y + up.i * centre.z);
		camera->position.y = hand.y - (forward.j * centre.x + left.j * centre.y + up.j * centre.z);
		camera->position.z = hand.z - (forward.k * centre.x + left.k * centre.y + up.k * centre.z);
	}
	else
	{
		hand_point(view->hand_position, &hand);
		camera->position.x = hand.x - (right.i * o[0] * across + up.i * o[1] + forward.i * o[2]) * units;
		camera->position.y = hand.y - (right.j * o[0] * across + up.j * o[1] + forward.j * o[2]) * units;
		camera->position.z = hand.z - (right.k * o[0] * across + up.k * o[1] + forward.k * o[2]) * units;
	}
	/* the shots' kick, the drawn weapon's alone: not a scope's view's
	(vr_render_scope_view), nor while a scope is raised to an eye */
	if (!vr_render.unkicked && !vr_scope_raised())
		weapon_kick(camera, &hand);
	vr_render.weapon_camera = *camera;
	vr_render.weapon_camera_valid = TRUE;
	vr_render.weapon_in_hand = TRUE;
	return TRUE;
}

/* vr_hands.c and first_person_weapons.c: the first-person weapon and the
hands posed this frame are drawn mirrored (a left-hander's: the game's are
right-handed), about the plane of the forward and the up of the camera it
was posed from, along the weapon (vr_render_weapon_camera): the camera's
place and its left (the plane's normal) in the world; FALSE, they are not */
boolean vr_render_weapon_mirrored(real_point3d *origin, real_vector3d *normal)
{
	struct render_camera const *camera = &vr_render.weapon_camera;

	if (!vr_render.active || !vr_render.weapon_camera_set || !vr_render.weapon_mirrored)
		return FALSE;
	*origin = camera->position;
	cross_product3d(&camera->up, &camera->forward, normal);
	return normalize3d(normal) > 0.0f;
}

/* vr_hands.c: the camera the first-person weapon was posed from this frame
(vr_render_weapon_camera's, the shots' kick in it), when that put it in the
right controller, whatever vr.hands is, to hold the weapon there; FALSE
when it did not (the head's, the gamepad's aim: the weapon as on the flat
screen) */
boolean vr_render_weapon_held(real_matrix4x3 *camera)
{
	struct render_camera const *weapon = &vr_render.weapon_camera;

	if (!vr_render.active || !vr_render.weapon_camera_set || !vr_render.weapon_in_hand)
		return FALSE;
	matrix4x3_from_point_and_vectors(camera, &weapon->position, &weapon->forward, &weapon->up);
	return TRUE;
}

/* the aim's line: from the right controller along it (or, with a gamepad,
from the game's eye along the facing); FALSE if it has no direction */
static boolean aim_line(real_point3d *origin, real_vector3d *direction)
{
	struct halo_vr_view *view = &vr_render.view;

	if (view->gamepad_aim)
	{
		/* (from the game's eye, which the aim is from, crouched or not) */
		*origin = vr_render.base_position;
		origin->z -= vr_render.crouch_lift;
		*direction = vr_render.aim_forward;
	}
	else
	{
		hand_point(view->hand_position, origin);
		yaw_rotate(vr_render.base_yaw, view->aim_forward, direction);
	}
	return normalize3d(direction) > 0.0f;
}

/* how far along the aim's line it meets the world (a line-of-sight test,
past the player's own body and vehicle), within CROSSHAIR_NEAREST and
CROSSHAIR_FARTHEST */
static real aim_distance(real_point3d const *origin, real_vector3d const *direction)
{
	struct collision_result collision;
	real_vector3d vector;
	long player_index = local_player_get_player_index(0);
	long ignore_index = NONE;
	real distance = CROSSHAIR_FARTHEST;

	if (player_index != NONE && player_get(player_index)->unit_index != NONE)
		ignore_index = object_get_ultimate_parent(player_get(player_index)->unit_index);
	vector.i = direction->i * CROSSHAIR_FARTHEST;
	vector.j = direction->j * CROSSHAIR_FARTHEST;
	vector.k = direction->k * CROSSHAIR_FARTHEST;
	if (global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH)
	{
		global_current_collision_users[global_current_collision_user_depth++] = _collision_user_ui;
		if (collision_test_vector(_collision_test_for_line_of_sight_flags | FLAG(_collision_test_objects_bipeds_bit),
			(real_point3d *)origin, &vector, ignore_index, &collision))
		{
			distance = (real)sqrt(distance_squared3d(origin, &collision.point));
		}
		--global_current_collision_user_depth;
	}
	return distance < CROSSHAIR_NEAREST ? CROSSHAIR_NEAREST : distance;
}

/* vr_scope.c, before the eye pass: the camera the first-person weapon will
be posed from (vr_render_weapon_camera's, the controller's), each eye and
the head's forward in the world, and the world's units a metre; FALSE
unless the right controller aims the weapon on foot */
boolean vr_render_scope_view(struct render_camera *weapon_camera, real_point3d eyes[2], real_vector3d *head_forward,
	real *units)
{
	struct render_camera camera;
	boolean placed;

	if (!vr_render.active || !vr_render.view.controller_aim || !halo_vr_aiming() || cinematic_in_progress() ||
		vr_render.seated_unit_index != NONE)
	{
		return FALSE;
	}
	csmemset(&camera, 0, sizeof(camera));
	camera.position = vr_render.head_position;
	vr_render.unkicked = TRUE;
	placed = vr_render_weapon_camera(&camera);
	vr_render.unkicked = FALSE;
	if (!placed || !vr_render.weapon_camera_valid)
		return FALSE;
	*weapon_camera = camera;
	eyes[0] = vr_render.eye_positions[0];
	eyes[1] = vr_render.eye_positions[1];
	view_rotate(vr_render.base_yaw, view_pitch(), vr_render.view.head_forward, head_forward);
	*units = vr_render.view.world_units_per_metre;
	return TRUE;
}

/* vr_scope.c, before the eye pass, in a vehicle's seat seen from it
(vr.vehicle_view = "first_person"), the right controller aiming: each eye
and the head's forward in the world, and the world's units a metre; FALSE
else */
boolean vr_render_scope_seat_view(real_point3d eyes[2], real_vector3d *head_forward, real *units)
{
	if (!vr_render.active || !vr_render.view.controller_aim || !halo_vr_aiming() || cinematic_in_progress() ||
		vr_render.seated_unit_index == NONE)
	{
		return FALSE;
	}
	eyes[0] = vr_render.eye_positions[0];
	eyes[1] = vr_render.eye_positions[1];
	view_rotate(vr_render.base_yaw, view_pitch(), vr_render.view.head_forward, head_forward);
	*units = vr_render.view.world_units_per_metre;
	return TRUE;
}

/* vr_scope.c, before the eye pass: where the reticle is, the aim meeting
the world (as vr_render_crosshairs draws it, at its smoothed distance from
the frame before, else the world's now), and the aim's line it is on (from
the right controller along the aim); FALSE without the controller's aim */
boolean vr_render_scope_aim(real_point3d *point, real_point3d *origin, real_vector3d *direction)
{
	real distance;

	if (!vr_render.active || !vr_render.view.controller_aim || !aim_line(origin, direction))
		return FALSE;
	distance = vr_render.crosshair_distance_valid && vr_render.crosshair_inverse_distance > 0.0f ?
		1.0f / vr_render.crosshair_inverse_distance : aim_distance(origin, direction);
	point->x = origin->x + direction->i * distance;
	point->y = origin->y + direction->j * distance;
	point->z = origin->z + direction->k * distance;
	return TRUE;
}

/* vr_scope.c: how far the aim's line meets the world now, from the right
controller (metres; 0: nothing within CROSSHAIR_FARTHEST) */
real vr_render_scope_range(void)
{
	real_point3d origin;
	real_vector3d direction;
	real distance;

	if (!vr_render.active || !vr_render.view.controller_aim || vr_render.view.world_units_per_metre <= 0.0f ||
		!aim_line(&origin, &direction))
	{
		return 0.0f;
	}
	distance = aim_distance(&origin, &direction);
	return distance >= CROSSHAIR_FARTHEST ? 0.0f : distance / vr_render.view.world_units_per_metre;
}

/* vr_seat.c: the way the head looks this frame, in the world (its yaw:
the play space's turn and the head's own in it) */
real vr_render_view_yaw(void)
{
	return vr_render.view.body_yaw + (real)atan2(vr_render.view.head_forward[1], vr_render.view.head_forward[0]);
}

/* vr_scope.c: the time from the last frame's display to this one's
(seconds) */
real vr_render_frame_seconds(void)
{
	return vr_render.view.seconds;
}

/* vr_aim.c, as the player's control aims, before this frame is drawn: the
right controller's line as vr.c aims along it now (halo_vr_aim_line: game
axes from the recentred head, metres, and the stick's turn) in the world,
from the player's eye as the last frame drew it (the eye is the game's at
the latest tick: no tick has passed since that frame was drawn); how far
along it the world is met, as the reticle is drawn there (aim_distance, not
the reticle's smoothed distance: what the line is on now); and the world's
units a metre. FALSE before a frame is drawn aiming with this unit, or
after a tick has passed (a map ended, a revert). */
boolean vr_render_aim_hit(const float position[3], const float forward[3], real yaw, real_point3d *origin,
	real_vector3d *direction, real *distance, real *units)
{
	long player_index = local_player_get_player_index(0);
	real_vector3d offset;

	if (!vr_render.aim_base_valid || player_index == NONE ||
		player_get(player_index)->unit_index != vr_render.aim_base_unit_index ||
		game_time_get() < vr_render.aim_base_tick || game_time_get() > vr_render.aim_base_tick + 1 ||
		vr_render.view.world_units_per_metre <= 0.0f)
	{
		return FALSE;
	}
	*units = vr_render.view.world_units_per_metre;
	yaw_rotate(yaw, position, &offset);
	origin->x = vr_render.base_position.x + offset.i * *units;
	origin->y = vr_render.base_position.y + offset.j * *units;
	origin->z = vr_render.base_position.z + offset.k * *units;
	yaw_rotate(yaw, forward, direction);
	if (normalize3d(direction) <= 0.0f)
		return FALSE;
	*distance = aim_distance(origin, direction);
	return TRUE;
}

/* vr_feel.c: a direction in the world as the recentred head's game axes,
while the VR mode aims; FALSE else */
boolean vr_render_world_direction_to_head(real_vector3d const *direction, float head[3])
{
	float d[3];
	real_vector3d turned;

	if (!vr_render.aim_base_valid)
		return FALSE;
	d[0] = direction->i;
	d[1] = direction->j;
	d[2] = direction->k;
	yaw_rotate(-vr_render.base_yaw, d, &turned);
	head[0] = turned.i;
	head[1] = turned.j;
	head[2] = turned.k;
	return TRUE;
}

/* vr_scope.c: a point and two directions in the world as the recentred
head's game axes (metres; hand_point's inverse), for vr.c's layers */
void vr_render_world_to_head(real_point3d const *point, real_vector3d const *forward, real_vector3d const *up,
	float position[3], float head_forward[3], float head_up[3])
{
	float offset[3], f[3], u[3];
	real_vector3d turned;
	real units = vr_render.view.world_units_per_metre;

	offset[0] = (float)((point->x - vr_render.base_position.x) / units);
	offset[1] = (float)((point->y - vr_render.base_position.y) / units);
	offset[2] = (float)((point->z - vr_render.base_position.z) / units);
	f[0] = forward->i;
	f[1] = forward->j;
	f[2] = forward->k;
	u[0] = up->i;
	u[1] = up->j;
	u[2] = up->k;
	yaw_rotate(-vr_render.base_yaw, offset, &turned);
	position[0] = turned.i;
	position[1] = turned.j;
	position[2] = turned.k;
	yaw_rotate(-vr_render.base_yaw, f, &turned);
	head_forward[0] = turned.i;
	head_forward[1] = turned.j;
	head_forward[2] = turned.k;
	yaw_rotate(-vr_render.base_yaw, u, &turned);
	head_up[0] = turned.i;
	head_up[1] = turned.j;
	head_up[2] = turned.k;
}

/* render.c: the scope's pass begins (before the eye pass) and ends: in
between, what the eye pass moves to the eyes is drawn from the scope's
camera as it is */
void vr_render_scope_begin(void)
{
	vr_render.active = FALSE;
	vr_render.scope_pass = TRUE;
}

void vr_render_scope_end(void)
{
	vr_render.scope_pass = FALSE;
	vr_render.infinite = FALSE;
	vr_render.projection_valid = FALSE;
	vr_render.active = TRUE;
	vr_render_screen_point_end();
}

/* first_person_weapons.c: the scope's pass draws no first-person weapon
(the view is out of its scope); the eye pass keeps it while zoomed (the eyes
are not magnified) */
boolean vr_render_scope_pass(void)
{
	return vr_render.scope_pass;
}

boolean vr_render_eye_pass(void)
{
	return vr_render.active;
}

/* the body's heading: halo_vr_view's (the head's, slowly, not the aim's),
else the head's own (game axes from the recentred head, radians) */
static real body_heading(void)
{
	struct halo_vr_view const *view = &vr_render.view;

	return view->body_heading_valid ? view->body_heading : (real)atan2(view->head_forward[1], view->head_forward[0]);
}

/* vr_hands.c: the frame the hands are posed in (vr.hands), when the weapon
was posed for the eyes (vr_render_weapon_camera); FALSE when the hands are
the game's */
/* first_person_weapons.c: the hands on a vehicle's handlebars are drawn
from its seat, whose camera is the game's following the vehicle, without the
weapon (vr_vehicle.c); and so in any such seat the player's body is drawn
in (first_person_legs.c), its arms the VR mode's, the hands where the
controllers are (the Scorpion's driver's, a Banshee's pilot's) */
boolean vr_render_handlebar_hands(void)
{
	extern boolean first_person_legs_seated(long object_index);

	return vr_render.active && vr_render.seated_unit_index != NONE &&
		(vr_render.view.handlebars ||
		(vr_render.view.controller_aim && director_get_perspective(0) != _director_perspective_first_person &&
		first_person_legs_seated(vr_render.seated_unit_index)));
}

boolean vr_render_hands(struct vr_hands_frame *frame)
{
	struct halo_vr_view *view = &vr_render.view;
	struct render_camera const *camera = &vr_render.weapon_camera;

	if (!vr_render.active || !vr_render.weapon_camera_set || !view->floating_hands)
		return FALSE;
	frame->arms = view->arms;
	matrix4x3_from_point_and_vectors(&frame->camera, &camera->position, &camera->forward, &camera->up);
	frame->right_valid = vr_render.weapon_camera_valid;
	if (frame->right_valid)
	{
		real_point3d position;

		hand_point(view->right_grip_position, &position);
		matrix4x3_from_point_and_vectors(&frame->right, &position, &camera->forward, &camera->up);
	}
	frame->left_valid = view->left_hand_valid;
	if (frame->left_valid)
	{
		real_vector3d forward, up;
		real_point3d position;

		yaw_rotate(vr_render.base_yaw, view->left_hand_forward, &forward);
		yaw_rotate(vr_render.base_yaw, view->left_hand_up, &up);
		normalize3d(&forward);
		normalize3d(&up);
		hand_point(view->left_grip_position, &position);
		matrix4x3_from_point_and_vectors(&frame->left, &position, &forward, &up);
	}
	frame->two_handed = view->two_handed;
	frame->right_trigger = view->right_trigger;
	frame->right_trigger_touch = view->right_trigger_touch;
	frame->left_grip = view->left_grip;
	frame->base_yaw = vr_render.base_yaw;
	frame->body_yaw = body_heading();
	/* (the eyes as they would be with the head level and turned with the
	body, from the top of the neck: the shoulders stay put as the head nods
	and turns, over the body's, first_person_legs.c) */
	if (view->body_heading_valid)
		hand_point(view->body_eyes, &frame->head);
	else
		frame->head = vr_render.head_position;
	frame->shoulder[0] = view->shoulder_offset[0];
	frame->shoulder[1] = view->shoulder_offset[1];
	frame->shoulder[2] = view->shoulder_offset[2];
	frame->units = view->world_units_per_metre;
	return TRUE;
}

/* vr_grenade.c: the left controller in the world, during the eye pass:
its grip's position, turned as it points (as vr_render_hands has it), and
the world's units a metre; FALSE when it is not known */
boolean vr_render_left_hand(real_matrix4x3 *hand, real *units)
{
	struct halo_vr_view *view = &vr_render.view;
	real_vector3d forward, up;
	real_point3d position;

	if (!vr_render.active || !view->left_hand_valid || !view->controller_aim || !halo_vr_aiming())
		return FALSE;
	yaw_rotate(vr_render.base_yaw, view->left_hand_forward, &forward);
	yaw_rotate(vr_render.base_yaw, view->left_hand_up, &up);
	normalize3d(&forward);
	normalize3d(&up);
	hand_point(view->left_grip_position, &position);
	matrix4x3_from_point_and_vectors(hand, &position, &forward, &up);
	*units = view->world_units_per_metre;
	return TRUE;
}

/* vr_reload.c: the right controller in the world, during the eye pass, as
the weapon is posed in it: its grip's position (with the hands the game's,
where it points from), turned as the weapon points; and the world's units
a metre. FALSE when the weapon is not posed in it */
boolean vr_render_right_hand(real_matrix4x3 *hand, real *units)
{
	struct halo_vr_view *view = &vr_render.view;
	struct render_camera const *camera = &vr_render.weapon_camera;
	real_point3d position;

	if (!vr_render.active || !view->controller_aim || view->gamepad_aim || !vr_render.weapon_camera_valid ||
		!halo_vr_aiming())
	{
		return FALSE;
	}
	hand_point(view->floating_hands ? view->right_grip_position : view->hand_position, &position);
	matrix4x3_from_point_and_vectors(hand, &position, &camera->forward, &camera->up);
	*units = view->world_units_per_metre;
	return TRUE;
}

/* vr_holster.c: a place on the body (game axes from the recentred head,
metres) with directions there, in the world, during the eye pass with the
controller aiming; and the world's units a metre. FALSE otherwise. A
left-hander's (halo_vr_left_handed), whose places on the body are on the
other side (vr.c's), has the directions, a right-hander's, mirrored across
the body, and the matrix too (left-handed: its left the mirror's): a model
posed from its forward and up alone is turned as the mirror has it, one
from the matrix mirrored */
boolean vr_render_body_place(const float place[3], const float forward[3], const float up[3], real_matrix4x3 *matrix,
	real *units)
{
	real_vector3d f, u;
	real_point3d position;
	float mirrored[2][3];

	if (!vr_render.active || !vr_render.view.controller_aim || !halo_vr_aiming())
		return FALSE;
	if (halo_vr_left_handed())
	{
		/* (across the body: its heading's left) */
		real heading = body_heading();
		float across[3] = { -(float)sin(heading), (float)cos(heading), 0.0f };
		float along_forward = forward[0] * across[0] + forward[1] * across[1];
		float along_up = up[0] * across[0] + up[1] * across[1];
		short axis;

		for (axis = 0; axis < 3; axis++)
		{
			mirrored[0][axis] = forward[axis] - 2.0f * along_forward * across[axis];
			mirrored[1][axis] = up[axis] - 2.0f * along_up * across[axis];
		}
		forward = mirrored[0];
		up = mirrored[1];
	}
	yaw_rotate(vr_render.base_yaw, forward, &f);
	yaw_rotate(vr_render.base_yaw, up, &u);
	if (normalize3d(&f) <= 0.0f || normalize3d(&u) <= 0.0f)
		return FALSE;
	hand_point(place, &position);
	matrix4x3_from_point_and_vectors(matrix, &position, &f, &u);
	if (halo_vr_left_handed())
		negate_vector3d(&matrix->left, &matrix->left);
	*units = vr_render.view.world_units_per_metre;
	return TRUE;
}

/* vr_flip.c: a point of the world as a place from the recentred head (game
axes, metres), where the last eye pass had the head; FALSE before any */
boolean vr_render_world_place(real_point3d const *point, float place[3])
{
	real units = vr_render.view.world_units_per_metre;
	real_vector3d offset;
	real c = (real)cos(vr_render.base_yaw), s = (real)sin(vr_render.base_yaw);

	if (units <= 0.0f)
		return FALSE;
	vector_from_points3d(&vr_render.base_position, point, &offset);
	place[0] = (offset.i * c + offset.j * s) / units;
	place[1] = (-offset.i * s + offset.j * c) / units;
	place[2] = offset.k / units;
	return TRUE;
}

/* first_person_legs.c: where the player's body is drawn in the eye pass,
under the real head rather than the game's eye: the top of the neck
(halo_vr_view's body_neck) in the world, and the body's heading there;
FALSE: the game's place (no eye pass, a cinematic, the aim not the VR
mode's) */
boolean vr_render_body_anchor(real_point3d *neck, real *yaw)
{
	struct halo_vr_view const *view = &vr_render.view;

	if (!vr_render.active || !view->body_heading_valid || !halo_vr_aiming() || cinematic_in_progress())
		return FALSE;
	hand_point(view->body_neck, neck);
	*yaw = vr_render.base_yaw + body_heading();
	return TRUE;
}

/* first_person_legs.c: the player's body is drawn in the vehicle's seat it
is in, viewed from it (object_index, the seated unit: vr.vehicle_view =
"first_person"), and how (seat): once the eye is in the seat (vr_seat.c,
in a feed cockpit the display woken) and the body seated, not climbing in
or out (by the game's animation, or the hands: vr_climb.c); standing in the
Scorpion's hatch (vr_cabin.c). Not in the scope's pass, nor a cinematic */
boolean vr_render_body_seat(long object_index, struct vr_body_seat *seat)
{
	extern boolean vr_seat_in(void);
	extern boolean vr_climb_active(void);
	extern boolean vr_cabin_standing(long unit_index);
	struct halo_vr_view const *view = &vr_render.view;
	struct unit_datum *unit;
	real_matrix4x3 const *root;
	real units = view->world_units_per_metre;
	real heading;

	if (!vr_render.active || object_index == NONE || object_index != vr_render.seated_unit_index ||
		!view->body_heading_valid || !halo_vr_aiming() || cinematic_in_progress() || !vr_seat_in() || vr_climb_active())
	{
		return FALSE;
	}
	unit = unit_get(object_index);
	if (unit->unit.animation.state == _unit_state_entering_seat || unit->unit.animation.state == _unit_state_exiting_seat)
		return FALSE;
	root = object_get_node_matrix(unit->object.parent_object_index, 0);
	heading = (real)atan2(root->forward.j, root->forward.i);
	seat->pose_forward.i = (real)cos(heading);
	seat->pose_forward.j = (real)sin(heading);
	seat->pose_forward.k = 0.0f;
	seat->standing = vr_cabin_standing(object_index);
	if (seat->standing)
		heading = vr_render.base_yaw + body_heading();
	seat->forward.i = (real)cos(heading);
	seat->forward.j = (real)sin(heading);
	seat->forward.k = 0.0f;
	seat->left.i = -seat->forward.j;
	seat->left.j = seat->forward.i;
	seat->left.k = 0.0f;
	/* (the top of the neck under the eye, as vr.c's body_place has it with
	the head level, facing the way the body does) */
	seat->rest_neck.x = vr_render.base_position.x - seat->forward.i * BODY_NECK_BACK * units;
	seat->rest_neck.y = vr_render.base_position.y - seat->forward.j * BODY_NECK_BACK * units;
	seat->rest_neck.z = vr_render.base_position.z - BODY_NECK_DOWN * units;
	/* (the VR mode's arms: the hands alone, where the controllers are, or
	the first-person weapon's in a seat with the first person's camera) */
	seat->arms = view->controller_aim || director_get_perspective(0) == _director_perspective_first_person;
	return TRUE;
}

/* first_person_legs.c: each eye in the world during the eye pass, and what
it sees (its frustum), how near the eyes' view begins (world units: what is
nearer is cut away), and the time from the last frame's display to this
one's (seconds); FALSE as vr_render_body_anchor */
boolean vr_render_body_eyes(real_point3d eyes[2], struct render_frustum const *views[2], real *near, real *seconds)
{
	if (!vr_render.active || !vr_render.view.body_heading_valid || !halo_vr_aiming() || cinematic_in_progress())
		return FALSE;
	eyes[0] = vr_render.eye_positions[0];
	eyes[1] = vr_render.eye_positions[1];
	views[0] = &vr_render.eyes[0];
	views[1] = &vr_render.eyes[1];
	*near = MAX(vr_render.eyes[0].z_near, vr_render.eyes[1].z_near);
	*seconds = vr_render.view.seconds;
	return TRUE;
}

/* player_control.c: the weapon is a long gun, which the left hand may hold
by its foregrip too (vr.two_handed): any but the pistols, the needler, the
flag and the ball */
boolean vr_render_long_gun(long weapon_index)
{
	static char const *const one_handed[] = { "pistol", "needler", "flag", "ball" };
	char const *name;
	short index;

	if (weapon_index == NONE)
		return FALSE;
	name = tag_get_name(object_get(weapon_index)->definition_index);
	if (!name)
		return FALSE;
	for (index = 0; index < (short)NUMBEROF(one_handed); index++)
	{
		if (strstr(name, one_handed[index]))
			return FALSE;
	}
	return TRUE;
}

/* the eye pass is over: the projections set from now on are no eye's */
void vr_render_end(void)
{
	vr_render.active = FALSE;
	structure_culling_set_camera_radius(0.0f);
	vr_render.seated_unit_index = NONE;
	vr_render.infinite = FALSE;
	vr_render_screen_point_end();
}

/* the object is the player's unit, which the eye pass views its vehicle
from the head of: not drawn, as a first-person player's is not
(render_objects.c); in the scope's pass from a seat (vr_scope.c), its
vehicle neither (with its cabin: vr_cabin.c), which the sight looks out of
from the controller, inside the Scorpion's compartment or its turret */
boolean vr_render_hides_object(long object_index)
{
	extern boolean vr_cabin_hides_attached(long object_index);

	/* (in a feed cockpit, what is attached to its vehicle: vr_cabin.c) */
	if (vr_render.active && vr_cabin_hides_attached(object_index))
		return TRUE;
	if (vr_render.scope_pass && vr_render.seated_unit_index != NONE)
	{
		return object_index == vr_render.seated_unit_index ||
			object_index == object_get(vr_render.seated_unit_index)->object.parent_object_index;
	}
	return vr_render.active && object_index == vr_render.seated_unit_index;
}

/* hud_unit.c's: the script hides the shields' meter */
#define HUD_SHIELD_HIDDEN_BIT 2

/* the HUD on the visor, while its pass draws (vr_render_hud_camera to
vr_render_hud_end): the half angles it spans (radians) */
static struct
{
	boolean curved;
	real half_angles[2];
} vr_hud;

/* Preparation needs the ordinary HUD camera state, then returns to the
same eye pose without updating its animation or controller state again. */
void vr_render_prepare_hud(boolean begin)
{
	static unsigned char saved_render[sizeof(vr_render)];
	static unsigned char saved_hud[sizeof(vr_hud)];
	static short saved_crosshair[2];

	if (begin)
	{
		memcpy(saved_render, &vr_render, sizeof(vr_render));
		memcpy(saved_hud, &vr_hud, sizeof(vr_hud));
		memcpy(saved_crosshair, vr_render_crosshair_offset, sizeof(saved_crosshair));
		vr_render_end();
	}
	else
	{
		memcpy(&vr_render, saved_render, sizeof(vr_render));
		memcpy(&vr_hud, saved_hud, sizeof(vr_hud));
		memcpy(vr_render_crosshair_offset, saved_crosshair, sizeof(saved_crosshair));
	}
}

/* the shader type of a plasma (shader_transparent_plasma: the shields'
glow on an armour), and its fields after the shader's own
(rasterizer_xbox_plasma_energy.c reads them so) */
#define SHADER_TYPE_PLASMA 10
struct plasma_fields
{
	byte reserved00[4];
	short intensity_exponent_source;
	short pad06;
	real intensity_exponent;
	byte reserved0c[0x2C];
	real perpendicular_alpha;
	real_rgb_color perpendicular_color;
	real parallel_alpha;
	real_rgb_color parallel_color;
	byte reserved68[0x40];
	real primary_period;
	real_vector3d primary_direction;
	real primary_scale;
	byte reservedac[0xC];
	long primary_noise_map;
	byte reservedbc[0x24];
	real secondary_period;
	real_vector3d secondary_direction;
	real secondary_scale;
	byte reservedf4[0xC];
	long secondary_noise_map;
};

typedef char plasma_fields_perpendicular_offset_assert[
	offsetof(struct plasma_fields, perpendicular_alpha) == 0x38 ? 1 : -1];
typedef char plasma_fields_primary_offset_assert[
	offsetof(struct plasma_fields, primary_period) == 0x98 ? 1 : -1];
typedef char plasma_fields_primary_map_offset_assert[
	offsetof(struct plasma_fields, primary_noise_map) == 0xB8 ? 1 : -1];
typedef char plasma_fields_secondary_map_offset_assert[
	offsetof(struct plasma_fields, secondary_noise_map) == 0x100 ? 1 : -1];

/* a plasma's noise map's texture (loaded as the game's draws load it) */
static void *plasma_noise_texture(long bitmap_group_index)
{
	struct bitmap_data *bitmap;

	if (bitmap_group_index == NONE)
		return NULL;
	bitmap = bitmap_group_try_and_get_bitmap(bitmap_group_index, 0);
	return bitmap ? _texture_cache_bitmap_get_hardware_format(bitmap, TRUE, TRUE) : NULL;
}

/* the shields' look on the unit's armour: its modifier shader (the Chief's
characters\cyborg\shaders\shield hit), a plasma whose intensity is one
of the unit's functions (the Chief's B, its 'shield glow source', from its
recent shield damage), as rasterizer_xbox_plasma_energy.c draws it */
static void visor_shield_look(struct unit_datum *unit, struct halo_vr_visor_state *state)
{
	struct object_definition *definition = object_definition_get(unit->definition_index);
	struct shader *shader;
	struct plasma_fields const *plasma;
	short source;

	if (definition->object.modifier_shader.index == NONE)
		return;
	shader = shader_definition_get(definition->object.modifier_shader.index);
	if (shader->base.type != SHADER_TYPE_PLASMA)
		return;
	plasma = (struct plasma_fields const *)((byte const *)shader + sizeof(struct shader));
	if (plasma->primary_period == 0.0f || plasma->secondary_period == 0.0f)
		return;
	state->noise_texture[0] = plasma_noise_texture(plasma->primary_noise_map);
	state->noise_texture[1] = plasma_noise_texture(plasma->secondary_noise_map);
	if (!state->noise_texture[0] || !state->noise_texture[1])
		return;
	state->look = TRUE;
	source = plasma->intensity_exponent_source;
	if (source >= 1 && source <= 4)
	{
		real value = unit->object.outgoing_function_values[source - 1];

		state->glow = value > 0.0f ? (float)pow(value, plasma->intensity_exponent) * plasma->perpendicular_alpha : 0.0f;
	}
	state->perpendicular[0] = plasma->perpendicular_color.red;
	state->perpendicular[1] = plasma->perpendicular_color.green;
	state->perpendicular[2] = plasma->perpendicular_color.blue;
	state->parallel[0] = plasma->parallel_color.red;
	state->parallel[1] = plasma->parallel_color.green;
	state->parallel[2] = plasma->parallel_color.blue;
	state->noise_scale[0] = plasma->primary_scale;
	state->noise_scale[1] = plasma->secondary_scale;
	state->noise_period[0] = plasma->primary_period;
	state->noise_period[1] = plasma->secondary_period;
	state->noise_direction[0][0] = plasma->primary_direction.i;
	state->noise_direction[0][1] = plasma->primary_direction.j;
	state->noise_direction[0][2] = plasma->primary_direction.k;
	state->noise_direction[1][0] = plasma->secondary_direction.i;
	state->noise_direction[1][1] = plasma->secondary_direction.j;
	state->noise_direction[1][2] = plasma->secondary_direction.k;
	state->time = global_frame_parameters.game_time_sec;
}

/* the player as the visor shows it (port/linux/src/vr_visor.c): its unit's
shields and health, as the HUD's meters have them, and their look on its
armour, while it plays and the HUD is up */
static void visor_state_tell(void)
{
	struct halo_vr_visor_state state;
	long player_index = local_player_get_player_index(0);
	struct unit_datum *unit = NULL;

	memset(&state, 0, sizeof(state));
	if (player_index != NONE && !cinematic_in_progress() && hud_scripted_globals && hud_scripted_globals->show_hud)
		unit = unit_try_and_get(player_get(player_index)->unit_index);
	state.dead = vr_death_viewing();
	if (unit && unit->object.body_vitality > 0.0f && !TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
	{
		state.active = TRUE;
		state.has_shield = game_engine_has_shield(player_index) &&
			!TEST_FLAG(hud_unit_port_script_flags(), HUD_SHIELD_HIDDEN_BIT);
		state.shield = unit->object.shield_vitality;
		state.charging = TEST_FLAG(unit->object.damage_flags, _object_shield_charging_bit);
		state.body = unit->object.body_vitality;
		visor_shield_look(unit, &state);
	}
	halo_vr_visor_state(&state);
}

/* render_cameras.c: a point before the HUD's camera (its view space: x
right, y up, -z ahead) in its screen, while the HUD is on the visor: by its
angles across and up, as the HUD's image is laid out (vr_visor.c), not
through the camera's plane; FALSE leaves it to the camera */
boolean vr_render_hud_to_screen(struct render_camera const *camera, real_point3d const *view_point,
	real_point2d *screen_point, boolean *visible)
{
	real across, up, x, y;

	if (!vr_hud.curved)
		return FALSE;
	*visible = FALSE;
	if (view_point->z >= 0.0f)
		return TRUE;
	across = (real)atan2(view_point->x, -view_point->z);
	up = (real)atan2(view_point->y, sqrt(view_point->x * view_point->x + view_point->z * view_point->z));
	x = across / vr_hud.half_angles[0];
	y = -up / vr_hud.half_angles[1];
	if (x >= -1.0f && x <= 1.0f && y >= -1.0f && y <= 1.0f)
	{
		screen_point->x = (real)(camera->viewport_bounds.x1 - camera->viewport_bounds.x0) * ((x + 1.0f) * 0.5f) +
			camera->viewport_bounds.x0;
		screen_point->y = (real)(camera->viewport_bounds.y1 - camera->viewport_bounds.y0) * ((y + 1.0f) * 0.5f) +
			camera->viewport_bounds.y0;
		*visible = TRUE;
	}
	else
	{
		screen_point->x = x;
		screen_point->y = y;
	}
	return TRUE;
}

/* render.c: the HUD's pass is over */
void vr_render_hud_end(void)
{
	vr_hud.curved = FALSE;
	vr_render.marks_drawn = FALSE;
}

/* the camera the HUD is drawn from (in place of the window's): where the
head looks (on the visor, where the HUD does), with the HUD layer's field of
view; FALSE without a VR frame */
boolean vr_render_hud_camera(struct render_camera *camera)
{
	struct halo_vr_view *view = &vr_render.view;
	real base_yaw;
	real turn;

	vr_hud.curved = FALSE;
	if (!halo_vr_frame_active())
		return FALSE;
	visor_state_tell();
	base_yaw = cinematic_in_progress() || !halo_vr_aiming() ?
		(real)atan2(camera->forward.j, camera->forward.i) : view->body_yaw;
	if (vr_death_turn(&turn))
		base_yaw = view->body_yaw + turn;
	vr_render_crosshair_offset[0] = vr_render_crosshair_offset[1] = 0;
	if (vr_render.crosshairs_drawn)
	{
		/* (drawn in the eyes: vr_render_crosshairs) */
		vr_render_crosshair_offset[0] = 10000;
		vr_render.crosshairs_drawn = FALSE;
	}
	if (view->hud_tangent > 0.0f)
	{
		real pitch = view_pitch();

		view_rotate(base_yaw, pitch, view->hud_forward, &camera->forward);
		view_rotate(base_yaw, pitch, view->hud_up, &camera->up);
		camera->vertical_field_of_view = 2.0f * (real)atan(view->hud_tangent);
		vr_hud.curved = view->hud_visor;
		vr_hud.half_angles[0] = view->hud_half_angles[0];
		vr_hud.half_angles[1] = view->hud_half_angles[1];
		if ((view->controller_aim || view->gamepad_aim || view->hud_visor) && halo_vr_aiming() &&
			!vr_render_crosshair_offset[0])
		{
			/* a point far along the controller (or the player's facing, where
			the gamepad aims; or where the head looks, which the HUD on the
			visor trails), where the HUD shows it */
			struct render_frustum frustum;
			real_vector3d forward;
			real_point3d point;
			real_point2d screen;

			render_camera_build_frustum(camera, NULL, &frustum, TRUE);
			if (view->gamepad_aim)
				forward = vr_render.aim_forward;
			else if (view->controller_aim)
				yaw_rotate(base_yaw, view->aim_forward, &forward);
			else
				view_rotate(base_yaw, pitch, view->head_forward, &forward);
			point.x = camera->position.x + forward.i * 100.0f;
			point.y = camera->position.y + forward.j * 100.0f;
			point.z = camera->position.z + forward.k * 100.0f;
			if (render_camera_world_to_screen(camera, &frustum, &point, &screen))
			{
				vr_render_crosshair_offset[0] = (short)(screen.x - (camera->viewport_bounds.x0 + camera->viewport_bounds.x1) * 0.5f);
				vr_render_crosshair_offset[1] = (short)(screen.y - (camera->viewport_bounds.y0 + camera->viewport_bounds.y1) * 0.5f);
			}
			else
			{
				/* (off the HUD: not drawn) */
				vr_render_crosshair_offset[0] = 10000;
			}
		}
	}
	return TRUE;
}

/* render.c, at the end of the eye pass: the crosshairs, drawn in each eye
where the aim meets the world (vr.aim = "gamepad": the facing from the
player's eye; "controller": the right controller's line), as the
first-person weapon is, and as large as in the HUD's layer. In that layer,
in front of the head, they were placed for the head's pose as the frame was
drawn, which the compositor moved them with to the pose shown while it
moved the eyes' images to it, so they swam against the world as the head
moved, and at the layer's distance the eyes saw them doubled against what
they aimed at. Drawn in the screen, about the middle as the game draws
them, then moved to the point in each eye by a scale and offset (no
rounding to the HUD's pixels). */
void vr_render_crosshairs(void)
{
	struct halo_vr_view *view = &vr_render.view;
	real_point3d origin, point;
	real_vector3d direction;
	long player_index = local_player_get_player_index(0);
	real distance, width, height, aspect, centre_x, centre_y;
	float corrections[2][4];
	short eye, i;

	if (!vr_render.active || !halo_vr_aiming() || cinematic_in_progress() || view->hud_tangent <= 0.0f ||
		!(view->gamepad_aim || view->controller_aim) || player_index == NONE)
	{
		vr_render.crosshair_distance_valid = FALSE;
		return;
	}
	if (vr_scope_raised())
	{
		/* the scope at the eye: its reticle is the aim (vr_scope.c), none
		in the world nor in the HUD's layer */
		vr_render.crosshair_distance_valid = FALSE;
		vr_render.crosshairs_drawn = TRUE;
		return;
	}
	if (!aim_line(&origin, &direction))
		return;
	distance = aim_distance(&origin, &direction);
	if (!vr_render.crosshair_distance_valid)
	{
		vr_render.crosshair_inverse_distance = 1.0f / distance;
		vr_render.crosshair_distance_valid = TRUE;
	}
	else
	{
		real t = view->seconds > 0.0f ? 1.0f - (real)exp(-view->seconds / CROSSHAIR_SMOOTHING) : 1.0f;

		vr_render.crosshair_inverse_distance += (1.0f / distance - vr_render.crosshair_inverse_distance) * t;
	}
	distance = 1.0f / vr_render.crosshair_inverse_distance;
	point.x = origin.x + direction.i * distance;
	point.y = origin.y + direction.j * distance;
	point.z = origin.z + direction.k * distance;

	/* the game's screen (the viewport, its pixels: x = ((ndc + 1) width -
	1) / 2, y from the top), and its middle, where the game draws them */
	width = (real)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	height = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	if (width <= 0.0f || height <= 0.0f)
		return;
	aspect = width / height;
	centre_x = (real)(short)((render.camera.window_bounds.x1 + render.camera.window_bounds.x0) / 2) -
		render.camera.viewport_bounds.x0;
	centre_y = (real)(short)((render.camera.window_bounds.y1 + render.camera.window_bounds.y0) / 2) -
		render.camera.viewport_bounds.y0;
	for (eye = 0; eye < 2; eye++)
	{
		double m[4][4];
		real clip[4], screen_x, screen_y, scale_x, scale_y;
		const float *fov = view->fov[eye];

		frustum_clip_transform(&vr_render.eyes[eye], m);
		for (i = 0; i < 4; i++)
			clip[i] = (real)(m[i][0] * point.x + m[i][1] * point.y + m[i][2] * point.z + m[i][3]);
		if (clip[3] <= 0.0f || fov[1] <= fov[0] || fov[2] <= fov[3])
			return;
		screen_x = ((clip[0] / clip[3] + 1.0f) * width - 1.0f) * 0.5f;
		screen_y = ((1.0f - clip[1] / clip[3]) * height - 1.0f) * 0.5f;
		/* the HUD's layer has height / (2 hud_tangent) pixels a unit of
		tangent, an eye width / (right - left) across and height / (up -
		down) up */
		scale_x = 2.0f * view->hud_tangent * aspect / (fov[1] - fov[0]);
		scale_y = 2.0f * view->hud_tangent / (fov[2] - fov[3]);
		corrections[eye][0] = (float)scale_x;
		corrections[eye][1] = (float)scale_y;
		corrections[eye][2] = (float)(screen_x - scale_x * centre_x);
		corrections[eye][3] = (float)(screen_y - scale_y * centre_y);
	}
	vr_render_crosshair_offset[0] = vr_render_crosshair_offset[1] = 0;
	halo_vr_set_screen_corrections(corrections);
	/* (each eye in a draw of its own: halo_vr_draw_eye) */
	for (eye = 0; eye < 2; eye++)
	{
		halo_vr_draw_eye(eye);
		hud_draw_vr_crosshairs();
	}
	halo_vr_draw_eye(-1);
	vr_render_screen_point_end();
	vr_render.crosshairs_drawn = TRUE;
}

/* render.c, at the end of the eye pass, after the crosshairs: the marks the
HUD puts over the world, the triangles over the player's teammates, the
players' names above their heads (hud.c) and the waypoints on the screen
(hud_nav_points.c: the campaign's, a multiplayer game's flags, ball and
hill), drawn in each eye over what they mark, as large as in the HUD's
layer. In that layer (on the visor its world panel,
2.5 m before the head at the defaults: vr_visor.c) a teammate's triangle was
at the layer's distance, not the teammate's: looking at a teammate 15 m away, the eyes saw
it doubled, about a degree and a quarter apart, and it trailed the head's
turns with the layer and moved in steps of the HUD's pixels, while the
teammate under it moved smoothly with the world; it flickered over them. The
game places each mark itself, at a point of the screen it projects
(vr_render_world_mark), as it does in the HUD's pass; here each eye's screen
correction moves what it draws about that point to where the point is in
that eye (no rounding to the screen's pixels), at the HUD's size. Only where
the eyes are drawn (in a frame without them, they are in the layer as
before), and not in cinematics, as in the HUD's pass. */
void vr_render_world_marks(void)
{
	struct halo_vr_view *view = &vr_render.view;
	short eye;

	if (!vr_render.active || view->hud_tangent <= 0.0f)
		return;
	vr_render.marks_drawing = TRUE;
	vr_render.waypoint_count = 0;
	/* (each eye in a draw of its own, as the crosshairs: drawn in both at
	once, the right eye's triangles lost their tops) */
	for (eye = 0; eye < 2; eye++)
	{
		halo_vr_draw_eye(eye);
		hud_draw_vr_world_marks();
	}
	halo_vr_draw_eye(-1);
	vr_render.marks_drawing = FALSE;
	vr_render_screen_point_end();
	vr_render.marks_drawn = TRUE;
}

/* hud.c, in the HUD's pass: the marks over the world were drawn in the eyes
this frame, and are not drawn in the HUD's layer */
boolean vr_render_world_marks_drawn(void)
{
	return vr_render.marks_drawn;
}

/* hud.c: a mark over the world at this point (world units), which the game
draws about this point of its screen (the viewport's pixels, from its top
left), as it projected the point: in the eye pass, what it draws next moves
to the point in each eye, as large as in the HUD's layer. FALSE if the point
is behind an eye (nothing to draw); TRUE, and nothing done, in the HUD's
pass. */
boolean vr_render_world_mark(real_point3d const *point, real x, real y)
{
	struct halo_vr_view *view = &vr_render.view;
	float corrections[2][4];
	real width, height, aspect;
	short eye, i;

	if (!vr_render.marks_drawing)
		return TRUE;
	width = (real)(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0);
	height = (real)(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0);
	if (width <= 0.0f || height <= 0.0f)
		return FALSE;
	aspect = width / height;
	for (eye = 0; eye < 2; eye++)
	{
		double m[4][4];
		real clip[4], screen_x, screen_y, scale_x, scale_y;
		const float *fov = view->fov[eye];

		frustum_clip_transform(&vr_render.eyes[eye], m);
		for (i = 0; i < 4; i++)
			clip[i] = (real)(m[i][0] * point->x + m[i][1] * point->y + m[i][2] * point->z + m[i][3]);
		if (clip[3] <= 0.0f || fov[1] <= fov[0] || fov[2] <= fov[3])
			return FALSE;
		/* (the game's screen: x = ((ndc + 1) width - 1) / 2, y from the
		top; the HUD's layer's size, as the crosshairs') */
		screen_x = ((clip[0] / clip[3] + 1.0f) * width - 1.0f) * 0.5f;
		screen_y = ((1.0f - clip[1] / clip[3]) * height - 1.0f) * 0.5f;
		scale_x = 2.0f * view->hud_tangent * aspect / (fov[1] - fov[0]);
		scale_y = 2.0f * view->hud_tangent / (fov[2] - fov[3]);
		corrections[eye][0] = (float)scale_x;
		corrections[eye][1] = (float)scale_y;
		corrections[eye][2] = (float)(screen_x - scale_x * x);
		corrections[eye][3] = (float)(screen_y - scale_y * y);
	}
	halo_vr_set_screen_corrections(corrections);
	return TRUE;
}

/* hud_nav_points.c: a waypoint at this point, on the screen (else at its
edge, pointing to it) and drawn about this point of the game's screen (as
vr_render_world_mark): whether to draw it now. In the eye pass, those on
the screen, each over its point (vr_render_world_mark); in the HUD's pass,
the others, at the edge of the HUD as ever. */
boolean vr_render_world_waypoint(real_point3d const *point, boolean on_screen, real x, real y)
{
	short index;

	if (vr_render.marks_drawing)
	{
		if (!on_screen || vr_render.waypoint_count >= WORLD_WAYPOINTS_MAXIMUM)
			return FALSE;
		if (!vr_render_world_mark(point, x, y))
			return FALSE;
		/* (each eye's draw sees it again) */
		for (index = 0; index < vr_render.waypoint_count; index++)
		{
			if (!memcmp(&vr_render.waypoints[index], point, sizeof(*point)))
				return TRUE;
		}
		vr_render.waypoints[vr_render.waypoint_count++] = *point;
		return TRUE;
	}
	if (!vr_render.marks_drawn)
		return TRUE;
	for (index = 0; index < vr_render.waypoint_count; index++)
	{
		if (!memcmp(&vr_render.waypoints[index], point, sizeof(*point)))
			return FALSE;
	}
	return TRUE;
}

/* an eye's clip transform from its own space (OpenXR's axes: x right, y
up, z back; metres), row-major, for the late latch (halo_vr_eye_latch):
the frustum's clip transform (m) from the eye's place in the world. (The
view's axes are those too: render_camera_build_frustum puts the right, the
up and the back in view_to_world's forward, left and up.) */
static void eye_clip_from_eye(const struct render_frustum *frustum, double m[4][4], float out[16])
{
	const real_matrix4x3 *view = &frustum->view_to_world;
	double units = vr_render.view.world_units_per_metre, a[4][4];
	short i, j, k;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
			a[i][j] = (double)view->n[j][i] * units;
		a[i][3] = (double)view->n[3][i];
	}
	a[3][0] = a[3][1] = a[3][2] = 0.0;
	a[3][3] = 1.0;
	for (i = 0; i < 4; i++)
	{
		for (j = 0; j < 4; j++)
		{
			double sum = 0.0;

			for (k = 0; k < 4; k++)
				sum += m[i][k] * a[k][j];
			out[i * 4 + j] = (float)sum;
		}
	}
}

/* the eyes' transforms for the projection set last (vr_render.centre):
each eye's own (or, for the sky, the eye where the centre is, its view
turned the same way: the sky is at infinity).

An eye's depth (its clip z and w) is the centre's: the eyes are beside the
centre and look the same way, with the same depth range. So those rows are
exactly the identity's. Made from the matrices they were not: the inverse
of the centre's clip transform loses the precision that depth needs (the
near plane 0.0625, the far 1024), and its rounding, new each frame, moved
the depth of all that was far: some frames a few parts in ten thousand,
enough to put all beyond about 100 world units past the far plane (where
the horizon's water flickered, and, while the whole eye pass was
depth-clamped, all of it was at one depth and the last drawn won: the
Pillar of Autumn's hull in a10's opening went dark in single frames, its
far side drawn over its near). The eyes' x and y are made in double
precision. */
static void eye_transforms_set(void)
{
	float transforms[2][16];
	float clip_from_eye[2][16];
	double inverse[4][4];
	short eye, i, j, k;
	int latch = XGPU_STREAM_VR_LATCH_NONE;

	memset(transforms, 0, sizeof(transforms));
	memset(clip_from_eye, 0, sizeof(clip_from_eye));
	if (!vr_render.active || !vr_render.projection_valid || !matrix4_inverse(vr_render.centre, inverse))
	{
		for (eye = 0; eye < 2; eye++)
		{
			for (i = 0; i < 4; i++)
				transforms[eye][i * 5] = 1.0f;
		}
	}
	else
	{
		for (eye = 0; eye < 2; eye++)
		{
			struct render_frustum eye_frustum = vr_render.infinite ? vr_render.eyes_at_centre[eye] : vr_render.eyes[eye];
			double m[4][4];

			/* the depth range is the centre's as set (the first-person
			weapon's, the sky's): the eyes look the same way, so their depth is
			the same */
			for (k = 0; k < 4; k++)
				eye_frustum.projection_matrix[k][2] = vr_render.projection_depth[k];
			frustum_clip_transform(&eye_frustum, m);
			eye_clip_from_eye(&eye_frustum, m, clip_from_eye[eye]);
			for (i = 0; i < 2; i++)
			{
				for (j = 0; j < 4; j++)
				{
					double sum = 0.0;

					for (k = 0; k < 4; k++)
						sum += m[i][k] * inverse[k][j];
					transforms[eye][i * 4 + j] = (float)sum;
				}
			}
			/* (z and w: the centre's) */
			transforms[eye][2 * 4 + 2] = 1.0f;
			transforms[eye][3 * 4 + 3] = 1.0f;
		}
		/* what the draws move with in the late latch: the world with the
		head, the sky (about the eye) with its turn, the first-person weapon
		and the hands with the controller they are in, else with the head
		(where they are posed from it: nothing moves them), or the world
		(the gamepad's, posed from the player's eye) */
		latch = vr_render.infinite ? XGPU_STREAM_VR_LATCH_SKY : XGPU_STREAM_VR_LATCH_WORLD;
		if (vr_render.first_person)
			latch = vr_render.weapon_in_hand ? XGPU_STREAM_VR_LATCH_HELD :
				vr_render.weapon_camera_valid ? XGPU_STREAM_VR_LATCH_WORLD : XGPU_STREAM_VR_LATCH_NONE;
	}
	memcpy(vr_render.transforms, transforms, sizeof(transforms));
	halo_vr_eye_latch(latch, clip_from_eye);
	halo_vr_set_eye_transforms(transforms);
}

/* rasterizer_set_frustum_z: the game has set the projection with this clip
transform (centre), the first-person weapon's or not; the renderer gets
each eye's from it */
void vr_render_projection_set(const struct render_frustum *frustum, real centre[4][4], boolean first_person)
{
	short k;

	vr_render.projection_valid = vr_render.active;
	vr_render.first_person = first_person;
	if (vr_render.active)
	{
		/* the depth range the eyes' depth is submitted with (the world's:
		the camera's, which the first-person weapon's and the sky's change
		only in the matrix) */
		halo_vr_depth_range(frustum->z_near / vr_render.view.world_units_per_metre,
			frustum->z_far / vr_render.view.world_units_per_metre);
		memcpy(vr_render.centre, centre, sizeof(vr_render.centre));
		for (k = 0; k < 4; k++)
			vr_render.projection_depth[k] = frustum->projection_matrix[k][2];
	}
	eye_transforms_set();
}

/* render_sky.c: the sky is drawn about the camera, a thousandth of its size
(as near as a few metres): while it is, the eyes see it from the centre of
the head, as they would at infinity */
void vr_render_sky(boolean sky)
{
	if (vr_render.infinite != sky)
	{
		vr_render.infinite = sky;
		if (vr_render.active)
		{
			eye_transforms_set();
			halo_vr_sky(sky);
		}
	}
}

/* rasterizer_xbox_widgets.c: a point the game has projected to the screen
itself (a lens flare's, a sprite's), at this clip position of the centre
view (x, y, z, w), in a viewport this wide and high: what the game draws
about it in the screen moves to each eye as the point does, and scales as
the eye's view does there */
void vr_render_screen_point(const real clip[4], real width, real height)
{
	float corrections[2][4];
	short eye, axis, i;

	if (!vr_render.active || clip[3] <= 0.0f)
		return;
	for (eye = 0; eye < 2; eye++)
	{
		const float *t = vr_render.transforms[eye];
		real moved[4], scale[2], centre_screen[2], eye_screen[2];

		for (i = 0; i < 4; i++)
			moved[i] = t[i * 4] * clip[0] + t[i * 4 + 1] * clip[1] + t[i * 4 + 2] * clip[2] + t[i * 4 + 3] * clip[3];
		if (moved[3] <= 0.0f)
			return;
		for (axis = 0; axis < 2; axis++)
		{
			/* a step across the centre's screen, at the point's depth */
			const real step = 0.01f;
			real nudged = (moved[axis] + t[axis * 4 + axis] * step * clip[3]) / (moved[3] + t[12 + axis] * step * clip[3]);
			real centre_ndc = clip[axis] / clip[3], eye_ndc = moved[axis] / moved[3];

			scale[axis] = (nudged - eye_ndc) / step;
			/* (the game's screen: x = ((ndc + 1) width - 1) / 2, y from the
			top) */
			if (axis == 0)
			{
				centre_screen[0] = ((centre_ndc + 1.0f) * width - 1.0f) * 0.5f;
				eye_screen[0] = ((eye_ndc + 1.0f) * width - 1.0f) * 0.5f;
			}
			else
			{
				centre_screen[1] = ((1.0f - centre_ndc) * height - 1.0f) * 0.5f;
				eye_screen[1] = ((1.0f - eye_ndc) * height - 1.0f) * 0.5f;
			}
		}
		corrections[eye][0] = (float)scale[0];
		corrections[eye][1] = (float)scale[1];
		corrections[eye][2] = (float)(eye_screen[0] - scale[0] * centre_screen[0]);
		corrections[eye][3] = (float)(eye_screen[1] - scale[1] * centre_screen[1]);
	}
	halo_vr_screen_point(clip, width, height);
	halo_vr_set_screen_corrections(corrections);
}

/* whether what is drawn about a screen point moves to each eye as the point
does (vr_render_screen_point): then each such draw is its own */
boolean vr_render_screen_points_move(void)
{
	return vr_render.active;
}

/* the draws in the screen are the same in both eyes again */
void vr_render_screen_point_end(void)
{
	static const float none[2][4] = { { 1.0f, 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f, 0.0f } };

	halo_vr_set_screen_corrections(none);
}

#endif
