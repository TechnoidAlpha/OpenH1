"""Matchmaking's decisions (port/linux/game/matchmaking.c): the game a party takes from the public listings (its
playlist's, an OpenH1 game of this build, open, no password, room for the whole party, by ping and then the fullest),
who may search, how a hosted game starts (full at once, else after a wait with bots or its least players), and a
search's steps (looked for, reached, joined, failed and looked again, the lead passed or hosted when none fits, the new
leader followed), and the playlists themselves (each listed by its number, which builds must agree on; teams that a
party fits on), and 2V2's kinds (Classic and Crispy: each its own
playlist, both at 120, never matched with the other's games). The party's host election is tools/p2p_party_check.c's "host" case; the listing's playlist
p2p_lobby_check.c's."""

import re

import pytest

from tools.harness import build, enum_with, function, mutated, read, run, structure

CASES = ["pick", "problem", "start", "look", "join", "host", "playlists", "doubles", "browser", "host_action"]
FUNCTIONS = ["matchmaking_listing_of", "matchmaking_game_score", "matchmaking_pick", "matchmaking_search_problem",
             "matchmaking_start_choice", "matchmaking_step", "matchmaking_playlist_games", "matchmaking_can_search",
             "matchmaking_host_playlist"]


def generated(fault=None):
    p2p = read("port/linux/src/p2p.h")
    header = read("port/linux/game/matchmaking.h")
    source = read("port/linux/game/matchmaking.c")
    config = enum_with(p2p, "P2P_LISTING_NAME_SIZE") + "\n" + structure(p2p, "p2p_listing") + "\n"
    config += enum_with(p2p, "_p2p_join_reached") + "\n"
    config += enum_with(header, "_matchmaking_looking") + "\n" + enum_with(header, "_matchmaking_problem_member") + "\n"
    config += enum_with(header, "_matchmaking_doubles_classic") + "\n"
    for member in ("MATCHMAKING_MAXIMUM_MAPS = ", "_matchmaking_action_reach,", "_matchmaking_start_bots,"):
        config += enum_with(source, member) + "\n"
    config += structure(source, "matchmaking_view") + "\n"
    config += enum_with(header, "MATCHMAKING_PLAYLIST_NAME_SIZE") + "\n" + structure(source, "matchmaking_playlist")
    config += "\n" + re.search(r"^static struct matchmaking_playlist const matchmaking_playlists\[\] =\n\{.*?^\};",
                                source, re.M | re.S).group(0) + "\n"
    config += re.search(r"^static short const matchmaking_doubles_playlists\[.*?;", source, re.M | re.S).group(0) + "\n"
    # (its state, which HOST GAME begins: matchmaking_host_playlist; the stubs it calls are matchmaking.c's test's)
    config += "#include \"stubs.inc\"\n"
    config += re.search(r"^static struct\n\{\n\tshort stage;.*?^\} matchmaking = \{[^}]*\};", source,
                        re.M | re.S).group(0) + "\n"
    code = "\n\n".join(function(source, name) for name in FUNCTIONS)
    if fault and fault[0] in code:
        code = mutated(code, *fault)
    elif fault:
        config = mutated(config, *fault)
    return (("config.inc", config), ("stubs.inc", STUBS), ("under_test.inc", code))


# the world matchmaking_can_search and matchmaking_host_playlist call: the party, the settings, the maps folder, the
# hosting itself (recorded), the stage set, the clock, the log
STUBS = """
static short stub_party_size = 1;
static boolean stub_member, stub_online = TRUE, stub_hosted_ok = TRUE;
static short stub_maps = 5, stub_hosted_playlist = NONE, stub_hosts;
static short matchmaking_party(boolean *member) { *member = stub_member; return stub_party_size; }
static int config_boolean(char const *name) { (void)name; return stub_online; }
static short matchmaking_maps_present(short playlist, short nth) { (void)playlist; (void)nth; return stub_maps; }
static boolean matchmaking_host(void);
static void matchmaking_stage(short stage);
static unsigned long system_milliseconds(void) { return 1000; }
static void platform_log(char const *format, ...) { (void)format; }
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
"""


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("matchmaking", generated()), case)
    assert status == 0, output


