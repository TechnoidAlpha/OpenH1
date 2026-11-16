/* The loading artwork and 640x480 glow, independent of game clocks and
 * memory. Integer filtering and display interpolation are not a bit-exact
 * reproduction of the console framebuffer. */
#ifndef LOADING_ART_H
#define LOADING_ART_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "loading_presentation.h"
#include "loading_art_tables.h"
#include "loading_ring.h"

#define LOADING_ART_WIDTH 640
#define LOADING_ART_HEIGHT 480
#define LOADING_ART_PIXELS (LOADING_ART_WIDTH * LOADING_ART_HEIGHT)
#define LOADING_ART_BYTES (LOADING_ART_PIXELS * 4)
#define LOADING_ART_FRAME_WIDTH LOADING_ART_WIDTH
#define LOADING_ART_FRAME_HEIGHT LOADING_ART_HEIGHT
#define LOADING_ART_FRAME_PIXELS (LOADING_ART_FRAME_WIDTH * LOADING_ART_FRAME_HEIGHT)
/* The frame is drawn at the screen's shape, 480 rows tall and as wide as
 * the screen is for them (loading_art_width): the scene seeing more at its
 * sides (Hor+), the sweep crossing all of it, the words at their size in
 * its middle, the picture scaled to cover it. Its widest: */
#define LOADING_ART_FRAME_MAXIMUM_WIDTH 1152
#define LOADING_ART_FRAME_MAXIMUM_PIXELS (LOADING_ART_FRAME_MAXIMUM_WIDTH * LOADING_ART_FRAME_HEIGHT)
/* how much of the menu's scene shows between the sweeps, of 256 */
#ifndef LOADING_ART_SCENE_BASE
#define LOADING_ART_SCENE_BASE 24
#endif
/* and of the planet behind the ring, of 256: more, as the picture's faint
planet is always there (its dark side has little the sweep can catch) */
#ifndef LOADING_ART_SKY_BASE
#define LOADING_ART_SKY_BASE 112
#endif
/* (the still layers' tints: the hue, a white speck, the sky) */
#define LOADING_ART_TINT_HUE 63u
#define LOADING_ART_TINT_SKY 64u
#define LOADING_ART_TINT_WHITE 128u
/* what the sweep lights of the scene: its brightness, its detail's edges
and the sweep's light on the ring, of 16 */
#ifndef LOADING_ART_FEED_TONE
#define LOADING_ART_FEED_TONE 10
#endif
#ifndef LOADING_ART_FEED_EDGE
#define LOADING_ART_FEED_EDGE 40
#endif
#ifndef LOADING_ART_FEED_GLINT
#define LOADING_ART_FEED_GLINT 7
#endif
/* the brightest the sweep is fed the scene but in its white specks, of 255,
 * and the brightness from which it is fed less (a third), so that a bright,
 * busy part of the ring does not whiten the feedback where the picture's
 * etching would not */
#ifndef LOADING_ART_FEED_KNEE
#define LOADING_ART_FEED_KNEE 110
#endif
#ifndef LOADING_ART_FEED_LIMIT
#define LOADING_ART_FEED_LIMIT 190
#endif
/* the most red and green there is beside blue in what is shown, of 256 */
#ifndef LOADING_ART_GREY_RED
#define LOADING_ART_GREY_RED 196
#endif
#ifndef LOADING_ART_GREY_GREEN
#define LOADING_ART_GREY_GREEN 228
#endif
/* the picture's hues (loading_art_tint_set) */
#define LOADING_ART_TINTS 16
/* the scene's specks: of 128 detailed pixels (their edges past this), how many */
#ifndef LOADING_ART_SPECKS
#define LOADING_ART_SPECKS 4
#endif
#ifndef LOADING_ART_SPECK_EDGE
#define LOADING_ART_SPECK_EDGE 40
#endif
/* the progress from which a load is complete, and how long the picture
then takes to fade out, in nanoseconds */
#define LOADING_ART_COMPLETE 0.995f
#define LOADING_ART_FADE_OUT_NS UINT64_C(500000000)
/* how long VR's depth takes to come in: the first sweep, in nanoseconds */
#define LOADING_ART_REVEAL_NS UINT64_C(4000000000)
/* and how long it takes to come up as a load begins, in nanoseconds */
#define LOADING_ART_SCENE_FADE_NS UINT64_C(400000000)

/* The sharp frame (loading_art_hd): on a screen of more than this many rows
 * the scene and the sweep's light are drawn at the screen's own rows (at
 * most LOADING_ART_HD_MAXIMUM_HEIGHT, and LOADING_ART_HD_BUDGET pixels in
 * all, the processor drawing each of them each frame), over the 480 rows'
 * trail. */
#define LOADING_ART_HD_MINIMUM_HEIGHT 540
#define LOADING_ART_HD_MAXIMUM_HEIGHT 2160
#define LOADING_ART_HD_MAXIMUM_WIDTH (LOADING_ART_HD_MAXIMUM_HEIGHT * 1152 / 480)
/* the threads that draw it, the presenter's among them */
#ifndef LOADING_ART_HD_THREADS
#define LOADING_ART_HD_THREADS 4
#endif
#ifndef LOADING_ART_HD_BUDGET
#define LOADING_ART_HD_BUDGET (1920u * 1080u)
#endif

/* The words of the supplied picture ("LOADING..."), bright blue over its
 * darker ring: their coverage in the box they are drawn in. */
#define LOADING_ART_TEXT_LEFT 232
#define LOADING_ART_TEXT_TOP 220
#define LOADING_ART_TEXT_RIGHT 408
#define LOADING_ART_TEXT_BOTTOM 256

struct loading_art_axis
{
	uint16_t index[3], weight[3];
};

struct loading_art;
/* A band of the sharp frame's rows, drawn by one thread (loading_art_hd's
 * workers or the presenter's): two of the 480 rows' trail between the steps
 * and its glint, scaled across, kept as consecutive rows read them. */
struct loading_art_hd_band
{
	struct loading_art *art;
	unsigned int first, last;
	/* the two rows (red, green, blue and glint, of 256), and a row of the
	trail between the steps */
	uint16_t *rows[2];
	int row_tags[2];
	uint8_t *mix;
	struct loading_art_hd_pool *pool;
	/* the feedback's rows blurred across, for a band of the step */
	uint32_t *step_rows[4];
	unsigned int step_tags[4], step_next;
};

struct loading_art_hd_pool
{
	pthread_mutex_t lock;
	pthread_cond_t wake, finished;
	/* a frame's number, the bands not yet drawn, whether to stop, and the
	job (0: the sharp frame's rows, 1: the feedback's step) */
	unsigned int generation, pending, quit, workers, job;
	pthread_t threads[LOADING_ART_HD_THREADS];
	struct loading_art_hd_band bands[LOADING_ART_HD_THREADS];
};


/* The sharp frame: the scene drawn once a load at the screen's rows (its
 * colour between the sweeps; what the sweep lights of it, but for the glint
 * of the sweep's light, which moves), and each frame the 480 rows' trail
 * scaled up to it, with the sweep's light added at its own pixels. */
struct loading_art_hd
{
	/* its size (0: none, the 480 rows are shown), and whether its still
	layers are drawn for this load */
	unsigned int width, height, built;
	size_t capacity;
	/* the scene as shown between the sweeps (or the picture, without one) */
	uint8_t *scene;
	/* what the sweep lights of it: the light of its tone and etching, its
	edges (for the glint), its hue in the band and whether it is a white
	speck (loading_art_scene_draw's) */
	uint16_t *light;
	uint8_t *edge, *tint;
	uint8_t *output;
	/* the words, at the sharp frame's pixels: their box, and in it each
	pixel's coverage and colour */
	unsigned int word_left, word_top, word_right, word_bottom;
	uint8_t *word;
	/* the 480 rows' trail (the history before each step's light is added),
	the last two steps' */
	uint8_t *trail[2];
	/* the 480 rows' column each sharp column reads, and its fraction, of
	256; the sweep's mask this frame, each of its 16 rows scaled across */
	uint16_t *column;
	uint8_t *column_fraction;
	uint16_t *mask_rows;
	/* this frame's: between the steps (of 256), the scene's share, what is
	left as the load completes (loading_art_hd_compose's), the sweep's
	brightness (of 65536) */
	unsigned int fraction, base, completion, light_scale, phase;
	/* where the sweep's mask is across the 480 rows' columns: a column's
	place in it is the column plus this */
	float shift;
	/* the threads drawing its rows (loading_art_hd_pool) */
	struct loading_art_hd_pool *pool;
};

struct loading_art
{
	uint8_t color[LOADING_ART_FRAME_MAXIMUM_PIXELS * 3];
	uint8_t history[2][LOADING_ART_FRAME_MAXIMUM_PIXELS * 4];
	uint8_t output[LOADING_ART_FRAME_MAXIMUM_PIXELS * 4];
	struct loading_art_axis x[LOADING_ART_FRAME_MAXIMUM_WIDTH], y[LOADING_ART_FRAME_HEIGHT];
	/* the frame's width (loading_art_width: 640 until a presenter says
	otherwise), and the supplied picture, which the frame's colour is made
	from at each width */
	unsigned int width;
	uint8_t picture[LOADING_ART_BYTES];
	uint64_t epoch, step;
	unsigned int valid, started, current;
	/* the menu's scene (loading_ring.h), when the presenter has one: drawn
	in the picture's place, in the picture's hue, with the picture's words
	over it (their coverage, text), and what the sweep lights of it (feed);
	VR's right eye's too, and its picture (stereo) */
	struct loading_ring ring;
	uint8_t scene[LOADING_ART_FRAME_MAXIMUM_PIXELS * 3];
	uint8_t feed[LOADING_ART_FRAME_MAXIMUM_PIXELS * 3];
	uint8_t text[LOADING_ART_FRAME_MAXIMUM_PIXELS];
	uint8_t scene_right[LOADING_ART_FRAME_PIXELS * 3];
	uint8_t output_right[LOADING_ART_FRAME_PIXELS * 4];
	/* the load's progress (negative: none told this load), and when it
	completed (the fade out's start) */
	float progress;
	/* the sweep following the progress (loading_art_follow): its place as
	the Xbox's had it (the progress smoothed, with a slow wobble), and the
	phase of the last two steps, for the sharp frame between them */
	unsigned int follow;
	float smoothed, wobble, phases[2];
	/* a line under the words (loading_art_caption): its coverage, at
	LOADING_PRESENTATION_CAPTION_SCALE times the 480 lines' size */
	uint8_t *caption;
	unsigned int caption_width, caption_height;
	/* and at the screen's pixels (loading_art_caption_shown): its coverage
	for a frame shown caption_rows high, that coverage's size, its pixels
	over the frame's (their memory), and how much of it is left as the load
	completes (of 256: the last render's) */
	uint8_t *caption_coverage, *caption_tile;
	unsigned int caption_rows, caption_shown_width, caption_shown_height, caption_completion;
	size_t caption_tile_capacity;
	/* and at the 480 rows, where the sweep lights it as it lights the words
	(loading_art_caption_layer): its coverage in the frame, the frame's
	width it was laid out for (0: none), and each row's colour, the words'
	(loading_art_caption_rgb) */
	uint8_t caption_text[LOADING_ART_FRAME_MAXIMUM_PIXELS];
	uint8_t caption_row_rgb[LOADING_ART_FRAME_HEIGHT][3];
	unsigned int caption_text_width;
	/* the words' colour down their capitals, a row of the picture's box
	each (where the words cover it: their rows from word_first to
	word_last, none without), and the caption's capitals' top and bottom,
	in the 480 lines */
	uint8_t word_rgb[LOADING_ART_TEXT_BOTTOM - LOADING_ART_TEXT_TOP][3];
	unsigned int word_rows, word_first, word_last;
	float caption_ink_top, caption_ink_bottom;
	/* the last render's: between its steps (of 256), and the scene's share
	between the sweeps (of 65536; 0 without a scene) */
	unsigned int caption_fraction, caption_base;
	uint64_t progress_epoch, completed_ns;
	unsigned int text_valid, stereo, eyes_valid, composed;
	/* the feedback's rows blurred across (loading_art_row) */
	uint32_t rows[4][LOADING_ART_FRAME_MAXIMUM_WIDTH * 3];
	unsigned int row_tags[4], row_next;
	float eyes[2][3];
	/* the picture's cumulative histogram, and the scene's brightness
	matched to it (loading_art_histogram, loading_art_tone) */
	uint32_t histogram[256];
	uint8_t tone[256], tints[LOADING_ART_TINTS][256][3];
	unsigned int histogram_valid, tone_valid;
	/* the scene's still layers at the 480 rows (what the sweep lights of it
	but its glint, as the sharp frame's), once drawn from the menu's camera */
	uint16_t still_light[LOADING_ART_FRAME_MAXIMUM_PIXELS];
	uint8_t still_edge[LOADING_ART_FRAME_MAXIMUM_PIXELS], still_tint[LOADING_ART_FRAME_MAXIMUM_PIXELS];
	unsigned int still_valid;
	/* the sharp frame, and the size of what the last render gave */
	struct loading_art_hd hd;
	unsigned int output_width, output_height;
};

/* Average bilinear samples at +/-0.1 pixels. Two such axes are the four
 * diagonal samples. Weights sum to 2048, including texture-edge clamping. */
