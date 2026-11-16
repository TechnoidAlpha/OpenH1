"""A bot against active camouflage (port/linux/game/bots.c's bots_camo_*): it notices a camouflaged enemy by a chance
that falls with the distance and grows with the enemy's motion (across its sight more than along it), the centre of its
view and its skill, and much more with a giveaway (a shot, the camouflage's flicker, a hit, a blow); a still one at
range it rarely notices but now and then, a running one close by soon but not surely at once; out of view only one close
by, or one firing at a bot hurt. It loses one it fights the more as it crosses the bot's sight, goes quiet and is
further, the less the more skilled the bot, and hardly while it fires. Its aim at one is blurred three times when the
enemy is quiet, not at all while it fires: its error settles slower and its wander is larger."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["distance", "motion", "skill", "giveaways", "still-at-range", "view", "losing", "aim"]
FUNCTIONS = ("bots_random", "bots_random_real", "bots_random_signed", "bots_angle_difference", "bots_aim_settle",
             "bots_camo_size", "bots_camo_giveaway", "bots_camo_view", "bots_camo_notice_rate", "bots_camo_lose_rate",
             "bots_camo_seen", "bots_camo_blur")
DEFINES = ("BOTS_PI", "BOTS_DEGREES", "BOTS_UNREACHABLE_ITEMS", "BOTS_CAMO_RUN_SPEED", "BOTS_CAMO_NEAR",
           "BOTS_CAMO_GIVEAWAY_NEAR", "BOTS_CAMO_FLICKER_RATE", "BOTS_CAMO_FIRED_RATE", "BOTS_CAMO_HURT_RATE",
           "BOTS_CAMO_MELEE_RATE")

# a fault, the function it is in (None: the skill table), and the case that must catch it
NEGATIVE_CONTROLS = {
    "distance-ignored": (("bots_camo_size(sight->distance, BOTS_CAMO_NEAR) *", "1.0f *"), "bots_camo_notice_rate",
                         "distance"),
    "motion-ignored": (("MIN((sight->across + 0.4f * sight->along) / BOTS_CAMO_RUN_SPEED, 1.5f)",
                        "MIN(0.0f * sight->across, 1.5f)"), "bots_camo_notice_rate", "motion"),
    "along-as-across": (("0.4f * sight->along", "1.0f * sight->along"), "bots_camo_notice_rate", "motion"),
    "legendary-sees-as-easy": (("1.00f, 1.5f, 0.35f,", "1.00f, 0.5f, 0.35f,"), None, "skill"),
    "legendary-loses-as-easy": (("1.00f, 1.5f, 0.35f,", "1.00f, 1.5f, 1.2f,"), None, "skill"),
    "shot-unseen": (("BOTS_CAMO_FIRED_RATE * sight->fired", "0.0f * sight->fired"), "bots_camo_giveaway",
                    "giveaways"),
    "flicker-unseen": (("BOTS_CAMO_FLICKER_RATE * (1.0f - sight->camouflage)", "0.0f * (1.0f - sight->camouflage)"),
                       "bots_camo_giveaway", "giveaways"),
    "hit-unseen": (("BOTS_CAMO_HURT_RATE * sight->hurt", "0.0f * sight->hurt"), "bots_camo_giveaway", "giveaways"),
    "blow-unseen": (("(sight->melee ? BOTS_CAMO_MELEE_RATE : 0.0f)", "0.0f"), "bots_camo_giveaway", "giveaways"),
    "noticed-surely": ((": bots_camo_notice_rate(skill, sight);", ": 1000.0f;"), "bots_camo_seen", "still-at-range"),
    "never-noticed-still": (("(0.1f + 1.4f * motion)", "(1.4f * motion)"), "bots_camo_notice_rate", "still-at-range"),
    "behind-seen": (("return sight->bot_hurt && sight->fired > 0.0f ? 0.3f : 0.0f;", "return 0.3f;"), "bots_camo_view",
                    "view"),
    "view-flat": (("return 1.0f - 0.7f * sight->off_view;", "return 1.0f;"), "bots_camo_view", "view"),
    "never-lost": (("return skill->camo_lose *", "return 0.0f * skill->camo_lose *"), "bots_camo_lose_rate", "losing"),
    "lost-while-firing": (("/\n\t\t(giveaway * giveaway);", "/ 1.0f;"), "bots_camo_lose_rate", "losing"),
    "no-blur": (("return 1.0f + 2.0f * sight->camouflage", "return 1.0f + 0.0f * sight->camouflage"), "bots_camo_blur",
                "aim"),
    "blur-ignores-giveaway": (("(1.0f - away)", "(1.0f - 0.0f * away)"), "bots_camo_blur", "aim"),
    "settles-unblurred": (("skill->aim_settle_seconds * blur *", "skill->aim_settle_seconds *"), "bots_aim_settle",
                          "aim"),
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
    config = f"#define TICKS_PER_SECOND {constant(read('source/cseries/cseries.h'), 'TICKS_PER_SECOND')}\n"
    config += "".join(_define(source, name) for name in DEFINES)
    config += enum_with(source, "_bots_skill_easy") + "\n"
    config += structure(source, "bots_skill_definition") + "\n"
    table = _skill_table(source)
    if fault and faulty_function is None:
        table = mutated(table, *fault)
    config += table + "\n"
    config += structure(source, "bots_bot") + "\n"
    config += structure(source, "bots_camo_sight") + "\n"
    code = ""
    for name in FUNCTIONS:
        text = function(source, name)
        if fault and name == faulty_function:
            text = mutated(text, *fault)
        code += text + "\n"
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("bots_camo", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("bots_camo", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"bots.c with a fault ({control}) passed '{case}': the test cannot see it"
