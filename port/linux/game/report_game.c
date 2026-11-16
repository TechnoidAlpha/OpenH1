/*
REPORT_GAME.C

What a Save Report's report.md says of the game (port/linux/src/report.c):
the map loaded (its scenario's tag, as play_log.c names it) and, in a
multiplayer game, its gametype's name (its variant's, where it has one);
the mods played and the map's file, by their names; where the game is
(report_game_where: the time, the BSP, the first local player's place and
aim, its camera, the network game, as report_where_text writes them);
whether a game is being played (the note's line closes when it is not), or
the main menu is up (a report's issue waits for it); and its note's line,
the text chat's (network_chat.c). Nothing that says who a player is: no
names, no addresses.
*/

#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "game/players.h"
#include "camera/observer.h"
#include "objects/objects.h"
#include "units/units.h"
#include "main/console.h"
#include "custom_edition_cache.h"
#include "mods.h"
#include "interface/ui_widget.h"
#include "network_chat.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "../src/report.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's (port_config.h) */
int config_boolean(const char *name);

/* how many mods are listed at most (the others counted) */
#define REPORT_GAME_MODS_LISTED 12

#define REPORT_DEGREES (180.0f / 3.14159265358979f)

/* the map loaded and running: 1 for a game's, 2 for the ui map's (the
menus), 0 for none (a loading screen, the time between levels) */
static int report_game_map(void)
{
	char const *map_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;

	if (!map_name || !map_name[0] || !game_in_progress())
		return 0;
	return strcmp(map_name, "ui") && !strstr(map_name, "\\ui") ? 1 : 2;
}

int report_game_playing(void)
{
	return report_game_map() == 1;
}

/* (the ui map, its menus up: not its loading, nor the movie before them) */
int report_game_menus(void)
{
	return report_game_map() == 2 && ui_widgets_active();
}

void report_game_describe(char *text, size_t size)
{
	char const *map_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	struct game_variant *variant;
	char gametype[16];
	short index;

	if (!size)
		return;
	if (!map_name || !map_name[0] || !game_in_progress())
	{
		snprintf(text, size, "the menus");
		return;
	}
	if (!strcmp(map_name, "ui") || strstr(map_name, "\\ui"))
	{
		snprintf(text, size, "the main menu");
		return;
	}
	variant = game_engine_running() ? game_engine_get_variant() : NULL;
	gametype[0] = 0;
	for (index = 0; variant && index < NUMBEROF(gametype) - 1 && index < NUMBEROF(variant->human_readable_game_description) &&
		variant->human_readable_game_description[index]; index++)
	{
		wchar_t character = variant->human_readable_game_description[index];

		gametype[index] = (char)(character >= 0x20 && character < 0x7F ? character : '?');
		gametype[index + 1] = 0;
	}
	if (gametype[0])
		snprintf(text, size, "%s, %s", map_name, gametype);
	else
		snprintf(text, size, "%s (%s)", map_name, variant ? "multiplayer" : "campaign");
}

/* a name a mod.toml or a map's file gives, as one line's text */
static void report_game_name(char *name)
{
	for (; *name; name++)
	{
		if ((unsigned char)*name < 0x20 || *name == 0x7F)
			*name = ' ';
	}
}

/* the size of a file, by its Xbox path (0: none, or unreadable) */
static unsigned long report_game_file_size(char const *path)
{
	FILE *file = fopen(path, "rb");
	long size = 0;

	if (!file)
		return 0;
	if (!fseek(file, 0, SEEK_END))
		size = ftell(file);
	fclose(file);

	return size > 0 ? (unsigned long)size : 0;
}

