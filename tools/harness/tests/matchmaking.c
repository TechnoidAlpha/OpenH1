#include "harness.h"

/* Matchmaking's decisions (port/linux/game/matchmaking.c): which public
game a party takes (matchmaking_game_score, matchmaking_pick: its playlist's,
an OpenH1 game of this build, open, no password, room for the whole party,
not failed; by ping in steps, then the fullest, a lobby first), who may
search (matchmaking_search_problem), how a hosted game starts
(matchmaking_start_choice: full at once, else after a wait with bots or with
its least players), and a search's steps (matchmaking_step: looked for,
reached, joined, failed and looked again, the lead passed or hosted when
none fits, the new leader followed), and the playlists (matchmaking_playlists:
listed by their numbers, which every build of a network version must agree
on, so a new one goes last; teams a party fits on; a screen's row each) */

#include <wchar.h>

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))

#include "config.inc"

/* (the hosting HOST GAME asks for: recorded, as matchmaking.c's would make
the server; and the stage set) */
static boolean matchmaking_host(void)
{
	stub_hosts++;
	stub_hosted_playlist = matchmaking.playlist;
	return stub_hosted_ok;
}

static void matchmaking_stage(short stage)
{
	matchmaking.stage = stage;
}

#include "under_test.inc"

static struct p2p_listing game(short playlist, short players, short most, short ping)
{
	struct p2p_listing listing;

	memset(&listing, 0, sizeof(listing));
	strcpy(listing.invite, "openh1://join/00");
	listing.playlist = (unsigned char)playlist;
	listing.player_count = (unsigned char)players;
	listing.maximum_player_count = (unsigned char)most;
	listing.ping = ping;
	listing.standard = 1;
	listing.open = 1;
	listing.holds_party = 1;
	return listing;
}

static void case_pick(void)
{
	struct p2p_listing games[8];

	games[0] = game(1, 2, 8, 40);
	CHECK(matchmaking_game_score(&games[0], 1, 3) != NONE, "a Team Slayer game with room for 3 takes the party");
	CHECK(matchmaking_game_score(&games[0], 2, 1) == NONE, "not one looking for another playlist");
	CHECK(matchmaking_game_score(&games[0], 0, 1) == NONE, "nor one looking for no playlist");
	games[1] = game(0, 2, 8, 40);
	CHECK(matchmaking_game_score(&games[1], 1, 1) == NONE, "a game of no playlist is never picked");
	games[1] = game(1, 6, 8, 40);
	CHECK(matchmaking_game_score(&games[1], 1, 2) != NONE && matchmaking_game_score(&games[1], 1, 3) == NONE,
		"room for 2, not 3");
	games[1].standard = 0;
	CHECK(matchmaking_game_score(&games[1], 1, 1) == NONE, "a Legacy game (or another protocol's) is never picked");
	games[1] = game(1, 2, 8, 40);
	games[1].open = 0;
	CHECK(matchmaking_game_score(&games[1], 1, 1) == NONE, "nor a closed one");
	games[1] = game(1, 2, 8, 40);
	games[1].locked = 1;
	CHECK(matchmaking_game_score(&games[1], 1, 1) == NONE, "nor one with a password");
	games[1] = game(1, 2, 8, 40);
	games[1].failed = 1;
	CHECK(matchmaking_game_score(&games[1], 1, 1) == NONE, "nor one this machine failed to join");
	games[1] = game(1, 2, 8, 40);
	games[1].invite[0] = 0;
	CHECK(matchmaking_game_score(&games[1], 1, 1) == NONE, "nor one with no invite");
	/* by ping, then the fullest */
	games[0] = game(1, 2, 8, 120);
	games[1] = game(1, 1, 8, 30);
	games[2] = game(2, 7, 8, 5);
	games[3] = game(1, 6, 8, 20);
	CHECK(matchmaking_pick(games, 4, 1, 2) == 3, "the nearest with room (not another playlist's)");
	CHECK(matchmaking_pick(games, 4, 1, 3) == 1, "the nearest with room for 3");
	games[3] = game(1, 3, 8, 35);
	CHECK(matchmaking_pick(games, 4, 1, 2) == 3, "of pings in one step, the fuller");
	games[3].in_progress = 1;
	games[1] = game(1, 3, 8, 30);
	CHECK(matchmaking_pick(games, 4, 1, 2) == 1, "and a lobby before a game under way");
	games[1].ping = -1;
	games[3].ping = -1;
	games[0].ping = 300;
	CHECK(matchmaking_pick(games, 4, 1, 2) == 1, "a ping not measured yet is taken as MATCHMAKING_UNKNOWN_PING");
	games[0] = game(1, 2, 8, 100);
	games[1] = game(1, 2, 8, -1);
	CHECK(matchmaking_pick(games, 2, 1, 2) == 0, "a game measured at 100 ms before one not measured");
	CHECK(matchmaking_pick(games, 4, 5, 1) == NONE, "none for a playlist no game has");
	CHECK(matchmaking_pick(games, 0, 1, 1) == NONE, "none of none");
}

