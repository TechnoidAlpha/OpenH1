/*
P2P.C

Internet play: machines that shared an invite reach each other's system
link games as if they were on one LAN, without a server of this project's.

- An invite is a link, openh1://join/<host><token>: the hash of the X25519
  public key the hosting machine makes each run (16 bytes of it, which no
  other key can be found to have; the first 6 are the machine's identifier,
  which its XNADDR also carries) and a random 16-byte token. A machine
  makes one when its game starts hosting (the game listens for
  connections), logs it, puts it on the clipboard, and offers it through
  Discord (p2p_discord.c). Nothing about a game is published anywhere else:
  without an invite there is no way to find or join it.
- Signalling (p2p_signal.c) goes through public MQTT brokers, on topics
  that are hashes of the token, with messages sealed with a key derived
  from it (p2p_crypto.c). A joiner offers its public key and the addresses
  it can be reached at; the host answers with its own, and makes the
  session once the joiner, answered, proves that it holds its key. The
  secret of their session comes from the two keys (X25519) and a nonce of
  each, and never travels.
- The tunnel is one UDP socket. Each machine learns its public address from
  public STUN servers, and both then send to each other's addresses until
  packets get through (hole punching). Two machines whose NATs both map
  every destination to a new port cannot meet so, unless a router forwards
  one of them a port. So a host asks its router to forward
  the tunnel's port (UPnP, posix_upnp.c) as soon as a player reaches out
  with its invite, and a joiner asks its own when it has not reached the
  host in a few seconds (network.allow_upnp); the forwarded port is one more
  of the addresses a machine offers (the joiner asks the host again every
  few seconds until they meet, and the host answers with them all). Every
  tunnel packet is sealed with a key of the session's for its direction,
  its header (the sender and the packet's number, the nonce) authenticated
  too, and a packet already received, or from further back than the last
  64, is dropped.
- Two machines that have not met after a few seconds (p2p_relay_due) try
  relays (p2p_relay.c): a dedicated server or a relay of halo --relay that
  both reach, which forwards their sealed packets and cannot read them.
  Never a player's machine. Each offers the other, through signalling
  (p2p_signal_relay_offer), the relays it knows: its relays.txt's
  (network.relays_file; a dedicated server's server.relays_file), and those
  the dedicated server of the game it plays in told it of (_packet_relays);
  both bind to all of them, the first pair whose packets get through
  carries the session, and the others stay paired, to fail over to. A
  direct path heard later takes the session back once it has lasted a few
  seconds (p2p_relay_path).
- A server or host behind a NAT that no one can punch through may register
  with a front (network.front; p2p_front.c): a public port on a relay of
  halo --relay that passes packets from anyone to it and its answers back.
  The front's address is one more of the addresses it offers joiners
  (p2p_local_candidates), which they punch to as to any, OpenCE's too; a
  joiner reached through it is a relay's path of the front's (its client
  there), and its address as the front saw it is the one its bans take.
- Each peer gets a virtual address in 100.64.0.0/10, which the game sees
  (XNetXnAddrToInAddr maps the peer's XNADDR to it). The game's datagrams
  to a peer go onto the tunnel from xnet.c at once (p2p_send_datagram);
  otherwise xnet.c rewrites the game's destinations there to local
  stand-ins: a UDP socket here per peer and port forwards datagrams (the
  peer's to the game, and the game's from a socket connected to the
  peer), and a TCP listener per peer and port takes the game's connections,
  carried reliably over the tunnel by KCP (port/third_party/kcp). Traffic
  arriving from a peer leaves these stand-ins, and xnet.c reports it as
  coming from the peer's address. The
  game's broadcasts also go to every peer, so a host's game shows up in its
  joiners' system link lists, and joining works as on a LAN. A peer reaches
  only the game's own ports (xnet.c says which it has), and has a few
  stand-ins and connections at a time.
- The server browser times the tunnel to each public game's host (its
  ping): a probe makes the tunnel as a join does, with the listing's token,
  keeps the shortest of a few pings' round trips, and says goodbye (but
  the game chosen in the browser keeps its tunnel while it is chosen:
  p2p_probe_keep). The game sees nothing of a probe's peer; a join of its
  host takes its tunnel up.
- A party (p2p_party.c) is friends grouped through the brokers alone, by a
  link of its own (openh1://party/...), which p2p_join_invite takes as it
  takes invites; its leader's game, as an invite, is what the others join.
- A join asks the brokers at once, beside STUN, and sends its request as
  soon as one STUN server has answered. A host takes a joiner's new proven
  request in place of the session it still had with it (the joiner gave
  that one up), rather than turning it away until that one lapses.

The work happens on a thread of its own, under p2p_lock (let go of for
whatever may wait: DNS, the desktop's link handlers); the game's threads
only look up and create stand-ins.
*/

#include "platform.h"
#include "posix.h"
#include "server_master.h"
#include "server_mode.h"
#include "master_list.h"
#include "port_config.h"
#include "p2p_internal.h"
#include "player_link.h"
#include "log_address.h"
#include "ikcp.h"
#ifdef HALO_PROFILE
#include "profile_trace.h"
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum
{
	/* the tunnel's version 2 (0x68 was the first) */
	TUNNEL_MAGIC = 0x69,
	/* the magic, the sender's identifier, and the packet's number */
	TUNNEL_HEADER_SIZE = 1 + P2P_IDENTIFIER_SIZE + 8,
	/* a tunnel packet's plaintext: a type, and at most the game's largest
	datagram (WSAStartup's iMaxUdpDg) with its ports */
	MAXIMUM_INNER_SIZE = 1400,
	MAXIMUM_PACKET_SIZE = TUNNEL_HEADER_SIZE + MAXIMUM_INNER_SIZE + P2P_TAG_SIZE,
	/* the packets received out of order that are still taken */
	REPLAY_WINDOW = 64,

	/* a host needs a UDP stand-in for two or three ports of every other
	machine, and a stream for each one's connection; one peer can have no
	more than these */
	MAXIMUM_PROXIES = 512,
	MAXIMUM_PEER_PROXIES = 4,
	MAXIMUM_LISTENERS = 64,
	MAXIMUM_STREAMS = 160,
	MAXIMUM_PEER_STREAMS = 4,
	MAXIMUM_PEER_OPENING_STREAMS = 2,
	/* the game's sockets' ports (xnet.c), and the ports it sent peers
	datagrams from */
	MAXIMUM_GAME_PORTS = 64,
	MAXIMUM_SENT_PORTS = 16,
	/* sessions that ended, which do not come back */
	MAXIMUM_RETIRED_SESSIONS = 16,
	/* the players a host is reaching at once (anyone with the invite can
	ask in any number of made-up names: each would have the host send to the
	addresses it gives) */
	MAXIMUM_OPENING_PEERS = 8,
	/* stand-ins closed lately, whose traffic the game may not have read yet
	(p2p_incoming) */
	MAXIMUM_CLOSED_PORTS = 128,
	CLOSED_PORT_TIME = 5000,
	/* a peer's stand-in used this lately is not closed for another: a peer
	sending from ever new ports has a few a second, not one a datagram */
	PROXY_REPLACE_TIME = 1000,
	MAXIMUM_STUN_SERVERS = 4,
	STREAM_BUFFER_SIZE = 16384,
	STREAM_CHUNK_SIZE = 1024,
	/* the most unacknowledged messages a stream sends ahead */
	STREAM_WINDOW = 128,
	KCP_MTU = 1200,

	/* milliseconds */
	LOOP_INTERVAL = 10,
	PUNCH_INTERVAL = 200,
	PING_INTERVAL = 1000,
	ENDPOINT_SWITCH_TIME = 3000,
	PEER_TIMEOUT = 20000,
	PUNCH_TIMEOUT = 30000,
	JOIN_TIMEOUT = 90000,
	/* another player of the game joined, asked for (p2p_reach_player) */
	PLAYER_REACH_TIMEOUT = 30000,
	/* a public game's host probed for its ping (p2p_probe_start): its
	round trip timed over this many pings, and given up after this long */
	PROBE_PINGS = 3,
	PROBE_TIMEOUT = 15000,
	/* the browser's game chosen keeps its host's tunnel while it asks this
	often (p2p_probe_keep), probed again this long after a probe of it ended
	unreached */
	PROBE_KEEP_TIME = 2000,
	PROBE_KEEP_RETRY = 5000,
	STREAM_LINGER_TIME = 10000,
	PROXY_IDLE_TIME = 60000,
	STUN_RETRY_INTERVAL = 500,
	STUN_REFRESH_INTERVAL = 25000,
	STUN_ATTEMPTS = 6,
	/* a joiner asks its router to forward the tunnel's port when it has not
	reached a peer in this long; a forwarding is renewed this often (its
	lease is an hour), and one refused asked for again this long after */
	UPNP_JOIN_DELAY = 2000,
	UPNP_RENEW_INTERVAL = 30 * 60 * 1000,
	UPNP_RETRY_INTERVAL = 5 * 60 * 1000,
	/* how long the game's exit waits for a request under way */
	UPNP_RELEASE_WAIT = 3000,

	/* where a running copy of the game takes invites from another one
	started to open a link (127.0.0.1), sealed with a key of the user's */
	HANDOFF_PORT = 47315,
};

typedef char tunnel_magic_assert[TUNNEL_MAGIC == P2P_TUNNEL_MAGIC &&
	TUNNEL_HEADER_SIZE + P2P_TAG_SIZE + 1 == P2P_TUNNEL_MINIMUM_PACKET &&
	MAXIMUM_PACKET_SIZE == P2P_TUNNEL_MAXIMUM_PACKET ? 1 : -1];

/* relays (p2p_relay.c) */
enum
{
	/* milliseconds: a peer not reached is offered relays again this often;
	a peer that offers none (an OpenCE machine, or an older build) is sent
	this many, and no more */
	RELAY_OFFER_INTERVAL = 2000,
	RELAY_OFFERS_UNANSWERED = 3,
	/* a peer whose relay is lost, and none other paired, has its relays
	bound again this often */
	RELAY_REBIND_INTERVAL = 2000,
	/* while relayed, the peer's own addresses are tried again this often
	(a direct path may open: a router's port forwarded), for this long, then
	this seldom */
	DIRECT_RETRY_INTERVAL = 3000,
	DIRECT_RETRY_TIME = 60000,
	DIRECT_RETRY_SLOW_INTERVAL = 30000,
	/* relays.txt read and looked up again this often */
	RELAYS_LOOKUP_INTERVAL = 300000,
	/* the relays this machine knows (relays.txt's), and its game's
	dedicated server told it of */
	MAXIMUM_OWN_RELAYS = 8,
	MAXIMUM_GAME_RELAYS = P2P_RELAY_OFFERED,
	/* a dedicated server tells each of its players its relays this many
	times, a ping apart (_packet_relays) */
	RELAYS_TOLD = 3,
};

enum
{
	_packet_ping = 1,
	_packet_pong,
	_packet_datagram,
	_packet_stream,
	_packet_bye,
	/* a dedicated server's relays, to a player of its game (_relays_*: it
	relays for its game's players itself; then a count, and each relay's
	address and port), which builds before relays pass over */
	_packet_relays,
};

enum
{
	_relays_server_relays = 1,
};

/* the messages of a stream, inside KCP */
enum
{
	_stream_open = 'O',
	_stream_data = 'D',
	_stream_close = 'C',
};

enum
{
	/* a peer's stream whose open has not arrived */
	_stream_awaiting_open,
	/* connecting to the local game */
	_stream_connecting,
	_stream_open_state,
};

struct peer
{
	int used;
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
	char name[2 * P2P_IDENTIFIER_SIZE + 1];
	/* the session's secret, and the keys from it for each direction */
	unsigned char secret[P2P_SHA256_SIZE];
	unsigned char send_key[P2P_SHA256_SIZE];
	unsigned char receive_key[P2P_SHA256_SIZE];
	/* the last packet number sent; the highest received, and which of the
	REPLAY_WINDOW before it were (bit n: highest - n) */
	unsigned long long send_counter;
	unsigned long long receive_highest;
	unsigned long long receive_window;
	unsigned long virtual_address;
	int is_host;
	/* (a host's: another player of its game, reached for map sharing:
	p2p_reach_player) */
	int is_player;
	/* the hash of its public key, which another machine reaches it by */
	unsigned char key_hash[P2P_KEY_HASH_SIZE];
	int connected;
	struct p2p_candidate candidates[P2P_MAXIMUM_CANDIDATES];
	int candidate_count;
	struct p2p_candidate endpoint;
	/* its UDP stand-ins (indices in p2p.proxies) */
	short proxies[MAXIMUM_PEER_PROXIES];
	int proxy_count;
	unsigned long offered_time;
	unsigned long heard_time;
	unsigned long endpoint_heard_time;
	unsigned long sent_time;
	unsigned long round_trip;
	/* the rounds of pings sent to every address it gave, unreached (the
	menus' "try") */
	int punch_rounds;
	/* the pings answered, and the shortest round trip of them */
	int pongs;
	unsigned long shortest_round_trip;
	/* a public game's host probed for its ping (p2p_probe_start): none of
	its traffic is the game's */
	int probe;
	/* relays (p2p_relay.c): the path it is reached by (directly, or a
	relay's attempt); the relays tried for it (both machines' offers);
	whether it offered any, the offers it sent and was sent; when this
	machine last offered, bound its relays again, and tried its own
	addresses while relayed; the address a direct path was heard from while
	relayed; the times a dedicated server told it of its relays */
	struct p2p_relay_path path;
	struct p2p_candidate relays[P2P_RELAY_PEER_RELAYS];
	int relay_count;
	int relay_offered;
	int relay_offers_sent;
	struct p2p_relay_offers_heard relay_offers_heard;
	unsigned long relay_offer_time;
	unsigned long relay_rebind_time;
	unsigned long direct_retry_time;
	struct p2p_candidate direct;
	unsigned long relayed_time;
	int relays_told;
	unsigned long relays_told_time;
};

/* a UDP stand-in for one port of a peer */
struct proxy
{
	int socket;
	int peer;
	unsigned short remote_port;
	unsigned short local_port;
	unsigned long used_time;
	/* the game's socket connected to it, or -1: while there is one, it
	stays */
	int pinned_socket;
};

/* a port of the game's (xnet.c) */
struct game_port
{
	int socket;
	/* 0: none */
	unsigned short port;
	unsigned char stream;
	unsigned char listening;
};

/* a stand-in closed lately (p2p_incoming) */
struct closed_port
{
	unsigned short local_port;
	unsigned short remote_port;
	int stream;
	unsigned long virtual_address;
	unsigned long time;
};

struct retired_session
{
	unsigned char secret[P2P_SHA256_SIZE];
	unsigned long virtual_address;
	/* the machine whose it was */
	unsigned char identifier[P2P_IDENTIFIER_SIZE];
};

/* a TCP stand-in for one port of a peer: takes the game's connections */
struct listener
{
	int socket;
	int peer;
	unsigned short remote_port;
	unsigned short local_port;
};

/* one of the game's TCP connections, carried over the tunnel */
struct stream
{
	int used;
	int peer;
	IUINT32 conversation;
	ikcpcb *kcp;
	int state;
	/* the local end: the game's connection to a listener, or a connection
	to the game made for a peer's (whose port stands for the peer's) */
	int socket;
	unsigned short local_port;
	unsigned short remote_port;
	int local_closed;
	int remote_closed;
	unsigned long created_time;
	unsigned long closed_time;
	/* something written to KCP or taken from it this pass (a message, a
	peer's segments): flushed at the pass's end, not at KCP's next interval */
	int flush;
	unsigned char pending[STREAM_BUFFER_SIZE];
	int pending_size;
};

struct stun_server
{
	char host[128];
	unsigned short port;
	unsigned long address;
	unsigned char transaction[12];
	int attempts;
	unsigned long sent_time;
	int has_mapped;
	struct p2p_candidate mapped;
};

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef HALO_PROFILE
/* the p2p thread's own track of the profiling build's recording
(profile_trace.c): profile.c's sections are the game thread's alone */
static struct
{
	int pass;
	int tunnel_receive;
	int kcp_update;
	int streams;
} p2p_profile_names;

/* the recording's track lock: the game thread flips the recording and its
arenas under it, and this thread records under it, so each record lands in
one arena; a pass that drops the lock (DNS, key exchange) can straddle a
cut, and profile_trace_end drops that scope */
void p2p_profile_lock(void)
{
	pthread_mutex_lock(&p2p_lock);
}

void p2p_profile_unlock(void)
{
	pthread_mutex_unlock(&p2p_lock);
}
#endif

static struct
{
	int running;
	unsigned long local_address;
	int tunnel_socket;
	unsigned short tunnel_port;
	int handoff_socket;

	struct peer peers[P2P_MAXIMUM_PEERS];
	struct retired_session retired[MAXIMUM_RETIRED_SESSIONS];
	int retired_next;
	struct proxy proxies[MAXIMUM_PROXIES];
	struct listener listeners[MAXIMUM_LISTENERS];
	struct stream streams[MAXIMUM_STREAMS];
	/* recently finished streams, whose late packets are ignored */
	IUINT32 finished[16];
	int finished_next;
	struct closed_port closed[MAXIMUM_CLOSED_PORTS];
	int closed_next;
	/* what peers may reach */
	struct game_port game_ports[MAXIMUM_GAME_PORTS];
	unsigned short sent_ports[MAXIMUM_SENT_PORTS];
	int sent_port_next;
	/* the key of invites handed over (handoff_readable) */
	int has_handoff_key;
	unsigned char handoff_key[P2P_SHA256_SIZE];

	struct stun_server stun[MAXIMUM_STUN_SERVERS];
	int stun_count;
	/* from the first time a game is hosted or joined */
	int stun_started;
	int reported_symmetric;

	/* hosting: while the game listens on hosting_socket, and its game may be
	joined from the internet (p2p_set_hosting_allowed) */
	int hosting_socket;
	int hosting;
	int hosting_lan_only;
	int has_token;
	unsigned char token[P2P_TOKEN_SIZE];
	/* whether the exit hook of the server browser and the party is set
	(lobby_quit) */
	int lobby_quit_registered;
	/* whether the server browser listed this token (p2p_lobby.c): going
	private makes a new one */
	int token_listed;
	char invite[P2P_LINK_SIZE];
	int invite_copied;
	/* the game's players and its most (p2p_set_game_player_counts; 0: not
	said, and the machines the tunnel reaches are shown), and what Discord
	was told */
	int game_player_count;
	int game_player_maximum;
	int reported_player_count;
	int reported_player_maximum;

	/* answering the other players of the game joined (map sharing:
	p2p_set_answering_players): wanted, and doing (p2p_signal_host with the
	token joined with) */
	int answer_players;
	int answering_players;
	/* the other players of the game joined being reached (p2p_reach_player):
	the hash of each one's key, and since when */
	struct
	{
		int used;
		int asked;
		unsigned char hash[P2P_KEY_HASH_SIZE];
		unsigned char identifier[P2P_IDENTIFIER_SIZE];
		unsigned long time;
	} reaching[P2P_MAXIMUM_PLAYER_JOINS];

	/* the server browser's probes of public games' hosts
	(p2p_probe_start): each host's identifier, and since when */
	struct
	{
		int used;
		unsigned char identifier[P2P_IDENTIFIER_SIZE];
		unsigned long time;
		/* its ping told the browser (a kept probe lives on: p2p_probe_keep) */
		int reported;
	} probes[P2P_MAXIMUM_PROBES];

	/* the server browser's game chosen (p2p_probe_keep): its host's tunnel
	kept open while it is, so that joining it takes the tunnel up at once */
	struct
	{
		unsigned char hash[P2P_KEY_HASH_SIZE];
		unsigned char token[P2P_TOKEN_SIZE];
		unsigned char identifier[P2P_IDENTIFIER_SIZE];
		/* asked for this lately; a probe of it last started */
		unsigned long time;
		unsigned long started_time;
	} keep;

	/* joining: until the host is reached, or JOIN_TIMEOUT */
	int join_requested;
	int joining;
	unsigned char join_host[P2P_IDENTIFIER_SIZE];
	unsigned char join_host_hash[P2P_KEY_HASH_SIZE];
	unsigned char join_token[P2P_TOKEN_SIZE];
	unsigned long join_time;
	/* when the join was asked for (the menus' and the log's clock), and
	whether it reached the host (p2p_join_status) */
	unsigned long join_asked_time;
	int join_reached;
	/* the join's progress (p2p_join_progress): one was asked for this run;
	its host answered; it was given up (JOIN_TIMEOUT); the host's game
	refused it (its rejection code, -1 if not) */
	int join_started;
	int join_answered;
	int join_gave_up;
	int join_refused;

	char clipboard[P2P_LINK_SIZE];
	int has_clipboard;

	/* UPnP (posix_upnp.c): a thread asking the router; the port it forwards
	here, and when it was last asked */
	int upnp_working;
	int upnp_asked;
	int upnp_forwarded;
	int upnp_release_registered;
	int upnp_released;
	struct p2p_candidate upnp_candidate;
	unsigned long upnp_time;
	/* network.public_address, looked up (an address and port joiners reach
	this machine at, behind a NAT with one forwarded port): offered first;
	the text it was looked up from, and when */
	struct p2p_candidate public_candidate;
	int has_public;
	char public_text[256];
	unsigned long public_time;
	int public_logged;

	/* relays (p2p_relay.c): relays.txt's, looked up (when, and from which
	setting's file); those the game's dedicated server told of, and which
	machine did; how this machine relays for others (a dedicated server
	alone), as last configured */
	struct p2p_candidate own_relays[MAXIMUM_OWN_RELAYS];
	int own_relay_count;
	unsigned long relays_time;
	char relays_setting[256];
	struct p2p_candidate game_relays[MAXIMUM_GAME_RELAYS];
	int game_relay_count;
	unsigned char game_relays_from[P2P_IDENTIFIER_SIZE];
	int relay_mode;

	/* network.front, looked up (the front this machine registers with,
	p2p_front.c): the text it was looked up from, and when */
	struct p2p_candidate front;
	char front_text[256];
	unsigned long front_time;
	/* halo --relay's front ports (server.front_ports), and when its
	operators' file was last read */
	int front_sockets[P2P_FRONT_MAXIMUM_PORTS];
	int front_socket_count;
	unsigned long front_operators_time;
} p2p = { 0, 0, -1, 0, -1, .hosting_socket = -1 };

/* the proxy (its index + 1) with each local port (all of theirs are on
p2p.local_address) */
static unsigned short proxy_by_port[65536];

static unsigned char identifier[P2P_IDENTIFIER_SIZE];
/* this run's X25519 keys, which the identifier comes from: from an Ed25519
seed, whose key signs the listing of a public game (p2p_lobby.c), so that a
listing's key is also the invite's */
static unsigned char seed[P2P_SEED_SIZE];
static unsigned char signing_key[P2P_KEY_SIZE];
static unsigned char secret_key[P2P_KEY_SIZE];
static unsigned char public_key[P2P_KEY_SIZE];
static int has_identifier;