# a fault, and the case that must catch it
FAULTS = {
    "any-playlist": (("return playlist && game->playlist == playlist && game->standard",
                      "return playlist && game->standard"), "pick"),
    "legacy-picked": (("game->playlist == playlist && game->standard && (", "game->playlist == playlist && ("),
                      "pick"),
    "browser-any-kind": (("if (!matchmaking_listing_of(&games[read], (short)(playlist + 1)))", "if (0)"), "browser"),
    "browser-legacy": (("game->playlist == playlist && game->standard && (", "game->playlist == playlist && ("),
                       "browser"),
    "browser-rate-ignored": (("(!rate || game->rate == rate)", "(1)"), "browser"),
    "host-anything": (("\tif (problem != _matchmaking_problem_none)\n\t\treturn FALSE;\n\tmatchmaking.playlist = playlist;\n"
                       "\tmatchmaking.controller = controller;\n\tmatchmaking.begin_time",
                       "\tmatchmaking.playlist = playlist;\n\tmatchmaking.controller = controller;\n\tmatchmaking.begin_time"),
                      "host_action"),
    "host-over-a-search": (("\tif (matchmaking.stage != _matchmaking_idle)\n\t\treturn FALSE;\n\tmatchmaking.problem = problem;",
                            "\tmatchmaking.problem = problem;"), "host_action"),
    "host-without-maps": (("\tif (problem == _matchmaking_problem_none && !matchmaking_maps_present(playlist, NONE))\n"
                           "\t\tproblem = _matchmaking_problem_no_maps;\n", ""), "host_action"),
    "password-picked": ((" || game->locked || game->failed", " || game->failed"), "pick"),
    "failed-picked": ((" || game->failed || !game->invite[0])", " || !game->invite[0])"), "pick"),
    "party-split": (("game->player_count + party_size > game->maximum_player_count",
                     "game->player_count + 1 > game->maximum_player_count"), "pick"),
    "emptiest-first": (("+ game->player_count * 10L", "- game->player_count * 10L"), "pick"),
    "ping-ignored": (("- (ping / MATCHMAKING_PING_STEP) * 1000L", "- (ping / MATCHMAKING_PING_STEP) * 0L"), "pick"),
    "unknown-ping-best": (("game->ping >= 0 ? game->ping : MATCHMAKING_UNKNOWN_PING", "game->ping >= 0 ? game->ping : 0"),
                          "pick"),
    "member-searches": (("\tif (member)\n\t\treturn _matchmaking_problem_member;\n", ""), "problem"),
    "party-too-big": (("if (party_size > party_most)", "if (0)"), "problem"),
    "never-starts-full": (("if (most > 0 && humans >= most)", "if (0)"), "start"),
    "no-wait": (("if (waited < MATCHMAKING_FILL_MILLISECONDS)\n\t\treturn _matchmaking_start_wait;", ""), "start"),
    "starts-too-few": (("return humans >= minimum ? _matchmaking_start_now : _matchmaking_start_wait;",
                        "return _matchmaking_start_now;"), "start"),
    "picked-at-once": (("view->best != NONE && view->searched >= MATCHMAKING_LOOK_MILLISECONDS",
                        "view->best != NONE"), "look"),
    "never-hosts": (("if (view->searched >= view->search_time)", "if (0)"), "look"),
    "member-searches-on": (("\t\tif (!view->leading)\n\t\t\treturn _matchmaking_following;\n\t\tif (view->best",
                            "\t\tif (view->best"), "look"),
    "reach-forever": ((" || view->waited > MATCHMAKING_REACH_MILLISECONDS)", ")"), "join"),
    "joined-at-reach": (("if (view->join_stage == _p2p_join_reached && view->entered)",
                         "if (view->join_stage == _p2p_join_reached)"), "join"),
    "refused-as-joined": (("\t\tif (view->in_game)\n\t\t{\n\t\t\t*action = _matchmaking_action_done;\n"
                           "\t\t\treturn _matchmaking_idle;\n\t\t}\n\t\tif ((view->in_none",
                           "\t\tif (view->in_none)\n\t\t{\n\t\t\t*action = _matchmaking_action_done;\n"
                           "\t\t\treturn _matchmaking_idle;\n\t\t}\n\t\tif ((view->in_none"), "join"),
    "follow-failure-searches": (("return view->following ? _matchmaking_following : _matchmaking_looking;\n\t\t}\n"
                                 "\t\treturn _matchmaking_reaching;",
                                 "return _matchmaking_looking;\n\t\t}\n\t\treturn _matchmaking_reaching;"), "join"),
    "handoff-forever": (("if (view->waited > MATCHMAKING_HANDOFF_MILLISECONDS)", "if (0)"), "host"),
    "new-leader-not-followed": (("\t\tif (view->leader_game)\n", "\t\tif (0)\n"), "host"),
    "followed-for-ever": (("if (view->waited > MATCHMAKING_FOLLOW_MILLISECONDS)", "if (0)"), "host"),
    "playlist-inserted": (('\t{ "Slayer", L"SLAYER"', '\t{ "Doubles", L"DOUBLES", L"", { "team_slayer", NULL },\n'
                           '\t\t{ "wizard", NULL }, 2, 4, 2, 2 },\n\t{ "Slayer", L"SLAYER"'), "playlists"),
    "party-split-teams": (("\t\t2, 4, 2, 2, 120, TRUE },", "\t\t2, 4, 2, 4, 120, TRUE },"), "playlists"),
    "rate-ignored": (("(!rate || game->rate == rate)", "(1)"), "doubles"),
    "kinds-swapped": (("matchmaking_doubles_playlists[NUMBER_OF_MATCHMAKING_DOUBLES_KINDS] = { 6, 5 };",
                       "matchmaking_doubles_playlists[NUMBER_OF_MATCHMAKING_DOUBLES_KINDS] = { 5, 6 };"), "doubles"),
    "classic-at-30": (("\t\t2, 4, 2, 2, 120, TRUE },", "\t\t2, 4, 2, 2, 30, TRUE },"), "doubles"),
    "classic-instant-aim": (("\t\t2, 4, 2, 2, 120, TRUE },", "\t\t2, 4, 2, 2, 120, FALSE },"), "doubles"),
    "crispy-no-instant-aim": (("\t\t2, 4, 2, 2, 120, FALSE },", "\t\t2, 4, 2, 2, 120, TRUE },"), "doubles"),
    # (the kinds, both at 120, told apart by the rate alone: by nothing)
    "kinds-by-rate-alone": (("return playlist && game->playlist == playlist && game->standard",
                             "return playlist && game->playlist && game->standard"), "doubles"),
    "kinds-maps-differ": (('{ "chillout", "hangemhigh", "prisoner", "damnation", "carousel", NULL, NULL, NULL, NULL },\n'
                           '\t\t2, 4, 2, 2, 120, TRUE',
                           '{ "chillout", "hangemhigh", "prisoner", "damnation", "wizard", NULL, NULL, NULL, NULL },\n'
                           '\t\t2, 4, 2, 2, 120, TRUE'), "doubles"),
    "server-waited-for-ever": (("if (!view->hosting && view->waited > MATCHMAKING_HOST_MILLISECONDS)", "if (0)"),
                               "host"),
}


@pytest.mark.parametrize("fault", sorted(FAULTS))
def test_negative_control(fault):
    change, case = FAULTS[fault]
    status, output = run(build("matchmaking", generated(change)), case)
    assert status == 1, f"{fault} was not caught by {case}: {output}"
