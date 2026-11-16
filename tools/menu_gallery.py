#!/usr/bin/env python3
"""Every menu screen in each input's variant, as one HTML contact sheet, with
the screens' layout checked from the game's own geometry.

    python tools/menu_gallery.py --check                # the layout checks alone, in seconds (CI's)
    python tools/menu_gallery.py                        # build/menu_gallery/index.html
    python tools/menu_gallery.py --regenerate --screens audio_settings   # a generator's change, seen
    python tools/menu_gallery.py --compare master       # only the screens that differ
    python tools/menu_gallery.py --remote 5900x.fabric  # on a Linux box over ssh

The screens are enumerated from the generated menu files
(port/assets/menus/ce: the root, every screen an event opens, and every
widget named *_screen that no other holds; VR's settings, drawn in the
headset, with --vr). One run of the game walks a variant's screens
(debug.menu_tour: each opened alone, its layout written by debug.menu_layout's
code, port/linux/game/menu_layout.c, and its frame saved), hidden and muted,
in folders of its own (its HOME, its data root with the maps linked in), off
the network and the clipboard; a variant's screens are split over as many
runs as --jobs allows, all at once. The variants are the maintainer's
priorities: keyboard and mouse at 16:9, a controller at 16:10 (the Steam
Deck's 1280x800) and touch on a phone in landscape (2400x1080); --variants
all adds 4:3 and 21:9.

No build is needed for a change to the menu files: the runs read this tree's
port/assets/menus over the build's own (the menus folder beside config.toml,
menu_files.c), so a generator's change (--regenerate runs
tools/port_settings.py first; tools/ce_menus.py needs the PC version's tags)
is seen in seconds. --build-menus uses the build's own.

--check draws no pictures (debug.null_renderer): the layout checks for every
screen and variant in a few seconds, the mode for after each regenerate and
for CI.

--compare <git ref or build folder> renders the screens with that too and
shows only the screens that changed, before | after, with a count of the
rest. A ref that differs from this tree only in its menu files (and tools and
documents) is drawn by this build with its files (git's archive of them);
another is checked out in build/menu_gallery_trees/<commit> and built, the
build's configure_args reused.

The layout checks (each screen in each variant): text cut by its clip or
past its box's foot, text over another's, anything off the screen, a phone's
safe areas (an iPhone's in landscape, scaled), touch targets under about 7 mm
on the phone, and prompts with the other input's glyphs. They are in the page
and in report.json, and the exit status is 1 where there is one (with
--compare, one the before has not; without, the touch targets are reported
but do not fail it, since the touch layout's rows are 3 to 5 mm on every
screen). A screen that writes no layout (it closes at once without a state
the run has not, or it is the virtual keyboard's) is marked so.

Each run has its own data and save folders (its profiles and saves its own), but not its own map cache: the
stock maps are unpacked once for a build and its maps folder (build/menu_gallery_saves, made again when the
build changes), and each run starts from a clone of them (an APFS clone on a Mac, a reflink on Linux, else
hard links of the maps), not 2.6 to 3.2 GB of its own.

The pictures are the game's imagery: they stay in --out (under build/, which
git ignores), never committed nor pushed.

--remote <host> runs it on a Linux box: this commit is pushed to the clone
there (--remote-tree; made from ~/openh1-vulkan-check's when missing), the
uncommitted changes applied, build/linux_x86_64 built, and the gallery made
there with --remote-data's maps; the folder comes back as a tar into --out.
"""
import argparse
import concurrent.futures
import hashlib
import html
import io
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import capture  # noqa: E402
import menu_sources  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENUS = Path("port") / "assets" / "menus" / "ce"


# ---------- the screens

@dataclass
class Screen:
    """a screen of the menus: the widget debug.menu_open opens, its file and the generator that writes it"""
    name: str
    file: str
    generator: str
    width: int = 640
    height: int = 480

    @property
    def label(self) -> str:
        return self.name[len("main_menu/"):] if self.name.startswith("main_menu/") else self.name

    @property
    def slug(self) -> str:
        return re.sub(r"[^A-Za-z0-9_.-]+", "-", self.name).strip("-")


# screens shown only for a moment, or that need a state a menu_open run has not (saving a profile)
TRANSIENT = re.compile(r"_(saving|creating)_profile$|^error/warning_")
# the event handler functions the game runs as it opens a screen that closes at once without what they do (the
# tour runs them first, as the first player's press would: port/linux/game/menu_tour.c's menu_tour_setup; "close
# keyboard" closes the keyboard one of them launched): a gametype being edited (the gametype editor's screens, which
# "close if not editing profile" closes without one, a player profile being edited apart), and a name's keyboard
TOUR_SETUP = {
    "main_menu/settings_select/multiplayer_setup/create_and_edit_new_gametype_screen":
        ("create&edit playlist profile", "close keyboard"),
    "main_menu/settings_select/multiplayer_setup/playlist_edit/gametype_edit_screen":
        ("create&edit playlist profile", "close keyboard"),
    "main_menu/settings_select/multiplayer_setup/name_edit/gametype_name_screen":
        ("create&edit playlist profile", "close keyboard", "mp profile change name"),
    "main_menu/settings_select/player_setup/player_profile_edit/name_edit/profile_name_screen":
        ("player profile change name",),
}
# the screens the game shows only under the virtual keyboard, which neither updates nor draws the widgets while it is
# up (ui_widget.c), and which close themselves once it closes (their auto_close): the keyboard is all they show, and
# they write no layout on either engine
UNDER_KEYBOARD = re.compile(r"/name_edit/[a-z_]+_name_screen$")


def tour_line(name: str) -> str:
    """a screen's line in a tour's list (debug.menu_tour) or debug.menu_open: its name and, a tab before each, the
    functions the game runs as it opens it (TOUR_SETUP)"""
    return "\t".join((name,) + TOUR_SETUP.get(name, ()))


# VR's screens, drawn in the headset (--vr includes them)
VR_SCREEN = re.compile(r"/vr_[a-z_]+/")


def enumerate_screens(menus: Path, vr: bool = False) -> list:
    """the screens of the menu files in a folder: the root, each widget an event opens, and each *_screen no
    other widget holds, in the files' order (VR's with vr)"""
    widgets, files, generators, children, opened = {}, {}, {}, set(), set()
    root = None
    for path in sorted(menus.glob("*.xml")):
        text = path.read_text(errors="replace")
        match = re.search(r"<!--[^>]*\((tools/[\w./-]+\.py)\)", text)
        generator = match.group(1) if match else ""
        try:
            tree = ElementTree.fromstring(text)
        except ElementTree.ParseError as error:
            raise ValueError(f"{path}: {error}") from None
        root = root or tree.get("root")
        for widget in tree.iter("widget"):
            name = widget.get("name")
            if name and name not in widgets:
                widgets[name] = widget
                files[name] = path.name
                generators[name] = generator
        for child in tree.iter("child"):
            children.add(child.get("widget"))
        for handler in tree.iter("on"):
            if handler.get("open"):
                opened.add(handler.get("open"))
    names = []
    for name in widgets:
        if TRANSIENT.search(name) or (not vr and VR_SCREEN.search(name)):
            continue
        if name == root or name in opened or (name.endswith("_screen") and name not in children):
            names.append(name)
    screens = []
    for name in names:
        widget = widgets[name]
        screens.append(Screen(name, files[name], generators[name], int(widget.get("width", 640) or 640),
                              int(widget.get("height", 480) or 480)))
    return screens


# ---------- the variants

@dataclass
class Variant:
    description: str
    size: tuple
    prompts: str
    touch: bool = False
    phone: bool = False
    default: bool = True
    settings: dict = field(default_factory=dict)


VARIANTS = {
    "keyboard-16x9": Variant("keyboard and mouse, 16:9", (1920, 1080), "keyboard"),
    "controller-16x10": Variant("controller, 16:10 (Steam Deck)", (1280, 800), "controller"),
    "touch-phone": Variant("touch, phone in landscape", (2400, 1080), "controller", touch=True, phone=True),
    "keyboard-4x3": Variant("keyboard and mouse, 4:3", (1440, 1080), "keyboard", default=False),
    "keyboard-21x9": Variant("keyboard and mouse, 21:9", (2520, 1080), "keyboard", default=False),
}

