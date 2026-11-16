#!/usr/bin/env python3
"""Writes the PC version's menus (Halo Custom Edition's) as the port's menu
files (port/assets/menus/ce, read by port/linux/src/menu_files.c):

    python tools/ce_menus.py --tags .../halopc-restored/tags \\
        --definitions .../invader/src/tag/hek/definition \\
        --redraws .../ui-svg-handmade

Every widget reachable from the PC main menu (ui\\shell\\main_menu\\main_menu:
its children, the widgets its handlers open, their conditional widgets and
list descriptions, and so on) is written as a <widget>, laid out as the PC
version has it, with its string lists as <strings> and its bitmaps as
<bitmap>s. Our widgets are named by their tag's path below ui\\shell, with
forward slashes (main_menu/main_menu); the root is the main menu.

The bitmaps are drawn from the high-res redraws (--redraws, hand-made SVGs
in the PC bitmaps' layout), copied to port/assets/menus/svg, at up to 4
times their size. The pictures with no redraw (renders, screenshots, logos)
are the Xbox map's bitmap of the same name, where it has the same pictures
in the same order (XBOX_BITMAPS: the player's own ui.map draws them, and
nothing is shipped), else nothing: the frame is drawn empty, so that a
screen shows no picture there rather than a stand-in's words;
port/assets/menus/NON_HANDDRAWN.md lists both, the empty frames as ones to
be redrawn.

What this engine (the Xbox's) has no use for is written but never runs: the
PC version's mouse events (the port's own mouse support clicks with A), and
its own functions (port/linux/game/menu_functions.c, most of which do
nothing yet). Its gamespy and ticker fonts are drawn with small_ui, which
the Xbox's map has.

Needs rsvg-convert, Pillow, NumPy and SciPy; port/assets/menus/menus.json,
which tools/embed_assets.py embeds the files from, is written too.
"""

import argparse
import io
import json
import shutil
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Optional
from xml.sax.saxutils import quoteattr

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from hud_assets import bleed, render_svg  # noqa: E402
import menu_behaviour  # noqa: E402
import menu_look  # noqa: E402
import menu_nodes  # noqa: E402
import port_settings  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port/assets/menus"
CE = MENUS / "ce"
SVG_COPIES = MENUS / "svg"
MAIN_MENU = "ui\\shell\\main_menu\\main_menu"
TITLE_FONT = ROOT / "port/assets/fonts/OpenCE-Regular.ttf"
# the widgets left out, with their pictures (and all they alone lead to):
# the server browser's GameSpy logo, which the port has no use for, and the
# main menu's Credits, which the Xbox's game has not
LEFT_OUT = {
    "ui\\shell\\main_menu\\multiplayer_type_select\\join_game\\header_gamespy_logo",
    "ui\\shell\\main_menu\\main_menu_item_credits",
    # (the main menu's Profile, whose profile manager Settings' PROFILES tab
    # opens: EXTRA_ROOTS)
    "ui\\shell\\main_menu\\main_menu_item_profile",
    # (the main menu's build number, the Xbox build the sources come from,
    # not this one's: Settings > Profiles' ABOUT shows both, menu_functions.c's
    # about_panel)
    "ui\\shell\\main_menu\\build_number",
    # (the "checking for updates" dialogs, which asked the PC version's
    # servers: Create > Internet and Join > Server Browser skip them,
    # port_settings.WIDGET_PATCHES)
    "ui\\shell\\main_menu\\multiplayer_type_select\\checking_updates_screen_create",
    "ui\\shell\\main_menu\\multiplayer_type_select\\checking_updates_screen_join",
    # (the server browser's Filters' PING, which is not known before a game
    # is joined (the browser does not ping the master servers' servers), and
    # TEAMPLAY OPTIONS, which the port's Filters leave to GAME TYPE)
    "ui\\shell\\main_menu\\multiplayer_type_select\\join_game\\filters\\op_ping",
    "ui\\shell\\main_menu\\multiplayer_type_select\\join_game\\filters\\op_game_options",
}
# the widgets written though the main menu leads to them no longer: the
# profile manager, which Settings' PROFILES tab opens
# (port_settings.PROFILE_MANAGER)
EXTRA_ROOTS = ["ui\\shell\\" + port_settings.PROFILE_MANAGER.replace("/", "\\")]
# children moved: (parent, child) to (x, y); the main menu's Settings and
# Quit a row up each, where Profile and Settings were; Multiplayer's Edit
# Gametypes and divider below Co-op, keeping its 32-unit highlight inside the description panel's
# bottom curve (port_settings.WIDGET_PATCHES)
CHILD_OFFSETS = {
    # the server browser's source tab (ALL GAMES, DEDICATED, P2P GAMES), 20
    # units wider than CE's for its words (port_settings.WIDGET_PATCHES): 9
    # units left, so that its words and arrows are centred in the tab the
    # screen's border picture outlines (its own box is gone)
    ("ui\\shell\\main_menu\\multiplayer_type_select\\join_game\\op_browser_mode",
     "ui\\shell\\main_menu\\multiplayer_type_select\\join_game\\browser_mode_spinner"): (16, 1),
    # Player Options' rows 27 apart, not 30, so that the host's AIM ASSIST and
    # SHIELD EFFECTS fit below them and above the help, which some fill with
    # three lines (port_settings.WIDGET_PATCHES)
    **{("ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\player_options_edit\\player_options_menu",
        f"ui\\shell\\main_menu\\settings_select\\multiplayer_setup\\player_options_edit\\op_{row}"): (54, 73 + 27 * index)
       for index, row in enumerate(port_settings.PLAYER_OPTIONS_ROWS)},
    # Capture the Flag's and King of the Hill's TIME LIMIT (King of the Hill's
    # list has Capture the Flag's) below the rows the port puts before it
    # (port_settings.GAMETYPE_RULES_EXTRAS)
    **{("ui\\shell\\" + f"{folder}/{screen['list']}".replace("/", "\\"),
        "ui\\shell\\" + f"{port_settings.CTF_EDIT}/op_time_limit".replace("/", "\\")):
       (54, screen["y"] + port_settings.GAMETYPE_RULES_STEP * len(screen["rows"]))
       for folder, screen in port_settings.GAMETYPE_RULES_EXTRAS.items()},
    ("ui\\shell\\main_menu\\main_menu_select_list", "ui\\shell\\main_menu\\main_menu_item_settings"):
        port_settings.SETTINGS_ITEM_PLACE,
    ("ui\\shell\\main_menu\\main_menu_select_list", "ui\\shell\\main_menu\\main_menu_item_quit_game"):
        port_settings.QUIT_ITEM_PLACE,
    ("ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_list",
     "ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_gametypes_item"):
        port_settings.MULTIPLAYER_GAMETYPES_PLACE,
    ("ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_screen",
     "ui\\shell\\main_menu\\blueline"): port_settings.MULTIPLAYER_LINE_PLACE,
    # (the rest of Multiplayer's rows, a step closer for Create Game's two
    # co-op items)
    **{("ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_type_select_list",
        f"ui\\shell\\main_menu\\multiplayer_type_select\\{name}"): place
       for name, place in port_settings.MULTIPLAYER_ROWS.items()},
    # the map lists' first row, the SINGLEPLAYER or MULTIPLAYER chooser
    # (port_settings.MAP_KIND_CHOOSER), where the gametype list has its own
    **{(f"ui\\shell\\main_menu\\{list_tag}", "ui\\shell\\main_menu\\new_select\\list_item_0"): (82, 73)
       for list_tag in ("multiplayer_type_select\\mp_map_select\\mp_map_select_list_2",
                        "solo_level_select\\solo_level_select_list")},
}
SCALE = 4
MAXIMUM_SIZE = 2048
# the PNG files there were before this run, by path: a picture drawn the same
# as before keeps its file, whose bytes zlib's builds (zlib-ng's differ on
# x86-64 and AArch64) would write differently (save_png)
PREVIOUS = {}


