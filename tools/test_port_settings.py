"""The settings pages' rows fit above the footer on every platform
(tools/port_settings.py's PAGES, packed as menu_functions.c's
settings_rows_pack packs them)."""
import re
from xml.etree import ElementTree
from pathlib import Path

import pytest

from tools import port_settings
from tools.port_settings import (PAGES, PAGE_TOP, SECTION, SETTINGS_NOT_IN_VR, SETTINGS_ROW_HEIGHT,
                                 SETTINGS_ROW_STEP, SETTINGS_ROWS_BOTTOM, SETTINGS_SAME_PLACE, SETTINGS_SECTION_GAP,
                                 SETTINGS_SECTION_STEP, SETTINGS_TOP)

# the platforms each build's menus are for (menu_files.c's for_this_platform),
# every one the build may have at once: the desktop's (the VR build's in the
# headset, on a renderer that draws HDR; and out of it), Android's, iOS's
# (desktop's menus but not "computer"'s, for a touchscreen)
BUILDS = {
    "desktop": {"desktop", "computer", "pointer", "hdr", "vr"},
    "desktop out of VR": {"desktop", "computer", "pointer", "hdr"},
    "android": {"android", "touch", "hdr"},
    "ios": {"desktop", "touch", "hdr"},
}


def _key(entry):
    label, setting, _choices, _help, _platform, *named = entry
    return named[0] if named else setting.split(".", 1)[1] if setting else None


def _profile_row(entry):
    """a row hidden with no profile (its condition in the model: profile)"""
    return (entry[1] or "").startswith("profile.") or _key(entry) in ("name", "color", "profiles")


# the layouts the rows are packed in: the classic engine's (settings_rows_pack),
# and the new engine's tall one (menu_engine.c's engine_settings_layout)
LAYOUTS = {"classic": port_settings.SETTINGS_CLASSIC, "tall": port_settings.SETTINGS_TALL}


def _rows_bottom(page, platforms, profile, layout=port_settings.SETTINGS_CLASSIC):
    """where the last row shown ends, the rows packed as settings_rows_pack
    packs them: a step apart, a section's title over its rows (none for a
    section with no row shown), a gap above a section after rows"""
    y = layout.tab_top if "tab" in page else layout.page_top
    bottom, rows_above, section, previous_shown = y, False, False, False
    for entry in page["rows"]:
        if entry[0] == SECTION:
            section, previous_shown = True, False
            continue
        shown = entry[4] is None or bool(set(entry[4].split()) & platforms)
        if _key(entry) == "create_profile":
            shown = shown and not profile
        elif not profile and _profile_row(entry):
            shown = False
        if "vr" in platforms and _key(entry) in SETTINGS_NOT_IN_VR:
            shown = False
        # (one of the rows in the same place at most: Resolution's or Window
        # Size, the desktop's anti-aliasing or Android's)
        if shown and previous_shown and (_key(entry) in SETTINGS_SAME_PLACE or entry[1] == "display.window_size"):
            continue
        previous_shown = shown
        if not shown:
            continue
        if section:
            y += (layout.section_gap if rows_above else 0) + layout.section_step
            section = False
        bottom = y + layout.row_height
        y += layout.row_step
        rows_above = True
    return bottom


@pytest.mark.parametrize("layout", sorted(LAYOUTS))
@pytest.mark.parametrize("folder", sorted(PAGES))
def test_settings_rows_fit(folder, layout):
    """no page's rows reach the footer's band, which they would be drawn
    over (the classic engine's list does not scroll; the new engine's tall
    layout scrolls them, but a page is laid out to need none), for any build,
    with a profile or without"""
    page, geometry = PAGES[folder], LAYOUTS[layout]
    for build, platforms in BUILDS.items():
        for profile in (True, False):
            bottom = _rows_bottom(page, platforms, profile, geometry)
            assert bottom <= geometry.rows_bottom, \
                f"{folder}'s rows end at {bottom} on {build}{'' if profile else ' with no profile'} in the " \
                f"{layout} layout, past {geometry.rows_bottom}: move a row to a page with room"


