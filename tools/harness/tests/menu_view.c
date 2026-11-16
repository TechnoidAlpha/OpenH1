/*
MENU_VIEW.C (test)

The view a player's screen of the menus is drawn in (port/linux/game/
menu_view.c, issue 887). At full screen nothing changes: the clip from the
wide layout's left, no offset, the cameras and the centring as they were.
In split screen a screen made for the menus' 640 by 480 is scaled to fit
the player's half or quarter (never larger than it was made), its canvas
the view at that scale, its design centred in it; the Xbox's own split
screen pause menus (640 by 240, 320 by 240) keep their size, centred in
their view; a dim that covers the screen's design reaches the view's edges;
and the cameras and the centring are given back after it. test_menu_view.py
takes the code.
*/

#include "harness.h"

#include <math.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define NONE (-1)

typedef struct { short x0, y0, x1, y1; } rectangle2d;
typedef struct { short x, y; } point2d;

struct widget_instance { short local_player_index; boolean owned; struct widget_instance *parent; long definition_tag_index; };
struct camera_bounds { rectangle2d viewport_bounds, window_bounds; };
static struct { struct camera_bounds camera; } render, global_window_parameters;

static long screen_width = 853;
static long ui_offset;
static short wide_extra, past_extra;
static struct { int set; float x0, y0, scale, left; } pointer;

static long halo_screen_width(void) { return screen_width; }
static void halo_screen_ui_offset_to(long offset) { ui_offset = offset; }
static void halo_screen_ui_pointer_view(int set, float x0, float y0, float scale, float left)
{
	pointer.set = set;
	pointer.x0 = x0;
	pointer.y0 = y0;
	pointer.scale = scale;
	pointer.left = left;
}
static void ui_widget_port_wide_layout(long width)
{
	wide_extra = (short)(width <= 640 ? 0 : width >= 852 ? 212 : width - 640);
	past_extra = (short)(width <= 852 ? 0 : MIN(width - 852, 4 * 286));
}
static short ui_widget_port_wide_extra(void) { return wide_extra; }
static short ui_widget_port_past_extra(void) { return past_extra; }
static short ui_widget_port_anchor_extra(void) { return 0; }
static boolean menu_engine_screen_owned(struct widget_instance const *screen) { return screen && screen->owned; }
static short local_players = 1;
static short local_player_count(void) { return local_players; }
/* (a screen's tag's name: 1, the pause menu's; 2, Settings') */
static char *tag_get_name(long tag_index)
{
	return tag_index == 1 ? "pc\\screens\\pause_live" : "pc\\main_menu\\settings_select\\player_setup\\video_settings_screen";
}

#include "under_test.inc"

static rectangle2d box(short x0, short y0, short x1, short y1)
{
	rectangle2d rectangle = { x0, y0, x1, y1 };

	return rectangle;
}