def save_png(image: Image.Image, path: Path) -> None:
    """the picture saved as a PNG file, or the file it had before where its
    pixels are the same"""
    previous = PREVIOUS.get(path) or (path.read_bytes() if path.is_file() else None)
    if previous:
        with Image.open(io.BytesIO(previous)) as before:
            if before.size == image.size and before.convert("RGBA").tobytes() == image.convert("RGBA").tobytes():
                path.write_bytes(previous)
                return
    image.save(path, optimize=True)


def rounded_still(png: str, width: int, height: int, rim: Optional[str]) -> str:
    """a still (port/assets/menus/stills) with its corners cut round, the
    rim drawn round it in rim's colour if there is one, as ce/port/stills'
    file of the same name (whose name it returns); drawn SCALE times larger
    and reduced, for smooth edges"""
    from PIL import ImageChops, ImageDraw
    out = f"ce/port/stills/{Path(png).stem}{'' if rim is None else '_framed'}.png"
    image = Image.open(MENUS / png).convert("RGBA")
    # (a whole number of pixels to the unit, as the menus read a frame: the
    # picture scaled to the nearest whole multiple of its size in the menu)
    multiple = max(1, round(image.width / width))
    if image.size != (width * multiple, height * multiple):
        image = image.resize((width * multiple, height * multiple), Image.LANCZOS)
    units = image.width / width
    big = (image.width * SCALE, image.height * SCALE)
    radius = port_settings.SETTINGS_STILL_RADIUS * units * SCALE
    mask = Image.new("L", big, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big[0] - 1, big[1] - 1), radius, fill=255)
    mask = mask.resize(image.size, Image.LANCZOS)
    image.putalpha(ImageChops.multiply(image.getchannel("A"), mask))
    if rim is not None:
        layer = Image.new("RGBA", big, (0, 0, 0, 0))
        ImageDraw.Draw(layer).rounded_rectangle((0, 0, big[0] - 1, big[1] - 1), radius, outline=rim,
                                                width=round(port_settings.SETTINGS_STILL_RIM * units * SCALE))
        image = Image.alpha_composite(image, layer.resize(image.size, Image.LANCZOS))
    (MENUS / out).parent.mkdir(parents=True, exist_ok=True)
    save_png(image, MENUS / out)
    return out


def titles_font(size: int):
    """the titles' font: laid out by FreeType alone, as every Pillow can (a
    Pillow with Raqm (and FriBiDi) lays it out with other advances)"""
    from PIL import ImageFont
    return ImageFont.truetype(str(TITLE_FONT), size, layout_engine=ImageFont.Layout.BASIC)

# the PC fonts this engine's map lacks, and the font drawn instead
FONTS = {"ui\\gamespy": "ui\\small_ui", "ui\\ticker": "ui\\small_ui"}

# the XML's names of the tags' values (menu_tags.c has the same)
EVENTS = ["a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger", "up", "down", "left", "right",
          "start", "back", "left_thumb", "right_thumb", "stick_up", "stick_down", "stick_left", "stick_right",
          "right_stick_up", "right_stick_down", "right_stick_left", "right_stick_right", "created", "deleted",
          "get_focus", "lose_focus", "left_mouse", "middle_mouse", "right_mouse", "double_click",
          "custom_activation", "post_render"]
WIDGET_FLAGS = ["pass_unhandled_to_focused_child", "pause_game", "flash_bitmap", "up_down_tabs_children",
                "left_right_tabs_children", "up_down_tabs_items", "left_right_tabs_items", "no_focused_child",
                "pass_unhandled_to_all_children", "render_any_controller", "pass_handled_to_all_children",
                "main_menu_if_no_history", "tag_controller_index", "nifty_fx", "no_history", "force_handle_mouse",
                "no_widescreen_fill"]
TEXT_FLAGS = ["editable", "password", "flashing", "no_focus_test"]
LIST_FLAGS = ["items_in_code", "items_from_strings", "one_tooltip", "single_preview"]
TYPES = ["container", "text", "spinner", "column_list"]
ALIGN = ["left", "right", "center"]
REPLACE = [None, "controller", "build_number", "pid"]

# the PC bitmaps with no redraw whose pictures the Xbox's ui.map has, under
# the same name and in the same order: drawn from the player's map. (The
# Xbox's mp_map_grafix has the 13 Xbox maps' pictures, as the PC version's
# first 13, and its "?" after them; the PC version's other 8 are its own
# maps', which the Xbox's game has not.)
XBOX_BITMAPS = {
    "ui\\shell\\bitmaps\\colors_sm", "ui\\shell\\bitmaps\\mp_map_grafix", "ui\\shell\\bitmaps\\sp_levels",
    "ui\\shell\\main_menu\\difficulty_select\\difficulty_options",
    "ui\\shell\\main_menu\\difficulty_select\\difficulty_options_small", "ui\\shell\\main_menu\\halo_logo",
    "ui\\shell\\main_menu\\settings_select\\player_setup\\player_profile_edit\\color_edit\\player_color_marine_large",
}
# the frames with no redraw that are drawn from a frame of the Xbox map's
# bitmap of the same name, whose other frames differ (<frame map=...>):
# the same picture, or for the profile settings the Xbox's for the same
# thing (Change Name; its Controller Setup for Gamepads)
XBOX_FRAMES = {
    "shell/main_menu/multiplayer_type_select/mp_options__2": 3,
    "shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__2": 2,
    "shell/main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_options__5": 4,
    "shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__0": 0,
    "shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__2": 1,
    "shell/main_menu/settings_select/player_setup/player_profile_edit/profile_options__7": 3,
}

# the Xbox's functions that the PC version rewrote for its own widgets (its
# column lists where the Xbox had spinners, and other children), which stop
# this engine on the PC widgets: written as "unwired <name>", which does
# nothing (UNWIRED.md)
INCOMPATIBLE = set()
# the PC version's own functions that menu_functions.c does what they do
# (and its campaign's: the Xbox's of those names, which take the Xbox's
# widgets, menu_functions.c's for ours)
WIRED = {
    "main menu quit game", "profile set edit begin", "mouse emit accept event", "mouse emit back event",
    "mouse emit x event", "emit custom activation event", "single prev cl item activated", "controls back handler",
    "gamespy back handler", "gamespy dismiss error", "gamespy dismiss filters", "gamespy screen init",
    "gamespy screen dispose", "mp type set mode",
    # (the server browser's: menu_functions.c's lobby_browser_*)
    "gamespy select header", "gamespy select button", "gamespy select item", "gamespy screen update",
    "gamespy update filter settings",
    "campaign menu init", "campaign menu continue", "difficulty item select", "solo map list update",
    "load game menu init", "load game menu dispose", "load game menu activated", "load game list update",
    "load game menu delete request", "load game menu delete finish",
    "controls screen init", "controls begin binding", "controls screen change set", "controls screen defaults",
    "controls update menu", "profile manager select", "direct ip connect go", "ss edit server password",
    "gamespy select header",
    # (Create Game's map and gametype lists, Server Setup, and the gametype
    # editor's teamplay and vehicle options: menu_functions.c)
    "mp map list update", "gt select list update", "gt edit list update", "server settings init",
    "server settings update", "ss edit server name", "ss start game", "mp prof init teamplay options",
    "mp prof save teamplay options", "mp prof init vehicle options", "mp prof save vehicle options",
    "mp prof vehicles update",
}

