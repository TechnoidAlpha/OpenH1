/*
P2P_RELAY.C (test)

Internet play's relays (port/linux/src/p2p_relay.c): the relay's pairing of
two machines by their session's token after a check that each bind's
address is its sender's (its cookie), a token for each relay, what the
relay tells each side of the other (the host's side alone learns the
joiner's address), its limits, buckets and expiry, the machines' attempts
(a demoted attempt timed from its demotion), the offers (tagged, a few a
second apart), the path a peer is reached by (failing over to another
relay, a direct path taking over only once it lasted), and malformed
packets. test_p2p_relay.py takes the whole file, with p2p_crypto.c's
SHA-256, HMAC and comparison; the clock, the addresses and what is sent are
the test's.
*/

#include "harness.h"

#include "p2p_internal.h"

/* (a check needs no words of its own: the line says which) */
#define REQUIRE(condition) CHECK(condition, "%s", "")

/* ---------- the world */

enum
{
	MAXIMUM_SENT = 4096,
	/* two relays, and machines A, B, C (addresses in network order, as the
	tunnel's; their values matter only as distinct numbers) */
	RELAY = 0x0A000001,
	RELAY2 = 0x0A000002,
	A = 0x0B000002,
	B = 0x0C000003,
	C = 0x0D000004,
	PORT = 0x1234,
	/* the packets' sizes (p2p_relay.c's) */
	BIND = 4 + 32 + 8 + 16,
	CHALLENGE = 4 + 8 + 16,
	BOUND = 4 + 8 + 8 + 8,
	RELEASE = 4 + 32,
};

struct sent
{
	unsigned long address;
	unsigned short port;
	int size;
	unsigned char data[2048];
};

static struct sent sent[MAXIMUM_SENT];
static int sent_count;
static unsigned int random_state = 12345;
static int paired_calls, delivered_calls, lost_calls;
static int delivered_index, lost_index;
static unsigned char delivered[2048];
static int delivered_size;
/* the machines the tunnel reaches directly (a dedicated server's game) */
static unsigned long peer_addresses[4];
static int peer_count;

void posix_random_bytes(void *buffer, unsigned long size)
{
	unsigned char *bytes = buffer;
	unsigned long index;

	for (index = 0; index < size; index++)
	{
		random_state = random_state * 1103515245u + 12345u;
		bytes[index] = (unsigned char)(random_state >> 16);
	}
}

void p2p_log(const char *format, ...)
{
	(void)format;
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	int index;

	for (index = 0; index < size; index++)
		sprintf(text + 2 * index, "%02x", bytes[index]);
}

void p2p_relay_sendto(unsigned long address, unsigned short port, const unsigned char *data, int size)
{
	if (sent_count == MAXIMUM_SENT || size > (int)sizeof(sent[0].data))
		return;
	sent[sent_count].address = address;
	sent[sent_count].port = port;
	sent[sent_count].size = size;
	memcpy(sent[sent_count].data, data, (size_t)size);
	sent_count++;
}

void p2p_relay_paired(int index)
{
	(void)index;
	paired_calls++;
}

void p2p_relay_delivered(int index, const unsigned char *packet, int size)
{
	delivered_calls++;
	delivered_index = index;
	delivered_size = size;
	memcpy(delivered, packet, (size_t)size);
}

void p2p_relay_lost(int index)
{
	lost_calls++;
	lost_index = index;
}

/* a front's path (p2p_front.c) sends through its front */
static int front_sent_calls;
static unsigned long front_sent_client;

int p2p_front_send(unsigned long client, const unsigned char *packet, int size)
{
	(void)packet;
	(void)size;
	front_sent_calls++;
	front_sent_client = client;
	return 1;
}

int p2p_relay_peer_address(unsigned long address, unsigned short port)
{
	int index;

	(void)port;
	for (index = 0; index < peer_count; index++)
	{
		if (peer_addresses[index] == address)
			return 1;
	}
	return 0;
}

#include "under_test.inc"

/* ---------- helpers */

static unsigned long now = 1000000;

static void limits_default(struct p2p_relay_limits *limits, int mode)
{
	memset(limits, 0, sizeof(*limits));
	limits->mode = mode;
	limits->maximum_pairs = 64;
	limits->pairs_per_address = 8;
	limits->binds_per_second = 20;
	limits->binds_in_all = 2000;
	limits->upload = 8192 * 1024;
	limits->pair_upload = 256 * 1024;
	limits->address_upload = 1024 * 1024;
}

static void relay_on(void)
{
	struct p2p_relay_limits limits;

	limits_default(&limits, _p2p_relay_any);
	p2p_relay_configure(&limits);
}

static void secret_of(int seed, unsigned char *secret)
{
	memset(secret, seed, P2P_SHA256_SIZE);
}

/* the token of a session (its secret's seed) at a relay */
static void token_at(int seed, unsigned long relay_address, unsigned char *token)
{
	unsigned char secret[P2P_SHA256_SIZE];
	struct p2p_candidate relay_candidate;

	secret_of(seed, secret);
	relay_candidate.address = relay_address;
	relay_candidate.port = PORT;
	p2p_relay_token(secret, &relay_candidate, token);
}

