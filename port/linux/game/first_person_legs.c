/*
FIRST_PERSON_LEGS.C

Your own legs in first person (Video Setup > FOV and Viewmodels > Legs): the
player's own body, which the game animates as everyone else sees it but does
not draw for its own player, drawn from its belly down. Look down while
standing, running, strafing, crouching or jumping and the legs move as the
body's animation moves them.

Only the drawing is changed, from a copy of the body's pose; the body itself,
its hit boxes and markers, are the game's as ever. The legs are drawn in the
world with the world's camera, so they keep their place and size at any field
of view. How the copy relates to the eye, each frame, is fixed by these rules,
none of which depends on where the view looks but for the aim's heading:

- The pose is the body's own, as it is drawn now, placed about the view drawn
  as the first-person weapon is (render_interpolation_view_body): at its eye
  and its heading this frame, the yaw the mouse gave it late in the frame
  included. The body is drawn between the ticks, a tick behind, but the view
  drawn on foot is the newest step's (display.direct_camera), a tick's motion
  on and aimed in the frame (#697); and the body faces the aim only once the
  aim has turned far enough from it, then turns after it a tick at a time,
  which swung the legs away from the view and back with a jerk each tick as
  the view turned (#826).
- From there its lower body is turned about the eye to a heading of its own
  (first_person_legs_lower_get), on this screen alone: planted while the aim
  turns within a dead zone, turning after it beyond, and walking at an angle,
  facing the way it walks; where the game plays another walk than the one
  that heading walks (a sideways walk at an angle), the walk it walks
  (first_person_legs_moving_pose). Turned to the view's heading, the legs
  swung with every turn of the view, their feet sliding, and walking at an
  angle they faced the aim (#772). It turns by the frames' time, as smoothly
  at any frame rate and any number of ticks a second.
- The eye is over the front of its hips and belly at rest, along that
  heading (first_person_legs_front): the game's eye is over the middle of the
  body, its spine, where looking down showed the top of the hips and the
  belly before the eye, and the armour's belly is deep; a person's eyes are
  over the front of their belly. (Set back 37 cm, the thighs reached forward
  under the eye from behind it, #772.) The heading is level, so the feet stay
  put however the view tilts (#724).
- Its chest and all above it (the neck, the head, the arms: the first-person
  arms are the weapon's) are folded into the chest's joint, and its belly,
  closed so, is flattened onto its joint's height, a lid over the hips. The
  eye is then never inside the body nor near enough for the near plane
  (0.0625 world units, about 19 cm, before the eye) to cut it, standing,
  walking or crouching: the chest's armour, which reached up to the eye and
  23 cm before it, was what the near plane cut, showing it hollow and the
  open collar; and looking down shows the belly as a lid, not a dark cone of
  it rising toward the eye.
- The game's eye is just above the joint of the body's head, 10 to 11 cm
  below the eye standing, walking and crouching. Only where the eye is lower
  in the body than that (a crouch jump: the eye comes down to crouching
  height while the body keeps its airborne pose, its hips at the eye, #692)
  is the body lowered, as far as keeps its head FIRST_PERSON_LEGS_HEAD_BELOW
  below the eye, as the game's own poses have it; standing and crouching on
  the ground it is never lowered.

Before this, the chest was drawn, leant back about the hips by a search each
frame as far as kept it clear of the near plane, coming upright again over a
second, and whatever the near plane still cut folded away, a part at a time:
the lean stepped as each frame's pose and view came, and parts folded and
came back from frame to frame, so the legs shook and flickered looking down,
the skin between the hips and a chest leant back as far as 86 degrees
stretched, and the parts folded showed as slivers (#772).

The body's shadow is drawn too, placed as the legs are, but whole
(first_person_legs_shadow_node_matrices): its legs stand on their own
shadow.

In the VR mode the whole body is drawn but its head and arms (the arms are
the VR mode's own, from its shoulders to the controllers: vr_hands.c), and
where the real body is rather than the game's: the game's eye is where the
head was at the last recentring and the unit faces where the gun aims, but
the head moves about and turns on its own. So the copy of the pose stands
upright and square over its hips (the game's leans into its aim, the chest
turned to the gun while the feet stay planted), its neck under the real
head, turned to the body's heading (vr.c's: the head's, which it follows
only past a turn and slowly; the holsters' and the arms' shoulders' too),
and the chest closes the waist; looking down shows the chest, the legs and
the feet below the head, as the arms reach from its shoulders. Its upper
body leans back about the hips as far as keeps what the eyes see of it out
of their near plane, and the collar's opening out of their view
(first_person_legs_lean): looking down, the eyes nod forward over the chest.
Its neck is a little behind the real one (the armour's chest is deeper than
a person's). The headset's eyes are where the real head is, not over the
body's spine, so its body is not set back.

In the VR mode's seats (vr.vehicle_view = "first_person": vr_seat.c) the
body is drawn too, once the eye is in the seat and the body is seated
(first_person_legs_vr_seated): as the game poses it in the seat (its legs
the seat's), but moved so that the top of its neck is under where the
real one is with the head as it was recentred (below and behind the seat's
eye, or a cabin's: vr_cabin.c), its upper body turned about its hips toward
the real neck (as far as FIRST_PERSON_LEGS_SEAT_TURN_MOST) and leant back
clear of the eyes as on foot, its head folded away and, where the VR mode
draws its own arms (vr_hands.c: the hands alone, or with the weapon), its
arms too. In the
Scorpion's hatch (vr.scorpion_view = "hatch") it stands, its legs hanging
straight down into the turret, turned to the body's heading. The game's
body, what others see, is as ever: nothing of the unit changes.
*/

#include "cseries.h"
#include "real_math.h"
#include "objects/objects.h"
#include "models/model_definitions.h"
#include "models/model_animation_definitions.h"
#include "game/game.h"
#include "game/players.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "units/biped_definitions.h"
#include "render/render.h"
#include "rasterizer/rasterizer.h"
#include <xtl.h>
#include "rasterizer/xbox/rasterizer_xbox.h"

#include "object_mesh.h"


#include <stdlib.h>
#include <string.h>

#ifdef HALO_VR
#include "vr_body.h"

/* port/linux/game/vr_render.c */
boolean vr_render_body_anchor(real_point3d *neck, real *yaw);
boolean vr_render_body_eyes(real_point3d eyes[2], struct render_frustum const *views[2], real *near, real *seconds);
boolean vr_render_body_seat(long object_index, struct vr_body_seat *seat);
#endif

/* port/linux/src/port_config.c */
#include "player_settings.h"
/* port/linux/game/render_interpolation.c */
boolean render_interpolation_view_body(long unit_index, real_matrix4x3 *move, real_vector3d *heading);
real render_interpolation_game_time_sec(long ticks);
real render_interpolation_ticks_behind(void);
#ifdef HALO_VR
/* port/linux/src/vr.c */
int halo_vr_running(void);
#endif

enum
{
	FIRST_PERSON_LEGS_MAXIMUM_NODES = 64,
};

/* a part folded away: its node's scale (the chest and all above it, folded
into the chest's joint; in the VR mode, the head and arms, folded into the
neck and shoulders) */
#define FIRST_PERSON_LEGS_FOLDED 0.001f

/* the flat screen's body (first_person_legs_place): lowered where its head
would be less than this below the eye (world units: about 8 cm; the Master
Chief's head's joint is 10 to 11 cm below it standing, walking and
crouching) */
#define FIRST_PERSON_LEGS_HEAD_BELOW 0.025f
/* ... its belly flattened onto the top of its hips to this of its height, a
lid over them (first_person_legs_node_matrices) */
#define FIRST_PERSON_LEGS_LID 0.05f

/* the flat screen's lower body's heading (first_person_legs_lower_turn),
radians and per second: standing, it stays planted until the aim is more than
FIRST_PERSON_LEGS_DEAD_ZONE from it, then turns after it until within
FIRST_PERSON_LEGS_SETTLED; moving (a throttle of more than
FIRST_PERSON_LEGS_MOVING), it walks the unit's walk (forward, backward, left
or right of its heading) nearest the way it moves, keeping the one it walks
until the way is more than FIRST_PERSON_LEGS_WALK_KEEP past halfway to
another, and turns so that the walk goes the way it moves. It turns as a
spring critically damped, of FIRST_PERSON_LEGS_TURN_RATE (its natural
frequency: nine tenths of the way in about a quarter of a second, the turn
starting and ending smoothly, the same at any frame rate), never more than
FIRST_PERSON_LEGS_TWIST_MOST from the aim. A walk of its own
(first_person_legs_moving_pose) comes in and goes in FIRST_PERSON_LEGS_SWAP_
SECONDS, eased in and out */
#define FIRST_PERSON_LEGS_DEAD_ZONE 1.0471976f
#define FIRST_PERSON_LEGS_SETTLED 0.05f
#define FIRST_PERSON_LEGS_MOVING 0.1f
#define FIRST_PERSON_LEGS_WALK_KEEP 0.17453293f
#define FIRST_PERSON_LEGS_TURN_RATE 15.0f
#define FIRST_PERSON_LEGS_TWIST_MOST 1.5707963f
#define FIRST_PERSON_LEGS_SWAP_SECONDS 0.2f