def test_rows_bottom_counts_a_page_that_overflows():
    """the check sees an overflow: a tab of many rows"""
    page = {"tab": "TEST", "rows": [(SECTION, "A")] + [(f"R{n}", f"x.r{n}", port_settings.ON_OFF, "", None)
                                                        for n in range(15)]}
    assert _rows_bottom(page, BUILDS["desktop"], True) == SETTINGS_TOP + SETTINGS_SECTION_STEP + \
        14 * SETTINGS_ROW_STEP + SETTINGS_ROW_HEIGHT
    assert _rows_bottom(page, BUILDS["desktop"], True) > SETTINGS_ROWS_BOTTOM


FUNCTIONS = (Path(__file__).resolve().parent.parent / "port/linux/game/menu_functions.c").read_text()


def _functions_define(name):
    return int(re.search(rf"#define {name} (\d+)", FUNCTIONS).group(1))


def _help_lines(words, width):
    """how many lines words take wrapped to a width in small_ui (settings_text_wrap: at spaces)"""
    lines, line = 1, ""
    for word in words.split(" "):
        longer = f"{line} {word}" if line else word
        if line and port_settings.small_ui_width(longer) > width:
            lines, line = lines + 1, word
        else:
            line = longer
    return lines


def _help_room(page, layout=port_settings.SETTINGS_CLASSIC):
    """the lines a page's panel has for a row's words under its name and value (settings_panel_show: the panel's
    bottom its layout's, the model's geometry's)"""
    top = layout.tab_top if "tab" in page else layout.page_top
    name_top = top + 12
    room = layout.panel_bottom - _functions_define("SETTINGS_PANEL_TEXT_MARGIN") - name_top - \
        _functions_define("SETTINGS_PANEL_TEXT_TOP")
    return room // port_settings.SMALL_UI_LINE


# the panel's words' width at its narrowest, a wide screen's (16:9 and wider: SETTINGS_WIDE_PANEL)
HELP_WIDTH = port_settings.SETTINGS_PANEL_WORDS + port_settings.SETTINGS_WIDE_PANEL[1]


@pytest.mark.parametrize("layout", sorted(LAYOUTS))
@pytest.mark.parametrize("folder", sorted(PAGES))
def test_settings_help_fits(folder, layout):
    """each row's words fit the panel at its narrowest without their last lines cut (settings_panel_show cuts
    them, after leaving out the note, which may go), in each layout (the tall one's panel taller)"""
    page, geometry = PAGES[folder], LAYOUTS[layout]
    assert port_settings.SMALL_UI_LINE == 20 and _functions_define("SETTINGS_PANEL_BOTTOM") == \
        port_settings.SETTINGS_PANEL_BOTTOM == port_settings.SETTINGS_CLASSIC.panel_bottom
    for entry in page["rows"]:
        if entry[0] == SECTION or not entry[3]:
            continue
        words = entry[3].split("|", 1)[0]
        lines = _help_lines(words, HELP_WIDTH)
        assert lines <= _help_room(page, geometry), f"{folder}'s {entry[0]}: its words take {lines} lines of " \
            f"the {layout} panel's {_help_room(page, geometry)} on a wide screen: say it in fewer"


def test_help_check_sees_words_too_long():
    """the check sees a help too long for the panel"""
    assert _help_lines(" ".join(["WORDS"] * 200), HELP_WIDTH) > _help_room(PAGES["video_settings"])


