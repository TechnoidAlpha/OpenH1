/*
P2P_INTERNAL.H

Shared internals of internet play (p2p.c, p2p_signal.c, p2p_crypto.c,
p2p_discord.c, p2p_lobby.c, p2p_party.c; see p2p.c for the design).
*/

#ifndef __HALO_LINUX_P2P_INTERNAL_H
#define __HALO_LINUX_P2P_INTERNAL_H

#include "p2p.h"

#include <pthread.h>

enum
{
	/* a machine's identifier: from the hash of its public key (which is new
	each run), and also its XNADDR's abEnet */
	P2P_IDENTIFIER_SIZE = 6,
	/* what an invite holds of the host's public key: the first bytes of its
	SHA-256 (of which the identifier is the first 6), so that no other key
	can be found to pass for it */
	P2P_KEY_HASH_SIZE = 16,
	/* an invite's secret */
	P2P_TOKEN_SIZE = 16,
	/* the addresses a machine offers to be reached at */
	P2P_MAXIMUM_CANDIDATES = 4,
	/* an invite link's text: P2P_JOIN_LINK_PREFIX, the host's key hash and
	the token in hexadecimal, and a terminator */
	P2P_LINK_SIZE = 14 + 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1,
	/* the most machines one tunnels to: a host and the rest of a system
	link game's 128 machines (include/halo_port_limits.h) */
	P2P_MAXIMUM_PEERS = 127,
	/* the other players of its game a machine asks to reach at once
	(p2p_reach_player) */
	P2P_MAXIMUM_PLAYER_JOINS = 8,
	/* the server browser's public games' hosts probed at once, for their
	ping (p2p_probe_start) */
	P2P_MAXIMUM_PROBES = 4,
};

typedef char p2p_player_key_hash_size_assert[P2P_PLAYER_KEY_HASH_SIZE == P2P_KEY_HASH_SIZE ? 1 : -1];

/* who offered a session (p2p_peer_offered): a joiner (to this machine,
hosting or answering its game's players), the invite's host, another player
of the host's game this machine asked for (p2p_reach_player), which answers
as a host does, or a public game's host probed for its ping
(p2p_probe_start) */
enum
{
	_p2p_offered_joiner,
	_p2p_offered_host,
	_p2p_offered_player,
	_p2p_offered_probe,
};

typedef char p2p_listing_invite_size_assert[P2P_LISTING_INVITE_SIZE == P2P_LINK_SIZE ? 1 : -1];
typedef char p2p_link_size_assert[P2P_LINK_SIZE == sizeof(P2P_JOIN_LINK_PREFIX) + 2 * (P2P_KEY_HASH_SIZE +
	P2P_TOKEN_SIZE) ? 1 : -1];

/* the prefix of a public game's slot (a key hash in hex follows), and the
topic of queries (p2p_lobby.c) */
#define P2P_LOBBY_SLOT_PREFIX "hceu/3/lobby/s/"
#define P2P_LOBBY_QUERY_TOPIC "hceu/3/lobby/q"

/* internet play's state (p2p.c), which the game's threads and the p2p
thread share */
extern pthread_mutex_t p2p_lock;

struct p2p_candidate
{
	/* network byte order */
	unsigned long address;
	unsigned short port;
};

/* ---------- p2p.c: what the signalling side calls back */

