"""The high-res HUD's committed assets (port/assets/hud, port/assets/buttons):
what tools/hud_assets.py and button_assets.py wrote, checked without the
game's data.

    python -m pytest -q tools/test_hud_assets.py
"""

import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
HUD = ROOT / "port/assets/hud"
BUTTONS = ROOT / "port/assets/buttons"
sys.path.insert(0, str(ROOT / "tools"))

# (port/linux/game/hud_hires_tags.c: MAXIMUM_HIRES_SPRITE_BITMAPS; hud_hires.h:
# a bit of 32 for each sequence)
MAXIMUM_SPRITE_BITMAPS = 8


def entries(listing: Path) -> list:
    return json.loads(listing.read_text())["assets"]


def test_every_cell_has_its_source():
    for entry in entries(HUD / "layout.json"):
        for cell in entry["cells"]:
            if "art" in cell:
                assert (HUD / "art" / cell["art"]).is_file(), (entry["name"], cell["art"])
            else:
                assert (HUD / "svg" / cell["svg"]).is_file(), (entry["name"], cell["svg"])
        png = Image.open(HUD / f"{entry['name']}.png")
        assert png.size == (entry["width"] * entry["scale"], entry["height"] * entry["scale"]), entry["name"]


def test_art_is_used_and_credited():
    used = {cell["art"] for entry in entries(HUD / "layout.json") for cell in entry["cells"] if "art" in cell}
    from hud_assets import BUTTON_ART

    on_disk = {path.name for path in (HUD / "art").glob("*.png")}
    assert on_disk == used | set(BUTTON_ART)
    notice = (HUD / "NOTICE.md").read_text()
    assert "Aerocatia" in notice and "Jessie" in notice and "r220" in notice
    assert "Halo PC Restored" in (ROOT / "AUTHORS.md").read_text()


def test_restored_art_is_the_hud_and_redraws_only_where_it_has_nothing_larger():
    redrawn = {entry["name"] for entry in entries(HUD / "layout.json")
               if all("svg" in cell for cell in entry["cells"])}
    assert redrawn == {"hud_health_base__0", "hud_sensor_blip__0", "hud_sensor_blip_custom__0", "hud_sweeper__0",
                       "hud_sweeper_mask__0", "vehicle_wrench_outline__0", "passenger_health_outline__0"}
    notice = (HUD / "NOTICE.md").read_text()
    for name in redrawn:
        assert f"{name}.png" in notice, name


def test_sprite_sheets_fit_the_game():
    sheets = [entry for listing in (HUD / "layout.json", BUTTONS / "buttons.json")
              for entry in entries(listing) if entry.get("sprites")]
    assert len(sheets) <= MAXIMUM_SPRITE_BITMAPS
    for entry in sheets:
        assert all(0 <= sprite["sequence"] < 32 for sprite in entry["sprites"]), entry["name"]
    # (one texture for a bitmap)
    stood_for = [(entry["tag"], entry["bitmap"]) for listing in (HUD / "layout.json", BUTTONS / "buttons.json")
                 for entry in entries(listing) if entry.get("crc")]
    assert len(stood_for) == len(set(stood_for))


def label_setting(svg: Path) -> tuple:
    """What a label's SVG says it is: (its text, font, spacing, weight)."""
    import re

    match = re.search(r'<!-- "([^"]+)" in (\S+), spacing (-?[\d.]+), weight ([\d.]+) -->', svg.read_text())
    return match and (match[1], match[2], float(match[3]), float(match[4]))


def test_zoom_labels_are_set_in_the_text_font_apart_and_at_one_weight(tmp_path):
    """The sniper's 2x and 10x (issue 756): Cyborg Interface Gothic, the
    high-res text's font, emboldened to one weight, their glyphs apart."""
    from hud_assets import LABEL_FONT, LABEL_GAP, text_gap

    scope = next(entry for entry in entries(HUD / "layout.json") if entry["name"] == "sniper__hud_reticles_scope__0")
    settings = [label_setting(HUD / "svg" / cell["svg"]) for cell in scope["cells"] if "svg" in cell]
    assert sorted(setting[0] for setting in settings) == ["10x", "2x"]
    assert {setting[1] for setting in settings} == {LABEL_FONT.name} == {"CyborgInterfaceGothic-Regular.ttf"}
    assert len({setting[3] for setting in settings}) == 1 and settings[0][3] > 0
    for text, _, spacing, weight in settings:
        assert text_gap(ROOT / LABEL_FONT, text, spacing, weight) >= LABEL_GAP, text
    # (negative controls: the Overpass label before, which says no weight;
    # and a spacing that runs the emboldened 2 into its x)
    before = tmp_path / "before.svg"
    before.write_text('<!-- "10x" in Overpass-900.ttf -->')
    assert label_setting(before) is None
    assert text_gap(ROOT / LABEL_FONT, "2x", -4, 6) < LABEL_GAP