static void token_of(int seed, unsigned char *token)
{
	token_at(seed, RELAY, token);
}

/* a bind as a machine sends it, with a cookie (none: zeros) */
static int bind_packet(unsigned char *packet, int side, const unsigned char *token, int nonce,
	const unsigned char *cookie)
{
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = 'B';
	packet[2] = 2;
	packet[3] = (unsigned char)side;
	memcpy(packet + 4, token, P2P_RELAY_TOKEN_SIZE);
	memset(packet + 4 + P2P_RELAY_TOKEN_SIZE, nonce, 8);
	if (cookie)
		memcpy(packet + 4 + P2P_RELAY_TOKEN_SIZE + 8, cookie, 16);
	else
		memset(packet + 4 + P2P_RELAY_TOKEN_SIZE + 8, 0, 16);
	return BIND;
}

static void receive(const unsigned char *packet, int size, unsigned long address)
{
	unsigned char copy[2048];

	memcpy(copy, packet, (size_t)size);
	p2p_relay_received(copy, size, address, PORT, now);
}

/* the latest packet of a type sent to an address since index from: its
index, -1 if none */
static int sent_of(unsigned long address, char type, int from)
{
	int index;

	for (index = sent_count - 1; index >= from; index--)
	{
		if (sent[index].address == address && sent[index].data[0] == P2P_RELAY_MAGIC && sent[index].data[1] == type)
			return index;
	}
	return -1;
}

/* a bind from an address, as a machine makes it: answered with a challenge
(its cookie), then again with the cookie */
static void bind_from(unsigned long address, int side, const unsigned char *token, int nonce)
{
	unsigned char packet[BIND];
	int before = sent_count;
	int challenge;

	bind_packet(packet, side, token, nonce, NULL);
	receive(packet, BIND, address);
	challenge = sent_of(address, 'C', before);
	if (challenge < 0)
		return;
	bind_packet(packet, side, token, nonce, sent[challenge].data + 4 + 8);
	receive(packet, BIND, address);
}

/* the latest BOUND sent to an address: its state (-1: none), channel, and
the address in it */
static int bound_to(unsigned long address, unsigned char *channel, unsigned long *told)
{
	int index = sent_of(address, 'K', 0);

	if (index < 0 || sent[index].size != BOUND)
		return -1;
	if (channel)
		memcpy(channel, sent[index].data + 12, 8);
	if (told)
		memcpy(told, sent[index].data + 20, 4);
	return sent[index].data[3];
}

/* a data packet (a tunnel packet of size bytes behind its header) */
static int data_packet(unsigned char *packet, const unsigned char *channel, int size)
{
	int index;

	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = 'D';
	memcpy(packet + 2, channel, 8);
	packet[10] = P2P_TUNNEL_MAGIC;
	for (index = 1; index < size; index++)
		packet[10 + index] = (unsigned char)index;
	return 10 + size;
}

/* the data sent to an address (0: any) since sent index from */
static int data_sent_to(unsigned long address, int from)
{
	int count = 0;
	int index;

	for (index = from; index < sent_count; index++)
	{
		count += (!address || sent[index].address == address) && sent[index].data[0] == P2P_RELAY_MAGIC &&
			sent[index].data[1] == 'd';
	}
	return count;
}

/* A and B paired by one token; the channel */
static void pair_a_b(const unsigned char *token, unsigned char *channel)
{
	bind_from(A, 0, token, 1);
	bind_from(B, 1, token, 2);
	bound_to(B, channel, NULL);
}

static void relay_at(unsigned long address, struct p2p_candidate *relay_candidate)
{
	relay_candidate->address = address;
	relay_candidate->port = PORT;
}

/* the relay's answer to an attempt's latest bind: a BOUND of a state, with
the channel (and an address in it) */
static void answer_attempt(unsigned long relay_address, int state, const unsigned char *channel, unsigned long told)
{
	unsigned char packet[BOUND];
	int bind = sent_of(relay_address, 'B', 0);

	memset(packet, 0, sizeof(packet));
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = 'K';
	packet[2] = 2;
	packet[3] = (unsigned char)state;
	memcpy(packet + 4, sent[bind].data + 4 + 32, 8);
	memcpy(packet + 12, channel, 8);
	memcpy(packet + 20, &told, 4);
	p2p_relay_received(packet, BOUND, relay_address, PORT, now);
}

/* ---------- cases */