def test_segments_fit_at_16_9():
    """every segmented row's segments fit in its row on a 16:9 screen (menu_functions.c's settings_segments_fit:
    the room kept at the row's end), so that no wide screen has a spinner for one"""
    extra = port_settings.WIDE_EXTRA
    room = port_settings.SETTINGS_ROW_WIDTH + port_settings.wide_scale(port_settings.SETTINGS_WIDE_ROWS, extra) - \
        _functions_define("SETTINGS_SEGMENTS_MARGIN")
    segmented = 0
    for folder, page in PAGES.items():
        for entry in page["rows"]:
            if entry[0] == SECTION or not port_settings.segmented(entry[1], entry[2]):
                continue
            segmented += 1
            count, width = len(entry[2]), port_settings.segments_width(entry[2])
            right = port_settings.SETTINGS_SPINNER_X + port_settings.wide_scale(port_settings.SETTINGS_WIDE_LABELS,
                                                                                extra) + \
                count * width + (count - 1) * port_settings.SEGMENT_GAP
            assert right <= room, f"{folder}'s {entry[0]}: its segments end at {right}, past {room}"
            # (each segment found by its name, which its instance keeps SETTINGS_WIDGET_NAME_SIZE long)
            assert len(f"{_key(entry)}_seg{count - 1}") < _functions_define("SETTINGS_WIDGET_NAME_SIZE"), entry[0]
    assert segmented and _functions_define("SETTINGS_SEGMENTS_MOST") == port_settings.SEGMENTED_MOST


def _main_menu_items():
    """the main menu's items (the headset's VR TUTORIAL left out), in their
    order, with their places"""
    menu = (Path(__file__).resolve().parent.parent / "port/assets/menus/ce/main_menu.xml").read_text()
    select = menu.split('<widget name="main_menu/main_menu_select_list"', 1)[1].split("</widget>", 1)[0]
    return [(name, int(x), int(y)) for name, x, y, platform in
            re.findall(r'<child widget="main_menu/main_menu_item_(\w+)" x="(\d+)" y="(\d+)"( platform="\w+")?/>',
                       select) if not platform]


def test_main_menu_items():
    """the main menu is CAMPAIGN, MULTIPLAYER, 2V2, SETTINGS and QUIT (the
    profiles and the mods are Settings' PROFILES and MODS tabs'), a row apart
    with no gap"""
    items = _main_menu_items()
    assert [name for name, _, _ in items] == ["load_camp", "multiplayer", "doubles", "settings", "quit_game"]
    assert {after[2] - before[2] for before, after in zip(items, items[1:])} == {36}
    assert items[2][1:] == port_settings.DOUBLES_ITEM_PLACE
    assert items[3][1:] == port_settings.SETTINGS_ITEM_PLACE
    assert items[4][1:] == port_settings.QUIT_ITEM_PLACE


def test_profiles_tab_opens_profiles():
    """the PROFILES tab opens the profile manager (the main menu's Profile's
    was), beside its own rows"""
    page = PAGES["profile_settings"]
    assert page["tab"] == "PROFILES"
    links = {_key(entry): entry[2] for entry in page["rows"] if entry[0] != SECTION and not entry[1]}
    assert links["profiles"] == port_settings.PROFILE_MANAGER
    assert {"name", "color", "create_profile", "about"} <= set(links)
    assert "mods" not in links


def test_mods_tab():
    """MODS is a tab of its own (issue 598), the last, after PROFILES: it opens
    the Mods screen, and says where mods go, in a game too (MANAGE MODS is the
    main menu's)"""
    assert port_settings.SETTINGS_TAB_PAGES[-2:] == ["profile_settings", "mods_settings"]
    page = PAGES["mods_settings"]
    assert page["tab"] == "MODS"
    links = {_key(entry): entry[2] for entry in page["rows"] if entry[0] != SECTION and not entry[1]}
    assert links["mods"] == f"{port_settings.MODS}/mods_screen"
    assert "mods_folder" in links
    assert port_settings.SETTINGS_ROW_WHEN["mods"] == "!in_game" and "mods_folder" not in port_settings.SETTINGS_ROW_WHEN


