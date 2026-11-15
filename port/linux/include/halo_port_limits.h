/*
HALO_PORT_LIMITS.H

Multiplayer session limits of the native builds (Windows, Linux, Android),
force-included by halo_linux_prefix.h.

The Xbox game allows 16 players on at most 4 machines (up to 4 players each
on split screen). The native builds allow 128 players on up to 128
machines; split screen stays at 4 players per machine.

128 is the largest session that fits the game's existing records: player,
machine and team indices are stored in signed chars (0..127 with NONE), and
a finishing place in 7 bits.
*/

#ifndef __HALO_PORT_LIMITS_H
#define __HALO_PORT_LIMITS_H

/* ---------- session limits */

#define HALO_PORT_MAXIMUM_NETWORK_PLAYERS 128
#define HALO_PORT_MAXIMUM_NETWORK_MACHINES 128

/* a host polls its listening socket and one socket per machine; the Xbox's
Winsock headers default to 64 (the prefix headers define FD_SETSIZE from
this before any of them is read) */
#define HALO_PORT_FD_SETSIZE 256

/* ---------- memory capacity for these limits (game state and pools) */

#include "halo_port_capacity.h"

/* ---------- struct network_game layout

The game settings record (struct network_game) is declared separately in
several networking and interface units; its layout follows from the limits.
Every copy checks its offsets against these values. The Xbox values (4
machines, 16 players) are 0x226 and 0x434. */

#define HALO_PORT_NETWORK_MACHINE_SIZE 0x44
#define HALO_PORT_NETWORK_PLAYER_SIZE 0x20
#define HALO_PORT_NETWORK_GAME_MACHINES_OFFSET 0x114
#define HALO_PORT_NETWORK_GAME_PLAYER_COUNT_OFFSET \
	(HALO_PORT_NETWORK_GAME_MACHINES_OFFSET + HALO_PORT_MAXIMUM_NETWORK_MACHINES * HALO_PORT_NETWORK_MACHINE_SIZE)
#define HALO_PORT_NETWORK_GAME_PLAYERS_OFFSET (HALO_PORT_NETWORK_GAME_PLAYER_COUNT_OFFSET + 2)
#define HALO_PORT_NETWORK_GAME_PLAYERS_END \
	(HALO_PORT_NETWORK_GAME_PLAYERS_OFFSET + HALO_PORT_MAXIMUM_NETWORK_PLAYERS * HALO_PORT_NETWORK_PLAYER_SIZE)
#define HALO_PORT_NETWORK_GAME_RANDOM_SEED_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 2)
/* (then the gametype's PC options, struct game_variant_options: 0x2C bytes) */
#define HALO_PORT_NETWORK_GAME_VARIANT_OPTIONS_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0xA)
#define HALO_PORT_NETWORK_GAME_LOCAL_DATA_OFFSET (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x36)
#define HALO_PORT_NETWORK_GAME_SIZE (HALO_PORT_NETWORK_GAME_PLAYERS_END + 0x3A + 64) /* MCC content and metadata SHA-256 */

/* ---------- system link protocol

The native builds' messages differ from the Xbox game's (longer arrays, the
game settings record in fragments), so they search for games with their own
protocol version and never see the Xbox game's, or it theirs. */

#define HALO_PORT_NETWORK_GAME_MESSAGE_VERSION 2

/* The native builds' network code has a version of its own (an unsigned
16-bit number): machines of different versions cannot play together, and a
client does not join a host of another version, but tells the player which
is newer (network_client_manager.c). A host advertises it, with its netcode,
in its game's advertisement's reserved bytes (network_server_message_handler.c),
which hosts built before there was a version send as zeros: version 0.
Raise it with any change to what the machines send each other. */
/* Version 42 integrates MCC admission/sharing and shared checkpoint, boat,
clock and scripted-pose agreement. Every map family uses the same version.
Version 43: an MCC map's damage impulses follow the damage's direction, a
vehicle on its side throws its riders, chat marks the host's lines, a co-op
cutscene's sound carries how far the host has played it, and a machine's
MCC conversion holds a round's start out of an allowance; a player standing
on a vehicle rides it (a unit's state carries the deck it stands on, the map's
riding choice has its own message: port/linux/NETCODE.md).
Version 44: MCC package offers are automatic under Map Sharing, and a joiner
may satisfy omitted base resources with its own validated local files. */
#define HALO_PORT_NETWORK_VERSION 44
/* ... the advertisement's reserved bytes: the version (a little-endian word),
then flags, then the game's kind (an OpenCE gametype's mode, and Hardcore:
port/linux/game/game_modes.h's game_modes_kind), for the server browser */
#define HALO_PORT_ADVERTISED_VERSION_OFFSET 0
#define HALO_PORT_ADVERTISED_FLAGS_OFFSET 2
#define HALO_PORT_ADVERTISED_KIND_OFFSET 3
/* ... and, a Standard game's (port/linux/game/network_standard.c), its rate
in Hz (a Legacy game's 0, as before) */
#define HALO_PORT_ADVERTISED_RATE_OFFSET 4
/* ... and, a Standard game that OpenCE players may join (a mixed game, which
advertises HALO_PORT_NETWORK_VERSION, as a Legacy game does), the OpenH1
protocol version (a Legacy game's 0). OpenCE reads no reserved byte past the
game's kind */
#define HALO_PORT_ADVERTISED_OPENH1_VERSION_OFFSET 5
/* ... and the bots the host plays in the game (port/linux/game/bots.c),
which its player count leaves out (0 for none, as before) */
#define HALO_PORT_ADVERTISED_BOTS_OFFSET 6

