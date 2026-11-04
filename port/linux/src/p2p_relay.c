/*
P2P_RELAY.C

Internet play's relays (p2p.c): two machines whose NATs keep them apart
(both give each destination a port of their own, or one does and the
other lets in only what answers it: hole punching cannot meet them) reach
each other through a third machine that both can reach. A relay is never a
player's machine: it is a dedicated server (server.relay: for its own
game's players, "game", or for anyone, "any") or a relay of its own
(halo --relay, on a small server), which the players name in relays.txt
(network.relays_file) or their game's dedicated server tells them of.
Relays are tried only once hole punching has had its time and failed
(p2p_relay_due); a direct path found later takes the session back.

The relay is the tunnel's UDP socket, its packets told from the tunnel's by
their first byte (P2P_RELAY_MAGIC). Two machines whose session cannot meet
directly each bind to the relay with a token that only they have: an HMAC
of their session's secret (p2p_signal.c's, from their X25519 keys and a
nonce of each, which never travels) with a label and the relay's address
(p2p_relay_token), so that each relay is shown a token of its own and
cannot use it at another. The relay pairs the two binds of one token, one
of each side of the session (the host's, the joiner's), and from then on
forwards between those two addresses alone, and only tunnel packets, which
the two seal with keys from the same secret under other labels: the relay
reads nothing of them, and cannot work the secret or the keys out of the
token. What the relay learns is the two machines' addresses.

- BIND (machine to relay): magic, 'B', version, side, the token, a nonce,
  and the relay's cookie (zero until it gave one). Sent every
  BIND_INTERVAL until bound and paired, then every KEEP_ALIVE_INTERVAL
  (which also keeps the NATs' mappings).
- CHALLENGE (relay to machine): magic, 'C', version, 0, the bind's nonce,
  a cookie: an HMAC, by a secret of the relay's, of the address and port
  the bind came from, its token and the time (COOKIE_TIME). A bind is
  taken only with a cookie the relay gave that address lately, so a bind
  sent in another's name (its source forged) binds nothing: the relay
  answers it with a challenge alone, smaller than the bind.
- BOUND (relay to machine): magic, 'K', version, state (waiting, paired,
  refused), the bind's nonce, the pair's channel (8 random bytes), and to
  the host's side (0) alone, once paired, the other side's address and port
  as the relay saw and checked them (the address a host's bans and votes
  take for a relayed player: p2p.c's p2p_peer_endpoint_address). No larger
  than a bind.
- DATA (machine to relay 'D', relay to machine 'd'): magic, the type, the
  channel, then one tunnel packet (P2P_TUNNEL_MAGIC, of a tunnel packet's
  sizes). The relay forwards a 'D' only from one of the pair's two
  addresses, to the other, as a 'd'; a 'd' is never forwarded again.
- RELEASE (machine to relay): magic, 'X', version, side, the token: the
  pair ends (from that side's own checked address only; and a token is one
  relay's, so no relay can end a pair at another).

The relay's limits (p2p_relay_limits): pairs in all and per address, binds
a second per address and in all, bytes a second in all, per pair and per
address; a side silent for SIDE_SILENCE is unbound, a pair idle for
IDLE_TIME ends, as does one unpaired for UNPAIRED_TIME. Everything it
receives is checked for its size and its fields before anything is done
with it.

The machines' side (the attempts): p2p.c asks for a relay with a peer's
session (p2p_relay_attempt_start), this file binds and keeps the binding,
and hands each tunnel packet the relay brings back to p2p.c
(p2p_relay_delivered); p2p.c sends through a paired attempt
(p2p_relay_send). Which path carries a peer, and when a direct one takes
over, is p2p_relay_path's. A front's path to a client of this machine
(p2p_front.c: a public port on a relay that passes the client's packets to
this machine and its answers back) is an attempt too, paired from the first
and never bound (p2p_relay_front_attempt): its sends go through the front,
and the client's address as the front saw it is its peer_address.
Everything here runs on the p2p thread, under p2p_lock (or halo --relay's
loop: p2p.c's p2p_relay_run).
*/

#include "platform.h"
#include "posix.h"
#include "p2p_internal.h"

#include <string.h>

enum
{
	/* 2: a cookie checks each bind's address; a token for each relay; the
	host's side told the joiner's address (the first was #309's, never
	released) */
	RELAY_VERSION = 2,
	RELAY_NONCE_SIZE = 8,
	RELAY_CHANNEL_SIZE = 8,
	RELAY_COOKIE_SIZE = 16,
	/* magic, 'B', version, side, token, nonce, cookie */
	BIND_SIZE = 4 + P2P_RELAY_TOKEN_SIZE + RELAY_NONCE_SIZE + RELAY_COOKIE_SIZE,
	/* magic, 'C', version, 0, nonce, cookie */
	CHALLENGE_SIZE = 4 + RELAY_NONCE_SIZE + RELAY_COOKIE_SIZE,
	/* magic, 'K', version, state, nonce, channel, the other side's address
	and port, two bytes zero */
	BOUND_SIZE = 4 + RELAY_NONCE_SIZE + RELAY_CHANNEL_SIZE + 8,
	/* magic, 'X', version, side, token */
	RELEASE_SIZE = 4 + P2P_RELAY_TOKEN_SIZE,
	/* magic, 'D' or 'd', channel; then a tunnel packet */
	DATA_HEADER_SIZE = 2 + RELAY_CHANNEL_SIZE,
	MINIMUM_DATA_SIZE = DATA_HEADER_SIZE + P2P_TUNNEL_MINIMUM_PACKET,
	MAXIMUM_DATA_SIZE = DATA_HEADER_SIZE + P2P_TUNNEL_MAXIMUM_PACKET,

	/* the relay's: its pairs and the addresses it keeps counts of */
	MAXIMUM_PAIRS = P2P_RELAY_MAXIMUM_PAIRS,
	MAXIMUM_ADDRESSES = 512,
	/* the machines' attempts: a few relays for each of a few peers */
	MAXIMUM_ATTEMPTS = P2P_RELAY_MAXIMUM_ATTEMPTS,