#ifdef HALO_VR
/* the VR mode's body's upper body leant back from its eyes
(first_person_legs_lean): turned back about its hips in steps of this
(radians), no farther than this, as far as keeps all of it that the eyes see
(within FIRST_PERSON_LEGS_VIEW_MARGIN of its view's edges, world units:
about 2 cm) at least the near plane's distance times
FIRST_PERSON_LEGS_CLEAR_SCALE and FIRST_PERSON_LEGS_CLEAR_MORE (world units:
about 2 cm) before them, and the top of its neck, if seen,
FIRST_PERSON_LEGS_NECK_BEHIND behind the eyes (world units, along its
heading: about 27 cm); coming back upright no faster than
FIRST_PERSON_LEGS_UPRIGHT_SPEED (radians a second). Of the body's surface, so
many points at most are weighed (those that can come that near) */
#define FIRST_PERSON_LEGS_LEAN_STEP 0.025f
#define FIRST_PERSON_LEGS_LEAN_MAXIMUM 0.9f
#define FIRST_PERSON_LEGS_VIEW_MARGIN 0.007f
#define FIRST_PERSON_LEGS_CLEAR_SCALE 1.1f
#define FIRST_PERSON_LEGS_CLEAR_MORE 0.007f
#define FIRST_PERSON_LEGS_NECK_BEHIND 0.09f
#define FIRST_PERSON_LEGS_UPRIGHT_SPEED 1.0f
#define FIRST_PERSON_LEGS_LEAN_POINTS 8192

/* the VR mode's body: its neck this far behind the top of the real neck
(world units, along its heading: about 5 cm), the armour's chest being
deeper than a person's: standing, the front of its chest is then about 10
cm before the eyes (a person's is about under them), not 15, and the eyes
nodding over it see past it to the belt sooner. The holster at the right
hip and the pouch at the left (vr.c's, from the head) are still at the
hips' sides, about 5 cm short of their front */
#define FIRST_PERSON_LEGS_VR_BACK 0.016f

/* the VR mode's body in a seat (first_person_legs_vr_seated): its upper
body turned about its hips toward the real neck no farther than this
(radians: as far as a person seated leans and twists) */
#define FIRST_PERSON_LEGS_SEAT_TURN_MOST 0.6f
/* the VR mode's body's chest as it was drawn last (first_person_legs_vr_back,
for the long gun slung across its back: vr_holster.c): its frame
(at the foot of the chest, up along it to the neck, left across the
shoulders) from the top of the real neck in the body's heading's axes
(world units: forward, left, up), and how far before the frame's middle its
back is (the farthest back of its upper body: negative); and its hips (for
the hip's holster): their middle from the top of the real neck, and how far
left of it their right side is (negative). Before any is drawn, the Master
Chief's standing upright (the chest about 5 cm behind and 32 cm below the
top of the real neck, the back 29 cm behind it; the hips 66 cm below it,
their right side 22 cm right of their middle) */
static struct
{
	real_point3d origin;
	real_vector3d forward, left, up;
	real back;
	real_point3d hips;
	real hips_side;
} first_person_legs_vr_chest =
{
	{ -0.047f / 3.048f, 0.0f, -0.321f / 3.048f },
	{ 1.0f, 0.0f, 0.0f },
	{ 0.0f, 1.0f, 0.0f },
	{ 0.0f, 0.0f, 1.0f },
	-0.294f / 3.048f,
	{ -0.049f / 3.048f, 0.0f, -0.660f / 3.048f },
	-0.22f / 3.048f,
};
#endif

static real_matrix4x3 first_person_legs_matrices[FIRST_PERSON_LEGS_MAXIMUM_NODES];
/* the local player whose unit it is (NONE: no local player's) */
static short first_person_legs_local_player(
	long object_index)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long player_index = local_player_get_player_index(local_player_index);

		if (player_index != NONE && player_get(player_index)->unit_index == object_index)
			return local_player_index;
	}
	return NONE;
}

/* display.first_person_legs, the unit's local player's own (their
profile's: player_settings.c) */
static boolean first_person_legs_enabled(
	long object_index)
{
	static struct
	{
		unsigned long changes;
		boolean read, enabled;
	} players[PLAYER_SETTINGS_SLOTS];
	short local_player_index = first_person_legs_local_player(object_index);
	short slot = PLAYER_SETTINGS_SLOT(local_player_index);

	if (!players[slot].read || players[slot].changes != config_changes())
	{
		players[slot].read = TRUE;
		players[slot].changes = config_changes();
		players[slot].enabled = config_player_boolean(local_player_index, "display.first_person_legs") != 0;
	}
	return players[slot].enabled;
}

/* the player's own unit, seen from inside it: its legs drawn (on foot,
alive, not hidden; in the VR mode, in a seat too: first_person_legs_vr_
seated) */
boolean first_person_legs_wanted(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (!first_person_legs_enabled(object_index) || !object || object->object.type != _object_type_biped ||
		TEST_FLAG(object->object.damage_flags, _object_dead_bit) ||
		TEST_FLAG(object->object.flags, _object_invisible_bit))
	{
		return FALSE;
	}
#ifdef HALO_VR
	if (object->object.parent_object_index != NONE)
	{
		struct vr_body_seat seat;

		return vr_render_body_seat(object_index, &seat);
	}
#endif
	return object->object.parent_object_index == NONE;
}

#ifdef HALO_VR
/* vr_render.c: the player's body is drawn in its seat (the VR mode's
hands drawn there with it, where the game draws none) */
boolean first_person_legs_seated(
	long object_index)
{
	return first_person_legs_wanted(object_index) && object_get(object_index)->object.parent_object_index != NONE;
}
#endif

/* whether a node's name has part in it, whatever its case (the game's
characters name theirs "bip01 l thigh", "Bip01 Pelvis") */
static boolean first_person_legs_name_has(
	char const *name,
	char const *part)
{
	char lower[TAG_STRING_LENGTH + 1];
	short index;

	for (index = 0; index < TAG_STRING_LENGTH && name[index]; index++)
		lower[index] = (char)(name[index] >= 'A' && name[index] <= 'Z' ? name[index] - 'A' + 'a' : name[index]);
	lower[index] = 0;
	return strstr(lower, part) != NULL;
}

/* whether a node is the node below, or one of its children's children */
static boolean first_person_legs_below(
	struct model const *model,
	short index,
	short below)
{
	for (; index != NONE; index = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->parent_node_index)
	{
		if (index == below)
			return TRUE;
	}
	return FALSE;
}

/* a new map: the mesh read forgotten (the new map's models have indices of
their own) */
void first_person_legs_reset(
	void)
{
	object_mesh_reset();
}

#ifdef HALO_VR
/* a node of the legs: the hips, thighs, calves, feet and toes */
static boolean first_person_legs_node(
	char const *name)
{
	static char const *const parts[] = { "pelvis", "thigh", "calf", "foot", "toe", "leg" };
	short index;

	for (index = 0; index < (short)NUMBEROF(parts); index++)
	{
		if (first_person_legs_name_has(name, parts[index]))
			return TRUE;
	}
	return FALSE;
}

/* a vector turned by the rotation from the frame from (forward, left, up)
to the frame to */
static void first_person_legs_turn(
	real_vector3d *v,
	real_vector3d const from[3],
	real_vector3d const to[3])
{
	real along[3];
	short axis;

	for (axis = 0; axis < 3; axis++)
		along[axis] = dot_product3d(v, &from[axis]);
	v->i = to[0].i * along[0] + to[1].i * along[1] + to[2].i * along[2];
	v->j = to[0].j * along[0] + to[1].j * along[1] + to[2].j * along[2];
	v->k = to[0].k * along[0] + to[1].k * along[1] + to[2].k * along[2];
}

/* a node's matrix turned so (first_person_legs_turn) about pivot, then
moved by move */
static void first_person_legs_rotate(
	real_matrix4x3 *matrix,
	real_vector3d const from[3],
	real_vector3d const to[3],
	real_point3d const *pivot,
	real_vector3d const *move)
{
	real_vector3d offset;

	first_person_legs_turn(&matrix->forward, from, to);
	first_person_legs_turn(&matrix->left, from, to);
	first_person_legs_turn(&matrix->up, from, to);
	vector_from_points3d(pivot, &matrix->position, &offset);
	first_person_legs_turn(&offset, from, to);
	matrix->position.x = pivot->x + offset.i + move->i;
	matrix->position.y = pivot->y + offset.j + move->j;
	matrix->position.z = pivot->z + offset.k + move->k;
}

/* whether an eye's view (its frustum, each plane's distance outward) has
a point in it, within FIRST_PERSON_LEGS_VIEW_MARGIN of its sides, and less
than within before the eye */
static boolean first_person_legs_seen_within(
	struct render_frustum const *view,
	real_point3d const *point,
	real within)
{
	/* (the near plane's distance outward is its own distance less the
	point's depth; then left, right, bottom and top) */
	real near_side = plane3d_distance_to_point(&view->world_planes[4], point);
	short side;

	if (near_side < view->z_near - within || near_side >= view->z_near)
		return FALSE;
	for (side = 0; side < 4; side++)
	{
		if (plane3d_distance_to_point(&view->world_planes[side], point) > FIRST_PERSON_LEGS_VIEW_MARGIN)
			return FALSE;
	}
	return TRUE;
}

/* a vertex of the body's mesh as the game would draw it (its nodes' blend,
skin: each node's from the model's space), split in two: the part on nodes
that do not lean and the pivot (base), and the part turned about the pivot
(arm); FALSE: not seen (the head and arms folded away) */
static boolean first_person_legs_lean_split(
	struct object_mesh const *mesh,
	long vertex_index,
	long count,
	real_matrix4x3 const *skin,
	boolean const *leans,
	real_point3d const *pivot,
	real_point3d *base,
	real_vector3d *arm)
{
	struct object_mesh_vertex const *vertex = &mesh->vertices[vertex_index];
	real weights[2];
	boolean shown = FALSE;
	short n;

	*base = *global_origin3d;
	*arm = *global_zero_vector3d;
	weights[0] = vertex->weight >= 32767 ? 1.f : vertex->weight / 32767.f;
	weights[1] = 1.f - weights[0];
	for (n = 0; n < 2; n++)
	{
		real_point3d p;

		if (weights[n] <= 0.f || vertex->nodes[n] >= count)
			continue;
		matrix4x3_transform_point(&skin[vertex->nodes[n]], &vertex->position, &p);
		if (first_person_legs_matrices[vertex->nodes[n]].scale > 0.01f)
			shown = TRUE;
		if (leans[vertex->nodes[n]])
		{
			arm->i += (p.x - pivot->x) * weights[n];
			arm->j += (p.y - pivot->y) * weights[n];
			arm->k += (p.z - pivot->z) * weights[n];
			base->x += pivot->x * weights[n];
			base->y += pivot->y * weights[n];
			base->z += pivot->z * weights[n];
		}
		else
		{
			base->x += p.x * weights[n];
			base->y += p.y * weights[n];
			base->z += p.z * weights[n];
		}
	}
	return shown;
}

