/*
RASTERIZER_TEXT.C

symbols in this file:
00172E80 0010:
	_code_00172e80 (0000)
00172E90 0010:
	_code_00172e90 (0000)
00172EA0 0090:
	_rasterizer_text_cache_initialize (0000)
00172F30 0010:
	_rasterizer_text_set_shadow_color (0000)
00172F40 0030:
	_rasterizer_text_cache_flush (0000)
00172F70 0030:
	_rasterizer_text_cache_dispose (0000)
00172FA0 0020:
	_code_00172fa0 (0000)
00172FC0 00b0:
	_code_00172fc0 (0000)
00173070 0060:
	_code_00173070 (0000)
001730D0 0380:
	_code_001730d0 (0000)
00173450 00f0:
	_code_00173450 (0000)
00173540 0170:
	_code_00173540 (0000)
001736B0 0200:
	_rasterizer_draw_string (0000)
001738B0 0200:
	_rasterizer_draw_unicode_string (0000)
0029EEE0 0033:
	??_C@_0DD@DKOHMJNA@?$CD?$CD?$CD?5ERROR?5failed?5to?5initialize?5h@ (0000)
0029EF14 0026:
	??_C@_0CG@HPKDNNGC@?$CBhardware_character_cache?4initia@ (0000)
0029EF3C 002c:
	??_C@_0CM@KJINBGGM@c?3?2halo?2SOURCE?2rasterizer?2raster@ (0000)
0029EF68 0009:
	??_C@_08KDNNBGOA@x0?5?$CG?$CG?5y0?$AA@ (0000)
0029EF78 0054:
	??_C@_0FE@BCPFIEIE@hardware_character_index?$DO?$DN0?5?$CG?$CG?5h@ (0000)
0029EFCC 0025:
	??_C@_0CF@POBHCEMM@hardware_character_cache?4initial@ (0000)
0029EFF4 0026:
	??_C@_0CG@JHCOKPHL@font?5cache?5overwrote?5character?5i@ (0000)
0029F01C 0013:
	??_C@_0BD@PIEBJAO@hardware_character?$AA@ (0000)
0029F030 0046:
	??_C@_0EG@CBPAFGHN@font_character?9?$DObitmap_height?$DM?$DNH@ (0000)
0029F078 0044:
	??_C@_0EE@KGLMBKON@font_character?9?$DObitmap_width?$DM?$DNHA@ (0000)
0029F0C0 0068:
	??_C@_0GI@IDEJCHPO@font_character?$DN?$DNhardware_charact@ (0000)
0029F128 0074:
	??_C@_0HE@KFKECHAF@font_character?9?$DOhardware_charact@ (0000)
0030D4D0 0002:
	_data_0030d4d0 (0000)
004B82C0 0816:
	_bss_004b82c0 (0000)
*/


/* ---------- headers */

#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "math/integer_math.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/rasterizer_console_vars.h"
#include "rasterizer/rasterizer_text.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h" /* port: tag_iterator (hires_text_fonts_ahead) */
#include "menu_layout.h" /* port: port/linux/game/menu_layout.c */
#include "menu_view.h" /* port: port/linux/game/menu_view.c */
wchar_t const *menu_studio_text(wchar_t const *text); /* port: port/linux/game/menu_studio.c */

/* port: the high-res text (port/linux/src/text_hires.c): the text drawn with
fonts at the display's resolution, laid out as before. Characters are drawn
from an atlas of the fonts' glyphs that a placeholder bitmap stands for. */
struct text_hires_glyph
{
	float left, top, right, bottom;
	float u0, v0, u1, v1;
	float advance;
	float ink_left, ink_right;
};

#define TEXT_HIRES_FIT_LETTERS 52
int text_hires_enabled(void); /* port: port/linux/src/text_hires.c */
long text_hires_font(char const *tag_name, float cap_height, short const *letter_widths, float oversample);
void text_hires_rasterize(long font, unsigned long code);
int text_hires_glyph(long font, unsigned long code, struct text_hires_glyph *glyph);
float text_hires_advance(long font, unsigned long code);
float text_hires_kerning(long font, unsigned long first, unsigned long second);
void text_hires_register_atlas(unsigned long const *texture, unsigned long width, unsigned long height);

/* ---------- constants */

enum
{
	HARDWARE_CHARACTER_CACHE_BITMAP_WIDTH = 128,
	HARDWARE_CHARACTER_CACHE_BITMAP_HEIGHT = 128,
	MAXIMUM_HARDWARE_CHARACTERS = 256,
	/* port: the high-res text atlas's placeholder (its texels are the units
	of the atlas's glyphs), and the fonts remembered */
	HIRES_TEXT_ATLAS_BITMAP_SIZE = 256,
	MAXIMUM_HIRES_TEXT_FONTS = 8,
};

enum
{
	_bitmap_format_a4r4g4b4 = 9,
};

enum
{
	_rasterizer_target_render_primary = 0,
	_shader_framebuffer_blend_function_alpha_blend = 0,
};

/* ---------- macros */

/* ---------- structures */

struct font_character
{
	word character;
	short character_width;
	short bitmap_width;
	short bitmap_height;
	short bitmap_origin_x;
	short bitmap_origin_y;
	short hardware_character_index;
	short pad;
	long pixels_offset;
};

struct parse_string_state;

typedef void (*draw_character_proc)(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy);

struct hardware_character
{
	struct font_character *character;
	short x0;
	short y0;
};

struct hardware_character_cache
{
	boolean initialized;
	byte unused1;
	short read_index;
	short write_index;
	short x0;
	short y0;
	short maximum_character_height;
	struct bitmap_data *bitmap;
	struct hardware_character characters[MAXIMUM_HARDWARE_CHARACTERS];
};

/* ---------- prototypes */

static struct bitmap_data *hardware_character_cache_get_bitmap(
	void);
static void hardware_character_cache_get_origin(
	short hardware_character_index,
	short *x0,
	short *y0);
static short hardware_character_padding(
	struct font_character const *font_character);
static void flush_hardware_character(
	struct hardware_character *hardware_character);
static void cache_hardware_format_character(
	struct font_header *font,
	struct font_character *font_character);
static void rasterizer_draw_character(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy);
static void rasterizer_draw_character_with_dropshadow(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy);
static long hires_text_font_get(
	long font_index,
	real oversample);
static void hires_text_fonts_ahead(
	void);
static void rasterizer_text_draw_scaled_character(
	struct dynamic_screen_vertex const *vertices);
static void rasterizer_draw_hires_character(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy);
static void rasterizer_draw_hires_character_with_dropshadow(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy);

/* ---------- globals */