/* the milliseconds of a monotonic clock */
unsigned long p2p_now(void);
/* a line of internet play's log: the terminal's, and debug.txt's (with its
date, beside the game's own lines) */
void p2p_log(const char *format, ...);
/* looks up a host name (posix_resolve_ipv4), letting go of the p2p lock
while it waits; the p2p thread's */
unsigned long p2p_resolve(const char *host);
/* registers this executable for links of scheme (posix_register_url_scheme),
unless it is an automated run (debug.exit_after, a hidden window, no
renderer), which must not take the links over. The p2p thread's: it lets
go of the p2p lock while it may wait for a program */
void p2p_register_url_scheme(const char *scheme, const char *description);
/* drops the desktop's registration of this executable for links of scheme
(posix_forget_url_scheme), under the same conditions; the p2p thread's */
void p2p_forget_url_scheme(const char *scheme);
/* formats bytes as lower-case hexadecimal (text holds 2 * size + 1) */
void p2p_hex(const unsigned char *bytes, int size, char *text);
/* the addresses this machine can be reached at; returns their count */
int p2p_local_candidates(struct p2p_candidate *candidates, int maximum_count);
/* this run's X25519 public key (P2P_KEY_SIZE bytes), whose hash the
identifier is (p2p_identifier) */
const unsigned char *p2p_public_key(void);
/* this run's Ed25519 public key (the X25519 one's, p2p_ed25519_to_x25519),
and a signature with it */
const unsigned char *p2p_signing_key(void);
void p2p_sign(const void *message, int size, unsigned char *signature);
/* the identifier of the machine with this public key */
void p2p_identifier_for(const unsigned char *public_key, unsigned char *identifier);
/* the hash of a public key an invite holds (P2P_KEY_HASH_SIZE bytes), and
the identifier of the machine whose key has that hash */
void p2p_key_hash(const unsigned char *public_key, unsigned char *hash);
void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *identifier);
/* the X25519 secret this machine shares with the one with that public key;
0 if the key is unusable (one giving a known secret). The p2p thread's: it
lets go of the p2p lock while it works it out */
int p2p_shared_secret(const unsigned char *public_key, unsigned char *shared);
/* a joiner (on the host) or the host (on a joiner) offered its addresses
through signalling, with its public key and the secret of a
session (P2P_SHA256_SIZE bytes) its tunnel's keys come from; the tunnel
starts reaching it. offered is who it is (_p2p_offered_*). Returns 0 if it
was turned away: another session with that machine lives (it must lapse
first), this one has ended, or there is no room */
int p2p_peer_offered(const unsigned char *identifier, const unsigned char *public_key, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count, int offered);
/* whether p2p_peer_offered would turn a new session with that machine away
now (it is this machine, a session with it lives, there is no room, or, as
a host, too many players are being reached): checked before its secret is
worked out */
int p2p_peer_turned_away(const unsigned char *identifier, int is_host);
/* ... more addresses of a machine whose session (that secret's) lives: 0 if
none does (a session that has ended is never taken up again: its keys'
packet numbers would start again) */
int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret,
	const struct p2p_candidate *candidates, int count);
/* an invite that arrived on the p2p thread (from Discord, or another copy
of the game) */
void p2p_invite_received(const char *text);
/* a new invite (token) for the game hosted, if its invite was listed in the
server browser (going private: those who saw it must not get in); under
p2p_lock */
void p2p_new_invite_if_listed(void);
/* the server browser's probe of a public game's host (its key's hash, its
listing's token): the tunnel to it made, timed and closed again, its ping
told p2p_lobby_probed (at once for this machine's own game, and a host the
tunnel reaches already); 0 if it cannot start now (no room, the public
address not known yet). The probes end as the browser closes. Under
p2p_lock, on the p2p thread */
int p2p_probe_start(const unsigned char *host_hash, const unsigned char *token);

/* the host's key hash and the token of the invite within text (a link, or
a code): 1 if it holds one, -1 if an older version's, else 0 */
int p2p_parse_invite(const char *text, unsigned char *host_hash, unsigned char *token);
/* text (a link) put on the clipboard, as a new invite is; under p2p_lock */
void p2p_offer_clipboard(const char *text);

/* ---------- p2p_party.c: parties */

/* the p2p thread's pass, under p2p_lock */
void p2p_party_update(void);
/* a message on the party's topic (sealed), under p2p_lock */
void p2p_party_heard(const unsigned char *payload, int size);
/* the game is quitting: the party left at once (the brokers' buckets
passed over); under p2p_lock */
void p2p_party_quit(void);
/* p2p_party_join_link under p2p_lock (an invite that arrived on the p2p
thread) */
int p2p_party_join_link_locked(const char *text);
/* whether this machine is in a party (or joining one); under p2p_lock */
int p2p_party_active(void);
/* what Discord shows of the party (p2p_discord.c): in one (or joining
one), its members, its link, the id it is shown by (its topic's digits,
which let no one in), and where this machine is (_p2p_party_place_*, as
the game said, in a party or not) */
struct p2p_party_presence
{
	int active;
	int count;
	char link[P2P_PARTY_LINK_SIZE];
	char id[2 * 16 + 1];
	int place;
};
void p2p_party_presence(struct p2p_party_presence *presence);
/* out of a party, the party Discord offers (the spare: made when first
asked for), its link and id; made a party, this machine its leader (a
friend let in, or one given the link arriving: p2p_party_standby listens
on its topic meanwhile). Under p2p_lock */
void p2p_party_spare(char *link, int link_size, char *id, int id_size);
void p2p_party_spare_lead(void);
void p2p_party_standby(int on);
/* ... and listened on while friends may join it (p2p_friends.c: the
presence shared), as while Discord offers it */
void p2p_party_friends_standby(int on);

