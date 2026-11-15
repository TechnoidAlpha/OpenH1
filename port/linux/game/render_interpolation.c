/*
RENDER_INTERPOLATION.C

Frames between the game's 30 Hz ticks, for the native ports (port/linux,
port/android, port/windows; see port/linux/README.md, "Frame rate").

The game simulates in 30 Hz ticks and originally drew one frame per tick.
The ports draw at the display's refresh rate instead, and every frame shows
the world between the last two ticks: after each tick the camera, every
object's node matrices and the first-person weapon's pose are kept, and a
frame blends the previous and the latest by how far the game clock has run
into the next tick, and so are what an object's shaders read (its function
values and change colors) and a unit's active camouflage. That puts what is
drawn one tick (33 ms) behind the simulation, the usual price of
interpolation. (A Catmull-Rom spline would also need the tick after the pair
it spans: two ticks behind.)

Rotations are blended as quaternions (normalised lerp, taking the shorter
way round), positions and scales linearly. Anything that moves further than
a tick of motion plausibly allows (teleports, respawns, camera cuts) snaps
instead of sweeping across the world, and so does a pose whose nodes moved
too far for one tick: two snapshots of different poses (a model swapped,
another weapon's skeleton, a pose left from seconds before) blended node by
node stretch vertices across the screen. The view cuts whenever its player's
unit changes (a respawn, however near the dead camera was), and a game state
loaded (a checkpoint's revert) leaves no snapshots behind.

Particles, contrails and other effects move every frame (game_frame), and
read the drawn pose of what they hang off. A widget simulated at the
Xbox's own step (an antenna's chain, a cloth flag) is stepped in the tick
instead, and drawn between the shapes the last two ticks left, hung from
its marker as drawn (render_interpolation_shape). A point the HUD works out
from an object's state (a unit's camera, for a waypoint's distance and the
motion tracker's centre) is taken whole as the game has it and moved with
the object as drawn (render_interpolation_unit_camera_position), not mixed
of the tick's position and the frame's markers. A stream of particles from an
object, made in the tick, is held back to be made through the frames between
the ticks (render_interpolation_particle_later).

An object made in a tick has no snapshot from the tick before: one moving
(a bolt, a grenade thrown) is drawn from half a tick of its motion back
(a whole tick back is where it was made, for a bolt the eye), not standing
a tick where the tick left it. A projectile deleted in a tick (at
what it hit) is drawn a tick longer, from its last snapshot to where it was
deleted, as the effects it made there are drawn from the start of the tick:
a ghost, drawn only here, which nothing of the game sees. At 60 and 120 a
projectile is drawn from its newest sub-steps instead (below), placed and
turned between two of them, its head start and hold from half a tick back,
and its ghost through the sub-step it was deleted in.

An object the distributed netcode moves to where the host has it
(port/linux/game/network_objects.c) is drawn gliding there over a few ticks
rather than jumping: its snapshots move with it, and the difference is drawn
on top of it, whole until the next tick and fading from then on (no more than
a few world units of it: further is a jump). A local player's view glides so
with their unit, and with what it rides.

At game.sim_rate 60 and 120 (sim_rate.h) an object of the micro lane (a
player's biped, a projectile, an item loose in the world: objects.h's
object_get_lane) moves in the tick's 2 or 4 sub-steps, not once a tick, and
its path through the tick is not the straight line between the tick's
snapshots: a jump's arc, a turn, a start or a stop, a grenade's bounce. Where
each is after every sub-step is kept (the fast phase's
_sim_substep_render_interpolation slot), for two ticks: the one drawn, and
the next, whose sub-steps run while the frames draw the one before
(sim_substep_next). A frame draws it between the poses of the sub-step it is
in (sim_substep_blend: the end of the sub-step before, or of the tick
before for the first, and the end of its own), still a tick behind as
everything else is. Only where it is moves so: its nodes move in the
sub-steps as the tick before posed them (bipeds.c's biped_carry_nodes) and
take the tick's new pose at its last, so the pose, the rotations and what
it carries are blended between the ticks as before, moved by where the
sub-steps put it from where the line between the ticks has it. The
first-person view, which the observer poses from the player's unit, is
moved the same way, and with it the first-person weapon, which is posed
from the view. An object's first two ticks of sub-steps (made, or let go
of: a projectile's head start, a whole tick's travel at once, and its hold)
are drawn between the ticks as before, from a tick's motion back; and one
put somewhere in a tick still open (render_interpolation_object_from: the
VR mode's grenade let go of in a sub-step) is drawn there until the frames
reach that tick. Kept here only, for the frames: the simulation never reads
it. At 30 a tick is one sub-step, nothing is kept, and the frames are as
they were. In a network game, other players and the vehicles they drive are
drawn between their newest two sub-steps instead, the open tick's, not a
tick behind (render_interpolation_lead_wanted, below), and so is every
projectile in any game (render_interpolation_projectile_lead).

A vehicle a player drives (of the micro lane too: vehicles.c) also turns in
the sub-steps, and what it carries (its riders, its gunner, its turret, the
player in its seat) is posed where each leaves it. Its frame (its root
node's place and rotation) is kept after every sub-step as well, and a frame
draws it between two of them, the place lerped and the rotation nlerped the
shorter way round. Its nodes, and those of everything it carries, are blended
between the ticks in that frame (each snapshot's seen from the root node of
the vehicle's own), and placed where the sub-steps' frame has the vehicle:
the vehicle and its riders on its path, turning as it did, and the pose
(its wheels, its turret's aim, a rider's animation) between the ticks as
before. A local player's view from one of its seats (in the first person or
following it, flat or in the VR mode, which poses its eye from the drawn
seat) hangs where the player's unit's camera is on the vehicle (kept, in
the vehicle's frame, at each tick: render_interpolation_camera_anchors). The
view's snapshot is taken back to where the tick left that point, not moved
and turned by the next tick's sub-steps run since, and a frame draws it
moved with that point onto the vehicle's path.

The local player's own view (display.direct_camera) is drawn from the
newest step instead, not a tick behind: on foot, where the newest step left
the player, carried on by the player's velocity and the lift they ride; in a vehicle's seat, the
vehicle and what it carries are drawn as the vehicle's newest step left it,
carried on as that step moved and turned it, and the view from the seat with
it (render_interpolation_newest_vehicles). Each aims where the player aims as
the frame takes its view.
*/

