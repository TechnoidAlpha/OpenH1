/*
FRIENDS_MENU.C (test)

The Friends screen's words and prompts (port/linux/game/friends_menu.inc):
each row's note and whether it is green (a friend online, a request), the
prompts a row offers (JOIN only when a friend's presence offers its party or
its invite waits, and only with internet play on; ADD FRIEND only for a
player of a code who is no friend yet; never two of one key at once), the
panel's words, and the game's words decoded from UTF-8.
test_friends_menu.py takes the code (under_test.inc) and the definitions
(config.inc).
*/

#include "harness.h"

#include <stdarg.h>
#include <wchar.h>

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define SET_FLAG(flags, bit, value) ((value) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))

#include "config.inc"

/* (the game's, MSVC's: %s a wide string, %d an int) */
static int usnprintf(wchar_t *buffer, long size, wchar_t const *format, ...)
{
	va_list arguments;
	long length = 0;

	va_start(arguments, format);
	for (; *format && length < size - 1; format++)
	{
		if (format[0] == '%' && format[1] == 's')
		{
			wchar_t const *text = va_arg(arguments, wchar_t const *);

			while (*text && length < size - 1)
				buffer[length++] = *text++;
			format++;
		}
		else if (format[0] == '%' && format[1] == 'd')
		{
			char digits[16];
			short index;

			snprintf(digits, sizeof(digits), "%d", va_arg(arguments, int));
			for (index = 0; digits[index] && length < size - 1; index++)
				buffer[length++] = digits[index];
			format++;
		}
		else
			buffer[length++] = *format;
	}
	buffer[length] = 0;
	va_end(arguments);
	return (int)length;
}

/* browser_favorites.c's and p2p_friends.c's */
static void browser_favorite_last_seen_text(long long last_seen, long long now, char *text, size_t size)
{
	snprintf(text, size, "%lld min ago", (now - last_seen) / 60);
}

static int p2p_friend_code_format(const unsigned char *code, char *text, int size)
{
	snprintf(text, (size_t)size, "CODE-%02X%02X-%02X%02X", code[0], code[1], code[2], code[3]);
	return 1;
}

#include "under_test.inc"

/* ---------- the cases */

static struct p2p_friend entry_of(int kind)
{
	struct p2p_friend entry;

	memset(&entry, 0, sizeof(entry));
	entry.kind = kind;
	memset(entry.code, 0xAB, sizeof(entry.code));
	snprintf(entry.name, sizeof(entry.name), "Johnson");
	entry.has_code = 1;
	entry.relation = _p2p_friend_none;
	return entry;
}

static boolean note_is(struct p2p_friend const *entry, wchar_t const *expected, boolean live_expected)
{
	wchar_t text[64];
	boolean live = !live_expected;

	friends_row_note(entry, "K7QM-2XPA-9DTR", 10000, text, NUMBEROF(text), &live);
	return !wcscmp(text, expected) && live == live_expected;
}

static void case_notes(void)
{
	struct p2p_friend entry = entry_of(_p2p_friend_online);

	CHECK(note_is(&entry, L"Online", TRUE), "a friend online: green");
	entry.party_count = 3;
	CHECK(note_is(&entry, L"In a party", TRUE), "in a party");
	entry.place = _p2p_party_place_game;
	CHECK(note_is(&entry, L"In a game", TRUE), "in a game");
	entry.place = _p2p_party_place_joining;
	CHECK(note_is(&entry, L"Joining a game", TRUE), "joining one");
	entry.hosting = 1;
	CHECK(note_is(&entry, L"Hosting", TRUE), "hosting");
	entry.inviting = 1;
	CHECK(note_is(&entry, L"Invites you", TRUE), "its invite first");
	entry = entry_of(_p2p_friend_request);
	CHECK(note_is(&entry, L"Wants to be friends", TRUE), "a request: green");
	entry = entry_of(_p2p_friend_offline);
	CHECK(note_is(&entry, L"Offline", FALSE), "offline: not green");
	entry = entry_of(_p2p_friend_sent);
	CHECK(note_is(&entry, L"Request sent", FALSE), "a request sent");
	entry = entry_of(_p2p_friend_blocked);
	CHECK(note_is(&entry, L"Blocked", FALSE), "blocked");
	entry = entry_of(_p2p_friend_recent);
	entry.time = 10000 - 300;
	CHECK(note_is(&entry, L"Played 5 min ago", FALSE), "a recent player: when");
	entry.relation = _p2p_friend_online;
	CHECK(note_is(&entry, L"Friend", FALSE), "one a friend already");
	entry.relation = _p2p_friend_sent;
	CHECK(note_is(&entry, L"Request sent", FALSE), "one asked already");
	entry.has_code = 0;
	entry.opence = 1;
	CHECK(note_is(&entry, L"OpenCE player", FALSE), "an OpenCE player, shown as such");
	entry.opence = 0;
	CHECK(note_is(&entry, L"No friend code", FALSE), "one of no code");
	CHECK(note_is(NULL, L"K7QM-2XPA-9DTR", FALSE), "the own row: its code");
	{
		wchar_t text[32];
		boolean live;

		friends_row_note(NULL, "", 0, text, NUMBEROF(text), &live);
		CHECK(!wcscmp(text, L"NO PROFILE"), "no profile");
	}
}