	/* milliseconds */
	BIND_INTERVAL = 500,
	KEEP_ALIVE_INTERVAL = 4000,
	/* a cookie is good in the period it was made in and the next */
	COOKIE_TIME = 30000,
	/* an attempt not paired in this long since it began (or since its
	relay lost the other side) is given up; a paired one that hears nothing
	from the relay in this long (its keep-alive answered, or a packet) is
	lost */
	ATTEMPT_TIMEOUT = 20000,
	ATTEMPT_SILENCE = 12000,
	/* a relay that refused an attempt is asked again after this long */
	REFUSED_TIME = 5000,
	/* the relay's: a side not heard from in this long is unbound; a pair
	no packet came for in this long ends; one unpaired for this long too; a
	side heard this lately keeps its address (another bind of its token
	from elsewhere is not taken: its NAT's mapping changes only after a
	silence) */
	SIDE_SILENCE = 15000,
	IDLE_TIME = 30000,
	UNPAIRED_TIME = 25000,
	REBIND_TIME = 5000,
	ADDRESS_FORGET_TIME = 60000,
	/* a bucket holds at most this long's refill (its burst) */
	BUCKET_BURST_TIME = 1000,
	/* a peer not reached in this long is tried through relays, sooner where
	both NATs are known strict (hole punching cannot meet them) */
	RELAY_DELAY = 4000,
	RELAY_STRICT_DELAY = 1500,
	/* a direct path heard while relayed takes over after this long without
	a gap of GAP; a carrier this silent is replaced (by a path heard since);
	a direct path's relays are released after it lasted this long */
	DIRECT_SWITCH_TIME = 5000,
	DIRECT_GAP = 2500,
	CARRIER_SILENCE = 2500,
	RELAYS_KEPT_TIME = 30000,
	/* the offers taken from one peer: one each this long, this many */
	OFFER_INTERVAL = 1000,
	OFFERS_PER_SESSION = 8,
};

enum
{
	_type_bind = 'B',
	_type_challenge = 'C',
	_type_bound = 'K',
	_type_data = 'D',
	_type_delivered = 'd',
	_type_release = 'X',
};

enum
{
	_bound_waiting,
	_bound_paired,
	_bound_refused,
};

enum
{
	_attempt_binding,
	_attempt_waiting,
	_attempt_paired,
	_attempt_refused,
};

/* spent from, and refilled at rate a second; in thousandths of its units
(bytes, or binds), so that a refill of a few milliseconds is not rounded
away */
struct bucket
{
	long long left;
	unsigned long time;
	int started;
};

struct side
{
	int bound;
	unsigned long address;
	unsigned short port;
	unsigned long heard_time;
	/* its latest bind's nonce, which its BOUND carries back */
	unsigned char nonce[RELAY_NONCE_SIZE];
};

struct pair
{
	int used;
	unsigned char token[P2P_RELAY_TOKEN_SIZE];
	unsigned char channel[RELAY_CHANNEL_SIZE];
	struct side sides[2];
	/* when it was made, or last lost a side; its last packet or bind */
	unsigned long unpaired_time;
	unsigned long active_time;
	struct bucket bytes;
	unsigned long forwarded;
};

/* an address the relay hears from: its binds and bytes */
struct heard_address
{
	int used;
	unsigned long address;
	unsigned long heard_time;
	struct bucket binds;
	struct bucket bytes;
};

struct attempt
{
	int used;
	unsigned char peer[P2P_IDENTIFIER_SIZE];
	int side;
	unsigned char token[P2P_RELAY_TOKEN_SIZE];
	struct p2p_candidate relay;
	unsigned char nonce[RELAY_NONCE_SIZE];
	unsigned char cookie[RELAY_COOKIE_SIZE];
	unsigned char channel[RELAY_CHANNEL_SIZE];
	int state;
	/* when it began, or was last told its pair lost the other side: its
	timeout runs from then */
	unsigned long state_time;
	unsigned long sent_time;
	unsigned long heard_time;
	/* the first bind's round trip to the relay, -1 until it answered */
	long bind_round_trip;
	unsigned long first_sent_time;
	struct p2p_candidate peer_address;
	int retried;
	/* a front's path to a client of this machine (p2p_front.c): paired from
	the first, with no binds; the client's number at the front */
	int front;
	unsigned long client;
};

static struct
{
	struct p2p_relay_limits limits;
	unsigned char secret[P2P_SHA256_SIZE];
	int has_secret;
	struct pair pairs[MAXIMUM_PAIRS];
	struct heard_address addresses[MAXIMUM_ADDRESSES];
	struct bucket bytes;
	struct bucket binds;
	struct p2p_relay_counts counts;

	struct attempt attempts[MAXIMUM_ATTEMPTS];
} relay;

static int relay_elapsed(unsigned long now, unsigned long since, unsigned long time)
{
	return (unsigned long)(now - since) >= time;
}

/* ---------- buckets */

/* whether amount can be spent at rate a second (0: none can), spending it */
static int bucket_spend(struct bucket *bucket, long rate, long amount, unsigned long now)
{
	/* (thousandths: a rate a second, by milliseconds, is thousandths of a
	unit, in 64 bits: a dedicated server's rate in bytes, times a thousand,
	is past a 32-bit long) */
	long long burst = (long long)rate * BUCKET_BURST_TIME;
	long long cost = (long long)amount * 1000;
	unsigned long gone;

	if (rate <= 0)
		return 0;
	if (burst < cost)
		burst = cost;
	if (!bucket->started)
	{
		bucket->left = burst;
		bucket->time = now;
		bucket->started = 1;
	}
	gone = now - bucket->time;
	/* (refilled for no more than the burst's time: no overflow) */
	if (gone > BUCKET_BURST_TIME)
		gone = BUCKET_BURST_TIME;
	bucket->left += (long long)rate * (long long)gone;
	if (bucket->left > burst)
		bucket->left = burst;
	bucket->time = now;
	if (bucket->left < cost)
		return 0;
	bucket->left -= cost;
	return 1;
}

/* ---------- when */

