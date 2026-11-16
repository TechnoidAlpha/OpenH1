#!/usr/bin/env python3
"""Build Vulkan's anti-aliasing passes.

Run with glslangValidator and spirv-val on PATH:
    python3 tools/vulkan_post_shaders.py
The generated C (port/linux/vulkan/xgpu_vulkan_post_shaders.c) embeds the
SPIR-V of display.anti_aliasing's passes and SMAA's lookup textures,
inflated; the game needs no shader compiler and no zlib for them.
--check verifies that the checked-in file still matches its sources.

The passes are the GLSL below: a vertex shader, FXAA, and the main
functions of SMAA's three passes after SMAA.hlsl (port/third_party/smaa) at
its HIGH preset. Each pass draws one triangle covering the rectangle (its
corners from gl_VertexID, with no vertex data) and finds its texels from
gl_FragCoord. FXAA is written here, after Timothy Lottes' description: the
direction of an edge from the luma around a pixel, the edge's ends found by
stepping along it, and the pixel blended across it by where it lies between
them, or by how much it stands out from its neighbours. Only the interface
changes when they are built: the uniforms become a block (set 0, binding 1,
as the backend's other draws have their uniforms), the samplers take the
texture units UNITS gives them as their bindings (set 1), the output a
location, and gl_VertexID is gl_VertexIndex. The rotate program
(ROTATE_SOURCE) is Vulkan's own: the present step's turn for a swapchain the
display turns. display.hdr's tone map and encoding are xgpu_hdr.c's GLSL
(hdr_programs), the same source tools/metal_post_shaders.py compiles for
Metal: the tone map reads the pixel it draws over as its input attachment
(set 2, binding 0: VK_KHR_dynamic_rendering_local_read), and again as each
sample's own for a multisampled target (hdr_tone_map_ms). The coordinates
stay: the passes' textures and targets have their first row at the top, as
Vulkan's images do, and gl_FragCoord counts rows from it. glslang 16.6.0 and
SPIRV-Tools 2026.4 (Homebrew's) produce the checked-in words.

display.gamma's curve (GAMMA) is the last pass of an SDR frame, in both
backends.
display.panini's projection (PANINI) is a pass over a window's 3D view as
FXAA's is, in both backends; its uniforms add window, panini and tangents.

Beside them, CLEAR is the pixel shader of the core's clears of some of a
target's colour channels (xgpu_vulkan.c, xgpu_vulkan_clear), which
vkCmdClearAttachments cannot do: drawn with the vertex shader above,
through the channels' write mask, it writes the uniforms' colour.
"""

from __future__ import annotations

import argparse
import ast
from pathlib import Path
import re
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
# display.hdr's tone map and encoding (their functions and main functions)
HDR_SOURCE = ROOT / "port/linux/src/xgpu_hdr.c"
SMAA = ROOT / "port/third_party/smaa"
OUTPUT = ROOT / "port/linux/vulkan/xgpu_vulkan_post_shaders.c"

STRINGS = re.compile(r'"(?:\\.|[^"\\])*"')
COMMENTS = re.compile(r"/\*.*?\*/", re.S)
UNIFORM = re.compile(r"^uniform vec4 (\w+);\n", re.M)
SAMPLER = re.compile(r"^uniform sampler2D (\w+);\n", re.M)
# the uniforms' block: each pass's vec4s, in this order (std140), as far as
# the last its program uses (hdr in display.hdr's passes, window, panini
# and tangents in display.panini's)
UNIFORMS = ("metrics", "bounds", "hdr", "window", "panini", "tangents")
# the fragment programs, in xgpu_vulkan_post_shaders.h's order
PROGRAMS = ("fxaa", "smaa_edges", "smaa_weights", "smaa_blend", "rotate", "hdr_tone_map", "hdr_tone_map_ms",
            "hdr_encode", "gamma", "panini")