/* each vertex's split (first_person_legs_lean_split) for one lean: made
the first time a triangle has the vertex as a corner, and kept for the
others that share it (most vertices are corners of about six), which gives
the same points as working each corner out again. NULL where the memory
cannot be had: each corner is worked out as it comes */
struct first_person_legs_split
{
	real_point3d base;
	real_vector3d arm;
	/* 0: not yet; 1: not seen; 2: seen */
	byte state;
};

static struct first_person_legs_split *first_person_legs_splits(
	long vertex_count)
{
	static struct first_person_legs_split *splits = NULL;
	long index;

	if (!splits)
		splits = malloc(sizeof(*splits) * OBJECT_MESH_MAXIMUM_VERTICES);
	if (!splits)
		return NULL;
	for (index = 0; index < vertex_count && index < OBJECT_MESH_MAXIMUM_VERTICES; index++)
		splits[index].state = 0;
	return splits;
}

/* a triangle's corner's split, kept in splits (if any) */
static boolean first_person_legs_lean_corner(
	struct object_mesh const *mesh,
	struct first_person_legs_split *splits,
	long vertex_index,
	long count,
	real_matrix4x3 const *skin,
	boolean const *leans,
	real_point3d const *pivot,
	real_point3d *base,
	real_vector3d *arm)
{
	struct first_person_legs_split *split;

	if (!splits || vertex_index < 0 || vertex_index >= mesh->vertex_count || vertex_index >= OBJECT_MESH_MAXIMUM_VERTICES)
		return first_person_legs_lean_split(mesh, vertex_index, count, skin, leans, pivot, base, arm);
	split = &splits[vertex_index];
	if (!split->state)
	{
		split->state = first_person_legs_lean_split(mesh, vertex_index, count, skin, leans, pivot, &split->base,
			&split->arm) ? 2 : 1;
	}
	*base = split->base;
	*arm = split->arm;
	return split->state == 2;
}

/* the body's upper body leant back about its hips (pivot; the top of its
neck; its heading's forward, and left), as little as keeps it clear of the
eye (the flat screen's: both eyes, its view) or the VR mode's eyes (eyes,
their views as drawn, the nearer of their near planes' distances, and the
time since the last frame, seconds; no farther than most, radians): the
eyes nod forward over the chest
as the head looks down (the neck stays under them), and the near plane,
about 19 cm before them, would cut into the armour of its chest and collar,
showing it hollow. Its hips and legs stand where they are, as do the
holsters and the pouch about them; its upper body leans back, out of the
eyes' way, as far as it must, and no farther. Only what the eyes see
counts, as they look now: looking ahead, or down at the belt, it stands as
it is (the chest's armour below and behind the eyes, out of their view, the
arms' shoulders over its own); looking down over the chest, it leans back,
as little as it can about the hips (it leant farther about the waist,
nearer the chest, the chest turned up into the view and the belly bulging
before the feet). Nor do the eyes see the opening of its collar (the head
folded away, the dark top of the neck, a pit before the eyes), which a chin
would hide: seen, it is behind them. Each vertex of the upper body is where
the game would draw it: its nodes' blend, the part on nodes that do not
lean and the pivot (base), and the part turned about the pivot (arm) */
static void first_person_legs_lean(
	long object_index,
	struct model const *model,
	long count,
	real_point3d const *pivot,
	real_point3d const *neck_top,
	real_vector3d const *forward,
	real_vector3d const *left,
	real_point3d const eyes[2],
	struct render_frustum const *const views[2],
	real near,
	real seconds,
	real most)
{
	static real_point3d base[FIRST_PERSON_LEGS_LEAN_POINTS];
	static real_vector3d arm[FIRST_PERSON_LEGS_LEAN_POINTS];
	static real lean = 0.f;
	struct object_mesh const *mesh = object_mesh_get(object_index);
	real_matrix4x3 skin[FIRST_PERSON_LEGS_MAXIMUM_NODES];
	boolean leans[FIRST_PERSON_LEGS_MAXIMUM_NODES];
	real_vector3d from[3], to[3], move, neck;
	real clear, seen_reach = 0.f, wanted, angle, eyes_ahead, neck_forward, neck_up;
	long index, weighed = 0;
	short eye, corner;
	struct first_person_legs_split *splits;

	if (!mesh || mesh->node_count < count)
		return;
	clear = near * FIRST_PERSON_LEGS_CLEAR_SCALE + FIRST_PERSON_LEGS_CLEAR_MORE;
	/* (how far from an eye a point it sees less than clear before it may
	be: out to its view's corners at that depth, and the margin) */
	for (eye = 0; eye < 2; eye++)
	{
		for (corner = 0; corner < 4; corner++)
		{
			real reach = distance3d(&views[eye]->world_vertices[corner], &views[eye]->world_vertices[4]) /
				views[eye]->z_far * clear + 2.f * FIRST_PERSON_LEGS_VIEW_MARGIN;

			seen_reach = MAX(seen_reach, reach);
		}
	}
	/* (the eyes ahead of the pivot, and the top of the neck from it, along
	the heading and up) */
	eyes_ahead = ((eyes[0].x + eyes[1].x) * 0.5f - pivot->x) * forward->i +
		((eyes[0].y + eyes[1].y) * 0.5f - pivot->y) * forward->j;
	vector_from_points3d(pivot, neck_top, &neck);
	neck_forward = dot_product3d(&neck, forward);
	neck_up = neck.k;
	for (index = 0; index < count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

		leans[index] = node->parent_node_index != NONE && !first_person_legs_node(node->name);
		matrix4x3_multiply(&first_person_legs_matrices[index], &mesh->inverse[index], &skin[index]);
	}
	splits = first_person_legs_splits(mesh->vertex_count);

	/* (the points of the upper body's surface that a lean could bring
	within seen_reach of an eye, its arm's reach about the pivot crossing
	that sphere: each triangle's corners, the middles of its sides and its
	middle, as a large triangle may cross the view with its corners out of
	it; a lean turns them as their corners) */
	for (index = 0; index < mesh->triangle_count; index++)
	{
		real_point3d corner_base[3];
		real_vector3d corner_arm[3];
		boolean leaning = FALSE, shown = FALSE;
		short point;

		for (corner = 0; corner < 3; corner++)
		{
			if (first_person_legs_lean_corner(mesh, splits, mesh->triangles[index][corner], count, skin, leans, pivot,
				&corner_base[corner], &corner_arm[corner]))
			{
				shown = TRUE;
			}
			if (magnitude_squared3d(&corner_arm[corner]) > 0.f)
				leaning = TRUE;
		}
		/* (not the head and arms folded away: a point, not seen) */
		if (!leaning || !shown)
			continue;
		for (point = 0; point < 7 && weighed < FIRST_PERSON_LEGS_LEAN_POINTS; point++)
		{
			/* (the corners, the sides' middles, the middle) */
			static real const thirds[7][3] =
			{
				{ 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f },
				{ 0.5f, 0.5f, 0.f }, { 0.f, 0.5f, 0.5f }, { 0.5f, 0.f, 0.5f },
				{ 1.f / 3.f, 1.f / 3.f, 1.f / 3.f },
			};
			real_point3d b = { 0.f, 0.f, 0.f };
			real_vector3d a = { 0.f, 0.f, 0.f };
			boolean near_eye = FALSE;
			real reach, distance;

			for (corner = 0; corner < 3; corner++)
			{
				real w = thirds[point][corner];

				b.x += corner_base[corner].x * w;
				b.y += corner_base[corner].y * w;
				b.z += corner_base[corner].z * w;
				a.i += corner_arm[corner].i * w;
				a.j += corner_arm[corner].j * w;
				a.k += corner_arm[corner].k * w;
			}
			reach = magnitude3d(&a);
			for (eye = 0; eye < 2; eye++)
			{
				distance = distance3d(&b, &eyes[eye]);
				if (distance - reach < seen_reach && reach - distance < seen_reach)
					near_eye = TRUE;
			}
			if (!near_eye)
				continue;
			base[weighed] = b;
			arm[weighed] = a;
			weighed++;
		}
	}

	/* (the least lean that keeps them all clear of what the eyes see;
	leaning back at once, as far as needed, and coming upright again
	smoothly: none less than it may come to this frame is tried) */
	for (wanted = MAX(lean - FIRST_PERSON_LEGS_UPRIGHT_SPEED * seconds, 0.f); wanted < most;
		wanted += FIRST_PERSON_LEGS_LEAN_STEP)
	{
		real c = (real)cos(wanted), s = (real)sin(wanted);
		boolean too_near = FALSE;

		if (neck_forward * c - neck_up * s > eyes_ahead - FIRST_PERSON_LEGS_NECK_BEHIND)
		{
			real_point3d p;

			/* (turned back as the points below are) */
			p.x = pivot->x + neck.i + forward->i * (neck_forward * (c - 1.f) - neck_up * s);
			p.y = pivot->y + neck.j + forward->j * (neck_forward * (c - 1.f) - neck_up * s);
			p.z = pivot->z + neck.k + neck_forward * s + neck_up * (c - 1.f);
			for (eye = 0; eye < 2; eye++)
			{
				if (first_person_legs_seen_within(views[eye], &p, 1000.f))
					too_near = TRUE;
			}
		}
		for (index = 0; index < weighed && !too_near; index++)
		{
			real_vector3d const *a = &arm[index];
			real along_forward = dot_product3d(a, forward), along_up = a->k;
			real_point3d p;

			/* (turned back: forward toward up, up toward backward) */
			p.x = base[index].x + a->i + forward->i * (along_forward * (c - 1.f) - along_up * s);
			p.y = base[index].y + a->j + forward->j * (along_forward * (c - 1.f) - along_up * s);
			p.z = base[index].z + a->k + along_forward * s + along_up * (c - 1.f);
			for (eye = 0; eye < 2; eye++)
			{
				if (first_person_legs_seen_within(views[eye], &p, clear))
					too_near = TRUE;
			}
		}
		if (!too_near)
			break;
	}
	if (wanted > most)
		wanted = most;
	lean = wanted;
	if (lean <= 0.f)
		return;

	angle = lean;
	from[0] = *forward;
	from[1] = *left;
	from[2] = *global_up3d;
	to[0].i = forward->i * (real)cos(angle);
	to[0].j = forward->j * (real)cos(angle);
	to[0].k = (real)sin(angle);
	to[1] = *left;
	to[2].i = -forward->i * (real)sin(angle);
	to[2].j = -forward->j * (real)sin(angle);
	to[2].k = (real)cos(angle);
	move = *global_zero_vector3d;
	for (index = 0; index < count; index++)
	{
		if (leans[index])
			first_person_legs_rotate(&first_person_legs_matrices[index], from, to, pivot, &move);
	}
}

