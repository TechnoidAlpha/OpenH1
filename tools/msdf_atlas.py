#!/usr/bin/env python3
"""The MSDF atlas of the shipped fonts' characters, which the high-res text
is drawn from (port/linux/src/text_hires.c), baked once and committed, as the HUD's
pictures are:

    python3 tools/msdf_atlas.py            # writes port/assets/fonts/msdf_atlas.zlib
    python3 tools/msdf_atlas.py --check    # whether the committed one is what the fonts make
    python3 tools/msdf_atlas.py --preview OUT.png   # the atlas as a picture, to look at

Each font fonts.json names (port/assets/fonts) gets a field of each of its
characters in CODES (ASCII, Latin-1 and Windows-1252's punctuation and
letters, what the menus, the HUD and the console draw); a fallback only of
those its font lacks. The fields are made by port/third_party/msdfgen/msdf_gen.h,
the code the game makes any other character's with at run time, compiled
here with the computer's C compiler (tools/msdf_atlas/bake.c). The atlas is
derived from the fonts, so it is under their licences (the SIL Open Font
License 1.1; Hack's MIT and Bitstream Vera licences), which the releases
carry beside it (port/assets/fonts/README.md).
"""

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "port/assets/fonts"
OUTPUT = FONTS / "msdf_atlas.zlib"
BAKE = ROOT / "tools/msdf_atlas/bake.c"

# texels to the em, the field's range in texels (the distance from its
# lowest value to its highest), the atlas's width
EM = 40
RANGE = 6
WIDTH = 1024

CODES = ("0x21-0x7e 0xa1-0xff 0x152 0x153 0x160 0x161 0x178 0x17d 0x17e 0x192 0x2c6 0x2dc "
         "0x2013 0x2014 0x2018-0x201a 0x201c-0x201e 0x2020-0x2022 0x2026 0x2030 0x2039 0x203a 0x20ac 0x2122")


def spec() -> str:
    fonts = json.loads((FONTS / "fonts.json").read_text())["fonts"]
    files, lacking = [], {}
    for font in fonts:
        if font["file"] not in files:
            files.append(font["file"])
    for font in fonts:
        fallback = font.get("fallback")
        if fallback and fallback not in files:
            files.append(fallback)
            lacking[fallback] = files.index(font["file"])
    lines = [f"em {EM} range {RANGE} width {WIDTH}"]
    for name in files:
        line = f"file {name} {FONTS / name}"
        if name in lacking:
            line += f" lacking {lacking[name]}"
        lines.append(line)
    lines.append(f"codes {CODES}")
    return "\n".join(lines) + "\n"


def bake() -> bytes:
    compiler = os.environ.get("CC") or shutil.which("cc") or shutil.which("clang")
    if not compiler:
        sys.exit("msdf_atlas.py: no C compiler")
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        tool = folder / "bake"
        # (no fused multiply-add: the same fields on every computer)
        subprocess.run([compiler, "-O2", "-ffp-contract=off", "-o", str(tool), str(BAKE), "-lm"], check=True)
        (folder / "spec").write_text(spec())
        subprocess.run([str(tool), str(folder / "raw"), str(folder / "spec")], check=True)
        return (folder / "raw").read_bytes()


def atlas_pixels(raw: bytes):
    """The atlas's width, height and RGBA pixels from its file."""
    width, height = struct.unpack_from("<HH", raw, 8)
    return width, height, raw[len(raw) - width * height * 4:]


def png(path: Path, width: int, height: int, rgba: bytes) -> None:
    rows = b"".join(b"\0" + rgba[row * width * 4:(row + 1) * width * 4] for row in range(height))

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--preview", type=Path)
    arguments = parser.parse_args()
    raw = bake()
    if arguments.preview:
        width, height, rgba = atlas_pixels(raw)
        # (the fields' alpha, the true distance, made opaque to look at)
        png(arguments.preview, width, height, bytes(value if index % 4 != 3 else 255 for index, value in enumerate(rgba)))
        return
    if arguments.check:
        committed = zlib.decompress(OUTPUT.read_bytes()) if OUTPUT.is_file() else b""
        if committed != raw:
            sys.exit(f"{OUTPUT.relative_to(ROOT)} is not what the fonts make: run tools/msdf_atlas.py")
        return
    OUTPUT.write_bytes(zlib.compress(raw, 9))
    print(f"{OUTPUT.relative_to(ROOT)}: {len(raw)} bytes, {OUTPUT.stat().st_size} compressed")


if __name__ == "__main__":
    main()