static inline void loading_art_axis_set(struct loading_art_axis *axis,
	int coordinate, unsigned int limit)
{
	unsigned int sample, tap, slot;
	int point, index;
	unsigned int fraction, weight;
	memset(axis, 0, sizeof(*axis));
	for (sample = 0; sample < 2; sample++)
	{
		point = coordinate + (sample ? 102 : -102);
		if (point < 0)
			point = 0;
		if (point > (int)((limit - 1) * 1024))
			point = (int)((limit - 1) * 1024);
		index = point / 1024;
		fraction = (unsigned int)point & 1023;
		for (tap = 0; tap < 2; tap++)
		{
			unsigned int pixel = (unsigned int)index + tap;
			if (pixel >= limit)
				pixel = limit - 1;
			weight = tap ? fraction : 1024 - fraction;
			if (!weight)
				continue;
			for (slot = 0; slot < 3; slot++)
				if (!axis->weight[slot] || axis->index[slot] == pixel)
					break;
			if (slot < 3)
			{
				axis->index[slot] = (uint16_t)pixel;
				axis->weight[slot] += (uint16_t)weight;
			}
		}
	}
}

/* The picture's colours: a narrow band of hues about its blue, from cyan
 * to violet (LOADING_ART_TINTS steps of LOADING_ART_TINT_STEP degrees from
 * LOADING_ART_TINT_FIRST), by brightness: black, deep navy, the blue, and
 * nearly white at the brightest, as its etching goes. */
#ifndef LOADING_ART_TINT_FIRST
#define LOADING_ART_TINT_FIRST 198
#endif
#ifndef LOADING_ART_TINT_STEP
#define LOADING_ART_TINT_STEP 3
#endif
/* the band's middle, where a colourless surface is */
#ifndef LOADING_ART_TINT_MIDDLE
#define LOADING_ART_TINT_MIDDLE 9
#endif
/* how far the ring's own colour moves it along the band (its channels'
 * differences over this, steps) */
#ifndef LOADING_ART_TINT_SPREAD
#define LOADING_ART_TINT_SPREAD 40
#endif
/* of 8 pixels, how many are not moved toward violet by its grain (2 of
 * them toward cyan) */
#ifndef LOADING_ART_TINT_WANDER
#define LOADING_ART_TINT_WANDER 6
#endif
/* the saturation of the band's colours, of 256, below the brightness where
 * they begin to whiten */
#ifndef LOADING_ART_TINT_SATURATION
#define LOADING_ART_TINT_SATURATION 176
#endif
#ifndef LOADING_ART_TINT_WHITEN
#define LOADING_ART_TINT_WHITEN 185
#endif

static inline void loading_art_tint_set(unsigned int tint, unsigned int level, uint8_t *rgb)
{
	unsigned int hue = LOADING_ART_TINT_FIRST + tint * LOADING_ART_TINT_STEP; /* degrees */
	unsigned int saturation = level <= LOADING_ART_TINT_WHITEN ? LOADING_ART_TINT_SATURATION :
		LOADING_ART_TINT_SATURATION - (LOADING_ART_TINT_SATURATION - 24) * (level - LOADING_ART_TINT_WHITEN) /
		(255 - LOADING_ART_TINT_WHITEN);
	unsigned int sector = hue / 60 % 6, fraction = (hue % 60) * 256 / 60;
	unsigned int p = level * (256 - saturation) >> 8;
	unsigned int q = level * (256 - (saturation * fraction >> 8)) >> 8;
	unsigned int t = level * (256 - (saturation * (256 - fraction) >> 8)) >> 8;
	unsigned int r, g, b;
	switch (sector)
	{
	case 0: r = level; g = t; b = p; break;
	case 1: r = q; g = level; b = p; break;
	case 2: r = p; g = level; b = t; break;
	case 3: r = p; g = q; b = level; break;
	case 4: r = t; g = p; b = level; break;
	default: r = level; g = p; b = q; break;
	}
	rgb[0] = (uint8_t)(r < 255 ? r : 255);
	rgb[1] = (uint8_t)(g < 255 ? g : 255);
	rgb[2] = (uint8_t)(b < 255 ? b : 255);
}

/* Where in the band a pixel of the scene is: the ring's own colour turned
 * into it (a blue surface toward violet, a green one toward cyan, a grey one
 * at its middle), and, here and there in its detail, a speck of cyan,
 * lavender or near white, as the picture's etching has, at texels the
 * ring's texture picks (so that they move with it, not with the screen). */
static inline unsigned int loading_art_tint(uint32_t value, int edge, int *level, int *white)
{
	int r = (int)(value & 255), g = (int)((value >> 8) & 255), b = (int)((value >> 16) & 255);
	int tint = LOADING_ART_TINT_MIDDLE + (b - g) / LOADING_ART_TINT_SPREAD + (r - g) * 3 / (LOADING_ART_TINT_SPREAD * 2);
	unsigned int grain = ((unsigned int)r * 7u + (unsigned int)g * 13u + (unsigned int)b * 29u) & 127u;
	/* (the picture's grain: its hue wanders a step either way, pixel by
	pixel, as its etching's does) */
	unsigned int wander = ((unsigned int)r * 3u + (unsigned int)g * 5u + (unsigned int)b * 11u) >> 1 & 7u;
	tint += wander < 2 ? -1 : wander < LOADING_ART_TINT_WANDER ? 0 : 1;
	if (edge > LOADING_ART_SPECK_EDGE && grain < LOADING_ART_SPECKS)
	{
		if (grain % 3 == 0)
			tint = 0;
		else if (grain % 3 == 1)
			tint = LOADING_ART_TINTS - 1;
		else
		{
			*level += 96;
			*white = 1;
		}
	}
	return (unsigned int)(tint < 0 ? 0 : tint >= LOADING_ART_TINTS ? LOADING_ART_TINTS - 1 : tint);
}

static inline void loading_art_axes(struct loading_art *art)
{
	unsigned int index;
	/* (and the picture's colours, loading_art_tint_set's) */
	for (index = 0; index < LOADING_ART_TINTS * 256; index++)
		loading_art_tint_set(index / 256, index % 256, art->tints[index / 256][index % 256]);
	/* Stock fade quad spans +/-129/128. Its source coordinates are
	 * 3..637 horizontally and 0..480 vertically (at 640; as much in from
	 * each edge at any width). */
	for (index = 0; index < art->width; index++)
		loading_art_axis_set(&art->x[index], 2560 + (int)(
			((uint64_t)(art->width - 6) * ((2 * index + 1) * 128 + art->width) * 1024) /
			(art->width * 258)), art->width);
	for (index = 0; index < LOADING_ART_FRAME_HEIGHT; index++)
		loading_art_axis_set(&art->y[index], (int)(
			(UINT64_C(480) * ((2 * index + 1) * 128 + LOADING_ART_FRAME_HEIGHT) * 1024) /
			(LOADING_ART_FRAME_HEIGHT * 258)) - 512, LOADING_ART_FRAME_HEIGHT);
}

/* The words of the supplied picture ("LOADING..."), bright blue over its
 * darker ring: their coverage in the box they are drawn in (above). */

/* how far right of the frame's left the picture's 640 columns' middle part
 * lies: the words' place */
static inline unsigned int loading_art_middle(const struct loading_art *art)
{
	return (art->width - LOADING_ART_WIDTH) / 2;
}

static inline void loading_art_text(struct loading_art *art, const uint8_t *rgba)
{
	unsigned int x, y, middle = loading_art_middle(art);
	memset(art->text, 0, sizeof(art->text));
	for (y = LOADING_ART_TEXT_TOP; y < LOADING_ART_TEXT_BOTTOM; y++)
		for (x = LOADING_ART_TEXT_LEFT; x < LOADING_ART_TEXT_RIGHT; x++)
		{
			unsigned int blue = rgba[(y * LOADING_ART_WIDTH + x) * 4 + 2];
			art->text[y * art->width + x + middle] =
				(uint8_t)(blue <= 130 ? 0 : blue >= 210 ? 255 : (blue - 130) * 255 / 80);
		}
	art->text_valid = 1;
}

/* The frame's colour: the picture scaled to cover the frame, in its middle,
 * its top and bottom cut as the frame is wider (bilinear, one picture). */
static inline void loading_art_color(struct loading_art *art)
{
	unsigned int x, y, channel;
	/* (the picture's pixels a frame's pixel is, in 1/1024ths) */
	uint32_t step = LOADING_ART_WIDTH * 1024 / art->width;
	int top = (int)(LOADING_ART_HEIGHT * 512) - (int)(LOADING_ART_FRAME_HEIGHT * step / 2);
	for (y = 0; y < LOADING_ART_FRAME_HEIGHT; y++)
	{
		int sy = top + (int)((2 * y + 1) * step / 2) - 512;
		unsigned int y0, y1, fy;
		if (sy < 0)
			sy = 0;
		y0 = (unsigned int)sy / 1024;
		fy = (unsigned int)sy & 1023;
		if (y0 >= LOADING_ART_HEIGHT - 1)
		{
			y0 = LOADING_ART_HEIGHT - 1;
			fy = 0;
		}
		y1 = y0 + (y0 < LOADING_ART_HEIGHT - 1);
		for (x = 0; x < art->width; x++)
		{
			int sx = (int)((2 * x + 1) * step / 2) - 512;
			unsigned int x0, x1, fx;
			if (sx < 0)
				sx = 0;
			x0 = (unsigned int)sx / 1024;
			fx = (unsigned int)sx & 1023;
			if (x0 >= LOADING_ART_WIDTH - 1)
			{
				x0 = LOADING_ART_WIDTH - 1;
				fx = 0;
			}
			x1 = x0 + (x0 < LOADING_ART_WIDTH - 1);
			for (channel = 0; channel < 3; channel++)
			{
				const uint8_t *p = art->picture;
				uint32_t upper = p[(y0 * LOADING_ART_WIDTH + x0) * 4 + channel] * (1024 - fx) +
					p[(y0 * LOADING_ART_WIDTH + x1) * 4 + channel] * fx;
				uint32_t lower = p[(y1 * LOADING_ART_WIDTH + x0) * 4 + channel] * (1024 - fx) +
					p[(y1 * LOADING_ART_WIDTH + x1) * 4 + channel] * fx;
				art->color[(y * art->width + x) * 3 + channel] =
					(uint8_t)((upper * (1024 - fy) + lower * fy + (1u << 19)) >> 20);
			}
		}
	}
}

static inline unsigned int loading_art_luminance(unsigned int r, unsigned int g, unsigned int b)
{
	return (r * 54 + g * 183 + b * 19) >> 8;
}

/* How much of the picture is at most each brightness (its cumulative
 * histogram, of 65536), for the scene's brightness to match. */
static inline void loading_art_histogram(struct loading_art *art, const uint8_t *rgba)
{
	uint32_t count[256], total = 0;
	unsigned int index, level;
	memset(count, 0, sizeof(count));
	for (index = 0; index < LOADING_ART_PIXELS; index++)
		count[loading_art_luminance(rgba[index * 4], rgba[index * 4 + 1], rgba[index * 4 + 2])]++;
	for (level = 0; level < 256; level++)
	{
		total += count[level];
		art->histogram[level] = (uint32_t)((uint64_t)total * 65536 / LOADING_ART_PIXELS);
	}
	art->histogram_valid = 1;
}

static inline void loading_art_set(struct loading_art *art, const uint8_t *rgba)
{
	unsigned int stereo = art->stereo, width = art->width;
	struct loading_ring ring = art->ring;
	struct loading_art_hd hd = art->hd;
	uint8_t *caption = art->caption, *caption_coverage = art->caption_coverage, *caption_tile = art->caption_tile;
	unsigned int caption_width = art->caption_width, caption_height = art->caption_height, follow = art->follow;
	size_t caption_tile_capacity = art->caption_tile_capacity;
	memset(art, 0, sizeof(*art));
	art->ring = ring;
	art->hd = hd;
	art->caption = caption;
	art->caption_width = caption_width;
	art->caption_height = caption_height;
	/* (its coverage at the screen's made again) */
	free(caption_coverage);
	art->caption_tile = caption_tile;
	art->caption_tile_capacity = caption_tile_capacity;
	art->caption_completion = 256;
	art->follow = follow;
	art->hd.built = 0;
	art->stereo = stereo;
	art->width = width >= LOADING_ART_WIDTH && width <= LOADING_ART_FRAME_MAXIMUM_WIDTH ? width : LOADING_ART_WIDTH;
	loading_art_axes(art);
	if (!rgba)
		return;
	/* Keep the supplied artwork colors; alpha is opaque in the presentation. */
	memcpy(art->picture, rgba, LOADING_ART_BYTES);
	loading_art_color(art);
	loading_art_text(art, rgba);
	loading_art_histogram(art, rgba);
	art->valid = 1;
}

static inline void *loading_art_hd_worker(void *data);

