#include "harness.h"

#include <stdarg.h>
#include <wchar.h>

/* What a player is told when a network game cannot be joined
(port/linux/game/menu_functions.c's connect_failure_text: the connecting box
and the server browser's panel; source/networking/network_client_manager.c's
network_game_client_rejection_text: a host's refusal): the advice is for
the cause the game knows (internet play off, no internet, no answer from
the host, this network blocking, the host's, one between them; a phone told
to try Wi-Fi; the setting that asks the router named only when it is off),
and the words have no technical terms */

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
typedef unsigned short word;

/* the game's wide formatting: its %s a wide string */
static int usnprintf(wchar_t *text, size_t size, wchar_t const *format, ...)
{
	wchar_t wide_format[256];
	size_t from, to = 0;
	va_list arguments;
	int result;

	for (from = 0; format[from] && to < NUMBEROF(wide_format) - 2; from++)
	{
		wide_format[to++] = format[from];
		if (format[from] == '%' && format[from + 1] == '%')
			wide_format[to++] = format[++from];
		else if (format[from] == '%' && format[from + 1] == 's')
			wide_format[to++] = 'l';
	}
	wide_format[to] = 0;
	va_start(arguments, format);
	result = vswprintf(text, size + 1, wide_format, arguments);
	va_end(arguments);
	return result;
}

/* network.allow_upnp (OPEN ROUTER PORT) */
static boolean fake_upnp = TRUE;

static boolean config_boolean(char const *name)
{
	return !strcmp(name, "network.allow_upnp") && fake_upnp;
}

#include "config.inc"
#include "under_test.inc"

static wchar_t headline[CONNECT_HEADLINE_LENGTH], detail[CONNECT_DETAIL_LENGTH], line[CONNECT_LINE_LENGTH];

static void failed(int online, int brokers, int stage, int nat_strict, int host_strict, boolean listing,
	boolean on_phone)
{
	struct p2p_join_status status;

	memset(&status, 0, sizeof(status));
	status.online = online;
	status.brokers = brokers;
	status.stage = stage;
	status.nat_strict = nat_strict;
	status.host_strict = host_strict;
	connect_failure_text(&status, listing, on_phone, headline, detail, line);
}

static boolean contains(wchar_t const *text, wchar_t const *part)
{
	return wcsstr(text, part) != NULL;
}

/* none of the words only a network engineer knows (OPEN ROUTER PORT is
a setting's name, in capitals) */
static wchar_t const *jargon(wchar_t const *text)
{
	static wchar_t const *const words[] = { L"NAT", L"UDP", L"STUN", L"UPnP", L"broker", L"signal", L"Legacy",
		L"Tick", L"tick", L" port", L"strict", L"protocol", L"lockstep" };
	short index;

	for (index = 0; index < (short)NUMBEROF(words); index++)
	{
		if (contains(text, words[index]))
			return words[index];
	}
	return NULL;
}

