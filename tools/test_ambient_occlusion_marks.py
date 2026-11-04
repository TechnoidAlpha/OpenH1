"""display.ambient_occlusion's marks in the stencil are those of the surface
each pixel shows: the draw that last wrote its depth, whatever drew there
before it (d3d8_device.c's ambient_occlusion_mark).

The objects draw before the level, so a wall over an object behind it once
kept the object's mark, and the occlusion found the hidden object through
the wall. Draws of a frame's opaque surfaces are played over a few pixels on
the CPU (the stencil and depth tests as the GPU does them), in the game's
order: the first-person weapon (the game's own stencil bit), then the
objects beside the game's rejection of the weapon's pixels, then the level.
The marks must follow the depth, and the game's own bits must be as they are
with the occlusion off.
"""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parent.parent


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
#include "xgpu_stream.h"

/* (xgpu_backend.h's values, and the pipeline key's stencil and depth) */
ENUMS
struct xgpu_pipeline_key
{
	unsigned long stencil_read_mask, stencil_write_mask;
	unsigned char depth_test, depth_function, depth_write;
	unsigned char stencil_test, stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
};

FUNCTION

#define PIXELS 6
#define MARKS (XGPU_AMBIENT_OCCLUSION_OBJECT | XGPU_AMBIENT_OCCLUSION_SELF_ILLUMINATED)

static unsigned char stencil[PIXELS];
static float depth[PIXELS];
/* the marks of the draw that last wrote each pixel's depth */
static unsigned char shown[PIXELS];

static int compare(unsigned char function, unsigned long a, unsigned long b)
{
	switch (function)
	{
	case XGPU_COMPARE_NEVER: return 0;
	case XGPU_COMPARE_LESS: return a < b;
	case XGPU_COMPARE_EQUAL: return a == b;
	case XGPU_COMPARE_LESS_EQUAL: return a <= b;
	case XGPU_COMPARE_GREATER: return a > b;
	case XGPU_COMPARE_NOT_EQUAL: return a != b;
	case XGPU_COMPARE_GREATER_EQUAL: return a >= b;
	default: return 1;
	}
}

static void stencil_apply(unsigned char operation, int pixel, const struct xgpu_pipeline_key *key, unsigned long reference)
{
	unsigned char value = stencil[pixel];

	switch (operation)
	{
	case XGPU_STENCIL_KEEP: return;
	case XGPU_STENCIL_ZERO: value = 0; break;
	case XGPU_STENCIL_REPLACE: value = (unsigned char)reference; break;
	case XGPU_STENCIL_INVERT: value = (unsigned char)~value; break;
	default: assert(!"an operation the game does not use"); break;
	}
	stencil[pixel] = (unsigned char)((stencil[pixel] & ~key->stencil_write_mask) | (value & key->stencil_write_mask));
}

/* the game's stencil modes (rasterizer_xbox.c's rasterizer_set_stencil_mode) */
enum { NONE, WRITE, REJECT };

/* a draw of the pixels [first, last] at a depth, with the game's stencil
mode and the occlusion's bits for it (an object's, a self-illuminated
surface's or none), the occlusion on or off */
static void draw(int occlusion, int mode, unsigned char bits, int first, int last, float z)
{
	struct xgpu_pipeline_key key;
	unsigned long reference = 0;
	int pixel;

	memset(&key, 0, sizeof(key));
	key.depth_test = 1;
	key.depth_function = XGPU_COMPARE_LESS_EQUAL;
	key.depth_write = 1;
	if (mode == WRITE)
	{
		key.stencil_test = 1;
		key.stencil_function = XGPU_COMPARE_ALWAYS;
		key.stencil_fail = key.stencil_depth_fail = XGPU_STENCIL_KEEP;
		key.stencil_pass = XGPU_STENCIL_REPLACE;
		key.stencil_read_mask = key.stencil_write_mask = 1;
		reference = 1;
	}
	else if (mode == REJECT)
	{
		key.stencil_test = 1;
		key.stencil_function = XGPU_COMPARE_EQUAL;
		key.stencil_fail = key.stencil_depth_fail = key.stencil_pass = XGPU_STENCIL_KEEP;
		key.stencil_read_mask = 1;
		key.stencil_write_mask = 0;
		reference = 0;
	}
	if (occlusion)
		ambient_occlusion_mark(&key, &reference, bits);
	for (pixel = first; pixel <= last; pixel++)
	{
		if (key.stencil_test &&
			!compare(key.stencil_function, reference & key.stencil_read_mask, stencil[pixel] & key.stencil_read_mask))
		{
			stencil_apply(key.stencil_fail, pixel, &key, reference);
			continue;
		}
		if (!compare(key.depth_function, (unsigned long)(z * 1000), (unsigned long)(depth[pixel] * 1000)))
		{
			if (key.stencil_test)
				stencil_apply(key.stencil_depth_fail, pixel, &key, reference);
			continue;
		}
		if (key.stencil_test)
			stencil_apply(key.stencil_pass, pixel, &key, reference);
		depth[pixel] = z;
		shown[pixel] = bits;
	}
}