void report_game_mods_and_maps(char *text, size_t size)
{
	char const *map_name = cache_file_loaded_map_name();
	char const *mod_map = mods_map_loaded() ? mods_map_name() : NULL;
	char mods[2048], file[256], path[1024], line[160];
	unsigned long file_size = 0;
	long counts[4];
	boolean in_game, played;
	int mod_count, listed = 0;
	size_t used;
	char *at;

	if (!size)
		return;
	text[0] = 0;
	/* (a map played, not the menus') */
	in_game = map_name && map_name[0] && strcmp(map_name, "ui") && report_game_playing();
	if (!in_game)
		mod_map = NULL;
	/* (the mods: those on, in the load order, each by its mod.toml's name
	and version) */
	mod_count = port_mods_report(mods, sizeof(mods), mod_map, REPORT_GAME_MODS_LISTED);
	for (at = mods; *at; at++)
	{
		if (*at == '\n')
			listed++;
	}
	played = mods_played_on_map(counts) && in_game;
	if (!mod_count)
		snprintf(text, size, "- Mods: none%s\n", loose_sounds_enabled() ? " (loose sound files on: audio.loose_sounds)" : "");
	else
	{
		line[0] = 0;
		if (played && !(counts[0] | counts[1] | counts[2] | counts[3]))
			snprintf(line, sizeof(line), ", none of the map's tags theirs");
		else if (played)
			snprintf(line, sizeof(line), ", playing %ld bitmaps, %ld sounds, %ld models, %ld string lists of the map",
				counts[1], counts[2], counts[3], counts[0]);
		else if (in_game)
			snprintf(line, sizeof(line), ", not played on this map");
		snprintf(text, size, "- Mods, in load order: %d on%s%s\n", mod_count, line,
			loose_sounds_enabled() ? " (and loose sound files: audio.loose_sounds)" : "");
		used = strlen(text);
		snprintf(text + used, size - used, "%s", mods);
		used = strlen(text);
		if (listed < mod_count)
			snprintf(text + used, size - used, "  - and %d more\n", mod_count - listed);
	}
	used = strlen(text);

	/* (the map: by its file's name and size, and the CRC its cache header
	gives, the loader's, the file not read again) */
	if (!in_game)
		snprintf(text + used, size - used, "- Map: none (the menus)\n");
	else if (custom_edition_cache_tags_loaded())
	{
		char const *loaded = custom_edition_cache_loaded_path();
		char const *slash = loaded ? strrchr(loaded, '\\') : NULL;

		if (loaded && strrchr(loaded, '/') > slash)
			slash = strrchr(loaded, '/');
		snprintf(file, sizeof(file), "%s", slash ? slash + 1 : loaded ? loaded : "unknown");
		report_game_name(file);
		file_size = loaded ? report_game_file_size(loaded) : 0;
		if (file_size)
			snprintf(line, sizeof(line), "%.1f MB, ", file_size / 1048576.0);
		else
			line[0] = 0;
		snprintf(text + used, size - used, "- Map: %s%s, Custom Edition (%s, %sheader CRC %08lx)\n",
			CUSTOM_EDITION_LEVEL_NAME_PREFIX, map_name, file, line, cache_files_get_checksum());
	}
	else
	{
		snprintf(file, sizeof(file), "%s.map", map_name);
		report_game_name(file);
		snprintf(path, sizeof(path), "%s%s", cache_files_map_directory(), file);
		file_size = report_game_file_size(path);
		if (file_size)
			snprintf(line, sizeof(line), "%.1f MB, ", file_size / 1048576.0);
		else
			line[0] = 0;
		snprintf(text + used, size - used, "- Map: %s, stock (%s, %sheader CRC %08lx)\n", map_name, file, line,
			cache_files_get_checksum());
	}
	used = strlen(text);
	snprintf(text + used, size - used, "- Custom Edition maps (game.custom_edition): %s\n",
		config_boolean("game.custom_edition") ? "on" : "off");
}

/* a direction's yaw (from the x axis) and pitch (up), in degrees */
static void report_game_angles(real_vector3d const *forward, float *yaw, float *pitch)
{
	real level = (real)sqrt(forward->i * forward->i + forward->j * forward->j);

	*yaw = (float)atan2(forward->j, forward->i) * REPORT_DEGREES;
	if (*yaw < 0.0f)
		*yaw += 360.0f;
	*pitch = (float)atan2(forward->k, level) * REPORT_DEGREES;
}

/* (the host) the other machines' builds as they told them, a count of each */
static void report_game_builds(struct report_where *where)
{
	long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	short count = distributed_client_machines(machines, NUMBEROF(machines));
	short index;

	for (index = 0; index < count; index++)
	{
		struct distributed_client_build build;
		long number;
		int kind;

		if (network_standard_machine_is_opence(machines[index]))
			number = REPORT_WHERE_OPENCE;
		else if (distributed_machine_client_build(machines[index], &build))
			number = strtol(build.number, NULL, 10) > 0 ? strtol(build.number, NULL, 10) : 0;
		else
		{
			where->builds_unknown++;
			continue;
		}
		for (kind = 0; kind < where->build_kinds && where->builds[kind] != number; kind++)
			;
		if (kind == where->build_kinds)
		{
			if (kind >= REPORT_WHERE_BUILDS)
			{
				where->builds_unknown++;
				continue;
			}
			where->builds[kind] = number;
			where->build_counts[kind] = 0;
			where->build_kinds++;
		}
		where->build_counts[kind]++;
	}
}

