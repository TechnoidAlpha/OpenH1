"""The menus' hand-written screens (port/linux/src/menu_screens.c, docs/menu_screens.md): the real main menu's file
read into its building blocks; files with problems (bad XML, an element or attribute it may not have, an item with
no label or action, a focus or a when that names nothing, an action or binding the game has not got) refused with
their file and line, the screen read before kept and the problem logged; a file read again taking its screen's place;
bindings filled, and a line whose binding has no value not complete; an item's when. The actions and bindings are the
game's own tables (port/linux/game/menu_screen.c), and every action the game names is one it runs."""

import functools
import re
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ABI_FLAGS, CHECK_FAILED, HARNESS, ROOT, _directory, mutated, read, run  # noqa: E402

EXPAT = ROOT / "port/third_party/expat"
EXPAT_SOURCES = ("xmlparse.c", "xmlrole.c", "xmltok.c")

# a fault in the code, and the case whose check must catch it
NEGATIVE_CONTROLS = {
    # an action not checked against the game's: "open nowhere" read
    "no-action-check": ("\t\t\tif (!reader->failed)\n\t\t\t\taction_check(reader, item->action);\n", "", "actions"),
    # a binding of no value taken as complete: the saved game's hint shown with no saved game
    "binding-complete": ("\t\t\t\tcomplete = 0;\n", "\t\t\t\t(void)0;\n", "hints"),
    # a broken file read again taking its screen's place
    "broken-kept": ("\tif (!menu_screen_read(path, data, size, &screen, problem, sizeof(problem)))\n\t{\n",
                    "\tif (!menu_screen_read(path, data, size, &screen, problem, sizeof(problem)))\n\t{\n"
                    "\t\tmemset(&screens.screens[0], 0, sizeof(screens.screens[0]));\n", "kept"),
    # a list's focus not checked: one naming no item read
    "no-focus-check": ("focus_found = !list->focus;", "focus_found = 1;", "items"),
    # a <spartan>'s players not checked: five read
    "players-unchecked": ("spartan->players < 1 || spartan->players > 4", "spartan->players < 1", "spartan"),
    # a table's row source not checked: one the game has not got read
    "no-source-check": ("\t\t\telse\n\t\t\t\tsource_check(reader, table->source);\n", "\n", "table"),
    # a tab with no id read
    "tab-no-id": ("\t\t\tif (!tab->id || !tab->id[0])\n\t\t\t\treader_problem(reader, \"<tab> has no id\", NULL);\n"
                  "\t\t\telse if", "\t\t\tif", "tabs"),
    # a binding named alone (an if, a tabs' current) not checked
    "binding-unchecked": ("\telse if (screen_names && !name_known(screen_names->bindings, screen_names->binding_count, "
                          "name, strlen(name)))", "\telse if (0)", "tabs"),
    # a <spartan>'s color binding not checked: one the game has not got read
    "spartan-color-unchecked": ("\t\t\t\tbinding_check(reader, values[4]);\n", "\n", "spartan"),
    # a panel's text's if not checked: a binding the game has not got taken
    "panel-if-unchecked": ("shown_check(reader, text->condition, text->when);", "shown_check(reader, NULL, text->when);",
                           "panel"),
    # a column's touch place's numbers not counted: "1 2" taken
    "touch-short": ("if (count != 5 || *at", "if (*at", "table"),
    # a <lane> taken in a table of no lanes
    "lane-unlaned": ("if (!reader->failed && table->lanes < 2)", "if (0)", "lanes"),
    # a table of lanes taken with fewer <lane>s than its lanes
    "lanes-uncounted": ("if (lanes && lanes != screen->blocks[self].lanes)", "if (lanes > 99)", "lanes"),
    # a table's left not checked: an action the game has not got taken
    "table-left-unchecked": ("\t\t\t\taction_check(reader, table->left_action);\n", "\n", "lanes"),
    # a dot's column refused (the kinds counted short)
    "dot-unknown": ("for (kind = 0; kind < 6 && values[2]; kind++)", "for (kind = 0; kind < 4 && values[2]; kind++)",
                    "lanes"),
    # a swatch's column refused (the kinds counted short): Profile Color's file refused
    "swatch-unknown": ("for (kind = 0; kind < 6 && values[2]; kind++)",
                       "for (kind = 0; kind < 5 && values[2]; kind++)", "profile_color"),
    # a choice with no right taken
    "choice-no-right": ("else if (!choice->left_action || !choice->right_action)", "else if (!choice->left_action)",
                        "choice"),
    # a chat's source not checked: one the game has not got taken
    "chat-source-unchecked": ("else if (screen_names && !name_known(screen_names->chats, screen_names->chat_count, "
                              "chat->source,", "else if (0 && !name_known(screen_names->chats, screen_names->chat_count, "
                              "chat->source,", "chat"),
    # a second chat taken
    "chat-two": ("one_check(reader, MENU_SCREEN_CHAT, \"chat\");", "(void)0;", "chat"),
    # a screen's start not checked: an action the game has not got taken
    "start-unchecked": ("\t\tif (!reader->failed && screen->start)\n\t\t\taction_check(reader, screen->start);",
                        "(void)0;", "actions"),
    # a choice's dim not checked
    "choice-dim-unchecked": ("if (!reader->failed && choice->dim)\n\t\t\t\tbinding_check(reader, choice->dim);",
                             "(void)0;", "choice"),
    # an item's if not checked: a binding the game has not got taken
    "item-if-unchecked": ("\t\t\tif (!reader->failed && item->condition)\n\t\t\t\tbinding_check(reader, item->condition);\n",
                          "\n", "box"),
    # a picture's set not checked: "photos" taken
    "picture-set-unchecked": ("(strcmp(picture->picture, \"maps\") && strcmp(picture->picture, \"missions\"))", "0",
                              "picture"),
    # a picture's frame's binding not checked
    "picture-frame-unchecked": ("\t\t\telse\n\t\t\t\tbinding_check(reader, picture->value);\n", "\n", "picture"),
    # a panel's second <spartan> taken
    "panel-spartans-uncounted": ("screen->blocks[index].parent == parent_block)\n", "screen->blocks[index].parent == -99)\n",
                                 "spartan"),
    # a fifth <panel> taken
    "panels-uncounted": ("kind_count(screen, MENU_SCREEN_PANEL) >= MENU_SCREEN_MAXIMUM_PANELS",
                         "kind_count(screen, MENU_SCREEN_PANEL) > 99", "panels"),
    # a lineup taken over a table without lanes
    "lineup-unlaned": ("screen->blocks[index].parent < 0 && lanes < 2)", "screen->blocks[index].parent < 0 && lanes < 0)",
                       "panels"),
    # a box's align not checked: "bottom" taken
    "box-align-unchecked": ("values[3] && strcmp(values[3], \"top\") && strcmp(values[3], \"middle\")", "0", "box"),
    # a dialog's choices not counted: a fifth taken (#901)
    "dialog-choices-uncounted": ("else if (choices > MENU_SCREEN_MAXIMUM_CHOICES)", "else if (choices > 99)", "dialog"),
    # a dialog taken with no list of its choices
    "dialog-no-list": ("\t\tif (list < 0)\n\t\t\treader_problem(reader, \"a <dialog> has a <list> of its choices\", NULL);\n"
                       "\t\telse if", "\t\tif", "dialog"),
}


