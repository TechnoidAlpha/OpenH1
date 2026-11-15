"""The server browser and Multiplayer in Settings' look (tools/port_settings.py's _browser and _hub, the menus'
shared parts; port/linux/game/menu_functions.c): the browser's tabs (every game, OpenH1's, Legacy ones, co-op, the
favorites) filter its list, a list with no games says why (the local network's, what to do), a text too long for
its column is cut short with "...", and the footers' key prompts are buttons a key of the keyboard presses
(menu_keys.c checks each key); the menu files are as the generator writes them, the parts' pictures as
tools/ce_menus.py draws them, and Multiplayer keeps every place it opened."""

import json
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, ROOT, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["tabs", "groups", "sort", "empty", "ellipsis", "ping", "filters", "tick", "game_type", "search", "doubles",
         "view"]

NEGATIVE_CONTROLS = {
    "doubles-by-tab": (("\tif (browser_doubles_playlist != NONE)\n\t\treturn TRUE;\n\tswitch (browser_tab)",
                        "\tswitch (browser_tab)"), "doubles"),
    "doubles-filtered": (("if (browser_filtering && browser_doubles_playlist == NONE &&",
                          "if (browser_filtering &&"), "doubles"),
    "doubles-offline": (("\tif (browser_doubles_playlist != NONE)\n\t\treturn FALSE;\n\treturn", "\treturn"),
                        "doubles"),
    "favorites-on-every-tab": (("\tcase _browser_tab_favorites:\n\t\treturn favorite;\n", ""), "tabs"),
    "legacy-as-openh1": (("\tcase _browser_tab_legacy:\n\t\treturn !entry->openh1;",
                          "\tcase _browser_tab_legacy:\n\t\treturn entry->openh1;"), "tabs"),
    "dedicated-tab-lists-all": (("\tcase _browser_tab_dedicated:\n\t\treturn entry->dedicated;\n", ""), "tabs"),
    "openh1-not-first": (("\t\tif (a->openh1 != b->openh1)", "\t\tif (a->openh1 != b->openh1 && FALSE)"), "sort"),
    "dedicated-not-first": (("\t\tif (a->dedicated != b->dedicated)",
                             "\t\tif (a->dedicated != b->dedicated && FALSE)"), "sort"),
    # (#920: the grouping before the players, as it was)
    "grouped-before-players": (("if (!difference && browser_sort_column", "if (browser_sort_column"), "sort"),
    # (the server browser's groups: OpenH1's dedicated servers, the project's first; the games players host; the
    # Legacy ones)
    "groups-none": (("\tif (!browser_grouped)\n\t\treturn _browser_group_dedicated;", "\tif (TRUE)\n\t\treturn "
                     "_browser_group_dedicated;"), "groups"),
    "hosted-as-dedicated": (("return entry->dedicated ? _browser_group_dedicated : _browser_group_hosted;",
                             "return _browser_group_dedicated;"), "groups"),
    "project-not-first": (("(project != NONE && entries[index].project != project)", "FALSE"), "groups"),
    "favorites-before-project": (("\t\tfor (project = browser_grouped ? TRUE : NONE; project >= (browser_grouped ? "
                                  "FALSE : NONE); project--)\n\t\t{\n\t\t\twritten = browser_order_part(favorite_of, "
                                  "entries, count, group, project, TRUE, order, written);\n\t\t\twritten = "
                                  "browser_order_part(favorite_of, entries, count, group, project, FALSE, order, written);",
                                  "\t\tfor (project = browser_grouped ? TRUE : NONE; project >= (browser_grouped ? "
                                  "FALSE : NONE); project--)\n\t\t\twritten = browser_order_part(favorite_of, "
                                  "entries, count, group, project, TRUE, order, written);\n\t\tfor (project = "
                                  "browser_grouped ? TRUE : NONE; project >= (browser_grouped ? FALSE : NONE); "
                                  "project--)\n\t\t{\n\t\t\twritten = browser_order_part(favorite_of, entries, "
                                  "count, group, project, FALSE, order, written);"), "groups"),
    "legacy-not-headed": (("return group != _browser_group_legacy || browser_tab != _browser_tab_legacy;",
                           "return group != _browser_group_legacy;"), "groups"),
    "legacy-tab-headed": (("return group != _browser_group_legacy || browser_tab != _browser_tab_legacy;",
                           "return TRUE;"), "groups"),
    "empty-group-headed": (("if (written > start && browser_grouped && browser_group_headed(group))",
                            "if (browser_grouped && browser_group_headed(group))"), "groups"),
    "heading-words": (('return L"Legacy (Slow) Servers";', 'return L"Legacy Servers";'), "groups"),
    "dedicated-heading-words": (('return L"GOAT H1 Dedicated Servers";', 'return L"Dedicated Servers";'), "groups"),
    "favorites-before-groups": (("browser_group_of(&entries[index]) != group ||", "FALSE ||"), "groups"),
    "offline-on-every-tab": (("\tif (!browser_tab_lists_offline())\n\t\tbrowser_offline_count = 0;\n", ""), "tabs"),
    "lan-says-nothing-to-do": (("L\"No games on your network yet.\\r\\nHost one from Create Game.\"",
                                "L\"No games.\""), "empty"),
    "never-cut": (("if (font == NONE || width <= 0 || settings_text_width(font, fitted) <= width)",
                   "if (TRUE)"), "ellipsis"),
    "full-never-hidden": (("entry->maximum_players > 0 && entry->players >= entry->maximum_players)",
                           "entry->maximum_players > 0 && entry->players > entry->maximum_players)"), "filters"),
    "empty-never-hidden": (("if (!filters->show_empty && entry->players <= 0)", "if (FALSE)"), "filters"),
    "password-never-hidden": (("if (!filters->show_password && entry->locked)", "if (FALSE)"), "filters"),
    "ping-at-limit-listed": (("entry->ping >= filters->maximum_ping)", "entry->ping > filters->maximum_ping)"),
                             "filters"),
    "ping-unknown-hidden": (("entry->ping >= filters->maximum_ping)",
                             "(entry->ping < 0 || entry->ping >= filters->maximum_ping))"), "filters"),
    # (the TICK RATE and GAME TYPE filters: browser_tick_shows, browser_type_shows)
    "tick-filter-ignored": (("if (!browser_tick_shows(filters->tick, entry) || ", "if ("), "tick"),
    "tick-legacy-as-30": (("\t\treturn entry->openh1 && entry->rate == 30;", "\t\treturn entry->rate == 30;"), "tick"),
    "tick-60-any-rate": (("\t\treturn entry->openh1 && entry->rate == 60;", "\t\treturn entry->openh1;"), "tick"),
    "tick-legacy-lists-openh1": (("\tcase _browser_tick_legacy:\n\t\treturn !entry->openh1;",
                                  "\tcase _browser_tick_legacy:\n\t\treturn TRUE;"), "tick"),
    "type-filter-ignored": (("|| !browser_type_shows(filters->game_type, entry))", ")"), "game_type"),
    "slayer-lists-teams": (("\t\treturn slayer && !entry->has_teams;", "\t\treturn slayer;"), "game_type"),
    "team-slayer-lists-ffa": (("\t\treturn slayer && entry->has_teams;", "\t\treturn slayer;"), "game_type"),
    "slayer-lists-modes": (("boolean slayer = entry->engine_type == 2 && mode == _game_mode_none;",
                            "boolean slayer = entry->engine_type == 2;"), "game_type"),
    "hardcore-not-its-mode": (("short mode = (short)(entry->kind & ~GAME_MODES_KIND_HARDCORE);",
                               "short mode = (short)entry->kind;"), "game_type"),
    "pro-2v2-as-team-slayer": (("\t\treturn entry->pro_2v2;", "\t\treturn entry->has_teams;"), "game_type"),
    "coop-as-any": (("\tcase _browser_type_coop:\n\t\treturn entry->coop;",
                     "\tcase _browser_type_coop:\n\t\treturn TRUE;"), "game_type"),
    "types-read-shifted": (("\t\tif (!strcmp(word, words[index]))\n\t\t\treturn index;",
                            "\t\tif (!strcmp(word, words[index]))\n\t\t\treturn (short)(index + 1);"), "filters"),
    "unknown-word-not-any": (("\t\t\treturn index;\n\t}\n\treturn 0;", "\t\t\treturn index;\n\t}\n\treturn 1;"),
                             "filters"),
    # (the tab and the order kept: browser_view_read, browser_view_write)
    "tab-not-written": (('\t\tconfig_write("network.browser_tab", browser_tab_words[browser_tab]);', "\t\t;"), "view"),
    "sort-not-written": (('\t\tconfig_write("network.browser_sort", browser_sort_words[browser_sort_column]);',
                          "\t\t;"), "view"),
    "way-not-written": (('\t\tconfig_write("network.browser_sort_descending", browser_sort_descending ? "true" : '
                         '"false");', "\t\t;"), "view"),
    "tab-not-read": (('\tbrowser_tab = browser_word_index(config_string("network.browser_tab")',
                      '\tbrowser_tab = 0 * browser_word_index(config_string("network.browser_tab")'), "view"),
    "way-not-read": (('\tbrowser_sort_descending = config_boolean("network.browser_sort_descending") != 0;',
                      "\tbrowser_sort_descending = TRUE;"), "view"),
    # (the new engine's browser on CO-OP kept: its CO-OP tab is co-op's screen's door, the browser on ALL)
    "coop-tab-kept": (("if (browser_tab == _browser_tab_coop && menu_engine_looks())", "if (FALSE)"), "view"),
    "written-unchanged": (('\tif (browser_tab_visit == NONE && strcmp(config_string("network.browser_tab"), '
                           'browser_tab_words[browser_tab]))', "\tif (browser_tab_visit == NONE)"), "view"),
    # (the column searches: browser_search_shows)
    "search-case-kept": (("*text >= 'a' && *text <= 'z' ? (wchar_t)(*text - 32) : *text;", "*text;"), "search"),
    "search-spaces-kept": (("if ((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') "
                            "|| character > 0x7F)", "if (TRUE)"), "search"),
    "search-whole-words": (("for (start = 0; start + words_length <= length; start++)",
                            "for (start = 0; start == 0 && words_length == length; start++)"), "search"),
    "search-map-shown-only": (("if (!browser_ascii_has(map, map_words) && !browser_text_has(map_shown, map_words))",
                               "if (!browser_text_has(map_shown, map_words))"), "search"),
    "search-map-file-only": (("if (!browser_ascii_has(map, map_words) && !browser_text_has(map_shown, map_words))",
                              "if (!browser_ascii_has(map, map_words))"), "search"),
    "search-either-column": (("if (!browser_ascii_has(name, searches[_browser_search_server]))\n\t\treturn FALSE;\n\t"
                              "if (!browser_ascii_has(map, map_words)",
                              "if (browser_ascii_has(name, searches[_browser_search_server]) && "
                              "searches[_browser_search_server][0])\n\t\treturn TRUE;\n\tif (!browser_ascii_has(map, "
                              "map_words)"), "search"),
    # (the touchscreen's one search: a word of the server's name or of its map, and the columns' searches too)
    "search-any-ignored": (("return browser_ascii_has(name, any_words) || browser_ascii_has(map, any_words) ||\n"
                            "\t\tbrowser_text_has(map_shown, any_words);", "return TRUE;"), "search"),
    "search-any-name-only": (("return browser_ascii_has(name, any_words) || browser_ascii_has(map, any_words) ||\n"
                              "\t\tbrowser_text_has(map_shown, any_words);",
                              "return browser_ascii_has(name, any_words);"), "search"),
    "search-any-map-only": (("return browser_ascii_has(name, any_words) || browser_ascii_has(map, any_words) ||\n"
                             "\t\tbrowser_text_has(map_shown, any_words);",
                             "return browser_ascii_has(map, any_words) || browser_text_has(map_shown, any_words);"),
                            "search"),
    "search-any-shown-map-unread": (("return browser_ascii_has(name, any_words) || browser_ascii_has(map, any_words) ||\n"
                                     "\t\tbrowser_text_has(map_shown, any_words);",
                                     "return browser_ascii_has(name, any_words) || browser_ascii_has(map, any_words);"),
                                    "search"),
    "search-any-overrides-server": (("if (!browser_ascii_has(name, searches[_browser_search_server]))",
                                     "if (!browser_ascii_has(name, searches[_browser_search_server]) && !any_words[0])"),
                                    "search"),
    "search-any-shown-map-not-found": (("return browser_searches[_browser_search_map][0] || "
                                        "browser_searches[_browser_search_any][0];",
                                        "return browser_searches[_browser_search_map][0];"), "search"),
    "search-server-searches-map": (("if (!browser_ascii_has(name, searches[_browser_search_server]))",
                                    "if (!browser_ascii_has(name, searches[_browser_search_server]) && "
                                    "!browser_ascii_has(map, searches[_browser_search_server]))"), "search"),
    "search-ignores-filters": (("\t\telse if (browser_filtering && !browser_search_shows(",
                                "\t\tif (browser_filtering && !browser_search_shows("), "search"),
    "search-not-counted": (("\t\t\tbrowser_searched_count++;\n\t\t}", "\t\t}"), "search"),
    "searched-says-filters": (("\telse if (searched > 0)", "\telse if (FALSE)"), "empty"),
    "hidden-not-counted": (("&entries[index]))\n\t\t\tbrowser_filtered_count++;", "&entries[index]))\n\t\t\t;"),
                           "filters"),
    "filtered-says-nothing": (("\telse if (filtered > 0)", "\telse if (FALSE)"), "empty"),
    "ping-unknown-as-measuring": (("\telse if (ping_state == _browser_ping_measuring)",
                                   "\telse if (ping_state != _browser_ping_measured)"), "ping"),
    "ping-bars-one-off": (("ping < PING_GOOD ? 3", "ping <= PING_GOOD ? 3"), "ping"),
    "cut-one-too-many": (("\t\tif (settings_text_width(font, fitted) <= width)\n\t\t\tlow = middle;",
                          "\t\tif (settings_text_width(font, fitted) < width)\n\t\t\tlow = middle;"), "ellipsis"),
}