/* the threads drawing the sharp frame's rows, each with its rows' memory;
NULL without the memory (and as few workers as can be started) */
static inline struct loading_art_hd_pool *loading_art_hd_pool_new(void)
{
	struct loading_art_hd_pool *pool = calloc(1, sizeof(*pool));
	unsigned int band;
	if (!pool)
		return NULL;
	for (band = 0; band < LOADING_ART_HD_THREADS; band++)
	{
		pool->bands[band].pool = pool;
		pool->bands[band].rows[0] = malloc(LOADING_ART_HD_MAXIMUM_WIDTH * 4 * sizeof(uint16_t));
		pool->bands[band].rows[1] = malloc(LOADING_ART_HD_MAXIMUM_WIDTH * 4 * sizeof(uint16_t));
		pool->bands[band].mix = malloc(LOADING_ART_FRAME_MAXIMUM_WIDTH * 3);
		pool->bands[band].step_rows[0] = malloc(LOADING_ART_FRAME_MAXIMUM_WIDTH * 3 * 4 * sizeof(uint32_t));
		if (!pool->bands[band].rows[0] || !pool->bands[band].rows[1] || !pool->bands[band].mix ||
			!pool->bands[band].step_rows[0])
			break;
		pool->bands[band].step_rows[1] = pool->bands[band].step_rows[0] + LOADING_ART_FRAME_MAXIMUM_WIDTH * 3;
		pool->bands[band].step_rows[2] = pool->bands[band].step_rows[1] + LOADING_ART_FRAME_MAXIMUM_WIDTH * 3;
		pool->bands[band].step_rows[3] = pool->bands[band].step_rows[2] + LOADING_ART_FRAME_MAXIMUM_WIDTH * 3;
	}
	if (band < LOADING_ART_HD_THREADS || pthread_mutex_init(&pool->lock, NULL) ||
		pthread_cond_init(&pool->wake, NULL) || pthread_cond_init(&pool->finished, NULL))
	{
		for (band = 0; band < LOADING_ART_HD_THREADS; band++)
		{
			free(pool->bands[band].rows[0]);
			free(pool->bands[band].rows[1]);
			free(pool->bands[band].mix);
			free(pool->bands[band].step_rows[0]);
		}
		free(pool);
		return NULL;
	}
	for (band = 1; band < LOADING_ART_HD_THREADS; band++)
	{
		if (pthread_create(&pool->threads[band], NULL, loading_art_hd_worker, &pool->bands[band]))
			break;
		pool->workers++;
	}
	return pool;
}

static inline void loading_art_hd_pool_free(struct loading_art_hd_pool *pool)
{
	unsigned int band;
	if (!pool)
		return;
	pthread_mutex_lock(&pool->lock);
	pool->quit = 1;
	pthread_cond_broadcast(&pool->wake);
	pthread_mutex_unlock(&pool->lock);
	for (band = 1; band <= pool->workers; band++)
		pthread_join(pool->threads[band], NULL);
	pthread_cond_destroy(&pool->wake);
	pthread_cond_destroy(&pool->finished);
	pthread_mutex_destroy(&pool->lock);
	for (band = 0; band < LOADING_ART_HD_THREADS; band++)
	{
		free(pool->bands[band].rows[0]);
		free(pool->bands[band].rows[1]);
		free(pool->bands[band].mix);
		free(pool->bands[band].step_rows[0]);
	}
	free(pool);
}

/* The sharp frame's memory and threads given up (the 480 rows are shown). */
static inline void loading_art_hd_free(struct loading_art *art)
{
	struct loading_art_hd *hd = &art->hd;
	loading_art_hd_pool_free(hd->pool);
	free(hd->scene);
	free(hd->light);
	free(hd->edge);
	free(hd->tint);
	free(hd->output);
	free(hd->word);
	free(hd->trail[0]);
	free(hd->trail[1]);
	free(hd->column);
	free(hd->column_fraction);
	free(hd->mask_rows);
	memset(hd, 0, sizeof(*hd));
}

/* The sharp frame for a screen `screen_rows` high, the 480 rows' frame
 * being art->width wide: as many rows as the screen's (within the budget),
 * of the 480 rows' shape; none on a screen of few more rows than 480, or
 * VR's window, or without the memory. */
static inline void loading_art_hd_size(struct loading_art *art, unsigned int screen_rows)
{
	struct loading_art_hd *hd = &art->hd;
	unsigned int height = 0, width = 0;
	if (!art->stereo && screen_rows > LOADING_ART_HD_MINIMUM_HEIGHT)
	{
		height = screen_rows < LOADING_ART_HD_MAXIMUM_HEIGHT ? screen_rows : LOADING_ART_HD_MAXIMUM_HEIGHT;
		width = (unsigned int)((uint64_t)art->width * height / LOADING_ART_FRAME_HEIGHT);
		if ((uint64_t)width * height > LOADING_ART_HD_BUDGET)
		{
			height = (unsigned int)sqrt((double)LOADING_ART_HD_BUDGET * LOADING_ART_FRAME_HEIGHT / art->width);
			width = (unsigned int)((uint64_t)art->width * height / LOADING_ART_FRAME_HEIGHT);
		}
		width &= ~1u;
		height &= ~1u;
		if (height <= LOADING_ART_HD_MINIMUM_HEIGHT)
			width = height = 0;
	}
	if (width == hd->width && height == hd->height)
		return;
	hd->built = 0;
	if ((size_t)width * height > hd->capacity)
	{
		size_t pixels = (size_t)width * height;
		loading_art_hd_free(art);
		hd->scene = malloc(pixels * 3);
		hd->light = malloc(pixels * sizeof(uint16_t));
		hd->edge = malloc(pixels);
		hd->tint = malloc(pixels);
		hd->output = malloc(pixels * 4);
		hd->trail[0] = calloc(LOADING_ART_FRAME_MAXIMUM_PIXELS, 3);
		hd->trail[1] = calloc(LOADING_ART_FRAME_MAXIMUM_PIXELS, 3);
		hd->column = malloc(LOADING_ART_HD_MAXIMUM_WIDTH * sizeof(uint16_t));
		hd->column_fraction = malloc(LOADING_ART_HD_MAXIMUM_WIDTH);
		hd->mask_rows = malloc(16 * LOADING_ART_HD_MAXIMUM_WIDTH * sizeof(uint16_t));
		hd->pool = loading_art_hd_pool_new();
		if (!hd->scene || !hd->light || !hd->edge || !hd->tint || !hd->output || !hd->trail[0] ||
			!hd->trail[1] || !hd->column || !hd->column_fraction || !hd->mask_rows || !hd->pool)
		{
			loading_art_hd_free(art);
			return;
		}
		hd->capacity = pixels;
	}
	hd->width = width;
	hd->height = height;
}

/* The frame's width for a screen `screen_width` by `screen_height`: as wide
 * as the screen is for 480 rows (even, 640 at the least, a 4:3 screen's,
 * and at most LOADING_ART_FRAME_MAXIMUM_WIDTH), so that the presenter fills
 * it; VR's window stays 640. A new width starts the sweep again. */
static inline void loading_art_width(struct loading_art *art, unsigned int screen_width, unsigned int screen_height)
{
	unsigned int width = screen_height ? (unsigned int)((uint64_t)LOADING_ART_FRAME_HEIGHT * screen_width /
		screen_height) & ~1u : LOADING_ART_WIDTH;
	if (art->stereo || width < LOADING_ART_WIDTH)
		width = LOADING_ART_WIDTH;
	if (width > LOADING_ART_FRAME_MAXIMUM_WIDTH)
		width = LOADING_ART_FRAME_MAXIMUM_WIDTH;
	if (width != art->width)
	{
		art->width = width;
		loading_art_axes(art);
		if (art->valid)
		{
			loading_art_color(art);
			loading_art_text(art, art->picture);
		}
		art->started = 0;
		art->still_valid = 0;
		art->hd.built = 0;
		art->hd.width = art->hd.height = 0;
	}
	/* (and the sharp frame, at the screen's rows) */
	loading_art_hd_size(art, screen_height);
}

/* The menu's scene (loading_scene.h) to draw in the picture's place; NULL
 * (or a block that is not one) returns to the picture. */
static inline int loading_art_scene(struct loading_art *art, const void *data, size_t bytes)
{
	int valid = loading_ring_set(&art->ring, data, bytes);
	art->hd.built = 0;
	art->still_valid = 0;
	if (art->width < LOADING_ART_WIDTH || art->width > LOADING_ART_FRAME_MAXIMUM_WIDTH)
		art->width = LOADING_ART_WIDTH;
	loading_art_axes(art);
	art->started = 0;
	return valid;
}

/* VR's stereo pair (loading_presentation.h): with a scene, each render
 * also draws the right eye's picture (output_right), the left eye's being
 * output; the sweep's light, one for both, lies on the window. */
static inline void loading_art_stereo(struct loading_art *art, unsigned int stereo)
{
	if (art->stereo != stereo)
	{
		art->stereo = stereo;
		art->started = 0;
		/* (VR's window: no sharp frame) */
		if (stereo)
			art->hd.width = art->hd.height = art->hd.built = 0;
		/* (VR's window: the menus' 640) */
		art->still_valid = 0;
		if (stereo && art->width != LOADING_ART_WIDTH)
		{
			art->width = LOADING_ART_WIDTH;
			loading_art_axes(art);
			if (art->valid)
			{
				loading_art_color(art);
				loading_art_text(art, art->picture);
			}
		}
	}
}

/* VR: where the eyes are, in metres from the window's middle (right, up,
 * out toward the head: loading_ring_draw's), as the frame being drawn
 * predicts them. Without them, the eyes are taken to be level before the
 * window's middle at its distance. */
static inline void loading_art_stereo_eyes(struct loading_art *art, const float *left, const float *right)
{
	unsigned int axis;
	for (axis = 0; axis < 3; axis++)
	{
		art->eyes[0][axis] = left[axis];
		art->eyes[1][axis] = right[axis];
	}
	art->eyes_valid = 1;
}

/* The eyes a frame is drawn from: the memory first, as the picture was on
 * a screen (both eyes at the window's middle: no depth), then, as the first
 * sweep crosses, the eyes come apart and follow the head, and the ring is
 * found deep behind the glass. */
static inline void loading_art_eyes(const struct loading_art *art, uint64_t elapsed_ns, float eyes[2][3])
{
	float reveal = elapsed_ns >= LOADING_ART_REVEAL_NS ? 1.0f : (float)elapsed_ns / (float)LOADING_ART_REVEAL_NS;
	unsigned int eye, axis;
	reveal = reveal * reveal * (3.0f - 2.0f * reveal);
	for (eye = 0; eye < 2; eye++)
		for (axis = 0; axis < 3; axis++)
		{
			float middle = axis == 2 ? LOADING_PRESENTATION_VR_DISTANCE : 0.0f;
			float place = art->eyes_valid ? art->eyes[eye][axis] :
				axis == 0 ? (eye ? 0.5f : -0.5f) * LOADING_RING_VR_EYES : middle;
			eyes[eye][axis] = middle + (place - middle) * reveal;
		}
}

/* VR: the tangent of half the angle the window is wide from the eyes' middle
 * (the menu's camera's field of view), or 0 without a scene. */
static inline float loading_art_stereo_tangent(const struct loading_art *art)
{
	return art->ring.valid && art->ring.focal > 0.0f ? 0.5f / art->ring.focal : 0.0f;
}

/* The load's progress, 0 to 1, for the load being drawn: as it completes
 * the picture fades out to black, as the original's did (its colour the
 * sine of the progress), so that the level comes up out of the dark. */
static inline void loading_art_progress(struct loading_art *art, float progress)
{
	art->progress = progress == progress ? (progress < 0.0f ? 0.0f : progress > 1.0f ? 1.0f : progress) : 0.0f;
	art->progress_epoch = art->epoch;
}

/* How the sweep moves: following the progress (1: once across as it goes
 * from 0 to 1, the Xbox's own, its trail fading over its last tenth), or
 * on its own clock, every four seconds (0: a map load's). */
static inline void loading_art_follow(struct loading_art *art, unsigned int follow)
{
	if (art->follow != (follow ? 1u : 0u))
	{
		art->follow = follow ? 1u : 0u;
		art->started = 0;
	}
}

/* A line under the words (a wait's: what is being done), its coverage
 * `width` by `height`, set at LOADING_PRESENTATION_CAPTION_SCALE times the
 * 480 lines' size (copied); NULL: none. */
static inline void loading_art_caption(struct loading_art *art, const uint8_t *coverage, unsigned int width,
	unsigned int height)
{
	free(art->caption);
	art->caption = NULL;
	art->caption_width = art->caption_height = 0;
	free(art->caption_coverage);
	art->caption_coverage = NULL;
	art->caption_rows = art->caption_shown_width = art->caption_shown_height = 0;
	if (!coverage || !width || !height || width > 8192 || height > 512)
		return;
	art->caption = malloc((size_t)width * height);
	if (!art->caption)
		return;
	memcpy(art->caption, coverage, (size_t)width * height);
	art->caption_width = width;
	art->caption_height = height;
}

/* the caption's colour where the picture has no words: the supplied
picture's words' blue (as their capitals have it, loading_art_word_rgb) */
#define LOADING_ART_CAPTION_RED 60
#define LOADING_ART_CAPTION_GREEN 133
#define LOADING_ART_CAPTION_BLUE 239
/* its line's top, in the 480 lines (under the words, its capitals a dozen
lines below theirs) */
#define LOADING_ART_CAPTION_TOP 258

