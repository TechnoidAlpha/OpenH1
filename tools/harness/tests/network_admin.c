/*
NETWORK_ADMIN.C (test)

A dedicated server's admins in the game (port/linux/game/network_admin.c):
whether a proven key is an admin's (admins.txt's and server.admins' keys
and key ids), what each request of an admin's runs as the server's command
and the requests refused (another player in the slot, out of range, a text
that would be more than one word), and how many requests a machine may make.
test_network_admin.py takes the header and the code (under_test.inc).
*/

#include "harness.h"

#include <stddef.h>

typedef unsigned short word;

#define MIN(a, b) ((a) < (b) ? (a) : (b))

#include "network_admin.h"

#include "under_test.inc"

static char const key[] = "3f9a2c7d1e5b8a04aabbccddeeff00112233445566778899aabbccddeeff0011";
static char const key_id[] = "1234abcd5678ef90";

/* ---------- the cases */

static void case_listed(void)
{
	CHECK(network_admin_listed(key, key_id, key), "the key alone");
	CHECK(network_admin_listed(key, key_id, "3F9A2C7D1E5B8A04AABBCCDDEEFF00112233445566778899AABBCCDDEEFF0011 sam"),
		"the key in capitals, with a name");
	CHECK(network_admin_listed(key, key_id, "# admins\nffff000011112222, 1234ABCD5678EF90\n"), "its id, second");
	CHECK(!network_admin_listed(key, key_id, ""), "an empty list");
	CHECK(!network_admin_listed(key, key_id, "# 1234abcd5678ef90\n"), "a comment's id");
	CHECK(!network_admin_listed(key, key_id, "1234abcd5678ef9"), "an id cut short");
	CHECK(!network_admin_listed(key, key_id, "1234abcd5678ef900"), "an id run on");
	CHECK(!network_admin_listed(key, key_id, "x1234abcd5678ef90"), "an id inside a word");
	CHECK(!network_admin_listed(key, key_id, "3f9a2c7d1e5b8a04"), "the key's first 16 digits are no id");
	CHECK(!network_admin_listed(key, key_id,
		"3f9a2c7d1e5b8a04aabbccddeeff00112233445566778899aabbccddeeff0012"), "another key");
	CHECK(!network_admin_listed("", "", "0000000000000000"), "no key proven");
}

static void request(struct network_admin_request *request, short verb, short slot, char const *name, long number,
	char const *text)
{
	memset(request, 0, sizeof(*request));
	request->verb = verb;
	request->slot = slot;
	request->number = number;
	snprintf(request->name, sizeof(request->name), "%s", name ? name : "");
	snprintf(request->text, sizeof(request->text), "%s", text ? text : "");
}

/* the line the request runs, else "refused: why" */
static char const *line_of(struct network_admin_request const *request, char const *slot_name, char const *current)
{
	static char line[600];
	char why[200];

	if (!network_admin_request_line(request, slot_name, current, line, sizeof(line), why, sizeof(why)))
		snprintf(line, sizeof(line), "refused: %s", why);
	return line;
}

