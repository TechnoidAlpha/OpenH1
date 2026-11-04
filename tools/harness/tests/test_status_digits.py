"""Multiplayer's panel's counts (ONLINE NOW's games and players, #519): each digit of the bitmap tools/ce_menus.py
draws (Art.digits) clear of its frame's sides by port_settings.DIGIT_MARGIN, so that none is cut where it meets the
next (OpenCE's widest digits at the titles' height were wider than the frame); the frames as wide as
port_settings.DIGIT_SIZE, and as wide as menu_functions.c lays the digits out."""

import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, read  # noqa: E402

sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

SCALE = int(re.search(r"^SCALE = (\d+)", read("tools/ce_menus.py"), re.M).group(1))


def alpha_columns(path):
    """the PNG's size and, for each column, whether any pixel in it is not transparent (8-bit RGBA or grey and
    alpha, or a palette with transparency, not interlaced)"""
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    position, chunks, idat = 8, {}, b""
    while position < len(data):
        length, kind = struct.unpack(">I4s", data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        if kind == b"IDAT":
            idat += body
        else:
            chunks[kind] = body
        position += 12 + length
    width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunks[b"IHDR"])
    assert depth == 8 and interlace == 0, (path, depth, interlace)
    channels = {6: 4, 4: 2, 3: 1, 2: 3, 0: 1}[color]
    stride = width * channels
    raw = zlib.decompress(idat)
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(line)
        previous = line
    if color == 3:
        transparency = chunks.get(b"tRNS", b"")
        opaque = [index >= len(transparency) or transparency[index] for index in range(256)]
        alpha = lambda row, x: 255 if opaque[row[x]] else 0  # noqa: E731
    elif color in (6, 4):
        alpha = lambda row, x: row[x * channels + channels - 1]  # noqa: E731
    else:
        alpha = lambda row, x: 255  # noqa: E731
    return (width, height), [any(alpha(row, x) for row in rows) for x in range(width)]


def clear_of_sides(columns, margin):
    """whether what is drawn (columns) keeps margin columns clear at either side, and something is drawn"""
    drawn = [x for x, used in enumerate(columns) if used]
    return bool(drawn) and drawn[0] >= margin and drawn[-1] < len(columns) - margin


def test_digits_clear_of_their_frames():
    folder = ROOT / "port/assets/menus" / f"ce/port/{port_settings.STATUS_DIGITS}__0.png"
    margin = int(port_settings.DIGIT_MARGIN * SCALE)
    for digit in range(10):
        path = folder.with_name(f"{Path(port_settings.STATUS_DIGITS).name}__{digit}.png")
        size, columns = alpha_columns(path)
        assert size == (port_settings.DIGIT_SIZE[0] * SCALE, port_settings.DIGIT_SIZE[1] * SCALE), (digit, size)
        assert clear_of_sides(columns, margin), f"{digit} reaches its frame's side"


def test_negative_control_cut_digit():
    """a digit drawn to its frame's sides, as master's 6 was (columns 0 to 95 of 96), is caught; one with its margin
    is not"""
    assert not clear_of_sides([True] * 96, 2)
    assert not clear_of_sides([False] * 2 + [True] * 94, 2)
    assert clear_of_sides([False] * 2 + [True] * 92 + [False] * 2, 2)
    assert not clear_of_sides([False] * 96, 2)


def test_frames_as_laid_out():
    bitmaps = read("port/assets/menus/ce/bitmaps.xml")
    frames = re.findall(rf'<frame png="ce/port/{re.escape(port_settings.STATUS_DIGITS)}__\d\.png" '
                        r'width="(\d+)" height="(\d+)"/>', bitmaps)
    assert len(frames) == 10
    assert {(int(w), int(h)) for w, h in frames} == {port_settings.DIGIT_SIZE}
    menus = read("port/linux/game/menu_functions.c")
    assert int(re.search(r"#define MULTIPLAYER_STATUS_DIGIT_WIDTH (\d+)", menus).group(1)) == \
        port_settings.DIGIT_SIZE[0]
