/*
P2P_DISCORD.C

Discord's rich presence, invites and asks to join (p2p.c), through the
Discord desktop client's local RPC socket (a Unix socket, or a named pipe on
Windows; debug.discord_socket names a test's stand-in).

What the activity shows (discord.rich_presence; nothing while it is off):
- the menus: "In the menus", and in a party "In a party (3 of 16)";
- a game this machine hosts: "Hosting OpenH1 120 Tick · Slayer on Blood
  Gulch" (what the game says it is: p2p_discord_set_game), and "In the
  lobby" or "Playing" with its players (or the party, in one);
- a game it joined: what it is, and the same.
Its art is the application's: "logo" (every application's; OpenCE's has it
alone), and beside it "party" or "host" in an application of OpenH1's own
(any other than OpenCE's).

Joining (discord.allow_join_requests; neither while off): the activity has
a party (its id and size) and a join secret, which Discord gives only to
the friends the player invites (Discord's Invite to Join) or lets in (a
friend's Ask to Join). The secret is:
- in a party, the party's link (its id is the party's topic's digits, which
  let no one in);
- else in an internet game, its invite, as before (its id is its host's key
  hash);
- else (the menus, a LAN game) the link of a party not made yet, the spare
  (p2p_party_spare), made when a friend is let in, or when one given the
  link arrives (p2p_party_standby listens on its topic meanwhile).
A friend's ask (ACTIVITY_JOIN_REQUEST) is shown in the game
(port/linux/game/network_party.c, p2p_discord_join_request), and answered
(p2p_discord_answer): SEND_ACTIVITY_JOIN_INVITE lets them in,
CLOSE_ACTIVITY_REQUEST does not; one not answered in REQUEST_TIMEOUT is
declined, and so is any while joining is off. The friend's game gets the
secret as ACTIVITY_JOIN and takes it as a link opened (p2p_invite_received):
a party's link joins the party, which takes them on to its leader's lobby or
game (p2p_party.c's following).

It goes through the Discord application in discord.application_id
(config.toml; OpenCE's by default, until OpenH1 has its own: its name is
what Discord shows as being played). Without one, or without Discord
running, invites are still links (p2p.c).

The protocol: frames of a little-endian opcode and length, then JSON. The
handshake (opcode 0) names the application; commands and events are opcode
1; 2 closes; 3 and 4 are ping and pong. Discord takes five activities in
twenty seconds: one is sent each ACTIVITY_INTERVAL at most, the latest.
Nothing here waits for Discord (the p2p thread holds its lock, which the
game's threads take): what it does not take at once waits in a buffer, and
a client that lets that fill up is disconnected.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum
{
	RETRY_INTERVAL = 20000,
	ACTIVITY_INTERVAL = 4000,
	/* an ask to join waits this long for its answer */
	REQUEST_TIMEOUT = 30000,
	MAXIMUM_REQUESTS = 4,
	BUFFER_SIZE = 16384,
	MAXIMUM_FRAME_SIZE = 2048,
	MAXIMUM_SECRET_SIZE = 128,
	MAXIMUM_TEXT_SIZE = 128,
	/* a game's invite code: its host's key hash and its token, in hex */
	CODE_SIZE = 2 * (P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE) + 1,

	_opcode_handshake = 0,
	_opcode_frame,
	_opcode_close,
	_opcode_ping,
	_opcode_pong,

	/* an ask's answer */
	_answer_none = 0,
	_answer_accept,
	_answer_decline,
};

/* OpenCE's application, the default, whose art is "logo" alone */
#define OPENCE_APPLICATION "1553978809840050229"

/* an activity, as sent */
struct activity
{
	char details[MAXIMUM_TEXT_SIZE];
	char state[MAXIMUM_TEXT_SIZE];
	char small_image[16];
	char small_text[32];
	/* its party (empty: none), and the size shown with the state */
	char party_id[48];
	int size, maximum;
	/* its join secret, and whether that is the spare party's link */
	char secret[MAXIMUM_SECRET_SIZE];
	int spare;
};

