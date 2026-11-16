"""The loading screen's caption (text_hires_line, port/linux/src/text_hires.c)
lettered as the words over it, "LOADING...", are: in capitals, in their
face (OpenCE, the menus' titles'), and drawn under them at the screen's own
pixels (loading_art_caption_shown): its letters are all there (a font whose
ascent is smaller than its capitals once lost every letter but the dots),
drawn where they belong, as tall as 15 of the 480 lines are on that screen
(not the loading frame's pixels scaled up: a 4K screen shows a frame of 1080
rows), in the words' colour from the picture, lit by the sweep as the words
are (bright as its band crosses them, its trail's glow about them in the
frame); in VR's window, in its frame."""
from pathlib import Path
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "port/assets/fonts/OpenCE-Regular.ttf"
PICTURE = ROOT / "port/assets/loading/loading.rgba.zlib"


def function(text, start):
    """a C function's text, from its signature to its closing brace"""
    begin = text.index(start)
    return text[begin:text.index("\n}\n", begin) + 3]


def test_caption_letters_are_set_and_drawn(tmp_path):
    source = (ROOT / "port/linux/src/text_hires.c").read_text()
    # (the embedded fonts' list as text_hires.h has it)
    embedded = re.search(r"^struct text_hires_embedded\n\{.*?^\};", (ROOT / "port/linux/src/text_hires.h").read_text(),
                         re.M | re.S).group(0)
    program = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#include "loading_art.h"
''' + embedded + r'''
static struct text_hires_embedded text_hires_embedded[1];
static const unsigned int text_hires_embedded_count = 1;
/* (the assets' file, embedded_assets.h: the font alone, at offset 16) */
static unsigned int font[1 << 20];
static const void *embedded_assets_data(unsigned int offset, unsigned int size)
{ (void)size; return offset == 16 ? font : NULL; }
''' + function(source, "static int names_differ(") + function(source, "unsigned char *text_hires_line(") + r'''
/* (the supplied picture, as the presenter has it: loading_assets.py's) */
static uint8_t picture[LOADING_ART_BYTES];

/* the caption's letters in a shown tile (its coverage past 250): their
mean blue, and how many */
static double letters_blue(struct loading_art *art, const uint8_t *tile, unsigned int width, unsigned int height,
    unsigned int *count) {
    unsigned int i; double blue = 0.0; *count = 0;
    for (i = 0; i < width * height; i++)
        if (art->caption_coverage[i] > 250) { blue += tile[i * 4 + 2]; (*count)++; }
    return *count ? blue / *count : 0.0;
}

/* a load followed at `progress`, rendered `steps` frames from `first` */
static const uint8_t *load(struct loading_art *art, struct loading_presentation_frame *frame, float progress,
    unsigned int first, unsigned int steps) {
    const uint8_t *pixels = NULL; unsigned int step;
    for (step = first; step < first + steps; step++) {
        frame->elapsed_ns = (uint64_t)step * 16666667u;
        pixels = loading_art_render(art, frame);
        assert(pixels);
        /* (told after the load's first frame, as its epoch is known) */
        loading_art_progress(art, progress);
    }
    return pixels;
}

