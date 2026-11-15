/*
HUD_KEY_PROMPTS.C

The HUD's button prompts for the keyboard and mouse (input.button_prompts,
"auto" by default): while the first player plays with the keyboard and
mouse, a prompt's controller button ("Hold [X] to pick up", the campaign's
"Press [flashlight]") is drawn as the key or mouse button bound to the
action instead, its name in the prompt's font and colour in a drawn key
cap, as Halo PC named the bound key; while they play with a controller (or
the touch controls) it is the controller's button, as before. An action
with no key bound (Controls Setup's "-") is named Unbound, never the
controller's button, which the player is not holding. The keys are
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
#include "interface/hud.h"
#include "interface/hud_definitions.h"
#include "interface/hud_draw.h"
#include "interface/interface.h"
#include "rasterizer/rasterizer.h"
#include "scenario/scenario.h"
#include "text/draw_string.h"
#include "halo_keyboard.h"
#include "player_settings.h"
#include "controller_buttons.h"

#include <math.h>

/* port/linux/game/menu_shapes.c */
real menu_shape_pixels(void);
void menu_shape_rounded_box_pixels(real x0, real y0, real x1, real y1, real radius, real edge, pixel32 fill,
	pixel32 color);

#ifdef HALO_VR
/* port/linux/src/vr.c (vr.h) */
int halo_vr_running(void);
int halo_vr_button_name(int button, char *name, unsigned long size);
char const *config_string(char const *name);
#endif

/* ---------- constants */

/* the HUD's icons of the Back button and the sticks (hud_messaging.c's
_icon_back_button, _icon_left_stick and _icon_right_stick, and the
controller buttons' indices before them) */
enum
{
	HUD_KEY_PROMPT_BACK_BUTTON = 13,
	HUD_KEY_PROMPT_LEFT_STICK = 16,
	HUD_KEY_PROMPT_RIGHT_STICK = 17,
	/* a message icon's sprite smaller than this many texels each way is
	no picture: the Xbox's sheet marks the buttons it has none of (Start,
	Back, the D-pad) with empty cells of 4 by 4 */
	HUD_KEY_PROMPT_MINIMUM_SPRITE = 8
};

/* ---------- prototypes */

boolean hud_key_prompt_text(short local_player_index, short game_control, short gamepad_button,
	wchar_t *text, long length);
void hud_key_prompt_action_name(short action, char *name, long size);
boolean hud_key_prompt_draw(short local_player_index, short game_control, short gamepad_button,
	rectangle2d *bounds, rectangle2d *cursor, pixel32 color);
boolean hud_key_prompt_draw_key(char const *name, rectangle2d *bounds, rectangle2d *cursor, pixel32 color);
short hud_key_prompt_cap_advance(short name_left, short name_right);
short hud_key_prompt_legend_advance(short name_left, short name_right, wchar_t const *words);
long hud_key_prompt_draw_equals(rectangle2d *bounds, rectangle2d *cursor, wchar_t const *text);
void hud_key_prompt_button_word(wchar_t *text, long length, wchar_t const *word, wchar_t const *icon);
void hud_key_prompt_score_hint(short local_player_index, wchar_t *text, long length);

/* ---------- private code */

static void latin1_to_wide(char const *name, wchar_t *text, long length)
{
	long index;

	for (index = 0; index < length - 1 && name[index]; index++)
		text[index] = (wchar_t)(unsigned char)name[index];
	text[index] = 0;
}

