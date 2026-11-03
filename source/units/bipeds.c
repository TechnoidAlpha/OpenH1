/*
BIPEDS.C

symbols in this file:
0018FFD0 0010:
	_animation_left_foot_frame_index (0000)
0018FFE0 0010:
	_animation_right_foot_frame_index (0000)
0018FFF0 0010:
	_bipeds_initialize (0000)
00190000 0010:
	_bipeds_initialize_for_new_map (0000)
00190010 0010:
	_bipeds_dispose_from_old_map (0000)
00190020 0010:
	_bipeds_dispose (0000)
00190030 0030:
	_biped_place (0000)
00190060 0010:
	_biped_delete (0000)
00190070 0060:
	_biped_reset (0000)
001900D0 0030:
	_biped_disconnect_from_structure_bsp (0000)
00190100 00c0:
	_biped_get_physics_pill (0000)
001901C0 0020:
	_biped_stop_melee_attack (0000)
001901E0 0080:
	_biped_start_limp_body_physics (0000)
00190260 0050:
	_biped_stop_limp_body_physics (0000)
001902B0 00f0:
	_code_001902b0 (0000)
001903A0 00b0:
	_code_001903a0 (0000)
00190450 01d0:
	_code_00190450 (0000)
00190620 0050:
	_biped_flying_through_air (0000)
00190670 0110:
	_code_00190670 (0000)
00190780 0110:
	_code_00190780 (0000)
00190890 0060:
	_biped_adjust_placement (0000)
001908F0 00c0:
	_biped_export_function_values (0000)
001909B0 01a0:
	_biped_get_sight_position (0000)
00190B50 0150:
	_biped_get_autoaim_pill (0000)
00190CA0 04d0:
	_biped_fix_position (0000)
00191170 0110:
	_biped_render_debug (0000)
00191280 0180:
	_biped_find_ground_surface (0000)
00191400 0030:
	_biped_approximate_surface_index (0000)
00191430 01c0:
	_biped_find_pathfinding_surface_index (0000)
001915F0 00f0:
	_biped_exit_seat_end (0000)
001916E0 0140:
	_code_001916e0 (0000)
00191820 01b0:
	_code_00191820 (0000)
001919D0 0130:
	_code_001919d0 (0000)
00191B00 01b0:
	_code_00191b00 (0000)
00191CB0 01a0:
	_code_00191cb0 (0000)
00191E50 0220:
	_biped_find_nearby_support_surface (0000)
00192070 0100:
	_biped_verify_object_vectors (0000)
00192170 0160:
	_code_00192170 (0000)
001922D0 00b0:
	_code_001922d0 (0000)
00192380 0080:
	_code_00192380 (0000)
00192400 0200:
	_code_00192400 (0000)
00192600 01b0:
	_biped_build_flying_axes (0000)
001927B0 1500:
	_code_001927b0 (0000)
00193CB0 0550:
	_biped_snap_facing (0000)
00194200 00c0:
	_biped_new (0000)
001942C0 0020:
	_biped_preprocess_node_orientations (0000)
001942E0 01e0:
	_biped_accelerate (0000)
001944C0 06b0:
	_code_001944c0 (0000)
00194B70 0ff0:
	_code_00194b70 (0000)
00195B60 00d0:
	_code_00195b60 (0000)
00195C30 0460:
	_biped_update (0000)
002A30B0 0144:
	_rdata_002a30b0 (0000)
002A31F4 000d:
	??_C@_0N@HOJCMADM@biped_update?$AA@ (0000)
002A3208 0043:
	??_C@_0ED@IAGIKAMA@WARNING?3?5biped?5?$CFs?5?$CI?$CFs?$CJ?5is?5in?5a?5b@ (0000)
002A324C 0004:
	__real@c4fa0000 (0000)
002A3250 0039:
	??_C@_0DJ@HLOPCOPC@WARNING?3?5biped?5?$CFs?5?$CI?$CFs?$CJ?5fell?5outs@ (0000)
002A328C 001e:
	??_C@_0BO@EMCOEGJH@c?3?2halo?2SOURCE?2units?2bipeds?4c?$AA@ (0000)
002A32AC 0025:
	??_C@_0CF@NNLMJPLM@desired_facing?5?$CG?$CG?5desired_gun_of@ (0000)
002A32D8 004b:
	??_C@_0EL@JGLDGCHJ@?$CIestimate_mode?5?$CB?$DN?5_unit_estimate@ (0000)
002A3328 004c:
	??_C@_0EM@DCHFFGOM@?$CIestimate_mode?5?$DN?$DN?5_unit_estimate@ (0000)
002A3374 0023:
	??_C@_0CD@INFHIELC@fixed_location?4cluster_index?$CB?$DNNO@ (0000)
002A3398 0021:
	??_C@_0CB@NBHLJDN@final_position?5?$HM?$HM?5?$CBdont_teleport@ (0000)
002A33BC 0012:
	??_C@_0BC@BGKMEKLE@pathfinding_point?$AA@ (0000)
002A33D0 003f:
	??_C@_0DP@JMJFKKMO@couldn?8t?5teleport?5the?5biped?5out?5@ (0000)
002A3410 0004:
	__real@44e10000 (0000)
002A3414 0004:
	__real@42700000 (0000)
002A3418 0042:
	??_C@_0EC@JBODJGAG@biped?5?$CFs?5?$CI?$CFs?$CFs?$CFs?$CJ?3?5?$CFs?5vector?5fai@ (0000)
002A345C 0006:
	??_C@_05MNHFFADL@climb?$AA@ (0000)
002A3464 000f:
	??_C@_0P@MMKEDIOE@player?9physics?$AA@ (0000)
002A3474 0006:
	??_C@_05JAAJDALH@?1dead?$AA@ (0000)
002A347C 0009:
	??_C@_08LOLHEFGJ@?1limping?$AA@ (0000)
002A3488 000e:
	??_C@_0O@HAMEHAGE@post?9airborne?$AA@ (0000)
002A3498 0033:
	??_C@_0DD@FLLKBPKC@TEST_FLAG?$CIbiped?9?$DObiped?4flags?0?5_b@ (0000)
002A34CC 000d:
	??_C@_0N@CBHHKMKF@post?9landing?$AA@ (0000)
002A34DC 000e:
	??_C@_0O@MGELMNBM@post?9slipping?$AA@ (0000)
002A34EC 0004:
	__real@3f36db6e (0000)
002A34F0 0053:
	??_C@_0FD@MEMEKANL@?$CFs?0?5?$CFs?0?5?$CFs?3?5assert_valid_real_ve@ (0000)
002A3544 000f:
	??_C@_0P@EFHIAMDK@forward_vector?$AA@ (0000)
002A3554 000c:
	??_C@_0M@MGINPDCA@left_vector?$AA@ (0000)
002A3560 000a:
	??_C@_09MDHMCNCN@up_vector?$AA@ (0000)
002A356C 002b:
	??_C@_0CL@JEOBBAMK@forward_vector?5?$CG?$CG?5left_vector?5?$CG?$CG@ (0000)
002A3598 0017:
	??_C@_0BH@LOKJEHJA@?$CGphysics?9?$DOnew_velocity?$AA@ (0000)
002A35B0 0017:
	??_C@_0BH@NMIMKOHN@?$CGphysics?9?$DOnew_position?$AA@ (0000)
002A35C8 0004:
	__real@322bcc76 (0000)
002A35CC 0004:
	__real@3d5a740e (0000)
002A35D0 0013:
	??_C@_0BD@LFOFBHEC@?$CGphysics?9?$DOvelocity?$AA@ (0000)
002A35E4 0013:
	??_C@_0BD@NHMAPOKP@?$CGphysics?9?$DOposition?$AA@ (0000)
002A35F8 0017:
	??_C@_0BH@OMEJIIDE@post?9normal?9snapfacing?$AA@ (0000)
002A3610 001a:
	??_C@_0BK@LJFJPFOM@post?9deadplane?9snapfacing?$AA@ (0000)
002A362C 0019:
	??_C@_0BJ@LPEPIIHF@pre?9deadplane?9snapfacing?$AA@ (0000)
002A3648 0016:
	??_C@_0BG@BILNKKA@post?9climb?9snapfacing?$AA@ (0000)
002A3660 0015:
	??_C@_0BF@BJAPGJCB@post?9bank?9snapfacing?$AA@ (0000)
002A3678 0017:
	??_C@_0BH@HMDDCPPK@?$CGbiped?9?$DOobject?4forward?$AA@ (0000)
002A3690 0011:
	??_C@_0BB@HNGPOHPO@preprocess?9nodes?$AA@ (0000)
002A36A4 000b:
	??_C@_0L@LGBFNPNE@post?9accel?$AA@ (0000)
002A36B0 000a:
	??_C@_09LBPFKLCO@pre?9accel?$AA@ (0000)
002A36BC 0011:
	??_C@_0BB@EHADHKAI@post?9moving?9turn?$AA@ (0000)
002A36D0 0013:
	??_C@_0BD@LNBPGJPJ@post?9standing?9turn?$AA@ (0000)
002A36E4 000e:
	??_C@_0O@NMGDOCHP@post?9fly?9turn?$AA@ (0000)
002A36F4 0004:
	__real@35b5b61c (0000)
002A36F8 0004:
	__real@3bef2eb7 (0000)
002A36FC 000b:
	??_C@_0L@EMJEBHAK@post?9dying?$AA@ (0000)
002A3708 0014:
	??_C@_0BE@OBCJD@post?9dying?9airborne?$AA@ (0000)
002A371C 0011:
	??_C@_0BB@JBPNJIHD@post?9limp?9noodle?$AA@ (0000)
002A3730 000c:
	??_C@_0M@KACGONJH@post?9update?$AA@ (0000)
002A373C 0018:
	??_C@_0BI@KEKHPNBM@post?9dead?1air?1land?1slip?$AA@ (0000)
002A3754 000c:
	??_C@_0M@PKIECHFJ@post?9moving?$AA@ (0000)
002A3760 000d:
	??_C@_0N@CDBMOEKH@post?9turning?$AA@ (0000)
002A3770 000c:
	??_C@_0M@JKCHFPFF@pre?9turning?$AA@ (0000)
002A377C 000b:
	??_C@_0L@PNEJBGOB@pre?9update?$AA@ (0000)
00314F50 0600:
	_rider_ejection (0000)
004CCB10 0004:
	_debug_biped_physics (0000)
	_debug_biped_skip_update (0001)
	_debug_biped_skip_collision (0002)
	_debug_biped_limp_body_disable (0003)
*/

/* ---------- headers */

#define REAL_MATH_EXTERNAL_REAL_RANDOM_RANGE
#include "cseries.h"
#include "ai/ai_communication.h"
#include "ai/ai_debug.h"
#include "ai/ai.h"
#include "ai/actors.h"
#include "cseries/errors.h"
#include "cseries/profile.h"
#include "cutscene/cinematics.h"
#include "cutscene/recorded_animations.h"
#include "devices/device_definitions.h"
#include "devices/devices.h"
#include "effects/material_effects.h"
#include "game/cheats.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/game_globals.h"
#include "game/player_rumble.h"
#include "game/players.h"
#include "interface/first_person_weapons.h"
#include "items/weapons.h"
#include "models/model_animation_definitions.h"
#include "models/model_definitions.h"
#include "objects/damage.h"
#include "objects/object_types.h"
#include "units/biped_limp_noodle.h"
#include "units/biped_definitions.h"
#include "units/bipeds.h"
#include "units/vehicle_definitions.h"
#include "units/vehicles.h"
#include "physics/breakable_surfaces.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collision_debug.h"
#include "physics/collision_features.h"
#include "physics/collision_models.h"
#include "physics/collisions.h"
#include "physics/collision_usage.h"
#include "physics/physics.h"
#include "render/render_debug.h"
#include "scenario/scenario.h"
#include "structures/structure_bsp_definitions.h"
#include "network_coop.h" /* port: port/linux/game/network_coop.c */
#include "sim_rate.h" /* port: port/linux/game/sim_rate.c */

/* port: a lift's carry of an AI's biped this tick, a share a sub-step
(devices/device_machines.c; its header's scenario structures are this
file's own too) */
boolean machine_elevator_carried(long biped_index, real_vector3d *offset);
void machine_elevator_carried_forget(void);

/* port: port/linux/game/network_objects.c's (a client deletes the host's
objects on the host's word alone) */
boolean network_objects_may_delete(long object_index);

/* port: an unarmed player's melee's length, in ticks (a weapon's is about
this: its first person melee animation, sped up a quarter) */
#define UNARMED_MELEE_TICKS 16

/* ---------- constants */

enum
{
	_material_effect_biped_walk = 0,
	_material_effect_biped_run,
	_material_effect_biped_sliding,
	_material_effect_biped_shuffle,
	_material_effect_biped_jump,
	_material_effect_biped_jump_land,
};

enum
{
	_unit_animation_action_melee = 7,
};

enum
{
	biped_state_idle = 0,
	biped_state_moving,
	biped_state_unknown,
	NUMBER_OF_BIPED_STATES,
};

enum
{
	_biped_physics_in_airborne_bit = 0,
	_biped_physics_in_slipping_bit,
	_biped_physics_in_crouched_bit,
	_biped_physics_in_trying_to_stand_bit,
	_biped_physics_in_flying_bit,
	_biped_physics_in_absolute_movement_bit,
	_biped_physics_in_no_collision_bit,
	_biped_physics_in_dead_bit,
	_biped_physics_in_pass_through_bipeds_bit,
	_biped_physics_in_climb_anything_bit,
	/* port: a player's, in a co-op game without player collisions: the other
	players' bipeds are not in its way (network_coop_player_collisions) */
	_biped_physics_in_pass_through_players_bit,
	/* port: a sub-step on the ground its tick's step met or landed on
	(biped_substep_move), past the one that met it: the tick's push into
	it given already, and no gap left to go down */
	_biped_physics_in_pushed_bit,
};

enum
{
	_biped_physics_out_airborne_bit = 0,
	_biped_physics_out_slipping_bit,
	_biped_physics_out_cannot_stand_bit,
	_biped_physics_out_splatter_bit,
	_biped_physics_out_volatile_collision_bit,
};

enum
{
	_unit_melee_attack_none = 0,
	_unit_melee_attack_starting,
	_unit_melee_attack_dangerous,
	_unit_melee_attack_impact,
	_unit_melee_attack_continuous,
};

enum
{
	_animation_frame_info_none = 0,
	_animation_frame_info_xy_translation,
	_animation_frame_info_xy_translation_yaw_rotation,
	_animation_frame_info_xyz_translation_yaw_rotation,
	NUMBER_OF_ANIMATION_FRAME_INFO_TYPES,
};

enum
{
	_collision_surface_two_sided_bit = 0,
	_collision_surface_invisible_bit,
	_collision_surface_climbable_bit,
	_collision_surface_breakable_bit,
};

/* ---------- macros */

#define BIPED_CLIMBING_SNAP_ANGLE ((real)(10.0*M_PI/180.0))
#define MINIMUM_SLIPPING_FOOTSTEP_VELOCITY_SQUARED (1.f/900.f)

/* ---------- structures */

struct biped_contact_point
{
	byte unused[32];
	char marker_name[32];
};

struct game_globals_falling_damage
{
	long falling_unused[2];
	real falling_distance_lower_bound;
	real falling_distance_upper_bound;
	struct tag_reference falling_damage;
	long terminal_velocity_unused[2];
	real maximum_distance;
	struct tag_reference maximum_distance_damage;
	struct tag_reference vehicle_hit_environment_damage_effect;
	struct tag_reference vehicle_killed_unit_damage_effect;
	struct tag_reference vehicle_collision_damage;
	struct tag_reference flaming_death_damage;
	long unused7c[4];
	real runtime_maximum_falling_velocity;
	real runtime_minimum_damage_velocity;
	real runtime_maximum_damage_velocity;
};

struct biped_physics
{
	long biped_index;
	word in_flags;
	word pad6;
	real_point3d position;
	real_vector3d forward;
	real_vector3d aiming;
	real_vector3d velocity;
	real crouch_velocity;
	real_vector3d movement_desired;
	real movement_penalty;
	real acceleration_maximum;
	real airborne_acceleration_maximum;
	real height;
	real width;
	real ground_tangential_velocity_max;
	real ground_tangential_angle;
	real minimum_normal_k;
	real downhill_k0;
	real downhill_k1;
	real downhill_velocity_scale;
	real uphill_k0;
	real uphill_k1;
	real uphill_velocity_scale;
	real_plane3d ground_plane;
	long existing_support_surface_index;
	real gravity;
	long bumped_object_index;
	long elevator_object_index;
	word out_flags;
	word padA2;
	long support_surface_index;
	long stick_surface_index;
	real_point3d new_position;
	real_vector3d new_velocity;
	real landing_velocity;
	real collision_velocity;
};

struct animation_frame_info_dx_dy
{
	real dx;
	real dy;
};

struct animation_frame_info_dx_dy_dyaw
{
	real dx;
	real dy;
	real dyaw;
};

struct animation_frame_info_dx_dy_dz_dyaw
{
	real dx;
	real dy;
	real dz;
	real dyaw;
};

struct vehicle_runtime_datum
{
	long definition_index;
	struct _object_datum object;
	struct _unit_datum unit;
	struct
	{
		word flags;
		short reserved;
		byte airborne_ticks;
	} vehicle;
};

struct scenario_object_datum
{
	short palette_entry_index;
	short name_index;
	word placement_flags;
	short variant_number;
	real_point3d position;
	real_euler_angles3d rotation;
	word on_bsp_flags;
	word misc_flags;
	unsigned long unused;
};

struct scenario_object_permutation
{
	unsigned long change_colors[4];
	byte region_permutations[8];
	unsigned long unused[2];
};

struct scenario_unit_datum
{
	real body_vitality;
	unsigned long flags;
};

struct scenario_biped_datum
{
	struct scenario_object_datum object;
	struct scenario_object_permutation permutation;
	struct scenario_unit_datum unit;
};

/* ---------- prototypes */

static void biped_make_footstep(
	long biped_index,
	short event_index,
	short contact_point_index);
static void biped_bumped_object(
	long biped_index,
	long object_index,
	real_vector3d const *old_velocity);
static void biped_falling_damage(
	long biped_index,
	real collision_velocity);
static void biped_start_landing(
	long biped_index,
	real landing_velocity);
static void biped_update_jumping(
	long biped_index,
	struct unit_animation_update_data *animation);
static void biped_update_physics(
	struct biped_physics *physics,
	real tick_fraction,
	real gravity,
	real_vector3d const *tick_velocity,
	real_plane3d const *tick_ground);
/* units.c's: whether the unit gets the animation enhancements, and whether
a player biped turns with the aim where the original did not (port) */
boolean unit_animation_enhanced(long unit_index);
boolean unit_turns_with_aim(long unit_index);
/* player_control.c's: the controls of a player's unit that went down in the
running sub-step past a tick's first */
unsigned long player_control_substep_pressed_flags(long unit_index);
static void biped_snap_facing(
	long biped_index);
static void biped_physics_apply_step(
	long biped_index,
	struct biped_physics *physics);
static void biped_physics_apply_tick(
	long biped_index,
	struct biped_physics *physics,
	struct unit_animation_update_data *animation);
static void biped_update_on_foot(
	long biped_index,
	struct unit_animation_update_data *animation);
static void biped_update_end(
	long biped_index,
	struct unit_animation_update_data *animation);
static void bipeds_substep(
	short substep_index,
	short substep_count,
	real seconds);

/* ---------- globals */

/* port: the fast phase (port/linux/game/sim_rate.h). At game.sim_rate 60 or
120 a player's biped moves in the tick's sub-steps (bipeds_substep, the
fast phase's _sim_substep_bipeds slot) rather than in the objects' update:
biped_update makes the tick's movement ready (its input, speeds and
accelerations, as a tick at 30 does) and leaves it here, and each sub-step
moves the biped its share of the tick. What a tick's movement leads to (a
landing, a jump, a melee's strike, a bump, falling damage, the animation,
the counters of ticks) follows the last sub-step, once a tick, in the order
biped_update has it; but a jump whose press only a later sub-step reads
(player_control.c's) launches at the end of that sub-step, as a tick
launches one at its end (biped_substep_jump). The tick's change of its
crouch (biped_physics_prepare's, once a tick from the seat the tick before
left it in) is made a share a sub-step, as its movement is: the crouch
starts the tick where the tick before left it and each sub-step ends its
share of the way, the last where a tick at 30 ends, so the eye (the
first-person view) goes down and up in the sub-steps, not a tick's step at
the first of them. Nothing here outlives the tick: it is emptied at the
last sub-step, and an entry of another tick is ignored. Every other biped
(a rider, a body: the macro lane, objects.h) moves in its tick, as at 30. A lift carries its riders of the micro lane a
share each sub-step before they move (objects.c's slot), and they stand on
what the sub-step sees of the macro lane.

The AI's bipeds (an actor's or a swarm's) are of the micro lane too, but
their brains are the tick's: the AI decides in ai_update, before the
objects' update, and what follows a tick's movement (a landing, the
animation and the permutation it picks by the game's random seed, the
footsteps, a jump) is decided in the objects' update, in the order of the
objects, as at 30, where the random seed is drawn in that order. So such a
biped moves its tick's sub-steps in its own update, one after the other
(biped_substep_inline: each step as bipeds_substep's, toward the throttle,
the facing and the root motion the AI chose for the tick, with gravity, a
jump's or a fall's launch bias, on the ground and against what it meets),
and the rest of its update follows as at 30. Where each sub-step left it is
kept (biped_replay_globals), and the fast phase puts it there at each
sub-step (bipeds_substep, before the players' bipeds move): it is drawn on
that path (render_interpolation.c), and what moves in the sub-steps (a
projectile, a player) meets it there. The tick's last sub-step leaves it
where its update did, and anything a sub-step moved it by besides goes with
it. On a lift its sub-steps see the lift that far into its movement
(objects.c's object_lane_view_substep); a lift that carries it, by the
tick's whole movement in the lift's update, is taken off where its
sub-steps are by the share still to come (device_machines.c's
machine_elevator_carried), so it rides a share a sub-step either way. */
enum
{
	MAXIMUM_SUBSTEP_BIPEDS = 16,
	/* the AI's bipeds whose sub-steps a tick keeps, at most (the array grows
	to the most a tick has had) */
	MAXIMUM_REPLAYED_BIPEDS = MAXIMUM_OBJECTS_PER_MAP
};

/* a player's speeds this tick (player physics), for a throttle back (0) and
forward (1): biped_player_movement */
struct biped_player_speeds
{
	boolean valid;
	boolean double_speed;
	real stun_scale;
	real forward_speed[2];
	real sideways_speed[2];
	real double_speed_multiplier;
};

