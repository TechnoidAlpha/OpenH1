"""Reversed depth (port/linux/src/xgpu_backend.h, "Reversed depth"): the
Metal and Vulkan backends keep depth as 1 - the game's, and their emitters
compute it from the projection's rows, so that a light a millimetre or two in
front of its wall (Blood Gulch's bases' red lights) is in front of it at the
sniper rifle's range, where the game's own depth fights the wall.

tools/reversed_depth/check.c, built with the shader decoders and the Metal
emitter (as tools/msl_corpus is):

- the game's 67 vertex programs (the decompilation's microcode): every one
  that projects through the game's rows has them found
  (xgpu_vertex_program_depth_rows), and its Metal shader computes its depth
  from them; every one reverses its depth;
- a light 1.5 mm in front of its wall 100 m away, in the world's
  coordinates: with the game's depth some of its points fail the depth
  test (the stripes), with reversed depth none do.
"""
import re
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/linux/src"
VERTEX = ROOT / "source/rasterizer/xbox"
CC = shutil.which("cc") or shutil.which("clang")
SHARED = ["xgpu.h", "xgpu_backend.h", "xgpu_hdr.h", "xgpu_shader.h", "xgpu_msl.h", "xgpu_stream.h", "nv2a_vsh.c",
          "nv2a_psh.c", "xgpu_text.c", "xgpu_msl.c"]


@pytest.fixture(scope="module")
def check(tmp_path_factory):
    if not CC:
        pytest.skip("no C compiler")
    build = tmp_path_factory.mktemp("reversed_depth")
    (build / "src").mkdir()
    for name in SHARED:
        shutil.copy(SOURCE / name, build / "src" / name)
    # (the XDK's enumerations, as tools/msl_corpus/build.sh takes them)
    pdb = (ROOT / "port/include/xdk/xdk_pdb.h").read_text()
    enums = pdb.split("/* ---------- enumerations", 1)[1].split("/* ---------- structures and unions", 1)[0]
    (build / "xdk_enums.h").write_text("/* ---------- enumerations" + enums)
    executable = build / "check"
    subprocess.run([CC, "-std=gnu99", "-O1", "-ffp-contract=off", "-Wall", f"-I{build}",
                    f"-I{ROOT / 'tools/msl_corpus/stub'}", f"-I{build / 'src'}", "-o", str(executable),
                    str(ROOT / "tools/reversed_depth/check.c")] +
                   [str(build / "src" / name) for name in SHARED if name.endswith(".c")] + ["-lm"], check=True)
    return executable


def game_programs(directory):
    """the decompilation's vertex programs as captures (vsNNN.bin: no packed inputs, the microcode)"""
    shaders = (VERTEX / "rasterizer_xbox_vertex_shaders.c").read_text()
    data = (VERTEX / "rasterizer_xbox_vertex_shaders_data.inc").read_text()
    code = [int(word, 16) for word in re.findall(r"0x([0-9A-Fa-f]{8})", data.split("*/", 1)[1])]
    entries = [(int(offset, 16), int(size, 16)) for offset, size in
               re.findall(r"VERTEX_SHADER_ENTRY\((0x[0-9A-F]+), (0x[0-9A-F]+)\)", shaders)]
    for index, (offset, size) in enumerate(entries):
        count = code[offset // 4] >> 16
        words = code[offset // 4 + 1:offset // 4 + 1 + count * 4]
        (directory / f"vs{index + 1:03d}.bin").write_bytes(struct.pack("<I", 0) + struct.pack(f"<{len(words)}I", *words))
    return len(entries)


def test_programs(check, tmp_path):
    count = game_programs(tmp_path)
    result = subprocess.run([str(check), "programs", str(tmp_path)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout
    # (one draws in the screen's coordinates, its depth none: the rest project through the game's rows)
    assert f"programs {count} with rows {count - 1} without 1" in result.stdout, result.stdout
    assert "projects 0" in result.stdout or "projects 1" in result.stdout


def precision(check, *arguments):
    """the light's points that pass the depth test, the game's depth's and reversed, and how many there are"""
    result = subprocess.run([str(check), "precision", *map(str, arguments)], capture_output=True, text=True,
                            check=True)
    counts = dict(re.findall(r"^(\w+) (\d+) of \d+$", result.stdout, re.MULTILINE))
    total = int(re.search(r"of (\d+)", result.stdout).group(1))
    return int(counts["game"]), int(counts["reversed"]), total


def test_precision(check):
    game, reversed_, total = precision(check)
    assert reversed_ == total
    # (the game's own depth: the light striped)
    assert game < total * 0.9


@pytest.mark.parametrize("distance, gap", [(100.0, 0.0015), (3000.0, 0.02), (9000.0, 0.1)])
def test_precision_far_draw_distance(check, distance, gap):
    """display.draw_distance's farthest far plane, 10000 (port/linux/game/far_clip.c): the light 0.0015 units
    in front of its wall 100 units away is whole, as at the game's 1024 (the near world loses nothing), and far
    surfaces stay apart: 0.02 units (6 cm) at 3000 units, 0.1 (30 cm) at 9000, where a 24-bit depth's step is
    about 9 and 77 units"""
    game, reversed_, total = precision(check, 10000.0, distance, gap)
    assert reversed_ == total
    # (negative control: the game's own depth, z / w, fights the wall there)
    assert game < total * 0.9