/* a friend's ask to join (serial 0: none), their id and name, when it came,
and its answer */
struct request
{
	unsigned long serial;
	char id[P2P_DISCORD_ID_SIZE];
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	unsigned long time;
	int answer;
};

static struct
{
	int checked;
	int enabled;
	/* an application of OpenH1's own, whose art has "party" and "host" */
	int own_art;
	char application[32];
	int handle;
	int ready;
	unsigned long attempt_time;
	int attempted;
	unsigned long nonce;
	unsigned char input[BUFFER_SIZE];
	int input_size;
	unsigned char output[BUFFER_SIZE];
	int output_size;

	/* hosting an internet game (p2p.c): its invite code and players */
	int hosting;
	char secret[MAXIMUM_SECRET_SIZE];
	int player_count;
	int maximum_player_count;

	/* the game this machine is in, as the game said (p2p_discord_set_game);
	a game joined's invite code and players */
	struct
	{
		int hosting;
		int playing;
		char description[MAXIMUM_TEXT_SIZE - 16];
		char code[CODE_SIZE];
		int player_count;
		int maximum_player_count;
	} game;

	/* the activity last sent since READY (shown: one, not none), and when */
	int sent_any;
	int shown;
	struct activity sent;
	unsigned long sent_time;

	/* the asks to join, the oldest first */
	struct request requests[MAXIMUM_REQUESTS];
	unsigned long request_serial;

	/* the Discord user signed in to the client (its READY), as told: an id
	of digits, a name of the letters, digits and marks Discord's allow (a
	host logs them, for a player it drops for cheating) */
	char user_id[P2P_DISCORD_ID_SIZE];
	char user_name[P2P_DISCORD_NAME_SIZE];
} discord = { .handle = -1 };

static int elapsed(unsigned long since, unsigned long time)
{
	/* (unsigned, as the clock wraps) */
	return (unsigned int)(p2p_now() - since) >= (unsigned int)time;
}

/* the text kept of a Discord user's id or name: of the characters allowed
(the rest left out), no longer than the size (and ended) */
void p2p_discord_sanitize(char *destination, int size, const char *source, int name)
{
	int length = 0;

	for (; source && *source && length < size - 1; source++)
	{
		char character = *source;

		if ((character >= '0' && character <= '9') ||
			(name && ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				character == '_' || character == '.' || character == '-')))
		{
			destination[length++] = character;
		}
	}
	destination[length] = 0;
}

/* the Discord user signed in, as told (empty if none); under p2p_lock */
void p2p_discord_user(char *id, int id_size, char *name, int name_size)
{
	p2p_discord_sanitize(id, id_size, discord.user_id, 0);
	p2p_discord_sanitize(name, name_size, discord.user_name, 1);
}

static void discord_close(void)
{
	if (discord.handle >= 0)
		posix_discord_close(discord.handle);
	discord.handle = -1;
	discord.ready = 0;
	discord.input_size = 0;
	discord.output_size = 0;
	/* (the asks were that connection's; the activity is sent again to the
	next) */
	discord.sent_any = 0;
	memset(discord.requests, 0, sizeof(discord.requests));
	p2p_party_standby(0);
}

/* what can be written now */
static void discord_flush(void)
{
	while (discord.handle >= 0 && discord.output_size > 0)
	{
		int written = posix_discord_write(discord.handle, discord.output, discord.output_size);

		if (written < 0)
		{
			discord_close();
			return;
		}
		if (!written)
			return;
		memmove(discord.output, discord.output + written, (size_t)(discord.output_size - written));
		discord.output_size -= written;
	}
}