/* (the relays' section, below) */
static void relay_corrupt(unsigned char *packet, int *size);
static void relay_peer_reached(struct peer *peer, int via);
static void relay_path_changed(struct peer *peer, int via, unsigned long address, unsigned short port);
static void update_relayed_peer(struct peer *peer);
static void relays_told_received(struct peer *peer, const unsigned char *inner, int size);
static int relayed_peer_address(const struct peer *peer, struct p2p_candidate *address);
/* halo --relay's run (p2p_relay_run): its log is the terminal's alone */
static int relay_standalone;
/* (the fronts' section, below) */
static void update_front(void);
static void front_operators_read(void);
static int front_ports_open(unsigned short *first_port);

/* ---------- helpers */

unsigned long p2p_now(void)
{
	return GetTickCount();
}

long long p2p_wall_seconds(void)
{
	return (long long)time(NULL);
}

unsigned long long p2p_wall_milliseconds(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_REALTIME, &now))
		return (unsigned long long)time(NULL) * 1000;
	return (unsigned long long)now.tv_sec * 1000 + (unsigned long long)now.tv_nsec / 1000000;
}

static int elapsed(unsigned long since, unsigned long time)
{
	/* (unsigned, as the clock wraps: a signed difference is negative for
	half of it, which had nothing lapse from a time of 0 from 24.8 days of
	uptime on) */
	return (unsigned int)(p2p_now() - since) >= (unsigned int)time;
}

/* errors.c's: a line of debug.txt, with its date (the game's own lines
are there: "attempting to connect to game") */
void write_to_error_file(char *string, unsigned char date);

void p2p_log(const char *format, ...)
{
	char text[512];
	size_t length;
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text) - 2, format, arguments);
	va_end(arguments);
	platform_log("%s", text);
	if (relay_standalone)
		return;
	length = strlen(text);
	memcpy(text + length, "\r\n", 3);
	write_to_error_file(text, 1);
}

/* the milliseconds since the join was asked for (p2p_join_invite), for the
log's lines of its steps */
static unsigned long join_elapsed(void)
{
	return p2p_now() - p2p.join_asked_time;
}

unsigned long p2p_resolve(const char *host)
{
	unsigned long address;

	/* DNS can take seconds, which the game's threads must not wait for */
	pthread_mutex_unlock(&p2p_lock);
	address = posix_resolve_ipv4(host);
	pthread_mutex_lock(&p2p_lock);
	return address;
}

void p2p_register_url_scheme(const char *scheme, const char *description)
{
	if (config_real("debug.exit_after") > 0.0 || config_boolean("debug.hidden_window") ||
		config_boolean("debug.null_renderer"))
		return;
	/* it may run a program and wait for it */
	pthread_mutex_unlock(&p2p_lock);
	posix_register_url_scheme(scheme, description);
	pthread_mutex_lock(&p2p_lock);
}

void p2p_forget_url_scheme(const char *scheme)
{
	if (config_real("debug.exit_after") > 0.0 || config_boolean("debug.hidden_window") ||
		config_boolean("debug.null_renderer"))
		return;
	/* (as p2p_register_url_scheme) */
	pthread_mutex_unlock(&p2p_lock);
	if (posix_forget_url_scheme(scheme))
		p2p_log("Internet play: this game no longer opens %s:// links (OpenCE's): its own are %s://", scheme,
			P2P_LINK_SCHEME);
	pthread_mutex_lock(&p2p_lock);
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[size * 2] = 0;
}

static int hex_value(char digit)
{
	if (digit >= '0' && digit <= '9')
		return digit - '0';
	if (digit >= 'a' && digit <= 'f')
		return digit - 'a' + 10;
	if (digit >= 'A' && digit <= 'F')
		return digit - 'A' + 10;
	return -1;
}

static unsigned long network_long(unsigned long value)
{
	return __builtin_bswap32(value);
}

static unsigned short network_short(unsigned short value)
{
	return (unsigned short)(value << 8 | value >> 8);
}

static void put_short(unsigned char *bytes, unsigned short value)
{
	/* value is in network byte order already */
	memcpy(bytes, &value, 2);
}

static unsigned short get_short(const unsigned char *bytes)
{
	unsigned short value;

	memcpy(&value, bytes, 2);
	return value;
}

static void make_address(struct sockaddr_in *address, unsigned long ip, unsigned short port)
{
	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_port = port;
	address->sin_addr.s_addr = ip;
}

/* a UDP or TCP socket bound to ip:port (0 for any), not blocking; its port
through bound_port; -1 on failure */
static int open_socket(int type, unsigned long ip, unsigned short port, unsigned short *bound_port)
{
	struct sockaddr_in address;
	int length = sizeof(address);
	int result = posix_socket(AF_INET, type, 0);

	if (result < 0)
		return -1;
	make_address(&address, ip, port);
	if (posix_socket_bind(result, &address, sizeof(address)) < 0 ||
		posix_socket_getsockname(result, &address, &length) < 0 ||
		posix_socket_set_nonblocking(result, 1) < 0)
	{
		posix_socket_close(result);
		return -1;
	}
	/* (the game's connections, carried over the tunnel: nothing held back) */
	if (type == SOCK_STREAM)
		posix_socket_set_nodelay(result);
	if (bound_port)
		*bound_port = address.sin_port;
	return result;
}

static int would_block(void)
{
	int error = posix_socket_last_error();

	return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

const unsigned char *p2p_identifier(void)
{
	/* its own lock: the p2p thread asks while holding p2p_lock */
	static pthread_mutex_t identifier_lock = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&identifier_lock);
	if (!has_identifier)
	{
		posix_random_bytes(seed, sizeof(seed));
		p2p_ed25519_public(seed, signing_key, secret_key);
		p2p_x25519(public_key, secret_key, NULL);
		p2p_identifier_for(public_key, identifier);
		has_identifier = 1;
	}
	pthread_mutex_unlock(&identifier_lock);
	return identifier;
}

void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *result)
{
	memcpy(result, hash, P2P_IDENTIFIER_SIZE);
	/* like a locally administered unicast MAC address, as XNADDR's abEnet
	holds one */
	result[0] = (unsigned char)((result[0] & 0xFC) | 0x02);
}

void p2p_identifier_for(const unsigned char *key, unsigned char *result)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];

	p2p_key_hash(key, hash);
	p2p_identifier_from_hash(hash, result);
}

const unsigned char *p2p_public_key(void)
{
	p2p_identifier();
	return public_key;
}

const unsigned char *p2p_signing_key(void)
{
	p2p_identifier();
	return signing_key;
}

void p2p_sign(const void *message, int size, unsigned char *signature)
{
	p2p_identifier();
	p2p_ed25519_sign(seed, signing_key, message, size, signature);
}

int p2p_shared_secret(const unsigned char *key, unsigned char *shared)
{
	static const unsigned char zero[P2P_KEY_SIZE];

	p2p_identifier();
	/* (milliseconds of arithmetic, which the game's threads need not wait
	for: nothing here changes meanwhile) */
	pthread_mutex_unlock(&p2p_lock);
	p2p_x25519(shared, secret_key, key);
	pthread_mutex_lock(&p2p_lock);
	/* a key of small order gives a secret anyone knows */
	return !p2p_equal(shared, zero, P2P_KEY_SIZE);
}

/* ---------- peers */

static struct peer *find_peer(const unsigned char *peer_identifier)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (p2p.peers[index].used && !memcmp(p2p.peers[index].identifier, peer_identifier, P2P_IDENTIFIER_SIZE))
			return &p2p.peers[index];
	}
	return NULL;
}

static struct peer *find_peer_by_address(unsigned long address)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (p2p.peers[index].used && !p2p.peers[index].probe && p2p.peers[index].virtual_address == address)
			return &p2p.peers[index];
	}
	return NULL;
}

/* ---------- this machine's hardware id */

#ifdef HALO_WINDOWS
/* the host's (port/windows/src/win32_p2p.c, through
port/windows/host/guest_windows.c): the SMBIOS system UUID, else the
registry's MachineGuid */
int posix_hardware_id_source(char *text, int size);
#endif

/* what this machine is known by, as text (none: 0): Windows' SMBIOS UUID or
MachineGuid (win32_p2p.c); Linux's /etc/machine-id; Android's ANDROID_ID,
which only the app's Java can read and puts in hardware_id.txt
(LauncherActivity.java) */
static int hardware_id_source(char *text, int size)
{
#ifdef HALO_WINDOWS
	return posix_hardware_id_source(text, size);
#else
	static const char *const linux_paths[] = { "/etc/machine-id", "/var/lib/dbus/machine-id" };
	char android_path[1024];
	const char *paths[2];
	int path_count = 0;
	int index;

#ifdef HALO_ANDROID
	snprintf(android_path, sizeof(android_path), "%s/hardware_id.txt", platform_data_root());
	paths[path_count++] = android_path;
#else
	(void)android_path;
	paths[path_count++] = linux_paths[0];
	paths[path_count++] = linux_paths[1];
#endif
	for (index = 0; index < path_count; index++)
	{
		FILE *file = fopen(paths[index], "rb");
		size_t length;

		if (!file)
			continue;
		length = fread(text, 1, (size_t)size - 1, file);
		fclose(file);
		text[length] = 0;
		/* (the line, without its end) */
		text[strcspn(text, "\r\n")] = 0;
		if (text[0])
			return 1;
	}
	return 0;
#endif
}

/* this machine's hardware id, as hex (empty if it has none to tell): a hash
of what it is known by (hardware_id_source) keyed for this game, so that
what is told is no raw serial and is this game's alone; a host it joins
logs it, and refuses one it banned. Anyone with administrator or root can
change what it is known by: a stable id, not a proof */
void p2p_hardware_id(char *hex, int size)
{
	static const char key[] = "halo-ce-universal hardware id v1";
	static char cached[2 * P2P_HARDWARE_ID_BYTES + 1];
	static int computed;

	if (!computed)
	{
		char source[256];
		unsigned char digest[P2P_SHA256_SIZE];

		computed = 1;
		cached[0] = 0;
		if (hardware_id_source(source, sizeof(source)))
		{
			p2p_hmac_sha256((const unsigned char *)key, (int)sizeof(key) - 1, source, (int)strlen(source), digest);
			p2p_hex(digest, P2P_HARDWARE_ID_BYTES, cached);
		}
	}
	snprintf(hex, (size_t)size, "%s", cached);
}

void p2p_hardware_id_sanitize(char *destination, int size, const char *source)
{
	int length = 0;

	for (; source && *source && length < size - 1 && length < 2 * P2P_HARDWARE_ID_BYTES; source++)
	{
		char character = *source >= 'A' && *source <= 'F' ? *source - 'A' + 'a' : *source;

		if ((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f'))
			destination[length++] = character;
	}
	if (size > 0)
		destination[length] = 0;
}

void p2p_discord_identity(char *id, int id_size, char *name, int name_size)
{
	if (id_size > 0)
		id[0] = 0;
	if (name_size > 0)
		name[0] = 0;
	if (!p2p.running || id_size <= 0 || name_size <= 0)
		return;
	pthread_mutex_lock(&p2p_lock);
	p2p_discord_user(id, id_size, name, name_size);
	pthread_mutex_unlock(&p2p_lock);
}

unsigned long p2p_peer_endpoint_address(unsigned long virtual_address)
{
	struct peer *peer;
	unsigned long address = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(virtual_address);
	/* (a peer reached through a relay: where the relay saw and checked its
	packets come from, which the relay tells the host's side alone; never
	an address the peer gave of itself, which anyone could give. Unknown
	otherwise: its bans and votes go by its other marks) */
	if (peer && peer->connected && peer->path.carrier)
	{
		struct p2p_candidate relayed;

		address = relayed_peer_address(peer, &relayed) ? relayed.address : 0;
	}
	else if (peer)
		address = peer->endpoint.address ? peer->endpoint.address :
			(peer->candidate_count > 0 ? peer->candidates[0].address : 0);
	pthread_mutex_unlock(&p2p_lock);
	return address;
}

static int is_virtual_address(unsigned long address)
{
	/* 100.64.0.0/10 */
	return (network_long(address) & 0xFFC00000) == 0x64400000;
}

/* whether address was another machine's than the identifier's, recently: the
game may still have traffic for that machine, which a new peer at the
address would take (and whose traffic would seem to come from it) */
static int address_retired_by_another(unsigned long address, const unsigned char *peer_identifier)
{
	int index;

	for (index = 0; index < MAXIMUM_RETIRED_SESSIONS; index++)
	{
		if (p2p.retired[index].virtual_address == address &&
			memcmp(p2p.retired[index].identifier, peer_identifier, P2P_IDENTIFIER_SIZE))
			return 1;
	}
	return 0;
}

/* an address in 100.64.0.0/10 from the identifier, not one another peer has
or had lately (the same machine reached again has its own back) */
static unsigned long virtual_address_for(const unsigned char *peer_identifier)
{
	unsigned char digest[P2P_SHA256_SIZE];
	unsigned long value;

	p2p_sha256(peer_identifier, P2P_IDENTIFIER_SIZE, digest);
	value = (unsigned long)digest[0] << 16 | (unsigned long)digest[1] << 8 | digest[2];
	for (;;)
	{
		unsigned long address = 0x64400000 | (value & 0x3FFFFF);

		/* no .0 or .255, which look like network and broadcast addresses */
		if ((address & 255) != 0 && (address & 255) != 255 && !find_peer_by_address(network_long(address)) &&
			!address_retired_by_another(network_long(address), peer_identifier))
			return network_long(address);
		value++;
	}
}

/* a tunnel packet's number, from its header */
static unsigned long long packet_counter(const unsigned char *packet)
{
	unsigned long long counter = 0;
	int index;

	for (index = 7; index >= 0; index--)
		counter = counter << 8 | packet[1 + P2P_IDENTIFIER_SIZE + index];
	return counter;
}

/* its nonce: its number (each direction has a key of its own) */
static void packet_nonce(const unsigned char *packet, unsigned char *nonce)
{
	memset(nonce, 0, P2P_NONCE_SIZE - 8);
	memcpy(nonce + P2P_NONCE_SIZE - 8, packet + 1 + P2P_IDENTIFIER_SIZE, 8);
}

/* a tunnel packet sealed for the peer, sent to an address (to), or through
a relay's attempt (relay: its index + 1; to is not used) */
static void peer_send_via(struct peer *peer, const struct p2p_candidate *to, int relay, const unsigned char *inner,
	int size)
{
	unsigned char packet[MAXIMUM_PACKET_SIZE];
	unsigned char nonce[P2P_NONCE_SIZE];
	struct sockaddr_in address;
	unsigned long long counter;
	int sealed;
	int index;

	if (p2p.tunnel_socket < 0 || size > MAXIMUM_INNER_SIZE)
		return;
	/* the header, authenticated with the rest */
	counter = ++peer->send_counter;
	packet[0] = TUNNEL_MAGIC;
	memcpy(packet + 1, identifier, P2P_IDENTIFIER_SIZE);
	for (index = 0; index < 8; index++)
		packet[1 + P2P_IDENTIFIER_SIZE + index] = (unsigned char)(counter >> (index * 8));
	packet_nonce(packet, nonce);
	sealed = p2p_aead_seal(peer->send_key, nonce, packet, TUNNEL_HEADER_SIZE, inner, size,
		packet + TUNNEL_HEADER_SIZE);
	if (relay)
	{
		p2p_relay_send(relay - 1, packet, TUNNEL_HEADER_SIZE + sealed);
		return;
	}
	make_address(&address, to->address, to->port);
	posix_socket_sendto(p2p.tunnel_socket, packet, TUNNEL_HEADER_SIZE + sealed, 0, &address, sizeof(address));
}

static void peer_send_to(struct peer *peer, const struct p2p_candidate *to, const unsigned char *inner, int size)
{
	peer_send_via(peer, to, 0, inner, size);
}

/* to a peer the tunnel has reached, on its path (its address, or its
relay); dropped otherwise, and while its relay is being replaced */
static void peer_send(struct peer *peer, const unsigned char *inner, int size)
{
	if (!peer->connected || peer->path.carrier < 0)
		return;
	peer_send_via(peer, &peer->endpoint, peer->path.carrier, inner, size);
	peer->sent_time = p2p_now();
}

/* a ping to an address (to), or through a relay's attempt (relay: its
index + 1) */
static void peer_ping_via(struct peer *peer, const struct p2p_candidate *to, int relay)
{
	unsigned char inner[5];
	unsigned int now = (unsigned int)p2p_now();

	inner[0] = _packet_ping;
	memcpy(inner + 1, &now, 4);
	peer_send_via(peer, to, relay, inner, sizeof(inner));
}

static void peer_ping(struct peer *peer, const struct p2p_candidate *to)
{
	peer_ping_via(peer, to, 0);
}

static void stream_free(struct stream *stream);
static void proxy_close(struct proxy *proxy);
static void listener_close(struct listener *listener);

/* everything carrying traffic for the peer (of this index) */
static void release_peer_links(int peer_index, int streams_only)
{
	struct peer *peer = &p2p.peers[peer_index];
	int index;

	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		if (p2p.streams[index].used && p2p.streams[index].peer == peer_index)
			stream_free(&p2p.streams[index]);
	}
	if (streams_only)
		return;
	while (peer->proxy_count > 0)
		proxy_close(&p2p.proxies[peer->proxies[peer->proxy_count - 1]]);
	for (index = 0; index < MAXIMUM_LISTENERS; index++)
	{
		if (p2p.listeners[index].socket >= 0 && p2p.listeners[index].peer == peer_index)
			listener_close(&p2p.listeners[index]);
	}
}

static int session_retired(const unsigned char *secret)
{
	int index;

	for (index = 0; index < MAXIMUM_RETIRED_SESSIONS; index++)
	{
		if (!memcmp(p2p.retired[index].secret, secret, P2P_SHA256_SIZE))
			return 1;
	}
	return 0;
}

/* whether address was a peer's, recently */
static int address_retired(unsigned long address)
{
	int index;

	for (index = 0; index < MAXIMUM_RETIRED_SESSIONS; index++)
	{
		if (p2p.retired[index].virtual_address == address)
			return 1;
	}
	return 0;
}

static void drop_peer(struct peer *peer, const char *reason)
{
	int ask_again = p2p.joining && peer->is_host && !memcmp(peer->identifier, p2p.join_host, P2P_IDENTIFIER_SIZE);

	if (!peer->probe)
	{
		p2p_log("Internet play: %s %s: %s", peer->is_host && !peer->is_player ? "host" : "player", peer->name,
			reason);
	}
	if (peer->connected)
	{
		unsigned char bye = _packet_bye;

		peer_send(peer, &bye, 1);
	}
	release_peer_links((int)(peer - p2p.peers), 0);
	/* (its relays released; its path unmarked first, as each attempt
	ending would otherwise look for another) */
	peer->path.carrier = 0;
	p2p_relay_forget_peer(peer->identifier, 0);
	/* a session that ended does not come back: its packets would pass
	again. (Not a probe's, which pushes no other's out: its request ended
	with it, and the game never had its address) */
	if (!peer->probe)
	{
		struct retired_session *retired = &p2p.retired[p2p.retired_next++ % MAXIMUM_RETIRED_SESSIONS];

		memcpy(retired->secret, peer->secret, P2P_SHA256_SIZE);
		retired->virtual_address = peer->virtual_address;
		memcpy(retired->identifier, peer->identifier, P2P_IDENTIFIER_SIZE);
	}
	memset(peer, 0, sizeof(*peer));
	/* still joining: a new request, with a nonce of its own (the host
	makes no second session from one request, which anyone who saw it could
	send again) */
	if (ask_again)
		p2p_signal_join(p2p.join_host_hash, p2p.join_token);
}

/* whether an offered address could be a machine's: not 0.0.0.0/8,
multicast, reserved or broadcast (224.0.0.0 up), nor port 0. Loopback and
LAN addresses stay: two copies on one machine or network use them */
static int candidate_usable(const struct p2p_candidate *candidate)
{
	unsigned long first = network_long(candidate->address) >> 24;

	return first != 0 && first < 224 && candidate->port != 0;
}

static void add_candidates(struct peer *peer, const struct p2p_candidate *candidates, int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		int known;

		if (!candidate_usable(&candidates[index]))
			continue;
		for (known = 0; known < peer->candidate_count; known++)
		{
			if (peer->candidates[known].address == candidates[index].address &&
				peer->candidates[known].port == candidates[index].port)
				break;
		}
		if (known < peer->candidate_count)
			continue;
		if (peer->candidate_count < P2P_MAXIMUM_CANDIDATES)
			peer->candidates[peer->candidate_count++] = candidates[index];
		else
			peer->candidates[P2P_MAXIMUM_CANDIDATES - 1] = candidates[index];
	}
}

/* a free peer, or NULL */
static struct peer *free_peer(void)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (!p2p.peers[index].used)
			return &p2p.peers[index];
	}
	return NULL;
}

/* the players this host is reaching and has not reached yet */
static int opening_peer_count(void)
{
	int count = 0;
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		count += p2p.peers[index].used && !p2p.peers[index].connected && !p2p.peers[index].is_host;
	return count;
}

int p2p_peer_turned_away(const unsigned char *peer_identifier, int is_host)
{
	static unsigned long logged_time;
	const char *reason = NULL;
	struct peer const *peer = find_peer(peer_identifier);

	/* (another session with the machine waits until the one that lives
	lapses: anyone with the invite can ask in its name. But a joiner's own
	new request, proven, replaces its session with this host, which it
	gave up: p2p_peer_offered. Answering it is as for any request) */
	if (!memcmp(peer_identifier, identifier, P2P_IDENTIFIER_SIZE) || (peer && (is_host || peer->is_host)))
		return 1;
	if (peer)
		return 0;
	if (!free_peer())
		reason = "too many players";
	else if (!is_host && opening_peer_count() >= MAXIMUM_OPENING_PEERS)
		reason = "too many players connecting at once";
	if (!reason)
		return 0;
	/* (anyone with the invite can ask as often as they like) */
	if (!logged_time || elapsed(logged_time, 10000))
	{
		p2p_log("Internet play: %s; one more was turned away (it asks again)", reason);
		logged_time = p2p_now() | 1;
	}
	return 1;
}