# one triangle over the whole viewport
VERTEX_SOURCE = (
    "void main()\n"
    "{\n"
    "\tgl_Position = vec4(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0, 0.0, 1.0);\n"
    "}\n"
)
FXAA_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "\n"
    "uniform vec4 metrics;\n"
    "\n"
    "uniform vec4 bounds;\n"
    "out vec4 result;\n"
    "\n"
    "const float EDGE_THRESHOLD = 0.125;\n"
    "const float EDGE_THRESHOLD_MINIMUM = 0.0312;\n"
    "const float SUBPIXEL_QUALITY = 0.75;\n"
    "const int STEPS = 12;\n"
    "const float STEP_LENGTHS[12] = float[12](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);\n"
    "\n"
    "vec3 color_at(vec2 position)\n"
    "{\n"
    "\treturn textureLod(color_texture, clamp(position, bounds.xy, bounds.zw), 0.0).rgb;\n"
    "}\n"
    "\n"
    "float luma_at(vec2 position)\n"
    "{\n"
    "\treturn dot(color_at(position), vec3(0.299, 0.587, 0.114));\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 position = gl_FragCoord.xy * metrics.xy;\n"
    "\tfloat luma = luma_at(position);\n"
    "\tfloat up = luma_at(position + vec2(0.0, -metrics.y));\n"
    "\tfloat down = luma_at(position + vec2(0.0, metrics.y));\n"
    "\tfloat left = luma_at(position + vec2(-metrics.x, 0.0));\n"
    "\tfloat right = luma_at(position + vec2(metrics.x, 0.0));\n"
    "\tfloat highest = max(luma, max(max(up, down), max(left, right)));\n"
    "\tfloat range = highest - min(luma, min(min(up, down), min(left, right)));\n"
    "\n"
    "\t\n"
    "\tif (range < max(EDGE_THRESHOLD_MINIMUM, highest * EDGE_THRESHOLD))\n"
    "\t\tdiscard;\n"
    "\tfloat up_left = luma_at(position + vec2(-metrics.x, -metrics.y));\n"
    "\tfloat up_right = luma_at(position + vec2(metrics.x, -metrics.y));\n"
    "\tfloat down_left = luma_at(position + vec2(-metrics.x, metrics.y));\n"
    "\tfloat down_right = luma_at(position + vec2(metrics.x, metrics.y));\n"
    "\n"
    "\t\n"
    "\tfloat average = (2.0 * (up + down + left + right) + up_left + up_right + down_left + down_right) / 12.0;\n"
    "\tfloat subpixel = smoothstep(0.0, 1.0, clamp(abs(average - luma) / range, 0.0, 1.0));\n"
    "\tsubpixel = subpixel * subpixel * SUBPIXEL_QUALITY;\n"
    "\n"
    "\t\n"
    "\tbool horizontal = abs(up_left + down_left - 2.0 * left) + 2.0 * abs(up + down - 2.0 * luma) +\n"
    "\t\tabs(up_right + down_right - 2.0 * right) >=\n"
    "\t\tabs(up_left + up_right - 2.0 * up) + 2.0 * abs(left + right - 2.0 * luma) +\n"
    "\t\tabs(down_left + down_right - 2.0 * down);\n"
    "\tfloat before = horizontal ? up : left;\n"
    "\tfloat after = horizontal ? down : right;\n"
    "\tbool before_steeper = abs(before - luma) >= abs(after - luma);\n"
    "\tfloat gradient = 0.25 * max(abs(before - luma), abs(after - luma));\n"
    "\tfloat edge_luma = 0.5 * ((before_steeper ? before : after) + luma);\n"
    "\tvec2 across = (horizontal ? vec2(0.0, metrics.y) : vec2(metrics.x, 0.0)) * (before_steeper ? -1.0 : 1.0);\n"
    "\tvec2 along = horizontal ? vec2(metrics.x, 0.0) : vec2(0.0, metrics.y);\n"
    "\n"
    "\t\n"
    "\tvec2 end_before = position + 0.5 * across - along;\n"
    "\tvec2 end_after = position + 0.5 * across + along;\n"
    "\tfloat luma_before = luma_at(end_before) - edge_luma;\n"
    "\tfloat luma_after = luma_at(end_after) - edge_luma;\n"
    "\tbool reached_before = abs(luma_before) >= gradient;\n"
    "\tbool reached_after = abs(luma_after) >= gradient;\n"
    "\tfor (int index = 1; index < STEPS && !(reached_before && reached_after); index++)\n"
    "\t{\n"
    "\t\tif (!reached_before)\n"
    "\t\t{\n"
    "\t\t\tend_before -= along * STEP_LENGTHS[index];\n"
    "\t\t\tluma_before = luma_at(end_before) - edge_luma;\n"
    "\t\t\treached_before = abs(luma_before) >= gradient;\n"
    "\t\t}\n"
    "\t\tif (!reached_after)\n"
    "\t\t{\n"
    "\t\t\tend_after += along * STEP_LENGTHS[index];\n"
    "\t\t\tluma_after = luma_at(end_after) - edge_luma;\n"
    "\t\t\treached_after = abs(luma_after) >= gradient;\n"
    "\t\t}\n"
    "\t}\n"
    "\n"
    "\t\n"
    "\tfloat distance_before = horizontal ? position.x - end_before.x : position.y - end_before.y;\n"
    "\tfloat distance_after = horizontal ? end_after.x - position.x : end_after.y - position.y;\n"
    "\tfloat pixel_offset = 0.5 - min(distance_before, distance_after) / (distance_before + distance_after);\n"
    "\tbool varies = ((distance_before < distance_after ? luma_before : luma_after) < 0.0) != (luma < edge_luma);\n"
    "\tresult = vec4(color_at(position + max(varies ? pixel_offset : 0.0, subpixel) * across), 1.0);\n"
    "}\n"
)
# the main functions of SMAA's passes, after SMAA.hlsl; its vertex functions'
# offsets are found for each pixel
SMAA_EDGES_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "out vec4 result;\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 texcoord = gl_FragCoord.xy * metrics.xy;\n"
    "\tvec4 offset[3];\n"
    "\tSMAAEdgeDetectionVS(texcoord, offset);\n"
    "\tresult = vec4(SMAALumaEdgeDetectionPS(texcoord, offset, color_texture), 0.0, 0.0);\n"
    "}\n"
)
SMAA_WEIGHTS_SOURCE = (
    "uniform sampler2D edges_texture;\n"
    "uniform sampler2D area_texture;\n"
    "uniform sampler2D search_texture;\n"
    "out vec4 result;\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 texcoord = gl_FragCoord.xy * metrics.xy;\n"
    "\tvec2 pixcoord;\n"
    "\tvec4 offset[3];\n"
    "\tSMAABlendingWeightCalculationVS(texcoord, pixcoord, offset);\n"
    "\tresult = SMAABlendingWeightCalculationPS(texcoord, pixcoord, offset, edges_texture, area_texture,\n"
    "\t\tsearch_texture, vec4(0.0));\n"
    "}\n"
)
SMAA_BLEND_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "uniform sampler2D weights_texture;\n"
    "out vec4 result;\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 texcoord = gl_FragCoord.xy * metrics.xy;\n"
    "\tvec4 offset;\n"
    "\tSMAANeighborhoodBlendingVS(texcoord, offset);\n"
    "\tresult = SMAANeighborhoodBlendingPS(texcoord, offset, color_texture, weights_texture);\n"
    "}\n"
)
SMAA_MAINS = (SMAA_EDGES_SOURCE, SMAA_WEIGHTS_SOURCE, SMAA_BLEND_SOURCE)
# what comes before SMAA.hlsl and a pass's main function
SMAA_PREFIX = (
    "#define SMAA_GLSL_4 1\n"
    "uniform vec4 metrics;\n"
    "#define SMAA_RT_METRICS metrics\n"
    "#define SMAA_PRESET_HIGH 1\n"
)
# each sampler's texture unit, which becomes its binding
UNITS = {"color_texture": 0, "edges_texture": 0, "area_texture": 1, "search_texture": 2, "weights_texture": 1}
# Vulkan's own program: the frame turned for a swapchain the display turns
# (a phone held sideways: xgpu_vulkan_post_rotate). Each pixel takes the
# texel the bounds' matrix (its rows xy and zw) turns it to, about the
# image's centre.
ROTATE_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "/* 1 / width, 1 / height, width, height of the turned image */\n"
    "uniform vec4 metrics;\n"
    "/* the turn: rows of a 2x2 matrix, from the turned image's coordinates to the frame's */\n"
    "uniform vec4 bounds;\n"
    "out vec4 result;\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 centred = gl_FragCoord.xy * metrics.xy - 0.5;\n"
    "\tvec2 coordinates = vec2(dot(bounds.xy, centred), dot(bounds.zw, centred)) + 0.5;\n"
    "\tresult = vec4(texture(color_texture, coordinates).rgb, 1.0);\n"
    "}\n"
)
# display.gamma's curve (both backends' own: xgpu_vulkan_post_gamma, and
# Metal's presentation, host_metal.m): the frame sampled over the viewport,
# as the encoding samples it, each channel raised to bounds.x (one over the
# setting: past 1 brighter, as Halo PC's gamma), alpha opaque
GAMMA_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "/* the viewport: x, y, 1 / width, 1 / height */\n"
    "uniform vec4 metrics;\n"
    "/* the power each channel is raised to */\n"
    "uniform vec4 bounds;\n"
    "out vec4 result;\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 position = (gl_FragCoord.xy - metrics.xy) * metrics.zw;\n"
    "\tvec3 color = max(textureLod(color_texture, position, 0.0).rgb, vec3(0.0));\n"
    "\tresult = vec4(pow(color, vec3(bounds.x)), 1.0);\n"
    "}\n"
)
# display.panini's projection (both backends': xgpu_vulkan_post_panini,
# xgpu_metal_post_panini; port/linux/include/panini_projection.h has the
# arithmetic, which this is the same as). Its uniforms: metrics, the
# target's; bounds, the rectangle's outermost texel centres (no texel of
# another window is read); window, the 3D view's corners in the target's
# pixels (x0, y0, width, height); panini, the projection's d, s and
# magnification at the middle; tangents, the rectilinear view's at the
# view's left, right, top and bottom. Each pixel takes the copy's pixel the
# rectilinear view saw its direction at, sampled over the pixels it covers
# there (the sides are drawn smaller): a bilinear tap for each two pixels
# each way, so that the taps' filters together cover them (up to 4 taps, 8
# pixels)
PANINI_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "\n"
    "uniform vec4 metrics;\n"
    "uniform vec4 bounds;\n"
    "uniform vec4 window;\n"
    "uniform vec4 panini;\n"
    "uniform vec4 tangents;\n"
    "out vec4 result;\n"
    "\n"
    "/* the rectilinear tangents a point of the screen at tangents p shows */\n"
    "vec2 rectilinear(vec2 p)\n"
    "{\n"
    "\tfloat d = panini.x;\n"
    "\tvec2 projected = p / panini.z;\n"
    "\tfloat k = projected.x * projected.x / ((d + 1.0) * (d + 1.0));\n"
    "\tfloat c = max((sqrt(1.0 + k * (1.0 - d * d)) - k * d) / (k + 1.0), 1.0e-6);\n"
    "\tfloat S = (d + 1.0) / (d + c);\n"
    "\treturn vec2(projected.x / (S * c), projected.y / (S * (panini.y + (1.0 - panini.y) * c)));\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tvec2 place = (gl_FragCoord.xy - window.xy) / window.zw;\n"
    "\tvec2 seen = rectilinear(vec2(mix(tangents.x, tangents.y, place.x), mix(tangents.z, tangents.w, place.y)));\n"
    "\tvec2 source = window.xy + window.zw * (seen - tangents.xz) / (tangents.yw - tangents.xz);\n"
    "\tvec2 across = dFdx(source);\n"
    "\tvec2 down = dFdy(source);\n"
    "\tivec2 taps = ivec2(clamp(ceil(vec2(length(across), length(down)) * 0.5 - 0.01), 1.0, 4.0));\n"
    "\tvec4 sum = vec4(0.0);\n"
    "\tfor (int row = 0; row < taps.y; row++)\n"
    "\t{\n"
    "\t\tfor (int column = 0; column < taps.x; column++)\n"
    "\t\t{\n"
    "\t\t\tvec2 offset = (vec2(column, row) + 0.5) / vec2(taps) - 0.5;\n"
    "\t\t\tvec2 at = (source + across * offset.x + down * offset.y) * metrics.xy;\n"
    "\t\t\tsum += textureLod(color_texture, clamp(at, bounds.xy, bounds.zw), 0.0);\n"
    "\t\t}\n"
    "\t}\n"
    "\tresult = sum / float(taps.x * taps.y);\n"
    "}\n"
)
# the clears' pixel shader (its uniforms at the passes' binding)
CLEAR = """#version 450
layout(std140, set = 0, binding = 1) uniform ClearUniforms
{
\tvec4 color;
};
layout(location = 0) out vec4 result;
void main()
{
\tresult = color;
}
"""
# SMAA's lookup textures: their file, size and channels
TEXTURES = (("area", "area_tex.zlib", 160, 560, 2), ("search", "search_tex.zlib", 64, 16, 1))