static void discord_send(int opcode, const char *json, int size)
{
	unsigned char *frame;

	if (discord.handle < 0 || size < 0 || size > MAXIMUM_FRAME_SIZE)
		return;
	if (discord.output_size + 8 + size > BUFFER_SIZE)
	{
		platform_log("Internet play: Discord is not responding; disconnected from it");
		discord_close();
		return;
	}
	frame = discord.output + discord.output_size;
	frame[0] = (unsigned char)opcode;
	frame[1] = frame[2] = frame[3] = 0;
	frame[4] = (unsigned char)size;
	frame[5] = (unsigned char)(size >> 8);
	frame[6] = frame[7] = 0;
	memcpy(frame + 8, json, (size_t)size);
	discord.output_size += 8 + size;
	discord_flush();
}

/* ---------- JSON */

/* text built a part at a time, cut short (never overrun) when too long */
struct writer
{
	char *text;
	int size;
	int length;
};

static void put(struct writer *writer, const char *format, ...)
{
	va_list arguments;
	int written;

	if (writer->length >= writer->size - 1)
		return;
	va_start(arguments, format);
	written = vsnprintf(writer->text + writer->length, (size_t)(writer->size - writer->length), format, arguments);
	va_end(arguments);
	if (written < 0)
		return;
	writer->length += written;
	if (writer->length > writer->size - 1)
		writer->length = writer->size - 1;
}

/* the length of the UTF-8 sequence at text (0: not one) */
static int utf8_length(const unsigned char *text)
{
	int length, index;

	if (text[0] < 0x80)
		return 1;
	if (text[0] >= 0xC2 && text[0] <= 0xDF)
		length = 2;
	else if (text[0] >= 0xE0 && text[0] <= 0xEF)
		length = 3;
	else if (text[0] >= 0xF0 && text[0] <= 0xF4)
		length = 4;
	else
		return 0;
	for (index = 1; index < length; index++)
	{
		if ((text[index] & 0xC0) != 0x80)
			return 0;
	}
	return length;
}

/* a string's value, quoted: its quotes, backslashes and control characters
escaped, anything that is not UTF-8 a "?" */
static void put_string(struct writer *writer, const char *value)
{
	const unsigned char *text = (const unsigned char *)value;

	put(writer, "\"");
	while (*text)
	{
		int length = utf8_length(text);

		if (!length)
		{
			put(writer, "?");
			text++;
		}
		else if (*text == '"' || *text == '\\')
			put(writer, "\\%c", *text++);
		else if (*text < 0x20)
			put(writer, "\\u%04x", *text++);
		else
		{
			put(writer, "%.*s", length, (const char *)text);
			text += length;
		}
	}
	put(writer, "\"");
}

/* a code point as UTF-8 (at most 4 bytes) */
static int utf8_put(unsigned long code, char *text)
{
	if (code < 0x80)
	{
		text[0] = (char)code;
		return 1;
	}
	if (code < 0x800)
	{
		text[0] = (char)(0xC0 | code >> 6);
		text[1] = (char)(0x80 | (code & 0x3F));
		return 2;
	}
	if (code < 0x10000)
	{
		text[0] = (char)(0xE0 | code >> 12);
		text[1] = (char)(0x80 | ((code >> 6) & 0x3F));
		text[2] = (char)(0x80 | (code & 0x3F));
		return 3;
	}
	text[0] = (char)(0xF0 | code >> 18);
	text[1] = (char)(0x80 | ((code >> 12) & 0x3F));
	text[2] = (char)(0x80 | ((code >> 6) & 0x3F));
	text[3] = (char)(0x80 | (code & 0x3F));
	return 4;
}

static int hex_digits(const char *text, int count, unsigned long *value)
{
	int index;

	*value = 0;
	for (index = 0; index < count; index++)
	{
		char digit = text[index];

		if (digit >= '0' && digit <= '9')
			*value = *value << 4 | (unsigned long)(digit - '0');
		else if ((digit | 0x20) >= 'a' && (digit | 0x20) <= 'f')
			*value = *value << 4 | (unsigned long)((digit | 0x20) - 'a' + 10);
		else
			return 0;
	}
	return 1;
}