/* The average of the values (`count` of them, each `stride` apart, each a
 * pixel's place) under the box from index * step to (index + 1) * step, in
 * 65536ths of a place: each weighted by how much of its place the box
 * covers, nothing past the last. */
static inline uint32_t loading_art_caption_box(const uint32_t *values, size_t stride, unsigned int count,
	unsigned int index, uint32_t step)
{
	uint64_t start = (uint64_t)index * step, end = start + step, sum = 0, place;
	for (place = start >> 16; place < count && place << 16 < end; place++)
	{
		uint64_t from = place << 16 > start ? place << 16 : start;
		uint64_t to = (place + 1) << 16 < end ? (place + 1) << 16 : end;
		sum += (uint64_t)values[place * stride] * (to - from);
	}
	return (uint32_t)((sum + step / 2) / step);
}

/* The caption's coverage for a frame `rows` high (480 lines its height),
 * `*width` by `*height` (allocated): each pixel the caption's under it,
 * averaged; NULL without a caption, or the memory. */
static inline uint8_t *loading_art_caption_resample(const struct loading_art *art, unsigned int rows,
	unsigned int *width_out, unsigned int *height_out)
{
	/* (the caption's pixels a frame's pixel is, in 65536ths) */
	uint32_t step;
	uint32_t *values, *across;
	uint8_t *coverage;
	unsigned int width, height, x, y;
	*width_out = *height_out = 0;
	if (!art->caption || !rows || rows > 16384)
		return NULL;
	step = (uint32_t)(((uint64_t)LOADING_ART_FRAME_HEIGHT * LOADING_PRESENTATION_CAPTION_SCALE << 16) / rows);
	if (!step)
		return NULL;
	width = (unsigned int)((((uint64_t)art->caption_width << 16) + step - 1) / step);
	height = (unsigned int)((((uint64_t)art->caption_height << 16) + step - 1) / step);
	values = malloc((size_t)art->caption_width * sizeof(uint32_t));
	across = malloc((size_t)width * art->caption_height * sizeof(uint32_t));
	coverage = malloc((size_t)width * height);
	if (!values || !across || !coverage)
	{
		free(values);
		free(across);
		free(coverage);
		return NULL;
	}
	/* (across each of the caption's rows, then down: in 256ths of a
	pixel's coverage between them) */
	for (y = 0; y < art->caption_height; y++)
	{
		for (x = 0; x < art->caption_width; x++)
			values[x] = (uint32_t)art->caption[y * art->caption_width + x] << 8;
		for (x = 0; x < width; x++)
			across[y * width + x] = loading_art_caption_box(values, 1, art->caption_width, x, step);
	}
	for (y = 0; y < height; y++)
		for (x = 0; x < width; x++)
		{
			uint32_t value = (loading_art_caption_box(across + x, width, art->caption_height, y, step) + 128) >> 8;
			coverage[y * width + x] = (uint8_t)(value < 255 ? value : 255);
		}
	free(values);
	free(across);
	*width_out = width;
	*height_out = height;
	return coverage;
}

/* The caption's coverage for a frame `rows` high, art->caption_coverage,
 * caption_shown_width by caption_shown_height (kept until the caption or
 * the rows change); 0 without a caption, or the memory. */
static inline int loading_art_caption_scaled(struct loading_art *art, unsigned int rows)
{
	if (!art->caption || !rows || rows > 16384)
		return 0;
	if (art->caption_coverage && art->caption_rows == rows)
		return 1;
	free(art->caption_coverage);
	art->caption_rows = 0;
	art->caption_coverage = loading_art_caption_resample(art, rows, &art->caption_shown_width,
		&art->caption_shown_height);
	if (!art->caption_coverage)
		return 0;
	art->caption_rows = rows;
	return 1;
}

/* The words' colour down their capitals, from the picture: in each row of
 * their box, the average of the pixels they wholly cover (as
 * loading_art_text has them), where they cover a few; a row between with
 * fewer, the row's above. */
static inline void loading_art_word_rgb(struct loading_art *art)
{
	unsigned int x, y, channel, found = 0;
	art->word_rows = 0;
	if (!art->valid)
		return;
	for (y = 0; y < LOADING_ART_TEXT_BOTTOM - LOADING_ART_TEXT_TOP; y++)
	{
		uint32_t sum[3] = { 0, 0, 0 }, count = 0;
		for (x = LOADING_ART_TEXT_LEFT; x < LOADING_ART_TEXT_RIGHT; x++)
		{
			const uint8_t *p = art->picture + ((y + LOADING_ART_TEXT_TOP) * LOADING_ART_WIDTH + x) * 4;
			if (p[2] < 210)
				continue;
			for (channel = 0; channel < 3; channel++)
				sum[channel] += p[channel];
			count++;
		}
		if (count < 8)
		{
			if (found)
				memcpy(art->word_rgb[y], art->word_rgb[y - 1], 3);
			continue;
		}
		for (channel = 0; channel < 3; channel++)
			art->word_rgb[y][channel] = (uint8_t)((sum[channel] + count / 2) / count);
		if (!found)
			art->word_first = y;
		found = 1;
		art->word_last = y;
	}
	art->word_rows = found ? art->word_last - art->word_first + 1 : 0;
}

/* The caption's colour at a height (in the 480 lines): the words' at the
 * same height of their capitals, as the caption's capitals are to theirs
 * (loading_art_caption_layer's), or LOADING_ART_CAPTION_RED's without. */
static inline void loading_art_caption_rgb(const struct loading_art *art, float y, uint8_t *rgb)
{
	float place, part;
	unsigned int row, next, channel;
	if (!art->word_rows || art->caption_ink_bottom <= art->caption_ink_top)
	{
		rgb[0] = LOADING_ART_CAPTION_RED;
		rgb[1] = LOADING_ART_CAPTION_GREEN;
		rgb[2] = LOADING_ART_CAPTION_BLUE;
		return;
	}
	place = (y - art->caption_ink_top) / (art->caption_ink_bottom - art->caption_ink_top);
	place = place < 0.0f ? 0.0f : place > 1.0f ? 1.0f : place;
	place = (float)art->word_first + place * (float)(art->word_last - art->word_first);
	row = (unsigned int)place;
	part = place - (float)row;
	next = row < art->word_last ? row + 1 : row;
	for (channel = 0; channel < 3; channel++)
		rgb[channel] = (uint8_t)((float)art->word_rgb[row][channel] * (1.0f - part) +
			(float)art->word_rgb[next][channel] * part + 0.5f);
}

/* The caption in the 480 rows, for the sweep to light as it lights the
 * words (loading_art_step_rows feeds it, VR's scene has it as it has the
 * words): laid out for the frame's width, in its middle at
 * LOADING_ART_CAPTION_TOP, where the presenter shows it, each row in the
 * words' colour (loading_art_caption_rgb); again as the caption, the
 * picture or the frame's width changes. */
static inline void loading_art_caption_layer(struct loading_art *art)
{
	uint8_t *coverage;
	unsigned int width, height, x, y, left, first = ~0u, last = 0;
	if (!art->caption)
	{
		art->caption_text_width = 0;
		return;
	}
	if (art->caption_text_width == art->width)
		return;
	art->caption_text_width = 0;
	coverage = loading_art_caption_resample(art, LOADING_ART_FRAME_HEIGHT, &width, &height);
	if (!coverage)
		return;
	if (width > art->width)
	{
		free(coverage);
		return;
	}
	/* (its capitals' top and bottom: its first and last rows with ink) */
	for (y = 0; y < art->caption_height; y++)
		for (x = 0; x < art->caption_width; x++)
			if (art->caption[y * art->caption_width + x] >= 128)
			{
				first = y < first ? y : first;
				last = y;
				break;
			}
	art->caption_ink_top = art->caption_ink_bottom = (float)LOADING_ART_CAPTION_TOP;
	if (first != ~0u)
	{
		art->caption_ink_top += (float)first / (float)LOADING_PRESENTATION_CAPTION_SCALE;
		art->caption_ink_bottom += (float)(last + 1) / (float)LOADING_PRESENTATION_CAPTION_SCALE;
	}
	loading_art_word_rgb(art);
	memset(art->caption_text, 0, (size_t)art->width * LOADING_ART_FRAME_HEIGHT);
	left = (art->width - width) / 2;
	for (y = 0; y < height && LOADING_ART_CAPTION_TOP + y < LOADING_ART_FRAME_HEIGHT; y++)
		memcpy(art->caption_text + (size_t)(LOADING_ART_CAPTION_TOP + y) * art->width + left,
			coverage + (size_t)y * width, width);
	for (y = 0; y < LOADING_ART_FRAME_HEIGHT; y++)
		loading_art_caption_rgb(art, (float)y + 0.5f, art->caption_row_rgb[y]);
	free(coverage);
	art->caption_text_width = art->width;
}

/* The 480 rows' caption layer, where it is laid out for this frame */
static inline const uint8_t *loading_art_caption_text(const struct loading_art *art)
{
	return art->caption && art->caption_text_width == art->width ? art->caption_text : NULL;
}

/* How much of the picture is left as the load completes, of 256. */
static inline unsigned int loading_art_completion(struct loading_art *art,
	const struct loading_presentation_frame *frame)
{
	uint64_t since;
	if (art->progress_epoch != frame->epoch || art->progress < LOADING_ART_COMPLETE)
	{
		art->completed_ns = 0;
		return 256;
	}
	if (!art->completed_ns)
		art->completed_ns = frame->elapsed_ns ? frame->elapsed_ns : 1;
	since = frame->elapsed_ns > art->completed_ns ? frame->elapsed_ns - art->completed_ns : 0;
	return since >= LOADING_ART_FADE_OUT_NS ? 0 : (unsigned int)(256 - since * 256 / LOADING_ART_FADE_OUT_NS);
}

/* Whether the last render drew the stereo pair. */
static inline int loading_art_stereo_drawn(const struct loading_art *art)
{
	return art->stereo && art->ring.valid && art->started;
}

/* The scene's brightness to the picture's: the scene's cumulative
 * histogram matched to the picture's, as a load's first frame has it. */
static inline void loading_art_tone(struct loading_art *art, const uint32_t *pixels)
{
	uint32_t count[256], total = 0, frame_pixels = art->width * LOADING_ART_FRAME_HEIGHT;
	unsigned int index, level, target = 0;
	memset(count, 0, sizeof(count));
	for (index = 0; index < frame_pixels; index++)
		count[loading_art_luminance(pixels[index] & 255, (pixels[index] >> 8) & 255, (pixels[index] >> 16) & 255)]++;
	for (level = 0; level < 256; level++)
	{
		uint32_t share;
		total += count[level];
		share = (uint32_t)((uint64_t)total * 65536 / frame_pixels);
		while (art->histogram_valid && target < 255 && art->histogram[target] < share)
			target++;
		art->tone[level] = (uint8_t)(art->histogram_valid ? target : level);
	}
	art->tone_valid = 1;
}

/* The sweep's light on the ring: where the sweep's band is across the
 * picture at a phase (loading_art_step's mask at its brightest), in pixels. */
static inline float loading_art_band(const struct loading_art *art, unsigned int phase)
{
	/* (it crosses from 128 pixels left of the frame to its right edge:
	3.2 a phase at 640) */
	return (float)(art->width + LOADING_ART_WIDTH / 5) / 240.0f * (float)phase - 64.5f;
}

/* The scene for one eye (NULL: the menu's camera; else VR's, loading_ring_draw's) `seconds` into
 * a load, the sweep at `phase`: in the picture's one hue (its brightness
 * matched to the picture's, its detail etched brighter, as the picture's
 * ring is), the picture's words over it; and how much the sweep's light
 * catches each pixel (the ring's rim and highlight, its detail's edges). */
