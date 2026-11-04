/*
P2P.H

Internet play (p2p.c): machines that shared an invite reach each other's
system link games as if they were on one LAN. xnet.c routes the game's
traffic for them through here; sdl_platform.c passes invite links in and
out through the clipboard.

Addresses and ports are in network byte order.
*/

#ifndef __HALO_LINUX_P2P_H
#define __HALO_LINUX_P2P_H

/* the scheme of the game's links (openh1://join/..., an invite, and
openh1://party/..., a party's), the one the game registers with the system
(p2p.c, tools/macos_app.py, tools/ios_app.py, the Android manifest) */
#define P2P_LINK_SCHEME "openh1"
/* an invite link: this, then its host's key hash and its token in
hexadecimal (p2p.c) */
#define P2P_JOIN_LINK_PREFIX "openh1://join/"
/* the scheme older builds' links have, OpenCE's: links of it are still
taken (shared before, or handed over by an older copy), but the game makes
none and registers it nowhere, and on the desktop drops a registration of
it that points at this executable (p2p.c) */
#define P2P_OLD_LINK_SCHEME "halo"
#define P2P_OLD_JOIN_LINK_PREFIX "halo://join/"

/* starts internet play, if network.online is set, when the game starts
its networking; local_address is the address the game's sockets are
reached at (network.address, else 127.0.0.1) */
void p2p_initialize(unsigned long local_address);

/* on the desktop, before anything else: if this process was started with
an invite link (openh1://join/...) and another copy of the game is running,
passes the link to it and returns nonzero (this one should quit) */
int p2p_hand_off_invite(void);

/* joins the game an invite link or code leads to; text may hold other
words around it. Returns nonzero if it held an invite */
int p2p_join_invite(const char *text);

/* this machine's identifier, which its XNADDR carries (6 bytes) */
const unsigned char *p2p_identifier(void);
/* the address the game reaches the machine with this identifier at, if it
is an internet play peer (reached yet or not) */
int p2p_peer_address(const unsigned char *identifier, unsigned long *address);
/* the ping to the machine with this identifier, in milliseconds, if the
tunnel reaches it; -1 if not, or not timed yet */
int p2p_peer_ping(const unsigned char *identifier);

/* a destination the game sends to or connects to (stream: a TCP socket;
socket: the game's UDP socket connected to it, else -1): 1 if it is a peer's
address, rewritten to the local address standing in for it; -1 if it is a
peer's (or was) but the peer cannot be reached now (the game's traffic must
not go to the address itself); 0 if it is not a peer's */
int p2p_outgoing(int stream, int socket, unsigned long *address, unsigned short *port);
/* a source the game received from, accepted from or is connected to: if
it is one standing in for a peer, the peer's address */
int p2p_incoming(int stream, unsigned long *address, unsigned short *port);
/* the local addresses standing in for this port (a broadcast's) on every
peer; returns their count */
int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum_count);
/* a datagram the game sends from a socket with the local port source_port
(bound: not 0) to address and port, sealed onto the tunnel at once (no
stand-in carries it; one is made for the answers when they come): 1 if it
is a peer's address (sent, or lost as a datagram may be); -1 if it is a
peer's (or was) but the peer cannot be reached now; 0 if it is not a
peer's */
int p2p_send_datagram(unsigned short source_port, unsigned long address, unsigned short port, const void *data,
	int size);
/* the same for a broadcast to port: to every peer the tunnel reaches;
returns their count */
int p2p_broadcast_datagram(unsigned short source_port, unsigned short port, const void *data, int size);

/* the game's socket has this local port: bound, given one, or listening
(stream and listening: it is hosting). Peers reach only these ports (a
stream's only while it listens), and datagram ports it sent them from */
void p2p_socket_port(int socket, int stream, int listening, unsigned short port);
/* a socket that peers are not to reach (bound to 127.0.0.1 alone) has this
local port: traffic from it is not a peer's whose stand-in had the port */
void p2p_port_taken(int stream, unsigned short port);
/* the game closes a socket, a datagram one of this local port (0: none, or
a stream's) */
void p2p_socket_closed(int socket, unsigned short datagram_port);

/* text for the clipboard (a new invite link), once; NULL if none. Called
from the main thread */
const char *p2p_take_clipboard_text(void);

/* whether a game this machine hosts may be joined from the internet (an
invite, Discord): the PC menus' Create Game > Internet (the default), not
> LAN */
void p2p_set_hosting_allowed(int allowed);
/* the invite link of the game this machine hosts (empty, 0: none, or LAN
only) */
int p2p_invite_link(char *link, int size);
/* the invite link that reaches the game whose host has this address
(network byte order; 0: this machine, hosting): its own invite (as
p2p_invite_link), or the invite this machine joined with, if the tunnel
reaches its host at that address; 0 if none (a LAN game) */
int p2p_game_invite(unsigned long host_address, char *link, int size);
/* an invite (a link or a code, or any text holding one) as the log shows
it: its first digits only, since players post their logs and an invite lets
anyone join; the dedicated server's log, its operator's, has it whole */
const char *p2p_invite_log_text(const char *invite, char *text, int size);