/* the VR mode's body's chest as it is drawn (chest: its node, the neck's
parent; neck; hips: the root, the pelvis; the top of the real neck, and the
heading's forward and left there), and how far its back is behind it, and
its hips' right side: first_person_legs_vr_chest */
static void first_person_legs_vr_chest_note(
	long object_index,
	struct model const *model,
	long count,
	short chest,
	short neck,
	short hips,
	real_point3d const *anchor,
	real_vector3d const *forward,
	real_vector3d const *left)
{
	struct object_mesh const *mesh = object_mesh_get(object_index);
	real_matrix4x3 skin[FIRST_PERSON_LEGS_MAXIMUM_NODES];
	boolean upper[FIRST_PERSON_LEGS_MAXIMUM_NODES];
	real_point3d const *origin = &first_person_legs_matrices[chest].position;
	real_point3d const *middle = &first_person_legs_matrices[hips].position;
	real_vector3d up, across, ahead, offset;
	real along, back = 0.0f, side = 0.0f;
	long index;

	vector_from_points3d(origin, &first_person_legs_matrices[neck].position, &up);
	if (!mesh || mesh->node_count < count || normalize3d(&up) <= 0.0f)
		return;
	across = *left;
	along = dot_product3d(&across, &up);
	across.i -= up.i * along;
	across.j -= up.j * along;
	across.k -= up.k * along;
	if (normalize3d(&across) <= 0.0f)
		return;
	cross_product3d(&across, &up, &ahead);
	for (index = 0; index < count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

		/* (the upper body as drawn: not the legs, nor the head and arms
		folded away) */
		upper[index] = node->parent_node_index != NONE && !first_person_legs_node(node->name) &&
			first_person_legs_matrices[index].scale > 0.01f;
		matrix4x3_multiply(&first_person_legs_matrices[index], &mesh->inverse[index], &skin[index]);
	}
	for (index = 0; index < mesh->vertex_count; index++)
	{
		struct object_mesh_vertex const *vertex = &mesh->vertices[index];
		real_point3d p;

		if (vertex->nodes[0] >= count || (!upper[vertex->nodes[0]] && vertex->nodes[0] != hips))
			continue;
		matrix4x3_transform_point(&skin[vertex->nodes[0]], &vertex->position, &p);
		if (vertex->nodes[0] == hips)
		{
			/* (the hips' right side, level) */
			vector_from_points3d(middle, &p, &offset);
			along = dot_product3d(&offset, left);
			if (along < side)
				side = along;
			continue;
		}
		vector_from_points3d(origin, &p, &offset);
		along = dot_product3d(&offset, &ahead);
		if (along < back)
			back = along;
	}
	/* (from the top of the real neck, in the heading's axes) */
	vector_from_points3d(anchor, origin, &offset);
	first_person_legs_vr_chest.origin.x = dot_product3d(&offset, forward);
	first_person_legs_vr_chest.origin.y = dot_product3d(&offset, left);
	first_person_legs_vr_chest.origin.z = offset.k;
	first_person_legs_vr_chest.forward.i = dot_product3d(&ahead, forward);
	first_person_legs_vr_chest.forward.j = dot_product3d(&ahead, left);
	first_person_legs_vr_chest.forward.k = ahead.k;
	first_person_legs_vr_chest.left.i = dot_product3d(&across, forward);
	first_person_legs_vr_chest.left.j = dot_product3d(&across, left);
	first_person_legs_vr_chest.left.k = across.k;
	first_person_legs_vr_chest.up.i = dot_product3d(&up, forward);
	first_person_legs_vr_chest.up.j = dot_product3d(&up, left);
	first_person_legs_vr_chest.up.k = up.k;
	first_person_legs_vr_chest.back = back;
	vector_from_points3d(anchor, middle, &offset);
	first_person_legs_vr_chest.hips.x = dot_product3d(&offset, forward);
	first_person_legs_vr_chest.hips.y = dot_product3d(&offset, left);
	first_person_legs_vr_chest.hips.z = offset.k;
	first_person_legs_vr_chest.hips_side = side;
}

/* vr_holster.c: the chest of the player's body as it was drawn last, from where the top of the real neck is now and the body's heading
(world yaw): its frame (at the foot of the chest, forward, left and up
along it) in the world, and how far before the frame's middle its back is
(world units, negative); the Master Chief's standing upright before it is
drawn (display.first_person_legs off) */
void first_person_legs_vr_back(real_point3d const *anchor, real yaw, real_matrix4x3 *chest, real *back)
{
	real c = (real)cos(yaw), s = (real)sin(yaw);
	real_point3d const *o = &first_person_legs_vr_chest.origin;
	real_vector3d const *f = &first_person_legs_vr_chest.forward, *u = &first_person_legs_vr_chest.up;

	chest->scale = 1.0f;
	chest->position.x = anchor->x + o->x * c - o->y * s;
	chest->position.y = anchor->y + o->x * s + o->y * c;
	chest->position.z = anchor->z + o->z;
	chest->forward.i = f->i * c - f->j * s;
	chest->forward.j = f->i * s + f->j * c;
	chest->forward.k = f->k;
	chest->up.i = u->i * c - u->j * s;
	chest->up.j = u->i * s + u->j * c;
	chest->up.k = u->k;
	cross_product3d(&chest->up, &chest->forward, &chest->left);
	*back = first_person_legs_vr_chest.back;
}

/* vr_holster.c: the hips of the player's body as they were drawn last,
from where the top of the real neck is now and the body's heading (world
yaw): their middle in the world, and how far left of it their right side is
(world units, negative) */
void first_person_legs_vr_hips(real_point3d const *anchor, real yaw, real_point3d *middle, real *side)
{
	real c = (real)cos(yaw), s = (real)sin(yaw);
	real_point3d const *h = &first_person_legs_vr_chest.hips;

	middle->x = anchor->x + h->x * c - h->y * s;
	middle->y = anchor->y + h->x * s + h->y * c;
	middle->z = anchor->z + h->z;
	*side = first_person_legs_vr_chest.hips_side;
}

