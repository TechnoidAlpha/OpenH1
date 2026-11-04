"""The lobby on the computer (tools/port_settings.py's _lobby_board, port/linux/game/lobby_menu.inc): its data (each
team's column, a game without teams' one list, the order, the tags, this machine's own row, the host's controls and a
player's, a game of this machine alone's bots: lobby_board.c), and its layout at 4:3, 16:10, 16:9 and 21:9 (16:9's,
centred): the teams' columns and the details panel apart and WIDE_MARGIN from the screen's edges, each row in its
column with its dot, name and tag in it, the panel's words and controls in it, the footer's prompts apart; the
touchscreen's the same players with its own panel and buttons. Each check has a fault that it must catch."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402
from harness import menu_layout  # noqa: E402
from harness.menu_layout import inner, layout, one, usable  # noqa: E402

port_settings = menu_layout.port_settings

LOBBY = "port/linux/game/lobby_menu.inc"
MT = port_settings.MT
ROOT_SCREEN = f"{MT}/lobby/lobby_screen"
WIDTHS = port_settings.WIDE_SCREENS
CASES = ["teams", "free-for-all", "overflow", "party", "tags", "controls"]
PURE = ("lobby_board_teams", "lobby_board_tag", "lobby_board_rank", "lobby_board_lay_out", "lobby_board_controls_for")

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    # (every player in red's column)
    "one-team": ("short team = entries[order[index]].team == 1 ? 1 : 0;", "short team = 0;", "teams"),
    # (a game without teams split by team)
    "teams-always": ("\tif (!teams)\n\t{\n\t\tshown", "\tif (FALSE)\n\t{\n\t\tshown", "free-for-all"),
    # (the host not first)
    "host-not-first": ("if (entry->host && !entry->bot)\n\t\treturn 0;", "if (FALSE)\n\t\treturn 0;", "party"),
    # (the party's members not together)
    "party-apart": ("if (entry->party && !entry->bot)\n\t\treturn 1;", "if (FALSE)\n\t\treturn 1;", "party"),
    # (a full column's last row a player, not how many more)
    "no-more": ("\t\t\tlayout->rows[column][LOBBY_BOARD_ROWS - 1] = NONE;\n", "", "overflow"),
    # (an OpenCE player tagged as the party's)
    "opence-as-party": ("\tif (entry->kind == _lobby_kind_opence)\n\t\treturn _lobby_tag_opence;\n\tif (entry->party)\n"
                        "\t\treturn _lobby_tag_party;",
                        "\tif (entry->party)\n\t\treturn _lobby_tag_party;\n\tif (entry->kind == _lobby_kind_opence)\n"
                        "\t\treturn _lobby_tag_opence;", "tags"),
    # (a player given the host's START GAME)
    "player-starts": ("controls->start = host;", "controls->start = TRUE;", "controls"),
    # (a network game's host given bots)
    "coop-bots": ("controls->bots = host && !cooperative;", "controls->bots = host;", "controls"),
    "bots-for-players": ("controls->bots = host && !cooperative;", "controls->bots = !cooperative;", "controls"),
    # (Infection's teams chosen in the lobby)
    "infection-teams": ("return teams && mode != _game_mode_infection;", "return teams;", "controls"),
}


def generated(fault=None):
    lobby = read(LOBBY)
    functions = read("port/linux/game/menu_functions.c")
    config = "".join(f"#define {name} {constant(functions, name)}\n" for name in ("LOBBY_BOARD_ROWS",
                                                                                  "LOBBY_BOARD_COLUMNS"))
    config += f"#define LOBBY_BOARD_ENTRIES {constant(read('port/linux/include/halo_port_limits.h'), 'HALO_PORT_MAXIMUM_NETWORK_PLAYERS')}\n"
    config += enum_with(read("source/game/game_engine.h"), "_game_mode_infection") + "\n"
    config += enum_with(lobby, "_lobby_tag_none") + "\n" + enum_with(lobby, "_lobby_kind_unknown") + "\n"
    config += "\n".join(structure(lobby, name) for name in ("lobby_board_entry", "lobby_board_layout",
                                                             "lobby_board_controls")) + "\n"
    # (the tags' words, as narrow strings)
    words = re.search(r"static wchar_t const \*const lobby_tag_words\[[^\]]*\] =\s*\{[^}]*\};", lobby).group(0)
    config += words.replace("wchar_t", "char").replace('L"', '"').replace("lobby_tag_words", "lobby_tag_words_test")
    code = "\n\n".join(function(lobby, name) for name in PURE)
    if fault:
        code = mutated(code, *NEGATIVE_CONTROLS[fault][:2])
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("lobby_board", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    case = NEGATIVE_CONTROLS[control][2]
    status, output = run(build("lobby_board", generated(control)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def test_generator_is_the_file_and_the_code():
    """The lobby's file is as tools/port_settings.py writes it, the generator's rows and gap the code's, and every
    widget the code looks for by name is there."""
    name = f"{MT}/lobby".replace("/", ".") + ".xml"
    assert (menu_layout.CE / name).read_text() == "\n".join(port_settings.multiplayer_files()[name]), \
        "run python3 tools/port_settings.py"
    functions = read("port/linux/game/menu_functions.c")
    lobby = read(LOBBY)
    assert constant(functions, "LOBBY_BOARD_ROWS") == port_settings.LOBBY_ROWS
    assert constant(functions, "LOBBY_BOARD_COLUMNS") == 2
    assert constant(lobby, "LOBBY_BOARD_COLUMN_GAP") == port_settings.LOBBY_COLUMN_GAP
    every = menu_layout.widgets()
    names = {widget.rsplit("/", 1)[-1] for widget in every}
    looked_for = set(re.findall(r'named\([^,]+, "(board_\w+|prompt_\w+)"', lobby))
    assert looked_for and looked_for <= names, looked_for - names
    for part in ("row", "name", "dot", "tag"):
        for column in range(2):
            for row in range(port_settings.LOBBY_ROWS):
                assert f"board_{part}_{column}_{row}" in names
    for function_name in ("port lobby board update", "port lobby team red", "port lobby team blue",
                          "port lobby team toggle", "port lobby team click", "port lobby start", "port lobby invite",
                          "port lobby settings"):
        assert f'"{function_name}"' in read("port/linux/game/menu_tags.c"), function_name
        assert f'"{function_name}"' in functions, function_name


def _apart(boxes, what):
    for index, box in enumerate(boxes):
        for other in boxes[index + 1:]:
            assert not box.overlaps(other), f"{what}: {box} over {other}"


def check_lobby_layout(width):
    """the computer's lobby at a width (the AssertionError a fault in its layout makes)"""
    area = inner(width)
    boxes = layout(ROOT_SCREEN, width)
    red, blue, players, panel = (one(boxes, name) for name in ("board_team_0", "board_team_1", "board_all",
                                                                "board_panel"))
    for box in (red, blue, players, panel):
        assert box.inside(area), f"{box} is within the margin of the screen's edge {area}"
    assert red.x1 + port_settings.LOBBY_COLUMN_GAP <= blue.x0, (red, blue)
    assert blue.x1 + 8 <= panel.x0, (blue, panel)
    # (a game without teams' list where both columns are, to the unit the narrower screens' rounding allows; the
    # players' focus over it, for a click on a column)
    assert (players.x0, players.y0, players.y1) == (red.x0, red.y0, red.y1) and abs(players.x1 - blue.x1) <= 1, \
        (players, red, blue)
    focus = one(boxes, "board_players")
    assert (focus.x0, focus.x1, focus.y0, focus.y1) == (players.x0, players.x1, players.y0, players.y1), focus
    for column, team in enumerate((red, blue)):
        title, count = one(boxes, f"board_title_{column}"), one(boxes, f"board_count_{column}")
        assert title.inside(team) and count.inside(team) and title.x0 < count.x0, (title, count, team)
        for row in range(port_settings.LOBBY_ROWS):
            row_box = one(boxes, f"board_row_{column}_{row}")
            assert row_box.inside(team), f"{row_box} is out of its column {team}"
            dot, name, tag = (one(boxes, f"board_{part}_{column}_{row}") for part in ("dot", "name", "tag"))
            for part in (dot, name, tag):
                assert part.inside(row_box), f"{part} is out of its row {row_box}"
            # (a bot's tag, with its skill, wider than a player's: its name fitted to what is left,
            # lobby_board_bot_tag; a player's tag where the name ends)
            assert dot.x1 <= name.x0 and name.x1 <= tag.x1 - port_settings.LOBBY_TAG_WIDTH, (dot, name, tag)
            assert tag.x1 + 8 <= row_box.x1, f"{tag} against its row's frame {row_box}"
    assert one(boxes, "board_title_all").inside(players)
    for box in boxes:
        if box.path and box.path[-1] == "board_panel":
            assert box.inside(panel), f"{box} is out of the panel {panel}"
    controls = [one(boxes, name) for name in ("board_bots_count", "board_bots_skill", "board_bots_teams",
                                              "board_host", "board_settings", "board_start")]
    for box in controls:
        assert box.inside(panel), f"{box} is out of the panel {panel}"
    _apart(controls + [one(boxes, name) for name in ("board_status", "board_after", "board_opence")],
           "the panel's controls")
    for key in ("bots_count", "bots_skill", "bots_teams"):
        row, label, spinner = (one(boxes, name) for name in (f"board_{key}", f"board_{key}_label",
                                                             f"board_{key}_spinner"))
        assert label.inside(row) and spinner.inside(row) and label.x1 <= spinner.x0, (row, label, spinner)
    prompts = [box for box in boxes if box.path and box.path[-1] == "board_prompts"]
    assert len(prompts) == len(port_settings.LOBBY_PROMPTS)
    for box in prompts:
        assert box.inside(area), box
    _apart(prompts, "prompts")
    # (the title over the columns, as the 640 have it over theirs)
    assert one(boxes, "board_header").x0 - red.x0 == 35 - port_settings.LOBBY_LIST_X
    if width >= port_settings.WIDE_WIDTH:
        assert layout(ROOT_SCREEN, width) == layout(ROOT_SCREEN, port_settings.WIDE_WIDTH)
        assert red.x0 - usable(width).x0 == 28 and usable(width).x1 - panel.x1 == 28