struct biped_substep_datum
{
	long biped_index;
	/* the tick (game_time) it was made ready in */
	long tick;
	/* the tick's movement, as biped_physics_prepare made it, each of its
	quantities per tick */
	struct biped_physics physics;
	/* the object's velocity as biped_physics_prepare left it (an
	animation's root motion it set: biped_substep_start_velocity) */
	real_vector3d prepared_velocity;
	struct unit_animation_update_data animation;
	/* its speeds, by which each sub-step's throttle is its movement */
	struct biped_player_speeds speeds;
	/* the pill at the first sub-step's start: what the melee strike sweeps
	from, once a tick, as a tick at 30 does */
	real_point3d tick_position;
	/* the crouch at the tick's start and as biped_physics_prepare changed it
	for the tick: each sub-step ends its share of the way, the last at the
	tick's (bipeds_substep), so the eye goes down a share a sub-step */
	real crouch_start;
	real crouch_end;
	/* of the sub-steps so far: the fastest landing, an elevator stood on,
	the last object bumped and the velocity it was bumped at */
	real landing_velocity;
	long elevator_object_index;
	long bumped_object_index;
	real_vector3d bumped_velocity;
	/* a jump launched at the end of one of the sub-steps before the last
	(biped_substep_jump), and the velocity it launched at, from which the
	rest of the tick's sub-steps work out their change of velocity */
	boolean jumped;
	real_vector3d launch_velocity;
	/* the ground at the tick's end as the tick's own step finds it, from
	the first sub-step's start (biped_substep_move): whether the sub-steps
	probe it themselves, stand on what the tick stands on, or leave it at
	the tick's start; the plane and surface the tick stands on, and the
	velocity it leaves at */
	short ground;
	real_plane3d ground_plane;
	long support_surface_index;
	real_vector3d leave_velocity;
};

/* biped_substep_datum's ground */
enum
{
	_biped_substep_ground_probed,
	_biped_substep_ground_tick,
	_biped_substep_ground_left,
	_biped_substep_ground_landed
};

static struct
{
	short count;
	struct biped_substep_datum bipeds[MAXIMUM_SUBSTEP_BIPEDS];
} biped_substep_globals;

/* an AI's biped's sub-steps, moved in its update (biped_substep_inline) */
struct biped_replay_datum
{
	long biped_index;
	/* the tick (game_time) it moved in */
	long tick;
	short substep_count;
	/* where each sub-step left it (the last: where its update did) */
	real_point3d poses[MAXIMUM_SIM_SUBSTEPS];
	/* where the fast phase put it last (its update's end, before the first
	sub-step), and what moved it since besides (a lift's shares) */
	real_point3d last;
	real_vector3d carried;
};

static struct
{
	long count;
	long capacity;
	struct biped_replay_datum *bipeds;
} biped_replay_globals;

boolean debug_biped_physics = FALSE;
boolean debug_biped_skip_update = FALSE;
boolean debug_biped_skip_collision = FALSE;
boolean debug_biped_limp_body_disable = FALSE;
boolean rider_ejection = TRUE;

static struct profile_section biped_update_section = {"biped_update", NONE, TRUE};

/* port: coop_scripts.c sets this around one teleport */
static boolean biped_fix_ignores_players;

void biped_fix_ignore_players(
	boolean ignore)
{
	biped_fix_ignores_players = ignore;

	return;
}

extern boolean debug_objects_biped_autoaim_pills;
extern boolean debug_objects_biped_physics_pills;

static real_vector3d const fudge_vectors[27] =
{
	{ { 0.f, 0.f, 0.f } },
	{ { 1.f, 0.f, 0.f } },
	{ { 0.f, 0.f, 1.f } },
	{ { 0.70710677f, 0.f, 0.70710677f } },
	{ { 0.57735026f, 0.57735026f, 0.57735026f } },
	{ { 0.57735026f, -0.57735026f, 0.57735026f } },
	{ { 0.70710677f, 0.70710677f, 0.f } },
	{ { 0.70710677f, -0.70710677f, 0.f } },
	{ { 0.f, 0.70710677f, 0.70710677f } },
	{ { 0.f, -0.70710677f, 0.70710677f } },
	{ { -1.f, 0.f, 0.f } },
	{ { 0.f, 1.f, 0.f } },
	{ { 0.f, -1.f, 0.f } },
	{ { -0.70710677f, -0.70710677f, 0.f } },
	{ { -0.70710677f, 0.70710677f, 0.f } },
	{ { -0.70710677f, 0.f, 0.70710677f } },
	{ { -0.57735026f, -0.57735026f, 0.57735026f } },
	{ { -0.57735026f, 0.57735026f, 0.57735026f } },
	{ { 0.f, 0.f, -1.f } },
	{ { 0.70710677f, 0.f, -0.70710677f } },
	{ { -0.70710677f, 0.f, -0.70710677f } },
	{ { 0.f, 0.70710677f, -0.70710677f } },
	{ { 0.f, -0.70710677f, -0.70710677f } },
	{ { 0.57735026f, 0.57735026f, -0.57735026f } },
	{ { 0.57735026f, -0.57735026f, -0.57735026f } },
	{ { -0.57735026f, -0.57735026f, -0.57735026f } },
	{ { -0.57735026f, 0.57735026f, -0.57735026f } },
};

/* ---------- public code */

char animation_left_foot_frame_index(
	struct animation const *animation)
{
	return animation->private_left_foot_frame_index;
}

char animation_right_foot_frame_index(
	struct animation const *animation)
{
	return animation->private_right_foot_frame_index;
}

void bipeds_initialize(
	void)
{
	/* port: the players' bipeds' movement in the fast phase (sim_rate.h),
	the micro lane's (objects.h) */
	sim_substep_register(_sim_substep_bipeds, bipeds_substep);
	object_lane_register(_object_mask_biped);

	return;
}
void bipeds_initialize_for_new_map(
	void)
{
	return;
}

void bipeds_dispose_from_old_map(
	void)
{
	return;
}

void bipeds_dispose(
	void)
{
	return;
}

void biped_place(
	long biped_index,
	struct scenario_object_datum *scenario_object)
{
	struct scenario_biped_datum *scenario_biped = (struct scenario_biped_datum *)scenario_object;

	unit_place(biped_index, &scenario_biped->unit);
	object_add_scenario_permutation(biped_index, &scenario_biped->permutation);

	return;
}

void biped_delete(
	long biped_index)
{
	return;
}

void biped_reset(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	csmemset(&biped->biped, 0, sizeof(biped->biped));
	biped->biped.ground_plane = depths_of_hell;
	biped->biped.last_falling_communication_time = NONE;

	return;
}

void biped_disconnect_from_structure_bsp(
	long biped_index)
{
	struct biped_datum *biped;

	biped = biped_get(biped_index);
	biped->biped.support_surface_index = NONE;
	biped->biped.pathfinding_surface_index = NONE;
	biped->biped.last_pathfinding_surface_index = NONE;

	return;
}

void biped_get_physics_pill(
	long biped_index,
	real_point3d *base,
	real *height,
	real *width)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	object_get_origin(biped_index, base);
	if (!TEST_FLAG(definition->biped.flags, _biped_pill_centered_at_origin_bit))
		base->z += definition->biped.collision_radius;

	if (!TEST_FLAG(definition->biped.flags, _biped_spherical_bit) &&
		(biped->unit.player_index!=NONE ||
		TEST_FLAG(biped->object.flags, _object_movie_star_bit)))
	{
		*height = definition->biped.collision_height_standing +
			(definition->biped.collision_height_crouching -
			definition->biped.collision_height_standing)*biped->biped.crouch -
			2.f*definition->biped.collision_radius;
	}
	else
	{
		*height = 0.f;
	}
	*width = definition->biped.collision_radius;

	return;
}

void biped_stop_melee_attack(
	long biped_index)
{
	struct biped_datum *biped;

	biped = biped_try_and_get(biped_index);
	if (biped)
		biped->biped.player_melee_ticks = 0;

	return;
}

void biped_start_limp_body_physics(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	boolean uses_limp_body_physics = TEST_FLAG(
		biped_definition_get(biped->definition_index)->biped.flags,
		_biped_uses_limp_body_physics_bit);

	if (!debug_biped_limp_body_disable &&
		uses_limp_body_physics &&
		TEST_FLAG(biped->object.flags, _object_at_rest_bit) &&
		!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		!TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
	{
		biped->biped.limp_body_current_relaxation_iterations = 0;
		biped->biped.limp_body_max_relaxation_iterations =
			(byte)biped_limp_noodle_get_max_relaxation_iterations(biped_index);
		SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, TRUE);
		SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, TRUE);
	}

	return;
}

void biped_stop_limp_body_physics(
	long biped_index)
{
	struct biped_datum *biped;
	struct biped_definition *definition;

	biped = biped_get(biped_index);
	definition = biped_definition_get(biped->definition_index);
	if (TEST_FLAG(definition->biped.flags, _biped_uses_limp_body_physics_bit) &&
		TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit))
	{
		SET_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit, FALSE);
		SET_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit, FALSE);
	}

	return;
}

static void biped_bumped_object(
	long biped_index,
	long object_index,
	real_vector3d const *old_velocity)
{
	struct biped_datum *biped = biped_get(biped_index);
	char bump_ticks;

	biped_definition_get(biped->definition_index);
	bump_ticks = biped->biped.bump_ticks;
	if (bump_ticks < 0)
	{
		if (object_index == NONE)
			biped->biped.bump_ticks = bump_ticks + 1;
		else
			biped->biped.bump_ticks = -15;
	}
	else if (object_index != NONE)
	{
		struct object_datum *object = object_get(object_index);

		ai_handle_bump(biped_index, object_index, old_velocity);
		if (biped->unit.player_index != NONE ||
			recorded_animation_controlling_unit(biped_index))
		{
			if (biped->biped.bump_object_index != object_index)
			{
				biped->biped.bump_object_index = object_index;
				biped->biped.bump_ticks = 0;
			}
			else
			{
				biped->biped.bump_ticks++;
				if (biped->biped.bump_ticks > 3)
				{
					if (object->object.type == _object_type_biped && cheat.bump_possession)
					{
						short local_player_index = unit_get_local_player_index(biped_index);

						if (local_player_index != NONE)
						{
							((struct biped_datum *)object)->biped.bump_ticks = -15;
							players_set_local_player_unit(local_player_index, object_index);
						}
					}
					biped->biped.bump_ticks = -15;
				}
			}
		}
	}

	return;
}

static void biped_falling_damage(
	long biped_index,
	real collision_velocity)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct game_globals_falling_damage *falling_damage = TAG_BLOCK_GET_ELEMENT(
		&scenario_get_game_globals()->falling_damage,
		0,
		struct game_globals_falling_damage);
	boolean immune =
		TEST_FLAG(biped->unit.flags, _unit_no_falling_damage_bit) ||
		TEST_FLAG(definition->biped.flags, _biped_immune_to_falling_damage_bit);
	real falling_velocity = biped->object.translational_velocity.k;

	if (!cheat.jetpack || biped->unit.player_index == NONE)
	{
		if (collision_velocity > falling_damage->runtime_minimum_damage_velocity)
		{
			if (!immune)
			{
				struct damage_data damage;

				damage_data_new(&damage, falling_damage->falling_damage.index);
				damage.scale =
					(collision_velocity - falling_damage->runtime_minimum_damage_velocity) /
					(falling_damage->runtime_maximum_damage_velocity -
						falling_damage->runtime_minimum_damage_velocity);
				damage.scale = PIN(
					damage.scale,
					0.f,
					1.f);
				object_cause_damage(&damage, biped_index, NONE, NONE, NONE, NULL);
			}
		}
		else if (!TEST_FLAG(definition->biped.flags, _biped_flying_bit) &&
			falling_velocity <
				-falling_damage->runtime_maximum_falling_velocity)
		{
			if (!immune && !TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
			{
				struct damage_data damage;

				damage_data_new(&damage, falling_damage->maximum_distance_damage.index);
				object_cause_damage(&damage, biped_index, NONE, NONE, NONE, NULL);
			}

			/* (port: not the host's biped on a client, which the host erases:
			the delete was refused, and logged as done) */
			if (!game_engine_running() &&
				TEST_FLAG(biped->object.flags, _object_outside_of_map_bit) &&
				player_index_from_unit_index(biped_index) == NONE &&
				network_objects_may_delete(biped_index))
			{
				long actor_index = biped->unit.swarm_actor_index;

				if (actor_index == NONE)
					actor_index = biped->unit.actor_index;
				error(
					_error_silent,
					"WARNING: biped %s (%s) fell outside world and was erased",
					tag_name_strip_path(tag_get_name(biped->definition_index)),
					ai_debug_describe_actor(
						actor_index,
						biped_index,
						TRUE,
						temporary,
						256));
				object_delete(biped_index);
			}
		}
	}

	return;
}

boolean biped_flying_through_air(
	long biped_index)
{
	struct biped_datum *biped;
	struct biped_definition *definition;

	biped = biped_get(biped_index);
	definition = biped_definition_get(biped->definition_index);

	return biped->biped.airborne_ticks>3 &&
		(!TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
		TEST_FLAG(biped->object.damage_flags, _object_dead_bit));
}

static void biped_start_landing(
	long biped_index,
	real landing_velocity)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	real minimum_soft_landing_velocity =
		definition->biped.minimum_soft_landing_velocity * (1.f / TICKS_PER_SECOND);
	real minimum_hard_landing_velocity =
		definition->biped.minimum_hard_landing_velocity * (1.f / TICKS_PER_SECOND);

	short landing_type;
	real velocity;
	real velocity_range;
	real recovery_time;

	if (landing_velocity < minimum_soft_landing_velocity)
		return;

	if (landing_velocity < minimum_hard_landing_velocity)
	{
		velocity = landing_velocity - minimum_soft_landing_velocity;
		velocity_range =
			minimum_hard_landing_velocity - minimum_soft_landing_velocity;
		recovery_time = definition->biped.maximum_soft_landing_time;
		landing_type = 0;
	}
	else
	{
		velocity = landing_velocity;
		velocity_range =
			definition->biped.maximum_hard_landing_velocity *
				(1.f / TICKS_PER_SECOND) - minimum_hard_landing_velocity;
		recovery_time = definition->biped.maximum_hard_landing_time;
		landing_type = 1;
	}

	recovery_time *= TICKS_PER_SECOND;
	if (velocity_range > 0.f)
	{
		velocity /= velocity_range;
		velocity = PIN(velocity, 0.f, 1.f);
		biped->biped.landing = landing_type;
		biped->biped.landing_recovery_counter = 0;
		biped->biped.landing_recovery_time =
			(char)(long)(recovery_time * velocity);
	}

	return;
}

void biped_adjust_placement(
	long object_index,
	struct object_placement_data *data)
{
	struct biped_datum *biped = biped_get(object_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	unsigned long flags = definition->biped.flags;

	if (TEST_FLAG(flags, _biped_pill_centered_at_origin_bit) &&
		!TEST_FLAG(flags, _biped_flying_bit))
	{
		point_from_line3d(&data->position, &data->up, (definition->biped.collision_radius), &data->position);
	}

	return;
}

void biped_export_function_values(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *biped_definition = biped_definition_get(biped->definition_index);
	real *function_values = biped->object.incoming_function_values;
	short *function_modes = biped_definition->biped.function_modes;
	long function_index;

	for (function_index = NUMBER_OF_INCOMING_OBJECT_FUNCTIONS; function_index; --function_index, ++function_modes, ++function_values)
	{
		if (*function_modes!=_biped_function_none)
		{
			real function_value = 0.0f;

			switch (*function_modes)
			{
			case _biped_function_flying_speed:
				function_value = magnitude3d(&biped->object.translational_velocity)/(biped_definition->biped.flying_velocity/(real)TICKS_PER_SECOND);
				function_value = PIN(function_value, 0.0f, 1.0f);
				break;
			}

			*function_values = function_value;
		}
	}

	return;
}

void biped_get_sight_position(
	long biped_index,
	short estimate_mode,
	real_point3d const *estimated_body_position,
	real_vector3d *desired_facing,
	real_vector3d const *desired_gun_offset,
	real_point3d *sight_position)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 759,
		(estimate_mode == _unit_estimate_none) ||
		(estimated_body_position != NULL));
	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 760,
		(estimate_mode != _unit_estimate_gun_position) ||
		(desired_facing != NULL));

	if (estimate_mode==_unit_estimate_none)
		object_get_origin(biped_index, sight_position);
	else
		*sight_position = *estimated_body_position;

	if (estimate_mode==_unit_estimate_gun_position)
	{
		real_vector3d left;

		match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 775, desired_facing && desired_gun_offset);

		left.i = -desired_facing->j;
		left.j = desired_facing->i;
		left.k = 0.f;

		point_from_line3d(sight_position, desired_facing, (desired_gun_offset->i), sight_position);
		point_from_line3d(sight_position, &left, (desired_gun_offset->j), sight_position);
		sight_position->z += desired_gun_offset->k;
	}
	else
	{
		real crouch;

		switch (estimate_mode)
		{
		case _unit_estimate_head_standing:
			crouch = 0.f;
			break;

		case _unit_estimate_head_crouching:
			crouch = 1.f;
			break;

		default:
			crouch = biped->biped.crouch;
			break;
		}

		sight_position->z += (1.f-crouch)*definition->biped.standing_camera_height +
			crouch*definition->biped.crouching_camera_height;
	}

	return;
}

/* port: the crouch goes on toward the seat the biped is in (all the way down
crouched, up otherwise) a tick's change a tick, as biped_physics_prepare
makes it, until it gets there; the eye (biped_get_sight_position's) with it */
real biped_eye_rise_ahead(
	long biped_index,
	real ticks)
{
	struct biped_datum *biped = biped_try_and_get(biped_index);
	struct biped_definition *definition;
	real change;
	real ahead;

	if (!biped || !(ticks > 0.f))
		return 0.f;
	definition = biped_definition_get(biped->definition_index);
	change = definition->biped.runtime_crouch_transition_velocity * ticks;
	if (biped->unit.animation.base_seat_index == _unit_base_seat_crouch)
		ahead = MIN(biped->biped.crouch + change, 1.f);
	else
		ahead = MAX(biped->biped.crouch - change, 0.f);
	return (biped->biped.crouch - ahead) *
		(definition->biped.standing_camera_height - definition->biped.crouching_camera_height);
}

void biped_get_autoaim_pill(
	long biped_index,
	real_point3d *base,
	real_vector3d *height,
	real *width)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	short pelvis_node_index = definition->biped.runtime_pelvis_node_index;

	if (pelvis_node_index!=NONE &&
		definition->biped.runtime_head_node_index!=NONE)
	{
		real_matrix4x3 *pelvis_matrix = object_get_node_matrix(biped_index, pelvis_node_index);
		real_matrix4x3 *head_matrix = object_get_node_matrix(
			biped_index,
			definition->biped.runtime_head_node_index);

		if (TEST_FLAG(definition->biped.flags, _biped_spherical_bit))
		{
			midpoint3d(&pelvis_matrix->position, &head_matrix->position, base);
			*height = *global_zero_vector3d;
		}
		else
		{
			*base = pelvis_matrix->position;
			vector_from_points3d(&pelvis_matrix->position, &head_matrix->position, height);
		}

		*width = definition->biped.autoaim_width;
	}
	else
	{
		real pill_height;
		real pill_width;

		biped_get_physics_pill(biped_index, base, &pill_height, &pill_width);
		base->z += pill_height*0.5f;
		scale_vector3d(global_up3d, pill_height*0.5f, height);
		*width = definition->biped.autoaim_width;
	}

	return;
}

boolean biped_fix_position(
	long biped_index,
	long line_of_sight_object_index,
	real_point3d const *new_position,
	real_point3d *final_position,
	real maximum_radius_fudge_factor,
	boolean fix_below_new_position,
	boolean dont_teleport,
	boolean use_radius_as_multiplier)
{
	boolean fixed = FALSE;

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 893,
		final_position || !dont_teleport);
	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 895,
		global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	global_current_collision_users[global_current_collision_user_depth++] =
		_collision_user_bipeds;

	if (biped_index != NONE || line_of_sight_object_index != NONE)
	{
		boolean line_of_sight_only;
		real_point3d line_of_sight_position;
		struct biped_datum *biped;
		unsigned long collision_flags;
		real_point3d position;
		real pill_height;
		real pill_width;
		struct collision_model_instance line_of_sight_instance;
		short maximum_fudge_vector_count;

		if (line_of_sight_object_index != NONE)
		{
			real unused_radius;

			object_get_bounding_sphere(
				line_of_sight_object_index,
				&line_of_sight_position,
				&unused_radius);
		}

		line_of_sight_only = FALSE;
		if (biped_index == NONE)
		{
			biped_index = line_of_sight_object_index;
			line_of_sight_only = TRUE;
		}

		biped = biped_get(biped_index);
		{
			struct biped_definition *definition =
				biped_definition_get(biped->definition_index);

			collision_flags = TEST_FLAG(
				definition->biped.flags,
				_biped_passes_through_bipeds_bit) ?
				_collision_test_for_bipeds_passthrough_living_flags :
				_collision_test_for_bipeds_living_flags;
			/* port: (where it fits among the other players, as it moves:
			_biped_physics_in_pass_through_players_bit). A scripted co-op
			teleport can also stand on a teammate for this one call. */
			if (biped_fix_ignores_players ||
				(biped->unit.player_index != NONE && !network_coop_player_collisions()))
				collision_flags |= FLAG(_collision_test_skip_player_bipeds_bit);
		}

		if (new_position)
		{
			real_point3d unused_pill_base;

			position = *new_position;
			biped_get_physics_pill(
				biped_index,
				&unused_pill_base,
				&pill_height,
				&pill_width);
		}
		else
		{
			biped_get_physics_pill(
				biped_index,
				&position,
				&pill_height,
				&pill_width);
		}

		if (line_of_sight_only)
			biped_index = NONE;

		maximum_fudge_vector_count = NUMBEROF(fudge_vectors) -
			(fix_below_new_position ? 0 : 9);
		if (line_of_sight_object_index != NONE)
		{
			/* port: an object with no collision model has no nodes to test
			(collision_model_test_pill went through the node count the stack
			left, and the matrices and model it pointed at) */
			if (!collision_model_instance_new(
				&line_of_sight_instance,
				line_of_sight_object_index))
			{
				line_of_sight_instance.node_count = 0;
			}
		}

		{
			real_vector3d left;
			real_vector3d pill_vector;
			short fudge_vector_index;

			cross_product3d(&biped->object.forward, &biped->object.up, &left);
			normalize3d(&left);
			scale_vector3d(global_up3d, pill_height, &pill_vector);
			if (use_radius_as_multiplier)
				maximum_radius_fudge_factor *= pill_width;

			for (fudge_vector_index = 0;
				!fixed && fudge_vector_index < maximum_fudge_vector_count;
				++fudge_vector_index)
			{
				real_point3d fixed_position;
				long cluster_index;
				struct collision_result pill_collision;
				struct collision_result line_collision;
				struct collision_model_test_pill_result model_pill_collision;

				if (fix_below_new_position)
				{
					real distance =
						maximum_radius_fudge_factor*fudge_vectors[fudge_vector_index].i;

					fixed_position.x = biped->object.forward.i*distance + position.x;
					fixed_position.y = biped->object.forward.j*distance + position.y;
					fixed_position.z = biped->object.forward.k*distance + position.z;
					distance = maximum_radius_fudge_factor*fudge_vectors[fudge_vector_index].j;
					fixed_position.x += left.i*distance;
					fixed_position.y += left.j*distance;
					fixed_position.z += left.k*distance;
					distance = maximum_radius_fudge_factor*fudge_vectors[fudge_vector_index].k;
					fixed_position.x += biped->object.up.i*distance;
					fixed_position.y += biped->object.up.j*distance;
					fixed_position.z += biped->object.up.k*distance;
				}
				else
				{
					real_vector3d const *fudge_vector =
						&fudge_vectors[fudge_vector_index];

					fixed_position.x =
						maximum_radius_fudge_factor*fudge_vector->i + position.x;
					fixed_position.y =
						maximum_radius_fudge_factor*fudge_vector->j + position.y;
					fixed_position.z =
						maximum_radius_fudge_factor*fudge_vector->k + position.z;
				}

				cluster_index = scenario_leaf_index_from_point(&fixed_position) == NONE ?
					NONE :
					TAG_BLOCK_GET_ELEMENT(
						&global_structure_bsp_get()->leaves,
						scenario_leaf_index_from_point(&fixed_position) & LONG_MAX,
						struct structure_leaf)->cluster_index;
				if (cluster_index != NONE &&
					collision_fix_pill(
						collision_flags,
						&fixed_position,
						pill_width*2.f,
						pill_height,
						pill_width,
						biped_index,
						&fixed_position) &&
					!collision_test_pill(
						collision_flags,
						&fixed_position,
						&pill_vector,
						pill_width,
						biped_index,
						&pill_collision) &&
					(line_of_sight_object_index == NONE ||
						(!collision_model_test_pill(
							&line_of_sight_instance,
							&fixed_position,
							&pill_vector,
							pill_width,
							&model_pill_collision) &&
						(!collision_test_line(
							collision_flags,
							&fixed_position,
							&line_of_sight_position,
							biped_index,
							&line_collision) ||
							line_collision.object_index == line_of_sight_object_index) &&
						(!collision_test_line(
							collision_flags,
							&line_of_sight_position,
							&fixed_position,
							line_of_sight_object_index,
							&line_collision) ||
							line_collision.object_index == biped_index))))
				{
					struct biped_definition *definition =
						biped_definition_get(biped->definition_index);
					struct location fixed_location;

					scenario_location_from_point(&fixed_location, &fixed_position);
					match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 1054,
						fixed_location.cluster_index!=NONE);
					if (!TEST_FLAG(
						definition->biped.flags,
						_biped_pill_centered_at_origin_bit))
					{
						fixed_position.z -= definition->biped.collision_radius;
					}

					if (biped_index != NONE && !dont_teleport)
					{
						biped->object.position = fixed_position;
						object_compute_node_matrices_recursive(biped_index);
						object_translate(
							biped_index,
							&fixed_position,
							&fixed_location);
					}

					if (final_position)
						*final_position = fixed_position;
					fixed = TRUE;
				}
			}
		}
	}

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 1080,
		global_current_collision_user_depth > 1);
	--global_current_collision_user_depth;

	return fixed;
}