/* the join page (website/public/join): a link's token in its address's
part after "#" (#party=, #join=), which browsers never send, opened as the
link it is (openh1://), else the downloads and the link to paste */
#define P2P_JOIN_PAGE "https://openh1.com/join/"
enum
{
	/* a message of p2p_invite_message's, with its end */
	P2P_INVITE_MESSAGE_SIZE = 320,
};
/* what COPY INVITE puts on the clipboard for the party or game link within
text: a line with the join page's address of it, which chat apps (Steam's,
Discord's) make clickable as they do not openh1://, and a line with the link
itself, for JOIN BY LINK (every form of either is taken: p2p_join_invite).
0 if text holds no link */
int p2p_invite_message(const char *text, char *message, int size);

/* the hosted game's players and the most it takes, which Discord shows
(0, 0: not hosting; until the game says, the machines the tunnel reaches
are shown). The game's server calls it as they change (calling it with the
same counts again costs little) */
void p2p_set_game_player_counts(int count, int maximum);

/* ---- the server browser's public games (p2p_lobby.c) */

/* whether the game this machine hosts for the internet (not LAN) is listed
in everyone's server browser: Create Game's PUBLIC; a private game is
reached only by its invite. Going private makes a new invite, if the old one
was listed */
void p2p_set_hosting_public(int public);
/* whether the game this machine hosts is listed in everyone's server
browser (PUBLIC, and network.public_lobby): the text chat is off in one a
player hosts (a dedicated server's follows network.text_chat) */
int p2p_hosting_listed(void);
/* the password of the game hosted for the internet (Server Setup's
PASSWORD; NULL or empty: none): its listing lets only those who know it
join from the server browser (its invite link still joins it). Setting or
changing it makes a new invite, if the old one was listed. It takes a few
milliseconds (the password's key) */
void p2p_set_hosting_password(const char *password);
/* the hosted game's details as listed (printable ASCII is kept; NULL leaves
one as it was): the game's server calls it as they change (calling it with
the same again costs little) */
void p2p_set_game_listing(const char *name, const char *map, const char *gametype, int engine_type, int kind,
	int open, int in_progress, int has_teams);
/* ... whether it is an OpenH1 Standard game (port/linux/game/network_standard.c,
its listing's version that game's protocol version), one OpenCE players join
too (mixed: its version the network version, the protocol's in its stamp),
and its rate */
void p2p_set_game_listing_standard(int standard, int mixed, int rate);
/* ... and the bots its host plays (port/linux/game/bots.c), which the
players' count (p2p_set_game_player_counts) leaves out */
void p2p_set_game_bots(int count);
/* ... and the matchmaking playlist it was hosted for (port/linux/game/
matchmaking_menu.inc's; 0: none, a game of its own), which the browsers of
those looking for a game of the playlist find it by */
void p2p_set_game_listing_playlist(int playlist);
/* the server browser: while on, the public games are gathered (asked for
when turned on, and by p2p_lobby_refresh) */
void p2p_lobby_browse(int on);
void p2p_lobby_refresh(void);

enum
{
	P2P_LISTING_NAME_SIZE = 32,
	P2P_LISTING_MAP_SIZE = 32,
	P2P_LISTING_GAMETYPE_SIZE = 24,
	/* an invite link's text (P2P_LINK_SIZE) */
	P2P_LISTING_INVITE_SIZE = 79,
	/* a password's game's: its host's key and its key's hash, and its token
	sealed (P2P_KEY_SIZE, P2P_KEY_HASH_SIZE, P2P_SEALED_TOKEN_SIZE) */
	P2P_LISTING_SIGNING_KEY_SIZE = 32,
	P2P_LISTING_KEY_HASH_SIZE = 16,
	P2P_LISTING_SEALED_TOKEN_SIZE = 56,
	/* a master server's word on a listed server (master_list.c) */
	P2P_LISTING_PUBLISHER_SIZE = 32,
	P2P_LISTING_REGION_SIZE = 16,
	/* the masters that list it, by their key ids (a Discord link's pass is
	shown from one of them: player_link.c) */
	P2P_LISTING_MAXIMUM_MASTERS = 4,
	P2P_LISTING_MASTER_KEY_ID_SIZE = 8,
};

