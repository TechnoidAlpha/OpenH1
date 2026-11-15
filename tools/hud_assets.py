#!/usr/bin/env python3
"""Makes the high-res HUD textures (port/assets/hud/*.png) from Aerocatia's and
Jessie's high-resolution HUD art in Halo PC Restored (release r220: its tags'
HUD sheets, the sniper rifle's scope ladder in its weapon's folder, and the
meters in the Xbox channel order in its extra/xbox_order_hud_meters; see
port/assets/hud/NOTICE.md), laid out as the Xbox maps' sheets:

    python tools/hud_assets.py layout --map assets/maps/bloodgulch.map --hek ../halopc-restored
    python tools/hud_assets.py labels --map assets/maps/bloodgulch.map
    python tools/hud_assets.py build
    python tools/hud_assets.py thresholds  # update only discrete segment data
    python tools/hud_assets.py check --map assets/maps/bloodgulch.map --out /tmp/hud_check

(--hek: a checkout of Halo PC Restored at the tag r220.)

The game draws a HUD bitmap at its tag's size and samples it with normalised
coordinates, so a texture of a whole multiple of its size in the same layout
draws in its place unchanged (port/linux/src/hud_hires.c swaps it in as the
bitmap is uploaded). The restored sheets are 4x the Xbox bitmaps (a few at
their size), and the PC tags have more sprites than the Xbox ones and place
some elsewhere, so each Xbox sprite (or bitmap) is copied from the restored
sprite whose shape matches it best, aligned by its content, at the art's own
scale. The way of matching them follows ChupathingyCE's tools/hud_assets.py
(GPL-3.0-or-later), which first fitted this art to the Xbox sheets.

The sheets with English text (the message icons, the reticle's warnings) are
textures of their sprites (SPRITE_SHEETS), each checked on its own cell's
texels, so that another language's maps draw the pictures from the art and
their own words; the other textures are checked on the whole bitmap's, and
another language's keep their own (the seat plates' text, upscaled).

Where the restored art is no larger than the Xbox bitmap (the motion
sensor's blips and sweeper, the health meter's base), the texture keeps the
SVG redraw in port/assets/hud/svg it had, at 8x; where the art says other
than the Xbox's (the sniper's 10x, REDRAWN), the cell is set in a font
(labels).

layout: reads the Xbox map's HUD bitmaps (sizes, formats, sprite rectangles,
    pixels) and the restored tags' sprites and pixels, matches and aligns
    each cell, and writes port/assets/hud/layout.json (numbers only), with
    the restored sheets used, decoded unchanged, as PNGs in
    port/assets/hud/art. A bitmap with a cell that the art does not match
    keeps its entry in layout.json as it was (its redraws). Needs a map and
    the restored tags; its output is committed.
labels: sets the sniper's zoom labels (LABELS) in Cyborg Interface Gothic,
    each fitted to the map's (size, place, spacing and weight), into
    port/assets/hud/svg and their cells of layout.json.
build: draws port/assets/hud/art and svg with layout.json into
    port/assets/hud/*.png (committed; the builds embed them).
check: compares each PNG, reduced to the tag's size, with the map's bitmap
    (only its sprites' cells, for a texture that stands for some sprites)
    and writes side-by-side images into --out.

Needs rsvg-convert, Pillow, NumPy and SciPy, and fontTools for the labels.
"""

import argparse
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "port/assets/hud"
LAYOUT = ASSETS / "layout.json"
# Aerocatia's and Jessie's art (NOTICE.md)
ART = ASSETS / "art"
# no texture's side longer than every phone's GPU can take (the textures are
# the restored art's scale, 4x the Xbox bitmaps)
MAXIMUM_SIZE = 2048

# the Xbox maps' tag data is loaded here (cache_files.c)
XBOX_TAG_BASE = 0x803A6000
BITMAP_LINEAR_FLAG = 0x10
FORMATS = {0: "a8", 1: "y8", 2: "ay8", 3: "a8y8", 6: "r5g6b5", 8: "a1r5g5b5", 9: "a4r4g4b4",
           10: "x8r8g8b8", 11: "a8r8g8b8", 14: "dxt1", 15: "dxt3", 16: "dxt5"}

HUD = "ui\\hud\\bitmaps\\"
# the folders of the HUD's bitmaps: the HUD's own, and the sniper rifle's
# (its scope's elevation ladder)
FOLDERS = (HUD, "weapons\\sniper rifle\\bitmaps\\")
# the meter sheets: their restored art in the Xbox channel order (the fill
# threshold the meter shader reads in the grey, the art in the alpha), from
# the release's extra/xbox_order_hud_meters
METERS = {
    HUD + "combined\\hud_unit_meters": "extra/xbox_order_hud_meters/tags/ui/hud/bitmaps/combined/hud_unit_meters.bitmap",
    HUD + "combined\\hud_ammo_meters": "extra/xbox_order_hud_meters/tags/ui/hud/bitmaps/combined/hud_ammo_meters.bitmap",
}
# restored sheets not used: a reticle the Xbox has none of
UNUSED_ART = {"hud__bitmaps__combined__hud_reticles__13.png"}
# the Xbox bitmaps not drawn from here: the stencils of the buttons the
# menus' text draws, which are the menus' (tools/button_assets.py), and the
# sniper rifle's own textures beside its scope's
NOT_HUD = (HUD + "hud_msg_icons_sm", "weapons\\sniper rifle\\bitmaps\\s")
# restored sheets kept in ART for others: the buttons' stencils, which the
# HUD's classic buttons are drawn from (tools/button_assets.py)
BUTTON_ART = {"hud__bitmaps__hud_msg_icons_sm__0.png": ("tags/ui/hud/bitmaps/hud_msg_icons_sm.bitmap", 0)}
# the sheets with text (the message icons' words, the reticle's warnings):
# each texture stands for its sprites, each checked on its own cell's
# texels, so that another language's maps draw their own text, upscaled,
# beside the pictures they share (port/linux/game/hud_hires_tags.c)
SPRITE_SHEETS = {HUD + "combined\\hud_msg_icons", HUD + "combined\\hud_reticle_warnings"}
# where a cell's art goes where its shape does not say (the bitmap, the Xbox
# cell: the corner on the restored sheet): the shotgun's ammo meter, whose
# restored art is narrower than its Xbox cell, left-aligned as the Xbox's is
CORNERS = {
    ("hud_ammo_meters__0", (0, 27, 130, 45)): [-2, 108],
    ("hud_ammo_meters__0", (0, 78, 130, 88)): [-5, 312],
}
# the cells drawn from their redraw, not the restored art (the bitmap, the
# Xbox cell): the sniper's zoom labels, the Xbox's 2x and 10x, where the
# restored ones (Halo PC's) say 2x and 8x, set in Cyborg Interface Gothic
# together (labels: LABELS)
REDRAWN = {
    ("sniper__hud_reticles_scope__0", (0, 0, 31, 24)),
    ("sniper__hud_reticles_scope__0", (0, 24, 43, 48)),
}
# how each redraw's texels are written, by its PC bitmap's format (as the
# Xbox decodes that format: xbox_texture_decode.c, convert_texel): AY8 is its one
# value in every channel (the HUD blends premultiplied), A8Y8 grey and alpha,
# A8 white with alpha, Y8 grey and opaque, colour formats as drawn; DXT ones
# as drawn with the colour carried into the transparent texels, as theirs is
KINDS = {0: "a8", 1: "y8", 2: "alpha", 3: "grey", 6: "colour", 8: "colour", 9: "colour", 10: "colour", 11: "colour",
         14: "dxt", 15: "dxt", 16: "dxt"}

