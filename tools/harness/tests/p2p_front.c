/*
P2P_FRONT.C (test)

Internet play's fronts (port/linux/src/p2p_front.c): a server's
registration (a cookie that shows its address first, then its operator's
key and signature, which the front's list must allow), the front's public
port forwarding a client's tunnel packets to the server marked with the
client's number and the server's answers back to that client alone, the
clients a registration keeps (per address, those never answered giving way
first, expiry), the registration's port kept for its key, the server's side
renewing and giving up its registration, the front's rates, and that
nothing a front sends to a sender it has not checked is larger than what it
was sent. test_p2p_front.py takes the whole file, with p2p_crypto.c's
SHA-256, HMAC and comparison; Ed25519 is the test's stand-in (a signature is
an HMAC by the key), and the clock, the addresses and what is sent are the
test's. Both sides live in the one process: the server's packets to the
front are handed to the front as from the server's address, and back.
*/

#include "harness.h"

#include "p2p_internal.h"

#define REQUIRE(condition) CHECK(condition, "%s", "")

enum
{
	MAXIMUM_SENT = 8192,
	/* the front's address and control port, the server's address and port
	(network order, as the tunnel's; their values matter only as distinct
	numbers), a client, an attacker */
	FRONT = 0x0A000001,
	FRONT_PORT = 0x5223,
	SERVER = 0x0B000002,
	SERVER_PORT = 0x1234,
	CLIENT = 0x0C000003,
	OTHER = 0x0D000004,
	FIRST_PORT = 2310,
	PORTS = 4,
	/* the packets' sizes (p2p_front.c's) */
	REGISTER = 4 + 32 + 8 + 16 + 64,
	CHALLENGE = 4 + 8 + 16,
	REGISTERED = 4 + 8 + 8 + 4,
	FROM_HEADER = 2 + 8 + 4 + 6,
	TO_HEADER = 2 + 8 + 4,
};

struct sent
{
	int port_index;
	unsigned long address;
	unsigned short port;
	int size;
	unsigned char data[2048];
};

static struct sent sent[MAXIMUM_SENT];
static int sent_count;
static unsigned int random_state = 12345;
static unsigned long now = 1000000;
/* the operator's key the server signs with (0: it has none), and a fault
of its signatures */
static unsigned char signing_key[32];
static int has_signing_key = 1;
static int bad_signatures;
static int verify_calls;
/* what the server was handed */
static int delivered_calls, lost_calls;
static unsigned long delivered_client;
static struct p2p_candidate delivered_address;
static unsigned char delivered[2048];
static int delivered_size;

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

const char *log_address_ipv4(unsigned long address, unsigned short port, char *text, int size)
{
	snprintf(text, (size_t)size, "%08lx:%u", address, (unsigned)port);
	return text;
}

/* Ed25519's stand-in: a signature is an HMAC of the message by the key */
static void fake_signature(const unsigned char *key, const void *message, int size, unsigned char *signature)
{
	memset(signature, 0, 64);
	p2p_hmac_sha256(key, 32, message, size, signature);
}

int p2p_ed25519_verify(const unsigned char *public_key, const void *message, int size,
	const unsigned char *signature)
{
	unsigned char expected[64];

	verify_calls++;
	fake_signature(public_key, message, size, expected);
	return p2p_equal(expected, signature, 64);
}

int p2p_front_sign(const unsigned char *message, int size, unsigned char *public_key, unsigned char *signature)
{
	if (!has_signing_key)
		return 0;
	memcpy(public_key, signing_key, 32);
	if (message)
	{
		fake_signature(signing_key, message, size, signature);
		if (bad_signatures)
			signature[0] ^= 1;
	}
	return 1;
}

void p2p_front_sendto(int port_index, unsigned long address, unsigned short port, const unsigned char *data,
	int size)
{
	if (sent_count == MAXIMUM_SENT || size > (int)sizeof(sent[0].data))
		return;
	sent[sent_count].port_index = port_index;
	sent[sent_count].address = address;
	sent[sent_count].port = port;
	sent[sent_count].size = size;
	memcpy(sent[sent_count].data, data, (size_t)size);
	sent_count++;
}

