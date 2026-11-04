/*
P2P_FRONT.C

Internet play's fronts: a public UDP port, on a relay of halo --relay
(server.front_ports), that a server or host behind a NAT no one can punch
through (one that gives each destination a port of its own) registers with,
so that any machine can reach it, an OpenCE one too. The front is no more
than one more address the server lists in its signalling answer (beside its
own, as it lists network.public_address): a joiner punches to it as to any
of the host's addresses, and the front passes each packet that arrives at the
server's public port down the one session the server keeps with it, marked
with a client number of the front's, and passes the server's answers to that
client back out of the same public port. Its public port takes packets from
any address and port (it is full-cone), so a joiner behind any NAT reaches
it. The tunnel's packets stay sealed end to end with keys only the two
machines have (p2p.c), so a front reads and changes nothing of them, and
the joiner cannot tell it from the host: nothing a joiner sends or receives
is new.

A front forwards only for the servers its operator allowed: each registers
with its operator's publisher key (server_master.c's, halo --keygen's), and
the front keeps a list of the keys it allows (server.front_operators_file).
Its packets, on the tunnel's UDP socket at the server and the relay's own at
the front, are told from others by their first byte (P2P_FRONT_MAGIC):

- REGISTER (server to front): magic, 'R', version, 0, the publisher key
  (32), a nonce (8), the front's cookie (16; zeros at first), and an Ed25519
  signature (64) by the key of a label, the key, the nonce and the cookie.
  Sent every REGISTER_INTERVAL until registered, then every
  KEEP_ALIVE_INTERVAL (which keeps the server's NAT's mapping).
- CHALLENGE (front to server): magic, 'C', version, 0, the REGISTER's
  nonce, a cookie: an HMAC, by a secret of the front's, of the address and
  port the REGISTER came from, its key and the time (COOKIE_TIME). A
  REGISTER is taken only with a cookie the front gave its address lately,
  so a REGISTER sent in another's name (its source forged) is answered
  only with a challenge, smaller than it.
- REGISTERED (front to server): magic, 'G', version, state (registered,
  refused: its key is not allowed, full: no port is free), the REGISTER's
  nonce, the registration's session (8 random bytes), the public port, 2
  zeros. Smaller than a REGISTER.
- FROM (front to server): magic, 'F', the session, the client's number (4),
  the client's IPv4 address and port as the front saw them, then one tunnel
  packet the client sent to the public port.
- TO (server to front): magic, 'T', the session, the client's number, then
  one tunnel packet, which the front sends to that client from the public
  port.

The front sends nothing to an address that did not show it can receive
(the cookie) but the server's own answers, and those only to a client that
reached the server's port: no front is an open reflector, and none of its
answers to a sender it has not checked is larger than what it was sent.
Its limits: REGISTERs answered and signatures checked a second; clients a
registration keeps (CLIENTS; those the server never answered give way to
new ones first, and go after a few seconds), per address; bytes a second
for each client, each registration and the front in all. A registration not
renewed for REGISTRATION_SILENCE ends, and its port is kept a while for its
key (the server's listed address stays the same when it comes back).

The server's side keeps its registration (p2p_front_use names the front)
and hands each tunnel packet from a client to p2p.c (p2p_front_delivered),
which reaches the client through a relay attempt of the front's
(p2p_relay.c's p2p_relay_front_attempt): a path "front and client number",
with the client's address as the front saw it for its bans and votes.
Everything here runs on the p2p thread, under p2p_lock (or halo --relay's
loop: p2p.c's p2p_relay_run).
*/

#include "platform.h"
#include "posix.h"
#include "p2p_internal.h"
#include "log_address.h"

#include <string.h>

enum
{
	FRONT_VERSION = 1,
	NONCE_SIZE = 8,
	SESSION_SIZE = 8,
	COOKIE_SIZE = 16,
	CLIENT_ID_SIZE = 4,
	/* magic, 'R', version, 0, key, nonce, cookie, signature */
	REGISTER_SIZE = 4 + P2P_FRONT_KEY_SIZE + NONCE_SIZE + COOKIE_SIZE + P2P_FRONT_SIGNATURE_SIZE,
	/* what the signature covers after its label: key, nonce, cookie */
	SIGNED_SIZE = P2P_FRONT_KEY_SIZE + NONCE_SIZE + COOKIE_SIZE,
	/* magic, 'C', version, 0, nonce, cookie */
	CHALLENGE_SIZE = 4 + NONCE_SIZE + COOKIE_SIZE,
	/* magic, 'G', version, state, nonce, session, port, 2 zeros */
	REGISTERED_SIZE = 4 + NONCE_SIZE + SESSION_SIZE + 4,
	/* magic, 'F', session, client, its address and port; a tunnel packet */
	FROM_HEADER_SIZE = 2 + SESSION_SIZE + CLIENT_ID_SIZE + 6,
	/* magic, 'T', session, client; a tunnel packet */
	TO_HEADER_SIZE = 2 + SESSION_SIZE + CLIENT_ID_SIZE,