/* the movement keys as one name: "WASD" for single letters, else each
separated by a slash, one with no key as Unbound; Unbound for none */
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
	boolean letters = TRUE, bound = FALSE;
	long index, used = 0;

	name[0] = 0;
	for (index = 0; index < NUMBEROF(actions); index++)
	{
		if (halo_keyboard_action_name(actions[index], keys[index], sizeof(keys[index])))
			bound = TRUE;
		else
			hud_key_prompt_action_name(NONE, keys[index], sizeof(keys[index]));
		letters = letters && keys[index][1] == 0;
	}
	if (!bound)
	{
		hud_key_prompt_action_name(NONE, name, size);
		return;
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

	if (!strcmp(config_player_string(local_player_index, "input.button_prompts"), "controller"))
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

/* a key cap's measures, in the HUD's units (as hud_hires_button_draw's):
how far past the cursor its box starts (inset), the room between its name's
letters and either side of it (margin), the room between it and the
letters of the text after it (gap), and half its height */
static void key_cap_metrics(short *inset, short *margin, short *gap, short *half)
{
	real scale = local_player_count() > 1 ? 0.75f : 1.0f;

	*inset = (short)(4.0f * scale);
	*margin = (short)(3.0f * scale);
	*gap = (short)(3.0f * scale);
	*half = (short)(10.0f * scale);
}

/* a key cap laid out for a name whose letters (their glyphs' boxes, as
draw_unicode_string_compute_bounds has them) span left to right when it is
drawn from 0, all from the cursor at 0: its box's sides, where its name is
drawn from (its letters in the middle of the box, as much room either side
of them), and where the text after it is drawn from (its letters, which
start as far past their pen as the name's, the gap past the box). A box is
at least as wide as it is tall, so a narrow name (a digit, a letter) is not
in a box taller than wide */
static void key_cap_layout(short left, short right, short *box_x0, short *box_x1, short *pen, short *after)
{
	short inset, margin, gap, half, letters, width;

	key_cap_metrics(&inset, &margin, &gap, &half);
	letters = (short)MAX(right - left, 0);
	width = (short)MAX(letters + 2 * margin, 2 * half - 1);
	/* (whole units either side: a box an odd unit wider would leave the
	name half a unit off its middle) */
	if ((width - letters) % 2)
		width++;
	*box_x0 = inset;
	*box_x1 = (short)(inset + width);
	*pen = (short)(inset + (width - letters) / 2 - left);
	*after = (short)(*box_x1 + gap - left);
}

/* a key cap about a box (in the HUD's units, laid out as the text is, and
drawn as it is: rasterizer_text_set_scale, the scoreboard's prompts), its
sides in fractions of a unit: filled, edged, its corners rounded, their
curves' edges blended over a pixel. The box is drawn whole pixels wide and
high, its middle at the nearest half pixel to its own, so that what it is
centred on (its name's letters, their capitals) is in its middle at any
resolution */
static void key_cap_shape(real box_x0, real box_y0, real box_x1, real box_y1, pixel32 fill, pixel32 color)
{
	real text_scale, origin_x, origin_y, pixels, size;
	real middle_x, middle_y, width, height, x0, y0;

	rasterizer_text_get_scale(&text_scale, &origin_x, &origin_y);
	pixels = menu_shape_pixels();
	/* (the box's middle and size in the display's pixels; the edge and the
	radius in pixels of the cap as large as it is drawn) */
	middle_x = (origin_x + ((box_x0 + box_x1) * 0.5f - origin_x) * text_scale - KEY_CAP_PIXEL_ORIGIN) * pixels;
	middle_y = (origin_y + ((box_y0 + box_y1) * 0.5f - origin_y) * text_scale - KEY_CAP_PIXEL_ORIGIN) * pixels;
	width = floorf((box_x1 - box_x0) * text_scale * pixels + 0.5f);
	height = floorf((box_y1 - box_y0) * text_scale * pixels + 0.5f);
	x0 = floorf(middle_x - width * 0.5f + 0.5f);
	y0 = floorf(middle_y - height * 0.5f + 0.5f);
	size = (local_player_count() > 1 ? 0.75f : 1.0f) * text_scale * pixels;
	menu_shape_rounded_box_pixels(x0, y0, x0 + width, y0 + height, floorf(KEY_CAP_RADIUS * size + 0.5f),
		MAX(floorf(KEY_CAP_EDGE * size + 0.5f), 1.0f), fill, color);
}

/* the last key cap drawn (key_cap_draw): the top of its line, and where
the letters after it start (its edge and the gap); SHORT_MIN for none */
static short key_cap_after_line = SHORT_MIN;
static short key_cap_letters_x;

/* the "=" between a key's cap and its word (the Xbox's legends'
"=CANCEL", issue 596), from where the letters after the cap start (its
edge and the gap, 0 here): where the "=" is drawn from and where the word
is, for an "=" that advances equals_advance. The "=" takes its whole
advance and KEY_CAP_EQUALS_ROOM either side of it, from the cap's edge to
the word's pen: the same room either side by the advances, which the
high-res text centres its glyphs on (rasterizer_text.c's
rasterizer_draw_hires_glyph), not by the font tag's glyphs' boxes, whose
blank columns are not the drawn letters'. (The "='s" own side bearings
are room enough beside the room: a legend's row is no wider than the
panel the Xbox drew it in has room for) */
#define KEY_CAP_EQUALS_ROOM 1
static void key_cap_equals_layout(short equals_advance, short *equals_pen, short *word_pen)
{
	short inset, margin, gap, half;

	key_cap_metrics(&inset, &margin, &gap, &half);
	*equals_pen = (short)(KEY_CAP_EQUALS_ROOM - gap);
	*word_pen = (short)(*equals_pen + equals_advance + KEY_CAP_EQUALS_ROOM);
}

/* where text's letters span drawn from 0 in the draw mode's font, as one
line (their ink where it is drawn: draw_unicode_string_compute_drawn_bounds),
and its advance (where the next text would be drawn from); FALSE for
none (no letter with ink) */
static boolean key_text_measure(wchar_t const *text, short *left, short *right, short *advance)
{
	rectangle2d bounds, text_bounds, cursor;

	*left = *right = *advance = 0;
	if (!text || !*text)
		return FALSE;
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = SHORT_MAX;
	bounds.y1 = SHORT_MAX;
	cursor = bounds;
	draw_string_set_indents(0, 0);
	draw_unicode_string_compute_drawn_bounds(&bounds, text, &text_bounds, &cursor);
	if (text_bounds.x0 > text_bounds.x1 || text_bounds.x1 == SHORT_MIN)
		return FALSE;
	*left = text_bounds.x0;
	*right = text_bounds.x1;
	*advance = cursor.x0;
	return TRUE;
}

static boolean key_text_span(wchar_t const *text, short *left, short *right)
{
	short advance;

	return key_text_measure(text, left, right, &advance);
}

/* the length of the "=" a legend's words start with (the maps' "=CANCEL",
and " =CANCEL" with the spaces before it), 0 for none */
static long key_equals_length(wchar_t const *words)
{
	long length = 0;

	while (words && words[length] == L' ')
		length++;
	return words && words[length] == L'=' ? length + 1 : 0;
}

/* how far past the cursor the text after a key's cap starts (the cap's
advance), for a name whose letters span left to right drawn from 0 in the
font it is drawn in (the HUD's units: draw_unicode_string_compute_bounds's
text bounds), as key_cap_draw lays it out */
short hud_key_prompt_cap_advance(short name_left, short name_right)
{
	short box_x0, box_x1, pen, after;

	key_cap_layout(name_left, name_right, &box_x0, &box_x1, &pen, &after);
	return after;
}

/* how far past the cursor a legend's words end after a key's cap (the
cap's advance and the words: "=CANCEL" with its "=" between the cap and
CANCEL, as hud_key_prompt_draw_equals draws it), for a name whose letters
span name_left to name_right drawn from 0, measured in the draw mode's
font, which the words are drawn in */
short hud_key_prompt_legend_advance(short name_left, short name_right, wchar_t const *words)
{
	short inset, margin, gap, half, box_x0, box_x1, pen, after;
	short equals_left, equals_right, equals_advance, word_left, word_right, equals_pen, word_pen;
	long equals;

	key_cap_metrics(&inset, &margin, &gap, &half);
	key_cap_layout(name_left, name_right, &box_x0, &box_x1, &pen, &after);
	equals = key_equals_length(words);
	if (equals && key_text_measure(L"=", &equals_left, &equals_right, &equals_advance) &&
		key_text_span(words + equals, &word_left, &word_right))
	{
		key_cap_equals_layout(equals_advance, &equals_pen, &word_pen);
		return (short)(box_x1 + gap + word_pen + word_right);
	}
	key_text_span(words, &word_left, &word_right);
	return (short)(after + word_right);
}

/* the key's name at the cursor in a key cap, as the HUD draws a button
there, the cursor moved past it. The cap is laid out in whole units
(key_cap_layout: the text after it, the legends' rows), and drawn about the
name's letters as they are drawn, in fractions of a unit: as much room
either side of them, and above and below their capitals */
static boolean key_cap_draw(wchar_t const *text, rectangle2d *bounds, rectangle2d *cursor, pixel32 color)
{
	rectangle2d text_bounds, line_start;
	short inset, margin, gap, half, middle;
	short start, left = 0, right = 0, box_x0, box_x1, pen, after;
	real ink_left, ink_right, ink_middle, capitals_middle;
	pixel32 fill;

	key_cap_metrics(&inset, &margin, &gap, &half);
	start = cursor->x0;
	line_start = *cursor;
	/* (the name measured from the cursor: its letters' span where they are
	drawn, the high-res text's glyphs where it draws them, so that the cap
	is about the letters seen, not the font tag's bitmaps) */
	draw_string_set_indents(start - bounds->x0, 0);
	draw_unicode_string_compute_drawn_bounds(bounds, text, &text_bounds, cursor);
	if (cursor->y0 != line_start.y0)
	{
		/* (a name that does not fit on the line: its text alone, where it
		wrapped) */
		text_bounds.x0 = bounds->x0;
		rasterizer_draw_unicode_string(&text_bounds, NULL, NULL, 0, text);
		bounds->y0 = cursor->y0;
		key_cap_after_line = SHORT_MIN;
		return TRUE;
	}
	if (text_bounds.x0 <= text_bounds.x1)
	{
		left = (short)(text_bounds.x0 - start);
		right = (short)(text_bounds.x1 - start);
	}
	/* (the letters' ink in fractions of a unit, else their whole units) */
	if (!draw_unicode_string_drawn_span(&ink_left, &ink_right))
	{
		ink_left = (real)(start + left);
		ink_right = (real)(start + right);
	}
	key_cap_layout(left, right, &box_x0, &box_x1, &pen, &after);
	/* (the name drawn from its place in the middle of the cap) */
	*cursor = line_start;
	cursor->x0 = (short)(start + pen);
	draw_string_set_indents(cursor->x0 - bounds->x0, 0);
	draw_unicode_string_compute_bounds(bounds, text, &text_bounds, cursor);
	/* a key cap: the prompt's colour faintly filled, edged in it, its
	middle the letters' (drawn pen units further on than measured) and its
	capitals' (half a unit lower than the whole unit for an odd number of
	rows) */
	ink_middle = (ink_left + ink_right) * 0.5f + (real)pen;
	middle = draw_unicode_string_capital_middle(cursor, L"");
	capitals_middle = (real)middle + (draw_string_capital_height() % 2 ? 0.5f : 0.0f);
	fill = ((pixel32)((color >> 24) * 3 / 10) << 24) | (color & 0x00FFFFFFUL);
	key_cap_shape(ink_middle - (real)(box_x1 - box_x0) * 0.5f, capitals_middle - ((real)half - 0.5f),
		ink_middle + (real)(box_x1 - box_x0) * 0.5f, capitals_middle + ((real)half - 0.5f), fill, color);
	text_bounds.x0 = bounds->x0;
	rasterizer_draw_unicode_string(&text_bounds, NULL, NULL, 0, text);
	bounds->y0 = cursor->y0;
	/* (the text that follows starts past the cap and its gap) */
	cursor->x0 = (short)(start + after);
	key_cap_after_line = cursor->y0;
	key_cap_letters_x = (short)(start + box_x1 + gap);
	return TRUE;
}

/* the "=" of a legend's words after the key cap just drawn at the cursor
("=CANCEL"), drawn as far from the cap as from the rest of the words, the
cursor moved to where they are drawn from (key_cap_equals_layout);
returns the characters taken (the "=" and the spaces before it), or 0
(nothing drawn) where the text starts with no "=", or no cap was just
drawn on the cursor's line */
long hud_key_prompt_draw_equals(
	rectangle2d *bounds,
	rectangle2d *cursor,
	wchar_t const *text)
{
	short equals_left, equals_right, equals_advance, word_left, word_right, equals_pen, word_pen;
	rectangle2d text_bounds, drawn;
	long equals = key_equals_length(text);

	if (!equals || cursor->y0 != key_cap_after_line)
		return 0;
	if (!key_text_measure(L"=", &equals_left, &equals_right, &equals_advance) ||
		!key_text_span(text + equals, &word_left, &word_right))
	{
		return 0;
	}
	key_cap_equals_layout(equals_advance, &equals_pen, &word_pen);
	drawn = *cursor;
	drawn.x0 = (short)(key_cap_letters_x + equals_pen);
	draw_string_set_indents((short)(drawn.x0 - bounds->x0), 0);
	draw_unicode_string_compute_bounds(bounds, L"=", &text_bounds, &drawn);
	text_bounds.x0 = bounds->x0;
	rasterizer_draw_unicode_string(&text_bounds, NULL, NULL, 0, L"=");
	cursor->x0 = (short)(key_cap_letters_x + word_pen);
	key_cap_after_line = SHORT_MIN;
	return equals;
}

/* an action's key as the prompts name it: its first binding, else
Unbound (an action with no key, or no action) */
void hud_key_prompt_action_name(
	short action,
	char *name,
	long size)
{
	if (size > 0 && (action == NONE || !halo_keyboard_action_name(action, name, (unsigned long)size)))
		snprintf(name, (size_t)size, "%s", "Unbound");
}

/* whether a character is a letter or a digit (of the Latin alphabets) */
static boolean word_character(wchar_t character)
{
	return (character >= L'0' && character <= L'9') || (character >= L'A' && character <= L'Z') ||
		(character >= L'a' && character <= L'z') || character >= 0xC0;
}

static long wide_length(wchar_t const *text)
{
	long length = 0;

	while (text[length])
		length++;
	return length;
}

/* a button's word in a message (the multiplayer start's "Hold BACK for
score", the map's string) as its icon's name ("%back-button"), which the
HUD's custom messages draw as the prompts draw the button
(draw_string_and_hack_in_icons): the key bound while the keyboard and mouse
play, else the controller's button (hud_key_prompt_score_hint: where there
is a picture of it). The first time the word stands alone (no
letter or digit either side), if the result fits in length characters with
its terminator; else the text is left as it is */
void hud_key_prompt_button_word(
	wchar_t *text,
	long length,
	wchar_t const *word,
	wchar_t const *icon)
{
	long text_length = wide_length(text);
	long word_length = wide_length(word);
	long icon_length = wide_length(icon);
	long index, at;

	for (index = 0; index + word_length <= text_length; index++)
	{
		for (at = 0; at < word_length && text[index + at] == word[at]; at++)
			;
		if (at < word_length || (index > 0 && word_character(text[index - 1])) ||
			word_character(text[index + word_length]))
		{
			continue;
		}
		if (text_length - word_length + icon_length >= length)
			return;
		memmove(text + index + icon_length, text + index + word_length,
			(size_t)(text_length - index - word_length + 1) * sizeof(wchar_t));
		memcpy(text + index, icon, (size_t)icon_length * sizeof(wchar_t));
		return;
	}
}

/* the key a prompt names for the keyboard and mouse, for a game control or
(game_control NONE) a controller button, Unbound for an action with no key;
FALSE when the keyboard has no action for it (the D-pad, the menus' accept
and back), which keeps the controller's button */
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
			hud_key_prompt_action_name(action, name, sizeof(name));
	}
	if (!name[0])
		return FALSE;
	latin1_to_wide(name, text, length);
	return TRUE;
}