int p2p_relay_due(unsigned long age, int both_strict)
{
	return age >= (unsigned long)(both_strict ? RELAY_STRICT_DELAY : RELAY_DELAY);
}

/* ---------- the token */

void p2p_relay_token(const unsigned char *secret, const struct p2p_candidate *relay_address, unsigned char *token)
{
	static const char label[] = "openh1 relay token 2";
	unsigned char data[sizeof(label) - 1 + 6];

	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, &relay_address->address, 4);
	memcpy(data + sizeof(label) - 1 + 4, &relay_address->port, 2);
	p2p_hmac_sha256(secret, P2P_SHA256_SIZE, data, (int)sizeof(data), token);
}

/* ---------- the relay */

void p2p_relay_configure(const struct p2p_relay_limits *limits)
{
	int changed = memcmp(&relay.limits, limits, sizeof(*limits)) != 0;

	relay.limits = *limits;
	if (relay.limits.maximum_pairs > MAXIMUM_PAIRS)
		relay.limits.maximum_pairs = MAXIMUM_PAIRS;
	if (relay.limits.maximum_pairs < 0)
		relay.limits.maximum_pairs = 0;
	if (!relay.has_secret)
	{
		posix_random_bytes(relay.secret, sizeof(relay.secret));
		relay.has_secret = 1;
	}
	/* (turned off: its pairs end at once) */
	if (changed && relay.limits.mode == _p2p_relay_off)
	{
		memset(relay.pairs, 0, sizeof(relay.pairs));
		memset(relay.addresses, 0, sizeof(relay.addresses));
	}
}

void p2p_relay_counts(struct p2p_relay_counts *counts)
{
	int index;

	*counts = relay.counts;
	counts->pairs = 0;
	counts->paired = 0;
	for (index = 0; index < MAXIMUM_PAIRS; index++)
	{
		if (!relay.pairs[index].used)
			continue;
		counts->pairs++;
		counts->paired += relay.pairs[index].sides[0].bound && relay.pairs[index].sides[1].bound;
	}
}

static struct heard_address *heard_address(unsigned long address, unsigned long now)
{
	struct heard_address *free_entry = NULL;
	struct heard_address *stalest = NULL;
	int index;

	for (index = 0; index < MAXIMUM_ADDRESSES; index++)
	{
		struct heard_address *entry = &relay.addresses[index];

		if (entry->used && entry->address == address)
			return entry;
		if (!entry->used && !free_entry)
			free_entry = entry;
		else if (entry->used && (!stalest || (long)(entry->heard_time - stalest->heard_time) < 0))
			stalest = entry;
	}
	/* (full: the address heard from longest ago is forgotten; its counts
	start over, which only lets an address spend a little sooner) */
	if (!free_entry)
		free_entry = stalest;
	memset(free_entry, 0, sizeof(*free_entry));
	free_entry->used = 1;
	free_entry->address = address;
	free_entry->heard_time = now;
	return free_entry;
}

/* the pairs a side of which is at address */
static int pairs_of_address(unsigned long address)
{
	int count = 0;
	int index;

	for (index = 0; index < MAXIMUM_PAIRS; index++)
	{
		struct pair const *pair = &relay.pairs[index];

		if (pair->used && ((pair->sides[0].bound && pair->sides[0].address == address) ||
			(pair->sides[1].bound && pair->sides[1].address == address)))
		{
			count++;
		}
	}
	return count;
}

static int pairs_used(void)
{
	int count = 0;
	int index;

	for (index = 0; index < MAXIMUM_PAIRS; index++)
		count += relay.pairs[index].used;
	return count;
}

static struct pair *pair_of_token(const unsigned char *token)
{
	int index;

	for (index = 0; index < MAXIMUM_PAIRS; index++)
	{
		if (relay.pairs[index].used && p2p_equal(relay.pairs[index].token, token, P2P_RELAY_TOKEN_SIZE))
			return &relay.pairs[index];
	}
	return NULL;
}

static struct pair *pair_of_channel(const unsigned char *channel)
{
	int index;

	for (index = 0; index < MAXIMUM_PAIRS; index++)
	{
		if (relay.pairs[index].used && !memcmp(relay.pairs[index].channel, channel, RELAY_CHANNEL_SIZE))
			return &relay.pairs[index];
	}
	return NULL;
}

/* the cookie for a bind of token from address and port, in the period of
the time when */
static void make_cookie(unsigned long address, unsigned short port, const unsigned char *token, unsigned long when,
	unsigned char *cookie)
{
	unsigned char data[4 + 4 + 2 + P2P_RELAY_TOKEN_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];
	unsigned long period = when / COOKIE_TIME;

	data[0] = (unsigned char)period;
	data[1] = (unsigned char)(period >> 8);
	data[2] = (unsigned char)(period >> 16);
	data[3] = (unsigned char)(period >> 24);
	memcpy(data + 4, &address, 4);
	memcpy(data + 8, &port, 2);
	memcpy(data + 10, token, P2P_RELAY_TOKEN_SIZE);
	p2p_hmac_sha256(relay.secret, P2P_SHA256_SIZE, data, (int)sizeof(data), digest);
	memcpy(cookie, digest, RELAY_COOKIE_SIZE);
}

/* whether a bind's cookie is the relay's for its address, of this period
or the last */
static int cookie_good(unsigned long address, unsigned short port, const unsigned char *token,
	const unsigned char *cookie, unsigned long now)
{
	unsigned char expected[RELAY_COOKIE_SIZE];

	make_cookie(address, port, token, now, expected);
	if (p2p_equal(expected, cookie, RELAY_COOKIE_SIZE))
		return 1;
	make_cookie(address, port, token, now - COOKIE_TIME, expected);
	return p2p_equal(expected, cookie, RELAY_COOKIE_SIZE);
}

static void send_challenge(unsigned long address, unsigned short port, const unsigned char *token,
	const unsigned char *nonce, unsigned long now)
{
	unsigned char packet[CHALLENGE_SIZE];

	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = _type_challenge;
	packet[2] = RELAY_VERSION;
	packet[3] = 0;
	memcpy(packet + 4, nonce, RELAY_NONCE_SIZE);
	make_cookie(address, port, token, now, packet + 4 + RELAY_NONCE_SIZE);
	relay.counts.challenged++;
	p2p_relay_sendto(address, port, packet, CHALLENGE_SIZE);
}

