"""The host's controls of a network game (port/linux/game/network_host_controls.c, host_controls_menu.inc): who has
them (the host of a network game under way; never a client, a game of one machine alone or the menus), what each player
may have done to them (the host's own players and the bots are never kicked, banned or muted; OpenCE's players are
kicked as anyone), Kick and Ban by the console's own kick and ban, Move team only in an OpenH1 game without OpenCE's
machines (the unit killed without statistics, every OpenH1 machine told, a client taking it from the host's stream
alone), Lock teams, End game now and Restart round (the scores, then the lobby and its countdown), Max players and
bans.txt's lines; and the menus: the pause menu's HOST CONTROLS the network game's host's alone, the scoreboard's player
menu offering what Host Controls offers, the lobby's team changes kept by the lock, and the screen's file the
generator's."""

import json
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import enum_with, CHECK_FAILED, ROOT, build, constant, mutated, read, run  # noqa: E402

CASES = ["seats", "actions", "team_moves", "kick", "move", "lock", "end", "next", "same", "maximum", "bans",
         "players", "bots", "bots_tab", "bot_count", "bot_skill", "bot_choices"]

# a fault in network_host_controls.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "client-has-them": (("seat->server && ((seat->connection == _game_connection_network_server && seat->playing) || "
                         "seat->lobby)", "seat->server && (seat->playing || seat->lobby)"), "seats"),
    "local-game-has-them": (("seat->connection == _game_connection_network_server && seat->playing",
                             "seat->connection != _game_connection_network_client && seat->playing"), "seats"),
    "menus-have-them": (("seat.playing = server && game_in_progress() && !main_menu_is_loaded();",
                         "seat.playing = server != NULL;"), "seats"),
    "host-kicks-itself": (("\tif (view->own)\n\t\treturn actions;", ""), "actions"),
    "bots-banned": (("\t\tif (view->bot_removable)\n\t\t\tSET_FLAG(actions, _host_controls_kick, TRUE);\n"
                     "\t\treturn actions;", "\t\t;"), "actions"),
    "move-without-teams": (("if (view->teams && view->team_moves)", "if (view->team_moves)"), "actions"),
    "kick-by-another-path": (("return network_votekick_host_kick((short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index), ban);",
                              "return TRUE;"), "kick"),
    "kick-unchecked": (("if (!TEST_FLAG(actions, ban ? _host_controls_ban : _host_controls_kick) || !player)",
                        "if (!player)"), "kick"),
    "moves-with-opence": (("view->standard && view->teams && view->opence_machines == 0",
                           "view->standard && view->teams"), "team_moves"),
    "moves-in-legacy": (("view->standard && view->teams && view->opence_machines == 0",
                         "view->teams && view->opence_machines == 0"), "team_moves"),
    "move-not-killed": (("\t\tunit_kill_no_statistics(unit_index);", "\t\t(void)unit_index;"), "move"),
    "move-not-recorded": (("\t\t\tgames[index]->players[list_index].team_index = (char)team;", "\t\t\t;"), "move"),
    "move-not-told": (("\t\tif (network_standard_machine_is_openh1(machines[index]))", "\t\tif (0)"), "move"),
    "move-from-datagram": (("if (machine_index != NONE || !from_stream ||", "if (machine_index != NONE ||"), "move"),
    "move-from-client": (("if (machine_index != NONE || !from_stream ||", "if (!from_stream ||"), "move"),
    "move-of-other-format": (("if (move.format != HOST_TEAM_FORMAT)", "if (0)"), "move"),
    "move-to-no-team": (("\tif (move.team >= HOST_CONTROLS_TEAMS)\n\t\treturn;", ""), "move"),
    "move-in-legacy": (("\t\t!network_standard_agreed_rate() || !game_in_progress()", "\t\t!game_in_progress()"),
                       "move"),
    "lock-ignored": (("return locked && !host_machine ? current : requested;", "return requested;"), "lock"),
    "lock-holds-host": (("return locked && !host_machine ? current : requested;",
                         "return locked ? current : requested;"), "lock"),
    "lock-outlives-server": (("\tif (!server)\n\t\thost_controls.teams_locked = FALSE;", ""), "lock"),
    "end-by-client": (("if (!network_host_controls_available() || !game_engine_running() || !game_engine_can_score())",
                       "if (!game_engine_running() || !game_engine_can_score())"), "end"),
    "restart-skips-scores": (("\t\tif (!view->postgame)\n\t\t\treturn stage;", ""), "end"),
    "restart-skips-lobby": (("\t\tif (!view->menu_loaded || !view->pregame)\n\t\t\treturn stage;", ""), "end"),
    "restart-held": (("\tnetwork_game_server_pause_countdown(server, FALSE);\n", ""), "end"),
    "lobby-not-theirs": (("(seat->connection == _game_connection_network_server && seat->playing) || seat->lobby",
                          "seat->connection == _game_connection_network_server && seat->playing"), "seats"),
    "next-not-taken": (("\t\tif (network_host_game_lobby_take(FALSE))", "\t\tif (FALSE)"), "next"),
    "next-every-frame": (("\t\thost_controls.played = FALSE;\n\t\tif (network_host_game_lobby_take(FALSE))",
                          "\t\tif (network_host_game_lobby_take(FALSE))"), "next"),
    # (a lobby's player chosen by slot alone: one who took the slot banned in their place)
    "slot-alone": (("\tif (player_index != NONE || player->slot != slot || player->machine_index != machine_index)\n"
                    "\t\treturn FALSE;\n\tfor (index = 0;", "\tif (player_index != NONE || player->slot != slot)\n"
                    "\t\treturn FALSE;\n\treturn TRUE;\n\tfor (index = 0;"), "same"),
    "restart-takes-nothing": (("\t\tnetwork_host_game_lobby_take(TRUE);\n", ""), "end"),
    "maximum-below-players": (("if (values[index] < current && values[index] >= present)",
                               "if (values[index] < current)"), "maximum"),
    "ban-key-anywhere": (("found != line && found[-1] != '\\t'", "0"), "bans"),
    "ban-of-nothing": (("if (!ban->address[0] && (!ban->hardware_id[0] || !strcmp(ban->hardware_id, \"none\")))",
                        "if (0)"), "bans"),
    "quit-listed": (("\t\tif (player->quit_out_of_game)\n\t\t\tcontinue;", ""), "players"),
    "bot-out-kept-in-count": (("\tnetwork_host_controls_choose_bots(left ? _host_bots_add : _host_bots_none, left);",
                               "\t(void)left;"), "bots"),
    "bot-choice-wraps": (("return (short)(stepped < 0 ? 0 : stepped > last ? last : stepped);",
                          "return (short)(stepped < 0 ? last : stepped > last ? 0 : stepped);"), "bot_choices"),
    "fill-kept-after-a-bot-out": (("return host_controls_bot_write(\"bots.make_room\", kind == _host_bots_fill ? "
                                   "\"true\" : \"false\");", "return TRUE;"), "bots"),
    "skill-wraps": (("return value < least ? least : value > most ? most : value;",
                     "return value < least ? most : value > most ? least : value;"), "bots"),
    "bot-out-of-asked-for": (("long left = MIN(count, playing) - 1;", "long left = count - 1;"), "bots"),
    "bot-out-in-matchmaking": (("bots_player_is_bot(player_index) && !bots_matchmaking_on();",
                                "bots_player_is_bot(player_index);"), "bots"),
    "matchmaking-count-changed": (("if (bots_matchmaking_on() || (kind != _host_bots_none", "if ((kind != _host_bots_none"), "bots"),
    "bots-play-any-gametype": (("return server && bots_play_game_engine(network_game_server_get_game(server)->variant"
                                ".game_engine_index);", "return server != NULL;"), "bots"),
    "tactics-not-written": (("return host_controls_bot_write(\"bots.tactics\"", "return TRUE || host_controls_bot_write("
                             "\"bots.tactics\""), "bots"),
    "bots-tab-for-clients": (("return network_host_controls_available() && !network_host_game_cooperative();",
                              "return !network_host_game_cooperative();"), "bots_tab"),
    "bots-tab-in-coop": (("return network_host_controls_available() && !network_host_game_cooperative();",
                          "return network_host_controls_available();"), "bots_tab"),
    "bots-tab-unnamed": (("\"bans\", \"bots\", \"rules\" };", "\"bans\", \"rules\" };"), "bots_tab"),
    "bot-count-wraps": (("return stepped < 0 ? 0 : stepped > most ? most : stepped;",
                         "return stepped < 0 ? most : stepped > most ? 0 : stepped;"), "bot_count"),
    "bot-count-past-room": (("long most = MIN(BOTS_MAXIMUM, MAX(most_players - humans, 0));",
                             "long most = BOTS_MAXIMUM;"), "bot_count"),
    "bot-count-past-maximum": (("long most = MIN(BOTS_MAXIMUM, MAX(most_players - humans, 0));",
                                "long most = MAX(most_players - humans, 0);"), "bot_count"),
    "skill-not-written": (("return host_controls_bot_write(\"bots.skill\", host_controls_bot_skill_names[skill]);",
                           "return TRUE;"), "bot_skill"),
    "skill-any-name": (("\t\t\tif (character != name[index])\n\t\t\t\tbreak;", "\t\t\tif (0)\n\t\t\t\tbreak;"),
                       "bot_skill"),
    "skill-prefix": (("if (!name[index] && (!text[index] ||", "if (!name[index] && (1 ||"), "bot_skill"),
    "bot-out-by-a-client": (("return player && !player->quit_out_of_game && global_network_game_server_get() &&",
                             "return player && !player->quit_out_of_game &&"), "bots"),
    "balance-row-always": (("if (row == _host_bot_row_balance && !network_host_game_teams_arrangeable())",
                            "if (0)"), "bots"),
    "legacy-told-apart": (("\t\telse if (network_standard_machine_is_openh1(machine_index))\n"
                           "\t\t\tentry->tag = _host_controls_tag_openh1;", ""), "players"),
}