# how far (in Xbox texels) a redrawn cell may sit from its Xbox one, and how
# much it must overlap it (the overlap of their shapes over their union: thin
# and small shapes, drawn cleaner than the Xbox's blurred texels, overlap
# little even where they match)
SEARCH = 4
MATCH = 0.25

# the zoom labels beside the sniper's reticle, by sequence and sprite: the
# restored art has Halo PC's (2x and 8x), not the Xbox's (2x and 10x), so they
# are set in the HUD's text font (issue 756: Overpass did not look like the
# Xbox's), each with the spacing between its glyphs that fits it best, and
# all at the weight that fits them best together: the Xbox's letters are
# heavier than the font's one weight, so they are emboldened by an outline of
# that width (hundredths of an em, mitred to keep the font's corners)
LABELS = {
    HUD + "sniper\\hud_reticles_scope": {(1, 0): "2x", (1, 1): "10x"},
}
LABEL_FONT = Path("port/assets/fonts/CyborgInterfaceGothic-Regular.ttf")
LABEL_SPACINGS = (-6, -4, -2, 0, 2, 4, 6, 8, 10, 12)
LABEL_WEIGHTS = (0, 2, 4, 6, 8, 10)
# the least room between two glyphs' boxes (hundredths of an em): the Xbox's
# run together in their blur, which drawn sharp would read as one shape
LABEL_GAP = 3
# render texels per Xbox texel while fitting one, and the scales tried
# around the one its extent gives
FIT_TEXELS = 8
FIT_SCALES = np.linspace(0.85, 1.15, 31)


# ---------- Xbox maps


class XboxMap:
    """The tags of an Xbox cache file (its body after the 2 KB header is
    zlib-compressed on the disc)."""

    def __init__(self, path: Path):
        raw = path.read_bytes()
        self.data = raw[:0x800] + zlib.decompressobj().decompress(raw[0x800:])
        offset, size = struct.unpack_from("<ii", self.data, 0x10)
        self.tags_data = self.data[offset:offset + size]
        instances, = struct.unpack_from("<I", self.tags_data, 0)
        count, = struct.unpack_from("<I", self.tags_data, 0xC)
        self.tags = {}
        for index in range(count):
            group, _, _, _, name, address, _, _ = struct.unpack("<4sIIIIIII", self.read(instances + index * 0x20, 0x20))
            name = self.read(name, 256).split(b"\0")[0].decode()
            self.tags[(group[::-1].decode(), name)] = address

    def read(self, address: int, size: int) -> bytes:
        offset = address - XBOX_TAG_BASE
        return self.tags_data[offset:offset + size]

    def bitmap_group(self, name: str) -> dict:
        address = self.tags[("bitm", name)]
        header = self.read(address, 0x6C)
        sequence_count, sequences = struct.unpack_from("<II", header, 0x54)
        bitmap_count, bitmaps = struct.unpack_from("<II", header, 0x60)
        group = {"bitmaps": [], "sequences": []}
        for index in range(bitmap_count):
            bitmap = self.read(bitmaps + index * 0x30, 0x30)
            width, height, _, _, format, flags = struct.unpack_from("<hhhhhH", bitmap, 4)
            offset, size = struct.unpack_from("<ii", bitmap, 0x18)
            group["bitmaps"].append({"width": width, "height": height, "format": format, "flags": flags,
                                     "pixels": self.data[offset:offset + size]})
        for index in range(sequence_count):
            sequence = self.read(sequences + index * 0x40, 0x40)
            sprite_count, sprites = struct.unpack_from("<II", sequence, 0x34)
            group["sequences"].append([sprite_from(self.read(sprites + j * 0x20, 0x20), "<") for j in range(sprite_count)])
        return group


def sprite_from(data: bytes, order: str) -> tuple:
    bitmap, = struct.unpack_from(order + "h", data, 0)
    left, right, top, bottom = struct.unpack_from(order + "4f", data, 8)
    return bitmap, left, top, right, bottom


def morton_order(width: int, height: int) -> np.ndarray:
    """Each texel's index in the Xbox's swizzled order: x and y bits
    interleaved, x first, the longer side's leftover bits on top."""
    xs, ys = np.meshgrid(np.arange(width), np.arange(height))
    index = np.zeros((height, width), np.int64)
    bit = shift_x = shift_y = 0
    x_bits, y_bits = width.bit_length() - 1, height.bit_length() - 1
    while shift_x < x_bits or shift_y < y_bits:
        if shift_x < x_bits:
            index |= ((xs >> shift_x) & 1) << bit
            bit += 1
            shift_x += 1
        if shift_y < y_bits:
            index |= ((ys >> shift_y) & 1) << bit
            bit += 1
            shift_y += 1
    return index


