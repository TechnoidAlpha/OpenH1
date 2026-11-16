"""The in-game menus' pictures of the maps (ui\\shell\\bitmaps: the pause menus' highlight, the buttons' icons) are
drawn from high-res textures with at least as many texels as 4K has pixels for them (issue 596: the pause menu's
highlight, the map's 256x32 menu_bkds, had its corners in steps of four and a half pixels): each texture a whole
multiple of its bitmap at that scale or more, its PNG that size, and the highlight among them
(port/assets/buttons/buttons.json, tools/button_assets.py)."""
import json
import struct
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
BUTTONS = ROOT / "port/assets/buttons"
# the display's pixels for each of the 480 lines at 4K (2160 lines; d3d8_device.c's halo_screen_pixel_scale)
LARGEST_PIXELS_PER_UNIT = 2160 / 480
MENUS = "ui\\shell\\bitmaps\\"
# the pause menus' highlight of the row chosen: menu_bkds's second frame (every game map's)
HIGHLIGHT = (MENUS + "menu_bkds", 1)


def png_size(path: Path) -> tuple:
    header = path.read_bytes()[:24]
    assert header[:8] == b"\x89PNG\r\n\x1a\n", path
    return struct.unpack(">II", header[16:24])


def problems(assets: list, folder: Path) -> list:
    """what is wrong with the menus' textures: one too coarse for 4K, a PNG not its size, the highlight not there"""
    found = []
    menus = [entry for entry in assets if entry["tag"].startswith(MENUS)]
    if not any((entry["tag"], entry["bitmap"]) == HIGHLIGHT for entry in menus):
        found.append(f"{HIGHLIGHT[0]} frame {HIGHLIGHT[1]} (the pause menus' highlight) has no high-res texture")
    for entry in menus:
        if entry["scale"] < LARGEST_PIXELS_PER_UNIT:
            found.append(f"{entry['name']}: {entry['scale']} texels a unit, fewer than 4K's {LARGEST_PIXELS_PER_UNIT}")
        size = png_size(folder / f"{entry['name']}.png")
        if size != (entry["width"] * entry["scale"], entry["height"] * entry["scale"]):
            found.append(f"{entry['name']}.png: {size}, not {entry['scale']}x {entry['width']}x{entry['height']}")
    return found


def assets() -> list:
    return json.loads((BUTTONS / "buttons.json").read_text())["assets"]


def test_menu_textures_crisp_at_4k():
    assert problems(assets(), BUTTONS) == []


@pytest.mark.parametrize("fault", ["highlight-missing", "highlight-coarse", "png-small"])
def test_negative_controls(fault, tmp_path):
    """the check sees a highlight left out, one drawn at the menus' 4x, and a PNG smaller than its entry says"""
    entries = [dict(entry) for entry in assets()]
    highlight = next(entry for entry in entries if (entry["tag"], entry["bitmap"]) == HIGHLIGHT)
    for entry in entries:
        (tmp_path / f"{entry['name']}.png").write_bytes((BUTTONS / f"{entry['name']}.png").read_bytes())
    if fault == "highlight-missing":
        entries.remove(highlight)
    elif fault == "highlight-coarse":
        highlight["scale"] = 4
    else:
        data = bytearray((tmp_path / f"{highlight['name']}.png").read_bytes())
        data[16:24] = struct.pack(">II", 1024, 128)
        (tmp_path / f"{highlight['name']}.png").write_bytes(bytes(data))
    assert problems(entries, tmp_path), f"the check did not see {fault}"
