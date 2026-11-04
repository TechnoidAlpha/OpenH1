/*
PLAY_LOG.C

The play log (debug.play_log): every player's state, about 60 times a
second, in a file of its own, for studying how people play (their aim, their
movement, their fights) beside the bots. Each machine logs what it knows:
the host, every player as the game has them; a client, the others as they
reach it.

Lines:

    start <ticks a second> <sample every this many ticks> <map>
                                     at the first tick, and each new map
    p <tick> <player> <kind> <team> <x> <y> <z> <vx> <vy> <vz>
      <aim x> <aim y> <aim z> <throttle i> <throttle j> <control flags>
      <trigger> <body> <shield> <vehicle tag> <weapon tag> <name>
                                     each living player, kind L (on this
                                     machine), B (a bot) or R (another's);
                                     in a vehicle, its place and velocity
                                     are the vehicle's; a tag is - for
                                     none, its spaces written as _
    spawn <tick> <player>            a player's unit appeared
    death <tick> <player>            a player's unit is gone
*/

#include "cseries.h"
#include "game/game.h"
#include "game/players.h"
#include "scenario/scenario.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "tag_files/tag_groups.h"

#include "bots.h"

#include <stdio.h>

const char *config_string(char const *name);
void platform_log(const char *format, ...);

static struct
{
	boolean opened;
	FILE *file;
	long last_flush_tick;
	long last_tick;
	long alive[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} play_log;

static void play_log_open(void)
{
	char const *path;

	play_log.opened = TRUE;
	path = config_string("debug.play_log");
	if (!path || !path[0])
		return;
	play_log.file = fopen(path, "w");
	if (!play_log.file)
	{
		platform_log("play log: could not open %s", path);
		return;
	}
	platform_log("play log: writing %s", path);
}

static long play_log_sample_ticks(void)
{
	long ticks = TICKS_PER_SECOND / 60;

	return ticks < 1 ? 1 : ticks;
}

/* a tag's name, its spaces as _ (one word of the line) */
static char const *play_log_tag_name(long definition_index, char *name, long size)
{
	char const *tag_name = tag_get_name(definition_index);
	long index;

	for (index = 0; index < size - 1 && tag_name[index]; index++)
		name[index] = tag_name[index] == ' ' ? '_' : tag_name[index];
	name[index] = 0;
	return name;
}

static void play_log_player(long tick, long index, struct player_datum *player,
	struct player_action const *action)
{
	struct unit_datum *unit = unit_get(player->unit_index);
	struct object_datum *place = (struct object_datum *)unit;
	char name[NUMBEROF(player->name)];
	char weapon_buffer[128];
	char vehicle_buffer[128];
	char const *weapon_name = "-";
	char const *vehicle_name = "-";
	char kind;
	long weapon_index = NONE;
	long character;

	for (character = 0; character < (long)NUMBEROF(name) - 1 && player->name[character]; character++)
		name[character] = player->name[character] > ' ' && player->name[character] < 127 ?
			(char)player->name[character] : '_';
	name[character] = 0;
	if (bots_player_is_bot(index))
		kind = 'B';
	else if (player->local_player_index != NONE)
		kind = 'L';
	else
		kind = 'R';
	if (unit->unit.current_weapon_index >= 0 && unit->unit.current_weapon_index < MAXIMUM_WEAPONS_PER_UNIT)
		weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
	if (weapon_index != NONE)
		weapon_name = play_log_tag_name(weapon_get(weapon_index)->definition_index, weapon_buffer,
			sizeof(weapon_buffer));
	if (unit->object.parent_object_index != NONE)
	{
		place = object_get(unit->object.parent_object_index);
		vehicle_name = play_log_tag_name(place->definition_index, vehicle_buffer, sizeof(vehicle_buffer));
	}
	fprintf(play_log.file, "p %ld %ld %c %ld %.3f %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f %.2f %.2f %lx %.2f "
		"%.3f %.3f %s %s %s\n",
		tick, index, kind, (long)player->team_index,
		place->object.position.x, place->object.position.y, place->object.position.z,
		place->object.translational_velocity.i, place->object.translational_velocity.j,
		place->object.translational_velocity.k,
		unit->unit.aiming_vector.i, unit->unit.aiming_vector.j, unit->unit.aiming_vector.k,
		unit->unit.throttle.i, unit->unit.throttle.j, unit->unit.control_flags,
		action ? action->primary_trigger : 0.0f,
		unit->object.body_vitality, unit->object.shield_vitality,
		vehicle_name, weapon_name, name[0] ? name : "-");
}

/* (players.c, after the bots' actions: the tick's actions of each player) */
void play_log_tick(struct player_action const *actions)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long tick;
	boolean sample;

	if (!play_log.opened)
	{
		play_log_open();
		play_log.last_tick = -1;
	}
	if (!play_log.file)
		return;
	tick = game_time_get();
	/* (a new game: its time starts again) */
	if (tick <= play_log.last_tick || play_log.last_tick < 0)
	{
		/* (the scenario's tag: main_get_map_name's is the menus' last map,
		not a network test's) */
		char const *map_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

		fprintf(play_log.file, "start %ld %ld %s\n", (long)TICKS_PER_SECOND, play_log_sample_ticks(),
			map_name && map_name[0] ? map_name : "-");
		csmemset(play_log.alive, 0, sizeof(play_log.alive));
	}
	play_log.last_tick = tick;
	sample = tick % play_log_sample_ticks() == 0;
	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		long index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		long alive = player->unit_index != NONE;

		if (index < 0 || index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			continue;
		if (alive != play_log.alive[index])
		{
			fprintf(play_log.file, "%s %ld %ld\n", alive ? "spawn" : "death", tick, index);
			play_log.alive[index] = alive;
		}
		if (alive && sample)
			play_log_player(tick, index, player, actions ? &actions[index] : NULL);
	}
	if (tick - play_log.last_flush_tick >= TICKS_PER_SECOND || tick < play_log.last_flush_tick)
	{
		fflush(play_log.file);
		play_log.last_flush_tick = tick;
	}
}