	/* a registration's clients at once, and from one address */
	CLIENTS = 64,
	CLIENTS_PER_ADDRESS = 8,

	/* milliseconds */
	REGISTER_INTERVAL = 1000,
	KEEP_ALIVE_INTERVAL = 4000,
	/* a front refused this server (its key not allowed, or no port free):
	asked again after this long */
	REFUSED_INTERVAL = 30000,
	/* a server that heard nothing from its front in this long is no longer
	registered (its listed address withdrawn) */
	REGISTERED_SILENCE = 12000,
	/* a cookie is good in the period it was made in and the next */
	COOKIE_TIME = 30000,
	/* the front's: a registration not renewed in this long ends; a client
	not heard in this long is forgotten, one the server never answered
	sooner */
	REGISTRATION_SILENCE = 15000,
	CLIENT_SILENCE = 20000,
	UNANSWERED_TIME = 5000,
	BUCKET_BURST_TIME = 1000,

	/* the front's rates: REGISTERs answered and signatures checked a
	second in all; bytes a second for a registration (both ways) and a
	client */
	REGISTERS_PER_SECOND = 200,
	VERIFIES_PER_SECOND = 50,
	REGISTRATION_UPLOAD = 2 * 1024 * 1024,
	CLIENT_UPLOAD = 256 * 1024,
};

enum
{
	_type_register = 'R',
	_type_challenge = 'C',
	_type_registered = 'G',
	_type_from = 'F',
	_type_to = 'T',
};

enum
{
	_registered_ok,
	_registered_refused,
	_registered_full,
};

/* the server's registration */
enum
{
	_session_none,
	_session_registering,
	_session_registered,
	_session_refused,
};

struct bucket
{
	long long left;
	unsigned long time;
	int started;
};

struct client
{
	int used;
	unsigned long id;
	unsigned long address;
	unsigned short port;
	unsigned long heard_time, created_time;
	/* the server sent it something (it reached the server's port, and the
	server took its packets: kept before those never answered) */
	int answered;
	struct bucket bytes;
};

/* a public port's registration (its index is the port's) */
struct registration
{
	int used;
	/* the key registered last (kept once the registration ended: the port
	is given back to it) */
	int has_key;
	unsigned char key[P2P_FRONT_KEY_SIZE];
	unsigned long address;
	unsigned short port;
	unsigned char session[SESSION_SIZE];
	unsigned long heard_time, ended_time;
	/* the last REGISTER taken: the same again (the keep-alive) is not
	checked again */
	unsigned char accepted[REGISTER_SIZE];
	struct bucket bytes;
	struct client clients[CLIENTS];
	unsigned long forwarded;
};

static struct
{
	/* the front's */
	struct p2p_front_limits limits;
	unsigned char secret[P2P_SHA256_SIZE];
	int has_secret;
	unsigned char operators[P2P_FRONT_MAXIMUM_OPERATORS][P2P_FRONT_KEY_SIZE];
	int operator_count;
	struct registration registrations[P2P_FRONT_MAXIMUM_PORTS];
	struct bucket registers;
	struct bucket verifies;
	struct bucket bytes;
	struct p2p_front_counts counts;

	/* the server's */
	struct p2p_candidate front;
	int state;
	unsigned char nonce[NONCE_SIZE];
	unsigned char cookie[COOKIE_SIZE];
	/* the REGISTER sent, signed for the cookie it carries */
	unsigned char packet[REGISTER_SIZE];
	int signed_cookie;
	unsigned char session[SESSION_SIZE];
	unsigned short public_port;
	int sent;
	unsigned long sent_time, heard_time;
	int unsigned_logged;
} front;

