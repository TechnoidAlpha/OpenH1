"""The loading screen's caption (text_hires_line, port/linux/src/text_hires.c)
set in the menus' heading font, OpenCE, and drawn under the words
(loading_art_caption_draw): its letters are all there (a font whose ascent is
smaller than its capitals once lost every letter but the dots), and drawn
where they belong at the screen's rows."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "port/assets/fonts/OpenCE-Regular.ttf"


def function(text, start):
    """a C function's text, from its signature to its closing brace"""
    begin = text.index(start)
    return text[begin:text.index("\n}\n", begin) + 3]


def test_caption_letters_are_set_and_drawn(tmp_path):
    source = (ROOT / "port/linux/src/text_hires.c").read_text()
    program = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#include "loading_art.h"
struct text_hires_embedded { const char *tag; const char *file; const unsigned int *data; unsigned int size; };
static struct text_hires_embedded text_hires_embedded[1];
static const unsigned int text_hires_embedded_count = 1;
''' + function(source, "static int names_differ(") + function(source, "unsigned char *text_hires_line(") + r'''
int main(int argc, char **argv) {
    FILE *file = fopen(argv[1], "rb");
    static unsigned int font[1 << 20];
    static struct loading_art art;
    static uint8_t frame[1916 * 1080 * 4];
    int width, height, x, y, inked_columns = 0, top = 1 << 30, bottom = -1;
    long sum = 0;
    unsigned char *line;
    unsigned int drawn = 0, i;
    assert(file && fread(font, 1, sizeof(font), file) > 1000); fclose(file);
    text_hires_embedded[0].tag = "pc\\heading"; text_hires_embedded[0].data = font;
    assert(!text_hires_line("no such font", "x", 40.0f, &width, &height));
    line = text_hires_line("pc\\heading", "Preparing menu pictures \xc2\xb7 4 of 11", 40.0f, &width, &height);
    assert(line && width > 600 && height >= 40);
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
    assert(bottom - top >= 38);
    assert(sum > (long)width * height * 255 / 10);
    /* drawn under the words at 1916x1080: 4 lines a pixel of it, 2.25
    pixels a line, so a pixel of the frame is 0.5625 of its */
    loading_art_caption(&art, line, (unsigned int)width, (unsigned int)height);
    memset(frame, 0, sizeof(frame));
    loading_art_caption_draw(&art, frame, 1916, 1080, 256);
    for (i = 0; i < 1916u * 1080u; i++)
        if (frame[i * 4 + 2]) {
            unsigned int row = i / 1916;
            assert(row >= LOADING_ART_CAPTION_TOP * 1080 / 480 && row < LOADING_ART_CAPTION_TOP * 1080 / 480 + 50);
            drawn++;
        }
    assert(drawn > (unsigned int)(sum / 255) / 4);
    return 0;
}
'''
    path = tmp_path / "caption.c"
    path.write_text(program)
    binary = tmp_path / "caption"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu99", "-O1", "-Wall", "-Werror", "-Wno-unused-function",
                    "-pthread", "-I", str(ROOT / "port/linux/src"), str(path), "-o", str(binary), "-lm"],
                   check=True, timeout=120)
    subprocess.run([str(binary), str(FONT)], check=True, timeout=60)
