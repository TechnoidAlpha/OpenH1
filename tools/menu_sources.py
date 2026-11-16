"""Where each menu widget comes from: the generated menu files' widgets
(port/assets/menus/ce), their attributes, and the generator's file, line and
functions that made each (its source attribute, which tools/port_settings.py
and tools/ce_menus.py write and the game ignores), for the menu review tools
(tools/menu_gallery.py's layouts, tools/menu_notes.py's briefs,
tools/menu_review.py's inspector).

A widget a generator wrote before the attribute came (the files of
tools/ce_menus.py, which needs the PC version's tags to be run again) is
given its file's generator and, for ce_menus.py's, the function that writes
every widget and the PC version's tag it is made from.
"""
import ast
import functools
import re
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port" / "assets" / "menus" / "ce"
# a widget's attributes the inspector shows
SHOWN = ("type", "left", "top", "width", "height", "font", "color", "align", "bitmap", "text", "string_list",
         "string_index", "text_flags", "flags", "wide", "platform")


@functools.lru_cache(maxsize=8)
def _catalog(menus: str, stamp: float) -> dict:
    found = {}
    for path in sorted(Path(menus).glob("*.xml")):
        text = path.read_text(errors="replace")
        match = re.search(r"<!--[^>]*\((tools/[\w./-]+\.py)\)", text)
        generator = match.group(1) if match else ""
        try:
            tree = ElementTree.fromstring(text)
        except ElementTree.ParseError:
            continue
        for widget in tree.iter("widget"):
            name = widget.get("name")
            if not name or name in found:
                continue
            source = widget.get("source") or fallback_source(generator, name)
            found[name] = {"file": path.name, "generator": generator, "source": source,
                           "attributes": {key: widget.get(key) for key in SHOWN if widget.get(key) is not None}}
    return found


def catalog(menus: Path = MENUS) -> dict:
    """the menu files' widgets by name (main_menu/...): their file, generator, source and shown attributes;
    read again when a file changes"""
    stamp = max((path.stat().st_mtime for path in menus.glob("*.xml")), default=0.0)
    return _catalog(str(menus), stamp)


def fallback_source(generator: str, name: str) -> str:
    """the source of a widget whose file has none written: ce_menus.py's writer and the PC tag it is made from"""
    if generator == "tools/ce_menus.py":
        return f"tools/ce_menus.py widget_xml ui\\shell\\{name.replace('/', chr(92))}"
    return generator


def menu_name(tag: str) -> str:
    """a widget's tag name (pc\\main_menu\\x) as the menu files name it (main_menu/x)"""
    tag = tag or ""
    if tag.startswith("pc\\"):
        tag = tag[3:]
    return tag.replace("\\", "/")


@functools.lru_cache(maxsize=16)
def _functions(path: str, stamp: float) -> dict:
    """a Python file's functions' lines by name, nested ones as outer.inner"""
    lines = {}
    try:
        tree = ast.parse(Path(path).read_text(errors="replace"))
    except (OSError, SyntaxError):
        return lines

    def walk(node, prefix):
        for child in ast.iter_child_nodes(node):
            if isinstance(child, (ast.FunctionDef, ast.AsyncFunctionDef)):
                name = f"{prefix}{child.name}"
                lines.setdefault(name, child.lineno)
                lines.setdefault(child.name, child.lineno)
                walk(child, name + ".")
            elif isinstance(child, ast.ClassDef):
                walk(child, f"{prefix}{child.name}.")

    walk(tree, "")
    return lines


def locate(source: str) -> dict:
    """a source attribute as places: the file, its call path ("multiplayer_files/_browser/_prompts"), each
    function's line, the innermost's "file:line" first, and the PC tag a ce_menus.py widget is made of"""
    source = source or ""
    file, _, rest = source.partition(" ")
    place = {"file": file, "calls": [], "lines": {}, "where": file, "tag": ""}
    if not file:
        return place
    path = ROOT / file
    functions = _functions(str(path), path.stat().st_mtime) if path.is_file() else {}
    if file.endswith("ce_menus.py"):
        function, _, tag = rest.partition(" ")
        place["calls"] = [function] if function else []
        place["tag"] = tag.split(" + ")[0]
    else:
        place["calls"] = [call for call in rest.split("/") if call]
    for call in place["calls"]:
        if call in functions:
            place["lines"][call] = functions[call]
    if place["calls"]:
        inner = place["calls"][-1]
        line = place["lines"].get(inner)
        place["where"] = f"{file}:{line} {inner}" if line else f"{file} {inner}"
    return place


def describe(source: str) -> str:
    """a source as a line for a brief: "tools/port_settings.py:4532 _prompts (multiplayer_files > _browser >
    _prompts)", and a ce_menus.py widget's PC tag"""
    place = locate(source)
    if not place["file"]:
        return "unknown"
    text = place["where"]
    if len(place["calls"]) > 1:
        text += " (" + " > ".join(f"{call}:{place['lines'][call]}" if call in place["lines"] else call
                                   for call in place["calls"]) + ")"
    if place["tag"]:
        text += f", from the PC tag {place['tag']}"
    return text


def enrich_layout(layout: dict, widgets: dict = None) -> dict:
    """a layout (menu_layout.c's JSON) with each widget's source and file, from the menu files"""
    widgets = widgets if widgets is not None else catalog()
    for item in layout.get("items", []):
        if item.get("kind") == "widget":
            found = widgets.get(menu_name(item.get("name", "")))
            if found:
                item["source"] = found["source"]
                item["file"] = found["file"]
    return layout
