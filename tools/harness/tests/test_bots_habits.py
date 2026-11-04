"""A fighting bot's habits (port/linux/game/bots.c's bots_aimed_at, bots_strafe and bots_strike_reach): it knows when its
target's aim is on it; it strafes one way while no one aims at it and turns about once it reacts to an aim on it (its
skill's dodge reaction and chance), again a moment later while the aim stays, never before its reaction; with no aim
on it, the other way about every second (people's rhythm); a blow is
struck at the engine's reach (unit_cause_player_melee_damage's rays) to the target's surface, further charging in."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["aimed-at", "strafe", "strike-reach"]
FUNCTIONS = ("bots_random", "bots_random_real", "bots_random_seconds", "bots_aimed_at", "bots_strafe", "bots_strike_reach")

# a fault, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "aim-beside-taken": (("< radius * radius", "< radius * radius * 100.0f"), "bots_aimed_at", "aimed-at"),
    "aim-behind-taken": (("if (along <= 0.0f || length <= 0.0f)", "if (length <= 0.0f)"), "bots_aimed_at",
                         "aimed-at"),
    "dodges-at-once": (("bot->dodge_tick = now + (long)(skill->dodge_reaction_seconds * TICKS_PER_SECOND);",
                        "bot->dodge_tick = now;"), "bots_strafe", "strafe"),
    "never-dodges": (("if (!bot->dodging || now < bot->dodge_tick)", "if (1)"), "bots_strafe", "strafe"),
    "dodge-chance-ignored": (("bots_random_real(bot) < skill->dodge_chance", "TRUE"), "bots_strafe", "strafe"),
    "strafes-on-a-timer": (("bots_random_seconds(bot, 0.95f, 0.5f, 0.35f, 2.5f)", "3.0f"), "bots_strafe", "strafe"),
    "strafes-too-quickly": (("bots_random_seconds(bot, 0.95f, 0.5f, 0.35f, 2.5f)", "0.2f"), "bots_strafe", "strafe"),
    "reach-hard-coded": (("return BOTS_MELEE_REACH + MIN(target_radius, 0.4f) + (charging ? 0.3f : 0.0f);",
                          "return 1.1f;"), "bots_strike_reach", "strike-reach"),
}


def _define(source, name):
    match = re.search(r"^#define\s+" + re.escape(name) + r"\s+(.+)$", source, re.M)
    if not match:
        raise LookupError(f"#define not found in the sources: {name}")
    return f"#define {name} {match.group(1)}\n"


def _skill_table(source):
    match = re.search(r"^static struct bots_skill_definition const bots_skills\[NUMBER_OF_BOTS_SKILLS\] =\n\{.*?^\};",
                      source, re.M | re.S)
    if not match:
        raise LookupError("bots_skills not found in bots.c")
    return match.group(0)


def generated(fault=None, faulty_function=None):
    source = read("port/linux/game/bots.c")
    config = "".join(_define(source, name) for name in ("BOTS_PI", "BOTS_DEGREES", "BOTS_UNREACHABLE_ITEMS",
                                                         "BOTS_MELEE_REACH"))
    config += f"#define TICKS_PER_SECOND {constant(read('source/cseries/cseries.h'), 'TICKS_PER_SECOND')}\n"
    config += enum_with(source, "_bots_skill_easy") + "\n"
    config += structure(source, "bots_skill_definition") + "\n"
    config += _skill_table(source) + "\n"
    config += structure(source, "bots_bot") + "\n"
    code = ""
    for name in FUNCTIONS:
        text = function(source, name)
        if fault and name == faulty_function:
            text = mutated(text, *fault)
        code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_habits", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_habits", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"bots.c with a fault ({control}) passed '{case}': the test cannot see it"
