#!/usr/bin/env python3
"""The high-res text's letter spacing, measured string by string (issue 744).

The game lays its text out from its font tags' character widths, whole
units at 480 lines (source/text/draw_string.c), and the high-res text draws
each character with a glyph of the font that stands for the tag
(port/linux/src/text_hires.c). Where each glyph goes is
source/rasterizer/rasterizer_text.c's hires_text_glyph_offset; this is the
same layout in Python, both ways it has been done:

- "advance": each glyph's advance centred on its character's width (as the
  high-res text drew before), which gives each letter the difference between
  the tag's whole-unit width and the font's advance as space either side;
- "run": a run of text (a line's characters up to a tab, an icon or a line's
  end) set with the font's own advances, every character as much closer or
  further apart as makes the run's ink as wide as the tag's, on which it is
  laid (as it draws now).

It lays out every string the menus show (port/assets/menus/ce: the widgets'
text and the string lists, the settings screens' labels and help) and the
maps' string lists (each map's ustr tags: the HUD's messages and prompts,
the campaign's and multiplayer's text) in the three tags the high-res text
draws with Cyborg Interface Gothic, and measures two things, in hundredths
of the tag's capital height:

- unevenness: for each two neighbouring letters of a run, how much closer or
  further apart (for each advance between them) they are than the font
  spaces them, less the run's mean: the letters' spacing against the font's
  own (Cyborg Interface Gothic carries no kerning, so its own spacing is its
  advances and side bearings), whatever the run's tracking;
- place: how far the middle of the run's ink is from the Xbox's (the tag's
  bitmaps').

    python3 tools/text_spacing.py --map assets/maps/ui.map --maps assets/maps
    python3 tools/text_spacing.py --place advance ...    # as it was drawn before

tools/test_text_spacing.py runs it over the maps, and fails if a string's
spacing is uneven or its place moved (it needs the maps: the tags are
theirs).
"""

import argparse
import html
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

from fontTools.ttLib import TTFont  # noqa: E402

FONTS = ROOT / "port" / "assets" / "fonts"
MENUS = ROOT / "port" / "assets" / "menus" / "ce"
# the tags the high-res text draws with Cyborg Interface Gothic (fonts.json)
TAGS = {"ui\\large_ui": "Overpass-900.ttf", "ui\\interstate": "Overpass-900.ttf", "ui\\small_ui": "Overpass-750.ttf"}
FIT_LETTERS = [chr(ord("A") + i) for i in range(26)] + [chr(ord("a") + i) for i in range(26)]
# the icons draw_string_and_hack_in_icons draws in text ("%x-button"): a run ends at one
ICON = re.compile(r"%[a-z][a-z0-9_-]*")


class TagFont:
    """A font tag of a map: its ascent and its characters' widths and bitmaps."""

    def __init__(self, xbox_map, name):
        address = xbox_map.tags[("font", name)]
        header = xbox_map.read(address, 0x9C)
        _, self.ascending_height = struct.unpack_from("<Ih", header, 0)
        count, characters = struct.unpack_from("<II", header, 0x7C)
        size, _, _, pixels = struct.unpack_from("<iiiI", header, 0x88)
        data = xbox_map.read(pixels, size)
        self.name = name
        self.characters = {}
        for index in range(count):
            code, width, bitmap_width, bitmap_height, origin_x, origin_y, _, _, offset = struct.unpack(
                "<HhhhhhhhI", xbox_map.read(characters + index * 0x14, 0x14))
            bitmap = data[offset:offset + bitmap_width * bitmap_height] \
                if bitmap_width > 0 and bitmap_height > 0 else b""
            self.characters[code] = (width, bitmap_width, bitmap_height, origin_x, bitmap)
        # the capital H's rows with ink (rasterizer_text.c's hires_text_font_get)
        _, bw, bh, _, bitmap = self.characters[ord("H")]
        rows = [row for row in range(bh) if any(bitmap[row * bw:(row + 1) * bw])]
        self.cap_height = rows[-1] - rows[0] + 1
        self._ink = {}

    def width(self, code):
        character = self.characters.get(code)
        return character[0] if character else None

    def ink(self, code):
        """The character's ink, left and right from the pen, in units
        (rasterizer_text.c's font_character_ink_span); None for none."""
        if code not in self._ink:
            self._ink[code] = None
            character = self.characters.get(code)
            if character and character[4]:
                _, bw, bh, origin_x, bitmap = character
                columns = [max(bitmap[row * bw + column] for row in range(bh)) for column in range(bw)]
                inked = [column for column, value in enumerate(columns) if value]
                if inked:
                    first, last = inked[0], inked[-1]
                    self._ink[code] = (first + 1.0 - columns[first] / 255.0 - origin_x,
                                       last + columns[last] / 255.0 - origin_x)
        return self._ink[code]


