/*
P2P_FRIENDS.C

Friends (p2p.h): lasting friends of a player profile, with friend codes,
online status and recent players. Like a party (p2p_party.c) they live on
the signalling brokers alone (p2p_signal.c), which learn nothing of them:
every topic is a hash, every message sealed.

- Identity: each player profile has a key of its own, kept in friends.txt in
  the profile's folder (port/linux/game/friends_menu.inc says which): an
  Ed25519 seed, made the first time. Its public key is who the profile is.
- Friend code: the first 55 bits of SHA-256("openh1 friend code" | key), as
  11 characters of Crockford's base32 (0-9 and the letters but I, L, O, U),
  then a check character, in groups of four: "K7QM-2XPA-9DTR". The check is
  Damm's algorithm over the 32 characters (the quasigroup x a + b of
  GF(32)), which catches every mistyped character and every swap of two
  neighbours. Typing is forgiving: lower case, I and L for 1, O for 0, and
  the dashes and spaces anywhere. Finding a key of another's code takes
  some 2^55 tries.
- Inbox: a code's topic, hceu/3/f/<HMAC(code, "inbox")>, which the profile
  listens on. Anyone with a code can write to it, so what comes there is
  checked and counted (MAXIMUM_CHECKS) before it is believed.
- A request ('R', then the message sealed with HMAC(code, "inbox seal")): ADD,
  "let us be friends", with the sender's key and name. It is shown as a
  request; accepting it sends an ADD back. An ADD from the machine whose code
  this profile asked for (a request sent) makes them friends: asking each
  other is accepting. A request sent is sent again at each start and every
  ADD_INTERVAL until answered or cancelled (the other may be offline).
- A friend's message ('F', the 8-byte tag of the pair and sender, then the
  message sealed with the pair's key): each pair's key comes from their
  X25519 secret (the Ed25519 keys' X25519 forms) and both keys. HELLO gives
  the friend this profile's presence key and its generation, and says which
  generation of the friend's it holds; one that holds an old one is sent
  another HELLO. INVITE gives the party's link (a toast, accepted with the
  party's accept key). REMOVE ends the friendship on both sides.
- Presence: each profile's presence topic, hceu/3/f/<HMAC(presence key,
  "presence")>, which only its friends can work out, carries PRESENCE sealed
  with the presence key: where it is (the menus, a party and its size,
  hosting or playing, and what: the game's words as Discord has them), its
  name, and the link that joins it, which is its party's, else its spare party
  (p2p_party_spare: the one Discord's Ask to Join makes; a friend's JOIN
  makes the party, this profile its leader, and the party then follows it
  into its games as any party does). Published when it changes (at most each
  PRESENCE_CHANGE_INTERVAL), every PRESENCE_INTERVAL, and once when a friend
  comes online; a friend not heard for PRESENCE_TIMEOUT is offline. With
  friends.share_status off nothing is published (a last OFFLINE as it is
  turned off), and no HELLO is sent. Removing or blocking a friend makes a
  new presence key (a new generation), which the others get by HELLO: the
  friend removed can no longer read or find the presence.
- Every message (network byte order): "F", the format (FRIENDS_VERSION: a
  machine takes its own alone, and builds without friends never listen),
  its type, the sender's key (32), its time (8: milliseconds since 1970, made
  to increase: a sender's are taken in order, once each), the type's own,
  then an Ed25519 signature over a context (the recipient's code's, or the
  presence key's) and the message.
- Recent players: the OpenH1 players of the games played (the game tells
  their codes: port/linux/game/network_party.c), the last P2P_FRIENDS_RECENT,
  with when and in what game; an OpenCE player or one of no code (an older
  build, a Legacy game) is kept, shown as one who cannot be added.
- Blocking: a blocked code's requests, ADDs and invites are let go.
- friends.txt is the profile's alone and untrusted: read up to
  FILE_MAXIMUM bytes, its lines checked, its text cut to printable ASCII; it
  is written whole, to friends.txt.new first.

Everything runs under p2p_lock: the p2p thread's passes, and the game's
calls (their functions take it).
*/

#include "posix.h"
#include "p2p_internal.h"
#include "port_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	FRIENDS_MAGIC = 'F',
	/* the friends' message format: a machine takes its own alone */
	FRIENDS_VERSION = 1,
	/* what a payload on an inbox starts with: a request, a friend's */
	_outer_request = 'R',
	_outer_friend = 'F',
	_message_add = 'A',
	_message_hello = 'H',
	_message_invite = 'I',
	_message_remove = 'X',
	_message_presence = 'P',

	TAG_SIZE = 8,
	/* "F", the format, the type, the key and the time */
	HEADER_SIZE = 3 + P2P_KEY_SIZE + 8,
	DESCRIPTION_SIZE = 63,
	/* the most a message is (p2p_signal.c takes 512): a PRESENCE */
	MAXIMUM_MESSAGE_SIZE = HEADER_SIZE + 5 + P2P_TOKEN_SIZE + 1 + P2P_FRIEND_NAME_SIZE + 1 + DESCRIPTION_SIZE +
		P2P_SIGNATURE_SIZE,
	MAXIMUM_PAYLOAD_SIZE = 1 + TAG_SIZE + MAXIMUM_MESSAGE_SIZE + P2P_SEAL_OVERHEAD,
	/* codes ignored (a request turned down), kept against their sending it
	again */
	MAXIMUM_IGNORED = 32,
	/* a friend's invites waiting for an answer */
	MAXIMUM_INVITES = 4,
	MAXIMUM_NOTICES = 8,
	FILE_MAXIMUM = 64 * 1024,

	/* a PRESENCE's flags */
	_presence_first = 1,
	_presence_offline = 2,
	_presence_party = 4,
	_presence_hosting = 8,
	_presence_playing = 16,
	_presence_joinable = 32,

	/* milliseconds */
	PRESENCE_INTERVAL = 60000,
	PRESENCE_CHANGE_INTERVAL = 3000,
	/* a friend's FIRST answered this soon (others answer too) */
	PRESENCE_ANSWER_DELAY = 1000,
	PRESENCE_TIMEOUT = 150000,
	/* a request sent is sent again this often, while it runs */
	ADD_INTERVAL = 300000,
	/* HELLOs: one each HELLO_SPACING at most; a friend that has not
	confirmed this profile's key asked again this often; a friend's HELLO
	answered at most this often */
	HELLO_SPACING = 1000,
	HELLO_RETRY = 300000,
	HELLO_ANSWER_INTERVAL = 10000,
	/* an ADD to a friend that asks again, and a friend's invites, at most
	this often */
	REPLY_INTERVAL = 30000,
	INVITE_INTERVAL = 10000,
	INVITE_TIMEOUT = 60000,
	/* the file written this long after a change at most (a recent player
	seen: RECENT_SAVE_INTERVAL) */
	SAVE_DELAY = 2000,
	RECENT_SAVE_INTERVAL = 60000,
	/* a recent player seen again is moved up at most this often (seconds) */
	RECENT_SEEN_SECONDS = 60,
	/* signatures checked (anyone with the code can write to an inbox): this
	many at once, one more each CHECK_INTERVAL */
	MAXIMUM_CHECKS = 24,
	CHECK_INTERVAL = 250,
};

typedef char friends_message_size_assert[MAXIMUM_PAYLOAD_SIZE <= 512 + P2P_SEAL_OVERHEAD ? 1 : -1];
typedef char friends_topics_assert[P2P_FRIENDS_TOPICS == 1 + P2P_FRIENDS_MAXIMUM ? 1 : -1];

/* what a friend said of itself last (its PRESENCE) */
struct presence
{
	unsigned char flags;
	unsigned char place;
	unsigned char party_count;
	unsigned char players, maximum_players;
	unsigned char token[P2P_TOKEN_SIZE];
	char name[P2P_FRIEND_NAME_SIZE];
	char description[DESCRIPTION_SIZE + 1];
};

struct friend_record
{
	int used;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
	long long added;
	/* its presence key and generation, as its HELLO told; and the
	generation of this profile's it holds, as it confirmed */
	int has_presence_key;
	unsigned long generation;
	unsigned char presence_key[P2P_KEY_SIZE];
	unsigned long confirmed;
	/* (this run's, not kept) the pair's key and tags (worked out once: an
	X25519 each) */
	int paired;
	unsigned char pair_seal[P2P_SHA256_SIZE];
	unsigned char tag_in[TAG_SIZE], tag_out[TAG_SIZE];
	/* its presence topic, key and context, of its presence key */
	char presence_topic[P2P_TOPIC_SIZE];
	unsigned char presence_seal[P2P_SHA256_SIZE];
	unsigned char presence_context[P2P_SHA256_SIZE];
	/* the last of its messages taken (its time), and its presence: heard
	when, as it said */
	unsigned long long last_time;
	unsigned long heard_time;
	int heard;
	struct presence presence;
	/* HELLOs sent to it and answered, ADDs answered, invites taken */
	unsigned long hello_time;
	int hello_sent;
	unsigned long answered_time;
	int answered;
	unsigned long reply_time;
	int replied;
	unsigned long invite_time;
	int invited;
	/* its invite this profile has not answered (the menus' JOIN takes it) */
	int inviting;
};

struct request_record
{
	int used;
	unsigned char key[P2P_KEY_SIZE];
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
	long long time;
	unsigned long long last_time;
};

struct sent_record
{
	int used;
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
	long long time;
	/* (this run's) when it was last sent, and whether it was */
	unsigned long sent_time;
	int sent;
};

struct blocked_record
{
	int used;
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
};

struct recent_record
{
	int used;
	int has_code;
	int opence;
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
	char game[P2P_FRIEND_GAME_SIZE];
	long long time;
};

struct invite_record
{
	unsigned long serial;
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	char name[P2P_FRIEND_NAME_SIZE];
	unsigned char token[P2P_TOKEN_SIZE];
	unsigned long time;
};

static struct
{
	/* the profile's folder (empty: none), and its name */
	char folder[1024];
	char name[P2P_FRIEND_NAME_SIZE];
	int has_identity;
	unsigned char seed[P2P_SEED_SIZE];
	unsigned char key[P2P_KEY_SIZE];
	unsigned char x25519_secret[P2P_KEY_SIZE];
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	/* the inbox's topic, key and context */
	char inbox_topic[P2P_TOPIC_SIZE];
	unsigned char inbox_seal[P2P_SHA256_SIZE];
	unsigned char inbox_context[P2P_SHA256_SIZE];
	/* this profile's presence key, its generation, topic, key, context */
	unsigned long generation;
	unsigned char presence_key[P2P_KEY_SIZE];
	char presence_topic[P2P_TOPIC_SIZE];
	unsigned char presence_seal[P2P_SHA256_SIZE];
	unsigned char presence_context[P2P_SHA256_SIZE];

