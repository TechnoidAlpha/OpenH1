/*
HUD_KEY_PROMPTS.C

The HUD's button prompts for the keyboard and mouse (input.button_prompts,
"auto" by default): while the first player plays with the keyboard and
mouse, a prompt's controller button ("Hold [X] to pick up", the campaign's
"Press [flashlight]") is drawn as the key or mouse button bound to the
action instead, its name in the prompt's font and colour in a drawn key
cap, as Halo PC named the bound key; while they play with a controller (or
the touch controls) it is the controller's button, as before. The keys are
config.toml's [controls] (Settings > Controls Setup), read as they change,
so a prompt follows a new binding at once (port/linux/src/xinput_sdl.c:
halo_keyboard_prompts, halo_keyboard_action_name).

The game names a prompt's button by a game control (the action, the
flashlight) or by a controller button; a button is the control the
player's profile puts on it (source/input/input_abstraction.c:
input_abstraction_port_keyboard_action). The keyboard's player is the first
local player's; what the keyboard has no action for (the D-pad, the menus'
accept and back) keeps the controller's button. Movement is the four
movement keys ("WASD"), looking the mouse.

In VR (HALO_VR) a prompt names what presses its controller button on
the controllers in the hands instead, as the OpenXR runtime names the
input bound to it, or the gesture that does (port/linux/src/vr.c:
halo_vr_button_name), unless input.button_prompts is "controller".

Only what this machine draws changes: nothing is sent, and nothing the
machines of a network game agree on.
*/

#include "cseries.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "cutscene/cinematics.h"
#include "game/game_globals.h"
#include "game/players.h"
#include "input/input_abstraction.h"
#include "rasterizer/rasterizer.h"
#include "scenario/scenario.h"
#include "text/draw_string.h"
#include "halo_keyboard.h"

#include <math.h>

/* port/linux/src/d3d8_device.c: the display's pixels for each of the 480
lines */
float halo_screen_pixel_scale(void);

#ifdef HALO_VR
/* port/linux/src/vr.c (vr.h) */
int halo_vr_running(void);
int halo_vr_button_name(int button, char *name, unsigned long size);
char const *config_string(char const *name);
#endif

/* ---------- constants */

/* the HUD's icons of the sticks (hud_messaging.c's _icon_left_stick and
_icon_right_stick, and the controller buttons' indices before them) */
enum
{
	HUD_KEY_PROMPT_LEFT_STICK = 16,
	HUD_KEY_PROMPT_RIGHT_STICK = 17
};

/* ---------- prototypes */

boolean hud_key_prompt_text(short local_player_index, short game_control, short gamepad_button,
	wchar_t *text, long length);
boolean hud_key_prompt_draw(short local_player_index, short game_control, short gamepad_button,
	rectangle2d *bounds, rectangle2d *cursor, pixel32 color);
boolean hud_key_prompt_draw_key(char const *name, rectangle2d *bounds, rectangle2d *cursor, pixel32 color);
short hud_key_prompt_cap_widening(short name_width);

/* ---------- private code */

static void latin1_to_wide(char const *name, wchar_t *text, long length)
{
	long index;

	for (index = 0; index < length - 1 && name[index]; index++)
		text[index] = (wchar_t)(unsigned char)name[index];
	text[index] = 0;
}

/* the movement keys as one name: "WASD" for single letters, else each
separated by a slash */
static void movement_name(char *name, long size)
{
	static short const actions[] =
	{
		HALO_KEYBOARD_MOVE_FORWARD,
		HALO_KEYBOARD_STRAFE_LEFT,
		HALO_KEYBOARD_MOVE_BACKWARD,
		HALO_KEYBOARD_STRAFE_RIGHT
	};
	char keys[NUMBEROF(actions)][32];
	boolean letters = TRUE;
	long index, used = 0;

	name[0] = 0;
	for (index = 0; index < NUMBEROF(actions); index++)
	{
		if (!halo_keyboard_action_name(actions[index], keys[index], sizeof(keys[index])))
		{
			name[0] = 0;
			return;
		}
		letters = letters && keys[index][1] == 0;
	}
	for (index = 0; index < NUMBEROF(actions) && used < size - 1; index++)
	{
		char const *key = keys[index];

		if (!letters && index > 0 && used < size - 1)
			name[used++] = '/';
		while (*key && used < size - 1)
			name[used++] = *key++;
	}
	name[used] = 0;
	return;
}

