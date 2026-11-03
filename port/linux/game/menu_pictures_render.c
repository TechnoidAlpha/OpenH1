/*
MENU_PICTURES_RENDER.C

The game's half of the menus' pictures drawn again from the player's maps
(menu_pictures.h; the platform's half, which decides, keeps and draws them,
is port/linux/src/menu_pictures.c).

menu_picture_at: which pose's original frame a texture is, wherever the
menus draw it: ui.map's bitmap itself (the multiplayer map's picture), or a
menu file's scaled copy of one of its frames (the campaign's cards:
menu_tags.c's placements).

"Preparing menu pictures": the first time the main menu shows, if any
picture is to be rendered (menu_pictures_wanted: none rendered yet, a pose
or map changed, a larger screen), the game renders them itself, in its own
window and process, one map at a time, then shows the main menu. Each map
is loaded the normal way (main.c's main_menu_pictures_map: as a mission
starts, but with no fade, nothing of the player's read or written: their
saved game, last level, checkpoints), and fully torn down by the next
load, as a mission is; the next map's file is decompressed into the cache
in the background meanwhile (main_queue_map_name). The world's time stands
still (game_speed 0) but where the scene needs it to pass, and there its
ticks run at once, in one frame, as many as it needs (as main_skip runs a
cinematic's): no time waited for. The scene is set as the pose was found
with (tools/preview_rerender/game.py): the map's opening ticks run until
no cinematic has run for 5 seconds of its time (each skipped as the skip
button does, debug.skip_cinematics), at least 5 and at most 20 seconds;
then no console text, HUD, help text or AI, a cinematic still running
stopped, the player's body gone, 1.5 seconds more, and the world stopped,
whatever a cinematic left on the screen cleared (its letterbox, titles and
the screen's fade); the pose's structure (always: a map may start in
another), its own lines and objects, settled (its settle_seconds' ticks);
the camera put at the pose (debug_camera_set, camera_pose.c); and one
frame drawn, each texture it draws with waited for (xbox_texture_cache.c),
with only the pipelines it draws with made (render_enumerate.c's map's
enumeration left out), at the window's size, and kept as it is presented
instead of shown (d3d8_device.c; menu_pictures_render_kept cuts and scales
it). Whatever the player's view settings, the frame is drawn with the
pose's own field of view, no Panini projection, the reticle in the middle
and every line drawn (port_config.c's config_picture_view). Sound is off
meanwhile.

While the pictures are rendered the window shows a screen of its own,
about 30 times a second while a map is set up (nothing else: the frames
between are not drawn, and the map's loading screen and the frames of its
file's decompression are not shown: the screen stays): "Preparing menu
pictures", each picture in a grid as it is made, a bar, and how to skip.
Escape, or the controller's B or Back, skips the rest: those keep the
originals (their notes as they were) and are rendered at a later start.
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "cache/cache_files.h"
#include "cache/texture_cache.h"
#include "cutscene/cinematics.h"
#include "effects/player_effects.h"
#include "game/game.h"
#include "hs/hs.h"
#include "input/input.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "main/main.h"
#include "rasterizer/rasterizer.h"
#include "rasterizer/xbox/rasterizer_xbox_hardware_bitmaps.h"
#include "render/render.h"
#include "scenario/scenario.h"
#include "sound/sound_manager.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"

#include "menu_pictures.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------- constants */

#define BITMAP_TAG 'bitm'
#define MAXIMUM_RENDER_LINE 512

enum
{
	/* (ticks, at 30 a second) the opening: at least, without a cinematic
	at its end, at most; then the scene's commands' time */
	PREPARE_OPENING_LEAST = 150,
	PREPARE_OPENING_QUIET = 150,
	PREPARE_OPENING_MOST = 600,
	PREPARE_SCENE_TICKS = 45,
	/* the most ticks run in a frame (the screen drawn between) */
	PREPARE_TICKS_A_FRAME = 120,
	/* (frames) a map not loaded after this many is given up */
	PREPARE_LOAD_FRAMES = 30,
	/* (nanoseconds) the screen's frames, at most this often */
	PREPARE_SCREEN_INTERVAL = 33000000,
};

