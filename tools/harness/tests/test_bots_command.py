"""The host's commands to its bots (port/linux/game/bots_command.c): a line parsed as the game takes it, its orders,
traits, counts and the errors it says; the traits a style or a value sets; the goal each order gives a bot (to a point,
pressing on, about it once there; a hold; a defense; a teammate followed; the enemy flag taken, its carrier hunted),
and that an attack on an enemy goes where the bot's team saw them, never where they are; the lines that come between
frames applied at the next tick in their order, none before it; the traits' effect on a bot's own play (bots.c). And
that nothing the tick runs of it takes a random number or a clock, so a game and its replay play the same."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["parse", "coordinates", "traits", "attack-point", "defend", "enemy-unseen", "enemy-seen", "follow", "flag",
         "queue", "aggression", "teamwork"]
SOURCE = "port/linux/game/bots_command.c"
FUNCTIONS = ("bots_command_normalize", "bots_command_words", "bots_command_name_index", "bots_command_team_word",
             "bots_command_join", "bots_command_coordinates", "bots_command_parse", "bots_command_traits_default",
             "bots_command_trait_set", "bots_command_unit_position", "bots_command_live_player",
             "bots_command_set_objective", "bots_command_order_fail", "bots_command_objective",
             "bots_command_enqueue", "bots_command_tick_begin")
BOTS_FUNCTIONS = ("bots_bot_recharge_shield", "bots_bot_team_minded")
# what the tick runs of it (bots_command_tick_begin's lines, the goals each bot asks for each tick)
TICK_FUNCTIONS = ("bots_command_tick_begin", "bots_command_apply", "bots_command_order_give", "bots_command_resolve",
                  "bots_command_places", "bots_command_select", "bots_command_objective", "bots_command_flag_events",
                  "bots_command_note_kill", "bots_command_team_middle", "bots_command_trait_set",
                  "bots_command_select_for", "bots_command_objective_events", "bots_command_hill_control")
# a random number, a clock: what would make a replay part from its game
NONDETERMINISTIC = re.compile(r"\b(rand|random|srand|time|clock|system_milliseconds|SDL_GetTicks\w*|"
                              r"posix_random_bytes|bots_random\w*)\s*\(")

# a fault in a function, and the case that must catch it
NEGATIVE_CONTROLS = {
    "wall-hack": ("bots_command_objective", ("\t\t\treturn FALSE;\n\t\t}\n\t\t/* (a teammate: with them) */",
                                             "\t\t\tbots_command_set_objective(objective, _bots_objective_hunt, &where, "
                                             "2.0f, TRUE);\n\t\t\treturn TRUE;\n\t\t}\n\t\t/* (a teammate: with them) */"),
                  "enemy-unseen"),
    "hunts-where-it-is": ("bots_command_objective", ("_bots_objective_hunt, &known[index].position",
                                                     "_bots_objective_hunt, &where"), "enemy-seen"),
    "never-there": ("bots_command_objective", ("BOTS_COMMAND_ARRIVE_REACH * BOTS_COMMAND_ARRIVE_REACH", "0"),
                    "attack-point"),
    "hold-gives-way": ("bots_command_objective", ("&point, BOTS_COMMAND_HOLD_RADIUS, TRUE)",
                                                  "&point, BOTS_COMMAND_HOLD_RADIUS, FALSE)"), "defend"),
    "flag-not-taken": ("bots_command_objective", ("objective->object_index = order->target.object_index;", ""), "flag"),
    "last-line-first": ("bots_command_tick_begin", ("bots_command.queue[bots_command.queue_first]",
                                                    "bots_command.queue[(bots_command.queue_first + "
                                                    "bots_command.queue_count - 1) % BOTS_COMMAND_QUEUE]"), "queue"),
    "all-red-is-all": ("bots_command_parse", ("bots_command_team_word(words[2]) != NONE", "0"), "parse"),
    "aggression-unchecked": ("bots_command_parse", ("!(value >= 0.0 && value <= 1.0)", "!(value >= 0.0)"), "parse"),
    "style-keeps-aggression": ("bots_command_trait_set", ("traits->aggression = bots_command_styles[style].aggression;",
                                                          ""), "traits"),
    "aggressive-bot-recharges": ("bots_bot_recharge_shield", ("aggression >= 0.8f", "aggression >= 2.0f"),
                                 "aggression"),
    "teamwork-ignored": ("bots_bot_team_minded", ("if (teamwork >= 1.0f)", "if (teamwork >= 0.0f)"), "teamwork"),
}


def table(source, name):
    """a static table's definition (its name to its "};")"""
    match = re.search(r"^static [^\n]*\b" + re.escape(name) + r"\b[^\n]*=\s*\n?\{", source, re.M)
    assert match, f"{name} not found in {SOURCE}"
    return source[match.start():source.index("};", match.end()) + 2]


