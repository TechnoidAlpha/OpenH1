"""The wide layout of the port's own screens (port/linux/game/menu_tags.c's ui_widget_port_wide_layout,
tools/port_settings.py's wide): the extra width a view lays them out in and a wide attribute's part of it, the
generator's copy of them the engine's; Settings and Multiplayer at 4:3, 16:10 (the Steam Deck), 16:9 and 21:9 (16:9's,
centred), every part on the screen and WIDE_MARGIN from its edges, nothing over another, each row, item, tab and button where a click finds it; and
the game's own menus left centred. The server browser's are test_browser_menu.py's, the carnage report's
test_carnage_report.py's."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, function, mutated, read, run  # noqa: E402
from harness import menu_layout  # noqa: E402
from harness.menu_layout import inner, layout, named, one, usable  # noqa: E402

port_settings = menu_layout.port_settings

CASES = ["extra", "scale"]

NEGATIVE_CONTROLS = {
    # 21:9 laid out wider than 16:9
    "wider-than-16-9": (("width >= WIDE_LAYOUT_WIDTH ? WIDE_LAYOUT_EXTRA", "width >= 1920 ? WIDE_LAYOUT_EXTRA"),
                        "extra"),
    # the parts rounded toward 0: the left edge's move a unit short
    "rounded-down": (("(scaled < 0 ? -WIDE_LAYOUT_EXTRA / 2 : WIDE_LAYOUT_EXTRA / 2)", "0"), "scale"),
}

MT = port_settings.MT
WIDTHS = port_settings.WIDE_SCREENS


def generated(fault=None):
    tags = read("port/linux/game/menu_tags.c")
    config = "\n".join(re.findall(r"^#define WIDE_LAYOUT_\w+ [^\n]+", tags, re.M)) + "\n"
    code = "\n\n".join(function(tags, name) for name in ("ui_widget_port_wide_extra_of", "ui_widget_port_wide_scale"))
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("wide_layout", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("wide_layout", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_generator_is_the_engine():
    """tools/port_settings.py's wide layout, which writes the files and lays them out for these tests, is
    menu_tags.c's: the same widths, and each value's part of each extra width."""
    tags = read("port/linux/game/menu_tags.c")
    assert f"#define WIDE_LAYOUT_WIDTH {port_settings.WIDE_WIDTH}" in tags
    status, output = run(build("wide_layout", generated()), "table")
    assert status == 0, output
    for line in output.splitlines():
        value, extra, part = (int(word) for word in line.split())
        assert port_settings.wide_scale(value, extra) == part, (value, extra, part)
    for width in (0, 640, 700, 768, 852, 853, 1138, 1920):
        assert port_settings.wide_extra_of(width) == min(max(width - 640, 0), port_settings.WIDE_EXTRA)


def test_game_menus_stay_centred():
    """Only the port's own screens (the generator's files) are laid out wide, and Multiplayer's title (the PC
    version's, over the hub: WIDGET_PATCHES); every wide attribute is "dx dw" within the extra width."""
    ours = {*port_settings.settings_files(), *port_settings.multiplayer_files(), *port_settings.campaign_files()}
    patched = {name for name, patch in port_settings.WIDGET_PATCHES.items() if "wide" in patch.get("set", {})}
    assert patched == {f"{MT}/header_multiplayer"}
    for path in sorted(menu_layout.CE.glob("*.xml")):
        for name, wide in re.findall(r'<widget name="([^"]+)"[^>]*? wide="([^"]*)"', path.read_text()):
            dx, dw = (int(value) for value in wide.split())
            assert abs(dx) <= port_settings.WIDE_EXTRA and abs(dw) <= port_settings.WIDE_EXTRA, name
            assert path.name in ours or name in patched, f"{name} ({path.name}) is the game's own, laid out wide"


def _check_apart(boxes, what):
    for index, box in enumerate(boxes):
        for other in boxes[index + 1:]:
            assert not box.overlaps(other), f"{what}: {box} over {other}"


def _settings_screens():
    """each tab and page, laid out as the desktop's, or as Android's where only Android has its rows (Screen
    Layout)"""
    pe = port_settings.PE

    def platforms(page):
        rows = [row for row in page["rows"] if row[0] != port_settings.SECTION]
        android = all(row[4] and "android" in row[4].split() for row in rows)
        return menu_layout.ANDROID if rows and android else menu_layout.DESKTOP

    return [(folder, f"{pe}/{folder}/{page['screen']}", "tab" in page, platforms(page))
            for folder, page in port_settings.PAGES.items()]