/* the prompts of a key (its group: A, X, Y) a row offers at once */
static short prompts_of_key(unsigned long prompts, short first, short last)
{
	short count = 0;
	short prompt;

	for (prompt = first; prompt <= last; prompt++)
		count += TEST_FLAG(prompts, prompt);
	return count;
}

static void case_prompts(void)
{
	struct p2p_friend entry = entry_of(_p2p_friend_online);
	unsigned long prompts;
	int kind;

	prompts = friends_prompts_of(&entry, TRUE, TRUE, TRUE);
	CHECK(!TEST_FLAG(prompts, _friends_prompt_join), "no JOIN without a link to join by");
	CHECK(TEST_FLAG(prompts, _friends_prompt_invite) && TEST_FLAG(prompts, _friends_prompt_remove), "INVITE, REMOVE");
	entry.joinable = 1;
	CHECK(TEST_FLAG(friends_prompts_of(&entry, TRUE, TRUE, TRUE), _friends_prompt_join), "JOIN by its link");
	entry.joinable = 0;
	entry.inviting = 1;
	CHECK(TEST_FLAG(friends_prompts_of(&entry, TRUE, TRUE, TRUE), _friends_prompt_join), "JOIN its invite");
	prompts = friends_prompts_of(&entry, TRUE, TRUE, FALSE);
	CHECK(!TEST_FLAG(prompts, _friends_prompt_join) && !TEST_FLAG(prompts, _friends_prompt_invite),
		"no JOIN nor INVITE with internet play off");
	entry = entry_of(_p2p_friend_offline);
	prompts = friends_prompts_of(&entry, TRUE, TRUE, TRUE);
	CHECK(prompts == FLAG(_friends_prompt_remove), "a friend offline: REMOVE alone");
	entry = entry_of(_p2p_friend_request);
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == (FLAG(_friends_prompt_accept) |
		FLAG(_friends_prompt_ignore) | FLAG(_friends_prompt_block)), "a request: ACCEPT, IGNORE, BLOCK");
	entry = entry_of(_p2p_friend_sent);
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == FLAG(_friends_prompt_cancel), "a request sent: CANCEL");
	entry = entry_of(_p2p_friend_blocked);
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == FLAG(_friends_prompt_unblock), "blocked: UNBLOCK");
	entry = entry_of(_p2p_friend_recent);
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == (FLAG(_friends_prompt_add) | FLAG(_friends_prompt_block)),
		"a recent player of a code: ADD FRIEND, BLOCK");
	entry.relation = _p2p_friend_request;
	CHECK(TEST_FLAG(friends_prompts_of(&entry, TRUE, TRUE, TRUE), _friends_prompt_add),
		"one who asked: ADD FRIEND (accepts it)");
	entry.relation = _p2p_friend_online;
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == 0, "a friend already: nothing");
	entry.relation = _p2p_friend_sent;
	CHECK(!TEST_FLAG(friends_prompts_of(&entry, TRUE, TRUE, TRUE), _friends_prompt_add), "asked already: no ADD");
	entry.relation = _p2p_friend_blocked;
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == 0, "blocked: nothing here");
	entry = entry_of(_p2p_friend_recent);
	entry.has_code = 0;
	entry.opence = 1;
	CHECK(friends_prompts_of(&entry, TRUE, TRUE, TRUE) == 0, "an OpenCE player: nothing (no code)");
	CHECK(friends_prompts_of(NULL, TRUE, TRUE, TRUE) == (FLAG(_friends_prompt_copy) | FLAG(_friends_prompt_code) |
		FLAG(_friends_prompt_hide)), "the own row: COPY CODE, ADD BY CODE, APPEAR OFFLINE");
	CHECK(TEST_FLAG(friends_prompts_of(NULL, TRUE, FALSE, TRUE), _friends_prompt_show), "hidden: APPEAR ONLINE");
	CHECK(friends_prompts_of(NULL, FALSE, TRUE, TRUE) == 0, "no profile: nothing");
	/* (never two of a key at once: A's, X's, Y's) */
	for (kind = _p2p_friend_online; kind <= _p2p_friend_blocked; kind++)
	{
		short variant;

		for (variant = 0; variant < 4; variant++)
		{
			entry = entry_of(kind);
			entry.joinable = (unsigned char)(variant & 1);
			entry.inviting = (unsigned char)(variant >> 1);
			prompts = friends_prompts_of(&entry, TRUE, variant & 1, TRUE);
			CHECK(prompts_of_key(prompts, _friends_prompt_copy, _friends_prompt_unblock) <= 1, "one A: kind %d", kind);
			CHECK(prompts_of_key(prompts, _friends_prompt_code, _friends_prompt_cancel) <= 1, "one X: kind %d", kind);
			CHECK(prompts_of_key(prompts, _friends_prompt_remove, _friends_prompt_show) <= 1, "one Y: kind %d", kind);
		}
	}
}

