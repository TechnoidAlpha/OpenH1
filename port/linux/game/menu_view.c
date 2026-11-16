/*
MENU_VIEW.C

The view a player's screen of the menus is drawn in. At full screen the
menus' 640 by 480 are centred on the screen, as they have always been, the
port's screens using its width up to 16:9 and past (the wide layout,
menu_tags.c). In split screen each player's screens (the pause menu, and
what it opens) are drawn in that player's view, a half or a quarter of the
screen: each is laid out for the room it was made for (its widget's bounds:
the menus' 640 by 480, or the Xbox's split screen pause menus' 640 by 240
and 320 by 240) and scaled to fit the view's height and width, never
larger than it was made, centred in it. Nothing of it is cut off, and the
view's room either side of it is the canvas its wide layout and its dim
(a pause menu's) reach across, edge to edge (menu_view_canvas).

What the pause menu opens (Settings, Host Controls, the chapters), a screen of
the menu engine's laid out for the whole screen, is drawn full screen over
every view instead (menu_view_over_views): once a frame, after the views, in
the whole screen's window (render.c's), still that player's screen, taking
their controller's presses and editing their profile.

The scale costs the menus nothing: the game draws its menus' rectangles and
text in the units of the viewport it is told it has (the rasterizer's
viewport_bounds: rasterizer_xbox_dynavobgeom.c and rasterizer_text.c map
them onto the viewport the GPU draws in), so while a screen is drawn the
viewport it is told of is its canvas, the view's size divided by the scale,
and the GPU's stays the view. The canvas's left (its room past the design's
0) is the menus' centring offset (halo_screen_ui_offset, d3d8_device.c,
which the pointer's position is read back through), its top the screen's
offset down. The pointer's targets, noted as the first player's screen is
drawn, are in the canvas's units, and the pointer is read back through the
same view (halo_screen_ui_pointer_view).
*/

#include "cseries.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "game/players.h"

#include "menu_engine.h"
#include "menu_view.h"
#include "menu_widget.h"

#include <math.h>
#include <string.h>

/* tag_groups.c's */
char *tag_get_name(long tag_index);
/* port/linux/src/d3d8_device.c's */
long halo_screen_width(void);
void halo_screen_ui_offset_to(long offset);
void halo_screen_ui_pointer_view(int set, float x0, float y0, float scale, float left);
/* menu_tags.c's */
void ui_widget_port_wide_layout(long width);
short ui_widget_port_wide_extra(void);
short ui_widget_port_past_extra(void);
short ui_widget_port_anchor_extra(void);

/* the pause menu's screens' names begin so (screens/blocks.xml's) */
#define MENU_VIEW_PAUSE "pc\\screens\\pause"

/* the menus' room (their 640 by 480) */
#define MENU_VIEW_WIDTH 640
#define MENU_VIEW_HEIGHT 480

static struct
{
	/* a screen is being drawn in a split screen's view */
	boolean fitted;
	/* the scale, and the canvas: its size and where the design's 0 is in it,
	in the menus' units */
	real scale;
	short width, height, left, top;
	/* the design's size */
	short design_width, design_height;
	/* the cameras' viewports as they were */
	rectangle2d render_viewport, render_window, rasterizer_viewport, rasterizer_window;
} menu_view;

/* the room a screen was made for: its widget's bounds (a hand-written or
generated screen of the menu engine's: the menus' 640 by 480), no more
than the menus' room */
static void menu_view_design(struct widget_instance const *screen, rectangle2d const *design, short *width,
	short *height)
{
	*width = MENU_VIEW_WIDTH;
	*height = MENU_VIEW_HEIGHT;
	if (menu_engine_screen_owned(screen) || !design)
		return;
	if (design->x1 - design->x0 > 0 && design->x1 - design->x0 < MENU_VIEW_WIDTH)
		*width = (short)(design->x1 - design->x0);
	if (design->y1 - design->y0 > 0 && design->y1 - design->y0 < MENU_VIEW_HEIGHT)
		*height = (short)(design->y1 - design->y0);
}

