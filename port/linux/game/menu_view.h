/*
MENU_VIEW.H

The view a player's screen of the menus is drawn in (menu_view.c): the
whole screen, or in split screen that player's half or quarter, into which
the screen is scaled to fit.
*/

#ifndef MENU_VIEW_H
#define MENU_VIEW_H

struct widget_instance;

/* a screen (a widget with no parent, its definition's bounds the room it
was made for) begins and ends being drawn in the window (a view's bounds,
the screen's units): *clip gets the rectangle the screen may draw in and
*offset where its widgets are drawn from, in its own units; between the two
the menus draw in the view's canvas */
void menu_view_begin(struct widget_instance const *screen, rectangle2d const *design, rectangle2d const *window,
	rectangle2d *clip, point2d *offset);
void menu_view_end(void);

/* how far the canvas the menus are drawn in reaches left of the menus' 0,
in their units: the screen's past the menus' 640 at full screen ((the
screen's width - 640) / 2), a split screen view's past the screen's design
while one is drawn */
short menu_view_side(void);

/* the rectangle of the canvas the menus are drawn in, in their units (the
whole screen, or the view's), to which a flat fill that covers the screen's
design (a pause menu's dim) is widened; FALSE if it is the design's own
(nothing to widen) */
boolean menu_view_canvas(rectangle2d const *bounds, rectangle2d *canvas);

/* whether a player's screen is drawn full screen, over every view, rather
than in the player's own (split screen's: a screen of the menu engine's
opened from the pause menu, Settings, Host Controls, Basic Training's
chapters, but the pause menu itself) */
boolean menu_view_over_views(struct widget_instance const *screen);

/* the scale the screen is drawn at (1 but in a split screen's view whose
screen was made for more room than it has) */
real menu_view_scale(void);

#endif