static inline int loading_art_scene_draw(struct loading_art *art, float seconds, unsigned int phase, const float *eye,
	uint8_t *scene, uint8_t *feed)
{
	const uint32_t *pixels;
	const uint8_t *caption = loading_art_caption_text(art);
	unsigned int width = art->width, middle = loading_art_middle(art);
	unsigned int x, y;
	/* (the ring is still, from the menu's camera: after its first picture
	only the sweep's light on it moves, and only that is drawn again) */
	if (!eye && art->still_valid && loading_ring_glint(&art->ring, width, LOADING_ART_FRAME_HEIGHT,
		loading_art_band(art, phase)))
	{
		if (feed)
			for (y = 0; y < LOADING_ART_FRAME_HEIGHT; y++)
				for (x = 0; x < width; x++)
				{
					unsigned int pixel = y * width + x, tint = art->still_tint[pixel] & LOADING_ART_TINT_HUE;
					unsigned int white = art->still_tint[pixel] >> 7, coverage, channel;
					int light = (int)art->still_light[pixel] +
						art->ring.glint[pixel] * (64 + art->still_edge[pixel]) * LOADING_ART_FEED_GLINT / 2048;
					if (light > LOADING_ART_FEED_KNEE && !white)
						light = LOADING_ART_FEED_KNEE + (light - LOADING_ART_FEED_KNEE) / 3;
					if (light > LOADING_ART_FEED_LIMIT && !white)
						light = LOADING_ART_FEED_LIMIT;
					memcpy(feed + pixel * 3, art->tints[tint][light > 255 ? 255 : light], 3);
					coverage = art->text_valid ? art->text[pixel] : 0;
					if (coverage)
						for (channel = 0; channel < 3; channel++)
						{
							int word = art->picture[(y * LOADING_ART_WIDTH + x - middle) * 4 + channel];
							feed[pixel * 3 + channel] = (uint8_t)(feed[pixel * 3 + channel] +
								(word - feed[pixel * 3 + channel]) * (int)coverage / 255);
						}
				}
		return 1;
	}
	pixels = loading_ring_draw(&art->ring, art->width, LOADING_ART_FRAME_HEIGHT, seconds,
		eye, loading_art_band(art, phase));
	if (!pixels)
		return 0;
	if (!art->tone_valid)
		loading_art_tone(art, pixels);
	for (y = 0; y < LOADING_ART_FRAME_HEIGHT; y++)
		for (x = 0; x < width; x++)
		{
			unsigned int pixel = y * width + x, value = pixels[pixel];
			int level = (int)art->tone[loading_art_luminance(value & 255, (value >> 8) & 255, (value >> 16) & 255)];
			int edge = 0, coverage, light, channel, white;
			unsigned int tint;
			uint8_t rgb[3];
			if (x > 0 && y > 0 && x + 1 < width && y + 1 < LOADING_ART_FRAME_HEIGHT)
			{
				const uint32_t *p = pixels + pixel;
				int gx = (int)((p[1] >> 8) & 255) - (int)((p[-1] >> 8) & 255);
				int gy = (int)((p[width] >> 8) & 255) - (int)((p[-(int)width] >> 8) & 255);
				edge = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
			}
			coverage = art->text_valid ? art->text[pixel] : 0;
			white = 0;
			tint = loading_art_tint(value, edge, &level, &white);
			memcpy(scene + pixel * 3, art->tints[tint][level + edge > 255 ? 255 : level + edge], 3);
			if (!eye)
			{
				/* (kept, for the steps that draw only the sweep's light) */
				light = (level * LOADING_ART_FEED_TONE + edge * LOADING_ART_FEED_EDGE) / 16;
				art->still_light[pixel] = (uint16_t)(light < 65535 ? light : 65535);
				art->still_edge[pixel] = (uint8_t)(edge < 192 ? edge : 192);
				art->still_tint[pixel] = (uint8_t)(tint | (white ? LOADING_ART_TINT_WHITE : 0u) |
					(art->ring.depth[pixel] > 0.0f ? 0u : LOADING_ART_TINT_SKY));
			}
			if (feed)
			{
				/* what the sweep lights: the ring etched as the picture's is
				(its detail's edges strong over its darker surface), the
				sweep's light on its rim and highlight, in its detail */
				light = (level * LOADING_ART_FEED_TONE + edge * LOADING_ART_FEED_EDGE) / 16 +
					art->ring.glint[pixel] * (64 + (edge < 192 ? edge : 192)) * LOADING_ART_FEED_GLINT / 2048;
				/* (white only in the specks: the feedback's whites are grey
				as they fade, the picture's few) */
				if (light > LOADING_ART_FEED_KNEE && !white)
					light = LOADING_ART_FEED_KNEE + (light - LOADING_ART_FEED_KNEE) / 3;
				if (light > LOADING_ART_FEED_LIMIT && !white)
					light = LOADING_ART_FEED_LIMIT;
				memcpy(feed + pixel * 3, art->tints[tint][light > 255 ? 255 : light], 3);
			}
			/* (the words, where they are: the picture's own, at their size) */
			if (coverage)
				for (channel = 0; channel < 3; channel++)
				{
					rgb[channel] = art->picture[(y * LOADING_ART_WIDTH + x - middle) * 4 + channel];
					scene[pixel * 3 + channel] = (uint8_t)(scene[pixel * 3 + channel] +
						(rgb[channel] - scene[pixel * 3 + channel]) * coverage / 255);
					if (feed)
						feed[pixel * 3 + channel] = (uint8_t)(feed[pixel * 3 + channel] +
							(rgb[channel] - feed[pixel * 3 + channel]) * coverage / 255);
				}
			/* (and an eye's, VR's window, the caption under them as the
			words are, there being no screen's pixels for it: the feed has
			it from loading_art_step_rows) */
			if (eye && caption && caption[pixel])
				for (channel = 0; channel < 3; channel++)
					scene[pixel * 3 + channel] = (uint8_t)(scene[pixel * 3 + channel] +
						(art->caption_row_rgb[y][channel] - scene[pixel * 3 + channel]) * caption[pixel] / 255);
		}
	art->still_valid = !eye;
	return 1;
}

static inline unsigned int loading_art_mask(int x, unsigned int y)
{
	if (x < 0 || x >= 128)
		return 5;
	return loading_art_mask_table[y * 128 + (unsigned int)x];
}

/* A row of the previous history blurred across (its horizontal taps): the
 * diagonal samples' weights are the axes' products, so the sum over rows of
 * these, weighted, is the same sum, in fewer multiplications. A few rows
 * are kept, as consecutive output rows read the same ones. */
static inline const uint32_t *loading_art_row(struct loading_art *art, const uint8_t *previous, unsigned int row,
	uint32_t *const *rows, unsigned int *tags, unsigned int *next)
{
	unsigned int slot, x, tap;
	uint32_t *sums;
	for (slot = 0; slot < 4; slot++)
		if (tags[slot] == row + 1)
			return rows[slot];
	slot = (*next)++ & 3;
	tags[slot] = row + 1;
	sums = rows[slot];
	for (x = 0; x < art->width; x++)
	{
		const struct loading_art_axis *ax = &art->x[x];
		uint32_t r = 0, g = 0, b = 0;
		for (tap = 0; tap < 3; tap++)
		{
			const uint8_t *sample;
			uint32_t weight = ax->weight[tap];
			if (!weight)
				continue;
			sample = previous + (row * art->width + ax->index[tap]) * 4;
			r += sample[0] * weight;
			g += sample[1] * weight;
			b += sample[2] * weight;
		}
		sums[x * 3] = r;
		sums[x * 3 + 1] = g;
		sums[x * 3 + 2] = b;
	}
	return sums;
}

/* The feedback's rows `first` to `last` for the step at `phase` (a
 * thread's band of them, with its own rows blurred across). */
static inline void loading_art_step_rows(struct loading_art *art, unsigned int phase, unsigned int first,
	unsigned int last, uint32_t *const *cache, unsigned int *tags, unsigned int *next)
{
	const uint8_t *previous = art->history[art->current];
	uint8_t *output = art->history[art->current ^ 1];
	unsigned int x, y, channel, ty, fade, brightness;
	int mask_x, mask_y, ix, iy;
	unsigned int fx, fy, mask, upper, lower, pixel, value, sum[3], weight;
	const struct loading_art_axis *ay;
	const uint8_t *source = art->ring.valid ? art->feed : art->color;
	/* (and the caption, which the sweep lights as it lights the words) */
	const uint8_t *caption = loading_art_caption_text(art);
	const uint32_t *rows[3];
	/* (the trail before the light is added, for the sharp frame) */
	uint8_t *trail = art->hd.built ? art->hd.trail[art->current ^ 1] : NULL;
	fade = phase <= 216 ? 216 : (240 - phase) * 9;
	brightness = loading_art_brightness[phase];
	memset(tags, 0, 4 * sizeof(*tags));
	for (y = first; y < last; y++)
	{
		ay = &art->y[y];
		for (ty = 0; ty < 3; ty++)
			rows[ty] = ay->weight[ty] ? loading_art_row(art, previous, ay->index[ty], cache, tags, next) : NULL;
		mask_y = (int)((2 * y + 1) * 16 * 128 / LOADING_ART_FRAME_HEIGHT) - 128;
		if (mask_y < 0)
			mask_y = 0;
		if (mask_y > 15 * 256)
			mask_y = 15 * 256;
		iy = mask_y / 256;
		fy = (unsigned int)mask_y & 255;
		for (x = 0; x < art->width; x++)
		{
			/* (the sweep's band crosses from 128 pixels left of the frame to
			past its right edge, as it crosses the 640) */
			mask_x = (int)(x * 256 + 32768) - (int)(phase * (art->width + 128) * 256 / 240);
			/* Floor division, including the border to the left of the mask. */
			ix = mask_x >= 0 ? mask_x / 256 : -((-mask_x + 255) / 256);
			fx = (unsigned int)(mask_x - ix * 256);
			upper = loading_art_mask(ix, (unsigned int)iy) * (256 - fx) +
				loading_art_mask(ix + 1, (unsigned int)iy) * fx;
			lower = loading_art_mask(ix, iy < 15 ? (unsigned int)iy + 1 : 15) * (256 - fx) +
				loading_art_mask(ix + 1, iy < 15 ? (unsigned int)iy + 1 : 15) * fx;
			mask = (upper * (256 - fy) + lower * fy + 32768) >> 16;
			pixel = y * art->width + x;
			sum[0] = sum[1] = sum[2] = 0;
			for (ty = 0; ty < 3; ty++)
			{
				if (!rows[ty])
					continue;
				weight = ay->weight[ty];
				sum[0] += rows[ty][x * 3] * weight;
				sum[1] += rows[ty][x * 3 + 1] * weight;
				sum[2] += rows[ty][x * 3 + 2] * weight;
			}
			for (channel = 0; channel < 3; channel++)
			{
				/* Average and fade before quantizing. The blur shader does
				 * not use the near-white diffuse RGB supplied by the draw. */
				value = (unsigned int)(((uint64_t)sum[channel] * fade + UINT64_C(503316480)) /
					UINT64_C(1006632960));
				unsigned int fed = source[pixel * 3 + channel];
				if (trail)
					trail[pixel * 3 + channel] = (uint8_t)(value < 255 ? value : 255);
				if (caption && caption[pixel])
					fed = (unsigned int)((int)fed + ((int)art->caption_row_rgb[y][channel] - (int)fed) *
						(int)caption[pixel] / 255);
				value += (fed * brightness * mask + 32512) / 65025;
				output[pixel * 4 + channel] = (uint8_t)(value < 255 ? value : 255);
			}
			output[pixel * 4 + 3] = 255;
		}
	}
}

static inline void loading_art_hd_run(struct loading_art *art, unsigned int job, unsigned int rows);

/* One step of the feedback, at `phase`: the previous history blurred and
 * faded, the sweep's light added (with the sharp frame's threads, where it
 * has them). */
static inline void loading_art_step(struct loading_art *art, unsigned int phase)
{
	if (art->hd.built && art->hd.pool && art->hd.pool->workers)
	{
		art->hd.phase = phase;
		loading_art_hd_run(art, 1, LOADING_ART_FRAME_HEIGHT);
	}
	else
	{
		uint32_t *cache[4];
		cache[0] = art->rows[0];
		cache[1] = art->rows[1];
		cache[2] = art->rows[2];
		cache[3] = art->rows[3];
		loading_art_step_rows(art, phase, 0, LOADING_ART_FRAME_HEIGHT, cache, art->row_tags, &art->row_next);
	}
	art->current ^= 1;
}

/* What is shown of the feedback and of an eye's scene: the scene stays in
 * view between the sweeps, dimly (faded in over the load's first moments),
 * added to what is shown, never to the feedback; VR's window darkens to
 * its edges, into the dark about it. */
static inline void loading_art_compose(struct loading_art *art, const uint8_t *previous, const uint8_t *current,
	unsigned int fraction, const uint8_t *scene, unsigned int base, uint8_t *output)
{
	unsigned int x, y, channel, pixel;
	if (!scene)
	{
		for (pixel = 0; pixel < art->width * LOADING_ART_FRAME_HEIGHT; pixel++)
		{
			for (channel = 0; channel < 3; channel++)
				output[pixel * 4 + channel] = (uint8_t)((previous[pixel * 4 + channel] *
					(256 - fraction) + current[pixel * 4 + channel] * fraction + 128) >> 8);
			output[pixel * 4 + 3] = 255;
		}
		return;
	}
	for (y = 0; y < LOADING_ART_FRAME_HEIGHT; y++)
	{
		unsigned int vy = art->stereo ? (y < 40 ? y * 256 / 40 : y >= LOADING_ART_FRAME_HEIGHT - 40 ?
			(LOADING_ART_FRAME_HEIGHT - 1 - y) * 256 / 40 : 256) : 256;
		for (x = 0; x < art->width; x++)
		{
			unsigned int value, edge = vy, shown = base;
			pixel = y * art->width + x;
			/* (the planet behind the ring more than the ring, from the
			menu's camera) */
			if (!art->stereo && art->still_valid && (art->still_tint[pixel] & LOADING_ART_TINT_SKY))
				shown = base * LOADING_ART_SKY_BASE / LOADING_ART_SCENE_BASE;
			/* (VR's window darkens toward its edges) */
			if (art->stereo)
				edge = (x < 48 ? x * 256 / 48 : x >= art->width - 48 ?
					(art->width - 1 - x) * 256 / 48 : 256) * vy >> 8;
			for (channel = 0; channel < 3; channel++)
			{
				value = ((previous[pixel * 4 + channel] * (256 - fraction) +
					current[pixel * 4 + channel] * fraction + 128) >> 8) + (scene[pixel * 3 + channel] * shown >> 16);
				output[pixel * 4 + channel] = (uint8_t)(((value < 255 ? value : 255) * edge) >> 8);
			}
			/* (the feedback's channels saturate apart, and its whites fade
			grey: kept in the picture's blues, its whites a pale blue) */
			value = output[pixel * 4 + 2] * LOADING_ART_GREY_RED >> 8;
			if (output[pixel * 4] > value)
				output[pixel * 4] = (uint8_t)value;
			value = output[pixel * 4 + 2] * LOADING_ART_GREY_GREEN >> 8;
			if (output[pixel * 4 + 1] > value)
				output[pixel * 4 + 1] = (uint8_t)value;
			output[pixel * 4 + 3] = 255;
		}
	}
}

