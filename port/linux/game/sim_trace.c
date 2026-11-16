/*
SIM_TRACE.C

The scorecard's per-step log (debug.trace, tools/scorecard): a line for each
simulation step with its time, then the players' and projectiles' state as
the step left it, and what happened in it. The scorecard plays a scenario at
each simulation rate and compares what this logs in real units (seconds,
world units, world units a second), so the times are the simulation's in
seconds from the scenario's start, never ticks.

Off (debug.trace empty), the game's calls cost a test of sim_trace_on
(SIM_TRACE, halo_linux_source_fixups.h), except sim_trace_step's, once a
step, which keeps the simulation's clock that debug.test_input's scripts
("script:", port/linux/src/xinput_sdl.c) are timed on.

Lines:

    # sim trace 1 rate <R> mutation <name|->       once, first
    s <t> <tick> <micro>                           each step: its end, in
                                                   seconds from the scenario's
                                                   start; the 30 Hz tick it is
                                                   in; its index in that tick
    p <player> <x> <y> <z> <vx> <vy> <vz> <ax> <ay> <az> <air> <body> <shield>
      <crouch> <eye>                               each player's unit: place,
                                                   velocity (world units a
                                                   second), aim (a unit
                                                   vector), 1 airborne, its
                                                   body's and shield's
                                                   vitality (fractions of
                                                   their maximums: 0 without),
                                                   how far it is crouched (0
                                                   to 1: 0 but a biped's) and
                                                   its eye's height (the
                                                   first-person camera's,
                                                   unit_get_camera_position)
    v <player> <id> <x> <y> <z> <vx> <vy> <vz> <fx> <fy> <fz> <ux> <uy> <uz>
      <wx> <wy> <wz> <ground>                      each vehicle, and the player
                                                   whose unit rides it (-1 for
                                                   none): place, velocity
                                                   (world units a second),
                                                   forward and up (unit
                                                   vectors), angular velocity
                                                   (radians a second), 1 on
                                                   the ground
    o <id> <x> <y> <z> <vx> <vy> <vz>              each projectile in flight
    u <id> <x> <y> <z> <vx> <vy> <vz> <air>        each biped the AI drives (an
                                                   actor's or a swarm's): place,
                                                   velocity, 1 airborne
    e fire <player> <weapon> <trigger>             a weapon fired
    e new <id> <tag> <x> <y> <z> <vx> <vy> <vz> <owner player>
                                                   a projectile made, where
                                                   and as fast as it starts
    e hit <id> <t> <x> <y> <z> <nx> <ny> <nz>      it struck a surface, at t
                                                   (in its update, by the
                                                   fraction the projectile
                                                   code found), at a point,
                                                   facing a normal
    e boom <id> <x> <y> <z>                        it detonated
    e gone <id>                                    it is no more
    e weapon <player> <tag|->                      the weapon in the player's
                                                   hands changed (a pickup, a
                                                   switch, a respawn; - none)
    e wstate <player> <state>                      its state changed: idle,
                                                   recoil, chamber (a pump, a
                                                   bolt), reload, charged,
                                                   ready (drawn), put_away
                                                   (primary or secondary)
    e zoom <player> <level>                        its zoom level changed (-1
                                                   none, 0 the first)
    e act <player> <state>                         the unit's animation state
                                                   changed to a melee or a
                                                   grenade's throw (melee,
                                                   melee_airborne, throw);
                                                   "-" when it ends
    f <seconds> <t> <x> <y> <z> <gf> <gl> <gu>     each frame drawn with the
                                                   first-person weapon (a
                                                   game that draws: not the
                                                   scorecard's): the frames'
                                                   time since the first, the
                                                   newest step's t, the
                                                   camera's place, and the
                                                   weapon's nodes' mean place
                                                   along its forward, left
                                                   and up
    c <seconds> <t> <x> <y> <z> <fx> <fy> <fz> <rx> <ry> <rz> <seat>
                                                   each frame drawn, as it
                                                   takes the first local
                                                   player's view: the frames'
                                                   time since the first, the
                                                   newest step's t, the view's
                                                   place and forward, the
                                                   drawn place of what the
                                                   player's unit rides (its
                                                   root node: the vehicle's,
                                                   or on foot the unit's),
                                                   1 in a vehicle's seat

An event's line comes after the step's s line it happened in. Identifiers
are object datum indexes in hexadecimal; a player is its absolute index, or
-1 for none.

The simulation's clock is sim_trace_step's, called after every step with
the 30 Hz tick it is in, the sub-step's index in that tick and the number of
sub-steps a tick (game.sim_rate / 30, sim_rate.h): by game_time.c after each
tick, for its last sub-step (at 30 Hz, its only one: 0 and 1), and from the
fast phase's last slot (_sim_substep_trace) for the others. The fast phase
runs after the objects' update: until what a unit moves runs in it, a
sub-step before the last sees the tick's end. A velocity
is logged as the object keeps it, taken to be world units a 30 Hz tick
(SECONDS_PER_VELOCITY_UNIT): if the micro lane keeps another unit, change
it here, in one place.

debug.trace_mutation puts a known bug in on purpose, which the scorecard
must catch (its negative controls): "head_start", a projectile moves a whole
tick further as it is made (as Halo 2's did, the other way, at 60 Hz);
"jump", a jump leaves the ground 1.3% faster (2.6% higher, as an
uncorrected 60 Hz jump does); "speed", a projectile starts 1% slow;
"unseeded", a draw of the game's random seed after a step whenever the
host's clock reads an odd microsecond, as randomness taken from outside the
game would (two runs of the same input then differ: identity.py --repeat
must catch it).
*/

