#include "harness.h"

/* A player host's next game and players (port/linux/game/network_host_game.c's
pure parts): the playlist's next entry that can be played, round from its
end, the entry played last's place kept; a ban's and a mute's duration as
the console takes it ("30m", "2h", "7d", "1w", "forever"); and the teams
shuffled (as many on each, drawn from the seed) or balanced (as few moved as
can be; by score where there are scores, each team's total near the
other's), the teams' colours kept for the most players */

#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define csstrcmp strcmp
#define SET_FLAG(flags, bit, on) ((on) ? ((flags) |= FLAG(bit)) : ((flags) &= ~FLAG(bit)))

#include <wchar.h>

#include "config.inc"

/* the variant's parts a change keeps (game_engine.h's, network_game's) */
struct universal_variant
{
	long score_to_win;
	unsigned long flags;
};

struct game_variant
{
	wchar_t human_readable_game_description[12];
	struct universal_variant universal_variant;
};

struct game_variant_options
{
	short time_limit;
};

struct network_game
{
	struct game_variant variant;
	struct game_variant_options variant_options;
};

#include "under_test.inc"

/* a playlist of so many entries, those marked rejected */
static void playlist_of(struct server_rotation *playlist, int count, char const *rejected)
{
	int index;

	memset(playlist, 0, sizeof(*playlist));
	playlist->count = count;
	for (index = 0; index < count; index++)
	{
		if (rejected && rejected[index] == 'x')
			strcpy(playlist->entries[index].rejected, "missing");
	}
}

static void case_playlist(void)
{
	static struct server_rotation playlist;

	playlist_of(&playlist, 0, NULL);
	CHECK(network_host_game_playlist_after(&playlist, NONE) == NONE, "an empty playlist: nothing");
	playlist_of(&playlist, 3, NULL);
	CHECK(network_host_game_playlist_after(&playlist, NONE) == 0, "none played: the first");
	CHECK(network_host_game_playlist_after(&playlist, 0) == 1, "the one after");
	CHECK(network_host_game_playlist_after(&playlist, 2) == 0, "round from the end");
	CHECK(network_host_game_playlist_after(&playlist, 7) == 0, "past a shorter playlist: its first");
	playlist_of(&playlist, 3, ".x.");
	CHECK(network_host_game_playlist_after(&playlist, 0) == 2, "one that can't be played passed over");
	playlist_of(&playlist, 1, NULL);
	CHECK(network_host_game_playlist_after(&playlist, 0) == 0, "one entry: itself again");
	playlist_of(&playlist, 2, "xx");
	CHECK(network_host_game_playlist_after(&playlist, NONE) == NONE, "none can be played: nothing");
}

static void case_duration(void)
{
	CHECK(network_host_game_duration_parse("30m") == 30 * 60, "minutes");
	CHECK(network_host_game_duration_parse("45") == 45 * 60, "a number alone: minutes");
	CHECK(network_host_game_duration_parse("2h") == 2 * 60 * 60, "hours");
	CHECK(network_host_game_duration_parse("7d") == 7 * 24 * 60 * 60, "days");
	CHECK(network_host_game_duration_parse("1w") == 7 * 24 * 60 * 60, "weeks");
	CHECK(network_host_game_duration_parse("forever") == 0, "for ever");
	CHECK(network_host_game_duration_parse("0") == 0, "0: for ever");
	CHECK(network_host_game_duration_parse("griefing") == NONE, "a word is no duration");
	CHECK(network_host_game_duration_parse("2x") == NONE, "an unknown unit");
	CHECK(network_host_game_duration_parse("2hh") == NONE, "a unit and more");
	CHECK(network_host_game_duration_parse("Bob") == NONE, "a name is no duration");
	CHECK(network_host_game_duration_parse("99999w") == 0, "past ten years: for ever");
}

static short count_on(char const *teams, short count, char team)
{
	short index, on = 0;

	for (index = 0; index < count; index++)
		on += teams[index] == team;
	return on;
}

static short moved(char const *teams, char const *current, short count)
{
	short index, changed = 0;

	for (index = 0; index < count; index++)
		changed += teams[index] != current[index];
	return changed;
}

