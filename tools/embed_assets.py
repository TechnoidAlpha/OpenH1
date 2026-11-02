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
import struct
import sys
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


def hud_asset_inputs() -> List[Path]:
    """The files the generated source is made from."""
    inputs = [listing for listing in (LAYOUT, CE_LAYOUT, CE_STOCK, CUSTOM_EDITION_HUD, TITLE_LIST, BUTTON_LIST, FONT_LIST, MENU_LIST)
              if (ROOT / listing).is_file()]
    return [*inputs, *(folder / f"{asset['name']}.png" for folder, asset, _ in textures()),
            *(FONT_ASSETS / name for name in font_files()), *(MENU_ASSETS / name for name in menu_files()),
            *([WINDOW_ICON] if (ROOT / WINDOW_ICON).is_file() else []),
            *([LOADING_PICTURE] if (ROOT / LOADING_PICTURE).is_file() else []),
            *([MENU_CURSOR] if (ROOT / MENU_CURSOR).is_file() else [])]


def hud_configure_inputs() -> List[Path]:
    """What configure.py is run again for: the lists of assets (and the
    folders, for files added or removed), not each file, which a change of a
    list may rename or remove."""
    inputs = []
    for folder, listing in ((HUD_ASSETS, LAYOUT), (HUD_ASSETS, CE_LAYOUT), (TITLE_ASSETS, TITLE_LIST), (BUTTON_ASSETS, BUTTON_LIST),
                            (FONT_ASSETS, FONT_LIST), (MENU_ASSETS, MENU_LIST)):
        if (ROOT / listing).is_file():
            inputs += [folder, listing]
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
    write_output(Path(sys.argv[1]), lines)


if __name__ == "__main__":
    main()