#include "cseries.h"
#include "camera/observer.h"
#include "game/game.h"
#include "game/players.h"
#include "main/main.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/bipeds.h"
#include "items/projectiles.h"
#include "items/weapons.h"
#include "tag_files/tag_files.h"
#include "sim_rate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(char const *name);
double config_real(char const *name);
void platform_log(char const *format, ...);
unsigned long long platform_shared_clock_microseconds(void);
/* port/linux/game/network_test.c: the tick a scenario's seconds count from
(its cues'), NONE until it is known; 0 with no network test */
long network_test_cues_start_tick(void);

/* ---------- constants */

enum
{
	MAXIMUM_TRACED_PROJECTILES = 256,
	TRACE_BUFFER_BYTES = 1 << 20,
};

/* a velocity as objects keep it (world units a 30 Hz tick) in world units a
second */
#define SECONDS_PER_VELOCITY_UNIT ((double)TICKS_PER_SECOND)

enum
{
	_mutation_none,
	_mutation_head_start,
	_mutation_jump,
	_mutation_speed,
	_mutation_unseeded,
};

/* ---------- globals */

int sim_trace_on;

static struct
{
	boolean checked;
	FILE *file;
	char *buffer;
	double stop_seconds;
	short mutation;
	/* the clock: the last step's tick, index and steps a tick */
	long tick;
	long micro_step;
	long steps_per_tick;
	boolean stepped;
	/* the projectiles in flight at the last step, and whether each is still
	there at this one */
	long projectiles[MAXIMUM_TRACED_PROJECTILES];
	short projectile_count;
	/* (the jump mutation: each player's unit airborne at the last step) */
	boolean airborne[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* each player's weapon (its definition, NONE for none), its state,
	the unit's zoom and its action at the last step, for the events of
	their changes; set once the player's first line is written */
	boolean weapon_known[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	long weapon_definition[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	char weapon_state[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	char zoom[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	char action[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} sim_trace;

/* the events of a step, written before the step's own line, are held back
until it: a step's events print after its s line */
static char sim_trace_events[64 * 1024];
static int sim_trace_events_length;

/* ---------- private code */

/* the fast phase's last slot (sim_rate.h): each sub-step but the tick's
last, which game_time.c logs after the tick (in the tick, game_time_get is
the tick being run) */
static void sim_trace_substep(
	short substep_index,
	short substep_count,
	real seconds)
{
	(void)seconds;
	if (substep_index < substep_count - 1)
		sim_trace_step(game_time_get(), substep_index, substep_count);
}

static void sim_trace_check(
	void)
{
	char const *path;
	char const *mutation;

	if (sim_trace.checked)
		return;
	sim_trace.checked = TRUE;
	/* (the clock, which debug.test_input's scripts read, kept whether the
	trace is on or not) */
	sim_substep_register(_sim_substep_trace, sim_trace_substep);
	path = config_string("debug.trace");
	if (!path || !*path)
		return;
	sim_trace.file = fopen(path, "w");
	if (!sim_trace.file)
	{
		platform_log("sim trace: cannot write %s", path);
		return;
	}
	sim_trace.buffer = malloc(TRACE_BUFFER_BYTES);
	if (sim_trace.buffer)
		setvbuf(sim_trace.file, sim_trace.buffer, _IOFBF, TRACE_BUFFER_BYTES);
	sim_trace.stop_seconds = config_real("debug.trace_seconds");
	mutation = config_string("debug.trace_mutation");
	if (!mutation || !*mutation)
		sim_trace.mutation = _mutation_none;
	else if (!strcmp(mutation, "head_start"))
		sim_trace.mutation = _mutation_head_start;
	else if (!strcmp(mutation, "jump"))
		sim_trace.mutation = _mutation_jump;
	else if (!strcmp(mutation, "speed"))
		sim_trace.mutation = _mutation_speed;
	else if (!strcmp(mutation, "unseeded"))
		sim_trace.mutation = _mutation_unseeded;
	else
	{
		platform_log("sim trace: no mutation \"%s\"", mutation);
		sim_trace.mutation = _mutation_none;
	}
	platform_log("sim trace: %s%s%s", path, sim_trace.mutation ? ", mutation " : "",
		sim_trace.mutation ? mutation : "");
}

/* the tick a scenario's seconds count from, NONE until it is known */
static long sim_trace_start_tick(
	void)
{
	return network_test_cues_start_tick();
}

/* seconds from the scenario's start to the end of the last step (the start
of the next), or a negative number before it starts */
static double sim_trace_seconds(
	void)
{
	long start = sim_trace_start_tick();

	if (!sim_trace.stepped || start == NONE)
		return -1.0;
	return ((double)(sim_trace.tick - start) + (double)(sim_trace.micro_step + 1) / (double)sim_trace.steps_per_tick) /
		(double)TICKS_PER_SECOND;
}

static boolean sim_trace_logging(
	void)
{
	return sim_trace.file && sim_trace_seconds() > 0.0;
}

static long sim_trace_player_of_unit(
	long unit_index)
{
	struct unit_datum *unit;

	if (unit_index == NONE)
		return NONE;
	unit = object_try_and_get_and_verify_type(unit_index, _object_mask_unit);
	if (!unit || unit->unit.player_index == NONE)
		return NONE;
	return DATUM_INDEX_TO_ABSOLUTE_INDEX(unit->unit.player_index);
}

/* a vehicle, and the player whose unit rides it, NONE for none */
static void sim_trace_vehicle(
	long absolute_index,
	long vehicle_index,
	struct object_datum *vehicle)
{
	fprintf(sim_trace.file, "v %ld %lx %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d\n",
		absolute_index, vehicle_index,
		vehicle->object.position.x, vehicle->object.position.y, vehicle->object.position.z,
		vehicle->object.translational_velocity.i * SECONDS_PER_VELOCITY_UNIT,
		vehicle->object.translational_velocity.j * SECONDS_PER_VELOCITY_UNIT,
		vehicle->object.translational_velocity.k * SECONDS_PER_VELOCITY_UNIT,
		vehicle->object.forward.i, vehicle->object.forward.j, vehicle->object.forward.k,
		vehicle->object.up.i, vehicle->object.up.j, vehicle->object.up.k,
		vehicle->object.angular_velocity.i * SECONDS_PER_VELOCITY_UNIT,
		vehicle->object.angular_velocity.j * SECONDS_PER_VELOCITY_UNIT,
		vehicle->object.angular_velocity.k * SECONDS_PER_VELOCITY_UNIT,
		TEST_FLAG(vehicle->object.flags, _object_on_ground_bit) ? 1 : 0);
}

/* every vehicle, and the player whose unit rides it (its ultimate parent,
the first such player), if any */
static void sim_trace_vehicles(
	void)
{
	struct object_iterator iterator;
	struct object_datum *vehicle;

	object_iterator_new(&iterator, _object_mask_vehicle, 0);
	while ((vehicle = object_iterator_next(&iterator)) != NULL)
	{
		long vehicle_index = iterator.index;
		long rider = NONE;
		struct data_iterator players;
		struct player_datum *player;

		data_iterator_new(&players, player_data);
		while (rider == NONE && (player = (struct player_datum *)data_iterator_next(&players)) != NULL)
		{
			if (player->unit_index != NONE && object_get_ultimate_parent(player->unit_index) == vehicle_index)
				rider = DATUM_INDEX_TO_ABSOLUTE_INDEX(players.datum_index);
		}
		sim_trace_vehicle(rider, vehicle_index, vehicle);
	}
}

/* the player's weapon, its state, its zoom and its melee or throw, each as
an event line when it changed since the last step (footage's cuts on the
same action: e weapon, e wstate, e zoom, e act) */
static void sim_trace_weapon_events(
	long absolute_index,
	struct unit_datum *unit)
{
	static char const *const weapon_states[] =
	{
		"idle", "primary_recoil", "secondary_recoil", "primary_chamber", "secondary_chamber",
		"primary_reload", "secondary_reload", "primary_charged", "secondary_charged", "ready", "put_away",
	};
	long weapon_index = unit->unit.current_weapon_index >= 0 &&
		unit->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT ?
		unit->unit.weapon_object_indices[unit->unit.current_weapon_index] : NONE;
	struct weapon_datum *weapon = weapon_index != NONE ?
		(struct weapon_datum *)object_try_and_get_and_verify_type(weapon_index, _object_mask_weapon) : NULL;
	long definition = weapon ? weapon->definition_index : NONE;
	char state = weapon ? weapon->weapon.state : -1;
	char zoom = unit->unit.current_zoom_level;
	char action = unit->unit.animation.state;
	char const *act;

	/* (only a melee's or a throw's start and end) */
	if (action != _unit_state_melee_attack && action != _unit_state_melee_airborne &&
		action != _unit_state_throw_grenade)
	{
		action = NONE;
	}
	if (sim_trace.weapon_known[absolute_index] && definition == sim_trace.weapon_definition[absolute_index] &&
		state == sim_trace.weapon_state[absolute_index] && zoom == sim_trace.zoom[absolute_index] &&
		action == sim_trace.action[absolute_index])
	{
		return;
	}
	if (!sim_trace.weapon_known[absolute_index] || definition != sim_trace.weapon_definition[absolute_index])
		fprintf(sim_trace.file, "e weapon %ld %s\n", absolute_index, definition != NONE ? tag_get_name(definition) : "-");
	if ((!sim_trace.weapon_known[absolute_index] || state != sim_trace.weapon_state[absolute_index]) && state >= 0 &&
		state < (char)NUMBEROF(weapon_states))
	{
		fprintf(sim_trace.file, "e wstate %ld %s\n", absolute_index, weapon_states[(int)state]);
	}
	if (!sim_trace.weapon_known[absolute_index] || zoom != sim_trace.zoom[absolute_index])
		fprintf(sim_trace.file, "e zoom %ld %d\n", absolute_index, zoom);
	if (sim_trace.weapon_known[absolute_index] && action != sim_trace.action[absolute_index])
	{
		act = action == _unit_state_melee_attack ? "melee" : action == _unit_state_melee_airborne ? "melee_airborne" :
			action == _unit_state_throw_grenade ? "throw" : "-";
		fprintf(sim_trace.file, "e act %ld %s\n", absolute_index, act);
	}
	sim_trace.weapon_known[absolute_index] = TRUE;
	sim_trace.weapon_definition[absolute_index] = definition;
	sim_trace.weapon_state[absolute_index] = state;
	sim_trace.zoom[absolute_index] = zoom;
	sim_trace.action[absolute_index] = action;
}

static void sim_trace_players(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct unit_datum *unit;
		boolean airborne = FALSE;
		real crouch = 0.f;
		real_point3d eye;

		if (player->unit_index == NONE)
			continue;
		unit = unit_get(player->unit_index);
		unit_get_camera_position(player->unit_index, &eye);
		if (TEST_FLAG(_object_mask_biped, unit->object.type))
		{
			struct biped_datum *biped = biped_get(player->unit_index);

			airborne = TEST_FLAG(biped->biped.flags, _biped_airborne_bit);
			crouch = biped->biped.crouch;
			/* (a jump: off the ground since the last step, rising) */
			if (sim_trace.mutation == _mutation_jump && absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS &&
				airborne && !sim_trace.airborne[absolute_index] && unit->object.translational_velocity.k > 0.f)
			{
				unit->object.translational_velocity.k *= 1.013f;
			}
		}
		if (absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			sim_trace.airborne[absolute_index] = airborne;
		fprintf(sim_trace.file, "p %ld %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d %.6f %.6f %.6f %.6f\n", absolute_index,
			unit->object.position.x, unit->object.position.y, unit->object.position.z,
			unit->object.translational_velocity.i * SECONDS_PER_VELOCITY_UNIT,
			unit->object.translational_velocity.j * SECONDS_PER_VELOCITY_UNIT,
			unit->object.translational_velocity.k * SECONDS_PER_VELOCITY_UNIT,
			unit->unit.aiming_vector.i, unit->unit.aiming_vector.j, unit->unit.aiming_vector.k, airborne ? 1 : 0,
			unit->object.body_vitality, unit->object.shield_vitality, crouch, eye.z);
		if (absolute_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			sim_trace_weapon_events(absolute_index, unit);
	}
}

static void sim_trace_projectiles(
	void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	long seen[MAXIMUM_TRACED_PROJECTILES];
	short seen_count = 0;
	short index;

	object_iterator_new(&iterator, _object_mask_projectile, 0);
	while ((object = object_iterator_next(&iterator)) != NULL)
	{
		boolean known = FALSE;

		for (index = 0; index < sim_trace.projectile_count; index++)
		{
			if (sim_trace.projectiles[index] == iterator.index)
				known = TRUE;
		}
		if (!known && sim_trace.mutation == _mutation_head_start)
		{
			real_point3d ahead;

			ahead.x = object->object.position.x + object->object.translational_velocity.i;
			ahead.y = object->object.position.y + object->object.translational_velocity.j;
			ahead.z = object->object.position.z + object->object.translational_velocity.k;
			object_translate(iterator.index, &ahead, NULL);
		}
		if (seen_count < MAXIMUM_TRACED_PROJECTILES)
			seen[seen_count++] = iterator.index;
		fprintf(sim_trace.file, "o %lx %.6f %.6f %.6f %.6f %.6f %.6f\n", iterator.index,
			object->object.position.x, object->object.position.y, object->object.position.z,
			object->object.translational_velocity.i * SECONDS_PER_VELOCITY_UNIT,
			object->object.translational_velocity.j * SECONDS_PER_VELOCITY_UNIT,
			object->object.translational_velocity.k * SECONDS_PER_VELOCITY_UNIT);
	}
	for (index = 0; index < sim_trace.projectile_count; index++)
	{
		short other;
		boolean there = FALSE;

		for (other = 0; other < seen_count; other++)
		{
			if (seen[other] == sim_trace.projectiles[index])
				there = TRUE;
		}
		if (!there)
			fprintf(sim_trace.file, "e gone %lx\n", sim_trace.projectiles[index]);
	}
	memcpy(sim_trace.projectiles, seen, seen_count * sizeof(seen[0]));
	sim_trace.projectile_count = seen_count;
}

/* the bipeds the AI drives (bipeds.c moves them in their sub-steps too) */
static void sim_trace_ai_units(
	void)
{
	struct object_iterator iterator;
	struct biped_datum *biped;

	object_iterator_new(&iterator, _object_mask_biped, 0);
	while ((biped = (struct biped_datum *)object_iterator_next(&iterator)) != NULL)
	{
		if (biped->unit.actor_index == NONE && biped->unit.swarm_actor_index == NONE)
			continue;
		fprintf(sim_trace.file, "u %lx %.6f %.6f %.6f %.6f %.6f %.6f %d\n", iterator.index,
			biped->object.position.x, biped->object.position.y, biped->object.position.z,
			biped->object.translational_velocity.i * SECONDS_PER_VELOCITY_UNIT,
			biped->object.translational_velocity.j * SECONDS_PER_VELOCITY_UNIT,
			biped->object.translational_velocity.k * SECONDS_PER_VELOCITY_UNIT,
			TEST_FLAG(biped->biped.flags, _biped_airborne_bit) ? 1 : 0);
	}
}

/* ---------- public code */

void sim_trace_step(
	long tick,
	long micro_step,
	long steps_per_tick)
{
	double seconds;

	if (!sim_trace.checked)
	{
		sim_trace_check();
		if (sim_trace.file)
		{
			fprintf(sim_trace.file, "# sim trace 1 rate %ld mutation %s\n", steps_per_tick * TICKS_PER_SECOND,
				sim_trace.mutation ? config_string("debug.trace_mutation") : "-");
		}
	}
	sim_trace.tick = tick;
	sim_trace.micro_step = micro_step;
	sim_trace.steps_per_tick = steps_per_tick > 0 ? steps_per_tick : 1;
	sim_trace.stepped = TRUE;
	/* (the events of the step to come follow its line: on from the first
	step of the scenario) */
	sim_trace_on = sim_trace.file != NULL && sim_trace_start_tick() != NONE;
	if (!sim_trace_logging())
	{
		sim_trace_events_length = 0;
		return;
	}
	if (sim_trace.mutation == _mutation_unseeded && (platform_shared_clock_microseconds() & 1))
		seed_random(get_global_random_seed_address());
	seconds = sim_trace_seconds();
	fprintf(sim_trace.file, "s %.6f %ld %ld\n", seconds, tick, micro_step);
	fwrite(sim_trace_events, 1, sim_trace_events_length, sim_trace.file);
	sim_trace_events_length = 0;
	sim_trace_players();
	sim_trace_vehicles();
	sim_trace_projectiles();
	sim_trace_ai_units();
	if (sim_trace.stop_seconds > 0.0 && seconds >= sim_trace.stop_seconds - 1e-9)
	{
		fclose(sim_trace.file);
		sim_trace.file = NULL;
		platform_log("sim trace: %.3f seconds traced", seconds);
		exit(EXIT_SUCCESS);
	}
}

double sim_trace_scenario_seconds(
	void)
{
	return sim_trace_seconds();
}

static void sim_trace_event(
	char const *format,
	...)
{
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(sim_trace_events + sim_trace_events_length,
		sizeof(sim_trace_events) - sim_trace_events_length, format, arguments);
	va_end(arguments);
	if (length > 0 && sim_trace_events_length + length < (int)sizeof(sim_trace_events))
		sim_trace_events_length += length;
}

void sim_trace_projectile_new(
	long projectile_index)
{
	struct projectile_datum *projectile = projectile_get(projectile_index);

	if (sim_trace.mutation == _mutation_speed)
		scale_vector3d(&projectile->object.translational_velocity, 0.99f, &projectile->object.translational_velocity);
	sim_trace_event("e new %lx %s %.6f %.6f %.6f %.6f %.6f %.6f %ld\n", projectile_index,
		tag_get_name(projectile->definition_index),
		projectile->object.position.x, projectile->object.position.y, projectile->object.position.z,
		projectile->object.translational_velocity.i * SECONDS_PER_VELOCITY_UNIT,
		projectile->object.translational_velocity.j * SECONDS_PER_VELOCITY_UNIT,
		projectile->object.translational_velocity.k * SECONDS_PER_VELOCITY_UNIT,
		sim_trace_player_of_unit(object_get_ultimate_parent(projectile->object.owner_object_index)));
}

void sim_trace_projectile_collision(
	long projectile_index,
	void const *point,
	void const *normal,
	float time_remaining,
	float fraction)
{
	real_point3d const *at = point;
	real_vector3d const *facing = normal;
	/* (the line tested spans the update's time left, from that far into it;
	an update in the fast phase spans its sub-step, one in the objects'
	update the whole tick) */
	double into_update = (1.0 - time_remaining) + fraction * time_remaining;
	short substep_index = sim_substep_index();
	double ticks = substep_index == NONE ? into_update :
		(substep_index + into_update) / sim_substep_count();

	sim_trace_event("e hit %lx %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", projectile_index,
		((double)(game_time_get() - sim_trace_start_tick()) + ticks) / (double)TICKS_PER_SECOND,
		at->x, at->y, at->z, facing->i, facing->j, facing->k);
}

void sim_trace_projectile_detonate(
	long projectile_index)
{
	struct object_datum *object = object_get(projectile_index);

	sim_trace_event("e boom %lx %.6f %.6f %.6f\n", projectile_index,
		object->object.position.x, object->object.position.y, object->object.position.z);
}

void sim_trace_weapon_fire(
	long weapon_index,
	long owner_object_index,
	short trigger_index)
{
	sim_trace_event("e fire %ld %lx %d\n", sim_trace_player_of_unit(object_get_ultimate_parent(owner_object_index)),
		weapon_index, (int)trigger_index);
}

/* (first_person_weapons.c, each frame drawn) the first local player's view
as drawn: the camera's place, and where the first-person weapon's nodes are
on average as seen from it (along its forward, left and up), after the
frames' time since the last one */
void sim_trace_frame(
	short local_player_index,
	void const *camera_position,
	void const *camera_forward,
	void const *camera_up,
	void const *node_matrices,
	short node_count)
{
	static double frames_seconds = -1.0;
	real_point3d const *position = camera_position;
	real_vector3d const *forward = camera_forward;
	real_vector3d const *up = camera_up;
	real_matrix4x3 const *nodes = node_matrices;
	real_vector3d left;
	double along[3] = { 0.0, 0.0, 0.0 };
	short node_index;

	if (local_player_index != 0 || !sim_trace_logging())
		return;
	frames_seconds = frames_seconds < 0.0 ? 0.0 : frames_seconds + main_get_seconds_elapsed();
	cross_product3d(up, forward, &left);
	for (node_index = 0; node_index < node_count; node_index++)
	{
		real_vector3d offset;

		vector_from_points3d(position, &nodes[node_index].position, &offset);
		along[0] += dot_product3d(&offset, forward);
		along[1] += dot_product3d(&offset, &left);
		along[2] += dot_product3d(&offset, up);
	}
	if (node_count > 0)
	{
		along[0] /= node_count;
		along[1] /= node_count;
		along[2] /= node_count;
	}
	fprintf(sim_trace.file, "f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", frames_seconds,
		sim_trace_seconds(), position->x, position->y, position->z, along[0], along[1], along[2]);
}

/* (main.c, each frame, as it takes the first local player's view) the view
as drawn and where what the player rides is drawn (object_get_node_matrices
gives the drawn pose while a frame is drawn), after the frames' time since
the last one: how evenly the view moves, and the vehicle in it, frame to
frame */
void sim_trace_view(
	short local_player_index,
	void const *observer_result)
{
	static double views_seconds = -1.0;
	struct observer_result const *observer = observer_result;
	long player_index = local_player_get_player_index(0);
	long unit_index = player_index == NONE ? NONE : player_get(player_index)->unit_index;
	real_point3d ridden = { 0.0f, 0.0f, 0.0f };
	int seated = 0;

	if (local_player_index != 0 || !observer || !sim_trace_logging())
		return;
	views_seconds = views_seconds < 0.0 ? 0.0 : views_seconds + main_get_seconds_elapsed();
	if (unit_index != NONE)
	{
		long root_index = object_get_ultimate_parent(unit_index);

		ridden = object_get_node_matrices(root_index)[0].position;
		seated = root_index != unit_index;
	}
	fprintf(sim_trace.file, "c %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f %d\n", views_seconds,
		sim_trace_seconds(), observer->position.x, observer->position.y, observer->position.z, observer->forward.i,
		observer->forward.j, observer->forward.k, ridden.x, ridden.y, ridden.z, seated);
}

/* debug.test_input "script:"'s aim (port/linux/src/xinput_sdl.c): the first
local player's view turned to a yaw and pitch, in degrees, as a map's
scripts turn it */
void sim_trace_aim(
	double yaw,
	double pitch)
{
	real_euler_angles2d angles;
	real_vector3d facing;

	if (local_player_get_player_index(0) == NONE)
		return;
	angles.yaw = DEGREES_TO_RADIANS(yaw);
	angles.pitch = DEGREES_TO_RADIANS(pitch);
	vector3d_from_euler_angles2d(&facing, &angles);
	player_control_set_facing(0, &facing);
}