/* the pose to draw the body with in the VR mode, the head's place and the
body's heading the real ones (vr_render_body_anchor); NULL: the game's
(no eye pass, a body without a spine, a neck and upper arms named) */
static real_matrix4x3 const *first_person_legs_vr_body(
	long object_index,
	struct object_datum const *object,
	struct model const *model,
	long count,
	real_matrix4x3 const *nodes)
{
	short pelvis = NONE, spine = NONE, neck = NONE, head = NONE, left_arm = NONE, right_arm = NONE;
	short index;
	real_point3d anchor, collar;
	real_vector3d from[3], to[3], move;
	real heading, along, c, s;

	if (!vr_render_body_anchor(&anchor, &heading))
		return NULL;
	for (index = 0; index < count; index++)
	{
		char const *name = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->name;

		if (pelvis == NONE && first_person_legs_name_has(name, "pelvis"))
			pelvis = index;
		if (spine == NONE && first_person_legs_name_has(name, "spine"))
			spine = index;
		if (neck == NONE && first_person_legs_name_has(name, "neck"))
			neck = index;
		if (head == NONE && first_person_legs_name_has(name, "head"))
			head = index;
		if (left_arm == NONE && first_person_legs_name_has(name, "l upperarm"))
			left_arm = index;
		if (right_arm == NONE && first_person_legs_name_has(name, "r upperarm"))
			right_arm = index;
	}
	if (pelvis == NONE || spine == NONE || neck == NONE || head == NONE || left_arm == NONE || right_arm == NONE)
		return NULL;

	/* (the upper body upright and square over the hips: the frame of its
	spine, from its foot up to the neck and across the shoulders, turned
	about its foot to the body's own, upright and to the unit's left) */
	vector_from_points3d(&nodes[spine].position, &nodes[neck].position, &from[2]);
	vector_from_points3d(&nodes[right_arm].position, &nodes[left_arm].position, &from[1]);
	if (normalize3d(&from[2]) <= 0.f)
		return NULL;
	along = dot_product3d(&from[1], &from[2]);
	from[1].i -= from[2].i * along;
	from[1].j -= from[2].j * along;
	from[1].k -= from[2].k * along;
	if (normalize3d(&from[1]) <= 0.f)
		return NULL;
	cross_product3d(&from[1], &from[2], &from[0]);
	to[0].i = object->object.forward.i;
	to[0].j = object->object.forward.j;
	to[0].k = 0.f;
	if (normalize3d(&to[0]) <= 0.f)
		return NULL;
	to[1].i = -to[0].j;
	to[1].j = to[0].i;
	to[1].k = 0.f;
	to[2] = *global_up3d;
	move = *global_zero_vector3d;
	for (index = 0; index < count; index++)
	{
		struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

		first_person_legs_matrices[index] = nodes[index];
		if (node->parent_node_index != NONE && !first_person_legs_node(node->name))
			first_person_legs_rotate(&first_person_legs_matrices[index], from, to, &nodes[spine].position, &move);
	}
	/* (the head folded into the top of the neck, and each arm into its
	shoulder, closing them: folded into the base of the neck, the neck
	opened a dark hole before the eyes, and the arms drew the armpits up
	across the chest; the collarbones are the shoulders' tops) */
	collar = first_person_legs_matrices[neck].position;
	for (index = 0; index < count; index++)
	{
		short up;

		for (up = index; up != NONE && up != head && up != left_arm && up != right_arm;
			up = TAG_BLOCK_GET_ELEMENT(&model->nodes, up, struct model_node)->parent_node_index);
		if (up != NONE)
		{
			first_person_legs_matrices[index].position = first_person_legs_matrices[up].position;
			first_person_legs_matrices[index].scale = FIRST_PERSON_LEGS_FOLDED;
		}
	}

	/* (all of it turned from the unit's facing to the body's heading about
	the base of the neck, and that moved under the top of the real neck, a
	little behind it (FIRST_PERSON_LEGS_VR_BACK), at its own height: the
	body stands where it stands, the head bowing or not) */
	from[0] = to[0];
	from[1] = to[1];
	from[2] = to[2];
	c = (real)cos(heading);
	s = (real)sin(heading);
	to[0].i = c;
	to[0].j = s;
	to[0].k = 0.f;
	to[1].i = -s;
	to[1].j = c;
	to[1].k = 0.f;
	move.i = anchor.x - collar.x - c * FIRST_PERSON_LEGS_VR_BACK;
	move.j = anchor.y - collar.y - s * FIRST_PERSON_LEGS_VR_BACK;
	move.k = 0.f;
	for (index = 0; index < count; index++)
		first_person_legs_rotate(&first_person_legs_matrices[index], from, to, &collar, &move);
	/* (and its upper body leant back about its hips, as far as keeps it
	clear of the eyes) */
	{
		real_point3d eyes[2];
		struct render_frustum const *views[2];
		real near, seconds;

		if (vr_render_body_eyes(eyes, views, &near, &seconds))
		{
			first_person_legs_lean(object_index, model, count, &first_person_legs_matrices[pelvis].position,
				&first_person_legs_matrices[head].position, &to[0], &to[1], eyes, views, near, seconds,
				FIRST_PERSON_LEGS_LEAN_MAXIMUM);
		}
	}
	/* (and its chest's frame and back noted, as drawn, for the long gun
	slung across it, and its hips' side, for the pistol at the hip) */
	if (TAG_BLOCK_GET_ELEMENT(&model->nodes, neck, struct model_node)->parent_node_index != NONE)
	{
		first_person_legs_vr_chest_note(object_index, model, count,
			TAG_BLOCK_GET_ELEMENT(&model->nodes, neck, struct model_node)->parent_node_index, neck, pelvis, &anchor,
			&to[0], &to[1]);
	}
	return first_person_legs_matrices;
}

/* the frames (from[], to[]: forward, left, up's places) of the turn that
takes the direction a toward b, about the axis across both, no farther than
most (radians); FALSE: none (either none, or the two along one line) */
static boolean first_person_legs_vr_turn_toward(
	real_vector3d const *a,
	real_vector3d const *b,
	real most,
	real_vector3d from[3],
	real_vector3d to[3])
{
	real_vector3d u = *a, v, w;
	real angle, c, s;

	cross_product3d(a, b, &w);
	if (normalize3d(&u) <= 0.f || magnitude3d(&w) < 1e-6f || normalize3d(&w) <= 0.f)
		return FALSE;
	cross_product3d(&w, &u, &v);
	angle = (real)atan2(dot_product3d(b, &v), dot_product3d(b, &u));
	if (angle > most)
		angle = most;
	c = (real)cos(angle);
	s = (real)sin(angle);
	from[0] = u;
	from[1] = v;
	from[2] = w;
	to[0].i = u.i * c + v.i * s;
	to[0].j = u.j * c + v.j * s;
	to[0].k = u.k * c + v.k * s;
	to[1].i = v.i * c - u.i * s;
	to[1].j = v.j * c - u.j * s;
	to[1].k = v.k * c - u.k * s;
	to[2] = w;
	return TRUE;
}

/* the pose to draw the body with in a seat in the VR mode (vr_render_body_
seat), the real head's place the neck's (vr_render_body_anchor); NULL: none
(not seated so, a body without a neck, a head and upper arms named). The
game's pose in the seat, its legs the seat's; standing, the legs hang
straight down from the hips and the body turns to the body's heading. All of
it is moved so that the top of its neck is a little behind (FIRST_PERSON_
LEGS_VR_BACK) where the real one is with the head as it was recentred: under
the seat's eye, or its cabin's, whose seat and floor are where its hips and
feet then are (tools/vr_cabin_model.py). Its upper body turns about its hips
toward the real neck as the head moves (no farther than FIRST_PERSON_LEGS_
SEAT_TURN_MOST), and leans back clear of the eyes, as on foot
(first_person_legs_lean); its head is folded into the top of its neck,
and its arms into its shoulders where the VR mode draws its own */
static real_matrix4x3 const *first_person_legs_vr_seated(
	long object_index,
	struct model const *model,
	long count,
	real_matrix4x3 const *nodes)
{
	struct vr_body_seat seat;
	short pelvis = NONE, neck = NONE, head = NONE, left_arm = NONE, right_arm = NONE;
	short index, child, pass;
	real_point3d anchor, target;
	real_vector3d from[3], to[3], move, a, b;
	real yaw;

	if (!vr_render_body_seat(object_index, &seat) || !vr_render_body_anchor(&anchor, &yaw))
		return NULL;
	for (index = 0; index < count; index++)
	{
		char const *name = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->name;

		if (pelvis == NONE && first_person_legs_name_has(name, "pelvis"))
			pelvis = index;
		if (neck == NONE && first_person_legs_name_has(name, "neck"))
			neck = index;
		if (head == NONE && first_person_legs_name_has(name, "head"))
			head = index;
		if (left_arm == NONE && first_person_legs_name_has(name, "l upperarm"))
			left_arm = index;
		if (right_arm == NONE && first_person_legs_name_has(name, "r upperarm"))
			right_arm = index;
	}
	if (pelvis == NONE || neck == NONE || head == NONE || left_arm == NONE || right_arm == NONE)
		return NULL;
	for (index = 0; index < count; index++)
		first_person_legs_matrices[index] = nodes[index];
	move = *global_zero_vector3d;

	if (seat.standing)
	{
		/* (each thigh, then each calf, turned about its joint so that the
		leg below it hangs straight down, all below it with it) */
		for (pass = 0; pass < 2; pass++)
		{
			for (index = 0; index < count; index++)
			{
				char const *name = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->name;

				if (!first_person_legs_name_has(name, pass == 0 ? "thigh" : "calf"))
					continue;
				for (child = 0; child < count; child++)
				{
					if (TAG_BLOCK_GET_ELEMENT(&model->nodes, child, struct model_node)->parent_node_index == index)
						break;
				}
				if (child >= count)
					continue;
				vector_from_points3d(&first_person_legs_matrices[index].position, &first_person_legs_matrices[child].position,
					&a);
				b.i = 0.f;
				b.j = 0.f;
				b.k = -1.f;
				if (first_person_legs_vr_turn_toward(&a, &b, 3.2f, from, to))
				{
					real_point3d pivot = first_person_legs_matrices[index].position;
					short below;

					for (below = 0; below < count; below++)
					{
						if (first_person_legs_below(model, below, index))
							first_person_legs_rotate(&first_person_legs_matrices[below], from, to, &pivot, &move);
					}
				}
			}
		}
		/* (and the whole turned about the vertical through its neck, from
		the way the game poses it facing to the body's heading) */
		{
			real_point3d pivot = first_person_legs_matrices[neck].position;

			from[0] = seat.pose_forward;
			from[1].i = -seat.pose_forward.j;
			from[1].j = seat.pose_forward.i;
			from[1].k = 0.f;
			from[2] = *global_up3d;
			to[0] = seat.forward;
			to[1] = seat.left;
			to[2] = *global_up3d;
			for (index = 0; index < count; index++)
				first_person_legs_rotate(&first_person_legs_matrices[index], from, to, &pivot, &move);
		}
	}

	/* (moved so that the top of its neck is a little behind the real one's
	place with the head as it was recentred) */
	target.x = seat.rest_neck.x - seat.forward.i * FIRST_PERSON_LEGS_VR_BACK;
	target.y = seat.rest_neck.y - seat.forward.j * FIRST_PERSON_LEGS_VR_BACK;
	target.z = seat.rest_neck.z;
	vector_from_points3d(&first_person_legs_matrices[neck].position, &target, &move);
	for (index = 0; index < count; index++)
	{
		first_person_legs_matrices[index].position.x += move.i;
		first_person_legs_matrices[index].position.y += move.j;
		first_person_legs_matrices[index].position.z += move.k;
	}
	move = *global_zero_vector3d;

	/* (its upper body turned about its hips toward the real neck, a little
	behind it, as far as it turns) */
	target.x = anchor.x - seat.forward.i * FIRST_PERSON_LEGS_VR_BACK;
	target.y = anchor.y - seat.forward.j * FIRST_PERSON_LEGS_VR_BACK;
	target.z = anchor.z;
	vector_from_points3d(&first_person_legs_matrices[pelvis].position, &first_person_legs_matrices[neck].position, &a);
	vector_from_points3d(&first_person_legs_matrices[pelvis].position, &target, &b);
	if (first_person_legs_vr_turn_toward(&a, &b, FIRST_PERSON_LEGS_SEAT_TURN_MOST, from, to))
	{
		real_point3d pivot = first_person_legs_matrices[pelvis].position;

		for (index = 0; index < count; index++)
		{
			struct model_node const *node = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node);

			if (node->parent_node_index != NONE && !first_person_legs_node(node->name))
				first_person_legs_rotate(&first_person_legs_matrices[index], from, to, &pivot, &move);
		}
	}

	/* (the head folded into the top of the neck, and the arms into the
	shoulders where the VR mode draws its own, as on foot) */
	for (index = 0; index < count; index++)
	{
		short up;

		for (up = index; up != NONE && up != head && (!seat.arms || (up != left_arm && up != right_arm));
			up = TAG_BLOCK_GET_ELEMENT(&model->nodes, up, struct model_node)->parent_node_index);
		if (up != NONE)
		{
			first_person_legs_matrices[index].position = first_person_legs_matrices[up].position;
			first_person_legs_matrices[index].scale = FIRST_PERSON_LEGS_FOLDED;
		}
	}

	/* (and its upper body leant back about its hips, as far as keeps it
	clear of the eyes) */
	{
		real_point3d eyes[2];
		struct render_frustum const *views[2];
		real near, seconds;

		if (vr_render_body_eyes(eyes, views, &near, &seconds))
		{
			first_person_legs_lean(object_index, model, count, &first_person_legs_matrices[pelvis].position,
				&first_person_legs_matrices[head].position, &seat.forward, &seat.left, eyes, views, near,
				seconds, FIRST_PERSON_LEGS_LEAN_MAXIMUM);
		}
	}
	return first_person_legs_matrices;
}
#endif

