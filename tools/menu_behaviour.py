#!/usr/bin/env python3
"""The behaviour of the generated menus' widgets, for the menu engine's model (docs/menu_engine.md): how each
widget takes its events (its behaviour flags) and what they do (its actions, the menus' <on> handlers), written to
port/assets/menus/model/behaviour/ beside the rows' model and the look (model/settings.xml, model/look/), one file
for each of ce/'s files of widgets.

The generators (tools/ce_menus.py, tools/port_settings.py) call it once they have written ce/'s files, as they call
tools/menu_look.py, so that the widgets and their behaviour cannot disagree; `python3 tools/menu_behaviour.py`
writes it again from them alone. The engine (port/linux/game/menu_engine.c) takes the generated screens' events by
it, not by their widget definitions.

    <behaviour name="main_menu/..." flags="pass_unhandled_to_focused_child up_down_tabs_children">
        <on event="a start" run="port settings ok" back="true"/>
    </behaviour>

A widget with neither flags nor actions has none. An <on> has the attributes of the menus' own (port/assets/menus/
README.md): event, run, open, replace, close, back, branch, sound, otherwise, focus, reload, widget, script, label.
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import quoteattr

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_look  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port" / "assets" / "menus"
# (beside the look, model/look: menu_model.c reads the folder)
BEHAVIOUR = "model/behaviour"
# the widget flags that are behaviour: the rest are the look's (tools/menu_look.py)
LOOK_FLAGS = menu_look.PLACE_FLAGS + menu_look.EFFECT_FLAGS
# an <on>'s attributes, in the order they are written
ON_ATTRIBUTES = ("event", "run", "open", "replace", "close", "back", "branch", "sound", "otherwise", "focus",
                 "reload", "widget", "script", "label")


def _attributes(pairs) -> str:
    return "".join(f" {name}={quoteattr(value)}" for name, value in pairs if value is not None)


def _behaviour(widget: ET.Element) -> list:
    flags = " ".join(word for word in (widget.get("flags") or "").split() if word not in LOOK_FLAGS) or None
    handlers = widget.findall("on")
    if not flags and not handlers:
        return []
    for handler in handlers:
        unknown = set(handler.keys()) - set(ON_ATTRIBUTES)
        if unknown:
            raise SystemExit(f"menu_behaviour: {widget.get('name')}'s <on> has {', '.join(sorted(unknown))}, which "
                             "it does not write")
    pairs = [("name", widget.get("name")), ("flags", flags)]
    if not handlers:
        return [f"\t<behaviour{_attributes(pairs)}/>"]
    return [f"\t<behaviour{_attributes(pairs)}>",
            *(f"\t\t<on{_attributes((name, handler.get(name)) for name in ON_ATTRIBUTES)}/>" for handler in handlers),
            "\t</behaviour>"]


def behaviour_files(ce: Path = menu_look.CE) -> dict:
    """the behaviour's files, by their paths in port/assets/menus: one for each of ce/'s files of widgets"""
    files = {}
    for path in sorted(ce.glob("*.xml")):
        if path.name in menu_look.NOT_WIDGETS:
            continue
        lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                 f"<!-- The behaviour of ce/{path.name}'s widgets, for the menu engine (docs/menu_engine.md; "
                 "tools/menu_behaviour.py) -->", "<model>"]
        for widget in ET.parse(path).getroot().iter("widget"):
            lines += _behaviour(widget)
        files[f"{BEHAVIOUR}/{path.name}"] = lines + ["</model>", ""]
    return files


def write(menus: Path = MENUS) -> list:
    """the behaviour's files written again from ce/'s (those of files no longer there removed): their paths in
    menus/"""
    folder = menus / BEHAVIOUR
    folder.mkdir(parents=True, exist_ok=True)
    files = behaviour_files(menus / "ce")
    for name, lines in files.items():
        (menus / name).write_text("\n".join(lines))
    for path in folder.glob("*.xml"):
        if f"{BEHAVIOUR}/{path.name}" not in files:
            path.unlink()
    return sorted(files)


def main() -> int:
    import json
    names = write()
    # (and listed in menus.json, as ce_menus.py lists every file)
    listed = json.loads((MENUS / "menus.json").read_text())
    files = [name for name in listed["files"] if not name.startswith(BEHAVIOUR + "/")]
    xmls = sorted([name for name in files if name.endswith(".xml")] + names)
    listed["files"] = xmls + [name for name in files if not name.endswith(".xml")]
    (MENUS / "menus.json").write_text(json.dumps(listed, indent=1) + "\n")
    print(f"menu_behaviour: {len(names)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