# ---------- HEK tags


class Tags:
    """HEK tags (big-endian), read with invader's definitions"""

    BASIC = {"int8": 1, "uint8": 1, "int16": 2, "uint16": 2, "int32": 4, "uint32": 4, "float": 4, "Fraction": 4,
             "Angle": 4, "TagFourCC": 4, "TagID": 4, "Pointer": 4, "Index": 2, "Point2D": 8, "Point2DInt": 4,
             "Rectangle2D": 8, "ColorARGB": 16, "ColorRGB": 12, "TagString": 32, "TagDependency": 16,
             "TagReflexive": 12, "TagDataOffset": 20}
    STRUCTS = {"ui_widget_definition": "UIWidgetDefinition", "bitmap": "Bitmap",
               "unicode_string_list": "UnicodeStringList"}

    def __init__(self, tags: Path, definitions: Path):
        self.root = tags
        self.types = {}
        for path in sorted(definitions.glob("*.json")):
            for item in json.loads(path.read_text()):
                self.types.setdefault(item["name"], item)

    def size(self, field: dict) -> int:
        kind = field["type"]
        if kind == "pad":
            return field["size"]
        if kind in self.BASIC:
            return self.BASIC[kind] * field.get("count", 1)
        definition = self.types[kind]
        if definition["type"] == "enum":
            return 2
        if definition["type"] == "bitfield":
            return definition["width"] // 8
        return definition["size"] * field.get("count", 1)

    def options(self, name: str) -> list:
        return [o if isinstance(o, str) else o["name"] for o in self.types[name]["options"]]

    def parse(self, name: str, data: bytes, base: int, cursor: list) -> dict:
        """the struct at base; what it points to follows from cursor[0], in
        the order of its fields"""
        result, position, later = {}, base, []
        for field in self.types[name]["fields"]:
            kind, key, size = field["type"], field.get("name"), self.size(field)
            raw = data[position:position + size]
            position += size
            if kind == "pad" or not key:
                continue
            if kind == "TagString":
                result[key] = raw.split(b"\0")[0].decode("latin-1")
            elif kind == "TagDependency":
                _, _, length, _ = struct.unpack(">4sIiI", raw)
                result[key] = None
                later.append(("dependency", key, length, None))
            elif kind == "TagReflexive":
                count = struct.unpack(">i", raw[:4])[0]
                result[key] = []
                later.append(("block", key, count, field["struct"]))
            elif kind == "TagDataOffset":
                result[key] = b""
                later.append(("data", key, struct.unpack(">i", raw[:4])[0], None))
            elif kind == "Rectangle2D":
                result[key] = struct.unpack(">4h", raw)
            elif kind == "ColorARGB":
                result[key] = struct.unpack(">4f", raw)
            elif kind in ("float", "Fraction", "Angle"):
                result[key] = struct.unpack(">f", raw)[0]
            elif size == 2:
                unsigned = kind == "uint16" or self.types.get(kind, {}).get("type") == "bitfield"
                result[key] = struct.unpack(">H" if unsigned else ">h", raw)[0]
            elif size == 4:
                unsigned = kind == "uint32" or self.types.get(kind, {}).get("type") == "bitfield"
                result[key] = struct.unpack(">I" if unsigned else ">i", raw)[0]
            else:
                result[key] = raw
        for kind, key, count, element in later:
            if kind == "dependency":
                if count > 0:
                    result[key] = data[cursor[0]:cursor[0] + count].decode("latin-1")
                    cursor[0] += count + 1
            elif kind == "data":
                result[key] = data[cursor[0]:cursor[0] + count]
                cursor[0] += count
            else:
                element_size = self.types[element]["size"]
                start = cursor[0]
                cursor[0] += element_size * count
                result[key] = [self.parse(element, data, start + index * element_size, cursor)
                               for index in range(count)]
        return result

    def path(self, tag: str, group: str) -> Path:
        return self.root / (tag.replace("\\", "/") + "." + group)

    def load(self, tag: str, group: str) -> dict:
        data = self.path(tag, group).read_bytes()
        name = self.STRUCTS[group]
        return self.parse(name, data, 64, [64 + self.types[name]["size"]])


# ---------- names


def our_name(tag: str) -> str:
    """a tag's name in our files: its path below ui\\shell, with slashes"""
    for prefix in ("ui\\shell\\", "ui\\"):
        if tag.startswith(prefix):
            return tag[len(prefix):].replace("\\", "/")
    return tag.replace("\\", "/")


def attributes(pairs: list) -> str:
    return "".join(f" {key}={quoteattr(str(value))}" for key, value in pairs if value is not None)


def text_value(utf16: bytes) -> str:
    text = utf16.decode("utf-16-le").split("\0")[0]
    return text.replace("\r\n", "\\n").replace("\n", "\\n").replace("\r", "")


def strings_xml(lists: dict) -> str:
    """strings.xml: the PC version's string lists (each tag's strings), with
    the port's changes to them (port_settings.STRING_OVERRIDES and
    STRING_INSERTS). Each list names its tag (map=) and each of its strings
    that is the PC version's its index there (map=), which a map in another
    language has in its language (menu_tags.c's map_strings)."""
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- The PC version's menus' text (tools/ce_menus.py) -->", "<menus>"]
    for tag in sorted(lists):
        strings = lists[tag]
        originals = list(range(len(strings)))
        if our_name(tag) in port_settings.STRING_OVERRIDES:
            strings = port_settings.STRING_OVERRIDES[our_name(tag)]
            originals = port_settings.OVERRIDE_ORIGINALS.get(our_name(tag), [None] * len(strings))
        for index, texts in port_settings.STRING_INSERTS.get(our_name(tag), []):
            strings = strings[:index] + texts + strings[index:]
            originals = originals[:index] + [None] * len(texts) + originals[index:]
        lines.append(f"\t<strings{attributes([('name', our_name(tag)), ('map', tag)])}>")
        lines += [f"\t\t<string{attributes([('text', text), ('map', original)])}/>"
                  for text, original in zip(strings, originals)]
        lines.append("\t</strings>")
    lines.append("</menus>")
    return "\n".join(lines) + "\n"


def flag_names(value: int, names: list) -> str:
    return " ".join(name for bit, name in enumerate(names) if value >> bit & 1) or None


def bounds_text(bounds: tuple) -> str:
    return " ".join(str(value) for value in bounds)