static int front_elapsed(unsigned long now, unsigned long since, unsigned long time)
{
	return (unsigned long)(now - since) >= time;
}

static void put_long(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

static unsigned long get_long(const unsigned char *bytes)
{
	return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 | (unsigned long)bytes[2] << 8 | bytes[3];
}

/* whether amount can be spent at rate a second (0: none can), spending it;
in thousandths, as p2p_relay.c's buckets */
static int bucket_spend(struct bucket *bucket, long rate, long amount, unsigned long now)
{
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

/* what a REGISTER's signature covers: a label, the key, nonce and cookie */
static int signed_message(const unsigned char *key_nonce_cookie, unsigned char *message)
{
	static const char label[] = "openh1 front register 1";

	memcpy(message, label, sizeof(label) - 1);
	memcpy(message + sizeof(label) - 1, key_nonce_cookie, SIGNED_SIZE);
	return (int)(sizeof(label) - 1) + SIGNED_SIZE;
}

/* ---------- the operators' keys */

static int hex_digit(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

int p2p_front_operators_parse(const char *text, unsigned char (*keys)[P2P_FRONT_KEY_SIZE], int maximum)
{
	int count = 0;

	while (text && *text && count < maximum)
	{
		const char *line = text;
		const char *end = line;
		int index;

		while (*end && *end != '\n')
			end++;
		text = *end ? end + 1 : end;
		while (line < end && (*line == ' ' || *line == '\t'))
			line++;
		if (end - line >= 8 && !memcmp(line, "ed25519:", 8))
			line += 8;
		/* (64 hexadecimal digits, then the line's end, a space or a
		comment; anything else is no key) */
		if (end - line < 2 * P2P_FRONT_KEY_SIZE)
			continue;
		for (index = 0; index < 2 * P2P_FRONT_KEY_SIZE && hex_digit(line[index]) >= 0; index++)
			;
		if (index < 2 * P2P_FRONT_KEY_SIZE || (line + index < end && line[index] != ' ' && line[index] != '\t' &&
			line[index] != '\r' && line[index] != '#'))
		{
			continue;
		}
		for (index = 0; index < P2P_FRONT_KEY_SIZE; index++)
			keys[count][index] = (unsigned char)(hex_digit(line[2 * index]) << 4 | hex_digit(line[2 * index + 1]));
		count++;
	}
	return count;
}

void p2p_front_operators(const unsigned char (*keys)[P2P_FRONT_KEY_SIZE], int count)
{
	int index;

	if (count < 0)
		count = 0;
	if (count > P2P_FRONT_MAXIMUM_OPERATORS)
		count = P2P_FRONT_MAXIMUM_OPERATORS;
	if (count)
		memcpy(front.operators, keys, (size_t)count * P2P_FRONT_KEY_SIZE);
	front.operator_count = count;
	/* (a key taken off the list: its registration ends at once) */
	for (index = 0; index < P2P_FRONT_MAXIMUM_PORTS; index++)
	{
		struct registration *registration = &front.registrations[index];
		int known = 0, operator;

		if (!registration->used)
			continue;
		for (operator = 0; operator < count; operator++)
			known |= p2p_equal(front.operators[operator], registration->key, P2P_FRONT_KEY_SIZE);
		if (!known)
		{
			registration->used = 0;
			memset(registration->clients, 0, sizeof(registration->clients));
		}
	}
}

static int operator_allowed(const unsigned char *key)
{
	int index;

	for (index = 0; index < front.operator_count; index++)
	{
		if (p2p_equal(front.operators[index], key, P2P_FRONT_KEY_SIZE))
			return 1;
	}
	return 0;
}

/* ---------- the front */

void p2p_front_configure(const struct p2p_front_limits *limits)
{
	front.limits = *limits;
	if (front.limits.port_count > P2P_FRONT_MAXIMUM_PORTS)
		front.limits.port_count = P2P_FRONT_MAXIMUM_PORTS;
	if (front.limits.port_count < 0)
		front.limits.port_count = 0;
	if (!front.has_secret)
	{
		posix_random_bytes(front.secret, sizeof(front.secret));
		front.has_secret = 1;
	}
}

void p2p_front_counts(struct p2p_front_counts *counts)
{
	int index, client;

	*counts = front.counts;
	counts->registered = 0;
	counts->clients = 0;
	for (index = 0; index < front.limits.port_count; index++)
	{
		if (!front.registrations[index].used)
			continue;
		counts->registered++;
		for (client = 0; client < CLIENTS; client++)
			counts->clients += front.registrations[index].clients[client].used;
	}
}

/* the cookie for a REGISTER of key from address and port, in the period of
the time when */
static void make_cookie(unsigned long address, unsigned short port, const unsigned char *key, unsigned long when,
	unsigned char *cookie)
{
	unsigned char data[4 + 4 + 2 + P2P_FRONT_KEY_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];

	put_long(data, when / COOKIE_TIME);
	memcpy(data + 4, &address, 4);
	memcpy(data + 8, &port, 2);
	memcpy(data + 10, key, P2P_FRONT_KEY_SIZE);
	p2p_hmac_sha256(front.secret, P2P_SHA256_SIZE, data, (int)sizeof(data), digest);
	memcpy(cookie, digest, COOKIE_SIZE);
}

static int cookie_good(unsigned long address, unsigned short port, const unsigned char *key,
	const unsigned char *cookie, unsigned long now)
{
	unsigned char expected[COOKIE_SIZE];

	make_cookie(address, port, key, now, expected);
	if (p2p_equal(expected, cookie, COOKIE_SIZE))
		return 1;
	make_cookie(address, port, key, now - COOKIE_TIME, expected);
	return p2p_equal(expected, cookie, COOKIE_SIZE);
}

static void send_registered(unsigned long address, unsigned short port, int state, const unsigned char *nonce,
	struct registration const *registration)
{
	unsigned char packet[REGISTERED_SIZE];

	memset(packet, 0, sizeof(packet));
	packet[0] = P2P_FRONT_MAGIC;
	packet[1] = _type_registered;
	packet[2] = FRONT_VERSION;
	packet[3] = (unsigned char)state;
	memcpy(packet + 4, nonce, NONCE_SIZE);
	if (registration)
	{
		unsigned short public_port = (unsigned short)(front.limits.first_port +
			(registration - front.registrations));

		memcpy(packet + 4 + NONCE_SIZE, registration->session, SESSION_SIZE);
		packet[4 + NONCE_SIZE + SESSION_SIZE] = (unsigned char)(public_port >> 8);
		packet[4 + NONCE_SIZE + SESSION_SIZE + 1] = (unsigned char)public_port;
	}
	p2p_front_sendto(-1, address, port, packet, REGISTERED_SIZE);
}

static struct registration *registration_of_key(const unsigned char *key)
{
	int index;

	for (index = 0; index < front.limits.port_count; index++)
	{
		struct registration *registration = &front.registrations[index];

		if (registration->used && p2p_equal(registration->key, key, P2P_FRONT_KEY_SIZE))
			return registration;
	}
	return NULL;
}

/* a port for a key: the one it had, else one never given, else the one
whose registration ended longest ago */
static struct registration *free_registration(const unsigned char *key)
{
	struct registration *never = NULL, *oldest = NULL;
	int index;

	for (index = 0; index < front.limits.port_count; index++)
	{
		struct registration *registration = &front.registrations[index];

		if (registration->used)
			continue;
		if (registration->has_key && p2p_equal(registration->key, key, P2P_FRONT_KEY_SIZE))
			return registration;
		if (!registration->has_key)
		{
			if (!never)
				never = registration;
		}
		else if (!oldest || (long)(registration->ended_time - oldest->ended_time) < 0)
			oldest = registration;
	}
	return never ? never : oldest;
}

static void register_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	const unsigned char *key = packet + 4;
	const unsigned char *nonce = key + P2P_FRONT_KEY_SIZE;
	const unsigned char *cookie = nonce + NONCE_SIZE;
	const unsigned char *signature = cookie + COOKIE_SIZE;
	struct registration *registration;

	if (size != REGISTER_SIZE || packet[2] != FRONT_VERSION || packet[3] != 0 || !front.limits.port_count)
		return;
	/* (so many answered a second: each answer smaller than the REGISTER,
	so a flood sent in another's name reflects less than it sends) */
	if (!bucket_spend(&front.registers, REGISTERS_PER_SECOND, 1, now))
	{
		front.counts.refused++;
		return;
	}
	if (!cookie_good(address, port, key, cookie, now))
	{
		unsigned char challenge[CHALLENGE_SIZE];

		challenge[0] = P2P_FRONT_MAGIC;
		challenge[1] = _type_challenge;
		challenge[2] = FRONT_VERSION;
		challenge[3] = 0;
		memcpy(challenge + 4, nonce, NONCE_SIZE);
		make_cookie(address, port, key, now, challenge + 4 + NONCE_SIZE);
		front.counts.challenged++;
		p2p_front_sendto(-1, address, port, challenge, CHALLENGE_SIZE);
		return;
	}
	registration = registration_of_key(key);
	/* (the keep-alive: the REGISTER taken last, from the same address) */
	if (!registration || registration->address != address || registration->port != port ||
		memcmp(registration->accepted, packet, REGISTER_SIZE))
	{
		unsigned char message[64 + SIGNED_SIZE];
		int message_size;

		if (!operator_allowed(key))
		{
			front.counts.refused++;
			send_registered(address, port, _registered_refused, nonce, NULL);
			return;
		}
		if (!bucket_spend(&front.verifies, VERIFIES_PER_SECOND, 1, now))
		{
			front.counts.refused++;
			return;
		}
		message_size = signed_message(key, message);
		if (!p2p_ed25519_verify(key, message, message_size, signature))
		{
			front.counts.refused++;
			return;
		}
		if (!registration)
		{
			char text[2 * 4 + 1];

			registration = free_registration(key);
			if (!registration)
			{
				front.counts.refused++;
				send_registered(address, port, _registered_full, nonce, NULL);
				return;
			}
			memset(registration, 0, sizeof(*registration));
			registration->used = 1;
			registration->has_key = 1;
			memcpy(registration->key, key, P2P_FRONT_KEY_SIZE);
			posix_random_bytes(registration->session, SESSION_SIZE);
			front.counts.registrations++;
			p2p_hex(key, 4, text);
			p2p_log("Relay: front: registered a server (key %s...) on UDP port %u", text,
				(unsigned)(front.limits.first_port + (registration - front.registrations)));
		}
		/* (the same key from another address: its NAT's new mapping, since
		only the key's holder signs; its clients stay) */
		registration->address = address;
		registration->port = port;
		memcpy(registration->accepted, packet, REGISTER_SIZE);
	}
	registration->heard_time = now;
	send_registered(address, port, _registered_ok, nonce, registration);
}

static struct registration *registration_of_server(const unsigned char *packet, unsigned long address,
	unsigned short port)
{
	int index;

	for (index = 0; index < front.limits.port_count; index++)
	{
		struct registration *registration = &front.registrations[index];

		if (registration->used && registration->address == address && registration->port == port &&
			p2p_equal(registration->session, packet + 2, SESSION_SIZE))
		{
			return registration;
		}
	}
	return NULL;
}

/* a server's tunnel packet to one of its clients, sent from its public
port */
static void to_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	struct registration *registration;
	unsigned long id;
	int index;

	if (size < TO_HEADER_SIZE + P2P_TUNNEL_MINIMUM_PACKET || size > TO_HEADER_SIZE + P2P_TUNNEL_MAXIMUM_PACKET ||
		packet[TO_HEADER_SIZE] != P2P_TUNNEL_MAGIC)
	{
		return;
	}
	registration = registration_of_server(packet, address, port);
	if (!registration)
		return;
	id = get_long(packet + 2 + SESSION_SIZE);
	for (index = 0; index < CLIENTS; index++)
	{
		struct client *client = &registration->clients[index];

		if (!client->used || client->id != id)
			continue;
		if (!bucket_spend(&registration->bytes, REGISTRATION_UPLOAD, size, now) ||
			!bucket_spend(&front.bytes, front.limits.upload, size, now))
		{
			front.counts.dropped++;
			return;
		}
		client->answered = 1;
		registration->forwarded += (unsigned long)(size - TO_HEADER_SIZE);
		front.counts.forwarded_packets++;
		front.counts.forwarded_bytes += (unsigned long)(size - TO_HEADER_SIZE);
		p2p_front_sendto((int)(registration - front.registrations), client->address, client->port,
			packet + TO_HEADER_SIZE, size - TO_HEADER_SIZE);
		return;
	}
}

/* a client for a packet from address and port: the one it is, else a new
one (NULL if there is no room: those the server never answered give way,
oldest first) */
static struct client *client_for(struct registration *registration, unsigned long address, unsigned short port,
	unsigned long now)
{
	struct client *free_client = NULL, *unanswered = NULL;
	int from_address = 0;
	int index;

	for (index = 0; index < CLIENTS; index++)
	{
		struct client *client = &registration->clients[index];

		if (!client->used)
		{
			if (!free_client)
				free_client = client;
			continue;
		}
		if (client->address == address)
		{
			if (client->port == port)
				return client;
			from_address++;
		}
		if (!client->answered && (!unanswered || (long)(client->created_time - unanswered->created_time) < 0))
			unanswered = client;
	}
	if (from_address >= CLIENTS_PER_ADDRESS)
		return NULL;
	if (!free_client)
		free_client = unanswered;
	if (!free_client)
		return NULL;
	memset(free_client, 0, sizeof(*free_client));
	free_client->used = 1;
	free_client->address = address;
	free_client->port = port;
	free_client->created_time = now;
	/* (a number no other client of the registration has) */
	for (;;)
	{
		unsigned char bytes[CLIENT_ID_SIZE];
		int other;

		posix_random_bytes(bytes, sizeof(bytes));
		free_client->id = get_long(bytes);
		for (other = 0; other < CLIENTS; other++)
		{
			if (&registration->clients[other] != free_client && registration->clients[other].used &&
				registration->clients[other].id == free_client->id)
			{
				break;
			}
		}
		if (other == CLIENTS)
			break;
	}
	return free_client;
}

void p2p_front_public_received(int port_index, const unsigned char *packet, int size, unsigned long address,
	unsigned short port, unsigned long now)
{
	unsigned char wrapped[FROM_HEADER_SIZE + P2P_TUNNEL_MAXIMUM_PACKET];
	struct registration *registration;
	struct client *client;

	if (port_index < 0 || port_index >= front.limits.port_count)
		return;
	registration = &front.registrations[port_index];
	/* (tunnel packets alone, of a tunnel packet's sizes, to a registered
	server's port) */
	if (!registration->used || size < P2P_TUNNEL_MINIMUM_PACKET || size > P2P_TUNNEL_MAXIMUM_PACKET ||
		packet[0] != P2P_TUNNEL_MAGIC || !address || !port)
	{
		return;
	}
	client = client_for(registration, address, port, now);
	if (!client)
	{
		front.counts.dropped++;
		return;
	}
	if (!bucket_spend(&client->bytes, CLIENT_UPLOAD, size, now) ||
		!bucket_spend(&registration->bytes, REGISTRATION_UPLOAD, size, now) ||
		!bucket_spend(&front.bytes, front.limits.upload, size, now))
	{
		front.counts.dropped++;
		return;
	}
	client->heard_time = now;
	wrapped[0] = P2P_FRONT_MAGIC;
	wrapped[1] = _type_from;
	memcpy(wrapped + 2, registration->session, SESSION_SIZE);
	put_long(wrapped + 2 + SESSION_SIZE, client->id);
	memcpy(wrapped + 2 + SESSION_SIZE + CLIENT_ID_SIZE, &address, 4);
	memcpy(wrapped + 2 + SESSION_SIZE + CLIENT_ID_SIZE + 4, &port, 2);
	memcpy(wrapped + FROM_HEADER_SIZE, packet, (size_t)size);
	registration->forwarded += (unsigned long)size;
	front.counts.forwarded_packets++;
	front.counts.forwarded_bytes += (unsigned long)size;
	p2p_front_sendto(-1, registration->address, registration->port, wrapped, FROM_HEADER_SIZE + size);
}

/* ---------- the server's registration */

void p2p_front_use(const struct p2p_candidate *candidate)
{
	struct p2p_candidate none;

	if (!candidate)
	{
		memset(&none, 0, sizeof(none));
		candidate = &none;
	}
	if (front.front.address == candidate->address && front.front.port == candidate->port)
		return;
	if (front.state == _session_registered)
		p2p_front_lost();
	front.front = *candidate;
	front.state = candidate->address && candidate->port ? _session_registering : _session_none;
	memset(front.cookie, 0, sizeof(front.cookie));
	front.signed_cookie = 0;
	front.sent = 0;
	front.heard_time = 0;
	front.public_port = 0;
	posix_random_bytes(front.nonce, NONCE_SIZE);
}

int p2p_front_public(struct p2p_candidate *candidate)
{
	if (front.state != _session_registered)
		return 0;
	candidate->address = front.front.address;
	candidate->port = front.public_port;
	return 1;
}

static void send_register(unsigned long now)
{
	front.sent = 1;
	front.sent_time = now;
	/* (signed again for each new cookie: the keep-alive repeats it) */
	if (!front.signed_cookie)
	{
		unsigned char message[64 + SIGNED_SIZE];
		int message_size;

		front.packet[0] = P2P_FRONT_MAGIC;
		front.packet[1] = _type_register;
		front.packet[2] = FRONT_VERSION;
		front.packet[3] = 0;
		memcpy(front.packet + 4 + P2P_FRONT_KEY_SIZE, front.nonce, NONCE_SIZE);
		memcpy(front.packet + 4 + P2P_FRONT_KEY_SIZE + NONCE_SIZE, front.cookie, COOKIE_SIZE);
		/* (the key first: it is signed with the rest) */
		if (!p2p_front_sign(NULL, 0, front.packet + 4, NULL))
		{
			if (!front.unsigned_logged)
				p2p_log("Internet play: network.front is set, but there is no publisher key to register with "
					"(halo --keygen makes one; its public key goes in the front's server.front_operators_file)");
			front.unsigned_logged = 1;
			return;
		}
		message_size = signed_message(front.packet + 4, message);
		if (!p2p_front_sign(message, message_size, front.packet + 4,
			front.packet + 4 + P2P_FRONT_KEY_SIZE + NONCE_SIZE + COOKIE_SIZE))
		{
			return;
		}
		front.signed_cookie = 1;
	}
	p2p_front_sendto(-1, front.front.address, front.front.port, front.packet, REGISTER_SIZE);
}

static void challenge_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	if (size != CHALLENGE_SIZE || packet[2] != FRONT_VERSION || front.state == _session_none ||
		address != front.front.address || port != front.front.port || memcmp(packet + 4, front.nonce, NONCE_SIZE))
	{
		return;
	}
	/* (the same cookie again: the REGISTER that carried it is on its way,
	or was lost; the next interval sends it) */
	if (!memcmp(front.cookie, packet + 4 + NONCE_SIZE, COOKIE_SIZE) && front.signed_cookie)
		return;
	memcpy(front.cookie, packet + 4 + NONCE_SIZE, COOKIE_SIZE);
	front.signed_cookie = 0;
	send_register(now);
}

