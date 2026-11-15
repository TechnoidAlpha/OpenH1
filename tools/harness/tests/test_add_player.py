"""The lobby's Start and the new menus' Add Player (port/linux/game/menu_functions.c's pc_menu_lobby_start,
lobby_join_start and add_player_*, lobby_menu.inc's lobby_board_start; issues 1066 and 1067), the real code against
a fake world (add_player.c): another controller's Start joins it on Add Player and never starts the host's game,
which the host's Start still does; on Add Player each controller not in the game joins, chooses its own profile and is
ready with its own keys, which are never the screen's, and all of them are brought in on their profiles once every one
is ready. Each check has a fault that it must catch."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

FUNCTIONS = "port/linux/game/menu_functions.c"
LOBBY = "port/linux/game/lobby_menu.inc"
TABLE = "port/linux/game/menu_table.h"

CASES = ["extra-start-joins", "host-start-starts", "player-start", "board-start-refuses-joiner", "full",
         "choose-ready-done", "players-keys", "waits-for-all", "b-backs-off"]

ADD_PLAYER = ("add_player_in_game", "add_player_joining", "add_player_room", "add_player_place", "add_player_taken",
              "add_player_first_profile", "add_player_reset", "add_player_act", "add_player_join", "add_player_step",
              "add_player_all_ready", "add_player_finish", "pc_menu_add_player_key", "pc_menu_add_player_action",
              "add_player_begin", "lobby_join_start", "pc_menu_lobby_start")

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (the code before the change: the lobby's Start the host's start, whoever pressed it)
    "start-anyone": ("\tif (lobby_controller_playing(controller))\n\t\treturn lobby_board_start(controller);\n",
                     "\treturn lobby_board_start(controller);\n", "extra-start-joins"),
    # (START GAME's start not asking whose press it was)
    "board-start-anyone": ("!lobby_local_player_count() ||\n\t\t!lobby_controller_playing(controller))",
                           "!lobby_local_player_count())", "board-start-refuses-joiner"),
    # (a player's keys taken from the screen: its focus, its B)
    "players-keys-taken": ("if (controller < 0 || controller >= MAXIMUM_LOCAL_PLAYERS || add_player_in_game(controller))\n"
                           "\t\treturn MENU_SCREEN_KEY_FREE;",
                           "if (controller < 0 || controller >= MAXIMUM_LOCAL_PLAYERS)\n"
                           "\t\treturn MENU_SCREEN_KEY_FREE;", "players-keys"),
    # (DONE brings the ready ones in while another still chooses)
    "not-all-ready": ("\t\tif (add_player.state[controller] == ADD_PLAYER_CHOOSING && !add_player_in_game(controller))\n"
                      "\t\t{\n\t\t\tadd_player_act(controller);\n\t\t\treturn campaign_fail();\n\t\t}\n", "",
                      "waits-for-all"),
    # (the profile chosen not the one joined on)
    "profile-ignored": ("ui_widget_port_multiplayer_player(controller, add_player.profile[controller])",
                        "ui_widget_port_multiplayer_player(controller, profile_list.indices[0])",
                        "choose-ready-done"),
    # (left and right not stepping the profile)
    "no-step": ("place = (short)(((place == NONE ? 0 : place + step)", "place = (short)(((place == NONE ? 0 : place)",
                "choose-ready-done"),
    # (B when ready leaving at once)
    "b-leaves-ready": ("\t\telse if (key == MENU_SCREEN_KEY_B)\n\t\t{\n\t\t\t*state = ADD_PLAYER_CHOOSING;",
                       "\t\telse if (key == MENU_SCREEN_KEY_B)\n\t\t{\n\t\t\t*state = ADD_PLAYER_EMPTY;", "b-backs-off"),
    # (no room asked for)
    "no-room": ("room = (short)MIN(room, game->maximum_players - lobby_player_count - add_player_joining());",
                "room = (short)MIN(room, 99);", "full"),
}


def static_object(source, name):
    """a file's own static structure, with its initializer if it has one ("static struct {...} name[ = {...}];")"""
    end = re.search(r"^\} " + re.escape(name) + r"(?: = \{[^;]*\})?;", source, re.M)
    start = source.rfind("static struct\n{", 0, end.start()) if end else -1
    if start < 0:
        raise LookupError(f"static structure not found in the sources: {name}")
    return source[start:end.end()]


def generated(fault=None):
    functions = read(FUNCTIONS)
    config = enum_with(read(TABLE), "MENU_SCREEN_KEY_OTHER") + "\n" + enum_with(read(TABLE), "MENU_SCREEN_KEY_FREE") + \
        "\n"
    code = enum_with(functions, "ADD_PLAYER_EMPTY") + "\n"
    code += static_object(functions, "lobby_join") + "\n" + static_object(functions, "add_player") + "\n"
    code += "\n\n".join(function(functions, name) for name in ADD_PLAYER) + "\n\n" + \
        function(read(LOBBY), "lobby_board_start")
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][:2])
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("add_player", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    case = NEGATIVE_CONTROLS[control][2]
    status, output = run(build("add_player", generated(control)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