def generated(fault=None):
    limits = read("port/linux/include/halo_port_limits.h")
    distributed = read("port/linux/game/network_distributed.h")
    config = "".join(f"#define {name} {constant(limits, name)}\n" for name in (
        "HALO_PORT_MAXIMUM_NETWORK_MACHINES", "HALO_PORT_MAXIMUM_NETWORK_PLAYERS"))
    value = re.search(r"\b_distributed_message_host_team = (\d+)", distributed).group(1)
    config += f"#define _distributed_message_host_team {value}\n"
    config += enum_with(read("port/linux/game/network_admin.h"), "_network_admin_lists,") + "\n"
    header = read("port/linux/game/network_host_controls.h")
    # (the constants, structures and prototypes: the code's functions call each other in any order)
    config += header[header.index("/* ---------- constants */"):header.rindex("#endif")]
    source = read("port/linux/game/network_host_controls.c")
    text = source[source.index("/* ---------- constants */"):]
    if fault:
        text = mutated(text, *fault)
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("host_controls", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("host_controls", generated(fault)), case)
    assert status == CHECK_FAILED, f"network_host_controls.c with a fault ({control}) passed '{case}': " \
        "the test cannot see it"


def test_pause_menu_is_the_network_hosts():
    """The pause menu's HOST CONTROLS is the host's of a network game (a game of this machine alone keeps END GAME),
    and opens Host Controls' screen; its function fails for anyone else."""
    tags = read("port/linux/game/menu_tags.c")
    patch = tags[tags.index("static long pause_list_patch("):tags.index("/* the Xbox's own box")]
    assert "host && !network_game_is_splitscreen_local() && host_controls_widget != NONE" in patch
    assert '"HOST CONTROLS"' in patch and 'function_index("port host open"' in patch
    assert "build.widget_tags[host_controls_widget]" in patch
    menu = read("port/linux/game/host_controls_menu.inc")
    opened = menu[menu.index("static boolean host_controls_open(void)"):]
    opened = opened[:opened.index("\n}\n")]
    assert "if (!host_available())" in opened and "return FALSE;" in opened
    # (the host's, or a dedicated server's admin's: network_admin.c)
    available = menu[menu.index("static boolean host_available(void)"):]
    assert "return network_host_controls_available() || host_remote();" in available[:available.index("\n}\n")]


