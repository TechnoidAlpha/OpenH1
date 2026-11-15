#include "harness.h"

#include <stdarg.h>
#include <wchar.h>

/* The server browsers' tabs, their words with no games, and the menus'
shared text fitting (port/linux/game/menu_functions.c, in Settings' look:
tools/port_settings.py's _browser): a tab lists its kind of game (dedicated
servers', OpenH1's, Legacy, co-op) or the favorites, the favorites first and
the rest sorted (in the order the list opens in, the most players first,
and of as many players the OpenH1 games first and the dedicated servers
first among them and among the Legacy ones),
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

static short browser_offline_count;
static boolean browser_grouped;
static boolean browser_listed_favorite[BROWSER_LISTED_MAXIMUM];
static short browser_sort_column, browser_tab;
/* (a visit's tab: pc_menu_browser_visit's, none in these tests) */
static short browser_tab_visit = NONE;
static boolean browser_sort_descending;
static struct browser_filters browser_filters = { TRUE, TRUE, TRUE, 0, _browser_tick_any, _browser_type_any };
static boolean browser_filtering;
/* the 2V2 browser's playlist (NONE: the server browser itself): its kind's
games listed whatever the tab and the Filters, no favorites offline */
static short browser_doubles_playlist = NONE;
static short browser_filtered_count, browser_searched_count;
static char browser_searches[NUMBER_OF_BROWSER_SEARCHES][BROWSER_SEARCH_LENGTH + 1];

/* the fake settings (port_config.c's): the filters' own, by name */
static struct
{
	char const *name;
	char const *value;
} fake_settings[] =
{
	{ "network.browser_show_full", "true" }, { "network.browser_show_empty", "true" },
	{ "network.browser_show_password", "true" }, { "network.browser_ping", "0" },
	{ "network.browser_tick", "any" }, { "network.browser_game_type", "any" },
	{ "network.browser_tab", "all" }, { "network.browser_sort", "players" },
	{ "network.browser_sort_descending", "true" },
};
/* the settings config_write wrote, in order ("name=value "), and the words
they were given, kept */
static char config_written[512];
static char config_words[8][32];
static short config_word_count;

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

/* (port_config.c's: the setting for now and in config.toml) */
static int config_write(char const *name, char const *value)
{
	char *kept = config_words[config_word_count++ % 8];

	snprintf(kept, sizeof(config_words[0]), "%s", value);
	config_set(name, kept);
	strncat(config_written, name, sizeof(config_written) - strlen(config_written) - 1);
	strncat(config_written, "=", sizeof(config_written) - strlen(config_written) - 1);
	strncat(config_written, value, sizeof(config_written) - strlen(config_written) - 1);
	strncat(config_written, " ", sizeof(config_written) - strlen(config_written) - 1);
	return 1;
}

static int config_boolean(char const *name)
{
	return !strcmp(config_string(name), "true");
}

/* (menu_engine.c's: the new engine draws the menus, not the classic) */
static boolean engine_looks;
static boolean menu_engine_looks(void) { return engine_looks; }

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
shown as favorites (browser_listed_favorite) marked with a * */
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
	/* (alpha's and coop's maps Blood Gulch, as shown too) */
	for (index = 0; index < 5; index++)
	{
		snprintf(entries[index].map, sizeof(entries[index].map), "%s", index == 0 || index == 2 ? "bloodgulch" :
			"beavercreek");
		wcscpy(entries[index].map_shown, index == 0 || index == 2 ? L"Blood Gulch" : L"Battle Creek");
	}
	browser_tab = tab;
	browser_sort_column = _browser_sort_players;
	browser_sort_descending = TRUE;
	browser_offline_count = 2;
	count = browser_favorites_order(favorite_of, entries, 5, order);
	names[0] = 0;
	for (index = 0; index < count; index++)
	{
		strncat(names, entries[order[index]].name, size - strlen(names) - 3);
		strncat(names, browser_listed_favorite[index] ? "* " : " ", size - strlen(names) - 1);
	}
	*offline = browser_offline_count;
}