void p2p_front_delivered(unsigned long client, const struct p2p_candidate *client_address,
	const unsigned char *packet, int size)
{
	delivered_calls++;
	delivered_client = client;
	delivered_address = *client_address;
	delivered_size = size;
	memcpy(delivered, packet, (size_t)size);
}

void p2p_front_lost(void)
{
	lost_calls++;
}

#include "under_test.inc"

/* ---------- helpers */

static unsigned short net16(unsigned short value)
{
	return (unsigned short)(value << 8 | value >> 8);
}

static void key_of(int seed, unsigned char *key)
{
	memset(key, seed, 32);
}

/* a front of PORTS ports, allowing the keys of these seeds */
static void front_on(const int *seeds, int count)
{
	struct p2p_front_limits limits;
	unsigned char keys[8][32];
	int index;

	limits.port_count = PORTS;
	limits.first_port = FIRST_PORT;
	limits.upload = 8192 * 1024;
	p2p_front_configure(&limits);
	for (index = 0; index < count; index++)
		key_of(seeds[index], keys[index]);
	p2p_front_operators((const unsigned char (*)[32])keys, count);
}

static void front_candidate(struct p2p_candidate *candidate)
{
	candidate->address = FRONT;
	candidate->port = FRONT_PORT;
}

/* a server's session (its key's seed) begun with the front */
static void server_begin(int seed)
{
	struct p2p_candidate candidate;

	p2p_front_use(NULL);
	key_of(seed, signing_key);
	front_candidate(&candidate);
	p2p_front_use(&candidate);
}

static void to_front(const unsigned char *packet, int size, unsigned long address, unsigned short port)
{
	unsigned char copy[2048];

	memcpy(copy, packet, (size_t)size);
	p2p_front_received(copy, size, address, port, now);
}

/* what the two sides sent each other since index from, handed on (to the
front as from the server, to the server as from the front), until nothing
more is: the packets handed */
static int pump(int from)
{
	int handed = 0;

	while (from < sent_count && handed < 64)
	{
		struct sent each = sent[from++];

		if (each.port_index != -1)
			continue;
		if (each.address == FRONT && each.port == FRONT_PORT)
			to_front(each.data, each.size, SERVER, SERVER_PORT);
		else if (each.address == SERVER && each.port == SERVER_PORT)
			to_front(each.data, each.size, FRONT, FRONT_PORT);
		else
			continue;
		handed++;
	}
	return handed;
}

/* the server registered (its session begun, the pass run, the packets
exchanged): whether it was */
static int server_registers(int seed)
{
	struct p2p_candidate candidate;
	int before = sent_count;

	server_begin(seed);
	p2p_front_update(now);
	pump(before);
	return p2p_front_public(&candidate);
}

/* time passed a second at a time, the server's registration kept up */
static void advance(unsigned long milliseconds)
{
	while (milliseconds)
	{
		unsigned long step = milliseconds > 1000 ? 1000 : milliseconds;
		int before = sent_count;

		now += step;
		milliseconds -= step;
		p2p_front_update(now);
		pump(before);
	}
}

/* the latest packet of a front's type sent to an address since index from:
its index, -1 if none */
static int sent_of(unsigned long address, char type, int from)
{
	int index;

	for (index = sent_count - 1; index >= from; index--)
	{
		if (sent[index].address == address && sent[index].data[0] == P2P_FRONT_MAGIC && sent[index].data[1] == type)
			return index;
	}
	return -1;
}

/* a tunnel packet of size bytes (its sender's identifier a seed's) */
static void tunnel_packet(unsigned char *packet, int size, int seed)
{
	int index;

	packet[0] = P2P_TUNNEL_MAGIC;
	for (index = 1; index < size; index++)
		packet[index] = (unsigned char)(index * 7 + seed);
}