/* a BOUND to a side: its state, its bind's nonce, the pair's channel (none
when refused), and, to the host's side of a pair, where the other side is */
static void send_bound(unsigned long address, unsigned short port, int state, const unsigned char *nonce,
	const unsigned char *channel, const struct side *other)
{
	unsigned char packet[BOUND_SIZE];

	memset(packet, 0, sizeof(packet));
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = _type_bound;
	packet[2] = RELAY_VERSION;
	packet[3] = (unsigned char)state;
	memcpy(packet + 4, nonce, RELAY_NONCE_SIZE);
	if (channel)
		memcpy(packet + 4 + RELAY_NONCE_SIZE, channel, RELAY_CHANNEL_SIZE);
	if (other && other->bound)
	{
		memcpy(packet + 4 + RELAY_NONCE_SIZE + RELAY_CHANNEL_SIZE, &other->address, 4);
		memcpy(packet + 4 + RELAY_NONCE_SIZE + RELAY_CHANNEL_SIZE + 4, &other->port, 2);
	}
	p2p_relay_sendto(address, port, packet, BOUND_SIZE);
}

/* a pair's BOUND to one of its sides (0 or 1): the other side's address
to the host's side alone */
static void send_pair_bound(struct pair const *pair, int side_index)
{
	struct side const *side = &pair->sides[side_index];
	struct side const *other = &pair->sides[!side_index];

	send_bound(side->address, side->port, other->bound ? _bound_paired : _bound_waiting, side->nonce, pair->channel,
		side_index == 0 ? other : NULL);
}

static void pair_free(struct pair *pair, const char *why)
{
	if (pair->sides[0].bound && pair->sides[1].bound)
	{
		p2p_log("Internet play: relay: a pair ended (%s; %lu bytes forwarded; the relay's packets dropped by its "
			"limits so far: %lu)", why, pair->forwarded, relay.counts.dropped);
		relay.counts.ended++;
	}
	memset(pair, 0, sizeof(*pair));
}

static void bind_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	const unsigned char *token = packet + 4;
	const unsigned char *nonce = token + P2P_RELAY_TOKEN_SIZE;
	const unsigned char *cookie = nonce + RELAY_NONCE_SIZE;
	struct heard_address *heard;
	struct pair *pair;
	struct side *side;
	int side_index;

	if (size != BIND_SIZE || packet[2] != RELAY_VERSION || packet[3] > 1)
		return;
	side_index = packet[3];
	heard = heard_address(address, now);
	heard->heard_time = now;
	/* (a few a second from an address, and so many in all: a bind is
	answered, so a flood of them sent in another's name reflects at most
	this to it, each answer smaller than the bind) */
	if (!bucket_spend(&heard->binds, relay.limits.binds_per_second, 1, now) ||
		!bucket_spend(&relay.binds, relay.limits.binds_in_all, 1, now))
	{
		relay.counts.refused++;
		return;
	}
	/* (the address's own cookie, which only the address could have been
	given: else a challenge, and nothing more) */
	if (!cookie_good(address, port, token, cookie, now))
	{
		send_challenge(address, port, token, nonce, now);
		return;
	}
	pair = pair_of_token(token);
	if (pair)
	{
		struct side *other = &pair->sides[!side_index];

		side = &pair->sides[side_index];
		if (side->bound && (side->address != address || side->port != port))
		{
			/* (another address for a side heard lately is no NAT's new
			mapping: ignored) */
			if (!relay_elapsed(now, side->heard_time, REBIND_TIME))
				return;
			side->bound = 0;
			pair->unpaired_time = now;
		}
		if (!side->bound)
		{
			/* (never a pair of one address with itself, nor one with an
			address past its pairs) */
			if ((other->bound && other->address == address && other->port == port) ||
				pairs_of_address(address) >= relay.limits.pairs_per_address)
			{
				relay.counts.refused++;
				send_bound(address, port, _bound_refused, nonce, NULL, NULL);
				return;
			}
			/* (a dedicated server's game's: one side of the pair must be a
			machine its tunnel reaches directly) */
			if (relay.limits.mode == _p2p_relay_game && !p2p_relay_peer_address(address, port) &&
				!(other->bound && p2p_relay_peer_address(other->address, other->port)))
			{
				relay.counts.refused++;
				send_bound(address, port, _bound_refused, nonce, NULL, NULL);
				return;
			}
			side->bound = 1;
			side->address = address;
			side->port = port;
			memcpy(side->nonce, nonce, RELAY_NONCE_SIZE);
			side->heard_time = now;
			if (other->bound)
			{
				char text[2 * RELAY_CHANNEL_SIZE + 1];

				relay.counts.formed++;
				p2p_hex(pair->channel, 4, text);
				p2p_log("Internet play: relay: paired two machines (%s...)", text);
				send_pair_bound(pair, !side_index);
			}
		}
		memcpy(side->nonce, nonce, RELAY_NONCE_SIZE);
		side->heard_time = now;
		pair->active_time = now;
		send_pair_bound(pair, side_index);
		return;
	}
	if (relay.limits.mode == _p2p_relay_game && !p2p_relay_peer_address(address, port))
	{
		relay.counts.refused++;
		send_bound(address, port, _bound_refused, nonce, NULL, NULL);
		return;
	}
	if (pairs_used() >= relay.limits.maximum_pairs || pairs_of_address(address) >= relay.limits.pairs_per_address)
	{
		relay.counts.refused++;
		send_bound(address, port, _bound_refused, nonce, NULL, NULL);
		return;
	}
	for (pair = relay.pairs; pair < relay.pairs + MAXIMUM_PAIRS && pair->used; pair++)
		;
	if (pair == relay.pairs + MAXIMUM_PAIRS)
		return;
	memset(pair, 0, sizeof(*pair));
	pair->used = 1;
	memcpy(pair->token, token, P2P_RELAY_TOKEN_SIZE);
	/* (a channel no other pair has) */
	do
		posix_random_bytes(pair->channel, RELAY_CHANNEL_SIZE);
	while (pair_of_channel(pair->channel) != pair);
	side = &pair->sides[side_index];
	side->bound = 1;
	side->address = address;
	side->port = port;
	side->heard_time = now;
	memcpy(side->nonce, nonce, RELAY_NONCE_SIZE);
	pair->unpaired_time = now;
	pair->active_time = now;
	send_pair_bound(pair, side_index);
}