/* the model and node count of a body whose pose can be drawn (its node
matrices' count, at most FIRST_PERSON_LEGS_MAXIMUM_NODES, all named in its
model); FALSE: none */
static boolean first_person_legs_model(
	struct object_datum const *object,
	struct model const **model,
	long *count)
{
	struct object_definition *definition = object_definition_get(object->definition_index);

	if (!definition || definition->object.model.index == NONE)
		return FALSE;
	*model = model_definition_get(definition->object.model.index);
	*count = object->object.node_matrices.size / (long)sizeof(real_matrix4x3);
	return *model && *count > 0 && *count <= FIRST_PERSON_LEGS_MAXIMUM_NODES && (*model)->nodes.count >= *count;
}

/* ---------- the lower body's heading (the flat screen's) */

/* The game turns the whole body: standing, it faces the aim only once the aim
has turned far enough from it, and moving it faces the aim, its legs walking
forward, backward or sideways from it. Drawn under the view, turned with it
(render_interpolation_view_body), the legs swung with every turn of the view,
their feet sliding over the ground, and walking at an angle they faced the aim
and stepped forward or sideways (#772). Drawn here, the lower body has a
heading of its own, the local player's own and only on their screen: nothing
of the unit, the network or the simulation changes. Standing, it stays planted
until the aim is FIRST_PERSON_LEGS_DEAD_ZONE from it, then turns after it.
Moving, it turns toward the way it moves: walking at an angle, it faces the
way it goes and walks forward or backward (never facing behind the aim), as
far as halfway to a sideways walk, which goes the way it goes square to the
aim; where the game plays another walk than the one it walks, it is drawn
walking its own (first_person_legs_moving_pose). Each walk goes the way its
heading and the body move, so its feet stay where they step (turned 60
degrees toward a sideways way and walking forward, the feet slid sideways a
third of the body's speed). Turned by each frame's time, it
moves as smoothly at any frame rate and number of ticks a second. */
struct first_person_legs_lower
{
	/* NONE: none */
	long unit_index;
	/* the game's time of the frame it was turned for (seconds) */
	real time;
	/* its heading (radians, in the world), and how fast it turns (radians a
	second) */
	real yaw;
	real turning;
	/* standing: turning after the aim */
	boolean catching;
	/* moving: the walk it walks (the unit's states' order from
	_unit_state_move_front: forward, backward, left, right), NONE standing */
	short walk;
	/* how far the walk of its own has come in (0 to 1: drawn eased, swap's),
	and that walk's pose (the model's frame: first_person_legs_moving_pose),
	the last one made */
	real swapping;
	real swap;
	boolean swap_posed;
	real_matrix4x3 swap_pose[FIRST_PERSON_LEGS_MAXIMUM_NODES];
};

/* an angle (radians) between -pi and pi */
static real first_person_legs_angle(
	real angle)
{
	while (angle > _pi)
		angle -= 2.0f * _pi;
	while (angle <= -_pi)
		angle += 2.0f * _pi;
	return angle;
}

/* the lower body's heading turned over seconds, for the aim's heading
(radians) and the way the player moves (the unit's throttle: forward and to
the left of the aim), as the notes above say */
static void first_person_legs_lower_turn(
	struct first_person_legs_lower *lower,
	real aim,
	real throttle_forward,
	real throttle_left,
	real seconds)
{
	real off = first_person_legs_angle(lower->yaw - aim);
	real target = off;

	if (throttle_forward * throttle_forward + throttle_left * throttle_left >
		FIRST_PERSON_LEGS_MOVING * FIRST_PERSON_LEGS_MOVING)
	{
		/* (each walk's way from the heading: forward, backward, left, right) */
		static real const walks[4] = { 0.0f, _pi, 0.5f * _pi, -0.5f * _pi };
		real way = (real)atan2(throttle_left, throttle_forward);
		short walk;

		lower->catching = FALSE;
		if (lower->walk < 0 || lower->walk >= 4 ||
			(real)fabs(first_person_legs_angle(way - walks[lower->walk])) > 0.25f * _pi + FIRST_PERSON_LEGS_WALK_KEEP)
		{
			/* (the nearest; halfway, forward or backward, as the game) */
			for (lower->walk = 0, walk = 1; walk < 4; walk++)
			{
				real by = (real)fabs(first_person_legs_angle(way - walks[walk]));
				real best = (real)fabs(first_person_legs_angle(way - walks[lower->walk]));

				if (by < best - (walk >= 2 ? 1e-4f : 0.0f))
					lower->walk = walk;
			}
		}
		target = first_person_legs_angle(way - walks[lower->walk]);
	}
	else
	{
		lower->walk = NONE;
		if ((real)fabs(off) > FIRST_PERSON_LEGS_DEAD_ZONE)
			lower->catching = TRUE;
		if (lower->catching)
			target = 0.0f;
	}
	if (target == off)
	{
		/* (planted, where it stands) */
		lower->turning = 0.0f;
		return;
	}
	{
		/* (x(t) = (x + (v + w x) t) e^-wt, v(t) = (v - w (v + w x) t) e^-wt) */
		real w = FIRST_PERSON_LEGS_TURN_RATE, x = off - target, v = lower->turning;
		real decay = (real)exp(-w * seconds);

		off = target + (x + (v + w * x) * seconds) * decay;
		lower->turning = (v - w * (v + w * x) * seconds) * decay;
	}
	if (lower->catching && (real)fabs(off) < FIRST_PERSON_LEGS_SETTLED)
	{
		lower->catching = FALSE;
		lower->turning = 0.0f;
	}
	off = PIN(off, -FIRST_PERSON_LEGS_TWIST_MOST, FIRST_PERSON_LEGS_TWIST_MOST);
	lower->yaw = first_person_legs_angle(aim + off);
}

