"""The touchscreen's layouts of the redesigned multiplayer screens (tools/port_settings.py's _touch_*: the menus'
"touch" platform, the Android and iOS apps and debug.touch_menus): Multiplayer and its pages, the server browser,
the lobby, Host Controls, the Party and Friends screens at each display's width, from a 4:3 tablet's (and a folding phone's
inner screen, laid out as 4:3) to a 21:9 phone's. Every tap target is at least TOUCH_TAP tall, 44 points or more on
each display of TOUCH_DISPLAYS; on the screen, WIDE_MARGIN from its edges; over no other; the words fixed in it fit
it (with the menus' fonts: Pillow). The touch layouts have the widgets menu_functions.c fills by name, as many rows
as it has room for, and the pictures they draw; the computer's layouts keep theirs (test_wide_layout.py,
test_browser_menu.py, test_lobby_board.py)."""

import re
import sys
import xml.etree.ElementTree as ElementTree
from functools import lru_cache
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, read  # noqa: E402
from harness import menu_layout  # noqa: E402
from harness.menu_layout import CE, inner, layout  # noqa: E402

port_settings = menu_layout.port_settings
MT = port_settings.MT
TOUCH = frozenset({"android", "touch"})
# the displays the layouts are checked at: (name, width, height in pixels, dots an inch); a phone, a 21:9 phone, a
# 16:10 tablet, a folding phone's inner screen (narrower than 4:3: the menus' 640 across it)
TOUCH_DISPLAYS = [("phone", 2400, 1080, 420), ("phone 21:9", 2520, 1080, 420), ("tablet", 2560, 1600, 320),
                  ("folding", 2184, 1968, 360)]
# (the screens: their roots, and the names menu_functions.c finds in them)
SCREENS = {
    "multiplayer": (f"{MT}/multiplayer_type_select_screen",
                    ["hub_matchmaking", "hub_join_internet", "hub_join_lan_item", "hub_join_direct", "hub_party",
                     "hub_friends", "hub_create", "hub_coop", "hub_gametypes", "browser_note",
                     "item_note", "invite_note", "party_note", "matchmaking_note", "friends_note", "hub_panel",
                     "multiplayer_status", "multiplayer_status_detail",
                     "multiplayer_options_txt", "online_now_label", "online_games_2", "online_players_2",
                     "best_2_name", "best_2_badge", "best_2_players", "best_2_ping", "prompt_refresh"]),
    "create game": (f"{MT}/hub/create_screen", ["hub_create_inter", "hub_create_lan_item", "hub_create_offline", "hub_panel"]),
    "co-op campaign": (f"{MT}/hub/coop_screen", ["hub_online_coop", "hub_split_coop", "hub_panel"]),
    "server browser": (f"{MT}/join_game/join_game_screen",
                       ["browser_tabs", "tab_all", "tab_favorites", "server_item_1", "server_item_server_name",
                        "server_item_map", "server_item_type", "server_item_players", "server_item_tick",
                        "server_item_locked", "server_item_ping", "server_item_ping_ms", "server_item_favorite",
                        "browser_panel", "panel_title", "panel_where", "panel_kind", "panel_tick", "panel_opence",
                        "panel_status", "browser_join_bar", "button_join",
                        "join_game_button_bar", "prompt_favorite", "prompt_refresh", "prompt_paste", "prompt_sort",
                        "prompt_back", "browser_empty", "browser_counts"]),
    "lobby": (f"{MT}/lobby/lobby_screen",
              ["board_players", "board_row_1_12", "board_name_0_0", "board_panel", "board_gametype", "board_map",
               "board_limits", "board_kind", "board_status", "board_waiting", "board_bots_count",
               "board_bots_count_spinner", "board_bots_skill_spinner", "board_bots_teams_spinner", "board_settings", "board_start", "board_prompts",
               "prompt_team", "prompt_invite", "prompt_add", "prompt_leave", "board_subtitle", "board_host",
               "board_after", "board_after_label"]),
    "host controls": ("main_menu/host_controls/host_controls_screen",
                      ["host_tabs", "tab_players", "tab_maps", "tab_playlist", "tab_bans", "host_item_1", "host_item_label", "host_item_team",
                       "host_item_score", "host_item_ping", "host_item_tag", "host_item_value", "host_action_0",
                       "host_action_3", "host_panel", "panel_title", "panel_where", "panel_status", "header_label",
                       "header_value", "host_empty", "host_counts"]),
    "friends": (f"{MT}/friends/friends_screen",
                ["friends_list", "friends_prompts", "fr_row_0", "fr_row_4", "fr_note_4", "fr_live_4", "friends_panel",
                 "panel_kind", "panel_title", "panel_status", "panel_detail", "panel_code", "panel_help",
                 "friends_count", "section_friends", "prompt_copy", "prompt_join", "prompt_accept", "prompt_add",
                 "prompt_unblock", "prompt_code", "prompt_invite", "prompt_ignore", "prompt_cancel", "prompt_remove",
                 "prompt_block", "prompt_hide", "prompt_show", "prompt_back"]),
    "party": (f"{MT}/party/party_screen",
              ["party_list", "party_prompts", "prompt_make", "prompt_copy", "prompt_paste", "prompt_follow",
               "prompt_end", "prompt_leave", "prompt_back", "party_panel", "party_name_15", "party_empty"]),
}
# the widths the layouts are checked at, in the menus' units at 480 lines: 4:3 and the folding phone's (640), the
# tablet's 16:10, 16:9, the phone's 20:9 and 21:9 (16:9's, centred)
WIDTHS = [640, 768, 852, 1067, 1120]


