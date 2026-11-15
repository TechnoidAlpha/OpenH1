/*
NETWORK_GAME_MANAGER.H

header included in hcex build.
*/

#ifndef __NETWORK_GAME_MANAGER_H
#define __NETWORK_GAME_MANAGER_H
#pragma once

/* ---------- headers */

#include "game/game_engine.h"
#include "game/players.h"

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct network_game;
struct network_machine;
struct network_player;

struct network_machine
{
	wchar_t name[32];
	char machine_index;
	byte __padding41[3];
};

struct network_game_map
{
	long version;
	char name[0x80];
};

struct network_game_local_data
{
	boolean game_objects_loaded;
	byte __padding431[3];
};

/* port: network_game's cooperative_flags */
enum
{
	/* the players pass through each other (Server Setup's PLAYER COLLISIONS:
	OFF, co-op's): network_coop_player_collisions */
	_network_game_cooperative_no_player_collisions_bit = 0,
	/* the players do their AI allies no harm (Server Setup's ALLY FRIENDLY
	FIRE: OFF, co-op's): network_coop_ally_friendly_fire */
	_network_game_cooperative_no_ally_friendly_fire_bit = 1,
	/* the host is a dedicated server: its loading zones ask for most of the
	players, not the host's (network_coop_host_dedicated) */
	_network_game_cooperative_dedicated_bit = 2,
	/* port: players standing on a vehicle are carried by it: the host's
	decision for the running game, made as it starts (network_coop.c's
	network_game_port_vehicle_riding_start) */
	_network_game_cooperative_vehicle_riding_bit = 3,
};

/* port: network_game's standard_flags: an OpenH1 Standard game's switches
(port/linux/game/network_standard.c). Only a Standard host sets them, and
its OpenH1 machines alone read them; a mixed game's record for an OpenCE
machine has them cleared (network_standard_game_for_machine). They were
bits 3 to 7 of cooperative_flags until OpenCE's network version 44 took bit
3 for vehicle riding: OpenCE owns cooperative_flags, OpenH1 this byte */
enum
{
	/* an OpenH1 game's players pick up a second weapon by
	holding the action button, as on the Xbox, not by walking over it
	(game.auto_pickup false: network_standard_walk_over_pickup). Only a
	Standard host sets it, and its OpenH1 machines alone read it */
	_network_game_hold_to_pick_up_bit = 0,
	/* an OpenH1 game's players keep the original's facing in a
	jump and a grenade throw (the host's game.enhanced_animations false:
	network_standard_turns_with_aim). Only a Standard host sets it, and its
	OpenH1 machines alone read it */
	_network_game_classic_turning_bit = 1,
	/* every OpenH1 machine draws its players' views as with
	display.direct_camera off, whatever its own setting (a 2V2 Classic
	game's: network_standard_direct_camera_allowed). Only a Standard host
	sets it, and its OpenH1 machines alone read it */
	_network_game_no_direct_camera_bit = 2,
	/* the game is a Pro 2v2 game (either kind), whose rules
	every OpenH1 machine plays whatever its own settings: display.dynamic_lights
	drawn "classic", the field of view at most 110 degrees, no teammates'
	feeds (network_standard_pro_game). Only a Standard host sets it, and its
	OpenH1 machines alone read it */
	_network_game_pro_rules_bit = 3,
	/* an OpenH1 game's host forbids its players' hit sounds
	(network.allow_hit_sounds false: network_standard_hit_sounds_allowed).
	Only a Standard host sets it, and its OpenH1 machines alone read it */
	_network_game_no_hit_sounds_bit = 4,
	/* an OpenH1 game's players hijack vehicles (the host's
	game.vehicle_hijack, or Host Controls' since:
	network_standard_vehicle_hijack). Only a Standard host sets it, and its
	OpenH1 machines alone read it */
	_network_game_vehicle_hijack_bit = 5,
};

struct network_game
{
	wchar_t name[16];
	struct network_game_map map;
	struct game_variant variant;
	/* port: the game's switches (_network_game_cooperative_*_bit), in a byte the
	Xbox's game left as padding, which builds before them send as 0 */
	byte cooperative_flags;
	char minimum_players;
	/* port: 128 does not fit a signed char */
	byte maximum_players;
	byte maximum_teams;
	short difficulty;
	short machine_count;
	/* port: the native builds' session limits, 128 machines and players
	(port/linux/include/halo_port_limits.h) */
	struct network_machine machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
	short player_count;
	struct network_player players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
	/* port: a Standard game's rate in Hz (port/linux/game/network_standard.c),
	and its switches (_network_game_hold_to_pick_up_bit and after), in a
	short the Xbox game left unused, which a Legacy host sends as it always
	has (zero: OpenCE never sets it) */
	byte standard_rate;
	byte standard_flags;
	unsigned long random_seed;
	long number_of_games_played;
	/* port: the gametype's PC options (game_engine.h) */
	struct game_variant_options variant_options;
	struct network_game_local_data local_data;
	byte mcc_content_sha256[32]; /* port: MCC-only content identity; zero for Xbox/CE */
	byte mcc_metadata_sha256[32]; /* port: required MCC package metadata; optional art is separate */
};

/* ---------- prototypes/EXAMPLE.C */

void network_game_invalidate_player(
	struct network_player *player);
void network_game_end_and_load_ui(
	struct network_game *game);
void network_game_assign_players_to_team(
	struct network_game *game,
	char const *prefix);
boolean network_player_is_valid(
	struct network_player const *player);
boolean network_game_add_machine(
	struct network_game *game,
	struct network_machine *machine);
boolean network_game_update_machine(
	struct network_game *game,
	struct network_machine *machine);
void xbox_set_machine_name(
	char const *machine_name);
void network_game_generate_local_machine_name(
	wchar_t *machine_name);
boolean network_game_spawn_player(
	struct network_player *player);
boolean network_game_player_is_valid(
	struct network_player *player,
	struct network_game *game);
void network_game_reset_for_next_round(
	struct network_game *game,
	boolean unload_game_objects);
boolean network_game_add_player(
	struct network_game *game,
	struct network_player *player);
boolean network_game_has_free_player_slot(
	struct network_game *game);
void network_game_invalidate_machine(
	struct network_game *game,
	word machine_index);
void network_game_invalidate(
	struct network_game *game);
boolean network_game_update_player(
	struct network_game *game,
	struct network_player *player);
boolean network_game_remove_player(
	struct network_game *game,
	struct network_player *player);
boolean network_game_remove_machine(
	struct network_game *game,
	struct network_machine *machine);
boolean network_game_create_game_objects(
	struct network_game *game);

/* ---------- globals */

/* ---------- public code */

#endif // __NETWORK_GAME_MANAGER_H
