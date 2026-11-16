/*
DISCORD_RPC.C

Discord's rich presence, invites and asks to join (port/linux/src/
p2p_discord.c, whole: under_test.inc) against a fake Discord client: a peer
in this process in place of the desktop client's socket, which reads the
game's frames and sends it Discord's (READY, ACTIVITY_JOIN,
ACTIVITY_JOIN_REQUEST). The party and the rest of internet play are stand-
ins that say what was asked of them. test_discord_rpc.py takes the code and
the invite parsing it uses (p2p_parts.inc: p2p.c's) from the sources. Each
case is run by its name:

- handshake: the socket debug.discord_socket names, the handshake with the
  application, READY's subscriptions, then the menus' activity: "In the
  menus", the spare party's id, size and link as the join secret, its topic
  listened on once Discord has it; no application, no connection;
- states: the activity in a party (its id, size and link), hosting (what the
  game is, its lobby or play, its players and invite), in a party while
  hosting (the party's link), a game joined (its invite, as its code), a LAN
  game (no secret, no spare), its text escaped as JSON; one at most each
  four seconds, the latest;
- art: "party" and "host" beside the logo in an application of OpenH1's
  own, the logo alone in OpenCE's;
- join: ACTIVITY_JOIN's secret, a party's link or a game's code, taken as a
  link opened;
- accept: a friend's ask is shown (their name, a party's), and let in: the
  spare party made, SEND_ACTIVITY_JOIN_INVITE with their id;
- decline: an ask declined: CLOSE_ACTIVITY_REQUEST, no invite, no party;
- timeout: an ask not answered is declined after 30 seconds;
- game: hosting, an ask is to join the game: let in without a party made;
- off: discord.rich_presence false: the activity cleared, no secret, no
  spare, an ask declined at once and never shown;
- nojoins: discord.allow_join_requests false: the activity without party or
  secret, an ask declined at once;
- reconnect: Discord gone: the asks forgotten, the topic no longer listened
  on; back, the activity sent again.
*/

#include "harness.h"

#include <stdarg.h>

/* (platform.h's, which p2p_discord.c takes only this from) */
void platform_log(const char *format, ...);

#include "under_test.inc"
#include "p2p_parts.inc"

/* ---------- the fake world */

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

#define SPARE_LINK "openh1://party/5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
#define SPARE_ID "c0ffeec0ffeec0ffeec0ffeec0ffee00"
#define PARTY_LINK "openh1://party/00112233445566778899aabbccddeeff"
#define PARTY_ID "abcdefabcdefabcdefabcdefabcdef12"
/* a game's code: its host's key hash, then its token */
#define CODE "0123456789abcdef0123456789abcdef00112233445566778899aabbccddeeff"
#define OTHER_CODE "fedcba9876543210fedcba9876543210ffeeddccbbaa99887766554433221100"
#define OPENCE "1553978809840050229"

static unsigned long now = 100000;
static char application[32] = OPENCE;
static boolean rich_presence = TRUE, allow_joins = TRUE;
static char last_log[256];

/* the party (p2p_party.c), and what was asked of it */
static struct p2p_party_presence party_now;
static int spare_asked, spare_led, standby;
/* the links taken (p2p_invite_received) */
static char received[8][160];
static int received_count;

void platform_log(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(last_log, sizeof(last_log), format, arguments);
	va_end(arguments);
}

const char *config_string(const char *name)
{
	if (!strcmp(name, "discord.application_id"))
		return application;
	if (!strcmp(name, "debug.discord_socket"))
		return "/scratch/fake-discord-ipc";
	return "";
}

int config_boolean(const char *name)
{
	if (!strcmp(name, "discord.rich_presence"))
		return rich_presence;
	if (!strcmp(name, "discord.allow_join_requests"))
		return allow_joins;
	return 0;
}

unsigned long p2p_now(void)
{
	return now;
}

posix_ulong posix_process_id(void)
{
	return 4242;
}

void p2p_register_url_scheme(const char *scheme, const char *description)
{
	(void)scheme;
	(void)description;
}

void p2p_invite_received(const char *text)
{
	if (received_count < 8)
		snprintf(received[received_count++], sizeof(received[0]), "%s", text);
}

void p2p_party_presence(struct p2p_party_presence *presence)
{
	*presence = party_now;
}

