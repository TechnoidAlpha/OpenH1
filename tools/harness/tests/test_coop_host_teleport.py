"""Host Controls' BRING TO ME and GO TO in the co-op host's simulation (source/game/players.c's
players_coop_host_can_teleport and players_coop_host_teleport): another machine's living player who is not away, the
host's player in the level, GO TO only to a player inside it, never in versus or a cutscene; the move beside the other
player, else in them."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["who", "bring", "go-to"]

# a fault, and the case that must catch it
NEGATIVE_CONTROLS = {
    "versus-too": (("if (!network_coop_active() || game_connection()", "if (game_connection()"), "who"),
    "away-moved": (("player->local_player_index != NONE ||\n\t\tnetwork_coop_player_away(player_index))",
                    "player->local_player_index != NONE)"), "who"),
    "own-moved": (("player->unit_index == NONE || player->local_player_index != NONE ||",
                   "player->unit_index == NONE ||"), "who"),
    "go-to-outside": (("(go_to && !players_coop_unit_in_structure(player->unit_index))", "FALSE"), "go-to"),
    "no-stacking": (("!player_teleport(moving_player_index, beside_unit_index, &center) &&\n"
                     "\t\t!players_coop_teleport_stacked(moving_player_index, beside_unit_index, &center)",
                     "!player_teleport(moving_player_index, beside_unit_index, &center)"), "bring"),
    "wrong-way-round": (("*moving_player_index = go_to ? unit_get(host_unit_index)->unit.player_index : player_index;",
                         "*moving_player_index = player_index;"), "go-to"),
}


def generated(fault=None):
    source = read("source/game/players.c")
    code = "\n".join(function(source, name) for name in (
        "players_coop_host_teleport_parties", "players_coop_host_can_teleport", "players_coop_host_teleport"))
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("coop_host_teleport", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("coop_host_teleport", generated(fault)), case)
    assert status == CHECK_FAILED, f"players.c with a fault ({control}) passed '{case}': the test cannot see it"