@pytest.mark.parametrize("aspect", WIDTHS)
@pytest.mark.parametrize("folder,root,tab,platforms", _settings_screens())
def test_settings_layout(aspect, folder, root, tab, platforms):
    """Each of Settings' tabs and pages: its rows from the screen's left (wider on a wide screen, each label before
    its value, the values in the rows), the panel beside them to its right (its words and pictures in it), the tabs
    spread across it, apart, between LB's and RB's prompts, DEFAULTS at its left and OK and CANCEL at its right;
    all on the screen, none over another; 21:9 as 16:9."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(root, width, platforms)
    rows = [box for box in boxes if box.name.startswith("op_") and box.path[-1] == "options_menu"]
    sections = [box for box in boxes if re.fullmatch(r"section_\d+", box.name)]
    panel = one(boxes, "tab_panel" if tab else "page_panel")
    assert rows
    for box in rows + sections + [panel]:
        assert box.inside(area), f"{box} is within the margin of the screen's edge {area}"
    for box in rows + sections:
        assert box.x1 + 8 <= panel.x0, f"{box} reaches the panel {panel}"
    # (each row's label before its value, both in the row)
    for row in rows:
        parts = [box for box in boxes if box.path and box.path[-1] == row.name]
        labels = [box for box in parts if box.name.endswith("_label")]
        values = [box for box in parts if box.name.endswith(("_spinner", "_track", "_value"))]
        assert len(labels) == 1, row
        for value in values:
            assert value.inside(row, slack=6), f"{value} is not in its row {row}"
            assert labels[0].x1 <= value.x0, f"{labels[0]} reaches its value {value}"
        assert labels[0].inside(row), labels[0]
    for name in ("panel_name", "panel_value", "panel_text", "panel_note", "panel_classic", "panel_enhanced",
                 "panel_classic_caption", "panel_enhanced_caption"):
        assert one(boxes, name).inside(panel), name
    buttons = [one(boxes, f"button_{name}") for name in ("defaults", "ok", "cancel")]
    for box in buttons:
        assert box.inside(area), box
    _check_apart(buttons, "buttons")
    assert buttons[0].x0 < panel.x0 and buttons[2].x1 > panel.x0
    if tab:
        tabs = [box for box in boxes if box.path[-1:] == ("tabs",)]
        prompts = named(boxes, "tab_prompt_previous") + named(boxes, "tab_prompt_next")
        assert len(tabs) == len(port_settings.SETTINGS_TAB_PAGES) and len(prompts) == 2
        for box in tabs + prompts:
            assert box.inside(area), box
        _check_apart(tabs + prompts, "tabs")
        assert prompts[0].x1 <= min(box.x0 for box in tabs) and prompts[1].x0 >= max(box.x1 for box in tabs)
    # (wider as the screen is: at 16:9 the rows and panel as wide as the usable screen, less the margins; 21:9's
    # 16:9's)
    if width >= port_settings.WIDE_WIDTH:
        assert layout(root, width, platforms) == layout(root, port_settings.WIDE_WIDTH, platforms)
        assert rows[0].x0 - usable(width).x0 == 28 and usable(width).x1 - panel.x1 == 28
    if width > 640:
        narrow = [box for box in layout(root, 640, platforms) if box.name in (rows[0].name, panel.name)]
        assert rows[0].x1 - rows[0].x0 > narrow[0].x1 - narrow[0].x0
        assert panel.x1 - panel.x0 > narrow[1].x1 - narrow[1].x0


@pytest.mark.parametrize("aspect", WIDTHS)
def test_hub_layout(aspect):
    """Multiplayer on the computer: its items under their sections from the screen's left, its panel beside them to
    its right, what the panel shows in it (the counts, the best games' names before their players and pings), the
    footer's prompts at the screen's edges, the items above them; on the screen, none over another; its title moved
    with its items."""
    width = WIDTHS[aspect]
    area = inner(width)
    for root, page_items in ((f"{MT}/multiplayer_type_select_screen", 9), (f"{MT}/hub/create_screen", 3),
                             (f"{MT}/hub/coop_screen", 2)):
        boxes = layout(root, width)
        items = [box for box in boxes if box.name.startswith("hub_") and box.path[-1].endswith("_list")
                 and box.name not in ("hub_panel", "hub_prompts")]
        panel = one(boxes, "hub_panel")
        assert len(items) == page_items, items
        for box in items + [panel]:
            assert box.inside(area), f"{box} is within the margin of the screen's edge {area}"
            assert box is panel or box.x1 + 8 <= panel.x0, f"{box} reaches the panel"
        _check_apart(items, "items")
        for box in boxes:
            if box.path and box.path[-1] == "hub_panel":
                assert box.inside(panel, slack=1), f"{box} is out of the panel {panel}"
            if box.name.endswith("_note"):
                assert box.inside([item for item in items if item.name == box.path[-1]][0]), box
        for row in range(port_settings.BEST_GAMES):
            name, players, ping = (one(boxes, f"best_{row}_{part}") for part in ("name", "players", "ping"))
            assert name.x1 <= players.x0 and players.x1 <= ping.x0, (name, players, ping)
        prompts = [box for box in boxes if box.name.startswith("prompt_")]
        for box in prompts:
            assert box.inside(area), box
        _check_apart(prompts, "prompts")
        for box in items:
            assert box.y1 <= min(prompt.y0 for prompt in prompts) - 4, f"{box} reaches the footer"
        title = one(boxes, "header_multiplayer")
        assert title.x0 - items[0].x0 == 35 - port_settings.HUB_LIST_X


@pytest.mark.parametrize("aspect", WIDTHS)
def test_friends_layout(aspect):
    """The Friends screen, laid out as Multiplayer is: its rows (each note in its row) under their section from the
    screen's left, apart, above the footer, the panel beside them to its right with its words in it, its footer's
    prompts on the screen and, of those one row shows at once, none over another."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(f"{MT}/friends/friends_screen", width)
    panel = one(boxes, "friends_panel")
    section = one(boxes, "section_friends")
    assert panel.inside(area) and section.inside(area) and section.x1 + 8 <= panel.x0
    rows = [one(boxes, f"fr_row_{row}") for row in range(port_settings.FRIENDS_ROWS)]
    prompts = {box.name: box for box in boxes if box.name.startswith("prompt_")}
    for row, box in enumerate(rows):
        assert box.inside(area) and box.x0 >= section.x0 and box.x1 <= section.x1 and box.x1 + 8 <= panel.x0, box
        assert one(boxes, f"fr_note_{row}").inside(box) and one(boxes, f"fr_live_{row}").inside(box), row
        assert box.y1 <= min(prompt.y0 for prompt in prompts.values()) - 4, f"{box} reaches the footer"
    _check_apart(rows, "rows")
    for box in boxes:
        if box.path and box.path[-1] == "friends_panel":
            assert box.inside(panel), f"{box} is out of the panel {panel}"
    for box in prompts.values():
        assert box.inside(area), box
    # (the prompts each kind of row shows: friends_menu.inc's friends_prompts_of)
    for shown in (["copy", "code", "hide"], ["copy", "code", "show"], ["join", "invite", "remove"],
                  ["accept", "ignore", "block"], ["cancel"], ["add", "block"], ["unblock"]):
        _check_apart([prompts[f"prompt_{name}"] for name in shown] + [prompts["prompt_back"]], f"prompts {shown}")


@pytest.mark.parametrize("aspect", WIDTHS)
def test_matchmaking_layout(aspect):
    """The Matchmaking screen, laid out as Multiplayer is: its playlists' rows (each note in its row) under their
    section from the screen's left, apart, the panel beside them to its right with its words in it, its footer's
    prompts at the screen's edges; on the screen, none over another."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(f"{MT}/matchmaking/matchmaking_screen", width)
    panel = one(boxes, "matchmaking_panel")
    section = one(boxes, "section_matchmaking")
    assert panel.inside(area) and section.inside(area) and section.x1 + 8 <= panel.x0
    rows = [one(boxes, f"mm_row_{row}") for row in range(port_settings.MATCHMAKING_ROWS)]
    for row, box in enumerate(rows):
        assert box.inside(area) and box.x0 >= section.x0 and box.x1 <= section.x1 and box.x1 + 8 <= panel.x0, box
        assert one(boxes, f"mm_note_{row}").inside(box), row
    _check_apart(rows, "rows")
    for box in boxes:
        if box.path and box.path[-1] == "matchmaking_panel":
            assert box.inside(panel), f"{box} is out of the panel {panel}"
    prompts = [box for box in boxes if box.name.startswith("prompt_")]
    for box in prompts:
        assert box.inside(area), box
    for left in ("prompt_search", "prompt_cancel"):
        assert one(boxes, left).x0 - usable(width).x0 == 28


@pytest.mark.parametrize("aspect", WIDTHS)
def test_party_layout(aspect):
    """The Party screen, laid out as Multiplayer is: its members' rows (each name before where it is, the leader's
    pill at the row's end) under their section from the screen's left, the panel beside them to its right with its
    words in it, its footer's prompts at the screen's edges; on the screen, none over another."""
    width = WIDTHS[aspect]
    area = inner(width)
    boxes = layout(f"{MT}/party/party_screen", width)
    panel = one(boxes, "party_panel")
    section = one(boxes, "section_party")
    assert panel.inside(area) and section.inside(area) and section.x1 + 8 <= panel.x0
    for row in range(port_settings.PARTY_ROWS):
        name, where, leader = (one(boxes, f"party_{part}_{row}") for part in ("name", "where", "leader"))
        for box in (name, where, leader):
            assert box.inside(area) and box.x0 >= section.x0 and box.x1 <= section.x1, box
        assert name.x1 <= where.x0, (name, where)
    for box in boxes:
        if box.path and box.path[-1] == "party_panel":
            assert box.inside(panel), f"{box} is out of the panel {panel}"
    prompts = [box for box in boxes if box.name.startswith("prompt_")]
    for box in prompts:
        assert box.inside(area), box
    for left in ("prompt_make", "prompt_copy"):
        assert one(boxes, left).x0 - usable(width).x0 == 28