struct p2p_listing
{
	/* (empty for a locked one until p2p_listing_unlock opens it) */
	char invite[P2P_LISTING_INVITE_SIZE];
	/* the host's (its XNADDR's abEnet once reached) */
	unsigned char identifier[6];
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	char gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	unsigned char player_count, maximum_player_count, engine_type;
	/* the game's kind: an OpenCE gametype's mode, and Hardcore
	(port/linux/game/game_modes.h's game_modes_kind) */
	unsigned char kind;
	/* an OpenH1 Standard game (port/linux/game/network_standard.c), one
	OpenCE players join too (mixed), and its rate in Hz (a Legacy game's 30) */
	unsigned char standard, mixed, rate;
	/* the bots its host plays, apart from its players */
	unsigned char bots;
	unsigned char open, in_progress, has_teams;
	/* its host holds room for a party that joins it, its leader first
	(port/linux/game/network_party.c): an OpenH1 game of a build that does,
	which this one joins as an OpenH1 game */
	unsigned char holds_party;
	/* the matchmaking playlist it was hosted for (an OpenH1 game's; 0: none) */
	unsigned char playlist;
	/* joining it failed this run (p2p_lobby_mark_failed) */
	unsigned char failed;
	/* milliseconds, -1 if not known */
	short ping;
	/* it has a password: its invite is sealed with it (p2p_listing_unlock) */
	unsigned char locked;
	unsigned char key_hash[P2P_LISTING_KEY_HASH_SIZE];
	unsigned char signing_key[P2P_LISTING_SIGNING_KEY_SIZE];
	unsigned char sealed_token[P2P_LISTING_SEALED_TOKEN_SIZE];
	/* a dedicated server's, on a master server's word (its listing's own
	flag, which its host sets, is not taken: a game the brokers alone list
	is shown as the players'); listed by the master servers
	(master_list.c): by how many, its publisher's name and its region
	(empty: none said) */
	unsigned char dedicated;
	unsigned char master;
	unsigned char master_count;
	char publisher[P2P_LISTING_PUBLISHER_SIZE + 1];
	char region[P2P_LISTING_REGION_SIZE + 1];
	/* the network version its host plays (another's cannot be joined) */
	unsigned short network_version;
	/* the key ids of the masters that listed it this time the browser was
	open (master_list.c): the masters its server publishes to, of those this
	machine asks */
	unsigned char master_key_ids[P2P_LISTING_MAXIMUM_MASTERS][P2P_LISTING_MASTER_KEY_ID_SIZE];
	unsigned char master_key_id_count;
};
/* joins a public game the server browser found (p2p_join_invite with its
invite), remembering its host: nonzero if it held an invite */
int p2p_join_listing(const struct p2p_listing *listing);
/* whether this machine is in the public game it joined from the server
browser: the tunnel still reaches its host, and no other invite was joined
since. Such a game has no text chat, whatever its host says
(port/linux/game/network_chat.c), unless it is a dedicated server's: */
int p2p_joined_listed_host(void);
/* whether the public game joined (p2p_joined_listed_host) is a dedicated
server's that a master server lists: an approved operator's, whose chat
follows its network.text_chat (a listing's own dedicated flag, which any
host may set, is not enough) */
int p2p_joined_listed_dedicated(void);
/* the key ids of the masters that listed the game being joined, if it was
joined from the server browser (master_key_ids): how many, 0 if not known (an
invite's, a LAN's) */
int p2p_joined_listing_masters(unsigned char (*key_ids)[P2P_LISTING_MASTER_KEY_ID_SIZE], int maximum);
/* a copy of the public games found, in the order shown (the most players
first; then those not failed, the open ones, by name); returns their count */
int p2p_lobby_games(struct p2p_listing *games, int maximum_count);
/* the master servers' (port/linux/src/master_list.c, server_master.c): a
fresh signed listing of the game this machine hosts for the internet, as
the brokers get it (0 if it hosts none, or none public); a master's entry's
listing taken into the server browser's games (its signature checked) with
the master's word on it, 1 if it was; and the masters' lists fetched again
(each game's count of them starts over) */
int p2p_lobby_host_listing(unsigned char *bytes, int size);
/* network.public_address as last looked up (p2p.c), "a.b.c.d:port": 0 if it
is unset or cannot be looked up (an IPv6 one, which the tunnel does not use) */
int p2p_public_address(char *text, int size);
int p2p_lobby_take_master_listing(const unsigned char *payload, int size, const char *publisher, const char *region,
	int network_version_shown, int dedicated, const unsigned char *master_key_id);
