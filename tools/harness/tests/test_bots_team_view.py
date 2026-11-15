"""A team's coach's view (port/linux/game/bots_command.c's --team, bot_control.c's team tokens): what the team knows
(bots_command_team_json) lists its own players where they are, and of its enemies only those its bots saw, where and
when they last saw them (bots_knowledge.c's bots_knowledge_team_known), never where they are now and never one it did
not see; and the watch's events (bots_command_events_answer) give it the other team's orders not at all and a kill of
theirs it was not part of without where it happened, while the host sees every event whole."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["team-knows", "events"]
COMMAND = "port/linux/game/bots_command.c"
KNOWLEDGE = "port/linux/game/bots_knowledge.c"
COMMAND_FUNCTIONS = ("bots_command_copy", "bots_command_event_add", "bots_command_player_teams", "bots_command_say",
                     "bots_command_say_json", "bots_command_events_answer", "bots_command_player_name",
                     "bots_command_unit_position", "bots_command_live_player", "bots_command_json_point",
                     "bots_command_team_json")
KNOWLEDGE_FUNCTIONS = ("bots_knowledge_team_known",)
JSMN = '#include "../../third_party/jsmn/jsmn.h"'

NEGATIVE_CONTROLS = {
    # (--team red never shows blue positions it has not seen)
    "enemies-listed": ("bots_command_team_json", ("if (player->quit_out_of_game || player->team_index != team)",
                                                  "if (player->quit_out_of_game)"), "team-knows"),
    "other-team-sightings": ("bots_knowledge_team_known", ("&bots_knowledge.team_seen[team][index].latest",
                                                           "&bots_knowledge.team_seen[1 - team][index].latest"),
                             "team-knows"),
    "where-they-are-now": ("bots_command_team_json", ("bots_command_json_point(out, \"position\", &known[index].position);",
                                                      "bots_command_live_player(known[index].player_index, "
                                                      "&known[index].position);\n\t\tbots_command_json_point(out, "
                                                      "\"position\", &known[index].position);"), "team-knows"),
    "events-unfiltered": ("bots_command_events_answer", ("if (text && view_team != NONE && "
                                                         "!(bots_command.event_teams[slot] & (1UL << view_team)))",
                                                         "if (0)"), "events"),
    "public-is-whole": ("bots_command_event_add", ("bots_command_copy(public_text)",
                                                   "bots_command_copy(server_json_text(json))"), "events"),
}


def generated(fault=None):
    command = read(COMMAND)
    knowledge = read(KNOWLEDGE)
    json = mutated(read("port/linux/src/server_json.c"), JSMN, '#include "jsmn.h"')
    config = "\n".join([
        enum_with(command, "BOTS_COMMAND_EVENTS"),
        structure(command, "bots_command_reply"),
        structure(read("port/linux/game/bots_knowledge.h"), "bots_known_enemy"),
        structure(knowledge, "bots_knowledge_sighting"),
        structure(knowledge, "bots_knowledge_team_sighting"),
        "",
    ])
    code = ""
    for source, names in ((knowledge, KNOWLEDGE_FUNCTIONS), (command, COMMAND_FUNCTIONS)):
        for name in names:
            text = function(source, name)
            if fault and fault[0] == name:
                text = mutated(text, *fault[1])
            code += text + "\n"
    return (("server_json.h", read("port/linux/src/server_json.h")), ("jsmn.h", read("port/third_party/jsmn/jsmn.h")),
            ("server_json.inc", json), ("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_team_view", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    name, fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_team_view", generated((name, fault))), case)
    assert status == CHECK_FAILED, f"the team view with a fault ({control}) passed '{case}': the test cannot see it"