static void case_request_lines(void)
{
	struct network_admin_request r;

	request(&r, _network_admin_kick, 3, "Milenko", 0, NULL);
	CHECK(!strcmp(line_of(&r, "Milenko", NULL), "kick 3"), "%s", line_of(&r, "Milenko", NULL));
	request(&r, _network_admin_ban, 3, "Milenko", 3600, "camping \"spawn\"");
	CHECK(!strcmp(line_of(&r, "Milenko", NULL), "ban 3 3600s camping spawn"), "%s", line_of(&r, "Milenko", NULL));
	request(&r, _network_admin_ban, 3, "Milenko", 0, "");
	CHECK(!strcmp(line_of(&r, "Milenko", NULL), "ban 3 forever banned by an admin"), "%s",
		line_of(&r, "Milenko", NULL));
	request(&r, _network_admin_mute, 3, "Milenko", 60, NULL);
	CHECK(!strcmp(line_of(&r, "Milenko", NULL), "mute 3 60"), "%s", line_of(&r, "Milenko", NULL));
	request(&r, _network_admin_unmute, 3, "Milenko", 0, NULL);
	CHECK(!strcmp(line_of(&r, "Milenko", NULL), "unmute 3"), "%s", line_of(&r, "Milenko", NULL));
	request(&r, _network_admin_move_team, 3, "Milenko", 0, NULL);
	CHECK(!strcmp(line_of(&r, "Milenko", NULL), "sv_team 3"), "%s", line_of(&r, "Milenko", NULL));
	request(&r, _network_admin_end_game, NONE, NULL, 0, NULL);
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_end_game"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_restart, NONE, NULL, 0, NULL);
	CHECK(!strcmp(line_of(&r, NULL, "bloodgulch:ctf"), "sv_map bloodgulch:ctf"), "%s",
		line_of(&r, NULL, "bloodgulch:ctf"));
	request(&r, _network_admin_shuffle_teams, NONE, NULL, 0, NULL);
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_teams shuffle"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_balance_teams, NONE, NULL, 0, NULL);
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_teams balance"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_lock_teams, NONE, NULL, 1, NULL);
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_lock_teams 1"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_announce, NONE, NULL, 0, "  hello\tall  ");
	CHECK(!strcmp(line_of(&r, NULL, NULL), "say helloall"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_map_now, NONE, NULL, 0, "sidewinder:ctf+score=5");
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_map sidewinder:ctf+score=5"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_map_after, NONE, NULL, 0, "sidewinder");
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_map_after sidewinder"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_rotation_add, NONE, NULL, 0, "hangemhigh:slayer");
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_rotation_add hangemhigh:slayer"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_rotation_remove, NONE, NULL, 2, NULL);
	CHECK(!strcmp(line_of(&r, NULL, NULL), "sv_rotation_del 2"), "%s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_unban, NONE, NULL, 7, NULL);
	CHECK(!strcmp(line_of(&r, NULL, NULL), "unban 7"), "%s", line_of(&r, NULL, NULL));
}

static void case_requests_refused(void)
{
	struct network_admin_request r;
	short verb;

	/* (another player in the slot now, or none) */
	request(&r, _network_admin_kick, 3, "Milenko", 0, NULL);
	CHECK(!strncmp(line_of(&r, "Sam", NULL), "refused", 7), "another's slot: %s", line_of(&r, "Sam", NULL));
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "an empty slot");
	request(&r, _network_admin_ban, NONE, "Milenko", 0, NULL);
	CHECK(!strncmp(line_of(&r, "Milenko", NULL), "refused", 7), "no slot");
	request(&r, _network_admin_ban, 200, "Milenko", 0, NULL);
	CHECK(!strncmp(line_of(&r, "Milenko", NULL), "refused", 7), "a slot past the last");
	/* (numbers out of range) */
	request(&r, _network_admin_ban, 3, "Milenko", -5, NULL);
	CHECK(!strncmp(line_of(&r, "Milenko", NULL), "refused", 7), "a ban's length below none");
	request(&r, _network_admin_ban, 3, "Milenko", NETWORK_ADMIN_BAN_MOST_SECONDS + 1, NULL);
	CHECK(!strncmp(line_of(&r, "Milenko", NULL), "refused", 7), "a ban past ten years");
	request(&r, _network_admin_mute, 3, "Milenko", 0, NULL);
	CHECK(!strncmp(line_of(&r, "Milenko", NULL), "refused", 7), "a mute of no minutes");
	request(&r, _network_admin_mute, 3, "Milenko", NETWORK_ADMIN_MUTE_MOST_MINUTES + 1, NULL);
	CHECK(!strncmp(line_of(&r, "Milenko", NULL), "refused", 7), "a mute past a day");
	request(&r, _network_admin_lock_teams, NONE, NULL, 2, NULL);
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "a lock of 2");
	request(&r, _network_admin_rotation_remove, NONE, NULL, NETWORK_ADMIN_ROTATION_MOST, NULL);
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "an entry past the rotation's most");
	request(&r, _network_admin_unban, NONE, NULL, -1, NULL);
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "a ban below the first");
	/* (an entry is one word: no second command, no other argument) */
	request(&r, _network_admin_map_now, NONE, NULL, 0, "bloodgulch; quit");
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "an entry of two words: %s", line_of(&r, NULL, NULL));
	request(&r, _network_admin_rotation_add, NONE, NULL, 0, "\"\"");
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "an empty entry");
	request(&r, _network_admin_restart, NONE, NULL, 0, NULL);
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "a restart with no game");
	request(&r, _network_admin_announce, NONE, NULL, 0, "\x01\x02");
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "an announcement of nothing printable");
	/* (no verb but the admin's: the lists are no command, and past the last
	none is one) */
	request(&r, _network_admin_lists, NONE, NULL, 0, NULL);
	CHECK(!strncmp(line_of(&r, NULL, NULL), "refused", 7), "the lists as a command");
	for (verb = NUMBER_OF_NETWORK_ADMIN_REQUESTS; verb < 300; verb++)
	{
		request(&r, verb, 3, "Milenko", 0, "x");
		CHECK(!strncmp(line_of(&r, "Milenko", "x"), "refused", 7), "verb %d", verb);
	}
	request(&r, -1, 3, "Milenko", 0, "x");
	CHECK(!strncmp(line_of(&r, "Milenko", "x"), "refused", 7), "verb -1");
	/* (no line past its size) */
	{
		char small[8], why[64];

		request(&r, _network_admin_announce, NONE, NULL, 0, "a long announcement");
		CHECK(network_admin_request_line(&r, NULL, NULL, small, sizeof(small), why, sizeof(why)) &&
			strlen(small) < sizeof(small), "the line kept to its size");
	}
}