void p2p_lobby_masters_heard(void);
/* how many hosts list a game of a newer network version, which is not
shown (an old build sees none of the games) */
int p2p_lobby_newer_games(void);
/* the game the browser has chosen (asked each frame): its host is probed,
and its tunnel kept open while it is chosen, so that joining it takes the
tunnel up at once */
void p2p_probe_keep(const struct p2p_listing *listing);
/* a game the browser could not join: kept, marked failed, for this run */
void p2p_lobby_mark_failed(const unsigned char *identifier);
/* whether a signalling broker answers (the player-hosted games' lists come
through them) */
int p2p_lobby_brokers_reached(void);

/* ---- what the server browser shows of the games found, and in what order:
its source (its top-left tab), Filters and the column titles (the menus'
network.browser_* settings). Pure, over a copy of the games (the harness
tests them) */

/* network.browser_source */
enum
{
	P2P_LOBBY_SOURCE_ALL,
	P2P_LOBBY_SOURCE_DEDICATED,
	P2P_LOBBY_SOURCE_PLAYERS,
};
/* network.browser_maps: games on maps this machine lacks shown, not, or
those it could download as it joins */
enum
{
	P2P_LOBBY_MAPS_YES,
	P2P_LOBBY_MAPS_NO,
	P2P_LOBBY_MAPS_DOWNLOADABLE,
};
/* what a game's map is to this machine */
enum
{
	P2P_LOBBY_MAP_INSTALLED,
	P2P_LOBBY_MAP_DOWNLOADABLE,
	P2P_LOBBY_MAP_MISSING,
};
/* network.browser_sort's orders (a column title's), each in its own first
direction: dedicated servers first, then the most players, then by name
(servers); the most players; by name, map or type (A to Z); those with a
password first */
enum
{
	P2P_LOBBY_SORT_SERVERS,
	P2P_LOBBY_SORT_PLAYERS,
	P2P_LOBBY_SORT_NAME,
	P2P_LOBBY_SORT_MAP,
	P2P_LOBBY_SORT_TYPE,
	P2P_LOBBY_SORT_LOCKED,
	NUMBER_OF_P2P_LOBBY_SORTS,
};
struct p2p_lobby_choices
{
	int source;
	int show_empty, show_full;
	/* one game engine's only (1 CTF, 2 Slayer, 3 Oddball, 4 King, 5 Race),
	0 any */
	int engine_type;
	int maps;
};
/* the games the choices show, kept at the front in their order; returns
how many. map_state tells what a game's map is to this machine (NULL: every
map installed). A game of another network version is kept: shown, greyed,
not joinable */
int p2p_lobby_filter(struct p2p_listing *games, int count, const struct p2p_lobby_choices *choices,
	int (*map_state)(const char *map));
/* the games in the order's (reverse: the other way), those of another
network version last; equal ones keep their order */
void p2p_lobby_sort(struct p2p_listing *games, int count, int order, int reverse);
/* network.browser_sort's text: an order's name, "-" before it reversed
("-players"); an unknown one is servers' */
int p2p_lobby_sort_parse(const char *text, int *reverse);
void p2p_lobby_sort_text(int order, int reverse, char *text, int size);

/* the join of an invite (or a listing) under way: its stage, and once it
failed why, as a word ("" while it goes on): "no_answer" (the host never
answered), "no_route" (it answered, and the tunnel never reached it); or
the host's game refused it (p2p_join_refused): "full", "closed", "banned",
"password", "version" or "token" (its invite was replaced). Returns the
stage */
enum
{
	P2P_JOIN_NONE,
	/* asking the brokers for the host */
	P2P_JOIN_ASKING,
	/* the host answered: the tunnel being made (hole punching) */
	P2P_JOIN_ANSWERED,
	/* the tunnel reaches the host: the game's own join follows */
	P2P_JOIN_CONNECTED,
	P2P_JOIN_FAILED,
};
int p2p_join_progress(int *stage, char *reason, int size);
/* the host of the game joined refused it (the game's rejection code:
network_game_protocol.h's _rejection_code_*), the game's client says */
void p2p_join_refused(int rejection_code);
/* a locked game's invite (into its invite), if the password is its own:
FALSE if not; TRUE for a game with none. It takes a few milliseconds */
int p2p_listing_unlock(struct p2p_listing *listing, const char *password);

/* the sizes of a Discord user's id and name as kept (with their end), and
the text kept of either as told: only the characters allowed (digits in an
id; letters, digits, "_", "." and "-" in a name), no longer than that */
enum
{
	P2P_DISCORD_ID_SIZE = 24,
	P2P_DISCORD_NAME_SIZE = 40,
};
void p2p_discord_sanitize(char *destination, int size, const char *source, int name);
/* the Discord user signed in to the client on this machine, as its
READY told (empty if none, or internet play is off) */
void p2p_discord_identity(char *id, int id_size, char *name, int name_size);
/* what Discord's activity says of the game this machine is in (p2p_discord.c;
port/linux/game/lobby_menu.inc tells it ten times a second, the same again
costing little): whether this machine hosts it and it is under way (not in
its lobby), what it is in UTF-8 ("OpenH1 120 Tick · Slayer on Blood Gulch";
NULL or empty: in no game), and a game joined's invite link (NULL: none, a
LAN game) and players */
void p2p_discord_set_game(int hosting, int playing, const char *description, const char *invite, int player_count,
	int maximum_player_count);