#include "cseries.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "models/models.h"
#include "rasterizer/rasterizer_model_types.h"
#include "camera/camera_scripting.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "effects/particles.h"
#include "game/game.h"
#include "game/players.h"
#include "render/render.h"
#include "render/render_cameras_internal.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/bipeds.h"
#include "devices/devices.h"
#include "devices/device_machines.h"
#include "devices/device_definitions.h"
#include "passenger_camera.h"
#include "sim_rate.h"
#include "demo.h"
#include "network_standard.h"
#include "vehicle_riding.h" /* port: port/linux/game/vehicle_riding.c */

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
unsigned long config_changes(void);
/* render_objects.c */
boolean render_objects_port_last_lighting(long object_index, struct render_lighting *lighting);
/* player_control.c */
struct player_control *player_control_get(short local_player_index);
/* ... the touch controls' swipe and gyroscope come since the frame's input
took them, as radians of the player's facing (the drawn view's own late
reading) */
boolean player_control_touch_look_late(short local_player_index, real *yaw, real *pitch);
/* ... and a controller's gyroscope's (port/linux/src/xinput_sdl.c), zoomed
as the input turns the player by it */
boolean player_control_gyro_look_late(short local_player_index, real *yaw, real *pitch);
/* view_fov.c: the mouse's turn zoomed (input.zoom_sensitivity) */
real mouse_zoom_look_scale(short local_player_index, long unit_index, short zoom_level);
/* port/linux/src/xinput_sdl.c: the mouse's motion come since the frame's
input took it, as radians of the player's facing (the drawn view's own
late reading) */
int halo_linux_mouse_look_late(short gamepad_index, float *yaw, float *pitch);
#ifdef HALO_VR
/* port/linux/src/vr.c: the VR mode is on */
int halo_vr_running(void);
#endif

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* (by absolute index: the object array's all) */
#define MAXIMUM_INTERPOLATED_OBJECTS MAXIMUM_OBJECTS_PER_MAP
#define MAXIMUM_INTERPOLATED_NODES 64

/* world units (10 feet each) a node may move in one tick before it snaps:
well beyond any vehicle, short of any teleport */
#define OBJECT_SNAP_DISTANCE 10.0f
/* ... a node may move in the object's root node's frame in one tick before
the pose is taken for a new one, not blended to: further than any limb or
part of a model moves in 33 ms (37 m/s), short of the game changing a pose
at once (an actor waking from dormancy, a model swapped), which blended
sweeps the vertices through poses it never had. (In the root's frame, so
that the whole object turning moves nothing: measured in the world, a fast
turn swung the head and weapon far enough to snap, and characters moved
like robots.) */
#define NODE_SNAP_DISTANCE 0.4f
/* ... or, a node moving steadily (render_interpolation_object_node_matrices),
further than it moved the tick before by this part of that move */
#define NODE_STEADY_GROWTH 1.5f
/* ... a first-person node may move relative to the camera */
#define FIRST_PERSON_SNAP_DISTANCE 0.25f
/* a correction's difference left drawn after each tick (of 1) */
#define CORRECTION_DECAY 0.6f
/* ... and small enough to be none */
#define CORRECTION_NEGLIGIBLE 0.001f
/* ... or so large that it is no glide but a jump (corrections come one on
another: their sum is drawn): past the largest a client's own unit or
vehicle is corrected by (3 and 4 world units), short of a snap */
#define CORRECTION_MAXIMUM 8.0f
/* a camera cut: a jump or turn no player or scripted camera makes in 33 ms */
#define CAMERA_CUT_DISTANCE 3.0f
#define CAMERA_CUT_COSINE 0.5f
/* particles held back to be made between the ticks
(render_interpolation_particle_later) */
#define MAXIMUM_LATER_PARTICLES 512
/* deleted projectiles drawn a tick longer (render_interpolation_object_deleted),
and the nodes each may have */
#define MAXIMUM_GHOSTS 64
#define MAXIMUM_GHOST_NODES 8
/* projectiles drawn from their newest sub-steps in a frame
(render_interpolation_projectile_nodes), by their index's slot, and the
nodes each may have */
#define MAXIMUM_LEAD_PROJECTILES 512
#define MAXIMUM_LEAD_PROJECTILE_NODES 8
/* how far the sub-steps' path through a tick may stray from the line
between the tick's snapshots before it is taken for a jump, not a path:
past a fast projectile's bounce, short of a teleport */
#define SUBSTEP_OFFSET_MAXIMUM 2.0f
/* how long (ticks) an object drawn from its newest sub-steps takes to glide
to the tick drawn, or back (render_interpolation_lead_weight): a lift
starting under a player, a local player getting into another's vehicle */
#define LEAD_GLIDE_TICKS 4.0f
/* the most a local player's vehicle drawn from its newest step turns on in
the time since that step (radians): past a tumble's, short of a spin not a
number */
#define NEWEST_TURN_MAXIMUM 0.5f
/* ... the most its steps' acceleration carries it on by (world units a tick
a tick, and radians): a hard turn's, a crash's or a landing's less, which
would throw it on a step past where its motion took it */
#define NEWEST_ACCELERATION_MAXIMUM 0.1f
#define NEWEST_SPIN_ACCELERATION_MAXIMUM 0.05f
/* ... and the longest its steps may be apart (ticks) to be its motion */
#define NEWEST_HISTORY_GAP 2
/* ... and slower than this (world units a tick) at its step before, it was
standing: a step's start from it is drawn at once, not blended in */
#define NEWEST_STANDING 0.002f
/* ... and how long a difference from where it was drawn takes to blend
away (ticks: a tick at every rate, over which the frames before drew the
vehicle's steps), and the most blended at once */
#define NEWEST_BLEND_TIME 1.0f
#define NEWEST_BLENDS 4
/* the frames' pitches kept for the view's pitch held at the seat's limit:
more than a step's at any rate's (a tick's at 30, at 240 Hz) */
#define NEWEST_PITCH_FRAMES 16
/* widgets' shapes kept (render_interpolation_shape_tick: 12 antennas and 2
cloth flags a map at most), the points each may have (a flag's 225), and
the ticks' shapes each keeps: the two drawn and the open tick's */
#define MAXIMUM_INTERPOLATED_SHAPES 32
#define MAXIMUM_SHAPE_POINTS 256
#define SHAPE_SNAPSHOTS 3

/* ---------- structures */

/* a widget's shape after each of its last ticks, seen from the frame of the
marker it hangs from (render_interpolation_shape_tick) */
struct interpolated_shape
{
	unsigned long owner; /* its kind (a tag group: 'ant!', 'flag'); 0: none */
	long index; /* its datum's */
	long newest_tick;
	long ticks[SHAPE_SNAPSHOTS]; /* (as interpolation_tick counts them) NONE: none */
	short counts[SHAPE_SNAPSHOTS];
	real_point3d points[SHAPE_SNAPSHOTS][MAXIMUM_SHAPE_POINTS];
};

struct interpolation_quaternion
{
	real i, j, k, w;
};

/* a node's rotation: whether its basis is one a quaternion can hold, and
that quaternion */
struct interpolation_rotation
{
	struct interpolation_quaternion quaternion;
	boolean is_rotation;
};

struct interpolated_object
{
	long object_index; /* NONE when unused */
	long tick; /* the tick of the latest snapshot */
	short node_count;
	short node_capacity;
	boolean has_previous;
	byte latest; /* which snapshot is the latest */
	long blended_frame;
	/* where it is drawn from where it is: a correction fading, and those
	since the last tick, drawn whole until the next begins to fade them */
	real_vector3d correction;
	real_vector3d correction_pending;
	/* [0] and [1]: the two snapshots, [2]: the blend drawn this frame */
	real_matrix4x3 *nodes;
	/* ... the two snapshots' rotations, found when first blended */
	struct interpolation_rotation *rotations;
	boolean rotations_valid[2];
	/* the two snapshots' function values, change colors and (a unit's)
	active camouflage, by snapshot, and [2] their blend drawn this frame */
	real function_values[3][NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	real_rgb_color change_colors[3][NUMBER_OF_OBJECT_CHANGE_COLORS];
	real camouflage[3][2];
	long values_blended_frame;
	/* where the object was at the latest snapshot, and at the one before
	(as made: a tick's motion back, or render_interpolation_object_from's) */
	real_point3d position;
	real_point3d previous_position;
	/* its facing at each snapshot, by snapshot (for a body drawn turned to
	the view: render_interpolation_view_body) */
	real_vector3d facing[2];
	/* how far each node moved in the root's frame between the snapshots
	drawn, by the parity of the tick drawn ([tick & 1] node_capacity
	each), and the tick each holds (NONE: none): how much further a node
	may move the next tick and be moving, not posed anew
	(render_interpolation_object_node_matrices) */
	real *node_moves;
	long node_moves_tick[2];
};

/* an object of the micro lane, by itself (not carried): where it was after
each sub-step of a tick, for two ticks, [tick & 1]: the tick drawn, and the
next, whose sub-steps run meanwhile */
struct interpolated_substeps
{
	long object_index; /* NONE when unused */
	long tick[2]; /* (as interpolation_tick counts them) NONE: none */
	short count[2]; /* the tick's sub-steps */
	byte recorded[2]; /* those kept, a bit each */
	/* every sub-step of the tick before kept too */
	boolean continuing[2];
	real_point3d positions[2][MAXIMUM_SIM_SUBSTEPS];
	/* a vehicle's frame (its root node's place and rotation) after each,
	when every one of the tick's was a rotation (framed): it turns in the
	sub-steps, and what it carries is posed in that frame */
	boolean framed[2];
	real_point3d frame_positions[2][MAXIMUM_SIM_SUBSTEPS];
	struct interpolation_quaternion frame_rotations[2][MAXIMUM_SIM_SUBSTEPS];
	/* a projectile's root node's rotation after each, a bit each for those
	kept (one whose basis is a rotation): it turns in the sub-steps (falling,
	guided to a target), and is drawn turning between them */
	byte rotated[2];
	struct interpolation_quaternion rotations[2][MAXIMUM_SIM_SUBSTEPS];
	/* where it is drawn this frame, between two of the drawn tick's */
	long drawn_frame;
	boolean drawn_valid;
	real_point3d drawn;
	/* ... and its frame */
	long frame_drawn_frame;
	boolean frame_drawn_valid;
	real_matrix4x3 frame_drawn;
	/* how far (0 to 1) it is drawn from its newest sub-steps rather than
	the tick drawn's (render_interpolation_lead_weight), as of the frame
	lead_frame drew at lead_time (ticks) */
	long lead_frame;
	real lead_weight;
	double lead_time;
	/* the sub-step it was first kept after (made, or let go of), counted
	as render_interpolation_kept_position counts them, and where a
	projectile's frames start from (render_interpolation_projectile_lead) */
	long made_step;
	real_point3d made_from;
};

/* a projectile drawn from its newest sub-steps, its nodes as drawn in
frame (render_interpolation_projectile_nodes) */
struct lead_projectile
{
	long object_index;
	long frame;
	real_matrix4x3 nodes[MAXIMUM_LEAD_PROJECTILE_NODES];
};

struct interpolated_camera
{
	long tick;
	boolean valid;
	boolean has_previous;
	struct observer_result previous;
	struct observer_result latest;
	struct observer_result blended;
	/* its player's unit at the latest snapshot: another (a respawn, a
	checkpoint's revert, a unit swapped) is a cut, however near */
	long unit_index;
	/* its player's unit (or what it rides) corrected: as an object's */
	real_vector3d correction;
	real_vector3d correction_pending;
	/* in a seat of a vehicle of the micro lane: where the view hangs on it
	(its player's unit's camera position), in the frame of the vehicle's
	root node as the tick (anchor_tick) left it; anchor_vehicle_index NONE
	when it hangs on none */
	long anchor_tick;
	long anchor_vehicle_index;
	real_point3d anchor;
	/* a view that moves in the frames' time (render_interpolation_camera_
	timed): when each snapshot was taken (ticks, as interpolation_tick
	counts them, and the fraction of the frame that took it), and the
	snapshot before the previous one */
	boolean timed;
	boolean has_before;
	struct observer_result before;
	double before_time;
	double previous_time;
	double latest_time;
	/* ... a cut from the snapshot before to each of the last two
	(observer_timed_cut), and how far a tick the view last moved between
	two snapshots with no cut between them (0: not since it began) */
	boolean cut_to_previous;
	boolean cut_to_latest;
	real speed;
};

/* a local player's vehicle's last three steps (render_interpolation_newest_
motion): when each ended, in sub-steps of count (the clock's ticks times
count, and the sub-steps since), and where each left its root node; [0] the
newest */
struct newest_history
{
	long vehicle_index;
	short count; /* the steps kept, 0 to 3 */
	short substep_count;
	long steps[3];
	real_point3d positions[3];
	struct interpolation_quaternion rotations[3];
	/* the motion carried on in the frame before, from its newest step
	(model_step: where it left the root node), and the vehicle drawn's
	difference from the motion carried on now, gliding away since a step
	(render_interpolation_newest_blend) */
	boolean model_valid;
	long model_step;
	real_point3d model_position;
	struct interpolation_quaternion model_rotation;
	real_vector3d model_velocity;
	real_vector3d model_acceleration;
	real_vector3d model_spin;
	real_vector3d model_spin_acceleration;
	short blend_count;
	real_vector3d blend_places[NEWEST_BLENDS];
	real_vector3d blend_turns[NEWEST_BLENDS];
	real blend_ages[NEWEST_BLENDS]; /* ticks */
	double frame_time; /* the frame before's, in ticks */
	/* the view's pitch held at the seat's limit (render_interpolation_seat_
	pitch): the frames' pitches of the last step or so, and their times
	(ticks), the newest at pitch_next - 1 */
	short pitch_count;
	short pitch_next;
	double pitch_times[NEWEST_PITCH_FRAMES];
	real pitches[NEWEST_PITCH_FRAMES];
};

/* the motion of a vehicle's last steps, a tick's worth: at its newest step's
end, and its change a tick (render_interpolation_newest_motion) */
struct newest_motion
{
	real_vector3d velocity;
	real_vector3d acceleration;
	real_vector3d spin;
	real_vector3d spin_acceleration;
};

/* a local player's vehicle, which their view from its seat is drawn from as
its newest step left it (render_interpolation_newest_vehicles), with what it
carries */
struct newest_vehicle
{
	boolean valid; /* FALSE: none this frame */
	/* whether the player's aim in the seat is the world's, not turned to
	the vehicle's up (render_interpolation_seat) */
	boolean aim_in_world;
	long vehicle_index;
	long unit_index; /* the player's, in its seat */
	/* its newest step (render_interpolation_newest_step) and the frame's
	time since, in ticks */
	long step;
	real since;
	/* its root node as the newest step left it, and as drawn: carried on as
	that step moved and turned it, for the time since
	(render_interpolation_newest_motion) */
	real_matrix4x3 root;
	real_matrix4x3 drawn;
	/* its axes (the object's), as drawn */
	real_vector3d forward;
	real_vector3d up;
	/* where the view hangs on it (the unit's camera position) as the newest
	step left it, in the root's frame */
	real_point3d anchor;
	/* ... and where the frames between the ticks would draw that point: what
	the view following the vehicle swings about, between the ticks
	(render_interpolation_seat_camera) */
	real_point3d anchor_between;
	/* how far its root is drawn from where the frames between the ticks
	would draw it (its place), for the glide from one to the other */
	boolean ahead_valid;
	real_vector3d ahead;
};

/* a particle held back */
struct later_particle
{
	struct new_particle_data data;
	long object_index;
	short node_index;
	/* where on the node it was made */
	real_point3d node_position;
	/* made from this tick's frames on, this far into it */
	long tick;
	real fraction;
};

struct interpolated_first_person
{
	long tick;
	short node_count;
	boolean has_previous;
	real_matrix4x3 previous[MAXIMUM_INTERPOLATED_NODES];
	real_matrix4x3 latest[MAXIMUM_INTERPOLATED_NODES];
};

/* a projectile deleted in a tick, drawn through the frames after it */
struct interpolation_ghost
{
	boolean in_use;
	long object_index;
	long tick; /* drawn in the frames after this tick; NONE until it ends */
	/* drawn from its newest sub-steps: the sub-step it was deleted in
	(render_interpolation_kept_position's count), and of how many a tick;
	NONE when drawn a tick behind, by tick */
	long step;
	short substep_count;
	long model_index;
	short node_count;
	short forced_shader_permutation_index;
	/* the last snapshot, and where it was then and from there when deleted */
	real_point3d position;
	real_vector3d offset;
	/* the bounding sphere, from its position */
	real_vector3d center_offset;
	real radius;
	real_matrix4x3 nodes[MAXIMUM_GHOST_NODES];
	real_matrix4x3 drawn[MAXIMUM_GHOST_NODES];
	byte region_permutations[MAXIMUM_REGIONS_PER_OBJECT];
	real function_values[NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS];
	real_rgb_color change_colors[NUMBER_OF_OBJECT_CHANGE_COLORS];
	struct render_lighting lighting;
};

/* ---------- globals */

static struct interpolated_object *interpolated_objects;
static struct interpolated_camera interpolated_cameras[MAXIMUM_LOCAL_PLAYERS];
/* each local player's vehicle drawn from its newest step this frame
(render_interpolation_newest_vehicles) */
static struct newest_vehicle newest_vehicles[MAXIMUM_LOCAL_PLAYERS];
/* ... and its last steps */
static struct newest_history newest_histories[MAXIMUM_LOCAL_PLAYERS];
static struct interpolated_first_person interpolated_first_person[MAXIMUM_LOCAL_PLAYERS];
/* an object to be drawn from a place of this tick's choosing rather than
from where it was the tick before (render_interpolation_object_from) */
static struct
{
	long object_index;
	real_point3d position;
} interpolation_from = { NONE };
static long interpolation_tick;
static long interpolation_frame;
static boolean interpolation_rendering;
static real interpolation_fraction = 1.0f;
/* how long ago the clock's newest step fell due, in ticks
(game_time_get_step_age): what is drawn from the newest step is carried on
by this much */
static real interpolation_step_age;
static struct later_particle later_particles[MAXIMUM_LATER_PARTICLES];
static short later_particle_count;
static struct interpolation_ghost ghosts[MAXIMUM_GHOSTS];
/* by absolute index, made at the first sub-step of a tick of several */
static struct interpolated_substeps *interpolated_substeps;
/* the frame's sub-steps a tick (sim_substep_count), the sub-step it is
drawn in and how far (sim_substep_blend) */
static short interpolation_substep_count = 1;
static short interpolation_substep_index;
static real interpolation_substep_fraction;
/* the last sub-step the slot kept the poses after
(render_interpolation_kept_position's count) */
static long interpolation_substep_kept = NONE;
static struct lead_projectile lead_projectiles[MAXIMUM_LEAD_PROJECTILES];
static struct interpolated_shape interpolated_shapes[MAXIMUM_INTERPOLATED_SHAPES];

void render_interpolation_shape_forget(unsigned long owner, long index);

/* ---------- blending */

static real lerp(real a, real b, real t)
{
	return a + (b - a) * t;
}

static void point_lerp(real_point3d const *a, real_point3d const *b, real t, real_point3d *result)
{
	result->x = lerp(a->x, b->x, t);
	result->y = lerp(a->y, b->y, t);
	result->z = lerp(a->z, b->z, t);
}

static real vector_length(real_vector3d const *v)
{
	return (real)sqrt(v->i * v->i + v->j * v->j + v->k * v->k);
}

static void vector_nlerp(real_vector3d const *a, real_vector3d const *b, real t, real_vector3d *result)
{
	real length;

	result->i = lerp(a->i, b->i, t);
	result->j = lerp(a->j, b->j, t);
	result->k = lerp(a->k, b->k, t);
	length = vector_length(result);
	if (length > 1e-6f)
	{
		result->i /= length;
		result->j /= length;
		result->k /= length;
	}
	else
	{
		*result = *b;
	}
}

/* an orthonormal right-handed basis, which a quaternion can represent */
static boolean basis_is_rotation(real_matrix4x3 const *matrix)
{
	real_vector3d cross;
	real determinant;

	if (fabs(vector_length(&matrix->forward) - 1.0f) > 1e-2f ||
		fabs(vector_length(&matrix->left) - 1.0f) > 1e-2f ||
		fabs(vector_length(&matrix->up) - 1.0f) > 1e-2f)
	{
		return FALSE;
	}
	cross.i = matrix->forward.j * matrix->left.k - matrix->forward.k * matrix->left.j;
	cross.j = matrix->forward.k * matrix->left.i - matrix->forward.i * matrix->left.k;
	cross.k = matrix->forward.i * matrix->left.j - matrix->forward.j * matrix->left.i;
	determinant = cross.i * matrix->up.i + cross.j * matrix->up.j + cross.k * matrix->up.k;
	return determinant > 0.5f;
}

/* the basis vectors are the matrix's columns (x forward, y left, z up) */
static void quaternion_from_basis(real_matrix4x3 const *matrix, struct interpolation_quaternion *q)
{
	real m00 = matrix->forward.i, m10 = matrix->forward.j, m20 = matrix->forward.k;
	real m01 = matrix->left.i, m11 = matrix->left.j, m21 = matrix->left.k;
	real m02 = matrix->up.i, m12 = matrix->up.j, m22 = matrix->up.k;
	real trace = m00 + m11 + m22;
	real s;

	if (trace > 0.0f)
	{
		s = 0.5f / (real)sqrt(trace + 1.0f);
		q->w = 0.25f / s;
		q->i = (m21 - m12) * s;
		q->j = (m02 - m20) * s;
		q->k = (m10 - m01) * s;
	}
	else if (m00 > m11 && m00 > m22)
	{
		s = 2.0f * (real)sqrt(1.0f + m00 - m11 - m22);
		q->w = (m21 - m12) / s;
		q->i = 0.25f * s;
		q->j = (m01 + m10) / s;
		q->k = (m02 + m20) / s;
	}
	else if (m11 > m22)
	{
		s = 2.0f * (real)sqrt(1.0f + m11 - m00 - m22);
		q->w = (m02 - m20) / s;
		q->i = (m01 + m10) / s;
		q->j = 0.25f * s;
		q->k = (m12 + m21) / s;
	}
	else
	{
		s = 2.0f * (real)sqrt(1.0f + m22 - m00 - m11);
		q->w = (m10 - m01) / s;
		q->i = (m02 + m20) / s;
		q->j = (m12 + m21) / s;
		q->k = 0.25f * s;
	}
}

static void basis_from_quaternion(struct interpolation_quaternion const *q, real_matrix4x3 *matrix)
{
	real ii = q->i * q->i, jj = q->j * q->j, kk = q->k * q->k;
	real ij = q->i * q->j, ik = q->i * q->k, jk = q->j * q->k;
	real wi = q->w * q->i, wj = q->w * q->j, wk = q->w * q->k;

	matrix->forward.i = 1.0f - 2.0f * (jj + kk);
	matrix->forward.j = 2.0f * (ij + wk);
	matrix->forward.k = 2.0f * (ik - wj);
	matrix->left.i = 2.0f * (ij - wk);
	matrix->left.j = 1.0f - 2.0f * (ii + kk);
	matrix->left.k = 2.0f * (jk + wi);
	matrix->up.i = 2.0f * (ik + wj);
	matrix->up.j = 2.0f * (jk - wi);
	matrix->up.k = 1.0f - 2.0f * (ii + jj);
}

static void rotation_from_matrix(real_matrix4x3 const *matrix, struct interpolation_rotation *rotation)
{
	rotation->is_rotation = basis_is_rotation(matrix);
	if (rotation->is_rotation)
		quaternion_from_basis(matrix, &rotation->quaternion);
}

/* a times b: b's rotation, then a's */
static void quaternion_multiply(
	struct interpolation_quaternion const *a,
	struct interpolation_quaternion const *b,
	struct interpolation_quaternion *result)
{
	struct interpolation_quaternion q;

	q.w = a->w * b->w - a->i * b->i - a->j * b->j - a->k * b->k;
	q.i = a->w * b->i + a->i * b->w + a->j * b->k - a->k * b->j;
	q.j = a->w * b->j - a->i * b->k + a->j * b->w + a->k * b->i;
	q.k = a->w * b->k + a->i * b->j - a->j * b->i + a->k * b->w;
	*result = q;
}

/* the rotation a fraction t of the way from a to b, the shorter way round
(b where the two cancel) */
static void quaternion_nlerp(
	struct interpolation_quaternion const *a,
	struct interpolation_quaternion const *b,
	real t,
	struct interpolation_quaternion *result)
{
	struct interpolation_quaternion qb = *b, q;
	real length;

	if (a->i * qb.i + a->j * qb.j + a->k * qb.k + a->w * qb.w < 0.0f)
	{
		qb.i = -qb.i;
		qb.j = -qb.j;
		qb.k = -qb.k;
		qb.w = -qb.w;
	}
	q.i = lerp(a->i, qb.i, t);
	q.j = lerp(a->j, qb.j, t);
	q.k = lerp(a->k, qb.k, t);
	q.w = lerp(a->w, qb.w, t);
	length = (real)sqrt(q.i * q.i + q.j * q.j + q.k * q.k + q.w * q.w);
	if (length > 1e-6f)
	{
		result->i = q.i / length;
		result->j = q.j / length;
		result->k = q.k / length;
		result->w = q.w / length;
	}
	else
	{
		*result = *b;
	}
}

/* the vector turned by a rotation's basis (basis_from_quaternion's) */
static void vector_turned_by(real_matrix4x3 const *basis, real_vector3d const *vector, real_vector3d *result)
{
	real_vector3d v = *vector;

	result->i = basis->forward.i * v.i + basis->left.i * v.j + basis->up.i * v.k;
	result->j = basis->forward.j * v.i + basis->left.j * v.j + basis->up.j * v.k;
	result->k = basis->forward.k * v.i + basis->left.k * v.j + basis->up.k * v.k;
}

/* a matrix a fraction t of the way from a to b, their rotations found */
static void matrix_blend_rotations(
	real_matrix4x3 const *a,
	real_matrix4x3 const *b,
	struct interpolation_rotation const *rotation_a,
	struct interpolation_rotation const *rotation_b,
	real t,
	real_matrix4x3 *result)
{
	result->scale = lerp(a->scale, b->scale, t);
	point_lerp(&a->position, &b->position, t, &result->position);
	if (rotation_a->is_rotation && rotation_b->is_rotation)
	{
		struct interpolation_quaternion qa = rotation_a->quaternion, qb = rotation_b->quaternion, q;
		real length;

		/* q and -q are the same rotation: take the shorter way round */
		if (qa.i * qb.i + qa.j * qb.j + qa.k * qb.k + qa.w * qb.w < 0.0f)
		{
			qb.i = -qb.i;
			qb.j = -qb.j;
			qb.k = -qb.k;
			qb.w = -qb.w;
		}
		q.i = lerp(qa.i, qb.i, t);
		q.j = lerp(qa.j, qb.j, t);
		q.k = lerp(qa.k, qb.k, t);
		q.w = lerp(qa.w, qb.w, t);
		length = (real)sqrt(q.i * q.i + q.j * q.j + q.k * q.k + q.w * q.w);
		if (length > 1e-6f)
		{
			q.i /= length;
			q.j /= length;
			q.k /= length;
			q.w /= length;
			basis_from_quaternion(&q, result);
			return;
		}
	}
	/* a basis a quaternion cannot hold (scaled or mirrored): blend it as is */
	result->forward.i = lerp(a->forward.i, b->forward.i, t);
	result->forward.j = lerp(a->forward.j, b->forward.j, t);
	result->forward.k = lerp(a->forward.k, b->forward.k, t);
	result->left.i = lerp(a->left.i, b->left.i, t);
	result->left.j = lerp(a->left.j, b->left.j, t);
	result->left.k = lerp(a->left.k, b->left.k, t);
	result->up.i = lerp(a->up.i, b->up.i, t);
	result->up.j = lerp(a->up.j, b->up.j, t);
	result->up.k = lerp(a->up.k, b->up.k, t);
}

/* the vector's parts' sum, large enough to be a correction (so written that
one not a number is none) */
static boolean correction_significant(real_vector3d const *correction)
{
	return fabs(correction->i) + fabs(correction->j) + fabs(correction->k) >= CORRECTION_NEGLIGIBLE;
}

/* a correction a tick on: what was drawn fades, what came since is drawn
whole from now on, fading from the next */
static void correction_advance(real_vector3d *correction, real_vector3d *pending)
{
	correction->i = correction->i * CORRECTION_DECAY + pending->i;
	correction->j = correction->j * CORRECTION_DECAY + pending->j;
	correction->k = correction->k * CORRECTION_DECAY + pending->k;
	*pending = *global_zero_vector3d;
	if (!correction_significant(correction))
		*correction = *global_zero_vector3d;
}

/* a correction as drawn this frame: fading through the tick as it does tick
to tick, and those since the tick whole */
static void correction_drawn(real_vector3d const *correction, real_vector3d const *pending, real_vector3d *drawn)
{
	real fade = lerp(1.0f, CORRECTION_DECAY, interpolation_fraction);

	drawn->i = correction->i * fade + pending->i;
	drawn->j = correction->j * fade + pending->j;
	drawn->k = correction->k * fade + pending->k;
}

/* a correction added (offset): all of it dropped when the sum is too large
to glide (or not a number) */
static void correction_add(real_vector3d *correction, real_vector3d *pending, real_vector3d const *offset)
{
	real i, j, k;

	pending->i += offset->i;
	pending->j += offset->j;
	pending->k += offset->k;
	i = correction->i + pending->i;
	j = correction->j + pending->j;
	k = correction->k + pending->k;
	if (!(i * i + j * j + k * k <= CORRECTION_MAXIMUM * CORRECTION_MAXIMUM))
	{
		*correction = *global_zero_vector3d;
		*pending = *global_zero_vector3d;
	}
}

static real distance_squared(real_point3d const *a, real_point3d const *b)
{
	real x = a->x - b->x, y = a->y - b->y, z = a->z - b->z;

	return x * x + y * y + z * z;
}

/* A frame: a root node's matrix whose basis is a rotation (basis_is_rotation),
its scale aside. What is in it is seen from it, and placed by another, by
its place and its axes alone: the world's distances kept */

/* the vector seen from the frame (along its forward, left and up) */
static void vector_in_frame(real_matrix4x3 const *frame, real_vector3d const *vector, real_vector3d *result)
{
	real_vector3d v = *vector;

	result->i = v.i * frame->forward.i + v.j * frame->forward.j + v.k * frame->forward.k;
	result->j = v.i * frame->left.i + v.j * frame->left.j + v.k * frame->left.k;
	result->k = v.i * frame->up.i + v.j * frame->up.j + v.k * frame->up.k;
}

/* ... and one seen from it, in the world */
static void vector_from_frame(real_matrix4x3 const *frame, real_vector3d const *vector, real_vector3d *result)
{
	real_vector3d v = *vector;

	result->i = frame->forward.i * v.i + frame->left.i * v.j + frame->up.i * v.k;
	result->j = frame->forward.j * v.i + frame->left.j * v.j + frame->up.j * v.k;
	result->k = frame->forward.k * v.i + frame->left.k * v.j + frame->up.k * v.k;
}

static void point_in_frame(real_matrix4x3 const *frame, real_point3d const *point, real_point3d *result)
{
	real_vector3d offset, local;

	offset.i = point->x - frame->position.x;
	offset.j = point->y - frame->position.y;
	offset.k = point->z - frame->position.z;
	vector_in_frame(frame, &offset, &local);
	result->x = local.i;
	result->y = local.j;
	result->z = local.k;
}

static void point_from_frame(real_matrix4x3 const *frame, real_point3d const *point, real_point3d *result)
{
	real_vector3d local, offset;

	local.i = point->x;
	local.j = point->y;
	local.k = point->z;
	vector_from_frame(frame, &local, &offset);
	result->x = frame->position.x + offset.i;
	result->y = frame->position.y + offset.j;
	result->z = frame->position.z + offset.k;
}

/* a node's matrix seen from the frame (its scale its own) */
static void matrix_in_frame(real_matrix4x3 const *frame, real_matrix4x3 const *matrix, real_matrix4x3 *result)
{
	real_matrix4x3 m = *matrix;

	point_in_frame(frame, &m.position, &result->position);
	vector_in_frame(frame, &m.forward, &result->forward);
	vector_in_frame(frame, &m.left, &result->left);
	vector_in_frame(frame, &m.up, &result->up);
	result->scale = m.scale;
}

/* ... and one seen from it, in the world */
static void matrix_from_frame(real_matrix4x3 const *frame, real_matrix4x3 const *matrix, real_matrix4x3 *result)
{
	real_matrix4x3 m = *matrix;

	point_from_frame(frame, &m.position, &result->position);
	vector_from_frame(frame, &m.forward, &result->forward);
	vector_from_frame(frame, &m.left, &result->left);
	vector_from_frame(frame, &m.up, &result->up);
	result->scale = m.scale;
}

/* a frame a fraction t of the way from one (its place and rotation) to
another: the place lerped, the rotation nlerped the shorter way round */
static void frame_blend(
	real_point3d const *position_a,
	struct interpolation_quaternion const *rotation_a,
	real_point3d const *position_b,
	struct interpolation_quaternion const *rotation_b,
	real t,
	real_matrix4x3 *result)
{
	real_matrix4x3 a, b;
	struct interpolation_rotation ra, rb;

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	a.position = *position_a;
	a.scale = 1.0f;
	b.position = *position_b;
	b.scale = 1.0f;
	ra.quaternion = *rotation_a;
	ra.is_rotation = TRUE;
	rb.quaternion = *rotation_b;
	rb.is_rotation = TRUE;
	matrix_blend_rotations(&a, &b, &ra, &rb, t, result);
}

/* ---------- ticks */

static void render_interpolation_ghosts_tick(void);

/* the record's snapshot of the tick before, made of its latest (its nodes
already placed): the latest's rotations, values and camouflage */
static void render_interpolation_previous_values(struct interpolated_object *record)
{
	record->rotations_valid[record->latest ^ 1] = FALSE;
	memcpy(record->function_values[record->latest ^ 1], record->function_values[record->latest],
		sizeof(record->function_values[0]));
	memcpy(record->change_colors[record->latest ^ 1], record->change_colors[record->latest],
		sizeof(record->change_colors[0]));
	memcpy(record->camouflage[record->latest ^ 1], record->camouflage[record->latest],
		sizeof(record->camouflage[0]));
	record->facing[record->latest ^ 1] = record->facing[record->latest];
	record->has_previous = TRUE;
}

static void render_interpolation_substep(short substep_index, short substep_count, real seconds);
static void render_interpolation_substep_rotation(long object_index, struct object_datum const *object,
	struct interpolated_substeps *substeps, short buffer, short substep_index);
static void render_interpolation_camera_anchors(void);

/* the fast phase's slot (sim_rate.h), at every map's start and every tick
(registering again changes nothing) */
static void render_interpolation_register(void)
{
	sim_substep_register(_sim_substep_render_interpolation, render_interpolation_substep);
}

void render_interpolation_tick(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long previous_tick = interpolation_tick++;

	render_interpolation_register();
	render_interpolation_ghosts_tick();
	if (!halo_interpolation_enabled())
	{
		interpolation_from.object_index = NONE;
		return;
	}
	/* the cameras' corrections a tick on, as the objects' (below) */
	{
		short local_player_index;

		for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		{
			struct interpolated_camera *camera = &interpolated_cameras[local_player_index];

			if (camera->valid)
				correction_advance(&camera->correction, &camera->correction_pending);
		}
	}
	if (!interpolated_objects)
	{
		long index;

		interpolated_objects = calloc(MAXIMUM_INTERPOLATED_OBJECTS, sizeof(*interpolated_objects));
		if (!interpolated_objects)
			return;
		for (index = 0; index < MAXIMUM_INTERPOLATED_OBJECTS; index++)
			interpolated_objects[index].object_index = NONE;
	}

	object_iterator_new(&iterator, _object_mask_all, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		struct interpolated_object *record;
		short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
		boolean continuing;
		boolean made;

		if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
			continue;
		record = &interpolated_objects[absolute_index];
		/* (made since the last tick: none of its snapshots) */
		made = record->object_index != iterator.index;
		if (node_count <= 0 || node_count > MAXIMUM_INTERPOLATED_NODES)
		{
			record->object_index = NONE;
			continue;
		}
		if (record->node_capacity < node_count)
		{
			real_matrix4x3 *nodes = realloc(record->nodes, 3 * node_count * sizeof(real_matrix4x3));
			struct interpolation_rotation *rotations;
			real *node_moves;

			if (!nodes)
			{
				record->object_index = NONE;
				continue;
			}
			record->nodes = nodes;
			rotations = realloc(record->rotations, 2 * node_count * sizeof(struct interpolation_rotation));
			if (!rotations)
			{
				record->object_index = NONE;
				continue;
			}
			record->rotations = rotations;
			node_moves = realloc(record->node_moves, 2 * node_count * sizeof(real));
			if (!node_moves)
			{
				record->object_index = NONE;
				continue;
			}
			record->node_moves = node_moves;
			record->node_capacity = node_count;
			record->object_index = NONE; /* the old snapshots moved */
		}
		continuing = record->object_index == iterator.index &&
			record->node_count == node_count &&
			record->tick == previous_tick;
		if (continuing)
		{
			record->latest ^= 1;
			correction_advance(&record->correction, &record->correction_pending);
		}
		else
		{
			record->correction = *global_zero_vector3d;
			record->correction_pending = *global_zero_vector3d;
			record->node_moves_tick[0] = NONE;
			record->node_moves_tick[1] = NONE;
		}
		memcpy(
			record->nodes + record->latest * record->node_capacity,
			object_get_node_matrices(iterator.index),
			node_count * sizeof(real_matrix4x3));
		record->rotations_valid[record->latest] = FALSE;
		record->object_index = iterator.index;
		record->node_count = node_count;
		record->tick = interpolation_tick;
		record->has_previous = continuing;
		record->blended_frame = NONE;
		memcpy(record->function_values[record->latest], object->object.outgoing_function_values,
			sizeof(record->function_values[0]));
		memcpy(record->change_colors[record->latest], object->object.outgoing_change_colors,
			sizeof(record->change_colors[0]));
		record->camouflage[record->latest][0] = 0.0f;
		record->camouflage[record->latest][1] = 0.0f;
		if (TEST_FLAG(_object_mask_unit, object->object.type))
		{
			struct unit_datum *unit = (struct unit_datum *)object;

			record->camouflage[record->latest][0] = unit->unit.active_camouflage;
			record->camouflage[record->latest][1] = unit->unit.active_camouflage_super_amount;
		}
		record->values_blended_frame = NONE;
		record->facing[record->latest] = object->object.forward;
		if (!continuing)
			record->facing[record->latest ^ 1] = object->object.forward;
		record->previous_position = continuing ? record->position : object->object.position;
		record->position = object->object.position;
		/* the tick's last sub-step's pose kept: where the tick left it (what
		the tick did after its fast phase too), as its snapshot has it */
		if (interpolated_substeps)
		{
			struct interpolated_substeps *substeps = &interpolated_substeps[absolute_index];
			short buffer = (short)(interpolation_tick & 1);

			if (substeps->object_index == iterator.index && substeps->tick[buffer] == interpolation_tick)
			{
				substeps->positions[buffer][substeps->count[buffer] - 1] = object->object.position;
				render_interpolation_substep_rotation(iterator.index, object, substeps, buffer,
					(short)(substeps->count[buffer] - 1));
				/* (and a vehicle's frame: its snapshot's root node) */
				if (substeps->framed[buffer])
				{
					real_matrix4x3 const *root = record->nodes + record->latest * record->node_capacity;
					struct interpolation_rotation rotation;

					rotation_from_matrix(root, &rotation);
					substeps->framed[buffer] = rotation.is_rotation;
					if (rotation.is_rotation)
					{
						substeps->frame_positions[buffer][substeps->count[buffer] - 1] = root->position;
						substeps->frame_rotations[buffer][substeps->count[buffer] - 1] = rotation.quaternion;
					}
				}
			}
		}
		/* Made in this tick, and moving (a bolt, a rocket, a grenade
		thrown): it has no snapshot from the tick before, and was drawn
		standing for a tick where this one left it (a bolt hung a tick's
		motion out from the muzzle, then set off), and a tick early, ahead
		of the world drawn about it. The tick before's snapshot: this one's,
		half a tick's motion back (a whole tick back is where it was made,
		for a bolt the eye, where its sprite would fill the view). (Not an
		object something carries, which moves with it; one that moves
		further than a tick allows snaps, as before.) */
		if (made && object->object.parent_object_index == NONE &&
			(object->object.translational_velocity.i != 0.0f ||
				object->object.translational_velocity.j != 0.0f ||
				object->object.translational_velocity.k != 0.0f))
		{
			real_matrix4x3 const *latest = record->nodes + record->latest * record->node_capacity;
			real_matrix4x3 *previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
			real_vector3d const *velocity = &object->object.translational_velocity;
			short node_index;

			memcpy(previous, latest, node_count * sizeof(real_matrix4x3));
			for (node_index = 0; node_index < node_count; node_index++)
			{
				previous[node_index].position.x -= 0.5f * velocity->i;
				previous[node_index].position.y -= 0.5f * velocity->j;
				previous[node_index].position.z -= 0.5f * velocity->k;
			}
			record->previous_position.x -= 0.5f * velocity->i;
			record->previous_position.y -= 0.5f * velocity->j;
			record->previous_position.z -= 0.5f * velocity->k;
			render_interpolation_previous_values(record);
		}
		if (iterator.index == interpolation_from.object_index)
		{
			/* the tick before's snapshot: this one's, moved to the place */
			real_matrix4x3 const *latest = record->nodes + record->latest * record->node_capacity;
			real_matrix4x3 *previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
			real_vector3d offset;
			short node_index;

			offset.i = interpolation_from.position.x - latest[0].position.x;
			offset.j = interpolation_from.position.y - latest[0].position.y;
			offset.k = interpolation_from.position.z - latest[0].position.z;
			memcpy(previous, latest, node_count * sizeof(real_matrix4x3));
			for (node_index = 0; node_index < node_count; node_index++)
			{
				previous[node_index].position.x += offset.i;
				previous[node_index].position.y += offset.j;
				previous[node_index].position.z += offset.k;
			}
			record->previous_position.x = record->position.x + offset.i;
			record->previous_position.y = record->position.y + offset.j;
			record->previous_position.z = record->position.z + offset.k;
			render_interpolation_previous_values(record);
		}
	}
	interpolation_from.object_index = NONE;
	render_interpolation_camera_anchors();
}

/* the object, put somewhere this tick, drawn from there to where the tick
leaves it, not from where it was the tick before: the VR mode's grenade let
go of from where the hand held it (port/linux/game/vr_grenade.c), not from
the hand of the player's body it hung from */
void render_interpolation_object_from(long object_index, real_point3d const *position)
{
	interpolation_from.object_index = object_index;
	interpolation_from.position = *position;
}

/* a new map (game.c): its objects take the indices of the last one's, and
nothing of theirs is drawn from */
void render_interpolation_reset(void)
{
	long index;

	render_interpolation_register();
	if (interpolated_objects)
	{
		for (index = 0; index < MAXIMUM_INTERPOLATED_OBJECTS; index++)
			interpolated_objects[index].object_index = NONE;
	}
	if (interpolated_substeps)
	{
		for (index = 0; index < MAXIMUM_INTERPOLATED_OBJECTS; index++)
			interpolated_substeps[index].object_index = NONE;
	}
	memset(interpolated_cameras, 0, sizeof(interpolated_cameras));
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
		interpolated_cameras[index].anchor_vehicle_index = NONE;
	/* (and nothing of the last map's to glide from, or carry on) */
	memset(newest_vehicles, 0, sizeof(newest_vehicles));
	memset(newest_histories, 0, sizeof(newest_histories));
	/* (the particles held back and the ghosts are the last map's) */
	later_particle_count = 0;
	for (index = 0; index < MAXIMUM_GHOSTS; index++)
		ghosts[index].in_use = FALSE;
	/* (and the widgets' shapes) */
	for (index = 0; index < MAXIMUM_INTERPOLATED_SHAPES; index++)
		interpolated_shapes[index].owner = 0;
	for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
	{
		interpolated_first_person[index].node_count = 0;
		interpolated_first_person[index].has_previous = FALSE;
	}
}

/* ---------- sub-steps */

/* (the fast phase's _sim_substep_render_interpolation slot, after every
other that moves anything) where each object of the micro lane is after the
sub-step, for the frames to draw it between them. Of the open tick, the one
after the tick drawn: kept beside the drawn tick's. Read only, of the game:
nothing of the simulation sees it */
static void render_interpolation_substep(short substep_index, short substep_count, real seconds)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long tick = interpolation_tick + 1;
	short buffer = (short)(tick & 1);

	(void)seconds;
	if (substep_count <= 1 || substep_count > MAXIMUM_SIM_SUBSTEPS || !halo_interpolation_enabled())
		return;
	if (!interpolated_substeps)
	{
		long index;

		interpolated_substeps = calloc(MAXIMUM_INTERPOLATED_OBJECTS, sizeof(*interpolated_substeps));
		if (!interpolated_substeps)
			return;
		for (index = 0; index < MAXIMUM_INTERPOLATED_OBJECTS; index++)
			interpolated_substeps[index].object_index = NONE;
	}
	interpolation_substep_kept = tick * substep_count + substep_index;
	/* (the micro lane's types: objects.c's object_lane_micro_candidate) */
	object_iterator_new(&iterator,
		_object_mask_biped | _object_mask_vehicle | _object_mask_projectile | _object_mask_item, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.index);
		struct interpolated_substeps *substeps;

		/* (what is carried is drawn with what carries it) */
		if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS || object->object.parent_object_index != NONE ||
			object_get_lane(iterator.index) != _object_lane_micro)
		{
			continue;
		}
		substeps = &interpolated_substeps[absolute_index];
		if (substeps->object_index != iterator.index)
		{
			substeps->object_index = iterator.index;
			substeps->tick[0] = NONE;
			substeps->tick[1] = NONE;
			substeps->drawn_frame = NONE;
			substeps->frame_drawn_frame = NONE;
			substeps->lead_frame = NONE;
			/* (where a projectile's frames start: half a tick's motion back
			from where its head start put it; for one let go of (a grenade
			thrown, which was drawn in the hand), the whole tick back, where
			the hand let it go, or where it was put) */
			substeps->made_step = interpolation_substep_kept;
			if (iterator.index == interpolation_from.object_index)
			{
				substeps->made_from = interpolation_from.position;
			}
			else
			{
				real back = interpolated_objects && interpolated_objects[absolute_index].object_index == iterator.index ?
					1.0f : 0.5f;

				substeps->made_from.x = object->object.position.x - back * object->object.translational_velocity.i;
				substeps->made_from.y = object->object.position.y - back * object->object.translational_velocity.j;
				substeps->made_from.z = object->object.position.z - back * object->object.translational_velocity.k;
			}
			/* (made in a sub-step after the tick's first: kept from it) */
			if (substep_index > 0)
			{
				substeps->tick[buffer] = tick;
				substeps->count[buffer] = substep_count;
				substeps->recorded[buffer] = 0;
				substeps->rotated[buffer] = 0;
				substeps->continuing[buffer] = FALSE;
				substeps->framed[buffer] = FALSE;
			}
		}
		/* (a tick given up and begun again begins again here) */
		if (substep_index == 0)
		{
			short other = buffer ^ 1;

			substeps->continuing[buffer] = substeps->tick[other] == tick - 1 &&
				substeps->count[other] == substep_count &&
				substeps->recorded[other] == (byte)(FLAG(substep_count) - 1);
			substeps->tick[buffer] = tick;
			substeps->count[buffer] = substep_count;
			substeps->recorded[buffer] = 0;
			substeps->rotated[buffer] = 0;
			substeps->framed[buffer] = TEST_FLAG(_object_mask_vehicle, object->object.type) &&
				object->object.node_matrices.size >= (short)sizeof(real_matrix4x3);
		}
		else if (substeps->tick[buffer] != tick || substeps->count[buffer] != substep_count)
		{
			continue;
		}
		substeps->positions[buffer][substep_index] = object->object.position;
		substeps->recorded[buffer] |= (byte)FLAG(substep_index);
		render_interpolation_substep_rotation(iterator.index, object, substeps, buffer, substep_index);
		/* a vehicle's frame: its root node, as the sub-step posed it
		(vehicles.c's vehicle_substep_pose) */
		if (substeps->framed[buffer])
		{
			real_matrix4x3 const *root = object_get_node_matrices(iterator.index);
			struct interpolation_rotation rotation;

			rotation_from_matrix(root, &rotation);
			substeps->framed[buffer] = rotation.is_rotation;
			if (rotation.is_rotation)
			{
				substeps->frame_positions[buffer][substep_index] = root->position;
				substeps->frame_rotations[buffer][substep_index] = rotation.quaternion;
			}
		}
	}
}