/* a string field's value in json (its first, after start), its escapes
undone (\u's as UTF-8; a lone surrogate a "?"), cut to size: 0 if it has
none (or the field is not a string) */
static int json_string(const char *json, const char *name, char *value, int size)
{
	char pattern[64];
	const char *start;
	int length = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", name);
	start = strstr(json, pattern);
	if (!start)
		return 0;
	start += strlen(pattern);
	while (*start == ' ' || *start == ':')
		start++;
	if (*start++ != '"')
		return 0;
	while (*start && *start != '"' && length < size - 1)
	{
		char encoded[4];
		int count = 1;

		if (*start != '\\')
			encoded[0] = *start++;
		else if (start[1] == 'u')
		{
			unsigned long code, low;

			if (!hex_digits(start + 2, 4, &code))
				return 0;
			start += 6;
			if (code >= 0xD800 && code <= 0xDBFF && start[0] == '\\' && start[1] == 'u' &&
				hex_digits(start + 2, 4, &low) && low >= 0xDC00 && low <= 0xDFFF)
			{
				code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
				start += 6;
			}
			if (code >= 0xD800 && code <= 0xDFFF)
				code = '?';
			count = utf8_put(code, encoded);
		}
		else if (start[1])
		{
			static const char escapes[] = "b\bf\fn\nr\rt\t";
			const char *escape = strchr(escapes, start[1]);

			encoded[0] = escape && (escape - escapes) % 2 == 0 ? escape[1] : start[1];
			start += 2;
		}
		else
			break;
		if (length + count > size - 1)
			break;
		memcpy(value + length, encoded, (size_t)count);
		length += count;
	}
	value[length] = 0;
	return 1;
}

/* a name to show the player (the in-game prompt's font is Latin-1): UTF-8
as Latin-1, printable, anything else a "?" */
static void display_name(char *destination, int size, const char *source)
{
	const unsigned char *text = (const unsigned char *)source;
	int length = 0;

	while (*text && length < size - 1)
	{
		int count = utf8_length(text);
		unsigned long code = '?';

		if (count == 1)
			code = *text;
		else if (count == 2)
			code = (unsigned long)(text[0] & 0x1F) << 6 | (text[1] & 0x3F);
		if (!count)
			count = 1;
		if ((code < 0x20 || code == 0x7F || (code >= 0x80 && code < 0xA0) || code > 0xFF) && code != '?')
			code = '?';
		destination[length++] = (char)code;
		text += count;
	}
	destination[length] = 0;
}

/* ---------- the activity */

/* the activity for this machine, as it is now (FALSE: none is shown) */
static int compose(struct activity *activity)
{
	struct p2p_party_presence party;
	int joins = config_boolean("discord.allow_join_requests");
	int in_game = discord.game.description[0] != 0;
	const char *code = discord.hosting ? discord.secret : discord.game.code;

	memset(activity, 0, sizeof(*activity));
	if (!config_boolean("discord.rich_presence"))
		return 0;
	p2p_party_presence(&party);
	if (!in_game)
		snprintf(activity->details, sizeof(activity->details), "In the menus");
	else
	{
		/* (the description's "OpenH1 <rate> Tick", which friends' machines
		read as sent, doubles_friend_in_kind, said by the name players know) */
		const char *description = discord.game.description;
		int renamed = !strncmp(description, "OpenH1 ", 7);

		snprintf(activity->details, sizeof(activity->details), "%s%s%s", discord.game.hosting ? "Hosting " : "",
			renamed ? "GOAT H1 " : "", description + (renamed ? 7 : 0));
	}
	if (discord.own_art && (party.active || discord.game.hosting))
	{
		snprintf(activity->small_image, sizeof(activity->small_image), "%s", party.active ? "party" : "host");
		snprintf(activity->small_text, sizeof(activity->small_text), "%s", party.active ? "In a party" : "Hosting");
	}
	if (party.active)
	{
		snprintf(activity->state, sizeof(activity->state), "In a party");
		snprintf(activity->party_id, sizeof(activity->party_id), "%s", party.id);
		activity->size = party.count;
		activity->maximum = P2P_PARTY_MAXIMUM_MEMBERS;
		if (joins)
			snprintf(activity->secret, sizeof(activity->secret), "%s", party.link);
		return 1;
	}
	if (in_game)
	{
		int count = discord.hosting ? discord.player_count : discord.game.player_count;
		int maximum = discord.hosting ? discord.maximum_player_count : discord.game.maximum_player_count;

		snprintf(activity->state, sizeof(activity->state), "%s", discord.game.playing ? "Playing" : "In the lobby");
		if (code[0] && count > 0 && maximum >= count)
		{
			/* (the game's id: its host's key hash, its code's first half) */
			snprintf(activity->party_id, sizeof(activity->party_id), "%.*s", 2 * P2P_KEY_HASH_SIZE, code);
			activity->size = count;
			activity->maximum = maximum;
			if (joins)
				snprintf(activity->secret, sizeof(activity->secret), "%s", code);
		}
		return 1;
	}
	/* (in the menus, out of a party: a friend let in joins the spare party,
	made then) */
	if (joins)
	{
		char link[P2P_PARTY_LINK_SIZE];

		p2p_party_spare(link, sizeof(link), activity->party_id, sizeof(activity->party_id));
		snprintf(activity->secret, sizeof(activity->secret), "%s", link);
		activity->size = 1;
		activity->maximum = P2P_PARTY_MAXIMUM_MEMBERS;
		activity->spare = 1;
	}
	return 1;
}