void biped_exit_seat_end(
	long biped_index,
	long seat_object_index)
{
	struct biped_datum *biped = biped_get(biped_index);

	biped_definition_get(biped->definition_index);

	biped->object.forward.k = 0.f;
	if (normalize3d(&biped->object.forward)==0.f)
		biped->object.forward = *global_forward3d;
	biped->object.up = *global_up3d;
	SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);

	if (!biped_fix_position(biped_index, seat_object_index, NULL, 0, 2.f, TRUE, FALSE, TRUE))
	{
		real_point3d center;
		real radius;

		object_get_bounding_sphere(seat_object_index, &center, &radius);
		if (!biped_fix_position(biped_index, seat_object_index, &center, 0, radius, TRUE, FALSE, FALSE))
			error(_error_silent, "couldn't teleport the biped out far enough from the vehicle...");
	}

	return;
}

static void biped_apply_rotation(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	real_vector3d axis = biped->object.angular_velocity;
	real angle = normalize3d(&axis);
	real cosine = (real)cos(angle);
	real sine = (real)sin(angle);

	rotate_vector_about_axis(&biped->object.forward, &axis, sine, cosine);
	normalize3d(&biped->object.forward);
	{
		real_vector3d rotated_up = biped->object.up;
		real_vector3d left;

		rotate_vector_about_axis(&rotated_up, &axis, sine, cosine);
		cross_product3d(&biped->object.forward, &rotated_up, &left);
		cross_product3d(&left, &biped->object.forward, &biped->object.up);
		if (normalize3d(&biped->object.up)==0.f)
		{
			biped->object.forward = *global_forward3d;
			biped->object.up = *global_up3d;
		}
	}

	return;
}

void biped_render_debug(
	long biped_index)
{
	real_point3d base;
	real_vector3d height_vector;
	real height;
	real width;

	if (debug_objects_biped_physics_pills)
	{
		biped_get_physics_pill(biped_index, &base, &height, &width);
		if (height>0.f)
		{
			scale_vector3d(global_up3d, height, &height_vector);
			render_debug_pill(TRUE, &base, &height_vector, width, global_real_argb_white);
		}
		else
		{
			render_debug_sphere(TRUE, &base, width, global_real_argb_white);
		}
	}

	if (debug_objects_biped_autoaim_pills)
	{
		biped_get_autoaim_pill(biped_index, &base, &height_vector, &width);
		if (magnitude_squared3d(&height_vector)>0.f)
			render_debug_pill(TRUE, &base, &height_vector, width, global_real_argb_red);
		else
			render_debug_sphere(TRUE, &base, width, global_real_argb_red);
	}

	return;
}

void biped_build_flying_axes(
	real_vector3d const *forward_vector,
	real_vector3d *left_vector,
	real_vector3d *up_vector)
{
	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 2993, forward_vector && left_vector && up_vector);

	*up_vector = *global_up3d;
	cross_product3d(up_vector, forward_vector, left_vector);
	if (normalize3d(left_vector)==0.f)
	{
		*up_vector = *global_forward3d;
		cross_product3d(up_vector, forward_vector, left_vector);
		normalize3d(left_vector);
	}

	cross_product3d(forward_vector, left_vector, up_vector);
	normalize3d(up_vector);

	match_vassert("c:\\halo\\SOURCE\\units\\bipeds.c", 3008,
		valid_real_vector3d_axes3(forward_vector, left_vector, up_vector),
		csprintf(
			temporary,
			"%s, %s, %s: assert_valid_real_vector3d_axes3(%f, %f, %f / %f, %f, %f / %f, %f, %f)",
			"forward_vector",
			"left_vector",
			"up_vector",
			forward_vector->i, forward_vector->j, forward_vector->k,
			up_vector->i, up_vector->j, up_vector->k,
			left_vector->i, left_vector->j, left_vector->k));

	return;
}

/* ---------- private code */

static long biped_find_ground_surface(
	long object_index,
	real_vector3d const *direction,
	real distance,
	real_point3d *point,
	real_vector3d *normal)
{
	struct biped_datum *biped = biped_get(object_index);
	struct collision_bsp *collision_bsp = global_collision_bsp_get();
	long surface_index = NONE;
	real_vector3d vector;
	real_point3d origin;
	struct collision_bsp_test_vector_result result;

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 1146, global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_bipeds;

	object_get_origin(object_index, &origin);
	point_from_line3d(&origin, global_up3d, 0.4f, &origin);
	scale_vector3d(direction, distance, &vector);

	if (collision_bsp_test_vector(
		FLAG(_collision_test_front_facing_surfaces_bit),
		collision_bsp,
		0,
		NULL,
		&origin,
		&vector,
		REAL_MAX,
		&result))
	{
		surface_index = result.surface_index;
		if (point)
			point_from_line3d(&origin, &vector, result.t, point);
		if (normal)
			*normal = result.plane->n;
	}

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 1168, global_current_collision_user_depth > 1);
	--global_current_collision_user_depth;

	return surface_index;
}

long biped_approximate_surface_index(
	long biped_index,
	real_point3d *point)
{
	return biped_find_ground_surface(biped_index, global_down3d, 2.f, point, NULL);
}

long biped_find_pathfinding_surface_index(
	long biped_index,
	real_point3d *pathfinding_point)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	if (TEST_FLAG(definition->biped.flags, _biped_flying_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		biped->biped.pathfinding_surface_index = NONE;
		object_get_origin(biped_index, pathfinding_point);
	}
	else if (biped->biped.pathfinding_surface_index==NONE)
	{
		long time = game_time_get();

		if (time>biped->biped.last_pathfinding_attempt_time)
		{
			struct collision_bsp *collision_bsp = global_collision_bsp_get();
			real_point3d point = biped->biped.pathfinding_point;

			biped->biped.last_pathfinding_attempt_time = time;
			if (biped->biped.support_surface_index!=NONE)
			{
				real_point2d closest_point;

				collision_surface_find_closest_point2d(
					collision_bsp,
					biped->biped.support_surface_index,
					_z,
					TRUE,
					(real_point2d const *)&biped->biped.pathfinding_point,
					&closest_point);
				collision_surface_project_point2d(
					collision_bsp,
					biped->biped.support_surface_index,
					_z,
					TRUE,
					&closest_point,
					&point);
				biped->biped.pathfinding_surface_index = biped->biped.support_surface_index;
			}
			else if (biped->biped.last_pathfinding_surface_index!=NONE &&
				collision_surface_test_point2d(
					collision_bsp,
					biped->biped.last_pathfinding_surface_index,
					_z,
					TRUE,
					(real_point2d const *)&biped->biped.pathfinding_point))
			{
				biped->biped.pathfinding_surface_index = biped->biped.last_pathfinding_surface_index;
				collision_surface_project_point2d(
					collision_bsp,
					biped->biped.last_pathfinding_surface_index,
					_z,
					TRUE,
					(real_point2d const *)&biped->biped.pathfinding_point,
					&point);
				biped->biped.pathfinding_surface_index = biped->biped.last_pathfinding_surface_index;
			}

			if (biped->biped.pathfinding_surface_index==NONE)
				biped->biped.pathfinding_surface_index = biped_find_ground_surface(biped_index, global_down3d, 2.f, &point, NULL);

			if (biped->biped.pathfinding_surface_index!=NONE)
			{
				biped->biped.pathfinding_point = point;
				biped->biped.last_pathfinding_surface_index = biped->biped.pathfinding_surface_index;
			}
		}
	}

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 1255, pathfinding_point);
	*pathfinding_point = biped->biped.pathfinding_point;

	return biped->biped.pathfinding_surface_index;
}

static void biped_verify_object_vectors(
	long biped_index,
	char const *state)
{
	struct biped_datum *biped = biped_get(biped_index);

	if (!valid_real_vector3d_axes2(&biped->object.forward, &biped->object.up))
	{
		struct biped_definition *definition = biped_definition_get(biped->definition_index);

		match_vassert("c:\\halo\\SOURCE\\units\\bipeds.c", 1373, FALSE,
			csprintf(
				temporary,
				"biped %s (%s%s%s): %s vector failure: (%f, %f, %f) / (%f, %f, %f)",
				tag_name_strip_path(tag_get_name(biped->definition_index)),
				TEST_FLAG(definition->biped.flags, _biped_flying_bit)
					? "flying"
					: TEST_FLAG(definition->biped.flags, _biped_uses_player_physics_bit)
						? "player-physics"
						: TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit)
							? "climb"
							: "normal",
				TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ? "/dead" : "",
				TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit) ? "/limping" : "",
				state,
				biped->object.forward.i, biped->object.forward.j, biped->object.forward.k,
				biped->object.up.i, biped->object.up.j, biped->object.up.k));
	}

	return;
}

void biped_preprocess_node_orientations(
	long biped_index,
	struct real_orientation *node_orientations)
{
	biped_verify_object_vectors(biped_index, "preprocess-nodes");

	return;
}

void biped_accelerate(
	long biped_index,
	real_vector3d *acceleration)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	if (!TEST_FLAG(definition->unit.flags, _unit_is_special_bit))
	{
		biped_verify_object_vectors(biped_index, "pre-accel");

		if (!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			acceleration->i *= 0.5f;
			acceleration->j *= 0.5f;
			acceleration->k *= 0.5f;
		}

		if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
			biped_stop_limp_body_physics(biped_index);

		add_vectors3d(
			&biped->object.translational_velocity,
			acceleration,
			&biped->object.translational_velocity);
		SET_FLAG(biped->object.flags, _object_at_rest_bit, FALSE);
		SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
		SET_FLAG(biped->biped.flags, _biped_slipping_bit, TRUE);

		if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
			TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
			TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit))
		{
			real_vector3d angular_acceleration_axis;
			real scale;

			cross_product3d(global_up3d, acceleration, &angular_acceleration_axis);
			normalize3d(&angular_acceleration_axis);
			scale = real_random()*magnitude3d(acceleration)*(_pi/2);
			biped->object.angular_velocity.i += angular_acceleration_axis.i*scale;
			biped->object.angular_velocity.j += angular_acceleration_axis.j*scale;
			biped->object.angular_velocity.k += angular_acceleration_axis.k*scale;
		}

		if (biped->object.parent_object_index==NONE)
		{
			real_vector3d new_forward = *acceleration;

			if (normalize3d(&new_forward)>0.f)
			{
				biped->object.forward = new_forward;
				biped_snap_facing(biped_index);
				biped_verify_object_vectors(biped_index, "post-accel");
			}
		}
	}

	return;
}

static void biped_falling_danger(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	if (!TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		!(definition->biped.flags &
			(FLAG(_biped_flying_bit) | FLAG(_biped_immune_to_falling_damage_bit))) &&
		!TEST_FLAG(biped->unit.flags, _unit_no_falling_damage_bit) &&
		biped->unit.actor_index!=NONE &&
		biped->unit.animation.state!=_unit_state_ai_impulse)
	{
		long game_time = game_time_get();

		if (biped->biped.airborne_ticks>30 &&
			(biped->biped.last_falling_communication_time==NONE ||
			biped->biped.last_falling_communication_time+15<game_time))
		{
			struct game_globals_falling_damage *falling_damage = TAG_BLOCK_GET_ELEMENT(
				&scenario_get_game_globals()->falling_damage,
				0,
				struct game_globals_falling_damage);
			boolean dangerous = TRUE;
			real_point3d ground_point;

			biped->biped.last_falling_communication_time = game_time;
			if (biped_find_ground_surface(
				biped_index,
				global_down3d,
				6.f,
				&ground_point,
				NULL)!=NONE)
			{
				real_point3d origin;

				object_get_origin(biped_index, &origin);
				if (biped->object.translational_velocity.k>0.f ||
					(origin.z-ground_point.z)*global_gravity*2.f +
					biped->object.translational_velocity.k*
					biped->object.translational_velocity.k <
					falling_damage->runtime_maximum_damage_velocity*
					falling_damage->runtime_maximum_damage_velocity)
				{
					dangerous = FALSE;
				}
			}

			if (dangerous)
				unit_scream(biped_index, _unit_scream_falling);
		}
	}

	return;
}

static void biped_vehicle_speech(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct vehicle_runtime_datum *vehicle = vehicle_runtime_get(
		biped->object.parent_object_index);
	struct unit_definition *vehicle_definition = vehicle_definition_get(
		vehicle->definition_index);

	if (TEST_FLAG(vehicle_definition->unit.flags, _unit_causes_passenger_dialogue_bit) &&
		biped->unit.actor_index!=NONE &&
		biped->unit.animation.state!=_unit_state_ai_impulse &&
		biped->unit.gunner_inactive_ticks>120)
	{
		long game_time = game_time_get();

		if (vehicle->vehicle.airborne_ticks>30 &&
			(biped->biped.last_falling_communication_time==NONE ||
			biped->biped.last_falling_communication_time+15<game_time))
		{
			boolean falling = FALSE;

			biped->biped.last_falling_communication_time = game_time;
			if (biped_find_ground_surface(
				biped_index,
				global_down3d,
				8.f,
				NULL,
				NULL)==NONE)
			{
				real_vector3d predicted_direction;

				falling = TRUE;
				predicted_direction.i = vehicle->object.translational_velocity.i*60.f;
				predicted_direction.j = vehicle->object.translational_velocity.j*60.f;
				predicted_direction.k = vehicle->object.translational_velocity.k*60.f -
					global_gravity*1800.f;
				if (normalize3d(&predicted_direction)>0.f)
				{
					real_vector3d landing_normal;

					if (biped_find_ground_surface(
						biped_index,
						&predicted_direction,
						8.f,
						NULL,
						&landing_normal)!=NONE)
					{
						if (landing_normal.k>0.3f)
							falling = FALSE;
					}
				}
			}

			if (falling)
			{
				ai_communication_event(
					_ai_communication_vehicle_falling,
					biped_index,
					NONE,
					NONE,
					NONE,
					NONE,
					NULL);
			}
			else if (vehicle->object.up.k>0.6f &&
				magnitude3d(&vehicle->object.angular_velocity)<0.052359879f)
			{
				ai_communication_event(
					_ai_communication_vehicle_woohoo,
					biped_index,
					NONE,
					NONE,
					NONE,
					NONE,
					NULL);
			}
			else
			{
				ai_communication_event(
					_ai_communication_vehicle_scared,
					biped_index,
					NONE,
					NONE,
					NONE,
					NONE,
					NULL);
			}
		}
	}

	return;
}

static boolean biped_jump(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	boolean jumped = FALSE;

	if (!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		biped->biped.landing!=1)
	{
		long actor_index;
		real_vector3d jump_velocity;
		real jump_magnitude;

		jumped = TRUE;
		jump_magnitude = definition->biped.jump_velocity;
		if (biped->unit.player_index!=NONE)
		{
			struct game_globals_player_information *player_information =
				TAG_BLOCK_GET_ELEMENT(
					&scenario_get_game_globals()->player_information,
					0,
					struct game_globals_player_information);

			jump_magnitude *=
				1.f - biped->unit.body_stun*player_information->stun_turning_penalty;
		}
		if (cheat.super_jump && biped->unit.player_index!=NONE)
			jump_magnitude *= 4.f;

		jump_velocity = biped->object.translational_velocity;
		{
			real upward_velocity =
				jump_velocity.i*biped->object.up.i +
				(jump_velocity.k*biped->object.up.k +
				jump_velocity.j*biped->object.up.j);

			if (upward_velocity<jump_magnitude)
			{
				point_from_line3d(
					(real_point3d *)&jump_velocity,
					&biped->object.up,
					jump_magnitude-upward_velocity,
					(real_point3d *)&jump_velocity);
			}
		}

		actor_index = biped->unit.swarm_actor_index;
		if (actor_index==NONE)
			actor_index = biped->unit.actor_index;
		if (actor_index!=NONE)
		{
			boolean leap = biped->unit.animation.state==_unit_state_leap_start ||
				biped->unit.animation.state==_unit_state_leap_airborne;

			jumped = actor_aim_jump(
				actor_index,
				biped_index,
				leap,
				jump_magnitude,
				&jump_velocity);
		}

		if (jumped)
		{
			biped->object.translational_velocity = jump_velocity;
			SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
			biped->biped.jump_recovery_timer = 0;
			biped->biped.support_surface_index = NONE;
			biped_make_footstep(biped_index, _material_effect_biped_jump, 0);
			biped_make_footstep(biped_index, _material_effect_biped_jump, 1);
		}
	}

	return jumped;
}

static void biped_update_dead(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	if (TEST_FLAG(biped->biped.flags, _biped_limp_body_physics_active_bit) &&
		biped->biped.limp_body_current_relaxation_iterations <
			biped->biped.limp_body_max_relaxation_iterations)
	{
		if (!debug_biped_limp_body_disable)
			biped_limp_noodle_relax_nodes_onto_environment(biped_index);
		biped_verify_object_vectors(biped_index, "post-limp-noodle");
		animation->crouching = FALSE;

		return;
	}

	if (biped->biped.airborne_ticks>=3 &&
		!TEST_FLAG(definition->biped.flags, _biped_has_no_dying_airborne_bit))
	{
		if (biped->unit.animation.state==_unit_state_dying_airborne)
			biped_apply_rotation(biped_index);
		animation->state_desired = _unit_state_dying_airborne;
		biped_verify_object_vectors(biped_index, "post-dying-airborne");
	}
	else
	{
		if (biped->unit.animation.state==_unit_state_dying_airborne)
		{
			biped->biped.bank = 0.f;
			biped_snap_facing(biped_index);
		}
		animation->state_desired = _unit_state_dying;
		biped_verify_object_vectors(biped_index, "post-dying");
	}

	animation->crouching = FALSE;

	return;
}

static void biped_make_footstep(
	long biped_index,
	short event_index,
	short contact_point_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 3965, global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_bipeds;

	if (contact_point_index<definition->biped.contact_points.count &&
		definition->biped.material_effects.index!=NONE &&
		material_effect_visible(&biped->object.bounding_sphere_center))
	{
		struct biped_contact_point *contact_point = TAG_BLOCK_GET_ELEMENT(
			&definition->biped.contact_points,
			contact_point_index,
			struct biped_contact_point);
		struct object_marker marker;

		if (object_get_marker_by_name(biped_index, contact_point->marker_name, &marker, 1))
		{
			material_effect_new_from_point(
				definition->biped.material_effects.index,
				event_index,
				&marker.matrix.position,
				0.f);
		}
	}

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 3982, global_current_collision_user_depth > 1);
	--global_current_collision_user_depth;

	return;
}

static void biped_try_to_make_footsteps(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	boolean air_contact;
	boolean moving;

	biped_definition_get(biped->definition_index);
	moving = FALSE;
	air_contact = FALSE;

	switch (biped->unit.animation.state)
	{
	case _unit_state_turn_left:
	case _unit_state_turn_right:
		air_contact = TRUE;
		break;

	case _unit_state_move_front:
	case _unit_state_move_back:
	case _unit_state_move_left:
	case _unit_state_move_right:
		if (magnitude_squared3d(&biped->unit.throttle)>0.25f)
			moving = TRUE;
		break;
	}

	if (biped->object.animation.state.index!=NONE)
	{
		struct animation_graph *animation_graph = animation_graph_definition_get(
			biped->object.animation.animation_graph_index);
		struct animation *animation = TAG_BLOCK_GET_ELEMENT(
			&animation_graph->animations,
			biped->object.animation.state.index,
			struct animation);

		if (air_contact)
		{
			if (biped->object.animation.state.frame_index==0)
			{
				biped_make_footstep(biped_index, _material_effect_biped_shuffle, 0);
				biped_make_footstep(biped_index, _material_effect_biped_shuffle, 1);
			}
		}
		else if (moving &&
			(animation->private_left_foot_frame_index ||
			animation->private_right_foot_frame_index))
		{
			short frame_index = biped->object.animation.state.frame_index;
			boolean right_foot;

			if (frame_index==animation->private_left_foot_frame_index)
				right_foot = FALSE;
			else if (frame_index==animation->private_right_foot_frame_index)
				right_foot = TRUE;
			else
				goto no_footstep;

			biped_make_footstep(
				biped_index,
				biped->unit.animation.base_seat_index==_unit_base_seat_stand,
				right_foot!=FALSE);
		}
	}

no_footstep:
	switch (biped->biped.state)
	{
	case biped_state_idle:
		if (biped->biped.stop_ticks>0)
		{
			if (++biped->biped.stop_ticks>3)
			{
				biped_make_footstep(biped_index, _material_effect_biped_shuffle, 0);
				biped_make_footstep(biped_index, _material_effect_biped_shuffle, 1);
				biped->biped.stop_ticks = 0;
			}
		}
		break;

	case biped_state_moving:
		biped->biped.stop_ticks = 1;
		break;

	default:
		biped->biped.stop_ticks = 0;
		break;
	}

	return;
}

