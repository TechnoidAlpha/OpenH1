/*
HUD_DRAW.H

Narrow cross-translation-unit interface owned by HUD_DRAW.C.
*/

#ifndef __HUD_DRAW_H
#define __HUD_DRAW_H
#pragma once

#include "math/integer_math.h"
#include "math/real_math.h"

/* ---------- constants */

enum
{
	STACK_BUFFER_LENGTH = 0x80,
	STACK_BUFFER_FILL = 0x62626262,
};

enum hud_draw_flags
{
	_hud_draw_flashing_bit = 0,
	_hud_draw_disabled_bit,
	_hud_draw_in_multiplayer_bit,

	NUMBER_OF_HUD_DRAW_FLAGS
};

/* ---------- macros */

/* The stack sentinel: a guarded function starts with
	long return_eip = get_return_eip();
	long stack_buffer[STACK_BUFFER_LENGTH];
	csmemset(stack_buffer, 0x62, sizeof(stack_buffer));
and ends with this check of both its return address and its fill pattern. */
#define match_assert_stack_frame(file, line) \
{ \
	short corrupt_index = check_stack_buffer(stack_buffer); \
	match_vassert(file, line, return_eip==get_return_eip(), "corrupt return address!"); \
	match_vassert(file, line, corrupt_index==NONE, csprintf(temporary, "corrupt stack at %d!", corrupt_index)); \
}

/* ---------- structures */

struct bitmap_data;
struct hud_absolute_placement_definition;
struct hud_color_definition;
struct hud_placement_definition;
struct meter_hud_element_definition;
struct number_hud_element_definition;
struct tag_block;
struct static_hud_element_definition;
struct weapon_hud_overlay_definition;

/* ---------- prototypes/HUD_DRAW.C */

long get_return_eip(
	void);
real hud_globals_get_scale(
	boolean in_multiplayer);
void hud_retrieve_bitmap_and_bounding_rect(
	long bitmap_group_index,
	short sequence_index,
	short frame_index,
	struct bitmap_data const **bitmap,
	real_rectangle2d const **clip);
long fast_ftol_C(
	real x);
pixel32 real_alpha_intensity_to_pixel32(
	real alpha,
	real intensity);
void hud_calculate_point(
	short local_player_index,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct hud_placement_definition const *placement,
	struct bitmap_data const *bitmap_data,
	boolean in_multiplayer,
	real override_scale,
	point2d *result);
boolean hud_multitexture_overlays_follow_zoom(
	struct tag_block const *multitexture_overlays);
boolean hud_number_shows_only_when_zoomed(
	struct number_hud_element_definition const *number);
void hud_zoomed_layout_begin(
	rectangle2d *saved_window_bounds);
void hud_zoomed_layout_end(
	rectangle2d const *saved_window_bounds);
long get_flash_duration(
	struct hud_color_definition const *hud_color);
pixel32 get_flash_color(
	struct hud_color_definition const *hud_color,
	long reference_value);
void hud_draw_weapon_overlays(
	short local_player_index,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct weapon_hud_overlay_definition const *overlays,
	long type_flags,
	long reference_time,
	short draw_flags,
	boolean in_multiplayer);
/* port: what hud_draw_bitmap's is_crosshair_bitmap may be besides FALSE
and TRUE (the weapon's reticle, its size display.fov's: view_fov.c): a
piece of a scope's own picture, drawn only zoomed over it, which keeps to
the view as the scope's picture does (render_fov_reticle_scale) */
#define HUD_CROSSHAIR_SCOPE_PIECE 2
void hud_draw_bitmap(
	struct bitmap_data const *bitmap,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct hud_placement_definition const *placement,
	real_rectangle2d const *clip,
	real scale,
	real theta,
	pixel32 color,
	boolean in_multiplayer,
	boolean is_interface_bitmap,
	boolean is_crosshair_bitmap);
void hud_draw_meter(
	short local_player_index,
	struct hud_absolute_placement_definition const *placement,
	struct meter_hud_element_definition const *meter,
	byte min_value,
	byte max_value,
	short draw_flags,
	real reference_time,
	real reference_value);
void hud_draw_static_element(
	short local_player_index,
	struct hud_absolute_placement_definition const *placement,
	struct static_hud_element_definition const *static_element,
	short draw_flags,
	long flash_reference_time);
void hud_draw_bitmap_direct(
	struct bitmap_data const *bitmap,
	short placement,
	point2d const *point,
	real_rectangle2d const *clip,
	real scale,
	real theta,
	pixel32 color,
	boolean is_interface_bitmap);
void hud_draw_numbers(
	short local_player_index,
	struct hud_absolute_placement_definition const *absolute_placement,
	struct number_hud_element_definition const *numbers,
	short value,
	short decimal_value,
	short draw_flags,
	long flash_reference_time,
	real override_scale);

/* port: a mark over the world (a waypoint and its distance, a teammate's
triangle, a player's name) at the point of the screen where its world point
projects through the view drawn (in the screen's units, not whole): whole is
the whole units the game places it at, and what is drawn until
hud_draw_mark_end is moved on by the rest, so that it keeps to its point as
the world under it moves, not a unit (several pixels) short of it in steps */
void hud_draw_mark_begin(
	real x,
	real y,
	point2d *whole);
void hud_draw_mark_end(
	void);

/* port: the left of the last picture drawn, in the window's units
(hud_unit.c: the motion sensor's, which the text chat goes above:
port/linux/game/network_chat.c) */
extern short hud_draw_last_left;

/* ---------- public code */

__inline short check_stack_buffer(
	long *buffer)
{
	short index;

	for (index = STACK_BUFFER_LENGTH-1; index>=0; index--)
	{
		if (buffer[index]!=STACK_BUFFER_FILL)
			return index;
	}

	return NONE;
}

#endif /* __HUD_DRAW_H */
