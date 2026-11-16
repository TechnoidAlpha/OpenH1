"""Menu texture lookup preserves replacement and forget semantics at capacity; art decoded ahead on threads is
drawn from what they decoded, once each."""
from pathlib import Path
import subprocess

from test_load_trace import build

ROOT = Path(__file__).resolve().parents[1]


def test_menu_art_registration_replacement_and_forget(tmp_path):
    text = (ROOT / "port/linux/src/menu_files.c").read_text()
    code = text[text.index("/* ---------- art"):]
    source = r'''
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define MAXIMUM_ART 1024
static unsigned int created, deleted, decoded;
static void platform_log(const char *format, ...) { (void)format; }
static void xgpu_image_delete(unsigned int image) { assert(image); deleted++; }
static void xgpu_texture_replacements_changed(void) {}
static const unsigned char *file_data(const char *name, unsigned long *size)
{
    *size = 1;
    return !strcmp(name, "missing") ? NULL : (const unsigned char *)"x";
}
static unsigned char *hud_hires_png_pixels(const void *png, unsigned long size,
    unsigned long *width, unsigned long *height)
{
    assert(png && size == 1);
    __atomic_add_fetch(&decoded, 1, __ATOMIC_SEQ_CST);
    *width = *height = 1;
    return malloc(4);
}
static unsigned int hud_hires_pixels_texture(unsigned char *pixels, unsigned long width,
    unsigned long height, unsigned long *levels)
{
    assert(pixels && width == 1 && height == 1);
    free(pixels);
    *levels = 3;
    return ++created;
}
''' + code + r'''
int main(void)
{
    unsigned long texture[2] = {0, 0}, levels = 0, i;
    unsigned int original;
    for (i = 0; i < MAXIMUM_ART; i++) {
        texture[1] = i * 4096;
        halo_menus_art_register(texture, "image");
    }
    for (i = 0; i < MAXIMUM_ART; i++) {
        assert(menu_art_texture(i * 4096, &levels) != 0 && levels == 3);
        assert(menu_art_texture(i * 4096 + 1, &levels) == 0);
    }
    assert(created == MAXIMUM_ART);
    texture[1] = MAXIMUM_ART * 4096;
    halo_menus_art_register(texture, "image");
    assert(menu_art_texture(texture[1], &levels) == 0);
    texture[1] = 0;
    original = menu_art_texture(0, &levels);
    halo_menus_art_register(texture, "image");
    assert(menu_art_texture(0, &levels) == original && !deleted);
    halo_menus_art_register(texture, "replacement");
    assert(deleted == 1 && menu_art_texture(0, &levels) != original);
    halo_menus_art_register(texture, "missing");
    assert(menu_art_texture(0, &levels) == 0);
    halo_menus_art_forget();
    assert(deleted == created);
    for (i = 0; i < MAXIMUM_ART; i++) assert(menu_art_texture(i * 4096, &levels) == 0);
    halo_menus_art_register(texture, "image");
    assert(menu_art_texture(0, &levels));
    halo_menus_art_forget();
    assert(deleted == created);
    /* decoded ahead: each drawn from its thread's pixels, never decoded
    again; and forgotten while decoding, nothing kept */
    decoded = 0;
    for (i = 0; i < 64; i++) {
        texture[1] = i * 4096;
        halo_menus_art_register(texture, "image");
        halo_menus_art_ahead(texture);
        halo_menus_art_ahead(texture);
    }
    for (i = 0; i < 64; i++)
        assert(menu_art_texture(i * 4096, &levels) != 0 && levels == 3);
    assert(decoded == 64);
    for (i = 0; i < 64; i++) {
        texture[1] = (i + 64) * 4096;
        halo_menus_art_register(texture, "image");
        halo_menus_art_ahead(texture);
    }
    halo_menus_art_forget();
    assert(deleted == created);
    return 0;
}
'''
    subprocess.run([str(build(tmp_path, source))], check=True, timeout=15)