def test_row_conditions():
    """every row's condition in the model (SETTINGS_ROW_WHEN, SETTINGS_ROW_ENABLED; a profile's setting's) names terms
    that menu_functions.c's settings_condition_term knows, and other rows' keys that are on its page with their
    values; each condition is written into the model's rows (beside their platforms)"""
    functions = (Path(__file__).resolve().parent.parent / "port/linux/game/menu_functions.c").read_text()
    terms = set(re.findall(r'!strcmp\(name, "(\w+)"\)', function_body(functions, "settings_condition_term")))
    platforms = set(port_settings.MENU_PLATFORMS)
    model = (Path(__file__).resolve().parent.parent / "port/assets/menus/model/settings.xml").read_text()
    for folder, page in PAGES.items():
        rows = {_key(entry) or re.sub(r"[^a-z0-9]+", "_", entry[0].lower()).strip("_"): entry
                for entry in page["rows"] if entry[0] != SECTION}
        for key, entry in rows.items():
            for condition in (port_settings.SETTINGS_ROW_WHEN.get(key), port_settings.SETTINGS_ROW_ENABLED.get(key)):
                for term in (condition or "").replace("+", " ").split():
                    term = term.lstrip("!")
                    if "=" in term:
                        other, value = term.split("=", 1)
                        assert other in rows, (folder, key, term)
                        assert value in [v for _, v in rows[other][2]], (folder, key, term)
                    else:
                        assert term in terms | platforms, (folder, key, term)
            when = port_settings._settings_row_when(key, entry[1], entry[4])
            if when:
                assert f'key="{key}"' in model and f'when="{when}"' in model, (folder, key, when)
    for key in port_settings.SETTINGS_ROW_WHEN:
        assert any(key in {_key(entry) for entry in page["rows"] if entry[0] != SECTION} for page in PAGES.values()), key


def function_body(source, name):
    """a C function's body, by its name"""
    start = source.index(f"static boolean {name}(")
    return source[start:source.index("\n}\n", start)]


def _engine_tabs():
    """the menus' tabs as menu_functions.c finds them (settings_tab_screen): the model's screens laid out as tabs,
    in order"""
    import xml.etree.ElementTree as ET
    model = ET.parse(Path(__file__).resolve().parent.parent / "port/assets/menus/model/settings.xml").getroot()
    return [screen.get("name") for screen in model.findall("screen") if screen.get("layout") == "tab"]


def test_engine_tabs_are_the_generators():
    """the tabs Q and E, LB and RB step through (and a click opens) are the
    generator's, in its order; the panel's words for each are its screen's"""
    assert _engine_tabs() == [f"{port_settings.PE}/{folder}/{PAGES[folder]['screen']}"
                              for folder in port_settings.SETTINGS_TAB_PAGES]
    functions = (Path(__file__).resolve().parent.parent / "port/linux/game/menu_functions.c").read_text()
    assert "settings_tabs[]" not in functions


def _tab_problems(places, names):
    """the tabs that are not in the row (from the first's place to RB's
    prompt), not apart, or too narrow for their names and room either side"""
    problems = []
    for index, ((x, width), name) in enumerate(zip(places, names)):
        if width < port_settings.tab_name_width(name) + 2 * 6:
            problems.append(f"{name}: {width} wide, its name {port_settings.tab_name_width(name):.1f}")
        if index and x < places[index - 1][0] + places[index - 1][1] + port_settings.SETTINGS_TAB_GAP:
            problems.append(f"{name} at {x} reaches the tab before it")
    if places[0][0] < port_settings.SETTINGS_TABS_X or sum(places[-1]) > port_settings.SETTINGS_TABS_RIGHT:
        problems.append(f"the tabs {places[0][0]} to {sum(places[-1])} are out of their row")
    return problems


def test_settings_tabs_fit_their_names():
    """at 4:3, where the row is narrowest, every tab holds its name with room
    either side, apart from the next, between LB's and RB's prompts"""
    names = [PAGES[folder]["tab"] for folder in port_settings.SETTINGS_TAB_PAGES]
    assert _tab_problems(port_settings.settings_tab_places(), names) == []