@pytest.mark.parametrize("aspect", WIDTHS)
def test_lobby_layout(aspect):
    check_lobby_layout(WIDTHS[aspect])


def test_touchscreen_lobby_is_the_board():
    """On a touchscreen the lobby is the computer's players and its own panel and buttons (_touch_lobby_board, its
    tap targets test_touch_screens.py's); the list of players and buttons it had is gone."""
    touch = layout(ROOT_SCREEN, 852, frozenset({"touch", "android"}))
    computer = layout(ROOT_SCREEN, 852)
    names = {box.name for box in touch}
    assert {"board_row_0_0", "board_row_1_12", "board_panel", "board_start", "board_settings"} <= names
    assert "lobby_list" not in names and "lobby_list" not in {box.name for box in computer}
    # (the players where the computer has them; the panel and its controls the touchscreen's own)
    assert [box for box in touch if box.name.startswith("board_row_")] == \
        [box for box in computer if box.name.startswith("board_row_")]
    assert {box.path[-1] for box in touch if box.name == "board_start"} == {"board_prompts"}


# a fault in the layout (a widget's wide attribute, or a child's place), and the width it shows at
LAYOUT_FAULTS = {
    # (the panel wider than the room to the screen's right)
    "panel-past-margin": (f"{MT}/lobby/board_panel", "wide", "120 120", 852),
    # (blue's column growing into the panel)
    "blue-into-panel": (f"{MT}/lobby/board_team_1", "wide", "-36 150", 852),
    # (a row's tag left where the 640 have it while its name grows)
    "tag-stays": (f"{MT}/lobby/board_tag_1_3", "wide", "0 0", 852),
    # (a red row grown as both columns are)
    "row-past-column": (f"{MT}/lobby/board_row_0_5", "wide", "-106 140", 768),
}


@pytest.mark.parametrize("fault", LAYOUT_FAULTS)
def test_layout_negative_control(fault, monkeypatch):
    name, attribute, value, width = LAYOUT_FAULTS[fault]
    every = dict(menu_layout.widgets())
    widget = every[name]
    every[name] = widget._replace(attributes={**widget.attributes, attribute: value})
    monkeypatch.setattr(menu_layout, "widgets", lambda: every)
    with pytest.raises(AssertionError):
        check_lobby_layout(width)
