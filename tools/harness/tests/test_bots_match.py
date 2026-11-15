"""A match run from the bot control (port/linux/game/bots_match.c): match start's words read as the game takes them
(a map and a gametype, --bots, --seed, --score, --time; no rules smuggled into the rotation entry), the rotation entry it
plays, where match speed may run (a dedicated server, or a game no other machine is in) and how fast at most, and match
at's lines given to the tick they were asked for, in their order, none before it; and that what the tick runs of it
takes no random number or clock, so a match plays the same from the same seed and lines."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["parse", "entry", "speed", "schedule", "lines"]
SOURCE = "port/linux/game/bots_match.c"
FUNCTIONS = ("bots_match_parse_start", "bots_match_entry_text", "bots_match_speed_allowed", "bots_match_speed_kept",
             "bots_match_due", "bots_match_note_line")
TICK_FUNCTIONS = ("bots_match_due", "bots_match_note_line", "bots_match_note_kill")
NONDETERMINISTIC = re.compile(r"\b(rand|random|srand|time|clock|system_milliseconds|SDL_GetTicks\w*|"
                              r"posix_random_bytes)\s*\(")

NEGATIVE_CONTROLS = {
    "rules-smuggled": ("bots_match_parse_start", ("strchr(word, ':') || strchr(word, '+') ||", ""), "parse"),
    "seed-zero": ("bots_match_parse_start", ("value > 4294967295.0 || value < 1.0", "value > 4294967295.0"), "parse"),
    "score-dropped": ("bots_match_entry_text", ("\"+score_to_win=%ld\"", "\"+score=%ld\""), "entry"),
    "speed-with-others": ("bots_match_speed_allowed", ("server_mode || remote_machines == 0", "TRUE"), "speed"),
    "speed-unbounded": ("bots_match_speed_kept", ("asked > (real)BOTS_MATCH_SPEED_MAXIMUM ?", "0 ?"), "speed"),
    "line-early": ("bots_match_due", ("if (next->tick > tick)", "if (next->tick > tick + 1)"), "schedule"),
    "lines-unbounded": ("bots_match_note_line", ("if (bots_match.line_count >= BOTS_MATCH_LINES)",
                                                 "if (bots_match.line_count > BOTS_MATCH_LINES)"), "lines"),
}


def globals_structure(source):
    end = source.index("} bots_match = ")
    start = source.rindex("static struct\n{", 0, end)
    return source[start:source.index(";", end) + 1]


def generated(fault=None):
    source = read(SOURCE)
    header = read("port/linux/game/bots_match.h")
    command = read("port/linux/game/bots_command.h")
    config = "\n".join([
        enum_with(command, "BOTS_COMMAND_LINE_SIZE"),
        enum_with(header, "BOTS_MATCH_NAME_SIZE"),
        structure(header, "bots_match_options"),
        enum_with(source, "BOTS_MATCH_PLAYERS"),
        enum_with(source, "_bots_match_none"),
        structure(source, "bots_match_player"),
        structure(source, "bots_match_line"),
        structure(source, "bots_match_kill"),
        globals_structure(source),
        "",
    ])
    code = ""
    for name in FUNCTIONS:
        text = function(source, name)
        if fault and fault[0] == name:
            text = mutated(text, *fault[1])
        code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_match", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    name, fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_match", generated((name, fault))), case)
    assert status == CHECK_FAILED, f"the match with a fault ({control}) passed '{case}': the test cannot see it"


def nondeterministic_calls(source):
    return [(name, match.group(0)) for name in TICK_FUNCTIONS
            for match in NONDETERMINISTIC.finditer(function(source, name))]


def test_the_tick_takes_no_random_number_or_clock():
    """what the tick runs of a match is a function of the game state and its lines"""
    assert nondeterministic_calls(read(SOURCE)) == []


def test_the_check_sees_a_clock():
    """(its negative control)"""
    source = read(SOURCE)
    text = function(source, "bots_match_note_line")
    faulty = source.replace(text, mutated(text, "kept->tick = tick;", "kept->tick = (long)system_milliseconds();"))
    assert nondeterministic_calls(faulty) == [("bots_match_note_line", "system_milliseconds(")]