/* (render_interpolation_substep, and the tick's end) a projectile's root
node's rotation as the sub-step left it, kept beside where it is */
static void render_interpolation_substep_rotation(long object_index, struct object_datum const *object,
	struct interpolated_substeps *substeps, short buffer, short substep_index)
{
	struct interpolation_rotation rotation;

	if (object->object.type != _object_type_projectile ||
		object->object.node_matrices.size < (short)sizeof(real_matrix4x3))
	{
		return;
	}
	rotation_from_matrix(object_get_node_matrices(object_index), &rotation);
	if (rotation.is_rotation)
	{
		substeps->rotations[buffer][substep_index] = rotation.quaternion;
		substeps->rotated[buffer] |= (byte)FLAG(substep_index);
	}
	else
	{
		substeps->rotated[buffer] &= (byte)~FLAG(substep_index);
	}
}

/* An object of the micro lane in a network game at 60 or 120 is drawn from
its newest sub-steps, not a tick behind. A frame of the tick drawn is the
tick's own fraction of the way from the tick before's end: a tick behind the
clock, where the newest sub-steps the clock has run (the open tick's, which
the frames would draw next) are a sub-step behind it at most. Another
machine's player, the vehicle they drive and an actor were drawn 25 to 33
ms (17 to 33 at 60) behind where this machine's simulation had them (where
its shots hit them), and are drawn between its two newest sub-steps instead:
0 to 8 ms behind (0 to 17 at 60), a sub-step's interpolation still, never
past a step the simulation has run. Only where it is moves so (and a
vehicle's frame): its pose, the aim of its head and gun, is still blended
between the ticks, moved there as the sub-steps' path moves it. Not what a
local player is or rides, whose view hangs on the tick drawn
(render_interpolation_blended_camera, or the newest step's own:
render_interpolation_newest_vehicles); not an item; a projectile
otherwise (render_interpolation_projectile_lead); not in a cinematic; and not a biped riding a lift that moves, which is of the macro
lane and drawn a tick behind. Single player, and at 30 (one sub-step a
tick: the newest is the tick drawn's end), are as they were */

/* whether the object's nodes moved between the ticks drawn, or since: a lift
moving (or not known) */
static boolean render_interpolation_nodes_moved(long object_index)
{
	struct object_datum *object = object_get(object_index);
	struct interpolated_object const *record;
	real_matrix4x3 const *previous, *latest, *now;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	short node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	short node_index;

	if (!interpolated_objects || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return TRUE;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != object_index || record->tick != interpolation_tick || !record->has_previous ||
		record->node_count != node_count)
	{
		return TRUE;
	}
	previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
	latest = record->nodes + record->latest * record->node_capacity;
	/* (as it is now: object_get_node_matrices gives the drawn pose while a
	frame is drawn) */
	now = (real_matrix4x3 const *)object_header_block_get(object_index, &object->object.node_matrices);
	for (node_index = 0; node_index < node_count; node_index++)
	{
		if (distance_squared(&previous[node_index].position, &latest[node_index].position) != 0.0f ||
			distance_squared(&latest[node_index].position, &now[node_index].position) != 0.0f)
		{
			return TRUE;
		}
	}
	return FALSE;
}

/* whether the object of the micro lane (by itself) is to be drawn from its
newest sub-steps (above) */
static boolean render_interpolation_lead_wanted(long root_index)
{
	struct object_datum *root = object_get(root_index);
	short local_player_index;

	if (!sim_rate_locked() || cinematic_in_progress() ||
		!TEST_FLAG(_object_mask_biped | _object_mask_vehicle, root->object.type))
	{
		return FALSE;
	}
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		long unit_index = player_control_get_unit_index(local_player_index);

		if (unit_index != NONE && object_try_and_get(unit_index) && object_get_ultimate_parent(unit_index) == root_index)
			return FALSE;
	}
	if (root->object.type == _object_type_biped)
	{
		long elevator_index = ((struct biped_datum *)root)->biped.elevator_object_index;

		if (elevator_index != NONE && object_try_and_get(elevator_index) &&
			render_interpolation_nodes_moved(elevator_index))
		{
			return FALSE;
		}
	}
	return TRUE;
}

/* (the frames) how far (0 to 1) the object is drawn from its newest
sub-steps this frame: wanted or not, gliding there or back over
LEAD_GLIDE_TICKS of the frames' time, rather than a jump of a sub-step's
part of a tick's motion; at once when first drawn. Once a frame */
static real render_interpolation_lead_weight(long root_index, struct interpolated_substeps *substeps)
{
	double now = (double)interpolation_tick + (double)interpolation_fraction;
	real target;

	if (substeps->lead_frame == interpolation_frame)
		return substeps->lead_weight;
	target = render_interpolation_lead_wanted(root_index) ? 1.0f : 0.0f;
	/* (so written that a weight not a number is the target) */
	if (substeps->lead_frame == NONE || !(now >= substeps->lead_time) ||
		!(substeps->lead_weight >= 0.0f && substeps->lead_weight <= 1.0f))
	{
		substeps->lead_weight = target;
	}
	else
	{
		real step = (real)(now - substeps->lead_time) / LEAD_GLIDE_TICKS;

		if (substeps->lead_weight < target)
			substeps->lead_weight = substeps->lead_weight + step < target ? substeps->lead_weight + step : target;
		else
			substeps->lead_weight = substeps->lead_weight - step > target ? substeps->lead_weight - step : target;
	}
	substeps->lead_frame = interpolation_frame;
	substeps->lead_time = now;
	return substeps->lead_weight;
}

/* where the object's sub-steps left it (and, framed, its frame), counted
from the tick drawn's first: up to count - 1 the tick drawn's, from count
the open tick's (sim_substep_next), those it has run. FALSE for one not
kept */
static boolean render_interpolation_lead_step(
	struct interpolated_substeps const *substeps,
	short index,
	boolean framed,
	real_point3d *position,
	struct interpolation_quaternion *rotation)
{
	short count = interpolation_substep_count;
	short buffer = (short)(interpolation_tick & 1);

	if (index < 0 || index >= 2 * count)
		return FALSE;
	if (index >= count)
	{
		buffer ^= 1;
		index -= count;
		if (substeps->tick[buffer] != interpolation_tick + 1 || substeps->count[buffer] != count ||
			!TEST_FLAG(substeps->recorded[buffer], index) || (framed && !substeps->framed[buffer]))
		{
			return FALSE;
		}
	}
	if (framed)
	{
		*position = substeps->frame_positions[buffer][index];
		*rotation = substeps->frame_rotations[buffer][index];
	}
	else
	{
		*position = substeps->positions[buffer][index];
	}
	return TRUE;
}

/* the newer of the two sub-steps a frame is drawn between from the newest
(the frame's sub-step of the tick drawn, count - 1 on: in the open tick from
its second frame's), as render_interpolation_lead_step counts them */
static short render_interpolation_lead_newest(void)
{
	return (short)(interpolation_substep_index + interpolation_substep_count - 1);
}

/* where the object's sub-steps left it after a sub-step counted from the
clock's first (the ticks as interpolation_tick counts them, times count,
and the sub-steps since: the open tick's are interpolation_tick + 1's).
FALSE for one not kept */
static boolean render_interpolation_kept_position(
	struct interpolated_substeps const *substeps,
	long step,
	short count,
	real_point3d *position)
{
	long tick;
	short buffer, index;

	if (step < 0 || count <= 1)
		return FALSE;
	tick = step / count;
	buffer = (short)(tick & 1);
	index = (short)(step % count);
	if (substeps->tick[buffer] != tick || substeps->count[buffer] != count ||
		!TEST_FLAG(substeps->recorded[buffer], index))
	{
		return FALSE;
	}
	*position = substeps->positions[buffer][index];
	return TRUE;
}

/* ... and a projectile's root node's rotation then (FALSE for one not kept,
or not a rotation) */
static boolean render_interpolation_kept_rotation(
	struct interpolated_substeps const *substeps,
	long step,
	short count,
	struct interpolation_quaternion *rotation)
{
	long tick;
	short buffer, index;

	if (step < 0 || count <= 1)
		return FALSE;
	tick = step / count;
	buffer = (short)(tick & 1);
	index = (short)(step % count);
	if (substeps->tick[buffer] != tick || substeps->count[buffer] != count ||
		!TEST_FLAG(substeps->recorded[buffer], index) || !TEST_FLAG(substeps->rotated[buffer], index))
	{
		return FALSE;
	}
	*rotation = substeps->rotations[buffer][index];
	return TRUE;
}

/* the newer of the two sub-steps a frame is drawn between from the newest
(render_interpolation_lead_newest), as render_interpolation_kept_position
counts them */
static long render_interpolation_lead_to_step(void)
{
	return interpolation_tick * interpolation_substep_count + render_interpolation_lead_newest();
}

/* A projectile (a bolt, a bullet, a needle, a rocket, a grenade thrown) at
60 and 120 is drawn from its newest sub-steps, as another's player is in a
network game, in any game but a cinematic: a tick behind, a shot was drawn
25 to 33 ms (17 to 33 at 60) after the simulation fired it, short of what
its impact showed, and another's left from a tick behind where the player
was drawn. Its first update is its head start, a whole tick's travel at
once, then it holds for a tick (projectiles.c): from the frame after the
sub-step it was made in, it is drawn from half a tick's motion back from
where the head start put it (a whole tick back is where it was made, for a
bolt the eye, where its sprite would fill the view) or from where it was
let go of (render_interpolation_object_from), to there over the tick it
holds, and on its sub-steps from then: never past a sub-step the
simulation ran, and never further than where it was deleted
(render_interpolation_object_deleted). Its pose is its newest, moved
there, and turned (rotated, when both its sub-steps' rotations were kept)
to the rotation between those of the sub-steps it is drawn between: its
newest alone turned in steps as each sub-step turned it (a mortar's arc, a
bolt guided to its target), 60 or 120 times a second whatever the frame
rate, where at 30 it turns between the ticks. FALSE when it is drawn
otherwise (one sub-step a tick, carried, in a cinematic, none kept) */
static boolean render_interpolation_projectile_lead(long object_index, real_point3d *drawn,
	struct interpolation_quaternion *rotation, boolean *rotated)
{
	struct object_datum *object = object_get(object_index);
	struct interpolated_substeps const *substeps;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	short count = interpolation_substep_count;
	long to_step;
	real_point3d from, to;
	struct interpolation_quaternion from_rotation, to_rotation;
	boolean has_from, has_to;

	*rotated = FALSE;
	if (count <= 1 || !interpolated_substeps || object->object.type != _object_type_projectile ||
		object->object.parent_object_index != NONE || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS ||
		cinematic_in_progress())
	{
		return FALSE;
	}
	substeps = &interpolated_substeps[absolute_index];
	if (substeps->object_index != object_index)
		return FALSE;
	to_step = render_interpolation_lead_to_step();
	/* (its head start and hold: from where its frames start, reaching where
	they put it as the hold ends) */
	if (to_step - substeps->made_step < count &&
		render_interpolation_kept_position(substeps, substeps->made_step, count, &to))
	{
		real part = ((real)(to_step - substeps->made_step) + interpolation_substep_fraction) / (real)count;

		point_lerp(&substeps->made_from, &to, part > 0.0f ? part : 0.0f, drawn);
		/* (turned as its first sub-step left it) */
		*rotated = render_interpolation_kept_rotation(substeps, substeps->made_step, count, rotation);
		return TRUE;
	}
	has_from = render_interpolation_kept_position(substeps, to_step - 1, count, &from);
	has_to = render_interpolation_kept_position(substeps, to_step, count, &to);
	/* (so written that a position not a number is a jump) */
	if (has_from && has_to && distance_squared(&from, &to) <= OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE)
	{
		point_lerp(&from, &to, interpolation_substep_fraction, drawn);
		if (render_interpolation_kept_rotation(substeps, to_step - 1, count, &from_rotation) &&
			render_interpolation_kept_rotation(substeps, to_step, count, &to_rotation))
		{
			quaternion_nlerp(&from_rotation, &to_rotation, interpolation_substep_fraction, rotation);
			*rotated = TRUE;
		}
	}
	else if (has_to)
	{
		*drawn = to;
		*rotated = render_interpolation_kept_rotation(substeps, to_step, count, rotation);
	}
	/* (the newer not run yet: where the last left it) */
	else if (has_from)
	{
		*drawn = from;
		*rotated = render_interpolation_kept_rotation(substeps, to_step - 1, count, rotation);
	}
	else
	{
		return FALSE;
	}
	return TRUE;
}

/* (the frames) the projectile's nodes drawn from its newest sub-steps
(above), NULL when it is drawn otherwise (or another's took its slot this
frame) */
static real_matrix4x3 *render_interpolation_projectile_nodes(long object_index)
{
	struct object_datum *object;
	struct lead_projectile *lead;
	real_matrix4x3 const *current;
	real_point3d drawn;
	struct interpolation_quaternion rotation;
	boolean rotated;
	struct interpolation_rotation now;
	real_matrix4x3 turn;
	short node_count;
	short node_index;

	if (!render_interpolation_projectile_lead(object_index, &drawn, &rotation, &rotated))
		return NULL;
	lead = &lead_projectiles[DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index) % MAXIMUM_LEAD_PROJECTILES];
	if (lead->frame == interpolation_frame)
		return lead->object_index == object_index ? lead->nodes : NULL;
	object = object_get(object_index);
	node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	if (node_count <= 0 || node_count > MAXIMUM_LEAD_PROJECTILE_NODES)
		return NULL;
	/* (as it is now: object_get_node_matrices gives the drawn pose while a
	frame is drawn) */
	current = (real_matrix4x3 const *)object_header_block_get(object_index, &object->object.node_matrices);
	/* (the turn from its root node's rotation now to the one drawn, about
	where it is) */
	rotation_from_matrix(&current[0], &now);
	rotated = rotated && now.is_rotation;
	if (rotated)
	{
		struct interpolation_quaternion inverse = now.quaternion, delta;

		inverse.i = -inverse.i;
		inverse.j = -inverse.j;
		inverse.k = -inverse.k;
		quaternion_multiply(&rotation, &inverse, &delta);
		basis_from_quaternion(&delta, &turn);
	}
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_vector3d offset;

		lead->nodes[node_index] = current[node_index];
		offset.i = current[node_index].position.x - object->object.position.x;
		offset.j = current[node_index].position.y - object->object.position.y;
		offset.k = current[node_index].position.z - object->object.position.z;
		if (rotated)
		{
			vector_turned_by(&turn, &offset, &offset);
			vector_turned_by(&turn, &current[node_index].forward, &lead->nodes[node_index].forward);
			vector_turned_by(&turn, &current[node_index].left, &lead->nodes[node_index].left);
			vector_turned_by(&turn, &current[node_index].up, &lead->nodes[node_index].up);
		}
		lead->nodes[node_index].position.x = drawn.x + offset.i;
		lead->nodes[node_index].position.y = drawn.y + offset.j;
		lead->nodes[node_index].position.z = drawn.z + offset.k;
	}
	lead->object_index = object_index;
	lead->frame = interpolation_frame;
	return lead->nodes;
}

/* (the frames) how far from where the line between the drawn tick's
snapshots has it (from where the tick left it, when its pose is drawn as
that: snapped) the object is drawn this frame, by itself or with what
carries it: where the sub-steps of the tick drawn put what carries it, at
the frame's sub-step, from there. FALSE when it is drawn on the line (one
sub-step a tick, of the macro lane, made in the tick, moved further than a
tick allows) */
static boolean render_interpolation_substep_offset(long object_index, boolean snapped, real_vector3d *offset)
{
	struct interpolated_object *record;
	struct interpolated_substeps *substeps;
	short buffer = (short)(interpolation_tick & 1);
	short count = interpolation_substep_count;
	real_point3d from;
	real_point3d lead_from, lead_to;
	real weight;
	long root_index;
	long absolute_index;

	if (count <= 1 || !interpolated_substeps || !interpolated_objects || object_index == NONE)
		return FALSE;
	root_index = object_get_ultimate_parent(object_index);
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(root_index);
	if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return FALSE;
	substeps = &interpolated_substeps[absolute_index];
	record = &interpolated_objects[absolute_index];
	/* (every sub-step of the tick drawn kept, as many as the frame's, and of
	the tick before: not one made, or let go of, in either, which is drawn
	between the ticks as before. A projectile's first update is its head
	start, a whole tick's travel at once, and it then holds for a tick
	(projectiles.c): drawn so, it would dart out and wait. The line from a
	tick's motion back (or from where it was let go of) draws its first
	tick, and the next, its hold's end, as at 30) */
	if (substeps->object_index != root_index || substeps->tick[buffer] != interpolation_tick ||
		substeps->count[buffer] != count || substeps->recorded[buffer] != (byte)(FLAG(count) - 1) ||
		!substeps->continuing[buffer] ||
		record->object_index != root_index || record->tick != interpolation_tick || !record->has_previous)
	{
		return FALSE;
	}
	if (substeps->drawn_frame != interpolation_frame)
	{
		real_point3d const *before = interpolation_substep_index > 0 ?
			&substeps->positions[buffer][interpolation_substep_index - 1] :
			&record->previous_position;

		/* (so written that a position not a number is none) */
		substeps->drawn_valid = distance_squared(&record->previous_position, &record->position) <=
			OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE;
		point_lerp(before, &substeps->positions[buffer][interpolation_substep_index], interpolation_substep_fraction,
			&substeps->drawn);
		/* (from its newest sub-steps, as far as it is drawn from them) */
		weight = render_interpolation_lead_weight(root_index, substeps);
		if (weight > 0.0f &&
			render_interpolation_lead_step(substeps, (short)(render_interpolation_lead_newest() - 1), FALSE, &lead_from,
				NULL) &&
			render_interpolation_lead_step(substeps, render_interpolation_lead_newest(), FALSE, &lead_to, NULL))
		{
			real_point3d lead;

			point_lerp(&lead_from, &lead_to, interpolation_substep_fraction, &lead);
			point_lerp(&substeps->drawn, &lead, weight, &substeps->drawn);
		}
		substeps->drawn_frame = interpolation_frame;
	}
	if (!substeps->drawn_valid)
		return FALSE;
	if (snapped)
		from = record->position;
	else
		point_lerp(&record->previous_position, &record->position, interpolation_fraction, &from);
	offset->i = substeps->drawn.x - from.x;
	offset->j = substeps->drawn.y - from.y;
	offset->k = substeps->drawn.z - from.z;
	/* (so written that an offset not a number is none) */
	return offset->i * offset->i + offset->j * offset->j + offset->k * offset->k <=
		SUBSTEP_OFFSET_MAXIMUM * SUBSTEP_OFFSET_MAXIMUM;
}

/* (render_interpolation_substep_frame) the vehicle's frame drawn moved from
the tick drawn's sub-steps toward its newest, as far as it is drawn from
them */
static void render_interpolation_lead_frame(long vehicle_index, struct interpolated_substeps *substeps)
{
	real weight = render_interpolation_lead_weight(vehicle_index, substeps);
	struct interpolation_quaternion from_rotation, to_rotation;
	real_point3d from_position, to_position;
	real_matrix4x3 lead;

	if (!(weight > 0.0f) ||
		!render_interpolation_lead_step(substeps, (short)(render_interpolation_lead_newest() - 1), TRUE, &from_position,
			&from_rotation) ||
		!render_interpolation_lead_step(substeps, render_interpolation_lead_newest(), TRUE, &to_position, &to_rotation))
	{
		return;
	}
	frame_blend(&from_position, &from_rotation, &to_position, &to_rotation, interpolation_substep_fraction, &lead);
	if (weight < 1.0f)
	{
		struct interpolation_rotation drawn_rotation, lead_rotation;

		rotation_from_matrix(&substeps->frame_drawn, &drawn_rotation);
		rotation_from_matrix(&lead, &lead_rotation);
		if (!drawn_rotation.is_rotation || !lead_rotation.is_rotation)
			return;
		frame_blend(&substeps->frame_drawn.position, &drawn_rotation.quaternion, &lead.position,
			&lead_rotation.quaternion, weight, &lead);
	}
	substeps->frame_drawn = lead;
}

/* (the frames) the frame of a vehicle of the micro lane (its root node:
place and rotation) as drawn this frame, by itself or with what it carries:
between the frames the sub-steps of the tick drawn left it in, at the
frame's sub-step (as render_interpolation_substep_offset's place), with the
frames of its two snapshots (frames[0]: the tick before's, [1]: the tick
drawn's), which its nodes and those of what it carries are blended in.
FALSE when it is drawn otherwise (no vehicle's, a root node no rotation, as
render_interpolation_substep_offset's: its place alone, or on the line) */
static boolean render_interpolation_substep_frame(
	long object_index,
	real_matrix4x3 const **frames,
	real_matrix4x3 const **drawn)
{
	struct interpolated_object *record;
	struct interpolated_substeps *substeps;
	short buffer = (short)(interpolation_tick & 1);
	short count = interpolation_substep_count;
	long vehicle_index;
	long absolute_index;

	if (count <= 1 || !interpolated_substeps || !interpolated_objects || object_index == NONE)
		return FALSE;
	vehicle_index = object_get_ultimate_parent(object_index);
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index);
	if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return FALSE;
	substeps = &interpolated_substeps[absolute_index];
	record = &interpolated_objects[absolute_index];
	/* (as render_interpolation_substep_offset's, and every frame kept) */
	if (substeps->object_index != vehicle_index || substeps->tick[buffer] != interpolation_tick ||
		substeps->count[buffer] != count || substeps->recorded[buffer] != (byte)(FLAG(count) - 1) ||
		!substeps->framed[buffer] || !substeps->continuing[buffer] ||
		record->object_index != vehicle_index || record->tick != interpolation_tick || !record->has_previous)
	{
		return FALSE;
	}
	frames[0] = record->nodes + (record->latest ^ 1) * record->node_capacity;
	frames[1] = record->nodes + record->latest * record->node_capacity;
	if (substeps->frame_drawn_frame != interpolation_frame)
	{
		struct interpolation_rotation previous_rotation, latest_rotation;
		short substep_index = interpolation_substep_index;

		rotation_from_matrix(frames[0], &previous_rotation);
		rotation_from_matrix(frames[1], &latest_rotation);
		/* (so written that a position not a number is none) */
		substeps->frame_drawn_valid = previous_rotation.is_rotation && latest_rotation.is_rotation &&
			distance_squared(&frames[0]->position, &frames[1]->position) <= OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE;
		if (substeps->frame_drawn_valid)
		{
			real_point3d line;

			/* (the first sub-step's from the tick before's end: its snapshot) */
			frame_blend(
				substep_index > 0 ? &substeps->frame_positions[buffer][substep_index - 1] : &frames[0]->position,
				substep_index > 0 ? &substeps->frame_rotations[buffer][substep_index - 1] : &previous_rotation.quaternion,
				&substeps->frame_positions[buffer][substep_index],
				&substeps->frame_rotations[buffer][substep_index],
				interpolation_substep_fraction,
				&substeps->frame_drawn);
			render_interpolation_lead_frame(vehicle_index, substeps);
			point_lerp(&frames[0]->position, &frames[1]->position, interpolation_fraction, &line);
			/* (so written that a position not a number is none) */
			substeps->frame_drawn_valid = distance_squared(&substeps->frame_drawn.position, &line) <=
				SUBSTEP_OFFSET_MAXIMUM * SUBSTEP_OFFSET_MAXIMUM;
		}
		substeps->frame_drawn_frame = interpolation_frame;
	}
	*drawn = &substeps->frame_drawn;
	return substeps->frame_drawn_valid;
}