/* ---------- p2p_friends.c: friends */

enum
{
	/* a topic's most, with its end (hceu/3/f/ and 32 hex digits: 42) */
	P2P_TOPIC_SIZE = 64,
	/* the friends' topics listened on: the profile's inbox and each
	friend's presence */
	P2P_FRIENDS_TOPICS = 1 + P2P_FRIENDS_MAXIMUM,
};

/* the p2p thread's pass, under p2p_lock */
void p2p_friends_update(void);
/* a message on one of the friends' topics, under p2p_lock */
void p2p_friends_heard(const char *topic, const unsigned char *payload, int size);
/* the game is quitting: the file written, an OFFLINE published at once;
under p2p_lock. Whether the presence is published (an OFFLINE is due as
the game quits) */
void p2p_friends_quit(void);
int p2p_friends_publishing(void);
/* (tools/p2p_friends_check.c's) the code of a key; friends.txt's text read
into the friends (returns whether it held an identity), and written (its
size, 0 if it does not fit); the list's order */
void p2p_friend_code_of(const unsigned char *key, unsigned char *code);
int p2p_friends_parse(const char *text, int size);
int p2p_friends_format(char *text, int size);
int p2p_friends_compare(const struct p2p_friend *first, const struct p2p_friend *second);
/* seconds and milliseconds since 1970 (p2p.c) */
long long p2p_wall_seconds(void);
unsigned long long p2p_wall_milliseconds(void);
/* the token of the game hosted was listed elsewhere than the brokers (a
master server's listing: p2p_lobby_host_listing), so going private makes a
new invite too; under p2p_lock */
void p2p_token_listed(const unsigned char *token);
/* the tunnel's socket's port (network byte order): a master server's
listing of a dedicated server (server_master.c) */
unsigned short p2p_tunnel_local_port(void);

/* ---------- p2p_signal.c: signalling through public MQTT brokers */