void p2p_party_spare(char *link, int link_size, char *id, int id_size)
{
	spare_asked++;
	snprintf(link, (size_t)link_size, "%s", SPARE_LINK);
	snprintf(id, (size_t)id_size, "%s", SPARE_ID);
}

void p2p_party_spare_lead(void)
{
	spare_led++;
	party_now.active = 1;
	party_now.count = 1;
	snprintf(party_now.link, sizeof(party_now.link), "%s", SPARE_LINK);
	snprintf(party_now.id, sizeof(party_now.id), "%s", SPARE_ID);
}

void p2p_party_standby(int on)
{
	standby = on;
}

/* ---------- the fake Discord client */

enum
{
	PEER_HANDLE = 7,
	MAXIMUM_FRAMES = 64,
};

static boolean peer_running = TRUE, peer_open;
static int connects;
static char connected_path[256];
static unsigned char to_game[65536];
static int to_game_size, to_game_read;
static unsigned char from_game[65536];
static int from_game_size, from_game_parsed;
/* the frames the game sent */
static struct
{
	int opcode;
	char json[2100];
} frames[MAXIMUM_FRAMES];
static int frame_count;

int posix_discord_connect(const char *path)
{
	connects++;
	snprintf(connected_path, sizeof(connected_path), "%s", path ? path : "");
	if (!peer_running)
		return -1;
	peer_open = TRUE;
	to_game_size = to_game_read = 0;
	from_game_size = from_game_parsed = 0;
	return PEER_HANDLE;
}

int posix_discord_write(int handle, const void *buffer, int length)
{
	if (handle != PEER_HANDLE || !peer_open || from_game_size + length > (int)sizeof(from_game))
		return -1;
	memcpy(from_game + from_game_size, buffer, (size_t)length);
	from_game_size += length;
	return length;
}

int posix_discord_read(int handle, void *buffer, int length)
{
	int count = to_game_size - to_game_read;

	if (handle != PEER_HANDLE || !peer_open)
		return -1;
	if (count > length)
		count = length;
	memcpy(buffer, to_game + to_game_read, (size_t)count);
	to_game_read += count;
	return count;
}

void posix_discord_close(int handle)
{
	(void)handle;
	peer_open = FALSE;
}

/* a frame from Discord to the game */
static void peer_send(int opcode, const char *json)
{
	int size = (int)strlen(json);
	unsigned char *frame = to_game + to_game_size;

	frame[0] = (unsigned char)opcode;
	frame[1] = frame[2] = frame[3] = 0;
	frame[4] = (unsigned char)size;
	frame[5] = (unsigned char)(size >> 8);
	frame[6] = frame[7] = 0;
	memcpy(frame + 8, json, (size_t)size);
	to_game_size += 8 + size;
}

static void frames_parse(void)
{
	while (from_game_size - from_game_parsed >= 8)
	{
		unsigned char *frame = from_game + from_game_parsed;
		int size = frame[4] | frame[5] << 8 | frame[6] << 16;

		CHECK(frame[1] == 0 && frame[2] == 0 && frame[3] == 0 && frame[7] == 0, "a frame's header is little-endian");
		if (from_game_size - from_game_parsed < 8 + size)
			break;
		CHECK(frame_count < MAXIMUM_FRAMES && size < (int)sizeof(frames[0].json), "frames kept");
		frames[frame_count].opcode = frame[0];
		memcpy(frames[frame_count].json, frame + 8, (size_t)size);
		frames[frame_count].json[size] = 0;
		frame_count++;
		from_game_parsed += 8 + size;
	}
}

/* the number of the game's frames of a command (and, if not NULL, holding
text), and the last one's JSON */
static int sent(const char *command, const char *text, const char **json)
{
	char pattern[64];
	int index, count = 0;

	snprintf(pattern, sizeof(pattern), "\"cmd\":\"%s\"", command);
	for (index = 0; index < frame_count; index++)
	{
		if (strstr(frames[index].json, pattern) && (!text || strstr(frames[index].json, text)))
		{
			count++;
			if (json)
				*json = frames[index].json;
		}
	}
	return count;
}

static const char *last_activity(void)
{
	const char *json = NULL;

	sent("SET_ACTIVITY", NULL, &json);
	CHECK(json != NULL, "an activity was sent");
	return json;
}