/* a frame: the sky (no depth), the first-person weapon over pixel 0, the
objects (a biped over 1 to 4, a self-illuminated one in front of it over
3 and 4, a plain one in front of that over 4), then the level: a wall in
front of them over 0 to 2 (the weapon's pixel rejected), and the ground
behind them over 1 to 5 */
static void frame(int occlusion)
{
	memset(stencil, 0, sizeof(stencil));
	memset(shown, 0, sizeof(shown));
	for (int pixel = 0; pixel < PIXELS; pixel++)
		depth[pixel] = 1.0f;
	draw(occlusion, WRITE, 0, 0, 0, 0.05f);
	draw(occlusion, REJECT, XGPU_AMBIENT_OCCLUSION_OBJECT, 1, 4, 0.6f);
	draw(occlusion, REJECT, XGPU_AMBIENT_OCCLUSION_OBJECT | XGPU_AMBIENT_OCCLUSION_SELF_ILLUMINATED, 3, 4, 0.5f);
	draw(occlusion, REJECT, XGPU_AMBIENT_OCCLUSION_OBJECT, 4, 4, 0.45f);
	draw(occlusion, REJECT, 0, 0, 2, 0.3f);
	draw(occlusion, REJECT, 0, 1, 5, 0.8f);
}

int main(void)
{
	unsigned char game[PIXELS];
	int pixel;

	frame(0);
	for (pixel = 0; pixel < PIXELS; pixel++)
	{
		assert(!(stencil[pixel] & MARKS));
		game[pixel] = stencil[pixel];
	}
	frame(1);
	for (pixel = 0; pixel < PIXELS; pixel++)
	{
		printf("pixel %d: stencil %02x, shown %02x\n", pixel, stencil[pixel], shown[pixel]);
		/* the marks are the shown surface's; the game's bits its own */
		assert((stencil[pixel] & MARKS) == shown[pixel]);
		assert((stencil[pixel] & ~MARKS) == game[pixel]);
	}
	/* (the weapon's pixel kept; the wall over the hidden biped unmarked; a
	visible object's marked as itself) */
	assert((stencil[0] & XGPU_AMBIENT_OCCLUSION_KEEP) == 1);
	assert(!(stencil[1] & MARKS) && !(stencil[2] & MARKS) && !(stencil[5] & MARKS));
	assert((stencil[3] & MARKS) == MARKS);
	assert((stencil[4] & MARKS) == XGPU_AMBIENT_OCCLUSION_OBJECT);
	return 0;
}
"""


def test_ambient_occlusion_marks_follow_the_depth(tmp_path):
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("needs a native C compiler")
    text = (ROOT / "port/linux/src/d3d8_device.c").read_text()
    function = function_text(text, "static void ambient_occlusion_mark(")
    backend = (ROOT / "port/linux/src/xgpu_backend.h").read_text()
    enums = "".join(backend[backend.rindex("enum", 0, backend.index(first)):backend.index("};", backend.index(first)) + 2] + "\n"
                    for first in ("XGPU_COMPARE_NEVER", "XGPU_STENCIL_KEEP"))
    source = tmp_path / "marks.c"
    source.write_text(HARNESS.replace("FUNCTION", function).replace("ENUMS", enums))
    binary = tmp_path / "marks"
    subprocess.run([cc, "-std=gnu11", "-w", f"-I{ROOT / 'port/linux/src'}",
                    str(source), "-o", str(binary)], check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