/* the cameras as render.c has them for a view */
static void view_cameras(rectangle2d const *window)
{
	render.camera.viewport_bounds = *window;
	render.camera.window_bounds = *window;
	global_window_parameters.camera = render.camera;
	ui_offset = (screen_width - 640) / 2;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct widget_instance player2 = { 1, FALSE, NULL, 2 }, player1 = { 0, FALSE, NULL, 2 };
	rectangle2d clip, design = box(0, 0, 640, 480);
	point2d offset;

	CASE("full")
	{
		/* the whole screen: as the menus have always been drawn */
		rectangle2d window = box(0, 0, 853, 480);

		view_cameras(&window);
		ui_widget_port_wide_layout(853);
		menu_view_begin(&player1, &design, &window, &clip, &offset);
		CHECK(clip.x0 == -107 && clip.y0 == 0 && clip.x1 == 853 && clip.y1 == 480, "full screen's clip %d %d %d %d",
			clip.x0, clip.y0, clip.x1, clip.y1);
		CHECK(offset.x == 0 && offset.y == 0, "full screen moved");
		CHECK(render.camera.viewport_bounds.x1 == 853 && global_window_parameters.camera.viewport_bounds.y1 == 480,
			"full screen's cameras changed");
		CHECK(ui_offset == 106, "full screen's centring changed: %ld", ui_offset);
		CHECK(menu_view_side() == 106, "full screen's side %d", menu_view_side());
		menu_view_end();
		CHECK(ui_offset == 106, "full screen's centring changed after");
	}
	CASE("half")
	{
		/* the lower half of a 16:9 screen: a 640 by 480 screen at half its
		size, the canvas 1706 by 480, its design in the middle */
		rectangle2d window = box(0, 240, 853, 480), canvas;

		view_cameras(&window);
		menu_view_begin(&player2, &design, &window, &clip, &offset);
		CHECK(fabsf(menu_view_scale() - 0.5f) < 1e-4f, "scale %f", menu_view_scale());
		CHECK(render.camera.viewport_bounds.x0 == 0 && render.camera.viewport_bounds.y0 == 240 &&
			render.camera.viewport_bounds.x1 == 1706 && render.camera.viewport_bounds.y1 == 720,
			"canvas %d %d %d %d", render.camera.viewport_bounds.x0, render.camera.viewport_bounds.y0,
			render.camera.viewport_bounds.x1, render.camera.viewport_bounds.y1);
		CHECK(global_window_parameters.camera.viewport_bounds.x1 == 1706, "the rasterizer's camera not the canvas");
		CHECK(menu_view_side() == 533, "side %d", menu_view_side());
		CHECK(clip.x0 == -533 && clip.y0 == 0 && clip.x1 == 1173 && clip.y1 == 480, "clip %d %d %d %d", clip.x0,
			clip.y0, clip.x1, clip.y1);
		CHECK(ui_offset == 267, "the design's 0 at %ld, not 267", ui_offset);
		CHECK(wide_extra == 212, "laid out for %d", wide_extra);
		/* the whole of the screen's 480 lines in the view's 240 */
		CHECK(480 * menu_view_scale() <= window.y1 - window.y0, "cut off at the foot");
		CHECK(menu_view_canvas(&design, &canvas) && canvas.x0 == -533 && canvas.x1 == 1173 && canvas.y0 == 0 &&
			canvas.y1 == 480, "the dim not across the view: %d %d %d %d", canvas.x0, canvas.y0, canvas.x1, canvas.y1);
		menu_view_end();
		CHECK(render.camera.viewport_bounds.x1 == 853 && render.camera.viewport_bounds.y1 == 480 &&
			global_window_parameters.camera.viewport_bounds.x1 == 853, "the cameras not given back");
		CHECK(ui_offset == 106, "the centring not given back: %ld", ui_offset);
	}
	CASE("quarter")
	{
		/* a quarter: half size, a 16:9 canvas */
		rectangle2d window = box(426, 240, 852, 480);

		view_cameras(&window);
		menu_view_begin(&player2, &design, &window, &clip, &offset);
		CHECK(fabsf(menu_view_scale() - 0.5f) < 1e-4f, "scale %f", menu_view_scale());
		CHECK(menu_view_side() == 106 && clip.y1 == 480 && offset.y == 0, "side %d", menu_view_side());
		CHECK(ui_offset == 53, "the design's 0 at %ld", ui_offset);
		menu_view_end();
	}
	CASE("tall")
	{
		/* a view taller than it is wide: the width fits, the screen centred
		down it */
		rectangle2d window = box(0, 0, 320, 480);

		view_cameras(&window);
		menu_view_begin(&player2, &design, &window, &clip, &offset);
		CHECK(fabsf(menu_view_scale() - 0.5f) < 1e-4f, "scale %f", menu_view_scale());
		CHECK(offset.y == 240 && clip.y0 == -240 && clip.y1 == 720, "offset %d clip %d %d", offset.y, clip.y0,
			clip.y1);
		menu_view_end();
	}
	CASE("xbox")
	{
		/* the Xbox's split screen pause menus keep their size, centred */
		rectangle2d half = box(0, 240, 853, 480), quarter = box(426, 0, 852, 240);
		rectangle2d half_design = box(0, 0, 640, 240), quarter_design = box(0, 0, 320, 240), canvas, window_tall;

		view_cameras(&half);
		menu_view_begin(&player2, &half_design, &half, &clip, &offset);
		CHECK(menu_view_scale() == 1.0f && ui_offset == 106, "half: scale %f at %ld", menu_view_scale(), ui_offset);
		CHECK(menu_view_canvas(&half_design, &canvas) && canvas.x0 == -106 && canvas.x1 == 747 && canvas.y1 == 240,
			"half's dim %d %d %d %d", canvas.x0, canvas.y0, canvas.x1, canvas.y1);
		menu_view_end();
		view_cameras(&quarter);
		menu_view_begin(&player2, &quarter_design, &quarter, &clip, &offset);
		CHECK(menu_view_scale() == 1.0f && ui_offset == 53, "quarter: scale %f at %ld", menu_view_scale(), ui_offset);
		CHECK(menu_view_canvas(&quarter_design, &canvas) && canvas.x0 == -53 && canvas.x1 == 373,
			"quarter's dim %d %d", canvas.x0, canvas.x1);
		menu_view_end();
		/* (nor larger in a view with more room than they were made for) */
		window_tall = box(0, 0, 426, 480);
		view_cameras(&window_tall);
		menu_view_begin(&player2, &quarter_design, &window_tall, &clip, &offset);
		CHECK(menu_view_scale() == 1.0f, "a quarter's pause menu made larger: %f", menu_view_scale());
		menu_view_end();
		/* (a screen of the menu engine's is the menus' 640 by 480, whatever
		its stand-in's bounds) */
		player2.owned = TRUE;
		view_cameras(&half);
		menu_view_begin(&player2, &half_design, &half, &clip, &offset);
		CHECK(fabsf(menu_view_scale() - 0.5f) < 1e-4f, "the engine's screen at %f", menu_view_scale());
		menu_view_end();
	}
	CASE("pointer")
	{
		/* the first player's view, where the pointer is read back through */
		rectangle2d window = box(0, 0, 853, 240);

		view_cameras(&window);
		menu_view_begin(&player1, &design, &window, &clip, &offset);
		CHECK(pointer.set && fabsf(pointer.scale - 0.5f) < 1e-4f && pointer.left == 533.0f, "pointer's view");
		menu_view_end();
		view_cameras(&window);
		window = box(0, 0, 853, 480);
		menu_view_begin(&player1, &design, &window, &clip, &offset);
		CHECK(!pointer.set, "the pointer's view kept at full screen");
		menu_view_end();
		/* (the other players' views leave it) */
		window = box(0, 240, 853, 480);
		menu_view_begin(&player2, &design, &window, &clip, &offset);
		CHECK(!pointer.set, "the second player's view taken for the pointer");
		menu_view_end();
	}
	CASE("over")
	{
		/* a split screen player's Settings over every view; not the pause
		menu, nor at one player, nor the game's own screens, nor a widget in
		a screen */
		struct widget_instance settings = { 1, TRUE, NULL, 2 }, pause = { 1, TRUE, NULL, 1 }, map = { 1, FALSE, NULL, 2 };
		struct widget_instance part = { 1, TRUE, &settings, 2 };

		local_players = 2;
		CHECK(menu_view_over_views(&settings), "Settings in a player's view");
		CHECK(!menu_view_over_views(&pause), "the pause menu over every view");
		CHECK(!menu_view_over_views(&map), "the map's screen over every view");
		CHECK(!menu_view_over_views(&part), "a screen's widget over every view");
		local_players = 1;
		CHECK(!menu_view_over_views(&settings), "Settings over every view at one player");
	}
	return 0;
}