/* (bitmaps.c's, as menu_tags.c has them: the placeholders' textures) */
enum
{
	_bitmap_type_2d = 0,
	_bitmap_format_a8r8g8b8 = 11,
	_bitmap_has_power_of_two_dimensions_bit = 0,
	PLACEHOLDER_SIZE = 4,
};

enum prepare_state
{
	_prepare_off,
	/* a map asked for; loading it (at the next frame's start) */
	_prepare_load,
	_prepare_loading,
	/* its opening's ticks */
	_prepare_opening,
	/* the scene set; the structure switched (at the next frame's start) */
	_prepare_structure,
	/* the pose's lines and objects, settled; the camera put there */
	_prepare_objects,
	/* the camera taken; the frame drawn and kept */
	_prepare_pose,
	_prepare_capture,
	/* the main menu asked for, and shown */
	_prepare_menu,
};

/* the phases each picture's time is counted in, for the log */
enum
{
	_phase_load,
	_phase_opening,
	_phase_scene,
	_phase_objects,
	_phase_frame,
	_phase_keep,
	_phase_after,
	NUMBER_OF_PHASES
};

/* ---------- prototypes */

/* the platform layer's */
unsigned long long SDL_GetTicksNS(void);
void platform_log(char const *format, ...);
/* cinematics.c's */
extern struct cinematic_global_data *cinematic_globals;
/* menu_tags.c's */
boolean pc_menu_map_frame_at(unsigned long address, long width, long height, long *tag_index, long *frame);
/* main.c's (port) */
void main_menu_pictures_map(char const *scenario);
void main_menu_pictures_end(void);
/* port_config.c's: the view as a pose was found with, whatever the
player's settings */
void config_picture_view(int on);

/* ---------- globals */

static struct
{
	enum prepare_state state;
	/* the poses to render, in order, the one rendered, and how many are
	done (rendered, failed or skipped) */
	long poses[MAXIMUM_MENU_PICTURES];
	long count;
	long index;
	long done, made;
	int skip_asked, skipped;
	/* (to see the skip's press, not its hold) */
	int skip_down;
	long frames, picture_frames;
	long ticks, quiet_ticks;
	/* the frame drawn this frame: none, the screen alone, or the game's
	kept (with the screen over it); and when the screen was last drawn */
	int drawn;
	unsigned long long screen_drawn;
	/* the frame is kept as it is drawn */
	int capture_wanted, captured;
	unsigned long long began, picture_began, phase_began;
	double phases[NUMBER_OF_PHASES];
	double totals[NUMBER_OF_PHASES];
	/* the screen's pictures: placeholder bitmaps drawn as the pictures */
	struct bitmap_data thumbnails[MAXIMUM_MENU_PICTURES];
	int thumbnails_made[MAXIMUM_MENU_PICTURES];
} prepare;

/* ---------- public code */

long menu_picture_at(
	unsigned long address,
	long width,
	long height)
{
	long pose, tag_index = NONE, frame = NONE;
	boolean scaled = pc_menu_map_frame_at(address, width, height, &tag_index, &frame);

	for (pose = 0; pose < (long)menu_picture_pose_count; pose++)
	{
		struct menu_picture_pose const *picture = &menu_picture_poses[pose];
		long picture_tag = tag_loaded(BITMAP_TAG, picture->bitmap);
		struct bitmap_group *group;
		struct bitmap_data const *bitmap;

		if (picture_tag == NONE)
			continue;
		if (scaled)
		{
			if (tag_index == picture_tag && frame == picture->frame)
				return pose;
			continue;
		}
		group = bitmap_group_get(picture_tag);
		if (!group || picture->frame >= group->bitmaps.count)
			continue;
		bitmap = (struct bitmap_data const *)group->bitmaps.address + picture->frame;
		if (bitmap->cache_block_index != NONE && (unsigned long)bitmap->base_address == address &&
			bitmap->width == width && bitmap->height == height)
		{
			return pose;
		}
	}
	return -1;
}

int menu_pictures_preparing(void)
{
	return prepare.state != _prepare_off;
}

int menu_pictures_frame_drawn(void)
{
	return prepare.state == _prepare_off || prepare.drawn;
}

int menu_pictures_screen_only(void)
{
	return prepare.state != _prepare_off && prepare.drawn == 1;
}

int menu_pictures_textures_wait(void)
{
	return prepare.capture_wanted;
}

