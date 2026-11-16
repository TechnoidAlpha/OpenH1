/*
NETWORK_PARTY.H

Parties made in a network game (network_party.c): what the main loop, the
HUD (hud.c), the scoreboard (game_engine.c) and the netcode ask of it.
*/

#ifndef __NETWORK_PARTY_H
#define __NETWORK_PARTY_H
#pragma once

/* ---------- constants */

/* what this machine may do about another's player (a bit each) */
enum
{
	/* invite them to this machine's party (one made for it if none) */
	_network_party_action_invite,
	/* ask to join theirs (they are in one) */
	_network_party_action_join,
	/* accept their invite, or let them in (they asked) */
	_network_party_action_accept,
	/* send them a friend request (an OpenH1 player who told its friend code,
	not a friend yet: port/linux/src/p2p_friends.c) */
	_network_party_action_add_friend,
	NUMBER_OF_NETWORK_PARTY_ACTIONS
};

/* ---------- prototypes/NETWORK_PARTY.C */

/* every frame (main.c): this machine's party told the host, the host's
list of who can party sent on, and the accept key */
void network_party_update(void);
/* the actions offered for a player (a datum index) of another machine: a
bit each (_network_party_action_*); none for an OpenCE machine's player, a
bot, a machine that has not said it can party, or outside an OpenH1 game.
The scoreboard's player menu shows them (the lobby's and the carnage
report's could) */
unsigned long network_party_player_actions(long player_index);
/* ... and what one says (at most size characters with its end) */
void network_party_action_label(long player_index, short action, wchar_t *label, short size);
/* ... done */
void network_party_player_act(long player_index, short action);
/* a player of the game's record (its slot: the lobby's, which has no datum
yet) and its friend code, as the host told it: what it is to this profile
(p2p.h's _p2p_friend_*: _p2p_friend_self for this machine's own), NONE if
its code is not known (an OpenCE machine's, a bot, a machine of an older
build, outside an OpenH1 game). A player card's ADD FRIEND (lobby_menu.inc) */
int network_party_slot_friend(short slot);
/* ... a friend request sent it (name: the game's), or its request accepted:
FALSE if it cannot be (no code, a friend already, asked, blocked; this
machine without a profile) */
boolean network_party_slot_add_friend(short slot, wchar_t const *name);
/* the newest invite (or ask) accepted: as its key does */
boolean network_party_accept(void);
/* (the HUD of the first local player's view) the invite to accept, with
its key, or what was done; and a Discord friend's ask to join */
void network_party_draw(short local_player_index);
/* a Discord friend's ask to join, with its keys, if there is one: centered
in window at y, in the font (the HUD's in the game; over the menus,
ui_widget.c's, theirs) */
void network_party_discord_draw(rectangle2d const *window, long font_index, short y);

/* room held for a party joining the game, while its members follow their
leader in (the host's server: network_server_message_handler.c,
network_server_manager.c). Addresses are as the game keeps them (host
order): a machine's of the internet is its tunnel's stand-in. Whether the
machine at this address may take one of the game's player slots (its
people's, the bots apart: network_game_server_port_bots_humans): the slots
held are their members' alone */
boolean network_party_slot_open(unsigned long address);
/* ... a player of the machine (its index in the game, at that address)
added: its hold taken up, the machine one of its party's from now on */
void network_party_slot_taken(long machine_index, unsigned long address);
/* ... whether two machines are of one party: the machine (machine_index:
NONE, or one not in the game yet) at address, and another in the game */
boolean network_party_machines_together(long machine_index, unsigned long address, long other_machine_index);
/* ... whether a machine is in a party (the lobby's team balance moves
another's player first) */
boolean network_party_machine_in_party(long machine_index);
/* ... the slots held, not taken up yet (the advertisement and the server
browser's listing count them as players') */
short network_party_held_slots(void);
/* ... the team (0, 1) for a player of a party joining a team game: its
party's (party_counts: its players on each team), if that team has room for
it (team_counts: each team's players; maximum: the game's most); NONE: none */
short network_party_team_choice(short const *party_counts, short const *team_counts, short maximum);

/* (network_distributed.c) the messages, in the lobby too; from_stream: it
came over the sender's own stream (anything else is refused) */
boolean network_party_handles_message(word const *message, word size);
void network_party_handle_message(long machine_index, word const *message, word size, boolean from_stream);

#endif