/* the server browser's list in its groups, on a tab, sorted by a column:
its games' names in order, the favorites marked with a *, each heading as
[its words]; project: the project's own servers marked as its */
static void grouped(short tab, short column, boolean descending, boolean project, char *names, size_t size)
{
	struct browser_sort_entry entries[8];
	short favorite_of[8] = { NONE, NONE, NONE, 0, NONE, NONE, 1, 2 };
	short order[8 + NUMBER_OF_BROWSER_GROUPS], count, index;

	/* (a full Legacy game players host; OpenH1's dedicated servers: an
	empty one, the project's two (one empty) and a favorite; OpenH1 games
	players host, one a favorite; a Legacy favorite) */
	entries[0] = entry("legacyhosted", FALSE, FALSE, 12);
	entries[1] = entry("openh1empty", TRUE, FALSE, 0);
	entries[1].dedicated = TRUE;
	entries[2] = entry("openh1", TRUE, FALSE, 4);
	entries[3] = entry("legacystar", FALSE, FALSE, 2);
	entries[4] = entry("projectempty", TRUE, FALSE, 0);
	entries[4].dedicated = TRUE;
	entries[4].project = project;
	entries[5] = entry("project", TRUE, FALSE, 6);
	entries[5].dedicated = TRUE;
	entries[5].project = project;
	entries[6] = entry("openh1star", TRUE, FALSE, 1);
	entries[7] = entry("communitystar", TRUE, FALSE, 3);
	entries[7].dedicated = TRUE;
	browser_tab = tab;
	browser_sort_column = column;
	browser_sort_descending = descending;
	browser_offline_count = 0;
	browser_grouped = TRUE;
	count = browser_favorites_order(favorite_of, entries, 8, order);
	browser_grouped = FALSE;
	names[0] = 0;
	for (index = 0; index < count; index++)
	{
		if (order[index] < 0)
		{
			char words[64];

			snprintf(words, sizeof(words), "[%ls] ", browser_group_heading(BROWSER_HEADING_GROUP(order[index])));
			strncat(names, words, size - strlen(names) - 1);
			continue;
		}
		strncat(names, entries[order[index]].name, size - strlen(names) - 3);
		strncat(names, browser_listed_favorite[index] ? "* " : " ", size - strlen(names) - 1);
	}
}

