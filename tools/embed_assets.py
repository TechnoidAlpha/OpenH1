#!/usr/bin/env python3
"""Embeds the high-res HUD textures (port/assets/hud, made by
tools/hud_assets.py), the menus' titles (port/assets/titles, made by
tools/title_assets.py), their controller button icons (port/assets/buttons,
made by tools/button_assets.py), the fonts the text is drawn with
(port/assets/fonts), the menus' files (port/assets/menus, made by
tools/ce_menus.py), the windows' icon and the loading picture in the game
as C data:

    python tools/embed_assets.py OUTPUT.c

writes OUTPUT.c with each PNG and the bitmap it stands for (its tag, index
and checksum of its pixels, from port/assets/hud/layout.json,
port/assets/titles/titles.json and port/assets/buttons/buttons.json), the
fonts and their tags from port/assets/fonts/fonts.json, the menu files
listed in port/assets/menus/menus.json, the icon and the picture, as
port/linux/src/hud_hires.h, text_hires.h, menu_files.h and sdl_platform.c
declare them. The builds generate it (hud_assets_build, called by
tools/android_build.py, which the other guest builds share), so the assets
are the committed source and the guests need no files beside their image.

The data are 32-bit words, not bytes: the Android build passes the guest's
assembly through tools/android_asm_convert.py, which rewrites identifiers in
operands (it keeps .ascii strings as they are, but once garbled them).
"""

import json
import math
import struct
import sys
import zlib
from pathlib import Path
from typing import Any, List

ROOT = Path(__file__).resolve().parent.parent
HUD_ASSETS = Path("port/assets/hud")
LAYOUT = HUD_ASSETS / "layout.json"
CE_LAYOUT = HUD_ASSETS / "ce_layout.json"
CE_STOCK = HUD_ASSETS / "ce_stock.json"
# the HUD textures whose bitmaps Halo Custom Edition lays out alike
CUSTOM_EDITION_HUD = HUD_ASSETS / "custom_edition.json"
TITLE_ASSETS = Path("port/assets/titles")
TITLE_LIST = TITLE_ASSETS / "titles.json"
BUTTON_ASSETS = Path("port/assets/buttons")
BUTTON_LIST = BUTTON_ASSETS / "buttons.json"
FONT_ASSETS = Path("port/assets/fonts")
FONT_LIST = FONT_ASSETS / "fonts.json"
MENU_ASSETS = Path("port/assets/menus")
MENU_LIST = MENU_ASSETS / "menus.json"
# the desktop windows' icon (tools/app_icon.py; port/linux/src/sdl_platform.c)
WINDOW_ICON = Path("port/assets/icon/openh1-icon-256.png")
LOADING_PICTURE = Path("port/assets/loading/loading.rgba.zlib")
# the pointer the menus show in place of the system's (port/linux/src/sdl_platform.c)
MENU_CURSOR = Path("port/assets/cursor/menu_cursor.png")
# the menus' pictures drawn again from the player's maps: a pose each (any
# <bitmap>_<frame>.json dropped in), and the maps they are made for
# (port/linux/include/menu_pictures.h)
PICTURE_ASSETS = Path("port/assets/pictures")
PICTURE_MAPS = PICTURE_ASSETS / "maps.json"
# the bitmaps of ui.map whose frames a pose may draw
PICTURE_BITMAPS = {"sp_levels": "ui\\shell\\bitmaps\\sp_levels", "mp_map_grafix": "ui\\shell\\bitmaps\\mp_map_grafix"}
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def font_files() -> List[str]:
    """The font files fonts.json uses, each once."""
    if not (ROOT / FONT_LIST).is_file():
        return []
    fonts = json.loads((ROOT / FONT_LIST).read_text())["fonts"]
    return sorted({font["file"] for font in fonts})


def textures() -> List[tuple]:
    """The textures: each one's folder, its entry in its list, and whether
    it is the menus' (a title or a button icon, drawn with the high-res
    text)."""
    result = []
    for folder, listing, title in ((HUD_ASSETS, LAYOUT, False), (TITLE_ASSETS, TITLE_LIST, True),
                                   (BUTTON_ASSETS, BUTTON_LIST, True), (HUD_ASSETS, CE_LAYOUT, False)):
        if (ROOT / listing).is_file():
            result += [(folder, asset, title) for asset in json.loads((ROOT / listing).read_text())["assets"]]
    return result