static void biped_update_airborne(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	boolean flying = FALSE;

	if (biped_flying_through_air(biped_index))
	{
		flying = TRUE;
		if (TEST_FLAG(definition->biped.flags, _biped_rotate_while_airborne_bit))
		{
			if (biped->unit.animation.state!=_unit_state_melee_airborne &&
				biped->unit.animation.state!=_unit_state_leap_melee)
			{
				real impulse = real_seed_random_range(
					get_global_random_seed_address(),
					0.052359879f,
					0.08726646f);
				real_vector3d axis;

				if (biped->object.up.k<0.8f)
				{
					cross_product3d(&biped->object.up, global_up3d, &axis);
					if (normalize3d(&axis)>0.f)
						goto axis_ready;
				}

				{
					vector3d_from_angle(&axis, real_random_range(0.f, 2.f*_pi));
				}

			axis_ready:
				point_from_line3d(
					(real_point3d *)&biped->object.angular_velocity,
					&axis,
					impulse,
					(real_point3d *)&biped->object.angular_velocity);
			}

			biped_apply_rotation(biped_index);
		}
	}

	match_assert(
		"c:\\halo\\SOURCE\\units\\bipeds.c",
		2687,
		TEST_FLAG(biped->biped.flags, _biped_airborne_bit));

	if (biped->unit.animation.state==_unit_state_leap_start ||
		biped->unit.animation.state==_unit_state_leap_airborne)
	{
		animation->state_desired = _unit_state_leap_airborne;
	}
	else if (biped->unit.animation.state==_unit_state_airborne || flying)
	{
		animation->state_desired = _unit_state_airborne;
	}

	biped_verify_object_vectors(biped_index, "post-airborne");

	return;
}

static void biped_update_landing(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);

	biped_definition_get(biped->definition_index);

	if (++biped->biped.landing_recovery_counter>=biped->biped.landing_recovery_time)
		biped->biped.landing = NONE;

	if (!cinematic_in_progress() &&
		(biped->biped.landing_recovery_counter==2 ||
		(biped->biped.landing==NONE &&
		biped->biped.landing_recovery_time<2)))
	{
		biped_make_footstep(biped_index, _material_effect_biped_jump_land, 0);
		biped_make_footstep(biped_index, _material_effect_biped_jump_land, 1);
	}

	animation->state_desired = biped->biped.landing==1
		? _unit_state_land_hard
		: _unit_state_land_soft;

	biped_verify_object_vectors(biped_index, "post-landing");

	return;
}

static void biped_update_slipping(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);

	biped_definition_get(biped->definition_index);

	if (biped->biped.slipping_ticks>3 &&
		magnitude_squared3d(&biped->object.translational_velocity)>MINIMUM_SLIPPING_FOOTSTEP_VELOCITY_SQUARED)
	{
		biped_make_footstep(biped_index, _material_effect_biped_sliding, 0);
		biped_make_footstep(biped_index, _material_effect_biped_sliding, 1);
	}

	biped_verify_object_vectors(biped_index, "post-slipping");

	return;
}

static void biped_update_jumping(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);

	biped_definition_get(biped->definition_index);
	if (!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
		biped->biped.landing != 1)
	{
		if (biped->biped.jump_recovery_timer < SCHAR_MAX)
			++biped->biped.jump_recovery_timer;

		if (TEST_FLAG(biped->unit.control_flags, _unit_control_jump_bit) &&
			biped->biped.jump_recovery_timer > 5)
		{
			biped_jump(biped_index);
		}
	}

	if (cheat.jetpack && biped->unit.player_index != NONE)
	{
		struct player_datum *player = player_get(biped->unit.player_index);
		/* inferred, descriptive names (not recovered Bungie identifiers): no PDB records the locals of this block */
		const real minimum_acceleration = 0.01f;
		const real maximum_acceleration = 0.05f;
		const real maximum_velocity = 1.4f;
		const real damping = 0.8f;
		boolean impulse = FALSE;

		if (TEST_FLAG(biped->unit.control_flags, _unit_control_weapon_primary_trigger_bit) &&
			TEST_FLAG(biped->unit.control_flags, _unit_control_throw_grenade_bit))
		{
			real forward_velocity = MAX(
				0.f,
				dot_product3d(
					&biped->object.translational_velocity,
					&biped->unit.aiming_vector));
			real acceleration_scale = PIN(forward_velocity / maximum_velocity, 0.f, 1.f);
			real_vector3d lateral_velocity;

			point_from_line3d(
				(real_point3d *)&biped->object.translational_velocity,
				&biped->unit.aiming_vector,
				-forward_velocity,
				(real_point3d *)&lateral_velocity);
			point_from_line3d(
				(real_point3d *)&biped->object.translational_velocity,
				&lateral_velocity,
				damping - 1.f,
				(real_point3d *)&biped->object.translational_velocity);
			point_from_line3d(
				(real_point3d *)&biped->object.translational_velocity,
				&biped->unit.aiming_vector,
				acceleration_scale * (maximum_acceleration - minimum_acceleration) -
					acceleration_scale * acceleration_scale * maximum_acceleration + minimum_acceleration,
				(real_point3d *)&biped->object.translational_velocity);
			SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
			impulse = TRUE;
		}
		else if (TEST_FLAG(biped->unit.control_flags, _unit_control_crouch_modifier_bit) &&
			TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			scale_vector3d(
				&biped->object.translational_velocity,
				damping - 1.f,
				&biped->object.translational_velocity);
		}

		if (player->local_player_index != NONE && impulse)
		{
			struct rumble_definition rumble;

			csmemset(&rumble, 0, sizeof(rumble));
			rumble.motors[0].scale = 1.f;
			rumble.motors[0].duration = 0.3f;
			rumble.motors[0].transition_function = 3;
			rumble_player_impulse(player->local_player_index, &rumble, 1.f, 1.f);
		}
	}

	return;
}

static void biped_find_nearby_support_surface(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct collision_bsp *collision_bsp = global_collision_bsp_get();
	real_point3d base;
	real height;
	real width;
	struct collision_bsp_test_sphere_result result;

	biped_get_physics_pill(biped_index, &base, &height, &width);

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 3999, global_current_collision_user_depth < MAXIMUM_COLLISION_USER_STACK_DEPTH);
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_bipeds;

	if (collision_bsp_test_sphere(
		collision_bsp,
		MAXIMUM_BREAKABLE_SURFACES_PER_MAP,
		breakable_surface_flags_get(),
		&base,
		width+0.05f,
		&result))
	{
		long support_surface_index = NONE;
		real closest_distance = REAL_MAX;
		real_plane3d support_plane;
		short surface_index;

		for (surface_index = 0; surface_index<result.surface_count; ++surface_index)
		{
			struct collision_surface *surface = TAG_BLOCK_GET_ELEMENT(
				&collision_bsp->surfaces,
				result.surface_indices[surface_index],
				struct collision_surface);
			real_plane3d surface_plane;
			real distance;

			bsp3d_get_plane_from_designator(
				&collision_bsp->bsp3d,
				surface->plane_designator,
				&surface_plane);
			distance = plane3d_distance_to_point(&surface_plane, &base);
			if (distance<closest_distance)
			{
				closest_distance = distance;
				support_surface_index = result.surface_indices[surface_index];
				support_plane = surface_plane;
			}
		}

		if (support_surface_index!=NONE)
		{
			biped->biped.support_surface_index = support_surface_index;
			biped->biped.ground_plane = support_plane;
			biped->object.up = support_plane.n;
		}
	}

	match_assert("c:\\halo\\SOURCE\\units\\bipeds.c", 4034, global_current_collision_user_depth > 1);
	--global_current_collision_user_depth;

	return;
}