int p2p_peer_offered(const unsigned char *peer_identifier, const unsigned char *public_key, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int offered)
{
	struct peer *peer = find_peer(peer_identifier);
	/* (a player answering as a host does: the keys of a host's session) */
	int is_host = offered != _p2p_offered_joiner;

	if (!memcmp(peer_identifier, identifier, P2P_IDENTIFIER_SIZE))
		return 0;
	/* a joiner's new request, proven (only the holder of its key can): it
	gave its session up (cancelled, or timed out before this host had
	given up on it, or a probe of the server browser's whose goodbye was
	lost), and its new one replaces it at once, not once it lapses */
	if (peer && !is_host && !peer->is_host && memcmp(peer->secret, secret, P2P_SHA256_SIZE) &&
		!session_retired(secret))
	{
		drop_peer(peer, "asked again (its new request replaces the session)");
		peer = NULL;
	}
	if (peer)
	{
		/* another session with the machine waits until this one lapses:
		anyone with the invite can ask in its name */
		if (memcmp(peer->secret, secret, P2P_SHA256_SIZE))
			return 0;
	}
	else
	{
		unsigned char joiner_key[P2P_SHA256_SIZE], host_key[P2P_SHA256_SIZE];

		if (session_retired(secret) || p2p_peer_turned_away(peer_identifier, is_host))
			return 0;
		peer = free_peer();
		memset(peer, 0, sizeof(*peer));
		peer->used = 1;
		memcpy(peer->identifier, peer_identifier, P2P_IDENTIFIER_SIZE);
		p2p_hex(peer_identifier, P2P_IDENTIFIER_SIZE, peer->name);
		/* a key for each direction, so that nothing sent one way passes the
		other */
		memcpy(peer->secret, secret, P2P_SHA256_SIZE);
		p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "joiner", 6, joiner_key);
		p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "host", 4, host_key);
		memcpy(peer->send_key, is_host ? joiner_key : host_key, P2P_SHA256_SIZE);
		memcpy(peer->receive_key, is_host ? host_key : joiner_key, P2P_SHA256_SIZE);
		/* (packets are numbered from 1) */
		peer->receive_window = 1;
		peer->virtual_address = virtual_address_for(peer_identifier);
		peer->is_host = is_host;
		peer->is_player = offered == _p2p_offered_player;
		peer->probe = offered == _p2p_offered_probe;
		p2p_key_hash(public_key, peer->key_hash);
		peer->offered_time = p2p_now();
		/* (the host of the join under way answered it: p2p_join_progress) */
		if (offered == _p2p_offered_host && (p2p.joining || p2p.join_requested) &&
			!memcmp(peer_identifier, p2p.join_host, P2P_IDENTIFIER_SIZE))
		{
			p2p.join_answered = 1;
		}
		if (!peer->probe && offered == _p2p_offered_host && p2p.joining)
		{
			/* (the host answered: the path to it is opened now) */
			p2p_log("Internet play: reaching host %s, %d %s (%lu ms after the join was asked for)", peer->name,
				count, count == 1 ? "address" : "addresses", join_elapsed());
		}
		else if (!peer->probe)
		{
			p2p_log("Internet play: reaching %s %s", offered == _p2p_offered_host ? "host" : "player", peer->name);
		}
	}
	add_candidates(peer, candidates, count);
	return 1;
}

int p2p_peer_reoffered(const unsigned char *peer_identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count)
{
	struct peer *peer = find_peer(peer_identifier);

	if (!peer || memcmp(peer->secret, secret, P2P_SHA256_SIZE))
		return 0;
	add_candidates(peer, candidates, count);
	return 1;
}

/* whether a packet of this number from the peer is new: not received yet,
nor from before the window */
static int packet_fresh(const struct peer *peer, unsigned long long counter)
{
	unsigned long long behind;

	if (counter > peer->receive_highest)
		return 1;
	behind = peer->receive_highest - counter;
	return behind < REPLAY_WINDOW && !((peer->receive_window >> behind) & 1);
}

static void packet_received(struct peer *peer, unsigned long long counter)
{
	if (counter > peer->receive_highest)
	{
		unsigned long long ahead = counter - peer->receive_highest;

		peer->receive_window = ahead >= REPLAY_WINDOW ? 0 : peer->receive_window << ahead;
		peer->receive_highest = counter;
	}
	peer->receive_window |= 1ULL << (peer->receive_highest - counter);
}

/* newest: the packet is the highest numbered yet (a replayed or delayed one
does not move the peer's endpoint); relay: it came through a relay's
attempt (its index + 1, and address is the relay's), 0 directly */
static void peer_heard(struct peer *peer, unsigned long address, unsigned short port, int newest, int relay)
{
	unsigned long now = p2p_now();
	int same = !peer->path.carrier && peer->endpoint.address == address && peer->endpoint.port == port;

	peer->heard_time = now;
	if (!peer->connected)
	{
		char text[LOG_ADDRESS_SIZE];
		const char *through = relay ? " through a relay" : "";

		peer->connected = 1;
		peer->endpoint.address = address;
		peer->endpoint.port = port;
		peer->endpoint_heard_time = now;
		relay_peer_reached(peer, relay);
		if (peer->probe)
			return;
		if (p2p.joining && peer->is_host && !peer->is_player &&
			!memcmp(p2p.join_host, peer->identifier, P2P_IDENTIFIER_SIZE))
		{
			p2p_log("Internet play: connected to host %s at %s%s (%lu ms after the join was asked for, %lu after "
				"the host answered)", peer->name, log_address_ipv4(address, port, text, sizeof(text)), through,
				join_elapsed(), now - peer->offered_time);
		}
		else
		{
			p2p_log("Internet play: connected to %s %s at %s%s", peer->is_host && !peer->is_player ? "host" :
				"player", peer->name, log_address_ipv4(address, port, text, sizeof(text)), through);
		}
		if (peer->is_host && !peer->is_player)
		{
			if (p2p.joining && !memcmp(p2p.join_host, peer->identifier, P2P_IDENTIFIER_SIZE))
			{
				p2p.joining = 0;
				p2p.join_reached = 1;
				p2p_signal_stop_joining();
			}
			p2p_log("Internet play: the host's game is listed under Multiplayer, System Link");
		}
	}
	else if (relay || peer->path.carrier)
	{
		/* (a relayed peer's, or a relay's packet: which path carries it is
		p2p_relay_path's, a direct one taking over only once it lasted) */
		if (!relay)
		{
			peer->direct.address = address;
			peer->direct.port = port;
		}
		if (newest && p2p_relay_path_heard(&peer->path, relay, now))
			relay_path_changed(peer, relay, address, port);
	}
	else if (same)
	{
		peer->endpoint_heard_time = now;
		peer->path.carrier_heard = now;
	}
	else if (newest && elapsed(peer->endpoint_heard_time, ENDPOINT_SWITCH_TIME))
	{
		/* its address changed (a NAT's mapping, or a better path) */
		peer->endpoint.address = address;
		peer->endpoint.port = port;
		peer->endpoint_heard_time = now;
		peer->path.carrier_heard = now;
	}
}

static void update_peers(void)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &p2p.peers[index];

		if (!peer->used)
			continue;
		if (peer->connected)
		{
			if (elapsed(peer->heard_time, PEER_TIMEOUT))
				drop_peer(peer, "lost the connection");
			else if (peer->path.carrier || peer->relay_count)
				update_relayed_peer(peer);
			else if (elapsed(peer->sent_time, PING_INTERVAL))
			{
				peer_ping(peer, &peer->endpoint);
				peer->sent_time = p2p_now();
			}
		}
		else if (elapsed(peer->offered_time, PUNCH_TIMEOUT))
		{
			drop_peer(peer, peer->relay_count ? "could not connect (both networks' NATs may be too strict for a "
				"direct connection, and no relay reached both; forwarding network.tunnel_port on one router "
				"helps, as UPnP does where the router allows it: network.allow_upnp)" :
				"could not connect (both networks' NATs may be too strict for a direct connection, and no "
				"relay was known to both: relays.txt, network.relays_file; forwarding network.tunnel_port on "
				"one router helps, as UPnP does where the router allows it: network.allow_upnp)");
		}
		else if (elapsed(peer->sent_time, PUNCH_INTERVAL))
		{
			int candidate;

			/* (every address it gave, until a packet sealed by it arrives
			from one: then only that one, above. The addresses are anyone's
			who has the invite to give, so they are sent to no longer than
			this) */
			for (candidate = 0; candidate < peer->candidate_count; candidate++)
				peer_ping(peer, &peer->candidates[candidate]);
			/* (and through each relay paired for it) */
			{
				int attempts[P2P_RELAY_PEER_RELAYS];
				int count = p2p_relay_peer_attempts(peer->identifier, attempts, P2P_RELAY_PEER_RELAYS);
				struct p2p_relay_attempt_state attempt;

				for (candidate = 0; candidate < count; candidate++)
				{
					if (p2p_relay_attempt(attempts[candidate], &attempt) && attempt.paired)
						peer_ping_via(peer, NULL, attempts[candidate] + 1);
				}
			}
			peer->sent_time = p2p_now();
			peer->punch_rounds++;
		}
	}
}

/* ---------- STUN (RFC 5389): this machine's public address */

static void stun_send(struct stun_server *server)
{
	unsigned char request[20];
	struct sockaddr_in address;

	if (!server->address)
		return;
	memset(request, 0, sizeof(request));
	request[1] = 0x01; /* binding request */
	request[4] = 0x21; request[5] = 0x12; request[6] = 0xA4; request[7] = 0x42;
	/* a retry is the same transaction (RFC 5389), so a late answer to an
	earlier one is taken */
	if (!server->attempts)
		posix_random_bytes(server->transaction, sizeof(server->transaction));
	memcpy(request + 8, server->transaction, sizeof(server->transaction));
	make_address(&address, server->address, server->port);
	posix_socket_sendto(p2p.tunnel_socket, request, sizeof(request), 0, &address, sizeof(address));
	server->sent_time = p2p_now();
	server->attempts++;
}

static void stun_setup(void)
{
	const char *text = config_string("network.stun_servers");

	while (*text && p2p.stun_count < MAXIMUM_STUN_SERVERS)
	{
		const char *end = text + strcspn(text, ",");
		struct stun_server *server = &p2p.stun[p2p.stun_count];
		const char *colon;
		int length;

		while (text < end && *text == ' ')
			text++;
		length = (int)(end - text);
		while (length > 0 && text[length - 1] == ' ')
			length--;
		if (length > 0 && length < (int)sizeof(server->host))
		{
			memset(server, 0, sizeof(*server));
			memcpy(server->host, text, (size_t)length);
			colon = strchr(server->host, ':');
			server->port = network_short(3478);
			if (colon)
			{
				server->port = network_short((unsigned short)atoi(colon + 1));
				server->host[colon - server->host] = 0;
			}
			p2p.stun_count++;
		}
		text = *end ? end + 1 : end;
	}
}

/* network.public_address ("host[:port]"; the port the router forwards to
the tunnel's, else the tunnel's own), looked up as it changes and every
PUBLIC_ADDRESS_SECONDS: a host name's address may change. Under p2p_lock,
on the p2p thread */
static void public_address_update(void)
{
	enum { PUBLIC_ADDRESS_SECONDS = 300 };
	const char *setting = config_string("network.public_address");
	char host[256];
	const char *colon;
	unsigned long port = 0, address;

	if (!setting[0])
	{
		p2p.has_public = 0;
		p2p.public_text[0] = 0;
		return;
	}
	if (!strcmp(setting, p2p.public_text) && p2p.public_time &&
		!elapsed(p2p.public_time, PUBLIC_ADDRESS_SECONDS * 1000UL))
	{
		return;
	}
	snprintf(p2p.public_text, sizeof(p2p.public_text), "%s", setting);
	p2p.public_time = p2p_now() | 1;
	snprintf(host, sizeof(host), "%s", setting);
	/* (an IPv6 address, bracketed or not, is no candidate: the tunnel is
	IPv4's) */
	colon = strchr(host, ':');
	if (host[0] == '[' || (colon && strchr(colon + 1, ':')))
	{
		if (!p2p.public_logged)
			platform_log("Internet play: network.public_address %s is IPv6, which the tunnel does not use: left "
				"out", setting);
		p2p.public_logged = 1;
		p2p.has_public = 0;
		return;
	}
	if (colon)
	{
		char *end;

		port = strtoul(colon + 1, &end, 10);
		host[colon - host] = 0;
		if (*end || port == 0 || port > 65535)
		{
			if (!p2p.public_logged)
				platform_log("Internet play: network.public_address %s has no port 1 to 65535: left out", setting);
			p2p.public_logged = 1;
			p2p.has_public = 0;
			return;
		}
	}
	address = p2p_resolve(host);
	if (!address)
	{
		platform_log("Internet play: cannot look up network.public_address %s", host);
		p2p.has_public = 0;
		return;
	}
	p2p.public_candidate.address = address;
	p2p.public_candidate.port = port ? network_short((unsigned short)port) : p2p.tunnel_port;
	if (!p2p.has_public || !p2p.public_logged)
	{
		char text[64];

		platform_log("Internet play: joiners are offered network.public_address, %s",
			log_address_ipv4(p2p.public_candidate.address, p2p.public_candidate.port, text, sizeof(text)));
	}
	p2p.has_public = 1;
	p2p.public_logged = 1;
}

unsigned short p2p_tunnel_local_port(void)
{
	return p2p.tunnel_port;
}

/* (p2p.h) network.public_address as looked up, "a.b.c.d:port"; 0 if none */
int p2p_public_address(char *text, int size)
{
	int has;

	pthread_mutex_lock(&p2p_lock);
	has = p2p.has_public;
	if (has)
	{
		unsigned long address = network_long(p2p.public_candidate.address);
		unsigned short port = network_short(p2p.public_candidate.port);

		snprintf(text, (size_t)size, "%lu.%lu.%lu.%lu:%u", (address >> 24) & 255, (address >> 16) & 255,
			(address >> 8) & 255, address & 255, (unsigned)port);
	}
	pthread_mutex_unlock(&p2p_lock);
	return has;
}

static void stun_update(void)
{
	int index;

	if (!p2p.stun_started)
		return;
	for (index = 0; index < p2p.stun_count; index++)
	{
		struct stun_server *server = &p2p.stun[index];

		if (!server->address && !server->attempts)
		{
			/* looked up here on the p2p thread, until it is found */
			server->address = p2p_resolve(server->host);
			if (!server->address)
			{
				p2p_log("Internet play: cannot look up the STUN server %s", server->host);
				/* (looked up again a refresh's time from now, below: the
				network may not have been up yet) */
				server->attempts = STUN_ATTEMPTS;
				server->sent_time = p2p_now();
				continue;
			}
		}
		if (server->has_mapped)
		{
			/* also keeps the NAT's mapping of the tunnel alive */
			if (elapsed(server->sent_time, STUN_REFRESH_INTERVAL))
			{
				server->attempts = 0;
				stun_send(server);
			}
		}
		else if (server->attempts < STUN_ATTEMPTS && elapsed(server->sent_time, STUN_RETRY_INTERVAL))
		{
			stun_send(server);
		}
		else if (server->attempts >= STUN_ATTEMPTS && elapsed(server->sent_time, STUN_REFRESH_INTERVAL))
		{
			server->attempts = 0;
		}
	}
}

static void stun_received(const unsigned char *packet, int size, const struct sockaddr_in *from)
{
	int index;
	int offset;

	if (size < 20 || packet[0] != 0x01 || packet[1] != 0x01)
		return;
	/* the answer of a server asked, from it */
	for (index = 0; index < p2p.stun_count; index++)
	{
		struct stun_server const *server = &p2p.stun[index];

		if (server->address && server->address == from->sin_addr.s_addr && server->port == from->sin_port &&
			!memcmp(packet + 8, server->transaction, 12))
		{
			break;
		}
	}
	if (index == p2p.stun_count)
		return;
	for (offset = 20; offset + 4 <= size; )
	{
		int type = packet[offset] << 8 | packet[offset + 1];
		int length = packet[offset + 2] << 8 | packet[offset + 3];
		const unsigned char *value = packet + offset + 4;

		if (offset + 4 + length > size)
			break;
		/* XOR-MAPPED-ADDRESS, or MAPPED-ADDRESS from an old server; IPv4 */
		if ((type == 0x0020 || type == 0x0001) && length >= 8 && value[1] == 0x01)
		{
			struct stun_server *server = &p2p.stun[index];
			unsigned char port[2], ip[4];
			int byte;

			memcpy(port, value + 2, 2);
			memcpy(ip, value + 4, 4);
			if (type == 0x0020)
			{
				for (byte = 0; byte < 2; byte++)
					port[byte] ^= packet[4 + byte];
				for (byte = 0; byte < 4; byte++)
					ip[byte] ^= packet[4 + byte];
			}
			if (!server->has_mapped)
			{
				char text[LOG_ADDRESS_SIZE];
				int other;

				memcpy(&server->mapped.address, ip, 4);
				memcpy(&server->mapped.port, port, 2);
				server->has_mapped = 1;
				p2p_log("Internet play: this machine's public address is %s (from %s)",
					log_address_ipv4(server->mapped.address, server->mapped.port, text, sizeof(text)), server->host);
				for (other = 0; other < p2p.stun_count; other++)
				{
					if (other != index && p2p.stun[other].has_mapped &&
						p2p.stun[other].mapped.port != server->mapped.port && !p2p.reported_symmetric)
					{
						p2p_log("Internet play: this network's NAT gives each destination its own "
							"port, so it can only connect to machines behind more lenient ones");
						p2p.reported_symmetric = 1;
					}
				}
			}
			else
			{
				memcpy(&server->mapped.address, ip, 4);
				memcpy(&server->mapped.port, port, 2);
			}
			break;
		}
		offset += 4 + ((length + 3) & ~3);
	}
}

int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count)
{
	unsigned long lan = posix_local_ipv4_address();
	int count = 0;
	int index;

	/* (network.public_address first: the operator's word for it) */
	if (p2p.has_public && count < maximum_count)
		candidates[count++] = p2p.public_candidate;
	/* (then the front's port, where this machine registered with one: a
	joiner whose NAT keeps it from this machine's own reaches it there) */
	if (count < maximum_count && p2p_front_public(&candidates[count]))
		count++;
	if (lan && count < maximum_count)
	{
		candidates[count].address = lan;
		candidates[count++].port = p2p.tunnel_port;
	}
	/* (the port the router forwards here, UPnP; STUN's addresses are offered
	too, even where the router names another: then either may be the one
	that works, and a machine on the LAN that answers as the router cannot
	take the real address's place) */
	if (p2p.upnp_forwarded && count < maximum_count)
		candidates[count++] = p2p.upnp_candidate;
	for (index = 0; index < p2p.stun_count && count < maximum_count; index++)
	{
		int known;

		if (!p2p.stun[index].has_mapped)
			continue;
		for (known = 0; known < count; known++)
		{
			if (candidates[known].address == p2p.stun[index].mapped.address &&
				candidates[known].port == p2p.stun[index].mapped.port)
				break;
		}
		if (known == count)
			candidates[count++] = p2p.stun[index].mapped;
	}
	/* two copies of the game on one machine without a network */
	if (!count && maximum_count)
	{
		candidates[count].address = network_long(0x7F000001);
		candidates[count++].port = p2p.tunnel_port;
	}
	return count;
}

static int stun_settled(void);

/* whether an offer can name this machine's public address: one server
told it (the others' only matter where the NAT is strict, and a request
repeats with them as they come), or none will */
static int stun_ready(void)
{
	int index;

	for (index = 0; index < p2p.stun_count; index++)
	{
		if (p2p.stun[index].has_mapped)
			return 1;
	}
	return stun_settled();
}

static int stun_settled(void)
{
	int index;

	for (index = 0; index < p2p.stun_count; index++)
	{
		if (!p2p.stun[index].has_mapped && p2p.stun[index].attempts < STUN_ATTEMPTS)
			return 0;
	}
	return 1;
}

/* the p2p thread was stopped for longer than a peer lasts: the process was
suspended (a phone app in the background) or the machine slept (a laptop).
The NAT's mapping of the tunnel may have lapsed, so this machine's public
address is asked again before an offer names it, and the brokers'
connections, which the network may have dropped, are made anew rather than
at their silence timeout */
static void network_resumed(unsigned long stopped)
{
	int index;

	for (index = 0; index < p2p.stun_count; index++)
	{
		p2p.stun[index].has_mapped = 0;
		p2p.stun[index].attempts = 0;
	}
	p2p_signal_reconnect();
	p2p_log("Internet play: the game was stopped for %lu seconds; its public address is asked again and "
		"the brokers reconnected", stopped / 1000);
}

/* ---------- stand-ins for peers' ports */

static void close_socket(int *socket)
{
	if (*socket >= 0)
		posix_socket_close(*socket);
	*socket = -1;
}

/* a stand-in of a peer's port closes: traffic from it that the game has not
read yet still comes from the peer (p2p_incoming) */
static void remember_closed(int stream, unsigned short local_port, int peer_index, unsigned short remote_port)
{
	struct closed_port *closed = &p2p.closed[p2p.closed_next++ % MAXIMUM_CLOSED_PORTS];

	closed->stream = stream;
	closed->local_port = local_port;
	closed->virtual_address = p2p.peers[peer_index].virtual_address;
	closed->remote_port = remote_port;
	closed->time = p2p_now();
}

/* the peer's address and port for a stand-in closed lately; 0 if none */
static int find_closed(int stream, unsigned long *address, unsigned short *port)
{
	int index;

	for (index = 0; index < MAXIMUM_CLOSED_PORTS; index++)
	{
		struct closed_port const *closed = &p2p.closed[index];

		/* (not for long: the system gives the port to other sockets again) */
		if (closed->local_port == *port && closed->stream == stream && closed->virtual_address &&
			!elapsed(closed->time, CLOSED_PORT_TIME))
		{
			*address = closed->virtual_address;
			*port = closed->remote_port;
			return 1;
		}
	}
	return 0;
}

/* a stand-in, or a socket of the game's, that has the port now: no closed
stand-in stands for it */
static void forget_closed(int stream, unsigned short local_port)
{
	int index;

	for (index = 0; index < MAXIMUM_CLOSED_PORTS; index++)
	{
		if (p2p.closed[index].local_port == local_port && p2p.closed[index].stream == stream)
			p2p.closed[index].virtual_address = 0;
	}
}

static void proxy_close(struct proxy *proxy)
{
	struct peer *peer = &p2p.peers[proxy->peer];
	int index = (int)(proxy - p2p.proxies);
	int entry;

	if (proxy->socket < 0)
		return;
	close_socket(&proxy->socket);
	proxy_by_port[proxy->local_port] = 0;
	remember_closed(0, proxy->local_port, proxy->peer, proxy->remote_port);
	for (entry = 0; entry < peer->proxy_count; entry++)
	{
		if (peer->proxies[entry] == index)
		{
			peer->proxies[entry] = peer->proxies[--peer->proxy_count];
			break;
		}
	}
}

static void listener_close(struct listener *listener)
{
	if (listener->socket < 0)
		return;
	close_socket(&listener->socket);
	remember_closed(1, listener->local_port, listener->peer, listener->remote_port);
}

static struct proxy *find_proxy(int peer_index, unsigned short remote_port, int create)
{
	struct peer *peer = &p2p.peers[peer_index];
	struct proxy *free_proxy = NULL;
	struct proxy *oldest = NULL;
	int index;