# a phone in landscape (an iPhone 15 Pro's 2556x1179 at 460 ppi, its safe area 59 points at each side and 21
# at the foot, for 3 pixels a point), scaled to the variant's 1080 rows
PHONE_PIXELS_PER_INCH = 460.0 * 1080 / 1179
PHONE_SAFE_INSETS = {"left": 59 * 3 * 1080 / 1179, "right": 59 * 3 * 1080 / 1179, "top": 0.0,
                     "bottom": 21 * 3 * 1080 / 1179}
TOUCH_TARGET_MILLIMETRES = 7.0


def parse_variants(text: str) -> list:
    if text == "default":
        return [name for name, variant in VARIANTS.items() if variant.default]
    if text == "all":
        return list(VARIANTS)
    names = [name.strip() for name in text.split(",") if name.strip()]
    for name in names:
        if name not in VARIANTS:
            raise argparse.ArgumentTypeError(f"no variant {name!r} ({', '.join(VARIANTS)}, default, all)")
    return names


# ---------- the layout checks

BUTTONS_WITH_KEYS = {"a-button", "b-button", "x-button", "y-button", "black-button", "white-button",
                     "left-trigger", "right-trigger", "back-button"}
TARGET_KINDS = ("item", "value", "list slot", "button", "band", "band")


def rectangle_area(a) -> int:
    return max(0, a[2] - a[0]) * max(0, a[3] - a[1])


def intersection(a, b):
    return [max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])]


def beyond(inner, outer, slack: float = 0.0) -> float:
    """how far the inner rectangle reaches past the outer one, at most (0: inside)"""
    return max(outer[0] - inner[0], outer[1] - inner[1], inner[2] - outer[2], inner[3] - outer[3], 0) - slack


def check_layout(layout: dict, variant: Variant) -> list:
    """the problems of a screen's layout (menu_layout.c's JSON) in a variant: dicts of check, message and the
    rectangle (menu units) it is about"""
    items = layout.get("items", [])
    widgets = {item["id"]: item for item in items if item["kind"] == "widget"}
    texts = [item for item in items if item["kind"] == "text" and item.get("extent") and item["text"].strip()]
    glyphs = [item for item in items if item["kind"] == "glyph"]
    targets = [item for item in items if item["kind"] == "target"]
    screen = layout["screen"]
    problems = []

    def owner(item) -> str:
        widget = widgets.get(item.get("widget"))
        # (a target names its widget's id, as a text does)
        return widget["name"].rsplit("\\", 1)[-1] if widget else "?"

    def add(check, message, box):
        problems.append({"check": check, "message": message, "box": box})

    def quoted(text: str) -> str:
        text = " ".join(text.split())
        return '"' + (text if len(text) <= 48 else text[:45] + "...") + '"'

    for text in texts:
        extent, clip, box = text["extent"], text["clip"], text["box"]
        # (the drop shadow's pixel, and a font's descent below its box, are not a cut)
        if beyond(extent, clip, 1) > 0:
            add("clipped", f"{quoted(text['text'])} ({owner(text)}) is cut by its clip, {beyond(extent, clip):.0f}"
                           " units past it", extent)
        elif extent[2] > box[2] + 2 or extent[0] < box[0] - 2:
            add("overflow", f"{quoted(text['text'])} ({owner(text)}) is wider than its box by"
                            f" {max(extent[2] - box[2], box[0] - extent[0]):.0f} units", extent)
        elif rectangle_area(box) > 0 and box[3] - box[1] >= 8 and extent[3] > box[3] + 4:
            add("overflow", f"{quoted(text['text'])} ({owner(text)}) runs {extent[3] - box[3]:.0f} units below"
                            " its box", extent)
        whole, wide = text.get("whole"), text.get("wide")
        if whole and whole[3] > extent[3] + 2:
            add("cut", f"{quoted(text['text'])} ({owner(text)}) runs past its box's foot: its last"
                       f" {whole[3] - extent[3]:.0f} units of lines are not drawn", [box[0], box[3], box[2],
                                                                                    whole[3]])
        # (a line, laid out without the right edge, the same height and wider: its last letters not drawn)
        elif wide and wide[3] == extent[3] and wide[2] - wide[0] > extent[2] - extent[0] + 2:
            add("cut", f"{quoted(text['text'])} ({owner(text)}) runs past its box's right edge: its last"
                       f" {wide[2] - wide[0] - (extent[2] - extent[0]):.0f} units of letters are not drawn", extent)
        if beyond(extent, screen) > 0:
            add("offscreen", f"{quoted(text['text'])} ({owner(text)}) is off the screen", extent)

    # text over text: two strings of different widgets whose letters share more than a sliver
    for index, first in enumerate(texts):
        for second in texts[index + 1:]:
            if first.get("widget") == second.get("widget"):
                continue
            a = visible(first)
            b = visible(second)
            if not a or not b:
                continue
            overlap = intersection(a, b)
            if overlap[2] - overlap[0] > 2 and overlap[3] - overlap[1] > 3:
                add("overlap", f"{quoted(first['text'])} ({owner(first)}) is drawn over {quoted(second['text'])}"
                               f" ({owner(second)})", overlap)
    for glyph in glyphs:
        if glyph["box"][2] <= glyph["box"][0]:
            continue
        if beyond(glyph["box"], screen) > 0:
            add("offscreen", f"the {glyph['name']} glyph ({owner(glyph)}) is off the screen", glyph["box"])
        if variant.prompts == "keyboard" and not glyph["key"] and glyph["name"] in BUTTONS_WITH_KEYS:
            add("glyphs", f"the controller's {glyph['name']} ({owner(glyph)}) shown to the keyboard and mouse",
                glyph["box"])
        if variant.prompts == "controller" and glyph["key"]:
            add("glyphs", f"the key {glyph['name']} ({owner(glyph)}) shown to "
                          f"{'touch' if variant.touch else 'a controller'}", glyph["box"])

    if variant.phone:
        units = (layout["screen"][3] - layout["screen"][1]) / variant.size[1]
        safe = [screen[0] + PHONE_SAFE_INSETS["left"] * units, screen[1] + PHONE_SAFE_INSETS["top"] * units,
                screen[2] - PHONE_SAFE_INSETS["right"] * units, screen[3] - PHONE_SAFE_INSETS["bottom"] * units]
        for text in texts:
            area = visible(text)
            if area and beyond(area, safe) > 1 and beyond(area, screen) <= 0:
                add("safe-area", f"{quoted(text['text'])} ({owner(text)}) is under the phone's safe area", area)
        for glyph in glyphs:
            if glyph["box"][2] > glyph["box"][0] and beyond(glyph["box"], safe) > 1:
                add("safe-area", f"the {glyph['name']} glyph ({owner(glyph)}) is under the phone's safe area",
                    glyph["box"])
        least = TOUCH_TARGET_MILLIMETRES / 25.4 * PHONE_PIXELS_PER_INCH * units
        small = []
        for target in targets:
            box = target["box"]
            width, height = box[2] - box[0], box[3] - box[1]
            if width > 0 and height > 0 and min(width, height) + 0.5 < least:
                small.append((min(width, height) / units / PHONE_PIXELS_PER_INCH * 25.4, target))
        # (one line a screen: the touch layout's rows are all of a size)
        if small:
            millimetres, target = min(small, key=lambda entry: entry[0])
            kind = TARGET_KINDS[target["target"]] if 0 <= target["target"] < len(TARGET_KINDS) else "target"
            add("touch-target", f"{len(small)} of {len(targets)} touch targets are under"
                                f" {TOUCH_TARGET_MILLIMETRES:g} mm across, the smallest {millimetres:.1f} mm (the"
                                f" {kind} {owner(target)})", target["box"])
    return problems


def visible(text):
    """the part of a text's letters its clip lets show, or None"""
    area = intersection(text["extent"], text["clip"])
    return area if area[2] > area[0] and area[3] > area[1] else None


# checks that every screen fails on master (the touch layout's rows are 3 to 5 mm on a phone): reported, and
# failing only where new (--compare)
ADVISORY = {"touch-target"}


def problem_key(problem: dict) -> tuple:
    """a problem as compared between two builds: its check and words (not where it is)"""
    return problem["check"], problem["message"]


# ---------- pictures

def load_picture(path: Path):
    return capture.read_bmp(path)