def globals_structure(source):
    """bots_command.c's globals (static struct {...} bots_command;)"""
    end = source.index("} bots_command;")
    start = source.rindex("static struct\n{", 0, end)
    return source[start:end + len("} bots_command;")]


def generated(fault=None):
    source = read(SOURCE)
    header = read("port/linux/game/bots_command.h")
    objectives = read("port/linux/game/bots_objectives.h")
    knowledge = read("port/linux/game/bots_knowledge.h")
    vehicles = read("port/linux/game/bots_vehicles.h")
    bots = read("port/linux/game/bots.c")
    config = "\n".join([
        "enum { BOTS_KNOWLEDGE_MAXIMUM_KNOWN = 16 };",
        enum_with(objectives, "_bots_objective_take"),
        structure(objectives, "bots_objective"),
        structure(knowledge, "bots_known_enemy"),
        enum_with(vehicles, "_bots_vehicle_jeep"),
        enum_with(header, "_bots_command_attack"),
        enum_with(header, "BOTS_COMMAND_LINE_SIZE"),
        enum_with(source, "BOTS_COMMAND_EVENTS"),
        *re.findall(r"^#define BOTS_COMMAND_\w+ [^\n]+$", source, re.M),
        enum_with(source, "_bots_request_help"),
        enum_with(source, "_bots_trait_skill"),
        enum_with(source, "_bots_order_state_active"),
        enum_with(source, "_bots_target_none"),
        structure(source, "bots_command_request"),
        structure(source, "bots_command_target"),
        structure(source, "bots_command_order"),
        structure(source, "bots_command_traits"),
        table(source, "bots_command_order_names"),
        table(source, "bots_command_skill_names"),
        table(source, "bots_command_trait_names"),
        table(source, "bots_command_vehicle_names"),
        source[source.index("/* the styles: their aggression and teamwork */"):
               source.index("#define NUMBER_OF_BOTS_COMMAND_STYLES")],
        re.search(r"^#define NUMBER_OF_BOTS_COMMAND_STYLES[^\n]+$", source, re.M).group(0),
        globals_structure(source),
        "",
    ])
    code = ""
    for name in FUNCTIONS:
        text = function(source, name)
        if fault and fault[0] == name:
            text = mutated(text, *fault[1])
        code += text + "\n"
    for name in BOTS_FUNCTIONS:
        text = function(bots, name)
        if fault and fault[0] == name:
            text = mutated(text, *fault[1])
        code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_command", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    name, fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_command", generated((name, fault))), case)
    assert status == CHECK_FAILED, f"the commands with a fault ({control}) passed '{case}': the test cannot see it"


def nondeterministic_calls(source):
    """the random numbers and clocks the tick's functions call: (function, call)"""
    found = []
    for name in TICK_FUNCTIONS:
        for match in NONDETERMINISTIC.finditer(function(source, name)):
            found.append((name, match.group(0)))
    return found


def test_the_tick_takes_no_random_number_or_clock():
    """what the tick runs of the commands is a function of the game state and the lines (a replay of its demo,
    and every machine of a network game, see the same)"""
    assert nondeterministic_calls(read(SOURCE)) == []


def test_the_check_sees_a_clock():
    """(its negative control: a clock in an order's resolution is found)"""
    source = read(SOURCE)
    text = function(source, "bots_command_order_give")
    faulty = source.replace(text, mutated(text, "order.given_tick = game_time_get();",
                                          "order.given_tick = (long)system_milliseconds();"))
    assert nondeterministic_calls(faulty) == [("bots_command_order_give", "system_milliseconds(")]