def c_string(data: bytes) -> str:
    """bytes as a C string's literal: its UTF-8 as octal escapes, a line a literal"""
    out = ['"']
    for byte in data:
        character = chr(byte)
        if character == "\n":
            out.append('\\n"\n"')
        elif character in '"\\':
            out.append("\\" + character)
        elif 32 <= byte < 127:
            out.append(character)
        else:
            out.append("\\%03o" % byte)
    return "".join(out) + '"'


def game_names():
    """the game's actions and bindings (menu_screen.c's screen_action_names and screen_binding_names), as C"""
    source = read("port/linux/game/menu_screen.c")
    lines = []
    for table, name in (("screen_action_names", "game_actions"), ("screen_binding_names", "game_bindings"),
                        ("screen_source_names", "game_sources"), ("screen_chat_names", "game_chats")):
        body = re.search(r"static char const \*const " + table + r"\[\] =\n\{\n(.*?)\n\};", source, re.S).group(1)
        # (and the test's own: a binding of a value and a row source, for the blocks' cases)
        extra = {"game_bindings": '"test.value", "test.flag",', "game_sources": '"test.rows",',
                 "game_chats": '"test.chat",'}.get(name, "")
        lines.append(f"static char const *const {name}[] =\n{{\n{extra}\n{body}\n}};")
    return "\n".join(lines) + "\n"