static void case_rate(void)
{
	struct network_admin_rate rate;
	unsigned long now = 5000;
	long taken = 0, index;

	memset(&rate, 0, sizeof(rate));
	/* (a burst at once, then no more) */
	for (index = 0; index < 50; index++)
		taken += network_admin_rate_take(&rate, now);
	CHECK(taken == NETWORK_ADMIN_REQUEST_BURST, "%ld taken at once", taken);
	/* (then so many a second) */
	taken = 0;
	for (index = 0; index < 10000; index += 10)
		taken += network_admin_rate_take(&rate, now + 1 + index);
	CHECK(taken >= NETWORK_ADMIN_REQUESTS_PER_SECOND * 10 - 1 && taken <= NETWORK_ADMIN_REQUESTS_PER_SECOND * 10 + 1,
		"%ld taken in ten seconds", taken);
	/* (a quiet minute earns the burst, no more) */
	taken = 0;
	for (index = 0; index < 50; index++)
		taken += network_admin_rate_take(&rate, now + 70000);
	CHECK(taken == NETWORK_ADMIN_REQUEST_BURST, "%ld taken after a minute", taken);
	/* (the clock's wrapping round) */
	memset(&rate, 0, sizeof(rate));
	taken = 0;
	for (index = 0; index < 20; index++)
		taken += network_admin_rate_take(&rate, 0xFFFFFFF0UL + (unsigned long)index);
	CHECK(taken == NETWORK_ADMIN_REQUEST_BURST, "%ld taken over the clock's wrap", taken);
}

int main(int argc, char **argv)
{
	static const struct
	{
		const char *name;
		void (*run)(void);
	} cases[] =
	{
		{ "listed", case_listed },
		{ "request-lines", case_request_lines },
		{ "requests-refused", case_requests_refused },
		{ "rate", case_rate },
	};
	int index;

	for (index = 0; argc > 1 && index < (int)(sizeof(cases) / sizeof(cases[0])); index++)
	{
		if (!strcmp(argv[1], cases[index].name))
		{
			cases[index].run();
			return 0;
		}
	}
	fprintf(stderr, "no such case\n");
	return 2;
}