def test_tab_check_sees_a_tab_too_narrow():
    """the check sees seven tabs of the old six's width, and tabs laid over each other"""
    names = [PAGES[folder]["tab"] for folder in port_settings.SETTINGS_TAB_PAGES]
    step = (port_settings.SETTINGS_TABS_RIGHT - port_settings.SETTINGS_TABS_X) // len(names)
    even = [(port_settings.SETTINGS_TABS_X + index * step, step - 2) for index in range(len(names))]
    assert any(name in problem for problem in _tab_problems(even, names) for name in ("INTERFACE", "CONTROLS"))
    places = port_settings.settings_tab_places()
    assert _tab_problems([places[0], (places[0][0] + 10, places[1][1])] + places[2:], names)


def test_search_tabs_fit_their_names():
    """the menu engine's tabs, after Settings' search's field (SETTINGS_SEARCH), hold their names at 4:3 too, the
    field's magnifying glass before them; from 16:10 the field is wide enough for its word"""
    names = [PAGES[folder]["tab"] for folder in port_settings.SETTINGS_TAB_PAGES]
    places = port_settings.settings_tab_places(port_settings.SETTINGS_SEARCH_TABS_X)
    assert _tab_problems(places, names) == []
    assert places[0][0] >= port_settings.SETTINGS_TABS_X + port_settings.SEARCH_FIELD_WIDTH
    assert port_settings.SEARCH_WORD_EXTRA <= port_settings.wide_extra_of(port_settings.WIDE_SCREENS["16:10"])


def test_search_words_are_rows():
    """Settings' search's other words (SETTINGS_SEARCH_WORDS) find real rows, each named once; every page has the way
    to it (its crumb), no longer than three names; the model's rows have them"""
    found = port_settings.settings_search_synonyms()
    assert set(found) <= set(port_settings.settings_search_rows())
    assert sum(map(len, found.values())) == sum(map(len, port_settings.SETTINGS_SEARCH_WORDS.values()))
    crumbs = port_settings.settings_crumbs()
    assert set(crumbs) == set(PAGES) and all(len(crumb.split(" › ")) <= 3 for crumb in crumbs.values())
    with pytest.raises(ValueError):
        saved = dict(port_settings.SETTINGS_SEARCH_WORDS)
        try:
            port_settings.SETTINGS_SEARCH_WORDS["nothing"] = ["no_such_row"]
            port_settings.settings_search_synonyms()
        finally:
            port_settings.SETTINGS_SEARCH_WORDS.clear()
            port_settings.SETTINGS_SEARCH_WORDS.update(saved)
    model = port_settings.model_files()[port_settings.MODEL_FILE]
    assert any('crumb="VIDEO › FRAME RATE &amp; SYNC"' in line and 'key="vsync"' in line for line in model)
    assert any('search="fov"' in line and 'key="fov"' in line for line in model)


def test_map_storage_row():
    """MAP STORAGE (issue 603) sets game.map_cache_megabytes: Instant Load
    Times its default (0, every stock map kept), Disk Space Saver the Xbox's
    730 MB"""
    rows = [entry for page in PAGES.values() for entry in page["rows"]
            if entry[0] != SECTION and entry[1] == "game.map_cache_megabytes"]
    assert [row[0] for row in rows] == ["MAP STORAGE"]
    assert rows[0][2] == [("INSTANT LOADING", "0"), ("DISK SPACE SAVER", "730")]
    config = (Path(__file__).resolve().parent.parent / "port/linux/src/port_config.c").read_text()
    assert re.search(r'\{ "game\.map_cache_megabytes", _config_integer, "0",', config)