def under_test(fault=None):
    code = read("port/linux/src/menu_screens.c")
    # (the platform layer's headers: the fake world stands in for them; no SDL, as on Android)
    code = re.sub(r'^#include [^\n]*\n', "", code, flags=re.M)
    code = "#define HALO_ANDROID 1\n" + code
    if fault:
        before, after, _ = NEGATIVE_CONTROLS[fault]
        code = mutated(code, before, after)
    return code


@functools.lru_cache(maxsize=None)
def expat_objects():
    work = Path(_directory.name) / "expat"
    work.mkdir(parents=True, exist_ok=True)
    objects = []
    for name in EXPAT_SOURCES:
        output = work / (name + ".o")
        command = ["clang", *ABI_FLAGS, "-std=gnu99", "-O1", "-w", "-I", str(EXPAT), "-c", str(EXPAT / name), "-o",
                   str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"expat's {name} does not compile:\n{result.stderr}")
        objects.append(str(output))
    return tuple(objects)


@functools.lru_cache(maxsize=None)
def built(fault=None):
    work = Path(_directory.name) / f"menu_screens-{fault or 'real'}"
    work.mkdir(parents=True, exist_ok=True)
    (work / "under_test.inc").write_text(under_test(fault))
    (work / "names.inc").write_text(game_names())
    (work / "main_xml.inc").write_text("static char const main_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/main.xml").read_bytes()) + ";\n" +
                                       "static char const browser_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/browser.xml").read_bytes()) + ";\n" +
                                       "static char const profile_select_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/profile_select.xml").read_bytes()) +
                                       ";\n" + "static char const profile_color_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/profile_color.xml").read_bytes()) +
                                       ";\n" + "static char const lobby_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/lobby.xml").read_bytes()) +
                                       ";\n" + "static char const pause_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/pause.xml").read_bytes()) + ";\n" +
                                       "static char const pause_campaign_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/pause_campaign.xml").read_bytes()) +
                                       ";\n" + "static char const save_changes_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/save_changes.xml").read_bytes()) +
                                       ";\n" + "static char const quit_xml[] =\n" +
                                       c_string((ROOT / "port/assets/menus/screens/quit.xml").read_bytes()) + ";\n" + "".join(
                                           f"static char const {name}_xml[] =\n" +
                                           c_string((ROOT / f"port/assets/menus/screens/{name}.xml").read_bytes()) +
                                           ";\n" for name in ("coop", "coop_who", "coop_join", "add_player")))
    (work / "menu_screens.h").write_text(read("port/linux/include/menu_screens.h"))
    executable = work / "menu_screens"
    command = ["clang", *ABI_FLAGS, "-std=gnu99", "-O1", "-Wall", "-Werror", "-Wno-unused-function",
               "-Wno-unused-variable", "-I", str(HARNESS / "include"), "-I", str(work), "-I", str(EXPAT),
               str(HARNESS / "tests" / "menu_screens.c"), *expat_objects(), "-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"menu_screens.c does not compile:\n{result.stderr}")
    return executable


CASES = ["main", "hints", "good", "syntax", "elements", "spartan", "items", "actions", "bindings", "when",
         "kept", "browser", "profile_select", "profile_color", "tabs", "table", "panel", "buttons", "lanes", "choice", "lobby", "chat",
         "box", "picture", "panels", "dialog", "coop", "add_player"]


@pytest.mark.parametrize("case", CASES)
def test_menu_screens(case):
    status, output = run(built(), case)
    assert status == 0, output


@pytest.mark.parametrize("fault", sorted(NEGATIVE_CONTROLS))
def test_negative(fault):
    status, output = run(built(fault), NEGATIVE_CONTROLS[fault][2])
    assert status == CHECK_FAILED, output


def test_actions_registered():
    """every action a file may name is one the game runs (screen_actions), and every one it runs a file may name"""
    source = read("port/linux/game/menu_screen.c")
    names = set(re.findall(r'"([^"]+)"', re.search(r"screen_action_names\[\] =\n\{\n(.*?)\n\};", source,
                                                     re.S).group(1)))
    table = re.search(r"static struct screen_action const screen_actions\[\] =\n\{\n(.*?)\n\};", source, re.S).group(1)
    runs = set(re.findall(r'^\t\{ "([^"]+)"', table, re.M))
    assert names == runs, (names, runs)
    # (and the bindings it fills, each named by screen_value, the server browser's by menu_functions.c's
    # pc_menu_browser_value, Select Profile's by its pc_menu_profile_value; and every row source a file may name, one
    # it has)
    value = re.search(r"static int screen_value\(.*?\n\}\n", source, re.S).group(0)
    functions = read("port/linux/game/menu_functions.c")
    browser = re.search(r"\nint pc_menu_browser_value\(.*?\n\}\n", functions, re.S).group(0)
    profile = re.search(r"\nint pc_menu_profile_value\(.*?\n\}\n", functions, re.S).group(0)
    color = re.search(r"\nint pc_menu_color_value\(.*?\n\}\n", functions, re.S).group(0)
    # (the lobby's in its pc_menu_lobby_value, or its cards' and friends' values: lobby_card_value, lobby_friends_value)
    lobby = re.search(r"\nint pc_menu_lobby_value\(.*?\n\}\n", read("port/linux/game/lobby_menu.inc"), re.S).group(0)
    for function in ("lobby_card_value", "lobby_friends_value"):
        lobby += re.search(r"\nstatic int " + function + r"\(.*?\n\}\n", read("port/linux/game/lobby_menu.inc"),
                           re.S).group(0)
    bindings = set(re.findall(r'"([^"]+)"', re.search(r"screen_binding_names\[\] =\n\{\n(.*?)\n\};", source,
                                                        re.S).group(1)))
    for binding in bindings:
        if binding.startswith("browser.search."):
            assert '"browser.search."' in browser, binding
        elif binding.startswith("browser."):
            assert f'"{binding}"' in browser, binding
        elif binding.startswith("profile."):
            assert f'"{binding}"' in profile, binding
        elif binding.startswith("color."):
            assert f'"{binding}"' in color, binding
        elif binding.startswith("lobby."):
            assert f'"{binding}"' in lobby, binding
        elif binding.startswith("add."):
            # (Add Player's, menu_functions.c's pc_menu_add_player_value)
            assert '"add."' in value, binding
            assert f'"{binding}"' in re.search(r"\nint pc_menu_add_player_value\(.*?\n\}\n", functions,
                                                re.S).group(0), binding
        elif binding.startswith("coop."):
            # (co-op's, coop_menu.inc's pc_menu_coop_value)
            assert '"coop."' in value, binding
            assert f'"{binding}"' in re.search(r"\nint pc_menu_coop_value\(.*?\n\}\n",
                                                read("port/linux/game/coop_menu.inc"), re.S).group(0), binding
        else:
            assert f'"{binding}"' in value, binding
    sources = re.findall(r'"([^"]+)"', re.search(r"screen_source_names\[\] =\n\{\n(.*?)\n\};", source,
                                                 re.S).group(1))
    table = re.search(r"screen_sources\[\] =\n\{\n(.*?)\n\};", source, re.S).group(1)
    assert len(sources) == len(re.findall(r"&\w+", table)), (sources, table)


def test_main_menu_files_listed():
    """the screens and their blocks are embedded (menus.json, tools/embed_assets.py), and menu_files.c reads the
    blocks' file as menus but no screen file"""
    import json
    listed = json.loads(read("port/assets/menus/menus.json"))["files"]
    assert "screens/main.xml" in listed and "screens/blocks.xml" in listed and "screens/browser.xml" in listed
    assert "screens/profile_select.xml" in listed and "screens/lobby.xml" in listed
    assert "screens/profile_color.xml" in listed
    assert all(f"screens/{name}.xml" in listed for name in ("coop", "coop_who", "coop_join", "add_player"))
    assert "screens/pause.xml" in listed and "screens/pause_campaign.xml" in listed
    assert "screens/quit.xml" in listed and "screens/save_changes.xml" in listed
    files = read("port/linux/src/menu_files.c")
    assert 'strncmp(files[index].path, "screens/", 8) || !strcmp(files[index].path, "screens/blocks.xml")' in files
