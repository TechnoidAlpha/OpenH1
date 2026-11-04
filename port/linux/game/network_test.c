/*
NETWORK_TEST.C

Automated system link sessions for testing the netcode without the menus
(debug.network_test in config.toml, HALO_NETWORK_TEST):

- "host:<map>[:<variant>[,<variant>...]]" hosts a game on that
  multiplayer map (bloodgulch, ...) with one of the built-in game variants
  (slayer by default; game_engine_get_variant_by_name), as the pregame
  screen's fast setup does, and starts it debug.network_test_start seconds
  later; with more variants, once a game is over (debug.network_test_score
  makes it short) the next, as the host's button on the scores does (also
  when the host's playlist or a change has a next game: network_host_game.c,
  which sets it up in the lobby and starts it). A map
  with a path is that level (custom_maps\a30, a Custom Edition map's), and
  the variant "coop" makes the game network co-op on it, at normal
  difficulty, as the Map screen does for a campaign level; a variant with
  a rotation entry's rules (team_slayer+ghosts=2+banshees=1) is built as a
  dedicated server builds that entry's;
- "local:<map>[:<variant>...]" the same with a local (split screen) game of
  one player, as Multiplayer > Split Screen of the Xbox's menus sets one up
  (no other machine joins it; the bots of bots.count join it, as they join
  a hosted game);
- "join" searches for games and joins the first it finds, as picking it in
  the system link list does;
- "browse" joins the game with the most players that the server browser
  lists after five seconds (internet play's public games: a host's with
  debug.network_test_public), co-op ones last, as picking it there does,
  and then as "join". It logs what it lists.
- "follow" (a party's member: debug.party) joins its party's leader's
  internet game once the party asks it to (p2p_party_follow), its invite's
  host reached, and then as "join": the Party screen's follow without the
  menus (and so without a player profile);
- "log" hosts and joins nothing: the game that runs (map_name, in init.txt,
  plays one without the menus) is logged, as below.
- "probe" (halo --probe [<invite>]: a list site's look at a server) searches
  as "join" does, joins nothing, and prints the first game it finds (with
  an invite, only its host's, over its tunnel; without, one on the LAN) on
  the standard output as one line of JSON, then exits (1, with ok false, if
  none is found in PROBE_SECONDS).

Once the game runs, every second each machine logs where every player's
unit is, so the machines' views of the game can be compared, and the host
how far its own run of each client's player came from the client's
(network_distributed_divergence): tools/net_compare.py compares two builds
so (port/linux/NETCODE.md, "Comparing two builds", has the line's format).

Scripted play for the netcode's parts the bots' wandering does not reach:
debug.network_test_kill (the host kills the last player every so often),
debug.network_test_shoot (every so often each machine's player hits the
next with their weapon's projectile, when the level's structure leaves a
line from their eye to the target, as the host checks: a client's through
its report to the host) and debug.network_test_vehicle (the host seats the last player as a
vehicle's driver that many seconds in, takes them out 15 seconds on, and 5
seconds later stands them at a vehicle's driver's entrance, where a joining
machine's player holds the action button, as getting in does; with
debug.network_test_hitch the frame then stops for that many seconds, as a
stall of the renderer or of the VR runtime stops it, which the host must
play through)
and debug.network_test_pickup (the last player stands on a weapon lying
about that many seconds in, and a joining machine's player holds the action
button a second later, to pick it up).

debug.network_test_cues: console lines the host (or a game of one machine)
runs at their ticks, counted from the first tick every player of
debug.network_test_cue_players has a unit (tools/stagekit's scenarios).

The latency figures (NETCODE.md, "Measuring latency"), on the computer's own
clock (CLOCK_MONOTONIC, in microseconds, which every copy on one computer
shares): each machine logs when each player's unit first takes a pull of
the trigger or a push of the stick, at the sub-step that takes it ("press",
local on the player's own machine, remote on the others), and when a hit of
its own player's is reported, dealt by the host and its damage shown;
tools/net_compare.py --latency pairs them up.

Called from the main loop every frame (main.c).
*/

#include "cseries.h"
#include "game_modes.h"
#include "main/main.h"
#include "bink/bink_playback.h"
#include "interface/player_ui.h"
#include "saved games/player_profile.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "items/projectile_definitions.h"
#include "items/items.h"
#include "objects/damage.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "tag_files/tag_files.h"
#include "camera/observer.h"
#include "hs/hs.h"
#include "network_clock.h"
#include "network_distributed.h"

#include "../src/p2p.h" /* port: port/linux/src/p2p.c, p2p_lobby.c */
#include "network_chat.h"
#include "server_gametypes.h"
#include "network_party.h"
#include "network_voice.h"
#include "sim_rate.h"
#include "network_standard.h"
#include "network_host_game.h"
#include "halo_keyboard.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
int config_boolean(char const *name);
double config_real(char const *name);
long config_integer(char const *name);
int config_write(char const *name, char const *value);
void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
/* xbox_kernel.c's: the computer's monotonic clock, shared by its processes */
unsigned long long platform_shared_clock_microseconds(void);
/* bsp_switch_test.c's */
void bsp_switch_test_update(boolean main_menu_loaded);
/* damage.c's */
void damage_kill_object_for_player(long object_index, long player_index);
/* network_distributed.c's */
void network_distributed_statistics(long *sent, long *received, long *corrections);
void network_distributed_remote_statistics(long *corrections, long *history_corrections, long *history_kept);
void network_distributed_batch_bytes(long *to_host, long *to_clients, short *clients);
void network_distributed_batch_datagrams(long *to_host, long *to_clients);
void network_distributed_divergence(long *samples, long *apart, real *mean, real *largest);
void network_distributed_player_divergence(short player_index, long *samples, long *apart);
void network_distributed_item_statistics(long *creates, long *deletes, long *failures, long *removed);
void network_damage_statistics(long *sent_reports, long *dealt_reports, long *rejected_reports, long *replayed_events);
void network_damage_prediction_statistics(long *shown, long *answered, long *unanswered, long *kills,
	long *kills_confirmed, long *kills_refused);
long distributed_own_ping(void);
long distributed_player_measured_ping(short player_index);
/* xinput_sdl.c's */
void test_input_hold_action(int hold);
void test_input_press(unsigned long buttons);
/* menu_functions.c's: whether the client's advertised game at the index is
an internet play peer's (over the tunnel), not the LAN's */
boolean menu_advertised_game_from_peer(long index);

enum
{
	/* how long a probe looks for a game (halo --probe), and the client's
	advertised games (network_client_manager.c's) */
	PROBE_SECONDS = 30,
	PROBE_MAXIMUM_GAMES = 9,
};

enum
{
	_network_test_off,
	_network_test_host,
	_network_test_join,
	_network_test_browse,
	_network_test_follow,
	_network_test_log,
	_network_test_probe,
};

static struct
{
	boolean checked;
	short mode;
	char map_name[128];
	char variant_name[128];
	/* "local:": a local (split screen) game, not a system link one; its
	players (debug.network_test_players: split screen's views, the first
	controller's the first) */
	boolean local;
	short local_players;
	/* ... the variant of this game, of variant_name's list; and the seconds
	the last game's scores have been shown */
	short variant_index;
	real postgame_seconds;
	boolean game_over;
	real start_delay;
	real menu_seconds;
	boolean set_up;
	/* (the hosted game's whole invite logged, for the test that joins it:
	the game's own lines give its first digits only, since players post
	their logs. This is debug.network_test's host alone, a test's machine,
	whose log nobody posts) */
	boolean invite_logged;
	real setup_seconds;
	boolean started;
	boolean joined;
	boolean map_set;
	boolean player_added;
	real joined_seconds;
	boolean team_set;
	real kill_interval;
	real shoot_interval;
	real vehicle_time;
	real hitch_seconds;
	real pickup_time;
	char pickup_weapon[64];
	/* debug.network_test_chat's, and the lines said this game */
	real chat_time;
	short chat_lines_said;
	/* debug.network_test_party's */
	real party_time;
	long score_to_win;
	/* the most players the host's games take (debug.network_test_most_players;
	0: the game's own) */
	long maximum_players;
	long logged_time;
	boolean public_game;
	boolean browsing;
	boolean browse_listed;
	float browse_seconds;
	float probe_seconds;
	/* debug.network_test_cues: each line and its tick from the cues' start,
	the first tick every player of cue_players has a unit (NONE until then) */
	struct
	{
		long tick;
		char line[256];
	} cues[32];
	short cue_count;
	short next_cue;
	long cue_players;
	long cues_start;
	/* debug.network_test_lobby_cues: the next one's place in its text */
	short next_lobby_cue;
} network_test;

