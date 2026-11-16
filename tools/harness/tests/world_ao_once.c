/* display.ambient_occlusion "world" found once a pixel (port/linux/game/world_ao_shading.c's world_ao_pass_* and
world_ao_upsample, test_world_ao_once.py): a pixel's world position found again from its distance along the
camera's ray through it, as render.c gives the camera, for points projected as the game projects them; and the
occlusion found at a quarter of the pixels read back by the surfaces with their edges kept (a foreground's pixel
beside a far wall takes the foreground's, not the wall's, and the wall's the wall's). */

#include "harness.h"

#include <math.h>

#include "world_ao.h"
#include "world_proxies.h"
#include "world_sdf.h"

/* (the shading's other parts, not under test here) */
float world_proxy_reach(struct world_proxy const *proxy) { (void)proxy; return 0.0f; }
float world_proxy_occlusion(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	(void)proxy; (void)point; (void)normal;
	return 0.0f;
}
float world_proxy_box_fade(struct world_proxy const *proxy, float const point[3], float const normal[3])
{
	(void)proxy; (void)point; (void)normal;
	return 0.0f;
}
float world_sdf_half_value(unsigned short half)
{
	(void)half;
	return 0.0f;
}
float world_sdf_sample(struct world_sdf_volume const *volume, float const point[3])
{
	(void)volume; (void)point;
	return 1.0f;
}

#include "under_test.inc"

