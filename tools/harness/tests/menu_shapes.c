/*
MENU_SHAPES.C (test)

The shapes the menus draw at the display's resolution (port/linux/game/
menu_shapes.c): a rounded box (the key caps, issue 596) and a picture with
its corners cut round (the campaign's mission pictures, issue 687), as the
quadrilaterals they are drawn as. Nothing of a box is past its sides: its
corners' blended edge is inside the radius, so that a corner meets its
straight sides, not a pixel past them; a picture's texture is laid over the
whole of it, its corners' quadrilaterals too, each point the texture's
where it is. The quadrilaterals are noted, not drawn. test_menu_shapes.py
takes the code.
*/

#include "harness.h"

#include <math.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MENU_SHAPE_PIXEL_ORIGIN 0.5f

typedef unsigned long pixel32;
struct bitmap_data { int unused; };

static real pixels_per_unit = 2.25f;
static real menu_shape_pixels(void) { return pixels_per_unit; }

/* what was drawn: each quadrilateral's corners, their texture's
coordinates (white's: 0), their colours' alpha */
#define MAXIMUM_QUADS 4096
static struct
{
	real x[4], y[4], u[4], v[4];
	unsigned alpha[4];
	int textured;
} quads[MAXIMUM_QUADS];
static int quad_count;

struct menu_shape_texture;
static void menu_shape_quad(struct bitmap_data *white, struct menu_shape_texture const *texture, real pixels,
	real const x[4], real const y[4], pixel32 const color[4]);

#include "under_test.inc"

static void menu_shape_quad(struct bitmap_data *white, struct menu_shape_texture const *texture, real pixels,
	real const x[4], real const y[4], pixel32 const color[4])
{
	int index;

	(void)white;
	(void)pixels;
	CHECK(quad_count < MAXIMUM_QUADS, "too many quadrilaterals");
	for (index = 0; index < 4; index++)
	{
		quads[quad_count].x[index] = x[index];
		quads[quad_count].y[index] = y[index];
		quads[quad_count].u[index] = !texture ? 0.0f :
			texture->u0 + (x[index] - texture->x0) * (texture->u1 - texture->u0) / (texture->x1 - texture->x0);
		quads[quad_count].v[index] = !texture ? 0.0f :
			texture->v0 + (y[index] - texture->y0) * (texture->v1 - texture->v0) / (texture->y1 - texture->y0);
		quads[quad_count].alpha[index] = (unsigned)(color[index] >> 24);
	}
	quads[quad_count].textured = texture != NULL;
	quad_count++;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	static struct bitmap_data white, picture;

	CASE("inside")
	{
		/* every point of a box, filled, edged or both, at any radius, inside
		its sides */
		static real const radii[] = { 0.0f, 2.0f, 3.0f, 5.0f, 8.0f, 16.0f };
		static real const edges[] = { 0.0f, 1.0f, 2.0f, 3.0f };
		unsigned long radius, edge;

		for (radius = 0; radius < sizeof(radii) / sizeof(radii[0]); radius++)
			for (edge = 0; edge < sizeof(edges) / sizeof(edges[0]); edge++)
			{
				int quad, corner;

				quad_count = 0;
				menu_shape_rounded(&white, NULL, 2.25f, 100.0f, 50.0f, 160.0f, 80.0f, radii[radius], edges[edge],
					0x4C2896FFUL, 0xFF2896FFUL);
				CHECK(quad_count > 0, "radius %g, edge %g: nothing drawn", radii[radius], edges[edge]);
				for (quad = 0; quad < quad_count; quad++)
					for (corner = 0; corner < 4; corner++)
					{
						CHECK(quads[quad].x[corner] >= 100.0f - 0.001f && quads[quad].x[corner] <= 160.0f + 0.001f &&
							quads[quad].y[corner] >= 50.0f - 0.001f && quads[quad].y[corner] <= 80.0f + 0.001f,
							"radius %g, edge %g: a point at %g, %g, past the box's sides", radii[radius], edges[edge],
							quads[quad].x[corner], quads[quad].y[corner]);
					}
			}
	}
	CASE("texture")
	{
		/* a picture's texture over the whole of it, its corners too: each
		point's coordinates where it is, from (u0, v0) at the top left to
		(u1, v1) at the bottom right */
		int quad, corner;
		real x0, y0, x1, y1;

		quad_count = 0;
		menu_shape_rounded_picture(&picture, 20.0f, 30.0f, 64.0f, 66.0f, 0.0f, 0.0f, 0.5f, 0.9f, 5.0f, 0xFFFFFFFFUL);
		x0 = menu_shape_pixel_edge(20.0f, pixels_per_unit);
		y0 = menu_shape_pixel_edge(30.0f, pixels_per_unit);
		x1 = menu_shape_pixel_edge(64.0f, pixels_per_unit);
		y1 = menu_shape_pixel_edge(66.0f, pixels_per_unit);
		CHECK(quad_count > 4, "the picture drawn as %d quadrilaterals: its corners not cut", quad_count);
		for (quad = 0; quad < quad_count; quad++)
		{
			CHECK(quads[quad].textured, "quadrilateral %d of the picture drawn white", quad);
			for (corner = 0; corner < 4; corner++)
			{
				real u = (quads[quad].x[corner] - x0) / (x1 - x0) * 0.5f;
				real v = (quads[quad].y[corner] - y0) / (y1 - y0) * 0.9f;

				CHECK(fabsf(quads[quad].u[corner] - u) < 0.0001f && fabsf(quads[quad].v[corner] - v) < 0.0001f,
					"a point at %g, %g textured from %g, %g, not %g, %g", quads[quad].x[corner],
					quads[quad].y[corner], quads[quad].u[corner], quads[quad].v[corner], u, v);
				CHECK(quads[quad].x[corner] >= x0 - 0.001f && quads[quad].x[corner] <= x1 + 0.001f &&
					quads[quad].y[corner] >= y0 - 0.001f && quads[quad].y[corner] <= y1 + 0.001f,
					"a point at %g, %g past the picture", quads[quad].x[corner], quads[quad].y[corner]);
			}
		}
	}
	CASE("cut")
	{
		/* the picture's corners cut: none of it at its very corners, its
		curves' last pixel blended to nothing */
		int quad, corner, faded = 0;
		real x0, y0;

		quad_count = 0;
		menu_shape_rounded_picture(&picture, 20.0f, 30.0f, 64.0f, 66.0f, 0.0f, 0.0f, 0.5f, 0.9f, 5.0f, 0xFFFFFFFFUL);
		x0 = menu_shape_pixel_edge(20.0f, pixels_per_unit);
		y0 = menu_shape_pixel_edge(30.0f, pixels_per_unit);
		for (quad = 0; quad < quad_count; quad++)
			for (corner = 0; corner < 4; corner++)
			{
				CHECK(!(fabsf(quads[quad].x[corner] - x0) < 0.5f && fabsf(quads[quad].y[corner] - y0) < 0.5f),
					"a point of the picture at its top left corner");
				faded += quads[quad].alpha[corner] == 0;
			}
		CHECK(faded > 0, "its curves' edges not blended");
	}
	return 0;
}