static int case_pairing(void)
{
	unsigned char token[P2P_RELAY_TOKEN_SIZE], channel[8], channel_a[8], packet[2048];
	struct p2p_relay_counts counts;
	int size, before;

	relay_on();
	token_of(1, token);
	bind_from(A, 0, token, 1);
	REQUIRE(bound_to(A, NULL, NULL) == 0);
	bind_from(B, 1, token, 2);
	/* (both told: B as it binds, A at once) */
	REQUIRE(bound_to(B, channel, NULL) == 1);
	REQUIRE(bound_to(A, channel_a, NULL) == 1);
	REQUIRE(!memcmp(channel, channel_a, 8));
	p2p_relay_counts(&counts);
	REQUIRE(counts.paired == 1);
	/* forwarded each way, as 'd', the packet whole */
	size = data_packet(packet, channel, 100);
	before = sent_count;
	receive(packet, size, A);
	REQUIRE(data_sent_to(B, before) == 1 && data_sent_to(A, before) == 0);
	REQUIRE(sent[sent_count - 1].size == size && !memcmp(sent[sent_count - 1].data + 2, packet + 2, (size_t)(size - 2)));
	before = sent_count;
	receive(packet, size, B);
	REQUIRE(data_sent_to(A, before) == 1);
	/* a 'd' is never forwarded again */
	before = sent_count;
	packet[1] = 'd';
	receive(packet, size, A);
	REQUIRE(data_sent_to(0, before) == 0);
	/* not a tunnel packet: not forwarded */
	size = data_packet(packet, channel, 64);
	packet[10] = 0;
	receive(packet, size, A);
	REQUIRE(data_sent_to(0, before) == 0);
	return 0;
}

/* a bind is taken only with the cookie its address was given: one sent in
another's name binds nothing, and is answered with a challenge alone, no
larger than the bind */
static int case_return_routability(void)
{
	unsigned char token[P2P_RELAY_TOKEN_SIZE], packet[BIND], cookie[16];
	struct p2p_relay_counts counts;
	int before, challenge;

	relay_on();
	token_of(1, token);
	/* no cookie: a challenge, smaller than the bind, and no pair */
	before = sent_count;
	bind_packet(packet, 0, token, 1, NULL);
	receive(packet, BIND, A);
	challenge = sent_of(A, 'C', before);
	REQUIRE(challenge >= 0 && sent_count == before + 1 && sent[challenge].size == CHALLENGE && CHALLENGE <= BIND);
	p2p_relay_counts(&counts);
	REQUIRE(counts.pairs == 0);
	memcpy(cookie, sent[challenge].data + 12, 16);
	/* A's cookie sent from C (C forging A's binds learns nothing it can
	use: a cookie is its address's) */
	before = sent_count;
	bind_packet(packet, 0, token, 1, cookie);
	receive(packet, BIND, C);
	REQUIRE(sent_of(C, 'K', before) < 0);
	p2p_relay_counts(&counts);
	REQUIRE(counts.pairs == 0);
	/* a forged cookie */
	cookie[0] ^= 1;
	bind_packet(packet, 0, token, 1, cookie);
	receive(packet, BIND, A);
	REQUIRE(sent_of(A, 'K', before) < 0);
	cookie[0] ^= 1;
	/* A's own: bound */
	bind_packet(packet, 0, token, 1, cookie);
	receive(packet, BIND, A);
	REQUIRE(bound_to(A, NULL, NULL) == 0);
	/* good in its period and the next, not after */
	now += 35000;
	before = sent_count;
	receive(packet, BIND, A);
	REQUIRE(sent_of(A, 'K', before) >= 0);
	now += 60000;
	before = sent_count;
	receive(packet, BIND, A);
	REQUIRE(sent_of(A, 'K', before) < 0 && sent_of(A, 'C', before) >= 0);
	/* another token's cookie */
	{
		unsigned char other[P2P_RELAY_TOKEN_SIZE];

		bind_from(A, 0, token, 1);
		challenge = sent_of(A, 'C', 0);
		token_of(2, other);
		before = sent_count;
		bind_packet(packet, 0, other, 3, sent[challenge].data + 12);
		receive(packet, BIND, A);
		REQUIRE(sent_of(A, 'K', before) < 0);
	}
	return 0;
}

