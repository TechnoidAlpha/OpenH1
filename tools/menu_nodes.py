#!/usr/bin/env python3
"""The setup screens' model, for the menu engine (docs/menu_engine.md): Server Setup, its co-op options' screen and
the gametype editor's option screens (tools/port_settings.py's SETUP_NODE_WHEN) as screens of nodes, written to
port/assets/menus/model/nodes.xml beside the Settings screens' rows (model/settings.xml).

A screen's nodes are its widgets, from the screen down (each after its parent), keyed by the widgets' names as the
look and the behaviour have them (model/look, model/behaviour); each with what a press of A does (open, run), whether
the focus may take it (a widget with handlers or one that passes its events on to its children), the first of
those in the screen's first list (initial), its condition (when), port_settings.py's, its place in its screen's help
(help_index, and help_choices for a spinner's row whose choices each have their own: Server Setup's,
port_settings.py's SERVER_SETTINGS_HELP), and a spinner of the host's own settings its setting and choices:

    <screen name="main_menu/.../server_settings_screen" kind="nodes">
        <node name="main_menu/.../options_menu" parent="main_menu/.../server_settings_screen" focusable="true"/>
        <node name="main_menu/.../op_listing" parent="main_menu/.../options_menu" focusable="true"
              when="internet_host+online+public_lobby"/>
    </screen>

The generators (tools/ce_menus.py, tools/port_settings.py) call it once they have written ce/'s files, as they call
tools/menu_look.py; `python3 tools/menu_nodes.py` writes it again from them alone. menu_functions.c tests the
nodes' conditions each frame as the screen is shown (setup_nodes_conditions).
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import quoteattr

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_look  # noqa: E402
import port_settings  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port" / "assets" / "menus"
NODES = "model/nodes.xml"


def _attributes(pairs) -> str:
    return "".join(f" {name}={quoteattr(value)}" for name, value in pairs if value is not None)


def _widgets(ce: Path) -> dict:
    """ce/'s widgets by their names (the first of a name: its children are the same on every platform,
    menu_look.py's check)"""
    widgets = {}
    for path in sorted(ce.glob("*.xml")):
        if path.name in menu_look.NOT_WIDGETS:
            continue
        for widget in ET.parse(path).getroot().iter("widget"):
            widgets.setdefault(widget.get("name"), widget)
    return widgets


def _pressed(widget: ET.Element) -> tuple:
    """what A does on the widget: its handler's (open, run)"""
    for handler in widget.findall("on"):
        if "a" in (handler.get("event") or "").split():
            return handler.get("open"), handler.get("run")
    return None, None


def _words(widget: ET.Element, lists: dict) -> list:
    """a spinner's words: its own (strings=), or its string list's"""
    if widget.get("strings"):
        return widget.get("strings").split("|")
    return lists.get(widget.get("string_list"), [])


def _settings(widget_name: str, widget: ET.Element, lists: dict) -> tuple:
    """a spinner of the host's own settings: its setting= and its <choice>s (tools/port_settings.py's
    SETUP_ROW_SETTINGS), else (None, [])"""
    leaf = widget_name.replace("\\", "/").rsplit("/", 1)[-1]
    key = leaf[:-len("_spinner")] if leaf.endswith("_spinner") else None
    if key not in port_settings.SETUP_ROW_SETTINGS:
        return None, []
    setting, values = port_settings.SETUP_ROW_SETTINGS[key]
    words = _words(widget, lists)
    if len(words) != len(values):
        raise SystemExit(f"menu_nodes: {widget_name} shows {len(words)} words, its setting {setting} has "
                         f"{len(values)} values")
    return setting, [f"\t\t\t<choice{_attributes([('shown', word), ('value', value)])}/>"
                     for word, value in zip(words, values)]


def _screen(name: str, whens: dict, widgets: dict, lists: dict) -> list:
    if name not in widgets:
        raise SystemExit(f"menu_nodes: ce/ has no screen {name}")
    lines, seen, initial = [], set(), [None]
    helps = port_settings.server_settings_help()
    unknown = set(whens)

    def walk(widget_name: str, parent: str, in_list: bool):
        widget = widgets.get(widget_name)
        if widget is None or widget_name in seen:
            return
        seen.add(widget_name)
        unknown.discard(widget_name)
        flags = (widget.get("flags") or "").split()
        focusable = bool(widget.findall("on")) or "pass_unhandled_to_focused_child" in flags
        first = in_list and focusable and initial[0] is None
        if first:
            initial[0] = widget_name
        opens, runs = _pressed(widget)
        setting, choices = _settings(widget_name, widget, lists)
        pairs = [("name", widget_name), ("parent", parent), ("open", opens), ("run", runs),
                 ("focusable", "true" if focusable else None), ("initial", "true" if first else None),
                 ("when", whens.get(widget_name)), ("setting", setting),
                 ("help_index", str(helps[widget_name][0]) if widget_name in helps else None),
                 ("help_choices", "true" if helps.get(widget_name, (0, False))[1] else None)]
        if choices:
            lines.extend([f"\t\t<node{_attributes(pairs)}>", *choices, "\t\t</node>"])
        else:
            lines.append(f"\t\t<node{_attributes(pairs)}/>")
        is_list = widget.get("type") == "column_list" and initial[0] is None
        for child in widget.findall("child"):
            walk(child.get("widget"), widget_name, is_list)

    walk(name, None, False)
    if unknown:
        raise SystemExit(f"menu_nodes: {name} has no widgets {', '.join(sorted(unknown))}")
    return [f"\t<screen{_attributes([('name', name), ('kind', 'nodes')])}>", *lines, "\t</screen>"]


def node_files(ce: Path = menu_look.CE) -> dict:
    """the nodes' file, by its path in port/assets/menus"""
    widgets = _widgets(ce)
    lists = {element.get("name"): [item.get("text") for item in element.findall("string")]
             for path in sorted(ce.glob("*.xml")) for element in ET.parse(path).getroot().iter("strings")}
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- The setup screens' nodes, for the menu engine (docs/menu_engine.md; tools/menu_nodes.py, "
             "tools/port_settings.py's SETUP_NODE_WHEN) -->", "<model>"]
    for name in port_settings.SETUP_NODE_SCREENS:
        lines += _screen(name, port_settings.SETUP_NODE_WHEN.get(name, {}), widgets, lists)
    return {NODES: lines + ["</model>", ""]}


def write(menus: Path = MENUS) -> list:
    """the nodes' file written again from ce/'s: its path in menus/"""
    files = node_files(menus / "ce")
    for name, lines in files.items():
        (menus / name).write_text("\n".join(lines))
    return sorted(files)


def main() -> int:
    import json
    names = write()
    # (and listed in menus.json, as ce_menus.py lists every file)
    listed = json.loads((MENUS / "menus.json").read_text())
    files = [name for name in listed["files"] if name not in names]
    xmls = sorted([name for name in files if name.endswith(".xml")] + names)
    listed["files"] = xmls + [name for name in files if not name.endswith(".xml")]
    (MENUS / "menus.json").write_text(json.dumps(listed, indent=1) + "\n")
    print(f"menu_nodes: {len(names)} file")
    return 0


if __name__ == "__main__":
    sys.exit(main())