static void case_problem(void)
{
	CHECK(matchmaking_search_problem(FALSE, FALSE, 1, 4) == _matchmaking_problem_offline, "internet play off");
	CHECK(matchmaking_search_problem(TRUE, TRUE, 3, 4) == _matchmaking_problem_member, "a member: its leader searches");
	CHECK(matchmaking_search_problem(TRUE, FALSE, 5, 4) == _matchmaking_problem_party_too_big, "a party of 5 for 4");
	CHECK(matchmaking_search_problem(TRUE, FALSE, 4, 4) == _matchmaking_problem_none, "a party of 4 for 4");
	CHECK(matchmaking_search_problem(TRUE, FALSE, 1, 2) == _matchmaking_problem_none, "a player in no party");
}

static void case_start(void)
{
	CHECK(matchmaking_start_choice(8, 8, 2, 0, TRUE) == _matchmaking_start_now, "full: at once");
	CHECK(matchmaking_start_choice(3, 8, 2, MATCHMAKING_FILL_MILLISECONDS - 1, TRUE) == _matchmaking_start_wait,
		"waiting for players");
	CHECK(matchmaking_start_choice(3, 8, 2, MATCHMAKING_FILL_MILLISECONDS, TRUE) == _matchmaking_start_bots,
		"then bots fill it");
	CHECK(matchmaking_start_choice(1, 8, 2, MATCHMAKING_FILL_MILLISECONDS, TRUE) == _matchmaking_start_bots,
		"one player and bots");
	CHECK(matchmaking_start_choice(3, 8, 2, MATCHMAKING_FILL_MILLISECONDS, FALSE) == _matchmaking_start_now,
		"a gametype bots do not play: with the players there are");
	CHECK(matchmaking_start_choice(1, 8, 2, MATCHMAKING_FILL_MILLISECONDS * 3, FALSE) == _matchmaking_start_wait,
		"but not fewer than its least");
	CHECK(matchmaking_start_choice(0, 0, 2, 0, FALSE) == _matchmaking_start_wait, "no most known: not full");
	/* Doubles': 4 at most */
	CHECK(matchmaking_start_choice(4, 4, 2, 0, TRUE) == _matchmaking_start_now, "Doubles full at 4");
	CHECK(matchmaking_start_choice(2, 4, 2, MATCHMAKING_FILL_MILLISECONDS, TRUE) == _matchmaking_start_bots,
		"a party of two, and two bots");
}

static short step(struct matchmaking_view *view, short *action)
{
	return matchmaking_step(view, action);
}

static struct matchmaking_view looking(unsigned long searched, short best)
{
	struct matchmaking_view view;

	memset(&view, 0, sizeof(view));
	view.stage = _matchmaking_looking;
	view.searched = searched;
	view.search_time = 30000;
	view.best = best;
	view.leading = TRUE;
	view.join_stage = _p2p_join_idle;
	return view;
}

static void case_look(void)
{
	struct matchmaking_view view;
	short action;

	view = looking(1000, 2);
	CHECK(step(&view, &action) == _matchmaking_looking && action == _matchmaking_action_none,
		"a game found at once waits for the others' listings and pings");
	view = looking(MATCHMAKING_LOOK_MILLISECONDS, 2);
	CHECK(step(&view, &action) == _matchmaking_reaching && action == _matchmaking_action_reach, "then reached");
	view = looking(20000, NONE);
	CHECK(step(&view, &action) == _matchmaking_looking && action == _matchmaking_action_none, "none yet: looking");
	view = looking(30000, NONE);
	CHECK(step(&view, &action) == _matchmaking_handing && action == _matchmaking_action_pass,
		"none in the search's time: the lead passed to a better host, or hosted");
	view = looking(30000, 1);
	CHECK(step(&view, &action) == _matchmaking_reaching, "a game found as the time runs out is taken");
	view = looking(1000, NONE);
	view.leading = FALSE;
	CHECK(step(&view, &action) == _matchmaking_following, "the lead taken by another: its game followed");
}