/* a Discord friend's ask to join this machine's party or game, the oldest
not answered (each is let go after a while): its serial (0: none), the
friend's name (Latin-1) and whether they would join a game (game, if not
NULL), not a party; the answer to one, letting them in or not */
enum
{
	P2P_DISCORD_REQUEST_NAME_SIZE = 33,
};
unsigned long p2p_discord_join_request(char *name, int size, int *game);
void p2p_discord_answer(unsigned long serial, int accept);
/* this machine's hardware id, as hex (empty if none), a host told it when
joining (a keyed hash of what the machine is known by: p2p.c); and the hex
kept of one told: lowercase hex digits only, P2P_HARDWARE_ID_BYTES' worth */
enum
{
	P2P_HARDWARE_ID_BYTES = 16,
	P2P_HARDWARE_ID_SIZE = 2 * P2P_HARDWARE_ID_BYTES + 1,
};
void p2p_hardware_id(char *hex, int size);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* the real address (network byte order) of the internet play peer of this
virtual address (network byte order): where its packets come from; 0 if
it is no peer's */
unsigned long p2p_peer_endpoint_address(unsigned long virtual_address);

/* ---- map sharing between the players of a game (port/linux/game/map_share.c)

The players of an internet game reach its host alone. To share a map they
reach each other too: a host tells a player the key hash of another, which
answers holders of the invite it joined with as its host does (only while
answering), and the player asks it for a session as it asked the host. */

enum
{
	/* the hash of a machine's public key, which reaches it */
	P2P_PLAYER_KEY_HASH_SIZE = 16,
	/* the port each machine takes the others' map sharing connections on:
	its game listens there without hosting */
	P2P_MAP_SHARE_PORT = 5153,
};

/* the identifier (6 bytes) and key hash of the internet play peer of this
virtual address (network byte order): 0 if it is no peer's */
int p2p_peer_key_hash(unsigned long virtual_address, unsigned char *identifier, unsigned char *key_hash);
/* the binding of the internet play tunnel's session with the peer of this
virtual address (network byte order), for a Discord link's pass
(player_link_binding of the session's secret, which both ends share and
nobody else has); 0 if it is no peer's */
int p2p_peer_binding(unsigned long virtual_address, unsigned char *binding);
/* whether this machine, a player of an internet game (not its host),
answers the game's other players: holders of the invite it joined with */
void p2p_set_answering_players(int on);
/* asks for a session with another player of the internet game this
machine joined, whose key has this hash (as the game's host told), if there
is none: nonzero if there is one, or it is asked for (for some seconds, or
until reached; asked again, it is asked for anew) */
int p2p_reach_player(const unsigned char *key_hash);
/* whether the tunnel reaches the machine with this identifier, and the
virtual address (network byte order) the game reaches it at */
int p2p_peer_reached(const unsigned char *identifier, unsigned long *address);
/* this network's NAT gives each destination its own port and no router
forwards the tunnel's: only games behind more lenient NATs can be reached */
int p2p_nat_strict(void);

/* where a join (p2p_join_invite, p2p_join_listing) has got to, for the
menus: finding the host (this machine's public address, then the
signalling brokers' answer), opening a path to it (the host answered: both
send to each other's addresses), or reached (its game is listed under
System Link) */
enum
{
	_p2p_join_idle,
	_p2p_join_finding,
	_p2p_join_opening,
	_p2p_join_reached,
};
struct p2p_join_status
{
	int stage;
	/* milliseconds since the join was asked for */
	unsigned long elapsed;
	/* the rounds of pings sent to every address of the host's */
	int tries;
	/* internet play is on; the request went to the brokers; a broker is
	connected */
	int online, asked, brokers;
	/* this network's NAT is strict (p2p_nat_strict); the host's addresses
	show that its is; the router was asked to forward a port and did not */
	int nat_strict, host_strict, upnp_tried;
	/* relays are being tried for the host (it was not reached directly in
	a few seconds, and both machines offered relays: p2p_relay.c); the host
	was reached through one */
	int relaying, relayed;
};
void p2p_join_status(struct p2p_join_status *status);
/* gives the join up (the menus' Back, and their timeout), the host not
reached */
void p2p_join_cancel(void);