/* ---------- the sharp frame */

/* The picture (640x480 RGBA) at a point of it, in its pixels (their middles
 * at whole numbers): Catmull-Rom's cubic each way, edges clamped, so that
 * the words and the picture scaled up stay crisp. */
static inline void loading_art_picture_sample(const uint8_t *picture, float x, float y, uint8_t *rgb)
{
	int ix = (int)floorf(x), iy = (int)floorf(y), i, j, channel;
	float fx = x - (float)ix, fy = y - (float)iy, wx[4], wy[4];
	float sum[3] = { 0.0f, 0.0f, 0.0f };
	wx[0] = ((-0.5f * fx + 1.0f) * fx - 0.5f) * fx;
	wx[1] = (1.5f * fx - 2.5f) * fx * fx + 1.0f;
	wx[2] = ((-1.5f * fx + 2.0f) * fx + 0.5f) * fx;
	wx[3] = (0.5f * fx - 0.5f) * fx * fx;
	wy[0] = ((-0.5f * fy + 1.0f) * fy - 0.5f) * fy;
	wy[1] = (1.5f * fy - 2.5f) * fy * fy + 1.0f;
	wy[2] = ((-1.5f * fy + 2.0f) * fy + 0.5f) * fy;
	wy[3] = (0.5f * fy - 0.5f) * fy * fy;
	for (j = 0; j < 4; j++)
	{
		int row = iy - 1 + j;
		row = row < 0 ? 0 : row >= LOADING_ART_HEIGHT ? LOADING_ART_HEIGHT - 1 : row;
		for (i = 0; i < 4; i++)
		{
			int column = ix - 1 + i;
			const uint8_t *p;
			float weight = wx[i] * wy[j];
			column = column < 0 ? 0 : column >= LOADING_ART_WIDTH ? LOADING_ART_WIDTH - 1 : column;
			p = picture + (row * LOADING_ART_WIDTH + column) * 4;
			for (channel = 0; channel < 3; channel++)
				sum[channel] += weight * (float)p[channel];
		}
	}
	for (channel = 0; channel < 3; channel++)
		rgb[channel] = (uint8_t)(sum[channel] <= 0.0f ? 0 : sum[channel] >= 255.0f ? 255 : (int)(sum[channel] + 0.5f));
}

/* The sharp frame's still layers, once a load (after its first step: the
 * scene's brightness matched then): the scene drawn at the sharp frame's
 * size, coloured and etched as loading_art_scene_draw colours the 480 rows,
 * with the words, from the picture, over it; or, without a scene, the
 * picture scaled to cover it. 0 if it cannot be (no memory). */
static inline int loading_art_hd_build(struct loading_art *art)
{
	struct loading_art_hd *hd = &art->hd;
	unsigned int width = hd->width, height = hd->height, x, y;
	unsigned int middle = loading_art_middle(art);
	/* (the 480 rows' pixels a sharp pixel is, each way) */
	float across = (float)art->width / (float)width, down = (float)LOADING_ART_FRAME_HEIGHT / (float)height;
	for (x = 0; x < width; x++)
	{
		/* (the 480 rows' column, in 256ths, of the sharp column's middle) */
		int source = (int)(((2 * x + 1) * (uint64_t)art->width * 128) / width) - 128;
		if (source < 0)
			source = 0;
		if (source >= (int)(art->width - 1) * 256)
			source = (int)(art->width - 1) * 256 - 1;
		hd->column[x] = (uint16_t)(source >> 8);
		hd->column_fraction[x] = (uint8_t)(source & 255);
	}
	hd->word_left = hd->word_top = hd->word_right = hd->word_bottom = 0;
	if (art->ring.valid)
	{
		struct loading_ring_frame frame;
		const uint32_t *pixels;
		memset(&frame, 0, sizeof(frame));
		loading_ring_frame_swap(&art->ring, &frame);
		pixels = loading_ring_draw(&art->ring, width, height, 0.0f, NULL, (float)width * 0.5f);
		if (!pixels)
		{
			loading_ring_frame_swap(&art->ring, &frame);
			loading_ring_frame_free(&frame);
			return 0;
		}
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
			{
				unsigned int pixel = y * width + x, value = pixels[pixel];
				int level = (int)art->tone[loading_art_luminance(value & 255, (value >> 8) & 255, (value >> 16) & 255)];
				int edge = 0, white = 0, light;
				unsigned int tint;
				if (x > 0 && y > 0 && x + 1 < width && y + 1 < height)
				{
					const uint32_t *p = pixels + pixel;
					int gx = (int)((p[1] >> 8) & 255) - (int)((p[-1] >> 8) & 255);
					int gy = (int)((p[width] >> 8) & 255) - (int)((p[-(int)width] >> 8) & 255);
					edge = (gx < 0 ? -gx : gx) + (gy < 0 ? -gy : gy);
				}
				tint = loading_art_tint(value, edge, &level, &white);
				memcpy(hd->scene + pixel * 3, art->tints[tint][level + edge > 255 ? 255 : level + edge], 3);
				light = (level * LOADING_ART_FEED_TONE + edge * LOADING_ART_FEED_EDGE) / 16;
				hd->light[pixel] = (uint16_t)(light < 65535 ? light : 65535);
				hd->edge[pixel] = (uint8_t)(edge < 192 ? edge : 192);
				hd->tint[pixel] = (uint8_t)(tint | (white ? LOADING_ART_TINT_WHITE : 0u) |
					(art->ring.depth[pixel] > 0.0f ? 0u : LOADING_ART_TINT_SKY));
			}
		/* (the small frame's buffers back; the sharp one's given up) */
		loading_ring_frame_swap(&art->ring, &frame);
		loading_ring_frame_free(&frame);
		/* the words: the picture's own, sampled at the sharp pixels in the
		box they are drawn in (and a pixel about it) */
		if (art->text_valid)
		{
			unsigned int left = (unsigned int)((float)(LOADING_ART_TEXT_LEFT + middle) / across) - 1;
			unsigned int right = (unsigned int)((float)(LOADING_ART_TEXT_RIGHT + middle) / across) + 2;
			unsigned int top = (unsigned int)((float)LOADING_ART_TEXT_TOP / down) - 1;
			unsigned int bottom = (unsigned int)((float)LOADING_ART_TEXT_BOTTOM / down) + 2;
			uint8_t *word;
			right = right < width ? right : width;
			bottom = bottom < height ? bottom : height;
			free(hd->word);
			hd->word = word = malloc((size_t)(right - left) * (bottom - top) * 4);
			if (word)
			{
				hd->word_left = left;
				hd->word_top = top;
				hd->word_right = right;
				hd->word_bottom = bottom;
				for (y = top; y < bottom; y++)
					for (x = left; x < right; x++, word += 4)
					{
						float px = ((float)x + 0.5f) * across - 0.5f - (float)middle;
						float py = ((float)y + 0.5f) * down - 0.5f;
						unsigned int pixel = y * width + x, blue, channel;
						word[0] = 0;
						if (px < (float)LOADING_ART_TEXT_LEFT - 0.5f || px > (float)LOADING_ART_TEXT_RIGHT - 0.5f ||
							py < (float)LOADING_ART_TEXT_TOP - 0.5f || py > (float)LOADING_ART_TEXT_BOTTOM - 0.5f)
							continue;
						loading_art_picture_sample(art->picture, px, py, word + 1);
						blue = word[3];
						word[0] = (uint8_t)(blue <= 130 ? 0 : blue >= 210 ? 255 : (blue - 130) * 255 / 80);
						for (channel = 0; channel < 3; channel++)
							hd->scene[pixel * 3 + channel] = (uint8_t)(hd->scene[pixel * 3 + channel] +
								(word[1 + channel] - hd->scene[pixel * 3 + channel]) * word[0] / 255);
					}
			}
		}
	}
	else if (art->valid)
	{
		/* (the picture covering the frame, its middle in the frame's: as
		wide as the frame, as loading_art_color has it) */
		float step = (float)LOADING_ART_WIDTH / (float)width;
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
				loading_art_picture_sample(art->picture, ((float)x + 0.5f) * step - 0.5f,
					(float)LOADING_ART_HEIGHT * 0.5f + ((float)y + 0.5f - (float)height * 0.5f) * step - 0.5f,
					hd->scene + (y * width + x) * 3);
	}
	else
		return 0;
	hd->built = 1;
	return 1;
}

/* One of the 480 rows' trail between the last two steps (`fraction` of 256
 * toward the last) and its glint, scaled across to the sharp frame's
 * columns (of 256). */
static inline const uint16_t *loading_art_hd_row(struct loading_art_hd_band *band, unsigned int row)
{
	struct loading_art *art = band->art;
	struct loading_art_hd *hd = &art->hd;
	unsigned int fraction = hd->fraction, width = hd->width, slot, x;
	const uint8_t *previous = hd->trail[art->current ^ 1] + (size_t)row * art->width * 3;
	const uint8_t *current = hd->trail[art->current] + (size_t)row * art->width * 3;
	const uint8_t *glint = art->ring.valid && art->ring.glint && art->ring.width == art->width ?
		art->ring.glint + (size_t)row * art->width : NULL;
	uint8_t *mix = band->mix;
	uint16_t *sums;
	for (slot = 0; slot < 2; slot++)
		if (band->row_tags[slot] == (int)row + 1)
			return band->rows[slot];
	/* (the rows are asked for in order: the earlier one goes) */
	slot = band->row_tags[0] <= band->row_tags[1] ? 0 : 1;
	band->row_tags[slot] = (int)row + 1;
	sums = band->rows[slot];
	for (x = 0; x < art->width * 3; x++)
		mix[x] = (uint8_t)((previous[x] * (256 - fraction) + current[x] * fraction + 128) >> 8);
	for (x = 0; x < width; x++)
	{
		unsigned int c0 = hd->column[x], c1 = c0 + 1 < art->width ? c0 + 1 : c0, f = hd->column_fraction[x];
		const uint8_t *a = mix + c0 * 3, *b = mix + c1 * 3;
		sums[x * 4] = (uint16_t)(a[0] * (256 - f) + b[0] * f);
		sums[x * 4 + 1] = (uint16_t)(a[1] * (256 - f) + b[1] * f);
		sums[x * 4 + 2] = (uint16_t)(a[2] * (256 - f) + b[2] * f);
		sums[x * 4 + 3] = (uint16_t)(glint ? glint[c0] * (256 - f) + glint[c1] * f : 0);
	}
	return sums;
}