static void data_received(unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	struct heard_address *heard;
	struct pair *pair;
	struct side *from;
	struct side const *to;

	if (size < MINIMUM_DATA_SIZE || size > MAXIMUM_DATA_SIZE || packet[DATA_HEADER_SIZE] != P2P_TUNNEL_MAGIC)
		return;
	pair = pair_of_channel(packet + 2);
	if (!pair || !pair->sides[0].bound || !pair->sides[1].bound)
		return;
	if (pair->sides[0].address == address && pair->sides[0].port == port)
		from = &pair->sides[0];
	else if (pair->sides[1].address == address && pair->sides[1].port == port)
		from = &pair->sides[1];
	else
		return;
	to = &pair->sides[from == &pair->sides[0]];
	heard = heard_address(address, now);
	heard->heard_time = now;
	/* (each pair's, each address's, and the relay's bytes a second) */
	if (!bucket_spend(&pair->bytes, relay.limits.pair_upload, size, now) ||
		!bucket_spend(&heard->bytes, relay.limits.address_upload, size, now) ||
		!bucket_spend(&relay.bytes, relay.limits.upload, size, now))
	{
		relay.counts.dropped++;
		return;
	}
	from->heard_time = now;
	pair->active_time = now;
	pair->forwarded += (unsigned long)size;
	relay.counts.forwarded_bytes += (unsigned long)size;
	relay.counts.forwarded_packets++;
	packet[1] = _type_delivered;
	p2p_relay_sendto(to->address, to->port, packet, size);
}

static void release_received(const unsigned char *packet, int size, unsigned long address, unsigned short port)
{
	struct pair *pair;
	struct side const *side;

	if (size != RELEASE_SIZE || packet[2] != RELAY_VERSION || packet[3] > 1)
		return;
	pair = pair_of_token(packet + 4);
	if (!pair)
		return;
	side = &pair->sides[packet[3]];
	if (side->bound && side->address == address && side->port == port)
		pair_free(pair, "released");
}

/* ---------- the machines' attempts */

static struct attempt *attempt_of_answer(unsigned long address, unsigned short port, const unsigned char *nonce)
{
	int index;

	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		struct attempt *attempt = &relay.attempts[index];

		if (attempt->used && !attempt->front && attempt->relay.address == address && attempt->relay.port == port &&
			!memcmp(attempt->nonce, nonce, RELAY_NONCE_SIZE))
		{
			return attempt;
		}
	}
	return NULL;
}

/* an attempt that carried nothing more: p2p.c told, if it was paired */
static void attempt_unpaired(struct attempt *attempt, int state, unsigned long now)
{
	int was_paired = attempt->state == _attempt_paired;

	attempt->state = state;
	attempt->state_time = now;
	if (was_paired)
		p2p_relay_lost((int)(attempt - relay.attempts));
}

static void attempt_free(struct attempt *attempt)
{
	int was_paired = attempt->state == _attempt_paired;
	int index = (int)(attempt - relay.attempts);

	memset(attempt, 0, sizeof(*attempt));
	if (was_paired)
		p2p_relay_lost(index);
}

static void send_bind(struct attempt *attempt, unsigned long now)
{
	unsigned char packet[BIND_SIZE];

	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = _type_bind;
	packet[2] = RELAY_VERSION;
	packet[3] = (unsigned char)attempt->side;
	memcpy(packet + 4, attempt->token, P2P_RELAY_TOKEN_SIZE);
	memcpy(packet + 4 + P2P_RELAY_TOKEN_SIZE, attempt->nonce, RELAY_NONCE_SIZE);
	memcpy(packet + 4 + P2P_RELAY_TOKEN_SIZE + RELAY_NONCE_SIZE, attempt->cookie, RELAY_COOKIE_SIZE);
	p2p_relay_sendto(attempt->relay.address, attempt->relay.port, packet, BIND_SIZE);
	attempt->sent_time = now;
}

static void send_release(struct attempt const *attempt)
{
	unsigned char packet[RELEASE_SIZE];

	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = _type_release;
	packet[2] = RELAY_VERSION;
	packet[3] = (unsigned char)attempt->side;
	memcpy(packet + 4, attempt->token, P2P_RELAY_TOKEN_SIZE);
	p2p_relay_sendto(attempt->relay.address, attempt->relay.port, packet, RELEASE_SIZE);
}

static void challenge_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	struct attempt *attempt;

	if (size != CHALLENGE_SIZE || packet[2] != RELAY_VERSION)
		return;
	attempt = attempt_of_answer(address, port, packet + 4);
	if (!attempt)
		return;
	/* (the bind again at once, with the cookie; a challenge to a paired
	attempt is the period's turn, its keep-alive's) */
	if (!memcmp(attempt->cookie, packet + 4 + RELAY_NONCE_SIZE, RELAY_COOKIE_SIZE))
		return;
	memcpy(attempt->cookie, packet + 4 + RELAY_NONCE_SIZE, RELAY_COOKIE_SIZE);
	send_bind(attempt, now);
}