static struct hardware_character_cache hardware_character_cache;
static struct bitmap_data *hires_text_atlas = NULL;
static long hires_text_font = NONE;
/* port: the high-res text's fonts of the font tags of the map (forgotten
when its tags are, by rasterizer_text_cache_flush) */
static struct
{
	struct font_header *font;
	real oversample;
	long hires_font;
} hires_text_fonts[MAXIMUM_HIRES_TEXT_FONTS];
static short hires_text_next_font = 0;
/* port: the map's fonts made, and their glyphs rasterized, ahead of their
first strings (hires_text_fonts_ahead) */
static boolean hires_text_fonts_made = FALSE;
/* port: the text's scale about a point (rasterizer_text_set_scale) */
static real text_scale = 1.0f;
static real text_scale_origin_x = 0.0f;
static real text_scale_origin_y = 0.0f;
/* port: the line's shift left and its cut (rasterizer_text_set_shift), the
high-res text's; the font tags' bitmaps are drawn at whole units */
static real text_shift = 0.0f;
static real text_shift_left = 0.0f;
static real text_shift_right = -1.0f;
/* (and faded out over so many units at its left and right edges: 0 none) */
static real text_shift_fade_left = 0.0f;
static real text_shift_fade_right = 0.0f;
static pixel32 global_shadow_color = 0;
static short rasterizer_text_unused = 0;
static short magic_number= 12;

/* ---------- public code */

void lock_rasterizer_text_data(
	void)
{
	return;
}

void unlock_rasterizer_text_data(
	void)
{
	return;
}

boolean
rasterizer_text_cache_initialize(
	void)
{
	struct bitmap_data *bitmap;
	boolean success = TRUE;

	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 118, !hardware_character_cache.initialized);

	bitmap = bitmap_2d_new(
		HARDWARE_CHARACTER_CACHE_BITMAP_WIDTH,
		HARDWARE_CHARACTER_CACHE_BITMAP_HEIGHT,
		0,
		_bitmap_format_a4r4g4b4);

	if (bitmap)
	{
		memset(&hardware_character_cache, 0, sizeof(hardware_character_cache));

		if (rasterizer_bitmap_new(bitmap))
		{
			hardware_character_cache.bitmap = bitmap;
			hardware_character_cache.initialized = TRUE;

			/* port: the high-res text atlas's placeholder */
			hires_text_atlas = bitmap_2d_new(
				HIRES_TEXT_ATLAS_BITMAP_SIZE,
				HIRES_TEXT_ATLAS_BITMAP_SIZE,
				0,
				_bitmap_format_a4r4g4b4);
			if (hires_text_atlas && !rasterizer_bitmap_new(hires_text_atlas))
			{
				bitmap_delete(hires_text_atlas);
				hires_text_atlas = NULL;
			}
			if (hires_text_atlas)
			{
				text_hires_register_atlas(
					(unsigned long const *)hires_text_atlas->hardware_format,
					HIRES_TEXT_ATLAS_BITMAP_SIZE,
					HIRES_TEXT_ATLAS_BITMAP_SIZE);
			}
		}
		else
		{
			error(_error_silent, "### ERROR failed to initialize hardware text cache");
			success = FALSE;
		}
	}
	else
	{
		error(_error_silent, "### ERROR failed to initialize hardware text cache");
		success = FALSE;
	}

	return success;
}

void
rasterizer_text_set_shadow_color(
	pixel32 shadow_color)
{
	global_shadow_color = shadow_color;

	return;
}

void
rasterizer_text_cache_flush(
	void)
{
	struct hardware_character *hardware_character;
	long hardware_character_count;

	if (hardware_character_cache.initialized)
	{
		hardware_character = hardware_character_cache.characters;
		hardware_character_count = MAXIMUM_HARDWARE_CHARACTERS;
		do
		{
			if (hardware_character->character)
				hardware_character->character->hardware_character_index = NONE;
			hardware_character->character = NULL;
			hardware_character++;
		} while (--hardware_character_count);
	}
	/* port: the next map's font tags may be at the same addresses */
	memset(hires_text_fonts, 0, sizeof(hires_text_fonts));
	hires_text_next_font = 0;
	hires_text_fonts_made = FALSE;

	return;
}

void
rasterizer_text_cache_dispose(
	void)
{
	if (hardware_character_cache.initialized)
	{
		rasterizer_text_cache_flush();
		bitmap_delete(hardware_character_cache.bitmap);
		hardware_character_cache.initialized = FALSE;
		if (hires_text_atlas)
		{
			text_hires_register_atlas(NULL, 0, 0);
			bitmap_delete(hires_text_atlas);
			hires_text_atlas = NULL;
		}
	}

	return;
}

static void
rasterizer_draw_character(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy)
{
	/* port: the string's characters so far drawn before a character is
	cached, which can give another's place in the cache to it */
	if (font_character->hardware_character_index == NONE)
		rasterizer_text_flush();
	cache_hardware_format_character(font, font_character);

	if (font_character->hardware_character_index != NONE)
	{
		struct dynamic_screen_vertex vertices[NUMBER_OF_VERTICES_PER_QUADRILATERAL];
		short u0, v0;

		hardware_character_cache_get_origin(font_character->hardware_character_index, &u0, &v0);

		u0 += x;
		v0 += y;

		vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = color;

		vertices[0].position.x = vertices[3].position.x = (real)x0;
		vertices[1].position.x = vertices[2].position.x = (real)(x0 + dx);
		vertices[0].position.y = vertices[1].position.y = (real)y0;
		vertices[2].position.y = vertices[3].position.y = (real)(y0 + dy);

		vertices[0].texture_coordinates.x = vertices[3].texture_coordinates.x = (real)u0;
		vertices[1].texture_coordinates.x = vertices[2].texture_coordinates.x = (real)(u0 + dx);
		vertices[0].texture_coordinates.y = vertices[1].texture_coordinates.y = (real)v0;
		vertices[2].texture_coordinates.y = vertices[3].texture_coordinates.y = (real)(v0 + dy);

		rasterizer_text_draw_scaled_character(vertices);
	}

	return;
}