void report_game_where(struct report_where *where)
{
	char const *map_name = global_scenario_index != NONE ? tag_get_name(global_scenario_index) : NULL;
	struct observer_result const *camera;
	long player_index;
	short connection;

	csmemset(where, 0, sizeof(*where));
	if (!report_game_playing())
		return;
	where->playing = TRUE;
	report_game_describe(where->game, sizeof(where->game));
	/* (the level's name, as map_name takes it: a Custom Edition map's is its
	file's in custom_maps, not its scenario's) */
	if (custom_edition_cache_tags_loaded())
		snprintf(where->map, sizeof(where->map), "%s%s", CUSTOM_EDITION_LEVEL_NAME_PREFIX, cache_file_loaded_map_name());
	else
		snprintf(where->map, sizeof(where->map), "%s", map_name);
	where->campaign = !game_engine_running();
	where->difficulty = game_difficulty_level_get();
	where->bsp = global_structure_bsp_index;
	where->ticks = game_time_get();
	where->ticks_per_second = TICKS_PER_SECOND;
	/* (the first local player: its unit's place, its vehicle's in one) */
	player_index = local_player_get_player_index(0);
	if (player_index != NONE)
	{
		struct player_datum *player = player_get(player_index);
		struct unit_datum *unit = player->unit_index != NONE ? unit_try_and_get(player->unit_index) : NULL;

		where->player = TRUE;
		if (unit)
		{
			struct object_datum *place = (struct object_datum *)unit;

			where->alive = TRUE;
			if (unit->object.parent_object_index != NONE)
			{
				place = object_get(unit->object.parent_object_index);
				snprintf(where->vehicle, sizeof(where->vehicle), "%s", tag_get_name(place->definition_index));
			}
			where->position[0] = place->object.position.x;
			where->position[1] = place->object.position.y;
			where->position[2] = place->object.position.z;
			report_game_angles(&unit->unit.aiming_vector, &where->yaw, &where->pitch);
		}
	}
	/* (the camera, as debug_camera_set takes it: its roll from the up of its
	forward with none, camera_pose_set's) */
	camera = observer_get_camera(0);
	if (camera)
	{
		float cy, sy, cp, sp;
		real_vector3d level_up, right;

		where->camera = TRUE;
		where->camera_position[0] = camera->position.x;
		where->camera_position[1] = camera->position.y;
		where->camera_position[2] = camera->position.z;
		report_game_angles(&camera->forward, &where->camera_yaw, &where->camera_pitch);
		cy = (float)cos(where->camera_yaw / REPORT_DEGREES);
		sy = (float)sin(where->camera_yaw / REPORT_DEGREES);
		cp = (float)cos(where->camera_pitch / REPORT_DEGREES);
		sp = (float)sin(where->camera_pitch / REPORT_DEGREES);
		level_up.i = -cy * sp;
		level_up.j = -sy * sp;
		level_up.k = cp;
		right.i = sy * cp * level_up.k - sp * level_up.j;
		right.j = sp * level_up.i - cy * cp * level_up.k;
		right.k = cy * cp * level_up.j - sy * cp * level_up.i;
		where->camera_roll = (float)atan2(
			camera->up.i * right.i + camera->up.j * right.j + camera->up.k * right.k,
			camera->up.i * level_up.i + camera->up.j * level_up.j + camera->up.k * level_up.k) * REPORT_DEGREES;
		where->camera_field_of_view = camera->field_of_view * REPORT_DEGREES;
	}
	/* (a network game: this machine's part, the netcode, the round trip,
	the players; no one's name or address) */
	connection = game_connection();
	if (connection == _game_connection_network_server || connection == _game_connection_network_client)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		where->network = connection == _game_connection_network_server ? 1 : 2;
		where->rate = network_standard_agreed_rate();
		where->ping = NONE;
		data_iterator_new(&iterator, player_data);
		while ((player = data_iterator_next(&iterator)) != NULL)
		{
			short index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			long ping = distributed_player_ping(index);

			where->players++;
			/* (a client: its own player's, as the host measured it; the
			host: its slowest client's) */
			if (ping == NONE || (where->network == 2 && iterator.datum_index != player_index))
				continue;
			if (ping > where->ping)
				where->ping = ping;
		}
		if (where->ping == 0 && where->network == 1)
			where->ping = NONE;
		where->connection_problem = distributed_connection_problem();
		if (where->network == 1)
			report_game_builds(where);
	}
}

/* the line for the note: the text chat's (network_chat.c) */
int report_game_note_begin(void)
{
	if (!network_chat_report_note_begin())
		return FALSE;
	console_printf(FALSE, "Report captured: type what happened, then Enter (Esc skips)");
	return TRUE;
}

void report_game_note_cancel(void)
{
	network_chat_report_note_cancel();
}

void report_game_say(const char *text)
{
	if (text && text[0])
		console_printf(FALSE, "%s", text);
}