#ifdef HALO_VR
/* in the headset: what presses the prompt's controller button on the
controllers in the hands (a game control's, the player's profile puts it
on), as the runtime names it, or the gesture that does (port/linux/src/
vr.c: halo_vr_button_name); FALSE for the sticks' moving and looking, and
with input.button_prompts "controller", which keep the button */
static boolean vr_prompt_text(
	short local_player_index,
	short game_control,
	short gamepad_button,
	wchar_t *text,
	long length)
{
	struct game_input_preferences preferences;
	char name[64];

	if (!strcmp(config_string("input.button_prompts"), "controller"))
		return FALSE;
	if (game_control != NONE)
	{
		if (game_control < 0 || game_control >= NUMBEROF(preferences.game_control_to_xbox_buttons))
			return FALSE;
		input_abstraction_get_local_player_preferences(local_player_index, &preferences);
		gamepad_button = preferences.game_control_to_xbox_buttons[game_control];
	}
	if (gamepad_button == NONE || gamepad_button >= HUD_KEY_PROMPT_LEFT_STICK ||
		!halo_vr_button_name(gamepad_button, name, sizeof(name)))
	{
		return FALSE;
	}
	latin1_to_wide(name, text, length);
	return TRUE;
}
#endif

/* ---------- public code */

/* a key cap's edge's width and its corners' radius, in the HUD's units at
full size; each is drawn as whole pixels of the display, the cap's sides
on the pixels' edges, so that its four sides are as wide at any
resolution */
#define KEY_CAP_EDGE 1.25f
#define KEY_CAP_RADIUS 3.5f
/* (where the screen's unit 0 falls, in its units: the Xbox's pixels'
centres, rasterizer_xbox_dynavobgeom.c) */
#define KEY_CAP_PIXEL_ORIGIN 0.5f

/* the key cap's margin about its name, the text's leading inset, and half
its height, in the HUD's units (as hud_hires_button_draw's) */
static void key_cap_metrics(short *margin, short *inset, short *half)
{
	real scale = local_player_count() > 1 ? 0.75f : 1.0f;

	*margin = (short)(3.0f * scale);
	*inset = (short)(4.0f * scale);
	*half = (short)(10.0f * scale);
}

/* a part of a key cap: a quadrilateral, its corners in the display's
pixels and their colours, the colours blended between them */
static void key_cap_quad(struct bitmap_data *white, real pixels, real const x[4], real const y[4],
	pixel32 const color[4])
{
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	struct dynamic_screen_vertex vertices[4];
	short index;

	for (index = 0; index < 4; index++)
	{
		vertices[index].position.x = x[index] / pixels + KEY_CAP_PIXEL_ORIGIN;
		vertices[index].position.y = y[index] / pixels + KEY_CAP_PIXEL_ORIGIN;
		vertices[index].texture_coordinates.x = 0.0f;
		vertices[index].texture_coordinates.y = 0.0f;
		vertices[index].color = color[index];
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map[0] = white;
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	rasterizer_globals.current_lock_operation = _rasterizer_lock_cinematics;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
}

static void key_cap_rectangle(struct bitmap_data *white, real pixels, real x0, real y0, real x1, real y1,
	pixel32 color)
{
	real x[4], y[4];
	pixel32 colors[4];

	if (x1 <= x0 || y1 <= y0)
		return;
	x[0] = x0; y[0] = y0;
	x[1] = x1; y[1] = y0;
	x[2] = x1; y[2] = y1;
	x[3] = x0; y[3] = y1;
	colors[0] = colors[1] = colors[2] = colors[3] = color;
	key_cap_quad(white, pixels, x, y, colors);
}

/* a corner's band between two radii about its centre (from the centre for
inner 0), towards side_x and side_y, its colour at each radius */
static void key_cap_corner(struct bitmap_data *white, real pixels, real center_x, real center_y,
	real side_x, real side_y, real inner, real outer, pixel32 inner_color, pixel32 outer_color, short segments)
{
	/* (each quadrilateral's corners turning the same way as the
	rectangles', which the renderer may cull otherwise) */
	short turn = side_x * side_y < 0.0f ? 1 : 0;
	short segment;

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
		key_cap_quad(white, pixels, x, y, colors);
	}
}