static void case_shuffle(void)
{
	char current[8] = { 0, 0, 0, 0, 0, 0, 1, 1 };
	char teams[8], other[8];
	unsigned long seed;
	boolean differed = FALSE;

	network_host_game_teams_plan(8, NULL, current, _host_game_shuffle, 1234, teams);
	CHECK(count_on(teams, 8, 0) == 4 && count_on(teams, 8, 1) == 4, "as many on each");
	CHECK(moved(teams, current, 8) <= 4, "the colours kept for the most players");
	for (seed = 1; seed < 20; seed++)
	{
		network_host_game_teams_plan(8, NULL, current, _host_game_shuffle, seed, other);
		CHECK(count_on(other, 8, 0) == 4, "as many on each, every draw");
		differed |= memcmp(teams, other, 8) != 0;
	}
	CHECK(differed, "drawn from the seed");
	network_host_game_teams_plan(5, NULL, current, _host_game_shuffle, 99, teams);
	CHECK(count_on(teams, 5, 0) + count_on(teams, 5, 1) == 5 && count_on(teams, 5, 0) >= 2 && count_on(teams, 5, 1) >= 2,
		"an odd count: one more on one");
}

static void case_balance(void)
{
	char current[7] = { 0, 0, 0, 0, 0, 1, 1 };
	char even[6] = { 0, 1, 0, 1, 0, 1 };
	char unteamed[4] = { NONE, NONE, 0, 0 };
	char teams[8];
	long scores[6] = { 30, 25, 20, 2, 1, 0 };
	char stacked[6] = { 0, 0, 0, 1, 1, 1 };
	long totals[2] = { 0, 0 };
	short index;

	network_host_game_teams_plan(7, NULL, current, _host_game_balance, 0, teams);
	CHECK(count_on(teams, 7, 0) == 4 && count_on(teams, 7, 1) == 3, "5 against 2: 4 against 3");
	CHECK(moved(teams, current, 7) == 1, "one moved, no more");
	network_host_game_teams_plan(6, NULL, even, _host_game_balance, 0, teams);
	CHECK(moved(teams, even, 6) == 0, "even teams: nobody moved");
	network_host_game_teams_plan(4, NULL, unteamed, _host_game_balance, 0, teams);
	CHECK(count_on(teams, 4, 0) == 2 && count_on(teams, 4, 1) == 2 && teams[2] == 0 && teams[3] == 0,
		"those on no team to the smaller");
	/* (by score: the best three were together) */
	network_host_game_teams_plan(6, scores, stacked, _host_game_balance, 0, teams);
	CHECK(count_on(teams, 6, 0) == 3, "as many on each, by score too");
	for (index = 0; index < 6; index++)
		totals[(short)teams[index]] += scores[index];
	CHECK(totals[0] - totals[1] <= 15 && totals[1] - totals[0] <= 15, "the scores near even (%ld, %ld)", totals[0],
		totals[1]);
	CHECK(moved(teams, stacked, 6) <= 3, "the colours kept for the most players");
}

/* the lobby after a game: none in co-op (its next level is the campaign's),
a change asked for (a restart's only when asked for now), the playlist's
next but on a restart */
static void case_takes(void)
{
	CHECK(network_host_game_lobby_takes(TRUE, TRUE, TRUE, FALSE) == 0, "co-op: nothing over its next level");
	CHECK(network_host_game_lobby_takes(TRUE, FALSE, FALSE, FALSE) == 0, "co-op: no playlist either");
	CHECK(network_host_game_lobby_takes(FALSE, TRUE, FALSE, FALSE) ==
		(FLAG(_host_game_take_change) | FLAG(_host_game_take_playlist)), "a change, the playlist behind it");
	CHECK(network_host_game_lobby_takes(FALSE, FALSE, FALSE, FALSE) == FLAG(_host_game_take_playlist),
		"no change: the playlist");
	CHECK(network_host_game_lobby_takes(FALSE, FALSE, FALSE, TRUE) == 0, "a restart replays its game");
	CHECK(network_host_game_lobby_takes(FALSE, TRUE, FALSE, TRUE) == 0, "a restart keeps a change for after it");
	CHECK(network_host_game_lobby_takes(FALSE, TRUE, TRUE, TRUE) == FLAG(_host_game_take_change),
		"a change asked for now: taken by its restart");
}

/* the entries playable() was asked of, and which it takes ('y') */
static char const *takes_these;
static int asked;

static boolean playable(struct server_entry const *entry, void *context)
{
	(void)context;
	asked++;
	/* (a try that goes round for ever stopped, for its check to see it) */
	if (asked > 64)
		return TRUE;
	return takes_these[entry->rule_count] == 'y';
}

static void playlist_numbered(struct server_rotation *playlist, int count)
{
	int index;

	playlist_of(playlist, count, NULL);
	for (index = 0; index < count; index++)
		playlist->entries[index].rule_count = index;
}