/* a token for each relay: the same session's at two relays differ, and
none of them is the secret or its tunnel keys; one relay's token used at
another binds a pair of its own there, and releases nothing of the
other's; a release counts from the side's own address alone */
static int case_token_per_relay(void)
{
	unsigned char token[P2P_RELAY_TOKEN_SIZE], token2[P2P_RELAY_TOKEN_SIZE], other[P2P_RELAY_TOKEN_SIZE];
	unsigned char secret[P2P_SHA256_SIZE], joiner_key[P2P_SHA256_SIZE], host_key[P2P_SHA256_SIZE];
	unsigned char channel[8], packet[2048];
	struct p2p_relay_counts counts;
	int size, before;

	relay_on();
	token_at(1, RELAY, token);
	token_at(1, RELAY2, token2);
	token_at(2, RELAY, other);
	REQUIRE(memcmp(token, token2, P2P_RELAY_TOKEN_SIZE) && memcmp(token, other, P2P_RELAY_TOKEN_SIZE));
	secret_of(1, secret);
	p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "joiner", 6, joiner_key);
	p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "host", 4, host_key);
	REQUIRE(memcmp(token, secret, P2P_RELAY_TOKEN_SIZE) && memcmp(token, joiner_key, P2P_RELAY_TOKEN_SIZE) &&
		memcmp(token, host_key, P2P_RELAY_TOKEN_SIZE));
	/* (this relay is RELAY2: A and B paired by its token) */
	pair_a_b(token2, channel);
	REQUIRE(bound_to(A, NULL, NULL) == 1);
	/* RELAY, which saw token, binds it here from C: a pair of its own,
	which A and B's does not take up */
	bind_from(C, 0, token, 5);
	REQUIRE(bound_to(C, NULL, NULL) == 0);
	/* ... and releases with it: A and B's pair lives */
	memset(packet, 0, RELEASE);
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = 'X';
	packet[2] = 2;
	packet[3] = 0;
	memcpy(packet + 4, token, P2P_RELAY_TOKEN_SIZE);
	receive(packet, RELEASE, C);
	/* the right token, from another address than the side's: nothing */
	memcpy(packet + 4, token2, P2P_RELAY_TOKEN_SIZE);
	receive(packet, RELEASE, C);
	p2p_relay_counts(&counts);
	REQUIRE(counts.paired == 1);
	size = data_packet(packet, channel, 64);
	before = sent_count;
	receive(packet, size, A);
	REQUIRE(data_sent_to(B, before) == 1);
	/* from the side itself: released */
	memset(packet, 0, RELEASE);
	packet[0] = P2P_RELAY_MAGIC;
	packet[1] = 'X';
	packet[2] = 2;
	packet[3] = 0;
	memcpy(packet + 4, token2, P2P_RELAY_TOKEN_SIZE);
	receive(packet, RELEASE, A);
	p2p_relay_counts(&counts);
	REQUIRE(counts.paired == 0);
	/* the attempts: each relay bound with its own token */
	{
		struct p2p_candidate first, second;
		unsigned char peer[6] = { 1, 2, 3, 4, 5, 6 };
		int one, two;

		relay_at(RELAY, &first);
		relay_at(RELAY2, &second);
		before = sent_count;
		one = p2p_relay_attempt_start(peer, secret, 1, &first, now);
		two = p2p_relay_attempt_start(peer, secret, 1, &second, now);
		REQUIRE(one >= 0 && two >= 0 && one != two);
		REQUIRE(!memcmp(sent[sent_of(RELAY, 'B', before)].data + 4, token, P2P_RELAY_TOKEN_SIZE));
		REQUIRE(!memcmp(sent[sent_of(RELAY2, 'B', before)].data + 4, token2, P2P_RELAY_TOKEN_SIZE));
	}
	return 0;
}

/* the address a host bans a relayed player by: where the relay saw and
checked its binds come from, told the host's side (0) alone; the joiner's
side is told nothing of its host, and believes nothing it is told */
static int case_verified_address(void)
{
	unsigned char token[P2P_RELAY_TOKEN_SIZE], channel[8], secret[P2P_SHA256_SIZE];
	struct p2p_relay_attempt_state state;
	struct p2p_candidate relay_candidate;
	unsigned char host_peer[6] = { 1, 1, 1, 1, 1, 1 }, joiner_peer[6] = { 2, 2, 2, 2, 2, 2 };
	unsigned long told = 0;
	int host_side, joiner_side;

	relay_on();
	token_of(1, token);
	/* A the host's side (0), B the joiner's */
	pair_a_b(token, channel);
	REQUIRE(bound_to(A, NULL, &told) == 1 && told == B);
	told = 1;
	REQUIRE(bound_to(B, NULL, &told) == 1 && told == 0);
	/* (and again on each keep-alive: the joiner's never told) */
	bind_from(B, 1, token, 2);
	REQUIRE(bound_to(B, NULL, &told) == 1 && told == 0);
	bind_from(A, 0, token, 1);
	REQUIRE(bound_to(A, NULL, &told) == 1 && told == B);
	/* the machines' attempts: the host's side keeps what its relay told */
	secret_of(3, secret);
	relay_at(RELAY, &relay_candidate);
	host_side = p2p_relay_attempt_start(joiner_peer, secret, 0, &relay_candidate, now);
	answer_attempt(RELAY, 1, channel, C);
	REQUIRE(p2p_relay_attempt(host_side, &state) && state.paired && state.peer_address.address == C);
	/* ... the joiner's believes nothing */
	relay_at(RELAY2, &relay_candidate);
	joiner_side = p2p_relay_attempt_start(host_peer, secret, 1, &relay_candidate, now);
	answer_attempt(RELAY2, 1, channel, C);
	REQUIRE(p2p_relay_attempt(joiner_side, &state) && state.paired && state.peer_address.address == 0);
	/* a tunnel packet the relay brings: delivered, from the relay alone */
	{
		unsigned char packet[2048];
		int size = data_packet(packet, channel, 64);

		packet[1] = 'd';
		p2p_relay_received(packet, size, C, PORT, now);
		REQUIRE(!delivered_calls);
		p2p_relay_received(packet, size, RELAY2, PORT, now);
		REQUIRE(delivered_calls == 1 && delivered_index == joiner_side && delivered_size == 64 &&
			delivered[0] == P2P_TUNNEL_MAGIC);
	}
	return 0;
}

