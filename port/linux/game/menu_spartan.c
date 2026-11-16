/*
MENU_SPARTAN.C

The menus' Spartan, in a view of its own among the menus' widgets: up to
four of them, each a scenery object of ui.map's (the menus' models'
scenery, menu_models.h), the assault rifle's attached to its hand, playing
the standing rifle idle, its armor the change color the player's profile
color gives it. Set dressing: no unit, player, physics or AI.

The engine draws them through its own renderer (render_object_alone, as
the scene draws an object), but not in ui.map's scene: the scene's view
leaves them out (menu_spartan_object, render_objects.c), and each is drawn
in a view of its own (rasterizer_view_begin) when the menus draw the
screen's <spartan> it stands in, in the widgets' order: over the screen's
background and what is drawn before it, under the widgets drawn after. Each
view has its own camera, fixed on that Spartan's place on the stage (the
stage where the first was made, never moved by the scene's camera; each
Spartan a step along it from the one before), looking straight at it, so
that a Spartan at a screen's edge is drawn as one in its middle is, not
seen from the side; its own depth (cleared in its rectangle: nothing of the
scene's, the ring or the sky, can cut it), no fog, and a fixed light
(spartan_lighting) in place of the scene's.

A screen asks for a Spartan each frame it draws one (menu_spartan_place:
where in the menus' 640 by 480, how tall, which color). menu_spartan_frame,
at the start of each frame the menus draw, makes the Spartans asked for in
the frame before, stands each on the stage where its view shows it in its
place, and deletes those no screen asked for (a screen closed). A view's
rectangle is its Spartan's box, grown so that a rifle or an arm reaching
out of it is not cut. Without ui.map's Spartan tags (no
file, or one refused: menu_models.c), nothing is made, and the screens draw
the original pictures (menu_spartan_available).
*/

#include "cseries.h"
#include "cache/cache_files.h"
#include "math/real_math.h"
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
#include "models/models.h"
#include "objects/objects.h"
#include "objects/scenery.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "render/render_cameras_internal.h"
#include "saved games/player_profile.h"
#include "tag_files/tag_groups.h"

#include "menu_models.h"
#include "menu_spartan.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- prototypes */

void platform_log(char const *format, ...);
char *getenv(char const *name);
long halo_screen_width(void);
void halo_screen_ui_offset(unsigned char centered);
void halo_hdr_interface(void);
unsigned long ui_widget_port_milliseconds(void);

/* ---------- constants */

#define SPARTAN_IDLE "stand rifle idle"
/* the fidgets' animations (the cyborg's graph's): the assault rifle's
reload, the rifle's ready (played backwards, the weapon put away, then
forwards), the crouched idle (dipped into and back out), and the aiming and
looking screens (the aim and the head turned, as a unit turns them) */
#define SPARTAN_RELOAD "stand rifle ar reload-1"
#define SPARTAN_READY "stand rifle ready"
#define SPARTAN_CROUCH "crouch rifle idle"
#define SPARTAN_AIM "stand rifle aim-still"
#define SPARTAN_LOOK "stand look"
/* the graph's seat and weapon class whose markers hold the rifle */
#define SPARTAN_SEAT "stand"
#define SPARTAN_WEAPON_CLASS "rifle"
#define SPARTAN_HAND_MARKER "right hand"
/* the most of the rifle class's weapon IK points kept (its hands put on the
rifle's markers, as a unit's are: units.c's unit_postprocess_node_matrices) */
#define MAXIMUM_SPARTAN_IK_POINTS 4

enum
{
	SCENERY_TAG = 'scen',
	/* the menus' canvas */
	MENUS_WIDTH = 640,
	MENUS_HEIGHT = 480,
};

/* how tall the cyborg stands, in world units, from its origin to the top of
its helmet (its idle's), and how far its boots' soles are under its origin
(stood that much higher, its soles are on its box's foot) */
#define SPARTAN_HEIGHT 0.70f
#define SPARTAN_SOLES 0.026f
/* how far it is turned from facing the camera, to its right (the
originals' pictures': their camera's yaw 172 to its facing) */
#define SPARTAN_TURN_DEGREES 8.0f
/* the view's camera's vertical field of view: a portrait's, narrow, so
that the Spartan is not drawn wider at the top than at the feet */
#define SPARTAN_FIELD_OF_VIEW_DEGREES 30.0f
/* how far the view's camera looks up at the Spartan: from about its knees,
as the originals' pictures were taken (player_color_marine_large), not
from its middle */
#define SPARTAN_PITCH_DEGREES 8.0f
/* how its idle aims (degrees from ahead, through the aiming screen, as a
unit aims): nearly at the camera, a little to its right and down toward
it, the rifle at its shoulder and its muzzle at the viewer, as in the
originals' pictures (both hands on it: spartan_weapon_ik) */
#define SPARTAN_AIM_YAW_DEGREES (-3.0f)
#define SPARTAN_AIM_PITCH_DEGREES (-15.0f)
/* the view's rectangle about a Spartan's box, in its heights: the rifle
and the arms reach out of the box at the sides, the helmet a little above */
#define SPARTAN_VIEW_SIDE 0.45f
#define SPARTAN_VIEW_TOP 0.10f
#define SPARTAN_VIEW_FOOT 0.04f
#define SPARTAN_VIEW_NEAR 0.05f
#define SPARTAN_VIEW_FAR 100.0f
/* how far apart, in world units, the Spartans stand on the stage (each its
own view's: apart, so that nothing of one is in another's) */
#define SPARTAN_STAGE_STEP 1.0f

/* a random color's Spartan: each color held, then blended into the next,
in all a color's step (milliseconds) */
#define SPARTAN_RANDOM_HOLD 750
#define SPARTAN_RANDOM_BLEND 250

/* the fidgets: now and then (each Spartan its own times, from its place), a
reload, a weapon check, a look around or a dip into a crouch, blended in
from the idle and back out to it. Their kinds */
#define SPARTAN_FIDGET_RELOAD 0
#define SPARTAN_FIDGET_WEAPON_CHECK 1
#define SPARTAN_FIDGET_LOOK 2
#define SPARTAN_FIDGET_CROUCH 3
#define NUMBER_OF_SPARTAN_FIDGETS 4
/* how often (milliseconds from one's start to the next's, a place's own
between these), the first a while after the menus' clock starts, and how
far apart any two Spartans' start (none in step with another) */
#define SPARTAN_FIDGET_EVERY_MINIMUM 10000UL
#define SPARTAN_FIDGET_EVERY_MAXIMUM 20000UL
#define SPARTAN_FIDGET_FIRST_MINIMUM 3000UL
#define SPARTAN_FIDGET_FIRST_MAXIMUM 15000UL
#define SPARTAN_FIDGET_APART 700UL
/* a Spartan just shown idles this long before its first */
#define SPARTAN_FIDGET_SETTLE 1500UL
/* the animations' frames a second */
#define SPARTAN_FRAMES_PER_SECOND 30
/* the blend from the idle into an animation and back (milliseconds) */
#define SPARTAN_BLEND_IN 250
#define SPARTAN_BLEND_OUT 350
/* a weapon check: the rifle held put away this long */
#define SPARTAN_WEAPON_CHECK_HOLD 350
/* ... and the ready played this much slower (a soldier's check, not a
weapon switch's) */
#define SPARTAN_WEAPON_CHECK_SLOWER 2
/* a look around: its length and blends, and how far the aim turns (to its
side and the other, degrees) and the head on from the aim */
#define SPARTAN_LOOK_LENGTH 5600
#define SPARTAN_LOOK_BLEND 450
#define SPARTAN_LOOK_AIM_DEGREES 28.0f
#define SPARTAN_LOOK_HEAD_DEGREES 18.0f
#define SPARTAN_LOOK_PITCH_DEGREES 4.0f
/* a crouch: down, held, back up */
#define SPARTAN_CROUCH_DOWN 550
#define SPARTAN_CROUCH_HOLD 650
#define SPARTAN_CROUCH_UP 650
/* ... how far into the crouched idle (a dip, not all the way down) */
#define SPARTAN_CROUCH_DEPTH 0.8f

/* each Spartan's own look in a lineup, from its place: turned a little
from the others (degrees either way, at least the first, at most the
second), a little nearer or further (its size, a part either way) and its
feet lower or higher as it is nearer or further (a part of its height),
and its idle at its own time (milliseconds, at most). One alone is turned
only a little */
#define SPARTAN_LINEUP_TURN_MINIMUM 1.5f
#define SPARTAN_LINEUP_TURN_MAXIMUM 6.0f
#define SPARTAN_SINGLE_TURN_MAXIMUM 1.5f
#define SPARTAN_LINEUP_NEARER 0.035f
#define SPARTAN_LINEUP_NEARER_LOWER 0.02f
#define SPARTAN_IDLE_PHASES 4000UL

/* ---------- structures */

/* a Spartan's own look, from its place (spartan_variation_find) */
struct spartan_variation
{
	real turn_degrees;
	/* -1 (further) to 1 (nearer) */
	real nearer;
	unsigned long idle_phase;
};

/* where a fidget is (spartan_fidget_progress): how much of it is drawn
over the idle (0 to 1), its animation's frame, and a look around's turn
(-1 to 1) */
struct spartan_fidget_pose
{
	real weight;
	real frame;
	real look;
};

/* the fidgets' times, every place's (spartan_schedule_advance): a pure
function of the menus' clock, worked out from its start */
struct spartan_schedule
{
	boolean started;
	boolean any;
	unsigned long time;
	unsigned long last_start;
	/* each place's next start, how many it has started, and its last's
	start and kind (NONE: none yet) */
	unsigned long next[MAXIMUM_MENU_SPARTANS];
	long count[MAXIMUM_MENU_SPARTANS];
	unsigned long start[MAXIMUM_MENU_SPARTANS];
	short kind[MAXIMUM_MENU_SPARTANS];
};

