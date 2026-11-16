"""The menu engine's model of the widgets' behaviour (tools/menu_behaviour.py, port/assets/menus/model/behaviour): it is
what ce/'s widgets have (their behaviour flags and their <on> handlers, every one), and the game embeds it."""

import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import menu_behaviour  # noqa: E402

MENUS = menu_behaviour.MENUS


def test_written_from_ce():
    """the files are those menu_behaviour.py writes from ce/'s now (run tools/menus_regen.sh, or
    python3 tools/menu_behaviour.py, after changing a generator)"""
    files = menu_behaviour.behaviour_files()
    assert files
    for name, lines in files.items():
        assert (MENUS / name).read_text() == "\n".join(lines), name
    assert {f"{menu_behaviour.BEHAVIOUR}/{path.name}" for path in (MENUS / menu_behaviour.BEHAVIOUR).glob("*.xml")} \
        == set(files)


def test_every_handler_and_flag():
    """each widget's <on> handlers are in its behaviour, in their order, and its flags but the look's"""
    behaviours = {}
    for path in (MENUS / menu_behaviour.BEHAVIOUR).glob("*.xml"):
        for element in ET.parse(path).getroot():
            behaviours[element.get("name")] = element
    for path in (MENUS / "ce").glob("*.xml"):
        if path.name in menu_behaviour.menu_look.NOT_WIDGETS:
            continue
        for widget in ET.parse(path).getroot().iter("widget"):
            flags = [word for word in (widget.get("flags") or "").split() if word not in menu_behaviour.LOOK_FLAGS]
            handlers = widget.findall("on")
            behaviour = behaviours.get(widget.get("name"))
            if not flags and not handlers:
                assert behaviour is None, widget.get("name")
                continue
            assert behaviour is not None, widget.get("name")
            assert (behaviour.get("flags") or "").split() == flags, widget.get("name")
            assert [dict(on.attrib) for on in behaviour.findall("on")] == [dict(on.attrib) for on in handlers], \
                widget.get("name")


def test_embedded():
    """menus.json lists every file of the behaviour, as it lists the menus' (the game embeds those)"""
    listed = set(json.loads((MENUS / "menus.json").read_text())["files"])
    assert set(menu_behaviour.behaviour_files()) <= listed