static void case_join(void)
{
	struct matchmaking_view view;
	short action;

	memset(&view, 0, sizeof(view));
	view.stage = _matchmaking_reaching;
	view.leading = TRUE;
	view.waited = 500;
	view.join_stage = _p2p_join_finding;
	CHECK(step(&view, &action) == _matchmaking_reaching && action == _matchmaking_action_none, "finding the host");
	view.join_stage = _p2p_join_reached;
	CHECK(step(&view, &action) == _matchmaking_reaching && action == _matchmaking_action_join,
		"reached: its game joined (once it is among the client's)");
	view.entered = TRUE;
	CHECK(step(&view, &action) == _matchmaking_joining, "joined: waiting to be in it");
	view.entered = FALSE;
	view.waited = MATCHMAKING_REACH_MILLISECONDS + 1;
	CHECK(step(&view, &action) == _matchmaking_looking && action == _matchmaking_action_failed,
		"its game never found: failed, looked for again");
	view.join_stage = _p2p_join_idle;
	view.waited = 1500;
	CHECK(step(&view, &action) == _matchmaking_looking && action == _matchmaking_action_failed,
		"the join given up: failed");
	view.join_stage = _p2p_join_opening;
	view.waited = MATCHMAKING_REACH_MILLISECONDS + 1;
	view.following = TRUE;
	CHECK(step(&view, &action) == _matchmaking_following && action == _matchmaking_action_failed,
		"a leader's game not reached: back to following");
	memset(&view, 0, sizeof(view));
	view.stage = _matchmaking_joining;
	view.waited = 1000;
	view.in_none = TRUE;
	CHECK(step(&view, &action) == _matchmaking_joining, "between the join and the lobby a moment");
	view.in_none = FALSE;
	view.in_game = TRUE;
	CHECK(step(&view, &action) == _matchmaking_idle && action == _matchmaking_action_done, "in the game: done");
	view.in_game = FALSE;
	view.in_none = TRUE;
	view.waited = 2500;
	CHECK(step(&view, &action) == _matchmaking_looking && action == _matchmaking_action_failed,
		"turned away: looked for again");
	view.in_none = FALSE;
	view.waited = MATCHMAKING_JOIN_MILLISECONDS + 1;
	CHECK(step(&view, &action) == _matchmaking_looking && action == _matchmaking_action_failed, "never in it");
}

static void case_host(void)
{
	struct matchmaking_view view;
	short action;

	memset(&view, 0, sizeof(view));
	view.stage = _matchmaking_handing;
	view.leading = TRUE;
	view.waited = 1000;
	CHECK(step(&view, &action) == _matchmaking_handing, "waiting for the member asked to host to lead");
	view.leading = FALSE;
	CHECK(step(&view, &action) == _matchmaking_following, "it leads: its game followed");
	view.leading = TRUE;
	view.waited = MATCHMAKING_HANDOFF_MILLISECONDS + 1;
	CHECK(step(&view, &action) == _matchmaking_hosting && action == _matchmaking_action_host,
		"it never did: hosted here");
	memset(&view, 0, sizeof(view));
	view.stage = _matchmaking_following;
	CHECK(step(&view, &action) == _matchmaking_following, "following: the new leader's game not there yet");
	view.leader_game = TRUE;
	CHECK(step(&view, &action) == _matchmaking_reaching && action == _matchmaking_action_follow, "then reached");
	view.leader_game = FALSE;
	view.waited = MATCHMAKING_FOLLOW_MILLISECONDS + 1;
	CHECK(step(&view, &action) == _matchmaking_idle && action == _matchmaking_action_give_up,
		"the new leader hosting nothing: the search given up");
	view.waited = 0;
	view.leading = TRUE;
	CHECK(step(&view, &action) == _matchmaking_looking, "the lead back here: looked for again");
	view.leading = FALSE;
	view.in_game = TRUE;
	CHECK(step(&view, &action) == _matchmaking_idle && action == _matchmaking_action_done, "in its game: done");
	memset(&view, 0, sizeof(view));
	view.stage = _matchmaking_hosting;
	view.hosting = TRUE;
	view.waited = MATCHMAKING_HOST_MILLISECONDS * 10;
	CHECK(step(&view, &action) == _matchmaking_hosting, "hosting until the game starts");
	view.hosting = FALSE;
	view.waited = 1000;
	CHECK(step(&view, &action) == _matchmaking_hosting, "the server a moment coming");
	view.waited = MATCHMAKING_HOST_MILLISECONDS + 1;
	CHECK(step(&view, &action) == _matchmaking_idle && action == _matchmaking_action_give_up,
		"a server that never came: given up");
}