/* ---------- the pictures rendered */

static unsigned long long prepare_now(
	void)
{
	return SDL_GetTicksNS();
}

/* the time since the phase began counted in this phase, and the next
begun */
static void prepare_phase(
	long phase)
{
	unsigned long long now = prepare_now();

	prepare.phases[phase] += (double)(now - prepare.phase_began) / 1e9;
	prepare.phase_began = now;
}

static void prepare_command(
	char const *line)
{
	hs_compile_and_evaluate((char *)line);
}

/* the world's ticks run now (its time stood still, game_speed 0, else) */
static void prepare_ticks(
	long ticks)
{
	game_time_set_speed(1.0f);
	while (ticks-- > 0)
	{
		game_time_update(1.0f / TICKS_PER_SECOND);
		prepare.ticks++;
	}
	game_time_set_speed(0.0f);
}

/* whatever a cinematic left on the screen gone: its letterbox (which goes
as the world's time passes), its titles, and the screen's fade */
static void prepare_quiet(
	void)
{
	real_rgb_color black = { 0.0f, 0.0f, 0.0f };
	short index;

	/* (and a fade a script began, out or in, that its end would undo) */
	player_effect_port_screen_fade_set(&black, NONE, FALSE, game_time_get());
	if (!cinematic_globals)
		return;
	cinematic_globals->show_letterbox = FALSE;
	cinematic_globals->letterbox_amount = 0.0f;
	for (index = 0; index < NUMBEROF(cinematic_globals->queued_titles); index++)
		cinematic_globals->queued_titles[index].title_index = NONE;
}

/* the original frame's texels, loaded now (ui.map's, in the main menu) */
static void prepare_keep_original(
	long pose)
{
	struct menu_picture_pose const *picture = &menu_picture_poses[pose];
	long tag_index = tag_loaded(BITMAP_TAG, picture->bitmap);
	struct bitmap_group *group = tag_index == NONE ? NULL : bitmap_group_get(tag_index);
	void *texture;

	if (!group || picture->frame >= group->bitmaps.count)
	{
		platform_log("menu pictures: ui.map has no %s frame %ld", picture->bitmap, picture->frame);
		return;
	}
	texture = _texture_cache_bitmap_get_hardware_format((struct bitmap_data *)group->bitmaps.address + picture->frame,
		TRUE, TRUE);
	if (texture)
		menu_pictures_keep_original(pose, texture);
}

/* the screen's picture of a pose: a placeholder texture, drawn as the
picture once it is made (menu_pictures_thumbnail_texture) */
static void prepare_thumbnail_make(
	long index)
{
	struct bitmap_data *bitmap = &prepare.thumbnails[index];

	csmemset(bitmap, 0, sizeof(*bitmap));
	bitmap->signature = 'bitm';
	bitmap->width = PLACEHOLDER_SIZE;
	bitmap->height = PLACEHOLDER_SIZE;
	bitmap->depth = 1;
	bitmap->type = _bitmap_type_2d;
	bitmap->format = _bitmap_format_a8r8g8b8;
	bitmap->flags = FLAG(_bitmap_has_power_of_two_dimensions_bit);
	bitmap->tag_index = NONE;
	bitmap->cache_block_index = NONE;
	if (!rasterizer_bitmap_new(bitmap) || !bitmap->hardware_format)
		return;
	/* (drawn in the frame's units, whatever its texture's size) */
	bitmap->width = MENU_PICTURE_FRAME_WIDTH;
	bitmap->height = MENU_PICTURE_FRAME_HEIGHT;
	menu_pictures_thumbnail_register(((unsigned long const *)bitmap->hardware_format)[1], prepare.poses[index]);
	prepare.thumbnails_made[index] = TRUE;
}

static void prepare_thumbnails_delete(
	void)
{
	long index;

	for (index = 0; index < prepare.count; index++)
	{
		if (prepare.thumbnails_made[index])
		{
			prepare.thumbnails[index].width = PLACEHOLDER_SIZE;
			prepare.thumbnails[index].height = PLACEHOLDER_SIZE;
			rasterizer_bitmap_delete(&prepare.thumbnails[index]);
			prepare.thumbnails_made[index] = FALSE;
		}
	}
}