/* offers: tagged with the session's secret, of the other side, whole; a
peer's taken one a second at most and a few in all; a peer's relays
merged, none twice, a few at most */
static int case_offers(void)
{
	unsigned char secret[P2P_SHA256_SIZE], wrong[P2P_SHA256_SIZE], body[P2P_RELAY_OFFER_MAXIMUM_SIZE];
	unsigned char identifier[6] = { 7, 7, 7, 7, 7, 7 };
	struct p2p_candidate relays[P2P_RELAY_OFFERED], read[P2P_RELAY_OFFERED], merged[P2P_RELAY_PEER_RELAYS];
	struct p2p_relay_offers_heard heard;
	int size, count, index, taken;

	secret_of(4, secret);
	secret_of(5, wrong);
	for (index = 0; index < P2P_RELAY_OFFERED; index++)
		relay_at(RELAY + (unsigned long)index, &relays[index]);
	size = p2p_relay_offer_write(identifier, 1, relays, P2P_RELAY_OFFERED, secret, body);
	REQUIRE(size == P2P_RELAY_OFFER_MAXIMUM_SIZE);
	count = p2p_relay_offer_read(body, size, 1, secret, read);
	REQUIRE(count == P2P_RELAY_OFFERED && read[0].address == relays[0].address && read[0].port == relays[0].port &&
		read[2].address == relays[2].address && read[2].port == relays[2].port);
	/* another session's secret (the invite's holders: signalling's seal is
	theirs), the other side's, a byte short or long, a byte changed */
	REQUIRE(p2p_relay_offer_read(body, size, 1, wrong, read) < 0);
	REQUIRE(p2p_relay_offer_read(body, size, 0, secret, read) < 0);
	REQUIRE(p2p_relay_offer_read(body, size - 1, 1, secret, read) < 0);
	REQUIRE(p2p_relay_offer_read(body, size + 1, 1, secret, read) < 0);
	body[P2P_RELAY_OFFER_HEADER_SIZE] ^= 1;
	REQUIRE(p2p_relay_offer_read(body, size, 1, secret, read) < 0);
	/* an empty offer (this machine binds, and has none of its own) */
	size = p2p_relay_offer_write(identifier, 0, relays, 0, secret, body);
	REQUIRE(p2p_relay_offer_read(body, size, 0, secret, read) == 0);
	/* one a second, a few in all */
	memset(&heard, 0, sizeof(heard));
	REQUIRE(p2p_relay_offer_take(&heard, now));
	REQUIRE(!p2p_relay_offer_take(&heard, now + 10));
	REQUIRE(!p2p_relay_offer_take(&heard, now + 999));
	REQUIRE(p2p_relay_offer_take(&heard, now + 1000));
	for (index = 0, taken = 0; index < 100; index++)
		taken += p2p_relay_offer_take(&heard, now + 2000 + (unsigned long)index * 100);
	REQUIRE(taken > 0 && heard.count <= 8);
	for (index = 0; index < 100; index++)
		REQUIRE(!p2p_relay_offer_take(&heard, now + 100000 + (unsigned long)index * 5000));
	/* merged: none twice, P2P_RELAY_PEER_RELAYS at most */
	count = 0;
	REQUIRE(p2p_relay_merge(merged, &count, relays, P2P_RELAY_OFFERED) == P2P_RELAY_OFFERED);
	REQUIRE(p2p_relay_merge(merged, &count, relays, P2P_RELAY_OFFERED) == 0 && count == P2P_RELAY_OFFERED);
	for (index = 0; index < 10; index++)
	{
		struct p2p_candidate more;

		relay_at(0x20000000 + (unsigned long)index, &more);
		p2p_relay_merge(merged, &count, &more, 1);
	}
	REQUIRE(count == P2P_RELAY_PEER_RELAYS);
	return 0;
}

