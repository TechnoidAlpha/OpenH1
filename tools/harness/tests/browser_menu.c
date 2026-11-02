#include "harness.h"

#include <stdarg.h>
#include <wchar.h>

/* The server browsers' tabs, their words with no games, and the menus'
shared text fitting (port/linux/game/menu_functions.c, in Settings' look:
tools/port_settings.py's _browser): a tab lists its kind of game (dedicated
servers', OpenH1's, Legacy, co-op) or the favorites, the favorites first and
the rest sorted (in the order the list opens in, the OpenH1 games first,
and the dedicated servers first among them and among the Legacy ones),
the favorites offline only on ALL and FAVORITES; a list with no games says
why, and the local network's what to do; a name too long for its column is
cut short with "..." where it fits, and one that fits is kept whole */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(value, low, high) MIN(MAX(value, low), high)
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define P2P_LISTING_NAME_SIZE 32
#define P2P_LISTING_MAP_SIZE 32
#define ROW_TEXT_LENGTH 64

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

#include "config.inc"

static short browser_found_favorites, browser_offline_count;
static short browser_sort_column, browser_tab;
static boolean browser_sort_descending;
static struct browser_filters browser_filters = { TRUE, TRUE, TRUE, 0, BROWSER_ALL_ENGINES, "" };
static boolean browser_filtering;
static short browser_filtered_count;

/* the fake settings (port_config.c's): the filters' own, by name */
static struct
{
	char const *name;
	char const *value;
} fake_settings[] =
{
	{ "network.browser_show_full", "true" }, { "network.browser_show_empty", "true" },
	{ "network.browser_show_password", "true" }, { "network.browser_ping", "0" },
	{ "network.browser_show_ctf", "true" }, { "network.browser_show_slayer", "true" },
	{ "network.browser_show_oddball", "true" }, { "network.browser_show_king", "true" },
	{ "network.browser_show_race", "true" }, { "network.browser_search", "" },
};

static char const *config_string(char const *name)
{
	size_t index;

	for (index = 0; index < sizeof(fake_settings) / sizeof(fake_settings[0]); index++)
	{
		if (!strcmp(fake_settings[index].name, name))
			return fake_settings[index].value;
	}
	CHECK(FALSE, "%s is not a setting", name);
	return "";
}

static void config_set(char const *name, char const *value)
{
	size_t index;

	for (index = 0; index < sizeof(fake_settings) / sizeof(fake_settings[0]); index++)
	{
		if (!strcmp(fake_settings[index].name, name))
			fake_settings[index].value = value;
	}
}

static int config_boolean(char const *name)
{
	return !strcmp(config_string(name), "true");
}

static long config_integer(char const *name)
{
	return atol(config_string(name));
}

/* the fake font: each character 6 units wide, "." 2 */
static short settings_text_width(long font, wchar_t const *text)
{
	short width = 0;

	(void)font;
	for (; *text; text++)
		width = (short)(width + (*text == '.' ? 2 : 6));
	return width;
}

#include "under_test.inc"

static struct browser_sort_entry entry(char const *name, boolean openh1, boolean coop, short players)
{
	struct browser_sort_entry result;

	memset(&result, 0, sizeof(result));
	snprintf(result.name, sizeof(result.name), "%s", name);
	result.openh1 = openh1;
	result.coop = coop;
	/* (the co-op game a dedicated server's: a Legacy one, OpenCE's; and
	alpha an OpenH1 one's) */
	result.dedicated = !strcmp(name, "coop") || !strcmp(name, "alpha");
	result.players = players;
	result.rate = openh1 ? 60 : 30;
	/* (12 players at most: delta's full) */
	result.maximum_players = 12;
	return result;
}

/* the games a tab lists, their names in order, separated by spaces, those
shown as favorites (the first browser_found_favorites) marked with a * */
static void listed(short tab, char *names, size_t size, short *offline)
{
	struct browser_sort_entry entries[5];
	short favorite_of[5] = { NONE, 0, NONE, NONE, 1 };
	short order[5], count, index;

	entries[0] = entry("alpha", TRUE, FALSE, 3);
	entries[1] = entry("bravo", FALSE, FALSE, 9);
	entries[2] = entry("coop", FALSE, TRUE, 1);
	entries[3] = entry("delta", TRUE, FALSE, 12);
	entries[4] = entry("echo", TRUE, TRUE, 2);
	browser_tab = tab;
	browser_sort_column = _browser_sort_players;
	browser_sort_descending = TRUE;
	browser_offline_count = 2;
	count = browser_favorites_order(favorite_of, entries, 5, order);
	names[0] = 0;
	for (index = 0; index < count; index++)
	{
		strncat(names, entries[order[index]].name, size - strlen(names) - 3);
		strncat(names, index < browser_found_favorites ? "* " : " ", size - strlen(names) - 1);
	}
	*offline = browser_offline_count;
}