static boolean has(const char *json, const char *text)
{
	return strstr(json, text) != NULL;
}

/* the p2p thread's pass */
static void pass(void)
{
	p2p_discord_update();
	frames_parse();
}

/* time passing, a pass each 50 ms */
static void run(unsigned long milliseconds)
{
	unsigned long end = now + milliseconds;

	while (now < end)
	{
		now += 50;
		pass();
	}
}

/* connected, Discord READY, its first activity sent */
static void ready(void)
{
	pass();
	CHECK(connects == 1 && peer_open, "connected to Discord");
	peer_send(1, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1,\"config\":{},\"user\":{\"id\":\"111\",\"username\":\"cortana\"}},"
		"\"evt\":\"READY\",\"nonce\":null}");
	pass();
}

static void ask(const char *id, const char *global_name)
{
	char json[512];

	snprintf(json, sizeof(json), "{\"cmd\":\"DISPATCH\",\"data\":{\"user\":{\"id\":\"%s\",\"username\":\"friend\","
		"\"discriminator\":\"0\",\"global_name\":%s,\"avatar\":null}},\"evt\":\"ACTIVITY_JOIN_REQUEST\","
		"\"nonce\":null}", id, global_name);
	peer_send(1, json);
	pass();
}

static void in_party(int count)
{
	party_now.active = 1;
	party_now.count = count;
	snprintf(party_now.link, sizeof(party_now.link), "%s", PARTY_LINK);
	snprintf(party_now.id, sizeof(party_now.id), "%s", PARTY_ID);
}

/* ---------- the cases */

static void case_handshake(void)
{
	const char *json;

	ready();
	CHECK(!strcmp(connected_path, "/scratch/fake-discord-ipc"), "the socket debug.discord_socket names, alone");
	CHECK(frame_count >= 1 && frames[0].opcode == 0 && has(frames[0].json, "\"client_id\":\"" OPENCE "\""),
		"the handshake names the application: %s", frame_count ? frames[0].json : "");
	CHECK(sent("SUBSCRIBE", "\"evt\":\"ACTIVITY_JOIN\"", NULL) == 1, "ACTIVITY_JOIN subscribed");
	CHECK(sent("SUBSCRIBE", "\"evt\":\"ACTIVITY_JOIN_REQUEST\"", NULL) == 1, "ACTIVITY_JOIN_REQUEST subscribed");
	json = last_activity();
	CHECK(has(json, "\"pid\":4242") && has(json, "\"details\":\"In the menus\"") && !has(json, "\"state\""),
		"the menus: %s", json);
	CHECK(has(json, "\"large_image\":\"logo\",\"large_text\":\"GOAT H1\"") && !has(json, "small_image"),
		"the logo alone, in OpenCE's application: %s", json);
	CHECK(has(json, "\"party\":{\"id\":\"" SPARE_ID "\",\"size\":[1,16]}") &&
		has(json, "\"secrets\":{\"join\":\"" SPARE_LINK "\"}"), "the spare party's id, size and link: %s", json);
	CHECK(spare_asked > 0 && !spare_led, "the spare asked for, not made");
	CHECK(standby, "the spare's topic listened on once Discord has its link");
	/* (no application: no connection) */
	{
		int before = connects;

		discord_close();
		discord.checked = 0;
		discord.enabled = 0;
		application[0] = 0;
		run(30000);
		CHECK(connects == before, "no application, no connection");
	}
}