def literal(text: str) -> str:
    """The C string literals of text (comments removed), joined."""
    return "".join(ast.literal_eval(value) for value in STRINGS.findall(COMMENTS.sub("", text)))


def declaration(source: str, name: str) -> str:
    match = re.search(r"(?:static )?const char " + name + r"\[\]\s*=\s*(.*?);\n", source, re.S)
    if not match:
        raise ValueError(f"{name} not found in {HDR_SOURCE.name}")
    return literal(match[1])


def smaa_hlsl() -> str:
    """SMAA.hlsl, its few characters beyond ASCII (in comments) spaces."""
    return bytes(byte if byte < 0x80 else 0x20 for byte in (SMAA / "SMAA.hlsl").read_bytes()).decode("ascii")


def hdr_programs() -> dict[str, str]:
    """display.hdr's fragment programs (xgpu_hdr.c): the tone map, in place
    (the pixel drawn over is the pass's input attachment), and the encoding,
    each its main function after the functions they share."""
    source = HDR_SOURCE.read_text()
    functions = declaration(source, "xgpu_hdr_functions_source")
    return {name: functions + declaration(source, f"{name}_source") for name in ("tone_map", "encode")}


def multisampled(code: str) -> str:
    """The tone map of a multisampled target: each sample's own pixel (which
    makes the pass run for each sample)."""
    changed = code.replace("uniform subpassInput ", "uniform subpassInputMS ").replace(
        "subpassLoad(frame_input)", "subpassLoad(frame_input, gl_SampleID)")
    if changed.count("subpassInputMS") != 1 or "gl_SampleID" not in changed:
        raise ValueError("the tone map's input attachment changed")
    return changed


