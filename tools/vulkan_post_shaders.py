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

display.ambient_occlusion's passes (OCCLUSION_*: the depth made
distances, GTAO, a blur along a direction, the occlusion laid over the
target) are Vulkan's own too (xgpu_vulkan_post.c); their uniforms add
camera, occlusion and sampling to the block. display.gamma's curve (GAMMA)
is the last pass of an SDR frame, in both backends.

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
# the last its program uses (hdr in display.hdr's passes, camera, occlusion
# and sampling in display.ambient_occlusion's)
UNIFORMS = ("metrics", "bounds", "hdr", "camera", "occlusion", "sampling")
# the fragment programs, in xgpu_vulkan_post_shaders.h's order
PROGRAMS = ("fxaa", "smaa_edges", "smaa_weights", "smaa_blend", "rotate", "hdr_tone_map", "hdr_tone_map_ms",
            "hdr_encode", "occlusion_depth", "occlusion", "occlusion_blur", "occlusion_apply", "gamma")
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
UNITS = {"color_texture": 0, "edges_texture": 0, "area_texture": 1, "search_texture": 2, "weights_texture": 1,
         "depth_texture": 0, "mask_texture": 1, "distance_texture": 2}
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
# display.ambient_occlusion's passes (Vulkan's own: xgpu_vulkan_post.c,
# xgpu_vulkan_post_ambient_occlusion). Their uniforms: metrics, the
# occlusion's image (1 / width, 1 / height, width, height); bounds, the 3D
# view's rectangle in the occlusion's pixels; camera, the projection's x and
# y scales and offsets; occlusion, the camera's near and far planes, the
# radius (world units) and the strength; sampling, the slices, the steps
# each side, the power and the target's pixels to the occlusion's (1 or 2),
# or the blur's direction (xy). The mask (the stencil's bits made a
# texture): red an object's pixel, green a pixel the occlusion leaves (the
# first-person weapon's, a self-illuminated surface's).
OCCLUSION_UNIFORMS = (
    "uniform vec4 metrics;\n"
    "uniform vec4 bounds;\n"
    "uniform vec4 hdr;\n"
    "uniform vec4 camera;\n"
    "uniform vec4 occlusion;\n"
    "uniform vec4 sampling;\n"
    "out vec4 result;\n"
)
# the depth made the distance along the view (reversed Z: 1 at the near
# plane, 0 at the far plane and where nothing was drawn), at the target's
# resolution, as a half float and what it leaves (red plus green), the
# precision a wall's slope needs
OCCLUSION_DEPTH_SOURCE = (
    "uniform sampler2D depth_texture;\n"
    + OCCLUSION_UNIFORMS +
    "\n"
    "void main()\n"
    "{\n"
    "\tfloat reversed = texelFetch(depth_texture, ivec2(gl_FragCoord.xy), 0).r;\n"
    "\tfloat near = occlusion.x, far = occlusion.y;\n"
    "\tfloat distance = reversed > 0.0 ? near * far / (near + reversed * (far - near)) : far;\n"
    "\tfloat high = unpackHalf2x16(packHalf2x16(vec2(distance, 0.0))).x;\n"
    "\tresult = vec4(high, distance - high, 0.0, 1.0);\n"
    "}\n"
)
# GTAO (Jimenez et al., "Practical Real-Time Strategies for Accurate
# Indirect Occlusion", 2016, as Intel's XeGTAO has it): in each of a few
# directions about the pixel (a slice of the view through it, turned by a
# 4x4 Bayer matrix), the highest horizon each side within the radius (each
# sample's rise above the pixel's tangent plane), its samples' weight
# falling off to none at the radius; the visible arc
# between them integrated with the cosine about the pixel's normal, which
# the depth gives (the nearer of each side's neighbours). Only what the
# lighting does not already hold: an object's pixel (lit by one ambient
# sample) is occluded by everything about it, the level's (whose lightmap
# holds the level's own occlusion) only by objects. The result: the light
# the pixel still sees, as a share of what its slices see unoccluded (1
# none occluded), its distance and whether it is an object's, for the blur
# and the filtering up.
OCCLUSION_SOURCE = (
    "uniform sampler2D depth_texture;\n"
    "uniform sampler2D mask_texture;\n"
    + OCCLUSION_UNIFORMS +
    "\n"
    "const float PI = 3.14159265;\n"
    "\n"
    "ivec2 full_texel(ivec2 texel)\n"
    "{\n"
    "\treturn clamp(texel, ivec2(bounds.xy), ivec2(bounds.zw) - 1) * int(sampling.w);\n"
    "}\n"
    "\n"
    "vec3 position_at(ivec2 texel)\n"
    "{\n"
    "\ttexel = clamp(texel, ivec2(bounds.xy), ivec2(bounds.zw) - 1);\n"
    "\tvec2 parts = texelFetch(depth_texture, texel * int(sampling.w), 0).rg;\n"
    "\tfloat z = parts.r + parts.g;\n"
    "\tvec2 ndc = (vec2(texel) + 0.5 - bounds.xy) / (bounds.zw - bounds.xy) * 2.0 - 1.0;\n"
    "\treturn vec3((ndc - camera.zw) / camera.xy * z, z);\n"
    "}\n"
    "\n"
    "/* a 4x4 Bayer matrix's: every one of its sixteen values in each four by\n"
    "four pixels, which the blur then averages */\n"
    "float noise(ivec2 texel)\n"
    "{\n"
    "\tconst int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);\n"
    "\treturn (float(bayer[(texel.y & 3) * 4 + (texel.x & 3)]) + 0.5) / 16.0;\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "\tivec2 texel = ivec2(gl_FragCoord.xy);\n"
    "\tvec3 centre = position_at(texel);\n"
    "\tvec2 mask = texelFetch(mask_texture, full_texel(texel), 0).rg;\n"
    "\tfloat object = mask.r > 0.5 ? 1.0 : 0.0;\n"
    "\tfloat radius = occlusion.z;\n"
    "\tfloat screen_radius = min(radius * 0.5 * (bounds.z - bounds.x) * camera.x / centre.z,\n"
    "\t\t0.12 * (bounds.w - bounds.y));\n"
    "\tif (mask.g > 0.5 || centre.z >= occlusion.y * 0.99 || screen_radius < 1.0)\n"
    "\t{\n"
    "\t\tresult = vec4(1.0, centre.z, object, 1.0);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tvec3 left = centre - position_at(texel - ivec2(1, 0)), right = position_at(texel + ivec2(1, 0)) - centre;\n"
    "\tvec3 up = centre - position_at(texel - ivec2(0, 1)), down = position_at(texel + ivec2(0, 1)) - centre;\n"
    "\tvec3 normal = normalize(cross(abs(up.z) < abs(down.z) ? up : down, abs(left.z) < abs(right.z) ? left : right));\n"
    "\tif (dot(normal, centre) > 0.0)\n"
    "\t\tnormal = -normal;\n"
    "\tvec3 view = normalize(-centre);\n"
    "\tfloat falloff_range = 0.615 * radius;\n"
    "\tfloat falloff_multiply = -1.0 / falloff_range;\n"
    "\tfloat falloff_add = (radius - falloff_range) / falloff_range + 1.0;\n"
    "\tint slices = int(sampling.x), steps = int(sampling.y);\n"
    "\tfloat slice_noise = noise(texel), step_noise = noise(texel.yx + ivec2(1, 2));\n"
    "\tfloat visibility = 0.0, unoccluded = 0.0;\n"
    "\tfor (int slice = 0; slice < slices; slice++)\n"
    "\t{\n"
    "\t\tfloat angle = (float(slice) + slice_noise) / float(slices) * PI;\n"
    "\t\tvec2 omega = vec2(cos(angle), sin(angle));\n"
    "\t\t/* (the screen's direction in the view, whose x and y scales differ) */\n"
    "\t\tvec3 direction = normalize(vec3(omega / ((bounds.zw - bounds.xy) * camera.xy), 0.0));\n"
    "\t\tvec3 across = direction - dot(direction, view) * view;\n"
    "\t\tvec3 axis = normalize(cross(across, view));\n"
    "\t\tvec3 projected = normal - axis * dot(normal, axis);\n"
    "\t\tfloat projected_length = max(length(projected), 1e-5);\n"
    "\t\tfloat cos_n = clamp(dot(projected, view) / projected_length, -1.0, 1.0);\n"
    "\t\tfloat n = (dot(across, projected) < 0.0 ? -1.0 : 1.0) * acos(cos_n);\n"
    "\t\t/* (each side's highest rise above the pixel's own surface, its tangent\n"
    "\t\tplane: a sample of a flat surface rises by none, however its offset was\n"
    "\t\trounded to a texel off the slice) */\n"
    "\t\tfloat rise0 = 0.0, rise1 = 0.0;\n"
    "\t\tfor (int taken = 0; taken < steps; taken++)\n"
    "\t\t{\n"
    "\t\t\tfloat along = (float(taken) + step_noise) / float(steps);\n"
    "\t\t\tivec2 offset = ivec2(round(omega * max(along * along * screen_radius, float(taken) + 1.0)));\n"
    "\t\t\tvec3 to0 = position_at(texel + offset) - centre;\n"
    "\t\t\tvec3 to1 = position_at(texel - offset) - centre;\n"
    "\t\t\tfloat length0 = max(length(to0), 1e-5), length1 = max(length(to1), 1e-5);\n"
    "\t\t\tfloat weight0 = clamp(length0 * falloff_multiply + falloff_add, 0.0, 1.0);\n"
    "\t\t\tfloat weight1 = clamp(length1 * falloff_multiply + falloff_add, 0.0, 1.0);\n"
    "\t\t\t/* (the level's pixels occluded by objects alone) */\n"
    "\t\t\tif (object < 0.5)\n"
    "\t\t\t{\n"
    "\t\t\t\tweight0 *= step(0.5, texelFetch(mask_texture, full_texel(texel + offset), 0).r);\n"
    "\t\t\t\tweight1 *= step(0.5, texelFetch(mask_texture, full_texel(texel - offset), 0).r);\n"
    "\t\t\t}\n"
    "\t\t\t/* (its height over the plane made the slice's angle: exact for a\n"
    "\t\t\tsample in the slice, whose normal there is the projected one) */\n"
    "\t\t\trise0 = max(rise0, weight0 * asin(clamp(dot(to0, normal) / (length0 * projected_length), 0.0, 1.0)));\n"
    "\t\t\trise1 = max(rise1, weight1 * asin(clamp(dot(to1, normal) / (length1 * projected_length), 0.0, 1.0)));\n"
    "\t\t}\n"
    "\t\tfloat h0 = n - 0.5 * PI + rise1, h1 = n + 0.5 * PI - rise0;\n"
    "\t\tfloat sin_n = sin(n);\n"
    "\t\tvisibility += projected_length * 0.25 * (2.0 * cos_n + 2.0 * (h0 + h1) * sin_n -\n"
    "\t\t\tcos(2.0 * h0 - n) - cos(2.0 * h1 - n));\n"
    "\t\t/* (the same with nothing about it: both horizons the surface's) */\n"
    "\t\tunoccluded += projected_length * (cos_n + n * sin_n);\n"
    "\t}\n"
    "\t/* (as a share of what the same slices see with nothing about the pixel,\n"
    "\tso that an open surface is 1 at any angle to the view, whatever its\n"
    "\tslices' turn) */\n"
    "\tresult = vec4(clamp(visibility / max(unoccluded, 1e-5), 0.0, 1.0), centre.z, object, 1.0);\n"
    "}\n"
)
# a blur along one direction (sampling.xy), nine taps, each weighted by how
# near its distance is to the pixel's and none across an object's edge, so
# that no edge is crossed; nearly flat, so that the Bayer matrix's pattern
# is averaged away
OCCLUSION_BLUR_SOURCE = (
    "uniform sampler2D color_texture;\n"
    + OCCLUSION_UNIFORMS +
    "\n"
    "void main()\n"
    "{\n"
    "\tivec2 texel = ivec2(gl_FragCoord.xy);\n"
    "\tvec3 centre = texelFetch(color_texture, texel, 0).rgb;\n"
    "\tfloat sum = centre.r, weights = 1.0;\n"
    "\tfor (int tap = -4; tap <= 4; tap++)\n"
    "\t{\n"
    "\t\tif (tap == 0)\n"
    "\t\t\tcontinue;\n"
    "\t\tivec2 at = clamp(texel + ivec2(sampling.xy) * tap, ivec2(bounds.xy), ivec2(bounds.zw) - 1);\n"
    "\t\tvec3 other = texelFetch(color_texture, at, 0).rgb;\n"
    "\t\tfloat weight = exp(-float(tap * tap) / 32.0 - abs(other.g - centre.g) / (0.02 * centre.g + 0.001)) *\n"
    "\t\t\t(abs(other.b - centre.b) < 0.5 ? 1.0 : 0.0);\n"
    "\t\tsum += other.r * weight;\n"
    "\t\tweights += weight;\n"
    "\t}\n"
    "\tresult = vec4(sum / weights, centre.g, centre.b, 1.0);\n"
    "}\n"
)
# the occlusion at the target's pixel, by its strength and power, filtered
# up from half the resolution by the four nearest, each weighted by how near
# its distance is to the pixel's (and none of the other kind of pixel):
# multiplied over the light by the pipeline's blend, or drawn alone
# (debug.ambient_occlusion_view); none where the mask leaves the pixel
OCCLUSION_APPLY_SOURCE = (
    "uniform sampler2D color_texture;\n"
    "uniform sampler2D mask_texture;\n"
    "uniform sampler2D distance_texture;\n"
    + OCCLUSION_UNIFORMS +
    "\n"
    "void main()\n"
    "{\n"
    "\tivec2 pixel = ivec2(gl_FragCoord.xy);\n"
    "\tvec2 mask = texelFetch(mask_texture, pixel, 0).rg;\n"
    "\tif (mask.g > 0.5)\n"
    "\t\tdiscard;\n"
    "\tvec2 parts = texelFetch(distance_texture, pixel, 0).rg;\n"
    "\tfloat z = parts.r + parts.g, object = mask.r > 0.5 ? 1.0 : 0.0;\n"
    "\tvec2 at = gl_FragCoord.xy / sampling.w - 0.5;\n"
    "\tivec2 base = ivec2(floor(at));\n"
    "\tvec2 fraction = at - vec2(base);\n"
    "\tfloat sum = 0.0, weights = 0.0;\n"
    "\tfor (int corner = 0; corner < 4; corner++)\n"
    "\t{\n"
    "\t\tivec2 offset = ivec2(corner & 1, corner >> 1);\n"
    "\t\tvec3 near = texelFetch(color_texture, clamp(base + offset, ivec2(bounds.xy), ivec2(bounds.zw) - 1), 0).rgb;\n"
    "\t\tvec2 bilinear = mix(1.0 - fraction, fraction, vec2(offset));\n"
    "\t\tfloat weight = bilinear.x * bilinear.y * exp(-abs(near.g - z) / (0.02 * z + 0.001)) *\n"
    "\t\t\t(abs(near.b - object) < 0.5 ? 1.0 : 0.001) + 1e-6;\n"
    "\t\tsum += near.r * weight;\n"
    "\t\tweights += weight;\n"
    "\t}\n"
    "\tresult = vec4(vec3(mix(1.0, pow(sum / weights, sampling.z), occlusion.w)), 1.0);\n"
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
    programs["occlusion_depth"] = ("frag", OCCLUSION_DEPTH_SOURCE)
    programs["occlusion"] = ("frag", OCCLUSION_SOURCE)
    programs["occlusion_blur"] = ("frag", OCCLUSION_BLUR_SOURCE)
    programs["occlusion_apply"] = ("frag", OCCLUSION_APPLY_SOURCE)
    programs["gamma"] = ("frag", GAMMA_SOURCE)
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