static void registered_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	char text[LOG_ADDRESS_SIZE];
	unsigned short public_port;
	int state;

	if (size != REGISTERED_SIZE || packet[2] != FRONT_VERSION || front.state == _session_none ||
		address != front.front.address || port != front.front.port || memcmp(packet + 4, front.nonce, NONCE_SIZE))
	{
		return;
	}
	state = packet[3];
	if (state == _registered_refused || state == _registered_full)
	{
		if (front.state != _session_refused)
		{
			p2p_log("Internet play: front %s refused this machine (%s)", log_address_ipv4(address, port, text,
				sizeof(text)), state == _registered_full ? "it has no port free" : "its server.front_operators_file "
				"does not list this machine's publisher key: halo --keygen prints it");
			if (front.state == _session_registered)
				p2p_front_lost();
		}
		front.state = _session_refused;
		front.heard_time = now;
		return;
	}
	if (state != _registered_ok)
		return;
	/* (in network order, as the packet has it) */
	memcpy(&public_port, packet + 4 + NONCE_SIZE + SESSION_SIZE, 2);
	if (!public_port)
		return;
	/* (a new session, or port: the old one's clients are gone) */
	if (front.state == _session_registered && (memcmp(front.session, packet + 4 + NONCE_SIZE, SESSION_SIZE) ||
		front.public_port != public_port))
	{
		p2p_front_lost();
	}
	if (front.state != _session_registered || front.public_port != public_port)
	{
		char offered[LOG_ADDRESS_SIZE];

		p2p_log("Internet play: front %s registered this machine: joiners are offered %s", log_address_ipv4(address,
			port, text, sizeof(text)), log_address_ipv4(address, public_port, offered, sizeof(offered)));
	}
	memcpy(front.session, packet + 4 + NONCE_SIZE, SESSION_SIZE);
	front.public_port = public_port;
	front.state = _session_registered;
	front.heard_time = now;
}