def translate(stage: str, code: str, bindings: dict[str, int]) -> str:
    found = UNIFORM.findall(code)
    unknown = set(found) - set(UNIFORMS)
    if unknown:
        raise ValueError(f"uniforms {sorted(unknown)} are not in the block")
    code = UNIFORM.sub("", code)
    used = max([1] + [UNIFORMS.index(name) for name in found])

    def sampler(match):
        return f"layout(set = 1, binding = {bindings[match[1]]}) uniform sampler2D {match[1]};\n"

    code = SAMPLER.sub(sampler, code)
    code = code.replace("out vec4 result;\n", "layout(location = 0) out vec4 result;\n")
    code = code.replace("gl_VertexID", "gl_VertexIndex")
    block = ""
    if stage == "frag":
        block = "layout(std140, set = 0, binding = 1) uniform PostUniforms\n{\n"
        block += "".join(f"\tvec4 {name};\n" for name in UNIFORMS[:used + 1]) + "};\n"
    return "#version 450\n" + block + code


def words_array(name: str, data: bytes, public: bool = False) -> str:
    data += b"\0" * (-len(data) % 4)
    words = struct.unpack(f"<{len(data) // 4}I", data)
    lines = [f"{'' if public else 'static '}const uint32_t {name}[] =\n{{"]
    for index in range(0, len(words), 8):
        lines.append("\t" + ", ".join(f"0x{word:08x}" for word in words[index:index + 8]) + ",")
    lines.append("};\n")
    return "\n".join(lines)


