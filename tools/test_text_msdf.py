"""The high-res text's distance fields (port/linux/src/text_hires.c, port/third_party/msdfgen/msdf_gen.h):

- the committed atlas (port/assets/fonts/msdf_atlas.zlib) is what the fonts
  make (tools/msdf_atlas.py --check);
- each glyph drawn from the distance fields is laid out exactly as it was
  when the text was rasterized: its quad, advance and ink are the font's box
  for it at the display's pixels (stb_truetype's), for the shipped fonts' tags,
  at 720p, 1080p and 4K and drawn twice as large, the baked characters and
  some made at run time (tools/text_msdf/layout.c, with text_hires.c built
  for this computer and stand-ins for the platform);
- the Metal and Vulkan shaders' coverage of a field agree: the MSL
  emitter's and the SPIR-V emitter's (made Metal's by SPIRV-Cross) drawn on
  a Mac's GPU over a glyph's field from 8 to 400 pixels tall
  (tools/text_msdf/shaders.c, harness.m), and the Metal backend's combiner
  has the emitter's function.
"""
import json
import platform
import struct
import zlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/linux/src"
FONTS = ROOT / "port/assets/fonts"
CC = shutil.which("cc") or shutil.which("clang")
CROSS = shutil.which("spirv-cross")
ON_THE_MAC = sys.platform == "darwin" and platform.machine().lower() == "arm64"
HERE = ROOT / "tools/text_msdf"

sys.path.insert(0, str(ROOT / "tools"))


@pytest.mark.skipif(not CC, reason="no C compiler")
def test_atlas_is_the_fonts():
    subprocess.run([sys.executable, str(ROOT / "tools/msdf_atlas.py"), "--check"], check=True)


STUBS = {
    "platform.h": "#include <pthread.h>\n#include <stdlib.h>\ntypedef int BOOL;\n#define FALSE 0\n#define TRUE 1\n"
                  "void platform_log(const char *format, ...);\n",
    "xgpu_images.h": "enum { XGPU_IMAGE_RGBA };\n"
                     "unsigned int xgpu_image_create(void *pixels, unsigned long width, unsigned long height, "
                     "unsigned long levels, int format, int owned);\nvoid xgpu_image_changed(unsigned int image);\n"
                     "void xgpu_image_delete(unsigned int image);\n"
                     "static void xgpu_texture_replacements_changed(void) {}\n",
}


def _generated(folder: Path) -> Path:
    """The embedded fonts' table and the atlas's place, in a blob of them."""
    fonts = json.loads((FONTS / "fonts.json").read_text())["fonts"]
    blob, offsets = bytearray(), {}
    for name in sorted({name for font in fonts for name in (font["file"], font.get("fallback")) if name}):
        offsets[name] = (len(blob), (FONTS / name).stat().st_size)
        blob += (FONTS / name).read_bytes()
    atlas = (FONTS / "msdf_atlas.zlib").read_bytes()
    atlas_offset = len(blob)
    blob += atlas
    lines = ['#include "text_hires.h"', "const struct text_hires_embedded text_hires_embedded[] = {"]
    for font in fonts:
        tag = font["tag"].replace("\\", "\\\\")
        fields = [f'"{name}", {offsets[name][0]}u, {offsets[name][1]}' if name else "0, 0u, 0"
                  for name in (font["file"], font.get("fallback"))]
        lines.append(f'\t{{ "{tag}", {fields[0]}, {fields[1]} }},')
    lines += ["};", f"const unsigned int text_hires_embedded_count = {len(fonts)};",
              f"const unsigned int text_hires_msdf_offset = {atlas_offset}u;",
              f"const unsigned int text_hires_msdf_size = {len(atlas)}u;"]
    (folder / "generated.c").write_text("\n".join(lines) + "\n")
    (folder / "blob").write_bytes(bytes(blob))
    return folder / "blob"


@pytest.mark.skipif(not CC, reason="no C compiler")
def test_layout_is_the_fonts(tmp_path):
    source = tmp_path / "port/linux/src"
    source.mkdir(parents=True)
    (tmp_path / "port/third_party").symlink_to(ROOT / "port/third_party")
    for name in ("text_hires.c", "text_hires.h", "embedded_assets.h", "port_config.h"):
        shutil.copy(SOURCE / name, source / name)
    for name, text in STUBS.items():
        (source / name).write_text(text)
    # (the range the shaders smooth by, as xgpu_shader.h has it)
    shader = (SOURCE / "xgpu_shader.h").read_text()
    (source / "xgpu_shader.h").write_text(re.search(r"#define XGPU_MSDF_RANGE .*\n", shader)[0])
    blob = _generated(source)
    tool = tmp_path / "layout"
    subprocess.run([CC, "-O1", "-ffp-contract=off", "-w", "-I", str(source), "-o", str(tool),
                    str(ROOT / "tools/text_msdf/layout.c"), str(source / "text_hires.c"), str(source / "generated.c"),
                    "-lz", "-lm", "-lpthread"], check=True)
    result = subprocess.run([str(tool), str(blob)], capture_output=True, text=True, timeout=600)
    print(result.stdout)
    assert result.returncode == 0, result.stdout[-4000:]
    checked, made = map(int, re.search(r"(\d+) glyphs checked, (\d+) of them made", result.stdout).groups())
    assert checked > 2000 and made > 0