void
rasterizer_draw_string(
	rectangle2d const *bounds,
	rectangle2d const *clip,
	point2d *cursor_reference,
	short height_adjust,
	char const *string)
{
	boolean drop_shadow = TRUE;

	if (rasterizer_debug_options.draw_dynamic_screen_geometry
		&& global_window_parameters.rasterizer_target == _rasterizer_target_render_primary)
	{
		struct bitmap_data *bitmap;
		struct rasterizer_dynamic_screen_geometry_parameters parameters;
		rectangle2d window_bounds;
		rectangle2d viewport_bounds;

		magic_number++;

		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 180, string);

		bitmap = hardware_character_cache_get_bitmap();

		if (bitmap && string[0])
		{
			long length = strlen(string);
			long vertex_count;
			draw_character_proc draw_character;

			if (drop_shadow)
			{
				vertex_count = length * NUMBER_OF_VERTICES_PER_QUADRILATERAL * 2;
				draw_character = rasterizer_draw_character_with_dropshadow;
			}
			else
			{
				vertex_count = length * NUMBER_OF_VERTICES_PER_QUADRILATERAL;
				draw_character = rasterizer_draw_character;
			}

			/* port: from the font's atlas, its glyphs rasterized first */
			hires_text_fonts_ahead();
			hires_text_font = hires_text_atlas ? hires_text_font_get(draw_string_get_font(), MAX(text_scale, 1.0f)) : NONE;
			if (hires_text_font != NONE)
			{
				long character_index;

				for (character_index = 0; character_index < length; character_index++)
					text_hires_rasterize(hires_text_font, (unsigned char)string[character_index]);
				bitmap = hires_text_atlas;
				draw_character = drop_shadow ?
					rasterizer_draw_hires_character_with_dropshadow :
					rasterizer_draw_hires_character;
			}

			if (!bounds)
			{
				window_bounds = render.camera.window_bounds;
				offset_rectangle2d(
					&window_bounds,
					-render.camera.viewport_bounds.x0,
					-render.camera.viewport_bounds.y0);
			}
			else
			{
				window_bounds = *bounds;
			}

			if (!clip)
			{
				viewport_bounds = render.camera.viewport_bounds;
				offset_rectangle2d(
					&viewport_bounds,
					-render.camera.viewport_bounds.x0,
					-render.camera.viewport_bounds.y0);
				/* port: from the screen's left edge, left of the menus' 0 while
				they draw centred on a wide screen (the port's screens' wide layout,
				a key cap's words in a prompt: ui_widget.c's
				draw_string_and_hack_in_icons; the carnage report) */
				viewport_bounds.x0 = (short)(viewport_bounds.x0 -
					MAX(ui_widget_port_anchor_extra(), menu_view_side()));
			}
			else
			{
				/* port: left of 0 where the menus' widgets are anchored to a wide
				screen's left edge (ui_widget_port_anchor_extra), or laid out across
				it (the port's screens' wide layout, the carnage report): the
				widgets' own clip keeps the others in the 640 */
				set_rectangle2d(
					&viewport_bounds,
					FLOOR(clip->x0, -MAX(ui_widget_port_anchor_extra(), menu_view_side())),
					FLOOR(clip->y0, 0),
					MIN(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0, clip->x1),
					MIN(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0, clip->y1));
			}
			/* port: text drawn scaled (rasterizer_text_set_scale) clipped where
			it reaches the viewport once scaled: the clip as it is before the
			scale */
			if (text_scale != 1.0f)
			{
				viewport_bounds.x0 = (short)(text_scale_origin_x + (viewport_bounds.x0 - text_scale_origin_x) / text_scale);
				viewport_bounds.x1 = (short)(text_scale_origin_x + (viewport_bounds.x1 - text_scale_origin_x) / text_scale);
				viewport_bounds.y0 = (short)(text_scale_origin_y + (viewport_bounds.y0 - text_scale_origin_y) / text_scale);
				viewport_bounds.y1 = (short)(text_scale_origin_y + (viewport_bounds.y1 - text_scale_origin_y) / text_scale);
			}

			memset(&parameters, 0, sizeof(parameters));
			parameters.map_texture_scale[0].i = 1.0f / (real)bitmap->width;
			parameters.map_texture_scale[0].j = 1.0f / (real)bitmap->height;
			parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
			parameters.meter_parameters = NULL;
			parameters.point_sampled = FALSE;
			/* port: (rasterizer.h) */
			parameters.alpha_weighted = FALSE;
			parameters.framebuffer_blend_function = _shader_framebuffer_blend_function_alpha_blend;
			parameters.map[0] = bitmap;

			rasterizer_text_begin(&parameters);
			draw_string(
				draw_character,
				&window_bounds,
				cursor_reference,
				&viewport_bounds,
				height_adjust,
				string);
			rasterizer_text_end();
			/* port: debug.render_marks: the text's draws (the batch's) drawn (port/linux/game/render_marks.c) */
			render_marks_text_end();
		}
	}

	return;
}