/* ---------- ghosts */

/* (objects.c's object_delete, in the tick) the object deleted: a projectile
(a rocket, a needle, a grenade) that hit something or ran out, whose
effects (an explosion, an impact) are made where it was. Those are drawn
from the start of the frames after the tick, but the projectile was drawn
only as far as the tick before had it: it went a tick early, short of what
it hit, as what it made there appeared. It is drawn through the frames
after this tick as it was, from its last snapshot to where it was deleted.
One drawn from its newest sub-steps (render_interpolation_projectile_lead)
was drawn as far as the sub-step before: it is drawn through the frames of
the sub-step it was deleted in, from where that sub-step began to where it
was deleted, as the newest two sub-steps would have drawn it, and is gone
as the next sub-step's frames begin: never further than where it struck.
Only a projectile drawn with a model, with nothing on it (its lights and
its contrail go with it), and drawn before. */
void render_interpolation_object_deleted(long object_index)
{
	struct object_datum *object;
	struct object_definition *definition;
	struct interpolated_object *record;
	struct interpolation_ghost *ghost = NULL;
	real_matrix4x3 const *latest;
	real_point3d from;
	long absolute_index;
	long step = NONE;
	short count = sim_substep_count();
	short node_count;
	short index;

	if (!halo_interpolation_enabled() || !interpolated_objects || object_index == NONE)
		return;
	object = object_try_and_get(object_index);
	if (!object || object->object.type != _object_type_projectile ||
		object->object.parent_object_index != NONE || object->object.first_child_object_index != NONE ||
		TEST_FLAG(object->object.flags, _object_invisible_bit))
	{
		return;
	}
	definition = object_definition_get(object->definition_index);
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (definition->object.model.index == NONE || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return;
	record = &interpolated_objects[absolute_index];
	node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	/* (drawn from its newest sub-steps: where the sub-step before the one
	running, or the tick's work, left it; its pose as it is) */
	if (count > 1 && interpolated_substeps && !cinematic_in_progress())
	{
		if (interpolated_substeps[absolute_index].object_index != object_index ||
			interpolation_substep_kept == NONE ||
			!render_interpolation_kept_position(&interpolated_substeps[absolute_index], interpolation_substep_kept,
				count, &from) ||
			node_count <= 0 || node_count > MAXIMUM_GHOST_NODES)
		{
			return;
		}
		step = interpolation_substep_kept + 1;
		latest = (real_matrix4x3 const *)object_header_block_get(object_index, &object->object.node_matrices);
	}
	/* (its snapshot at the end of the tick before) */
	else if (record->object_index != object_index || record->tick != interpolation_tick ||
		record->node_count > MAXIMUM_GHOST_NODES)
	{
		return;
	}
	else
	{
		from = record->position;
		node_count = record->node_count;
		latest = record->nodes + record->latest * record->node_capacity;
	}
	for (index = 0; index < MAXIMUM_GHOSTS && !ghost; index++)
	{
		if (!ghosts[index].in_use)
			ghost = &ghosts[index];
	}
	if (!ghost)
		return;
	ghost->offset.i = object->object.position.x - from.x;
	ghost->offset.j = object->object.position.y - from.y;
	ghost->offset.k = object->object.position.z - from.z;
	/* (so written that an offset not a number is none) */
	if (!(ghost->offset.i * ghost->offset.i + ghost->offset.j * ghost->offset.j + ghost->offset.k * ghost->offset.k <=
		OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE))
	{
		return;
	}
	/* (as last drawn; its point lights were that frame's, not one to come's) */
	if (!render_objects_port_last_lighting(object_index, &ghost->lighting))
		return;
	ghost->lighting.point_light_count = 0;
	memcpy(ghost->nodes, latest, node_count * sizeof(real_matrix4x3));
	if (step != NONE)
	{
		short node_index;

		/* (its pose moved to where the sub-step began: never on past where it
		was deleted, wherever the pose was last made) */
		for (node_index = 0; node_index < node_count; node_index++)
		{
			ghost->nodes[node_index].position.x += from.x - latest[0].position.x;
			ghost->nodes[node_index].position.y += from.y - latest[0].position.y;
			ghost->nodes[node_index].position.z += from.z - latest[0].position.z;
		}
		memcpy(ghost->function_values, object->object.outgoing_function_values, sizeof(ghost->function_values));
		memcpy(ghost->change_colors, object->object.outgoing_change_colors, sizeof(ghost->change_colors));
	}
	else
	{
		memcpy(ghost->function_values, record->function_values[record->latest], sizeof(ghost->function_values));
		memcpy(ghost->change_colors, record->change_colors[record->latest], sizeof(ghost->change_colors));
	}
	memcpy(ghost->region_permutations, object->object.region_permutations, sizeof(ghost->region_permutations));
	ghost->in_use = TRUE;
	ghost->object_index = object_index;
	ghost->tick = NONE;
	ghost->step = step;
	ghost->substep_count = count;
	ghost->model_index = definition->object.model.index;
	ghost->node_count = node_count;
	ghost->forced_shader_permutation_index = object->object.forced_shader_permutation_index;
	ghost->position = from;
	ghost->center_offset.i = object->object.bounding_sphere_center.x - object->object.position.x;
	ghost->center_offset.j = object->object.bounding_sphere_center.y - object->object.position.y;
	ghost->center_offset.k = object->object.bounding_sphere_center.z - object->object.position.z;
	ghost->radius = object->object.bounding_sphere_radius;
}

/* (render_interpolation_tick) those deleted in the tick are drawn after it;
those drawn after the tick before are done */
static void render_interpolation_ghosts_tick(void)
{
	short index;

	for (index = 0; index < MAXIMUM_GHOSTS; index++)
	{
		struct interpolation_ghost *ghost = &ghosts[index];

		/* (one of a sub-step's, done with by the frames after it: those of a
		tick before the tick drawn's, if no frame was) */
		if (ghost->in_use && ghost->step != NONE)
			ghost->in_use = ghost->step / ghost->substep_count >= interpolation_tick;
		else if (ghost->in_use && ghost->tick == NONE && halo_interpolation_enabled())
			ghost->tick = interpolation_tick;
		else
			ghost->in_use = FALSE;
	}
}

/* whether the ghost is drawn in this frame, and how far (0 to 1) from where
it is drawn from to where it was deleted: the frame's part of the tick
after the one it was deleted in, or of the sub-step it was deleted in (at
its start while the frames have not reached it) */
static boolean render_interpolation_ghost_part(struct interpolation_ghost const *ghost, real *part)
{
	long to_step;

	if (ghost->step == NONE)
	{
		*part = interpolation_fraction;
		return ghost->tick == interpolation_tick;
	}
	to_step = render_interpolation_lead_to_step();
	*part = to_step == ghost->step ? interpolation_substep_fraction : 0.0f;
	return ghost->substep_count == interpolation_substep_count && to_step <= ghost->step;
}

/* (render_objects.c, as the objects are drawn) this tick's ghosts, as far
from their last snapshot to where they were deleted as the frame is into
the tick (or the sub-step's) */
void render_interpolation_ghosts_draw(void)
{
	short index;

	if (!interpolation_rendering)
		return;
	for (index = 0; index < MAXIMUM_GHOSTS; index++)
	{
		struct interpolation_ghost *ghost = &ghosts[index];
		struct render_model_effect model_effect;
		real_point3d center;
		real part;
		short node_index;

		if (!ghost->in_use || !render_interpolation_ghost_part(ghost, &part))
			continue;
		for (node_index = 0; node_index < ghost->node_count; node_index++)
		{
			ghost->drawn[node_index] = ghost->nodes[node_index];
			ghost->drawn[node_index].position.x += ghost->offset.i * part;
			ghost->drawn[node_index].position.y += ghost->offset.j * part;
			ghost->drawn[node_index].position.z += ghost->offset.k * part;
		}
		center.x = ghost->position.x + ghost->offset.i * part + ghost->center_offset.i;
		center.y = ghost->position.y + ghost->offset.j * part + ghost->center_offset.j;
		center.z = ghost->position.z + ghost->offset.k * part + ghost->center_offset.k;
		/* (no effect: as render_object's) */
		memset(&model_effect, 0, sizeof(model_effect));
		render_model(
			ghost->model_index,
			render_frustum_sphere_diameter_in_pixels(&render.frustum, &center, ghost->radius),
			ghost->drawn,
			(char const *)ghost->region_permutations,
			ghost->change_colors,
			ghost->function_values,
			&ghost->lighting,
			&center,
			ghost->radius,
			&model_effect,
			ghost->object_index,
			ghost->forced_shader_permutation_index,
			0);
	}
}

/* ---------- frames */

static void render_interpolation_newest_vehicles(boolean rendering);
static boolean render_interpolation_newest_frame(
	long object_index,
	real_matrix4x3 const **frames,
	real_matrix4x3 const **drawn,
	real_vector3d *correction);

void render_interpolation_frame_begin(void)
{
	boolean rendering = halo_interpolation_enabled();
	short index;

	/* (not drawn between the ticks until the local players' vehicles are
	taken as their newest step left them, below) */
	interpolation_rendering = FALSE;
	interpolation_frame++;
	interpolation_fraction = game_time_get_tick_fraction();
	interpolation_step_age = game_time_get_step_age();
	/* (the sub-step drawn: at 30 the tick's one, as far as the fraction) */
	interpolation_substep_count = sim_substep_count();
	sim_substep_blend(interpolation_fraction, &interpolation_substep_index, &interpolation_substep_fraction);
	/* the ghost of one deleted since the last tick (between the ticks: by
	the netcode, a script) is none: it was drawn as far as that tick; one of
	a sub-step's is done once the frames are past its sub-step */
	for (index = 0; index < MAXIMUM_GHOSTS; index++)
	{
		struct interpolation_ghost *ghost = &ghosts[index];
		real part;

		if (ghost->in_use && (ghost->step != NONE ? !render_interpolation_ghost_part(ghost, &part) :
			ghost->tick == NONE))
		{
			ghost->in_use = FALSE;
		}
	}
	render_interpolation_newest_vehicles(rendering);
	interpolation_rendering = rendering;
}

void render_interpolation_frame_end(void)
{
	interpolation_rendering = FALSE;
}

real render_interpolation_fraction(void)
{
	return interpolation_rendering ? interpolation_fraction : 1.0f;
}

/* An object put somewhere (render_interpolation_object_from) in a tick of
several sub-steps that has not ended (the VR mode's grenade let go of in a
sub-step, or in the tick's own work, which runs with its first): the frames
still draw the tick before, whose snapshot has it in the hand of the
player's body, which is not drawn in first person (and no snapshot of one
made since), while the hand no longer draws it. It is drawn where it was put
(its nodes as they are, moved there) until the frames reach the tick, which
draws it from there. At 30 no frame comes between a tick's work and its end:
none is drawn so */
static real_matrix4x3 *render_interpolation_pending_from(long object_index)
{
	static real_matrix4x3 nodes[MAXIMUM_INTERPOLATED_NODES];
	struct object_datum *object;
	real_matrix4x3 const *current;
	real_vector3d offset;
	short node_count;
	short node_index;

	if (object_index != interpolation_from.object_index || !object_try_and_get(object_index))
		return NULL;
	object = object_get(object_index);
	node_count = (short)(object->object.node_matrices.size / (short)sizeof(real_matrix4x3));
	if (node_count <= 0 || node_count > MAXIMUM_INTERPOLATED_NODES)
		return NULL;
	current = (real_matrix4x3 const *)object_header_block_get(object_index, &object->object.node_matrices);
	offset.i = interpolation_from.position.x - current[0].position.x;
	offset.j = interpolation_from.position.y - current[0].position.y;
	offset.k = interpolation_from.position.z - current[0].position.z;
	for (node_index = 0; node_index < node_count; node_index++)
	{
		nodes[node_index] = current[node_index];
		nodes[node_index].position.x += offset.i;
		nodes[node_index].position.y += offset.j;
		nodes[node_index].position.z += offset.k;
	}
	return nodes;
}

/* The object (a vehicle of the micro lane, or what it carries) drawn in the
vehicle's frame: each node of each snapshot seen from the vehicle's root
node of that snapshot (frames), blended between the ticks there (or the
latest taken, snapped: another pose), and placed by the vehicle's frame as
drawn (render_interpolation_substep_frame). What rides with the vehicle
turns with it, as the sub-steps posed it, rather than sweeping the straight
line between two ticks' places; at the ticks' ends it is where the
snapshots have it */
static void render_interpolation_blend_in_frame(
	struct interpolated_object *record,
	boolean snap,
	real_matrix4x3 const **frames,
	real_matrix4x3 const *frame)
{
	real_matrix4x3 const *previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
	real_matrix4x3 const *latest = record->nodes + record->latest * record->node_capacity;
	real_matrix4x3 *blended = record->nodes + 2 * record->node_capacity;
	short node_index;

	for (node_index = 0; node_index < record->node_count; node_index++)
	{
		real_matrix4x3 latest_local, local;

		matrix_in_frame(frames[1], &latest[node_index], &latest_local);
		if (snap)
		{
			local = latest_local;
		}
		else
		{
			real_matrix4x3 previous_local;
			struct interpolation_rotation previous_rotation, latest_rotation;

			matrix_in_frame(frames[0], &previous[node_index], &previous_local);
			rotation_from_matrix(&previous_local, &previous_rotation);
			rotation_from_matrix(&latest_local, &latest_rotation);
			matrix_blend_rotations(&previous_local, &latest_local, &previous_rotation, &latest_rotation,
				interpolation_fraction, &local);
		}
		matrix_from_frame(frame, &local, &blended[node_index]);
	}
}

real_matrix4x3 *render_interpolation_object_node_matrices(long object_index)
{
	struct interpolated_object *record;
	long absolute_index;
	real_matrix4x3 *pending;

	if (!interpolation_rendering || !interpolated_objects || object_index == NONE)
		return NULL;
	if ((pending = render_interpolation_projectile_nodes(object_index)) != NULL)
		return pending;
	if ((pending = render_interpolation_pending_from(object_index)) != NULL)
		return pending;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return NULL;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != object_index || record->tick != interpolation_tick || !record->has_previous)
		return NULL;
	if (record->blended_frame != interpolation_frame)
	{
		real_matrix4x3 const *previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
		real_matrix4x3 const *latest = record->nodes + record->latest * record->node_capacity;
		real_matrix4x3 *blended = record->nodes + 2 * record->node_capacity;
		short node_index;
		/* (so written that a position not a number snaps) */
		boolean jumped = !(distance_squared(&previous[0].position, &latest[0].position) <=
			OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE);
		boolean snap = jumped;
		boolean newest;
		real_vector3d offset;
		real_vector3d newest_correction;
		real_matrix4x3 const *frames[2];
		real_matrix4x3 const *frame;

		/* A node moved further in the root's frame than a tick allows: the
		two snapshots are different poses, not one moving (so written that
		a position not a number snaps). A node that moved the tick before
		may move half as far again besides: a part an animation carries
		away from the root faster each tick (a30's lifepod, whose body drops
		from its root in the opening, its camera with it) was taken for a
		new pose each tick and drawn stepping from tick to tick, a jolt each
		tick at high frame rates (#805), where a new pose moves a node at
		once from still or slower */
		real *moves = record->node_moves + (interpolation_tick & 1) * record->node_capacity;
		real const *moves_before = record->node_moves + ((interpolation_tick - 1) & 1) * record->node_capacity;
		boolean steady = record->node_moves_tick[(interpolation_tick - 1) & 1] == interpolation_tick - 1;

		for (node_index = 1; node_index < record->node_count; node_index++)
		{
			real_point3d previous_local, latest_local;
			real moved;

			matrix4x3_inverse_transform_point(&previous[0], &previous[node_index].position, &previous_local);
			matrix4x3_inverse_transform_point(&latest[0], &latest[node_index].position, &latest_local);
			moved = (real)sqrt(distance_squared(&previous_local, &latest_local));
			if (!(moved <= NODE_SNAP_DISTANCE + (steady ? NODE_STEADY_GROWTH * moves_before[node_index] : 0.0f)))
				snap = TRUE;
			/* (no further than an object may move in a tick) */
			moves[node_index] = moved < OBJECT_SNAP_DISTANCE ? moved : OBJECT_SNAP_DISTANCE;
		}
		record->node_moves_tick[interpolation_tick & 1] = interpolation_tick;
		newest = !jumped && render_interpolation_newest_frame(object_index, frames, &frame, &newest_correction);
		/* a local player's vehicle, or what it carries, with their view
		from its seat drawn from its newest step: posed in its frame as
		between the ticks, placed where that step left it, carried on */
		if (newest)
		{
			render_interpolation_blend_in_frame(record, snap, frames, frame);
		}
		/* a vehicle's of the micro lane, or what it carries: posed in its
		frame as between the ticks, placed where its sub-steps' frame has it */
		else if (!jumped && render_interpolation_substep_frame(object_index, frames, &frame))
		{
			render_interpolation_blend_in_frame(record, snap, frames, frame);
		}
		else
		{
			if (!snap)
			{
				struct interpolation_rotation *previous_rotations =
					record->rotations + (record->latest ^ 1) * record->node_capacity;
				struct interpolation_rotation *latest_rotations =
					record->rotations + record->latest * record->node_capacity;
				short snapshot;

				/* (each snapshot's rotations found once, not every frame: its
				nodes' positions may move with a correction, their rotations
				never) */
				for (snapshot = 0; snapshot < 2; snapshot++)
				{
					if (!record->rotations_valid[snapshot])
					{
						real_matrix4x3 const *nodes = record->nodes + snapshot * record->node_capacity;
						struct interpolation_rotation *rotations = record->rotations + snapshot * record->node_capacity;

						for (node_index = 0; node_index < record->node_count; node_index++)
							rotation_from_matrix(&nodes[node_index], &rotations[node_index]);
						record->rotations_valid[snapshot] = TRUE;
					}
				}
				for (node_index = 0; node_index < record->node_count; node_index++)
				{
					matrix_blend_rotations(&previous[node_index], &latest[node_index], &previous_rotations[node_index],
						&latest_rotations[node_index], interpolation_fraction, &blended[node_index]);
				}
			}
			if (snap)
				memcpy(blended, latest, record->node_count * sizeof(real_matrix4x3));
			/* the micro lane's: moved onto the sub-steps' path */
			if (!jumped && render_interpolation_substep_offset(object_index, snap, &offset))
			{
				for (node_index = 0; node_index < record->node_count; node_index++)
				{
					blended[node_index].position.x += offset.i;
					blended[node_index].position.y += offset.j;
					blended[node_index].position.z += offset.k;
				}
			}
		}
		/* (what the vehicle carries glides with it, by the vehicle's glide) */
		if (newest || correction_significant(&record->correction) ||
			correction_significant(&record->correction_pending))
		{
			real_vector3d drawn;

			if (newest)
				drawn = newest_correction;
			else
				correction_drawn(&record->correction, &record->correction_pending, &drawn);
			for (node_index = 0; node_index < record->node_count; node_index++)
			{
				blended[node_index].position.x += drawn.i;
				blended[node_index].position.y += drawn.j;
				blended[node_index].position.z += drawn.k;
			}
		}
		record->blended_frame = interpolation_frame;
	}
	return record->nodes + 2 * record->node_capacity;
}

/* ---------- what objects are drawn with */

/* An object's function values (a weapon's charge or heat, a vehicle's
speed, a shield's flare: what its shaders' animations and glows read), its
change colors and a unit's active camouflage, which the tick computes: the
blend of the last two ticks', as its pose is, rather than a step a tick */
static struct interpolated_object *render_interpolation_object_values(long object_index)
{
	struct interpolated_object *record;
	long absolute_index;

	if (!interpolation_rendering || !interpolated_objects || object_index == NONE)
		return NULL;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return NULL;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != object_index || record->tick != interpolation_tick || !record->has_previous)
		return NULL;
	if (record->values_blended_frame != interpolation_frame)
	{
		short previous = record->latest ^ 1, latest = record->latest;
		short index;

		for (index = 0; index < NUMBER_OF_OUTGOING_OBJECT_FUNCTIONS; index++)
		{
			record->function_values[2][index] = lerp(record->function_values[previous][index],
				record->function_values[latest][index], interpolation_fraction);
		}
		for (index = 0; index < NUMBER_OF_OBJECT_CHANGE_COLORS; index++)
		{
			real_rgb_color const *a = &record->change_colors[previous][index];
			real_rgb_color const *b = &record->change_colors[latest][index];

			record->change_colors[2][index].red = lerp(a->red, b->red, interpolation_fraction);
			record->change_colors[2][index].green = lerp(a->green, b->green, interpolation_fraction);
			record->change_colors[2][index].blue = lerp(a->blue, b->blue, interpolation_fraction);
		}
		for (index = 0; index < 2; index++)
		{
			record->camouflage[2][index] = lerp(record->camouflage[previous][index],
				record->camouflage[latest][index], interpolation_fraction);
		}
		record->values_blended_frame = interpolation_frame;
	}
	return record;
}

real const *render_interpolation_object_function_values(long object_index)
{
	struct interpolated_object *record = render_interpolation_object_values(object_index);

	return record ? record->function_values[2] : object_get(object_index)->object.outgoing_function_values;
}

real_rgb_color const *render_interpolation_object_change_colors(long object_index)
{
	struct interpolated_object *record = render_interpolation_object_values(object_index);

	return record ? record->change_colors[2] : object_get(object_index)->object.outgoing_change_colors;
}

/* ... and how much it is camouflaged (super_amount: the camouflage's
super amount) */
real render_interpolation_unit_camouflage(long unit_index, real *super_amount)
{
	struct interpolated_object *record = render_interpolation_object_values(unit_index);
	struct unit_datum *unit;

	if (record)
	{
		*super_amount = record->camouflage[2][1];
		return record->camouflage[2][0];
	}
	unit = unit_get(unit_index);
	*super_amount = unit->unit.active_camouflage_super_amount;
	return unit->unit.active_camouflage;
}

/* a point that moves with the object's root node (its bounding sphere's
centre: a nav point over it), from where the object's state has it now (the
last tick, and at 60 and 120 the open tick's sub-steps run since: a point
taken earlier is held to the object as it was then, game_engine.c's goals)
to where the object is drawn this frame; left as it is when the object is
drawn as the tick left it */
void render_interpolation_object_point(long object_index, real_point3d *point)
{
	real_matrix4x3 const *drawn = render_interpolation_object_node_matrices(object_index);
	real_matrix4x3 const *nodes;
	real_point3d local;

	if (!drawn)
		return;
	/* (as of the last tick: object_get_node_matrices gives the drawn pose
	while a frame is drawn) */
	nodes = (real_matrix4x3 const *)object_header_block_get(object_index, &object_get(object_index)->object.node_matrices);
	matrix4x3_inverse_transform_point(&nodes[0], point, &local);
	matrix4x3_transform_point(&drawn[0], &local, point);
}

/* A point the game works out from an object's state reads some of it as the
tick left it (its position, its bounding sphere) and some as a frame draws
it (its nodes and markers, object_get_node_matrices): a unit's camera on foot
is its origin, the tick's, and in a seat its seat's marker, drawn. Mixed, it
moves a tick's step at a time, or is drawn a tick's motion off the object.
These take the whole of it as the tick left it and move that with the object
as drawn (render_interpolation_object_point), the same in every case. */

/* the object's origin (object_get_origin) where it is drawn */
void render_interpolation_object_origin(long object_index, real_point3d *origin)
{
	boolean rendering = interpolation_rendering;

	interpolation_rendering = FALSE;
	object_get_origin(object_index, origin);
	interpolation_rendering = rendering;
	render_interpolation_object_point(object_index, origin);
}

static boolean render_interpolation_view_carried(long unit_index, real_vector3d *carried);

/* the unit's camera (unit_get_camera_position) where it is drawn: what a
waypoint's distance, and the motion tracker's centre, are measured from.
The window's own player's on foot, whose view is drawn from the newest step
(display.direct_camera), is where that view is drawn: as the newest step
left it, carried on as the view is (render_interpolation_view_carried), not
a tick behind with the body, so that a waypoint's distance is the view's
from what it marks in every frame */
void render_interpolation_unit_camera_position(long unit_index, real_point3d *position)
{
	boolean rendering = interpolation_rendering;
	real_vector3d carried;

	interpolation_rendering = FALSE;
	unit_get_camera_position(unit_index, position);
	interpolation_rendering = rendering;
	if (render_interpolation_view_carried(unit_index, &carried))
	{
		position->x += carried.i;
		position->y += carried.j;
		position->z += carried.k;
		return;
	}
	render_interpolation_object_point(unit_index, position);
}