/* connects to the brokers, if not already; called from the p2p thread */
void p2p_signal_start(void);
/* adds the signalling sockets to the p2p thread's select lists */
void p2p_signal_select_sets(int *read, int *read_count, int *write, int *write_count, int maximum_count);
/* services the sockets and timers; called from the p2p thread each pass */
void p2p_signal_update(const int *read, int read_count, const int *write, int write_count);
/* hosting: listen for joiners who hold this token */
void p2p_signal_host(const unsigned char *token);
void p2p_signal_stop_hosting(void);
/* joining: ask the host whose public key has this hash (p2p_key_hash),
holding this token, until it answers (or p2p_signal_stop_joining); each call
asks anew, with a new nonce (as after the session with the host ended
before the tunnel reached it: the host makes one session of a request) */
void p2p_signal_join(const unsigned char *host_hash, const unsigned char *token);
void p2p_signal_stop_joining(void);
/* ... and another player of the host's game (whose key has this hash, as
the host told), which answers holders of the token as its host does
(p2p_signal_host with the token it joined with), alongside the request to
the host; 0 if there is no room, or the request to the host is of another
token. Stopped by its identifier */
int p2p_signal_join_player(const unsigned char *player_hash, const unsigned char *token);
void p2p_signal_stop_joining_player(const unsigned char *identifier);
/* ... and a public game's host (whose key has this hash) with its listing's
token, only to time the tunnel to it (p2p_probe_start): a request of its own
token, whose answers come to a topic of their own; 0 if there is no room.
Stopped by its identifier */
int p2p_signal_probe(const unsigned char *host_hash, const unsigned char *token);
void p2p_signal_stop_probe(const unsigned char *identifier);
/* whether any broker is connected */
int p2p_signal_connected(void);
/* every broker's connection made anew at once: after the p2p thread was
stopped (the process was suspended, or the machine slept), the old ones
may carry nothing, and would be noticed only at their silence timeout */
void p2p_signal_reconnect(void);
/* the server browser's topics: the own slot and the queries (a listed
game), and every slot (browsing) */
void p2p_signal_lobby_topics(int listed, int browsing);
/* publishes a listing to the own slot on every broker, retained, and again
on each broker that connects later; closing: a tombstone, after which the
slot is cleared and nothing is published again */
void p2p_signal_lobby_publish(const unsigned char *listing, int size, int closing);
/* asks the hosts to publish again */
void p2p_signal_lobby_query(void);
/* the game is quitting: the tombstone published (p2p_signal_lobby_publish)
and the slot cleared on every ready broker now, whatever their buckets, and
the connections closed cleanly */
void p2p_signal_lobby_quit(void);
/* the party's topic (p2p_party.c; empty: none) subscribed to, and a
message published on it on every broker, at most once: within each
broker's bucket, or (urgent: the game is quitting) at once */
void p2p_signal_party(const char *topic);
void p2p_signal_party_publish(const unsigned char *payload, int size, int urgent);
/* the friends' topics (p2p_friends.c: count of them, the rest none)
subscribed to, a few at a time; and a message published on any topic (a
friend's inbox, the own presence) on every broker, at most once: within
each broker's bucket, or (urgent) at once */
void p2p_signal_friends(const char (*topics)[P2P_TOPIC_SIZE], int count);
void p2p_signal_friends_publish(const char *topic, const unsigned char *payload, int size, int urgent);
/* the relays this machine offers a peer whose session has not met (p2p.c:
body, size bytes, p2p_relay_offer_write's), sealed as its session's
requests are: to the machine it asked (to_host: a join's host, or a player
answering as one), or to its joiner; a message of its own ('R'), which
OpenCE and older builds pass over. p2p_relay_offer_heard (p2p.c) is given
the other's */
void p2p_signal_relay_offer(const unsigned char *peer_identifier, int to_host, const unsigned char *body, int size);
void p2p_relay_offer_heard(const unsigned char *body, int size);

/* ---------- p2p_crypto.c */

enum
{
	P2P_SHA256_SIZE = 32,
	/* an X25519 secret or public key, and a shared secret */
	P2P_KEY_SIZE = 32,
	/* ChaCha20-Poly1305's nonce and tag */
	P2P_NONCE_SIZE = 12,
	P2P_TAG_SIZE = 16,
	/* what p2p_seal adds: a random nonce and the tag */
	P2P_SEAL_OVERHEAD = P2P_NONCE_SIZE + P2P_TAG_SIZE,
};

void p2p_sha256(const void *data, int size, unsigned char *digest);
/* the same of data given a piece at a time (a release's download,
updater.c) */
struct p2p_sha256
{
	unsigned int state[8];
	unsigned char block[64];
	unsigned long long length;
	int used;
};
void p2p_sha256_begin(struct p2p_sha256 *context);
void p2p_sha256_add(struct p2p_sha256 *context, const void *data, int size);
void p2p_sha256_end(struct p2p_sha256 *context, unsigned char *digest);
void p2p_hmac_sha256(const unsigned char *key, int key_size, const void *data, int size, unsigned char *digest);
/* ChaCha20-Poly1305: encrypts plaintext with a 32-byte key and a nonce used
with that key once, and authenticates it and the additional data, into
sealed (size + P2P_TAG_SIZE bytes); returns the sealed size */
int p2p_aead_seal(const unsigned char *key, const unsigned char *nonce, const void *additional,
	int additional_size, const void *plaintext, int size, unsigned char *sealed);
/* the reverse: the plaintext size, or -1 if sealed (or the additional data)
was not made so or was altered */
int p2p_aead_open(const unsigned char *key, const unsigned char *nonce, const void *additional,
	int additional_size, const unsigned char *sealed, int size, unsigned char *plaintext);
/* the same with a random nonce ahead of the ciphertext (size +
P2P_SEAL_OVERHEAD bytes) and no additional data */
int p2p_seal(const unsigned char *key, const void *plaintext, int size, unsigned char *sealed);
int p2p_open(const unsigned char *key, const unsigned char *sealed, int size, unsigned char *plaintext);
/* whether two byte strings are the same, compared in constant time */
int p2p_equal(const void *first, const void *second, int size);
/* X25519: scalar times point (NULL: the base point, which gives the public
key of the secret key scalar) */
void p2p_x25519(unsigned char *result, const unsigned char *scalar, const unsigned char *point);