struct menu_spartan
{
	/* asked for in the frame (its frame index), and where, in the menus'
	640 by 480 of its screen */
	long asked_frame;
	real x, bottom, height;
	short color_index;
	/* its view's rectangle, in the menus' 640 by 480 of its screen */
	real view[4];
	/* in a lineup (its own look: variation) */
	boolean lineup;
	struct spartan_variation variation;
	/* made: its objects, and where it stands on the stage, and when (the
	menus' clock) */
	long object_index;
	long rifle_index;
	real_point3d position;
	real_vector3d forward;
	unsigned long made_milliseconds;
	/* its pose as drawn this frame, its body's nodes and its rifle's (none:
	the game's own, the idle's) */
	boolean posed, rifle_posed;
	real_matrix4x3 body_nodes[MAXIMUM_NODES_PER_MODEL];
	real_matrix4x3 rifle_nodes[MAXIMUM_NODES_PER_MODEL];
	/* debug: the fidget traced as begun (its start; NONE: none) */
	unsigned long traced_start;
	short traced_kind;
};

/* ---------- globals */

static struct
{
	struct menu_spartan spartans[MAXIMUM_MENU_SPARTANS];
	/* the tags' generation the objects are of (a map loaded since: none) */
	unsigned long generation;
	long frame;
	long updated_frame;
	long spartan_definition, rifle_definition, animation_graph;
	/* the idle's and the fidgets' animations (NONE: none), and the aiming
	and looking screens' bounds */
	short idle_animation;
	short fidget_animations[NUMBER_OF_SPARTAN_FIDGETS];
	short aim_animation, look_animation;
	struct animation_aiming_screen_bounds aim_bounds, look_bounds;
	/* the fidgets' times */
	struct spartan_schedule schedule;
	/* the Spartan whose view is drawn now (its nodes: menu_spartan_node_matrices) */
	struct menu_spartan const *drawing;
	char hand_marker[TAG_STRING_LENGTH + 1];
	char grip_marker[TAG_STRING_LENGTH + 1];
	/* the rifle class's weapon IK points: each a marker of the Spartan's (a
	hand's) put on one of the rifle's */
	short ik_point_count;
	struct animation_graph_ik_point ik_points[MAXIMUM_SPARTAN_IK_POINTS];
	/* the stage: where the first view's camera stands (set as the first
	Spartan is made, kept while there is one; each Spartan's a step along it) */
	boolean staged;
	real_point3d stage;
	/* debug: the frames drawn since the last trace */
	long traced_frames;
} menu_spartan;

/* ---------- private code */

/* a number from two (a place, and what it is for): the same each time,
scattered (a hash's mix, 32 bits) */
static unsigned long spartan_random(
	unsigned long a,
	unsigned long b)
{
	unsigned long x = (a * 0x9E3779B1UL + b * 0x85EBCA77UL + 0x165667B1UL) & 0xFFFFFFFFUL;

	x ^= x >> 15;
	x = (x * 0x2C1B3C6DUL) & 0xFFFFFFFFUL;
	x ^= x >> 12;
	x = (x * 0x297A2D39UL) & 0xFFFFFFFFUL;
	x ^= x >> 15;
	return x;
}

/* ... as a part, 0 to 1 */
static real spartan_random_part(
	unsigned long a,
	unsigned long b)
{
	return (real)(spartan_random(a, b) % 10001UL) / 10000.0f;
}

/* a Spartan's own look, from its place (index), the same each frame: in a
lineup turned its own way and a little nearer or further, alone turned
hardly at all; its idle at its own time either way */
static void spartan_variation_find(
	short index,
	boolean lineup,
	struct spartan_variation *variation)
{
	real side = spartan_random(index, 1) & 1 ? 1.0f : -1.0f;

	if (lineup)
	{
		variation->turn_degrees = side * (SPARTAN_LINEUP_TURN_MINIMUM +
			(SPARTAN_LINEUP_TURN_MAXIMUM - SPARTAN_LINEUP_TURN_MINIMUM) * spartan_random_part(index, 2));
		variation->nearer = spartan_random_part(index, 3) * 2.0f - 1.0f;
	}
	else
	{
		variation->turn_degrees = side * SPARTAN_SINGLE_TURN_MAXIMUM * spartan_random_part(index, 2);
		variation->nearer = 0.0f;
	}
	variation->idle_phase = spartan_random(index, 4) % SPARTAN_IDLE_PHASES;
}

/* a place's fidget after the count before it: one at random, never the
one it did last */
static short spartan_fidget_kind(
	short index,
	long count,
	short previous)
{
	short kind = (short)(spartan_random(index, 0x10000UL + (unsigned long)count) % NUMBER_OF_SPARTAN_FIDGETS);

	if (kind == previous)
	{
		kind = (short)((kind + 1 + spartan_random(index, 0x20000UL + (unsigned long)count) %
			(NUMBER_OF_SPARTAN_FIDGETS - 1)) % NUMBER_OF_SPARTAN_FIDGETS);
	}
	return kind;
}

/* the fidgets started by the menus' clock's milliseconds, each place's
last kept: each place's first a while after the clock's start and each
next its own while after its last (both from the place), and none
starting sooner than SPARTAN_FIDGET_APART after another's (put off till
then). Worked out from the clock's start (again as the clock goes back),
so the same at the same time however often it is asked */
static void spartan_schedule_advance(
	struct spartan_schedule *schedule,
	unsigned long milliseconds)
{
	short index;

	if (!schedule->started || milliseconds < schedule->time)
	{
		memset(schedule, 0, sizeof(*schedule));
		schedule->started = TRUE;
		for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
		{
			schedule->next[index] = SPARTAN_FIDGET_FIRST_MINIMUM + spartan_random(index, 5) %
				(SPARTAN_FIDGET_FIRST_MAXIMUM - SPARTAN_FIDGET_FIRST_MINIMUM + 1);
			schedule->kind[index] = NONE;
		}
	}
	schedule->time = milliseconds;
	for (;;)
	{
		short soonest = 0;
		unsigned long at;

		for (index = 1; index < MAXIMUM_MENU_SPARTANS; index++)
		{
			if (schedule->next[index] < schedule->next[soonest])
				soonest = index;
		}
		at = schedule->next[soonest];
		if (schedule->any && at < schedule->last_start + SPARTAN_FIDGET_APART)
			at = schedule->last_start + SPARTAN_FIDGET_APART;
		if (at > milliseconds)
			break;
		schedule->kind[soonest] = spartan_fidget_kind(soonest, schedule->count[soonest], schedule->kind[soonest]);
		schedule->start[soonest] = at;
		schedule->count[soonest]++;
		schedule->last_start = at;
		schedule->any = TRUE;
		schedule->next[soonest] = at + SPARTAN_FIDGET_EVERY_MINIMUM +
			spartan_random(soonest, 0x100UL + (unsigned long)schedule->count[soonest]) %
			(SPARTAN_FIDGET_EVERY_MAXIMUM - SPARTAN_FIDGET_EVERY_MINIMUM + 1);
	}
}

/* eased in and out, 0 to 1 */
static real spartan_ease(
	real t)
{
	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	return t * t * (3.0f - 2.0f * t);
}

/* how long a fidget is (milliseconds), its animation frame_count frames
long (a reload's, the ready's) */
static unsigned long spartan_fidget_length(
	short kind,
	short frame_count)
{
	unsigned long play = (unsigned long)(frame_count > 1 ? frame_count - 1 : 0) * 1000UL / SPARTAN_FRAMES_PER_SECOND;

	switch (kind)
	{
	case SPARTAN_FIDGET_RELOAD:
		return play;
	case SPARTAN_FIDGET_WEAPON_CHECK:
		return play ? 2 * SPARTAN_WEAPON_CHECK_SLOWER * play + SPARTAN_WEAPON_CHECK_HOLD : 0;
	case SPARTAN_FIDGET_LOOK:
		return SPARTAN_LOOK_LENGTH;
	case SPARTAN_FIDGET_CROUCH:
		return SPARTAN_CROUCH_DOWN + SPARTAN_CROUCH_HOLD + SPARTAN_CROUCH_UP;
	}
	return 0;
}

/* a look around's turn elapsed milliseconds in: to one side (1), held, to
the other (-1), held, and back (0), eased between */
static real spartan_look_turn(
	unsigned long elapsed)
{
	static unsigned long const times[] = { 0, 1600, 2200, 3900, 4500, SPARTAN_LOOK_LENGTH };
	static real const turns[] = { 0.0f, 1.0f, 1.0f, -1.0f, -1.0f, 0.0f };
	short key;

	for (key = 1; key < (short)(sizeof(times) / sizeof(times[0])); key++)
	{
		if (elapsed < times[key])
		{
			real t = (real)(elapsed - times[key - 1]) / (real)(times[key] - times[key - 1]);

			return turns[key - 1] + (turns[key] - turns[key - 1]) * spartan_ease(t);
		}
	}
	return 0.0f;
}

/* a fidget elapsed milliseconds after its start (its animation
frame_count frames long): how much of it is drawn over the idle (eased in
from none and back out to none), its animation's frame, a look's turn.
FALSE once it is over */
static boolean spartan_fidget_progress(
	short kind,
	unsigned long elapsed,
	short frame_count,
	struct spartan_fidget_pose *pose)
{
	unsigned long length = spartan_fidget_length(kind, frame_count);
	real blend_in = (real)SPARTAN_BLEND_IN, blend_out = (real)SPARTAN_BLEND_OUT;
	real frames = (real)elapsed * (real)SPARTAN_FRAMES_PER_SECOND / 1000.0f;
	real last = (real)(frame_count > 1 ? frame_count - 1 : 0);