/* ---------- shapes */

/* A widget's points (an antenna's chain, a cloth flag's grid) are a
simulation of the Xbox's own step, an update a tick: their springs and
cloth are per-update quantities, and stepped a frame at a time they act
against the frames' own movement of them (an antenna swung wide, a flag's
cloth thrashing). The tick steps them (game.c), and leaves their shape here,
seen from the frame of the marker they hang from as the tick had it; a frame
draws them the part of the way between the last two ticks' shapes that it
is, hung from that marker as the frame draws it, as an object's nodes are
drawn between their snapshots. A tick's work runs before its sub-steps
(at game.sim_rate 60 and 120), so the shape it leaves is the tick's that
ends after them: three are kept, the two drawn and the open tick's. */
static struct interpolated_shape *render_interpolation_shape_find(unsigned long owner, long index, boolean make)
{
	struct interpolated_shape *oldest = NULL;
	short slot;

	for (slot = 0; slot < MAXIMUM_INTERPOLATED_SHAPES; slot++)
	{
		struct interpolated_shape *shape = &interpolated_shapes[slot];

		if (shape->owner == owner && shape->index == index)
			return shape;
		/* (a slot of none, or the one left longest) */
		if (!oldest || shape->owner == 0 ||
			(oldest->owner != 0 && shape->newest_tick < oldest->newest_tick))
		{
			oldest = shape;
		}
	}
	if (!make)
		return NULL;
	memset(oldest, 0, sizeof(*oldest));
	oldest->owner = owner;
	oldest->index = index;
	for (slot = 0; slot < SHAPE_SNAPSHOTS; slot++)
		oldest->ticks[slot] = NONE;
	oldest->newest_tick = NONE;
	return oldest;
}

/* the frame the shape is seen from: the marker's place and its axes made a
rotation (forward kept, up square to it; a mirrored marker's left aside),
the same at the tick and in the frame, so that what is seen from one is
placed by the other */
static void shape_frame(real_matrix4x3 const *anchor, real_matrix4x3 *frame)
{
	real length;
	real dot;

	frame->position = anchor->position;
	frame->scale = 1.0f;
	frame->forward = anchor->forward;
	length = vector_length(&frame->forward);
	if (!(length > 1e-6f))
	{
		/* (no rotation to speak of: the world's axes, moved) */
		frame->forward = *global_forward3d;
		frame->left = *global_left3d;
		frame->up = *global_up3d;
		return;
	}
	frame->forward.i /= length;
	frame->forward.j /= length;
	frame->forward.k /= length;
	dot = anchor->up.i * frame->forward.i + anchor->up.j * frame->forward.j + anchor->up.k * frame->forward.k;
	frame->up.i = anchor->up.i - frame->forward.i * dot;
	frame->up.j = anchor->up.j - frame->forward.j * dot;
	frame->up.k = anchor->up.k - frame->forward.k * dot;
	length = vector_length(&frame->up);
	if (!(length > 1e-6f))
	{
		frame->forward = *global_forward3d;
		frame->left = *global_left3d;
		frame->up = *global_up3d;
		return;
	}
	frame->up.i /= length;
	frame->up.j /= length;
	frame->up.k /= length;
	/* left = up x forward */
	frame->left.i = frame->up.j * frame->forward.k - frame->up.k * frame->forward.j;
	frame->left.j = frame->up.k * frame->forward.i - frame->up.i * frame->forward.k;
	frame->left.k = frame->up.i * frame->forward.j - frame->up.j * frame->forward.i;
}

/* the shape this tick leaves a widget in (owner: its kind, index: its
datum's), its points in the world and the marker they hang from as the tick
has them */
void render_interpolation_shape_tick(
	unsigned long owner,
	long index,
	real_matrix4x3 const *anchor,
	short count,
	real_point3d const *points)
{
	struct interpolated_shape *shape;
	/* (the tick this one ends with, as interpolation_tick counts them once
	it has) */
	long tick = interpolation_tick + 1;
	real_matrix4x3 frame;
	short slot;
	short point_index;

	if (count <= 0 || count > MAXIMUM_SHAPE_POINTS)
	{
		render_interpolation_shape_forget(owner, index);
		return;
	}
	shape = render_interpolation_shape_find(owner, index, TRUE);
	/* (the slot of the oldest, or of this tick again) */
	slot = 0;
	for (point_index = 0; point_index < SHAPE_SNAPSHOTS; point_index++)
	{
		if (shape->ticks[point_index] == tick)
		{
			slot = point_index;
			break;
		}
		if (shape->ticks[point_index] == NONE ||
			(shape->ticks[slot] != NONE && shape->ticks[point_index] < shape->ticks[slot]))
		{
			slot = point_index;
		}
	}
	shape_frame(anchor, &frame);
	for (point_index = 0; point_index < count; point_index++)
		point_in_frame(&frame, &points[point_index], &shape->points[slot][point_index]);
	shape->counts[slot] = count;
	shape->ticks[slot] = tick;
	shape->newest_tick = tick;
}

/* the widget's shape is none to blend from (made again, or put somewhere
other than its last tick left it): its points are drawn as they are until
the ticks leave two more */
void render_interpolation_shape_forget(unsigned long owner, long index)
{
	struct interpolated_shape *shape = render_interpolation_shape_find(owner, index, FALSE);

	if (shape)
		shape->owner = 0;
}

static short shape_snapshot(struct interpolated_shape const *shape, long tick, short count)
{
	short slot;

	for (slot = 0; slot < SHAPE_SNAPSHOTS; slot++)
	{
		if (shape->ticks[slot] == tick && shape->counts[slot] == count)
			return slot;
	}
	return NONE;
}

/* where the widget's count points are drawn this frame, hung from the
marker they hang from as the frame draws it (anchor): between the shapes
of the last two ticks, or the last's alone after a gap. FALSE when the
frame draws none (not between the ticks), or the ticks left none: its
points are drawn as they are */
boolean render_interpolation_shape(
	unsigned long owner,
	long index,
	real_matrix4x3 const *anchor,
	short count,
	real_point3d *drawn)
{
	struct interpolated_shape const *shape;
	short latest;
	short previous;
	real_matrix4x3 frame;
	short point_index;

	if (!interpolation_rendering || count <= 0 || count > MAXIMUM_SHAPE_POINTS ||
		(shape = render_interpolation_shape_find(owner, index, FALSE)) == NULL ||
		(latest = shape_snapshot(shape, interpolation_tick, count)) == NONE)
	{
		return FALSE;
	}
	previous = shape_snapshot(shape, interpolation_tick - 1, count);
	shape_frame(anchor, &frame);
	for (point_index = 0; point_index < count; point_index++)
	{
		real_point3d local = shape->points[latest][point_index];

		if (previous != NONE)
			point_lerp(&shape->points[previous][point_index], &local, interpolation_fraction, &local);
		point_from_frame(&frame, &local, &drawn[point_index]);
	}
	return TRUE;
}

/* ---------- corrections */

/* the object (and what it carries) moved by the netcode from where it was,
offset from where it is now: drawn from there, gliding */
void render_interpolation_correct_object(long object_index, real_vector3d const *offset)
{
	struct object_datum *object;
	long child_index;
	long absolute_index;

	/* (so written that an offset not a number is none) */
	if (!interpolated_objects || object_index == NONE ||
		!(offset->i * offset->i + offset->j * offset->j + offset->k * offset->k <= OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE))
	{
		return;
	}
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (absolute_index < MAXIMUM_INTERPOLATED_OBJECTS &&
		interpolated_objects[absolute_index].object_index == object_index)
	{
		struct interpolated_object *record = &interpolated_objects[absolute_index];
		short snapshot;
		short node_index;

		/* the snapshots where it would have been, the difference drawn */
		for (snapshot = 0; snapshot < 2; snapshot++)
		{
			real_matrix4x3 *nodes = record->nodes + snapshot * record->node_capacity;

			for (node_index = 0; node_index < record->node_count; node_index++)
			{
				nodes[node_index].position.x -= offset->i;
				nodes[node_index].position.y -= offset->j;
				nodes[node_index].position.z -= offset->k;
			}
		}
		record->position.x -= offset->i;
		record->position.y -= offset->j;
		record->position.z -= offset->k;
		record->previous_position.x -= offset->i;
		record->previous_position.y -= offset->j;
		record->previous_position.z -= offset->k;
		correction_add(&record->correction, &record->correction_pending, offset);
		record->blended_frame = NONE;
		/* (and its sub-steps' poses: as its snapshots, where it would have
		been) */
		if (interpolated_substeps && interpolated_substeps[absolute_index].object_index == object_index)
		{
			struct interpolated_substeps *substeps = &interpolated_substeps[absolute_index];
			short buffer;
			short substep_index;

			for (buffer = 0; buffer < 2; buffer++)
			{
				for (substep_index = 0; substep_index < MAXIMUM_SIM_SUBSTEPS; substep_index++)
				{
					substeps->positions[buffer][substep_index].x -= offset->i;
					substeps->positions[buffer][substep_index].y -= offset->j;
					substeps->positions[buffer][substep_index].z -= offset->k;
					substeps->frame_positions[buffer][substep_index].x -= offset->i;
					substeps->frame_positions[buffer][substep_index].y -= offset->j;
					substeps->frame_positions[buffer][substep_index].z -= offset->k;
				}
			}
			substeps->drawn_frame = NONE;
			substeps->frame_drawn_frame = NONE;
		}
	}
	/* a local player's unit (by itself, or with what it rides): its
	first-person view, which the observer poses from it, glides with it */
	{
		short local_player_index;

		for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		{
			struct interpolated_camera *camera = &interpolated_cameras[local_player_index];

			if (!camera->valid || player_control_get_unit_index(local_player_index) != object_index)
				continue;
			camera->previous.position.x -= offset->i;
			camera->previous.position.y -= offset->j;
			camera->previous.position.z -= offset->k;
			camera->latest.position.x -= offset->i;
			camera->latest.position.y -= offset->j;
			camera->latest.position.z -= offset->k;
			correction_add(&camera->correction, &camera->correction_pending, offset);
		}
	}
	/* ... and the steps of a local player's vehicle drawn from its newest,
	where they would have been */
	{
		short local_player_index;

		for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
		{
			struct newest_history *history = &newest_histories[local_player_index];
			short index;

			if (history->vehicle_index != object_index)
				continue;
			for (index = 0; index < history->count; index++)
			{
				history->positions[index].x -= offset->i;
				history->positions[index].y -= offset->j;
				history->positions[index].z -= offset->k;
			}
			history->model_position.x -= offset->i;
			history->model_position.y -= offset->j;
			history->model_position.z -= offset->k;
		}
	}
	object = object_get(object_index);
	for (child_index = object->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		render_interpolation_correct_object(child_index, offset);
	}
}

/* ---------- camera */

static struct observer_result direct_cameras[MAXIMUM_LOCAL_PLAYERS];
/* each local player's view drawn last from the newest step
(render_interpolation_view_newest) */
static boolean newest_views[MAXIMUM_LOCAL_PLAYERS];
/* each local player's view drawn last from the newest step on foot, and how
far it is carried on from where that step left it (the observer's place:
render_interpolation_newest_camera), for render_interpolation_view_carried */
static boolean newest_carried_valid[MAXIMUM_LOCAL_PLAYERS];
static real_vector3d newest_carried[MAXIMUM_LOCAL_PLAYERS];
/* each local player's view between the ticks (which goes with the body
drawn) and the view drawn, this frame (render_interpolation_view_flashlight) */
static struct observer_result blended_views[MAXIMUM_LOCAL_PLAYERS];
static struct observer_result drawn_views[MAXIMUM_LOCAL_PLAYERS];
static boolean drawn_views_valid[MAXIMUM_LOCAL_PLAYERS];

/* where the last frame drew the local player's view from, if it drew one
(debug.network_test_elevator logs it) */
boolean render_interpolation_drawn_view_position(short local_player_index, real_point3d *position)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!drawn_views_valid[local_player_index])
	{
		return FALSE;
	}
	*position = drawn_views[local_player_index].position;
	return TRUE;
}

/* the views display.direct_camera takes (render_interpolation_direct) */
enum
{
	_direct_none,
	/* in the first person, on foot */
	_direct_on_foot,
	/* in a vehicle's seat, in the first person or following it */
	_direct_seat
};

static struct observer_result const *render_interpolation_blended_camera(
	short local_player_index,
	struct observer_result const *observer);

/* display.direct_camera: which view of the local player's is drawn as the
newest step left it, their own and its facing theirs (not a cinematic's, nor
the script's): in the first person on foot (_direct_on_foot), or (not in the
VR mode, whose seats stay as they were) in a seat of a vehicle that rides
nothing, in the first person or following it (_direct_seat); _direct_none
for any other. Android's too, as iOS's: the touch controls' aim is taken in
each frame's input, as the mouse's and the sticks' (player_control.c) */
static short render_interpolation_direct(
	short local_player_index)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;
	short perspective;
	long unit_index;
	struct object_datum *vehicle;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.direct_camera");
	}
	perspective = director_get_perspective(local_player_index);
	/* (off in a game whose host says so, a 2V2 Classic game's, whatever the
	setting: network_standard_direct_camera_allowed) */
	if (!enabled || !network_standard_direct_camera_allowed() ||
		(perspective != _director_perspective_first_person && perspective != _director_perspective_third_person) ||
		director_inhibited_facing(local_player_index) ||
		cinematic_in_progress())
	{
		return _direct_none;
	}
	unit_index = player_control_get_unit_index(local_player_index);
	if (unit_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit))
		return _direct_none;
	if (object_get(unit_index)->object.parent_object_index == NONE)
		return perspective == _director_perspective_first_person ? _direct_on_foot : _direct_none;
#ifdef HALO_VR
	if (halo_vr_running())
		return _direct_none;
#endif
	vehicle = (struct object_datum *)object_try_and_get_and_verify_type(
		object_get(unit_index)->object.parent_object_index, _object_mask_vehicle);
	return vehicle && vehicle->object.parent_object_index == NONE ? _direct_seat : _direct_none;
}

/* The aim's motion come since the frame's input took it, which the view
drawn takes as late as it can: the mouse's (halo_linux_mouse_look_late, the
first player's, as the input turns it: by the zoom's scale), the touch
controls' swipe and gyroscope (player_control_touch_look_late, as the input
turns them) and a controller's gyroscope (player_control_gyro_look_late),
read as the frame takes its view rather than at its start.
Only the view drawn takes it: the next frame's input turns the player by it
(none in the VR mode, whose head aims). FALSE for none. Once a frame */
static boolean render_interpolation_look_late(
	short local_player_index,
	real *yaw,
	real *pitch)
{
	float mouse_yaw, mouse_pitch;
	real touch_yaw, touch_pitch;
	boolean turned = FALSE;

	*yaw = 0.0f;
	*pitch = 0.0f;
#ifdef HALO_VR
	if (halo_vr_running())
		return FALSE;
#endif
	if (halo_linux_mouse_look_late(local_player_index, &mouse_yaw, &mouse_pitch))
	{
		struct player_control const *control = player_control_get(local_player_index);

		if (control->zoom_level != NONE)
		{
			real zoom_scale = mouse_zoom_look_scale(local_player_index,
				player_control_get_unit_index(local_player_index), control->zoom_level);

			mouse_yaw *= zoom_scale;
			mouse_pitch *= zoom_scale;
		}
		*yaw += mouse_yaw;
		*pitch += mouse_pitch;
		turned = TRUE;
	}
	if (player_control_touch_look_late(local_player_index, &touch_yaw, &touch_pitch))
	{
		*yaw += touch_yaw;
		*pitch += touch_pitch;
		turned = TRUE;
	}
	if (player_control_gyro_look_late(local_player_index, &touch_yaw, &touch_pitch))
	{
		*yaw += touch_yaw;
		*pitch += touch_pitch;
		turned = TRUE;
	}
	return turned;
}

/* A first-person view is posed from the player's facing, which the input
turns every frame (player_control.c), but the observer keeps it as of the
last tick and the blend above draws it a tick later still. On foot, the view
points where the player aims now (display.direct_camera): the facing the
frame's input gave the player, turned on by the aim's motion come since
(render_interpolation_look_late), the pitch within the player's limits. */
static struct observer_result const *render_interpolation_direct_camera(
	short local_player_index,
	struct observer_result const *observer)
{
	struct observer_result *direct;
	real_euler_angles2d angles;
	real yaw, pitch;

	if (!observer || render_interpolation_direct(local_player_index) != _direct_on_foot)
		return observer;
	direct = &direct_cameras[local_player_index];
	*direct = *observer;
	angles = *player_control_get_facing_angles(local_player_index);
	if (render_interpolation_look_late(local_player_index, &yaw, &pitch))
	{
		struct player_control const *control = player_control_get(local_player_index);

		angles.yaw += yaw;
		angles.pitch = PIN(angles.pitch + pitch, control->pitch_minimum, control->pitch_maximum);
	}
	player_aiming_vector_from_facing(local_player_get_player_index(local_player_index), &direct->forward, &angles);
	/* (a dead player's unit keeps its last turn: its ticks no longer reset it) */
	{
		long unit_index = player_control_get_unit_index(local_player_index);

		if (unit_index != NONE && !TEST_FLAG(object_get(unit_index)->object.damage_flags, _object_dead_bit))
		{
			/* (the facing holds this tick's whole turn by the vehicle the
			player stands on, which is drawn only part of the way into the
			tick; vehicle_riding.c) */
			real kept_dyaw = vehicle_riding_kept_dyaw(local_player_index);

			/* (no turn kept: the rotation by zero is skipped, for the frame's camera only) */
			if (kept_dyaw != 0.f)
				vehicle_riding_turn_vector(&direct->forward, -(1.0f - render_interpolation_fraction())*kept_dyaw);
		}
	}
	observer_up_from_forward(&direct->forward, &direct->up);
	return direct;
}

/* how far a lift carries the unit in a tick (world units a tick), if one
carries it (device_machines.c's machine_move_elevator_riders, and at 60 and
120 objects.c's lanes, a share each sub-step): its platform's movement
between the last two ticks' snapshots, which the carry of the tick to come
(at 30) or under way (at 60 and 120, a tick behind the lift) is, but at a
start or a stop. The unit's own velocity has none of it: the carry moves it
where it is */
static boolean render_interpolation_rider_carry(long unit_index, real_vector3d *carry)
{
	struct biped_datum *biped = (struct biped_datum *)object_try_and_get_and_verify_type(unit_index, _object_mask_biped);
	struct interpolated_object const *record;
	struct machine_datum *machine;
	real_matrix4x3 const *previous, *latest;
	long machine_index;
	long absolute_index;
	short node;

	if (!biped || !interpolated_objects)
		return FALSE;
	machine_index = biped->biped.elevator_object_index;
	if (machine_index == NONE || !machine_carries_rider(machine_index, unit_index))
		return FALSE;
	machine = machine_get(machine_index);
	node = machine_definition_get(machine->definition_index)->machine.elevator_node_index;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(machine_index);
	if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return FALSE;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != machine_index || record->tick != interpolation_tick || !record->has_previous ||
		node < 0 || node >= record->node_count)
	{
		return FALSE;
	}
	previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
	latest = record->nodes + record->latest * record->node_capacity;
	carry->i = latest[node].position.x - previous[node].position.x;
	carry->j = latest[node].position.y - previous[node].position.y;
	carry->k = latest[node].position.z - previous[node].position.z;
	return TRUE;
}

/* The player's own view on foot (display.direct_camera, while frames are
drawn between the ticks, and not in the VR mode, whose eyes stay as they
were) stands where the newest step left the unit, not where the blend of the
last two ticks has it a tick behind: the observer (taken after the frame's
steps, the open tick's sub-steps too) moved on by the unit's own velocity (and
the lift's carry, render_interpolation_rider_carry) for the time since that
step, and its eye by its crouch's change
(bipeds.c's biped_eye_rise_ahead), so that it moves every frame at 30 as at
60. The first-person weapon, posed from it, goes with it (render_interpolation_
first_person). What the netcode glides (correction) is kept. Everything else
is drawn as before. */
static void render_interpolation_newest_camera(
	short local_player_index,
	struct observer_result const *observer,
	struct observer_result *drawn)
{
	struct interpolated_camera *camera = &interpolated_cameras[local_player_index];
	long unit_index = player_control_get_unit_index(local_player_index);
	real_vector3d velocity = object_get(unit_index)->object.translational_velocity;
	real_vector3d carry;
	/* (the time since the newest step, in ticks: the clock's, not the
	fraction's part past its last whole sub-step, which is a sub-step short
	while a step's time has come and it has not run: OpenH1 #706) */
	real since = interpolation_step_age;

	/* (and by the lift it rides: carried by its velocity alone, the view
	held still between the steps on a moving lift, a step at a time) */
	if (render_interpolation_rider_carry(unit_index, &carry))
	{
		velocity.i += carry.i;
		velocity.j += carry.j;
		velocity.k += carry.k;
	}
	drawn->position = observer->position;
	if (velocity.i * velocity.i + velocity.j * velocity.j + velocity.k * velocity.k <=
		CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE)
	{
		drawn->position.x += velocity.i * since;
		drawn->position.y += velocity.j * since;
		drawn->position.z += velocity.k * since;
	}
	/* (and its eye on with its crouch, which goes down or up a step at a
	time as the unit's place does) */
	if (object_try_and_get_and_verify_type(unit_index, _object_mask_biped))
		drawn->position.z += biped_eye_rise_ahead(unit_index, since);
	if (camera->valid && camera->unit_index == unit_index &&
		(correction_significant(&camera->correction) || correction_significant(&camera->correction_pending)))
	{
		real_vector3d correction;

		correction_drawn(&camera->correction, &camera->correction_pending, &correction);
		drawn->position.x += correction.i;
		drawn->position.y += correction.j;
		drawn->position.z += correction.k;
	}
}

/* the vector turned about a unit axis by an angle (radians, the right
hand's way round) */
static void vector_turned(
	real_vector3d const *vector,
	real_vector3d const *axis,
	real angle,
	real_vector3d *result)
{
	real_vector3d v = *vector;
	real c = (real)cos(angle);
	real s = (real)sin(angle);
	real along = (axis->i * v.i + axis->j * v.j + axis->k * v.k) * (1.0f - c);

	result->i = v.i * c + (axis->j * v.k - axis->k * v.j) * s + axis->i * along;
	result->j = v.j * c + (axis->k * v.i - axis->i * v.k) * s + axis->j * along;
	result->k = v.k * c + (axis->i * v.j - axis->j * v.i) * s + axis->k * along;
}

/* The newest step that moved the vehicle, as the frames count them: its end,
in sub-steps of the tick (count) from the clock's start. One of the micro
lane moves a sub-step at a time (at 30, a tick); one of the macro lane a
whole tick's motion at once, in the tick's first sub-step (game.c's
game_tick_substep): at 60 and 120, past that sub-step, the next tick's end */
static long render_interpolation_newest_step(
	long vehicle_index)
{
	short count = interpolation_substep_count;
	short next = sim_substep_next();

	if (count > 1 && object_get_lane(vehicle_index) != _object_lane_micro)
		return (interpolation_tick + (next >= 1 ? 1 : 0)) * count;
	return interpolation_tick * count + next;
}

/* The seat the unit rides in: whether its first-person view is the
vehicle's gun's (first_person_camera.c: its "primary trigger" marker, which
aims once a tick), and whether the player's aim in it is the world's
(players.c's player_aiming_vector_from_facing turns another seat's from the
world's up to the vehicle's) */
static void render_interpolation_seat(
	long unit_index,
	boolean *gun_view,
	boolean *aim_in_world)
{
	struct unit_datum *unit = (struct unit_datum *)object_get(unit_index);
	long vehicle_index = unit->object.parent_object_index;
	struct unit_definition *definition = unit_definition_get(object_get(vehicle_index)->definition_index);
	struct unit_seat const *seat;
	struct object_marker marker;

	*gun_view = FALSE;
	*aim_in_world = FALSE;
	if (unit->unit.parent_seat_index < 0 || unit->unit.parent_seat_index >= definition->unit.seats.count)
		return;
	seat = TAG_BLOCK_GET_ELEMENT(&definition->unit.seats, unit->unit.parent_seat_index, struct unit_seat);
	*gun_view = TEST_FLAG(seat->flags, _unit_seat_first_person_camera_bit) &&
		object_get_marker_by_name(vehicle_index, "primary trigger", &marker, 1) > 0;
	*aim_in_world = TEST_FLAG(seat->flags, _unit_seat_third_person_camera_bit);
}

/* the turn from one rotation to another, as a vector: about its axis, as
long as its angle (radians), the shorter way round */
static void rotation_turn(
	struct interpolation_quaternion const *from,
	struct interpolation_quaternion const *to,
	real_vector3d *turn)
{
	struct interpolation_quaternion q;
	real length, angle;

	/* (to's, by the inverse of from's) */
	q.w = to->w * from->w + to->i * from->i + to->j * from->j + to->k * from->k;
	q.i = -to->w * from->i + to->i * from->w - to->j * from->k + to->k * from->j;
	q.j = -to->w * from->j + to->j * from->w - to->k * from->i + to->i * from->k;
	q.k = -to->w * from->k + to->k * from->w - to->i * from->j + to->j * from->i;
	if (q.w < 0.0f)
	{
		q.w = -q.w;
		q.i = -q.i;
		q.j = -q.j;
		q.k = -q.k;
	}
	length = (real)sqrt(q.i * q.i + q.j * q.j + q.k * q.k);
	angle = 2.0f * (real)atan2(length, q.w);
	*turn = *global_zero_vector3d;
	if (length > 1e-7f)
	{
		turn->i = q.i / length * angle;
		turn->j = q.j / length * angle;
		turn->k = q.k / length * angle;
	}
}

