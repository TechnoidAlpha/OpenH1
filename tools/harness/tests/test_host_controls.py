"""The host's controls of a network game (port/linux/game/network_host_controls.c, host_controls_menu.inc): who has
them (the host of a network game under way; never a client, a game of one machine alone or the menus), what each player
may have done to them (the host's own players and the bots are never kicked, banned or muted; OpenCE's players are
kicked as anyone), Kick and Ban by the console's own kick and ban, Move team only in an OpenH1 game without OpenCE's
machines (the unit killed without statistics, every OpenH1 machine told, a client taking it from the host's stream
alone), Lock teams, End game now and Restart round (the scores, then the lobby and its countdown), Max players and
bans.txt's lines; and the menus: the pause menu's HOST CONTROLS the network game's host's alone (co-op's too), the scoreboard's player
menu offering what Host Controls offers, the lobby's team changes kept by the lock, and the screen's file the
generator's."""

import json
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import enum_with, CHECK_FAILED, ROOT, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["seats", "actions", "team_moves", "kick", "move", "lock", "end", "next", "same", "maximum", "bans",
         "players", "bots", "bots_tab", "bot_count", "bot_skill", "bot_choices", "pause", "pause_players",
         "teleports"]

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
    # (the maintainer's report: a co-op host's pause menu had no HOST CONTROLS)
    "coop-host-without-them": (("\telse if (!view->local && view->host_controls_screen)",
                                "\telse if (!view->coop && !view->local && view->host_controls_screen)"), "pause"),
    "coop-client-has-them": (("\tif (!view->host || !view->first_player)\n\t\treturn rows;",
                              "\tif (!view->first_player)\n\t\treturn rows;"), "pause"),
    "coop-end-game": (("\telse if (!view->coop)\n", "\telse\n"), "pause"),
    "coop-no-friendly-fire": (("\tif (view->coop)\n\t\tSET_FLAG(rows, _host_controls_pause_friendly_fire, TRUE);",
                               ""), "pause"),
    # (issue 889: split screen's other players had the host's pause rows)
    "every-player-hosts": (("\tif (!view->host || !view->first_player)\n\t\treturn rows;",
                            "\tif (!view->host)\n\t\treturn rows;"), "pause_players"),
    "every-local-player-first": (("\t\tlocal_player_index == first_local_player_index;", "\t\tTRUE;"),
                                 "pause_players"),
    "player-zero-always-first": (("network_host_controls_local_player_for(local_player_index, local_player_get_next(NONE))",
                                  "network_host_controls_local_player_for(local_player_index, 0)"), "pause_players"),
    "friendly-fire-first-player-only": (("\tif (view->coop)\n\t\tSET_FLAG(rows, _host_controls_pause_friendly_fire, "
                                         "TRUE);\n\t/* (the host's",
                                         "\tif (view->coop && view->first_player)\n\t\tSET_FLAG(rows, "
                                         "_host_controls_pause_friendly_fire, TRUE);\n\t/* (the host's"),
                                        "pause_players"),
    # (co-op's BRING TO ME and GO TO)
    "teleports-in-versus": (("\tif (view->coop)\n\t{\n\t\tSET_FLAG(actions, _host_controls_bring",
                             "\tif (1)\n\t{\n\t\tSET_FLAG(actions, _host_controls_bring"), "teleports"),
    "teleports-of-own": (("\tif (view->own)\n\t\treturn actions;", "\tif (0)\n\t\treturn actions;"), "teleports"),
    "teleport-not-told": (("\tdistributed_send_to_machine_reliably(machine_index, &message, "
                           "_distributed_message_host_teleport, 1,", "\tif (0) distributed_send_to_machine_reliably("
                           "machine_index, &message, _distributed_message_host_teleport, 1,"), "teleports"),
    "teleport-from-client": (("\t\tif (machine_index == NONE && from_stream && game_connection() == "
                              "_game_connection_network_client &&", "\t\tif (from_stream && game_connection() == "
                              "_game_connection_network_client &&"), "teleports"),
    "teleport-not-turned": (("\t\t\tplayer_control_set_facing(player->local_player_index, &facing);", "\t\t\t;"),
                            "teleports"),
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
    value = re.search(r"\b_distributed_message_host_teleport = (\d+)", distributed).group(1)
    config += f"#define _distributed_message_host_teleport {value}\n"
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
    assert "rows = network_host_controls_pause_rows_for(&view);" in patch
    assert "view.local = network_game_is_splitscreen_local();" in patch
    assert "view.host_controls_screen = host_controls_widget != NONE;" in patch
    assert '"HOST CONTROLS"' in patch and 'function_index("port host open"' in patch
    assert "build.widget_tags[host_controls_widget]" in patch
    # (the tags kept for the buttons: each a widget and its text, as many as the list adds at most)
    most = int(re.search(r"char const \*names\[(\d+)\];", patch).group(1))
    assert int(re.search(r"#define PAUSE_PATCH_TAGS (\d+)", tags).group(1)) >= 2 * most
    # (network co-op's campaign pause menu: its host's too)
    campaign = tags[tags.index("static void pause_campaign_patch("):]
    campaign = campaign[:campaign.index("\n}\n")]
    assert "pause_list_patch(instances, list, revert, build.coop && global_network_game_server_get() != NULL," in campaign
    menu = read("port/linux/game/host_controls_menu.inc")
    opened = menu[menu.index("static boolean host_controls_open(struct widget_instance *widget)"):]
    opened = opened[:opened.index("\n}\n")]
    assert "if (!host_available() || !host_rows_player(widget))" in opened and "return FALSE;" in opened
    # (the host's, or a dedicated server's admin's: network_admin.c)
    available = menu[menu.index("static boolean host_available(void)"):]
    assert "return network_host_controls_available() || host_remote();" in available[:available.index("\n}\n")]