/* a key cap about box (in the HUD's units, laid out as the text is, and
drawn as it is: rasterizer_text_set_scale, the scoreboard's prompts):
filled, edged, its corners rounded, their curves' edges blended over a
pixel */
static void key_cap_shape(rectangle2d const *box, pixel32 fill, pixel32 color)
{
	static real const sides[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
	struct game_globals *game_globals = scenario_get_game_globals();
	struct game_globals_rasterizer_data *rasterizer_data;
	struct bitmap_data *white;
	real text_scale, origin_x, origin_y, pixels, size;
	real x0, y0, x1, y1, edge, radius;
	pixel32 clear = color & 0x00FFFFFFUL;
	short segments, corner;

	if (!game_globals || !game_globals->rasterizer_data.count)
		return;
	rasterizer_data = TAG_BLOCK_GET_ELEMENT(&game_globals->rasterizer_data, 0, struct game_globals_rasterizer_data);
	white = TAG_BLOCK_GET_ELEMENT(&bitmap_group_get(rasterizer_data->default_textures[0].index)->bitmaps, 1,
		struct bitmap_data);
	rasterizer_text_get_scale(&text_scale, &origin_x, &origin_y);
	pixels = halo_screen_pixel_scale();
	if (pixels <= 0.0f)
		pixels = 1.0f;
	/* (the box in whole pixels; the edge and the radius in pixels of the
	cap as large as it is drawn) */
	x0 = floorf((origin_x + (box->x0 - origin_x) * text_scale - KEY_CAP_PIXEL_ORIGIN) * pixels + 0.5f);
	x1 = floorf((origin_x + (box->x1 - origin_x) * text_scale - KEY_CAP_PIXEL_ORIGIN) * pixels + 0.5f);
	y0 = floorf((origin_y + (box->y0 - origin_y) * text_scale - KEY_CAP_PIXEL_ORIGIN) * pixels + 0.5f);
	y1 = floorf((origin_y + (box->y1 - origin_y) * text_scale - KEY_CAP_PIXEL_ORIGIN) * pixels + 0.5f);
	size = (local_player_count() > 1 ? 0.75f : 1.0f) * text_scale * pixels;
	edge = MAX(floorf(KEY_CAP_EDGE * size + 0.5f), 1.0f);
	radius = floorf(KEY_CAP_RADIUS * size + 0.5f);
	radius = MIN(radius, floorf(MIN(x1 - x0, y1 - y0) / 2.0f));
	radius = MAX(radius, edge);
	if (x1 - x0 < 2.0f * radius || y1 - y0 < 2.0f * radius)
		return;
	/* (enough segments that a curve looks round at any size) */
	segments = (short)MAX(2, MIN(8, (long)(radius / 2.0f)));

	/* the fill: the middle, above and below it, and the corners */
	key_cap_rectangle(white, pixels, x0, y0 + radius, x1, y1 - radius, fill);
	key_cap_rectangle(white, pixels, x0 + radius, y0, x1 - radius, y0 + radius, fill);
	key_cap_rectangle(white, pixels, x0 + radius, y1 - radius, x1 - radius, y1, fill);
	/* the edge over it: the four sides, between the corners */
	key_cap_rectangle(white, pixels, x0 + radius, y0, x1 - radius, y0 + edge, color);
	key_cap_rectangle(white, pixels, x0 + radius, y1 - edge, x1 - radius, y1, color);
	key_cap_rectangle(white, pixels, x0, y0 + radius, x0 + edge, y1 - radius, color);
	key_cap_rectangle(white, pixels, x1 - edge, y0 + radius, x1, y1 - radius, color);
	for (corner = 0; corner < 4; corner++)
	{
		real center_x = sides[corner][0] < 0.0f ? x0 + radius : x1 - radius;
		real center_y = sides[corner][1] < 0.0f ? y0 + radius : y1 - radius;

		key_cap_corner(white, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
			0.0f, radius, fill, fill, segments);
		key_cap_corner(white, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
			radius - edge, radius, color, color, segments);
		/* (the curves' edges blended: outside the edge, and inside it) */
		key_cap_corner(white, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
			radius, radius + 1.0f, color, clear, segments);
		if (radius - edge >= 1.0f)
		{
			key_cap_corner(white, pixels, center_x, center_y, sides[corner][0], sides[corner][1],
				radius - edge - 1.0f, radius - edge, clear, color, segments);
		}
	}
}

/* how much wider than its name and the edges about it a key's cap is
drawn: a narrow name's (a digit, a letter) is as wide as the cap is tall,
the name in its middle. name_width is the name's width in the font it is
drawn in (the HUD's units) */
short hud_key_prompt_cap_widening(short name_width)
{
	short margin, inset, half;

	key_cap_metrics(&margin, &inset, &half);
	return (short)MAX(0, (2 * half - 1) - (name_width + 2 * margin + 1 - inset));
}

/* the key's name at the cursor in a key cap, as the HUD draws a button
there, the cursor moved past it */
static boolean key_cap_draw(wchar_t const *text, rectangle2d *bounds, rectangle2d *cursor, pixel32 color)
{
	rectangle2d text_bounds, box, name_start;
	short margin, inset, half, line_top, middle, widening;
	short start;
	pixel32 fill;

	key_cap_metrics(&margin, &inset, &half);
	start = cursor->x0;
	line_top = cursor->y0;
	cursor->x0 += margin;
	name_start = *cursor;
	draw_string_set_indents(cursor->x0 - bounds->x0, 0);
	draw_unicode_string_compute_bounds(bounds, text, &text_bounds, cursor);
	/* (a narrow name measured again, moved to the middle of its cap) */
	widening = cursor->y0 == line_top ? hud_key_prompt_cap_widening((short)(cursor->x0 - 3 - name_start.x0)) : 0;
	if (widening > 0)
	{
		*cursor = name_start;
		cursor->x0 = (short)(cursor->x0 + widening / 2);
		draw_string_set_indents(cursor->x0 - bounds->x0, 0);
		draw_unicode_string_compute_bounds(bounds, text, &text_bounds, cursor);
	}
	cursor->x0 -= 3;
	box.x0 = (short)(start + inset);
	box.x1 = (short)(cursor->x0 + 4 + margin + widening - widening / 2);
	if (cursor->y0 == line_top)
	{
		/* a key cap: the prompt's colour faintly filled, edged in it */
		middle = draw_unicode_string_capital_middle(cursor, L"");
		box.y0 = (short)(middle - half + 1);
		box.y1 = (short)(middle + half);
		fill = ((pixel32)((color >> 24) * 3 / 10) << 24) | (color & 0x00FFFFFFUL);
		key_cap_shape(&box, fill, color);
	}
	text_bounds.x0 = bounds->x0;
	rasterizer_draw_unicode_string(&text_bounds, NULL, NULL, 0, text);
	bounds->y0 = cursor->y0;
	/* (the text that follows starts past the cap) */
	cursor->x0 = (short)(box.x1 - inset + 1);
	return TRUE;
}

/* the key a prompt names for the keyboard and mouse, for a game control or
(game_control NONE) a controller button; FALSE when the keyboard has none */
boolean hud_key_prompt_text(
	short local_player_index,
	short game_control,
	short gamepad_button,
	wchar_t *text,
	long length)
{
	char name[64];
	short action;

	name[0] = 0;
	if (game_control == NONE && gamepad_button == NONE)
		return FALSE;
	if (game_control == NONE && gamepad_button == HUD_KEY_PROMPT_LEFT_STICK)
	{
		movement_name(name, sizeof(name));
	}
	else if (game_control == NONE && gamepad_button == HUD_KEY_PROMPT_RIGHT_STICK)
	{
		csstrcpy(name, "Mouse");
	}
	else
	{
		action = input_abstraction_port_keyboard_action(local_player_index, game_control, gamepad_button);
		if (action != NONE)
			halo_keyboard_action_name(action, name, sizeof(name));
	}
	if (!name[0])
		return FALSE;
	latin1_to_wide(name, text, length);
	return TRUE;
}

/* draws the prompt's key at the cursor, as the HUD draws a button there,
and moves the cursor past it; FALSE (nothing drawn) when the prompt keeps
the controller's button */
boolean hud_key_prompt_draw(
	short local_player_index,
	short game_control,
	short gamepad_button,
	rectangle2d *bounds,
	rectangle2d *cursor,
	pixel32 color)
{
	wchar_t text[64];

	if (local_player_index != 0)
		return FALSE;
#ifdef HALO_VR
	if (halo_vr_running())
	{
		if (!vr_prompt_text(local_player_index, game_control, gamepad_button, text, NUMBEROF(text)))
			return FALSE;
	}
	else
#endif
	if (!halo_keyboard_prompts() ||
		!hud_key_prompt_text(local_player_index, game_control, gamepad_button, text, NUMBEROF(text)))
	{
		return FALSE;
	}
	return key_cap_draw(text, bounds, cursor, color);
}

/* the menus' key of their own (Settings' tabs' Q and E for LB and RB:
port/linux/src/xinput_sdl.c), drawn as the prompts' keys are while the
keyboard and mouse play; FALSE (nothing drawn) while a controller does */
boolean hud_key_prompt_draw_key(
	char const *name,
	rectangle2d *bounds,
	rectangle2d *cursor,
	pixel32 color)
{
	wchar_t text[16];

#ifdef HALO_VR
	if (halo_vr_running())
		return FALSE;
#endif
	if (!halo_keyboard_prompts())
		return FALSE;
	latin1_to_wide(name, text, NUMBEROF(text));
	return key_cap_draw(text, bounds, cursor, color);
}