void
rasterizer_draw_unicode_string(
	rectangle2d const *bounds,
	rectangle2d const *clip,
	point2d *cursor_reference,
	short height_adjust,
	wchar_t const *string)
{
	boolean drop_shadow = TRUE;

	/* port: every label longer, where the menu studio asks
	(port/linux/game/menu_studio.c) */
	string = menu_studio_text(string);
	if (rasterizer_debug_options.draw_dynamic_screen_geometry
		&& global_window_parameters.rasterizer_target == _rasterizer_target_render_primary)
	{
		struct bitmap_data *bitmap;
		struct rasterizer_dynamic_screen_geometry_parameters parameters;
		rectangle2d window_bounds;
		rectangle2d viewport_bounds;

		magic_number++;

		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 310, string);

		bitmap = hardware_character_cache_get_bitmap();

		if (bitmap && string[0])
		{
			long length = ustrlen(string);
			long vertex_count;
			draw_character_proc draw_character;

			if (drop_shadow)
			{
				vertex_count = length * NUMBER_OF_VERTICES_PER_QUADRILATERAL * 2;
				draw_character = rasterizer_draw_character_with_dropshadow;
			}
			else
			{
				vertex_count = length * NUMBER_OF_VERTICES_PER_QUADRILATERAL;
				draw_character = rasterizer_draw_character;
			}

			/* port: from the font's atlas, its glyphs rasterized first */
			hires_text_fonts_ahead();
			hires_text_font = hires_text_atlas ? hires_text_font_get(draw_string_get_font(), MAX(text_scale, 1.0f)) : NONE;
			if (hires_text_font != NONE)
			{
				long character_index;

				for (character_index = 0; character_index < length; character_index++)
					text_hires_rasterize(hires_text_font, (word)string[character_index]);
				bitmap = hires_text_atlas;
				draw_character = drop_shadow ?
					rasterizer_draw_hires_character_with_dropshadow :
					rasterizer_draw_hires_character;
			}

			if (!bounds)
			{
				window_bounds = render.camera.window_bounds;
				offset_rectangle2d(
					&window_bounds,
					-render.camera.viewport_bounds.x0,
					-render.camera.viewport_bounds.y0);
			}
			else
			{
				window_bounds = *bounds;
			}

			if (!clip)
			{
				viewport_bounds = render.camera.viewport_bounds;
				offset_rectangle2d(
					&viewport_bounds,
					-render.camera.viewport_bounds.x0,
					-render.camera.viewport_bounds.y0);
				/* port: from the screen's left edge, left of the menus' 0 while
				they draw centred on a wide screen (the port's screens' wide layout,
				a key cap's words in a prompt: ui_widget.c's
				draw_string_and_hack_in_icons; the carnage report) */
				viewport_bounds.x0 = (short)(viewport_bounds.x0 -
					MAX(ui_widget_port_anchor_extra(), menu_view_side()));
			}
			else
			{
				/* port: left of 0 where the menus' widgets are anchored to a wide
				screen's left edge (ui_widget_port_anchor_extra), or laid out across
				it (the port's screens' wide layout, the carnage report): the
				widgets' own clip keeps the others in the 640 */
				set_rectangle2d(
					&viewport_bounds,
					FLOOR(clip->x0, -MAX(ui_widget_port_anchor_extra(), menu_view_side())),
					FLOOR(clip->y0, 0),
					MIN(render.camera.viewport_bounds.x1 - render.camera.viewport_bounds.x0, clip->x1),
					MIN(render.camera.viewport_bounds.y1 - render.camera.viewport_bounds.y0, clip->y1));
			}
			/* port: text drawn scaled (rasterizer_text_set_scale) clipped where
			it reaches the viewport once scaled: the clip as it is before the
			scale */
			if (text_scale != 1.0f)
			{
				viewport_bounds.x0 = (short)(text_scale_origin_x + (viewport_bounds.x0 - text_scale_origin_x) / text_scale);
				viewport_bounds.x1 = (short)(text_scale_origin_x + (viewport_bounds.x1 - text_scale_origin_x) / text_scale);
				viewport_bounds.y0 = (short)(text_scale_origin_y + (viewport_bounds.y0 - text_scale_origin_y) / text_scale);
				viewport_bounds.y1 = (short)(text_scale_origin_y + (viewport_bounds.y1 - text_scale_origin_y) / text_scale);
			}
			/* port: the string's bounds, clip and extent, and its extent in
			bounds without a foot (the lines that run past the foot are not
			drawn) and in bounds without a right edge (a line's letters past
			it are not drawn), for the menus' layout (menu_layout.c) */
			if (menu_layout_recording())
			{
				rectangle2d extent, whole, wide, unbounded, cursor;

				draw_unicode_string_compute_bounds(&window_bounds, string, &extent, &cursor);
				unbounded = window_bounds;
				unbounded.y1 = SHORT_MAX;
				draw_unicode_string_compute_bounds(&unbounded, string, &whole, &cursor);
				unbounded = window_bounds;
				unbounded.x1 = SHORT_MAX;
				draw_unicode_string_compute_bounds(&unbounded, string, &wide, &cursor);
				menu_layout_text(&window_bounds, &viewport_bounds, &extent, &whole, &wide, string, text_scale,
					text_scale_origin_x, text_scale_origin_y);
			}

			memset(&parameters, 0, sizeof(parameters));
			parameters.map_texture_scale[0].i = 1.0f / (real)bitmap->width;
			parameters.map_texture_scale[0].j = 1.0f / (real)bitmap->height;
			parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
			parameters.meter_parameters = NULL;
			parameters.point_sampled = FALSE;
			/* port: (rasterizer.h) */
			parameters.alpha_weighted = FALSE;
			parameters.framebuffer_blend_function = _shader_framebuffer_blend_function_alpha_blend;
			parameters.map[0] = bitmap;

			rasterizer_text_begin(&parameters);
			draw_unicode_string(
				draw_character,
				&window_bounds,
				cursor_reference,
				&viewport_bounds,
				height_adjust,
				string);
			rasterizer_text_end();
			/* port: debug.render_marks: the text's draws (the batch's) drawn (port/linux/game/render_marks.c) */
			render_marks_text_end();
		}
	}

	return;
}

static void
rasterizer_draw_character_with_dropshadow(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy)
{
	/* port: the string's characters so far drawn before a character is
	cached, which can give another's place in the cache to it */
	if (font_character->hardware_character_index == NONE)
		rasterizer_text_flush();
	cache_hardware_format_character(font, font_character);

	if (font_character->hardware_character_index != NONE)
	{
		struct dynamic_screen_vertex vertices[NUMBER_OF_VERTICES_PER_QUADRILATERAL];
		real x_offset = 1.0f;
		real y_offset = 1.0f;
		unsigned long shadow_color = global_shadow_color
			? global_shadow_color
			: (color & 0xFF000000);
		real left = (real)x0;
		real right = (real)(x0 + dx);
		real top = (real)y0;
		real bottom = (real)(y0 + dy);
		boolean shadow = TRUE;

		while (TRUE)
		{
			unsigned long vertex_color;
			short u0, v0;

			hardware_character_cache_get_origin(font_character->hardware_character_index, &u0, &v0);

			u0 += x;
			v0 += y;

			vertex_color = shadow ? shadow_color : color;

			vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = vertex_color;

			vertices[0].position.x = vertices[3].position.x = left + x_offset;
			vertices[1].position.x = vertices[2].position.x = right + x_offset;
			vertices[0].position.y = vertices[1].position.y = top + y_offset;
			vertices[2].position.y = vertices[3].position.y = bottom + y_offset;

			vertices[0].texture_coordinates.x = vertices[3].texture_coordinates.x = (real)u0;
			vertices[1].texture_coordinates.x = vertices[2].texture_coordinates.x = (real)(u0 + dx);
			vertices[0].texture_coordinates.y = vertices[1].texture_coordinates.y = (real)v0;
			vertices[2].texture_coordinates.y = vertices[3].texture_coordinates.y = (real)(v0 + dy);

			rasterizer_text_draw_scaled_character(vertices);

			if (!shadow)
				break;

			shadow = FALSE;
			x_offset = y_offset = 0.0f;
		}
	}

	return;
}

/* ---------- private code */

/* port: text drawn scale times larger about a point (in screen units), until
it is set back to 1: the characters' quads are laid out as before, and
scaled about it as they are drawn */
void rasterizer_text_set_scale(
	real scale,
	real origin_x,
	real origin_y)
{
	text_scale = scale > 0.0f ? scale : 1.0f;
	text_scale_origin_x = origin_x;
	text_scale_origin_y = origin_y;

	return;
}

void rasterizer_text_get_scale(
	real *scale,
	real *origin_x,
	real *origin_y)
{
	*scale = text_scale;
	*origin_x = text_scale_origin_x;
	*origin_y = text_scale_origin_y;

	return;
}

void rasterizer_text_set_shift(
	real shift,
	real left,
	real right,
	real fade_left,
	real fade_right)
{
	text_shift = shift;
	text_shift_left = left;
	text_shift_right = right;
	text_shift_fade_left = fade_left;
	text_shift_fade_right = fade_right;

	return;
}

/* port: a character's quad, scaled (rasterizer_text_set_scale), and moved
past the whole units it is laid out at (rasterizer_screen_offset_set) */
static void rasterizer_text_draw_scaled_character(
	struct dynamic_screen_vertex const *vertices)
{
	struct dynamic_screen_vertex scaled[NUMBER_OF_VERTICES_PER_QUADRILATERAL];
	real_vector2d offset;
	short vertex_index;