def test_pause_host_rows_are_player_ones():
    """Issue 889: the hand-written pause menus' host rows (HOST CONTROLS, END GAME, CHAPTERS) are the screen's own
    local player's, the host machine's first alone (split screen's player 1), and their actions refuse anyone else's
    screen, the map's screens (shared by every player) too."""
    screen = read("port/linux/game/menu_screen.c")
    assert "unsigned long rows = menu_tags_pause_rows(values->local_player_index);" in screen
    clear = function(screen, "screen_values_clear")
    assert "values->local_player_index = open && open->widget ? open->widget->local_player_index : NONE;" in clear
    # (every screen's values are its own player's: none cleared another way)
    assert "memset(&values, 0, sizeof(values));" not in screen
    assert "tall->values.local_player_index = widget->local_player_index;" in read("port/linux/game/menu_screen_tall.inc")
    tags = read("port/linux/game/menu_tags.c")
    rows = tags[tags.index("unsigned long menu_tags_pause_rows(\n\tshort local_player_index)"):]
    rows = rows[:rows.index("\n}\n")]
    assert "view.first_player = network_host_controls_local_player(local_player_index);" in rows
    # (each host row's action is guarded at its function: HOST CONTROLS' in test_pause_menu_is_the_network_hosts)
    assert '{ "pause chapters", NULL, NULL, "port training open",' in screen
    functions = read("port/linux/game/menu_functions.c")
    assert "network_host_controls_local_player(screen_of(widget)->local_player_index)" in \
        function(functions, "host_rows_player")
    assert "|| !host_rows_player(widget))" in function(functions, "pause_end_game")
    training = functions[functions.index('else if (!strcmp(name, "port training open"))'):]
    assert "if (!host_rows_player(widget))" in training[:training.index("else if", 10)]
    # (every host row of both pause screens takes one of those actions)
    for name in ("pause.xml", "pause_campaign.xml"):
        for item in re.findall(r"<item [^>]*>", read(f"port/assets/menus/screens/{name}")):
            if 'if="pause.' in item:
                assert re.search(r'do="(open host controls|pause end game|pause chapters)"', item), item


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


# Host Controls' PLAYLIST (host_controls_menu.inc): the playlist and its next, the host's own or, for a dedicated
# server's admin, the server's; each once ended in itself, and a host's PLAYLIST tab hung the game
PLAYLIST_NEGATIVE_CONTROLS = {
    "playlist-ends-in-itself": (("return host_remote() ? network_admin_rotation() : network_host_game_playlist();",
                                 "return host_remote() ? network_admin_rotation() : host_playlist();"), "host"),
    "index-ends-in-itself": (("\treturn network_host_game_playlist_index();", "\treturn host_playlist_index();"),
                             "host"),
    "admin-sees-the-hosts": (("return host_remote() ? network_admin_rotation() : network_host_game_playlist();",
                              "return network_host_game_playlist();"), "admin"),
}


def playlist_generated(fault=None):
    menu = read("port/linux/game/host_controls_menu.inc")
    code = function(menu, "host_playlist") + "\n\n" + function(menu, "host_playlist_index") + "\n"
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", ["host", "admin"])
def test_playlist(case):
    status, output = run(build("host_controls_playlist", playlist_generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", PLAYLIST_NEGATIVE_CONTROLS)
def test_playlist_negative_control(control):
    fault, case = PLAYLIST_NEGATIVE_CONTROLS[control]
    status, output = run(build("host_controls_playlist", playlist_generated(fault)), case)
    assert status == CHECK_FAILED, f"host_controls_menu.inc with a fault ({control}) passed '{case}': " \
        "the test cannot see it"