def level0_size(bitmap: dict) -> int:
    """The bytes of a bitmap's first mip level."""
    width, height, kind = bitmap["width"], bitmap["height"], FORMATS[bitmap["format"]]
    if kind.startswith("dxt"):
        return max(1, width // 4) * max(1, height // 4) * (8 if kind == "dxt1" else 16)
    texel = {"a8": 1, "y8": 1, "ay8": 1, "a8y8": 2, "r5g6b5": 2, "a1r5g5b5": 2, "a4r4g4b4": 2}.get(kind, 4)
    return width * height * texel


def decode_dxt(kind: str, data: bytes, width: int, height: int) -> np.ndarray:
    """DXT blocks (row by row, not swizzled) as RGBA."""
    image = np.zeros((height, width, 4), np.uint8)
    block_size = 8 if kind == "dxt1" else 16
    blocks_wide = max(1, width // 4)
    for block in range(max(1, width // 4) * max(1, height // 4)):
        bx, by = block % blocks_wide * 4, block // blocks_wide * 4
        chunk = data[block * block_size:(block + 1) * block_size]
        colour = chunk[-8:]
        c0, c1, bits = struct.unpack("<HHI", colour)
        def rgb(value):
            return [((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31]
        palette = [rgb(c0) + [255], rgb(c1) + [255]]
        if kind != "dxt1" or c0 > c1:
            palette += [[(2 * a + b) // 3 for a, b in zip(palette[0][:3], palette[1][:3])] + [255],
                        [(a + 2 * b) // 3 for a, b in zip(palette[0][:3], palette[1][:3])] + [255]]
        else:
            palette += [[(a + b) // 2 for a, b in zip(palette[0][:3], palette[1][:3])] + [255], [0, 0, 0, 0]]
        if kind == "dxt3":
            alphas = [((int.from_bytes(chunk[:8], "little") >> (4 * i)) & 15) * 17 for i in range(16)]
        elif kind == "dxt5":
            a0, a1 = chunk[0], chunk[1]
            table = [a0, a1] + ([((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)] if a0 > a1 else
                                [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255])
            codes = int.from_bytes(chunk[2:8], "little")
            alphas = [table[(codes >> (3 * i)) & 7] for i in range(16)]
        else:
            alphas = None
        for i in range(16):
            x, y = bx + i % 4, by + i // 4
            if x < width and y < height:
                texel = list(palette[(bits >> (2 * i)) & 3])
                if alphas is not None:
                    texel[3] = alphas[i]
                image[y, x] = texel
    return image


def stored_texels(bitmap: dict) -> np.ndarray:
    """An uncompressed bitmap's level 0 texels as they are stored (each
    one's bytes), by row and column."""
    width, height = bitmap["width"], bitmap["height"]
    pixels = np.frombuffer(bitmap["pixels"][:level0_size(bitmap)], np.uint8)
    order = (np.arange(width * height).reshape(height, width) if bitmap["flags"] & BITMAP_LINEAR_FLAG else
             morton_order(width, height))
    return pixels.reshape(-1, level0_size(bitmap) // (width * height))[order]


def cell_crc(bitmap: dict, cell: list) -> int:
    """The CRC-32 of a cell's texels as they are stored, row by row, top
    first, each texel's bytes (as port/linux/src/hud_hires.c, cell_crc,
    reads them)."""
    left, top, right, bottom = cell
    return zlib.crc32(np.ascontiguousarray(stored_texels(bitmap)[top:bottom, left:right]).tobytes())


def decode_bitmap(bitmap: dict) -> np.ndarray:
    """An Xbox HUD bitmap's level 0 as RGBA, as xbox_texture_decode.c decodes it."""
    width, height, kind = bitmap["width"], bitmap["height"], FORMATS[bitmap["format"]]
    if kind.startswith("dxt"):
        return decode_dxt(kind, bitmap["pixels"], width, height)
    raw = stored_texels(bitmap)
    image = np.zeros((height, width, 4), np.uint8)
    if kind == "a8":
        image[..., :3], image[..., 3] = 255, raw[..., 0]
    elif kind == "y8":
        image[..., :3], image[..., 3] = raw[..., :1], 255
    elif kind == "ay8":
        image[...] = raw[..., :1]
    elif kind == "a8y8":
        image[..., :3], image[..., 3] = raw[..., :1], raw[..., 1]
    elif kind == "a4r4g4b4":
        value = raw[..., 0].astype(np.uint16) | (raw[..., 1].astype(np.uint16) << 8)
        for channel, shift in ((3, 12), (0, 8), (1, 4), (2, 0)):
            image[..., channel] = ((value >> shift) & 15) * 17
    elif kind in ("a8r8g8b8", "x8r8g8b8"):
        image[..., 0], image[..., 1], image[..., 2] = raw[..., 2], raw[..., 1], raw[..., 0]
        image[..., 3] = raw[..., 3] if kind == "a8r8g8b8" else 255
    else:
        raise ValueError(f"no decoder for {kind}")
    return image


# ---------- PC (HEK) tags


def hek_bitmap_group(path: Path) -> dict:
    """A HEK bitmap tag's bitmaps (sizes, formats and pixels) and sprites
    (big-endian; its blocks follow the 0x40-byte file header and the
    0x6C-byte tag in order: data, sequences with their sprites, bitmaps)."""
    data = path.read_bytes()
    header = data[0x40:0x40 + 0x6C]
    compressed_size, = struct.unpack_from(">I", header, 0x1C)
    pixels_size, = struct.unpack_from(">I", header, 0x30)
    sequence_count, = struct.unpack_from(">I", header, 0x54)
    bitmap_count, = struct.unpack_from(">I", header, 0x60)
    position = 0x40 + 0x6C + compressed_size + pixels_size
    counts = [struct.unpack_from(">I", data, position + index * 0x40 + 0x34)[0] for index in range(sequence_count)]
    position += sequence_count * 0x40
    group = {"sequences": [], "bitmaps": []}
    for count in counts:
        group["sequences"].append([sprite_from(data[position + j * 0x20:position + (j + 1) * 0x20], ">")
                                   for j in range(count)])
        position += count * 0x20
    for index in range(bitmap_count):
        width, height, _, _, format = struct.unpack_from(">HHHhh", data, position + index * 0x30 + 4)
        # (its pixels at its offset in the pixel data, which follows the
        # compressed import data; in rows, not swizzled)
        offset, = struct.unpack_from(">i", data, position + index * 0x30 + 0x18)
        group["bitmaps"].append({"width": width, "height": height, "format": format, "flags": BITMAP_LINEAR_FLAG,
                                 "pixels": data[0x40 + 0x6C + compressed_size + offset:]})
    return group


# ---------- rendering and matching


def render_svg(svg: Path, zoom: float = 1) -> np.ndarray:
    with tempfile.TemporaryDirectory() as directory:
        output = Path(directory) / "render.png"
        subprocess.run(["rsvg-convert", "-z", str(zoom), "-o", str(output), str(svg)], check=True)
        return np.asarray(Image.open(output).convert("RGBA")).copy()


def pixel_rectangle(sprite: tuple, width: int, height: int) -> list:
    _, left, top, right, bottom = sprite
    return [round(left * width), round(top * height), round(right * width), round(bottom * height)]


def window(image: np.ndarray, left: int, top: int, width: int, height: int) -> np.ndarray:
    """image[top:top+height, left:left+width], transparent outside it."""
    result = np.zeros((height, width) + image.shape[2:], image.dtype)
    x0, y0 = max(left, 0), max(top, 0)
    x1, y1 = min(left + width, image.shape[1]), min(top + height, image.shape[0])
    if x0 < x1 and y0 < y1:
        result[y0 - top:y1 - top, x0 - left:x1 - left] = image[y0:y1, x0:x1]
    return result


def clipped(image: np.ndarray, clip: list) -> np.ndarray:
    """image with only clip's texels (a sprite's own, not its neighbours')."""
    left, top, right, bottom = clip
    result = np.zeros_like(image)
    result[top:bottom, left:right] = image[top:bottom, left:right]
    return result


def overlap(a: np.ndarray, b: np.ndarray) -> float:
    union = np.maximum(a, b).sum()
    return float(np.minimum(a, b).sum() / union) if union else 0.0


def match(alpha: np.ndarray, render: np.ndarray, clip: list, scale: int) -> tuple:
    """How well the redrawn sprite in clip (on the render, scale times the
    Xbox texels) matches the Xbox cell's alpha, and where: (overlap, the
    render's corner for the cell). Searched in Xbox texels on the render
    reduced, then refined in its own texels."""
    height, width = alpha.shape
    middle = ((clip[0] + clip[2]) / 2, (clip[1] + clip[3]) / 2)
    corner = [round(middle[0] - width * scale / 2), round(middle[1] - height * scale / 2)]
    alpha = alpha.astype(float) / 255
    source = clipped(render[..., 3].astype(float) / 255, clip)
    best = (-1.0, corner)
    for dy in range(-SEARCH, SEARCH + 1):
        for dx in range(-SEARCH, SEARCH + 1):
            at = [corner[0] + dx * scale, corner[1] + dy * scale]
            reduced = window(source, at[0], at[1], width * scale, height * scale).reshape(
                height, scale, width, scale).mean(axis=(1, 3))
            score = overlap(reduced, alpha)
            if score > best[0]:
                best = (score, at)
    target = np.asarray(Image.fromarray((alpha * 255).astype(np.uint8)).resize(
        (width * scale, height * scale), Image.BILINEAR), float) / 255
    coarse = best[1]
    best = (-1.0, coarse)
    for dy in range(-scale + 1, scale):
        for dx in range(-scale + 1, scale):
            at = [coarse[0] + dx, coarse[1] + dy]
            score = overlap(window(source, at[0], at[1], width * scale, height * scale), target)
            if score > best[0]:
                best = (score, at)
    return best


def similar_size(clip: list, cell: list, scale: int) -> bool:
    """Whether a PC sprite (clip, on a sheet scale times the Xbox's) is about
    the size of an Xbox cell."""
    return (0.6 < (clip[2] - clip[0]) / ((cell[2] - cell[0]) * scale) < 1.6 and
            0.6 < (clip[3] - clip[1]) / ((cell[3] - cell[1]) * scale) < 1.6)


def best_match(scored: list):
    """Of the matches for a cell, each (score, source, scale, corner, clip,
    kind, whether its sprite numbers are the cell's), the best; but the
    sprite of the same numbers where the sheets agree and it matches nearly
    as well (small glyphs, a dot or a colon, overlap others as much as their
    own). None if there are none."""
    best = max(scored, default=None)
    same = max((entry for entry in scored if entry[6]), default=None)
    if same is not None and best is not None and same[0] >= max(MATCH, 0.6 * best[0]):
        best = same
    return best


# ---------- the restored art
#
# (the matching of the Xbox sheets' cells to the restored sheets' sprites,
# the meters in the Xbox channel order, the faint cells and the warnings'
# grey shapes follow ChupathingyCE's tools/hud_assets.py, GPL-3.0-or-later,
# at c32bfacf: https://github.com/ChupathingyCE/chupathingyce)


def art_name(tag: str, index: int, count: int) -> str:
    """A restored sheet's PNG in ART: its tag's path (under ui), with the
    bitmap's index when the tag has more than one."""
    parts = tag.split("\\")
    name = "__".join(parts[1:] if parts[0] == "ui" else parts).replace(" ", "_")
    return name + (f"__{index}" if count > 1 else "") + ".png"


def art_texels(name: str) -> np.ndarray:
    return np.asarray(Image.open(ART / name).convert("RGBA")).copy()


def restored_sheets(hek_root: Path, tag: str) -> list:
    """The restored art for an Xbox tag's bitmaps: (its PNG's name in ART,
    the PC bitmap's index, the PC bitmap group, its texels decoded as the
    Xbox decodes them), those in UNUSED_ART left out."""
    if tag in METERS:
        hek = hek_root / METERS[tag]
    else:
        hek = hek_root / "tags" / Path(*tag.split("\\")).with_suffix(".bitmap")
    if not hek.exists():
        return []
    group = hek_bitmap_group(hek)
    sheets = []
    for index, bitmap in enumerate(group["bitmaps"]):
        name = art_name(tag, index, len(group["bitmaps"]))
        if name not in UNUSED_ART:
            sheets.append((name, index, group, decode_bitmap(bitmap)))
    return sheets


def match_restored(alpha: np.ndarray, cell: list, numbers: set, xbox_width: int, sheets: list):
    """The restored sprite that matches a cell of an Xbox bitmap xbox_width
    wide best (best_match), on a sheet a whole multiple, two or more, of its
    size: (score, PNG, scale, corner, clip, kind, whether its sprite
    numbers are the cell's); or None."""
    scored = []
    for name, pc_index, pc_group, texels in sheets:
        if texels.shape[1] % xbox_width or texels.shape[1] // xbox_width < 2:
            continue
        scale = texels.shape[1] // xbox_width
        pc_cells = [(pixel_rectangle(sprite, texels.shape[1], texels.shape[0]), (sequence_index, sprite_index))
                    for sequence_index, sequence in enumerate(pc_group["sequences"])
                    for sprite_index, sprite in enumerate(sequence) if sprite[0] == pc_index]
        for clip, pc_numbers in pc_cells or [([0, 0, texels.shape[1], texels.shape[0]], None)]:
            if not similar_size(clip, cell, scale):
                continue
            score, corner = match(alpha, texels, clip, scale)
            scored.append((score, name, scale, corner, clip, KINDS.get(pc_group["bitmaps"][pc_index]["format"]),
                           pc_numbers in numbers))
    return best_match(scored)


def layout(arguments) -> None:
    xbox_map = XboxMap(Path(arguments.map))
    hek_root = Path(arguments.hek)
    old_entries = json.loads(LAYOUT.read_text())["assets"] if LAYOUT.exists() else []
    old = {entry["name"]: entry for entry in old_entries}
    entries, used = [], {}
    for group_tag, tag in sorted(xbox_map.tags):
        if group_tag != "bitm" or not tag.startswith(FOLDERS) or tag.startswith(NOT_HUD):
            continue
        sheets = restored_sheets(hek_root, tag)
        if not sheets:
            continue
        group = xbox_map.bitmap_group(tag)
        for index, bitmap in enumerate(group["bitmaps"]):
            width, height = bitmap["width"], bitmap["height"]
            # (the combined sheets by their own names; the weapon's by its
            # name and theirs)
            stem = tag[len(HUD):].replace("combined\\", "") if tag.startswith(HUD) else \
                tag.split("\\", 1)[1].replace("\\bitmaps\\", "\\")
            name = stem.replace("\\", "__").replace(" ", "_") + f"__{index}"
            kept = old.get(name)
            xbox = decode_bitmap(bitmap)
            # (each cell with the sprites, as sequence and sprite numbers, it is)
            cells = {}
            for sequence_index, sequence in enumerate(group["sequences"]):
                for sprite_index, sprite in enumerate(sequence):
                    if sprite[0] == index:
                        cells.setdefault(tuple(pixel_rectangle(sprite, width, height)), set()).add(
                            (sequence_index, sprite_index))
            cells = cells or {(0, 0, width, height): set()}
            matched, redrawn = [], []
            for cell, numbers in sorted(cells.items()):
                cell = list(cell)
                left, top, right, bottom = cell
                # (the cell's shape: its alpha, or its grey where its alpha is
                # flat (the "Km" glyph has only grey), or where the grey is
                # the shape drawn (the warnings' pips are dots in the grey
                # over triangles in the alpha, where the restored ones are
                # dots in both)
                alpha = xbox[top:bottom, left:right, 3]
                grey = xbox[top:bottom, left:right, 0]
                if alpha.min() == alpha.max() or (
                        "reticle_warnings" in tag and overlap(alpha.astype(float), grey.astype(float)) < 0.5):
                    alpha = grey
                # (blank, or a few stray texels of a neighbour's shape: left
                # blank)
                if np.count_nonzero(alpha) <= 4:
                    continue
                previous = next((c for c in (kept or {"cells": []})["cells"] if c["xbox"] == cell), None)
                if (name, tuple(cell)) in REDRAWN:
                    if previous is None or "svg" not in previous:
                        print(f"{name}: cell {cell} has no redraw yet: run labels")
                        continue
                    matched.append(previous)
                    redrawn.append(cell)
                    continue
                best = match_restored(alpha, cell, numbers, width, sheets)
                faint = (best is None or best[0] < MATCH) and alpha.max() < 64
                if faint:
                    # (a faint cell: matched by its shape, drawn as faint; or,
                    # matching nothing, left blank, as the message icons'
                    # all but invisible frame is)
                    best = match_restored((alpha.astype(float) * 255 / alpha.max()).astype(np.uint8), cell,
                                          numbers, width, sheets)
                    if best is None or best[0] < MATCH:
                        print(f"{name}: cell {cell}, faint, matches nothing: left blank")
                        continue
                if best is None or best[0] < MATCH or best[5] is None:
                    # (the bitmap as it was: its redraw, where it had one)
                    matched = None
                    print(f"{name}: cell {cell} " +
                          ("has no restored sprite" if best is None else f"matches best {best[1]} at {best[0]:.2f}")
                          + (": kept as it was" if kept else ": left out"))
                    break
                score, art, scale, corner, clip, kind, _ = best
                corner = CORNERS.get((name, tuple(cell)), corner)
                if tag in METERS:
                    kind = "meter"
                used[art] = next(texels for sheet, _, _, texels in sheets if sheet == art)
                found = {"xbox": cell, "art": art, "source_scale": scale, "source": corner, "clip": clip,
                         "kind": kind, "score": round(score, 3)}
                previous = previous or {}
                for key in ("thresholds", "quantize_thresholds"):
                    if key in previous:
                        found[key] = previous[key]
                if faint:
                    found["intensity"] = round(float(xbox[top:bottom, left:right, 3].max()) / 255, 3)
                matched.append(found)
            if not matched:
                if kept is not None:
                    entries.append(kept)
                elif matched is not None:
                    print(f"{name}: left out, empty")
                continue
            # (the restored art's own scale)
            scale = max(cell["source_scale"] for cell in matched if "art" in cell)
            while max(width, height) * scale > MAXIMUM_SIZE:
                scale //= 2
            entry = {"name": name, "tag": tag, "bitmap": index, "width": width, "height": height,
                     "format": FORMATS[bitmap["format"]], "scale": scale,
                     # (the Xbox bitmap's, which the game checks before drawing
                     # this in its place: other languages' maps and modified
                     # ones differ)
                     "crc": zlib.crc32(bitmap["pixels"][:level0_size(bitmap)])}
            if tag in SPRITE_SHEETS:
                # (the sprites it stands for, each checked on its own cell's
                # texels, so that another language's maps draw their own text
                # and these pictures: hud_hires.h)
                entry["sprites"] = [{"sequence": sequence, "cell": list(cell), "crc": cell_crc(bitmap, list(cell))}
                                    for cell, numbers in sorted(cells.items())
                                    for sequence in sorted({number[0] for number in numbers})
                                    if any(c["xbox"] == list(cell) for c in matched)]
            entry["cells"] = matched
            entries.append(entry)
            print(f"{name}: {width}x{height} {FORMATS[bitmap['format']]} at {scale}x, {len(matched)} cells, "
                  f"{len(redrawn)} redrawn, worst match {min(c.get('score', 1) for c in matched):.2f}")
    # (the textures of no Xbox bitmap: the outlines the upscaled sheets get)
    names = {entry["name"] for entry in entries}
    entries += [entry for entry in old_entries if entry["name"] not in names and
                not entry["tag"].startswith(FOLDERS)]
    for art, (path, index) in BUTTON_ART.items():
        used[art] = decode_bitmap(hek_bitmap_group(hek_root / path)["bitmaps"][index])
    if ART.exists():
        shutil.rmtree(ART)
    ART.mkdir(parents=True)
    for art, texels in sorted(used.items()):
        Image.fromarray(texels, "RGBA").save(ART / art, optimize=True)
    kept_svgs = {cell["svg"] for entry in entries for cell in entry["cells"] if "svg" in cell}
    for svg in (ASSETS / "svg").glob("*.svg"):
        if svg.name not in kept_svgs:
            svg.unlink()
            print(f"{svg.name}: no longer used")
    LAYOUT.write_text(json.dumps({"assets": entries}, indent=1) + "\n")


# ---------- the sniper's zoom labels


def soften(image: np.ndarray) -> np.ndarray:
    """A shape with its edges softened, so that a thinner stroke on one side
    still lines up with the other's (the Xbox's are heavier and blurred)."""
    from scipy import ndimage

    return ndimage.gaussian_filter(image.astype(float), 0.8)


def likeness(a: np.ndarray, b: np.ndarray) -> float:
    a, b = a - a.mean(), b - b.mean()
    norm = np.sqrt((a * a).sum() * (b * b).sum())
    return float((a * b).sum() / norm) if norm else 0.0


def extent(alpha: np.ndarray) -> tuple:
    """The box (left, top, right, bottom) of a shape's clearly covered texels."""
    ys, xs = np.nonzero(alpha >= alpha.max() / 4)
    return xs.min(), ys.min(), xs.max() + 1, ys.max() + 1


def text_outlines(font: Path, text: str, spacing: float = 0) -> tuple:
    """text's outlines in a font, an em of 100 units, the line's top (the
    font's ascent) at 0: (the paths, an SVG group's contents in the font's
    units; the transform from those; the line's width and height), set with
    its advances and spacing (in hundredths of an em) between them."""
    from fontTools.pens.svgPathPen import SVGPathPen
    from fontTools.ttLib import TTFont

    face = TTFont(str(font))
    glyphs = face.getGlyphSet()
    cmap = face.getBestCmap()
    scale = 100 / face["head"].unitsPerEm
    paths, x = [], 0
    for index, character in enumerate(text):
        name = cmap[ord(character)]
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(pen)
        paths.append(f'<path transform="translate({x:g} 0)" d="{pen.getCommands()}"/>')
        x += face["hmtx"][name][0] + (spacing / scale if index + 1 < len(text) else 0)
    ascent, descent = face["hhea"].ascent, face["hhea"].descent
    return ("\n".join(paths), f"scale({scale:g} {-scale:g}) translate(0 {-ascent})",
            x * scale, (ascent - descent) * scale)


def text_gap(font: Path, text: str, spacing: float = 0, weight: float = 0) -> float:
    """The least room (hundredths of an em) between the boxes of text's
    neighbouring glyphs, set as text_svg sets them."""
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.ttLib import TTFont

    face = TTFont(str(font))
    glyphs = face.getGlyphSet()
    cmap = face.getBestCmap()
    scale = 100 / face["head"].unitsPerEm
    gaps, x, previous = [], 0, None
    for character in text:
        name = cmap[ord(character)]
        pen = BoundsPen(glyphs)
        glyphs[name].draw(pen)
        if previous is not None:
            gaps.append((x + pen.bounds[0] - previous) * scale - weight)
        previous = x + pen.bounds[2]
        x += face["hmtx"][name][0] + spacing / scale
    return min(gaps, default=0)


def text_svg(font: Path, text: str, spacing: float = 0, weight: float = 0) -> str:
    """text's outlines in a font, as an SVG (an em of 100 units), for a
    label that has no redraw: the glyphs as the font draws them, set with
    its advances and spacing (in hundredths of an em) between them, and
    emboldened by an outline weight hundredths of an em wide (half of it
    outside the glyphs), the picture grown by that half on each side."""
    paths, transform, width, height = text_outlines(font, text, spacing)
    pad = weight / 2
    width, height = width + 2 * pad, height + 2 * pad
    stroke = ""
    if weight:
        scale = float(transform.split("(")[1].split()[0])
        stroke = f' stroke="#ffffff" stroke-width="{weight / scale:g}" stroke-linejoin="miter"'
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width:.2f}" height="{height:.2f}" '
            f'viewBox="0 0 {width:.2f} {height:.2f}">\n'
            f'<!-- "{text}" in {font.name}, spacing {spacing:g}, weight {weight:g} -->\n'
            f'<g fill="#ffffff"{stroke} transform="translate({pad:g} {pad:g}) {transform}">\n'
            + paths + "\n</g>\n</svg>\n")


def fit_reticle(svg: Path, alpha: np.ndarray) -> tuple:
    """The size and place at which a redraw best matches an Xbox cell's
    alpha: (likeness, redraw units per Xbox texel, the redraw's point at the
    cell's corner)."""
    target = soften(alpha / 255.0)

    def drawn(zoom):
        return render_svg(svg, zoom)[..., 3].astype(float) / 255

    base = drawn(FIT_TEXELS)
    svg_box = [value / FIT_TEXELS for value in extent(base)]
    xbox_box = extent(alpha)
    # (the Xbox's extent with its blur's texel taken off each side)
    units = max((svg_box[2] - svg_box[0]) / max(xbox_box[2] - xbox_box[0] - 1, 1),
                (svg_box[3] - svg_box[1]) / max(xbox_box[3] - xbox_box[1] - 1, 1))
    best = (-2.0, units, [0.0, 0.0])
    for factor in FIT_SCALES:
        per_texel = units * factor
        zoom = FIT_TEXELS / per_texel
        score, corner = place(drawn(zoom), alpha, target, zoom)
        if score > best[0]:
            best = (score, per_texel, corner)
    return best


def place(render: np.ndarray, alpha: np.ndarray, target: np.ndarray, zoom: float) -> tuple:
    """Where a redraw's render (its alpha, FIT_TEXELS texels to an Xbox
    texel; zoom times its units) best matches an Xbox cell's alpha (target:
    softened): (likeness, the redraw's point at the cell's corner)."""
    height, width = alpha.shape
    box = extent(render)
    xbox_box = extent(alpha)
    best = (-2.0, [0.0, 0.0])
    # (centred on the Xbox shape's centre, then moved by eighths of a texel)
    centre = ((box[0] + box[2]) / 2 - (xbox_box[0] + xbox_box[2]) / 2 * FIT_TEXELS,
              (box[1] + box[3]) / 2 - (xbox_box[1] + xbox_box[3]) / 2 * FIT_TEXELS)
    for dy in range(-FIT_TEXELS * 3 // 2, FIT_TEXELS * 3 // 2 + 1):
        for dx in range(-FIT_TEXELS * 3 // 2, FIT_TEXELS * 3 // 2 + 1):
            at = (round(centre[0]) + dx, round(centre[1]) + dy)
            reduced = window(render, at[0], at[1], width * FIT_TEXELS, height * FIT_TEXELS).reshape(
                height, FIT_TEXELS, width, FIT_TEXELS).mean(axis=(1, 3))
            score = likeness(soften(reduced), target)
            if score > best[0]:
                best = (score, [at[0] / zoom, at[1] / zoom])
    return best


def labels(arguments) -> None:
    """The sniper's zoom labels (LABELS), set in LABEL_FONT at the size,
    place and spacing between their glyphs that match the map's best, all
    at the one weight that matches them best together (the Xbox's are one
    weight), into their cells of layout.json (REDRAWN: layout keeps them)."""
    xbox_map = XboxMap(Path(arguments.map))
    description = json.loads(LAYOUT.read_text())
    labelled = []
    for tag, texts in LABELS.items():
        group = xbox_map.bitmap_group(tag)
        for (sequence, sprite_index), text in sorted(texts.items()):
            sprite = group["sequences"][sequence][sprite_index]
            bitmap = group["bitmaps"][sprite[0]]
            name = tag[len(HUD):].replace("combined\\", "").replace("\\", "__").replace(" ", "_") + f"__{sprite[0]}"
            entry = next((e for e in description["assets"] if e["name"] == name), None)
            if entry is None:
                sys.exit(f"{name}: not in layout.json (run layout first)")
            cell = pixel_rectangle(sprite, bitmap["width"], bitmap["height"])
            alpha = decode_bitmap(bitmap)[cell[1]:cell[3], cell[0]:cell[2], 3].astype(float)
            svg = f"label__{LABEL_FONT.stem}__{text}.svg"
            # (each weight's best fit, over the spacings)
            fits = {}
            for weight in LABEL_WEIGHTS:
                for spacing in LABEL_SPACINGS:
                    if text_gap(LABEL_FONT, text, spacing, weight) < LABEL_GAP:
                        continue
                    (ASSETS / "svg" / svg).write_text(text_svg(LABEL_FONT, text, spacing, weight))
                    fit = fit_reticle(ASSETS / "svg" / svg, alpha)
                    if weight not in fits or fit[0] > fits[weight][0][0]:
                        fits[weight] = (fit, spacing)
            labelled.append((name, entry, cell, text, svg, fits))
    weight = max(LABEL_WEIGHTS, key=lambda w: sum(fits[w][0][0] for *_, fits in labelled))
    for name, entry, cell, text, svg, fits in labelled:
        (score, per_texel, corner), spacing = fits[weight]
        (ASSETS / "svg" / svg).write_text(text_svg(LABEL_FONT, text, spacing, weight))
        size = render_svg(ASSETS / "svg" / svg)[..., 3].shape
        laid = {"xbox": cell, "svg": svg, "source_scale": round(per_texel, 5),
                "source": [round(corner[0], 4), round(corner[1], 4)],
                "clip": [0, 0, size[1], size[0]], "kind": "alpha", "score": round(score, 3)}
        entry["cells"] = [c for c in entry["cells"] if c["xbox"] != cell] + [laid]
        entry["cells"].sort(key=lambda c: c["xbox"])
        print(f"{name} {cell}: \"{text}\" in {LABEL_FONT.name}, spacing {spacing}, weight {weight}, "
              f"{per_texel:.4f} units a texel, likeness {score:.3f}")
    LAYOUT.write_text(json.dumps(description, indent=1) + "\n")


# ---------- building


def bleed(image: np.ndarray) -> np.ndarray:
    """Gives each transparent texel the colour of the nearest covered one: so
    that filtering and mip levels at a meter's edges read its own fill
    thresholds, and a DXT texture keeps its colour where it is transparent."""
    from scipy import ndimage

    covered = image[:, :, 3] > 0
    if not covered.any():
        return image
    _, (rows, columns) = ndimage.distance_transform_edt(~covered, return_indices=True)
    result = image.copy()
    for channel in range(3):
        result[:, :, channel] = image[rows, columns, channel]
    result[:, :, 3] = image[:, :, 3]
    return result


def coverage(alpha: np.ndarray) -> np.ndarray:
    """How much of each texel a meter's shapes cover: all of it inside them
    (their dim cells too: their alpha is faint by design), and on their
    outline (covered texels next to uncovered ones) the share their alpha has
    of their brightest neighbour's. The meter shader reads only blue and
    alpha; the game eases the meter's darkening of what is behind it by this
    (port/linux/src/xgpu_spirv.c, coverage_alpha), which on the Xbox stopped at
    its point-sampled texels' edges, and filtered would leave a dark fringe."""
    from scipy import ndimage

    covered = alpha > 0
    outline = covered & ndimage.binary_dilation(~covered, structure=np.ones((3, 3), bool))
    brightest = ndimage.maximum_filter(alpha, size=3).astype(float)
    result = np.where(covered, 255.0, 0.0)
    result[outline] = np.clip(alpha[outline] * 255.0 / np.maximum(brightest[outline], 1.0), 0, 255)
    return np.round(result).astype(np.uint8)


def recipe(texels: np.ndarray, kind: str) -> np.ndarray:
    """A redraw's texels written as its kind is (KINDS)."""
    result = texels.copy()
    grey = np.round(texels[..., :3].astype(float).mean(axis=-1)).astype(np.uint8)
    if kind == "alpha":
        result[..., :3] = texels[..., 3:4]
    elif kind in ("grey", "meter"):
        result[..., :3] = grey[..., None]
        result[texels[..., 3] == 0, :3] = 0
    elif kind == "a8":
        result[..., :3] = 255
    elif kind == "y8":
        result[..., :3], result[..., 3] = grey[..., None], 255
    return result


def meter_thresholds(texels: np.ndarray, values: list, quantize: bool = False) -> np.ndarray:
    """Categorical segment data, independent of antialiased edge RGB/alpha.

    Each solid-colour shape's opaque stroke supplies its exact threshold.
    Assign that value to its whole core, then extend it to the nearest edge
    and transparent texel. Low-alpha SVG texels cannot supply reliable RGB:
    unpremultiplying 8-bit colour there can turn 150 into 128 (or 60 into 0).
    This is only for explicitly listed discrete health and ammunition
    sprites; shield and other continuous gradients retain their data.
    """
    from scipy import ndimage

    cores, count = ndimage.label(texels[..., 3] >= 127)
    exact = np.zeros(texels.shape[:2], np.uint8)
    seen = set()
    for index in range(1, count + 1):
        core = cores == index
        opaque = texels[core & (texels[..., 3] == 255), :3]
        if not len(opaque):
            continue
        colours = np.unique(opaque, axis=0)
        if quantize:
            # Ammunition redraws use a gradient across separate bullets.
            # The counter consumes whole bullets: keep each one's integer
            # threshold constant through its antialiased edge and mip levels.
            centre = float(np.median(opaque[:, 0]))
            value = min(values, key=lambda candidate: abs(candidate - centre))
        else:
            if len(colours) != 1 or not np.all(colours[0] == colours[0, 0]):
                raise ValueError("discrete meter shape must have one solid grey threshold")
            value = int(colours[0, 0])
        if value not in values:
            raise ValueError(f"unexpected discrete meter threshold {value}")
        exact[core] = value
        seen.add(value)
    if seen != set(values):
        raise ValueError(f"missing discrete meter thresholds: {set(values) - seen}")
    _, (rows, columns) = ndimage.distance_transform_edt(exact == 0, return_indices=True)
    return exact[rows, columns]


def add_meter_thresholds(image: np.ndarray, entry: dict) -> np.ndarray:
    """Red is an unfiltered categorical field; zero selects normal blue data.
    Green coverage and alpha artwork remain filtered, including their mips.
    """
    cells = [cell for cell in entry["cells"] if cell.get("thresholds")]
    if cells:
        image[..., 0] = 0
        scale = entry["scale"]
        for cell in cells:
            left, top, right, bottom = [value * scale for value in cell["xbox"]]
            texels = image[top:bottom, left:right]
            # The meter's blue is still the original greyscale artwork data.
            source = texels.copy()
            source[..., :3] = source[..., 2:3]
            texels[..., 0] = meter_thresholds(source, cell["thresholds"], cell.get("quantize_thresholds", False))
    return image


def resized(texels: np.ndarray, zoom: float) -> np.ndarray:
    """Restored art at zoom times its size (premultiplied while resampled, so
    that transparent texels' colour does not bleed in); as it is at 1. (As
    ChupathingyCE's tools/hud_assets.py resizes it.)"""
    if zoom == 1:
        return texels
    size = (round(texels.shape[1] * zoom), round(texels.shape[0] * zoom))
    alpha = texels[..., 3:4].astype(float) / 255
    channels = [Image.fromarray(channel.astype(np.float32), "F").resize(size, Image.LANCZOS)
                for channel in np.moveaxis(np.concatenate([texels[..., :3] * alpha, alpha * 255], axis=-1), -1, 0)]
    result = np.clip(np.stack([np.asarray(channel) for channel in channels], axis=-1), 0, 255)
    covered = result[..., 3:4] > 0
    result[..., :3] = np.where(covered, result[..., :3] * 255 / np.maximum(result[..., 3:4], 1e-6), 0)
    return np.round(np.clip(result, 0, 255)).astype(np.uint8)


def build_asset(entry: dict, renders: dict) -> np.ndarray:
    scale = entry["scale"]
    image = np.zeros((entry["height"] * scale, entry["width"] * scale, 4), np.uint8)
    for cell in entry["cells"]:
        # (the restored art, or the redraw, at the texture's scale, its
        # coordinates with it)
        zoom = scale / cell["source_scale"]
        if zoom == int(zoom):
            zoom = int(zoom)
        key = (cell.get("svg") or cell["art"], zoom)
        if key not in renders:
            renders[key] = resized(art_texels(cell["art"]), zoom) if "art" in cell else \
                render_svg(ASSETS / "svg" / cell["svg"], zoom)
        left, top, right, bottom = cell["xbox"]
        width, height = (right - left) * scale, (bottom - top) * scale
        texels = window(clipped(renders[key], [round(value * zoom) for value in cell["clip"]]),
                        round(cell["source"][0] * zoom), round(cell["source"][1] * zoom), width, height)
        texels = recipe(texels, cell["kind"])
        if "intensity" in cell:
            texels = np.round(texels * cell["intensity"]).astype(np.uint8)
        image[top * scale:top * scale + height, left * scale:left * scale + width] = texels
    if any(cell["kind"] in ("meter", "dxt") for cell in entry["cells"]):
        image = bleed(image)
    if any(cell["kind"] == "meter" for cell in entry["cells"]):
        image[..., 1] = coverage(image[..., 3])
    return add_meter_thresholds(image, entry)


def thresholds(arguments) -> None:
    """Rebuild only categorical data in committed PNGs, retaining their art."""
    for entry in json.loads(LAYOUT.read_text())["assets"]:
        if any(cell.get("thresholds") for cell in entry["cells"]):
            path = ASSETS / f"{entry['name']}.png"
            image = np.asarray(Image.open(path).convert("RGBA")).copy()
            Image.fromarray(add_meter_thresholds(image, entry), "RGBA").save(path, optimize=True)
            print(f"{path.name}: rebuilt exact segment thresholds; artwork unchanged")


def build(arguments) -> None:
    description = json.loads(LAYOUT.read_text())
    names = {f"{entry['name']}.png" for entry in description["assets"]}
    # (and the Custom Edition sheets tools/ce_hud_assets.py makes from these,
    # and the MCC maps' own, mcc_layout.json's)
    for listing in ("ce_layout.json", "mcc_layout.json"):
        if (ASSETS / listing).is_file():
            names |= {f"{entry['name']}.png" for entry in json.loads((ASSETS / listing).read_text())["assets"]}
    # (all of them, the stale ones removed; or only those named)
    if not arguments.names:
        for stale in ASSETS.glob("*.png"):
            if stale.name not in names:
                stale.unlink()
    renders = {}
    for entry in description["assets"]:
        if arguments.names and entry["name"] not in arguments.names:
            continue
        image = build_asset(entry, renders)
        Image.fromarray(image, "RGBA").save(ASSETS / f"{entry['name']}.png", optimize=True)
        print(f"{entry['name']}.png: {image.shape[1]}x{image.shape[0]}")


# ---------- checking


def check(arguments) -> None:
    xbox_map = XboxMap(Path(arguments.map))
    output = Path(arguments.out)
    output.mkdir(parents=True, exist_ok=True)
    description = json.loads(LAYOUT.read_text())
    for entry in description["assets"]:
        if ("bitm", entry["tag"]) not in xbox_map.tags:
            print(f"{entry['name']}: not in this map")
            continue
        bitmap = xbox_map.bitmap_group(entry["tag"])["bitmaps"][entry["bitmap"]]
        sprites = entry.get("sprites", [])
        if any("crc" in sprite for sprite in sprites):
            # (checked by its sprites' cells, as the game checks them)
            other = [sprite["sequence"] for sprite in sprites if cell_crc(bitmap, sprite["cell"]) != sprite["crc"]]
            if other:
                print(f"{entry['name']}: this map's sprites of sequences {other} are not the ones laid out")
        elif zlib.crc32(bitmap["pixels"][:level0_size(bitmap)]) != entry["crc"]:
            print(f"{entry['name']}: this map's bitmap is not the one laid out")
        xbox = decode_bitmap(bitmap)
        image = Image.open(ASSETS / f"{entry['name']}.png").convert("RGBA")
        reduced = np.asarray(image.resize((entry["width"], entry["height"]), Image.BOX))
        # (a texture of some sprites compared on their cells alone)
        mask = np.zeros(xbox.shape[:2], bool)
        for sprite in sprites:
            left, top, right, bottom = sprite["cell"]
            mask[top:bottom, left:right] = True
        if not sprites:
            mask[...] = True
        score = overlap(reduced[..., 3][mask].astype(float), xbox[..., 3][mask].astype(float))
        print(f"{entry['name']}: alpha overlap {score:.2f}")
        # Xbox | ours reduced | ours, each over blue, then each one's alpha
        panels = [Image.fromarray(picture, "RGBA").resize(image.size, Image.NEAREST) for picture in (xbox, reduced)]
        panels.append(image)
        width, height = image.size
        sheet = Image.new("RGB", (width * 3 + 16, height * 2 + 8), (255, 0, 0))
        for index, picture in enumerate(panels):
            background = Image.new("RGBA", picture.size, (40, 40, 90, 255))
            sheet.paste(Image.alpha_composite(background, picture).convert("RGB"), (index * (width + 8), 0))
            sheet.paste(picture.getchannel("A").convert("RGB"), (index * (width + 8), height + 8))
        sheet.save(output / f"{entry['name']}.png")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("layout")
    command.add_argument("--map", required=True)
    command.add_argument("--hek", required=True)
    command = commands.add_parser("labels")
    command.add_argument("--map", required=True)
    command = commands.add_parser("build")
    command.add_argument("names", nargs="*", help="only these textures (as layout.json names them)")
    commands.add_parser("thresholds")
    command = commands.add_parser("check")
    command.add_argument("--map", required=True)
    command.add_argument("--out", required=True)
    arguments = parser.parse_args()
    {"layout": layout, "labels": labels, "build": build,
     "thresholds": thresholds, "check": check}[arguments.command](arguments)


if __name__ == "__main__":
    sys.exit(main())