/* port: a step of the biped's movement, tick_fraction of a tick long: 1 (the
tick's own step), or a sub-step's share of it (1/2, 1/4: sim_rate.h, the
fast phase). Velocities stay per tick, and a step moves the biped
velocity * tick_fraction.

A tick at 30 changes the velocity once, toward the velocity the biped
wants (by as much as its acceleration's maximum) and by gravity, then
moves a tick at the new velocity: semi-implicit Euler. That is exactly the
parabola of a steady acceleration a (the tick's change of velocity) whose
velocity at the tick's start is the velocity kept plus a/2. So a step of h
of a tick changes the velocity by h of the tick's change, worked out as at
30 from the velocity at the tick's start (tick_velocity) with a tick's
maximum, and moves along that parabola:
	(v + a/2) h + a h^2/2 = v' h + a h (1 - h)/2
(tick units; v the velocity kept, v' = v + a h the step's new one). For
gravity (a = -g) that is the launch bias of every ballistic start, a
jump's or a walk off a ledge (its velocity less g T30/2, as at 30), which
puts a jump's apex, air time and landing, and a run's start, where a tick
at 30 does, at every rate. At h = 1 it is the tick at 30's, bit for bit.
The push into the ground is no acceleration: a velocity of a 128th of a
world unit a tick, which the ground clips away at each step, is given at
each. gravity is a tick's (global_gravity), or none (bipeds_substep's, for
the rest of the tick a biped walks off the ground in).

tick_ground (NULL but in a sub-step: biped_substep_move) is the plane the
tick's own step met, which its push goes down to and then slides along:
the step's movement goes down onto it (the first sub-step) and along it,
rather than probing for ground past it. A tick's step ends where its
velocity clipped by the plane takes it, less the gap it went down, so
the sub-steps end there too */
static void biped_update_physics(
	struct biped_physics *physics,
	real tick_fraction,
	real gravity,
	real_vector3d const *tick_velocity,
	real_plane3d const *tick_ground)
{
	real_vector2d facing = { 0.f, 0.f };
	boolean climbing = TEST_FLAG(physics->in_flags, _biped_physics_in_climb_anything_bit);
	word in_flags;
	real_point3d position;
	real_vector3d velocity;
	real_point3d clipped_position;
	real_vector3d clipped_velocity;
	struct collision_plane collisions[16];
	short collision_count;
	unsigned long collision_flags;
	/* port: the step's movement (velocity * tick_fraction, and arc: the
	tick's steady acceleration's share of it, the step's change of velocity
	times arc_scale), and the step's gravity */
	real_vector3d displacement;
	real_vector3d arc = { 0.f, 0.f, 0.f };
	real arc_scale = (1.f - tick_fraction) * 0.5f;
	real step_gravity = gravity * tick_fraction;
	/* port: the velocity the tick's step at 30 would end at (the tick's
	starting velocity and the whole of its change, worked out in the same
	order: physics->new_velocity's, bit for bit, at 30), which a landing in
	any of its sub-steps is as fast as (physics->landing_velocity: its
	landing, and falling damage, as at 30) */
	real_vector3d tick_new_velocity;

	match_assert_valid_real_point3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xBCC, &physics->position);
	match_assert_valid_real_vector3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xBCD, &physics->velocity);

	in_flags = physics->in_flags;
	physics->out_flags = 0;
	if (TEST_FLAG(in_flags, _biped_physics_in_flying_bit))
	{
		real_vector3d up;
		real_vector3d left;
		real_vector3d desired_velocity;
		real_vector3d acceleration;
		real_vector3d direction;
		real_vector3d velocity_change;

		biped_build_flying_axes(&physics->forward, &left, &up);
		desired_velocity.i = physics->forward.i * physics->movement_desired.i +
			left.i * physics->movement_desired.j +
			up.i * physics->movement_desired.k;
		desired_velocity.j = physics->forward.j * physics->movement_desired.i +
			left.j * physics->movement_desired.j +
			up.j * physics->movement_desired.k;
		desired_velocity.k = physics->forward.k * physics->movement_desired.i +
			left.k * physics->movement_desired.j +
			up.k * physics->movement_desired.k;
		scale_vector3d(&desired_velocity, 1.f - physics->movement_penalty, &desired_velocity);
		acceleration.i = desired_velocity.i - tick_velocity->i;
		acceleration.j = desired_velocity.j - tick_velocity->j;
		acceleration.k = desired_velocity.k - tick_velocity->k;
		direction = acceleration;
		if (normalize3d(&direction) > physics->acceleration_maximum)
			scale_vector3d(&direction, physics->acceleration_maximum, &velocity_change);
		else
			velocity_change = acceleration;
		add_vectors3d(&velocity_change, tick_velocity, &tick_new_velocity);
		/* (port: the step's share of the tick's change) */
		scale_vector3d(&velocity_change, tick_fraction, &velocity_change);
		scale_vector3d(&velocity_change, arc_scale, &arc);
		add_vectors3d(&velocity_change, &physics->velocity, &physics->new_velocity);
		SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, TRUE);
		SET_FLAG(physics->out_flags, _biped_physics_out_slipping_bit, FALSE);
	}
	else if (TEST_FLAG(in_flags, _biped_physics_in_absolute_movement_bit))
	{
		physics->new_velocity.k = physics->movement_desired.k;
		facing.i = physics->new_velocity.i =
			physics->movement_desired.i * physics->forward.i -
			physics->forward.j * physics->movement_desired.j;
		facing.j = physics->new_velocity.j =
			physics->movement_desired.j * physics->forward.i +
			physics->movement_desired.i * physics->forward.j;
		tick_new_velocity = physics->new_velocity;
	}
	else if (TEST_FLAG(in_flags, _biped_physics_in_airborne_bit))
	{
		real_vector2d desired_velocity;
		real_vector2d acceleration;
		real_vector2d direction;
		real_vector2d velocity_change;

		desired_velocity.i = physics->movement_desired.i * physics->forward.i -
			physics->forward.j * physics->movement_desired.j;
		desired_velocity.j = physics->movement_desired.j * physics->forward.i +
			physics->movement_desired.i * physics->forward.j;
		scale_vector2d(&desired_velocity, 1.f - physics->movement_penalty, &desired_velocity);
		acceleration.i = desired_velocity.i - tick_velocity->i;
		acceleration.j = desired_velocity.j - tick_velocity->j;
		direction = acceleration;
		if (normalize2d(&direction) > physics->airborne_acceleration_maximum)
			scale_vector2d(&direction, physics->airborne_acceleration_maximum, &velocity_change);
		else
			velocity_change = acceleration;
		tick_new_velocity.i = velocity_change.i + tick_velocity->i;
		tick_new_velocity.j = velocity_change.j + tick_velocity->j;
		tick_new_velocity.k = tick_velocity->k - gravity;
		/* (port: the step's share of the tick's change) */
		scale_vector2d(&velocity_change, tick_fraction, &velocity_change);
		arc.i = velocity_change.i * arc_scale;
		arc.j = velocity_change.j * arc_scale;
		physics->new_velocity.i = velocity_change.i + physics->velocity.i;
		physics->new_velocity.j = velocity_change.j + physics->velocity.j;
		physics->new_velocity.k = physics->velocity.k - step_gravity;
		arc.k = -step_gravity * arc_scale;
		physics->out_flags = TEST_FLAG(in_flags, _biped_physics_in_slipping_bit) ?
			FLAG(_biped_physics_out_slipping_bit) : 0;
	}
	else
	{
		real speed = magnitude3d(&physics->movement_desired);
		real_vector3d const *ground_normal = &physics->ground_plane.n;
		real_vector3d move_direction;

		if (TEST_FLAG(in_flags, _biped_physics_in_climb_anything_bit))
		{
			real_vector3d forward = physics->aiming;
			real_vector3d left;

			cross_product3d(ground_normal, &forward, &left);
			if (normalize3d(&left) == 0.f)
			{
				cross_product3d(ground_normal, global_up3d, &left);
				if (normalize3d(&left) == 0.f)
				{
					cross_product3d(ground_normal, global_forward3d, &left);
					normalize3d(&left);
				}
			}
			cross_product3d(&left, ground_normal, &forward);
			normalize3d(&forward);

			move_direction.i = forward.i * physics->movement_desired.i +
				left.i * physics->movement_desired.j;
			move_direction.j = forward.j * physics->movement_desired.i +
				left.j * physics->movement_desired.j;
			move_direction.k = forward.k * physics->movement_desired.i +
				left.k * physics->movement_desired.j +
				physics->movement_desired.k;
			normalize3d(&move_direction);
		}
		else if (ground_normal->k > _real_epsilon)
		{
			move_direction.i = facing.i =
				physics->movement_desired.i * physics->forward.i -
				physics->forward.j * physics->movement_desired.j;
			move_direction.j = facing.j =
				physics->movement_desired.j * physics->forward.i +
				physics->movement_desired.i * physics->forward.j;
			move_direction.k = physics->movement_desired.k -
				(move_direction.j * ground_normal->j + move_direction.i * ground_normal->i) /
				ground_normal->k;
			normalize3d(&move_direction);
		}
		else
		{
			real_vector3d forward = physics->aiming;
			real_vector3d left;

			cross_product3d(global_up3d, &physics->aiming, &left);
			normalize3d(&left);
			point_from_line3d(
				(real_point3d *)&forward,
				ground_normal,
				-dot_product3d(&forward, ground_normal),
				(real_point3d *)&forward);
			point_from_line3d(
				(real_point3d *)&left,
				ground_normal,
				-dot_product3d(&left, ground_normal),
				(real_point3d *)&left);
			facing.i = physics->movement_desired.i * physics->forward.i -
				physics->forward.j * physics->movement_desired.j;
			facing.j = physics->movement_desired.j * physics->forward.i +
				physics->movement_desired.i * physics->forward.j;
			move_direction.i = forward.i * physics->movement_desired.i +
				left.i * physics->movement_desired.j;
			move_direction.j = forward.j * physics->movement_desired.i +
				left.j * physics->movement_desired.j;
			move_direction.k = forward.k * physics->movement_desired.i +
				left.k * physics->movement_desired.j +
				physics->movement_desired.k;
			if (!climbing)
				move_direction.k *= 5.f;
			normalize3d(&move_direction);
		}

		if (!TEST_FLAG(in_flags, _biped_physics_in_climb_anything_bit))
		{
			if (move_direction.k <= physics->downhill_k1)
			{
				speed *= physics->downhill_velocity_scale;
			}
			else if (move_direction.k < physics->downhill_k0)
			{
				speed *= (move_direction.k - physics->downhill_k0) *
					(physics->downhill_velocity_scale - 1.f) /
					(physics->downhill_k1 - physics->downhill_k0) + 1.f;
			}
			else if (move_direction.k >= physics->uphill_k1)
			{
				speed *= physics->uphill_velocity_scale;
			}
			else if (move_direction.k > physics->uphill_k0)
			{
				speed *= (move_direction.k - physics->uphill_k0) *
					(physics->uphill_velocity_scale - 1.f) /
					(physics->uphill_k1 - physics->uphill_k0) + 1.f;
			}
		}

		{
			real_vector3d desired_velocity;
			real_vector3d acceleration;
			real_vector3d direction;
			real_vector3d velocity_change;
			boolean slipping = FALSE;

			scale_vector3d(&move_direction, speed * (1.f - physics->movement_penalty), &desired_velocity);
			acceleration.i = desired_velocity.i - tick_velocity->i;
			acceleration.j = desired_velocity.j - tick_velocity->j;
			acceleration.k = desired_velocity.k - tick_velocity->k;
			direction = acceleration;
			if (normalize3d(&direction) > physics->acceleration_maximum)
			{
				if (!TEST_FLAG(in_flags, _biped_physics_in_climb_anything_bit))
					slipping = TEST_FLAG(in_flags, _biped_physics_in_slipping_bit);
				scale_vector3d(&direction, physics->acceleration_maximum, &velocity_change);
			}
			else
			{
				velocity_change = acceleration;
			}
			tick_new_velocity.i = velocity_change.i - ground_normal->i * (1.f / 128.f);
			tick_new_velocity.j = velocity_change.j - ground_normal->j * (1.f / 128.f);
			tick_new_velocity.k = velocity_change.k - ground_normal->k * (1.f / 128.f);
			add_vectors3d(&tick_new_velocity, tick_velocity, &tick_new_velocity);
			/* (port: the step's share of the tick's change) */
			scale_vector3d(&velocity_change, tick_fraction, &velocity_change);
			scale_vector3d(&velocity_change, arc_scale, &arc);

			/* (port: the push into the ground, a velocity of a 128th of a
			world unit a tick given at each step, which the ground clips
			away: a step probes its share of a tick's 128th. On the ground
			the tick's own step met (tick_ground) it is the tick's,
			given at its first sub-step, where the tick's step gives it) */
			if (!TEST_FLAG(in_flags, _biped_physics_in_pushed_bit))
			{
				velocity_change.i -= ground_normal->i * (1.f / 128.f);
				velocity_change.j -= ground_normal->j * (1.f / 128.f);
				velocity_change.k -= ground_normal->k * (1.f / 128.f);
			}
			physics->out_flags = slipping ? FLAG(_biped_physics_out_slipping_bit) : 0;
			add_vectors3d(&velocity_change, &physics->velocity, &physics->new_velocity);
			if (TEST_FLAG(physics->out_flags, _biped_physics_out_slipping_bit))
			{
				physics->new_velocity.k -= step_gravity;
				arc.k -= step_gravity * arc_scale;
				tick_new_velocity.k -= gravity;
			}
		}
	}

	if (TEST_FLAG(physics->in_flags, _biped_physics_in_no_collision_bit))
	{
		collision_flags = 0;
	}
	else if (TEST_FLAG(physics->in_flags, _biped_physics_in_dead_bit))
	{
		collision_flags = _collision_test_for_bipeds_dead_flags;
	}
	else
	{
		collision_flags = TEST_FLAG(physics->in_flags, _biped_physics_in_pass_through_bipeds_bit) ?
			_collision_test_for_bipeds_passthrough_living_flags :
			_collision_test_for_bipeds_living_flags;
		if (TEST_FLAG(physics->in_flags, _biped_physics_in_pass_through_players_bit))
			collision_flags |= FLAG(_collision_test_skip_player_bipeds_bit);
	}

	position = physics->position;
	velocity = physics->new_velocity;
	velocity.k += physics->crouch_velocity;
	/* port: the step's movement (the velocity's, at 30) */
	scale_vector3d(&velocity, tick_fraction, &displacement);
	if (tick_ground)
	{
		/* (port: less what goes into the tick's ground, of the velocity's
		movement (the push, all of it) and of the arc's; and the first
		sub-step goes down onto it, as the tick's step goes down to it
		before it slides on: movement that is no velocity, as the arc) */
		real_vector3d const *normal = &tick_ground->n;
		real displacement_dot = dot_product3d(&displacement, normal);
		real arc_dot = dot_product3d(&arc, normal);

		if (displacement_dot < 0.f)
		{
			point_from_line3d(
				(real_point3d *)&displacement,
				normal,
				-displacement_dot,
				(real_point3d *)&displacement);
		}
		if (arc_dot < 0.f)
		{
			point_from_line3d(
				(real_point3d *)&arc,
				normal,
				-arc_dot,
				(real_point3d *)&arc);
		}
		if (!TEST_FLAG(in_flags, _biped_physics_in_pushed_bit))
		{
			/* (the collisions' planes are the pill's: where its point is
			when it touches) */
			real gap = dot_product3d((real_vector3d const *)&position, normal) - tick_ground->d;

			if (gap > 0.f)
				point_from_line3d((real_point3d *)&arc, normal, -gap, (real_point3d *)&arc);
		}
	}
	/* (none at 30, where arc is zeros of either sign, which would change a
	velocity of -0) */
	if (arc.i != 0.f)
		displacement.i += arc.i;
	if (arc.j != 0.f)
		displacement.j += arc.j;
	if (arc.k != 0.f)
		displacement.k += arc.k;
	if (debug_biped_skip_collision)
	{
		collisions[0].t = 0.f;
		collisions[0].point = physics->position;
		collisions[0].plane.n = *global_up3d;
		collisions[0].plane.d = -position.z;
		collisions[0].object_index = physics->biped_index;
		collisions[0].surface_index = NONE;
		collisions[0].flags = 0;
		collisions[0].breakable_surface_index = 0;
		collisions[0].material_index = NONE;
		clipped_velocity = velocity;
		clipped_position.x = position.x + displacement.i;
		clipped_position.y = position.y + displacement.j;
		clipped_position.z = position.z;
		clipped_velocity.k = 0.f;
		collision_count = 1;
	}
	else
	{
		collision_count = collision_move_pill(
			collision_flags,
			&position,
			&displacement,
			physics->height,
			physics->width,
			physics->biped_index,
			&clipped_position,
			&clipped_velocity,
			16,
			collisions);

		/* port: the movement as the collisions clipped it, as the step's
		velocity (a tick's): arc clipped as the movement was (by the planes it
		met: the ground takes all of a fall's, a wall none of it) taken off,
		and a step's movement made a tick's. At 30 this is the clipped
		movement itself */
		if (arc.i != 0.f || arc.j != 0.f || arc.k != 0.f)
		{
			short collision_index;

			for (collision_index = 0; collision_index < collision_count; collision_index++)
			{
				real_vector3d const *normal = &collisions[collision_index].plane.n;

				point_from_line3d(
					(real_point3d *)&arc,
					normal,
					-dot_product3d(&arc, normal),
					(real_point3d *)&arc);
			}
			clipped_velocity.i -= arc.i;
			clipped_velocity.j -= arc.j;
			clipped_velocity.k -= arc.k;
		}
		scale_vector3d(&clipped_velocity, 1.f / tick_fraction, &clipped_velocity);
	}

	if (debug_biped_physics &&
		object_get(physics->biped_index)->object.owner_player_index != NONE)
	{
		collision_debug_repeat = TRUE;
		collision_debug_point = position;
		collision_debug_vector = displacement;
		collision_debug_length = 1.0f;
		collision_debug_width = physics->width;
		collision_debug_height = physics->height;
	}

	SET_FLAG(physics->out_flags, _biped_physics_out_splatter_bit, collision_count >= 16);
	physics->stick_surface_index = NONE;
	if (!collision_count && physics->existing_support_surface_index != NONE)
	{
		struct collision_bsp *bsp = global_collision_bsp_get();
		long best_surface_index = NONE;
		real best_distance_squared = REAL_MAX;

		if (VALID_INDEX(physics->existing_support_surface_index, bsp->surfaces.count))
		{
			struct collision_surface *surface = TAG_BLOCK_GET_ELEMENT(
				&bsp->surfaces,
				physics->existing_support_surface_index,
				struct collision_surface);
			long edge_index = surface->first_edge_index;
			short edge_count = 0;
			real_plane3d plane;
			real_plane3d best_plane;
			real best_velocity_dot;
			real_point3d point;
			real distance;

			bsp3d_get_plane_from_designator(&bsp->bsp3d, surface->plane_designator, &plane);
			distance = -(plane.n.i * clipped_position.x +
				plane.n.j * clipped_position.y +
				plane.n.k * clipped_position.z -
				plane.d);
			point.x = plane.n.i * distance + clipped_position.x;
			point.y = plane.n.j * distance + clipped_position.y;
			point.z = plane.n.k * distance + clipped_position.z;

			do
			{
				struct collision_edge *edge;
				boolean reverse;
				long neighbor_surface_index;

				/* port: the surface's ring of edges ends as
				collision_surface_edge_ring_continues says, or at an edge
				whose vertices aren't the bsp's (a map's indices; the
				retail rings all close within 3 to 8 edges) */
				if (!collision_surface_edge_ring_continues(bsp, edge_index, edge_count++))
				{
					break;
				}
				edge = TAG_BLOCK_GET_ELEMENT(
					&bsp->edges,
					edge_index,
					struct collision_edge);
				if (!VALID_INDEX(edge->vertex_indices[0], bsp->vertices.count) ||
					!VALID_INDEX(edge->vertex_indices[1], bsp->vertices.count))
				{
					static boolean reported = FALSE;

					if (!reported)
					{
						reported = TRUE;
						error(_error_silent, "collision edge #%ld's vertices are not the bsp's", edge_index);
					}
					break;
				}
				reverse = physics->existing_support_surface_index == edge->surface_indices[1];
				neighbor_surface_index = edge->surface_indices[!reverse];

				/* port: (and a neighbor that is no surface of the bsp's is
				none) */
				if (neighbor_surface_index != NONE &&
					collision_bsp_valid_surface_index(bsp, neighbor_surface_index))
				{
					struct collision_surface *neighbor_surface = TAG_BLOCK_GET_ELEMENT(
						&bsp->surfaces,
						neighbor_surface_index,
						struct collision_surface);

					if (TEST_FLAG(physics->in_flags, _biped_physics_in_climb_anything_bit) ||
						TEST_FLAG(neighbor_surface->flags, _collision_surface_climbable_bit))
					{
						real velocity_dot;

						bsp3d_get_plane_from_designator(
							&bsp->bsp3d,
							neighbor_surface->plane_designator,
							&plane);
						velocity_dot = dot_product3d(&plane.n, &clipped_velocity);
						if (velocity_dot > 0.f &&
							plane.n.i * clipped_position.x +
								plane.n.j * clipped_position.y +
								plane.n.k * clipped_position.z -
								plane.d > physics->width * -0.5f)
						{
							struct collision_vertex *vertex0 = TAG_BLOCK_GET_ELEMENT(
								&bsp->vertices,
								edge->vertex_indices[0],
								struct collision_vertex);
							struct collision_vertex *vertex1 = TAG_BLOCK_GET_ELEMENT(
								&bsp->vertices,
								edge->vertex_indices[1],
								struct collision_vertex);
							real_vector3d edge_vector;
							real_point3d closest_point;
							real t;
							real distance_squared;

							vector_from_points3d(&vertex0->point, &vertex1->point, &edge_vector);
							t = ((point.x - vertex0->point.x) * edge_vector.i +
								(point.y - vertex0->point.y) * edge_vector.j +
								(point.z - vertex0->point.z) * edge_vector.k) /
								magnitude_squared3d(&edge_vector);
							if (t < 0.f)
								closest_point = vertex0->point;
							else if (t > 1.f)
								closest_point = vertex1->point;
							else
								point_from_line3d(&vertex0->point, &edge_vector, t, &closest_point);

							distance_squared = distance_squared3d(&point, &closest_point);
							if (distance_squared < best_distance_squared)
							{
								best_distance_squared = distance_squared;
								best_surface_index = neighbor_surface_index;
								best_plane = plane;
								best_velocity_dot = velocity_dot;
							}
						}
					}
				}

				edge_index = edge->edge_indices[reverse];
			}
			while (edge_index != surface->first_edge_index);

			if (best_surface_index != NONE &&
				best_distance_squared <= (physics->width * 2.f) * (physics->width * 2.f) &&
				best_velocity_dot <= 1.6f / TICKS_PER_SECOND)
			{
				distance = best_plane.n.i * clipped_position.x +
					best_plane.n.j * clipped_position.y +
					best_plane.n.k * clipped_position.z -
					(best_plane.d + physics->width);
				/* (port: the snap onto the next surface reaches as far as a
				step's share of a tick's, half the pill's width at 30: a
				step of a sub-step goes its share of the way off the
				surface, and more steps a tick with a tick's reach each
				would hold the biped to the ground harder) */
				if (!(fabs(distance) > physics->width * 0.5f * tick_fraction))
				{
					real new_velocity_dot = dot_product3d(&best_plane.n, &clipped_velocity);

					clipped_position.x = best_plane.n.i * -distance + clipped_position.x;
					clipped_position.y = best_plane.n.j * -distance + clipped_position.y;
					clipped_position.z = best_plane.n.k * -distance + clipped_position.z;
					if (new_velocity_dot > -1.f / TICKS_PER_SECOND)
					{
						point_from_line3d(
							(real_point3d *)&clipped_velocity,
							&best_plane.n,
							-(new_velocity_dot + 1.f / TICKS_PER_SECOND),
							(real_point3d *)&clipped_velocity);
					}

					collisions[0].t = 0.f;
					collisions[0].point.x = best_plane.n.i * -physics->width + clipped_position.x;
					collisions[0].point.y = best_plane.n.j * -physics->width + clipped_position.y;
					collisions[0].point.z = best_plane.n.k * -physics->width + clipped_position.z;
					collisions[0].plane = best_plane;
					collisions[0].object_index = NONE;
					collisions[0].surface_index = best_surface_index;
					collisions[0].flags = 0;
					collisions[0].breakable_surface_index = 0;
					collisions[0].material_index = NONE;
					collision_count = 1;
					physics->stick_surface_index = best_surface_index;
				}
			}
		}
	}

	{
		real magnitude_squared = facing.j * facing.j + facing.i * facing.i;

		if (magnitude_squared > _real_epsilon * _real_epsilon)
		{
			real inverse_magnitude = 1.0 / square_root(magnitude_squared);

			facing.i *= inverse_magnitude;
			facing.j *= inverse_magnitude;
		}
	}

	{
		short best_collision_index = NONE;
		boolean best_walkable = FALSE;
		boolean best_stuck = FALSE;
		real best_normal_k = -REAL_MAX;
		real best_velocity_dot = -REAL_MAX;

		if (!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit))
		{
			short collision_index;

			for (collision_index = 0; collision_index < collision_count; collision_index++)
			{
				struct collision_plane *collision = &collisions[collision_index];
				boolean walkable =
					!TEST_FLAG(physics->in_flags, _biped_physics_in_dead_bit) &&
					(climbing || TEST_FLAG(collision->flags, _collision_surface_climbable_bit));
				boolean stuck =
					physics->stick_surface_index != NONE &&
					best_collision_index != NONE &&
					collisions[best_collision_index].surface_index == physics->stick_surface_index;
				real velocity_dot = -dot_product3d(&collision->plane.n, &physics->new_velocity);
				boolean better;

				if (walkable)
				{
					if (!climbing && collision->plane.n.i * facing.i + collision->plane.n.j * facing.j > 0.5f)
						better = FALSE;
					else if (!best_walkable || stuck)
						better = TRUE;
					else if (best_stuck)
						better = FALSE;
					else
						better = velocity_dot > best_velocity_dot;
				}
				else
				{
					better = !best_walkable && collision->plane.n.k > best_normal_k;
				}

				if (better)
				{
					best_velocity_dot = velocity_dot;
					best_walkable = walkable;
					best_collision_index = collision_index;
					best_stuck = stuck;
					best_normal_k = collision->plane.n.k;
				}

				if (!TEST_FLAG(physics->out_flags, _biped_physics_out_volatile_collision_bit) &&
					(TEST_FLAG(collision->flags, _collision_surface_breakable_bit) ||
					(collision->object_index != NONE &&
					!TEST_FLAG(_object_mask_scenery, object_get_type(collision->object_index)))))
				{
					SET_FLAG(physics->out_flags, _biped_physics_out_volatile_collision_bit, TRUE);
				}
			}
		}

		if (best_collision_index != NONE)
		{
			struct collision_plane *collision = &collisions[best_collision_index];
			real_plane3d plane = collision->plane;
			boolean supported = TRUE;
			real velocity_dot = -dot_product3d(&plane.n, &velocity);

			if (!best_walkable && !best_stuck)
			{
				if (best_normal_k < physics->minimum_normal_k)
				{
					supported = FALSE;
				}
				else if (TEST_FLAG(physics->in_flags, _biped_physics_in_airborne_bit) &&
					physics->ground_tangential_velocity_max < REAL_MAX)
				{
					real_vector3d tangential_velocity;
					real maximum = physics->ground_tangential_velocity_max;

					point_from_line3d(
						(real_point3d *)&velocity,
						&plane.n,
						velocity_dot,
						(real_point3d *)&tangential_velocity);
					if (magnitude_squared3d(&tangential_velocity) > maximum * maximum &&
						velocity_dot / magnitude3d(&velocity) < physics->ground_tangential_angle)
					{
						supported = FALSE;
					}
				}
			}

			if (supported)
			{
				SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, FALSE);
				physics->ground_plane = collision->plane;
				physics->support_surface_index = collision->surface_index;
				if (physics->support_surface_index == NONE ||
					physics->support_surface_index != physics->stick_surface_index)
				{
					/* (port: as the tick's step at 30 lands, at the velocity it ends
					at, in whichever sub-step the landing is) */
					real_vector3d landing = tick_new_velocity;

					landing.k += physics->crouch_velocity;
					physics->landing_velocity = -dot_product3d(&landing, &physics->ground_plane.n);
				}
				else
				{
					physics->landing_velocity = 0.f;
				}
			}
			else
			{
				SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, TRUE);
				physics->ground_plane = depths_of_hell;
				physics->support_surface_index = NONE;
				physics->landing_velocity = 0.f;
			}
		}
		else
		{
			SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, TRUE);
			physics->ground_plane = depths_of_hell;
			physics->support_surface_index = NONE;
			physics->landing_velocity = 0.f;
		}
	}

	{
		long bumped_object_index = NONE;
		short bumped_object_type;
		real bumped_speed_squared;
		short collision_index;

		for (collision_index = 0; collision_index < collision_count; collision_index++)
		{
			long object_index = collisions[collision_index].object_index;

			if (object_index != NONE)
			{
				struct object_datum *object = object_get(object_index);
				real_vector3d relative_velocity;
				real speed_squared;
				boolean bump;

				relative_velocity.i = object->object.translational_velocity.i - clipped_velocity.i;
				relative_velocity.j = object->object.translational_velocity.j - clipped_velocity.j;
				relative_velocity.k = object->object.translational_velocity.k - clipped_velocity.k;
				speed_squared = magnitude_squared3d(&relative_velocity);
				if (bumped_object_index == NONE)
					bump = TRUE;
				else if (bumped_object_type == _object_type_vehicle)
					bump = object->object.type == _object_type_vehicle && speed_squared > bumped_speed_squared;
				else
					bump = object->object.type == _object_type_vehicle || speed_squared > bumped_speed_squared;

				if (bump)
				{
					bumped_object_type = object->object.type;
					bumped_speed_squared = speed_squared;
					bumped_object_index = object_index;
				}
			}
		}
		physics->bumped_object_index = bumped_object_index;
	}

	physics->elevator_object_index = NONE;
	{
		short collision_index;

		for (collision_index = 0; collision_index < collision_count; collision_index++)
		{
			long object_index = collisions[collision_index].object_index;

			if (object_index != NONE)
			{
				struct object_datum *machine = object_try_and_get_and_verify_type(
					object_index,
					_object_mask_machine);

				if (machine)
				{
					struct machine_definition *machine_definition =
						machine_definition_get(machine->definition_index);

					if (TEST_FLAG(machine_definition->machine.flags, _machine_never_appears_locked_bit) &&
						machine_definition->machine.elevator_node_index != NONE)
					{
						physics->elevator_object_index = object_index;
					}
				}
			}
		}
	}

	physics->new_position = clipped_position;
	physics->new_velocity = clipped_velocity;
	{
		real_vector3d velocity_delta;

		velocity_delta.i = clipped_velocity.i - velocity.i;
		velocity_delta.j = clipped_velocity.j - velocity.j;
		velocity_delta.k = clipped_velocity.k - velocity.k;
		physics->collision_velocity = magnitude3d(&velocity_delta);
	}
	physics->new_velocity.k -= physics->crouch_velocity;

	if (TEST_FLAG(physics->in_flags, _biped_physics_in_crouched_bit) &&
		TEST_FLAG(physics->in_flags, _biped_physics_in_trying_to_stand_bit))
	{
		struct biped_datum *biped = biped_get(physics->biped_index);

		if (biped->unit.player_index != NONE)
		{
			struct biped_definition *definition = biped_definition_get(biped->definition_index);

			if (!TEST_FLAG(definition->biped.flags, _biped_pill_centered_at_origin_bit) &&
				!TEST_FLAG(definition->biped.flags, _biped_spherical_bit))
			{
				real radius = definition->biped.collision_height_standing * 0.5f;
				real_point3d center = physics->new_position;
				struct collision_feature_list features;

				center.z += radius;
				if (collision_get_features_in_sphere(
					TEST_FLAG(physics->in_flags, _biped_physics_in_dead_bit) ?
						_collision_test_for_bipeds_dead_flags :
						_collision_test_for_bipeds_living_flags,
					&center,
					radius,
					0.f,
					definition->biped.collision_radius,
					physics->biped_index,
					&features))
				{
					real_vector3d standing_vector;
					struct collision_plane collision;

					scale_vector3d(
						global_up3d,
						definition->biped.collision_height_standing -
							2.f * definition->biped.collision_radius,
						&standing_vector);
					if (collision_features_test_vector(
						&features,
						&physics->new_position,
						&standing_vector,
						&collision))
					{
						SET_FLAG(physics->out_flags, _biped_physics_out_cannot_stand_bit, TRUE);
					}
				}
			}
		}
	}

	match_assert_valid_real_point3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xF10, &physics->new_position);
	match_assert_valid_real_vector3d("c:\\halo\\SOURCE\\units\\bipeds.c", 0xF11, &physics->new_velocity);

	return;
}