static void bound_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	struct attempt *attempt;
	int state;

	if (size != BOUND_SIZE || packet[2] != RELAY_VERSION)
		return;
	attempt = attempt_of_answer(address, port, packet + 4);
	if (!attempt)
		return;
	state = packet[3];
	if (state != _bound_waiting && state != _bound_paired && state != _bound_refused)
		return;
	attempt->heard_time = now;
	if (attempt->bind_round_trip < 0)
		attempt->bind_round_trip = (long)(now - attempt->first_sent_time);
	if (state == _bound_refused)
	{
		if (attempt->state != _attempt_refused)
			attempt_unpaired(attempt, _attempt_refused, now);
		return;
	}
	/* (the channel is the pair's, which a bind of the same token keeps) */
	memcpy(attempt->channel, packet + 4 + RELAY_NONCE_SIZE, RELAY_CHANNEL_SIZE);
	if (state == _bound_paired)
	{
		/* (the joiner's side is told nothing of its host: a relay that told
		it is not believed) */
		if (attempt->side == 0)
		{
			memcpy(&attempt->peer_address.address, packet + 4 + RELAY_NONCE_SIZE + RELAY_CHANNEL_SIZE, 4);
			memcpy(&attempt->peer_address.port, packet + 4 + RELAY_NONCE_SIZE + RELAY_CHANNEL_SIZE + 4, 2);
		}
		if (attempt->state != _attempt_paired)
		{
			attempt->state = _attempt_paired;
			attempt->state_time = now;
			p2p_relay_paired((int)(attempt - relay.attempts));
		}
	}
	else if (attempt->state == _attempt_paired)
	{
		/* (the relay lost the other side: waiting again, timed from now) */
		memset(&attempt->peer_address, 0, sizeof(attempt->peer_address));
		attempt_unpaired(attempt, _attempt_waiting, now);
	}
	else
	{
		attempt->state = _attempt_waiting;
	}
}

static void delivered_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	int index;

	if (size < MINIMUM_DATA_SIZE || size > MAXIMUM_DATA_SIZE || packet[DATA_HEADER_SIZE] != P2P_TUNNEL_MAGIC)
		return;
	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		struct attempt *attempt = &relay.attempts[index];

		if (attempt->used && !attempt->front && attempt->state == _attempt_paired && attempt->relay.address == address &&
			attempt->relay.port == port && !memcmp(attempt->channel, packet + 2, RELAY_CHANNEL_SIZE))
		{
			attempt->heard_time = now;
			p2p_relay_delivered(index, packet + DATA_HEADER_SIZE, size - DATA_HEADER_SIZE);
			return;
		}
	}
}

int p2p_relay_attempt_start(const unsigned char *peer, const unsigned char *secret, int side,
	const struct p2p_candidate *relay_address, unsigned long now)
{
	struct attempt *free_attempt = NULL;
	int peer_attempts = 0;
	int index;

	if ((side != 0 && side != 1) || !relay_address->address || !relay_address->port)
		return -1;
	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		struct attempt *attempt = &relay.attempts[index];

		if (attempt->used && !memcmp(attempt->peer, peer, P2P_IDENTIFIER_SIZE))
		{
			if (!attempt->front && attempt->relay.address == relay_address->address &&
				attempt->relay.port == relay_address->port)
				return index;
			peer_attempts++;
		}
		if (!attempt->used && !free_attempt)
			free_attempt = attempt;
	}
	/* (a few relays for one peer: whatever its offers named) */
	if (!free_attempt || peer_attempts >= P2P_RELAY_PEER_RELAYS)
		return -1;
	memset(free_attempt, 0, sizeof(*free_attempt));
	free_attempt->used = 1;
	memcpy(free_attempt->peer, peer, P2P_IDENTIFIER_SIZE);
	free_attempt->side = side;
	free_attempt->relay = *relay_address;
	p2p_relay_token(secret, relay_address, free_attempt->token);
	posix_random_bytes(free_attempt->nonce, RELAY_NONCE_SIZE);
	free_attempt->state = _attempt_binding;
	free_attempt->state_time = now;
	free_attempt->heard_time = now;
	free_attempt->bind_round_trip = -1;
	free_attempt->first_sent_time = now;
	send_bind(free_attempt, now);
	return (int)(free_attempt - relay.attempts);
}

void p2p_relay_forget_peer(const unsigned char *peer, int unpaired_only)
{
	int index;

	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		struct attempt *attempt = &relay.attempts[index];

		if (!attempt->used || memcmp(attempt->peer, peer, P2P_IDENTIFIER_SIZE) ||
			(unpaired_only && attempt->state == _attempt_paired))
		{
			continue;
		}
		if (!attempt->front && (attempt->state == _attempt_waiting || attempt->state == _attempt_paired))
			send_release(attempt);
		attempt_free(attempt);
	}
}

int p2p_relay_front_attempt(const unsigned char *peer, const struct p2p_candidate *front, unsigned long client,
	const struct p2p_candidate *client_address, unsigned long now, int *created)
{
	struct attempt *free_attempt = NULL;
	int peer_attempts = 0;
	int index;

	*created = 0;
	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		struct attempt *attempt = &relay.attempts[index];

		if (attempt->used && attempt->front && attempt->client == client)
		{
			/* (the same client at the front is the same machine: another
			peer's packet through it is not taken) */
			if (memcmp(attempt->peer, peer, P2P_IDENTIFIER_SIZE) || attempt->relay.address != front->address ||
				attempt->relay.port != front->port)
			{
				return -1;
			}
			return index;
		}
		if (attempt->used && !memcmp(attempt->peer, peer, P2P_IDENTIFIER_SIZE))
			peer_attempts++;
		if (!attempt->used && !free_attempt)
			free_attempt = attempt;
	}
	if (!free_attempt || peer_attempts >= P2P_RELAY_PEER_RELAYS)
		return -1;
	memset(free_attempt, 0, sizeof(*free_attempt));
	free_attempt->used = 1;
	free_attempt->front = 1;
	free_attempt->client = client;
	memcpy(free_attempt->peer, peer, P2P_IDENTIFIER_SIZE);
	free_attempt->relay = *front;
	free_attempt->peer_address = *client_address;
	free_attempt->state = _attempt_paired;
	free_attempt->state_time = now;
	free_attempt->heard_time = now;
	free_attempt->sent_time = now;
	free_attempt->bind_round_trip = -1;
	*created = 1;
	return (int)(free_attempt - relay.attempts);
}

void p2p_relay_front_heard(int index, unsigned long now)
{
	if (index >= 0 && index < MAXIMUM_ATTEMPTS && relay.attempts[index].used && relay.attempts[index].front)
		relay.attempts[index].heard_time = now;
}