# ---------- the art


class Art:
    def __init__(self, tags: Tags, redraws: Path):
        self.tags, self.redraws = tags, redraws
        self.empty = []  # (frame, size, PC picture) of the frames drawn empty, with no redraw
        self.xbox_frames = []  # (PC frame, Xbox bitmap, its frame) drawn from the Xbox map
        self.added_frames = []  # (our bitmap, Xbox bitmap, its frame): port_settings.BITMAP_FRAMES
        self.svgs = []
        self.pngs = []

    def title(self, name: str, text: str) -> str:
        """a <bitmap> of a title this port has that the PC version has not:
        the text set in OpenCE (the public-domain Newtown respaced, as the
        port's titles are: tools/title_assets.py) over the PC headers'
        backdrop"""
        from PIL import ImageDraw
        scale = SCALE
        probe = titles_font(1000)
        left, top, right, bottom = probe.getbbox("H")
        title_font = titles_font(round(port_settings.TITLE_CAP * scale * 1000 / (bottom - top)))
        width = title_font.getlength(text) / scale
        svg = MENUS / "port_svg" / f"{name}.svg"
        svg.parent.mkdir(parents=True, exist_ok=True)
        svg.write_text(port_settings.title_backdrop(width))
        image = Image.fromarray(render_svg(svg, scale), "RGBA")
        draw = ImageDraw.Draw(image)
        cap_top = title_font.getbbox("H")[1]
        draw.text((port_settings.TITLE_LEFT * scale, port_settings.TITLE_TOP * scale - cap_top), text,
                  font=title_font, fill=port_settings.TITLE_COLOR)
        png = f"ce/port/{name}.png"
        (MENUS / png).parent.mkdir(parents=True, exist_ok=True)
        save_png(image, MENUS / png)
        self.pngs.append(png)
        return "\n".join([f"\t<bitmap{attributes([('name', name)])}>",
                          f"\t\t<frame{attributes([('png', png), ('width', 512), ('height', 64)])}/>",
                          "\t</bitmap>"])

    def still(self, name: str, png, width: int, height: int, rim: Optional[str] = None) -> str:
        """a <bitmap> of a picture shipped as it is (port/assets/menus/stills:
        NON_HANDDRAWN.md says what each is), its corners cut round as the
        panel's frames are (port_settings.SETTINGS_STILL_RADIUS), and with
        rim its frame drawn in that colour, into ce/port/stills"""
        pngs = [rounded_still(frame, width, height, rim) for frame in ([png] if isinstance(png, str) else png)]
        self.pngs += [frame for frame in dict.fromkeys(pngs) if frame not in self.pngs]
        return "\n".join([f"\t<bitmap{attributes([('name', name)])}>",
                          *(f"\t\t<frame{attributes([('png', frame), ('width', width), ('height', height)])}/>"
                            for frame in pngs),
                          "\t</bitmap>"])

    def digits(self, name: str) -> str:
        """a <bitmap> of the ten digits, a frame each, set as the titles are
        (OpenCE, the titles' colour) at port_settings.DIGIT_CAP units high,
        each in the middle of a frame of one width: Multiplayer's panel's
        counts (menu_functions.c's multiplayer_status_number). Smaller where
        the widest digit would not fit the frame's width with a margin of
        DIGIT_MARGIN either side (OpenCE's 6 and 9 at the cap's height are
        wider than the frame: their sides were cut, #519)"""
        from PIL import ImageDraw
        scale = SCALE
        width, height = port_settings.DIGIT_SIZE
        probe = titles_font(1000)
        left, top, right, bottom = probe.getbbox("0")
        widest = max(probe.getbbox(str(digit))[2] - probe.getbbox(str(digit))[0] for digit in range(10))
        size = min(port_settings.DIGIT_CAP * scale * 1000 / (bottom - top),
                   (width - 2 * port_settings.DIGIT_MARGIN) * scale * 1000 / widest)
        digit_font = titles_font(int(size))
        lines = [f"\t<bitmap{attributes([('name', name)])}>"]
        for digit in range(10):
            image = Image.new("RGBA", (width * scale, height * scale), (0, 0, 0, 0))
            draw = ImageDraw.Draw(image)
            box = digit_font.getbbox(str(digit))
            x = (width * scale - (box[2] - box[0])) / 2 - box[0]
            y = (height * scale - (box[3] - box[1])) / 2 - box[1]
            draw.text((x, y), str(digit), font=digit_font, fill=port_settings.DIGIT_COLOR)
            png = f"ce/port/{name}__{digit}.png"
            (MENUS / png).parent.mkdir(parents=True, exist_ok=True)
            save_png(image, MENUS / png)
            self.pngs.append(png)
            lines.append(f"\t\t<frame{attributes([('png', png), ('width', width), ('height', height)])}/>")
        lines.append("\t</bitmap>")
        return "\n".join(lines)

    def port_picture(self, name: str, frames: list, keep: bool = False) -> str:
        """a <bitmap> of this port's own drawing: each frame an SVG (text,
        width, height) written to port_svg/ and drawn (keep: not drawn again
        where its SVG and picture are there already, unchanged)"""
        lines = [f"\t<bitmap{attributes([('name', name)])}>"]
        for index, (text, width, height) in enumerate(frames):
            svg = MENUS / "port_svg" / f"{name}__{index}.svg"
            png = f"ce/port/{name}__{index}.png"
            unchanged = keep and svg.is_file() and svg.read_text() == text and (MENUS / png).is_file()
            svg.parent.mkdir(parents=True, exist_ok=True)
            svg.write_text(text)
            (MENUS / png).parent.mkdir(parents=True, exist_ok=True)
            if not unchanged:
                save_png(Image.fromarray(render_svg(svg, SCALE), "RGBA"), MENUS / png)
            self.pngs.append(png)
            lines.append(f"\t\t<frame{attributes([('png', png), ('width', width), ('height', height)])}/>")
        lines.append("\t</bitmap>")
        return "\n".join(lines)

    def frame_sources(self, relative: str, index: int, count: int) -> list:
        return [relative, f"{relative}__0"] if count == 1 else [f"{relative}__{index}"]

    def bitmap(self, tag: str) -> str:
        """a <bitmap> for the bitmap tag, its frames drawn"""
        group = self.tags.load(tag, "bitmap")
        data, sequences = group["bitmap data"], group["bitmap group sequence"]
        relative = tag.replace("\\", "/")[3:]
        if sequences and sequences[0]["bitmap count"]:
            first = sequences[0]["first bitmap index"]
            indices = list(range(first, first + sequences[0]["bitmap count"]))
        elif sequences and sequences[0]["sprites"]:
            sys.exit(f"{tag}: sprites are not supported")
        else:
            indices = list(range(len(data)))
        lines = [f"\t<bitmap{attributes([('name', our_name(tag))])}>"]
        for index in indices:
            width, height = data[index]["width"], data[index]["height"]
            name = self.frame_sources(relative, index, len(data))[-1]
            if name in XBOX_FRAMES and not any((self.redraws / f"{source}.svg").is_file()
                                               for source in self.frame_sources(relative, index, len(data))):
                self.xbox_frames.append((name, tag, XBOX_FRAMES[name]))
                lines.append(f"\t\t<frame{attributes([('map', tag), ('index', XBOX_FRAMES[name])])}/>")
                continue
            png = f"ce/{relative}__{index}.png"
            self.draw(relative, index, len(data), width, height, MENUS / png)
            self.pngs.append(png)
            lines.append(f"\t\t<frame{attributes([('png', png), ('width', width), ('height', height)])}/>")
        # (frames of the port's items: tools/port_settings.py)
        lines += [f"\t\t{line}" for line in port_settings.FRAMES_ADDED.get(our_name(tag), [])]
        # (the port's frames after them: the Xbox map's, scaled)
        for source, index, width, height, x, y in port_settings.BITMAP_FRAMES.get(our_name(tag), []):
            self.added_frames.append((our_name(tag), source, index))
            lines.append(f"\t\t<frame{attributes([('map', source), ('index', index), ('width', width), ('height', height), ('x', x or None), ('y', y or None)])}/>")
        lines.append("\t</bitmap>")
        return "\n".join(lines)

    def draw(self, relative: str, index: int, count: int, width: int, height: int, output: Path) -> None:
        output.parent.mkdir(parents=True, exist_ok=True)
        for name in self.frame_sources(relative, index, count):
            svg = self.redraws / f"{name}.svg"
            if svg.is_file():
                copy = SVG_COPIES / f"{name}.svg"
                copy.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(svg, copy)
                self.svgs.append(copy)
                scale = max(1, min(SCALE, MAXIMUM_SIZE // max(width, height)))
                image = render_svg(copy, scale)
                if image.shape[:2] != (height * scale, width * scale):
                    sys.exit(f"{svg}: {image.shape[1]}x{image.shape[0]}, not {scale}x {width}x{height}")
                save_png(Image.fromarray(bleed(image), "RGBA"), output)
                return
        # (no redraw: the frame empty, at its size, until it is redrawn; a
        # stand-in picture would show players its words)
        name = self.frame_sources(relative, index, count)[-1]
        save_png(Image.new("RGBA", (width, height), (0, 0, 0, 0)), output)
        self.empty.append((output.relative_to(MENUS).as_posix(), width, height, f"{name}.png"))


# ---------- the widgets


def reachable(tags: Tags) -> dict:
    widgets, queue = {}, [*EXTRA_ROOTS, MAIN_MENU]
    while queue:
        tag = queue.pop()
        if tag in widgets:
            continue
        widget = tags.load(tag, "ui_widget_definition")
        widgets[tag] = widget
        for reference in ([child["widget tag"] for child in widget["child widgets"]] +
                          [handler["widget tag"] for handler in widget["event handlers"]] +
                          [conditional["widget tag"] for conditional in widget["conditional widgets"]] +
                          [widget["extended description widget"]]):
            if reference and reference not in LEFT_OUT:
                queue.append(reference)
    return widgets


def bitmap_name(tag: str) -> str:
    """a bitmap: the Xbox map's (its path) for those it has, else ours"""
    return tag if tag in XBOX_BITMAPS else our_name(tag)


def widget_xml(tag: str, widget: dict, tags: Tags, functions: list, inputs: list, fonts: Counter,
               children_of: dict, depth: int = 1) -> list:
    """the <widget>, and its children's where it is their only parent"""
    indent = "\t" * depth
    bounds = widget["bounds"]
    top, left, bottom, right = bounds
    pairs = [
        ("name", our_name(tag)),
        ("type", TYPES[widget["widget type"]] if widget["widget type"] else None),
        ("controller", None if widget["controller index"] == 4 else str(widget["controller index"] + 1)),
        ("left", left or None), ("top", top or None), ("width", right - left), ("height", bottom - top),
        ("flags", flag_names(widget["flags"], WIDGET_FLAGS)),
        ("auto_close", widget["milliseconds to auto close"] or None),
        ("auto_close_fade", widget["milliseconds auto close fade time"] or None),
        ("bitmap", bitmap_name(widget["background bitmap"]) if widget["background bitmap"] else None),
    ]
    if widget["text label unicode strings list"]:
        pairs.append(("string_list", our_name(widget["text label unicode strings list"])))
        pairs.append(("string_index", widget["string list index"] or None))
    if widget["text font"]:
        font = FONTS.get(widget["text font"], widget["text font"])
        fonts[widget["text font"]] += 1
        pairs.append(("font", font))
    alpha, red, green, blue = widget["text color"]
    if (alpha, red, green, blue) != (0, 0, 0, 0):
        pairs.append(("color", "#%02X%02X%02X%02X" % tuple(round(c * 255) for c in (alpha, red, green, blue))))
    pairs += [
        ("align", ALIGN[widget["justification"]] if widget["justification"] else None),
        ("text_flags", flag_names(widget["flags 1"], TEXT_FLAGS)),
        ("text_x", widget["horiz offset"] or None), ("text_y", widget["vert offset"] or None),
        ("list_flags", flag_names(widget["flags 2"], LIST_FLAGS)),
        ("header_bitmap", bitmap_name(widget["list header bitmap"]) if widget["list header bitmap"] else None),
        ("footer_bitmap", bitmap_name(widget["list footer bitmap"]) if widget["list footer bitmap"] else None),
        ("header_bounds", bounds_text(widget["header bounds"]) if any(widget["header bounds"]) else None),
        ("footer_bounds", bounds_text(widget["footer bounds"]) if any(widget["footer bounds"]) else None),
        ("description", our_name(widget["extended description widget"])
         if widget["extended description widget"] else None),
    ]
    patch = port_settings.WIDGET_PATCHES.get(our_name(tag), {})
    # (a value the tag has is changed where it is, one it has not is added
    # after the others)
    for key, value in patch.get("set", {}).items():
        if any(k == key and v is not None for k, v in pairs):
            pairs = [(k, value if k == key else v) for k, v in pairs]
        else:
            pairs = [(k, v) for k, v in pairs if k != key] + [(key, value)]
    # (where it comes from, which the game ignores and the menu review tools
    # read: tools/menu_review.py; the PC version's tag, and the port's patch)
    pairs.append(("source", f"tools/ce_menus.py widget_xml {tag}" +
                  (" + port_settings.WIDGET_PATCHES" if patch else "")))
    lines = [f"{indent}<widget{attributes(pairs)}>"]
    inner = indent + "\t"
    for data in widget["game data inputs"]:
        name = inputs[data["function"]]
        lines.append(f"{inner}<data{attributes([('input', f'unwired {name}' if name in INCOMPATIBLE else name)])}/>")
    lines += [f"{inner}<data{attributes([('input', name)])}/>" for name in patch.get("inputs", [])]
    lines += [f"{inner}{line}" for line in patch.get("handlers", [])]
    for handler in widget["event handlers"] if "handlers" not in patch else []:
        flags = handler["flags"]
        target = our_name(handler["widget tag"]) if handler["widget tag"] else None
        handler_pairs = [("event", EVENTS[handler["event type"]])]
        if flags >> 7 & 1:
            name = functions[handler["function"]]
            handler_pairs.append(("run", f"unwired {name}" if name in INCOMPATIBLE else name))
        if flags >> 10 & 1:
            handler_pairs.append(("script", handler["script"]))
        elif handler["script"]:
            handler_pairs.append(("label", handler["script"]))
        for bit, key in ((3, "open"), (8, "replace"), (6, "focus")):
            if flags >> bit & 1:
                handler_pairs.append((key, target))
        if flags >> 1 & 1:
            handler_pairs += [("close", "other"), ("widget", target)]
        if flags >> 5 & 1:
            handler_pairs += [("reload", "other"), ("widget", target)]
        for bit, pair in ((0, ("close", "current")), (2, ("close", "all")), (4, ("reload", "self")),
                          (9, ("back", "true")), (11, ("branch", "true"))):
            if flags >> bit & 1:
                handler_pairs.append(pair)
        if handler["sound effect"]:
            handler_pairs.append(("sound", handler["sound effect"]))
        seen, unique = set(), []
        for key, value in handler_pairs:
            if key not in seen:
                seen.add(key)
                unique.append((key, value))
        lines.append(f"{inner}<on{attributes(unique)}/>")
    for item in widget["search and replace functions"]:
        function = REPLACE[item["replace function"]] if item["replace function"] < len(REPLACE) else None
        lines.append(f"{inner}<replace{attributes([('search', item['search string']), ('function', function)])}/>")
    for conditional in widget["conditional widgets"]:
        lines.append(f"{inner}<conditional{attributes([('widget', our_name(conditional['widget tag'])), ('if_failed', 'true' if conditional['flags'] & 1 else None)])}/>")
    for child in widget["child widgets"]:
        child_tag = child["widget tag"]
        if child_tag in LEFT_OUT:
            continue
        x, y = CHILD_OFFSETS.get((tag, child_tag), (child["horizontal offset"], child["vertical offset"]))
        name = patch.get("swap", {}).get(our_name(child_tag), our_name(child_tag))
        lines += [f"{inner}{line}" for line in patch.get("insert_before", {}).get(our_name(child_tag), [])]
        pairs = [("widget", name), ("x", x or None), ("y", y or None),
                 ("controller", str(child["custom controller index"] + 1) if child["flags"] & 1 else None),
                 ("platform", port_settings.CHILD_PLATFORMS.get((our_name(tag), name)))]
        lines.append(f"{inner}<child{attributes(pairs)}/>")
    lines += [f"{inner}{line}" for line in patch.get("children", [])]
    lines.append(f"{indent}</widget>")
    return lines


def engine_names(source: Path, start: str, count: int) -> list:
    """the names a table of the game's functions gives them, in order"""
    import re
    text = source.read_text(encoding="latin-1")
    text = text[text.index(start):]
    return re.findall(r'"([^"\\]*)"', text[:text.index("}")])[:count]


def pause_art(art):
    return [art.port_picture(name, frames) for name, frames in port_settings.pause_pictures()]


# the port's own pictures that tools/port_settings.py draws (each list of
# (bitmap name, frames) in the order main writes them into bitmaps.xml): the
# button bars' buttons (in the PC version's bitmaps' place), the pause
# boxes', Settings', then the shared parts and the campaign's
PORT_PICTURE_GROUPS = [
    lambda: port_settings.text_button_pictures(),
    lambda: port_settings.pause_pictures(),
    lambda: port_settings.settings_pictures(),
    lambda: port_settings.parts_pictures() + port_settings.campaign_pictures(),
]


def bitmap_blocks(text: str) -> list:
    """bitmaps.xml's <bitmap> elements, in order: (name, its lines)"""
    blocks, lines = [], text.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("\t<bitmap "):
            name = line.split('name="', 1)[1].split('"', 1)[0]
            end = index
            while lines[end] != "\t</bitmap>":
                end += 1
            blocks.append((name, lines[index:end + 1]))
            index = end + 1
        else:
            index += 1
    return blocks


def port_pictures_only() -> None:
    """the port's own screens (tools/port_settings.py) and pictures written
    again without the PC version's tags: the pictures port_settings.py
    draws (PORT_PICTURE_GROUPS) put in bitmaps.xml in place of what it had
    of them, each group where its first was (as main writes them), the rest
    of it as it was; a frame drawn again only where its SVG changed; files of
    pictures it no longer has removed, and menus.json listing what it has"""
    ports = {**port_settings.settings_files(), **port_settings.multiplayer_files(), **port_settings.campaign_files()}
    for name, lines in ports.items():
        (CE / name).write_text("\n".join(lines))
    # (the model's rows, the widgets' look and behaviour, and the setup screens' nodes: tools/menu_look.py,
    # tools/menu_behaviour.py, tools/menu_nodes.py)
    for name, lines in port_settings.model_files().items():
        (MENUS / name).write_text("\n".join(lines))
    looks = menu_look.write(MENUS) + menu_behaviour.write(MENUS) + menu_nodes.write(MENUS)
    art = Art.__new__(Art)
    art.pngs = []
    blocks = bitmap_blocks((CE / "bitmaps.xml").read_text())
    groups = [group() for group in PORT_PICTURE_GROUPS]
    ours = {name for group in groups for name, _ in group}
    # (and those it drew before that no screen names now: a picture of
    # the same folders as ours, drawn as ours are, that no menu file or
    # source names)
    import re
    folders = {name.rsplit("/", 1)[0] for name in ours}
    named = "\n".join(path.read_text() for path in CE.glob("*.xml") if path.name != "bitmaps.xml")
    named += "\n".join(path.read_text() for path in (ROOT / "port/linux/game").glob("*.[ch]*"))
    for name, lines in blocks:
        frames = [line for line in lines if 'png="' in line]
        if (name not in ours and name.rsplit("/", 1)[0] in folders and frames and
                all(re.search(r'png="ce/port/[^"]*__\d+\.png"', line) for line in frames) and
                f'"{name}"' not in named and name.rsplit("/", 1)[1] not in named):
            ours.add(name)
    old_pngs = {line.split('png="', 1)[1].split('"', 1)[0] for name, lines in blocks if name in ours
                for line in lines if 'png="' in line}
    written, placed = [], [False] * len(groups)
    old_names = [name for name, _ in blocks]
    for name, lines in blocks:
        if name in ours:
            group = next((index for index, members in enumerate(groups) if name in dict(members)), None)
            if group is not None and not placed[group]:
                placed[group] = True
                written += [art.port_picture(member, frames, keep=True) for member, frames in groups[group]]
            continue
        written.append("\n".join(lines))
    for group, done in enumerate(placed):
        if not done:
            written += [art.port_picture(member, frames, keep=True) for member, frames in groups[group]]
    text = (CE / "bitmaps.xml").read_text()
    head = text[:text.index("\t<bitmap ")]
    (CE / "bitmaps.xml").write_text(head + "\n".join(written) + "\n</menus>\n")
    for png in sorted(old_pngs - set(art.pngs)):
        (MENUS / png).unlink(missing_ok=True)
        (MENUS / "port_svg" / (png[len("ce/port/"):-len(".png")] + ".svg")).unlink(missing_ok=True)
    listed = json.loads((MENUS / "menus.json").read_text())
    pngs = sorted((set(name for name in listed["files"] if name.endswith(".png")) - old_pngs) | set(art.pngs))
    xmls = [name for name in listed["files"] if not name.endswith(".png") and not name.startswith(menu_look.LOOK)
            and not name.startswith(menu_behaviour.BEHAVIOUR) and name != menu_nodes.NODES]
    xmls = sorted(xmls + looks)
    # (and a file of the port's screens new to it, before the first listed
    # after it)
    for name in sorted(set(f"ce/{name}" for name in ports) - set(xmls)):
        xmls.insert(next((index for index, listed_name in enumerate(xmls) if listed_name > name), len(xmls)), name)
    listed["files"] = xmls + pngs
    (MENUS / "menus.json").write_text(json.dumps(listed, indent=1) + "\n")
    added = sorted(set(name for name, _ in bitmap_blocks((CE / "bitmaps.xml").read_text())) - set(old_names))
    print(f"{len(ours)} of the port's pictures; added {added or 'none'}; "
          f"{len(old_pngs - set(art.pngs))} frames' files removed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port-pictures", action="store_true",
                        help="only the port's own screens and the pictures tools/port_settings.py draws, without "
                        "the PC version's tags (the rest of bitmaps.xml kept as it is)")
    parser.add_argument("--tags", type=Path)
    parser.add_argument("--definitions", type=Path)
    parser.add_argument("--redraws", type=Path)
    arguments = parser.parse_args()
    if arguments.port_pictures:
        port_pictures_only()
        return
    if not (arguments.tags and arguments.definitions and arguments.redraws):
        parser.error("--tags, --definitions and --redraws are needed (or --port-pictures)")
    tags = Tags(arguments.tags, arguments.definitions)
    # (the names of the Xbox's own as the game has them, which the tags' own
    # names give a little differently: "create&edit", "NULL")
    functions = tags.options("UIEventHandlerReferenceFunction")
    inputs = tags.options("UIGameDataInputReferenceFunction")
    functions[:102] = engine_names(ROOT / "source/interface/ui_widget_event_handler_functions.c",
                                   '\t{\n\t\t"NULL",', 102)
    inputs[:41] = engine_names(ROOT / "port/linux/game/menu_tags.c", "game_data_input_names[] =", 41)
    PREVIOUS.update((path, path.read_bytes()) for path in CE.rglob("*.png"))
    for folder in (CE, SVG_COPIES, MENUS / "port_svg"):
        if folder.exists():
            shutil.rmtree(folder)
    CE.mkdir(parents=True)
    widgets = reachable(tags)
    art = Art(tags, arguments.redraws)
    fonts = Counter()
    # the widgets, a file for each folder of their tags
    folders = defaultdict(list)
    for tag in sorted(widgets):
        folder = our_name(tag).rsplit("/", 1)[0] if "/" in our_name(tag) else "shell"
        # (the settings screens this port has its own of, and the widgets:
        # tools/port_settings.py)
        if not port_settings.replaced(folder) and our_name(tag) not in port_settings.REPLACED_WIDGETS:
            folders[folder].append(tag)
    string_lists, bitmaps = set(), set()
    for widget in widgets.values():
        if widget["text label unicode strings list"]:
            string_lists.add(widget["text label unicode strings list"])
        for key in ("background bitmap", "list header bitmap", "list footer bitmap"):
            if widget[key] and widget[key] not in XBOX_BITMAPS:
                bitmaps.add(widget[key])
    for folder, members in sorted(folders.items()):
        lines = ['<?xml version="1.0" encoding="UTF-8"?>',
                 f"<!-- The PC version's menus: ui\\shell\\{folder.replace('/', chr(92))} (tools/ce_menus.py) -->"]
        lines.append("<menus>" if MAIN_MENU not in members else f"<menus{attributes([('root', our_name(MAIN_MENU))])}>")
        for tag in members:
            lines += widget_xml(tag, widgets[tag], tags, functions, inputs, fonts, {})
        lines.append("</menus>")
        (CE / f"{folder.replace('/', '.')}.xml").write_text("\n".join(lines) + "\n")
    for name, lines in [*port_settings.settings_files().items(), *port_settings.multiplayer_files().items(),
                        *port_settings.campaign_files().items()]:
        (CE / name).write_text("\n".join(lines))
    # (the menu engine's model of them: docs/menu_engine.md)
    for name, lines in port_settings.model_files().items():
        (MENUS / name).parent.mkdir(exist_ok=True)
        (MENUS / name).write_text("\n".join(lines))
    # (and the look and behaviour of every widget written, and the setup screens' nodes: tools/menu_look.py,
    # tools/menu_behaviour.py, tools/menu_nodes.py)
    menu_look.write(MENUS)
    menu_behaviour.write(MENUS)
    menu_nodes.write(MENUS)
    # their strings
    (CE / "strings.xml").write_text(strings_xml({
        tag: [text_value(item["string"]) for item in tags.load(tag, "unicode_string_list")["strings"]]
        for tag in string_lists}))
    # their bitmaps
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- The PC version's menus' bitmaps, drawn from the redraws, else the PC version's own pictures"
             " (NON_HANDDRAWN.md; tools/ce_menus.py) -->", "<menus>"]
    # (the PC version's bitmaps the port draws its own of in their place:
    # port_settings.text_button_pictures)
    drawn = dict(port_settings.text_button_pictures())
    lines += [art.port_picture(our_name(tag), drawn[our_name(tag)]) if our_name(tag) in drawn else art.bitmap(tag)
              for tag in sorted(bitmaps)]
    lines += [art.title(name, text) for name, text in sorted(port_settings.TITLES.items())]
    lines += pause_art(art)
    # (voice chat's speaker, of a player who talks and of one muted: Lucide's
    # icons, port/assets/icons/lucide, white for network_voice.c to tint)
    lines.append(art.port_picture("voice/speaker", [
        ((ROOT / "port/assets/icons/lucide" / f"{icon}.svg").read_text().replace('stroke="currentColor"',
                                                                                 'stroke="#FFFFFF"'), 24, 24)
        for icon in ("volume-2", "volume-x")]))
    # (the VR build's VR TUTORIAL, in the redraws' own letters, and the
    # mods' status icons: Lucide's, port/assets/icons/lucide)
    lines.append(art.port_picture("main_menu/menu_training",
                                  port_settings.menu_item_frames("VR TUTORIAL", art.redraws)))
    # (the main menu's PRO 2V2, so too: port_settings._doubles)
    lines.append(art.port_picture("main_menu/menu_doubles", port_settings.menu_item_frames("PRO 2V2", art.redraws)))
    # (the main menu's VERSUS: port/assets/menus/screens/main.xml)
    lines.append(art.port_picture("main_menu/menu_versus", port_settings.menu_item_frames("VERSUS", art.redraws)))
    # (the Campaign screen's SOLO and CO-OP: port/assets/menus/screens/campaign.xml; its hyphen menu_item_frames's)
    lines.append(art.port_picture("main_menu/menu_solo", port_settings.menu_item_frames("SOLO", art.redraws)))
    lines.append(art.port_picture("main_menu/menu_coop", port_settings.menu_item_frames("CO-OP", art.redraws)))
    lines.append(art.port_picture("mods/status", port_settings.mod_status_frames(ROOT / "port/assets/icons/lucide")))
    lines.append(art.port_picture("mods/status_small",
                                  port_settings.mod_status_frames(ROOT / "port/assets/icons/lucide", 16)))
    # (nothing: what a game map draws for a frame of ui.map's it has not,
    # menu_tags.c)
    lines.append(art.port_picture("blank", [
        ('<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4" viewBox="0 0 4 4"/>\n', 4, 4)]))
    # (Multiplayer's panel: its counts' digits, and a game's ping as bars)
    lines.append(art.digits(port_settings.STATUS_DIGITS))
    lines.append(art.port_picture(port_settings.PING_BARS, port_settings.ping_bars_frames()))
    # (the browsers' rows' star: Lucide's, port/assets/icons/lucide)
    lines.append(art.port_picture(port_settings.FAVORITE_STAR,
                                  port_settings.favorite_star_frames(ROOT / "port/assets/icons/lucide")))
    # (Settings' rows, tabs and panels, and the Quality row's stills)
    lines += [art.port_picture(name, frames) for name, frames in port_settings.settings_pictures()]
    lines += [art.still(name, png, width, height) for name, png, width, height, _ in port_settings.SETTINGS_STILLS]
    lines += [art.still(f"{port_settings.SETTINGS}/pictures_{key}", pngs, port_settings.SETTINGS_PICTURE_WIDTH,
                        port_settings.SETTINGS_PICTURE_HEIGHT, port_settings.SETTINGS_PICTURE_RIM)
              for key, pngs in port_settings.SETTINGS_VALUE_PICTURES.items()]
    # (the menus' shared parts, in Settings' look: the server browser's and
    # Multiplayer's tabs, rows, panels, pills, button and prompts, and the
    # touchscreen's chips, rows, tiles and buttons)
    lines += [art.port_picture(name, frames) for name, frames in port_settings.parts_pictures()]
    # (the campaign's cards, arrows, dots and panels: port_settings.campaign_pictures)
    lines += [art.port_picture(name, frames) for name, frames in port_settings.campaign_pictures()]
    lines.append("</menus>")
    (CE / "bitmaps.xml").write_text("\n".join(lines) + "\n")
    # what is not hand-drawn
    report = [
        "# Menu pictures that are not redraws",
        "",
        "The menus' bitmaps are drawn from the high-res redraws (`svg/`, from ui-svg-handmade) where there is",
        "one. The PC version's pictures that have none (3D renders, screenshots and logos) are not in this",
        "repository.",
        "",
        "## From the Xbox's map",
        "",
        "These have the same pictures, in the same order, in the Xbox's `ui.map`, which the menus draw them",
        "from (the player's own; nothing is shipped). `mp_map_grafix`: the Xbox's has the 13 Xbox maps' pictures",
        "(the PC version's first 13) and its \"?\"; the PC version's other 8 are its own maps'.",
        "",
    ]
    report += [f"- `{tag}`" for tag in sorted(XBOX_BITMAPS)]
    report += [
        "",
        "These frames are drawn from a frame of the Xbox map's bitmap of the same name, whose other frames differ:",
        "the same picture, or for the profile settings the Xbox's picture for the same thing.",
        "",
        "| The PC version's frame | The Xbox's frame |",
        "| --- | --- |",
    ]
    report += [f"| `{name}.png` | `{tag}` frame {index} |" for name, tag, index in sorted(art.xbox_frames)]
    report += [
        "",
        "These frames are added after a bitmap's, drawn scaled from the Xbox map's (the profile settings'",
        "picture of Gamepad Setup: the Xbox's Controller Setup's pictures of the button settings).",
        "",
        "| Our bitmap | The Xbox's frame |",
        "| --- | --- |",
    ]
    report += [f"| `{name}` | `{tag}` frame {index} |" for name, tag, index in art.added_frames]
    report += [
        "",
        "## Screenshots",
        "",
        *port_settings.SETTINGS_STILLS_MADE,
        "",
        "| File | Size | What it shows |",
        "| --- | --- | --- |",
    ]
    report += [f"| `{png}` | {width}x{height} | {what} |" for _, png, width, height, what in port_settings.SETTINGS_STILLS]
    report += [f"| `{png}` | {port_settings.SETTINGS_PICTURE_WIDTH}x{port_settings.SETTINGS_PICTURE_HEIGHT} | {what} |"
               for png, what in port_settings.SETTINGS_PICTURE_FILES.items()]
    report += [
        "",
        "## Not redrawn yet: drawn empty",
        "",
        "The Xbox's map has none of these (or other pictures under the name), so each frame is drawn empty",
        "(no picture shows there) until it is redrawn.",
        "",
        "| File | Size | The PC version's picture |",
        "| --- | --- | --- |",
    ]
    report += [f"| `{png}` | {w}x{h} | `{source}` |" for png, w, h, source in sorted(art.empty)]
    report += ["", f"{len(art.empty)} of {len(art.pngs)} frames are drawn empty.",
               "", "The PC version's `ui\\gamespy` and `ui\\ticker` fonts are drawn with `ui\\small_ui`, which",
               "the Xbox's map has.", ""]
    (MENUS / "NON_HANDDRAWN.md").write_text("\n".join(report))
    # what is not wired
    used = Counter()
    for path in CE.glob("*.xml"):
        import re
        for name in re.findall(r'(?:run|input)="([^"]*)"', path.read_text()):
            used[name] += 1
    pc_functions = set(functions[102:]) | set(inputs[41:])
    unwired = sorted((name[8:] if name.startswith("unwired ") else name, count) for name, count in used.items()
                     if name.startswith("unwired ") or (name in pc_functions and name not in WIRED))
    report = [
        "# Menu functions that are not wired yet",
        "",
        "The PC version's menus (`ce/`, from `tools/ce_menus.py`) run functions of the game's. These do nothing",
        "yet (`port/linux/game/menu_functions.c`), so what they fill in (lists, values) is empty, and what they",
        "do (save a setting, join a game) does not happen; the screens open and close as the PC version's.",
        "",
        "- The PC version's own functions, which the Xbox's game has not.",
        "- The Xbox's functions that the PC version rewrote for its own widgets, written `unwired <name>` in the",
        "  files: run on the PC widgets, the Xbox's would stop the game.",
        "",
        "| Function | Uses | |",
        "| --- | --- | --- |",
    ]
    report += [f"| `{name}` | {count} | {'Xbox function, PC widgets' if name in INCOMPATIBLE else 'PC function'} |"
               for name, count in unwired]
    report += ["", "Wired: " + ", ".join(f"`{name}`" for name in sorted(WIRED)) + ".", ""]
    (MENUS / "UNWIRED.md").write_text("\n".join(report))
    files = sorted(path.relative_to(MENUS).as_posix() for path in MENUS.rglob("*.xml"))
    files += sorted(art.pngs)
    (MENUS / "menus.json").write_text(json.dumps({
        "comment": "The menus' files the game embeds (tools/ce_menus.py, tools/embed_assets.py).",
        "files": files,
    }, indent=1) + "\n")
    size = sum((MENUS / name).stat().st_size for name in files)
    print(f"{len(widgets)} widgets in {len(folders)} files, {len(string_lists)} string lists, {len(bitmaps)} bitmaps "
          f"({len(art.pngs)} frames, {len(art.empty)} not redrawn); {size // 1024} KB; fonts {dict(fonts)}")


if __name__ == "__main__":
    main()