static void case_playlists(void)
{
	/* the listings' numbers (1 up) of the playlists, as the builds before
	knew them (the sixth, 2v2 Low Latency before, is the same kind renamed
	Crispy): a playlist is only ever added after these */
	static char const *const listed[] = { "Team Slayer", "Slayer", "CTF", "SWAT", "Big Team", "2v2 Crispy",
		"2v2 Classic" };
	/* the Xbox's multiplayer maps */
	static char const *const maps[] = { "beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout",
		"damnation", "hangemhigh", "longest", "prisoner", "putput", "ratrace", "sidewinder", "wizard" };
	long index;

	CHECK(NUMBEROF(matchmaking_playlists) >= NUMBEROF(listed), "no playlist taken out");
	for (index = 0; index < NUMBEROF(listed); index++)
	{
		CHECK(!strcmp(matchmaking_playlists[index].name, listed[index]), "playlist %ld is still %s (it is %s)",
			index + 1, listed[index], matchmaking_playlists[index].name);
	}
	for (index = 0; index < NUMBEROF(matchmaking_playlists); index++)
	{
		struct matchmaking_playlist const *playlist = &matchmaking_playlists[index];
		long map, known;

		CHECK(strlen(playlist->name) < MATCHMAKING_PLAYLIST_NAME_SIZE, "%s: its name fits", playlist->name);
		CHECK(playlist->variants[0] && playlist->maps[0], "%s: a gametype and a map", playlist->name);
		CHECK(playlist->minimum >= 2 && playlist->minimum <= playlist->players, "%s: its least players",
			playlist->name);
		CHECK(playlist->party_most >= 1 && playlist->party_most <= playlist->players, "%s: its party's most",
			playlist->name);
		if (playlist->team_size)
		{
			CHECK(playlist->players == playlist->team_size * 2, "%s: two teams of %d", playlist->name,
				playlist->team_size);
			/* (a team holds half the game's most: network_party.c's
			party_team_choice) */
			CHECK(playlist->party_most <= (playlist->players + 1) / 2, "%s: a party fits on one team",
				playlist->name);
		}
		for (map = 0; map < MATCHMAKING_MAXIMUM_MAPS && playlist->maps[map]; map++)
		{
			for (known = 0; known < NUMBEROF(maps) && strcmp(playlist->maps[map], maps[known]); known++)
				;
			CHECK(known < NUMBEROF(maps), "%s: %s is a multiplayer map", playlist->name, playlist->maps[map]);
		}
	}
}

