"""display.ambient_occlusion "world" in a map's preparation (#672): no window is given then, yet the draws' keys are
the ones play gives them (d3d8_device.c's world_ao_key), so that no world pipeline is made in play: the window the
pre-pass finds once a pixel (its span WORLD_AO_SPAN_FOUND) has the world's light read from the pre-pass, and its
draws' twins made with them (world_ao_twins, XGPU_WORLD_AO_PREPASS_TWINS); a mirror's window (plain
WORLD_AO_SPAN_OPEN) the world's light alone and no twins. The front end's own functions, compiled with the device's
state as stubs; with a negative control each."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parent.parent
DEVICE = "port/linux/src/d3d8_device.c"
FUNCTIONS = ["static BOOL world_ao_applies(void)", "static BOOL world_ao_preparing(void)",
             "static BOOL world_ao_span_found(void)", "static BOOL world_ao_found(void)", "static unsigned char world_ao_key(int light)",
             "static void world_ao_twins(void)"]

HARNESS = r"""
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#include "world_ao.h"
#define XGPU_AMBIENT_OCCLUSION_LIGHT_NONE 0
#define XGPU_AMBIENT_OCCLUSION_WORLD 0x10
#define XGPU_AMBIENT_OCCLUSION_WORLD_VIEW 0x20
#define XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED 0x40
#define HALO_HDR_KIND_SELF_ILLUMINATION 64
enum { XGPU_WORLD_AO_PREPASS_NONE, XGPU_WORLD_AO_PREPASS_BEGIN, XGPU_WORLD_AO_PREPASS_END,
    XGPU_WORLD_AO_PREPASS_TWINS };
static struct { BOOL active, view; int owner; float reference; BOOL failed, prepass, sampled; float scale; int span;
    BOOL preparing, twins; } world_ao;
static struct { int kind; } hdr;
static int setting_world, inline_setting, phase = -1, phases;
int world_ao_mode(void) { return setting_world ? 3 : 0; }
static int config_boolean(const char *name) { return !strcmp(name, "debug.world_ao_inline") ? inline_setting : 0; }
static void xgpu_backend_world_ao_prepass(int value, int target, const void *pass)
{
    (void)target; (void)pass;
    phase = value;
    phases++;
}
@FUNCTIONS@
static int failures;
#define CHECK(c, ...) do { if (!(c)) { printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)
int main(void)
{
    unsigned key;

    world_ao.preparing = TRUE;
    setting_world = 1;
    /* the primary view, found once a pixel: the world's light read from the pre-pass, and the twins */
    world_ao.span = WORLD_AO_SPAN_FOUND;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == (XGPU_AMBIENT_OCCLUSION_WORLD | XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED), "found: %#x", key);
    world_ao_twins();
    CHECK(phase == XGPU_WORLD_AO_PREPASS_TWINS && phases == 1, "found: twins %d (%d)", phase, phases);
    world_ao_twins();
    CHECK(phases == 1, "the twins told again: %d", phases);
    /* its first-person weapon: no light of the world's, its twin all the same (an occluder in the pre-pass) */
    world_ao.span = WORLD_AO_SPAN_FOUND_FIRST_PERSON;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == 0, "found's first person: %#x", key);
    world_ao_twins();
    CHECK(phase == XGPU_WORLD_AO_PREPASS_TWINS, "found's first person: twins %d", phase);
    /* a mirror's window: the world's light, not read from a pre-pass, and no twins */
    world_ao.span = WORLD_AO_SPAN_OPEN;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == XGPU_AMBIENT_OCCLUSION_WORLD, "mirror: %#x", key);
    world_ao_twins();
    CHECK(phase == XGPU_WORLD_AO_PREPASS_NONE && phases == 2, "mirror: twins %d (%d)", phase, phases);
    /* the setting off: nothing of the world's, found or not */
    setting_world = 0;
    world_ao.span = WORLD_AO_SPAN_FOUND;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == 0, "off: %#x", key);
    world_ao_twins();
    CHECK(phase == XGPU_WORLD_AO_PREPASS_NONE && phases == 2, "off: twins %d (%d)", phase, phases);
    /* found inline (debug.world_ao_inline): the world's light found by each surface, no twins */
    setting_world = 1;
    inline_setting = 1;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == XGPU_AMBIENT_OCCLUSION_WORLD, "inline: %#x", key);
    world_ao_twins();
    CHECK(phase == XGPU_WORLD_AO_PREPASS_NONE, "inline: twins %d", phase);
    inline_setting = 0;
    /* in play, outside a given window: none */
    world_ao.preparing = FALSE;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == 0, "play without a window: %#x", key);
    world_ao_twins();
    CHECK(phase == XGPU_WORLD_AO_PREPASS_NONE, "play: twins %d", phase);
    /* in play, a window given and its pre-pass begun: as before */
    world_ao.active = world_ao.sampled = TRUE;
    key = world_ao_key(XGPU_AMBIENT_OCCLUSION_LIGHT_NONE);
    CHECK(key == (XGPU_AMBIENT_OCCLUSION_WORLD | XGPU_AMBIENT_OCCLUSION_WORLD_SAMPLED), "play: %#x", key);
    return failures ? 3 : 0;
}
"""

CONTROLS = [
    # the world's keys made while preparing only for a given window (the pipelines then missed in play)
    ("\telse if (world_ao_preparing())\n", "\telse if (0 && world_ao_preparing())\n"),
    # every window prepared as found once a pixel (a mirror's world pipelines then missed)
    ("\treturn world_ao.span == WORLD_AO_SPAN_FOUND || world_ao.span == WORLD_AO_SPAN_FOUND_FIRST_PERSON;\n",
     "\treturn world_ao.span != WORLD_AO_SPAN_NONE || world_ao.span == WORLD_AO_SPAN_FOUND_FIRST_PERSON;\n"),
    # the found window's draws not lit by the world (its span read as a plain one's)
    ("\treturn (world_ao.span == WORLD_AO_SPAN_OPEN || world_ao.span == WORLD_AO_SPAN_FOUND) &&\n",
     "\treturn (world_ao.span == WORLD_AO_SPAN_OPEN) &&\n"),
    # the twins whatever the setting
    ("\treturn world_ao.preparing && !world_ao.failed && world_ao_mode() != 0;",
     "\treturn world_ao.preparing && !world_ao.failed;"),
]


def function(text, signature):
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def run(tmp_path, fault=None):
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("needs a native C compiler")
    text = (ROOT / DEVICE).read_text()
    if fault:
        assert text.count(fault[0]) == 1, fault[0]
        text = text.replace(*fault)
    source = tmp_path / "world_ao_preparation.c"
    source.write_text(HARNESS.replace("@FUNCTIONS@", "\n".join(function(text, name) for name in FUNCTIONS)))
    # (world_ao.h alone: its folder's own stdio.h is the game's)
    shutil.copy(ROOT / "port/linux/include/world_ao.h", tmp_path / "world_ao.h")
    binary = tmp_path / "world_ao_preparation"
    subprocess.run([cc, "-std=c11", "-Wall", "-Werror", "-Wno-unused-function", f"-I{tmp_path}",
                    str(source), "-o", str(binary)], check=True)
    return subprocess.run([str(binary)], capture_output=True, text=True)


def test_preparation_keys_and_twins(tmp_path):
    result = run(tmp_path)
    assert result.returncode == 0, result.stdout


@pytest.mark.parametrize("fault", CONTROLS)
def test_negative_control(tmp_path, fault):
    result = run(tmp_path, fault)
    assert result.returncode == 3, result.stdout
