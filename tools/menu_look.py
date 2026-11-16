#!/usr/bin/env python3
"""The look of the generated menus' widgets, for the menu engine's model (docs/menu_engine.md): each widget's
bounds, places, pictures and text, and where its children are, written to port/assets/menus/model/look/ beside
the rows' model (model/settings.xml), one file for each of ce/'s files of widgets.

The generators (tools/ce_menus.py, tools/port_settings.py) call it once they have written ce/'s files, so that
the widgets and their look cannot disagree; `python3 tools/menu_look.py` writes it again from them alone. The
engine (port/linux/game/menu_engine.c) draws the generated screens from it, not from their widget definitions.

    <look name="main_menu/..." type="text" box="x0 y0 x1 y1" wide="dx dw" past="dx dw" place="anchor_left stretch keep_ends foot above_foot"
          picture="<bitmap>" radius="8" effects="flash_bitmap nifty_fx" list="items_in_code ..."
          header="<bitmap>" header_box="x0 y0 x1 y1" footer="<bitmap>" footer_box="x0 y0 x1 y1">
        <text font="large" color="#AARRGGBB" align="center" at="x y" flags="wrap ..." string_list="<strings>"
              index="3" text="..." items="A|B" replace="<search> <function>"/>
        <at widget="<child>" x="0" y="0"/>
    </look>

A box is the widget's area in its parent's units, left, top, right and bottom; a header's or footer's in its
list's. Each attribute is left out where it is its default (the widgets' own: type container, box 0 0 640 480,
no picture, no text). The text is its own element so that a text style's attributes (outlines, glows, shadows:
the MSDF text's distance) are added there alone.
"""

import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import quoteattr

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port" / "assets" / "menus"
CE = MENUS / "ce"
# (beside the rows' model, model/settings.xml: menu_model.c reads the folder)
LOOK = "model/look"
# ce/'s files that are not widgets
NOT_WIDGETS = ("bitmaps.xml", "strings.xml")

# the widget flags that are look (the rest are behaviour, the widgets' definitions' until the engine has it)
PLACE_FLAGS = ("anchor_left", "anchor_right", "stretch", "keep_ends", "foot", "above_foot")
EFFECT_FLAGS = ("flash_bitmap", "nifty_fx")


def _box(widget: ET.Element) -> str:
    left, top = int(widget.get("left", 0)), int(widget.get("top", 0))
    return f"{left} {top} {left + int(widget.get('width', 640))} {top + int(widget.get('height', 480))}"


def _bounds_box(text: str) -> str:
    """a list's header or footer's bounds ("top left bottom right") as a box"""
    top, left, bottom, right = text.split()
    return f"{left} {top} {right} {bottom}"


def _attributes(pairs) -> str:
    return "".join(f" {name}={quoteattr(value)}" for name, value in pairs if value is not None)


def _words(text, kept) -> str:
    words = [word for word in (text or "").split() if word in kept]
    return " ".join(words) or None


def _text(widget: ET.Element):
    """a widget's text element's attributes (None: it has no text)"""
    get = widget.get
    keys = ("font", "color", "align", "text_x", "text_y", "text_flags", "string_list", "string_index", "text",
            "strings", "text_scale")
    if not any(get(key) is not None for key in keys) and not widget.findall("replace"):
        return None
    at = f"{get('text_x', 0)} {get('text_y', 0)}" if get("text_x") or get("text_y") else None
    replaces = [f"{item.get('search')} {item.get('function', 'none')}" for item in widget.findall("replace")]
    return [("font", get("font")), ("color", get("color")), ("align", get("align")), ("at", at),
            ("flags", get("text_flags")), ("string_list", get("string_list")), ("index", get("string_index")),
            ("text", get("text")), ("items", get("strings")), ("replace", "|".join(replaces) or None),
            ("scale", get("text_scale"))]


def _tall() -> dict:
    """the widgets of the screens laid out in the tall layout (tools/port_settings.py's tall_widgets), by their
    names: the engine draws them so, not as ce/ has them"""
    import port_settings
    menus = ET.fromstring("<menus>" + "\n".join(port_settings.tall_widgets()) + "</menus>")
    return {widget.get("name"): widget for widget in menus.iter("widget")}