/* the latency figures' presses: each player's unit at the last sub-step,
and what of it was down (a bit a kind of press) */
enum
{
	_network_test_press_fire,
	_network_test_press_move,
	NUMBER_OF_NETWORK_TEST_PRESSES
};
static struct
{
	long unit_index[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	byte down[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
} network_test_presses;

static void network_test_presses_substep(short substep_index, short substep_count, real seconds);

/* the variant at the index of the list (copied to name), FALSE past its end */
static boolean network_test_variant(
	short index,
	char *name,
	size_t size)
{
	char const *variant = network_test.variant_name;

	for (; index > 0 && variant; index--)
	{
		variant = strchr(variant, ',');
		if (variant)
			variant++;
	}
	if (!variant || !*variant)
		return FALSE;
	if (name)
	{
		size_t length = strcspn(variant, ",");

		snprintf(name, size, "%.*s", (int)length, variant);
	}
	return TRUE;
}

static void network_test_read_settings(
	void)
{
	char const *setting = config_string("debug.network_test");

	network_test.checked = TRUE;
	if ((!strncmp(setting, "host:", 5) && setting[5]) || (!strncmp(setting, "local:", 6) && setting[6]))
	{
		char *colon;

		network_test.mode = _network_test_host;
		network_test.local = setting[0] == 'l';
		snprintf(network_test.map_name, sizeof(network_test.map_name), "%s", strchr(setting, ':') + 1);
		snprintf(network_test.variant_name, sizeof(network_test.variant_name), "slayer");
		colon = strchr(network_test.map_name, ':');
		if (colon)
		{
			*colon = 0;
			snprintf(network_test.variant_name, sizeof(network_test.variant_name), "%s", colon + 1);
		}
	}
	else if (!strcmp(setting, "join"))
	{
		network_test.mode = _network_test_join;
	}
	else if (!strcmp(setting, "browse"))
	{
		network_test.mode = _network_test_browse;
	}
	else if (!strcmp(setting, "follow"))
	{
		network_test.mode = _network_test_follow;
	}
	else if (!strcmp(setting, "log"))
	{
		network_test.mode = _network_test_log;
	}
	else if (!strcmp(setting, "probe"))
	{
		network_test.mode = _network_test_probe;
	}
	network_test.public_game = config_boolean("debug.network_test_public") != 0;
	network_test.start_delay = (real)config_real("debug.network_test_start");
	network_test.local_players = (short)PIN(config_integer("debug.network_test_players"), 1, MAXIMUM_NUMBER_OF_LOCAL_PLAYERS);
	network_test.kill_interval = (real)config_real("debug.network_test_kill");
	network_test.shoot_interval = (real)config_real("debug.network_test_shoot");
	network_test.vehicle_time = (real)config_real("debug.network_test_vehicle");
	network_test.hitch_seconds = (real)config_real("debug.network_test_hitch");
	network_test.pickup_time = (real)config_real("debug.network_test_pickup");
	network_test.chat_time = (real)config_real("debug.network_test_chat");
	network_test.party_time = (real)config_real("debug.network_test_party");
	snprintf(network_test.pickup_weapon, sizeof(network_test.pickup_weapon), "%s",
		config_string("debug.network_test_pickup_weapon"));
	network_test.score_to_win = (long)config_integer("debug.network_test_score");
	network_test.maximum_players = (long)config_integer("debug.network_test_most_players");
	network_test.cue_players = MAX(1, config_integer("debug.network_test_cue_players"));
	network_test.cues_start = NONE;
	{
		char const *cue = config_string("debug.network_test_cues");

		while (*cue)
		{
			char const *end = strchr(cue, '|');
			int length = end ? (int)(end - cue) : (int)strlen(cue);
			char *line;
			double second = strtod(cue, &line);
			short index;

			if (!(second >= 0.0 && second < 86400.0) || line == cue || line >= cue + length ||
				cue + length - line >= (int)sizeof(network_test.cues[0].line))
			{
				platform_log("network test: cue \"%.*s\" is not \"<second> <line>\" of %d characters at most: left out",
					length, cue, (int)sizeof(network_test.cues[0].line) - 1);
			}
			else if (network_test.cue_count >= (short)NUMBEROF(network_test.cues))
			{
				platform_log("network test: cues past %d left out", (int)NUMBEROF(network_test.cues));
			}
			else
			{
				/* (in the order they run) */
				long tick = (long)(second * TICKS_PER_SECOND + 0.5);

				for (index = network_test.cue_count; index > 0 && network_test.cues[index - 1].tick > tick; index--)
					network_test.cues[index] = network_test.cues[index - 1];
				network_test.cues[index].tick = tick;
				snprintf(network_test.cues[index].line, sizeof(network_test.cues[index].line), "%.*s",
					(int)(cue + length - line), line);
				network_test.cue_count++;
			}
			cue += length + (end != NULL);
		}
	}
	if (network_test.mode != _network_test_off)
		platform_log("network test: %s", setting);
	/* (the latency figures' presses, each sub-step: none without a test) */
	if (network_test.mode != _network_test_off && network_test.mode != _network_test_probe)
	{
		short player_index;

		for (player_index = 0; player_index < HALO_PORT_MAXIMUM_NETWORK_PLAYERS; player_index++)
			network_test_presses.unit_index[player_index] = NONE;
		sim_substep_register(_sim_substep_network_test, network_test_presses_substep);
	}
}

/* the computer's own clock, in microseconds: the same in every copy on one
computer, so that a press on one and its sight on another are timed alike */
static unsigned long long network_test_microseconds(
	void)
{
	return platform_shared_clock_microseconds();
}

/* (each sub-step, _sim_substep_network_test) each player's unit's pull of
the trigger and push of the stick, as the sub-step's units took them: on the
player's own machine their press, on the others its sight */
static void network_test_presses_substep(
	short substep_index,
	short substep_count,
	real seconds)
{
	static char const *const names[NUMBER_OF_NETWORK_TEST_PRESSES] = { "fire", "move" };
	short connection = game_connection();
	struct data_iterator iterator;
	struct player_datum *player;

	(void)substep_index;
	(void)substep_count;
	(void)seconds;
	if (connection != _game_connection_network_client && connection != _game_connection_network_server)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		struct unit_datum *unit;
		byte down = 0;
		byte pressed;
		short kind;

		if (absolute_index < 0 || absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS)
			continue;
		if (player->unit_index == NONE)
		{
			network_test_presses.unit_index[absolute_index] = NONE;
			continue;
		}
		unit = unit_get(player->unit_index);
		if (TEST_FLAG(unit->unit.control_flags, _unit_control_weapon_primary_trigger_bit))
			down |= FLAG(_network_test_press_fire);
		if (unit->unit.throttle.i * unit->unit.throttle.i + unit->unit.throttle.j * unit->unit.throttle.j > 0.25f)
			down |= FLAG(_network_test_press_move);
		pressed = network_test_presses.unit_index[absolute_index] == player->unit_index ?
			(byte)(down & ~network_test_presses.down[absolute_index]) : 0;
		network_test_presses.unit_index[absolute_index] = player->unit_index;
		network_test_presses.down[absolute_index] = down;
		for (kind = 0; kind < NUMBER_OF_NETWORK_TEST_PRESSES; kind++)
		{
			if (TEST_FLAG(pressed, kind))
			{
				platform_log("network test: press %s player %ld %s at %llu", names[kind], absolute_index,
					player->local_player_index != NONE ? "local" : "remote", network_test_microseconds());
			}
		}
	}
}

/* (network_damage.c) a hit of this machine's player made (reported), dealt
by the host, or its damage shown on this machine */
void network_test_note_hit(
	char const *what,
	long object_index)
{
	if (network_test.mode == _network_test_off || network_test.mode == _network_test_probe)
		return;
	platform_log("network test: hit %s object %lx at %llu", what, (unsigned long)object_index,
		network_test_microseconds());
}

/* appends to a line, cut short when it is full */
static void network_test_append(
	char *line,
	int size,
	int *length,
	char const *format,
	...)
{
	va_list arguments;
	int written;

	if (*length >= size - 1)
		return;
	va_start(arguments, format);
	written = vsnprintf(line + *length, (size_t)(size - *length), format, arguments);
	va_end(arguments);
	if (written > 0)
		*length = MIN(*length + written, size - 1);
}

/* every player's unit, as this machine sees it */
static void network_test_log_players(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	char line[4096];
	int length = 0;

	line[0] = 0;
	/* (each player's name, once a game: for tests that name a player, such
	as the host's ban command) */
	{
		static wchar_t named[HALO_PORT_MAXIMUM_NETWORK_PLAYERS][12];

		if (game_time_get() < 2 * TICKS_PER_SECOND)
			csmemset(named, 0, sizeof(named));
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			long absolute_index = DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
			char name[13];
			short index;

			if (absolute_index >= HALO_PORT_MAXIMUM_NETWORK_PLAYERS ||
				!csmemcmp(named[absolute_index], player->name, sizeof(named[absolute_index])))
			{
				continue;
			}
			csmemcpy(named[absolute_index], player->name, sizeof(named[absolute_index]));
			for (index = 0; index < 12 && player->name[index]; index++)
				name[index] = player->name[index] >= 32 && player->name[index] < 127 ? (char)player->name[index] : '?';
			name[index] = 0;
			platform_log("network test: player %ld is named %s", absolute_index, name);
		}
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index != NONE)
		{
			struct object_datum *object = object_get(player->unit_index);
			struct unit_datum *unit = unit_get(player->unit_index);
			short slot;

			/* (riding: where its vehicle is) */
			struct object_datum *placed = object->object.parent_object_index != NONE ?
				object_get(object->object.parent_object_index) : object;

			network_test_append(line, (int)sizeof(line), &length, " player %ld: (%.3f %.3f %.3f) h%.2f/%.2f%s%s g%d/%d w",
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), placed->object.position.x,
				placed->object.position.y, placed->object.position.z, object->object.body_vitality,
				object->object.shield_vitality, placed != object ? " riding" : "",
				TEST_FLAG(unit->unit.flags, _unit_active_camouflaged_bit) ? " camo" : "",
				unit->unit.grenade_counts[0], unit->unit.grenade_counts[1]);
			/* where it aims (yaw and pitch, degrees), its animation state and
			how hard it is moving */
			network_test_append(line, (int)sizeof(line), &length, " a%.0f/%.0f f%.0f l%.0f/%.0f as%d/%d st%d thr%.2f",
				atan2(unit->unit.aiming_vector.j, unit->unit.aiming_vector.i) * 57.29578,
				asin(PIN(unit->unit.aiming_vector.k, -1.0f, 1.0f)) * 57.29578,
				atan2(object->object.forward.j, object->object.forward.i) * 57.29578,
				atan2(unit->unit.looking_vector.j, unit->unit.looking_vector.i) * 57.29578,
				asin(PIN(unit->unit.looking_vector.k, -1.0f, 1.0f)) * 57.29578,
				(int)unit->unit.animation.aiming_screen_index, (int)unit->unit.animation.looking_screen_index,
				(int)unit->unit.animation.state,
				sqrt(unit->unit.throttle.i * unit->unit.throttle.i + unit->unit.throttle.j * unit->unit.throttle.j));
			for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
			{
				long weapon_index = unit->unit.weapon_object_indices[slot];

				if (weapon_index != NONE)
				{
					struct weapon_datum *weapon = weapon_get(weapon_index);

					network_test_append(line, (int)sizeof(line), &length, " %lx:%d",
						(unsigned long)weapon->definition_index & 0xFFFF, weapon->weapon.magazines[0].rounds_total +
						weapon->weapon.magazines[0].rounds_loaded);
				}
			}
		}
		else
		{
			network_test_append(line, (int)sizeof(line), &length, " player %ld: dead",
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index));
		}
		/* the game type's score and the kills and deaths */
		network_test_append(line, (int)sizeof(line), &length, " s%ld k%d d%d f%d t%ld m%d",
			game_engine && game_engine->get_player_score ?
				game_engine->get_player_score(iterator.datum_index, _get_score_individual) : -1L,
			player->statistics.kills[0], player->statistics.deaths, player->statistics.friendly_fire_kills, (long)player->team_index,
			(int)player->network_player_data.machine_index);
	}
	{
		long sent, received, corrections;
		struct object_iterator objects;
		long ground_items = 0;
		long creates, deletes, failures, removed;
		long sent_reports, dealt_reports, rejected_reports, replayed_events;
		/* this machine's player and where its camera is (a player that never
		spawns leaves it where it began) */
		long local_player_index = local_player_get_player_index(0);
		struct observer_result const *camera = observer_get_camera(0);
		long divergence_samples, divergence_apart;
		real divergence_mean, divergence_largest;

		object_iterator_new(&objects, _object_mask_weapon | _object_mask_equipment, 0);
		while (object_iterator_next(&objects))
		{
			struct item_datum *item = item_get(objects.index);

			if (item->object.parent_object_index == NONE &&
				TEST_FLAG(item->object.flags, _object_connected_to_map_bit) &&
				!TEST_FLAG(item->item.flags, _item_attached_to_unit_bit))
			{
				ground_items++;
			}
		}
		network_distributed_statistics(&sent, &received, &corrections);
		network_distributed_item_statistics(&creates, &deletes, &failures, &removed);
		network_damage_statistics(&sent_reports, &dealt_reports, &rejected_reports, &replayed_events);
		network_distributed_divergence(&divergence_samples, &divergence_apart, &divergence_mean, &divergence_largest);

		platform_log("network test: tick %ld%s | items %ld (+%ld -%ld !%ld x%ld) | %s to %ld | sent %ld received %ld corrected %ld"
			" | hits %ld dealt %ld rejected %ld replayed %ld | local %ld camera (%.1f %.1f %.1f) respawn %ld"
			" | drift %ld %ld %.6f %.6f",
			game_time_get(), line, ground_items, creates, deletes, failures, removed,
			game_engine_can_score() ? "playing" : "game over",
			game_engine_running() ? (long)game_engine_get_variant()->universal_variant.score_to_win : 0L,
			sent, received, corrections,
			sent_reports, dealt_reports, rejected_reports, replayed_events,
			local_player_index == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(local_player_index),
			camera ? camera->position.x : 0.0f, camera ? camera->position.y : 0.0f, camera ? camera->position.z : 0.0f,
			local_player_index == NONE ? 0L : (long)player_get(local_player_index)->respawn_timer,
			divergence_samples, divergence_apart, divergence_mean, divergence_largest);
		/* (a client: its own players' hits it showed at once, and how the
		host's word on them went: network_damage.c) */
		if (game_connection() == _game_connection_network_client)
		{
			long shown, answered, unanswered, kills, kills_confirmed, kills_refused;

			network_damage_prediction_statistics(&shown, &answered, &unanswered, &kills, &kills_confirmed,
				&kills_refused);
			platform_log("network test: predicted hits %ld answered %ld unanswered %ld | kills %ld confirmed %ld"
				" refused %ld", shown, answered, unanswered, kills, kills_confirmed, kills_refused);
		}
		/* (the bytes of the second's datagrams, the per-tick stream: to the
		host, and to the clients, each) */
		{
			long to_host, to_clients;
			short clients;

			network_distributed_batch_bytes(&to_host, &to_clients, &clients);
			platform_log("network test: bandwidth to host %ld to clients %ld (%d clients, %ld each) bytes a second",
				to_host, to_clients, (int)clients, clients ? to_clients / clients : 0L);
			network_distributed_batch_datagrams(&to_host, &to_clients);
			platform_log("network test: datagrams to host %ld to clients %ld a second", to_host, to_clients);
			/* (the host's actors' states of those, and the entries a press or
			an aim alone sent) */
			if (game_connection() == _game_connection_network_server)
			{
				long all, pressed, aimed;

				network_actors_bytes_sent(&all, &pressed, &aimed);
				platform_log("network test: actors to clients %ld (pressed %ld aimed %ld) bytes a second", all,
					pressed, aimed);
			}
		}
		/* (a client: its corrections of the other players' units, all and
		against its history of them, and the host's states within that
		history's tolerance it would have pulled them back to before: since
		the game began) */
		{
			long remote_corrections, history_corrections, history_kept;

			network_distributed_remote_statistics(&remote_corrections, &history_corrections, &history_kept);
			platform_log("network test: remote corrected %ld history %ld kept %ld", remote_corrections,
				history_corrections, history_kept);
		}
		/* (the host: its ticks of each take of its OpenH1 clients' players'
		predictions as its ticks began, and the drift samples of the ticks
		after each, apart of compared: since the game began) */
		if (game_connection() == _game_connection_network_server)
		{
			long ticks[NUMBER_OF_DISTRIBUTED_TAKES];
			long samples[NUMBER_OF_DISTRIBUTED_TAKES];
			long apart[NUMBER_OF_DISTRIBUTED_TAKES];

			network_distributed_take_statistics(ticks, samples, apart);
			platform_log("network test: takes none %ld latest %ld late %ld earlier %ld lost %ld hold %ld"
				" | drift after latest %ld/%ld late %ld/%ld earlier %ld/%ld lost %ld/%ld hold %ld/%ld",
				ticks[_distributed_take_none], ticks[_distributed_take_latest], ticks[_distributed_take_late],
				ticks[_distributed_take_earlier], ticks[_distributed_take_lost], ticks[_distributed_take_hold],
				apart[_distributed_take_latest], samples[_distributed_take_latest],
				apart[_distributed_take_late], samples[_distributed_take_late],
				apart[_distributed_take_earlier], samples[_distributed_take_earlier],
				apart[_distributed_take_lost], samples[_distributed_take_lost],
				apart[_distributed_take_hold], samples[_distributed_take_hold]);
		}
		/* (a client of an OpenH1 game: the host's latest word on how long
		before its tick of their number this machine's inputs came, the
		words taken, and the ticks its clock skipped and the time it moved
		by to lead the host's, since the game began: network_clock.c) */
		if (game_connection() == _game_connection_network_client)
		{
			long early, reports, skipped;
			real moved;

			network_clock_statistics(&early, &reports, &skipped, &moved);
			platform_log("network test: clock early %ld reports %ld skipped %ld moved %ld us", early, reports,
				skipped, (long)(moved * 1000000.0f));
		}
		/* (the latency meter's round trips, with display.latency_meter on:
		a client's own, and each player's as this machine times it) */
		{
			char pings[512];
			int pings_length = 0;
			struct data_iterator players;

			pings[0] = 0;
			data_iterator_new(&players, player_data);
			while (data_iterator_next(&players))
			{
				short absolute_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(players.datum_index);
				long ping = distributed_player_measured_ping(absolute_index);

				if (ping != NONE)
					network_test_append(pings, (int)sizeof(pings), &pings_length, " %d:%ld", (int)absolute_index, ping);
			}
			platform_log("network test: ping own %ld | players%s", distributed_own_ping(),
				pings_length ? pings : " none");
		}
		/* (mixed games, network_standard.c: this machine's rate, the players it
		steps at 30 as OpenCE's, and, a client that joined as OpenCE does, what
		of OpenH1's own it heard: none of OpenH1's messages, no rate in its
		record; the host's drift, each client's player's) */
		{
			char opence[128];
			char drift[256];
			int opence_length = 0;
			int drift_length = 0;
			long openh1_messages, record_rate;
			struct data_iterator players;

			opence[0] = 0;
			drift[0] = 0;
			data_iterator_new(&players, player_data);
			while (data_iterator_next(&players))
			{
				short absolute_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(players.datum_index);
				long samples, apart;

				if (network_standard_player_is_opence(players.datum_index))
					network_test_append(opence, (int)sizeof(opence), &opence_length, " %d", (int)absolute_index);
				network_distributed_player_divergence(absolute_index, &samples, &apart);
				if (samples)
				{
					network_test_append(drift, (int)sizeof(drift), &drift_length, " %d:%ld/%ld", (int)absolute_index,
						apart, samples);
				}
			}
			network_standard_legacy_heard(&openh1_messages, &record_rate);
			platform_log("network test: rate %ld opence players%s | legacy heard openh1 %ld record %ld"
				" | drift by player%s",
				sim_rate_get(), opence_length ? opence : " none", openh1_messages, record_rate,
				drift_length ? drift : " none");
		}
	}
}