	rasterizer_screen_offset_get(&offset);
	if (text_scale == 1.0f && offset.i == 0.0f && offset.j == 0.0f)
	{
		rasterizer_text_draw_character(vertices);
		return;
	}
	for (vertex_index = 0; vertex_index < NUMBER_OF_VERTICES_PER_QUADRILATERAL; vertex_index++)
	{
		scaled[vertex_index] = vertices[vertex_index];
		scaled[vertex_index].position.x = offset.i +
			text_scale_origin_x + (vertices[vertex_index].position.x - text_scale_origin_x) * text_scale;
		scaled[vertex_index].position.y = offset.j +
			text_scale_origin_y + (vertices[vertex_index].position.y - text_scale_origin_y) * text_scale;
	}
	rasterizer_text_draw_character(scaled);

	return;
}

/* port: the high-res text's fonts of every font tag of the map, made as its
first string is drawn: their ASCII glyphs are rasterized then (text_hires.c),
before the menus and the HUD draw with them */
static void hires_text_fonts_ahead(
	void)
{
	struct tag_iterator iterator;
	long font_index;

	if (hires_text_fonts_made || !hires_text_atlas || !text_hires_enabled())
		return;
	hires_text_fonts_made = TRUE;
	tag_iterator_new(&iterator, FONT_GROUP_TAG);
	while ((font_index = tag_iterator_next(&iterator)) != NONE)
		hires_text_font_get(font_index, 1.0f);

	return;
}

/* port: the high-res text's font for a font tag, sized by the height of its
capital H (its rows with ink) and fitted to the widths of its letters, its
glyphs drawn with oversample times the pixels (for text scaled up), or NONE */
static long hires_text_font_get(
	long font_index,
	real oversample)
{
	struct font_header *font;
	struct font_character *capital;
	short letter_widths[TEXT_HIRES_FIT_LETTERS];
	short font_slot;
	short letter;
	short top = NONE;
	short bottom = NONE;
	short row;
	long hires_font;

	/* port: consult the committed setting before the font cache. In particular,
	 * never cache NONE merely because high-res text was disabled. */
	if (font_index == NONE || !text_hires_enabled())
		return NONE;
	font = font_definition_get(font_index);
	for (font_slot = 0; font_slot < MAXIMUM_HIRES_TEXT_FONTS; font_slot++)
	{
		if (hires_text_fonts[font_slot].font == font && hires_text_fonts[font_slot].oversample == oversample)
			return hires_text_fonts[font_slot].hires_font;
	}
	/* (A to Z, then a to z; none where the tag has not the letter) */
	for (letter = 0; letter < TEXT_HIRES_FIT_LETTERS; letter++)
	{
		struct font_character *character = font_get_character_by_ascii_code(font,
			(word)(letter < 26 ? 'A' + letter : 'a' + letter - 26));

		letter_widths[letter] = character ? character->character_width : 0;
	}
	capital = font_get_character_by_ascii_code(font, 'H');
	if (capital)
	{
		byte const *pixels = (byte const *)font->pixels.address + capital->pixels_offset;

		for (row = 0; row < capital->bitmap_height; row++)
		{
			short column;

			for (column = 0; column < capital->bitmap_width; column++)
			{
				if (pixels[row * capital->bitmap_width + column])
				{
					if (top == NONE)
						top = row;
					bottom = row;
					break;
				}
			}
		}
	}
	hires_font = top == NONE ? NONE :
		text_hires_font(tag_get_name(font_index), (float)(bottom - top + 1), letter_widths, (float)oversample);
	if (hires_font < 0)
		hires_font = NONE;
	hires_text_fonts[hires_text_next_font].font = font;
	hires_text_fonts[hires_text_next_font].oversample = oversample;
	hires_text_fonts[hires_text_next_font].hires_font = hires_font;
	hires_text_next_font = (short)((hires_text_next_font + 1) % MAXIMUM_HIRES_TEXT_FONTS);

	return hires_font;
}

/* port: the font tag's character's ink in its bitmap's columns x0 to x1 and
rows y0 to y1 (its pixels' coverage, added) */
static long font_character_ink(
	struct font_header *font,
	struct font_character *font_character,
	short x0,
	short x1,
	short y0,
	short y1)
{
	byte const *pixels = (byte const *)font->pixels.address + font_character->pixels_offset;
	short row, column;
	long ink = 0;

	for (row = MAX(y0, 0); row < MIN(y1, font_character->bitmap_height); row++)
	{
		for (column = MAX(x0, 0); column < MIN(x1, font_character->bitmap_width); column++)
			ink += pixels[row * font_character->bitmap_width + column];
	}

	return ink;
}

/* port: whether draw_string's cut of the font tag's character took a part
of it that shows: more than an eighth of its ink (a cut through its edge's
faint pixels, or its bitmap's empty columns, leaves it whole to the eye) */
static boolean font_character_cut(
	struct font_header *font,
	struct font_character *font_character,
	short x0,
	short x1,
	short y0,
	short y1)
{
	long all = font_character_ink(font, font_character, 0, font_character->bitmap_width, 0, font_character->bitmap_height);

	return font_character_ink(font, font_character, x0, x1, y0, y1) * 8 > all;
}

/* port: a font tag's character's ink, its left and right from the pen in
units: its bitmap's first and last columns with ink, each counted by its
most covered pixel (a column half covered, half a unit of ink); FALSE if it
has none */
static boolean font_character_ink_span(
	struct font_header *font,
	struct font_character const *font_character,
	real *left,
	real *right)
{
	byte const *pixels = (byte const *)font->pixels.address + font_character->pixels_offset;
	short first = NONE;
	short last = NONE;
	byte first_coverage = 0;
	byte last_coverage = 0;
	short column;

	for (column = 0; column < font_character->bitmap_width; column++)
	{
		byte coverage = 0;
		short row;

		for (row = 0; row < font_character->bitmap_height; row++)
		{
			if (pixels[row * font_character->bitmap_width + column] > coverage)
				coverage = pixels[row * font_character->bitmap_width + column];
		}
		if (coverage)
		{
			if (first == NONE)
			{
				first = column;
				first_coverage = coverage;
			}
			last = column;
			last_coverage = coverage;
		}
	}
	if (first == NONE)
		return FALSE;
	*left = first + 1.0f - first_coverage / 255.0f - font_character->bitmap_origin_x;
	*right = last + last_coverage / 255.0f - font_character->bitmap_origin_x;

	return TRUE;
}