	/* (a peer has only a few) */
	for (index = 0; index < peer->proxy_count; index++)
	{
		struct proxy *proxy = &p2p.proxies[peer->proxies[index]];

		if (proxy->remote_port == remote_port)
		{
			proxy->used_time = p2p_now();
			return proxy;
		}
		if (proxy->pinned_socket < 0 && (!oldest || (long)(proxy->used_time - oldest->used_time) < 0))
			oldest = proxy;
	}
	if (!create)
		return NULL;
	/* past a peer's few, its least used goes: no peer takes them all */
	if (peer->proxy_count >= MAXIMUM_PEER_PROXIES)
	{
		if (!oldest || !elapsed(oldest->used_time, PROXY_REPLACE_TIME))
			return NULL;
		proxy_close(oldest);
		free_proxy = oldest;
	}
	for (index = 0; index < MAXIMUM_PROXIES && !free_proxy; index++)
	{
		if (p2p.proxies[index].socket < 0)
			free_proxy = &p2p.proxies[index];
	}
	if (!free_proxy)
		return NULL;
	free_proxy->socket = open_socket(SOCK_DGRAM, p2p.local_address, 0, &free_proxy->local_port);
	if (free_proxy->socket < 0)
		return NULL;
	free_proxy->peer = peer_index;
	free_proxy->remote_port = remote_port;
	free_proxy->used_time = p2p_now();
	free_proxy->pinned_socket = -1;
	peer->proxies[peer->proxy_count++] = (short)(free_proxy - p2p.proxies);
	proxy_by_port[free_proxy->local_port] = (unsigned short)(free_proxy - p2p.proxies + 1);
	forget_closed(0, free_proxy->local_port);
	return free_proxy;
}

/* stand-ins unused for a while go (a peer's ports change with its game's
sockets) */
static void expire_proxies(void)
{
	int index;

	for (index = 0; index < MAXIMUM_PROXIES; index++)
	{
		struct proxy *proxy = &p2p.proxies[index];

		if (proxy->socket >= 0 && proxy->pinned_socket < 0 && elapsed(proxy->used_time, PROXY_IDLE_TIME))
			proxy_close(proxy);
	}
}

static struct listener *find_listener(int peer_index, unsigned short remote_port)
{
	struct listener *free_listener = NULL;
	int index;

	for (index = 0; index < MAXIMUM_LISTENERS; index++)
	{
		struct listener *listener = &p2p.listeners[index];

		if (listener->socket < 0)
		{
			if (!free_listener)
				free_listener = listener;
		}
		else if (listener->peer == peer_index && listener->remote_port == remote_port)
			return listener;
	}
	if (!free_listener)
		return NULL;
	free_listener->socket = open_socket(SOCK_STREAM, p2p.local_address, 0, &free_listener->local_port);
	if (free_listener->socket < 0)
		return NULL;
	if (posix_socket_listen(free_listener->socket, 8) < 0)
	{
		close_socket(&free_listener->socket);
		return NULL;
	}
	free_listener->peer = peer_index;
	free_listener->remote_port = remote_port;
	forget_closed(1, free_listener->local_port);
	return free_listener;
}

