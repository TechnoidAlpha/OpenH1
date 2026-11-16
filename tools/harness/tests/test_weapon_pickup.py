"""The real unit_should_autopick_weapon (players.c) and network_standard.c's pickup way: the campaign walks over a
second weapon, multiplayer holds the action button (the Xbox's) unless game.auto_pickup asks for Custom Edition's
way, an OpenH1 game takes its host's (its record carries it), a Legacy game and OpenCE's players walk over."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["campaign", "local", "host", "opence-record", "legacy-host", "client"]

# a fault in the code and the case that must catch it
NEGATIVE_CONTROLS = {
    # (multiplayer walks over, as the port did before: Custom Edition's way always)
    "walk-over-always": (("((!game_engine_running() ||\n\t\t\tnetwork_standard_walk_over_pickup(",
                          "((TRUE ||\n\t\t\tnetwork_standard_walk_over_pickup("), "local"),
    # (the Xbox's test alone: the setting never asked)
    "setting-ignored": (("((!game_engine_running() ||\n\t\t\tnetwork_standard_walk_over_pickup(",
                         "((!game_engine_running() || 0 *\n\t\t\tnetwork_standard_walk_over_pickup("), "local"),
    # (an OpenCE machine's player held to the OpenH1 host's way)
    "opence-player-held": (("if (!network_standard_globals.hosting || network_standard_player_is_opence(player_index))",
                            "if (!network_standard_globals.hosting)"), "host"),
    # (the host's way changed during a game)
    "read-in-game": (("\tif (in_lobby)\n\t{\n\t\tnetwork_standard_globals.hosting_hold_to_pick_up",
                      "\tif (TRUE)\n\t{\n\t\tnetwork_standard_globals.hosting_hold_to_pick_up"), "host"),
    # (an OpenCE machine sent the bit)
    "opence-sent-the-bit": (("\tSET_FLAG(legacy_game.standard_flags, _network_game_hold_to_pick_up_bit, FALSE);\n",
                             ""), "opence-record"),
    # (a Legacy host's record says hold)
    "legacy-record": (("network_standard_globals.hosting && network_standard_globals.hosting_hold_to_pick_up);",
                       "network_standard_globals.hosting_hold_to_pick_up);"), "legacy-host"),
    # (a client's own setting, not its host's)
    "client-own-setting": (("return !TEST_FLAG(game->standard_flags, _network_game_hold_to_pick_up_bit);",
                            "return config_boolean(\"game.auto_pickup\") != 0;"), "client"),
}


def generated(fault=None):
    standard = read("port/linux/game/network_standard.c")
    config = enum_with(read("source/networking/network_game_manager.h"), "_network_game_hold_to_pick_up_bit") + "\n" + \
        enum_with(read("port/linux/game/demo.h"), "_demo_rule_walk_over_pickup_bit") + "\n"
    code = "\n".join([
        function(standard, "network_standard_game_for_machine"),
        function(standard, "network_standard_stamp_game"),
        function(standard, "network_standard_walk_over_pickup"),
        function(read("source/game/players.c"), "unit_should_autopick_weapon"),
    ])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("weapon_pickup", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("weapon_pickup", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