/* the path a peer is reached by: a direct path takes over from a relay
only once heard for a few seconds without a gap (or at once if the relay
is silent); another relay takes over only from a silent carrier (a relay
that died: failover); the relays go once the direct path has lasted */
static int case_path(void)
{
	struct p2p_relay_path path;
	unsigned long start = now;
	int step;

	memset(&path, 0, sizeof(path));
	path.carrier = 1;
	path.carrier_heard = now;
	/* a direct packet now and then, with gaps: never taken */
	for (step = 0; step < 10; step++)
	{
		now += 3000;
		REQUIRE(!p2p_relay_path_heard(&path, 1, now));
		REQUIRE(!p2p_relay_path_heard(&path, 0, now));
	}
	REQUIRE(path.carrier == 1);
	/* a direct path heard every second: taken after DIRECT_SWITCH_TIME */
	for (step = 0; step < 4; step++)
	{
		now += 1000;
		REQUIRE(!p2p_relay_path_heard(&path, 1, now));
		REQUIRE(!p2p_relay_path_heard(&path, 0, now));
	}
	now += 1000;
	p2p_relay_path_heard(&path, 1, now);
	REQUIRE(p2p_relay_path_heard(&path, 0, now) && path.carrier == 0);
	/* the relays kept a while, then let go */
	REQUIRE(!p2p_relay_path_relays_done(&path, now + 1000));
	REQUIRE(p2p_relay_path_relays_done(&path, now + 31000));
	/* a relay's packet while the direct path lives: not taken; once it
	is silent, taken (back to the relay) */
	now += 1000;
	REQUIRE(!p2p_relay_path_heard(&path, 2, now));
	REQUIRE(!p2p_relay_path_heard(&path, 0, now));
	now += 3000;
	REQUIRE(p2p_relay_path_heard(&path, 2, now) && path.carrier == 2);
	/* failover: another relay while the carrier is heard, not taken */
	now += 500;
	REQUIRE(!p2p_relay_path_heard(&path, 3, now));
	REQUIRE(!p2p_relay_path_silent(&path, now));
	/* the carrier silent: another relay's packet takes over at once */
	now += 3000;
	REQUIRE(p2p_relay_path_silent(&path, now));
	REQUIRE(p2p_relay_path_heard(&path, 3, now) && path.carrier == 3);
	/* the carrier lost (p2p.c's -1): the first path heard takes over */
	path.carrier = -1;
	REQUIRE(p2p_relay_path_silent(&path, now));
	REQUIRE(p2p_relay_path_heard(&path, 1, now) && path.carrier == 1);
	/* ... a direct path, at once, while the relay is silent */
	now += 3000;
	REQUIRE(p2p_relay_path_heard(&path, 0, now) && path.carrier == 0);
	(void)start;
	return 0;
}