	pose->weight = 0.0f;
	pose->frame = 0.0f;
	pose->look = 0.0f;
	if (elapsed >= length)
		return FALSE;
	switch (kind)
	{
	case SPARTAN_FIDGET_RELOAD:
		pose->frame = frames;
		break;
	case SPARTAN_FIDGET_WEAPON_CHECK:
	{
		/* (put away: the ready backwards; held; ready again) */
		unsigned long play = (length - SPARTAN_WEAPON_CHECK_HOLD) / 2;

		if (elapsed < play)
			pose->frame = last - frames / SPARTAN_WEAPON_CHECK_SLOWER;
		else if (elapsed < play + SPARTAN_WEAPON_CHECK_HOLD)
			pose->frame = 0.0f;
		else
			pose->frame = (real)(elapsed - play - SPARTAN_WEAPON_CHECK_HOLD) * (real)SPARTAN_FRAMES_PER_SECOND /
				(1000.0f * SPARTAN_WEAPON_CHECK_SLOWER);
		break;
	}
	case SPARTAN_FIDGET_LOOK:
		pose->look = spartan_look_turn(elapsed);
		blend_in = blend_out = (real)SPARTAN_LOOK_BLEND;
		break;
	case SPARTAN_FIDGET_CROUCH:
		blend_in = (real)SPARTAN_CROUCH_DOWN;
		blend_out = (real)SPARTAN_CROUCH_UP;
		break;
	}
	pose->frame = pose->frame < 0.0f ? 0.0f : pose->frame > last ? last : pose->frame;
	pose->weight = MIN(spartan_ease((real)elapsed / blend_in), spartan_ease((real)(length - elapsed) / blend_out));
	return TRUE;
}

/* node_count nodes' orientations blended from from to to, weight of the
way (0: from's, 1: to's), into to */
static void spartan_orientations_blend(
	short node_count,
	real_orientation const *from,
	real_orientation *to,
	real weight)
{
	short node_index;

	if (weight >= 1.0f)
		return;
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_orientation blended;

		orientations_interpolate(&from[node_index], &to[node_index], weight, &blended);
		to[node_index] = blended;
	}
}

static void spartan_forget(
	void)
{
	short index;

	for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
	{
		menu_spartan.spartans[index].object_index = NONE;
		menu_spartan.spartans[index].rifle_index = NONE;
		menu_spartan.spartans[index].asked_frame = -100;
		menu_spartan.spartans[index].posed = FALSE;
		menu_spartan.spartans[index].rifle_posed = FALSE;
	}
	for (index = 0; index < NUMBER_OF_SPARTAN_FIDGETS; index++)
		menu_spartan.fidget_animations[index] = NONE;
	menu_spartan.idle_animation = NONE;
	menu_spartan.aim_animation = NONE;
	menu_spartan.look_animation = NONE;
	menu_spartan.drawing = NULL;
	csmemset(&menu_spartan.aim_bounds, 0, sizeof(menu_spartan.aim_bounds));
	csmemset(&menu_spartan.look_bounds, 0, sizeof(menu_spartan.look_bounds));
	menu_spartan.spartan_definition = NONE;
	menu_spartan.rifle_definition = NONE;
	menu_spartan.animation_graph = NONE;
	menu_spartan.staged = FALSE;
	menu_spartan.generation = cache_files_tags_generation();
}

/* the tags of the map loaded now: the scenery, its graph, and the markers
the graph holds a rifle by (its standing seat's rifle class's, as a unit's:
units.c) */
static void spartan_tags_find(
	void)
{
	long animation_graph;

	spartan_forget();
	menu_spartan.spartan_definition = menu_models_tag(SCENERY_TAG, MENU_MODELS_SPARTAN);
	menu_spartan.rifle_definition = menu_models_tag(SCENERY_TAG, MENU_MODELS_RIFLE);
	strcpy(menu_spartan.hand_marker, SPARTAN_HAND_MARKER);
	menu_spartan.grip_marker[0] = 0;
	menu_spartan.ik_point_count = 0;
	if (menu_spartan.spartan_definition == NONE)
		return;
	animation_graph = object_definition_get(menu_spartan.spartan_definition)->object.animation_graph.index;
	menu_spartan.animation_graph = animation_graph;
	if (animation_graph != NONE)
	{
		struct animation_graph *graph = animation_graph_definition_get(animation_graph);
		long seat_index, class_index, ik_index;

		for (seat_index = 0; seat_index < graph->unit_seats.count; seat_index++)
		{
			struct animation_graph_unit_seat *seat =
				(struct animation_graph_unit_seat *)graph->unit_seats.address + seat_index;

			if (strcmp(seat->label, SPARTAN_SEAT))
				continue;
			menu_spartan.look_bounds = seat->looking_screen_bounds;
			for (class_index = 0; class_index < seat->weapon_classes.count; class_index++)
			{
				struct animation_graph_weapon_class *weapon_class =
					(struct animation_graph_weapon_class *)seat->weapon_classes.address + class_index;

				if (strcmp(weapon_class->label, SPARTAN_WEAPON_CLASS))
					continue;
				menu_spartan.aim_bounds = weapon_class->aiming_screen_bounds;
				if (weapon_class->hand_marker_name[0])
				{
					strncpy(menu_spartan.hand_marker, weapon_class->hand_marker_name, TAG_STRING_LENGTH);
					menu_spartan.hand_marker[TAG_STRING_LENGTH] = 0;
				}
				strncpy(menu_spartan.grip_marker, weapon_class->grip_marker_name, TAG_STRING_LENGTH);
				menu_spartan.grip_marker[TAG_STRING_LENGTH] = 0;
				menu_spartan.ik_point_count = 0;
				for (ik_index = 0; ik_index < weapon_class->ik_points.count &&
					menu_spartan.ik_point_count < MAXIMUM_SPARTAN_IK_POINTS; ik_index++)
				{
					struct animation_graph_ik_point *ik_point =
						(struct animation_graph_ik_point *)weapon_class->ik_points.address + ik_index;
					struct animation_graph_ik_point *kept = &menu_spartan.ik_points[menu_spartan.ik_point_count++];

					memcpy(kept, ik_point, sizeof(*kept));
					kept->marker_name[TAG_STRING_LENGTH] = 0;
					kept->attached_to_marker_name[TAG_STRING_LENGTH] = 0;
				}
			}
		}
		menu_spartan.idle_animation = animation_graph_get_animation_by_name(animation_graph, SPARTAN_IDLE);
		menu_spartan.fidget_animations[SPARTAN_FIDGET_RELOAD] =
			animation_graph_get_animation_by_name(animation_graph, SPARTAN_RELOAD);
		menu_spartan.fidget_animations[SPARTAN_FIDGET_WEAPON_CHECK] =
			animation_graph_get_animation_by_name(animation_graph, SPARTAN_READY);
		menu_spartan.fidget_animations[SPARTAN_FIDGET_CROUCH] =
			animation_graph_get_animation_by_name(animation_graph, SPARTAN_CROUCH);
		menu_spartan.aim_animation = animation_graph_get_animation_by_name(animation_graph, SPARTAN_AIM);
		menu_spartan.look_animation = animation_graph_get_animation_by_name(animation_graph, SPARTAN_LOOK);
		/* (a look around is the aiming screen's, the looking screen's on it) */
		menu_spartan.fidget_animations[SPARTAN_FIDGET_LOOK] = menu_spartan.aim_animation;
		if (getenv("HALO_MENU_SPARTAN_TRACE"))
		{
			char const *names[] = { SPARTAN_IDLE, SPARTAN_RELOAD, SPARTAN_READY, SPARTAN_CROUCH, SPARTAN_AIM, SPARTAN_LOOK };
			short indices[6];
			short each;

			indices[0] = menu_spartan.idle_animation;
			indices[1] = menu_spartan.fidget_animations[SPARTAN_FIDGET_RELOAD];
			indices[2] = menu_spartan.fidget_animations[SPARTAN_FIDGET_WEAPON_CHECK];
			indices[3] = menu_spartan.fidget_animations[SPARTAN_FIDGET_CROUCH];
			indices[4] = menu_spartan.aim_animation;
			indices[5] = menu_spartan.look_animation;
			for (each = 0; each < menu_spartan.ik_point_count; each++)
			{
				platform_log("menu spartan: weapon IK: the Spartan's \"%s\" on the rifle's \"%s\"",
					menu_spartan.ik_points[each].marker_name, menu_spartan.ik_points[each].attached_to_marker_name);
			}
			platform_log("menu spartan: the graph's %d animations, %d nodes; aim bounds yaw %.3f x %d / %.3f x %d, "
				"look bounds yaw %.3f x %d / %.3f x %d", graph->animations.count, graph->nodes.count,
				menu_spartan.aim_bounds.negative_yaw_delta, menu_spartan.aim_bounds.negative_yaw_frame_count,
				menu_spartan.aim_bounds.positive_yaw_delta, menu_spartan.aim_bounds.positive_yaw_frame_count,
				menu_spartan.look_bounds.negative_yaw_delta, menu_spartan.look_bounds.negative_yaw_frame_count,
				menu_spartan.look_bounds.positive_yaw_delta, menu_spartan.look_bounds.positive_yaw_frame_count);
			for (each = 0; each < 6; each++)
			{
				struct animation const *animation = indices[each] == NONE ? NULL :
					(struct animation const *)graph->animations.address + indices[each];

				platform_log("menu spartan: animation \"%s\": %d%s", names[each], indices[each],
					animation ? "" : " (none)");
				if (animation)
				{
					platform_log("menu spartan:   type %d, %d frames, %d nodes", animation->type,
						animation->frame_count, animation->node_count);
				}
			}
		}
	}
}