def test_settings_spinners_are_model_rows():
    """every spinner of a setting in the menus (its setting= and values=) is a choice row of its screen's in the
    model, of the same setting and values, in the order of the screen's widgets: menu_functions.c's settings_each
    and settings_setting_of take Settings' spinners' settings from the model's rows (but the lobby's, which the model
    has no screen of)"""
    import xml.etree.ElementTree as ET
    menus = Path(__file__).resolve().parent.parent / "port/assets/menus"
    widgets = {}
    for path in sorted((menus / "ce").glob("*.xml")):
        for widget in ET.parse(path).getroot().iter("widget"):
            widgets.setdefault(widget.get("name"), []).append(widget)
    model = ET.parse(menus / "model/settings.xml").getroot()
    rows = {}
    for screen in model.findall("screen"):
        folder = screen.get("name").rsplit("/", 1)[0]
        order = []

        def walk(name, seen):
            if name in seen:
                return
            seen.add(name)
            for widget in widgets.get(name, [])[:1]:
                if widget.get("setting"):
                    order.append(name)
                for child in widget.findall("child"):
                    walk(child.get("widget"), seen)

        walk(screen.get("name"), set())
        choices = [row for row in screen.findall("row") if row.get("kind") == "choice" and row.get("setting")]
        for row in choices:
            rows[f"{folder}/{row.get('key')}_spinner"] = row
        named = [f"{folder}/{row.get('key')}_spinner" for row in choices]
        assert [name for name in named if name in order] == order, screen.get("name")
    for name, variants in widgets.items():
        for widget in variants:
            if widget.get("setting") is None or "/lobby/" in name:
                continue
            row = rows.get(name)
            assert row is not None, name
            assert row.get("setting") == widget.get("setting"), name
            assert [choice.get("value") for choice in row.findall("choice")] == widget.get("values").split("|"), name


def _tall_widgets():
    """the tall layout's widgets (tools/port_settings.py's tall_widgets), by name"""
    import xml.etree.ElementTree as ET
    menus = ET.fromstring("<menus>" + "\n".join(port_settings.tall_widgets()) + "</menus>")
    return {widget.get("name"): widget for widget in menus.iter("widget")}


def _tall_scale(widget, extra):
    """a widget's text scale at an extra width (menu_tags.c's pc_menu_text_scale)"""
    least, most = (float(value) for value in (widget.get("text_scale") or "1 1").split())
    return least + (most - least) * extra / port_settings.WIDE_EXTRA


def _tall_width(widget, extra):
    """a widget's width at an extra width (its wide's growth; past 16:9 none here)"""
    dw = int((widget.get("wide") or "0 0").split()[1])
    return int(widget.get("width", 640)) + port_settings.wide_scale(dw, extra)


@pytest.mark.parametrize("aspect", sorted(set(port_settings.WIDE_SCREENS) - {"4:3"}))
def test_tall_rows_words_fit(aspect):
    """in the tall layout each row's label, and each spinner's longest value, fits its box at its text's scale
    on a wide screen (labels laid out from their text_x; a value in its spinner's box between its arrows). (At
    4:3 its words are their font's size in the classic's widths)"""
    extra = port_settings.wide_extra_of(port_settings.WIDE_SCREENS[aspect])
    widgets, labels, too_wide = _tall_widgets(), {}, []
    for folder, page in PAGES.items():
        names = [entry[1] if entry[0] == SECTION else entry[0] for entry in page["rows"]]
        labels[f"{port_settings.PE}/{folder}/labels"] = names
    for name, widget in widgets.items():
        scale = _tall_scale(widget, extra)
        if name.endswith("_label") and widget.get("string_list") in labels:
            text = labels[widget.get("string_list")][int(widget.get("string_index") or 0)]
            room = _tall_width(widget, extra) - int(widget.get("text_x", 0))
        elif widget.get("type") == "spinner" and widget.get("strings"):
            text = max(widget.get("strings").split("|"), key=port_settings.small_ui_width)
            room = _tall_width(widget, extra)
        else:
            continue
        if text.endswith(" ›"):
            text = text[:-2]
        if port_settings.small_ui_width(text) * scale > room:
            too_wide.append(f"{name}: {text!r} {port_settings.small_ui_width(text) * scale:.0f} > {room}")
    assert not too_wide, f"at {aspect}: " + "; ".join(too_wide[:8])