/* port: where the high-res text draws a character's glyph: the left of its
advance from the pen. The game lays its text out from the font tag's
character widths, whole units at 480 lines, which are not the font's own,
and the tag's letters sit to the left of their widths (their ink from a
unit before the pen). Each glyph's advance centred on its character's, as
it was drawn, gave each letter the difference as space either side (half a
unit or more, which shows at the display's resolution as letters too close
or too far apart: issue 744), and every word a unit or two right of where
the tag's ink is (the space before a button's icon, and after it, a unit or
two more: issue 754). Now a run of text (draw_string_port_run: a line's
characters up to a tab, a button's icon or a change of style) is set with
the font's own advances, its spaces too, every character as much closer or
further apart as makes the run's ink as wide as the tag's, on which it is
laid: the font's own spacing throughout (its kerning too, where it has
any: Cyborg Interface Gothic has none), and the run where the game puts it,
in the room it gives it. A run the font cannot draw all of (or longer
than a run can be) is drawn with each glyph's advance centred on its
character's, as before. */
static struct
{
	long run;
	long hires_font;
	struct font_header *font;
	boolean laid_out;
	short count;
	real offsets[256];
} hires_text_run = { NONE, NONE, NULL, FALSE, 0 };

static void hires_text_run_lay_out(
	struct font_header *font,
	long hires_font,
	word const *characters,
	short count)
{
	real tag_pens[256];
	real font_pens[256];
	real advances[256];
	real tag_pen = 0.0f;
	real font_pen = 0.0f;
	real tag_left = 0.0f;
	real tag_right = 0.0f;
	real font_left = 0.0f;
	real font_right = 0.0f;
	real tracking;
	real limit = font->ascending_height;
	short first = NONE;
	short last = NONE;
	short previous = NONE;
	short index;

	hires_text_run.laid_out = FALSE;
	if (count > NUMBEROF(hires_text_run.offsets))
		return;
	for (index = 0; index < count; index++)
	{
		struct font_character *character = font_get_character_by_ascii_code(font, characters[index]);
		struct text_hires_glyph glyph;
		real ink_left;
		real ink_right;
		real advance;

		advances[index] = 0.0f;
		/* (a character the tag has not is not drawn, and moves nothing) */
		if (!character)
		{
			tag_pens[index] = tag_pen;
			font_pens[index] = font_pen;
			continue;
		}
		/* (the font's kerning, where it has it: OpenCE's, Overpass's) */
		if (previous != NONE)
			font_pen += text_hires_kerning(hires_font, characters[previous], characters[index]);
		previous = index;
		tag_pens[index] = tag_pen;
		font_pens[index] = font_pen;
		advance = text_hires_advance(hires_font, characters[index]);
		if (advance < 0.0f)
			return;
		advances[index] = advance;
		if (font_character_ink_span(font, character, &ink_left, &ink_right) &&
			text_hires_glyph(hires_font, characters[index], &glyph))
		{
			if (first == NONE)
			{
				first = index;
				tag_left = tag_pen + ink_left;
				font_left = font_pen + glyph.ink_left;
			}
			last = index;
			tag_right = tag_pen + ink_right;
			font_right = font_pen + glyph.ink_right;
		}
		tag_pen += character->character_width;
		font_pen += advance;
	}
	if (first == NONE)
		return;
	/* (the difference spread over the advances from the first ink to the
	last; the run centred on the tag's where that is more than a tenth of
	the ascent an advance) */
	tracking = last > first ? ((tag_right - tag_left) - (font_right - font_left)) / (last - first) : 0.0f;
	if (tracking > font->ascending_height * 0.1f || tracking < -font->ascending_height * 0.1f)
		tracking = 0.0f;
	for (index = 0; index < count; index++)
	{
		struct font_character *character = font_get_character_by_ascii_code(font, characters[index]);
		real centred = character ? (character->character_width - advances[index]) * 0.5f : 0.0f;
		real offset = font_pens[index] + (index - first) * tracking - tag_pens[index] +
			((tag_left + tag_right) - (font_left + font_right + (last - first) * tracking)) * 0.5f;

		/* (never further from its own place than the font's ascent: a run
		the font draws much wider or narrower than the tag) */
		if (offset > centred + limit)
			offset = centred + limit;
		if (offset < centred - limit)
			offset = centred - limit;
		hires_text_run.offsets[index] = offset;
	}
	hires_text_run.count = count;
	hires_text_run.laid_out = TRUE;

	return;
}

static real hires_text_glyph_offset(
	struct parse_string_state const *state,
	struct font_header *font,
	struct font_character *font_character,
	long hires_font,
	real advance)
{
	word const *characters;
	short count = 0;
	short current = 0;
	long run = NONE;

	characters = draw_string_port_run(state, &count, &current, &run);
	if (characters && characters[current] == font_character->character)
	{
		if (run != hires_text_run.run || hires_font != hires_text_run.hires_font || font != hires_text_run.font)
		{
			hires_text_run.run = run;
			hires_text_run.hires_font = hires_font;
			hires_text_run.font = font;
			hires_text_run_lay_out(font, hires_font, characters, count);
		}
		if (hires_text_run.laid_out && current < hires_text_run.count)
			return hires_text_run.offsets[current];
	}

	return (font_character->character_width - advance) * 0.5f;
}

/* port: a shifted line's colour at x (rasterizer_text_set_shift, in the
units it is drawn at once shifted): its alpha faded out toward a cut edge
that fades, over the fade's units */
static unsigned long rasterizer_text_shift_faded(
	unsigned long color,
	real x)
{
	real alpha = 1.0f;

	if (text_shift_fade_left > 0.0f)
		alpha = MIN(alpha, (x - text_shift_left) / text_shift_fade_left);
	if (text_shift_fade_right > 0.0f)
		alpha = MIN(alpha, (text_shift_right - x) / text_shift_fade_right);
	alpha = PIN(alpha, 0.0f, 1.0f);

	return (color & 0x00FFFFFF) | ((unsigned long)((real)(color >> 24) * alpha + 0.5f) << 24);
}

