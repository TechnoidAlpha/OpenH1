/*
NETWORK_STANDARD.H

OpenH1's Standard games and OpenCE's Legacy ones (network_standard.c).
*/

#ifndef NETWORK_STANDARD_H
#define NETWORK_STANDARD_H

struct network_game;

/* ---------- constants */

/* the token a machine joined a host with (network_standard_join_kind) */
enum
{
	/* a Legacy game's (whatever the machine is: a Legacy host cannot tell) */
	_network_standard_join_legacy = 0,
	/* a Standard game's, an OpenH1 machine's */
	_network_standard_join_openh1,
	/* a mixed game's Legacy token: an OpenCE machine's (or an OpenH1 one
	that joined as OpenCE does) */
	_network_standard_join_opence,
};

/* ---------- prototypes */

/* hosting: the server made reads network.game_mode, which holds while it
hosts: whether its games are Standard */
void network_standard_server_created(void);
/* ... and again (Server Setup's GAME, chosen after the server was made),
while no other machine has joined: a game's kind holds once one has */
void network_standard_server_mode_read(void);
boolean network_standard_hosting(void);
/* the game this machine hosts, as a dedicated server's status tells it: its
OpenH1 game's rate (as last read in the lobby, then the game's), 0 for a
Legacy game; and whether OpenCE players join it now (a Legacy game, or a
mixed one). A dedicated server that hosts nothing yet: what its
server.game_mode says it will host */
long network_standard_server_hosted(boolean *opence);
/* the rate of the Standard game hosted: network.sim_rate as read in the
lobby (in_lobby, before the game's start), the same from the start to the
game's end. 0 hosting a Legacy game */
long network_standard_server_rate(boolean in_lobby);
/* the host's game record, as it goes out: a Standard game's rate in it
(nothing of a Legacy game's changes) */
void network_standard_stamp_game(struct network_game *game, boolean in_lobby);
/* whether a multiplayer player (a datum index) picks up a second weapon by
walking over it, as Custom Edition does (game.auto_pickup), or holds the
action button, as on the Xbox (players.c's unit_should_autopick_weapon): a
game of this machine's own, its setting; an OpenH1 game, its host's as read
in the lobby, which the record carries to its OpenH1 clients
(_network_game_hold_to_pick_up_bit); a Legacy game, and an OpenCE machine's
player, OpenCE's way, walking over */
boolean network_standard_walk_over_pickup(long player_index);

/* joining: whether the game joined is Standard (its advertisement's), set
as the join begins, and its join token made a Standard game's */
void network_standard_client_joining(boolean standard, byte *join_token, long token_size);

/* a join token as a Standard game's (of this protocol's version): the host
of one takes only that, so a machine that is not of it is refused */
void network_standard_join_token(byte *join_token, long token_size);

/* the rate the network game played agreed on (sim_rate.c's
sim_rate_network): the Standard game's, its host's or the one its record
brought, or 0 for a Legacy game (or none) */
long network_standard_agreed_rate(void);

/* Mixed games: a Standard game that OpenCE players join (network.allow_opence,
read with network.game_mode) */
boolean network_standard_hosting_mixed(void);
/* ... and whether it takes OpenCE's joins now (Host Controls' Allow OpenCE
players: those in it stay); an OpenH1 game made OpenCE's to join from now on,
or no more */
boolean network_standard_opence_joins_open(void);
void network_standard_set_opence_joins(boolean allowed);
/* the host's advertisement's reserved bytes: its version (a mixed game's the
Legacy game's), and a Standard game's rate and, a mixed game's, the OpenH1
protocol version */
void network_standard_advertise(byte *reserved, boolean in_lobby);
/* the host: the token a join request carries (_network_standard_join_*),
against the game's own (network_game_generate_join_game_token), or NONE
when this host takes no join with it */
short network_standard_join_kind(byte const *request_token, byte const *game_token, long token_size);
/* ... the machine accepted with it, at its index in the game */
void network_standard_machine_joined(long machine_index, short kind);
/* the host: whether the machine (its index in the game) joined this OpenH1
game as an OpenCE machine (its Legacy token, in a mixed game), or as an
OpenH1 one (the Standard token). A Legacy host, and any client, knows
neither */
boolean network_standard_machine_is_opence(long machine_index);
boolean network_standard_machine_is_openh1(long machine_index);
/* the host: whether a distributed message of the type may go to the machine:
none of OpenH1's own (FIRST_OPENH1_DISTRIBUTED_MESSAGE and after) goes to an
OpenCE machine */
boolean network_standard_message_goes_to(long machine_index, byte type);
/* the host: its game record as it goes to a machine: to an OpenCE one, a copy
without the Standard game's rate, as a Legacy host has it */
struct network_game const *network_standard_game_for_machine(struct network_game const *game, boolean opence);
/* every OpenH1 machine of a mixed game: whether the player (a datum index)
plays on an OpenCE machine, and so at 30 (objects.c's lanes): the host's
from its machines, a client's from its host
(_distributed_message_opence_players) */
boolean network_standard_player_is_opence(long player_index);
/* a new game (network_distributed.c), each tick of the host (it tells its
OpenH1 clients which players are OpenCE's) and the message it sends them */
void network_standard_new_game(void);
void network_standard_host_tick(void);
boolean network_standard_handles_message(word const *message, word size);
void network_standard_handle_message(long machine_index, word const *message, word size);
/* a client: a distributed message heard; and, if it joined as OpenCE does
(a Legacy join), how many of OpenH1's own it heard and the rate a record
brought it, both 0 from a mixed game's host (the network test logs them) */
void network_standard_message_heard(byte type);
void network_standard_legacy_heard(long *openh1_messages, long *record_rate);

#endif