static void spartan_delete(
	struct menu_spartan *spartan)
{
	/* (the rifle goes with it, attached: deleted first all the same, in
	case it was not) */
	if (spartan->rifle_index != NONE && object_try_and_get(spartan->rifle_index))
		object_delete(spartan->rifle_index);
	if (spartan->object_index != NONE && object_try_and_get(spartan->object_index))
		object_delete(spartan->object_index);
	spartan->object_index = NONE;
	spartan->rifle_index = NONE;
	spartan->posed = FALSE;
	spartan->rifle_posed = FALSE;
}

static void spartan_color_set(
	long object_index,
	real_rgb_color color)
{
	struct object_datum *object = object_try_and_get(object_index);
	short index;

	if (!object)
		return;
	/* (every change color, as a player's body has its color: players.c; the
	armor's shader takes its third) */
	for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
	{
		object->object.base_change_colors[index] = color;
		object->object.outgoing_change_colors[index] = color;
	}
}

/* the idle played again from its start as it ends (a scenery's animation
holds its last frame: scenery_update) */
static void spartan_idle_loop(
	struct menu_spartan *spartan)
{
	struct object_datum *object = object_try_and_get(spartan->object_index);
	struct animation_graph *graph;
	struct animation *animation;

	if (!object || menu_spartan.animation_graph == NONE || object->object.animation.state.index == NONE)
		return;
	graph = animation_graph_definition_get(menu_spartan.animation_graph);
	if (object->object.animation.state.index < 0 || object->object.animation.state.index >= graph->animations.count)
		return;
	animation = (struct animation *)graph->animations.address + object->object.animation.state.index;
	/* (scenery_update holds it a frame short of its last) */
	if (object->object.animation.state.frame_index >= animation->frame_count - 2)
		scenery_animation_start(spartan->object_index, menu_spartan.animation_graph, SPARTAN_IDLE);
}

/* the view's camera's axes: fixed in the world, whatever the scene's
camera does (forward along x, up along z, right their product) */
static real_vector3d const spartan_forward = { 1.0f, 0.0f, 0.0f };
static real_vector3d const spartan_up = { 0.0f, 0.0f, 1.0f };
static real_vector3d const spartan_right = { 0.0f, -1.0f, 0.0f };

/* a Spartan's view's rectangle: its box, grown */
static void spartan_view_find(
	struct menu_spartan *spartan)
{
	spartan->view[0] = spartan->x - spartan->height * SPARTAN_VIEW_SIDE;
	spartan->view[2] = spartan->x + spartan->height * SPARTAN_VIEW_SIDE;
	spartan->view[1] = spartan->bottom - spartan->height * (1.0f + SPARTAN_VIEW_TOP);
	spartan->view[3] = spartan->bottom + spartan->height * SPARTAN_VIEW_FOOT;
}

/* where a Spartan's view's camera stands: its step along the stage */
static void spartan_camera_at(
	struct menu_spartan const *spartan,
	real_point3d *camera)
{
	real step = SPARTAN_STAGE_STEP * (real)(spartan - menu_spartan.spartans);

	camera->x = menu_spartan.stage.x + spartan_right.i * step;
	camera->y = menu_spartan.stage.y + spartan_right.j * step;
	camera->z = menu_spartan.stage.z + spartan_right.k * step;
}

/* the view's camera: its pitch, field of view and the Spartan's turn
(degrees: SPARTAN_PITCH_DEGREES, _FIELD_OF_VIEW_DEGREES, _TURN_DEGREES, and
the idle's aim, _AIM_YAW_DEGREES, _AIM_PITCH_DEGREES; debug:
HALO_MENU_SPARTAN_CAMERA's "<pitch> <field of view> <turn> <aim yaw> <aim
pitch>" in their
place, to match them to the originals' pictures), in radians */
static real spartan_camera_angles_read[5] = { SPARTAN_PITCH_DEGREES, SPARTAN_FIELD_OF_VIEW_DEGREES, SPARTAN_TURN_DEGREES,
	SPARTAN_AIM_YAW_DEGREES, SPARTAN_AIM_PITCH_DEGREES };

static void spartan_camera_angles(
	real *pitch,
	real *field_of_view,
	real *turn)
{
	static boolean read;
	real *angles = spartan_camera_angles_read;

	if (!read)
	{
		char const *given = getenv("HALO_MENU_SPARTAN_CAMERA");
		short each;

		read = TRUE;
		for (each = 0; given && *given && each < 5; each++)
		{
			char *end;
			double value = strtod(given, &end);

			if (end == given)
				break;
			angles[each] = (real)value;
			given = end;
		}
	}
	*pitch = angles[0] * 3.14159265f / 180.0f;
	*field_of_view = angles[1] * 3.14159265f / 180.0f;
	*turn = angles[2] * 3.14159265f / 180.0f;
}

/* the view's camera's forward and up: the stage's forward, pitched up */
static void spartan_camera_axes(
	real_vector3d *forward,
	real_vector3d *up)
{
	real pitch, field_of_view, turn;
	real c, s;

	spartan_camera_angles(&pitch, &field_of_view, &turn);
	c = (real)cos(pitch);
	s = (real)sin(pitch);
	forward->i = spartan_forward.i * c + spartan_up.i * s;
	forward->j = spartan_forward.j * c + spartan_up.j * s;
	forward->k = spartan_forward.k * c + spartan_up.k * s;
	up->i = spartan_up.i * c - spartan_forward.i * s;
	up->j = spartan_up.j * c - spartan_forward.j * s;
	up->k = spartan_up.k * c - spartan_forward.k * s;
}

/* where a Spartan stands on the stage, and which way it faces, for the
view's camera to show it in its place in the view's rectangle: its feet's
middle there, as tall as it was asked (its own look's turn, nearness and
feet on that: variation). With the camera pitched up by p, a point d ahead
of it and z above it is seen y up from the view's middle (in the view's
half heights, t the tangent of half the field of view) where
z = d tan(p + atan(y t)): d is found from where the feet and the head are
to be seen, the height from the feet's */
static void spartan_stand(
	struct menu_spartan const *spartan,
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up)
{
	real const *view = spartan->view;
	real half_height = MAX((view[3] - view[1]) * 0.5f, 1.0f);
	real pitch, field_of_view, turn;
	real tangent;
	real height = spartan->height * (1.0f + SPARTAN_LINEUP_NEARER * spartan->variation.nearer);
	real bottom = spartan->bottom + spartan->height * SPARTAN_LINEUP_NEARER_LOWER * spartan->variation.nearer;
	/* (across and up from the view's middle, in its half heights: the
	feet, the head) */
	real across = (spartan->x - (view[0] + view[2]) * 0.5f) / half_height;
	real feet = ((view[1] + view[3]) * 0.5f - bottom) / half_height;
	real head = feet + MAX(height, 1.0f) / half_height;
	real feet_slope, head_slope, distance, lift, middle, side;
	real_point3d camera;

	spartan_camera_angles(&pitch, &field_of_view, &turn);
	tangent = (real)tan(field_of_view * 0.5f);
	turn += spartan->variation.turn_degrees * 3.14159265f / 180.0f;
	feet_slope = (real)tan(pitch + atan(feet * tangent));
	head_slope = (real)tan(pitch + atan(head * tangent));
	distance = SPARTAN_HEIGHT / MAX(head_slope - feet_slope, 0.001f);
	lift = distance * feet_slope + SPARTAN_SOLES;
	/* (sideways as far as its middle is along the camera's forward) */
	middle = distance * (real)cos(pitch) + (lift + SPARTAN_HEIGHT * 0.5f) * (real)sin(pitch);
	side = across * middle * tangent;
	spartan_camera_at(spartan, &camera);
	position->x = camera.x + spartan_forward.i * distance + spartan_right.i * side + spartan_up.i * lift;
	position->y = camera.y + spartan_forward.j * distance + spartan_right.j * side + spartan_up.j * lift;
	position->z = camera.z + spartan_forward.k * distance + spartan_right.k * side + spartan_up.k * lift;
	/* (upright, facing the camera, turned to its right) */
	forward->i = -spartan_forward.i * (real)cos(turn) + spartan_forward.j * (real)sin(turn);
	forward->j = -spartan_forward.i * (real)sin(turn) - spartan_forward.j * (real)cos(turn);
	forward->k = 0.0f;
	*up = spartan_up;
}

/* the view's light: fixed to its camera's axes, as a portrait's (a key
light from above, before and to the left, a little light from under it,
the rest ambient), lit as the engine lights an object from a lightmap
(object_lights.c's build_distant_lights) */
static void spartan_lighting(
	struct render_lighting *lighting)
{
	real_vector3d toward_light;