static void from_received(const unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	struct p2p_candidate client;

	(void)now;
	if (size < FROM_HEADER_SIZE + P2P_TUNNEL_MINIMUM_PACKET || size > FROM_HEADER_SIZE + P2P_TUNNEL_MAXIMUM_PACKET ||
		packet[FROM_HEADER_SIZE] != P2P_TUNNEL_MAGIC || front.state != _session_registered ||
		address != front.front.address || port != front.front.port ||
		!p2p_equal(packet + 2, front.session, SESSION_SIZE))
	{
		return;
	}
	memcpy(&client.address, packet + 2 + SESSION_SIZE + CLIENT_ID_SIZE, 4);
	memcpy(&client.port, packet + 2 + SESSION_SIZE + CLIENT_ID_SIZE + 4, 2);
	if (!client.address || !client.port)
		return;
	front.heard_time = now;
	p2p_front_delivered(get_long(packet + 2 + SESSION_SIZE), &client, packet + FROM_HEADER_SIZE,
		size - FROM_HEADER_SIZE);
}

int p2p_front_send(unsigned long client, const unsigned char *packet, int size)
{
	unsigned char wrapped[TO_HEADER_SIZE + P2P_TUNNEL_MAXIMUM_PACKET];

	if (front.state != _session_registered || size < P2P_TUNNEL_MINIMUM_PACKET || size > P2P_TUNNEL_MAXIMUM_PACKET)
		return 0;
	wrapped[0] = P2P_FRONT_MAGIC;
	wrapped[1] = _type_to;
	memcpy(wrapped + 2, front.session, SESSION_SIZE);
	put_long(wrapped + 2 + SESSION_SIZE, client);
	memcpy(wrapped + TO_HEADER_SIZE, packet, (size_t)size);
	p2p_front_sendto(-1, front.front.address, front.front.port, wrapped, TO_HEADER_SIZE + size);
	return 1;
}

