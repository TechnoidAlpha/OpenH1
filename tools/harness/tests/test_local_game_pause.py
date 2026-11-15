"""A campaign started after the server browser (CAMPAIGN's CO-OP) or a lobby was left: the menus' client and server
let go of as its map loads (main.c's main_local_game_leave_network), so Start pauses it, whichever controller
pressed it, where it took itself for a network game and asked for a multiplayer pause screen the map has not."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

LET_GO = "\tdispose_global_network_game_client();\n\tdispose_global_network_game_server();\n"


def generated(fault=None):
    main = function(read("source/main/main.c"), "main_local_game_leave_network")
    if fault == "kept":
        main = mutated(main, LET_GO, "")
    elif fault == "pictures":
        main = mutated(main, "\tif (menu_pictures_preparing())\n\t\treturn;\n", "")
    globals_source = read("source/networking/network_game_globals.c")
    code = "\n".join(function(globals_source, name) for name in (
        "network_game_is_active", "global_network_game_client_get", "dispose_global_network_game_client",
        "dispose_global_network_game_server"))
    code += "\n" + main + "\n" + function(read("source/interface/ui_widget.c"), "ui_check_for_pause_game") + "\n"
    config = enum_with(read("source/game/game.h"), "_game_connection_film_playback") + "\n"
    config += enum_with(read("source/input/input.h"), "_gamepad_binary_button_start") + "\n"
    players = constant(read("source/networking/network_connection.h"), "MAXIMUM_NUMBER_OF_LOCAL_PLAYERS")
    config += f"#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS {players}\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", ["after-browser", "after-lobby", "fresh", "pictures", "network-coop"])
def test_local_game_pause(case):
    status, output = run(build("local_game_pause", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault,case", [("kept", "after-browser"), ("kept", "after-lobby"), ("pictures", "pictures")])
def test_negative_control(fault, case):
    """the menus' client kept into the campaign (the code before the change): Start pauses nothing"""
    status, output = run(build("local_game_pause", generated(fault)), case)
    assert status == CHECK_FAILED, output