/* port: a character from the high-res text's atlas: the font's glyph on the
baseline, where hires_text_glyph_offset sets it in its word, cut where
draw_string cut a part of the font tag's character that shows (to the
text's box). Where it cut only the character's bitmap's empty columns or
rows, or its edge's faint pixels, the character looked whole, and so does
the glyph. */
static void rasterizer_draw_hires_glyph(
	struct parse_string_state const *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	unsigned long shadow_color,
	boolean shadow,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy)
{
	struct text_hires_glyph glyph;
	struct dynamic_screen_vertex vertices[NUMBER_OF_VERTICES_PER_QUADRILATERAL];
	real pen_x = (real)(x0 - x + font_character->bitmap_origin_x);
	real baseline = (real)(y0 - y + font_character->bitmap_origin_y);
	real clip_left = x > 0 ? (real)x0 : -32768.0f;
	real clip_top = y > 0 ? (real)y0 : -32768.0f;
	real clip_right = x + dx < font_character->bitmap_width ? (real)(x0 + dx) : 32767.0f;
	real clip_bottom = y + dy < font_character->bitmap_height ? (real)(y0 + dy) : 32767.0f;
	real left, top, right, bottom, u0, v0, u1, v1;
	short pass;

	if (!text_hires_glyph(hires_text_font, font_character->character, &glyph))
		return;
	/* (in its word, with the font's own spacing) */
	left = pen_x + hires_text_glyph_offset(state, font, font_character, hires_text_font, glyph.advance) + glyph.left;
	right = left + (glyph.right - glyph.left);
	if (x > 0 && !font_character_cut(font, font_character, 0, x, 0, font_character->bitmap_height))
		clip_left = -32768.0f;
	if (x + dx < font_character->bitmap_width &&
		!font_character_cut(font, font_character, x + dx, font_character->bitmap_width, 0, font_character->bitmap_height))
	{
		clip_right = 32767.0f;
	}
	if (y > 0 && !font_character_cut(font, font_character, 0, font_character->bitmap_width, 0, y))
		clip_top = -32768.0f;
	if (y + dy < font_character->bitmap_height &&
		!font_character_cut(font, font_character, 0, font_character->bitmap_width, y + dy, font_character->bitmap_height))
	{
		clip_bottom = 32767.0f;
	}
	/* port: a line shifted left (rasterizer_text_set_shift): cut to its
	box's edges where they are once it is shifted, then shifted */
	if (text_shift_left <= text_shift_right)
	{
		clip_left = MAX(clip_left, text_shift_left + text_shift);
		clip_right = MIN(clip_right, text_shift_right + text_shift);
	}
	top = baseline + glyph.top;
	bottom = baseline + glyph.bottom;
	u0 = glyph.u0;
	u1 = glyph.u1;
	v0 = glyph.v0;
	v1 = glyph.v1;
	if (left < clip_left)
	{
		u0 += (clip_left - left) * (u1 - u0) / (right - left);
		left = clip_left;
	}
	if (right > clip_right)
	{
		u1 -= (right - clip_right) * (u1 - u0) / (right - left);
		right = clip_right;
	}
	if (top < clip_top)
	{
		v0 += (clip_top - top) * (v1 - v0) / (bottom - top);
		top = clip_top;
	}
	if (bottom > clip_bottom)
	{
		v1 -= (bottom - clip_bottom) * (v1 - v0) / (bottom - top);
		bottom = clip_bottom;
	}
	if (right <= left || bottom <= top)
		return;
	left -= text_shift;
	right -= text_shift;

	/* (the drop shadow first, a unit down and right, as the font tags') */
	for (pass = shadow ? 0 : 1; pass < 2; pass++)
	{
		real offset = pass ? 0.0f : 1.0f;
		unsigned long vertex_color = pass ? color : shadow_color;

		vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = vertex_color;
		vertices[0].position.x = vertices[3].position.x = left + offset;
		vertices[1].position.x = vertices[2].position.x = right + offset;
		/* port: (a shifted line faded out at its cut edges) */
		if (text_shift_fade_left > 0.0f || text_shift_fade_right > 0.0f)
		{
			vertices[0].color = vertices[3].color = rasterizer_text_shift_faded(vertex_color, left + offset);
			vertices[1].color = vertices[2].color = rasterizer_text_shift_faded(vertex_color, right + offset);
		}
		vertices[0].position.y = vertices[1].position.y = top + offset;
		vertices[2].position.y = vertices[3].position.y = bottom + offset;
		vertices[0].texture_coordinates.x = vertices[3].texture_coordinates.x = u0;
		vertices[1].texture_coordinates.x = vertices[2].texture_coordinates.x = u1;
		vertices[0].texture_coordinates.y = vertices[1].texture_coordinates.y = v0;
		vertices[2].texture_coordinates.y = vertices[3].texture_coordinates.y = v1;
		rasterizer_text_draw_scaled_character(vertices);
	}

	return;
}

/* port: a place in units to the nearest whole unit (a half up) */
static short drawn_span_unit(
	real units)
{
	short whole = (short)units;

	if ((real)whole > units)
		whole--;
	return (short)(units - whole >= 0.5f ? whole + 1 : whole);
}

/* port: where the high-res text draws a font tag's character whose bitmap
is drawn from x0 (from its column x), in the draw mode's font, as
rasterizer_draw_hires_glyph places it: its ink's left and right, in
fractions of a unit; FALSE (left and right as they were) where it is drawn
from the tag's bitmap (the high-res text off, its font or the glyph not
made yet) */
boolean rasterizer_text_port_drawn_span_real(
	struct parse_string_state const *state,
	struct font_character const *font_character,
	short x0,
	short x,
	real *left,
	real *right)
{
	struct text_hires_glyph glyph;
	long hires_font;
	real ink_left;

	if (!hires_text_atlas || !font_character)
		return FALSE;
	hires_font = hires_text_font_get(draw_string_get_font(), MAX(text_scale, 1.0f));
	if (hires_font == NONE || !text_hires_glyph(hires_font, font_character->character, &glyph))
	{
		return FALSE;
	}
	ink_left = (real)(x0 - x + font_character->bitmap_origin_x) +
		hires_text_glyph_offset(state, draw_string_port_font(state), (struct font_character *)font_character, hires_font,
			glyph.advance) + glyph.left;
	/* (its outline's own left and right, inside the quad's whole pixels of
	the atlas: text_hires.c) */
	*left = ink_left + (glyph.ink_left - glyph.left);
	*right = ink_left + (glyph.ink_right - glyph.left);

	return TRUE;
}

/* port: as rasterizer_text_port_drawn_span_real, the glyph's quad's left and
right (its ink to whole pixels of the atlas) to the nearest whole units */
boolean rasterizer_text_port_drawn_span(
	struct parse_string_state const *state,
	struct font_character const *font_character,
	short x0,
	short x,
	short *left,
	short *right)
{
	struct text_hires_glyph glyph;
	long hires_font;
	real ink_left;

	if (!hires_text_atlas || !font_character)
		return FALSE;
	hires_font = hires_text_font_get(draw_string_get_font(), MAX(text_scale, 1.0f));
	if (hires_font == NONE || !text_hires_glyph(hires_font, font_character->character, &glyph))
	{
		return FALSE;
	}
	ink_left = (real)(x0 - x + font_character->bitmap_origin_x) +
		hires_text_glyph_offset(state, draw_string_port_font(state), (struct font_character *)font_character, hires_font,
			glyph.advance) + glyph.left;
	*left = drawn_span_unit(ink_left);
	*right = drawn_span_unit(ink_left + (glyph.right - glyph.left));

	return TRUE;
}

static void rasterizer_draw_hires_character(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy)
{
	rasterizer_draw_hires_glyph(state, font, font_character, color, 0, FALSE, x0, y0, x, y, dx, dy);

	return;
}

static void rasterizer_draw_hires_character_with_dropshadow(
	struct parse_string_state *state,
	struct font_header *font,
	struct font_character *font_character,
	unsigned long color,
	short x0,
	short y0,
	short x,
	short y,
	short dx,
	short dy)
{
	unsigned long shadow_color = global_shadow_color ?
		global_shadow_color :
		(color & 0xFF000000);

	rasterizer_draw_hires_glyph(state, font, font_character, color, shadow_color, TRUE, x0, y0, x, y, dx, dy);

	return;
}

