"""Compile the shared compositor itself; no game assets or graphics driver."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(tmp_path, body):
    source = tmp_path / "art.c"
    source.write_text('#include <assert.h>\n#include <stdlib.h>\n'
                      '#include "loading_art.h"\n' + body)
    binary = tmp_path / "art"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu89", "-O2",
                    "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                    "-pthread", "-I", str(ROOT / "port/linux/src"), str(source),
                    "-o", str(binary), "-lm"], check=True, timeout=60)
    subprocess.run([str(binary)], check=True, timeout=30)


def test_owned_image_reset_and_bounded_time_jumps(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art)), *fresh = calloc(1, sizeof(*fresh));
    struct loading_presentation_frame frame;
    uint8_t *input = malloc(LOADING_ART_BYTES), *saved = malloc(LOADING_ART_FRAME_PIXELS * 4);
    const uint8_t *pixels;
    unsigned int i, lit = 0;
    assert(art && fresh && input && saved); memset(&frame, 0, sizeof(frame));
    assert(!loading_art_render(art, &frame)); memset(input, 255, LOADING_ART_BYTES);
    loading_art_set(art, input); loading_art_set(fresh, input); memset(input, 0, LOADING_ART_BYTES);
    frame.epoch = 1; frame.elapsed_ns = UINT64_C(1010000000);
    pixels = loading_art_render(art, &frame);
    for (i = 0; i < LOADING_ART_FRAME_PIXELS; i++) {
        assert(pixels[i*4] <= pixels[i*4+1] && pixels[i*4+1] <= pixels[i*4+2]);
        assert(pixels[i*4+3] == 255); lit += pixels[i*4+2] != 0;
    }
    assert(lit); memcpy(saved, pixels, LOADING_ART_FRAME_PIXELS * 4);
    assert(!memcmp(saved, loading_art_render(art, &frame), LOADING_ART_FRAME_PIXELS * 4));
    frame.epoch = 2;
    assert(!memcmp(loading_art_render(art, &frame), loading_art_render(fresh, &frame), LOADING_ART_FRAME_PIXELS * 4));
    frame.elapsed_ns = UINT64_C(9876543210987654321); loading_art_render(art, &frame);
    assert(art->step == (frame.elapsed_ns / UINT64_C(1000000000))*60 +
        (frame.elapsed_ns % UINT64_C(1000000000))*60 / UINT64_C(1000000000));
    frame.elapsed_ns = UINT64_C(1010000000);
    assert(!memcmp(loading_art_render(art, &frame), saved, LOADING_ART_FRAME_PIXELS * 4));
    loading_art_set(art, NULL); assert(!loading_art_render(art, &frame));
    free(saved); free(input); free(fresh); free(art); return 0;
}
''')


def test_refresh_interpolation_does_not_advance_feedback_twice(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct loading_art *slow = calloc(1, sizeof(*slow)), *fast = calloc(1, sizeof(*fast));
    struct loading_presentation_frame frame;
    uint8_t *input = malloc(LOADING_ART_BYTES), *last = malloc(LOADING_ART_FRAME_PIXELS * 4);
    unsigned int i, changed = 0; uint64_t prior_step;
    assert(slow && fast && input && last); memset(&frame, 0, sizeof(frame));
    memset(input, 255, LOADING_ART_BYTES); loading_art_set(slow, input); loading_art_set(fast, input);
    memset(last, 0, LOADING_ART_FRAME_PIXELS * 4); frame.epoch = 8;
    for (i = 0; i < 490; i++) {
        frame.elapsed_ns = (UINT64_C(1000000000) * i + 119) / 120;
        prior_step = fast->step;
        loading_art_render(fast, &frame);
        if (i % 2 == 0) {
            loading_art_render(slow, &frame);
            assert(!memcmp(slow->output, fast->output, LOADING_ART_FRAME_PIXELS * 4));
            assert(!memcmp(slow->history, fast->history, sizeof(slow->history)));
        } else {
            assert(prior_step == fast->step);
            changed += memcmp(last, fast->output, LOADING_ART_FRAME_PIXELS * 4) != 0;
        }
        memcpy(last, fast->output, LOADING_ART_FRAME_PIXELS * 4);
    }
    assert(changed > 150);
    free(last); free(input); free(fast); free(slow); return 0;
}
''')


def test_feedback_filter_conserves_constant_image_and_keeps_taps_in_bounds(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art));
    uint8_t *input = calloc(1, LOADING_ART_BYTES);
    unsigned int i, tap, sum;
    assert(art && input); loading_art_set(art, input);
    for (i = 0; i < LOADING_ART_FRAME_WIDTH; i++) {
        sum = 0;
        for (tap = 0; tap < 3; tap++) { assert(art->x[i].index[tap] < LOADING_ART_FRAME_WIDTH); sum += art->x[i].weight[tap]; }
        assert(sum == 2048);
    }
    for (i = 0; i < LOADING_ART_FRAME_HEIGHT; i++) {
        sum = 0;
        for (tap = 0; tap < 3; tap++) { assert(art->y[i].index[tap] < LOADING_ART_FRAME_HEIGHT); sum += art->y[i].weight[tap]; }
        assert(sum == 2048);
    }
    memset(art->history[0], 100, sizeof(art->history[0]));
    loading_art_step(art, 0);
    for (i = 0; i < LOADING_ART_FRAME_PIXELS; i++) {
        assert(art->history[1][i*4] == 90 && art->history[1][i*4+1] == 90 && art->history[1][i*4+2] == 90);
        assert(art->history[1][i*4+3] == 255 && art->history[0][i*4] == 100);
    }
    free(input); free(art); return 0;
}
''')


def test_owned_rgb_and_single_step_after_stall(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art)), *reference = calloc(1, sizeof(*reference));
    struct loading_presentation_frame frame;
    uint8_t *input = malloc(LOADING_ART_BYTES);
    unsigned int i;
    assert(art && reference && input); memset(&frame, 0, sizeof(frame));
    for (i = 0; i < LOADING_ART_PIXELS; i++) {
        input[i*4] = (uint8_t)i; input[i*4+1] = (uint8_t)(i/640);
        input[i*4+2] = (uint8_t)(255-i); input[i*4+3] = (uint8_t)(i*7);
    }
    loading_art_set(art, input);
    for (i = 0; i < LOADING_ART_PIXELS; i++) {
        assert(art->color[i*3] == input[i*4]);
        assert(art->color[i*3+1] == input[i*4+1]);
        assert(art->color[i*3+2] == input[i*4+2]);
    }
    memset(input, 0, LOADING_ART_BYTES);
    frame.epoch = 1; frame.elapsed_ns = UINT64_C(500000000);
    loading_art_render(art, &frame); memcpy(reference, art, sizeof(*art));
    frame.elapsed_ns = UINT64_C(2000000000);
    loading_art_step(reference, 120);
    loading_art_render(art, &frame);
    assert(art->current == reference->current);
    assert(!memcmp(art->history[art->current], reference->history[reference->current], sizeof(art->history[0])));
    assert(!memcmp(art->history[0], art->history[1], sizeof(art->history[0])));
    assert(!memcmp(art->output, art->history[art->current], LOADING_ART_BYTES));
    free(input); free(reference); free(art); return 0;
}
''')


def test_frame_at_the_screens_shape(tmp_path):
    """the frame drawn as wide as the screen is for its 480 rows: the picture
    covering it, the words at their size in its middle, every pixel drawn;
    VR's window stays 640"""
    run(tmp_path, r'''
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art));
    struct loading_presentation_frame frame;
    uint8_t *input = malloc(LOADING_ART_BYTES);
    const uint8_t *pixels;
    unsigned int i, x, y, words = 0, middle;
    assert(art && input); memset(&frame, 0, sizeof(frame));
    for (i = 0; i < LOADING_ART_PIXELS; i++) {
        input[i*4] = input[i*4+1] = 40; input[i*4+2] = 230; input[i*4+3] = 255;
    }
    loading_art_set(art, input);
    assert(art->width == LOADING_ART_WIDTH);
    loading_art_width(art, 2796, 1290); assert(art->width == 1040);
    loading_art_width(art, 2560, 1600); assert(art->width == 768);
    loading_art_width(art, 1024, 768); assert(art->width == LOADING_ART_WIDTH);
    loading_art_width(art, 10000, 480); assert(art->width == LOADING_ART_FRAME_MAXIMUM_WIDTH);
    loading_art_width(art, 2796, 1290);
    middle = (1040 - LOADING_ART_WIDTH) / 2;
    for (y = 0; y < LOADING_ART_FRAME_HEIGHT; y++)
        for (x = 0; x < 1040; x++)
            if (art->text[y * 1040 + x]) {
                words++;
                assert(x >= LOADING_ART_TEXT_LEFT + middle && x < LOADING_ART_TEXT_RIGHT + middle);
                assert(y >= LOADING_ART_TEXT_TOP && y < LOADING_ART_TEXT_BOTTOM);
            }
    assert(words);
    /* (the picture covers the frame: its colour at the edges too) */
    assert(art->color[2] == 230 && art->color[(1040 * 480 - 1) * 3 + 2] == 230);
    frame.epoch = 1; frame.active = 1; frame.elapsed_ns = UINT64_C(2000000000);
    pixels = loading_art_render(art, &frame);
    assert(pixels);
    for (i = 0; i < 1040 * LOADING_ART_FRAME_HEIGHT; i++)
        assert(pixels[i*4+3] == 255);
    loading_art_stereo(art, 1); assert(art->width == LOADING_ART_WIDTH);
    free(input); free(art); return 0;
}
''')