static void case_states(void)
{
	const char *json;
	char hosted[160];

	ready();
	/* (a party: its id, its size, its link) */
	in_party(3);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"details\":\"In the menus\",\"state\":\"In a party\"") &&
		has(json, "\"party\":{\"id\":\"" PARTY_ID "\",\"size\":[3,16]}") &&
		has(json, "\"secrets\":{\"join\":\"" PARTY_LINK "\"}"), "in a party: %s", json);
	CHECK(!standby, "in a party, the spare's topic is not listened on");
	/* (hosting, out of a party: what the game is, its lobby, its players,
	its invite) */
	memset(&party_now, 0, sizeof(party_now));
	p2p_discord_set_hosting(CODE, 5, 16);
	p2p_discord_set_game(1, 0, "OpenH1 120 Tick \xC2\xB7 Slayer on Blood Gulch", NULL, 5, 16);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"details\":\"Hosting GOAT H1 120 Tick \xC2\xB7 Slayer on Blood Gulch\",\"state\":\"In the lobby\""),
		"hosting, in the lobby: %s", json);
	CHECK(has(json, "\"party\":{\"id\":\"0123456789abcdef0123456789abcdef\",\"size\":[5,16]}") &&
		has(json, "\"secrets\":{\"join\":\"" CODE "\"}"), "the game's players and invite: %s", json);
	snprintf(hosted, sizeof(hosted), "%s", json);
	p2p_discord_set_game(1, 1, "OpenH1 120 Tick \xC2\xB7 Slayer on Blood Gulch", NULL, 5, 16);
	run(4000);
	CHECK(has(last_activity(), "\"state\":\"Playing\""), "hosting, playing: %s", last_activity());
	/* (hosting in a party: the party's link) */
	in_party(2);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"details\":\"Hosting GOAT H1") && has(json, "\"state\":\"In a party\"") &&
		has(json, "\"secrets\":{\"join\":\"" PARTY_LINK "\"}") && !has(json, CODE),
		"hosting in a party, the party's link: %s", json);
	/* (a game joined: its invite, as its code) */
	memset(&party_now, 0, sizeof(party_now));
	p2p_discord_set_hosting(NULL, 0, 0);
	p2p_discord_set_game(0, 1, "Legacy \xC2\xB7 CTF on Hang 'Em High", "join me: openh1://join/" OTHER_CODE, 3, 8);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"details\":\"Legacy \xC2\xB7 CTF on Hang 'Em High\",\"state\":\"Playing\"") &&
		has(json, "\"size\":[3,8]") && has(json, "\"secrets\":{\"join\":\"" OTHER_CODE "\"}"),
		"a game joined: %s", json);
	/* (a LAN game: no secret, and no spare) */
	spare_asked = 0;
	p2p_discord_set_game(0, 1, "OpenH1 60 Tick \xC2\xB7 Slayer on Prisoner", NULL, 4, 16);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"state\":\"Playing\"") && !has(json, "secrets") && !has(json, "\"party\"") && !spare_asked &&
		!standby, "a LAN game: %s", json);
	/* (its text as JSON: quotes, backslashes and controls escaped, what is
	not UTF-8 a "?") */
	p2p_discord_set_game(0, 0, "A \"name\" \\ with\x01 \xFF\xC2", NULL, 0, 0);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"details\":\"A \\\"name\\\" \\\\ with\\u0001 ??\""), "escaped: %s", json);
	/* (one each four seconds at most, the latest) */
	{
		int before = sent("SET_ACTIVITY", NULL, NULL);

		p2p_discord_set_game(0, 0, "First", NULL, 0, 0);
		run(1000);
		p2p_discord_set_game(0, 0, "Second", NULL, 0, 0);
		run(1000);
		CHECK(sent("SET_ACTIVITY", NULL, NULL) <= before + 1, "two changes within four seconds: one at most sent");
		run(4000);
		CHECK(has(last_activity(), "\"details\":\"Second\""), "then the latest: %s", last_activity());
		before = sent("SET_ACTIVITY", NULL, NULL);
		run(10000);
		CHECK(sent("SET_ACTIVITY", NULL, NULL) == before, "unchanged, not sent again");
	}
	(void)hosted;
}

static void case_art(void)
{
	const char *json;

	snprintf(application, sizeof(application), "%s", "1700000000000000001");
	ready();
	CHECK(has(frames[0].json, "\"client_id\":\"1700000000000000001\""), "the application configured");
	in_party(2);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"large_image\":\"logo\"") && has(json, "\"small_image\":\"party\",\"small_text\":\"In a party\""),
		"in a party, OpenH1's own art: %s", json);
	memset(&party_now, 0, sizeof(party_now));
	p2p_discord_set_hosting(CODE, 1, 16);
	p2p_discord_set_game(1, 0, "OpenH1 120 Tick \xC2\xB7 Slayer on Blood Gulch", NULL, 1, 16);
	run(4000);
	CHECK(has(last_activity(), "\"small_image\":\"host\",\"small_text\":\"Hosting\""), "hosting: %s", last_activity());
}

