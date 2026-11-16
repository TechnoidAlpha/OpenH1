"""The menu engine's model of the setup screens (tools/menu_nodes.py, port/assets/menus/model/nodes.xml): their nodes
are ce/'s widgets, their conditions tools/port_settings.py's (SETUP_NODE_WHEN), each term one menu_functions.c's
settings_condition_term knows, and the game embeds it."""

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_nodes  # noqa: E402
import port_settings  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENUS = menu_nodes.MENUS


def test_written_from_ce():
    """the file is what menu_nodes.py writes from ce/'s now (run tools/menus_regen.sh, or
    python3 tools/menu_nodes.py, after changing a generator)"""
    for name, lines in menu_nodes.node_files().items():
        assert (MENUS / name).read_text() == "\n".join(lines), name


def test_embedded():
    """menus.json lists it, as it lists the menus' files (the game embeds those)"""
    listed = set(json.loads((MENUS / "menus.json").read_text())["files"])
    assert set(menu_nodes.node_files()) <= listed


def _screens() -> dict:
    root = ET.parse(MENUS / menu_nodes.NODES).getroot()
    return {screen.get("name"): screen.findall("node") for screen in root.findall("screen")}


def test_conditions_known():
    """every node's condition is SETUP_NODE_WHEN's, names terms settings_condition_term knows, and each
    "<key>=<n>" names a spinner of the screen with an nth item"""
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    body = source[source.index("static boolean settings_condition_term("):]
    body = body[:body.index("\n}\n")]
    terms = set(re.findall(r'!strcmp\(name, "(\w+)"\)', body))
    widgets = menu_nodes._widgets(MENUS / "ce")
    lists = {element.get("name"): [item.get("text") for item in element.findall("string")]
             for path in (MENUS / "ce").glob("*.xml") for element in ET.parse(path).getroot().iter("strings")}
    screens = _screens()
    assert list(screens) == port_settings.SETUP_NODE_SCREENS
    for screen, nodes in screens.items():
        names = {node.get("name") for node in nodes}
        whens = {node.get("name"): node.get("when") for node in nodes if node.get("when")}
        assert whens == port_settings.SETUP_NODE_WHEN.get(screen, {}), screen
        for name, when in whens.items():
            for term in re.split(r"[ +]", when):
                term = term.lstrip("!")
                if "=" not in term:
                    assert term in terms, (name, term)
                    continue
                key, value = term.split("=")
                spinner = f"{screen.rsplit('/', 1)[0]}/{key}_spinner"
                assert spinner in names, (name, term)
                items = widgets[spinner].get("strings")
                items = items.split("|") if items else lists.get(widgets[spinner].get("string_list"))
                assert items and int(value) < len(items), (name, term)


def test_one_initial():
    """each screen's focus starts on one node, a focusable one"""
    for screen, nodes in _screens().items():
        initial = [node for node in nodes if node.get("initial")]
        assert len(initial) == 1 and initial[0].get("focusable") == "true", screen


def test_host_settings():
    """every spinner of the host's own settings (SETUP_ROW_SETTINGS) is a node of the setup screens with its setting
    and values, in its words' order; and menu_functions.c's gametype_options names each of them (an _option_setting)
    with no setting or values of its own, which gametype_option_setting reads from the nodes"""
    found = {}
    for nodes in _screens().values():
        for node in nodes:
            if node.get("setting"):
                key = node.get("name").rsplit("/", 1)[1][:-len("_spinner")]
                found.setdefault(key, set()).add((node.get("setting"),
                                                  tuple(choice.get("value") for choice in node.findall("choice"))))
    assert found == {key: {(setting, tuple(values))} for key, (setting, values) in
                     port_settings.SETUP_ROW_SETTINGS.items()}
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    table = source[source.index("static struct gametype_option const gametype_options[] ="):]
    table = table[:table.index("\n};")]
    assert set(re.findall(r'\{ "(\w+)_spinner", _option_setting \}', table)) == set(port_settings.SETUP_ROW_SETTINGS)
    assert table.count("_option_setting") == len(port_settings.SETUP_ROW_SETTINGS)


def test_server_settings_help():
    """Server Setup's rows' places in its help (help_index, help_choices) are their words' in its help_strings
    (tools/port_settings.py's SERVER_SETTINGS_HELP), and menu_functions.c keeps no places of its own"""
    lists = {element.get("name"): [item.get("text") for item in element.findall("string")]
             for path in (MENUS / "ce").glob("*.xml") for element in ET.parse(path).getroot().iter("strings")}
    strings = lists[f"{port_settings.SS}/help_strings"]
    nodes = {node.get("name"): node for screen in _screens().values() for node in screen}
    for key, texts in port_settings.SERVER_SETTINGS_HELP:
        if not key:
            continue
        node = nodes[f"{port_settings.SS}/op_{key}"]
        first = int(node.get("help_index"))
        assert strings[first:first + len(texts)] == [text.replace("\\n", "\n") for text in texts] or \
            strings[first:first + len(texts)] == texts, key
        assert (node.get("help_choices") == "true") == (len(texts) > 1), key
    source = (ROOT / "port/linux/game/menu_functions.c").read_text()
    assert not re.search(r"#define \w+_HELP \d+", source)
