"""The passes over a window's 3D view keep a multisampled back buffer's
samples (d3d8_device.c): the backends change a target's samples only
between frames, its attachment made again, empty. A pass that took the back
buffer out of multisampling had the HUD's draws make it again mid-frame, and
the frame's resolve put the empty samples and the HUD over the 3D view: with
multisampling and display.panini, a black view under the HUD.

The front end's passes are compiled on their own over stubs of the backend
and played over a multisampled frame: none may change the target's samples,
and each still records its pass.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parent.parent

FUNCTIONS = (
    "static long scaled_pixel(",
    "static void render_target_resolve(",
    "static void render_target_multisample(",
    "void halo_screen_anti_alias(",
    "static void view_corners(",
    "void halo_screen_panini(",
)


def function_text(text, signature):
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
typedef unsigned long xgpu_target;
/* (a map's preparation's journal: not recorded here) */
enum { JOURNAL_ANTI_ALIAS = 5, JOURNAL_PANINI };
static BOOL journal_recording;
/* (no extra view drawn: d3d8_device.c's halo_view_begin) */
static int screen_view;
static void journal_operation(unsigned long op, const void *arguments, unsigned long bytes)
{ (void)op; (void)arguments; (void)bytes; }
struct xgpu_render_target { xgpu_target number; int samples; float scale[2]; };
struct render_target_entry { struct xgpu_render_target target; };
static struct { BOOL gl_ready; int back_buffer; void *depth_stencil; } device;
/* (the targets bound: the back buffer, as it is) */
static BOOL bind_targets(BOOL *has_depth) { *has_depth = TRUE; return TRUE; }
enum { _anti_aliasing_off, _anti_aliasing_fxaa, _anti_aliasing_smaa, _anti_aliasing_msaa };
static int mode = _anti_aliasing_msaa;
static int anti_aliasing(void) { return mode; }
static struct render_target_entry back_buffer;
static struct render_target_entry *render_target_get(void *surface) { (void)surface; return &back_buffer; }
static int sample_changes, resolves, passes;
static void xgpu_backend_target_multisample(xgpu_target target, int samples)
{
	fprintf(stderr, "target %lu made again with %d samples a pixel mid-frame\n", target, samples);
	sample_changes++;
}
static void xgpu_backend_target_resolve(xgpu_target target) { (void)target; resolves++; }
static void xgpu_backend_anti_alias(xgpu_target target, BOOL smaa, const long corners[4])
{ (void)target; (void)smaa; (void)corners; passes++; }
static void xgpu_backend_panini(xgpu_target target, const long corners[4], const float projection[8])
{ (void)target; (void)projection; assert(corners[2] == 640 && corners[3] == 480); passes++; }
%FUNCTIONS%
int main(void)
{
	static const float projection[8] = { 0.5f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.75f, -0.75f };
	static const int counts[] = { 2, 4, 8 };
	int index;

	device.gl_ready = TRUE;
	device.depth_stencil = &device;
	back_buffer.target.number = 1;
	back_buffer.target.scale[0] = back_buffer.target.scale[1] = 1.0f;
	for (index = 0; index < 3; index++)
	{
		/* (the back buffer as the 3D view left it, multisampled) */
		back_buffer.target.samples = counts[index];
		passes = 0;
		halo_screen_anti_alias(0, 0, 640, 480);
		halo_screen_panini(0, 0, 640, 480, projection);
		/* (multisampling has no anti-aliasing pass: Panini's alone) */
		assert(passes == 1);
		assert(back_buffer.target.samples == counts[index]);
	}
	(void)resolves;
	return sample_changes ? 1 : 0;
}
"""


def test_screen_passes_keep_samples(tmp_path):
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("needs a native C compiler")
    text = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    functions = "\n".join(function_text(text, signature) for signature in FUNCTIONS)
    source = tmp_path / "screen_passes.c"
    source.write_text(HARNESS.replace("%FUNCTIONS%", functions))
    binary = tmp_path / "screen_passes"
    subprocess.run([cc, "-std=gnu11", "-Wall", "-Werror", "-Wno-unused-function", str(source), "-o", str(binary)],
                   check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
