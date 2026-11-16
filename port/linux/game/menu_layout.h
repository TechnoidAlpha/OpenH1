/*
MENU_LAYOUT.H

The open screen's geometry as the menus draw it, written as JSON for
tools/menu_gallery.py's checks (debug.menu_layout, the menu tour's) or kept
for the menu studio's overlay (menu_layout.c). Coordinates are
the menus' (640 by 480 in the middle of the screen, x below 0 and past 640
where the screen is wider), as drawn: offsets, anchors, the wide layout and
a text's scale applied.
*/

#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

/* whether this render of the menus is being written (between
menu_layout_frame_begin and menu_layout_frame_end of a frame that is due) */
int menu_layout_recording(void);

/* the next frame's layout written to this path (in the data root), whatever
debug.menu_layout says (the menu tour's: menu_tour.c) */
void menu_layout_request(char const *path);

/* the first player's render of the menus begins and ends: the screen's
width in the menus' units, the open screen's tag name */
void menu_layout_frame_begin(long screen_width);
void menu_layout_frame_end(char const *screen_name);

/* a widget drawn (visible), its box and the clip it is drawn in; the text,
glyphs and targets noted after it are its own until the next widget, or
menu_layout_widget_none */
void menu_layout_widget(void const *widget, void const *parent, char const *name, short type,
	rectangle2d const *box, rectangle2d const *clip, int focused);
void menu_layout_widget_none(void);

/* a string drawn: the bounds it is laid out in, the clip it is cut by, the
extent of its characters drawn, of all of them in the bounds without their
foot (the lines past it are not drawn) and in the bounds without their
right edge (a line's letters past it are not drawn), before the text's
scale about its origin */
void menu_layout_text(rectangle2d const *box, rectangle2d const *clip, rectangle2d const *extent,
	rectangle2d const *whole, rectangle2d const *wide, wchar_t const *text, real scale, real origin_x,
	real origin_y);

/* a button's glyph drawn: a key's cap (key TRUE, its name) or a
controller's button (its icon's name), from x0 to x1 on the line y0 to y1 */
void menu_layout_glyph(int key, char const *name, short x0, short y0, short x1, short y1);
void menu_layout_glyph_wide(int key, wchar_t const *name, short x0, short y0, short x1, short y1);

/* a place the pointer acts on (ui_widget.c's ui_mouse_targets) */
void menu_layout_target(void const *widget, short kind, rectangle2d const *box);

/* each frame's layout kept for menu_layout_overlay (the menu studio's) */
void menu_layout_keep(int keep);

/* what menu_layout_overlay outlines: each widget's box, and each problem the
layout's checks find (tools/menu_gallery.py's check_layout's) */
enum
{
	_menu_layout_box,
	_menu_layout_clipped,
	_menu_layout_cut,
	_menu_layout_overflow,
	_menu_layout_overlap,
	_menu_layout_offscreen,
	_menu_layout_safe_area,
	_menu_layout_touch_target,
	_menu_layout_glyphs,
	NUMBER_OF_MENU_LAYOUT_PROBLEMS,
	/* (the menu studio's: a widget with an open note, the one being noted) */
	_menu_layout_note = NUMBER_OF_MENU_LAYOUT_PROBLEMS,
	_menu_layout_picked
};

typedef void (*menu_layout_outline_proc)(rectangle2d const *rectangle, long kind);

/* the last kept frame's widgets' boxes and problems, each outlined; the
problems of each kind counted (NUMBER_OF_MENU_LAYOUT_PROBLEMS of them,
_menu_layout_box's none); the problems in all. The phone's (its safe area,
its touch targets) where the menus are the touchscreen's */
long menu_layout_overlay(menu_layout_outline_proc outline, long *counts);

/* the kept frame's widgets, by their record's number (the menu studio's
notes): the one on top at a point (-1 for none but the screen's own), a
widget's, a name's; a record's box and name; and a record as JSON fields:
"widget", "widget_path" (the names from the screen down), "widget_text"
(the words it and its children draw) and "box" */
long menu_layout_widget_at(short x, short y);
long menu_layout_widget_of(void const *widget);
long menu_layout_widget_named(char const *name);
boolean menu_layout_widget_box(long id, rectangle2d *box);
char const *menu_layout_widget_name(long id);
void menu_layout_widget_json(long id, char *json, long size);

#endif