def menu_files() -> List[str]:
    """The menus' files menus.json lists, relative to their folder."""
    if not (ROOT / MENU_LIST).is_file():
        return []
    return json.loads((ROOT / MENU_LIST).read_text())["files"]


def picture_poses() -> List[Path]:
    """The pictures' pose files, by name."""
    folder = ROOT / PICTURE_ASSETS
    if not folder.is_dir():
        return []
    return sorted(PICTURE_ASSETS / path.name for path in folder.glob("*.json") if path.name != PICTURE_MAPS.name)


def picture_number(value: float) -> str:
    return ("%.4f" % value).rstrip("0").rstrip(".")


def picture_pose(path: Path, maps: dict) -> dict:
    """A pose file's entry in menu_picture_poses (menu_pictures.h); exits on
    a pose the game could not use."""
    data = (ROOT / path).read_bytes()
    pose = json.loads(data)
    bitmap, _, frame = path.stem.rpartition("_")
    if bitmap not in PICTURE_BITMAPS or not frame.isdigit():
        sys.exit(f"{path}: name it <bitmap>_<frame>.json, of {', '.join(PICTURE_BITMAPS)}")
    scenario = pose["scenario"]
    map_name = scenario.replace("/", "\\").rsplit("\\", 1)[-1]
    if map_name not in maps:
        sys.exit(f"{path}: {map_name} is not in {PICTURE_MAPS} (its cache file checksum)")
    camera = pose["pose"]
    # (the vertical field of view as the game's, across at 4:3; the script's
    # camera halts at 90 degrees: camera_pose.c)
    fov = math.degrees(2 * math.atan(math.tan(math.radians(camera["vfov"]) / 2) * 4 / 3))
    if not 0 < fov < 90:
        sys.exit(f"{path}: a field of view of {fov:.1f} degrees (it must be under 90)")
    words = [camera["x"], camera["y"], camera["z"], camera["yaw"] % 360, camera["pitch"], camera["roll"], fov]
    commands = list(pose.get("prepare", []))
    objects = pose.get("objects", [])
    if objects:
        commands.append('debug_object_place "clear"')
    for item in objects:
        if "seat" in item:
            line = [item["tag"], "seat", item["seat"]]
        else:
            line = [item["tag"]] + [picture_number(item.get(key, 0.0)) for key in ("x", "y", "z", "yaw", "pitch", "roll")]
        if "color" in item:
            line += ["color"] + [picture_number(value) for value in item["color"]]
        commands.append('debug_object_place "%s"' % " ".join(line))
    # ("raw" and "none": the picture's colours as rendered)
    colour = pose.get("colour", "raw")
    if colour not in ("raw", "none", "curve"):
        sys.exit(f'{path}: colour is "raw" or "curve"')
    # (how long the world runs for the objects to settle; 0: left as placed)
    settle = float(pose.get("settle_seconds", 3.0)) if objects else 0.0
    if not 0 <= settle <= 30:
        sys.exit(f"{path}: settle_seconds is 0 to 30")
    return {"name": path.stem, "bitmap": PICTURE_BITMAPS[bitmap], "frame": int(frame), "map": map_name,
            "scenario": scenario, "bsp": int(pose.get("bsp", 0)), "aspect": float(pose["aspect"]),
            "colour": int(colour == "curve"), "settle": int(round(settle * 1000)),
            "camera": " ".join(picture_number(value) for value in words),
            "commands": "".join(line + "\n" for line in commands),
            "checksums": [int(value, 16) for value in maps[map_name]][:4], "hash": zlib.crc32(data)}