void menu_view_begin(struct widget_instance const *screen, rectangle2d const *design, rectangle2d const *window,
	rectangle2d *clip, point2d *offset)
{
	long view_width = window->x1 - window->x0, view_height = window->y1 - window->y0;
	real scale;

	offset->x = 0;
	offset->y = 0;
	menu_view.fitted = FALSE;
	/* a view of the whole screen: as the menus have always been drawn, from
	the screen's left edge where widgets are anchored to it
	(ui_widget_port_anchor_extra), or the wide layout's
	(ui_widget_port_wide_extra, and past 16:9 the menu engine's:
	ui_widget_port_past_extra), not the menus' 0 */
	if (view_width >= halo_screen_width() && view_height >= MENU_VIEW_HEIGHT)
	{
		clip->x0 = (short)-MAX(ui_widget_port_anchor_extra(),
			(ui_widget_port_wide_extra() + ui_widget_port_past_extra() + 1) / 2);
		clip->y0 = 0;
		clip->x1 = (short)view_width;
		clip->y1 = (short)view_height;
		if (screen && (screen->local_player_index == NONE || screen->local_player_index == 0))
			halo_screen_ui_pointer_view(FALSE, 0.0f, 0.0f, 1.0f, 0.0f);
		return;
	}
	/* a split screen's: the screen scaled to fit it (no larger than it was
	made), its canvas the view at that scale */
	menu_view_design(screen, design, &menu_view.design_width, &menu_view.design_height);
	scale = MIN((real)view_width / menu_view.design_width, (real)view_height / menu_view.design_height);
	scale = MIN(scale, 1.0f);
	menu_view.fitted = TRUE;
	menu_view.scale = scale;
	menu_view.width = (short)floorf(view_width / scale + 0.5f);
	menu_view.height = (short)floorf(view_height / scale + 0.5f);
	menu_view.left = (short)((menu_view.width - menu_view.design_width) / 2);
	menu_view.top = (short)((menu_view.height - menu_view.design_height) / 2);
	/* (the port's screens laid out for the canvas's width) */
	ui_widget_port_wide_layout(menu_view.width);
	clip->x0 = (short)-menu_view.left;
	clip->y0 = (short)-menu_view.top;
	clip->x1 = (short)(menu_view.width - menu_view.left);
	clip->y1 = (short)(menu_view.height - menu_view.top);
	offset->y = menu_view.top;
	/* the viewport the menus are told of: the canvas */
	menu_view.render_viewport = render.camera.viewport_bounds;
	menu_view.render_window = render.camera.window_bounds;
	menu_view.rasterizer_viewport = global_window_parameters.camera.viewport_bounds;
	menu_view.rasterizer_window = global_window_parameters.camera.window_bounds;
	render.camera.viewport_bounds.x0 = window->x0;
	render.camera.viewport_bounds.y0 = window->y0;
	render.camera.viewport_bounds.x1 = (short)(window->x0 + menu_view.width);
	render.camera.viewport_bounds.y1 = (short)(window->y0 + menu_view.height);
	render.camera.window_bounds = render.camera.viewport_bounds;
	global_window_parameters.camera.viewport_bounds = render.camera.viewport_bounds;
	global_window_parameters.camera.window_bounds = render.camera.viewport_bounds;
	/* (the design's 0 moved right by the canvas's left, at the scale) */
	halo_screen_ui_offset_to((long)floorf(menu_view.left * scale + 0.5f));
	if (screen && (screen->local_player_index == NONE || screen->local_player_index == 0))
		halo_screen_ui_pointer_view(TRUE, window->x0, window->y0, scale, menu_view.left);
}

void menu_view_end(void)
{
	if (!menu_view.fitted)
		return;
	menu_view.fitted = FALSE;
	render.camera.viewport_bounds = menu_view.render_viewport;
	render.camera.window_bounds = menu_view.render_window;
	global_window_parameters.camera.viewport_bounds = menu_view.rasterizer_viewport;
	global_window_parameters.camera.window_bounds = menu_view.rasterizer_window;
	halo_screen_ui_offset_to((halo_screen_width() - MENU_VIEW_WIDTH) / 2);
}

short menu_view_side(void)
{
	return menu_view.fitted ? menu_view.left : (short)((halo_screen_width() - MENU_VIEW_WIDTH) / 2);
}

boolean menu_view_canvas(rectangle2d const *bounds, rectangle2d *canvas)
{
	short design_width = menu_view.fitted ? menu_view.design_width : MENU_VIEW_WIDTH;
	short design_height = menu_view.fitted ? menu_view.design_height : MENU_VIEW_HEIGHT;

	if (bounds->x0 > 0 || bounds->y0 > 0 || bounds->x1 < design_width || bounds->y1 < design_height)
		return FALSE;
	if (!menu_view.fitted)
	{
		if (halo_screen_width() <= MENU_VIEW_WIDTH)
			return FALSE;
		canvas->x0 = (short)(bounds->x0 - (halo_screen_width() - MENU_VIEW_WIDTH) / 2);
		canvas->x1 = (short)(MENU_VIEW_WIDTH + (halo_screen_width() - MENU_VIEW_WIDTH) / 2);
		canvas->y0 = bounds->y0;
		canvas->y1 = bounds->y1;
		return TRUE;
	}
	canvas->x0 = (short)MIN(bounds->x0, -menu_view.left);
	canvas->x1 = (short)MAX(bounds->x1, menu_view.width - menu_view.left);
	canvas->y0 = (short)MIN(bounds->y0, -menu_view.top);
	canvas->y1 = (short)MAX(bounds->y1, menu_view.height - menu_view.top);
	return TRUE;
}

boolean menu_view_over_views(struct widget_instance const *screen)
{
	char const *name;

	if (!screen || screen->parent || screen->local_player_index == NONE || local_player_count() <= 1 ||
		!menu_engine_screen_owned(screen))
	{
		return FALSE;
	}
	name = tag_get_name(screen->definition_tag_index);
	return name && strncmp(name, MENU_VIEW_PAUSE, sizeof(MENU_VIEW_PAUSE) - 1);
}

real menu_view_scale(void)
{
	return menu_view.fitted ? menu_view.scale : 1.0f;
}
