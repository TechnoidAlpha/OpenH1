"""A campaign mission in progress started again (#680; port/linux/game/campaign_menu.inc's campaign_restart_*,
menu_functions.c's campaign_start and campaign_continue, game_state.c's game_state_save_to_persistent_storage): RESTART
offered with a saved game, on the Campaign menu and on its mission's card in Choose Mission; the question's RESTART
plays the mission from its start at the saved game's difficulty, without loading it; CANCEL changes nothing; the saved
game's file is kept until the new start's first checkpoint. And the screens: RESTART MISSION under CONTINUE, RESTART
in Choose Mission's footer apart from its other prompts, the question laid out as the other boxes are."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402
from harness import menu_layout  # noqa: E402
from harness.menu_layout import inner, layout, one  # noqa: E402

port_settings = menu_layout.port_settings
CAMPAIGN = port_settings.CAMPAIGN
WIDTHS = port_settings.WIDE_SCREENS

CAMPAIGN_MENU = "port/linux/game/campaign_menu.inc"
FUNCTIONS = "port/linux/game/menu_functions.c"
GAME_STATE = "source/saved games/game_state.c"

CASES = ["offered", "confirm-fresh-start", "continue-goes-on", "cancel-keeps", "kept-until-checkpoint"]

# a fault in the code (the file it is in, the text and its replacement) and the case that must catch it
NEGATIVE_CONTROLS = {
    # RESTART on every card, not only the saved game's
    "offered-on-every-card": (CAMPAIGN_MENU, ("return saved_level != NONE && (chosen == NONE || (!custom && chosen == "
                                              "saved_level));", "return saved_level != NONE;"), "offered"),
    # RESTART goes on with the saved game (as CONTINUE does)
    "restart-continues": (CAMPAIGN_MENU, ("\tcampaign_start(map_name, difficulty, controller);\n"
                                          "\tmain_disallow_persistent_storage();\n",
                                          "\tcampaign_start(map_name, difficulty, controller);\n"),
                          "confirm-fresh-start"),
    # RESTART at another difficulty than the saved game's
    "restart-on-normal": (CAMPAIGN_MENU, ("\tcampaign_start(map_name, difficulty, controller);\n"
                                          "\tmain_disallow_persistent_storage();\n",
                                          "\tcampaign_start(map_name, 1, controller);\n"
                                          "\tmain_disallow_persistent_storage();\n"), "confirm-fresh-start"),
    # CANCEL leaves the question asked: a RESTART after it starts the mission
    "cancel-keeps-asked": (CAMPAIGN_MENU, ("\tcampaign_restart_level = NONE;\n\treturn TRUE;\n}",
                                           "\treturn TRUE;\n}"), "cancel-keeps"),
    # RESTART does not check the saved game is still the one asked about
    "restart-unchecked": (CAMPAIGN_MENU, (" || level != campaign_restart_level)", ")"), "cancel-keeps"),
    # leaving before a checkpoint writes the game state over the saved game
    "written-without-checkpoint": (GAME_STATE, ("if (player_spawn_count==1 && !demo_playing() && "
                                                "(game_state_port_saved_game_valid() || recover_saved_games_hack))",
                                                "if (player_spawn_count==1 && !demo_playing())"),
                                   "kept-until-checkpoint"),
}

MENU_NAMES = ("campaign_restart_offered", "campaign_restart_ask", "campaign_restart", "campaign_restart_cancel")


def generated(control=None):
    sources = {path: read(path) for path in (CAMPAIGN_MENU, FUNCTIONS, GAME_STATE)}
    if control:
        path, fault, _ = NEGATIVE_CONTROLS[control]
        sources[path] = mutated(sources[path], *fault)
    menu = sources[CAMPAIGN_MENU]
    level = re.search(r"^static short campaign_restart_level = NONE;$", menu, re.M)
    if not level:
        raise LookupError("campaign_restart_level not found in the sources")
    code = [level.group(0)]
    code += [function(sources[FUNCTIONS], name) for name in ("campaign_start", "campaign_fail", "campaign_continue")]
    code += [function(menu, name) for name in MENU_NAMES]
    code.append(function(sources[GAME_STATE], "game_state_save_to_persistent_storage"))
    return (("under_test.inc", "\n\n".join(code) + "\n"),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("campaign_restart", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    case = NEGATIVE_CONTROLS[control][2]
    status, output = run(build("campaign_restart", generated(control)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_functions_are_named():
    """The question's functions and its update are ones menu_tags.c names, and campaign_menu.inc runs."""
    tags = read("port/linux/game/menu_tags.c")
    menu = read(CAMPAIGN_MENU)
    for name in ("restart ask", "missions restart", "restart", "restart cancel", "restart update"):
        assert f'"port campaign {name}"' in tags, name
        assert f'!strcmp(name, "{name}")' in menu, name


@pytest.mark.parametrize("aspect", WIDTHS)
def test_layout(aspect):
    """RESTART MISSION within the screen's margin, apart from the other items; Choose Mission's RESTART apart from
    SELECT and BACK; the question's buttons in its box."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(f"{CAMPAIGN}/campaign_screen", width)
    items = [one(boxes, name) for name in ("item_continue", "item_restart", "item_missions", "item_coop", "item_back")]
    for index, box in enumerate(items):
        assert box.inside(area), box
        for other in items[index + 1:]:
            assert not box.overlaps(other), (box, other)
    boxes = layout(f"{CAMPAIGN}/mission_screen", width)
    prompts = [one(boxes, name) for name in ("prompt_restart", "prompt_select", "prompt_back")]
    for index, box in enumerate(prompts):
        assert box.inside(area), box
        for other in prompts[index + 1:]:
            assert not box.overlaps(other), (box, other)
    boxes = layout(f"{CAMPAIGN}/restart_screen", width)
    question = boxes[0]
    for name in ("restart_button_confirm", "restart_button_cancel", "header_restart"):
        assert one(boxes, name).inside(question), name
