"""The campaign's screens (tools/port_settings.py's _campaign_menu, _missions and _difficulties; port/linux/game/
campaign_menu.inc): Choose Mission's row of cards (campaign_cards.c: the chosen card in the middle but at the row's
ends, the larger, the row in the middle and wider apart on a wide screen, a step kept to the row, the slide eased
out), the generator's sizes the engine's, and the three screens at 4:3 to 21:9: their parts on the screen and within
its margin, the cards' row between its arrows, the footers' prompts apart, the main menu's CAMPAIGN and co-op's
player 2 opening them, the names menu_functions.c finds."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402
from harness import menu_layout  # noqa: E402
from harness.menu_layout import inner, layout, named, one, screen  # noqa: E402

port_settings = menu_layout.port_settings
CAMPAIGN = port_settings.CAMPAIGN
WIDTHS = port_settings.WIDE_SCREENS

CASES = ["first-centred", "first-at-ends", "place-chosen-larger", "place-wide-apart", "step-kept-to-row",
         "slide-eases"]

# a fault in campaign_menu.inc, and the case that must catch it
NEGATIVE_CONTROLS = {
    # the chosen card at the row's left, not its middle
    "chosen-first": (("return (short)PIN(chosen - MISSION_SLOTS / 2, 0, count - MISSION_SLOTS);",
                      "return (short)PIN(chosen, 0, count - MISSION_SLOTS);"), "first-centred"),
    # past the last mission's card
    "past-the-end": (("return (short)PIN(chosen - MISSION_SLOTS / 2, 0, count - MISSION_SLOTS);",
                      "return (short)MAX(chosen - MISSION_SLOTS / 2, 0);"), "first-at-ends"),
    # every card the same size
    "no-larger-card": (("total = (short)(total + (slot == chosen_slot ? MISSION_BIG_WIDTH : MISSION_SMALL_WIDTH)",
                        "total = (short)(total + MISSION_SMALL_WIDTH"), "place-chosen-larger"),
    "step-wraps": (("return (short)(count > 0 ? PIN(chosen + step, 0, count - 1) : 0);",
                    "return (short)(count > 0 ? (chosen + step + count) % count : 0);"), "step-kept-to-row"),
    "slide-linear": (("return from * left * left * left;", "return from * left;"), "slide-eases"),
}

FUNCTIONS = ("mission_cards_first", "mission_cards_place", "mission_cards_step", "mission_cards_slide")


def generated(fault=None):
    source = read("port/linux/game/campaign_menu.inc")
    config = "\n".join(re.findall(r"^#define MISSION_\w+ [^\n]+", source, re.M)) + "\n"
    code = "\n\n".join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("campaign_cards", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("campaign_cards", generated(fault)), case)
    assert status == CHECK_FAILED, f"campaign_menu.inc with a fault ({control}) passed '{case}': the test cannot see it"


def _define(source, name):
    return int(re.search(rf"^#define {name} (\d+)", source, re.M).group(1))


def test_generator_sizes_are_the_engines():
    """The cards' sizes, gap and top, and the Campaign menu's items, as campaign_menu.inc lays them out, are the
    generator's (the files' cards are that size)."""
    source = read("port/linux/game/campaign_menu.inc")
    assert (_define(source, "MISSION_BIG_WIDTH"), _define(source, "MISSION_BIG_HEIGHT")) == port_settings.MISSION_BIG
    assert (_define(source, "MISSION_SMALL_WIDTH"),
            _define(source, "MISSION_SMALL_HEIGHT")) == port_settings.MISSION_SMALL
    assert _define(source, "MISSION_CARD_GAP") == port_settings.MISSION_CARD_GAP
    assert _define(source, "MISSION_CARDS_TOP") == port_settings.MISSION_CARDS_TOP
    assert _define(source, "MISSION_DOTS") == port_settings.MISSION_DOTS
    assert _define(source, "MISSION_DOT_STEP") == port_settings.MISSION_DOT_STEP
    assert _define(source, "CAMPAIGN_ITEMS_TOP") == port_settings.CAMPAIGN_ITEMS_TOP
    assert _define(source, "CAMPAIGN_CONTINUE_HEIGHT") == port_settings.CAMPAIGN_CONTINUE[1]
    assert _define(source, "CAMPAIGN_ITEM_HEIGHT") == port_settings.HUB_ITEM[1]
    every = menu_layout.widgets()
    for kind, size in (("big", port_settings.MISSION_BIG), ("small", port_settings.MISSION_SMALL)):
        for slot in range(3):
            attributes = every[f"{CAMPAIGN}/card_{kind}_{slot}"].attributes
            assert (int(attributes["width"]), int(attributes["height"])) == size


def test_the_menus_open_them():
    """The main menu's CAMPAIGN opens the Campaign menu, a new profile's campaign the difficulties, and split-screen
    co-op's player 2 Choose Mission: no screen opens the old list (New Game's, Load Game's)."""
    main_menu = (menu_layout.CE / "main_menu.xml").read_text()
    assert main_menu.count(f'open="{CAMPAIGN}/campaign_screen"') == 2
    assert f'open="{CAMPAIGN}/difficulty_screen"' in main_menu
    coop = (menu_layout.CE / "main_menu.multiplayer_type_select.coop.xml").read_text()
    assert f'open="{CAMPAIGN}/mission_screen"' in coop
    for path in menu_layout.CE.glob("*.xml"):
        if path.name.startswith(("main_menu.campaign_select", "main_menu.solo_level_select")):
            continue
        text = path.read_text()
        for old in ("campaign_select/campaign_menu", "solo_level_select/solo_level_select_screen",
                    "difficulty_select/difficulty_select_list_screen", "load_level_select_screen"):
            assert f'open="main_menu/{old}"' not in text, (path.name, old)