/* (render_interpolation_newest_vehicle) the vehicle's newest step kept, as
it ends (step) and where it left its root node: after the steps before, or
again from it (another vehicle, another rate, the clock gone back or long
stopped, a jump); the same step's again (a frame more in it) taken as it is
now */
static void render_interpolation_newest_history(
	struct newest_history *history,
	long vehicle_index,
	long step,
	real_matrix4x3 const *root)
{
	struct interpolation_quaternion rotation;
	short count = interpolation_substep_count;
	short index;

	quaternion_from_basis(root, &rotation);
	if (history->vehicle_index != vehicle_index || history->count <= 0 || history->substep_count != count ||
		step < history->steps[0] || step - history->steps[0] > NEWEST_HISTORY_GAP * count ||
		/* (so written that a position not a number is a jump) */
		!(distance_squared(&root->position, &history->positions[0]) <=
			CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE * NEWEST_HISTORY_GAP * NEWEST_HISTORY_GAP))
	{
		history->vehicle_index = vehicle_index;
		history->substep_count = count;
		history->count = 0;
	}
	if (history->count <= 0 || step > history->steps[0])
	{
		for (index = 2; index > 0; index--)
		{
			history->steps[index] = history->steps[index - 1];
			history->positions[index] = history->positions[index - 1];
			history->rotations[index] = history->rotations[index - 1];
		}
		history->count = (short)MIN(history->count + 1, 3);
	}
	history->steps[0] = step;
	history->positions[0] = root->position;
	history->rotations[0] = rotation;
}

/* a vector at most so long (so written that one not a number is none) */
static void vector_at_most(real_vector3d *vector, real maximum)
{
	real length_squared = vector->i * vector->i + vector->j * vector->j + vector->k * vector->k;

	if (!(length_squared <= maximum * maximum))
	{
		real scale = length_squared > maximum * maximum ? maximum / (real)sqrt(length_squared) : 0.0f;

		vector->i *= scale;
		vector->j *= scale;
		vector->k *= scale;
	}
}

/* The vehicle carried on from its newest step for the time since (ticks:
less than none for the macro lane's next tick's end), as its steps moved and
turned it: by where its last steps left its root node (the steps' own
motion, which the frames drew, and not the physics' velocity, which a tick
keeps apart from its sub-steps' by half a sub-step's acceleration and runs
half a step behind the motion), at the velocity and spin it had at the
newest step and on with the acceleration of its last three (so that a
vehicle speeding up, braking or turning harder does not step back at each
step, the motion carried on falling short), the acceleration no more than
a hard turn's: shift, the place's change, and turn, about an axis as long
as its angle. The physics' velocity and spin while the steps before are not
known (its first steps) */
static void render_interpolation_newest_motion(
	struct newest_history const *history,
	struct object_datum const *vehicle,
	struct newest_motion *motion)
{
	real_vector3d velocity = vehicle->object.translational_velocity;
	real_vector3d spin = vehicle->object.angular_velocity;
	real_vector3d acceleration = *global_zero_vector3d;
	real_vector3d spin_acceleration = *global_zero_vector3d;

	if (history->count >= 2)
	{
		real step = (real)(history->steps[0] - history->steps[1]) / (real)history->substep_count;

		velocity.i = (history->positions[0].x - history->positions[1].x) / step;
		velocity.j = (history->positions[0].y - history->positions[1].y) / step;
		velocity.k = (history->positions[0].z - history->positions[1].z) / step;
		rotation_turn(&history->rotations[1], &history->rotations[0], &spin);
		spin.i /= step;
		spin.j /= step;
		spin.k /= step;
		if (history->count >= 3)
		{
			real step_before = (real)(history->steps[1] - history->steps[2]) / (real)history->substep_count;
			real between = 0.5f * (step + step_before);
			real_vector3d spin_before;

			acceleration.i = (velocity.i - (history->positions[1].x - history->positions[2].x) / step_before) / between;
			acceleration.j = (velocity.j - (history->positions[1].y - history->positions[2].y) / step_before) / between;
			acceleration.k = (velocity.k - (history->positions[1].z - history->positions[2].z) / step_before) / between;
			rotation_turn(&history->rotations[2], &history->rotations[1], &spin_before);
			spin_acceleration.i = (spin.i - spin_before.i / step_before) / between;
			spin_acceleration.j = (spin.j - spin_before.j / step_before) / between;
			spin_acceleration.k = (spin.k - spin_before.k / step_before) / between;
			vector_at_most(&acceleration, NEWEST_ACCELERATION_MAXIMUM);
			vector_at_most(&spin_acceleration, NEWEST_SPIN_ACCELERATION_MAXIMUM);
			/* (the newest step's mean, half a step on: at its end) */
			velocity.i += acceleration.i * 0.5f * step;
			velocity.j += acceleration.j * 0.5f * step;
			velocity.k += acceleration.k * 0.5f * step;
			spin.i += spin_acceleration.i * 0.5f * step;
			spin.j += spin_acceleration.j * 0.5f * step;
			spin.k += spin_acceleration.k * 0.5f * step;
		}
	}
	motion->velocity = velocity;
	motion->acceleration = acceleration;
	motion->spin = spin;
	motion->spin_acceleration = spin_acceleration;
}

/* the motion carried on for the time since (ticks): shift, the place's
change, and turn, about an axis as long as its angle */
static void newest_motion_carried(
	struct newest_motion const *motion,
	real since,
	real_vector3d *shift,
	real_vector3d *turn)
{
	real half_squared = 0.5f * since * since;

	shift->i = motion->velocity.i * since + motion->acceleration.i * half_squared;
	shift->j = motion->velocity.j * since + motion->acceleration.j * half_squared;
	shift->k = motion->velocity.k * since + motion->acceleration.k * half_squared;
	turn->i = motion->spin.i * since + motion->spin_acceleration.i * half_squared;
	turn->j = motion->spin.j * since + motion->spin_acceleration.j * half_squared;
	turn->k = motion->spin.k * since + motion->spin_acceleration.k * half_squared;
}

/* a rotation turned on, about the turn's axis by its length (radians) */
static void quaternion_turned(
	struct interpolation_quaternion const *rotation,
	real_vector3d const *turn,
	struct interpolation_quaternion *result)
{
	real angle = (real)sqrt(turn->i * turn->i + turn->j * turn->j + turn->k * turn->k);
	struct interpolation_quaternion q, r = *rotation;
	real s;

	if (!(angle > 1e-7f))
	{
		*result = r;
		return;
	}
	s = (real)sin(0.5f * angle) / angle;
	q.w = (real)cos(0.5f * angle);
	q.i = turn->i * s;
	q.j = turn->j * s;
	q.k = turn->k * s;
	result->w = q.w * r.w - q.i * r.i - q.j * r.j - q.k * r.k;
	result->i = q.w * r.i + q.i * r.w + q.j * r.k - q.k * r.j;
	result->j = q.w * r.j - q.i * r.k + q.j * r.w + q.k * r.i;
	result->k = q.w * r.k + q.i * r.j - q.j * r.i + q.k * r.w;
}

/* how much of a difference blended away is left at its age (ticks): all of
it as it begins, none a tick on (NEWEST_BLEND_TIME), easing out and in, so
that the vehicle's motion as drawn never jumps where it begins or ends */
static real newest_blend_left(real age)
{
	real x = age / NEWEST_BLEND_TIME;

	if (!(x < 1.0f))
		return 0.0f;
	if (x <= 0.0f)
		return 1.0f;
	return 1.0f - x * x * (3.0f - 2.0f * x);
}

/* the differences being blended away, as left now: their places' sum and
their turns' (small turns, added) */
static void newest_blends_left(
	struct newest_history const *history,
	real_vector3d *place,
	real_vector3d *turn)
{
	short index;

	*place = *global_zero_vector3d;
	*turn = *global_zero_vector3d;
	for (index = 0; index < history->blend_count; index++)
	{
		real left = newest_blend_left(history->blend_ages[index]);

		place->i += history->blend_places[index].i * left;
		place->j += history->blend_places[index].j * left;
		place->k += history->blend_places[index].k * left;
		turn->i += history->blend_turns[index].i * left;
		turn->j += history->blend_turns[index].j * left;
		turn->k += history->blend_turns[index].k * left;
	}
}

/* (render_interpolation_newest_vehicle) The vehicle carried on from its
newest step meets the next step where that step left it, which the motion
carried on did not foresee as it changed (a wheel touching down, a landing,
a bounce, a crash): drawn so, it would jump there in a frame, at 30 up to a
tick's misjudged motion. The difference, from where the frame before's
motion carried on puts it now (with what was left of the one before) to
where the new step's does, is drawn and blended away over a tick, easing
out and in, on top of those blended still (at 60 and 120 a step's comes
before the last's is done), so that
the vehicle moves on from where it was drawn, its motion never jumping; but not for one
that stood (a start is drawn at once, a step's latency not given back), nor
a jump. Its place (residual) and rotation (residual_turn) as left now */
static void render_interpolation_newest_blend(
	struct newest_history *history,
	long vehicle_index,
	long step,
	real since,
	real_matrix4x3 const *root,
	struct newest_motion const *motion,
	real_vector3d *residual,
	real_vector3d *residual_turn)
{
	double frame_time = (double)interpolation_tick + (double)interpolation_fraction;
	real_vector3d shift, turn;
	struct interpolation_quaternion rotation, now_rotation;
	short index;

	quaternion_from_basis(root, &rotation);
	if (!history->model_valid || history->vehicle_index != vehicle_index || history->count < 2 ||
		!(frame_time >= history->frame_time) || frame_time - history->frame_time > NEWEST_HISTORY_GAP)
	{
		history->blend_count = 0;
	}
	else
	{
		short kept = 0;

		/* (the blends aged to now; those done let go) */
		for (index = 0; index < history->blend_count; index++)
		{
			history->blend_ages[index] += (real)(frame_time - history->frame_time);
			if (newest_blend_left(history->blend_ages[index]) > 0.0f)
			{
				history->blend_places[kept] = history->blend_places[index];
				history->blend_turns[kept] = history->blend_turns[index];
				history->blend_ages[kept] = history->blend_ages[index];
				kept++;
			}
		}
		history->blend_count = kept;
		if (step != history->model_step)
		{
			struct newest_motion before;
			real since_model = (real)(frame_time * history->substep_count - (double)history->model_step) /
				(real)history->substep_count;
			real_point3d drawn_before;
			struct interpolation_quaternion before_rotation;
			real_vector3d difference, turned;
			real velocity_before;

			/* where the frame before's motion carries it now (the blends
			under way go on as they were, on top) ... */
			before.velocity = history->model_velocity;
			before.acceleration = history->model_acceleration;
			before.spin = history->model_spin;
			before.spin_acceleration = history->model_spin_acceleration;
			newest_motion_carried(&before, since_model, &shift, &turn);
			drawn_before.x = history->model_position.x + shift.i;
			drawn_before.y = history->model_position.y + shift.j;
			drawn_before.z = history->model_position.z + shift.k;
			quaternion_turned(&history->model_rotation, &turn, &before_rotation);
			/* ... and where the new step's does */
			newest_motion_carried(motion, since, &shift, &turn);
			quaternion_turned(&rotation, &turn, &now_rotation);
			difference.i = drawn_before.x - (root->position.x + shift.i);
			difference.j = drawn_before.y - (root->position.y + shift.j);
			difference.k = drawn_before.z - (root->position.z + shift.k);
			rotation_turn(&now_rotation, &before_rotation, &turned);
			velocity_before = (real)sqrt(before.velocity.i * before.velocity.i + before.velocity.j * before.velocity.j +
				before.velocity.k * before.velocity.k);
			/* (so written that a difference not a number is a jump) */
			if (!(velocity_before >= NEWEST_STANDING) ||
				!(difference.i * difference.i + difference.j * difference.j + difference.k * difference.k <=
					CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE) ||
				!(turned.i * turned.i + turned.j * turned.j + turned.k * turned.k <=
					NEWEST_TURN_MAXIMUM * NEWEST_TURN_MAXIMUM))
			{
				history->blend_count = 0;
			}
			else
			{
				/* (no room: the oldest's rest taken into the new one) */
				if (history->blend_count >= NEWEST_BLENDS)
				{
					real left = newest_blend_left(history->blend_ages[0]);

					difference.i += history->blend_places[0].i * left;
					difference.j += history->blend_places[0].j * left;
					difference.k += history->blend_places[0].k * left;
					turned.i += history->blend_turns[0].i * left;
					turned.j += history->blend_turns[0].j * left;
					turned.k += history->blend_turns[0].k * left;
					for (index = 1; index < history->blend_count; index++)
					{
						history->blend_places[index - 1] = history->blend_places[index];
						history->blend_turns[index - 1] = history->blend_turns[index];
						history->blend_ages[index - 1] = history->blend_ages[index];
					}
					history->blend_count--;
				}
				history->blend_places[history->blend_count] = difference;
				history->blend_turns[history->blend_count] = turned;
				history->blend_ages[history->blend_count] = 0.0f;
				history->blend_count++;
			}
		}
	}
	newest_blends_left(history, residual, residual_turn);
	/* (the motion carried on, from this step, for the next frame) */
	history->model_valid = TRUE;
	history->model_step = step;
	history->model_position = root->position;
	history->model_rotation = rotation;
	history->model_velocity = motion->velocity;
	history->model_acceleration = motion->acceleration;
	history->model_spin = motion->spin;
	history->model_spin_acceleration = motion->spin_acceleration;
	history->frame_time = frame_time;
}

/* (render_interpolation_newest_vehicles) the local player's vehicle as its
newest step left it, carried on: FALSE when it cannot be drawn so (no
snapshots of the tick drawn and the one before, a jump between them, a root
node no rotation; a seat whose view is the gun's, which aims once a tick:
its view would turn a tick at a time, and is drawn between the ticks as
before) */
static boolean render_interpolation_newest_vehicle(
	short local_player_index,
	struct newest_vehicle *newest)
{
	long unit_index = player_control_get_unit_index(local_player_index);
	long vehicle_index = object_get(unit_index)->object.parent_object_index;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index);
	struct object_datum *vehicle = object_get(vehicle_index);
	struct newest_history *history = &newest_histories[local_player_index];
	real_vector3d shift, turn, residual, residual_turn;
	struct newest_motion motion;
	struct interpolated_object *record;
	real_matrix4x3 const *root;
	real_matrix4x3 const *frames[2];
	real_matrix4x3 const *frame;
	real_point3d anchor;
	real_point3d line;
	real since;
	real angle;
	long step;
	boolean gun_view;
	boolean aim_in_world;

	if (!interpolated_objects || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return FALSE;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != vehicle_index || record->tick != interpolation_tick || !record->has_previous)
		return FALSE;
	frames[0] = record->nodes + (record->latest ^ 1) * record->node_capacity;
	frames[1] = record->nodes + record->latest * record->node_capacity;
	/* (as the newest step left it: no frame is drawn yet) */
	root = (real_matrix4x3 const *)object_header_block_get(vehicle_index, &vehicle->object.node_matrices);
	/* (so written that a position not a number is a jump) */
	if (!basis_is_rotation(root) || !basis_is_rotation(frames[0]) || !basis_is_rotation(frames[1]) ||
		!(distance_squared(&frames[0]->position, &frames[1]->position) <= OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE))
	{
		return FALSE;
	}
	render_interpolation_seat(unit_index, &gun_view, &aim_in_world);
	if (gun_view)
		return FALSE;
	/* (the frame's time, from the newest step's end, in ticks) */
	step = render_interpolation_newest_step(vehicle_index);
	since = interpolation_step_age - (real)(step - (interpolation_tick * interpolation_substep_count +
		sim_substep_next())) / (real)interpolation_substep_count;
	render_interpolation_newest_history(history, vehicle_index, step, root);
	render_interpolation_newest_motion(history, vehicle, &motion);
	newest_motion_carried(&motion, since, &shift, &turn);
	render_interpolation_newest_blend(history, vehicle_index, step, since, root, &motion, &residual, &residual_turn);
	newest->vehicle_index = vehicle_index;
	newest->unit_index = unit_index;
	newest->step = step;
	newest->since = since;
	newest->aim_in_world = aim_in_world;
	newest->root = *root;
	newest->drawn = *root;
	newest->forward = vehicle->object.forward;
	newest->up = vehicle->object.up;
	/* carried on (so written that a shift not a number, or a teleport's, is
	none) */
	if (shift.i * shift.i + shift.j * shift.j + shift.k * shift.k <= CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE)
	{
		newest->drawn.position.x += shift.i;
		newest->drawn.position.y += shift.j;
		newest->drawn.position.z += shift.k;
	}
	/* (with the blend away from where it was drawn) */
	newest->drawn.position.x += residual.i;
	newest->drawn.position.y += residual.j;
	newest->drawn.position.z += residual.k;
	{
		struct interpolation_quaternion at_root, turned;

		/* (the turn as drawn, from the root as the step left it) */
		quaternion_from_basis(root, &at_root);
		quaternion_turned(&at_root, &turn, &turned);
		quaternion_turned(&turned, &residual_turn, &turned);
		rotation_turn(&at_root, &turned, &turn);
	}
	/* ... and turned on, about its root node (so written that a turn not a
	number is none) */
	angle = (real)sqrt(turn.i * turn.i + turn.j * turn.j + turn.k * turn.k);
	if (angle > 1e-6f && angle <= NEWEST_TURN_MAXIMUM)
	{
		real_vector3d axis;

		axis.i = turn.i / angle;
		axis.j = turn.j / angle;
		axis.k = turn.k / angle;
		vector_turned(&root->forward, &axis, angle, &newest->drawn.forward);
		vector_turned(&root->left, &axis, angle, &newest->drawn.left);
		vector_turned(&root->up, &axis, angle, &newest->drawn.up);
		vector_turned(&vehicle->object.forward, &axis, angle, &newest->forward);
		vector_turned(&vehicle->object.up, &axis, angle, &newest->up);
	}
	unit_get_camera_position(unit_index, &anchor);
	point_in_frame(root, &anchor, &newest->anchor);
	/* how far ahead of where the frames between the ticks would draw it,
	and that point on it (as render_interpolation_blended_camera has it: on
	its sub-steps' path, or between the ticks' places of it) */
	if (render_interpolation_substep_frame(vehicle_index, frames, &frame))
	{
		struct interpolated_camera const *camera = &interpolated_cameras[local_player_index];

		line = frame->position;
		/* (the point the frames between the ticks hang the view on, as the
		tick left it on the vehicle: render_interpolation_camera_anchors) */
		point_from_frame(frame, camera->anchor_vehicle_index == vehicle_index && camera->anchor_tick ==
			interpolation_tick ? &camera->anchor : &newest->anchor, &newest->anchor_between);
	}
	else
	{
		real_point3d previous_anchor, latest_anchor;

		point_lerp(&frames[0]->position, &frames[1]->position, interpolation_fraction, &line);
		point_from_frame(frames[0], &newest->anchor, &previous_anchor);
		point_from_frame(frames[1], &newest->anchor, &latest_anchor);
		point_lerp(&previous_anchor, &latest_anchor, interpolation_fraction, &newest->anchor_between);
	}
	newest->ahead.i = newest->drawn.position.x - line.x;
	newest->ahead.j = newest->drawn.position.y - line.y;
	newest->ahead.k = newest->drawn.position.z - line.z;
	/* (so written that a difference not a number is none) */
	newest->ahead_valid = newest->ahead.i * newest->ahead.i + newest->ahead.j * newest->ahead.j +
		newest->ahead.k * newest->ahead.k <= CORRECTION_MAXIMUM * CORRECTION_MAXIMUM;
	return TRUE;
}

/* (render_interpolation_newest_vehicles) the object, and what it carries,
drawn from another place than the frame before drew it: the difference
(offset: where it was drawn, from where it is drawn now) glides away, as a
correction's does. Drawing only */
static void render_interpolation_glide(
	long object_index,
	real_vector3d const *offset)
{
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	long child_index;

	if (!interpolated_objects || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return;
	if (interpolated_objects[absolute_index].object_index == object_index)
	{
		struct interpolated_object *record = &interpolated_objects[absolute_index];

		correction_add(&record->correction, &record->correction_pending, offset);
		record->blended_frame = NONE;
	}
	for (child_index = object_get(object_index)->object.first_child_object_index; child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		render_interpolation_glide(child_index, offset);
	}
}

/* whether one of the local players' vehicles drawn from the newest step
(of those given, before the count given) is this one */
static boolean render_interpolation_newest_among(
	struct newest_vehicle const *vehicles,
	short count,
	long vehicle_index)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < count; local_player_index++)
	{
		if (vehicles[local_player_index].valid && vehicles[local_player_index].vehicle_index == vehicle_index)
			return TRUE;
	}
	return FALSE;
}

/* whether the vehicle is drawn between the ticks it moved in, not cut to
where the last left it (a teleport, a new vehicle: no glide to it) */
static boolean render_interpolation_vehicle_continues(
	long vehicle_index)
{
	struct interpolated_object const *record;
	real_matrix4x3 const *previous, *latest;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index);

	if (!interpolated_objects || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS || !object_try_and_get(vehicle_index))
		return FALSE;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != vehicle_index || record->tick != interpolation_tick || !record->has_previous)
		return FALSE;
	previous = record->nodes + (record->latest ^ 1) * record->node_capacity;
	latest = record->nodes + record->latest * record->node_capacity;
	/* (so written that a position not a number is a cut) */
	return distance_squared(&previous->position, &latest->position) <= OBJECT_SNAP_DISTANCE * OBJECT_SNAP_DISTANCE;
}

/* (render_interpolation_frame_begin, before any object is drawn between the
ticks: rendering, whether the frame is) A local player's view from a seat of
their vehicle (render_interpolation_direct, while frames are drawn between
the ticks) stands where the vehicle's newest step left it, as on foot, and
aims where the player aims (render_interpolation_seat_camera). So the
vehicle, and what it carries (its riders, the player in the seat, its gun),
is drawn as that step left it, carried on as that step moved and turned it
(render_interpolation_newest_motion) for the time since that step
(render_interpolation_newest_step), or the view
would leave the vehicle drawn a step behind it (out through a Warthog's
windshield at speed). Its pose (its wheels, its turret's aim, a rider's
animation) is blended between the ticks as before, in its frame
(render_interpolation_newest_frame). Everything else, another player's
vehicle too, is drawn between the ticks as before. As a vehicle comes to be
drawn so, or no longer (a player gets in or out, dies, rides another), the
difference glides away as a correction's does, rather than a jump: the
vehicle, what it carries, and the player who left it. Drawing only: the game
is as it was */
static void render_interpolation_newest_vehicles(
	boolean rendering)
{
	struct newest_vehicle was[MAXIMUM_LOCAL_PLAYERS];
	short local_player_index;

	memcpy(was, newest_vehicles, sizeof(was));
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct newest_vehicle *newest = &newest_vehicles[local_player_index];

		newest->valid = rendering && render_interpolation_direct(local_player_index) == _direct_seat &&
			render_interpolation_newest_vehicle(local_player_index, newest);
	}
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct newest_vehicle const *before = &was[local_player_index];
		struct newest_vehicle const *newest = &newest_vehicles[local_player_index];

		/* drawn between the ticks again, from where it was drawn (once, for
		the first player who rode it; not cut away meanwhile) */
		if (before->valid && before->ahead_valid &&
			!render_interpolation_newest_among(newest_vehicles, MAXIMUM_LOCAL_PLAYERS, before->vehicle_index) &&
			!render_interpolation_newest_among(was, local_player_index, before->vehicle_index) &&
			render_interpolation_vehicle_continues(before->vehicle_index))
		{
			render_interpolation_glide(before->vehicle_index, &before->ahead);
			/* (and the player who left it) */
			if (object_try_and_get(before->unit_index) &&
				object_get_ultimate_parent(before->unit_index) != before->vehicle_index &&
				!render_interpolation_newest_among(newest_vehicles, MAXIMUM_LOCAL_PLAYERS,
					object_get_ultimate_parent(before->unit_index)))
			{
				render_interpolation_glide(before->unit_index, &before->ahead);
			}
		}
		/* drawn from the newest step, from where the frames between the
		ticks drew it (once) */
		if (newest->valid && newest->ahead_valid &&
			!render_interpolation_newest_among(was, MAXIMUM_LOCAL_PLAYERS, newest->vehicle_index) &&
			!render_interpolation_newest_among(newest_vehicles, local_player_index, newest->vehicle_index))
		{
			real_vector3d back;

			back.i = -newest->ahead.i;
			back.j = -newest->ahead.j;
			back.k = -newest->ahead.k;
			render_interpolation_glide(newest->vehicle_index, &back);
		}
	}
}

/* the glide drawn this frame of a vehicle drawn from its newest step, which
what it carries, and the view from its seat, glide by with it: its
correction's (render_interpolation_correct_object, and
render_interpolation_glide's) */
static void render_interpolation_newest_correction(
	long vehicle_index,
	real_vector3d *correction)
{
	struct interpolated_object const *record = &interpolated_objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index)];

	*correction = *global_zero_vector3d;
	if (correction_significant(&record->correction) || correction_significant(&record->correction_pending))
		correction_drawn(&record->correction, &record->correction_pending, correction);
}

/* (render_interpolation_object_node_matrices) the frame an object of a local
player's vehicle drawn from its newest step (the vehicle, or what it
carries) is drawn in: the vehicle's root node as that step left it, carried
on (render_interpolation_newest_vehicles), with the frames of its two
snapshots, which the object's nodes are blended in (as
render_interpolation_substep_frame's), and the vehicle's glide (correction).
FALSE for any other object */
static boolean render_interpolation_newest_frame(
	long object_index,
	real_matrix4x3 const **frames,
	real_matrix4x3 const **drawn,
	real_vector3d *correction)
{
	long vehicle_index;
	short local_player_index;

	if (!interpolated_objects || object_index == NONE)
		return FALSE;
	vehicle_index = object_get_ultimate_parent(object_index);
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct newest_vehicle const *newest = &newest_vehicles[local_player_index];
		struct interpolated_object const *record;

		if (!newest->valid || newest->vehicle_index != vehicle_index)
			continue;
		record = &interpolated_objects[DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index)];
		frames[0] = record->nodes + (record->latest ^ 1) * record->node_capacity;
		frames[1] = record->nodes + record->latest * record->node_capacity;
		*drawn = &newest->drawn;
		render_interpolation_newest_correction(vehicle_index, correction);
		return TRUE;
	}
	return FALSE;
}

