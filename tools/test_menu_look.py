"""The menu engine's model of the widgets' look (tools/menu_look.py): written again from ce/'s widgets it is the
same, byte for byte, as the files in port/assets/menus/model/look, and the same each time it is written, so that
the generators may write it alone in place of the widgets' XML (docs/menu_engine.md, step 5)."""

import json
from pathlib import Path

import menu_look

MENUS = Path(__file__).resolve().parent.parent / "port" / "assets" / "menus"


def test_look_files_are_the_widgets():
    made = menu_look.look_files(MENUS / "ce")
    written = {f"{menu_look.LOOK}/{path.name}": path.read_text() for path in (MENUS / menu_look.LOOK).glob("*.xml")}
    assert sorted(made) == sorted(written)
    for name, lines in made.items():
        assert "\n".join(lines) == written[name], f"{name} is not what tools/menu_look.py writes: run it"


def test_look_files_are_the_same_each_time():
    assert menu_look.look_files(MENUS / "ce") == menu_look.look_files(MENUS / "ce")


def test_look_files_are_embedded():
    listed = set(json.loads((MENUS / "menus.json").read_text())["files"])
    assert {name for name in menu_look.look_files(MENUS / "ce")} <= listed


def test_roles():
    """every role menu_functions.c finds a widget by (settings_part, settings_is_part) is a look's in the model
    (tools/port_settings.py's settings_look_roles), each one widget's but the tabs' and button bars', one a screen,
    and Settings' search's: its field (the tabs' and its screen's) and its crumbs (its groups')"""
    import re
    import xml.etree.ElementTree as ET
    roles = {}
    for path in (MENUS / menu_look.LOOK).glob("*.xml"):
        for look in ET.parse(path).getroot():
            if look.get("role"):
                roles.setdefault(look.get("role"), []).append(look.get("name"))
    source = "".join((MENUS.parent.parent / "linux/game" / name).read_text()
                     for name in ("menu_functions.c", "settings_search_menu.inc", "menu_engine.c"))
    used = set(re.findall(r'(?:settings_(?:part|is_part)|engine_child_of_role)\([^,]+, "([^"]+)"\)', source))
    used |= {f"still {still}{part}" for still in ("classic", "enhanced") for part in ("", " frame", " caption")}
    assert used and used <= set(roles), used - set(roles)
    for role, names in roles.items():
        assert len(names) == 1 or role in ("tabs", "search tabs", "buttons", "search field", "search crumb"), \
            (role, names)