	csmemset(lighting, 0, sizeof(*lighting));
	lighting->ambient_color.red = 0.30f;
	lighting->ambient_color.green = 0.31f;
	lighting->ambient_color.blue = 0.34f;
	lighting->distant_light_count = 2;
	toward_light.i = -0.65f * spartan_forward.i + 0.65f * spartan_up.i - 0.40f * spartan_right.i;
	toward_light.j = -0.65f * spartan_forward.j + 0.65f * spartan_up.j - 0.40f * spartan_right.j;
	toward_light.k = -0.65f * spartan_forward.k + 0.65f * spartan_up.k - 0.40f * spartan_right.k;
	normalize3d(&toward_light);
	/* (a distant light's direction is the way its light goes) */
	lighting->distant_lights[0].color.red = 0.85f;
	lighting->distant_lights[0].color.green = 0.84f;
	lighting->distant_lights[0].color.blue = 0.80f;
	lighting->distant_lights[0].direction.i = -toward_light.i;
	lighting->distant_lights[0].direction.j = -toward_light.j;
	lighting->distant_lights[0].direction.k = -toward_light.k;
	lighting->distant_lights[1].color.red = 0.16f;
	lighting->distant_lights[1].color.green = 0.17f;
	lighting->distant_lights[1].color.blue = 0.20f;
	lighting->distant_lights[1].direction = spartan_up;
	lighting->point_light_count = 0;
	lighting->reflection_tint_color.alpha = 0.9f;
	lighting->reflection_tint_color.red = 0.75f;
	lighting->reflection_tint_color.green = 0.75f;
	lighting->reflection_tint_color.blue = 0.75f;
	lighting->shadow_vector.k = -1.0f;
	lighting->shadow_color.red = 1.0f;
	lighting->shadow_color.green = 1.0f;
	lighting->shadow_color.blue = 1.0f;
}

/* a Spartan's objects made where it stands: FALSE if they cannot be */
static boolean spartan_make(
	struct menu_spartan *spartan)
{
	struct object_placement_data data;
	real_rgb_color rgb = menu_spartan_color((short)(spartan - menu_spartan.spartans), spartan->color_index,
		ui_widget_port_milliseconds());
	short color;

	object_placement_data_new(&data, menu_spartan.spartan_definition, NONE);
	spartan_stand(spartan, &data.position, &data.forward, &data.up);
	for (color = 0; color < NUMBER_OF_OBJECT_CHANGE_COLORS; color++)
		data.change_colors[color] = rgb;
	spartan->object_index = object_new(&data);
	if (spartan->object_index == NONE)
		return FALSE;
	spartan->position = data.position;
	spartan->forward = data.forward;
	spartan->made_milliseconds = ui_widget_port_milliseconds();
	spartan->traced_start = (unsigned long)NONE;
	spartan->traced_kind = NONE;
	spartan->posed = FALSE;
	spartan->rifle_posed = FALSE;
	/* (kept active, ticked, though no player sees its cluster: the menus
	have none) */
	object_set_automatic_deactivation(spartan->object_index, FALSE);
	object_activate(spartan->object_index);
	if (menu_spartan.animation_graph != NONE)
		scenery_animation_start(spartan->object_index, menu_spartan.animation_graph, SPARTAN_IDLE);
	if (menu_spartan.rifle_definition != NONE)
	{
		object_placement_data_new(&data, menu_spartan.rifle_definition, NONE);
		spartan_stand(spartan, &data.position, &data.forward, &data.up);
		spartan->rifle_index = object_new(&data);
		if (spartan->rifle_index != NONE)
		{
			object_set_automatic_deactivation(spartan->rifle_index, FALSE);
			object_activate(spartan->rifle_index);
			object_attach_to_marker(spartan->object_index, menu_spartan.hand_marker, spartan->rifle_index,
				menu_spartan.grip_marker);
		}
	}
	return TRUE;
}

/* an animation's frame over the pose base (a base animation's frame in
its place; a replacement's or an overlay's on it), into pose */
static void spartan_animation_frame(
	struct model const *model,
	struct animation const *animation,
	short frame_index,
	real_orientation const *base,
	short node_count,
	real_orientation *pose)
{
	memcpy(pose, base, sizeof(real_orientation) * node_count);
	if (animation->type == _animation_base)
		animation_get_node_orientations(model, animation, frame_index, pose);
	else if (animation->type == _animation_replacement)
		replacement_animation_apply(animation, frame_index, pose);
	else if (animation->type == _animation_overlay)
		overlay_animation_apply(animation, frame_index, pose);
}

/* ... at a frame between two (the one before it and the next, blended: the
menus' frames are more than the animation's), looped or held at its last */
static void spartan_animation_pose(
	struct model const *model,
	struct animation const *animation,
	real frame,
	boolean loop,
	real_orientation const *base,
	short node_count,
	real_orientation *pose)
{
	real_orientation next[MAXIMUM_NODES_PER_MODEL];
	short count = animation->frame_count;
	short first, second;
	real fraction;

	if (count <= 0)
	{
		memcpy(pose, base, sizeof(real_orientation) * node_count);
		return;
	}
	if (loop)
	{
		frame = (real)fmod(frame, (double)count);
		if (frame < 0.0f)
			frame += (real)count;
	}
	frame = frame < 0.0f ? 0.0f : frame > (real)(count - 1) ? (real)(count - 1) : frame;
	first = (short)frame;
	fraction = frame - (real)first;
	second = (short)(first + 1);
	if (second >= count)
		second = loop ? 0 : (short)(count - 1);
	spartan_animation_frame(model, animation, first, base, node_count, pose);
	if (fraction > 0.0f && second != first)
	{
		spartan_animation_frame(model, animation, second, base, node_count, next);
		spartan_orientations_blend(node_count, pose, next, fraction);
		memcpy(pose, next, sizeof(real_orientation) * node_count);
	}
}

/* the nodes' matrices of a pose, each its parent's times its own (the
model's nodes, parents before their children, as
object_compute_node_matrices places them: the animations' orientations are
in the model's nodes' order), the root's from root */
static void spartan_node_matrices(
	struct model const *model,
	short node_count,
	real_orientation const *pose,
	real_matrix4x3 const *root,
	real_matrix4x3 *nodes)
{
	short stack[MAXIMUM_NODES_PER_MODEL];
	short stacked = 0, done = 0;

	stack[stacked++] = 0;
	while (stacked && done < node_count)
	{
		short node_index = stack[--stacked];
		struct model_node const *node = (struct model_node const *)model->nodes.address + node_index;
		real_matrix4x3 local;

		matrix4x3_from_orientation(&local, &pose[node_index]);
		if (node_index == 0 || node->parent_node_index < 0 || node->parent_node_index >= node_count)
			matrix4x3_multiply(root, &local, &nodes[node_index]);
		else
			matrix4x3_multiply(&nodes[node->parent_node_index], &local, &nodes[node_index]);
		done++;
		if (node->next_sibling_node_index > 0 && node->next_sibling_node_index < node_count &&
			stacked < MAXIMUM_NODES_PER_MODEL)
		{
			stack[stacked++] = node->next_sibling_node_index;
		}
		if (node->first_child_node_index > 0 && node->first_child_node_index < node_count &&
			stacked < MAXIMUM_NODES_PER_MODEL)
		{
			stack[stacked++] = node->first_child_node_index;
		}
	}
}

/* the menus' clock's frame of a looped animation frame_count frames long,
at the Spartan's own time (its idle's phase): in whole rounds of it, so
that a long clock loses nothing */
static real spartan_loop_frame(
	struct menu_spartan const *spartan,
	short frame_count,
	unsigned long milliseconds)
{
	/* (a round of 100 ms a frame: frame_count * 3 frames, a whole number of
	the animation's) */
	unsigned long round = (unsigned long)MAX(frame_count, 1) * 100UL;
	unsigned long at = (milliseconds + spartan->variation.idle_phase) % round;

	return (real)fmod((double)at * SPARTAN_FRAMES_PER_SECOND / 1000.0, (double)MAX(frame_count, 1));
}

static char const *spartan_fidget_name(
	short kind)
{
	static char const *const names[NUMBER_OF_SPARTAN_FIDGETS] = { "reload", "weapon check", "look around", "crouch" };

	return kind >= 0 && kind < NUMBER_OF_SPARTAN_FIDGETS ? names[kind] : "none";
}

/* the fidget a Spartan shows at the menus' clock's milliseconds (FALSE:
none, the idle alone): its place's last, unless it began before the
Spartan was shown and had settled, or is over */
static boolean spartan_fidget_now(
	struct menu_spartan const *spartan,
	short index,
	unsigned long milliseconds,
	short *kind,
	unsigned long *start,
	struct spartan_fidget_pose *pose)
{
	struct animation_graph *graph;
	struct animation const *animation;
	short animation_index;

	*kind = menu_spartan.schedule.kind[index];
	*start = menu_spartan.schedule.start[index];
	if (*kind == NONE || *start < spartan->made_milliseconds + SPARTAN_FIDGET_SETTLE || *start > milliseconds)
		return FALSE;
	animation_index = menu_spartan.fidget_animations[*kind];
	if (animation_index == NONE || menu_spartan.animation_graph == NONE)
		return FALSE;
	graph = animation_graph_definition_get(menu_spartan.animation_graph);
	if (animation_index < 0 || animation_index >= graph->animations.count)
		return FALSE;
	animation = (struct animation const *)graph->animations.address + animation_index;
	return spartan_fidget_progress(*kind, milliseconds - *start, animation->frame_count, pose);
}

/* the aiming screen's (the rifle's, yaw and pitch radians from ahead)
and the looking screen's (the head's, head_yaw on from the aim) over a
pose, as a unit's are (units.c's unit_preprocess_node_orientations) */
static void spartan_aim(
	struct animation_graph *graph,
	short node_count,
	real yaw,
	real pitch,
	real head_yaw,
	real_orientation *pose)
{
	if (menu_spartan.aim_animation != NONE && menu_spartan.aim_animation < graph->animations.count)
	{
		struct animation const *aim = (struct animation const *)graph->animations.address + menu_spartan.aim_animation;

		if (aim->node_count == node_count)
			aiming_screen_apply(aim, &menu_spartan.aim_bounds, yaw, pitch, pose);
	}
	if (menu_spartan.look_animation != NONE && menu_spartan.look_animation < graph->animations.count)
	{
		struct animation const *look = (struct animation const *)graph->animations.address + menu_spartan.look_animation;

		if (look->node_count == node_count)
			aiming_screen_apply(look, &menu_spartan.look_bounds, head_yaw, 0.0f, pose);
	}
}

