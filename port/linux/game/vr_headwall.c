/*
VR_HEADWALL.C

The VR mode's head inside a wall (HALO_VR; vr.head_in_wall,
port/linux/README.md "VR"), for vr_render.c, render.c and hud.c.

The head moves in the room apart from the player's unit: leaning or
stepping toward a wall or a pillar takes the eyes into the level's
geometry, or through it, which the game's own eye never is (the unit's
physics keeps it off the walls). From inside a wall the view sees through
it: the level's far side, the void about it, and what is beyond, which is
disorienting and an unfair peek.

With vr.head_in_wall = "fade" (off by default: a black view is worse than
seeing through the wall), each frame on foot the line
from the game's eye (where the game knows the player can be) to the eyes'
middle, and on by a few centimetres (the eyes' small sphere), is tested
against the world as the game tests a line of sight (the level's
structure, scenery and machines such as doors; not two-sided surfaces such
as grates, nor invisible ones, nor vehicles, which drive past the head and
whose hulls are not their models); and, from the eyes' middle, the world
ahead, where it comes nearer than the near plane (0.0625 world units:
19 cm), which cuts away whatever is nearer and so shows what is behind it.
The view fades to black over the last few centimetres in, black once the
eyes are well in (or the world ahead well within the near plane), and back
as the head comes out: a tint of the eyes' images as they are presented,
as the seat's blinks are, the HUD's layer lit, its state message saying to
step back; a scope (raised to the eye, or the smart-link's on the visor)
shows nothing meanwhile, as it would see through the wall too. Leaning around a corner, the head in the open, crouching behind
cover and the head near a wall (but not within the near plane of it) do
not fade, nor does anything in a vehicle's seat (the vehicle's own hull
about the seated eye) or as the player dies.

With debug.vr_test_hands, the log says, as the head moves and the fade
changes, how far the head is from the game's eye, how far past the world it
is, how near the world is ahead, and how dark the view is.
debug.vr_test_head_wall puts the player's eye at a given place in the
level, for the test to lean the head into a wall from there (and, given a
point to face, for views of the level from a known place).
*/

#ifdef HALO_VR

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "camera/director.h"
#include "cutscene/cinematics.h"
#include "physics/collision_usage.h"
#include "physics/collisions.h"
#include "text/unicode.h"

#include "../src/vr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);
/* xgpu_stream_encode.c's */
void halo_vr_eye_tint(float red, float green, float blue);

/* the eyes' sphere about their middle (metres: each eye is some 3 cm from
it) */
#define HEAD_RADIUS 0.05f
/* past the world (the sphere's front beyond where the line from the
game's eye meets it), the view is black this far in (metres): once the
eyes' middle is at the surface, where the view from it sees through the
world whichever way it looks (and the world ahead, looked for from inside
it, is not found) */
#define BEHIND_FADE_DISTANCE HEAD_RADIUS
/* the world ahead of the eyes' middle: the view begins to fade where it
is this much farther than the near plane (metres), and is black where it
is still this much farther (NEAR_BLACK_DISTANCE): a wall before the eyes is
cut away all at once at the near plane, and a little before that, where
the world bulges nearer than its middle off it, the edges of the view see
through already */
#define NEAR_FADE_DISTANCE 0.06f
#define NEAR_BLACK_DISTANCE 0.01f
/* the world ahead is looked for along the view's middle and this far off
it (degrees) up, down, left and right; nearer the edges of the view, a
wall beside the head (leaning around a corner) is cut by the near plane
too, but shows only a sliver at the edge of the view */
#define AHEAD_SPREAD_DEGREES 15.0f
/* the view darkens to black in no less than this (seconds), and lightens
in no less than this */
#define DARKENING_SECONDS 0.05f
#define LIGHTENING_SECONDS 0.25f
/* the view is faded this dark: the state message (hud.c) says to step
back, and a scope shows nothing (render.c) */
#define FADED_DARKNESS 0.5f
/* the test's log: as the darkness moves this much, or the head or the
world about it this much (metres) */
#define LOG_DARKNESS_CHANGE 0.1f
#define LOG_DISTANCE_CHANGE 0.01f
/* debug.vr_test_head_wall: the player is placed once on foot this long
(seconds) */
#define TEST_PLACE_SECONDS 2.0f