static void send_activity(const struct activity *activity, int shown)
{
	char json[MAXIMUM_FRAME_SIZE];
	struct writer writer = { json, sizeof(json), 0 };

	put(&writer, "{\"cmd\":\"SET_ACTIVITY\",\"nonce\":\"%lu\",\"args\":{\"pid\":%lu", ++discord.nonce,
		(unsigned long)posix_process_id());
	if (shown)
	{
		put(&writer, ",\"activity\":{\"details\":");
		put_string(&writer, activity->details);
		if (activity->state[0])
		{
			put(&writer, ",\"state\":");
			put_string(&writer, activity->state);
		}
		/* (the images uploaded under these names in the application's Rich
		Presence art assets) */
		put(&writer, ",\"assets\":{\"large_image\":\"logo\",\"large_text\":\"GOAT H1\"");
		if (activity->small_image[0])
		{
			put(&writer, ",\"small_image\":");
			put_string(&writer, activity->small_image);
			put(&writer, ",\"small_text\":");
			put_string(&writer, activity->small_text);
		}
		put(&writer, "}");
		if (activity->party_id[0])
		{
			put(&writer, ",\"party\":{\"id\":");
			put_string(&writer, activity->party_id);
			if (activity->size > 0)
				put(&writer, ",\"size\":[%d,%d]", activity->size, activity->maximum);
			put(&writer, "}");
		}
		if (activity->secret[0])
		{
			put(&writer, ",\"secrets\":{\"join\":");
			put_string(&writer, activity->secret);
			put(&writer, "}");
		}
		put(&writer, ",\"instance\":false}");
	}
	put(&writer, "}}");
	discord_send(_opcode_frame, json, writer.length);
}

/* ---------- the asks to join */

/* a command for a friend's ask (their id is digits) */
static void send_request_command(const char *command, const char *id)
{
	char json[256];
	int size = snprintf(json, sizeof(json), "{\"cmd\":\"%s\",\"nonce\":\"%lu\",\"args\":{\"user_id\":\"%s\"}}",
		command, ++discord.nonce, id);

	discord_send(_opcode_frame, json, size);
}