def c_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def picture_lines() -> List[str]:
    """The poses' table (menu_pictures.h)."""
    maps = json.loads((ROOT / PICTURE_MAPS).read_text())["maps"] if (ROOT / PICTURE_MAPS).is_file() else {}
    poses = [picture_pose(path, maps) for path in picture_poses()]
    lines = ['#include "menu_pictures.h"', "", "const struct menu_picture_pose menu_picture_poses[] =", "{"]
    for pose in poses:
        checksums = ", ".join(f"0x{value:08x}u" for value in pose["checksums"])
        lines.append(f'\t{{ {c_string(pose["name"])}, {c_string(pose["bitmap"])}, {pose["frame"]}, '
                     f'{c_string(pose["map"])}, {c_string(pose["scenario"])}, {pose["bsp"]}, {pose["aspect"]!r}f, '
                     f'{pose["colour"]}, {pose["settle"]}, {c_string(pose["camera"])}, {c_string(pose["commands"])}, '
                     f'{{ {checksums} }}, {len(pose["checksums"])}, 0x{pose["hash"]:08x}u }},')
    if not poses:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append(f"const unsigned int menu_picture_pose_count = {len(poses)};")
    lines.append("")
    return lines


def hud_asset_inputs() -> List[Path]:
    """The files the generated source is made from."""
    inputs = [listing for listing in (LAYOUT, CE_LAYOUT, CE_STOCK, CUSTOM_EDITION_HUD, TITLE_LIST, BUTTON_LIST, FONT_LIST, MENU_LIST)
              if (ROOT / listing).is_file()]
    return [*inputs, *(folder / f"{asset['name']}.png" for folder, asset, _ in textures()),
            *(FONT_ASSETS / name for name in font_files()), *(MENU_ASSETS / name for name in menu_files()),
            *([WINDOW_ICON] if (ROOT / WINDOW_ICON).is_file() else []),
            *([LOADING_PICTURE] if (ROOT / LOADING_PICTURE).is_file() else []),
            *([MENU_CURSOR] if (ROOT / MENU_CURSOR).is_file() else []),
            *([PICTURE_MAPS] if (ROOT / PICTURE_MAPS).is_file() else []), *picture_poses()]


def hud_configure_inputs() -> List[Path]:
    """What configure.py is run again for: the lists of assets (and the
    folders, for files added or removed), not each file, which a change of a
    list may rename or remove."""
    inputs = []
    for folder, listing in ((HUD_ASSETS, LAYOUT), (HUD_ASSETS, CE_LAYOUT), (TITLE_ASSETS, TITLE_LIST), (BUTTON_ASSETS, BUTTON_LIST),
                            (FONT_ASSETS, FONT_LIST), (MENU_ASSETS, MENU_LIST)):
        if (ROOT / listing).is_file():
            inputs += [folder, listing]
    # (a pose dropped in changes the folder: configure.py runs again)
    if (ROOT / PICTURE_ASSETS).is_dir():
        inputs.append(PICTURE_ASSETS)
    return inputs


def words(data: bytes) -> List[str]:
    """data as lines of 32-bit words (padded with zeros)."""
    padded = data + b"\0" * (-len(data) % 4)
    values = struct.unpack(f"<{len(padded) // 4}I", padded)
    return ["\t" + ", ".join(f"0x{word:08x}" for word in values[start:start + 8]) + ","
            for start in range(0, len(values), 8)]


def hud_assets_build(n: Any, prefix: str, output: Path) -> List[Path]:
    """Emits the rule that generates output; returns [output]. It is made
    whatever assets the checkout has (with none, its tables are empty), so
    that the symbols the platform layer refers to are always defined."""
    inputs = hud_asset_inputs()
    n.rule(
        name=f"{prefix}_embed_assets",
        command="$python tools/embed_assets.py $out",
        description=f"{prefix.upper()} EMBED $out",
    )
    n.build(outputs=output, rule=f"{prefix}_embed_assets", implicit=[Path("tools/embed_assets.py"), *inputs])
    return [output]


def write_output(output: Path, lines: List[str]) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    if not output.is_file() or output.read_text() != text:
        output.write_text(text)
def server_ui_build(n: Any, prefix: str, output: Path) -> List[Path]:
    """Emits the rule that generates a dedicated server's admin page as C
    (tools/server_ui_embed.py, port/assets/server_ui); returns [output]"""
    ui = Path("port/assets/server_ui")
    inputs = sorted(path for path in (ROOT / ui).glob("*") if path.is_file())
    n.rule(
        name=f"{prefix}_server_ui",
        command="$python tools/server_ui_embed.py $out",
        description=f"{prefix.upper()} EMBED $out",
    )
    n.build(outputs=output, rule=f"{prefix}_server_ui",
            implicit=[Path("tools/server_ui_embed.py"), Path("tools/server_sim.py"),
                      *(ui / path.name for path in inputs)])
    return [output]