/* the walk the lower body walks (walk: the unit's states' order from
_unit_state_move_front), where the game plays another: the unit's walk, as
the game would pose it now (the same frame of it, as far through
it as the game's own walk is, between the ticks; blended from where the game
began its walk as the game blends it), in the model's frame (its origin at
the unit's, facing forward), into pose. FALSE: none (the game plays that walk,
or no walk, or the graph has none) */
static boolean first_person_legs_moving_pose(
	long unit_index,
	struct model const *model,
	long count,
	short walk,
	real_matrix4x3 *pose)
{
	static real_orientation drawn[FIRST_PERSON_LEGS_MAXIMUM_NODES], next[FIRST_PERSON_LEGS_MAXIMUM_NODES];
	struct biped_datum *biped = biped_try_and_get(unit_index);
	struct animation_graph *graph;
	struct animation_graph_unit_seat *seat;
	struct animation_graph_weapon_class *weapon_class;
	struct animation const *playing, *walking;
	short state, base, walk_index, next_frame, node;
	real behind = render_interpolation_ticks_behind(), phase, frame, part;

	if (!biped || biped->object.animation.animation_graph_index == NONE)
		return FALSE;
	state = biped->unit.animation.state;
	if (state >= _unit_state_move_front && state <= _unit_state_move_right)
		base = _unit_state_move_front;
	else if (state >= _unit_state_stunned_move_front && state <= _unit_state_stunned_move_right)
		base = _unit_state_stunned_move_front;
	else
		return FALSE;
	if (walk < 0 || walk >= 4 || state == base + walk)
		return FALSE;

	/* (the walk, as unit_animation_set_state finds it: its seat's, its
	weapon's class's) */
	graph = animation_graph_definition_get(biped->object.animation.animation_graph_index);
	if (!graph || !VALID_INDEX(biped->unit.animation.seat_index, graph->unit_seats.count))
		return FALSE;
	seat = TAG_BLOCK_GET_ELEMENT(&graph->unit_seats, biped->unit.animation.seat_index, struct animation_graph_unit_seat);
	if (!VALID_INDEX(biped->unit.animation.weapon_index, seat->weapon_classes.count))
		return FALSE;
	weapon_class = TAG_BLOCK_GET_ELEMENT(&seat->weapon_classes, biped->unit.animation.weapon_index,
		struct animation_graph_weapon_class);
	walk_index = (short)((base == _unit_state_move_front ? _unit_weapon_class_animation_moving_front :
		_unit_weapon_class_animation_moving_wounded_front) + walk);
	if (!VALID_INDEX(walk_index, weapon_class->animations.count))
		return FALSE;
	walk_index = animation_graph_animation_index_get(&weapon_class->animations)[walk_index].animation_index;
	if (!VALID_INDEX(walk_index, graph->animations.count) ||
		!VALID_INDEX(biped->object.animation.state.index, graph->animations.count))
	{
		return FALSE;
	}
	playing = TAG_BLOCK_GET_ELEMENT(&graph->animations, biped->object.animation.state.index, struct animation);
	walking = TAG_BLOCK_GET_ELEMENT(&graph->animations, walk_index, struct animation);
	if (playing->frame_count <= 0 || walking->frame_count <= 0 || walking->node_count != count ||
		model->nodes.count != count || graph->nodes.count != count)
	{
		return FALSE;
	}

	/* (as far through it as the game's walk is through its own, drawn a
	tick behind as the body is: between its frame and the one after) */
	phase = ((real)biped->object.animation.state.frame_index - behind) / (real)playing->frame_count;
	phase -= (real)floor(phase);
	frame = phase * (real)walking->frame_count;
	part = frame - (real)floor(frame);
	next_frame = (short)PIN((long)frame, 0, walking->frame_count - 1);
	animation_get_node_orientations(model, walking, next_frame, drawn);
	animation_get_node_orientations(model, walking, (short)((next_frame + 1) % walking->frame_count), next);
	for (node = 0; node < count; node++)
	{
		quaternions_interpolate_and_normalize(&drawn[node].rotation, &next[node].rotation, part, &drawn[node].rotation);
		drawn[node].translation.x += (next[node].translation.x - drawn[node].translation.x) * part;
		drawn[node].translation.y += (next[node].translation.y - drawn[node].translation.y) * part;
		drawn[node].translation.z += (next[node].translation.z - drawn[node].translation.z) * part;
		drawn[node].scale += (next[node].scale - drawn[node].scale) * part;
	}
	/* (blended from the pose the game began its walk from, as far as the
	game has blended, between the ticks) */
	if (biped->object.animation.interpolation_frame_count > 0)
	{
		real_orientation const *original = (real_orientation const *)object_header_block_get(unit_index,
			&biped->object.original_node_orientations);
		real blend = ((real)biped->object.animation.interpolation_frame_index + 1.0f - behind) /
			(real)biped->object.animation.interpolation_frame_count;

		blend = PIN(blend, 0.0f, 1.0f);
		for (node = 0; original && node < count; node++)
		{
			quaternions_interpolate_and_normalize(&original[node].rotation, &drawn[node].rotation, blend,
				&drawn[node].rotation);
			drawn[node].translation.x = original[node].translation.x + (drawn[node].translation.x - original[node].translation.x) * blend;
			drawn[node].translation.y = original[node].translation.y + (drawn[node].translation.y - original[node].translation.y) * blend;
			drawn[node].translation.z = original[node].translation.z + (drawn[node].translation.z - original[node].translation.z) * blend;
			drawn[node].scale = original[node].scale + (drawn[node].scale - original[node].scale) * blend;
		}
	}
	if (biped->object.scale > 0.0f)
	{
		drawn[0].scale *= biped->object.scale;
		drawn[0].translation.x *= biped->object.scale;
		drawn[0].translation.y *= biped->object.scale;
		drawn[0].translation.z *= biped->object.scale;
	}
	animation_graph_node_matrices_from_orientations(biped->object.animation.animation_graph_index, pose, drawn,
		global_origin3d, global_forward3d, global_up3d);
	return TRUE;
}

/* the lower body of the unit (of a local player, seen in the first person,
facing aim: radians), turned for this frame: once a frame, however many times
it is drawn (its shadow, its legs), by the game's time, which stops while the
game is paused */
static struct first_person_legs_lower *first_person_legs_lower_get(
	long unit_index,
	struct model const *model,
	long count,
	real aim)
{
	static struct first_person_legs_lower lowers[MAXIMUM_LOCAL_PLAYERS];
	static boolean begun = FALSE;
	struct unit_datum *unit = unit_get(unit_index);
	struct first_person_legs_lower *lower = &lowers[0];
	real now = render_interpolation_game_time_sec(game_time_get()), seconds;
	short index;

	if (!begun)
	{
		for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
			lowers[index].unit_index = NONE;
		begun = TRUE;
	}
	/* (its own, or the one turned longest ago) */
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		if (lowers[index].unit_index == unit_index)
		{
			lower = &lowers[index];
			break;
		}
		if (lowers[index].unit_index == NONE || lowers[index].time < lower->time)
			lower = &lowers[index];
	}
	seconds = now - lower->time;
	if (lower->unit_index != unit_index || seconds < 0.0f || seconds > 0.5f)
	{
		/* (a new unit, a new game, or a long pause: facing the aim) */
		lower->unit_index = unit_index;
		lower->time = now;
		lower->yaw = aim;
		lower->turning = 0.0f;
		lower->catching = FALSE;
		lower->walk = NONE;
		lower->swapping = 0.0f;
		lower->swap = 0.0f;
		lower->swap_posed = FALSE;
		seconds = 0.0f;
	}
	else if (seconds == 0.0f)
	{
		return lower;
	}
	lower->time = now;
	first_person_legs_lower_turn(lower, aim, unit->unit.throttle.i, unit->unit.throttle.j, seconds);
	{
		boolean posed = first_person_legs_moving_pose(unit_index, model, count, lower->walk, lower->swap_pose);
		real wanted = posed ? 1.0f : 0.0f;

		if (posed)
			lower->swap_posed = TRUE;
		if (lower->swapping < wanted)
			lower->swapping = MIN(lower->swapping + seconds / FIRST_PERSON_LEGS_SWAP_SECONDS, wanted);
		else
			lower->swapping = MAX(lower->swapping - seconds / FIRST_PERSON_LEGS_SWAP_SECONDS, wanted);
		if (!lower->swap_posed)
			lower->swapping = 0.0f;
		lower->swap = lower->swapping * lower->swapping * (3.0f - 2.0f * lower->swapping);
	}
	return lower;
}

/* a node's matrix moved toward another by part (0 to 1), its axes kept at
right angles */
static void first_person_legs_blend(
	real_matrix4x3 *matrix,
	real_matrix4x3 const *toward,
	real part)
{
	real along;

	matrix->scale += (toward->scale - matrix->scale) * part;
	matrix->position.x += (toward->position.x - matrix->position.x) * part;
	matrix->position.y += (toward->position.y - matrix->position.y) * part;
	matrix->position.z += (toward->position.z - matrix->position.z) * part;
	matrix->forward.i += (toward->forward.i - matrix->forward.i) * part;
	matrix->forward.j += (toward->forward.j - matrix->forward.j) * part;
	matrix->forward.k += (toward->forward.k - matrix->forward.k) * part;
	matrix->left.i += (toward->left.i - matrix->left.i) * part;
	matrix->left.j += (toward->left.j - matrix->left.j) * part;
	matrix->left.k += (toward->left.k - matrix->left.k) * part;
	if (normalize3d(&matrix->forward) <= 0.0f)
	{
		*matrix = *toward;
		return;
	}
	along = dot_product3d(&matrix->forward, &matrix->left);
	matrix->left.i -= matrix->forward.i * along;
	matrix->left.j -= matrix->forward.j * along;
	matrix->left.k -= matrix->forward.k * along;
	if (normalize3d(&matrix->left) <= 0.0f)
	{
		*matrix = *toward;
		return;
	}
	cross_product3d(&matrix->forward, &matrix->left, &matrix->up);
}

/* a node's matrix turned about the vertical through pivot (by the angle of
cosine c, sine s) */
static void first_person_legs_yaw(
	real_matrix4x3 *matrix,
	real_point3d const *pivot,
	real c,
	real s)
{
	real_vector3d *axes[3];
	real x = matrix->position.x - pivot->x, y = matrix->position.y - pivot->y, i;
	short axis;

	axes[0] = &matrix->forward;
	axes[1] = &matrix->left;
	axes[2] = &matrix->up;
	for (axis = 0; axis < 3; axis++)
	{
		i = axes[axis]->i;
		axes[axis]->i = c * i - s * axes[axis]->j;
		axes[axis]->j = s * i + c * axes[axis]->j;
	}
	matrix->position.x = pivot->x + c * x - s * y;
	matrix->position.y = pivot->y + s * x + c * y;
}