static void biped_snap_facing(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	if (TEST_FLAG(definition->biped.flags, _biped_flying_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		real_vector3d left;
		real_vector3d up;
		real sine;
		real cosine;

		match_assert_valid_real_normal3d("c:\\halo\\SOURCE\\units\\bipeds.c", 4051, &biped->object.forward);

		cross_product3d(&biped->object.forward, global_up3d, &left);
		cross_product3d(&left, &biped->object.forward, &up);
		if (normalize3d(&up)==0.f)
		{
			up = *global_forward3d;
			left = *global_left3d;
		}

		cosine = (real)cos(biped->biped.bank);
		sine = (real)sin(biped->biped.bank);
		up.i *= cosine;
		up.j *= cosine;
		up.k *= cosine;
		normalize3d(&left);
		biped->object.up.i = left.i*sine + up.i;
		biped->object.up.j = left.j*sine + up.j;
		biped->object.up.k = left.k*sine + up.k;

		biped_verify_object_vectors(biped_index, "post-bank-snapfacing");
	}
	else if (TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		real_vector3d normal;
		real_vector3d vector;
		real_vector3d axis;
		real_vector3d forward;

		if (biped->biped.support_surface_index==NONE)
		{
			normal = biped->object.up;
		}
		else
		{
			boolean tilt_toward_surface = TRUE;

			normal = biped->biped.ground_plane.n;
			cross_product3d(&biped->object.up, &normal, &axis);
			if (normalize3d(&axis)==0.f)
			{
				if (dot_product3d(&normal, &biped->object.up)>0.f)
					tilt_toward_surface = FALSE;
				else
					axis = biped->object.forward;
			}

			if (tilt_toward_surface)
			{
				real_vector3d tilted_up = biped->object.up;

				rotate_vector_about_axis(
					&tilted_up,
					&axis,
					(real)sin(BIPED_CLIMBING_SNAP_ANGLE),
					(real)cos(BIPED_CLIMBING_SNAP_ANGLE));
				cross_product3d(&tilted_up, &normal, &vector);
				if (dot_product3d(&axis, &vector)>0.f)
					normal = tilted_up;
			}
		}

		cross_product3d(&biped->object.forward, &normal, &vector);
		cross_product3d(&normal, &vector, &forward);
		if (normalize3d(&forward)==0.f)
		{
			cross_product3d(&normal, &biped->object.up, &vector);
			cross_product3d(&normal, &vector, &forward);
			if (normalize3d(&forward)==0.f)
			{
				normal = *global_up3d;
				forward = *global_forward3d;
			}
		}

		biped->object.forward = forward;
		biped->object.up = normal;

		biped_verify_object_vectors(biped_index, "post-climb-snapfacing");
	}
	else if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		!TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
	{
		real cosine_of_angle;

		biped_verify_object_vectors(biped_index, "pre-deadplane-snapfacing");

		cosine_of_angle = dot_product3d(&biped->object.up, &biped->biped.ground_plane.n);
		if (!(fabs(cosine_of_angle - 1.f)<_real_epsilon))
		{
			real angle = (real)acos(cosine_of_angle);

			if (angle!=0.f)
			{
				real_vector3d axis;

				cross_product3d(&biped->object.up, &biped->biped.ground_plane.n, &axis);
				if (normalize3d(&axis)!=0.f)
				{
					real cosine = (real)cos(angle);
					real sine = (real)sin(angle);

					rotate_vector_about_axis(&biped->object.up, &axis, sine, cosine);
					rotate_vector_about_axis(&biped->object.forward, &axis, sine, cosine);
					normalize3d(&biped->object.up);
					normalize3d(&biped->object.forward);
				}
			}
		}

		biped_verify_object_vectors(biped_index, "post-deadplane-snapfacing");
	}
	else
	{
		biped->object.forward.k = 0.f;
		if (normalize3d(&biped->object.forward)==0.f)
			biped->object.forward = *global_forward3d;
		biped->object.up = *global_up3d;

		biped_verify_object_vectors(biped_index, "post-normal-snapfacing");
	}

	return;
}

static void biped_update_turning(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	unsigned long flags = definition->biped.flags;

	if (TEST_FLAG(flags, _biped_flying_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		boolean nearly_stationary = FALSE;
		real_vector3d target_facing;
		real_vector3d turn_axis;
		real turn_rate;
		real target_bank;
		real bank_blend;
		real bank_time;
		real_rectangle2d facing_bounds;
		real angular_velocity_limit;
		real angular_acceleration_limit;

		if (magnitude_squared3d(&biped->object.translational_velocity)<0.00027777778f &&
			magnitude_squared3d(&biped->object.angular_velocity)<0.0000013538552f &&
			magnitude_squared3d(&biped->unit.throttle)<0.010000001f)
		{
			real stationary_turning_threshold =
				TEST_FLAG(biped->unit.control_flags, _unit_control_exact_facing_bit) ?
				0.99000001f : definition->biped.runtime_cosine_stationary_turning_threshold;

			if (dot_product3d(
				&biped->unit.desired_facing_vector,
				&biped->object.forward)>stationary_turning_threshold)
			{
				nearly_stationary = TRUE;
			}
		}

		if (nearly_stationary)
		{
			target_facing = biped->object.forward;
		}
		else
		{
			real pitch = definition->biped.flying_pitch_amount*biped->unit.throttle.k;

			target_facing = biped->unit.desired_facing_vector;
			if (pitch!=0.f)
			{
				target_facing.k += pitch;
				if (normalize3d(&target_facing)==0.f)
					target_facing = biped->unit.desired_facing_vector;
			}
		}

		cross_product3d(&biped->object.forward, &biped->object.up, &turn_axis);
		turn_rate = dot_product3d(&turn_axis, &biped->unit.desired_facing_vector)*
			(10.f/3.f)*biped->unit.throttle.i - biped->unit.throttle.j;
		if (turn_rate>1.5f)
			turn_rate = 1.5f;

		target_bank = definition->biped.flying_bank_amount*turn_rate;
		if (biped->biped.bank*target_bank>0.f)
		{
			real bank_ratio = biped->biped.bank/target_bank;

			if (bank_ratio>1.f)
				bank_ratio = 1.f;
			bank_blend = 1.f-bank_ratio;
		}
		else
		{
			bank_blend = 1.f;
		}

		bank_time = definition->biped.flying_bank_apply_time*bank_blend +
			definition->biped.flying_bank_decay_time*(1.f-bank_blend);
		if (bank_time>0.f)
		{
			biped->biped.bank +=
				(target_bank-biped->biped.bank)/(bank_time*TICKS_PER_SECOND);
		}
		else
		{
			biped->biped.bank = target_bank;
		}

		facing_bounds.x0 = -_pi;
		facing_bounds.x1 = _pi;
		facing_bounds.y0 = -_pi/2.f;
		facing_bounds.y1 = _pi/2.f;
		angular_velocity_limit =
			definition->biped.flying_angular_velocity/TICKS_PER_SECOND;
		angular_acceleration_limit =
			definition->biped.flying_angular_acceleration/
			(TICKS_PER_SECOND*TICKS_PER_SECOND);
		if (angular_acceleration_limit==0.f)
		{
			biped->object.forward = target_facing;
		}
		else
		{
			unit_euler_aiming_update(
				NULL,
				&biped->object.forward,
				&target_facing,
				&biped->object.angular_velocity,
				&facing_bounds,
				angular_velocity_limit,
				angular_acceleration_limit);
		}

		biped_snap_facing(biped_index);
		biped_verify_object_vectors(biped_index, "post-fly-turn");
	}
	else if (biped->unit.animation.base_seat_index!=_unit_base_seat_asleep)
	{
		boolean flaming =
			biped->unit.animation.base_seat_index==_unit_base_seat_flaming;
		/* port: a player throwing a grenade turns with the aim, as one
		meleeing does: the melee plays over the moving or idle state, the
		throw is a state of its own, which the original never turned in
		(unit_turns_with_aim: game.enhanced_animations, but always in a
		network game) */
		boolean throwing_turn =
			biped->unit.player_index!=NONE &&
			biped->unit.animation.state==_unit_state_throw_grenade &&
			unit_turns_with_aim(biped_index);
		/* port: and a player in the air turns with the aim, as one running
		does: the original never turned the airborne state, and clipped the
		aim to its aiming screen about the body left facing where the jump
		began, so a shot after a turn in the air missed the crosshair
		(unit_turns_with_aim: game.enhanced_animations, but always in a
		network game) */
		boolean airborne_turn =
			biped->unit.player_index!=NONE &&
			biped->unit.animation.state==_unit_state_airborne &&
			unit_turns_with_aim(biped_index);
		real_vector3d turn_axis;
		real turn_error;
		real facing_alignment;
		boolean turn_right;

		if (TEST_FLAG(flags, _biped_climbs_anything_bit))
		{
			real_vector3d cross;
			real_vector3d error;

			cross_product3d(
				&biped->object.up,
				&biped->unit.desired_facing_vector,
				&cross);
			cross_product3d(&cross, &biped->object.up, &turn_axis);
			if (normalize3d(&turn_axis)==0.f)
				turn_axis = biped->object.forward;

			cross_product3d(&turn_axis, &biped->object.forward, &error);
			turn_error = dot_product3d(&error, &biped->object.up);
			facing_alignment = dot_product3d(&turn_axis, &biped->object.forward);
		}
		else
		{
			real_vector2d forward_2d;
			real_vector2d turn_axis_2d;

			turn_axis = biped->unit.desired_facing_vector;
			turn_axis.k = 0.f;
			turn_axis_2d.i = turn_axis.i;
			turn_axis_2d.j = turn_axis.j;
			if (normalize2d(&turn_axis_2d)==0.f)
			{
				turn_axis = biped->object.forward;
			}
			else
			{
				turn_axis.i = turn_axis_2d.i;
				turn_axis.j = turn_axis_2d.j;
			}
			turn_axis_2d.i = turn_axis.i;
			turn_axis_2d.j = turn_axis.j;

			forward_2d.i = biped->object.forward.i;
			forward_2d.j = biped->object.forward.j;
			turn_error = cross_product2d(
				&turn_axis_2d,
				&forward_2d);
			facing_alignment = dot_product2d(
				&turn_axis_2d,
				&forward_2d);
		}

		turn_right = turn_error>0.f;
		if (facing_alignment<-0.89999998f)
		{
			if (biped->unit.animation.state==_unit_state_turn_right)
				turn_right = TRUE;
			else if (biped->unit.animation.state==_unit_state_turn_left)
				turn_right = FALSE;
		}

		if (biped->biped.state==biped_state_moving ||
			TEST_FLAG(flags, _biped_turns_without_animating_bit) ||
			throwing_turn ||
			airborne_turn)
		{
			if (!TEST_FLAG(biped->unit.control_flags, _unit_control_look_dont_turn_bit))
			{
				real turn_step =
					definition->biped.moving_turning_speed/TICKS_PER_SECOND;
				real cosine = (real)cos(turn_step);
				real sine = (real)sin(turn_step);
				real progress;

				if (turn_right)
					sine = -sine;

				if (TEST_FLAG(flags, _biped_climbs_anything_bit))
				{
					real_vector3d cross;

					rotate_vector_about_axis(
						&biped->object.forward,
						&biped->object.up,
						sine,
						cosine);
					cross_product3d(
						&turn_axis,
						&biped->object.forward,
						&cross);
					progress = dot_product3d(&cross, &biped->object.up);
				}
				else
				{
					real_vector2d forward_2d;
					real_vector2d turn_axis_2d;

					forward_2d.i = biped->object.forward.i;
					forward_2d.j = biped->object.forward.j;
					turn_axis_2d.i = turn_axis.i;
					turn_axis_2d.j = turn_axis.j;
					rotate_vector2d(
						&forward_2d,
						sine,
						cosine,
						&forward_2d);
					biped->object.forward.i = forward_2d.i;
					biped->object.forward.j = forward_2d.j;
					progress = cross_product2d(
						&turn_axis_2d,
						&forward_2d);
				}

				if ((!turn_right || progress<0.f) &&
					(turn_right || progress>0.f))
				{
					if (TEST_FLAG(flags, _biped_climbs_anything_bit))
					{
						real_vector3d rotation_axis;

						cross_product3d(
							&biped->object.up,
							&turn_axis,
							&rotation_axis);
						if (normalize3d(&rotation_axis)>0.f)
						{
							cross_product3d(
								&rotation_axis,
								&biped->object.up,
								&biped->object.forward);
						}
					}
					else
					{
						biped->object.forward = turn_axis;
						biped->object.forward.k = 0.f;
						biped->object.up = *global_up3d;
					}

					normalize3d(&biped->object.forward);
				}
			}

			biped_verify_object_vectors(biped_index, "post-moving-turn");
		}
		else if (biped->biped.state==biped_state_idle &&
			!flaming &&
			!TEST_FLAG(biped->unit.flags, _unit_aim_without_turning_bit) &&
			!TEST_FLAG(biped->unit.control_flags, _unit_control_look_dont_turn_bit))
		{
			real stationary_turning_threshold =
				TEST_FLAG(biped->unit.control_flags, _unit_control_exact_facing_bit) ?
				0.99000001f : definition->biped.runtime_cosine_stationary_turning_threshold;

			if (facing_alignment<stationary_turning_threshold &&
				!TEST_FLAG(definition->unit.flags, _unit_is_special_bit))
			{
				animation->state_desired = turn_right ?
					_unit_state_turn_right : _unit_state_turn_left;
			}

			biped_verify_object_vectors(biped_index, "post-standing-turn");
		}
	}

	return;
}

/* port: a player's movement a tick (a velocity, per tick) by its throttle at
the tick's speeds (biped_physics_prepare's, and each sub-step's of the fast
phase, whose throttle the player's input gives again, player_control.c) */
static void biped_player_movement(
	struct biped_player_speeds const *speeds,
	real_vector3d const *throttle,
	real_vector3d *movement_desired)
{
	short direction = throttle->i <= 0.f ? 0 : 1;

	movement_desired->i =
		throttle->i * speeds->stun_scale * speeds->forward_speed[direction] /
		TICKS_PER_SECOND;
	movement_desired->j =
		throttle->j * speeds->stun_scale * speeds->sideways_speed[direction] /
		TICKS_PER_SECOND;
	movement_desired->k = 0.f;
	if (speeds->double_speed)
	{
		scale_vector3d(
			movement_desired,
			speeds->double_speed_multiplier,
			movement_desired);
	}

	return;
}

/* port: the biped's movement this tick made ready for biped_update_physics (its
input, speeds and accelerations, its crouch's change, its flags): FALSE when
it does not move (the first half of the Xbox game's biped_update_moving) */
static boolean biped_physics_prepare(
	long biped_index,
	struct unit_animation_update_data *animation,
	struct biped_physics *result,
	struct biped_player_speeds *speeds)
{
	struct biped_physics physics;
	struct biped_definition *definition;
	struct biped_datum *biped;
	real movement_scale;
	word in_flags;

	biped = biped_get(biped_index);

	if (TEST_FLAG(biped->object.flags, _object_at_rest_bit) &&
		TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		TEST_FLAG(biped->unit.animation.flags, _unit_animation_ignore_translation_bit))
	{
		return FALSE;
	}

	definition = biped_definition_get(biped->definition_index);
	speeds->valid = FALSE;
	physics.biped_index = biped_index;
	physics.in_flags = 0;
	physics.forward = biped->object.forward;
	if (TEST_FLAG(definition->unit.flags, _unit_simple_creature_bit))
		physics.aiming = biped->object.forward;
	else
		unit_get_aiming_vector(biped_index, &physics.aiming);

	physics.velocity = biped->object.translational_velocity;
	physics.crouch_velocity = 0.f;
	physics.acceleration_maximum = 0.0053333333f;
	physics.airborne_acceleration_maximum = 0.f;
	biped_get_physics_pill(
		biped_index,
		&physics.position,
		&physics.height,
		&physics.width);
	physics.minimum_normal_k = definition->biped.runtime_minimum_normal_k;
	physics.downhill_k0 = definition->biped.runtime_downhill_k0;
	physics.downhill_k1 = definition->biped.runtime_downhill_k1;
	physics.downhill_velocity_scale = definition->biped.downhill_velocity_scale;
	physics.uphill_k0 = definition->biped.runtime_uphill_k0;
	physics.uphill_k1 = definition->biped.runtime_uphill_k1;
	physics.uphill_velocity_scale = definition->biped.uphill_velocity_scale;
	physics.ground_plane = biped->biped.ground_plane;
	physics.ground_tangential_velocity_max = REAL_MAX;
	physics.ground_tangential_angle = 0.f;
	physics.existing_support_surface_index = biped->biped.support_surface_index;

	if (biped->biped.landing == 1)
	{
		physics.movement_desired.i = 0.f;
		physics.movement_desired.j = 0.f;
		physics.movement_desired.k = 0.f;
		physics.movement_penalty = 1.f;
	}
	else
	{
		movement_scale = 1.f;
		if (TEST_FLAG(definition->biped.flags, _biped_random_speed_increase_bit) &&
			!biped->unit.aiming_speed)
		{
			unsigned long stagger_ordinal = (unsigned long)biped_index % 137;
			real difficulty = game_difficulty_get_value(
				_game_difficulty_value_infection_forms);

			movement_scale =
				difficulty * ((real)stagger_ordinal * 0.00729927f) + 1.f;
		}

		if (!TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
			TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			real throttle_x = biped->unit.throttle.i;
			real throttle_y = biped->unit.throttle.j;

			if (throttle_x != 0.f || throttle_y != 0.f ||
				biped->unit.throttle.k != 0.f)
			{
				boolean stunned = biped->unit.body_stun > 0.2f;
				char movement_base;

				if (definition->unit.stunned_movement_threshold > 0.f &&
					biped->object.recent_body_damage >
						definition->unit.stunned_movement_threshold)
				{
					stunned = TRUE;
				}

				movement_base = stunned ? _unit_state_stunned_move_front :
					_unit_state_move_front;
				if (fabs(throttle_x) >= fabs(throttle_y))
				{
					animation->state_desired = throttle_x >= 0.f ?
						movement_base : movement_base + 1;
				}
				else
				{
					animation->state_desired = throttle_y >= 0.f ?
						movement_base + 2 : movement_base + 3;
				}
			}
		}
		else
		{
			animation->state_desired = _unit_state_idle;
		}

		physics.movement_desired = *global_zero_vector3d;
		if (biped->object.animation.state.index != NONE &&
			(TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
			!TEST_FLAG(definition->biped.flags, _biped_flying_bit)) &&
			!TEST_FLAG(
				biped->unit.animation.flags,
				_unit_animation_ignore_translation_bit))
		{
			struct animation_graph *animation_graph;
			struct animation *animation_definition;
			real dy = 0.f;
			real dyaw = 0.f;

			model_definition_get(definition->object.model.index);
			animation_graph = animation_graph_definition_get(
				biped->object.animation.animation_graph_index);
			animation_definition = TAG_BLOCK_GET_ELEMENT(
				&animation_graph->animations,
				biped->object.animation.state.index,
				struct animation);

			switch (animation_definition->frame_info_type)
			{
			case _animation_frame_info_xy_translation:
				{
					struct animation_frame_info_dx_dy const *frame_info =
						(struct animation_frame_info_dx_dy const *)animation_get_frame_info(
							animation_definition,
							biped->object.animation.state.frame_index,
							sizeof(struct animation_frame_info_dx_dy));

					physics.movement_desired.i = frame_info->dx;
					dy = frame_info->dy;
				}
				break;

			case _animation_frame_info_xy_translation_yaw_rotation:
				{
					struct animation_frame_info_dx_dy_dyaw const *frame_info =
						(struct animation_frame_info_dx_dy_dyaw const *)animation_get_frame_info(
							animation_definition,
							biped->object.animation.state.frame_index,
							sizeof(struct animation_frame_info_dx_dy_dyaw));

					physics.movement_desired.i = frame_info->dx;
					dy = frame_info->dy;
					physics.movement_desired.j = frame_info->dy;
					dyaw = frame_info->dyaw;
				}
				break;

			case _animation_frame_info_xyz_translation_yaw_rotation:
				{
					struct animation_frame_info_dx_dy_dz_dyaw const *frame_info =
						(struct animation_frame_info_dx_dy_dz_dyaw const *)animation_get_frame_info(
							animation_definition,
							biped->object.animation.state.frame_index,
							sizeof(struct animation_frame_info_dx_dy_dz_dyaw));

					physics.movement_desired.i = frame_info->dx;
					dy = frame_info->dy;
					physics.movement_desired.j = frame_info->dy;
					physics.movement_desired.k = frame_info->dz;
					dyaw = frame_info->dyaw;
				}
				break;
			}

			physics.movement_desired.i *= movement_scale;
			physics.movement_desired.j = dy * movement_scale;
			physics.movement_desired.k *= movement_scale;

			if (!(fabs(dyaw) < _real_epsilon))
			{
				real_vector3d rotated_forward;
				real sine_value = (real)sin(dyaw);
				real cosine_value = (real)cos(dyaw);

				rotated_forward = biped->object.forward;
				rotate_vector_about_axis(
					&rotated_forward,
					&biped->object.up,
					sine_value,
					cosine_value);
				if (TEST_FLAG(
					biped->unit.control_flags,
					_unit_control_exact_facing_bit) &&
					(biped->unit.animation.state == _unit_state_turn_left ||
					biped->unit.animation.state == _unit_state_turn_right) &&
					dot_product3d(
						&biped->object.forward,
						&biped->unit.desired_facing_vector) > 0.5f)
				{
					real_vector3d old_cross;
					real_vector3d new_cross;

					cross_product3d(
						&biped->unit.desired_facing_vector,
						&biped->object.forward,
						&old_cross);
					cross_product3d(
						&biped->unit.desired_facing_vector,
						&rotated_forward,
						&new_cross);
					if (dot_product3d(&old_cross, &biped->object.up) *
						dot_product3d(&new_cross, &biped->object.up) <= 0.f)
					{
						rotated_forward = biped->unit.desired_facing_vector;
						unit_abort_animation(biped_index);
					}
				}

				biped->object.forward = rotated_forward;
				biped_snap_facing(biped_index);
			}
		}

		if (!TEST_FLAG(definition->biped.flags, _biped_flying_bit) ||
			TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			if (!TEST_FLAG(definition->biped.flags, _biped_uses_player_physics_bit) ||
				biped->unit.animation.state == _unit_state_user_animation)
			{
				if (!TEST_FLAG(biped->biped.flags, _biped_slipping_bit) &&
					!TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
				{
					biped->object.translational_velocity.i =
						physics.movement_desired.i;
					biped->object.translational_velocity.j =
						physics.movement_desired.j;
					physics.acceleration_maximum = REAL_MAX;
				}
			}
			else
			{
				struct game_globals_player_information *player_information =
					TAG_BLOCK_GET_ELEMENT(
						&scenario_get_game_globals()->player_information,
						0,
						struct game_globals_player_information);
				struct game_globals_player_information player_information_block;
				real body_stun_scale = 1.f;
				real crouch;
				real uncrouch;
				real stun_scale;
				real forward_speed;
				real sideways_speed;
				real acceleration;
				short direction;

				if (cinematic_in_progress() ||
					TEST_FLAG(
						definition->biped.flags,
						_biped_uses_old_player_physics_bit))
				{
					player_information_block = *player_information;
					player_information = &player_information_block;
					player_information_block.walking_speed = 0.51200002f;
					player_information_block.run_forward_speed = 2.25f;
					player_information_block.run_backward_speed = 2.f;
					player_information_block.run_sideways_speed = 2.f;
					player_information_block.run_acceleration = 0.31999999f;
				}

				if (biped->unit.player_index != NONE)
				{
					body_stun_scale =
						player_get(biped->unit.player_index)->speed_multiplier;
				}

				crouch = biped->biped.crouch;
				uncrouch = 1.f - crouch;
				stun_scale =
					-(player_information->stun_movement_penalty *
						biped->unit.body_stun - 1.f) *
					body_stun_scale * movement_scale;
				/* (port: the speeds of a throttle back (0) and forward (1), by
				which a sub-step's throttle is its movement too,
				biped_player_movement) */
				for (direction = 0; direction < 2; direction++)
				{
					if (!direction)
					{
						forward_speed = player_information->sneak_backward_speed;
						sideways_speed = player_information->run_backward_speed;
					}
					else
					{
						forward_speed = player_information->sneak_forward_speed;
						sideways_speed = player_information->run_forward_speed;
					}

					forward_speed = sideways_speed * uncrouch + forward_speed * crouch;
					sideways_speed = player_information->run_sideways_speed * uncrouch +
						player_information->sneak_sideways_speed * crouch;
					if (biped->unit.animation.base_seat_index == _unit_base_seat_alert)
					{
						forward_speed = !direction ?
							0.f : player_information->walking_speed;
						sideways_speed = 0.f;
					}
					speeds->forward_speed[direction] = forward_speed;
					speeds->sideways_speed[direction] = sideways_speed;
				}
				acceleration = player_information->run_acceleration * uncrouch +
					player_information->sneak_acceleration * crouch;
				speeds->stun_scale = stun_scale;
				speeds->double_speed = game_players_are_double_speed();
				speeds->double_speed_multiplier = player_information->double_speed_multiplier;
				speeds->valid = TRUE;

				physics.acceleration_maximum = acceleration / TICKS_PER_SECOND;
				biped_player_movement(speeds, &biped->unit.throttle, &physics.movement_desired);
				physics.airborne_acceleration_maximum =
					player_information->airborne_acceleration / TICKS_PER_SECOND;
				if (!TEST_FLAG(
					biped->unit.control_flags,
					_unit_control_look_dont_turn_bit))
				{
					physics.forward = biped->unit.desired_facing_vector;
				}
			}
		}
		else
		{
			real deceleration_scale =
				1.f - MIN(magnitude3d(&biped->unit.throttle), 1.f);
			real crouch_modifier = 1.f;
			real acceleration;

			if (definition->biped.flying_crouch_velocity_modifier > 0.f)
			{
				if (biped->biped.crouch == 1.f)
				{
					crouch_modifier =
						definition->biped.flying_crouch_velocity_modifier;
				}
				else if (biped->biped.crouch > 0.f)
				{
					crouch_modifier =
						(definition->biped.flying_crouch_velocity_modifier - 1.f) *
						biped->biped.crouch + 1.f;
				}
			}

			physics.movement_desired.i =
				definition->biped.flying_velocity * crouch_modifier * movement_scale;
			physics.movement_desired.j =
				definition->biped.flying_sidestep_velocity * crouch_modifier *
				movement_scale;
			physics.movement_desired.k =
				definition->biped.flying_sidestep_velocity * crouch_modifier *
				movement_scale;
			acceleration = (1.f - deceleration_scale) *
				definition->biped.flying_acceleration +
				definition->biped.flying_deceleration * deceleration_scale;
			physics.movement_desired.i *=
				biped->unit.throttle.i / TICKS_PER_SECOND;
			physics.movement_desired.j *=
				biped->unit.throttle.j / TICKS_PER_SECOND;
			physics.movement_desired.k *=
				biped->unit.throttle.k / TICKS_PER_SECOND;
			physics.acceleration_maximum =
				acceleration * crouch_modifier * movement_scale /
				TICKS_PER_SECOND;
			physics.airborne_acceleration_maximum =
				physics.acceleration_maximum;
		}

		if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
			biped->biped.airborne_ticks < 22 &&
			biped->unit.actor_index != NONE &&
			actor_is_leaping(biped->unit.actor_index))
		{
			physics.ground_tangential_velocity_max = 0.1f;
			physics.ground_tangential_angle = 0.5f;
		}

		physics.movement_penalty = 0.f;
	}

	{
		real crouch_delta;
		real crouch_transition_velocity =
			definition->biped.runtime_crouch_transition_velocity;

		if (biped->unit.animation.base_seat_index == _unit_base_seat_crouch)
		{
			crouch_delta = 1.f - biped->biped.crouch;
			if (crouch_delta <= crouch_transition_velocity)
			{
				biped->biped.crouch = 1.f;
			}
			else
			{
				crouch_delta = crouch_transition_velocity;
				biped->biped.crouch += crouch_transition_velocity;
			}
		}
		else
		{
			crouch_delta = -biped->biped.crouch;
			if (crouch_delta >= -crouch_transition_velocity)
			{
				biped->biped.crouch = 0.f;
			}
			else
			{
				crouch_delta = -crouch_transition_velocity;
				biped->biped.crouch -= crouch_transition_velocity;
			}
		}

		if (fabs(crouch_delta) > 0.01f &&
			TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			physics.crouch_velocity =
				(definition->biped.collision_height_standing -
					definition->biped.collision_height_crouching) * crouch_delta;
		}
	}

	in_flags = physics.in_flags;
	if (biped->biped.crouch != 0.f)
	{
		SET_FLAG(in_flags, _biped_physics_in_crouched_bit, TRUE);
		if (!animation->crouching)
			SET_FLAG(in_flags, _biped_physics_in_trying_to_stand_bit, TRUE);
	}
	if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		SET_FLAG(in_flags, _biped_physics_in_airborne_bit, TRUE);
	if (TEST_FLAG(biped->biped.flags, _biped_slipping_bit))
		SET_FLAG(in_flags, _biped_physics_in_slipping_bit, TRUE);
	if (TEST_FLAG(biped->biped.flags, _biped_absolute_movement_bit))
		SET_FLAG(in_flags, _biped_physics_in_absolute_movement_bit, TRUE);
	if (TEST_FLAG(biped->biped.flags, _biped_no_collision_bit))
		SET_FLAG(in_flags, _biped_physics_in_no_collision_bit, TRUE);
	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		SET_FLAG(in_flags, _biped_physics_in_dead_bit, TRUE);
	if (TEST_FLAG(definition->biped.flags, _biped_flying_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		SET_FLAG(in_flags, _biped_physics_in_flying_bit, TRUE);
	}
	if (TEST_FLAG(definition->biped.flags, _biped_passes_through_bipeds_bit))
		SET_FLAG(in_flags, _biped_physics_in_pass_through_bipeds_bit, TRUE);
	if (biped->unit.player_index != NONE && !network_coop_player_collisions())
		SET_FLAG(in_flags, _biped_physics_in_pass_through_players_bit, TRUE);
	if (TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit) &&
		!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		SET_FLAG(in_flags, _biped_physics_in_climb_anything_bit, TRUE);
	}
	physics.in_flags = in_flags;
	*result = physics;

	return TRUE;
}

/* port: a step's movement applied (the second half of the Xbox game's
biped_update_moving, the part of it each step of a tick makes): the biped
where biped_update_physics left it, its velocity, its flags of the ground
and the air, the ground under it */
static void biped_physics_apply_step(
	long biped_index,
	struct biped_physics *physics)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	real_point3d new_biped_position;
	word out_flags;

	if (TEST_FLAG(biped->unit.flags, _unit_suspended_bit))
	{
		physics->new_position = physics->position;
		SET_FLAG(physics->out_flags, _biped_physics_out_airborne_bit, FALSE);
		physics->new_velocity = *global_zero_vector3d;
		physics->velocity = *global_zero_vector3d;
	}

	new_biped_position = physics->new_position;
	out_flags = physics->out_flags;
	if (!TEST_FLAG(
		definition->biped.flags,
		_biped_pill_centered_at_origin_bit))
	{
		new_biped_position.z -= physics->width;
	}
	object_translate(biped_index, &new_biped_position, NULL);

	biped->biped.support_surface_index = physics->support_surface_index;
	biped->biped.pathfinding_point = new_biped_position;
	biped->biped.pathfinding_surface_index = NONE;
	biped->object.translational_velocity = physics->new_velocity;

	SET_FLAG(
		biped->biped.flags,
		_biped_airborne_bit,
		TEST_FLAG(out_flags, _biped_physics_out_airborne_bit));
	SET_FLAG(
		biped->biped.flags,
		_biped_slipping_bit,
		TEST_FLAG(out_flags, _biped_physics_out_slipping_bit));
	SET_FLAG(
		biped->biped.flags,
		_biped_movement_passes_through_bipeds_bit,
		TEST_FLAG(
			definition->biped.flags,
			_biped_passes_through_bipeds_bit));

	biped->biped.ground_plane = physics->ground_plane;

	return;
}

/* port: whether the biped is on the ground, and at rest, as its movement left
it */
static void biped_physics_ground_flags(
	long biped_index,
	struct biped_physics const *physics)
{
	struct biped_datum *biped = biped_get(biped_index);

	SET_FLAG(
		biped->object.flags,
		_object_on_ground_bit,
		!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit) &&
			!TEST_FLAG(biped->biped.flags, _biped_airborne_bit));
	SET_FLAG(
		biped->object.flags,
		_object_at_rest_bit,
		!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit) &&
			!TEST_FLAG(biped->biped.flags, _biped_airborne_bit) &&
			!TEST_FLAG(physics->out_flags, _biped_physics_out_volatile_collision_bit) &&
			magnitude_squared3d(&biped->object.translational_velocity) < 0.0001f);
	if (TEST_FLAG(biped->object.flags, _object_on_ground_bit))
		biped->object.angular_velocity = *global_zero_vector3d;

	return;
}