/* six games sorted by their players, their names in order */
static void sorted(boolean descending, char *names, size_t size)
{
	struct browser_sort_entry entries[6];
	short order[6], index, place;

	/* (#920: a game players host with 13 under an empty dedicated server;
	four games of 5, OpenH1's and Legacy, dedicated or hosted) */
	entries[0] = entry("empty", FALSE, FALSE, 0);
	entries[0].dedicated = TRUE;
	entries[1] = entry("hosted", FALSE, FALSE, 13);
	entries[2] = entry("legacy", FALSE, FALSE, 5);
	entries[3] = entry("legacyserver", FALSE, FALSE, 5);
	entries[3].dedicated = TRUE;
	entries[4] = entry("openh1", TRUE, FALSE, 5);
	entries[5] = entry("openh1server", TRUE, FALSE, 5);
	entries[5].dedicated = TRUE;
	browser_sort_column = _browser_sort_players;
	browser_sort_descending = descending;
	for (index = 0; index < 6; index++)
	{
		for (place = index; place > 0 && browser_sort_before(&entries[index], &entries[order[place - 1]]); place--)
			order[place] = order[place - 1];
		order[place] = index;
	}
	names[0] = 0;
	for (index = 0; index < 6; index++)
	{
		strncat(names, entries[order[index]].name, size - strlen(names) - 2);
		strncat(names, " ", size - strlen(names) - 1);
	}
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

		/* (the favorites first, then the rest by their players) */
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* echo* delta alpha coop "), "ALL lists %s", names);
		CHECK(offline == 2, "ALL lists no favorites offline");
		listed(_browser_tab_dedicated, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "alpha coop "), "DEDICATED lists %s", names);
		CHECK(offline == 0, "DEDICATED lists the favorites offline");
		listed(_browser_tab_openh1, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* delta alpha "), "OPENH1 lists %s", names);
		CHECK(offline == 0, "OPENH1 lists the favorites offline");
		listed(_browser_tab_legacy, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* coop "), "LEGACY lists %s", names);
		listed(_browser_tab_coop, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* coop "), "CO-OP lists %s", names);
		CHECK(offline == 0, "CO-OP lists the favorites offline");
		listed(_browser_tab_favorites, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* echo* "), "FAVORITES lists %s", names);
		CHECK(offline == 2, "FAVORITES lists no favorites offline");
		return 0;
	}
	CASE("doubles")
	{
		char names[128];
		short offline;
		short tab;

		/* (the 2V2 browser's games, its kind's already: matchmaking.c's
		matchmaking_playlist_games; every one listed on any tab, the
		favorites first, none offline) */
		browser_doubles_playlist = 6;
		for (tab = 0; tab < NUMBER_OF_BROWSER_TABS; tab++)
		{
			listed(tab, names, sizeof(names), &offline);
			CHECK(!strcmp(names, "bravo* echo* delta alpha coop "), "tab %d hides 2V2's games: %s", tab, names);
			CHECK(offline == 0, "tab %d lists favorites offline in 2V2's", tab);
		}
		/* (the Filters, the server browser's settings, hide none of them) */
		browser_filtering = TRUE;
		browser_filters.show_full = FALSE;
		browser_filters.maximum_ping = 1;
		browser_filters.tick = _browser_tick_120;
		browser_filters.game_type = _browser_type_race;
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* echo* delta alpha coop "), "the Filters hide 2V2's games: %s", names);
		/* (and the server browser itself as it was) */
		browser_doubles_playlist = NONE;
		listed(_browser_tab_legacy, names, sizeof(names), &offline);
		CHECK(strcmp(names, "bravo* echo* delta alpha coop "), "the server browser's tabs gone");
		return 0;
	}
	CASE("groups")
	{
		char names[512];

		/* (OpenH1's dedicated servers, the project's first, then the games
		players host, then the Legacy ones; in each part the favorites
		first, then by players) */
		grouped(_browser_tab_all, _browser_sort_players, TRUE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] project projectempty communitystar* openh1empty "
			"[Hosted Servers] openh1star* openh1 [Legacy (Slow) Servers] legacystar* legacyhosted "),
			"ALL lists %s", names);
		/* (none of the project's: the dedicated servers as the others) */
		grouped(_browser_tab_all, _browser_sort_players, TRUE, FALSE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] communitystar* project openh1empty projectempty "
			"[Hosted Servers] openh1star* openh1 [Legacy (Slow) Servers] legacystar* legacyhosted "),
			"ALL without the project's servers lists %s", names);
		/* (each part sorted by the column chosen, the other way too) */
		grouped(_browser_tab_all, _browser_sort_name, FALSE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] project projectempty communitystar* openh1empty "
			"[Hosted Servers] openh1star* openh1 [Legacy (Slow) Servers] legacystar* legacyhosted "),
			"by name lists %s", names);
		grouped(_browser_tab_all, _browser_sort_players, FALSE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] projectempty project communitystar* openh1empty "
			"[Hosted Servers] openh1star* openh1 [Legacy (Slow) Servers] legacystar* legacyhosted "),
			"fewest first lists %s", names);
		/* (LEGACY's lists nothing else: no heading; a group with no games
		none: OPENH1's, DEDICATED's) */
		grouped(_browser_tab_legacy, _browser_sort_players, TRUE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "legacystar* legacyhosted "), "LEGACY lists %s", names);
		grouped(_browser_tab_openh1, _browser_sort_players, TRUE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] project projectempty communitystar* openh1empty "
			"[Hosted Servers] openh1star* openh1 "), "OPENH1 lists %s", names);
		grouped(_browser_tab_dedicated, _browser_sort_players, TRUE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] project projectempty communitystar* openh1empty "),
			"DEDICATED lists %s", names);
		/* (the filters still hide games, and a group they empty has no
		heading) */
		config_set("network.browser_show_empty", "false");
		browser_filters_read(&browser_filters);
		browser_filtering = TRUE;
		grouped(_browser_tab_all, _browser_sort_players, TRUE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "[GOAT H1 Dedicated Servers] project communitystar* [Hosted Servers] openh1star* "
			"openh1 [Legacy (Slow) Servers] legacystar* legacyhosted "), "SHOW EMPTY GAMES: NO lists %s", names);
		CHECK(browser_filtered_count == 2, "%d hidden counted, not 2", browser_filtered_count);
		config_set("network.browser_show_full", "false");
		browser_filters_read(&browser_filters);
		grouped(_browser_tab_legacy, _browser_sort_players, TRUE, TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "legacystar* "), "LEGACY, no full games, lists %s", names);
		return 0;
	}
	CASE("sort")
	{
		char names[128];

		/* (the most players first, whatever the game; of as many, the
		OpenH1 games first, the dedicated servers first in each) */
		sorted(TRUE, names, sizeof(names));
		CHECK(!strcmp(names, "hosted openh1server openh1 legacyserver legacy empty "), "most first sorts %s", names);
		/* (the fewest first, as the games came of as many) */
		sorted(FALSE, names, sizeof(names));
		CHECK(!strcmp(names, "empty legacy legacyserver openh1 openh1server hosted "), "fewest first sorts %s",
			names);
		return 0;
	}

	CASE("empty")
	{
		wchar_t text[256];

		/* the local network's: what to do */
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_all, TRUE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No games on your network yet.") && contains(text, L"Host one from Create Game."),
			"the local network's empty list says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_openh1, TRUE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No GOAT H1 games on your network."), "OPENH1's says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_favorites, TRUE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"favorites") && contains(text, L"FAVORITE on a game"), "FAVORITES' says %ls", text);
		/* the server browser's: still looking, off, none of a kind, newer */
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, TRUE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"Looking for public games"), "looking says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, FALSE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"Internet play is off"), "offline says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_coop, TRUE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No co-op games"), "CO-OP's says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_dedicated, TRUE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No dedicated server games"), "DEDICATED's says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, FALSE, 3, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"3 games are on a newer version"), "newer says %ls", text);
		/* (the filters' hidden games: how many, and where to change them;
		on a tab too, and before the newer games) */
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_openh1, TRUE, TRUE, FALSE, 3, 2, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"2 games are hidden by your filters") && contains(text, L"FILTERS"),
			"filtered says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_all, TRUE, TRUE, FALSE, 0, 1, 0, text, NUMBEROF(text));
		CHECK(contains(text, L"1 game is hidden by your filters"), "one filtered says %ls", text);
		/* (the searches' hidden games, the Filters' too: no server matches) */
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, FALSE, 0, 3, 2, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No servers match your search"), "searched says %ls", text);
		browser_empty_text(_multiplayer_mode_lan, _browser_tab_all, TRUE, TRUE, FALSE, 0, 3, 0, text,
			NUMBEROF(text));
		CHECK(!contains(text, L"match") && contains(text, L"hidden by your filters"),
			"filtered alone says %ls", text);
		browser_empty_text(_multiplayer_mode_server_browser, _browser_tab_all, TRUE, TRUE, FALSE, 0, 0, 0, text,
			NUMBEROF(text));
		CHECK(contains(text, L"No public games found"), "none says %ls", text);
		/* Join by Link's: how to paste a link (its footer's prompt) */
		browser_empty_text(_multiplayer_mode_direct_link, _browser_tab_all, TRUE, TRUE, FALSE, 0, 0, 0, text,
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
			filters.tick == _browser_tick_any && filters.game_type == _browser_type_any, "the defaults filter games");
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
		/* the settings read: the tick rate and the game type by their
		words, in their orders; a word not known (another build's) any */
		config_set("network.browser_show_full", "false");
		config_set("network.browser_show_password", "false");
		config_set("network.browser_ping", "250");
		config_set("network.browser_tick", "120");
		config_set("network.browser_game_type", "team_slayer");
		browser_filters_read(&filters);
		CHECK(!filters.show_full && filters.show_empty && !filters.show_password, "the settings' YES and NO");
		CHECK(filters.maximum_ping == 250, "network.browser_ping 250 read as %d", filters.maximum_ping);
		CHECK(filters.tick == _browser_tick_120, "network.browser_tick 120 read as %d", filters.tick);
		CHECK(filters.game_type == _browser_type_team_slayer, "network.browser_game_type team_slayer read as %d",
			filters.game_type);
		config_set("network.browser_tick", "legacy");
		config_set("network.browser_game_type", "pro_2v2");
		browser_filters_read(&filters);
		CHECK(filters.tick == _browser_tick_legacy && filters.game_type == _browser_type_pro_2v2,
			"legacy and pro_2v2 read as %d and %d", filters.tick, filters.game_type);
		config_set("network.browser_tick", "240");
		config_set("network.browser_game_type", "juggernaut");
		browser_filters_read(&filters);
		CHECK(filters.tick == _browser_tick_any && filters.game_type == _browser_type_any,
			"words not known read as %d and %d, not any", filters.tick, filters.game_type);
		config_set("network.browser_tick", "any");
		config_set("network.browser_game_type", "any");
		/* the list: the filters hide games from every tab, counted; the
		favorites too. Off (Join by Link's), they hide none */
		browser_filters = filters;
		browser_filters.show_empty = FALSE;
		browser_filters.maximum_ping = 0;
		browser_filtering = TRUE;
		/* (bravo's 9 and delta's 12 of 12 players: delta full) */
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* echo* alpha coop "), "ALL filtered lists %s", names);
		CHECK(browser_filtered_count == 1, "ALL filtered counts %d hidden", browser_filtered_count);
		listed(_browser_tab_openh1, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "echo* alpha "), "OPENH1 filtered lists %s", names);
		CHECK(browser_filtered_count == 1, "OPENH1 filtered counts %d hidden", browser_filtered_count);
		listed(_browser_tab_legacy, names, sizeof(names), &offline);
		CHECK(browser_filtered_count == 0, "LEGACY counts %d hidden of another tab's", browser_filtered_count);
		browser_filtering = FALSE;
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* echo* delta alpha coop "), "unfiltered lists %s", names);
		CHECK(browser_filtered_count == 0, "unfiltered counts %d hidden", browser_filtered_count);
		return 0;
	}
	CASE("tick")
	{
		struct browser_filters filters = { TRUE, TRUE, TRUE, 0, _browser_tick_any, _browser_type_any };
		struct browser_sort_entry game = entry("game", TRUE, FALSE, 4);
		/* (OpenH1 at 30, 60 and 120, and Legacy at 30) */
		static short const rates[] = { 30, 60, 120, 30 };
		static boolean const openh1[] = { TRUE, TRUE, TRUE, FALSE };
		/* (which of them each choice lists: any, 30, 60, 120, Legacy) */
		static char const *const shown[NUMBER_OF_BROWSER_TICKS] = { "1111", "1000", "0100", "0010", "0001" };
		short tick, index;

		for (tick = 0; tick < NUMBER_OF_BROWSER_TICKS; tick++)
		{
			filters.tick = tick;
			for (index = 0; index < 4; index++)
			{
				game.rate = rates[index];
				game.openh1 = openh1[index];
				CHECK(browser_filters_show(&filters, &game) == (shown[tick][index] == '1'),
					"TICK RATE %s %s an %s game at %d", browser_tick_words[tick],
					shown[tick][index] == '1' ? "hides" : "lists", openh1[index] ? "OpenH1" : "Legacy", rates[index]);
			}
		}
		/* (the list: OpenH1 60's alone, of the five: alpha, delta and echo
		at 60, bravo and coop Legacy; delta full but listed) */
		{
			char names[128];
			short offline;

			browser_filters = filters;
			browser_filters.tick = _browser_tick_60;
			browser_filtering = TRUE;
			listed(_browser_tab_all, names, sizeof(names), &offline);
			CHECK(!strcmp(names, "echo* delta alpha "), "OPENH1 60 lists %s", names);
			CHECK(browser_filtered_count == 2, "OPENH1 60 counts %d hidden", browser_filtered_count);
			browser_filters.tick = _browser_tick_legacy;
			listed(_browser_tab_all, names, sizeof(names), &offline);
			CHECK(!strcmp(names, "bravo* coop "), "LEGACY lists %s", names);
			browser_filters.tick = _browser_tick_any;
			browser_filtering = FALSE;
		}
		return 0;
	}
	CASE("game_type")
	{
		struct browser_filters filters = { TRUE, TRUE, TRUE, 0, _browser_tick_any, _browser_type_any };
		/* the games: (engine, kind, teams, PRO 2V2's, co-op) */
		static struct
		{
			char const *name;
			short engine;
			byte kind;
			boolean teams, pro_2v2, coop;
		} const games[] =
		{
			{ "slayer", 2, _game_mode_none, FALSE, FALSE, FALSE },
			{ "team_slayer", 2, _game_mode_none, TRUE, FALSE, FALSE },
			{ "hc_slayer", 2, GAME_MODES_KIND_HARDCORE, FALSE, FALSE, FALSE },
			{ "ctf", 1, _game_mode_none, TRUE, FALSE, FALSE },
			{ "hc_ctf", 1, GAME_MODES_KIND_HARDCORE, TRUE, FALSE, FALSE },
			{ "oddball", 3, _game_mode_none, FALSE, FALSE, FALSE },
			{ "king", 4, _game_mode_none, TRUE, FALSE, FALSE },
			{ "race", 5, _game_mode_none, FALSE, FALSE, FALSE },
			{ "swat", 2, _game_mode_swat, TRUE, FALSE, FALSE },
			{ "hc_infection", 2, _game_mode_infection | GAME_MODES_KIND_HARDCORE, TRUE, FALSE, FALSE },
			{ "gun_game", 2, _game_mode_gun_game, FALSE, FALSE, FALSE },
			{ "coop", 0, _game_mode_none, FALSE, FALSE, TRUE },
			{ "pro_2v2", 2, _game_mode_none, TRUE, TRUE, FALSE },
		};
		/* (which games each choice lists, by their names) */
		static char const *const shown[NUMBER_OF_BROWSER_TYPES] =
		{
			"slayer team_slayer hc_slayer ctf hc_ctf oddball king race swat hc_infection gun_game coop pro_2v2 ",
			"slayer hc_slayer ", "team_slayer pro_2v2 ", "ctf hc_ctf ", "oddball ", "king ", "race ", "swat ",
			"hc_infection ", "gun_game ", "coop ", "pro_2v2 ",
		};
		short game_type, index;

		for (game_type = 0; game_type < NUMBER_OF_BROWSER_TYPES; game_type++)
		{
			char names[256];

			names[0] = 0;
			filters.game_type = game_type;
			for (index = 0; index < (short)NUMBEROF(games); index++)
			{
				struct browser_sort_entry game = entry(games[index].name, TRUE, games[index].coop, 4);

				game.engine_type = games[index].engine;
				game.kind = games[index].kind;
				game.has_teams = games[index].teams;
				game.pro_2v2 = games[index].pro_2v2;
				if (browser_filters_show(&filters, &game))
				{
					strncat(names, games[index].name, sizeof(names) - strlen(names) - 2);
					strcat(names, " ");
				}
			}
			CHECK(!strcmp(names, shown[game_type]), "GAME TYPE %s lists %s", browser_type_words[game_type], names);
		}
		return 0;
	}
	CASE("view")
	{
		/* the defaults: ALL, by players, the most first */
		browser_tab = _browser_tab_favorites;
		browser_sort_column = _browser_sort_map;
		browser_sort_descending = FALSE;
		browser_view_read();
		CHECK(browser_tab == _browser_tab_all && browser_sort_column == _browser_sort_players &&
			browser_sort_descending, "the defaults read as tab %d, column %d, %s", browser_tab, browser_sort_column,
			browser_sort_descending ? "down" : "up");
		/* nothing changed, nothing written */
		config_written[0] = 0;
		browser_view_write();
		CHECK(!config_written[0], "an unchanged view wrote %s", config_written);
		/* a tab, a column and its way chosen: each written, and read again
		as the next run's */
		browser_tab = _browser_tab_legacy;
		browser_view_write();
		CHECK(!strcmp(config_written, "network.browser_tab=legacy "), "LEGACY wrote %s", config_written);
		browser_sort_column = _browser_sort_ping;
		browser_sort_descending = FALSE;
		config_written[0] = 0;
		browser_view_write();
		CHECK(!strcmp(config_written, "network.browser_sort=ping network.browser_sort_descending=false "),
			"PING up wrote %s", config_written);
		browser_tab = _browser_tab_all;
		browser_sort_column = _browser_sort_players;
		browser_sort_descending = TRUE;
		browser_view_read();
		CHECK(browser_tab == _browser_tab_legacy && browser_sort_column == _browser_sort_ping &&
			!browser_sort_descending, "read again as tab %d, column %d, %s", browser_tab, browser_sort_column,
			browser_sort_descending ? "down" : "up");
		/* every tab and column, by its word */
		for (browser_tab = 0; browser_tab < NUMBER_OF_BROWSER_TABS; browser_tab++)
		{
			short tab = browser_tab;

			browser_view_write();
			browser_tab = NONE;
			browser_view_read();
			CHECK(browser_tab == tab, "tab %d read again as %d", tab, browser_tab);
		}
		for (browser_sort_column = 0; browser_sort_column < NUMBER_OF_BROWSER_SORTS; browser_sort_column++)
		{
			short column = browser_sort_column;

			browser_view_write();
			browser_sort_column = NONE;
			browser_view_read();
			CHECK(browser_sort_column == column, "column %d read again as %d", column, browser_sort_column);
		}
		/* a word not known (an older or newer build's): the first */
		config_set("network.browser_tab", "pinned");
		config_set("network.browser_sort", "region");
		browser_view_read();
		CHECK(browser_tab == _browser_tab_all && browser_sort_column == _browser_sort_players,
			"words not known read as tab %d, column %d", browser_tab, browser_sort_column);
		/* CO-OP kept, the new engine's browser: ALL (its CO-OP tab opens
		co-op's own screen, whose list is the co-op games) */
		config_set("network.browser_tab", "coop");
		engine_looks = TRUE;
		browser_view_read();
		CHECK(browser_tab == _browser_tab_all, "the new engine's browser opened on tab %d, not ALL", browser_tab);
		engine_looks = FALSE;
		browser_view_read();
		CHECK(browser_tab == _browser_tab_coop, "the classic engine's browser opened on tab %d, not CO-OP", browser_tab);
		return 0;
	}
	CASE("search")
	{
		char searches[NUMBER_OF_BROWSER_SEARCHES][BROWSER_SEARCH_LENGTH + 1];
		char names[128];
		short offline;

#define SEARCH(server, map) (snprintf(searches[_browser_search_server], sizeof(searches[0]), "%s", server), \
	snprintf(searches[_browser_search_map], sizeof(searches[0]), "%s", map), searches[_browser_search_any][0] = 0)