@lru_cache(maxsize=None)
def definitions():
    """each widget's element, by its name, of the touchscreen's (an element of the other platforms left out)"""
    found = {}

    def read_element(element):
        platform = element.get("platform")
        if platform and not set(platform.split()) & TOUCH:
            return
        if element.tag == "widget":
            found.setdefault(element.get("name"), element)
        for child in element:
            read_element(child)

    for path in sorted(CE.glob("*.xml")):
        read_element(ElementTree.parse(path).getroot())
    return found


def tapped(name):
    """whether a widget is a tap target: one a tap uses (its left_mouse)"""
    element = definitions().get(name)
    return element is not None and any(on.get("event") == "left_mouse" for on in element.findall("on"))


def full_names(root):
    """the widgets' full names under a screen, as layout walks them (the touchscreen's)"""
    names = []
    every = menu_layout.widgets()

    def walk(name):
        widget = every[name]
        if not menu_layout._platform_shown(widget.attributes.get("platform"), TOUCH):
            return
        names.append(name)
        for child, _, _, platform in widget.children:
            if menu_layout._platform_shown(platform, TOUCH):
                walk(child)
        if widget.attributes.get("description"):
            walk(widget.attributes["description"])

    walk(root)
    return names


def targets(root, width):
    """the tap targets of a screen on the touchscreen at a width: (box, full name)"""
    boxes = layout(root, width, TOUCH)
    names = full_names(root)
    assert len(names) == len(boxes)
    return [(box, name) for box, name in zip(boxes, names) if tapped(name)]


def finger():
    """the least size, in the menus' units, that is 44 points on every display of TOUCH_DISPLAYS (the menus' 480
    lines its height, or their 640 units its width on a display narrower than 4:3)"""
    return max(44 * (dpi / 160) / min(height / 480, width / 640) for _, width, height, dpi in TOUCH_DISPLAYS)


def test_tap_targets_are_a_finger_tall_on_every_display():
    """TOUCH_TAP units are 44 points or more on each display"""
    assert port_settings.TOUCH_TAP >= finger(), f"TOUCH_TAP is {port_settings.TOUCH_TAP}, a finger {finger():.1f}"


@pytest.mark.parametrize("screen", SCREENS)
@pytest.mark.parametrize("width", WIDTHS)
def test_touch_targets(screen, width):
    """every tap target 44 points or more each way on every display, on the screen within WIDE_MARGIN of its
    edges, over no other; the rows and panels over the footer's band, its buttons under it"""
    root, _ = SCREENS[screen]
    found = targets(root, width)
    assert found, f"{screen}: no tap targets"
    area = inner(width)
    for box, name in found:
        assert box.y1 - box.y0 >= finger(), f"{screen} at {width}: {name} is {box.y1 - box.y0} tall"
        assert box.x1 - box.x0 >= finger(), f"{screen} at {width}: {name} is {box.x1 - box.x0} wide"
        assert box.inside(area), f"{screen} at {width}: {name} {box} is outside {area}"
        assert box.y1 <= 470, f"{screen} at {width}: {name} {box} is under the screen's foot"
    # (the rows and panels end over the footer's band, the footer's buttons are under it)
    band = port_settings.TOUCH_FOOTER_BAND
    for box in layout(root, width, TOUCH):
        if box.y1 - box.y0 < 480 and box.y0 < band and (box.name.endswith("_panel") or
                                                       any(box == target for target, _ in found)):
            assert box.y1 <= band, f"{screen} at {width}: {box.name} {box} crosses the footer's band at {band}"
    for index, (box, name) in enumerate(found):
        for other, other_name in found[index + 1:]:
            if box.name in other.path or other.name in box.path:
                continue
            # (a footer's alternatives in one place, the game showing one: PASTE LINK for REFRESH, COPY LINK
            # for MAKE PARTY)
            if box[1:5] == other[1:5] and box.path == other.path and box.name.startswith("prompt_"):
                continue
            assert not box.overlaps(other), f"{screen} at {width}: {name} {box} is over {other_name} {other}"