def _function(source: str, name: str) -> str:
    """A function of a C string literal's MSL, its quoting undone."""
    lines = re.findall(r'^\s*"(.*)";?$', source, re.M)
    text = "".join(lines).replace("\\n", "\n").replace("\\t", "\t").replace('" XGPU_MSDF_RANGE_TEXT "', "R")
    match = re.search(r"static inline float4 " + name + r"\(.*?\n\}\n", text, re.S)
    assert match, name
    return match[0]


def test_combiner_has_the_emitters_function():
    emitter = _function((SOURCE / "xgpu_msl.c").read_text(), "msdf_coverage")
    combiner = _function((ROOT / "port/macos/xgpu_metal_combiner.c").read_text(), "msdf_coverage")
    assert emitter == combiner


def _vertex(fragment_source: str) -> str:
    """A vertex function for a fragment function: a quad over the target,
    xT0 its texture's coordinates (0 to 1), xD0 white, the rest zero."""
    stage_in = re.search(r"fragment \w+ \w+\((\w+) in \[\[stage_in\]\]", fragment_source)[1]
    body = re.search(r"struct " + stage_in + r"\s*\{(.*?)\};", fragment_source, re.S)[1]
    members = re.findall(r"^\s*(float[234]?) (\w+) \[\[(user\([^)]*\))\]\];", body, re.M)
    assert any(name == "xT0" for _, name, _ in members), members
    fields = "".join(f"\t{kind} {name} [[{attribute}]];\n" for kind, name, attribute in members)
    return ("#include <metal_stdlib>\nusing namespace metal;\n"
            "struct msdf_test_out\n{\n\tfloat4 position [[position]];\n" + fields + "};\n"
            "vertex msdf_test_out msdf_test_vertex(uint vertex_id [[vertex_id]])\n{\n"
            "\tmsdf_test_out out = {};\n"
            "\tfloat2 corner = float2(float(vertex_id & 1u), float(vertex_id >> 1));\n"
            "\tout.position = float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);\n"
            "\tout.xT0 = float4(corner, 0.0, 1.0);\n"
            "\tout.xD0 = float4(1.0);\n"
            "\treturn out;\n}\n")


def _field(code: int) -> bytes:
    """A baked glyph's field of Cyborg Interface Gothic: its width, height and RGBA."""
    raw = zlib.decompress((FONTS / "msdf_atlas.zlib").read_bytes())
    width, height = struct.unpack_from("<HH", raw, 8)
    files = struct.unpack_from("<H", raw, 20)[0]
    offset = 22 + files * 52
    count = struct.unpack_from("<I", raw, offset)[0]
    offset += 4
    pixels = raw[offset + count * 24:]
    for index in range(count):
        file, _, character, x, y, w, h, _, _ = struct.unpack_from("<HHIHHHHff", raw, offset + index * 24)
        if file == 0 and character == code:
            rows = b"".join(pixels[((y + row) * width + x) * 4:((y + row) * width + x + w) * 4] for row in range(h))
            return struct.pack("<II", w, h) + rows
    raise LookupError(code)


@pytest.mark.skipif(not (ON_THE_MAC and CROSS and CC), reason="needs a Mac with Apple silicon, cc and spirv-cross")
def test_backends_agree_on_the_gpu(tmp_path):
    build = tmp_path / "shaders"
    (build / "src").mkdir(parents=True)
    for name in ("xgpu.h", "xgpu_backend.h", "xgpu_hdr.h", "xgpu_shader.h", "xgpu_spirv.h", "xgpu_msl.h", "xgpu_text.c",
                 "xgpu_spirv.c", "xgpu_msl.c"):
        shutil.copy(SOURCE / name, build / "src" / name)
    xdk = (ROOT / "port/include/xdk/xdk_pdb.h").read_text(encoding="latin-1")
    enums = xdk[xdk.index("/* ---------- enumerations"):xdk.index("/* ---------- structures and unions")]
    (build / "xdk_enums.h").write_text(enums, encoding="latin-1")
    tool = build / "shaders"
    subprocess.run([CC, "-std=gnu99", "-O1", "-Wall", f"-I{build}", f"-I{ROOT / 'tools/msl_corpus/stub'}",
                    f"-I{build / 'src'}", "-o", str(tool), str(HERE / "shaders.c"), str(build / "src/xgpu_text.c"),
                    str(build / "src/xgpu_spirv.c"), str(build / "src/xgpu_msl.c"), "-lm"], check=True)
    out = tmp_path / "out"
    subprocess.run([str(tool), str(out)], check=True)
    subprocess.run([CROSS, str(out / "msdf.spv"), "--msl", "--msl-version", "20300", "--msl-decoration-binding",
                    "--rename-entry-point", "main", "xgpu_vk_fragment", "frag", "--output", str(out / "msdf.vk.metal")],
                   check=True)
    if shutil.which("spirv-val"):
        subprocess.run(["spirv-val", "--target-env", "vulkan1.1", str(out / "msdf.spv")], check=True)
    for stem in ("msdf", "msdf.vk"):
        (out / f"{stem}.vertex.metal").write_text(_vertex((out / f"{stem}.metal").read_text()))
    # ("g": curves, a corner, a hole's worth of inside and outside)
    (out / "field.raw").write_bytes(_field(ord("g")))
    binary = tmp_path / "harness"
    subprocess.run([CC, "-fobjc-arc", "-O1", "-Wall", "-Werror", str(HERE / "harness.m"), "-o", str(binary),
                    "-framework", "Metal", "-framework", "Foundation"], check=True)
    result = subprocess.run([str(binary), str(out)], capture_output=True, text=True, timeout=300)
    print(result.stdout)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "pixels compared" in result.stdout