/* a client's packet at the public port of that index; the FROM sent, its
index (-1: none) */
static int client_sends(int port_index, unsigned long address, unsigned short port, int size)
{
	unsigned char packet[1500];
	int before = sent_count;

	tunnel_packet(packet, size, (int)address);
	p2p_front_public_received(port_index, packet, size, address, port, now);
	return sent_of(SERVER, 'F', before);
}

static unsigned long from_client(int index)
{
	const unsigned char *bytes = sent[index].data + 2 + 8;

	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

/* the server's answer to a client (by the front's TO, sent as from
address): the packet the front sent to a client from a public port since,
its index (-1: none) */
static int server_answers(unsigned long client, int size, unsigned long address, unsigned short port)
{
	unsigned char packet[1500];
	int before = sent_count, index;

	tunnel_packet(packet, size, 99);
	REQUIRE(p2p_front_send(client, packet, size));
	index = sent_of(FRONT, 'T', before);
	REQUIRE(index >= 0);
	to_front(sent[index].data, sent[index].size, address, port);
	for (index = sent_count - 1; index >= before; index--)
	{
		if (sent[index].port_index >= 0)
			return index;
	}
	return -1;
}

/* ---------- cases */

static int case_registration(void)
{
	static const int allowed[] = { 1 };
	struct p2p_candidate candidate;
	int index, before, verified;

	front_on(allowed, 1);
	server_begin(1);
	p2p_front_update(now);
	/* (first without a cookie: a challenge, smaller than the REGISTER) */
	index = sent_of(FRONT, 'R', 0);
	REQUIRE(index >= 0 && sent[index].size == REGISTER);
	to_front(sent[index].data, sent[index].size, SERVER, SERVER_PORT);
	index = sent_of(SERVER, 'C', 0);
	REQUIRE(index >= 0 && sent[index].size == CHALLENGE && sent[index].size < REGISTER);
	REQUIRE(sent_of(SERVER, 'G', 0) < 0);
	to_front(sent[index].data, sent[index].size, FRONT, FRONT_PORT);
	/* (then with it: registered, on the first port) */
	index = sent_of(FRONT, 'R', index);
	REQUIRE(index >= 0);
	to_front(sent[index].data, sent[index].size, SERVER, SERVER_PORT);
	index = sent_of(SERVER, 'G', 0);
	REQUIRE(index >= 0 && sent[index].size == REGISTERED && sent[index].data[3] == 0);
	REQUIRE(!p2p_front_public(&candidate));
	to_front(sent[index].data, sent[index].size, FRONT, FRONT_PORT);
	REQUIRE(p2p_front_public(&candidate));
	REQUIRE(candidate.address == FRONT && candidate.port == net16(FIRST_PORT));
	/* (kept alive every few seconds, the same REGISTER: not checked again) */
	verified = verify_calls;
	before = sent_count;
	now += 3000;
	p2p_front_update(now);
	REQUIRE(sent_of(FRONT, 'R', before) < 0);
	now += 1000;
	p2p_front_update(now);
	REQUIRE(sent_of(FRONT, 'R', before) >= 0);
	pump(before);
	REQUIRE(sent_of(SERVER, 'G', before) >= 0);
	REQUIRE(verify_calls == verified);
	REQUIRE(p2p_front_public(&candidate));
	/* (a new cookie period: challenged, signed again, still registered on
	its port) */
	for (index = 0; index < 30; index++)
	{
		now += 4000;
		before = sent_count;
		p2p_front_update(now);
		pump(before);
	}
	REQUIRE(p2p_front_public(&candidate) && candidate.port == net16(FIRST_PORT));
	REQUIRE(lost_calls == 0);
	return 0;
}

static int case_return_routability(void)
{
	static const int allowed[] = { 1 };
	struct p2p_candidate candidate;
	struct p2p_front_counts counts;
	unsigned char good[REGISTER];
	int index, before;

	front_on(allowed, 1);
	server_begin(1);
	p2p_front_update(now);
	index = sent_of(FRONT, 'R', 0);
	to_front(sent[index].data, sent[index].size, SERVER, SERVER_PORT);
	index = sent_of(SERVER, 'C', 0);
	to_front(sent[index].data, sent[index].size, FRONT, FRONT_PORT);
	/* (a REGISTER with the server's cookie, from another address: only a
	challenge, to that address, and no registration) */
	index = sent_of(FRONT, 'R', index);
	memcpy(good, sent[index].data, REGISTER);
	before = sent_count;
	to_front(good, REGISTER, OTHER, SERVER_PORT);
	REQUIRE(sent_of(OTHER, 'G', before) < 0);
	REQUIRE(sent_of(OTHER, 'C', before) >= 0);
	/* (... or another port of the server's address) */
	before = sent_count;
	to_front(good, REGISTER, SERVER, SERVER_PORT + 1);
	REQUIRE(sent_of(SERVER, 'G', before) < 0);
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 0);
	/* (from the server's own, two cookie periods later: stale) */
	now += 61000;
	before = sent_count;
	to_front(good, REGISTER, SERVER, SERVER_PORT);
	REQUIRE(sent_of(SERVER, 'G', before) < 0);
	REQUIRE(sent_of(SERVER, 'C', before) >= 0);
	/* (and it registers by answering) */
	before = sent_count;
	p2p_front_update(now);
	pump(before);
	REQUIRE(p2p_front_public(&candidate));
	return 0;
}