	struct friend_record friends[P2P_FRIENDS_MAXIMUM];
	struct request_record requests[P2P_FRIENDS_REQUESTS];
	struct sent_record sent[P2P_FRIENDS_SENT];
	struct blocked_record blocked[P2P_FRIENDS_BLOCKED];
	struct recent_record recent[P2P_FRIENDS_RECENT];
	unsigned char ignored[MAXIMUM_IGNORED][P2P_FRIEND_CODE_BYTES];
	int ignored_next;

	/* the file: to write (when), a recent player's change only */
	int dirty;
	int dirty_recent;
	unsigned long dirty_time;
	unsigned long saved_time;
	/* the topics told p2p_signal.c */
	int topics_wanted;

	/* the last message's time sent */
	unsigned long long sent_time;
	/* publishing: the presence shared (friends.share_status, and online),
	its last PRESENCE and when, one wanted (when), and its first sent */
	int sharing;
	int share_setting;
	unsigned long setting_time;
	struct presence published;
	unsigned long published_time;
	int presence_wanted;
	unsigned long presence_wanted_time;
	int first_sent;
	/* HELLOs: the last sent, when */
	unsigned long hello_time;
	/* a broker was connected at the last pass */
	int was_connected;

	int checks_left;
	unsigned long checks_time;

	struct invite_record invites[MAXIMUM_INVITES];
	unsigned long invite_serial;
	struct p2p_friends_notice notices[MAXIMUM_NOTICES];
	int notice_count;
	unsigned long notice_serial;
	/* the game this machine hosts is its invite's alone (a co-op game's
	INVITE ONLY: p2p_friends_set_game_closed): its presence offers no JOIN
	while it hosts */
	int game_closed;
} friends;

/* the messages sent now are the last before the game quits */
static int friends_urgent;

