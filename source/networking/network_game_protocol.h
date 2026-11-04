/*
NETWORK_GAME_PROTOCOL.H

Shared game-protocol constants for the connection and message consumers.
*/

#ifndef __NETWORK_GAME_PROTOCOL_H
#define __NETWORK_GAME_PROTOCOL_H
#pragma once

/* ---------- constants */

/* port: the game's ports are network.port's (network_game_globals.c), the
Xbox's 5150 (0x141E) and 5151 by default: the host's, and the next its
clients' (a client's game searches and pings name it). Another lets copies
of the game host side by side on one computer (tests, dedicated setups);
a LAN game on another port is found only by machines set to it */
enum
{
	NETWORK_GAME_DEFAULT_SERVER_PORT = 0x141E,
};

unsigned short network_game_server_port(void);
unsigned short network_game_client_port(void);

#define NETWORK_GAME_SERVER_PORT network_game_server_port()
#define NETWORK_GAME_CLIENT_PORT network_game_client_port()

enum
{
	_rejection_code_version_too_old = 0,
	_rejection_code_version_too_new,
	_rejection_code_bad_join_token,
	_rejection_code_bad_password,
	_rejection_code_game_is_full,
	_rejection_code_game_is_closed,
	_rejection_code_blacklisted_machine,
	/* port: a player's name a dedicated server's word filter keeps out
	(server.chat_filter "block"); a build before it says the game is closed */
	_rejection_code_name_refused,
	/* port: a dedicated server's VPN policy (server.vpn_policy,
	port/linux/game/network_link.c): a machine from a listed address shows
	no Discord pass (an updated game joins again at once with one); the
	server lets no VPN in; the pass shown does not check; its account is
	younger than the server takes; its account is in the game already. A
	build before them says the game is closed */
	_rejection_code_vpn_link_required,
	_rejection_code_vpn_blocked,
	_rejection_code_vpn_link_invalid,
	_rejection_code_vpn_link_too_new,
	_rejection_code_vpn_link_in_use,
	/* port: a dedicated server's limit of machines from one address
	(server.max_machines_per_address) */
	_rejection_code_address_full,
	NUMBER_OF_SERVER_REJECTION_CODES,
};

#endif // __NETWORK_GAME_PROTOCOL_H
