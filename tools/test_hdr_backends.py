"""display.hdr's contract across the backends (docs/hdr.md, "Tests").

Vulkan's shaders cannot run without a Vulkan device, so on a Mac they are
made Metal's by SPIRV-Cross and run on its GPU (tools/hdr_backends/harness.m):

- the tone map and the encoding as the Vulkan backend ships them (the
  modules of port/linux/vulkan/xgpu_vulkan_post_shaders.c) over the golden
  colours the Metal backend's own are held to, against the reference
  (port/linux/src/xgpu_hdr.h);
- each tagged draw (every ceiling, a glow's expansion, the alpha limit, over
  each blend) as the Vulkan backend's SPIR-V emitter writes it (reading the
  pixel drawn over as an input attachment) and as the Metal backend's MSL
  emitter writes it (reading it as [[color(0)]]), drawn over the same pixels:
  the two must leave the same values, and none past what its tag allows;
  a screen effect's pass unclamped (XGPU_HDR_UNCLAMPED), its colour
  past 1 in both; and a self-illuminated surface's pass
  (XGPU_HDR_SELF_ILLUMINATION), the reference's values in both, past 1 only
  by its self-illumination, and the same pass untagged the SDR frame's.

Everywhere: the tone map and the encoding the two backends compile are one
GLSL source, but for where the uniforms are bound.
"""
import platform
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "port/linux/src"
HERE = ROOT / "tools/hdr_backends"
sys.path.insert(0, str(ROOT / "tools"))

import metal_post_shaders  # noqa: E402
import vulkan_post_shaders  # noqa: E402

CROSS = shutil.which("spirv-cross")
CC = shutil.which("cc") or shutil.which("clang")
ON_THE_MAC = sys.platform == "darwin" and platform.machine().lower() == "arm64"


def test_one_source():
    for name, code in vulkan_post_shaders.hdr_programs().items():
        vulkan = vulkan_post_shaders.translate("frag", code, vulkan_post_shaders.UNITS)
        metal = vulkan.replace("set = 0, binding = 1)", f"set = 0, binding = {metal_post_shaders.UNIFORMS_BUFFER})")
        assert metal != vulkan, name
        assert "uniform vec4 hdr" not in vulkan and "\tvec4 hdr;\n" in vulkan, name
    # (the multisampled tone map: the same, each sample's own pixel)
    tone_map = vulkan_post_shaders.hdr_programs()["tone_map"]
    assert vulkan_post_shaders.multisampled(tone_map).replace("subpassInputMS", "subpassInput").replace(
        ", gl_SampleID)", ")") == tone_map


def _arrays(path):
    text = path.read_text()
    return {name: [int(word, 16) for word in re.findall(r"0x([0-9a-f]{8})", body)]
            for name, body in re.findall(r"const uint32_t (\w+)\[\] =\n\{\n(.*?)\n\};", text, re.S)}


def _cross(spv, out, stage, entry, fetch=False):
    arguments = [CROSS, str(spv), "--msl", "--msl-version", "20300", "--msl-decoration-binding",
                 "--rename-entry-point", "main", entry, stage, "--output", str(out)]
    if fetch:
        arguments.insert(-2, "--msl-framebuffer-fetch")
    subprocess.run(arguments, check=True)


MEMBER = re.compile(r"^\s*(float[234]?) (\w+) \[\[(user\([^)]*\))\]\];", re.M)


def _vertex(fragment_source):
    """A vertex function for a fragment function: a point over each pixel,
    its inputs the fragment's (its diffuse colour, xD0, the pixel's colour;
    the rest zero)."""
    stage_in = re.search(r"fragment \w+ \w+\((\w+) in \[\[stage_in\]\]", fragment_source)[1]
    body = re.search(r"struct " + stage_in + r"\s*\{(.*?)\};", fragment_source, re.S)[1]
    members = MEMBER.findall(body)
    assert any(name == "xD0" for _, name, _ in members), members
    fields = "".join(f"\t{kind} {name} [[{attribute}]];\n" for kind, name, attribute in members)
    return ("#include <metal_stdlib>\nusing namespace metal;\n"
            "struct hdr_test_out\n{\n\tfloat4 position [[position]];\n\tfloat point_size [[point_size]];\n" + fields +
            "};\n"
            "vertex hdr_test_out hdr_test_vertex(uint vertex_id [[vertex_id]], constant float4 *colors [[buffer(0)]],\n"
            "\tconstant float &width [[buffer(1)]])\n{\n"
            "\thdr_test_out out = {};\n"
            "\tout.position = float4((float(vertex_id % uint(width)) + 0.5) / width * 2.0 - 1.0, 0.0, 0.0, 1.0);\n"
            "\tout.point_size = 1.0;\n"
            "\tout.xD0 = colors[vertex_id];\n"
            "\treturn out;\n}\n")


@pytest.fixture(scope="module")
def harness(tmp_path_factory):
    if not (ON_THE_MAC and CROSS and CC):
        pytest.skip("needs a Mac with Apple silicon, a C compiler and spirv-cross")
    folder = tmp_path_factory.mktemp("hdr_backends")
    binary = folder / "harness"
    subprocess.run([CC, "-fobjc-arc", "-O1", "-Wall", "-Werror", f"-I{SOURCE}", str(HERE / "harness.m"), "-o",
                    str(binary), "-framework", "Metal", "-framework", "Foundation"], check=True)
    return folder, binary


def _run(binary, *arguments):
    result = subprocess.run([str(binary), *arguments], capture_output=True, text=True, timeout=300)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def test_post_shaders_on_the_gpu(harness):
    folder, binary = harness
    post = folder / "post"
    post.mkdir()
    arrays = _arrays(ROOT / "port/linux/vulkan/xgpu_vulkan_post_shaders.c")
    for name, stage, entry, fetch in (("post_vert", "vert", "xgpu_vk_post_vertex", False),
                                      ("hdr_tone_map_frag", "frag", "xgpu_vk_tone_map", True),
                                      ("hdr_encode_frag", "frag", "xgpu_vk_encode", False)):
        spv = post / f"{name}.spv"
        spv.write_bytes(struct.pack(f"<{len(arrays[name])}I", *arrays[name]))
        _cross(spv, post / (name.replace("_frag", "") + ".metal"), stage, entry, fetch)
    output = _run(binary, "post", str(post))
    assert "post: 1152 values checked" in output, output
    assert "post: 9 values not a number or infinite tone mapped finite" in output, output


def test_draws_agree_on_the_gpu(harness):
    folder, binary = harness
    build = folder / "shaders"
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
    out = folder / "draws"
    subprocess.run([str(tool), str(out)], check=True)
    names = [line.split()[0] for line in (out / "index.txt").read_text().splitlines()[1:]]
    assert len(names) == 141
    validator = shutil.which("spirv-val")
    for name in names:
        # (the interlocked draws: valid for Vulkan 1.3, their target a storage image under a raster order group)
        if validator and "-interlock" in name:
            subprocess.run([validator, "--target-env", "vulkan1.3", str(out / f"{name}.spv")], check=True)
        _cross(out / f"{name}.spv", out / f"{name}.vk.metal", "frag", "xgpu_vk_fragment", fetch=True)
        if "-interlock" in name:
            assert "raster_order_group(0)" in (out / f"{name}.vk.metal").read_text(), name
        for stem in (f"{name}.vk", name):
            (out / f"{stem}.vertex.metal").write_text(_vertex((out / f"{stem}.metal").read_text()))
    output = _run(binary, "draws", str(out))
    assert "draws: 141 draws (72 interlocked), 288768 values compared" in output, output