def test_scoreboard_offers_host_controls():
    """The scoreboard's player menu offers the host's Mute for everyone, Kick, Ban, Move and Host controls... after
    the vote, mute and party items, each as Host Controls offers it for the player, and does them through it."""
    engine = read("source/game/game_engine.c")
    items = engine[engine.index("\t_scoreboard_item_vote,"):engine.index("NUMBER_OF_SCOREBOARD_ITEMS")]
    order = re.findall(r"_scoreboard_item_(\w+),", items)
    assert order == ["vote", "mute", "party_invite", "party_join", "party_accept", "party_friend", "host_mute", "kick",
                     "ban", "move_team", "host_controls", "cancel"]
    offered = engine[engine.index("static void scoreboard_menu_items("):engine.index("static long scoreboard_menu_first_row(")]
    assert "if (network_host_controls_available() || network_admin_remote())" in offered
    for item, action in (("host_mute", "mute"), ("kick", "kick"), ("ban", "ban"), ("move_team", "move_team")):
        assert re.search(rf"offered\[_scoreboard_item_{item}\] = TEST_FLAG\(actions, _host_controls_{action}\)",
                         offered), item
    picked = engine[engine.index("static void scoreboard_menu_pick("):engine.index("static void game_engine_rasterize_scoreboard(")]
    assert "network_host_controls_kick(scoreboard_menu->player_index, FALSE)" in picked
    assert "network_host_controls_kick(scoreboard_menu->player_index, TRUE)" in picked
    assert "network_host_controls_move_team(scoreboard_menu->player_index)" in picked
    assert "network_host_controls_open(scoreboard_menu->player_index, 0)" in picked
    assert "ban_picked" in picked, "Ban is picked twice"