static const char code_digits[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static int elapsed(unsigned long since, unsigned long time)
{
	return (unsigned int)(p2p_now() - since) >= (unsigned int)time;
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

/* a name or a game's words as kept: printable ASCII alone (UTF-8 too:
utf8), at most size - 1 bytes, a character not cut in two */
static void clean_text(char *destination, int size, const char *text, int length, int utf8)
{
	int read, written = 0;

	for (read = 0; read < length && text[read] && written < size - 1; read++)
	{
		unsigned char character = (unsigned char)text[read];

		if (character >= 0x20 && character < 0x7F && character != '\t')
			destination[written++] = (char)character;
		else if (utf8 && character >= 0xC2 && character <= 0xF4)
		{
			int more = character >= 0xF0 ? 3 : character >= 0xE0 ? 2 : 1;
			int index;

			if (written + 1 + more > size - 1 || read + more >= length)
				break;
			for (index = 1; index <= more && ((unsigned char)text[read + index] & 0xC0) == 0x80; index++)
				;
			if (index <= more)
				continue;
			memcpy(destination + written, text + read, (size_t)(1 + more));
			written += 1 + more;
			read += more;
		}
	}
	destination[written] = 0;
}

static void notice(int kind, const char *name)
{
	struct p2p_friends_notice *entry;

	if (friends.notice_count == MAXIMUM_NOTICES)
	{
		memmove(friends.notices, friends.notices + 1, sizeof(friends.notices[0]) * (MAXIMUM_NOTICES - 1));
		friends.notice_count--;
	}
	entry = &friends.notices[friends.notice_count++];
	memset(entry, 0, sizeof(*entry));
	entry->serial = ++friends.notice_serial;
	entry->kind = kind;
	clean_text(entry->name, sizeof(entry->name), name ? name : "", P2P_FRIEND_NAME_SIZE, 0);
}

static void changed(int recent_only)
{
	if (!friends.dirty && !friends.dirty_recent)
		friends.dirty_time = p2p_now();
	if (recent_only)
		friends.dirty_recent = 1;
	else
		friends.dirty = 1;
}

/* ---------- codes */

void p2p_friend_code_of(const unsigned char *key, unsigned char *code)
{
	unsigned char data[18 + P2P_KEY_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];

	memcpy(data, "openh1 friend code", 18);
	memcpy(data + 18, key, P2P_KEY_SIZE);
	p2p_sha256(data, sizeof(data), digest);
	memcpy(code, digest, P2P_FRIEND_CODE_BYTES);
	/* (55 bits: the last byte's lowest bit is none) */
	code[P2P_FRIEND_CODE_BYTES - 1] &= 0xFE;
}

/* a code's 11 characters' values (5 bits each, the first byte's high bits
first) */
static void code_values(const unsigned char *code, int *values)
{
	int index;

	for (index = 0; index < 11; index++)
	{
		int bit = index * 5;
		int pair = code[bit / 8] << 8 | (bit / 8 + 1 < P2P_FRIEND_CODE_BYTES ? code[bit / 8 + 1] : 0);

		values[index] = pair >> (11 - bit % 8) & 31;
	}
}

/* a value times x in GF(32) (modulo x^5 + x^2 + 1) */
static int code_double(int value)
{
	value <<= 1;
	return value & 32 ? value ^ 0x25 : value;
}

/* Damm's check of the 11 values: the quasigroup a * b = x a + b of GF(32),
whose check makes the whole code's reduce to 0 */
static int code_check(const int *values)
{
	int interim = 0;
	int index;

	for (index = 0; index < 11; index++)
		interim = code_double(interim) ^ values[index];
	return code_double(interim);
}

int p2p_friend_code_format(const unsigned char *code, char *text, int size)
{
	int values[11];
	char characters[12];
	int index;

	if (size < P2P_FRIEND_CODE_TEXT_SIZE)
	{
		if (size > 0)
			text[0] = 0;
		return 0;
	}
	code_values(code, values);
	for (index = 0; index < 11; index++)
		characters[index] = code_digits[values[index]];
	characters[11] = code_digits[code_check(values)];
	snprintf(text, (size_t)size, "%.4s-%.4s-%.4s", characters, characters + 4, characters + 8);
	return 1;
}

/* a character's value, -1 if it is none of a code's (the forgiving ones:
lower case, I and L for 1, O for 0) */
static int code_value(char character)
{
	const char *found;

	if (character >= 'a' && character <= 'z')
		character = (char)(character - 'a' + 'A');
	if (character == 'I' || character == 'L')
		character = '1';
	else if (character == 'O')
		character = '0';
	found = character ? strchr(code_digits, character) : NULL;
	return found ? (int)(found - code_digits) : -1;
}

int p2p_friend_code_parse(const char *text, unsigned char *code)
{
	int values[12];
	int count = 0;
	int index;

	for (; *text && count <= 12; text++)
	{
		int value;

		if (*text == '-' || *text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
			continue;
		value = code_value(*text);
		if (value < 0)
			return 0;
		if (count < 12)
			values[count] = value;
		count++;
	}
	if (count != 12)
		return 0;
	if (code_check(values) != values[11])
		return -1;
	memset(code, 0, P2P_FRIEND_CODE_BYTES);
	for (index = 0; index < 11; index++)
	{
		int bit = index * 5;
		int shifted = values[index] << (11 - bit % 8);

		code[bit / 8] |= (unsigned char)(shifted >> 8);
		if (bit / 8 + 1 < P2P_FRIEND_CODE_BYTES)
			code[bit / 8 + 1] |= (unsigned char)shifted;
	}
	return 1;
}

/* ---------- the keys */

static void topic_of(const unsigned char *key, int key_size, const char *label, char *topic)
{
	unsigned char digest[P2P_SHA256_SIZE];
	char hex[33];

	p2p_hmac_sha256(key, key_size, label, (int)strlen(label), digest);
	p2p_hex(digest, 16, hex);
	snprintf(topic, P2P_TOPIC_SIZE, "hceu/3/f/%s", hex);
}

/* a code's inbox: its topic, key and context */
static void inbox_of(const unsigned char *code, char *topic, unsigned char *seal, unsigned char *context)
{
	topic_of(code, P2P_FRIEND_CODE_BYTES, "inbox", topic);
	p2p_hmac_sha256(code, P2P_FRIEND_CODE_BYTES, "inbox seal", 10, seal);
	p2p_hmac_sha256(code, P2P_FRIEND_CODE_BYTES, "inbox id", 8, context);
}

static void presence_of(const unsigned char *key, char *topic, unsigned char *seal, unsigned char *context)
{
	topic_of(key, P2P_KEY_SIZE, "presence", topic);
	p2p_hmac_sha256(key, P2P_KEY_SIZE, "presence seal", 13, seal);
	p2p_hmac_sha256(key, P2P_KEY_SIZE, "presence id", 11, context);
}

/* the profile's key's derivations, of its seed */
static void identity_derive(void)
{
	p2p_ed25519_public(friends.seed, friends.key, friends.x25519_secret);
	p2p_friend_code_of(friends.key, friends.code);
	inbox_of(friends.code, friends.inbox_topic, friends.inbox_seal, friends.inbox_context);
	friends.has_identity = 1;
}

static void presence_derive(void)
{
	presence_of(friends.presence_key, friends.presence_topic, friends.presence_seal, friends.presence_context);
}

/* a friend's pair key and tags: 0 if its key is unusable (of small order) */
static int pair(struct friend_record *record)
{
	unsigned char x25519[P2P_KEY_SIZE], shared[P2P_KEY_SIZE];
	unsigned char data[18 + 3 * P2P_KEY_SIZE];
	unsigned char secret[P2P_SHA256_SIZE];
	static const unsigned char zero[P2P_KEY_SIZE];
	int first;

	if (record->paired)
		return 1;
	if (!p2p_ed25519_to_x25519(record->key, x25519))
		return 0;
	p2p_x25519(shared, friends.x25519_secret, x25519);
	if (p2p_equal(shared, zero, P2P_KEY_SIZE))
		return 0;
	/* (both keys, the lower first: the same on both sides) */
	first = memcmp(friends.key, record->key, P2P_KEY_SIZE) < 0;
	memcpy(data, "openh1 friend pair", 18);
	memcpy(data + 18, shared, P2P_KEY_SIZE);
	memcpy(data + 18 + P2P_KEY_SIZE, first ? friends.key : record->key, P2P_KEY_SIZE);
	memcpy(data + 18 + 2 * P2P_KEY_SIZE, first ? record->key : friends.key, P2P_KEY_SIZE);
	p2p_sha256(data, sizeof(data), secret);
	p2p_hmac_sha256(secret, P2P_SHA256_SIZE, "seal", 4, record->pair_seal);
	{
		unsigned char tag[3 + P2P_KEY_SIZE];
		unsigned char digest[P2P_SHA256_SIZE];

		memcpy(tag, "tag", 3);
		memcpy(tag + 3, friends.key, P2P_KEY_SIZE);
		p2p_hmac_sha256(secret, P2P_SHA256_SIZE, tag, sizeof(tag), digest);
		memcpy(record->tag_out, digest, TAG_SIZE);
		memcpy(tag + 3, record->key, P2P_KEY_SIZE);
		p2p_hmac_sha256(secret, P2P_SHA256_SIZE, tag, sizeof(tag), digest);
		memcpy(record->tag_in, digest, TAG_SIZE);
	}
	record->paired = 1;
	return 1;
}

static void friend_presence_derive(struct friend_record *record)
{
	if (record->has_presence_key)
		presence_of(record->presence_key, record->presence_topic, record->presence_seal, record->presence_context);
	else
		record->presence_topic[0] = 0;
}

/* ---------- the records */

static struct friend_record *find_friend(const unsigned char *code)
{
	int index;

	for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
	{
		if (friends.friends[index].used && !memcmp(friends.friends[index].code, code, P2P_FRIEND_CODE_BYTES))
			return &friends.friends[index];
	}
	return NULL;
}

static struct request_record *find_request(const unsigned char *code)
{
	int index;

	for (index = 0; index < P2P_FRIENDS_REQUESTS; index++)
	{
		if (friends.requests[index].used && !memcmp(friends.requests[index].code, code, P2P_FRIEND_CODE_BYTES))
			return &friends.requests[index];
	}
	return NULL;
}

static struct sent_record *find_sent(const unsigned char *code)
{
	int index;

	for (index = 0; index < P2P_FRIENDS_SENT; index++)
	{
		if (friends.sent[index].used && !memcmp(friends.sent[index].code, code, P2P_FRIEND_CODE_BYTES))
			return &friends.sent[index];
	}
	return NULL;
}

static struct blocked_record *find_blocked(const unsigned char *code)
{
	int index;

	for (index = 0; index < P2P_FRIENDS_BLOCKED; index++)
	{
		if (friends.blocked[index].used && !memcmp(friends.blocked[index].code, code, P2P_FRIEND_CODE_BYTES))
			return &friends.blocked[index];
	}
	return NULL;
}

static int ignored(const unsigned char *code)
{
	int index;

	for (index = 0; index < MAXIMUM_IGNORED; index++)
	{
		if (!memcmp(friends.ignored[index], code, P2P_FRIEND_CODE_BYTES))
			return 1;
	}
	return 0;
}

static void ignored_forget(const unsigned char *code)
{
	int index;

	for (index = 0; index < MAXIMUM_IGNORED; index++)
	{
		if (!memcmp(friends.ignored[index], code, P2P_FRIEND_CODE_BYTES))
			memset(friends.ignored[index], 0, P2P_FRIEND_CODE_BYTES);
	}
}

/* the friend whose pair tag (its messages' to this profile) this is */
static struct friend_record *friend_of_tag(const unsigned char *tag)
{
	int index;

	for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
	{
		struct friend_record *record = &friends.friends[index];

		if (record->used && pair(record) && !memcmp(record->tag_in, tag, TAG_SIZE))
			return record;
	}
	return NULL;
}

/* ---------- the file */

static void file_path(char *path, int size)
{
	snprintf(path, (size_t)size, "%s%sfriends.txt", friends.folder,
		friends.folder[0] && friends.folder[strlen(friends.folder) - 1] != '/' ? "/" : "");
}

static int hex_read(const char *text, int length, unsigned char *bytes, int size)
{
	int index;

	if (length != 2 * size)
		return 0;
	for (index = 0; index < 2 * size; index++)
	{
		char digit = text[index];
		int value = digit >= '0' && digit <= '9' ? digit - '0' : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
			-1;

		if (value < 0)
			return 0;
		if (index % 2)
			bytes[index / 2] = (unsigned char)(bytes[index / 2] << 4 | value);
		else
			bytes[index / 2] = (unsigned char)value;
	}
	return 1;
}

static int number_read(const char *text, int length, long long *value)
{
	int index;

	*value = 0;
	if (length <= 0 || length > 18)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (text[index] < '0' || text[index] > '9')
			return 0;
		*value = *value * 10 + (text[index] - '0');
	}
	return 1;
}

/* a line's fields, split at tabs (at most maximum): their starts and
lengths; returns their count */
static int fields_of(const char *line, int length, const char **starts, int *lengths, int maximum)
{
	int count = 0;
	int start = 0;
	int index;

	for (index = 0; index <= length && count < maximum; index++)
	{
		if (index == length || line[index] == '\t')
		{
			starts[count] = line + start;
			lengths[count] = index - start;
			count++;
			start = index + 1;
		}
	}
	return count;
}

static int code_read(const char *text, int length, unsigned char *code)
{
	char copy[24];

	if (length <= 0 || length >= (int)sizeof(copy))
		return 0;
	memcpy(copy, text, (size_t)length);
	copy[length] = 0;
	return p2p_friend_code_parse(copy, code) > 0;
}

/* a line of the file, taken if it is valid */
static void line_read(const char *line, int length)
{
	const char *starts[8];
	int lengths[8];
	int count = fields_of(line, length, starts, lengths, 8);
	long long number;
	int index;

#define FIELD_IS(word) (lengths[0] == (int)sizeof(word) - 1 && !memcmp(starts[0], word, sizeof(word) - 1))
	if (FIELD_IS("identity") && count >= 2)
	{
		if (hex_read(starts[1], lengths[1], friends.seed, P2P_SEED_SIZE))
			identity_derive();
	}
	else if (FIELD_IS("presence") && count >= 3)
	{
		if (number_read(starts[1], lengths[1], &number) && number > 0 && number < 0xFFFFFFFFLL &&
			hex_read(starts[2], lengths[2], friends.presence_key, P2P_KEY_SIZE))
		{
			friends.generation = (unsigned long)number;
			presence_derive();
		}
	}
	else if (FIELD_IS("friend") && count >= 7)
	{
		struct friend_record record;
		long long confirmed;

		memset(&record, 0, sizeof(record));
		if (!hex_read(starts[1], lengths[1], record.key, P2P_KEY_SIZE) ||
			!number_read(starts[2], lengths[2], &record.added) || !number_read(starts[3], lengths[3], &number) ||
			!number_read(starts[5], lengths[5], &confirmed) || number >= 0xFFFFFFFFLL || confirmed >= 0xFFFFFFFFLL)
		{
			return;
		}
		record.generation = (unsigned long)number;
		record.confirmed = (unsigned long)confirmed;
		record.has_presence_key = lengths[4] != 1 && hex_read(starts[4], lengths[4], record.presence_key, P2P_KEY_SIZE);
		clean_text(record.name, sizeof(record.name), starts[6], lengths[6], 0);
		p2p_friend_code_of(record.key, record.code);
		if (find_friend(record.code) || (friends.has_identity && !memcmp(record.key, friends.key, P2P_KEY_SIZE)))
			return;
		for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
		{
			if (!friends.friends[index].used)
			{
				record.used = 1;
				friend_presence_derive(&record);
				friends.friends[index] = record;
				return;
			}
		}
	}
	else if (FIELD_IS("request") && count >= 4)
	{
		struct request_record record;

		memset(&record, 0, sizeof(record));
		if (!hex_read(starts[1], lengths[1], record.key, P2P_KEY_SIZE) || !number_read(starts[2], lengths[2], &record.time))
			return;
		p2p_friend_code_of(record.key, record.code);
		clean_text(record.name, sizeof(record.name), starts[3], lengths[3], 0);
		for (index = 0; index < P2P_FRIENDS_REQUESTS && !find_request(record.code); index++)
		{
			if (!friends.requests[index].used)
			{
				record.used = 1;
				friends.requests[index] = record;
			}
		}
	}
	else if (FIELD_IS("sent") && count >= 4)
	{
		struct sent_record record;

		memset(&record, 0, sizeof(record));
		if (!code_read(starts[1], lengths[1], record.code) || !number_read(starts[2], lengths[2], &record.time))
			return;
		clean_text(record.name, sizeof(record.name), starts[3], lengths[3], 0);
		for (index = 0; index < P2P_FRIENDS_SENT && !find_sent(record.code); index++)
		{
			if (!friends.sent[index].used)
			{
				record.used = 1;
				friends.sent[index] = record;
			}
		}
	}
	else if (FIELD_IS("blocked") && count >= 3)
	{
		struct blocked_record record;

		memset(&record, 0, sizeof(record));
		if (!code_read(starts[1], lengths[1], record.code))
			return;
		clean_text(record.name, sizeof(record.name), starts[2], lengths[2], 0);
		for (index = 0; index < P2P_FRIENDS_BLOCKED && !find_blocked(record.code); index++)
		{
			if (!friends.blocked[index].used)
			{
				record.used = 1;
				friends.blocked[index] = record;
			}
		}
	}
	else if (FIELD_IS("ignored") && count >= 2)
	{
		unsigned char code[P2P_FRIEND_CODE_BYTES];

		if (code_read(starts[1], lengths[1], code) && !ignored(code))
		{
			memcpy(friends.ignored[friends.ignored_next], code, P2P_FRIEND_CODE_BYTES);
			friends.ignored_next = (friends.ignored_next + 1) % MAXIMUM_IGNORED;
		}
	}
	else if (FIELD_IS("recent") && count >= 6)
	{
		struct recent_record record;

		memset(&record, 0, sizeof(record));
		record.has_code = code_read(starts[1], lengths[1], record.code);
		if ((!record.has_code && (lengths[1] != 1 || starts[1][0] != '-')) ||
			!number_read(starts[2], lengths[2], &record.time) || !number_read(starts[3], lengths[3], &number))
		{
			return;
		}
		record.opence = number == 1;
		clean_text(record.name, sizeof(record.name), starts[4], lengths[4], 0);
		clean_text(record.game, sizeof(record.game), starts[5], lengths[5], 1);
		for (index = 0; index < P2P_FRIENDS_RECENT; index++)
		{
			if (!friends.recent[index].used)
			{
				record.used = 1;
				friends.recent[index] = record;
				return;
			}
		}
	}
#undef FIELD_IS
}

int p2p_friends_parse(const char *text, int size)
{
	int start = 0;
	int index;

	for (index = 0; index <= size; index++)
	{
		if (index == size || text[index] == '\n')
		{
			int length = index - start;

			if (length > 0 && text[start + length - 1] == '\r')
				length--;
			if (length > 0 && text[start] != '#')
				line_read(text + start, length);
			start = index + 1;
		}
	}
	return friends.has_identity;
}

static int text_put(char *text, int size, int *at, const char *format, ...)
{
	va_list arguments;
	int written;

	va_start(arguments, format);
	written = *at < size ? vsnprintf(text + *at, (size_t)(size - *at), format, arguments) : -1;
	va_end(arguments);
	if (written < 0 || *at + written >= size)
		return 0;
	*at += written;
	return 1;
}

int p2p_friends_format(char *text, int size)
{
	char seed[2 * P2P_SEED_SIZE + 1], key[2 * P2P_KEY_SIZE + 1], presence[2 * P2P_KEY_SIZE + 1];
	char code[P2P_FRIEND_CODE_TEXT_SIZE];
	int at = 0;
	int ok;
	int index;

	p2p_hex(friends.seed, P2P_SEED_SIZE, seed);
	p2p_hex(friends.presence_key, P2P_KEY_SIZE, presence);
	p2p_friend_code_format(friends.code, code, sizeof(code));
	ok = text_put(text, size, &at, "# OpenH1's friends of this player profile. Keep it to yourself: its identity is the "
		"profile's\n# secret key, and anyone who has it can be this profile to your friends.\n# Your friend code: "
		"%s\nidentity\t%s\npresence\t%lu\t%s\n", code, seed, friends.generation, presence);
	for (index = 0; ok && index < P2P_FRIENDS_MAXIMUM; index++)
	{
		struct friend_record const *record = &friends.friends[index];
		char their[2 * P2P_KEY_SIZE + 1];

		if (!record->used)
			continue;
		p2p_hex(record->key, P2P_KEY_SIZE, key);
		if (record->has_presence_key)
			p2p_hex(record->presence_key, P2P_KEY_SIZE, their);
		else
			strcpy(their, "-");
		ok = text_put(text, size, &at, "friend\t%s\t%lld\t%lu\t%s\t%lu\t%s\n", key, record->added, record->generation,
			their, record->confirmed, record->name);
	}
	for (index = 0; ok && index < P2P_FRIENDS_REQUESTS; index++)
	{
		if (!friends.requests[index].used)
			continue;
		p2p_hex(friends.requests[index].key, P2P_KEY_SIZE, key);
		ok = text_put(text, size, &at, "request\t%s\t%lld\t%s\n", key, friends.requests[index].time,
			friends.requests[index].name);
	}
	for (index = 0; ok && index < P2P_FRIENDS_SENT; index++)
	{
		if (!friends.sent[index].used)
			continue;
		p2p_friend_code_format(friends.sent[index].code, code, sizeof(code));
		ok = text_put(text, size, &at, "sent\t%s\t%lld\t%s\n", code, friends.sent[index].time, friends.sent[index].name);
	}
	for (index = 0; ok && index < P2P_FRIENDS_BLOCKED; index++)
	{
		if (!friends.blocked[index].used)
			continue;
		p2p_friend_code_format(friends.blocked[index].code, code, sizeof(code));
		ok = text_put(text, size, &at, "blocked\t%s\t%s\n", code, friends.blocked[index].name);
	}
	for (index = 0; ok && index < MAXIMUM_IGNORED; index++)
	{
		static const unsigned char none[P2P_FRIEND_CODE_BYTES];

		if (!memcmp(friends.ignored[index], none, P2P_FRIEND_CODE_BYTES))
			continue;
		p2p_friend_code_format(friends.ignored[index], code, sizeof(code));
		ok = text_put(text, size, &at, "ignored\t%s\n", code);
	}
	for (index = 0; ok && index < P2P_FRIENDS_RECENT; index++)
	{
		struct recent_record const *record = &friends.recent[index];

		if (!record->used)
			continue;
		if (record->has_code)
			p2p_friend_code_format(record->code, code, sizeof(code));
		else
			strcpy(code, "-");
		ok = text_put(text, size, &at, "recent\t%s\t%lld\t%d\t%s\t%s\n", code, record->time, record->opence,
			record->name, record->game);
	}
	return ok ? at : 0;
}

/* (the guest's C library's files: every build's game is a guest) */
static int file_write(const char *path, const char *text, int size)
{
	char temporary[1100];
	FILE *file;
	int written;

	snprintf(temporary, sizeof(temporary), "%s.new", path);
	file = fopen(temporary, "wb");
	if (!file)
		return 0;
	written = fwrite(text, 1, (size_t)size, file) == (size_t)size;
	written = fclose(file) == 0 && written;
	if (written)
		written = rename(temporary, path) == 0;
	if (!written)
		remove(temporary);
	return written;
}

/* the file's text (at most FILE_MAXIMUM bytes), NULL if it cannot be read */
static char *file_read(const char *path, int *size)
{
	FILE *file = fopen(path, "rb");
	char *text;

	*size = 0;
	if (!file)
		return NULL;
	text = malloc(FILE_MAXIMUM + 1);
	if (text)
	{
		*size = (int)fread(text, 1, FILE_MAXIMUM, file);
		text[*size] = 0;
	}
	fclose(file);
	return text;
}

static void save(void)
{
	static char text[FILE_MAXIMUM];
	char path[1100];
	int size;

	friends.dirty = friends.dirty_recent = 0;
	friends.saved_time = p2p_now();
	if (!friends.has_identity || !friends.folder[0])
		return;
	size = p2p_friends_format(text, sizeof(text));
	file_path(path, sizeof(path));
	if (!size || !file_write(path, text, size))
		p2p_log("Friends: could not write %s", path);
}

/* the profile's friends read (none: a new identity made, and written) */
static void load(void)
{
	char path[1100];
	int size = 0;
	char *text;

	file_path(path, sizeof(path));
	text = file_read(path, &size);
	if (text)
	{
		p2p_friends_parse(text, size);
		free(text);
	}
	if (!friends.has_identity)
	{
		/* (a file of no identity is no friends': a new one) */
		posix_random_bytes(friends.seed, P2P_SEED_SIZE);
		identity_derive();
		changed(0);
		save();
		p2p_log("Friends: made this profile's friend key (friends.txt in its folder)");
	}
	if (!friends.generation)
	{
		friends.generation = 1;
		posix_random_bytes(friends.presence_key, P2P_KEY_SIZE);
		presence_derive();
		changed(0);
	}
	{
		char code[P2P_FRIEND_CODE_TEXT_SIZE];

		p2p_friend_code_format(friends.code, code, sizeof(code));
		p2p_log("Friends: this profile's friend code is %s", code);
	}
	friends.topics_wanted = 1;
	friends.checks_left = MAXIMUM_CHECKS;
	friends.checks_time = p2p_now();
}

/* ---------- the messages */

/* (milliseconds, made to increase: a profile started again soon after carries
on later than its last run) */
static unsigned long long next_time(void)
{
	unsigned long long now = p2p_wall_milliseconds();

	friends.sent_time = now > friends.sent_time ? now : friends.sent_time + 1;
	return friends.sent_time;
}

static int message_header(unsigned char *message, int type)
{
	unsigned long long time = next_time();

	message[0] = FRIENDS_MAGIC;
	message[1] = FRIENDS_VERSION;
	message[2] = (unsigned char)type;
	memcpy(message + 3, friends.key, P2P_KEY_SIZE);
	put_long(message + 3 + P2P_KEY_SIZE, (unsigned long)(time >> 32));
	put_long(message + 7 + P2P_KEY_SIZE, (unsigned long)time);
	return HEADER_SIZE;
}

static unsigned long long message_time(const unsigned char *message)
{
	return (unsigned long long)get_long(message + 3 + P2P_KEY_SIZE) << 32 | get_long(message + 7 + P2P_KEY_SIZE);
}

/* the message signed (over context and it) */
static int sign(const unsigned char *context, unsigned char *message, int size)
{
	unsigned char data[P2P_SHA256_SIZE + MAXIMUM_MESSAGE_SIZE];

	memcpy(data, context, P2P_SHA256_SIZE);
	memcpy(data + P2P_SHA256_SIZE, message, (size_t)size);
	p2p_ed25519_sign(friends.seed, friends.key, data, P2P_SHA256_SIZE + size, message + size);
	return size + P2P_SIGNATURE_SIZE;
}

/* a request to a code's inbox */
static void send_request(const unsigned char *code, int type, const unsigned char *body, int body_size)
{
	unsigned char message[MAXIMUM_MESSAGE_SIZE];
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	unsigned char seal[P2P_SHA256_SIZE], context[P2P_SHA256_SIZE];
	char topic[P2P_TOPIC_SIZE];
	int size = message_header(message, type);

	memcpy(message + size, body, (size_t)body_size);
	inbox_of(code, topic, seal, context);
	size = sign(context, message, size + body_size);
	payload[0] = _outer_request;
	size = 1 + p2p_seal(seal, message, size, payload + 1);
	p2p_signal_friends_publish(topic, payload, size, friends_urgent);
}

/* a message to a friend's inbox */
static void send_friend(struct friend_record *record, int type, const unsigned char *body, int body_size)
{
	unsigned char message[MAXIMUM_MESSAGE_SIZE];
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	unsigned char seal[P2P_SHA256_SIZE], context[P2P_SHA256_SIZE];
	char topic[P2P_TOPIC_SIZE];
	int size;

	if (!pair(record))
		return;
	size = message_header(message, type);
	memcpy(message + size, body, (size_t)body_size);
	inbox_of(record->code, topic, seal, context);
	size = sign(context, message, size + body_size);
	payload[0] = _outer_friend;
	memcpy(payload + 1, record->tag_out, TAG_SIZE);
	size = 1 + TAG_SIZE + p2p_seal(record->pair_seal, message, size, payload + 1 + TAG_SIZE);
	p2p_signal_friends_publish(topic, payload, size, friends_urgent);
}

static int name_put(unsigned char *body, const char *name)
{
	int length = (int)strlen(name);

	if (length > P2P_FRIEND_NAME_SIZE - 1)
		length = P2P_FRIEND_NAME_SIZE - 1;
	body[0] = (unsigned char)length;
	memcpy(body + 1, name, (size_t)length);
	return 1 + length;
}

static void send_add(const unsigned char *code)
{
	unsigned char body[1 + P2P_FRIEND_NAME_SIZE];

	send_request(code, _message_add, body, name_put(body, friends.name));
}

static void send_hello(struct friend_record *record)
{
	unsigned char body[P2P_KEY_SIZE + 8 + 1 + P2P_FRIEND_NAME_SIZE];

	memcpy(body, friends.presence_key, P2P_KEY_SIZE);
	put_long(body + P2P_KEY_SIZE, friends.generation);
	put_long(body + P2P_KEY_SIZE + 4, record->has_presence_key ? record->generation : 0);
	send_friend(record, _message_hello, body, P2P_KEY_SIZE + 8 + name_put(body + P2P_KEY_SIZE + 8, friends.name));
	record->hello_time = p2p_now();
	record->hello_sent = 1;
	friends.hello_time = p2p_now();
}

/* ---------- the presence */

/* this machine's, as the party and the game say */
static void presence_now(struct presence *presence)
{
	struct p2p_party_presence party;
	char link[P2P_PARTY_LINK_SIZE], id[2 * 16 + 1];
	int hosting = 0, playing = 0, players = 0, maximum = 0;

	memset(presence, 0, sizeof(*presence));
	p2p_party_presence(&party);
	presence->place = (unsigned char)party.place;
	if (party.active)
	{
		presence->flags |= _presence_party;
		presence->party_count = (unsigned char)party.count;
		snprintf(link, sizeof(link), "%s", party.link);
	}
	else
		p2p_party_spare(link, sizeof(link), id, sizeof(id));
	p2p_discord_game(&hosting, &playing, presence->description, sizeof(presence->description), &players, &maximum);
	/* (a game hosted for its invite alone: no JOIN offered while it is) */
	if (!(hosting && friends.game_closed) && p2p_party_parse_link(link, presence->token))
		presence->flags |= _presence_joinable;
	if (hosting)
		presence->flags |= _presence_hosting;
	if (playing)
		presence->flags |= _presence_playing;
	presence->players = (unsigned char)(players < 0 ? 0 : players > 255 ? 255 : players);
	presence->maximum_players = (unsigned char)(maximum < 0 ? 0 : maximum > 255 ? 255 : maximum);
	snprintf(presence->name, sizeof(presence->name), "%s", friends.name);
}

static int presence_same(const struct presence *a, const struct presence *b)
{
	return a->flags == b->flags && a->place == b->place && a->party_count == b->party_count &&
		a->players == b->players && a->maximum_players == b->maximum_players &&
		!memcmp(a->token, b->token, P2P_TOKEN_SIZE) && !strcmp(a->name, b->name) &&
		!strcmp(a->description, b->description);
}

static void send_presence(const struct presence *presence)
{
	unsigned char message[MAXIMUM_MESSAGE_SIZE];
	unsigned char payload[MAXIMUM_PAYLOAD_SIZE];
	int size = message_header(message, _message_presence);
	int length = (int)strlen(presence->description);

	message[size++] = presence->flags;
	message[size++] = presence->place;
	message[size++] = presence->party_count;
	message[size++] = presence->players;
	message[size++] = presence->maximum_players;
	memcpy(message + size, presence->token, P2P_TOKEN_SIZE);
	size += P2P_TOKEN_SIZE;
	size += name_put(message + size, presence->name);
	message[size++] = (unsigned char)length;
	memcpy(message + size, presence->description, (size_t)length);
	size += length;
	size = sign(friends.presence_context, message, size);
	size = p2p_seal(friends.presence_seal, message, size, payload);
	p2p_signal_friends_publish(friends.presence_topic, payload, size, friends_urgent);
}

static void publish_presence(void)
{
	struct presence presence;

	presence_now(&presence);
	if (!friends.first_sent)
		presence.flags |= _presence_first;
	send_presence(&presence);
	presence.flags &= (unsigned char)~_presence_first;
	friends.published = presence;
	friends.published_time = p2p_now();
	friends.presence_wanted = 0;
	friends.first_sent = 1;
}

/* the presence no longer shared: an OFFLINE, twice (the brokers carry each
at most once) */
static void publish_offline(void)
{
	struct presence presence;

	memset(&presence, 0, sizeof(presence));
	presence.flags = _presence_offline;
	snprintf(presence.name, sizeof(presence.name), "%s", friends.name);
	send_presence(&presence);
	send_presence(&presence);
	friends.first_sent = 0;
}

/* a new presence key (removing or blocking a friend): every friend left is
sent the new one (HELLO, as they confirm it) */
static void presence_rotate(void)
{
	int index;

	/* (each friend sent a HELLO at once, not after HELLO_RETRY) */
	for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
		friends.friends[index].hello_sent = 0;
	friends.generation++;
	posix_random_bytes(friends.presence_key, P2P_KEY_SIZE);
	presence_derive();
	friends.topics_wanted = 1;
	friends.first_sent = 0;
	friends.presence_wanted = 1;
	changed(0);
}

/* ---------- what is heard */

/* the signature's check, within the budget */
static int check_signature(const unsigned char *context, const unsigned char *message, int size)
{
	unsigned char data[P2P_SHA256_SIZE + MAXIMUM_MESSAGE_SIZE];
	unsigned long earned = (p2p_now() - friends.checks_time) / CHECK_INTERVAL;

	if (earned)
	{
		friends.checks_left = friends.checks_left + (int)earned > MAXIMUM_CHECKS ? MAXIMUM_CHECKS :
			friends.checks_left + (int)earned;
		friends.checks_time += earned * CHECK_INTERVAL;
	}
	if (friends.checks_left <= 0)
		return 0;
	friends.checks_left--;
	size -= P2P_SIGNATURE_SIZE;
	memcpy(data, context, P2P_SHA256_SIZE);
	memcpy(data + P2P_SHA256_SIZE, message, (size_t)size);
	return p2p_ed25519_verify(message + 3, data, P2P_SHA256_SIZE + size, message + size);
}

static int name_get(const unsigned char *body, int size, char *name)
{
	if (size < 1 || body[0] > P2P_FRIEND_NAME_SIZE - 1 || 1 + body[0] > size)
		return -1;
	clean_text(name, P2P_FRIEND_NAME_SIZE, (const char *)body + 1, body[0], 0);
	return 1 + body[0];
}

/* a friend made of a key (accepted, or asked for), at its slot; NULL if
there is no room */
static struct friend_record *friend_make(const unsigned char *key, const char *name)
{
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	struct friend_record *record;
	struct request_record *request;
	struct sent_record *sent;
	int index;

	p2p_friend_code_of(key, code);
	record = find_friend(code);
	if (record)
		return record;
	for (index = 0; index < P2P_FRIENDS_MAXIMUM && !record; index++)
	{
		if (!friends.friends[index].used)
			record = &friends.friends[index];
	}
	if (!record)
		return NULL;
	memset(record, 0, sizeof(*record));
	memcpy(record->key, key, P2P_KEY_SIZE);
	memcpy(record->code, code, P2P_FRIEND_CODE_BYTES);
	if (!pair(record))
	{
		memset(record, 0, sizeof(*record));
		return NULL;
	}
	record->used = 1;
	record->added = p2p_wall_seconds();
	snprintf(record->name, sizeof(record->name), "%s", name);
	request = find_request(code);
	if (request)
		memset(request, 0, sizeof(*request));
	sent = find_sent(code);
	if (sent)
		memset(sent, 0, sizeof(*sent));
	ignored_forget(code);
	friends.topics_wanted = 1;
	changed(0);
	return record;
}

static void add_heard(const unsigned char *key, const unsigned char *code, unsigned long long time,
	const unsigned char *body, int size)
{
	char name[P2P_FRIEND_NAME_SIZE];
	struct friend_record *record = find_friend(code);
	struct request_record *request;
	int index;

	if (name_get(body, size, name) != size)
		return;
	if (record)
	{
		/* (a friend that asks again had not heard this profile's ADD:
		answered) */
		if (time <= record->last_time)
			return;
		record->last_time = time;
		if (!record->replied || elapsed(record->reply_time, REPLY_INTERVAL))
		{
			record->replied = 1;
			record->reply_time = p2p_now();
			send_add(code);
			send_hello(record);
		}
		return;
	}
	if (find_sent(code))
	{
		/* (asked for: friends now. An ADD back, as an accept sends: both may
		have asked at once, this profile's own not sent yet) */
		record = friend_make(key, name);
		if (!record)
			return;
		record->last_time = time;
		p2p_log("Friends: %s accepted; friends now", name);
		notice(_p2p_friends_notice_accepted, name);
		send_add(code);
		send_hello(record);
		return;
	}
	if (find_blocked(code) || ignored(code))
		return;
	request = find_request(code);
	if (request)
	{
		if (time > request->last_time)
		{
			request->last_time = time;
			if (strcmp(request->name, name))
			{
				snprintf(request->name, sizeof(request->name), "%s", name);
				changed(0);
			}
		}
		return;
	}
	for (index = 0; index < P2P_FRIENDS_REQUESTS && !request; index++)
	{
		if (!friends.requests[index].used)
			request = &friends.requests[index];
	}
	if (!request)
	{
		p2p_log("Friends: a friend request let go: there are %d waiting already", P2P_FRIENDS_REQUESTS);
		return;
	}
	memset(request, 0, sizeof(*request));
	request->used = 1;
	memcpy(request->key, key, P2P_KEY_SIZE);
	memcpy(request->code, code, P2P_FRIEND_CODE_BYTES);
	snprintf(request->name, sizeof(request->name), "%s", name);
	request->time = p2p_wall_seconds();
	request->last_time = time;
	changed(0);
	p2p_log("Friends: a friend request from %s", name);
	notice(_p2p_friends_notice_request, name);
}

static void hello_heard(struct friend_record *record, const unsigned char *body, int size)
{
	unsigned long generation, held;
	char name[P2P_FRIEND_NAME_SIZE];

	if (size < P2P_KEY_SIZE + 8 + 1 || name_get(body + P2P_KEY_SIZE + 8, size - P2P_KEY_SIZE - 8, name) !=
		size - P2P_KEY_SIZE - 8)
	{
		return;
	}
	generation = get_long(body + P2P_KEY_SIZE);
	held = get_long(body + P2P_KEY_SIZE + 4);
	if (!generation)
		return;
	if (!record->has_presence_key || record->generation != generation ||
		memcmp(record->presence_key, body, P2P_KEY_SIZE))
	{
		record->has_presence_key = 1;
		record->generation = generation;
		memcpy(record->presence_key, body, P2P_KEY_SIZE);
		friend_presence_derive(record);
		record->heard = 0;
		friends.topics_wanted = 1;
		changed(0);
	}
	if (name[0] && strcmp(name, record->name))
	{
		snprintf(record->name, sizeof(record->name), "%s", name);
		changed(0);
	}
	if (record->confirmed != held)
	{
		record->confirmed = held;
		changed(0);
	}
	/* (it holds an older key of this profile's: another HELLO; and, sharing,
	its presence soon) */
	if (held != friends.generation && friends.sharing &&
		(!record->answered || elapsed(record->answered_time, HELLO_ANSWER_INTERVAL)))
	{
		record->answered = 1;
		record->answered_time = p2p_now();
		send_hello(record);
	}
	if (friends.sharing && !friends.presence_wanted)
	{
		friends.presence_wanted = 1;
		friends.presence_wanted_time = p2p_now();
	}
}

static void invite_heard(struct friend_record *record, const unsigned char *body, int size)
{
	struct invite_record *invite = NULL;
	int index;

	if (size != P2P_TOKEN_SIZE || (record->invited && !elapsed(record->invite_time, INVITE_INTERVAL)))
		return;
	record->invited = 1;
	record->invite_time = p2p_now();
	/* (its earlier one replaced, else the oldest) */
	for (index = 0; index < MAXIMUM_INVITES && !invite; index++)
	{
		if (friends.invites[index].serial && !memcmp(friends.invites[index].code, record->code, P2P_FRIEND_CODE_BYTES))
			invite = &friends.invites[index];
	}
	for (index = 0; index < MAXIMUM_INVITES && !invite; index++)
	{
		if (!friends.invites[index].serial || elapsed(friends.invites[index].time, INVITE_TIMEOUT))
			invite = &friends.invites[index];
	}
	if (!invite)
	{
		invite = &friends.invites[0];
		for (index = 1; index < MAXIMUM_INVITES; index++)
		{
			if ((unsigned int)(friends.invites[index].time - invite->time) > 0x7FFFFFFF)
				invite = &friends.invites[index];
		}
	}
	memset(invite, 0, sizeof(*invite));
	invite->serial = ++friends.invite_serial;
	memcpy(invite->code, record->code, P2P_FRIEND_CODE_BYTES);
	snprintf(invite->name, sizeof(invite->name), "%s", record->name);
	memcpy(invite->token, body, P2P_TOKEN_SIZE);
	invite->time = p2p_now();
	record->inviting = 1;
	p2p_log("Friends: %s invites this machine to their party", record->name);
}

static void friend_remove(struct friend_record *record)
{
	int index;

	for (index = 0; index < MAXIMUM_INVITES; index++)
	{
		if (friends.invites[index].serial && !memcmp(friends.invites[index].code, record->code, P2P_FRIEND_CODE_BYTES))
			memset(&friends.invites[index], 0, sizeof(friends.invites[index]));
	}
	memset(record, 0, sizeof(*record));
	friends.topics_wanted = 1;
	presence_rotate();
}

/* a message on the inbox */
static void inbox_heard(const unsigned char *payload, int size)
{
	unsigned char message[MAXIMUM_PAYLOAD_SIZE];
	unsigned char code[P2P_FRIEND_CODE_BYTES];
	struct friend_record *record = NULL;
	unsigned long long time;
	const unsigned char *body;
	int body_size;

	if (size < 1 || size > MAXIMUM_PAYLOAD_SIZE)
		return;
	if (payload[0] == _outer_request)
		size = p2p_open(friends.inbox_seal, payload + 1, size - 1, message);
	else if (payload[0] == _outer_friend && size > 1 + TAG_SIZE)
	{
		record = friend_of_tag(payload + 1);
		if (!record)
			return;
		size = p2p_open(record->pair_seal, payload + 1 + TAG_SIZE, size - 1 - TAG_SIZE, message);
	}
	else
		return;
	if (size < 3 || message[0] != FRIENDS_MAGIC || message[1] != FRIENDS_VERSION ||
		size < HEADER_SIZE + P2P_SIGNATURE_SIZE || size > MAXIMUM_MESSAGE_SIZE)
	{
		return;
	}
	/* (a friend's is its own key's; its own, sent back, is none) */
	if ((record && memcmp(message + 3, record->key, P2P_KEY_SIZE)) || !memcmp(message + 3, friends.key, P2P_KEY_SIZE))
		return;
	time = message_time(message);
	if (record && time <= record->last_time)
		return;
	if (!check_signature(friends.inbox_context, message, size))
		return;
	body = message + HEADER_SIZE;
	body_size = size - HEADER_SIZE - P2P_SIGNATURE_SIZE;
	if (!record)
	{
		if (message[2] != _message_add)
			return;
		p2p_friend_code_of(message + 3, code);
		add_heard(message + 3, code, time, body, body_size);
		return;
	}
	record->last_time = time;
	switch (message[2])
	{
	case _message_hello:
		hello_heard(record, body, body_size);
		break;
	case _message_invite:
		invite_heard(record, body, body_size);
		break;
	case _message_remove:
		if (!body_size)
		{
			p2p_log("Friends: %s is no longer a friend", record->name);
			friend_remove(record);
		}
		break;
	case _message_add:
		add_heard(message + 3, record->code, time, body, body_size);
		break;
	}
}

/* a PRESENCE on a friend's topic */
static void presence_heard(struct friend_record *record, const unsigned char *payload, int size)
{
	unsigned char message[MAXIMUM_PAYLOAD_SIZE];
	struct presence presence;
	unsigned long long time;
	const unsigned char *body;
	int body_size, offset, used, length;
	int was_online;

	if (size > MAXIMUM_PAYLOAD_SIZE)
		return;
	size = p2p_open(record->presence_seal, payload, size, message);
	if (size < HEADER_SIZE + P2P_SIGNATURE_SIZE || size > MAXIMUM_MESSAGE_SIZE || message[0] != FRIENDS_MAGIC ||
		message[1] != FRIENDS_VERSION || message[2] != _message_presence ||
		memcmp(message + 3, record->key, P2P_KEY_SIZE))
	{
		return;
	}
	time = message_time(message);
	if (time <= record->last_time || !check_signature(record->presence_context, message, size))
		return;
	body = message + HEADER_SIZE;
	body_size = size - HEADER_SIZE - P2P_SIGNATURE_SIZE;
	if (body_size < 5 + P2P_TOKEN_SIZE + 2)
		return;
	memset(&presence, 0, sizeof(presence));
	presence.flags = body[0];
	presence.place = body[1] <= _p2p_party_place_local_game ? body[1] : _p2p_party_place_menus;
	presence.party_count = body[2] <= P2P_PARTY_MAXIMUM_MEMBERS ? body[2] : 0;
	presence.players = body[3];
	presence.maximum_players = body[4];
	memcpy(presence.token, body + 5, P2P_TOKEN_SIZE);
	offset = 5 + P2P_TOKEN_SIZE;
	used = name_get(body + offset, body_size - offset, presence.name);
	if (used < 0)
		return;
	offset += used;
	if (offset >= body_size)
		return;
	length = body[offset++];
	if (length > DESCRIPTION_SIZE || offset + length != body_size)
		return;
	clean_text(presence.description, sizeof(presence.description), (const char *)body + offset, length, 1);
	record->last_time = time;
	was_online = record->heard && !(record->presence.flags & _presence_offline) &&
		!elapsed(record->heard_time, PRESENCE_TIMEOUT);
	record->heard = 1;
	record->heard_time = p2p_now();
	record->presence = presence;
	if (presence.name[0] && strcmp(presence.name, record->name))
	{
		snprintf(record->name, sizeof(record->name), "%s", presence.name);
		changed(0);
	}
	if (presence.flags & _presence_offline)
	{
		record->inviting = 0;
		return;
	}
	/* (just come online: told, and answered with this profile's presence) */
	if (presence.flags & _presence_first)
	{
		if (!was_online)
		{
			p2p_log("Friends: %s is online", record->name);
			notice(_p2p_friends_notice_online, record->name);
		}
		if (friends.sharing && !friends.presence_wanted)
		{
			friends.presence_wanted = 1;
			friends.presence_wanted_time = p2p_now();
		}
	}
}

void p2p_friends_heard(const char *topic, const unsigned char *payload, int size)
{
	int index;

	if (!friends.has_identity)
		return;
	if (!strcmp(topic, friends.inbox_topic))
	{
		inbox_heard(payload, size);
		return;
	}
	for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
	{
		struct friend_record *record = &friends.friends[index];

		if (record->used && record->has_presence_key && !strcmp(topic, record->presence_topic))
		{
			presence_heard(record, payload, size);
			return;
		}
	}
}

/* ---------- the p2p thread's pass */

static void topics_tell(void)
{
	char topics[P2P_FRIENDS_TOPICS][P2P_TOPIC_SIZE];
	int count = 0;
	int index;

	memset(topics, 0, sizeof(topics));
	if (friends.has_identity)
	{
		snprintf(topics[count++], P2P_TOPIC_SIZE, "%s", friends.inbox_topic);
		for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
		{
			if (friends.friends[index].used && friends.friends[index].has_presence_key)
				snprintf(topics[count++], P2P_TOPIC_SIZE, "%s", friends.friends[index].presence_topic);
		}
	}
	p2p_signal_friends((const char (*)[P2P_TOPIC_SIZE])topics, count);
	friends.topics_wanted = 0;
}

void p2p_friends_update(void)
{
	int sharing;
	int index;

	if (friends.topics_wanted)
		topics_tell();
	if ((friends.dirty && elapsed(friends.dirty_time, SAVE_DELAY)) ||
		(friends.dirty_recent && elapsed(friends.dirty_time, RECENT_SAVE_INTERVAL)))
	{
		save();
	}
	if (!friends.has_identity)
		return;
	p2p_signal_start();
	if (!friends.setting_time || elapsed(friends.setting_time, 1000))
	{
		friends.setting_time = p2p_now() | 1;
		friends.share_setting = config_boolean("friends.share_status");
	}
	/* (out of a party, the spare's topic listened on while friends may join
	it: p2p_party.c) */
	p2p_party_friends_standby(friends.share_setting);
	if (!p2p_signal_connected())
	{
		friends.was_connected = 0;
		return;
	}
	if (!friends.was_connected)
	{
		/* (connected again: a FIRST, and the requests sent again) */
		friends.was_connected = 1;
		friends.first_sent = 0;
		for (index = 0; index < P2P_FRIENDS_SENT; index++)
			friends.sent[index].sent = 0;
	}
	sharing = friends.share_setting;
	if (sharing != friends.sharing)
	{
		if (!sharing && friends.first_sent)
		{
			publish_offline();
			p2p_log("Friends: appearing offline (friends.share_status)");
		}
		friends.sharing = sharing;
	}
	if (friends.sharing)
	{
		struct presence presence;

		presence_now(&presence);
		if (!friends.first_sent)
			publish_presence();
		else if (elapsed(friends.published_time, PRESENCE_INTERVAL) ||
			((!presence_same(&presence, &friends.published) || (friends.presence_wanted &&
			elapsed(friends.presence_wanted_time, PRESENCE_ANSWER_DELAY))) &&
			elapsed(friends.published_time, PRESENCE_CHANGE_INTERVAL)))
		{
			publish_presence();
		}
	}
	/* (requests sent again, one a pass) */
	for (index = 0; index < P2P_FRIENDS_SENT; index++)
	{
		struct sent_record *sent = &friends.sent[index];

		if (sent->used && (!sent->sent || elapsed(sent->sent_time, ADD_INTERVAL)))
		{
			sent->sent = 1;
			sent->sent_time = p2p_now();
			send_add(sent->code);
			break;
		}
	}
	/* (HELLOs to those who hold an old key of this profile's, one each
	HELLO_SPACING; none while appearing offline) */
	if (friends.sharing && elapsed(friends.hello_time, HELLO_SPACING))
	{
		for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
		{
			struct friend_record *record = &friends.friends[index];

			if (record->used && record->confirmed != friends.generation &&
				(!record->hello_sent || elapsed(record->hello_time, HELLO_RETRY)))
			{
				send_hello(record);
				break;
			}
		}
	}
}

int p2p_friends_publishing(void)
{
	return friends.has_identity && friends.sharing && friends.first_sent;
}

void p2p_friends_quit(void)
{
	if (!friends.has_identity)
		return;
	if (friends.dirty || friends.dirty_recent)
		save();
	if (friends.sharing && friends.first_sent)
	{
		friends_urgent = 1;
		publish_offline();
		friends_urgent = 0;
	}
}

/* ---------- the game's side */

void p2p_friends_set_profile(const char *folder, const char *name)
{
	char cleaned[P2P_FRIEND_NAME_SIZE];

	clean_text(cleaned, sizeof(cleaned), name ? name : "", P2P_FRIEND_NAME_SIZE, 0);
	pthread_mutex_lock(&p2p_lock);
	if (strcmp(cleaned, friends.name))
	{
		snprintf(friends.name, sizeof(friends.name), "%s", cleaned);
		friends.presence_wanted = 1;
		friends.presence_wanted_time = p2p_now();
	}
	if (strcmp(folder ? folder : "", friends.folder))
	{
		char kept[P2P_FRIEND_NAME_SIZE];
		struct p2p_friends_notice notices[MAXIMUM_NOTICES];
		int notice_count = friends.notice_count;
		unsigned long notice_serial = friends.notice_serial, invite_serial = friends.invite_serial;

		/* (another profile: the last one's written, then let go) */
		if (friends.has_identity)
		{
			if (friends.dirty || friends.dirty_recent)
				save();
			if (friends.sharing && friends.first_sent)
				publish_offline();
		}
		memcpy(kept, friends.name, sizeof(kept));
		memcpy(notices, friends.notices, sizeof(notices));
		memset(&friends, 0, sizeof(friends));
		memcpy(friends.name, kept, sizeof(kept));
		memcpy(friends.notices, notices, sizeof(notices));
		friends.notice_count = notice_count;
		friends.notice_serial = notice_serial;
		friends.invite_serial = invite_serial;
		snprintf(friends.folder, sizeof(friends.folder), "%s", folder ? folder : "");
		friends.topics_wanted = 1;
		if (friends.folder[0])
			load();
	}
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_friends_own_code(unsigned char *code)
{
	int has;

	pthread_mutex_lock(&p2p_lock);
	has = friends.has_identity;
	if (has && code)
		memcpy(code, friends.code, P2P_FRIEND_CODE_BYTES);
	pthread_mutex_unlock(&p2p_lock);
	return has;
}

int p2p_friends_code_in_file(const char *path, unsigned char *code)
{
	int size;
	char *text = file_read(path, &size);
	const char *at = text ? strstr(text, "Your friend code: ") : NULL;
	char typed[P2P_FRIEND_CODE_TEXT_SIZE];
	int found;

	if (at)
		snprintf(typed, sizeof(typed), "%s", at + 18);
	found = at && p2p_friend_code_parse(typed, code) > 0;
	free(text);
	return found;
}

int p2p_friends_relation(const unsigned char *code)
{
	int relation = _p2p_friend_none;

	pthread_mutex_lock(&p2p_lock);
	if (!friends.has_identity || !memcmp(code, friends.code, P2P_FRIEND_CODE_BYTES))
		relation = _p2p_friend_self;
	else if (find_friend(code))
		relation = _p2p_friend_online;
	else if (find_request(code))
		relation = _p2p_friend_request;
	else if (find_sent(code))
		relation = _p2p_friend_sent;
	else if (find_blocked(code))
		relation = _p2p_friend_blocked;
	pthread_mutex_unlock(&p2p_lock);
	return relation;
}

int p2p_friends_add(const unsigned char *code, const char *name)
{
	struct request_record *request;
	struct sent_record *sent = NULL;
	struct blocked_record *blocked;
	int result = _p2p_friends_done;
	int index;

	pthread_mutex_lock(&p2p_lock);
	if (!friends.has_identity)
		result = _p2p_friends_no_profile;
	else if (!memcmp(code, friends.code, P2P_FRIEND_CODE_BYTES))
		result = _p2p_friends_self;
	else if (find_friend(code) || find_sent(code))
		result = _p2p_friends_already;
	if (result != _p2p_friends_done)
	{
		pthread_mutex_unlock(&p2p_lock);
		return result;
	}
	blocked = find_blocked(code);
	if (blocked)
		memset(blocked, 0, sizeof(*blocked));
	ignored_forget(code);
	/* (a request from them: accepted, an ADD sent back) */
	request = find_request(code);
	if (request)
	{
		struct request_record copy = *request;
		struct friend_record *record = friend_make(copy.key, copy.name);

		if (!record)
			result = _p2p_friends_full;
		else
		{
			record->last_time = copy.last_time;
			send_add(code);
			send_hello(record);
			p2p_log("Friends: accepted %s's request", record->name);
		}
		pthread_mutex_unlock(&p2p_lock);
		return result;
	}
	for (index = 0; index < P2P_FRIENDS_SENT && !sent; index++)
	{
		if (!friends.sent[index].used)
			sent = &friends.sent[index];
	}
	if (!sent)
		result = _p2p_friends_full;
	else
	{
		char text[P2P_FRIEND_CODE_TEXT_SIZE];

		memset(sent, 0, sizeof(*sent));
		sent->used = 1;
		memcpy(sent->code, code, P2P_FRIEND_CODE_BYTES);
		clean_text(sent->name, sizeof(sent->name), name ? name : "", P2P_FRIEND_NAME_SIZE, 0);
		sent->time = p2p_wall_seconds();
		/* (sent at the next pass, and again until answered) */
		changed(0);
		p2p_friend_code_format(code, text, sizeof(text));
		p2p_log("Friends: a friend request to %s", text);
	}
	pthread_mutex_unlock(&p2p_lock);
	return result;
}

void p2p_friends_ignore(const unsigned char *code)
{
	struct request_record *request;

	pthread_mutex_lock(&p2p_lock);
	request = find_request(code);
	if (request)
	{
		memset(request, 0, sizeof(*request));
		if (!ignored(code))
		{
			memcpy(friends.ignored[friends.ignored_next], code, P2P_FRIEND_CODE_BYTES);
			friends.ignored_next = (friends.ignored_next + 1) % MAXIMUM_IGNORED;
		}
		changed(0);
	}
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_friends_cancel(const unsigned char *code)
{
	struct sent_record *sent;

	pthread_mutex_lock(&p2p_lock);
	sent = find_sent(code);
	if (sent)
	{
		memset(sent, 0, sizeof(*sent));
		changed(0);
	}
	pthread_mutex_unlock(&p2p_lock);
}

/* a friend let go: told (REMOVE, twice: the brokers carry each at most once) */
static void remove_locked(const unsigned char *code)
{
	struct friend_record *record = find_friend(code);

	if (!record)
		return;
	send_friend(record, _message_remove, NULL, 0);
	send_friend(record, _message_remove, NULL, 0);
	p2p_log("Friends: removed %s", record->name);
	friend_remove(record);
}

void p2p_friends_remove(const unsigned char *code)
{
	pthread_mutex_lock(&p2p_lock);
	remove_locked(code);
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_friends_block(const unsigned char *code, const char *name)
{
	struct blocked_record *blocked = NULL;
	struct friend_record *record;
	struct request_record *request;
	struct sent_record *sent;
	char kept[P2P_FRIEND_NAME_SIZE];
	int index;

	pthread_mutex_lock(&p2p_lock);
	if (!friends.has_identity || !memcmp(code, friends.code, P2P_FRIEND_CODE_BYTES))
	{
		pthread_mutex_unlock(&p2p_lock);
		return 0;
	}
	clean_text(kept, sizeof(kept), name ? name : "", P2P_FRIEND_NAME_SIZE, 0);
	record = find_friend(code);
	if (record && !kept[0])
		snprintf(kept, sizeof(kept), "%s", record->name);
	remove_locked(code);
	request = find_request(code);
	if (request)
	{
		if (!kept[0])
			snprintf(kept, sizeof(kept), "%s", request->name);
		memset(request, 0, sizeof(*request));
	}
	sent = find_sent(code);
	if (sent)
		memset(sent, 0, sizeof(*sent));
	blocked = find_blocked(code);
	for (index = 0; index < P2P_FRIENDS_BLOCKED && !blocked; index++)
	{
		if (!friends.blocked[index].used)
			blocked = &friends.blocked[index];
	}
	/* (the oldest let go, when full) */
	if (!blocked)
	{
		memmove(friends.blocked, friends.blocked + 1, sizeof(friends.blocked[0]) * (P2P_FRIENDS_BLOCKED - 1));
		blocked = &friends.blocked[P2P_FRIENDS_BLOCKED - 1];
	}
	memset(blocked, 0, sizeof(*blocked));
	blocked->used = 1;
	memcpy(blocked->code, code, P2P_FRIEND_CODE_BYTES);
	snprintf(blocked->name, sizeof(blocked->name), "%s", kept);
	changed(0);
	p2p_log("Friends: blocked %s", kept[0] ? kept : "a player");
	pthread_mutex_unlock(&p2p_lock);
	return 1;
}

void p2p_friends_unblock(const unsigned char *code)
{
	struct blocked_record *blocked;

	pthread_mutex_lock(&p2p_lock);
	blocked = find_blocked(code);
	if (blocked)
	{
		memset(blocked, 0, sizeof(*blocked));
		changed(0);
	}
	pthread_mutex_unlock(&p2p_lock);
}

/* a friend's online, heard lately and not gone */
static int online(const struct friend_record *record)
{
	return record->heard && !(record->presence.flags & _presence_offline) &&
		!elapsed(record->heard_time, PRESENCE_TIMEOUT);
}

/* the link of a party token */
static void party_link(const unsigned char *token, char *link, int size)
{
	char hex[2 * P2P_TOKEN_SIZE + 1];

	p2p_hex(token, P2P_TOKEN_SIZE, hex);
	snprintf(link, (size_t)size, "%s%s", P2P_PARTY_LINK_PREFIX, hex);
}

int p2p_friends_join(const unsigned char *code)
{
	struct friend_record *record;
	char link[P2P_PARTY_LINK_SIZE];
	int joined = 0;
	int index;

	pthread_mutex_lock(&p2p_lock);
	record = find_friend(code);
	/* (its invite, if it has one waiting: answered; else its presence's
	link) */
	for (index = 0; record && index < MAXIMUM_INVITES && !joined; index++)
	{
		struct invite_record *invite = &friends.invites[index];

		if (invite->serial && !memcmp(invite->code, code, P2P_FRIEND_CODE_BYTES) &&
			!elapsed(invite->time, INVITE_TIMEOUT))
		{
			party_link(invite->token, link, sizeof(link));
			memset(invite, 0, sizeof(*invite));
			record->inviting = 0;
			joined = p2p_party_join_link_locked(link);
		}
	}
	if (record && !joined && online(record) && (record->presence.flags & _presence_joinable))
	{
		party_link(record->presence.token, link, sizeof(link));
		joined = p2p_party_join_link_locked(link);
	}
	if (joined)
		p2p_log("Friends: joining %s's party", record->name);
	pthread_mutex_unlock(&p2p_lock);
	return joined;
}

void p2p_friends_set_game_closed(int closed)
{
	pthread_mutex_lock(&p2p_lock);
	if (friends.game_closed != (closed != 0))
		p2p_log(closed ? "Friends: the game hosted is invite only: friends are offered no JOIN" :
			"Friends: friends may join the game hosted");
	friends.game_closed = closed != 0;
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_friends_game_closed(void)
{
	int closed;

	pthread_mutex_lock(&p2p_lock);
	closed = friends.game_closed;
	pthread_mutex_unlock(&p2p_lock);
	return closed;
}

int p2p_friends_invite(const unsigned char *code)
{
	struct friend_record *record;
	unsigned char token[P2P_TOKEN_SIZE];
	char link[P2P_PARTY_LINK_SIZE + 16];
	int sent = 0;

	/* (the party's link, one made if none: p2p_party.c takes the lock) */
	pthread_mutex_lock(&p2p_lock);
	record = find_friend(code);
	pthread_mutex_unlock(&p2p_lock);
	if (!record || !p2p_party_invite_link(link, sizeof(link)) || !p2p_party_parse_link(link, token))
		return 0;
	pthread_mutex_lock(&p2p_lock);
	record = find_friend(code);
	if (record)
	{
		send_friend(record, _message_invite, token, P2P_TOKEN_SIZE);
		p2p_log("Friends: invited %s to the party", record->name);
		sent = 1;
	}
	pthread_mutex_unlock(&p2p_lock);
	return sent;
}

void p2p_friends_saw_player(const unsigned char *code, const char *name, int opence, const char *game)
{
	struct recent_record *record = NULL;
	char cleaned[P2P_FRIEND_NAME_SIZE];
	long long now = p2p_wall_seconds();
	int index;

	clean_text(cleaned, sizeof(cleaned), name ? name : "", P2P_FRIEND_NAME_SIZE, 0);
	pthread_mutex_lock(&p2p_lock);
	if (!friends.has_identity || (code && !memcmp(code, friends.code, P2P_FRIEND_CODE_BYTES)) || !cleaned[0])
	{
		pthread_mutex_unlock(&p2p_lock);
		return;
	}
	/* (by its code; one of none by its name) */
	for (index = 0; index < P2P_FRIENDS_RECENT && !record; index++)
	{
		struct recent_record *recent = &friends.recent[index];

		if (recent->used && (code ? recent->has_code && !memcmp(recent->code, code, P2P_FRIEND_CODE_BYTES) :
			!recent->has_code && !strcmp(recent->name, cleaned)))
		{
			record = recent;
		}
	}
	if (record && now - record->time < RECENT_SEEN_SECONDS && !strcmp(record->name, cleaned))
	{
		pthread_mutex_unlock(&p2p_lock);
		return;
	}
	/* (a player of a code noted before as one of none, by its name, as its
	code came after: that one let go) */
	for (index = 0; code && index < P2P_FRIENDS_RECENT; index++)
	{
		struct recent_record *recent = &friends.recent[index];

		if (recent->used && !recent->has_code && !recent->opence && !strcmp(recent->name, cleaned) &&
			now - recent->time < RECENT_SEEN_SECONDS * 10)
		{
			memmove(recent, recent + 1, sizeof(*recent) * (size_t)(P2P_FRIENDS_RECENT - 1 - index));
			memset(&friends.recent[P2P_FRIENDS_RECENT - 1], 0, sizeof(friends.recent[0]));
			if (record > recent)
				record--;
			index--;
		}
	}
	/* (the newest first: those before it moved down, the oldest let go) */
	if (!record)
		record = &friends.recent[P2P_FRIENDS_RECENT - 1];
	memmove(friends.recent + 1, friends.recent, (size_t)((char *)record - (char *)friends.recent));
	record = &friends.recent[0];
	memset(record, 0, sizeof(*record));
	record->used = 1;
	record->has_code = code != NULL;
	if (code)
		memcpy(record->code, code, P2P_FRIEND_CODE_BYTES);
	record->opence = opence != 0;
	snprintf(record->name, sizeof(record->name), "%s", cleaned);
	/* (the game's words as Discord has them, if none are given) */
	if (game)
		clean_text(record->game, sizeof(record->game), game, P2P_FRIEND_GAME_SIZE, 1);
	else
	{
		char description[P2P_FRIEND_GAME_SIZE];
		int hosting, playing, players, maximum;

		p2p_discord_game(&hosting, &playing, description, sizeof(description), &players, &maximum);
		clean_text(record->game, sizeof(record->game), description, P2P_FRIEND_GAME_SIZE, 1);
	}
	record->time = now;
	changed(1);
	pthread_mutex_unlock(&p2p_lock);
}

/* a friend as the list shows it */
static void entry_of_friend(struct p2p_friend *entry, const struct friend_record *record)
{
	memset(entry, 0, sizeof(*entry));
	entry->kind = online(record) ? _p2p_friend_online : _p2p_friend_offline;
	memcpy(entry->code, record->code, P2P_FRIEND_CODE_BYTES);
	snprintf(entry->name, sizeof(entry->name), "%s", record->name);
	entry->time = record->added;
	entry->has_code = 1;
	entry->inviting = (unsigned char)record->inviting;
	if (entry->kind != _p2p_friend_online)
		return;
	entry->place = record->presence.place;
	entry->hosting = (record->presence.flags & _presence_hosting) != 0;
	entry->playing = (record->presence.flags & _presence_playing) != 0;
	entry->party_count = (record->presence.flags & _presence_party) ? record->presence.party_count : 0;
	entry->players = record->presence.players;
	entry->maximum_players = record->presence.maximum_players;
	entry->joinable = (record->presence.flags & _presence_joinable) != 0;
	snprintf(entry->game, sizeof(entry->game), "%s", record->presence.description);
}

/* the order the list shows: online friends, requests, offline friends,
requests sent, recent players, blocked; by name within each (recent
players by when) */
int p2p_friends_compare(const struct p2p_friend *first, const struct p2p_friend *second)
{
	if (first->kind != second->kind)
		return first->kind < second->kind ? -1 : 1;
	if (first->kind == _p2p_friend_recent)
		return first->time > second->time ? -1 : first->time < second->time ? 1 : 0;
	return strcmp(first->name, second->name);
}

static void list_sort(struct p2p_friend *entries, int count)
{
	int index;

	/* (few: insertion) */
	for (index = 1; index < count; index++)
	{
		struct p2p_friend moving = entries[index];
		int at = index;

		while (at > 0 && p2p_friends_compare(&moving, &entries[at - 1]) < 0)
		{
			entries[at] = entries[at - 1];
			at--;
		}
		entries[at] = moving;
	}
}

int p2p_friends_list(struct p2p_friend *entries, int maximum)
{
	int count = 0;
	int index;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < P2P_FRIENDS_MAXIMUM && count < maximum; index++)
	{
		if (friends.friends[index].used)
			entry_of_friend(&entries[count++], &friends.friends[index]);
	}
	for (index = 0; index < P2P_FRIENDS_REQUESTS && count < maximum; index++)
	{
		struct request_record const *request = &friends.requests[index];

		if (!request->used)
			continue;
		memset(&entries[count], 0, sizeof(entries[count]));
		entries[count].kind = _p2p_friend_request;
		memcpy(entries[count].code, request->code, P2P_FRIEND_CODE_BYTES);
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", request->name);
		entries[count].time = request->time;
		entries[count].has_code = 1;
		count++;
	}
	for (index = 0; index < P2P_FRIENDS_SENT && count < maximum; index++)
	{
		struct sent_record const *sent = &friends.sent[index];

		if (!sent->used)
			continue;
		memset(&entries[count], 0, sizeof(entries[count]));
		entries[count].kind = _p2p_friend_sent;
		memcpy(entries[count].code, sent->code, P2P_FRIEND_CODE_BYTES);
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", sent->name);
		entries[count].time = sent->time;
		entries[count].has_code = 1;
		count++;
	}
	for (index = 0; index < P2P_FRIENDS_RECENT && count < maximum; index++)
	{
		struct recent_record const *recent = &friends.recent[index];

		/* (a friend is listed as one, not again here) */
		if (!recent->used || (recent->has_code && find_friend(recent->code)))
			continue;
		memset(&entries[count], 0, sizeof(entries[count]));
		entries[count].kind = _p2p_friend_recent;
		memcpy(entries[count].code, recent->code, P2P_FRIEND_CODE_BYTES);
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", recent->name);
		snprintf(entries[count].game, sizeof(entries[count].game), "%s", recent->game);
		entries[count].time = recent->time;
		entries[count].has_code = (unsigned char)recent->has_code;
		entries[count].opence = (unsigned char)recent->opence;
		if (recent->has_code)
		{
			entries[count].relation = (unsigned char)(find_friend(recent->code) ? _p2p_friend_online :
				find_sent(recent->code) ? _p2p_friend_sent : find_request(recent->code) ? _p2p_friend_request :
				find_blocked(recent->code) ? _p2p_friend_blocked : _p2p_friend_none);
		}
		count++;
	}
	for (index = 0; index < P2P_FRIENDS_BLOCKED && count < maximum; index++)
	{
		struct blocked_record const *blocked = &friends.blocked[index];

		if (!blocked->used)
			continue;
		memset(&entries[count], 0, sizeof(entries[count]));
		entries[count].kind = _p2p_friend_blocked;
		memcpy(entries[count].code, blocked->code, P2P_FRIEND_CODE_BYTES);
		snprintf(entries[count].name, sizeof(entries[count].name), "%s", blocked->name);
		entries[count].has_code = 1;
		count++;
	}
	pthread_mutex_unlock(&p2p_lock);
	list_sort(entries, count);
	return count;
}

void p2p_friends_state(struct p2p_friends_state *state)
{
	int index;

	memset(state, 0, sizeof(*state));
	pthread_mutex_lock(&p2p_lock);
	state->identity = friends.has_identity;
	if (friends.has_identity)
		p2p_friend_code_format(friends.code, state->code, sizeof(state->code));
	snprintf(state->name, sizeof(state->name), "%s", friends.name);
	state->connected = p2p_signal_connected();
	state->sharing = friends.sharing;
	for (index = 0; index < P2P_FRIENDS_MAXIMUM; index++)
	{
		if (!friends.friends[index].used)
			continue;
		state->friends++;
		if (online(&friends.friends[index]))
			state->online++;
	}
	for (index = 0; index < P2P_FRIENDS_REQUESTS; index++)
	{
		if (friends.requests[index].used)
			state->requests++;
	}
	pthread_mutex_unlock(&p2p_lock);
}

int p2p_friends_notice(struct p2p_friends_notice *latest)
{
	int has;

	pthread_mutex_lock(&p2p_lock);
	has = friends.notice_count > 0;
	if (has)
		*latest = friends.notices[friends.notice_count - 1];
	pthread_mutex_unlock(&p2p_lock);
	return has;
}

unsigned long p2p_friends_invite_prompt(char *name, int size)
{
	unsigned long serial = 0;
	unsigned long oldest = 0;
	int index;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < MAXIMUM_INVITES; index++)
	{
		struct invite_record const *invite = &friends.invites[index];

		if (invite->serial && !elapsed(invite->time, INVITE_TIMEOUT) && (!serial ||
			(unsigned int)(invite->time - oldest) > 0x7FFFFFFF))
		{
			serial = invite->serial;
			oldest = invite->time;
			if (name && size > 0)
				snprintf(name, (size_t)size, "%s", invite->name);
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return serial;
}

void p2p_friends_answer(unsigned long serial, int accept)
{
	char link[P2P_PARTY_LINK_SIZE];
	int index;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; serial && index < MAXIMUM_INVITES; index++)
	{
		struct invite_record *invite = &friends.invites[index];
		struct friend_record *record;

		if (invite->serial != serial)
			continue;
		record = find_friend(invite->code);
		if (record)
			record->inviting = 0;
		if (accept && record)
		{
			party_link(invite->token, link, sizeof(link));
			if (p2p_party_join_link_locked(link))
				p2p_log("Friends: joining %s's party", record->name);
		}
		memset(invite, 0, sizeof(*invite));
	}
	pthread_mutex_unlock(&p2p_lock);
}
