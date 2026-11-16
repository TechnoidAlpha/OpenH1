"""Android sizes render targets from drawable pixels, including after resize."""

from pathlib import Path
import shutil
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def test_android_drawable_resolution(tmp_path):
    compiler = shutil.which("clang") or shutil.which("cc")
    if not compiler:
        pytest.skip("needs a C compiler")
    platform = (ROOT / "port/linux/src/sdl_platform.c").read_text()
    platform = platform[platform.index("/* Android renders at the surface's pixel size."):]
    renderer = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    source = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define HALO_ANDROID 1
#define SCREEN_HEIGHT 480
#define SCREEN_MAXIMUM_WIDTH 1920
#define D3DFMT_LIN_A8R8G8B8 1
#define D3DFMT_LIN_D24S8 2
typedef int BOOL;
typedef unsigned int UINT;
static void *platform_window;
static const char *boot_width, *boot_height;
static int drawable_width, drawable_height;
static long screen_width;
static float screen_scale[2], screen_supersampling, screen_supersampling_asked;
static long shadow_scale;
static unsigned long shadow_scale_read_at;
static unsigned int resized, evicted;
static struct { int created, back_buffer, depth_buffer;
    struct { UINT BackBufferWidth; } presentation; } device;
static struct { long largest_target; } xgpu_backend_capabilities = { 16384 };
/* (display.supersampling: none on Android, d3d8_device.c's) */
static float supersampling_wanted(void) { return 1.0f; }
static void screen_mode_log(long width, const float scale[2]) { (void)width; (void)scale; }
/* (display.panini supersamples nothing on Android: d3d8_device.c's) */
static float panini_supersampling(long width, long height) { (void)width; (void)height; return 1.0f; }
static void anti_aliasing_read(void) {}
/* (halo_screen_commit's other settings: none here) */
static void battery_saver_update(void) {}
static void bink_import_update(void) {}
static void hdr_read(void) {}
static void picture_position_send(void) {}
static void gamma_send(void) {}
static unsigned long screen_settings_taken;
static unsigned long config_changes(void) { return 0; }
static long shadow_scale_choose(void) { return 1; }
#define SHADOW_MAP_SIZE 128
static void render_targets_evict(void) { evicted++; }
/* (no extra view's images let go of: d3d8_device.c's halo_view_release) */
#define FALSE 0
static BOOL views_released;
static void platform_log(const char *format, ...) { (void)format; }
static void d3d8_surface_resize(int *surface, int format, unsigned long width, unsigned long height)
{
    assert(surface == &device.back_buffer || surface == &device.depth_buffer);
    assert(format == D3DFMT_LIN_A8R8G8B8 || format == D3DFMT_LIN_D24S8);
    assert(width == (unsigned long)screen_width && height == SCREEN_HEIGHT);
    resized++;
}
static const char *display_environment(const char *name)
{
    if (!strcmp(name, "HALO_DISPLAY_WIDTH")) return boot_width;
    assert(!strcmp(name, "HALO_DISPLAY_HEIGHT"));
    return boot_height;
}
#define getenv display_environment
static void SDL_GetWindowSizeInPixels(void *window, int *width, int *height)
{
    assert(window == platform_window);
    *width = drawable_width;
    *height = drawable_height;
}
static const char *config_string(const char *name) { assert(!strcmp(name, "display.picture_shape")); return "4:3"; }
''' + function(platform, "BOOL platform_screen_mode(") + function(
        renderer, "static long screen_narrowest_width("
    ) + function(renderer, "static void screen_mode_choose(") + function(renderer, "long halo_screen_width(") + function(
        renderer, "long halo_screen_commit("
    ) + r'''
static void check_pixels(long expected_width, long expected_height)
{
    assert((long)(screen_width * screen_scale[0] + 0.5f) == expected_width);
    assert((long)(SCREEN_HEIGHT * screen_scale[1] + 0.5f) == expected_height);
}
int main(void)
{
    long width, height;
    assert(!platform_screen_mode(&width, &height));
    boot_width = "2410";
    boot_height = "1080";
    assert(halo_screen_width() == 1070);
    check_pixels(2410, 1080);
    platform_window = &device;
    drawable_width = 2410;
    drawable_height = 1080;
    device.created = 1;
    halo_screen_commit();
    assert(!resized && !evicted);
    /* The actual window can differ from the display used at boot. */
    drawable_width = 2560;
    drawable_height = 1440;
    halo_screen_commit();
    check_pixels(2560, 1440);
    assert(resized == 2 && evicted == 1);
    assert(device.presentation.BackBufferWidth == (UINT)screen_width);
    /* A surface being recreated must not replace its targets with 0x0. */
    drawable_width = drawable_height = 0;
    halo_screen_commit();
    check_pixels(2560, 1440);
    assert(resized == 2 && evicted == 1);
    drawable_width = 3840;
    drawable_height = 2160;
    halo_screen_commit();
    check_pixels(3840, 2160);
    assert(resized == 4 && evicted == 2);
    return 0;
}
'''
    path = tmp_path / "resolution.c"
    path.write_text(source)
    binary = tmp_path / "resolution"
    subprocess.run([compiler, "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                    str(path), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