/* port: what a tick's movement leads to, once a tick, after its last step
(the rest of the Xbox game's biped_update_moving): the elevator it rides,
whether it can stand, a landing, a jump, a melee's strike (swept over the
tick's movement, from physics->position to physics->new_position), a bump,
falling damage */
static void biped_physics_apply_tick(
	long biped_index,
	struct biped_physics *physics,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);
	word out_flags = physics->out_flags;

	if (physics->elevator_object_index == NONE)
	{
		if (biped->biped.elevator_ticks <= 0)
			biped->biped.elevator_object_index = NONE;
		else
			--biped->biped.elevator_ticks;
	}
	else
	{
		biped->biped.elevator_object_index = physics->elevator_object_index;
		biped->biped.elevator_ticks = 60;
	}

	if (!animation->crouching &&
		TEST_FLAG(out_flags, _biped_physics_out_cannot_stand_bit))
	{
		animation->crouching = TRUE;
	}

	if (physics->landing_velocity > 0.f)
		biped_start_landing(biped_index, physics->landing_velocity);
	if (!TEST_FLAG(out_flags, _biped_physics_out_cannot_stand_bit))
		biped_update_jumping(biped_index, animation);

	if (biped->unit.melee_attack_state == _unit_melee_attack_impact &&
		biped->biped.impact_target_object_index != NONE)
	{
		struct object_datum *target = object_get(
			biped->biped.impact_target_object_index);
		real_vector3d melee_vector;

		match_assert(
			"c:\\halo\\SOURCE\\units\\bipeds.c",
			0x87A,
			global_current_collision_user_depth <
				MAXIMUM_COLLISION_USER_STACK_DEPTH);
		global_current_collision_users[global_current_collision_user_depth++] =
			_collision_user_bipeds;

		vector_from_points3d(
			&physics->position,
			&physics->new_position,
			&melee_vector);
		if (fast_vector_intersects_sphere(
			&physics->position,
			&melee_vector,
			&target->object.bounding_sphere_center,
			target->object.bounding_sphere_radius))
		{
			struct collision_model_instance instance;
			struct collision_model_test_vector_result vector_result;
			struct collision_result collision;

			if (collision_model_instance_new(
				&instance,
				biped->biped.impact_target_object_index) &&
				collision_model_test_vector(
					&instance,
					FLAG(_collision_test_front_facing_surfaces_bit) |
						FLAG(_collision_test_back_facing_surfaces_bit),
					&physics->position,
					&melee_vector,
					&vector_result) &&
				!collision_test_vector(
					_collision_test_for_bipeds_passthrough_living_flags,
					&physics->position,
					&melee_vector,
					biped_index,
					&collision))
			{
				real_point3d collision_point;
				real_plane3d collision_plane;

				point_from_line3d(
					&physics->position,
					&melee_vector,
					vector_result.bsp_result.t,
					&collision_point);
				matrix4x3_transform_plane(
					&instance.matrices[vector_result.node_index],
					vector_result.bsp_result.plane,
					&collision_plane);
				if (vector_result.bsp_result.plane_designator < 0)
					plane3d_negate(&collision_plane, &collision_plane);
				unit_impact_melee_damage(
					biped_index,
					biped->biped.impact_target_object_index,
					vector_result.node_index,
					vector_result.region_index,
					vector_result.bsp_result.material_index,
					&collision_point,
					&collision_plane.n,
					&collision.location);
			}
		}

		match_assert(
			"c:\\halo\\SOURCE\\units\\bipeds.c",
			0x8A5,
			global_current_collision_user_depth > 1);
		--global_current_collision_user_depth;
	}

	biped_bumped_object(
		biped_index,
		physics->bumped_object_index,
		&physics->velocity);
	biped_falling_damage(biped_index, physics->landing_velocity);

	biped_physics_ground_flags(biped_index, physics);

	return;
}

/* port: the velocity a biped's first sub-step of a tick starts from (velocity,
the object's now): the one the biped kept (kept, as the tick's step at 30
starts from it) and what changed the object's since biped_physics_prepare
(prepared, as it left it: a bump, a blast in the rest of the objects'
update). A velocity biped_physics_prepare set on the object itself (an
animation's root motion: the AI's walk, a player's scripted animation),
which the tick's step at 30 overwrites unread, is the movement the step
wants, not where it starts */
static void biped_substep_start_velocity(
	real_vector3d const *kept,
	real_vector3d const *prepared,
	real_vector3d *velocity)
{
	if (prepared->i != kept->i)
		velocity->i = kept->i + (velocity->i - prepared->i);
	if (prepared->j != kept->j)
		velocity->j = kept->j + (velocity->j - prepared->j);
	if (prepared->k != kept->k)
		velocity->k = kept->k + (velocity->k - prepared->k);

	return;
}

/* port: whether a biped moves in the fast phase this tick (biped_substep_globals):
a player's of the micro lane (objects.h: on foot) in a tick of more than one
sub-step, made ready in the objects' update (an AI's moves its sub-steps in
its update: biped_substep_inline). At one (30) the fast phase's one step is
the tick's own, which the biped takes in its place in the objects' update,
in the order the game always had */
static boolean biped_moves_in_substeps(
	long biped_index)
{
	return sim_substep_index() == NONE &&
		biped_get(biped_index)->unit.player_index != NONE &&
		object_lane_moves_in_substeps(biped_index);
}

/* port: the tick's movement left for the fast phase, if the biped moves in it;
its crouch (crouch_start before biped_physics_prepare changed it) put back
where the tick starts it, for the sub-steps to change */
static boolean biped_substep_defer(
	long biped_index,
	struct biped_physics const *physics,
	struct unit_animation_update_data const *animation,
	struct biped_player_speeds const *speeds,
	real crouch_start)
{
	long tick = game_time_get();
	struct biped_substep_datum *datum = NULL;
	short index;

	if (!biped_moves_in_substeps(biped_index))
		return FALSE;

	/* (another tick's, which no fast phase took, gone) */
	if (biped_substep_globals.count > 0 && biped_substep_globals.bipeds[0].tick != tick)
		biped_substep_globals.count = 0;
	for (index = 0; index < biped_substep_globals.count; index++)
	{
		if (biped_substep_globals.bipeds[index].biped_index == biped_index)
			datum = &biped_substep_globals.bipeds[index];
	}
	if (!datum)
	{
		if (biped_substep_globals.count >= MAXIMUM_SUBSTEP_BIPEDS)
			return FALSE;
		datum = &biped_substep_globals.bipeds[biped_substep_globals.count++];
	}

	datum->biped_index = biped_index;
	datum->tick = tick;
	datum->physics = *physics;
	datum->prepared_velocity = biped_get(biped_index)->object.translational_velocity;
	datum->animation = *animation;
	datum->speeds = *speeds;
	datum->tick_position = physics->position;
	datum->landing_velocity = 0.f;
	datum->elevator_object_index = NONE;
	datum->bumped_object_index = NONE;
	datum->bumped_velocity = physics->velocity;
	datum->jumped = FALSE;
	datum->launch_velocity = *global_zero_vector3d;
	datum->crouch_start = crouch_start;
	datum->crouch_end = biped_get(biped_index)->biped.crouch;
	biped_get(biped_index)->biped.crouch = crouch_start;

	return TRUE;
}

/* port: an object moved in a sub-step, after the tick's nodes were made: its
nodes and bounding sphere moved with it, and its children's (the weapon it
holds) */
static void biped_carry_nodes(
	long object_index,
	real_vector3d const *moved)
{
	struct object_datum *object = object_get(object_index);
	short node_count = (short)(object->object.node_matrices.size / sizeof(real_matrix4x3));
	long child_index;

	if (node_count > 0)
	{
		real_matrix4x3 *matrices = (real_matrix4x3 *)object_header_block_get(
			object_index,
			&object->object.node_matrices);
		short node_index;

		for (node_index = 0; node_index < node_count; node_index++)
		{
			matrices[node_index].position.x += moved->i;
			matrices[node_index].position.y += moved->j;
			matrices[node_index].position.z += moved->k;
		}
	}
	object->object.bounding_sphere_center.x += moved->i;
	object->object.bounding_sphere_center.y += moved->j;
	object->object.bounding_sphere_center.z += moved->k;

	for (child_index = object->object.first_child_object_index;
		child_index != NONE;
		child_index = object_get(child_index)->object.next_object_index)
	{
		biped_carry_nodes(child_index, moved);
	}

	return;
}

/* port: the biped's nodes made again after its tick's end in the fast phase
(its animation stepped after the objects' update made them), as the objects'
update makes them: its children's too, and the unit's aim put in */
static void biped_refresh_nodes(
	long biped_index)
{
	struct biped_datum *biped = biped_try_and_get(biped_index);
	struct object_definition *definition;

	if (!biped || TEST_FLAG(biped->object.flags, _object_do_not_recompute_node_matrices_bit))
		return;

	object_compute_node_matrices_recursive(biped_index);
	definition = object_definition_get(biped->definition_index);
	if (definition->object.model.index != NONE &&
		definition->object.animation_graph.index != NONE)
	{
		object_type_postprocess_node_matrices(
			biped_index,
			(real_matrix4x3 *)object_header_block_get(biped_index, &biped->object.node_matrices));
	}

	return;
}

/* port: one sub-step of a biped's tick (bipeds_substep's, biped_substep_inline's):
the biped moved a sub-step's share of its tick, from where it is (a lift
carries a player's a share first, objects.c's slot) as fast as it goes, with
its input (a player's, again, past the first) and a sub-step's share of the
tick's change of velocity (biped_update_physics); what it met kept in datum,
and the step's movement in result */
static void biped_substep_move(
	long biped_index,
	struct biped_substep_datum *datum,
	short substep_index,
	real tick_fraction,
	struct biped_physics *result)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_physics physics;
	real_point3d old_position;
	real_vector3d moved;
	real gravity;
	real_vector3d const *tick_velocity;
	real_plane3d const *tick_ground;

	physics = datum->physics;
	biped_get_physics_pill(
		biped_index,
		&physics.position,
		&physics.height,
		&physics.width);
	physics.velocity = biped->object.translational_velocity;
	if (substep_index == 0)
		biped_substep_start_velocity(&datum->physics.velocity, &datum->prepared_velocity, &physics.velocity);
	physics.ground_plane = biped->biped.ground_plane;
	physics.existing_support_surface_index = biped->biped.support_surface_index;
	/* (a sub-step past the first, whose input the player's is again,
	player_control.c: the movement its throttle wants, and the way it
	faces, as biped_update and biped_physics_prepare make them of the
	tick's. The AI's input is the tick's: what it wants of every sub-step
	is what biped_physics_prepare made) */
	if (substep_index > 0 && datum->speeds.valid && biped->unit.player_index != NONE)
	{
		real_vector3d throttle = biped->unit.throttle;

		if (magnitude_squared3d(&throttle) < 0.1f * 0.1f)
			throttle = *global_zero_vector3d;
		biped_player_movement(&datum->speeds, &throttle, &physics.movement_desired);
		if (!TEST_FLAG(biped->unit.control_flags, _unit_control_look_dont_turn_bit))
		{
			struct biped_definition *definition = biped_definition_get(biped->definition_index);

			physics.forward = biped->unit.desired_facing_vector;
			if (!(definition->biped.flags &
				(FLAG(_biped_flying_bit) | FLAG(_biped_climbs_anything_bit))))
			{
				physics.forward.k = 0.f;
				if (normalize3d(&physics.forward) == 0.f)
					physics.forward = *global_forward3d;
			}
		}
	}
	if (substep_index == 0)
		datum->tick_position = physics.position;

	/* (at 30 a tick's movement is in the air or on the ground, and what
	it meets changes that at the tick's end: so an arc starts and ends
	at a tick's end, biped_update_physics's launch bias from there. A
	biped in the air at the tick's start stays in the air to its end,
	a landing on the ground clipping its fall: the tick's step slides on
	along the plane it lands on, so the sub-steps after the one that
	lands do (their movement clipped by it, on the ground), rather than
	falling again onto what lies past it (a rounded corner); but on
	what moves or breaks within the tick, or by a snap, they fall and
	meet the ground themselves, as below. A jump launched at an
	earlier sub-step's end (biped_substep_jump) is the arc a tick at 30
	starts at its end: in the air from there, with gravity, the change
	of velocity worked out from the velocity it launched at, as the
	first tick of a jump at 30 works it out from its start. Slipping or
	not is each sub-step's.

	On the ground at the tick's start, whether it stays there is the
	tick's own step's to say. At 30 the push into the ground meets the
	ground once a tick, at the step's start, and the step slides on
	along the plane it met; a step that meets none in the whole of its
	movement leaves the ground. A sub-step's push, its share of the
	tick's over its share of the movement, meets ground the tick's step
	passes over: off an edge it would follow the rounded corner down,
	still on the ground, and leave it later and faster than the tick.
	So the first sub-step takes the tick's step from the tick's start,
	as at 30, without moving, and:
	- if it stands on the ground it met, every sub-step slides on along
	  that plane (its movement clipped by it), on the ground at the
	  tick's start, the tick's push given once, at the first sub-step,
	  as the tick's step gives it at its start; and the tick ends on
	  what the step stood on;
	- if it meets no ground, the biped leaves the ground at the tick's
	  start at the velocity the step leaves at, and goes on as it does,
	  without gravity, to the tick's end;
	- if it stands on the next surface by its snap (the sub-steps snap
	  their shares), or what it met moves or breaks within the tick (a
	  lift, a vehicle, glass: its volatile collision), each sub-step
	  probes the ground itself, and one that leaves it goes on as it
	  left it, without gravity, to the tick's end) */
	gravity = global_gravity;
	tick_velocity = &datum->physics.velocity;
	tick_ground = NULL;
	SET_FLAG(physics.in_flags, _biped_physics_in_slipping_bit,
		TEST_FLAG(biped->biped.flags, _biped_slipping_bit));
	if (substep_index == 0)
	{
		datum->ground = _biped_substep_ground_probed;
		if (!TEST_FLAG(physics.in_flags, _biped_physics_in_airborne_bit) &&
			!TEST_FLAG(physics.in_flags, _biped_physics_in_flying_bit) &&
			!TEST_FLAG(physics.in_flags, _biped_physics_in_absolute_movement_bit) &&
			!TEST_FLAG(physics.in_flags, _biped_physics_in_no_collision_bit) &&
			!TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			struct biped_physics tick = physics;

			biped_update_physics(&tick, 1.f, gravity, tick_velocity, NULL);
			if (TEST_FLAG(tick.out_flags, _biped_physics_out_airborne_bit))
			{
				datum->ground = _biped_substep_ground_left;
				datum->leave_velocity = tick.new_velocity;
			}
			else if (tick.stick_surface_index == NONE &&
				!TEST_FLAG(tick.out_flags, _biped_physics_out_volatile_collision_bit))
			{
				datum->ground = _biped_substep_ground_tick;
				datum->ground_plane = tick.ground_plane;
				datum->support_surface_index = tick.support_surface_index;
				datum->landing_velocity = tick.landing_velocity;
				datum->elevator_object_index = tick.elevator_object_index;
			}
		}
	}

	if (datum->jumped)
	{
		SET_FLAG(physics.in_flags, _biped_physics_in_airborne_bit, TRUE);
		tick_velocity = &datum->launch_velocity;
	}
	else if (datum->ground == _biped_substep_ground_tick)
	{
		physics.ground_plane = datum->physics.ground_plane;
		physics.existing_support_surface_index = NONE;
		SET_FLAG(physics.in_flags, _biped_physics_in_pushed_bit, substep_index > 0);
		tick_ground = &datum->ground_plane;
	}
	else if (datum->ground == _biped_substep_ground_landed)
	{
		SET_FLAG(physics.in_flags, _biped_physics_in_pushed_bit, TRUE);
		tick_ground = &datum->ground_plane;
	}
	else if (datum->ground == _biped_substep_ground_left)
	{
		/* (the tick's change of velocity is in the velocity it leaves at,
		as the step left it: none in the air besides) */
		SET_FLAG(physics.in_flags, _biped_physics_in_airborne_bit, TRUE);
		gravity = 0.f;
		physics.airborne_acceleration_maximum = 0.f;
		tick_velocity = &datum->leave_velocity;
		if (substep_index == 0)
			physics.velocity = datum->leave_velocity;
	}
	else if (!TEST_FLAG(datum->physics.in_flags, _biped_physics_in_airborne_bit) &&
		TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
	{
		SET_FLAG(physics.in_flags, _biped_physics_in_airborne_bit, TRUE);
		gravity = 0.f;
	}

	old_position = biped->object.position;
	biped_update_physics(&physics, tick_fraction, gravity, tick_velocity, tick_ground);
	if (tick_ground)
	{
		/* (on what the tick's step stood on, its landing the step's) */
		SET_FLAG(physics.out_flags, _biped_physics_out_airborne_bit, FALSE);
		physics.ground_plane = datum->ground_plane;
		physics.support_surface_index = datum->support_surface_index;
		physics.landing_velocity = 0.f;
	}
	else if (datum->ground == _biped_substep_ground_probed &&
		TEST_FLAG(datum->physics.in_flags, _biped_physics_in_airborne_bit) &&
		!TEST_FLAG(physics.out_flags, _biped_physics_out_airborne_bit) &&
		!TEST_FLAG(physics.out_flags, _biped_physics_out_volatile_collision_bit) &&
		physics.stick_surface_index == NONE)
	{
		/* (landed, in a tick it started in the air: the rest of the tick
		on the plane it landed on) */
		datum->ground = _biped_substep_ground_landed;
		datum->ground_plane = physics.ground_plane;
		datum->support_surface_index = physics.support_surface_index;
	}
	biped_physics_apply_step(biped_index, &physics);
	vector_from_points3d(&old_position, &biped->object.position, &moved);
	biped_carry_nodes(biped_index, &moved);

	if (physics.landing_velocity > datum->landing_velocity)
		datum->landing_velocity = physics.landing_velocity;
	if (physics.elevator_object_index != NONE)
		datum->elevator_object_index = physics.elevator_object_index;
	if (physics.bumped_object_index != NONE)
	{
		datum->bumped_object_index = physics.bumped_object_index;
		datum->bumped_velocity = physics.velocity;
	}
	*result = physics;

	return;
}