/* 2V2's kinds: each its own playlist, both at 120, Classic's aim off for
everyone, the same rules otherwise, and a search of one never takes a game
of the other's: told apart by the listing's playlist alone */
static void case_doubles(void)
{
	short classic = matchmaking_doubles_playlists[_matchmaking_doubles_classic];
	short crispy = matchmaking_doubles_playlists[_matchmaking_doubles_crispy];
	struct matchmaking_playlist const *a = &matchmaking_playlists[classic];
	struct matchmaking_playlist const *b = &matchmaking_playlists[crispy];
	struct p2p_listing games[4];
	long index;

	CHECK(classic == 6 && crispy == 5, "Classic still playlist 7, Crispy 6 (1 up)");
	CHECK(classic != crispy && classic >= 0 && classic < NUMBEROF(matchmaking_playlists) && crispy >= 0 &&
		crispy < NUMBEROF(matchmaking_playlists), "two playlists of their own");
	CHECK(a->rate == 120 && a->classic_aim, "Classic: OpenH1 120 Tick, Instant Aim off for everyone");
	CHECK(b->rate == 120 && !b->classic_aim, "Crispy: OpenH1 120 Tick, each player's own aim");
	CHECK(!wcscmp(a->title, L"PRO 2V2 CLASSIC") && !wcscmp(b->title, L"PRO 2V2 CRISPY"), "their titles");
	CHECK(a->team_size == 2 && a->players == 4 && a->party_most == 2, "Classic: 2 against 2, a party of 2 at most");
	CHECK(b->team_size == a->team_size && b->players == a->players && b->minimum == a->minimum &&
		b->party_most == a->party_most, "the same teams");
	CHECK(!strcmp(a->variants[0], "pro_doubles") && !strcmp(b->variants[0], "pro_doubles") && !a->variants[1] &&
		!b->variants[1], "both play Pro Doubles alone");
	for (index = 0; index < MATCHMAKING_MAXIMUM_MAPS; index++)
	{
		CHECK((!a->maps[index] && !b->maps[index]) || (a->maps[index] && b->maps[index] &&
			!strcmp(a->maps[index], b->maps[index])), "the same maps (map %ld)", index);
	}
	for (index = 0; index < NUMBEROF(matchmaking_playlists); index++)
	{
		if (index != classic && index != crispy)
			CHECK(matchmaking_playlists[index].rate == 0 && !matchmaking_playlists[index].classic_aim,
				"%s: the host's own rate and aim", matchmaking_playlists[index].name);
	}
	/* (listings stamp the playlist 1 up) */
	games[0] = game((short)(classic + 1), 1, 4, 30);
	games[0].rate = 120;
	games[1] = game((short)(crispy + 1), 1, 4, 30);
	games[1].rate = 120;
	CHECK(matchmaking_game_score(&games[0], (short)(classic + 1), 2) != NONE, "Classic takes a Classic game at 120");
	CHECK(matchmaking_game_score(&games[1], (short)(crispy + 1), 2) != NONE, "Crispy takes a Crispy game at 120");
	CHECK(matchmaking_game_score(&games[1], (short)(classic + 1), 1) == NONE &&
		matchmaking_game_score(&games[0], (short)(crispy + 1), 1) == NONE,
		"nor the other kind's game, at the same rate");
	/* (a game stamped for a kind but at another rate: a host whose rate was
	not the playlist's; Classic's at 30 was a build's before protocol
	version 27, which this build never hears: p2p_lobby.c's version_ours) */
	games[2] = game((short)(classic + 1), 1, 4, 10);
	games[2].rate = 30;
	games[3] = game((short)(crispy + 1), 1, 4, 10);
	games[3].rate = 60;
	CHECK(matchmaking_game_score(&games[2], (short)(classic + 1), 1) == NONE,
		"Classic never takes a 30 Tick game, even stamped Classic");
	CHECK(matchmaking_game_score(&games[3], (short)(crispy + 1), 1) == NONE, "Crispy never takes a 60 Tick one");
	games[3].rate = 30;
	CHECK(matchmaking_game_score(&games[3], (short)(crispy + 1), 1) == NONE, "nor a 30 Tick one");
	CHECK(matchmaking_pick(games, 4, (short)(classic + 1), 1) == 0, "Classic's pick: its own, not the nearer others");
	CHECK(matchmaking_pick(games, 4, (short)(crispy + 1), 1) == 1, "Crispy's: its own");
	games[0].rate = 0;
	CHECK(matchmaking_game_score(&games[0], (short)(classic + 1), 1) == NONE, "nor a game of no rate said");
}

/* the 2V2 browser's list (matchmaking_playlist_games): of every public game,
the kind's alone, in their order: never the other kind's (at the same rate),
a Legacy game, a game of the kind's playlist at another rate, or another
playlist's */
static void case_browser(void)
{
	short classic = matchmaking_doubles_playlists[_matchmaking_doubles_classic];
	short crispy = matchmaking_doubles_playlists[_matchmaking_doubles_crispy];
	struct p2p_listing games[8], copy[8];
	short count;

	games[0] = game((short)(classic + 1), 2, 4, 40);
	games[0].rate = 120;
	games[1] = game((short)(crispy + 1), 2, 4, 30);
	games[1].rate = 120;
	/* (a Legacy game stamped as the kind's: a Legacy listing has no playlist,
	p2p_lobby.c's, but the browser does not lean on it; at the kind's rate,
	so that only its being Legacy keeps it out) */
	games[2] = game((short)(classic + 1), 1, 4, 20);
	games[2].rate = 120;
	games[2].standard = 0;
	games[3] = game((short)(classic + 1), 1, 4, 20);
	games[3].rate = 30;
	games[4] = game(1, 3, 8, 20);
	games[5] = game(0, 3, 4, 20);
	games[5].rate = 120;
	games[6] = game((short)(classic + 1), 3, 4, 90);
	games[6].rate = 120;
	games[6].locked = 1;
	games[7] = game((short)(crispy + 1), 4, 4, 10);
	games[7].rate = 120;
	games[7].in_progress = 1;
	memcpy(copy, games, sizeof(games));
	count = matchmaking_playlist_games(copy, 8, classic);
	CHECK(count == 2 && copy[0].ping == 40 && copy[1].ping == 90,
		"Classic's browser lists its two games, in order (it listed %d)", count);
	CHECK(copy[0].playlist == classic + 1 && copy[1].playlist == classic + 1 && copy[0].rate == 120 &&
		copy[1].rate == 120 && copy[0].standard && copy[1].standard,
		"never a Crispy game, a 30 Tick one nor a Legacy one");
	memcpy(copy, games, sizeof(games));
	count = matchmaking_playlist_games(copy, 8, crispy);
	CHECK(count == 2 && copy[0].ping == 30 && copy[1].ping == 10 && copy[0].playlist == crispy + 1 &&
		copy[1].playlist == crispy + 1,
		"Crispy's lists its two (full and under way too: the browser shows them as they are), never Classic's");
	memcpy(copy, games, sizeof(games));
	count = matchmaking_playlist_games(copy, 8, 0);
	CHECK(count == 1 && copy[0].playlist == 1, "Team Slayer's (no rate of its own) lists its game");
	CHECK(matchmaking_playlist_games(copy, 0, classic) == 0, "none of none");
}