void p2p_relay_front_drop(int index)
{
	if (index >= 0 && index < MAXIMUM_ATTEMPTS && relay.attempts[index].used && relay.attempts[index].front)
		attempt_free(&relay.attempts[index]);
}

void p2p_relay_front_forget(void)
{
	int index;

	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		if (relay.attempts[index].used && relay.attempts[index].front)
			attempt_free(&relay.attempts[index]);
	}
}

int p2p_relay_send(int index, const unsigned char *packet, int size)
{
	unsigned char data[MAXIMUM_DATA_SIZE];
	struct attempt const *attempt;

	if (index < 0 || index >= MAXIMUM_ATTEMPTS || size < P2P_TUNNEL_MINIMUM_PACKET ||
		size > P2P_TUNNEL_MAXIMUM_PACKET)
	{
		return 0;
	}
	attempt = &relay.attempts[index];
	if (!attempt->used || attempt->state != _attempt_paired)
		return 0;
	if (attempt->front)
		return p2p_front_send(attempt->client, packet, size);
	data[0] = P2P_RELAY_MAGIC;
	data[1] = _type_data;
	memcpy(data + 2, attempt->channel, RELAY_CHANNEL_SIZE);
	memcpy(data + DATA_HEADER_SIZE, packet, (size_t)size);
	p2p_relay_sendto(attempt->relay.address, attempt->relay.port, data, DATA_HEADER_SIZE + size);
	return 1;
}

int p2p_relay_attempt(int index, struct p2p_relay_attempt_state *state)
{
	struct attempt const *attempt;

	if (index < 0 || index >= MAXIMUM_ATTEMPTS)
		return 0;
	attempt = &relay.attempts[index];
	if (!attempt->used)
		return 0;
	if (state)
	{
		memcpy(state->peer, attempt->peer, P2P_IDENTIFIER_SIZE);
		state->relay = attempt->relay;
		state->side = attempt->side;
		state->paired = attempt->state == _attempt_paired;
		state->bind_round_trip = attempt->bind_round_trip;
		state->peer_address = attempt->peer_address;
	}
	return 1;
}

int p2p_relay_peer_attempts(const unsigned char *peer, int *indices, int maximum)
{
	int count = 0;
	int index;

	for (index = 0; index < MAXIMUM_ATTEMPTS && count < maximum; index++)
	{
		if (relay.attempts[index].used && !memcmp(relay.attempts[index].peer, peer, P2P_IDENTIFIER_SIZE))
			indices[count++] = index;
	}
	return count;
}

/* ---------- offers */

static void offer_tag(const unsigned char *secret, const unsigned char *body, int size, unsigned char *tag)
{
	static const char label[] = "openh1 relay offer 2";
	unsigned char data[sizeof(label) - 1 + P2P_RELAY_OFFER_MAXIMUM_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];

	memcpy(data, label, sizeof(label) - 1);
	memcpy(data + sizeof(label) - 1, body, (size_t)size);
	p2p_hmac_sha256(secret, P2P_SHA256_SIZE, data, (int)(sizeof(label) - 1) + size, digest);
	memcpy(tag, digest, P2P_RELAY_OFFER_TAG_SIZE);
}

int p2p_relay_offer_write(const unsigned char *identifier, int side, const struct p2p_candidate *relays, int count,
	const unsigned char *secret, unsigned char *body)
{
	int size = 0;
	int index;

	if (count < 0)
		count = 0;
	if (count > P2P_RELAY_OFFERED)
		count = P2P_RELAY_OFFERED;
	memcpy(body, identifier, P2P_IDENTIFIER_SIZE);
	size += P2P_IDENTIFIER_SIZE;
	body[size++] = (unsigned char)side;
	body[size++] = (unsigned char)count;
	for (index = 0; index < count; index++)
	{
		memcpy(body + size, &relays[index].address, 4);
		memcpy(body + size + 4, &relays[index].port, 2);
		size += P2P_RELAY_OFFER_ENTRY_SIZE;
	}
	offer_tag(secret, body, size, body + size);
	return size + P2P_RELAY_OFFER_TAG_SIZE;
}

int p2p_relay_offer_read(const unsigned char *body, int size, int side, const unsigned char *secret,
	struct p2p_candidate *relays)
{
	unsigned char tag[P2P_RELAY_OFFER_TAG_SIZE];
	int count, index;

	if (size < P2P_RELAY_OFFER_HEADER_SIZE + P2P_RELAY_OFFER_TAG_SIZE || size > P2P_RELAY_OFFER_MAXIMUM_SIZE)
		return -1;
	count = body[P2P_IDENTIFIER_SIZE + 1];
	if (body[P2P_IDENTIFIER_SIZE] != side || count > P2P_RELAY_OFFERED ||
		size != P2P_RELAY_OFFER_HEADER_SIZE + count * P2P_RELAY_OFFER_ENTRY_SIZE + P2P_RELAY_OFFER_TAG_SIZE)
	{
		return -1;
	}
	/* (the peer's own, tagged with the session's secret: the signalling's
	seal is the invite's, which anyone who has the invite holds) */
	offer_tag(secret, body, size - P2P_RELAY_OFFER_TAG_SIZE, tag);
	if (!p2p_equal(tag, body + size - P2P_RELAY_OFFER_TAG_SIZE, P2P_RELAY_OFFER_TAG_SIZE))
		return -1;
	for (index = 0; index < count; index++)
	{
		const unsigned char *entry = body + P2P_RELAY_OFFER_HEADER_SIZE + index * P2P_RELAY_OFFER_ENTRY_SIZE;

		memcpy(&relays[index].address, entry, 4);
		memcpy(&relays[index].port, entry + 4, 2);
	}
	return count;
}

int p2p_relay_offer_take(struct p2p_relay_offers_heard *heard, unsigned long now)
{
	if (heard->count >= OFFERS_PER_SESSION || (heard->count && !relay_elapsed(now, heard->time, OFFER_INTERVAL)))
		return 0;
	heard->count++;
	heard->time = now;
	return 1;
}