def generate(args):
    build = args.build_dir
    build.mkdir(parents=True, exist_ok=True)
    hlsl = smaa_hlsl()
    programs = {
        "post": ("vert", VERTEX_SOURCE),
        "fxaa": ("frag", FXAA_SOURCE),
    }
    for name, main in zip(PROGRAMS[1:4], SMAA_MAINS):
        programs[name] = ("frag", SMAA_PREFIX + hlsl + "\n" + main)
    programs["rotate"] = ("frag", ROTATE_SOURCE)
    hdr = hdr_programs()
    programs["hdr_tone_map"] = ("frag", hdr["tone_map"])
    programs["hdr_tone_map_ms"] = ("frag", multisampled(hdr["tone_map"]))
    programs["hdr_encode"] = ("frag", hdr["encode"])
    programs["gamma"] = ("frag", GAMMA_SOURCE)
    programs["panini"] = ("frag", PANINI_SOURCE)
    programs["clear"] = ("frag", CLEAR)
    text = [
        "/* Generated by tools/vulkan_post_shaders.py from its GLSL, xgpu_hdr.c and port/third_party/smaa. "
        "Do not edit. */\n",
        '#include "xgpu_vulkan_post_shaders.h"\n',
    ]
    for name, (stage, code) in programs.items():
        path = build / f"{name}.{stage}"
        path.write_text(code if name == "clear" else translate(stage, code, UNITS))
        spv = path.with_suffix(f".{stage}.spv")
        spv.unlink(missing_ok=True)
        subprocess.run([args.compiler, "-V", "--target-env", "vulkan1.1", "-o", str(spv), str(path)],
                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        subprocess.run([args.validator, "--target-env", "vulkan1.1", str(spv)], check=True)
        text.append(words_array(f"{name}_{stage}", spv.read_bytes()))
        print(f"{name}: {spv.stat().st_size} bytes of SPIR-V")
    text.append("const struct xgpu_vulkan_post_module xgpu_vulkan_post_vertex =\n{")
    text.append("\tpost_vert, sizeof(post_vert) / 4,\n};\n")
    text.append("const struct xgpu_vulkan_post_module xgpu_vulkan_post_programs[XGPU_VULKAN_POST_PROGRAM_COUNT] =\n{")
    for name in PROGRAMS:
        text.append(f"\t{{ {name}_frag, sizeof({name}_frag) / 4 }},")
    text.append("};\n")
    text.append("const struct xgpu_vulkan_post_module xgpu_vulkan_post_clear =\n{")
    text.append("\tclear_frag, sizeof(clear_frag) / 4,\n};\n")
    for name, file, width, height, channels in TEXTURES:
        texels = zlib.decompress((SMAA / file).read_bytes())
        if len(texels) != width * height * channels:
            raise ValueError(f"{file} is {len(texels)} bytes, not {width}x{height}x{channels}")
        text.append(words_array(f"xgpu_vulkan_post_{name}_texture", texels, public=True))
    generated = "\n".join(text)
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != generated:
            raise SystemExit(f"{OUTPUT.relative_to(ROOT)} differs; regenerate it")
    else:
        OUTPUT.write_text(generated)
    print(f"{len(programs)} modules validated for Vulkan 1.1")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compiler", default="glslangValidator")
    parser.add_argument("--validator", default="spirv-val")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/vulkan_post_shaders")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        generate(args)
    except subprocess.CalledProcessError as error:
        if error.stdout:
            print(error.stdout.decode())
        raise


if __name__ == "__main__":
    main()
