/* The main menu's scene drawn for the loading screen: the ui map's sky and
   ring (loading_scene.h, read from the player's own map as the menu showed
   it) drawn into the loading art's 640x480 picture on the processor, from
   the menu's camera, the ring's surface flowing slowly along it as it
   turns; or, for VR, from each eye through a window in the world
   (loading_presentation.h). The sky is drawn once (the camera does not
   turn; an eye's view of it is moved, as what is at infinity is), the ring
   over it each time, with how much the loading screen's sweep, a light
   behind it, catches it (glint); textures are decoded once, as the scene
   arrives, to a few mip levels. Independent of the game's clocks and
   memory, as loading_art.h is. */
#ifndef LOADING_RING_H
#define LOADING_RING_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "loading_scene.h"
#include "loading_presentation.h"

#define LOADING_RING_MIPS 8
/* the largest level kept of a texture, each way */
#define LOADING_RING_TEXTURE_LIMIT 1024u
/* the far side of the ring's softening (a depth of field): how far, in
   pixels, at its farthest from the camera, for the picture and for an eye
   (VR's, a little), and from how far along the ring's depth, of 256 */
#define LOADING_RING_FOCUS_RADIUS 3
#define LOADING_RING_FOCUS_EYE_RADIUS 1
#define LOADING_RING_FOCUS_START 80
/* VR's stereo pair: the eyes' separation (metres), the game's metres to a
   world unit (vr.c's), and how much larger than the game's scale the scene
   seems (the eyes' separation that much less): the ring kilometres away */
#define LOADING_RING_VR_EYES 0.064f
#define LOADING_RING_VR_METRES_PER_UNIT 3.048f
#define LOADING_RING_VR_SCALE 8.0f

struct loading_ring_mip
{
	uint32_t width, height, shift;
	uint32_t *texels; /* 0xAABBGGRR */
};

struct loading_ring_texture
{
	uint32_t levels;
	struct loading_ring_mip mips[LOADING_RING_MIPS];
};

struct loading_ring_map
{
	uint32_t texture, function;
	float scale[2], offset[2];
};

struct loading_ring_part
{
	uint32_t layer, blend, flags, map_count;
	struct loading_ring_map maps[LOADING_SCENE_PART_MAPS];
	uint32_t first_vertex, vertex_count, first_triangle, triangle_count;
	float tint[4];
};

/* a vertex as transformed for a frame: the view's x, y, z, and the screen's */
struct loading_ring_point
{
	float x, y, z, u, v, shade, glint;
};

struct loading_ring
{
	int valid;
	uint32_t width, height;
	uint32_t part_count, texture_count, vertex_count, triangle_count;
	struct loading_ring_part parts[LOADING_SCENE_MAXIMUM_PARTS];
	struct loading_ring_texture textures[LOADING_SCENE_MAXIMUM_TEXTURES];
	float *positions, *normals, *texcoords; /* 3, 3, 2 a vertex */
	uint16_t *triangles;
	float camera_position[3], camera_forward[3], camera_up[3], camera_right[3];
	float focal, light[3];
	float spin_center[3];
	/* the frame: the sky drawn once, the colour and depth buffers, the
	vertices transformed */
	uint32_t *sky, *color;
	float *depth;
	/* how much the sweep's light catches each pixel of the ring (its rim
	and the highlight of a light behind it at the sweep's place), of 255 */
	uint8_t *glint;
	float glint_x;
	struct loading_ring_point *points;
	int sky_drawn;
	/* the view drawn: the eye's place from the camera's (world units, along
	its right, up and forward), the pixels the picture moves by for it and
	its scale (an off-axis view through VR's window: 0, 0 and 1 for the
	camera's own view) */
	float view_eye[3], view_shift[2], view_zoom;
	/* drawing only the glint, over the frame drawn before (loading_ring_glint) */
	int glint_only;
};

/* ---------- textures */