static void case_join(void)
{
	ready();
	peer_send(1, "{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"" PARTY_LINK "\"},\"evt\":\"ACTIVITY_JOIN\"}");
	pass();
	CHECK(received_count == 1 && !strcmp(received[0], PARTY_LINK), "a party's link taken as a link opened");
	peer_send(1, "{\"cmd\":\"DISPATCH\",\"data\":{\"secret\":\"" CODE "\"},\"evt\":\"ACTIVITY_JOIN\"}");
	pass();
	CHECK(received_count == 2 && !strcmp(received[1], CODE), "a game's code taken as a link opened");
}

static void case_accept(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	unsigned long serial;
	int game = -1;
	const char *json = NULL;

	ready();
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "no ask yet");
	ask("222", "\"Zo\\u00eb \\\"Z\\\"\"");
	serial = p2p_discord_join_request(name, sizeof(name), &game);
	CHECK(serial && !strcmp(name, "Zo\xEB \"Z\"") && game == 0, "the ask is shown, their name, a party's: %s",
		name);
	CHECK(!sent("SEND_ACTIVITY_JOIN_INVITE", NULL, NULL) && !sent("CLOSE_ACTIVITY_REQUEST", NULL, NULL),
		"not answered yet");
	run(5000);
	CHECK(p2p_discord_join_request(name, sizeof(name), NULL) == serial, "still asked");
	p2p_discord_answer(serial, TRUE);
	pass();
	CHECK(spare_led == 1, "let in: the spare party made");
	CHECK(sent("SEND_ACTIVITY_JOIN_INVITE", NULL, &json) == 1 && has(json, "\"args\":{\"user_id\":\"222\"}"),
		"the friend sent the invite: %s", json ? json : "");
	CHECK(!sent("CLOSE_ACTIVITY_REQUEST", NULL, NULL), "and not declined");
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "the ask is answered");
	run(4000);
	CHECK(has(last_activity(), "\"state\":\"In a party\"") && has(last_activity(), SPARE_LINK),
		"the party shown, its link the spare's: %s", last_activity());
}

static void case_decline(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	unsigned long serial;
	const char *json = NULL;

	ready();
	ask("333", "null");
	serial = p2p_discord_join_request(name, sizeof(name), NULL);
	CHECK(serial && !strcmp(name, "friend"), "no name shown: their user name: %s", name);
	p2p_discord_answer(serial, FALSE);
	pass();
	CHECK(sent("CLOSE_ACTIVITY_REQUEST", NULL, &json) == 1 && has(json, "\"user_id\":\"333\""),
		"declined: %s", json ? json : "");
	CHECK(!sent("SEND_ACTIVITY_JOIN_INVITE", NULL, NULL) && !spare_led, "no invite, no party");
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "the ask is answered");
	/* (an id that is not digits is no ask) */
	ask("x\"y", "\"Mallory\"");
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "an id of no digits is no ask");
}

static void case_timeout(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];

	ready();
	ask("444", "\"Johnson\"");
	CHECK(p2p_discord_join_request(name, sizeof(name), NULL), "asked");
	run(29000);
	CHECK(p2p_discord_join_request(name, sizeof(name), NULL) && !sent("CLOSE_ACTIVITY_REQUEST", NULL, NULL),
		"shown for thirty seconds");
	run(2000);
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "then no longer");
	CHECK(sent("CLOSE_ACTIVITY_REQUEST", "\"444\"", NULL) == 1 && !sent("SEND_ACTIVITY_JOIN_INVITE", NULL, NULL),
		"declined, not answered in time");
}

static void case_game(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	unsigned long serial;
	int game = -1;

	p2p_discord_set_hosting(CODE, 2, 16);
	p2p_discord_set_game(1, 0, "OpenH1 120 Tick \xC2\xB7 Slayer on Blood Gulch", NULL, 2, 16);
	ready();
	CHECK(has(last_activity(), "\"secrets\":{\"join\":\"" CODE "\"}"), "hosting: %s", last_activity());
	ask("555", "\"Keyes\"");
	serial = p2p_discord_join_request(name, sizeof(name), &game);
	CHECK(serial && game == 1, "the ask is to join the game");
	p2p_discord_answer(serial, TRUE);
	pass();
	CHECK(sent("SEND_ACTIVITY_JOIN_INVITE", "\"555\"", NULL) == 1 && !spare_led,
		"let into the game, no party made");
}