/* The view's pitch while the player's aim is held at the seat's pitch limit
(player_control.c's player_control_modify_desired_angles eases the limit
toward the vehicle's tilt, which changes a step at a time, at most so far a
frame: the aim held at it steps with it, a step's easing in a frame or two,
then holds). Drawn as the mean of the frames' pitches over the last step
(the frame's time back by a step), which a step's staircase makes a ramp and
an easing that runs on keeps as it was, a half a step behind; the aim's own
pitch, at once, when it is not held there (the player's turn is never put
off). The difference from the aim's pitch */
static real render_interpolation_seat_pitch(
	short local_player_index)
{
	struct newest_history *history = &newest_histories[local_player_index];
	struct player_control const *control = player_control_get(local_player_index);
	real pitch = player_control_get_facing_angles(local_player_index)->pitch;
	boolean held = pitch <= control->pitch_minimum + 1e-4f || pitch >= control->pitch_maximum - 1e-4f;
	double now = (double)interpolation_tick + (double)interpolation_fraction;
	double window = 1.0 / (double)interpolation_substep_count;
	double from = now - window;
	double weighed = 0.0, until = now;
	short index, at;

	if (!held || (history->pitch_count > 0 &&
		!(now >= history->pitch_times[(history->pitch_next + NEWEST_PITCH_FRAMES - 1) % NEWEST_PITCH_FRAMES])))
	{
		history->pitch_count = 0;
	}
	if (!held)
		return 0.0f;
	history->pitch_times[history->pitch_next] = now;
	history->pitches[history->pitch_next] = pitch;
	history->pitch_next = (short)((history->pitch_next + 1) % NEWEST_PITCH_FRAMES);
	if (history->pitch_count < NEWEST_PITCH_FRAMES)
		history->pitch_count++;
	/* (each frame's pitch held from the frame before's time to its own:
	the window's mean, as far back as the frames kept go) */
	for (index = 0, at = history->pitch_next; index < history->pitch_count; index++)
	{
		double since_time;

		at = (short)((at + NEWEST_PITCH_FRAMES - 1) % NEWEST_PITCH_FRAMES);
		since_time = index + 1 < history->pitch_count ?
			history->pitch_times[(at + NEWEST_PITCH_FRAMES - 1) % NEWEST_PITCH_FRAMES] : from;
		if (since_time < from)
			since_time = from;
		if (until > since_time)
			weighed += (until - since_time) * (double)history->pitches[at];
		until = since_time;
		if (until <= from)
			break;
	}
	if (now - until <= 1e-9)
		return 0.0f;
	return (real)(weighed / (now - until)) - pitch;
}

/* a view's axes from its forward (its up square to it, toward the world's:
observer_up_from_forward, as the following camera has it) */
static void view_frame(
	real_vector3d const *forward,
	real_matrix4x3 *frame)
{
	frame->forward = *forward;
	observer_up_from_forward(forward, &frame->up);
	cross_product3d(&frame->up, &frame->forward, &frame->left);
}

/* The local player's view from a seat of their vehicle drawn from its newest
step (render_interpolation_newest_vehicles), gliding with the vehicle. It
aims where the player aims now: the frame's facing, turned on by the aim's
motion come since (render_interpolation_look_late). In the first person, as
first_person_camera.c poses it, the observer (taken after the frame's steps)
moved with the point it hangs on, the unit's camera position, from where the
newest step left it to where the vehicle is drawn: the facing turned from
the world's up to the vehicle's in a seat whose aim is the vehicle's
(players.c's player_aiming_vector_from_facing), its up square to it in the
vehicle's frame (the vehicle's roll), of the vehicle as drawn. Following
it, about that point as drawn, swung as the frames between the ticks swing
it (blended: the camera's pull in at what it would pass through and its
track, which step a frame at a time as the observer finds them, blended from
a tick's to the next), turned with the aim: the player's (none of the
vehicle's turn), with the late motion but a passenger's (passenger_camera.c),
whose aim keeps to its firing arc. NULL for any other view */
static struct observer_result const *render_interpolation_seat_camera(
	short local_player_index,
	struct observer_result const *observer,
	struct observer_result const *blended)
{
	struct newest_vehicle const *newest = &newest_vehicles[local_player_index];
	struct observer_result *direct;
	real_point3d anchor_now, anchor_drawn;
	real_vector3d correction;
	real yaw, pitch;

	if (!interpolation_rendering || !observer || !blended || !newest->valid ||
		player_control_get_unit_index(local_player_index) != newest->unit_index)
	{
		return NULL;
	}
	direct = &direct_cameras[local_player_index];
	*direct = *observer;
	point_from_frame(&newest->root, &newest->anchor, &anchor_now);
	point_from_frame(&newest->drawn, &newest->anchor, &anchor_drawn);
	if (director_get_perspective(local_player_index) == _director_perspective_first_person)
	{
		real_euler_angles2d angles = *player_control_get_facing_angles(local_player_index);
		real_matrix4x3 frame;
		real_vector3d forward, up;

		direct->position.x = anchor_drawn.x + observer->position.x - anchor_now.x;
		direct->position.y = anchor_drawn.y + observer->position.y - anchor_now.y;
		direct->position.z = anchor_drawn.z + observer->position.z - anchor_now.z;
		if (render_interpolation_look_late(local_player_index, &yaw, &pitch))
		{
			struct player_control const *control = player_control_get(local_player_index);

			angles.yaw += yaw;
			angles.pitch = PIN(angles.pitch + pitch, control->pitch_minimum, control->pitch_maximum);
		}
		/* (held at the seat's pitch limit, evenly) */
		angles.pitch += render_interpolation_seat_pitch(local_player_index);
		vector3d_from_euler_angles2d(&direct->forward, &angles);
		if (!newest->aim_in_world)
		{
			/* (players.c's: forward across the vehicle's up and the world's
			right, up the vehicle's) */
			frame.up = newest->up;
			cross_product3d(&newest->up, global_right3d, &frame.forward);
			if (normalize3d(&frame.forward) == 0.0f)
			{
				cross_product3d(&newest->up, global_down3d, &frame.forward);
				normalize3d(&frame.forward);
			}
			cross_product3d(&frame.up, &frame.forward, &frame.left);
			vector_from_frame(&frame, &direct->forward, &direct->forward);
		}
		/* (first_person_camera.c's: up square to forward in the vehicle's
		frame) */
		frame.forward = newest->forward;
		frame.up = newest->up;
		cross_product3d(&frame.up, &frame.forward, &frame.left);
		vector_in_frame(&frame, &direct->forward, &forward);
		observer_up_from_forward(&forward, &up);
		vector_from_frame(&frame, &up, &direct->up);
	}
	else
	{
		struct interpolated_camera const *camera = &interpolated_cameras[local_player_index];
		real_matrix4x3 swung, aimed;
		real_vector3d swing, local;

		/* the swing between the ticks (without the camera's glide: the
		vehicle's is drawn), or the observer's own, from the point as the
		newest step left it, when the frames do not blend it (a cut) */
		if (blended == &camera->blended)
		{
			swing.i = blended->position.x - newest->anchor_between.x;
			swing.j = blended->position.y - newest->anchor_between.y;
			swing.k = blended->position.z - newest->anchor_between.z;
			if (correction_significant(&camera->correction) || correction_significant(&camera->correction_pending))
			{
				correction_drawn(&camera->correction, &camera->correction_pending, &correction);
				swing.i -= correction.i;
				swing.j -= correction.j;
				swing.k -= correction.k;
			}
		}
		else
		{
			swing.i = observer->position.x - anchor_now.x;
			swing.j = observer->position.y - anchor_now.y;
			swing.k = observer->position.z - anchor_now.z;
		}
		/* (held at the seat's pitch limit, evenly; and the late motion) */
		yaw = 0.0f;
		pitch = render_interpolation_seat_pitch(local_player_index);
		if (!passenger_camera_enabled(local_player_index))
		{
			real late_yaw, late_pitch;

			if (render_interpolation_look_late(local_player_index, &late_yaw, &late_pitch))
			{
				struct player_control const *control = player_control_get(local_player_index);
				real pitch_now = player_control_get_facing_angles(local_player_index)->pitch;

				yaw = late_yaw;
				/* (as far as the player's pitch may go) */
				pitch += PIN(pitch_now + late_pitch, control->pitch_minimum, control->pitch_maximum) - pitch_now;
			}
		}
		if (yaw != 0.0f || pitch != 0.0f)
		{
			real_vector3d axis;
			real length;

			/* the yaw about the world's up, the pitch about the view's right */
			axis.i = 0.0f;
			axis.j = 0.0f;
			axis.k = 1.0f;
			vector_turned(&direct->forward, &axis, yaw, &direct->forward);
			axis.i = direct->forward.j;
			axis.j = -direct->forward.i;
			axis.k = 0.0f;
			length = vector_length(&axis);
			if (length > 1e-6f)
			{
				axis.i /= length;
				axis.j /= length;
				vector_turned(&direct->forward, &axis, pitch, &direct->forward);
			}
		}
		/* (turned from the aim it was swung for to the view's) */
		view_frame(&blended->forward, &swung);
		view_frame(&direct->forward, &aimed);
		vector_in_frame(&swung, &swing, &local);
		vector_from_frame(&aimed, &local, &swing);
		direct->position.x = anchor_drawn.x + swing.i;
		direct->position.y = anchor_drawn.y + swing.j;
		direct->position.z = anchor_drawn.z + swing.k;
		observer_up_from_forward(&direct->forward, &direct->up);
	}
	render_interpolation_newest_correction(newest->vehicle_index, &correction);
	direct->position.x += correction.i;
	direct->position.y += correction.j;
	direct->position.z += correction.k;
	return direct;
}

struct observer_result const *render_interpolation_camera(
	short local_player_index,
	struct observer_result const *observer)
{
	struct observer_result const *blended;
	struct observer_result const *direct;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return observer;
	blended = render_interpolation_blended_camera(local_player_index, observer);
	drawn_views_valid[local_player_index] = FALSE;
	newest_carried_valid[local_player_index] = FALSE;
	/* (in a seat of the player's vehicle drawn from its newest step) */
	direct = render_interpolation_seat_camera(local_player_index, observer, blended);
	if (direct)
	{
		newest_views[local_player_index] = TRUE;
		return direct;
	}
	direct = render_interpolation_direct_camera(local_player_index, blended);
	newest_views[local_player_index] = interpolation_rendering && direct == &direct_cameras[local_player_index];
#ifdef HALO_VR
	if (halo_vr_running())
		newest_views[local_player_index] = FALSE;
#endif
	if (newest_views[local_player_index])
	{
		struct observer_result const *drawn = &direct_cameras[local_player_index];

		render_interpolation_newest_camera(local_player_index, observer, &direct_cameras[local_player_index]);
		newest_carried[local_player_index].i = drawn->position.x - observer->position.x;
		newest_carried[local_player_index].j = drawn->position.y - observer->position.y;
		newest_carried[local_player_index].k = drawn->position.z - observer->position.z;
		newest_carried_valid[local_player_index] = TRUE;
	}
	if (blended && direct)
	{
		blended_views[local_player_index] = *blended;
		drawn_views[local_player_index] = *direct;
		drawn_views_valid[local_player_index] = TRUE;
	}
	return direct;
}

/* The local player's own flashlight in the first person on foot, when the
first-person weapon does not carry it (none in hand, or zoomed: it is on the
weapon's marker otherwise, which is posed from the view): the game lights it
from the body's marker, on the head that the first person does not draw.
That marker turns with the head, whose look trails the aim through a turn
and catches up with it, and is drawn between the ticks, a tick behind the
view drawn from the newest step (display.direct_camera); so the pool of
light slid about the screen and jumped back as the player turned (#589).
Here it shines along the view drawn, its aim (up as the view's), from where
the body has it: as far along, across and up from the view drawn as it is
from the view between the ticks, which goes with the body. FALSE (and
nothing moved) for any other unit, view or window, and in the VR mode,
whose view is the head's */
boolean render_interpolation_view_flashlight(
	long unit_index,
	real_point3d *position,
	real_vector3d *forward,
	real_vector3d *up)
{
	short local_player_index = render.local_player_index;
	struct observer_result const *from, *to;
	real_vector3d from_left, to_left, offset;
	real along, across, height;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!drawn_views_valid[local_player_index] || unit_index == NONE ||
		player_control_get_unit_index(local_player_index) != unit_index ||
		director_get_perspective(local_player_index) != _director_perspective_first_person ||
		director_inhibited_facing(local_player_index) || cinematic_in_progress() ||
		object_get(unit_index)->object.parent_object_index != NONE)
	{
		return FALSE;
	}
#ifdef HALO_VR
	if (halo_vr_running())
		return FALSE;
#endif
	from = &blended_views[local_player_index];
	to = &drawn_views[local_player_index];
	cross_product3d(&from->up, &from->forward, &from_left);
	cross_product3d(&to->up, &to->forward, &to_left);
	offset.i = position->x - from->position.x;
	offset.j = position->y - from->position.y;
	offset.k = position->z - from->position.z;
	along = dot_product3d(&offset, &from->forward);
	across = dot_product3d(&offset, &from_left);
	height = dot_product3d(&offset, &from->up);
	position->x = to->position.x + to->forward.i * along + to_left.i * across + to->up.i * height;
	position->y = to->position.y + to->forward.j * along + to_left.j * across + to->up.j * height;
	position->z = to->position.z + to->forward.k * along + to_left.k * across + to->up.k * height;
	*forward = to->forward;
	*up = to->up;
	return TRUE;
}

/* a view's heading, level: its forward along the ground (FALSE: it looks
straight up or down, and has none) */
static boolean view_heading(
	struct observer_result const *view,
	real *i,
	real *j)
{
	real length = (real)sqrt(view->forward.i * view->forward.i + view->forward.j * view->forward.j);

	if (length < 1e-4f)
		return FALSE;
	*i = view->forward.i / length;
	*j = view->forward.j / length;
	return TRUE;
}

/* the unit's facing, level, as its body is drawn this frame: the blend of
its facing at the two ticks drawn between, as its pose is (FALSE: none, or
level it has none) */
static boolean render_interpolation_body_heading(
	long unit_index,
	real *i,
	real *j)
{
	struct interpolated_object const *record = NULL;
	real_vector3d const *forward = &object_get(unit_index)->object.forward;
	real length;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(unit_index);

	*i = forward->i;
	*j = forward->j;
	if (interpolation_rendering && interpolated_objects && absolute_index < MAXIMUM_INTERPOLATED_OBJECTS)
		record = &interpolated_objects[absolute_index];
	if (record && record->object_index == unit_index && record->tick == interpolation_tick && record->has_previous)
	{
		real_vector3d const *a = &record->facing[record->latest ^ 1];
		real_vector3d const *b = &record->facing[record->latest];

		*i = lerp(a->i, b->i, interpolation_fraction);
		*j = lerp(a->j, b->j, interpolation_fraction);
	}
	length = (real)sqrt(*i * *i + *j * *j);
	if (!(length >= 1e-4f))
		return FALSE;
	*i /= length;
	*j /= length;
	return TRUE;
}

/* The local player's own body in the first person on foot, drawn below the
view as its legs (display.first_person_legs: first_person_legs.c). The body
is drawn between the ticks, a tick behind, as the view between the ticks is;
the view drawn is the newest step's (display.direct_camera), a tick's motion
on, and points where the player aims in the frame. Drawn where the body has
them, the legs trailed the view by a tick's motion, and by more or less as
each step's speed and each frame's aim came: they slid under the view and
stepped against it (#697). And the body faces where the player aimed only
after the aim has turned far enough from it, then turns after it a tick at
a time: looking down while turning, the legs swung away from the view, came
back with a jerk each tick and swung away again (#826). Here the move that
takes the body to the view drawn, level: turned about the vertical from the
body's heading as it is drawn to the view drawn's heading (*heading), and
moved from the view between the ticks' place to the view drawn's (the body
stands upright, whatever the pitch). So the legs are posed about the eye as
the view is, at its yaw, place and time this frame, as the first-person
weapon is: still under the view as it turns, moving only as the body's
animation moves them. FALSE (and the move none, *heading the body's) for any
other unit, view or window, and in the VR mode, whose body has its own
place */
boolean render_interpolation_view_body(
	long unit_index,
	real_matrix4x3 *move,
	real_vector3d *heading)
{
	short local_player_index = render.local_player_index;
	struct observer_result const *from, *to;
	real from_i, from_j, to_i, to_j, c = 1.0f, s = 0.0f;
	boolean turned;

	move->scale = 1.0f;
	move->forward.i = 1.0f;
	move->forward.j = 0.0f;
	move->forward.k = 0.0f;
	move->left.i = 0.0f;
	move->left.j = 1.0f;
	move->left.k = 0.0f;
	move->up.i = 0.0f;
	move->up.j = 0.0f;
	move->up.k = 1.0f;
	move->position.x = 0.0f;
	move->position.y = 0.0f;
	move->position.z = 0.0f;
	if (unit_index != NONE && object_try_and_get(unit_index))
		*heading = object_get(unit_index)->object.forward;
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!drawn_views_valid[local_player_index] || unit_index == NONE ||
		player_control_get_unit_index(local_player_index) != unit_index ||
		director_get_perspective(local_player_index) != _director_perspective_first_person ||
		director_inhibited_facing(local_player_index) || cinematic_in_progress() ||
		object_get(unit_index)->object.parent_object_index != NONE)
	{
		return FALSE;
	}
#ifdef HALO_VR
	if (halo_vr_running())
		return FALSE;
#endif
	from = &blended_views[local_player_index];
	to = &drawn_views[local_player_index];
	/* (the turn from the body's heading to the view's: none if either has
	none) */
	turned = render_interpolation_body_heading(unit_index, &from_i, &from_j) && view_heading(to, &to_i, &to_j);
	if (turned)
	{
		c = from_i * to_i + from_j * to_j;
		s = from_i * to_j - from_j * to_i;
		heading->i = to_i;
		heading->j = to_j;
		heading->k = 0.0f;
	}
	move->forward.i = c;
	move->forward.j = s;
	move->left.i = -s;
	move->left.j = c;
	/* (turned about the view between the ticks' place, onto the view
	drawn's) */
	move->position.x = to->position.x - (c * from->position.x - s * from->position.y);
	move->position.y = to->position.y - (s * from->position.x + c * from->position.y);
	move->position.z = to->position.z - from->position.z;
	return TRUE;
}

/* The window's own player's unit on foot, its view drawn from the newest
step (render_interpolation_newest_camera): how far the view drawn is carried
on from where the newest step left it, which the unit's camera point (the
game's, the newest step's) is carried on by as well to be where the view is.
FALSE for any other unit, view or window: one drawn between the ticks, a
seat's (the vehicle drawn from its newest step carries its riders and the
view from the seat with it: render_interpolation_object_point), and the VR
mode's */
static boolean render_interpolation_view_carried(long unit_index, real_vector3d *carried)
{
	short local_player_index = render.local_player_index;

	if (!interpolation_rendering || unit_index == NONE ||
		local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!newest_carried_valid[local_player_index] ||
		player_control_get_unit_index(local_player_index) != unit_index)
	{
		return FALSE;
	}
	*carried = newest_carried[local_player_index];
	return TRUE;
}

boolean render_interpolation_view_newest(
	short local_player_index)
{
	return local_player_index >= 0 && local_player_index < MAXIMUM_LOCAL_PLAYERS &&
		newest_views[local_player_index];
}

/* between two of the observer's results: the position, the axes (up kept
square to forward) and the field of view */
static void observer_blend(
	struct observer_result const *a,
	struct observer_result const *b,
	real t,
	struct observer_result *result)
{
	real along, length;

	*result = *b;
	point_lerp(&a->position, &b->position, t, &result->position);
	vector_nlerp(&a->forward, &b->forward, t, &result->forward);
	vector_nlerp(&a->up, &b->up, t, &result->up);
	along = result->up.i * result->forward.i + result->up.j * result->forward.j + result->up.k * result->forward.k;
	result->up.i -= result->forward.i * along;
	result->up.j -= result->forward.j * along;
	result->up.k -= result->forward.k * along;
	length = vector_length(&result->up);
	if (length > 1e-6f)
	{
		result->up.i /= length;
		result->up.j /= length;
		result->up.k /= length;
	}
	else
	{
		result->up = b->up;
	}
	result->field_of_view = lerp(a->field_of_view, b->field_of_view, t);
}

/* a cut between two of the observer's results (so written that a position
or direction not a number cuts) */
static boolean observer_cut(
	struct observer_result const *a,
	struct observer_result const *b)
{
	return !(distance_squared(&a->position, &b->position) <= CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE) ||
		!(a->forward.i * b->forward.i + a->forward.j * b->forward.j + a->forward.k * b->forward.k >= CAMERA_CUT_COSINE);
}

/* A cut between two snapshots of a view that moves in the frames' time
(render_interpolation_camera_timed), taken at their times (ticks). Its
snapshots are as far apart as the frames that took them, a tick or more,
and a cutscene's fast camera (d40's flight after the Longsword, c40's)
moves nearly the distance of a cut in a tick, or further: judged as the
player's views are, two snapshots a little more than a tick apart were a
cut, drawn from the latest, a tick ahead of the world, and the next not,
so that the camera jumped ahead a tick's flight and back at random ticks
uncapped, and after any long frame (#805). It is a cut if it turned as
far as a cut does; or if it moved further than a cut's distance a tick
and, from the snapshot before (earlier, when no cut came between them),
further than a cut's distance from where its last move carried on would
have put it, or, with no such move to carry on (the first after a cut),
further than the view's last speed takes it besides: a camera set to
another point, or a cut from a moving camera, is still one, and a fast
camera's first move after a cut is not */
static boolean observer_timed_cut(
	struct observer_result const *earlier,
	double earlier_time,
	real speed,
	struct observer_result const *a,
	double a_time,
	struct observer_result const *b,
	double b_time)
{
	double apart = b_time - a_time;
	real ticks = (real)(apart > 1.0 ? apart : 1.0);
	real reach = CAMERA_CUT_DISTANCE * ticks;
	real_point3d expected;
	real carried;

	if (!(a->forward.i * b->forward.i + a->forward.j * b->forward.j + a->forward.k * b->forward.k >= CAMERA_CUT_COSINE))
		return TRUE;
	/* (so written that a position not a number cuts) */
	if (distance_squared(&a->position, &b->position) <= reach * reach)
		return FALSE;
	if (!earlier || !(a_time > earlier_time))
	{
		reach += speed * ticks;
		return !(distance_squared(&a->position, &b->position) <= reach * reach);
	}
	carried = (real)(apart / (a_time - earlier_time));
	expected.x = a->position.x + (a->position.x - earlier->position.x) * carried;
	expected.y = a->position.y + (a->position.y - earlier->position.y) * carried;
	expected.z = a->position.z + (a->position.z - earlier->position.z) * carried;
	return !(distance_squared(&expected, &b->position) <= CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE);
}

/* A view that moves in the frames' time, not the ticks': a scripted camera
(a cutscene's camera point and its transition, a camera animation, the dead
camera), which the director and the observer move once a frame by the
frame's time (main.c: director_update, observer_update), so that the
observer as a frame leaves it is where the camera is at that frame's moment.
Not a scripted first-person camera, which looks from its unit as the tick
left it, as the player's own view does. The first frame after a tick (whose
snapshot is taken) comes any part of a tick after it: at 30 a steady part,
at other rates a part that changes from tick to tick (and more after a long
frame), so that blended as if each snapshot were the tick's, the camera
lurched at each tick, ahead and back by as much as a tick's motion, in every
cutscene's slow move (#805). Its snapshots are kept with their times, and
the view is drawn where they place it a tick before the frame's moment, as
the objects are drawn */
static boolean render_interpolation_camera_timed(short local_player_index)
{
	long relative_object_index;

	if (director_get_perspective(local_player_index) != _director_perspective_scripted)
		return FALSE;
	relative_object_index = scripted_camera_object_relative_to();
	return relative_object_index == NONE || !scripted_camera_object_is_first_person_camera(relative_object_index);
}

/* The first-person view of a unit of the micro lane (a player's biped on
foot), which the observer poses from the unit as it is now: NONE for any
other view (in a seat, a cinematic, another perspective), or at 30. The
observer, as the frame after the tick took it, may have the next tick's
first sub-steps in it (a frame slower than a sub-step runs several, the
tick's last and the next's first); the view is drawn from the snapshots
moved back to where the tick left the unit, then moved with the unit onto
its sub-steps' path, as its body is (render_interpolation_substep_offset) */
static long render_interpolation_camera_substep_unit(short local_player_index)
{
	long unit_index;

	if (interpolation_substep_count <= 1 ||
		director_get_perspective(local_player_index) != _director_perspective_first_person ||
		cinematic_in_progress())
	{
		return NONE;
	}
	unit_index = player_control_get_unit_index(local_player_index);
	if (unit_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit) ||
		object_get(unit_index)->object.parent_object_index != NONE ||
		object_get_lane(unit_index) != _object_lane_micro)
	{
		return NONE;
	}
	return unit_index;
}