static int case_unknown_key(void)
{
	static const int allowed[] = { 1 };
	struct p2p_candidate candidate;
	struct p2p_front_counts counts;
	int index, before;

	/* (a key the front does not allow: refused once its address showed) */
	front_on(allowed, 1);
	REQUIRE(!server_registers(2));
	index = sent_of(SERVER, 'G', 0);
	REQUIRE(index >= 0 && sent[index].data[3] == 1 && sent[index].size < REGISTER);
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 0);
	/* (asked again only after a while) */
	before = sent_count;
	now += 10000;
	p2p_front_update(now);
	REQUIRE(sent_of(FRONT, 'R', before) < 0);
	now += 25000;
	p2p_front_update(now);
	REQUIRE(sent_of(FRONT, 'R', before) >= 0);
	/* (an allowed key with a signature that is not its own: nothing) */
	bad_signatures = 1;
	before = sent_count;
	REQUIRE(!server_registers(1));
	REQUIRE(sent_of(SERVER, 'G', before) < 0);
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 0);
	bad_signatures = 0;
	/* (no key at all: no REGISTER) */
	has_signing_key = 0;
	before = sent_count;
	server_begin(1);
	p2p_front_update(now);
	REQUIRE(sent_of(FRONT, 'R', before) < 0);
	has_signing_key = 1;
	/* (allowed, registered; then taken off the list: ended at once) */
	REQUIRE(server_registers(1));
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 1);
	p2p_front_operators(NULL, 0);
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 0);
	REQUIRE(client_sends(0, CLIENT, 0x4000, 100) < 0);
	(void)candidate;
	return 0;
}