static void case_try(void)
{
	static struct server_rotation playlist;

	playlist_numbered(&playlist, 4);
	takes_these = "yyyy";
	asked = 0;
	CHECK(network_host_game_playlist_try(&playlist, 0, playable, NULL) == 1 && asked == 1, "the next, taken");
	takes_these = "yn.y";
	asked = 0;
	CHECK(network_host_game_playlist_try(&playlist, 0, playable, NULL) == 3, "past those it can't play");
	takes_these = "nnnn";
	asked = 0;
	CHECK(network_host_game_playlist_try(&playlist, 0, playable, NULL) == NONE && asked == 4,
		"none it can play: each tried once, none taken");
	playlist_of(&playlist, 0, NULL);
	CHECK(network_host_game_playlist_try(&playlist, NONE, playable, NULL) == NONE, "an empty playlist");
}

static void case_fits(void)
{
	static struct server_rotation playlist;
	char entry[SERVER_ENTRY_TEXT_SIZE];
	int index;

	memset(entry, 'a', sizeof(entry) - 1);
	entry[sizeof(entry) - 1] = 0;
	playlist_of(&playlist, 0, NULL);
	CHECK(network_host_game_playlist_fits(&playlist, "bloodgulch:ctf"), "an empty one takes an entry");
	/* (long entries: the text full before the entries) */
	for (index = 0; network_host_game_playlist_fits(&playlist, entry); index++)
	{
		strcpy(playlist.entries[playlist.count].text, entry);
		playlist.count++;
		CHECK(index < SERVER_ROTATION_MAXIMUM_ENTRIES, "no end");
	}
	{
		long length = 0;

		for (index = 0; index < playlist.count; index++)
			length += (long)strlen(playlist.entries[index].text) + 2;
		CHECK(length - 2 < SERVER_ROTATION_MAXIMUM_TEXT, "its text kept whole (%ld)", length);
		CHECK(playlist.count < SERVER_ROTATION_MAXIMUM_ENTRIES, "the text full first");
	}
	/* (short entries: the entries full) */
	playlist_of(&playlist, SERVER_ROTATION_MAXIMUM_ENTRIES - 1, NULL);
	CHECK(network_host_game_playlist_fits(&playlist, "a"), "one more");
	playlist.count = SERVER_ROTATION_MAXIMUM_ENTRIES;
	CHECK(!network_host_game_playlist_fits(&playlist, "a"), "no more than the entries");
}

static void case_ban(void)
{
	char name[64], reason[96];
	long seconds;

	CHECK(network_host_game_ban_parse("\"Some Name\" 2h spam in chat)", name, sizeof(name), &seconds, reason,
		sizeof(reason)), "a quoted name");
	CHECK(!strcmp(name, "Some Name") && seconds == 2 * 60 * 60 && !strcmp(reason, "spam in chat"),
		"its name, length and reason: \"%s\" %ld \"%s\"", name, seconds, reason);
	CHECK(network_host_game_ban_parse("Player 2 7d \"griefing, twice\" )", name, sizeof(name), &seconds, reason,
		sizeof(reason)) && !strcmp(name, "Player 2") && seconds == 7 * 24 * 60 * 60 &&
		!strcmp(reason, "griefing, twice"), "a name with a number, a quoted reason: \"%s\" \"%s\"", name, reason);
	CHECK(network_host_game_ban_parse("Bob", name, sizeof(name), &seconds, reason, sizeof(reason)) &&
		!strcmp(name, "Bob") && seconds == 0 && !reason[0], "a name alone: for ever");
	CHECK(network_host_game_ban_parse("\"Bob\" forever cheating", name, sizeof(name), &seconds, reason,
		sizeof(reason)) && seconds == 0 && !strcmp(reason, "cheating"), "for ever, with why");
	CHECK(!network_host_game_ban_parse("\"Bob", name, sizeof(name), &seconds, reason, sizeof(reason)),
		"a quote left open");
	CHECK(!network_host_game_ban_parse(")", name, sizeof(name), &seconds, reason, sizeof(reason)), "no name");
}

static void case_saved(void)
{
	char key[58];

	CHECK(server_gametype_saved_key(L"My CTF  #2", key, sizeof(key)) && !strcmp(key, "my_ctf_2"), "%s", key);
	CHECK(server_gametype_saved_key(L"-- Rockets! --", key, sizeof(key)) && !strcmp(key, "rockets"), "%s", key);
	CHECK(!server_gametype_saved_key(L"!!!", key, sizeof(key)) && !key[0], "none of letters or digits");
	CHECK(server_gametype_saved_key(L"abcdefghij", key, 6) && !strcmp(key, "abcde"), "kept to its size");
}