/* a matrix as an orientation (its rotation, position and scale) */
static void spartan_matrix_orientation(
	real_matrix4x3 const *matrix,
	real_orientation *orientation)
{
	matrix4x3_rotation_to_quaternion(matrix, &orientation->rotation);
	orientation->translation = matrix->position;
	orientation->scale = matrix->scale;
}

/* the rifle class's weapon IK over a Spartan's posed nodes, as a unit's
(units.c's unit_postprocess_node_matrices, objects.c's
object_inverse_kinematics): each IK point's hand (its marker's node, the
arm its parent and grandparent) put where its marker is on the rifle's
marker, hold of the way from where the pose has it (0: the pose's, as a
unit's IK is off while it reloads or readies); the nodes under the arm's
moved with it */
static void spartan_weapon_ik(
	struct menu_spartan *spartan,
	struct object_datum const *object,
	struct model const *model,
	long model_index,
	short node_count,
	real hold)
{
	struct object_datum *rifle = object_try_and_get(spartan->rifle_index);
	struct object_definition *rifle_definition;
	boolean mirrored = TEST_FLAG(object->object.flags, _object_mirrored_bit);
	short point_index;

	if (!rifle || !spartan->rifle_posed || hold <= 0.0f)
		return;
	rifle_definition = object_definition_get(rifle->definition_index);
	if (rifle_definition->object.model.index == NONE)
		return;
	hold = hold > 1.0f ? 1.0f : hold;
	for (point_index = 0; point_index < menu_spartan.ik_point_count; point_index++)
	{
		struct animation_graph_ik_point const *point = &menu_spartan.ik_points[point_index];
		struct object_marker hand_marker, grip_marker;
		real_matrix4x3 before[3], inverse, desired, moved[3];
		short arm[3], node_index, joint;

		if (!model_get_marker_by_name(model_index, point->marker_name, object->object.region_permutations, NULL, 0,
				spartan->body_nodes, mirrored, &hand_marker, 1) ||
			!model_get_marker_by_name(rifle_definition->object.model.index, point->attached_to_marker_name,
				rifle->object.region_permutations, NULL, 0, spartan->rifle_nodes,
				TEST_FLAG(rifle->object.flags, _object_mirrored_bit), &grip_marker, 1))
		{
			continue;
		}
		/* (the hand, the forearm, the upper arm) */
		arm[2] = hand_marker.node_index;
		if (arm[2] < 0 || arm[2] >= node_count)
			continue;
		arm[1] = ((struct model_node const *)model->nodes.address + arm[2])->parent_node_index;
		if (arm[1] < 0 || arm[1] >= node_count)
			continue;
		arm[0] = ((struct model_node const *)model->nodes.address + arm[1])->parent_node_index;
		if (arm[0] < 0 || arm[0] >= node_count)
			continue;
		matrix4x3_inverse(&hand_marker.node_matrix, &inverse);
		matrix4x3_multiply(&grip_marker.matrix, &inverse, &desired);
		if (hold < 1.0f)
		{
			real_orientation posed, held, blended;

			spartan_matrix_orientation(&spartan->body_nodes[arm[2]], &posed);
			spartan_matrix_orientation(&desired, &held);
			orientations_interpolate(&posed, &held, hold, &blended);
			matrix4x3_from_orientation(&desired, &blended);
		}
		for (joint = 0; joint < 3; joint++)
			before[joint] = spartan->body_nodes[arm[joint]];
		inverse_kinematics_adjust_matrices(&desired, &spartan->body_nodes[arm[0]], &spartan->body_nodes[arm[1]],
			&spartan->body_nodes[arm[2]]);
		/* (each joint's move: after times before's inverse) */
		for (joint = 0; joint < 3; joint++)
		{
			matrix4x3_inverse(&before[joint], &inverse);
			matrix4x3_multiply(&spartan->body_nodes[arm[joint]], &inverse, &moved[joint]);
		}
		/* (a node under the arm, a finger or a twist, moved as its nearest
		joint of it above it is) */
		for (node_index = 0; node_index < node_count; node_index++)
		{
			short above = ((struct model_node const *)model->nodes.address + node_index)->parent_node_index;
			short steps = 0;

			if (node_index == arm[0] || node_index == arm[1] || node_index == arm[2])
				continue;
			while (above >= 0 && above < node_count && steps++ < MAXIMUM_NODES_PER_MODEL)
			{
				for (joint = 2; joint >= 0; joint--)
				{
					if (above == arm[joint])
						break;
				}
				if (joint >= 0)
				{
					real_matrix4x3 node = spartan->body_nodes[node_index];

					matrix4x3_multiply(&moved[joint], &node, &spartan->body_nodes[node_index]);
					break;
				}
				above = ((struct model_node const *)model->nodes.address + above)->parent_node_index;
			}
		}
	}
}

/* a Spartan's pose at the menus' clock's milliseconds, its nodes' matrices
as the game would place them (spartan->body_nodes; FALSE: none, the game's
own drawn): its idle at its own time, and a fidget blended over it; its
rifle's nodes moved with its hand (spartan->rifle_nodes). Drawing only:
the game's objects are not changed */
static boolean spartan_pose(
	struct menu_spartan *spartan,
	short index,
	unsigned long milliseconds)
{
	real_orientation rest[MAXIMUM_NODES_PER_MODEL];
	real_orientation idle[MAXIMUM_NODES_PER_MODEL];
	real_orientation fidget[MAXIMUM_NODES_PER_MODEL];
	struct object_datum *object = object_try_and_get(spartan->object_index);
	struct object_definition *definition;
	struct animation_graph *graph;
	struct model *model;
	struct animation const *idle_animation;
	struct spartan_fidget_pose fidget_pose;
	short node_count, kind;
	unsigned long start;
	boolean fidgeting;
	real_vector3d left;
	real_point3d origin;
	real_matrix4x3 root;
	real camera_pitch, camera_field_of_view, camera_turn, aim_yaw, aim_pitch;

	spartan->posed = FALSE;
	spartan->rifle_posed = FALSE;
	if (!object || menu_spartan.animation_graph == NONE || menu_spartan.idle_animation == NONE)
		return FALSE;
	definition = object_definition_get(object->definition_index);
	if (definition->object.model.index == NONE)
		return FALSE;
	model = model_definition_get(definition->object.model.index);
	graph = animation_graph_definition_get(menu_spartan.animation_graph);
	node_count = (short)model->nodes.count;
	if (node_count <= 0 || node_count > MAXIMUM_NODES_PER_MODEL || menu_spartan.idle_animation >= graph->animations.count)
	{
		return FALSE;
	}
	idle_animation = (struct animation const *)graph->animations.address + menu_spartan.idle_animation;
	if (idle_animation->node_count != node_count)
		return FALSE;

	/* (the idle, at its own time, aimed ahead as a unit's is) */
	model_get_node_orientations(model, rest);
	spartan_animation_pose(model, idle_animation, spartan_loop_frame(spartan, idle_animation->frame_count, milliseconds),
		TRUE, rest, node_count, idle);
	spartan_camera_angles(&camera_pitch, &camera_field_of_view, &camera_turn);
	aim_yaw = spartan_camera_angles_read[3] * 3.14159265f / 180.0f;
	aim_pitch = spartan_camera_angles_read[4] * 3.14159265f / 180.0f;
	spartan_aim(graph, node_count, aim_yaw, aim_pitch, 0.0f, idle);

	/* (a fidget over it) */
	fidgeting = spartan_fidget_now(spartan, index, milliseconds, &kind, &start, &fidget_pose);
	if (fidgeting && fidget_pose.weight > 0.0f)
	{
		struct animation const *animation =
			(struct animation const *)graph->animations.address + menu_spartan.fidget_animations[kind];

		switch (kind)
		{
		case SPARTAN_FIDGET_RELOAD:
		case SPARTAN_FIDGET_WEAPON_CHECK:
			spartan_animation_pose(model, animation, fidget_pose.frame, FALSE, idle, node_count, fidget);
			break;
		case SPARTAN_FIDGET_CROUCH:
			spartan_animation_pose(model, animation, spartan_loop_frame(spartan, animation->frame_count, milliseconds),
				TRUE, rest, node_count, fidget);
			spartan_aim(graph, node_count, aim_yaw, aim_pitch, 0.0f, fidget);
			break;
		case SPARTAN_FIDGET_LOOK:
			/* (the aim turned, and the head on from it) */
			spartan_animation_pose(model, idle_animation, spartan_loop_frame(spartan, idle_animation->frame_count,
				milliseconds), TRUE, rest, node_count, fidget);
			spartan_aim(graph, node_count, aim_yaw + fidget_pose.look * SPARTAN_LOOK_AIM_DEGREES * 3.14159265f / 180.0f,
				aim_pitch + (real)fabs(fidget_pose.look) * SPARTAN_LOOK_PITCH_DEGREES * 3.14159265f / 180.0f,
				fidget_pose.look * SPARTAN_LOOK_HEAD_DEGREES * 3.14159265f / 180.0f, fidget);
			break;
		default:
			memcpy(fidget, idle, sizeof(real_orientation) * node_count);
			break;
		}
		spartan_orientations_blend(node_count, idle, fidget,
			kind == SPARTAN_FIDGET_CROUCH ? fidget_pose.weight * SPARTAN_CROUCH_DEPTH : fidget_pose.weight);
	}
	else
		memcpy(fidget, idle, sizeof(real_orientation) * node_count);

	/* (its nodes placed as the game places them: object_compute_node_matrices,
	from its place, its facing and its definition's origin offset) */
	cross_product3d(&object->object.up, &object->object.forward, &left);
	origin.x = object->object.position.x + object->object.forward.i * definition->object.origin_offset.x +
		left.i * definition->object.origin_offset.y + object->object.up.i * definition->object.origin_offset.z;
	origin.y = object->object.position.y + object->object.forward.j * definition->object.origin_offset.x +
		left.j * definition->object.origin_offset.y + object->object.up.j * definition->object.origin_offset.z;
	origin.z = object->object.position.z + object->object.forward.k * definition->object.origin_offset.x +
		left.k * definition->object.origin_offset.y + object->object.up.k * definition->object.origin_offset.z;
	matrix4x3_from_point_and_vectors(&root, &origin, &object->object.forward, &object->object.up);
	spartan_node_matrices(model, node_count, fidget, &root, spartan->body_nodes);
	spartan->posed = TRUE;

	/* (its rifle where its hand has it: the game's rifle moved as the hand
	it hangs from is moved from the game's pose to this) */
	if (spartan->rifle_index != NONE)
	{
		struct object_datum *rifle = object_try_and_get(spartan->rifle_index);

		if (rifle && rifle->object.parent_object_index == spartan->object_index &&
			rifle->object.parent_node_index >= 0 && rifle->object.parent_node_index < node_count)
		{
			struct object_definition *rifle_definition = object_definition_get(rifle->definition_index);
			short rifle_node_count = rifle_definition->object.model.index == NONE ? 0 :
				(short)MIN(model_definition_get(rifle_definition->object.model.index)->nodes.count, MAXIMUM_NODES_PER_MODEL);
			real_matrix4x3 const *game_body = object_get_node_matrices(spartan->object_index);
			real_matrix4x3 const *game_rifle = object_get_node_matrices(spartan->rifle_index);
			real_matrix4x3 inverse, moved;
			short node_index;

			matrix4x3_inverse(&game_body[rifle->object.parent_node_index], &inverse);
			matrix4x3_multiply(&spartan->body_nodes[rifle->object.parent_node_index], &inverse, &moved);
			for (node_index = 0; node_index < rifle_node_count; node_index++)
				matrix4x3_multiply(&moved, &game_rifle[node_index], &spartan->rifle_nodes[node_index]);
			spartan->rifle_posed = rifle_node_count > 0;
		}
	}

	/* (its hands on the rifle as a unit's are, but while it reloads or
	readies: its own animation's hands then, blended to and from) */
	spartan_weapon_ik(spartan, object, model, definition->object.model.index, node_count,
		fidgeting && (kind == SPARTAN_FIDGET_RELOAD || kind == SPARTAN_FIDGET_WEAPON_CHECK) ?
		1.0f - fidget_pose.weight : 1.0f);

	/* (debug: as a fidget begins and as it ends) */
	if (getenv("HALO_MENU_SPARTAN_TRACE"))
	{
		if (fidgeting && spartan->traced_start != start)
		{
			spartan->traced_start = start;
			spartan->traced_kind = kind;
			platform_log("menu spartan: %d: %s begins at %lu ms (the menus' clock %lu), blended in from the idle",
				index, spartan_fidget_name(kind), start, milliseconds);
		}
		else if (!fidgeting && spartan->traced_kind != NONE)
		{
			platform_log("menu spartan: %d: %s over at %lu ms, back to the idle", index,
				spartan_fidget_name(spartan->traced_kind), milliseconds);
			spartan->traced_kind = NONE;
		}
	}
	return TRUE;
}

