"""A player host's next game and players (port/linux/game/network_host_game.c's pure parts: host_game.c): the
playlist's next entry that can be played, round from its end; a ban's and a mute's duration as the host's console and
Host Controls take it; and the teams shuffled (as many on each, drawn from the seed) or balanced (as few moved as can
be; by score where there are scores), the teams' colours kept for the most players; what the lobby after a game takes
(nothing in co-op, a restart its own game), the playlist past entries that can no longer be played (each tried once),
a playlist kept whole within its text, the console's ban line (a quoted name, its length and reason), a saved
gametype's key, and a change of map alone (MAPS' CURRENT, the console's host_map without a gametype) keeping the
lobby's gametype, score, time and rules while a gametype named is built, MAPS' gametype found by the game's name.
Each check has a fault that it must catch."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

SOURCE = "port/linux/game/network_host_game.c"
CASES = ["playlist", "duration", "shuffle", "balance", "takes", "try", "fits", "ban", "saved", "kept", "find"]
PURE = ("network_host_game_playlist_after", "network_host_game_duration_parse", "network_host_game_teams_plan",
        "network_host_game_lobby_takes", "network_host_game_playlist_try", "network_host_game_playlist_fits",
        "network_host_game_ban_parse", "network_host_game_entry_keeps", "network_host_game_variant_kept",
        "network_host_game_gametype_find")
GAMETYPES = "port/linux/game/server_gametypes.c"

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    "playlist-no-wrap": ("long candidate = (index + step) % playlist->count;", "long candidate = index + step;",
                         "playlist"),
    "playlist-plays-rejected": ("if (!playlist->entries[candidate].rejected[0])", "if (TRUE)", "playlist"),
    "playlist-shorter-kept": ("if (index < 0 || index >= playlist->count)", "if (index < 0)", "playlist"),
    "duration-hours-as-minutes": ("case 'h': unit = 60 * 60; break;", "case 'h': unit = 60; break;", "duration"),
    "duration-no-forever": ('!csstrcmp(text, "forever")', "FALSE", "duration"),
    "duration-trailing": ("if ((*end && end[1]) || number <= 0)", "if (number <= 0)", "duration"),
    "shuffle-one-team": ("teams[order[index]] = (char)(index % HOST_GAME_TEAMS);", "teams[order[index]] = 0;",
                         "shuffle"),
    "shuffle-no-seed": ("seed = seed * 1664525UL + 1013904223UL;", "seed = 7;", "shuffle"),
    "balance-moves-everyone": ("if (teams[index] == larger)", "if (TRUE)", "balance"),
    "balance-scores-ignored": ("\telse if (scored)", "\telse if (scored && FALSE)", "balance"),
    "colours-not-kept": ("if (kept * 2 < count)", "if (FALSE)", "balance"),
    # (co-op's next level replaced by the playlist's or a change: the review's first)
    "coop-takes-the-playlist": ("\tif (cooperative)\n\t\treturn 0;\n", "", "takes"),
    "restart-takes-the-playlist": ("\tif (!restarting)\n\t\tSET_FLAG(takes, _host_game_take_playlist, TRUE);",
                                   "\tSET_FLAG(takes, _host_game_take_playlist, TRUE);", "takes"),
    # (an entry that can't be played blocks the playlist, or is played)
    "try-no-skip": ("\t\tindex = next;\n\t}\n\treturn NONE;", "\t\treturn NONE;\n\t}\n\treturn NONE;", "try"),
    "try-forever": ("for (tried = 0; tried < playlist->count; tried++)", "for (tried = 0; tried < playlist->count || TRUE; tried++)", "try"),
    # (a playlist written past its text's end)
    "fits-past-the-text": ("return length < SERVER_ROTATION_MAXIMUM_TEXT;",
                           "return length < SERVER_ROTATION_MAXIMUM_TEXT || TRUE;", "fits"),
    "fits-past-the-entries": ("\tif (playlist->count >= SERVER_ROTATION_MAXIMUM_ENTRIES)\n\t\treturn FALSE;\n", "",
                              "fits"),
    # (a quoted name's length and reason lost)
    "ban-quoted-name": ("\tif (line[0] == '\"')", "\tif (FALSE)", "ban"),
    "ban-parenthesis-kept": ("\tif (length > 0 && line[length - 1] == ')')\n\t\tline[--length] = 0;\n", "", "ban"),
    "saved-key-spaces": ("\t\telse\n\t\t\tgap = TRUE;", "\t\telse\n\t\t\tgap = FALSE;", "saved"),
    # (a map alone played as Slayer at its defaults: the bug; a gametype named kept as the lobby's)
    "map-alone-is-slayer": ("return text && !strchr(text, ':');", "return FALSE;", "kept"),
    "named-gametype-kept": ("\tif (entry->gametype[0] || !game)", "\tif (!game)", "kept"),
    "kept-score-lost": ("\t*variant = game->variant;", "\t(void)variant;", "kept"),
    # (MAPS' selector on the list's first whatever the game plays, or on a name the game's begins with)
    "find-the-first": ("\t\t\treturn index;\n\t}\n\treturn NONE;", "\t\t\treturn 0;\n\t}\n\treturn 0;", "find"),
    "find-a-prefix": (": listed[at] == shown[at])", ": !listed[at])", "find"),
}


def generated(fault=None):
    source = read(SOURCE)
    rotation = read("port/linux/src/server_rotation.h")
    limits = read("port/linux/include/halo_port_limits.h")
    config = rotation[rotation.index("enum\n{\n\tSERVER_ROTATION_MAXIMUM_ENTRIES"):
                      rotation.index("/* one entry's text read")]
    config += enum_with(read("port/linux/game/network_host_game.h"), "_host_game_shuffle,") + ";\n"
    config += f"#define MAXIMUM_HOST_GAME_PLAYERS {constant(limits, 'HALO_PORT_MAXIMUM_NETWORK_PLAYERS')}\n"
    config += f"#define HOST_GAME_TEAMS {constant(source, 'HOST_GAME_TEAMS')}\n"
    config += enum_with(read("port/linux/game/network_host_game.h"), "_host_game_take_change,") + ";\n"
    config += f"#define SERVER_SAVED_NAME_SIZE {constant(read(GAMETYPES), 'SERVER_SAVED_NAME_SIZE')}\n"
    header = read("port/linux/game/network_host_game.h")
    config += f"#define HOST_GAME_GAMETYPE_SHOWN_SIZE {constant(header, 'HOST_GAME_GAMETYPE_SHOWN_SIZE')}\n"
    config += header[header.index("struct host_game_gametype\n{"):header.index("/* ---------- prototypes")]
    text = ""
    sources = {name: source for name in PURE}
    sources["server_gametype_saved_key"] = read(GAMETYPES)
    for name, from_source in sources.items():
        code = function(from_source, name)
        if fault:
            try:
                code = mutated(code, fault[0], fault[1])
            except LookupError:
                pass
        text += code + "\n"
    if fault:
        assert fault[0] not in text, f"the fault {fault[0]!r} is in none of the pure parts"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("host_game", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    before, after, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("host_game", generated((before, after))), case)
    assert status == CHECK_FAILED, f"network_host_game.c with a fault ({control}) passed '{case}': the test cannot see it"
