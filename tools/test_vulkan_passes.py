"""The Vulkan core's pass/load actions and layered image views over a fake Vulkan,
and what a draw costs the render thread: the texture stages looked up again,
and the guest's memory found mapped without a system call.

Compiles the actual core; no GPU, display, game data or Vulkan loader is
needed. Vulkan headers come from configure's downloaded host dependencies
or the system installation.
"""

import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zlib

import pytest

ROOT = Path(__file__).resolve().parent.parent


def test_vulkan_passes(tmp_path):
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc or not sys.platform.startswith("linux"):
        pytest.skip("needs a C compiler on Linux")
    candidates = [ROOT / f"build/{name}/third_party/vulkan-headers" for name in ("linux_x86_64", "linux_arm64")]
    headers = next((path for path in candidates + [Path("/usr/include")]
                    if (path / "vulkan/vulkan_core.h").exists()), None)
    if headers is None:
        pytest.skip("needs Vulkan headers (configure.py fetches them for the Linux build)")
    binary = tmp_path / "vulkan_passes"
    subprocess.run([
        cc, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        f"-I{headers}", f"-I{ROOT / 'port/android/host'}", f"-I{ROOT / 'port/android/include'}",
        f"-I{ROOT / 'port/linux/src'}", str(ROOT / "port/linux/vulkan/tests/passes_test.c"), "-o", str(binary), "-ldl", "-lm", "-pthread",
    ], check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr


def test_pacing_presents_before_the_display_answers(tmp_path):
    # display.low_latency over a present wait that says nothing until a
    # frame is shown, or nothing ever (the Steam Deck's gamescope, where
    # build 940 showed no frame): the first frames go unpaced, no present
    # waits, a silent display is given up on, an answering one paces the
    # frames from the present after its first answer
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc or not sys.platform.startswith("linux"):
        pytest.skip("needs a C compiler on Linux")
    candidates = [ROOT / f"build/{name}/third_party/vulkan-headers" for name in ("linux_x86_64", "linux_arm64")]
    headers = next((path for path in candidates + [Path("/usr/include")]
                    if (path / "vulkan/vulkan_core.h").exists()), None)
    if headers is None:
        pytest.skip("needs Vulkan headers (configure.py fetches them for the Linux build)")
    binary = tmp_path / "pacing"
    subprocess.run([
        cc, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
        f"-I{headers}", f"-I{ROOT / 'port/android/host'}", f"-I{ROOT / 'port/android/include'}",
        f"-I{ROOT / 'port/linux/src'}", str(ROOT / "port/linux/vulkan/tests/pacing_test.c"), "-o", str(binary),
        "-pthread",
    ], check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr


def test_vr_required_capabilities(tmp_path):
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("needs a native C compiler")
    # Compile the compositor's actual gate; it must reject an optional GPU
    # feature that the sky needs instead of silently changing its pixels.
    text = (ROOT / "port/linux/vulkan/host_vulkan_vr.c").read_text()
    function = text[text.index("static BOOL supported(void)"):
                    text.index("static BOOL sample_counting(void)")]
    source = tmp_path / "vr_capabilities.c"
    source.write_text(r"""
#include <assert.h>
#include <stdarg.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define HOST_LOG_WARN 1
struct xgpu_vulkan_capabilities { unsigned int multiview_views; int depth_clamp; };
static struct xgpu_vulkan_capabilities available;
static unsigned int warnings;
static void xgpu_vulkan_capabilities(struct xgpu_vulkan_capabilities *capabilities)
{
    *capabilities = available;
}
static void host_logf(int priority, const char *format, ...)
{
    assert(priority == HOST_LOG_WARN && format);
    warnings++;
}
""" + function + r"""
int main(void)
{
    available.multiview_views = 2;
    available.depth_clamp = 1;
    assert(supported() && !warnings);
    available.multiview_views = 4;
    assert(supported() && !warnings);
    available.depth_clamp = 0;
    assert(!supported() && warnings == 1);
    available.depth_clamp = 1;
    available.multiview_views = 1;
    assert(!supported() && warnings == 2);
    available.multiview_views = 0;
    assert(!supported() && warnings == 3);
    return 0;
}
""")
    binary = tmp_path / "vr_capabilities"
    subprocess.run([cc, "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_post_shaders(tmp_path):
    # The anti-aliasing passes' generated file (tools/vulkan_post_shaders.py),
    # with display.hdr's tone map and encoding, display.gamma's curve and
    # display.panini's projection: SMAA's lookup textures are
    # port/third_party/smaa's, inflated, and each module is valid for Vulkan
    # 1.1 where spirv-val is installed.
    text = (ROOT / "port/linux/vulkan/xgpu_vulkan_post_shaders.c").read_text()
    arrays = {name: [int(word, 16) for word in re.findall(r"0x([0-9a-f]{8})", body)]
              for name, body in re.findall(r"const uint32_t (\w+)\[\] =\n\{\n(.*?)\n\};", text, re.S)}
    for name, file, size in (("area", "area_tex.zlib", 160 * 560 * 2), ("search", "search_tex.zlib", 64 * 16)):
        texels = zlib.decompress((ROOT / "port/third_party/smaa" / file).read_bytes())
        words = arrays[f"xgpu_vulkan_post_{name}_texture"]
        assert len(texels) == size
        assert struct.pack(f"<{len(words)}I", *words)[:size] == texels
    modules = sorted(name for name in arrays if name.endswith(("_vert", "_frag")))
    assert modules == ["clear_frag", "fxaa_frag", "gamma_frag", "hdr_encode_frag", "hdr_tone_map_frag", "hdr_tone_map_ms_frag",
                       "panini_frag", "post_vert", "rotate_frag", "smaa_blend_frag", "smaa_edges_frag",
                       "smaa_weights_frag"]
    validator = shutil.which("spirv-val")
    if not validator:
        pytest.skip("needs spirv-val (SPIRV-Tools)")
    for name in modules:
        assert arrays[name][0] == 0x07230203
        module = tmp_path / f"{name}.spv"
        module.write_bytes(struct.pack(f"<{len(arrays[name])}I", *arrays[name]))
        subprocess.run([validator, "--target-env", "vulkan1.1", str(module)], check=True)


def _native_cc():
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("needs a native C compiler")
    return cc


def test_texture_memory_in_the_guests_ranges_is_not_asked_of_the_kernel(tmp_path):
    # Without the base register (the Steam Frame's guest) readable() asked
    # mincore for each stage's header and palette at each bind. A range in
    # the memory the guest's heaps come from (host_low_owns: mapped whole,
    # reserved where unused) is readable without a system call; any other
    # range is still asked about, page by page as before.
    text = (ROOT / "port/linux/vulkan/xgpu_vulkan_textures.c").read_text()
    function = text[text.index("static BOOL readable(uint32_t address, uint32_t size)"):
                    text.index("static struct xgpu_vulkan_image *xbox_texture_get(")]
    source = tmp_path / "readable.c"
    source.write_text(r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define CONTIGUOUS_BASE 0x80000000ull
#define CONTIGUOUS_SIZE 0x08000000ull
#define POOL_BASE 0x20000000ull
#define POOL_SIZE 0x10000000ull
static unsigned asked, owned_asked;
static int mapped = 1;
static uint64_t host_page_size(void) { return 4096; }
static uintptr_t host_address(uint32_t address) { return address; }
static void *host_pointer(uint32_t address) { return (void *)(uintptr_t)address; }
static int host_low_owns(uintptr_t address, size_t size)
{
    owned_asked++;
    return address >= POOL_BASE && address + size <= POOL_BASE + POOL_SIZE;
}
static int mincore(void *address, size_t length, unsigned char *residency)
{
    (void)address; (void)residency;
    assert(length && length <= 4096 * 64);
    asked++;
    return mapped ? 0 : -1;
}
""" + function + r"""
int main(void)
{
    /* the window, as before: no question at all */
    assert(readable(0x80001000u, 20) && !asked && !owned_asked);
    /* a header in a pool: no system call */
    assert(readable(0x20000010u, 20) && !asked && owned_asked == 1);
    assert(readable(0x2ffffc00u, 1024) && !asked);
    /* across the pool's end, or outside it: mincore, as before */
    assert(readable(0x2ffffc00u, 2048) && asked == 1);
    mapped = 0;
    assert(!readable(0x10000000u, 1024) && asked == 2);
    /* nothing at 0, or past 4 GB */
    assert(!readable(0, 20) && !readable(0xfffffff0u, 64));
    return 0;
}
""")
    binary = tmp_path / "readable"
    subprocess.run([_native_cc(), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def test_only_the_texture_stages_that_changed_are_looked_up_again(tmp_path):
    # The guest sends the four stages when any changes; each stage's image and
    # sampler depend on that stage alone, so only the changed ones are looked
    # up again, and a stage keeps its sampler while its sampler is the same. A
    # watched page written, a sampled target drawn into and each frame's end
    # look at them all again; the targets set (before most draws), the stages
    # of targets and images alone; and a palettized texture's stage whenever
    # the stages are set (its palette is read as it is looked up, and is not
    # watched).
    text = (ROOT / "port/linux/vulkan/xgpu_vulkan_stream.c").read_text()
    set_textures = text[text.index("static int stage_palettized("):
                        text.index("/* a copy of a target the draw draws into")]
    bind = text[text.index("/* the stages' images and samplers for a draw */"):
                text.index("/* ---------- pipelines */")]
    set_targets = text[text.index("static void stream_set_targets("):
                       text.index("static void stream_clear(")]
    source = tmp_path / "textures_bind.c"
    source.write_text(r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "xgpu_stream.h"
typedef uint64_t VkSampler;
#define VK_NULL_HANDLE 0
#define TEXTURES_ALL 15u
#define D3DFORMAT_FORMAT_MASK 0x0000ff00u
#define D3DFORMAT_FORMAT_SHIFT 8
enum { _source_none, _source_texture, _source_target };
enum { _xgpu_sampler_2d, _xgpu_sampler_3d, _xgpu_sampler_cube, _xgpu_sampler_2d_layered };
enum { XGPU_VULKAN_IMAGE_2D, XGPU_VULKAN_IMAGE_2D_ARRAY, XGPU_VULKAN_IMAGE_3D, XGPU_VULKAN_IMAGE_CUBE };
struct host_render_texture_stage
{
    struct xgpu_stream_texture_stage stage;
    struct xgpu_stream_texture_level levels[XGPU_STREAM_MIP_LEVELS];
};
struct xgpu_vulkan_image_description { uint8_t type; };
struct xgpu_vulkan_image { struct xgpu_vulkan_image_description description; };
struct xgpu_vulkan_texture { struct xgpu_vulkan_image *image; VkSampler sampler; uint8_t kind; };
struct stream_target { struct xgpu_vulkan_image *image; };
static struct
{
    struct host_render_texture_stage stages[4];
    uint8_t border[4];
    unsigned int textures_dirty;
    int feedback_bound, uniforms_dirty;
    uint32_t textures_serial, color, depth, samples, views, base;
    int hud_group, preparing;
    struct xgpu_vulkan_texture textures[4];
    struct xgpu_stream_sampler sampler_from[4];
    uint8_t sampler_axes[4], sampler_valid[4];
    VkSampler sampler_made[4];
} stream;
static struct xgpu_vulkan_image images[4];
static unsigned looked_up[4], samplers, binds;
static uint32_t serial = 1;
static uint32_t host_memory_watch_serial(void) { return serial; }
static uint8_t border_axes(const struct xgpu_stream_sampler *sampler) { (void)sampler; return 0; }
static VkSampler sampler_get(const struct xgpu_stream_sampler *from, uint8_t axes)
{ (void)axes; samplers++; return 1 + from->minify; }
static struct stream_target *target_get(uint32_t number) { (void)number; return NULL; }
static void target_resolve(struct stream_target *target) { (void)target; }
static struct xgpu_vulkan_image *composite_get(const struct host_render_texture_stage *source)
{ (void)source; return NULL; }
static struct xgpu_vulkan_image *feedback_copy(uint32_t number, struct stream_target *target)
{ (void)number; (void)target; return NULL; }
static struct xgpu_vulkan_image *xgpu_vulkan_textures_get(const struct xgpu_stream_texture_stage *stage)
{
    looked_up[stage->resource - 1]++;
    return &images[stage->resource - 1];
}
static const struct xgpu_vulkan_image_description *xgpu_vulkan_image_description(const struct xgpu_vulkan_image *image)
{ return &image->description; }
static void targets_bind(void) { binds++; }
static void prepass_prepare(void) { }
#define XGPU_VULKAN_CLEAR_COLOR 1
#define XGPU_VULKAN_CLEAR_ALPHA 2
static int xgpu_vulkan_clear_prepare(int parts) { (void)parts; return 1; }
""" + set_textures + bind + set_targets + r"""
static void set(struct host_render_texture_stage stages[4])
{
    memset(looked_up, 0, sizeof(looked_up));
    stream_set_textures(NULL, stages);
    textures_bind();
}

int main(void)
{
    struct host_render_texture_stage stages[4];
    int index;

    memset(stages, 0, sizeof(stages));
    for (index = 0; index < 4; index++)
    {
        stages[index].stage.source = _source_texture;
        stages[index].stage.resource = (uint32_t)index + 1;
        stages[index].stage.resource_format = 0x0c00; /* DXT1 */
    }
    set(stages);
    for (index = 0; index < 4; index++)
        assert(looked_up[index] == 1 && stream.textures[index].image == &images[index]);
    assert(samplers == 4);
    /* one stage changed: it alone */
    stages[2].stage.sampler.minify = 2;
    set(stages);
    assert(!looked_up[0] && !looked_up[1] && looked_up[2] == 1 && !looked_up[3]);
    assert(stream.textures[2].sampler == 3 && stream.textures[0].image == &images[0] && samplers == 5);
    /* its texture changed, its sampler not: the sampler kept */
    stages[2].stage.resource = 4;
    set(stages);
    assert(looked_up[3] == 1 && stream.textures[2].image == &images[3] && stream.textures[2].sampler == 3);
    assert(samplers == 5);
    stages[2].stage.resource = 3;
    set(stages);
    assert(looked_up[2] == 1 && samplers == 5);
    /* the same stages again (another stage's levels written, say): none */
    binds = 0;
    set(stages);
    assert(!looked_up[0] && !looked_up[1] && !looked_up[2] && !looked_up[3] && !binds);
    /* a watched page written: all of them, their samplers kept */
    serial++;
    set(stages);
    for (index = 0; index < 4; index++)
        assert(looked_up[index] == 1);
    assert(samplers == 5);
    /* a palettized stage with a palette: whenever the stages are set */
    stages[1].stage.resource_format = 0x0b00;
    stages[1].stage.palette = 0x1000;
    set(stages);
    assert(looked_up[1] == 1 && !looked_up[0]);
    set(stages);
    assert(looked_up[1] == 1 && !looked_up[0] && !looked_up[2] && !looked_up[3]);
    /* the targets set: the stages of textures not (a target's would be) */
    {
        struct xgpu_stream_set_targets targets;

        memset(&targets, 0, sizeof(targets));
        targets.color = 7;
        stream_set_targets(NULL, &targets);
        assert(stream.color == 7 && !stream.textures_dirty);
        stages[3].stage.source = _source_target;
        set(stages);
        stream_set_targets(NULL, &targets);
        assert(stream.textures_dirty == 8u);
        stages[3].stage.source = _source_texture;
        set(stages);
    }
    /* a frame's end: all of them */
    stream.textures_dirty = TEXTURES_ALL;
    memset(looked_up, 0, sizeof(looked_up));
    textures_bind();
    for (index = 0; index < 4; index++)
        assert(looked_up[index] == 1);
    return 0;
}
""")
    binary = tmp_path / "textures_bind"
    subprocess.run([_native_cc(), "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                    f"-I{ROOT / 'port/linux/src'}", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