/* The sharp frame's rows of a band (loading_art_hd_compose's). */
static inline void loading_art_hd_rows(struct loading_art_hd_band *band)
{
	struct loading_art *art = band->art;
	struct loading_art_hd *hd = &art->hd;
	unsigned int width = hd->width, height = hd->height, x, y;
	unsigned int light_scale = hd->light_scale, base = hd->base, completion = hd->completion;
	int scene = art->ring.valid;
	band->row_tags[0] = band->row_tags[1] = 0;
	for (y = band->first; y < band->last; y++)
	{
		/* (the 480 rows' row of the sharp row's middle, in 256ths) */
		int source = (int)(((2 * y + 1) * (uint64_t)LOADING_ART_FRAME_HEIGHT * 128) / height) - 128;
		unsigned int r0, r1, fy, iy, mfy;
		const uint16_t *upper, *lower, *mask_upper, *mask_lower;
		uint8_t *out = hd->output + (size_t)y * width * 4;
		const uint8_t *still = hd->scene + (size_t)y * width * 3;
		const uint16_t *light = hd->light + (size_t)y * width;
		const uint8_t *edge = hd->edge + (size_t)y * width, *tint = hd->tint + (size_t)y * width;
		const uint8_t *word = hd->word && y >= hd->word_top && y < hd->word_bottom ?
			hd->word + (size_t)(y - hd->word_top) * (hd->word_right - hd->word_left) * 4 : NULL;
		int mask_row;
		if (source < 0)
			source = 0;
		r0 = (unsigned int)source >> 8;
		fy = (unsigned int)source & 255;
		if (r0 >= LOADING_ART_FRAME_HEIGHT - 1)
		{
			r0 = LOADING_ART_FRAME_HEIGHT - 1;
			fy = 0;
		}
		r1 = r0 + (r0 < LOADING_ART_FRAME_HEIGHT - 1);
		upper = loading_art_hd_row(band, r0);
		lower = loading_art_hd_row(band, r1);
		/* the mask's row: its 16 over the frame's height (in 256ths) */
		mask_row = (int)(((2 * y + 1) * (uint64_t)16 * 128) / height) - 128;
		if (mask_row < 0)
			mask_row = 0;
		if (mask_row > 15 * 256)
			mask_row = 15 * 256;
		iy = (unsigned int)mask_row >> 8;
		mfy = (unsigned int)mask_row & 255;
		mask_upper = hd->mask_rows + (size_t)iy * width;
		mask_lower = hd->mask_rows + (size_t)(iy < 15 ? iy + 1 : 15) * width;
		for (x = 0; x < width; x++, out += 4, still += 3)
		{
			unsigned int mask = (mask_upper[x] * (256 - mfy) + mask_lower[x] * mfy + 32768) >> 16;
			unsigned int scale = (light_scale * mask + 128) >> 8;
			unsigned int r = (upper[x * 4] * (256 - fy) + lower[x * 4] * fy + 32768) >> 16;
			unsigned int g = (upper[x * 4 + 1] * (256 - fy) + lower[x * 4 + 1] * fy + 32768) >> 16;
			unsigned int b = (upper[x * 4 + 2] * (256 - fy) + lower[x * 4 + 2] * fy + 32768) >> 16;
			if (scene)
			{
				unsigned int glint = (upper[x * 4 + 3] * (256 - fy) + lower[x * 4 + 3] * fy + 32768) >> 16;
				unsigned int hue = tint[x] & LOADING_ART_TINT_HUE, white = tint[x] >> 7, limit;
				/* (the planet behind the ring more than the ring between
				the sweeps) */
				unsigned int shown = tint[x] & LOADING_ART_TINT_SKY ? base * LOADING_ART_SKY_BASE / LOADING_ART_SCENE_BASE : base;
				int lit = (int)light[x] + (int)(glint * (64 + edge[x]) * LOADING_ART_FEED_GLINT / 2048);
				const uint8_t *feed;
				uint8_t worded[3];
				if (!white && lit > LOADING_ART_FEED_KNEE)
				{
					lit = LOADING_ART_FEED_KNEE + (lit - LOADING_ART_FEED_KNEE) / 3;
					if (lit > LOADING_ART_FEED_LIMIT)
						lit = LOADING_ART_FEED_LIMIT;
				}
				feed = art->tints[hue][lit > 255 ? 255 : lit];
				if (word && x >= hd->word_left && x < hd->word_right && word[(x - hd->word_left) * 4])
				{
					const uint8_t *w = word + (x - hd->word_left) * 4;
					unsigned int channel;
					for (channel = 0; channel < 3; channel++)
						worded[channel] = (uint8_t)(feed[channel] + (w[1 + channel] - feed[channel]) * w[0] / 255);
					feed = worded;
				}
				r += (feed[0] * scale + 32512) / 65025 + (still[0] * shown >> 16);
				g += (feed[1] * scale + 32512) / 65025 + (still[1] * shown >> 16);
				b += (feed[2] * scale + 32512) / 65025 + (still[2] * shown >> 16);
				r = r < 255 ? r : 255;
				g = g < 255 ? g : 255;
				b = b < 255 ? b : 255;
				/* (kept in the picture's blues, as loading_art_compose's) */
				limit = b * LOADING_ART_GREY_RED >> 8;
				r = r < limit ? r : limit;
				limit = b * LOADING_ART_GREY_GREEN >> 8;
				g = g < limit ? g : limit;
			}
			else
			{
				r += (still[0] * scale + 32512) / 65025;
				g += (still[1] * scale + 32512) / 65025;
				b += (still[2] * scale + 32512) / 65025;
				r = r < 255 ? r : 255;
				g = g < 255 ? g : 255;
				b = b < 255 ? b : 255;
			}
			if (completion < 256)
			{
				r = r * completion >> 8;
				g = g * completion >> 8;
				b = b * completion >> 8;
			}
			out[0] = (uint8_t)r;
			out[1] = (uint8_t)g;
			out[2] = (uint8_t)b;
			out[3] = 255;
		}
	}
}

/* A band's part of a job. */
static inline void loading_art_hd_band_run(struct loading_art_hd_band *band, unsigned int job)
{
	if (job == 1)
		loading_art_step_rows(band->art, band->art->hd.phase, band->first, band->last, band->step_rows,
			band->step_tags, &band->step_next);
	else
		loading_art_hd_rows(band);
}

/* The sharp frame's workers: each draws a band of its rows as a frame is
 * composed, the presenter's thread the first. */
static inline void *loading_art_hd_worker(void *data)
{
	struct loading_art_hd_band *band = data;
	struct loading_art_hd_pool *pool = band->pool;
	unsigned int done = 0;
	pthread_mutex_lock(&pool->lock);
	for (;;)
	{
		while (pool->generation == done && !pool->quit)
			pthread_cond_wait(&pool->wake, &pool->lock);
		if (pool->quit)
			break;
		done = pool->generation;
		pthread_mutex_unlock(&pool->lock);
		loading_art_hd_band_run(band, pool->job);
		pthread_mutex_lock(&pool->lock);
		if (!--pool->pending)
			pthread_cond_broadcast(&pool->finished);
	}
	pthread_mutex_unlock(&pool->lock);
	return NULL;
}

/* What the sharp frame shows: the 480 rows' trail between the last two
 * steps (`fraction` of 256 toward the last), scaled up; the sweep's light,
 * at the sharp pixels, where it is between them (the step's band, at its
 * brightness, the ring etched and lit by the sweep's glint as the 480
 * rows' feed is); the scene dimly between the sweeps (`base`, of 65536,
 * as loading_art_compose's); as much of it as `completion` (of 256) has
 * left as the load completes. */
static inline void loading_art_hd_compose(struct loading_art *art, unsigned int fraction, unsigned int base,
	unsigned int completion)
{
	struct loading_art_hd *hd = &art->hd;
	unsigned int width = hd->width, x, row;
	float phase = art->follow ? art->phases[0] + (art->phases[1] - art->phases[0]) * (float)fraction / 256.0f :
		(float)(art->step % 240) - 1.0f + (float)fraction / 256.0f, shift, brightness;
	/* (the 480 rows' pixels a sharp pixel is, across) */
	float across = (float)art->width / (float)width;
	/* the sweep's brightness at the phase (between the steps': nothing
	before the first) */
	if (phase < 0.0f)
		brightness = (phase + 1.0f) * (float)loading_art_brightness[0];
	else
	{
		unsigned int whole = (unsigned int)phase;
		float part = phase - (float)whole;
		unsigned int next = whole + 1 < 240 ? whole + 1 : whole;
		brightness = (float)loading_art_brightness[whole] * (1.0f - part) + (float)loading_art_brightness[next] * part;
	}
	hd->fraction = fraction;
	hd->base = base;
	hd->completion = completion;
	hd->light_scale = (unsigned int)(brightness * 256.0f + 0.5f);
	/* its mask, scaled across each of its 16 rows (of 256): the band, 128
	of the 480 rows' pixels wide, crossing from 128 left of the frame past
	its right edge, as loading_art_step's */
	shift = 128.0f - phase * (float)(art->width + 128) / 240.0f;
	hd->shift = shift;
	for (x = 0; x < width; x++)
	{
		float column = ((float)x + 0.5f) * across - 0.5f + shift, whole = floorf(column);
		int ix = (int)(whole < -1.0f ? -1.0f : whole > 128.0f ? 128.0f : whole);
		unsigned int fx = (unsigned int)((column - whole) * 256.0f);
		for (row = 0; row < 16; row++)
			hd->mask_rows[row * width + x] = (uint16_t)(loading_art_mask(ix, row) * (256 - fx) +
				loading_art_mask(ix + 1, row) * fx);
	}
	/* the rows, in the threads' bands */
	loading_art_hd_run(art, 0, hd->height);
}

/* A job's rows `rows` in bands, a worker's each, the calling thread's the
 * first; done when it returns. */
static inline void loading_art_hd_run(struct loading_art *art, unsigned int job, unsigned int rows)
{
	struct loading_art_hd_pool *pool = art->hd.pool;
	unsigned int bands = pool->workers + 1, band;
	for (band = 0; band < bands; band++)
	{
		pool->bands[band].art = art;
		pool->bands[band].first = rows * band / bands;
		pool->bands[band].last = rows * (band + 1) / bands;
	}
	if (pool->workers)
	{
		pthread_mutex_lock(&pool->lock);
		pool->job = job;
		pool->pending = pool->workers;
		pool->generation++;
		pthread_cond_broadcast(&pool->wake);
		pthread_mutex_unlock(&pool->lock);
	}
	loading_art_hd_band_run(&pool->bands[0], job);
	if (pool->workers)
	{
		pthread_mutex_lock(&pool->lock);
		while (pool->pending)
			pthread_cond_wait(&pool->finished, &pool->lock);
		pthread_mutex_unlock(&pool->lock);
	}
}

/* The size of the picture the next render will give, as loading_art_width
 * last set it (the sharp frame's, or the 480 rows'): what a presenter
 * makes its image for. */
static inline unsigned int loading_art_frame_width(const struct loading_art *art)
{
	return art->hd.width ? art->hd.width : art->width;
}

static inline unsigned int loading_art_frame_height(const struct loading_art *art)
{
	return art->hd.width ? art->hd.height : LOADING_ART_FRAME_HEIGHT;
}

/* The size of the picture the last render gave (the sharp frame's, or the
 * 480 rows'): what a presenter uploads and fills the screen with. */
static inline unsigned int loading_art_output_width(const struct loading_art *art)
{
	return art->output_width ? art->output_width : art->width;
}

static inline unsigned int loading_art_output_height(const struct loading_art *art)
{
	return art->output_height ? art->output_height : LOADING_ART_FRAME_HEIGHT;
}

static inline const uint8_t *loading_art_render(struct loading_art *art,
	const struct loading_presentation_frame *frame)
{
	uint64_t target;
	unsigned int reset, skipped, fraction, base, completion, stepped;
	unsigned int stereo = art->stereo && art->ring.valid;
	if (!art->valid && !art->ring.valid)
		return NULL;
	if (art->width < LOADING_ART_WIDTH || art->width > LOADING_ART_FRAME_MAXIMUM_WIDTH)
		loading_art_width(art, LOADING_ART_WIDTH, LOADING_ART_FRAME_HEIGHT);
	/* (the caption where the sweep lights it, laid out for this width) */
	loading_art_caption_layer(art);
	target = (frame->elapsed_ns / UINT64_C(1000000000)) * 60 +
		(frame->elapsed_ns % UINT64_C(1000000000)) * 60 / UINT64_C(1000000000);
	reset = !art->started || art->epoch != frame->epoch || target < art->step;
	skipped = art->started && target > art->step && target - art->step > 1;
	if (reset || (!art->follow && target != art->step && target / 240 != art->step / 240))
	{
		memset(art->history, 0, sizeof(art->history));
		if (art->hd.built)
		{
			memset(art->hd.trail[0], 0, LOADING_ART_FRAME_MAXIMUM_PIXELS * 3);
			memset(art->hd.trail[1], 0, LOADING_ART_FRAME_MAXIMUM_PIXELS * 3);
		}
	}
	if (reset)
	{
		art->tone_valid = 0;
		art->still_valid = 0;
		art->smoothed = 0.0f;
		/* (the sharp frame's still layers again: the scene's brightness is
		matched again) */
		art->hd.built = 0;
	}
	/* At most one feedback step, even after a producer or display stall.
	 * Elapsed time selects the sweep phase; no costly fast-forward loop. */
	stepped = reset || target != art->step;
	if (stepped)
	{
		float seconds = (float)((double)frame->elapsed_ns * 1e-9);
		unsigned int phase = (unsigned int)(target % 240);
		/* (following the progress: the band where the Xbox's was, its
		position the progress smoothed toward, a fifth of the way a step,
		with a wobble of a twentieth) */
		if (art->follow)
		{
			float told = art->progress_epoch == frame->epoch ? art->progress : 0.0f;
			art->wobble += 0.01f;
			art->smoothed += (sinf(art->wobble) * 0.05f + told - art->smoothed) * 0.2f;
			art->smoothed = art->smoothed < 0.0f ? 0.0f : art->smoothed > 1.0f ? 1.0f : art->smoothed;
			phase = (unsigned int)(art->smoothed * 240.0f);
			phase = phase < 239 ? phase : 239;
			art->phases[0] = reset ? art->smoothed * 240.0f : art->phases[1];
			art->phases[1] = art->smoothed * 240.0f;
		}
		/* (the scene every other step: its motion is slow, the sweep's is
		the feedback's, at every step) */
		if (art->ring.valid && (reset || skipped || !(target & 1)))
		{
			float eyes[2][3];
			if (stereo)
				loading_art_eyes(art, frame->elapsed_ns, eyes);
			loading_art_scene_draw(art, seconds, phase, stereo ? eyes[0] : NULL, art->scene, art->feed);
			if (stereo)
				loading_art_scene_draw(art, seconds, phase, eyes[1], art->scene_right, NULL);
		}
		/* (the sharp frame's still layers, once the scene's brightness is
		known; without the memory, the 480 rows are shown) */
		if (art->hd.width && !stereo && !art->hd.built)
		{
			if (loading_art_hd_build(art))
			{
				memset(art->hd.trail[0], 0, LOADING_ART_FRAME_MAXIMUM_PIXELS * 3);
				memset(art->hd.trail[1], 0, LOADING_ART_FRAME_MAXIMUM_PIXELS * 3);
			}
			else
				art->hd.width = art->hd.height = 0;
		}
		loading_art_step(art, phase);
		if (reset || skipped)
		{
			memcpy(art->history[art->current ^ 1], art->history[art->current],
				sizeof(art->history[0]));
			if (art->hd.built)
				memcpy(art->hd.trail[art->current ^ 1], art->hd.trail[art->current],
					LOADING_ART_FRAME_MAXIMUM_PIXELS * 3);
		}
	}
	art->epoch = frame->epoch;
	art->step = target;
	art->started = 1;
	completion = loading_art_completion(art, frame);
	/* (VR's pair, at the headset's rate, between the steps: the pictures
	 * of the step, as they were; the eyes' two pictures cost twice) */
	if (stereo && !stepped && art->composed && completion == 256)
		return art->output;
	art->composed = stereo;
	/* Interpolate the fixed-rate histories for each display refresh. This
	 * adds one visual step of delay without changing the feedback clock. */
	fraction = (unsigned int)(((frame->elapsed_ns % UINT64_C(1000000000)) * 60 %
		UINT64_C(1000000000)) * 256 / UINT64_C(1000000000));
	base = frame->elapsed_ns >= LOADING_ART_SCENE_FADE_NS ? LOADING_ART_SCENE_BASE * 256u :
		(unsigned int)(LOADING_ART_SCENE_BASE * 256u * frame->elapsed_ns / LOADING_ART_SCENE_FADE_NS);
	/* (for the caption at the screen's pixels, loading_art_caption_shown) */
	art->caption_fraction = fraction;
	art->caption_base = art->ring.valid ? base : 0;
	art->caption_completion = completion;
	/* (the sharp frame, where there is one) */
	if (art->hd.built && art->hd.width && !stereo)
	{
		loading_art_hd_compose(art, fraction, base, completion);
		art->output_width = art->hd.width;
		art->output_height = art->hd.height;
		return art->hd.output;
	}
	art->output_width = art->width;
	art->output_height = LOADING_ART_FRAME_HEIGHT;
	loading_art_compose(art, art->history[art->current ^ 1], art->history[art->current], fraction,
		art->ring.valid ? art->scene : NULL, base, art->output);
	if (stereo)
		loading_art_compose(art, art->history[art->current ^ 1], art->history[art->current], fraction,
			art->scene_right, base, art->output_right);
	if (completion < 256)
	{
		unsigned int pixel, channel;
		for (pixel = 0; pixel < art->width * LOADING_ART_FRAME_HEIGHT; pixel++)
			for (channel = 0; channel < 3; channel++)
			{
				art->output[pixel * 4 + channel] = (uint8_t)(art->output[pixel * 4 + channel] * completion >> 8);
				if (stereo)
					art->output_right[pixel * 4 + channel] =
						(uint8_t)(art->output_right[pixel * 4 + channel] * completion >> 8);
			}
	}
	/* (the caption, under the words: in VR's frame, as they are; else at
	the screen's pixels, the presenter's, loading_art_caption_shown) */
	return art->output;
}