enum
{
	/* an Ed25519 seed (a run's key comes from one) and signature */
	P2P_SEED_SIZE = 32,
	P2P_SIGNATURE_SIZE = 64,
	P2P_SHA512_SIZE = 64,
};

void p2p_sha512(const void *data, int size, unsigned char *digest);
/* Ed25519 (with SHA-512): a seed's public key (P2P_KEY_SIZE bytes) and,
unless NULL, its X25519 secret key (the scalar it signs with, whose X25519
public key is p2p_ed25519_to_x25519 of the Ed25519 one) */
void p2p_ed25519_public(const unsigned char *seed, unsigned char *public_key, unsigned char *x25519_secret);
void p2p_ed25519_sign(const unsigned char *seed, const unsigned char *public_key, const void *message, int size,
	unsigned char *signature);
/* whether the signature is the key's, of the message (never for a key of
small order, which anyone can sign for) */
int p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size,
	const unsigned char *signature);
/* the X25519 public key of an Ed25519 one; 0 if it has a small order */
int p2p_ed25519_to_x25519(const unsigned char *public_key, unsigned char *x25519_public);

enum
{
	/* a password-protected listing's token, sealed (p2p_seal_token): its
	nonce (24), its tag (16), then the token sealed */
	P2P_PASSWORD_KEY_SIZE = 32,
	P2P_SEALED_TOKEN_SIZE = 24 + 16 + 16,
};

/* the key of a password (Argon2id: P2P_PASSWORD_KEY_SIZE bytes), for the
host whose Ed25519 key salt is (P2P_KEY_SIZE bytes): it takes a few
milliseconds, so that guessing passwords at a listing takes long */
void p2p_password_key(const char *password, const unsigned char *salt, unsigned char *key);
/* a token (P2P_TOKEN_SIZE bytes) sealed with a password's key, bound to the
host's Ed25519 key (P2P_SEALED_TOKEN_SIZE bytes); and opened: 0 if the key
is not the one it was sealed with (a wrong password), or it was altered */
void p2p_seal_token(const unsigned char *key, const unsigned char *signing_key, const unsigned char *token,
	unsigned char *sealed);
int p2p_unseal_token(const unsigned char *key, const unsigned char *signing_key, const unsigned char *sealed,
	unsigned char *token);

/* ---------- p2p_lobby.c: public games' listings */

/* the p2p thread's pass: the token of the game hosted for the internet
(NULL if none) and its player counts */
void p2p_lobby_update(const unsigned char *token, int player_count, int maximum_player_count);
/* whether the game hosted is listed now, and whether the browser is open */
int p2p_lobby_listed(void);
int p2p_lobby_browsing(void);
/* a message on a slot (its key hash in hex) through a broker; retained: the
slot's retained copy, sent on subscribing */
void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained);
/* a query was heard */
void p2p_lobby_query_heard(void);
/* a probe of a host (its identifier) ended: its ping in milliseconds, -1 if
it was not reached; under p2p_lock */
void p2p_lobby_probed(const unsigned char *identifier, int ping);
/* the game is quitting: a listed game's tombstone, and its slot cleared,
written to the brokers at once; under p2p_lock */
void p2p_lobby_quit(void);
/* a key hash's slot */
void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size);

/* ---------- p2p_discord.c: rich presence and invites through the Discord
desktop client */

/* called from the p2p thread each pass */
void p2p_discord_update(void);
/* the Discord user signed in, as told (empty if none): under p2p_lock */
void p2p_discord_user(char *id, int id_size, char *name, int name_size);
/* what to show: hosting with an invite link's secret and player counts, or
not (secret NULL); under p2p_lock */
void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count);
/* the game this machine is in, as the game told (p2p_discord_set_game): its
words (UTF-8, empty: none), whether it hosts it and it is under way, its
players; under p2p_lock */
void p2p_discord_game(int *hosting, int *playing, char *description, int size, int *player_count,
	int *maximum_player_count);

/* ---------- p2p_relay.c: relays, for two machines whose NATs keep them
apart, on machines this project's players chose to trust with their
addresses (a dedicated server, or a relay of halo --relay) */

