/*
MENU_SHAPES.C

Shapes the port draws at the display's resolution rather than from a
picture: a box with rounded corners, filled and edged (the prompts' key
caps, port/linux/game/hud_key_prompts.c; the name keyboard's panels and
keys, source/interface/virtual_keyboard.c; the keyboard's panels shaded
from one colour at the top to another at the bottom), a picture with its corners cut
round (a menu bitmap's radius: the campaign's mission pictures,
source/interface/ui_widget.c), a text field's caret (a bar about the
capitals, source/interface/ui_widget.c), and a band shaded from one colour
at its top to another at its bottom (the name keyboard's backdrop, as the
menus' bitmaps\gradient has it). Each is drawn as quadrilaterals of the
maps' white texture (the game globals' default textures), or of the
picture's, their corners in the display's pixels, so that it is as sharp
at any resolution as the high-res text drawn with it.

Only what this machine draws: nothing is sent.
*/

#include "cseries.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "game/game_globals.h"
#include "rasterizer/rasterizer.h"
#include "scenario/scenario.h"

#include <math.h>

/* port/linux/src/d3d8_device.c: the display's pixels for each of the 480
lines */
float halo_screen_pixel_scale(void);

/* ---------- prototypes */

void menu_shape_rounded_box_pixels(real x0, real y0, real x1, real y1, real radius, real edge, pixel32 fill,
	pixel32 color);
void menu_shape_rounded_box(real x0, real y0, real x1, real y1, real radius, real edge, pixel32 fill,
	pixel32 color);
void menu_shape_shaded_box(real x0, real y0, real x1, real y1, real radius, real edge, pixel32 top, pixel32 bottom,
	pixel32 color);
void menu_shape_rounded_picture(struct bitmap_data *bitmap, real x0, real y0, real x1, real y1, real u0, real v0,
	real u1, real v1, real radius, pixel32 color);
void menu_shape_band(real x0, real y0, real x1, real y1, pixel32 top, pixel32 bottom);
void menu_shape_caret(real x, real y0, real y1, real width, pixel32 color);
real menu_shape_pixels(void);

/* ---------- constants */

/* (where the screen's unit 0 falls, in its units: the Xbox's pixels'
centres, rasterizer_xbox_dynavobgeom.c) */
#define MENU_SHAPE_PIXEL_ORIGIN 0.5f

/* ---------- private code */

/* the maps' white texture, NULL where the map has none */
static struct bitmap_data *menu_shape_white(void)
{
	struct game_globals *game_globals = scenario_get_game_globals();
	struct game_globals_rasterizer_data *rasterizer_data;

	if (!game_globals || !game_globals->rasterizer_data.count)
		return NULL;
	rasterizer_data = TAG_BLOCK_GET_ELEMENT(&game_globals->rasterizer_data, 0, struct game_globals_rasterizer_data);
	return TAG_BLOCK_GET_ELEMENT(&bitmap_group_get(rasterizer_data->default_textures[0].index)->bitmaps, 1,
		struct bitmap_data);
}

/* a picture's texture laid over a shape: the texture's coordinates (u0, v0)
at the display's pixel x0, y0 and (u1, v1) at x1, y1, each corner's between
them where it falls (bitmap NULL, or the whole NULL: the maps' white
texture, its coordinates 0); and a shade over it, top at y0 to bottom at
y1, which each corner's colour is multiplied by (white for none) */
struct menu_shape_texture
{
	struct bitmap_data *bitmap;
	real x0, y0, x1, y1;
	real u0, v0, u1, v1;
	pixel32 top, bottom;
};

/* a colour times the texture's shade where y falls between its top and
bottom (each channel of the one by the other's, out of 255) */
static pixel32 menu_shape_shaded(struct menu_shape_texture const *texture, real y, pixel32 color)
{
	real along = (y - texture->y0) / (texture->y1 - texture->y0);
	pixel32 result = 0;
	short shift;

	along = along < 0.0f ? 0.0f : along > 1.0f ? 1.0f : along;
	for (shift = 0; shift < 32; shift += 8)
	{
		real top = (real)((texture->top >> shift) & 0xFF);
		real shade = top + ((real)((texture->bottom >> shift) & 0xFF) - top) * along;
		unsigned long channel = (unsigned long)(shade * (real)((color >> shift) & 0xFF) / 255.0f + 0.5f);

		result |= (pixel32)(MIN(channel, 0xFFUL) << shift);
	}
	return result;
}