static boolean contains(wchar_t const *text, wchar_t const *part)
{
	return wcsstr(text, part) != NULL;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("tabs")
	{
		char names[128];
		short offline;

		/* (the favorites first, OpenH1's echo before Legacy's bravo; the
		rest the OpenH1 games first, the dedicated server alpha before
		delta's more players, then the Legacy ones) */
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* bravo* alpha delta coop "), "ALL lists %s", names);
		CHECK(offline == 2, "ALL lists no favorites offline");
		listed(_browser_tab_dedicated, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "alpha coop "), "DEDICATED lists %s", names);
		CHECK(offline == 0, "DEDICATED lists the favorites offline");
		listed(_browser_tab_openh1, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* alpha delta "), "OPENH1 lists %s", names);
		CHECK(offline == 0, "OPENH1 lists the favorites offline");
		listed(_browser_tab_legacy, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* coop "), "LEGACY lists %s", names);
		listed(_browser_tab_coop, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* coop "), "CO-OP lists %s", names);
		CHECK(offline == 0, "CO-OP lists the favorites offline");
		listed(_browser_tab_favorites, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* bravo* "), "FAVORITES lists %s", names);
		CHECK(offline == 2, "FAVORITES lists no favorites offline");
		return 0;
	}
	CASE("empty")
	{
		wchar_t text[256];

		/* the local network's: what to do */
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_all, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No games on your network yet.") && contains(text, L"Host one from Create Game."),
			"the local network's empty list says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_openh1, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No OpenH1 games on your network."), "OPENH1's says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_favorites, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"favorites") && contains(text, L"FAVORITE on a game"), "FAVORITES' says %ls", text);
		/* the server browser's: still looking, off, none of a kind, newer */
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, TRUE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"Looking for public games"), "looking says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, FALSE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"Internet play is off"), "offline says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_coop, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No co-op games"), "CO-OP's says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_dedicated, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No dedicated server games"), "DEDICATED's says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, FALSE, 3, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"3 games are on a newer version"), "newer says %ls", text);
		/* (the filters' hidden games: how many, and where to change them;
		on a tab too, and before the newer games) */
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_openh1, TRUE, TRUE, FALSE, 3, 2, text,
			NUMBEROF(text));
		CHECK(contains(text, L"2 games are hidden by your filters") && contains(text, L"FILTERS"),
			"filtered says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_all, TRUE, TRUE, FALSE, 0, 1, text, NUMBEROF(text));
		CHECK(contains(text, L"1 game is hidden by your filters"), "one filtered says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No public games found"), "none says %ls", text);
		/* Join by Link's: how to paste a link (its footer's prompt) */
		browser_empty_text(_multiplayer_mode_direct_link, _browser_tab_all, TRUE, TRUE, FALSE, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"PASTE LINK"), "Join by Link's says %ls", text);
		return 0;
	}
	CASE("filters")
	{
		struct browser_filters filters;
		struct browser_sort_entry game = entry("Foozy BG Snipers", TRUE, FALSE, 4);
		char names[128];
		short offline;

		/* (a Slayer game of 4 of 16 on Blood Gulch, a ping of 120, no password) */
		game.maximum_players = 16;
		game.engine_type = 2;
		game.ping = 120;
		snprintf(game.map, sizeof(game.map), "bloodgulch");
		wcscpy(game.map_shown, L"Blood Gulch");
		/* the settings' defaults list every game */
		browser_filters_read(&filters);
		CHECK(filters.show_full && filters.show_empty && filters.show_password && filters.maximum_ping == 0 &&
			filters.engines == BROWSER_ALL_ENGINES && !filters.search[0], "the defaults filter games");
		CHECK(browser_filters_show(&filters, &game), "the defaults hide a game");
		/* full and empty games */
		game.players = 16;
		CHECK(browser_filters_show(&filters, &game), "SHOW FULL GAMES: YES hides a full game");
		filters.show_full = FALSE;
		CHECK(!browser_filters_show(&filters, &game), "SHOW FULL GAMES: NO lists a full game");
		game.players = 15;
		CHECK(browser_filters_show(&filters, &game), "SHOW FULL GAMES: NO hides a game with room");
		game.maximum_players = 0;
		game.players = 40;
		CHECK(browser_filters_show(&filters, &game), "a game whose most players is not known hidden as full");
		game.maximum_players = 16;
		game.players = 0;
		filters.show_empty = FALSE;
		CHECK(!browser_filters_show(&filters, &game), "SHOW EMPTY GAMES: NO lists an empty game");
		game.players = 1;
		CHECK(browser_filters_show(&filters, &game), "SHOW EMPTY GAMES: NO hides a game of one player");
		/* a password */
		game.locked = TRUE;
		CHECK(browser_filters_show(&filters, &game), "SHOW PASSWORD GAMES: YES hides a locked game");
		filters.show_password = FALSE;
		CHECK(!browser_filters_show(&filters, &game), "SHOW PASSWORD GAMES: NO lists a locked game");
		game.locked = FALSE;
		/* the ping: under the limit, a ping not known listed */
		filters.maximum_ping = 150;
		CHECK(browser_filters_show(&filters, &game), "PING UNDER 150 hides 120 ms");
		filters.maximum_ping = 100;
		CHECK(!browser_filters_show(&filters, &game), "PING UNDER 100 lists 120 ms");
		game.ping = 100;
		CHECK(!browser_filters_show(&filters, &game), "PING UNDER 100 lists 100 ms");
		game.ping = 99;
		CHECK(browser_filters_show(&filters, &game), "PING UNDER 100 hides 99 ms");
		game.ping = NONE;
		CHECK(browser_filters_show(&filters, &game), "a ping not known hidden by PING");
		/* the game types, more than one at once; a co-op game has none */
		filters.engines = (short)(1 << 1 | 1 << 3);
		CHECK(!browser_filters_show(&filters, &game), "CTF and ODDBALL alone list Slayer");
		game.engine_type = 3;
		CHECK(browser_filters_show(&filters, &game), "CTF and ODDBALL alone hide Oddball");
		game.engine_type = 1;
		CHECK(browser_filters_show(&filters, &game), "CTF and ODDBALL alone hide CTF");
		game.engine_type = 0;
		CHECK(browser_filters_show(&filters, &game), "the game types hide a co-op game");
		filters.engines = 0;
		CHECK(browser_filters_show(&filters, &game), "no game type hides a co-op game");
		game.engine_type = 5;
		CHECK(!browser_filters_show(&filters, &game), "no game type lists Race");
		filters.engines = BROWSER_ALL_ENGINES;
		/* the words: the name's or the map's (shown or its own), in any case */
		snprintf(filters.search, sizeof(filters.search), "snip");
		CHECK(browser_filters_show(&filters, &game), "\"snip\" hides Foozy BG Snipers");
		snprintf(filters.search, sizeof(filters.search), "BLOOD GULCH");
		CHECK(browser_filters_show(&filters, &game), "\"BLOOD GULCH\" hides a game on Blood Gulch");
		snprintf(filters.search, sizeof(filters.search), "bloodg");
		CHECK(browser_filters_show(&filters, &game), "\"bloodg\" hides a game on bloodgulch");
		snprintf(filters.search, sizeof(filters.search), "hang em");
		CHECK(!browser_filters_show(&filters, &game), "\"hang em\" lists a game on Blood Gulch");
		snprintf(filters.search, sizeof(filters.search), "Snipers!");
		CHECK(!browser_filters_show(&filters, &game), "\"Snipers!\" lists Foozy BG Snipers");
		/* the settings read: the game types in the engines' order, the
		ping, the words without the spaces around them */
		config_set("network.browser_show_full", "false");
		config_set("network.browser_show_password", "false");
		config_set("network.browser_ping", "250");
		config_set("network.browser_show_slayer", "false");
		config_set("network.browser_show_race", "false");
		config_set("network.browser_search", "  gulch ");
		browser_filters_read(&filters);
		CHECK(!filters.show_full && filters.show_empty && !filters.show_password, "the settings' YES and NO");
		CHECK(filters.maximum_ping == 250, "network.browser_ping 250 read as %d", filters.maximum_ping);
		CHECK(filters.engines == (1 << 1 | 1 << 3 | 1 << 4), "the game types read as %#x", filters.engines);
		CHECK(!strcmp(filters.search, "gulch"), "the words read as \"%s\"", filters.search);
		/* the list: the filters hide games from every tab, counted; the
		favorites too. Off (Join by Link's), they hide none */
		browser_filters = filters;
		browser_filters.engines = BROWSER_ALL_ENGINES;
		browser_filters.search[0] = 0;
		browser_filters.show_empty = FALSE;
		browser_filters.maximum_ping = 0;
		browser_filtering = TRUE;
		/* (bravo's 9 and delta's 12 of 12 players: delta full) */
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* bravo* alpha coop "), "ALL filtered lists %s", names);
		CHECK(browser_filtered_count == 1, "ALL filtered counts %d hidden", browser_filtered_count);
		listed(_browser_tab_openh1, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* alpha "), "OPENH1 filtered lists %s", names);
		CHECK(browser_filtered_count == 1, "OPENH1 filtered counts %d hidden", browser_filtered_count);
		listed(_browser_tab_legacy, names, sizeof(names), &offline);
		CHECK(browser_filtered_count == 0, "LEGACY counts %d hidden of another tab's", browser_filtered_count);
		snprintf(browser_filters.search, sizeof(browser_filters.search), "ECHO");
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* "), "\"ECHO\" lists %s", names);
		CHECK(browser_filtered_count == 4, "\"ECHO\" counts %d hidden", browser_filtered_count);
		browser_filtering = FALSE;
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* bravo* alpha delta coop "), "unfiltered lists %s", names);
		CHECK(browser_filtered_count == 0, "unfiltered counts %d hidden", browser_filtered_count);
		return 0;
	}
	CASE("ping")
	{
		wchar_t text[16];

		/* the ping's column: its milliseconds, a faded "..." while it is
		measured, a dash while it is not known */
		browser_ping_cell(_browser_ping_measured, 38, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"38"), "38 ms shown as %ls", text);
		browser_ping_cell(_browser_ping_measuring, -1, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"\x2026"), "a ping being measured shown as %ls", text);
		browser_ping_cell(_browser_ping_locked, -1, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"\x2013"), "a game with a password's ping shown as %ls", text);
		browser_ping_cell(_browser_ping_failed, -1, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"\x2013"), "a host not reached's ping shown as %ls", text);
		browser_ping_cell(_browser_ping_local, -1, text, NUMBEROF(text));
		CHECK(!wcscmp(text, L"\x2013"), "a local network game's ping shown as %ls", text);
		/* (the bars' frame by how good: none lit, poor, fair, good) */
		CHECK(browser_ping_bars(-1) == 0 && browser_ping_bars(38) == 3 && browser_ping_bars(PING_GOOD) == 2 &&
			browser_ping_bars(PING_FAIR) == 1, "the bars do not follow the ping");
		return 0;
	}
	CASE("ellipsis")
	{
		wchar_t fitted[ROW_TEXT_LENGTH];

		/* (6 units a character: "FOOZY BG SNIPER" is 90) */
		menu_text_fit(1, L"FOOZY BG SNIPER", fitted, NUMBEROF(fitted), 90);
		CHECK(!wcscmp(fitted, L"FOOZY BG SNIPER"), "a name that fits cut: %ls", fitted);
		menu_text_fit(1, L"FOOZY BG SNIPER", fitted, NUMBEROF(fitted), 60);
		CHECK(!wcscmp(fitted, L"FOOZY BG..."), "cut to 60 units: %ls", fitted);
		CHECK(settings_text_width(1, fitted) <= 60, "the cut text is wider than its column: %ls", fitted);
		/* (the most that fits: one more character would not) */
		menu_text_fit(1, L"Assault on the Control Room", fitted, NUMBEROF(fitted), 84);
		CHECK(!wcscmp(fitted, L"Assault on th..."), "cut to 84 units, exactly its width: %ls", fitted);
		menu_text_fit(1, L"ABCDEFGHIJ", fitted, NUMBEROF(fitted), 4);
		CHECK(!wcscmp(fitted, L"..."), "nothing fits: %ls", fitted);
		/* (a font not known: all of it) */
		menu_text_fit(NONE, L"FOOZY BG SNIPER", fitted, NUMBEROF(fitted), 10);
		CHECK(!wcscmp(fitted, L"FOOZY BG SNIPER"), "cut without its font: %ls", fitted);
		return 0;
	}
	CHECK(FALSE, "no such case: %s", case_name);
	return 1;
}