/* the observer (latest) as of the tick's end, where its unit (of the micro
lane) was as the tick left it: not with the next tick's sub-steps run since */
static void render_interpolation_camera_at_tick(long unit_index, struct observer_result *latest)
{
	struct interpolated_object *record;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(unit_index);
	real_point3d const *position;
	real_vector3d since;

	if (!interpolated_objects || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != unit_index || record->tick != interpolation_tick)
		return;
	position = &object_get(unit_index)->object.position;
	since.i = position->x - record->position.x;
	since.j = position->y - record->position.y;
	since.k = position->z - record->position.z;
	/* (so written that a difference not a number is none) */
	if (!(since.i * since.i + since.j * since.j + since.k * since.k <= CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE))
		return;
	latest->position.x -= since.i;
	latest->position.y -= since.j;
	latest->position.z -= since.k;
}

/* (render_interpolation_tick, as the tick ends) where each local player's
view hangs on the vehicle of the micro lane it rides in (with its sub-steps'
frames kept): its unit's camera position (units.c's unit_get_camera_position:
what the first-person camera stands at, and what the following one swings
about), in the frame of the vehicle's root node as the tick left it. Read
only, of the game */
static void render_interpolation_camera_anchors(void)
{
	short buffer = (short)(interpolation_tick & 1);
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct interpolated_camera *camera = &interpolated_cameras[local_player_index];
		struct interpolated_substeps *substeps;
		struct interpolated_object *record;
		long unit_index;
		long vehicle_index;
		long absolute_index;
		real_point3d anchor;

		camera->anchor_vehicle_index = NONE;
		if (!interpolated_substeps || !interpolated_objects)
			continue;
		unit_index = player_control_get_unit_index(local_player_index);
		if (unit_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit) ||
			object_get(unit_index)->object.parent_object_index == NONE)
		{
			continue;
		}
		vehicle_index = object_get_ultimate_parent(unit_index);
		absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index);
		if (absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
			continue;
		substeps = &interpolated_substeps[absolute_index];
		record = &interpolated_objects[absolute_index];
		if (substeps->object_index != vehicle_index || substeps->tick[buffer] != interpolation_tick ||
			!substeps->framed[buffer] || record->object_index != vehicle_index || record->tick != interpolation_tick)
		{
			continue;
		}
		unit_get_camera_position(unit_index, &anchor);
		point_in_frame(record->nodes + record->latest * record->node_capacity, &anchor, &camera->anchor);
		camera->anchor_tick = interpolation_tick;
		camera->anchor_vehicle_index = vehicle_index;
	}
}

/* The view of a local player in a seat of a vehicle of the micro lane (a
player drives it), in the first person or following it: the vehicle, when
the view's anchor on it was kept at the tick drawn
(render_interpolation_camera_anchors); NONE for any other view (on foot, a
cinematic, another perspective), or at 30. The observer, as the frame after
the tick took it, may have the next tick's first sub-steps in it, the
vehicle moved and turned by them; the view is drawn from the snapshots taken
back to where the tick left its anchor, then moved with the anchor onto the
vehicle's sub-steps' path, as the vehicle and its seat are
(render_interpolation_substep_frame) */
static long render_interpolation_camera_substep_vehicle(short local_player_index)
{
	struct interpolated_camera *camera = &interpolated_cameras[local_player_index];
	short perspective;
	long unit_index;

	if (interpolation_substep_count <= 1 || camera->anchor_vehicle_index == NONE ||
		camera->anchor_tick != interpolation_tick || cinematic_in_progress())
	{
		return NONE;
	}
	perspective = director_get_perspective(local_player_index);
	if (perspective != _director_perspective_first_person && perspective != _director_perspective_third_person)
		return NONE;
	unit_index = player_control_get_unit_index(local_player_index);
	if (unit_index == NONE || !object_try_and_get_and_verify_type(unit_index, _object_mask_unit) ||
		object_get(unit_index)->object.parent_object_index == NONE ||
		object_get_ultimate_parent(unit_index) != camera->anchor_vehicle_index)
	{
		return NONE;
	}
	return camera->anchor_vehicle_index;
}

/* the observer (latest) as of the tick's end, where the vehicle its view
hangs on (of the micro lane) left its anchor: not moved and turned by the
next tick's sub-steps run since */
static void render_interpolation_camera_at_tick_in_vehicle(
	struct interpolated_camera const *camera,
	struct observer_result *latest)
{
	struct interpolated_object *record;
	long vehicle_index = camera->anchor_vehicle_index;
	long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicle_index);
	real_matrix4x3 const *now;
	real_point3d at_tick, at_now;
	real_vector3d since;

	if (!interpolated_objects || absolute_index >= MAXIMUM_INTERPOLATED_OBJECTS)
		return;
	record = &interpolated_objects[absolute_index];
	if (record->object_index != vehicle_index || record->tick != interpolation_tick)
		return;
	/* (as it is now: object_get_node_matrices gives the drawn pose while a
	frame is drawn) */
	now = (real_matrix4x3 const *)object_header_block_get(vehicle_index, &object_get(vehicle_index)->object.node_matrices);
	if (!basis_is_rotation(now))
		return;
	point_from_frame(record->nodes + record->latest * record->node_capacity, &camera->anchor, &at_tick);
	point_from_frame(now, &camera->anchor, &at_now);
	since.i = at_now.x - at_tick.x;
	since.j = at_now.y - at_tick.y;
	since.k = at_now.z - at_tick.z;
	/* (so written that a difference not a number is none) */
	if (!(since.i * since.i + since.j * since.j + since.k * since.k <= CAMERA_CUT_DISTANCE * CAMERA_CUT_DISTANCE))
		return;
	latest->position.x -= since.i;
	latest->position.y -= since.j;
	latest->position.z -= since.k;
}

/* (the frames) how far the view hanging on a vehicle of the micro lane is
moved from where its snapshots' blend has it: its anchor as the vehicle's
drawn frame places it (render_interpolation_substep_frame), from where the
blend of the anchor at the two ticks has it. FALSE when the vehicle is not
drawn so */
static boolean render_interpolation_camera_vehicle_offset(
	struct interpolated_camera const *camera,
	real t,
	real_vector3d *offset)
{
	real_matrix4x3 const *frames[2];
	real_matrix4x3 const *frame;
	real_point3d previous_anchor, latest_anchor, line, drawn;

	if (!render_interpolation_substep_frame(camera->anchor_vehicle_index, frames, &frame))
		return FALSE;
	point_from_frame(frames[0], &camera->anchor, &previous_anchor);
	point_from_frame(frames[1], &camera->anchor, &latest_anchor);
	point_lerp(&previous_anchor, &latest_anchor, t, &line);
	point_from_frame(frame, &camera->anchor, &drawn);
	offset->i = drawn.x - line.x;
	offset->j = drawn.y - line.y;
	offset->k = drawn.z - line.z;
	/* (so written that an offset not a number is none) */
	return offset->i * offset->i + offset->j * offset->j + offset->k * offset->k <=
		SUBSTEP_OFFSET_MAXIMUM * SUBSTEP_OFFSET_MAXIMUM;
}

static struct observer_result const *render_interpolation_blended_camera(
	short local_player_index,
	struct observer_result const *observer)
{
	struct interpolated_camera *camera;
	real t = interpolation_fraction;
	long substep_unit_index;
	long substep_vehicle_index;
	real_vector3d offset;
	/* (the frame's moment, in ticks as interpolation_tick counts them) */
	double now = (double)interpolation_tick + (double)interpolation_fraction;
	boolean timed;

	if (!interpolation_rendering || !observer)
	{
		return observer;
	}
	camera = &interpolated_cameras[local_player_index];
	timed = render_interpolation_camera_timed(local_player_index);
	substep_unit_index = render_interpolation_camera_substep_unit(local_player_index);
	substep_vehicle_index = render_interpolation_camera_substep_vehicle(local_player_index);
	/* A scripted camera that moves with an object (a cinematic's camera
	point relative to a lifepod or dropship, or a unit's eyes in one) is
	placed by the object as it stands after the last tick. Blended from its
	own snapshots (taken at the first frame after each tick, a different
	part of a tick each tick), it did not follow the object as drawn, which
	is blended from the object's snapshots, or steps from tick to tick when
	its pose snaps: it ran up to 2 m ahead of or behind the object, out
	through the lifepod's hull in a30. It is drawn where the observer has
	it, moved with the object from where the object is to where it is
	drawn. (An animated camera has no such object; a scripted first-person
	camera looks along its unit's aim, which turns once a tick, and is
	blended from its snapshots as the player's camera is.) */
	if (director_get_perspective(local_player_index) == _director_perspective_scripted)
	{
		long relative_object_index = scripted_camera_object_relative_to();
		real_matrix4x3 const *drawn;

		if (relative_object_index != NONE &&
			!scripted_camera_object_is_first_person_camera(relative_object_index) &&
			object_try_and_get_and_verify_type(relative_object_index, _object_mask_all) &&
			(drawn = render_interpolation_object_node_matrices(relative_object_index)) != NULL)
		{
			/* (as of the last tick: object_get_node_matrices gives the drawn
			pose while a frame is drawn) */
			real_matrix4x3 const *nodes = (real_matrix4x3 const *)object_header_block_get(
				relative_object_index, &object_get(relative_object_index)->object.node_matrices);

			camera->blended = *observer;
			camera->blended.position.x += drawn[0].position.x - nodes[0].position.x;
			camera->blended.position.y += drawn[0].position.y - nodes[0].position.y;
			camera->blended.position.z += drawn[0].position.z - nodes[0].position.z;
			/* (the snapshots start again after) */
			camera->valid = FALSE;
			return &camera->blended;
		}
	}
	/* the observer as it stood after each tick (the first frame drawn
	after the tick) */
	if (!camera->valid || camera->tick != interpolation_tick)
	{
		long unit_index = player_control_get_unit_index(local_player_index);

		/* (its correction taken on each tick, render_interpolation_tick;
		the last unit's none of the next's) */
		if (!camera->valid || camera->unit_index != unit_index)
		{
			camera->correction = *global_zero_vector3d;
			camera->correction_pending = *global_zero_vector3d;
		}
		/* A respawn is a cut: the game's camera leaves the dead one for the
		new unit's eye at once (player_spawn: observer_obsolete_position),
		but the tick's blend drew it gliding from the dead camera into the
		head wherever they were nearer than a cut's distance and turn (the
		dead camera 2 to 6 units above the body, a spawn point near it; or
		network co-op's spectating, 3.5 units behind the teammate a dead
		player respawns beside) */
		camera->has_before = camera->has_previous;
		camera->has_previous = camera->valid && camera->unit_index == unit_index;
		camera->has_before = camera->has_before && camera->has_previous;
		camera->unit_index = unit_index;
		camera->before = camera->previous;
		camera->before_time = camera->previous_time;
		camera->previous = camera->latest;
		camera->previous_time = camera->latest_time;
		camera->latest = *observer;
		camera->latest_time = timed ? now : (double)interpolation_tick;
		camera->timed = timed;
		camera->cut_to_previous = camera->cut_to_latest;
		if (!camera->has_previous)
			camera->speed = 0.0f;
		camera->cut_to_latest = camera->has_previous && observer_timed_cut(
			camera->has_before && !camera->cut_to_previous ? &camera->before : NULL, camera->before_time,
			camera->speed, &camera->previous, camera->previous_time, &camera->latest, camera->latest_time);
		if (camera->has_previous && !camera->cut_to_latest && camera->latest_time > camera->previous_time)
		{
			camera->speed = (real)sqrt(distance_squared(&camera->previous.position, &camera->latest.position)) /
				(real)(camera->latest_time - camera->previous_time);
		}
		if (substep_unit_index != NONE)
			render_interpolation_camera_at_tick(substep_unit_index, &camera->latest);
		if (substep_vehicle_index != NONE)
			render_interpolation_camera_at_tick_in_vehicle(camera, &camera->latest);
		/* Several ticks since the last snapshot (a long frame): the objects
		are drawn between the last two ticks, so the camera's previous is
		where it was a tick ago, as nearly as a steady move from the last
		snapshot tells. (Blended across all the ticks since, the camera
		moved further each frame than the world it is in, out through a
		Pelican's hull for a frame.) A cut stays a cut. */
		if (!timed && camera->has_previous && interpolation_tick - camera->tick > 1 &&
			!observer_cut(&camera->previous, &camera->latest))
		{
			real ticks = (real)(interpolation_tick - camera->tick);
			struct observer_result previous = camera->previous;

			observer_blend(&previous, &camera->latest, (ticks - 1.0f) / ticks, &camera->previous);
		}
		camera->tick = interpolation_tick;
		camera->valid = TRUE;
	}
	if (!camera->has_previous ||
		(camera->timed ? camera->cut_to_latest : observer_cut(&camera->previous, &camera->latest)))
	{
		return observer;
	}

	if (camera->timed)
	{
		/* a tick before the frame's moment, between the two snapshots taken
		either side of it: the last two, or (the previous one taken later in
		its tick than the latest in its) the two before; a cut between those
		two drawn from the later */
		double drawn_time = now - 1.0;
		struct observer_result const *from = &camera->previous;
		struct observer_result const *to = &camera->latest;
		double from_time = camera->previous_time;
		double to_time = camera->latest_time;

		if (drawn_time < from_time && camera->has_before)
		{
			/* (after a cut, the shot cut to, carried back along its first
			move from the snapshot after the cut to the moment drawn, no
			further back than a tick, nor than the snapshot before the
			cut, after which the cut came: not held still at that
			snapshot until the moment drawn reaches it, a long frame's
			stall) */
			if (camera->cut_to_previous)
			{
				double back_to = drawn_time > camera->before_time ? drawn_time : camera->before_time;

				if (back_to < camera->previous_time - 1.0)
					back_to = camera->previous_time - 1.0;

				camera->blended = camera->previous;
				if (camera->latest_time > camera->previous_time)
				{
					observer_blend(&camera->previous, &camera->latest,
						(real)((back_to - camera->previous_time) / (camera->latest_time - camera->previous_time)),
						&camera->blended);
				}
				return &camera->blended;
			}
			from = &camera->before;
			to = &camera->previous;
			from_time = camera->before_time;
			to_time = camera->previous_time;
		}
		t = to_time > from_time ? (real)((drawn_time - from_time) / (to_time - from_time)) : 1.0f;
		/* (so written that a part not a number is none) */
		if (!(t > 0.0f))
			t = 0.0f;
		else if (t > 1.0f)
			t = 1.0f;
		observer_blend(from, to, t, &camera->blended);
	}
	else
	{
		observer_blend(&camera->previous, &camera->latest, t, &camera->blended);
	}
	if (correction_significant(&camera->correction) || correction_significant(&camera->correction_pending))
	{
		real_vector3d drawn;

		correction_drawn(&camera->correction, &camera->correction_pending, &drawn);
		camera->blended.position.x += drawn.i;
		camera->blended.position.y += drawn.j;
		camera->blended.position.z += drawn.k;
	}
	/* the micro lane's unit's view: moved onto its sub-steps' path */
	if (substep_unit_index != NONE && render_interpolation_substep_offset(substep_unit_index, FALSE, &offset))
	{
		camera->blended.position.x += offset.i;
		camera->blended.position.y += offset.j;
		camera->blended.position.z += offset.k;
	}
	/* ... and the view from a vehicle's seat, with the vehicle */
	if (substep_vehicle_index != NONE && render_interpolation_camera_vehicle_offset(camera, t, &offset))
	{
		camera->blended.position.x += offset.i;
		camera->blended.position.y += offset.j;
		camera->blended.position.z += offset.k;
	}
	return &camera->blended;
}

/* ---------- first-person weapon */

/* when the first-person weapons' animations last advanced
(first_person_weapons_update, in the tick's own work), in ticks of the
game's clock (render_interpolation_first_person_phase) */
static double first_person_updated_time;

/* (first_person_weapons.c, in the tick's own work: its first sub-step's,
which ends a sub-step into the tick) */
void render_interpolation_first_person_updated(void)
{
	first_person_updated_time = (double)game_time_get() + 1.0 / (double)sim_substep_count();
}

/* The player's own view drawn from the newest step
(render_interpolation_view_newest): how far, in ticks (0 to 1), the frame
is past the first-person animations' last advance. Their animations are
authored a frame a tick and advance a frame each tick, so the frame drawn
samples each where it is now, between its frame and the next
(first_person_weapons.c): the animation moves every frame at any rate, as
the clock does, none of it guessed or a tick behind. At 60 and 120 the
animations advance in the tick's first sub-step, not at its end, which the
blend between ticks below keyed them to: a jump each tick. -1 for any other
view, drawn between the ticks as before */
real render_interpolation_first_person_phase(short local_player_index)
{
	double phase;

	if (!interpolation_rendering || !render_interpolation_view_newest(local_player_index))
		return -1.0f;
	if (game_time_get_paused())
		return 0.0f;
	phase = (double)game_time_get() + (double)interpolation_fraction - first_person_updated_time;
	return phase < 0.0 ? 0.0f : phase > 1.0 ? 1.0f : (real)phase;
}

/* A Spartan link feed's first-person weapon (spartan_link.c; its slot's
animations are first_person_weapons.c's feeds', advanced with the players'):
sampled at the frame's time as the player's own view drawn from the newest
step is, so that it moves every frame however seldom the feed is drawn (one
feed a frame, in turn), which a blend between the poses drawn at the last
two ticks (render_interpolation_first_person) could not: a feed drawn every
other frame or less has none at the tick before. Its world is drawn between
the ticks, a tick behind the weapon, which is posed at the feed's camera
and so moves with nothing of the world's. -1 with the frames' interpolation
off, drawn by the tick as before */
real render_interpolation_first_person_feed_phase(void)
{
	double phase;

	if (!interpolation_rendering)
		return -1.0f;
	if (game_time_get_paused())
		return 0.0f;
	phase = (double)game_time_get() + (double)interpolation_fraction - first_person_updated_time;
	return phase < 0.0 ? 0.0f : phase > 1.0 ? 1.0f : (real)phase;
}

/* The phase (render_interpolation_first_person_phase) of the step running
now, at its end, where the frames after it go on from: a state's change made
in it (a shot of a sub-step, a message of the tick's own work) is blended
from the pose there (first_person_weapons.c). The tick's own work before its
fast phase ends with its first sub-step, as the first-person weapons'
update in it does: 1 before that update, 0 after it */
real render_interpolation_first_person_step_phase(void)
{
	real end = sim_substep_in_macro_step() ? 1.0f / (real)sim_substep_count() : sim_substep_end_fraction();
	double phase = (double)game_time_get() + (double)end - first_person_updated_time;

	return phase < 0.0 ? 0.0f : phase > 1.0 ? 1.0f : (real)phase;
}

/* The first-person weapon and hands are posed in world space from the drawn
camera each frame, from animation state that changes once a tick: blend the
pose relative to the camera. The player's own view drawn from the newest
step is posed as it is: its animations sampled at the frame's time
(render_interpolation_first_person_phase), from the camera of the frame. */
void render_interpolation_first_person(
	short local_player_index,
	real_matrix4x3 *node_matrices,
	short node_count,
	struct render_camera const *camera)
{
	struct interpolated_first_person *first_person;
	real_matrix4x3 camera_matrix;
	real_matrix4x3 inverse_camera;
	struct interpolation_rotation previous_rotations[MAXIMUM_INTERPOLATED_NODES];
	struct interpolation_rotation latest_rotations[MAXIMUM_INTERPOLATED_NODES];
	short node_index;

	if (!interpolation_rendering ||
		local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		node_count <= 0 || node_count > MAXIMUM_INTERPOLATED_NODES)
	{
		return;
	}
	first_person = &interpolated_first_person[local_player_index];
	matrix4x3_from_point_and_vectors(&camera_matrix, &camera->position, &camera->forward, &camera->up);
	matrix4x3_inverse(&camera_matrix, &inverse_camera);
	if (first_person->tick != interpolation_tick)
	{
		/* the pose drawn last, if that was at the end of the tick just
		before: not one left from before the view was last away from first
		person (zoomed, in a vehicle, dead, in a cinematic), seconds old */
		first_person->has_previous = first_person->tick == interpolation_tick - 1 &&
			first_person->node_count == node_count;
		memcpy(first_person->previous, first_person->latest, sizeof(first_person->previous));
		first_person->tick = interpolation_tick;
	}
	for (node_index = 0; node_index < node_count; node_index++)
		matrix4x3_multiply(&inverse_camera, &node_matrices[node_index], &first_person->latest[node_index]);
	first_person->node_count = node_count;
	if (!first_person->has_previous || render_interpolation_first_person_phase(local_player_index) >= 0.0f)
		return;
	/* a node that jumped further in the camera's frame than a tick allows:
	the last pose was another weapon's skeleton (of as many nodes), not this
	one moving (so written that a position not a number snaps) */
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_matrix4x3 const *previous = &first_person->previous[node_index];
		real_matrix4x3 const *latest = &first_person->latest[node_index];

		if (!(distance_squared(&previous->position, &latest->position) <=
			FIRST_PERSON_SNAP_DISTANCE * FIRST_PERSON_SNAP_DISTANCE))
		{
			return;
		}
		rotation_from_matrix(previous, &previous_rotations[node_index]);
		rotation_from_matrix(latest, &latest_rotations[node_index]);
	}
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_matrix4x3 blended;

		matrix_blend_rotations(
			&first_person->previous[node_index],
			&first_person->latest[node_index],
			&previous_rotations[node_index],
			&latest_rotations[node_index],
			interpolation_fraction,
			&blended);
		matrix4x3_multiply(&camera_matrix, &blended, &node_matrices[node_index]);
	}
}

/* ---------- particles */

/* An effect's particles are made in the tick for all of the tick's time
(effects.c), at once and at the emitter's place as the tick begins: a
stream from an object (a looping effect's: a vehicle's exhaust, a wreck's
smoke) came in clumps 30 times a second, from a moving object each a tick's
motion behind the last, which the object drawn between the ticks then moved
away from. Such a particle, the nth of a tick's count, is held back for that
part of the tick and made in the first frame drawn that far into the tick
after, where the emitter is drawn then: a stream, from the emitter as it is
seen. */

/* (effects.c, in the tick) the particle data (its position in the world)
from the object's node, made this part of the way into the tick's time:
TRUE as it is held back to be made then (particle_new), FALSE to be made
now (frames are not drawn between the ticks, the object is not drawn
between two poses, too many are held) */
boolean render_interpolation_particle_later(
	struct new_particle_data const *data,
	long object_index,
	short node_index,
	real fraction)
{
	struct interpolated_object *record = NULL;
	struct later_particle *later;
	long absolute_index;
	boolean possible;

	if (object_index == NONE || !(fraction > 0.0f && fraction < 1.0f))
		return FALSE;
	absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(object_index);
	if (interpolated_objects && absolute_index < MAXIMUM_INTERPOLATED_OBJECTS)
		record = &interpolated_objects[absolute_index];
	possible = halo_interpolation_enabled() && record && later_particle_count < MAXIMUM_LATER_PARTICLES &&
		record->object_index == object_index && record->has_previous && node_index >= 0 &&
		node_index < record->node_count;
	/* (whether it can wait is the frames' (its emitter's snapshots here),
	and a particle made now takes the tick's local seed: a demo's playback
	has the recording's choice, demo.c; one the recording held back that
	cannot be here is made now on the frames' seed, as the recording's
	frames made it) */
	if (!demo_decide_frames(possible))
		return FALSE;
	if (!possible)
	{
		boolean ticking = random_math_port_begin_cosmetic();

		particle_new(data);
		random_math_port_end_cosmetic(ticking);
		return TRUE;
	}
	later = &later_particles[later_particle_count++];
	later->data = *data;
	later->object_index = object_index;
	later->node_index = node_index;
	/* (the tick's node: object_get_node_matrix gives it in the tick) */
	matrix4x3_inverse_transform_point(object_get_node_matrix(object_index, node_index), &data->position,
		&later->node_position);
	/* (the tick's frames come after render_interpolation_tick) */
	later->tick = interpolation_tick + 1;
	later->fraction = fraction;
	return TRUE;
}

/* (particles.c, each frame before the particles move) the particles held
back that this frame has reached, made on their emitters as drawn */
void render_interpolation_particles_due(void)
{
	short index = 0;

	while (index < later_particle_count)
	{
		struct later_particle *later = &later_particles[index];

		if (interpolation_rendering && (interpolation_tick < later->tick ||
			(interpolation_tick == later->tick && interpolation_fraction < later->fraction)))
		{
			index++;
			continue;
		}
		if (object_try_and_get(later->object_index))
		{
			matrix4x3_transform_point(object_get_node_matrix(later->object_index, later->node_index),
				&later->node_position, &later->data.position);
		}
		particle_new(&later->data);
		*later = later_particles[--later_particle_count];
	}
}

/* ---------- time */

/* game time for animated shaders, continuous between ticks: the time of
the frame drawn (a tick behind the simulation, like the objects) */
real render_interpolation_game_time_sec(long ticks)
{
	real time;

	if (!interpolation_rendering)
		return (real)ticks * (1.0f / TICKS_PER_SECOND);
	time = ((real)ticks - 1.0f + interpolation_fraction) * (1.0f / TICKS_PER_SECOND);
	return time > 0.0f ? time : 0.0f;
}

/* how many ticks the frame drawn is behind the game's time: the part of a
tick still to run to the last tick's (as the objects, above), none when
frames are not drawn between ticks */
real render_interpolation_ticks_behind(void)
{
	return interpolation_rendering ? 1.0f - interpolation_fraction : 0.0f;
}

/* a timer that the frames count down by the ticks each ran (by
game_time_get_elapsed(), after reading it: the screen's flash, the camera's
impulse and shake), read before this frame's count as it stands at the time
of the frame drawn: what is left after this frame's ticks, and the part of a
tick the frame is behind them. When frames are not drawn between ticks, as
it is (before this frame's ticks), as the game read it. */
real render_interpolation_ticks_left(long ticks_left)
{
	if (!interpolation_rendering)
		return (real)ticks_left;
	return (real)(ticks_left - game_time_get_elapsed()) + 1.0f - interpolation_fraction;
}