/* OpenH1's Standard games (port/linux/game/network_standard.c, NETCODE.md's
"Versions") have a protocol version of their own, separate from
HALO_PORT_NETWORK_VERSION, which stays the Legacy games' (OpenCE's). A
Standard host advertises it, with HALO_PORT_STANDARD_VERSION_BIT, where a
Legacy host advertises the network version (its game's advertisement and
its internet listing), so a build that knows nothing of Standard games
takes it for a newer version and does not join. Raise it with any change to
what a Standard game's machines send each other, and with every raise of
HALO_PORT_NETWORK_VERSION: HALO_PORT_STANDARD_BASE_VERSION is the network
version it was last raised for, and the build fails until they agree. */
#define HALO_PORT_STANDARD_NETWORK_VERSION 31
/* (1: the rate agreed; 2: the players' sub-steps sent, network_substeps.c;
3: OpenCE players in the game, network_standard.c's mixed games; 4: a
player moved to the other team by the host, network_host_controls.c; 5:
network version 39, a dedicated server's flag; 6: a client's input sent on
by the host as it comes, network_distributed.c's forwarded input; 7: each
sub-step's controls as it runs, network_substeps.c's live controls; 8: a
client's input sent as its tick's first sub-step ends, and taken with its
prediction as the host's tick begins, network_distributed.c's
distributed_prediction_at_tick_start; 9: every client player's state to
every OpenH1 client with the client's tick the host has them at,
network_distributed.c's distributed_correct_remote_unit; 10: the variant's
flags of a game under way the host changed, network_host_rules.c; 11:
network version 41, OpenCE's votes, map vote and join request with a
Discord pass; 12: the host's word to each client on how long before its
tick its inputs came, and a client's clock led by it, network_clock.c; 13:
every player's presses as the host's sub-steps ran them sent on to every
OpenH1 client, not only to those near, network_distributed.c's
distributed_far_live; 14: the states of the players near a client, or aimed
at, as each of the host's sub-steps ends, network_distributed.c's
_distributed_message_substep_states; 15: a dedicated server's admins in the
game, their player keys proven and their Host Controls asked of the server,
network_admin.c; 16: relays for machines whose NATs keep them apart,
p2p_relay.c: the relays' packets on the tunnel's socket, the signalling's
relay offers, a dedicated server's relays told its players; 17: the bots'
machines the session's last 64 (port/linux/game/bots.h's BOTS_FIRST_MACHINE,
113 before), by which every OpenH1 machine tells a bot's player; 18: the
host's way of picking up a second weapon in the game record
(_network_game_hold_to_pick_up_bit, network_standard.c's
network_standard_walk_over_pickup); 19: whether the host's players turn
with the aim in a jump and a throw (game.enhanced_animations) in the game
record (_network_game_classic_turning_bit, network_standard.c's
network_standard_turns_with_aim); 20: the players' pings, network_markers.c's
_distributed_message_marker_up and _down; 21: the rest of the animation
enhancements (a seat's reload borrowed, the hands off a vehicle's grips and
the pose in a throw) the host's word on every machine too, as the turning
is, network_standard.c's network_standard_enhanced_animations; 22: a
sub-step's state written as its difference from the tick's state the
client was last sent of the player, network_distributed.c's
distributed_substep_state_write; 23: the tick's states of the players
as their differences from states the client says it took, on a grid of
1/4096 of a world unit, network_distributed.c's
_distributed_message_unit_state_differences and the input's word on them;
24: the actors' states without the parts at their defaults,
network_actors.c's _distributed_message_actor_states_compact; 25: the
host's word that its players' views are drawn as with display.direct_camera
off in the game record (_network_game_no_direct_camera_bit, a 2V2 Classic
game's: network_standard.c's network_standard_direct_camera_allowed); 26:
the host's word that its game is a Pro 2v2 game in the game record
(_network_game_pro_rules_bit: network_standard_pro_game; its players' dynamic
lights classic, their field of view at most 110, no teammates' feeds), and a
client's mods each marked with what they replace (sounds, bitmaps, models,
text: distributed_client_build's mods), which a Pro host refuses a machine
for; 27: the Pro 2v2 Classic playlist (7) at 120 ticks, as Crispy (6, Low
Latency before) is, where it was at 30: the listing's playlist number and
rate a search requires of a game (matchmaking.c's matchmaking_listing_of),
which every machine must take alike, so a build of 26, whose Classic search
and browser want 30, never sees a game of 27's Classic, and the other way
round; 28: a loose item struck where it already was lifted 0.05 off
the surface (items.c's item_move_substep, network_standard.c's
network_standard_simulation_fixes), where it stayed pinned on a wall
steeper than 45 degrees: every machine of the game moves its copies alike;
29: a co-op player the host brought to its own player, or went to, told on
their machine, the brought player turned the host's way
(network_host_controls.c's _distributed_message_host_teleport); 30:
whether the host forbids its players' hit sounds in the game record
(_network_game_no_hit_sounds_bit, network_standard.c's
network_standard_hit_sounds_allowed); 31: network version 44, OpenCE's
(the MCC maps' admission, package sharing, checkpoint, game speed and
vehicle riding messages, _distributed_message_checkpoint to
_distributed_message_vehicle_riding_map, 96 to 107; the join request's
shared agreement; the game record's MCC content and metadata hashes), and the Standard
game's switches (hold to pick up, classic turning, no direct camera, Pro
rules, no hit sounds) moved from bits 3 to 7 of the record's
cooperative_flags, whose bit 3 OpenCE took for vehicle riding, to bits 0 to
4 of standard_flags, the high byte of what was the short standard_rate
(source/networking/network_game_manager.h); the chat line's lobby bit
moved from 3 to 4 and the chat settings' lobby bit from 2 to 3, after
OpenCE's host bits (network_chat.c); a unit state's fine velocity moved
from part bit 6, OpenCE's scripted pose's, to bit 7 of its unit flags
(network_legacy.h).
Legacy games, and what an OpenCE machine sees, are unchanged) */
#define HALO_PORT_STANDARD_BASE_VERSION 44
#define HALO_PORT_STANDARD_VERSION_BIT 0x8000
#define HALO_PORT_STANDARD_ADVERTISED_VERSION (HALO_PORT_STANDARD_VERSION_BIT | HALO_PORT_STANDARD_NETWORK_VERSION)
#if HALO_PORT_STANDARD_BASE_VERSION != HALO_PORT_NETWORK_VERSION
#error "HALO_PORT_NETWORK_VERSION was raised: raise HALO_PORT_STANDARD_NETWORK_VERSION and its base too"
#endif
/* what an OpenCE build that knows of OpenH1's games tells its player
joining one, with the game's rate (the advertisement's
HALO_PORT_ADVERTISED_RATE_OFFSET): NETCODE.md, "OpenH1 and Legacy games".
Players see Standard games as "OpenH1 <rate> Tick" */
#define HALO_PORT_STANDARD_REFUSAL "This is a GOAT H1 %u Tick game; join a Legacy game or get GOAT H1"
/* ... the host plays the distributed netcode (always, since the lockstep
netcode was removed; hosts of version 4 built before then may not) */
#define HALO_PORT_ADVERTISED_DISTRIBUTED_FLAG 0x01
/* ... the game is under way (loading, playing or over), not in its lobby:
the menus show it before joining it (hosts built before then never set it) */
#define HALO_PORT_ADVERTISED_IN_PROGRESS_FLAG 0x02
/* ... the host is a dedicated server, with no player of its own (halo
--server: port/linux/src/server_mode.c): the menus say so (from version 39) */
#define HALO_PORT_ADVERTISED_DEDICATED_FLAG 0x04

/* a message header's 12-bit length allows messages of up to 0xFFF bytes,
header included; the per-tick update of 128 players is 3,857 */
#define HALO_PORT_MAXIMUM_NETWORK_MESSAGE_SIZE 0x1000

/* the packet codec's limit on a decoded or encoded packet; the per-tick
update of 128 players decodes to 0x1010 bytes */
#define HALO_PORT_NETWORK_PACKET_SIZE 0x1100

/* the game settings record (HALO_PORT_NETWORK_GAME_SIZE, 13,200 bytes at 128
machines and players) does not fit one message; it is sent in pieces of
this many bytes (4 pieces), each 3,594 bytes on the wire */
#define HALO_PORT_NETWORK_GAME_SETTINGS_FRAGMENT_SIZE 0xE00

#endif /* __HALO_PORT_LIMITS_H */
