"""The VR build's Basic Training's logic (port/linux/game/basic_training.c): each step's check of what the player did,
the chapters skipped to forwards and back past those not made yet, the step a jump to a chapter begins at (its own
set up, nothing of the chapters before it), the steps' table, the finished chapters' setting, and from
port/linux/game/bots.c where an ordered bot goes (still, side to side, peeking from cover)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["step-checks", "headset-steps", "targets-dry", "score", "chapters", "jumps", "steps-table", "finished", "words",
         "order-goal"]
TRAINING = ("basic_training_step_result", "basic_training_chapter_ready", "basic_training_next_chapter",
            "basic_training_first_step", "basic_training_jump_step", "basic_training_finished_parse",
            "basic_training_finished_text", "basic_training_words")
BOTS = ("bots_order_goal",)

# a fault in the code, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "dry-never-fails": (("return seen->magazine_dry ? _training_result_failed : _training_result_waiting;",
                         "return _training_result_waiting;"), "basic_training_step_result", "targets-dry"),
    "fit-needs-headset": (("return seen->fitted || !seen->vr ?", "return seen->fitted ?"),
                          "basic_training_step_result", "headset-steps"),
    "score-one-more": (("if (seen->kills >= step->amount)", "if (seen->kills > step->amount)"),
                       "basic_training_step_result", "score"),
    "turn-one-short": (("seen->turned >= step->amount", "seen->turned + 1.0f >= step->amount"),
                       "basic_training_step_result", "step-checks"),
    "skip-into-unmade": (("chapter != TRAINING_CHAPTER_SNIPER &&", ""), "basic_training_chapter_ready", "chapters"),
    "jump-unclamped": (("first + PIN(step_in_chapter, 0, count - 1)", "first + step_in_chapter"),
                       "basic_training_jump_step", "jumps"),
    "finished-past-end": (("chapter < BASIC_TRAINING_CHAPTERS)\n\t\t\t\tfinished", "chapter <= BASIC_TRAINING_CHAPTERS)\n\t\t\t\tfinished"),
                          "basic_training_finished_parse", "finished"),
    "peek-inside-out": (("*exposed = phase >= period;", "*exposed = phase < period;"), "bots_order_goal", "order-goal"),
}


def table(source, start):
    """an array's definition, its initializer whole"""
    at = source.index(start)
    return source[at:source.index("\n};", at) + 3]


def generated(fault=None, faulty_function=None):
    training = read("port/linux/game/basic_training.c")
    header = read("port/linux/game/basic_training.h")
    bots_header = read("port/linux/game/bots.h")
    bots = read("port/linux/game/bots.c")
    config = ""
    for name in ("BASIC_TRAINING_CHAPTERS", "BASIC_TRAINING_LEFT_GRIP"):
        config += f"#define {name} {constant(header, name)}\n"
    for name in ("TRAINING_CHAPTER_SNIPER", "TRAINING_CHAPTER_WARTHOG", "TRAINING_SPOTS"):
        config += f"#define {name} {constant(training, name)}\n"
    for member in ("_training_button_a", "_training_check_turn", "_training_setup_clear", "_training_loadout_rifle =",
                   "_training_result_waiting"):
        config += enum_with(training, member) + "\n"
    config += enum_with(bots_header, "_bots_order_free = 0") + "\n"
    config += structure(bots_header, "bots_order") + "\n"
    for name in ("basic_training_chapter", "basic_training_step", "basic_training_seen"):
        config += structure(training, name) + "\n"
    config += table(training, "static struct basic_training_chapter const basic_training_chapters[") + "\n"
    config += table(training, "static struct basic_training_step const basic_training_steps[]") + "\n"
    config += "#define TRAINING_STEP_COUNT ((short)NUMBEROF(basic_training_steps))\n"
    text = ""
    for source, names in ((training, TRAINING), (bots, BOTS)):
        for name in names:
            code = function(source, name)
            if fault and name == faulty_function:
                code = mutated(code, *fault)
            text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("basic_training", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("basic_training", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_sounds_are_the_games():
    # the training's beeps and announcer are the game's multiplayer sounds by their place in its enum
    sounds = read("source/game/game_engine_multiplayer_sounds.c")
    body = sounds[sounds.index("enum multiplayer_information_sound"):]
    names = re.findall(r"^\t(_multiplayer_sound_\w+)", body[:body.index("};")], re.M)
    training = read("port/linux/game/basic_training.c")
    for ours, theirs in (("_training_sound_slayer", "_multiplayer_sound_slayer"),
                         ("_training_sound_countdown_timer", "_multiplayer_sound_countdown_timer"),
                         ("_training_sound_team_slayer", "_multiplayer_sound_team_slayer"),
                         ("_training_sound_countdown_timer_end", "_multiplayer_sound_countdown_timer_end")):
        assert constant(training, ours) == names.index(theirs), ours


def test_buttons_are_the_prompts():
    # the steps' buttons are numbered as port/linux/src/vr.c's halo_vr_button_name numbers its prompts
    vr = read("port/linux/src/vr.c")
    prompts = re.findall(r"\{ (VR_\w+), \"", vr[vr.index("} prompt_inputs[] ="):])
    assert prompts[0] == "VR_BUTTON_A" and prompts[2] == "VR_BUTTON_X"
    assert prompts[7] == "VR_INPUT_RIGHT_TRIGGER" and prompts[10] == "VR_BUTTON_DPAD_LEFT"
    assert prompts[11] == "VR_BUTTON_DPAD_RIGHT" and len(prompts) == 16
    training = read("port/linux/game/basic_training.c")
    order = re.findall(r"^\t(_training_button_\w+)", training[training.rindex("enum", 0, training.index("_training_button_a = 0")):], re.M)
    assert order[:16] == ["_training_button_a", "_training_button_b", "_training_button_x", "_training_button_y",
                          "_training_button_right_bumper", "_training_button_left_bumper",
                          "_training_button_left_trigger", "_training_button_right_trigger",
                          "_training_button_dpad_up", "_training_button_dpad_down", "_training_button_dpad_left",
                          "_training_button_dpad_right", "_training_button_menu", "_training_button_view",
                          "_training_button_left_stick", "_training_button_right_stick"]


def test_offered_in_the_headset_only():
    # the main menu's BASIC TRAINING is a child of the VR build in the headset only, and the menus call the
    # training's functions by the names the port's function tables give them
    main_menu = read("port/assets/menus/ce/main_menu.xml")
    assert '<child widget="main_menu/main_menu_item_training" x="192" y="211" platform="vr"/>' in main_menu
    tags = read("port/linux/game/menu_tags.c")
    for name in ("port training chapter", "port training open", "port training update"):
        assert f'"{name}"' in tags
    screen = read("port/assets/menus/ce/main_menu.training.xml")
    assert screen.count('run="port training chapter"') == 2 * constant(read("tools/port_settings.py"), "TRAINING_CHAPTERS")