class Face:
    """A font file as text_hires.c's face_make sizes it for a tag."""

    def __init__(self, path, tag):
        self.font = TTFont(path)
        self.cmap = self.font.getBestCmap()
        self.metrics = self.font["hmtx"].metrics
        self.glyf = self.font["glyf"]
        h = self.glyf[self.cmap[ord("H")]]
        self.scale = tag.cap_height / float(h.yMax - h.yMin)
        tag_width = font_width = 0.0
        for letter in FIT_LETTERS:
            width = tag.width(ord(letter))
            if not width or width <= 0 or ord(letter) not in self.cmap:
                continue
            tag_width += width
            font_width += self.metrics[self.cmap[ord(letter)]][0] * self.scale
        self.narrowing = tag_width / font_width if font_width > tag_width > 0 else 1.0
        self._glyphs = {}

    def has(self, code):
        return code in self.cmap

    def glyph(self, code):
        """(advance, ink left, ink right) in units, as drawn (narrowed); ink
        None for a glyph with none."""
        if code not in self._glyphs:
            name = self.cmap[code]
            x = self.scale * self.narrowing
            glyph = self.glyf[name]
            if getattr(glyph, "numberOfContours", 0):
                glyph.recalcBounds(self.glyf)
                self._glyphs[code] = (self.metrics[name][0] * x, glyph.xMin * x, glyph.xMax * x)
            else:
                self._glyphs[code] = (self.metrics[name][0] * x, None, None)
        return self._glyphs[code]


class Font:
    """The high-res text's font for a tag: Cyborg Interface Gothic, and its
    fallback for what it lacks (fonts.json)."""

    def __init__(self, tag):
        self.faces = (Face(FONTS / "CyborgInterfaceGothic-Regular.ttf", tag), Face(FONTS / TAGS[tag.name], tag))

    def glyph(self, code):
        for face in self.faces:
            if face.has(code):
                return face.glyph(code)
        return None


# the widest line wrapped text is drawn in (the settings' help panel's, some
# 200 units): longer lines are wrapped there, at spaces, as the game wraps them
WRAP = 200


def runs(text, tag):
    """A string's runs: its lines (wrapped at WRAP units), cut at tabs, at the
    icons in it, and at a "|" (the settings' help's note after it)."""
    for line in re.split(r"\\n|\n|\r|\|n|\t|\|", text):
        for run in ICON.split(line):
            words, width = [], 0
            for word in run.split(" "):
                word_width = sum(tag.width(ord(c)) or 0 for c in word + " ")
                if words and width + word_width > WRAP:
                    yield " ".join(words)
                    words, width = [], 0
                words.append(word)
                width += word_width
            if any(words):
                yield " ".join(words)