MT = "main_menu/multiplayer_type_select"
CE = ROOT / "port/assets/menus/ce"


def generated(fault=None):
    menus = read("port/linux/game/menu_functions.c")
    config = "\n\n".join([
        enum_with(menus, "_multiplayer_mode_server_browser"), enum_with(menus, "_browser_sort_players"),
        enum_with(menus, "_browser_tab_all"), enum_with(menus, "_browser_group_dedicated"),
        "\n".join(re.findall(r"^#define BROWSER_HEADING(?:_GROUP)?\(\w+\) [^\n]+", menus, re.M)),
        "#define BROWSER_LISTED_MAXIMUM %s" % re.search(r"#define BROWSER_LISTED_MAXIMUM (.+)", menus).group(1),
        enum_with(menus, "_browser_ping_measured"),
        enum_with(menus, "PING_GOOD = 80"), enum_with(menus, "_browser_search_server"),
        "#define BROWSER_SEARCH_LENGTH %s" % re.search(r"#define BROWSER_SEARCH_LENGTH (\d+)", menus).group(1),
        enum_with(menus, "_browser_tick_any"), enum_with(menus, "_browser_type_any"),
        enum_with(read("source/game/game_engine.h"), "_game_mode_none = 0"),
        "#define GAME_MODES_KIND_HARDCORE %s" % re.search(r"#define GAME_MODES_KIND_HARDCORE (\w+)",
                                                           read("port/linux/game/game_modes.h")).group(1),
        "\n".join(re.findall(r"^#define BROWSER_PING_\w+ L[^\n]+", menus, re.M)),
        "#define BROWSER_MODE_LENGTH %s" % re.search(r"#define BROWSER_MODE_LENGTH (\d+)", menus).group(1),
        "#define MENU_TEXT_ELLIPSIS_LENGTH %s" % re.search(r"#define MENU_TEXT_ELLIPSIS_LENGTH (\d+)",
                                                           menus).group(1),
        structure(menus, "browser_sort_entry"), structure(menus, "browser_filters"),
        *(re.search(r"^static char const \*const %s\[[^]]*\] =\n\{.*?^\};" % name, menus, re.M | re.S).group(0)
          for name in ("browser_tick_words", "browser_type_words", "browser_tab_words", "browser_sort_words"))]) + "\n"
    code = "\n\n".join(function(menus, name) for name in (
        "settings_wide_copy", "browser_text_compare", "browser_wide_compare", "browser_sort_before",
        "browser_group_of", "browser_group_headed", "browser_group_heading", "browser_tab_shows", "browser_tab_lists_offline", "browser_search_key", "browser_text_has",
        "browser_ascii_has", "browser_search_shows", "browser_search_reads_map_shown", "browser_tick_shows",
        "browser_type_shows", "browser_word_index", "browser_filters_show", "browser_filters_read", "browser_view_read",
        "browser_view_write", "browser_order_part", "browser_favorites_order", "browser_empty_text",
        "menu_text_cut", "menu_text_fit", "browser_ping_cell", "browser_ping_bars"))
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("browser_menu", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("browser_menu", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


def _port_settings():
    sys.path.insert(0, str(ROOT / "tools"))
    import port_settings

    return port_settings


def test_files_are_the_generators():
    """The browser's file and Multiplayer's are tools/port_settings.py's, the browser's folder its own (the PC
    version's tags, which ce_menus.py needs, write none of it, nor its filters), and both are embedded."""
    port_settings = _port_settings()
    files = port_settings.multiplayer_files()
    listed = json.loads((ROOT / "port/assets/menus/menus.json").read_text())["files"]
    for name in (f"{MT}/join_game".replace("/", ".") + ".xml", f"{MT}/hub".replace("/", ".") + ".xml"):
        assert (CE / name).read_text() == "\n".join(files[name]), f"run python3 tools/port_settings.py ({name})"
        assert f"ce/{name}" in listed
    assert port_settings.replaced(f"{MT}/join_game") and port_settings.replaced(f"{MT}/join_game/filters")
    assert not (CE / "main_menu.multiplayer_type_select.join_game.filters.xml").exists()


def test_parts_pictures_are_drawn():
    """Each of the shared parts' pictures is in bitmaps.xml as ce_menus.py's Art.port_picture writes it (a frame
    each, its PNG embedded, a whole multiple of its size), its SVG the generator's, and ce_menus.py draws them."""
    port_settings = _port_settings()
    bitmaps = (CE / "bitmaps.xml").read_text()
    listed = json.loads((ROOT / "port/assets/menus/menus.json").read_text())["files"]
    for name, frames in port_settings.parts_pictures():
        lines = [f'\t<bitmap name="{name}">']
        for index, (svg, width, height) in enumerate(frames):
            png = f"ce/port/{name}__{index}.png"
            lines.append(f'\t\t<frame png="{png}" width="{width}" height="{height}"/>')
            assert png in listed, png
            assert (ROOT / "port/assets/menus/port_svg" / f"{name}__{index}.svg").read_text() == svg, name
        lines.append("\t</bitmap>")
        assert "\n".join(lines) in bitmaps, f"{name} is not in bitmaps.xml as ce_menus.py writes it"
    assert "port_settings.parts_pictures()" in read("tools/ce_menus.py")


@pytest.mark.parametrize("aspect", ["4:3", "16:10", "16:9", "21:9"])
def test_browser_wide_layout(aspect):
    """The browser at 4:3, 16:10 (the Steam Deck), 16:9 and 21:9 (16:9's, centred): its rows from the screen's left,
    their columns in order inside them, apart, each title over its column, Server, Map and Mode wider on a wider
    screen; the panel beside them to the screen's right, its words and JOIN GAME in it; the tabs spread apart between
    LB's and RB's prompts; the footer's prompts at the screen's edges, apart; everything on the screen, WIDE_MARGIN from
    its edges."""
    from harness.menu_layout import inner, layout, one

    port_settings = _port_settings()
    width = port_settings.WIDE_SCREENS[aspect]
    area = inner(width)
    boxes = layout(f"{MT}/join_game/join_game_screen", width)
    rows = [box for box in boxes if re.fullmatch(r"server_item_\d+", box.name)]
    panel = one(boxes, "browser_panel")
    assert len(rows) == port_settings.BROWSER_ROWS
    columns = ("server_name", "map", "type", "players", "ping_ms")
    for row in rows + [panel, one(boxes, "browser_columns_line"), one(boxes, "browser_empty")]:
        assert row.inside(area), f"{row} is within the margin of the screen's edge {area}"
    for row in rows:
        assert row.x1 + 8 <= panel.x0, f"{row} reaches the panel {panel}"
        cells = [box for column in columns for box in boxes
                 if box.name == f"server_item_{column}" and box.path[-1] == row.name]
        assert len(cells) == len(columns)
        for cell in cells:
            assert cell.inside(row), f"{cell} is out of its row {row}"
        assert all(cells[index].x1 <= cells[index + 1].x0 for index in range(len(cells) - 1)), cells
    # (each title over its column, in order)
    first = rows[0]
    titles = []
    for header, _, column in port_settings.BROWSER_HEADERS:
        title = one(boxes, header)
        cell = [box for box in boxes if box.name == f"server_item_{column if column != 'ping' else 'ping_ms'}"
                and box.path[-1] == first.name][0]
        if column in ("players", "ping"):
            assert title.x1 == cell.x1, (title, cell)
        else:
            assert cell.x0 - 8 <= title.x0 <= cell.x0, (title, cell)
        titles.append(title)
    assert all(titles[index].x1 <= titles[index + 1].x0 for index in range(len(titles) - 1)), titles
    for name in ("panel_title", "panel_where", "panel_players", "panel_ping", "panel_status", "panel_kind",
                 "panel_tick", "panel_opence", "button_join"):
        assert one(boxes, name).inside(panel), name
    tabs = [box for box in boxes if box.name.startswith("tab_") and box.path[-1] == "browser_tabs"]
    prompts = [one(boxes, "tab_prompt_previous"), one(boxes, "tab_prompt_next")]
    assert len(tabs) == len(port_settings.BROWSER_TABS)
    for index, box in enumerate(tabs + prompts):
        assert box.inside(area), box
        assert all(not box.overlaps(other) for other in (tabs + prompts)[index + 1:]), box
    footer = [box for box in boxes if box.name.startswith("prompt_") and box.name != "prompt_paste"]
    for index, box in enumerate(footer):
        assert box.inside(area), box
        assert all(not box.overlaps(other) for other in footer[index + 1:]), box
    # (the room the screen adds: Server's, Map's and Mode's, and the panel's; 21:9 lays out as 16:9)
    narrow = layout(f"{MT}/join_game/join_game_screen", 640)
    for name in ("server_item_server_name", "server_item_map", "server_item_type", "browser_panel"):
        wide, plain = one(boxes, name) if name == "browser_panel" else [box for box in boxes if box.name == name][0], \
            one(narrow, name) if name == "browser_panel" else [box for box in narrow if box.name == name][0]
        assert (wide.x1 - wide.x0 > plain.x1 - plain.x0) == (width > 640), name
    if width > port_settings.WIDE_WIDTH:
        assert boxes == layout(f"{MT}/join_game/join_game_screen", port_settings.WIDE_WIDTH)