static void request_heard(const char *json)
{
	const char *user = strstr(json, "\"user\"");
	char value[128];
	char id[P2P_DISCORD_ID_SIZE];
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	int index;

	if (!user || !json_string(user, "id", value, sizeof(value)))
		return;
	p2p_discord_sanitize(id, sizeof(id), value, 0);
	if (!id[0])
		return;
	/* (the name the friend shows, else their user name) */
	if (!json_string(user, "global_name", value, sizeof(value)) || !value[0])
	{
		if (!json_string(user, "username", value, sizeof(value)))
			value[0] = 0;
	}
	display_name(name, sizeof(name), value);
	if (!name[0])
		snprintf(name, sizeof(name), "A Discord friend");
	if (!discord.shown || !discord.sent.secret[0] || !config_boolean("discord.allow_join_requests"))
	{
		platform_log("Internet play: a Discord friend asked to join; declined (discord.allow_join_requests)");
		send_request_command("CLOSE_ACTIVITY_REQUEST", id);
		return;
	}
	for (index = 0; index < MAXIMUM_REQUESTS; index++)
	{
		struct request *request = &discord.requests[index];

		/* (asked again: the same ask, from now) */
		if (request->serial && !strcmp(request->id, id) && request->answer == _answer_none)
		{
			request->time = p2p_now();
			return;
		}
	}
	for (index = 0; index < MAXIMUM_REQUESTS && discord.requests[index].serial; index++)
		;
	if (index == MAXIMUM_REQUESTS)
	{
		send_request_command("CLOSE_ACTIVITY_REQUEST", id);
		return;
	}
	platform_log("Internet play: a Discord friend asks to join");
	discord.requests[index].serial = ++discord.request_serial;
	snprintf(discord.requests[index].id, sizeof(discord.requests[index].id), "%s", id);
	snprintf(discord.requests[index].name, sizeof(discord.requests[index].name), "%s", name);
	discord.requests[index].time = p2p_now();
	discord.requests[index].answer = _answer_none;
}

/* the asks answered (or let go of: not answered in time, or joining turned
off) sent their answers, and taken out */
static void requests_update(int allowed)
{
	int index = 0;

	while (index < MAXIMUM_REQUESTS && discord.requests[index].serial)
	{
		struct request *request = &discord.requests[index];

		if (request->answer == _answer_none && (!allowed || elapsed(request->time, REQUEST_TIMEOUT)))
			request->answer = _answer_decline;
		if (request->answer == _answer_none)
		{
			index++;
			continue;
		}
		if (request->answer == _answer_accept)
		{
			/* (out of a party, the spare party whose link Discord has made
			now, the friend's to join) */
			if (discord.sent.spare)
				p2p_party_spare_lead();
			platform_log("Internet play: let a Discord friend in");
			send_request_command("SEND_ACTIVITY_JOIN_INVITE", request->id);
		}
		else
		{
			platform_log("Internet play: declined a Discord friend's ask to join");
			send_request_command("CLOSE_ACTIVITY_REQUEST", request->id);
		}
		memmove(request, request + 1, sizeof(*request) * (size_t)(MAXIMUM_REQUESTS - index - 1));
		memset(&discord.requests[MAXIMUM_REQUESTS - 1], 0, sizeof(*request));
		if (discord.handle < 0)
			return;
	}
}

/* ---------- what Discord sends */