#define SEARCH_ANY(server, map, any) (SEARCH(server, map), \
	snprintf(searches[_browser_search_any], sizeof(searches[0]), "%s", any))
#define SHOWS() browser_search_shows(searches, "Foozy BG-Snipers", "bloodgulch", L"Blood Gulch")
		/* nothing searched lists every game */
		SEARCH("", "");
		CHECK(SHOWS(), "no search hides a game");
		/* the server's words: part of its name, in any case, without spaces
		or punctuation */
		SEARCH("snip", "");
		CHECK(SHOWS(), "\"snip\" hides Foozy BG-Snipers");
		SEARCH("FOOZY", "");
		CHECK(SHOWS(), "\"FOOZY\" hides Foozy BG-Snipers");
		SEARCH("bg snipers", "");
		CHECK(SHOWS(), "\"bg snipers\" hides Foozy BG-Snipers");
		SEARCH("foozybg", "");
		CHECK(SHOWS(), "\"foozybg\" hides Foozy BG-Snipers");
		SEARCH("Snipers!", "");
		CHECK(SHOWS(), "\"Snipers!\" hides Foozy BG-Snipers");
		SEARCH("!?", "");
		CHECK(SHOWS(), "punctuation alone hides a game");
		SEARCH("sniperz", "");
		CHECK(!SHOWS(), "\"sniperz\" lists Foozy BG-Snipers");
		/* (the server's words not the map's) */
		SEARCH("gulch", "");
		CHECK(!SHOWS(), "the server's \"gulch\" lists a game by its map");
		/* the map's words: its own name or its name as shown */
		SEARCH("", "blood gulch");
		CHECK(SHOWS(), "\"blood gulch\" hides bloodgulch");
		SEARCH("", "BLOODG");
		CHECK(SHOWS(), "\"BLOODG\" hides bloodgulch");
		CHECK(!browser_search_shows(searches, "x", "bg_map", L"Hang 'Em High"), "\"BLOODG\" lists Hang 'Em High");
		SEARCH("", "danger");
		CHECK(browser_search_shows(searches, "x", "map_a", L"Danger Canyon"), "a map's name as shown not searched");
		CHECK(browser_search_shows(searches, "x", "dangercanyon", L""), "a map's own name not searched");
		SEARCH("", "snip");
		CHECK(!SHOWS(), "the map's \"snip\" lists a game by its name");
		/* both: each its own, together */
		SEARCH("snip", "gulch");
		CHECK(SHOWS(), "\"snip\" and \"gulch\" hide Foozy BG-Snipers on Blood Gulch");
		SEARCH("snip", "hang");
		CHECK(!SHOWS(), "\"snip\" and \"hang\" list a game on Blood Gulch");
		SEARCH("halo", "gulch");
		CHECK(!SHOWS(), "\"halo\" and \"gulch\" list Foozy BG-Snipers");
		/* the touchscreen's one search: a word of the server's name or of its
		map (its own name or as shown), in any case, without spaces or
		punctuation; with the columns' searches, each its own, together */
		SEARCH_ANY("", "", "snip");
		CHECK(SHOWS(), "the touchscreen's \"snip\" hides Foozy BG-Snipers");
		SEARCH_ANY("", "", "Blood Gulch");
		CHECK(SHOWS(), "the touchscreen's \"Blood Gulch\" hides a game on bloodgulch");
		SEARCH_ANY("", "", "gulch");
		CHECK(browser_search_shows(searches, "x", "bloodgulch", L""), "the touchscreen's search not of a map's own name");
		SEARCH_ANY("", "", "danger");
		CHECK(browser_search_shows(searches, "x", "map_a", L"Danger Canyon"),
			"the touchscreen's search not of a map's name as shown");
		SEARCH_ANY("", "", "hang");
		CHECK(!SHOWS(), "the touchscreen's \"hang\" lists Foozy BG-Snipers on Blood Gulch");
		CHECK(browser_search_shows(searches, "Hangar Bay", "x", L""), "the touchscreen's search not of a name");
		SEARCH_ANY("halo", "", "gulch");
		CHECK(!SHOWS(), "the touchscreen's \"gulch\" and the server's \"halo\" list Foozy BG-Snipers");
		SEARCH_ANY("", "hang", "snip");
		CHECK(!SHOWS(), "the touchscreen's \"snip\" and the map's \"hang\" list a game on Blood Gulch");
		SEARCH_ANY("snip", "gulch", "foozy");
		CHECK(SHOWS(), "the three searches hide Foozy BG-Snipers on Blood Gulch");
		/* (the lists find the maps' names as shown for it, as for the map's) */
		memset(browser_searches, 0, sizeof(browser_searches));
		CHECK(!browser_search_reads_map_shown(), "no search reads the maps' names");
		snprintf(browser_searches[_browser_search_any], sizeof(browser_searches[0]), "gulch");
		CHECK(browser_search_reads_map_shown(), "the touchscreen's search leaves the maps' names unread");
		/* the list: the searches and the Filters together, those hidden
		counted (the searches' apart); the searches stay as the Filters are
		read again (Filters opened, changed and closed) */
		memset(browser_searches, 0, sizeof(browser_searches));
		browser_filters_read(&browser_filters);
		browser_filtering = TRUE;
		snprintf(browser_searches[_browser_search_server], sizeof(browser_searches[0]), "A");
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* delta alpha "), "\"A\" lists %s", names);
		CHECK(browser_filtered_count == 2 && browser_searched_count == 2, "\"A\" counts %d hidden, %d searched",
			browser_filtered_count, browser_searched_count);
		/* (delta full, which SHOW FULL GAMES: NO hides: counted the Filters') */
		config_set("network.browser_show_full", "false");
		browser_filters_read(&browser_filters);
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* alpha "), "\"A\" and the Filters list %s", names);
		CHECK(browser_filtered_count == 3 && browser_searched_count == 2,
			"\"A\" and the Filters count %d hidden, %d searched", browser_filtered_count, browser_searched_count);
		CHECK(!strcmp(browser_searches[_browser_search_server], "A"), "the Filters read forgot the search");
		snprintf(browser_searches[_browser_search_map], sizeof(browser_searches[0]), "gulch");
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "alpha "), "\"A\" on gulch lists %s", names);
		snprintf(browser_searches[_browser_search_server], sizeof(browser_searches[0]), "zulu");
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!names[0] && browser_searched_count == 4, "\"zulu\" lists %s, %d searched", names,
			browser_searched_count);
		config_set("network.browser_show_full", "true");
		/* (the touchscreen's: the games on Blood Gulch, then bravo by its
		name; full games listed again) */
		browser_filters_read(&browser_filters);
		memset(browser_searches, 0, sizeof(browser_searches));
		snprintf(browser_searches[_browser_search_any], sizeof(browser_searches[0]), "blood gulch");
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "alpha coop ") && browser_searched_count == 3,
			"the touchscreen's \"blood gulch\" lists %s, %d searched", names, browser_searched_count);
		snprintf(browser_searches[_browser_search_any], sizeof(browser_searches[0]), "BRAVO");
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* "), "the touchscreen's \"BRAVO\" lists %s", names);
		browser_filtering = FALSE;
		listed(_browser_tab_all, names, sizeof(names), &offline);
		CHECK(!strcmp(names, "bravo* echo* delta alpha coop "), "unfiltered lists %s", names);
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