/* HOST GAME (matchmaking_host_playlist): the kind's game hosted at once, as a
search that finds none hosts it; refused, and why, offline, without its
maps, a party's member, while a search goes on */
static void case_host_action(void)
{
	short classic = matchmaking_doubles_playlists[_matchmaking_doubles_classic];
	short crispy = matchmaking_doubles_playlists[_matchmaking_doubles_crispy];

	matchmaking.stage = _matchmaking_idle;
	CHECK(matchmaking_host_playlist(classic, 1) && stub_hosts == 1 && stub_hosted_playlist == classic &&
		matchmaking.stage == _matchmaking_hosting && matchmaking.controller == 1,
		"Classic's HOST GAME hosts its playlist (its rate, gametype and maps: matchmaking_host)");
	CHECK(!matchmaking_host_playlist(crispy, 0) && stub_hosts == 1, "not over a game being hosted");
	matchmaking.stage = _matchmaking_looking;
	CHECK(!matchmaking_host_playlist(crispy, 0) && stub_hosts == 1, "nor over a search, which hosts by itself");
	matchmaking.stage = _matchmaking_idle;
	stub_online = FALSE;
	CHECK(!matchmaking_host_playlist(crispy, 0) && matchmaking.problem == _matchmaking_problem_offline &&
		stub_hosts == 1, "offline: refused, and why");
	stub_online = TRUE;
	stub_maps = 0;
	CHECK(!matchmaking_host_playlist(crispy, 0) && matchmaking.problem == _matchmaking_problem_no_maps &&
		stub_hosts == 1, "none of its maps: refused, and why");
	stub_maps = 5;
	stub_member = TRUE;
	stub_party_size = 2;
	CHECK(!matchmaking_host_playlist(crispy, 0) && matchmaking.problem == _matchmaking_problem_member &&
		stub_hosts == 1, "a party's member: its leader hosts");
	stub_member = FALSE;
	stub_party_size = 3;
	CHECK(!matchmaking_host_playlist(crispy, 0) && matchmaking.problem == _matchmaking_problem_party_too_big,
		"a party of 3 for 2 against 2: refused");
	stub_party_size = 2;
	CHECK(matchmaking_host_playlist(crispy, 0) && stub_hosted_playlist == crispy &&
		matchmaking.stage == _matchmaking_hosting, "a party of two's leader hosts Crispy's");
	matchmaking.stage = _matchmaking_idle;
	stub_hosted_ok = FALSE;
	CHECK(!matchmaking_host_playlist(classic, 0) && matchmaking.stage == _matchmaking_idle,
		"a host that could not be made: not hosting");
	CHECK(!matchmaking_host_playlist(NUMBEROF(matchmaking_playlists), 0), "no such playlist");
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	if (!strcmp(case_name, "pick"))
		case_pick();
	else if (!strcmp(case_name, "problem"))
		case_problem();
	else if (!strcmp(case_name, "start"))
		case_start();
	else if (!strcmp(case_name, "look"))
		case_look();
	else if (!strcmp(case_name, "join"))
		case_join();
	else if (!strcmp(case_name, "host"))
		case_host();
	else if (!strcmp(case_name, "playlists"))
		case_playlists();
	else if (!strcmp(case_name, "doubles"))
		case_doubles();
	else if (!strcmp(case_name, "browser"))
		case_browser();
	else if (!strcmp(case_name, "host_action"))
		case_host_action();
	else
		return 2;
	return 0;
}