/* each line of a text, after its kind */
static void print_lines(char kind, wchar_t const *text)
{
	printf("%c ", kind);
	for (; *text; text++)
	{
		if (*text == '\r')
			continue;
		if (*text == '\n')
			printf("\n%c ", kind);
		else
			putchar((int)*text);
	}
	putchar('\n');
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("cause")
	{
		failed(FALSE, FALSE, _p2p_join_idle, FALSE, FALSE, FALSE, FALSE);
		CHECK(contains(headline, L"INTERNET PLAY IS OFF") && contains(detail, L"Settings"), "off: %ls", detail);
		failed(TRUE, FALSE, _p2p_join_finding, FALSE, FALSE, FALSE, FALSE);
		CHECK(contains(detail, L"internet") && contains(line, L"internet connection"), "no internet: %ls", detail);
		failed(TRUE, TRUE, _p2p_join_finding, FALSE, FALSE, FALSE, FALSE);
		CHECK(contains(detail, L"may have ended") && contains(detail, L"new\r\ninvite link"),
			"an invite's host not found: %ls", detail);
		failed(TRUE, TRUE, _p2p_join_finding, FALSE, FALSE, TRUE, FALSE);
		CHECK(contains(detail, L"may have ended") && contains(detail, L"another game") &&
			!contains(detail, L"invite"), "a server browser's game not found: %ls", detail);
		/* this network blocks: on a phone, Wi-Fi instead of mobile data */
		failed(TRUE, TRUE, _p2p_join_opening, TRUE, FALSE, FALSE, TRUE);
		CHECK(contains(detail, L"Your network is blocking") && contains(detail, L"Wi-Fi") &&
			contains(detail, L"mobile data"), "a phone's network blocking: %ls", detail);
		CHECK(contains(line, L"Your network"), "the browser's line: %ls", line);
		/* on a computer, another network or another host, no mobile data */
		failed(TRUE, TRUE, _p2p_join_opening, TRUE, FALSE, FALSE, FALSE);
		CHECK(contains(detail, L"Your network is blocking") && contains(detail, L"another network") &&
			!contains(detail, L"mobile data"), "a computer's network blocking: %ls", detail);
		/* the setting that asks the router, when it is off */
		fake_upnp = FALSE;
		failed(TRUE, TRUE, _p2p_join_opening, TRUE, FALSE, FALSE, TRUE);
		CHECK(contains(detail, L"OPEN ROUTER PORT") && contains(detail, L"Turn on"),
			"OPEN ROUTER PORT off: %ls", detail);
		fake_upnp = TRUE;
		/* the host's network blocks: the host asked */
		failed(TRUE, TRUE, _p2p_join_opening, FALSE, TRUE, FALSE, FALSE);
		CHECK(contains(detail, L"host's network") && contains(detail, L"Ask the host"), "the host's: %ls", detail);
		/* neither known: one between them */
		failed(TRUE, TRUE, _p2p_join_opening, FALSE, FALSE, FALSE, FALSE);
		CHECK(contains(detail, L"A network is blocking") && !contains(detail, L"Your network"),
			"neither known: %ls", detail);
		return 0;
	}
	CASE("words")
	{
		int online, brokers, stage, nat, host, listing, phone;
		word code;

		for (online = 0; online < 2; online++)
		for (brokers = 0; brokers < 2; brokers++)
		for (stage = _p2p_join_idle; stage <= _p2p_join_reached; stage++)
		for (nat = 0; nat < 2; nat++)
		for (host = 0; host < 2; host++)
		for (listing = 0; listing < 2; listing++)
		for (phone = 0; phone < 2; phone++)
		{
			failed(online, brokers, stage, nat, host, (boolean)listing, (boolean)phone);
			CHECK(!jargon(headline) && !jargon(detail) && !jargon(line), "\"%ls\" in %ls / %ls / %ls",
				jargon(headline) ? jargon(headline) : jargon(detail) ? jargon(detail) : jargon(line), headline,
				detail, line);
			CHECK(headline[0] && detail[0] && line[0], "a failure with nothing said");
		}
		for (code = 0; code <= NUMBER_OF_SERVER_REJECTION_CODES; code++)
		{
			wchar_t const *text = network_game_client_rejection_text(code);

			CHECK(!jargon(text), "\"%ls\" in %ls", jargon(text), text);
		}
		return 0;
	}
	CASE("rejection")
	{
		CHECK(contains(network_game_client_rejection_text(_rejection_code_version_too_old), L"Update GOAT H1"),
			"a newer host's: %ls", network_game_client_rejection_text(_rejection_code_version_too_old));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_version_too_new), L"Ask the host to update"),
			"an older host's: %ls", network_game_client_rejection_text(_rejection_code_version_too_new));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_bad_password), L"password"),
			"a wrong password's: %ls", network_game_client_rejection_text(_rejection_code_bad_password));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_game_is_full), L"full"),
			"a full game's: %ls", network_game_client_rejection_text(_rejection_code_game_is_full));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_game_is_closed), L"right now"),
			"a closed game's: %ls", network_game_client_rejection_text(_rejection_code_game_is_closed));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_blacklisted_machine), L"blocked"),
			"a ban's: %ls", network_game_client_rejection_text(_rejection_code_blacklisted_machine));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_name_refused), L"player name"),
			"a refused name's: %ls", network_game_client_rejection_text(_rejection_code_name_refused));
		CHECK(contains(network_game_client_rejection_text(_rejection_code_bad_join_token), L"Try joining again"),
			"another refusal's: %ls", network_game_client_rejection_text(_rejection_code_bad_join_token));
		return 0;
	}
	/* every text, for the test to measure in the menus' fonts: "H " a
	headline, "D " each of a detail's lines, "L " a browser's line */
	CASE("print")
	{
		int online, brokers, stage, nat, host, listing, phone, upnp;

		for (upnp = 0; upnp < 2; upnp++)
		for (online = 0; online < 2; online++)
		for (brokers = 0; brokers < 2; brokers++)
		for (stage = _p2p_join_idle; stage <= _p2p_join_reached; stage++)
		for (nat = 0; nat < 2; nat++)
		for (host = 0; host < 2; host++)
		for (listing = 0; listing < 2; listing++)
		for (phone = 0; phone < 2; phone++)
		{
			fake_upnp = upnp;
			failed(online, brokers, stage, nat, host, (boolean)listing, (boolean)phone);
			print_lines('H', headline);
			print_lines('D', detail);
			print_lines('L', line);
		}
		return 0;
	}
	return 2;
}