int main(int argc, char **argv) {
    FILE *file = fopen(argv[1], "rb");
    static struct loading_art art, plain, vr, vr_plain;
    static uint8_t compressed[1 << 20], bare_copy[1916 * 1080 * 4];
    struct loading_presentation_frame frame;
    uLongf unpacked = sizeof(picture);
    size_t packed;
    int width, height, x, y, inked_columns = 0, top = 1 << 30, bottom = -1;
    long sum = 0;
    unsigned char *line;
    const uint8_t *tile, *pixels;
    unsigned int tile_left, tile_top, tile_width, tile_height, ink_top = ~0u, ink_bottom = 0, i, lit, dark, glow = 0;
    unsigned int words_peak = 0, caption_peak = 0;
    double blue_lit, blue_dark;
    (void)argc;
    assert(file && fread(font, 1, sizeof(font), file) > 1000); fclose(file);
    file = fopen(argv[2], "rb");
    assert(file && (packed = fread(compressed, 1, sizeof(compressed), file)) > 1000); fclose(file);
    assert(uncompress(picture, &unpacked, compressed, packed) == Z_OK && unpacked == sizeof(picture));
    text_hires_embedded[0].tag = "pc\\heading"; text_hires_embedded[0].data_offset = 16;
    text_hires_embedded[0].size = sizeof(font);
    assert(!text_hires_line("no such font", "x", 80.0f, &width, &height));
    /* (as sdl_platform.c sets it: in capitals, in the words' face, its
    capitals 15 of the 480 lines, at LOADING_PRESENTATION_CAPTION_SCALE
    times their size) */
    line = text_hires_line("pc\\heading", "MENU PICTURES \xb7 4 OF 23",
        15.0f * LOADING_PRESENTATION_CAPTION_SCALE, &width, &height);
    assert(line && width > 600 && width <= 8192 && height >= 120 && height <= 512);
    for (x = 0; x < width; x++) {
        int inked = 0;
        for (y = 0; y < height; y++) {
            sum += line[y * width + x];
            if (line[y * width + x] > 128) { inked = 1; top = y < top ? y : top; bottom = y > bottom ? y : bottom; }
        }
        inked_columns += inked;
    }
    /* (every letter: ink across most of the line, as tall as the capitals) */
    assert(inked_columns > width / 2);
    assert(bottom - top >= 114);
    assert(sum > (long)width * height * 255 / 10);
    /* (a count that changes moves nothing: from 1 of 23 to 23 of 23, the
    line as wide, its first ink at the same column, its words after the
    count the same pixels) */
    {
        int count, count_width, count_height, first = -1, words = -1;
        for (count = 1; count <= 23; count++) {
            char text[64];
            unsigned char *other;
            int left = -1;
            snprintf(text, sizeof(text), "MENU PICTURES \xb7 %d OF 23", count);
            other = text_hires_line("pc\\heading", text, 15.0f * LOADING_PRESENTATION_CAPTION_SCALE,
                &count_width, &count_height);
            assert(other && count_width == width && count_height == height);
            for (x = 0; x < count_width && left < 0; x++)
                for (y = 0; y < count_height; y++)
                    if (other[y * count_width + x] > 128) { left = x; break; }
            if (first < 0) first = left;
            assert(left == first);
            /* ("OF 23": its last eighth) */
            if (words < 0) words = count_width * 7 / 8;
            for (y = 0; y < count_height; y++)
                assert(!memcmp(other + y * count_width + words, line + y * width + words,
                    (size_t)(count_width - words)));
            free(other);
        }
    }

    /* a load on a 3840x2160 screen, the sweep following its progress, with
    and without the caption (its sharp frame 1916x1080, as on one) */
    memset(&frame, 0, sizeof(frame));
    frame.epoch = 1; frame.active = 1;
    loading_art_set(&art, picture); loading_art_set(&plain, picture);
    loading_art_follow(&art, 1); loading_art_follow(&plain, 1);
    loading_art_width(&art, 3840, 2160); loading_art_width(&plain, 3840, 2160);
    loading_art_caption(&art, line, (unsigned int)width, (unsigned int)height);
    /* (the words' colour, from the picture: its blue, down their capitals) */
    loading_art_caption_layer(&art);
    assert(art.word_rows >= 18 && art.word_rows <= 26);
    for (i = art.word_first; i <= art.word_last; i++)
        assert(art.word_rgb[i][2] > 220 && art.word_rgb[i][2] > art.word_rgb[i][1] + 60 &&
            art.word_rgb[i][1] > art.word_rgb[i][0] + 40);
    memcpy(bare_copy, load(&plain, &frame, 0.5f, 0, 300), sizeof(bare_copy));
    pixels = load(&art, &frame, 0.5f, 0, 300);
    assert(loading_art_output_width(&art) == 1916 && loading_art_output_height(&art) == 1080);
    /* (the frame has the caption's glow, the sweep's trail of it: under
    the words only, the words as they were) */
    for (i = 0; i < 1916u * 1080u * 4; i++)
        if (pixels[i] != bare_copy[i]) {
            unsigned int row = i / 4 / 1916;
            assert(row * 480 / 1080 + 6 >= LOADING_ART_CAPTION_TOP);
            glow++;
        }
    assert(glow > 1000);
    /* (at the screen's pixels: 8 lines a pixel of the caption, 4.5 pixels a
    line of the screen) */
    tile = loading_art_caption_shown(&art, pixels, 3840, 2160, &tile_left, &tile_top, &tile_width, &tile_height);
    assert(tile);
    assert(tile_top == LOADING_ART_CAPTION_TOP * 2160 / 480);
    assert(tile_width <= 3840 && tile_left == (3840 - tile_width) / 2);
    assert(tile_width >= (unsigned int)width * 2160 / 3840 && tile_width <= (unsigned int)width * 2160 / 3840 + 1);
    for (i = 0; i < tile_width * tile_height; i++) {
        const uint8_t *p = tile + i * 4;
        unsigned int row = i / tile_width;
        assert(p[3] == 255);
        if (art.caption_coverage[i] > 250) {
            /* (in the words' blues: the picture's, the sweep's light) */
            assert(p[2] >= p[1] && p[1] >= p[0]);
            ink_top = row < ink_top ? row : ink_top;
            ink_bottom = row > ink_bottom ? row : ink_bottom;
        }
    }
    /* (its ink as tall as the line's: its capitals 67 of the screen's rows,
    not the frame's 34 scaled up) */
    assert((int)(ink_bottom - ink_top) * 3840 >= (bottom - top - 4) * 2160 &&
        (int)(ink_bottom - ink_top) * 3840 <= (bottom - top + 4) * 2160);
    /* (lit by the sweep as the words are: bright as its band crosses them,
    dim once it has passed) */
    blue_lit = letters_blue(&art, tile, tile_width, tile_height, &lit);
    /* (as bright as the words where the band is: the brightest of each,
    the words' in the frame, the caption's at the screen's pixels) */
    for (i = 0; i < tile_width * tile_height; i++)
        if (art.caption_coverage[i] > 250 && tile[i * 4 + 2] > caption_peak) caption_peak = tile[i * 4 + 2];
    for (y = 227 * 1080 / 480; y < 249 * 1080 / 480; y++)
        for (x = 0; x < 1916; x++)
            if (pixels[(y * 1916 + x) * 4 + 2] > words_peak) words_peak = pixels[(y * 1916 + x) * 4 + 2];
    fprintf(stderr, "brightest blue: the words' %u, the caption's %u\n", words_peak, caption_peak);
    assert(caption_peak + 24 >= words_peak && caption_peak <= words_peak + 24);
    pixels = load(&art, &frame, 1.0f, 300, 40);
    tile = loading_art_caption_shown(&art, pixels, 3840, 2160, &tile_left, &tile_top, &tile_width, &tile_height);
    blue_dark = letters_blue(&art, tile, tile_width, tile_height, &dark);
    fprintf(stderr, "letters' blue: %.1f lit, %.1f after (%u pixels)\n", blue_lit, blue_dark, lit);
    assert(lit > 1000 && lit == dark);
    assert(blue_lit > 60.0 && blue_lit > blue_dark + 40.0);
    loading_art_caption(&art, NULL, 0, 0);
    pixels = load(&art, &frame, 1.0f, 340, 1);
    assert(!loading_art_caption_shown(&art, pixels, 3840, 2160, &tile_left, &tile_top, &tile_width, &tile_height));

    /* VR's window: the caption in its frame, at its 480 rows, lit by the
    sweep as the words are; none at the screen's pixels */
    loading_art_set(&vr, picture); loading_art_set(&vr_plain, picture);
    loading_art_stereo(&vr, 1); loading_art_stereo(&vr_plain, 1);
    loading_art_follow(&vr, 1); loading_art_follow(&vr_plain, 1);
    loading_art_caption(&vr, line, (unsigned int)width, (unsigned int)height);
    memset(&frame, 0, sizeof(frame)); frame.epoch = 2; frame.active = 1;
    memcpy(bare_copy, load(&vr_plain, &frame, 0.5f, 0, 300), 640u * 480u * 4);
    pixels = load(&vr, &frame, 0.5f, 0, 300);
    assert(!loading_art_caption_shown(&vr, pixels, 1920, 1080, &tile_left, &tile_top, &tile_width, &tile_height));
    for (i = glow = 0; i < 640u * 480u; i++)
        if (pixels[i * 4 + 2] > bare_copy[i * 4 + 2] + 20) {
            unsigned int row = i / 640;
            assert(row + 6 >= LOADING_ART_CAPTION_TOP && row < LOADING_ART_CAPTION_TOP + 40);
            glow++;
        }
    assert(glow > (unsigned int)(sum / 255) / 64 / 4);
    loading_art_hd_free(&art); loading_art_hd_free(&plain);
    return 0;
}
'''
    path = tmp_path / "caption.c"
    path.write_text(program)
    binary = tmp_path / "caption"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu99", "-O1", "-Wall", "-Werror", "-Wno-unused-function",
                    "-pthread", "-I", str(ROOT / "port/linux/src"), str(path), "-o", str(binary), "-lm", "-lz"],
                   check=True, timeout=120)
    subprocess.run([str(binary), str(FONT), str(PICTURE)], check=True, timeout=60)