static void prepare_picture_log(
	long pose,
	char const *how)
{
	long phase;

	prepare_phase(_phase_after);
	platform_log("menu pictures: %s %s in %.2f s (%ld frames): load %.2f, opening %.2f (%ld ticks), scene %.2f, "
		"objects %.2f, frame %.2f, kept %.2f, after %.2f", menu_picture_poses[pose].name, how,
		(double)(prepare_now() - prepare.picture_began) / 1e9, prepare.picture_frames, prepare.phases[_phase_load],
		prepare.phases[_phase_opening], prepare.ticks, prepare.phases[_phase_scene], prepare.phases[_phase_objects],
		prepare.phases[_phase_frame], prepare.phases[_phase_keep], prepare.phases[_phase_after]);
	for (phase = 0; phase < NUMBER_OF_PHASES; phase++)
		prepare.totals[phase] += prepare.phases[phase];
}

/* the next picture's map asked for, or the main menu after the last (or a
skip) */
static void prepare_next(
	void)
{
	long pose;

	prepare.capture_wanted = FALSE;
	prepare.captured = FALSE;
	if (prepare.skip_asked)
	{
		long index;

		for (index = prepare.index; index < prepare.count; index++)
			menu_pictures_render_skipped(prepare.poses[index]);
		prepare.skipped = prepare.count - prepare.index;
		prepare.done = prepare.count;
		prepare.index = prepare.count;
	}
	if (prepare.index >= prepare.count)
	{
		platform_log("menu pictures: %ld of %ld made%s in %.2f s (load %.2f, opening %.2f, scene %.2f, objects %.2f, "
			"frame %.2f, kept %.2f, after %.2f)", prepare.made, prepare.count, prepare.skipped ? ", the rest skipped" : "",
			(double)(prepare_now() - prepare.began) / 1e9, prepare.totals[_phase_load], prepare.totals[_phase_opening],
			prepare.totals[_phase_scene], prepare.totals[_phase_objects], prepare.totals[_phase_frame],
			prepare.totals[_phase_keep], prepare.totals[_phase_after]);
		main_queue_map_name(NULL);
		sound_enable(TRUE);
		game_time_set_speed(1.0f);
		main_menu_pictures_end();
		prepare.state = _prepare_menu;
		prepare.frames = 0;
		return;
	}
	pose = prepare.poses[prepare.index];
	csmemset(prepare.phases, 0, sizeof(prepare.phases));
	prepare.picture_began = prepare.phase_began = prepare_now();
	prepare.ticks = 0;
	prepare.quiet_ticks = 0;
	prepare.frames = prepare.picture_frames = 0;
	menu_pictures_render_begin(pose);
	main_menu_pictures_map(menu_picture_poses[pose].scenario);
	prepare.state = _prepare_loading;
}

static void prepare_picture_failed(
	char const *why)
{
	long pose = prepare.poses[prepare.index];

	menu_pictures_render_failed(pose, why);
	prepare_picture_log(pose, "failed");
	prepare.index++;
	prepare.done++;
	prepare_next();
}

/* the pictures to render looked for, as the main menu first shows; and if
there are any, the first begun */
static void prepare_begin(
	void)
{
	long index;

	prepare.count = menu_pictures_wanted(prepare.poses, MAXIMUM_MENU_PICTURES);
	if (prepare.count <= 0)
		return;
	platform_log("menu pictures: preparing %ld at %ld times their size", prepare.count, menu_pictures_scale());
	prepare.began = prepare_now();
	prepare.index = prepare.done = prepare.made = prepare.skipped = 0;
	prepare.skip_asked = FALSE;
	/* (the skip's key held as the menu showed is not a press) */
	prepare.skip_down = TRUE;
	csmemset(prepare.totals, 0, sizeof(prepare.totals));
	for (index = 0; index < prepare.count; index++)
	{
		if (menu_picture_poses[prepare.poses[index]].colour_curve)
			prepare_keep_original(prepare.poses[index]);
		prepare_thumbnail_make(index);
	}
	config_picture_view(TRUE);
	sound_stop_all();
	sound_enable(FALSE);
	prepare.drawn = 1;
	prepare.screen_drawn = 0;
	prepare_next();
}