def test_functions_are_named():
    """Every function and game data input the campaign's file runs is one menu_tags.c names."""
    tags = read("port/linux/game/menu_tags.c")
    text = (menu_layout.CE / "main_menu.campaign.xml").read_text()
    for name in sorted(set(re.findall(r'(?:run|input)="(port campaign[^"]*)"', text))):
        assert f'"{name}"' in tags, name


def _apart(boxes, what):
    for index, box in enumerate(boxes):
        for other in boxes[index + 1:]:
            assert not box.overlaps(other), f"{what}: {box} over {other}"


@pytest.mark.parametrize("aspect", WIDTHS)
def test_campaign_menu_layout(aspect):
    """The Campaign menu: its items down the left, the panel beside them, its picture and words in it; within the
    screen's margin."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(f"{CAMPAIGN}/campaign_screen", width)
    items = [one(boxes, name) for name in ("item_continue", "item_missions", "item_coop", "item_back")]
    panel = one(boxes, "campaign_panel")
    for box in items + [panel]:
        assert box.inside(area), box
        assert box.inside(screen(width)), box
    for box in items:
        assert box.x1 + 8 <= panel.x0, (box, panel)
    for name in ("panel_art", "panel_number", "panel_title", "panel_status", "panel_objective", "panel_hint"):
        assert one(boxes, name).inside(panel), name
    assert one(boxes, "continue_art").inside(one(boxes, "item_continue"))


@pytest.mark.parametrize("aspect", WIDTHS)
def test_mission_screen_layout(aspect):
    """Choose Mission: the arrows within the margin with the row of cards between them (the larger in any slot,
    clear of both), the dots under the cards, the panel under them across the screen, its words and badges in it,
    HALO and CUSTOM at the top right, the footer's prompts apart."""
    width = WIDTHS[aspect]
    extra = port_settings.wide_extra_of(width)
    area = inner(width)
    boxes = layout(f"{CAMPAIGN}/mission_screen", width)
    previous, following = one(boxes, "arrow_previous"), one(boxes, "arrow_next")
    for box in (previous, following):
        assert box.inside(area), box
    # (the row as campaign_menu.inc's mission_cards_place lays it out)
    big, small = port_settings.MISSION_BIG[0], port_settings.MISSION_SMALL[0]
    gap = port_settings.MISSION_CARD_GAP + extra // 8
    total = big + 2 * small + 2 * gap
    assert previous.x1 + 4 <= 320 - total // 2, (previous, total)
    assert 320 - total // 2 + total + 4 <= following.x0, (following, total)
    cards_bottom = port_settings.MISSION_CARDS_TOP + port_settings.MISSION_BIG[1]
    panel = one(boxes, "mission_panel")
    assert panel.inside(area)
    assert cards_bottom <= port_settings.MISSION_DOTS_TOP <= panel.y0 - port_settings.MISSION_DOT[1]
    for name in ("detail_title", "detail_objective", "detail_status", "detail_status_new", "detail_beaten"):
        assert one(boxes, name).inside(panel), name
    badges = named(boxes, "detail_badge")
    assert len(badges) == 4
    for box in badges + named(boxes, "detail_badge_name"):
        assert box.inside(panel), box
    _apart(badges, "the panel's badges")
    assert one(boxes, "detail_objective").x1 <= min(box.x0 for box in badges)
    tabs = [one(boxes, "tab_halo"), one(boxes, "tab_custom")]
    for box in tabs:
        assert box.inside(area), box
    _apart(tabs, "the tabs")
    prompts = [one(boxes, "prompt_select"), one(boxes, "prompt_back")]
    for box in prompts:
        assert box.inside(area), box
    _apart(prompts, "the prompts")


@pytest.mark.parametrize("aspect", WIDTHS)
def test_difficulty_screen_layout(aspect):
    """Choose Difficulty: four cards across the screen within its margin, apart, each its badge, name, words and
    BEATEN in it; PLAY and BACK apart."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(f"{CAMPAIGN}/difficulty_screen", width)
    cards = [one(boxes, f"difficulty_{index}") for index in range(4)]
    for card in cards:
        assert card.inside(area), card
        for name in ("difficulty_badge_box", "difficulty_badge", "difficulty_name", "difficulty_blurb",
                     "difficulty_beaten"):
            parts = [box for box in named(boxes, name) if box.path[-1] == card.name]
            assert len(parts) == 1 and parts[0].inside(card), (name, card)
    _apart(cards, "the cards")
    prompts = [one(boxes, "prompt_play"), one(boxes, "prompt_back")]
    _apart(prompts, "the prompts")


@pytest.mark.parametrize("aspect", WIDTHS)
def test_mission_cards_fit(aspect):
    """A card's parts within it: its picture, MISSION 0N, its name, its four badges (apart) and NOT YET BEATEN."""
    for kind in ("big", "small"):
        boxes = layout(f"{CAMPAIGN}/card_{kind}_0", WIDTHS[aspect])
        card = boxes[0]
        for box in boxes[1:]:
            if box.name.startswith("card_"):
                assert box.inside(card), (kind, box)
        badges = named(boxes, f"card_badge_{kind}")
        assert len(badges) == 4
        _apart(badges, "the card's badges")
        assert one(boxes, f"card_title_{kind}").y1 <= min(box.y0 for box in badges)