static void case_panel(void)
{
	struct p2p_friend entry = entry_of(_p2p_friend_online);
	wchar_t kind[48], status[64], detail[160];

	entry.hosting = 1;
	entry.players = 5;
	entry.maximum_players = 16;
	/* (the game's words as Discord has them: UTF-8) */
	snprintf(entry.game, sizeof(entry.game), "OpenH1 120 Tick \xC2\xB7 Slayer");
	friends_panel_text(&entry, "K7QM", TRUE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(!wcscmp(status, L"Hosting a game"), "hosting");
	CHECK(!wcscmp(detail, L"GOAT H1 120 Tick \x00B7 Slayer (5 of 16)."), "its game, from UTF-8, and its players");
	entry.hosting = 0;
	entry.place = _p2p_party_place_menus;
	entry.party_count = 3;
	entry.game[0] = 0;
	entry.joinable = 1;
	friends_panel_text(&entry, "K7QM", TRUE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(!wcscmp(status, L"In a party of 3") && wcsstr(detail, L"JOIN"), "a party of 3, joinable");
	entry.inviting = 1;
	friends_panel_text(&entry, "K7QM", TRUE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(wcsstr(detail, L"Invites you"), "its invite told");
	entry = entry_of(_p2p_friend_request);
	friends_panel_text(&entry, "K7QM", TRUE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(!wcscmp(kind, L"FRIEND REQUEST") && wcsstr(detail, L"CODE-ABAB-ABAB"), "a request, with its code");
	entry = entry_of(_p2p_friend_recent);
	entry.has_code = 0;
	entry.opence = 1;
	friends_panel_text(&entry, "K7QM", TRUE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(wcsstr(detail, L"OpenCE has no friend codes"), "an OpenCE player can't be added, and says why");
	friends_panel_text(NULL, "K7QM", FALSE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(!wcscmp(status, L"You appear offline"), "the own row, hidden");
	friends_panel_text(NULL, "K7QM", TRUE, FALSE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(!wcscmp(status, L"Offline: internet play is off"), "internet play off");
	friends_panel_text(NULL, "", TRUE, TRUE, 0, kind, NUMBEROF(kind), status, NUMBEROF(status), detail,
		NUMBEROF(detail));
	CHECK(!wcscmp(status, L"No player profile"), "no profile");
	/* (UTF-8: a character cut short, and one past the 16 bits, are "?") */
	{
		wchar_t wide[16];

		friends_wide_utf8("a\xC2", wide, NUMBEROF(wide));
		CHECK(!wcscmp(wide, L"a?"), "a character cut short");
		friends_wide_utf8("\xF0\x9F\x98\x80!", wide, NUMBEROF(wide));
		CHECK(!wcscmp(wide, L"?!"), "one past 16 bits");
		friends_wide_utf8("\xE2\x80\xA2", wide, NUMBEROF(wide));
		CHECK(wide[0] == 0x2022 && !wide[1], "three bytes");
	}
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("notes") case_notes();
	else CASE("prompts") case_prompts();
	else CASE("panel") case_panel();
	else
	{
		fprintf(stderr, "no case %s\n", case_name);
		return 2;
	}
	printf("%s: passed\n", case_name);
	return 0;
}