/* A place in a frame `size` pixels across shown `shown` pixels across,
 * from a shown pixel's middle: the frame's pixel there, the next, and the
 * second's share (of 256), as a bilinear filter reads it (from the frame's
 * pixels' middles, clamped at its edges). */
static inline void loading_art_caption_sample(unsigned int pixel, unsigned int shown, unsigned int size,
	unsigned int *first, unsigned int *second, unsigned int *share)
{
	int64_t place = ((int64_t)(2 * pixel + 1) * size * 128) / shown - 128;
	int64_t last = ((int64_t)size - 1) * 256;
	place = place < 0 ? 0 : place > last ? last : place;
	*first = (unsigned int)(place >> 8);
	*share = (unsigned int)(place & 255);
	*second = *first + 1 < size ? *first + 1 : *first;
}

/* A history's or trail's colour at a place of the 480 rows (`stride`
 * bytes a pixel), between its pixels, from x0, y0 toward the next by fx, fy
 * (of 256). */
static inline unsigned int loading_art_caption_bilinear(const uint8_t *image, unsigned int width, unsigned int stride,
	unsigned int x0, unsigned int x1, unsigned int y0, unsigned int y1, unsigned int fx, unsigned int fy,
	unsigned int channel)
{
	unsigned int upper = image[((size_t)y0 * width + x0) * stride + channel] * (256 - fx) +
		image[((size_t)y0 * width + x1) * stride + channel] * fx;
	unsigned int lower = image[((size_t)y1 * width + x0) * stride + channel] * (256 - fx) +
		image[((size_t)y1 * width + x1) * stride + channel] * fx;
	return (upper * (256 - fy) + lower * fy + 32768) >> 16;
}

/* The words' pixel at a place of the last render's frame (`x`, `y` in the
 * 480 rows' pixels, their middles at whole numbers) were the words there,
 * coloured `word`: lit as loading_art_hd_rows lights the words, the 480
 * rows' trail there (between the last two steps), the sweep's light on
 * them (its brightness and its band's mask at the place), the word dimly
 * between the sweeps (with a scene, `caption_base`), kept in the picture's
 * blues, as much as is left as the load completes. Without the sharp
 * frame, as loading_art_compose shows them: the history there (the sweep's
 * light in it already) and the word dimly between the sweeps. */
static inline void loading_art_caption_lit(const struct loading_art *art, int sharp, float x, float y,
	const uint8_t *word, uint8_t *rgb)
{
	unsigned int width = art->width, x0, x1, y0, y1, fx, fy, channel, value[3], scale = 0;
	unsigned int fraction = art->caption_fraction, completion = art->caption_completion;
	x = x < 0.0f ? 0.0f : x > (float)(width - 1) ? (float)(width - 1) : x;
	y = y < 0.0f ? 0.0f : y > (float)(LOADING_ART_FRAME_HEIGHT - 1) ? (float)(LOADING_ART_FRAME_HEIGHT - 1) : y;
	x0 = (unsigned int)x;
	y0 = (unsigned int)y;
	fx = (unsigned int)((x - (float)x0) * 256.0f);
	fy = (unsigned int)((y - (float)y0) * 256.0f);
	x1 = x0 + 1 < width ? x0 + 1 : x0;
	y1 = y0 + 1 < LOADING_ART_FRAME_HEIGHT ? y0 + 1 : y0;
	if (sharp)
	{
		/* the sweep's mask at the place (loading_art_hd_compose's and
		loading_art_hd_rows'), its brightness */
		float column = x + art->hd.shift, whole = floorf(column);
		int ix = (int)(whole < -1.0f ? -1.0f : whole > 128.0f ? 128.0f : whole);
		unsigned int across = (unsigned int)((column - whole) * 256.0f), iy, down, upper, lower, mask;
		int row = (int)((y + 0.5f) * 16.0f * 256.0f / (float)LOADING_ART_FRAME_HEIGHT) - 128;
		row = row < 0 ? 0 : row > 15 * 256 ? 15 * 256 : row;
		iy = (unsigned int)row >> 8;
		down = (unsigned int)row & 255;
		upper = loading_art_mask(ix, iy) * (256 - across) + loading_art_mask(ix + 1, iy) * across;
		lower = loading_art_mask(ix, iy < 15 ? iy + 1 : 15) * (256 - across) +
			loading_art_mask(ix + 1, iy < 15 ? iy + 1 : 15) * across;
		mask = (upper * (256 - down) + lower * down + 32768) >> 16;
		scale = (art->hd.light_scale * mask + 128) >> 8;
	}
	for (channel = 0; channel < 3; channel++)
	{
		unsigned int before, after;
		if (sharp)
		{
			before = loading_art_caption_bilinear(art->hd.trail[art->current ^ 1], width, 3, x0, x1, y0, y1, fx, fy, channel);
			after = loading_art_caption_bilinear(art->hd.trail[art->current], width, 3, x0, x1, y0, y1, fx, fy, channel);
		}
		else
		{
			before = loading_art_caption_bilinear(art->history[art->current ^ 1], width, 4, x0, x1, y0, y1, fx, fy, channel);
			after = loading_art_caption_bilinear(art->history[art->current], width, 4, x0, x1, y0, y1, fx, fy, channel);
		}
		value[channel] = (before * (256 - fraction) + after * fraction + 128) >> 8;
		if (sharp)
			value[channel] += (word[channel] * scale + 32512) / 65025;
		value[channel] += word[channel] * art->caption_base >> 16;
		value[channel] = value[channel] < 255 ? value[channel] : 255;
	}
	/* (kept in the picture's blues, with a scene, as the words are) */
	if (art->ring.valid)
	{
		unsigned int limit = value[2] * LOADING_ART_GREY_RED >> 8;
		value[0] = value[0] < limit ? value[0] : limit;
		limit = value[2] * LOADING_ART_GREY_GREEN >> 8;
		value[1] = value[1] < limit ? value[1] : limit;
	}
	for (channel = 0; channel < 3; channel++)
		rgb[channel] = (uint8_t)(completion < 256 ? value[channel] * completion >> 8 : value[channel]);
}

/* The caption at the screen's own pixels, for a presenter that shows the
 * last render's frame (`frame`, loading_art_output_width by _height) scaled
 * to `shown_width` by `shown_height` pixels with a bilinear filter: its box
 * in those pixels, from the shown frame's top left, and its pixels (RGBA,
 * `*width` by `*height`), the frame's as the filter shows them, and where
 * the caption's letters are, the words' light (loading_art_caption_lit) at
 * the screen's pixels, for the presenter to draw over the frame as they
 * are. The frame has the caption's glow already: the sweep lit it in the
 * 480 rows (loading_art_caption_layer). NULL: no caption, or no memory for
 * it (VR's pair has it in the frame). */
static inline const uint8_t *loading_art_caption_shown(struct loading_art *art, const uint8_t *frame,
	unsigned int shown_width, unsigned int shown_height, unsigned int *left, unsigned int *top,
	unsigned int *width, unsigned int *height)
{
	unsigned int frame_width = loading_art_output_width(art), frame_height = loading_art_output_height(art);
	unsigned int x, y, drawn_height;
	int sharp;
	size_t bytes;
	*left = *top = *width = *height = 0;
	if (!frame || art->stereo || !shown_width || !shown_height || !frame_width || !frame_height ||
		!loading_art_caption_scaled(art, shown_height) || art->caption_shown_width > shown_width)
		return NULL;
	/* (its colours, and the frame the render drew: the sharp one's, or the
	480 rows') */
	loading_art_caption_layer(art);
	sharp = art->hd.built && art->hd.width && frame_width == art->hd.width && frame_height == art->hd.height;
	*left = (shown_width - art->caption_shown_width) / 2;
	*top = (unsigned int)((uint64_t)LOADING_ART_CAPTION_TOP * shown_height / LOADING_ART_FRAME_HEIGHT);
	drawn_height = shown_height - *top < art->caption_shown_height ? shown_height - *top : art->caption_shown_height;
	if (!drawn_height)
		return NULL;
	bytes = (size_t)art->caption_shown_width * drawn_height * 4;
	if (bytes > art->caption_tile_capacity)
	{
		free(art->caption_tile);
		art->caption_tile = malloc(bytes);
		art->caption_tile_capacity = art->caption_tile ? bytes : 0;
		if (!art->caption_tile)
			return NULL;
	}
	for (y = 0; y < drawn_height; y++)
	{
		unsigned int row0, row1, down;
		const uint8_t *upper, *lower;
		/* (the 480 rows' place of the screen's row, and the words' colour
		there) */
		float place_y = ((float)(*top + y) + 0.5f) * (float)LOADING_ART_FRAME_HEIGHT / (float)shown_height - 0.5f;
		uint8_t word[3];
		loading_art_caption_rgb(art, place_y + 0.5f, word);
		loading_art_caption_sample(*top + y, shown_height, frame_height, &row0, &row1, &down);
		upper = frame + (size_t)row0 * frame_width * 4;
		lower = frame + (size_t)row1 * frame_width * 4;
		for (x = 0; x < art->caption_shown_width; x++)
		{
			uint8_t *out = art->caption_tile + ((size_t)y * art->caption_shown_width + x) * 4;
			unsigned int column0, column1, across, channel, coverage;
			loading_art_caption_sample(*left + x, shown_width, frame_width, &column0, &column1, &across);
			for (channel = 0; channel < 3; channel++)
			{
				unsigned int above = upper[column0 * 4 + channel] * (256 - across) + upper[column1 * 4 + channel] * across;
				unsigned int below = lower[column0 * 4 + channel] * (256 - across) + lower[column1 * 4 + channel] * across;
				out[channel] = (uint8_t)((above * (256 - down) + below * down + 32768) >> 16);
			}
			out[3] = 255;
			coverage = art->caption_coverage[y * art->caption_shown_width + x];
			if (coverage)
			{
				uint8_t lit[3];
				float place_x = ((float)(*left + x) + 0.5f) * (float)art->width / (float)shown_width - 0.5f;
				loading_art_caption_lit(art, sharp, place_x, place_y, word, lit);
				for (channel = 0; channel < 3; channel++)
					out[channel] = (uint8_t)(out[channel] + ((int)lit[channel] - (int)out[channel]) * (int)coverage / 255);
			}
		}
	}
	*width = art->caption_shown_width;
	*height = drawn_height;
	return art->caption_tile;
}

#endif