static inline uint32_t loading_ring_rgba(uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
	return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline uint32_t loading_ring_565(uint32_t value)
{
	uint32_t r = (value >> 11) & 31, g = (value >> 5) & 63, b = value & 31;

	return loading_ring_rgba(255, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

static inline uint32_t loading_ring_mix(uint32_t a, uint32_t b, uint32_t wa, uint32_t wb, uint32_t divisor)
{
	uint32_t result = 0xff000000u;
	int shift;

	for (shift = 0; shift < 24; shift += 8)
		result |= ((((a >> shift) & 255) * wa + ((b >> shift) & 255) * wb) / divisor) << shift;
	return result;
}

/* one DXT level, its rows from the first */
static inline void loading_ring_decode_dxt(uint32_t kind, const unsigned char *source, uint32_t width, uint32_t height,
	uint32_t *destination)
{
	uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4, bx, by, x, y, index;
	uint32_t block_bytes = kind == 0x0c ? 8 : 16;

	for (by = 0; by < blocks_y; by++)
	{
		for (bx = 0; bx < blocks_x; bx++)
		{
			const unsigned char *block = source + (by * blocks_x + bx) * block_bytes;
			const unsigned char *color = kind == 0x0c ? block : block + 8;
			uint32_t c0 = color[0] | (color[1] << 8), c1 = color[2] | (color[3] << 8);
			uint32_t bits = color[4] | (color[5] << 8) | ((uint32_t)color[6] << 16) | ((uint32_t)color[7] << 24);
			uint32_t palette[4], alpha[16];

			palette[0] = loading_ring_565(c0);
			palette[1] = loading_ring_565(c1);
			if (c0 > c1 || kind != 0x0c)
			{
				palette[2] = loading_ring_mix(palette[0], palette[1], 2, 1, 3);
				palette[3] = loading_ring_mix(palette[0], palette[1], 1, 2, 3);
			}
			else
			{
				palette[2] = loading_ring_mix(palette[0], palette[1], 1, 1, 2);
				palette[3] = 0;
			}
			for (index = 0; index < 16; index++)
			{
				if (kind == 0x0c)
					alpha[index] = palette[(bits >> (index * 2)) & 3] >> 24;
				else if (kind == 0x0e)
					alpha[index] = ((block[index / 2] >> ((index & 1) * 4)) & 15) * 17;
				else
				{
					uint32_t a0 = block[0], a1 = block[1], values[8], step;
					uint64_t alpha_bits = 0;
					int bit;

					for (bit = 0; bit < 6; bit++)
						alpha_bits |= (uint64_t)block[2 + bit] << (bit * 8);
					values[0] = a0;
					values[1] = a1;
					if (a0 > a1)
					{
						for (step = 2; step < 8; step++)
							values[step] = ((8 - step) * a0 + (step - 1) * a1) / 7;
					}
					else
					{
						for (step = 2; step < 6; step++)
							values[step] = ((6 - step) * a0 + (step - 1) * a1) / 5;
						values[6] = 0;
						values[7] = 255;
					}
					alpha[index] = values[(alpha_bits >> (index * 3)) & 7];
				}
			}
			for (y = 0; y < 4; y++)
			{
				for (x = 0; x < 4; x++)
				{
					uint32_t px = bx * 4 + x, py = by * 4 + y;

					if (px < width && py < height)
						destination[py * width + px] = (palette[(bits >> ((y * 4 + x) * 2)) & 3] & 0x00ffffffu) |
							(alpha[y * 4 + x] << 24);
				}
			}
		}
	}
}

/* the bytes a texel of a swizzled format takes, or 0 for one not read */
static inline uint32_t loading_ring_texel_bytes(uint32_t format)
{
	switch (format)
	{
	case 0x00: case 0x01: case 0x19: return 1;
	case 0x02: case 0x03: case 0x04: case 0x05: case 0x1a: return 2;
	case 0x06: case 0x07: return 4;
	default: return 0;
	}
}

static inline uint32_t loading_ring_texel(uint32_t format, const unsigned char *s)
{
	uint32_t v = s[0] | ((uint32_t)s[1] << 8);

	switch (format)
	{
	case 0x00: return loading_ring_rgba(255, s[0], s[0], s[0]);
	case 0x01: return loading_ring_rgba(s[0], s[0], s[0], s[0]);
	case 0x19: return loading_ring_rgba(s[0], 255, 255, 255);
	case 0x1a: return loading_ring_rgba(s[1], s[0], s[0], s[0]);
	case 0x02: return loading_ring_rgba(v & 0x8000 ? 255 : 0, ((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31,
		(v & 31) * 255 / 31);
	case 0x03: return loading_ring_rgba(255, ((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31);
	case 0x04: return loading_ring_rgba((v >> 12) * 17, ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17);
	case 0x05: return loading_ring_565(v);
	case 0x06: return loading_ring_rgba(s[3], s[2], s[1], s[0]);
	default: return loading_ring_rgba(255, s[2], s[1], s[0]);
	}
}

/* the Morton offset of x or y in a width by height swizzled texture */
static inline uint32_t loading_ring_spread(uint32_t value, uint32_t width, uint32_t height, int y_axis)
{
	uint32_t result = 0, bit = 1, out = 1;

	while (bit < width || bit < height)
	{
		if (bit < width)
		{
			if (!y_axis && (value & bit))
				result |= out;
			out <<= 1;
		}
		if (bit < height)
		{
			if (y_axis && (value & bit))
				result |= out;
			out <<= 1;
		}
		bit <<= 1;
	}
	return result;
}

static inline void loading_ring_texture_free(struct loading_ring_texture *texture)
{
	uint32_t level;

	for (level = 0; level < LOADING_RING_MIPS; level++)
		free(texture->mips[level].texels);
	memset(texture, 0, sizeof(*texture));
}

/* an Xbox texture's largest level within LOADING_RING_TEXTURE_LIMIT, and the
   levels below it made from it */
static inline int loading_ring_texture_decode(struct loading_ring_texture *texture, const struct loading_scene_texture *source,
	const unsigned char *base)
{
	uint32_t format = (source->format & 0x0000ff00u) >> 8;
	uint32_t width = 1u << ((source->format & 0x00f00000u) >> 20);
	uint32_t height = 1u << ((source->format & 0x0f000000u) >> 24);
	uint32_t levels = (source->format & 0x000f0000u) >> 16;
	uint32_t compressed = format == 0x0c || format == 0x0e || format == 0x0f;
	uint32_t bytes = loading_ring_texel_bytes(format), level, offset = 0, x, y;
	const unsigned char *texels = base + source->texel_offset;
	struct loading_ring_mip *mip;

	memset(texture, 0, sizeof(*texture));
	/* (swizzled or compressed 2D textures only: no Size word's linear
	layout, no cube map or volume) */
	if (source->size || (source->format & 4u) || ((source->format & 0xf0u) >> 4) == 3 ||
		(!compressed && !bytes) || width > 4096 || height > 4096)
		return 0;
	if (!levels)
		levels = 1;
	for (level = 0; level < levels; level++)
	{
		uint32_t w = width >> level ? width >> level : 1, h = height >> level ? height >> level : 1;
		uint32_t size = compressed ? ((w + 3) / 4) * ((h + 3) / 4) * (format == 0x0c ? 8 : 16) : w * h * bytes;

		if ((w <= LOADING_RING_TEXTURE_LIMIT && h <= LOADING_RING_TEXTURE_LIMIT) || level + 1 == levels)
		{
			if ((uint64_t)offset + size > source->texel_bytes)
				return 0;
			width = w;
			height = h;
			break;
		}
		offset += size;
	}
	mip = &texture->mips[0];
	mip->texels = malloc((size_t)width * height * 4);
	if (!mip->texels)
		return 0;
	mip->width = width;
	mip->height = height;
	for (mip->shift = 0; (1u << mip->shift) < width; mip->shift++)
		;
	if (compressed)
		loading_ring_decode_dxt(format, texels + offset, width, height, mip->texels);
	else
	{
		for (y = 0; y < height; y++)
		{
			uint32_t yo = loading_ring_spread(y, width, height, 1);

			for (x = 0; x < width; x++)
				mip->texels[y * width + x] = loading_ring_texel(format,
					texels + offset + (loading_ring_spread(x, width, height, 0) | yo) * bytes);
		}
	}
	/* (each level the box filter of the one above) */
	for (texture->levels = 1; texture->levels < LOADING_RING_MIPS; texture->levels++)
	{
		struct loading_ring_mip *above = &texture->mips[texture->levels - 1];
		struct loading_ring_mip *below = &texture->mips[texture->levels];

		if (above->width < 2 && above->height < 2)
			break;
		below->width = above->width > 1 ? above->width / 2 : 1;
		below->height = above->height > 1 ? above->height / 2 : 1;
		below->shift = above->shift ? above->shift - (above->width > 1) : 0;
		below->texels = malloc((size_t)below->width * below->height * 4);
		if (!below->texels)
			break;
		for (y = 0; y < below->height; y++)
		{
			for (x = 0; x < below->width; x++)
			{
				uint32_t x0 = x * 2 < above->width ? x * 2 : 0, x1 = x * 2 + 1 < above->width ? x * 2 + 1 : x0;
				uint32_t y0 = y * 2 < above->height ? y * 2 : 0, y1 = y * 2 + 1 < above->height ? y * 2 + 1 : y0;
				uint32_t a = above->texels[y0 * above->width + x0], b = above->texels[y0 * above->width + x1];
				uint32_t c = above->texels[y1 * above->width + x0], d = above->texels[y1 * above->width + x1];
				uint32_t result = 0;
				int shift;

				for (shift = 0; shift < 32; shift += 8)
					result |= ((((a >> shift) & 255) + ((b >> shift) & 255) + ((c >> shift) & 255) +
						((d >> shift) & 255) + 2) / 4) << shift;
				below->texels[y * below->width + x] = result;
			}
		}
	}
	return 1;
}

/* ---------- the scene */

static inline void loading_ring_clear(struct loading_ring *ring)
{
	uint32_t index;

	for (index = 0; index < LOADING_SCENE_MAXIMUM_TEXTURES; index++)
		loading_ring_texture_free(&ring->textures[index]);
	free(ring->positions);
	free(ring->normals);
	free(ring->texcoords);
	free(ring->triangles);
	free(ring->sky);
	free(ring->color);
	free(ring->depth);
	free(ring->glint);
	free(ring->points);
	memset(ring, 0, sizeof(*ring));
}

static inline float loading_ring_dot(const float *a, const float *b)
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void loading_ring_cross(const float *a, const float *b, float *result)
{
	float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];

	result[0] = x;
	result[1] = y;
	result[2] = z;
}

static inline int loading_ring_normalize(float *v)
{
	float length = sqrtf(loading_ring_dot(v, v));

	if (!(length > 1e-12f) || length != length || length > 1e30f)
		return 0;
	v[0] /= length;
	v[1] /= length;
	v[2] /= length;
	return 1;
}

static inline float loading_ring_finite(float value)
{
	return value == value && value < 1e9f && value > -1e9f ? value : 0.0f;
}

/* the middle of the world's vertices (the ring's): the camera's target when
   the menu's is not known, and which ring the scene's is */
static inline void loading_ring_center(struct loading_ring *ring)
{
	double sum[3] = { 0.0, 0.0, 0.0 };
	uint32_t part, index, count = 0;
	int i;

	for (part = 0; part < ring->part_count; part++)
	{
		const struct loading_ring_part *p = &ring->parts[part];

		if (p->layer != LOADING_SCENE_LAYER_WORLD)
			continue;
		for (index = p->first_vertex; index < p->first_vertex + p->vertex_count; index++, count++)
			for (i = 0; i < 3; i++)
				sum[i] += ring->positions[index * 3 + i];
	}
	for (i = 0; i < 3; i++)
		ring->spin_center[i] = count ? (float)(sum[i] / count) : 0.0f;
}

/* The stock ui map's ring (the middle of its vertices, as loading_ring_center
   finds it), and the camera the bundled picture shows it from, found by
   matching the picture's ring to the drawn one (position, then yaw, pitch
   and roll, radians, and the field of view across): with the stock ring,
   the loading screen shows it as the picture does, whatever the menu's
   camera was doing as the load began. */
static const float loading_ring_stock_center[3] = { 12.38f, -1.63f, 16.66f };
static const float loading_ring_picture_camera[7] = { 0.114f, 0.109f, 0.0f, -0.0002f, 0.0086f, -0.0278f, 1.2218f };

static inline int loading_ring_picture_framing(struct loading_ring *ring, float *fov)
{
	const float *c = loading_ring_picture_camera;
	float d[3], left[3];
	int i;

	for (i = 0; i < 3; i++)
		d[i] = ring->spin_center[i] - loading_ring_stock_center[i];
	if (loading_ring_dot(d, d) > 0.25f)
		return 0;
	ring->camera_forward[0] = cosf(c[3]) * cosf(c[4]);
	ring->camera_forward[1] = sinf(c[3]) * cosf(c[4]);
	ring->camera_forward[2] = sinf(c[4]);
	ring->camera_up[0] = -cosf(c[3]) * sinf(c[4]);
	ring->camera_up[1] = -sinf(c[3]) * sinf(c[4]);
	ring->camera_up[2] = cosf(c[4]);
	loading_ring_cross(ring->camera_up, ring->camera_forward, left);
	for (i = 0; i < 3; i++)
	{
		ring->camera_position[i] = c[i];
		ring->camera_up[i] = ring->camera_up[i] * cosf(c[5]) + left[i] * sinf(c[5]);
	}
	*fov = c[6];
	return 1;
}

/* the scene to draw, from a block (loading_scene.h); 0: none (NULL, or a
   block that is not one), and the scene before is forgotten either way */
static inline int loading_ring_set(struct loading_ring *ring, const void *data, size_t bytes)
{
	const struct loading_scene_header *header;
	const struct loading_scene_part *parts;
	const struct loading_scene_texture *textures;
	const unsigned char *base = data;
	uint32_t index, vertices = 0, triangles = 0;

	loading_ring_clear(ring);
	header = loading_scene_validate(data, bytes);
	if (!header)
		return 0;
	parts = (const struct loading_scene_part *)(base + header->part_offset);
	textures = (const struct loading_scene_texture *)(base + header->texture_offset);
	for (index = 0; index < header->part_count; index++)
	{
		vertices += parts[index].vertex_count;
		triangles += parts[index].triangle_count;
	}
	ring->positions = malloc((size_t)vertices * 3 * sizeof(float));
	ring->normals = malloc((size_t)vertices * 3 * sizeof(float));
	ring->texcoords = malloc((size_t)vertices * 2 * sizeof(float));
	ring->points = malloc((size_t)vertices * sizeof(*ring->points));
	ring->triangles = malloc((size_t)triangles * 3 * sizeof(uint16_t));
	if (!ring->positions || !ring->normals || !ring->texcoords || !ring->points || !ring->triangles)
	{
		loading_ring_clear(ring);
		return 0;
	}
	for (index = 0; index < header->texture_count; index++)
		loading_ring_texture_decode(&ring->textures[index], &textures[index], base);
	ring->texture_count = header->texture_count;
	for (index = 0; index < header->part_count; index++)
	{
		const struct loading_scene_part *source = &parts[index];
		const struct loading_scene_vertex *in = (const struct loading_scene_vertex *)(base + source->vertex_offset);
		struct loading_ring_part *part = &ring->parts[index];
		uint32_t vertex, row, column;
		float matrix[3][4];

		for (row = 0; row < 3; row++)
			for (column = 0; column < 4; column++)
				matrix[row][column] = loading_ring_finite(source->matrix[row][column]);
		part->layer = source->layer;
		part->blend = source->blend;
		part->flags = source->flags;
		part->map_count = source->map_count;
		for (row = 0; row < source->map_count; row++)
		{
			const struct loading_scene_map *map = &source->maps[row];

			part->maps[row].texture = map->texture != LOADING_SCENE_NO_TEXTURE && ring->textures[map->texture].levels ?
				map->texture : LOADING_SCENE_NO_TEXTURE;
			part->maps[row].function = map->function;
			for (column = 0; column < 2; column++)
			{
				part->maps[row].scale[column] = loading_ring_finite(map->scale[column]);
				part->maps[row].offset[column] = loading_ring_finite(map->offset[column]);
			}
		}
		part->first_vertex = ring->vertex_count;
		part->vertex_count = source->vertex_count;
		part->first_triangle = ring->triangle_count;
		part->triangle_count = source->triangle_count;
		for (row = 0; row < 4; row++)
		{
			float value = loading_ring_finite(source->tint[row]);

			part->tint[row] = value < 0.0f ? 0.0f : value > 4.0f ? 4.0f : value;
		}
		for (vertex = 0; vertex < source->vertex_count; vertex++)
		{
			float *position = &ring->positions[(part->first_vertex + vertex) * 3];
			float *normal = &ring->normals[(part->first_vertex + vertex) * 3];
			float *texcoord = &ring->texcoords[(part->first_vertex + vertex) * 2];
			float p[3], n[3];

			for (row = 0; row < 3; row++)
			{
				p[row] = loading_ring_finite(in[vertex].position[row]);
				n[row] = loading_ring_finite(in[vertex].normal[row]);
			}
			for (row = 0; row < 3; row++)
			{
				position[row] = matrix[row][0] * p[0] + matrix[row][1] * p[1] + matrix[row][2] * p[2] +
					(part->layer == LOADING_SCENE_LAYER_WORLD ? matrix[row][3] : 0.0f);
				normal[row] = matrix[row][0] * n[0] + matrix[row][1] * n[1] + matrix[row][2] * n[2];
			}
			if (!loading_ring_normalize(normal))
				normal[0] = normal[1] = 0.0f, normal[2] = 1.0f;
			for (row = 0; row < 2; row++)
				texcoord[row] = loading_ring_finite(in[vertex].texcoord[row]);
		}
		memcpy(&ring->triangles[part->first_triangle * 3], base + source->triangle_offset,
			(size_t)source->triangle_count * 6);
		ring->vertex_count += source->vertex_count;
		ring->triangle_count += source->triangle_count;
	}
	ring->part_count = header->part_count;
	loading_ring_center(ring);
	/* the menu's camera; or, unknown, one looking at the ring's centre */
	for (index = 0; index < 3; index++)
	{
		ring->camera_position[index] = loading_ring_finite(header->camera_position[index]);
		ring->camera_forward[index] = loading_ring_finite(header->camera_forward[index]);
		ring->camera_up[index] = loading_ring_finite(header->camera_up[index]);
		ring->light[index] = loading_ring_finite(header->light_direction[index]);
	}
	if (!loading_ring_normalize(ring->camera_forward))
	{
		for (index = 0; index < 3; index++)
			ring->camera_forward[index] = ring->spin_center[index] - ring->camera_position[index];
		if (!loading_ring_normalize(ring->camera_forward))
			ring->camera_forward[0] = 1.0f;
	}
	loading_ring_cross(ring->camera_forward, ring->camera_up, ring->camera_right);
	if (!loading_ring_normalize(ring->camera_right))
	{
		static const float z[3] = { 0.0f, 0.0f, 1.0f }, y[3] = { 0.0f, 1.0f, 0.0f };

		loading_ring_cross(ring->camera_forward, fabsf(ring->camera_forward[2]) < 0.9f ? z : y, ring->camera_right);
		loading_ring_normalize(ring->camera_right);
	}
	loading_ring_cross(ring->camera_right, ring->camera_forward, ring->camera_up);
	{
		float fov = loading_ring_finite(header->camera_field_of_view);

		/* (the stock ring as the picture shows it) */
		if (loading_ring_picture_framing(ring, &fov))
		{
			loading_ring_cross(ring->camera_forward, ring->camera_up, ring->camera_right);
			loading_ring_normalize(ring->camera_right);
			loading_ring_cross(ring->camera_right, ring->camera_forward, ring->camera_up);
		}
		if (!(fov > 0.2f && fov < 3.0f))
			fov = 1.2217305f;
		ring->focal = 0.5f / tanf(fov * 0.5f);
	}
	if (!loading_ring_normalize(ring->light))
	{
		/* (from over the camera's shoulder) */
		for (index = 0; index < 3; index++)
			ring->light[index] = ring->camera_up[index] - ring->camera_forward[index] - 0.5f * ring->camera_right[index];
		loading_ring_normalize(ring->light);
	}
	ring->valid = 1;
	return 1;
}

/* ---------- drawing */

static inline uint32_t loading_ring_sample(const struct loading_ring_texture *texture, uint32_t level, float u, float v)
{
	const struct loading_ring_mip *mip = &texture->mips[level < texture->levels ? level : texture->levels - 1];
	int x = (int)floorf(u * (float)mip->width), y = (int)floorf(v * (float)mip->height);

	return mip->texels[((uint32_t)y & (mip->height - 1)) * mip->width + ((uint32_t)x & (mip->width - 1))];
}

static inline uint32_t loading_ring_channel(uint32_t value, int shift)
{
	return (value >> shift) & 255;
}

/* a map's colour with the next map's (LOADING_SCENE_MAP_*) */
static inline uint32_t loading_ring_combine(uint32_t function, uint32_t current, uint32_t next)
{
	uint32_t result = 0;
	int shift, ca = (int)(current >> 24), na = (int)(next >> 24);

	if (function == LOADING_SCENE_MAP_CURRENT)
		return current;
	if (function == LOADING_SCENE_MAP_NEXT)
		return next;
	for (shift = 0; shift < 32; shift += 8)
	{
		int c = (int)loading_ring_channel(current, shift), n = (int)loading_ring_channel(next, shift), v;

		switch (function)
		{
		case LOADING_SCENE_MAP_MULTIPLY: v = (c * n + 127) / 255; break;
		case LOADING_SCENE_MAP_DOUBLE_MULTIPLY: v = (2 * c * n + 127) / 255; break;
		case LOADING_SCENE_MAP_ADD: v = c + n; break;
		case LOADING_SCENE_MAP_ADD_SIGNED_CURRENT: case LOADING_SCENE_MAP_ADD_SIGNED_NEXT: v = c + n - 128; break;
		case LOADING_SCENE_MAP_SUBTRACT_CURRENT: v = c - n; break;
		case LOADING_SCENE_MAP_SUBTRACT_NEXT: v = n - c; break;
		case LOADING_SCENE_MAP_BLEND_CURRENT_ALPHA: v = (c * ca + n * (255 - ca) + 127) / 255; break;
		case LOADING_SCENE_MAP_BLEND_CURRENT_ALPHA_INVERSE: v = (n * ca + c * (255 - ca) + 127) / 255; break;
		case LOADING_SCENE_MAP_BLEND_NEXT_ALPHA: v = (n * na + c * (255 - na) + 127) / 255; break;
		default: v = (c * na + n * (255 - na) + 127) / 255; break;
		}
		result |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << shift;
	}
	return result;
}

static inline uint32_t loading_ring_blend(uint32_t blend, uint32_t source, uint32_t destination)
{
	uint32_t result = 0xff000000u, alpha = source >> 24;
	int shift;

	for (shift = 0; shift < 24; shift += 8)
	{
		int s = (int)loading_ring_channel(source, shift), d = (int)loading_ring_channel(destination, shift), c;

		switch (blend)
		{
		case LOADING_SCENE_BLEND_ALPHA: c = (s * (int)alpha + d * (255 - (int)alpha) + 127) / 255; break;
		case LOADING_SCENE_BLEND_MULTIPLY: c = (s * d + 127) / 255; break;
		case LOADING_SCENE_BLEND_DOUBLE_MULTIPLY: c = (2 * s * d + 127) / 255; break;
		case LOADING_SCENE_BLEND_ADD: c = s + d; break;
		case LOADING_SCENE_BLEND_SUBTRACT: c = d - s; break;
		case LOADING_SCENE_BLEND_MINIMUM: c = s < d ? s : d; break;
		case LOADING_SCENE_BLEND_MAXIMUM: c = s > d ? s : d; break;
		case LOADING_SCENE_BLEND_ALPHA_MULTIPLY_ADD: c = d + (s * (int)alpha + 127) / 255; break;
		default: c = s; break;
		}
		result |= (uint32_t)(c < 0 ? 0 : c > 255 ? 255 : c) << shift;
	}
	return result;
}

/* one triangle of the screen's points (clipped to the near plane before) */
static inline void loading_ring_triangle(struct loading_ring *ring, const struct loading_ring_part *part, uint32_t *target,
	const struct loading_ring_point *a, const struct loading_ring_point *b, const struct loading_ring_point *c)
{
	const struct loading_ring_texture *textures[LOADING_SCENE_PART_MAPS];
	uint32_t levels[LOADING_SCENE_PART_MAPS], map;
	/* (the camera's field of view across a 4:3 frame, kept up and down: a
	wider frame sees more at its sides) */
	float w = (float)ring->width, h = (float)ring->height, scale = ring->focal * (h * 4.0f / 3.0f) * ring->view_zoom;
	float middle = w * 0.5f + ring->view_shift[0], centre = h * 0.5f - ring->view_shift[1];
	float ax = middle + scale * a->x / a->z, ay = centre - scale * a->y / a->z;
	float bx = middle + scale * b->x / b->z, by = centre - scale * b->y / b->z;
	float cx = middle + scale * c->x / c->z, cy = centre - scale * c->y / c->z;
	float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
	float iza = 1.0f / a->z, izb = 1.0f / b->z, izc = 1.0f / c->z;
	int depth_test = part->layer == LOADING_SCENE_LAYER_WORLD;
	int depth_write = depth_test && part->blend == LOADING_SCENE_BLEND_OPAQUE;
	int minimum_x, maximum_x, minimum_y, maximum_y, x, y;
	uint32_t tint[4];

	if (ring->glint_only && !depth_write)
		return;
	for (map = 0; map < 4; map++)
		tint[map] = (uint32_t)(part->tint[map] * 256.0f);
	if (!(area == area) || fabsf(area) < 1e-6f)
		return;
	if (area < 0.0f)
	{
		const struct loading_ring_point *swap = b;
		float sx = bx, sy = by, sz = izb;

		if (!(part->flags & LOADING_SCENE_PART_TWO_SIDED))
			return;
		b = c; bx = cx; by = cy; izb = izc;
		c = swap; cx = sx; cy = sy; izc = sz;
		area = -area;
	}
	minimum_x = (int)floorf(fminf(ax, fminf(bx, cx)));
	maximum_x = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
	minimum_y = (int)floorf(fminf(ay, fminf(by, cy)));
	maximum_y = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
	if (maximum_x < 0 || maximum_y < 0 || minimum_x >= (int)ring->width || minimum_y >= (int)ring->height)
		return;
	if (minimum_x < 0)
		minimum_x = 0;
	if (minimum_y < 0)
		minimum_y = 0;
	if (maximum_x > (int)ring->width - 1)
		maximum_x = (int)ring->width - 1;
	if (maximum_y > (int)ring->height - 1)
		maximum_y = (int)ring->height - 1;
	for (map = 0; map < part->map_count; map++)
	{
		const struct loading_ring_map *m = &part->maps[map];

		textures[map] = m->texture != LOADING_SCENE_NO_TEXTURE ? &ring->textures[m->texture] : NULL;
		levels[map] = 0;
		if (textures[map])
		{
			/* the level whose texels are about the size of the pixels */
			const struct loading_ring_mip *mip = &textures[map]->mips[0];
			float uv_area = fabsf(((b->u - a->u) * (c->v - a->v) - (b->v - a->v) * (c->u - a->u)) *
				m->scale[0] * m->scale[1]) * (float)mip->width * (float)mip->height;
			float ratio = uv_area / area;

			while (ratio > 2.0f && levels[map] + 1 < textures[map]->levels)
			{
				ratio *= 0.25f;
				levels[map]++;
			}
		}
	}
	{
		float inverse = 1.0f / area;
		/* each attribute over z, by the screen's x and y */
		float e0x = (by - cy) * inverse, e0y = (cx - bx) * inverse;
		float e1x = (cy - ay) * inverse, e1y = (ax - cx) * inverse;
		float px0 = (float)minimum_x + 0.5f, py0 = (float)minimum_y + 0.5f;
		float l0 = ((bx - px0) * (cy - py0) - (by - py0) * (cx - px0)) * inverse;
		float l1 = ((cx - px0) * (ay - py0) - (cy - py0) * (ax - px0)) * inverse;

		for (y = minimum_y; y <= maximum_y; y++)
		{
			float w0 = l0 + e0y * (float)(y - minimum_y), w1 = l1 + e1y * (float)(y - minimum_y);
			uint32_t *row = target + (uint32_t)y * ring->width;
			float *depth_row = ring->depth + (uint32_t)y * ring->width;

			for (x = minimum_x; x <= maximum_x; x++, w0 += e0x, w1 += e1x)
			{
				float w2 = 1.0f - w0 - w1, iz, z, u, v, shade;
				uint32_t color, r, g, bl, alpha;

				if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
					continue;
				iz = w0 * iza + w1 * izb + w2 * izc;
				if (ring->glint_only)
				{
					/* (the surface the frame shows here: the nearest's depth,
					found again) */
					if (iz < depth_row[x])
						continue;
					z = 1.0f / iz;
					shade = (w0 * a->glint * iza + w1 * b->glint * izb + w2 * c->glint * izc) * z;
					ring->glint[(uint32_t)y * ring->width + (uint32_t)x] = (uint8_t)(shade < 0.0f ? 0.0f :
						shade > 1.0f ? 255.0f : shade * 255.0f);
					continue;
				}
				if (depth_test && iz <= depth_row[x])
					continue;
				z = 1.0f / iz;
				u = (w0 * a->u * iza + w1 * b->u * izb + w2 * c->u * izc) * z;
				v = (w0 * a->v * iza + w1 * b->v * izb + w2 * c->v * izc) * z;
				shade = (w0 * a->shade * iza + w1 * b->shade * izb + w2 * c->shade * izc) * z;
				if (depth_write)
				{
					float glint = (w0 * a->glint * iza + w1 * b->glint * izb + w2 * c->glint * izc) * z;

					ring->glint[(uint32_t)y * ring->width + (uint32_t)x] = (uint8_t)(glint < 0.0f ? 0.0f :
						glint > 1.0f ? 255.0f : glint * 255.0f);
				}
				color = 0xffffffffu;
				for (map = 0; map < part->map_count; map++)
				{
					const struct loading_ring_map *m = &part->maps[map];
					uint32_t texel = textures[map] ? loading_ring_sample(textures[map], levels[map],
						u * m->scale[0] + m->offset[0], v * m->scale[1] + m->offset[1]) : 0xffffffffu;

					color = map ? loading_ring_combine(part->maps[map - 1].function, color, texel) : texel;
				}
				{
					/* (the tint and the light, of 256) */
					uint32_t light = (uint32_t)(shade > 0.0f ? shade * 256.0f : 0.0f);

					r = loading_ring_channel(color, 0) * tint[0] * light >> 16;
					g = loading_ring_channel(color, 8) * tint[1] * light >> 16;
					bl = loading_ring_channel(color, 16) * tint[2] * light >> 16;
					alpha = (part->flags & LOADING_SCENE_PART_OPAQUE_ALPHA ? 255 : color >> 24) * tint[3] >> 8;
				}
				r = r < 255 ? r : 255;
				g = g < 255 ? g : 255;
				bl = bl < 255 ? bl : 255;
				alpha = alpha < 255 ? alpha : 255;
				color = (alpha << 24) | (bl << 16) | (g << 8) | r;
				row[x] = part->blend == LOADING_SCENE_BLEND_OPAQUE ? color | 0xff000000u :
					loading_ring_blend(part->blend, color, row[x]);
				if (depth_write)
					depth_row[x] = iz;
			}
		}
	}
}

/* the part's triangles, each clipped to the near plane */
static inline void loading_ring_part_draw(struct loading_ring *ring, const struct loading_ring_part *part, uint32_t *target,
	float near_z)
{
	uint32_t triangle;

	for (triangle = part->first_triangle; triangle < part->first_triangle + part->triangle_count; triangle++)
	{
		const uint16_t *indices = &ring->triangles[triangle * 3];
		struct loading_ring_point in[3], out[4];
		int count = 0, i, inside = 0;

		for (i = 0; i < 3; i++)
		{
			in[i] = ring->points[part->first_vertex + indices[i]];
			inside += in[i].z > near_z;
		}
		if (!inside)
			continue;
		if (inside == 3)
		{
			loading_ring_triangle(ring, part, target, &in[0], &in[1], &in[2]);
			continue;
		}
		for (i = 0; i < 3; i++)
		{
			const struct loading_ring_point *p = &in[i], *q = &in[(i + 1) % 3];

			if (p->z > near_z)
				out[count++] = *p;
			if ((p->z > near_z) != (q->z > near_z))
			{
				float t = (near_z - p->z) / (q->z - p->z);

				out[count].x = p->x + (q->x - p->x) * t;
				out[count].y = p->y + (q->y - p->y) * t;
				out[count].z = near_z;
				out[count].u = p->u + (q->u - p->u) * t;
				out[count].v = p->v + (q->v - p->v) * t;
				out[count].shade = p->shade + (q->shade - p->shade) * t;
				out[count].glint = p->glint + (q->glint - p->glint) * t;
				count++;
			}
		}
		for (i = 1; i + 1 < count; i++)
			loading_ring_triangle(ring, part, target, &out[0], &out[i], &out[i + 1]);
	}
}

/* the vertices of a layer's parts as the camera sees them: still, as the
   picture is (the sweep's light alone moves), from the camera's place or an
   eye's beside it */
static inline void loading_ring_transform(struct loading_ring *ring, uint32_t layer)
{
	float eye[3], light[3];
	uint32_t part, index, i;

	for (i = 0; i < 3; i++)
		eye[i] = layer == LOADING_SCENE_LAYER_SKY ? 0.0f : ring->camera_position[i] +
			ring->view_eye[0] * ring->camera_right[i] + ring->view_eye[1] * ring->camera_up[i] +
			ring->view_eye[2] * ring->camera_forward[i];
	/* the sweep's light: behind the ring, toward the sweep's place across the
	picture (glint_x, its pixel), lighting the edges that face across it */
	{
		float across = ((ring->glint_x - (float)ring->width * 0.5f - ring->view_shift[0]) /
			((float)ring->height * 4.0f / 3.0f * ring->focal * ring->view_zoom));

		for (i = 0; i < 3; i++)
			light[i] = ring->camera_forward[i] + across * ring->camera_right[i] + 0.15f * ring->camera_up[i];
		loading_ring_normalize(light);
	}
	for (part = 0; part < ring->part_count; part++)
	{
		const struct loading_ring_part *p = &ring->parts[part];

		if (p->layer != layer)
			continue;
		for (index = p->first_vertex; index < p->first_vertex + p->vertex_count; index++)
		{
			const float *position = &ring->positions[index * 3], *normal = &ring->normals[index * 3];
			struct loading_ring_point *point = &ring->points[index];
			float d[3], shade = 1.0f, glint = 0.0f;

			for (i = 0; i < 3; i++)
				d[i] = position[i] - eye[i];
			if (p->flags & LOADING_SCENE_PART_LIT)
			{
				float lambert = loading_ring_dot(normal, ring->light), view[3], half[3], facing, specular;

				shade = 0.35f + 0.85f * (lambert > 0.0f ? lambert : 0.0f);
				/* (the rim: a surface seen edge on; the highlight: Blinn's,
				of the sweep's light behind it) */
				for (i = 0; i < 3; i++)
					view[i] = -d[i];
				loading_ring_normalize(view);
				facing = fabsf(loading_ring_dot(normal, view));
				for (i = 0; i < 3; i++)
					half[i] = light[i] + view[i];
				specular = loading_ring_normalize(half) ? fabsf(loading_ring_dot(normal, half)) : 0.0f;
				/* (to the 64th power) */
				specular *= specular;
				specular *= specular;
				specular *= specular;
				specular *= specular;
				specular *= specular;
				specular *= specular;
				facing = 1.0f - facing;
				facing *= facing;
				glint = facing * facing * 0.7f + specular;
			}
			point->x = loading_ring_dot(d, ring->camera_right);
			point->y = loading_ring_dot(d, ring->camera_up);
			point->z = loading_ring_dot(d, ring->camera_forward);
			point->u = ring->texcoords[index * 2];
			point->v = ring->texcoords[index * 2 + 1];
			point->shade = shade;
			point->glint = glint;
		}
	}
}

/* sums of a row or column of w values, each the sum of the 2r+1 about it
   (the edges' outside counted as nothing) */
static inline void loading_ring_box(const uint32_t *in, uint32_t *out, int count, int stride, int r)
{
	uint32_t sum = 0;
	int i;

	for (i = 0; i < r && i < count; i++)
		sum += in[i * stride];
	for (i = 0; i < count; i++)
	{
		if (i + r < count)
			sum += in[(i + r) * stride];
		out[i * stride] = sum;
		if (i - r >= 0)
			sum -= in[(i - r) * stride];
	}
}

/* the ring's far side softened, its near edge sharp, as the picture's is (a
   depth of field): each pixel mixed with the box of radius r about it, by
   how far along the ring's depth it is; a pixel beside the ring by the
   depth of the ring about it, so that the far side's outline softens too */
static inline void loading_ring_focus(struct loading_ring *ring, int r)
{
	uint32_t width = ring->width, height = ring->height, pixels = width * height, index;
	uint32_t *planes, *temporary;
	float nearest = 0.0f, farthest = 1e30f;
	int channel, row, column;

	if (r <= 0)
		return;
	/* (the depth buffer holds 1/z: the ring's nearest is its largest) */
	for (index = 0; index < pixels; index++)
	{
		float iz = ring->depth[index];

		if (iz > 0.0f)
		{
			nearest = fmaxf(nearest, iz);
			farthest = fminf(farthest, iz);
		}
	}
	if (!(nearest > 0.0f) || !(farthest < nearest))
		return;
	/* planes, one at a time (so that a large frame needs three): the depth's
	mix (of 256) where the ring is, and where it is (1), boxed, to each
	pixel's mix; then each channel boxed and mixed in */
	planes = malloc((size_t)pixels * 3 * sizeof(uint32_t));
	if (!planes)
		return;
	temporary = planes + (size_t)pixels * 2;
	for (index = 0; index < pixels; index++)
	{
		uint32_t mix = 0, inside = ring->depth[index] > 0.0f;

		if (inside)
		{
			float along = (1.0f / ring->depth[index] - 1.0f / nearest) / (1.0f / farthest - 1.0f / nearest);
			float t = (along * 256.0f - LOADING_RING_FOCUS_START) / (256.0f - LOADING_RING_FOCUS_START);

			t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
			mix = (uint32_t)(t * t * (3.0f - 2.0f * t) * 256.0f);
		}
		planes[index] = mix;
		planes[pixels + index] = inside;
	}
	for (channel = 0; channel < 2; channel++)
	{
		uint32_t *plane = planes + (size_t)pixels * channel;

		for (row = 0; row < (int)height; row++)
			loading_ring_box(plane + (uint32_t)row * width, temporary + (uint32_t)row * width, (int)width, 1, r);
		for (column = 0; column < (int)width; column++)
			loading_ring_box(temporary + column, plane + column, (int)height, (int)width, r);
	}
	/* (each pixel's mix: a pixel beside the ring by the ring's about it, the
	ring's own by its own depth) */
	for (index = 0; index < pixels; index++)
	{
		uint32_t mix;

		if (ring->depth[index] > 0.0f)
		{
			float along = (1.0f / ring->depth[index] - 1.0f / nearest) / (1.0f / farthest - 1.0f / nearest);
			float t = (along * 256.0f - LOADING_RING_FOCUS_START) / (256.0f - LOADING_RING_FOCUS_START);

			t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
			mix = (uint32_t)(t * t * (3.0f - 2.0f * t) * 256.0f);
		}
		else
			mix = planes[pixels + index] ? planes[index] / planes[pixels + index] : 0;
		planes[pixels + index] = mix;
	}
	{
		uint32_t area = (uint32_t)((2 * r + 1) * (2 * r + 1));
		uint32_t *mixes = planes + pixels;

		for (channel = 0; channel < 3; channel++)
		{
			for (index = 0; index < pixels; index++)
				planes[index] = (ring->color[index] >> (channel * 8)) & 255;
			for (row = 0; row < (int)height; row++)
				loading_ring_box(planes + (uint32_t)row * width, temporary + (uint32_t)row * width, (int)width, 1, r);
			for (column = 0; column < (int)width; column++)
				loading_ring_box(temporary + column, planes + column, (int)height, (int)width, r);
			for (index = 0; index < pixels; index++)
			{
				uint32_t value = ring->color[index], mix = mixes[index], sharp, soft;

				if (!mix)
					continue;
				sharp = (value >> (channel * 8)) & 255;
				soft = planes[index] / area;
				ring->color[index] = (value & ~(255u << (channel * 8))) | 0xff000000u |
					(((sharp * (256 - mix) + soft * mix) >> 8) << (channel * 8));
			}
		}
	}
	free(planes);
}

/* the sky drawn about the camera into target, moved by (dx, dy) pixels (an
   eye's view of what is at infinity); the rest black */
static inline void loading_ring_sky_copy(const struct loading_ring *ring, uint32_t *target, int dx, int dy)
{
	int width = (int)ring->width, height = (int)ring->height, y;

	if (dx <= -width || dx >= width || dy <= -height || dy >= height)
		dx = dy = 0;
	for (y = 0; y < height; y++)
	{
		uint32_t *row = target + (size_t)y * (uint32_t)width;
		int from = y - dy, x, first = dx > 0 ? dx : 0, last = dx < 0 ? width + dx : width;

		if (from < 0 || from >= height)
		{
			for (x = 0; x < width; x++)
				row[x] = 0xff000000u;
			continue;
		}
		for (x = 0; x < first; x++)
			row[x] = 0xff000000u;
		memcpy(row + first, ring->sky + (size_t)from * (uint32_t)width + (first - dx),
			(size_t)(last - first) * 4);
		for (x = last; x < width; x++)
			row[x] = 0xff000000u;
	}
}

/* the scene `seconds` into a load, into width by height pixels (0xAABBGGRR),
   from the menu's camera (eye NULL), or from an eye looking through VR's
   window (loading_presentation.h): eye is its place in metres from the
   window's middle (right, up, and out from the window toward the head), the
   camera standing for the place LOADING_PRESENTATION_VR_DISTANCE straight
   out from it, at LOADING_RING_VR_SCALE; NULL when there is none or no
   memory */
static inline const uint32_t *loading_ring_draw(struct loading_ring *ring, uint32_t width, uint32_t height, float seconds,
	const float *eye, float glint_x)
{
	uint32_t part, pixels = width * height, index;

	if (!ring->valid || !width || !height || width > 4096 || height > 4096)
		return NULL;
	if (ring->width != width || ring->height != height || !ring->color)
	{
		free(ring->sky);
		free(ring->color);
		free(ring->depth);
		free(ring->glint);
		ring->sky = malloc((size_t)pixels * 4);
		ring->color = malloc((size_t)pixels * 4);
		ring->depth = malloc((size_t)pixels * sizeof(float));
		ring->glint = malloc((size_t)pixels);
		ring->width = width;
		ring->height = height;
		ring->sky_drawn = 0;
		if (!ring->sky || !ring->color || !ring->depth || !ring->glint)
		{
			/* (the frame's buffers given up, the scene kept: a smaller
			frame may still be drawn) */
			free(ring->sky);
			free(ring->color);
			free(ring->depth);
			free(ring->glint);
			ring->sky = ring->color = NULL;
			ring->depth = NULL;
			ring->glint = NULL;
			ring->width = ring->height = 0;
			return NULL;
		}
	}
	ring->view_eye[0] = ring->view_eye[1] = ring->view_eye[2] = 0.0f;
	ring->view_shift[0] = ring->view_shift[1] = 0.0f;
	ring->view_zoom = 1.0f;
	if (!ring->sky_drawn)
	{
		/* the sky, about the camera, which only moves (never turns) */
		for (index = 0; index < pixels; index++)
			ring->sky[index] = 0xff000000u;
		loading_ring_transform(ring, LOADING_SCENE_LAYER_SKY);
		for (part = 0; part < ring->part_count; part++)
			if (ring->parts[part].layer == LOADING_SCENE_LAYER_SKY)
				loading_ring_part_draw(ring, &ring->parts[part], ring->sky, 1e-3f);
		ring->sky_drawn = 1;
	}
	if (eye)
	{
		/* (metres to the scene's units; the window's distance in them; a
		point on the window appears where it is, wherever the eye) */
		float units = 1.0f / (LOADING_RING_VR_METRES_PER_UNIT * LOADING_RING_VR_SCALE);
		float window = LOADING_PRESENTATION_VR_DISTANCE * units, scale = ring->focal * (float)width;
		float x = loading_ring_finite(eye[0]), y = loading_ring_finite(eye[1]), z = loading_ring_finite(eye[2]);

		z = fminf(fmaxf(z, 0.25f * LOADING_PRESENTATION_VR_DISTANCE), 4.0f * LOADING_PRESENTATION_VR_DISTANCE);
		ring->view_eye[0] = x * units;
		ring->view_eye[1] = y * units;
		ring->view_eye[2] = (LOADING_PRESENTATION_VR_DISTANCE - z) * units;
		ring->view_zoom = (window - ring->view_eye[2]) / window;
		ring->view_shift[0] = scale * ring->view_eye[0] / window;
		ring->view_shift[1] = scale * ring->view_eye[1] / window;
	}
	loading_ring_sky_copy(ring, ring->color, (int)floorf(ring->view_shift[0] + 0.5f),
		-(int)floorf(ring->view_shift[1] + 0.5f));
	memset(ring->depth, 0, (size_t)pixels * sizeof(float));
	memset(ring->glint, 0, (size_t)pixels);
	ring->glint_x = glint_x;
	loading_ring_transform(ring, LOADING_SCENE_LAYER_WORLD);
	/* opaque parts first, then those blended over them */
	for (part = 0; part < ring->part_count; part++)
		if (ring->parts[part].layer == LOADING_SCENE_LAYER_WORLD && ring->parts[part].blend == LOADING_SCENE_BLEND_OPAQUE)
			loading_ring_part_draw(ring, &ring->parts[part], ring->color, 1e-2f);
	for (part = 0; part < ring->part_count; part++)
		if (ring->parts[part].layer == LOADING_SCENE_LAYER_WORLD && ring->parts[part].blend != LOADING_SCENE_BLEND_OPAQUE)
			loading_ring_part_draw(ring, &ring->parts[part], ring->color, 1e-2f);
	/* (the picture's softening at its 480 rows, as much of it at any) */
	loading_ring_focus(ring, eye ? LOADING_RING_FOCUS_EYE_RADIUS :
		(int)((LOADING_RING_FOCUS_RADIUS * height + 240) / 480));
	(void)seconds;
	return ring->color;
}

/* The sweep's light on the ring again (its glint, at the sweep's place
   glint_x), over the frame the last loading_ring_draw of this size drew
   from the menu's camera: the ring is still, only the light moves; 0 (and
   nothing drawn) if there is no such frame. */
static inline int loading_ring_glint(struct loading_ring *ring, uint32_t width, uint32_t height, float glint_x)
{
	uint32_t part;

	if (!ring->valid || !ring->color || !ring->depth || !ring->glint || ring->width != width ||
		ring->height != height || ring->view_zoom != 1.0f || ring->view_eye[0] != 0.0f ||
		ring->view_eye[1] != 0.0f || ring->view_eye[2] != 0.0f)
		return 0;
	memset(ring->glint, 0, (size_t)width * height);
	ring->glint_x = glint_x;
	loading_ring_transform(ring, LOADING_SCENE_LAYER_WORLD);
	ring->glint_only = 1;
	for (part = 0; part < ring->part_count; part++)
		if (ring->parts[part].layer == LOADING_SCENE_LAYER_WORLD && ring->parts[part].blend == LOADING_SCENE_BLEND_OPAQUE)
			loading_ring_part_draw(ring, &ring->parts[part], ring->color, 1e-2f);
	ring->glint_only = 0;
	return 1;
}

/* A frame's buffers (loading_ring_draw's), set aside while the ring is drawn
   at another size and given back after: the loading screen's sharp picture is
   drawn once, at the screen's size, between the small frames its sweep is
   drawn from. */
struct loading_ring_frame
{
	uint32_t width, height;
	uint32_t *sky, *color;
	float *depth;
	uint8_t *glint;
	int sky_drawn;
};

static inline void loading_ring_frame_swap(struct loading_ring *ring, struct loading_ring_frame *frame)
{
	struct loading_ring_frame held;

	held.width = ring->width;
	held.height = ring->height;
	held.sky = ring->sky;
	held.color = ring->color;
	held.depth = ring->depth;
	held.glint = ring->glint;
	held.sky_drawn = ring->sky_drawn;
	ring->width = frame->width;
	ring->height = frame->height;
	ring->sky = frame->sky;
	ring->color = frame->color;
	ring->depth = frame->depth;
	ring->glint = frame->glint;
	ring->sky_drawn = frame->sky_drawn;
	*frame = held;
}

static inline void loading_ring_frame_free(struct loading_ring_frame *frame)
{
	free(frame->sky);
	free(frame->color);
	free(frame->depth);
	free(frame->glint);
	memset(frame, 0, sizeof(*frame));
}

#endif