/* a change of map alone (MAPS' CURRENT, host_map without a gametype)
keeps the lobby's gametype, score to win, time limit and rules; a gametype
named is built (its own defaults) */
static void case_kept(void)
{
	struct server_entry entry;
	struct network_game game;
	struct game_variant variant;
	struct game_variant_options options;

	CHECK(network_host_game_entry_keeps("bloodgulch"), "a map alone keeps the game's gametype");
	CHECK(network_host_game_entry_keeps("custom_maps\\hugeass"), "a Custom Edition map alone too");
	CHECK(!network_host_game_entry_keeps("bloodgulch:ctf"), "a gametype named is played");
	CHECK(!network_host_game_entry_keeps("bloodgulch:slayer+score=5"), "with its rules");
	memset(&game, 0, sizeof(game));
	wcscpy(game.variant.human_readable_game_description, L"CTF");
	game.variant.universal_variant.score_to_win = 7;
	game.variant.universal_variant.flags = 0x15;
	game.variant_options.time_limit = 12;
	memset(&entry, 0, sizeof(entry));
	memset(&variant, 0, sizeof(variant));
	memset(&options, 0, sizeof(options));
	/* (network_host_game_change empties a map alone's gametype) */
	CHECK(network_host_game_variant_kept(&entry, &game, &variant, &options), "a map alone keeps the lobby's");
	CHECK(variant.universal_variant.score_to_win == 7 && options.time_limit == 12, "score %ld, time %d kept",
		variant.universal_variant.score_to_win, (int)options.time_limit);
	CHECK(variant.universal_variant.flags == 0x15, "the rules' flags kept: %lx", variant.universal_variant.flags);
	CHECK(!wcscmp(variant.human_readable_game_description, L"CTF"), "the gametype kept");
	memset(&variant, 0, sizeof(variant));
	memset(&options, 0, sizeof(options));
	strcpy(entry.gametype, "slayer");
	CHECK(!network_host_game_variant_kept(&entry, &game, &variant, &options), "a gametype named is built");
	CHECK(variant.universal_variant.score_to_win == 0 && options.time_limit == 0, "nothing of the lobby's taken");
	entry.gametype[0] = 0;
	CHECK(!network_host_game_variant_kept(&entry, NULL, &variant, &options), "no lobby's game: none kept");
}

/* MAPS' gametype of the list the game's is, by its name */
static void case_find(void)
{
	static struct host_game_gametype gametypes[3];

	memset(gametypes, 0, sizeof(gametypes));
	wcscpy(gametypes[0].shown, L"Slayer");
	wcscpy(gametypes[1].shown, L"CTF");
	wcscpy(gametypes[2].shown, L"Oddball");
	CHECK(network_host_game_gametype_find(gametypes, 3, L"CTF") == 1, "the game's CTF");
	CHECK(network_host_game_gametype_find(gametypes, 3, L"Oddball") == 2, "the game's Oddball");
	CHECK(network_host_game_gametype_find(gametypes, 3, L"Slayer Pro") == NONE, "a longer name is none of them");
	CHECK(network_host_game_gametype_find(gametypes, 3, L"Team Slayer") == NONE, "not in the list: none");
	CHECK(network_host_game_gametype_find(gametypes, 3, L"") == NONE, "no name: none");
	CHECK(network_host_game_gametype_find(gametypes, 0, L"CTF") == NONE, "an empty list: none");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	if (!strcmp(case_name, "takes"))
		case_takes();
	else if (!strcmp(case_name, "try"))
		case_try();
	else if (!strcmp(case_name, "fits"))
		case_fits();
	else if (!strcmp(case_name, "ban"))
		case_ban();
	else if (!strcmp(case_name, "saved"))
		case_saved();
	else if (!strcmp(case_name, "kept"))
		case_kept();
	else if (!strcmp(case_name, "find"))
		case_find();
	else

	if (!strcmp(case_name, "playlist"))
		case_playlist();
	else if (!strcmp(case_name, "duration"))
		case_duration();
	else if (!strcmp(case_name, "shuffle"))
		case_shuffle();
	else if (!strcmp(case_name, "balance"))
		case_balance();
	else
		return 2;
	return 0;
}