/* the key a prompt draws in place of its controller button for the first
player: while the keyboard and mouse play, the key (hud_key_prompt_text); in
the headset, what presses the button (vr_prompt_text); FALSE when the prompt
keeps the controller's button */
static boolean prompt_key_text(
	short local_player_index,
	short game_control,
	short gamepad_button,
	wchar_t *text,
	long length)
{
	if (local_player_index != 0)
		return FALSE;
#ifdef HALO_VR
	if (halo_vr_running())
		return vr_prompt_text(local_player_index, game_control, gamepad_button, text, length);
#endif
	return halo_keyboard_prompts() &&
		hud_key_prompt_text(local_player_index, game_control, gamepad_button, text, length);
}

/* whether text draws a picture of a controller button: the message icons
draw_string_and_hack_in_icons (ui_widget.c) takes its buttons from, the
globals' third interface bitmap (the Xbox maps' and Halo PC's
hud_msg_icons_sm), have a sprite of it no smaller than
HUD_KEY_PROMPT_MINIMUM_SPRITE each way. Theirs of Start and Back are empty
cells of 4 by 4 texels: the Xbox's messages wrote those buttons' names */
static boolean button_sprite_drawn(
	short gamepad_button)
{
	struct game_globals *game_globals = scenario_get_game_globals();
	struct game_globals_interface_tag_references *references;
	struct icon_hud_element_definition const *icon;
	struct bitmap_data const *bitmap = NULL;
	real_rectangle2d const *clip = NULL;
	long bitmap_group_index;

	if (!hud_globals || !game_globals || !game_globals->interface_tag_references.count ||
		gamepad_button < 0 || gamepad_button >= hud_globals->messaging.button_icons.count)
	{
		return FALSE;
	}
	references = TAG_BLOCK_GET_ELEMENT(&game_globals->interface_tag_references, 0,
		struct game_globals_interface_tag_references);
	bitmap_group_index = references->interface_tag_references[_interface_bitmap_iface_map2].index;
	if (bitmap_group_index == NONE)
		return FALSE;
	icon = TAG_BLOCK_GET_ELEMENT(&hud_globals->messaging.button_icons, gamepad_button,
		struct icon_hud_element_definition);
	hud_retrieve_bitmap_and_bounding_rect(bitmap_group_index, icon->sequence_index, 0, &bitmap, &clip);
	return bitmap && clip &&
		(clip->x1 - clip->x0) * bitmap->width >= HUD_KEY_PROMPT_MINIMUM_SPRITE &&
		(clip->y1 - clip->y0) * bitmap->height >= HUD_KEY_PROMPT_MINIMUM_SPRITE;
}