/* each of this machine's players hits the next player with their weapon's
projectile, as its impact would, when it is within the projectile's reach
(as a real shot is: the host checks it) */
static void network_test_shoot(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct data_iterator targets;
		struct player_datum *target = NULL;
		struct player_datum *candidate;
		struct unit_datum *unit;
		long weapon_index;
		struct weapon_definition *weapon;
		struct weapon_trigger_definition *trigger;
		long damage_index = NONE;
		real reach = 0.0f;
		boolean melee;
		struct damage_data damage;
		struct object_datum *target_object;
		real_vector3d direction;

		if (player->local_player_index == NONE || player->unit_index == NONE)
			continue;
		data_iterator_new(&targets, player_data);
		while ((candidate = (struct player_datum *)data_iterator_next(&targets)) != NULL)
		{
			if (candidate != player && candidate->unit_index != NONE)
			{
				target = candidate;
				break;
			}
		}
		unit = unit_get(player->unit_index);
		if (!target || unit->unit.current_weapon_index == NONE ||
			TEST_FLAG(object_get(target->unit_index)->object.damage_flags, _object_dead_bit))
		{
			continue;
		}
		weapon_index = unit->unit.weapon_object_indices[unit->unit.current_weapon_index];
		if (weapon_index == NONE)
			continue;
		weapon = weapon_definition_get(object_get(weapon_index)->definition_index);
		if (weapon->weapon.triggers.count > 0)
		{
			trigger = TAG_BLOCK_GET_ELEMENT(&weapon->weapon.triggers, 0, struct weapon_trigger_definition);
			if (trigger->projectile.index != NONE)
			{
				struct projectile_definition *projectile = projectile_definition_get(trigger->projectile.index);

				damage_index = projectile->projectile.impact_damage.index;
				/* (how far it flies: its range, or at its speed for as long as
				its timer runs; none for no bound, network_damage.c) */
				if (projectile->projectile.maximum_range > 0.0f)
					reach = projectile->projectile.maximum_range;
				else if (projectile->projectile.detonation_timer_starts == 0)
				{
					reach = projectile->projectile.timer_upper_bound * TICKS_PER_SECOND *
						MAX(projectile->projectile.initial_velocity, projectile->projectile.final_velocity);
				}
			}
		}
		/* (else its melee: a blow's epicenter is its striker's, as
		unit_cause_player_melee_damage has it) */
		melee = damage_index == NONE;
		if (melee)
			damage_index = weapon->weapon.melee_attack_damage.index;
		if (damage_index == NONE)
			continue;
		target_object = object_get(target->unit_index);
		/* (a blow reaches only a target at hand, as the host checks) */
		if (melee)
		{
			real dx = target_object->object.position.x - unit->object.position.x;
			real dy = target_object->object.position.y - unit->object.position.y;
			real dz = target_object->object.position.z - unit->object.position.z;

			if (dx * dx + dy * dy + dz * dz > 1.5f * 1.5f)
				continue;
		}
		/* (a shot reaches no further than its projectile flies, with a margin
		for how far the target is from where the shot started) */
		else if (reach > 0.0f)
		{
			real dx = target_object->object.position.x - unit->object.position.x;
			real dy = target_object->object.position.y - unit->object.position.y;
			real dz = target_object->object.position.z - unit->object.position.z;

			if (dx * dx + dy * dy + dz * dz > 0.9f * reach * 0.9f * reach)
				continue;
		}
		/* (a shot needs a line to the target, as the host checks:
		network_damage.c's distributed_report_line_of_sight, whose flags
		these are) */
		if (!melee)
		{
			unsigned long flags = FLAG(_collision_test_structure_bit) |
				FLAG(_collision_test_front_facing_surfaces_bit) | FLAG(_collision_test_ignore_invisible_surfaces_bit) |
				FLAG(_collision_test_ignore_breakable_surfaces_bit) | FLAG(_collision_test_ignore_two_sided_surfaces_bit);
			real_point3d eye;
			struct collision_result collision;

			unit_get_camera_position(player->unit_index, &eye);
			if (collision_test_line(flags, &eye, &target_object->object.bounding_sphere_center, NONE, &collision))
				continue;
		}
		damage_data_new(&damage, damage_index);
		damage.owner_player_index = iterator.datum_index;
		damage.owner_object_index = player->unit_index;
		damage.owner_team_index = unit->object.owner_team_index;
		damage.origin = melee ? unit->object.bounding_sphere_center : target_object->object.position;
		damage.epicenter = melee ? unit->object.bounding_sphere_center : target_object->object.position;
		direction.i = target_object->object.position.x - unit->object.position.x;
		direction.j = target_object->object.position.y - unit->object.position.y;
		direction.k = target_object->object.position.z - unit->object.position.z;
		normalize3d(&direction);
		damage.direction = direction;
		damage.scale = 1.0f;
		scenario_location_from_point(&damage.location, &damage.epicenter);
		object_cause_damage(&damage, target->unit_index, NONE, NONE, NONE, NULL);
		platform_log("network test: player %ld shoots player %ld",
			(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), (long)(target - (struct player_datum *)player_data->data));
	}
}