int p2p_relay_merge(struct p2p_candidate *relays, int *count, const struct p2p_candidate *added, int added_count)
{
	int added_index, known, merged = 0;

	for (added_index = 0; added_index < added_count && *count < P2P_RELAY_PEER_RELAYS; added_index++)
	{
		for (known = 0; known < *count; known++)
		{
			if (relays[known].address == added[added_index].address && relays[known].port == added[added_index].port)
				break;
		}
		if (known < *count)
			continue;
		relays[(*count)++] = added[added_index];
		merged++;
	}
	return merged;
}

/* ---------- the path */

int p2p_relay_path_heard(struct p2p_relay_path *path, int via, unsigned long now)
{
	int silent = path->carrier < 0 || relay_elapsed(now, path->carrier_heard, CARRIER_SILENCE);

	if (via == path->carrier)
	{
		path->carrier_heard = now;
		return 0;
	}
	if (!via && path->carrier)
	{
		/* (direct while relayed: taken once it lasted, or at once if the
		relay is silent) */
		if (!path->direct_last || relay_elapsed(now, path->direct_last, DIRECT_GAP))
			path->direct_first = now;
		path->direct_last = now;
		if (!silent && !relay_elapsed(now, path->direct_first, DIRECT_SWITCH_TIME))
			return 0;
		path->carrier = 0;
		path->carrier_heard = now;
		path->direct_time = now;
		path->direct_first = path->direct_last = 0;
		return 1;
	}
	/* (another relay, or a relay while direct: only in place of a silent
	path) */
	if (!silent)
		return 0;
	path->carrier = via;
	path->carrier_heard = now;
	path->direct_first = path->direct_last = 0;
	path->direct_time = 0;
	return 1;
}

int p2p_relay_path_silent(const struct p2p_relay_path *path, unsigned long now)
{
	return path->carrier < 0 || relay_elapsed(now, path->carrier_heard, CARRIER_SILENCE);
}

int p2p_relay_path_relays_done(const struct p2p_relay_path *path, unsigned long now)
{
	return path->carrier == 0 && path->direct_time && relay_elapsed(now, path->direct_time, RELAYS_KEPT_TIME);
}

/* ---------- both */

int p2p_relay_received(unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	int relaying = relay.limits.mode != _p2p_relay_off;

	if (size < 2 || packet[0] != P2P_RELAY_MAGIC)
		return 0;
	switch (packet[1])
	{
	case _type_bind:
		if (relaying)
			bind_received(packet, size, address, port, now);
		break;
	case _type_data:
		if (relaying)
			data_received(packet, size, address, port, now);
		break;
	case _type_release:
		if (relaying)
			release_received(packet, size, address, port);
		break;
	case _type_challenge:
		challenge_received(packet, size, address, port, now);
		break;
	case _type_bound:
		bound_received(packet, size, address, port, now);
		break;
	case _type_delivered:
		delivered_received(packet, size, address, port, now);
		break;
	}
	return 1;
}

void p2p_relay_update(unsigned long now)
{
	int index;

	for (index = 0; index < MAXIMUM_PAIRS; index++)
	{
		struct pair *pair = &relay.pairs[index];
		int side_index;

		if (!pair->used)
			continue;
		/* (a side gone quiet unbound: its NAT's mapping may be gone, and
		the other side is told it waits) */
		for (side_index = 0; side_index < 2; side_index++)
		{
			struct side *side = &pair->sides[side_index];

			if (side->bound && relay_elapsed(now, side->heard_time, SIDE_SILENCE))
			{
				int was_paired = pair->sides[!side_index].bound;

				side->bound = 0;
				pair->unpaired_time = now;
				if (was_paired)
				{
					relay.counts.ended++;
					send_pair_bound(pair, !side_index);
				}
			}
		}
		if (!pair->sides[0].bound && !pair->sides[1].bound)
			pair_free(pair, "both sides gone");
		else if (!(pair->sides[0].bound && pair->sides[1].bound) &&
			relay_elapsed(now, pair->unpaired_time, UNPAIRED_TIME))
		{
			pair_free(pair, "never paired");
		}
		else if (relay_elapsed(now, pair->active_time, IDLE_TIME))
			pair_free(pair, "idle");
	}
	for (index = 0; index < MAXIMUM_ADDRESSES; index++)
	{
		struct heard_address *entry = &relay.addresses[index];

		if (entry->used && relay_elapsed(now, entry->heard_time, ADDRESS_FORGET_TIME) &&
			!pairs_of_address(entry->address))
		{
			entry->used = 0;
		}
	}
	for (index = 0; index < MAXIMUM_ATTEMPTS; index++)
	{
		struct attempt *attempt = &relay.attempts[index];

		if (!attempt->used)
			continue;
		/* (a front's path: kept while its client is heard; the server's
		registration keeps the way open) */
		if (attempt->front)
		{
			if (relay_elapsed(now, attempt->heard_time, ATTEMPT_SILENCE))
				attempt_free(attempt);
			continue;
		}
		if (attempt->state == _attempt_paired)
		{
			if (relay_elapsed(now, attempt->heard_time, ATTEMPT_SILENCE))
				attempt_free(attempt);
			else if (relay_elapsed(now, attempt->sent_time, KEEP_ALIVE_INTERVAL))
				send_bind(attempt, now);
		}
		/* (timed from its start, or from when its pair last lost the other
		side: not from its creation) */
		else if (relay_elapsed(now, attempt->state_time, ATTEMPT_TIMEOUT))
		{
			if (attempt->state == _attempt_waiting)
				send_release(attempt);
			attempt_free(attempt);
		}
		else if (attempt->state == _attempt_refused)
		{
			/* (asked again once, after a while: a pair may have ended) */
			if (!attempt->retried && relay_elapsed(now, attempt->sent_time, REFUSED_TIME))
			{
				attempt->retried = 1;
				attempt->state = _attempt_binding;
				send_bind(attempt, now);
			}
		}
		else if (relay_elapsed(now, attempt->sent_time, BIND_INTERVAL))
		{
			send_bind(attempt, now);
		}
	}
}