/* the skip: Escape, the controller's B or Back, or a click */
static void prepare_skip_input(
	void)
{
	struct gamepad_state const *pad = input_get_gamepad_state(0);
	int down = input_key_is_down(_key_escape) ||
		(pad && (pad->buttons[_gamepad_analog_button_b] > 0 || pad->buttons[_gamepad_binary_button_back] > 0));

	if (down && !prepare.skip_down && !prepare.skip_asked && prepare.state != _prepare_menu)
	{
		prepare.skip_asked = TRUE;
		platform_log("menu pictures: skipped");
	}
	prepare.skip_down = down;
	/* (Escape and Start open no pause menu meanwhile) */
	ui_widgets_disable_pause_game(2);
}

static void prepare_update(
	int main_menu_loaded)
{
	struct menu_picture_pose const *picture = &menu_picture_poses[prepare.poses[prepare.index < prepare.count ?
		prepare.index : 0]];
	char line[MAXIMUM_RENDER_LINE];

	switch (prepare.state)
	{
	case _prepare_loading:
		if (!main_menu_loaded && game_in_progress() && !strcasecmp(cache_file_loaded_map_name(), picture->map))
		{
			long next;

			prepare_phase(_phase_load);
			/* (the next map's file decompressed meanwhile) */
			for (next = prepare.index + 1; next < prepare.count; next++)
			{
				char const *scenario = menu_picture_poses[prepare.poses[next]].scenario;

				if (strcasecmp(menu_picture_poses[prepare.poses[next]].map, picture->map))
				{
					main_queue_map_name((char *)scenario);
					break;
				}
			}
			game_time_set_speed(0.0f);
			prepare.state = _prepare_opening;
		}
		else if (++prepare.frames > PREPARE_LOAD_FRAMES)
		{
			prepare_picture_failed("did not load");
		}
		break;
	case _prepare_opening:
	{
		long ticks = 0;
		int opened = FALSE;

		/* (its opening cinematics skipped as the skip button does, else run
		at once: none for PREPARE_OPENING_QUIET ticks, and the scripts' fades
		after them done) */
		while (ticks < PREPARE_TICKS_A_FRAME)
		{
			opened = prepare.ticks >= PREPARE_OPENING_MOST ||
				(prepare.ticks >= PREPARE_OPENING_LEAST && prepare.quiet_ticks >= PREPARE_OPENING_QUIET);
			if (opened)
				break;
			if (cinematic_in_progress())
			{
				prepare.quiet_ticks = 0;
				if (cinematic_can_be_skipped())
				{
					/* (at the next frame's start) */
					main_skip_cinematic();
					break;
				}
			}
			else
			{
				prepare.quiet_ticks++;
			}
			prepare_ticks(1);
			ticks++;
		}
		if (!opened)
			break;
		prepare_phase(_phase_opening);
		{
			static char const *const scene[] =
			{
				"terminal_render 0",
				"show_hud 0",
				"show_hud_help_text 0",
				"hud_clear_messages",
				"ai_erase_all",
				/* (a cinematic still running, as a10's waits for the player: its
				letterbox and title go as the world's time passes) */
				"cinematic_stop",
				"cinematic_show_letterbox 0",
				"object_destroy (unit (list_get (players) 0))",
			};
			short index;

			for (index = 0; index < NUMBEROF(scene); index++)
				prepare_command(scene[index]);
		}
		prepare_ticks(PREPARE_SCENE_TICKS);
		prepare_quiet();
		/* (the first camera set takes the script's camera, a frame later) */
		prepare_command("debug_camera_set \"0 0 0 0 0 0 50\"");
		snprintf(line, sizeof(line), "switch_bsp %ld", picture->bsp);
		prepare_command(line);
		prepare_phase(_phase_scene);
		prepare.state = _prepare_structure;
		break;
	}
	case _prepare_structure:
	{
		char const *commands = picture->commands;

		/* (the structure switched at this frame's start) */
		prepare_command("debug_camera_set \"0 0 0 0 0 0 50\"");
		while (commands && *commands)
		{
			char const *end = strchr(commands, '\n');
			long length = end ? (long)(end - commands) : (long)strlen(commands);

			if (length >= MAXIMUM_RENDER_LINE)
				length = MAXIMUM_RENDER_LINE - 1;
			memcpy(line, commands, length);
			line[length] = 0;
			prepare_command(line);
			commands = end ? end + 1 : NULL;
		}
		/* (a vehicle comes to rest on the ground, a rider takes its seat) */
		if (picture->settle_milliseconds > 0)
			prepare_ticks((picture->settle_milliseconds * TICKS_PER_SECOND + 999) / 1000);
		prepare_quiet();
		snprintf(line, sizeof(line), "debug_camera_set \"%s\"", picture->camera);
		prepare_command(line);
		prepare_phase(_phase_objects);
		prepare.state = _prepare_pose;
		break;
	}
	case _prepare_pose:
		/* (the camera taken: the next frame is kept) */
		prepare.capture_wanted = TRUE;
		prepare.state = _prepare_capture;
		break;
	case _prepare_capture:
		if (prepare.captured || ++prepare.frames > 4)
		{
			long pose = prepare.poses[prepare.index];

			if (!prepare.captured)
			{
				prepare_picture_failed("was not drawn");
				break;
			}
			prepare_picture_log(pose, "made");
			prepare.index++;
			prepare.done++;
			prepare.made++;
			/* (shown at once) */
			prepare.screen_drawn = 0;
			prepare_next();
		}
		break;
	case _prepare_menu:
		if (main_menu_loaded)
		{
			prepare_thumbnails_delete();
			config_picture_view(FALSE);
			prepare.state = _prepare_off;
			prepare.drawn = 0;
			platform_log("menu pictures: the main menu after %.2f s", (double)(prepare_now() - prepare.began) / 1e9);
		}
		break;
	default:
		break;
	}
}