/* debug.network_test_hitch: the frame stopped for that many seconds, as
a stall of the renderer or of the VR runtime stops it (nothing sent, read
or ticked meanwhile) */
static void network_test_hitch(
	void)
{
	unsigned long start = system_milliseconds();
	unsigned long length = (unsigned long)(network_test.hitch_seconds * 1000.0f);

	if (network_test.hitch_seconds <= 0.0f)
		return;
	platform_log("network test: the frame stops for %.1f seconds", network_test.hitch_seconds);
	while (system_milliseconds() - start < length)
		;
}

/* the host seats the last player as the nearest vehicle's driver, or out */
/* the host brings the players far from the first (on foot) near it, so
that they are within their weapons' reach (network_test_shoot shoots only
so near, as a player does); the host moving a client's player, as a
teleporter does */
static void network_test_gather(
	boolean leave_last)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *first = NULL;
	struct player_datum *last = NULL;
	short count = 0;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (!first)
			first = player;
		last = player;
	}
	if (!first || first->unit_index == NONE)
		return;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		struct object_datum const *center = object_get(first->unit_index);
		struct object_datum *unit;
		real_point3d position;
		real dx, dy, dz;

		if (player == first || player->unit_index == NONE || (leave_last && player == last))
			continue;
		count++;
		unit = object_get(player->unit_index);
		if (unit->object.parent_object_index != NONE || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
			continue;
		dx = unit->object.position.x - center->object.position.x;
		dy = unit->object.position.y - center->object.position.y;
		dz = unit->object.position.z - center->object.position.z;
		if (dx * dx + dy * dy + dz * dz <= 20.0f * 20.0f)
			continue;
		/* (beside it, where the map is open: at its feet and its head) */
		{
			static real const offsets[][2] = { { 1.5f, 0.0f }, { -1.5f, 0.0f }, { 0.0f, 1.5f }, { 0.0f, -1.5f },
				{ 1.5f, 1.5f }, { -1.5f, -1.5f }, { 1.5f, -1.5f }, { -1.5f, 1.5f } };
			short try_index;

			for (try_index = 0; try_index < (short)NUMBEROF(offsets); try_index++)
			{
				short index = (short)((count + try_index) % (short)NUMBEROF(offsets));
				struct location feet, head;
				real_point3d above;

				position = center->object.position;
				position.x += offsets[index][0];
				position.y += offsets[index][1];
				position.z += 0.2f;
				above = position;
				above.z += 0.7f;
				scenario_location_from_point(&feet, &position);
				scenario_location_from_point(&head, &above);
				if (feet.cluster_index != NONE && head.cluster_index != NONE)
					break;
			}
			if (try_index >= (short)NUMBEROF(offsets))
				continue;
		}
		object_set_position(player->unit_index, &position, NULL, NULL);
		platform_log("network test: the host brings player %ld near the first",
			(long)(player - (struct player_datum *)player_data->data));
	}
}

static void network_test_vehicle(
	boolean enter)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE)
		return;
	if (!enter)
	{
		if (object_get(last->unit_index)->object.parent_object_index != NONE)
		{
			unit_exit_seat_end(last->unit_index);
			platform_log("network test: the last player leaves the vehicle");
		}
		return;
	}
	{
		struct object_iterator vehicles;
		long nearest_index = NONE;
		real nearest_distance = 0.0f;
		real_point3d const *position = &object_get(last->unit_index)->object.position;
		short seat_index;

		object_iterator_new(&vehicles, _object_mask_vehicle, 0);
		while (object_iterator_next(&vehicles))
		{
			real_point3d const *vehicle_position = &object_get(vehicles.index)->object.position;
			real distance = distance_squared3d(position, vehicle_position);

			if (nearest_index == NONE || distance < nearest_distance)
			{
				nearest_index = vehicles.index;
				nearest_distance = distance;
			}
		}
		if (nearest_index == NONE)
		{
			platform_log("network test: no vehicle");
			return;
		}
		for (seat_index = 0; seat_index < unit_definition_get(object_get(nearest_index)->definition_index)->unit.seats.count; seat_index++)
		{
			if (unit_seat_is_driver(nearest_index, seat_index) &&
				unit_enter_seat(last->unit_index, nearest_index, seat_index))
			{
				platform_log("network test: the last player drives vehicle %lx", nearest_index);
				network_test_hitch();
				return;
			}
		}
		platform_log("network test: the last player cannot drive vehicle %lx", nearest_index);
	}
}

/* debug.network_test_name: the name the machine's first player takes (its
profile's, which it joins with), in ASCII */
static void network_test_set_name(
	void)
{
	char const *name = config_string("debug.network_test_name");
	struct player_profile profile;
	short index;

	if (!name || !name[0])
		return;
	player_ui_get_active_player_profile(0, &profile);
	for (index = 0; name[index] && index < MAXIMUM_PLAYER_PROFILE_NAME_LENGTH - 1; index++)
		profile.player_name[index] = (wchar_t)(unsigned char)name[index];
	profile.player_name[index] = 0;
	player_ui_set_active_player_profile(0, player_ui_get_active_player_profile_index(0), &profile);
	platform_log("network test: this machine's player is named %s", name);
}

/* debug.network_test_chat: the host seats the last two players in one
vehicle, the nearest to the first of them with seats for both (any, a map's
own), for the vehicle chat (two joining machines' players, of one team: they
start near each other) */
static void network_test_seat_together(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct object_iterator vehicles;
	long units[2];
	long best_index = NONE;
	real best_distance = 0.0f;
	short count = 0;
	short index;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->unit_index == NONE)
			continue;
		units[0] = count ? units[1] : NONE;
		units[1] = player->unit_index;
		count++;
	}
	if (count < 2)
		return;
	object_iterator_new(&vehicles, _object_mask_vehicle, 0);
	while (object_iterator_next(&vehicles))
	{
		real distance;

		if (unit_definition_get(object_get(vehicles.index)->definition_index)->unit.seats.count < 2)
			continue;
		distance = distance_squared3d(&object_get(units[0])->object.position, &object_get(vehicles.index)->object.position);
		if (best_index == NONE || distance < best_distance)
		{
			best_index = vehicles.index;
			best_distance = distance;
		}
	}
	if (best_index == NONE)
	{
		platform_log("network test: no vehicle with two seats");
		return;
	}
	for (index = 0; index < (short)NUMBEROF(units); index++)
	{
		short seat_index;

		for (seat_index = 0;
			seat_index < unit_definition_get(object_get(best_index)->definition_index)->unit.seats.count; seat_index++)
		{
			if (!unit_seat_filled(best_index, seat_index) && unit_enter_seat(units[index], best_index, seat_index))
				break;
		}
	}
	platform_log("network test: the last two players ride vehicle %lx: %s, %s", best_index,
		object_get(units[0])->object.parent_object_index == best_index ? "in" : "out",
		object_get(units[1])->object.parent_object_index == best_index ? "in" : "out");
}