static int case_forwarding(void)
{
	static const int allowed[] = { 1 };
	unsigned char packet[1500], to[2048];
	unsigned long client;
	int index, to_size, before;

	front_on(allowed, 1);
	REQUIRE(server_registers(1));
	/* (a client's tunnel packet: to the server, its number and address
	before it) */
	index = client_sends(0, CLIENT, 0x4000, 100);
	REQUIRE(index >= 0 && sent[index].size == FROM_HEADER + 100);
	REQUIRE(sent[index].address == SERVER && sent[index].port == SERVER_PORT && sent[index].port_index == -1);
	client = from_client(index);
	to_front(sent[index].data, sent[index].size, FRONT, FRONT_PORT);
	REQUIRE(delivered_calls == 1 && delivered_client == client && delivered_size == 100);
	REQUIRE(delivered_address.address == CLIENT && delivered_address.port == 0x4000);
	tunnel_packet(packet, 100, (int)CLIENT);
	REQUIRE(!memcmp(delivered, packet, 100));
	/* (the same from another address than the front's: not taken) */
	to_front(sent[index].data, sent[index].size, OTHER, FRONT_PORT);
	REQUIRE(delivered_calls == 1);
	/* (the server's answer: from the port, to the client, as it was) */
	index = server_answers(client, 200, SERVER, SERVER_PORT);
	REQUIRE(index >= 0 && sent[index].port_index == 0 && sent[index].address == CLIENT && sent[index].port == 0x4000);
	tunnel_packet(packet, 200, 99);
	REQUIRE(sent[index].size == 200 && !memcmp(sent[index].data, packet, 200));
	/* (a TO from another address, or with another session, or to a number
	no client has: nothing) */
	before = sent_count;
	REQUIRE(p2p_front_send(client, packet, 200));
	index = sent_of(FRONT, 'T', before);
	to_size = sent[index].size;
	memcpy(to, sent[index].data, (size_t)to_size);
	to_front(to, to_size, OTHER, SERVER_PORT);
	to_front(to, to_size, SERVER, SERVER_PORT + 1);
	to[2] ^= 1;
	to_front(to, to_size, SERVER, SERVER_PORT);
	to[2] ^= 1;
	to[2 + 8 + 3] ^= 1;
	to_front(to, to_size, SERVER, SERVER_PORT);
	for (index = before; index < sent_count; index++)
		REQUIRE(sent[index].port_index < 0);
	/* (no tunnel packet, or one not of a tunnel packet's sizes, or to a
	port no server registered: not passed on) */
	before = sent_count;
	packet[0] = P2P_RELAY_MAGIC;
	p2p_front_public_received(0, packet, 100, CLIENT, 0x4000, now);
	tunnel_packet(packet, 100, 1);
	p2p_front_public_received(0, packet, P2P_TUNNEL_MINIMUM_PACKET - 1, CLIENT, 0x4000, now);
	p2p_front_public_received(1, packet, 100, CLIENT, 0x4000, now);
	p2p_front_public_received(PORTS, packet, 100, CLIENT, 0x4000, now);
	p2p_front_public_received(-1, packet, 100, CLIENT, 0x4000, now);
	REQUIRE(sent_count == before);
	return 0;
}

static int case_clients(void)
{
	static const int allowed[] = { 1 };
	struct p2p_front_counts counts;
	unsigned long clients[64];
	int index;

	front_on(allowed, 1);
	REQUIRE(server_registers(1));
	/* (a few from one address) */
	for (index = 0; index < 8; index++)
		REQUIRE(client_sends(0, CLIENT, (unsigned short)(0x4000 + index), 60) >= 0);
	REQUIRE(client_sends(0, CLIENT, 0x4100, 60) < 0);
	/* (those never answered go after a few seconds) */
	advance(5000);
	p2p_front_counts(&counts);
	REQUIRE(counts.clients == 0);
	/* (a full table: one never answered gives way) */
	for (index = 0; index < 64; index++)
	{
		int from = client_sends(0, OTHER + (unsigned long)index * 256, 0x5000, 60);

		REQUIRE(from >= 0);
		clients[index] = from_client(from);
		now += 1;
	}
	REQUIRE(client_sends(0, CLIENT, 0x4000, 60) >= 0);
	/* (and once every one was answered, none does) */
	for (index = 1; index < 64; index++)
		REQUIRE(server_answers(clients[index], 60, SERVER, SERVER_PORT) >= 0);
	REQUIRE(server_answers(clients[0], 60, SERVER, SERVER_PORT) < 0);
	now += 1;
	REQUIRE(server_answers(from_client(client_sends(0, CLIENT, 0x4000, 60)), 60, SERVER, SERVER_PORT) >= 0);
	REQUIRE(client_sends(0, CLIENT + 256, 0x4000, 60) < 0);
	/* (an answered client, silent: forgotten after its silence) */
	advance(19000);
	p2p_front_counts(&counts);
	REQUIRE(counts.clients == 64);
	advance(2000);
	p2p_front_counts(&counts);
	REQUIRE(counts.clients == 0);
	return 0;
}