@pytest.mark.parametrize("screen", SCREENS)
def test_touch_layouts_have_what_the_game_fills(screen):
    """the touch layout has every widget menu_functions.c finds in the screen by its name"""
    root, wanted = SCREENS[screen]
    names = {name.rsplit("/", 1)[-1] for name in full_names(root)}
    missing = [name for name in wanted if name not in names]
    assert not missing, f"{screen}: the touchscreen's layout has no {missing}"


def test_touch_rows_within_the_game_s_arrays():
    """the browser's and Host Controls' touch rows: fewer than the computer's, no more than menu_functions.c's
    arrays hold (it counts the rows a list has: browser_rows_of, host_rows_of)"""
    menus = read("port/linux/game/menu_functions.c")
    host = read("port/linux/game/host_controls_menu.inc")
    browser_rows = int(re.search(r"#define BROWSER_ROWS (\d+)", menus).group(1))
    host_rows = int(re.search(r"#define HOST_CONTROLS_ROWS (\d+)", host).group(1))
    assert 0 < port_settings.TOUCH_BROWSER_ROWS < port_settings.BROWSER_ROWS <= browser_rows
    assert 0 < port_settings.TOUCH_HOST_ROWS < port_settings.HOST_ROWS <= host_rows
    # (and the C counts rows, not its arrays' size)
    assert "browser_rows_of(list)" in menus and "host_rows_of(list)" in host


def test_touch_pictures_are_drawn_and_embedded():
    """every bitmap the touch layouts name is in bitmaps.xml, and each frame's PNG is there and in menus.json"""
    import json
    bitmaps = ElementTree.parse(CE / "bitmaps.xml").getroot()
    frames = {element.get("name"): [frame.get("png") for frame in element.findall("frame")] for element in bitmaps}
    manifest = set(json.loads((CE.parent / "menus.json").read_text())["files"])
    for path in sorted(CE.glob("*.touch.xml")):
        assert f"ce/{path.name}" in manifest, path.name
        for element in ElementTree.parse(path).getroot().iter("widget"):
            bitmap = element.get("bitmap")
            if not bitmap or "\\" in bitmap:
                continue
            assert bitmap in frames, f"{path.name}: {element.get('name')} draws {bitmap}, which bitmaps.xml has not"
            for png in frames[bitmap]:
                assert (CE.parent / png).is_file() and png in manifest, png
    for name, pictures in port_settings.touch_pictures():
        assert len(frames[name]) == len(pictures), name


def text_width(words, font):
    """the words' width in the menus' units, as the game draws them (menu_text.c: large_ui Overpass 900 at capitals
    13 units high, small_ui Overpass 750 at 10, each 98% wide), with 7% to spare for its spacing"""
    from PIL import ImageFont
    face, capitals = ("Overpass-900.ttf", 13) if "large" in font else ("Overpass-750.ttf", 10)
    probe = ImageFont.truetype(str(ROOT / "port/assets/fonts" / face), 1000)
    _, top, _, bottom = probe.getbbox("H")
    return probe.getlength(words) * capitals / (bottom - top) * 0.98 * 1.07


def test_touch_words_fit():
    """each touch widget's own words fit it at 4:3 (the narrowest), after its text_x; the game's words (START GAME's,
    SORT's, Host Controls' buttons') fit theirs at their longest (the countdown is the Xbox's, under ten
    seconds)"""
    pytest.importorskip("PIL")
    given = {f"{MT}/join_game/touch/prompt_sort": "SORT: PLAYERS", f"{MT}/join_game/touch/panel_status":
             "Room for your party (16), but places aren't held. It has a password.", f"{MT}/lobby/touch/board_start": "STARTING IN 9",
             **{f"main_menu/host_controls/touch/host_action_{index}": "UNMUTE FOR ALL" for index in range(4)}}
    for path in sorted(CE.glob("*.touch.xml")):
        for element in ElementTree.parse(path).getroot().iter("widget"):
            name = element.get("name")
            words = element.get("text") or given.get(name)
            if not words or element.get("type") not in ("text", "spinner"):
                continue
            if element.get("type") == "spinner":
                # (its longest value, between its arrows)
                words = max(element.get("strings").split("|"), key=lambda value: text_width(value, element.get("font")))
            room = int(element.get("width")) - (2 * int(element.get("text_x", 0)) if element.get("align") != "center"
                                                else 12 if element.get("type") == "text" else 40)
            # (a text the game wraps, to the lines its box has)
            lines = max(1, int(element.get("height")) // 20)
            if lines > 1:
                room *= lines
                words = words + " " * 8 * (lines - 1)
            width = text_width(words, element.get("font", "small"))
            assert width <= room, f"{name}: {words!r} is {width:.0f} wide, {room} room"