static float dot3(float const a[3], float const b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static unsigned int seed = 12345u;

static float random_between(float low, float high)
{
	seed = seed * 1664525u + 1013904223u;
	return low + (high - low) * (float)(seed >> 8) / 16777216.0f;
}

/* A camera as render.c hands it on (render_camera_screen_to_view's view vectors through the view's axes: x the
right, y the up, -z the forward), a world point projected as the game projects it (the view's x by the projection's
x scale plus its offset, over the depth; y flipped: the screen's down), and its distance found again */
static void position_case(void)
{
	int trial;
	double worst = 0.0;

	for (trial = 0; trial < 2000; trial++)
	{
		float forward[3], up[3], right[3], eye[3], camera[4][4], point[3], found[3], offset[3];
		float p00 = random_between(0.6f, 2.0f), p11 = random_between(0.6f, 2.0f);
		float p20 = random_between(-0.2f, 0.2f), p21 = random_between(-0.2f, 0.2f);
		long corners[4] = { 100, 50, 100 + 1920, 50 + 1080 };
		float view[3], sx, sy, x, y, length, distance;
		int axis;

		/* an orthonormal frame */
		for (axis = 0; axis < 3; axis++)
		{
			forward[axis] = random_between(-1.0f, 1.0f);
			up[axis] = random_between(-1.0f, 1.0f);
			eye[axis] = random_between(-50.0f, 50.0f);
		}
		length = sqrtf(dot3(forward, forward));
		for (axis = 0; axis < 3; axis++)
			forward[axis] /= length;
		length = dot3(up, forward);
		for (axis = 0; axis < 3; axis++)
			up[axis] -= length * forward[axis];
		length = sqrtf(dot3(up, up));
		for (axis = 0; axis < 3; axis++)
			up[axis] /= length;
		right[0] = forward[1] * up[2] - forward[2] * up[1];
		right[1] = forward[2] * up[0] - forward[0] * up[2];
		right[2] = forward[0] * up[1] - forward[1] * up[0];
		/* render.c's: the view vector at the screen's middle, and its change a unit of x and of y */
		for (axis = 0; axis < 3; axis++)
		{
			camera[0][axis] = eye[axis];
			camera[1][axis] = (-p20 / p00) * right[axis] + (-p21 / p11) * up[axis] + forward[axis];
			camera[2][axis] = (1.0f / p00) * right[axis];
			camera[3][axis] = (-1.0f / p11) * up[axis];
		}
		camera[0][3] = 0.06f;
		camera[1][3] = 1024.0f;
		camera[2][3] = 0.0f;
		camera[3][3] = 2.0f;
		/* a point in view, projected through a pixel's centre */
		sx = random_between(-0.95f, 0.95f);
		sy = random_between(-0.95f, 0.95f);
		x = (float)corners[0] + floorf((sx + 1.0f) * 0.5f * (float)(corners[2] - corners[0])) + 0.5f;
		y = (float)corners[1] + floorf((sy + 1.0f) * 0.5f * (float)(corners[3] - corners[1])) + 0.5f;
		sx = 2.0f * (x - (float)corners[0]) / (float)(corners[2] - corners[0]) - 1.0f;
		sy = 2.0f * (y - (float)corners[1]) / (float)(corners[3] - corners[1]) - 1.0f;
		view[2] = random_between(0.5f, 200.0f);
		view[0] = (sx - p20) / p00 * view[2];
		view[1] = -(p21 + sy) / p11 * view[2];
		for (axis = 0; axis < 3; axis++)
			point[axis] = eye[axis] + view[0] * right[axis] + view[1] * up[axis] + view[2] * forward[axis];
		/* (the game's own projection of it: the screen's point it is drawn at) */
		for (axis = 0; axis < 3; axis++)
			offset[axis] = point[axis] - eye[axis];
		CHECK(fabsf(dot3(offset, right) / dot3(offset, forward) * p00 + p20 - sx) < 1.0e-4f, "projected x");
		CHECK(fabsf(-dot3(offset, up) / dot3(offset, forward) * p11 - p21 - sy) < 1.0e-4f, "projected y");
		distance = sqrtf(dot3(offset, offset));
		world_ao_pass_position((float const (*)[4])camera, corners, x, y, distance, eye, found);
		for (axis = 0; axis < 3; axis++)
		{
			double error = fabs((double)found[axis] - point[axis]) / (double)distance;

			if (error > worst)
				worst = error;
		}
	}
	/* (a float's rounding, at any distance: well under a pixel's width there) */
	CHECK(worst < 2.0e-5, "a position found again %g of its distance away", worst);
	printf("position: worst %g of the distance\n", worst);
}

/* A wall at 50 units (occlusion 0.9) and a narrow post before it at 5 (0.3), its normal another way, found at a
quarter of the pixels; the surfaces' pixels about the post's edges read their own surface's */
#define TEXELS_WIDE 32
#define TEXELS_HIGH 16

static void upsample_case(void)
{
	static struct world_ao_pass_texel texels[TEXELS_WIDE * TEXELS_HIGH];
	float wall_normal[3] = { 0.0f, 0.0f, 1.0f }, post_normal[3] = { 0.6f, 0.0f, 0.8f };
	unsigned int words[4];
	float decoded[3], owner, value, distance;
	int lightmap, all, cards, tx, ty, x, y, bled = 0, wrong = 0;

	for (ty = 0; ty < TEXELS_HIGH; ty++)
	{
		for (tx = 0; tx < TEXELS_WIDE; tx++)
		{
			/* (the post: pixels 30 to 33 of each row, the 2x2 blocks 15 and 16 have it nearest) */
			int post = tx == 15 || tx == 16;
			float const *normal = post ? post_normal : wall_normal;
			struct world_ao_pass_texel *texel = &texels[ty * TEXELS_WIDE + tx];

			world_ao_inputs_pack(normal, 3.0f, 1.0f, 0, 0, post, post ? 5.0f : 50.0f, words);
			CHECK(world_ao_inputs_unpack(words, decoded, &owner, &value, &lightmap, &all, &cards, &distance), "unpacked");
			texel->occlusion = post ? 0.3f : 0.9f;
			texel->distance = distance;
			texel->normal[0] = (float)(short)(words[0] & 0xffffu) / 32767.0f;
			texel->normal[1] = (float)(short)(words[0] >> 16) / 32767.0f;
			CHECK(fabsf(dot3(decoded, normal) - 1.0f) < 1.0e-4f && owner == 3.0f && distance == (post ? 5.0f : 50.0f) &&
				cards == post, "the inputs found again");
		}
	}
	for (y = 2; y < 2 * TEXELS_HIGH - 2; y++)
	{
		for (x = 24; x < 40; x++)
		{
			int post = x >= 30 && x <= 33;
			float read = world_ao_upsample(texels, TEXELS_WIDE, TEXELS_HIGH, 2.0f, (float)x + 0.5f, (float)y + 0.5f,
				post ? 5.0f : 50.0f, 1.0f, post ? post_normal : wall_normal);

			if (fabsf(read - (post ? 0.3f : 0.9f)) > 0.01f)
				wrong++;
			if (fabsf(read - (post ? 0.3f : 0.9f)) > 0.1f)
				bled++;
		}
	}
	CHECK(!wrong, "%d pixels about the post's edges read another surface's occlusion (%d by more than 0.1)", wrong,
		bled);
	/* (at scale 1, a pixel's own texel) */
	CHECK(world_ao_upsample(texels, TEXELS_WIDE, TEXELS_HIGH, 1.0f, 15.5f, 3.5f, 5.0f, 1.0f, post_normal) == 0.3f,
		"its own texel");
	printf("upsample: edges kept\n");
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "position"))
		position_case();
	else if (!strcmp(name, "upsample"))
		upsample_case();
	else
		return 2;
	return 0;
}