/* an attempt paired a long time, then demoted (its relay lost the other
side): p2p.c told at once, and timed from the demotion, not its start; a
silent paired attempt is lost */
static int case_demoted(void)
{
	unsigned char secret[P2P_SHA256_SIZE], channel[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
	unsigned char peer[6] = { 1, 2, 3, 4, 5, 6 };
	struct p2p_relay_attempt_state state;
	struct p2p_candidate relay_candidate;
	int index, step;

	secret_of(7, secret);
	relay_at(RELAY, &relay_candidate);
	index = p2p_relay_attempt_start(peer, secret, 0, &relay_candidate, now);
	answer_attempt(RELAY, 1, channel, B);
	REQUIRE(paired_calls == 1);
	/* paired a minute, its keep-alives answered */
	for (step = 0; step < 60; step++)
	{
		now += 1000;
		p2p_relay_update(now);
		answer_attempt(RELAY, 1, channel, B);
	}
	REQUIRE(p2p_relay_attempt(index, &state) && state.paired);
	/* demoted: lost at once, waiting, and not given up for a while */
	answer_attempt(RELAY, 0, channel, 0);
	REQUIRE(lost_calls == 1 && lost_index == index);
	REQUIRE(p2p_relay_attempt(index, &state) && !state.paired && state.peer_address.address == 0);
	now += 1000;
	p2p_relay_update(now);
	REQUIRE(p2p_relay_attempt(index, NULL));
	now += 10000;
	p2p_relay_update(now);
	REQUIRE(p2p_relay_attempt(index, NULL));
	/* paired again, then the relay silent: lost */
	answer_attempt(RELAY, 1, channel, B);
	REQUIRE(paired_calls == 2);
	now += 13000;
	p2p_relay_update(now);
	REQUIRE(!p2p_relay_attempt(index, NULL) && lost_calls == 2);
	/* never paired: given up after its time */
	index = p2p_relay_attempt_start(peer, secret, 0, &relay_candidate, now);
	now += 21000;
	p2p_relay_update(now);
	REQUIRE(!p2p_relay_attempt(index, NULL));
	return 0;
}

/* a bucket refilled a few milliseconds at a time keeps every thousandth:
binds every 10 ms from one address, 20 a second allowed, about 20 a
second taken (not none, as each refill rounded down would make it) */
static int case_buckets(void)
{
	unsigned char token[P2P_RELAY_TOKEN_SIZE], packet[BIND], channel[8], data[2048];
	struct p2p_relay_limits limits;
	int step, answered = 0, before, size, passed;

	relay_on();
	token_of(1, token);
	bind_packet(packet, 0, token, 1, NULL);
	/* (its burst spent first) */
	for (step = 0; step < 40; step++)
		receive(packet, BIND, A);
	before = sent_count;
	for (step = 0; step < 300; step++)
	{
		now += 10;
		receive(packet, BIND, A);
	}
	for (step = before; step < sent_count; step++)
		answered += sent[step].address == A;
	REQUIRE(answered >= 55 && answered <= 61);
	/* bytes: 1000 a second for a pair, 100-byte packets every 20 ms */
	limits_default(&limits, _p2p_relay_any);
	limits.pair_upload = 1000;
	p2p_relay_configure(&limits);
	/* (A's binds refilled first) */
	now += 2000;
	token_of(2, token);
	pair_a_b(token, channel);
	size = data_packet(data, channel, 90);
	for (step = 0; step < 20; step++)
		receive(data, size, A);
	before = sent_count;
	for (step = 0; step < 150; step++)
	{
		now += 20;
		receive(data, size, A);
	}
	passed = data_sent_to(B, before);
	REQUIRE(passed >= 28 && passed <= 31);
	return 0;
}

/* a dedicated server's game ("game"): a pair only where one side is a
machine its tunnel reaches directly; strangers refused */
static int case_game_mode(void)
{
	struct p2p_relay_limits limits;
	unsigned char token[P2P_RELAY_TOKEN_SIZE], channel[8];
	struct p2p_relay_counts counts;

	limits_default(&limits, _p2p_relay_game);
	p2p_relay_configure(&limits);
	token_of(1, token);
	/* two strangers: refused */
	bind_from(A, 0, token, 1);
	bind_from(B, 1, token, 2);
	REQUIRE(bound_to(A, NULL, NULL) == 2 && bound_to(B, NULL, NULL) == 2);
	p2p_relay_counts(&counts);
	REQUIRE(counts.pairs == 0);
	/* one of its game's machines: paired with a stranger */
	peer_addresses[peer_count++] = A;
	bind_from(A, 0, token, 1);
	REQUIRE(bound_to(A, NULL, NULL) == 0);
	bind_from(B, 1, token, 2);
	REQUIRE(bound_to(B, channel, NULL) == 1);
	/* off: nothing at all */
	limits.mode = _p2p_relay_off;
	p2p_relay_configure(&limits);
	token_of(3, token);
	{
		int before = sent_count;

		bind_from(A, 0, token, 5);
		REQUIRE(sent_count == before);
	}
	return 0;
}

static int case_limits(void)
{
	struct p2p_relay_limits limits;
	unsigned char token[P2P_RELAY_TOKEN_SIZE];
	int index;

	limits_default(&limits, _p2p_relay_any);
	limits.maximum_pairs = 3;
	limits.pairs_per_address = 2;
	p2p_relay_configure(&limits);
	/* two from A, a third refused */
	for (index = 0; index < 3; index++)
	{
		token_of(10 + index, token);
		bind_from(A, 0, token, index);
	}
	REQUIRE(bound_to(A, NULL, NULL) == 2);
	token_of(20, token);
	bind_from(B, 0, token, 1);
	REQUIRE(bound_to(B, NULL, NULL) == 0);
	token_of(21, token);
	bind_from(C, 0, token, 1);
	REQUIRE(bound_to(C, NULL, NULL) == 2);
	/* attempts: a few relays for one peer */
	{
		unsigned char secret[P2P_SHA256_SIZE], peer[6] = { 1, 2, 3, 4, 5, 6 };
		struct p2p_candidate relay_candidate;
		int started = 0;

		secret_of(9, secret);
		for (index = 0; index < 20; index++)
		{
			relay_at(0x30000000 + (unsigned long)index, &relay_candidate);
			started += p2p_relay_attempt_start(peer, secret, 0, &relay_candidate, now) >= 0;
		}
		REQUIRE(started == P2P_RELAY_PEER_RELAYS);
	}
	return 0;
}

/* anything at all: no crash, nothing sent to an address that never bound
or spoke, nothing larger than what came */
/* a front's path to a client (p2p_front.c's): paired from the first, one
client's one peer, sent through the front, matched by no relay's answer,
within the peer's attempts, freed in silence, never a release or a bind */
static int case_front_path(void)
{
	static const unsigned char peer[P2P_IDENTIFIER_SIZE] = { 1, 2, 3, 4, 5, 6 };
	static const unsigned char other[P2P_IDENTIFIER_SIZE] = { 9, 9, 9, 9, 9, 9 };
	struct p2p_candidate front_address, client_address;
	struct p2p_relay_attempt_state state;
	unsigned char packet[100], answer[BOUND];
	int index, created, before, count, client;

	relay_at(RELAY, &front_address);
	client_address.address = C;
	client_address.port = PORT;
	index = p2p_relay_front_attempt(peer, &front_address, 77, &client_address, now, &created);
	REQUIRE(index >= 0 && created);
	REQUIRE(p2p_relay_attempt(index, &state) && state.paired && state.peer_address.address == C);
	REQUIRE(p2p_relay_front_attempt(peer, &front_address, 77, &client_address, now, &created) == index && !created);
	/* (another peer's packet through that client: not taken) */
	REQUIRE(p2p_relay_front_attempt(other, &front_address, 77, &client_address, now, &created) < 0);
	/* (sent through the front, with the client's number) */
	memset(packet, 0, sizeof(packet));
	packet[0] = P2P_TUNNEL_MAGIC;
	before = sent_count;
	REQUIRE(p2p_relay_send(index, packet, sizeof(packet)) && front_sent_calls == 1 && front_sent_client == 77);
	REQUIRE(sent_count == before);
	/* (a relay's BOUND or delivery from its address, of zeros, is not its) */
	memset(answer, 0, sizeof(answer));
	answer[0] = P2P_RELAY_MAGIC;
	answer[1] = 'K';
	answer[2] = 2;
	answer[3] = 2;
	p2p_relay_received(answer, BOUND, RELAY, PORT, now);
	REQUIRE(p2p_relay_attempt(index, &state) && state.paired);
	{
		unsigned char delivered_packet[10 + 60];

		memset(delivered_packet, 0, sizeof(delivered_packet));
		delivered_packet[0] = P2P_RELAY_MAGIC;
		delivered_packet[1] = 'd';
		delivered_packet[10] = P2P_TUNNEL_MAGIC;
		p2p_relay_received(delivered_packet, sizeof(delivered_packet), RELAY, PORT, now);
		REQUIRE(delivered_calls == 0);
	}
	/* (a peer's paths at most) */
	for (count = 1, client = 100; client < 120; client++)
		count += p2p_relay_front_attempt(peer, &front_address, (unsigned long)client, &client_address, now,
			&created) >= 0;
	REQUIRE(count == P2P_RELAY_PEER_RELAYS);
	/* (forgotten: no release sent) */
	before = sent_count;
	p2p_relay_forget_peer(peer, 0);
	REQUIRE(sent_count == before && !p2p_relay_attempt(index, NULL));
	/* (kept while heard; never a bind; freed in silence, its loss told) */
	index = p2p_relay_front_attempt(peer, &front_address, 78, &client_address, now, &created);
	REQUIRE(index >= 0);
	before = sent_count;
	lost_calls = 0;
	now += 11000;
	p2p_relay_front_heard(index, now);
	now += 11000;
	p2p_relay_update(now);
	REQUIRE(p2p_relay_attempt(index, NULL) && sent_count == before);
	now += 1000;
	p2p_relay_update(now);
	REQUIRE(!p2p_relay_attempt(index, NULL) && lost_calls == 1);
	/* (dropped unheard of; all a front's forgotten) */
	index = p2p_relay_front_attempt(peer, &front_address, 79, &client_address, now, &created);
	p2p_relay_front_drop(index);
	REQUIRE(!p2p_relay_attempt(index, NULL));
	index = p2p_relay_front_attempt(peer, &front_address, 80, &client_address, now, &created);
	p2p_relay_front_forget();
	REQUIRE(!p2p_relay_attempt(index, NULL));
	return 0;
}

static int case_malformed(void)
{
	unsigned char token[P2P_RELAY_TOKEN_SIZE], channel[8], packet[2048], valid[2048];
	unsigned char secret[P2P_SHA256_SIZE], peer[6] = { 1, 2, 3, 4, 5, 6 };
	struct p2p_candidate relay_candidate;
	int round;

	relay_on();
	token_of(1, token);
	pair_a_b(token, channel);
	secret_of(7, secret);
	relay_at(RELAY, &relay_candidate);
	p2p_relay_attempt_start(peer, secret, 0, &relay_candidate, now);
	for (round = 0; round < 100000; round++)
	{
		unsigned long from = round % 3 == 0 ? A : round % 3 == 1 ? B : (unsigned long)rand();
		int size = rand() % 1600;
		int kind = rand() % 4;
		int index, before = sent_count;

		if (kind == 0)
		{
			for (index = 0; index < size; index++)
				packet[index] = (unsigned char)rand();
		}
		else
		{
			int valid_size = kind == 1 ? bind_packet(valid, rand() % 2, token, rand(), NULL) :
				kind == 2 ? data_packet(valid, channel, 32 + rand() % 1400) : BOUND;

			if (kind == 3)
			{
				valid[0] = P2P_RELAY_MAGIC;
				valid[1] = "KdCX"[rand() % 4];
				valid[2] = 2;
			}
			memcpy(packet, valid, (size_t)valid_size);
			size = valid_size + rand() % 5 - 2;
			if (size < 0)
				size = 0;
			for (index = 0; index < rand() % 4; index++)
				packet[rand() % (size ? size : 1)] ^= (unsigned char)(1 << (rand() % 8));
		}
		if (size > 0)
			packet[0] = rand() % 8 ? P2P_RELAY_MAGIC : packet[0];
		p2p_relay_received(packet, size, from, PORT, now);
		for (index = before; index < sent_count; index++)
		{
			/* (to a side that bound, or its sender) */
			REQUIRE(sent[index].address == A || sent[index].address == B || sent[index].address == from);
			REQUIRE(sent[index].size <= size);
		}
		if (sent_count > MAXIMUM_SENT - 64)
			sent_count = 0;
		now += rand() % 5;
		if (round % 1000 == 0)
			p2p_relay_update(now);
	}
	return 0;
}

int main(int argc, char **argv)
{
	static const struct
	{
		const char *name;
		int (*run)(void);
	} cases[] =
	{
		{ "pairing", case_pairing },
		{ "return-routability", case_return_routability },
		{ "token-per-relay", case_token_per_relay },
		{ "verified-address", case_verified_address },
		{ "offers", case_offers },
		{ "path", case_path },
		{ "demoted", case_demoted },
		{ "buckets", case_buckets },
		{ "game-mode", case_game_mode },
		{ "limits", case_limits },
		{ "malformed", case_malformed },
		{ "front-path", case_front_path },
	};
	size_t index;

	if (argc != 2)
		return 2;
	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
	{
		if (!strcmp(argv[1], cases[index].name))
			return cases[index].run();
	}
	return 2;
}