/* port: a tick's movement, its sub-steps over, as one (from the first
sub-step's start to the last's end, and what any of its sub-steps met),
for biped_physics_apply_tick */
static void biped_substep_tick_physics(
	struct biped_substep_datum const *datum,
	struct biped_physics *physics)
{
	physics->position = datum->tick_position;
	physics->landing_velocity = datum->landing_velocity;
	physics->elevator_object_index = datum->elevator_object_index;
	physics->bumped_object_index = datum->bumped_object_index;
	physics->velocity = datum->bumped_velocity;

	return;
}

/* port: whether a tick's movement on the ground is too small for its
sub-steps: the collisions (collision_move_point) stop a movement once each
of its components is under _real_epsilon, so a sub-step of a slow drift (an
idle animation's root motion, a few hundredths of a world unit a second)
would not move at all where the tick at 30 does. Such a tick (its velocity
and the movement it wants each under a sub-step's count of _real_epsilon a
tick) moves as at 30, in one step */
static boolean biped_substep_too_small(
	struct biped_physics const *physics,
	short substep_count)
{
	real limit = _real_epsilon * (real)substep_count;

	return !TEST_FLAG(physics->in_flags, _biped_physics_in_airborne_bit) &&
		!TEST_FLAG(physics->in_flags, _biped_physics_in_flying_bit) &&
		magnitude_squared3d(&physics->movement_desired) < limit * limit &&
		physics->velocity.i * physics->velocity.i + physics->velocity.j * physics->velocity.j < limit * limit;
}

/* port: an AI's biped's tick in its update at game.sim_rate 60 or 120 (the
globals' comment, above): its sub-steps one after the other, where each
left it kept for the fast phase (biped_replay_globals), then what the
tick's movement leads to (biped_physics_apply_tick); the rest of its update
follows in biped_update, as at 30. FALSE for a player's biped (the fast
phase's, biped_substep_defer) and in a tick of one sub-step */
static boolean biped_substep_inline(
	long biped_index,
	struct biped_physics const *physics,
	struct unit_animation_update_data *animation,
	struct biped_player_speeds const *speeds)
{
	struct biped_substep_datum datum;
	struct biped_replay_datum *replay = NULL;
	struct biped_physics step;
	short substep_count = sim_substep_count();
	real tick_fraction;
	short substep_index;

	if (sim_substep_index() != NONE || !object_lane_moves_in_substeps(biped_index) ||
		substep_count > MAXIMUM_SIM_SUBSTEPS)
	{
		return FALSE;
	}
	/* (a sub-step's share of a tick: 1/2 or 1/4, exactly) */
	tick_fraction = 1.f / (real)substep_count;

	/* (where its sub-steps leave it, for the fast phase: a tick's, from
	the first biped of it on) */
	if (biped_replay_globals.count > 0 && biped_replay_globals.bipeds[0].tick != game_time_get())
		biped_replay_globals.count = 0;
	if (biped_replay_globals.count >= biped_replay_globals.capacity &&
		biped_replay_globals.capacity < MAXIMUM_REPLAYED_BIPEDS)
	{
		long capacity = MIN(MAX(2 * biped_replay_globals.capacity, 64), MAXIMUM_REPLAYED_BIPEDS);
		struct biped_replay_datum *bipeds = realloc(biped_replay_globals.bipeds,
			capacity * sizeof(struct biped_replay_datum));

		if (bipeds)
		{
			biped_replay_globals.bipeds = bipeds;
			biped_replay_globals.capacity = capacity;
		}
	}
	if (biped_replay_globals.count < biped_replay_globals.capacity)
		replay = &biped_replay_globals.bipeds[biped_replay_globals.count++];

	datum.biped_index = biped_index;
	datum.tick = game_time_get();
	datum.physics = *physics;
	datum.prepared_velocity = biped_get(biped_index)->object.translational_velocity;
	datum.animation = *animation;
	datum.speeds = *speeds;
	datum.tick_position = physics->position;
	datum.landing_velocity = 0.f;
	datum.elevator_object_index = NONE;
	datum.bumped_object_index = NONE;
	datum.bumped_velocity = physics->velocity;
	datum.jumped = FALSE;
	datum.launch_velocity = *global_zero_vector3d;

	if (biped_substep_too_small(physics, substep_count))
	{
		/* (a tick's movement on the ground smaller than a sub-step can make
		of it: the tick's step, as at 30, its sub-steps on the line) */
		real_point3d start = biped_get(biped_index)->object.position;
		real_point3d const *end;

		step = *physics;
		biped_update_physics(&step, 1.f, global_gravity, &step.velocity, NULL);
		biped_physics_apply_step(biped_index, &step);
		biped_physics_apply_tick(biped_index, &step, animation);
		end = &biped_get(biped_index)->object.position;
		for (substep_index = 0; replay && substep_index < substep_count; substep_index++)
		{
			real t = (real)(substep_index + 1) * tick_fraction;

			replay->poses[substep_index].x = start.x + (end->x - start.x) * t;
			replay->poses[substep_index].y = start.y + (end->y - start.y) * t;
			replay->poses[substep_index].z = start.z + (end->z - start.z) * t;
		}
		if (replay)
			replay->poses[substep_count - 1] = *end;
	}
	else
	{
		for (substep_index = 0; substep_index < substep_count; substep_index++)
		{
			/* (on the macro lane where the sub-step sees it, a lift it
			stands on a share of its movement: objects.h) */
			object_lane_view_substep(biped_index, substep_index);
			biped_substep_move(biped_index, &datum, substep_index, tick_fraction, &step);
			if (replay)
				replay->poses[substep_index] = biped_get(biped_index)->object.position;
			if (substep_index < substep_count - 1)
				biped_physics_ground_flags(biped_index, &step);
			object_lane_view_substep(biped_index, NONE);
		}
		biped_substep_tick_physics(&datum, &step);
		biped_physics_apply_tick(biped_index, &step, animation);
	}

	if (replay)
	{
		replay->biped_index = biped_index;
		replay->tick = datum.tick;
		replay->substep_count = substep_count;
		replay->last = biped_get(biped_index)->object.position;
		replay->carried = *global_zero_vector3d;
	}

	return TRUE;
}

/* port: at the end of an AI's biped's update (biped_update), its sub-steps
kept (biped_substep_inline) for the fast phase, unless the rest of its
update moved it (a teleport): then it waits where it is */
static void biped_replay_seal(
	long biped_index)
{
	struct biped_replay_datum *replay;
	real_point3d const *position;

	if (biped_replay_globals.count <= 0)
		return;
	replay = &biped_replay_globals.bipeds[biped_replay_globals.count - 1];
	if (replay->biped_index != biped_index || replay->tick != game_time_get())
		return;
	position = &biped_get(biped_index)->object.position;
	if (position->x != replay->last.x || position->y != replay->last.y || position->z != replay->last.z)
		replay->tick = NONE;

	return;
}

/* port: the AI's bipeds at a sub-step (bipeds_substep, before the players'
move): each where its sub-step left it in its update, and what moved it
since besides (a lift's shares, objects.c's slot) with it; at the tick's
last, where its update left it (and that) */
static void bipeds_replay_substep(
	short substep_index,
	boolean last)
{
	long tick = game_time_get();
	long index;

	for (index = 0; index < biped_replay_globals.count; index++)
	{
		struct biped_replay_datum *replay = &biped_replay_globals.bipeds[index];
		struct biped_datum *biped;
		real_point3d target;
		real_vector3d moved;
		real_vector3d lifted;

		if (replay->tick != tick || substep_index >= replay->substep_count)
			continue;
		biped = biped_try_and_get(replay->biped_index);
		if (!biped || biped->object.parent_object_index != NONE)
		{
			replay->tick = NONE;
			continue;
		}
		/* (moved since by something else: exactly nothing, most often) */
		replay->carried.i += biped->object.position.x - replay->last.x;
		replay->carried.j += biped->object.position.y - replay->last.y;
		replay->carried.k += biped->object.position.z - replay->last.z;
		target = replay->poses[substep_index];
		if (replay->carried.i != 0.f || replay->carried.j != 0.f || replay->carried.k != 0.f)
		{
			target.x += replay->carried.i;
			target.y += replay->carried.j;
			target.z += replay->carried.k;
		}
		/* (a lift's carry, the tick's whole movement, before its update or
		after it: only its share so far, the rest still to come) */
		if (!last && machine_elevator_carried(replay->biped_index, &lifted))
		{
			real still_to_come = 1.f - sim_substep_end_fraction();

			target.x -= lifted.i * still_to_come;
			target.y -= lifted.j * still_to_come;
			target.z -= lifted.k * still_to_come;
		}
		vector_from_points3d(&biped->object.position, &target, &moved);
		if (moved.i != 0.f || moved.j != 0.f || moved.k != 0.f)
		{
			object_translate(replay->biped_index, &target, NULL);
			biped_carry_nodes(replay->biped_index, &moved);
		}
		replay->last = target;
	}
	if (last)
	{
		biped_replay_globals.count = 0;
		machine_elevator_carried_forget();
	}

	return;
}

/* port: a player's jump at the end of a sub-step before the tick's last, its
press read at this sub-step (player_control.c: a sub-step past the first
reads the controller again), as a tick at 30 launches a jump at its end, its
press read at its start: so it takes off a sub-step after the press is
read, not at the tick's end. Whether it can is the tick's own test
(biped_physics_apply_tick's and biped_update_jumping's) of the state as the
sub-step left it: standing room, on the ground, no hard landing, and the
recovery since the last jump with this tick's count in it (the tick counts
it at its end before it tests, as it would were the biped still on the
ground there). A biped in the air at the tick's start is not tested: its
landing is the tick's, at its end (biped_substep_move), and the tick's own
test follows it. The tick's last sub-step leaves its press to the tick's
test, which runs there; the tick's own input (its first sub-step's) too.
The counters of ticks are left to the tick: launched here, the biped is in
the air at the tick's end, so the tick neither counts the recovery nor
tests again, and jump_recovery_timer is 0 there as at 30 */
static void biped_substep_jump(
	long biped_index,
	struct biped_substep_datum *datum,
	struct biped_physics const *physics)
{
	struct biped_datum *biped = biped_get(biped_index);
	char recovery = biped->biped.jump_recovery_timer;

	if (datum->jumped ||
		biped->unit.player_index == NONE ||
		!TEST_FLAG(player_control_substep_pressed_flags(biped_index), _unit_control_jump_bit) ||
		!TEST_FLAG(biped->unit.control_flags, _unit_control_jump_bit) ||
		TEST_FLAG(datum->physics.in_flags, _biped_physics_in_airborne_bit) ||
		TEST_FLAG(physics->out_flags, _biped_physics_out_cannot_stand_bit) ||
		TEST_FLAG(biped->biped.flags, _biped_airborne_bit) ||
		biped->biped.landing == 1)
	{
		return;
	}
	if (recovery < SCHAR_MAX)
		++recovery;
	if (recovery > 5 && biped_jump(biped_index))
	{
		datum->jumped = TRUE;
		datum->launch_velocity = biped->object.translational_velocity;
	}

	return;
}

/* port: the fast phase's _sim_substep_bipeds slot (sim_rate.h): the AI's
bipeds where their sub-steps left them (bipeds_replay_substep), then each
biped left by biped_update moves a sub-step's share of its tick
(biped_substep_move); at the last sub-step what the tick's movement leads
to, and the rest of biped_update, once */
static void bipeds_substep(
	short substep_index,
	short substep_count,
	real seconds)
{
	real tick_fraction = sim_substep_tick_fraction();
	boolean last = substep_index == substep_count - 1;
	long tick = game_time_get();
	short index;

	(void)seconds;

	bipeds_replay_substep(substep_index, last);
	for (index = 0; index < biped_substep_globals.count; index++)
	{
		struct biped_substep_datum *datum = &biped_substep_globals.bipeds[index];
		long biped_index = datum->biped_index;
		struct biped_datum *biped;
		struct biped_physics physics;

		if (datum->tick != tick)
			continue;
		biped = biped_try_and_get(biped_index);
		if (!biped || biped->object.parent_object_index != NONE)
		{
			/* (its crouch the tick's, as the tick would have left it) */
			if (biped)
				biped->biped.crouch = datum->crouch_end;
			datum->tick = NONE;
			continue;
		}

		profile_enter(biped_update_section);
		biped_substep_move(biped_index, datum, substep_index, tick_fraction, &physics);
		/* (its crouch its share of the tick's change of it, which the
		sub-step moved with as the tick's step moves with the crouch it
		starts with: the last the tick's own) */
		biped->biped.crouch = last ? datum->crouch_end :
			datum->crouch_start + (datum->crouch_end - datum->crouch_start) * sim_substep_end_fraction();

		if (!last)
		{
			biped_substep_jump(biped_index, datum, &physics);
			biped_physics_ground_flags(biped_index, &physics);
		}
		else
		{
			struct unit_animation_update_data animation = datum->animation;

			biped_substep_tick_physics(datum, &physics);
			biped_physics_apply_tick(biped_index, &physics, &animation);
			biped_verify_object_vectors(biped_index, "post-moving");
			biped_update_on_foot(biped_index, &animation);
			biped_update_end(biped_index, &animation);
			biped_refresh_nodes(biped_index);
			datum->tick = NONE;
		}
		profile_exit(biped_update_section);
	}
	if (last)
		biped_substep_globals.count = 0;

	return;
}

/* the biped's movement this tick (port: biped_physics_prepare, then its steps
applied): TRUE when it waits for the fast phase (biped_substep_defer) */
static boolean biped_update_moving(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_physics physics;
	struct biped_player_speeds speeds;
	real crouch = biped_get(biped_index)->biped.crouch;

	if (!biped_physics_prepare(biped_index, animation, &physics, &speeds))
		return FALSE;
	if (biped_substep_defer(biped_index, &physics, animation, &speeds, crouch))
		return TRUE;
	if (biped_substep_inline(biped_index, &physics, animation, &speeds))
		return FALSE;

	biped_update_physics(&physics, 1.f, global_gravity, &physics.velocity, NULL);
	biped_physics_apply_step(biped_index, &physics);
	biped_physics_apply_tick(biped_index, &physics, animation);

	return FALSE;
}

boolean biped_new(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);

	biped->biped.ground_plane = depths_of_hell;
	biped->biped.jump_recovery_timer = 127;
	biped->biped.support_surface_index = NONE;
	biped->biped.pathfinding_surface_index = NONE;
	object_get_origin(biped_index, &biped->biped.pathfinding_point);
	biped->biped.last_pathfinding_surface_index = NONE;
	biped->biped.last_pathfinding_attempt_time = NONE;
	biped->biped.impact_target_object_index = NONE;
	if (TEST_FLAG(definition->biped.flags, _biped_climbs_anything_bit))
		biped_find_nearby_support_surface(biped_index);
	biped_snap_facing(biped_index);
	biped->biped.elevator_object_index = NONE;
	biped->biped.elevator_ticks = 0;

	return TRUE;
}

static boolean biped_check_discard(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);

	/* port: not the host's biped on a client, which the host erases: the
	delete was refused, and logged as done every tick as it fell */
	if (!game_engine_running() &&
		(TEST_FLAG(biped->object.flags, _object_outside_of_map_bit) ||
		biped->object.location.cluster_index==NONE) &&
		biped->object.position.z<-2000.f &&
		network_objects_may_delete(biped_index))
	{
		long actor_index = biped->unit.swarm_actor_index;

		if (actor_index==NONE)
			actor_index = biped->unit.actor_index;

		error(
			_error_silent,
			"WARNING: biped %s (%s) is in a bad place (%.1f %.1f %.1f), erasing",
			tag_name_strip_path(tag_get_name(biped->definition_index)),
			ai_debug_describe_actor(
				actor_index,
				biped_index,
				TRUE,
				temporary,
				256),
			biped->object.position.x,
			biped->object.position.y,
			biped->object.position.z);
		object_delete(biped_index);
	}

	return FALSE;
}

/* port: the rest of a biped on foot's update after its movement (biped_update's,
which a biped moved in the fast phase runs after its last sub-step) */
static void biped_update_on_foot(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);

	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		biped_update_dead(biped_index, animation);
	}
	else if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
	{
		biped_update_airborne(biped_index, animation);
	}
	else if (biped->biped.landing!=NONE)
	{
		biped_update_landing(biped_index, animation);
	}
	else if (TEST_FLAG(biped->biped.flags, _biped_slipping_bit))
	{
		biped_update_slipping(biped_index, animation);
	}

	biped_verify_object_vectors(biped_index, "post-dead/air/land/slip");

	if (!biped->biped.player_melee_ticks)
	{
		if (biped->unit.player_index!=NONE &&
			TEST_FLAG(biped->unit.control_flags, _unit_control_use_equipment_bit))
		{
			long weapon_index = unit_inventory_get_weapon(
				biped_index,
				unit_get(biped_index)->unit.current_weapon_index);

			/* (port: and with no weapon, which prevents it in the
			Xbox game: a gametype's loadout of none; not from a
			vehicle's seat, which holds no weapon either) */
			if (((weapon_index == NONE && biped->unit.parent_seat_index == NONE) ||
				(weapon_index != NONE && !weapon_prevents_melee_attack(weapon_index))) &&
				biped->unit.current_zoom_level==NONE)
			{
				short melee_speedup_ticks;

				unit_animation_start_action(biped_index, _unit_animation_action_melee);
				/* port: a player with no weapon (a gametype's loadout of
				none) melees too: in a weapon's usual time, the hit
				halfway (the Xbox game read the timing from the weapon's
				animations, through a weapon it did not check) */
				if (weapon_index == NONE)
				{
					biped->biped.player_melee_ticks = UNARMED_MELEE_TICKS;
					biped->biped.player_melee_attack_tick = UNARMED_MELEE_TICKS / 2;
				}
				else
				{
				weapon_stop_reload(weapon_index);
				first_person_weapon_message_from_unit(
					biped_index,
					_first_person_weapon_message_melee);
				biped->biped.player_melee_ticks = weapon_get_first_person_animation_time(
					weapon_index,
					_weapon_first_person_animation_time_frame_count,
					_first_person_weapon_animation_melee,
					NONE);
				biped->biped.player_melee_attack_tick = biped->biped.player_melee_ticks -
					weapon_get_first_person_animation_time(
						weapon_index,
						_weapon_first_person_animation_time_private_key_frame,
						_first_person_weapon_animation_melee,
						NONE);
				}

				melee_speedup_ticks = biped->biped.player_melee_ticks >> 2;
				biped->biped.player_melee_ticks -= melee_speedup_ticks;
				biped->biped.player_melee_attack_tick -= melee_speedup_ticks;
			}
		}
	}
	else
	{
		if (biped->biped.player_melee_ticks==biped->biped.player_melee_attack_tick)
			unit_cause_player_melee_damage(biped_index);
		--biped->biped.player_melee_ticks;
	}

	biped_try_to_make_footsteps(biped_index);
	biped_falling_danger(biped_index);
	biped_check_discard(biped_index);

	return;
}

/* port: the end of every biped's update (biped_update's) */
static void biped_update_end(
	long biped_index,
	struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);

	if (unit_update_animation(biped_index, animation)==1)
		biped_jump(biped_index);

	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit) &&
		TEST_FLAG(biped->object.flags, _object_at_rest_bit))
	{
		++biped->object.idle_ticks;
	}
	else
	{
		biped->object.idle_ticks = 0;
	}

	biped_verify_object_vectors(biped_index, "post-update");

	return;
}

boolean biped_update(
	long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	struct biped_definition *definition = biped_definition_get(biped->definition_index);
	struct unit_animation_update_data animation;

	if (debug_biped_skip_update)
		return TRUE;

	profile_enter(biped_update_section);
	biped_verify_object_vectors(biped_index, "pre-update");

	animation.state_desired = _unit_state_idle;
	animation.crouching = FALSE;

	if (biped->object.parent_object_index!=NONE)
	{
		struct object_datum *parent = object_get(biped->object.parent_object_index);

		if (parent->object.type==_object_type_vehicle)
		{
			struct unit_datum *vehicle = vehicle_get(biped->object.parent_object_index);

			biped_vehicle_speech(biped_index);
			if (TEST_FLAG(biped->unit.control_flags, _unit_control_action_bit))
				unit_try_and_exit_seat(biped_index);

			if (rider_ejection &&
				vehicle->object.up.k<0.f &&
				TEST_FLAG(vehicle->object.flags, _object_on_ground_bit))
			{
				unit_exit_seat_end(biped_index);
			}
		}
		else if (parent->object.type==_object_type_biped)
		{
			animation.state_desired =
				(char)((parent->object.damage_flags & FLAG(_object_dead_bit)) | 0x20);
		}
	}
	else
	{
		biped_snap_facing(biped_index);
		if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit) ||
			!(definition->biped.flags &
				(FLAG(_biped_flying_bit) | FLAG(_biped_climbs_anything_bit))))
		{
			biped->unit.desired_facing_vector.k = 0.f;
			if (normalize3d(&biped->unit.desired_facing_vector)==0.f)
				biped->unit.desired_facing_vector = *global_forward3d;
		}

		switch (biped->unit.animation.state)
		{
		case _unit_state_move_front:
		case _unit_state_move_back:
		case _unit_state_move_left:
		case _unit_state_move_right:
			biped->biped.state = biped_state_moving;
			break;

		case _unit_state_idle:
		case _unit_state_turn_left:
		case _unit_state_turn_right:
			biped->biped.state = biped_state_idle;
			break;

		default:
			biped->biped.state = biped_state_unknown;
			break;
		}

		if (magnitude_squared3d(&biped->unit.throttle)<0.1f*0.1f)
			biped->unit.throttle = *global_zero_vector3d;

		if (TEST_FLAG(biped->biped.flags, _biped_airborne_bit))
		{
			if (biped->biped.airborne_ticks<SCHAR_MAX)
				++biped->biped.airborne_ticks;
		}
		else
		{
			biped->biped.airborne_ticks = 0;
		}

		if (TEST_FLAG(biped->biped.flags, _biped_slipping_bit))
		{
			if (biped->biped.slipping_ticks<SCHAR_MAX)
				++biped->biped.slipping_ticks;
		}
		else
		{
			biped->biped.slipping_ticks = 0;
		}

		animation.state_desired = _unit_state_idle;
		animation.crouching = TEST_FLAG(
			biped->unit.control_flags,
			_unit_control_crouch_modifier_bit);

		biped_verify_object_vectors(biped_index, "pre-turning");
		if (!TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
		{
			biped_update_turning(biped_index, &animation);
			biped_verify_object_vectors(biped_index, "post-turning");
		}

		/* port: a player's biped at game.sim_rate 60 or 120 moves in the fast
		phase, and the rest of its update follows its last sub-step
		(bipeds_substep); an AI's moves its sub-steps here
		(biped_substep_inline) */
		if (biped_update_moving(biped_index, &animation))
		{
			profile_exit(biped_update_section);
			return TRUE;
		}
		biped_verify_object_vectors(biped_index, "post-moving");
		biped_update_on_foot(biped_index, &animation);
	}

	biped_update_end(biped_index, &animation);
	/* port: (an AI's sub-steps kept for the fast phase) */
	biped_replay_seal(biped_index);
	profile_exit(biped_update_section);

	return TRUE;
}