/* the multiplayer start's score hint ("Hold BACK for score", the map's
string, which game_engine_get_state_message gives) for a local player: its
BACK the Back button's icon (hud_key_prompt_button_word) only where the HUD
draws something for the icon, the scoreboard's key while the keyboard and
mouse play (in the headset, what presses Back), or the map's picture of the
button. The Xbox's and Halo PC's message icons have none of Back (and no
button style draws one), so with a controller (or the touch controls) the
word stays, as the Xbox's message wrote it, rather than leaving a gap */
void hud_key_prompt_score_hint(
	short local_player_index,
	wchar_t *text,
	long length)
{
	wchar_t key[64];

	if (prompt_key_text(local_player_index, NONE, HUD_KEY_PROMPT_BACK_BUTTON, key, NUMBEROF(key)) ||
		button_sprite_drawn(HUD_KEY_PROMPT_BACK_BUTTON))
	{
		hud_key_prompt_button_word(text, length, L"BACK", L"%back-button");
	}
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

	if (!prompt_key_text(local_player_index, game_control, gamepad_button, text, NUMBEROF(text)))
		return FALSE;
	return key_cap_draw(text, bounds, cursor, color);
}

/* the name of a pad's Back button, its family's (an Xbox pad's View, a
PlayStation's Share, a Switch's minus), for its cap in the menus
(hud_key_prompt_draw_back_cap); NULL for no family's */
char const *hud_key_prompt_back_name(int family)
{
	switch (family)
	{
	case CONTROLLER_PLAYSTATION:
		return "Share";
	case CONTROLLER_SWITCH:
		return "-";
	default:
		return "View";
	}
}

/* the menus' Back (View) button while a controller plays (the server
browser's HOST & MORE): no button style has a picture of it (the Xbox's and
Halo PC's message icons have none: hud_key_prompt_score_hint), so a cap of
its name, the menus' player's pad's (player 1's), as the keyboard's keys are
drawn; FALSE while the keyboard and mouse play (their key's cap then) */
boolean hud_key_prompt_draw_back_cap(
	rectangle2d *bounds,
	rectangle2d *cursor,
	pixel32 color)
{
	wchar_t text[16];

#ifdef HALO_VR
	if (halo_vr_running())
		return FALSE;
#endif
	if (halo_keyboard_prompts())
		return FALSE;
	latin1_to_wide(hud_key_prompt_back_name(controller_button_style_family(controller_button_style_for_port(0))),
		text, NUMBEROF(text));
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