def test_tall_segments_fit_at_16_9():
    """the tall layout's segments, each as wide as its words at their scale, fit in their row on a 16:9 screen
    (menu_functions.c's settings_segments_fit)"""
    extra = port_settings.WIDE_EXTRA
    widgets = _tall_widgets()
    room = port_settings.SETTINGS_ROW_WIDTH + port_settings.wide_scale(port_settings.SETTINGS_WIDE_ROWS, extra) - \
        _functions_define("SETTINGS_SEGMENTS_MARGIN")
    segmented = 0
    for name, widget in widgets.items():
        children = [child for child in widget.findall("child") if re.search(r"_seg\d+$", child.get("widget"))]
        if not children:
            continue
        segmented += 1
        last = children[-1]
        segment = widgets[last.get("widget")]
        right = int(last.get("x")) + port_settings.wide_scale(int(segment.get("wide").split()[0]), extra) + \
            int(segment.get("width"))
        assert right <= room, f"{name}: its segments end at {right}, past {room}"
        assert all(port_settings.small_ui_width(widgets[child.get("widget")].get("text")) * _tall_scale(
            widgets[child.get("widget")], extra) <= int(widgets[child.get("widget")].get("width")) for child in
            children), name
    assert segmented


def test_tall_layout_is_the_new_engines():
    """the tall layout's widgets are ce/'s by their names (tools/menu_look.py writes their looks in place of
    ce/'s), its pictures are drawn, and the model names its geometry for every Settings tab and page"""
    widgets = _tall_widgets()
    ce = "\n".join(path.read_text() for path in (Path(__file__).resolve().parent.parent /
                                                 "port/assets/menus/ce").glob("*.xml"))
    assert all(f'<widget name="{name}"' in ce for name in widgets)
    assert "past=" not in ce and "text_scale=" not in ce
    pictures = {name for name, _ in port_settings.settings_pictures() + port_settings.parts_pictures()}
    tall = port_settings.SETTINGS_TALL
    assert {tall.row_picture, tall.section_picture, tall.segment_picture, port_settings.TALL_BACKGROUND,
            tall.panel_picture.format(kind="tab"), tall.panel_picture.format(kind="page")} <= pictures
    model = "\n".join(port_settings.model_files()[port_settings.MODEL_FILE])
    assert model.count(f'geometry="{tall.name}"') == len(PAGES) + 1  # (and the search's screen)
    assert f'<geometry name="{tall.name}"' in model


def test_the_tall_footer_is_raised_on_a_touchscreen():
    """On a touchscreen the tall layout's footer rises TALL_FOOT_LIFT (menu_tags.c's, the same number): every tall
    screen's footer bar is flagged foot, the panels that reach down to it above_foot, each of those panels' pictures
    and the background have a _touch picture (the footer's line raised), and nothing else is flagged (the rows the
    engine lays out past the footer's line among them)."""
    tags = Path("port/linux/game/menu_tags.c").read_text()
    assert int(re.search(r"#define TALL_FOOT_LIFT (\d+)", tags).group(1)) == port_settings.TALL_FOOT_LIFT
    root = ElementTree.fromstring("<menus>" + "\n".join(port_settings.tall_widgets()) + "</menus>")
    flagged = {}
    for widget in root.iter("widget"):
        for flag in ("foot", "above_foot"):
            if flag in (widget.get("flags") or "").split():
                flagged.setdefault(flag, set()).add(widget.get("name"))
    footers = {name.rsplit("/", 1)[1] for name in flagged["foot"]}
    assert footers == {"button_bar", "mission_prompts", "difficulty_prompts", "doubles_prompts",
                       "join_game_button_bar"}
    assert all(root.find(f".//widget[@name='{name}']").get("type") == "column_list" for name in flagged["foot"])
    pictures = {name for name, _ in port_settings.settings_pictures() + port_settings.parts_pictures()}
    panels = {root.find(f".//widget[@name='{name}']").get("bitmap") for name in flagged["above_foot"]}
    assert panels and all(port_settings.touch_picture(picture) in pictures for picture in panels)
    assert port_settings.touch_picture(port_settings.TALL_BACKGROUND) in pictures
    # (the computer's footer as it was: the classic widgets have no such flag)
    ce = "\n".join(path.read_text() for path in Path("port/assets/menus/ce").glob("*.xml"))
    assert not re.search(r'flags="[^"]*\b(above_)?foot\b', ce)