def png_size(data: bytes, name: str) -> tuple:
    """The width and height of an 8-bit RGBA, non-interlaced PNG (the only
    kind port/linux/src/hud_hires.c reads)."""
    if data[:8] != PNG_SIGNATURE or data[12:16] != b"IHDR":
        sys.exit(f"{name}: not a PNG")
    width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", data[16:29])
    if depth != 8 or colour != 6 or interlace != 0:
        sys.exit(f"{name}: must be 8-bit RGBA and not interlaced")
    return width, height


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit("usage: embed_assets.py OUTPUT.c")
    lines = [
        "/* generated by tools/embed_assets.py from port/assets: do not edit */",
        "",
        '#include "hud_hires.h"',
        "",
    ]
    table = []
    custom_edition = {}
    if (ROOT / CUSTOM_EDITION_HUD).is_file():
        custom_edition = {(entry["tag"], entry["bitmap"]): entry
                          for entry in json.loads((ROOT / CUSTOM_EDITION_HUD).read_text())["assets"]}
    for index, (folder, asset, title) in enumerate(textures()):
        name = f"{asset['name']}.png"
        data = (ROOT / folder / name).read_bytes()
        width, height = png_size(data, name)
        scale = asset["scale"]
        if (width, height) != (asset["width"] * scale, asset["height"] * scale):
            sys.exit(f"{name}: {width}x{height}, not {scale}x its bitmap's {asset['width']}x{asset['height']}")
        lines.append(f"static const unsigned int asset{index}[] = {{")
        lines.extend(words(data))
        lines.append("};")
        lines.append("")
        tag = asset["tag"].replace("\\", "\\\\")
        coverage = int(asset.get("coverage", False) or any(cell.get("kind") == "meter" for cell in asset.get("cells", [])))
        point_threshold = int(asset.get("point_threshold", False) or any(cell.get("thresholds") for cell in asset.get("cells", [])))
        # (the sequences whose sprites it stands for, a bit each of 32; 0: the whole bitmap)
        sequences = [sprite["sequence"] for sprite in asset.get("sprites", [])]
        if any(not 0 <= sequence < 32 for sequence in sequences):
            sys.exit(f"{name}: sprite sequences must be 0 to 31 (hud_hires.h: sprites)")
        sprites = sum(1 << sequence for sequence in set(sequences))
        # (their cells, each checked on its own texels, where the list has
        # their checksums: hud_hires.h, hud_hires_cell)
        cells = [sprite for sprite in asset.get("sprites", []) if "crc" in sprite]
        if cells:
            lines.append(f"static const struct hud_hires_cell cells{index}[] = {{")
            lines.extend(f"\t{{ {cell['sequence']}, {', '.join(str(value) for value in cell['cell'])}, "
                         f"0x{cell['crc']:08x}u }}," for cell in cells)
            lines.append("};")
            lines.append("")
        # (a Custom Edition map's stock bitmap it is drawn for too, and that
        # bitmap's first level as the Custom Edition loader leaves it)
        ce = custom_edition.get((asset["tag"], asset["bitmap"]), {})
        if ce.get("replacement", asset["name"]) != asset["name"]:
            ce = {}
        table.append(f'\t{{ "{tag}", {asset["bitmap"]}, {width}, {height}, 0x{asset["crc"]:08x}u, {coverage}, '
                     f'{point_threshold}, {int(title)}, 0x{sprites:x}u, '
                     f'{f"cells{index}" if cells else "0"}, {len(cells)}, '
                     f'{int(bool(ce))}, {int(asset.get("ce_only", False))}, {ce.get("width", 0)}, {ce.get("height", 0)}, '
                     f'{ce.get("size", 0)}, 0x{ce.get("crc", 0):08x}u, asset{index}, {len(data)} }},')
    lines.append("const struct hud_hires_embedded hud_hires_embedded[] =")
    lines.append("{")
    lines.extend(table)
    if not table:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append(f"const unsigned int hud_hires_embedded_count = {len(table)};")
    # (the Custom Edition maps' stock HUD sheets the high-res HUD upscales,
    # each by its first level as the Custom Edition loader leaves it)
    stock = json.loads((ROOT / CE_STOCK).read_text())["bitmaps"] if (ROOT / CE_STOCK).is_file() else []
    lines.append("const struct hud_hires_ce_stock hud_hires_ce_stock[] = {")
    for item in stock:
        tag = item['tag'].replace('\\', '\\\\')
        lines.append(f'\t{{ "{tag}", {item["bitmap"]}, {item["width"]}, {item["height"]}, '
                     f'{item["size"]}, 0x{item["crc"]:08x}u }},')
    if not stock:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append(f"const unsigned int hud_hires_ce_stock_count = {len(stock)};")
    lines.append("")
    # the fonts, and which draws each font tag (text_hires.h)
    lines.append('#include "text_hires.h"')
    lines.append("")
    files = font_files()
    for index, name in enumerate(files):
        lines.append(f"static const unsigned int font{index}[] = {{")
        lines.extend(words((ROOT / FONT_ASSETS / name).read_bytes()))
        lines.append("};")
        lines.append("")
    fonts = json.loads((ROOT / FONT_LIST).read_text())["fonts"] if files else []
    lines.append("const struct text_hires_embedded text_hires_embedded[] =")
    lines.append("{")
    for font in fonts:
        index = files.index(font["file"])
        tag = font["tag"].replace("\\", "\\\\")
        size = (ROOT / FONT_ASSETS / font["file"]).stat().st_size
        lines.append(f'\t{{ "{tag}", "{font["file"]}", font{index}, {size} }},')
    if not fonts:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append(f"const unsigned int text_hires_embedded_count = {len(fonts)};")
    lines.append("")
    # the menus' files (menu_files.h)
    lines.append('#include "menu_files.h"')
    lines.append("")
    menus = menu_files()
    for index, name in enumerate(menus):
        data = (ROOT / MENU_ASSETS / name).read_bytes()
        if name.endswith(".png"):
            png_size(data, name)
        lines.append(f"static const unsigned int menu{index}[] = {{")
        lines.extend(words(data))
        lines.append("};")
        lines.append("")
    lines.append("const struct menu_file_embedded menu_files_embedded[] =")
    lines.append("{")
    for index, name in enumerate(menus):
        size = (ROOT / MENU_ASSETS / name).stat().st_size
        lines.append(f'\t{{ "{name}", menu{index}, {size} }},')
    if not menus:
        lines.append("\t{ 0 },")
    lines.append("};")
    lines.append(f"const unsigned int menu_files_embedded_count = {len(menus)};")
    # the windows' icon, a PNG (of size 0 where the checkout has none)
    data = (ROOT / WINDOW_ICON).read_bytes() if (ROOT / WINDOW_ICON).is_file() else b""
    lines.append("")
    lines.append("const unsigned int platform_window_icon[] = {")
    lines.extend(words(data) if data else ["\t0,"])
    lines.append("};")
    lines.append(f"const unsigned long platform_window_icon_size = {len(data)};")
    # the menus' pointer, a PNG (of size 0 where the checkout has none)
    data = (ROOT / MENU_CURSOR).read_bytes() if (ROOT / MENU_CURSOR).is_file() else b""
    lines.append("")
    lines.append("const unsigned int platform_menu_cursor[] = {")
    lines.extend(words(data) if data else ["\t0,"])
    lines.append("};")
    lines.append(f"const unsigned long platform_menu_cursor_size = {len(data)};")
    data = (ROOT / LOADING_PICTURE).read_bytes() if (ROOT / LOADING_PICTURE).is_file() else b""
    lines.append("")
    lines.append("const unsigned int platform_loading_picture[] = {")
    lines.extend(words(data) if data else ["\t0,"])
    lines.append("};")
    lines.append(f"const unsigned long platform_loading_picture_size = {len(data)};")
    lines.append("")
    lines.extend(picture_lines())
    write_output(Path(sys.argv[1]), lines)


if __name__ == "__main__":
    main()