void menu_pictures_frame(
	int main_menu_loaded)
{
	static int looked;
	unsigned long long now;

	if (prepare.state == _prepare_off)
	{
		/* (as the main menu first shows) */
		if (!main_menu_loaded || looked)
			return;
		looked = TRUE;
		prepare_begin();
		if (prepare.state == _prepare_off)
			return;
	}
	else
	{
		prepare.picture_frames++;
		prepare_skip_input();
		prepare_update(main_menu_loaded);
	}
	if (prepare.state == _prepare_off)
		return;
	/* this frame: the game's kept, the screen alone (about 30 a second), or
	none */
	now = prepare_now();
	if (prepare.state == _prepare_capture && prepare.capture_wanted && !prepare.captured)
		prepare.drawn = 2;
	else if (!prepare.screen_drawn || now - prepare.screen_drawn >= PREPARE_SCREEN_INTERVAL)
		prepare.drawn = 1;
	else
		prepare.drawn = 0;
	if (prepare.drawn)
		prepare.screen_drawn = now;
}

int menu_pictures_frame_kept(
	void)
{
	if (prepare.state != _prepare_capture || !prepare.capture_wanted || prepare.captured)
		return FALSE;
	prepare_phase(_phase_frame);
	prepare.capture_wanted = FALSE;
	prepare.captured = menu_pictures_render_kept(prepare.poses[prepare.index]);
	if (!prepare.captured)
		prepare.frames = 1000;
	prepare_phase(_phase_keep);
	return TRUE;
}

/* ---------- the preparing screen */

static void screen_rectangle(
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 color)
{
	rectangle2d rectangle;

	rectangle.x0 = (short)x0;
	rectangle.y0 = (short)y0;
	rectangle.x1 = (short)x1;
	rectangle.y1 = (short)y1;
	draw_quad(&rectangle, color);
}

static void screen_text(
	long font,
	short justification,
	real_argb_color const *color,
	real x0,
	real y0,
	real x1,
	real y1,
	char const *text)
{
	rectangle2d bounds;

	if (font == NONE)
		return;
	bounds.x0 = (short)x0;
	bounds.y0 = (short)y0;
	bounds.x1 = (short)x1;
	bounds.y1 = (short)y1;
	draw_string_set_draw_mode(font, NONE, justification, 0, color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, text);
}