enum
{
	/* the first byte of a relay's packets on the tunnel's socket, and of
	the tunnel's own (p2p.c's TUNNEL_MAGIC), whose packets alone a relay
	carries, of these sizes (its header, a tag and a byte at least; the
	largest inner packet at most) */
	P2P_RELAY_MAGIC = 0x6A,
	P2P_TUNNEL_MAGIC = 0x69,
	P2P_TUNNEL_MINIMUM_PACKET = 1 + P2P_IDENTIFIER_SIZE + 8 + 16 + 1,
	P2P_TUNNEL_MAXIMUM_PACKET = 1 + P2P_IDENTIFIER_SIZE + 8 + 1400 + 16,
	/* what both machines of a session show one relay (p2p_relay_token) */
	P2P_RELAY_TOKEN_SIZE = 32,
	/* the pairs a relay keeps at most, and the attempts a machine makes */
	P2P_RELAY_MAXIMUM_PAIRS = 256,
	P2P_RELAY_MAXIMUM_ATTEMPTS = 64,
	/* the relays one machine offers a peer at once, and those tried for a
	peer at most (both machines' offers) */
	P2P_RELAY_OFFERED = 3,
	P2P_RELAY_PEER_RELAYS = 2 * P2P_RELAY_OFFERED,
	/* an offer (p2p_relay_offer_write): the offering machine's identifier,
	its side, the count, each relay's address and port, and a tag */
	P2P_RELAY_OFFER_ENTRY_SIZE = 6,
	P2P_RELAY_OFFER_TAG_SIZE = 16,
	P2P_RELAY_OFFER_HEADER_SIZE = P2P_IDENTIFIER_SIZE + 2,
	P2P_RELAY_OFFER_MAXIMUM_SIZE = P2P_RELAY_OFFER_HEADER_SIZE + P2P_RELAY_OFFERED * P2P_RELAY_OFFER_ENTRY_SIZE +
		P2P_RELAY_OFFER_TAG_SIZE,
};

/* for whom this machine relays (p2p_relay_limits): no one (a player's
machine, always); only pairs one side of which its tunnel reaches directly
(a dedicated server's own game's players: server.relay "game"); any two
machines (a relay of its region, named in players' relays.txt: "any", and
halo --relay) */
enum
{
	_p2p_relay_off,
	_p2p_relay_game,
	_p2p_relay_any,
};

/* how much this machine relays for others (p2p_relay_configure): for whom;
pairs in all and per address; binds a second per address and in all;
bytes a second in all, per pair, per address */
struct p2p_relay_limits
{
	int mode;
	int maximum_pairs;
	int pairs_per_address;
	int binds_per_second;
	int binds_in_all;
	long upload;
	long pair_upload;
	long address_upload;
};

/* the relay's counts (logged, and halo --relay's status) */
struct p2p_relay_counts
{
	int pairs, paired;
	unsigned long formed, ended, refused, challenged, dropped;
	unsigned long forwarded_packets, forwarded_bytes;
};

/* an attempt's state (p2p_relay_attempt) */
struct p2p_relay_attempt_state
{
	unsigned char peer[P2P_IDENTIFIER_SIZE];
	struct p2p_candidate relay;
	int side, paired;
	/* milliseconds, -1 until the relay answered */
	long bind_round_trip;
	/* (the host's side, 0) where the relay saw the other side's packets
	come from, which it checked the other side receives at (its
	return-routability check): zero until paired, and always on the
	joiner's side (the relay tells a joiner nothing of its host) */
	struct p2p_candidate peer_address;
};