static int case_expiry(void)
{
	static const int allowed[] = { 1, 2 };
	struct p2p_candidate candidate;
	struct p2p_front_counts counts;
	int before;

	front_on(allowed, 2);
	REQUIRE(server_registers(1));
	REQUIRE(client_sends(0, CLIENT, 0x4000, 60) >= 0);
	/* (the server gone quiet: its registration ends, its clients too) */
	p2p_front_use(NULL);
	now += 15000;
	p2p_front_update(now);
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 0 && counts.clients == 0);
	REQUIRE(client_sends(0, CLIENT, 0x4000, 60) < 0);
	/* (another key: a port never given; the first key back: its own) */
	REQUIRE(server_registers(2));
	REQUIRE(p2p_front_public(&candidate) && candidate.port == net16(FIRST_PORT + 1));
	REQUIRE(server_registers(1));
	REQUIRE(p2p_front_public(&candidate) && candidate.port == net16(FIRST_PORT));
	p2p_front_counts(&counts);
	REQUIRE(counts.registered == 2);
	/* (the server's side: its front silent, the registration given up, the
	paths through it told lost, its address withdrawn; asked again each
	second) */
	lost_calls = 0;
	now += 12000;
	before = sent_count;
	p2p_front_update(now);
	REQUIRE(!p2p_front_public(&candidate));
	REQUIRE(lost_calls == 1);
	REQUIRE(sent_of(FRONT, 'R', before) >= 0);
	before = sent_count;
	now += 1000;
	p2p_front_update(now);
	REQUIRE(sent_of(FRONT, 'R', before) >= 0);
	pump(before);
	REQUIRE(p2p_front_public(&candidate));
	return 0;
}

static int case_limits(void)
{
	static const int allowed[] = { 1 };
	struct p2p_front_counts counts;
	int index, passed = 0;

	front_on(allowed, 1);
	REQUIRE(server_registers(1));
	/* (one client's bytes a second: a burst of 256 KB, then dropped) */
	for (index = 0; index < 400; index++)
		passed += client_sends(0, CLIENT, 0x4000, 1400) >= 0;
	REQUIRE(passed > 150 && passed <= 256 * 1024 / 1400 + 1);
	p2p_front_counts(&counts);
	REQUIRE(counts.dropped >= 400 - (unsigned long)passed);
	/* (refilled with time, by thousandths: a short wait passes a little) */
	now += 50;
	REQUIRE(client_sends(0, CLIENT, 0x4000, 1400) >= 0);
	/* (REGISTERs answered a second in all: a flood from forged addresses
	is cut short) */
	{
		unsigned char packet[REGISTER];
		int answered = 0, before = sent_count;

		memset(packet, 0, sizeof(packet));
		packet[0] = P2P_FRONT_MAGIC;
		packet[1] = 'R';
		packet[2] = 1;
		for (index = 0; index < 1000; index++)
			to_front(packet, REGISTER, OTHER + (unsigned long)index, 0x6000);
		for (index = before; index < sent_count; index++)
			answered += sent[index].data[1] == 'C';
		REQUIRE(answered > 0 && answered <= 200);
	}
	return 0;
}