def lay_out(tag, font, run, place):
    """Each character of a run: (code, the Xbox's ink, the high-res text's
    ink, its font advance), in units from the run's pen; None if the
    high-res text draws the string from the tag's bitmaps (a character
    neither font has)."""
    codes = [ord(c) for c in run]
    if any(code >= 32 and font.glyph(code) is None for code in codes):
        return None
    pens, offsets = [], []
    pen = 0.0
    for code in codes:
        pens.append(pen)
        pen += tag.width(code) or 0
    if place == "run":
        offsets = run_offsets(tag, font, codes)
    if not offsets:
        offsets = [((tag.width(code) or 0) - font.glyph(code)[0]) * 0.5 for code in codes]
    out = []
    for code, pen, offset in zip(codes, pens, offsets):
        if tag.width(code) is None:
            continue
        advance, left, right = font.glyph(code)
        stock = tag.ink(code)
        out.append((code, (pen + stock[0], pen + stock[1]) if stock else None,
                    (pen + offset + left, pen + offset + right) if left is not None else None, advance))
    return out


def run_offsets(tag, font, codes):
    """rasterizer_text.c's hires_text_run_lay_out: each glyph's advance's
    left from its pen; [] where it centres each glyph on its character."""
    tag_pens, font_pens, advances = [], [], []
    tag_pen = font_pen = 0.0
    first = last = None
    for index, code in enumerate(codes):
        tag_pens.append(tag_pen)
        font_pens.append(font_pen)
        advances.append(0.0)
        width = tag.width(code)
        if width is None:
            continue
        advance, ink_left, ink_right = font.glyph(code)
        advances[index] = advance
        stock = tag.ink(code)
        if stock and ink_left is not None:
            if first is None:
                first = index
                tag_left, font_left = tag_pen + stock[0], font_pen + ink_left
            last = index
            tag_right, font_right = tag_pen + stock[1], font_pen + ink_right
        tag_pen += width
        font_pen += advance
    if first is None:
        return []
    tracking = ((tag_right - tag_left) - (font_right - font_left)) / (last - first) if last > first else 0.0
    if abs(tracking) > tag.ascending_height * 0.1:
        tracking = 0.0
    limit = tag.ascending_height
    offsets = []
    for index, code in enumerate(codes):
        width = tag.width(code)
        centred = (width - advances[index]) * 0.5 if width is not None else 0.0
        offset = font_pens[index] + (index - first) * tracking - tag_pens[index] + \
            ((tag_left + tag_right) - (font_left + font_right + (last - first) * tracking)) * 0.5
        offsets.append(min(max(offset, centred - limit), centred + limit))
    return offsets


def measure_run(tag, font, run, place):
    """A run's unevenness (each pair's, in hundredths of the capital height)
    and its place (its ink's ends against the Xbox's, likewise); None for a
    run drawn from the tag's bitmaps or with no ink."""
    laid = lay_out(tag, font, run, place)
    if not laid:
        return None
    inked = [(index, item) for index, item in enumerate(laid) if item[1] and item[2]]
    if not inked:
        return None
    pairs = []
    for (i, a), (j, b) in zip(inked, inked[1:]):
        # (the font's own gap: its advances between the two inks)
        own = sum(item[3] for item in laid[i:j]) - font.glyph(a[0])[2] + font.glyph(b[0])[1]
        drawn = b[2][0] - a[2][1]
        pairs.append((chr(a[0]) + chr(b[0]), (drawn - own) / (j - i)))
    unit = 100.0 / tag.cap_height
    mean = sum(error for _, error in pairs) / len(pairs) if pairs else 0.0
    uneven = [(pair, (error - mean) * unit) for pair, error in pairs]
    first, last = inked[0][1], inked[-1][1]
    # (its ink's middle: where the run's ink is wider or narrower than the
    # tag's by more than its tracking may spread, it is centred on the tag's)
    place_error = abs((first[2][0] + last[2][1]) - (first[1][0] + last[1][1])) * 0.5 * unit
    return uneven, place_error, mean * unit


def menu_strings():
    """The menus' strings (their widgets' text and string lists), each with
    the font it is drawn in where the menus say (None: any)."""
    found = {}
    for path in sorted(MENUS.glob("*.xml")):
        for element in re.finditer(r"<(widget|string)\b([^>]*)>", path.read_text(encoding="utf-8")):
            attributes = dict(re.findall(r'(\w+)="([^"]*)"', element.group(2)))
            words = html.unescape(attributes.get("text", ""))
            if words.strip():
                found.setdefault(words, set()).add(attributes.get("font"))
    return found