/* debug: how far (world units, the most of any node) the pose drawn of the
game's idle frame is from the game's own nodes: none, but for the
interpolation between the ticks */
static real spartan_pose_check(
	struct menu_spartan const *spartan)
{
	struct object_datum *object = object_try_and_get(spartan->object_index);
	real_orientation rest[MAXIMUM_NODES_PER_MODEL], idle[MAXIMUM_NODES_PER_MODEL];
	real_matrix4x3 nodes[MAXIMUM_NODES_PER_MODEL];
	struct object_definition *definition;
	struct model *model;
	struct animation_graph *graph;
	struct animation const *animation;
	real_matrix4x3 const *game;
	real_matrix4x3 root;
	real_vector3d left;
	real_point3d origin;
	real most = 0.0f;
	short node_index;

	if (!object || menu_spartan.animation_graph == NONE || object->object.animation.state.index == NONE)
		return -1.0f;
	definition = object_definition_get(object->definition_index);
	model = model_definition_get(definition->object.model.index);
	graph = animation_graph_definition_get(menu_spartan.animation_graph);
	animation = (struct animation const *)graph->animations.address + object->object.animation.state.index;
	if (model->nodes.count > MAXIMUM_NODES_PER_MODEL)
		return -1.0f;
	model_get_node_orientations(model, rest);
	spartan_animation_frame(model, animation, object->object.animation.state.frame_index, rest,
		(short)model->nodes.count, idle);
	cross_product3d(&object->object.up, &object->object.forward, &left);
	origin.x = object->object.position.x + object->object.forward.i * definition->object.origin_offset.x +
		left.i * definition->object.origin_offset.y + object->object.up.i * definition->object.origin_offset.z;
	origin.y = object->object.position.y + object->object.forward.j * definition->object.origin_offset.x +
		left.j * definition->object.origin_offset.y + object->object.up.j * definition->object.origin_offset.z;
	origin.z = object->object.position.z + object->object.forward.k * definition->object.origin_offset.x +
		left.k * definition->object.origin_offset.y + object->object.up.k * definition->object.origin_offset.z;
	matrix4x3_from_point_and_vectors(&root, &origin, &object->object.forward, &object->object.up);
	spartan_node_matrices(model, (short)model->nodes.count, idle, &root, nodes);
	game = (real_matrix4x3 const *)object_header_block_get(spartan->object_index, &object->object.node_matrices);
	for (node_index = 0; node_index < model->nodes.count; node_index++)
		most = MAX(most, (real)sqrt(distance_squared3d(&nodes[node_index].position, &game[node_index].position)));
	return most;
}

/* ---------- public code */

boolean menu_spartan_available(
	void)
{
	if (menu_spartan.generation != cache_files_tags_generation())
		spartan_tags_find();
	return menu_spartan.spartan_definition != NONE;
}

void menu_spartan_place(
	short index,
	short color_index,
	real x,
	real bottom,
	real height,
	boolean lineup)
{
	struct menu_spartan *spartan;

	if (index < 0 || index >= MAXIMUM_MENU_SPARTANS || !menu_spartan_available())
		return;
	spartan = &menu_spartan.spartans[index];
	spartan->asked_frame = menu_spartan.frame;
	spartan->x = x;
	spartan->bottom = bottom;
	spartan->height = height;
	spartan->color_index = color_index;
	spartan->lineup = lineup;
}

real menu_spartan_feet_lower(
	short index,
	boolean lineup)
{
	struct spartan_variation variation;

	if (index < 0 || index >= MAXIMUM_MENU_SPARTANS)
		return 0.0f;
	spartan_variation_find(index, lineup, &variation);
	return SPARTAN_LINEUP_NEARER_LOWER * variation.nearer;
}

real_rgb_color menu_spartan_color(
	short index,
	short color_index,
	unsigned long milliseconds)
{
	long count = (long)player_profile_number_of_available_primary_colors();
	unsigned long step = SPARTAN_RANDOM_HOLD + SPARTAN_RANDOM_BLEND;
	unsigned long round, at;
	long color;
	real blend;
	real_rgb_color from, to;

	if (color_index == MENU_SPARTAN_TEAM_RED)
		return *global_real_rgb_red;
	if (color_index == MENU_SPARTAN_TEAM_BLUE)
		return *global_real_rgb_blue;
	if (color_index != MENU_SPARTAN_RANDOM || count <= 0)
		return player_profile_get_rgb_color(color_index);
	/* (where in the round of the colors it is, each Spartan side by side an
	eighth of the round on) */
	round = (unsigned long)count * step;
	at = (milliseconds % round + round * (unsigned long)(index & 7) / 8) % round;
	color = (long)(at / step);
	from = player_profile_get_rgb_color(color);
	to = player_profile_get_rgb_color((color + 1) % count);
	/* (eased in and out over the blend) */
	blend = at % step < SPARTAN_RANDOM_HOLD ? 0.0f :
		(real)(at % step - SPARTAN_RANDOM_HOLD) / (real)SPARTAN_RANDOM_BLEND;
	blend = blend * blend * (3.0f - 2.0f * blend);
	from.red += (to.red - from.red) * blend;
	from.green += (to.green - from.green) * blend;
	from.blue += (to.blue - from.blue) * blend;
	return from;
}

short menu_spartan_test_colors(
	short colors[MAXIMUM_MENU_SPARTAN_PLACES])
{
	char const *test = getenv("HALO_MENU_SPARTAN_TEST");
	short count = 0;

	while (test && *test && count < MAXIMUM_MENU_SPARTAN_PLACES)
	{
		char *end;
		long color = strtol(test, &end, 10);

		if (end == test)
			break;
		colors[count++] = (short)color;
		test = end;
	}
	return count;
}

boolean menu_spartan_object(
	long object_index)
{
	short index;

	if (object_index == NONE)
		return FALSE;
	for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
	{
		if (menu_spartan.spartans[index].object_index == object_index ||
			menu_spartan.spartans[index].rifle_index == object_index)
		{
			return TRUE;
		}
	}
	return FALSE;
}

