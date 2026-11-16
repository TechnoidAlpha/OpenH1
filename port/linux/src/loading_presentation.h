/* Loading art owns no map data. Every backend draws these same rectangles;
   animation reads presentation time only, never the simulation clock. */
#ifndef LOADING_PRESENTATION_H
#define LOADING_PRESENTATION_H

#include <stdint.h>

#define LOADING_PRESENTATION_WIDTH 512
#define LOADING_PRESENTATION_HEIGHT 256
#define LOADING_PRESENTATION_RECTS 12
#define LOADING_PRESENTATION_RED 0.015f
#define LOADING_PRESENTATION_GREEN 0.03f
#define LOADING_PRESENTATION_BLUE 0.055f
/* VR: the menu's scene is a stereo pair on a window in the world, this many
   metres before the head as the load began, as wide as the menu's camera
   saw from there, 4:3; each eye's 640x480 picture side by side in the
   loading image, the left eye's first */
#define LOADING_PRESENTATION_VR_DISTANCE 8.0f
#define LOADING_PRESENTATION_VR_EYE_WIDTH 640
#define LOADING_PRESENTATION_VR_EYE_HEIGHT 480
/* the caption under the words (loading_screen.h) is set at this many times
   the 480 lines' size, and drawn at the screen's own pixels from it
   (loading_art_caption_shown): sharp up to 3840 rows */
#define LOADING_PRESENTATION_CAPTION_SCALE 8

struct loading_presentation_rect
{
	int32_t x, y, width, height;
	float color[4];
};

struct loading_presentation_frame
{
	uint64_t epoch, serial, started_ns, elapsed_ns;
	uint32_t active, ready, count;
	struct loading_presentation_rect rectangles[LOADING_PRESENTATION_RECTS];
};

static inline void loading_presentation_build(struct loading_presentation_frame *frame)
{
	float phase = (float)(frame->elapsed_ns % 1200000000ull) / 100000000.0f;
	uint32_t index;

	frame->count = LOADING_PRESENTATION_RECTS;
	for (index = 0; index < frame->count; index++)
	{
		struct loading_presentation_rect *rectangle = &frame->rectangles[index];
		float distance = phase - (float)index;
		float brightness;

		if (distance < 0.0f)
			distance += 12.0f;
		brightness = distance < 4.0f ? 1.0f - distance / 4.0f : 0.0f;
		rectangle->x = 112 + (int32_t)index * 24;
		rectangle->y = 124;
		rectangle->width = 16;
		rectangle->height = 8;
		rectangle->color[0] = 0.12f + brightness * 0.38f;
		rectangle->color[1] = 0.22f + brightness * 0.58f;
		rectangle->color[2] = 0.32f + brightness * 0.68f;
		rectangle->color[3] = 1.0f;
	}
}

#endif
