"""The high-res text's spacing over every string the game shows (issues 744 and 754), with the maps' font tags
(assets/maps, or HALO_TEST_MAPS: ui.map and the other maps, for their string lists).

tools/text_spacing.py lays out every string of the menus and the maps as the high-res text draws it
(rasterizer_text.c's hires_text_run_lay_out, whose C tools/harness/tests/test_text_spacing.py checks): no run's
letters spaced unevenly against the font's own spacing, and no run's ink moved from the Xbox's, by more than 3% of the
capital height. Its negative control is the high-res text as it drew before, each glyph's advance centred on its
character's, which must fail.

The prompts' buttons (port/linux/game/hud_hires_tags.c) are drawn where the Xbox draws them: from the maps' message
icons' A, B, X and Y sprites, HUD_BUTTON_SPRITE_WIDTH units wide, their faces HUD_BUTTON_FACE_LEFT in."""

import os
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

MAPS = Path(os.environ.get("HALO_TEST_MAPS", ROOT / "assets" / "maps"))
LIMIT = 3.0

pytestmark = pytest.mark.skipif(not (MAPS / "ui.map").is_file(), reason=f"needs {MAPS}/ui.map")


@pytest.fixture(scope="module")
def strings():
    import text_spacing
    return text_spacing.every_string(MAPS / "ui.map", MAPS)


def failures(strings, place):
    import text_spacing
    tags, every = strings
    lines = []
    passed = text_spacing.report(text_spacing.measure(tags, every, place), LIMIT, LIMIT, worst=5, out=lines.append)
    return passed, "\n".join(lines)


def test_every_string_spaced_evenly_where_the_xbox_has_it(strings):
    passed, lines = failures(strings, "run")
    assert passed, lines


def test_negative_control_glyphs_centred_on_their_characters(strings):
    passed, lines = failures(strings, "advance")
    assert not passed, "the glyphs centred on their characters (as before issue 744) passed:\n" + lines


def button_sprites():
    """A multiplayer and two campaign maps' message icons' A, B, X and Y sprites: (width, the face's first and last
    column at half alpha)."""
    import numpy as np
    from hud_assets import XboxMap, decode_bitmap
    found = set()
    for path in (MAPS / name for name in ("bloodgulch.map", "a10.map", "d40.map")):
        if not path.is_file():
            continue
        xbox_map = XboxMap(path)
        name = "ui\\hud\\bitmaps\\combined\\hud_msg_icons"
        if ("bitm", name) not in xbox_map.tags:
            continue
        group = xbox_map.bitmap_group(name)
        images = {}
        for sequence in group["sequences"][:4]:
            index, left, top, right, bottom = sequence[0]
            bitmap = group["bitmaps"][index]
            image = images.setdefault(index, decode_bitmap(bitmap))
            x0, x1 = round(left * bitmap["width"]), round(right * bitmap["width"])
            y0, y1 = round(top * bitmap["height"]), round(bottom * bitmap["height"])
            columns = np.where(image[y0:y1, x0:x1, 3].max(axis=0) >= 128)[0]
            found.add((x1 - x0, int(columns[0]), int(columns[-1]) + 1))
    return found


def button_constants(source):
    return tuple(int(re.search(name + r" = (\d+)", source).group(1))
                 for name in ("HUD_BUTTON_SPRITE_WIDTH", "HUD_BUTTON_FACE_LEFT"))


def buttons_match(sprites, width, face_left):
    # (the face drawn 21 units wide: hud_button_face_bounds)
    return all(sprite == (width, face_left, face_left + 21) for sprite in sprites)


def test_buttons_where_the_xbox_draws_them():
    sprites = button_sprites()
    assert sprites, "no map has the message icons"
    source = (ROOT / "port/linux/game/hud_hires_tags.c").read_text()
    width, face_left = button_constants(source)
    assert buttons_match(sprites, width, face_left), f"the maps' button sprites {sprites}, the HUD's {width}, {face_left}"
    # (negative control: the face where it was drawn before issue 754, 6 units in)
    assert not buttons_match(sprites, width, face_left + 4)
    drawing = source.split("boolean hud_hires_button_draw(", 1)[1]
    assert "x = sprite_x + HUD_BUTTON_FACE_LEFT * viewport;" in drawing
    assert "(short)((HUD_BUTTON_SPRITE_WIDTH + icon->width_offset) * viewport + sprite_x)" in drawing