/* debug.network_test_chat: a line typed on the text chat's on-screen
keyboard with the controller's buttons, ticks since it began: each held
for two frames and let go for two, whatever the frame rate (a press of two
ticks falls between two frames of a machine drawing slowly) (the keyboard
opens on the first key, 'q': "gg ez", then D-pad Down to the team, then
Start) */
static void network_test_keyboard_line(
	long ticks)
{
	enum { STEP_FRAMES = 4 };
	static long frames;
	static long last_ticks = -1;
	static unsigned long const steps[] =
	{
		1UL << HALO_CHAT_PAD_UP,
		1UL << HALO_CHAT_PAD_STICK_DOWN,
		1UL << HALO_CHAT_PAD_STICK_RIGHT, 1UL << HALO_CHAT_PAD_STICK_RIGHT, 1UL << HALO_CHAT_PAD_STICK_RIGHT,
		1UL << HALO_CHAT_PAD_STICK_RIGHT,
		1UL << HALO_CHAT_PAD_A, 1UL << HALO_CHAT_PAD_A,
		1UL << HALO_CHAT_PAD_STICK_DOWN, 1UL << HALO_CHAT_PAD_STICK_DOWN,
		1UL << HALO_CHAT_PAD_A,
		1UL << HALO_CHAT_PAD_STICK_UP, 1UL << HALO_CHAT_PAD_STICK_UP, 1UL << HALO_CHAT_PAD_STICK_UP,
		1UL << HALO_CHAT_PAD_STICK_LEFT, 1UL << HALO_CHAT_PAD_STICK_LEFT,
		1UL << HALO_CHAT_PAD_A,
		1UL << HALO_CHAT_PAD_STICK_DOWN, 1UL << HALO_CHAT_PAD_STICK_DOWN,
		1UL << HALO_CHAT_PAD_STICK_LEFT, 1UL << HALO_CHAT_PAD_STICK_LEFT,
		1UL << HALO_CHAT_PAD_A,
		1UL << HALO_CHAT_PAD_DOWN,
		1UL << HALO_CHAT_PAD_START,
	};
	long step;

	/* (frames since it began: again for the next game's line) */
	if (ticks < last_ticks)
		frames = 0;
	last_ticks = ticks;
	step = frames / STEP_FRAMES;
	frames++;
	if (step >= (long)NUMBEROF(steps))
	{
		/* (after the line, let go: a held key would move nothing more) */
		if (step == (long)NUMBEROF(steps))
			test_input_press(0);
		return;
	}
	test_input_press((frames - 1) % STEP_FRAMES < STEP_FRAMES / 2 ? steps[step] : 0);
}

/* debug.network_test_chat: the last player (absolute index; NONE), whom the
host mutes; whether it is this machine's; and the first player's machine
(the network's index: the host's), which the last player's mutes */
static short network_test_last_player_index(
	void)
{
	struct data_iterator iterator;
	short last = NONE;

	data_iterator_new(&iterator, player_data);
	while (data_iterator_next(&iterator) != NULL)
		last = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
	return last;
}

static boolean network_test_last_player_local(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	return last && last->local_player_index != NONE;
}

static long network_test_first_player_machine(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	data_iterator_new(&iterator, player_data);
	player = (struct player_datum *)data_iterator_next(&iterator);
	return player ? player->network_player_data.machine_index : NONE;
}

/* debug.network_test_chat: the last player's name, in ASCII (the host
mutes it) */
static void network_test_last_player_name(
	char *name,
	short size)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	short index;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	for (index = 0; last && index < size - 1 && index < (short)NUMBEROF(last->name) && last->name[index]; index++)
		name[index] = player_name_character_ascii(last->name[index]);
	name[index] = 0;
}

/* debug.network_test_party, that many seconds in: the host invites each
player it may to its party (one made for it), unless it leads one already
(debug.party), and a client asks each player in a party to let it in, as
their scoreboard menus do; then from three seconds on (its line shown) each
second each machine accepts the invite (or lets in the ask) it has, as its
key does (network_party.c) */
static void network_test_party(
	void)
{
	long party_time = (long)(network_test.party_time * TICKS_PER_SECOND);
	struct data_iterator iterator;
	struct player_datum *player;

	if (game_time_get() >= party_time && game_time_get() - party_time < TICKS_PER_SECOND)
	{
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			unsigned long actions = network_party_player_actions(iterator.datum_index);
			char name[16];
			short index;

			for (index = 0; index < 15 && index < (short)NUMBEROF(player->name) && player->name[index]; index++)
				name[index] = player_name_character_ascii(player->name[index]);
			name[index] = 0;
			platform_log("network test: party actions for %s: %s%s%s%s", name,
				TEST_FLAG(actions, _network_party_action_invite) ? "invite " : "",
				TEST_FLAG(actions, _network_party_action_join) ? "join " : "",
				TEST_FLAG(actions, _network_party_action_accept) ? "accept " : "",
				TEST_FLAG(actions, _network_party_action_add_friend) ? "add-friend" : "");
			/* (every machine sends each player it may a friend request, as the
			scoreboard's Add friend does: port/linux/src/p2p_friends.c) */
			if (TEST_FLAG(actions, _network_party_action_add_friend))
				network_party_player_act(iterator.datum_index, _network_party_action_add_friend);
			if (network_test.mode == _network_test_host && !*config_string("debug.party") &&
				TEST_FLAG(actions, _network_party_action_invite))
			{
				network_party_player_act(iterator.datum_index, _network_party_action_invite);
			}
			if (network_test.mode != _network_test_host && TEST_FLAG(actions, _network_party_action_join))
				network_party_player_act(iterator.datum_index, _network_party_action_join);
		}
	}
	if (game_time_get() >= party_time + 3 * TICKS_PER_SECOND && network_party_accept())
		platform_log("network test: a party invite accepted");
}

/* both machines stand the last player at the driver's entrance of the
nearest vehicle no one rides (a joining machine's player then holds the
action button, as a player getting in does) */
static void network_test_vehicle_approach(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	struct object_iterator vehicles;
	long nearest_index = NONE;
	real nearest_distance = 0.0f;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE || object_get(last->unit_index)->object.parent_object_index != NONE)
		return;
	object_iterator_new(&vehicles, _object_mask_vehicle, 0);
	while (object_iterator_next(&vehicles))
	{
		struct unit_datum *vehicle = unit_get(vehicles.index);
		/* (the same vehicle on every machine: the lowest index) */
		real distance = (real)DATUM_INDEX_TO_ABSOLUTE_INDEX(vehicles.index);

		if (vehicle->unit.driver_object_index != NONE || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
			continue;
		if (nearest_index == NONE || distance < nearest_distance)
		{
			nearest_index = vehicles.index;
			nearest_distance = distance;
		}
	}
	if (nearest_index != NONE)
	{
		short seat_index;

		for (seat_index = 0; seat_index < unit_definition_get(object_get(nearest_index)->definition_index)->unit.seats.count;
			seat_index++)
		{
			real_point3d entrance;
			real_point3d seat;

			if (unit_seat_is_driver(nearest_index, seat_index) &&
				unit_get_seat_entrance_point(last->unit_index, nearest_index, seat_index, &entrance, &seat, NULL))
			{
				object_set_position(last->unit_index, &entrance, NULL, NULL);
				platform_log("network test: the last player stands at vehicle %lx's driver's entrance (%.2f %.2f %.2f)",
					nearest_index, entrance.x, entrance.y, entrance.z);
				return;
			}
		}
	}
	platform_log("network test: no vehicle to get into");
}

/* the host gives the last player a second weapon, lying about */
static void network_test_second_weapon(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	struct object_iterator weapons;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE)
		return;
	object_iterator_new(&weapons, _object_mask_weapon, 0);
	while (object_iterator_next(&weapons))
	{
		struct object_datum *weapon = object_get(weapons.index);
		struct unit_datum *unit = unit_get(last->unit_index);
		long current_index = unit->unit.weapon_object_indices[0];

		if (weapon->object.parent_object_index == NONE && TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit) &&
			(current_index == NONE || object_get(current_index)->definition_index != weapon->definition_index) &&
			unit_add_weapon_to_inventory(last->unit_index, weapons.index, TRUE))
		{
			platform_log("network test: the last player takes a second weapon (%lx)", weapon->definition_index);
			/* (and camouflage, as a powerup gives) */
			player_handle_powerup(DATUM_INDEX_NEW(last - (struct player_datum *)player_data->data, last->identifier),
				_player_powerup_active_camouflage, 10 * TICKS_PER_SECOND);
			return;
		}
	}
}

/* debug.network_test_lobby_cues: the host's console lines in its first
lobby, each at its second of the lobby's setup ("<second> <line>|..."), the
next once its second has come */
static void network_test_run_lobby_cues(
	void)
{
	char const *cues = config_string("debug.network_test_lobby_cues");
	char const *cue = cues;
	short index;

	for (index = 0; *cue && index < network_test.next_lobby_cue; index++)
	{
		cue = strchr(cue, '|');
		cue = cue ? cue + 1 : cues + strlen(cues);
	}
	if (*cue)
	{
		char *line;
		char text[256];
		double second = strtod(cue, &line);
		char const *end = strchr(cue, '|');
		int length = end ? (int)(end - line) : (int)strlen(line);

		if (network_test.setup_seconds < second)
			return;
		snprintf(text, sizeof(text), "%.*s", MAX(length, 0), line);
		platform_log("network test: lobby cue at %.1f s: %s", network_test.setup_seconds, text);
		hs_compile_and_evaluate(text);
		network_test.next_lobby_cue++;
	}
}