/* halo --relay (server_mode.c): this process a relay alone (p2p_relay.c),
for any two machines that cannot meet directly, on network.tunnel_port
(2302 if none), until stop returns nonzero; 0, or -1 if its port could not
be opened. Nothing else of internet play runs */
int p2p_relay_run(int (*stop)(void));

/* BLAKE2b with a 32-byte digest (Monocypher's): a map's pieces
(map_share.c) */
void p2p_blake2b_256(const void *data, unsigned long size, unsigned char *digest);

/* ---- parties (p2p_party.c): friends grouped before any game, who go from
game to game together (port/linux/game/party.c is the menus' side). A
party's machines talk through the signalling brokers; the leader's game is
the party's, which the others join */

/* a party's link: this, then its token in hexadecimal */
#define P2P_PARTY_LINK_PREFIX "openh1://party/"
/* an older build's, which the game still takes (P2P_OLD_LINK_SCHEME) */
#define P2P_OLD_PARTY_LINK_PREFIX "halo://party/"

enum
{
	P2P_PARTY_MAXIMUM_MEMBERS = 16,
	/* a member's name as kept (printable ASCII), with its end */
	P2P_PARTY_NAME_SIZE = 16,
	P2P_PARTY_LINK_SIZE = 15 + 2 * 16 + 1,
	P2P_PARTY_MAXIMUM_NOTICES = 8,
};

/* in a party: none, joining one (its leader has not listed this machine
yet), a member */
enum
{
	_p2p_party_none,
	_p2p_party_joining,
	_p2p_party_member,
};

/* where a machine is: the menus, joining a game, an internet game, a game
no link reaches (a LAN game, a game of its own) */
enum
{
	_p2p_party_place_menus,
	_p2p_party_place_joining,
	_p2p_party_place_game,
	_p2p_party_place_local_game,
};

/* the leader's game, which the party follows: none (the menus), an
internet game (its invite), a game no link reaches */
enum
{
	_p2p_party_game_none,
	_p2p_party_game_invite,
	_p2p_party_game_local,
};

/* how a member's following its leader into a game went */
enum
{
	_p2p_party_follow_none,
	_p2p_party_follow_joined,
	/* the host was not reached */
	_p2p_party_follow_unreachable,
	_p2p_party_follow_full,
	/* another version of OpenH1's network code */
	_p2p_party_follow_version,
	/* a Legacy game this machine cannot join (an OpenCE-only one) */
	_p2p_party_follow_legacy,
	/* a game no link reaches (the leader's network's, or its own) */
	_p2p_party_follow_local,
	/* in another game: follows once back in the menus */
	_p2p_party_follow_busy,
	/* the host turned it away, or the join failed */
	_p2p_party_follow_refused,
};

/* what the menus show of what happened */
enum
{
	_p2p_party_notice_none,
	_p2p_party_notice_made,
	/* this machine joined (name: the leader's) */
	_p2p_party_notice_joined,
	_p2p_party_notice_member_joined,
	_p2p_party_notice_member_left,
	/* not heard for a while */
	_p2p_party_notice_member_lost,
	/* this machine leads now */
	_p2p_party_notice_leader,
	_p2p_party_notice_new_leader,
	/* the leader ended the party; this machine did */
	_p2p_party_notice_disbanded,
	_p2p_party_notice_ended,
	_p2p_party_notice_you_left,
	_p2p_party_notice_full,
	/* no answer from the party (it may have ended) */
	_p2p_party_notice_unreachable,
	/* a member of another version (name; none: someone) */
	_p2p_party_notice_version,
	/* a member could not join the leader's game (result: why) */
	_p2p_party_notice_follow_failed,
};

struct p2p_party_notice
{
	unsigned long serial;
	int kind;
	char name[P2P_PARTY_NAME_SIZE];
	int result;
};

struct p2p_party_member
{
	char name[P2P_PARTY_NAME_SIZE];
	unsigned char self, leader;
	/* _p2p_party_place_*; in the leader's game (or the menus with it) */
	unsigned char place, with_leader;
	/* its follow's result, of the leader's game now */
	unsigned char result;
	unsigned char other_version;
};

struct p2p_party_status
{
	/* _p2p_party_none, _joining, _member; this machine leads */
	int state;
	int leader;
	char link[P2P_PARTY_LINK_SIZE];
	char leader_name[P2P_PARTY_NAME_SIZE];
	/* the leader's game (_p2p_party_game_*) */
	int target_kind;
	/* in the roster's order (this machine first while it joins) */
	int count;
	struct p2p_party_member members[P2P_PARTY_MAXIMUM_MEMBERS];
	/* the latest notices, the newest last (kept after a party ends) */
	int notice_count;
	struct p2p_party_notice notices[P2P_PARTY_MAXIMUM_NOTICES];
};