static struct bitmap_data *
hardware_character_cache_get_bitmap(
	void)
{
	return hardware_character_cache.initialized ? hardware_character_cache.bitmap : NULL;
}

static void
hardware_character_cache_get_origin(
	short hardware_character_index,
	short *x0,
	short *y0)
{
	struct hardware_character *hardware_character =
		&hardware_character_cache.characters[hardware_character_index];

	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 597, hardware_character_cache.initialized);
	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 598, hardware_character_index>=0 && hardware_character_index<MAXIMUM_HARDWARE_CHARACTERS);
	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 599, x0 && y0);

	*x0 = hardware_character->x0 + hardware_character_padding(hardware_character->character);
	*y0 = hardware_character->y0 + hardware_character_padding(hardware_character->character);

	return;
}

/* port: the clear texels around a character in the cache (its cell is that
much larger each side). The cache packs characters edge to edge, which the
Xbox's 640x480 sampled texel for pixel; drawn larger (a fullscreen
display's resolution), each edge pixel blends in half a texel past the
character, from the next character or one the cache dropped, and the text
and its drop shadow showed the cells' borders. A character too large for
the border goes without. */
static short hardware_character_padding(
	struct font_character const *font_character)
{
	return font_character &&
		font_character->bitmap_width + 2 <= HARDWARE_CHARACTER_CACHE_BITMAP_WIDTH &&
		font_character->bitmap_height + 2 <= HARDWARE_CHARACTER_CACHE_BITMAP_HEIGHT ? 1 : 0;
}

static void
flush_hardware_character(
	struct hardware_character *hardware_character)
{
	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 610, hardware_character);

	if (hardware_character->character)
	{
		hardware_character->character->hardware_character_index = NONE;

		if (hardware_character->character->pad == magic_number)
			error(_error_log, "font cache overwrote character in use");

		hardware_character->character = NULL;
	}

	return;
}

static void
cache_hardware_format_character(
	struct font_header *font,
	struct font_character *font_character)
{
	match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 633, hardware_character_cache.initialized);

	if (font_character->hardware_character_index != NONE)
	{
		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 637, font_character->hardware_character_index>=0 && font_character->hardware_character_index<MAXIMUM_HARDWARE_CHARACTERS);
		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 638, font_character==hardware_character_cache.characters[font_character->hardware_character_index].character);
	}
	else
	{
		struct hardware_character *hardware_character;
		byte *source;
		short y0, y1;
		short x, y;
		short next_write_index;
		short padding = hardware_character_padding(font_character);
		short cell_width = font_character->bitmap_width + 2 * padding;
		short cell_height = font_character->bitmap_height + 2 * padding;

		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 645, font_character->bitmap_width<=HARDWARE_CHARACTER_CACHE_BITMAP_WIDTH);
		match_assert("c:\\halo\\SOURCE\\rasterizer\\rasterizer_text.c", 646, font_character->bitmap_height<=HARDWARE_CHARACTER_CACHE_BITMAP_HEIGHT);

		font_character->pad = magic_number;

		if (cell_width + hardware_character_cache.x0 > HARDWARE_CHARACTER_CACHE_BITMAP_WIDTH)
		{
			hardware_character_cache.x0 = 0;
			hardware_character_cache.y0 += hardware_character_cache.maximum_character_height;
			hardware_character_cache.maximum_character_height = 0;
		}

		if (cell_height + hardware_character_cache.y0 > HARDWARE_CHARACTER_CACHE_BITMAP_HEIGHT)
		{
			hardware_character_cache.y0 = 0;
			hardware_character_cache.x0 = 0;
			hardware_character_cache.maximum_character_height = 0;

			for (;
				hardware_character_cache.read_index != hardware_character_cache.write_index;
				hardware_character_cache.read_index = (hardware_character_cache.read_index + 1) & (MAXIMUM_HARDWARE_CHARACTERS - 1))
			{
				hardware_character = &hardware_character_cache.characters[hardware_character_cache.read_index];

				if (hardware_character->y0 <= 0)
					break;

				flush_hardware_character(hardware_character);
			}
		}

		if (cell_height > hardware_character_cache.maximum_character_height)
		{
			y0 = hardware_character_cache.y0 + hardware_character_cache.maximum_character_height;
			y1 = hardware_character_cache.y0 + cell_height;

			for (;
				hardware_character_cache.read_index != hardware_character_cache.write_index;
				hardware_character_cache.read_index = (hardware_character_cache.read_index + 1) & (MAXIMUM_HARDWARE_CHARACTERS - 1))
			{
				hardware_character = &hardware_character_cache.characters[hardware_character_cache.read_index];

				if (hardware_character->y0 < y0 || hardware_character->y0 >= y1)
					break;

				flush_hardware_character(hardware_character);
			}

			hardware_character_cache.maximum_character_height = cell_height;
		}

		next_write_index = (hardware_character_cache.write_index + 1) & (MAXIMUM_HARDWARE_CHARACTERS - 1);
		if (next_write_index == hardware_character_cache.read_index)
		{
			flush_hardware_character(&hardware_character_cache.characters[hardware_character_cache.read_index]);
			hardware_character_cache.read_index = (hardware_character_cache.read_index + 1) & (MAXIMUM_HARDWARE_CHARACTERS - 1);
		}

		hardware_character = &hardware_character_cache.characters[hardware_character_cache.write_index];
		font_character->hardware_character_index = hardware_character_cache.write_index;

		hardware_character->character = font_character;
		hardware_character->x0 = hardware_character_cache.x0;
		hardware_character->y0 = hardware_character_cache.y0;

		source = (byte *)font->pixels.address + font_character->pixels_offset;

		/* (port: the border clear, white with no alpha as the character's
		own clear texels are) */
		for (y = 0; y < cell_height; y++)
		{
			word *destination = (word *)bitmap_2d_address(
				hardware_character_cache.bitmap,
				hardware_character->x0,
				(short)(hardware_character->y0 + y),
				0);

			for (x = 0; x < cell_width; x++)
			{
				if (y < padding || y >= padding + font_character->bitmap_height ||
					x < padding || x >= padding + font_character->bitmap_width)
				{
					*destination++ = 0x0FFF;
				}
				else
				{
					*destination++ = (word)((*source++ << 8) | 0x0FFF);
				}
			}
		}

		rasterizer_bitmap_changed(hardware_character_cache.bitmap);

		hardware_character_cache.x0 += cell_width;
		hardware_character_cache.write_index = (hardware_character_cache.write_index + 1) & (MAXIMUM_HARDWARE_CHARACTERS - 1);
	}

	return;
}