/* ---------- both */

int p2p_front_received(unsigned char *packet, int size, unsigned long address, unsigned short port,
	unsigned long now)
{
	if (size < 2 || packet[0] != P2P_FRONT_MAGIC)
		return 0;
	switch (packet[1])
	{
	case _type_register:
		register_received(packet, size, address, port, now);
		break;
	case _type_to:
		if (front.limits.port_count)
			to_received(packet, size, address, port, now);
		break;
	case _type_challenge:
		challenge_received(packet, size, address, port, now);
		break;
	case _type_registered:
		registered_received(packet, size, address, port, now);
		break;
	case _type_from:
		from_received(packet, size, address, port, now);
		break;
	}
	return 1;
}

void p2p_front_update(unsigned long now)
{
	int index;

	for (index = 0; index < front.limits.port_count; index++)
	{
		struct registration *registration = &front.registrations[index];
		int client;

		if (!registration->used)
			continue;
		if (front_elapsed(now, registration->heard_time, REGISTRATION_SILENCE))
		{
			p2p_log("Relay: front: a server's registration on UDP port %u ended (not renewed; %lu bytes forwarded)",
				(unsigned)(front.limits.first_port + index), registration->forwarded);
			registration->used = 0;
			registration->ended_time = now;
			memset(registration->clients, 0, sizeof(registration->clients));
			front.counts.ended++;
			continue;
		}
		for (client = 0; client < CLIENTS; client++)
		{
			struct client *each = &registration->clients[client];

			if (each->used && (front_elapsed(now, each->heard_time, CLIENT_SILENCE) ||
				(!each->answered && front_elapsed(now, each->created_time, UNANSWERED_TIME))))
			{
				each->used = 0;
			}
		}
	}
	/* (the server's: its registration kept, renewed, and given up when its
	front falls silent) */
	if (front.state == _session_none)
		return;
	if (front.state == _session_registered && front_elapsed(now, front.heard_time, REGISTERED_SILENCE))
	{
		char text[LOG_ADDRESS_SIZE];

		p2p_log("Internet play: front %s not heard from: its address is no longer offered",
			log_address_ipv4(front.front.address, front.front.port, text, sizeof(text)));
		p2p_front_lost();
		front.state = _session_registering;
		memset(front.cookie, 0, sizeof(front.cookie));
		front.signed_cookie = 0;
		posix_random_bytes(front.nonce, NONCE_SIZE);
	}
	if (!front.sent || front_elapsed(now, front.sent_time, front.state == _session_registered ?
		KEEP_ALIVE_INTERVAL : front.state == _session_refused ? REFUSED_INTERVAL : REGISTER_INTERVAL))
	{
		send_register(now);
	}
}