/* nothing sent to an address the front has not checked is more than it
was sent, whatever it was sent; nothing reaches a client the server did not
answer */
static int case_no_amplification(void)
{
	static const int allowed[] = { 1 };
	unsigned char packet[1600];
	int round, challenged = 0;

	front_on(allowed, 1);
	REQUIRE(server_registers(1));
	sent_count = 0;
	for (round = 0; round < 100000; round++)
	{
		unsigned long address = OTHER + (unsigned long)(rand() % 256) * 65536;
		int size = rand() % 1600;
		int kind = rand() % 4;
		int index, before = sent_count;
		int answered = 0;

		for (index = 0; index < size; index++)
			packet[index] = (unsigned char)rand();
		if (size > 0 && rand() % 2)
			packet[0] = kind == 3 ? P2P_TUNNEL_MAGIC : P2P_FRONT_MAGIC;
		if (size > 1 && kind < 3 && rand() % 2)
			packet[1] = "RTCGF"[rand() % 5];
		if (size > 2 && rand() % 2)
			packet[2] = 1;
		/* (a REGISTER's shape, now and then: the one packet answered) */
		if (kind == 0 && rand() % 4 == 0)
		{
			size = REGISTER;
			packet[0] = P2P_FRONT_MAGIC;
			packet[1] = 'R';
			packet[2] = 1;
			packet[3] = 0;
		}
		if (kind == 3)
			p2p_front_public_received(rand() % PORTS, packet, size, address, 0x7000, now);
		else
			to_front(packet, size, address, 0x7000);
		for (index = before; index < sent_count; index++)
		{
			/* (to the server alone, else back to the sender) */
			if (sent[index].address == SERVER && sent[index].port == SERVER_PORT)
				continue;
			REQUIRE(sent[index].address == address && sent[index].port_index == -1);
			answered += sent[index].size;
			challenged++;
		}
		/* (no answer larger than what it answers) */
		REQUIRE(answered <= size);
		if (sent_count > MAXIMUM_SENT - 64)
			sent_count = 0;
		now += rand() % 5;
		if (round % 1000 == 0)
		{
			int before_update = sent_count;

			p2p_front_update(now);
			pump(before_update);
		}
	}
	/* (the fuzz reached the answers it checks) */
	REQUIRE(challenged > 100);
	return 0;
}

/* the server's side, sent anything from its front's address: never a crash,
nothing delivered without the session */
static int case_malformed(void)
{
	static const int allowed[] = { 1 };
	unsigned char packet[1600];
	int round;

	front_on(allowed, 1);
	REQUIRE(server_registers(1));
	delivered_calls = 0;
	for (round = 0; round < 100000; round++)
	{
		int size = rand() % 1600;
		int index;

		for (index = 0; index < size; index++)
			packet[index] = (unsigned char)rand();
		if (size > 0)
			packet[0] = P2P_FRONT_MAGIC;
		if (size > 1 && rand() % 2)
			packet[1] = "FGC"[rand() % 3];
		p2p_front_received(packet, size, FRONT, FRONT_PORT, now);
		if (sent_count > MAXIMUM_SENT - 64)
			sent_count = 0;
	}
	REQUIRE(delivered_calls == 0);
	return 0;
}

static int case_operators_file(void)
{
	unsigned char keys[8][32];
	static const char text[] =
		"# the operators this front serves\n"
		"ed25519:0101010101010101010101010101010101010101010101010101010101010101\n"
		"  0202020202020202020202020202020202020202020202020202020202020202  # bare, indented\n"
		"ed25519:03030303030303030303030303030303030303030303030303030303030303\n"
		"ed25519:0404040404040404040404040404040404040404040404040404040404040404x\n"
		"ed25519:0505050505050505050505050505050505050505050505050505050505050505\r\n"
		"ed25519:zz06060606060606060606060606060606060606060606060606060606060606\n"
		"\n";

	REQUIRE(p2p_front_operators_parse(text, keys, 8) == 3);
	REQUIRE(keys[0][0] == 1 && keys[0][31] == 1 && keys[1][0] == 2 && keys[2][0] == 5);
	REQUIRE(p2p_front_operators_parse(text, keys, 1) == 1);
	REQUIRE(p2p_front_operators_parse("", keys, 8) == 0);
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
		{ "registration", case_registration },
		{ "return-routability", case_return_routability },
		{ "unknown-key", case_unknown_key },
		{ "forwarding", case_forwarding },
		{ "clients", case_clients },
		{ "expiry", case_expiry },
		{ "limits", case_limits },
		{ "no-amplification", case_no_amplification },
		{ "malformed", case_malformed },
		{ "operators-file", case_operators_file },
	};
	size_t index;

	if (argc != 2)
		return 2;
	srand(7);
	for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++)
	{
		if (!strcmp(argv[1], cases[index].name))
			return cases[index].run();
	}
	return 2;
}