/* whether relays are tried for a peer not reached directly, by how long
since its session was made (milliseconds) and whether both NATs are known
to be strict: after hole punching had its time (at once where it cannot
work) */
int p2p_relay_due(unsigned long age, int both_strict);
/* the token both machines of a session (its secret, P2P_SHA256_SIZE
bytes) show one relay (its address and port as offered): an HMAC of the
secret with a label and the relay's address, which gives away neither the
secret, nor the tunnel's keys, nor the token the same machines show any
other relay */
void p2p_relay_token(const unsigned char *secret, const struct p2p_candidate *relay, unsigned char *token);
/* the relay's limits (mode _p2p_relay_off: it relays nothing, its pairs
ended) */
void p2p_relay_configure(const struct p2p_relay_limits *limits);
void p2p_relay_counts(struct p2p_relay_counts *counts);
/* a packet of the tunnel's socket that starts with P2P_RELAY_MAGIC, from
address and port (network byte order): 1 (it was a relay's, done with),
else 0. The packet may be changed */
int p2p_relay_received(unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now);
/* the pass of the p2p thread: binds sent, pairs and attempts expired */
void p2p_relay_update(unsigned long now);
/* a relay tried for a peer's session (side: 0 the host's, 1 the
joiner's): the attempt's index, -1 if there is no room (P2P_RELAY_PEER_RELAYS
for one peer at most; the same relay again: the same attempt) */
int p2p_relay_attempt_start(const unsigned char *peer, const unsigned char *secret, int side,
	const struct p2p_candidate *relay, unsigned long now);
/* a peer's attempts given up (unpaired_only: those not paired alone),
each pair released */
void p2p_relay_forget_peer(const unsigned char *peer, int unpaired_only);
/* a tunnel packet sent through a paired attempt: 0 if it is not one */
int p2p_relay_send(int index, const unsigned char *packet, int size);
/* a front's path to a client of this machine (p2p_front.c): its attempt,
always paired (front: the front's public address; client: its number
there; client_address: where the front saw it, its peer_address). Found by
the client's number (-1 if that one is another peer's), else made (created
set; -1 if there is no room). Heard: a packet of the peer's came through
it. Dropped: freed, unheard of. Forget: the front's paths all freed */
int p2p_relay_front_attempt(const unsigned char *peer, const struct p2p_candidate *front, unsigned long client,
	const struct p2p_candidate *client_address, unsigned long now, int *created);
void p2p_relay_front_heard(int index, unsigned long now);
void p2p_relay_front_drop(int index);
void p2p_relay_front_forget(void);
/* an attempt's state: 0 if there is none of that index */
int p2p_relay_attempt(int index, struct p2p_relay_attempt_state *state);
/* a peer's attempts' indices; returns their count */
int p2p_relay_peer_attempts(const unsigned char *peer, int *indices, int maximum);

/* an offer of relays (sent through signalling: p2p_signal_relay_offer),
written for a peer (the offering machine's identifier and side, the
relays, count at most P2P_RELAY_OFFERED, and a tag with the session's
secret): its size. Read: the count of relays (-1 if it is not a whole,
well-tagged offer of that side's), into relays */
int p2p_relay_offer_write(const unsigned char *identifier, int side, const struct p2p_candidate *relays, int count,
	const unsigned char *secret, unsigned char *body);
int p2p_relay_offer_read(const unsigned char *body, int size, int side, const unsigned char *secret,
	struct p2p_candidate *relays);
/* the offers a peer sent, and when it was last heard: whether one more is
taken now (one a second, a few in all for a session) */
struct p2p_relay_offers_heard
{
	unsigned long time;
	int count;
};
int p2p_relay_offer_take(struct p2p_relay_offers_heard *heard, unsigned long now);
/* relays added to a peer's (count of them, P2P_RELAY_PEER_RELAYS at most;
none twice): returns how many were new */
int p2p_relay_merge(struct p2p_candidate *relays, int *count, const struct p2p_candidate *added, int added_count);

/* the path a peer is reached by: directly (carrier 0), through a relay's
attempt (its index + 1), or none while its relay is replaced (-1); when it
was last heard through it; a direct path heard while relayed (since when,
without a gap, and lately), which takes over only once it has lasted; when
it went direct from a relay (its relays kept a while, to go back to) */
struct p2p_relay_path
{
	int carrier;
	unsigned long carrier_heard;
	unsigned long direct_first, direct_last;
	unsigned long direct_time;
};
/* a packet of the peer's newest heard by a path (via: 0 directly, else an
attempt's index + 1): whether the peer's path is now that one. A direct
path takes over from a relay once heard for a few seconds without a gap; a
relay from another, or from a direct path, only once that one is silent */
int p2p_relay_path_heard(struct p2p_relay_path *path, int via, unsigned long now);
/* the carrier's silence (the peer's other paths are tried meanwhile), and
whether a direct path has lasted long enough for the relays to go */
int p2p_relay_path_silent(const struct p2p_relay_path *path, unsigned long now);
int p2p_relay_path_relays_done(const struct p2p_relay_path *path, unsigned long now);