int p2p_outgoing(int stream, int socket, unsigned long *address, unsigned short *port)
{
	struct peer *peer;
	int result = 0;

	if (!is_virtual_address(*address) || !p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(*address);
	/* a peer's, or one that was: never to the address itself (100.64.0.0/10
	is also a carrier's NAT's and some VPNs', which the game's traffic is not
	for) */
	if (!peer)
		result = address_retired(*address) ? -1 : 0;
	else if (!peer->connected)
		result = -1;
	else if (stream)
	{
		struct listener *listener = find_listener((int)(peer - p2p.peers), *port);

		result = -1;
		if (listener)
		{
			*address = p2p.local_address;
			*port = listener->local_port;
			result = 1;
		}
	}
	else
	{
		struct proxy *proxy = find_proxy((int)(peer - p2p.peers), *port, 1);

		result = -1;
		if (proxy)
		{
			*address = p2p.local_address;
			*port = proxy->local_port;
			if (socket >= 0)
				proxy->pinned_socket = socket;
			result = 1;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_incoming(int stream, unsigned long *address, unsigned short *port)
{
	int result = 0;
	int index;

	if (!p2p.running || *address != p2p.local_address)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	if (stream)
	{
		for (index = 0; index < MAXIMUM_LISTENERS && !result; index++)
		{
			struct listener *listener = &p2p.listeners[index];

			if (listener->socket >= 0 && listener->local_port == *port)
			{
				*address = p2p.peers[listener->peer].virtual_address;
				*port = listener->remote_port;
				result = 1;
			}
		}
		for (index = 0; index < MAXIMUM_STREAMS && !result; index++)
		{
			struct stream *entry = &p2p.streams[index];

			if (entry->used && entry->local_port && entry->local_port == *port)
			{
				*address = p2p.peers[entry->peer].virtual_address;
				*port = entry->remote_port;
				result = 1;
			}
		}
	}
	else if (proxy_by_port[*port])
	{
		struct proxy const *proxy = &p2p.proxies[proxy_by_port[*port] - 1];

		*address = p2p.peers[proxy->peer].virtual_address;
		*port = proxy->remote_port;
		result = 1;
	}
	/* one that closed since (a peer's stand-ins past its few, a stream
	ended before the game took its connection) is still the peer's: as
	127.0.0.1 it would be this machine's own */
	if (!result)
		result = find_closed(stream, address, port);
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum_count)
{
	int count = 0;
	int index;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < P2P_MAXIMUM_PEERS && count < maximum_count; index++)
	{
		struct proxy *proxy;

		if (!p2p.peers[index].used || !p2p.peers[index].connected || p2p.peers[index].probe)
			continue;
		proxy = find_proxy(index, port, 1);
		if (proxy)
		{
			addresses[count] = p2p.local_address;
			ports[count++] = proxy->local_port;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return count;
}

static void datagram_send(struct peer *peer, unsigned short source_port, unsigned short port, const void *data,
	int size);

int p2p_send_datagram(unsigned short source_port, unsigned long address, unsigned short port, const void *data,
	int size)
{
	struct peer *peer;
	int result;

	if (!is_virtual_address(address) || !p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(address);
	if (!peer)
		result = address_retired(address) ? -1 : 0;
	else if (!peer->connected)
		result = -1;
	else
	{
		datagram_send(peer, source_port, port, data, size);
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_broadcast_datagram(unsigned short source_port, unsigned short port, const void *data, int size)
{
	int count = 0;
	int index;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		if (p2p.peers[index].used && p2p.peers[index].connected)
		{
			datagram_send(&p2p.peers[index], source_port, port, data, size);
			count++;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return count;
}

int p2p_peer_address(const unsigned char *peer_identifier, unsigned long *address)
{
	struct peer *peer;
	int result = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	/* reached or not (until it is, sending there fails: p2p_outgoing) */
	peer = find_peer(peer_identifier);
	if (peer && !peer->probe)
	{
		*address = peer->virtual_address;
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_peer_ping(const unsigned char *peer_identifier)
{
	struct peer *peer;
	int ping = -1;

	if (!p2p.running)
		return -1;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer(peer_identifier);
	if (peer && peer->connected && !peer->probe && peer->pongs)
		ping = (int)peer->round_trip;
	pthread_mutex_unlock(&p2p_lock);
	return ping;
}

/* ---------- the game's ports: all that peers may reach */

void p2p_socket_port(int socket, int stream, int listening, unsigned short port)
{
	struct game_port *entry = NULL;
	int index;

	if (!p2p.running || !port)
		return;
	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < MAXIMUM_GAME_PORTS; index++)
	{
		struct game_port *known = &p2p.game_ports[index];

		if (known->port && known->socket == socket)
		{
			entry = known;
			break;
		}
		if (!known->port && !entry)
			entry = known;
	}
	if (entry)
	{
		entry->socket = socket;
		entry->port = port;
		entry->stream = (unsigned char)(stream != 0);
		entry->listening |= (unsigned char)(listening != 0);
	}
	/* the game listens for connections while it hosts (on any port but map
	sharing's, which every player of a game listens on) */
	if (stream && listening && port != network_short(P2P_MAP_SHARE_PORT))
		p2p.hosting_socket = socket;
	/* (and no stand-in that closed has the port now) */
	forget_closed(stream != 0, port);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_port_taken(int stream, unsigned short port)
{
	if (!p2p.running || !port)
		return;
	pthread_mutex_lock(&p2p_lock);
	forget_closed(stream != 0, port);
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_socket_closed(int socket, unsigned short datagram_port)
{
	int index;

	if (!p2p.running)
		return;
	pthread_mutex_lock(&p2p_lock);
	/* (a port it sent peers datagrams from, which the system gives again) */
	for (index = 0; index < MAXIMUM_SENT_PORTS && datagram_port; index++)
	{
		if (p2p.sent_ports[index] == datagram_port)
			p2p.sent_ports[index] = 0;
	}
	for (index = 0; index < MAXIMUM_GAME_PORTS; index++)
	{
		if (p2p.game_ports[index].port && p2p.game_ports[index].socket == socket)
			memset(&p2p.game_ports[index], 0, sizeof(p2p.game_ports[index]));
	}
	for (index = 0; index < MAXIMUM_PROXIES; index++)
	{
		if (p2p.proxies[index].socket >= 0 && p2p.proxies[index].pinned_socket == socket)
			p2p.proxies[index].pinned_socket = -1;
	}
	if (p2p.hosting_socket == socket)
		p2p.hosting_socket = -1;
	pthread_mutex_unlock(&p2p_lock);
}

/* whether a peer may reach this port of the game's: one a socket of the
game's listens on (stream), or a datagram socket's, bound or sent from to a
peer */
static int game_port_open(int stream, unsigned short port)
{
	int index;

	for (index = 0; index < MAXIMUM_GAME_PORTS; index++)
	{
		struct game_port const *entry = &p2p.game_ports[index];

		if (entry->port == port && entry->stream == (stream != 0) && (!stream || entry->listening))
			return 1;
	}
	for (index = 0; index < MAXIMUM_SENT_PORTS && !stream; index++)
	{
		if (port && p2p.sent_ports[index] == port)
			return 1;
	}
	return 0;
}

/* ---------- datagrams */

/* a datagram from the game's port source_port to the peer's port */
static void datagram_send(struct peer *peer, unsigned short source_port, unsigned short port, const void *data,
	int size)
{
	unsigned char inner[MAXIMUM_INNER_SIZE];

	if (size < 0 || size > MAXIMUM_INNER_SIZE - 5)
		return;
	/* (the peer answers to that port) */
	if (!game_port_open(0, source_port))
		p2p.sent_ports[p2p.sent_port_next++ % MAXIMUM_SENT_PORTS] = source_port;
	inner[0] = _packet_datagram;
	put_short(inner + 1, source_port);
	put_short(inner + 3, port);
	memcpy(inner + 5, data, (size_t)size);
	peer_send(peer, inner, size + 5);
}

/* a datagram from the game to a peer, through its stand-in (from a socket
connected to it, or not bound yet: p2p_send_datagram sends the rest) */
static void proxy_readable(struct proxy *proxy)
{
	unsigned char data[MAXIMUM_INNER_SIZE - 5];
	int count;

	for (count = 0; count < 64; count++)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int size = posix_socket_recvfrom(proxy->socket, data, sizeof(data), 0, &from, &from_length);

		if (size < 0)
			break;
		/* only the game's own sockets use a stand-in */
		if (from.sin_addr.s_addr != p2p.local_address && from.sin_addr.s_addr != network_long(0x7F000001))
			continue;
		proxy->used_time = p2p_now();
		datagram_send(&p2p.peers[proxy->peer], from.sin_port, proxy->remote_port, data, size);
	}
}

/* a datagram from a peer to the game */
static void datagram_received(struct peer *peer, const unsigned char *inner, int size)
{
	struct proxy *proxy;
	struct sockaddr_in to;

	/* only to the game, from a peer of its */
	if (size < 5 || peer->probe || !game_port_open(0, get_short(inner + 3)))
		return;
	proxy = find_proxy((int)(peer - p2p.peers), get_short(inner + 1), 1);
	if (!proxy)
		return;
	make_address(&to, p2p.local_address, get_short(inner + 3));
	posix_socket_sendto(proxy->socket, inner + 5, size - 5, 0, &to, sizeof(to));
}

/* ---------- streams */

static int kcp_output(const char *buffer, int size, ikcpcb *kcp, void *user)
{
	struct stream *stream = user;
	unsigned char inner[MAXIMUM_INNER_SIZE];

	(void)kcp;
	if (size + 1 > (int)sizeof(inner))
		return -1;
	inner[0] = _packet_stream;
	memcpy(inner + 1, buffer, (size_t)size);
	peer_send(&p2p.peers[stream->peer], inner, size + 1);
	return 0;
}

static struct stream *stream_new(int peer_index, IUINT32 conversation)
{
	int index;

	for (index = 0; index < MAXIMUM_STREAMS; index++)
	{
		struct stream *stream = &p2p.streams[index];

		if (stream->used)
			continue;
		/* all but the buffer, which pending_size (after it) says is empty */
		memset(stream, 0, offsetof(struct stream, pending));
		stream->pending_size = 0;
		stream->kcp = ikcp_create(conversation, stream);
		if (!stream->kcp)
			return NULL;
		stream->used = 1;
		stream->peer = peer_index;
		stream->conversation = conversation;
		stream->socket = -1;
		stream->created_time = p2p_now();
		ikcp_setoutput(stream->kcp, kcp_output);
		ikcp_nodelay(stream->kcp, 1, LOOP_INTERVAL, 2, 1);
		ikcp_wndsize(stream->kcp, 256, 256);
		ikcp_setmtu(stream->kcp, KCP_MTU);
		return stream;
	}
	return NULL;
}

static void stream_free(struct stream *stream)
{
	/* (the game may not have taken the connection made for it yet) */
	if (stream->local_port)
		remember_closed(1, stream->local_port, stream->peer, stream->remote_port);
	close_socket(&stream->socket);
	if (stream->kcp)
		ikcp_release(stream->kcp);
	p2p.finished[p2p.finished_next++ % 16] = stream->conversation;
	memset(stream, 0, offsetof(struct stream, pending));
	stream->pending_size = 0;
	stream->socket = -1;
}

static void stream_message(struct stream *stream, unsigned char type, const void *data, int size)
{
	unsigned char message[1 + STREAM_CHUNK_SIZE];

	message[0] = type;
	memcpy(message + 1, data, (size_t)size);
	ikcp_send(stream->kcp, (const char *)message, size + 1);
	stream->flush = 1;
}

static void stream_local_closed(struct stream *stream)
{
	close_socket(&stream->socket);
	/* (its port the system gives again now, not when the stream is let go
	once the peer has all it sent: remembered from now, and no longer the
	peer's while the stream lingers) */
	if (stream->local_port)
	{
		remember_closed(1, stream->local_port, stream->peer, stream->remote_port);
		stream->local_port = 0;
	}
	if (!stream->local_closed)
	{
		stream->local_closed = 1;
		stream->closed_time = p2p_now();
		if (!stream->remote_closed)
			stream_message(stream, _stream_close, NULL, 0);
	}
}

/* the game connected to a stand-in listener */
static void listener_readable(struct listener *listener)
{
	for (;;)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int socket = posix_socket_accept(listener->socket, &from, &from_length);
		struct stream *stream;
		IUINT32 conversation;
		unsigned char open[4];

		if (socket < 0)
			return;
		/* only the game's own sockets use a stand-in (with network.address
		a LAN address, other machines could reach it) */
		if (from.sin_addr.s_addr != p2p.local_address && from.sin_addr.s_addr != network_long(0x7F000001))
		{
			posix_socket_close(socket);
			continue;
		}
		posix_socket_set_nonblocking(socket, 1);
		posix_socket_set_nodelay(socket);
		posix_random_bytes(&conversation, sizeof(conversation));
		stream = stream_new(listener->peer, conversation | 1);
		if (!stream)
		{
			posix_socket_close(socket);
			continue;
		}
		stream->socket = socket;
		stream->state = _stream_open_state;
		put_short(open, listener->remote_port);
		put_short(open + 2, from.sin_port);
		stream_message(stream, _stream_open, open, sizeof(open));
	}
}

/* a peer's connection opened: connect to the game for it */
static void stream_opened(struct stream *stream, const unsigned char *data, int size)
{
	struct sockaddr_in to;

	if (stream->state != _stream_awaiting_open || size < 4)
		return;
	stream->remote_port = get_short(data + 2);
	/* only to where the game listens: nothing else here is the peer's to
	reach */
	if (game_port_open(1, get_short(data)))
	{
		stream->socket = open_socket(SOCK_STREAM, p2p.local_address, 0, &stream->local_port);
		if (stream->socket >= 0)
			forget_closed(1, stream->local_port);
	}
	make_address(&to, p2p.local_address, get_short(data));
	if (stream->socket < 0 || (posix_socket_connect(stream->socket, &to, sizeof(to)) < 0 && !would_block()))
	{
		stream->local_port = 0;
		stream_local_closed(stream);
		return;
	}
	stream->state = _stream_connecting;
}

static void stream_received(struct peer *peer, const unsigned char *data, int size)
{
	int peer_index = (int)(peer - p2p.peers);
	IUINT32 conversation;
	struct stream *stream = NULL;
	int count = 0, opening = 0;
	int index;

	if (size < 24 || peer->probe)
		return;
	conversation = ikcp_getconv(data);
	for (index = 0; index < MAXIMUM_STREAMS && !stream; index++)
	{
		struct stream *entry = &p2p.streams[index];

		if (!entry->used || entry->peer != peer_index)
			continue;
		if (entry->conversation == conversation)
			stream = entry;
		count++;
		opening += entry->state == _stream_awaiting_open;
	}
	if (!stream)
	{
		for (index = 0; index < 16; index++)
		{
			if (p2p.finished[index] == conversation)
				return;
		}
		/* a peer has a few at a time: no peer takes them all */
		if (count >= MAXIMUM_PEER_STREAMS || opening >= MAXIMUM_PEER_OPENING_STREAMS)
			return;
		stream = stream_new(peer_index, conversation);
		if (!stream)
			return;
		stream->state = _stream_awaiting_open;
	}
	ikcp_input(stream->kcp, (const char *)data, size);
	stream->flush = 1;
}

static void stream_flush_pending(struct stream *stream)
{
	while (stream->pending_size > 0 && stream->socket >= 0 && stream->state == _stream_open_state)
	{
		int sent = posix_socket_send(stream->socket, stream->pending, stream->pending_size, 0);

		if (sent < 0)
		{
			if (!would_block())
				stream_local_closed(stream);
			return;
		}
		memmove(stream->pending, stream->pending + sent, (size_t)(stream->pending_size - sent));
		stream->pending_size -= sent;
	}
}

static void stream_update(struct stream *stream)
{
	unsigned char message[1 + STREAM_CHUNK_SIZE];

	ikcp_update(stream->kcp, p2p_now());
	/* what the peer sent */
	for (;;)
	{
		int size = ikcp_peeksize(stream->kcp);

		if (size > (int)sizeof(message))
		{
			/* larger than any this sends: nothing behind it would get
			through */
			stream_local_closed(stream);
			stream->remote_closed = 1;
			break;
		}
		if (size <= 0)
			break;
		if (size - 1 > STREAM_BUFFER_SIZE - stream->pending_size)
			break;
		size = ikcp_recv(stream->kcp, (char *)message, size);
		if (size <= 0)
			break;
		switch (message[0])
		{
		case _stream_open:
			stream_opened(stream, message + 1, size - 1);
			break;
		case _stream_data:
			if (stream->socket >= 0)
			{
				memcpy(stream->pending + stream->pending_size, message + 1, (size_t)(size - 1));
				stream->pending_size += size - 1;
			}
			break;
		case _stream_close:
			stream->remote_closed = 1;
			if (!stream->closed_time)
				stream->closed_time = p2p_now();
			break;
		}
	}
	stream_flush_pending(stream);
	if (stream->remote_closed && !stream->pending_size && stream->socket >= 0)
		stream_local_closed(stream);
	/* (what the game wrote and the acknowledgements of what came, sent now:
	KCP sends only as its interval passes, which held each reliable message
	up to LOOP_INTERVAL on each machine it crossed, and the peer's resending
	timed from its later acknowledgement. Once a pass, after every read, so
	that a burst goes as few datagrams as it fills) */
	if (stream->flush)
	{
		stream->flush = 0;
		ikcp_flush(stream->kcp);
	}
	/* finished: both ends closed and everything delivered, or the tunnel
	gave up on it */
	if ((stream->local_closed && stream->remote_closed && !ikcp_waitsnd(stream->kcp)) ||
		(stream->closed_time && elapsed(stream->closed_time, STREAM_LINGER_TIME)) ||
		(stream->state == _stream_awaiting_open && elapsed(stream->created_time, STREAM_LINGER_TIME)) ||
		stream->kcp->state == (IUINT32)-1)
	{
		stream_free(stream);
	}
}

static void stream_readable(struct stream *stream)
{
	unsigned char buffer[STREAM_CHUNK_SIZE];
	int count;

	for (count = 0; count < 8 && ikcp_waitsnd(stream->kcp) < STREAM_WINDOW && stream->socket >= 0; count++)
	{
		int size = posix_socket_recv(stream->socket, buffer, sizeof(buffer), 0);

		if (size == 0 || (size < 0 && !would_block()))
		{
			stream_local_closed(stream);
			return;
		}
		if (size < 0)
			return;
		stream_message(stream, _stream_data, buffer, size);
	}
}

static void stream_writeable(struct stream *stream)
{
	if (stream->state == _stream_connecting)
		stream->state = _stream_open_state;
	stream_flush_pending(stream);
}

/* ---------- the tunnel */

/* relay: it came through a relay's attempt (its index + 1; from is the
relay's address), else 0. Returns 1 if it was a peer's tunnel packet, its
seal good */
static int tunnel_received(unsigned char *packet, int size, const struct sockaddr_in *from, int relay)
{
	unsigned char inner[MAXIMUM_INNER_SIZE];
	unsigned char nonce[P2P_NONCE_SIZE];
	unsigned long long counter;
	struct peer *peer;
	int inner_size;
	int newest;

	/* (the magic first: a tunnel packet's bytes 4 to 7, of the sender and
	its number, can be STUN's magic cookie) */
	if (size < 1 || packet[0] != TUNNEL_MAGIC)
	{
		unsigned char answer[128];
		int answer_size;

		/* (a master server's probe of a dedicated server's port, answered
		to where it came from: server_master.c) */
		if (server_master_probe(packet, size, from->sin_addr.s_addr, answer, &answer_size))
			posix_socket_sendto(p2p.tunnel_socket, answer, answer_size, 0, from, sizeof(*from));
		/* (a relay's: never one a relay brought) */
		else if (!relay && size >= 1 && packet[0] == P2P_RELAY_MAGIC)
		{
			relay_corrupt(packet, &size);
			p2p_relay_received(packet, size, from->sin_addr.s_addr, from->sin_port, p2p_now());
		}
		/* (a front's: this machine's registration, and its clients'
		packets) */
		else if (!relay && size >= 1 && packet[0] == P2P_FRONT_MAGIC)
		{
			relay_corrupt(packet, &size);
			p2p_front_received(packet, size, from->sin_addr.s_addr, from->sin_port, p2p_now());
		}
		else if (size >= 20 && packet[4] == 0x21 && packet[5] == 0x12 && packet[6] == 0xA4 && packet[7] == 0x42)
			stun_received(packet, size, from);
		return 0;
	}
	if (size < TUNNEL_HEADER_SIZE + P2P_TAG_SIZE + 1 || size - TUNNEL_HEADER_SIZE - P2P_TAG_SIZE > (int)sizeof(inner))
		return 0;
	peer = find_peer(packet + 1);
	if (!peer)
		return 0;
	/* each packet once, sealed by the peer for this direction, its header
	and all */
	counter = packet_counter(packet);
	if (!packet_fresh(peer, counter))
		return 0;
	packet_nonce(packet, nonce);
	inner_size = p2p_aead_open(peer->receive_key, nonce, packet, TUNNEL_HEADER_SIZE, packet + TUNNEL_HEADER_SIZE,
		size - TUNNEL_HEADER_SIZE, inner);
	if (inner_size < 1)
		return 0;
	newest = counter > peer->receive_highest;
	packet_received(peer, counter);
	peer_heard(peer, from->sin_addr.s_addr, from->sin_port, newest, relay);
	switch (inner[0])
	{
	case _packet_ping:
		if (inner_size >= 5)
		{
			struct p2p_candidate to;

			/* (answered the way it came) */
			inner[0] = _packet_pong;
			to.address = from->sin_addr.s_addr;
			to.port = from->sin_port;
			peer_send_via(peer, &to, relay, inner, 5);
		}
		break;
	case _packet_pong:
		if (inner_size >= 5)
		{
			/* (4 bytes of the clock, as the ping carries) */
			unsigned int sent;

			memcpy(&sent, inner + 1, 4);
			peer->round_trip = (unsigned int)p2p_now() - sent;
			if (!peer->pongs++ || peer->round_trip < peer->shortest_round_trip)
				peer->shortest_round_trip = peer->round_trip;
		}
		break;
	case _packet_datagram:
		datagram_received(peer, inner, inner_size);
		break;
	case _packet_stream:
		stream_received(peer, inner + 1, inner_size - 1);
		break;
	case _packet_bye:
		drop_peer(peer, "left");
		break;
	case _packet_relays:
		relays_told_received(peer, inner, inner_size);
		break;
	}
	return 1;
}

static void tunnel_readable(void)
{
	unsigned char packet[2048];
	int count;

	for (count = 0; count < 256; count++)
	{
		struct sockaddr_in from;
		int from_length = sizeof(from);
		int size = posix_socket_recvfrom(p2p.tunnel_socket, packet, sizeof(packet), 0, &from, &from_length);

		if (size < 0)
			break;
		tunnel_received(packet, size, &from, 0);
	}
}

/* ---------- the server browser's probes of public games' hosts */

/* the probe of the host with this identifier, or -1 */
static int probe_index(const unsigned char *host)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PROBES; index++)
	{
		if (p2p.probes[index].used && !memcmp(p2p.probes[index].identifier, host, P2P_IDENTIFIER_SIZE))
			return index;
	}
	return -1;
}

/* a probe ends: its request, and its peer (unless a join took it up), go;
the browser is told the shortest round trip timed, if any */
static void probe_end(int index)
{
	unsigned char host[P2P_IDENTIFIER_SIZE];
	struct peer *peer;
	int ping;

	memcpy(host, p2p.probes[index].identifier, P2P_IDENTIFIER_SIZE);
	p2p.probes[index].used = 0;
	p2p_signal_stop_probe(host);
	peer = find_peer(host);
	ping = peer && peer->pongs ? (int)peer->shortest_round_trip : -1;
	if (peer && peer->probe)
		drop_peer(peer, "probed");
	p2p_lobby_probed(host, ping);
}

int p2p_probe_start(const unsigned char *host_hash, const unsigned char *token)
{
	unsigned char host[P2P_IDENTIFIER_SIZE];
	struct peer *peer;
	int free_index = -1;
	int index;

	p2p_identifier_from_hash(host_hash, host);
	peer = find_peer(host);
	/* (this machine's own game, and a host the tunnel reaches already: no
	tunnel to make) */
	if (!memcmp(host, identifier, P2P_IDENTIFIER_SIZE) || (peer && peer->connected && !peer->probe))
	{
		p2p_lobby_probed(host, peer && peer->pongs ? (int)peer->round_trip : -1);
		return 1;
	}
	if (probe_index(host) >= 0)
		return 1;
	/* (a session with it under way, or a join of it: their own) */
	if (peer || ((p2p.joining || p2p.join_requested) && !memcmp(host, p2p.join_host, P2P_IDENTIFIER_SIZE)))
		return 0;
	for (index = 0; index < P2P_MAXIMUM_PROBES && free_index < 0; index++)
	{
		if (!p2p.probes[index].used)
			free_index = index;
	}
	if (free_index < 0)
		return 0;
	/* (the request carries the public address) */
	p2p.stun_started = 1;
	p2p_signal_start();
	if (!stun_ready())
		return 0;
	if (!p2p_signal_probe(host_hash, token))
		return 0;
	p2p.probes[free_index].used = 1;
	memcpy(p2p.probes[free_index].identifier, host, P2P_IDENTIFIER_SIZE);
	p2p.probes[free_index].time = p2p_now();
	p2p.probes[free_index].reported = 0;
	return 1;
}

/* the probes timed (PROBE_PINGS answered) end, as do those not reached in
PROBE_TIMEOUT, and all of them once the browser closes */
static void update_probes(void)
{
	int browsing = p2p_lobby_browsing();
	int keep = browsing && p2p.keep.time && !elapsed(p2p.keep.time, PROBE_KEEP_TIME);
	int index;

	for (index = 0; index < P2P_MAXIMUM_PROBES; index++)
	{
		struct peer const *peer;
		int timed;

		if (!p2p.probes[index].used)
			continue;
		peer = find_peer(p2p.probes[index].identifier);
		timed = peer && peer->probe && peer->pongs >= PROBE_PINGS;
		/* (the game chosen: its ping told, its tunnel kept) */
		if (timed && keep && !memcmp(p2p.probes[index].identifier, p2p.keep.identifier, P2P_IDENTIFIER_SIZE))
		{
			if (!p2p.probes[index].reported)
			{
				p2p.probes[index].reported = 1;
				p2p_lobby_probed(p2p.probes[index].identifier, (int)peer->shortest_round_trip);
			}
			continue;
		}
		if (!browsing || timed || elapsed(p2p.probes[index].time, PROBE_TIMEOUT))
			probe_end(index);
	}
	/* the game chosen, its host not reached nor being reached: probed */
	if (keep && probe_index(p2p.keep.identifier) < 0 && !find_peer(p2p.keep.identifier) &&
		(!p2p.keep.started_time || elapsed(p2p.keep.started_time, PROBE_KEEP_RETRY)))
	{
		p2p.keep.started_time = p2p_now() | 1;
		p2p_probe_start(p2p.keep.hash, p2p.keep.token);
	}
}

/* a join of a host being probed: the probe's tunnel, if it reached the
host, is the join's (1); else the probe is given up, as the join's own
session could not be made beside its */
static int probe_taken_up(const unsigned char *host)
{
	int index = probe_index(host);
	struct peer *peer = find_peer(host);
	int taken = peer && peer->probe && peer->connected;

	if (taken)
		peer->probe = 0;
	if (index >= 0)
		probe_end(index);
	else if (peer && peer->probe)
		drop_peer(peer, "probe given up for a join");
	return taken;
}

/* ---------- invites */

/* the host's key hash and the token in an invite link or code within text:
1 if it holds one, -1 if it holds an older version's (with the host's
identifier alone, which a key made to have it could pass for), else 0 */
static int parse_invite(const char *text, unsigned char *host_hash, unsigned char *token)
{
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	const char *start = NULL;
	const char *search;
	int digits;
	int index;

	for (search = text; *search && !start; search++)
	{
		/* (a link, or an address with one: ?join=, or the join page's
		#join=, the part a browser never sends) */
		static const char *const prefixes[] = { P2P_JOIN_LINK_PREFIX, P2P_OLD_JOIN_LINK_PREFIX,
			"?join=", "&join=", "#join=" };
		int prefix;

		for (prefix = 0; prefix < (int)(sizeof(prefixes) / sizeof(*prefixes)) && !start; prefix++)
		{
			int length;

			for (length = 0; prefixes[prefix][length] && search[length] &&
				(search[length] | 0x20) == prefixes[prefix][length]; length++)
				;
			if (!prefixes[prefix][length])
				start = search + length;
		}
	}
	if (!start)
	{
		/* a bare code: the text is its digits alone, spaces around them
		allowed */
		start = text;
		while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
			start++;
		for (search = start; hex_value(*search) >= 0; search++)
			;
		for (; *search; search++)
		{
			if (*search != ' ' && *search != '\t' && *search != '\r' && *search != '\n')
				return 0;
		}
	}
	for (digits = 0; hex_value(start[digits]) >= 0; digits++)
		;
	if (digits == 2 * (P2P_IDENTIFIER_SIZE + P2P_TOKEN_SIZE))
		return -1;
	if (digits != (int)sizeof(bytes) * 2)
		return 0;
	for (index = 0; index < (int)sizeof(bytes); index++)
		bytes[index] = (unsigned char)(hex_value(start[index * 2]) << 4 | hex_value(start[index * 2 + 1]));
	memcpy(host_hash, bytes, P2P_KEY_HASH_SIZE);
	memcpy(token, bytes + P2P_KEY_HASH_SIZE, P2P_TOKEN_SIZE);
	return 1;
}

int p2p_parse_invite(const char *text, unsigned char *host_hash, unsigned char *token)
{
	return parse_invite(text, host_hash, token);
}

/* (p2p.h) */
int p2p_invite_message(const char *text, char *message, int size)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	char hex[2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1];

	/* (a party's link first: p2p_join_invite takes it so) */
	if (p2p_party_parse_link(text, token))
	{
		p2p_hex(token, P2P_TOKEN_SIZE, hex);
		snprintf(message, (size_t)size, "Join my OpenH1 party: %s#party=%s\n"
			"Or paste this into OpenH1's Join by Link: " P2P_PARTY_LINK_PREFIX "%s", P2P_JOIN_PAGE, hex, hex);
		return 1;
	}
	if (parse_invite(text, hash, token) <= 0)
		return 0;
	memcpy(bytes, hash, P2P_KEY_HASH_SIZE);
	memcpy(bytes + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
	p2p_hex(bytes, sizeof(bytes), hex);
	snprintf(message, (size_t)size, "Join my OpenH1 game: %s#join=%s\n"
		"Or paste this into OpenH1's Join by Link: " P2P_JOIN_LINK_PREFIX "%s", P2P_JOIN_PAGE, hex, hex);
	return 1;
}

void p2p_offer_clipboard(const char *text)
{
	snprintf(p2p.clipboard, sizeof(p2p.clipboard), "%s", text);
	p2p.has_clipboard = 1;
}

/* the host of the public game this machine joined from the server browser
(p2p_join_listing), till another invite is joined: its identifier */
static unsigned char listed_host[P2P_IDENTIFIER_SIZE];
static int listed_host_known;
/* the listing joined is a dedicated server's that a master server lists (an
approved operator's, by its key: a player's host cannot say it is one) */
static int listed_host_dedicated;
/* ... and the masters that listed it, by their key ids */
static unsigned char listed_host_masters[P2P_LISTING_MAXIMUM_MASTERS][P2P_LISTING_MASTER_KEY_ID_SIZE];
static int listed_host_master_count;

/* a new join's progress (p2p_join_progress): answered, its host reached
already (a probe's tunnel) */
static void join_progress_begin(int answered)
{
	p2p.join_started = 1;
	p2p.join_answered = answered;
	p2p.join_gave_up = 0;
	p2p.join_refused = -1;
}

/* under p2p_lock: parse_invite's result */
static int join_invite(const char *text)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], host[P2P_IDENTIFIER_SIZE], token[P2P_TOKEN_SIZE];
	struct peer *peer;
	int parsed = parse_invite(text, hash, token);

	if (parsed < 0)
		p2p_log("Internet play: that invite is from an older version of the game, which this one "
			"cannot join");
	if (parsed <= 0)
		return parsed;
	/* (any invite's game is no listed one, unless p2p_join_listing says) */
	listed_host_known = 0;
	listed_host_dedicated = 0;
	listed_host_master_count = 0;
	p2p_identifier_from_hash(hash, host);
	if (!memcmp(host, identifier, P2P_IDENTIFIER_SIZE))
		return 1;
	if (probe_taken_up(host))
	{
		int index;

		/* (a join of another game under way ends, and the last game's
		players are not this one's) */
		if (p2p.joining)
			p2p_signal_stop_joining();
		p2p.joining = p2p.join_requested = 0;
		for (index = 0; index < P2P_MAXIMUM_PLAYER_JOINS; index++)
		{
			if (p2p.reaching[index].used && p2p.reaching[index].asked)
				p2p_signal_stop_joining_player(p2p.reaching[index].identifier);
		}
		memset(p2p.reaching, 0, sizeof(p2p.reaching));
		memcpy(p2p.join_host, host, sizeof(host));
		memcpy(p2p.join_host_hash, hash, sizeof(hash));
		memcpy(p2p.join_token, token, sizeof(token));
		p2p.join_asked_time = p2p_now();
		p2p.join_reached = 1;
		join_progress_begin(1);
		p2p_log("Internet play: joining %s's game, reached already; it is listed under Multiplayer, "
			"System Link", find_peer(host)->name);
		return 1;
	}
	peer = find_peer(host);
	if (peer && peer->connected)
	{
		p2p_log("Internet play: already connected to that invite's host");
		return 1;
	}
	if ((p2p.joining || p2p.join_requested) && !memcmp(hash, p2p.join_host_hash, sizeof(hash)) &&
		!memcmp(token, p2p.join_token, sizeof(token)))
		return 1;
	memcpy(p2p.join_host, host, sizeof(host));
	memcpy(p2p.join_host_hash, hash, sizeof(hash));
	memcpy(p2p.join_token, token, sizeof(token));
	p2p.join_requested = 1;
	p2p.join_asked_time = p2p_now();
	p2p.join_reached = 0;
	join_progress_begin(0);
	return 1;
}

/* (p2p.h) */
const char *p2p_invite_log_text(const char *invite, char *text, int size)
{
	enum { SHOWN_DIGITS = 8 };
	const char *start = invite;
	const char *search;
	size_t kept;

	/* (after "openh1://join/", or another scheme's, if there is one) */
	for (search = invite; *search; search++)
	{
		if (!strncmp(search, "://", 3))
		{
			const char *slash = strchr(search + 3, '/');

			start = slash ? slash + 1 : search + 3;
			break;
		}
	}
	kept = (size_t)(start - invite) + SHOWN_DIGITS;
	if (strlen(invite) <= kept)
		snprintf(text, (size_t)size, "%s", invite);
	else
		snprintf(text, (size_t)size, "%.*s... (the rest is left out of the log)", (int)kept, invite);
	return text;
}

int p2p_join_invite(const char *text)
{
	int result;

	p2p_identifier();
	/* (a party's link joins the party: p2p_party.c) */
	if (p2p_party_join_link(text))
	{
		if (!p2p.running)
			p2p_log("Internet play is off (network.online in config.toml): the party's link is ignored");
		return 1;
	}
	pthread_mutex_lock(&p2p_lock);
	result = join_invite(text);
	pthread_mutex_unlock(&p2p_lock);
	if (result > 0 && !p2p.running)
		p2p_log("Internet play is off (network.online in config.toml): the invite is ignored");
	return result > 0;
}

int p2p_join_listing(const struct p2p_listing *listing)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];

	if (!p2p_join_invite(listing->invite))
		return 0;
	/* (its host as the invite names it, as the tunnel knows its peers) */
	pthread_mutex_lock(&p2p_lock);
	if (parse_invite(listing->invite, hash, token) > 0)
	{
		p2p_identifier_from_hash(hash, listed_host);
		listed_host_known = 1;
		listed_host_dedicated = listing->dedicated && listing->master;
		listed_host_master_count = listing->master ? listing->master_key_id_count : 0;
		if (listed_host_master_count > P2P_LISTING_MAXIMUM_MASTERS)
			listed_host_master_count = P2P_LISTING_MAXIMUM_MASTERS;
		memcpy(listed_host_masters, listing->master_key_ids, sizeof(listed_host_masters));
	}
	pthread_mutex_unlock(&p2p_lock);
	return 1;
}

int p2p_joined_listed_host(void)
{
	struct peer *peer;
	int reached;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = listed_host_known ? find_peer(listed_host) : NULL;
	reached = peer && peer->connected;
	pthread_mutex_unlock(&p2p_lock);
	return reached;
}

int p2p_joined_listed_dedicated(void)
{
	int dedicated;

	if (!p2p_joined_listed_host())
		return 0;
	pthread_mutex_lock(&p2p_lock);
	dedicated = listed_host_dedicated;
	pthread_mutex_unlock(&p2p_lock);
	return dedicated;
}

int p2p_joined_listing_masters(unsigned char (*key_ids)[P2P_LISTING_MASTER_KEY_ID_SIZE], int maximum)
{
	int count;

	pthread_mutex_lock(&p2p_lock);
	count = listed_host_known ? listed_host_master_count : 0;
	if (count > maximum)
		count = maximum;
	memcpy(key_ids, listed_host_masters, (size_t)count * P2P_LISTING_MASTER_KEY_ID_SIZE);
	pthread_mutex_unlock(&p2p_lock);
	return count;
}

int p2p_join_progress(int *stage, char *reason, int size)
{
	/* (the game's rejection codes in their order: network_game_protocol.h's
	version too old, too new, bad join token, bad password, full, closed,
	blacklisted machine, then the port's: a name refused, and a dedicated
	server's VPN policy's) */
	static const char *const refusals[] = { "version", "version", "token", "password", "full", "closed", "banned",
		"name", "vpn", "vpn_blocked", "link", "link_age", "link_in_use", "address_full" };
	struct peer *peer;
	const char *why = "";

	pthread_mutex_lock(&p2p_lock);
	peer = p2p.join_started ? find_peer(p2p.join_host) : NULL;
	*stage = P2P_JOIN_NONE;
	if (p2p.join_started && p2p.join_refused >= 0)
	{
		*stage = P2P_JOIN_FAILED;
		why = p2p.join_refused < (int)(sizeof(refusals) / sizeof(*refusals)) ? refusals[p2p.join_refused] : "closed";
		/* (a server that blocks VPNs, on a master this machine knows but holds
		no pass of: link Discord with that one, player_link.c) */
		if (!strcmp(why, "vpn") && player_link_needed_master() >= 0)
			why = "vpn_master";
	}
	else if (peer && peer->connected && peer->is_host && !peer->is_player)
		*stage = P2P_JOIN_CONNECTED;
	else if (p2p.joining || p2p.join_requested)
		*stage = peer || p2p.join_answered ? P2P_JOIN_ANSWERED : P2P_JOIN_ASKING;
	else if (p2p.join_started && p2p.join_gave_up)
	{
		*stage = P2P_JOIN_FAILED;
		why = p2p.join_answered ? "no_route" : "no_answer";
	}
	pthread_mutex_unlock(&p2p_lock);
	if (reason && size > 0)
		snprintf(reason, (size_t)size, "%s", why);
	return *stage;
}

void p2p_join_refused(int rejection_code)
{
	pthread_mutex_lock(&p2p_lock);
	if (p2p.join_started && rejection_code >= 0)
		p2p.join_refused = rejection_code;
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_invite_received(const char *text)
{
	/* (a party's link joins the party) */
	if (p2p_party_join_link_locked(text))
		return;
	/* (an older version's is logged as such) */
	if (!join_invite(text))
		p2p_log("Internet play: that is not an invite");
}

static void update_joining(void)
{
	char name[2 * P2P_IDENTIFIER_SIZE + 1];

	if (p2p.join_requested)
	{
		/* the offer carries the public address, if there is one; the
		brokers are connected to meanwhile (not after: their handshakes and
		STUN's answers take round trips each) */
		p2p.stun_started = 1;
		p2p_signal_start();
		if (!stun_ready())
			return;
		p2p.join_requested = 0;
		p2p.joining = 1;
		p2p.join_time = p2p_now();
		/* (the last game's players are not this one's) */
		memset(p2p.reaching, 0, sizeof(p2p.reaching));
		p2p_hex(p2p.join_host, P2P_IDENTIFIER_SIZE, name);
		p2p_log("Internet play: joining %s's game (%lu ms after it was asked for: this machine's public address "
			"first)", name, join_elapsed());
		p2p_signal_start();
		p2p_signal_join(p2p.join_host_hash, p2p.join_token);
	}
	else if (p2p.joining && elapsed(p2p.join_time, JOIN_TIMEOUT))
	{
		p2p.joining = 0;
		p2p.join_gave_up = 1;
		p2p_signal_stop_joining();
		p2p_log("Internet play: no answer from the invite's host; it may have stopped hosting or quit");
	}
}

/* the host of the game this machine joined, if the tunnel reaches it */
static struct peer *joined_host(void)
{
	struct peer *peer = find_peer(p2p.join_host);

	return peer && peer->connected && peer->is_host && !peer->is_player ? peer : NULL;
}

/* answering the other players of the game joined (with the token joined
with), as its host does, while asked and not hosting */
static void update_answering(void)
{
	int want = p2p.answer_players && !p2p.hosting && joined_host() != NULL;

	if (want && !p2p.answering_players)
	{
		p2p.answering_players = 1;
		p2p_signal_start();
		p2p_signal_host(p2p.join_token);
		p2p_log("Internet play: the game's other players may reach this machine (map sharing)");
	}
	else if (!want && p2p.answering_players)
	{
		p2p.answering_players = 0;
		/* (hosting took the topic over) */
		if (!p2p.hosting)
			p2p_signal_stop_hosting();
	}
}

/* the other players of the game joined that are asked for: until reached,
or PLAYER_REACH_TIMEOUT */
static void update_reaching(void)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PLAYER_JOINS; index++)
	{
		struct peer *peer;

		if (!p2p.reaching[index].used)
			continue;
		peer = find_peer(p2p.reaching[index].identifier);
		if ((peer && peer->connected) || elapsed(p2p.reaching[index].time, PLAYER_REACH_TIMEOUT) || !joined_host())
		{
			if (p2p.reaching[index].asked)
				p2p_signal_stop_joining_player(p2p.reaching[index].identifier);
			p2p.reaching[index].used = 0;
			continue;
		}
		/* (the offer carries the public address, if there is one) */
		if (!p2p.reaching[index].asked && stun_ready())
		{
			p2p_signal_start();
			if (!p2p_signal_join_player(p2p.reaching[index].hash, p2p.join_token))
			{
				p2p.reaching[index].used = 0;
				continue;
			}
			p2p.reaching[index].asked = 1;
		}
	}
}

static int connected_player_count(void)
{
	int count = 0;
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		count += p2p.peers[index].used && p2p.peers[index].connected && !p2p.peers[index].is_host;
	return count;
}

/* a new invite (a new token) */
static void make_invite(void)
{
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	char text[2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1];

	posix_random_bytes(p2p.token, sizeof(p2p.token));
	p2p.has_token = 1;
	p2p.token_listed = 0;
	p2p_key_hash(p2p_public_key(), bytes);
	memcpy(bytes + P2P_KEY_HASH_SIZE, p2p.token, P2P_TOKEN_SIZE);
	p2p_hex(bytes, sizeof(bytes), text);
	snprintf(p2p.invite, sizeof(p2p.invite), P2P_JOIN_LINK_PREFIX "%s", text);
}

void p2p_token_listed(const unsigned char *token)
{
	if (p2p.has_token && !memcmp(token, p2p.token, P2P_TOKEN_SIZE))
		p2p.token_listed = 1;
}

void p2p_new_invite_if_listed(void)
{
	if (!p2p.has_token || !p2p.token_listed)
		return;
	make_invite();
	{
		char shown[128];

		p2p_log("Internet play: a new invite, so that the one listed lets no one in (private, or a new password): %s",
			p2p_invite_log_text(p2p.invite, shown, sizeof(shown)));
	}
	if (p2p.hosting)
	{
		p2p_signal_host(p2p.token);
		memcpy(p2p.clipboard, p2p.invite, sizeof(p2p.clipboard));
		p2p.has_clipboard = 1;
		/* (Discord is told the new one) */
		p2p.reported_player_count = -1;
	}
}

/* the hosted game's players, as the game says; else the host and the
machines the tunnel reaches */
static void hosted_player_counts(int *count, int *maximum)
{
	*count = p2p.game_player_maximum > 0 ? p2p.game_player_count : connected_player_count() + 1;
	*maximum = p2p.game_player_maximum > 0 ? p2p.game_player_maximum : P2P_MAXIMUM_PEERS + 1;
}

static void update_hosting(void)
{
	int want = p2p.hosting_socket >= 0 && !p2p.hosting_lan_only;

	if (want && !p2p.hosting)
	{
		/* one invite for the whole run, so a link keeps working from game
		to game */
		if (!p2p.has_token)
			make_invite();
		p2p.hosting = 1;
		p2p.stun_started = 1;
		p2p_signal_start();
		p2p_signal_host(p2p.token);
		{
			char shown[128];

			/* (whole on the clipboard and in the menus) */
			p2p_log("Internet play: hosting. Invite players with this link (it only works while this "
				"copy of the game runs; it is on the clipboard): %s",
				p2p_invite_log_text(p2p.invite, shown, sizeof(shown)));
		}
		if (!p2p.invite_copied)
		{
			memcpy(p2p.clipboard, p2p.invite, sizeof(p2p.clipboard));
			p2p.has_clipboard = 1;
			p2p.invite_copied = 1;
		}
		p2p.reported_player_count = -1;
	}
	else if (!want && p2p.hosting)
	{
		p2p.hosting = 0;
		p2p_signal_stop_hosting();
		p2p_discord_set_hosting(NULL, 0, 0);
	}
	if (p2p.hosting)
	{
		int count, maximum;

		hosted_player_counts(&count, &maximum);
		if (count != p2p.reported_player_count || maximum != p2p.reported_player_maximum)
		{
			p2p.reported_player_count = count;
			p2p.reported_player_maximum = maximum;
			p2p_discord_set_hosting(p2p.invite + strlen(P2P_JOIN_LINK_PREFIX), count, maximum);
		}
	}
}

/* ---------- map sharing between the players of a game joined */

int p2p_peer_key_hash(unsigned long virtual_address, unsigned char *peer_identifier, unsigned char *key_hash)
{
	struct peer *peer;
	int result = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(virtual_address);
	if (peer)
	{
		memcpy(peer_identifier, peer->identifier, P2P_IDENTIFIER_SIZE);
		memcpy(key_hash, peer->key_hash, P2P_KEY_HASH_SIZE);
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

int p2p_peer_binding(unsigned long virtual_address, unsigned char *binding)
{
	struct peer *peer;
	int result = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer_by_address(virtual_address);
	if (peer)
	{
		player_link_binding(peer->secret, binding);
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

void p2p_set_answering_players(int on)
{
	/* (only the game's thread writes it: unchanged, it need not wait for the
	lock) */
	if (!p2p.running || !on == !p2p.answer_players)
		return;
	pthread_mutex_lock(&p2p_lock);
	p2p.answer_players = on != 0;
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_reach_player(const unsigned char *key_hash)
{
	unsigned char peer_identifier[P2P_IDENTIFIER_SIZE];
	int free_index = -1;
	int result = 0;
	int index;

	if (!p2p.running)
		return 0;
	p2p_identifier_from_hash(key_hash, peer_identifier);
	pthread_mutex_lock(&p2p_lock);
	if (!memcmp(peer_identifier, identifier, P2P_IDENTIFIER_SIZE) || !joined_host())
	{
		pthread_mutex_unlock(&p2p_lock);
		return 0;
	}
	/* (reached, or being reached: by its offer, or by the request below) */
	if (find_peer(peer_identifier))
		result = 1;
	for (index = 0; index < P2P_MAXIMUM_PLAYER_JOINS && !result; index++)
	{
		if (p2p.reaching[index].used && !memcmp(p2p.reaching[index].hash, key_hash, P2P_KEY_HASH_SIZE))
			result = 1;
		else if (!p2p.reaching[index].used && free_index < 0)
			free_index = index;
	}
	if (!result && free_index >= 0)
	{
		memset(&p2p.reaching[free_index], 0, sizeof(p2p.reaching[free_index]));
		p2p.reaching[free_index].used = 1;
		memcpy(p2p.reaching[free_index].hash, key_hash, P2P_KEY_HASH_SIZE);
		memcpy(p2p.reaching[free_index].identifier, peer_identifier, P2P_IDENTIFIER_SIZE);
		p2p.reaching[free_index].time = p2p_now();
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* whether a machine's addresses show a NAT that gives each destination its
own port: two public addresses alike but for the port (what two STUN servers
told it: one forwarded by UPnP would also be one) */
static int candidates_strict(const struct p2p_candidate *candidates, int count)
{
	int index, other;

	for (index = 0; index < count; index++)
	{
		unsigned long address = network_long(candidates[index].address);
		unsigned long first = address >> 24;

		/* (not a LAN's, CGNAT's, the loopback's or a link's) */
		if (first == 10 || first == 127 || (address >> 20) == 0xAC1 || (address >> 16) == 0xC0A8 ||
			(address >> 22) == (0x64400000UL >> 22) || (address >> 16) == 0xA9FE)
			continue;
		for (other = index + 1; other < count; other++)
		{
			if (candidates[other].address == candidates[index].address &&
				candidates[other].port != candidates[index].port)
				return 1;
		}
	}
	return 0;
}

void p2p_join_status(struct p2p_join_status *status)
{
	struct peer const *peer;

	memset(status, 0, sizeof(*status));
	status->online = p2p.running;
	if (!p2p.running)
		return;
	pthread_mutex_lock(&p2p_lock);
	peer = p2p.join_requested || p2p.joining || p2p.join_reached ? find_peer(p2p.join_host) : NULL;
	if (peer && peer->probe)
		peer = NULL;
	if (peer || p2p.join_requested || p2p.joining)
		status->elapsed = p2p_now() - p2p.join_asked_time;
	if (peer && peer->connected)
	{
		status->stage = _p2p_join_reached;
		status->relayed = peer->path.carrier != 0;
	}
	else if (peer)
	{
		status->stage = _p2p_join_opening;
		status->tries = peer->punch_rounds;
		status->host_strict = candidates_strict(peer->candidates, peer->candidate_count);
		/* (an OpenCE host, or an older build's, offers none: it is not
		waited for longer) */
		status->relaying = peer->relay_offered && peer->relay_count > 0;
	}
	else if (p2p.join_requested || p2p.joining)
		status->stage = _p2p_join_finding;
	status->asked = p2p.joining;
	status->brokers = p2p_signal_connected();
	status->nat_strict = p2p.reported_symmetric && !p2p.upnp_forwarded;
	status->upnp_tried = p2p.upnp_asked && !p2p.upnp_working && !p2p.upnp_forwarded;
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_join_cancel(void)
{
	struct peer *peer;

	if (!p2p.running)
		return;
	pthread_mutex_lock(&p2p_lock);
	if (p2p.join_requested || p2p.joining)
	{
		/* (first: drop_peer would ask the host again) */
		if (p2p.joining)
			p2p_signal_stop_joining();
		p2p.join_requested = p2p.joining = p2p.join_reached = 0;
		peer = find_peer(p2p.join_host);
		if (peer && !peer->connected && !peer->probe && peer->is_host && !peer->is_player)
			drop_peer(peer, "given up (the join was cancelled)");
		p2p_log("Internet play: the join was given up after %lu ms", p2p_now() - p2p.join_asked_time);
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_probe_keep(const struct p2p_listing *listing)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];

	if (!p2p.running || !listing->invite[0])
		return;
	pthread_mutex_lock(&p2p_lock);
	if (parse_invite(listing->invite, hash, token) > 0)
	{
		memcpy(p2p.keep.hash, hash, sizeof(hash));
		memcpy(p2p.keep.token, token, sizeof(token));
		p2p_identifier_from_hash(hash, p2p.keep.identifier);
		p2p.keep.time = p2p_now() | 1;
	}
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_nat_strict(void)
{
	int strict;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	strict = p2p.reported_symmetric && !p2p.upnp_forwarded;
	pthread_mutex_unlock(&p2p_lock);
	return strict;
}

int p2p_peer_reached(const unsigned char *peer_identifier, unsigned long *address)
{
	struct peer *peer;
	int result = 0;

	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	peer = find_peer(peer_identifier);
	if (peer && peer->connected)
	{
		*address = peer->virtual_address;
		result = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

void p2p_set_hosting_allowed(int allowed)
{
	pthread_mutex_lock(&p2p_lock);
	p2p.hosting_lan_only = !allowed;
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_invite_link(char *link, int size)
{
	int hosting;

	pthread_mutex_lock(&p2p_lock);
	hosting = p2p.hosting && p2p.has_token;
	snprintf(link, (size_t)size, "%s", hosting ? p2p.invite : "");
	pthread_mutex_unlock(&p2p_lock);
	return hosting;
}

int p2p_game_invite(unsigned long host_address, char *link, int size)
{
	int found = 0;

	if (size > 0)
		link[0] = 0;
	if (!p2p.running)
		return 0;
	pthread_mutex_lock(&p2p_lock);
	if (!host_address)
	{
		found = p2p.hosting && p2p.has_token;
		if (found)
			snprintf(link, (size_t)size, "%s", p2p.invite);
	}
	else
	{
		struct peer const *peer = find_peer_by_address(host_address);

		/* (the host the last invite joined reached, at that address) */
		if (peer && peer->connected && peer->is_host && !peer->is_player &&
			!memcmp(peer->identifier, p2p.join_host, P2P_IDENTIFIER_SIZE))
		{
			unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
			char text[2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1];

			memcpy(bytes, p2p.join_host_hash, P2P_KEY_HASH_SIZE);
			memcpy(bytes + P2P_KEY_HASH_SIZE, p2p.join_token, P2P_TOKEN_SIZE);
			p2p_hex(bytes, sizeof(bytes), text);
			snprintf(link, (size_t)size, P2P_JOIN_LINK_PREFIX "%s", text);
			found = 1;
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return found;
}

void p2p_set_game_player_counts(int count, int maximum)
{
	/* (only the game's server writes these: unchanged, it need not wait
	for the lock) */
	count = count < 0 ? 0 : count;
	maximum = maximum < 0 ? 0 : maximum;
	if (count == p2p.game_player_count && maximum == p2p.game_player_maximum)
		return;
	pthread_mutex_lock(&p2p_lock);
	p2p.game_player_count = count;
	p2p.game_player_maximum = maximum;
	pthread_mutex_unlock(&p2p_lock);
}

/* ---------- relays (p2p_relay.c): two machines that do not meet directly
reach each other through a third, a dedicated server or a relay of
halo --relay */

/* debug.network_corrupt: that share of the relay's packets this machine
receives damaged, as xnet.c damages the game's datagrams, to test that
nothing sent to a relay can crash it (the tunnel's own packets need no
test of this: their seal fails) */
static void relay_corrupt(unsigned char *packet, int *size)
{
	static unsigned int seed;
	static int percent = -1;
	int index;

	if (percent < 0)
	{
		double corrupt = config_real("debug.network_corrupt");

		percent = !(corrupt > 0.0) ? 0 : corrupt > 100.0 ? 100 : (int)corrupt;
		seed = (unsigned int)p2p_now() | 1;
	}
	if (!percent)
		return;
	seed = seed * 1103515245u + 12345u;
	if ((int)((seed >> 8) % 100u) >= percent)
		return;
	seed = seed * 1103515245u + 12345u;
	switch ((seed >> 8) % 4u)
	{
	case 0:
		/* (cut short) */
		*size = (int)((seed >> 12) % (unsigned int)(*size + 1));
		break;
	case 1:
		/* (stretched, with what came before in the buffer: its 2048 bytes
		are tunnel_readable's and p2p_relay_run's) */
		*size += (int)((seed >> 12) % 64u);
		if (*size > 2048)
			*size = 2048;
		break;
	default:
		for (index = 0; index < 1 + (int)((seed >> 12) % 8u) && *size > 0; index++)
		{
			seed = seed * 1103515245u + 12345u;
			packet[(seed >> 8) % (unsigned int)*size] ^= (unsigned char)(seed >> 20);
		}
		break;
	}
}

/* this machine's side of a peer's session (p2p_relay_attempt_start): the
host's 0 (or a player answering as one), the joiner's 1 */
static int relay_side(const struct peer *peer)
{
	return peer->is_host ? 1 : 0;
}

static const char *peer_kind_text(const struct peer *peer)
{
	return peer->is_host && !peer->is_player ? "host" : "player";
}

/* where the relay carrying a peer saw its packets come from (the host's
side's alone): 0 if not known */
static int relayed_peer_address(const struct peer *peer, struct p2p_candidate *address)
{
	struct p2p_relay_attempt_state attempt;

	if (peer->path.carrier <= 0 || !p2p_relay_attempt(peer->path.carrier - 1, &attempt) ||
		!attempt.peer_address.address)
	{
		return 0;
	}
	*address = attempt.peer_address;
	return 1;
}

static struct peer *relay_attempt_peer(int index, struct p2p_relay_attempt_state *attempt)
{
	struct peer *peer;

	if (!p2p_relay_attempt(index, attempt))
		return NULL;
	peer = find_peer(attempt->peer);
	return peer && !peer->probe ? peer : NULL;
}

void p2p_relay_sendto(unsigned long address, unsigned short port, const unsigned char *data, int size)
{
	struct sockaddr_in to;

	if (p2p.tunnel_socket < 0)
		return;
	make_address(&to, address, port);
	posix_socket_sendto(p2p.tunnel_socket, data, size, 0, &to, sizeof(to));
}

void p2p_relay_paired(int index)
{
	struct p2p_relay_attempt_state attempt;
	struct peer *peer = relay_attempt_peer(index, &attempt);
	char text[LOG_ADDRESS_SIZE];

	if (!peer)
		return;
	p2p_log("Internet play: relay %s paired this machine with %s %s (%ld ms to the relay)",
		log_address_ipv4(attempt.relay.address, attempt.relay.port, text, sizeof(text)), peer_kind_text(peer),
		peer->name, attempt.bind_round_trip);
	/* (the peer pinged through it at once: the first packet through makes
	the session's path, or takes over from a silent one) */
	if (!peer->connected || p2p_relay_path_silent(&peer->path, p2p_now()))
		peer_ping_via(peer, NULL, index + 1);
}

void p2p_relay_delivered(int index, const unsigned char *packet, int size)
{
	unsigned char copy[MAXIMUM_PACKET_SIZE];
	struct p2p_relay_attempt_state attempt;
	struct sockaddr_in from;

	/* (a tunnel packet of the peer the attempt is for: the relay passes on
	only what the other side of its pair sent, but that side is anyone who
	had the token, so the packet's own seal is what proves it) */
	if (size < P2P_TUNNEL_MINIMUM_PACKET || size > (int)sizeof(copy) || !p2p_relay_attempt(index, &attempt) ||
		memcmp(packet + 1, attempt.peer, P2P_IDENTIFIER_SIZE))
	{
		return;
	}
	memcpy(copy, packet, (size_t)size);
	make_address(&from, attempt.relay.address, attempt.relay.port);
	tunnel_received(copy, size, &from, index + 1);
}

void p2p_relay_lost(int index)
{
	int peer_index;

	for (peer_index = 0; peer_index < P2P_MAXIMUM_PEERS; peer_index++)
	{
		struct peer *peer = &p2p.peers[peer_index];

		if (peer->used && peer->path.carrier == index + 1)
		{
			/* (not dropped: another of its relays, paired or bound again,
			takes over, or the peer's own silence ends it) */
			peer->path.carrier = -1;
			if (peer->connected && !peer->probe)
			{
				p2p_log("Internet play: the relay carrying %s %s lost it; trying its other relays",
					peer_kind_text(peer), peer->name);
			}
		}
	}
}

int p2p_relay_peer_address(unsigned long address, unsigned short port)
{
	int index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer const *peer = &p2p.peers[index];

		if (peer->used && peer->connected && !peer->probe && !peer->path.carrier &&
			peer->endpoint.address == address && peer->endpoint.port == port)
		{
			return 1;
		}
	}
	return 0;
}

/* the relays of a peer's session bound (each one both machines offered,
once the peer is known to bind: an OpenCE machine, or an older build,
never offers, and its pair would hold a relay's room for nothing) */
static void relay_bind_all(struct peer *peer)
{
	int index;

	if (!peer->relay_offered || peer->probe)
		return;
	for (index = 0; index < peer->relay_count; index++)
		p2p_relay_attempt_start(peer->identifier, peer->secret, relay_side(peer), &peer->relays[index], p2p_now());
}

/* a peer reached at last (via: 0 directly, else a relay's attempt + 1) */
static void relay_peer_reached(struct peer *peer, int via)
{
	memset(&peer->path, 0, sizeof(peer->path));
	peer->path.carrier = via;
	peer->path.carrier_heard = p2p_now();
	if (!via)
	{
		/* (met directly: no relay is needed) */
		p2p_relay_forget_peer(peer->identifier, 0);
		peer->relay_count = 0;
		return;
	}
	/* (the relays paired too stay, to fail over to; the rest go) */
	peer->relayed_time = p2p_now();
	peer->direct_retry_time = p2p_now();
	p2p_relay_forget_peer(peer->identifier, 1);
}

static void relay_path_changed(struct peer *peer, int via, unsigned long address, unsigned short port)
{
	char text[LOG_ADDRESS_SIZE];

	peer->endpoint.address = address;
	peer->endpoint.port = port;
	peer->endpoint_heard_time = p2p_now();
	if (via && !peer->relayed_time)
		peer->relayed_time = p2p_now();
	if (peer->probe)
		return;
	if (!via)
	{
		p2p_log("Internet play: %s %s reached directly at %s, no longer through a relay (kept a while)",
			peer_kind_text(peer), peer->name, log_address_ipv4(address, port, text, sizeof(text)));
	}
	else
	{
		p2p_log("Internet play: %s %s now reached through relay %s", peer_kind_text(peer), peer->name,
			log_address_ipv4(address, port, text, sizeof(text)));
	}
}

/* a peer that is, or was lately, reached through a relay: pinged on its
path; its other relays tried while that one is silent, and bound again if
none is paired; its own addresses tried now and then (a direct path may
open); its relays released once a direct path has lasted */
static void update_relayed_peer(struct peer *peer)
{
	unsigned long now = p2p_now();
	int attempts[P2P_RELAY_PEER_RELAYS];
	struct p2p_relay_attempt_state attempt;
	int count, index, paired = 0;

	if (p2p_relay_path_relays_done(&peer->path, now) || (!peer->path.carrier && !peer->path.direct_time))
	{
		p2p_relay_forget_peer(peer->identifier, 0);
		peer->relay_count = 0;
		memset(&peer->path, 0, sizeof(peer->path));
		peer->path.carrier_heard = now;
		return;
	}
	count = p2p_relay_peer_attempts(peer->identifier, attempts, P2P_RELAY_PEER_RELAYS);
	if (elapsed(peer->sent_time, PING_INTERVAL))
	{
		int silent = p2p_relay_path_silent(&peer->path, now);

		if (peer->path.carrier >= 0)
			peer_ping_via(peer, &peer->endpoint, peer->path.carrier);
		for (index = 0; index < count; index++)
		{
			if (!p2p_relay_attempt(attempts[index], &attempt) || !attempt.paired)
				continue;
			paired++;
			if (silent && attempts[index] + 1 != peer->path.carrier)
				peer_ping_via(peer, NULL, attempts[index] + 1);
		}
		/* (a direct path heard lately, while relayed: kept up, to see that
		it lasts) */
		if (peer->path.carrier > 0 && peer->path.direct_last && !elapsed(peer->path.direct_last, 2 * PING_INTERVAL) &&
			peer->direct.address)
		{
			peer_ping(peer, &peer->direct);
		}
		peer->sent_time = now;
		/* (none paired, the path lost: its relays bound again) */
		if (peer->path.carrier < 0 && !paired && elapsed(peer->relay_rebind_time, RELAY_REBIND_INTERVAL))
		{
			peer->relay_rebind_time = now;
			relay_bind_all(peer);
		}
	}
	/* (its own addresses, while relayed: often at first, seldom later; the
	addresses are its own word, so not for ever often) */
	if (peer->path.carrier > 0 && elapsed(peer->direct_retry_time,
		elapsed(peer->relayed_time, DIRECT_RETRY_TIME) ? DIRECT_RETRY_SLOW_INTERVAL : DIRECT_RETRY_INTERVAL))
	{
		int candidate;

		for (candidate = 0; candidate < peer->candidate_count; candidate++)
			peer_ping(peer, &peer->candidates[candidate]);
		peer->direct_retry_time = now;
	}
}

/* how much this machine relays for others, in a mode: within
server.relay_pairs and server.relay_upload, and limits of its own for
each pair and each address */
static void relay_limits(int mode, struct p2p_relay_limits *limits)
{
	long pairs = config_integer("server.relay_pairs");
	long upload = config_integer("server.relay_upload");

	memset(limits, 0, sizeof(*limits));
	limits->mode = mode;
	limits->maximum_pairs = (int)(pairs < 1 ? 1 : pairs > P2P_RELAY_MAXIMUM_PAIRS ? P2P_RELAY_MAXIMUM_PAIRS : pairs);
	/* (a CGNAT's address is many players') */
	limits->pairs_per_address = 8;
	limits->binds_per_second = 20;
	limits->binds_in_all = 2000;
	limits->upload = (upload < 16 ? 16 : upload > 1000000 ? 1000000 : upload) * 1024;
	limits->pair_upload = 256 * 1024;
	limits->address_upload = 1024 * 1024;
}

/* for whom this machine relays: a dedicated server as server.relay says
("game": its game's players alone; "any"; "off"), never a player's
machine */
static int relay_mode_setting(void)
{
	const char *setting;

	if (!halo_server_mode())
		return _p2p_relay_off;
	setting = config_string("server.relay");
	if (!strcmp(setting, "any"))
		return _p2p_relay_any;
	if (!strcmp(setting, "off"))
		return _p2p_relay_off;
	return _p2p_relay_game;
}

static void relay_configure(void)
{
	struct p2p_relay_limits limits;
	int mode = relay_mode_setting();

	relay_limits(mode, &limits);
	if (mode != p2p.relay_mode)
	{
		p2p.relay_mode = mode;
		if (mode != _p2p_relay_off)
		{
			p2p_log("Internet play: this server relays for %s (%d pairs at most, %ld KB/s)", mode == _p2p_relay_any ?
				"any two machines that cannot meet" : "its game's players that cannot meet each other",
				limits.maximum_pairs, limits.upload / 1024);
		}
	}
	p2p_relay_configure(&limits);
}

/* relays.txt (network.relays_file; a dedicated server's server.relays_file)
read and looked up, every RELAYS_LOOKUP_INTERVAL and as the setting
changes: a step of its own in the p2p thread's pass, as DNS lets go of the
lock (no offer is being made meanwhile: offers take the relays looked up
last, and the list changes at once, after every lookup) */
static void relays_lookup(void)
{
	const char *setting = config_string(halo_server_mode() ? "server.relays_file" : "network.relays_file");
	char hosts[MAXIMUM_OWN_RELAYS][MASTER_LIST_RELAY_HOST_SIZE];
	unsigned short ports[MAXIMUM_OWN_RELAYS];
	struct p2p_candidate found[MAXIMUM_OWN_RELAYS];
	int count, index, found_count = 0;

	if (!strcmp(setting, p2p.relays_setting) && p2p.relays_time && !elapsed(p2p.relays_time, RELAYS_LOOKUP_INTERVAL))
		return;
	snprintf(p2p.relays_setting, sizeof(p2p.relays_setting), "%s", setting);
	p2p.relays_time = p2p_now() | 1;
	count = master_list_read_relays(p2p.relays_setting, hosts, ports, MAXIMUM_OWN_RELAYS);
	for (index = 0; index < count; index++)
	{
		unsigned long address = p2p_resolve(hosts[index]);

		if (!address)
		{
			p2p_log("Internet play: relay %s of %s not found", hosts[index], p2p.relays_setting);
			continue;
		}
		found[found_count].address = address;
		found[found_count].port = network_short(ports[index]);
		found_count++;
	}
	if (found_count && (found_count != p2p.own_relay_count ||
		memcmp(found, p2p.own_relays, sizeof(found[0]) * (size_t)found_count)))
	{
		p2p_log("Internet play: %d relay%s known (%s)", found_count, found_count == 1 ? "" : "s", p2p.relays_setting);
	}
	memcpy(p2p.own_relays, found, sizeof(found[0]) * (size_t)found_count);
	p2p.own_relay_count = found_count;
}

/* the relays this machine offers a peer: those its game's dedicated server
told it of, then relays.txt's; never the peer itself, and only relays:
never another player's machine. Returns their count */
static int relay_own_offer(const struct peer *peer, struct p2p_candidate *relays)
{
	struct p2p_candidate all[MAXIMUM_GAME_RELAYS + MAXIMUM_OWN_RELAYS];
	int all_count = 0, count = 0;
	int index, known;

	for (index = 0; index < p2p.game_relay_count; index++)
		all[all_count++] = p2p.game_relays[index];
	for (index = 0; index < p2p.own_relay_count; index++)
		all[all_count++] = p2p.own_relays[index];
	for (index = 0; index < all_count && count < P2P_RELAY_OFFERED; index++)
	{
		int skip = !candidate_usable(&all[index]);

		for (known = 0; known < peer->candidate_count && !skip; known++)
		{
			skip = peer->candidates[known].address == all[index].address &&
				peer->candidates[known].port == all[index].port;
		}
		for (known = 0; known < count && !skip; known++)
			skip = relays[known].address == all[index].address && relays[known].port == all[index].port;
		if (!skip)
			relays[count++] = all[index];
	}
	return count;
}

/* the relays this machine knows for a peer not reached, offered to it
through signalling (an empty offer too: the peer learns that this machine
binds, and binds its own), and bound once the peer offered too */
static void relay_offer(struct peer *peer)
{
	struct p2p_candidate relays[P2P_RELAY_OFFERED];
	unsigned char body[P2P_RELAY_OFFER_MAXIMUM_SIZE];
	int count = relay_own_offer(peer, relays);
	int size = p2p_relay_offer_write(identifier, relay_side(peer), relays, count, peer->secret, body);

	p2p_relay_merge(peer->relays, &peer->relay_count, relays, count);
	p2p_signal_relay_offer(peer->identifier, peer->is_host, body, size);
	if (!peer->relay_offer_time)
	{
		p2p_log("Internet play: %s %s not reached in %lu ms: offering it %d relay%s", peer_kind_text(peer),
			peer->name, p2p_now() - peer->offered_time, count, count == 1 ? "" : "s");
	}
	peer->relay_offer_time = p2p_now() | 1;
	peer->relay_offers_sent++;
	relay_bind_all(peer);
}

void p2p_relay_offer_heard(const unsigned char *body, int size)
{
	struct p2p_candidate relays[P2P_RELAY_OFFERED];
	struct p2p_candidate usable[P2P_RELAY_OFFERED];
	struct peer *peer;
	int count, index, usable_count = 0;

	if (size < P2P_IDENTIFIER_SIZE || !config_boolean("network.use_relays"))
		return;
	peer = find_peer(body);
	/* (the peer's own offer, of its side of the session: tagged with the
	session's secret, which no one else has; a few, a second apart) */
	if (!peer || peer->probe || peer->connected)
		return;
	count = p2p_relay_offer_read(body, size, !relay_side(peer), peer->secret, relays);
	if (count < 0 || !p2p_relay_offer_take(&peer->relay_offers_heard, p2p_now()))
		return;
	for (index = 0; index < count; index++)
	{
		if (candidate_usable(&relays[index]))
			usable[usable_count++] = relays[index];
	}
	if (!peer->relay_offered)
	{
		p2p_log("Internet play: %s %s offers %d relay%s", peer_kind_text(peer), peer->name, usable_count,
			usable_count == 1 ? "" : "s");
	}
	peer->relay_offered = 1;
	p2p_relay_merge(peer->relays, &peer->relay_count, usable, usable_count);
	/* (this machine's own offer, if it made none yet: the peer binds its
	relays only once it heard this machine would) */
	if (!peer->relay_offer_time)
		relay_offer(peer);
	else
		relay_bind_all(peer);
}

/* a dedicated server's relays, told a player of its game (the player
offers them to the game's other players it cannot reach: map sharing) */
static void relays_tell(struct peer *peer)
{
	unsigned char inner[3 + MAXIMUM_GAME_RELAYS * 6];
	int relaying = p2p.relay_mode != _p2p_relay_off;
	int count = 0, index;

	inner[0] = _packet_relays;
	inner[1] = (unsigned char)(relaying ? _relays_server_relays : 0);
	for (index = 0; index < p2p.own_relay_count && count < MAXIMUM_GAME_RELAYS - relaying; index++)
	{
		memcpy(inner + 3 + count * 6, &p2p.own_relays[index].address, 4);
		memcpy(inner + 3 + count * 6 + 4, &p2p.own_relays[index].port, 2);
		count++;
	}
	inner[2] = (unsigned char)count;
	peer_send(peer, inner, 3 + count * 6);
}

static void relays_told_received(struct peer *peer, const unsigned char *inner, int size)
{
	struct p2p_candidate relays[MAXIMUM_GAME_RELAYS];
	int count, index, relay_count = 0;

	/* (the host of the game joined, reached directly) */
	if (size < 3 || peer->probe || peer != joined_host() || p2p.hosting || peer->path.carrier)
		return;
	count = inner[2];
	if (count > MAXIMUM_GAME_RELAYS || size != 3 + count * 6)
		return;
	if (inner[1] & _relays_server_relays)
		relays[relay_count++] = peer->endpoint;
	for (index = 0; index < count && relay_count < MAXIMUM_GAME_RELAYS; index++)
	{
		memcpy(&relays[relay_count].address, inner + 3 + index * 6, 4);
		memcpy(&relays[relay_count].port, inner + 3 + index * 6 + 4, 2);
		if (candidate_usable(&relays[relay_count]))
			relay_count++;
	}
	if (relay_count != p2p.game_relay_count || memcmp(p2p.game_relays_from, peer->identifier, P2P_IDENTIFIER_SIZE))
		p2p_log("Internet play: the game's server offers %d relay%s", relay_count, relay_count == 1 ? "" : "s");
	memcpy(p2p.game_relays, relays, sizeof(relays[0]) * (size_t)relay_count);
	p2p.game_relay_count = relay_count;
	memcpy(p2p.game_relays_from, peer->identifier, P2P_IDENTIFIER_SIZE);
}

/* the relays' pass: this machine's relaying configured and run; relays.txt
looked up; the game's server's relays forgotten once its game is left; a
dedicated server's told its players; and the peers not reached offered
relays once hole punching had its time (again every RELAY_OFFER_INTERVAL
while not reached) */
static void update_relays(void)
{
	int strict = p2p.reported_symmetric && !p2p.upnp_forwarded;
	int index;

	relay_configure();
	p2p_relay_update(p2p_now());
	relays_lookup();
	if (p2p.game_relay_count)
	{
		struct peer const *host = joined_host();

		if (!host || memcmp(host->identifier, p2p.game_relays_from, P2P_IDENTIFIER_SIZE))
			p2p.game_relay_count = 0;
	}
	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer *peer = &p2p.peers[index];

		if (!peer->used || peer->probe)
			continue;
		/* (a dedicated server's players told its relays, a few times) */
		if (peer->connected && halo_server_mode() && !peer->is_host && peer->relays_told < RELAYS_TOLD &&
			(p2p.relay_mode != _p2p_relay_off || p2p.own_relay_count) &&
			(!peer->relays_told || elapsed(peer->relays_told_time, PING_INTERVAL)))
		{
			relays_tell(peer);
			peer->relays_told++;
			peer->relays_told_time = p2p_now();
		}
		if (peer->connected || !config_boolean("network.use_relays") ||
			!p2p_relay_due(p2p_now() - peer->offered_time, strict && candidates_strict(peer->candidates,
				peer->candidate_count)))
		{
			continue;
		}
		/* (again while the peer offers too, a few in all; an OpenCE machine,
		or an older build, is sent RELAY_OFFERS_UNANSWERED, which it passes
		over, and no more) */
		if (!peer->relay_offer_time || (peer->relay_offers_sent < (peer->relay_offered ? 8 : RELAY_OFFERS_UNANSWERED) &&
			elapsed(peer->relay_offer_time, RELAY_OFFER_INTERVAL)))
		{
			relay_offer(peer);
		}
	}
}

/* halo --relay (server_mode.c): a relay alone, on network.tunnel_port (2302
if none), for any two machines, within server.relay_pairs and
server.relay_upload, until stop says so; 0, or -1 if its port could not be
opened */
int p2p_relay_run(int (*stop)(void))
{
	struct p2p_relay_limits limits;
	struct p2p_relay_counts counts, logged;
	struct p2p_front_limits front_limits;
	struct p2p_front_counts front_counts, front_logged;
	long port = config_integer("network.tunnel_port");
	unsigned long logged_time;

	relay_standalone = 1;
	if (port <= 0 || port > 65535)
		port = 2302;
	p2p.tunnel_socket = open_socket(SOCK_DGRAM, 0, network_short((unsigned short)port), &p2p.tunnel_port);
	if (p2p.tunnel_socket < 0)
	{
		p2p_log("Relay: cannot open UDP port %ld (network.tunnel_port): taken by another program?", port);
		return -1;
	}
	{
		int size = 1 << 20;

		posix_socket_setsockopt(p2p.tunnel_socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
		posix_socket_setsockopt(p2p.tunnel_socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	}
	relay_limits(_p2p_relay_any, &limits);
	p2p_relay_configure(&limits);
	p2p_log("Relay: relaying on UDP port %ld for any two machines that cannot meet (%d pairs at most, %ld KB/s); "
		"players name it in relays.txt as <this server's address>:%ld", port, limits.maximum_pairs,
		limits.upload / 1024, port);
	/* (its front: public ports for the servers its operators' keys allow) */
	front_limits.port_count = front_ports_open(&front_limits.first_port);
	front_limits.upload = limits.upload;
	p2p.front_socket_count = front_limits.port_count;
	p2p_front_configure(&front_limits);
	if (front_limits.port_count)
	{
		front_operators_read();
		p2p_log("Relay: front on UDP ports %u to %u, one for each server registered (network.front = <this "
			"server's address>:%ld)", (unsigned)front_limits.first_port,
			(unsigned)(front_limits.first_port + front_limits.port_count - 1), port);
	}
	memset(&logged, 0, sizeof(logged));
	memset(&front_logged, 0, sizeof(front_logged));
	logged_time = p2p_now();
	while (!stop())
	{
		unsigned char packet[2048];
		int read[1 + P2P_FRONT_MAXIMUM_PORTS], read_count = 1, write_count = 0, error_count = 0;
		int count, index;

		read[0] = p2p.tunnel_socket;
		for (index = 0; index < p2p.front_socket_count; index++)
			read[read_count++] = p2p.front_sockets[index];
		posix_socket_select(read, &read_count, NULL, &write_count, NULL, &error_count, 0, 100000, 0);
		for (count = 0; count < 256; count++)
		{
			struct sockaddr_in from;
			int from_length = sizeof(from);
			int size = posix_socket_recvfrom(p2p.tunnel_socket, packet, sizeof(packet), 0, &from, &from_length);

			if (size < 0)
				break;
			if (size >= 1 && packet[0] == P2P_RELAY_MAGIC)
			{
				relay_corrupt(packet, &size);
				p2p_relay_received(packet, size, from.sin_addr.s_addr, from.sin_port, p2p_now());
			}
			else if (size >= 1 && packet[0] == P2P_FRONT_MAGIC && p2p.front_socket_count)
			{
				relay_corrupt(packet, &size);
				p2p_front_received(packet, size, from.sin_addr.s_addr, from.sin_port, p2p_now());
			}
		}
		for (index = 0; index < p2p.front_socket_count; index++)
		{
			for (count = 0; count < 64; count++)
			{
				struct sockaddr_in from;
				int from_length = sizeof(from);
				int size = posix_socket_recvfrom(p2p.front_sockets[index], packet, sizeof(packet), 0, &from,
					&from_length);

				if (size < 0)
					break;
				p2p_front_public_received(index, packet, size, from.sin_addr.s_addr, from.sin_port, p2p_now());
			}
		}
		p2p_relay_update(p2p_now());
		if (p2p.front_socket_count)
		{
			front_operators_read();
			p2p_front_update(p2p_now());
		}
		/* (its counts each minute they changed) */
		if (elapsed(logged_time, 60000))
		{
			p2p_relay_counts(&counts);
			if (memcmp(&counts, &logged, sizeof(counts)))
			{
				p2p_log("Relay: %d pairs (%d paired); %lu formed, %lu ended, %lu refused, %lu challenged; %lu packets "
					"(%lu bytes) forwarded, %lu dropped by its limits", counts.pairs, counts.paired, counts.formed,
					counts.ended, counts.refused, counts.challenged, counts.forwarded_packets,
					counts.forwarded_bytes, counts.dropped);
				logged = counts;
			}
			p2p_front_counts(&front_counts);
			if (p2p.front_socket_count && memcmp(&front_counts, &front_logged, sizeof(front_counts)))
			{
				p2p_log("Relay: front: %d servers registered (%d clients); %lu registered, %lu ended, %lu refused, "
					"%lu challenged; %lu packets (%lu bytes) forwarded, %lu dropped by its limits",
					front_counts.registered, front_counts.clients, front_counts.registrations, front_counts.ended,
					front_counts.refused, front_counts.challenged, front_counts.forwarded_packets,
					front_counts.forwarded_bytes, front_counts.dropped);
				front_logged = front_counts;
			}
			logged_time = p2p_now();
		}
	}
	{
		int index;

		for (index = 0; index < p2p.front_socket_count; index++)
			close_socket(&p2p.front_sockets[index]);
		p2p.front_socket_count = 0;
	}
	close_socket(&p2p.tunnel_socket);
	return 0;
}

/* ---------- fronts (p2p_front.c): a public port on a relay of
halo --relay that this machine registers with (network.front), and
halo --relay's own ports for servers (server.front_ports) */

void p2p_front_sendto(int port_index, unsigned long address, unsigned short port, const unsigned char *data,
	int size)
{
	struct sockaddr_in to;
	int socket = port_index < 0 ? p2p.tunnel_socket :
		port_index < p2p.front_socket_count ? p2p.front_sockets[port_index] : -1;

	if (socket < 0)
		return;
	make_address(&to, address, port);
	posix_socket_sendto(socket, data, size, 0, &to, sizeof(to));
}

int p2p_front_sign(const unsigned char *message, int size, unsigned char *public_key, unsigned char *signature)
{
	return server_master_sign(message, size, public_key, signature);
}

void p2p_front_delivered(unsigned long client, const struct p2p_candidate *client_address,
	const unsigned char *packet, int size)
{
	unsigned char copy[MAXIMUM_PACKET_SIZE];
	struct p2p_candidate public_address;
	struct sockaddr_in from;
	struct peer *peer;
	int index, created;

	/* (a peer's packet: its sender's identifier names it, and its seal
	proves it, below. A path is made for the client only once one did) */
	if (size < P2P_TUNNEL_MINIMUM_PACKET || size > (int)sizeof(copy) || !p2p_front_public(&public_address))
		return;
	peer = find_peer(packet + 1);
	if (!peer || peer->probe)
		return;
	index = p2p_relay_front_attempt(peer->identifier, &public_address, client, client_address, p2p_now(), &created);
	if (index < 0)
		return;
	memcpy(copy, packet, (size_t)size);
	make_address(&from, public_address.address, public_address.port);
	if (!tunnel_received(copy, size, &from, index + 1))
	{
		if (created)
			p2p_relay_front_drop(index);
		return;
	}
	p2p_relay_front_heard(index, p2p_now());
}

void p2p_front_lost(void)
{
	p2p_relay_front_forget();
}

/* network.front ("host[:port]", 2302 if none), looked up as it changes and
every few minutes (a host name's address may change), and the registration
kept (p2p_front_update) */
static void update_front(void)
{
	enum { FRONT_LOOKUP_SECONDS = 300 };
	const char *setting = config_string("network.front");
	char host[256];
	char *colon;
	long port = 2302;

	if (strcmp(setting, p2p.front_text) || !p2p.front_time || elapsed(p2p.front_time, FRONT_LOOKUP_SECONDS * 1000UL))
	{
		snprintf(p2p.front_text, sizeof(p2p.front_text), "%s", setting);
		p2p.front_time = p2p_now() | 1;
		memset(&p2p.front, 0, sizeof(p2p.front));
		snprintf(host, sizeof(host), "%s", setting);
		colon = strchr(host, ':');
		if (colon && !strchr(colon + 1, ':'))
		{
			char *end;

			port = strtol(colon + 1, &end, 10);
			*colon = 0;
			if (*end || port < 1 || port > 65535)
				port = 0;
		}
		if (host[0] && (!colon || !strchr(colon + 1, ':')) && port)
		{
			p2p.front.address = p2p_resolve(host);
			p2p.front.port = network_short((unsigned short)port);
			if (!p2p.front.address)
				p2p_log("Internet play: cannot look up network.front %s", host);
		}
		else if (host[0])
			p2p_log("Internet play: network.front %s is not host[:port] (IPv4): no front used", setting);
		if (!p2p.front.address)
			memset(&p2p.front, 0, sizeof(p2p.front));
	}
	p2p_front_use(&p2p.front);
	p2p_front_update(p2p_now());
}

/* server.front_operators_file's keys given to the front (halo --relay),
read again every minute */
static void front_operators_read(void)
{
	const char *setting = config_string("server.front_operators_file");
	unsigned char keys[P2P_FRONT_MAXIMUM_OPERATORS][P2P_FRONT_KEY_SIZE];
	char folder[1024], path[1300];
	size_t length;
	char *text;
	int count = 0;

	if (p2p.front_operators_time && !elapsed(p2p.front_operators_time, 60000))
		return;
	if (setting[0] == '/' || (setting[0] && setting[1] == ':'))
		snprintf(path, sizeof(path), "%s", setting);
	else
	{
		config_folder(folder, sizeof(folder));
		snprintf(path, sizeof(path), "%s%s", folder, setting);
	}
	text = setting[0] ? config_file_read(path, &length) : NULL;
	if (text)
	{
		count = p2p_front_operators_parse(text, keys, P2P_FRONT_MAXIMUM_OPERATORS);
		free(text);
	}
	if (!p2p.front_operators_time)
	{
		p2p_log("Relay: front: %d operator key%s allowed (%s)", count, count == 1 ? "" : "s", setting[0] ? path :
			"server.front_operators_file is empty");
	}
	p2p.front_operators_time = p2p_now() | 1;
	p2p_front_operators((const unsigned char (*)[P2P_FRONT_KEY_SIZE])keys, count);
}

/* server.front_ports ("first-last", or one port; empty: none) opened for
halo --relay's front: how many (0: none, or a port was taken) */
static int front_ports_open(unsigned short *first_port)
{
	const char *setting = config_string("server.front_ports");
	long first, last;
	char *end;
	int index;

	if (!setting[0])
		return 0;
	first = strtol(setting, &end, 10);
	last = *end == '-' ? strtol(end + 1, &end, 10) : first;
	if (*end || first < 1 || last > 65535 || last < first || last - first + 1 > P2P_FRONT_MAXIMUM_PORTS)
	{
		p2p_log("Relay: server.front_ports %s is not first-last (1 to 65535, %d ports at most): no front", setting,
			P2P_FRONT_MAXIMUM_PORTS);
		return 0;
	}
	for (index = 0; index <= last - first; index++)
	{
		int size = 1 << 20;

		p2p.front_sockets[index] = open_socket(SOCK_DGRAM, 0, network_short((unsigned short)(first + index)), NULL);
		if (p2p.front_sockets[index] < 0)
		{
			p2p_log("Relay: cannot open UDP port %ld (server.front_ports): taken by another program? No front",
				first + index);
			while (index-- > 0)
				close_socket(&p2p.front_sockets[index]);
			return 0;
		}
		posix_socket_setsockopt(p2p.front_sockets[index], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
		posix_socket_setsockopt(p2p.front_sockets[index], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	}
	*first_port = (unsigned short)first;
	return (int)(last - first + 1);
}

/* ---------- UPnP (posix_upnp.c): the router forwards a port here */

/* the router asked, on a thread of its own (it takes seconds) */
static void *upnp_thread(void *unused)
{
	unsigned short port, previous_port = 0;
	posix_ulong address = 0;
	unsigned short external_port = 0;
	char error[160] = "";
	int forwarded;

	(void)unused;
	pthread_mutex_lock(&p2p_lock);
	port = p2p.tunnel_port;
	if (p2p.upnp_forwarded)
		previous_port = p2p.upnp_candidate.port;
	pthread_mutex_unlock(&p2p_lock);
	/* a renewal asks for the port the router gave before; if it gives
	another, the old forwarding goes */
	forwarded = posix_upnp_forward_udp(port, previous_port ? previous_port : port, &address, &external_port,
		error, sizeof(error));
	if (forwarded && previous_port && previous_port != external_port)
		posix_upnp_stop_forwarding_udp(previous_port);
	pthread_mutex_lock(&p2p_lock);
	p2p.upnp_working = 0;
	p2p.upnp_time = p2p_now();
	if (forwarded)
	{
		char text[LOG_ADDRESS_SIZE];

		if (!p2p.upnp_forwarded || p2p.upnp_candidate.address != address ||
			p2p.upnp_candidate.port != external_port)
		{
			p2p_log("Internet play: the router forwards %s to this machine (UPnP)",
				log_address_ipv4(address, external_port, text, sizeof(text)));
		}
		p2p.upnp_forwarded = 1;
		p2p.upnp_candidate.address = address;
		p2p.upnp_candidate.port = external_port;
	}
	else if (!p2p.upnp_forwarded)
	{
		p2p_log("Internet play: UPnP: %s", error);
	}
	pthread_mutex_unlock(&p2p_lock);
	return NULL;
}

/* quitting: the party is left at once (its leader's passes on, not after
its silence), and a game listed in the server browser is taken out of it
at once (not when its listing lapses), as when it stops being hosted */
static void lobby_quit(void)
{
	pthread_mutex_lock(&p2p_lock);
	p2p_party_quit();
	p2p_friends_quit();
	p2p_lobby_quit();
	pthread_mutex_unlock(&p2p_lock);
}

/* the game exits: the router forwards the port no longer (after a request
under way, which may forward one, if it ends soon) */
static void upnp_release(void)
{
	unsigned short external_port = 0;
	unsigned long start = p2p_now();

	pthread_mutex_lock(&p2p_lock);
	p2p.upnp_released = 1;
	while (p2p.upnp_working && !elapsed(start, UPNP_RELEASE_WAIT))
	{
		pthread_mutex_unlock(&p2p_lock);
		Sleep(50);
		pthread_mutex_lock(&p2p_lock);
	}
	/* (still under way: posix_upnp.c takes one caller at a time) */
	if (p2p.upnp_forwarded && !p2p.upnp_working)
	{
		external_port = p2p.upnp_candidate.port;
		p2p.upnp_forwarded = 0;
	}
	pthread_mutex_unlock(&p2p_lock);
	if (external_port)
		posix_upnp_stop_forwarding_udp(external_port);
}

/* whether a forwarded port would help: a player reaching this host (at
once: they may need it), or a peer not reached in a while (every copy of
the game listens, and so has an invite, from its start: a host asks only
when its invite is used) */
static int upnp_needed(void)
{
	int index;

	/* (at once where this network's NAT is known strict: the path needs it) */
	if (p2p.joining && (p2p.reported_symmetric || elapsed(p2p.join_time, UPNP_JOIN_DELAY)))
		return 1;
	/* a game in the server browser: at once (more joiners get through) */
	if (p2p_lobby_listed())
		return 1;
	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
	{
		struct peer const *peer = &p2p.peers[index];

		if (peer->used && !peer->connected && !peer->probe &&
			((p2p.hosting && !peer->is_host) || elapsed(peer->offered_time, UPNP_JOIN_DELAY)))
		{
			return 1;
		}
	}
	return 0;
}

static void update_upnp(void)
{
	static int allowed = -1;
	int ask;
	pthread_t thread;

	if (allowed < 0)
		allowed = config_boolean("network.allow_upnp") ? 1 : 0;
	if (!allowed || p2p.upnp_working || p2p.upnp_released)
		return;
	if (p2p.upnp_forwarded)
		ask = elapsed(p2p.upnp_time, UPNP_RENEW_INTERVAL);
	else
		ask = upnp_needed() && (!p2p.upnp_asked || elapsed(p2p.upnp_time, UPNP_RETRY_INTERVAL));
	if (!ask)
		return;
	p2p.upnp_working = 1;
	p2p.upnp_asked = 1;
	if (!p2p.upnp_release_registered)
	{
		p2p.upnp_release_registered = 1;
		atexit(upnp_release);
	}
	if (pthread_create(&thread, NULL, upnp_thread, NULL) != 0)
	{
		p2p.upnp_working = 0;
		p2p.upnp_time = p2p_now();
		return;
	}
	pthread_detach(thread);
}

const char *p2p_take_clipboard_text(void)
{
	static char text[P2P_LINK_SIZE];
	const char *result = NULL;

	if (!p2p.running)
		return NULL;
	pthread_mutex_lock(&p2p_lock);
	if (p2p.has_clipboard)
	{
		memcpy(text, p2p.clipboard, sizeof(text));
		p2p.has_clipboard = 0;
		result = text;
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

/* ---------- invites from elsewhere */

/* the first command line argument holding an invite (or an older
version's, which the copy that takes it says it cannot join), or a party's
link */
static int command_line_invite(char *text, int size)
{
	int index;

	for (index = 1; posix_command_line_argument(index, text, (posix_ulong)size); index++)
	{
		unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];

		/* (or a party's link) */
		if (parse_invite(text, hash, token) || p2p_party_parse_link(text, token))
			return 1;
	}
	return 0;
}

/* the key of this user's copies of the game, from a secret only they can
read: another user's program may have the port, and must neither read the
invites nor pass its own */
static int handoff_key(unsigned char *key)
{
	unsigned char secret[P2P_SHA256_SIZE];

	if (!posix_user_secret(secret, sizeof(secret)))
		return 0;
	p2p_hmac_sha256(secret, sizeof(secret), "halo handoff", 12, key);
	return 1;
}

/* the answer to a handed over invite: that the copy that took it has the
key */
static void handoff_answer(const unsigned char *key, const unsigned char *message, unsigned char *answer)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_hmac_sha256(key, P2P_SHA256_SIZE, message, 12 + P2P_NONCE_SIZE, digest);
	memcpy(answer, digest, 16);
}

int p2p_hand_off_invite(void)
{
#ifdef HALO_ANDROID
	return 0;
#else
	char invite[256];
	unsigned char key[P2P_SHA256_SIZE];
	unsigned char message[12 + sizeof(invite) + P2P_SEAL_OVERHEAD];
	unsigned char answer[16];
	struct sockaddr_in to;
	int size;
	int socket;
	int attempt;
	int result = 0;

	if (!command_line_invite(invite, sizeof(invite)) || !handoff_key(key))
		return 0;
	socket = open_socket(SOCK_DGRAM, network_long(0x7F000001), 0, NULL);
	if (socket < 0)
		return 0;
	memcpy(message, "halo-invite ", 12);
	size = 12 + p2p_seal(key, invite, (int)strlen(invite), message + 12);
	handoff_answer(key, message, answer);
	make_address(&to, network_long(0x7F000001), network_short(HANDOFF_PORT));
	for (attempt = 0; attempt < 3 && !result; attempt++)
	{
		int read[1] = { socket };
		int read_count = 1, write_count = 0, error_count = 0;
		unsigned char reply[32];

		posix_socket_sendto(socket, message, size, 0, &to, sizeof(to));
		if (posix_socket_select(read, &read_count, NULL, &write_count, NULL, &error_count, 0, 150000, 0) > 0 &&
			posix_socket_recv(socket, reply, sizeof(reply), 0) == (int)sizeof(answer) &&
			p2p_equal(reply, answer, sizeof(answer)))
		{
			result = 1;
		}
	}
	posix_socket_close(socket);
	if (result)
		p2p_log("Internet play: passed the invite to the copy of the game already running");
	return result;
#endif
}

static void handoff_readable(void)
{
	unsigned char message[12 + 256 + P2P_SEAL_OVERHEAD];
	char invite[257];
	unsigned char answer[16];
	struct sockaddr_in from;
	int from_length = sizeof(from);
	int size = posix_socket_recvfrom(p2p.handoff_socket, message, sizeof(message), 0, &from, &from_length);

	if (size < 12 + P2P_SEAL_OVERHEAD || from.sin_addr.s_addr != network_long(0x7F000001) ||
		memcmp(message, "halo-invite ", 12) || !p2p.has_handoff_key)
		return;
	size = p2p_open(p2p.handoff_key, message + 12, size - 12, (unsigned char *)invite);
	if (size < 0)
		return;
	invite[size] = 0;
	handoff_answer(p2p.handoff_key, message, answer);
	posix_socket_sendto(p2p.handoff_socket, answer, sizeof(answer), 0, &from, sizeof(from));
	p2p_invite_received(invite);
}

#ifdef HALO_ANDROID
/* the app's activity writes a link it was opened with here */
static void poll_invite_file(void)
{
	static unsigned long checked_time;
	char path[512];
	char taken[512];
	char text[256];
	FILE *file;
	size_t size;

	if (!elapsed(checked_time, 1000))
		return;
	checked_time = p2p_now();
	snprintf(path, sizeof(path), "%s/join_link.txt", platform_data_root());
	snprintf(taken, sizeof(taken), "%s/join_link.taken", platform_data_root());
	/* (taken first: a link the launcher writes while this reads is left for
	the next look, not removed unread) */
	if (rename(path, taken) != 0)
		return;
	file = fopen(taken, "rb");
	if (!file)
		return;
	size = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	remove(taken);
	text[size] = 0;
	p2p_invite_received(text);
}
#endif

#ifdef HALO_MACOS
/* port/macos/host_links.c: the links the Mac or iPhone app was opened with
(openh1://..., the join page's addresses), at its launch or since: macOS and
iOS give a running app the links it handles, and start no second copy */
int host_app_take_link(char *link, int size);

static void poll_app_links(void)
{
	char text[256];

	while (host_app_take_link(text, sizeof(text)))
		p2p_invite_received(text);
}
#endif

/* ---------- the thread */

#ifndef HALO_ANDROID
/* the desktop's links (Linux's and Windows'; the Mac's, iOS's and
Android's apps declare theirs): the game's scheme, openh1, and not older
builds' halo, which is OpenCE's: a registration of it this executable made
is dropped, so that OpenCE's copies have their links, and the game's are
never opened by OpenCE (the game still takes a halo:// link it is given) */
static void register_link_schemes(void)
{
	p2p_register_url_scheme(P2P_LINK_SCHEME, "OpenH1 invite");
	p2p_forget_url_scheme(P2P_OLD_LINK_SCHEME);
}
#endif

static void *p2p_thread(void *unused)
{
	enum
	{
		MAXIMUM_SOCKETS = 2 + MAXIMUM_PROXIES + MAXIMUM_LISTENERS + MAXIMUM_STREAMS + 16,
		/* what each socket waited for is (owners) */
		_owner_tunnel = 0,
		_owner_handoff,
		_owner_proxy,
		_owner_listener,
		_owner_stream,
		_owner_signal,
	};
	/* the sockets waited for, and those that are ready (the same order,
	which posix_socket_select keeps), and whose each is: its kind in the low
	byte, its index above */
	static int read[MAXIMUM_SOCKETS], write[MAXIMUM_SOCKETS];
	static int asked_read[MAXIMUM_SOCKETS], asked_write[MAXIMUM_SOCKETS];
	static int read_owners[MAXIMUM_SOCKETS], write_owners[MAXIMUM_SOCKETS];

	(void)unused;
#ifdef HALO_PROFILE
	profile_trace_thread_register(_profile_track_p2p);
	p2p_profile_names.pass = profile_trace_name("p2p.pass");
	p2p_profile_names.tunnel_receive = profile_trace_name("p2p.tunnel_receive");
	p2p_profile_names.kcp_update = profile_trace_name("p2p.kcp_update");
	p2p_profile_names.streams = profile_trace_name("p2p.streams");
#endif
	pthread_mutex_lock(&p2p_lock);
#ifndef HALO_ANDROID
	/* (here: it may wait for a program) */
	register_link_schemes();
#endif
	for (;;)
	{
		int read_count = 0, write_count = 0, error_count = 0;
		int asked_read_count, asked_write_count;
		/* KCP's clock needs a pass every LOOP_INTERVAL while it carries
		streams; otherwise the thread can sleep longer */
		int wait = LOOP_INTERVAL * 5;
		int index, asked;
		unsigned long waited;

		/* what to wait for */
		read_owners[read_count] = _owner_tunnel;
		read[read_count++] = p2p.tunnel_socket;
		if (p2p.handoff_socket >= 0)
		{
			read_owners[read_count] = _owner_handoff;
			read[read_count++] = p2p.handoff_socket;
		}
		for (index = 0; index < MAXIMUM_PROXIES; index++)
		{
			if (p2p.proxies[index].socket >= 0)
			{
				read_owners[read_count] = _owner_proxy | index << 8;
				read[read_count++] = p2p.proxies[index].socket;
			}
		}
		for (index = 0; index < MAXIMUM_LISTENERS; index++)
		{
			if (p2p.listeners[index].socket >= 0)
			{
				read_owners[read_count] = _owner_listener | index << 8;
				read[read_count++] = p2p.listeners[index].socket;
			}
		}
		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			struct stream *stream = &p2p.streams[index];

			if (stream->used)
				wait = LOOP_INTERVAL;
			if (!stream->used || stream->socket < 0)
				continue;
			/* (not while the tunnel's window is full, which stream_readable
			waits out, or the wait would return at once) */
			if (stream->state == _stream_open_state && ikcp_waitsnd(stream->kcp) < STREAM_WINDOW)
			{
				read_owners[read_count] = _owner_stream | index << 8;
				read[read_count++] = stream->socket;
			}
			if (stream->state == _stream_connecting || stream->pending_size)
			{
				write_owners[write_count] = _owner_stream | index << 8;
				write[write_count++] = stream->socket;
			}
		}
		asked_read_count = read_count;
		asked_write_count = write_count;
		p2p_signal_select_sets(read, &read_count, write, &write_count,
			MAXIMUM_SOCKETS - (read_count > write_count ? read_count : write_count));
		for (index = asked_read_count; index < read_count; index++)
			read_owners[index] = _owner_signal;
		for (index = asked_write_count; index < write_count; index++)
			write_owners[index] = _owner_signal;
		asked_read_count = read_count;
		asked_write_count = write_count;
		memcpy(asked_read, read, sizeof(*read) * (size_t)read_count);
		memcpy(asked_write, write, sizeof(*write) * (size_t)write_count);

		pthread_mutex_unlock(&p2p_lock);
		waited = p2p_now();
		if (posix_socket_select(read, &read_count, write, &write_count, NULL, &error_count, 0,
			wait * 1000, 0) < 0)
		{
			read_count = write_count = 0;
		}
		pthread_mutex_lock(&p2p_lock);
#ifdef HALO_PROFILE
		profile_trace_begin(p2p_profile_names.pass);
#endif
		/* the wait lasted far longer than asked (the process was suspended
		or the machine slept): what the sockets report is stale */
		if (elapsed(waited, (unsigned long)wait + PEER_TIMEOUT))
		{
			network_resumed(p2p_now() - waited);
			read_count = write_count = 0;
		}

		/* what is ready (the lists now hold only ready sockets, in the order
		asked, so each one's owner is found going along both); a socket
		closed meanwhile is not its owner's any more */
		for (index = 0, asked = 0; index < read_count; index++)
		{
			int socket = read[index];
			int owner, entry;

			while (asked < asked_read_count && asked_read[asked] != socket)
				asked++;
			if (asked == asked_read_count)
				break;
			owner = read_owners[asked++];
			entry = owner >> 8;
			switch (owner & 255)
			{
			case _owner_tunnel:
#ifdef HALO_PROFILE
				profile_trace_begin(p2p_profile_names.tunnel_receive);
#endif
				tunnel_readable();
#ifdef HALO_PROFILE
				profile_trace_end(p2p_profile_names.tunnel_receive);
#endif
				break;
			case _owner_handoff:
				if (socket == p2p.handoff_socket)
					handoff_readable();
				break;
			case _owner_proxy:
				if (p2p.proxies[entry].socket == socket)
					proxy_readable(&p2p.proxies[entry]);
				break;
			case _owner_listener:
				if (p2p.listeners[entry].socket == socket)
					listener_readable(&p2p.listeners[entry]);
				break;
			case _owner_stream:
				if (p2p.streams[entry].used && p2p.streams[entry].socket == socket)
					stream_readable(&p2p.streams[entry]);
				break;
			}
		}
#ifdef HALO_PROFILE
		profile_trace_begin(p2p_profile_names.streams);
#endif
		for (index = 0, asked = 0; index < write_count; index++)
		{
			int socket = write[index];
			int entry;

			while (asked < asked_write_count && asked_write[asked] != socket)
				asked++;
			if (asked == asked_write_count)
				break;
			entry = write_owners[asked] >> 8;
			if ((write_owners[asked++] & 255) == _owner_stream && p2p.streams[entry].used &&
				p2p.streams[entry].socket == socket)
			{
				stream_writeable(&p2p.streams[entry]);
			}
		}
		/* a connection to the game that failed is in neither list */
		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			struct stream *stream = &p2p.streams[index];

			if (stream->used && stream->state == _stream_connecting && elapsed(stream->created_time, 5000))
				stream_local_closed(stream);
		}
#ifdef HALO_PROFILE
		profile_trace_end(p2p_profile_names.streams);
#endif
		p2p_signal_update(read, read_count, write, write_count);

#ifdef HALO_PROFILE
		profile_trace_begin(p2p_profile_names.kcp_update);
#endif
		for (index = 0; index < MAXIMUM_STREAMS; index++)
		{
			if (p2p.streams[index].used)
				stream_update(&p2p.streams[index]);
		}
#ifdef HALO_PROFILE
		profile_trace_end(p2p_profile_names.kcp_update);
#endif
		update_peers();
		update_probes();
		expire_proxies();
		public_address_update();
		stun_update();
		update_hosting();
		/* the server browser (p2p_lobby.c): signalling while browsing too */
		if (p2p_lobby_browsing())
		{
			p2p_signal_start();
			/* (this machine's public address asked now, not when a game is
			joined: a join's request waits for it) */
			p2p.stun_started = 1;
		}
		{
			int count, maximum;

			hosted_player_counts(&count, &maximum);
			p2p_lobby_update(p2p.hosting && p2p.has_token ? p2p.token : NULL, count, maximum);
			if (p2p_lobby_listed())
				p2p.token_listed = 1;
		}
		/* a party (p2p_party.c): signalling while in one */
		p2p_party_update();
		/* friends (p2p_friends.c): signalling while a profile has an
		identity */
		p2p_friends_update();
		if ((p2p_lobby_listed() || p2p_party_active() || p2p_friends_publishing()) && !p2p.lobby_quit_registered)
		{
			p2p.lobby_quit_registered = 1;
			atexit(lobby_quit);
		}
		update_joining();
		update_answering();
		update_reaching();
		update_upnp();
		update_relays();
		update_front();
		p2p_discord_update();
#ifdef HALO_ANDROID
		poll_invite_file();
#endif
#ifdef HALO_MACOS
		poll_app_links();
#endif
#ifdef HALO_PROFILE
		profile_trace_end(p2p_profile_names.pass);
#endif
	}
	return NULL;
}

void p2p_initialize(unsigned long local_address)
{
	char invite[256];
	pthread_t thread;
	long tunnel_port = config_integer("network.tunnel_port");
	int index;

	p2p_identifier();
	if (p2p.running || !config_boolean("network.online"))
		return;
	if (tunnel_port < 0 || tunnel_port > 65535)
	{
		p2p_log("Internet play: network.tunnel_port %ld is not a port (0 to 65535); the game selects one",
			tunnel_port);
		tunnel_port = 0;
	}
	for (index = 0; index < MAXIMUM_PROXIES; index++)
		p2p.proxies[index].socket = -1;
	for (index = 0; index < MAXIMUM_LISTENERS; index++)
		p2p.listeners[index].socket = -1;
	for (index = 0; index < MAXIMUM_STREAMS; index++)
		p2p.streams[index].socket = -1;
	p2p.local_address = local_address;
	p2p.tunnel_socket = open_socket(SOCK_DGRAM, 0, network_short((unsigned short)tunnel_port), &p2p.tunnel_port);
	if (p2p.tunnel_socket < 0)
	{
		p2p_log("Internet play: cannot open its socket (network.tunnel_port %ld in use?); it is off",
			tunnel_port);
		return;
	}
	{
		/* all of a host's traffic, up to a 128-machine game's, goes through
		this one socket: buffers as large as the game's own sockets' (at
		least 1 MB, transport_endpoint_winsock.c) */
		int size = 1 << 20;

		posix_socket_setsockopt(p2p.tunnel_socket, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
		posix_socket_setsockopt(p2p.tunnel_socket, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	}
#ifndef HALO_ANDROID
	/* the first copy of the game takes the invites later ones are opened
	with */
	p2p.has_handoff_key = handoff_key(p2p.handoff_key);
	if (p2p.has_handoff_key)
		p2p.handoff_socket = open_socket(SOCK_DGRAM, network_long(0x7F000001), network_short(HANDOFF_PORT), NULL);
#endif
	stun_setup();
	if (pthread_create(&thread, NULL, p2p_thread, NULL) != 0)
	{
		close_socket(&p2p.tunnel_socket);
		close_socket(&p2p.handoff_socket);
		return;
	}
	pthread_detach(thread);
	p2p.running = 1;
	if (command_line_invite(invite, sizeof(invite)))
		p2p_join_invite(invite);
}