def test_lobby_keeps_locked_teams():
    """The host's lobby takes a player's team change through the lock (the host machine's own are its own)."""
    handler = read("source/networking/network_server_message_handler.c")
    request = handler[handler.index("static boolean network_game_server_handle_message_client_player_settings_request(\n"
                                    "\tstruct network_game_server *server,\n\tstruct network_game_server_client_machine "
                                    "*client_machine,\n\tword *message,\n\tshort message_size)\n{"):]
    request = request[:request.index("\n}\n")]
    assert "network_host_controls_lobby_team(\n\t\t\t\t\tnetwork_game_server_client_machine_is_local(server, " \
        "client_machine)" in request


def test_files_are_the_generators():
    """Host Controls' file is tools/port_settings.py's, embedded, with its title drawn and listed."""
    sys.path.insert(0, str(ROOT / "tools"))
    import port_settings

    files = port_settings.multiplayer_files()
    name = port_settings.HOST_CONTROLS.replace("/", ".") + ".xml"
    ce = ROOT / "port/assets/menus/ce"
    assert (ce / name).read_text() == "\n".join(files[name]), "run python3 tools/port_settings.py"
    listed = json.loads((ROOT / "port/assets/menus/menus.json").read_text())["files"]
    title = f"{port_settings.HOST_CONTROLS}/header_host_controls"
    assert f"ce/{name}" in listed and f"ce/port/{title}.png" in listed
    assert f'<bitmap name="{title}">' in (ce / "bitmaps.xml").read_text()
    assert port_settings.TITLES[title] == "HOST CONTROLS"