def _roles() -> dict:
    """the widgets' roles, by their names (tools/port_settings.py's settings_look_roles)"""
    import port_settings
    return port_settings.settings_look_roles()


def _look(widget: ET.Element, roles: dict) -> list:
    get = widget.get
    box = _box(widget)
    flags = get("flags")
    pairs = [("name", get("name")), ("role", roles.get(get("name"))),
             ("type", None if get("type", "container") == "container" else get("type")),
             ("box", None if box == "0 0 640 480" else box), ("wide", get("wide")), ("past", get("past")),
             ("place", _words(flags, PLACE_FLAGS)), ("picture", get("bitmap")), ("radius", get("radius")),
             ("effects", _words(flags, EFFECT_FLAGS)), ("list", get("list_flags")),
             ("header", get("header_bitmap")),
             ("header_box", _bounds_box(get("header_bounds")) if get("header_bounds") else None),
             ("footer", get("footer_bitmap")),
             ("footer_box", _bounds_box(get("footer_bounds")) if get("footer_bounds") else None)]
    inner = []
    text = _text(widget)
    if text is not None:
        inner.append(f"\t\t<text{_attributes(text)}/>")
    seen = {}
    for child in widget:
        if child.tag not in ("widget", "child"):
            continue
        name = child.get("name") if child.tag == "widget" else child.get("widget")
        # (a child is found by its name and its place among those of that name, which the platforms must not
        # change: the engine matches them so, menu_engine.c)
        if name in seen and (seen[name] or child.get("platform")):
            raise SystemExit(f"menu_look: {get('name')} has {name} twice, for a platform: the engine cannot "
                             "tell them apart")
        seen[name] = seen.get(name) or child.get("platform") is not None
        inner.append(f"\t\t<at{_attributes([('widget', name), ('x', child.get('x', '0')), ('y', child.get('y', '0'))])}/>")
    if not inner:
        return [f"\t<look{_attributes(pairs)}/>"]
    return [f"\t<look{_attributes(pairs)}>", *inner, "\t</look>"]


def look_files(ce: Path = CE) -> dict:
    """the look's files, by their paths in port/assets/menus: one for each of ce/'s files of widgets (a widget
    of a screen laid out in the tall layout as it is there)"""
    files = {}
    roles = _roles()
    tall = _tall()
    named = set()
    for path in sorted(ce.glob("*.xml")):
        if path.name in NOT_WIDGETS:
            continue
        lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                 f"<!-- The look of ce/{path.name}'s widgets, for the menu engine (docs/menu_engine.md; "
                 "tools/menu_look.py) -->", "<model>"]
        for widget in ET.parse(path).getroot().iter("widget"):
            lines += _look(tall.get(widget.get("name"), widget), roles)
            named.add(widget.get("name"))
        files[f"{LOOK}/{path.name}"] = lines + ["</model>", ""]
    if set(tall) - named:
        raise SystemExit(f"menu_look: no widgets {', '.join(sorted(set(tall) - named))} in ce/ for their tall looks")
    if set(roles) - named:
        raise SystemExit(f"menu_look: no widgets {', '.join(sorted(set(roles) - named))} for their roles")
    return files


def write(menus: Path = MENUS) -> list:
    """the look's files written again from ce/'s (those of files no longer there removed): their paths in
    menus/"""
    folder = menus / LOOK
    folder.mkdir(parents=True, exist_ok=True)
    files = look_files(menus / "ce")
    for name, lines in files.items():
        (menus / name).write_text("\n".join(lines))
    for path in folder.glob("*.xml"):
        if f"{LOOK}/{path.name}" not in files:
            path.unlink()
    return sorted(files)


def main() -> int:
    import json
    names = write()
    # (and listed in menus.json, as ce_menus.py lists every file)
    listed = json.loads((MENUS / "menus.json").read_text())
    files = [name for name in listed["files"] if not name.startswith(LOOK + "/")]
    xmls = sorted([name for name in files if name.endswith(".xml")] + names)
    listed["files"] = xmls + [name for name in files if not name.endswith(".xml")]
    (MENUS / "menus.json").write_text(json.dumps(listed, indent=1) + "\n")
    print(f"menu_look: {len(names)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