/* the token of the party link within text: 0 if it holds none */
int p2p_party_parse_link(const char *text, unsigned char *token);
/* a new party, this machine its leader (the one it leads alone, if it
does), its link put on the clipboard */
int p2p_party_make(void);
/* joins the party of a link within text (leaving the one it is in): 0 if
it holds none. p2p_join_invite takes party links too */
int p2p_party_join_link(const char *text);
/* makes the party of a link within text, this machine its leader
(debug.party's: several copies of the game in one party, in a test) */
int p2p_party_lead_link(const char *text);
/* leaves the party (a leader's passes to the next member); a leader ends
it for everyone */
void p2p_party_leave(void);
void p2p_party_disband(void);
/* the party's link, to invite a player of the game (the scoreboard's Invite
to party: port/linux/game/network_party.c), a party made for it (this
machine its leader) if it is in none; the clipboard is left alone */
int p2p_party_invite_link(char *link, int size);
/* 32 bits of the party's topic (not of its link, which lets anyone in), by
which the machines of a game tell who is in the same party; 0 in none */
unsigned long p2p_party_tag(void);
/* what the party is told of this machine (each frame: unchanged, little
is done): its player's name, and where it is (_p2p_party_place_*) and in
what (_p2p_party_game_*, an internet game with its invite link, NULL
otherwise). A leader's game is the party's */
void p2p_party_set_name(const char *name);
void p2p_party_set_game(int place, int kind, const char *invite);
/* a member: the leader's game to join (an internet game, into invite, or
one no link reaches), once for each of the leader's games (order),
until p2p_party_follow_result says how it went; _p2p_party_game_none if
none (none, or this machine is in it already) */
int p2p_party_follow(char *invite, int size, unsigned long *order);
void p2p_party_follow_result(unsigned long order, int result);
/* the leader's game asked to be joined again (JOIN LEADER) */
void p2p_party_follow_again(void);
/* hosting for the party (matchmaking: port/linux/game/matchmaking_menu.inc).
How well this machine can host a game now, which a member's STATE tells its
leader (_p2p_party_host_*: each frame, the same again costing little) */
enum
{
	/* not now (in a game, or not in the menus), or not known (a build
	that does not say) */
	_p2p_party_host_unable,
	/* a strict NAT: some players will not reach it */
	_p2p_party_host_strict,
	_p2p_party_host_open,
};
void p2p_party_set_host_quality(int quality);
/* a leader that would host a game for the party (of the matchmaking
playlist, 1 to 255): the lead passed to the member that can host best, if
one can better than this machine (it then leads and hosts, and this machine
follows it): nonzero if it was; 0 (this machine hosts) if none can, or this
machine leads none */
int p2p_party_pass_host(int playlist);
/* the playlist this machine was asked to host a game of, as its party's new
leader, once (0: none) */
int p2p_party_take_host_request(void);
/* the party's other members, if this machine leads it: the hash of each
one's tunnel key (P2P_PLAYER_KEY_HASH_SIZE bytes each, into hashes: the
X25519 form of its party key, the key a game's host knows it by,
p2p_peer_key_hash), of those listed and of this network version, at most
maximum_count; returns their count (0 leading no one, or not leading) */
int p2p_party_member_key_hashes(unsigned char *hashes, int maximum_count);
void p2p_party_status(struct p2p_party_status *status);

/* ---- friends (p2p_friends.c): a player profile's lasting friends, by
friend code, their online status (each shares it with its friends alone),
and the players of its recent games (port/linux/game/friends_menu.inc is the
menus' side). Each profile has a key of its own, in friends.txt in its
folder; its code is its key's hash */

enum
{
	P2P_FRIENDS_MAXIMUM = 64,
	/* requests waiting, requests sent, codes blocked, recent players */
	P2P_FRIENDS_REQUESTS = 16,
	P2P_FRIENDS_SENT = 16,
	P2P_FRIENDS_BLOCKED = 32,
	P2P_FRIENDS_RECENT = 50,
	P2P_FRIENDS_LIST_MAXIMUM = P2P_FRIENDS_MAXIMUM + P2P_FRIENDS_REQUESTS + P2P_FRIENDS_SENT + P2P_FRIENDS_RECENT +
		P2P_FRIENDS_BLOCKED,
	/* a code (55 bits), and its text ("K7QM-2XPA-9DTR") with its end */
	P2P_FRIEND_CODE_BYTES = 7,
	P2P_FRIEND_CODE_TEXT_SIZE = 15,
	/* a name (printable ASCII) and a game's words (UTF-8), with their ends */
	P2P_FRIEND_NAME_SIZE = 16,
	P2P_FRIEND_GAME_SIZE = 64,
};