static void case_off(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	const char *json;

	rich_presence = FALSE;
	ready();
	json = last_activity();
	CHECK(!has(json, "\"activity\"") && !has(json, "secrets"), "no activity: %s", json);
	CHECK(!spare_asked && !standby, "no spare");
	ask("666", "\"Halsey\"");
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "an ask is not shown");
	CHECK(sent("CLOSE_ACTIVITY_REQUEST", "\"666\"", NULL) == 1 && !sent("SEND_ACTIVITY_JOIN_INVITE", NULL, NULL),
		"an ask is declined at once");
	/* (on again: shown) */
	rich_presence = TRUE;
	run(4000);
	CHECK(has(last_activity(), "\"details\":\"In the menus\""), "on again: %s", last_activity());
	/* (off again with an ask shown: cleared, the ask declined) */
	ask("667", "\"Halsey\"");
	CHECK(p2p_discord_join_request(name, sizeof(name), NULL), "asked");
	rich_presence = FALSE;
	run(4000);
	CHECK(!has(last_activity(), "\"activity\""), "off again: cleared");
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL) && sent("CLOSE_ACTIVITY_REQUEST", "\"667\"", NULL),
		"the ask shown declined");
	CHECK(!standby, "the spare's topic no longer listened on");
}

static void case_nojoins(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	const char *json;

	allow_joins = FALSE;
	ready();
	json = last_activity();
	CHECK(has(json, "\"details\":\"In the menus\"") && !has(json, "secrets") && !has(json, "\"party\"") &&
		!spare_asked && !standby, "status, but nothing to join: %s", json);
	in_party(3);
	run(4000);
	json = last_activity();
	CHECK(has(json, "\"state\":\"In a party\"") && !has(json, "secrets") && !has(json, PARTY_LINK),
		"a party without its link: %s", json);
	ask("777", "\"Johnson\"");
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "an ask is not shown");
	CHECK(sent("CLOSE_ACTIVITY_REQUEST", "\"777\"", NULL) == 1, "an ask is declined at once");
}

static void case_reconnect(void)
{
	char name[P2P_DISCORD_REQUEST_NAME_SIZE];
	int before;

	ready();
	CHECK(standby, "listening");
	ask("888", "\"Keyes\"");
	CHECK(p2p_discord_join_request(name, sizeof(name), NULL), "asked");
	peer_open = FALSE;
	peer_running = FALSE;
	pass();
	CHECK(!p2p_discord_join_request(name, sizeof(name), NULL), "Discord gone: the ask forgotten");
	CHECK(!standby, "Discord gone: the spare's topic no longer listened on");
	CHECK(has(last_log, "disconnected from Discord"), "said: %s", last_log);
	peer_running = TRUE;
	frame_count = 0;
	run(10000);
	CHECK(connects == 1, "not again at once");
	run(11000);
	CHECK(connects == 2 && peer_open, "again after a while");
	before = sent("SET_ACTIVITY", NULL, NULL);
	peer_send(1, "{\"cmd\":\"DISPATCH\",\"data\":{\"user\":{\"id\":\"111\",\"username\":\"cortana\"}},\"evt\":\"READY\"}");
	pass();
	CHECK(sent("SET_ACTIVITY", "\"In the menus\"", NULL) == before + 1, "the activity sent again");
}

static const struct
{
	const char *name;
	void (*run)(void);
} cases[] =
{
	{ "handshake", case_handshake },
	{ "states", case_states },
	{ "art", case_art },
	{ "join", case_join },
	{ "accept", case_accept },
	{ "decline", case_decline },
	{ "timeout", case_timeout },
	{ "game", case_game },
	{ "off", case_off },
	{ "nojoins", case_nojoins },
	{ "reconnect", case_reconnect },
};

int main(int argc, char **argv)
{
	int index;

	for (index = 0; argc > 1 && index < (int)(sizeof(cases) / sizeof(*cases)); index++)
	{
		if (!strcmp(argv[1], cases[index].name))
		{
			cases[index].run();
			return 0;
		}
	}
	fprintf(stderr, "no case %s\n", argc > 1 ? argv[1] : "");
	return 2;
}