def save_picture(picture, path: Path, thumbnail: Path = None, thumbnail_width: int = 640):
    """the picture (a PIL image as JPEG, capture.py's as PNG), and a smaller copy for the page"""
    if not isinstance(picture, capture.Picture):
        picture.save(path.with_suffix(".jpg"), "JPEG", quality=90)
        if thumbnail is not None:
            height = max(1, round(picture.height * thumbnail_width / picture.width))
            picture.resize((thumbnail_width, height), 2).save(thumbnail, "JPEG", quality=82)  # (BILINEAR)
        return
    capture.write_png(picture, path)
    if thumbnail is None:
        return
    try:
        from PIL import Image
        image = Image.frombytes("RGB", (picture.width, picture.height), b"".join(picture.rows))
        height = max(1, round(picture.height * thumbnail_width / picture.width))
        image.resize((thumbnail_width, height), Image.LANCZOS).save(thumbnail, "JPEG", quality=82)
        return
    except ImportError:
        pass
    capture.write_png(subsample(picture, max(1, picture.width // thumbnail_width)), thumbnail.with_suffix(".png"))


def read_shot(path: Path, size):
    """a tour's frame (a BMP file) at the size asked for (a Retina display's twice that, averaged): PIL's
    image where it is there, else capture.py's picture"""
    try:
        from PIL import Image
    except ImportError:
        return capture.fit(capture.read_bmp(path), size)
    with Image.open(path) as opened:
        image = opened.convert("RGB")
    if image.size != tuple(size) and image.width % size[0] == 0 and image.width // size[0] == image.height // size[1]:
        image = image.resize(tuple(size), Image.BOX)
    return image


def kept(picture):
    """a frame as the page's files and --compare need it: the full picture, a thumbnail, the picture of every
    PICTURE_STEP-th pixel (capture.py's) and a digest. PIL's JPEG files (fast, small) where it is there, else
    PNG files"""
    if isinstance(picture, capture.Picture):
        return picture, subsample(picture, PICTURE_STEP), picture_digest(picture)
    raw = picture.tobytes()
    small = picture.resize((max(1, picture.width // PICTURE_STEP), max(1, picture.height // PICTURE_STEP)),
                           0)  # (NEAREST: every third pixel, as subsample takes them)
    data, stride = small.tobytes(), small.width * 3
    rows = [data[row * stride:(row + 1) * stride] for row in range(small.height)]
    return picture, capture.Picture(small.width, small.height, rows), hashlib.sha256(raw).hexdigest()


def subsample(picture, step: int):
    """every step-th pixel of every step-th row"""
    if step <= 1:
        return picture
    width = len(range(0, picture.width, step))
    rows = []
    for row in picture.rows[::step]:
        out = bytearray(width * 3)
        for channel in range(3):
            out[channel::3] = row[channel::step * 3][:width]
        rows.append(bytes(out))
    return capture.Picture(width, len(rows), rows)


def picture_digest(picture) -> str:
    return hashlib.sha256(b"".join(picture.rows)).hexdigest()


def layout_shape(layout: dict) -> list:
    """what a screen drew, as compared between two builds: its widgets' names and boxes, its texts' words and
    extents, its glyphs"""
    shape = []
    for item in layout.get("items", []):
        if item["kind"] == "widget":
            shape.append(("widget", item["name"], tuple(item["box"])))
        elif item["kind"] == "text":
            shape.append(("text", item["text"], tuple(item["extent"] or ()), tuple(item.get("whole") or ())))
        elif item["kind"] == "glyph":
            shape.append(("glyph", item["key"], item["name"], tuple(item["box"])))
    # (in no order: a build may write the items in another)
    return sorted(shape, key=repr)


def regions_differ(a, b, layouts, threshold: int = 48, share: float = 0.25) -> bool:
    """whether the letters or glyphs either layout drew changed: a text's or glyph's box (in the pictures,
    whatever their size) with more than the share of its pixels changed by more than the threshold. The
    background's 3D scene and the panels' plasma move between two runs; the menus' own drawing does not"""
    if a.width != b.width or a.height != b.height:
        return True
    for layout in layouts:
        x0, y0, x1, y1 = layout["screen"]
        scale = a.height / (y1 - y0)
        for item in layout["items"]:
            box = item.get("extent") if item["kind"] == "text" else item["box"] if item["kind"] == "glyph" else None
            if not box:
                continue
            left, right = max(0, int((box[0] - x0) * scale)), min(a.width, int((box[2] - x0) * scale) + 1)
            top, bottom = max(0, int((box[1] - y0) * scale)), min(a.height, int((box[3] - y0) * scale) + 1)
            if right - left < 2 or bottom - top < 2:
                continue
            changed = 0
            for row in range(top, bottom):
                row_a, row_b = a.rows[row], b.rows[row]
                for index in range(left * 3, right * 3, 3):
                    if (abs(row_a[index] - row_b[index]) > threshold or
                            abs(row_a[index + 1] - row_b[index + 1]) > threshold or
                            abs(row_a[index + 2] - row_b[index + 2]) > threshold):
                        changed += 1
            if changed > share * (right - left) * (bottom - top):
                return True
    return False


def results_differ(before: dict, after: dict) -> bool:
    """whether a screen changed between two builds: by their layouts where both wrote one (what they drew,
    and the pixels of their letters and glyphs), else by the whole picture; without pictures (--check), by
    their layouts alone"""
    if "picture" not in before or "picture" not in after:
        return layout_shape(before.get("layout") or {}) != layout_shape(after.get("layout") or {})
    if before.get("digest") == after.get("digest"):
        return False
    if before.get("layout") and after.get("layout"):
        if layout_shape(before["layout"]) != layout_shape(after["layout"]):
            return True
        return regions_differ(before["picture"], after["picture"], (before["layout"], after["layout"]))
    return blocks_differ(before["picture"], after["picture"])


def blocks_differ(a, b, block: int = 6, threshold: int = 64) -> bool:
    """whether two pictures differ by more than the background's motion: some block of pixels' average colour
    moved by more than the threshold (the 3D scene and the plasma move a block's by up to about 45)"""
    if a.width != b.width or a.height != b.height:
        return True
    try:
        from PIL import Image, ImageChops
        size = (max(1, a.width // block), max(1, a.height // block))
        small = [Image.frombytes("RGB", (p.width, p.height), b"".join(p.rows)).crop(
            (0, 0, size[0] * block, size[1] * block)).resize(size, Image.BOX) for p in (a, b)]
        return max(high for _, high in ImageChops.difference(*small).getextrema()) > threshold
    except ImportError:
        pass
    for top in range(0, a.height - block + 1, block):
        sums_a = [0] * (a.width * 3)
        sums_b = [0] * (a.width * 3)
        for row in range(top, top + block):
            sums_a = [x + y for x, y in zip(sums_a, a.rows[row])]
            sums_b = [x + y for x, y in zip(sums_b, b.rows[row])]
        for left in range(0, a.width - block + 1, block):
            for channel in range(3):
                total_a = sum(sums_a[(left + column) * 3 + channel] for column in range(block))
                total_b = sum(sums_b[(left + column) * 3 + channel] for column in range(block))
                if abs(total_a - total_b) > threshold * block * block:
                    return True
    return False


def pictures_differ(a, b, threshold: int = 24, fraction: float = 0.0005) -> float:
    """the share of pixels whose colour moved by more than the threshold (0 when the same), or 1 for another
    size; under the fraction counts as the same (the menus' animations at a frame's difference)"""
    if a.width != b.width or a.height != b.height:
        return 1.0
    changed = 0
    for row_a, row_b in zip(a.rows, b.rows):
        if row_a == row_b:
            continue
        for index in range(0, len(row_a), 3):
            if (abs(row_a[index] - row_b[index]) > threshold or abs(row_a[index + 1] - row_b[index + 1]) > threshold
                    or abs(row_a[index + 2] - row_b[index + 2]) > threshold):
                changed += 1
    share = changed / (a.width * a.height)
    return share if share >= fraction else 0.0


# ---------- runs

def run_environment(variant: Variant, folder: Path, timeout: float, pictures: bool) -> dict:
    """a tour's run: its own folders, hidden, muted, off the network and the clipboard, its frames' time
    fixed (the menus' animations alike in two runs); no renderer where no pictures are wanted"""
    home, data, saves, shots = folder / "home", folder / "data", folder / "saves", folder / "shots"
    for path in (home, data, saves, shots, home / "cache", data / "tour"):
        path.mkdir(parents=True, exist_ok=True)
    width, height = capture.window_size(variant.size) if pictures else variant.size
    environment = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home), "TMPDIR": str(home),
        "XDG_CACHE_HOME": str(home / "cache"), "XDG_CONFIG_HOME": str(home / "config"),
        "XDG_DATA_HOME": str(home / "share"),
        "HALO_DATA_ROOT": str(data), "HALO_SAVE_ROOT": str(saves),
        "HALO_HIDDEN_WINDOW": "1", "SDL_AUDIO_DRIVER": "dummy", "HALO_VOLUME": "0",
        "HALO_NO_CLIPBOARD": "1", "HALO_NET_ONLINE": "false", "HALO_UPDATE_AUTO": "false",
        "HALO_NET_PORT": str(capture.free_port()), "HALO_NET_ADDRESS": "127.0.0.1",
        "HALO_NET_BROADCAST": "127.255.255.255", "HALO_NET_ALLOW_UPNP": "false", "HALO_NET_PUBLIC_LOBBY": "false",
        "HALO_EXIT_AFTER": str(int(timeout)),
        "HALO_DISPLAY_MODE": "windowed", "HALO_WINDOW_SIZE": f"{width}x{height}",
        "HALO_RESOLUTION_SCALING": "native", "HALO_BUTTON_PROMPTS": variant.prompts,
        "HALO_MENU_TOUR": "tour.txt", "HALO_SCREENSHOT_DIR": str(shots),
        "HALO_FRAME_TIME": f"{1 / 60:.6f}", "HALO_RANDOM_SEED": "1",
    }
    if not pictures:
        environment["HALO_NULL_RENDERER"] = "1"
    if variant.touch:
        environment["HALO_TOUCH_MENUS"] = "1"
    environment.update(variant.settings)
    return environment


# the pictures kept for --compare: every this many pixels each way
PICTURE_STEP = 3


def place(source: Path, destination: Path):
    """a build's file in a run's folder: a hard link where they share a file system (a gallery's hundreds of
    runs would copy gigabytes), else a copy"""
    if destination.exists():
        return
    try:
        os.link(source, destination)
    except OSError:
        shutil.copy2(source, destination)


# ---------- the map cache the runs share

# (the unpacked maps' files in a save root's z: cacheNNN.map and its note, cacheNNN.source, and the language
# they were unpacked in, without which the game unpacks them all again: port/linux/src/game_caches.c,
# map_unpack.c)
MAP_CACHE_FILE = re.compile(r"(cache\d{3}\.(map|source)|last_language\.dat)$")
# where the templates are kept, a folder for each build, a template for each of its builds and maps folders
MAP_CACHE_TEMPLATES = ROOT / "build" / "menu_gallery_saves"
# the most seconds the template's run may take to unpack the stock maps
MAP_CACHE_TIMEOUT = 600


def map_cache_key(build: Path, maps: Path) -> tuple:
    """(the build's folder's name among the templates, the template's for this build of it and these maps)"""
    stamp = [str(maps.resolve())]
    for name in ("halo", "halo_guest.elf"):
        if (build / name).is_file():
            status = (build / name).stat()
            stamp.append(f"{name}:{status.st_size}:{status.st_mtime_ns}")
    folder = hashlib.sha256(str(build.resolve()).encode()).hexdigest()[:12]
    return folder, hashlib.sha256("\n".join(stamp).encode()).hexdigest()[:12]


def map_cache_template(build: Path, maps: Path, variant: "Variant") -> Path:
    """a z folder with the stock maps unpacked by this build (its cacheNNN files), made once by a run of its
    own that waits for the map cache's prefill to go idle, and kept for later galleries until the build
    changes (the build's older templates deleted): the runs each start from a clone of it, not a cache of
    their own (2.6 to 3.2 GB a run), still with their own profiles and saves. None where it cannot be made"""
    folder, key = map_cache_key(build, maps)
    template = MAP_CACHE_TEMPLATES / folder / key
    if (template / "made").is_file():
        return template
    for old in (MAP_CACHE_TEMPLATES / folder).glob("*") if (MAP_CACHE_TEMPLATES / folder).is_dir() else []:
        shutil.rmtree(old, ignore_errors=True)
    started = time.monotonic()
    (MAP_CACHE_TEMPLATES / "runs").mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix="menu-gallery-template-", dir=MAP_CACHE_TEMPLATES / "runs"))
    process = None
    try:
        game = run / "game"
        game.mkdir()
        for name in capture.BUILD_FILES:
            if (build / name).is_file():
                place(build / name, game / name)
        if sys.platform != "darwin":
            for library in build.glob("*.so*"):
                place(library, game / library.name)
        environment = run_environment(variant, run, MAP_CACHE_TIMEOUT, False)
        environment.pop("HALO_MENU_TOUR", None)
        environment["HALO_LOAD_TRACE"] = "1"
        if sys.platform != "darwin":
            environment["SDL_VIDEO_DRIVER"] = "offscreen"
        (run / "data" / "maps").symlink_to(maps.resolve())
        log_path = run / "halo.log"
        with open(log_path, "w") as log:
            process = subprocess.Popen([str(game / "halo")], cwd=game, env=environment, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            idle = False
            while process.poll() is None and time.monotonic() - started < MAP_CACHE_TIMEOUT:
                text = log_path.read_text(errors="replace")
                debug = run / "data" / "debug.txt"
                text += debug.read_text(errors="replace") if debug.is_file() else ""
                if "phase=map_cache.prefill.idle" in text:
                    idle = True
                    break
                time.sleep(1)
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
        z = run / "saves" / "z"
        files = [path for path in z.iterdir() if MAP_CACHE_FILE.match(path.name)] if z.is_dir() else []
        if not idle or not files:
            print(f"menu_gallery: the map cache's template was not made (idle: {idle}, {len(files)} files): each "
                  "run unpacks its own", file=sys.stderr, flush=True)
            return None
        template.mkdir(parents=True, exist_ok=True)
        for path in files:
            path.rename(template / path.name)
        (template / "made").write_text(f"{len(files)} files, {time.monotonic() - started:.1f} s\n")
        print(f"menu_gallery: the map cache's template made: {len(files)} files, "
              f"{sum((template / p.name).stat().st_size for p in files) / 2 ** 30:.1f} GB, "
              f"{time.monotonic() - started:.1f} s", file=sys.stderr, flush=True)
        return template
    finally:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        shutil.rmtree(run, ignore_errors=True)


def map_cache_clone(template: Path, z: Path):
    """the template's files in a run's z folder, sharing their blocks: an APFS clone on a Mac (clonefile, cp
    -c), a reflink on Linux where the file system has them, else hard links of the maps (the game writes a
    slot only to unpack another map into it, which the template's full cache never needs) and copies of
    their small notes"""
    z.mkdir(parents=True, exist_ok=True)
    files = sorted(path for path in template.iterdir() if MAP_CACHE_FILE.match(path.name))
    if sys.platform == "darwin":
        if subprocess.run(["cp", "-c", *map(str, files), str(z)], capture_output=True).returncode == 0:
            return
    elif subprocess.run(["cp", "--reflink=always", *map(str, files), str(z)], capture_output=True).returncode == 0:
        return
    for path in files:
        if not (z / path.name).exists():
            if path.suffix == ".map":
                place(path, z / path.name)
            else:
                shutil.copy2(path, z / path.name)


def map_cache_unchanged(template: Path) -> bool:
    """whether no run wrote a hard-linked map of the template (its times as made); one that did is deleted,
    made again by the next gallery"""
    made = (template / "made").stat().st_mtime_ns
    if any(path.stat().st_mtime_ns > made for path in template.iterdir() if path.suffix == ".map"):
        shutil.rmtree(template, ignore_errors=True)
        return False
    return True


def tour(build: Path, screens: list, variant: Variant, maps: Path, menus: Path, pictures: bool, xvfb: bool,
         timeout: float, template: Path = None, logs: list = None, profile: Path = None) -> dict:
    """one run of the game through the screens (debug.menu_tour), the menu files of a folder over its own
    (its menus folder: no build for a change to them): {screen's name: (picture or None, layout or None)}
    (logs: the run's log appended to it; profile: a save folder's player profiles, its u, the run starts with:
    the screens that close at once with no profile being edited draw then)"""
    # (beside the template, on its file system, where its files can be cloned or linked: not a /tmp of RAM)
    if template:
        (MAP_CACHE_TEMPLATES / "runs").mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix="menu-gallery-", dir=MAP_CACHE_TEMPLATES / "runs" if template else None))
    process = server = None
    try:
        game = folder / "game"
        game.mkdir()
        for name in capture.BUILD_FILES:
            if (build / name).is_file():
                place(build / name, game / name)
        if sys.platform != "darwin":
            for library in build.glob("*.so*"):
                place(library, game / library.name)
        if menus:
            # (the folder beside config.toml, which a build run from its folder keeps in its own)
            (game / "menus").symlink_to(menus.resolve())
        environment = run_environment(variant, folder, timeout, pictures)
        if template:
            map_cache_clone(template, folder / "saves" / "z")
        if profile:
            shutil.copytree(profile, folder / "saves" / "u", dirs_exist_ok=True)
        (folder / "data" / "tour.txt").write_text("".join(tour_line(screen.name) + "\n" for screen in screens))
        (folder / "data" / "maps").symlink_to(maps.resolve())
        if sys.platform != "darwin":
            if xvfb and pictures:
                server, display = capture.start_xvfb(variant.size)
                environment.update({"DISPLAY": f":{display}", "SDL_VIDEO_DRIVER": "x11"})
            else:
                environment["SDL_VIDEO_DRIVER"] = "offscreen"
        with open(folder / "halo.log", "w") as log:
            process = subprocess.Popen([str(game / "halo")], cwd=game, env=environment, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                process.wait(timeout=timeout + 10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
        text = (folder / "halo.log").read_text(errors="replace")
        if logs is not None:
            logs.append(text)
        if "debug.menu_tour: done" not in text:
            raise RuntimeError(f"the tour ({variant.description}) did not finish in {timeout:g} s\n{text[-2000:]}")
        results = {}
        for index, screen in enumerate(screens):
            picture = layout = None
            shot = folder / "shots" / f"tour-{index}.bmp"
            if pictures and shot.exists():
                picture = read_shot(shot, variant.size)
            path = folder / "data" / "tour" / f"{index}.json"
            if path.exists():
                try:
                    layout = json.loads(path.read_text(errors="replace"))
                except json.JSONDecodeError:
                    layout = None
            results[screen.name] = (picture, layout)
        return results
    finally:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        if server:
            server.terminate()
            server.wait()
        shutil.rmtree(folder, ignore_errors=True)


def render(build: Path, screens: list, variants: list, maps: Path, out: Path, label: str, jobs: int,
           timeout: float, xvfb: bool, menus: Path = None, pictures: bool = True) -> dict:
    """every screen in every variant with a build, a tour a variant's part of the screens (as many parts as
    the jobs allow): {(screen, variant): result}, the pictures in out/label"""
    parts = max(1, -(-jobs // len(variants))) if pictures else 1
    size = -(-len(screens) // parts)
    tasks = [(name, screens[start:start + size]) for name in variants for start in range(0, len(screens), size)]
    results = {}
    sources = menu_sources.catalog(menus / "ce" if menus else ROOT / MENUS)
    template = map_cache_template(build, maps, VARIANTS[variants[0]])

    def one(task):
        name, part = task
        folder = out / label / name
        folder.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        try:
            found = tour(build, part, VARIANTS[name], maps, menus, pictures, xvfb, timeout + len(part), template)
        except Exception as error:  # (reported in the page)
            return task, {screen.name: {"error": str(error)} for screen in part}, time.monotonic() - started
        made = {}
        for screen in part:
            picture, layout = found[screen.name]
            result = {"layout": layout, "opened": layout is not None and
                      layout.get("screen_name", "").replace("\\", "/").endswith(screen.name)}
            stem = folder / screen.slug
            if layout is not None:
                # (each widget's source and file, from the menu files: the review tools')
                menu_sources.enrich_layout(layout, sources)
                stem.with_suffix(".json").write_text(json.dumps(layout))
            if picture is not None:
                save_picture(picture, stem.with_suffix(".png"), stem.with_suffix(".thumb.jpg"))
                # (a third of the pixels each way kept for --compare's difference: a whole gallery's would not
                # fit)
                _, small, digest = kept(picture)
                result.update(picture=small, digest=digest)
            elif pictures:
                result["error"] = "no picture"
            made[screen.name] = result
        return task, made, time.monotonic() - started

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, len(tasks))) as pool:
        for (name, part), made, seconds in pool.map(one, tasks):
            for screen_name, result in made.items():
                results[(screen_name, name)] = result
            print(f"menu_gallery: {label} {name}: {len(part)} screens in {seconds:.1f} s", file=sys.stderr,
                  flush=True)
    if template:
        map_cache_unchanged(template)
    return results


# ---------- a git ref's build

def build_target() -> str:
    if sys.platform == "darwin":
        return "macos"
    import platform
    return "linux_arm64" if platform.machine() in ("aarch64", "arm64") else "linux_x86_64"


def configure_arguments(tree: Path) -> list:
    """the options build.ninja was configured with (its configure_args)"""
    text = (tree / "build.ninja").read_text(errors="replace")
    match = re.search(r"^configure_args = ((?:.*\$\n)*.*)$", text, re.MULTILINE)
    if not match:
        return []
    return shlex.split(match.group(1).replace("$\n", " ").replace("$ ", " "))


# what a ref may differ from this tree in and still be drawn by this tree's build, its menu files laid over
# it: the menus' files, the tools, the documents
MENUS_ONLY = ("port/assets/menus/", "tools/")


def menus_only(changed) -> bool:
    """whether files changed between two trees are ones a build need not be made again for"""
    return all(name.startswith(MENUS_ONLY) or name.endswith(".md") for name in changed)


def ref_runner(tree: Path, ref: str, trees: Path, build: Path):
    """the build and menu files that draw a ref's menus: where it differs from this tree (and its uncommitted
    changes) only in its menu files, tools and documents, this tree's build with its menu files (git's
    archive of them, in trees/menus-<commit>); else its own build (ref_build)"""
    commit = subprocess.run(["git", "-C", str(tree), "rev-parse", "--verify", f"{ref}^{{commit}}"],
                            capture_output=True, text=True, check=True).stdout.strip()
    changed = subprocess.run(["git", "-C", str(tree), "diff", "--name-only", commit], capture_output=True,
                             text=True, check=True).stdout.split()
    changed += subprocess.run(["git", "-C", str(tree), "ls-files", "--others", "--exclude-standard"],
                              capture_output=True, text=True, check=True).stdout.split()
    if menus_only(changed):
        menus = (trees / f"menus-{commit[:12]}").resolve()
        if not (menus / "port" / "assets" / "menus").is_dir():
            menus.mkdir(parents=True, exist_ok=True)
            archive = subprocess.run(["git", "-C", str(tree), "archive", commit, "port/assets/menus"],
                                     capture_output=True, check=True).stdout
            with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
                tar.extractall(menus, filter="data")
        print(f"menu_gallery: {ref} differs only in its menus: drawn by this build", file=sys.stderr)
        return build, menus / "port" / "assets" / "menus"
    worktree_build = ref_build(tree, ref, trees)
    return worktree_build, worktree_build.parent.parent / "port" / "assets" / "menus"


def ref_build(tree: Path, ref: str, trees: Path) -> Path:
    """a build of a git ref: a worktree of it in trees/<commit>, configured as this tree is (its build
    folder cloned from this one's, for the compiler cache) and built"""
    commit = subprocess.run(["git", "-C", str(tree), "rev-parse", "--verify", f"{ref}^{{commit}}"],
                            capture_output=True, text=True, check=True).stdout.strip()
    worktree = (trees / commit[:12]).resolve()
    target = build_target()
    if not (worktree / ".git").exists():
        worktree.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["git", "-C", str(tree), "worktree", "add", "--detach", str(worktree), commit], check=True,
                       stdout=subprocess.DEVNULL)
        source = tree / "build" / target
        if source.is_dir():
            (worktree / "build").mkdir(exist_ok=True)
            copy = ["cp", "-Rc"] if sys.platform == "darwin" else ["cp", "-a", "--reflink=auto"]
            subprocess.run(copy + [str(source), str(worktree / "build" / target)], check=True)
            shutil.rmtree(worktree / "build" / target / "sdl3-build" / "CMakeFiles", ignore_errors=True)
            cache = worktree / "build" / target / "sdl3-build" / "CMakeCache.txt"
            if cache.exists():
                cache.unlink()
        if (tree / "build" / "msvc_semantics").is_dir() and not (worktree / "build" / "msvc_semantics").exists():
            subprocess.run(["cp", "-R", str(tree / "build" / "msvc_semantics"), str(worktree / "build")],
                           check=True)
        subprocess.run([sys.executable, "configure.py"] + configure_arguments(tree), cwd=worktree, check=True,
                       stdout=subprocess.DEVNULL)
    print(f"menu_gallery: building {ref} ({commit[:12]}) in {worktree}", file=sys.stderr, flush=True)
    subprocess.run(["ninja", target], cwd=worktree, check=True, stdout=subprocess.DEVNULL)
    return worktree / "build" / target


# ---------- the page

PAGE_STYLE = """
:root { --bg: #0b0f14; --panel: #141b24; --line: #26313f; --text: #dbe4ee; --dim: #8796a8; --accent: #4aa3ff;
  --bad: #ff6b5e; --warn: #ffb648; --good: #57d17c; color-scheme: dark; }
@media (prefers-color-scheme: light) { :root { --bg: #f4f6f9; --panel: #ffffff; --line: #d7dde5; --text: #18212b;
  --dim: #5d6b7c; --accent: #0b6bd3; --bad: #c62f22; --warn: #a96500; --good: #1d8a43; color-scheme: light; } }
* { box-sizing: border-box; }
body { margin: 0; background: var(--bg); color: var(--text); font: 15px/1.45 system-ui, -apple-system, sans-serif; }
header { padding: 20px 16px 8px; max-width: 1800px; margin: 0 auto; }
h1 { font-size: 22px; margin: 0 0 4px; }
.summary { color: var(--dim); margin: 0 0 8px; }
.filters { display: flex; gap: 8px; flex-wrap: wrap; margin: 10px 0; }
.filters input { flex: 1 1 220px; padding: 8px 10px; border-radius: 8px; border: 1px solid var(--line);
  background: var(--panel); color: var(--text); font: inherit; }
.filters label { display: flex; align-items: center; gap: 6px; color: var(--dim); }
main { padding: 0 16px 40px; max-width: 1800px; margin: 0 auto; }
section.screen { background: var(--panel); border: 1px solid var(--line); border-radius: 12px; padding: 12px;
  margin: 14px 0; }
section.screen h2 { font-size: 16px; margin: 0; word-break: break-word; }
.meta { color: var(--dim); font-size: 13px; margin: 2px 0 10px; word-break: break-word; }
.row { display: grid; gap: 12px; grid-template-columns: repeat(auto-fit, minmax(min(100%, 340px), 1fr)); }
figure { margin: 0; }
figure img { width: 100%; height: auto; display: block; border-radius: 6px; border: 1px solid var(--line);
  cursor: zoom-in; background: #000; }
figcaption { font-size: 13px; color: var(--dim); margin-top: 4px; }
.pair { display: grid; gap: 6px; grid-template-columns: 1fr 1fr; }
.pair figcaption { text-align: center; }
ul.problems { margin: 6px 0 0; padding-left: 18px; font-size: 13px; }
ul.problems li { margin: 2px 0; }
.check { font-weight: 600; }
.clipped, .cut, .overflow, .overlap { color: var(--bad); }
.offscreen, .safe-area, .glyphs { color: var(--warn); }
.touch-target { color: var(--accent); }
.error { color: var(--bad); font-size: 13px; white-space: pre-wrap; max-height: 8em; overflow: auto; }
.badge { display: inline-block; font-size: 12px; padding: 1px 7px; border-radius: 99px; border: 1px solid var(--line);
  color: var(--dim); margin-left: 6px; }
.badge.bad { color: var(--bad); border-color: var(--bad); }
.badge.new { color: var(--warn); border-color: var(--warn); }
#viewer { position: fixed; inset: 0; background: rgba(0,0,0,.92); display: none; z-index: 10;
  align-items: center; justify-content: center; flex-direction: column; padding: 12px; }
#viewer.open { display: flex; }
#viewer img { max-width: 100%; max-height: calc(100% - 40px); object-fit: contain; }
#viewer p { color: #dbe4ee; margin: 8px 0 0; font-size: 14px; text-align: center; }
"""

PAGE_SCRIPT = """
const viewer = document.getElementById('viewer'), big = viewer.querySelector('img'), words = viewer.querySelector('p');
document.querySelectorAll('figure img').forEach(img => img.addEventListener('click', () => {
  big.src = img.dataset.full; words.textContent = img.alt; viewer.classList.add('open'); }));
viewer.addEventListener('click', () => { viewer.classList.remove('open'); big.removeAttribute('src'); });
document.addEventListener('keydown', e => { if (e.key === 'Escape') viewer.click(); });
const search = document.getElementById('search'), only = document.getElementById('only-problems');
function filter() {
  const words = search.value.toLowerCase();
  document.querySelectorAll('section.screen').forEach(s => {
    const shown = s.dataset.name.includes(words) && (!only.checked || s.dataset.problems !== '0');
    s.style.display = shown ? '' : 'none'; });
}
search.addEventListener('input', filter); only.addEventListener('change', filter);
"""


def problem_list(problems: list, new=None) -> str:
    if not problems:
        return ""
    lines = []
    for problem in problems[:30]:
        mark = ' <span class="badge new">new</span>' if new is not None and problem_key(problem) in new else ""
        lines.append(f'<li><span class="check {problem["check"]}">{problem["check"]}</span>: '
                     f'{html.escape(problem["message"])}{mark}</li>')
    if len(problems) > 30:
        lines.append(f"<li>... and {len(problems) - 30} more (report.json)</li>")
    return '<ul class="problems">' + "".join(lines) + "</ul>"


def figure(path: str, thumbnail: str, alt: str, caption: str) -> str:
    return (f'<figure><img loading="lazy" src="{html.escape(thumbnail)}" data-full="{html.escape(path)}"'
            f' alt="{html.escape(alt)}"><figcaption>{caption}</figcaption></figure>')


def full_of(out: Path, label: str, variant: str, screen: Screen) -> str:
    stem = f"{label}/{variant}/{screen.slug}"
    return f"{stem}.jpg" if (out / f"{stem}.jpg").exists() else f"{stem}.png"


def thumbnail_of(out: Path, label: str, variant: str, screen: Screen) -> str:
    stem = f"{label}/{variant}/{screen.slug}"
    return f"{stem}.thumb.jpg" if (out / f"{stem}.thumb.jpg").exists() else f"{stem}.thumb.png" \
        if (out / f"{stem}.thumb.png").exists() else f"{stem}.png"


def note_list(notes: list) -> str:
    """a screen's notes (tools/menu_notes.py's), each with its status and picture"""
    if not notes:
        return ""
    lines = []
    for note in notes:
        status = "open" if note.get("status") == "open" else f"done {str(note.get('commit', ''))[:9]}"
        widget = (note.get("widget") or "").rsplit("\\", 1)[-1] or "the screen"
        picture = (f' <a href="{html.escape(Path(note["picture"]).as_uri())}">picture</a>'
                   if note.get("picture") else "")
        lines.append(f'<li><span class="check">{html.escape(status)}</span>: {html.escape(widget)}: '
                     f'{html.escape(note.get("note", ""))}{picture}</li>')
    return '<ul class="problems notes">' + "".join(lines) + "</ul>"


def write_page(out: Path, screens: list, variants: list, after: dict, problems: dict, before: dict = None,
               before_problems: dict = None, title: str = "", elapsed: float = 0.0, notes: dict = None) -> Path:
    sections = []
    unchanged = 0
    notes = notes or {}
    total_problems = sum(len(found) for found in problems.values())
    for screen in screens:
        cells = []
        changed = False
        count = 0
        for name in variants:
            result = after.get((screen.name, name), {})
            found = problems.get((screen.name, name), [])
            count += len(found)
            caption = html.escape(VARIANTS[name].description)
            if "error" in result:
                cells.append(f'<div><p class="meta">{caption}</p><p class="error">{html.escape(result["error"])}'
                             "</p></div>")
                changed = True
                continue
            if not result.get("opened", True):
                caption += ' <span class="badge">no layout: the screen closed, or a keyboard is up</span>'
            if found:
                caption += f' <span class="badge bad">{len(found)}</span>'
            if "picture" not in result and before is None:
                cells.append(f'<div><p class="meta">{caption}</p>{problem_list(found)}</div>')
                continue
            full = full_of(out, "after", name, screen)
            thumbnail = thumbnail_of(out, "after", name, screen)
            if before is not None:
                previous = before.get((screen.name, name), {})
                if not results_differ(previous, result) and screen.name not in notes:
                    continue
                changed = True
                new = set(map(problem_key, found)) - set(map(problem_key, before_problems.get((screen.name, name),
                                                                                              [])))
                if "picture" not in result:
                    cells.append(f'<div><p class="meta">{caption}: its layout changed</p>'
                                 f"{problem_list(found, new)}</div>")
                    continue
                left = figure(full_of(out, "before", name, screen), thumbnail_of(out, "before", name, screen),
                              f"{screen.label}, before, {VARIANTS[name].description}", "before")
                right = figure(full, thumbnail, f"{screen.label}, after, {VARIANTS[name].description}", "after")
                cells.append(f'<div><p class="meta">{caption}</p><div class="pair">{left}{right}</div>'
                             f"{problem_list(found, new)}</div>")
            else:
                alt = f"{screen.label}, {VARIANTS[name].description}"
                cells.append(f"<div>{figure(full, thumbnail, alt, caption)}{problem_list(found)}</div>")
        if before is not None and not changed and screen.name not in notes:
            unchanged += 1
            continue
        generator = f" &middot; {html.escape(screen.generator)}" if screen.generator else ""
        badge = f'<span class="badge bad">{count} issues</span>' if count else ""
        sections.append(
            f'<section class="screen" data-name="{html.escape(screen.name.lower())}" data-problems="{count}">'
            f'<h2>{html.escape(screen.label)}{badge}</h2><p class="meta">{html.escape(screen.name)} &middot; '
            f'{html.escape(screen.file)}{generator}</p>{note_list(notes.get(screen.name, []))}'
            f'<div class="row">{"".join(cells)}</div></section>')
    summary = f"{len(screens)} screens, {len(variants)} variants, {total_problems} layout issues"
    if before is not None:
        summary = (f"{len(sections)} screens differ, {unchanged} unchanged (of {len(screens)}), "
                   f"{len(variants)} variants, {total_problems} layout issues after")
    if elapsed:
        summary += f", made in {elapsed / 60:.1f} min"
    page = f"""<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>Menu gallery</title>
<style>{PAGE_STYLE}</style></head><body>
<header><h1>Menu gallery</h1><p class="summary">{html.escape(title)}</p><p class="summary">{summary}</p>
<div class="filters"><input id="search" type="search" placeholder="Filter screens by name">
<label><input id="only-problems" type="checkbox"> only screens with issues</label></div></header>
<main>{"".join(sections) or '<p class="summary">No screen differs.</p>'}</main>
<div id="viewer"><img alt=""><p></p></div><script>{PAGE_SCRIPT}</script></body></html>
"""
    path = out / "index.html"
    path.write_text(page)
    return path


# ---------- --remote

REMOTE_OWN = ("--remote", "--remote-tree", "--remote-data", "--out", "--data", "--build", "--jobs", "--trees")


def remote_arguments(argv) -> list:
    words, index = [], 0
    while index < len(argv):
        word = argv[index]
        name, equals, _ = word.partition("=")
        index += 1
        if name in REMOTE_OWN:
            if not equals:
                index += 1
            continue
        words.append(shlex.quote(word))
    return words


def remote(arguments, argv) -> int:
    """the gallery made on --remote's host from this commit and its uncommitted changes, its folder put in
    --out: the exit status"""
    started = time.monotonic()
    commit = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True,
                            check=True).stdout.strip()
    diff = subprocess.run(["git", "-C", str(ROOT), "diff", "--binary", "HEAD"], capture_output=True,
                          check=True).stdout
    tree = capture.remote_path(arguments.remote_tree)
    setup = f"""set -e
tree={tree}
if [ ! -d "$tree/.git" ] && [ ! -f "$tree/.git" ]; then
  git -C "$HOME/openh1-vulkan-check" worktree add --detach "$tree" HEAD >/dev/null
  mkdir -p "$tree/build"
  cp -a "$HOME/openh1-vulkan-check/build/linux_x86_64" "$tree/build/"
  cp -a "$HOME/openh1-vulkan-check/build/msvc_semantics" "$tree/build/" 2>/dev/null || true
  rm -rf "$tree/build/linux_x86_64/sdl3-build/CMakeCache.txt" "$tree/build/linux_x86_64/sdl3-build/CMakeFiles"
fi
git -C "$tree" rev-parse --git-dir
"""
    result = subprocess.run(["ssh", "-o", "BatchMode=yes", arguments.remote, "bash -s"], input=setup.encode(),
                            capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors="replace"), file=sys.stderr)
        return result.returncode
    git_dir = result.stdout.decode().strip().splitlines()[-1]
    # (this commit sent to the clone, by git's own protocol over ssh)
    remote_tree = arguments.remote_tree.replace("~/", "")
    refs = [f"{commit}:refs/menu-gallery/head"]
    # (--compare's ref as its commit, sent too: the clone's own branches are not this one's)
    # (else a build folder of the remote's)
    compared = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", f"{arguments.compare}^{{commit}}"],
                              capture_output=True, text=True) if arguments.compare else None
    if compared and compared.returncode == 0:
        sha = compared.stdout.strip()
        refs.append(f"{sha}:refs/menu-gallery/compare")
        argv = [sha if word == arguments.compare else word.replace(f"={arguments.compare}", f"={sha}")
                for word in argv]
    push = subprocess.run(["git", "-C", str(ROOT), "push", "-q", "-f", f"{arguments.remote}:{remote_tree}"] + refs,
                          capture_output=True, text=True)
    if push.returncode:
        print(f"menu_gallery: could not push to {arguments.remote}:{remote_tree} ({git_dir})\n{push.stderr}",
              file=sys.stderr)
        return 1
    command = " ".join(["python3", "tools/menu_gallery.py", "--out", '"$out"', "--data",
                        capture.remote_path(arguments.remote_data)] + remote_arguments(argv))
    end = "MENU_GALLERY_DIFF_END"
    script = f"""gallery_remote() {{
tree={tree}
# (a cmake of pip's, where the box has none of its own)
for bin in "$HOME/.cache/capture-venv/bin" "$HOME/.local/bin"; do [ -d "$bin" ] && PATH="$PATH:$bin"; done
cd "$tree" || exit 1
git checkout -q --detach -f {commit} 1>&2 || exit 1
git clean -q -fd -- port tools source 1>&2
base64 -d > "$tree/.menu-gallery.diff" <<'{end}'
{__import__('base64').b64encode(diff).decode()}
{end}
if [ -s "$tree/.menu-gallery.diff" ]; then git apply --whitespace=nowarn "$tree/.menu-gallery.diff" 1>&2 || exit 1; fi
rm -f "$tree/.menu-gallery.diff"
python3 configure.py $(sed -n 's/^configure_args = //p' build.ninja) >/dev/null 2>&1 || python3 configure.py --compiler-launcher ccache 1>&2
ninja linux_x86_64 > "$tree/build/menu-gallery-ninja.log" 2>&1 || {{ tail -40 "$tree/build/menu-gallery-ninja.log" 1>&2; exit 1; }}
# (the runs' folders and the pictures beside the build, not in a small /tmp)
mkdir -p "$tree/build/menu-gallery-tmp"
export TMPDIR="$tree/build/menu-gallery-tmp"
out=$(mktemp -d "$TMPDIR/out-XXXXXX")
trap 'rm -rf "$out"' EXIT
{command} --jobs {arguments.jobs or 16} 1>&2
status=$?
tar -C "$out" --exclude=trees -cf - .
exit $status
}}
gallery_remote < /dev/null
"""
    limit = 7200
    result = subprocess.run(["ssh", "-o", "BatchMode=yes", arguments.remote, "bash -s"], input=script.encode(),
                            stdout=subprocess.PIPE, timeout=limit)
    out = arguments.out
    out.mkdir(parents=True, exist_ok=True)
    try:
        with tarfile.open(fileobj=io.BytesIO(result.stdout)) as archive:
            members = [member for member in archive.getmembers()
                       if (member.isfile() or member.isdir()) and not member.name.startswith("/") and
                       ".." not in Path(member.name).parts]
            archive.extractall(out, members=members, filter="data")
    except tarfile.TarError:
        print(f"menu_gallery: nothing came back from {arguments.remote}", file=sys.stderr)
        return result.returncode or 1
    print(f"menu_gallery: {arguments.remote}: {time.monotonic() - started:.0f} s in all", file=sys.stderr)
    print(out / "index.html")
    return result.returncode


# ---------- main

def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Every menu screen in each variant, as an HTML contact sheet, "
                                                 "with layout checks.", allow_abbrev=False)
    parser.add_argument("--out", type=Path, default=ROOT / "build" / "menu_gallery",
                        help="the folder the page and pictures go to (build/menu_gallery)")
    parser.add_argument("--build", type=Path, help="the build folder (build/macos; build/linux_x86_64)")
    parser.add_argument("--data", type=Path, help="the maps folder")
    parser.add_argument("--compare", metavar="REF_OR_BUILD",
                        help="a git ref (built in --trees) or a build folder: show only the screens that differ")
    parser.add_argument("--variants", type=parse_variants, default=parse_variants("default"),
                        help=f"default, all, or some of {', '.join(VARIANTS)}")
    parser.add_argument("--screens", default="", help="only the screens whose name has this in it (a regex)")
    parser.add_argument("--vr", action="store_true", help="VR's settings screens too (drawn flat)")
    parser.add_argument("--trees", type=Path, default=ROOT / "build" / "menu_gallery_trees",
                        help="where --compare's git refs are checked out and built (build/menu_gallery_trees)")
    parser.add_argument("--check", action="store_true",
                        help="the layout checks alone: no pictures, no renderer (seconds; CI's)")
    parser.add_argument("--regenerate", action="store_true",
                        help="write the port's own screens again first (tools/port_settings.py)")
    parser.add_argument("--build-menus", action="store_true",
                        help="the build's own menu files, not this tree's (port/assets/menus) laid over them")
    parser.add_argument("--notes", nargs="?", type=Path, const=ROOT / "build" / "menu_notes.jsonl", metavar="FILE",
                        help="only the screens the menu studio's notes are on, each with its notes, before | after"
                             " whether it changed or not (tools/menu_notes.py; build/menu_notes.jsonl)")
    parser.add_argument("--list", action="store_true", help="print the screens and variants and exit")
    parser.add_argument("--jobs", type=int, default=0, help="runs at a time (half the processors)")
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds a run may take")
    parser.add_argument("--xvfb", action="store_true", help="Linux: each run on a virtual X display of its own")
    parser.add_argument("--no-fail", action="store_true", help="exit 0 whatever the layout checks find")
    parser.add_argument("--remote", metavar="HOST", help="make it on that Linux host over ssh")
    parser.add_argument("--remote-tree", default="~/openh1-menu-gallery", metavar="PATH",
                        help="with --remote, the clone there (made from ~/openh1-vulkan-check's when missing)")
    parser.add_argument("--remote-data", default="~/halo-x86-maps/maps", metavar="PATH",
                        help="with --remote, the maps folder there")
    return parser


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else list(argv)
    arguments = make_parser().parse_args(argv)
    screens = enumerate_screens(ROOT / MENUS, arguments.vr)
    if arguments.screens:
        pattern = re.compile(arguments.screens)
        screens = [screen for screen in screens if pattern.search(screen.name)]
    notes = {}
    if arguments.notes:
        import menu_notes
        for note in menu_notes.read_notes(arguments.notes):
            notes.setdefault(note.get("screen", ""), []).append(note)
        screens = [screen for screen in screens if screen.name in notes]
        if not screens:
            print(f"menu_gallery: no screen has a note in {arguments.notes}", file=sys.stderr)
            return 1
    if arguments.list:
        for screen in screens:
            print(f"{screen.name}  ({screen.file}, {screen.generator or 'by hand'})")
        for name in arguments.variants:
            print(f"variant {name}: {VARIANTS[name].description}, {VARIANTS[name].size[0]}x{VARIANTS[name].size[1]}")
        return 0
    if arguments.remote:
        return remote(arguments, argv)
    started = time.monotonic()
    if arguments.regenerate:
        subprocess.run([sys.executable, str(ROOT / "tools" / "port_settings.py")], check=True)
        screens = [screen for screen in enumerate_screens(ROOT / MENUS, arguments.vr)
                   if (not arguments.screens or re.search(arguments.screens, screen.name)) and
                   (not notes or screen.name in notes)]
    menus = None if arguments.build_menus else ROOT / "port" / "assets" / "menus"
    pictures = not arguments.check
    jobs = arguments.jobs or max(1, (os.cpu_count() or 2) // 2)
    build = (arguments.build or capture.default_build(ROOT)).resolve()
    maps = (arguments.data or capture.default_maps(ROOT)).resolve()
    if not (build / "halo").is_file():
        print(f"menu_gallery: no {build / 'halo'} (--build)", file=sys.stderr)
        return 1
    if not (maps / "ui.map").is_file():
        print(f"menu_gallery: no ui.map in {maps} (--data)", file=sys.stderr)
        return 1
    out = arguments.out.resolve()
    for label in ("after", "before"):
        shutil.rmtree(out / label, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    before = before_problems = None
    title = subprocess.run(["git", "-C", str(ROOT), "describe", "--always", "--dirty"], capture_output=True,
                           text=True).stdout.strip()
    if arguments.compare:
        other = Path(arguments.compare).expanduser()
        other_menus = None
        if not (other / "halo").is_file():
            other, other_menus = ref_runner(ROOT, arguments.compare, arguments.trees, build)
        title = f"{arguments.compare} | {title}"
        before = render(other, screens, arguments.variants, maps, out, "before", jobs, arguments.timeout,
                        arguments.xvfb, other_menus, pictures)
        before_problems = {key: check_layout(result["layout"], VARIANTS[key[1]]) if result.get("layout") else []
                           for key, result in before.items()}
    after = render(build, screens, arguments.variants, maps, out, "after", jobs, arguments.timeout, arguments.xvfb,
                   menus, pictures)
    problems = {key: check_layout(result["layout"], VARIANTS[key[1]]) if result.get("layout") else []
                for key, result in after.items()}
    elapsed = time.monotonic() - started
    page = write_page(out, screens, arguments.variants, after, problems, before, before_problems, title, elapsed,
                      notes)
    report = {"screens": len(screens), "variants": arguments.variants, "seconds": round(elapsed, 1),
              "problems": [{"screen": key[0], "variant": key[1], **problem}
                           for key, found in problems.items() for problem in found],
              "errors": [{"screen": key[0], "variant": key[1], "error": result["error"]}
                         for key, result in after.items() if "error" in result],
              "not_opened": [{"screen": key[0], "variant": key[1]} for key, result in after.items()
                             if result.get("opened") is False]}
    if before is not None:
        report["new_problems"] = [entry for entry in report["problems"]
                                  if problem_key(entry) not in set(map(problem_key, before_problems.get(
                                      (entry["screen"], entry["variant"]), [])))]
    (out / "report.json").write_text(json.dumps(report, indent=1))
    if arguments.check:
        listed = report.get("new_problems", report["problems"])
        for entry in listed:
            if before is not None or entry["check"] not in ADVISORY:
                print(f"{entry['variant']}: {entry['screen']}: {entry['check']}: {entry['message']}")
        advisory = [entry for entry in listed if before is None and entry["check"] in ADVISORY]
        if advisory:
            print(f"({len(advisory)} screens with {', '.join(sorted(ADVISORY))} issues: report.json)")
    print(f"menu_gallery: {len(report['problems'])} layout issues, {len(report['errors'])} runs failed,"
          f" {elapsed:.1f} s", file=sys.stderr)
    print(page)
    failing = report["new_problems"] if before is not None else [
        problem for problem in report["problems"] if problem["check"] not in ADVISORY]
    return 0 if arguments.no_fail or (not failing and not report["errors"]) else 1


if __name__ == "__main__":
    sys.exit(main())