/* where the eye is over, in the model's frame (forward, left: its origin, the
unit's, is under the game's eye): before the hips' joint at rest (the root's,
pelvis), as far forward as the front of the hips and belly at rest (the
surface on the pelvis and belly, belly: the model's own, object_mesh), as a
person's eyes are over the front of their belly. The Master Chief's armour
reaches about 20 cm before his hips' joint: over the joint, looking down showed
the hips and belly before the eye, filling the view under it; set back farther
(37 cm, before #772's second report), the thighs reached forward under the
eye from behind it. Worked out once for each model */
static void first_person_legs_front(
	struct object_datum const *object,
	struct model const *model,
	long count,
	short pelvis,
	short belly,
	real_point3d *front)
{
	static long model_index = NONE;
	static real_point3d found;
	struct object_definition *definition = object_definition_get(object->definition_index);
	struct model_node const *root = TAG_BLOCK_GET_ELEMENT(&model->nodes, pelvis, struct model_node);

	if (definition->object.model.index != model_index)
	{
		struct object_mesh const *mesh = object_mesh_get_model(definition->object.model.index);
		long index;

		model_index = definition->object.model.index;
		found.x = root->parent_node_index == NONE ? root->default_translation.x : 0.f;
		found.y = root->parent_node_index == NONE ? root->default_translation.y : 0.f;
		found.z = 0.f;
		for (index = 0; mesh && index < mesh->vertex_count; index++)
		{
			struct object_mesh_vertex const *vertex = &mesh->vertices[index];
			short node = vertex->weight >= 16384 ? vertex->nodes[0] : vertex->nodes[1];

			if (node < count && (node == pelvis || node == belly) && vertex->position.x > found.x)
				found.x = vertex->position.x;
		}
	}
	*front = found;
}

/* the body's pose placed for the flat screen's eye, into placed: the body's
own as it is drawn now, turned to the view drawn's heading and moved to its
eye (render_interpolation_view_body); its lower body turned from there to its
own heading (first_person_legs_lower_get), about the eye, the walk of its own
blended in as far as it is drawn; moved along that heading so that the eye is
over the front of its hips and belly at rest (first_person_legs_front: the
game's eye is over the unit's origin); and lowered where its head is less than
FIRST_PERSON_LEGS_HEAD_BELOW below the eye (first_person_legs.c's notes at the
top). *chest: the neck's parent, the Master Chief's "bip01 spine1"; *belly:
the chest's parent, below it ("bip01 spine"), or the chest. The same for any
view and pitch: nothing is weighed against the view but the eye's height and
the aim's heading. FALSE: none (in the VR mode's eye pass, or a body without
named hips, thighs and neck) */
static boolean first_person_legs_place(
	long object_index,
	struct object_datum const *object,
	struct model const *model,
	long count,
	real_matrix4x3 const *nodes,
	real_matrix4x3 *placed,
	short *chest,
	short *belly)
{
	short pelvis = NONE, neck = NONE, head = NONE, thighs = 0;
	short index;
	real_vector3d forward;
	real_matrix4x3 move;
	real drop;
	boolean moved;

#ifdef HALO_VR
	if (halo_vr_running())
		return FALSE;
#endif
	for (index = 0; index < count; index++)
	{
		char const *name = TAG_BLOCK_GET_ELEMENT(&model->nodes, index, struct model_node)->name;

		if (pelvis == NONE && first_person_legs_name_has(name, "pelvis"))
			pelvis = index;
		if (neck == NONE && first_person_legs_name_has(name, "neck"))
			neck = index;
		if (head == NONE && first_person_legs_name_has(name, "head"))
			head = index;
		if (first_person_legs_name_has(name, "thigh"))
			thighs++;
	}
	if (pelvis == NONE || neck == NONE || thighs == 0)
		return FALSE;
	if (head == NONE)
		head = neck;
	*chest = TAG_BLOCK_GET_ELEMENT(&model->nodes, neck, struct model_node)->parent_node_index;
	if (*chest == NONE || *chest == pelvis)
		*chest = neck;
	*belly = TAG_BLOCK_GET_ELEMENT(&model->nodes, *chest, struct model_node)->parent_node_index;
	if (*belly == NONE || *belly == pelvis)
		*belly = *chest;

	/* (the body, between the ticks, turned to the view drawn's heading and
	moved to its eye: that is the newest step's, a tick's motion on, aimed in
	the frame; the legs trailed it, sliding and stepping against it, #697, and
	swung against it turning, #826) */
	forward = object->object.forward;
	moved = render_interpolation_view_body(object_index, &move, &forward);
	for (index = 0; index < count; index++)
	{
		if (moved)
			matrix4x3_multiply(&move, &nodes[index], &placed[index]);
		else
			placed[index] = nodes[index];
	}
	forward.k = 0.f;
	if (normalize3d(&forward) <= 0.f)
		forward = *global_forward3d;
	/* (its lower body turned from the aim's heading to its own, about the
	eye, with the walk of its own blended in: placed as the game's pose is, at
	the unit's origin under the eye, facing the aim) */
	if (moved)
	{
		real_point3d const *eye = &global_window_parameters.camera.position;
		real aim = (real)atan2(forward.j, forward.i);
		struct first_person_legs_lower const *lower = first_person_legs_lower_get(object_index, model, count, aim);
		real c, s;

		if (lower->swap > 0.f)
		{
			struct biped_datum *biped = biped_try_and_get(object_index);
			struct biped_definition const *definition = biped ? biped_definition_get(biped->definition_index) : NULL;
			real_matrix4x3 origin, swapped;

			if (definition)
			{
				origin.scale = 1.f;
				origin.forward = forward;
				origin.left.i = -forward.j;
				origin.left.j = forward.i;
				origin.left.k = 0.f;
				origin.up = *global_up3d;
				origin.position = *eye;
				origin.position.z -= (1.f - biped->biped.crouch) * definition->biped.standing_camera_height +
					biped->biped.crouch * definition->biped.crouching_camera_height;
				for (index = 0; index < count; index++)
				{
					matrix4x3_multiply(&origin, &lower->swap_pose[index], &swapped);
					first_person_legs_blend(&placed[index], &swapped, lower->swap);
				}
			}
		}
		c = (real)cos(lower->yaw - aim);
		s = (real)sin(lower->yaw - aim);
		for (index = 0; index < count; index++)
			first_person_legs_yaw(&placed[index], eye, c, s);
		forward.i = (real)cos(lower->yaw);
		forward.j = (real)sin(lower->yaw);
	}
	/* (the eye over the front of the hips and belly at rest, along that
	heading) */
	{
		real_point3d front;

		first_person_legs_front(object, model, count, pelvis, *belly, &front);
		for (index = 0; index < count; index++)
		{
			placed[index].position.x -= forward.i * front.x - forward.j * front.y;
			placed[index].position.y -= forward.j * front.x + forward.i * front.y;
		}
	}
	/* (lowered where the eye is lower in it than its head: a crouch
	jump's) */
	drop = placed[head].position.z - (global_window_parameters.camera.position.z - FIRST_PERSON_LEGS_HEAD_BELOW);
	if (drop > 0.f)
	{
		for (index = 0; index < count; index++)
			placed[index].position.z -= drop;
	}
	return TRUE;
}

/* the pose to draw the body with: in the VR mode, the VR mode's
(first_person_legs_vr_body, first_person_legs_vr_seated); on the flat
screen, the body placed for the eye (first_person_legs_place), its chest and
all above it folded into the chest's joint, and its belly, closed so, flattened
onto the top of its hips, a lid over them (FIRST_PERSON_LEGS_LID): looking
straight down from over the hips, the belly as a person sees theirs, not
into the hips, nor a dark cone of it toward the eye. NULL: none (a body
without named legs and neck) */
real_matrix4x3 const *first_person_legs_node_matrices(
	long object_index,
	real_matrix4x3 const *nodes)
{
	struct object_datum *object = object_try_and_get(object_index);
	struct model const *model;
	long count;
	short chest, belly;
	short index;
	real lid;

	if (!object || !nodes || !first_person_legs_model(object, &model, &count))
		return NULL;
#ifdef HALO_VR
	/* (in a seat: the VR mode's seated body, or none) */
	if (object->object.parent_object_index != NONE)
		return first_person_legs_vr_seated(object_index, model, count, nodes);
	{
		real_matrix4x3 const *body = first_person_legs_vr_body(object_index, object, model, count, nodes);

		if (body)
			return body;
	}
#endif
	if (!first_person_legs_place(object_index, object, model, count, nodes, first_person_legs_matrices, &chest, &belly))
		return NULL;
	for (index = 0; index < count; index++)
	{
		if (first_person_legs_below(model, index, chest))
		{
			first_person_legs_matrices[index].position = first_person_legs_matrices[chest].position;
			first_person_legs_matrices[index].scale = FIRST_PERSON_LEGS_FOLDED;
		}
	}
	lid = first_person_legs_matrices[belly].position.z;
	for (index = 0; index < count; index++)
	{
		if (first_person_legs_below(model, index, belly))
		{
			first_person_legs_matrices[index].forward.k *= FIRST_PERSON_LEGS_LID;
			first_person_legs_matrices[index].left.k *= FIRST_PERSON_LEGS_LID;
			first_person_legs_matrices[index].up.k *= FIRST_PERSON_LEGS_LID;
			first_person_legs_matrices[index].position.z = lid +
				(first_person_legs_matrices[index].position.z - lid) * FIRST_PERSON_LEGS_LID;
		}
	}
	return first_person_legs_matrices;
}

/* render_objects.c: the pose to draw the body's shadow with on the flat
screen, the whole body placed as its legs are (first_person_legs_place), so
that they stand on their own shadow; NULL: none (the VR mode's, whose body
casts none, as the stock game's first-person body casts none) */
real_matrix4x3 const *first_person_legs_shadow_node_matrices(
	long object_index,
	real_matrix4x3 const *nodes)
{
	static real_matrix4x3 shadow[FIRST_PERSON_LEGS_MAXIMUM_NODES];
	struct object_datum *object = object_try_and_get(object_index);
	struct model const *model;
	long count;
	short chest, belly;

	if (!object || !nodes || object->object.parent_object_index != NONE ||
		!first_person_legs_model(object, &model, &count) ||
		!first_person_legs_place(object_index, object, model, count, nodes, shadow, &chest, &belly))
	{
		return NULL;
	}
	return shadow;
}