/* debug.network_test_cues: from the first tick every player waited for has a
unit (logged, cues or not: where a test's seconds start), each line at its
tick (the host's, or a game of one machine: a client runs none) */
static void network_test_run_cues(
	void)
{
	if (network_test.mode != _network_test_host && network_test.mode != _network_test_log)
		return;
	/* (between two ticks, as a frame of a game at 30 always is: not between
	two sub-steps of an open tick, game.sim_rate's spread over the frames,
	whose time is still the tick's: sim_rate.h) */
	if (sim_substep_next())
		return;
	if (network_test.cues_start == NONE)
	{
		struct data_iterator iterator;
		struct player_datum *player;
		long players = 0;

		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			players += player->unit_index != NONE;
		if (players < network_test.cue_players)
			return;
		network_test.cues_start = game_time_get();
		platform_log("network test: cues from tick %ld", network_test.cues_start);
	}
	while (network_test.next_cue < network_test.cue_count &&
		game_time_get() >= network_test.cues_start + network_test.cues[network_test.next_cue].tick)
	{
		platform_log("network test: cue at tick %ld: %s", game_time_get(), network_test.cues[network_test.next_cue].line);
		hs_compile_and_evaluate(network_test.cues[network_test.next_cue].line);
		network_test.next_cue++;
	}
}

/* both machines stand the last player on the first weapon lying about that
it does not carry (the same object on both: a client's own player is where
it has it, within a tolerance) */
static void network_test_pickup(
	void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	struct player_datum *last = NULL;
	struct object_iterator weapons;
	long nearest_index = NONE;
	real nearest_distance = 0.0f;
	struct unit_datum *unit;

	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		last = player;
	if (!last || last->unit_index == NONE)
		return;
	unit = unit_get(last->unit_index);
	object_iterator_new(&weapons, _object_mask_weapon, 0);
	while (object_iterator_next(&weapons))
	{
		struct object_datum *weapon = object_get(weapons.index);
		short slot;
		boolean carried = FALSE;
		real distance;

		if (weapon->object.parent_object_index != NONE ||
			!TEST_FLAG(weapon->object.flags, _object_connected_to_map_bit))
		{
			continue;
		}
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			long carried_index = unit->unit.weapon_object_indices[slot];

			carried |= carried_index != NONE && object_get(carried_index)->definition_index == weapon->definition_index;
		}
		if (network_test.pickup_weapon[0] && !strstr(tag_get_name(weapon->definition_index), network_test.pickup_weapon))
			continue;
		distance = (real)DATUM_INDEX_TO_ABSOLUTE_INDEX(weapons.index);
		if (!carried && (nearest_index == NONE || distance < nearest_distance))
		{
			nearest_index = weapons.index;
			nearest_distance = distance;
		}
	}
	if (nearest_index != NONE)
	{
		real_point3d position = object_get(nearest_index)->object.position;

		position.z += 0.1f;
		object_set_position(last->unit_index, &position, NULL, NULL);
		platform_log("network test: the last player stands on weapon %lx (%lx)", nearest_index,
			object_get(nearest_index)->definition_index);
	}
}

/* (a probe: halo --probe) the first game found, printed as JSON on the
standard output, and the end; when given_up, that none was. With an
invite (p2p.c joins the command line's), the invite's host's game alone:
the tunnel's peers are its host only, and a game on the LAN is another */
static void network_test_probe_answer(
	boolean given_up)
{
	static char const *const engines[] = { "none", "ctf", "slayer", "oddball", "king", "race" };
	struct network_advertised_game_summary game;
	long index;
	int stage;
	boolean invited = p2p_join_progress(&stage, NULL, 0) != P2P_JOIN_NONE;

	for (index = 0; index < PROBE_MAXIMUM_GAMES; index++)
	{
		char name[2 * NUMBEROF(game.name) + 1];
		char map[2 * sizeof(game.map_name) + 1];
		char const *last;
		long character, length = 0;

		if (!network_game_client_port_advertised_game_summary(index, &game) ||
			(invited && !menu_advertised_game_from_peer(index)))
		{
			continue;
		}
		/* (printable ASCII, quotes and backslashes escaped) */
		for (character = 0; character < (long)NUMBEROF(game.name) && game.name[character]; character++)
		{
			char ascii = game.name[character] >= 0x20 && game.name[character] < 0x7F ? (char)game.name[character] : '?';

			if (ascii == '"' || ascii == '\\')
				name[length++] = '\\';
			name[length++] = ascii;
		}
		name[length] = 0;
		/* (the map's name, the last part of its path) */
		last = strrchr(game.map_name, '\\');
		last = last ? last + 1 : game.map_name;
		for (length = 0, character = 0; last[character]; character++)
		{
			char ascii = (unsigned char)last[character] >= 0x20 && (unsigned char)last[character] < 0x7F ?
				last[character] : '?';

			if (ascii == '"' || ascii == '\\')
				map[length++] = '\\';
			map[length++] = ascii;
		}
		map[length] = 0;
		printf("{\"ok\":true,\"name\":\"%s\",\"map\":\"%s\",\"gametype\":\"%s\",\"players\":%d,"
			"\"max_players\":%d,\"teams\":%s,\"in_progress\":%s,\"open\":%s,\"dedicated\":%s,"
			"\"network_version\":%u,\"kind\":%u,\"tick_rate\":%d,\"openh1_version\":%u}\n", name, map,
			game.engine_type >= 0 && game.engine_type < (short)NUMBEROF(engines) ? engines[game.engine_type] : "none",
			game.player_count, game.maximum_player_count, game.has_teams ? "true" : "false",
			game.in_progress ? "true" : "false", game.open ? "true" : "false", game.dedicated ? "true" : "false",
			(unsigned)game.version, (unsigned)game.kind, (int)game.rate, (unsigned)game.openh1_version);
		fflush(stdout);
		exit(0);
	}
	if (given_up)
	{
		printf("{\"ok\":false,\"error\":\"no game answered in %d seconds%s\"}\n", PROBE_SECONDS,
			invited ? " from the invite's host" : "");
		fflush(stdout);
		exit(1);
	}
}