static struct
{
	/* this frame's: how dark the view should be for where the head is,
	and how dark it is (0 to 1) */
	real target;
	real darkness;
	/* the test's log: the frame, and what was logged last (the head's
	distance from the game's eye, past the world, the world ahead: world
	units) */
	long frame;
	boolean logged_radius;
	boolean logged_tested;
	real logged_darkness;
	real logged_head;
	real logged_behind;
	real logged_ahead;
	/* debug.vr_test_head_wall: read, whether it places the player, where
	(the game's eye, world units), whether they are turned to face a point
	(x and y), how long they have been on foot (seconds) and whether they
	have been placed */
	boolean test_checked;
	boolean testing;
	real_point3d test_eye;
	boolean test_facing;
	real test_face[2];
	real test_seconds;
	boolean test_placed;
} vr_headwall = { 0.0f, 0.0f, 0, FALSE, FALSE, -1.0f };

/* vr.head_in_wall: "fade" (TRUE) or "off" */
static boolean head_in_wall_fades(void)
{
	static unsigned long read_at = (unsigned long)-1;
	static boolean fades;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		fades = strcmp(config_string("vr.head_in_wall"), "fade") == 0;
	}
	return fades;
}

static real smooth(real t)
{
	t = PIN(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/* the first player's unit on foot and alive, in the first-person view,
else NONE (looked up each frame: a frame drawn as a map ends has the old
map's handles) */
static long unit_on_foot(void)
{
	long player_index = local_player_get_player_index(0);
	long unit_index = player_index != NONE ? player_get(player_index)->unit_index : NONE;
	struct object_datum *unit = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	if (!unit || unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit) ||
		cinematic_in_progress() || director_get_perspective(0) != _director_perspective_first_person)
	{
		return NONE;
	}
	return unit_index;
}

/* what the head's line and sphere are tested against: as the game's line
of sight (front faces, not two-sided nor invisible surfaces; the
structure, scenery and machines), without vehicles */
static unsigned long collision_flags(void)
{
	return _collision_test_for_line_of_sight_flags & ~FLAG(_collision_test_objects_vehicles_bit);
}

/* how far the eyes' sphere is past the world on the line from the game's
eye (world units; 0: not), and how near the world is ahead of the eyes'
middle (view depth, world units; reach where it is not as near) */
static void head_test(long unit_index, real_point3d const *eye, real_point3d const *head,
	real_vector3d const *forward, real_vector3d const *up, real radius, real reach, real *behind, real *ahead)
{
	struct collision_result collision;
	real_vector3d line, left;
	real length;
	real spread = AHEAD_SPREAD_DEGREES * _pi / 180.0f;
	short probe;

	*behind = 0.0f;
	*ahead = reach;
	vector_from_points3d(eye, head, &line);
	length = magnitude3d(&line);
	if (length > 0.001f * radius)
	{
		/* (on past the eyes' middle by the sphere's radius) */
		scale_vector3d(&line, (length + radius) / length, &line);
		if (collision_test_vector(collision_flags(), eye, &line, unit_index, &collision))
			*behind = (length + radius) * (1.0f - collision.t);
	}
	cross_product3d(up, forward, &left);
	normalize3d(&left);
	for (probe = 0; probe < 5; probe++)
	{
		/* (the view's middle, then off it up, down, left and right) */
		real_vector3d across = probe <= 2 ? *up : left;
		real angle = probe == 0 ? 0.0f : (probe & 1) ? spread : -spread;
		real cosine = (real)cos(angle), sine = (real)sin(angle);
		real_vector3d direction;

		direction.i = (forward->i * cosine + across.i * sine) * reach / cosine;
		direction.j = (forward->j * cosine + across.j * sine) * reach / cosine;
		direction.k = (forward->k * cosine + across.k * sine) * reach / cosine;
		if (collision_test_vector(collision_flags(), head, &direction, unit_index, &collision))
			*ahead = MIN(*ahead, collision.t * reach);
	}
}

/* debug.vr_test_hands: the unit's collision radius once (how near the
game's eye stays to a wall), and the test's state as the head or the world
about it moves, or the darkness does */
static void headwall_log(long unit_index, real_point3d const *eye, real_point3d const *head,
	real_vector3d const *forward, real units, real near, real behind, real ahead)
{
	boolean tested = unit_index != NONE;
	real distance = distance3d(eye, head);
	real change = LOG_DISTANCE_CHANGE * units;

	if (!halo_vr_test_hands() || units <= 0.0f)
		return;
	if (tested && !vr_headwall.logged_radius)
	{
		struct biped_datum *biped = biped_try_and_get(unit_index);

		if (biped)
		{
			struct biped_definition *definition = biped_definition_get(biped->definition_index);

			platform_log("vr head wall: the unit's collision radius %.3f m, the near plane %.3f m",
				definition->biped.collision_radius / units, near / units);
		}
		vr_headwall.logged_radius = TRUE;
	}
	if (tested == vr_headwall.logged_tested &&
		fabsf(vr_headwall.darkness - vr_headwall.logged_darkness) < LOG_DARKNESS_CHANGE &&
		!(vr_headwall.darkness == 0.0f && vr_headwall.logged_darkness > 0.0f) &&
		!(vr_headwall.darkness == 1.0f && vr_headwall.logged_darkness < 1.0f) &&
		(!tested || (fabsf(distance - vr_headwall.logged_head) < change &&
		fabsf(behind - vr_headwall.logged_behind) < change && fabsf(ahead - vr_headwall.logged_ahead) < change)))
	{
		return;
	}
	vr_headwall.logged_tested = tested;
	vr_headwall.logged_darkness = vr_headwall.darkness;
	vr_headwall.logged_head = distance;
	vr_headwall.logged_behind = behind;
	vr_headwall.logged_ahead = ahead;
	if (tested)
	{
		platform_log("vr head wall: frame %ld: the head %.3f m from the game's eye (%.2f %.2f %.2f), looking %.0f "
			"degrees, past the world %.3f m, the world %s%.3f m ahead; to %.2f dark, %.2f", vr_headwall.frame,
			distance / units, eye->x, eye->y, eye->z, atan2(forward->j, forward->i) * 180.0f / _pi,
			behind / units, ahead >= near + NEAR_FADE_DISTANCE * units ? "beyond " : "", ahead / units,
			vr_headwall.target, vr_headwall.darkness);
	}
	else
	{
		platform_log("vr head wall: frame %ld: not tested (%s), %.2f dark", vr_headwall.frame,
			head_in_wall_fades() ? "not on foot in the first-person view" : "vr.head_in_wall = \"off\"",
			vr_headwall.darkness);
	}
}

/* debug.vr_test_head_wall: "<x> <y> <z>[ <x> <y>]", the first player's eye
put there (world units), once they have been on foot TEST_PLACE_SECONDS,
once a run, the unit as far below it as its eye is now; turned to face the
second point, where there is one */
static void headwall_test_place(long unit_index, real_point3d const *eye, real seconds)
{
	struct object_datum *unit;
	real_point3d position;

	if (!vr_headwall.test_checked)
	{
		char const *setting = config_string("debug.vr_test_head_wall");
		int count = sscanf(setting, "%f %f %f %f %f", &vr_headwall.test_eye.x, &vr_headwall.test_eye.y,
			&vr_headwall.test_eye.z, &vr_headwall.test_face[0], &vr_headwall.test_face[1]);

		vr_headwall.test_checked = TRUE;
		vr_headwall.testing = count >= 3;
		vr_headwall.test_facing = count == 5;
	}
	vr_headwall.test_seconds = unit_index != NONE ? vr_headwall.test_seconds + MAX(seconds, 0.0f) : 0.0f;
	if (!vr_headwall.testing || vr_headwall.test_placed || vr_headwall.test_seconds < TEST_PLACE_SECONDS)
		return;
	unit = object_get(unit_index);
	position.x = unit->object.position.x + vr_headwall.test_eye.x - eye->x;
	position.y = unit->object.position.y + vr_headwall.test_eye.y - eye->y;
	position.z = unit->object.position.z + vr_headwall.test_eye.z - eye->z;
	object_set_position(unit_index, &position, NULL, NULL);
	if (vr_headwall.test_facing)
		halo_vr_test_face((float)atan2(vr_headwall.test_face[1] - vr_headwall.test_eye.y,
			vr_headwall.test_face[0] - vr_headwall.test_eye.x));
	vr_headwall.test_placed = TRUE;
	platform_log("vr head wall test: frame %ld: the eye put at %.3f %.3f %.3f", vr_headwall.frame,
		vr_headwall.test_eye.x, vr_headwall.test_eye.y, vr_headwall.test_eye.z);
}

/* each VR frame (vr_render.c), once the eyes are placed: the game's eye
(before the head moves it), the eyes' middle and the view's forward and up
(world), the near plane (world units), world units a metre, the time since
the last frame (seconds), and whether the view is the player's (aiming,
alive, no cinematic) */
void vr_headwall_update(boolean playing, real_point3d const *eye, real_point3d const *head,
	real_vector3d const *forward, real_vector3d const *up, real near, real units, real seconds)
{
	long on_foot_index = playing ? unit_on_foot() : NONE;
	long unit_index = head_in_wall_fades() ? on_foot_index : NONE;
	real behind = 0.0f, ahead = 0.0f;

	vr_headwall.frame++;
	vr_headwall.target = 0.0f;
	if (unit_index != NONE && units > 0.0f)
	{
		real reach = near + NEAR_FADE_DISTANCE * units;

		head_test(unit_index, eye, head, forward, up, HEAD_RADIUS * units, reach, &behind, &ahead);
		vr_headwall.target = MAX(smooth(behind / (BEHIND_FADE_DISTANCE * units)),
			smooth((reach - ahead) / ((NEAR_FADE_DISTANCE - NEAR_BLACK_DISTANCE) * units)));
	}
	/* (at once nearly, going in: the fade over the last centimetres is
	smooth already, and the far side should not show; slower out) */
	if (vr_headwall.target > vr_headwall.darkness)
	{
		vr_headwall.darkness = seconds > 0.0f ?
			MIN(vr_headwall.target, vr_headwall.darkness + seconds / DARKENING_SECONDS) : vr_headwall.target;
	}
	else
	{
		vr_headwall.darkness = MAX(vr_headwall.target, vr_headwall.darkness - MAX(seconds, 0.0f) / LIGHTENING_SECONDS);
	}
	headwall_log(unit_index, eye, head, forward, units, near, behind, ahead);
	headwall_test_place(on_foot_index, eye, seconds);
}

/* the eye pass, after the game's screen flash (render.c): the fade over
both eyes, all of each (the VR host's tint, as they are presented; the HUD's
layer stays lit) */
void vr_headwall_draw(void)
{
	if (vr_headwall.darkness <= 0.0f)
		return;
	halo_vr_eye_tint(1.0f - vr_headwall.darkness, 1.0f - vr_headwall.darkness, 1.0f - vr_headwall.darkness);
}

/* the view is faded by the head in a wall, half dark or more: the scope's
view is not drawn (render.c), and the HUD says to step back */
boolean vr_headwall_faded(void)
{
	return vr_headwall.darkness >= FADED_DARKNESS;
}

/* hud.c, for the first player: the state message to show instead of the
game's (TRUE) while the view is faded by the head in a wall */
boolean vr_headwall_prompt(wchar_t *text, long length)
{
	if (!vr_headwall_faded() || length < 2)
		return FALSE;
	ustrncpy(text, L"Step back: your head is in the wall", length - 1);
	text[length - 1] = 0;
	return TRUE;
}

#endif