/* what an entry of the list is, in the list's order: a friend online, a
request to this profile, a friend offline, a request this profile sent, a
recent player, a code blocked. As a relation (p2p_friends_relation), a
friend is _p2p_friend_online, online or not */
enum
{
	_p2p_friend_online,
	_p2p_friend_request,
	_p2p_friend_offline,
	_p2p_friend_sent,
	_p2p_friend_recent,
	_p2p_friend_blocked,
	_p2p_friend_none,
	/* this profile's own code, or no profile */
	_p2p_friend_self,
};

struct p2p_friend
{
	int kind;
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
	/* a friend online: where (_p2p_party_place_*), hosting, playing (its
	game under way), its party's size (0: none), its game's players and
	most, whether JOIN reaches it, and whether its invite waits */
	unsigned char place, hosting, playing, party_count, players, maximum_players, joinable, inviting;
	/* a recent player: it has a code (not an OpenCE player's, nor an older
	build's), it is OpenCE's, and what it is to this profile (_p2p_friend_*) */
	unsigned char has_code, opence, relation;
	/* seconds since 1970: a friend's added, a request's, a recent player's
	seen */
	long long time;
	/* a friend online's game, a recent player's (UTF-8) */
	char game[P2P_FRIEND_GAME_SIZE];
};

/* the profile's: it has an identity (a profile), its code and name, a
broker is connected, its presence is shared; its friends, those online, the
requests waiting */
struct p2p_friends_state
{
	int identity;
	char code[P2P_FRIEND_CODE_TEXT_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	int connected, sharing;
	int friends, online, requests;
};

/* what the toasts say (port/linux/game/network_party.c) */
enum
{
	_p2p_friends_notice_online,
	_p2p_friends_notice_request,
	_p2p_friends_notice_accepted,
};

struct p2p_friends_notice
{
	unsigned long serial;
	int kind;
	char name[P2P_FRIEND_NAME_SIZE];
};

/* how an action went */
enum
{
	_p2p_friends_done,
	_p2p_friends_no_profile,
	_p2p_friends_self,
	_p2p_friends_already,
	_p2p_friends_full,
};

/* a code as its text ("K7QM-2XPA-9DTR"; size at least
P2P_FRIEND_CODE_TEXT_SIZE), and the code of a text: 1, 0 if it is none, -1
if its check character is wrong (a mistyped code) */
int p2p_friend_code_format(const unsigned char *code, char *text, int size);
int p2p_friend_code_parse(const char *text, unsigned char *code);
/* the profile whose friends these are: its folder (where friends.txt is;
empty: none) and its player's name, each frame (the same again costing
little); another folder is read (a new identity made in it if it has none) */
void p2p_friends_set_profile(const char *folder, const char *name);
/* the profile's code: 0 if there is no profile */
int p2p_friends_own_code(unsigned char *code);
/* the code another friends.txt says it has (a test's: debug.friends): 0 if
it says none */
int p2p_friends_code_in_file(const char *path, unsigned char *code);
void p2p_friends_state(struct p2p_friends_state *state);
/* the list, in its order; returns its count */
int p2p_friends_list(struct p2p_friend *entries, int maximum);
/* what a code is to the profile (_p2p_friend_*) */
int p2p_friends_relation(const unsigned char *code);
/* a friend request to a code (its player's name, if known), or its request
accepted (_p2p_friends_*) */
int p2p_friends_add(const unsigned char *code, const char *name);
/* a request turned down (not shown again); a request sent taken back; a
friend removed (on both sides); a code blocked (a friend removed, its
requests and invites let go: 0 if it cannot be, its own) and unblocked */
void p2p_friends_ignore(const unsigned char *code);
void p2p_friends_cancel(const unsigned char *code);
void p2p_friends_remove(const unsigned char *code);
int p2p_friends_block(const unsigned char *code, const char *name);
void p2p_friends_unblock(const unsigned char *code);
/* joins a friend's party (its invite's, else the one its presence offers:
its own, or the one made for it as the friend joins): 0 if none; invites a
friend to this machine's party (one made if none): 0 if not sent */
int p2p_friends_join(const unsigned char *code);
int p2p_friends_invite(const unsigned char *code);
/* a player of a network game this machine is in (its code, NULL if it has
none; an OpenCE player's), and the game's words (NULL: as Discord's
activity has them), for the recent players */
void p2p_friends_saw_player(const unsigned char *code, const char *name, int opence, const char *game);
/* the newest notice: 0 if none */
int p2p_friends_notice(struct p2p_friends_notice *latest);
/* a friend's invite to its party not answered yet, the oldest: its serial
(0: none) and the friend's name; and the answer, joining or not */
unsigned long p2p_friends_invite_prompt(char *name, int size);
void p2p_friends_answer(unsigned long serial, int accept);

#endif