void network_test_update(
	boolean main_menu_loaded,
	real seconds)
{
	/* debug.bsp_switch_test (bsp_switch_test.c) */
	bsp_switch_test_update(main_menu_loaded);
	if (!network_test.checked)
		network_test_read_settings();
	if (network_test.mode == _network_test_off)
		return;

	/* debug.network_test_chat: each client's player types "gg ez" on the
	text chat's on-screen keyboard 32 seconds on, as a controller does, and
	sends it to the team (each frame, not each second, as the rest) */
	if (network_test.chat_time > 0.0f && network_test.mode != _network_test_host && game_in_progress() &&
		!main_menu_loaded &&
		game_time_get() >= (long)(network_test.chat_time * TICKS_PER_SECOND) + 32 * TICKS_PER_SECOND)
	{
		network_test_keyboard_line(game_time_get() - (long)(network_test.chat_time * TICKS_PER_SECOND) -
			32 * TICKS_PER_SECOND);
	}
	/* the game running: report (from the start of each game, or a revert to
	a checkpoint: the time starts over, and the cues with it) */
	if (game_in_progress() && game_time_get() < network_test.logged_time)
	{
		network_test.logged_time = 0;
		network_test.cues_start = NONE;
		network_test.next_cue = 0;
	}
	if (game_in_progress() && !main_menu_loaded)
		network_test_run_cues();
	if (game_in_progress() && !main_menu_loaded && game_time_get() - network_test.logged_time >= TICKS_PER_SECOND)
	{
		/* (each second's at the first frame on its first tick, so that every
		machine logs the same ticks: an OpenH1 game's client skips ticks to
		lead the host's, network_clock.c) */
		network_test.logged_time = game_time_get() - game_time_get() % TICKS_PER_SECOND;
		network_test_log_players();
		if (network_test.shoot_interval > 0.0f &&
			game_time_get() % MAX(1, (long)(network_test.shoot_interval * TICKS_PER_SECOND)) < TICKS_PER_SECOND)
		{
			network_test_shoot();
		}
		/* (the host brings the players near a second before they shoot;
		not the last while the vehicle test has it. A dedicated server's
		"log" too, which hosts with no player of its own) */
		if ((network_test.mode == _network_test_host ||
			(network_test.mode == _network_test_log && game_connection() == _game_connection_network_server)) &&
			network_test.shoot_interval > 0.0f &&
			(game_time_get() + TICKS_PER_SECOND) % MAX(1, (long)(network_test.shoot_interval * TICKS_PER_SECOND)) <
				TICKS_PER_SECOND)
		{
			long vehicle_time = (long)(network_test.vehicle_time * TICKS_PER_SECOND);

			network_test_gather(network_test.vehicle_time > 0.0f && game_time_get() >= vehicle_time - 2 * TICKS_PER_SECOND);
		}
		if (network_test.pickup_time > 0.0f)
		{
			long pickup_time = (long)(network_test.pickup_time * TICKS_PER_SECOND);

			/* (two weapons first: picking up a third swaps) */
			if (network_test.mode == _network_test_host && game_time_get() >= pickup_time - 3 * TICKS_PER_SECOND &&
				game_time_get() - (pickup_time - 3 * TICKS_PER_SECOND) < TICKS_PER_SECOND)
			{
				network_test_second_weapon();
			}

			if (game_time_get() >= pickup_time && game_time_get() - pickup_time < TICKS_PER_SECOND)
			{
				network_test_pickup();
			}
			/* (standing there for four seconds, the button held from a second
			on) */
			if (network_test.mode == _network_test_join)
			{
				boolean hold = game_time_get() >= pickup_time &&
					game_time_get() < pickup_time + 4 * TICKS_PER_SECOND;

				test_input_hold_action(hold);
			}
		}
		if (network_test.party_time > 0.0f)
			network_test_party();
		/* debug.network_test_chat: each machine's player says a line to
		everyone that many seconds in, one to the team two seconds on, and
		one to the vehicle two more on, the host having seated the last two
		players in one vehicle (the others in none) three seconds before;
		then the host mutes the last player for a minute (a client's /mute
		of the host refused), each says a line to everyone (the last
		player's going no further than the host), the host unmutes it, and
		each says another; then each says a line with a link (refused, as
		network.text_chat "on" refuses links), the host turns the text chat
		off (network.text_chat "off"), and each says a line (refused); then
		the host mutes the last player for everyone, as its scoreboard does,
		and each says a line, and the last player's machine mutes the host's
		for itself, as a client's scoreboard does, and each says another */
		if (network_test.chat_time > 0.0f)
		{
			long chat_time = (long)(network_test.chat_time * TICKS_PER_SECOND);
			char const *machine = network_test.mode == _network_test_host ? "the host" : "a client";
			char text[64];

			if (game_time_get() < chat_time - 3 * TICKS_PER_SECOND)
				network_test.chat_lines_said = 0;
			if (network_test.mode == _network_test_host && network_test.chat_lines_said == 0 &&
				game_time_get() >= chat_time - 3 * TICKS_PER_SECOND)
			{
				network_test_seat_together();
				network_test.chat_lines_said = -1;
			}
			if (network_test.chat_lines_said <= 0 && game_time_get() >= chat_time)
			{
				/* (long enough to wrap in the chat's column) */
				snprintf(text, sizeof(text), "hello everyone from %s, long enough to wrap the column", machine);
				network_chat_say(_chat_channel_everyone, text);
				network_test.chat_lines_said = 1;
			}
			if (network_test.chat_lines_said == 1 && game_time_get() >= chat_time + 2 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "hello team from %s", machine);
				network_chat_say(_chat_channel_team, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 2 && game_time_get() >= chat_time + 4 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "hello vehicle from %s", machine);
				network_chat_say(_chat_channel_vehicle, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 3 && game_time_get() >= chat_time + 6 * TICKS_PER_SECOND)
			{
				char name[16];

				network_test_last_player_name(name, sizeof(name));
				if (network_test.mode == _network_test_host)
					snprintf(text, sizeof(text), "/mute %s 1", name);
				else
					snprintf(text, sizeof(text), "/mute host");
				network_chat_say(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 4 && game_time_get() >= chat_time + 8 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "while muted from %s", machine);
				network_chat_say(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 5 && game_time_get() >= chat_time + 10 * TICKS_PER_SECOND)
			{
				char name[16];

				network_test_last_player_name(name, sizeof(name));
				if (network_test.mode == _network_test_host)
				{
					snprintf(text, sizeof(text), "/unmute %s", name);
					network_chat_say(_chat_channel_everyone, text);
				}
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 6 && game_time_get() >= chat_time + 12 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "after unmute from %s", machine);
				network_chat_say(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 7 && game_time_get() >= chat_time + 14 * TICKS_PER_SECOND)
			{
				/* (a client's sent past its own check, for the host to refuse) */
				snprintf(text, sizeof(text), "join us at discord(dot)gg/halo from %s", machine);
				if (network_test.mode == _network_test_host)
					network_chat_say(_chat_channel_everyone, text);
				else
					network_chat_say_unchecked(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 8 && game_time_get() >= chat_time + 16 * TICKS_PER_SECOND)
			{
				if (network_test.mode == _network_test_host && !config_write("network.text_chat", "off"))
					platform_log("network test: could not turn the text chat off");
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 9 && game_time_get() >= chat_time + 18 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "with the chat off from %s", machine);
				if (network_test.mode == _network_test_host)
					network_chat_say(_chat_channel_everyone, text);
				else
					network_chat_say_unchecked(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
			/* (and on again: config.toml is the executable's folder's, which
			every test of it shares) */
			if (network_test.chat_lines_said == 10 && game_time_get() >= chat_time + 20 * TICKS_PER_SECOND)
			{
				if (network_test.mode == _network_test_host && !config_write("network.text_chat", "on"))
					platform_log("network test: could not turn the text chat on");
				network_test.chat_lines_said++;
			}
			/* (the scoreboard's mutes: the host's of the last player for
			everyone, their lines and voice, then the last player's machine's
			own mute of the host's machine, here alone) */
			if (network_test.chat_lines_said == 11 && game_time_get() >= chat_time + 22 * TICKS_PER_SECOND)
			{
				if (network_test.mode == _network_test_host)
					network_chat_host_mute(network_test_last_player_index(), TRUE);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 12 && game_time_get() >= chat_time + 24 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "after the scoreboard mute from %s", machine);
				network_chat_say(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 13 && game_time_get() >= chat_time + 28 * TICKS_PER_SECOND)
			{
				if (network_test.mode == _network_test_host)
					network_chat_host_mute(network_test_last_player_index(), FALSE);
				else if (network_test_last_player_local())
					network_voice_mute_machine(network_test_first_player_machine(), TRUE);
				network_test.chat_lines_said++;
			}
			if (network_test.chat_lines_said == 14 && game_time_get() >= chat_time + 30 * TICKS_PER_SECOND)
			{
				snprintf(text, sizeof(text), "after the local mute from %s", machine);
				network_chat_say(_chat_channel_everyone, text);
				network_test.chat_lines_said++;
			}
		}
		if (network_test.mode == _network_test_host && network_test.vehicle_time > 0.0f)
		{
			long enter_time = (long)(network_test.vehicle_time * TICKS_PER_SECOND);

			if (game_time_get() >= enter_time && game_time_get() - enter_time < TICKS_PER_SECOND)
				network_test_vehicle(TRUE);
			if (game_time_get() >= enter_time + 15 * TICKS_PER_SECOND &&
				game_time_get() - enter_time - 15 * TICKS_PER_SECOND < TICKS_PER_SECOND)
			{
				network_test_vehicle(FALSE);
			}
		}
		/* ... and 20 seconds on, the last player getting in as a player does:
		at the entrance, the button held for three seconds */
		if (network_test.vehicle_time > 0.0f)
		{
			long approach_time = (long)(network_test.vehicle_time * TICKS_PER_SECOND) + 20 * TICKS_PER_SECOND;

			if (game_time_get() >= approach_time && game_time_get() - approach_time < TICKS_PER_SECOND)
				network_test_vehicle_approach();
			if (network_test.mode == _network_test_join && network_test.pickup_time <= 0.0f)
			{
				test_input_hold_action(game_time_get() >= approach_time + TICKS_PER_SECOND &&
					game_time_get() < approach_time + 4 * TICKS_PER_SECOND);
			}
		}
		/* debug.network_test_kill: the host kills the last player every so
		often, to test deaths and respawns reaching the clients */
		if (network_test.mode == _network_test_host && network_test.kill_interval > 0.0f &&
			game_time_get() % MAX(1, (long)(network_test.kill_interval * TICKS_PER_SECOND)) < TICKS_PER_SECOND)
		{
			struct data_iterator iterator;
			struct player_datum *player;
			struct player_datum *last = NULL;

			struct player_datum *first = NULL;
			long first_index = NONE;

			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				if (!first)
				{
					first = player;
					first_index = iterator.datum_index;
				}
				last = player;
			}
			if (last && last != first && last->unit_index != NONE && first->unit_index != NONE)
			{
				/* killed by the first player: a kill that scores */
				platform_log("network test: the first player kills the last");
				damage_kill_object_for_player(last->unit_index, first_index);
				/* and picks up a weapon lying about, and two grenades of each kind */
				{
					struct object_iterator objects;
					struct unit_datum *unit = unit_get(first->unit_index);
					long held_index = unit->unit.weapon_object_indices[0];
					long held_definition_index = held_index != NONE ? weapon_get(held_index)->definition_index : NONE;

					object_iterator_new(&objects, _object_mask_weapon, 0);
					while (object_iterator_next(&objects))
					{
						struct weapon_datum *weapon = weapon_get(objects.index);

						if (weapon->object.parent_object_index == NONE &&
							weapon->definition_index != held_definition_index)
						{
							if (unit_add_weapon_to_inventory(first->unit_index, objects.index, TRUE))
								platform_log("network test: the first player picks up a weapon");
							break;
						}
					}
					unit->unit.grenade_counts[0] = 2;
					unit->unit.grenade_counts[1] = 2;
				}
			}
		}
	}

	/* the next game of the list: once the scores have been shown a while,
	the host's button (the bots may press it first) */
	if (network_test.mode == _network_test_host && network_test.started && game_engine_running() &&
		!main_menu_loaded && !game_engine_can_score())
	{
		network_test.game_over = TRUE;
		if (game_engine_showing_postgame() && global_network_game_server_get())
		{
			struct server_entry next;
			boolean now;

			network_test.postgame_seconds += seconds;
			if (network_test.postgame_seconds >= 3.0f && (network_test_variant(network_test.variant_index + 1, NULL, 0) ||
				network_host_game_next(&next, &now)))
			{
				network_test.postgame_seconds = 0.0f;
				network_game_server_reset_to_pregame(global_network_game_server_get());
			}
		}
	}
	/* (a joining machine's player: the other team again in the next game's
	lobby, whose variant may have teams where the last had none) */
	if (network_test.mode == _network_test_join && network_test.team_set && game_engine_running() &&
		!main_menu_loaded && !game_engine_can_score())
	{
		network_test.game_over = TRUE;
	}
	if (network_test.mode == _network_test_join && network_test.game_over && main_menu_loaded)
	{
		network_test.game_over = FALSE;
		network_test.team_set = FALSE;
		network_test.joined_seconds = 0.0f;
	}
	/* ... back in the lobby, set up as the first was */
	if (network_test.mode == _network_test_host && network_test.game_over && main_menu_loaded)
	{
		network_test.game_over = FALSE;
		network_test.postgame_seconds = 0.0f;
		if (network_test_variant(network_test.variant_index + 1, NULL, 0))
		{
			network_test.variant_index++;
			network_test.started = FALSE;
			network_test.map_set = FALSE;
			network_test.setup_seconds = 0.0f;
			network_test.menu_seconds = 0.0f;
			/* (as picking the next game's map does: the scores' map choice
			holds the countdown) */
			if (global_network_game_server_get())
				network_game_server_pause_countdown(global_network_game_server_get(), FALSE);
			/* a join that arrived as the last game ended would refuse every
			later one, the server holding it behind the queued player */
			network_game_server_port_clear_queued_players(global_network_game_server_get());
			platform_log("network test: the next game");
		}
	}

	if (!main_menu_loaded)
		return;
	/* the intro movie skipped first, as a button press does: the movie
	holds the texture cache's spare memory, which starting the map
	takes too (texture_cache_steal_memory's assertion) */
	if (bink_playback_in_progress())
	{
		bink_playback_stop();
		network_test.menu_seconds = 0.0f;
		platform_log("network test: skipped the movie");
		return;
	}
	network_test.menu_seconds += seconds;
	/* (the main menu settling first) */
	if (network_test.menu_seconds < 2.0f)
		return;

	switch (network_test.mode)
	{
	case _network_test_browse:
	{
		struct p2p_listing listed[8], games[8];
		int listed_count, count = 0, pass, index;

		if (!network_test.browsing)
		{
			network_test.browsing = TRUE;
			p2p_lobby_browse(TRUE);
			platform_log("network test: browsing the public games");
		}
		/* (listings arriving meanwhile from every broker) */
		network_test.browse_seconds += seconds;
		listed_count = p2p_lobby_games(listed, NUMBEROF(listed));
		if (!listed_count || network_test.browse_seconds < 5.0f)
			break;
		/* those that can be joined (no password, not failed): the list has
		the most players first; co-op, the game with no game engine, last */
		for (pass = 0; pass < 2; pass++)
		{
			for (index = 0; index < listed_count; index++)
			{
				if (listed[index].invite[0] && !listed[index].failed && !listed[index].engine_type == pass)
					games[count++] = listed[index];
			}
		}
		if (!network_test.browse_listed)
		{
			network_test.browse_listed = TRUE;
			for (index = 0; index < listed_count; index++)
			{
				platform_log("network test: listed %s's game (%s, %s) %d/%d players%s%s", listed[index].name,
					listed[index].map, listed[index].gametype, listed[index].player_count,
					listed[index].maximum_player_count, listed[index].engine_type ? "" : ", co-op",
					listed[index].invite[0] ? "" : ", locked");
			}
		}
		if (count > 0 && p2p_join_listing(&games[0]))
		{
			p2p_lobby_browse(FALSE);
			platform_log("network test: joining %s's game (%s) from the server browser", games[0].name, games[0].map);
			network_test.mode = _network_test_join;
		}
		break;
	}
	case _network_test_follow:
	{
		char link[128];
		unsigned long order = 0;

		/* (the leader's internet game: its host reached by its invite, then
		joined as "join" joins) */
		if (p2p_party_follow(link, sizeof(link), &order) == _p2p_party_game_invite && p2p_join_invite(link))
		{
			p2p_party_follow_result(order, _p2p_party_follow_joined);
			platform_log("network test: following the party's leader into its game");
			network_test.mode = _network_test_join;
		}
		break;
	}
	case _network_test_host:
		if (network_test.set_up && !network_test.local && !network_test.invite_logged)
		{
			char invite[256];

			if (p2p_invite_link(invite, sizeof(invite)))
			{
				network_test.invite_logged = TRUE;
				platform_log("network test: invite %s", invite);
			}
		}
		if (!network_test.set_up)
		{
			network_test.set_up = TRUE;
			main_set_multiplayer_map_name(network_test.map_name);
			if (network_test.local)
			{
				/* as Multiplayer > Split Screen does (split_screen_game_initialize) */
				ui_widgets_close_all();
				dispose_global_network_game_server();
				dispose_global_network_game_client();
				network_game_accept_remote_connections(FALSE);
				game_engine_playlist_initialize();
				if (create_global_network_game_server())
				{
					game_engine_playlist_begin();
					game_connection_set(_game_connection_network_server);
				}
				if (!global_network_game_client_get())
					create_global_network_game_client();
				ui_widget_load_by_name_or_tag(
					pc_menus_screen("ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\pregame\\splitscreen_pregame_wrapper_normal"),
					NONE, NULL, NONE, NONE, NONE, NONE);
				platform_log("network test: a local game on %s", network_test.map_name);
			}
			else
			{
				/* debug.network_test_most_players: a game of so many (as Server
				Setup's MAX PLAYERS has it) */
				if (network_test.maximum_players > 0)
				{
					extern void network_game_server_port_set_settings(wchar_t const *name, long maximum_players);

					network_game_server_port_set_settings(NULL, network_test.maximum_players);
				}
				player_ui_fast_setup_network_server();
				platform_log("network test: hosting %s", network_test.map_name);
				/* (in the server browser, as Create Game > Internet's PUBLIC) */
				if (network_test.public_game)
					p2p_set_hosting_public(TRUE);
			}
		}
		else if (!network_test.started)
		{
			network_test.setup_seconds += seconds;
			/* the map (fast setup clears it), and a player for controller 1, as
			pressing A in the lobby adds one */
			if (!network_test.map_set && network_test.setup_seconds >= 1.0f && global_network_game_server_get())
			{
				char path[128];
				char variant_name[128];
				struct game_variant variant;

				/* (a level of its own path, else a multiplayer map's name) */
				if (strchr(network_test.map_name, '\\'))
					snprintf(path, sizeof(path), "%s", network_test.map_name);
				else
					snprintf(path, sizeof(path), "levels\\test\\%s\\%s", network_test.map_name, network_test.map_name);
				network_test_variant(network_test.variant_index, variant_name, sizeof(variant_name));
				if (!strcmp(variant_name, "coop"))
				{
					/* (the Map screen's co-op: menu_functions.c) */
					extern boolean ui_widget_port_cooperative_level_choose(char const *map_name, short difficulty);

					if (!ui_widget_port_cooperative_level_choose(path, _game_difficulty_level_normal))
						platform_log("network test: %s is not a campaign level, for co-op", path);
					platform_log("network test: game %d, co-op", network_test.variant_index + 1);
				}
				else
				{
					struct game_variant_options options;
					/* (one of OpenCE's gametypes by its name: game_modes.c; a
					gametype with a rotation entry's rules, "team_slayer+ghosts=2":
					server_gametypes.c) */
					boolean opence = game_modes_gametype_by_name(variant_name, &variant, &options);

					if (!opence && strchr(variant_name, '+'))
					{
						static struct server_entry entry;
						char text[288], reason[128];

						snprintf(text, sizeof(text), "%s:%s", network_test.map_name, variant_name);
						if (server_rotation_parse_entry(text, (int)strlen(text), &entry) && !entry.rejected[0] &&
							server_gametype_build(entry.gametype, entry.rules, entry.rule_count, &variant, &options,
								reason, (long)sizeof(reason)))
						{
							opence = TRUE;
						}
						else
							platform_log("network test: the gametype %s cannot be built: %s", variant_name, entry.rejected);
					}

					network_game_server_change_map_name(global_network_game_server_get(), path);
					/* the variant, as picking the game settings does */
					if (!opence)
						variant = *game_engine_get_variant_by_name(&variant, variant_name);
					platform_log("network test: game %d, %s", network_test.variant_index + 1, variant_name);
					/* debug.network_test_score: a short game, to test the next */
					if (network_test.score_to_win > 0)
						variant.universal_variant.score_to_win = network_test.score_to_win;
					player_ui_set_game_variant(&variant);
					if (opence)
						player_ui_set_game_variant_options(&options);
					network_game_server_change_game_variant(global_network_game_server_get(), &variant);
				}
				network_test.map_set = TRUE;
			}
			if (!network_test.player_added && network_test.setup_seconds >= 2.0f && global_network_game_client_get())
			{
				network_test_set_name();
				network_test.player_added = network_game_client_add_player(global_network_game_client_get(), 0);
				/* (a local game's others: split screen) */
				if (network_test.local)
				{
					short local_player_index;

					for (local_player_index = 1; local_player_index < network_test.local_players; local_player_index++)
						network_game_client_add_player(global_network_game_client_get(), local_player_index);
				}
			}
			/* (the host's first lobby: its cues) */
			if (network_test.map_set && !network_test.variant_index)
				network_test_run_lobby_cues();
			if (network_test.setup_seconds >= network_test.start_delay)
			{
				network_test.started = TRUE;
				network_game_client_request_immediate_start();
				platform_log("network test: starting the game");
			}
		}
		break;
	case _network_test_probe:
		if (!network_test.set_up)
		{
			network_test.set_up = TRUE;
			dispose_global_network_game_client();
			dispose_global_network_game_server();
			if (create_global_network_game_client())
				game_connection_set(_game_connection_network_client);
		}
		else
		{
			network_test.probe_seconds += seconds;
			network_test_probe_answer(network_test.probe_seconds >= PROBE_SECONDS);
		}
		break;
	case _network_test_join:
		if (!network_test.set_up)
		{
			network_test.set_up = TRUE;
			dispose_global_network_game_client();
			dispose_global_network_game_server();
			network_test_set_name();
			if (create_global_network_game_client())
			{
				game_connection_set(_game_connection_network_client);
				/* (the player joined to multiplayer first, as a player picking
				their profile: the pregame screen then asks for them every
				frame until they are in the settings) */
				player_ui_local_player_joined_multiplayer_game(0);
				platform_log("network test: searching for games");
			}
		}
		else if (!network_test.joined && network_game_client_join_first_available_game())
		{
			network_test.joined = TRUE;
			ui_widgets_close_all();
			ui_widget_load_by_name_or_tag(
				pc_menus_screen("ui\\shell\\main_menu\\multiplayer_type_select\\connected\\pregame\\connected_pregame_screen"),
				NONE, NULL, NONE, NONE, NONE, NONE);
			platform_log("network test: joining");
		}
		else if (network_test.joined && !network_test.player_added)
		{
			network_test.joined_seconds += seconds;
			if (network_test.joined_seconds >= 3.0f && global_network_game_client_get())
				network_test.player_added = TRUE;
		}
		/* (the other team from the host's player: a team game needs both) */
		else if (network_test.player_added && !network_test.team_set)
		{
			network_test.joined_seconds += seconds;
			if (network_test.joined_seconds >= 5.0f)
				network_test.team_set = network_game_client_set_team(NONE);
		}
		break;
	}
}

/* the game's time in seconds while a game runs, -1 otherwise: the scripted
player's clock (xinput_sdl.c, debug.test_input) */
double network_test_game_seconds(
	void)
{
	return game_time_initialized() && game_in_progress() ? (double)game_time_get() / TICKS_PER_SECOND : -1.0;
}

/* the tick a test's seconds count from (its cues', whether it has any):
the first every player waited for has a unit, NONE until then; 0 with no
test, a game's own start (port/linux/game/sim_trace.c) */
long network_test_cues_start_tick(
	void)
{
	if (!network_test.checked)
		network_test_read_settings();
	return network_test.mode == _network_test_off ? 0 : network_test.cues_start;
}