/* p2p.c's, which p2p_relay.c calls: a packet sent from the tunnel's
socket; an attempt paired (the peer pinged through it at once); a tunnel
packet a relay brought through an attempt; an attempt no longer paired
(ended, or its relay lost the other side: a peer reached through it is
reached otherwise); whether a machine the tunnel reaches directly is at
this address and port */
void p2p_relay_sendto(unsigned long address, unsigned short port, const unsigned char *data, int size);
void p2p_relay_paired(int index);
void p2p_relay_delivered(int index, const unsigned char *packet, int size);
void p2p_relay_lost(int index);
int p2p_relay_peer_address(unsigned long address, unsigned short port);

/* ---------- p2p_front.c: fronts, public ports on a relay of halo --relay
that a server or host behind a NAT no one can punch through registers with
(network.front), listed as one more of its addresses: a client, OpenCE's
too, reaches it there as at any of its addresses */

enum
{
	/* the first byte of a front's packets on the tunnel's socket */
	P2P_FRONT_MAGIC = 0x6B,
	/* public ports a front gives out at most (server.front_ports) */
	P2P_FRONT_MAXIMUM_PORTS = 64,
	/* operators' keys a front allows at most */
	P2P_FRONT_MAXIMUM_OPERATORS = 64,
	/* an operator's (publisher) key, Ed25519, and a signature */
	P2P_FRONT_KEY_SIZE = 32,
	P2P_FRONT_SIGNATURE_SIZE = 64,
};

/* the front's ports (port_count of them from first_port, host byte order;
none: it is no front) and its bytes a second in all */
struct p2p_front_limits
{
	int port_count;
	unsigned short first_port;
	long upload;
};

/* the front's counts (halo --relay logs them) */
struct p2p_front_counts
{
	int registered, clients;
	unsigned long registrations, ended, refused, challenged, dropped;
	unsigned long forwarded_packets, forwarded_bytes;
};

/* the front's: its ports and rate; the operators' keys it allows (the
registrations of any other end at once), from a file's text (one
"ed25519:<64 hex>" or 64 hex digits a line: their count, maximum at most);
its counts; a packet that arrived at its public port of that index */
void p2p_front_configure(const struct p2p_front_limits *limits);
void p2p_front_operators(const unsigned char (*keys)[P2P_FRONT_KEY_SIZE], int count);
int p2p_front_operators_parse(const char *text, unsigned char (*keys)[P2P_FRONT_KEY_SIZE], int maximum);
void p2p_front_counts(struct p2p_front_counts *counts);
void p2p_front_public_received(int port_index, const unsigned char *packet, int size, unsigned long address,
	unsigned short port, unsigned long now);
/* both sides': a packet that starts with P2P_FRONT_MAGIC (the tunnel's
socket, or the relay's), from address and port (network byte order): 1 if
it was a front's, done with; the pass (registrations kept and expired) */
int p2p_front_received(unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now);
void p2p_front_update(unsigned long now);
/* the server's: the front to register with (NULL or zeros: none); its
public address once registered (1); a tunnel packet to one of its clients
(0 if not registered) */
void p2p_front_use(const struct p2p_candidate *front);
int p2p_front_public(struct p2p_candidate *candidate);
int p2p_front_send(unsigned long client, const unsigned char *packet, int size);

/* p2p.c's, which p2p_front.c calls: a packet sent (port_index -1: from the
tunnel's socket, or the relay's; else from the front's public port of that
index); the operator's public key (P2P_FRONT_KEY_SIZE bytes) and, given a
message, its signature: 0 if there is no key (server_master.c's publisher
key); a tunnel packet a client sent to this machine's front (its number
there, its address as the front saw it); this machine's registration
ended (the paths through it gone) */
void p2p_front_sendto(int port_index, unsigned long address, unsigned short port, const unsigned char *data,
	int size);
int p2p_front_sign(const unsigned char *message, int size, unsigned char *public_key, unsigned char *signature);
void p2p_front_delivered(unsigned long client, const struct p2p_candidate *client_address,
	const unsigned char *packet, int size);
void p2p_front_lost(void);

#endif