static void frame_received(int opcode, char *json)
{
	char event[64];

	switch (opcode)
	{
	case _opcode_frame:
		if (!json_string(json, "evt", event, sizeof(event)))
			break;
		if (!strcmp(event, "READY"))
		{
			char command[256];
			int size;

			discord.ready = 1;
			platform_log("Internet play: connected to Discord");
			/* (who is signed in: its user, after the configuration) */
			{
				const char *user = strstr(json, "\"user\"");
				char value[128];

				discord.user_id[0] = 0;
				discord.user_name[0] = 0;
				if (user && json_string(user, "id", value, sizeof(value)))
					p2p_discord_sanitize(discord.user_id, sizeof(discord.user_id), value, 0);
				if (user && json_string(user, "username", value, sizeof(value)))
					p2p_discord_sanitize(discord.user_name, sizeof(discord.user_name), value, 1);
			}
			size = snprintf(command, sizeof(command),
				"{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN\",\"nonce\":\"%lu\"}", ++discord.nonce);
			discord_send(_opcode_frame, command, size);
			size = snprintf(command, sizeof(command),
				"{\"cmd\":\"SUBSCRIBE\",\"evt\":\"ACTIVITY_JOIN_REQUEST\",\"nonce\":\"%lu\"}", ++discord.nonce);
			discord_send(_opcode_frame, command, size);
			discord.sent_any = 0;
		}
		else if (!strcmp(event, "ACTIVITY_JOIN"))
		{
			char secret[MAXIMUM_SECRET_SIZE];

			/* (an invite this player accepted, or a friend's Join, or the
			answer to this player's ask: theirs to join, as a link opened) */
			if (json_string(json, "secret", secret, sizeof(secret)))
			{
				platform_log("Internet play: joining through Discord");
				p2p_invite_received(secret);
			}
		}
		else if (!strcmp(event, "ACTIVITY_JOIN_REQUEST"))
			request_heard(json);
		else if (!strcmp(event, "ERROR"))
		{
			char message[256];

			if (json_string(json, "message", message, sizeof(message)))
				platform_log("Internet play: Discord: %s", message);
		}
		break;
	case _opcode_close:
		discord_close();
		break;
	case _opcode_ping:
		discord_send(_opcode_pong, json, (int)strlen(json));
		break;
	}
}

static void discord_read(void)
{
	for (;;)
	{
		int size = posix_discord_read(discord.handle, discord.input + discord.input_size,
			BUFFER_SIZE - 1 - discord.input_size);

		if (size < 0)
		{
			if (discord.ready)
				platform_log("Internet play: disconnected from Discord");
			discord_close();
			return;
		}
		if (size == 0)
			break;
		discord.input_size += size;
		if (discord.input_size >= BUFFER_SIZE - 1)
			break;
	}
	while (discord.input_size >= 8)
	{
		int opcode = discord.input[0] | discord.input[1] << 8;
		int length = discord.input[4] | discord.input[5] << 8 | discord.input[6] << 16;
		char json[BUFFER_SIZE];

		if (length > BUFFER_SIZE - 9)
		{
			discord_close();
			return;
		}
		if (discord.input_size < 8 + length)
			break;
		memcpy(json, discord.input + 8, (size_t)length);
		json[length] = 0;
		memmove(discord.input, discord.input + 8 + length, (size_t)(discord.input_size - 8 - length));
		discord.input_size -= 8 + length;
		frame_received(opcode, json);
		if (discord.handle < 0)
			return;
	}
}

void p2p_discord_update(void)
{
	struct activity activity;
	int shown;

	if (!discord.checked)
	{
		const char *application = config_string("discord.application_id");

		discord.checked = 1;
		/* (an application's id is digits: it goes into a file's path and
		the handshake's JSON) */
		if (*application && strlen(application) < sizeof(discord.application) &&
			strspn(application, "0123456789") == strlen(application))
		{
			char scheme[48];

			strcpy(discord.application, application);
			discord.enabled = 1;
			discord.own_art = strcmp(application, OPENCE_APPLICATION) != 0;
			/* how Discord starts the game for an invite when it is not
			running; the game then receives the invite once connected */
			snprintf(scheme, sizeof(scheme), "discord-%s", application);
			/* (it lets go of the p2p lock while it may wait) */
			p2p_register_url_scheme(scheme, "GOAT H1");
		}
	}
	if (!discord.enabled)
		return;
	if (discord.handle < 0)
	{
		char handshake[128];
		int size;

		if (discord.attempted && !elapsed(discord.attempt_time, RETRY_INTERVAL))
			return;
		discord.attempted = 1;
		discord.attempt_time = p2p_now();
		discord.handle = posix_discord_connect(config_string("debug.discord_socket"));
		if (discord.handle < 0)
			return;
		size = snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}", discord.application);
		discord_send(_opcode_handshake, handshake, size);
		if (discord.handle < 0)
			return;
	}
	discord_flush();
	if (discord.handle < 0)
		return;
	discord_read();
	if (!discord.ready)
		return;
	requests_update(discord.shown && discord.sent.secret[0] && config_boolean("discord.rich_presence") &&
		config_boolean("discord.allow_join_requests"));
	if (discord.handle < 0)
		return;
	shown = compose(&activity);
	if ((!discord.sent_any || shown != discord.shown || memcmp(&activity, &discord.sent, sizeof(activity))) &&
		(!discord.sent_any || elapsed(discord.sent_time, ACTIVITY_INTERVAL)))
	{
		send_activity(&activity, shown);
		discord.sent = activity;
		discord.shown = shown;
		discord.sent_any = 1;
		discord.sent_time = p2p_now();
	}
	/* (the spare's topic listened on while Discord has its link) */
	p2p_party_standby(discord.shown && discord.sent.spare);
}