def map_strings(xbox_map):
    """The unicode string lists of a map (its ustr tags)."""
    found = set()
    for (group, _), address in xbox_map.tags.items():
        if group != "ustr":
            continue
        count, entries = struct.unpack_from("<II", xbox_map.read(address, 12), 0)
        for index in range(count):
            size, _, _, data = struct.unpack_from("<iiiI", xbox_map.read(entries + index * 20, 20), 0)
            words = xbox_map.read(data, size).decode("utf-16-le", "replace").split("\0")[0]
            if words.strip():
                found.add(words)
    return found


def every_string(ui_map, maps):
    from hud_assets import XboxMap
    xbox_map = XboxMap(Path(ui_map))
    tags = {name: TagFont(xbox_map, name) for name in TAGS if ("font", name) in xbox_map.tags}
    strings = menu_strings()
    for words in map_strings(xbox_map):
        strings.setdefault(words, set()).add(None)
    for path in sorted(Path(maps).glob("*.map")) if maps else []:
        if path.name == "ui.map":
            continue
        for words in map_strings(XboxMap(path)):
            strings.setdefault(words, set()).add(None)
    return tags, strings


def measure(tags, strings, place):
    """Per tag: each run's (worst unevenness, its pair, place error, run,
    its letters' tracking from the font's own spacing)."""
    results = {}
    for name, tag in tags.items():
        font = Font(tag)
        rows = []
        for text, fonts in strings.items():
            if None not in fonts and name not in fonts:
                continue
            for run in runs(text, tag):
                measured = measure_run(tag, font, run, place)
                if not measured:
                    continue
                uneven, place_error, tracking = measured
                worst = max(uneven, key=lambda item: abs(item[1]), default=("", 0.0))
                rows.append((abs(worst[1]), worst[0], place_error, run, abs(tracking)))
        results[name] = rows
    return results


def report(results, uneven_limit, place_limit, worst=10, out=print):
    """Totals per tag and the worst runs; whether every run is within the limits."""
    passed = True
    for name, rows in results.items():
        uneven = [row for row in rows if row[0] > uneven_limit]
        moved = [row for row in rows if row[2] > place_limit]
        passed = passed and not uneven and not moved
        out(f"{name}: {len(rows)} runs; {len(uneven)} with a pair spaced unevenly by more than {uneven_limit}% "
            f"of the capital height (worst {max((r[0] for r in rows), default=0):.1f}%), {len(moved)} whose ink's "
            f"middle moved from the Xbox's by more than {place_limit}% (worst {max((r[2] for r in rows), default=0):.1f}%); "
            f"mean unevenness {sum(r[0] for r in rows) / max(len(rows), 1):.1f}%, letters tracked "
            f"{sorted(r[4] for r in rows)[len(rows) // 2] if rows else 0:.1f}% (median) to "
            f"{max((r[4] for r in rows), default=0):.1f}% from the font's own spacing")
        for row in sorted(rows, key=lambda r: -r[0])[:worst]:
            out(f"    {row[0]:5.1f}%  {row[1]!r:6} in {row[3][:60]!r}")
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--map", default=str(ROOT / "assets" / "maps" / "ui.map"))
    parser.add_argument("--maps", default=None, help="a folder of maps whose string lists to add")
    parser.add_argument("--place", choices=("advance", "run"), default="run")
    parser.add_argument("--uneven", type=float, default=3.0, help="hundredths of the capital height")
    parser.add_argument("--moved", type=float, default=3.0, help="hundredths of the capital height")
    parser.add_argument("--worst", type=int, default=10)
    args = parser.parse_args()
    tags, strings = every_string(args.map, args.maps)
    print(f"{len(strings)} strings, placed by {args.place}")
    sys.exit(0 if report(measure(tags, strings, args.place), args.uneven, args.moved, args.worst) else 1)


if __name__ == "__main__":
    main()
