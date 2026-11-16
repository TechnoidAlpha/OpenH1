"""Player 1's last used profile (source/interface/player_ui.c): lastprof.txt looked for once. The menus and the party
(party_update, ten times a second in a game) ask for it, and without the file (no profile chosen yet: a new player, a
headless or test game) each look logged two lines to debug.txt. Nor is it looked for before this run's enumeration of
the profiles has written the mapfile, on the filesystem thread (#888)."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["missing", "there", "before_enumeration"]
NAME = "player_ui_get_player1_last_used_profile_index"

NEGATIVE_CONTROLS = {
    "looked-for-each-time": (("player1_profile_path_looked_for = TRUE;", "player1_profile_path_looked_for = FALSE;"),
                             "missing"),
    "looked-for-before-enumeration": ((" && !saved_game_files_enumerated()", ""), "before_enumeration"),
}


def generated(fault=None):
    code = function(read("source/interface/player_ui.c"), NAME)
    if fault:
        code = mutated(code, *fault)
    return (("under_test.inc", code + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("last_profile_lookup", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("last_profile_lookup", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