/* a quadrilateral, its corners in the display's pixels and their colours,
the colours blended between them, of the white texture or a picture's */
static void menu_shape_quad(struct bitmap_data *white, struct menu_shape_texture const *texture, real pixels,
	real const x[4], real const y[4], pixel32 const color[4])
{
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	struct dynamic_screen_vertex vertices[4];
	short index;

	for (index = 0; index < 4; index++)
	{
		vertices[index].position.x = x[index] / pixels + MENU_SHAPE_PIXEL_ORIGIN;
		vertices[index].position.y = y[index] / pixels + MENU_SHAPE_PIXEL_ORIGIN;
		vertices[index].texture_coordinates.x = !texture || !texture->bitmap ? 0.0f :
			texture->u0 + (x[index] - texture->x0) * (texture->u1 - texture->u0) / (texture->x1 - texture->x0);
		vertices[index].texture_coordinates.y = !texture || !texture->bitmap ? 0.0f :
			texture->v0 + (y[index] - texture->y0) * (texture->v1 - texture->v0) / (texture->y1 - texture->y0);
		vertices[index].color = texture ? menu_shape_shaded(texture, y[index], color[index]) : color[index];
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map[0] = texture && texture->bitmap ? texture->bitmap : white;
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	rasterizer_globals.current_lock_operation = _rasterizer_lock_cinematics;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
}

static void menu_shape_rectangle(struct bitmap_data *white, struct menu_shape_texture const *texture, real pixels,
	real x0, real y0, real x1, real y1, pixel32 top, pixel32 bottom)
{
	real x[4], y[4];
	pixel32 colors[4];

	if (x1 <= x0 || y1 <= y0)
		return;
	x[0] = x0; y[0] = y0;
	x[1] = x1; y[1] = y0;
	x[2] = x1; y[2] = y1;
	x[3] = x0; y[3] = y1;
	colors[0] = colors[1] = top;
	colors[2] = colors[3] = bottom;
	menu_shape_quad(white, texture, pixels, x, y, colors);
}

/* a corner's band between two radii about its centre (from the centre for
inner 0), towards side_x and side_y, its colour at each radius */
static void menu_shape_corner(struct bitmap_data *white, struct menu_shape_texture const *texture, real pixels,
	real center_x, real center_y, real side_x, real side_y, real inner, real outer, pixel32 inner_color,
	pixel32 outer_color, short segments)
{
	/* (each quadrilateral's corners turning the same way as the
	rectangles', which the renderer may cull otherwise) */
	short turn = side_x * side_y < 0.0f ? 1 : 0;
	short segment;

	if (outer <= inner)
		return;
	for (segment = 0; segment < segments; segment++)
	{
		real angle0 = (real)(segment + turn) * (0.5f * (real)M_PI) / segments;
		real angle1 = (real)(segment + 1 - turn) * (0.5f * (real)M_PI) / segments;
		real x[4], y[4];
		pixel32 colors[4];

		x[0] = center_x + side_x * outer * cosf(angle0);
		y[0] = center_y + side_y * outer * sinf(angle0);
		x[1] = center_x + side_x * outer * cosf(angle1);
		y[1] = center_y + side_y * outer * sinf(angle1);
		x[2] = center_x + side_x * inner * cosf(angle1);
		y[2] = center_y + side_y * inner * sinf(angle1);
		x[3] = center_x + side_x * inner * cosf(angle0);
		y[3] = center_y + side_y * inner * sinf(angle0);
		colors[0] = colors[1] = outer_color;
		colors[2] = colors[3] = inner_color;
		menu_shape_quad(white, texture, pixels, x, y, colors);
	}
}

/* a rounded box in the display's pixels (its sides on their edges, its
corners' radius whole pixels), filled with fill (of a picture's texture,
or white), and edged edge pixels wide in color (none for 0). Everything is
inside the box: the curves' edges are blended over their last pixel, inside
the radius, so that a corner meets its straight sides with none of it past
them (issue 596: a cap's corners stood a pixel past its edges) */
static void menu_shape_rounded(struct bitmap_data *white, struct menu_shape_texture const *texture, real pixels,
	real x0, real y0, real x1, real y1, real radius, real edge, pixel32 fill, pixel32 color)
{
	static real const sides[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
	boolean edged = edge > 0.0f && (color >> 24) != 0;
	pixel32 rim = edged ? color : fill;
	pixel32 clear = rim & 0x00FFFFFFUL;
	real solid;
	short segments, corner;

	radius = MIN(radius, floorf(MIN(x1 - x0, y1 - y0) / 2.0f));
	if (edged)
		radius = MAX(radius, edge + 1.0f);
	else
		edge = 0.0f;
	radius = MAX(radius, 0.0f);
	if (x1 - x0 < 2.0f * radius || y1 - y0 < 2.0f * radius || x1 <= x0 || y1 <= y0)
		return;
	/* (enough segments that a curve looks round at any size) */
	segments = (short)MAX(2, MIN(8, (long)(radius / 2.0f)));
	/* (the fill's corners solid out to the edge, or to the curve's last
	pixel) */
	solid = edged ? radius - edge : MAX(radius - 1.0f, 0.0f);

	/* the fill: the middle, above and below it, and the corners */
	menu_shape_rectangle(white, texture, pixels, x0, y0 + radius, x1, y1 - radius, fill, fill);
	menu_shape_rectangle(white, texture, pixels, x0 + radius, y0, x1 - radius, y0 + radius, fill, fill);
	menu_shape_rectangle(white, texture, pixels, x0 + radius, y1 - radius, x1 - radius, y1, fill, fill);
	/* the edge over it: the four sides, between the corners */
	if (edged)
	{
		menu_shape_rectangle(white, NULL, pixels, x0 + radius, y0, x1 - radius, y0 + edge, color, color);
		menu_shape_rectangle(white, NULL, pixels, x0 + radius, y1 - edge, x1 - radius, y1, color, color);
		menu_shape_rectangle(white, NULL, pixels, x0, y0 + radius, x0 + edge, y1 - radius, color, color);
		menu_shape_rectangle(white, NULL, pixels, x1 - edge, y0 + radius, x1, y1 - radius, color, color);
	}
	for (corner = 0; corner < 4 && radius > 0.0f; corner++)
	{
		real center_x = sides[corner][0] < 0.0f ? x0 + radius : x1 - radius;
		real center_y = sides[corner][1] < 0.0f ? y0 + radius : y1 - radius;

		menu_shape_corner(white, texture, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
			0.0f, solid, fill, fill, segments);
		if (edged)
		{
			/* (the edge blended into the fill over a pixel, then solid to
			the curve's last pixel) */
			if (solid >= 1.0f)
			{
				menu_shape_corner(white, NULL, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
					solid - 1.0f, solid, color & 0x00FFFFFFUL, color, segments);
			}
			menu_shape_corner(white, NULL, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
				solid, radius - 1.0f, color, color, segments);
		}
		/* (the curve's last pixel blended to nothing, inside the radius) */
		menu_shape_corner(white, edged ? NULL : texture, pixels, center_x, center_y, sides[corner][0],
			sides[corner][1], radius - 1.0f, radius, rim, clear, segments);
	}
}

/* a position in the screen's units as the display's pixel edge nearest it */
static real menu_shape_pixel_edge(real units, real pixels)
{
	return floorf((units - MENU_SHAPE_PIXEL_ORIGIN) * pixels + 0.5f);
}

/* ---------- public code */

/* the display's pixels for each of the screen's units (1 at least) */
real menu_shape_pixels(void)
{
	real pixels = halo_screen_pixel_scale();

	return pixels > 0.0f ? pixels : 1.0f;
}

/* a box in the display's pixels (its sides on their edges), its corners
rounded by radius pixels: filled, edged edge pixels wide (none for 0, or
a colour of no alpha), the curves' edges blended over their last pixel
(menu_shape_rounded) */
void menu_shape_rounded_box_pixels(
	real x0,
	real y0,
	real x1,
	real y1,
	real radius,
	real edge,
	pixel32 fill,
	pixel32 color)
{
	struct bitmap_data *white = menu_shape_white();

	if (!white)
		return;
	menu_shape_rounded(white, NULL, menu_shape_pixels(), x0, y0, x1, y1, radius, edge, fill, color);
}

/* a box in the screen's units (x from the menus' 640's left, which a wide
screen reaches past), its corners rounded by radius units, edged edge
units wide: its sides on the display's pixels' edges, the edge as wide on
every side, both whole pixels */
void menu_shape_rounded_box(
	real x0,
	real y0,
	real x1,
	real y1,
	real radius,
	real edge,
	pixel32 fill,
	pixel32 color)
{
	real pixels = menu_shape_pixels();

	menu_shape_rounded_box_pixels(
		menu_shape_pixel_edge(x0, pixels),
		menu_shape_pixel_edge(y0, pixels),
		menu_shape_pixel_edge(x1, pixels),
		menu_shape_pixel_edge(y1, pixels),
		floorf(radius * pixels + 0.5f),
		edge > 0.0f ? MAX(floorf(edge * pixels + 0.5f), 1.0f) : 0.0f,
		fill,
		color);
}

/* a box in the screen's units, as menu_shape_rounded_box draws it, its fill
shaded from top at its top to bottom at its bottom */
void menu_shape_shaded_box(
	real x0,
	real y0,
	real x1,
	real y1,
	real radius,
	real edge,
	pixel32 top,
	pixel32 bottom,
	pixel32 color)
{
	struct bitmap_data *white = menu_shape_white();
	struct menu_shape_texture shade;
	real pixels = menu_shape_pixels();

	if (!white)
		return;
	csmemset(&shade, 0, sizeof(shade));
	shade.x0 = menu_shape_pixel_edge(x0, pixels);
	shade.y0 = menu_shape_pixel_edge(y0, pixels);
	shade.x1 = menu_shape_pixel_edge(x1, pixels);
	shade.y1 = menu_shape_pixel_edge(y1, pixels);
	shade.top = top;
	shade.bottom = bottom;
	if (shade.x1 <= shade.x0 || shade.y1 <= shade.y0)
		return;
	menu_shape_rounded(white, &shade, pixels, shade.x0, shade.y0, shade.x1, shade.y1, floorf(radius * pixels + 0.5f),
		edge > 0.0f ? MAX(floorf(edge * pixels + 0.5f), 1.0f) : 0.0f, 0xFFFFFFFFUL, color);
}

/* a picture (a frame's texture, from u0, v0 at its top left to u1, v1 at
its bottom right) drawn over a box in the screen's units, its corners cut
round by radius units, the curves' edges blended over their last pixel:
its own corners never show past them, and nothing is drawn over them (the
campaign's mission pictures, issue 687: a picture of what is around a
square one's corners drawn over them showed it through, and missed where
the picture moved). The color tints and fades it, as a widget's picture */
void menu_shape_rounded_picture(
	struct bitmap_data *bitmap,
	real x0,
	real y0,
	real x1,
	real y1,
	real u0,
	real v0,
	real u1,
	real v1,
	real radius,
	pixel32 color)
{
	struct menu_shape_texture texture;
	real pixels = menu_shape_pixels();

	if (!bitmap)
		return;
	texture.bitmap = bitmap;
	texture.x0 = menu_shape_pixel_edge(x0, pixels);
	texture.y0 = menu_shape_pixel_edge(y0, pixels);
	texture.x1 = menu_shape_pixel_edge(x1, pixels);
	texture.y1 = menu_shape_pixel_edge(y1, pixels);
	texture.u0 = u0;
	texture.v0 = v0;
	texture.u1 = u1;
	texture.v1 = v1;
	texture.top = texture.bottom = 0xFFFFFFFFUL;
	if (texture.x1 <= texture.x0 || texture.y1 <= texture.y0)
		return;
	menu_shape_rounded(NULL, &texture, pixels, texture.x0, texture.y0, texture.x1, texture.y1,
		floorf(radius * pixels + 0.5f), 0.0f, color, 0);
}

/* a band in the screen's units, its colour top at its top shading to
bottom at its bottom, its sides on the display's pixels' edges */
void menu_shape_band(
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 top,
	pixel32 bottom)
{
	struct bitmap_data *white = menu_shape_white();
	real pixels = menu_shape_pixels();

	if (!white)
		return;
	menu_shape_rectangle(white, NULL, pixels, menu_shape_pixel_edge(x0, pixels), menu_shape_pixel_edge(y0, pixels),
		menu_shape_pixel_edge(x1, pixels), menu_shape_pixel_edge(y1, pixels), top, bottom);
}

/* a text field's caret: a bar from x, width units wide (a pixel at least),
from y0 to y1, in the text's units (drawn as the text is, scaled by
rasterizer_text_set_scale about its origin), its sides on the display's
pixels' edges, over the text's drop shadow (a unit down and right, black) */
void menu_shape_caret(
	real x,
	real y0,
	real y1,
	real width,
	pixel32 color)
{
	struct bitmap_data *white = menu_shape_white();
	real pixels = menu_shape_pixels();
	real scale, origin_x, origin_y, left, right, top, bottom, shadow;
	pixel32 dark = color & 0xFF000000UL;

	if (!white || y1 <= y0)
		return;
	rasterizer_text_get_scale(&scale, &origin_x, &origin_y);
	left = menu_shape_pixel_edge(origin_x + (x - origin_x) * scale, pixels);
	right = MAX(menu_shape_pixel_edge(origin_x + (x + width - origin_x) * scale, pixels), left + 1.0f);
	top = menu_shape_pixel_edge(origin_y + (y0 - origin_y) * scale, pixels);
	bottom = menu_shape_pixel_edge(origin_y + (y1 - origin_y) * scale, pixels);
	shadow = MAX(floorf(scale * pixels + 0.5f), 1.0f);
	menu_shape_rectangle(white, NULL, pixels, left + shadow, top + shadow, right + shadow, bottom + shadow, dark, dark);
	menu_shape_rectangle(white, NULL, pixels, left, top, right, bottom, color, color);
}