void menu_pictures_screen_draw(
	void)
{
	/* the menus' colours: their blue (#2896FF), white, a dark night blue */
	static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };
	static real_argb_color const grey = { 1.0f, 0.62f, 0.69f, 0.78f };
	pixel32 const background = 0xFF07101C, panel = 0xFF0F1F33, edge = 0xFF1C3654, blue = 0xFF2896FF;
	rectangle2d viewport = render.camera.viewport_bounds;
	real x0 = viewport.x0, y0 = viewport.y0, width = viewport.x1 - viewport.x0, height = viewport.y1 - viewport.y0;
	long title_font, text_font;
	long columns, rows, index;
	real cell_width, cell_height, gap, grid_width, grid_x, grid_y, bar_y, unit;
	char text[128];

	if (prepare.state == _prepare_off || prepare.drawn == 2 || width <= 0 || height <= 0 || !global_scenario_get())
		return;
	title_font = interface_get_tag_index(_interface_font_system);
	text_font = interface_get_tag_index(_interface_font_terminal);
	/* (laid out in a 4:3 frame's units, centred) */
	unit = MIN(width / 640.0f, height / 480.0f);
	screen_rectangle(x0, y0, x0 + width, y0 + height, background);
	screen_text(title_font, 2, &white, x0, y0 + 70.0f * unit, x0 + width, y0 + 110.0f * unit,
		"Preparing menu pictures");
	screen_text(text_font, 2, &grey, x0, y0 + 100.0f * unit, x0 + width, y0 + 130.0f * unit,
		"Drawn once, sharp, from your own maps");
	/* the pictures: as many columns as fit two rows, at the cards' shape */
	columns = prepare.count <= 4 ? prepare.count : (prepare.count + 1) / 2;
	columns = MAX(columns, 1);
	rows = (prepare.count + columns - 1) / columns;
	gap = 8.0f * unit;
	cell_width = MIN((560.0f * unit - gap * (columns - 1)) / columns, 112.0f * unit);
	cell_height = cell_width * MENU_PICTURE_HEIGHT / MENU_PICTURE_WIDTH;
	grid_width = cell_width * columns + gap * (columns - 1);
	grid_x = x0 + (width - grid_width) / 2.0f;
	grid_y = y0 + 150.0f * unit;
	for (index = 0; index < prepare.count; index++)
	{
		long row = index / columns, column = index % columns;
		long in_row = row == rows - 1 ? prepare.count - row * columns : columns;
		real row_x = grid_x + (grid_width - (cell_width * in_row + gap * (in_row - 1))) / 2.0f;
		real x = row_x + column * (cell_width + gap), y = grid_y + row * (cell_height + gap);
		unsigned long levels;
		long pose = prepare.poses[index];
		int current = index == prepare.index && prepare.state != _prepare_menu;

		screen_rectangle(x - unit, y - unit, x + cell_width + unit, y + cell_height + unit, current ? blue : edge);
		screen_rectangle(x, y, x + cell_width, y + cell_height, panel);
		if (prepare.thumbnails_made[index] && menu_pictures_texture(pose, &levels))
		{
			real_rectangle2d clip = { 0.0f, (real)MENU_PICTURE_WIDTH / MENU_PICTURE_FRAME_WIDTH, 0.0f,
				(real)MENU_PICTURE_HEIGHT / MENU_PICTURE_FRAME_HEIGHT };
			point2d point;

			point.x = (short)x;
			point.y = (short)y;
			interface_draw_bitmap(&prepare.thumbnails[index], &point, &clip, cell_width / MENU_PICTURE_WIDTH, 0.0f, 1.0f);
		}
	}
	/* the bar, and how far it is */
	bar_y = grid_y + rows * (cell_height + gap) + 14.0f * unit;
	screen_rectangle(grid_x, bar_y, grid_x + grid_width, bar_y + 4.0f * unit, panel);
	screen_rectangle(grid_x, bar_y, grid_x + grid_width * prepare.done / MAX(prepare.count, 1), bar_y + 4.0f * unit,
		blue);
	if (prepare.state == _prepare_menu)
		snprintf(text, sizeof(text), "%ld of %ld ready", prepare.made, prepare.count);
	else
		snprintf(text, sizeof(text), "%ld of %ld", prepare.done + 1, prepare.count);
	screen_text(text_font, 0, &grey, grid_x, bar_y + 10.0f * unit, grid_x + grid_width, bar_y + 30.0f * unit, text);
	screen_text(text_font, 1, &grey, grid_x, bar_y + 10.0f * unit, grid_x + grid_width, bar_y + 30.0f * unit,
		prepare.skip_asked || prepare.state == _prepare_menu ? "Starting..." : "Esc or B: skip (they keep the originals)");
}