void menu_spartan_frame(
	void)
{
	short index;
	boolean any = FALSE;

	/* (once a frame: the screens drawn after it ask for this frame's) */
	if (menu_spartan.updated_frame == render.frame_index && menu_spartan.frame)
		return;
	menu_spartan.updated_frame = render.frame_index;
	if (!menu_spartan_available())
	{
		menu_spartan.frame++;
		return;
	}
	for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
	{
		struct menu_spartan *spartan = &menu_spartan.spartans[index];

		if (spartan->object_index != NONE && !object_try_and_get(spartan->object_index))
		{
			spartan->object_index = NONE;
			spartan->rifle_index = NONE;
		}
		any |= spartan->object_index != NONE;
	}
	/* (the fidgets' times, to the menus' clock now) */
	spartan_schedule_advance(&menu_spartan.schedule, ui_widget_port_milliseconds());
	/* (the stage where the scene's camera is as the first is made: inside
	the map, where nothing of the scene is drawn in the Spartans' view, nor
	they in the scene's) */
	if (!any)
	{
		menu_spartan.stage = render.camera.position;
		menu_spartan.staged = TRUE;
	}
	for (index = 0; index < MAXIMUM_MENU_SPARTANS; index++)
	{
		struct menu_spartan *spartan = &menu_spartan.spartans[index];
		boolean asked = spartan->asked_frame == menu_spartan.frame;

		if (!asked)
		{
			if (spartan->object_index != NONE)
				spartan_delete(spartan);
			continue;
		}
		spartan_view_find(spartan);
		spartan_variation_find(index, spartan->lineup, &spartan->variation);
		if (spartan->object_index == NONE)
		{
			if (!spartan_make(spartan))
				continue;
		}
		else
		{
			real_point3d position;
			real_vector3d forward, up;

			/* (moved only as its place in the view changes: the screen's
			layout, the screen's shape) */
			spartan_stand(spartan, &position, &forward, &up);
			if (memcmp(&position, &spartan->position, sizeof(position)) ||
				memcmp(&forward, &spartan->forward, sizeof(forward)))
			{
				object_set_position(spartan->object_index, &position, &forward, &up);
				spartan->position = position;
				spartan->forward = forward;
			}
			spartan_idle_loop(spartan);
		}
		spartan_color_set(spartan->object_index,
			menu_spartan_color(index, spartan->color_index, ui_widget_port_milliseconds()));
	}
	menu_spartan.frame++;
}

void menu_spartan_draw(
	short x,
	short y,
	short first,
	short count)
{
	long ui_offset = (halo_screen_width() - MENUS_WIDTH) / 2;
	struct render_camera camera, scene_camera;
	struct render_frustum frustum, scene_frustum;
	struct render_lighting lighting;
	real_rectangle2d bounds;
	short index, drawn = 0;
	unsigned long milliseconds = ui_widget_port_milliseconds();
	real pitch, field_of_view, turn;

	if (!menu_spartan.staged)
		return;
	spartan_camera_angles(&pitch, &field_of_view, &turn);
	spartan_lighting(&lighting);
	/* (the renderer's camera each view's while it draws, as the models' level
	of detail is chosen by it; the scene's again after) */
	scene_camera = render.camera;
	scene_frustum = render.frustum;
	/* (its viewport where it is on the screen, not shifted with the menus) */
	halo_screen_ui_offset(FALSE);
	for (index = (short)MAX(first, 0); index < first + count && index < MAXIMUM_MENU_SPARTANS; index++)
	{
		struct menu_spartan *spartan = &menu_spartan.spartans[index];
		real const *view = spartan->view;

		if (spartan->object_index == NONE || !object_try_and_get(spartan->object_index) ||
			view[2] <= view[0] || view[3] <= view[1])
		{
			continue;
		}
		/* its view's camera: at its place on the stage, its window its
		rectangle on the screen (the menus' 640 centred in its width), its
		viewport that rectangle's part on the screen */
		csmemset(&camera, 0, sizeof(camera));
		spartan_camera_at(spartan, &camera.position);
		spartan_camera_axes(&camera.forward, &camera.up);
		camera.vertical_field_of_view = field_of_view;
		camera.z_near = SPARTAN_VIEW_NEAR;
		camera.z_far = SPARTAN_VIEW_FAR;
		camera.window_bounds.x0 = (short)(view[0] + x + ui_offset);
		camera.window_bounds.y0 = (short)(view[1] + y);
		camera.window_bounds.x1 = (short)(view[2] + x + ui_offset);
		camera.window_bounds.y1 = (short)(view[3] + y);
		camera.viewport_bounds.x0 = (short)MAX(camera.window_bounds.x0, 0);
		camera.viewport_bounds.y0 = (short)MAX(camera.window_bounds.y0, 0);
		camera.viewport_bounds.x1 = (short)MIN(camera.window_bounds.x1, halo_screen_width());
		camera.viewport_bounds.y1 = (short)MIN(camera.window_bounds.y1, MENUS_HEIGHT);
		if (camera.viewport_bounds.x1 <= camera.viewport_bounds.x0 ||
			camera.viewport_bounds.y1 <= camera.viewport_bounds.y0)
		{
			continue;
		}
		render_camera_build_frustum_bounds(&camera, &bounds);
		render_camera_build_frustum(&camera, &bounds, &frustum, TRUE);
		render.camera = camera;
		render.frustum = frustum;
		rasterizer_view_begin(&camera, &frustum);
		rasterizer_models_begin(FALSE);
		/* (posed on the menus' clock: menu_spartan_node_matrices gives
		its nodes as it draws them) */
		spartan_pose(spartan, index, milliseconds);
		menu_spartan.drawing = spartan;
		render_object_alone(spartan->object_index, &lighting);
		menu_spartan.drawing = NULL;
		rasterizer_models_end();
		rasterizer_view_end();
		drawn++;
	}
	halo_screen_ui_offset(TRUE);
	halo_hdr_interface();
	render.camera = scene_camera;
	render.frustum = scene_frustum;
	/* (debug: where each is drawn, and the scene's camera, a line a
	second or so) */
	if (getenv("HALO_MENU_SPARTAN_TRACE") && drawn && ++menu_spartan.traced_frames >= 60)
	{
		menu_spartan.traced_frames = 0;
		platform_log("menu spartan: the last view %d %d %d %d (viewport %d %d %d %d), %d drawn; scene camera at %.3f %.3f %.3f "
			"facing %.3f %.3f %.3f", camera.window_bounds.x0, camera.window_bounds.y0, camera.window_bounds.x1,
			camera.window_bounds.y1, camera.viewport_bounds.x0, camera.viewport_bounds.y0, camera.viewport_bounds.x1,
			camera.viewport_bounds.y1, drawn, scene_camera.position.x, scene_camera.position.y, scene_camera.position.z,
			scene_camera.forward.i, scene_camera.forward.j, scene_camera.forward.k);
		for (index = (short)MAX(first, 0); index < first + count && index < MAXIMUM_MENU_SPARTANS; index++)
		{
			struct menu_spartan const *spartan = &menu_spartan.spartans[index];
			struct object_datum *object = spartan->object_index == NONE ? NULL : object_try_and_get(spartan->object_index);
			real_point2d feet, head;
			real_point3d top;
			struct spartan_fidget_pose pose;
			short kind;
			unsigned long start;

			if (!object)
				continue;
			platform_log("menu spartan: %d: the game's idle frame posed %.4f from the game's nodes", index,
				spartan_pose_check(spartan));
			if (spartan_fidget_now(spartan, index, milliseconds, &kind, &start, &pose))
			{
				platform_log("menu spartan: %d: %s, %lu ms in, weight %.2f, frame %.1f, look %.2f", index,
					spartan_fidget_name(kind), milliseconds - start, pose.weight, pose.frame, pose.look);
			}
			else
			{
				platform_log("menu spartan: %d: idle, frame %.1f (its own time %lu ms on), next fidget at %lu ms; "
					"turned %.2f degrees, nearer %.2f%s", index,
					menu_spartan.idle_animation == NONE ? 0.0f : spartan_loop_frame(spartan,
						((struct animation const *)animation_graph_definition_get(menu_spartan.animation_graph)->animations.address +
							menu_spartan.idle_animation)->frame_count, milliseconds),
					spartan->variation.idle_phase, menu_spartan.schedule.next[index], spartan->variation.turn_degrees,
					spartan->variation.nearer, spartan->lineup ? " (lineup)" : "");
			}
			top = object->object.position;
			top.z += SPARTAN_HEIGHT;
			feet.x = feet.y = head.x = head.y = 0.0f;
			render_camera_world_to_screen(&camera, &frustum, &object->object.position, &feet);
			render_camera_world_to_screen(&camera, &frustum, &top, &head);
			platform_log("menu spartan: %d: color %d (armor %.3f %.3f %.3f), animation %d frame %d, on the screen feet %.1f %.1f head %.1f %.1f, "
				"facing %.3f %.3f %.3f up %.3f %.3f %.3f, rifle %s", index, spartan->color_index,
				object->object.base_change_colors[0].red, object->object.base_change_colors[0].green,
				object->object.base_change_colors[0].blue, object->object.animation.state.index, object->object.animation.state.frame_index, feet.x, feet.y,
				head.x, head.y, object->object.forward.i, object->object.forward.j, object->object.forward.k,
				object->object.up.i, object->object.up.j, object->object.up.k,
				spartan->rifle_index != NONE ? "held" : "none");
		}
	}
}

real_matrix4x3 const *menu_spartan_node_matrices(
	long object_index,
	real_matrix4x3 const *nodes)
{
	struct menu_spartan const *spartan = menu_spartan.drawing;

	if (!spartan || object_index == NONE || !spartan->posed)
		return nodes;
	if (object_index == spartan->object_index)
		return spartan->body_nodes;
	if (object_index == spartan->rifle_index && spartan->rifle_posed)
		return spartan->rifle_nodes;
	return nodes;
}
