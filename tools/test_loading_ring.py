"""Compile the loading screen's scene renderer itself (loading_scene.h,
loading_ring.h, loading_art.h) on synthetic scenes: no game assets, no
graphics driver."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]

# a scene of one sky triangle and one world quad, its texture a 4x4
# swizzled A8R8G8B8 of a colour (as the Xbox's texture cache holds one)
SCENE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "loading_art.h"

struct block
{
	struct loading_scene_header header;
	struct loading_scene_part parts[2];
	struct loading_scene_texture textures[1];
	struct loading_scene_vertex vertices[7];
	uint16_t triangles[9];
	uint16_t pad;
	uint32_t texels[16];
};

static struct block *scene_new(uint32_t argb)
{
	struct block *b = calloc(1, sizeof(*b));
	unsigned int i;
	assert(b);
	b->header.magic = LOADING_SCENE_MAGIC;
	b->header.version = LOADING_SCENE_VERSION;
	b->header.bytes = sizeof(*b);
	b->header.part_count = 2;
	b->header.texture_count = 1;
	b->header.part_offset = offsetof(struct block, parts);
	b->header.texture_offset = offsetof(struct block, textures);
	/* the camera at the origin, looking along +x, z up, 70 degrees across */
	b->header.camera_forward[0] = 1.0f;
	b->header.camera_up[2] = 1.0f;
	b->header.camera_field_of_view = 1.2217305f;
	b->header.light_direction[0] = -1.0f;
	/* the sky: a triangle far ahead, untextured, alpha blended (a planet's:
	the sky's stars and galaxy, added, are not drawn) */
	b->parts[0].layer = LOADING_SCENE_LAYER_SKY;
	b->parts[0].blend = LOADING_SCENE_BLEND_ALPHA;
	b->parts[0].flags = LOADING_SCENE_PART_TWO_SIDED;
	b->parts[0].vertex_offset = offsetof(struct block, vertices);
	b->parts[0].vertex_count = 3;
	b->parts[0].triangle_offset = offsetof(struct block, triangles);
	b->parts[0].triangle_count = 1;
	b->vertices[0].position[0] = 100.0f; b->vertices[0].position[1] = 60.0f; b->vertices[0].position[2] = 40.0f;
	b->vertices[1].position[0] = 100.0f; b->vertices[1].position[1] = 40.0f; b->vertices[1].position[2] = 40.0f;
	b->vertices[2].position[0] = 100.0f; b->vertices[2].position[1] = 50.0f; b->vertices[2].position[2] = 60.0f;
	b->triangles[0] = 0; b->triangles[1] = 1; b->triangles[2] = 2;
	/* the world: a lit quad facing the camera 10 units ahead, 4 across */
	b->parts[1].layer = LOADING_SCENE_LAYER_WORLD;
	b->parts[1].blend = LOADING_SCENE_BLEND_OPAQUE;
	b->parts[1].flags = LOADING_SCENE_PART_LIT | LOADING_SCENE_PART_OPAQUE_ALPHA | LOADING_SCENE_PART_TWO_SIDED;
	b->parts[1].map_count = 1;
	b->parts[1].maps[0].texture = 0;
	b->parts[1].maps[0].scale[0] = b->parts[1].maps[0].scale[1] = 1.0f;
	b->parts[1].vertex_offset = offsetof(struct block, vertices) + 3 * sizeof(struct loading_scene_vertex);
	b->parts[1].vertex_count = 4;
	b->parts[1].triangle_offset = offsetof(struct block, triangles) + 6;
	b->parts[1].triangle_count = 2;
	for (i = 0; i < 4; i++)
	{
		struct loading_scene_vertex *v = &b->vertices[3 + i];
		v->position[0] = 10.0f;
		v->position[1] = (i & 1) ? -2.0f : 2.0f;
		v->position[2] = (i & 2) ? 2.0f : -2.0f;
		v->normal[0] = -1.0f;
		v->texcoord[0] = (i & 1) ? 1.0f : 0.0f;
		v->texcoord[1] = (i & 2) ? 0.0f : 1.0f;
	}
	b->triangles[3] = 0; b->triangles[4] = 1; b->triangles[5] = 2;
	b->triangles[6] = 1; b->triangles[7] = 3; b->triangles[8] = 2;
	for (i = 0; i < 2; i++)
	{
		b->parts[i].tint[0] = b->parts[i].tint[1] = b->parts[i].tint[2] = b->parts[i].tint[3] = 1.0f;
		b->parts[i].matrix[0][0] = b->parts[i].matrix[1][1] = b->parts[i].matrix[2][2] = 1.0f;
	}
	/* (D3DFMT_A8R8G8B8, swizzled: format 0x06, 4x4, one level) */
	b->textures[0].format = (0x06u << 8) | (1u << 16) | (2u << 20) | (2u << 24) | (2u << 4);
	b->textures[0].texel_offset = offsetof(struct block, texels);
	b->textures[0].texel_bytes = sizeof(b->texels);
	for (i = 0; i < 16; i++)
		b->texels[i] = argb;
	return b;
}
'''


def run(tmp_path, body):
    source = tmp_path / "ring.c"
    source.write_text(SCENE + body)
    binary = tmp_path / "ring"
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu89", "-O1",
                    "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined,address",
                    "-I", str(ROOT / "port/linux/src"), str(source), "-lm",
                    "-o", str(binary)], check=True, timeout=120)
    subprocess.run([str(binary)], check=True, timeout=60)


def test_scene_blocks_are_checked_before_use(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct block *b = scene_new(0xff808080u);
    assert(loading_scene_validate(b, sizeof(*b)));
    assert(!loading_scene_validate(b, sizeof(*b) - 4));
    b->header.magic ^= 1; assert(!loading_scene_validate(b, sizeof(*b))); b->header.magic ^= 1;
    b->triangles[8] = 4; assert(!loading_scene_validate(b, sizeof(*b))); b->triangles[8] = 2;
    b->parts[1].vertex_offset += sizeof(*b); assert(!loading_scene_validate(b, sizeof(*b)));
    b->parts[1].vertex_offset -= sizeof(*b);
    b->parts[1].maps[0].texture = 1; assert(!loading_scene_validate(b, sizeof(*b))); b->parts[1].maps[0].texture = 0;
    b->parts[1].maps[0].function = LOADING_SCENE_MAP_FUNCTIONS; assert(!loading_scene_validate(b, sizeof(*b)));
    b->parts[1].maps[0].function = 0;
    b->textures[0].texel_offset = 2; assert(!loading_scene_validate(b, sizeof(*b)));
    b->textures[0].texel_offset = offsetof(struct block, texels);
    b->header.part_count = 0; assert(!loading_scene_validate(b, sizeof(*b))); b->header.part_count = 2;
    assert(loading_scene_validate(b, sizeof(*b)));
    free(b); return 0;
}
''')


def test_scene_is_drawn_textured_and_lit_from_the_camera(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct block *b = scene_new(0xff3060c0u);
    static struct loading_ring ring;
    const uint32_t *pixels;
    uint32_t middle, corner, sky;
    assert(loading_ring_set(&ring, b, sizeof(*b)));
    pixels = loading_ring_draw(&ring, 640, 480, 0.0f, NULL, 320.0f);
    assert(pixels);
    middle = pixels[240 * 640 + 320]; corner = pixels[5 * 640 + 5];
    /* the quad, lit head on (0.35 + 0.85 of the texture's colour) */
    assert((middle & 0xff) > 0x30 && ((middle >> 8) & 0xff) > 0x60 && ((middle >> 16) & 0xff) > 0xc0);
    assert(((middle >> 16) & 0xff) > ((middle >> 8) & 0xff) && ((middle >> 8) & 0xff) > (middle & 0xff));
    assert((corner & 0xffffff) == 0);
    /* the sky's triangle, up and to the left, added white over black */
    sky = pixels[27 * 640 + 92];
    assert((sky & 0xffffff) == 0xffffff);
    /* the same frame again is the same frame */
    { uint32_t *copy = malloc(640 * 480 * 4); memcpy(copy, pixels, 640 * 480 * 4);
      assert(!memcmp(copy, loading_ring_draw(&ring, 640, 480, 0.0f, NULL, 320.0f), 640 * 480 * 4)); free(copy); }
    /* forgotten with a block that is not one */
    b->header.version++;
    assert(!loading_ring_set(&ring, b, sizeof(*b)) && !ring.valid);
    assert(!loading_ring_draw(&ring, 640, 480, 0.0f, NULL, 0.0f));
    loading_ring_set(&ring, NULL, 0);
    free(b); return 0;
}
''')


def test_dxt1_blocks_decode(tmp_path):
    run(tmp_path, r'''
int main(void) {
    /* red and blue, the first row red, then blue, then the thirds */
    unsigned char block[8] = { 0x00, 0xf8, 0x1f, 0x00, 0x00, 0x55, 0xaa, 0xff };
    uint32_t out[16];
    loading_ring_decode_dxt(0x0c, block, 4, 4, out);
    assert(out[0] == 0xff0000ffu && out[4] == 0xffff0000u);
    assert((out[8] & 0xff) > (out[8] >> 16 & 0xff) && (out[12] & 0xff) < (out[12] >> 16 & 0xff));
    return 0;
}
''')


def test_scene_takes_the_pictures_place_in_its_hues_and_gives_it_back(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art)), *fresh = calloc(1, sizeof(*fresh));
    struct loading_presentation_frame frame;
    uint8_t *picture = malloc(LOADING_ART_BYTES);
    struct block *b = scene_new(0xffc08040u);
    const uint8_t *out;
    unsigned int i, lit = 0;
    assert(art && fresh && picture);
    for (i = 0; i < LOADING_ART_BYTES; i++) picture[i] = (uint8_t)(i * 7 + (i >> 9));
    memset(&frame, 0, sizeof(frame));
    /* a scene with no picture is drawn */
    assert(loading_art_scene(art, b, sizeof(*b)));
    frame.epoch = 1; frame.elapsed_ns = UINT64_C(1500000000);
    out = loading_art_render(art, &frame);
    assert(out);
    for (i = 0; i < LOADING_ART_FRAME_PIXELS; i++) {
        /* the picture's band of hues, cyan to violet, whatever the texture's
           (orange): blue the most, no warm or green colour */
        assert(out[i*4+2] >= out[i*4+1] && out[i*4+2] >= out[i*4] && out[i*4+3] == 255);
        lit += out[i*4+2] > 8;
    }
    assert(lit > 1000);
    /* the picture set keeps the scene; the scene forgotten gives the picture back as it was */
    loading_art_set(art, picture); loading_art_set(fresh, picture);
    assert(art->ring.valid);
    assert(!loading_art_scene(art, NULL, 0) && !art->ring.valid);
    frame.epoch = 2;
    assert(!memcmp(loading_art_render(art, &frame), loading_art_render(fresh, &frame), LOADING_ART_FRAME_PIXELS * 4));
    free(b); free(picture); free(fresh); free(art); return 0;
}
''')


def test_vr_starts_flat_then_finds_its_depth(tmp_path):
    run(tmp_path, r'''
static unsigned int difference(const uint8_t *a, const uint8_t *b) {
    unsigned int i, d = 0;
    for (i = 0; i < LOADING_ART_FRAME_PIXELS * 4; i++) d += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
    return d;
}
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art));
    struct loading_presentation_frame frame;
    struct block *b = scene_new(0xffffffffu);
    float left[3] = { -0.032f, 0.0f, LOADING_PRESENTATION_VR_DISTANCE }, right[3] = { 0.032f, 0.0f, LOADING_PRESENTATION_VR_DISTANCE };
    unsigned int start, revealed;
    assert(art);
    /* the quad brought near enough for the eyes to see it apart */
    { unsigned int i; for (i = 3; i < 7; i++) { b->vertices[i].position[0] = 0.05f;
        b->vertices[i].position[1] *= 0.005f; b->vertices[i].position[2] *= 0.005f; } }
    memset(&frame, 0, sizeof(frame));
    assert(loading_art_scene(art, b, sizeof(*b)));
    loading_art_stereo(art, 1);
    loading_art_stereo_eyes(art, left, right);
    frame.epoch = 1; frame.elapsed_ns = 1;
    assert(loading_art_render(art, &frame) && loading_art_stereo_drawn(art));
    start = difference(art->output, art->output_right);
    frame.elapsed_ns = UINT64_C(4600000000);
    loading_art_render(art, &frame);
    revealed = difference(art->output, art->output_right);
    if (!(start == 0 && revealed > 0)) { fprintf(stderr, "start %u revealed %u\n", start, revealed); abort(); }
    assert(loading_art_stereo_tangent(art) > 0.65f && loading_art_stereo_tangent(art) < 0.75f);
    /* the window's edges dark */
    assert(art->output[(240 * 640) * 4 + 2] == 0 && art->output_right[(240 * 640 + 639) * 4 + 2] == 0);
    /* (the ring's buffers given back, for LeakSanitizer) */
    loading_art_scene(art, NULL, 0); free(b); free(art); return 0;
}
''')


def test_a_completed_load_fades_out(tmp_path):
    run(tmp_path, r'''
int main(void) {
    struct loading_art *art = calloc(1, sizeof(*art));
    struct loading_presentation_frame frame;
    struct block *b = scene_new(0xffffffffu);
    const uint8_t *out;
    unsigned int i, sum = 0;
    assert(art);
    memset(&frame, 0, sizeof(frame));
    assert(loading_art_scene(art, b, sizeof(*b)));
    frame.epoch = 1; frame.elapsed_ns = UINT64_C(1000000000);
    loading_art_render(art, &frame);
    loading_art_progress(art, 1.0f);
    loading_art_render(art, &frame);
    frame.elapsed_ns += LOADING_ART_FADE_OUT_NS;
    out = loading_art_render(art, &frame);
    for (i = 0; i < LOADING_ART_FRAME_PIXELS * 4; i += 4) sum += out[i] + out[i+1] + out[i+2];
    assert(sum == 0);
    /* a new load is not faded */
    frame.epoch = 2; frame.elapsed_ns = UINT64_C(1500000000);
    out = loading_art_render(art, &frame);
    for (i = 0, sum = 0; i < LOADING_ART_FRAME_PIXELS * 4; i += 4) sum += out[i+2];
    assert(sum > 0);
    /* (the ring's buffers given back, for LeakSanitizer) */
    loading_art_scene(art, NULL, 0); free(b); free(art); return 0;
}
''')