void p2p_discord_set_hosting(const char *secret, int player_count, int maximum_player_count)
{
	discord.hosting = secret != NULL;
	snprintf(discord.secret, sizeof(discord.secret), "%s", secret ? secret : "");
	discord.player_count = player_count;
	discord.maximum_player_count = maximum_player_count;
}

/* ---------- the game's (p2p.h): they take p2p_lock */

void p2p_discord_set_game(int hosting, int playing, const char *description, const char *invite, int player_count,
	int maximum_player_count)
{
	unsigned char hash[P2P_KEY_HASH_SIZE], token[P2P_TOKEN_SIZE];
	unsigned char bytes[P2P_KEY_HASH_SIZE + P2P_TOKEN_SIZE];
	char code[CODE_SIZE] = "";

	if (invite && p2p_parse_invite(invite, hash, token) > 0)
	{
		memcpy(bytes, hash, P2P_KEY_HASH_SIZE);
		memcpy(bytes + P2P_KEY_HASH_SIZE, token, P2P_TOKEN_SIZE);
		p2p_hex(bytes, sizeof(bytes), code);
	}
	pthread_mutex_lock(&p2p_lock);
	discord.game.hosting = hosting != 0;
	discord.game.playing = playing != 0;
	snprintf(discord.game.description, sizeof(discord.game.description), "%s", description ? description : "");
	memcpy(discord.game.code, code, sizeof(code));
	discord.game.player_count = player_count;
	discord.game.maximum_player_count = maximum_player_count;
	pthread_mutex_unlock(&p2p_lock);
}

void p2p_discord_game(int *hosting, int *playing, char *description, int size, int *player_count,
	int *maximum_player_count)
{
	*hosting = discord.game.hosting;
	*playing = discord.game.playing;
	snprintf(description, (size_t)size, "%s", discord.game.description);
	*player_count = discord.game.player_count;
	*maximum_player_count = discord.game.maximum_player_count;
}

unsigned long p2p_discord_join_request(char *name, int size, int *game)
{
	unsigned long serial = 0;
	int index;

	pthread_mutex_lock(&p2p_lock);
	if (game)
	{
		*game = discord.sent.secret[0] &&
			strncmp(discord.sent.secret, P2P_PARTY_LINK_PREFIX, strlen(P2P_PARTY_LINK_PREFIX)) != 0;
	}
	for (index = 0; index < MAXIMUM_REQUESTS && !serial; index++)
	{
		struct request const *request = &discord.requests[index];

		if (request->serial && request->answer == _answer_none && !elapsed(request->time, REQUEST_TIMEOUT))
		{
			serial = request->serial;
			snprintf(name, (size_t)size, "%s", request->name);
		}
	}
	pthread_mutex_unlock(&p2p_lock);
	return serial;
}

void p2p_discord_answer(unsigned long serial, int accept)
{
	int index;

	pthread_mutex_lock(&p2p_lock);
	for (index = 0; index < MAXIMUM_REQUESTS; index++)
	{
		struct request *request = &discord.requests[index];

		if (serial && request->serial == serial && request->answer == _answer_none)
			request->answer = accept ? _answer_accept : _answer_decline;
	}
	pthread_mutex_unlock(&p2p_lock);
}
